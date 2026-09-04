#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "pipeline_limits.h"
#include "song_types.h"

namespace ff7rp::pipeline {

// Runtime-neutral compiled row. Both pipeline ChartNote and game-owned
// SongChartNote expose this exact semantic subset; no source MIDI or game type
// is required by the canonical planner.
struct ChartEventRow {
    std::string time_str;
    std::string monotone_id;
    std::string chord_id;
    std::int32_t monotone_note_type = 0;
    std::int32_t monotone_dot_type = 0;
    std::int32_t chord_note_type = 0;
    std::int32_t chord_dot_type = 0;
    std::int32_t camera_switch_timing = 0;
    std::int32_t group_index = 0;
    std::array<std::string, 3> ignore_sound_ids{};
};

template <typename CompiledOrRuntimeRow>
inline ChartEventRow chart_event_row_from_compiled(const CompiledOrRuntimeRow& row)
{
    return {row.time_str, row.monotone_id, row.chord_id,
        static_cast<std::int32_t>(row.monotone_note_type),
        static_cast<std::int32_t>(row.monotone_dot_type),
        static_cast<std::int32_t>(row.chord_note_type),
        static_cast<std::int32_t>(row.chord_dot_type),
        static_cast<std::int32_t>(row.camera_switch_timing),
        static_cast<std::int32_t>(row.group_index), row.ignore_sound_ids};
}

enum class ChartEventKind : std::uint8_t { Monotone = 0, Chord = 1 };
enum class ChartEventHand : std::uint8_t { Right = 0, Left = 1 };
enum class ChartEventPlanError : std::uint8_t {
    None = 0,
    EmptyRows,
    RowLimitExceeded,
    NativeEventLimitExceeded,
    InvalidRow,
    InvalidTime,
    TimeOrderViolation,
    InvalidGroupIndex,
    InvalidGroupRun,
    OrdinalOverflow,
};

inline constexpr std::size_t kNoChartEvent = std::numeric_limits<std::size_t>::max();

struct ChartEventEntry {
    std::size_t source_row_index = 0;
    std::size_t compact_event_index = 0;
    std::size_t backing_row_index = 0;
    std::uint32_t ordinal = 0;
    ChartEventKind kind = ChartEventKind::Monotone;
    ChartEventHand hand = ChartEventHand::Right;
    std::uint8_t group_index = 0;
    std::uint32_t run_index = 0;
    bool parentless = true;
    std::size_t root_event_index = kNoChartEvent;
};

struct ChartEventLink {
    std::size_t root_event_index = 0;
    std::size_t child_event_index = 0;
    std::uint32_t run_index = 0;
    std::uint8_t group_index = 0;
};

struct ChartEventPlan {
    ChartEventPlanError error = ChartEventPlanError::EmptyRows;
    std::size_t source_row_count = 0;
    std::size_t native_prefix_event_count = 0;
    std::size_t native_event_count = 0;
    std::size_t required_action_count = 0;
    std::uint64_t physical_digest = 0;
    std::uint8_t final_group_index = 0;
    std::vector<ChartEventEntry> events;
    std::vector<ChartEventLink> links;

    bool valid() const noexcept { return error == ChartEventPlanError::None; }
};

inline bool chart_event_plans_equal(const ChartEventPlan& left, const ChartEventPlan& right)
{
    if (left.error != right.error || left.source_row_count != right.source_row_count
        || left.native_prefix_event_count != right.native_prefix_event_count
        || left.native_event_count != right.native_event_count
        || left.required_action_count != right.required_action_count
        || left.physical_digest != right.physical_digest
        || left.final_group_index != right.final_group_index
        || left.events.size() != right.events.size() || left.links.size() != right.links.size()) return false;
    for (std::size_t index = 0; index < left.events.size(); ++index) {
        const auto& a = left.events[index];
        const auto& b = right.events[index];
        if (a.source_row_index != b.source_row_index
            || a.compact_event_index != b.compact_event_index
            || a.backing_row_index != b.backing_row_index || a.ordinal != b.ordinal
            || a.kind != b.kind || a.hand != b.hand || a.group_index != b.group_index
            || a.run_index != b.run_index || a.parentless != b.parentless
            || a.root_event_index != b.root_event_index) return false;
    }
    for (std::size_t index = 0; index < left.links.size(); ++index) {
        const auto& a = left.links[index];
        const auto& b = right.links[index];
        if (a.root_event_index != b.root_event_index || a.child_event_index != b.child_event_index
            || a.run_index != b.run_index || a.group_index != b.group_index) return false;
    }
    return true;
}

inline bool chart_note_semantics_equal(const Note& source, const ChartNote& compiled)
{
    const NativeNoteValue monotone = resolved_native_note_value(
        source.monotone_note_value, source.duration_beats);
    const NativeNoteValue chord = resolved_native_note_value(
        source.chord_note_value, source.duration_beats);
    return std::isfinite(source.beat) && source.beat >= 0.0
        && std::isfinite(source.duration_beats) && source.duration_beats > 0.0
        && compiled.beat == source.beat
        && compiled.duration_beats == source.duration_beats
        && compiled.pitch == source.pitch
        && compiled.chord_id == source.chord_id
        && compiled.group_index == source.group_index
        && valid_note_value_override(source.monotone_note_value)
        && valid_note_value_override(source.chord_note_value)
        && compiled.monotone_note_type == (source.pitch.empty() ? 0 : monotone.note_type)
        && compiled.monotone_dot_type == (source.pitch.empty() ? 0 : monotone.dot_type)
        && compiled.chord_note_type == (source.chord_id.empty() ? 0 : chord.note_type)
        && compiled.chord_dot_type == (source.chord_id.empty() ? 0 : chord.dot_type)
        && compiled.camera_switch_timing == 0;
}

inline std::uint64_t chart_event_append(
    std::uint64_t hash, const void* data, const std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

inline std::uint64_t chart_event_append_string(std::uint64_t hash, const std::string& value)
{
    const std::uint64_t size = value.size();
    hash = chart_event_append(hash, &size, sizeof(size));
    return chart_event_append(hash, value.data(), value.size());
}

inline bool chart_event_time_frame(const std::string& value, std::uint64_t* frame)
{
    if (!frame || value.size() < 5u || value[value.size() - 3u] != '_') return false;
    std::uint64_t seconds = 0;
    for (std::size_t index = 0; index + 3u < value.size(); ++index) {
        const char digit = value[index];
        if (digit < '0' || digit > '9'
            || seconds > (std::numeric_limits<std::uint64_t>::max() - 9u) / 10u) return false;
        seconds = seconds * 10u + static_cast<std::uint64_t>(digit - '0');
    }
    const char tens = value[value.size() - 2u];
    const char ones = value[value.size() - 1u];
    if (tens < '0' || tens > '9' || ones < '0' || ones > '9') return false;
    const std::uint64_t subframe = static_cast<std::uint64_t>(tens - '0') * 10u
        + static_cast<std::uint64_t>(ones - '0');
    if (subframe >= 60u || seconds > (std::numeric_limits<std::uint64_t>::max() - subframe) / 60u)
        return false;
    *frame = seconds * 60u + subframe;
    return true;
}

// Canonical runtime-capable derivation. Physical digest covers complete compiled
// musical/runtime semantics and excludes only GroupIndex topology. R/P/E/A and
// links are always rederived and must be checked separately.
inline ChartEventPlan derive_chart_event_plan(const std::vector<ChartEventRow>& rows)
{
    ChartEventPlan plan;
    plan.source_row_count = rows.size();
    if (rows.empty()) return plan;
    if (rows.size() > kMaximumExtendedChartRows) {
        plan.error = ChartEventPlanError::RowLimitExceeded;
        return plan;
    }

    plan.events.reserve(std::min(rows.size() * 2u, kMaximumNativeChartEvents));
    plan.links.reserve(plan.events.capacity());
    plan.physical_digest = 14695981039346656037ull ^ 0x706879736963616cull;
    const std::uint64_t row_count = rows.size();
    plan.physical_digest = chart_event_append(plan.physical_digest, &row_count, sizeof(row_count));

    std::uint8_t active_group = 0;
    std::size_t active_group_rows = 0;
    std::uint32_t active_run = 0;
    std::size_t active_root_event = kNoChartEvent;
    std::uint64_t previous_frame = 0;
    bool have_previous_frame = false;

    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        const ChartEventRow& row = rows[row_index];
        const bool right = !row.monotone_id.empty();
        const bool left = !row.chord_id.empty();
        const auto valid_side = [](const std::int32_t note_type, const std::int32_t dot_type) {
            return note_type >= 0 && note_type <= 4 && dot_type >= 0 && dot_type <= 1;
        };
        if ((!right && !left)
            || (right ? !valid_side(row.monotone_note_type, row.monotone_dot_type)
                      : row.monotone_note_type != 0 || row.monotone_dot_type != 0)
            || (left ? !valid_side(row.chord_note_type, row.chord_dot_type)
                     : row.chord_note_type != 0 || row.chord_dot_type != 0)
            || row.camera_switch_timing != 0
            || (!left && (row.ignore_sound_ids[0].size() || row.ignore_sound_ids[1].size()
                || row.ignore_sound_ids[2].size()))) {
            plan.error = ChartEventPlanError::InvalidRow;
            return plan;
        }
        if (row.group_index < 0 || row.group_index > 255) {
            plan.error = ChartEventPlanError::InvalidGroupIndex;
            return plan;
        }
        std::uint64_t native_frame = 0;
        if (!chart_event_time_frame(row.time_str, &native_frame)) {
            plan.error = ChartEventPlanError::InvalidTime;
            return plan;
        }
        if (have_previous_frame && native_frame < previous_frame) {
            plan.error = ChartEventPlanError::TimeOrderViolation;
            return plan;
        }
        previous_frame = native_frame;
        have_previous_frame = true;

        const std::uint8_t group = static_cast<std::uint8_t>(row.group_index);
        const bool continuation = group != 0 && group == active_group;
        if (!continuation) {
            if (active_group != 0 && active_group_rows < 2u) {
                plan.error = ChartEventPlanError::InvalidGroupRun;
                return plan;
            }
            active_group = group;
            active_group_rows = group == 0 ? 0u : 1u;
            active_root_event = kNoChartEvent;
            if (group != 0) {
                if (active_run == std::numeric_limits<std::uint32_t>::max()) {
                    plan.error = ChartEventPlanError::OrdinalOverflow;
                    return plan;
                }
                ++active_run;
            }
        } else {
            ++active_group_rows;
        }

        const std::size_t row_events = static_cast<std::size_t>(right) + static_cast<std::size_t>(left);
        if (plan.events.size() > kMaximumNativeChartEvents - row_events) {
            plan.error = ChartEventPlanError::NativeEventLimitExceeded;
            return plan;
        }
        if (row_index > (std::numeric_limits<std::uint32_t>::max() - 1u) / 2u) {
            plan.error = ChartEventPlanError::OrdinalOverflow;
            return plan;
        }

        const std::uint64_t digest_row = row_index;
        plan.physical_digest = chart_event_append(
            plan.physical_digest, &digest_row, sizeof(digest_row));
        plan.physical_digest = chart_event_append_string(plan.physical_digest, row.time_str);
        plan.physical_digest = chart_event_append_string(plan.physical_digest, row.monotone_id);
        plan.physical_digest = chart_event_append_string(plan.physical_digest, row.chord_id);
        plan.physical_digest = chart_event_append(
            plan.physical_digest, &row.monotone_note_type, sizeof(row.monotone_note_type));
        plan.physical_digest = chart_event_append(
            plan.physical_digest, &row.monotone_dot_type, sizeof(row.monotone_dot_type));
        plan.physical_digest = chart_event_append(
            plan.physical_digest, &row.chord_note_type, sizeof(row.chord_note_type));
        plan.physical_digest = chart_event_append(
            plan.physical_digest, &row.chord_dot_type, sizeof(row.chord_dot_type));
        plan.physical_digest = chart_event_append(
            plan.physical_digest, &row.camera_switch_timing, sizeof(row.camera_switch_timing));
        for (const std::string& id : row.ignore_sound_ids)
            plan.physical_digest = chart_event_append_string(plan.physical_digest, id);

        const auto append_event = [&](const ChartEventKind kind, const ChartEventHand hand,
                                      const std::uint32_t ordinal) {
            const std::size_t event_index = plan.events.size();
            const bool parentless = !continuation;
            ChartEventEntry event{row_index, event_index, row_index, ordinal, kind, hand,
                group, group == 0 ? 0u : active_run, parentless,
                parentless ? kNoChartEvent : active_root_event};
            plan.events.push_back(event);
            if (parentless) {
                ++plan.required_action_count;
                if (active_root_event == kNoChartEvent) active_root_event = event_index;
            } else {
                plan.links.push_back(
                    {active_root_event, event_index, active_run, group});
            }
            if (row_index < kMaxChartRows) ++plan.native_prefix_event_count;
        };
        if (right) append_event(ChartEventKind::Monotone, ChartEventHand::Right,
            static_cast<std::uint32_t>(row_index * 2u));
        if (left) append_event(ChartEventKind::Chord, ChartEventHand::Left,
            static_cast<std::uint32_t>(row_index * 2u + 1u));
    }
    if (active_group != 0 && active_group_rows < 2u) {
        plan.error = ChartEventPlanError::InvalidGroupRun;
        return plan;
    }
    plan.native_event_count = plan.events.size();
    plan.final_group_index = static_cast<std::uint8_t>(rows.back().group_index);
    plan.error = ChartEventPlanError::None;
    return plan;
}

inline bool derive_chart_event_plan(
    const std::vector<Note>& source_rows,
    const std::vector<ChartNote>& compiled_rows,
    ChartEventPlan* out_plan)
{
    if (!out_plan || source_rows.empty() || source_rows.size() != compiled_rows.size()) return false;
    std::vector<ChartEventRow> rows;
    rows.reserve(compiled_rows.size());
    for (std::size_t index = 0; index < source_rows.size(); ++index) {
        const Note& source = source_rows[index];
        const ChartNote& compiled = compiled_rows[index];
        const bool right = !source.pitch.empty();
        if (!chart_note_semantics_equal(source, compiled)
            || (right != !compiled.monotone_id.empty())) return false;
        rows.push_back(chart_event_row_from_compiled(compiled));
    }
    ChartEventPlan plan = derive_chart_event_plan(rows);
    if (!plan.valid()) return false;
    *out_plan = std::move(plan);
    return true;
}

inline bool complete_profile_rows(
    const LoadedDifficultyProfile& profile,
    std::vector<Note>* source_rows,
    std::vector<ChartNote>* compiled_rows)
{
    if (!source_rows || !compiled_rows || profile.config.notes.size() != profile.chart.notes.size()) return false;
    *source_rows = profile.config.notes;
    *compiled_rows = profile.chart.notes;
    if (!profile.diagnostic_chart.present()) return true;
    if (profile.diagnostic_chart.native_prefix_row_count != profile.config.notes.size()
        || profile.diagnostic_chart.source_row_count !=
            profile.config.notes.size() + profile.diagnostic_chart.tail_rows.size()) return false;
    source_rows->reserve(profile.diagnostic_chart.source_row_count);
    compiled_rows->reserve(profile.diagnostic_chart.source_row_count);
    for (std::size_t index = 0; index < profile.diagnostic_chart.tail_rows.size(); ++index) {
        const auto& tail = profile.diagnostic_chart.tail_rows[index];
        if (tail.source_row != profile.config.notes.size() + index) return false;
        source_rows->push_back(tail.source);
        compiled_rows->push_back(tail.compiled);
    }
    return true;
}

inline bool derive_profile_event_plan(
    const LoadedDifficultyProfile& profile, ChartEventPlan* out_plan)
{
    std::vector<Note> source;
    std::vector<ChartNote> compiled;
    return complete_profile_rows(profile, &source, &compiled)
        && derive_chart_event_plan(source, compiled, out_plan);
}

} // namespace ff7rp::pipeline
