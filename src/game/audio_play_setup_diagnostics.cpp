#include "game/audio_play_setup_diagnostics.h"

#include "core/logging.h"
#include "game/bgm_playback_aggregate_policy.h"

#include <atomic>
#include <iomanip>
#include <sstream>

namespace ff7r::piano::game::audio_sead_detail {

const char* restore_failure_stage_name(
    const AudioPatchRestoreFailureStage stage) noexcept
{
    switch (stage) {
    case AudioPatchRestoreFailureStage::None: return "none";
    case AudioPatchRestoreFailureStage::FieldRead: return "field_read";
    case AudioPatchRestoreFailureStage::ConflictDecision: return "conflict_decision";
    case AudioPatchRestoreFailureStage::RestoreWrite: return "restore_write";
    case AudioPatchRestoreFailureStage::PostWriteVerify: return "post_write_verify";
    }
    return "unknown";
}

const char* restore_failure_category_name(
    const AudioPatchRestoreFailureCategory category) noexcept
{
    switch (category) {
    case AudioPatchRestoreFailureCategory::Unclassified: return "unclassified";
    case AudioPatchRestoreFailureCategory::AlreadyRestored: return "already_restored";
    case AudioPatchRestoreFailureCategory::RestoreOriginal: return "restore_original";
    case AudioPatchRestoreFailureCategory::PreserveNativeValue: return "preserve_native_value";
    case AudioPatchRestoreFailureCategory::Conflict: return "conflict";
    }
    return "unknown";
}

const char* rollback_failure_stage_name(
    const AudioPatchRollbackFailureStage stage) noexcept
{
    switch (stage) {
    case AudioPatchRollbackFailureStage::None: return "none";
    case AudioPatchRollbackFailureStage::RestoreWrite: return "restore_write";
    case AudioPatchRollbackFailureStage::PostWriteVerify: return "post_write_verify";
    }
    return "unknown";
}

void log_play_setup_restore_failure(
    const AudioPatchRestoreFailureReport& report) noexcept
{
    if (!report.failed()) return;
    static std::atomic_uint32_t s_failure_logs{0};
    if (s_failure_logs.fetch_add(1, std::memory_order_relaxed) >= 16) return;
    try {
        std::ostringstream out;
        out << "[audio_sead] play_setup_restore_failure"
            << " stage=" << restore_failure_stage_name(report.stage)
            << " category=" << restore_failure_category_name(report.category)
            << " label=" << (report.label[0] ? report.label : "?")
            << " offset=0x" << std::hex << report.offset
            << std::dec
            << " size=" << report.size
            << " current_read=" << (report.current_read ? 1 : 0);
        if (report.values_redacted) {
            out << " values=redacted";
        } else {
            if (report.current_read) {
                out << " current=0x" << std::hex << report.current << std::dec;
            } else {
                out << " current=unread";
            }
            out << " original=0x" << std::hex << report.original
                << " replacement=0x" << report.replacement << std::dec;
        }
        out << " allow_native_changes=" << (report.allow_native_changes ? 1 : 0)
            << " restore_plan_index=" << report.restore_plan_index
            << " classified_already_restored=" << report.already_restored_count
            << " classified_restore_original=" << report.restore_original_count
            << " classified_preserve_native_value=" << report.preserve_native_value_count
            << " rollback_failed=" << (report.rollback_failed() ? 1 : 0)
            << " rollback_stage=" << rollback_failure_stage_name(report.rollback_stage);
        if (report.rollback_failed()) {
            out << " rollback_label="
                << (report.rollback_label[0] ? report.rollback_label : "?")
                << " rollback_offset=0x" << std::hex << report.rollback_offset
                << std::dec
                << " rollback_size=" << report.rollback_size
                << " rollback_current_read="
                << (report.rollback_current_read ? 1 : 0);
            if (report.rollback_values_redacted) {
                out << " rollback_values=redacted";
            } else {
                if (report.rollback_current_read) {
                    out << " rollback_current=0x" << std::hex
                        << report.rollback_current << std::dec;
                } else {
                    out << " rollback_current=unread";
                }
                out << " rollback_original=0x" << std::hex
                    << report.rollback_original
                    << " rollback_replacement=0x" << report.rollback_replacement
                    << std::dec;
            }
            out << " rollback_plan_index=" << report.rollback_plan_index;
        }
        core::log(core::LogLevel::Error, out.str());
    } catch (...) {
    }
}

const char* chart_audio_play_setup_decision_name(
    const ChartAudioPlaySetupDecision decision) noexcept
{
    switch (decision) {
    case ChartAudioPlaySetupDecision::None: return "none";
    case ChartAudioPlaySetupDecision::DisabledOrNull: return "disabled_or_null";
    case ChartAudioPlaySetupDecision::NoExactArm: return "no_exact_arm";
    case ChartAudioPlaySetupDecision::ArmMissing: return "arm_missing";
    case ChartAudioPlaySetupDecision::SidecarMissing: return "sidecar_missing";
    case ChartAudioPlaySetupDecision::SoundIdentityInvalid: return "sound_identity_invalid";
    case ChartAudioPlaySetupDecision::BankQualificationFailed: return "bank_qualification_failed";
    case ChartAudioPlaySetupDecision::PendingRestoreFailed: return "pending_restore_failed";
    case ChartAudioPlaySetupDecision::PatchFailed: return "patch_failed";
    case ChartAudioPlaySetupDecision::RouteChanged: return "route_changed";
    case ChartAudioPlaySetupDecision::TokenUpdateFailed: return "token_update_failed";
    case ChartAudioPlaySetupDecision::PatchedForOriginal: return "patched_for_original";
    case ChartAudioPlaySetupDecision::RoutedPublicationSucceeded: return "routed_publication_succeeded";
    case ChartAudioPlaySetupDecision::RoutedPublicationFailed: return "routed_publication_failed";
    }
    return "unknown";
}

const char* guarded_play_setup_claim_read_failure_name(
    const GuardedPlaySetupClaimReadFailure failure) noexcept
{
    switch (failure) {
    case GuardedPlaySetupClaimReadFailure::None: return "none";
    case GuardedPlaySetupClaimReadFailure::BoundControllerMissing:
        return "bound_controller_missing";
    case GuardedPlaySetupClaimReadFailure::CurrentControllerMismatch:
        return "current_controller_mismatch";
    case GuardedPlaySetupClaimReadFailure::ControllerProofUnavailable:
        return "controller_proof_unavailable";
    case GuardedPlaySetupClaimReadFailure::AudioChainUnreadable:
        return "audio_chain_unreadable";
    case GuardedPlaySetupClaimReadFailure::SoundMissing:
        return "sound_missing";
    case GuardedPlaySetupClaimReadFailure::SoundIdentityUnavailable:
        return "sound_identity_unavailable";
    }
    return "unknown";
}

const char* controller_audio_chain_read_failure_name(
    const ControllerAudioChainReadFailure failure) noexcept
{
    switch (failure) {
    case ControllerAudioChainReadFailure::None: return "none";
    case ControllerAudioChainReadFailure::ControllerNull:
        return "controller_null";
    case ControllerAudioChainReadFailure::SlotUnreadable:
        return "slot_unreadable";
    case ControllerAudioChainReadFailure::SlotNull: return "slot_null";
    case ControllerAudioChainReadFailure::BgmUnreadable:
        return "bgm_unreadable";
    case ControllerAudioChainReadFailure::BgmNull: return "bgm_null";
    case ControllerAudioChainReadFailure::SoundUnreadable:
        return "sound_unreadable";
    case ControllerAudioChainReadFailure::RequestUnreadable:
        return "request_unreadable";
    case ControllerAudioChainReadFailure::StateUnreadable:
        return "state_unreadable";
    }
    return "unknown";
}

void log_chart_audio_play_setup_diagnostic(
    const ChartAudioPlaySetupDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed || diagnostic.generation == 0) return;
    bgm_playback_observe_best_effort(true, [&]() {
        std::ostringstream out;
        out << "[audio_sead] chart_audio_play_setup"
            << " generation=" << diagnostic.generation
            << " associated_by_tls=" << diagnostic.associated_by_tls
            << " associated_by_exact_state="
            << diagnostic.associated_by_exact_state
            << " selection_generation=" << diagnostic.selection_generation
            << " route_generation=" << diagnostic.route_generation
            << " lease_generation=" << diagnostic.lease_generation
            << " song_key=" << diagnostic.song_key
            << " setup_stage=" << static_cast<unsigned>(diagnostic.setup_stage)
            << " expand_depth=" << diagnostic.tls.depth
            << " expand_enter_ordinal=" << diagnostic.tls.enter_ordinal
            << " expand_exit_ordinal=" << diagnostic.tls.exit_ordinal
            << " expand_original_inflight="
            << diagnostic.tls.original_inflight
            << " expand_enter_time_count_read="
            << diagnostic.tls.enter_time.count_read
            << " expand_enter_time_count=" << diagnostic.tls.enter_time.count
            << " expand_enter_event_count_read="
            << diagnostic.tls.enter_event.count_read
            << " expand_enter_event_count=" << diagnostic.tls.enter_event.count
            << " expand_exit_time_count_read="
            << diagnostic.tls.exit_time.count_read
            << " expand_exit_time_count=" << diagnostic.tls.exit_time.count
            << " expand_exit_event_count_read="
            << diagnostic.tls.exit_event.count_read
            << " expand_exit_event_count=" << diagnostic.tls.exit_event.count
            << " entry_captured=" << diagnostic.entry_captured
            << " decision="
            << chart_audio_play_setup_decision_name(diagnostic.decision)
            << " patch_applied=" << diagnostic.patch_applied
            << " original_forwarded=" << diagnostic.original_forwarded
            << " original_forwarded_patched="
            << diagnostic.original_forwarded_patched
            << " original_forwarded_unmodified="
            << diagnostic.original_forwarded_unmodified
            << " native_attempt_count="
            << static_cast<unsigned>(diagnostic.native_attempt.attempt_count)
            << " native_selected_target="
            << static_cast<unsigned>(diagnostic.native_attempt.selected_target)
            << " native_invocation_started="
            << diagnostic.native_attempt.invocation_started
            << " native_return_captured="
            << diagnostic.native_attempt.native_return_captured
            << " native_tuple_authoritative="
            << bgm_playback_native_play_setup_return_tuple_authoritative(
                diagnostic.native_attempt);
        if (bgm_playback_native_play_setup_return_tuple_authoritative(
                diagnostic.native_attempt)) {
            out << " native_target="
                << static_cast<unsigned>(diagnostic.native_return.native.target)
                << " native_result="
                << static_cast<unsigned>(diagnostic.native_return.native.result)
                << " native_call_count="
                << static_cast<unsigned>(
                    diagnostic.native_return.native.call_count)
                << " native_return_observed="
                << diagnostic.native_return.post_return_observation_ran
                << " native_return_original_depth="
                << diagnostic.native_return.tls.original_depth
                << " native_return_expected_generation="
                << diagnostic.native_return.tls.expected_generation
                << " native_return_play_claimed="
                << diagnostic.native_return.tls.play_claimed
                << " native_return_original_forwarded="
                << diagnostic.native_return.original_forwarded;
        } else {
            out << " native_target=unavailable"
                << " native_result=unavailable"
                << " native_call_count=unavailable"
                << " native_return_observed=0"
                << " native_return_original_depth=unavailable"
                << " native_return_expected_generation=unavailable"
                << " native_return_play_claimed=unavailable"
                << " native_return_original_forwarded=unavailable";
        }
        out
            << " pre_restore_claim_read_attempted="
            << diagnostic.pre_restore_claim_read_attempted
            << " pre_restore_claim_read_failure="
            << (diagnostic.pre_restore_claim_read_attempted
                ? guarded_play_setup_claim_read_failure_name(
                    diagnostic.pre_restore_claim_read_failure)
                : "unavailable")
            << " pre_restore_claim_audio_chain_failure="
            << (diagnostic.pre_restore_claim_read_attempted
                ? controller_audio_chain_read_failure_name(
                    diagnostic.pre_restore_claim_audio_chain_failure)
                : "unavailable")
            << " post_restore_claim_read_attempted="
            << diagnostic.post_restore_claim_read_attempted
            << " post_restore_claim_read_failure="
            << (diagnostic.post_restore_claim_read_attempted
                ? guarded_play_setup_claim_read_failure_name(
                    diagnostic.post_restore_claim_read_failure)
                : "unavailable")
            << " post_restore_claim_audio_chain_failure="
            << (diagnostic.post_restore_claim_read_attempted
                ? controller_audio_chain_read_failure_name(
                    diagnostic.post_restore_claim_audio_chain_failure)
                : "unavailable")
            << " routed_play_claimed=" << diagnostic.routed_play_claimed
            << " publication_succeeded=" << diagnostic.publication_succeeded
            << " substrate_bridge=" << diagnostic.substrate_bridge
            << " substrate_bridge_generation="
            << diagnostic.substrate_bridge_generation
            << " substrate_bridge_phase="
            << static_cast<unsigned>(diagnostic.substrate_bridge_phase)
            << " substrate_bridge_first_failure="
            << static_cast<unsigned>(diagnostic.substrate_bridge_first_failure)
            << " substrate_bridge_patch_failure="
            << static_cast<unsigned>(diagnostic.substrate_bridge_patch_failure)
            << " substrate_bridge_restore_prepatch_failure="
            << static_cast<unsigned>(
                diagnostic.substrate_bridge_restore_prepatch_failure)
            << " substrate_bridge_restore_attempted="
            << diagnostic.substrate_bridge_restore_attempted
            << " substrate_bridge_restore_succeeded="
            << diagnostic.substrate_bridge_restore_succeeded
            << " substrate_bridge_restore_failed="
            << diagnostic.substrate_bridge_restore_failed
            << " substrate_bridge_route_generation_before_restore="
            << diagnostic.substrate_bridge_route_generation_before_restore
            << " substrate_bridge_route_generation_after_restore="
            << diagnostic.substrate_bridge_route_generation_after_restore
            << " substrate_bridge_post_restore_check_attempted="
            << diagnostic.substrate_bridge_post_restore_check_attempted
            << " substrate_bridge_post_restore_exact="
            << diagnostic.substrate_bridge_post_restore_exact
            << " substrate_bridge_new_patch_invoked="
            << diagnostic.substrate_bridge_new_patch_invoked
            << " substrate_bridge_original_invoked="
            << diagnostic.substrate_bridge_original_invoked
            << " substrate_bridge_old_route_generation="
            << diagnostic.substrate_bridge_old_route_generation
            << " substrate_bridge_old_setup_generation="
            << diagnostic.substrate_bridge_old_setup_generation
            << " substrate_bridge_patched_generation="
            << diagnostic.substrate_bridge_patched_generation
            << " substrate_bridge_successor_exact="
            << diagnostic.substrate_bridge_successor_exact
            << " substrate_bridge_generation_advanced="
            << diagnostic.substrate_bridge_generation_advanced
            << " substrate_bridge_canonical=0x" << std::hex
            << diagnostic.substrate_bridge_canonical
            << " substrate_bridge_custom=0x"
            << diagnostic.substrate_bridge_custom << std::dec
            << " substrate_bridge_canonical_kind="
            << diagnostic.substrate_bridge_canonical_kind
            << " substrate_bridge_custom_kind="
            << diagnostic.substrate_bridge_custom_kind
            << " substrate_bridge_set_bound="
            << diagnostic.substrate_bridge_set_bound
            << " substrate_bridge_published="
            << diagnostic.substrate_bridge_published;
        core::log(core::LogLevel::Info, out.str());
    });
}

} // namespace ff7r::piano::game::audio_sead_detail
