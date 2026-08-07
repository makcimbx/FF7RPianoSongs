#include "game/synthetic_extended_chart_model.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace ff7r::piano::game::synthetic_model {
namespace {

bool append_event(const SourceRow& row, std::size_t source_row, bool chord,
    std::vector<Event>& events)
{
    Event event;
    event.source_row = source_row;
    event.chord = chord;
    event.camera_transition = row.camera_transition;
    event.owned_values = row.owned_values;
    events.push_back(std::move(event));
    return true;
}

bool fail_at(FailurePoint actual, const BuildRequest& request, std::size_t step = 0)
{
    return actual == request.failure_point && step == request.failure_step;
}

} // namespace

bool links_are_internal(const Chart& chart)
{
    for (const Event& event : chart.events) {
        if ((event.parent != kNoEvent && event.parent >= chart.events.size())
            || (event.successor != kNoEvent && event.successor >= chart.events.size())) {
            return false;
        }
    }
    return true;
}

bool build_transactionally(const BuildRequest& request, Chart* destination)
{
    if (!destination || !request.rows || request.maximum_rows == 0
        || request.maximum_rows > kExperimentalMaximumRows
        || request.rows->empty() || request.rows->size() > request.maximum_rows
        || request.rows->size() <= kNativeMaximumRows
        || request.native_prefix_rows > request.rows->size()
        || request.native_prefix_rows != kNativeMaximumRows
        || !request.experiment_enabled || !request.persistent_caller
        || !request.active_custom_descriptor
        || request.playback_active || request.another_chart_active) {
        return false;
    }

    std::size_t event_count = 0;
    double previous_time = -1.0;
    for (const SourceRow& row : *request.rows) {
        if (!std::isfinite(row.time) || row.time < 0.0 || row.time < previous_time
            || (!row.monotone && !row.chord)) {
            return false;
        }
        const std::size_t additions = static_cast<std::size_t>(row.monotone)
            + static_cast<std::size_t>(row.chord);
        if (event_count > request.maximum_rows * 2u - additions) {
            return false;
        }
        event_count += additions;
        previous_time = row.time;
    }

    try {
        Chart candidate;
        candidate.times.reserve(request.rows->size());
        if (fail_at(FailurePoint::AfterTimeReserve, request)) {
            return false;
        }
        candidate.events.reserve(event_count);
        if (fail_at(FailurePoint::AfterEventReserve, request)) {
            return false;
        }

        std::size_t constructed = 0;
        for (std::size_t i = 0; i < request.native_prefix_rows; ++i) {
            const SourceRow& row = (*request.rows)[i];
            candidate.times.push_back(row.time);
            if (row.monotone) {
                append_event(row, i, false, candidate.events);
                if (fail_at(FailurePoint::AfterNativeEventConstruction, request, constructed++)) {
                    return false;
                }
            }
            if (row.chord) {
                append_event(row, i, true, candidate.events);
                if (fail_at(FailurePoint::AfterNativeEventConstruction, request, constructed++)) {
                    return false;
                }
            }
        }
        constructed = 0;
        for (std::size_t i = request.native_prefix_rows; i < request.rows->size(); ++i) {
            const SourceRow& row = (*request.rows)[i];
            candidate.times.push_back(row.time);
            if (row.monotone) {
                append_event(row, i, false, candidate.events);
                if (fail_at(FailurePoint::AfterTailEventConstruction, request, constructed++)) {
                    return false;
                }
            }
            if (row.chord) {
                append_event(row, i, true, candidate.events);
                if (fail_at(FailurePoint::AfterTailEventConstruction, request, constructed++)) {
                    return false;
                }
            }
        }

        std::size_t group_root = kNoEvent;
        std::size_t linked = 0;
        uint32_t active_group = 0;
        for (std::size_t i = 0; i < candidate.events.size(); ++i) {
            const uint32_t group = (*request.rows)[candidate.events[i].source_row].group;
            if (group == 0) {
                active_group = 0;
                group_root = kNoEvent;
                continue;
            }
            if (group != active_group || group_root == kNoEvent) {
                active_group = group;
                group_root = i;
                continue;
            }

            Event& root = candidate.events[group_root];
            Event& child = candidate.events[i];
            child.parent = group_root;
            child.successor = root.successor;
            root.successor = i;
            if (fail_at(FailurePoint::AfterLink, request, linked++)) {
                return false;
            }
        }

        candidate.max_time = candidate.times.back();
        candidate.displayed_note_count = request.rows->size();
        if (!links_are_internal(candidate)
            || candidate.times.capacity() < request.rows->size()
            || candidate.events.capacity() < event_count
            || fail_at(FailurePoint::BeforePublish, request)) {
            return false;
        }
        candidate.published = true;
        *destination = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace ff7r::piano::game::synthetic_model
