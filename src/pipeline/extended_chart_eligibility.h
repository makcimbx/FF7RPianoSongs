#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "pipeline_limits.h"
#include "song_types.h"

namespace ff7rp::pipeline {

inline bool valid_compiled_time_string(const std::string& value)
{
    const std::size_t separator = value.find('_');
    if (separator == std::string::npos || separator == 0u || separator + 3u != value.size()) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index != separator && !std::isdigit(static_cast<unsigned char>(value[index]))) return false;
    }
    return (value[separator + 1] - '0') * 10 + value[separator + 2] - '0' < 60;
}

inline std::string expected_compiled_time_string(const double beat, const double bpm)
{
    if (!std::isfinite(beat) || !std::isfinite(bpm) || bpm <= 0.0) return {};
    const long long frames = static_cast<long long>(std::llround(beat * 3600.0 / bpm));
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << frames / 60
        << '_' << std::setw(2) << std::setfill('0') << frames % 60;
    return out.str();
}

inline bool restricted_extended_source_note(const Note& note)
{
    return std::isfinite(note.beat) && note.beat >= 0.0
        && std::isfinite(note.duration_beats) && note.duration_beats > 0.0
        && !note.pitch.empty() && note.chord_id.empty() && note.group_index == 0
        && note.ignore_sound_pitches.empty() && note.source_chord_pitches.empty();
}

inline bool restricted_extended_compiled_note(const ChartNote& note)
{
    return std::isfinite(note.beat) && note.beat >= 0.0
        && std::isfinite(note.duration_beats) && note.duration_beats > 0.0
        && !note.pitch.empty() && !note.monotone_id.empty() && note.chord_id.empty()
        && valid_compiled_time_string(note.time_str)
        && note.note_type == (note.duration_beats >= 2.0 ? 2 : 3)
        && note.group_index == 0 && note.dot_type == 0 && note.camera_switch_timing == 0
        && std::all_of(note.ignore_sound_ids.begin(), note.ignore_sound_ids.end(),
            [](const std::string& id) { return id.empty(); });
}

inline bool restricted_extended_row_pair(
    const Note& source, const ChartNote& compiled, const double bpm)
{
    return restricted_extended_source_note(source)
        && restricted_extended_compiled_note(compiled)
        && compiled.beat == source.beat
        && compiled.duration_beats == source.duration_beats
        && compiled.pitch == source.pitch
        && compiled.time_str == expected_compiled_time_string(source.beat, bpm);
}

inline bool extended_chart_row_count_in_range(const std::size_t row_count)
{
    return row_count >= kMinimumExtendedChartRows && row_count <= kMaximumExtendedChartRows;
}

inline bool bounded_extended_retention_shape(const DiagnosticChartRetention& diagnostic)
{
    if (!extended_chart_row_count_in_range(diagnostic.source_row_count)
        || diagnostic.native_prefix_row_count != kMaxChartRows
        || diagnostic.tail_rows.size() != diagnostic.source_row_count - kMaxChartRows) return false;
    for (std::size_t index = 0; index < diagnostic.tail_rows.size(); ++index) {
        if (diagnostic.tail_rows[index].source_row != kMaxChartRows + index) return false;
    }
    return true;
}

} // namespace ff7rp::pipeline
