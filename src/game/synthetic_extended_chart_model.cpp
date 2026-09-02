#include "game/synthetic_extended_chart_model.h"

#include <cmath>

namespace ff7r::piano::game::synthetic_model {
namespace {
bool restricted(const std::vector<SourceRow>& rows) {
    if (rows.size() != kPlayableRows) return false;
    float previous = -1;
    bool prefix_assignments[256]{};
    uint8_t prefix_lookup_path = 2;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        if (!row.monotone || row.chord || row.group || row.camera_transition || row.ignore_sound
            || row.strength != 0 || row.fname == 0 || row.assignment == 8 || row.lookup_path > 1
            || !std::isfinite(row.time) || row.time < previous) return false;
        if (index < kNativeRows) {
            if (index && row.lookup_path != prefix_lookup_path) return false;
            if (!index) prefix_lookup_path = row.lookup_path;
            prefix_assignments[row.assignment] = true;
        }
        previous = row.time;
    }
    return rows.back().lookup_path == prefix_lookup_path
        && prefix_assignments[rows.back().assignment];
}
}

Result run(const BuildRequest& request, Chart& chart) {
    Result result;
    const bool gate = request.rows && request.experiment_enabled && request.verified_1005
        && request.exact_caller && request.exact_header && request.exact_thread
        && request.depth_one && request.exact_generation && request.global_claim
        && restricted(*request.rows);
    if (!gate) { result.forwarded = true; return result; }
    result.substituted = true;
    if (request.second_reserve_hit) { result.forwarded = true; return result; }
    if (request.reserve_capacity < kPlayableRows || request.reserve_capacity > 1024) return result;

    chart.storage.clear(); chart.storage.reserve(request.reserve_capacity); chart.capacity = request.reserve_capacity;
    for (std::size_t i = 0; i < kNativeRows; ++i)
        chart.storage.push_back({i, static_cast<uint32_t>(2 * i), (*request.rows)[i].time, true});
    chart.count = kNativeRows; chart.max_time = request.rows->at(kNativeRows - 1).time;
    if (request.failure == FailurePoint::PrefixValidation) return result;
    result.prefix_validated = true;
    if (request.failure == FailurePoint::FNameFind) return result;

    chart.storage.push_back({kNativeRows, 1024, request.rows->back().time, false});
    chart.tail_constructor_entered = true; result.tail_constructed = true;
    const auto cleanup = [&] { chart.storage.resize(kNativeRows); chart.tail_destructed = true; };
    if (request.failure == FailurePoint::ConstructorReturnedNonTail) {
        chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    result.constructor_returned_tail = true;
    if (request.failure == FailurePoint::ConstructorValidation) {
        cleanup(); return result;
    }
    if (request.failure == FailurePoint::CallbackBuild || request.failure == FailurePoint::CallbackValidation) {
        cleanup(); return result;
    }
    if (request.failure == FailurePoint::CallbackCleanupIdentityFailure) {
        chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    chart.storage.back().callback_valid = true; result.callback_validated = true;
    const float old_max = chart.max_time;
    if (request.failure == FailurePoint::PreWriteMaxReadFailure) {
        chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    if (request.failure == FailurePoint::PreWriteMaxDrift) {
        chart.max_time = old_max + 0.25f;
        chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    chart.max_time = request.rows->back().time; ++chart.max_write_count;
    if (request.failure == FailurePoint::PostWriteMaxReadFailure) {
        chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    if (request.failure == FailurePoint::PostWriteMaxMismatch) {
        chart.max_time = request.rows->back().time + 0.25f;
        chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    chart.count = kPlayableRows; result.count_committed = true;
    if (request.failure == FailurePoint::PostCountExternalDrift) {
        chart.count = 77; chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    if (request.failure == FailurePoint::PostCountRestoreFailure) {
        chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    if (request.failure == FailurePoint::PostCountValidation) {
        chart.count = kNativeRows; result.count_restore_proved = true;
        chart.max_time = old_max; result.max_restore_proved = true;
        cleanup(); result.rolled_back = true; return result;
    }
    return result;
}

void model_next_parser_reset(Chart& chart) {
    chart.storage.clear(); chart.count = 0; chart.capacity = 0;
}

void model_expansion_begin(PublicationState& state) { state = {}; }

void model_publish_result(const Result& result, const CommitIdentity& identity,
    PublicationState& state)
{
    state = result.count_committed && !result.rolled_back && !result.route_blocked
        ? PublicationState{true, false, identity} : PublicationState{};
}

std::size_t model_published_count(PublicationState& state,
    const CommitIdentity& current, const bool profile_eligible,
    const bool playback_present)
{
    if (!(state.pending || state.active) || !profile_eligible) return kNativeRows;
    if (state.pending && !playback_present) return kNativeRows;
    const auto& a = state.identity;
    const auto& b = current;
    const bool matches = a.song == b.song && a.profile == b.profile
        && a.selection_generation == b.selection_generation
        && a.policy_generation == b.policy_generation
        && a.registry_generation == b.registry_generation
        && a.route_generation == b.route_generation
        && a.lease_generation == b.lease_generation && a.song_key == b.song_key
        && a.descriptor_hash == b.descriptor_hash
        && a.activation_generation == b.activation_generation
        && a.owner_generation == b.owner_generation && a.owner == b.owner
        && a.wrapper == b.wrapper && a.chart == b.chart && a.header == b.header
        && a.allocation == b.allocation;
    if (!matches) state = {};
    if (matches && state.pending) {
        state.pending = false;
        state.active = true;
    }
    return matches && state.active ? kPlayableRows : kNativeRows;
}

std::size_t model_presentation_published_count(PublicationState& state,
    const CommitIdentity& menu, const CommitIdentity* playback, const bool profile_eligible)
{
    if (!playback) return kNativeRows;
    if (model_published_count(state, *playback, profile_eligible) != kPlayableRows) {
        state = {};
        return kNativeRows;
    }
    const auto& committed = state.identity;
    const bool matches = menu.song == committed.song && menu.profile == committed.profile
        && menu.selection_generation == committed.selection_generation
        && menu.policy_generation == committed.policy_generation
        && menu.registry_generation == committed.registry_generation
        && menu.descriptor_hash == committed.descriptor_hash
        && menu.song == playback->song && menu.profile == playback->profile
        && menu.selection_generation == playback->selection_generation
        && menu.registry_generation == playback->registry_generation;
    if (!matches) state = {};
    return matches ? kPlayableRows : kNativeRows;
}

void model_shutdown(PublicationState& state) { state = {}; }
void model_activation_abort(PublicationState& state) { state = {}; }
} // namespace ff7r::piano::game::synthetic_model
