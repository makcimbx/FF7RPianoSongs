#include "pipeline/cache_manifest_renderer.h"

#include "pipeline/cache.h"
#include "pipeline/chart_compiler.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

using namespace ff7rp::pipeline;

bool expect(const bool condition, const char* message) {
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

DifficultyProfileDiagnostics diagnostics(const std::size_t base) {
    DifficultyProfileDiagnostics value;
    std::size_t count = base;
#define SET_COUNT(field) value.field = ++count
    SET_COUNT(selected_actions); SET_COUNT(candidate_actions); SET_COUNT(candidate_frames);
    SET_COUNT(protected_baseline_actions); SET_COUNT(target_rows); SET_COUNT(target_minimum_rows);
    SET_COUNT(target_maximum_rows); SET_COUNT(target_exclusions); SET_COUNT(local_skill_rejections);
    SET_COUNT(retained_actions); SET_COUNT(removed_actions); SET_COUNT(replaced_actions); SET_COUNT(added_actions);
    SET_COUNT(scheduled_rows); SET_COUNT(scheduled_conflicts); SET_COUNT(dropped_actions);
    SET_COUNT(lead_in_rejections); SET_COUNT(audio_duration_rejections); SET_COUNT(strain_rejections);
    for (std::size_t& field : value.maximum_window_actions) field = ++count;
    SET_COUNT(maximum_quarter_second_stream_actions); SET_COUNT(maximum_half_second_stream_actions);
    SET_COUNT(maximum_jack_run); SET_COUNT(maximum_reversal_run);
    SET_COUNT(maximum_octave_movements_in_five_seconds); SET_COUNT(maximum_large_reversals_in_five_seconds);
#undef SET_COUNT
    double number = static_cast<double>(base) + 0.125;
#define SET_NUMBER(field) value.field = number; number += 0.125
    SET_NUMBER(joint_strain_p95); SET_NUMBER(joint_strain_peak);
    for (double& field : value.maximum_window_begin_seconds) { field = number; number += 0.125; }
    SET_NUMBER(maximum_quarter_second_stream_duration); SET_NUMBER(maximum_quarter_second_stream_begin_seconds);
    SET_NUMBER(maximum_quarter_second_stream_actions_end_seconds);
    SET_NUMBER(maximum_quarter_second_stream_duration_begin_seconds);
    SET_NUMBER(maximum_quarter_second_stream_duration_end_seconds);
    SET_NUMBER(maximum_half_second_stream_duration); SET_NUMBER(maximum_half_second_stream_begin_seconds);
    SET_NUMBER(maximum_half_second_stream_actions_end_seconds);
    SET_NUMBER(maximum_half_second_stream_duration_begin_seconds);
    SET_NUMBER(maximum_half_second_stream_duration_end_seconds);
    SET_NUMBER(maximum_jack_begin_seconds); SET_NUMBER(maximum_reversal_begin_seconds);
    SET_NUMBER(rapid_movement_p90); SET_NUMBER(rapid_movement_maximum); SET_NUMBER(rapid_movement_maximum_seconds);
    SET_NUMBER(octave_movement_rate); SET_NUMBER(maximum_octave_window_begin_seconds);
    SET_NUMBER(maximum_large_reversal_window_begin_seconds); SET_NUMBER(right_fatigue_peak);
    SET_NUMBER(right_fatigue_peak_seconds); SET_NUMBER(left_fatigue_peak); SET_NUMBER(left_fatigue_peak_seconds);
    SET_NUMBER(hand_imbalance); SET_NUMBER(rhythm_irregularity_p90); SET_NUMBER(rhythm_irregularity_maximum);
    SET_NUMBER(rhythm_irregularity_peak_seconds); SET_NUMBER(hardest_window_begin_seconds);
    SET_NUMBER(hardest_window_end_seconds); SET_NUMBER(satisfied_route_ratio); SET_NUMBER(satisfied_route_margin);
    SET_NUMBER(overlap_ratio);
#undef SET_NUMBER
    value.dominant_skill = static_cast<int>(base + 301);
    value.satisfied_route = static_cast<int>(base + 302);
    value.satisfied_route_name = "route:" + std::to_string(base) + ":Ω\nnext";
    value.exposure_decision = "decision;" + std::to_string(base);
    value.exposure_reason = "reason=" + std::to_string(base) + "\tΩ";
    value.complete = base == 100 || base == 900;
    value.row_limit_exceeded = base == 500 || base == 900;
    value.nested_from_previous = base == 100 || base == 500;
    value.dominant_skill_is_global = base == 900;
    return value;
}

LoadedSong comprehensive_song(SongConfig* source_config) {
    LoadedSong song;
    song.id = "manifest-oracle";
    song.audio_source_path = "C:/source/音 song.wav";
    song.midi_source_path = "C:/source/曲.mid";
    song.cache_sidecar_path = "C:/cache/Ω folder/side\ncar.mabf";
    song.chart_from_midi = true;
    song.cache_key = 0x123456789abcdef0ull;
    song.accepted_chart_input_limit = 1024;
    song.published_chart_row_limit = 512;
    song.chart_policy_enabled = true;
    song.chart_policy_generation = 17;
    song.chart_policy_identity = "policy:Ω\nverified";
    song.midi_alignment_confidence = 0.8125;
    song.loudness_gain_applied = true;
    song.loudness_limiter_engaged = false;
    song.loudness_input_lufs = -21.125;
    song.loudness_output_lufs = -12.875;
    song.loudness_input_peak_dbfs = -7.625;
    song.loudness_output_peak_dbfs = -1.125;
    song.loudness_applied_gain_db = 8.25;
    song.gain_envelope_applied = true;
    song.gain_envelope_point_count = 2;
    song.gain_envelope_max_gain_db = 3.75;
    song.gain_envelope_min_gain_db = -2.25;
    song.metronome_beat_count = 23;
    song.metronome_downbeat_count = 6;
    song.metronome_first_beat_seconds = 0.375;
    song.metronome_last_beat_seconds = 11.625;
    song.mabf_metadata = {0x8877665544332211ull, 654321, 120000, 119, 48000, 2, 31, 47, 1024};
    song.audio.sample_rate = 48000;
    song.audio.channels = 2;
    song.audio.source_frame_count = 120000;
    song.config.schema = "ff7rpianosongs.song.v2";
    song.config.title = "Manifest Ω\nline\t=\r";
    song.config.bpm = 123.5;
    song.config.difficulty = 3;
    song.config.score_thresholds = {0, 111, 333, 777};
    song.config.mode_change_combo_counts = {7, 19};
    song.config.midi_audio_alignment_seconds = 0.03125;
    song.config.midi_audio_offset_seconds = -0.0625;
    song.config.midi_minimum_lead_in_seconds = 2.75;
    song.config.loudness_normalization = true;
    song.config.loudness_target_lufs = -12.875;
    song.config.loudness_peak_ceiling_dbfs = -1.125;
    song.config.gain_envelope = {{0.25, -2.25}, {3.5, 3.75}};
    song.config.metronome_enabled = true;
    song.config.metronome_level = 0.1875;
    song.config.metronome_beat_zero_offset_seconds = -0.375;
    song.config.notes_provided = true;
    song.config.notes = {{0.5, 1.25, "C4", "pca_Ω:right"}, {3.75, 0.5, "", "pca_left"}};
    if (!compile_chart(song.config, &song.chart).ok()) return {};

    LoadedDifficultyProfile ordinary;
    ordinary.config = song.config;
    ordinary.chart = song.chart;
    ordinary.diagnostics = diagnostics(100);
    song.difficulty_profiles.push_back(std::move(ordinary));

    LoadedDifficultyProfile middle;
    middle.config = song.config;
    middle.config.difficulty = 5;
    middle.chart = song.chart;
    middle.diagnostics = diagnostics(500);
    song.difficulty_profiles.push_back(std::move(middle));

    SongConfig tail_source = song.config;
    tail_source.title = "Tail Ω";
    tail_source.difficulty = 7;
    tail_source.diagnostic_extended_chart_fixture = true;
    tail_source.notes.clear();
    for (std::size_t index = 0; index < 520; ++index) {
        tail_source.notes.push_back({static_cast<double>(index) * 0.25, 0.125,
            index % 2 == 0 ? "D4" : "", index % 2 == 0 ? "" : "pca_tail_" + std::to_string(index)});
    }
    LoadedDifficultyProfile tail;
    if (!compile_chart(tail_source, &tail.chart, &tail.diagnostic_chart, 520).ok()) return {};
    tail_source.notes.resize(512);
    tail.config = tail_source;
    tail.diagnostics = diagnostics(900);
    tail.diagnostic_chart.descriptor_hash = diagnostic_descriptor_hash(
        song.id, tail.config.difficulty, tail.chart, tail.diagnostic_chart);
    song.difficulty_profiles.push_back(std::move(tail));

    DifficultyProfileOmission omission;
    omission.difficulty = 9;
    omission.desired_rows = 777;
    omission.reason = "omitted:semicolon;colon:\nΩ";
    omission.diagnostics = diagnostics(700);
    omission.witness_notes = {{2.25, 0.75, "F4", "pca_witness/Ω"}, {4.5, 1.5, "", "pca_second"}};
    song.difficulty_profile_omissions.push_back(std::move(omission));

    *source_config = song.config;
    source_config->midi_audio_alignment_provided = true;
    source_config->midi_audio_offset_provided = false;
    return song;
}

LoadedSong empty_song(SongConfig* source_config) {
    LoadedSong song;
    song.chart_policy_identity = "";
    song.config.title = "";
    song.audio.sample_rate = 0;
    song.cache_sidecar_path = "";
    song.audio_source_path = "";
    *source_config = song.config;
    return song;
}

bool check_golden(
    const LoadedSong& song,
    const SongConfig& source_config,
    const std::size_t expected_size,
    const std::uint64_t expected_hash,
    const char* name) {
    std::string manifest;
    const Status status = render_cache_manifest(song, source_config, &manifest);
    if (!status.ok()) {
        std::cerr << name << " failed: " << status.message << '\n';
        return false;
    }
    const std::uint64_t hash = fnv1a64_append(kFnv1a64OffsetBasis, manifest.data(), manifest.size());
    if (manifest.size() == expected_size && hash == expected_hash) return true;
    std::cerr << name << " changed: bytes=" << manifest.size() << " hash=0x" << std::hex << hash << '\n';
    return false;
}

} // namespace

int main() {
    SongConfig source_config;
    const LoadedSong comprehensive = comprehensive_song(&source_config);
    // The oracle includes the generated runtime identity. It was regenerated
    // after the catalog's retained evidence path changed, without changing the
    // cache schema or rendering algorithm.
    if (!check_golden(comprehensive, source_config, 5189u, 0x2c833f4e635fadf6ull, "comprehensive")) return 1;

    SongConfig empty_source;
    const LoadedSong empty = empty_song(&empty_source);
    if (!check_golden(empty, empty_source, 3320u, 0x141cd9e951189931ull, "empty")) return 1;

    LoadedSong nonfinite = empty;
    nonfinite.midi_alignment_confidence = std::numeric_limits<double>::infinity();
    nonfinite.loudness_input_lufs = std::numeric_limits<double>::quiet_NaN();
    if (!check_golden(nonfinite, empty_source, 3321u, 0x435b9dc72afa6db3ull, "nonfinite")) return 1;

    std::string unchanged = "unchanged";
    const Status null_status = render_cache_manifest(comprehensive, source_config, nullptr);
    if (!expect(null_status.code == StatusCode::InvalidArgument &&
            null_status.message == "manifest output must not be null",
            "null output behavior changed")) return 1;
    if (!expect(unchanged == "unchanged", "null output changed unrelated storage")) return 1;

    std::string text;
    if (!render_cache_manifest(comprehensive, source_config, &text).ok()) return 1;
    return expect(text.find("title=Manifest Ω\nline\t=\r\n") != std::string::npos,
               "Unicode/control title behavior changed") &&
        expect(text.find("profile_omissions=9:777:omitted:semicolon;colon:\nΩ\n") != std::string::npos,
            "omission string behavior changed") &&
        expect(text.find("profile_diagnostic_source_rows=0,0,520\n") != std::string::npos &&
                text.find("profile_diagnostic_tail_rows=0,0,8\n") != std::string::npos,
            "diagnostic-tail metadata changed") &&
        expect(text.find("profile_complete=1,0,1\n") != std::string::npos &&
                text.find("profile_row_limit_exceeded=0,1,1\n") != std::string::npos &&
                text.find("profile_nested_from_previous=1,1,0\n") != std::string::npos,
            "diagnostics boolean signatures changed")
        ? 0 : 1;
}
