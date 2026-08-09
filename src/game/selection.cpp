#include "game/module_hooks.h"
#include "game/menu_ui_refresh.h"

#include "core/hooks.h"
#include "core/logging.h"
#include "core/pe_image.h"
#include "game/audio_sead.h"
#include "game/hook_specs.h"
#include "game/runtime_context_policy.h"
#include "game/runtime_layouts.h"
#include "game/rvas.h"
#include "game/song_registry.h"
#include "game/uobject_lifetime.h"
#include "game/ue_types.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <intrin.h>
#include <mutex>
#include <sstream>

namespace ff7r::piano::game {
namespace {

using OnMenuSelectedIndexChangedBodyFn = void(__fastcall*)(void* context, int32_t selected_index);
using SelectIndexHelperFn = uintptr_t(__fastcall*)(void* wrapper, int32_t index, uint8_t flag);
using SelectedEntryUseFn = uintptr_t(__fastcall*)(void* context, void* wrapper, void* value);
using SelectedEntryGetterFn = uintptr_t(__fastcall*)(void* context, void* out_entry);
using ActionStateQueryFn = bool(__fastcall*)(void* action_ref);

using PianoListEntry = runtime_layouts::PianoListEntry;

struct PianoListArrayView {
    PianoListEntry* data = nullptr;
    int32_t count = 0;
};

struct SelectionAliasState {
    void* context = nullptr;
    UObjectLiveHandle context_identity{};
    int32_t active_visible_index = -1;
    int32_t active_base_slot = -1;
};

struct SelectionMapInsertPlan {
    FNameValue inserted_name{};
    FNameValue cloned_base_name{};
    int32_t visible_index = -1;
    int32_t base_slot = -1;
};

SelectIndexHelperFn g_original_select_index_helper = nullptr;
SelectedEntryUseFn g_original_selected_entry_use = nullptr;
SelectedEntryGetterFn g_original_selected_entry_getter = nullptr;
ActionStateQueryFn g_original_action_state_query = nullptr;
std::mutex g_selection_state_mutex;
SelectionAliasState g_selection_state;
core::RawRvaHook g_on_menu_selected_index_changed_body_hook;
core::RawRvaHook g_select_index_helper_hook;
core::RawRvaHook g_selected_entry_use_hook;
core::RawRvaHook g_selected_entry_getter_hook;
core::RawRvaHook g_action_state_query_hook;
OnMenuSelectedIndexChangedBodyFn g_original_on_menu_selected_index_changed_body = nullptr;
uintptr_t g_selection_exe_base = 0;
std::mutex g_activation_denial_mutex;
SelectionSnapshot g_activation_denied_selection;
bool g_activation_denied = false;
uint64_t g_activation_denied_epoch = 0;
std::atomic_int g_activation_admission_logs{0};

bool selection_snapshot_matches(
    const SelectionSnapshot& left, const SelectionSnapshot& right) noexcept
{
    return left && right && left.generation == right.generation
        && left.storage == right.storage && left.song == right.song
        && left.profile == right.profile
        && left.profile_index == right.profile_index
        && left.visible_index == right.visible_index
        && left.base_slot == right.base_slot;
}

void clear_activation_denial() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_activation_denial_mutex);
        g_activation_denied = false;
        g_activation_denied_selection = {};
        g_activation_denied_epoch = 0;
    } catch (...) {
    }
}

bool activation_denied_for(const SelectionSnapshot& selection) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_activation_denial_mutex);
        if (!g_activation_denied) return false;
        if (g_activation_denied_epoch
                == selection_audio_activation_revocation_epoch()
            && selection_snapshot_matches(selection, g_activation_denied_selection)) {
            return true;
        }
        g_activation_denied = false;
        g_activation_denied_selection = {};
        g_activation_denied_epoch = 0;
    } catch (...) {
        return true;
    }
    revoke_selection_audio_activation();
    return false;
}

void latch_activation_denial(const SelectionSnapshot& selection) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_activation_denial_mutex);
        g_activation_denied_selection = selection;
        g_activation_denied_epoch = selection_audio_activation_revocation_epoch();
        g_activation_denied = true;
    } catch (...) {
    }
}

const char* handoff_result_name(
    const SelectionActivationHandoffResult result) noexcept
{
    switch (result) {
    case SelectionActivationHandoffResult::Prepared: return "prepared";
    case SelectionActivationHandoffResult::Confirmed: return "confirmed";
    case SelectionActivationHandoffResult::NoPendingReservation: return "no_pending";
    case SelectionActivationHandoffResult::AlreadyConfirmed: return "duplicate_getter";
    case SelectionActivationHandoffResult::ReservationGenerationDrift: return "reservation_generation";
    case SelectionActivationHandoffResult::ContextDrift: return "context_drift";
    case SelectionActivationHandoffResult::PriorSelectionDrift: return "prior_selection_drift";
    case SelectionActivationHandoffResult::GenerationWrap: return "generation_wrap";
    case SelectionActivationHandoffResult::SameGeneration: return "same_generation";
    case SelectionActivationHandoffResult::SkippedGeneration: return "skipped_generation";
    case SelectionActivationHandoffResult::StorageDrift: return "storage_drift";
    case SelectionActivationHandoffResult::SongDrift: return "song_drift";
    case SelectionActivationHandoffResult::ProfileDrift: return "profile_drift";
    case SelectionActivationHandoffResult::VisibleIndexDrift: return "visible_index_drift";
    case SelectionActivationHandoffResult::BaseSlotDrift: return "base_slot_drift";
    }
    return "unknown";
}

const char* notification_result_name(
    const SelectionActivationNotificationResult result) noexcept
{
    switch (result) {
    case SelectionActivationNotificationResult::ConfirmedSameSelection:
        return "confirmed_same_selection_notification";
    case SelectionActivationNotificationResult::NoPendingReservation:
        return "no_pending_reservation";
    case SelectionActivationNotificationResult::DuplicateNotification:
        return "duplicate_notification";
    case SelectionActivationNotificationResult::ContextDrift:
        return "context_drift";
    case SelectionActivationNotificationResult::SelectionDrift:
        return "selection_drift";
    }
    return "selection_drift";
}

void log_activation_handoff(
    const bool confirmed, const char* reason,
    const uint64_t old_generation, const uint64_t new_generation) noexcept
{
    try {
        if (g_activation_admission_logs.fetch_add(
                1, std::memory_order_relaxed) >= 64) {
            return;
        }
        std::ostringstream out;
        out << "[selection] activation_admission status="
            << (confirmed ? "selection_handoff_confirmed"
                          : "selection_handoff_rejected")
            << " old_generation=" << old_generation
            << " new_generation=" << new_generation
            << " reason=" << (reason ? reason : "unknown");
        core::log(confirmed ? core::LogLevel::Info : core::LogLevel::Error,
            out.str());
    } catch (...) {
    }
}

void log_activation_notification(
    const SelectionActivationNotificationResult result,
    const uint64_t old_generation, const uint64_t new_generation) noexcept
{
    try {
        if (g_activation_admission_logs.fetch_add(
                1, std::memory_order_relaxed) >= 64) {
            return;
        }
        const bool confirmed = result
            == SelectionActivationNotificationResult::ConfirmedSameSelection;
        std::ostringstream out;
        out << "[selection] activation_admission status="
            << (confirmed ? "confirmed_same_selection_notification"
                          : "selection_notification_revoked")
            << " old_generation=" << old_generation
            << " new_generation=" << new_generation
            << " reason=" << notification_result_name(result);
        core::log(confirmed ? core::LogLevel::Info : core::LogLevel::Error,
            out.str());
    } catch (...) {
    }
}

SelectionActivationListContext activation_list_context(
    void* context, const UObjectLiveHandle& identity) noexcept
{
    return {context, identity.internal_index, identity.serial_number};
}

bool snapshot_activation_list_context(
    const SelectionSnapshot& selection,
    SelectionActivationListContext& out) noexcept
{
    out = {};
    try {
        std::lock_guard<std::mutex> lock(g_selection_state_mutex);
        if (!selection || !g_selection_state.context
            || g_selection_state.active_visible_index != selection.visible_index
            || g_selection_state.active_base_slot != selection.base_slot) {
            return false;
        }
        out = activation_list_context(
            g_selection_state.context, g_selection_state.context_identity);
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}

bool selection_alias_exact(
    void* context, const UObjectLiveHandle& identity,
    const int32_t visible_index, const int32_t base_slot) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_selection_state_mutex);
        return g_selection_state.context == context
            && g_selection_state.context_identity.internal_index
                == identity.internal_index
            && g_selection_state.context_identity.serial_number
                == identity.serial_number
            && g_selection_state.active_visible_index == visible_index
            && g_selection_state.active_base_slot == base_slot;
    } catch (...) {
        return false;
    }
}

SelectionSnapshot descriptor_for_visible_index(int32_t visible_index)
{
    return registry().snapshot_for_visible_index(visible_index);
}

bool read_selected_index(void* context, int32_t& out)
{
    return core::safe_read_field(context, runtime_layouts::PianoMusicList::selected_index, out);
}

bool read_music_list_array(void* context, PianoListArrayView& out)
{
    PianoListEntry* data = nullptr;
    int32_t count = 0;
    if (!core::safe_read_field(context, runtime_layouts::PianoMusicList::entries, data)
        || !core::safe_read_field(context, runtime_layouts::PianoMusicList::count, count)) {
        return false;
    }
    if (!data || count <= 0 || count > 128) {
        return false;
    }
    out = PianoListArrayView{data, count};
    return true;
}

bool read_list_entry(const PianoListArrayView& list, int32_t index, PianoListEntry& out)
{
    if (index < 0 || index >= list.count) {
        return false;
    }
    return core::safe_read_field(list.data, static_cast<uintptr_t>(sizeof(PianoListEntry) * index), out);
}

int32_t base_slot_for_descriptor(const SongDescriptor& descriptor, int32_t list_count)
{
    if (list_count <= 0) {
        return -1;
    }
    return std::clamp(descriptor.base_slot, 0, list_count - 1);
}

int32_t fallback_base_slot_for_descriptor(const SongDescriptor& descriptor)
{
    return std::clamp(descriptor.base_slot, 0, runtime_layouts::PianoMusicList::vanilla_count - 1);
}

void set_active_selection(void* context, int32_t visible_index, int32_t base_slot)
{
    UObjectLiveHandle identity{};
    if (!capture_live_uobject_handle(context, identity)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_selection_state_mutex);
    if (g_selection_state.context == context
        && g_selection_state.context_identity.internal_index
            == identity.internal_index
        && g_selection_state.context_identity.serial_number
            == identity.serial_number
        && g_selection_state.active_visible_index == visible_index
        && g_selection_state.active_base_slot == base_slot) {
        return;
    }
    clear_activation_denial();
    revoke_selection_audio_activation();
    g_selection_state = SelectionAliasState{context, identity, visible_index, base_slot};
    registry().set_active_selection(visible_index, base_slot);
}

void clear_active_selection()
{
    std::lock_guard<std::mutex> lock(g_selection_state_mutex);
    clear_activation_denial();
    revoke_selection_audio_activation();
    g_selection_state = SelectionAliasState{};
    registry().clear_active_selection();
}

class ScopedSelectedIndexAlias {
public:
    ScopedSelectedIndexAlias(void* context, int32_t alias_index)
        : context_(context)
        , alias_index_(alias_index)
    {
    }

    ScopedSelectedIndexAlias(const ScopedSelectedIndexAlias&) = delete;
    ScopedSelectedIndexAlias& operator=(const ScopedSelectedIndexAlias&) = delete;

    ~ScopedSelectedIndexAlias()
    {
        restore();
    }

    bool apply()
    {
        if (alias_index_ < 0 || !read_selected_index(context_, original_index_)) {
            return false;
        }
        active_ = core::safe_write_field(context_, runtime_layouts::PianoMusicList::selected_index, alias_index_);
        return active_;
    }

    void restore()
    {
        if (!active_) {
            return;
        }
        (void)core::safe_write_field(context_, runtime_layouts::PianoMusicList::selected_index, original_index_);
        active_ = false;
    }

private:
    void* context_ = nullptr;
    int32_t alias_index_ = -1;
    int32_t original_index_ = -1;
    bool active_ = false;
};

bool build_selection_map_insert_plan(void* context, int32_t visible_index, const SongDescriptor& descriptor, SelectionMapInsertPlan& out)
{
    PianoListArrayView list{};
    if (!read_music_list_array(context, list)) {
        return false;
    }

    const int32_t base_slot = base_slot_for_descriptor(descriptor, list.count);
    PianoListEntry visible_entry{};
    PianoListEntry base_entry{};
    if (!read_list_entry(list, visible_index, visible_entry) || !read_list_entry(list, base_slot, base_entry)) {
        return false;
    }

    out = SelectionMapInsertPlan{visible_entry.row_name, base_entry.row_name, visible_index, base_slot};
    return true;
}

bool try_insert_selection_map_entry(void* context, int32_t visible_index, const SongDescriptor& descriptor)
{
    SelectionMapInsertPlan plan{};
    uint8_t inline_map_probe = 0;
    if (!build_selection_map_insert_plan(context, visible_index, descriptor, plan)
        || !core::safe_read_field(context, runtime_layouts::PianoMusicList::selection_map, inline_map_probe)) {
        return false;
    }

    (void)plan;
    (void)inline_map_probe;
    return false;
}

uintptr_t __fastcall select_index_helper_detour(void* wrapper, int32_t index, uint8_t flag)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!g_original_select_index_helper) {
        return 0;
    }
    if (!callback) {
        return g_original_select_index_helper(wrapper, index, flag);
    }

    const SelectionSnapshot descriptor = descriptor_for_visible_index(index);
    if (!descriptor) {
        return g_original_select_index_helper(wrapper, index, flag);
    }

    return g_original_select_index_helper(wrapper, index, flag);
}

uintptr_t __fastcall selected_entry_use_detour(void* context, void* wrapper, void* value)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!g_original_selected_entry_use) {
        return 0;
    }
    if (!callback) {
        return g_original_selected_entry_use(context, wrapper, value);
    }

    int32_t selected_index = -1;
    if (read_selected_index(context, selected_index)) {
        if (const SelectionSnapshot descriptor = descriptor_for_visible_index(selected_index)) {
            (void)try_insert_selection_map_entry(context, selected_index, *descriptor.song);
        }
    }

    return g_original_selected_entry_use(context, wrapper, value);
}

uintptr_t __fastcall selected_entry_getter_detour(void* context, void* out_entry)
{
    const uintptr_t return_address
        = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto callback = non_audio_hook_gate().try_enter();
    if (!g_original_selected_entry_getter) {
        return 0;
    }
    if (!callback) {
        return g_original_selected_entry_getter(context, out_entry);
    }

    int32_t selected_index = -1;
    if (!read_selected_index(context, selected_index)) {
        const SelectionSnapshot prior_selection = registry().selection_snapshot();
        if (revoke_selection_audio_activation_if_pending()) {
            log_activation_handoff(false, "selected_index_unreadable",
                prior_selection.generation, prior_selection.generation);
        }
        clear_active_selection();
        return g_original_selected_entry_getter(context, out_entry);
    }

    const SelectionSnapshot descriptor = descriptor_for_visible_index(selected_index);
    if (!descriptor) {
        const SelectionSnapshot prior_selection = registry().selection_snapshot();
        if (revoke_selection_audio_activation_if_pending()) {
            log_activation_handoff(false, "descriptor_drift",
                prior_selection.generation, prior_selection.generation);
        }
        clear_active_selection();
        return g_original_selected_entry_getter(context, out_entry);
    }

    PianoListArrayView list{};
    const int32_t base_slot = read_music_list_array(context, list) ? base_slot_for_descriptor(*descriptor.song, list.count) : fallback_base_slot_for_descriptor(*descriptor.song);
    UObjectLiveHandle context_identity{};
    const bool context_identity_exact
        = capture_live_uobject_handle(context, context_identity);
    const uintptr_t activation_getter_return = g_selection_exe_base
        && rva::SelectedEntryGetterActivationReturn
        ? g_selection_exe_base + rva::SelectedEntryGetterActivationReturn
        : 0;
    const bool activation_getter = selection_activation_getter_return_exact(
        return_address, activation_getter_return);
    const SelectionSnapshot prior_selection = registry().selection_snapshot();
    const SelectionActivationListContext list_context
        = context_identity_exact
        ? activation_list_context(context, context_identity)
        : SelectionActivationListContext{};
    SelectionActivationHandoff handoff;
    if (activation_getter && list_context) {
        handoff = prepare_selection_audio_activation_handoff(
            prior_selection, list_context);
    } else if (activation_getter) {
        handoff.result = SelectionActivationHandoffResult::ContextDrift;
    }
    bool log_rejected_handoff = false;
    const char* rejected_handoff_reason = nullptr;
    if (!activation_getter || !handoff) {
        const bool rejected_pending
            = revoke_selection_audio_activation_if_pending();
        log_rejected_handoff = rejected_pending || activation_getter;
        rejected_handoff_reason = activation_getter
            ? handoff_result_name(handoff.result) : "unrelated_getter";
    }

    bool handoff_published = false;
    uint64_t published_handoff_generation = 0;
    SelectionActivationHandoffResult handoff_result
        = SelectionActivationHandoffResult::NoPendingReservation;
    if (handoff) {
        const bool alias_exact = selection_alias_exact(
            context, context_identity, selected_index, base_slot);
        SelectionSnapshot published_selection;
        handoff_result = publish_selection_audio_activation_handoff(
            handoff, list_context, selected_index, base_slot, alias_exact,
            published_selection);
        handoff_published
            = handoff_result == SelectionActivationHandoffResult::Confirmed;
        published_handoff_generation = published_selection.generation;
        if (!handoff_published) {
            log_activation_handoff(false, handoff_result_name(handoff_result),
                handoff.prior_selection_generation,
                published_handoff_generation);
        }
    } else {
        set_active_selection(context, selected_index, base_slot);
    }
    if (log_rejected_handoff) {
        const SelectionSnapshot rejected_selection
            = registry().selection_snapshot();
        log_activation_handoff(false, rejected_handoff_reason,
            prior_selection.generation,
            rejected_selection.generation);
        if (rejected_selection) latch_activation_denial(rejected_selection);
    }

    ScopedSelectedIndexAlias alias(context, base_slot);
    if (!alias.apply()) {
        if (handoff && handoff_published) {
            revoke_selection_audio_activation();
            const SelectionSnapshot rejected_selection
                = registry().selection_snapshot();
            log_activation_handoff(false, "alias_apply_failed",
                handoff.prior_selection_generation,
                rejected_selection.generation);
            if (rejected_selection) latch_activation_denial(rejected_selection);
        }
        return g_original_selected_entry_getter(context, out_entry);
    }
    if (handoff && handoff_published) {
        const SelectionSnapshot published_selection
            = registry().selection_snapshot();
        log_activation_handoff(
            true, handoff_result_name(handoff_result),
            handoff.prior_selection_generation,
            published_selection.generation);
    }
    return g_original_selected_entry_getter(context, out_entry);
}

bool __fastcall action_state_query_detour(void* action_ref)
{
    const uintptr_t return_address
        = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto callback = non_audio_hook_gate().try_enter();
    if (!g_original_action_state_query) return false;
    const uintptr_t expected_return = g_selection_exe_base
        && rva::ActionStateQueryPrimaryReturn
        ? g_selection_exe_base + rva::ActionStateQueryPrimaryReturn
        : 0;
    const bool primary_return = selection_activation_primary_return_exact(
        return_address, expected_return);
    return selection_activation_query_exact_once(
        [&]() { return g_original_action_state_query(action_ref); },
        static_cast<bool>(callback), primary_return,
        [&]() noexcept {
            if (primary_return) {
                clear_activation_denial();
                revoke_selection_audio_activation();
            }
        },
        [&]() {
            const SelectionSnapshot selection = registry().selection_snapshot();
            if (!selection || !selection.song) return true;
            if (activation_denied_for(selection)) return false;

            SelectionActivationListContext list_context;
            const bool reserved
                = snapshot_activation_list_context(selection, list_context)
                && reserve_selection_audio_activation(selection, list_context);
            const bool result = selection_activation_query_result({
                true, true, true, false, reserved,
            });
            if (!result) {
                revoke_selection_audio_activation();
                latch_activation_denial(selection);
            }
            if (g_activation_admission_logs.fetch_add(
                    1, std::memory_order_relaxed) < 64) {
                std::ostringstream out;
                out << "[selection] activation_admission status="
                    << (result ? "reserved" : "denied")
                    << " song_id=" << selection.song->id
                    << " selection_generation=" << selection.generation
                    << " return_rva=0x" << std::hex
                    << rva::ActionStateQueryPrimaryReturn << std::dec
                    << " native_result=1 original_calls=1";
                core::log(result ? core::LogLevel::Info : core::LogLevel::Error,
                    out.str());
            }
            return result;
        });
}

void __fastcall on_menu_selected_index_changed_body_detour(void* context, int32_t selected_index)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!g_original_on_menu_selected_index_changed_body) {
        return;
    }
    if (!callback) {
        g_original_on_menu_selected_index_changed_body(context, selected_index);
        return;
    }

    const SelectionSnapshot descriptor = descriptor_for_visible_index(selected_index);
    if (!descriptor) {
        clear_active_selection();
        g_original_on_menu_selected_index_changed_body(context, selected_index);
        return;
    }

    PianoListArrayView list{};
    const int32_t base_slot = read_music_list_array(context, list) ? base_slot_for_descriptor(*descriptor.song, list.count) : fallback_base_slot_for_descriptor(*descriptor.song);
    const SelectionSnapshot prior_selection = registry().selection_snapshot();
    UObjectLiveHandle context_identity{};
    const SelectionActivationListContext list_context
        = capture_live_uobject_handle(context, context_identity)
        ? activation_list_context(context, context_identity)
        : SelectionActivationListContext{};
    const auto notification
        = observe_selection_audio_activation_index_notification(
            prior_selection, list_context, selected_index, base_slot);
    if (notification
        == SelectionActivationNotificationResult::ConfirmedSameSelection) {
        log_activation_notification(notification,
            prior_selection.generation, prior_selection.generation);
    } else {
        const bool had_pending = notification
            != SelectionActivationNotificationResult::NoPendingReservation;
        set_active_selection(context, selected_index, base_slot);
        const SelectionSnapshot published_selection
            = registry().selection_snapshot();
        if (had_pending) {
            log_activation_notification(notification,
                prior_selection.generation, published_selection.generation);
            if (published_selection) latch_activation_denial(published_selection);
        }
    }

    const SelectionSnapshot published = registry().selection_snapshot();
    SelectionUiRefreshTarget render_target{};
    if (!published || published.song != descriptor.song
        || published.profile == nullptr
        || !capture_active_selection_ui_target(published, render_target)
        || render_target.context != context
        || render_target.visible_index != selected_index) {
        g_original_on_menu_selected_index_changed_body(context, selected_index);
        return;
    }

    ScopedSongRenderContext menu_metadata(published);
    g_original_on_menu_selected_index_changed_body(context, selected_index);
}

bool install_raw_hook(const HookInstallContext& context, const char* spec_name, void* detour, void** original, core::RawRvaHook& hook)
{
    const HookSpec* spec = find_hook_spec(spec_name);
    if (!spec) {
        std::ostringstream out;
        out << "[selection] status=hook_spec_missing name=" << spec_name;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    std::string error;
    if (!hook.install(context.exe_module, spec->rva, spec->expected_prologue, detour, original, error)) {
        std::ostringstream out;
        out << "[selection] status=hook_install_failed name=" << spec_name << " error=" << error;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    std::ostringstream out;
    out << "[selection] status=hook_installed name=" << spec_name << " rva=0x" << std::hex << spec->rva;
    core::log(core::LogLevel::Info, out.str());
    return true;
}

} // namespace

bool try_selection_runtime_idle_for_catalog_adoption() noexcept
{
    std::unique_lock selection_lock(g_selection_state_mutex, std::defer_lock);
    std::unique_lock denial_lock(g_activation_denial_mutex, std::defer_lock);
    if (std::try_lock(selection_lock, denial_lock) != -1) return false;
    return g_selection_state.context == nullptr
        && g_selection_state.active_visible_index < 0
        && !g_activation_denied_selection;
}

bool capture_active_selection_ui_target(
    const SelectionSnapshot& expected, SelectionUiRefreshTarget& out) noexcept
{
    out = {};
    if (!registry().selection_matches(expected)) return false;
    {
        std::lock_guard<std::mutex> lock(g_selection_state_mutex);
        out.context = g_selection_state.context;
        out.identity = g_selection_state.context_identity;
        out.visible_index = g_selection_state.active_visible_index;
    }
    return out.context && out.visible_index == expected.visible_index
        && validate_live_uobject_handle(out.context, out.identity);
}

bool validate_active_selection_ui_target(const SelectionSnapshot& expected,
    const SelectionUiRefreshTarget& target) noexcept
{
    SelectionUiRefreshTarget current;
    return capture_active_selection_ui_target(expected, current)
        && current.context == target.context
        && current.visible_index == target.visible_index
        && current.identity.internal_index == target.identity.internal_index
        && current.identity.serial_number == target.identity.serial_number;
}

bool refresh_active_selection_ui(const SelectionSnapshot& expected,
    const SelectionUiRefreshTarget& target)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!callback || !g_original_on_menu_selected_index_changed_body
        || !validate_active_selection_ui_target(expected, target)) {
        return false;
    }
    ScopedSongRenderContext menu_metadata(expected);
    g_original_on_menu_selected_index_changed_body(target.context, target.visible_index);
    return true;
}

bool install_selection_hooks(const HookInstallContext& context)
{
    g_selection_exe_base = reinterpret_cast<uintptr_t>(context.exe_module);
    if (!install_raw_hook(
            context,
            "piano_on_menu_selected_index_changed_body",
            reinterpret_cast<void*>(&on_menu_selected_index_changed_body_detour),
            reinterpret_cast<void**>(&g_original_on_menu_selected_index_changed_body),
            g_on_menu_selected_index_changed_body_hook)) {
        shutdown_selection();
        return false;
    }
    if (!install_raw_hook(
            context,
            "select_index_helper",
            reinterpret_cast<void*>(&select_index_helper_detour),
            reinterpret_cast<void**>(&g_original_select_index_helper),
            g_select_index_helper_hook)) {
        shutdown_selection();
        return false;
    }
    if (!install_raw_hook(
            context,
            "selected_entry_use",
            reinterpret_cast<void*>(&selected_entry_use_detour),
            reinterpret_cast<void**>(&g_original_selected_entry_use),
            g_selected_entry_use_hook)) {
        shutdown_selection();
        return false;
    }
    if (!install_raw_hook(
            context,
            "selected_entry_getter",
            reinterpret_cast<void*>(&selected_entry_getter_detour),
            reinterpret_cast<void**>(&g_original_selected_entry_getter),
            g_selected_entry_getter_hook)) {
        shutdown_selection();
        return false;
    }
    if (!install_raw_hook(
            context,
            "action_state_query",
            reinterpret_cast<void*>(&action_state_query_detour),
            reinterpret_cast<void**>(&g_original_action_state_query),
            g_action_state_query_hook)) {
        shutdown_selection();
        return false;
    }

    return true;
}

core::HookShutdownResult shutdown_selection()
{
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_action_state_query_hook),
        core::teardown_operation(g_selected_entry_getter_hook),
        core::teardown_operation(g_selected_entry_use_hook),
        core::teardown_operation(g_select_index_helper_hook),
        core::teardown_operation(g_on_menu_selected_index_changed_body_hook),
    }, {}, [] {
        clear_activation_denial();
        revoke_selection_audio_activation();
        clear_active_selection();
        g_selection_exe_base = 0;
        g_original_on_menu_selected_index_changed_body = nullptr;
        g_original_select_index_helper = nullptr;
        g_original_selected_entry_use = nullptr;
        g_original_selected_entry_getter = nullptr;
        g_original_action_state_query = nullptr;
    });
}

} // namespace ff7r::piano::game
