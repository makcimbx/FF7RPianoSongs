#pragma once

#include "game/audio_cleanup_policy.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <utility>

namespace ff7r::piano::game {

class BgmPlaybackAggregateMutationGate final {
public:
    bool enter() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return false;
            ++active_;
            return true;
        } catch (...) {
            return false;
        }
    }

    void leave() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ > 0) --active_;
            cv_.notify_all();
        } catch (...) {
        }
    }

    void close_and_drain() noexcept
    {
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            closed_ = true;
            cv_.wait(lock, [&] { return active_ == 0; });
        } catch (...) {
        }
    }

    template <typename Publish>
    void close_and_drain(Publish&& publish) noexcept
    {
        static_assert(noexcept(publish()));
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            closed_ = true;
            publish();
            cv_.wait(lock, [&] { return active_ == 0; });
        } catch (...) {
        }
    }

    template <typename Operation>
    bool while_open(Operation&& operation) noexcept
    {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return false;
            operation();
            return true;
        } catch (...) {
            return false;
        }
    }

    void reopen() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = false;
            cv_.notify_all();
        } catch (...) {
        }
    }

    template <typename Predicate>
    bool reopen_after_drain_if(Predicate&& predicate) noexcept
    {
        static_assert(noexcept(predicate()));
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ != 0 || !predicate()) return false;
            closed_ = false;
            cv_.notify_all();
            return true;
        } catch (...) {
            return false;
        }
    }

    bool closed() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            return closed_;
        } catch (...) {
            return true;
        }
    }

    bool drained() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            return active_ == 0;
        } catch (...) {
            return false;
        }
    }

    bool try_drained() noexcept
    {
        try {
            std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
            return lock.owns_lock() && active_ == 0;
        } catch (...) { return false; }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;
    uint32_t active_ = 0;
};

template <typename Gate>
class BgmPlaybackAggregateMutationLease final {
public:
    template <typename ExitRequested>
    BgmPlaybackAggregateMutationLease(
        Gate& gate, ExitRequested&& exit_requested) noexcept
        : gate_(&gate)
    {
        static_assert(noexcept(exit_requested()));
        if (!exit_requested()) {
            active = gate_->enter();
            if (active && exit_requested()) (void)release();
        }
    }

    BgmPlaybackAggregateMutationLease(
        const BgmPlaybackAggregateMutationLease&) = delete;
    BgmPlaybackAggregateMutationLease& operator=(
        const BgmPlaybackAggregateMutationLease&) = delete;

    BgmPlaybackAggregateMutationLease(
        BgmPlaybackAggregateMutationLease&& other) noexcept
        : active(std::exchange(other.active, false)),
          gate_(std::exchange(other.gate_, nullptr))
    {
    }

    BgmPlaybackAggregateMutationLease& operator=(
        BgmPlaybackAggregateMutationLease&& other) noexcept
    {
        if (this != &other) {
            (void)release();
            active = std::exchange(other.active, false);
            gate_ = std::exchange(other.gate_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] bool release() noexcept
    {
        const bool was_active = active;
        if (active && gate_) gate_->leave();
        active = false;
        gate_ = nullptr;
        return was_active;
    }

    ~BgmPlaybackAggregateMutationLease() noexcept { (void)release(); }

    bool active = false;

private:
    Gate* gate_ = nullptr;
};

enum class BgmPlaybackAggregateReleaseClaimState : uint8_t {
    Pending,
    InFlight,
    Retained,
    Complete,
};

struct BgmPlaybackAggregateReleaseClaimMachine {
    BgmPlaybackAggregateReleaseClaimState state =
        BgmPlaybackAggregateReleaseClaimState::Complete;
    uint64_t generation = 0;
    uint64_t claim_id = 0;

    bool publish_pending(bool slot_available, uint64_t next_generation) noexcept
    {
        if (!slot_available || state == BgmPlaybackAggregateReleaseClaimState::InFlight
            || next_generation == 0) return false;
        state = BgmPlaybackAggregateReleaseClaimState::Pending;
        generation = next_generation;
        claim_id = 0;
        return true;
    }

    bool take(uint64_t expected_generation, uint64_t next_claim_id) noexcept
    {
        if (state != BgmPlaybackAggregateReleaseClaimState::Pending
            || generation != expected_generation || next_claim_id == 0) return false;
        state = BgmPlaybackAggregateReleaseClaimState::InFlight;
        claim_id = next_claim_id;
        return true;
    }

    bool complete(uint64_t expected_generation, uint64_t expected_claim_id,
        bool succeeded) noexcept
    {
        if (state != BgmPlaybackAggregateReleaseClaimState::InFlight
            || generation != expected_generation || claim_id != expected_claim_id) {
            return false;
        }
        state = succeeded ? BgmPlaybackAggregateReleaseClaimState::Complete
                          : BgmPlaybackAggregateReleaseClaimState::Retained;
        return true;
    }
};

// Observation-only policy for the native playback transfer seam.  These POD
// facts deliberately carry no action and can never authorize a mutation.
struct BgmPlaybackTransferSnapshot final {
    bool readable = false;
    uintptr_t sound = 0;
    uintptr_t moved_handle = 0;
    uint32_t index50 = 0;
    uint32_t index54 = 0;
    uint32_t metadata68 = 0;
    uint32_t metadata6c = 0;
    uint64_t optional_token = 0;
};

constexpr bool bgm_playback_transfer_source_zeroed(
    const BgmPlaybackTransferSnapshot& after) noexcept
{
    return after.readable && after.moved_handle == 0
        && after.optional_token == 0;
}

constexpr bool bgm_playback_transfer_destination_adopted(
    const BgmPlaybackTransferSnapshot& source_before,
    const BgmPlaybackTransferSnapshot& destination_after) noexcept
{
    const bool optional_token_moved = source_before.optional_token == 0
        ? destination_after.optional_token == 0
        : destination_after.optional_token == source_before.optional_token;
    return source_before.readable && destination_after.readable
        && source_before.sound != 0 && source_before.moved_handle != 0
        && destination_after.sound == source_before.sound
        && destination_after.moved_handle == source_before.moved_handle
        && destination_after.index50 == source_before.index50
        && destination_after.index54 == source_before.index54
        && destination_after.metadata68 == source_before.metadata68
        && destination_after.metadata6c == source_before.metadata6c
        && optional_token_moved;
}

struct BgmPlaybackCanonicalStopBoundaryFacts final {
    bool parent_active = false;
    bool parent_anchored = false;
    bool canonical_play_confirmed = false;
    bool canonical_handle_exact = false;
    bool canonical_sound_pointer_exact = false;
    bool canonical_sound_identity_exact = false;
    bool controller_chain_exact = false;
    bool owner_thread_exact = false;
    bool owner_command_exact = false;
    bool lease_lifecycle_tokens_exact = false;
    bool boundary_clear = false;
};

enum class BgmPlaybackCanonicalParentFailure : uint8_t {
    None,
    ParentInactive,
    ParentUnanchored,
    CanonicalPlayUnconfirmed,
    CanonicalHandleMismatch,
    CanonicalSoundPointerMismatch,
    CanonicalSoundIdentityMismatch,
    ControllerIdentityMismatch,
    LiveSlotBgmMismatch,
    OwnerThreadMismatch,
    OwnerCommandMismatch,
    RouteSuccessorMismatch,
    LeaseLifecycleTokenMismatch,
    VersionOrBoundaryReuse,
};

struct BgmPlaybackCanonicalParentLookupFacts final {
    bool parent_active = false;
    bool parent_anchored = false;
    bool canonical_play_confirmed = false;
    bool canonical_handle_exact = false;
    bool canonical_sound_pointer_exact = false;
    bool canonical_sound_identity_exact = false;
    bool controller_identity_exact = false;
    bool live_slot_bgm_exact = false;
    bool owner_thread_exact = false;
    bool owner_command_exact = false;
    bool route_successor_exact = false;
    bool lease_lifecycle_tokens_exact = false;
    bool version_and_boundary_clear = false;
};

constexpr BgmPlaybackCanonicalParentFailure
bgm_playback_canonical_parent_first_failure(
    const BgmPlaybackCanonicalParentLookupFacts& f) noexcept
{
    if (!f.parent_active) return BgmPlaybackCanonicalParentFailure::ParentInactive;
    if (!f.parent_anchored) return BgmPlaybackCanonicalParentFailure::ParentUnanchored;
    if (!f.canonical_play_confirmed) return BgmPlaybackCanonicalParentFailure::CanonicalPlayUnconfirmed;
    if (!f.canonical_handle_exact) return BgmPlaybackCanonicalParentFailure::CanonicalHandleMismatch;
    if (!f.canonical_sound_pointer_exact) return BgmPlaybackCanonicalParentFailure::CanonicalSoundPointerMismatch;
    if (!f.canonical_sound_identity_exact) return BgmPlaybackCanonicalParentFailure::CanonicalSoundIdentityMismatch;
    if (!f.controller_identity_exact) return BgmPlaybackCanonicalParentFailure::ControllerIdentityMismatch;
    if (!f.live_slot_bgm_exact) return BgmPlaybackCanonicalParentFailure::LiveSlotBgmMismatch;
    if (!f.owner_thread_exact) return BgmPlaybackCanonicalParentFailure::OwnerThreadMismatch;
    if (!f.owner_command_exact) return BgmPlaybackCanonicalParentFailure::OwnerCommandMismatch;
    if (!f.route_successor_exact) return BgmPlaybackCanonicalParentFailure::RouteSuccessorMismatch;
    if (!f.lease_lifecycle_tokens_exact) return BgmPlaybackCanonicalParentFailure::LeaseLifecycleTokenMismatch;
    if (!f.version_and_boundary_clear) return BgmPlaybackCanonicalParentFailure::VersionOrBoundaryReuse;
    return BgmPlaybackCanonicalParentFailure::None;
}

constexpr bool bgm_playback_exact_successor(
    uint64_t predecessor, uint64_t successor) noexcept
{
    return predecessor != 0 && predecessor != UINT64_MAX
        && successor == predecessor + 1;
}

constexpr bool bgm_playback_initial_predecessor_publication_exact(
    bool fresh_lineage, bool pre_session, bool retirement_candidate,
    bool record_uninitialized, uint64_t captured_nonce,
    uint64_t synchronized_global_nonce) noexcept
{
    return fresh_lineage && pre_session && retirement_candidate
        && record_uninitialized
        && captured_nonce == synchronized_global_nonce;
}

constexpr bool bgm_playback_boundary_destination_exact(
    bool vector_readable, bool member_present, bool pointer_exact,
    bool handle_exact, bool sound_pointer_exact,
    bool sound_positive_identity_exact, bool destination_reused) noexcept
{
    return vector_readable && member_present && pointer_exact && handle_exact
        && sound_pointer_exact && sound_positive_identity_exact
        && !destination_reused;
}

constexpr bool bgm_playback_post_set_snapshot_exact(
    bool slot_bgm_exact, bool route_successor_exact, bool lease_exact,
    bool lifecycle_state_exact, bool token_ordinal_exact,
    bool token_values_exact, bool owner_thread_exact,
    bool prepared_command_exact) noexcept
{
    return slot_bgm_exact && route_successor_exact && lease_exact
        && lifecycle_state_exact && token_ordinal_exact && token_values_exact
        && owner_thread_exact && prepared_command_exact;
}

constexpr bool bgm_playback_canonical_stop_boundary_exact(
    const BgmPlaybackCanonicalStopBoundaryFacts& f) noexcept
{
    return f.parent_active && f.parent_anchored
        && f.canonical_play_confirmed && f.canonical_handle_exact
        && f.canonical_sound_pointer_exact
        && f.canonical_sound_identity_exact && f.controller_chain_exact
        && f.owner_thread_exact && f.owner_command_exact
        && f.lease_lifecycle_tokens_exact && f.boundary_clear;
}

struct BgmPlaybackBoundarySetFacts final {
    bool boundary_active = false;
    bool boundary_unconsumed = false;
    bool parent_version_exact = false;
    bool retained_custom_pointer_exact = false;
    bool retained_custom_identity_exact = false;
    bool controller_chain_exact = false;
    bool owner_thread_exact = false;
    bool owner_command_exact = false;
    bool lease_lifecycle_tokens_exact = false;
    bool request_strictly_newer = false;
};

constexpr bool bgm_playback_boundary_set_exact(
    const BgmPlaybackBoundarySetFacts& f) noexcept
{
    return f.boundary_active && f.boundary_unconsumed
        && f.parent_version_exact && f.retained_custom_pointer_exact
        && f.retained_custom_identity_exact && f.controller_chain_exact
        && f.owner_thread_exact && f.owner_command_exact
        && f.lease_lifecycle_tokens_exact && f.request_strictly_newer;
}

constexpr bool bgm_playback_transition_consumable(
    bool set_committed, bool play_confirmed_state4,
    bool exact_set_created_handle, bool exact_positive_identity,
    bool already_consumed) noexcept
{
    return set_committed && play_confirmed_state4
        && exact_set_created_handle && exact_positive_identity
        && !already_consumed;
}

constexpr bool bgm_playback_boundary_must_clear(
    bool mismatch, bool fault, bool list_exit, bool shutdown,
    bool version_or_aba_drift, bool unrelated_transfer) noexcept
{
    return mismatch || fault || list_exit || shutdown
        || version_or_aba_drift || unrelated_transfer;
}

constexpr bool bgm_playback_blocked_canonical_boundary_invalidates(
    bool canonical_transfer_recognized, bool transfer_blocked,
    bool boundary_active, bool boundary_applies) noexcept
{
    return canonical_transfer_recognized && transfer_blocked
        && boundary_active && boundary_applies;
}

constexpr bool bgm_playback_intervening_operation_invalidated_exact(
    bool recognized, bool applicable, bool boundary_cleared,
    bool pending_cleared, bool record_version_advanced,
    bool collection_version_advanced, bool nonce_advanced) noexcept
{
    return recognized && applicable && boundary_cleared && pending_cleared
        && record_version_advanced && collection_version_advanced
        && nonce_advanced;
}

enum class BgmPlaybackAggregateMarker : uint8_t {
    Invalid,
    FinalActiveBorrower,
    ReleaseBlockedBorrowers,
    ReleaseCommittedBorrowers,
    AggregateClosed,
    ListExitPending,
    ListExitClosed,
    ShutdownRetained,
    ShutdownClosed,
};

enum class BgmPlaybackLineageTerminalState : uint8_t {
    Active,
    SupersededPending,
    SupersededClosed,
    Failed,
};

enum class BgmPlaybackAggregateRetirementSuccessorClassification : uint8_t {
    NotDirectSuccessorRefusal,
    ActiveExactSuccessor,
    TerminalExactSuccessor,
    AggregateFailure,
    IdentityMismatch,
    VersionDrift,
    IncompleteSuccessor,
};

struct BgmPlaybackAggregateRetirementSuccessorFacts final {
    bool direct_successor_cleanup_refused = false;
    uint8_t monitor_origin = 0;
    uint64_t monitor_epoch = 0;
    uint64_t monitor_route_generation = 0;
    uint64_t monitor_request_handle = 0;
    uint64_t monitor_request_generation = 0;
    uint64_t live_route_generation = 0;
    bool aggregate_snapshot_found = false;
    uint64_t aggregate_ordinal = 0;
    uint64_t aggregate_version = 0;
    uint64_t collection_version = 0;
    bool aggregate_active = false;
    bool aggregate_failed = false;
    BgmPlaybackLineageTerminalState terminal_state =
        BgmPlaybackLineageTerminalState::Active;
    bool aggregate_marker_closed = false;
    bool anchor_exact = false;
    bool controller_exact = false;
    bool lease_exact = false;
    bool lifecycle_exact = false;
    bool tokens_exact = false;
    bool sound_exact = false;
    uint64_t old_request_generation = 0;
    uint64_t canonical_request_generation = 0;
    uint64_t latest_custom_request_generation = 0;
    uint64_t old_request_handle = 0;
    uint64_t canonical_request_handle = 0;
    uint64_t latest_custom_request_handle = 0;
    bool old_absent = false;
    bool canonical_absent = false;
    bool latest_custom_absent = false;
    bool latest_custom_consumed = false;
    uint64_t anchor_route_generation = 0;
    uint64_t canonical_input_route_generation = 0;
    uint64_t canonical_route_generation = 0;
    uint64_t latest_custom_route_generation = 0;
    uint64_t operation_predecessor_nonce = 0;
    uint64_t transition_set_nonce = 0;
    uint64_t transition_play_nonce = 0;
    bool operation_nonce_exact = false;
    bool route_chain_exact = false;
    bool journals_restored = false;
    bool frozen_fields_restored = false;
    bool owner_field_observed = false;
    bool owner_field_empty = false;
    bool native_route_empty = false;
    bool native_route_identity_exact = false;
    bool version_recheck_exact = false;
};

constexpr BgmPlaybackAggregateRetirementSuccessorClassification
classify_bgm_playback_aggregate_retirement_successor(
    const BgmPlaybackAggregateRetirementSuccessorFacts& f) noexcept
{
    using Result = BgmPlaybackAggregateRetirementSuccessorClassification;
    if (!f.direct_successor_cleanup_refused
        || f.monitor_route_generation == 0
        || f.monitor_route_generation > UINT64_MAX - 2
        || f.live_route_generation != f.monitor_route_generation + 2) {
        return Result::NotDirectSuccessorRefusal;
    }
    if (!f.aggregate_snapshot_found || !f.aggregate_marker_closed) {
        return Result::IncompleteSuccessor;
    }
    if ((f.monitor_origin != 1 && f.monitor_origin != 2)
        || f.monitor_epoch == 0 || f.monitor_request_handle == 0
        || f.aggregate_ordinal == 0 || f.aggregate_version == 0
        || f.collection_version == 0) {
        return Result::IncompleteSuccessor;
    }
    if (f.aggregate_failed
        || f.terminal_state == BgmPlaybackLineageTerminalState::Failed) {
        return Result::AggregateFailure;
    }
    if (!f.version_recheck_exact) return Result::VersionDrift;
    if (!f.anchor_exact || !f.controller_exact || !f.lease_exact
        || !f.lifecycle_exact || !f.tokens_exact || !f.sound_exact) {
        return Result::IdentityMismatch;
    }
    if (!f.old_absent
        || f.old_request_generation == 0
        || f.old_request_generation != f.monitor_request_generation
        || f.old_request_handle == 0
        || f.old_request_handle != f.monitor_request_handle
        || f.canonical_request_generation == 0
        || f.latest_custom_request_generation == 0
        || f.canonical_request_handle == 0
        || f.latest_custom_request_handle == 0
        || f.canonical_request_handle == f.old_request_handle
        || f.latest_custom_request_handle == f.old_request_handle
        || f.latest_custom_request_handle == f.canonical_request_handle
        || (f.canonical_request_generation != 0 && !f.canonical_absent)
        || (f.latest_custom_request_generation != 0
            && (!f.latest_custom_absent || !f.latest_custom_consumed))
        || !f.operation_nonce_exact || !f.route_chain_exact
        || !f.journals_restored || !f.frozen_fields_restored
        || !f.owner_field_observed || !f.owner_field_empty
        || !f.native_route_empty) {
        return Result::IncompleteSuccessor;
    }
    if (f.terminal_state == BgmPlaybackLineageTerminalState::Active) {
        return f.aggregate_active
            ? Result::ActiveExactSuccessor : Result::IncompleteSuccessor;
    }
    if (f.terminal_state == BgmPlaybackLineageTerminalState::SupersededClosed) {
        return !f.aggregate_active
            ? Result::TerminalExactSuccessor : Result::IncompleteSuccessor;
    }
    return Result::IncompleteSuccessor;
}

static_assert(std::is_trivially_copyable_v<
    BgmPlaybackAggregateRetirementSuccessorFacts>);

enum class BgmPlaybackAggregateTerminalHandoffClassification : uint8_t {
    NotCandidate,
    NoQuiescentMonitor,
    AggregateFailure,
    VersionDrift,
    RootNotFound,
    LineageBranch,
    LineageCycle,
    LineageMismatch,
    BackingMismatch,
    LifecycleMismatch,
    OwnerMismatch,
    FrozenOrJournalMismatch,
    PauseConflict,
    RegistryMismatch,
    NativeRouteNotEmpty,
    ExactAtAggregateClosed,
    ExactAtNaturalStop,
};

enum class BgmPlaybackAggregateTerminalOwnerCategory : uint8_t {
    Unreadable,
    Zero,
    Canonical,
    DetachedCustom,
    Foreign,
};

struct BgmPlaybackAggregateTerminalLineageNode final {
    bool present = false;
    bool domain_exact = false;
    bool active = false;
    bool failed = false;
    bool aggregate_closed = false;
    bool borrowers_zero = false;
    BgmPlaybackLineageTerminalState terminal_state =
        BgmPlaybackLineageTerminalState::Active;
    uint64_t ordinal = 0;
    uint64_t version = 0;
    uint64_t old_request = 0;
    uint64_t canonical_request = 0;
    uint64_t latest_custom_request = 0;
    uint32_t old_request_generation = 0;
    uint32_t canonical_request_generation = 0;
    uint32_t latest_custom_request_generation = 0;
    bool old_absent = false;
    bool canonical_absent = false;
    bool latest_custom_absent = false;
    bool latest_custom_consumed = false;
    uint64_t child_ordinal = 0;
    uint64_t anchor_route_generation = 0;
    uint64_t lineage_route_predecessor_generation = 0;
    uint64_t canonical_set_input_route_generation = 0;
    uint64_t canonical_route_generation = 0;
    uint64_t latest_custom_route_generation = 0;
    uint64_t operation_predecessor_nonce = 0;
    uint64_t canonical_set_nonce = 0;
    uint64_t canonical_play_nonce = 0;
    uint64_t stop_boundary_nonce = 0;
    uint64_t custom_set_nonce = 0;
    uint64_t custom_play_nonce = 0;
    bool stop_boundary_consumed = false;
};

struct BgmPlaybackAggregateTerminalLineageResult final {
    bool aggregate_failure = false;
    bool root_found = false;
    bool branch = false;
    bool cycle = false;
    bool exact = false;
    uint8_t root_index = 0xff;
    uint8_t leaf_index = 0xff;
    uint8_t path_count = 0;
};

enum class BgmPlaybackAggregateTerminalSemanticDeltaClassification : uint8_t {
    NotCompared,
    NoDelta,
    VersionOnly,
    MarkerOnly,
    ExactBoundaryInvalidation,
    NodeSetDrift,
    ActiveMutation,
    FailureMutation,
    TerminalMutation,
    RequestMutation,
    AbsenceMutation,
    PendingSetMutation,
    BoundaryMutation,
    ChildMutation,
    NonceMutation,
    RouteMutation,
    MultipleSemanticMutations,
};

enum BgmPlaybackAggregateTerminalSemanticDeltaMask : uint16_t {
    BgmPlaybackAggregateTerminalSemanticDeltaNone = 0,
    BgmPlaybackAggregateTerminalSemanticDeltaActive = 1u << 0,
    BgmPlaybackAggregateTerminalSemanticDeltaFailure = 1u << 1,
    BgmPlaybackAggregateTerminalSemanticDeltaTerminal = 1u << 2,
    BgmPlaybackAggregateTerminalSemanticDeltaMarker = 1u << 3,
    BgmPlaybackAggregateTerminalSemanticDeltaRequest = 1u << 4,
    BgmPlaybackAggregateTerminalSemanticDeltaAbsence = 1u << 5,
    BgmPlaybackAggregateTerminalSemanticDeltaPendingSet = 1u << 6,
    BgmPlaybackAggregateTerminalSemanticDeltaBoundary = 1u << 7,
    BgmPlaybackAggregateTerminalSemanticDeltaChild = 1u << 8,
    BgmPlaybackAggregateTerminalSemanticDeltaNonce = 1u << 9,
    BgmPlaybackAggregateTerminalSemanticDeltaRoute = 1u << 10,
};

struct BgmPlaybackAggregateTerminalSemanticNode final {
    bool present = false;
    uint64_t ordinal = 0;
    uint64_t version = 0;
    bool active = false;
    bool failed = false;
    BgmPlaybackLineageTerminalState terminal_state =
        BgmPlaybackLineageTerminalState::Active;
    BgmPlaybackAggregateMarker marker = BgmPlaybackAggregateMarker::Invalid;
    uint64_t old_request = 0;
    uint64_t canonical_request = 0;
    uint64_t latest_custom_request = 0;
    bool old_absent = false;
    bool canonical_absent = false;
    bool latest_custom_absent = false;
    bool transition_set_pending = false;
    bool stop_boundary_active = false;
    bool stop_boundary_consumed = false;
    uint64_t stop_boundary_nonce = 0;
    uint64_t stop_boundary_route_generation = 0;
    bool latest_custom_consumed = false;
    uint64_t child_ordinal = 0;
    uint64_t operation_predecessor_nonce = 0;
    uint64_t transition_set_nonce = 0;
    uint64_t transition_play_nonce = 0;
    uint64_t canonical_set_input_route_generation = 0;
    uint64_t canonical_route_generation = 0;
    uint64_t latest_custom_route_generation = 0;
};

struct BgmPlaybackAggregateTerminalSemanticDelta final {
    BgmPlaybackAggregateTerminalSemanticDeltaClassification classification =
        BgmPlaybackAggregateTerminalSemanticDeltaClassification::NotCompared;
    uint16_t semantic_mask = 0;
    uint8_t changed_node_count = 0;
    uint8_t version_changed_node_count = 0;
    uint8_t marker_changed_node_count = 0;
    uint8_t boundary_invalidated_node_count = 0;
    bool node_set_exact = false;
    bool collection_changed = false;
    uint64_t before_collection_version = 0;
    uint64_t after_collection_version = 0;
    std::array<uint16_t, 32> node_masks{};
};

constexpr uint16_t bgm_playback_aggregate_terminal_semantic_node_delta(
    const BgmPlaybackAggregateTerminalSemanticNode& before,
    const BgmPlaybackAggregateTerminalSemanticNode& after) noexcept
{
    uint16_t mask = 0;
    if (before.active != after.active) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaActive;
    }
    if (before.failed != after.failed) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaFailure;
    }
    if (before.terminal_state != after.terminal_state) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaTerminal;
    }
    if (before.marker != after.marker) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaMarker;
    }
    if (before.old_request != after.old_request
        || before.canonical_request != after.canonical_request
        || before.latest_custom_request != after.latest_custom_request) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaRequest;
    }
    if (before.old_absent != after.old_absent
        || before.canonical_absent != after.canonical_absent
        || before.latest_custom_absent != after.latest_custom_absent) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaAbsence;
    }
    if (before.transition_set_pending != after.transition_set_pending) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaPendingSet;
    }
    if (before.stop_boundary_active != after.stop_boundary_active
        || before.stop_boundary_consumed != after.stop_boundary_consumed) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaBoundary;
    }
    if (before.latest_custom_consumed != after.latest_custom_consumed
        || before.child_ordinal != after.child_ordinal) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaChild;
    }
    if (before.operation_predecessor_nonce != after.operation_predecessor_nonce
        || before.transition_set_nonce != after.transition_set_nonce
        || before.transition_play_nonce != after.transition_play_nonce
        || before.stop_boundary_nonce != after.stop_boundary_nonce) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaNonce;
    }
    if (before.stop_boundary_route_generation
            != after.stop_boundary_route_generation
        || before.canonical_set_input_route_generation
            != after.canonical_set_input_route_generation
        || before.canonical_route_generation != after.canonical_route_generation
        || before.latest_custom_route_generation
            != after.latest_custom_route_generation) {
        mask |= BgmPlaybackAggregateTerminalSemanticDeltaRoute;
    }
    return mask;
}

constexpr BgmPlaybackAggregateTerminalSemanticDelta
classify_bgm_playback_aggregate_terminal_semantic_delta(
    const BgmPlaybackAggregateTerminalSemanticNode* before,
    const size_t before_count, const uint64_t before_collection_version,
    const BgmPlaybackAggregateTerminalSemanticNode* after,
    const size_t after_count, const uint64_t after_collection_version) noexcept
{
    using Classification =
        BgmPlaybackAggregateTerminalSemanticDeltaClassification;
    BgmPlaybackAggregateTerminalSemanticDelta result;
    result.before_collection_version = before_collection_version;
    result.after_collection_version = after_collection_version;
    result.collection_changed = before_collection_version
        != after_collection_version;
    if (!before || !after || before_count == 0 || before_count > 32
        || after_count == 0 || after_count > 32) {
        result.classification = Classification::NodeSetDrift;
        return result;
    }
    bool matched_after[32]{};
    result.node_set_exact = before_count == after_count;
    for (size_t i = 0; i < before_count; ++i) {
        size_t match_count = 0;
        size_t match = 0;
        for (size_t j = 0; j < after_count; ++j) {
            if (after[j].present && before[i].present
                && after[j].ordinal == before[i].ordinal) {
                ++match_count;
                match = j;
            }
        }
        if (!before[i].present || match_count != 1 || matched_after[match]) {
            result.node_set_exact = false;
            continue;
        }
        matched_after[match] = true;
        const uint16_t mask =
            bgm_playback_aggregate_terminal_semantic_node_delta(
                before[i], after[match]);
        result.node_masks[i] = mask;
        result.semantic_mask |= mask;
        if (mask != 0) ++result.changed_node_count;
        if (before[i].version != after[match].version) {
            ++result.version_changed_node_count;
        }
        if ((mask & BgmPlaybackAggregateTerminalSemanticDeltaMarker) != 0) {
            ++result.marker_changed_node_count;
        }
        const bool exact_boundary_invalidation =
            mask == BgmPlaybackAggregateTerminalSemanticDeltaBoundary
            && before[i].stop_boundary_active
            && !before[i].stop_boundary_consumed
            && !after[match].stop_boundary_active
            && after[match].stop_boundary_consumed
            && before[i].version != UINT64_MAX
            && after[match].version == before[i].version + 1;
        if (exact_boundary_invalidation) {
            ++result.boundary_invalidated_node_count;
        }
    }
    for (size_t j = 0; j < after_count; ++j) {
        if (after[j].present && !matched_after[j]) result.node_set_exact = false;
    }
    if (!result.node_set_exact) {
        result.classification = Classification::NodeSetDrift;
        return result;
    }
    if (result.semantic_mask == 0) {
        result.classification = result.collection_changed
                || result.version_changed_node_count != 0
            ? Classification::VersionOnly : Classification::NoDelta;
        return result;
    }
    if (result.semantic_mask
            == BgmPlaybackAggregateTerminalSemanticDeltaMarker) {
        result.classification = Classification::MarkerOnly;
        return result;
    }
    if (result.semantic_mask
            == BgmPlaybackAggregateTerminalSemanticDeltaBoundary) {
        result.classification = result.changed_node_count == 1
                && result.boundary_invalidated_node_count == 1
                && before_collection_version != UINT64_MAX
                && after_collection_version == before_collection_version + 1
            ? Classification::ExactBoundaryInvalidation
            : Classification::BoundaryMutation;
        return result;
    }
    const auto single = [mask = result.semantic_mask](uint16_t expected) {
        return mask == expected;
    };
    if (single(BgmPlaybackAggregateTerminalSemanticDeltaActive)) {
        result.classification = Classification::ActiveMutation;
    } else if (single(BgmPlaybackAggregateTerminalSemanticDeltaFailure)) {
        result.classification = Classification::FailureMutation;
    } else if (single(BgmPlaybackAggregateTerminalSemanticDeltaTerminal)) {
        result.classification = Classification::TerminalMutation;
    } else if (single(BgmPlaybackAggregateTerminalSemanticDeltaRequest)) {
        result.classification = Classification::RequestMutation;
    } else if (single(BgmPlaybackAggregateTerminalSemanticDeltaAbsence)) {
        result.classification = Classification::AbsenceMutation;
    } else if (single(BgmPlaybackAggregateTerminalSemanticDeltaPendingSet)) {
        result.classification = Classification::PendingSetMutation;
    } else if (single(BgmPlaybackAggregateTerminalSemanticDeltaChild)) {
        result.classification = Classification::ChildMutation;
    } else if (single(BgmPlaybackAggregateTerminalSemanticDeltaNonce)) {
        result.classification = Classification::NonceMutation;
    } else if (single(BgmPlaybackAggregateTerminalSemanticDeltaRoute)) {
        result.classification = Classification::RouteMutation;
    } else {
        result.classification = Classification::MultipleSemanticMutations;
    }
    return result;
}

constexpr BgmPlaybackAggregateTerminalLineageResult
analyze_bgm_playback_aggregate_terminal_lineage(
    const BgmPlaybackAggregateTerminalLineageNode* nodes,
    const size_t count, const uint64_t monitor_request) noexcept
{
    BgmPlaybackAggregateTerminalLineageResult result;
    if (!nodes || count == 0 || count > 32 || monitor_request == 0) return result;
    size_t root_count = 0;
    size_t root = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!nodes[i].present) continue;
        result.aggregate_failure = result.aggregate_failure || nodes[i].failed
            || nodes[i].terminal_state == BgmPlaybackLineageTerminalState::Failed;
        if (!nodes[i].domain_exact) continue;
        if (nodes[i].old_request == monitor_request) {
            ++root_count;
            root = i;
        }
    }
    result.root_found = root_count != 0;
    if (root_count != 1) {
        result.branch = root_count > 1;
        return result;
    }
    result.root_index = static_cast<uint8_t>(root);
    {
        bool topology_visited[32]{};
        size_t topology_current = root;
        while (true) {
            if (topology_visited[topology_current]) {
                result.cycle = true;
                return result;
            }
            topology_visited[topology_current] = true;
            size_t child_count = 0;
            size_t child = 0;
            for (size_t i = 0; i < count; ++i) {
                if (!nodes[i].present || !nodes[i].domain_exact
                    || i == topology_current) continue;
                if (nodes[i].old_request
                    == nodes[topology_current].latest_custom_request) {
                    ++child_count;
                    child = i;
                }
            }
            if (child_count > 1) {
                result.branch = true;
                return result;
            }
            if (child_count == 0) break;
            topology_current = child;
        }
    }
    bool visited[32]{};
    size_t current = root;
    while (result.path_count < count) {
        if (visited[current]) {
            result.cycle = true;
            return result;
        }
        visited[current] = true;
        ++result.path_count;
        const auto& node = nodes[current];
        const AudioBgmRequestHandle old_request{node.old_request};
        const AudioBgmRequestHandle canonical_request{node.canonical_request};
        const AudioBgmRequestHandle latest_request{node.latest_custom_request};
        if (node.ordinal == 0 || node.version == 0 || node.old_request == 0
            || node.canonical_request == 0 || node.latest_custom_request == 0
            || !old_request.valid_bgm_request()
            || !canonical_request.valid_bgm_request()
            || !latest_request.valid_bgm_request()
            || old_request.generation() != node.old_request_generation
            || canonical_request.generation() != node.canonical_request_generation
            || latest_request.generation() != node.latest_custom_request_generation
            || node.old_request_generation == 0
            || node.old_request_generation > UINT32_MAX - 2
            || node.canonical_request_generation
                != node.old_request_generation + 1
            || node.latest_custom_request_generation
                != node.canonical_request_generation + 1
            || !node.old_absent || !node.canonical_absent
            || !node.latest_custom_absent
            || node.anchor_route_generation == 0
            || node.canonical_set_input_route_generation == 0
            || node.canonical_set_input_route_generation > UINT64_MAX - 2
            || node.canonical_route_generation
                != node.canonical_set_input_route_generation + 1
            || node.latest_custom_route_generation
                != node.canonical_route_generation + 1
            || node.operation_predecessor_nonce == 0
            || node.canonical_set_nonce == 0
            || node.canonical_set_nonce > UINT64_MAX - 4
            || node.canonical_play_nonce != node.canonical_set_nonce + 1
            || node.stop_boundary_nonce != node.canonical_play_nonce + 1
            || node.custom_set_nonce != node.stop_boundary_nonce + 1
            || node.custom_play_nonce != node.custom_set_nonce + 1
            || node.operation_predecessor_nonce != node.custom_play_nonce) {
            return result;
        }
        if (current == root
            && (node.anchor_route_generation == UINT64_MAX
                || node.canonical_set_input_route_generation
                    != node.anchor_route_generation + 1)) {
            return result;
        }
        size_t child_count = 0;
        size_t child = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!nodes[i].present || !nodes[i].domain_exact || i == current) continue;
            if (nodes[i].old_request == node.latest_custom_request) {
                ++child_count;
                child = i;
            }
        }
        if (child_count > 1) {
            result.branch = true;
            return result;
        }
        if (child_count == 0) {
            result.leaf_index = static_cast<uint8_t>(current);
            if (!node.active || node.failed || !node.aggregate_closed
                || !node.borrowers_zero
                || node.terminal_state != BgmPlaybackLineageTerminalState::Active
                || node.latest_custom_consumed
                || node.child_ordinal != 0) {
                return result;
            }
            break;
        }
        if (visited[child]) {
            result.cycle = true;
            return result;
        }
        const auto& next = nodes[child];
        if (!node.latest_custom_consumed || node.child_ordinal != next.ordinal
            || node.ordinal == UINT64_MAX || next.ordinal != node.ordinal + 1
            || next.old_request != node.latest_custom_request
            || next.old_request_generation != node.latest_custom_request_generation
            || next.anchor_route_generation != node.anchor_route_generation
            || next.lineage_route_predecessor_generation
                != node.canonical_route_generation
            || next.canonical_set_input_route_generation
                != node.latest_custom_route_generation
             || !node.stop_boundary_consumed || node.stop_boundary_nonce == 0
            || node.custom_play_nonce == UINT64_MAX
            || next.canonical_set_nonce != node.custom_play_nonce + 1
            || node.terminal_state
                != BgmPlaybackLineageTerminalState::SupersededClosed
            || node.failed) {
            return result;
        }
        current = child;
    }
    if (result.leaf_index == 0xff || result.path_count == 0) return result;
    size_t present_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (nodes[i].present) ++present_count;
    }
    result.exact = result.path_count == present_count && !result.aggregate_failure;
    return result;
}

struct BgmPlaybackAggregateTerminalHandoffFacts final {
    bool candidate = false;
    bool quiescent_monitor = false;
    uint8_t monitor_origin = 0;
    uint64_t monitor_epoch = 0;
    uint64_t monitor_route_generation = 0;
    uint64_t monitor_request = 0;
    uint64_t live_route_generation = 0;
    uint64_t collection_version = 0;
    uint8_t record_count = 0;
    std::array<BgmPlaybackAggregateTerminalLineageNode, 32> records{};
    bool version_recheck_exact = false;
    bool marker_only_successor = false;
    bool backing_exact = false;
    uint8_t backing_provenance = 0;
    bool lifecycle_present = false;
    bool lifecycle_restore_applied = false;
    bool lifecycle_state_epoch_exact = false;
    bool lifecycle_ordinal_exact = false;
    bool lifecycle_route_exact = false;
    bool lifecycle_cleanup_exact = false;
    bool lifecycle_request_exact = false;
    bool lifecycle_sound_exact = false;
    bool lifecycle_canonical_exact = false;
    bool lifecycle_custom_exact = false;
    bool lifecycle_owner_restored = false;
    bool lifecycle_release_attempted = false;
    bool lifecycle_release_in_flight_absent = false;
    bool lifecycle_exact = false;
    BgmPlaybackAggregateTerminalOwnerCategory owner_category =
        BgmPlaybackAggregateTerminalOwnerCategory::Unreadable;
    bool owner_exact = false;
    bool frozen_generation_exact = false;
    bool frozen_lease_exact = false;
    bool frozen_sound_exact = false;
    bool frozen_non_owner_fields_restored = false;
    bool frozen_owner_observed = false;
    bool frozen_exact = false;
    bool journals_restored = false;
    uint8_t pause_phase = 0;
    uint64_t pause_session_epoch = 0;
    uint64_t pause_cycle_epoch = 0;
    bool pause_source_available = false;
    bool pause_request_exact = false;
    bool pause_sound_exact = false;
    bool pause_lease_exact = false;
    bool pause_exit_requested = false;
    bool pause_release_pending = false;
    bool pause_retirement_active = false;
    bool pause_same_lineage = false;
    bool pause_retirement_candidate = false;
    bool pause_active = false;
    bool pause_conflict = false;
    bool registry_playback_absent = false;
    bool registry_playback_present = false;
    bool registry_playback_selection_exact = false;
    bool registry_playback_controller_exact = false;
    bool registry_playback_slot_exact = false;
    bool registry_playback_bgm_exact = false;
    bool registry_playback_sound_exact = false;
    bool registry_playback_request_exact = false;
    bool registry_playback_route_exact = false;
    bool registry_playback_lease_exact = false;
    bool registry_playback_active_exact = false;
    bool registry_cleanup_present = false;
    bool registry_cleanup_selection_exact = false;
    bool registry_cleanup_valid = false;
    bool registry_cleanup_controller_exact = false;
    bool registry_cleanup_slot_exact = false;
    bool registry_cleanup_bgm_exact = false;
    bool registry_cleanup_sound_exact = false;
    bool registry_cleanup_request_exact = false;
    bool registry_cleanup_route_exact = false;
    bool registry_cleanup_lease_exact = false;
    bool registry_exact = false;
    bool native_route_empty = false;
    bool setup_absent = false;
    bool handoff_absent = false;
    bool release_absent = false;
    bool natural_stop_recheck = false;
    bool route_unchanged = false;
    bool null_chain_exact = false;
    bool post_stop_recheck = false;
    uint8_t post_diagnostic_count = 0;
    bool post_observation_ran = false;
    bool post_observation_succeeded = false;
    bool native_stop_called_once = false;
    bool post_null_chain_exact = false;
    uint8_t aggregate_closed_semantic_count = 0;
    std::array<BgmPlaybackAggregateTerminalSemanticNode, 32>
        aggregate_closed_semantics{};
    uint8_t pre_stop_semantic_count = 0;
    std::array<BgmPlaybackAggregateTerminalSemanticNode, 32>
        pre_stop_semantics{};
    uint8_t post_stop_semantic_count = 0;
    std::array<BgmPlaybackAggregateTerminalSemanticNode, 32>
        post_stop_semantics{};
    BgmPlaybackAggregateTerminalSemanticDelta aggregate_closed_to_pre_stop{};
    BgmPlaybackAggregateTerminalSemanticDelta pre_to_post_stop{};
    uint64_t post_live_route_generation = 0;
    bool post_monitor_exact = false;
    bool post_backing_exact = false;
    BgmPlaybackAggregateTerminalOwnerCategory post_owner_category =
        BgmPlaybackAggregateTerminalOwnerCategory::Unreadable;
    bool post_owner_exact = false;
    bool post_frozen_exact = false;
    bool post_lifecycle_exact = false;
    bool post_pause_exact = false;
    bool post_registry_exact = false;
    bool post_setup_absent = false;
    bool post_handoff_absent = false;
    bool post_release_absent = false;
    bool cleanup_committed = false;
};

constexpr void reset_bgm_playback_aggregate_terminal_stop_snapshot(
    BgmPlaybackAggregateTerminalHandoffFacts& facts) noexcept
{
    facts.record_count = 0;
    facts.records = {};
    facts.pre_stop_semantic_count = 0;
    facts.pre_stop_semantics = {};
}

constexpr bool bgm_playback_aggregate_terminal_backing_exact(
    const AudioStopRetirementOrigin origin,
    const OnMemoryBankRetiredBackingProvenance provenance,
    const bool common_identity_exact,
    const bool detached_current_exact) noexcept
{
    if (!common_identity_exact) return false;
    if (origin == AudioStopRetirementOrigin::OrdinaryStop) {
        return provenance
            == OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence;
    }
    if (origin == AudioStopRetirementOrigin::ExactNaturalCompletion) {
        return detached_current_exact
            && (provenance
                    == OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached
                || provenance
                    == OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector);
    }
    return false;
}

constexpr bool bgm_playback_aggregate_terminal_lifecycle_exact(
    const bool present,
    const bool restore_applied,
    const bool state_epoch_exact,
    const bool ordinal_exact,
    const bool route_exact,
    const bool cleanup_exact,
    const bool request_exact,
    const bool sound_exact,
    const bool canonical_exact,
    const bool custom_exact,
    const bool owner_restored,
    const bool release_attempted,
    const bool release_in_flight_absent,
    const bool monitor_identity_exact) noexcept
{
    return present && restore_applied && state_epoch_exact && ordinal_exact
        && route_exact && cleanup_exact && request_exact && sound_exact
        && canonical_exact && custom_exact && owner_restored
        && !release_attempted && release_in_flight_absent
        && monitor_identity_exact;
}

constexpr bool bgm_playback_aggregate_terminal_pause_conflict(
    const bool pause_active,
    const bool same_lineage,
    const bool retirement_candidate = false) noexcept
{
    return pause_active && same_lineage && !retirement_candidate;
}

constexpr bool bgm_playback_aggregate_terminal_registry_exact(
    const bool playback_absent,
    const bool playback_active_exact,
    const bool cleanup_valid,
    const bool cleanup_controller_exact,
    const bool cleanup_slot_exact,
    const bool cleanup_bgm_exact,
    const bool cleanup_sound_exact,
    const bool cleanup_request_exact,
    const bool cleanup_route_exact,
    const bool cleanup_lease_exact) noexcept
{
    return (playback_absent || playback_active_exact)
        && cleanup_valid && cleanup_controller_exact && cleanup_slot_exact
        && cleanup_bgm_exact && cleanup_sound_exact && cleanup_request_exact
        && cleanup_route_exact && cleanup_lease_exact;
}

constexpr bool bgm_playback_aggregate_terminal_lineage_cacheable(
    const BgmPlaybackAggregateTerminalHandoffFacts& f) noexcept
{
    if (!f.candidate || !f.quiescent_monitor || f.monitor_origin == 0
        || f.monitor_epoch == 0 || f.monitor_route_generation == 0
        || f.monitor_request == 0 || f.collection_version == 0
        || !f.version_recheck_exact) {
        return false;
    }
    const auto lineage = analyze_bgm_playback_aggregate_terminal_lineage(
        f.records.data(), f.record_count, f.monitor_request);
    if (lineage.aggregate_failure || !lineage.root_found || lineage.branch
        || lineage.cycle || !lineage.exact
        || lineage.root_index >= f.record_count
        || lineage.leaf_index >= f.record_count) {
        return false;
    }
    const auto& root = f.records[lineage.root_index];
    const auto& leaf = f.records[lineage.leaf_index];
    return root.anchor_route_generation != UINT64_MAX
        && root.anchor_route_generation + 1 == f.monitor_route_generation
        && leaf.latest_custom_route_generation == f.live_route_generation
        && leaf.active && leaf.aggregate_closed && leaf.borrowers_zero
        && leaf.terminal_state == BgmPlaybackLineageTerminalState::Active;
}

constexpr bool bgm_playback_aggregate_terminal_cache_claimable(
    const bool valid, const bool in_flight) noexcept
{
    return valid && !in_flight;
}

enum class BgmPlaybackAggregateTerminalAuthorityState : uint8_t {
    None,
    Armed,
    InFlight,
    Failed,
};

struct BgmPlaybackAggregateTerminalAuthority final {
    BgmPlaybackAggregateTerminalAuthorityState state =
        BgmPlaybackAggregateTerminalAuthorityState::None;
    uint64_t generation = 0;
    BgmPlaybackAggregateTerminalHandoffFacts facts{};
};

constexpr bool bgm_playback_aggregate_terminal_semantics_unchanged(
    const BgmPlaybackAggregateTerminalSemanticDelta& delta) noexcept
{
    using Classification =
        BgmPlaybackAggregateTerminalSemanticDeltaClassification;
    return delta.node_set_exact && delta.semantic_mask == 0
        && (delta.classification == Classification::NoDelta
            || delta.classification == Classification::VersionOnly);
}

constexpr bool claim_bgm_playback_aggregate_terminal_authority(
    BgmPlaybackAggregateTerminalAuthority& authority,
    const uint64_t generation) noexcept
{
    if (authority.state != BgmPlaybackAggregateTerminalAuthorityState::Armed
        || generation == 0 || authority.generation != generation) {
        return false;
    }
    authority.state = BgmPlaybackAggregateTerminalAuthorityState::InFlight;
    return true;
}

constexpr void fail_bgm_playback_aggregate_terminal_authority(
    BgmPlaybackAggregateTerminalAuthority& authority) noexcept
{
    if (authority.state != BgmPlaybackAggregateTerminalAuthorityState::None) {
        authority.state = BgmPlaybackAggregateTerminalAuthorityState::Failed;
    }
}

constexpr void clear_bgm_playback_aggregate_terminal_authority(
    BgmPlaybackAggregateTerminalAuthority& authority) noexcept
{
    authority = {};
}

static_assert(std::is_trivially_copyable_v<
    BgmPlaybackAggregateTerminalAuthority>);

constexpr BgmPlaybackAggregateTerminalHandoffClassification
classify_bgm_playback_aggregate_terminal_handoff(
    const BgmPlaybackAggregateTerminalHandoffFacts& f) noexcept
{
    using Result = BgmPlaybackAggregateTerminalHandoffClassification;
    if (!f.candidate) return Result::NotCandidate;
    if (!f.quiescent_monitor
        || f.monitor_origin
            != static_cast<uint8_t>(AudioStopRetirementOrigin::OrdinaryStop)
        || f.monitor_epoch == 0
        || f.monitor_route_generation == 0 || f.monitor_request == 0) {
        return Result::NoQuiescentMonitor;
    }
    const auto lineage = analyze_bgm_playback_aggregate_terminal_lineage(
        f.records.data(), f.record_count, f.monitor_request);
    if (lineage.aggregate_failure) return Result::AggregateFailure;
    if (f.collection_version == 0
        || (!f.version_recheck_exact && !f.marker_only_successor)) {
        return Result::VersionDrift;
    }
    if (!lineage.root_found) return Result::RootNotFound;
    if (lineage.branch) return Result::LineageBranch;
    if (lineage.cycle) return Result::LineageCycle;
    if (!lineage.exact) return Result::LineageMismatch;
    const auto& root = f.records[lineage.root_index];
    const auto& leaf = f.records[lineage.leaf_index];
    if (root.anchor_route_generation == UINT64_MAX
        || root.anchor_route_generation + 1 != f.monitor_route_generation
        || leaf.latest_custom_route_generation != f.live_route_generation) {
        return Result::LineageMismatch;
    }
    if (!f.backing_exact) return Result::BackingMismatch;
    if (!f.lifecycle_exact) return Result::LifecycleMismatch;
    if (!f.owner_exact) return Result::OwnerMismatch;
    if (!f.frozen_exact || !f.journals_restored
        || !f.setup_absent || !f.handoff_absent || !f.release_absent) {
        return Result::FrozenOrJournalMismatch;
    }
    if (f.pause_conflict) return Result::PauseConflict;
    if (!f.registry_exact) return Result::RegistryMismatch;
    if (!f.native_route_empty) return Result::NativeRouteNotEmpty;
    if (!f.natural_stop_recheck) return Result::ExactAtAggregateClosed;
    if (!f.route_unchanged || !f.null_chain_exact) {
        return Result::LineageMismatch;
    }
    if (f.post_stop_recheck
        && (f.post_diagnostic_count != 1 || !f.post_observation_ran
            || !f.post_observation_succeeded || !f.native_stop_called_once
            || !f.post_null_chain_exact)) {
        return Result::LineageMismatch;
    }
    return Result::ExactAtNaturalStop;
}

constexpr bool arm_bgm_playback_aggregate_terminal_authority(
    BgmPlaybackAggregateTerminalAuthority& authority,
    const uint64_t generation,
    const BgmPlaybackAggregateTerminalHandoffFacts& facts) noexcept
{
    if (authority.state != BgmPlaybackAggregateTerminalAuthorityState::None
        || generation == 0
        || classify_bgm_playback_aggregate_terminal_handoff(facts)
            != BgmPlaybackAggregateTerminalHandoffClassification::ExactAtAggregateClosed) {
        return false;
    }
    authority.state = BgmPlaybackAggregateTerminalAuthorityState::Armed;
    authority.generation = generation;
    authority.facts = facts;
    return true;
}

constexpr bool bgm_playback_aggregate_terminal_cleanup_exact(
    const BgmPlaybackAggregateTerminalHandoffFacts& facts,
    const BgmPlaybackAggregateTerminalHandoffClassification
        pre_stop_classification) noexcept
{
    return bgm_playback_aggregate_terminal_semantics_unchanged(
            facts.aggregate_closed_to_pre_stop)
        && bgm_playback_aggregate_terminal_semantics_unchanged(
            facts.pre_to_post_stop)
        && pre_stop_classification
            == BgmPlaybackAggregateTerminalHandoffClassification::ExactAtNaturalStop
        && classify_bgm_playback_aggregate_terminal_handoff(facts)
            == BgmPlaybackAggregateTerminalHandoffClassification::ExactAtNaturalStop
        && facts.post_live_route_generation == facts.live_route_generation
        && facts.post_monitor_exact && facts.post_backing_exact
        && facts.post_owner_exact && facts.post_frozen_exact
        && facts.post_lifecycle_exact && facts.post_pause_exact
        && facts.post_registry_exact && facts.post_setup_absent
        && facts.post_handoff_absent && facts.post_release_absent;
}

// Terminal callback retirement is a transaction-local consequence of already
// authenticated natural-Stop authority. It does not release borrower/token
// ownership; it only prevents later native Set/Play refreshes from joining the
// retired aggregate while that ownership drains normally.
constexpr bool bgm_playback_aggregate_terminal_callback_retirement_authorized(
    const bool terminal_cleanup_exact,
    const bool terminal_authority_current,
    const bool cleanup_already_committed) noexcept
{
    return terminal_cleanup_exact && terminal_authority_current
        && !cleanup_already_committed;
}

struct BgmPlaybackAggregateCallbackRetirementRecord final {
    uint64_t ordinal = 0;
    uint64_t expected_version = 0;
    uint64_t current_version = 0;
    bool active = false;
    bool failed = false;
    bool retired = false;
};

struct BgmPlaybackAggregateCallbackRetirementTransaction final {
    bool authorized = false;
    bool lineage_exact = false;
    uint64_t root_ordinal = 0;
    uint64_t leaf_ordinal = 0;
    uint64_t expected_collection_version = 0;
    uint64_t current_collection_version = 0;
    uint8_t record_count = 0;
    std::array<BgmPlaybackAggregateCallbackRetirementRecord, 32> records{};
};

struct BgmPlaybackAggregateCallbackRetirementResult final {
    bool exact = false;
    uint64_t next_collection_version = 0;
    uint8_t record_count = 0;
    std::array<uint64_t, 32> ordinals{};
    std::array<uint64_t, 32> next_versions{};
};

constexpr BgmPlaybackAggregateCallbackRetirementResult
plan_bgm_playback_aggregate_callback_retirement(
    const BgmPlaybackAggregateCallbackRetirementTransaction& tx) noexcept
{
    BgmPlaybackAggregateCallbackRetirementResult result;
    if (!tx.authorized || !tx.lineage_exact || tx.record_count == 0
        || tx.record_count > tx.records.size()
        || tx.root_ordinal == 0 || tx.leaf_ordinal == 0
        || (tx.record_count > 1 && tx.root_ordinal == tx.leaf_ordinal)
        || tx.expected_collection_version != tx.current_collection_version
        || tx.current_collection_version == UINT64_MAX) {
        return result;
    }
    bool root_found = false;
    bool leaf_found = false;
    for (size_t i = 0; i < tx.record_count; ++i) {
        const auto& record = tx.records[i];
        if (record.ordinal == 0
            || record.expected_version != record.current_version
            || record.current_version == UINT64_MAX || !record.active
            || record.failed || record.retired) {
            return {};
        }
        for (size_t prior = 0; prior < i; ++prior) {
            if (tx.records[prior].ordinal == record.ordinal) return {};
        }
        root_found = root_found || record.ordinal == tx.root_ordinal;
        leaf_found = leaf_found || record.ordinal == tx.leaf_ordinal;
        result.ordinals[i] = record.ordinal;
        result.next_versions[i] = record.current_version + 1;
    }
    if (!root_found || !leaf_found) return {};
    result.exact = true;
    result.next_collection_version = tx.current_collection_version + 1;
    result.record_count = tx.record_count;
    return result;
}

constexpr BgmPlaybackAggregateCallbackRetirementResult
plan_bgm_playback_aggregate_callback_retirement_rollback(
    const BgmPlaybackAggregateCallbackRetirementTransaction& tx) noexcept
{
    BgmPlaybackAggregateCallbackRetirementResult result;
    if (!tx.authorized || !tx.lineage_exact || tx.record_count == 0
        || tx.record_count > tx.records.size()
        || tx.expected_collection_version != tx.current_collection_version
        || tx.current_collection_version == UINT64_MAX) {
        return result;
    }
    for (size_t i = 0; i < tx.record_count; ++i) {
        const auto& record = tx.records[i];
        if (record.ordinal == 0
            || record.expected_version != record.current_version
            || record.current_version == UINT64_MAX || !record.active
            || record.failed || !record.retired) {
            return {};
        }
        for (size_t prior = 0; prior < i; ++prior) {
            if (tx.records[prior].ordinal == record.ordinal) return {};
        }
        result.ordinals[i] = record.ordinal;
        result.next_versions[i] = record.current_version + 1;
    }
    result.exact = true;
    result.next_collection_version = tx.current_collection_version + 1;
    result.record_count = tx.record_count;
    return result;
}

constexpr bool bgm_playback_aggregate_callback_match_eligible(
    const bool active,
    const bool failed,
    const bool terminal_callback_retired) noexcept
{
    return active && !failed && !terminal_callback_retired;
}

static_assert(std::is_trivially_copyable_v<
    BgmPlaybackAggregateTerminalLineageNode>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackAggregateTerminalSemanticNode>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackAggregateTerminalSemanticDelta>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackAggregateTerminalHandoffFacts>);

struct BgmPlaybackLineageTerminalFacts final {
    bool failed = false;
    bool child_claimed_exactly_once = false;
    bool old_absent = false;
    bool canonical_absent = false;
    bool custom_absent = false;
    bool current_active = false;
    bool pending_set = false;
    bool pending_boundary = false;
    bool unresolved_request = false;
};

constexpr BgmPlaybackLineageTerminalState
classify_bgm_playback_lineage_terminal(
    const BgmPlaybackLineageTerminalFacts& f) noexcept
{
    if (f.failed) return BgmPlaybackLineageTerminalState::Failed;
    if (!f.child_claimed_exactly_once) {
        return BgmPlaybackLineageTerminalState::Active;
    }
    if (f.old_absent && f.canonical_absent && f.custom_absent
        && !f.current_active && !f.pending_set && !f.pending_boundary
        && !f.unresolved_request) {
        return BgmPlaybackLineageTerminalState::SupersededClosed;
    }
    return BgmPlaybackLineageTerminalState::SupersededPending;
}

constexpr bool bgm_playback_expected_custom_successor(
    uint64_t observed_handle, uint64_t canonical_handle,
    uint64_t confirmed_custom_handle, bool confirmed_custom_identity) noexcept
{
    return observed_handle != 0 && canonical_handle != 0
        && observed_handle != canonical_handle
        && observed_handle == confirmed_custom_handle
        && confirmed_custom_identity;
}

constexpr bool bgm_playback_lineage_current_reconciliation_allowed(
    bool confirmed_custom_transition_consumed,
    uint64_t confirmed_custom_transition_child_ordinal) noexcept
{
    return !(confirmed_custom_transition_consumed
        && confirmed_custom_transition_child_ordinal != 0);
}

constexpr bool bgm_playback_confirmed_custom_route_transition_exact(
    uint64_t retirement_anchor_route_generation,
    uint64_t canonical_set_input_route_generation,
    uint64_t canonical_route_generation,
    uint64_t canonical_boundary_route_generation,
    uint64_t custom_route_generation) noexcept
{
    return bgm_playback_exact_successor(
               retirement_anchor_route_generation,
               canonical_set_input_route_generation)
        && bgm_playback_exact_successor(
            canonical_set_input_route_generation, canonical_route_generation)
        && canonical_route_generation != 0
        && canonical_boundary_route_generation == canonical_route_generation
        && bgm_playback_exact_successor(
            canonical_boundary_route_generation, custom_route_generation);
}

constexpr uint64_t bgm_playback_descendant_route_predecessor(
    uint64_t parent_canonical_boundary_route_generation,
    uint64_t parent_confirmed_custom_route_generation,
    uint64_t child_transfer_route_generation) noexcept
{
    return bgm_playback_exact_successor(
               parent_canonical_boundary_route_generation,
               parent_confirmed_custom_route_generation)
            && child_transfer_route_generation
                == parent_confirmed_custom_route_generation
        ? parent_canonical_boundary_route_generation
        : 0;
}

constexpr bool bgm_playback_confirmed_custom_lease_transition_exact(
    const AudioRouteLeaseIdentity& transfer_lease,
    const AudioRouteLeaseIdentity& canonical_lease,
    const AudioRouteLeaseIdentity& custom_lease) noexcept
{
    return transfer_lease.generation != 0 && transfer_lease.song_key != 0
        && canonical_lease.generation == transfer_lease.generation
        && canonical_lease.song_key == transfer_lease.song_key
        && custom_lease.generation == canonical_lease.generation
        && custom_lease.song_key == canonical_lease.song_key;
}

enum class BgmPlaybackConfirmedCustomRefreshFailure : uint8_t {
    None,
    PlayUnconfirmed,
    PendingOrUnresolvedProvenance,
    HandleMismatch,
    SoundNotLive,
    SoundPointerMismatch,
    SoundIdentityNotPositive,
    SoundIdentityIndexMismatch,
    SoundIdentitySerialMismatch,
    StateNotPlaying,
    ControllerSlotBgmMismatch,
    PlayOwnerProofMissing,
    OwnerBindingMismatch,
    EpochContinuityMismatch,
    RouteLeaseMismatch,
    LifecycleTokenMismatch,
};

constexpr bool bgm_playback_owner_command_phase_exact(
    bool owner_and_thread_exact, bool owner_state_read, uint8_t owner_state,
    bool active_key_read, uint64_t expected_command,
    uint64_t active_key) noexcept
{
    return owner_and_thread_exact && owner_state_read && active_key_read
        && owner_state == 0 && expected_command != 0
        && active_key == expected_command;
}

constexpr bool bgm_playback_confirmed_custom_owner_command_exact(
    bool owner_and_thread_exact, bool owner_state_read, uint8_t owner_state,
    bool active_key_read, uint64_t canonical_command,
    uint64_t anchored_custom_command, uint64_t active_key) noexcept
{
    return canonical_command != 0 && anchored_custom_command != 0
        && anchored_custom_command != canonical_command
        && bgm_playback_owner_command_phase_exact(owner_and_thread_exact,
            owner_state_read, owner_state, active_key_read,
            anchored_custom_command, active_key);
}

inline constexpr uint8_t kBgmPlaybackCustomPlayOwnerState = 8;

constexpr bool bgm_playback_custom_play_post_native_owner_phase_exact(
    bool owner_read, bool owner_state_read, uint8_t owner_state,
    uint64_t expected_command, uint64_t captured_command) noexcept
{
    return owner_read && owner_state_read
        && owner_state == kBgmPlaybackCustomPlayOwnerState
        && expected_command != 0 && captured_command == expected_command;
}

inline bool bgm_playback_confirmed_custom_play_owner_proof_exact(
    bool proof_confirmed, void* record_owner, uint32_t record_owner_thread,
    uint64_t canonical_command, uint64_t anchored_custom_command,
    void* play_owner, uint32_t play_owner_thread, uint8_t play_owner_state,
    uint64_t play_command) noexcept
{
    return proof_confirmed && record_owner && play_owner == record_owner
        && record_owner_thread != 0 && play_owner_thread == record_owner_thread
        && play_owner_state == kBgmPlaybackCustomPlayOwnerState
        && canonical_command != 0
        && anchored_custom_command != 0
        && anchored_custom_command != canonical_command
        && play_command == anchored_custom_command;
}

inline bool bgm_playback_confirmed_custom_owner_binding_exact(
    bool current_owner_resolved, void* record_owner,
    uint32_t record_owner_thread, void* play_owner,
    uint32_t play_owner_thread, void* current_owner,
    uint32_t current_thread) noexcept
{
    return current_owner_resolved && record_owner
        && play_owner == record_owner && current_owner == record_owner
        && record_owner_thread != 0 && play_owner_thread == record_owner_thread
        && current_thread == record_owner_thread;
}

struct BgmPlaybackConfirmedCustomRefreshFacts final {
    bool prior_play_confirmed = false;
    bool provenance_resolved = false;
    bool handle_exact = false;
    bool sound_live = false;
    bool sound_pointer_exact = false;
    bool sound_identity_positive = false;
    bool sound_identity_index_exact = false;
    bool sound_identity_serial_exact = false;
    bool state4 = false;
    bool controller_slot_bgm_exact = false;
    bool play_owner_proof_exact = false;
    bool owner_binding_exact = false;
    bool epochs_and_continuity_exact = false;
    bool route_and_lease_exact = false;
    bool lifecycle_and_tokens_exact = false;
};

constexpr BgmPlaybackConfirmedCustomRefreshFailure
bgm_playback_confirmed_custom_refresh_first_failure(
    const BgmPlaybackConfirmedCustomRefreshFacts& f) noexcept
{
    if (!f.prior_play_confirmed) {
        return BgmPlaybackConfirmedCustomRefreshFailure::PlayUnconfirmed;
    }
    if (!f.provenance_resolved) {
        return BgmPlaybackConfirmedCustomRefreshFailure::PendingOrUnresolvedProvenance;
    }
    if (!f.handle_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::HandleMismatch;
    }
    if (!f.sound_live) {
        return BgmPlaybackConfirmedCustomRefreshFailure::SoundNotLive;
    }
    if (!f.sound_pointer_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::SoundPointerMismatch;
    }
    if (!f.sound_identity_positive) {
        return BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentityNotPositive;
    }
    if (!f.sound_identity_index_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentityIndexMismatch;
    }
    if (!f.sound_identity_serial_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentitySerialMismatch;
    }
    if (!f.state4) {
        return BgmPlaybackConfirmedCustomRefreshFailure::StateNotPlaying;
    }
    if (!f.controller_slot_bgm_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::ControllerSlotBgmMismatch;
    }
    if (!f.play_owner_proof_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::PlayOwnerProofMissing;
    }
    if (!f.owner_binding_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::OwnerBindingMismatch;
    }
    if (!f.epochs_and_continuity_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::EpochContinuityMismatch;
    }
    if (!f.route_and_lease_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::RouteLeaseMismatch;
    }
    if (!f.lifecycle_and_tokens_exact) {
        return BgmPlaybackConfirmedCustomRefreshFailure::LifecycleTokenMismatch;
    }
    return BgmPlaybackConfirmedCustomRefreshFailure::None;
}

constexpr bool bgm_playback_confirmed_custom_refresh_exact(
    const BgmPlaybackConfirmedCustomRefreshFacts& f) noexcept
{
    return bgm_playback_confirmed_custom_refresh_first_failure(f)
        == BgmPlaybackConfirmedCustomRefreshFailure::None;
}

struct BgmPlaybackConfirmedCustomRefreshDiagnostic final {
    bool proposed = false;
    BgmPlaybackConfirmedCustomRefreshFailure failure =
        BgmPlaybackConfirmedCustomRefreshFailure::None;
    uint64_t ordinal = 0;
    uint64_t canonical_handle = 0;
    uint64_t custom_handle = 0;
    uint64_t current_handle = 0;
};

template <typename Publish, typename Diagnose>
bool bgm_playback_publish_before_diagnostic(
    Publish&& publish, Diagnose&& diagnose)
{
    const bool published = publish();
    if (!published) return false;
    try {
        diagnose();
    } catch (...) {
        // Diagnostic failure must not unwind across persisted release facts.
    }
    return true;
}

struct BgmPlaybackAggregateFacts final {
    bool identity_exact = false;
    bool epochs_exact = false;
    bool handles_exact = false;
    bool destination_aba_safe = false;
    bool all_retired_absent = false;
    bool final_active_present = false;
    bool final_active_ready = false;
    bool list_exit = false;
    bool shutdown = false;
    bool release_authorized = false;
    bool release_committed = false;
    bool failed = false;
    uint32_t borrower_count = 0;
};

enum class BgmPlaybackLineageProvenance : uint8_t {
    Invalid,
    PreSession,
    SessionScoped,
};

struct BgmPlaybackLineageAdmissionFacts final {
    bool route_lease_exact = false;
    bool custom_cleanup_owned = false;
    bool lifecycle_restore_applied = false;
    bool lifecycle_tokens_exact = false;
    bool owner_thread_exact = false;
    bool command_exact = false;
    bool controller_exact = false;
    bool sound_live_exact = false;
    bool release_clear = false;
    bool failure_clear = false;
    uint64_t session_epoch = 0;
    uint64_t cycle_epoch = 0;
};

constexpr BgmPlaybackLineageProvenance classify_bgm_playback_lineage_admission(
    const BgmPlaybackLineageAdmissionFacts& f) noexcept
{
    if (!f.route_lease_exact || !f.custom_cleanup_owned
        || !f.lifecycle_restore_applied || !f.lifecycle_tokens_exact
        || !f.owner_thread_exact || !f.command_exact || !f.controller_exact
        || !f.sound_live_exact || !f.release_clear || !f.failure_clear) {
        return BgmPlaybackLineageProvenance::Invalid;
    }
    if (f.session_epoch == 0 && f.cycle_epoch == 0) {
        return BgmPlaybackLineageProvenance::PreSession;
    }
    if (f.session_epoch != 0 && f.cycle_epoch != 0) {
        return BgmPlaybackLineageProvenance::SessionScoped;
    }
    return BgmPlaybackLineageProvenance::Invalid;
}

struct BgmPlaybackSessionCorrelationFacts final {
    BgmPlaybackLineageProvenance provenance =
        BgmPlaybackLineageProvenance::Invalid;
    bool continuity_exact = false;
    uint64_t initial_session_epoch = 0;
    uint64_t initial_cycle_epoch = 0;
    uint64_t correlated_session_epoch = 0;
    uint64_t correlated_cycle_epoch = 0;
    uint64_t current_session_epoch = 0;
    uint64_t current_cycle_epoch = 0;
};

struct BgmPlaybackRetirementAnchorFacts final {
    bool retirement_candidate = false;
    bool zero_session_epochs = false;
    bool detached_restore_applied = false;
    bool detached_identity_exact = false;
    bool detached_token_epoch_exact = false;
    bool lifecycle_state_epoch_exact = false;
    bool detached_tokens_exact = false;
    bool detached_request_exact = false;
    bool controller_exact = false;
};

constexpr bool bgm_playback_retirement_anchor_exact(
    const BgmPlaybackRetirementAnchorFacts& f) noexcept
{
    return f.retirement_candidate && f.zero_session_epochs
        && f.detached_restore_applied && f.detached_identity_exact
        && f.detached_token_epoch_exact && f.lifecycle_state_epoch_exact
        && f.detached_tokens_exact && f.detached_request_exact
        && f.controller_exact;
}

struct BgmPlaybackSetCorrelationFacts final {
    bool anchored = false;
    bool controller_exact = false;
    bool slot_bgm_exact = false;
    bool lease_exact = false;
    bool lifecycle_exact = false;
    bool unresolved = false;
};

constexpr bool bgm_playback_operation_nonce_next(
    uint64_t previous, uint64_t candidate) noexcept
{
    return previous != UINT64_MAX && candidate == previous + 1;
}

constexpr bool bgm_playback_canonical_to_custom_command_edge_exact(
    uint64_t anchored_custom, uint64_t confirmed_canonical,
    uint64_t live_next_custom) noexcept
{
    return anchored_custom != 0 && confirmed_canonical != 0
        && anchored_custom != confirmed_canonical
        && live_next_custom == anchored_custom;
}

constexpr bool bgm_playback_set_commit_exact(
    bool native_set_succeeded, bool candidate_current,
    bool requested_sound_applied, uint64_t previous_nonce,
    uint64_t set_nonce) noexcept
{
    return native_set_succeeded && candidate_current
        && requested_sound_applied
        && bgm_playback_operation_nonce_next(previous_nonce, set_nonce);
}

enum class BgmPlaybackSetCommitFailure : uint8_t {
    None,
    TargetMissing,
    RecordFailed,
    RecordVersion,
    CollectionVersion,
    Owner,
    Thread,
    Command,
    PostSetSnapshot,
    BoundaryState,
    RequestGeneration,
    AppliedState,
    Nonce,
};

struct BgmPlaybackSetCommitFacts final {
    bool target_present = false;
    bool record_live = false;
    bool record_version_exact = false;
    bool collection_version_exact = false;
    bool owner_exact = false;
    bool thread_exact = false;
    bool command_exact = false;
    bool post_set_snapshot_exact = false;
    bool boundary_state_exact = false;
    bool request_generation_exact = false;
    bool applied_state_exact = false;
    bool nonce_exact = false;
};

constexpr BgmPlaybackSetCommitFailure bgm_playback_set_commit_first_failure(
    const BgmPlaybackSetCommitFacts& f) noexcept
{
    if (!f.target_present) return BgmPlaybackSetCommitFailure::TargetMissing;
    if (!f.record_live) return BgmPlaybackSetCommitFailure::RecordFailed;
    if (!f.record_version_exact) return BgmPlaybackSetCommitFailure::RecordVersion;
    if (!f.collection_version_exact) return BgmPlaybackSetCommitFailure::CollectionVersion;
    if (!f.owner_exact) return BgmPlaybackSetCommitFailure::Owner;
    if (!f.thread_exact) return BgmPlaybackSetCommitFailure::Thread;
    if (!f.command_exact) return BgmPlaybackSetCommitFailure::Command;
    if (!f.post_set_snapshot_exact) return BgmPlaybackSetCommitFailure::PostSetSnapshot;
    if (!f.boundary_state_exact) return BgmPlaybackSetCommitFailure::BoundaryState;
    if (!f.request_generation_exact) return BgmPlaybackSetCommitFailure::RequestGeneration;
    if (!f.applied_state_exact) return BgmPlaybackSetCommitFailure::AppliedState;
    if (!f.nonce_exact) return BgmPlaybackSetCommitFailure::Nonce;
    return BgmPlaybackSetCommitFailure::None;
}

enum class BgmPlaybackPlayFailure : uint8_t {
    None,
    PendingSet,
    PublicationAuthority,
    ControllerChain,
    OwnerThread,
    Command,
    Route,
    Lease,
    LifecycleState,
    TokenOrdinal,
    TokenValues,
    RequestSoundIdentity,
    RequestHandle,
    State4,
    Nonce,
};

struct BgmPlaybackPlayFacts final {
    bool pending_set = false;
    bool publication_authority_exact = false;
    bool controller_chain_exact = false;
    bool owner_thread_exact = false;
    bool command_exact = false;
    bool route_exact = false;
    bool lease_exact = false;
    bool lifecycle_state_exact = false;
    bool token_ordinal_exact = false;
    bool token_values_exact = false;
    bool request_sound_identity_exact = false;
    bool request_handle_exact = false;
    bool state4 = false;
    bool nonce_exact = false;
};

constexpr BgmPlaybackPlayFailure bgm_playback_play_first_failure(
    const BgmPlaybackPlayFacts& f) noexcept
{
    if (!f.pending_set) return BgmPlaybackPlayFailure::PendingSet;
    if (!f.publication_authority_exact) {
        return BgmPlaybackPlayFailure::PublicationAuthority;
    }
    if (!f.controller_chain_exact) return BgmPlaybackPlayFailure::ControllerChain;
    if (!f.owner_thread_exact) return BgmPlaybackPlayFailure::OwnerThread;
    if (!f.command_exact) return BgmPlaybackPlayFailure::Command;
    if (!f.route_exact) return BgmPlaybackPlayFailure::Route;
    if (!f.lease_exact) return BgmPlaybackPlayFailure::Lease;
    if (!f.lifecycle_state_exact) return BgmPlaybackPlayFailure::LifecycleState;
    if (!f.token_ordinal_exact) return BgmPlaybackPlayFailure::TokenOrdinal;
    if (!f.token_values_exact) return BgmPlaybackPlayFailure::TokenValues;
    if (!f.request_sound_identity_exact) return BgmPlaybackPlayFailure::RequestSoundIdentity;
    if (!f.request_handle_exact) return BgmPlaybackPlayFailure::RequestHandle;
    if (!f.state4) return BgmPlaybackPlayFailure::State4;
    if (!f.nonce_exact) return BgmPlaybackPlayFailure::Nonce;
    return BgmPlaybackPlayFailure::None;
}

constexpr bool bgm_playback_custom_play_intended_command_exact(
    bool set_pending, bool custom_set, uint64_t set_command,
    uint64_t canonical_command, uint64_t anchored_custom_command) noexcept
{
    return set_pending && custom_set && set_command != 0
        && canonical_command != 0 && anchored_custom_command != 0
        && anchored_custom_command != canonical_command
        && set_command == anchored_custom_command;
}

enum class BgmPlaybackSetProvenance : uint8_t {
    None,
    Canonical,
    Custom,
};

constexpr BgmPlaybackSetProvenance classify_bgm_playback_set_provenance(
    bool set_committed,
    bool retained_sound_pointer,
    bool retained_sound_identity) noexcept
{
    if (!set_committed) return BgmPlaybackSetProvenance::None;
    return retained_sound_pointer && retained_sound_identity
        ? BgmPlaybackSetProvenance::Custom
        : BgmPlaybackSetProvenance::Canonical;
}

constexpr bool bgm_playback_play_join_exact(
    bool native_play_succeeded, bool set_committed, bool correlation_exact,
    uint64_t set_nonce, uint64_t play_nonce) noexcept
{
    return native_play_succeeded && set_committed && correlation_exact
        && bgm_playback_operation_nonce_next(set_nonce, play_nonce);
}

constexpr bool bgm_playback_child_claim_exact(
    bool transition_consumed, uint64_t parent_ordinal, uint64_t child_ordinal,
    uint64_t transition_handle, uint64_t child_old_handle) noexcept
{
    return !transition_consumed && parent_ordinal != 0
        && child_ordinal > parent_ordinal && transition_handle != 0
        && transition_handle == child_old_handle;
}

template <typename Observer>
bool bgm_playback_observe_best_effort(
    bool native_succeeded, Observer observer) noexcept
{
    if (!native_succeeded) return false;
    try {
        observer();
    } catch (...) {
    }
    return native_succeeded;
}

constexpr bool bgm_playback_set_correlation_exact(
    const BgmPlaybackSetCorrelationFacts& f) noexcept
{
    return f.anchored && f.controller_exact && f.slot_bgm_exact
        && f.lease_exact && f.lifecycle_exact && f.unresolved;
}

struct BgmPlaybackLineageInheritanceFacts final {
    bool parent_anchor_exact = false;
    bool token_epoch_exact = false;
    bool lifecycle_state_epoch_exact = false;
    bool lease_exact = false;
    bool controller_chain_exact = false;
    bool handle_generation_exact = false;
    bool sound_identity_exact = false;
    bool alternating_transition_exact = false;
};

constexpr bool bgm_playback_lineage_inheritance_exact(
    const BgmPlaybackLineageInheritanceFacts& f) noexcept
{
    return f.parent_anchor_exact && f.token_epoch_exact
        && f.lifecycle_state_epoch_exact && f.lease_exact
        && f.controller_chain_exact && f.handle_generation_exact
        && f.sound_identity_exact && f.alternating_transition_exact;
}

constexpr bool bgm_playback_alternating_transition_exact(
    uint64_t custom_old_handle, uint64_t canonical_intermediate_handle,
    uint64_t latest_custom_handle, bool canonical_sound_positive,
    bool latest_custom_sound_exact) noexcept
{
    const uint64_t old_generation = custom_old_handle >> 32;
    const uint64_t canonical_generation = canonical_intermediate_handle >> 32;
    const uint64_t custom_generation = latest_custom_handle >> 32;
    return custom_old_handle != 0 && canonical_intermediate_handle != 0
        && latest_custom_handle != 0 && old_generation != 0
        && old_generation < canonical_generation
        && canonical_generation < custom_generation
        && canonical_sound_positive && latest_custom_sound_exact;
}

constexpr bool bgm_playback_marker_batch_current(
    uint64_t snapshot_collection_version,
    uint64_t current_collection_version,
    uint64_t snapshot_ordinal, uint64_t snapshot_version,
    uint64_t current_ordinal, uint64_t current_version) noexcept
{
    return snapshot_collection_version == current_collection_version
        && snapshot_ordinal != 0 && snapshot_ordinal == current_ordinal
        && snapshot_version == current_version;
}

constexpr bool bgm_playback_session_correlation_exact(
    const BgmPlaybackSessionCorrelationFacts& f) noexcept
{
    if (!f.continuity_exact) return false;
    if (f.provenance == BgmPlaybackLineageProvenance::SessionScoped) {
        return f.initial_session_epoch != 0 && f.initial_cycle_epoch != 0
            && f.current_session_epoch == f.initial_session_epoch
            && f.current_cycle_epoch == f.initial_cycle_epoch;
    }
    if (f.provenance != BgmPlaybackLineageProvenance::PreSession
        || f.initial_session_epoch != 0 || f.initial_cycle_epoch != 0) {
        return false;
    }
    if (f.current_session_epoch == 0 && f.current_cycle_epoch == 0) {
        return f.correlated_session_epoch == 0
            && f.correlated_cycle_epoch == 0;
    }
    if (f.current_session_epoch == 0 || f.current_cycle_epoch == 0) return false;
    return (f.correlated_session_epoch == 0
            && f.correlated_cycle_epoch == 0)
        || (f.correlated_session_epoch == f.current_session_epoch
            && f.correlated_cycle_epoch == f.current_cycle_epoch);
}

constexpr bool bgm_playback_refresh_publish_current(
    uint64_t snapshot_ordinal, uint64_t snapshot_version,
    uint64_t current_ordinal, uint64_t current_version,
    uint64_t snapshot_collection_version,
    uint64_t current_collection_version) noexcept
{
    return snapshot_ordinal != 0 && snapshot_ordinal == current_ordinal
        && snapshot_version == current_version
        && snapshot_collection_version == current_collection_version;
}

constexpr bool bgm_playback_old_handle_identity_conflict(
    uint64_t expected_handle, uintptr_t expected_sound,
    uint64_t observed_handle, uintptr_t observed_sound) noexcept
{
    return expected_handle != 0 && observed_handle == expected_handle
        && observed_sound != expected_sound;
}

constexpr bool bgm_playback_record_resolved(
    bool active, bool failed, bool old_absent,
    uint64_t new_handle, bool new_absent,
    bool transition_chain_resolved) noexcept
{
    (void)failed;
    return active && old_absent && (new_handle == 0 || new_absent)
        && transition_chain_resolved;
}

constexpr BgmPlaybackAggregateMarker classify_bgm_playback_aggregate(
    const BgmPlaybackAggregateFacts& f) noexcept
{
    if (!f.identity_exact || !f.epochs_exact || !f.handles_exact
        || !f.destination_aba_safe || f.failed) {
        return BgmPlaybackAggregateMarker::Invalid;
    }
    const bool closed = f.borrower_count == 0 && f.all_retired_absent
        && !f.final_active_present;
    if (f.release_committed && !closed) {
        return BgmPlaybackAggregateMarker::ReleaseCommittedBorrowers;
    }
    if (f.shutdown) {
        return closed ? BgmPlaybackAggregateMarker::ShutdownClosed
                      : BgmPlaybackAggregateMarker::ShutdownRetained;
    }
    if (f.list_exit) {
        return closed ? BgmPlaybackAggregateMarker::ListExitClosed
                      : BgmPlaybackAggregateMarker::ListExitPending;
    }
    if (f.release_authorized && !closed) {
        return BgmPlaybackAggregateMarker::ReleaseBlockedBorrowers;
    }
    if (closed) return BgmPlaybackAggregateMarker::AggregateClosed;
    if (f.final_active_present && f.final_active_ready) {
        return BgmPlaybackAggregateMarker::FinalActiveBorrower;
    }
    return BgmPlaybackAggregateMarker::Invalid;
}

constexpr bool bgm_playback_aggregate_authorizes_mutation(
    const BgmPlaybackAggregateFacts&) noexcept
{
    return false;
}

struct BgmPlaybackAggregateMutationFacts final {
    bool lineage_exact = false;
    bool record_version_exact = false;
    bool operation_nonce_exact = false;
    bool route_lease_exact = false;
    bool lifecycle_exact = false;
    bool tokens_exact = false;
    bool identity_exact = false;
    bool owner_command_exact = false;
    bool canonical_owner_exact = false;
    bool native_exact_once = false;
    bool boundary_or_pending_exact = false;
};

constexpr bool bgm_playback_aggregate_mutation_exact(
    const BgmPlaybackAggregateMutationFacts& f) noexcept
{
    return f.lineage_exact && f.record_version_exact
        && f.operation_nonce_exact && f.route_lease_exact
        && f.lifecycle_exact && f.tokens_exact && f.identity_exact
        && f.owner_command_exact && f.canonical_owner_exact
        && f.native_exact_once && f.boundary_or_pending_exact;
}

struct BgmPlaybackAggregateCanonicalExitClearFacts final {
    bool exit_publication_exact = false;
    bool mutation_authority_drained = false;
    bool latest_record_exact = false;
    bool record_version_exact = false;
    bool collection_version_exact = false;
    bool list_exit_exact = false;
    bool current_active_exact = false;
    bool record_not_failed = false;
    bool destination_aba_safe = false;
    bool identity_exact = false;
    bool continuity_exact = false;
    bool controller_proof_exact = false;
    bool slot_bgm_exact = false;
    bool canonical_sound_pointer_exact = false;
    bool canonical_sound_identity_exact = false;
    bool new_handle_exact = false;
    bool canonical_handle_exact = false;
    bool state4_exact = false;
    bool no_pending_set = false;
    bool no_pending_boundary = false;
    bool no_unresolved_request = false;
    bool no_custom_publication = false;
    bool route_exact = false;
    bool lease_exact = false;
    bool lifecycle_exact = false;
    bool tokens_exact = false;
    bool retained_custom_route_exact = false;
};

constexpr bool bgm_playback_aggregate_canonical_exit_clear_exact(
    const BgmPlaybackAggregateCanonicalExitClearFacts& f) noexcept
{
    return f.exit_publication_exact && f.mutation_authority_drained
        && f.latest_record_exact && f.record_version_exact
        && f.collection_version_exact && f.list_exit_exact
        && f.current_active_exact && f.record_not_failed
        && f.destination_aba_safe && f.identity_exact && f.continuity_exact
        && f.controller_proof_exact && f.slot_bgm_exact
        && f.canonical_sound_pointer_exact
        && f.canonical_sound_identity_exact && f.new_handle_exact
        && f.canonical_handle_exact && f.state4_exact && f.no_pending_set
        && f.no_pending_boundary && f.no_unresolved_request
        && f.no_custom_publication && f.route_exact && f.lease_exact
        && f.lifecycle_exact && f.tokens_exact
        && f.retained_custom_route_exact;
}

constexpr bool bgm_playback_list_return_clear_authorized(
    const bool legacy_route_owned,
    const bool aggregate_canonical_exit_exact) noexcept
{
    return legacy_route_owned || aggregate_canonical_exit_exact;
}

struct BgmPlaybackCanonicalSubstrateRelinquishmentFacts final {
    bool legacy_route_unowned = false;
    bool aggregate_authority_exact = false;
    bool pre_publication_revalidated = false;
    bool version_epoch_exact = false;
};

enum class BgmPlaybackCanonicalSubstrateRelinquishmentState : uint8_t {
    None,
    Pending,
    RouteCommitted,
    CanonicalRelinquished,
    Failed,
};

struct BgmPlaybackCanonicalSubstratePhaseFacts final {
    bool generation_nonzero = false;
    bool expected_state = false;
    bool ordinal_exact = false;
    bool record_version_exact = false;
    bool collection_version_exact = false;
    bool list_exit_epoch_exact = false;
    bool route_generation_exact = false;
    bool lifecycle_epoch_exact = false;
    bool controller_exact = false;
    bool request_sound_exact = false;
    bool tokens_exact = false;
};

constexpr bool bgm_playback_canonical_substrate_phase_exact(
    const BgmPlaybackCanonicalSubstratePhaseFacts& f) noexcept
{
    return f.generation_nonzero && f.expected_state && f.ordinal_exact
        && f.record_version_exact && f.collection_version_exact
        && f.list_exit_epoch_exact && f.route_generation_exact
        && f.lifecycle_epoch_exact && f.controller_exact
        && f.request_sound_exact && f.tokens_exact;
}

constexpr bool bgm_playback_canonical_substrate_partial_blocks_release(
    const BgmPlaybackCanonicalSubstrateRelinquishmentState state) noexcept
{
    return state == BgmPlaybackCanonicalSubstrateRelinquishmentState::Pending
        || state == BgmPlaybackCanonicalSubstrateRelinquishmentState::RouteCommitted
        || state == BgmPlaybackCanonicalSubstrateRelinquishmentState::Failed;
}

constexpr bool bgm_playback_canonical_substrate_relinquishment_exact(
    const BgmPlaybackCanonicalSubstrateRelinquishmentFacts& f) noexcept
{
    return f.legacy_route_unowned && f.aggregate_authority_exact
        && f.pre_publication_revalidated && f.version_epoch_exact;
}

constexpr bool bgm_playback_list_return_native_clear_required(
    const bool legacy_route_owned,
    const bool canonical_substrate_relinquishment_exact) noexcept
{
    (void)canonical_substrate_relinquishment_exact;
    return legacy_route_owned;
}

struct BgmPlaybackCanonicalSubstrateClosureFacts final {
    bool relinquishment_committed = false;
    bool handle_exact = false;
    bool sound_identity_exact = false;
    bool controller_route_exact = false;
    bool old_request_absent = false;
    bool custom_request_absent = false;
    bool no_pending_or_unresolved = false;
    bool record_healthy = false;
    bool substrate_current_or_absent = false;
};

constexpr bool bgm_playback_canonical_substrate_closure_exact(
    const BgmPlaybackCanonicalSubstrateClosureFacts& f) noexcept
{
    return f.relinquishment_committed && f.handle_exact
        && f.sound_identity_exact && f.controller_route_exact
        && f.old_request_absent && f.custom_request_absent
        && f.no_pending_or_unresolved && f.record_healthy
        && f.substrate_current_or_absent;
}

constexpr bool bgm_playback_canonical_substrate_active_sound_exact(
    const bool proof_committed, const bool controller_exact,
    const bool slot_bgm_exact, const bool sound_pointer_exact,
    const bool sound_identity_exact, const bool request_exact,
    const bool state4_exact) noexcept
{
    return proof_committed && controller_exact && slot_bgm_exact
        && sound_pointer_exact && sound_identity_exact && request_exact
        && state4_exact;
}

struct BgmCanonicalSubstrateBridgeEnrichmentFacts final {
    bool source_found = false;
    bool proof_active = false;
    bool proof_state_exact = false;
    bool proof_transaction_nonzero = false;
    bool generation_available = false;
    bool bridge_uninitialized = false;
    bool source_active = false;
    bool source_ordinal_exact = false;
    bool source_version_monotonic = false;
    bool collection_version_monotonic = false;
    bool source_state_exact = false;
    bool transaction_exact = false;
    bool list_exit_epoch_exact = false;
    bool controller_exact = false;
    bool slot_bgm_exact = false;
    bool canonical_sound_exact = false;
    bool canonical_request_exact = false;
    bool route_generation_exact = false;
    bool lease_exact = false;
    bool lifecycle_epoch_exact = false;
    bool tokens_exact = false;
    bool callback_identity_exact = false;
    bool callback_distinct_from_canonical = false;
    bool release_epoch_exact = false;
    bool lifecycle_complete_custom_absent = false;
    bool lifecycle_anchor_exact = false;
    bool substrate_current_exact = false;
    bool record_healthy = false;
    bool no_pending_provenance = false;
    bool no_child_provenance = false;
};

constexpr bool bgm_canonical_substrate_bridge_source_version_monotonic(
    const uint64_t proof_version,
    const uint64_t source_version) noexcept
{
    return proof_version != 0 && source_version >= proof_version;
}

constexpr bool bgm_canonical_substrate_bridge_release_epoch_exact(
    const uint64_t relinquishment_epoch,
    const uint64_t release_committed_epoch,
    const uint64_t cleanup_generation) noexcept
{
    return relinquishment_epoch != 0
        && release_committed_epoch > relinquishment_epoch
        && cleanup_generation != 0;
}

enum class BgmCanonicalSubstrateBridgeEnrichmentFailure : uint8_t {
    None,
    SourceMissing,
    ProofInactive,
    ProofState,
    ProofTransaction,
    GenerationExhausted,
    BridgeAlreadyInitialized,
    SourceInactive,
    SourceOrdinal,
    SourceVersionRegression,
    CollectionVersionRegression,
    SourceState,
    Transaction,
    ListExitEpoch,
    Controller,
    SlotBgm,
    CanonicalSound,
    CanonicalRequest,
    RouteGeneration,
    Lease,
    LifecycleEpoch,
    Tokens,
    CallbackIdentity,
    CallbackAliasesCanonical,
    ReleaseEpoch,
    LifecycleNotCompleteAbsent,
    LifecycleAnchor,
    SubstrateNotCurrent,
    RecordUnhealthy,
    PendingProvenance,
    ChildProvenance,
};

constexpr BgmCanonicalSubstrateBridgeEnrichmentFailure
first_bgm_canonical_substrate_bridge_enrichment_failure(
    const BgmCanonicalSubstrateBridgeEnrichmentFacts& f) noexcept
{
    if (!f.source_found) return BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceMissing;
    if (!f.proof_active) return BgmCanonicalSubstrateBridgeEnrichmentFailure::ProofInactive;
    if (!f.proof_state_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::ProofState;
    if (!f.proof_transaction_nonzero) return BgmCanonicalSubstrateBridgeEnrichmentFailure::ProofTransaction;
    if (!f.generation_available) return BgmCanonicalSubstrateBridgeEnrichmentFailure::GenerationExhausted;
    if (!f.bridge_uninitialized) return BgmCanonicalSubstrateBridgeEnrichmentFailure::BridgeAlreadyInitialized;
    if (!f.source_active) return BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceInactive;
    if (!f.source_ordinal_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceOrdinal;
    if (!f.source_version_monotonic) return BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceVersionRegression;
    if (!f.collection_version_monotonic) return BgmCanonicalSubstrateBridgeEnrichmentFailure::CollectionVersionRegression;
    if (!f.source_state_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceState;
    if (!f.transaction_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::Transaction;
    if (!f.list_exit_epoch_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::ListExitEpoch;
    if (!f.controller_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::Controller;
    if (!f.slot_bgm_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::SlotBgm;
    if (!f.canonical_sound_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::CanonicalSound;
    if (!f.canonical_request_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::CanonicalRequest;
    if (!f.route_generation_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::RouteGeneration;
    if (!f.lease_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::Lease;
    if (!f.lifecycle_epoch_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::LifecycleEpoch;
    if (!f.tokens_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::Tokens;
    if (!f.callback_identity_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::CallbackIdentity;
    if (!f.callback_distinct_from_canonical) return BgmCanonicalSubstrateBridgeEnrichmentFailure::CallbackAliasesCanonical;
    if (!f.release_epoch_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::ReleaseEpoch;
    if (!f.lifecycle_complete_custom_absent) return BgmCanonicalSubstrateBridgeEnrichmentFailure::LifecycleNotCompleteAbsent;
    if (!f.lifecycle_anchor_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::LifecycleAnchor;
    if (!f.substrate_current_exact) return BgmCanonicalSubstrateBridgeEnrichmentFailure::SubstrateNotCurrent;
    if (!f.record_healthy) return BgmCanonicalSubstrateBridgeEnrichmentFailure::RecordUnhealthy;
    if (!f.no_pending_provenance) return BgmCanonicalSubstrateBridgeEnrichmentFailure::PendingProvenance;
    if (!f.no_child_provenance) return BgmCanonicalSubstrateBridgeEnrichmentFailure::ChildProvenance;
    return BgmCanonicalSubstrateBridgeEnrichmentFailure::None;
}

constexpr bool bgm_canonical_substrate_bridge_enrichment_exact(
    const BgmCanonicalSubstrateBridgeEnrichmentFacts& f) noexcept
{
    return first_bgm_canonical_substrate_bridge_enrichment_failure(f)
        == BgmCanonicalSubstrateBridgeEnrichmentFailure::None;
}

static_assert(std::is_trivially_copyable_v<
    BgmCanonicalSubstrateBridgeEnrichmentFacts>);

struct BgmPlaybackListReturnPostClearFacts final {
    bool native_clear_called_once = false;
    bool controller_chain_exact = false;
    bool cleared_sound_null = false;
    bool cleared_request_zero = false;
    bool cleared_state_zero = false;
    bool route_state_commit_exact = false;
};

constexpr bool bgm_playback_list_return_post_clear_exact(
    const BgmPlaybackListReturnPostClearFacts& f) noexcept
{
    return f.native_clear_called_once && f.controller_chain_exact
        && f.cleared_sound_null && f.cleared_request_zero
        && f.cleared_state_zero && f.route_state_commit_exact;
}

constexpr bool bgm_playback_list_return_synchronous_absence_allowed(
    const bool aggregate_clear_authority_exact,
    const bool aggregate_pre_call_revalidated,
    const bool post_clear_exact) noexcept
{
    return aggregate_clear_authority_exact
        && aggregate_pre_call_revalidated && post_clear_exact;
}

constexpr bool bgm_playback_aggregate_release_exact(
    bool exit_authority, bool all_borrowers_closed,
    bool custom_token, bool canonical_token_untouched,
    bool single_claimant, bool destination_aba_safe) noexcept
{
    return exit_authority && all_borrowers_closed && custom_token
        && canonical_token_untouched && single_claimant
        && destination_aba_safe;
}

constexpr bool bgm_playback_aggregate_native_callbacks_exact(
    uint32_t expected_set, uint32_t actual_set,
    uint32_t expected_play, uint32_t actual_play,
    uint32_t expected_stop, uint32_t actual_stop,
    bool tick_suppressed, bool chart_mutated) noexcept
{
    return expected_set == actual_set && expected_play == actual_play
        && expected_stop == actual_stop && !tick_suppressed && !chart_mutated;
}

constexpr bool bgm_playback_aggregate_exit_blocks_mutation(
    bool exit_pending, bool native_forwarded_once,
    bool owner_patch_applied, bool new_admission) noexcept
{
    return exit_pending && native_forwarded_once
        && !owner_patch_applied && !new_admission;
}

struct BgmPlaybackAggregateExitOwnershipFacts final {
    bool active_or_retained_borrower = false;
    bool unresolved_publication = false;
    bool release_claim_owned = false;
    bool rollback_owned = false;
    bool route_or_cleanup_owned = false;
    bool observer_lease_in_flight = false;
};

constexpr bool bgm_playback_aggregate_exit_ownership_present(
    const BgmPlaybackAggregateExitOwnershipFacts& f) noexcept
{
    return f.active_or_retained_borrower || f.unresolved_publication
        || f.release_claim_owned || f.rollback_owned
        || f.route_or_cleanup_owned || f.observer_lease_in_flight;
}

constexpr bool bgm_playback_aggregate_menu_ready(
    const BgmPlaybackAggregateExitOwnershipFacts& f,
    bool exit_requested, bool exit_pending) noexcept
{
    return !exit_requested && !exit_pending
        && !bgm_playback_aggregate_exit_ownership_present(f);
}

struct BgmPlaybackAggregateExitClosureFacts final {
    bool exit_authority = false;
    bool all_old_requests_absent = false;
    bool no_current_or_ready_borrower = false;
    bool no_pending_set_boundary_or_child = false;
    bool identity_and_aba_exact = false;
    bool owner_canonical_or_zero = false;
    bool single_custom_release_claimant = false;
    bool canonical_token_untouched = false;
    bool no_unresolved_native_request = false;
    bool no_observer_lease_in_flight = false;
};

constexpr bool bgm_playback_aggregate_exit_closure_exact(
    const BgmPlaybackAggregateExitClosureFacts& f) noexcept
{
    return f.exit_authority && f.all_old_requests_absent
        && f.no_current_or_ready_borrower
        && f.no_pending_set_boundary_or_child
        && f.identity_and_aba_exact && f.owner_canonical_or_zero
        && f.single_custom_release_claimant && f.canonical_token_untouched
        && f.no_unresolved_native_request
        && f.no_observer_lease_in_flight;
}

constexpr bool bgm_playback_new_song_arm_allowed(
    bool exit_pending, bool cleanup_complete,
    uint64_t old_song_key, uint64_t requested_song_key) noexcept
{
    if (exit_pending || !cleanup_complete || requested_song_key == 0) return false;
    // Same-song re-entry is also fresh: no old lineage may survive cleanup.
    return old_song_key == 0 || requested_song_key != 0;
}

constexpr bool bgm_playback_no_gain_mutation(
    uint32_t bgm_gain_write_count) noexcept
{
    return bgm_gain_write_count == 0;
}

constexpr bool bgm_playback_exit_request_closed(
    bool current_active, bool new_ready,
    uint64_t new_handle, bool new_absent) noexcept
{
    // Readiness is historical once the exact request is independently absent.
    (void)new_ready;
    return !current_active && (new_handle == 0 || new_absent);
}

constexpr bool bgm_playback_initial_anchor_state_clean(
    bool pending_set, bool boundary_active, bool boundary_consumed,
    uint64_t boundary_nonce, uint64_t latest_custom_handle,
    bool latest_custom_consumed) noexcept
{
    return !pending_set && !boundary_active && !boundary_consumed
        && boundary_nonce == 0 && latest_custom_handle == 0
        && !latest_custom_consumed;
}

struct BgmPlaybackRollbackAuthorityFacts final {
    bool descriptor_exact = false;
    bool positive_identity_exact = false;
    bool controller_proof_exact = false;
    bool slot_bgm_chain_exact = false;
    bool record_version_exact = false;
    bool collection_version_exact = false;
    bool operation_nonce_exact = false;
    bool route_lease_exact = false;
    bool lifecycle_tokens_exact = false;
    bool owner_thread_command_exact = false;
    bool request_exact = false;
};

constexpr bool bgm_playback_rollback_authority_exact(
    const BgmPlaybackRollbackAuthorityFacts& facts) noexcept
{
    return facts.descriptor_exact && facts.positive_identity_exact
        && facts.controller_proof_exact && facts.slot_bgm_chain_exact
        && facts.record_version_exact && facts.collection_version_exact
        && facts.operation_nonce_exact && facts.route_lease_exact
        && facts.lifecycle_tokens_exact && facts.owner_thread_command_exact
        && facts.request_exact;
}

constexpr bool bgm_playback_emergency_init_allowed(
    const bool all_slots_free, const bool durable_empty) noexcept
{
    return all_slots_free && durable_empty;
}

constexpr bool bgm_playback_emergency_slot_arm_allowed(
    const bool slot_free, const uint64_t prior_generation) noexcept
{
    return slot_free && prior_generation != UINT64_MAX;
}

constexpr bool bgm_playback_emergency_migration_complete(
    const bool descriptor_copied, const bool allocation_succeeded,
    const bool durable_owner_published) noexcept
{
    return descriptor_copied && allocation_succeeded && durable_owner_published;
}

constexpr bool bgm_playback_emergency_shutdown_succeeded(
    const bool hooks_disabled, const bool callbacks_drained,
    const bool all_slots_free, const bool durable_empty) noexcept
{
    return hooks_disabled && callbacks_drained
        && bgm_playback_emergency_init_allowed(all_slots_free, durable_empty);
}

enum class BgmPlaybackProtectedRollbackResult : uint8_t {
    Restored,
    NativeOverwrite,
    Retained,
};

template <typename Read, typename Authority, typename Restore>
BgmPlaybackProtectedRollbackResult bgm_playback_protected_rollback_cleanup(
    const uint64_t expected_value, const uint64_t candidate_value,
    Read&& read, Authority&& authority, Restore&& restore,
    uint64_t& current_out) noexcept
{
    static_assert(noexcept(read(current_out)), "rollback read must be nonthrowing");
    static_assert(noexcept(authority()), "authority proof must be nonthrowing");
    static_assert(noexcept(restore(expected_value)),
        "rollback restore must be nonthrowing");
    current_out = 0;
    if (!read(current_out)) return BgmPlaybackProtectedRollbackResult::Retained;
    if (current_out != candidate_value) {
        return BgmPlaybackProtectedRollbackResult::NativeOverwrite;
    }
    if (!authority() || !restore(expected_value)) {
        return BgmPlaybackProtectedRollbackResult::Retained;
    }
    current_out = expected_value;
    return BgmPlaybackProtectedRollbackResult::Restored;
}

template <typename OwnershipClear, typename BeginMutation>
bool bgm_playback_protected_install_begin(
    OwnershipClear&& ownership_clear, BeginMutation&& begin_mutation) noexcept
{
    static_assert(noexcept(ownership_clear()),
        "install ownership precondition must be nonthrowing");
    static_assert(noexcept(begin_mutation()),
        "install first mutation must be nonthrowing");
    if (!ownership_clear()) return false;
    begin_mutation();
    return true;
}

inline bool bgm_playback_custom_set_post_request_exact(
    const bool native_succeeded, const bool request_nonzero,
    const bool request_type8, const bool state2,
    const bool sound_identity_exact) noexcept
{
    return native_succeeded && request_nonzero && request_type8
        && state2 && sound_identity_exact;
}

constexpr bool bgm_playback_custom_set_cleanup_authority_exact(
    const bool native_succeeded, const bool pre_set_authority_exact,
    const bool exact_post_set_strengthened) noexcept
{
    return native_succeeded
        ? exact_post_set_strengthened
        : pre_set_authority_exact;
}

inline bool bgm_playback_custom_play_rollback_request_exact(
    uint64_t authority_request,
    uint64_t transition_set_handle,
    uint64_t transition_set_nonce) noexcept
{
    return AudioBgmRequestHandle{authority_request}.valid_bgm_request()
        && authority_request == transition_set_handle
        && authority_request != transition_set_nonce;
}

} // namespace ff7r::piano::game
