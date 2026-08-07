#include "game/audio_native_call_policy.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "audio_native_call_policy_selftest: " << message << '\n';
        std::exit(1);
    }
}

void test_bgm_playback_native_set_call_policy()
{
    using namespace ff7r::piano::game;
    using Target = BgmPlaybackNativeSetTarget;
    using Result = BgmPlaybackNativeSetCallResult;

    void* const controller = reinterpret_cast<void*>(uintptr_t{0x1111});
    void* const sound = reinterpret_cast<void*>(uintptr_t{0x2222});
    uint32_t trampoline_calls = 0;
    uint32_t direct_calls = 0;
    const auto invoke = [&](Target target, void* actual_controller,
                            void* actual_sound) {
        require(actual_controller == controller && actual_sound == sound,
            "native Set policy changed forwarded arguments");
        if (target == Target::Trampoline) ++trampoline_calls;
        if (target == Target::DirectRva) ++direct_calls;
        return true;
    };

    const auto no_target = select_bgm_playback_native_set_target(false, false);
    auto outcome = invoke_bgm_playback_native_set_exactly_once(
        no_target, invoke, controller, sound);
    auto compatibility = bgm_playback_native_set_compatibility_result(outcome);
    require(no_target == Target::None && outcome.target == Target::None
            && outcome.result == Result::NotCalled && outcome.call_count == 0
            && trampoline_calls == 0 && direct_calls == 0
            && compatibility.succeeded
            && compatibility.fault == BgmPlaybackProtectedNativeFault::None,
        "missing native Set target changed compatibility behavior");

    const auto preferred = select_bgm_playback_native_set_target(true, true);
    outcome = invoke_bgm_playback_native_set_exactly_once(
        preferred, invoke, controller, sound);
    require(preferred == Target::Trampoline
            && outcome.result == Result::CalledSucceeded
            && outcome.call_count == 1 && trampoline_calls == 1
            && direct_calls == 0
            && bgm_playback_native_set_call_outcome_valid(outcome),
        "native Set trampoline precedence or exact-once call changed");

    const auto fallback = select_bgm_playback_native_set_target(false, true);
    outcome = invoke_bgm_playback_native_set_exactly_once(
        fallback, invoke, controller, sound);
    require(fallback == Target::DirectRva
            && outcome.result == Result::CalledSucceeded
            && outcome.call_count == 1 && trampoline_calls == 1
            && direct_calls == 1,
        "native Set direct-RVA fallback was not selected exactly once");

    trampoline_calls = direct_calls = 0;
    outcome = invoke_bgm_playback_native_set_exactly_once(preferred,
        [&](Target target, void* actual_controller, void* actual_sound) {
            require(target == Target::Trampoline
                    && actual_controller == controller && actual_sound == sound,
                "failed native Set changed target or arguments");
            ++trampoline_calls;
            return false;
        }, controller, sound);
    require(outcome.result == Result::CalledFailed && outcome.call_count == 1
            && trampoline_calls == 1 && direct_calls == 0,
        "failed trampoline retried the direct-RVA target");

    trampoline_calls = direct_calls = 0;
    outcome = invoke_bgm_playback_native_set_exactly_once(preferred,
        [&](Target target, void*, void*) -> bool {
            if (target == Target::Trampoline) ++trampoline_calls;
            if (target == Target::DirectRva) ++direct_calls;
            throw std::runtime_error("injected native Set C++ exception");
        }, controller, sound);
    require(outcome.result == Result::CppException && outcome.call_count == 1
            && trampoline_calls == 1 && direct_calls == 0,
        "C++-faulted trampoline retried another native Set target");

    const auto structured_called =
        bgm_playback_native_set_structured_exception_outcome(preferred, true);
    const auto structured_not_called =
        bgm_playback_native_set_structured_exception_outcome(preferred, false);
    require(structured_called.result == Result::StructuredException
            && structured_called.call_count == 1
            && structured_not_called.result == Result::StructuredException
            && structured_not_called.call_count == 0
            && bgm_playback_native_set_call_outcome_valid(structured_called)
            && bgm_playback_native_set_call_outcome_valid(structured_not_called)
            && !bgm_playback_native_set_call_outcome_valid(
                {preferred, Result::CalledSucceeded, 2}),
        "structured native Set classification violated call-count bounds");

    const struct CompatibilityCase {
        Result result;
        bool succeeded;
        BgmPlaybackProtectedNativeFault fault;
    } cases[] = {
        {Result::NotCalled, true, BgmPlaybackProtectedNativeFault::None},
        {Result::CalledSucceeded, true, BgmPlaybackProtectedNativeFault::None},
        {Result::CalledFailed, false,
            BgmPlaybackProtectedNativeFault::NativeFailure},
        {Result::CppException, false,
            BgmPlaybackProtectedNativeFault::CppException},
        {Result::StructuredException, false,
            BgmPlaybackProtectedNativeFault::StructuredException},
    };
    for (const auto& test : cases) {
        const auto mapped = bgm_playback_native_set_compatibility_result(
            {test.result == Result::NotCalled ? Target::None : preferred,
                test.result,
                static_cast<uint8_t>(test.result == Result::NotCalled ? 0 : 1)});
        require(mapped.succeeded == test.succeeded && mapped.fault == test.fault,
            "native Set compatibility mapping changed");
    }

    std::string order;
    BgmPlaybackNativeSetCallOutcome ordered_outcome;
    const auto protected_result = bgm_playback_protected_native_boundary(
        [&] {
            ordered_outcome = invoke_bgm_playback_native_set_exactly_once(
                preferred, [&](Target, void*, void*) -> bool {
                    order += "call;";
                    throw std::runtime_error("ordered native Set exception");
                }, controller, sound);
            return bgm_playback_native_set_compatibility_result(ordered_outcome);
        }, [&]() noexcept { order += "cleanup;"; });
    require(order == "call;cleanup;" && !protected_result.succeeded
            && protected_result.fault
                == BgmPlaybackProtectedNativeFault::CppException
            && ordered_outcome.result == Result::CppException
            && ordered_outcome.call_count == 1,
        "native Set policy changed call/cleanup ordering");
}
void test_bgm_playback_native_play_call_policy()
{
    using namespace ff7r::piano::game;
    using Target = BgmPlaybackNativePlayTarget;
    using Result = BgmPlaybackNativePlayCallResult;

    void* const controller = reinterpret_cast<void*>(uintptr_t{0x3333});
    uint32_t calls = 0;
    const auto no_target = select_bgm_playback_native_play_target(false);
    auto outcome = invoke_bgm_playback_native_play_exactly_once(no_target,
        [&](Target, void*) { ++calls; return true; }, controller);
    auto compatibility = bgm_playback_native_play_compatibility_result(outcome);
    require(no_target == Target::None && outcome.target == Target::None
            && outcome.result == Result::NotCalled && outcome.call_count == 0
            && calls == 0 && compatibility.succeeded
            && compatibility.fault == BgmPlaybackProtectedNativeFault::None,
        "missing native Play trampoline changed compatibility behavior");

    const auto trampoline = select_bgm_playback_native_play_target(true);
    outcome = invoke_bgm_playback_native_play_exactly_once(trampoline,
        [&](Target target, void* actual_controller) {
            require(target == Target::Trampoline
                    && actual_controller == controller,
                "native Play policy changed target or arguments");
            ++calls;
            return true;
        }, controller);
    require(outcome.result == Result::CalledSucceeded
            && outcome.call_count == 1 && calls == 1
            && bgm_playback_native_play_call_outcome_valid(outcome),
        "native Play trampoline was not called exactly once");

    calls = 0;
    outcome = invoke_bgm_playback_native_play_exactly_once(trampoline,
        [&](Target target, void* actual_controller) {
            require(target == Target::Trampoline
                    && actual_controller == controller,
                "failed native Play changed target or arguments");
            ++calls;
            return false;
        }, controller);
    require(outcome.result == Result::CalledFailed && outcome.call_count == 1
            && calls == 1
            && !bgm_playback_native_play_compatibility_result(outcome).succeeded,
        "failed native Play retried or changed compatibility");

    calls = 0;
    outcome = invoke_bgm_playback_native_play_exactly_once(trampoline,
        [&](Target, void*) -> bool {
            ++calls;
            throw std::runtime_error("injected native Play C++ exception");
        }, controller);
    require(outcome.result == Result::CppException && outcome.call_count == 1
            && calls == 1
            && bgm_playback_native_play_compatibility_result(outcome).fault
                == BgmPlaybackProtectedNativeFault::CppException,
        "C++-faulted native Play retried or changed fault mapping");

    const auto structured_pre_entry =
        bgm_playback_native_play_structured_exception_outcome(
            trampoline, false);
    const auto structured_entered =
        bgm_playback_native_play_structured_exception_outcome(
            trampoline, true);
    require(structured_pre_entry.call_count == 0
            && structured_entered.call_count == 1
            && bgm_playback_native_play_call_outcome_valid(structured_pre_entry)
            && bgm_playback_native_play_call_outcome_valid(structured_entered),
        "structured native Play entry classification changed");

    const BgmPlaybackNativePlayCallOutcome valid_outcomes[]{
        {Target::None, Result::NotCalled, 0},
        {Target::Trampoline, Result::CalledSucceeded, 1},
        {Target::Trampoline, Result::CalledFailed, 1},
        {Target::Trampoline, Result::CppException, 1},
        {Target::Trampoline, Result::StructuredException, 0},
        {Target::Trampoline, Result::StructuredException, 1},
    };
    for (const auto& valid : valid_outcomes) {
        require(bgm_playback_native_play_call_outcome_valid(valid),
            "valid native Play target/result/call-count tuple rejected");
    }
    const BgmPlaybackNativePlayCallOutcome invalid_outcomes[]{
        {Target::Trampoline, Result::NotCalled, 0},
        {Target::None, Result::CalledSucceeded, 1},
        {Target::Trampoline, Result::CalledFailed, 0},
        {Target::Trampoline, Result::CppException, 0},
        {Target::Trampoline, Result::StructuredException, 2},
    };
    for (const auto& invalid : invalid_outcomes) {
        require(!bgm_playback_native_play_call_outcome_valid(invalid),
            "invalid native Play target/result/call-count tuple accepted");
    }

    const struct CompatibilityCase {
        Result result;
        bool succeeded;
        BgmPlaybackProtectedNativeFault fault;
    } cases[]{
        {Result::NotCalled, true, BgmPlaybackProtectedNativeFault::None},
        {Result::CalledSucceeded, true, BgmPlaybackProtectedNativeFault::None},
        {Result::CalledFailed, false,
            BgmPlaybackProtectedNativeFault::NativeFailure},
        {Result::CppException, false,
            BgmPlaybackProtectedNativeFault::CppException},
        {Result::StructuredException, false,
            BgmPlaybackProtectedNativeFault::StructuredException},
    };
    for (const auto& test : cases) {
        const auto mapped = bgm_playback_native_play_compatibility_result(
            {test.result == Result::NotCalled ? Target::None : trampoline,
                test.result,
                static_cast<uint8_t>(test.result == Result::NotCalled ? 0 : 1)});
        require(mapped.succeeded == test.succeeded && mapped.fault == test.fault,
            "native Play compatibility mapping changed");
    }

    std::string order;
    bool latch_discharge = false;
    auto run_production_orchestration = [&](const Target target,
        const BgmPlaybackNativePlayOrchestrationFacts& facts,
        auto&& invoke, const bool state4_observed,
        const bool retained_publication) {
        order.clear();
        latch_discharge = false;
        return run_bgm_playback_native_play_orchestration(target, facts,
            [&](const Target selected) {
                order += "invoke;";
                return std::forward<decltype(invoke)>(invoke)(selected);
            },
            [&] {
                order += "capture;";
                return BgmPlaybackNativePlayCaptureResult{state4_observed};
            },
            [&] { order += "cleanup;"; },
            [&](const BgmPlaybackProtectedNativeResult&,
                const BgmPlaybackNativePlayCaptureResult&) {
                order += "observe;";
            },
            [&](const BgmPlaybackNativePlayCaptureResult&) {
                order += "retain;";
                return retained_publication;
            },
            [&](const bool discharge) {
                order += "latch;";
                latch_discharge = discharge;
            });
    };

    calls = 0;
    auto orchestrated = run_production_orchestration(no_target,
        BgmPlaybackNativePlayOrchestrationFacts{},
        [&](const Target selected) {
            return invoke_bgm_playback_native_play_exactly_once(selected,
                [&](Target, void*) { ++calls; return true; }, controller);
        }, false, false);
    require(orchestrated.native.result == Result::NotCalled
            && orchestrated.native.call_count == 0 && calls == 0
            && orchestrated.compatibility.succeeded
            && order == "invoke;capture;cleanup;latch;"
            && !orchestrated.observation_performed
            && !orchestrated.retention_attempted && !latch_discharge,
        "production Play orchestration changed missing-target compatibility");

    const BgmPlaybackNativePlayOrchestrationFacts active_facts{
        true, true, true, true, false, true};
    calls = 0;
    orchestrated = run_production_orchestration(trampoline, active_facts,
        [&](const Target selected) {
            return invoke_bgm_playback_native_play_exactly_once(selected,
                [&](Target actual_target, void* actual_controller) {
                    require(actual_target == Target::Trampoline
                            && actual_controller == controller,
                        "production Play orchestration changed native args");
                    ++calls;
                    return true;
                }, controller);
        }, true, true);
    require(orchestrated.native.result == Result::CalledSucceeded
            && orchestrated.native.call_count == 1 && calls == 1
            && orchestrated.compatibility.succeeded
            && order == "invoke;capture;cleanup;observe;retain;latch;"
            && orchestrated.capture_completed
            && orchestrated.cleanup_completed
            && orchestrated.observation_performed
            && orchestrated.retention_attempted
            && orchestrated.retained_publication
            && orchestrated.publication_durable && latch_discharge,
        "production Play orchestration changed success ordering or ownership");

    calls = 0;
    orchestrated = run_production_orchestration(trampoline, active_facts,
        [&](const Target selected) {
            return invoke_bgm_playback_native_play_exactly_once(selected,
                [&](Target, void*) { ++calls; return false; }, controller);
        }, false, false);
    require(orchestrated.native.result == Result::CalledFailed
            && orchestrated.native.call_count == 1 && calls == 1
            && !orchestrated.compatibility.succeeded
            && order == "invoke;capture;cleanup;observe;retain;latch;"
            && orchestrated.unknown_publication_latched
            && !orchestrated.publication_durable && !latch_discharge,
        "production Play orchestration retried or lost failed ownership");
    const auto failed_orchestration = orchestrated;

    calls = 0;
    orchestrated = run_production_orchestration(trampoline, active_facts,
        [&](const Target selected) {
            return invoke_bgm_playback_native_play_exactly_once(selected,
                [&](Target, void*) -> bool {
                    ++calls;
                    throw std::runtime_error(
                        "production Play orchestration C++ exception");
                }, controller);
        }, false, false);
    require(orchestrated.native.result == Result::CppException
            && orchestrated.native.call_count == 1 && calls == 1
            && orchestrated.compatibility.fault
                == BgmPlaybackProtectedNativeFault::CppException
            && order == "invoke;capture;cleanup;observe;retain;latch;",
        "production Play orchestration retried a C++-faulted call");

    const auto structured_facts = BgmPlaybackNativePlayOrchestrationFacts{
        false, false, false, true, true, false};
    orchestrated = run_production_orchestration(trampoline, structured_facts,
        [&](Target) { return structured_pre_entry; }, false, false);
    require(orchestrated.native.result == Result::StructuredException
            && orchestrated.native.call_count == 0
            && orchestrated.compatibility.fault
                == BgmPlaybackProtectedNativeFault::StructuredException
            && order == "invoke;capture;cleanup;observe;latch;",
        "production Play orchestration lost pre-entry structured fault count");
    orchestrated = run_production_orchestration(trampoline, structured_facts,
        [&](Target) { return structured_entered; }, false, false);
    require(orchestrated.native.result == Result::StructuredException
            && orchestrated.native.call_count == 1
            && order == "invoke;capture;cleanup;observe;latch;",
        "production Play orchestration lost entered structured fault count");

    const BgmPlaybackPendingPlayTransactionFacts unresolved_fault{
        true, true, true, failed_orchestration.native.call_count,
        false, false, false,
        failed_orchestration.unknown_publication_latched, false};
    require(bgm_playback_pending_play_ownership_durable(unresolved_fault)
            && !bgm_playback_pending_play_latch_discharge_allowed(
                unresolved_fault)
            && bgm_playback_pending_play_release_blocked(unresolved_fault)
            && !failed_orchestration.latch_discharge_requested,
        "faulted native Play no longer retains unresolved publication ownership");

}

void test_bgm_playback_native_stop_call_policy()
{
    using namespace ff7r::piano::game;
    using Target = BgmPlaybackNativeStopTarget;
    using Result = BgmPlaybackNativeStopCallResult;

    const BgmPlaybackNativeStopCallOutcome legal_outcomes[]{
        {Target::None, Result::NotCalled, 0},
        {Target::Trampoline, Result::CalledReturned, 1},
    };
    for (const auto& outcome : legal_outcomes) {
        require(bgm_playback_native_stop_call_outcome_valid(outcome),
            "valid native Stop target/result/call-count tuple rejected");
    }
    const BgmPlaybackNativeStopCallOutcome illegal_outcomes[]{
        {Target::Trampoline, Result::NotCalled, 0},
        {Target::None, Result::CalledReturned, 1},
        {Target::None, Result::NotCalled, 1},
        {Target::Trampoline, Result::CalledReturned, 0},
        {Target::Trampoline, Result::CalledReturned, 2},
    };
    for (const auto& outcome : illegal_outcomes) {
        require(!bgm_playback_native_stop_call_outcome_valid(outcome),
            "invalid native Stop target/result/call-count tuple accepted");
    }

    void* const controller = reinterpret_cast<void*>(uintptr_t{0x4545});
    uint32_t native_calls = 0;
    uint32_t post_calls = 0;
    std::string order;
    const auto no_target = select_bgm_playback_native_stop_target(false);
    auto observed = run_bgm_playback_native_stop_post_observation(no_target,
        [&](Target, void*) { ++native_calls; },
        [&](const BgmPlaybackNativeStopCallOutcome& outcome) {
            ++post_calls;
            order += "post;";
            return outcome.result == Result::NotCalled;
        }, controller);
    require(observed.native.target == Target::None
            && observed.native.result == Result::NotCalled
            && observed.native.call_count == 0 && native_calls == 0
            && post_calls == 1 && order == "post;"
            && observed.post_observation_ran
            && observed.post_observation_succeeded
            && observed.original_forwarded,
        "missing native Stop target skipped admitted post-observation");

    native_calls = post_calls = 0;
    order.clear();
    const auto trampoline = select_bgm_playback_native_stop_target(true);
    observed = run_bgm_playback_native_stop_post_observation(trampoline,
        [&](Target target, void* actual_controller) {
            require(target == Target::Trampoline
                    && actual_controller == controller,
                "native Stop orchestration changed forwarded arguments");
            ++native_calls;
            order += "native;";
        },
        [&](const BgmPlaybackNativeStopCallOutcome& outcome) {
            ++post_calls;
            order += "post;";
            return outcome.call_count == 1;
        }, controller);
    require(observed.native.result == Result::CalledReturned
            && observed.native.call_count == 1 && native_calls == 1
            && post_calls == 1 && order == "native;post;"
            && observed.post_observation_ran
            && observed.post_observation_succeeded
            && observed.original_forwarded,
        "native Stop was retried or post-observed before wrapper return");

    native_calls = post_calls = 0;
    observed = run_bgm_playback_native_stop_post_observation(trampoline,
        [&](Target, void*) { ++native_calls; },
        [&](const BgmPlaybackNativeStopCallOutcome&) {
            ++post_calls;
            return false;
        }, controller);
    require(native_calls == 1 && post_calls == 1
            && observed.native.call_count == 1
            && observed.post_observation_ran
            && !observed.post_observation_succeeded
            && observed.original_forwarded,
        "false native Stop post-observation changed forwarding compatibility");

    native_calls = post_calls = 0;
    bool propagated = false;
    std::atomic_bool inject_stop_exception{true};
    try {
        (void)run_bgm_playback_native_stop_post_observation(trampoline,
            [&](Target, void*) {
                ++native_calls;
                if (inject_stop_exception.load(std::memory_order_relaxed)) {
                    throw std::runtime_error("injected native Stop exception");
                }
            },
            [&](const BgmPlaybackNativeStopCallOutcome&) {
                ++post_calls;
                return true;
            }, controller);
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    require(propagated && native_calls == 1 && post_calls == 0,
        "native Stop exception was caught, retried, or post-observed");

    require(bgm_playback_native_stop_original_forwarded(legal_outcomes[0])
            && bgm_playback_native_stop_original_forwarded(legal_outcomes[1])
            && !bgm_playback_native_stop_original_forwarded(
                illegal_outcomes[0]),
        "native Stop original_forwarded compatibility marker changed");
}
void test_bgm_playback_native_play_setup_call_policy()
{
    using namespace ff7r::piano::game;
    using Target = BgmPlaybackNativePlaySetupTarget;
    using Result = BgmPlaybackNativePlaySetupCallResult;

    const BgmPlaybackNativePlaySetupCallOutcome legal_outcomes[]{
        {Target::None, Result::NotCalled, 0},
        {Target::Trampoline, Result::CalledReturned, 1},
    };
    for (const auto& outcome : legal_outcomes) {
        require(bgm_playback_native_play_setup_call_outcome_valid(outcome),
            "valid native PlaySetup target/result/call-count tuple rejected");
    }
    const BgmPlaybackNativePlaySetupCallOutcome illegal_outcomes[]{
        {Target::Trampoline, Result::NotCalled, 0},
        {Target::None, Result::CalledReturned, 1},
        {Target::None, Result::NotCalled, 1},
        {Target::Trampoline, Result::CalledReturned, 0},
        {Target::Trampoline, Result::CalledReturned, 2},
    };
    for (const auto& outcome : illegal_outcomes) {
        require(!bgm_playback_native_play_setup_call_outcome_valid(outcome),
            "invalid native PlaySetup target/result/call-count tuple accepted");
    }

    void* const sound = reinterpret_cast<void*>(uintptr_t{0x5151});
    void* const arg6 = reinterpret_cast<void*>(uintptr_t{0x6161});
    uint32_t native_calls = 0;
    uint32_t observation_calls = 0;
    uint32_t attempt_publications = 0;
    BgmPlaybackNativePlaySetupAttemptState published_attempt{};
    std::string order;
    uint32_t tls_depth = 0;
    uint64_t tls_generation = 0;
    bool tls_claimed = false;
    const auto observe_tls = [&](const BgmPlaybackNativePlaySetupCallOutcome&) {
        ++observation_calls;
        order += "observe;";
        return BgmPlaybackNativePlaySetupTlsSnapshot{
            tls_depth, tls_generation, tls_claimed};
    };
    const auto publish_attempt =
        [&](const BgmPlaybackNativePlaySetupAttemptState& attempt) {
            ++attempt_publications;
            published_attempt = attempt;
        };

    const auto no_target = select_bgm_playback_native_play_setup_target(false);
    auto observed = run_bgm_playback_native_play_setup_return_observation(
        no_target,
        [&](Target, void*, float, float, uint64_t, uint64_t, uint8_t, void*) {
            ++native_calls;
        }, observe_tls, publish_attempt, sound, 1.0f, 2.0f, uint64_t{3}, uint64_t{4},
        uint8_t{5}, arg6);
    require(observed.native.target == Target::None
            && observed.native.result == Result::NotCalled
            && observed.native.call_count == 0 && native_calls == 0
            && observation_calls == 1 && order == "observe;"
            && observed.post_return_observation_ran
            && observed.attempt.selected_target == Target::None
            && observed.attempt.attempt_count == 1
            && !observed.attempt.invocation_started
            && observed.attempt.native_return_captured
            && attempt_publications == 2
            && published_attempt.native_return_captured
            && bgm_playback_native_play_setup_return_tuple_authoritative(
                published_attempt)
            && observed.original_forwarded,
        "missing native PlaySetup target skipped return/TLS observation");

    native_calls = observation_calls = attempt_publications = 0;
    published_attempt = {};
    order.clear();
    tls_depth = 1;
    tls_generation = 0x7171;
    tls_claimed = false;
    const auto trampoline =
        select_bgm_playback_native_play_setup_target(true);
    observed = run_bgm_playback_native_play_setup_return_observation(
        trampoline,
        [&](Target target, void* actual_sound, float actual_arg1,
            float actual_arg2, uint64_t actual_arg3, uint64_t actual_arg4,
            uint8_t actual_flag, void* actual_arg6) {
            require(attempt_publications == 1
                    && published_attempt.selected_target == Target::Trampoline
                    && published_attempt.attempt_count == 1
                    && published_attempt.invocation_started
                    && !published_attempt.native_return_captured,
                "native PlaySetup attempt was not published before invocation");
            require(target == Target::Trampoline && actual_sound == sound
                    && actual_arg1 == 1.0f && actual_arg2 == 2.0f
                    && actual_arg3 == 3 && actual_arg4 == 4
                    && actual_flag == 5 && actual_arg6 == arg6,
                "native PlaySetup policy changed forwarded arguments");
            ++native_calls;
            order += "native;";
            // Simulate the TLS effects expected from transparent nested
            // Set/Play/Stop callbacks; this direct seam test does not run hooks.
            require(tls_depth == 1 && tls_generation == 0x7171
                    && !tls_claimed,
                "nested callbacks did not observe outer PlaySetup TLS");
            tls_claimed = true;
        }, observe_tls, publish_attempt, sound, 1.0f, 2.0f, uint64_t{3}, uint64_t{4},
        uint8_t{5}, arg6);
    require(observed.native.result == Result::CalledReturned
            && observed.native.call_count == 1 && native_calls == 1
            && observation_calls == 1 && order == "native;observe;"
            && observed.tls.original_depth == 1
            && observed.tls.expected_generation == 0x7171
            && observed.tls.play_claimed
            && observed.post_return_observation_ran
            && observed.attempt.selected_target == Target::Trampoline
            && observed.attempt.attempt_count == 1
            && observed.attempt.invocation_started
            && observed.attempt.native_return_captured
            && attempt_publications == 2
            && observed.original_forwarded,
        "native PlaySetup return/TLS observation lost nested callback state");

    native_calls = observation_calls = attempt_publications = 0;
    published_attempt = {};
    order.clear();
    bool propagated = false;
    std::atomic_bool inject_play_setup_exception{true};
    try {
        (void)run_bgm_playback_native_play_setup_return_observation(
            trampoline,
            [&](Target, void*, float, float, uint64_t, uint64_t, uint8_t,
                void*) {
                require(attempt_publications == 1
                        && published_attempt.attempt_count == 1
                        && published_attempt.invocation_started
                        && !published_attempt.native_return_captured,
                    "throwing native PlaySetup attempt was not captured");
                ++native_calls;
                order += "native;";
                if (inject_play_setup_exception.load(
                        std::memory_order_relaxed)) {
                    throw std::runtime_error(
                        "injected native PlaySetup exception");
                }
            }, observe_tls, publish_attempt, sound, 1.0f, 2.0f,
            uint64_t{3}, uint64_t{4},
            uint8_t{5}, arg6);
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    require(propagated && native_calls == 1 && observation_calls == 0
            && order == "native;" && attempt_publications == 1
            && published_attempt.selected_target == Target::Trampoline
            && published_attempt.attempt_count == 1
            && published_attempt.invocation_started
            && !published_attempt.native_return_captured
            && !bgm_playback_native_play_setup_return_tuple_authoritative(
                published_attempt),
        "native PlaySetup exception was caught, retried, or post-observed");

    require(bgm_playback_native_play_setup_original_forwarded(
                legal_outcomes[0])
            && bgm_playback_native_play_setup_original_forwarded(
                legal_outcomes[1])
            && !bgm_playback_native_play_setup_original_forwarded(
                illegal_outcomes[0]),
        "native PlaySetup original_forwarded compatibility marker changed");
}

} // namespace

int main()
{
    test_bgm_playback_native_set_call_policy();
    test_bgm_playback_native_play_call_policy();
    test_bgm_playback_native_stop_call_policy();
    test_bgm_playback_native_play_setup_call_policy();
    return 0;
}
