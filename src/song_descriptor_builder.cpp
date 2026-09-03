#include "song_descriptor_builder.h"

#include "core/logging.h"
#include "pipeline/diagnostic_descriptor_hash.h"
#include "pipeline/chart_event_plan.h"
#include "pipeline/extended_chart_eligibility.h"
#include "pipeline/pipeline_limits.h"

#include <algorithm>
#include <utility>

namespace ff7r::piano {
namespace {

bool eligible_extended_profile(const ff7rp::pipeline::LoadedSong& song,
    const ff7rp::pipeline::LoadedDifficultyProfile& profile,
    const ff7rp::pipeline::ChartEventPlan& event_plan)
{
    const auto& diagnostic = profile.diagnostic_chart;
    if (!ff7rp::pipeline::extended_chart_row_count_in_range(event_plan.source_row_count)
        || profile.config.notes.size() != ff7rp::pipeline::kMaxChartRows
        || profile.chart.notes.size() != ff7rp::pipeline::kMaxChartRows
        || diagnostic.source_row_count != event_plan.source_row_count
        || diagnostic.native_prefix_row_count != ff7rp::pipeline::kMaxChartRows
        || diagnostic.tail_rows.size() != event_plan.source_row_count - ff7rp::pipeline::kMaxChartRows
        || diagnostic.descriptor_hash == 0
        || diagnostic.descriptor_hash != ff7rp::pipeline::compute_diagnostic_descriptor_hash(
            song.id, profile.config.difficulty, profile.chart, diagnostic)
        || !ff7rp::pipeline::bounded_extended_retention_shape(diagnostic)) {
        return false;
    }
    std::vector<ff7rp::pipeline::Note> source_rows;
    std::vector<ff7rp::pipeline::ChartNote> compiled_rows;
    ff7rp::pipeline::ChartEventPlan verified;
    return ff7rp::pipeline::complete_profile_rows(profile, &source_rows, &compiled_rows)
        && ff7rp::pipeline::eligible_extended_chart_plan(
            source_rows, compiled_rows, profile.config.bpm, &verified)
        && verified.source_row_count == event_plan.source_row_count
        && verified.native_event_count == event_plan.native_event_count
        && verified.required_action_count == event_plan.required_action_count
        && verified.physical_digest == event_plan.physical_digest;
}

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
        chart_note.ignore_sound_ids = note.ignore_sound_ids;
        descriptor.chart_notes.push_back(std::move(chart_note));
    }
    descriptor.sidecar_path = core::widen(song.cache_sidecar_path);
    descriptor.default_profile_index = 0;
    descriptor.profiles.reserve(song.difficulty_profiles.size());
    for (size_t profile_index = 0; profile_index < song.difficulty_profiles.size(); ++profile_index) {
        const auto& source_profile = song.difficulty_profiles[profile_index];
        game::SongDifficultyProfile profile;
        profile.difficulty = source_profile.config.difficulty;
        ff7rp::pipeline::ChartEventPlan event_plan;
        const bool event_plan_valid = ff7rp::pipeline::derive_profile_event_plan(source_profile, &event_plan);
        if (event_plan_valid) {
            profile.source_row_count = event_plan.source_row_count;
            profile.native_prefix_event_count = event_plan.native_prefix_event_count;
            profile.native_event_count = event_plan.native_event_count;
            profile.required_action_count = event_plan.required_action_count;
            profile.physical_chart_digest = event_plan.physical_digest;
            profile.note_count = static_cast<int>(event_plan.required_action_count);
        } else {
            profile.note_count = static_cast<int>(source_profile.chart.notes.size());
        }
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
                note.dot_type, note.camera_switch_timing, note.group_index, note.ignore_sound_ids});
        }
        profile.diagnostic_source_rows = source_profile.diagnostic_chart.source_row_count;
        profile.diagnostic_native_prefix_rows = source_profile.diagnostic_chart.native_prefix_row_count;
        profile.diagnostic_tail_rows = source_profile.diagnostic_chart.tail_rows.size();
        profile.diagnostic_descriptor_hash = source_profile.diagnostic_chart.descriptor_hash;
        profile.diagnostic_policy_generation = song.chart_policy_generation;
        profile.diagnostic_loaded_from_runtime_cache = song.loaded_from_runtime_cache;
        const std::size_t source_rows = source_profile.diagnostic_chart.source_row_count;
        bool supported_tail_shape =
            ff7rp::pipeline::extended_chart_row_count_in_range(source_rows)
            && source_profile.diagnostic_chart.native_prefix_row_count == ff7rp::pipeline::kMaxChartRows
            && source_profile.diagnostic_chart.tail_rows.size()
                == source_rows - ff7rp::pipeline::kMaxChartRows;
        for (std::size_t tail_index = 0;
             supported_tail_shape && tail_index < source_profile.diagnostic_chart.tail_rows.size();
             ++tail_index) {
            supported_tail_shape = source_profile.diagnostic_chart.tail_rows[tail_index].source_row
                == ff7rp::pipeline::kMaxChartRows + tail_index;
        }
        if (supported_tail_shape) {
            profile.extended_chart_tail_notes.reserve(source_profile.diagnostic_chart.tail_rows.size());
            for (const auto& tail_row : source_profile.diagnostic_chart.tail_rows) {
                const auto& note = tail_row.compiled;
                profile.extended_chart_tail_notes.push_back(game::SongChartNote{
                note.time_str, note.monotone_id, note.chord_id, note.note_type,
                note.dot_type, note.camera_switch_timing, note.group_index,
                note.ignore_sound_ids});
            }
            const auto policy = ff7rp::pipeline::chart_row_policy_snapshot();
            if (policy.playable_extended_available && song.chart_policy_enabled
                && song.chart_policy_generation == policy.generation
                && song.chart_policy_identity == policy.identity()
                && song.accepted_chart_input_limit >= source_rows
                && song.published_chart_row_limit >= source_rows
                && event_plan_valid
                && event_plan.native_event_count <= ff7rp::pipeline::kMaximumNativeChartEvents
                && eligible_extended_profile(song, source_profile, event_plan)) {
                // Runtime owners consume native_event_count for allocation and
                // required_action_count/note_count for scoring.
            } else {
                profile.extended_chart_tail_notes.clear();
                ff7rp::pipeline::ChartEventPlan prefix_plan;
                if (ff7rp::pipeline::derive_chart_event_plan(
                        source_profile.config.notes, source_profile.chart.notes, &prefix_plan)) {
                    profile.source_row_count = prefix_plan.source_row_count;
                    profile.native_prefix_event_count = prefix_plan.native_prefix_event_count;
                    profile.native_event_count = prefix_plan.native_event_count;
                    profile.required_action_count = prefix_plan.required_action_count;
                    profile.physical_chart_digest = prefix_plan.physical_digest;
                    profile.note_count = static_cast<int>(prefix_plan.required_action_count);
                }
            }
        }
        descriptor.profiles.push_back(std::move(profile));
    }
    return descriptor;
}

} // namespace ff7r::piano
