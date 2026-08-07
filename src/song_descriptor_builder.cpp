#include "song_descriptor_builder.h"

#include "core/logging.h"

#include <algorithm>
#include <utility>

namespace ff7r::piano {
namespace {

float chart_duration_seconds(const ff7rp::pipeline::LoadedSong& song)
{
    double last_beat = 0.0;
    for (const auto& note : song.config.notes) {
        last_beat = std::max(last_beat, note.beat + note.duration_beats);
    }
    if (song.config.bpm <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(last_beat * 60.0 / song.config.bpm);
}

float song_duration_seconds(const ff7rp::pipeline::LoadedSong& song)
{
    return std::max(chart_duration_seconds(song), static_cast<float>(song.audio.source_duration_seconds()));
}

} // namespace

game::SongDescriptor build_song_descriptor(
    const ff7rp::pipeline::LoadedSong& song, int visible_index)
{
    game::SongDescriptor descriptor;
    descriptor.id = song.id;
    descriptor.title = core::widen(song.config.title);
    descriptor.visible_index = visible_index;
    descriptor.base_slot = 0;
    descriptor.unique_index = 0;
    descriptor.difficulty = song.config.difficulty;
    descriptor.note_count = static_cast<int>(song.chart.notes.size());
    descriptor.bpm = static_cast<float>(song.config.bpm);
    descriptor.duration_seconds = song_duration_seconds(song);
    for (size_t i = 0; i < descriptor.score_thresholds.size() && i < song.config.score_thresholds.size(); ++i) {
        descriptor.score_thresholds[i] = song.config.score_thresholds[i];
    }
    for (size_t i = 0; i < descriptor.mode_change_combo_counts.size() && i < song.config.mode_change_combo_counts.size(); ++i) {
        descriptor.mode_change_combo_counts[i] = song.config.mode_change_combo_counts[i];
    }
    descriptor.chart_notes.reserve(song.chart.notes.size());
    for (const auto& note : song.chart.notes) {
        game::SongChartNote chart_note;
        chart_note.time_str = note.time_str;
        chart_note.monotone_id = note.monotone_id;
        chart_note.chord_id = note.chord_id;
        chart_note.note_type = note.note_type;
        chart_note.dot_type = note.dot_type;
        chart_note.camera_switch_timing = note.camera_switch_timing;
        chart_note.group_index = note.group_index;
        descriptor.chart_notes.push_back(std::move(chart_note));
    }
    descriptor.sidecar_path = core::widen(song.cache_sidecar_path);
    descriptor.default_profile_index = 0;
    descriptor.profiles.reserve(song.difficulty_profiles.size());
    for (size_t profile_index = 0; profile_index < song.difficulty_profiles.size(); ++profile_index) {
        const auto& source_profile = song.difficulty_profiles[profile_index];
        game::SongDifficultyProfile profile;
        profile.difficulty = source_profile.config.difficulty;
        profile.note_count = static_cast<int>(source_profile.chart.notes.size());
        profile.bpm = static_cast<float>(source_profile.config.bpm);
        profile.title = descriptor.title;
        if (song.difficulty_profiles.size() > 1) {
            profile.title += L" [Lv." + std::to_wstring(profile.difficulty) + L"]";
        }
        for (size_t i = 0; i < profile.score_thresholds.size() && i < source_profile.config.score_thresholds.size(); ++i) {
            profile.score_thresholds[i] = source_profile.config.score_thresholds[i];
        }
        for (size_t i = 0; i < profile.mode_change_combo_counts.size() && i < source_profile.config.mode_change_combo_counts.size(); ++i) {
            profile.mode_change_combo_counts[i] = source_profile.config.mode_change_combo_counts[i];
        }
        profile.chart_notes.reserve(source_profile.chart.notes.size());
        for (const auto& note : source_profile.chart.notes) {
            profile.chart_notes.push_back({note.time_str, note.monotone_id, note.chord_id, note.note_type,
                note.dot_type, note.camera_switch_timing, note.group_index});
        }
        profile.diagnostic_source_rows = source_profile.diagnostic_chart.source_row_count;
        profile.diagnostic_native_prefix_rows = source_profile.diagnostic_chart.native_prefix_row_count;
        profile.diagnostic_tail_rows = source_profile.diagnostic_chart.tail_rows.size();
        profile.diagnostic_descriptor_hash = source_profile.diagnostic_chart.descriptor_hash;
        profile.diagnostic_policy_generation = song.chart_policy_generation;
        profile.diagnostic_loaded_from_runtime_cache = song.loaded_from_runtime_cache;
        descriptor.profiles.push_back(std::move(profile));
    }
    return descriptor;
}

} // namespace ff7r::piano
