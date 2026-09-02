#include "pipeline/cache.h"
#include "pipeline/chart_compiler.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/runtime_cache_codec.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace ff7rp::pipeline;

constexpr char kMagic[8] = {'F', '7', 'R', 'P', 'R', 'T', '1', '4'};
constexpr std::uint32_t kFormat = 14;

bool expect(const bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

LoadedSong representative_song() {
    LoadedSong song;
    song.id = "unicode-codec";
    song.cache_key = 0x123456789abcdef0ull;
    song.accepted_chart_input_limit = 512;
    song.published_chart_row_limit = 512;
    song.chart_policy_identity = "disabled";
    song.manifest_digest = 0x9988776655443322ull;
    song.audio.sample_rate = 48000;
    song.audio.channels = 2;
    song.audio.source_frame_count = 96000;
    song.mabf_metadata.logical_source_frames = song.audio.source_frame_count;
    song.mabf_metadata.digest = 7;
    song.mabf_metadata.byte_count = 4096;
    song.mabf_metadata.hca_frame_count = 94;
    song.mabf_metadata.sample_rate = 48000;
    song.mabf_metadata.channels = 2;
    song.mabf_metadata.block_size = 1024;
    song.metronome_first_beat_seconds = -1.0;
    song.metronome_last_beat_seconds = -1.0;
    song.config.schema = "ff7rpianosongs.song.v2";
    song.config.title = "Codec 星 café";
    song.config.bpm = 120.0;
    song.config.difficulty = 3;
    song.config.notes_provided = true;
    song.config.notes = {{0.0, 1.0, "C4", "和音"}, {1.5, 2.0, "E4", ""}};
    if (!compile_chart(song.config, &song.chart).ok()) return {};
    LoadedDifficultyProfile profile;
    profile.config = song.config;
    profile.chart = song.chart;
    profile.diagnostics.selected_actions = 2;
    profile.diagnostics.candidate_actions = 5;
    profile.diagnostics.maximum_window_actions = {1, 2, 3, 4, 5};
    profile.diagnostics.maximum_window_begin_seconds = {0.1, 0.2, 0.3, 0.4, 0.5};
    profile.diagnostics.joint_strain_p95 = 0.75;
    profile.diagnostics.dominant_skill = 2;
    profile.diagnostics.satisfied_route = 1;
    profile.diagnostics.dominant_skill_is_global = true;
    profile.diagnostics.satisfied_route_name = "route-右";
    profile.diagnostics.exposure_decision = "publish";
    profile.diagnostics.exposure_reason = "理由";
    song.difficulty_profiles.push_back(std::move(profile));
    return song;
}

DifficultyProfileDiagnostics comprehensive_diagnostics(const std::size_t base) {
    DifficultyProfileDiagnostics value;
    std::size_t size_value = base;
#define SET_SIZE(field) value.field = ++size_value
    SET_SIZE(selected_actions); SET_SIZE(candidate_actions); SET_SIZE(candidate_frames);
    SET_SIZE(protected_baseline_actions); SET_SIZE(target_rows); SET_SIZE(target_minimum_rows);
    SET_SIZE(target_maximum_rows); SET_SIZE(target_exclusions); SET_SIZE(local_skill_rejections);
    SET_SIZE(retained_actions); SET_SIZE(removed_actions); SET_SIZE(replaced_actions); SET_SIZE(added_actions);
    SET_SIZE(scheduled_rows); SET_SIZE(scheduled_conflicts); SET_SIZE(dropped_actions);
    SET_SIZE(lead_in_rejections); SET_SIZE(audio_duration_rejections); SET_SIZE(strain_rejections);
    for (std::size_t& field : value.maximum_window_actions) field = ++size_value;
    SET_SIZE(maximum_quarter_second_stream_actions); SET_SIZE(maximum_half_second_stream_actions);
    SET_SIZE(maximum_jack_run); SET_SIZE(maximum_reversal_run);
    SET_SIZE(maximum_octave_movements_in_five_seconds); SET_SIZE(maximum_large_reversals_in_five_seconds);
#undef SET_SIZE
    double double_value = static_cast<double>(base) + 0.125;
#define SET_DOUBLE(field) value.field = double_value; double_value += 0.125
    SET_DOUBLE(joint_strain_p95); SET_DOUBLE(joint_strain_peak);
    for (double& field : value.maximum_window_begin_seconds) { field = double_value; double_value += 0.125; }
    SET_DOUBLE(maximum_quarter_second_stream_duration); SET_DOUBLE(maximum_quarter_second_stream_begin_seconds);
    SET_DOUBLE(maximum_half_second_stream_duration); SET_DOUBLE(maximum_half_second_stream_begin_seconds);
    SET_DOUBLE(maximum_jack_begin_seconds); SET_DOUBLE(maximum_reversal_begin_seconds);
    SET_DOUBLE(rapid_movement_p90); SET_DOUBLE(rapid_movement_maximum); SET_DOUBLE(octave_movement_rate);
    SET_DOUBLE(maximum_octave_window_begin_seconds); SET_DOUBLE(maximum_large_reversal_window_begin_seconds);
    SET_DOUBLE(right_fatigue_peak); SET_DOUBLE(right_fatigue_peak_seconds); SET_DOUBLE(left_fatigue_peak);
    SET_DOUBLE(left_fatigue_peak_seconds); SET_DOUBLE(hand_imbalance); SET_DOUBLE(rhythm_irregularity_p90);
    SET_DOUBLE(rhythm_irregularity_maximum); SET_DOUBLE(rhythm_irregularity_peak_seconds);
    SET_DOUBLE(hardest_window_begin_seconds); SET_DOUBLE(hardest_window_end_seconds); SET_DOUBLE(overlap_ratio);
    SET_DOUBLE(maximum_quarter_second_stream_actions_end_seconds);
    SET_DOUBLE(maximum_quarter_second_stream_duration_begin_seconds);
    SET_DOUBLE(maximum_quarter_second_stream_duration_end_seconds);
    SET_DOUBLE(maximum_half_second_stream_actions_end_seconds);
    SET_DOUBLE(maximum_half_second_stream_duration_begin_seconds);
    SET_DOUBLE(maximum_half_second_stream_duration_end_seconds);
    SET_DOUBLE(rapid_movement_maximum_seconds); SET_DOUBLE(satisfied_route_ratio); SET_DOUBLE(satisfied_route_margin);
#undef SET_DOUBLE
    value.dominant_skill = static_cast<int>(base + 301);
    value.satisfied_route = static_cast<int>(base + 302);
    value.satisfied_route_name = "route-" + std::to_string(base);
    value.exposure_decision = "decision-" + std::to_string(base);
    value.exposure_reason = "reason-" + std::to_string(base);
    // Across bases 100/500/900, persisted flag signatures are respectively
    // complete=101, row_limit_exceeded=011, nested_from_previous=110, global=001.
    value.complete = base == 100 || base == 900;
    value.row_limit_exceeded = base == 500 || base == 900;
    value.nested_from_previous = base == 100 || base == 500;
    value.dominant_skill_is_global = base == 900;
    return value;
}

LoadedSong comprehensive_oracle_song() {
    LoadedSong song;
    song.id = "oracle-comprehensive";
    song.cache_key = 0x123456789abcdef0ull;
    song.accepted_chart_input_limit = 512;
    song.published_chart_row_limit = 512;
    song.chart_policy_identity = "disabled";
    song.manifest_digest = 0x9988776655443322ull;
    song.audio.sample_rate = 48000;
    song.audio.channels = 2;
    song.audio.source_frame_count = 96000;
    song.mabf_metadata.logical_source_frames = 96000;
    song.mabf_metadata.digest = 7;
    song.mabf_metadata.byte_count = 4096;
    song.mabf_metadata.hca_frame_count = 94;
    song.mabf_metadata.sample_rate = 48000;
    song.mabf_metadata.channels = 2;
    song.mabf_metadata.block_size = 1024;
    song.metronome_first_beat_seconds = -1.0;
    song.metronome_last_beat_seconds = -1.0;
    song.config.schema = "ff7rpianosongs.song.v2";
    song.config.title = "Oracle comprehensive";
    song.config.bpm = 120.0;
    song.config.difficulty = 3;
    song.config.notes_provided = true;
    Note first{0.0, 1.0, "C4", ""};
    first.group_index = 7;
    first.alternate_monotone = true;
    Note second{0.1, 1.0, "C#4", ""};
    second.group_index = 7;
    Note voiced{1.5, 2.0, "", "pca_C_7"};
    voiced.ignore_sound_pitches = {"As2"};
    voiced.source_chord_pitches = {"C3", "E3", "G3"};
    song.config.notes = {first, second, voiced};
    if (!compile_chart(song.config, &song.chart).ok()) return {};
    LoadedDifficultyProfile profile;
    profile.config = song.config;
    profile.chart = song.chart;
    profile.diagnostics = comprehensive_diagnostics(100);
    song.difficulty_profiles.push_back(std::move(profile));
    DifficultyProfileOmission omission;
    omission.difficulty = 4;
    omission.desired_rows = 77;
    omission.reason = "oracle-omission";
    omission.diagnostics = comprehensive_diagnostics(500);
    omission.witness_notes = {{2.5, 1.25, "G4", "witness"}};
    song.difficulty_profile_omissions.push_back(std::move(omission));
    return song;
}

LoadedSong diagnostic_tail_oracle_song() {
    LoadedSong song = comprehensive_oracle_song();
    song.id = "oracle-tail";
    song.cache_key = 0xfedcba9876543210ull;
    song.accepted_chart_input_limit = 1024;
    song.chart_policy_enabled = true;
    song.chart_policy_generation = 9;
    song.chart_policy_identity = "chart_rows=native512+diagnostic1024;extended=verified";
    song.difficulty_profile_omissions.clear();
    SongConfig source = song.config;
    source.title = "Oracle tail";
    source.diagnostic_extended_chart_fixture = true;
    source.notes.clear();
    for (std::size_t index = 0; index < 520; ++index) {
        source.notes.push_back({static_cast<double>(index), 1.0, index % 2 ? "D4" : "C4",
            "tail-" + std::to_string(index)});
    }
    DiagnosticChartRetention diagnostic;
    if (!compile_chart(source, &song.chart, &diagnostic, 520).ok()) return {};
    source.notes.resize(512);
    song.config = source;
    song.difficulty_profiles.clear();
    LoadedDifficultyProfile profile;
    profile.config = source;
    profile.chart = song.chart;
    profile.diagnostics = comprehensive_diagnostics(900);
    profile.diagnostic_chart = std::move(diagnostic);
    profile.diagnostic_chart.descriptor_hash = diagnostic_descriptor_hash(
        song.id, profile.config.difficulty, profile.chart, profile.diagnostic_chart);
    song.difficulty_profiles.push_back(std::move(profile));
    return song;
}

LoadedSong playable_extended_oracle_song(const std::size_t row_count = 520u) {
    LoadedSong song = diagnostic_tail_oracle_song();
    song.chart_policy_identity = kPlayableExtendedChartRowPolicyIdentity;
    song.accepted_chart_input_limit = kMaximumExtendedChartRows;
    song.published_chart_row_limit = kMaximumExtendedChartRows;
    SongConfig complete = song.difficulty_profiles.front().config;
    for (const auto& row : song.difficulty_profiles.front().diagnostic_chart.tail_rows) {
        complete.notes.push_back(row.source);
    }
    for (auto& note : complete.notes) {
        note.chord_id.clear();
        note.pitch = "C4";
    }
    complete.notes.resize(row_count);
    for (std::size_t index = 0; index < complete.notes.size(); ++index) {
        complete.notes[index].beat = static_cast<double>(index);
        complete.notes[index].duration_beats = 1.0;
        complete.notes[index].pitch = "C4";
        complete.notes[index].chord_id.clear();
    }
    DiagnosticChartRetention diagnostic;
    if (!compile_chart(complete, &song.chart, &diagnostic, kMaximumExtendedChartRows).ok()) return {};
    complete.notes.resize(kMaxChartRows);
    song.config = complete;
    auto& profile = song.difficulty_profiles.front();
    profile.config = complete;
    profile.chart = song.chart;
    profile.diagnostic_chart = std::move(diagnostic);
    profile.diagnostic_chart.descriptor_hash = diagnostic_descriptor_hash(
        song.id, profile.config.difficulty, profile.chart, profile.diagnostic_chart);
    return song;
}

bool expect_parent_oracle(
    const LoadedSong& song,
    const std::size_t expected_size,
    const std::uint64_t expected_hash,
    const char* name) {
    std::vector<std::uint8_t> bytes;
    if (!expect(encode_runtime_cache(song, kMagic, kFormat, &bytes), "parent-oracle fixture did not encode")) return false;
    const std::uint64_t hash = fnv1a64_append(kFnv1a64OffsetBasis, bytes.data(), bytes.size());
    if (bytes.size() != expected_size || hash != expected_hash) {
        std::cerr << name << " parent oracle changed: bytes=" << bytes.size() << " hash=0x" << std::hex << hash << '\n';
        return false;
    }
    LoadedSong decoded;
    decoded.id = song.id;
    decoded.cache_key = song.cache_key;
    decoded.accepted_chart_input_limit = song.accepted_chart_input_limit;
    decoded.published_chart_row_limit = song.published_chart_row_limit;
    decoded.chart_policy_enabled = song.chart_policy_enabled;
    decoded.chart_policy_generation = song.chart_policy_generation;
    decoded.chart_policy_identity = song.chart_policy_identity;
    decoded.config = song.config;
    std::vector<std::uint8_t> round_trip;
    return expect(decode_runtime_cache(bytes, kMagic, kFormat, &decoded),
            (std::string(name) + " parent-oracle fixture did not decode").c_str()) &&
        expect(encode_runtime_cache(decoded, kMagic, kFormat, &round_trip), "parent-oracle round-trip did not encode") &&
        expect(round_trip == bytes, "parent-oracle round-trip bytes changed");
}

} // namespace

int main() {
    // Oracle derivation: an archive of parent 24ce7710af2595ca39ad89928381f052f4688c33 was
    // built in a private temporary workspace. A test-only wrapper called the
    // parent's internal write_runtime_cache for these exact value constructors, then hashed
    // runtime.bin with the parent's fnv1a64_append. No extracted-code output supplied these values.
    if (!expect_parent_oracle(comprehensive_oracle_song(), 2760u, 0xd84ffa5ffec85467ull, "comprehensive") ||
        !expect_parent_oracle(diagnostic_tail_oracle_song(), 127386u, 0x0fdf7cbfb6f772f2ull, "diagnostic tail")) return 1;
    const LoadedSong legacy_diagnostic = diagnostic_tail_oracle_song();
    std::vector<std::uint8_t> legacy_diagnostic_bytes;
    LoadedSong legacy_diagnostic_destination;
    legacy_diagnostic_destination.id = legacy_diagnostic.id;
    legacy_diagnostic_destination.cache_key = legacy_diagnostic.cache_key;
    legacy_diagnostic_destination.accepted_chart_input_limit = kMaximumExtendedChartRows;
    legacy_diagnostic_destination.published_chart_row_limit = legacy_diagnostic.published_chart_row_limit;
    legacy_diagnostic_destination.chart_policy_enabled = legacy_diagnostic.chart_policy_enabled;
    legacy_diagnostic_destination.chart_policy_generation = legacy_diagnostic.chart_policy_generation;
    legacy_diagnostic_destination.chart_policy_identity = legacy_diagnostic.chart_policy_identity;
    legacy_diagnostic_destination.config = legacy_diagnostic.config;
    if (!expect(encode_runtime_cache(legacy_diagnostic, kMagic, kFormat, &legacy_diagnostic_bytes),
            "legacy diagnostic cache did not encode")
        || !expect(decode_runtime_cache(
            legacy_diagnostic_bytes, kMagic, kFormat, &legacy_diagnostic_destination),
            "legacy diagnostic cache did not survive the bounded-limit compatibility admission")) return 1;

    LoadedSong playable_513 = playable_extended_oracle_song(513u);
    std::vector<std::uint8_t> playable_513_bytes;
    LoadedSong playable_513_decoded;
    playable_513_decoded.id = playable_513.id;
    playable_513_decoded.cache_key = playable_513.cache_key;
    playable_513_decoded.accepted_chart_input_limit = playable_513.accepted_chart_input_limit;
    playable_513_decoded.published_chart_row_limit = playable_513.published_chart_row_limit;
    playable_513_decoded.chart_policy_enabled = playable_513.chart_policy_enabled;
    playable_513_decoded.chart_policy_generation = playable_513.chart_policy_generation;
    playable_513_decoded.chart_policy_identity = playable_513.chart_policy_identity;
    playable_513_decoded.config = playable_513.config;
    if (!expect(encode_runtime_cache(playable_513, kMagic, kFormat, &playable_513_bytes),
            "exact-513 retained tail did not encode")
        || !expect(decode_runtime_cache(playable_513_bytes, kMagic, kFormat, &playable_513_decoded),
            "exact-513 retained tail did not decode")
        || !expect(playable_513_decoded.difficulty_profiles.front().diagnostic_chart.source_row_count == 513
            && playable_513_decoded.difficulty_profiles.front().diagnostic_chart.tail_rows.size() == 1,
            "exact-513 retained tail changed during cache round trip")) return 1;

    const LoadedSong playable_520 = playable_extended_oracle_song();
    std::vector<std::uint8_t> playable_520_bytes;
    LoadedSong playable_520_decoded;
    playable_520_decoded.id = playable_520.id;
    playable_520_decoded.cache_key = playable_520.cache_key;
    playable_520_decoded.accepted_chart_input_limit = playable_520.accepted_chart_input_limit;
    playable_520_decoded.published_chart_row_limit = playable_520.published_chart_row_limit;
    playable_520_decoded.chart_policy_enabled = playable_520.chart_policy_enabled;
    playable_520_decoded.chart_policy_generation = playable_520.chart_policy_generation;
    playable_520_decoded.chart_policy_identity = playable_520.chart_policy_identity;
    playable_520_decoded.config = playable_520.config;
    if (!expect(encode_runtime_cache(playable_520, kMagic, kFormat, &playable_520_bytes),
            "exact-520 playable tail did not encode")
        || !expect(decode_runtime_cache(playable_520_bytes, kMagic, kFormat, &playable_520_decoded),
            "exact-520 playable tail did not decode")
        || !expect(playable_520_decoded.difficulty_profiles.front().diagnostic_chart.tail_rows.size() == 8,
            "exact-520 playable tail changed during cache round trip")) return 1;
    LoadedSong old_policy_destination = playable_520_decoded;
    old_policy_destination.chart_policy_identity = kDiagnosticChartRowPolicyIdentity;
    if (!expect(!decode_runtime_cache(
            playable_520_bytes, kMagic, kFormat, &old_policy_destination),
            "playable exact-520 cache crossed a diagnostic-only policy identity")) return 1;
    LoadedSong old_exact_policy_destination = playable_520_decoded;
    old_exact_policy_destination.chart_policy_identity =
        "chart_rows=native512+playable513+playable520;extended=verified1005";
    if (!expect(!decode_runtime_cache(
            playable_520_bytes, kMagic, kFormat, &old_exact_policy_destination),
            "old exact-shape playable policy gained broader authority")) return 1;

    const LoadedSong playable_8192 = playable_extended_oracle_song(kMaximumExtendedChartRows);
    std::vector<std::uint8_t> playable_8192_bytes;
    LoadedSong playable_8192_decoded;
    playable_8192_decoded.id = playable_8192.id;
    playable_8192_decoded.cache_key = playable_8192.cache_key;
    playable_8192_decoded.accepted_chart_input_limit = playable_8192.accepted_chart_input_limit;
    playable_8192_decoded.published_chart_row_limit = playable_8192.published_chart_row_limit;
    playable_8192_decoded.chart_policy_enabled = playable_8192.chart_policy_enabled;
    playable_8192_decoded.chart_policy_generation = playable_8192.chart_policy_generation;
    playable_8192_decoded.chart_policy_identity = playable_8192.chart_policy_identity;
    playable_8192_decoded.config = playable_8192.config;
    if (!expect(encode_runtime_cache(playable_8192, kMagic, kFormat, &playable_8192_bytes),
            "8192-row playable tail did not encode")
        || !expect(decode_runtime_cache(
            playable_8192_bytes, kMagic, kFormat, &playable_8192_decoded),
            "8192-row playable tail did not decode")
        || !expect(playable_8192_decoded.difficulty_profiles.front().diagnostic_chart.tail_rows.size()
                == kMaximumExtendedChartTailRows,
            "8192-row playable tail changed during cache round trip")) return 1;
    LoadedSong above_maximum = playable_520;
    above_maximum.difficulty_profiles.front().diagnostic_chart.source_row_count =
        kMaximumExtendedChartRows + 1u;
    std::vector<std::uint8_t> rejected_above_maximum;
    if (!expect(!encode_runtime_cache(above_maximum, kMagic, kFormat, &rejected_above_maximum),
            "8193-row cache shape encoded")) return 1;

    const LoadedSong source = representative_song();
    std::vector<std::uint8_t> bytes;
    if (!expect(!source.id.empty(), "representative chart setup failed") ||
        !expect(encode_runtime_cache(source, kMagic, kFormat, &bytes), "encode failed")) return 1;

    const std::uint64_t hash = fnv1a64_append(kFnv1a64OffsetBasis, bytes.data(), bytes.size());
    if (!expect(bytes.size() == 1660u, "encoded byte count changed") ||
        !expect(hash == 0x5e5f24ba16b3506eull, "encoded byte fixture changed")) {
        std::cerr << "actual bytes=" << bytes.size() << " hash=0x" << std::hex << hash << '\n';
        return 1;
    }

    LoadedSong decoded;
    decoded.id = source.id;
    decoded.cache_key = source.cache_key;
    decoded.accepted_chart_input_limit = source.accepted_chart_input_limit;
    decoded.published_chart_row_limit = source.published_chart_row_limit;
    decoded.chart_policy_identity = source.chart_policy_identity;
    decoded.config = source.config;
    if (!expect(decode_runtime_cache(bytes, kMagic, kFormat, &decoded), "round-trip decode failed") ||
        !expect(decoded.config.title == source.config.title && decoded.config.notes[0].chord_id == source.config.notes[0].chord_id,
            "Unicode fields changed") ||
        !expect(decoded.difficulty_profiles.size() == 1u &&
            decoded.difficulty_profiles[0].diagnostics.maximum_window_actions[4] == 5u &&
            decoded.difficulty_profiles[0].diagnostics.satisfied_route_name == "route-右",
            "profile diagnostics changed") ||
        !expect(!decoded.difficulty_profiles[0].diagnostic_chart.present(), "diagnostic retention state changed")) return 1;

    const auto rejects_without_publication = [&](std::vector<std::uint8_t> candidate, const char* message) {
        LoadedSong destination = decoded;
        destination.config.title = "sentinel";
        const bool rejected = !decode_runtime_cache(candidate, kMagic, kFormat, &destination);
        return expect(rejected && destination.config.title == "sentinel", message);
    };
    std::vector<std::uint8_t> truncated = bytes;
    truncated.resize(24u);
    std::vector<std::uint8_t> truncated_envelope = bytes;
    truncated_envelope.resize(39u);
    std::vector<std::uint8_t> truncated_payload = bytes;
    truncated_payload.resize(bytes.size() - 16u);
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0);
    std::vector<std::uint8_t> corrupt = bytes;
    corrupt[24] ^= 1u;
    std::array<char, 8> wrong_magic{'B', 'A', 'D', 'M', 'A', 'G', 'I', 'C'};
    LoadedSong wrong_identity = decoded;
    wrong_identity.cache_key ^= 1u;
    if (!rejects_without_publication(std::move(truncated), "truncation was accepted or partially published") ||
        !rejects_without_publication(std::move(truncated_envelope), "truncated envelope was accepted") ||
        !rejects_without_publication(std::move(truncated_payload), "truncated payload was accepted") ||
        !rejects_without_publication(std::move(trailing), "trailing bytes were accepted or partially published") ||
        !rejects_without_publication(std::move(corrupt), "corruption was accepted or partially published") ||
        !expect(!decode_runtime_cache(bytes, wrong_magic, kFormat, &wrong_identity), "wrong magic was accepted") ||
        !expect(!decode_runtime_cache(bytes, kMagic, kFormat + 1u, &wrong_identity), "wrong version was accepted")) return 1;

    wrong_identity = decoded;
    wrong_identity.cache_key ^= 1u;
    if (!expect(!decode_runtime_cache(bytes, kMagic, kFormat, &wrong_identity), "cache identity mismatch was accepted")) return 1;

    const auto encoded_rejects = [&](LoadedSong candidate, const char* message) {
        std::vector<std::uint8_t> invalid_bytes;
        if (!encode_runtime_cache(candidate, kMagic, kFormat, &invalid_bytes)) return expect(false, "invalid fixture did not encode");
        LoadedSong destination;
        destination.id = candidate.id;
        destination.cache_key = candidate.cache_key;
        destination.accepted_chart_input_limit = candidate.accepted_chart_input_limit;
        destination.published_chart_row_limit = candidate.published_chart_row_limit;
        destination.chart_policy_enabled = candidate.chart_policy_enabled;
        destination.chart_policy_generation = candidate.chart_policy_generation;
        destination.chart_policy_identity = candidate.chart_policy_identity;
        destination.config = candidate.config;
        return expect(!decode_runtime_cache(invalid_bytes, kMagic, kFormat, &destination), message);
    };
    LoadedSong invalid_semantics = source;
    invalid_semantics.config.title.clear();
    invalid_semantics.difficulty_profiles[0].config.title.clear();
    LoadedSong invalid_enum = source;
    invalid_enum.chart.notes[0].note_type = 99;
    invalid_enum.difficulty_profiles[0].chart.notes[0].note_type = 99;
    LoadedSong invalid_count = source;
    invalid_count.config.notes.resize(8193u, source.config.notes.front());
    invalid_count.difficulty_profiles[0].config.notes = invalid_count.config.notes;
    LoadedSong invalid_length = source;
    invalid_length.config.title.assign((1u << 20u) + 1u, 'x');
    LoadedSong invalid_ignore = comprehensive_oracle_song();
    invalid_ignore.config.notes[2].ignore_sound_pitches = {"Fn2"};
    invalid_ignore.difficulty_profiles[0].config.notes[2].ignore_sound_pitches = {"Fn2"};
    LoadedSong invalid_playable_tail = playable_extended_oracle_song();
    invalid_playable_tail.difficulty_profiles.front().diagnostic_chart.tail_rows[0].compiled.group_index = 1;
    std::vector<std::uint8_t> invalid_playable_tail_bytes;
    if (!expect(!encode_runtime_cache(
            invalid_playable_tail, kMagic, kFormat, &invalid_playable_tail_bytes),
            "invalid playable exact-520 tail encoded")) return 1;
    if (!encoded_rejects(std::move(invalid_semantics), "invalid semantic payload was accepted") ||
        !encoded_rejects(std::move(invalid_enum), "invalid chart enum was accepted") ||
        !encoded_rejects(std::move(invalid_ignore), "invalid IgnoreSound cache mutation was accepted") ||
        !expect(!encode_runtime_cache(invalid_count, kMagic, kFormat, &bytes), "invalid note count was encoded") ||
        !expect(!encode_runtime_cache(invalid_length, kMagic, kFormat, &bytes), "oversized string was encoded")) return 1;
    return 0;
}
