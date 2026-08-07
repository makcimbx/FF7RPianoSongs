#include "game/audio_production_wiring.h"

#include "game/rvas.h"

#include <utility>

namespace ff7r::piano::game {
namespace {

ProductionBgmSlotSetFn g_set = nullptr;
ProductionBgmSlotPlayFn g_play = nullptr;
ProductionBgmSlotStopFn g_stop = nullptr;
ProductionPlaySetupFn g_play_setup = nullptr;
HMODULE g_executable = nullptr;
SRWLOCK g_callback_gate = SRWLOCK_INIT;
core::HookCallbackGate g_lifecycle_gate;
std::recursive_mutex g_operation_mutex;

thread_local uint32_t g_callback_depth = 0;
thread_local AudioRouteCallbackKind g_callback_kind = AudioRouteCallbackKind::None;
thread_local uint32_t g_play_setup_depth = 0;
thread_local uint64_t g_play_setup_generation = 0;
thread_local bool g_play_setup_claimed = false;

template <typename Callback>
bool run_shutdown_phase_contained(AudioProductionShutdownExecution& out,
    const AudioProductionShutdownPhase phase, const bool reopen_admission,
    Callback&& callback) noexcept {
    bool succeeded = false;
    try {
        succeeded = std::forward<Callback>(callback)();
    } catch (...) {
        succeeded = false;
    }
    if (succeeded) return true;
    if (reopen_admission) {
        g_lifecycle_gate.open();
        out.admission_reopened = true;
    }
    out.failed_phase = phase;
    return false;
}

} // namespace

ProductionBgmSlotSetFn& production_bgm_slot_set_trampoline() noexcept { return g_set; }
ProductionBgmSlotPlayFn& production_bgm_slot_play_trampoline() noexcept { return g_play; }
ProductionBgmSlotStopFn& production_bgm_slot_stop_trampoline() noexcept { return g_stop; }
ProductionPlaySetupFn& production_play_setup_trampoline() noexcept { return g_play_setup; }

void bind_audio_production_executable(HMODULE module) noexcept { g_executable = module; }

void clear_audio_production_native_bindings() noexcept {
    g_set = nullptr;
    g_play = nullptr;
    g_stop = nullptr;
    g_play_setup = nullptr;
    g_executable = nullptr;
}

bool audio_production_set_available() noexcept {
    return g_set || (g_executable && rva::BgmSlotSet != 0);
}
bool audio_production_play_available() noexcept { return g_play != nullptr; }
bool audio_production_stop_available() noexcept { return g_stop != nullptr; }
bool audio_production_play_setup_available() noexcept { return g_play_setup != nullptr; }

BgmPlaybackNativeSetCallOutcome invoke_audio_production_set(
    void* controller, void* sound) {
    const auto target = select_bgm_playback_native_set_target(
        g_set != nullptr, g_executable != nullptr && rva::BgmSlotSet != 0);
    volatile bool call_started = false;
    __try {
        return invoke_bgm_playback_native_set_exactly_once(target,
            [&call_started](BgmPlaybackNativeSetTarget selected,
                void* forwarded_controller, void* forwarded_sound) {
                call_started = true;
                if (selected == BgmPlaybackNativeSetTarget::Trampoline) {
                    g_set(forwarded_controller, forwarded_sound);
                } else if (selected == BgmPlaybackNativeSetTarget::DirectRva) {
                    auto* direct = reinterpret_cast<ProductionBgmSlotSetFn>(
                        reinterpret_cast<uintptr_t>(g_executable) + rva::BgmSlotSet);
                    direct(forwarded_controller, forwarded_sound);
                }
                return true;
            }, controller, sound);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return bgm_playback_native_set_structured_exception_outcome(
            target, call_started);
    }
}

BgmPlaybackNativePlayCallOutcome invoke_audio_production_play(
    void* controller) {
    const auto target = select_bgm_playback_native_play_target(g_play != nullptr);
    volatile bool call_started = false;
    __try {
        return invoke_bgm_playback_native_play_exactly_once(target,
            [&call_started](BgmPlaybackNativePlayTarget,
                void* forwarded_controller) {
                call_started = true;
                g_play(forwarded_controller);
                return true;
            }, controller);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return bgm_playback_native_play_structured_exception_outcome(
            target, call_started);
    }
}

BgmPlaybackNativeStopCallOutcome invoke_audio_production_stop(void* controller) {
    const auto target = select_bgm_playback_native_stop_target(g_stop != nullptr);
    return invoke_bgm_playback_native_stop_exactly_once(target,
        [](BgmPlaybackNativeStopTarget, void* forwarded_controller) {
            g_stop(forwarded_controller);
        }, controller);
}

BgmPlaybackNativePlaySetupReturnObservation invoke_audio_production_play_setup(
    void* sound, float arg1, float arg2, uint64_t arg3, uint64_t arg4,
    uint8_t flag, void* arg6,
    BgmPlaybackNativePlaySetupAttemptState& attempt) {
    const auto target = select_bgm_playback_native_play_setup_target(
        g_play_setup != nullptr);
    return run_bgm_playback_native_play_setup_return_observation(target,
        [](BgmPlaybackNativePlaySetupTarget, void* forwarded_sound,
            float forwarded_arg1, float forwarded_arg2, uint64_t forwarded_arg3,
            uint64_t forwarded_arg4, uint8_t forwarded_flag, void* forwarded_arg6) {
            g_play_setup(forwarded_sound, forwarded_arg1, forwarded_arg2,
                forwarded_arg3, forwarded_arg4, forwarded_flag, forwarded_arg6);
        },
        [](const BgmPlaybackNativePlaySetupCallOutcome&) {
            return audio_production_play_setup_tls();
        },
        [&attempt](const BgmPlaybackNativePlaySetupAttemptState& current) {
            attempt = current;
        }, sound, arg1, arg2, arg3, arg4, flag, arg6);
}

AudioProductionCallbackScope::AudioProductionCallbackScope(
    const AudioRouteCallbackKind kind)
    : previous_kind_(g_callback_kind) {
    g_callback_kind = kind;
    if (g_callback_depth++ == 0) {
        lifecycle_lease_ = g_lifecycle_gate.try_enter();
        entered_ = static_cast<bool>(lifecycle_lease_);
        AcquireSRWLockShared(&g_callback_gate);
        shared_gate_held_ = true;
        audio_production_outer_callback_entered();
    } else {
        entered_ = true;
    }
}

AudioProductionCallbackScope::~AudioProductionCallbackScope() {
    const uint32_t depth = --g_callback_depth;
    if (depth == 0) {
        lifecycle_lease_ = {};
        if (shared_gate_held_) {
            ReleaseSRWLockShared(&g_callback_gate);
            shared_gate_held_ = false;
        }
        audio_production_outer_callback_exited(entered_);
    }
    g_callback_kind = previous_kind_;
}

AudioProductionShutdownScope::AudioProductionShutdownScope() {
    AcquireSRWLockExclusive(&g_callback_gate);
}
AudioProductionShutdownScope::~AudioProductionShutdownScope() {
    ReleaseSRWLockExclusive(&g_callback_gate);
}

AudioProductionCatalogScope::AudioProductionCatalogScope() noexcept
{
    if (audio_production_callback_depth() != 0) return;
    held_ = TryAcquireSRWLockExclusive(&g_callback_gate) != FALSE;
}

AudioProductionCatalogScope::~AudioProductionCatalogScope()
{
    if (held_) ReleaseSRWLockExclusive(&g_callback_gate);
}

AudioProductionPlaySetupScope::AudioProductionPlaySetupScope(
    const uint64_t generation) noexcept {
    if (g_play_setup_depth++ == 0) {
        g_play_setup_generation = generation;
        g_play_setup_claimed = false;
    }
}
AudioProductionPlaySetupScope::~AudioProductionPlaySetupScope() {
    if (--g_play_setup_depth == 0) {
        g_play_setup_generation = 0;
        g_play_setup_claimed = false;
    }
}

std::recursive_mutex& audio_production_operation_mutex() noexcept {
    return g_operation_mutex;
}
uint32_t audio_production_callback_depth() noexcept { return g_callback_depth; }
AudioRouteCallbackKind audio_production_callback_kind() noexcept { return g_callback_kind; }
BgmPlaybackNativePlaySetupTlsSnapshot audio_production_play_setup_tls() noexcept {
    return {g_play_setup_depth, g_play_setup_generation, g_play_setup_claimed};
}
void audio_production_claim_play_setup_play() noexcept { g_play_setup_claimed = true; }
void audio_production_open_callback_admission() noexcept { g_lifecycle_gate.open(); }

AudioProductionInstallResult execute_audio_production_install(
    std::span<AudioProductionInstallStep> steps) noexcept {
    size_t failed_step = steps.size();
    switch (install_mandatory_hook_transaction(
        std::span<MandatoryHookOperation>(steps.data(), steps.size()),
        &failed_step)) {
    case MandatoryHookTransactionResult::Installed:
        return {AudioProductionInstallStatus::Installed, failed_step};
    case MandatoryHookTransactionResult::RolledBack:
        return {AudioProductionInstallStatus::RolledBack, failed_step};
    case MandatoryHookTransactionResult::Retained:
        return {AudioProductionInstallStatus::Retained, failed_step};
    }
    return {AudioProductionInstallStatus::Retained, steps.size()};
}

AudioProductionShutdownExecution execute_audio_production_shutdown(
    const AudioProductionShutdownPlan& plan) noexcept {
    AudioProductionShutdownExecution out;
    if (g_callback_depth != 0) {
        out.failed_phase = AudioProductionShutdownPhase::CallbackOwned;
        return out;
    }
    g_lifecycle_gate.close();
    if (!run_shutdown_phase_contained(out,
            AudioProductionShutdownPhase::CleanupReadiness, true,
            [&] { return plan.cleanup_ready(); })) return out;
    if (!run_shutdown_phase_contained(out,
            AudioProductionShutdownPhase::DisableHooks, false,
            [&] { return plan.disable_hooks(); })) return out;
    if (!run_shutdown_phase_contained(out,
            AudioProductionShutdownPhase::CallbackDrain, false,
            [&] { return g_lifecycle_gate.drain(plan.drain_timeout); })) return out;
    if (!run_shutdown_phase_contained(out,
            AudioProductionShutdownPhase::AggregateRollback, false,
            [&] { return plan.aggregate_rollback(); })) return out;
    if (!run_shutdown_phase_contained(out,
            AudioProductionShutdownPhase::RestoreAndRelease, false,
            [&] { return plan.restore_and_release(); })) return out;
    if (!run_shutdown_phase_contained(out,
            AudioProductionShutdownPhase::PublishClear, false,
            [&] { plan.publish_clear(); return true; })) return out;
    out.clear_published = true;
    return out;
}

} // namespace ff7r::piano::game
