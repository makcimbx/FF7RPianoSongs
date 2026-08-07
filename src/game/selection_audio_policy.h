#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace ff7r::piano::game {

constexpr bool selection_activation_primary_return_exact(
    uintptr_t observed_return_rva, uintptr_t primary_return_rva) noexcept
{
    return primary_return_rva != 0 && observed_return_rva == primary_return_rva;
}

constexpr bool selection_activation_getter_return_exact(
    const uintptr_t observed_return,
    const uintptr_t expected_activation_getter_return) noexcept
{
    return observed_return != 0 && expected_activation_getter_return != 0
        && observed_return == expected_activation_getter_return;
}

struct SelectionActivationQueryFacts final {
    bool native_result = false;
    bool primary_return_site = false;
    bool custom_selection_exact = false;
    bool denial_latched = false;
    bool reservation_acquired = false;
};

constexpr bool selection_activation_query_result(
    const SelectionActivationQueryFacts& f) noexcept
{
    if (!f.native_result) return false;
    if (!f.primary_return_site || !f.custom_selection_exact) return true;
    return !f.denial_latched && f.reservation_acquired;
}

template <typename Original, typename OnNativeFalse, typename OnPrimaryTrue>
bool selection_activation_query_exact_once(
    Original&& original, const bool callback_active,
    const bool primary_return_site, OnNativeFalse&& on_native_false,
    OnPrimaryTrue&& on_primary_true)
{
    const bool native_result = original();
    if (!callback_active) return native_result;
    if (!native_result) {
        on_native_false();
        return false;
    }
    if (!primary_return_site) return true;
    return on_primary_true();
}

enum class SelectionActivationReservationState : uint8_t {
    Empty,
    Reserved,
    Consumed,
    Revoked,
};

enum class SelectionActivationTerminalReason : uint8_t {
    State5Exit,
    ListClose,
    NativeCloseFailure,
    ControllerDestructor,
    Reopen,
};

enum class SelectionActivationTerminalDecision : uint8_t {
    Preserved,
    Revoked,
    Rejected,
};

enum class SelectionActivationTransferState : uint8_t {
    Pending,
    Preserved,
    ClaimedFrozen,
    Consumed,
    AdmissionOwned,
    Retired,
};

struct SelectionActivationTerminalFacts final {
    SelectionActivationTerminalReason reason{};
    SelectionActivationReservationState state{};
    SelectionActivationTransferState transfer_state{};
    bool handoff_confirmed = false;
    bool active_session_exact = false;
    bool widget_uobject_exact = false;
    bool reservation_generation_exact = false;
    bool reservation_session_exact = false;
    bool reservation_context_exact = false;
    bool reservation_bgm_controller_exact = false;
    bool reservation_bgm_identity_exact = false;
    bool reservation_substrate_exact = false;
    bool registry_selection_exact = false;
    bool reservation_selection_exact = false;
    bool rollback_exact = true;
};

enum class SelectionActivationTerminalFailure : uint8_t {
    None,
    ReservationState,
    TransferState,
    PreservationSeam,
    HandoffUnconfirmed,
    ActiveMenuSession,
    WidgetUObject,
    ReservationGeneration,
    ReservationSession,
    ReservationContext,
    BgmController,
    BgmIdentity,
    SubstrateIdentity,
    RegistrySelection,
    ReservationSelection,
    Rollback,
};

constexpr bool selection_activation_terminal_identity_exact(
    const SelectionActivationTerminalFacts& facts) noexcept
{
    return facts.active_session_exact && facts.widget_uobject_exact
        && facts.reservation_generation_exact
        && facts.reservation_session_exact && facts.reservation_context_exact
        && facts.reservation_bgm_controller_exact
        && facts.reservation_bgm_identity_exact
        && facts.reservation_substrate_exact
        && facts.registry_selection_exact && facts.reservation_selection_exact;
}

constexpr SelectionActivationTerminalFailure
selection_activation_terminal_first_failure(
    const SelectionActivationTerminalFacts& facts) noexcept
{
    if (facts.state != SelectionActivationReservationState::Reserved)
        return SelectionActivationTerminalFailure::ReservationState;
    if (facts.transfer_state != SelectionActivationTransferState::Pending)
        return SelectionActivationTerminalFailure::TransferState;
    if (facts.reason != SelectionActivationTerminalReason::ListClose
        && facts.reason != SelectionActivationTerminalReason::State5Exit)
        return SelectionActivationTerminalFailure::PreservationSeam;
    if (!facts.handoff_confirmed)
        return SelectionActivationTerminalFailure::HandoffUnconfirmed;
    if (!facts.active_session_exact)
        return SelectionActivationTerminalFailure::ActiveMenuSession;
    if (!facts.widget_uobject_exact)
        return SelectionActivationTerminalFailure::WidgetUObject;
    if (!facts.reservation_generation_exact)
        return SelectionActivationTerminalFailure::ReservationGeneration;
    if (!facts.reservation_session_exact)
        return SelectionActivationTerminalFailure::ReservationSession;
    if (!facts.reservation_context_exact)
        return SelectionActivationTerminalFailure::ReservationContext;
    if (!facts.reservation_bgm_controller_exact)
        return SelectionActivationTerminalFailure::BgmController;
    if (!facts.reservation_bgm_identity_exact)
        return SelectionActivationTerminalFailure::BgmIdentity;
    if (!facts.reservation_substrate_exact)
        return SelectionActivationTerminalFailure::SubstrateIdentity;
    if (!facts.registry_selection_exact)
        return SelectionActivationTerminalFailure::RegistrySelection;
    if (!facts.reservation_selection_exact)
        return SelectionActivationTerminalFailure::ReservationSelection;
    return SelectionActivationTerminalFailure::None;
}

constexpr SelectionActivationTerminalDecision selection_activation_terminal_decision(
    const SelectionActivationTerminalFacts& facts) noexcept
{
    if (facts.state != SelectionActivationReservationState::Reserved
        || facts.transfer_state != SelectionActivationTransferState::Pending)
        return SelectionActivationTerminalDecision::Rejected;
    if (selection_activation_terminal_first_failure(facts)
        == SelectionActivationTerminalFailure::None)
        return SelectionActivationTerminalDecision::Preserved;
    return facts.rollback_exact ? SelectionActivationTerminalDecision::Revoked
                                : SelectionActivationTerminalDecision::Rejected;
}

enum class SelectionActivationOwnershipEvent : uint8_t {
    Preserve,
    ClaimFrozen,
    Consume,
    TransferToAdmission,
    Rollback,
    Commit,
};

enum class SelectionActivationRollbackPlan : uint8_t {
    None,
    ReservationOnly,
    ReservationAndFrozenProfile,
};

struct SelectionActivationOwnershipTransition final {
    bool accepted = false;
    SelectionActivationTransferState next = SelectionActivationTransferState::Pending;
    SelectionActivationRollbackPlan rollback = SelectionActivationRollbackPlan::None;
};

constexpr SelectionActivationOwnershipTransition
selection_activation_ownership_transition(
    const SelectionActivationTransferState current,
    const SelectionActivationOwnershipEvent event,
    const bool exact_authority) noexcept
{
    if (!exact_authority) return {false, current, SelectionActivationRollbackPlan::None};
    switch (event) {
    case SelectionActivationOwnershipEvent::Preserve:
        return current == SelectionActivationTransferState::Pending
            ? SelectionActivationOwnershipTransition{
                true, SelectionActivationTransferState::Preserved,
                SelectionActivationRollbackPlan::None}
            : SelectionActivationOwnershipTransition{false, current, {}};
    case SelectionActivationOwnershipEvent::ClaimFrozen:
        return current == SelectionActivationTransferState::Preserved
            ? SelectionActivationOwnershipTransition{
                true, SelectionActivationTransferState::ClaimedFrozen,
                SelectionActivationRollbackPlan::None}
            : SelectionActivationOwnershipTransition{false, current, {}};
    case SelectionActivationOwnershipEvent::Consume:
        return current == SelectionActivationTransferState::ClaimedFrozen
            ? SelectionActivationOwnershipTransition{
                true, SelectionActivationTransferState::Consumed,
                SelectionActivationRollbackPlan::None}
            : SelectionActivationOwnershipTransition{false, current, {}};
    case SelectionActivationOwnershipEvent::TransferToAdmission:
        return current == SelectionActivationTransferState::Consumed
            ? SelectionActivationOwnershipTransition{
                true, SelectionActivationTransferState::AdmissionOwned,
                SelectionActivationRollbackPlan::None}
            : SelectionActivationOwnershipTransition{false, current, {}};
    case SelectionActivationOwnershipEvent::Rollback:
        if (current == SelectionActivationTransferState::Pending
            || current == SelectionActivationTransferState::Preserved) {
            return {true, SelectionActivationTransferState::Retired,
                SelectionActivationRollbackPlan::ReservationOnly};
        }
        if (current == SelectionActivationTransferState::ClaimedFrozen
            || current == SelectionActivationTransferState::Consumed
            || current == SelectionActivationTransferState::AdmissionOwned) {
            return {true, SelectionActivationTransferState::Retired,
                SelectionActivationRollbackPlan::ReservationAndFrozenProfile};
        }
        return {false, current, SelectionActivationRollbackPlan::None};
    case SelectionActivationOwnershipEvent::Commit:
        return current == SelectionActivationTransferState::AdmissionOwned
            ? SelectionActivationOwnershipTransition{
                true, SelectionActivationTransferState::Retired,
                SelectionActivationRollbackPlan::None}
            : SelectionActivationOwnershipTransition{false, current, {}};
    }
    return {false, current, SelectionActivationRollbackPlan::None};
}

struct SelectionActivationClaimFacts final {
    SelectionActivationReservationState reservation_state{};
    SelectionActivationTransferState transfer_state{};
    bool handoff_confirmed = false;
    bool no_active_menu_session = false;
    bool reservation_generation_nonzero = false;
    bool reservation_generation_not_wrapped = false;
    bool menu_session_generation_nonzero = false;
    bool menu_session_generation_not_wrapped = false;
    bool selection_generation_nonzero = false;
    bool selection_generation_not_wrapped = false;
    bool registry_selection_exact = false;
    bool bgm_controller_exact = false;
    bool bgm_identity_exact = false;
    bool caller_exact = false;
    bool wrapper_safe_read = false;
    bool wrapper_exact = false;
    bool substrate_exact_if_active = false;
};

enum class SelectionActivationClaimFailure : uint8_t {
    None,
    Caller,
    WrapperSafeRead,
    WrapperEquality,
    ReservationState,
    TransferState,
    HandoffUnconfirmed,
    MenuActive,
    ReservationGeneration,
    ReservationGenerationWrap,
    MenuGeneration,
    MenuGenerationWrap,
    SelectionGeneration,
    SelectionGenerationWrap,
    RegistrySelection,
    BgmController,
    BgmUObjectIdentity,
    SubstrateIdentity,
    FreezeProfile,
    FrozenIdentity,
    OwnershipTransition,
    Exception,
};

constexpr SelectionActivationClaimFailure selection_activation_claim_first_failure(
    const SelectionActivationClaimFacts& facts) noexcept
{
    if (!facts.caller_exact) return SelectionActivationClaimFailure::Caller;
    if (!facts.wrapper_safe_read)
        return SelectionActivationClaimFailure::WrapperSafeRead;
    if (!facts.wrapper_exact)
        return SelectionActivationClaimFailure::WrapperEquality;
    if (facts.reservation_state != SelectionActivationReservationState::Reserved)
        return SelectionActivationClaimFailure::ReservationState;
    if (facts.transfer_state != SelectionActivationTransferState::Preserved)
        return SelectionActivationClaimFailure::TransferState;
    if (!facts.handoff_confirmed)
        return SelectionActivationClaimFailure::HandoffUnconfirmed;
    if (!facts.no_active_menu_session)
        return SelectionActivationClaimFailure::MenuActive;
    if (!facts.reservation_generation_nonzero)
        return SelectionActivationClaimFailure::ReservationGeneration;
    if (!facts.reservation_generation_not_wrapped)
        return SelectionActivationClaimFailure::ReservationGenerationWrap;
    if (!facts.menu_session_generation_nonzero)
        return SelectionActivationClaimFailure::MenuGeneration;
    if (!facts.menu_session_generation_not_wrapped)
        return SelectionActivationClaimFailure::MenuGenerationWrap;
    if (!facts.selection_generation_nonzero)
        return SelectionActivationClaimFailure::SelectionGeneration;
    if (!facts.selection_generation_not_wrapped)
        return SelectionActivationClaimFailure::SelectionGenerationWrap;
    if (!facts.registry_selection_exact)
        return SelectionActivationClaimFailure::RegistrySelection;
    if (!facts.bgm_controller_exact)
        return SelectionActivationClaimFailure::BgmController;
    if (!facts.bgm_identity_exact)
        return SelectionActivationClaimFailure::BgmUObjectIdentity;
    if (!facts.substrate_exact_if_active)
        return SelectionActivationClaimFailure::SubstrateIdentity;
    return SelectionActivationClaimFailure::None;
}

constexpr bool selection_activation_claim_prestate_exact(
    const SelectionActivationClaimFacts& facts) noexcept
{
    return selection_activation_claim_first_failure(facts)
        == SelectionActivationClaimFailure::None;
}

constexpr const char* selection_activation_terminal_reason_name(
    SelectionActivationTerminalReason reason) noexcept
{
    switch (reason) {
    case SelectionActivationTerminalReason::State5Exit: return "state5_exit";
    case SelectionActivationTerminalReason::ListClose: return "list_close";
    case SelectionActivationTerminalReason::NativeCloseFailure:
        return "native_close_failure";
    case SelectionActivationTerminalReason::ControllerDestructor:
        return "controller_destructor";
    case SelectionActivationTerminalReason::Reopen: return "reopen";
    }
    return "unknown";
}

constexpr const char* selection_activation_transfer_state_name(
    SelectionActivationTransferState state) noexcept
{
    switch (state) {
    case SelectionActivationTransferState::Pending: return "pending";
    case SelectionActivationTransferState::Preserved: return "preserved";
    case SelectionActivationTransferState::ClaimedFrozen: return "claimed_frozen";
    case SelectionActivationTransferState::Consumed: return "consumed";
    case SelectionActivationTransferState::AdmissionOwned: return "admission_owned";
    case SelectionActivationTransferState::Retired: return "retired";
    }
    return "unknown";
}

constexpr const char* selection_activation_terminal_failure_name(
    SelectionActivationTerminalFailure failure) noexcept
{
    switch (failure) {
    case SelectionActivationTerminalFailure::None: return "none";
    case SelectionActivationTerminalFailure::ReservationState: return "reservation_state";
    case SelectionActivationTerminalFailure::TransferState: return "transfer_state";
    case SelectionActivationTerminalFailure::PreservationSeam: return "preservation_seam";
    case SelectionActivationTerminalFailure::HandoffUnconfirmed: return "handoff_confirmed";
    case SelectionActivationTerminalFailure::ActiveMenuSession: return "menu_session";
    case SelectionActivationTerminalFailure::WidgetUObject: return "list_widget_uobject";
    case SelectionActivationTerminalFailure::ReservationGeneration: return "reservation_generation";
    case SelectionActivationTerminalFailure::ReservationSession: return "reservation_session";
    case SelectionActivationTerminalFailure::ReservationContext: return "reservation_context";
    case SelectionActivationTerminalFailure::BgmController: return "bgm_controller";
    case SelectionActivationTerminalFailure::BgmIdentity: return "bgm_uobject_identity";
    case SelectionActivationTerminalFailure::SubstrateIdentity: return "substrate_identity";
    case SelectionActivationTerminalFailure::RegistrySelection: return "registry_selection";
    case SelectionActivationTerminalFailure::ReservationSelection: return "reservation_selection";
    case SelectionActivationTerminalFailure::Rollback: return "rollback";
    }
    return "unknown";
}

constexpr const char* selection_activation_claim_failure_name(
    SelectionActivationClaimFailure failure) noexcept
{
    switch (failure) {
    case SelectionActivationClaimFailure::None: return "none";
    case SelectionActivationClaimFailure::Caller: return "caller";
    case SelectionActivationClaimFailure::WrapperSafeRead: return "wrapper_safe_read";
    case SelectionActivationClaimFailure::WrapperEquality: return "wrapper_equality";
    case SelectionActivationClaimFailure::ReservationState: return "reservation_state";
    case SelectionActivationClaimFailure::TransferState: return "transfer_state";
    case SelectionActivationClaimFailure::HandoffUnconfirmed: return "handoff_confirmed";
    case SelectionActivationClaimFailure::MenuActive: return "menu_inactive";
    case SelectionActivationClaimFailure::ReservationGeneration: return "reservation_generation";
    case SelectionActivationClaimFailure::ReservationGenerationWrap: return "reservation_generation_wrap";
    case SelectionActivationClaimFailure::MenuGeneration: return "menu_generation";
    case SelectionActivationClaimFailure::MenuGenerationWrap: return "menu_generation_wrap";
    case SelectionActivationClaimFailure::SelectionGeneration: return "selection_generation";
    case SelectionActivationClaimFailure::SelectionGenerationWrap: return "selection_generation_wrap";
    case SelectionActivationClaimFailure::RegistrySelection: return "registry_selection";
    case SelectionActivationClaimFailure::BgmController: return "bgm_controller";
    case SelectionActivationClaimFailure::BgmUObjectIdentity: return "bgm_uobject_identity";
    case SelectionActivationClaimFailure::SubstrateIdentity: return "substrate_identity";
    case SelectionActivationClaimFailure::FreezeProfile: return "freeze_profile";
    case SelectionActivationClaimFailure::FrozenIdentity: return "frozen_identity";
    case SelectionActivationClaimFailure::OwnershipTransition: return "ownership_transition";
    case SelectionActivationClaimFailure::Exception: return "exception";
    }
    return "unknown";
}

constexpr const char* selection_activation_terminal_decision_name(
    SelectionActivationTerminalDecision decision) noexcept
{
    switch (decision) {
    case SelectionActivationTerminalDecision::Preserved: return "preserved";
    case SelectionActivationTerminalDecision::Revoked: return "revoked";
    case SelectionActivationTerminalDecision::Rejected: return "rejected";
    }
    return "rejected";
}

constexpr const char* selection_activation_list_close_classification(
    const bool native_close_accepted, const bool session_owned,
    const SelectionActivationTerminalDecision decision) noexcept
{
    if (!native_close_accepted) return "native_close_rejected";
    return session_owned
            && decision == SelectionActivationTerminalDecision::Preserved
        ? "activation_confirmed" : "cancel_unowned";
}

struct MenuSessionRevocationFacts {
    SelectionActivationReservationState state{};
    bool expected_selection_valid = false;
    bool active_session_exact = false;
    bool registry_selection_exact = false;
    bool reservation_session_exact = false;
    bool reservation_selection_exact = false;
};

inline bool menu_session_revocation_prestate_exact(
    const MenuSessionRevocationFacts& facts) noexcept {
    if (!facts.expected_selection_valid || !facts.active_session_exact
        || !facts.registry_selection_exact) return false;
    if (facts.state == SelectionActivationReservationState::Empty
        || facts.state == SelectionActivationReservationState::Revoked) return true;
    return facts.state == SelectionActivationReservationState::Reserved
        && facts.reservation_session_exact && facts.reservation_selection_exact;
}

class SelectionActivationReservationMachine final {
public:
    explicit constexpr SelectionActivationReservationMachine(
        const uint64_t initial_generation = 0) noexcept
        : generation_(initial_generation)
    {
    }

    bool reserve() noexcept
    {
        if (state_ == SelectionActivationReservationState::Reserved
            || state_ == SelectionActivationReservationState::Consumed
            || generation_ == UINT64_MAX) {
            return false;
        }
        ++generation_;
        state_ = SelectionActivationReservationState::Reserved;
        return true;
    }

    bool consume(const uint64_t expected_generation) noexcept
    {
        if (state_ != SelectionActivationReservationState::Reserved
            || expected_generation == 0 || generation_ != expected_generation) {
            return false;
        }
        state_ = SelectionActivationReservationState::Consumed;
        return true;
    }

    void revoke() noexcept
    {
        if (state_ == SelectionActivationReservationState::Reserved) {
            state_ = SelectionActivationReservationState::Revoked;
        }
    }

    bool retire_consumed(const uint64_t expected_generation) noexcept
    {
        if (state_ != SelectionActivationReservationState::Consumed
            || expected_generation == 0 || generation_ != expected_generation) {
            return false;
        }
        state_ = SelectionActivationReservationState::Revoked;
        return true;
    }

    SelectionActivationReservationState state() const noexcept { return state_; }
    uint64_t generation() const noexcept { return generation_; }

private:
    SelectionActivationReservationState state_ = SelectionActivationReservationState::Empty;
    uint64_t generation_ = 0;
};

enum class SelectionActivationHandoffFailure : uint8_t {
    None,
    NoPendingReservation,
    AlreadyConfirmed,
    ReservationGenerationDrift,
    ContextDrift,
    PriorSelectionDrift,
    GenerationWrap,
    SameGeneration,
    SkippedGeneration,
    StorageDrift,
    SongDrift,
    ProfileDrift,
    VisibleIndexDrift,
    BaseSlotDrift,
};

struct SelectionActivationHandoffFacts final {
    bool pending_reservation = false;
    bool handoff_unconfirmed = false;
    bool reservation_generation_exact = false;
    bool context_exact = false;
    bool prior_selection_exact = false;
    uint64_t prior_generation = 0;
    uint64_t published_generation = 0;
    bool storage_exact = false;
    bool song_exact = false;
    bool profile_exact = false;
    bool visible_index_exact = false;
    bool base_slot_exact = false;
};

constexpr SelectionActivationHandoffFailure
selection_activation_handoff_first_failure(
    const SelectionActivationHandoffFacts& f) noexcept
{
    if (!f.pending_reservation) {
        return SelectionActivationHandoffFailure::NoPendingReservation;
    }
    if (!f.handoff_unconfirmed) {
        return SelectionActivationHandoffFailure::AlreadyConfirmed;
    }
    if (!f.reservation_generation_exact) {
        return SelectionActivationHandoffFailure::ReservationGenerationDrift;
    }
    if (!f.context_exact) return SelectionActivationHandoffFailure::ContextDrift;
    if (!f.prior_selection_exact) {
        return SelectionActivationHandoffFailure::PriorSelectionDrift;
    }
    if (f.prior_generation == UINT64_MAX) {
        return SelectionActivationHandoffFailure::GenerationWrap;
    }
    if (f.published_generation == f.prior_generation) {
        return SelectionActivationHandoffFailure::SameGeneration;
    }
    if (f.published_generation != f.prior_generation + 1) {
        return SelectionActivationHandoffFailure::SkippedGeneration;
    }
    if (!f.storage_exact) return SelectionActivationHandoffFailure::StorageDrift;
    if (!f.song_exact) return SelectionActivationHandoffFailure::SongDrift;
    if (!f.profile_exact) return SelectionActivationHandoffFailure::ProfileDrift;
    if (!f.visible_index_exact) {
        return SelectionActivationHandoffFailure::VisibleIndexDrift;
    }
    if (!f.base_slot_exact) {
        return SelectionActivationHandoffFailure::BaseSlotDrift;
    }
    return SelectionActivationHandoffFailure::None;
}

struct ChartAudioAdmissionFacts final {
    bool chart_plan_complete = false;
    bool callback_and_exit_lease = false;
    bool selection_exact = false;
    bool sidecar_exact = false;
    bool controller_exact = false;
    bool route_exact_and_idle = false;
    bool frozen_and_cleanup_clear = false;
    bool lifecycle_allows_arm = false;
    bool exit_and_unresolved_clear = false;
};

enum class ChartAudioAdmissionFailure : uint8_t {
    None,
    ChartPlanIncomplete,
    CallbackOrExitLeaseMissing,
    SelectionDrift,
    SidecarDrift,
    ControllerDrift,
    RouteNotExactIdle,
    FrozenOrCleanupActive,
    LifecycleArmRejected,
    ExitOrUnresolvedActive,
};

constexpr ChartAudioAdmissionFailure first_chart_audio_admission_failure(
    const ChartAudioAdmissionFacts& f) noexcept
{
    if (!f.chart_plan_complete) return ChartAudioAdmissionFailure::ChartPlanIncomplete;
    if (!f.callback_and_exit_lease) return ChartAudioAdmissionFailure::CallbackOrExitLeaseMissing;
    if (!f.selection_exact) return ChartAudioAdmissionFailure::SelectionDrift;
    if (!f.sidecar_exact) return ChartAudioAdmissionFailure::SidecarDrift;
    if (!f.controller_exact) return ChartAudioAdmissionFailure::ControllerDrift;
    if (!f.route_exact_and_idle) return ChartAudioAdmissionFailure::RouteNotExactIdle;
    if (!f.frozen_and_cleanup_clear) return ChartAudioAdmissionFailure::FrozenOrCleanupActive;
    if (!f.lifecycle_allows_arm) return ChartAudioAdmissionFailure::LifecycleArmRejected;
    if (!f.exit_and_unresolved_clear) return ChartAudioAdmissionFailure::ExitOrUnresolvedActive;
    return ChartAudioAdmissionFailure::None;
}

constexpr bool chart_audio_admission_exact(
    const ChartAudioAdmissionFacts& f) noexcept
{
    return first_chart_audio_admission_failure(f)
        == ChartAudioAdmissionFailure::None;
}

constexpr bool chart_audio_expand_custom_data_allowed(
    bool chart_writes_active, bool audio_arm_committed,
    bool selection_song_and_lease_exact) noexcept
{
    return chart_writes_active && audio_arm_committed
        && selection_song_and_lease_exact;
}

constexpr bool chart_audio_cancel_complete(
    bool chart_writes_restored, bool selection_guard_released,
    bool frozen_profile_thawed, bool audio_route_uncommitted) noexcept
{
    return chart_writes_restored && selection_guard_released
        && frozen_profile_thawed && audio_route_uncommitted;
}

struct ChartAudioCommittedCancelFacts {
    bool route_and_generation_exact = false;
    bool frozen_lease_active_and_exact = false;
    bool native_arm_not_attempted = false;
    bool unpublished_token_and_guard_exact = false;
    bool lifecycle_epoch_exact = false;
    bool no_native_or_custom_route_ownership = false;
};

constexpr bool chart_audio_committed_cancel_authority_exact(
    const ChartAudioCommittedCancelFacts& f) noexcept
{
    return f.route_and_generation_exact
        && f.frozen_lease_active_and_exact
        && f.native_arm_not_attempted
        && f.unpublished_token_and_guard_exact
        && f.lifecycle_epoch_exact
        && f.no_native_or_custom_route_ownership;
}

enum class ChartAudioAdmissionState : uint8_t {
    Empty,
    Reserved,
    ChartApplied,
    AudioCommitted,
    Cancelled,
};

class ChartAudioAdmissionCoordinator final {
public:
    bool reserve(const ChartAudioAdmissionFacts& facts) noexcept
    {
        if (state_ != ChartAudioAdmissionState::Empty
            || !chart_audio_admission_exact(facts)) {
            state_ = ChartAudioAdmissionState::Cancelled;
            return false;
        }
        state_ = ChartAudioAdmissionState::Reserved;
        return true;
    }

    bool finish_chart_write(const bool succeeded) noexcept
    {
        if (state_ != ChartAudioAdmissionState::Reserved || !succeeded) {
            state_ = ChartAudioAdmissionState::Cancelled;
            return false;
        }
        state_ = ChartAudioAdmissionState::ChartApplied;
        return true;
    }

    bool finish_audio_commit(const bool exact_matching_arm) noexcept
    {
        if (state_ != ChartAudioAdmissionState::ChartApplied
            || !exact_matching_arm) {
            state_ = ChartAudioAdmissionState::Cancelled;
            return false;
        }
        state_ = ChartAudioAdmissionState::AudioCommitted;
        return true;
    }

    void cancel() noexcept { state_ = ChartAudioAdmissionState::Cancelled; }
    bool original_may_consume_custom_chart() const noexcept
    {
        return state_ == ChartAudioAdmissionState::AudioCommitted;
    }
    ChartAudioAdmissionState state() const noexcept { return state_; }

private:
    ChartAudioAdmissionState state_ = ChartAudioAdmissionState::Empty;
};

enum class ChartMutationTransactionOutcome : uint8_t {
    NativePristine,
    CustomCommitted,
    MutationUnresolved,
};

constexpr ChartMutationTransactionOutcome chart_mutation_rollback_outcome(
    const bool rollback_complete, const bool audio_cancel_complete) noexcept
{
    return rollback_complete && audio_cancel_complete
        ? ChartMutationTransactionOutcome::NativePristine
        : ChartMutationTransactionOutcome::MutationUnresolved;
}

constexpr bool chart_expand_original_allowed(
    const ChartMutationTransactionOutcome outcome) noexcept
{
    return outcome != ChartMutationTransactionOutcome::MutationUnresolved;
}

constexpr ChartMutationTransactionOutcome chart_protected_exception_outcome(
    const bool chart_restored, const bool audio_cancelled) noexcept
{
    return chart_restored && audio_cancelled
        ? ChartMutationTransactionOutcome::NativePristine
        : ChartMutationTransactionOutcome::MutationUnresolved;
}

constexpr bool chart_transaction_journal_may_clear(
    const bool chart_pristine, const bool audio_cancellation_required,
    const bool audio_cancellation_committed) noexcept
{
    return chart_pristine
        && (!audio_cancellation_required || audio_cancellation_committed);
}

template <typename Diagnostic>
void chart_diagnostic_best_effort(Diagnostic&& diagnostic) noexcept
{
    try {
        diagnostic();
    } catch (...) {
    }
}

template <typename ReadChunk, typename WriteAll>
bool chart_chunked_restore_exact(
    const uint8_t* original, const uint8_t* patched, const size_t size,
    const bool allow_partial_write,
    ReadChunk&& read_chunk, WriteAll&& write_all) noexcept
{
    static_assert(noexcept(std::declval<ReadChunk&>()(
        size_t{}, static_cast<uint8_t*>(nullptr), size_t{})));
    static_assert(noexcept(std::declval<WriteAll&>()(
        static_cast<const uint8_t*>(nullptr), size_t{})));
    if (!original || !patched || size == 0) return false;

    const auto equals = [&](const uint8_t* expected) noexcept {
        constexpr size_t kChunkSize = 64;
        std::array<uint8_t, kChunkSize> current{};
        for (size_t offset = 0; offset < size; offset += kChunkSize) {
            const size_t count = std::min(kChunkSize, size - offset);
            if (!read_chunk(offset, current.data(), count)
                || std::memcmp(current.data(), expected + offset, count) != 0) {
                return false;
            }
        }
        return true;
    };

    if (!allow_partial_write) {
        if (equals(original)) return true;
        if (!equals(patched)) return false;
    }
    return write_all(original, size) && equals(original);
}

} // namespace ff7r::piano::game
