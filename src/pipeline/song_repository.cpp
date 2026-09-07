#include "song_repository.h"
#include "song_repository_detail.h"

#include "core/joining_workers.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <filesystem>
#include <functional>
#include <fstream>
#include <cmath>
#include <cstring>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <locale>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>
#include <sstream>
#include <string_view>
#include <thread>

#include "cache.h"
#include "audio_artifact_builder.h"
#include "cache_artifact_writer.h"
#include "cache_manifest_renderer.h"
#include "audio_reader.h"
#include "audio_loudness.h"
#include "audio_metronome.h"
#include "chart_event_plan.h"
#include "chart_compiler.h"
#include "mabf_builder.h"
#include "midi_chart_generator.h"
#include "midi_source_normalizer.h"
#include "native_asset_capabilities.h"
#include "pipeline_limits.h"
#include "resolved_song_renderer.h"
#include "runtime_cache_codec.h"
#include "runtime_artifact_validator.h"
#include "song_json.h"
#include "song_source_template.h"
#include "wav_reader.h"

namespace ff7rp::pipeline {
namespace {

constexpr char kRuntimeCacheMagic[8] = {'F', '7', 'R', 'P', 'R', 'T', '1', '6'};
constexpr std::uint32_t kRuntimeCacheFormat = 16;
constexpr std::uint32_t kRuntimeSongSection = 0x474e4f53u;
constexpr std::uint32_t kMaxRuntimeCacheNotes = 8192;

std::string path_string(const std::filesystem::path& path);
using cache_artifact_writer::clear_last_error;
using cache_artifact_writer::write_binary_file;
using cache_artifact_writer::write_last_error;

std::mutex& song_cache_mutex(const std::string& directory) {
    static std::array<std::mutex, 64> mutexes;
    return mutexes[std::hash<std::string>{}(directory) % mutexes.size()];
}

std::string runtime_cache_path(const std::string& song_directory) {
    return path_string(std::filesystem::path(cache_directory_path(song_directory)) / "runtime.bin");
}

bool write_runtime_cache(const LoadedSong& song) {
    const std::string path = runtime_cache_path(song.directory);
    if (cache_artifact_writer::create_parent_directories(path)) return false;
    std::vector<std::uint8_t> bytes;
    if (!encode_runtime_cache(song, kRuntimeCacheMagic, kRuntimeCacheFormat, &bytes)) return false;
    return cache_artifact_writer::write_binary_file_unreported(path, bytes);
}

bool read_runtime_cache(LoadedSong* song) {
    if (!song) return false;
    std::ifstream in(runtime_cache_path(song->directory), std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamsize size = static_cast<std::streamsize>(in.tellg());
    if (size < 40 || size > static_cast<std::streamsize>(64u << 20u)) return false;
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return false;
    return decode_runtime_cache(bytes, kRuntimeCacheMagic, kRuntimeCacheFormat, song);
}

void report_resolved_song_stage_best_effort(
    const SongLoadTrace& trace, const char* stage) noexcept {
    if (!trace) return;
    try {
        trace(stage);
    } catch (...) {
    }
}

void publish_resolved_song_best_effort(
    const LoadedSong& song, const bool source_declared_profiles, const SongLoadTrace& trace) noexcept {
    const char* exception_stage = "resolved_song_render_failed";
    try {
        std::string rendered;
        const Status render_status = render_resolved_song_json(song, source_declared_profiles, &rendered);
        if (!render_status.ok()) {
            report_resolved_song_stage_best_effort(trace, "resolved_song_render_failed");
            return;
        }
        exception_stage = "resolved_song_write_failed";
        const std::vector<std::uint8_t> bytes(rendered.begin(), rendered.end());
        const Status write_status = write_binary_file(resolved_song_json_path(song.directory), bytes);
        report_resolved_song_stage_best_effort(
            trace, write_status.ok() ? "resolved_song_write_ready" : "resolved_song_write_failed");
    } catch (...) {
        report_resolved_song_stage_best_effort(trace, exception_stage);
    }
}

int round_to_hundred(const int value) {
    return ((value + 50) / 100) * 100;
}

void finalize_gameplay_metadata(SongConfig& config) {
    int scoring_actions = 0;
    std::uint8_t previous_group = 0;
    for (const Note& note : config.notes) {
        const bool continuation = note.group_index != 0 && note.group_index == previous_group;
        if (!continuation) scoring_actions += static_cast<int>(!note.pitch.empty())
            + static_cast<int>(!note.chord_id.empty());
        previous_group = note.group_index;
    }
    scoring_actions = std::max(1, scoring_actions);

    if (!config.score_thresholds_provided) {
        const int excellent = scoring_actions * 100;
        config.score_thresholds = {
            0,
            round_to_hundred(scoring_actions * 70),
            round_to_hundred(scoring_actions * 85),
            excellent,
        };
    }
    if (!config.mode_change_combo_counts_provided) {
        const int action_based = std::clamp(static_cast<int>(std::lround(scoring_actions * 0.05)), 5, 15);
        const auto scaled_difficulty = 5LL + 2LL * (static_cast<long long>(config.difficulty) - 1LL);
        const int difficulty_based = static_cast<int>(std::clamp(scaled_difficulty, 5LL, 15LL));
        int first = (action_based + difficulty_based + 1) / 2;
        first = std::clamp(first, 1, std::max(1, scoring_actions / 2));
        const int second = std::min(scoring_actions, std::max(first + 1, first * 2));
        config.mode_change_combo_counts = {first, second};
    }
}

std::string path_string(const std::filesystem::path& path) {
    return path.string();
}

std::string path_utf8_string(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string ascii_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    });
    return value;
}

Status find_audio_sources(const std::filesystem::path& directory, ResolvedAudioSources* out_sources) {
    if (!out_sources) {
        return Status::error(StatusCode::InvalidArgument, "out_sources must not be null");
    }
    ResolvedAudioSources sources;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_error;
        if (!it->is_regular_file(type_error) || type_error) continue;
        const std::string filename = ascii_lower(path_utf8_string(it->path().filename()));
        std::size_t role = 3u;
        if (filename == "song.wav" || filename == "song.mp3" || filename == "song.flac") role = 0u;
        else if (filename == "song.mode1.wav" || filename == "song.mode1.mp3" || filename == "song.mode1.flac") role = 1u;
        else if (filename == "song.mode2.wav" || filename == "song.mode2.mp3" || filename == "song.mode2.flac") role = 2u;
        else if (filename == "song.mode0.wav" || filename == "song.mode0.mp3" || filename == "song.mode0.flac") {
            return Status::error(StatusCode::InvalidAudio,
                "song.mode0.* is not supported; Mode0 always uses the required base source: " + path_string(directory));
        }
        if (role == 3u) continue;
        if (sources.authored[role].present) {
            return Status::error(StatusCode::InvalidAudio,
                "song directory contains multiple supported files for one audio role: " + path_string(directory));
        }
        sources.authored[role].present = true;
        sources.authored[role].path = path_string(it->path());
        sources.authored[role].filename = filename;
    }
    if (ec) {
        return Status::error(StatusCode::IoError,
            "failed to enumerate song audio sources: " + path_string(directory) + ": " + ec.message());
    }
    if (!sources.authored[0].present) {
        return Status::error(StatusCode::NotFound,
            "song directory must contain exactly one required base source song.wav, song.mp3, or song.flac: " +
            path_string(directory));
    }
    sources.resolved_authored_indices = {0u,
        static_cast<std::uint8_t>(sources.authored[1].present ? 1u : 0u),
        static_cast<std::uint8_t>(sources.authored[2].present ? 2u : 0u)};
    *out_sources = std::move(sources);
    return Status::ok_status();
}

Status find_midi_source(const std::filesystem::path& directory, std::filesystem::path* out_path) {
    if (!out_path) {
        return Status::error(StatusCode::InvalidArgument, "out_path must not be null");
    }
    constexpr std::array<std::string_view, 2> kMidiNames{"song.mid", "song.midi"};
    std::vector<std::filesystem::path> found;
    for (const std::string_view name : kMidiNames) {
        const std::filesystem::path candidate = directory / name;
        if (std::filesystem::is_regular_file(candidate)) found.push_back(candidate);
    }
    if (found.empty()) {
        return Status::error(StatusCode::NotFound,
            "song JSON omits notes, so the directory must contain song.mid or song.midi: " + path_string(directory));
    }
    if (found.size() != 1) {
        return Status::error(StatusCode::InvalidMidi,
            "song directory contains both song.mid and song.midi; keep exactly one: " + path_string(directory));
    }
    *out_path = std::move(found.front());
    return Status::ok_status();
}

struct ProfileActionCounts {
    std::size_t right = 0;
    std::size_t left = 0;
    std::size_t dual = 0;
};

ProfileActionCounts profile_action_counts(const SongConfig& config) {
    ProfileActionCounts counts;
    std::uint8_t previous_group = 0;
    for (const Note& note : config.notes) {
        const bool continuation = note.group_index != 0 && note.group_index == previous_group;
        const bool right = !note.pitch.empty() && !continuation;
        const bool left = !note.chord_id.empty() && !continuation;
        counts.right += right ? 1u : 0u;
        counts.left += left ? 1u : 0u;
        counts.dual += right && left ? 1u : 0u;
        previous_group = note.group_index;
    }
    return counts;
}

struct ProfileActionIdentity {
    std::uint64_t beat = 0;
    std::uint64_t duration = 0;
    std::uint8_t hand = 0;
    NativeNoteValue note_value{};
    std::string value;

    bool operator<(const ProfileActionIdentity& other) const {
        return std::tie(beat, duration, hand, note_value.note_type, note_value.dot_type, value) <
            std::tie(other.beat, other.duration, other.hand,
                other.note_value.note_type, other.note_value.dot_type, other.value);
    }
};

struct ProfileActionSlot {
    std::uint64_t beat = 0;
    std::uint64_t duration = 0;
    std::uint8_t hand = 0;

    bool operator<(const ProfileActionSlot& other) const {
        return std::tie(beat, duration, hand) < std::tie(other.beat, other.duration, other.hand);
    }
};

std::uint64_t double_identity(const double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

using ProfileActionMultiset = std::map<ProfileActionIdentity, std::size_t>;
using ProfileSlotMultiset = std::map<ProfileActionSlot, std::size_t>;

void normalized_profile_actions(
    const SongConfig& config,
    ProfileActionMultiset* identities,
    ProfileSlotMultiset* slots) {
    std::uint8_t previous_group = 0;
    for (const Note& note : config.notes) {
        const bool continuation = note.group_index != 0 && note.group_index == previous_group;
        const auto append = [&](const std::uint8_t hand, const std::string& value) {
            const std::uint64_t beat = double_identity(note.beat);
            const std::uint64_t duration = double_identity(note.duration_beats);
            const NativeNoteValue notation = hand == 0
                ? resolved_native_note_value(note.monotone_note_value, note.duration_beats)
                : resolved_native_note_value(note.chord_note_value, note.duration_beats);
            ++(*identities)[{beat, duration, hand, notation, value}];
            ++(*slots)[{beat, duration, hand}];
        };
        if (!note.pitch.empty() && !continuation) append(0, note.pitch);
        if (!note.chord_id.empty() && !continuation) append(1, note.chord_id);
        previous_group = note.group_index;
    }
}

ProfileActionComparison compare_profiles(const SongConfig& previous, const SongConfig& current) {
    ProfileActionMultiset previous_actions;
    ProfileActionMultiset current_actions;
    ProfileSlotMultiset previous_slots;
    ProfileSlotMultiset current_slots;
    normalized_profile_actions(previous, &previous_actions, &previous_slots);
    normalized_profile_actions(current, &current_actions, &current_slots);
    ProfileActionComparison result;
    for (const auto& [identity, count] : previous_actions) {
        const auto found = current_actions.find(identity);
        if (found != current_actions.end()) result.retained += std::min(count, found->second);
    }
    std::size_t shared_slots = 0;
    for (const auto& [slot, count] : previous_slots) {
        const auto found = current_slots.find(slot);
        if (found != current_slots.end()) shared_slots += std::min(count, found->second);
    }
    std::size_t previous_total = 0;
    std::size_t current_total = 0;
    for (const auto& entry : previous_actions) previous_total += entry.second;
    for (const auto& entry : current_actions) current_total += entry.second;
    result.replaced = shared_slots - result.retained;
    result.removed = previous_total - shared_slots;
    result.added = current_total - shared_slots;
    result.overlap = previous_total == 0 ? 1.0 :
        static_cast<double>(result.retained) / static_cast<double>(previous_total);
    result.nested = result.removed == 0 && result.replaced == 0;
    return result;
}

bool runtime_profiles_semantically_valid(const LoadedSong& song) {
    const double duration = song.audio.source_duration_seconds();
    const MabfArtifactMetadata& mabf = song.mabf_metadata;
    if (song.difficulty_profiles.empty() || !std::isfinite(duration) || duration <= 0.0 ||
        mabf.sample_rate != song.audio.sample_rate || mabf.channels != song.audio.channels ||
        mabf.logical_source_frames != song.audio.source_frame_count || mabf.hca_frame_count == 0u ||
        mabf.inserted_samples != 128u || mabf.block_size != 682u ||
        static_cast<std::uint64_t>(mabf.hca_frame_count) * 1024u <
            static_cast<std::uint64_t>(mabf.inserted_samples) + mabf.appended_samples ||
        static_cast<std::uint64_t>(mabf.hca_frame_count) * 1024u - mabf.inserted_samples - mabf.appended_samples !=
            mabf.logical_source_frames || !std::isfinite(song.midi_alignment_confidence) ||
        song.midi_alignment_confidence < 0.0 || song.midi_alignment_confidence > 1.0) return false;

    const bool envelope_expected = !song.config.gain_envelope.empty();
    double envelope_min = 0.0;
    double envelope_max = 0.0;
    if (envelope_expected) {
        envelope_min = song.config.gain_envelope.front().gain_db;
        envelope_max = envelope_min;
        for (const GainEnvelopePoint& point : song.config.gain_envelope) {
            envelope_min = std::min(envelope_min, point.gain_db);
            envelope_max = std::max(envelope_max, point.gain_db);
        }
    }
    const double expected_gain = song.config.loudness_normalization && song.loudness_input_lufs > -99.0 ?
        std::clamp(song.config.loudness_target_lufs - song.loudness_input_lufs, -24.0, 24.0) : 0.0;
    const bool expected_gain_applied = std::fabs(expected_gain) > 0.01;
    if (song.gain_envelope_applied != envelope_expected ||
        song.gain_envelope_point_count != song.config.gain_envelope.size() ||
        song.gain_envelope_min_gain_db != envelope_min || song.gain_envelope_max_gain_db != envelope_max ||
        song.loudness_applied_gain_db != expected_gain ||
        song.loudness_gain_applied != expected_gain_applied ||
        song.loudness_normalized != (song.loudness_gain_applied || song.loudness_limiter_engaged) ||
        song.loudness_input_lufs < -200.0 || song.loudness_input_lufs > 20.0 ||
        song.loudness_output_lufs < -200.0 || song.loudness_output_lufs > 20.0 ||
        song.loudness_input_peak_dbfs < -200.0 || song.loudness_input_peak_dbfs > 40.0 ||
        song.loudness_output_peak_dbfs < -200.0 || song.loudness_output_peak_dbfs > 40.0 ||
        song.loudness_applied_gain_db < -24.0 || song.loudness_applied_gain_db > 24.0) return false;
    if (!song.config.loudness_normalization && (song.loudness_normalized || song.loudness_gain_applied ||
            song.loudness_limiter_engaged || song.loudness_output_lufs != song.loudness_input_lufs ||
            song.loudness_output_peak_dbfs != song.loudness_input_peak_dbfs)) return false;
    if (song.config.loudness_normalization && !song.loudness_limiter_engaged && song.loudness_input_lufs > -99.0 &&
        std::fabs(song.loudness_output_lufs - (song.loudness_input_lufs + expected_gain)) > 0.1) return false;
    if (!song.loudness_limiter_engaged &&
        std::fabs(song.loudness_output_peak_dbfs - (song.loudness_input_peak_dbfs + expected_gain)) > 0.1) return false;
    if (song.loudness_limiter_engaged &&
        song.loudness_output_peak_dbfs > song.config.loudness_peak_ceiling_dbfs + 0.1) return false;

    if (song.config.metronome_enabled) {
        if (song.metronome_beat_count == 0 || song.metronome_downbeat_count > song.metronome_beat_count ||
            song.metronome_first_beat_seconds < 0.0 ||
            song.metronome_last_beat_seconds < song.metronome_first_beat_seconds ||
            song.metronome_last_beat_seconds > duration + 1.0 / 60.0) return false;
    } else if (song.metronome_beat_count != 0 || song.metronome_downbeat_count != 0 ||
        song.metronome_first_beat_seconds != -1.0 || song.metronome_last_beat_seconds != -1.0) {
        return false;
    }

    const auto timing_valid = [&](const SongConfig& config) {
        std::set<std::tuple<std::uint64_t, std::uint64_t, std::string, std::string>> rows;
        for (const Note& note : config.notes) {
            const double seconds = note.beat * 60.0 / config.bpm;
            if (!std::isfinite(seconds) || seconds < 0.0 || seconds > duration + 1.0 / 60.0 ||
                (song.chart_from_midi && seconds + 1.0 / 60.0 < config.midi_minimum_lead_in_seconds) ||
                !rows.emplace(double_identity(note.beat), double_identity(note.duration_beats),
                    note.pitch, note.chord_id).second) return false;
        }
        return true;
    };
    if (!timing_valid(song.config)) return false;
    for (std::size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        const LoadedDifficultyProfile& profile = song.difficulty_profiles[index];
        SongConfig normalized_config = profile.config;
        normalized_config.difficulty = song.config.difficulty;
        normalized_config.score_thresholds = song.config.score_thresholds;
        normalized_config.mode_change_combo_counts = song.config.mode_change_combo_counts;
        normalized_config.notes = song.config.notes;
        normalized_config.diagnostic_extended_chart_fixture = song.config.diagnostic_extended_chart_fixture;
        SongConfig complete_config = profile.config;
        for (const auto& row : profile.diagnostic_chart.tail_rows) complete_config.notes.push_back(row.source);
        const ProfileActionCounts counts = profile_action_counts(complete_config);
        if (song.chart_from_midi) {
            ChartEventPlan event_plan;
            if (!derive_profile_event_plan(profile, &event_plan)
                || event_plan.required_action_count != profile.diagnostics.selected_actions) return false;
        }
        if (profile.diagnostics.selected_actions != counts.right + counts.left ||
            profile.diagnostics.scheduled_rows != complete_config.notes.size() ||
            !song_configs_equal(normalized_config, song.config) ||
            !timing_valid(complete_config) ||
            profile.config.difficulty < 0 ||
            (song.chart_from_midi && (profile.config.difficulty < kLowestMidiDifficulty ||
                profile.config.difficulty > kHighestMidiDifficulty)) ||
            (song.chart_from_midi && (profile.diagnostics.candidate_frames > profile.diagnostics.candidate_actions ||
                (profile.diagnostics.dropped_actions !=
                    profile.diagnostics.candidate_actions - profile.diagnostics.candidate_frames) ||
                profile.diagnostics.selected_actions > profile.diagnostics.candidate_actions ||
                profile.diagnostics.protected_baseline_actions > profile.diagnostics.candidate_actions ||
                profile.diagnostics.target_minimum_rows > profile.diagnostics.target_rows ||
                profile.diagnostics.target_rows > profile.diagnostics.target_maximum_rows ||
                profile.diagnostics.scheduled_conflicts > profile.diagnostics.selected_actions)) ||
            profile.diagnostics.joint_strain_p95 < 0.0 ||
            profile.diagnostics.joint_strain_peak < profile.diagnostics.joint_strain_p95 ||
            !std::isfinite(profile.diagnostics.overlap_ratio) || profile.diagnostics.overlap_ratio < 0.0 ||
            profile.diagnostics.overlap_ratio > 1.0) return false;
        if (song.chart_from_midi && (!profile.diagnostics.complete || profile.diagnostics.row_limit_exceeded ||
            profile.diagnostics.exposure_decision != "visible" ||
            profile.diagnostics.exposure_reason != "complete_feasible_chart")) return false;
        if (song.chart_from_midi) {
            const MidiJointStrainMetrics strain = analyze_midi_joint_strain(complete_config.notes, profile.config.bpm);
            const MidiRouteValidation route = validate_midi_difficulty_route(
                complete_config.notes, profile.config.bpm, profile.config.difficulty);
            const MidiLocalSkillMetrics& local = route.metrics;
            const DifficultyProfileDiagnostics& d = profile.diagnostics;
            if (!route.feasible || d.joint_strain_p95 != strain.p95 || d.joint_strain_peak != strain.peak ||
                d.maximum_window_actions != local.maximum_window_actions ||
                d.maximum_window_begin_seconds != local.maximum_window_begin_seconds ||
                d.maximum_quarter_second_stream_actions != local.maximum_quarter_second_stream_actions ||
                d.maximum_quarter_second_stream_duration != local.maximum_quarter_second_stream_duration ||
                d.maximum_quarter_second_stream_begin_seconds != local.maximum_quarter_second_stream_begin_seconds ||
                d.maximum_quarter_second_stream_actions_end_seconds != local.maximum_quarter_second_stream_actions_end_seconds ||
                d.maximum_quarter_second_stream_duration_begin_seconds != local.maximum_quarter_second_stream_duration_begin_seconds ||
                d.maximum_quarter_second_stream_duration_end_seconds != local.maximum_quarter_second_stream_duration_end_seconds ||
                d.maximum_half_second_stream_actions != local.maximum_half_second_stream_actions ||
                d.maximum_half_second_stream_duration != local.maximum_half_second_stream_duration ||
                d.maximum_half_second_stream_begin_seconds != local.maximum_half_second_stream_begin_seconds ||
                d.maximum_half_second_stream_actions_end_seconds != local.maximum_half_second_stream_actions_end_seconds ||
                d.maximum_half_second_stream_duration_begin_seconds != local.maximum_half_second_stream_duration_begin_seconds ||
                d.maximum_half_second_stream_duration_end_seconds != local.maximum_half_second_stream_duration_end_seconds ||
                d.maximum_jack_run != local.maximum_jack_run || d.maximum_jack_begin_seconds != local.maximum_jack_begin_seconds ||
                d.maximum_reversal_run != local.maximum_reversal_run ||
                d.maximum_reversal_begin_seconds != local.maximum_reversal_begin_seconds ||
                d.rapid_movement_p90 != local.rapid_movement_p90 ||
                d.rapid_movement_maximum != local.rapid_movement_maximum ||
                d.rapid_movement_maximum_seconds != local.rapid_movement_maximum_seconds ||
                d.octave_movement_rate != local.octave_movement_rate ||
                d.maximum_octave_movements_in_five_seconds != local.maximum_octave_movements_in_five_seconds ||
                d.maximum_octave_window_begin_seconds != local.maximum_octave_window_begin_seconds ||
                d.maximum_large_reversals_in_five_seconds != local.maximum_large_reversals_in_five_seconds ||
                d.maximum_large_reversal_window_begin_seconds != local.maximum_large_reversal_window_begin_seconds ||
                d.right_fatigue_peak != local.right_fatigue_peak ||
                d.right_fatigue_peak_seconds != local.right_fatigue_peak_seconds ||
                d.left_fatigue_peak != local.left_fatigue_peak ||
                d.left_fatigue_peak_seconds != local.left_fatigue_peak_seconds ||
                d.hand_imbalance != local.hand_imbalance ||
                d.rhythm_irregularity_p90 != local.rhythm_irregularity_p90 ||
                d.rhythm_irregularity_maximum != local.rhythm_irregularity_maximum ||
                d.rhythm_irregularity_peak_seconds != local.rhythm_irregularity_peak_seconds ||
                d.hardest_window_begin_seconds != local.hardest_window_begin_seconds ||
                d.hardest_window_end_seconds != local.hardest_window_end_seconds ||
                d.dominant_skill != local.dominant_skill || d.satisfied_route != route.route_index ||
                d.satisfied_route_ratio != route.ratio || d.satisfied_route_margin != route.margin ||
                d.dominant_skill_is_global != local.dominant_skill_is_global ||
                d.satisfied_route_name != local.satisfied_route_name) return false;
        }
        CompiledChart expected_complete_chart;
        DiagnosticChartRetention expected_diagnostic;
        if (!compile_chart(complete_config, &expected_complete_chart, &expected_diagnostic,
                complete_config.notes.size()).ok() ||
            expected_diagnostic.tail_rows.size() != profile.diagnostic_chart.tail_rows.size()) return false;
        double previous_tail_beat = profile.config.notes.empty() ? -1.0 : profile.config.notes.back().beat;
        for (std::size_t tail_index = 0; tail_index < profile.diagnostic_chart.tail_rows.size(); ++tail_index) {
            const DiagnosticChartTailRow& row = profile.diagnostic_chart.tail_rows[tail_index];
            const DiagnosticChartTailRow& expected = expected_diagnostic.tail_rows[tail_index];
            const double seconds = row.source.beat * 60.0 / profile.config.bpm;
            if (!std::isfinite(seconds) || seconds < profile.config.midi_minimum_lead_in_seconds - 1.0 / 60.0 ||
                seconds > duration + 1.0 / 60.0 || row.source.beat < previous_tail_beat ||
                row.source_row != expected.source_row ||
                !diagnostic_charts_equal(
                    DiagnosticChartRetention{1u, 1u, {row}, 0u},
                    DiagnosticChartRetention{1u, 1u, {expected}, 0u})) return false;
            previous_tail_beat = row.source.beat;
        }
        if (index == 0) {
            if (profile.diagnostics.overlap_ratio != 1.0 || !profile.diagnostics.nested_from_previous) return false;
            continue;
        }
        SongConfig previous_complete = song.difficulty_profiles[index - 1].config;
        for (const auto& row : song.difficulty_profiles[index - 1].diagnostic_chart.tail_rows)
            previous_complete.notes.push_back(row.source);
        const ProfileActionComparison comparison = compare_profiles(previous_complete, complete_config);
        if (profile.diagnostics.retained_actions != comparison.retained ||
            profile.diagnostics.replaced_actions != comparison.replaced ||
            profile.diagnostics.removed_actions != comparison.removed ||
            profile.diagnostics.added_actions != comparison.added ||
            profile.diagnostics.overlap_ratio != comparison.overlap ||
            profile.diagnostics.nested_from_previous != comparison.nested) return false;
    }
    std::set<int> difficulties;
    for (const LoadedDifficultyProfile& profile : song.difficulty_profiles) {
        if (!difficulties.insert(profile.config.difficulty).second) return false;
    }
    for (const DifficultyProfileOmission& omission : song.difficulty_profile_omissions) {
        if (!difficulties.insert(omission.difficulty).second ||
            omission.difficulty < kLowestMidiDifficulty || omission.difficulty > kHighestMidiDifficulty) return false;
        if (omission.reason == "not_evaluated_after_monotonic_row_limit") {
            if (omission.desired_rows != 0 || !omission.witness_notes.empty() ||
                omission.diagnostics.selected_actions != 0 || omission.diagnostics.candidate_actions != 0) return false;
        } else if (omission.desired_rows == 0) return false;
        if (!omission.witness_notes.empty()) {
            SongConfig witness = song.config;
            witness.notes = omission.witness_notes;
            const ProfileActionCounts counts = profile_action_counts(witness);
            if (!timing_valid(witness) || omission.diagnostics.selected_actions != counts.right + counts.left) return false;
        }
    }
    return true;
}

void copy_midi_diagnostics(const MidiChartStats& stats, DifficultyProfileDiagnostics* diagnostics) {
    diagnostics->selected_actions = stats.selected_actions;
    diagnostics->candidate_actions = stats.candidate_actions;
    diagnostics->candidate_frames = stats.candidate_frames;
    diagnostics->protected_baseline_actions = stats.protected_baseline_actions;
    diagnostics->target_rows = stats.target_rows;
    diagnostics->target_minimum_rows = stats.target_minimum_rows;
    diagnostics->target_maximum_rows = stats.target_maximum_rows;
    diagnostics->target_exclusions = stats.target_exclusions;
    diagnostics->local_skill_rejections = stats.local_skill_rejections;
    diagnostics->retained_actions = stats.retained_actions;
    diagnostics->removed_actions = stats.removed_actions;
    diagnostics->replaced_actions = stats.replaced_actions;
    diagnostics->added_actions = stats.added_actions;
    diagnostics->scheduled_rows = stats.desired_rows;
    diagnostics->scheduled_conflicts = stats.scheduled_conflicts;
    diagnostics->dropped_actions = stats.dropped_conflicts;
    diagnostics->lead_in_rejections = stats.lead_in_rejections;
    diagnostics->audio_duration_rejections = stats.audio_duration_rejections;
    diagnostics->strain_rejections = stats.strain_rejections;
    diagnostics->joint_strain_p95 = stats.joint_strain_p95;
    diagnostics->joint_strain_peak = stats.joint_strain_peak;
    diagnostics->maximum_window_actions = stats.local_skills.maximum_window_actions;
    diagnostics->maximum_window_begin_seconds = stats.local_skills.maximum_window_begin_seconds;
    diagnostics->maximum_quarter_second_stream_actions = stats.local_skills.maximum_quarter_second_stream_actions;
    diagnostics->maximum_quarter_second_stream_duration = stats.local_skills.maximum_quarter_second_stream_duration;
    diagnostics->maximum_quarter_second_stream_begin_seconds = stats.local_skills.maximum_quarter_second_stream_begin_seconds;
    diagnostics->maximum_quarter_second_stream_actions_end_seconds = stats.local_skills.maximum_quarter_second_stream_actions_end_seconds;
    diagnostics->maximum_quarter_second_stream_duration_begin_seconds = stats.local_skills.maximum_quarter_second_stream_duration_begin_seconds;
    diagnostics->maximum_quarter_second_stream_duration_end_seconds = stats.local_skills.maximum_quarter_second_stream_duration_end_seconds;
    diagnostics->maximum_half_second_stream_actions = stats.local_skills.maximum_half_second_stream_actions;
    diagnostics->maximum_half_second_stream_duration = stats.local_skills.maximum_half_second_stream_duration;
    diagnostics->maximum_half_second_stream_begin_seconds = stats.local_skills.maximum_half_second_stream_begin_seconds;
    diagnostics->maximum_half_second_stream_actions_end_seconds = stats.local_skills.maximum_half_second_stream_actions_end_seconds;
    diagnostics->maximum_half_second_stream_duration_begin_seconds = stats.local_skills.maximum_half_second_stream_duration_begin_seconds;
    diagnostics->maximum_half_second_stream_duration_end_seconds = stats.local_skills.maximum_half_second_stream_duration_end_seconds;
    diagnostics->maximum_jack_run = stats.local_skills.maximum_jack_run;
    diagnostics->maximum_jack_begin_seconds = stats.local_skills.maximum_jack_begin_seconds;
    diagnostics->maximum_reversal_run = stats.local_skills.maximum_reversal_run;
    diagnostics->maximum_reversal_begin_seconds = stats.local_skills.maximum_reversal_begin_seconds;
    diagnostics->rapid_movement_p90 = stats.local_skills.rapid_movement_p90;
    diagnostics->rapid_movement_maximum = stats.local_skills.rapid_movement_maximum;
    diagnostics->rapid_movement_maximum_seconds = stats.local_skills.rapid_movement_maximum_seconds;
    diagnostics->octave_movement_rate = stats.local_skills.octave_movement_rate;
    diagnostics->maximum_octave_movements_in_five_seconds = stats.local_skills.maximum_octave_movements_in_five_seconds;
    diagnostics->maximum_octave_window_begin_seconds = stats.local_skills.maximum_octave_window_begin_seconds;
    diagnostics->maximum_large_reversals_in_five_seconds = stats.local_skills.maximum_large_reversals_in_five_seconds;
    diagnostics->maximum_large_reversal_window_begin_seconds = stats.local_skills.maximum_large_reversal_window_begin_seconds;
    diagnostics->right_fatigue_peak = stats.local_skills.right_fatigue_peak;
    diagnostics->right_fatigue_peak_seconds = stats.local_skills.right_fatigue_peak_seconds;
    diagnostics->left_fatigue_peak = stats.local_skills.left_fatigue_peak;
    diagnostics->left_fatigue_peak_seconds = stats.local_skills.left_fatigue_peak_seconds;
    diagnostics->hand_imbalance = stats.local_skills.hand_imbalance;
    diagnostics->rhythm_irregularity_p90 = stats.local_skills.rhythm_irregularity_p90;
    diagnostics->rhythm_irregularity_maximum = stats.local_skills.rhythm_irregularity_maximum;
    diagnostics->rhythm_irregularity_peak_seconds = stats.local_skills.rhythm_irregularity_peak_seconds;
    diagnostics->hardest_window_begin_seconds = stats.local_skills.hardest_window_begin_seconds;
    diagnostics->hardest_window_end_seconds = stats.local_skills.hardest_window_end_seconds;
    diagnostics->dominant_skill = stats.local_skills.dominant_skill;
    diagnostics->satisfied_route = stats.local_skills.satisfied_route;
    diagnostics->satisfied_route_ratio = stats.local_skills.satisfied_route_ratio;
    diagnostics->satisfied_route_margin = stats.local_skills.satisfied_route_margin;
    diagnostics->dominant_skill_is_global = stats.local_skills.dominant_skill_is_global;
    diagnostics->satisfied_route_name = stats.local_skills.satisfied_route_name;
    diagnostics->complete = !stats.row_limit_exceeded;
    diagnostics->row_limit_exceeded = stats.row_limit_exceeded;
}

Status write_cache_manifest(const LoadedSong& song, const SongConfig& source_config) {
    const std::error_code ec = cache_artifact_writer::create_parent_directories(song.cache_manifest_path);
    if (ec) {
        return Status::error(StatusCode::IoError, "failed to create cache manifest directory: " + ec.message());
    }
    std::string manifest;
    Status status = render_cache_manifest(song, source_config, &manifest);
    if (!status.ok()) return status;
    return write_binary_file(song.cache_manifest_path,
        std::vector<std::uint8_t>(manifest.begin(), manifest.end()));
}

Status build_audio_cache(
    LoadedSong* song,
    const SongConfig& source_config,
    const std::array<std::reference_wrapper<const WavAudio>, 3>& resolved_modes,
    const WavAudio* metronome_mode0_audio,
    const SongLoadTrace& trace) {
    const auto report = [&](const char* stage) {
        if (trace) trace(stage);
    };
    if (!song) {
        return Status::error(StatusCode::InvalidArgument, "song must not be null");
    }
    try {
        AudioMabfInputs inputs{resolved_modes, std::nullopt};
        if (metronome_mode0_audio) inputs.mode0_guide = std::cref(*metronome_mode0_audio);
        MabfBuildResult mabf = build_audio_mabf(inputs, trace);
        if (!mabf.status.ok()) {
            return mabf.status;
        }
        if (!mabf.release_valid) {
            return Status::error(StatusCode::MabfNotReleaseValid, mabf.validation_note);
        }
        report("mabf_release_validation_started");
        MabfArtifactMetadata metadata;
        const MabfResolvedModePolicy policy{
            song->audio_sources.resolved_authored_indices, song->config.metronome_enabled};
        Status status = validate_resolved_mabf(
            mabf.bytes, song->audio.source_frame_count, policy, &metadata);
        if (!status.ok()) return status;
        report("mabf_hash_started");
        metadata.digest = fnv1a64_append(kFnv1a64OffsetBasis, mabf.bytes.data(), mabf.bytes.size());
        song->mabf_metadata = metadata;
        report("mabf_write_started");
        status = write_binary_file(song->cache_sidecar_path, mabf.bytes);
        if (!status.ok()) {
            return status;
        }
        report("manifest_write_started");
        status = write_cache_manifest(*song, source_config);
        if (!status.ok()) {
            return status;
        }
        report("manifest_hash_started");
        status = fnv1a64_file(song->cache_manifest_path, &song->manifest_digest);
        if (!status.ok()) return status;
        report("audio_cache_ready");
        return Status::ok_status();
    } catch (const std::exception& error) {
        return Status::error(StatusCode::HcaUnavailable, error.what());
    }
}

} // namespace

namespace detail {

SongCandidateIdentity project_song_candidate_identity(
    const std::filesystem::path& directory,
    std::optional<std::string> legacy_narrow_identity) {
    if (legacy_narrow_identity) {
        return {*legacy_narrow_identity, *legacy_narrow_identity, true};
    }
    const std::string fallback = path_utf8_string(directory.filename());
    return {fallback, fallback, false};
}

SongCandidateIdentity song_candidate_identity(
    const std::filesystem::path& directory) {
    try {
        return project_song_candidate_identity(
            directory, directory.filename().string());
    } catch (const std::system_error&) {
        return project_song_candidate_identity(directory, std::nullopt);
    } catch (const std::range_error&) {
        return project_song_candidate_identity(directory, std::nullopt);
    }
}

void project_song_discovery_setup_failure(
    SongRepositoryResult& result, const bool unknown_exception) {
    result.songs.clear();
    result.errors.clear();
    result.candidates.clear();
    result.discovery_code = SongDiscoveryCode::SetupFailed;
    result.discovery_status = Status::error(
        StatusCode::IoError,
        unknown_exception
            ? "unexpected song discovery setup failure: unknown exception"
            : "unexpected song discovery setup failure");
    result.errors.push_back(result.discovery_status);
}

} // namespace detail

ProfileActionComparison compare_profile_actions(const SongConfig& previous, const SongConfig& current) {
    return compare_profiles(previous, current);
}

Status load_song_directory(
    const std::string& song_directory,
    LoadedSong* out_song,
    SongLoadTrace trace,
    const bool rebuild_invalid_cache,
    SongLoadProgress progress) {
    const auto report = [&](const char* stage) {
        if (trace) trace(stage);
    };
    const auto advance = [&](const SongLoadProgressStage stage) {
        if (progress) progress(stage);
    };
    report("begin");
    advance(SongLoadProgressStage::Inspecting);
    if (!out_song) {
        return Status::error(StatusCode::InvalidArgument, "out_song must not be null");
    }

    const std::filesystem::path directory(song_directory);
    if (!std::filesystem::is_directory(directory)) {
        return Status::error(StatusCode::NotFound, "song directory does not exist: " + song_directory);
    }

    LoadedSong song;
    song.directory = path_string(directory);
    song.id = directory.filename().string();
    song.cache_manifest_path = cache_manifest_path(song.directory);
    song.cache_sidecar_path = cache_sidecar_mabf_path(song.directory);

    const std::string json_path = path_string(directory / "song.json");
    std::filesystem::path midi_path;

    Status status = find_audio_sources(directory, &song.audio_sources);
    if (!status.ok()) {
        song.status = status;
        write_last_error(song.directory, status);
        *out_song = std::move(song);
        return status;
    }
    song.audio_source_path = song.audio_sources.authored[0].path;

    if (!std::filesystem::is_regular_file(directory / "song.json")) {
        status = create_default_song_json(directory);
        if (!status.ok()) {
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
    }
    ParsedSongSource parsed_source;
    status = load_song_json_file(json_path, &parsed_source);
    if (!status.ok()) {
        song.status = status;
        write_last_error(song.directory, status);
        *out_song = std::move(song);
        return status;
    }
    song.config = parsed_source.config;
    report("config_ready");
    advance(SongLoadProgressStage::ValidatingCache);

    std::unique_lock<std::mutex> cache_lock(song_cache_mutex(song.directory));
    ChartRowPolicySnapshot chart_policy = chart_row_policy_snapshot();
    if (!chart_policy.playable_extended_available) {
        chart_policy.accepted_input_limit = kMaxChartRows;
        chart_policy.publication_limit = kMaxChartRows;
    }
    song.accepted_chart_input_limit = chart_policy.accepted_input_limit;
    song.published_chart_row_limit = chart_policy.publication_limit;
    song.chart_policy_enabled = chart_policy.enabled;
    song.chart_policy_generation = chart_policy.generation;
    song.chart_policy_identity = chart_policy.identity();

    if (!song.config.notes_provided) {
        status = find_midi_source(directory, &midi_path);
        if (!status.ok()) {
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
        song.midi_source_path = path_string(midi_path);
        song.chart_from_midi = true;
        if (song.config.metronome_beat_zero_offset_provided) {
            status = Status::error(StatusCode::InvalidJson,
                "metronome.beat_zero_offset_seconds is only valid with explicit JSON notes");
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
    }

    std::vector<std::string> cache_files{json_path};
    if (song.chart_from_midi) cache_files.push_back(song.midi_source_path);
    for (const auto& source : song.audio_sources.authored) {
        if (source.present) cache_files.push_back(source.path);
    }
    std::vector<std::string> cache_identity{
        kPipelineCacheVersion, song.chart_from_midi ? "chart=midi" : "chart=json",
        song.chart_policy_identity,
        std::string(selected_native_asset_capabilities().cache_identity()),
        "audio_processing=gain_envelope_then_loudness_or_limiter:v1",
        song.config.metronome_enabled
            ? "metronome=resolved_mode0_only_before_hca"
            : "metronome=disabled"};
    if (song.chart_from_midi) cache_identity.push_back(kGeneratedMidiGenerationIdentity);
    if (!song.config.chord_voicings.empty())
        cache_identity.push_back("authored_chord_voicing=verified1004+1005:ordered_stock_velocity_slots:v1");
    for (std::size_t role = 0; role < song.audio_sources.authored.size(); ++role) {
        const auto& source = song.audio_sources.authored[role];
        cache_identity.push_back("authored_role=" + std::to_string(role) + ":" +
            (source.present ? source.filename : "absent"));
    }
    cache_identity.push_back("resolved_modes=base," +
        std::string(song.audio_sources.resolved_authored_indices[1] == 0u ? "base_fallback" : "mode1") + "," +
        std::string(song.audio_sources.resolved_authored_indices[2] == 0u ? "base_fallback" : "mode2"));
    status = fnv1a64_files_and_strings(cache_files, cache_identity, &song.cache_key);
    if (!status.ok()) {
        song.status = status;
        write_last_error(song.directory, status);
        *out_song = std::move(song);
        return status;
    }
    report("cache_key_ready");
    const SongConfig source_config = song.config;
    LoadedSong cached_song = song;
    report("runtime_cache_read_started");
    const bool runtime_cache_read = read_runtime_cache(&cached_song);
    report(runtime_cache_read ? "runtime_cache_read" : "runtime_cache_rejected");
    const bool runtime_semantics_valid = runtime_cache_read && runtime_profiles_semantically_valid(cached_song);
    report(runtime_semantics_valid ? "runtime_semantics_valid" : "runtime_semantics_rejected");
    std::string artifact_failure;
    const bool runtime_artifacts_valid = runtime_semantics_valid &&
        runtime_artifacts_match(cached_song, source_config, trace, &artifact_failure);
    if (runtime_artifacts_valid) {
        report("runtime_artifacts_valid");
    } else {
        const std::string stage = "runtime_artifacts_rejected:" +
            (artifact_failure.empty() ? std::string("semantic_prerequisite") : artifact_failure);
        report(stage.c_str());
    }
    if (runtime_artifacts_valid) {
        cached_song.loaded_from_runtime_cache = true;
        publish_resolved_song_best_effort(
            cached_song, !parsed_source.authored_profiles.empty(), trace);
        clear_last_error(cached_song.directory);
        *out_song = std::move(cached_song);
        return Status::ok_status();
    }
    if (!rebuild_invalid_cache) {
        song.status = Status::error(StatusCode::CacheMiss,
            "runtime cache rejected: " +
                (artifact_failure.empty() ? std::string("runtime_or_semantic_validation") : artifact_failure));
        *out_song = std::move(song);
        return out_song->status;
    }

    advance(SongLoadProgressStage::DecodingAudio);
    report("audio_decode_started");
    status = read_audio_file(song.audio_source_path, &song.audio);
    if (!status.ok()) {
        song.status = status;
        write_last_error(song.directory, status);
        *out_song = std::move(song);
        return status;
    }
    report("audio_decode_ready");

    std::array<WavAudio, 2> override_audio;
    for (std::size_t role = 1; role < song.audio_sources.authored.size(); ++role) {
        if (!song.audio_sources.authored[role].present) continue;
        report(role == 1u ? "mode1_audio_decode_started" : "mode2_audio_decode_started");
        status = read_audio_file(song.audio_sources.authored[role].path, &override_audio[role - 1u]);
        if (status.ok() && override_audio[role - 1u].source_frame_count != song.audio.source_frame_count) {
            status = Status::error(StatusCode::InvalidAudio,
                "Mode" + std::to_string(role) + " override logical frame count does not match the required base source");
        }
        if (!status.ok()) {
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
        report(role == 1u ? "mode1_audio_decode_ready" : "mode2_audio_decode_ready");
    }

    advance(SongLoadProgressStage::GeneratingChart);
    if (song.chart_from_midi) {
        report("midi_generation_started");
        song.difficulty_profiles.reserve(kHighestMidiDifficulty);
        NormalizedMidiSource normalized_midi;
        status = normalize_midi_source(song.midi_source_path, &normalized_midi);
        const std::string pitch_warning = midi_pitch_exclusion_warning(normalized_midi);
        if (!pitch_warning.empty()) {
            const std::string stage = "midi_source_warning:" + pitch_warning;
            report(stage.c_str());
        }
        if (!status.ok()) {
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
        for (int difficulty = kLowestMidiDifficulty; difficulty <= kHighestMidiDifficulty; ++difficulty) {
            LoadedDifficultyProfile profile;
            profile.config = song.config;
            profile.config.difficulty = difficulty;
            MidiChartStats stats;
            std::vector<Note> complete_baseline;
            const std::vector<Note>* baseline = nullptr;
            if (!song.difficulty_profiles.empty()) {
                const LoadedDifficultyProfile& previous = song.difficulty_profiles.back();
                complete_baseline = previous.config.notes;
                complete_baseline.reserve(complete_baseline.size()
                    + previous.diagnostic_chart.tail_rows.size());
                for (const DiagnosticChartTailRow& row : previous.diagnostic_chart.tail_rows) {
                    complete_baseline.push_back(row.source);
                }
                baseline = &complete_baseline;
            }
            std::size_t maximum_visible_rows = 0;
            if (!song.difficulty_profiles.empty()) {
                maximum_visible_rows = maximum_midi_visible_profile_actions(
                    song.difficulty_profiles.back().diagnostics.selected_actions);
            }
            status = generate_notes_from_normalized_midi(normalized_midi, song.audio, profile.config,
                &profile.config.notes, &stats, baseline, maximum_visible_rows);
            const std::string selection_stage = "midi_selection:difficulty=" + std::to_string(difficulty) +
                " beam_width=" + std::to_string(stats.selector_beam_width) +
                " processed_frames=" + std::to_string(stats.selector_processed_frames) +
                " skill_row_visits=" + std::to_string(stats.selector_skill_row_visits) +
                " optimization=" + (stats.selector_beam_width < 384 ? "breadth_limited" : "full_breadth") +
                " outcome=" + (status.ok() ? "complete_feasible" : "rejected");
            report(selection_stage.c_str());
            if (status.code == StatusCode::ChartRowLimitExceeded) {
                DifficultyProfileOmission omission;
                omission.difficulty = difficulty;
                omission.desired_rows = stats.desired_rows;
                omission.reason = status.message;
                copy_midi_diagnostics(stats, &omission.diagnostics);
                song.difficulty_profile_omissions.push_back(std::move(omission));
                continue;
            }
            if (status.code == StatusCode::ChartStrainLimitExceeded) {
                DifficultyProfileOmission omission;
                omission.difficulty = difficulty;
                omission.desired_rows = stats.target_rows;
                omission.reason = status.message;
                copy_midi_diagnostics(stats, &omission.diagnostics);
                omission.witness_notes = profile.config.notes;
                song.difficulty_profile_omissions.push_back(std::move(omission));
                continue;
            }
            if (status.ok()) {
                copy_midi_diagnostics(stats, &profile.diagnostics);
                profile.diagnostics.exposure_decision = "visible";
                profile.diagnostics.exposure_reason = "complete_feasible_chart";
                profile.config.midi_audio_alignment_seconds = stats.audio_alignment_seconds;
                profile.config.midi_audio_offset_seconds = stats.audio_offset_seconds;
                profile.config.midi_audio_alignment_provided = true;
                profile.config.midi_audio_offset_provided = true;
                if (difficulty == 1) {
                    song.config.midi_audio_alignment_seconds = stats.audio_alignment_seconds;
                    song.config.midi_audio_offset_seconds = stats.audio_offset_seconds;
                    song.config.midi_audio_alignment_provided = true;
                    song.config.midi_audio_offset_provided = true;
                    song.midi_alignment_confidence = stats.alignment_confidence;
                }
                if (!profile.config.bpm_provided) profile.config.bpm = stats.source_bpm;
                finalize_gameplay_metadata(profile.config);
                if (!profile.config.mode_change_combo_counts_provided) {
                    const auto authored_counts = vanilla_mode_change_counts_for_route(
                        stats.local_skills.satisfied_route_name);
                    if (authored_counts[0] > 0 && authored_counts[1] > authored_counts[0]) {
                        profile.config.mode_change_combo_counts.assign(
                            authored_counts.begin(), authored_counts.end());
                    }
                }
                profile.config.diagnostic_extended_chart_fixture =
                    profile.config.notes.size() > kMaxChartRows;
                status = compile_chart(profile.config, &profile.chart,
                    profile.config.diagnostic_extended_chart_fixture ? &profile.diagnostic_chart : nullptr,
                    song.accepted_chart_input_limit);
                if (status.ok() && profile.diagnostic_chart.present()) {
                    profile.config.notes.resize(kMaxChartRows);
                    profile.diagnostic_chart.descriptor_hash = diagnostic_descriptor_hash(
                        song.id, profile.config.difficulty, profile.chart, profile.diagnostic_chart);
                }
            }
            if (!status.ok()) {
                song.status = status;
                write_last_error(song.directory, status);
                *out_song = std::move(song);
                return status;
            }
            if (!song.difficulty_profiles.empty()) {
                const auto& previous_profile = song.difficulty_profiles.back();
                SongConfig complete_previous = previous_profile.config;
                for (const DiagnosticChartTailRow& row : previous_profile.diagnostic_chart.tail_rows) {
                    complete_previous.notes.push_back(row.source);
                }
                SongConfig complete_current = profile.config;
                for (const DiagnosticChartTailRow& row : profile.diagnostic_chart.tail_rows) {
                    complete_current.notes.push_back(row.source);
                }
                const ProfileActionComparison comparison = compare_profiles(complete_previous, complete_current);
                profile.diagnostics.retained_actions = comparison.retained;
                profile.diagnostics.replaced_actions = comparison.replaced;
                profile.diagnostics.removed_actions = comparison.removed;
                profile.diagnostics.added_actions = comparison.added;
                profile.diagnostics.overlap_ratio = comparison.overlap;
                profile.diagnostics.nested_from_previous = comparison.nested;
            } else {
                profile.diagnostics.overlap_ratio = 1.0;
                profile.diagnostics.nested_from_previous = true;
            }
            song.difficulty_profiles.push_back(std::move(profile));
        }
        if (song.difficulty_profiles.empty()) {
            status = Status::error(StatusCode::InvalidChart, "MIDI generation produced no distinct difficulty profiles");
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
        const size_t default_index = 0;
        song.config = song.difficulty_profiles[default_index].config;
        song.chart = song.difficulty_profiles[default_index].chart;
        report("midi_generation_ready");
    } else {
        report("json_chart_compile_started");
        if (!song.config.bpm_provided) {
            status = Status::error(StatusCode::InvalidJson, "field 'bpm' is required when JSON notes override MIDI");
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
        const std::size_t profile_count = parsed_source.authored_profiles.empty()
            ? 1u : parsed_source.authored_profiles.size();
        song.difficulty_profiles.reserve(profile_count);
        for (std::size_t index = 0; index < profile_count; ++index) {
            LoadedDifficultyProfile profile;
            profile.config = parsed_source.config;
            if (!parsed_source.authored_profiles.empty()) {
                profile.config.difficulty = parsed_source.authored_profiles[index].difficulty;
                profile.config.notes = parsed_source.authored_profiles[index].notes;
                profile.config.notes_provided = true;
            }
            profile.config.diagnostic_extended_chart_fixture =
                profile.config.notes.size() > kMaxChartRows;
            status = compile_chart(profile.config, &profile.chart, &profile.diagnostic_chart,
                song.accepted_chart_input_limit);
            if (!status.ok()) {
                song.status = status;
                write_last_error(song.directory, status);
                *out_song = std::move(song);
                return status;
            }
            finalize_gameplay_metadata(profile.config);
            const ProfileActionCounts counts = profile_action_counts(profile.config);
            profile.diagnostics.selected_actions = counts.right + counts.left;
            profile.diagnostics.scheduled_rows = profile.config.notes.size();
            const MidiJointStrainMetrics strain =
                analyze_midi_joint_strain(profile.config.notes, profile.config.bpm);
            profile.diagnostics.joint_strain_p95 = strain.p95;
            profile.diagnostics.joint_strain_peak = strain.peak;
            if (profile.diagnostic_chart.present()) {
                profile.config.notes.resize(kMaxChartRows);
                profile.diagnostic_chart.descriptor_hash = diagnostic_descriptor_hash(
                    song.id, profile.config.difficulty, profile.chart, profile.diagnostic_chart);
            }
            if (index == 0u) {
                profile.diagnostics.overlap_ratio = 1.0;
                profile.diagnostics.nested_from_previous = true;
            } else {
                SongConfig previous_complete = song.difficulty_profiles.back().config;
                for (const auto& row : song.difficulty_profiles.back().diagnostic_chart.tail_rows)
                    previous_complete.notes.push_back(row.source);
                SongConfig current_complete = profile.config;
                for (const auto& row : profile.diagnostic_chart.tail_rows)
                    current_complete.notes.push_back(row.source);
                const ProfileActionComparison comparison = compare_profiles(
                    previous_complete, current_complete);
                profile.diagnostics.retained_actions = comparison.retained;
                profile.diagnostics.replaced_actions = comparison.replaced;
                profile.diagnostics.removed_actions = comparison.removed;
                profile.diagnostics.added_actions = comparison.added;
                profile.diagnostics.overlap_ratio = comparison.overlap;
                profile.diagnostics.nested_from_previous = comparison.nested;
            }
            song.difficulty_profiles.push_back(std::move(profile));
        }
        song.config = song.difficulty_profiles.front().config;
        song.chart = song.difficulty_profiles.front().chart;
        report("json_chart_compile_ready");
    }

    advance(SongLoadProgressStage::BuildingAudioCache);
    WavAudio metronome_mode0_audio;
    const WavAudio* metronome_mode0_audio_ptr = nullptr;
    if (song.config.metronome_enabled) {
        report("metronome_beats_started");
        std::vector<MetronomeBeat> beats;
        status = build_metronome_beats(
            song.midi_source_path,
            song.chart_from_midi,
            song.audio,
            song.config,
            &beats);
        MetronomeStats metronome_stats;
        const auto process_guide = [&](WavAudio* guide, const double level_scale,
                                       const MetronomeVoice voice,
                                       MetronomeStats* stats) -> Status {
            *guide = song.audio;
            SongConfig guide_config = song.config;
            guide_config.metronome_level *= level_scale;
            Status guide_status = mix_metronome_clicks(guide, guide_config, beats, voice, stats);
            AudioLoudnessStats guide_loudness;
            if (guide_status.ok()) {
                guide_status = normalize_audio_loudness(
                    guide,
                    guide_config.gain_envelope,
                    guide_config.loudness_normalization,
                    guide_config.loudness_target_lufs,
                    guide_config.loudness_peak_ceiling_dbfs,
                    &guide_loudness);
            }
            if (guide_status.ok() && !guide_config.loudness_normalization) {
                bool guide_limiter_engaged = false;
                guide_status = limit_audio_peak(
                    guide, guide_config.loudness_peak_ceiling_dbfs, &guide_limiter_engaged);
            }
            return guide_status;
        };
        if (status.ok()) {
            report("metronome_mode0_processing_started");
            status = process_guide(
                &metronome_mode0_audio, 1.0, MetronomeVoice::Strong, &metronome_stats);
        }
        if (!status.ok()) {
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
        song.metronome_beat_count = metronome_stats.beat_count;
        song.metronome_downbeat_count = metronome_stats.downbeat_count;
        song.metronome_first_beat_seconds = metronome_stats.first_beat_seconds;
        song.metronome_last_beat_seconds = metronome_stats.last_beat_seconds;
        metronome_mode0_audio_ptr = &metronome_mode0_audio;
        report("metronome_guides_ready");
    }

    AudioLoudnessStats loudness;
    report("clean_audio_processing_started");
    status = normalize_audio_loudness(
        &song.audio,
        song.config.gain_envelope,
        song.config.loudness_normalization,
        song.config.loudness_target_lufs,
        song.config.loudness_peak_ceiling_dbfs,
        &loudness);
    if (!status.ok()) {
        song.status = status;
        write_last_error(song.directory, status);
        *out_song = std::move(song);
        return status;
    }
    song.loudness_normalized = loudness.normalized;
    song.loudness_gain_applied = loudness.gain_applied;
    song.loudness_limiter_engaged = loudness.limiter_engaged;
    song.loudness_input_lufs = loudness.input_lufs;
    song.loudness_output_lufs = loudness.output_lufs;
    song.loudness_input_peak_dbfs = loudness.input_peak_dbfs;
    song.loudness_output_peak_dbfs = loudness.output_peak_dbfs;
    song.loudness_applied_gain_db = loudness.applied_gain_db;
    song.gain_envelope_applied = loudness.gain_envelope_applied;
    song.gain_envelope_point_count = loudness.gain_envelope_point_count;
    song.gain_envelope_max_gain_db = loudness.gain_envelope_max_gain_db;
    song.gain_envelope_min_gain_db = loudness.gain_envelope_min_gain_db;
    report("clean_audio_processing_ready");

    for (std::size_t role = 1; role < song.audio_sources.authored.size(); ++role) {
        if (!song.audio_sources.authored[role].present) continue;
        AudioLoudnessStats override_loudness;
        report(role == 1u ? "mode1_audio_processing_started" : "mode2_audio_processing_started");
        status = normalize_audio_loudness(
            &override_audio[role - 1u], song.config.gain_envelope,
            song.config.loudness_normalization, song.config.loudness_target_lufs,
            song.config.loudness_peak_ceiling_dbfs, &override_loudness);
        if (!status.ok()) {
            song.status = status;
            write_last_error(song.directory, status);
            *out_song = std::move(song);
            return status;
        }
        report(role == 1u ? "mode1_audio_processing_ready" : "mode2_audio_processing_ready");
    }

    const std::array<std::reference_wrapper<const WavAudio>, 3> resolved_modes{
        std::cref(song.audio),
        song.audio_sources.resolved_authored_indices[1] == 0u
            ? std::cref(song.audio) : std::cref(override_audio[0]),
        song.audio_sources.resolved_authored_indices[2] == 0u
            ? std::cref(song.audio) : std::cref(override_audio[1])};

    song.status = Status::ok_status();
    song.hca_status = build_audio_cache(
        &song, source_config, resolved_modes, metronome_mode0_audio_ptr, trace);
    if (!song.hca_status.ok()) {
        song.status = song.hca_status;
        write_last_error(song.directory, song.hca_status);
        *out_song = std::move(song);
        return out_song->status;
    }
    advance(SongLoadProgressStage::PublishingCache);
    report("runtime_cache_write_started");
    if (!write_runtime_cache(song)) {
        song.status = Status::error(StatusCode::IoError, "failed to write runtime cache: " + runtime_cache_path(song.directory));
        write_last_error(song.directory, song.status);
        *out_song = std::move(song);
        return out_song->status;
    }
    report("runtime_cache_write_ready");
    publish_resolved_song_best_effort(song, !parsed_source.authored_profiles.empty(), trace);
    clear_last_error(song.directory);
    *out_song = std::move(song);
    return Status::ok_status();
}

SongRepositoryResult discover_songs(
    const std::filesystem::path& music_root, const SongDiscoveryHooks& hooks) {
    constexpr std::size_t kMaxConcurrentSongLoads = 2;
    SongRepositoryResult result;
    try {
        if (hooks.before_setup) hooks.before_setup();
        const std::filesystem::path& root = music_root;
        std::error_code filesystem_error;
        const bool root_is_directory = std::filesystem::is_directory(root, filesystem_error);
        if (filesystem_error == std::errc::no_such_file_or_directory) {
            result.discovery_code = SongDiscoveryCode::MissingRoot;
            result.discovery_status = Status::error(
                StatusCode::NotFound, "Music root does not exist: " + path_utf8_string(root));
            result.errors.push_back(result.discovery_status);
            return result;
        }
        if (filesystem_error) {
            result.discovery_code = SongDiscoveryCode::InspectFailed;
            result.discovery_status = Status::error(
                StatusCode::IoError, "failed to inspect Music root: " + filesystem_error.message());
            result.errors.push_back(result.discovery_status);
            return result;
        }
        if (!root_is_directory) {
            result.discovery_code = SongDiscoveryCode::MissingRoot;
            result.discovery_status = Status::error(
                StatusCode::NotFound, "Music root does not exist: " + path_utf8_string(root));
            result.errors.push_back(result.discovery_status);
            return result;
        }

        struct DiscoveryCandidate {
            std::filesystem::path directory;
            detail::SongCandidateIdentity identity;
        };
        std::vector<DiscoveryCandidate> candidates;
        std::filesystem::directory_iterator iterator(root, filesystem_error);
        const std::filesystem::directory_iterator end;
        while (!filesystem_error && iterator != end) {
            std::error_code entry_error;
            if (iterator->is_directory(entry_error)) {
                std::filesystem::path directory = iterator->path();
                candidates.push_back({directory, detail::song_candidate_identity(directory)});
            }
            if (entry_error) filesystem_error = entry_error;
            if (!filesystem_error) iterator.increment(filesystem_error);
        }
        if (filesystem_error) {
            result.discovery_code = SongDiscoveryCode::EnumerationFailed;
            result.discovery_status = Status::error(
                StatusCode::IoError, "failed to enumerate Music root: " + filesystem_error.message());
            result.errors.push_back(result.discovery_status);
            return result;
        }
        std::sort(candidates.begin(), candidates.end(), [](const DiscoveryCandidate& a, const DiscoveryCandidate& b) {
            return a.identity.lexical_key < b.identity.lexical_key;
        });
        result.enumeration_completed = true;
        result.discovered_candidate_count = candidates.size();
        if (hooks.after_enumeration) hooks.after_enumeration(candidates.size());

        std::vector<SettledSongCandidate> pending(candidates.size());
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            pending[index].directory = candidates[index].directory;
            pending[index].directory_name = candidates[index].identity.directory_name;
        }
        std::vector<bool> settled(candidates.size(), false);
        std::mutex settlement_mutex;
        std::mutex settlement_observer_mutex;
        std::mutex worker_admission_mutex;
        std::condition_variable worker_admission_changed;
        bool workers_admitted = false;
        bool worker_start_failed = false;
        std::size_t emitted_prefix = 0;
        bool settlement_observer_failed = false;
        const auto emit_settled_delta = [&](const std::size_t completed_index) noexcept {
            if (!hooks.on_settled_delta) return;
            std::lock_guard observer_lock(settlement_observer_mutex);
            std::size_t first = 0;
            std::size_t prefix = 0;
            {
                std::lock_guard lock(settlement_mutex);
                settled[completed_index] = true;
                if (settlement_observer_failed) return;
                first = emitted_prefix;
                prefix = first;
                while (prefix < settled.size() && settled[prefix]) ++prefix;
                if (prefix == first) return;
                emitted_prefix = prefix;
            }
            try {
                const SongRepositorySettlementDelta delta{
                    std::span<const SettledSongCandidate>(pending.data() + first, prefix - first),
                    first, candidates.size(), prefix == candidates.size()};
                hooks.on_settled_delta(delta);
            } catch (...) {
                std::lock_guard lock(settlement_mutex);
                settlement_observer_failed = true;
            }
        };
        std::atomic<std::size_t> next_candidate{0};
        const std::size_t worker_count = std::min(kMaxConcurrentSongLoads, candidates.size());
        const auto worker_body = [&](const std::size_t worker_index, const std::atomic<bool>& cancel) {
            {
                std::unique_lock lock(worker_admission_mutex);
                worker_admission_changed.wait(lock, [&] {
                    return workers_admitted || worker_start_failed;
                });
                if (worker_start_failed) return;
            }
            for (;;) {
                if (cancel.load(std::memory_order_acquire)) return;
                const std::size_t index = next_candidate.fetch_add(1, std::memory_order_relaxed);
                if (index >= candidates.size()) {
                    if (hooks.after_worker_body) hooks.after_worker_body(worker_index);
                    return;
                }
                try {
                    if (hooks.before_candidate_load) hooks.before_candidate_load(index);
                    const auto report_progress = [&](const SongLoadProgressStage stage) noexcept {
                        try {
                            if (hooks.on_progress) hooks.on_progress({index, stage});
                        } catch (...) {
                        }
                    };
                    pending[index].status = load_song_directory(
                        path_string(candidates[index].directory), &pending[index].song,
                        [&](const char* stage) {
                            pending[index].trace_stages.emplace_back(stage);
                        }, true, report_progress);
                    if (pending[index].status.ok() &&
                        hooks.audio_retention == SongDiscoveryHooks::AudioRetention::ReleaseDecodedPcmAfterLoad) {
                        std::vector<float>().swap(pending[index].song.audio.stereo_samples);
                    }
                    if (hooks.after_candidate_load) hooks.after_candidate_load(index);
                } catch (const std::exception& error) {
                    pending[index].status = Status::error(
                        StatusCode::IoError, "unexpected song load failure: " + std::string(error.what()));
                } catch (...) {
                    pending[index].status = Status::error(
                        StatusCode::IoError, "unexpected song load failure: unknown exception");
                }
                try {
                    if (hooks.on_progress) hooks.on_progress({index,
                        SongLoadProgressStage::Complete,
                        pending[index].status.ok()
                            ? SongLoadTerminalOutcome::Succeeded
                            : SongLoadTerminalOutcome::Failed});
                } catch (...) {
                }
                emit_settled_delta(index);
            }
        };
        std::size_t next_worker = 0;
        const auto thread_factory = [&](auto&& body) {
            if (hooks.before_worker_start) hooks.before_worker_start(next_worker);
            ++next_worker;
            return std::thread(std::forward<decltype(body)>(body));
        };
        const ff7r::piano::core::JoinedWorkerResult workers =
            ff7r::piano::core::run_joined_workers(worker_count, worker_body, thread_factory,
                [&](const bool admitted) noexcept {
                    {
                        std::lock_guard lock(worker_admission_mutex);
                        workers_admitted = admitted;
                        worker_start_failed = !admitted;
                    }
                    worker_admission_changed.notify_all();
                });
        if (workers.code == ff7r::piano::core::JoinedWorkerCode::StartFailed) {
            result.discovery_code = SongDiscoveryCode::WorkerStartFailed;
            result.discovery_status = Status::error(
                StatusCode::IoError, "failed to start song discovery workers");
            result.errors.push_back(result.discovery_status);
            return result;
        }
        if (workers.code == ff7r::piano::core::JoinedWorkerCode::WorkerFailed) {
            result.discovery_code = SongDiscoveryCode::WorkerFailed;
            result.discovery_status = Status::error(
                StatusCode::IoError, "song discovery worker failed outside a candidate load");
            result.errors.push_back(result.discovery_status);
            return result;
        }
        result.settlement_observer_failed = settlement_observer_failed;

        result.candidates.reserve(candidates.size());
        for (std::size_t index = 0; index < pending.size(); ++index) {
            SettledSongCandidate& load = pending[index];
            SongCandidateResult candidate;
            candidate.directory = load.directory;
            candidate.directory_name = load.directory_name;
            candidate.status = load.status;
            candidate.trace_stages = std::move(load.trace_stages);
            if (load.status.ok()) {
                candidate.loaded_song_index = result.songs.size();
                result.songs.push_back(std::move(load.song));
            } else {
                result.errors.push_back(load.status);
            }
            result.candidates.push_back(std::move(candidate));
        }
    } catch (const std::exception&) {
        detail::project_song_discovery_setup_failure(result, false);
    } catch (...) {
        detail::project_song_discovery_setup_failure(result, true);
    }
    return result;
}

SongRepositoryResult discover_songs(const std::filesystem::path& music_root) {
    return discover_songs(music_root, SongDiscoveryHooks{});
}

SongRepositoryResult discover_songs(
    const std::string& music_root, const SongDiscoveryHooks& hooks) {
    try {
        return discover_songs(std::filesystem::path(music_root), hooks);
    } catch (const std::exception&) {
        SongRepositoryResult result;
        detail::project_song_discovery_setup_failure(result, false);
        return result;
    } catch (...) {
        SongRepositoryResult result;
        detail::project_song_discovery_setup_failure(result, true);
        return result;
    }
}

SongRepositoryResult discover_songs(const std::string& music_root) {
    return discover_songs(music_root, SongDiscoveryHooks{});
}

} // namespace ff7rp::pipeline
