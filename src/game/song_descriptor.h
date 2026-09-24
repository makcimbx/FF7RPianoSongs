#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ff7r::piano::game {

struct SongChartNote {
    std::string time_str;
    std::string monotone_id;
    std::string chord_id;
    int32_t monotone_note_type = 0;
    int32_t monotone_dot_type = 0;
    int32_t chord_note_type = 0;
    int32_t chord_dot_type = 0;
    int32_t camera_switch_timing = 0;
    int32_t group_index = 0;
    std::array<std::string, 3> ignore_sound_ids{};
};

struct SongDifficultyProfile {
    std::wstring title;
    int difficulty = 1;
    int note_count = 0;
    // Source rows, expanded native events, and required parentless actions are
    // distinct. note_count remains the compatibility required-action value.
    std::size_t source_row_count = 0;
    std::size_t native_prefix_event_count = 0;
    std::size_t native_event_count = 0;
    std::size_t required_action_count = 0;
    std::uint64_t physical_chart_digest = 0;
    float bpm = 0.0f;
    std::array<int32_t, 4> score_thresholds{0, 1200, 2400, 3600};
    std::array<int32_t, 2> mode_change_combo_counts{8, 16};
    std::vector<SongChartNote> chart_notes;
    std::size_t diagnostic_source_rows = 0;
    std::size_t diagnostic_native_prefix_rows = 0;
    std::size_t diagnostic_tail_rows = 0;
    std::uint64_t diagnostic_descriptor_hash = 0;
    std::uint64_t diagnostic_policy_generation = 0;
    bool diagnostic_loaded_from_runtime_cache = false;
    // Immutable owned rows beyond chart_notes' native 512-row prefix.
    std::vector<SongChartNote> extended_chart_tail_notes;
};

// A song's row in the piano list is a runtime fact, not an offline one.
// Catalog adoption resolves this value against the live menu widget.
inline constexpr int kUnresolvedVisibleIndex = -1;

struct SongChordVoicing {
    std::string chord_id;
    std::vector<std::string> sound_ids;
};

struct SongDescriptor {
    std::string id;
    std::wstring title;
    int visible_index = kUnresolvedVisibleIndex;
    int base_slot = 0;
    int unique_index = 0;
    int difficulty = 1;
    int note_count = 0;
    float bpm = 0.0f;
    float duration_seconds = 0.0f;
    std::array<int32_t, 4> score_thresholds{0, 1200, 2400, 3600};
    std::array<int32_t, 2> mode_change_combo_counts{8, 16};
    std::vector<SongChartNote> chart_notes;
    std::wstring sidecar_path;
    std::vector<SongDifficultyProfile> profiles;
    int default_profile_index = 0;
    std::vector<SongChordVoicing> chord_voicings;
};

} // namespace ff7r::piano::game
