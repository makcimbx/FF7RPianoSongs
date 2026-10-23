#pragma once

#include "core/config.h"
#include "core/hooks.h"
#include "game/menu_session_authority.h"
#include <cstdint>
#include <filesystem>
#include <windows.h>

namespace ff7r::piano::game::menu_theme_runtime {
// Startup worker only, before callback admission. Optional failure never fails startup.
// Complete programs are admitted atomically across main, genuine song setups and
// current uniform SpeedIcon members, the current Cursor provider, and Preview values.
// Missing capabilities never apply a subset; replaced subjects require a new qualified baseline.
// Inventory/reopen counts are not quotas; actual backend allocation/byte checks govern resources.
void configure(HMODULE executable, const std::filesystem::path& themes,
    const std::wstring& ini, const core::Config&) noexcept;
// Every owned open/close/input entry shadows an in-progress native operation,
// including entries which will merely forward their original.
void callback_entered() noexcept;
// Terminal owner retirement without a qualified close stack: never restore stale UI.
void retire() noexcept;
// Borrow the actual hook frame's native return slot and gate lease synchronously.
// These seams are not authority tokens and never retain either argument.
// Genuine Ready publishes Original, then prepares one persisted custom program
// synchronously when its receivers are ready. Explicit gestures do the same for
// one requested program; each Song setup repairs only its actual physical row.
void ready(void* list, std::uint64_t notified_generation, const void* native_return_slot,
    const core::HookCallbackGate::Lease&) noexcept;
void close(void* list, const void* native_return_slot,
    const core::HookCallbackGate::Lease&) noexcept;
// Existing title poller only: samples are values, never native/UI authority.
// Gestures cycle Original and completely prepared programs only; unfinished IDs
// never advance an invisible selection. With no alternate ready program, one early
// intent applies on completion; repeats coalesce, opposite direction/focus/close cancel.
// Idle samples may post maintenance even unfocused; input still requires focus.
// Deferred maintenance services one semantic unit/cleanup, never native work on
// the poller or a self-posting drain. Focus loss does not revoke visible styling.
int function_key() noexcept;
void poll(bool focused, bool keyboard_held, bool previous_held = false, bool next_held = false) noexcept;
// Window focus notification only: atomically revoke queued input, preserve held state.
void focus_lost() noexcept;
void posted_input(const void* owned_dispatch_frame, std::uint64_t ticket,
    const core::HookCallbackGate::Lease&) noexcept;
// Metadata only. A session-bound, consumed-once fallback wake; never native work
// on the worker or an automatically replenished continuation chain.
std::uint64_t reserve_preparation_continuation() noexcept;
bool post_preparation_continuation(std::uint64_t ticket) noexcept;
void cancel_preparation_continuation(std::uint64_t ticket) noexcept;
bool preparation_dispatch_active() noexcept;
// Called only after Ready's Scope and mutex retire, for actual leftover work.
void wake_preparation_after_ready() noexcept;
// Implemented by title's existing window hook. The qualifier recognizes ONLY its
// current private message's stack-local frame, never a retained permission token.
bool post_input(std::uint64_t ticket, bool preparation = false) noexcept;
bool qualify_owned_dispatch(const void* frame, std::uint64_t ticket,
    const MenuSessionSnapshot&, bool preparation = false) noexcept;
// Composition into title's already-owned input service, before admission.
// No second hook and no dependency of the title service on theme availability.
void bind_owned_input(void (*entered)() noexcept,
    int (*key)() noexcept, void (*sample)(bool, bool, bool, bool) noexcept, void (*lost)() noexcept,
    void (*dispatch)(const void*, std::uint64_t, const core::HookCallbackGate::Lease&) noexcept) noexcept;
// Admission must already be closed/drained. No native cleanup.
void disable() noexcept;

// Optional setter hooks participate in the existing initialized hook composition.
void install_palette_hooks() noexcept;
bool uninstall_palette_hooks() noexcept;

enum class RowSetupSource { NativeSetup, OwnedRefresh };
using RowSetupObserver = void (*)(const void*, void*, void*, std::int32_t, std::uint64_t,
    bool, const core::HookCallbackGate::Lease&, RowSetupSource) noexcept;
using RowSetupRetired = void (*)(const void*) noexcept;
using SelectionObserver = void (*)(const void*, void*, std::int32_t, bool,
    const core::HookCallbackGate::Lease&) noexcept;
// Startup composition only; the owning modules keep the private current frames.
void bind_owned_row_setup(RowSetupObserver, RowSetupRetired) noexcept;
void bind_owned_selection(SelectionObserver) noexcept;
bool qualify_owned_row_setup(const void*, const MenuSessionSnapshot&, void*, std::int32_t,
    std::uint64_t, bool) noexcept;
bool qualify_owned_selection(const void*, const MenuSessionSnapshot&, std::int32_t, bool after) noexcept;
} // namespace ff7r::piano::game::menu_theme_runtime
