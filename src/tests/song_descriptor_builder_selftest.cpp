#include "song_descriptor_builder.h"
#include "pipeline/diagnostic_descriptor_hash.h"
#include "pipeline/extended_chart_eligibility.h"
#include "pipeline/pipeline_limits.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace {

using ff7r::piano::game::SongChartNote;
using ff7r::piano::game::SongDescriptor;
using ff7r::piano::game::SongDifficultyProfile;
using ff7rp::pipeline::ChartNote;
using ff7rp::pipeline::LoadedDifficultyProfile;
using ff7rp::pipeline::LoadedSong;

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

bool chart_note_equal(const SongChartNote& left, const SongChartNote& right)
{
    return left.time_str == right.time_str &&
        left.monotone_id == right.monotone_id &&
        left.chord_id == right.chord_id &&
        left.note_type == right.note_type &&
        left.dot_type == right.dot_type &&
        left.camera_switch_timing == right.camera_switch_timing &&
        left.group_index == right.group_index &&
        left.ignore_sound_ids == right.ignore_sound_ids;
}

bool profile_equal(const SongDifficultyProfile& left, const SongDifficultyProfile& right)
{
    if (left.title != right.title || left.difficulty != right.difficulty ||
        left.note_count != right.note_count || left.source_row_count != right.source_row_count ||
        left.native_prefix_event_count != right.native_prefix_event_count ||
        left.native_event_count != right.native_event_count ||
        left.required_action_count != right.required_action_count ||
        left.physical_chart_digest != right.physical_chart_digest || left.bpm != right.bpm ||
        left.score_thresholds != right.score_thresholds ||
        left.mode_change_combo_counts != right.mode_change_combo_counts ||
        left.chart_notes.size() != right.chart_notes.size() ||
        left.diagnostic_source_rows != right.diagnostic_source_rows ||
        left.diagnostic_native_prefix_rows != right.diagnostic_native_prefix_rows ||
        left.diagnostic_tail_rows != right.diagnostic_tail_rows ||
        left.diagnostic_descriptor_hash != right.diagnostic_descriptor_hash ||
        left.extended_chart_tail_notes.size() != right.extended_chart_tail_notes.size() ||
        left.diagnostic_policy_generation != right.diagnostic_policy_generation ||
        left.diagnostic_loaded_from_runtime_cache != right.diagnostic_loaded_from_runtime_cache) {
        return false;
    }
    for (std::size_t i = 0; i < left.chart_notes.size(); ++i) {
        if (!chart_note_equal(left.chart_notes[i], right.chart_notes[i])) return false;
    }
    for (std::size_t i = 0; i < left.extended_chart_tail_notes.size(); ++i) {
        if (!chart_note_equal(left.extended_chart_tail_notes[i], right.extended_chart_tail_notes[i])) return false;
    }
    return true;
}

bool descriptor_equal(const SongDescriptor& left, const SongDescriptor& right)
{
    if (left.id != right.id || left.title != right.title ||
        left.visible_index != right.visible_index || left.base_slot != right.base_slot ||
        left.unique_index != right.unique_index || left.difficulty != right.difficulty ||
        left.note_count != right.note_count || left.bpm != right.bpm ||
        left.duration_seconds != right.duration_seconds ||
        left.score_thresholds != right.score_thresholds ||
        left.mode_change_combo_counts != right.mode_change_combo_counts ||
        left.chart_notes.size() != right.chart_notes.size() ||
        left.sidecar_path != right.sidecar_path || left.profiles.size() != right.profiles.size() ||
        left.default_profile_index != right.default_profile_index) {
        return false;
    }
    for (std::size_t i = 0; i < left.chart_notes.size(); ++i) {
        if (!chart_note_equal(left.chart_notes[i], right.chart_notes[i])) return false;
    }
    for (std::size_t i = 0; i < left.profiles.size(); ++i) {
        if (!profile_equal(left.profiles[i], right.profiles[i])) return false;
    }
    return true;
}

ChartNote chart_note(
    std::string time, std::string monotone, std::string chord,
    int note_type, int dot_type, int camera, int group)
{
    ChartNote note;
    note.time_str = std::move(time);
    note.monotone_id = std::move(monotone);
    note.chord_id = std::move(chord);
    note.note_type = note_type;
    note.dot_type = dot_type;
    note.camera_switch_timing = camera;
    note.group_index = group;
    note.ignore_sound_ids = {"En2", "Gn2", ""};
    return note;
}

LoadedDifficultyProfile profile_fixture(int difficulty, double bpm, const ChartNote& note)
{
    LoadedDifficultyProfile profile;
    profile.config.difficulty = difficulty;
    profile.config.bpm = bpm;
    profile.config.score_thresholds = {difficulty, difficulty + 10, difficulty + 20,
        difficulty + 30, difficulty + 40};
    profile.config.mode_change_combo_counts = {difficulty + 1};
    profile.chart.notes = {note};
    profile.diagnostic_chart.source_row_count = static_cast<std::size_t>(difficulty + 100);
    profile.diagnostic_chart.native_prefix_row_count = static_cast<std::size_t>(difficulty + 90);
    profile.diagnostic_chart.tail_rows.resize(2);
    profile.diagnostic_chart.descriptor_hash = 0xabc000u + static_cast<std::uint64_t>(difficulty);
    if (difficulty == 1) {
        profile.diagnostic_chart.source_row_count = 513;
        profile.diagnostic_chart.native_prefix_row_count = 512;
        profile.diagnostic_chart.tail_rows.resize(1);
        profile.diagnostic_chart.tail_rows[0].source_row = 512;
        profile.diagnostic_chart.tail_rows[0].compiled = chart_note("64_0", "Cn4", "", 7, 2, 0, 0);
        profile.diagnostic_chart.tail_rows[0].compiled.ignore_sound_ids = {"", "", ""};
    }
    return profile;
}

LoadedSong full_fixture()
{
    LoadedSong song;
    song.id = "fixture-id";
    song.directory = "ignored-directory";
    song.audio_source_path = "ignored-audio.wav";
    song.midi_source_path = "ignored-chart.mid";
    song.chart_from_midi = true;
    song.loaded_from_runtime_cache = true;
    song.chart_policy_generation = 0x1122334455667788ull;
    song.chart_policy_identity = "ignored-policy-identity";
    song.config.title = "\xC3\x89tude";
    song.config.difficulty = 4;
    song.config.bpm = 120.5;
    song.config.score_thresholds = {11, 22, 33, 44, 55};
    song.config.mode_change_combo_counts = {7};
    song.config.notes = {{2.0, 1.0, "C4", ""}, {4.0, 2.0, "", "pca_C"}};
    song.audio.sample_rate = 48000;
    song.audio.source_frame_count = 192001;
    song.audio.stereo_samples.resize(16);
    song.chart.notes = {
        chart_note("1_25", "Cn4", "", 3, 1, 2, 3),
        chart_note("2_50", "", "pca_C", 4, 5, 6, 7),
    };
    song.cache_key = 0x0102030405060708ull;
    song.cache_manifest_path = "ignored/manifest.txt";
    song.cache_sidecar_path = "cache/\xC3\x89tude.mabf";
    song.mabf_metadata.digest = 0xfedcba9876543210ull;
    song.mabf_metadata.byte_count = 12345;
    song.manifest_digest = 0x8877665544332211ull;
    song.difficulty_profiles = {
        profile_fixture(1, 90.25, song.chart.notes[0]),
        profile_fixture(6, 180.75, song.chart.notes[1]),
    };
    return song;
}

SongDescriptor expected_full_descriptor()
{
    SongDescriptor expected;
    expected.id = "fixture-id";
    expected.title = L"\u00c9tude";
    expected.visible_index = 17;
    expected.base_slot = 0;
    expected.unique_index = 0;
    expected.difficulty = 4;
    expected.note_count = 2;
    expected.bpm = 120.5f;
    expected.duration_seconds = static_cast<float>(192001.0 / 48000.0);
    expected.score_thresholds = {11, 22, 33, 44};
    expected.mode_change_combo_counts = {7, 16};
    expected.chart_notes = {
        {"1_25", "Cn4", "", 3, 1, 2, 3, {"En2", "Gn2", ""}},
        {"2_50", "", "pca_C", 4, 5, 6, 7, {"En2", "Gn2", ""}},
    };
    expected.sidecar_path = L"cache/\u00c9tude.mabf";
    expected.default_profile_index = 0;
    for (const int difficulty : {1, 6}) {
        SongDifficultyProfile profile;
        profile.title = L"\u00c9tude [Lv." + std::to_wstring(difficulty) + L"]";
        profile.difficulty = difficulty;
        profile.note_count = 1;
        profile.bpm = difficulty == 1 ? 90.25f : 180.75f;
        profile.score_thresholds = {difficulty, difficulty + 10, difficulty + 20, difficulty + 30};
        profile.mode_change_combo_counts = {difficulty + 1, 16};
        profile.chart_notes = {difficulty == 1 ? expected.chart_notes[0] : expected.chart_notes[1]};
        profile.diagnostic_source_rows = difficulty == 1 ? 513u : static_cast<std::size_t>(difficulty + 100);
        profile.diagnostic_native_prefix_rows = difficulty == 1 ? 512u : static_cast<std::size_t>(difficulty + 90);
        profile.diagnostic_tail_rows = difficulty == 1 ? 1u : 2u;
        profile.diagnostic_descriptor_hash = 0xabc000u + static_cast<std::uint64_t>(difficulty);
        profile.diagnostic_policy_generation = 0x1122334455667788ull;
        profile.diagnostic_loaded_from_runtime_cache = true;
        expected.profiles.push_back(std::move(profile));
    }
    return expected;
}

SongDescriptor build_after_source_destruction()
{
    LoadedSong local = full_fixture();
    return ff7r::piano::build_song_descriptor(local, 17);
}

LoadedDifficultyProfile extended_profile_fixture(const std::size_t row_count)
{
    LoadedDifficultyProfile profile;
    profile.config.schema = "2";
    profile.config.title = "Extended";
    profile.config.bpm = 120.0;
    profile.config.bpm_provided = true;
    profile.config.difficulty = 1;
    profile.config.diagnostic_extended_chart_fixture = true;
    for (std::size_t index = 0; index < row_count; ++index) {
        ff7rp::pipeline::Note note;
        note.beat = static_cast<double>(index);
        note.duration_beats = 1.0;
        note.pitch = "C4";
        ChartNote compiled;
        compiled.beat = note.beat;
        compiled.duration_beats = note.duration_beats;
        compiled.pitch = note.pitch;
        compiled.time_str = ff7rp::pipeline::expected_compiled_time_string(note.beat, profile.config.bpm);
        compiled.monotone_id = "Cn4";
        compiled.note_type = 3;
        if (index < ff7rp::pipeline::kMaxChartRows) {
            profile.config.notes.push_back(std::move(note));
            profile.chart.notes.push_back(std::move(compiled));
        } else {
            profile.diagnostic_chart.tail_rows.push_back({index, std::move(note), std::move(compiled)});
        }
    }
    profile.diagnostic_chart.source_row_count = row_count;
    profile.diagnostic_chart.native_prefix_row_count = ff7rp::pipeline::kMaxChartRows;
    return profile;
}

LoadedSong extended_song_fixture(const std::size_t row_count)
{
    LoadedSong song;
    song.id = "extended-fixture";
    song.config.title = "Extended";
    song.config.bpm = 120.0;
    song.audio.sample_rate = 48000;
    song.audio.source_frame_count = 48000;
    song.difficulty_profiles.push_back(extended_profile_fixture(row_count));
    auto& profile = song.difficulty_profiles.front();
    profile.diagnostic_chart.descriptor_hash = ff7rp::pipeline::compute_diagnostic_descriptor_hash(
        song.id, profile.config.difficulty, profile.chart, profile.diagnostic_chart);
    const auto policy = ff7rp::pipeline::chart_row_policy_snapshot();
    song.chart_policy_enabled = policy.enabled;
    song.chart_policy_generation = policy.generation;
    song.chart_policy_identity = policy.identity();
    song.accepted_chart_input_limit = policy.accepted_input_limit;
    song.published_chart_row_limit = policy.publication_limit;
    return song;
}

} // namespace

int main()
{
    LoadedSong full = full_fixture();
    const SongDescriptor expected = expected_full_descriptor();
    const SongDescriptor actual = ff7r::piano::build_song_descriptor(full, 17);
    if (!descriptor_equal(actual, expected)) {
        return fail("field-complete descriptor golden mismatch");
    }

    ff7rp::pipeline::configure_chart_row_limit(true, true);
    LoadedSong playable = extended_song_fixture(513);
    SongDescriptor playable_descriptor = ff7r::piano::build_song_descriptor(playable, 17);
    if (playable_descriptor.profiles.front().note_count != 513
        || playable_descriptor.profiles.front().extended_chart_tail_notes.size() != 1) {
        return fail("exact eligible 513 descriptor did not publish its immutable tail");
    }
    LoadedSong playable_520 = extended_song_fixture(520);
    const SongDescriptor playable_520_descriptor = ff7r::piano::build_song_descriptor(playable_520, 17);
    if (playable_520_descriptor.profiles.front().note_count != 520
        || playable_520_descriptor.profiles.front().chart_notes.size() != 512
        || playable_520_descriptor.profiles.front().extended_chart_tail_notes.size() != 8) {
        return fail("exact eligible 520 descriptor did not publish its immutable tail vector");
    }
    LoadedSong warm_520 = playable_520;
    warm_520.loaded_from_runtime_cache = true;
    SongDescriptor warm_520_descriptor = ff7r::piano::build_song_descriptor(warm_520, 17);
    SongDescriptor normalized_cold_520 = playable_520_descriptor;
    normalized_cold_520.profiles.front().diagnostic_loaded_from_runtime_cache = true;
    if (!descriptor_equal(normalized_cold_520, warm_520_descriptor)) {
        return fail("cold/warm exact-520 descriptors differed beyond cache provenance");
    }
    {
        LoadedSong maximum = extended_song_fixture(ff7rp::pipeline::kMaximumExtendedChartRows);
        const auto maximum_profile = ff7r::piano::build_song_descriptor(maximum, 17).profiles.front();
        if (maximum_profile.note_count != static_cast<int>(ff7rp::pipeline::kMaximumExtendedChartRows)
            || maximum_profile.chart_notes.size() != ff7rp::pipeline::kMaxChartRows
            || maximum_profile.extended_chart_tail_notes.size()
                != ff7rp::pipeline::kMaximumExtendedChartTailRows) {
            return fail("8192-row eligible descriptor did not publish its complete owned tail");
        }
    }
    playable.difficulty_profiles.front().chart.notes.front().chord_id = "pca_C";
    if (ff7r::piano::build_song_descriptor(playable, 17).profiles.front().note_count != 512) {
        return fail("ineligible 513 descriptor published a generalized count");
    }
    playable_520.difficulty_profiles.front().diagnostic_chart.tail_rows[1].source_row = 519;
    const auto malformed = ff7r::piano::build_song_descriptor(playable_520, 17).profiles.front();
    if (malformed.note_count != 512 || !malformed.extended_chart_tail_notes.empty()) {
        return fail("noncontiguous 520 tail did not fail closed");
    }
    LoadedSong wrong_shape = extended_song_fixture(520);
    wrong_shape.difficulty_profiles.front().diagnostic_chart.source_row_count = 520;
    wrong_shape.difficulty_profiles.front().diagnostic_chart.tail_rows.resize(7);
    const auto wrong_shape_descriptor = ff7r::piano::build_song_descriptor(wrong_shape, 17).profiles.front();
    if (wrong_shape_descriptor.note_count != 512
        || !wrong_shape_descriptor.extended_chart_tail_notes.empty()) {
        return fail("count/tail mismatch did not fail closed");
    }
    LoadedSong above_maximum = extended_song_fixture(520);
    above_maximum.difficulty_profiles.front().diagnostic_chart.source_row_count =
        ff7rp::pipeline::kMaximumExtendedChartRows + 1u;
    const auto above_maximum_descriptor =
        ff7r::piano::build_song_descriptor(above_maximum, 17).profiles.front();
    if (above_maximum_descriptor.note_count != 512
        || !above_maximum_descriptor.extended_chart_tail_notes.empty()) {
        return fail("8193-row descriptor shape did not fail closed");
    }
    LoadedSong empty_tail = extended_song_fixture(513);
    empty_tail.difficulty_profiles.front().diagnostic_chart.tail_rows.clear();
    const auto empty_tail_descriptor = ff7r::piano::build_song_descriptor(empty_tail, 17).profiles.front();
    if (empty_tail_descriptor.note_count != 512
        || !empty_tail_descriptor.extended_chart_tail_notes.empty()) {
        return fail("empty extended tail did not fail closed");
    }
    LoadedSong hash_mismatch = extended_song_fixture(520);
    hash_mismatch.difficulty_profiles.front().diagnostic_chart.descriptor_hash ^= 1u;
    if (ff7r::piano::build_song_descriptor(hash_mismatch, 17).profiles.front().note_count != 512) {
        return fail("exact-520 descriptor hash mismatch gained publication authority");
    }
    LoadedSong chord_tail = extended_song_fixture(520);
    auto& chord_diagnostic = chord_tail.difficulty_profiles.front().diagnostic_chart;
    chord_diagnostic.tail_rows.front().source.chord_id = "pca_C";
    chord_diagnostic.tail_rows.front().compiled.chord_id = "pca_C";
    chord_diagnostic.descriptor_hash = ff7rp::pipeline::compute_diagnostic_descriptor_hash(
        chord_tail.id, chord_tail.difficulty_profiles.front().config.difficulty,
        chord_tail.difficulty_profiles.front().chart, chord_diagnostic);
    const auto chord_profile = ff7r::piano::build_song_descriptor(chord_tail, 17).profiles.front();
    if (chord_profile.note_count != 521 || chord_profile.native_event_count != 521
        || chord_profile.extended_chart_tail_notes.size() != 8) {
        return fail("dual-hand exact-520 tail did not publish distinct row/event/action counts");
    }
    const auto rejects_tail_mutation = [&](const char* label, const auto& mutate) {
        LoadedSong candidate = extended_song_fixture(520);
        auto& diagnostic = candidate.difficulty_profiles.front().diagnostic_chart;
        mutate(diagnostic.tail_rows.front());
        diagnostic.descriptor_hash = ff7rp::pipeline::compute_diagnostic_descriptor_hash(
            candidate.id, candidate.difficulty_profiles.front().config.difficulty,
            candidate.difficulty_profiles.front().chart, diagnostic);
        return ff7r::piano::build_song_descriptor(candidate, 17).profiles.front().note_count == 512
            ? 0 : fail(std::string(label) + " exact-520 tail gained publication authority");
    };
    if (rejects_tail_mutation("grouped", [](auto& row) {
            row.source.group_index = 1;
            row.compiled.group_index = 1;
        }) != 0
        || rejects_tail_mutation("IgnoreSound", [](auto& row) {
            row.source.ignore_sound_pitches = {"Cn2"};
            row.compiled.ignore_sound_ids = {"Cn2", "", ""};
        }) != 0
        || rejects_tail_mutation("camera-cued", [](auto& row) {
            row.compiled.camera_switch_timing = 1;
        }) != 0) return 1;
    ff7rp::pipeline::configure_chart_row_limit(true, false);
    LoadedSong diagnostic_only = extended_song_fixture(520);
    const auto diagnostic_descriptor = ff7r::piano::build_song_descriptor(diagnostic_only, 17).profiles.front();
    if (diagnostic_descriptor.note_count != 512
        || !diagnostic_descriptor.extended_chart_tail_notes.empty()) {
        return fail("diagnostic-only 520 gained generalized tail authority");
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);

    LoadedSong cache_variant = full;
    cache_variant.cache_key ^= 0xffffu;
    cache_variant.cache_manifest_path = "different/manifest.txt";
    cache_variant.mabf_metadata = {};
    cache_variant.manifest_digest = 0;
    cache_variant.status = ff7rp::pipeline::Status::error(
        ff7rp::pipeline::StatusCode::CacheMiss, "ignored status");
    cache_variant.hca_status = ff7rp::pipeline::Status::ok_status();
    if (!descriptor_equal(
            ff7r::piano::build_song_descriptor(cache_variant, 17), expected)) {
        return fail("non-descriptor cache metadata changed the mapping");
    }

    LoadedSong chart_duration_variant = full;
    chart_duration_variant.config.bpm = 60.0;
    chart_duration_variant.config.notes = {{9.0, 1.0, "C4", ""}};
    chart_duration_variant.audio.source_frame_count = 48000;
    if (ff7r::piano::build_song_descriptor(chart_duration_variant, 17).duration_seconds != 10.0f) {
        return fail("chart duration did not remain authoritative over shorter source audio");
    }

    full.id.clear();
    full.config.title.clear();
    full.config.notes.clear();
    full.chart.notes.clear();
    full.cache_sidecar_path.clear();
    full.difficulty_profiles.clear();
    full.loaded_from_runtime_cache = false;
    full.chart_policy_generation = 0;
    if (!descriptor_equal(actual, expected) ||
        !descriptor_equal(build_after_source_destruction(), expected)) {
        return fail("descriptor did not retain independent owned storage");
    }

    LoadedSong empty;
    empty.config.score_thresholds.clear();
    empty.config.mode_change_combo_counts.clear();
    empty.audio.sample_rate = 0;
    const SongDescriptor empty_actual = ff7r::piano::build_song_descriptor(empty, -9);
    SongDescriptor empty_expected;
    empty_expected.visible_index = -9;
    if (!descriptor_equal(empty_actual, empty_expected)) {
        return fail("empty/default descriptor mapping mismatch");
    }

    LoadedSong single_profile;
    single_profile.config.title = "Solo";
    single_profile.difficulty_profiles.push_back(profile_fixture(
        3, 77.0, chart_note("0_00", "Cn4", "", 3, 0, 0, 0)));
    const SongDescriptor single = ff7r::piano::build_song_descriptor(single_profile, 5);
    if (single.profiles.size() != 1 || single.profiles[0].title != L"Solo") {
        return fail("single-profile title unexpectedly gained a level suffix");
    }

    std::cout << "song_descriptor_builder_selftest ok\n";
    return 0;
}
