#include "game/audio_play_setup_diagnostics.h"

#include "core/logging.h"
#include "tests/test_support.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

using namespace ff7r::piano::game;
namespace detail = ff7r::piano::game::audio_sead_detail;

void require(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "audio_play_setup_diagnostics_selftest: " << message << '\n';
        std::exit(1);
    }
}

std::string read_log(const std::filesystem::path& path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void test_stable_names()
{
    struct DecisionCase {
        detail::ChartAudioPlaySetupDecision value;
        const char* name;
    };
    const DecisionCase decisions[]{
        {detail::ChartAudioPlaySetupDecision::None, "none"},
        {detail::ChartAudioPlaySetupDecision::DisabledOrNull, "disabled_or_null"},
        {detail::ChartAudioPlaySetupDecision::NoExactArm, "no_exact_arm"},
        {detail::ChartAudioPlaySetupDecision::ArmMissing, "arm_missing"},
        {detail::ChartAudioPlaySetupDecision::SidecarMissing, "sidecar_missing"},
        {detail::ChartAudioPlaySetupDecision::SoundIdentityInvalid, "sound_identity_invalid"},
        {detail::ChartAudioPlaySetupDecision::BankQualificationFailed, "bank_qualification_failed"},
        {detail::ChartAudioPlaySetupDecision::PendingRestoreFailed, "pending_restore_failed"},
        {detail::ChartAudioPlaySetupDecision::PatchFailed, "patch_failed"},
        {detail::ChartAudioPlaySetupDecision::RouteChanged, "route_changed"},
        {detail::ChartAudioPlaySetupDecision::TokenUpdateFailed, "token_update_failed"},
        {detail::ChartAudioPlaySetupDecision::PatchedForOriginal, "patched_for_original"},
        {detail::ChartAudioPlaySetupDecision::RoutedPublicationSucceeded,
            "routed_publication_succeeded"},
        {detail::ChartAudioPlaySetupDecision::RoutedPublicationFailed,
            "routed_publication_failed"},
    };
    for (const auto& item : decisions) {
        require(std::string(detail::chart_audio_play_setup_decision_name(item.value)) == item.name,
            "chart decision name changed");
    }

    struct GuardedCase {
        GuardedPlaySetupClaimReadFailure value;
        const char* name;
    };
    const GuardedCase guarded[]{
        {GuardedPlaySetupClaimReadFailure::None, "none"},
        {GuardedPlaySetupClaimReadFailure::BoundControllerMissing,
            "bound_controller_missing"},
        {GuardedPlaySetupClaimReadFailure::CurrentControllerMismatch,
            "current_controller_mismatch"},
        {GuardedPlaySetupClaimReadFailure::ControllerProofUnavailable,
            "controller_proof_unavailable"},
        {GuardedPlaySetupClaimReadFailure::AudioChainUnreadable,
            "audio_chain_unreadable"},
        {GuardedPlaySetupClaimReadFailure::SoundMissing, "sound_missing"},
        {GuardedPlaySetupClaimReadFailure::SoundIdentityUnavailable,
            "sound_identity_unavailable"},
    };
    for (const auto& item : guarded) {
        require(std::string(detail::guarded_play_setup_claim_read_failure_name(item.value))
                == item.name,
            "guarded claim failure name changed");
    }

    struct ChainCase {
        ControllerAudioChainReadFailure value;
        const char* name;
    };
    const ChainCase chain[]{
        {ControllerAudioChainReadFailure::None, "none"},
        {ControllerAudioChainReadFailure::ControllerNull, "controller_null"},
        {ControllerAudioChainReadFailure::SlotUnreadable, "slot_unreadable"},
        {ControllerAudioChainReadFailure::SlotNull, "slot_null"},
        {ControllerAudioChainReadFailure::BgmUnreadable, "bgm_unreadable"},
        {ControllerAudioChainReadFailure::BgmNull, "bgm_null"},
        {ControllerAudioChainReadFailure::SoundUnreadable, "sound_unreadable"},
        {ControllerAudioChainReadFailure::RequestUnreadable, "request_unreadable"},
        {ControllerAudioChainReadFailure::StateUnreadable, "state_unreadable"},
    };
    for (const auto& item : chain) {
        require(std::string(detail::controller_audio_chain_read_failure_name(item.value))
                == item.name,
            "audio chain failure name changed");
    }

    require(std::string(detail::restore_failure_stage_name(
                AudioPatchRestoreFailureStage::FieldRead)) == "field_read" &&
            std::string(detail::restore_failure_category_name(
                AudioPatchRestoreFailureCategory::PreserveNativeValue)) ==
                "preserve_native_value" &&
            std::string(detail::rollback_failure_stage_name(
                AudioPatchRollbackFailureStage::PostWriteVerify)) == "post_write_verify",
        "restore failure name changed");
}

void test_restore_projection(const std::filesystem::path& path)
{
    detail::log_play_setup_restore_failure({});
    { detail::DeferredPlaySetupRestoreFailureEmitter emitter; }
    require(read_log(path).find("play_setup_restore_failure") == std::string::npos,
        "default restore report emitted");

    {
        detail::DeferredPlaySetupRestoreFailureEmitter emitter;
        auto* report = emitter.report();
        report->stage = AudioPatchRestoreFailureStage::PostWriteVerify;
        report->category = AudioPatchRestoreFailureCategory::Conflict;
        std::memcpy(report->label, "primary", sizeof("primary"));
        report->offset = 0x28;
        report->size = 8;
        report->current_read = true;
        report->values_redacted = true;
        report->allow_native_changes = true;
        report->restore_plan_index = 3;
        report->rollback_stage = AudioPatchRollbackFailureStage::RestoreWrite;
        std::memcpy(report->rollback_label, "rollback", sizeof("rollback"));
        report->rollback_offset = 0x38;
        report->rollback_size = 8;
        report->rollback_current_read = true;
        report->rollback_values_redacted = true;
        report->rollback_plan_index = 2;
    }
    const std::string log = read_log(path);
    require(log.find("[error] [audio_sead] play_setup_restore_failure") != std::string::npos &&
            log.find("stage=post_write_verify category=conflict label=primary") !=
                std::string::npos &&
            log.find("offset=0x28 size=8 current_read=1 values=redacted") !=
                std::string::npos &&
            log.find("rollback_failed=1 rollback_stage=restore_write") !=
                std::string::npos &&
            log.find("rollback_label=rollback rollback_offset=0x38") !=
                std::string::npos &&
            log.find("rollback_values=redacted rollback_plan_index=2") !=
                std::string::npos,
        "restore failure projection changed");
}

void test_chart_projection(const std::filesystem::path& path)
{
    detail::ChartAudioPlaySetupDiagnostic suppressed{};
    detail::log_chart_audio_play_setup_diagnostic(suppressed);
    suppressed.proposed = true;
    detail::log_chart_audio_play_setup_diagnostic(suppressed);
    require(read_log(path).find("chart_audio_play_setup") == std::string::npos,
        "suppressed chart diagnostic emitted");

    detail::ChartAudioPlaySetupDiagnostic unavailable{};
    unavailable.proposed = true;
    unavailable.generation = 41;
    unavailable.decision = detail::ChartAudioPlaySetupDecision::RouteChanged;
    unavailable.substrate_bridge = true;
    unavailable.substrate_bridge_generation = 42;
    unavailable.substrate_bridge_restore_attempted = true;
    unavailable.substrate_bridge_restore_failed = true;
    unavailable.substrate_bridge_canonical = 0x1234;
    unavailable.substrate_bridge_custom = 0x5678;
    unavailable.substrate_bridge_set_bound = true;
    unavailable.routed_play_claimed = true;
    unavailable.publication_succeeded = false;
    detail::log_chart_audio_play_setup_diagnostic(unavailable);

    detail::ChartAudioPlaySetupDiagnostic authoritative{};
    authoritative.proposed = true;
    authoritative.generation = 51;
    authoritative.decision =
        detail::ChartAudioPlaySetupDecision::RoutedPublicationSucceeded;
    authoritative.native_attempt = {
        BgmPlaybackNativePlaySetupTarget::Trampoline, 1, true, true};
    authoritative.native_return.native = {BgmPlaybackNativePlaySetupTarget::Trampoline,
        BgmPlaybackNativePlaySetupCallResult::CalledReturned, 1};
    authoritative.native_return.tls = {2, 52, true};
    authoritative.native_return.post_return_observation_ran = true;
    authoritative.native_return.original_forwarded = true;
    authoritative.pre_restore_claim_read_attempted = true;
    authoritative.pre_restore_claim_read_failure =
        GuardedPlaySetupClaimReadFailure::AudioChainUnreadable;
    authoritative.pre_restore_claim_audio_chain_failure =
        ControllerAudioChainReadFailure::RequestUnreadable;
    authoritative.post_restore_claim_read_attempted = true;
    authoritative.post_restore_claim_read_failure =
        GuardedPlaySetupClaimReadFailure::SoundIdentityUnavailable;
    authoritative.post_restore_claim_audio_chain_failure =
        ControllerAudioChainReadFailure::StateUnreadable;
    authoritative.publication_succeeded = true;
    authoritative.substrate_bridge_published = true;
    detail::log_chart_audio_play_setup_diagnostic(authoritative);

    const std::string log = read_log(path);
    require(log.find("generation=41") != std::string::npos &&
            log.find("decision=route_changed") != std::string::npos &&
            log.find("native_tuple_authoritative=0 native_target=unavailable") !=
                std::string::npos &&
            log.find("pre_restore_claim_read_failure=unavailable") !=
                std::string::npos &&
            log.find("post_restore_claim_audio_chain_failure=unavailable") !=
                std::string::npos &&
            log.find("substrate_bridge_canonical=0x1234 substrate_bridge_custom=0x5678") !=
                std::string::npos &&
            log.find("substrate_bridge_restore_failed=1") != std::string::npos &&
            log.find("routed_play_claimed=1 publication_succeeded=0") !=
                std::string::npos,
        "unavailable chart projection changed");
    require(log.find("generation=51") != std::string::npos &&
            log.find("decision=routed_publication_succeeded") != std::string::npos &&
            log.find("native_tuple_authoritative=1 native_target=1 native_result=1") !=
                std::string::npos &&
            log.find("native_call_count=1 native_return_observed=1") !=
                std::string::npos &&
            log.find("native_return_original_depth=2 native_return_expected_generation=52") !=
                std::string::npos &&
            log.find("pre_restore_claim_read_failure=audio_chain_unreadable") !=
                std::string::npos &&
            log.find("pre_restore_claim_audio_chain_failure=request_unreadable") !=
                std::string::npos &&
            log.find("post_restore_claim_read_failure=sound_identity_unavailable") !=
                std::string::npos &&
            log.find("post_restore_claim_audio_chain_failure=state_unreadable") !=
                std::string::npos &&
            log.find("publication_succeeded=1") != std::string::npos &&
            log.find("substrate_bridge_published=1") != std::string::npos,
        "authoritative chart projection changed");
}

} // namespace

int main()
{
    try {
        ff7rp::tests::TemporaryDirectory temporary("ff7rp-audio-play-setup-diagnostics");
        const auto path = temporary.path() / "diagnostics.log";
        ff7r::piano::core::set_log_path(path.wstring());
        ff7r::piano::core::set_log_level(ff7r::piano::core::LogLevel::Debug);
        test_stable_names();
        test_restore_projection(path);
        test_chart_projection(path);
    } catch (const std::exception& ex) {
        std::cerr << "audio_play_setup_diagnostics_selftest: " << ex.what() << '\n';
        return 1;
    }
    std::cout << "audio_play_setup_diagnostics_selftest: ok\n";
    return 0;
}
