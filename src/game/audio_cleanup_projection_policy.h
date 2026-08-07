#pragma once

#include "game/audio_cleanup_policy.h"
#include "game/song_registry.h"
#include "game/uobject_lifetime.h"
#include "game/uobject_locator_core.h"
#include "game/audio_memory_read_policy.h"
#include "game/audio_native_call_policy.h"
#include "game/pause_resume_policy.h"
#include "game/selection_audio_policy.h"
#include "game/canonical_substrate_policy.h"
#include "game/bgm_playback_aggregate_policy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace ff7r::piano::game {

enum class CustomActivationQuarantineFailure : uint8_t {
    None,
    ArmMissing,
    SidecarMissing,
    SoundIdentityInvalid,
    BankQualificationFailed,
    PendingRestoreFailed,
    PostRestoreAuthorityFailed,
    PatchFailed,
    RouteGenerationFailed,
    SetupTokenAdvanceFailed,
    DuplicateCallback,
};

enum class CustomActivationQuarantineClearReason : uint8_t {
    None,
    ListReturn,
    TitleOrReselection,
    Shutdown,
};

constexpr bool custom_activation_quarantine_clear_event(
    const CustomActivationQuarantineClearReason reason) noexcept
{
    return reason == CustomActivationQuarantineClearReason::ListReturn
        || reason == CustomActivationQuarantineClearReason::TitleOrReselection
        || reason == CustomActivationQuarantineClearReason::Shutdown;
}

struct CustomActivationQuarantineRecord {
    bool active = false;
    uint64_t generation = 0;
    CustomActivationQuarantineFailure failure =
        CustomActivationQuarantineFailure::None;
    uint32_t nested_failure = 0;
    uint64_t selection_generation = 0;
    uint64_t route_generation = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
    uintptr_t callback_sound = 0;
    int32_t callback_sound_index = -1;
    uint32_t callback_sound_serial = 0;
    bool original_forwarded = false;
    bool token_state_none = false;

    constexpr explicit operator bool() const noexcept
    {
        return active && generation != 0
            && failure != CustomActivationQuarantineFailure::None
            && selection_generation != 0 && route_generation != 0
            && lease_generation != 0 && song_key != 0
            && !original_forwarded && token_state_none;
    }
};

struct CustomActivationQuarantineClearFacts {
    bool record_exact = false;
    bool explicit_lifecycle_event = false;
    bool original_not_forwarded = false;
    bool token_state_none = false;
    bool unpublished_setup_absent = false;
    bool route_idle = false;
    bool route_disabled = false;
    bool route_identity_empty = false;
    bool custom_resource_absent = false;
    bool request_absent = false;
    bool pending_patch_empty = false;
    bool frozen_patch_empty = false;
    bool failed_patch_empty = false;
    bool active_journal_empty = false;
    bool frozen_lease_inactive = false;
    bool playback_absent = false;
    bool cleanup_lease_absent = false;
    bool normal_token_ownership_absent = false;
    bool cleanup_only_ownership_absent = false;
    bool aggregate_borrowers_absent = false;
    bool substrate_bridge_absent = false;
    bool selection_reservation_absent = false;
    bool list_cleanup_clear = false;
    bool retirement_authority_absent = false;
    bool deferred_handoff_absent = false;
    bool pause_resume_absent = false;
    bool aggregate_cleanup_absent = false;
};

enum class CustomActivationQuarantineClearFailure : uint8_t {
    None,
    RecordMismatch,
    NoExplicitLifecycleEvent,
    OriginalForwarded,
    TokenStatePresent,
    UnpublishedSetupActive,
    RouteNotIdle,
    RouteNotDisabled,
    RouteIdentityActive,
    CustomResourceOwned,
    RequestActive,
    PendingPatchActive,
    FrozenPatchActive,
    FailedPatchActive,
    ActiveJournal,
    FrozenLeaseActive,
    PlaybackActive,
    CleanupLeaseActive,
    NormalTokenOwnershipActive,
    CleanupOnlyOwnershipActive,
    AggregateBorrowerActive,
    SubstrateBridgeActive,
    SelectionReservationActive,
    ListCleanupPending,
    RetirementAuthorityActive,
    DeferredHandoffActive,
    PauseResumeActive,
    AggregateCleanupActive,
};

constexpr CustomActivationQuarantineClearFailure
first_custom_activation_quarantine_clear_failure(
    const CustomActivationQuarantineClearFacts& f) noexcept
{
    if (!f.record_exact) return CustomActivationQuarantineClearFailure::RecordMismatch;
    if (!f.explicit_lifecycle_event) return CustomActivationQuarantineClearFailure::NoExplicitLifecycleEvent;
    if (!f.original_not_forwarded) return CustomActivationQuarantineClearFailure::OriginalForwarded;
    if (!f.token_state_none) return CustomActivationQuarantineClearFailure::TokenStatePresent;
    if (!f.unpublished_setup_absent) return CustomActivationQuarantineClearFailure::UnpublishedSetupActive;
    if (!f.route_idle) return CustomActivationQuarantineClearFailure::RouteNotIdle;
    if (!f.route_disabled) return CustomActivationQuarantineClearFailure::RouteNotDisabled;
    if (!f.route_identity_empty) return CustomActivationQuarantineClearFailure::RouteIdentityActive;
    if (!f.custom_resource_absent) return CustomActivationQuarantineClearFailure::CustomResourceOwned;
    if (!f.request_absent) return CustomActivationQuarantineClearFailure::RequestActive;
    if (!f.pending_patch_empty) return CustomActivationQuarantineClearFailure::PendingPatchActive;
    if (!f.frozen_patch_empty) return CustomActivationQuarantineClearFailure::FrozenPatchActive;
    if (!f.failed_patch_empty) return CustomActivationQuarantineClearFailure::FailedPatchActive;
    if (!f.active_journal_empty) return CustomActivationQuarantineClearFailure::ActiveJournal;
    if (!f.frozen_lease_inactive) return CustomActivationQuarantineClearFailure::FrozenLeaseActive;
    if (!f.playback_absent) return CustomActivationQuarantineClearFailure::PlaybackActive;
    if (!f.cleanup_lease_absent) return CustomActivationQuarantineClearFailure::CleanupLeaseActive;
    if (!f.normal_token_ownership_absent) return CustomActivationQuarantineClearFailure::NormalTokenOwnershipActive;
    if (!f.cleanup_only_ownership_absent) return CustomActivationQuarantineClearFailure::CleanupOnlyOwnershipActive;
    if (!f.aggregate_borrowers_absent) return CustomActivationQuarantineClearFailure::AggregateBorrowerActive;
    if (!f.substrate_bridge_absent) return CustomActivationQuarantineClearFailure::SubstrateBridgeActive;
    if (!f.selection_reservation_absent) return CustomActivationQuarantineClearFailure::SelectionReservationActive;
    if (!f.list_cleanup_clear) return CustomActivationQuarantineClearFailure::ListCleanupPending;
    if (!f.retirement_authority_absent) return CustomActivationQuarantineClearFailure::RetirementAuthorityActive;
    if (!f.deferred_handoff_absent) return CustomActivationQuarantineClearFailure::DeferredHandoffActive;
    if (!f.pause_resume_absent) return CustomActivationQuarantineClearFailure::PauseResumeActive;
    if (!f.aggregate_cleanup_absent) return CustomActivationQuarantineClearFailure::AggregateCleanupActive;
    return CustomActivationQuarantineClearFailure::None;
}

constexpr bool committed_custom_play_setup_may_forward_unmodified(
    const bool committed_custom_activation,
    const bool quarantine_active) noexcept
{
    return !committed_custom_activation && !quarantine_active;
}

constexpr bool custom_activation_quarantine_callback_suppressed(
    const bool quarantine_active,
    const bool callback_pointer_exact,
    const bool expected_identity_available,
    const bool identity_read_succeeded,
    const bool identity_exact) noexcept
{
    if (!quarantine_active || !callback_pointer_exact) return false;
    if (!expected_identity_available || !identity_read_succeeded) return true;
    return identity_exact;
}

static_assert(std::is_trivially_copyable_v<CustomActivationQuarantineRecord>);
static_assert(std::is_trivially_copyable_v<CustomActivationQuarantineClearFacts>);

enum class CleanupOnlyPostOriginalOwnership : uint8_t {
    None,
    NormalDetached,
    CleanupOnly,
    FailedRetained,
};

struct CleanupOnlyPostOriginalOwnershipFacts {
    bool deferred_bridge_original_entered = false;
    bool normal_detached_retained = false;
    bool playback_authority_absent = false;
    bool sound_identity_exact = false;
    bool distinct_type1_tokens = false;
    bool both_kinds2 = false;
    bool owner_restored = false;
};

constexpr CleanupOnlyPostOriginalOwnership classify_cleanup_only_post_original_ownership(
    const CleanupOnlyPostOriginalOwnershipFacts& f) noexcept
{
    if (f.normal_detached_retained) {
        return CleanupOnlyPostOriginalOwnership::NormalDetached;
    }
    if (!f.deferred_bridge_original_entered) {
        return CleanupOnlyPostOriginalOwnership::None;
    }
    return f.playback_authority_absent && f.sound_identity_exact && f.distinct_type1_tokens
            && f.both_kinds2 && f.owner_restored
        ? CleanupOnlyPostOriginalOwnership::CleanupOnly
        : CleanupOnlyPostOriginalOwnership::FailedRetained;
}

constexpr bool cleanup_only_post_callback_observation_required(
    const bool callback_gate_entered,
    const uint32_t callback_depth_after_decrement) noexcept
{
    return callback_gate_entered && callback_depth_after_decrement == 0;
}

constexpr bool cleanup_only_backing_is_detached(
    const bool backing_read_succeeded, const void* const backing) noexcept
{
    return backing_read_succeeded && backing == nullptr;
}

constexpr bool cleanup_only_record_requires_exclusive_observation(
    const bool record_present,
    const bool phase_none,
    const bool phase_complete,
    const bool phase_failed_retained) noexcept
{
    return record_present && !phase_none && !phase_complete
        && !phase_failed_retained;
}

constexpr bool cleanup_only_observation_generation_exact(
    const uint64_t expected_generation,
    const bool record_present,
    const uint64_t observed_generation) noexcept
{
    return expected_generation != 0 && record_present
        && observed_generation == expected_generation;
}

struct CleanupOnlyObservationFingerprint {
    bool valid = false;
    uint64_t generation = 0;
    uint8_t phase = 0;
    uint8_t failure = 0;
    uint64_t fact_mask = 0;
    bool release_eligible = false;
    bool release_claimed = false;
    uint8_t release_outcome = 0;
};

static_assert(std::is_trivially_copyable_v<CleanupOnlyObservationFingerprint>);

struct CleanupOnlyObservationEmissionDecision {
    bool emit = false;
    bool reset_fact_budget = false;
    bool spend_fact_budget = false;
};

constexpr CleanupOnlyObservationEmissionDecision
cleanup_only_observation_emission_decision(
    const CleanupOnlyObservationFingerprint& previous,
    const CleanupOnlyObservationFingerprint& current,
    const uint32_t fact_budget_used,
    const uint32_t fact_budget_limit) noexcept
{
    if (!current.valid) return {};
    if (!previous.valid || previous.generation != current.generation) {
        return {true, true, false};
    }
    if (previous.phase != current.phase
        || previous.failure != current.failure
        || previous.release_eligible != current.release_eligible
        || previous.release_claimed != current.release_claimed
        || previous.release_outcome != current.release_outcome) {
        return {true, false, false};
    }
    if (previous.fact_mask != current.fact_mask
        && fact_budget_used < fact_budget_limit) {
        return {true, false, true};
    }
    return {};
}

struct DeferredMutationLogFacts {
    bool proposal_valid = false;
    bool mutation_complete = false;
    bool mutation_authority_released = false;
};

enum class BgmPlaybackDeferredLogKind : uint8_t {
    None,
    SetFailure,
    SetSuccess,
    PlayFailure,
    PlaySuccess,
};

struct BgmPlaybackDeferredLogProposal {
    BgmPlaybackDeferredLogKind kind = BgmPlaybackDeferredLogKind::None;
    BgmPlaybackSetCommitFailure set_failure = BgmPlaybackSetCommitFailure::None;
    BgmPlaybackPlayFailure play_failure = BgmPlaybackPlayFailure::None;
    const char* operation_reason = nullptr;
    uint64_t ordinal = 0;
    uint64_t version = 0;
    uint64_t nonce = 0;
    void* requested_sound = nullptr;
    UObjectLiveHandle requested_identity{};
    void* live_sound = nullptr;
    uint64_t request = 0;
    uint8_t state = 0;
    bool custom = false;
    uint64_t expected_version = 0;
    uint64_t current_version = 0;
    uint64_t expected_collection = 0;
    uint64_t current_collection = 0;
    uint64_t expected_route = 0;
    uint64_t current_route = 0;
    uint64_t expected_lifecycle = 0;
    uint64_t current_lifecycle = 0;
    uint64_t previous_nonce = 0;
    uint64_t expected_nonce = 0;
    uint64_t old_handle = 0;
    uint64_t canonical_handle = 0;
    uint64_t custom_handle = 0;
    uint64_t token_epoch = 0;
    uint64_t lifecycle_state_epoch = 0;
    uint64_t set_epoch = 0;
    uint64_t play_epoch = 0;
    uint64_t set_nonce = 0;
    uint64_t play_nonce = 0;
    bool play_owner_rebound = false;
    bool mutation_complete = false;
};
static_assert(std::is_trivially_copyable_v<BgmPlaybackDeferredLogProposal>);

enum class BgmPlaybackPreparationLogKind : uint8_t {
    None,
    StalePlay,
    LineageInherited,
    SetPrepared,
};

struct BgmPlaybackPreparationLogEntry {
    BgmPlaybackPreparationLogKind kind = BgmPlaybackPreparationLogKind::None;
    const char* reason = nullptr;
    uint64_t ordinal = 0;
    uint64_t version = 0;
    uint64_t nonce = 0;
    void* requested_sound = nullptr;
    UObjectLiveHandle requested_identity{};
    uint64_t request = 0;
    bool custom = false;
    uint64_t parent_ordinal = 0;
    uint64_t child_ordinal = 0;
    uint64_t token_epoch = 0;
    uint64_t lifecycle_state_epoch = 0;
    uint64_t parent_old_handle = 0;
    uint64_t canonical_handle = 0;
    uint64_t child_old_handle = 0;
};

struct BgmPlaybackPreparationLogProposal {
    std::array<BgmPlaybackPreparationLogEntry, 34> entries{};
    uint32_t count = 0;
    bool mutation_complete = false;
};
static_assert(std::is_trivially_copyable_v<BgmPlaybackPreparationLogProposal>);

enum class BgmPlaybackPreparationEmissionPhase : uint8_t {
    Capturing,
    BorrowerAuthorityReleased,
};

constexpr bool bgm_playback_preparation_log_may_emit(
    const BgmPlaybackPreparationLogProposal& proposal,
    BgmPlaybackPreparationEmissionPhase phase,
    bool aggregate_mutation_authority_released) noexcept
{
    return proposal.count != 0 && proposal.mutation_complete
        && phase == BgmPlaybackPreparationEmissionPhase::BorrowerAuthorityReleased
        && aggregate_mutation_authority_released;
}

enum class BgmAggregateOwnerPatchLogStatus : uint8_t {
    None,
    AuthorizationBlocked,
    PatchFailed,
};

struct BgmAggregateOwnerPatchLogProposal {
    BgmAggregateOwnerPatchLogStatus status =
        BgmAggregateOwnerPatchLogStatus::None;
    bool play = false;
    uint64_t ordinal = 0;
    uint64_t version = 0;
    uint64_t predecessor = 0;
    uint64_t route_generation = 0;
    uint64_t lifecycle_epoch = 0;
    int32_t sound_index = -1;
    int32_t sound_serial = 0;
    bool exact_record = false;
    bool lifecycle_available = false;
    bool owner_binding_exact = false;
    bool owner_command_exact = false;
    bool patch_planned = false;
    bool rollback_armed = false;
    bool gate_open = false;
    bool write_exact = false;
    bool rollback_cleanup_attempted = false;
    bool mutation_complete = false;
};
static_assert(std::is_trivially_copyable_v<
    BgmAggregateOwnerPatchLogProposal>);

constexpr bool bgm_aggregate_owner_patch_log_may_emit(
    const BgmAggregateOwnerPatchLogProposal& proposal,
    const bool aggregate_mutation_authority_released) noexcept
{
    return proposal.status != BgmAggregateOwnerPatchLogStatus::None
        && proposal.mutation_complete
        && aggregate_mutation_authority_released;
}

struct BgmAggregateSetForwardLogProposal {
    bool valid = false;
    bool mutation_complete = false;
    uint64_t ordinal = 0;
    uint64_t version = 0;
    uint64_t predecessor = 0;
    uint64_t route_generation_before = 0;
    uint64_t route_generation_after = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
};
static_assert(std::is_trivially_copyable_v<
    BgmAggregateSetForwardLogProposal>);

struct BgmAggregateSetForwardResult {
    bool route_advanced = false;
    BgmAggregateSetForwardLogProposal log{};
};
static_assert(std::is_trivially_copyable_v<BgmAggregateSetForwardResult>);

constexpr bool bgm_aggregate_set_forward_log_may_emit(
    const BgmAggregateSetForwardResult& result) noexcept
{
    const auto& proposal = result.log;
    return proposal.valid && proposal.mutation_complete
        && result.route_advanced
        && proposal.route_generation_after
            == proposal.route_generation_before + 1;
}

constexpr bool deferred_mutation_log_may_emit(
    const DeferredMutationLogFacts& facts) noexcept
{
    return facts.proposal_valid && facts.mutation_complete
        && facts.mutation_authority_released;
}

struct CleanupOnlyCallbackQuiescenceFacts {
    bool outermost_body_completed = false;
    bool lifecycle_lease_released = false;
    bool shared_gate_released = false;
    bool exclusive_gate_acquired = false;
    bool native_and_mod_facts_reobserved = false;
};

constexpr bool cleanup_only_callback_quiescent(
    const CleanupOnlyCallbackQuiescenceFacts& f) noexcept
{
    return f.outermost_body_completed && f.lifecycle_lease_released
        && f.shared_gate_released && f.exclusive_gate_acquired
        && f.native_and_mod_facts_reobserved;
}

struct CleanupOnlyRouteQuiescenceFacts {
    bool cleanup_generation_exact = false;
    bool unpublished_setup_absent = false;
    bool substrate_bridge_absent = false;
    bool selection_reservation_absent = false;
    bool route_idle = false;
    bool route_disabled = false;
    bool route_identity_empty = false;
    bool list_cleanup_clear = false;
    bool pending_patch_empty = false;
    bool frozen_patch_empty = false;
    bool failed_patch_empty = false;
    bool active_journal_empty = false;
    bool stop_retirement_absent = false;
    bool deferred_handoff_absent = false;
    bool frozen_lease_inactive = false;
    bool playback_absent = false;
    bool aggregate_borrowers_absent = false;
    bool pause_resume_absent = false;
    bool aggregate_cleanup_absent = false;
};

enum class CleanupOnlyRouteQuiescenceFailure : uint8_t {
    None,
    CleanupGenerationDrift,
    UnpublishedSetupActive,
    SubstrateBridgeActive,
    SelectionReservationActive,
    RouteNotIdle,
    RouteNotDisabled,
    RouteIdentityActive,
    ListCleanupPending,
    PendingPatchActive,
    FrozenPatchActive,
    FailedPatchRetained,
    ActiveJournal,
    StopRetirementActive,
    DeferredHandoffActive,
    FrozenLeaseActive,
    PlaybackPublished,
    AggregateBorrowerActive,
    PauseResumeActive,
    AggregateCleanupActive,
};

constexpr CleanupOnlyRouteQuiescenceFailure
first_cleanup_only_route_quiescence_failure(
    const CleanupOnlyRouteQuiescenceFacts& f) noexcept
{
    if (!f.cleanup_generation_exact) return CleanupOnlyRouteQuiescenceFailure::CleanupGenerationDrift;
    if (!f.unpublished_setup_absent) return CleanupOnlyRouteQuiescenceFailure::UnpublishedSetupActive;
    if (!f.substrate_bridge_absent) return CleanupOnlyRouteQuiescenceFailure::SubstrateBridgeActive;
    if (!f.selection_reservation_absent) return CleanupOnlyRouteQuiescenceFailure::SelectionReservationActive;
    if (!f.route_idle) return CleanupOnlyRouteQuiescenceFailure::RouteNotIdle;
    if (!f.route_disabled) return CleanupOnlyRouteQuiescenceFailure::RouteNotDisabled;
    if (!f.route_identity_empty) return CleanupOnlyRouteQuiescenceFailure::RouteIdentityActive;
    if (!f.list_cleanup_clear) return CleanupOnlyRouteQuiescenceFailure::ListCleanupPending;
    if (!f.pending_patch_empty) return CleanupOnlyRouteQuiescenceFailure::PendingPatchActive;
    if (!f.frozen_patch_empty) return CleanupOnlyRouteQuiescenceFailure::FrozenPatchActive;
    if (!f.failed_patch_empty) return CleanupOnlyRouteQuiescenceFailure::FailedPatchRetained;
    if (!f.active_journal_empty) return CleanupOnlyRouteQuiescenceFailure::ActiveJournal;
    if (!f.stop_retirement_absent) return CleanupOnlyRouteQuiescenceFailure::StopRetirementActive;
    if (!f.deferred_handoff_absent) return CleanupOnlyRouteQuiescenceFailure::DeferredHandoffActive;
    if (!f.frozen_lease_inactive) return CleanupOnlyRouteQuiescenceFailure::FrozenLeaseActive;
    if (!f.playback_absent) return CleanupOnlyRouteQuiescenceFailure::PlaybackPublished;
    if (!f.aggregate_borrowers_absent) return CleanupOnlyRouteQuiescenceFailure::AggregateBorrowerActive;
    if (!f.pause_resume_absent) return CleanupOnlyRouteQuiescenceFailure::PauseResumeActive;
    if (!f.aggregate_cleanup_absent) return CleanupOnlyRouteQuiescenceFailure::AggregateCleanupActive;
    return CleanupOnlyRouteQuiescenceFailure::None;
}

constexpr bool cleanup_only_route_quiescent(
    const CleanupOnlyRouteQuiescenceFacts& f) noexcept
{
    return first_cleanup_only_route_quiescence_failure(f)
        == CleanupOnlyRouteQuiescenceFailure::None;
}

constexpr const char* cleanup_only_route_quiescence_failure_name(
    const CleanupOnlyRouteQuiescenceFailure failure) noexcept
{
    switch (failure) {
    case CleanupOnlyRouteQuiescenceFailure::None: return "none";
    case CleanupOnlyRouteQuiescenceFailure::CleanupGenerationDrift: return "cleanup_generation";
    case CleanupOnlyRouteQuiescenceFailure::UnpublishedSetupActive: return "unpublished_setup";
    case CleanupOnlyRouteQuiescenceFailure::SubstrateBridgeActive: return "substrate_bridge";
    case CleanupOnlyRouteQuiescenceFailure::SelectionReservationActive: return "selection_reservation";
    case CleanupOnlyRouteQuiescenceFailure::RouteNotIdle: return "route_phase";
    case CleanupOnlyRouteQuiescenceFailure::RouteNotDisabled: return "route_disabled";
    case CleanupOnlyRouteQuiescenceFailure::RouteIdentityActive: return "route_identity";
    case CleanupOnlyRouteQuiescenceFailure::ListCleanupPending: return "list_cleanup";
    case CleanupOnlyRouteQuiescenceFailure::PendingPatchActive: return "pending_patch";
    case CleanupOnlyRouteQuiescenceFailure::FrozenPatchActive: return "frozen_patch";
    case CleanupOnlyRouteQuiescenceFailure::FailedPatchRetained: return "failed_patch";
    case CleanupOnlyRouteQuiescenceFailure::ActiveJournal: return "active_journal";
    case CleanupOnlyRouteQuiescenceFailure::StopRetirementActive: return "stop_retirement";
    case CleanupOnlyRouteQuiescenceFailure::DeferredHandoffActive: return "deferred_handoff";
    case CleanupOnlyRouteQuiescenceFailure::FrozenLeaseActive: return "frozen_lease";
    case CleanupOnlyRouteQuiescenceFailure::PlaybackPublished: return "playback_publication";
    case CleanupOnlyRouteQuiescenceFailure::AggregateBorrowerActive: return "aggregate_borrower";
    case CleanupOnlyRouteQuiescenceFailure::PauseResumeActive: return "pause_resume";
    case CleanupOnlyRouteQuiescenceFailure::AggregateCleanupActive: return "aggregate_cleanup";
    }
    return "unknown";
}

} // namespace ff7r::piano::game
