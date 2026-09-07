#include "game/menu_session_authority.h"
#include "game/catalog_adoption.h"
#include "game/mandatory_hook_transaction.h"
#include "game/menu_focus_restore.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "game/audio_sead.h"
#include "game/hook_specs.h"
#include "game/module_hooks.h"
#include "game/profile_list_coordinator.h"
#include "game/generated/rvas.generated.h"
#include "game/uobject_identity.h"

#include <intrin.h>

#include <cstring>
#include <string>

namespace ff7r::piano::game {
namespace {

constexpr ptrdiff_t kListMemberOffset = 0x6f0;
constexpr ptrdiff_t kWidgetWeakHandleOffset = 0x7a0;
constexpr ptrdiff_t kActiveWordOffset = 0x7d8;
constexpr uintptr_t kOpenReturnRva = rva::PianoMenuListOpenCall + 5;
constexpr uintptr_t kListCloseReturnRva = rva::PianoMenuListCancelCloseCall + 5;

using OpenFn = void(__fastcall*)(void*, uint64_t);
using CloseFn = void(__fastcall*)(void*);
using ControllerFn = void(__fastcall*)(void*);

OpenFn g_original_open = nullptr;
CloseFn g_original_close = nullptr;
ControllerFn g_original_exit = nullptr;
ControllerFn g_original_destructor = nullptr;
HMODULE g_module = nullptr;
ControllerFn g_restore_selection = nullptr;
core::RawRvaHook g_open_hook;
core::RawRvaHook g_close_hook;
core::RawRvaHook g_exit_hook;
core::RawRvaHook g_destructor_hook;
core::RawRvaHook g_restore_hook;

bool resolve_list_identity(void* list, void*& widget, UObjectLiveHandle& identity) noexcept {
    widget = nullptr;
    struct WeakHandle { int32_t index; int32_t serial; } weak{};
    runtime_layouts::GUObjectArrayView objects{};
    if (!list || !core::safe_read_field(list, kWidgetWeakHandleOffset, weak)
        || weak.index < 0 || weak.serial <= 0 || !read_list_guobject_array(objects)
        || !read_list_uobject_item_object(objects, weak.index, widget) || !widget)
        return false;
    identity = {weak.index, weak.serial};
    return validate_live_uobject_handle(widget, identity);
}

bool read_active_word(void* list, uint16_t& word) noexcept {
    return list && core::safe_read_field(list, kActiveWordOffset, word);
}

SelectionActivationTerminalResult terminate_terminal_audio(const MenuSessionSnapshot& session,
    SelectionActivationTerminalReason reason) noexcept {
    if (!session) return {};
    const SelectionSnapshot selection = registry().selection_snapshot();
    return terminate_selection_audio_activation_for_menu_session(
        session, selection, reason);
}

void log_list_close(const char* classification, const bool before_read,
    const uint16_t before, const bool after_read, const uint16_t after,
    const SelectionActivationTerminalResult& result) noexcept
{
    try {
        std::ostringstream out;
        out << "[menu_session] shared_close classification=" << classification
            << " word_before=";
        if (before_read) out << "0x" << std::hex << before << std::dec;
        else out << "unreadable";
        out << " word_after=";
        if (after_read) out << "0x" << std::hex << after << std::dec;
        else out << "unreadable";
        out << " transfer_before="
            << selection_activation_transfer_state_name(result.transfer_before)
            << " transfer_after="
            << selection_activation_transfer_state_name(result.transfer_after)
            << " first_failure="
            << selection_activation_terminal_failure_name(result.first_failure);
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {}
}

bool signature_matches(HMODULE module, const char* id) noexcept {
    const auto* spec = find_rva_signature(id);
    if (!module || !spec || spec->expected_prologue.empty()) return false;
    const auto image = core::image_range(module);
    return spec->rva < image.size && spec->expected_prologue.size() <= image.size - spec->rva
        && core::bytes_equal(image.base + spec->rva, spec->expected_prologue);
}

bool install_one(const HookInstallContext& context, const char* id, void* detour,
    void** original, core::RawRvaHook& hook) {
    const auto* spec = find_hook_spec(id);
    if (!spec) return false;
    std::string error;
    if (hook.install(context.exe_module, spec->rva, spec->expected_prologue,
            detour, original, error)) return true;
    core::log(core::LogLevel::Error, "[menu_session] install_failed hook="
        + std::string(id) + " error=" + error);
    return false;
}

void __fastcall restore_selection_detour(void* widget) noexcept {
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto original = g_restore_selection;
    if (!original) return;
    auto lease = non_audio_hook_gate().try_enter();
    const bool exact = lease && g_module && ret == reinterpret_cast<uintptr_t>(g_module)
        + rva::PianoListRestoreSelectionCaller + 0x18;
    intercept_menu_open_focus(widget, exact, [&] { original(widget); },
        [&](MenuOpenFocusContext& context) {
            return project_last_played_menu_focus(context, original);
        });
}

void __fastcall open_detour(void* list, uint64_t selected) noexcept {
    // Every entry shadows outer authority, including unrelated/declined Opens.
    MenuOpenFocusScope ineligible_scope(nullptr);
    auto original = g_original_open;
    if (!original) return;
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool exact_call_site = g_module
        && ret == reinterpret_cast<uintptr_t>(g_module) + kOpenReturnRva;
    if (classify_menu_open_admission(exact_call_site, false, true)
        == MenuOpenAdmissionDisposition::ForwardOriginal) {
        try { original(list, selected); } catch (...) {}
        return;
    }
    if (classify_menu_open_admission(true,
            piano_list_catalog_terminal_failure(), true)
        != MenuOpenAdmissionDisposition::Protected) return;
    auto lease = non_audio_hook_gate().try_enter();
    const auto admission = classify_menu_open_admission(true,
        piano_list_catalog_terminal_failure(), static_cast<bool>(lease));
    if (admission == MenuOpenAdmissionDisposition::ForwardOriginal) {
        try { original(list, selected); } catch (...) {}
        return;
    }
    if (admission == MenuOpenAdmissionDisposition::SuppressTerminalFailure) return;
    MenuSessionSnapshot opening{};
    CatalogAdoptionResult adoption = CatalogAdoptionResult::NoPending;
    try {
        uint16_t word = 0; void* widget = nullptr; UObjectLiveHandle identity{};
        void* controller = static_cast<uint8_t*>(list) - kListMemberOffset;
        (void)reclaim_selection_audio_activation_for_controller(
            controller, SelectionActivationTerminalReason::Reopen);
        (void)resolve_piano_menu_widget_binding(list, widget, identity);
        adoption = try_adopt_pending_catalog_before_menu_open(
            list, widget, identity, lease);
        observe_catalog_adoption_result(adoption);
        if (classify_menu_open_catalog_result(true, adoption)
            == MenuOpenCatalogDisposition::SuppressTerminalFailure) {
            try {
                std::ostringstream out;
                out << "[menu_session] open_suppressed reason=catalog_terminal_failure"
                    << " authoritative_attempt=true";
                core::log(core::LogLevel::Error, out.str());
            } catch (...) {}
            return;
        }
        if (read_active_word(list, word)
            && resolve_piano_menu_widget_binding(list, widget, identity)) {
            void* const classified_widget = widget;
            const UObjectLiveHandle classified_identity = identity;
            const PianoListOwnerState owner_state
                = piano_list_catalog_owner_state(widget, identity);
            if (owner_state == PianoListOwnerState::Uncertain) return;
            const MenuListOwnership classified_ownership
                = menu_list_ownership_from_open_facts(
                    owner_state == PianoListOwnerState::Managed,
                    owner_state == PianoListOwnerState::Transient);
            opening = menu_session_authority().begin_open(controller,
                list, widget, identity, word,
                menu_open_ownership_for_final_binding(
                    classified_widget, classified_identity,
                    widget, identity, classified_ownership));
        }
        if (opening) profile_list_coordinator().begin_session(opening.generation);
    } catch (...) {}
    MenuOpenFocusContext focus{};
    focus.opening = opening;
    try {
        MenuOpenFocusScope scope(&focus);
        original(list, selected);
    } catch (...) {
        if (focus.result != MenuFocusRestoreResult::NotApplied) {
            focus.result = MenuFocusRestoreResult::Failed;
            (void)finish_last_played_menu_focus(focus);
        }
        if (opening) { profile_list_coordinator().retire_session(opening.generation); menu_session_authority().retire(opening.generation); }
        return;
    }
    if (!opening) return;
    bool ready = false;
    try {
        uint16_t word = 0; void* widget = nullptr; UObjectLiveHandle identity{};
        ready = finish_last_played_menu_focus(focus)
            && read_active_word(list, word) && resolve_piano_menu_widget_binding(list, widget, identity)
            && menu_session_authority().publish_ready(opening, word, widget, identity);
    } catch (...) {}
    if (focus.consumed) {
        try {
            std::ostringstream out;
            out << "[menu_session] focus_interception generation=" << opening.generation
                << " status=" << (focus.result == MenuFocusRestoreResult::Applied ? "applied"
                    : focus.result == MenuFocusRestoreResult::NotApplied ? "native" : "failed")
                << " original_calls=" << (focus.original_entered ? 1 : 0)
                << " ready=" << ready;
            core::log(ready ? core::LogLevel::Info : core::LogLevel::Error, out.str());
        } catch (...) {}
    }
    if (!ready) {
        profile_list_coordinator().retire_session(opening.generation);
        (void)menu_session_authority().retire(opening.generation);
        return;
    }
    try {
        auto callbacks = make_profile_list_callbacks();
        (void)profile_list_coordinator().notify_session_ready(
            opening.generation, callbacks);
    } catch (...) {}
}

void __fastcall close_detour(void* list) noexcept {
    auto original = g_original_close;
    if (!original) return;
    auto lease = non_audio_hook_gate().try_enter();
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (!lease || !g_module || ret != reinterpret_cast<uintptr_t>(g_module) + kListCloseReturnRva) {
        try { original(list); } catch (...) {}
        return;
    }
    uint16_t word_before = 0;
    const bool before_read = read_active_word(list, word_before);
    const MenuSessionSnapshot closing = menu_session_authority().begin_close(list);
    try { original(list); } catch (...) {
        SelectionActivationTerminalResult result;
        if (closing) {
            result = terminate_terminal_audio(
                closing, SelectionActivationTerminalReason::NativeCloseFailure);
            profile_list_coordinator().retire_session(closing.generation);
            menu_session_authority().retire(closing.generation);
        }
        log_list_close("cancel_unowned", before_read, word_before,
            false, 0, result);
        return;
    }
    SelectionActivationTerminalResult result;
    uint16_t word = 0;
    const bool after_read = read_active_word(list, word);
    if (!closing) {
        log_list_close(selection_activation_list_close_classification(
                after_read && word == 0x0100, false, result.decision),
            before_read, word_before, after_read, word, result);
        return;
    }
    if (!after_read) {
        result = terminate_terminal_audio(
            closing, SelectionActivationTerminalReason::NativeCloseFailure);
        profile_list_coordinator().retire_session(closing.generation);
        (void)menu_session_authority().retire(closing.generation);
        log_list_close("cancel_unowned", before_read, word_before,
            false, 0, result);
        return;
    }
    void* post_close_widget = nullptr;
    UObjectLiveHandle post_close_identity{};
    if (word == 0x0100) {
        if (closing.list_ownership != MenuListOwnership::Unmanaged) {
            (void)resolve_piano_menu_widget_binding(
                list, post_close_widget, post_close_identity);
        }
        result = terminate_terminal_audio(
            closing, SelectionActivationTerminalReason::ListClose);
        profile_list_coordinator().retire_session(closing.generation);
    }
    const bool exact_session_finished
        = menu_session_authority().finish_close(closing, word);
    if (should_restore_list_after_cancel_close(
            true, exact_session_finished, true, word,
            closing.list_ownership)) {
        (void)restore_owned_list_after_exact_cancel_close(
            closing, post_close_widget, post_close_identity);
    }
    log_list_close(selection_activation_list_close_classification(
            word == 0x0100, true, result.decision),
        before_read, word_before, true, word, result);
}

void terminal_detour(void* controller, ControllerFn original,
    SelectionActivationTerminalReason reason) noexcept {
    if (!original) return;
    auto lease = non_audio_hook_gate().try_enter();
    if (!lease) { try { original(controller); } catch (...) {} return; }
    const MenuSessionSnapshot ending = menu_session_authority().begin_ending(controller);
    if (ending) {
        profile_list_coordinator().retire_session(ending.generation);
        (void)terminate_terminal_audio(ending, reason);
    } else if (reason == SelectionActivationTerminalReason::ControllerDestructor) {
        (void)reclaim_selection_audio_activation_for_controller(
            controller, SelectionActivationTerminalReason::ControllerDestructor);
    }
    try { original(controller); } catch (...) {}
    if (ending) (void)menu_session_authority().retire(ending.generation);
}

void __fastcall exit_detour(void* controller) noexcept { terminal_detour(controller, g_original_exit, SelectionActivationTerminalReason::State5Exit); }
void __fastcall destructor_detour(void* controller) noexcept { terminal_detour(controller, g_original_destructor, SelectionActivationTerminalReason::ControllerDestructor); }

} // namespace

bool resolve_piano_menu_widget_binding(void* embedded_list, void*& widget,
    UObjectLiveHandle& identity) noexcept
{
    return resolve_list_identity(embedded_list, widget, identity);
}

bool install_menu_session_hooks(const HookInstallContext& context) {
    g_module = context.exe_module;
    if (!signature_matches(context.exe_module, "piano_menu_list_open_call")
        || !signature_matches(context.exe_module, "piano_menu_list_cancel_close_call")
        || !signature_matches(context.exe_module, "piano_list_restore_selection_caller")) return false;
    std::array<MandatoryHookOperation, 5> operations{{
        {[&] { return install_one(context, "piano_list_restore_selection", reinterpret_cast<void*>(&restore_selection_detour), reinterpret_cast<void**>(&g_restore_selection), g_restore_hook); }, [&] { return g_restore_hook.disable(); }, [&] { return g_restore_hook.remove(); }},
        {[&] { return install_one(context, "piano_menu_list_open", reinterpret_cast<void*>(&open_detour), reinterpret_cast<void**>(&g_original_open), g_open_hook); }, [&] { return g_open_hook.disable(); }, [&] { return g_open_hook.remove(); }},
        {[&] { return install_one(context, "piano_menu_list_cancel_close", reinterpret_cast<void*>(&close_detour), reinterpret_cast<void**>(&g_original_close), g_close_hook); }, [&] { return g_close_hook.disable(); }, [&] { return g_close_hook.remove(); }},
        {[&] { return install_one(context, "piano_menu_state5_exit", reinterpret_cast<void*>(&exit_detour), reinterpret_cast<void**>(&g_original_exit), g_exit_hook); }, [&] { return g_exit_hook.disable(); }, [&] { return g_exit_hook.remove(); }},
        {[&] { return install_one(context, "piano_menu_controller_destructor", reinterpret_cast<void*>(&destructor_detour), reinterpret_cast<void**>(&g_original_destructor), g_destructor_hook); }, [&] { return g_destructor_hook.disable(); }, [&] { return g_destructor_hook.remove(); }} }};
    const auto transaction = install_mandatory_hook_transaction(operations);
    if (transaction == MandatoryHookTransactionResult::Installed) return true;
    if (transaction == MandatoryHookTransactionResult::RolledBack) {
        g_restore_selection = nullptr;
        g_original_destructor = nullptr; g_original_exit = nullptr;
        g_original_close = nullptr; g_original_open = nullptr; g_module = nullptr;
    } else {
        core::log(core::LogLevel::Error,
            "[menu_session] rollback_failed ownership=retained");
    }
    return false;
}

core::HookShutdownResult shutdown_menu_session() {
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_destructor_hook), core::teardown_operation(g_exit_hook),
        core::teardown_operation(g_close_hook), core::teardown_operation(g_open_hook),
        core::teardown_operation(g_restore_hook),
    }, [] { return true; }, [] {
        menu_session_authority().shutdown(); g_restore_selection = nullptr;
        g_original_destructor = nullptr; g_original_exit = nullptr;
        g_original_close = nullptr; g_original_open = nullptr; g_module = nullptr;
    });
}

} // namespace ff7r::piano::game
