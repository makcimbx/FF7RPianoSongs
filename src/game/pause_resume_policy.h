#pragma once

#include "game/song_registry.h"

#include <cstdint>
#include <utility>

namespace ff7r::piano::game {

enum class PauseResumeRetirementMismatch : uint8_t {
    None,
    PreChainUnreadable,
    ControllerMismatch,
    SoundMismatch,
    RequestMismatch,
    StateMismatch,
    RetirementReadInvalid,
    MonitorStartRejected,
    CommitStale,
    QuiescentProofMissing,
};

struct PauseResumeRetirementAdmissionFacts final {
    bool session_scoped = false;
    bool pre_chain_readable = false;
    bool controller_matches = false;
    bool sound_matches = false;
    bool request_matches = false;
    bool state_matches = false;
    bool retirement_read_valid = false;
    bool monitor_started = false;
    bool commit_current = false;
};

constexpr PauseResumeRetirementMismatch classify_pause_resume_retirement_mismatch(
    const PauseResumeRetirementAdmissionFacts& facts) noexcept
{
    if (!facts.session_scoped) return PauseResumeRetirementMismatch::None;
    if (!facts.pre_chain_readable) return PauseResumeRetirementMismatch::PreChainUnreadable;
    if (!facts.controller_matches) return PauseResumeRetirementMismatch::ControllerMismatch;
    if (!facts.sound_matches) return PauseResumeRetirementMismatch::SoundMismatch;
    if (!facts.request_matches) return PauseResumeRetirementMismatch::RequestMismatch;
    if (!facts.state_matches) return PauseResumeRetirementMismatch::StateMismatch;
    if (!facts.retirement_read_valid) return PauseResumeRetirementMismatch::RetirementReadInvalid;
    if (!facts.monitor_started) return PauseResumeRetirementMismatch::MonitorStartRejected;
    if (!facts.commit_current) return PauseResumeRetirementMismatch::CommitStale;
    return PauseResumeRetirementMismatch::None;
}

constexpr const char* pause_resume_retirement_mismatch_name(
    PauseResumeRetirementMismatch reason) noexcept
{
    switch (reason) {
    case PauseResumeRetirementMismatch::PreChainUnreadable: return "pre_chain_unreadable";
    case PauseResumeRetirementMismatch::ControllerMismatch: return "controller_mismatch";
    case PauseResumeRetirementMismatch::SoundMismatch: return "sound_mismatch";
    case PauseResumeRetirementMismatch::RequestMismatch: return "request_mismatch";
    case PauseResumeRetirementMismatch::StateMismatch: return "state_mismatch";
    case PauseResumeRetirementMismatch::RetirementReadInvalid: return "retirement_read_invalid";
    case PauseResumeRetirementMismatch::MonitorStartRejected: return "monitor_start_rejected";
    case PauseResumeRetirementMismatch::CommitStale: return "commit_stale";
    case PauseResumeRetirementMismatch::QuiescentProofMissing: return "quiescent_proof_missing";
    case PauseResumeRetirementMismatch::None: return "none";
    }
    return "none";
}

constexpr bool pause_resume_retirement_mismatch_admitted(
    bool already_recorded,
    PauseResumeRetirementMismatch mismatch) noexcept
{
    return !already_recorded && mismatch != PauseResumeRetirementMismatch::None;
}

constexpr bool pause_resume_owner_tick_deferred_admitted(
    bool retirement_diagnostic,
    bool retirement_release,
    bool pending_bank_observation,
    bool initial_hold_probe,
    bool release_authority_probe,
    bool marker_eligible,
    uint32_t batched_marker_count) noexcept
{
    return retirement_diagnostic || retirement_release
        || pending_bank_observation || initial_hold_probe
        || release_authority_probe || marker_eligible
        || batched_marker_count != 0;
}

constexpr bool pause_resume_marker_log_admitted(
    uint32_t prior_count) noexcept
{
    return prior_count < 64;
}

template <typename Emit>
void drain_pause_resume_marker_batch(
    uint32_t count, Emit&& emit)
{
    for (uint32_t index = 0; index < count; ++index) emit(index);
}

// Durable pause/resume ownership policy.  The runtime keeps the detached
// OnMemoryBank lifecycle record authoritative; this policy only coordinates
// the exact native Stop -> Set -> Play resume transaction around it.
enum class PauseResumeBankPhase : uint8_t {
    Idle,
    RetirementCandidate,
    AwaitingResumeOrListReturn,
    ResumeStopObserved,
    OwnerRebound,
    ResumedActive,
    RetirementWaiting,
    ExitPending,
    ReleaseRequested,
    ReleasePending,
    Complete,
    Failed,
};

constexpr uint64_t next_pause_resume_bank_epoch(uint64_t current) noexcept
{
    return current == UINT64_MAX ? 1 : current + 1;
}

enum class PauseResumeSelectionSemantics : uint8_t { Match, Changed, Absent };

struct PauseResumeSelectionContinuity final {
    PauseResumeSelectionSemantics semantics = PauseResumeSelectionSemantics::Absent;
    bool revision_same = false;
};

inline PauseResumeSelectionContinuity classify_pause_resume_selection_continuity(
    const SelectionSnapshot& retained, const SelectionSnapshot& fresh) noexcept
{
    PauseResumeSelectionContinuity result;
    result.revision_same = retained.generation == fresh.generation;
    const bool retained_valid = static_cast<bool>(retained) && retained.storage
        && retained.profile_index >= 0 && retained.visible_index >= 0
        && retained.base_slot >= 0;
    const bool fresh_valid = static_cast<bool>(fresh) && fresh.storage
        && fresh.profile_index >= 0 && fresh.visible_index >= 0
        && fresh.base_slot >= 0;
    if (!retained_valid || !fresh_valid) return result;
    const bool same_owner = !retained.storage.owner_before(fresh.storage)
        && !fresh.storage.owner_before(retained.storage);
    result.semantics = same_owner
        && retained.storage.get() == fresh.storage.get()
        && retained.song == fresh.song && retained.profile == fresh.profile
        && retained.profile_index == fresh.profile_index
        && retained.visible_index == fresh.visible_index
        && retained.base_slot == fresh.base_slot
        ? PauseResumeSelectionSemantics::Match
        : PauseResumeSelectionSemantics::Changed;
    return result;
}

struct PauseResumeBankSetFacts final {
    bool awaiting_resume = false;
    bool owner_tick_active = false;
    bool exact_tick_nonce = false;
    bool outside_play_setup = false;
    bool resume_stop_observed = false;
    bool selection_semantics_match = false;
    bool exact_controller = false;
    bool exact_sound_identity = false;
    bool exact_canonical_owner = false;
    bool exact_lifecycle = false;
    bool no_release_pending = false;
    bool sidecar_ready = false;
    bool runtime_ready = false;
};

constexpr bool pause_resume_bank_set_eligible(
    const PauseResumeBankSetFacts& facts) noexcept
{
    return facts.awaiting_resume && facts.owner_tick_active
        && facts.exact_tick_nonce && facts.outside_play_setup
        && facts.resume_stop_observed && facts.selection_semantics_match
        && facts.exact_controller && facts.exact_sound_identity
        && facts.exact_canonical_owner && facts.exact_lifecycle
        && facts.no_release_pending && facts.sidecar_ready
        && facts.runtime_ready;
}

struct PauseResumeBankPlayFacts final {
    bool same_owner_tick = false;
    bool exact_tick_nonce = false;
    bool outside_play_setup = false;
    bool exact_controller = false;
    bool exact_sound_identity = false;
    bool exact_request = false;
    bool state_ready = false;
    bool custom_owner_present = false;
};

constexpr bool pause_resume_bank_play_eligible(
    const PauseResumeBankPlayFacts& facts) noexcept
{
    return facts.same_owner_tick && facts.exact_tick_nonce
        && facts.outside_play_setup && facts.exact_controller
        && facts.exact_sound_identity && facts.exact_request
        && facts.state_ready && facts.custom_owner_present;
}

enum class PauseResumePatchApplyOutcome : uint8_t {
    AppliedVerified,
    ApplyFailedCanonicalRestored,
    ApplyFailedOwnerUncertain,
};

struct PauseResumeRestorationProof final {
    bool performed = false;
    bool canonical_verified = false;
    constexpr explicit operator bool() const noexcept
    {
        return performed && canonical_verified;
    }
};

template <typename Write, typename VerifyReplacement, typename VerifyOriginal,
    typename Restore>
PauseResumePatchApplyOutcome apply_pause_resume_patch_transaction(
    Write&& write, VerifyReplacement&& verify_replacement,
    VerifyOriginal&& verify_original, Restore&& restore)
{
    if (!std::forward<Write>(write)()) {
        if (std::forward<VerifyOriginal>(verify_original)()) {
            return PauseResumePatchApplyOutcome::ApplyFailedCanonicalRestored;
        }
        return static_cast<bool>(std::forward<Restore>(restore)())
            ? PauseResumePatchApplyOutcome::ApplyFailedCanonicalRestored
            : PauseResumePatchApplyOutcome::ApplyFailedOwnerUncertain;
    }
    if (std::forward<VerifyReplacement>(verify_replacement)()) {
        return PauseResumePatchApplyOutcome::AppliedVerified;
    }
    return static_cast<bool>(std::forward<Restore>(restore)())
        ? PauseResumePatchApplyOutcome::ApplyFailedCanonicalRestored
        : PauseResumePatchApplyOutcome::ApplyFailedOwnerUncertain;
}

enum class PauseResumeNativeForwardResult : uint8_t {
    Succeeded,
    NativeFailedRestored,
    VerificationFailedRestored,
    RestoreFailed,
};

enum class PauseResumeDetourDisposition : uint8_t {
    NotApplicable,
    ContinueSession,
    ForwardAfterVerifiedCanonical,
    OriginalAlreadyForwarded,
    SuppressFailClosed,
};

struct PauseResumeDetourResult final {
    PauseResumeDetourDisposition disposition =
        PauseResumeDetourDisposition::NotApplicable;
    uint8_t native_forward_count = 0;
    bool canonical_verified = false;
};

template <typename Forward, typename Verify, typename Restore>
PauseResumeNativeForwardResult coordinate_pause_resume_native_forward(
    Forward&& forward, Verify&& verify, Restore&& restore,
    bool restore_after_success)
{
    if (!std::forward<Forward>(forward)()) {
        return std::forward<Restore>(restore)()
            ? PauseResumeNativeForwardResult::NativeFailedRestored
            : PauseResumeNativeForwardResult::RestoreFailed;
    }
    if (!std::forward<Verify>(verify)()) {
        return std::forward<Restore>(restore)()
            ? PauseResumeNativeForwardResult::VerificationFailedRestored
            : PauseResumeNativeForwardResult::RestoreFailed;
    }
    if (restore_after_success && !std::forward<Restore>(restore)()) {
        return PauseResumeNativeForwardResult::RestoreFailed;
    }
    return PauseResumeNativeForwardResult::Succeeded;
}

constexpr PauseResumeDetourResult pause_resume_forward_disposition(
    PauseResumeNativeForwardResult result, uint8_t native_calls,
    bool success_canonical_verified) noexcept
{
    if (result != PauseResumeNativeForwardResult::RestoreFailed
        && native_calls != 1) {
        return {PauseResumeDetourDisposition::SuppressFailClosed,
            native_calls, false};
    }
    return {
        result == PauseResumeNativeForwardResult::RestoreFailed
            ? PauseResumeDetourDisposition::SuppressFailClosed
            : PauseResumeDetourDisposition::OriginalAlreadyForwarded,
        native_calls,
        result == PauseResumeNativeForwardResult::Succeeded
            ? success_canonical_verified
            : result != PauseResumeNativeForwardResult::RestoreFailed,
    };
}

template <typename Forward>
bool dispatch_pause_resume_detour(
    const PauseResumeDetourResult& result, Forward&& forward)
{
    switch (result.disposition) {
    case PauseResumeDetourDisposition::NotApplicable: return false;
    case PauseResumeDetourDisposition::ForwardAfterVerifiedCanonical:
        if (result.canonical_verified && result.native_forward_count == 0) {
            (void)std::forward<Forward>(forward)();
        }
        return true;
    case PauseResumeDetourDisposition::ContinueSession:
    case PauseResumeDetourDisposition::OriginalAlreadyForwarded:
    case PauseResumeDetourDisposition::SuppressFailClosed:
        return true;
    }
    return true;
}

struct PauseResumeStopOutcome final {
    PauseResumeBankPhase phase = PauseResumeBankPhase::Failed;
    bool consume_cycle = false;
};

constexpr PauseResumeStopOutcome pause_resume_stop_outcome(
    PauseResumeBankPhase phase, bool in_scope, bool exact_transition) noexcept
{
    if (phase != PauseResumeBankPhase::AwaitingResumeOrListReturn || !in_scope) {
        return {phase, false};
    }
    return {exact_transition ? PauseResumeBankPhase::ResumeStopObserved
                             : PauseResumeBankPhase::Failed,
        true};
}

struct PauseResumeExitOutcome final {
    PauseResumeBankPhase phase = PauseResumeBankPhase::Idle;
    bool restore_owner = false;
    bool release_ready = false;
};

constexpr PauseResumeExitOutcome pause_resume_list_return_outcome(
    PauseResumeBankPhase phase) noexcept
{
    if (phase == PauseResumeBankPhase::OwnerRebound) {
        return {PauseResumeBankPhase::OwnerRebound, true, false};
    }
    if (phase == PauseResumeBankPhase::RetirementCandidate) {
        return {PauseResumeBankPhase::RetirementCandidate, false, false};
    }
    if (phase == PauseResumeBankPhase::AwaitingResumeOrListReturn
        || phase == PauseResumeBankPhase::ResumeStopObserved) {
        return {PauseResumeBankPhase::ExitPending, false, true};
    }
    if (phase == PauseResumeBankPhase::ResumedActive
        || phase == PauseResumeBankPhase::RetirementWaiting) {
        return {PauseResumeBankPhase::ExitPending, false, false};
    }
    return {phase, false, false};
}

struct PauseResumeListRestoreOutcome final {
    PauseResumeBankPhase phase = PauseResumeBankPhase::Failed;
    bool continue_list_return = false;
};

constexpr PauseResumeListRestoreOutcome pause_resume_list_restore_outcome(
    PauseResumeBankPhase authoritative_phase,
    bool restoration_performed_and_verified) noexcept
{
    if (authoritative_phase != PauseResumeBankPhase::OwnerRebound
        || !restoration_performed_and_verified) {
        return {PauseResumeBankPhase::Failed, false};
    }
    return {PauseResumeBankPhase::ExitPending, true};
}

struct PauseResumeInitialHoldOutcome final {
    PauseResumeBankPhase phase = PauseResumeBankPhase::Failed;
    bool resume_ready = false;
    bool release_ready = false;
};

constexpr PauseResumeInitialHoldOutcome pause_resume_initial_hold_outcome(
    bool exact_probe,
    bool canonical_kind2,
    bool custom_kind2,
    bool exit_requested) noexcept
{
    if (!exact_probe || !canonical_kind2 || !custom_kind2) return {};
    return exit_requested
        ? PauseResumeInitialHoldOutcome{
            PauseResumeBankPhase::ReleaseRequested, false, true}
        : PauseResumeInitialHoldOutcome{
            PauseResumeBankPhase::AwaitingResumeOrListReturn, true, false};
}

constexpr bool pause_resume_epoch_matches(
    uint64_t expected_session, uint64_t expected_cycle,
    uint64_t current_session, uint64_t current_cycle) noexcept
{
    return expected_session != 0 && expected_cycle != 0
        && expected_session == current_session
        && expected_cycle == current_cycle;
}

struct PauseResumeRetirementOutcome {
    PauseResumeBankPhase phase = PauseResumeBankPhase::Failed;
    bool advance_cycle = false;
    bool release_ready = false;
};

constexpr PauseResumeRetirementOutcome pause_resume_retirement_outcome(
    PauseResumeBankPhase phase,
    bool exact_session_cycle,
    bool monitor_quiescent,
    bool exit_requested) noexcept
{
    if ((phase != PauseResumeBankPhase::RetirementWaiting
            && phase != PauseResumeBankPhase::ExitPending)
        || !exact_session_cycle || !monitor_quiescent) {
        return {PauseResumeBankPhase::Failed, false, false};
    }
    return exit_requested
        ? PauseResumeRetirementOutcome{
            PauseResumeBankPhase::ReleaseRequested, false, true}
        : PauseResumeRetirementOutcome{
            PauseResumeBankPhase::AwaitingResumeOrListReturn, true, false};
}

struct PauseResumeReleaseAuthorityFacts final {
    bool exact_session_cycle = false;
    bool exact_route_snapshot = false;
    bool route_released = false;
    bool playback_released = false;
    bool cleanup_released = false;
    bool exact_lifecycle = false;
    bool exact_lifecycle_epoch = false;
    bool owner_canonical_or_zero = false;
    bool exact_cleanup_generation = false;
    bool no_release_or_shutdown = false;
};

constexpr bool pause_resume_release_authority_valid(
    const PauseResumeReleaseAuthorityFacts& facts) noexcept
{
    return facts.exact_session_cycle
        && facts.exact_route_snapshot
        && facts.route_released
        && facts.playback_released
        && facts.cleanup_released
        && facts.exact_lifecycle
        && facts.exact_lifecycle_epoch
        && facts.owner_canonical_or_zero
        && facts.exact_cleanup_generation
        && facts.no_release_or_shutdown;
}

struct PauseResumeCoordinatorResult final {
    bool committed = false;
    bool action_exposed = false;
    bool failed = false;
};

struct PauseResumeReleaseProbeIdentity final {
    uint64_t ordinal = 0;
    uint64_t session_epoch = 0;
    uint64_t cycle_epoch = 0;
    PauseResumeBankPhase source_phase = PauseResumeBankPhase::Failed;
    uint64_t lifecycle_state_epoch = 0;
    uint64_t retirement_epoch = 0;
    uint64_t request_identity = 0;

    explicit constexpr operator bool() const noexcept
    {
        return ordinal != 0 && session_epoch != 0 && cycle_epoch != 0
            && lifecycle_state_epoch != 0 && retirement_epoch != 0
            && request_identity != 0
            && (source_phase == PauseResumeBankPhase::RetirementWaiting
                || source_phase == PauseResumeBankPhase::ExitPending);
    }
};

constexpr bool pause_resume_release_probe_matches(
    const PauseResumeReleaseProbeIdentity& expected,
    const PauseResumeReleaseProbeIdentity& current) noexcept
{
    return expected && current
        && expected.ordinal == current.ordinal
        && expected.session_epoch == current.session_epoch
        && expected.cycle_epoch == current.cycle_epoch
        && expected.source_phase == current.source_phase
        && expected.lifecycle_state_epoch == current.lifecycle_state_epoch
        && expected.retirement_epoch == current.retirement_epoch
        && expected.request_identity == current.request_identity;
}

struct PauseResumeReleaseProbeExposure final {
    bool exposed = false;
    uint64_t next_ordinal = 0;
    PauseResumeReleaseProbeIdentity identity;
};

constexpr PauseResumeReleaseProbeExposure expose_pause_resume_release_probe(
    uint64_t current_ordinal,
    const PauseResumeReleaseProbeIdentity& pending,
    PauseResumeBankPhase source_phase,
    uint64_t session_epoch,
    uint64_t cycle_epoch,
    uint64_t lifecycle_state_epoch,
    uint64_t retirement_epoch,
    uint64_t request_identity) noexcept
{
    if (pending || session_epoch == 0 || cycle_epoch == 0
        || lifecycle_state_epoch == 0 || retirement_epoch == 0
        || request_identity == 0
        || (source_phase != PauseResumeBankPhase::RetirementWaiting
            && source_phase != PauseResumeBankPhase::ExitPending)) {
        return {};
    }
    const uint64_t next = next_pause_resume_bank_epoch(current_ordinal);
    return {true, next, {next, session_epoch, cycle_epoch, source_phase,
        lifecycle_state_epoch, retirement_epoch, request_identity}};
}

struct PauseResumeReleaseProbeCurrentFacts final {
    bool exact_pending_identity = false;
    bool pending_evidence_matches = false;
    bool current_evidence_matches = false;
    bool monitor_identity_matches = false;
    bool exact_source_state = false;
    bool lifecycle_epoch_matches = false;
    bool detached_record_matches = false;
};

constexpr bool pause_resume_release_probe_current(
    const PauseResumeReleaseProbeCurrentFacts& facts) noexcept
{
    return facts.exact_pending_identity
        && facts.pending_evidence_matches
        && facts.current_evidence_matches
        && facts.monitor_identity_matches
        && facts.exact_source_state
        && facts.lifecycle_epoch_matches
        && facts.detached_record_matches;
}

constexpr bool pause_resume_retirement_poll_allowed(
    PauseResumeBankPhase phase,
    bool monitor_active,
    const PauseResumeReleaseProbeIdentity& pending) noexcept
{
    return monitor_active && !pending
        && (phase == PauseResumeBankPhase::RetirementWaiting
            || phase == PauseResumeBankPhase::ExitPending);
}

template <typename PersistExit, typename RestoreOwner,
    typename RevalidateTransition, typename ContinueCleanup,
    typename FailClosed>
PauseResumeCoordinatorResult coordinate_pause_resume_list_return(
    PauseResumeBankPhase snapshot_phase,
    PersistExit&& persist_exit,
    RestoreOwner&& restore_owner,
    RevalidateTransition&& revalidate_transition,
    ContinueCleanup&& continue_cleanup,
    FailClosed&& fail_closed)
{
    const auto fail = [&]() {
        fail_closed();
        return PauseResumeCoordinatorResult{false, false, true};
    };
    if (!persist_exit()) return fail();
    const auto outcome = pause_resume_list_return_outcome(snapshot_phase);
    if (outcome.restore_owner) {
        if (!restore_owner()
            || !revalidate_transition(
                PauseResumeBankPhase::OwnerRebound,
                PauseResumeBankPhase::ExitPending)) {
            return fail();
        }
    } else if (!revalidate_transition(snapshot_phase, outcome.phase)) {
        return fail();
    }
    if (!continue_cleanup(outcome.release_ready)) return fail();
    return {true, outcome.release_ready, false};
}

struct PauseResumeHoldCommit final {
    bool committed = false;
    bool resume_ready = false;
    bool release_action_exposed = false;
};

template <typename Revalidate, typename Commit, typename FailClosed>
PauseResumeHoldCommit coordinate_pause_resume_initial_hold(
    bool canonical_kind2,
    bool custom_kind2,
    Revalidate&& revalidate,
    Commit&& commit,
    FailClosed&& fail_closed)
{
    bool exit_requested = false;
    if (!revalidate(exit_requested)) {
        fail_closed();
        return {};
    }
    const auto outcome = pause_resume_initial_hold_outcome(
        true, canonical_kind2, custom_kind2, exit_requested);
    if ((!outcome.resume_ready && !outcome.release_ready)
        || !commit(outcome)) {
        fail_closed();
        return {};
    }
    return {true, outcome.resume_ready, outcome.release_ready};
}

template <typename Snapshot, typename Observe, typename RevalidateClaim,
    typename FailClosed>
PauseResumeCoordinatorResult coordinate_pause_resume_release_authority(
    Snapshot&& snapshot,
    Observe&& observe,
    RevalidateClaim&& revalidate_claim,
    FailClosed&& fail_closed)
{
    if (!snapshot() || !observe()) {
        fail_closed();
        return {false, false, true};
    }
    const PauseResumeCoordinatorResult claimed = revalidate_claim();
    if (!claimed.committed) {
        fail_closed();
        return {false, false, true};
    }
    return claimed;
}

template <typename Execute>
bool execute_pause_resume_deferred_action_once(
    bool action_exposed, Execute&& execute)
{
    return action_exposed && execute();
}

} // namespace ff7r::piano::game
