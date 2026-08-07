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

template <typename Action>
class DeferredNoexceptAction {
public:
    explicit DeferredNoexceptAction(Action action)
        : action_(std::move(action))
    {
    }

    DeferredNoexceptAction(const DeferredNoexceptAction&) = delete;
    DeferredNoexceptAction& operator=(const DeferredNoexceptAction&) = delete;

    ~DeferredNoexceptAction() noexcept
    {
        if (!eligible_) return;
        try {
            action_();
        } catch (...) {
        }
    }

    void make_eligible() noexcept { eligible_ = true; }

private:
    Action action_;
    bool eligible_ = false;
};

template <typename Action>
DeferredNoexceptAction<Action> make_deferred_noexcept_action(Action action)
{
    return DeferredNoexceptAction<Action>(std::move(action));
}

enum class UObjectIdentityPrefilterResult {
    NotEvaluated,
    Passed,
    ExpectedLiveHandleValidationFailed,
    CurrentIdentityReadFailed,
    CurrentLiveHandleChanged,
    ObjectClassChanged,
    NameComparisonIndexChanged,
    NameNumberChanged,
    OuterChanged,
};

struct UObjectIdentityStopDiagnosticObservation {
    UObjectIdentityPrefilterResult prefilter_result =
        UObjectIdentityPrefilterResult::NotEvaluated;
    bool observed_live_capture_attempted = false;
    uobject_locator_core::UObjectLiveHandleCaptureResult observed_live_capture_result =
        uobject_locator_core::UObjectLiveHandleCaptureResult::ResolverOrViewUnavailable;
};

constexpr bool uobject_identity_prefilter_passed(
    const UObjectIdentityPrefilterResult result) noexcept
{
    return result == UObjectIdentityPrefilterResult::Passed;
}

template <typename ValidateExpected, typename ReadCurrent, typename CompareCurrent>
UObjectIdentityPrefilterResult evaluate_uobject_identity_prefilter(
    const bool requires_live_handle,
    ValidateExpected&& validate_expected,
    ReadCurrent&& read_current,
    CompareCurrent&& compare_current)
{
    if (requires_live_handle && !validate_expected()) {
        return UObjectIdentityPrefilterResult::ExpectedLiveHandleValidationFailed;
    }
    if (!read_current()) {
        return UObjectIdentityPrefilterResult::CurrentIdentityReadFailed;
    }
    return compare_current();
}

enum class AudioArmPublicationProof {
    NativeHandoffBeforeCommit,
    RequestValidationFailed,
    RequestException,
    RebuildFailed,
    WaitingForPlaySetup,
    RequestReturnedNoClaim,
    PlaySetupClaimed,
    ControllerRebuildClaimed,
};

constexpr bool audio_arm_proof_allows_playback(
    const AudioArmPublicationProof proof) noexcept
{
    return proof == AudioArmPublicationProof::PlaySetupClaimed
        || proof == AudioArmPublicationProof::ControllerRebuildClaimed;
}

inline bool custom_route_claim_is_complete(const CustomContextToken& token) noexcept
{
    return token.valid() && token.controller && token.slot && token.bgm
        && token.sound && token.request_handle != 0;
}

enum class PrivateControllerSetupStage {
    AwaitingStop,
    StopObserved,
    SetCaptured,
    Patched,
    SetForwarded,
    Claimed,
};

enum class AudioArmPublicationPath {
    Reject,
    Generic,
    GuardedPlaySetupClaim,
    PrivateControllerClaim,
};

constexpr AudioArmPublicationPath classify_audio_arm_publication(
    const AudioArmPublicationProof proof,
    const PrivateControllerSetupStage stage) noexcept
{
    if (!audio_arm_proof_allows_playback(proof)) {
        return AudioArmPublicationPath::Reject;
    }
    if (stage == PrivateControllerSetupStage::Claimed) {
        return AudioArmPublicationPath::PrivateControllerClaim;
    }
    if (proof == AudioArmPublicationProof::PlaySetupClaimed
        && stage == PrivateControllerSetupStage::StopObserved) {
        return AudioArmPublicationPath::GuardedPlaySetupClaim;
    }
    return stage == PrivateControllerSetupStage::AwaitingStop
        ? AudioArmPublicationPath::Generic
        : AudioArmPublicationPath::Reject;
}

enum class DiagnosticFact {
    NotEvaluated,
    Rejected,
    Accepted,
};

constexpr DiagnosticFact diagnostic_fact(const bool accepted) noexcept
{
    return accepted ? DiagnosticFact::Accepted : DiagnosticFact::Rejected;
}

enum class PrivateControllerStopObservationResult {
    NotEvaluated,
    ContextInvalid,
    TokenChanged,
    StageChanged,
    RequiredContextMissing,
    ControllerHandleInvalid,
    SoundHandleInvalid,
    Rejected,
    Accepted,
};

struct PrivateControllerStopObservationEvidence {
    DiagnosticFact context_valid = DiagnosticFact::NotEvaluated;
    DiagnosticFact token_unchanged = DiagnosticFact::NotEvaluated;
    DiagnosticFact stage_awaiting_stop = DiagnosticFact::NotEvaluated;
    DiagnosticFact required_context_present = DiagnosticFact::NotEvaluated;
    DiagnosticFact controller_handle_valid = DiagnosticFact::NotEvaluated;
    DiagnosticFact sound_handle_valid = DiagnosticFact::NotEvaluated;
};

constexpr PrivateControllerStopObservationResult classify_private_controller_stop_observation(
    const PrivateControllerStopObservationEvidence& evidence) noexcept
{
    if (evidence.context_valid != DiagnosticFact::Accepted) {
        return PrivateControllerStopObservationResult::ContextInvalid;
    }
    if (evidence.token_unchanged != DiagnosticFact::Accepted) {
        return PrivateControllerStopObservationResult::TokenChanged;
    }
    if (evidence.stage_awaiting_stop != DiagnosticFact::Accepted) {
        return PrivateControllerStopObservationResult::StageChanged;
    }
    if (evidence.required_context_present != DiagnosticFact::Accepted) {
        return PrivateControllerStopObservationResult::RequiredContextMissing;
    }
    if (evidence.controller_handle_valid != DiagnosticFact::Accepted) {
        return PrivateControllerStopObservationResult::ControllerHandleInvalid;
    }
    if (evidence.sound_handle_valid != DiagnosticFact::Accepted) {
        return PrivateControllerStopObservationResult::SoundHandleInvalid;
    }
    return PrivateControllerStopObservationResult::Accepted;
}

struct PrivateControllerStopObservationDiagnostic {
    PrivateControllerStopObservationEvidence evidence;
    PrivateControllerStopObservationResult result =
        PrivateControllerStopObservationResult::NotEvaluated;
};

enum class PrivateControllerStopDiagnostic {
    Accepted,
    ControllerIdentityPrefilterRejected,
    ControllerIdentityChainIncomplete,
    SelectionGuardRejected,
    RouteGenerationChanged,
    LeaseChanged,
    PhaseNotArmed,
    ControllerIdentityChanged,
    ObserveContextInvalid,
    ObserveTokenChanged,
    ObserveStageChanged,
    ObserveRequiredContextMissing,
    ObserveControllerHandleInvalid,
    ObserveSoundHandleInvalid,
    ObserveRejected,
};

struct PrivateControllerStopDiagnosticEvidence {
    DiagnosticFact selection_guard_acquired = DiagnosticFact::NotEvaluated;
    DiagnosticFact route_generation_unchanged = DiagnosticFact::NotEvaluated;
    DiagnosticFact lease_unchanged = DiagnosticFact::NotEvaluated;
    DiagnosticFact phase_armed = DiagnosticFact::NotEvaluated;
    DiagnosticFact controller_identity_unchanged = DiagnosticFact::NotEvaluated;
    PrivateControllerStopObservationResult observe_result =
        PrivateControllerStopObservationResult::NotEvaluated;
    PrivateControllerStopObservationEvidence observe_evidence;
    UObjectIdentityStopDiagnosticObservation identity_observation;
};

constexpr PrivateControllerStopDiagnostic classify_private_controller_stop_diagnostic(
    const PrivateControllerStopDiagnosticEvidence& evidence) noexcept
{
    if (evidence.identity_observation.prefilter_result
            != UObjectIdentityPrefilterResult::NotEvaluated
        && !uobject_identity_prefilter_passed(
            evidence.identity_observation.prefilter_result)) {
        return PrivateControllerStopDiagnostic::ControllerIdentityPrefilterRejected;
    }
    if (evidence.selection_guard_acquired == DiagnosticFact::Rejected) {
        return PrivateControllerStopDiagnostic::SelectionGuardRejected;
    }
    if (evidence.selection_guard_acquired == DiagnosticFact::NotEvaluated) {
        return PrivateControllerStopDiagnostic::ControllerIdentityChainIncomplete;
    }
    if (evidence.route_generation_unchanged != DiagnosticFact::Accepted) {
        return PrivateControllerStopDiagnostic::RouteGenerationChanged;
    }
    if (evidence.lease_unchanged != DiagnosticFact::Accepted) {
        return PrivateControllerStopDiagnostic::LeaseChanged;
    }
    if (evidence.phase_armed != DiagnosticFact::Accepted) {
        return PrivateControllerStopDiagnostic::PhaseNotArmed;
    }
    if (evidence.controller_identity_unchanged != DiagnosticFact::Accepted) {
        return PrivateControllerStopDiagnostic::ControllerIdentityChanged;
    }
    switch (evidence.observe_result) {
    case PrivateControllerStopObservationResult::Accepted:
        return PrivateControllerStopDiagnostic::Accepted;
    case PrivateControllerStopObservationResult::ContextInvalid:
        return PrivateControllerStopDiagnostic::ObserveContextInvalid;
    case PrivateControllerStopObservationResult::TokenChanged:
        return PrivateControllerStopDiagnostic::ObserveTokenChanged;
    case PrivateControllerStopObservationResult::StageChanged:
        return PrivateControllerStopDiagnostic::ObserveStageChanged;
    case PrivateControllerStopObservationResult::RequiredContextMissing:
        return PrivateControllerStopDiagnostic::ObserveRequiredContextMissing;
    case PrivateControllerStopObservationResult::ControllerHandleInvalid:
        return PrivateControllerStopDiagnostic::ObserveControllerHandleInvalid;
    case PrivateControllerStopObservationResult::SoundHandleInvalid:
        return PrivateControllerStopDiagnostic::ObserveSoundHandleInvalid;
    case PrivateControllerStopObservationResult::NotEvaluated:
    case PrivateControllerStopObservationResult::Rejected:
        return PrivateControllerStopDiagnostic::ObserveRejected;
    }
    return PrivateControllerStopDiagnostic::ObserveRejected;
}

struct PrivateControllerSetupEligibility {
    bool playback_absent = false;
    bool cleanup_absent = false;
    bool route_unowned = false;
    bool retirement_absent = false;
    bool selection_matches = false;
    bool route_matches = false;
    bool operation_expected = false;
};

constexpr bool private_controller_setup_eligible(
    const PrivateControllerSetupEligibility& evidence) noexcept
{
    return evidence.playback_absent && evidence.cleanup_absent
        && evidence.route_unowned && evidence.retirement_absent
        && evidence.selection_matches && evidence.route_matches
        && evidence.operation_expected;
}

struct PlaySetupQualificationRevalidation {
    bool sound_identity_unchanged = false;
    bool owner_unchanged = false;
    bool registry_generation_unchanged = false;
    bool route_generation_unchanged = false;
    bool lease_unchanged = false;
    bool song_unchanged = false;
    bool controller_unchanged = false;
    bool phase_armed = false;
    bool setup_token_unchanged = false;
    bool sidecar_unchanged = false;
};

struct PlaySetupQualificationEntryFacts {
    bool sound_identity_read_succeeded = false;
    bool sound_live_capture_succeeded = false;
    bool sound_index_valid = false;
    bool sound_serial_valid = false;
    bool owner_read_succeeded = false;
    bool route_matches = false;
    bool setup_matches = false;
    bool sidecar_matches = false;
};

enum class PlaySetupQualificationEntryFailure : uint8_t {
    None,
    SoundIdentityReadFailed,
    SoundLiveCaptureFailed,
    SoundIndexInvalid,
    SoundSerialInvalid,
    OwnerFieldUnreadable,
    RouteMismatch,
    SetupMismatch,
    SidecarMismatch,
};

constexpr PlaySetupQualificationEntryFailure
first_play_setup_qualification_entry_failure(
    const PlaySetupQualificationEntryFacts& facts) noexcept
{
    if (!facts.sound_identity_read_succeeded) return PlaySetupQualificationEntryFailure::SoundIdentityReadFailed;
    if (!facts.sound_live_capture_succeeded) return PlaySetupQualificationEntryFailure::SoundLiveCaptureFailed;
    if (!facts.sound_index_valid) return PlaySetupQualificationEntryFailure::SoundIndexInvalid;
    if (!facts.sound_serial_valid) return PlaySetupQualificationEntryFailure::SoundSerialInvalid;
    if (!facts.owner_read_succeeded) return PlaySetupQualificationEntryFailure::OwnerFieldUnreadable;
    if (!facts.route_matches) return PlaySetupQualificationEntryFailure::RouteMismatch;
    if (!facts.setup_matches) return PlaySetupQualificationEntryFailure::SetupMismatch;
    if (!facts.sidecar_matches) return PlaySetupQualificationEntryFailure::SidecarMismatch;
    return PlaySetupQualificationEntryFailure::None;
}

enum class PlaySetupQualificationRevalidationFailure : uint8_t {
    None,
    SoundIdentityChanged,
    OwnerChanged,
    RegistryGenerationChanged,
    RouteGenerationChanged,
    LeaseChanged,
    SongChanged,
    ControllerChanged,
    PhaseNotArmed,
    SetupTokenChanged,
    SidecarChanged,
};

constexpr PlaySetupQualificationRevalidationFailure
first_play_setup_qualification_revalidation_failure(
    const PlaySetupQualificationRevalidation& facts) noexcept
{
    if (!facts.sound_identity_unchanged) return PlaySetupQualificationRevalidationFailure::SoundIdentityChanged;
    if (!facts.owner_unchanged) return PlaySetupQualificationRevalidationFailure::OwnerChanged;
    if (!facts.registry_generation_unchanged) return PlaySetupQualificationRevalidationFailure::RegistryGenerationChanged;
    if (!facts.route_generation_unchanged) return PlaySetupQualificationRevalidationFailure::RouteGenerationChanged;
    if (!facts.lease_unchanged) return PlaySetupQualificationRevalidationFailure::LeaseChanged;
    if (!facts.song_unchanged) return PlaySetupQualificationRevalidationFailure::SongChanged;
    if (!facts.controller_unchanged) return PlaySetupQualificationRevalidationFailure::ControllerChanged;
    if (!facts.phase_armed) return PlaySetupQualificationRevalidationFailure::PhaseNotArmed;
    if (!facts.setup_token_unchanged) return PlaySetupQualificationRevalidationFailure::SetupTokenChanged;
    if (!facts.sidecar_unchanged) return PlaySetupQualificationRevalidationFailure::SidecarChanged;
    return PlaySetupQualificationRevalidationFailure::None;
}

constexpr bool play_setup_qualification_revalidated(
    const PlaySetupQualificationRevalidation& facts) noexcept
{
    return first_play_setup_qualification_revalidation_failure(facts)
        == PlaySetupQualificationRevalidationFailure::None;
}

inline bool private_controller_set_prepared(
    const uint64_t request_before_set, const uint64_t request_after_set,
    const uint8_t state_after_set) noexcept
{
    return AudioBgmRequestHandle{request_after_set}.valid_bgm_request()
        && request_after_set != request_before_set && state_after_set == 2;
}

struct UnpublishedAudioSetupContext {
    SelectionSnapshot selection;
    CustomContextToken token;
    PrivateControllerSetupStage controller_stage = PrivateControllerSetupStage::AwaitingStop;
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    void* expected_sound = nullptr;
    void* sound = nullptr;
    ControllerIdentityProof controller_proof{};
    UObjectLiveHandle expected_sound_handle{};
    uint64_t request_before_set = 0;
    uint64_t request_after_set = 0;
    uint8_t state_after_set = 0xff;
    bool custom_patch_applied = false;
    bool custom_patch_restored = false;
    CanonicalSubstrateBridgeAuthority substrate_bridge{};

    explicit operator bool() const noexcept { return selection.song && token.valid(); }

    bool advance(const CustomContextToken& expected, const CustomContextToken& replacement) noexcept
    {
        if (!*this || token != expected || !expected.same_lease(replacement)
            || replacement.route_generation < expected.route_generation) return false;
        token = replacement;
        return true;
    }

    bool advance_or_confirm_claimed(
        const CustomContextToken& expected,
        const CustomContextToken& replacement) noexcept
    {
        if (controller_stage == PrivateControllerSetupStage::Claimed
            && token == replacement && expected.same_lease(replacement)
            && replacement.route_generation >= expected.route_generation) {
            return true;
        }
        return advance(expected, replacement);
    }

    bool bind_route_controller(const ControllerIdentityProof& proof) noexcept
    {
        if (!*this || controller_stage != PrivateControllerSetupStage::AwaitingStop
            || controller_identity_proof_valid(controller_proof)
            || !controller_identity_proof_valid(proof)) return false;
        controller_proof = proof;
        return true;
    }

    bool observe_controller_stop(
        const CustomContextToken& expected, void* expected_controller,
        const ControllerIdentityProof& current_controller_proof,
        void* expected_slot, void* expected_bgm, void* live_sound,
        const UObjectLiveHandle& live_sound_handle, uint64_t request_handle,
        PrivateControllerStopObservationDiagnostic* diagnostic = nullptr) noexcept
    {
        PrivateControllerStopObservationEvidence evidence;
        const auto reject = [&]() noexcept {
            if (diagnostic) {
                diagnostic->evidence = evidence;
                diagnostic->result = classify_private_controller_stop_observation(evidence);
            }
            return false;
        };
        evidence.context_valid = diagnostic_fact(static_cast<bool>(*this));
        if (evidence.context_valid != DiagnosticFact::Accepted) return reject();
        evidence.token_unchanged = diagnostic_fact(token == expected);
        if (evidence.token_unchanged != DiagnosticFact::Accepted) return reject();
        evidence.stage_awaiting_stop =
            diagnostic_fact(controller_stage == PrivateControllerSetupStage::AwaitingStop);
        if (evidence.stage_awaiting_stop != DiagnosticFact::Accepted) return reject();
        evidence.required_context_present =
            diagnostic_fact(expected_controller && expected_slot && expected_bgm && live_sound);
        if (evidence.required_context_present != DiagnosticFact::Accepted) return reject();
        evidence.controller_handle_valid =
            diagnostic_fact(controller_proof.controller == expected_controller
                && current_controller_proof.controller == expected_controller
                && controller_identity_proof_matches(
                    controller_proof, current_controller_proof));
        if (evidence.controller_handle_valid != DiagnosticFact::Accepted) return reject();
        evidence.sound_handle_valid =
            diagnostic_fact(private_object_handle_valid(live_sound_handle));
        if (evidence.sound_handle_valid != DiagnosticFact::Accepted) return reject();
        controller = expected_controller;
        slot = expected_slot;
        bgm = expected_bgm;
        expected_sound = live_sound;
        expected_sound_handle = live_sound_handle;
        request_before_set = request_handle;
        controller_stage = PrivateControllerSetupStage::StopObserved;
        if (substrate_bridge.phase == CanonicalSubstrateBridgePhase::Armed) {
            const bool bridge_exact = substrate_bridge.controller == expected_controller
                && substrate_bridge.slot == expected_slot
                && substrate_bridge.bgm == expected_bgm
                && substrate_bridge.old_canonical_sound == live_sound
                && private_object_handle_matches(
                    substrate_bridge.old_canonical_sound_identity,
                    live_sound_handle)
                && substrate_bridge.old_canonical_request == request_handle
                && substrate_bridge.route_generation == token.route_generation
                && substrate_bridge.lease.generation == token.lease_generation
                && substrate_bridge.song_key == token.song_key;
            substrate_bridge.phase = bridge_exact
                ? CanonicalSubstrateBridgePhase::StopObserved
                : CanonicalSubstrateBridgePhase::Failed;
        }
        if (diagnostic) {
            diagnostic->evidence = evidence;
            diagnostic->result = classify_private_controller_stop_observation(evidence);
        }
        return true;
    }

    bool capture_controller_set(
        const CustomContextToken& expected, const CustomContextToken& replacement,
        void* expected_controller, const ControllerIdentityProof& current_controller_proof,
        void* expected_slot, void* expected_bgm,
        void* requested_sound, const UObjectLiveHandle& requested_sound_handle) noexcept
    {
        if (controller_stage != PrivateControllerSetupStage::StopObserved
            || controller != expected_controller || slot != expected_slot
            || bgm != expected_bgm || requested_sound != expected_sound
            || current_controller_proof.controller != expected_controller
            || !controller_identity_proof_matches(
                controller_proof, current_controller_proof)
            || !private_object_handle_matches(expected_sound_handle, requested_sound_handle)
            || !advance(expected, replacement)) return false;
        sound = requested_sound;
        controller_stage = PrivateControllerSetupStage::SetCaptured;
        return true;
    }

    bool mark_controller_set_patched(const CustomContextToken& expected) noexcept
    {
        if (!*this || token != expected
            || controller_stage != PrivateControllerSetupStage::SetCaptured) return false;
        custom_patch_applied = true;
        controller_stage = PrivateControllerSetupStage::Patched;
        return true;
    }

    bool mark_controller_set_forwarded(
        const CustomContextToken& expected,
        const ControllerIdentityProof& current_controller_proof,
        uint64_t request_handle,
        uint8_t state, bool restored) noexcept
    {
        if (!*this || token != expected
            || controller_stage != PrivateControllerSetupStage::Patched
            || !custom_patch_applied || !restored
            || !controller_identity_proof_matches(
                controller_proof, current_controller_proof)
            || !private_controller_set_prepared(
                request_before_set, request_handle, state)) return false;
        custom_patch_restored = true;
        request_after_set = request_handle;
        state_after_set = state;
        controller_stage = PrivateControllerSetupStage::SetForwarded;
        return true;
    }

    bool controller_play_forwardable(
        const CustomContextToken& expected, void* current_controller,
        const ControllerIdentityProof& current_controller_proof,
        void* current_slot, void* current_bgm, void* current_sound,
        const UObjectLiveHandle& current_sound_handle,
        uint64_t request_handle, uint8_t state) const noexcept
    {
        return *this && token == expected
            && controller_stage == PrivateControllerSetupStage::SetForwarded
            && custom_patch_applied && custom_patch_restored
            && controller == current_controller && slot == current_slot
            && bgm == current_bgm && expected_sound == current_sound
            && current_controller_proof.controller == current_controller
            && controller_identity_proof_matches(
                controller_proof, current_controller_proof)
            && private_object_handle_matches(expected_sound_handle, current_sound_handle)
            && request_handle == request_after_set && state == state_after_set
            && state == 2;
    }

    bool controller_play_claimable(
        const CustomContextToken& expected, void* current_controller,
        const ControllerIdentityProof& current_controller_proof,
        void* current_slot, void* current_bgm, void* current_sound,
        const UObjectLiveHandle& current_sound_handle,
        uint64_t current_request_handle, uint8_t state) const noexcept
    {
        return *this && token == expected
            && controller_stage == PrivateControllerSetupStage::SetForwarded
            && custom_patch_applied && custom_patch_restored
            && controller == current_controller && slot == current_slot
            && bgm == current_bgm && expected_sound == current_sound
            && current_controller_proof.controller == current_controller
            && controller_identity_proof_matches(
                controller_proof, current_controller_proof)
            && private_object_handle_matches(expected_sound_handle, current_sound_handle)
            && current_request_handle == request_after_set
            && state_after_set == 2 && state == 4;
    }

    bool mark_controller_claimed(
        const CustomContextToken& expected, const CustomContextToken& replacement) noexcept
    {
        if (controller_stage != PrivateControllerSetupStage::SetForwarded
            || !custom_route_claim_is_complete(replacement)
            || !advance(expected, replacement)) return false;
        controller_stage = PrivateControllerSetupStage::Claimed;
        return true;
    }

    void invalidate() noexcept { *this = {}; }
};

inline bool private_controller_play_forwardable(
    const SongRegistry& registry, const UnpublishedAudioSetupContext& setup,
    const CustomContextToken& expected, void* current_controller,
    const ControllerIdentityProof& current_controller_proof,
    void* current_slot, void* current_bgm, void* current_sound,
    const UObjectLiveHandle& current_sound_handle,
    const uint64_t current_request, const uint8_t current_state)
{
    return registry.selection_guard_matches(setup.selection, expected)
        && setup.controller_play_forwardable(
            expected, current_controller, current_controller_proof,
            current_slot, current_bgm, current_sound, current_sound_handle,
            current_request, current_state);
}

inline bool claim_private_controller_play_after_forward(
    const SongRegistry& registry, UnpublishedAudioSetupContext& setup,
    const CustomContextToken& expected, void* current_controller,
    const ControllerIdentityProof& pre_controller_proof,
    void* pre_slot, void* pre_bgm, void* pre_sound,
    const UObjectLiveHandle& pre_sound_handle,
    const uint64_t pre_request, const uint8_t pre_state,
    const ControllerIdentityProof& post_controller_proof,
    void* post_slot, void* post_bgm, void* post_sound,
    const UObjectLiveHandle& post_sound_handle,
    const uint64_t post_request, const uint8_t post_state,
    const CustomContextToken& replacement)
{
    return private_controller_play_forwardable(
               registry, setup, expected, current_controller,
               pre_controller_proof, pre_slot, pre_bgm, pre_sound,
               pre_sound_handle, pre_request, pre_state)
        && registry.selection_guard_matches(setup.selection, expected)
        && setup.controller_play_claimable(
            expected, current_controller, post_controller_proof,
            post_slot, post_bgm, post_sound, post_sound_handle,
            post_request, post_state)
        && setup.mark_controller_claimed(expected, replacement);
}

inline bool publish_private_controller_claim_if_proven(
    SongRegistry& registry, const UnpublishedAudioSetupContext& setup)
{
    if (setup.controller_stage != PrivateControllerSetupStage::Claimed
        || !custom_route_claim_is_complete(setup.token)) return false;
    const PlaybackSnapshot current = registry.playback_snapshot();
    if (current.song) return current.token == setup.token;
    return registry.publish_playback_from_selection_guard(setup.selection, setup.token);
}

struct GuardedPlaySetupNativeOwnerFacts {
    bool routed_play_claimed = false;
    bool custom_resource_owned = false;
    uint64_t expected_route_generation = 0;
    uint64_t route_generation = 0;
    bool route_playing = false;
    bool route_song_matches = false;
    void* play_setup_sound = nullptr;
    void* route_controller = nullptr;
    void* owned_slot = nullptr;
    void* owned_bgm = nullptr;
    void* owned_sound = nullptr;
    UObjectLiveHandle owned_sound_handle{};
    uint64_t owned_request_handle = 0;
    CustomContextToken current_route_token{};
    GuardedPlaySetupClaimObservation observation{};
};

inline bool guarded_play_setup_native_owner_matches(
    const UnpublishedAudioSetupContext& setup,
    const CustomContextToken& current_route_token,
    const GuardedPlaySetupClaimObservation& observation,
    const UObjectLiveHandle& routed_sound_handle) noexcept
{
    return custom_route_claim_is_complete(current_route_token)
        && setup.controller == current_route_token.controller
        && setup.controller == observation.controller
        && observation.controller_proof.controller == observation.controller
        && controller_identity_proof_matches(
            setup.controller_proof, observation.controller_proof)
        && setup.slot == current_route_token.slot
        && setup.slot == observation.slot
        && setup.bgm == current_route_token.bgm
        && setup.bgm == observation.bgm
        && current_route_token.sound == observation.sound
        && private_object_handle_valid(routed_sound_handle)
        && private_object_handle_valid(observation.sound_handle)
        && private_object_handle_matches(
            routed_sound_handle, observation.sound_handle)
        && observation.request_handle == current_route_token.request_handle
        && AudioBgmRequestHandle{observation.request_handle}.valid_bgm_request()
        && observation.state == 4;
}

inline bool guarded_play_setup_native_changes_allowed(
    const UnpublishedAudioSetupContext& setup,
    const GuardedPlaySetupNativeOwnerFacts& facts) noexcept
{
    return setup
        && setup.controller_stage == PrivateControllerSetupStage::StopObserved
        && facts.routed_play_claimed
        && facts.custom_resource_owned
        && facts.expected_route_generation != 0
        && facts.route_generation == facts.expected_route_generation
        && facts.current_route_token.route_generation == facts.route_generation
        && facts.route_playing
        && facts.route_song_matches
        && facts.play_setup_sound
        && facts.current_route_token.sound == facts.play_setup_sound
        && facts.route_controller == facts.current_route_token.controller
        && facts.owned_slot == facts.current_route_token.slot
        && facts.owned_bgm == facts.current_route_token.bgm
        && facts.owned_sound == facts.current_route_token.sound
        && private_object_handle_valid(facts.owned_sound_handle)
        && facts.owned_request_handle == facts.current_route_token.request_handle
        && AudioBgmRequestHandle{facts.owned_request_handle}.valid_bgm_request()
        && guarded_play_setup_native_owner_matches(
            setup, facts.current_route_token, facts.observation,
            facts.owned_sound_handle);
}

inline bool advance_guarded_play_setup_after_restore(
    UnpublishedAudioSetupContext& setup,
    const CustomContextToken& pre_restore_token,
    const CustomContextToken& post_restore_token) noexcept
{
    return setup
        && setup.controller_stage == PrivateControllerSetupStage::StopObserved
        && setup.token == pre_restore_token
        && custom_route_claim_is_complete(pre_restore_token)
        && custom_route_claim_is_complete(post_restore_token)
        && post_restore_token.registry_generation
            == pre_restore_token.registry_generation
        && post_restore_token.route_generation
            > pre_restore_token.route_generation
        && post_restore_token.lease_generation
            == pre_restore_token.lease_generation
        && post_restore_token.song_key == pre_restore_token.song_key
        && post_restore_token.controller == pre_restore_token.controller
        && post_restore_token.slot == pre_restore_token.slot
        && post_restore_token.bgm == pre_restore_token.bgm
        && post_restore_token.sound == pre_restore_token.sound
        && post_restore_token.request_handle == pre_restore_token.request_handle
        && setup.advance(pre_restore_token, post_restore_token);
}

inline bool publish_guarded_play_setup_claim_if_proven(
    SongRegistry& registry, const UnpublishedAudioSetupContext& setup,
    const CustomContextToken& current_route_token,
    const AudioArmPublicationProof proof,
    const GuardedPlaySetupClaimObservation& observation,
    const UObjectLiveHandle& routed_sound_handle)
{
    if (!setup || proof != AudioArmPublicationProof::PlaySetupClaimed
        || setup.controller_stage != PrivateControllerSetupStage::StopObserved
        || setup.token != current_route_token
        || !custom_route_claim_is_complete(setup.token)
        || setup.selection.generation != current_route_token.registry_generation
        || !guarded_play_setup_native_owner_matches(
            setup, current_route_token, observation, routed_sound_handle)
        || registry.playback_snapshot().song) return false;
    return registry.publish_playback_from_selection_guard(
        setup.selection, current_route_token);
}

inline bool publish_audio_arm_if_proven(
    SongRegistry& registry, const SelectionSnapshot& selection,
    const CustomContextToken& token, const AudioArmPublicationProof proof)
{
    if (!audio_arm_proof_allows_playback(proof)
        || !custom_route_claim_is_complete(token)) return false;
    const PlaybackSnapshot current = registry.playback_snapshot();
    if (current.song) return current.token == token;
    return registry.publish_playback(selection, token);
}

constexpr bool bgm_prepare_identity_matches(
    const void* expected_bgm, const void* expected_sound,
    const void* current_bgm, const void* current_sound) noexcept
{
    return expected_bgm && expected_sound
        && current_bgm == expected_bgm && current_sound == expected_sound;
}

class AudioRouteOperationCoordinator {
public:
    std::recursive_mutex& mutex() noexcept { return mutex_; }

private:
    std::recursive_mutex mutex_;
};

} // namespace ff7r::piano::game
