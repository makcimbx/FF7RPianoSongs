#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "song_types.h"

namespace ff7rp::pipeline {

inline std::uint64_t compute_diagnostic_descriptor_hash(
    const std::string& song_id, const int difficulty, const CompiledChart& playable_chart,
    const DiagnosticChartRetention& diagnostic)
{
    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    const auto append = [&](const void* data, const std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= kPrime;
        }
    };
    const auto append_string = [&](const std::string& value) {
        const std::uint64_t size = value.size();
        append(&size, sizeof(size));
        append(value.data(), value.size());
    };
    const auto append_source = [&](const Note& note) {
        append(&note.beat, sizeof(note.beat));
        append(&note.duration_beats, sizeof(note.duration_beats));
        append_string(note.pitch);
        append_string(note.chord_id);
        append(&note.group_index, sizeof(note.group_index));
        append(&note.alternate_monotone, sizeof(note.alternate_monotone));
        const std::uint64_t ignore_count = note.ignore_sound_pitches.size();
        const std::uint64_t voicing_count = note.source_chord_pitches.size();
        append(&ignore_count, sizeof(ignore_count));
        for (const auto& pitch : note.ignore_sound_pitches) append_string(pitch);
        append(&voicing_count, sizeof(voicing_count));
        for (const auto& pitch : note.source_chord_pitches) append_string(pitch);
    };
    const auto append_compiled = [&](const ChartNote& note) {
        append(&note.beat, sizeof(note.beat));
        append(&note.duration_beats, sizeof(note.duration_beats));
        append_string(note.pitch);
        append_string(note.time_str);
        append_string(note.monotone_id);
        append_string(note.chord_id);
        append(&note.note_type, sizeof(note.note_type));
        append(&note.dot_type, sizeof(note.dot_type));
        append(&note.camera_switch_timing, sizeof(note.camera_switch_timing));
        append(&note.group_index, sizeof(note.group_index));
        for (const auto& id : note.ignore_sound_ids) append_string(id);
    };
    append_string(song_id);
    append(&difficulty, sizeof(difficulty));
    const std::uint64_t playable_count = playable_chart.notes.size();
    append(&playable_count, sizeof(playable_count));
    for (const ChartNote& note : playable_chart.notes) append_compiled(note);
    const std::uint64_t source_count = diagnostic.source_row_count;
    const std::uint64_t prefix_count = diagnostic.native_prefix_row_count;
    const std::uint64_t tail_count = diagnostic.tail_rows.size();
    append(&source_count, sizeof(source_count));
    append(&prefix_count, sizeof(prefix_count));
    append(&tail_count, sizeof(tail_count));
    for (const DiagnosticChartTailRow& row : diagnostic.tail_rows) {
        const std::uint64_t source_row = row.source_row;
        append(&source_row, sizeof(source_row));
        append_source(row.source);
        append_compiled(row.compiled);
    }
    return hash;
}

} // namespace ff7rp::pipeline
