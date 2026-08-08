#pragma once

#include "game/frozen_profile_lifecycle.h"
#include "game/onmemory_bank_lifecycle.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ff7r::piano::game {

enum class AudioCleanupOperation {
    ListReturn,
    Shutdown,
};

struct AudioCleanupEvidence {
    AudioCleanupOperation operation = AudioCleanupOperation::ListReturn;
    bool hooks_disabled = true;
    bool callbacks_drained = true;
    bool active_journal_empty = true;
    bool pending_journal_empty = true;
    bool failed_journal_empty = true;
    bool auxiliary_journals_restored = true;
    bool immutable_identity_matches = false;
    bool native_clear_verified = false;
    bool route_state_unchanged = false;
    bool native_attempted = false;
    bool ambiguous_route_state = false;
};

struct AudioJournalCommitEvidence {
    bool pending_journal_restored = false;
    bool failed_journal_restored = false;
    bool auxiliary_journals_restored = false;
    bool active_journal_empty = false;
    bool lease_active = false;
    bool lease_identity_matches = false;
};

enum class AudioCleanupDecision {
    Retain,
    CommitVerifiedNoRoute,
    CommitRelease,
};

enum class AudioPatchRestoreDecision {
    AlreadyRestored,
    RestoreOriginal,
    NativeOwned,
    Conflict,
};

struct AudioPatchRestoreField {
    void* object = nullptr;
    uintptr_t offset = 0;
    uint64_t original = 0;
    uint64_t replacement = 0;
    size_t size = 0;
    const char* label = nullptr;
    bool redact_values_in_report = false;
};

enum class AudioPatchRestoreFailureStage : uint8_t {
    None,
    FieldRead,
    ConflictDecision,
    RestoreWrite,
    PostWriteVerify,
};

enum class AudioPatchRestoreFailureCategory : uint8_t {
    Unclassified,
    AlreadyRestored,
    RestoreOriginal,
    PreserveNativeValue,
    Conflict,
};

enum class AudioPatchRollbackFailureStage : uint8_t {
    None,
    RestoreWrite,
    PostWriteVerify,
};

constexpr size_t kAudioPatchRestoreReportLabelCapacity = 24;

struct AudioPatchRestoreFailureReport {
    AudioPatchRestoreFailureStage stage = AudioPatchRestoreFailureStage::None;
    AudioPatchRestoreFailureCategory category =
        AudioPatchRestoreFailureCategory::Unclassified;
    char label[kAudioPatchRestoreReportLabelCapacity]{};
    uintptr_t offset = 0;
    size_t size = 0;
    uint64_t current = 0;
    uint64_t original = 0;
    uint64_t replacement = 0;
    bool current_read = false;
    bool values_redacted = false;
    bool allow_native_changes = false;
    size_t restore_plan_index = 0;
    uint32_t already_restored_count = 0;
    uint32_t restore_original_count = 0;
    uint32_t preserve_native_value_count = 0;
    AudioPatchRollbackFailureStage rollback_stage =
        AudioPatchRollbackFailureStage::None;
    char rollback_label[kAudioPatchRestoreReportLabelCapacity]{};
    uintptr_t rollback_offset = 0;
    size_t rollback_size = 0;
    uint64_t rollback_current = 0;
    uint64_t rollback_original = 0;
    uint64_t rollback_replacement = 0;
    // DURABLE: rollback targets the value inspected before this transaction.
    uint64_t rollback_inspected_value = 0;
    bool rollback_current_read = false;
    bool rollback_values_redacted = false;
    size_t rollback_plan_index = 0;

    bool failed() const noexcept
    {
        return stage != AudioPatchRestoreFailureStage::None;
    }
    bool rollback_failed() const noexcept
    {
        return rollback_stage != AudioPatchRollbackFailureStage::None;
    }
};

using AudioPatchRestoreRead = bool(*)(
    const AudioPatchRestoreField&, uint64_t&, void* context);
using AudioPatchRestoreWrite = bool(*)(
    const AudioPatchRestoreField&, uint64_t value, void* context);
using AudioPatchPreserveNative = void(*)(
    const AudioPatchRestoreField&, uint64_t current, void* context);
struct AudioPatchRestoreAccess {
    AudioPatchRestoreRead read = nullptr;
    AudioPatchRestoreWrite write = nullptr;
    AudioPatchPreserveNative preserve_native = nullptr;
    void* context = nullptr;
};

// Durable, field-exact override for a native-owned value whose restoration
// target is independently qualified. It cannot broaden ordinary patch policy.
struct AudioPatchRestoreExactOverride {
    bool enabled = false;
    bool fresh_native_owner_proof = false;
    bool lookup_signature_valid = false;
    bool release_signature_valid = false;
    bool canonical_kind2_qualified = false;
    bool custom_kind2_qualified = false;
    void* object = nullptr;
    uintptr_t offset = 0;
    size_t size = 0;
    const char* label = nullptr;
    uint64_t expected_journal_original = 0;
    uint64_t expected_current = 0;
    uint64_t restore_value = 0;
    bool applied = false;
};

bool rebase_audio_patch_original_after_exact_override(
    AudioPatchRestoreField& field,
    const AudioPatchRestoreExactOverride& exact_override) noexcept;
bool rebase_unique_audio_patch_original_after_exact_override(
    std::vector<AudioPatchRestoreField>& fields,
    const AudioPatchRestoreExactOverride& exact_override,
    uint32_t& matching_field_count) noexcept;
bool audio_patch_original_rebase_route_successor_exact(
    uint64_t frozen_route_generation,
    uint64_t pre_restore_route_generation,
    uint64_t live_route_generation) noexcept;

struct AudioPatchOriginalRebasePostRestoreSetupFacts final {
    bool selection_song_exact = false;
    bool selection_profile_valid = false;
    bool desired_song_exact = false;
    bool patched_song_cleared = false;
    bool transient_sound_cleared = false;
    bool pending_patch_journal_empty = false;
    bool unpublished_setup_cleared = false;
};

static_assert(std::is_trivially_copyable_v<
    AudioPatchOriginalRebasePostRestoreSetupFacts>);

bool audio_patch_original_rebase_post_restore_setup_exact(
    const AudioPatchOriginalRebasePostRestoreSetupFacts& facts) noexcept;

struct AudioPatchOriginalRebaseContextFacts final {
    bool route_generation_exact = false;
    bool lease_exact = false;
    bool sound_pointer_exact = false;
    bool sound_live_identity_exact = false;
    bool controller_pointer_exact = false;
    bool controller_live_identity_exact = false;
    bool setup_exact = false;
    bool frozen_snapshot_valid = false;
};

bool audio_patch_original_rebase_context_exact(
    const AudioPatchOriginalRebaseContextFacts& facts) noexcept;

struct AudioNativeRouteObservation {
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint64_t request_handle = 0;
    uint8_t state = 0;
    bool valid = false;
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease_identity{};
};

struct AudioBgmRequestHandle {
    uint64_t value = 0;

    bool empty() const noexcept { return value == 0; }
    uint8_t type() const noexcept { return static_cast<uint8_t>(value); }
    uint16_t pool_index() const noexcept
    {
        return static_cast<uint16_t>((value >> 16) & 0xffffu);
    }
    uint32_t generation() const noexcept
    {
        return static_cast<uint32_t>(value >> 32);
    }
    bool valid_bgm_request() const noexcept { return value != 0 && type() == 8; }
};

enum class AudioStopRetirementPhase {
    None,
    Waiting,
    Quiescent,
    TimedOut,
    Invalid,
};

enum class AudioStopRetirementOrigin : uint8_t {
    None,
    OrdinaryStop,
    ExactNaturalCompletion,
};

struct AudioStopRetirementState;
struct AudioStopRetirementObservation;
struct AudioNaturalCompletionRetirementFacts;
struct AudioNaturalCompletionRetirementStart;

bool begin_audio_stop_retirement_monitor(
    AudioStopRetirementState& state,
    const AudioStopRetirementObservation& observation) noexcept;
AudioNaturalCompletionRetirementStart
start_audio_natural_completion_retirement(
    const AudioNaturalCompletionRetirementFacts& facts,
    const AudioStopRetirementObservation& observation,
    const OnMemoryBankRetiredBackingEvidence& backing_evidence) noexcept;

struct AudioStopRetirementObservation {
    bool valid = false;
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease_identity{};
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    void* custom_sound = nullptr;
    uint64_t request_handle = 0;
    bool request_handle_retired = false;
    uint32_t retired_count = 0;
    void* current_sound = nullptr;
    uint64_t current_request_handle = 0;
    uint8_t current_state = 0xff;
};

struct AudioStopRetirementState {
    AudioStopRetirementPhase phase = AudioStopRetirementPhase::None;
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease_identity{};
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    void* custom_sound = nullptr;
    uint64_t request_handle = 0;
    uint32_t poll_count = 0;

    bool active() const noexcept { return phase == AudioStopRetirementPhase::Waiting; }
    AudioStopRetirementOrigin origin() const noexcept { return origin_; }

private:
    AudioStopRetirementOrigin origin_ = AudioStopRetirementOrigin::None;

    friend bool begin_audio_stop_retirement_monitor(
        AudioStopRetirementState& state,
        const AudioStopRetirementObservation& observation) noexcept;
    friend AudioNaturalCompletionRetirementStart
    start_audio_natural_completion_retirement(
        const AudioNaturalCompletionRetirementFacts& facts,
        const AudioStopRetirementObservation& observation,
        const OnMemoryBankRetiredBackingEvidence& backing_evidence) noexcept;
};

enum class AudioNaturalCompletionRetirementFailure : uint8_t {
    None,
    RouteNotCustomOwned,
    RouteLeaseInvalid,
    RouteControllerMismatch,
    PreStopChainUnreadable,
    PreStopSoundNotNull,
    PreStopRequestNotZero,
    PreStopStateNotIdle,
    DetachedMissing,
    DetachedPhaseNotRestoreApplied,
    DetachedRouteNotPredecessor,
    DetachedCleanupGenerationMismatch,
    DetachedSoundMismatch,
    DetachedRequestUnavailable,
    DetachedRequestMismatch,
    DetachedTokensInvalid,
    DetachedOwnerNotRestored,
    FrozenPatchInvalid,
    FrozenRouteNotPredecessor,
    FrozenLeaseMismatch,
    FrozenSoundMismatch,
    FrozenOwnerPatchMismatch,
    JournalsNotRestored,
    PauseResumeConflict,
    SetupConflict,
    RegistryPlaybackMismatch,
    RegistryCleanupConflict,
    AggregateSnapshotUnavailable,
    ActiveAggregateBorrower,
    CanonicalProofSnapshotUnavailable,
    CanonicalProofActive,
    ExistingRetirement,
    InvalidObservation,
    MonitorStartFailed,
    RouteCommitMismatch,
    DetachedCommitMismatch,
    FrozenCommitMismatch,
    OwnershipCommitMismatch,
    RegistryCommitMismatch,
};

struct AudioNaturalCompletionRetirementFacts final {
    bool route_custom_owned = false;
    bool route_lease_valid = false;
    bool route_controller_exact = false;
    bool pre_stop_chain_read = false;
    bool pre_stop_sound_null = false;
    bool pre_stop_request_zero = false;
    bool pre_stop_state_idle = false;
    bool detached_present = false;
    bool detached_restore_applied = false;
    bool detached_route_predecessor = false;
    bool detached_cleanup_generation_exact = false;
    bool detached_sound_exact = false;
    bool detached_request_available = false;
    bool detached_request_exact = false;
    bool detached_tokens_valid = false;
    bool detached_owner_restored = false;
    bool frozen_patch_valid = false;
    bool frozen_route_predecessor = false;
    bool frozen_lease_exact = false;
    bool frozen_sound_exact = false;
    bool frozen_owner_patch_exact = false;
    bool journals_restored = false;
    bool pause_resume_clear = false;
    bool setup_clear = false;
    bool registry_playback_exact = false;
    bool registry_cleanup_clear_or_exact = false;
    bool aggregate_snapshot_available = false;
    bool active_aggregate_borrower = false;
    bool canonical_proof_snapshot_available = false;
    bool canonical_proof_active = false;
    AudioStopRetirementPhase existing_retirement_phase =
        AudioStopRetirementPhase::None;
    bool route_commit_exact = false;
    bool detached_commit_exact = false;
    bool frozen_commit_exact = false;
    bool ownership_commit_exact = false;
    bool registry_commit_exact = false;
};

struct AudioNaturalCompletionRetirementStart final {
    AudioNaturalCompletionRetirementFailure failure =
        AudioNaturalCompletionRetirementFailure::None;
    bool admitted = false;
    bool presence_pending = false;
    AudioStopRetirementObservation monitor_observation{};
    AudioStopRetirementState monitor{};
};

AudioNaturalCompletionRetirementFailure
classify_audio_natural_completion_retirement(
    const AudioNaturalCompletionRetirementFacts& facts) noexcept;
AudioNaturalCompletionRetirementStart
start_audio_natural_completion_retirement(
    const AudioNaturalCompletionRetirementFacts& facts,
    const AudioStopRetirementObservation& observation,
    const OnMemoryBankRetiredBackingEvidence& backing_evidence) noexcept;
AudioNaturalCompletionRetirementFailure
classify_audio_natural_completion_retirement_commit(
    const AudioNaturalCompletionRetirementFacts& facts) noexcept;
bool audio_retirement_frozen_generation_proven(
    uint64_t frozen_route_generation,
    const AudioStopRetirementState& retirement,
    uint64_t live_route_generation) noexcept;

enum class AudioRetirementCleanupFailure : uint8_t {
    None,
    InvalidOrigin,
    RetirementNotQuiescent,
    ImmutableIdentityMismatch,
    BackingNotProven,
    LiveRouteNotSuccessor,
    JournalsNotRestored,
    FrozenFieldsNotRestored,
    NativeRouteNotProven,
    CleanupPolicyRejected,
};

struct AudioRetirementCleanupFailureFacts final {
    bool origin_valid = false;
    bool retirement_quiescent = false;
    bool immutable_identity_matches = false;
    bool backing_proven = false;
    bool live_route_successor = false;
    bool journals_restored = false;
    bool frozen_fields_restored = false;
    bool native_route_proven = false;
    bool cleanup_policy_accepted = false;
};

enum class AudioRetirementRouteAuthority : uint8_t {
    DirectSuccessor,
    TerminalAggregateHandoff,
};

struct AudioRetirementRouteAuthorityFacts final {
    AudioRetirementRouteAuthority authority =
        AudioRetirementRouteAuthority::DirectSuccessor;
    bool terminal_handoff_authenticated = false;
    uint64_t monitor_route_generation = 0;
    uint64_t live_route_generation = 0;
    uint64_t terminal_leaf_route_generation = 0;
    uint64_t terminal_leaf_request_handle = 0;
    uint64_t frozen_route_generation = 0;
    uint64_t monitor_request_handle = 0;
};

bool audio_retirement_route_authority_proven(
    const AudioRetirementRouteAuthorityFacts& facts) noexcept;
bool audio_retirement_owned_request_identity_exact(
    uint64_t retirement_request_handle,
    uint64_t route_owned_request_handle) noexcept;

struct AudioRetirementOwnedRouteIdentityFacts final {
    void* expected_slot = nullptr;
    void* expected_bgm = nullptr;
    void* expected_sound = nullptr;
    uint64_t expected_request = 0;
    void* owned_slot = nullptr;
    void* owned_bgm = nullptr;
    void* owned_sound = nullptr;
    uint64_t owned_request = 0;
};

bool audio_retirement_owned_route_identity_exact(
    const AudioRetirementOwnedRouteIdentityFacts& facts) noexcept;

static_assert(std::is_trivially_copyable_v<
    AudioRetirementOwnedRouteIdentityFacts>);

struct AudioRetirementOnMemoryRequestProjectionFacts final {
    uint64_t retirement_request_handle = 0;
    uint64_t route_owned_request_handle = 0;
    AudioRetirementRouteAuthorityFacts route_authority{};
};

struct AudioRetirementOnMemoryRequestProjection final {
    bool accepted = false;
    uint64_t retired_request_handle = 0;
};

AudioRetirementOnMemoryRequestProjection
project_audio_retirement_onmemory_request(
    const AudioRetirementOnMemoryRequestProjectionFacts& facts) noexcept;

static_assert(std::is_trivially_copyable_v<
    AudioRetirementOnMemoryRequestProjectionFacts>);
static_assert(std::is_trivially_copyable_v<
    AudioRetirementOnMemoryRequestProjection>);

AudioRetirementCleanupFailure first_audio_retirement_cleanup_failure(
    const AudioRetirementCleanupFailureFacts& facts) noexcept;

struct AudioRetirementCleanupEvidence {
    AudioStopRetirementState retirement{};
    AudioStopRetirementObservation retirement_observation{};
    AudioNativeRouteObservation current_route{};
    bool immutable_identity_matches = false;
    bool all_journals_restored = false;
    bool custom_sound_fields_restored = false;
    bool current_native_identity_matches = false;
    AudioRetirementRouteAuthorityFacts route_authority{};
};

enum class AudioNativeHandoffDecision {
    OwnedCustomRoute,
    NativeReplacementProven,
    ReleaseBeforeNativePlay,
    AmbiguousRetainAndBlock,
};

enum class AudioDeferredNativeHandoffPhase {
    None,
    SetCaptured,
    CustomCleared,
    NativeSetApplied,
    NativePlayForwarded,
    RetainedFailure,
};

enum class AudioDeferredNativeHandoffAction {
    Retain,
    ApplyNativeSet,
    ForwardNativePlay,
    ForwardNativePlayRetainingFailure,
};

struct AudioDeferredNativeHandoffState {
    AudioDeferredNativeHandoffPhase phase = AudioDeferredNativeHandoffPhase::None;
    AudioRouteLeaseIdentity lease_identity{};
    uint64_t route_generation = 0;
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    void* requested_sound = nullptr;
    uint64_t retired_custom_request_handle = 0;
    uint64_t native_request_handle = 0;
    // Monotonic record that `call_bgm_slot_play_original` returned for
    // `requested_sound`.  The phase cannot carry this fact.  `RetainedFailure`
    // is reached both *before* any forward (a clear or Set the mod could not
    // verify, in `bgm_slot_set`) and *after* a successful recovery forward, and
    // the recovery branch has no later phase to advance into.  Only the
    // forwarding event itself is evidence that the native's own BGM Play
    // reached the game.  Cleared solely by resetting the whole record.
    bool native_play_forwarded = false;

    bool active() const noexcept
    {
        return phase != AudioDeferredNativeHandoffPhase::None;
    }

    // The deferred obligation is exactly "the native's replacement Play was not
    // swallowed by the mod".  It is met when no handoff exists at all, or when
    // this handoff forwarded that Play.  A handoff that has not forwarded yet
    // still blocks retirement, unchanged.
    //
    // Deliberately not conditioned on any post-Play observation: an obligation
    // to *make a call* is discharged by making it.  Re-deriving it from
    // observed slot state would let one unverifiable read strand the record
    // permanently, which is the defect this replaces.
    bool native_play_obligation_met() const noexcept
    {
        return !active() || native_play_forwarded;
    }
};

class AudioPatchJournalState {
public:
    bool begin(FrozenProfileLeaseState& lease, AudioRouteLeaseIdentity identity) noexcept;
    void finish() noexcept;
    bool empty() const noexcept { return active_operations_ == 0; }

private:
    uint32_t active_operations_ = 0;
};

AudioCleanupDecision evaluate_audio_cleanup(const AudioCleanupEvidence& evidence) noexcept;
bool audio_journal_commit_ready(const AudioJournalCommitEvidence& evidence) noexcept;
AudioPatchRestoreDecision evaluate_audio_patch_restore(
    uint64_t current, uint64_t original, uint64_t replacement,
    bool allow_native_changes) noexcept;
bool restore_audio_patches_reverse(
    const std::vector<AudioPatchRestoreField>& patches,
    bool allow_native_changes,
    const AudioPatchRestoreAccess& access,
    AudioPatchRestoreFailureReport* report = nullptr,
    AudioPatchRestoreExactOverride* exact_override = nullptr);
AudioNativeHandoffDecision evaluate_audio_native_handoff(
    const AudioNativeRouteObservation& owned,
    const AudioNativeRouteObservation& current,
    void* requested_sound,
    uint64_t expected_generation = 0) noexcept;
bool native_route_replacement_proven(
    const AudioNativeRouteObservation& owned,
    const AudioNativeRouteObservation& current,
    void* requested_sound,
    uint64_t expected_generation = 0) noexcept;
bool native_route_release_proven(
    const AudioNativeRouteObservation& owned,
    const AudioNativeRouteObservation& cleared,
    uint64_t expected_generation) noexcept;
bool begin_deferred_native_handoff(
    AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& owned,
    void* controller,
    void* requested_sound,
    uint64_t expected_generation) noexcept;
bool record_deferred_native_clear(
    AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& owned,
    const AudioNativeRouteObservation& cleared) noexcept;
bool record_deferred_native_set(
    AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& current) noexcept;
bool deferred_native_play_ready(
    const AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& current) noexcept;
AudioDeferredNativeHandoffAction evaluate_deferred_native_handoff_action(
    const AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& current) noexcept;
bool bypass_audio_native_detour(uint32_t native_replay_depth) noexcept;
bool begin_audio_stop_retirement_monitor(
    AudioStopRetirementState& state,
    const AudioStopRetirementObservation& observation) noexcept;
AudioStopRetirementPhase advance_audio_stop_retirement_monitor(
    AudioStopRetirementState& state,
    const AudioStopRetirementObservation& observation,
    uint32_t max_polls) noexcept;
bool audio_retirement_cleanup_proven(
    const AudioRetirementCleanupEvidence& evidence) noexcept;
bool record_deferred_native_play(
    AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& current) noexcept;
void record_deferred_native_play_forwarded(
    AudioDeferredNativeHandoffState& handoff) noexcept;
AudioRouteCleanupResult apply_audio_cleanup(
    FrozenProfileLeaseState& lease,
    AudioRouteLeaseIdentity identity,
    const AudioCleanupEvidence& evidence) noexcept;

} // namespace ff7r::piano::game
