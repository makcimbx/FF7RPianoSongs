#pragma once

#include <array>
#include <string_view>

namespace ff7rp::pipeline {

// Closed public song.json object inventories. The parser and packaged-reference
// audit intentionally consume these same arrays.
inline constexpr std::array<std::string_view, 18> kSongJsonRootFields{{
    "schema", "title", "bpm", "difficulty", "score_thresholds",
    "mode_change_combo_counts", "midi_audio_offset_seconds",
    "midi_audio_alignment_seconds", "midi_minimum_lead_in_seconds",
    "loudness_normalization", "loudness_target_lufs",
    "loudness_peak_ceiling_dbfs", "gain_envelope", "metronome", "notes",
    "profiles", "diagnostic_extended_chart_fixture", "chord_voicings",
}};

inline constexpr std::array<std::string_view, 2> kSongJsonProfileFields{{
    "difficulty", "notes",
}};

inline constexpr std::array<std::string_view, 9> kSongJsonNoteFields{{
    "beat", "duration_beats", "pitch", "chord_id", "group_index",
    "monotone_variant", "ignore_sound", "monotone_note_value", "chord_note_value",
}};

inline constexpr std::array<std::string_view, 3> kSongJsonMetronomeFields{{
    "enabled", "level", "beat_zero_offset_seconds",
}};

inline constexpr std::array<std::string_view, 2> kSongJsonGainPointFields{{
    "time_seconds", "gain_db",
}};

} // namespace ff7rp::pipeline
