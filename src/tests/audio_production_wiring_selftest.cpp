#include "game/audio_production_wiring.h"
#include "game/rvas.h"

#include <cassert>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace ff7r::piano::game;

namespace {
int outer_enters = 0;
int outer_exits = 0;
int set_calls = 0;
int play_calls = 0;
int stop_calls = 0;
int setup_calls = 0;
void* seen_controller = nullptr;
void* seen_sound = nullptr;
float seen_f1 = 0;
float seen_f2 = 0;
uint64_t seen_u1 = 0;
uint64_t seen_u2 = 0;
uint8_t seen_flag = 0;
void* seen_tail = nullptr;
bool state_lock_held = false;
bool nest_from_setup = false;

void __fastcall set_stub(void* controller, void* sound) {
    assert(!state_lock_held);
    ++set_calls;
    seen_controller = controller;
    seen_sound = sound;
}
void __fastcall set_throw(void*, void*) { throw std::runtime_error("set"); }
void __fastcall play_stub(void* controller) {
    assert(!state_lock_held);
    ++play_calls;
    seen_controller = controller;
}
void __fastcall play_throw(void*) { throw std::runtime_error("play"); }
void __fastcall stop_stub(void* controller) {
    assert(!state_lock_held);
    ++stop_calls;
    seen_controller = controller;
}
void __fastcall setup_stub(void* sound, float f1, float f2, uint64_t u1,
    uint64_t u2, uint8_t flag, void* tail) {
    assert(!state_lock_held);
    ++setup_calls;
    seen_sound = sound;
    seen_f1 = f1;
    seen_f2 = f2;
    seen_u1 = u1;
    seen_u2 = u2;
    seen_flag = flag;
    seen_tail = tail;
    if (!nest_from_setup) return;
    const auto outer = audio_production_play_setup_tls();
    {
        AudioProductionCallbackScope callback(AudioRouteCallbackKind::Set);
        assert(callback);
        assert(audio_production_callback_kind() == AudioRouteCallbackKind::Set);
        const auto nested = audio_production_play_setup_tls();
        assert(nested.original_depth == outer.original_depth);
        assert(nested.expected_generation == outer.expected_generation);
        assert(invoke_audio_production_set(reinterpret_cast<void*>(0x31),
            reinterpret_cast<void*>(0x32)).call_count == 1);
    }
    assert(audio_production_callback_kind() == AudioRouteCallbackKind::PlaySetup);
    {
        AudioProductionCallbackScope callback(AudioRouteCallbackKind::Play);
        assert(callback);
        assert(audio_production_callback_kind() == AudioRouteCallbackKind::Play);
        audio_production_claim_play_setup_play();
        assert(invoke_audio_production_play(reinterpret_cast<void*>(0x33)).call_count == 1);
    }
    assert(audio_production_callback_kind() == AudioRouteCallbackKind::PlaySetup);
    {
        AudioProductionCallbackScope callback(AudioRouteCallbackKind::Stop);
        assert(callback);
        assert(audio_production_callback_kind() == AudioRouteCallbackKind::Stop);
        assert(invoke_audio_production_stop(reinterpret_cast<void*>(0x34)).call_count == 1);
    }
    assert(audio_production_callback_kind() == AudioRouteCallbackKind::PlaySetup);
}

void reset_native() {
    clear_audio_production_native_bindings();
    set_calls = play_calls = stop_calls = setup_calls = 0;
    seen_controller = seen_sound = seen_tail = nullptr;
}
}

namespace ff7r::piano::game {
void audio_production_outer_callback_entered() noexcept { ++outer_enters; }
void audio_production_outer_callback_exited(bool) noexcept { ++outer_exits; }
}

int main() {
    audio_production_open_callback_admission();

    // Production fixed bindings: unavailable means exact zero calls.
    reset_native();
    assert(invoke_audio_production_set(reinterpret_cast<void*>(1), nullptr).call_count == 0);
    assert(invoke_audio_production_play(reinterpret_cast<void*>(1)).call_count == 0);
    assert(invoke_audio_production_stop(reinterpret_cast<void*>(1)).call_count == 0);
    BgmPlaybackNativePlaySetupAttemptState attempt;
    auto setup = invoke_audio_production_play_setup(reinterpret_cast<void*>(1),
        1, 2, 3, 4, 5, reinterpret_cast<void*>(6), attempt);
    assert(setup.native.call_count == 0 && setup.post_return_observation_ran);

    production_bgm_slot_set_trampoline() = set_stub;
    auto set = invoke_audio_production_set(reinterpret_cast<void*>(0x11), nullptr);
    assert(set.call_count == 1 && set_calls == 1 && seen_sound == nullptr);
    set = invoke_audio_production_set(reinterpret_cast<void*>(0x12),
        reinterpret_cast<void*>(0x13));
    assert(set.call_count == 1 && set_calls == 2
        && seen_controller == reinterpret_cast<void*>(0x12)
        && seen_sound == reinterpret_cast<void*>(0x13));

    // Direct-RVA is the same fixed production target projection, without an
    // injectable alternate callback.
    production_bgm_slot_set_trampoline() = nullptr;
    bind_audio_production_executable(reinterpret_cast<HMODULE>(
        reinterpret_cast<uintptr_t>(&set_stub) - rva::BgmSlotSet));
    set = invoke_audio_production_set(reinterpret_cast<void*>(0x14),
        reinterpret_cast<void*>(0x15));
    assert(set.call_count == 1 && set.target == BgmPlaybackNativeSetTarget::DirectRva
        && set_calls == 3 && seen_sound == reinterpret_cast<void*>(0x15));

    reset_native();
    production_bgm_slot_play_trampoline() = play_stub;
    production_bgm_slot_stop_trampoline() = stop_stub;
    assert(invoke_audio_production_play(reinterpret_cast<void*>(0x21)).call_count == 1);
    assert(invoke_audio_production_stop(reinterpret_cast<void*>(0x22)).call_count == 1);
    assert(play_calls == 1 && stop_calls == 1);

    // C++ containment remains the authoritative protected boundary around the
    // production invocation. Platform SEH projection is tested deterministically
    // by its result constructor rather than unsafe fault injection.
    production_bgm_slot_set_trampoline() = set_throw;
    const auto cpp_fault = bgm_playback_protected_native_boundary(
        [] { return bgm_playback_native_set_compatibility_result(
            invoke_audio_production_set(nullptr, nullptr)); }, []() noexcept {});
    assert(!cpp_fault.succeeded
        && cpp_fault.fault == BgmPlaybackProtectedNativeFault::CppException);
    production_bgm_slot_play_trampoline() = play_throw;
    const auto play_cpp_fault = bgm_playback_protected_native_boundary(
        [] { return bgm_playback_native_play_compatibility_result(
            invoke_audio_production_play(nullptr)); }, []() noexcept {});
    assert(!play_cpp_fault.succeeded
        && play_cpp_fault.fault == BgmPlaybackProtectedNativeFault::CppException);
    const auto seh_projection = bgm_playback_native_set_structured_exception_outcome(
        BgmPlaybackNativeSetTarget::Trampoline, true);
    assert(seh_projection.call_count == 1
        && seh_projection.result == BgmPlaybackNativeSetCallResult::StructuredException);

    reset_native();
    production_bgm_slot_set_trampoline() = set_stub;
    production_bgm_slot_play_trampoline() = play_stub;
    production_bgm_slot_stop_trampoline() = stop_stub;
    production_play_setup_trampoline() = setup_stub;
    nest_from_setup = true;
    outer_enters = outer_exits = 0;
    {
        AudioProductionCallbackScope callback(AudioRouteCallbackKind::PlaySetup);
        assert(callback && audio_production_callback_depth() == 1);
        assert(audio_production_callback_kind() == AudioRouteCallbackKind::PlaySetup);
        std::lock_guard<std::recursive_mutex> outer(audio_production_operation_mutex());
        std::lock_guard<std::recursive_mutex> recursive(audio_production_operation_mutex());
        AudioProductionPlaySetupScope tls(0x8877665544332211ULL);
        setup = invoke_audio_production_play_setup(reinterpret_cast<void*>(0x41),
            1.25f, -2.5f, 0x1122334455667788ULL, 0x99aabbccddeeff00ULL,
            0x7d, reinterpret_cast<void*>(0x42), attempt);
        assert(setup.native.call_count == 1 && setup_calls == 1);
        assert(seen_f1 == 1.25f && seen_f2 == -2.5f
            && seen_u1 == 0x1122334455667788ULL
            && seen_u2 == 0x99aabbccddeeff00ULL && seen_flag == 0x7d
            && seen_tail == reinterpret_cast<void*>(0x42));
        const auto tls_after = audio_production_play_setup_tls();
        assert(tls_after.original_depth == 1
            && tls_after.expected_generation == 0x8877665544332211ULL
            && tls_after.play_claimed);
        assert(audio_production_callback_kind() == AudioRouteCallbackKind::PlaySetup);
        assert(audio_production_callback_depth() == 1);
    }
    nest_from_setup = false;
    assert(outer_enters == 1 && outer_exits == 1);
    assert(audio_production_callback_depth() == 0);
    assert(audio_production_callback_kind() == AudioRouteCallbackKind::None);
    assert(audio_production_play_setup_tls().original_depth == 0);

    // Install attempts in order and rolls every attempted hook back in reverse.
    std::vector<std::string> events;
    std::vector<AudioProductionInstallStep> steps;
    for (int i = 0; i != 4; ++i) {
        steps.push_back({[&, i] { events.push_back("i" + std::to_string(i)); return i != 2; },
            [&, i] { events.push_back("d" + std::to_string(i)); return true; },
            [&, i] { events.push_back("r" + std::to_string(i)); return true; }});
    }
    auto install = execute_audio_production_install(steps);
    assert(install.status == AudioProductionInstallStatus::RolledBack);
    assert(install.failed_step == 2);
    assert((events == std::vector<std::string>{
        "i0", "i1", "i2", "d2", "d1", "d0", "r2", "r1", "r0"}));
    events.clear();
    steps = {{[&] { events.push_back("install"); return false; },
        [&] { events.push_back("disable"); return false; },
        [&] { events.push_back("remove"); return true; }}};
    install = execute_audio_production_install(steps);
    assert(install.status == AudioProductionInstallStatus::Retained);
    assert(install.failed_step == 0);
    assert((events == std::vector<std::string>{"install", "disable"}));
    for (const size_t failed_step : {size_t{0}, size_t{3}}) {
        steps.clear();
        for (size_t i = 0; i != 4; ++i) {
            steps.push_back({[=] { return i != failed_step; },
                [] { return true; }, [] { return true; }});
        }
        install = execute_audio_production_install(steps);
        assert(install.status == AudioProductionInstallStatus::RolledBack
            && install.failed_step == failed_step);
    }
    steps.clear();
    for (size_t i = 0; i != 4; ++i) {
        steps.push_back({[] { return true; }, [] { return true; },
            [] { return true; }});
    }
    install = execute_audio_production_install(steps);
    assert(install.status == AudioProductionInstallStatus::Installed
        && install.failed_step == steps.size());
    steps[2].install = []() -> bool { throw std::runtime_error("install"); };
    install = execute_audio_production_install(steps);
    assert(install.status == AudioProductionInstallStatus::RolledBack
        && install.failed_step == 2);

    // Shutdown callback ownership and cleanup failure retain authority; only
    // cleanup failure reopens admission. Clear is published after release only.
    AudioProductionShutdownPlan plan;
    int phase = 0;
    plan.cleanup_ready = [&] { assert(++phase == 1); return true; };
    plan.disable_hooks = [&] { assert(++phase == 2); return true; };
    plan.aggregate_rollback = [&] { assert(++phase == 3); return true; };
    plan.restore_and_release = [&] { assert(++phase == 4); return true; };
    plan.publish_clear = [&] { assert(++phase == 5); };
    {
        AudioProductionCallbackScope callback(AudioRouteCallbackKind::Stop);
        const auto active = execute_audio_production_shutdown(plan);
        assert(active.failed_phase == AudioProductionShutdownPhase::CallbackOwned
            && phase == 0 && !active.clear_published);
    }
    plan.cleanup_ready = [&] { ++phase; return false; };
    auto shutdown = execute_audio_production_shutdown(plan);
    assert(shutdown.failed_phase == AudioProductionShutdownPhase::CleanupReadiness
        && shutdown.admission_reopened && !shutdown.clear_published);
    {
        AudioProductionCallbackScope callback;
        assert(callback); // admission was deterministically reopened
    }
    audio_production_open_callback_admission();
    phase = 0;
    plan.cleanup_ready = [&] { assert(++phase == 1); return true; };
    plan.disable_hooks = [&] { assert(++phase == 2); return true; };
    plan.aggregate_rollback = [&] { assert(++phase == 3); return true; };
    plan.restore_and_release = [&] { assert(++phase == 4); return true; };
    plan.publish_clear = [&] { assert(phase == 4); ++phase; };
    shutdown = execute_audio_production_shutdown(plan);
    assert(shutdown.succeeded() && phase == 5 && shutdown.clear_published);

    // A delayed outer callback is represented by the lifecycle lease, not by
    // asking a caller to wait before shutdown. The timeout is only a bounded
    // failure result and never publishes clear eligibility.
    audio_production_open_callback_admission();
    std::atomic_bool callback_ready{false};
    std::atomic_bool release_callback{false};
    std::thread delayed([&] {
        AudioProductionCallbackScope callback(AudioRouteCallbackKind::Play);
        assert(callback);
        callback_ready.store(true, std::memory_order_release);
        while (!release_callback.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });
    while (!callback_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    bool drain_clear = false;
    plan.cleanup_ready = [] { return true; };
    plan.disable_hooks = [] { return true; };
    plan.aggregate_rollback = [] { return true; };
    plan.restore_and_release = [] { return true; };
    plan.publish_clear = [&] { drain_clear = true; };
    plan.drain_timeout = std::chrono::milliseconds(1);
    shutdown = execute_audio_production_shutdown(plan);
    assert(shutdown.failed_phase == AudioProductionShutdownPhase::CallbackDrain
        && !shutdown.admission_reopened && !drain_clear);
    release_callback.store(true, std::memory_order_release);
    delayed.join();
    plan.drain_timeout = std::chrono::milliseconds(2000);

    for (const auto failed : {AudioProductionShutdownPhase::DisableHooks,
             AudioProductionShutdownPhase::AggregateRollback,
             AudioProductionShutdownPhase::RestoreAndRelease}) {
        audio_production_open_callback_admission();
        bool cleared = false;
        plan.cleanup_ready = [] { return true; };
        plan.disable_hooks = [=] { return failed != AudioProductionShutdownPhase::DisableHooks; };
        plan.aggregate_rollback = [=] { return failed != AudioProductionShutdownPhase::AggregateRollback; };
        plan.restore_and_release = [=] { return failed != AudioProductionShutdownPhase::RestoreAndRelease; };
        plan.publish_clear = [&] { cleared = true; };
        shutdown = execute_audio_production_shutdown(plan);
        assert(shutdown.failed_phase == failed && !cleared && !shutdown.admission_reopened);
    }

    // Every throwing plan callback is projected onto its exact production
    // phase. Only cleanup readiness reopens admission. Publish may have made a
    // partial caller-owned mutation before throwing, but wiring never reports
    // clear publication or attempts an unsafe compensating clear.
    for (const auto failed : {AudioProductionShutdownPhase::CleanupReadiness,
             AudioProductionShutdownPhase::DisableHooks,
             AudioProductionShutdownPhase::AggregateRollback,
             AudioProductionShutdownPhase::RestoreAndRelease,
             AudioProductionShutdownPhase::PublishClear}) {
        audio_production_open_callback_admission();
        bool publish_entered = false;
        plan.cleanup_ready = [=]() -> bool {
            if (failed == AudioProductionShutdownPhase::CleanupReadiness)
                throw std::runtime_error("cleanup");
            return true;
        };
        plan.disable_hooks = [=]() -> bool {
            if (failed == AudioProductionShutdownPhase::DisableHooks)
                throw std::runtime_error("disable");
            return true;
        };
        plan.aggregate_rollback = [=]() -> bool {
            if (failed == AudioProductionShutdownPhase::AggregateRollback)
                throw std::runtime_error("aggregate");
            return true;
        };
        plan.restore_and_release = [=]() -> bool {
            if (failed == AudioProductionShutdownPhase::RestoreAndRelease)
                throw std::runtime_error("restore");
            return true;
        };
        plan.publish_clear = [&, failed] {
            publish_entered = true;
            if (failed == AudioProductionShutdownPhase::PublishClear)
                throw std::runtime_error("publish");
        };
        shutdown = execute_audio_production_shutdown(plan);
        assert(shutdown.failed_phase == failed && !shutdown.clear_published);
        assert(shutdown.admission_reopened
            == (failed == AudioProductionShutdownPhase::CleanupReadiness));
        assert(publish_entered
            == (failed == AudioProductionShutdownPhase::PublishClear));
        if (shutdown.admission_reopened) {
            AudioProductionCallbackScope callback;
            assert(callback);
        }
    }
    reset_native();
}
