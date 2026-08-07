#include "runtime_lifecycle_test_support.h"

#include "core/hooks.h"
#include "game/audio_cleanup_policy.h"
#include "game/completion_capture.h"
#include "game/duration.h"
#include "game/frozen_profile_lifecycle.h"
#include "game/native_array_publication.h"
#include "game/onmemory_bank_diagnostic.h"
#include "game/onmemory_bank_lifecycle.h"
#include "game/progress.h"
#include "game/runtime_context_policy.h"
#include "game/scoreinfo_overlay.h"
#include "game/title.h"
#include "game/uobject_locator_core.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ff7r::piano::tests::runtime_lifecycle {

struct PatchRestoreTestConfig {
    std::vector<int> failed_reads;
    std::vector<int> failed_writes;
    std::vector<int> mismatched_reads;
};

struct PatchRestoreTestState {
    PatchRestoreTestConfig config;
    int read_count = 0;
    int write_count = 0;
    std::vector<std::string> events;
};

struct PatchRestoreTestResult {
    bool restored = false;
    std::vector<uint64_t> values;
    std::vector<std::string> events;
    ff7r::piano::game::AudioPatchRestoreFailureReport report;
};

bool patch_restore_call_selected(const std::vector<int>& calls, const int call)
{
    return std::find(calls.begin(), calls.end(), call) != calls.end();
}

std::string patch_restore_event_name(
    const char* operation,
    const ff7r::piano::game::AudioPatchRestoreField& field)
{
    return std::string(operation) + ":patch_"
        + std::to_string((field.offset - 0x38) / 8);
}

PatchRestoreTestResult run_patch_restore_test(
    const std::vector<uint64_t>& initial_values,
    const std::vector<uint64_t>& original_values,
    const std::vector<uint64_t>& replacement_values,
    const bool allow_native_changes,
    const PatchRestoreTestConfig& config,
    const bool capture_report,
    const char* first_label_override = nullptr,
    const bool first_field_values_redacted = false,
    const bool last_field_values_redacted = false)
{
    using namespace ff7r::piano::game;
    require(initial_values.size() == original_values.size()
            && initial_values.size() == replacement_values.size(),
        "patch restore test vectors have different sizes");
    PatchRestoreTestResult result;
    result.values = initial_values;
    PatchRestoreTestState state{config};
    static const char* const labels[]{
        "patch_0", "patch_1", "patch_2", "patch_3", "patch_4"};
    require(result.values.size() <= sizeof(labels) / sizeof(*labels),
        "patch restore test has too many fields");
    std::vector<AudioPatchRestoreField> fields;
    fields.reserve(result.values.size());
    for (size_t index = 0; index < result.values.size(); ++index) {
        const char* const label = index == 0 && first_label_override
            ? first_label_override
            : labels[index];
        AudioPatchRestoreField field;
        field.object = &result.values[index];
        field.offset = 0x38 + index * 8;
        field.original = original_values[index];
        field.replacement = replacement_values[index];
        field.size = sizeof(uint64_t);
        field.label = label;
        field.redact_values_in_report =
            (index == 0 && first_field_values_redacted)
            || (index + 1 == result.values.size() && last_field_values_redacted);
        fields.push_back(field);
    }
    const AudioPatchRestoreAccess access{
        [](const AudioPatchRestoreField& field,
            uint64_t& current, void* context) {
            auto& test = *static_cast<PatchRestoreTestState*>(context);
            const int call = ++test.read_count;
            test.events.push_back(patch_restore_event_name("read", field));
            if (patch_restore_call_selected(test.config.failed_reads, call)) return false;
            current = *static_cast<const uint64_t*>(field.object);
            if (patch_restore_call_selected(test.config.mismatched_reads, call)) current ^= 0xff;
            return true;
        },
        [](const AudioPatchRestoreField& field,
            const uint64_t value, void* context) {
            auto& test = *static_cast<PatchRestoreTestState*>(context);
            const int call = ++test.write_count;
            test.events.push_back(patch_restore_event_name("write", field));
            if (patch_restore_call_selected(test.config.failed_writes, call)) return false;
            *static_cast<uint64_t*>(field.object) = value;
            return true;
        },
        [](const AudioPatchRestoreField& field, uint64_t, void* context) {
            static_cast<PatchRestoreTestState*>(context)->events.push_back(
                patch_restore_event_name("preserve", field));
        },
        &state,
    };
    result.restored = restore_audio_patches_reverse(fields,
        allow_native_changes, access, capture_report ? &result.report : nullptr);
    result.events = std::move(state.events);
    return result;
}

void require_restore_report_observational(
    const std::vector<uint64_t>& initial_values,
    const std::vector<uint64_t>& original_values,
    const std::vector<uint64_t>& replacement_values,
    const bool allow_native_changes,
    const PatchRestoreTestConfig& config,
    const PatchRestoreTestResult& reported,
    const char* first_label_override = nullptr,
    const bool first_field_values_redacted = false,
    const bool last_field_values_redacted = false)
{
    const auto unreported = run_patch_restore_test(initial_values,
        original_values, replacement_values, allow_native_changes, config, false,
        first_label_override, first_field_values_redacted,
        last_field_values_redacted);
    require(reported.restored == unreported.restored
            && reported.values == unreported.values
            && reported.events == unreported.events,
        "patch restore report changed result, order, or rollback outcome");
}

void test_audio_journal_commit_policy()
{
    using namespace ff7r::piano::game;

    struct TestCase {
        ff7r::piano::game::AudioJournalCommitEvidence evidence;
        bool expected;
        const char* message;
    };
    const TestCase cases[] = {
        {{true, true, true, true, true, true}, true,
            "complete journal commit evidence was rejected"},
        {{false, true, true, true, true, true}, false,
            "unrestored pending journal permitted commit"},
        {{true, false, true, true, true, true}, false,
            "unrestored failed journal permitted commit"},
        {{true, true, false, true, true, true}, false,
            "unrestored auxiliary journal permitted commit"},
        {{true, true, true, false, true, true}, false,
            "active patch journal permitted commit"},
        {{true, true, true, true, false, true}, false,
            "inactive lease permitted journal commit"},
        {{true, true, true, true, true, false}, false,
            "lease identity mismatch permitted journal commit"},
        {{false, false, false, false, false, false}, false,
            "empty journal commit evidence permitted commit"},
    };
    for (const TestCase& test : cases) {
        require(ff7r::piano::game::audio_journal_commit_ready(test.evidence) == test.expected,
            test.message);
    }
}

void test_audio_patch_restore_failure_report()
{
    using namespace ff7r::piano::game;
    const auto check_observational = [](const std::vector<uint64_t>& initial,
        const std::vector<uint64_t>& original,
        const std::vector<uint64_t>& replacement,
        const bool allow_native_changes,
        const PatchRestoreTestConfig& config,
        const PatchRestoreTestResult& reported) {
        require_restore_report_observational(initial, original, replacement,
            allow_native_changes, config, reported);
    };

    const PatchRestoreTestConfig read_config{{1}, {}, {}};
    const auto read = run_patch_restore_test(
        {20}, {10}, {20}, false, read_config, true);
    require(!read.restored
            && read.report.stage == AudioPatchRestoreFailureStage::FieldRead
            && read.report.category == AudioPatchRestoreFailureCategory::Unclassified
            && !read.report.current_read && read.report.restore_plan_index == 0
            && read.report.offset == 0x38
            && read.report.size == sizeof(uint64_t)
            && std::string(read.report.label) == "patch_0"
            && read.values == std::vector<uint64_t>({20})
            && read.events == std::vector<std::string>({"read:patch_0"}),
        "field-read restore failure report mutated memory or order");
    check_observational({20}, {10}, {20}, false, read_config, read);

    const auto conflict = run_patch_restore_test(
        {30}, {10}, {20}, false, {}, true);
    require(!conflict.restored
            && conflict.report.stage == AudioPatchRestoreFailureStage::ConflictDecision
            && conflict.report.category == AudioPatchRestoreFailureCategory::Conflict
            && conflict.report.current_read && conflict.report.current == 30
            && conflict.report.original == 10 && conflict.report.replacement == 20
            && !conflict.report.allow_native_changes
            && conflict.values == std::vector<uint64_t>({30})
            && conflict.events == std::vector<std::string>({"read:patch_0"}),
        "restore conflict report claimed authority or changed memory");
    check_observational({30}, {10}, {20}, false, {}, conflict);

    const PatchRestoreTestConfig write_config{{}, {1}, {}};
    const auto write = run_patch_restore_test(
        {20}, {10}, {20}, false, write_config, true);
    require(!write.restored
            && write.report.stage == AudioPatchRestoreFailureStage::RestoreWrite
            && write.report.category == AudioPatchRestoreFailureCategory::RestoreOriginal
            && write.report.current_read && write.report.current == 20
            && write.report.restore_original_count == 1
            && write.values == std::vector<uint64_t>({20})
            && write.events == std::vector<std::string>({
                "read:patch_0", "write:patch_0",
                "write:patch_0", "read:patch_0"}),
        "restore-write failure report changed memory or order");
    check_observational({20}, {10}, {20}, false, write_config, write);

    const PatchRestoreTestConfig verify_config{{}, {}, {2}};
    const auto verify = run_patch_restore_test(
        {20}, {10}, {20}, false, verify_config, true);
    require(!verify.restored
            && verify.report.stage == AudioPatchRestoreFailureStage::PostWriteVerify
            && verify.report.current_read && verify.report.current == (10 ^ 0xff)
            && verify.values == std::vector<uint64_t>({20})
            && verify.events == std::vector<std::string>({
                "read:patch_0", "write:patch_0", "read:patch_0",
                "write:patch_0", "read:patch_0"}),
        "post-write verification failure report changed the exact write");
    check_observational({20}, {10}, {20}, false, verify_config, verify);

    const PatchRestoreTestConfig rollback_write_config{{}, {2, 3}, {}};
    const auto rollback_write = run_patch_restore_test(
        {20, 40}, {10, 30}, {20, 40}, false, rollback_write_config, true);
    require(!rollback_write.restored
            && rollback_write.report.stage == AudioPatchRestoreFailureStage::RestoreWrite
            && rollback_write.report.restore_plan_index == 1
            && rollback_write.report.rollback_stage == AudioPatchRollbackFailureStage::RestoreWrite
            && std::string(rollback_write.report.rollback_label) == "patch_0"
            && rollback_write.report.rollback_plan_index == 1
            && rollback_write.report.rollback_inspected_value == 20
            && rollback_write.values == std::vector<uint64_t>({20, 40})
            && rollback_write.events == std::vector<std::string>({
                "read:patch_1", "read:patch_0", "write:patch_1",
                "read:patch_1", "write:patch_0", "write:patch_0",
                "read:patch_0", "write:patch_1", "read:patch_1"}),
        "rollback-write failure was not retained after the first failure");
    check_observational({20, 40}, {10, 30}, {20, 40}, false,
        rollback_write_config, rollback_write);

    const PatchRestoreTestConfig rollback_verify_config{{}, {2}, {4}};
    const auto rollback_verify = run_patch_restore_test(
        {20, 40}, {10, 30}, {20, 40}, false, rollback_verify_config, true);
    require(!rollback_verify.restored
            && rollback_verify.report.rollback_stage
                == AudioPatchRollbackFailureStage::PostWriteVerify
            && rollback_verify.report.rollback_current_read
            && rollback_verify.values == std::vector<uint64_t>({20, 40}),
        "rollback post-write verification failure was not captured");
    check_observational({20, 40}, {10, 30}, {20, 40}, false,
        rollback_verify_config, rollback_verify);

    const auto success = run_patch_restore_test(
        {20, 50, 30}, {10, 40, 30}, {20, 45, 35}, true, {}, true);
    require(success.restored && !success.report.failed()
            && success.report.already_restored_count == 1
            && success.report.restore_original_count == 1
            && success.report.preserve_native_value_count == 1
            && success.values == std::vector<uint64_t>({10, 50, 30})
            && success.events == std::vector<std::string>({
                "read:patch_2", "read:patch_1", "preserve:patch_1",
                "read:patch_0", "write:patch_0", "read:patch_0"}),
        "classification counts changed reverse inspection or restore order");
    check_observational({20, 50, 30}, {10, 40, 30}, {20, 45, 35},
        true, {}, success);

    const PatchRestoreTestConfig forward_failure_config{{}, {3}, {}};
    const auto forward_failure = run_patch_restore_test(
        {20, 40, 60}, {10, 30, 50}, {20, 40, 60}, false,
        forward_failure_config, true);
    require(!forward_failure.restored && !forward_failure.report.rollback_failed()
            && forward_failure.values == std::vector<uint64_t>({20, 40, 60})
            && forward_failure.events == std::vector<std::string>({
                "read:patch_2", "read:patch_1", "read:patch_0",
                "write:patch_2", "read:patch_2",
                "write:patch_1", "read:patch_1", "write:patch_0",
                "write:patch_0", "read:patch_0",
                "write:patch_1", "read:patch_1",
                "write:patch_2", "read:patch_2"}),
        "three-field forward failure did not complete reverse rollback");
    check_observational({20, 40, 60}, {10, 30, 50}, {20, 40, 60}, false,
        forward_failure_config, forward_failure);

    const PatchRestoreTestConfig continued_rollback_write_config{
        {}, {3, 4}, {}};
    const auto continued_rollback_write = run_patch_restore_test(
        {20, 40, 60}, {10, 30, 50}, {20, 40, 60}, false,
        continued_rollback_write_config, true);
    require(!continued_rollback_write.restored
            && continued_rollback_write.report.rollback_stage
                == AudioPatchRollbackFailureStage::RestoreWrite
            && std::string(continued_rollback_write.report.rollback_label)
                == "patch_0"
            && continued_rollback_write.values
                == std::vector<uint64_t>({20, 40, 60})
            && continued_rollback_write.events == std::vector<std::string>({
                "read:patch_2", "read:patch_1", "read:patch_0",
                "write:patch_2", "read:patch_2",
                "write:patch_1", "read:patch_1", "write:patch_0",
                "write:patch_0", "read:patch_0",
                "write:patch_1", "read:patch_1",
                "write:patch_2", "read:patch_2"}),
        "first rollback write failure stopped a later rollback attempt");
    check_observational({20, 40, 60}, {10, 30, 50}, {20, 40, 60}, false,
        continued_rollback_write_config, continued_rollback_write);

    const PatchRestoreTestConfig continued_rollback_read_config{
        {6}, {3}, {}};
    const auto continued_rollback_read = run_patch_restore_test(
        {20, 40, 60}, {10, 30, 50}, {20, 40, 60}, false,
        continued_rollback_read_config, true);
    require(!continued_rollback_read.restored
            && continued_rollback_read.report.rollback_stage
                == AudioPatchRollbackFailureStage::PostWriteVerify
            && !continued_rollback_read.report.rollback_current_read
            && continued_rollback_read.values
                == std::vector<uint64_t>({20, 40, 60})
            && continued_rollback_read.events == std::vector<std::string>({
                "read:patch_2", "read:patch_1", "read:patch_0",
                "write:patch_2", "read:patch_2",
                "write:patch_1", "read:patch_1", "write:patch_0",
                "write:patch_0", "read:patch_0",
                "write:patch_1", "read:patch_1",
                "write:patch_2", "read:patch_2"}),
        "rollback verification read failure stopped a later rollback attempt");
    check_observational({20, 40, 60}, {10, 30, 50}, {20, 40, 60}, false,
        continued_rollback_read_config, continued_rollback_read);

    char unterminated_label[kAudioPatchRestoreReportLabelCapacity];
    std::fill_n(unterminated_label,
        kAudioPatchRestoreReportLabelCapacity, 'x');
    const auto bounded_label = run_patch_restore_test(
        {20}, {10}, {20}, false, read_config, true, unterminated_label);
    require(!bounded_label.restored
            && bounded_label.report.label[
                kAudioPatchRestoreReportLabelCapacity - 1] == '\0'
            && std::string(bounded_label.report.label)
                == std::string(kAudioPatchRestoreReportLabelCapacity - 1, 'x')
            && bounded_label.events
                == std::vector<std::string>({"read:patch_0"}),
        "restore report label copy was not strictly capacity bounded");
    require_restore_report_observational({20}, {10}, {20}, false,
        read_config, bounded_label, unterminated_label);

    const auto redacted = run_patch_restore_test(
        {30}, {10}, {20}, false, {}, true, nullptr, true);
    require(!redacted.restored && redacted.report.values_redacted
            && redacted.report.stage
                == AudioPatchRestoreFailureStage::ConflictDecision
            && redacted.report.current == 0
            && redacted.report.original == 0
            && redacted.report.replacement == 0,
        "pointer-bearing primary report retained redacted values");
    require_restore_report_observational({30}, {10}, {20}, false,
        {}, redacted, nullptr, true);

    const PatchRestoreTestConfig redacted_rollback_config{{}, {2, 3}, {}};
    const auto redacted_rollback = run_patch_restore_test(
        {20, 40}, {10, 30}, {20, 40}, false,
        redacted_rollback_config, true, nullptr, true, true);
    require(!redacted_rollback.restored
            && redacted_rollback.report.rollback_values_redacted
            && redacted_rollback.report.rollback_current == 0
            && redacted_rollback.report.rollback_original == 0
            && redacted_rollback.report.rollback_replacement == 0,
        "pointer-bearing rollback report retained redacted values");
    require_restore_report_observational({20, 40}, {10, 30}, {20, 40}, false,
        redacted_rollback_config, redacted_rollback, nullptr, true, true);
}

void test_native_handoff_cleanup_policy()
{
    using namespace ff7r::piano::game;

    const AudioRouteLeaseIdentity identity{90, 0xabc123};
    const uint64_t owned_generation = 17;
    const uint64_t set_generation = owned_generation + 1;
    const auto pointer = [](uintptr_t value) { return reinterpret_cast<void*>(value); };
    const auto request_handle = [](uint32_t generation, uint16_t pool_index = 0) {
        return (static_cast<uint64_t>(generation) << 32)
            | (static_cast<uint64_t>(pool_index) << 16) | 8ull;
    };
    const AudioBgmRequestHandle logged_handle{0x600010008ull};
    require(logged_handle.valid_bgm_request() && logged_handle.type() == 8
            && logged_handle.pool_index() == 1 && logged_handle.generation() == 6,
        "shipping BGM +0x48 packed request-handle layout was treated as a pointer");
    const AudioNativeRouteObservation owned{
        pointer(0x1000), pointer(0x2000), pointer(0x3000), request_handle(4),
        4, true, owned_generation, identity,
    };
    AudioNativeRouteObservation current{
        owned.slot, owned.bgm, pointer(0x5000), request_handle(5),
        2, true, set_generation, identity,
    };

    require(evaluate_audio_native_handoff(owned, current, current.sound, set_generation)
            == AudioNativeHandoffDecision::NativeReplacementProven,
        "native Set with a new sound and resource was not accepted");
    require(native_route_replacement_proven(owned, current, current.sound, set_generation),
        "generation-bound native replacement proof failed");

    current.request_handle = owned.request_handle;
    require(evaluate_audio_native_handoff(owned, current, current.sound, set_generation)
            == AudioNativeHandoffDecision::ReleaseBeforeNativePlay,
        "logged native Set resource reuse was not quarantined before Play");
    require(!native_route_replacement_proven(owned, current, current.sound, set_generation),
        "custom resource reuse was accepted as native replacement");

    current.sound = owned.sound;
    current.state = 4;
    require(evaluate_audio_native_handoff(owned, current, current.sound, set_generation)
            == AudioNativeHandoffDecision::OwnedCustomRoute,
        "unchanged custom route was not retained for explicit list cleanup");
    current.request_handle = request_handle(5);
    require(evaluate_audio_native_handoff(owned, current, current.sound, set_generation)
            == AudioNativeHandoffDecision::AmbiguousRetainAndBlock,
        "reused sound pointer with a changed resource was treated as a safe replacement");

    current.sound = pointer(0x5000);
    current.route_generation = set_generation + 1;
    require(evaluate_audio_native_handoff(owned, current, current.sound, set_generation)
            == AudioNativeHandoffDecision::AmbiguousRetainAndBlock,
        "generation drift did not block the native handoff");
    current.route_generation = set_generation;
    current.lease_identity.song_key ^= 1;
    require(evaluate_audio_native_handoff(owned, current, current.sound, set_generation)
            == AudioNativeHandoffDecision::AmbiguousRetainAndBlock,
        "song identity drift did not block the native handoff");

    AudioNativeRouteObservation cleared{
        owned.slot, owned.bgm, nullptr, 0, 0, true, set_generation, identity,
    };
    require(native_route_release_proven(owned, cleared, set_generation),
        "exact Set(nullptr) release was not verified");
    cleared.request_handle = owned.request_handle;
    require(!native_route_release_proven(owned, cleared, set_generation),
        "retained custom resource passed release verification");
    cleared.request_handle = 0;
    cleared.route_generation++;
    require(!native_route_release_proven(owned, cleared, set_generation),
        "release verification accepted generation drift");
    cleared.route_generation = set_generation;

    FrozenProfileLeaseState lease;
    lease.acquire(identity);
    require(lease.mark_native_arm_attempt(identity),
        "custom PlaySetup attempt was not attached to the handoff lease");
    AudioCleanupEvidence evidence;
    evidence.immutable_identity_matches = true;
    evidence.native_attempted = true;
    evidence.ambiguous_route_state = true;
    AudioRouteCleanupResult result = apply_audio_cleanup(lease, identity, evidence);
    require(result.status == AudioRouteCleanupStatus::Retained
            && lease.active() && !result.thaw_profile,
        "failed native replacement thawed the custom profile");

    evidence.native_clear_verified = true;
    evidence.route_state_unchanged = true;
    result = apply_audio_cleanup(lease, identity, evidence);
    require(result.status == AudioRouteCleanupStatus::Released
            && result.thaw_profile && result.clear_route_metadata && !lease.active(),
        "verified list-return release did not atomically thaw the profile");
    result = apply_audio_cleanup(lease, identity, evidence);
    require(result.status == AudioRouteCleanupStatus::NoAction
            && !result.thaw_profile && !result.clear_route_metadata,
        "verified cleanup retry was not idempotent");
}

void test_deferred_native_handoff_state_machine()
{
    using namespace ff7r::piano::game;

    const auto pointer = [](uintptr_t value) { return reinterpret_cast<void*>(value); };
    const auto request_handle = [](uint32_t generation, uint16_t pool_index = 0) {
        return (static_cast<uint64_t>(generation) << 32)
            | (static_cast<uint64_t>(pool_index) << 16) | 8ull;
    };
    const AudioRouteLeaseIdentity identity{91, 0x520};
    const uint64_t owned_generation = 40;
    const uint64_t handoff_generation = owned_generation + 1;
    const AudioNativeRouteObservation custom{
        pointer(0x1000), pointer(0x2000), pointer(0x3000), request_handle(4),
        4, true, owned_generation, identity,
    };
    const AudioNativeRouteObservation cleared{
        custom.slot, custom.bgm, nullptr, 0, 0, true,
        handoff_generation, identity,
    };
    void* controller = pointer(0x9000);
    void* requested_sound = pointer(0x5000);

    AudioDeferredNativeHandoffState handoff;
    require(begin_deferred_native_handoff(
                handoff, custom, controller, requested_sound,
                handoff_generation)
            && handoff.phase == AudioDeferredNativeHandoffPhase::SetCaptured,
        "v38 native Set intent was not captured before cleanup");
    AudioNativeRouteObservation premature_native{
        custom.slot, custom.bgm, requested_sound, request_handle(5), 2, true,
        handoff_generation, identity,
    };
    require(!record_deferred_native_set(handoff, premature_native)
            && evaluate_deferred_native_handoff_action(handoff, premature_native)
                == AudioDeferredNativeHandoffAction::Retain,
        "native Set/Play became eligible before custom route release");
    require(record_deferred_native_clear(handoff, custom, cleared)
            && handoff.phase == AudioDeferredNativeHandoffPhase::CustomCleared,
        "exact custom route clear did not advance deferred handoff");

    AudioNativeRouteObservation missing_native = cleared;
    require(!record_deferred_native_set(handoff, missing_native)
            && handoff.phase == AudioDeferredNativeHandoffPhase::CustomCleared,
        "failed native Set did not remain retryable after cleanup");
    require(!deferred_native_play_ready(handoff, missing_native),
        "Play was allowed while reconstructed native Set was pending");
    int native_set_forward_count = 0;
    if (evaluate_deferred_native_handoff_action(handoff, missing_native)
        == AudioDeferredNativeHandoffAction::ApplyNativeSet) {
        ++native_set_forward_count;
    }

    AudioNativeRouteObservation native_route{
        custom.slot, custom.bgm, requested_sound, request_handle(5), 2, true,
        handoff_generation, identity,
    };
    require(record_deferred_native_set(handoff, native_route)
            && handoff.phase == AudioDeferredNativeHandoffPhase::NativeSetApplied
            && evaluate_deferred_native_handoff_action(handoff, native_route)
                == AudioDeferredNativeHandoffAction::ForwardNativePlay,
        "deferred native Set retry did not reconstruct the requested route");
    int native_play_forward_count = 0;
    if (evaluate_deferred_native_handoff_action(handoff, native_route)
        == AudioDeferredNativeHandoffAction::ForwardNativePlay) {
        ++native_play_forward_count;
    }
    AudioNativeRouteObservation played_native = native_route;
    played_native.state = 4;
    require(record_deferred_native_play(handoff, played_native)
            && handoff.phase == AudioDeferredNativeHandoffPhase::NativePlayForwarded,
        "blocked native Play was not eventually recorded as forwarded");
    require(native_set_forward_count == 1 && native_play_forward_count == 1,
        "successful cleanup did not eventually forward one native Set and Play");
    require(played_native.sound == requested_sound
            && played_native.request_handle == native_route.request_handle
            && played_native.request_handle != custom.request_handle,
        "Vanilla playback did not retain its reconstructed native sound/request handle");

    AudioDeferredNativeHandoffState reused_resource;
    require(begin_deferred_native_handoff(
                reused_resource, custom, controller, requested_sound,
                handoff_generation)
            && record_deferred_native_clear(reused_resource, custom, cleared),
        "resource-reuse setup did not clear custom ownership first");
    native_route.request_handle = custom.request_handle;
    require(record_deferred_native_set(reused_resource, native_route),
        "allocator pointer reuse after verified null clear was rejected");
    played_native.request_handle = custom.request_handle;
    require(record_deferred_native_play(reused_resource, played_native),
        "resource pointer reuse prevented eventual native Play forwarding");

    AudioDeferredNativeHandoffState reused_sound;
    require(begin_deferred_native_handoff(
                reused_sound, custom, controller, custom.sound,
                handoff_generation)
            && record_deferred_native_clear(reused_sound, custom, cleared),
        "sound-pointer reuse setup did not clear custom ownership first");
    AudioNativeRouteObservation reused_sound_route{
        custom.slot, custom.bgm, custom.sound, request_handle(7), 2, true,
        handoff_generation, identity,
    };
    require(record_deferred_native_set(reused_sound, reused_sound_route),
        "verified sound-pointer reuse did not restore the native route");
    reused_sound_route.state = 4;
    require(record_deferred_native_play(reused_sound, reused_sound_route),
        "verified sound-pointer reuse did not forward native Play");

    AudioDeferredNativeHandoffState regressed;
    require(begin_deferred_native_handoff(
                regressed, custom, controller, requested_sound,
                handoff_generation)
            && record_deferred_native_clear(regressed, custom, cleared),
        "post-Play regression setup failed");
    native_route.request_handle = request_handle(5);
    require(record_deferred_native_set(regressed, native_route),
        "post-Play regression native Set setup failed");
    played_native.request_handle = custom.request_handle;
    require(!record_deferred_native_play(regressed, played_native),
        "post-Play custom resource regression was accepted");

    AudioDeferredNativeHandoffState ambiguous;
    AudioNativeRouteObservation wrong_clear = cleared;
    wrong_clear.slot = pointer(0x1010);
    require(begin_deferred_native_handoff(
                ambiguous, custom, controller, requested_sound,
                handoff_generation)
            && !record_deferred_native_clear(ambiguous, custom, wrong_clear)
            && ambiguous.phase == AudioDeferredNativeHandoffPhase::RetainedFailure,
        "ambiguous clear did not retain and fail closed");
    require(!deferred_native_play_ready(ambiguous, native_route),
        "ambiguous handoff allowed native Play");

    // Fresh integrated trace: Set(nullptr) reached state 0 but cleanup proof
    // failed, so the exact native Set/Play intent must still be forwarded while
    // the cleanup lease remains retained.
    AudioDeferredNativeHandoffState logged_failure;
    std::string native_calls;
    require(begin_deferred_native_handoff(
                logged_failure, custom, controller, requested_sound,
                handoff_generation),
        "fresh-log native Set intent capture failed");
    native_calls += "Set(null);";
    AudioNativeRouteObservation unverified_clear = cleared;
    unverified_clear.request_handle = custom.request_handle;
    require(!record_deferred_native_clear(
                logged_failure, custom, unverified_clear)
            && logged_failure.phase == AudioDeferredNativeHandoffPhase::RetainedFailure,
        "fresh-log clear failure was not retained");
    native_calls += "Set(requested);";
    AudioNativeRouteObservation recovered_native{
        custom.slot, custom.bgm, requested_sound, request_handle(6, 1), 4, true,
        handoff_generation, identity,
    };
    require(evaluate_deferred_native_handoff_action(
                logged_failure, recovered_native)
            == AudioDeferredNativeHandoffAction::ForwardNativePlayRetainingFailure,
        "correctly reconstructed native route did not recover retained Play");
    native_calls += "Play;";
    require(native_calls == "Set(null);Set(requested);Play;",
        "fresh-log recovery did not preserve exact native call order");
    require(logged_failure.phase == AudioDeferredNativeHandoffPhase::RetainedFailure,
        "unverified recovery incorrectly committed cleanup");

    AudioNativeRouteObservation failed_reconstruction = recovered_native;
    failed_reconstruction.sound = nullptr;
    require(evaluate_deferred_native_handoff_action(
                logged_failure, failed_reconstruction)
            == AudioDeferredNativeHandoffAction::Retain,
        "failed native reconstruction forwarded Play");
    AudioNativeRouteObservation invalid_packed_handle = recovered_native;
    invalid_packed_handle.request_handle = (invalid_packed_handle.request_handle & ~0xffull) | 7ull;
    require(evaluate_deferred_native_handoff_action(
                logged_failure, invalid_packed_handle)
            == AudioDeferredNativeHandoffAction::Retain,
        "non-BGM packed request handle was accepted for native Play recovery");
    AudioNativeRouteObservation stale_reconstruction = recovered_native;
    ++stale_reconstruction.route_generation;
    require(evaluate_deferred_native_handoff_action(
                logged_failure, stale_reconstruction)
            == AudioDeferredNativeHandoffAction::Retain,
        "stale-generation recovery forwarded Play");
    require(evaluate_deferred_native_handoff_action(
                logged_failure, recovered_native)
            == AudioDeferredNativeHandoffAction::ForwardNativePlayRetainingFailure,
        "repeated retained transition lost a recoverable native Play intent");
    require(!bypass_audio_native_detour(0)
            && bypass_audio_native_detour(1)
            && bypass_audio_native_detour(2),
        "recursive native replay did not bypass Set/Play detours");

    AudioDeferredNativeHandoffState retry_transition;
    require(begin_deferred_native_handoff(
                retry_transition, custom, controller, requested_sound,
                handoff_generation)
            && retry_transition.phase == AudioDeferredNativeHandoffPhase::SetCaptured,
        "subsequent native transition could not retry retained cleanup");

    FrozenProfileLeaseState lease;
    lease.acquire(identity);
    require(lease.mark_native_arm_attempt(identity),
        "deferred handoff cleanup lease was not marked attempted");
    AudioCleanupEvidence evidence;
    evidence.immutable_identity_matches = true;
    evidence.native_attempted = true;
    evidence.native_clear_verified = true;
    evidence.route_state_unchanged = true;
    AudioRouteCleanupResult cleanup = apply_audio_cleanup(lease, identity, evidence);
    require(cleanup.status == AudioRouteCleanupStatus::Released
            && cleanup.thaw_profile && !lease.active(),
        "forwarded deferred native route did not release its profile");
    cleanup = apply_audio_cleanup(lease, identity, evidence);
    require(cleanup.status == AudioRouteCleanupStatus::NoAction
            && !cleanup.thaw_profile,
        "repeated list return was not idempotent");
}

void test_audio_stop_retirement_monitor()
{
    using namespace ff7r::piano::game;

    const auto pointer = [](uintptr_t value) { return reinterpret_cast<void*>(value); };
    const AudioRouteLeaseIdentity identity{101, 0xabc};
    AudioStopRetirementObservation observation;
    observation.valid = true;
    observation.route_generation = 70;
    observation.lease_identity = identity;
    observation.controller = pointer(0x1000);
    observation.slot = pointer(0x2000);
    observation.bgm = pointer(0x3000);
    observation.custom_sound = pointer(0x4000);
    observation.request_handle = 0x400010008ull;
    observation.request_handle_retired = true;
    observation.retired_count = 1;

    AudioStopRetirementState monitor;
    require(begin_audio_stop_retirement_monitor(monitor, observation)
            && monitor.phase == AudioStopRetirementPhase::Waiting,
        "asynchronous native Stop did not bind the retired custom request");
    require(advance_audio_stop_retirement_monitor(monitor, observation, 4)
                == AudioStopRetirementPhase::Waiting
            && monitor.poll_count == 1,
        "first delayed native retirement poll did not remain waiting");
    observation.retired_count = 2;
    require(advance_audio_stop_retirement_monitor(monitor, observation, 4)
                == AudioStopRetirementPhase::Waiting
            && monitor.poll_count == 2
            && monitor.request_handle == 0x400010008ull,
        "unrelated retired entries or repeated Set updates replaced the tracked custom handle");

    AudioStopRetirementState delayed_92;
    require(begin_audio_stop_retirement_monitor(delayed_92, observation),
        "92-poll retirement monitor setup failed");
    for (uint32_t poll = 0; poll < 92; ++poll) {
        require(advance_audio_stop_retirement_monitor(delayed_92, observation, 300)
                == AudioStopRetirementPhase::Waiting,
            "exact log chronology retired the custom handle before poll 92");
    }

    AudioStopRetirementObservation quiescent = observation;
    quiescent.request_handle_retired = false;
    quiescent.retired_count = 1;
    quiescent.current_sound = pointer(0x5000);
    quiescent.current_request_handle = 0x500000008ull;
    quiescent.current_state = 4;
    require(advance_audio_stop_retirement_monitor(monitor, quiescent, 4)
                == AudioStopRetirementPhase::Quiescent,
        "native tick removal of the custom handle was not accepted as quiescence");
    require(advance_audio_stop_retirement_monitor(monitor, observation, 4)
                == AudioStopRetirementPhase::Quiescent,
        "terminal quiescence regressed on an idempotent retry");
    require(advance_audio_stop_retirement_monitor(delayed_92, quiescent, 300)
                == AudioStopRetirementPhase::Quiescent
            && delayed_92.poll_count == 92,
        "exact 92-poll retirement chronology did not reach quiescence");

    AudioNativeRouteObservation current_native;
    current_native.slot = observation.slot;
    current_native.bgm = observation.bgm;
    current_native.sound = quiescent.current_sound;
    current_native.request_handle = quiescent.current_request_handle;
    current_native.state = 4;
    current_native.valid = true;
    current_native.route_generation = 71;
    current_native.lease_identity = identity;
    AudioRetirementCleanupEvidence retirement_cleanup{
        delayed_92,
        quiescent,
        current_native,
        true,
        true,
        true,
        true,
    };
    require(audio_retirement_cleanup_proven(retirement_cleanup),
        "exact retired-handle disappearance did not authorize metadata-only cleanup");
    require(audio_retirement_route_authority_proven({
                AudioRetirementRouteAuthority::DirectSuccessor,
                false, 12, 13, 0, 0, 11, 0})
            && !audio_retirement_route_authority_proven({
                AudioRetirementRouteAuthority::DirectSuccessor,
                false, 12, 16, 0, 0, 11, 0})
            && audio_retirement_route_authority_proven({
                AudioRetirementRouteAuthority::TerminalAggregateHandoff,
                true, 12, 16, 16, 0x600000008ull, 11, 0x400010008ull})
            && !audio_retirement_route_authority_proven({
                AudioRetirementRouteAuthority::TerminalAggregateHandoff,
                false, 12, 16, 16, 0x600000008ull, 11, 0x400010008ull}),
        "authenticated direct/terminal retirement route authority drifted");
    const AudioRetirementRouteAuthorityFacts terminal_route{
        AudioRetirementRouteAuthority::TerminalAggregateHandoff,
        true, 12, 16, 16, 0x600000008ull, 11, 0x400010008ull};
    auto reject_terminal_route = [&](AudioRetirementRouteAuthorityFacts drifted,
                                     const char* message) {
        require(!audio_retirement_route_authority_proven(drifted), message);
    };
    auto drifted_terminal_route = terminal_route;
    drifted_terminal_route.monitor_route_generation = 0;
    reject_terminal_route(drifted_terminal_route,
        "zero monitor generation authenticated terminal cleanup");
    drifted_terminal_route = terminal_route;
    drifted_terminal_route.live_route_generation = 15;
    reject_terminal_route(drifted_terminal_route,
        "live route not owned by terminal leaf authenticated cleanup");
    drifted_terminal_route = terminal_route;
    drifted_terminal_route.terminal_leaf_request_handle = 0;
    reject_terminal_route(drifted_terminal_route,
        "missing terminal leaf request authenticated cleanup");
    drifted_terminal_route = terminal_route;
    drifted_terminal_route.monitor_request_handle = 0;
    reject_terminal_route(drifted_terminal_route,
        "missing monitor root request authenticated terminal cleanup");
    drifted_terminal_route = terminal_route;
    drifted_terminal_route.monitor_request_handle =
        terminal_route.terminal_leaf_request_handle;
    reject_terminal_route(drifted_terminal_route,
        "identical root and leaf requests authenticated terminal cleanup");
    drifted_terminal_route = terminal_route;
    drifted_terminal_route.frozen_route_generation = 10;
    reject_terminal_route(drifted_terminal_route,
        "frozen route outside monitor predecessor authenticated cleanup");
    AudioRetirementCleanupEvidence terminal_cleanup = retirement_cleanup;
    terminal_cleanup.current_route.route_generation = 74;
    terminal_cleanup.route_authority = {
        AudioRetirementRouteAuthority::TerminalAggregateHandoff,
        true,
        delayed_92.route_generation,
        74,
        74,
        0x600000008ull,
        delayed_92.route_generation - 1,
        delayed_92.request_handle,
    };
    require(audio_retirement_cleanup_proven(terminal_cleanup),
        "authenticated terminal aggregate handoff did not authorize cleanup");
    constexpr uint64_t terminal_root_request = 0xa00010008ull;
    constexpr uint64_t terminal_leaf_request = 0xe00010008ull;
    require(audio_retirement_owned_request_identity_exact(
                terminal_root_request, terminal_root_request)
            && !audio_retirement_owned_request_identity_exact(
                terminal_root_request, terminal_leaf_request)
            && !audio_retirement_owned_request_identity_exact(0, 0)
            && !audio_retirement_owned_request_identity_exact(
                terminal_root_request, 0xc00010008ull),
        "root-owned retirement request identity was not fail closed");
    const AudioRetirementOwnedRouteIdentityFacts owned_route_identity{
        pointer(0x2000), pointer(0x3000), pointer(0x4000),
        terminal_root_request,
        pointer(0x2000), pointer(0x3000), pointer(0x4000),
        terminal_root_request,
    };
    require(audio_retirement_owned_route_identity_exact(owned_route_identity),
        "exact owned slot/BGM/sound/root-request identity was rejected");
    auto reject_owned_route = [&](AudioRetirementOwnedRouteIdentityFacts drifted,
                                  const char* message) {
        require(!audio_retirement_owned_route_identity_exact(drifted), message);
    };
    auto drifted_owned_route = owned_route_identity;
    drifted_owned_route.owned_slot = pointer(0x2001);
    reject_owned_route(drifted_owned_route,
        "owned slot drift passed retirement commit identity");
    drifted_owned_route = owned_route_identity;
    drifted_owned_route.owned_bgm = pointer(0x3001);
    reject_owned_route(drifted_owned_route,
        "owned BGM drift passed retirement commit identity");
    drifted_owned_route = owned_route_identity;
    drifted_owned_route.owned_sound = pointer(0x4001);
    reject_owned_route(drifted_owned_route,
        "owned sound drift passed retirement commit identity");
    drifted_owned_route = owned_route_identity;
    drifted_owned_route.owned_request = terminal_leaf_request;
    reject_owned_route(drifted_owned_route,
        "leaf-only owned request passed root retirement commit identity");
    require(audio_retirement_route_authority_proven({
            AudioRetirementRouteAuthority::TerminalAggregateHandoff,
            true, 12, 16, 16, terminal_leaf_request, 11,
            terminal_root_request}),
        "exact session root/leaf request lineage was not authenticated");
    AudioRetirementOnMemoryRequestProjectionFacts release_request_facts;
    release_request_facts.retirement_request_handle = terminal_root_request;
    release_request_facts.route_owned_request_handle = terminal_root_request;
    release_request_facts.route_authority = {
        AudioRetirementRouteAuthority::TerminalAggregateHandoff,
        true, 12, 16, 16, terminal_leaf_request, 11,
        terminal_root_request,
    };
    const auto projected_release_request =
        project_audio_retirement_onmemory_request(release_request_facts);
    require(projected_release_request.accepted
            && projected_release_request.retired_request_handle
                == terminal_root_request
            && projected_release_request.retired_request_handle
                != terminal_leaf_request,
        "terminal leaf request replaced root request in release facts");
    auto reject_release_request = [&] (
        AudioRetirementOnMemoryRequestProjectionFacts drifted,
        const char* message) {
        const auto projection =
            project_audio_retirement_onmemory_request(drifted);
        require(!projection.accepted
                && projection.retired_request_handle == 0,
            message);
    };
    auto drifted_release_request = release_request_facts;
    drifted_release_request.route_owned_request_handle = terminal_leaf_request;
    reject_release_request(drifted_release_request,
        "leaf-only request selected for OnMemory retirement");
    drifted_release_request = release_request_facts;
    drifted_release_request.route_owned_request_handle = 0xc00010008ull;
    reject_release_request(drifted_release_request,
        "unrelated request selected for OnMemory retirement");
    drifted_release_request = release_request_facts;
    drifted_release_request.retirement_request_handle = 0;
    reject_release_request(drifted_release_request,
        "zero root request selected for OnMemory retirement");
    drifted_release_request = release_request_facts;
    drifted_release_request.route_owned_request_handle = 0;
    reject_release_request(drifted_release_request,
        "zero owned request selected for OnMemory retirement");
    drifted_release_request = release_request_facts;
    drifted_release_request.route_authority.monitor_request_handle =
        terminal_leaf_request;
    reject_release_request(drifted_release_request,
        "leaf monitor request selected for OnMemory retirement");
    terminal_cleanup.route_authority.terminal_handoff_authenticated = false;
    require(!audio_retirement_cleanup_proven(terminal_cleanup),
        "caller-selected terminal route arithmetic authorized cleanup");
    require(current_native.sound == pointer(0x5000)
            && current_native.request_handle == 0x500000008ull
            && current_native.state == 4,
        "metadata-only cleanup policy mutated the current native route");
    FrozenProfileLeaseState committed_lease;
    committed_lease.acquire(identity);
    require(committed_lease.mark_native_arm_attempt(identity),
        "retirement cleanup lease was not marked attempted");
    AudioCleanupEvidence committed_evidence;
    committed_evidence.immutable_identity_matches = true;
    committed_evidence.native_attempted = true;
    committed_evidence.native_clear_verified = true;
    committed_evidence.route_state_unchanged = true;
    const AudioRouteCleanupResult first_commit = apply_audio_cleanup(
        committed_lease, identity, committed_evidence);
    const AudioRouteCleanupResult repeated_list_return = apply_audio_cleanup(
        committed_lease, identity, committed_evidence);
    require(first_commit.status == AudioRouteCleanupStatus::Released
            && first_commit.thaw_profile
            && repeated_list_return.status == AudioRouteCleanupStatus::NoAction
            && !repeated_list_return.thaw_profile,
        "retirement cleanup or repeated list return was not idempotent");

    AudioRetirementCleanupEvidence rejected = retirement_cleanup;
    rejected.current_route.route_generation = 72;
    require(!audio_retirement_cleanup_proven(rejected),
        "generation drift authorized retirement cleanup");
    rejected = retirement_cleanup;
    rejected.current_route.request_handle = observation.request_handle;
    rejected.retirement_observation.current_request_handle = observation.request_handle;
    require(!audio_retirement_cleanup_proven(rejected),
        "current handle reuse of the retired custom request authorized cleanup");
    rejected = retirement_cleanup;
    rejected.current_route.sound = observation.custom_sound;
    rejected.retirement_observation.current_sound = observation.custom_sound;
    require(!audio_retirement_cleanup_proven(rejected),
        "current custom-sound regression authorized cleanup");
    rejected = retirement_cleanup;
    rejected.retirement_observation.valid = false;
    require(!audio_retirement_cleanup_proven(rejected),
        "retired-vector read failure authorized cleanup");
    rejected = retirement_cleanup;
    rejected.all_journals_restored = false;
    require(!audio_retirement_cleanup_proven(rejected),
        "pending patch journal authorized retirement cleanup");
    rejected = retirement_cleanup;
    rejected.custom_sound_fields_restored = false;
    require(!audio_retirement_cleanup_proven(rejected),
        "unrestored custom sound fields authorized retirement cleanup");

    AudioRetirementCleanupEvidence empty_cleanup = retirement_cleanup;
    empty_cleanup.retirement_observation.current_sound = nullptr;
    empty_cleanup.retirement_observation.current_request_handle = 0;
    empty_cleanup.retirement_observation.current_state = 0;
    empty_cleanup.current_route.sound = nullptr;
    empty_cleanup.current_route.request_handle = 0;
    empty_cleanup.current_route.state = 0;
    empty_cleanup.current_native_identity_matches = false;
    require(audio_retirement_cleanup_proven(empty_cleanup),
        "verified empty native route did not authorize metadata-only cleanup");

    AudioStopRetirementState timeout;
    require(begin_audio_stop_retirement_monitor(timeout, observation),
        "timeout monitor setup failed");
    require(advance_audio_stop_retirement_monitor(timeout, observation, 2)
                == AudioStopRetirementPhase::Waiting
            && advance_audio_stop_retirement_monitor(timeout, observation, 2)
                == AudioStopRetirementPhase::TimedOut,
        "bounded native Stop retirement timeout did not fail closed");
    AudioRetirementCleanupEvidence timed_out_cleanup = retirement_cleanup;
    timed_out_cleanup.retirement = timeout;
    require(!audio_retirement_cleanup_proven(timed_out_cleanup),
        "timed-out retirement authorized metadata cleanup");

    AudioStopRetirementState stale;
    require(begin_audio_stop_retirement_monitor(stale, observation),
        "stale-generation monitor setup failed");
    AudioStopRetirementObservation drifted = observation;
    ++drifted.route_generation;
    require(advance_audio_stop_retirement_monitor(stale, drifted, 4)
                == AudioStopRetirementPhase::Invalid,
        "generation drift did not invalidate native Stop retirement evidence");
    AudioRetirementCleanupEvidence invalid_cleanup = retirement_cleanup;
    invalid_cleanup.retirement = stale;
    require(!audio_retirement_cleanup_proven(invalid_cleanup),
        "invalid retirement authorized metadata cleanup");

    AudioStopRetirementState recursive;
    AudioStopRetirementObservation wrong_handle = observation;
    wrong_handle.request_handle = 0x500000008ull;
    require(begin_audio_stop_retirement_monitor(recursive, observation)
            && advance_audio_stop_retirement_monitor(recursive, wrong_handle, 4)
                == AudioStopRetirementPhase::Invalid,
        "request-handle regression was accepted during recursive polling");

    AudioStopRetirementState not_retired;
    AudioStopRetirementObservation synchronous = observation;
    synchronous.request_handle_retired = false;
    require(!begin_audio_stop_retirement_monitor(not_retired, synchronous)
            && not_retired.phase == AudioStopRetirementPhase::None,
        "synchronous Stop incorrectly started an asynchronous retirement monitor");

    FrozenProfileLeaseState lease;
    lease.acquire(identity);
    require(lease.mark_native_arm_attempt(identity),
        "Stop-retirement cleanup lease was not marked attempted");
    AudioCleanupEvidence evidence;
    evidence.immutable_identity_matches = true;
    evidence.native_attempted = true;
    // Production withholds clear proof until the monitored retired handle disappears.
    evidence.native_clear_verified = false;
    evidence.route_state_unchanged = true;
    evidence.ambiguous_route_state = true;
    require(apply_audio_cleanup(lease, identity, evidence).status
                == AudioRouteCleanupStatus::Retained
            && lease.active(),
        "list return thawed while the custom decoded stream was still retiring");
}

void test_audio_natural_completion_retirement_policy()
{
    using namespace ff7r::piano::game;
    static_assert(!std::is_aggregate_v<AudioStopRetirementState>);
    AudioNaturalCompletionRetirementFacts valid;
    valid.route_custom_owned = true;
    valid.route_lease_valid = true;
    valid.route_controller_exact = true;
    valid.pre_stop_chain_read = true;
    valid.pre_stop_sound_null = true;
    valid.pre_stop_request_zero = true;
    valid.pre_stop_state_idle = true;
    valid.detached_present = true;
    valid.detached_restore_applied = true;
    valid.detached_route_predecessor = true;
    valid.detached_cleanup_generation_exact = true;
    valid.detached_sound_exact = true;
    valid.detached_request_available = true;
    valid.detached_request_exact = true;
    valid.detached_tokens_valid = true;
    valid.detached_owner_restored = true;
    valid.frozen_patch_valid = true;
    valid.frozen_route_predecessor = true;
    valid.frozen_lease_exact = true;
    valid.frozen_sound_exact = true;
    valid.frozen_owner_patch_exact = true;
    valid.journals_restored = true;
    valid.pause_resume_clear = true;
    valid.setup_clear = true;
    valid.registry_playback_exact = true;
    valid.registry_cleanup_clear_or_exact = true;
    valid.aggregate_snapshot_available = true;
    valid.canonical_proof_snapshot_available = true;
    valid.route_commit_exact = true;
    valid.detached_commit_exact = true;
    valid.frozen_commit_exact = true;
    valid.ownership_commit_exact = true;
    valid.registry_commit_exact = true;

    AudioRetirementCleanupFailureFacts cleanup_failure_facts;
    cleanup_failure_facts.origin_valid = true;
    cleanup_failure_facts.retirement_quiescent = true;
    cleanup_failure_facts.immutable_identity_matches = true;
    cleanup_failure_facts.backing_proven = true;
    cleanup_failure_facts.live_route_successor = true;
    cleanup_failure_facts.journals_restored = true;
    cleanup_failure_facts.frozen_fields_restored = true;
    cleanup_failure_facts.native_route_proven = true;
    cleanup_failure_facts.cleanup_policy_accepted = true;
    using CleanupFailureMember = bool AudioRetirementCleanupFailureFacts::*;
    const std::array<std::pair<CleanupFailureMember, AudioRetirementCleanupFailure>, 9>
        cleanup_failures{{
            {&AudioRetirementCleanupFailureFacts::origin_valid,
                AudioRetirementCleanupFailure::InvalidOrigin},
            {&AudioRetirementCleanupFailureFacts::retirement_quiescent,
                AudioRetirementCleanupFailure::RetirementNotQuiescent},
            {&AudioRetirementCleanupFailureFacts::live_route_successor,
                AudioRetirementCleanupFailure::LiveRouteNotSuccessor},
            {&AudioRetirementCleanupFailureFacts::immutable_identity_matches,
                AudioRetirementCleanupFailure::ImmutableIdentityMismatch},
            {&AudioRetirementCleanupFailureFacts::backing_proven,
                AudioRetirementCleanupFailure::BackingNotProven},
            {&AudioRetirementCleanupFailureFacts::journals_restored,
                AudioRetirementCleanupFailure::JournalsNotRestored},
            {&AudioRetirementCleanupFailureFacts::frozen_fields_restored,
                AudioRetirementCleanupFailure::FrozenFieldsNotRestored},
            {&AudioRetirementCleanupFailureFacts::native_route_proven,
                AudioRetirementCleanupFailure::NativeRouteNotProven},
            {&AudioRetirementCleanupFailureFacts::cleanup_policy_accepted,
                AudioRetirementCleanupFailure::CleanupPolicyRejected},
        }};
    for (size_t index = 0; index < cleanup_failures.size(); ++index) {
        auto facts = cleanup_failure_facts;
        for (size_t failure = index; failure < cleanup_failures.size(); ++failure) {
            facts.*(cleanup_failures[failure].first) = false;
        }
        require(first_audio_retirement_cleanup_failure(facts)
                == cleanup_failures[index].second,
            "retirement cleanup first-failure projection order drifted");
    }
    require(first_audio_retirement_cleanup_failure(cleanup_failure_facts)
            == AudioRetirementCleanupFailure::None,
        "complete retirement cleanup facts projected a failure");

    using Failure = AudioNaturalCompletionRetirementFailure;
    using Member = bool AudioNaturalCompletionRetirementFacts::*;
    const std::array<std::pair<Member, Failure>, 29> ordered_failures{{
        {&AudioNaturalCompletionRetirementFacts::route_custom_owned, Failure::RouteNotCustomOwned},
        {&AudioNaturalCompletionRetirementFacts::route_lease_valid, Failure::RouteLeaseInvalid},
        {&AudioNaturalCompletionRetirementFacts::route_controller_exact, Failure::RouteControllerMismatch},
        {&AudioNaturalCompletionRetirementFacts::pre_stop_chain_read, Failure::PreStopChainUnreadable},
        {&AudioNaturalCompletionRetirementFacts::pre_stop_sound_null, Failure::PreStopSoundNotNull},
        {&AudioNaturalCompletionRetirementFacts::pre_stop_request_zero, Failure::PreStopRequestNotZero},
        {&AudioNaturalCompletionRetirementFacts::pre_stop_state_idle, Failure::PreStopStateNotIdle},
        {&AudioNaturalCompletionRetirementFacts::detached_present, Failure::DetachedMissing},
        {&AudioNaturalCompletionRetirementFacts::detached_restore_applied, Failure::DetachedPhaseNotRestoreApplied},
        {&AudioNaturalCompletionRetirementFacts::detached_route_predecessor, Failure::DetachedRouteNotPredecessor},
        {&AudioNaturalCompletionRetirementFacts::detached_cleanup_generation_exact, Failure::DetachedCleanupGenerationMismatch},
        {&AudioNaturalCompletionRetirementFacts::detached_sound_exact, Failure::DetachedSoundMismatch},
        {&AudioNaturalCompletionRetirementFacts::detached_request_available, Failure::DetachedRequestUnavailable},
        {&AudioNaturalCompletionRetirementFacts::detached_request_exact, Failure::DetachedRequestMismatch},
        {&AudioNaturalCompletionRetirementFacts::detached_tokens_valid, Failure::DetachedTokensInvalid},
        {&AudioNaturalCompletionRetirementFacts::detached_owner_restored, Failure::DetachedOwnerNotRestored},
        {&AudioNaturalCompletionRetirementFacts::frozen_patch_valid, Failure::FrozenPatchInvalid},
        {&AudioNaturalCompletionRetirementFacts::frozen_route_predecessor, Failure::FrozenRouteNotPredecessor},
        {&AudioNaturalCompletionRetirementFacts::frozen_lease_exact, Failure::FrozenLeaseMismatch},
        {&AudioNaturalCompletionRetirementFacts::frozen_sound_exact, Failure::FrozenSoundMismatch},
        {&AudioNaturalCompletionRetirementFacts::frozen_owner_patch_exact, Failure::FrozenOwnerPatchMismatch},
        {&AudioNaturalCompletionRetirementFacts::journals_restored, Failure::JournalsNotRestored},
        {&AudioNaturalCompletionRetirementFacts::pause_resume_clear, Failure::PauseResumeConflict},
        {&AudioNaturalCompletionRetirementFacts::setup_clear, Failure::SetupConflict},
        {&AudioNaturalCompletionRetirementFacts::registry_playback_exact, Failure::RegistryPlaybackMismatch},
        {&AudioNaturalCompletionRetirementFacts::registry_cleanup_clear_or_exact, Failure::RegistryCleanupConflict},
        {&AudioNaturalCompletionRetirementFacts::aggregate_snapshot_available, Failure::AggregateSnapshotUnavailable},
        {&AudioNaturalCompletionRetirementFacts::canonical_proof_snapshot_available, Failure::CanonicalProofSnapshotUnavailable},
        {&AudioNaturalCompletionRetirementFacts::route_commit_exact, Failure::RouteCommitMismatch},
    }};
    for (const auto& [member, expected] : ordered_failures) {
        auto facts = valid;
        facts.*member = false;
        const auto actual = expected == Failure::RouteCommitMismatch
            ? classify_audio_natural_completion_retirement_commit(facts)
            : classify_audio_natural_completion_retirement(facts);
        require(actual == expected,
            "natural-completion ordered failure classifier drifted");
    }
    const std::array<std::pair<Member, Failure>, 4> commit_failures{{
        {&AudioNaturalCompletionRetirementFacts::detached_commit_exact,
            Failure::DetachedCommitMismatch},
        {&AudioNaturalCompletionRetirementFacts::frozen_commit_exact,
            Failure::FrozenCommitMismatch},
        {&AudioNaturalCompletionRetirementFacts::ownership_commit_exact,
            Failure::OwnershipCommitMismatch},
        {&AudioNaturalCompletionRetirementFacts::registry_commit_exact,
            Failure::RegistryCommitMismatch},
    }};
    for (const auto& [member, expected] : commit_failures) {
        auto facts = valid;
        facts.*member = false;
        require(classify_audio_natural_completion_retirement_commit(facts)
                == expected,
            "natural-completion commit failure order drifted");
    }
    auto conflict = valid;
    conflict.active_aggregate_borrower = true;
    require(classify_audio_natural_completion_retirement(conflict)
            == Failure::ActiveAggregateBorrower,
        "active aggregate borrower authorized natural completion");
    conflict = valid;
    conflict.canonical_proof_active = true;
    require(classify_audio_natural_completion_retirement(conflict)
            == Failure::CanonicalProofActive,
        "active canonical proof authorized natural completion");
    conflict = valid;
    conflict.existing_retirement_phase = AudioStopRetirementPhase::Waiting;
    require(classify_audio_natural_completion_retirement(conflict)
            == Failure::ExistingRetirement,
        "duplicate natural-completion monitor was admitted");

    const auto pointer = [](uintptr_t value) { return reinterpret_cast<void*>(value); };
    AudioStopRetirementObservation observation;
    observation.valid = true;
    // Frozen/detached generation N=70, Stop monitor generation N+1=71,
    // and the post-Set live route generation N+2=72.
    observation.route_generation = 71;
    observation.lease_identity = {101, 0xabc};
    observation.controller = pointer(0x1000);
    observation.slot = pointer(0x2000);
    observation.bgm = pointer(0x3000);
    observation.custom_sound = pointer(0x4000);
    observation.request_handle = 0x400010008ull;
    observation.request_handle_retired = false;

    OnMemoryBankDetachedRecord detached;
    detached.phase = OnMemoryBankLifecyclePhase::RestoreApplied;
    detached.sound = {observation.custom_sound, {40, 400}};
    detached.canonical = {1, 4, 40};
    detached.custom = {1, 5, 50};
    detached.ordinal = 60;
    detached.route_generation = observation.route_generation - 1;
    detached.cleanup_generation = observation.lease_identity.generation;
    detached.request_handle = observation.request_handle;
    detached.backing_identity = pointer(0x6000);
    detached.backing_observed = true;
    detached.owner_restore_verified = true;
    OnMemoryBankDetachedBackingSeedFacts seed_facts{
        80, detached.ordinal, observation.route_generation,
        detached.cleanup_generation, 90, detached.request_handle,
        detached.sound.live, detached.canonical, detached.custom,
        detached.backing_observed, detached.backing_identity};
    OnMemoryBankRetiredBackingEvidence detached_backing;
    require(seed_onmemory_bank_retired_backing_from_detached(
                detached_backing, detached, seed_facts)
            && detached_backing.provenance
                == OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached
            && onmemory_bank_exact_natural_detached_backing_current(
                detached_backing, seed_facts.lifecycle_epoch, detached),
        "exact detached backing seed was rejected");

    const auto seed_rejected = [&](OnMemoryBankDetachedRecord record,
                                   OnMemoryBankDetachedBackingSeedFacts expected) {
        OnMemoryBankRetiredBackingEvidence evidence;
        return !seed_onmemory_bank_retired_backing_from_detached(
            evidence, record, expected) && !evidence.observed && !evidence.failed;
    };
    auto bad_detached = detached;
    bad_detached.phase = OnMemoryBankLifecyclePhase::CanonicalQualified;
    require(seed_rejected(bad_detached, seed_facts), "non-RestoreApplied seed was accepted");
    auto bad_seed = seed_facts;
    ++bad_seed.ordinal;
    require(seed_rejected(detached, bad_seed), "detached ordinal drift was accepted");
    bad_seed = seed_facts; ++bad_seed.route_generation;
    require(seed_rejected(detached, bad_seed), "future monitor route was accepted");
    bad_seed = seed_facts; bad_seed.route_generation = detached.route_generation;
    require(seed_rejected(detached, bad_seed), "stale detached route was accepted as monitor route");
    bad_seed = seed_facts; ++bad_seed.cleanup_generation;
    require(seed_rejected(detached, bad_seed), "detached cleanup drift was accepted");
    bad_seed = seed_facts; ++bad_seed.request_handle;
    require(seed_rejected(detached, bad_seed), "detached request drift was accepted");
    bad_seed = seed_facts; ++bad_seed.sound_identity.serial_number;
    require(seed_rejected(detached, bad_seed), "detached sound drift was accepted");
    bad_seed = seed_facts; ++bad_seed.canonical.generation;
    require(seed_rejected(detached, bad_seed), "detached canonical token drift was accepted");
    bad_seed = seed_facts; ++bad_seed.custom.generation;
    require(seed_rejected(detached, bad_seed), "detached custom token drift was accepted");
    bad_seed = seed_facts; bad_seed.lifecycle_epoch = 0;
    require(seed_rejected(detached, bad_seed), "zero lifecycle epoch was accepted");
    bad_seed = seed_facts; bad_seed.monitor_epoch = 0;
    require(seed_rejected(detached, bad_seed), "zero monitor epoch was accepted");
    bad_seed = seed_facts; bad_seed.backing = pointer(0x6001);
    require(seed_rejected(detached, bad_seed), "detached backing drift was accepted");
    bad_seed = seed_facts; bad_seed.backing_observed = false;
    require(seed_rejected(detached, bad_seed), "unobserved backing seed was accepted");
    bad_detached = detached; bad_detached.backing_observed = false;
    require(seed_rejected(bad_detached, seed_facts),
        "detached record without observed backing was accepted");
    bad_detached = detached; bad_detached.canonical.type = 2;
    require(seed_rejected(bad_detached, seed_facts),
        "detached record with invalid token lineage was accepted");
    bad_detached = detached; bad_detached.owner_restore_verified = false;
    require(seed_rejected(bad_detached, seed_facts), "unrestored owner seed was accepted");
    bad_detached = detached; bad_detached.release_attempted = true;
    require(seed_rejected(bad_detached, seed_facts), "release-attempted seed was accepted");
    auto nonempty_seed = detached_backing;
    require(!seed_onmemory_bank_retired_backing_from_detached(
                nonempty_seed, detached, seed_facts),
        "nonempty backing evidence was reseeded");

    const OnMemoryBankRetiredBackingObservation vector_observation{
        observation.route_generation, detached.cleanup_generation,
        seed_facts.monitor_epoch, detached.request_handle, detached.sound.live,
        true, true, detached.backing_identity};
    auto upgraded_backing = detached_backing;
    require(record_onmemory_bank_retired_backing(
                upgraded_backing, vector_observation)
            && upgraded_backing.provenance
                == OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector
            && onmemory_bank_retired_backing_satisfies(
                upgraded_backing,
                OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion)
            && onmemory_bank_retired_backing_satisfies(
                upgraded_backing,
                OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence),
        "matching retired-vector presence did not upgrade detached provenance");
    auto mismatched_backing = detached_backing;
    auto mismatched_observation = vector_observation;
    ++mismatched_observation.request_handle;
    require(!record_onmemory_bank_retired_backing(
                mismatched_backing, mismatched_observation)
            && mismatched_backing.failed,
        "mismatched retired-vector presence did not poison detached provenance");
    auto null_detached = detached;
    null_detached.backing_identity = nullptr;
    auto null_seed_facts = seed_facts;
    null_seed_facts.backing = nullptr;
    OnMemoryBankRetiredBackingEvidence refined_backing;
    require(seed_onmemory_bank_retired_backing_from_detached(
                refined_backing, null_detached, null_seed_facts),
        "readable null detached backing seed was rejected");
    void* proven_null_backing = pointer(0x1);
    require(onmemory_bank_exact_request_retired(
                refined_backing, observation.route_generation,
                observation.lease_identity.generation,
                null_seed_facts.monitor_epoch, observation.request_handle,
                detached.sound.live, true, proven_null_backing,
                OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion)
            && proven_null_backing == nullptr,
        "readable null detached backing did not prove exact natural retirement");
    auto refinement_observation = vector_observation;
    require(record_onmemory_bank_retired_backing(
                refined_backing, refinement_observation)
            && refined_backing.value == detached.backing_identity
            && refined_backing.provenance
                == OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector,
        "retired-vector presence did not refine null detached backing");
    OnMemoryBankRetiredBackingEvidence vector_backing;
    require(record_onmemory_bank_retired_backing(vector_backing, vector_observation)
            && vector_backing.provenance
                == OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence
            && !onmemory_bank_retired_backing_satisfies(
                vector_backing,
                OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion),
        "ordinary retired-vector evidence acquired natural provenance");
    require(!onmemory_bank_retirement_presence_pending(
                detached_backing, true, false,
                OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion)
            && onmemory_bank_retirement_presence_pending(
                detached_backing, true, false,
                OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence)
            && !onmemory_bank_retirement_presence_pending(
                vector_backing, true, false,
                OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence),
        "retirement presence pending ignored backing provenance");
    auto lifecycle_drift = detached;
    ++lifecycle_drift.ordinal;
    require(!onmemory_bank_exact_natural_detached_backing_current(
                detached_backing, seed_facts.lifecycle_epoch + 1, detached)
            && !onmemory_bank_exact_natural_detached_backing_current(
                detached_backing, seed_facts.lifecycle_epoch, lifecycle_drift),
        "lifecycle epoch or detached-record drift retained natural authority");
    auto stale_monitor_backing = detached_backing;
    stale_monitor_backing.route_generation = detached.route_generation;
    require(!onmemory_bank_exact_natural_detached_backing_current(
                stale_monitor_backing, seed_facts.lifecycle_epoch, detached),
        "stale monitor generation retained detached natural authority");

    const auto pending = start_audio_natural_completion_retirement(
        valid, observation, detached_backing);
    require(pending.admitted && !pending.presence_pending
            && pending.monitor.phase == AudioStopRetirementPhase::Waiting
            && !pending.monitor_observation.request_handle_retired
            && pending.monitor.origin()
                == AudioStopRetirementOrigin::ExactNaturalCompletion,
        "exact detached backing did not start natural Waiting monitor");
    auto invalid_observation = observation;
    invalid_observation.valid = false;
    const auto invalid = start_audio_natural_completion_retirement(
        valid, invalid_observation, detached_backing);
    require(!invalid.admitted && invalid.failure == Failure::InvalidObservation
            && invalid.monitor.phase == AudioStopRetirementPhase::None
            && invalid.monitor.origin() == AudioStopRetirementOrigin::None,
        "invalid observation mutated natural-completion monitor state");
    const auto absent = start_audio_natural_completion_retirement(
        valid, observation, {});
    require(!absent.admitted && absent.failure == Failure::InvalidObservation,
        "missing exact detached backing started a monitor");
    auto unauthorized_facts = valid;
    unauthorized_facts.detached_owner_restored = false;
    const auto unauthorized = start_audio_natural_completion_retirement(
        unauthorized_facts, pending.monitor_observation, detached_backing);
    require(!unauthorized.admitted
            && unauthorized.failure == Failure::DetachedOwnerNotRestored
            && unauthorized.monitor.origin() == AudioStopRetirementOrigin::None,
        "unqualified caller selected natural-completion retirement provenance");

    bool native_called_returned = false;
    const auto before_native = classify_audio_natural_completion_retirement(valid);
    native_called_returned = true;
    require(native_called_returned
            && classify_audio_natural_completion_retirement(valid) == before_native,
        "native Stop return became natural-completion authority");

    AudioStopRetirementState monitor = pending.monitor;
    auto quiescent = pending.monitor_observation;
    quiescent.request_handle_retired = false;
    quiescent.current_sound = pointer(0x5000);
    quiescent.current_request_handle = 0x500000008ull;
    quiescent.current_state = 4;
    require(advance_audio_stop_retirement_monitor(monitor, quiescent, 4)
                == AudioStopRetirementPhase::Quiescent,
        "N+1 retirement did not advance to Quiescent after N+2 canonical Set/Play");
    AudioStopRetirementState ordinary_absent;
    require(!begin_audio_stop_retirement_monitor(ordinary_absent, observation)
            && ordinary_absent.phase == AudioStopRetirementPhase::None,
        "ordinary/pause retirement accepted first-absent request without vector presence");
    void* proven_natural_backing = nullptr;
    require(onmemory_bank_exact_request_retired(
                detached_backing, observation.route_generation,
                observation.lease_identity.generation,
                seed_facts.monitor_epoch, observation.request_handle,
                detached.sound.live, true, proven_natural_backing,
                OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion)
            && proven_natural_backing == detached.backing_identity
            && !onmemory_bank_exact_request_retired(
                detached_backing, observation.route_generation,
                observation.lease_identity.generation,
                seed_facts.monitor_epoch, observation.request_handle,
                detached.sound.live, true, proven_natural_backing,
                OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence),
        "exact detached backing did not prove natural-only request retirement");
    require(monitor.origin()
                == AudioStopRetirementOrigin::ExactNaturalCompletion
            && audio_retirement_frozen_generation_proven(70, monitor, 72),
        "natural frozen N was not accepted for monitor N+1 and live N+2");
    require(!audio_retirement_frozen_generation_proven(69, monitor, 72)
            && !audio_retirement_frozen_generation_proven(71, monitor, 72),
        "stale or same-generation frozen relation was accepted for natural retirement");

    AudioStopRetirementState ordinary;
    auto ordinary_observation = pending.monitor_observation;
    ordinary_observation.request_handle_retired = true;
    require(begin_audio_stop_retirement_monitor(
                ordinary, ordinary_observation)
            && ordinary.origin() == AudioStopRetirementOrigin::OrdinaryStop
            && advance_audio_stop_retirement_monitor(ordinary, quiescent, 4)
                == AudioStopRetirementPhase::Quiescent,
        "ordinary retirement provenance was not preserved through monitor advance");
    require(audio_retirement_frozen_generation_proven(70, ordinary, 72),
        "ordinary frozen predecessor was not accepted for successor monitor/live route");
    require(!audio_retirement_frozen_generation_proven(69, ordinary, 72)
            && !audio_retirement_frozen_generation_proven(71, ordinary, 72),
        "ordinary retirement accepted a non-predecessor frozen generation");

    AudioStopRetirementState invalid_provenance;
    invalid_provenance.phase = AudioStopRetirementPhase::Quiescent;
    invalid_provenance.route_generation = 71;
    require(invalid_provenance.origin() == AudioStopRetirementOrigin::None
            && !audio_retirement_frozen_generation_proven(
                70, invalid_provenance, 72)
            && !audio_retirement_frozen_generation_proven(
                71, invalid_provenance, 72),
        "invalid retirement provenance authorized a frozen generation relation");
    require(!audio_retirement_frozen_generation_proven(70, ordinary, 71)
            && !audio_retirement_frozen_generation_proven(70, monitor, 73),
        "non-successor live route authorized frozen retirement evidence");
    AudioNativeRouteObservation current;
    current.valid = true;
    current.route_generation = 72;
    current.lease_identity = observation.lease_identity;
    current.slot = observation.slot;
    current.bgm = observation.bgm;
    current.sound = quiescent.current_sound;
    current.request_handle = quiescent.current_request_handle;
    current.state = quiescent.current_state;
    const AudioRetirementCleanupEvidence cleanup{
        monitor, quiescent, current, true, true, true, true};
    require(classify_audio_natural_completion_retirement_commit(valid)
                == Failure::None
            && audio_retirement_cleanup_proven(cleanup),
        "deterministic frozen N to monitor N+1 to live N+2 cleanup proof failed");
    const OnMemoryBankReleaseAction release_action{
        detached.ordinal, detached.route_generation, detached.cleanup_generation,
        detached.canonical, detached.custom};
    uint64_t released_token = 0;
    int lookup_count = 0;
    const auto release_execution = execute_onmemory_bank_release(
        true, true, false, release_action,
        [&](uint64_t token) {
            ++lookup_count;
            return token == detached.canonical.encode()
                    || token == detached.custom.encode()
                ? 2u : 0u;
        },
        [&](const uint64_t* token, uint8_t mode) {
            released_token = mode == 1 ? *token : 0;
            return OnMemoryBankNativeReleaseResult{true, 0};
        });
    require(release_execution.outcome
                == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
            && lookup_count == 2
            && released_token == detached.custom.encode()
            && released_token != detached.canonical.encode(),
        "exact natural completion did not preserve canonical token during sole custom release");
    bool rejected_release_called = false;
    const auto rejected_release = execute_onmemory_bank_release(
        true, true, false, release_action,
        [&](uint64_t token) {
            return token == detached.canonical.encode() ? 2u : 1u;
        },
        [&](const uint64_t*, uint8_t) {
            rejected_release_called = true;
            return OnMemoryBankNativeReleaseResult{true, 0};
        });
    require(rejected_release.outcome == OnMemoryBankReleaseOutcome::Failed
            && !rejected_release_called,
        "invalid custom-token kind reached exact-natural release");
    AudioStopRetirementState no_origin;
    no_origin.phase = monitor.phase;
    no_origin.route_generation = monitor.route_generation;
    no_origin.lease_identity = monitor.lease_identity;
    no_origin.controller = monitor.controller;
    no_origin.slot = monitor.slot;
    no_origin.bgm = monitor.bgm;
    no_origin.custom_sound = monitor.custom_sound;
    no_origin.request_handle = monitor.request_handle;
    no_origin.poll_count = monitor.poll_count;
    AudioRetirementCleanupEvidence no_origin_cleanup = cleanup;
    no_origin_cleanup.retirement = no_origin;
    require(no_origin.origin() == AudioStopRetirementOrigin::None
            && !audio_retirement_cleanup_proven(no_origin_cleanup),
        "cleanup accepted retirement evidence without production provenance");
}

} // namespace ff7r::piano::tests::runtime_lifecycle
