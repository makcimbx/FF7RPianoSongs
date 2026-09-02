#pragma once

#include "game/audio_cleanup_policy.h"
#include "game/audio_memory_read_policy.h"
#include "game/audio_native_call_policy.h"
#include "game/canonical_substrate_policy.h"
#include "game/chart_patch.h"

#include <cstdint>
#include <type_traits>

namespace ff7r::piano::game::audio_sead_detail {

const char* restore_failure_stage_name(
    AudioPatchRestoreFailureStage stage) noexcept;
const char* restore_failure_category_name(
    AudioPatchRestoreFailureCategory category) noexcept;
const char* rollback_failure_stage_name(
    AudioPatchRollbackFailureStage stage) noexcept;
void log_play_setup_restore_failure(
    const AudioPatchRestoreFailureReport& report) noexcept;

class DeferredPlaySetupRestoreFailureEmitter final {
public:
    DeferredPlaySetupRestoreFailureEmitter() noexcept = default;

    AudioPatchRestoreFailureReport* report() noexcept
    {
        return &report_;
    }

    ~DeferredPlaySetupRestoreFailureEmitter() noexcept
    {
        log_play_setup_restore_failure(report_);
    }

private:
    AudioPatchRestoreFailureReport report_{};
};

enum class ChartAudioPlaySetupDecision : uint8_t {
    None,
    DisabledOrNull,
    NoExactArm,
    ArmMissing,
    SidecarMissing,
    SoundIdentityInvalid,
    BankQualificationFailed,
    PendingRestoreFailed,
    PatchFailed,
    RouteChanged,
    TokenUpdateFailed,
    PatchedForOriginal,
    RoutedPublicationSucceeded,
    RoutedPublicationFailed,
};

struct ChartAudioPlaySetupDiagnostic final {
    bool proposed = false;
    bool associated_by_tls = false;
    bool associated_by_exact_state = false;
    uint64_t generation = 0;
    uint64_t selection_generation = 0;
    uint64_t route_generation = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
    uint8_t setup_stage = 0xff;
    ChartAudioExpandTlsSnapshot tls{};
    ChartAudioPlaySetupDecision decision = ChartAudioPlaySetupDecision::None;
    bool patch_applied = false;
    bool original_forwarded = false;
    bool original_forwarded_patched = false;
    bool substrate_bridge = false;
    uint64_t substrate_bridge_generation = 0;
    CanonicalSubstrateBridgePhase substrate_bridge_phase =
        CanonicalSubstrateBridgePhase::None;
    CanonicalSubstrateBridgePlaySetupFailure substrate_bridge_first_failure =
        CanonicalSubstrateBridgePlaySetupFailure::None;
    CanonicalSubstrateBridgePatchTransitionFailure substrate_bridge_patch_failure =
        CanonicalSubstrateBridgePatchTransitionFailure::None;
    CanonicalSubstrateBridgeRestorePrepatchFailure substrate_bridge_restore_prepatch_failure =
        CanonicalSubstrateBridgeRestorePrepatchFailure::None;
    bool substrate_bridge_restore_attempted = false;
    bool substrate_bridge_restore_succeeded = false;
    bool substrate_bridge_restore_failed = false;
    uint64_t substrate_bridge_route_generation_before_restore = 0;
    uint64_t substrate_bridge_route_generation_after_restore = 0;
    bool substrate_bridge_post_restore_check_attempted = false;
    bool substrate_bridge_post_restore_exact = false;
    bool substrate_bridge_new_patch_invoked = false;
    bool substrate_bridge_original_invoked = false;
    uint64_t substrate_bridge_old_route_generation = 0;
    uint64_t substrate_bridge_old_setup_generation = 0;
    uint64_t substrate_bridge_patched_generation = 0;
    bool substrate_bridge_successor_exact = false;
    bool substrate_bridge_generation_advanced = false;
    uint64_t substrate_bridge_canonical = 0;
    uint64_t substrate_bridge_custom = 0;
    uint32_t substrate_bridge_canonical_kind = UINT32_MAX;
    uint32_t substrate_bridge_custom_kind = UINT32_MAX;
    bool substrate_bridge_set_bound = false;
    bool substrate_bridge_published = false;
    bool original_forwarded_unmodified = false;
    BgmPlaybackNativePlaySetupAttemptState native_attempt{};
    BgmPlaybackNativePlaySetupReturnObservation native_return{};
    bool pre_restore_claim_read_attempted = false;
    GuardedPlaySetupClaimReadFailure pre_restore_claim_read_failure =
        GuardedPlaySetupClaimReadFailure::BoundControllerMissing;
    ControllerAudioChainReadFailure pre_restore_claim_audio_chain_failure =
        ControllerAudioChainReadFailure::None;
    bool post_restore_claim_read_attempted = false;
    GuardedPlaySetupClaimReadFailure post_restore_claim_read_failure =
        GuardedPlaySetupClaimReadFailure::BoundControllerMissing;
    ControllerAudioChainReadFailure post_restore_claim_audio_chain_failure =
        ControllerAudioChainReadFailure::None;
    bool routed_play_claimed = false;
    bool publication_succeeded = false;
    bool lifecycle_transition_exact = false;
    uint64_t lifecycle_epoch_before = 0;
    uint64_t lifecycle_epoch_after = 0;
    bool entry_captured = false;
};

static_assert(std::is_trivially_copyable_v<ChartAudioPlaySetupDiagnostic>);

void log_chart_audio_play_setup_diagnostic(
    const ChartAudioPlaySetupDiagnostic& diagnostic) noexcept;

const char* chart_audio_play_setup_decision_name(
    ChartAudioPlaySetupDecision decision) noexcept;
const char* guarded_play_setup_claim_read_failure_name(
    GuardedPlaySetupClaimReadFailure failure) noexcept;
const char* controller_audio_chain_read_failure_name(
    ControllerAudioChainReadFailure failure) noexcept;

} // namespace ff7r::piano::game::audio_sead_detail
