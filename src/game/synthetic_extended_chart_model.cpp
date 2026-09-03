#include "game/synthetic_extended_chart_model.h"

#include <cmath>
#include <limits>

namespace ff7r::piano::game::synthetic_model {
namespace {
bool restricted(const std::vector<SourceRow>& rows) {
    if (rows.size() < kMinPlayableRows || rows.size() > kMaxPlayableRows) return false;
    bool prefix_assignments[256]{};
    uint8_t prefix_lookup_path = 2;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        if (!row.monotone || row.chord || row.group || row.camera_transition || row.ignore_sound
            || row.strength != 0 || row.fname == 0 || row.assignment == 8 || row.lookup_path > 1)
            return false;
        if (index < kNativeRows) {
            if (index && row.lookup_path != prefix_lookup_path) return false;
            if (!index) prefix_lookup_path = row.lookup_path;
            prefix_assignments[row.assignment] = true;
        }
    }
    for (std::size_t index = kNativeRows; index < rows.size(); ++index) {
        if (rows[index].lookup_path != prefix_lookup_path
            || !prefix_assignments[rows[index].assignment]) return false;
    }
    return true;
}
}

std::size_t expected_capacity(const std::size_t target_count)
{
    constexpr std::size_t event_size = 0x90;
    constexpr std::size_t threshold = 0x20000;
    if (target_count < kMinPlayableRows || target_count > kMaxPlayableRows
        || target_count > (std::numeric_limits<std::size_t>::max)() / event_size) return 0;
    const std::size_t raw = target_count * event_size;
    const std::size_t quantum = raw <= threshold ? 0x1000 : 0x10000;
    if (raw > (std::numeric_limits<std::size_t>::max)() - (quantum - 1)) return 0;
    const std::size_t capacity = ((raw + quantum - 1) / quantum) * quantum / event_size;
    return capacity >= target_count && capacity <= kMaxPlayableRows ? capacity : 0;
}

Result run(const BuildRequest& request, Chart& chart) {
    Result result;
    const auto reject = [&](const RejectionReason reason,
        const std::size_t tail_index = static_cast<std::size_t>(-1)) {
        result.rejection_reason = reason;
        result.rejection_tail_index = tail_index;
    };
    const bool gate = request.rows && request.experiment_enabled && request.verified_1005
        && request.exact_caller && request.exact_header && request.exact_thread
        && request.depth_one && request.exact_generation && request.global_claim
        && request.controller_capture_read && request.controller_nonnull
        && request.control_block_nonnull && request.reciprocal_pre && request.header_pre
        && restricted(*request.rows);
    if (!gate) { result.forwarded = true; reject(RejectionReason::EarlyGate); return result; }
    result.target_count = request.rows->size();
    // Native production preflights every tail FName before reserve substitution.
    if (request.failure == FailurePoint::FNameFind) {
        result.forwarded = true;
        reject(RejectionReason::PreflightFName, 0);
        return result;
    }
    if (!request.reciprocal_reserve || !request.header_reserve) {
        result.forwarded = true;
        reject(RejectionReason::ReserveAuthority);
        return result;
    }
    result.substituted = true;
    if (request.second_reserve_hit) { result.forwarded = true; return result; }
    const std::size_t required_capacity = expected_capacity(result.target_count);
    const std::size_t actual_capacity = request.reserve_capacity
        ? request.reserve_capacity : required_capacity;
    if (!required_capacity || actual_capacity != required_capacity) {
        reject(RejectionReason::ReserveCapacity);
        return result;
    }

    chart.storage.clear(); chart.storage.reserve(actual_capacity); chart.capacity = actual_capacity;
    for (std::size_t i = 0; i < kNativeRows; ++i)
        chart.storage.push_back({i, static_cast<uint32_t>(2 * i), (*request.rows)[i].time, true});
    chart.count = kNativeRows; chart.max_time = request.rows->at(kNativeRows - 1).time;
    if (!request.reciprocal_post || !request.header_post) {
        reject(RejectionReason::PostAuthority);
        return result;
    }
    if (!request.post_original_fps_read) {
        reject(RejectionReason::PostFpsRead);
        return result;
    }
    if (!request.post_original_fps_valid) {
        reject(RejectionReason::PostFpsInvalid);
        return result;
    }
    if (request.failure == FailurePoint::PrefixValidation) {
        reject(RejectionReason::PrefixValidation);
        return result;
    }
    result.prefix_validated = true;

    // Production decodes all tail times from the parser-published post-original
    // FPS before constructing the first tail. Pre-original FPS is ignored.
    float previous = request.rows->at(kNativeRows - 1).time;
    if (!std::isfinite(previous)) {
        reject(RejectionReason::PrefixValidation);
        return result;
    }
    for (std::size_t i = 0; i < result.target_count - kNativeRows; ++i) {
        const SourceRow& row = request.rows->at(kNativeRows + i);
        if (!row.time_parse_valid || !std::isfinite(row.time)) {
            reject(RejectionReason::TailTimeParse, i);
            return result;
        }
        if (row.time < previous) {
            reject(RejectionReason::TailTimeOrder, i);
            return result;
        }
        previous = row.time;
    }

    const auto cleanup = [&] {
        for (std::size_t row = chart.storage.size(); row > kNativeRows; --row)
            chart.destroyed_tail_indices.push_back(row - 1 - kNativeRows);
        chart.tail_destruct_count += chart.storage.size() - kNativeRows;
        chart.storage.resize(kNativeRows);
        chart.tail_destructed = chart.tail_destruct_count != 0;
    };
    const std::size_t tail_count = result.target_count - kNativeRows;
    for (std::size_t i = 0; i < tail_count; ++i) {
        const std::size_t row = kNativeRows + i;
        chart.storage.push_back({row, static_cast<uint32_t>(2 * row), (*request.rows)[row].time, false});
        chart.tail_constructor_entered = true; result.tail_constructed = true;
        if (i == request.failure_tail_index
            && request.failure == FailurePoint::ConstructorReturnedNonTail) {
            chart.ownership_preserved = true; result.route_blocked = true; return result;
        }
        result.constructor_returned_tail = true;
        if (i == request.failure_tail_index
            && (request.failure == FailurePoint::ConstructorValidation
                || request.failure == FailurePoint::CallbackBuild
                || request.failure == FailurePoint::CallbackValidation)) {
            cleanup(); return result;
        }
        if (i == request.failure_tail_index
            && request.failure == FailurePoint::CallbackCleanupIdentityFailure) {
            chart.ownership_preserved = true; result.route_blocked = true; return result;
        }
        chart.storage.back().callback_valid = true;
        ++result.constructed_tail_count;
    }
    result.callback_validated = true;
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
    chart.count = result.target_count; result.count_committed = true;
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

void model_expansion_begin(PublicationState& state) {
    state.pending = false; state.active = false; state.identity = {};
}

bool model_transaction_begin(const PublicationState& state,
    const CommitIdentity& identity)
{
    return !state.failed_terminal
        || state.failed_lifecycle_epoch != identity.route_lifecycle_epoch
        || identity.activation_generation > state.failed_generation;
}

bool model_publish_result(Result& result, Chart& chart, const CommitIdentity& identity,
    PublicationState& state)
{
    const bool invalid_success = state.successful_transition_delivered
        && state.successful_generation >= identity.activation_generation
        && (state.successful_generation != identity.activation_generation
            || !state.successful_transition_exact
            || identity.route_lifecycle_epoch == UINT64_MAX
            || state.successful_lifecycle_epoch_before
                != identity.route_lifecycle_epoch
            || state.successful_lifecycle_epoch_after
                != identity.route_lifecycle_epoch + 1);
    if ((state.failed_terminal
            && state.failed_lifecycle_epoch == identity.route_lifecycle_epoch
            && state.failed_generation >= identity.activation_generation)
        || invalid_success) {
        if (result.count_committed && chart.count == identity.target_count) {
            chart.count = kNativeRows;
            chart.storage.resize(kNativeRows);
            chart.tail_destructed = true;
            result.rolled_back = true;
            result.count_restore_proved = true;
            result.max_restore_proved = true;
        }
        state.pending = false; state.active = false; state.identity = {};
        return false;
    }
    if (result.count_committed && !result.rolled_back && !result.route_blocked) {
        state.pending = true; state.active = false; state.identity = identity;
        return true;
    }
    state.pending = false; state.active = false; state.identity = {};
    return false;
}

void model_publish_result(const Result& result, const CommitIdentity& identity,
    PublicationState& state)
{
    const bool invalid_success = state.successful_transition_delivered
        && state.successful_generation >= identity.activation_generation
        && (state.successful_generation != identity.activation_generation
            || !state.successful_transition_exact
            || identity.route_lifecycle_epoch == UINT64_MAX
            || state.successful_lifecycle_epoch_before
                != identity.route_lifecycle_epoch
            || state.successful_lifecycle_epoch_after
                != identity.route_lifecycle_epoch + 1);
    if (result.count_committed && !result.rolled_back && !result.route_blocked
        && !invalid_success) {
        state.pending = true; state.active = false; state.identity = identity;
    } else {
        state.pending = false; state.active = false; state.identity = {};
    }
}

std::size_t model_published_count(PublicationState& state,
    const CommitIdentity& current, const bool profile_eligible,
    const bool playback_present)
{
    if (!(state.pending || state.active) || !profile_eligible) return kNativeRows;
    if (!playback_present) {
        if (state.active) {
            state.pending = false; state.active = false; state.identity = {};
        }
        return kNativeRows;
    }
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
        && a.preparation_ordinal == b.preparation_ordinal
        && a.route_lifecycle_epoch == b.route_lifecycle_epoch
        && a.target_count == b.target_count;
    if (!matches) {
        state.pending = false; state.active = false; state.identity = {};
        return kNativeRows;
    }
    const bool transition_awaiting = !state.successful_transition_delivered
        || state.successful_generation < a.activation_generation;
    if (transition_awaiting) {
        if (state.active) {
            state.pending = false; state.active = false; state.identity = {};
        }
        return kNativeRows;
    }
    const bool lifecycle_succeeded = state.successful_transition_exact
        && state.successful_generation == a.activation_generation
        && state.successful_lifecycle_epoch_before == a.route_lifecycle_epoch
        && a.route_lifecycle_epoch != UINT64_MAX
        && state.successful_lifecycle_epoch_after == a.route_lifecycle_epoch + 1;
    if (!lifecycle_succeeded) {
        state.pending = false; state.active = false; state.identity = {};
        return kNativeRows;
    }
    if (matches && state.pending) {
        state.pending = false;
        state.active = true;
    }
    return matches && state.active ? a.target_count : kNativeRows;
}

std::size_t model_presentation_published_count(PublicationState& state,
    const CommitIdentity& menu, const CommitIdentity* playback, const bool profile_eligible)
{
    if (!playback) {
        if (state.active) {
            state.pending = false; state.active = false; state.identity = {};
        }
        return kNativeRows;
    }
    const auto& committed = state.identity;
    const bool menu_matches = menu.song == committed.song && menu.profile == committed.profile
        && menu.selection_generation == committed.selection_generation
        && menu.policy_generation == committed.policy_generation
        && menu.registry_generation == committed.registry_generation
        && menu.descriptor_hash == committed.descriptor_hash
        && menu.song == playback->song && menu.profile == playback->profile
        && menu.selection_generation == playback->selection_generation
        && menu.registry_generation == playback->registry_generation;
    if (!menu_matches) {
        state.pending = false; state.active = false; state.identity = {};
        return kNativeRows;
    }
    return model_published_count(state, *playback, profile_eligible);
}

void model_shutdown(PublicationState& state) { state = {}; }
void model_configuration_reset(PublicationState& state) { state = {}; }
void model_activation_abort(PublicationState& state) { state = {}; }

void model_terminal(PublicationState& state, const std::uint64_t generation,
    const std::uint64_t lifecycle_epoch, const TerminalOutcome outcome,
    const std::uint64_t successful_lifecycle_epoch)
{
    if (outcome == TerminalOutcome::AudioPublished) {
        const bool exact = lifecycle_epoch != 0 && lifecycle_epoch != UINT64_MAX
            && successful_lifecycle_epoch == lifecycle_epoch + 1;
        if (!state.successful_transition_delivered
            || generation > state.successful_generation) {
            state.successful_transition_delivered = true;
            state.successful_transition_exact = exact;
            state.successful_generation = generation;
            state.successful_lifecycle_epoch_before = lifecycle_epoch;
            state.successful_lifecycle_epoch_after = successful_lifecycle_epoch;
        }
        return;
    }
    if (outcome == TerminalOutcome::ExpandFinished)
        return;
    if (!state.failed_terminal || generation > state.failed_generation) {
        state.failed_terminal = true;
        state.failed_generation = generation;
        state.failed_lifecycle_epoch = lifecycle_epoch;
    }
    if ((state.pending || state.active)
        && state.identity.activation_generation == generation
        && state.identity.route_lifecycle_epoch == lifecycle_epoch) {
        state.pending = false; state.active = false; state.identity = {};
    }
    if (state.successful_transition_delivered && state.successful_generation == generation
        && state.successful_lifecycle_epoch_before == lifecycle_epoch) {
        state.successful_transition_delivered = false;
        state.successful_transition_exact = false;
        state.successful_generation = 0;
        state.successful_lifecycle_epoch_before = 0;
        state.successful_lifecycle_epoch_after = 0;
    }
}

GeneralizedResult run_generalized(const GeneralizedRequest& request)
{
    GeneralizedResult result{};
    if (!request.plan || !request.plan->valid() || !request.verified_1005
        || !request.eligible || request.plan->source_row_count <= kNativeRows
        || request.plan->native_event_count > kMaxPlayableRows
        || request.plan->native_prefix_event_count > kNativeRows * 2
        || request.plan->native_event_count <= request.plan->native_prefix_event_count
        || request.plan->required_action_count
            != request.plan->native_event_count - request.plan->links.size()
        || request.failure == GeneralizedFailure::Preflight) return result;
    const auto& plan = *request.plan;
    if (plan.events.size() != plan.native_event_count) return result;
    std::vector<int32_t> parents(plan.events.size(), -1);
    for (std::size_t i = 0; i < plan.events.size(); ++i) {
        const auto& event = plan.events[i];
        const uint32_t expected_ordinal = static_cast<uint32_t>(event.source_row_index * 2u
            + (event.kind == ff7rp::pipeline::ChartEventKind::Chord ? 1u : 0u));
        if (event.compact_event_index != i || event.source_row_index >= plan.source_row_count
            || event.ordinal != expected_ordinal
            || ((i < plan.native_prefix_event_count) != (event.source_row_index < kNativeRows)))
            return result;
    }
    for (const auto& link : plan.links) {
        if (link.root_event_index >= plan.events.size()
            || link.child_event_index >= plan.events.size()
            || link.root_event_index == link.child_event_index
            || parents[link.root_event_index] != -1
            || parents[link.child_event_index] != -1) return result;
        parents[link.child_event_index] = static_cast<int32_t>(link.root_event_index);
        std::size_t depth = 0;
        for (int32_t current = static_cast<int32_t>(link.child_event_index);
             current >= 0; current = parents[static_cast<std::size_t>(current)]) {
            if (++depth > parents.size()) return result;
        }
    }
    result.parser_count = plan.native_prefix_event_count;
    result.reserve_target = plan.native_event_count;
    result.final_count = result.parser_count;
    result.reserve_substituted = true;
    const std::size_t tail_count = plan.native_event_count - plan.native_prefix_event_count;
    auto clean_tails = [&](const std::size_t count) {
        for (std::size_t i = count; i; --i)
            result.destroyed_compact_indices.push_back(plan.native_prefix_event_count + i - 1);
    };
    for (std::size_t i = 0; i < tail_count; ++i) {
        if (request.failure == GeneralizedFailure::ConstructorNonTail
            && request.failure_index == i) {
            result.ownership_preserved = true;
            return result;
        }
        ++result.constructed_tails;
        if ((request.failure == GeneralizedFailure::IgnoreSound
                || request.failure == GeneralizedFailure::Callback)
            && request.failure_index == i) {
            clean_tails(result.constructed_tails);
            result.constructed_tails = 0;
            result.rollback_completed = true;
            return result;
        }
    }
    for (std::size_t i = 0; i < plan.links.size(); ++i) {
        if (request.failure == GeneralizedFailure::Link && request.failure_index == i) {
            for (std::size_t j = result.applied_links; j; --j)
                result.rolled_back_link_indices.push_back(j - 1);
            clean_tails(result.constructed_tails);
            result.constructed_tails = 0;
            result.applied_links = 0;
            result.rollback_completed = true;
            return result;
        }
        ++result.applied_links;
    }
    if (request.failure == GeneralizedFailure::UncertainOwnership) {
        result.ownership_preserved = true;
        return result;
    }
    if (request.failure == GeneralizedFailure::MaxPublish) {
        for (std::size_t j = result.applied_links; j; --j)
            result.rolled_back_link_indices.push_back(j - 1);
        clean_tails(result.constructed_tails);
        result.constructed_tails = result.applied_links = 0;
        result.rollback_completed = true;
        return result;
    }
    result.final_count = plan.native_event_count;
    result.count_committed = true;
    if (request.failure == GeneralizedFailure::CountValidation) {
        result.final_count = plan.native_prefix_event_count;
        for (std::size_t j = result.applied_links; j; --j)
            result.rolled_back_link_indices.push_back(j - 1);
        clean_tails(result.constructed_tails);
        result.constructed_tails = result.applied_links = 0;
        result.rollback_completed = true;
        return result;
    }
    result.published_actions = plan.required_action_count;
    return result;
}
} // namespace ff7r::piano::game::synthetic_model
