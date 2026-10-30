#pragma once

#include "game/native_audio_bindings.h"
#include "game/native_audio_gate.h"
#include "core/hook_install_trace.h"

#include <windows.h>
#include <memory>

namespace ff7r::piano::game::native_audio {

struct NativeInstallDiagnostic {
    std::array<core::HookInstallTrace, 12> hooks{};
    core::HookBatchTrace batch;
    std::size_t count = 0;
    std::int64_t begin = 0, end = 0, frequency = 0;
    const char* first_failure = nullptr;
    const char* failed_hook = nullptr;
    const char* proxy_predicate = nullptr;
    std::uintptr_t failed_rva = 0;
    unsigned bootstrap_failure = 0, proxy_failure = 0;
    bool synchronous = false, proxy_entry = false, published = false;
    bool rollback_attempted = false, rollback_completed = false;
};
// Once-only snapshot, available only after the installer and private bootstrap scopes exit.
bool take_native_install_diagnostic(NativeInstallDiagnostic& out) noexcept;
void emit_native_install_diagnostic() noexcept;

enum class RuntimeStage : unsigned char {
    Unavailable, AwaitingInitializer, AwaitingArtifacts, Queued, Mounting,
    Acquiring, Authenticating, Registering, Ready, Failed, Stopped,
    AwaitingStock, // Valid empty stock group0 or contended reader gate; no native cohort calls yet.
};
enum class RuntimeFailure : unsigned char {
    None, UnsupportedBuild, Install, MissedInitializer, Replaced, Shutdown,
    Provenance, Subscription, Artifacts, PackageStore, Mount, Acquisition,
    Names, Discovery, Bindings, Query, Capacity, Stopped,
};
struct RuntimeSnapshot {
    RuntimeStage stage = RuntimeStage::Unavailable;
    RuntimeFailure failure = RuntimeFailure::None;
    BindingResult binding{};
    std::uint64_t generation = 0;
    unsigned char role = 0;
};
struct alignas(8) NativeAccessor {
    std::array<std::uint64_t, 4> words{};
};
// Centralized GU identity and the already-hooked ScoreInfo owner's trampoline.
// Those two callbacks may not take repository/audio locks or invoke another detour.
struct RuntimeServices {
    bool (*identity)(uobject_locator_core::UObjectIdentityAccess&) noexcept = nullptr;
    NativeAccessor* (*scoreinfo_original)(NativeAccessor*, std::uint64_t) = nullptr;
    // Once, on the actual normal full initializer return after persistent capture
    // and outside lifetime/manager/queue locks (including an enclosing factory).
    // Notification only, not audio readiness or a lifetime/generation lease. The
    // The callback lease prevents shutdown completion until notification returns;
    // the host must retain its module as required by the shutdown contract below.
    void (*initializer_complete)() noexcept = nullptr;
};
class PreparedNativeAudio;

// Worker-thread only: verifies and opens immutable cache files denying write and
// delete sharing. It performs no game reads/calls. Retained forever after enqueue.
std::shared_ptr<PreparedNativeAudio> prepare_native_audio(
    std::shared_ptr<const ff7rp::pipeline::NativeAudioArtifactSet>,
    std::uint64_t descriptor_generation) noexcept;

// Production requires the bootstrap helper's live, private synchronous scope;
// revalidates it before/after all selected-operation barriers are installed.
// Unqualified workers/late installs/older profiles are unavailable. The separate
// literal pre-AEP observation/forwarding transaction has an early-source test harness.
// Existing ScoreInfo/selection addresses are never hooked here.
bool install_native_audio_runtime(HMODULE, RuntimeServices) noexcept;
// Finish enqueue before publishing the token to status/selection readers.
// Re-enqueueing the same token is idempotent, not another native attempt.
bool enqueue_native_audio(std::shared_ptr<PreparedNativeAudio>) noexcept;
// Optional original-getter observation, never a readiness prerequisite or an
// arbitrary stock-row baseline. Stock queries use the authenticated artifact's
// find-existing base_score_name. No second selection/ScoreInfo hook is used.
void observe_native_stock_selected_name(std::uint64_t) noexcept;
RuntimeSnapshot native_audio_status(const PreparedNativeAudio&) noexcept;
const char* native_audio_stage_name(RuntimeStage) noexcept;
const char* native_audio_failure_name(RuntimeFailure) noexcept;
// A positive answer is identity/generation-specific; pending/failed never return
// a stock fallback or synthetic selected name. No native call occurs here.
bool native_audio_selected_name(const PreparedNativeAudio&, std::uint64_t generation,
    std::uint64_t& name) noexcept;
struct NativePianoScoreRow {
    std::uintptr_t provider=0,row=0;
    std::uint64_t selected_name=0,cohort_generation=0;
};
// Static predicates and copied facts only; format after capture releases its guards.
struct NativePianoScoreCaptureFacts {
    const char* predicate="not_attempted";
    std::uint64_t generation=0,requested_name=0,observed_name=0;
    RuntimeStage stage=RuntimeStage::Unavailable;
    RuntimeFailure failure=RuntimeFailure::None;
    BindingFailure binding_failure=BindingFailure::None;
    unsigned char binding_role=0;
    bool status_observed=false,binding_observed=false,name_observed=false;
    NativeEntryGateFailureSnapshot manager_gate{};
};
// Read-only snapshot of the retained constructor result, not a mutation lease.
// Requires the exact immutable artifact and parser-selected name. Chart writes
// still require their own activation authority and reversible scoped journal.
bool capture_native_pianoscore_row(const ff7rp::pipeline::NativeAudioArtifactSet&,
    std::uint64_t selected_name,NativePianoScoreRow&,
    NativePianoScoreCaptureFacts* = nullptr) noexcept;
// Close admission first. Native files/owning pairs/registration are deliberately
// retained until process exit; this does NOT release, remove or unmount anything.
// Incomplete is nonblocking and retains protective hooks while work is admitted;
// an explicit later retry is required, never a timer/poll-count teardown proof.
// Actual EXE-entry forwarding may last until normal process exit. Incomplete
// requires the caller to retain the ASI/module AND every hook/trampoline; short
// origin observation is not forwarding completion or permission for live unload.
bool shutdown_native_audio_runtime() noexcept;

} // namespace ff7r::piano::game::native_audio
