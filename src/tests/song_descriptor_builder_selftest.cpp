#include "song_descriptor_builder.h"

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
        left.group_index == right.group_index;
}

bool profile_equal(const SongDifficultyProfile& left, const SongDifficultyProfile& right)
{
    if (left.title != right.title || left.difficulty != right.difficulty ||
        left.note_count != right.note_count || left.bpm != right.bpm ||
        left.score_thresholds != right.score_thresholds ||
        left.mode_change_combo_counts != right.mode_change_combo_counts ||
        left.chart_notes.size() != right.chart_notes.size() ||
        left.diagnostic_source_rows != right.diagnostic_source_rows ||
        left.diagnostic_native_prefix_rows != right.diagnostic_native_prefix_rows ||
        left.diagnostic_tail_rows != right.diagnostic_tail_rows ||
        left.diagnostic_descriptor_hash != right.diagnostic_descriptor_hash ||
        left.diagnostic_policy_generation != right.diagnostic_policy_generation ||
        left.diagnostic_loaded_from_runtime_cache != right.diagnostic_loaded_from_runtime_cache) {
        return false;
    }
    for (std::size_t i = 0; i < left.chart_notes.size(); ++i) {
        if (!chart_note_equal(left.chart_notes[i], right.chart_notes[i])) return false;
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
        {"1_25", "Cn4", "", 3, 1, 2, 3},
        {"2_50", "", "pca_C", 4, 5, 6, 7},
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
        profile.diagnostic_source_rows = static_cast<std::size_t>(difficulty + 100);
        profile.diagnostic_native_prefix_rows = static_cast<std::size_t>(difficulty + 90);
        profile.diagnostic_tail_rows = 2;
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

} // namespace

int main()
{
    LoadedSong full = full_fixture();
    const SongDescriptor expected = expected_full_descriptor();
    const SongDescriptor actual = ff7r::piano::build_song_descriptor(full, 17);
    if (!descriptor_equal(actual, expected)) {
        return fail("field-complete descriptor golden mismatch");
    }

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
