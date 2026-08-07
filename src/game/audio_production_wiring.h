#pragma once

#include "core/hooks.h"
#include "game/audio_cleanup_policy.h"
#include "game/audio_native_call_policy.h"
#include "game/mandatory_hook_transaction.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>

namespace ff7r::piano::game {

enum class AudioRouteCallbackKind : uint8_t {
    None, PlaybackTransfer, PlaySetup, Prepare, Set, Play, Stop,
    ManagerPause, SlotTransition, AdaptiveJudgment, PianoAudioTick,
    SelectionReservation, SelectionAdmission, Arm, ListReturn,
};

using ProductionBgmSlotSetFn = void(__fastcall*)(void*, void*);
using ProductionBgmSlotPlayFn = void(__fastcall*)(void*);
using ProductionBgmSlotStopFn = void(__fastcall*)(void*);
using ProductionPlaySetupFn = void(__fastcall*)(
    void*, float, float, uint64_t, uint64_t, uint8_t, void*);

// These slots are written only by the catalog-validated production hook
// installer. Calls below select from this fixed production binding; callers do
// not supply alternate native authority.
ProductionBgmSlotSetFn& production_bgm_slot_set_trampoline() noexcept;
ProductionBgmSlotPlayFn& production_bgm_slot_play_trampoline() noexcept;
ProductionBgmSlotStopFn& production_bgm_slot_stop_trampoline() noexcept;
ProductionPlaySetupFn& production_play_setup_trampoline() noexcept;
void bind_audio_production_executable(HMODULE module) noexcept;
void clear_audio_production_native_bindings() noexcept;

bool audio_production_set_available() noexcept;
bool audio_production_play_available() noexcept;
bool audio_production_stop_available() noexcept;
bool audio_production_play_setup_available() noexcept;

BgmPlaybackNativeSetCallOutcome invoke_audio_production_set(
    void* controller, void* sound);
BgmPlaybackNativePlayCallOutcome invoke_audio_production_play(
    void* controller);
BgmPlaybackNativeStopCallOutcome invoke_audio_production_stop(
    void* controller);
BgmPlaybackNativePlaySetupReturnObservation invoke_audio_production_play_setup(
    void* sound, float arg1, float arg2, uint64_t arg3, uint64_t arg4,
    uint8_t flag, void* arg6,
    BgmPlaybackNativePlaySetupAttemptState& attempt);

// Implemented by audio_sead.cpp. Keeping these as concrete parent bridges
// avoids installing caller-selectable callbacks in production wiring.
void audio_production_outer_callback_entered() noexcept;
void audio_production_outer_callback_exited(bool entered) noexcept;

class AudioProductionCallbackScope {
public:
    explicit AudioProductionCallbackScope(
        AudioRouteCallbackKind kind = AudioRouteCallbackKind::None);
    AudioProductionCallbackScope(const AudioProductionCallbackScope&) = delete;
    AudioProductionCallbackScope& operator=(
        const AudioProductionCallbackScope&) = delete;
    ~AudioProductionCallbackScope();
    explicit operator bool() const noexcept { return entered_; }

private:
    core::HookCallbackGate::Lease lifecycle_lease_;
    AudioRouteCallbackKind previous_kind_ = AudioRouteCallbackKind::None;
    bool entered_ = false;
    bool shared_gate_held_ = false;
};

class AudioProductionShutdownScope {
public:
    AudioProductionShutdownScope();
    AudioProductionShutdownScope(const AudioProductionShutdownScope&) = delete;
    AudioProductionShutdownScope& operator=(
        const AudioProductionShutdownScope&) = delete;
    ~AudioProductionShutdownScope();
};

class AudioProductionCatalogScope {
public:
    AudioProductionCatalogScope() noexcept;
    AudioProductionCatalogScope(const AudioProductionCatalogScope&) = delete;
    AudioProductionCatalogScope& operator=(const AudioProductionCatalogScope&) = delete;
    ~AudioProductionCatalogScope();
    explicit operator bool() const noexcept { return held_; }
private:
    bool held_ = false;
};

class AudioProductionPlaySetupScope {
public:
    explicit AudioProductionPlaySetupScope(uint64_t generation) noexcept;
    AudioProductionPlaySetupScope(const AudioProductionPlaySetupScope&) = delete;
    AudioProductionPlaySetupScope& operator=(
        const AudioProductionPlaySetupScope&) = delete;
    ~AudioProductionPlaySetupScope();
};

std::recursive_mutex& audio_production_operation_mutex() noexcept;
uint32_t audio_production_callback_depth() noexcept;
AudioRouteCallbackKind audio_production_callback_kind() noexcept;
BgmPlaybackNativePlaySetupTlsSnapshot audio_production_play_setup_tls() noexcept;
void audio_production_claim_play_setup_play() noexcept;
void audio_production_open_callback_admission() noexcept;

using AudioProductionInstallStep = MandatoryHookOperation;

enum class AudioProductionInstallStatus : uint8_t {
    Installed,
    RolledBack,
    Retained,
};

struct AudioProductionInstallResult {
    AudioProductionInstallStatus status;
    // Zero-based step whose install returned false or threw. steps.size() is
    // the explicit sentinel when every install succeeded.
    size_t failed_step;
};

AudioProductionInstallResult execute_audio_production_install(
    std::span<AudioProductionInstallStep> steps) noexcept;

enum class AudioProductionShutdownPhase : uint8_t {
    None,
    CallbackOwned,
    CleanupReadiness,
    DisableHooks,
    CallbackDrain,
    AggregateRollback,
    RestoreAndRelease,
    PublishClear,
};

struct AudioProductionShutdownPlan {
    std::function<bool()> cleanup_ready;
    std::function<bool()> disable_hooks;
    std::function<bool()> aggregate_rollback;
    std::function<bool()> restore_and_release;
    std::function<void()> publish_clear;
    std::chrono::milliseconds drain_timeout{2000};
};

struct AudioProductionShutdownExecution {
    AudioProductionShutdownPhase failed_phase = AudioProductionShutdownPhase::None;
    bool admission_reopened = false;
    bool clear_published = false;
    bool succeeded() const noexcept {
        return failed_phase == AudioProductionShutdownPhase::None
            && clear_published;
    }
};

AudioProductionShutdownExecution execute_audio_production_shutdown(
    const AudioProductionShutdownPlan& plan) noexcept;

} // namespace ff7r::piano::game
