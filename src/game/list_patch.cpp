#include "game/module_hooks.h"
#include "game/menu_ui_refresh.h"
#include "game/profile_list_coordinator.h"

#include "core/hooks.h"
#include "core/logging.h"
#include "core/pe_image.h"
#include "game/hook_specs.h"
#include "game/audio_sead.h"
#include "game/native_array_publication.h"
#include "game/menu_focus_restore.h"
#include "game/list_patch_selftest.h"
#include "game/progress.h"
#include "game/rvas.h"
#include "game/runtime_layouts.h"
#include "game/scoreinfo_overlay.h"
#include "game/song_registry.h"
#include "game/title.h"
#include "game/uobject_identity.h"
#include "game/ue_types.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <sstream>
#include <vector>

namespace ff7r::piano::game {
namespace {

using OnSetupItemBodyFn = void(__fastcall*)(void* context, void* item_widget, int32_t index);
using FindChildWidgetFn = void*(__fastcall*)(void* widget, void* name);
using SetTextFn = void(__fastcall*)(void* text_widget, void* text);

using FStringView = runtime_layouts::FString;
using PianoListEntry = runtime_layouts::PianoListEntry;

constexpr uintptr_t kMusicListEntriesOffset = runtime_layouts::PianoMusicList::entries;
constexpr uintptr_t kMusicListEntryCountOffset = runtime_layouts::PianoMusicList::count;
constexpr uintptr_t kMusicListEntryCapacityOffset = runtime_layouts::PianoMusicList::capacity;
constexpr int32_t kVanillaPianoSongCount = runtime_layouts::PianoMusicList::vanilla_count;

struct PianoListArrayView {
    PianoListEntry* data = nullptr;
    int32_t count = 0;
    int32_t capacity = 0;
};

struct OwnedPianoListPatch {
    void* context = nullptr;
    UObjectLiveHandle context_identity{};
    PianoListEntry* original_array = nullptr;
    int32_t original_count = 0;
    int32_t original_capacity = 0;
    RegistrySnapshot catalog{};
    std::vector<PianoListEntry> entries;

    void clear()
    {
        context = nullptr;
        original_array = nullptr;
        original_count = 0;
        original_capacity = 0;
        entries.clear();
    }
};

struct PianoListRepublishCandidate {
    uint64_t catalog_revision = 0;
    std::shared_ptr<const SongRegistryStorage> storage;
    explicit operator bool() const noexcept { return static_cast<bool>(storage); }
};

std::mutex g_owned_list_mutex;
std::list<OwnedPianoListPatch> g_owned_lists;
PianoListRepublishCandidate g_republish_candidate{};
std::atomic_bool g_catalog_terminal_failure{false};
core::RawRvaHook g_on_setup_item_body_hook;
OnSetupItemBodyFn g_original_on_setup_item_body = nullptr;
HMODULE g_exe_module = nullptr;
#ifdef FF7RP_LIST_CATALOG_SELFTEST
ListCatalogIdentityValidator g_test_identity = nullptr;
NativeArrayTupleFaultInjection g_test_fault{};
ListCatalogTrace g_test_trace = nullptr;
#endif

bool validate_list_identity(void* object, const UObjectLiveHandle& identity) noexcept
{
#ifdef FF7RP_LIST_CATALOG_SELFTEST
    if (g_test_identity) return g_test_identity(object, identity);
#endif
    return validate_live_uobject_handle(object, identity);
}


bool apply_custom_rank_text(void* item_widget, const SongDescriptor& song,
    const SongDifficultyProfile* profile, int32_t visible_index)
{
    if (!item_widget || !g_exe_module) {
        return false;
    }

    auto* const exe_base = reinterpret_cast<uint8_t*>(g_exe_module);
    auto* const find_child = reinterpret_cast<FindChildWidgetFn>(exe_base + rva::FindChildWidget);
    auto* const set_text = rva::EndTextBlockSetText
        ? reinterpret_cast<SetTextFn>(exe_base + rva::EndTextBlockSetText)
        : nullptr;
    void* rank_widget = nullptr;
    const char* source = "missing";

    if (find_child) {
        rank_widget = find_child(item_widget, exe_base + rva::RankTextWidgetNameGlobal);
        source = rank_widget ? "find_child" : source;
    }
    if (!rank_widget) {
        void* direct_rank_widget = nullptr;
        if (core::safe_read_field(item_widget, 0x408, direct_rank_widget) && direct_rank_widget) {
            rank_widget = direct_rank_widget;
            source = "item_widget+0x408";
        }
    }

    const std::wstring ini_path = custom_scores_ini_path(GetModuleHandleW(nullptr));
    const ProgressRecord progress = load_progress(song, ini_path, profile);
    const RankText rank = compute_rank_text(song, progress, profile);
    FStringView text{rank.text, rank.num, rank.num};
    const bool applied = rank_widget && set_text;
    if (applied) {
        set_text(rank_widget, &text);
    }

    static std::atomic_int s_rank_logs{0};
    const int log_index = s_rank_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 64) {
        std::ostringstream out;
        out << "[list_patch] rank_text status=" << (applied ? "applied" : (rank_widget ? "set_text_missing" : "missing_rank_widget"))
            << " visible_index=" << visible_index
            << " song_id=" << song.id
            << " rank_kind=" << rank.kind
            << " played=" << (progress.played ? 1 : 0)
            << " high_score=" << progress.high_score
            << " source=" << source;
        core::log(core::LogLevel::Info, out.str());
    }
    return applied;
}

bool read_music_list_array(void* music_list, PianoListArrayView& out)
{
    PianoListEntry* data = nullptr;
    int32_t count = 0;
    int32_t capacity = 0;
    if (!core::safe_read_field(music_list, kMusicListEntriesOffset, data)
        || !core::safe_read_field(music_list, kMusicListEntryCountOffset, count)
        || !core::safe_read_field(music_list, kMusicListEntryCapacityOffset, capacity)) {
        return false;
    }
    if (!data || count <= 0 || capacity < count
        || count > runtime_layouts::PianoMusicList::maximum_count || capacity > 256) {
        return false;
    }
    out = PianoListArrayView{data, count, capacity};
    return true;
}

bool same_list_identity(const UObjectLiveHandle& left,
    const UObjectLiveHandle& right) noexcept
{
    return left.internal_index == right.internal_index
        && left.serial_number == right.serial_number;
}

bool same_catalog_identity(const PianoListRepublishCandidate& left,
    const RegistrySnapshot& right) noexcept
{
    return left.catalog_revision == right.catalog_revision
        && left.storage == right.storage;
}

bool same_catalog_identity(const PianoListRepublishCandidate& left,
    const PianoListRepublishCandidate& right) noexcept
{
    return left.catalog_revision == right.catalog_revision
        && left.storage == right.storage;
}

NativeArrayTuple original_tuple(const OwnedPianoListPatch& owned) noexcept
{
    return {reinterpret_cast<uintptr_t>(owned.original_array),
        owned.original_count, owned.original_capacity};
}

NativeArrayTuple redirected_tuple(const OwnedPianoListPatch& owned) noexcept
{
    return {reinterpret_cast<uintptr_t>(owned.entries.data()),
        static_cast<int32_t>(owned.entries.size()),
        static_cast<int32_t>(owned.entries.capacity())};
}

NativeArrayTupleAccess owned_list_tuple_access(OwnedPianoListPatch& owned)
{
    return {
        [&owned](NativeArrayTuple& tuple) {
            PianoListEntry* pointer = nullptr;
            return validate_list_identity(owned.context, owned.context_identity)
                && core::safe_read_field(owned.context, kMusicListEntriesOffset, pointer)
                && core::safe_read_field(owned.context, kMusicListEntryCountOffset, tuple.count)
                && core::safe_read_field(owned.context, kMusicListEntryCapacityOffset, tuple.capacity)
                && (tuple.pointer = reinterpret_cast<uintptr_t>(pointer), true);
        },
        [&owned](uintptr_t value) {
            return core::safe_write_field(owned.context, kMusicListEntriesOffset,
                reinterpret_cast<PianoListEntry*>(value));
        },
        [&owned](int32_t value) {
            return core::safe_write_field(owned.context, kMusicListEntryCountOffset, value);
        },
        [&owned](int32_t value) {
            return core::safe_write_field(owned.context, kMusicListEntryCapacityOffset, value);
        },
#ifdef FF7RP_LIST_CATALOG_SELFTEST
        [&owned](const NativeArrayTuple& value) {
            if (!g_test_trace) return;
            const NativeArrayTuple original = original_tuple(owned);
            const NativeArrayTuple redirected = redirected_tuple(owned);
            if (value.pointer == redirected.pointer
                && value.count == original.count
                && value.capacity == redirected.capacity) g_test_trace(2);
            else if (value.pointer == redirected.pointer
                && value.count == original.count
                && value.capacity == original.capacity) g_test_trace(3);
            else if (value == original) g_test_trace(4);
        },
#else
        {},
#endif
    };
}

NativeArrayTupleTransitionResult restore_owned_list_tuple(
    OwnedPianoListPatch& owned)
{
    const NativeArrayTupleAccess access = owned_list_tuple_access(owned);
    return restore_native_array_tuple(original_tuple(owned),
        owned.original_capacity, redirected_tuple(owned),
        static_cast<int32_t>(owned.entries.capacity()), access
#ifdef FF7RP_LIST_CATALOG_SELFTEST
        , g_test_fault
#endif
    );
}

const char* exact_close_restore_failure_name(
    const ExactCloseListRestoreResult result) noexcept
{
    switch (result) {
    case ExactCloseListRestoreResult::Restored: return "none";
    case ExactCloseListRestoreResult::VerifiedOwnerless: return "none";
    case ExactCloseListRestoreResult::NoOwnedPatch: return "owner_missing";
    case ExactCloseListRestoreResult::SessionIdentityMismatch: return "session_identity";
    case ExactCloseListRestoreResult::LiveIdentityInvalid: return "live_identity";
    case ExactCloseListRestoreResult::CatalogIdentityMismatch: return "catalog_identity";
    case ExactCloseListRestoreResult::TupleUnreadable: return "tuple_unreadable";
    case ExactCloseListRestoreResult::TupleDrift: return "tuple_drift";
    case ExactCloseListRestoreResult::TransitionFailed: return "transition_write_or_readback";
    case ExactCloseListRestoreResult::RollbackUnverified: return "rollback_unverified";
    }
    return "unknown";
}

bool list_catalog_matches_registry(void* widget)
{
    const RegistrySnapshot current = registry().registry_snapshot();
    std::lock_guard lock(g_owned_list_mutex);
    const auto owner = std::find_if(g_owned_lists.begin(), g_owned_lists.end(),
        [widget](const OwnedPianoListPatch& candidate) {
            return candidate.context == widget;
        });
    if (owner == g_owned_lists.end()) return current.songs().empty();
    PianoListArrayView native{};
    return owner->catalog.catalog_revision == current.catalog_revision
        && owner->catalog.storage == current.storage
        && validate_list_identity(owner->context, owner->context_identity)
        && read_music_list_array(widget, native)
        && native.data == owner->entries.data()
        && native.count == static_cast<int32_t>(owner->entries.size())
        && native.capacity == static_cast<int32_t>(owner->entries.capacity());
}

// Custom rows are appended to the original native list, so the catalog must
// claim exactly the rows [source.count, source.count + songs). Requiring that outright means
// composition writes every appended slot exactly once: a catalog resolved
// against a different list length is refused instead of composing a list with
// a dropped song or an unpopulated row.
bool appended_music_list_count(const PianoListArrayView& source,
    const RegistrySnapshot& registry_view, int32_t& desired_count)
{
    const SongRegistryStorage& songs = registry_view.songs();
    if (songs.empty()) return false;
    int32_t next_row = source.count;
    for (const SongDescriptor& song : songs) {
        if (song.visible_index != next_row++) return false;
    }
    desired_count = next_row;
    return desired_count > source.count
        && desired_count <= runtime_layouts::PianoMusicList::maximum_count;
}

bool clone_registry_entries(const PianoListArrayView& source,
    const RegistrySnapshot& registry_view, std::vector<PianoListEntry>& out)
{
    int32_t desired_count = 0;
    if (!appended_music_list_count(source, registry_view, desired_count)) return false;

    out.resize(static_cast<size_t>(source.count));
    for (int32_t index = 0; index < source.count; ++index) {
        if (!core::safe_read_field(source.data, static_cast<uintptr_t>(sizeof(PianoListEntry) * index), out[static_cast<size_t>(index)])) {
            return false;
        }
    }
    out.resize(static_cast<size_t>(desired_count));

    for (const SongDescriptor& song : registry_view.songs()) {
        const int32_t base_slot = std::clamp(song.base_slot, 0, source.count - 1);
        if (!core::safe_read_field(source.data, static_cast<uintptr_t>(sizeof(PianoListEntry) * base_slot), out[static_cast<size_t>(song.visible_index)])) {
            return false;
        }
    }
    return true;
}

std::mutex g_dynamic_item_mutex;
struct DynamicItemReference {
    void* context = nullptr;
    void* widget = nullptr;
    UObjectLiveHandle context_identity{};
    UObjectLiveHandle widget_identity{};
    int32_t index = -1;
};
std::vector<DynamicItemReference> g_dynamic_items;

bool prepare_list_setup_view(ListSetupView& view)
{
    auto lease = non_audio_hook_gate().try_enter();
    if (!lease) return false;
    view.callback_authority = std::make_shared<core::HookCallbackGate::Lease>(
        std::move(lease));
    if (!list_catalog_matches_registry(view.ingress_context)) return false;
    int source_count = kVanillaPianoSongCount;
    PianoListArrayView list_view{};
    if (read_music_list_array(view.ingress_context, list_view))
        source_count = std::max(kVanillaPianoSongCount,
            std::min(view.ingress_index, list_view.count - 1));
    SelectionSnapshot setup = registry().snapshot_for_visible_index(view.ingress_index);
    setup.base_slot = setup.song
        ? std::clamp(setup.song->base_slot, 0, source_count)
        : std::clamp(view.ingress_index, 0, source_count);
    UObjectLiveHandle context_identity{}, widget_identity{};
    if (!capture_live_uobject_handle(view.ingress_context, context_identity)
        || !capture_live_uobject_handle(view.ingress_widget, widget_identity))
        return false;
    const MenuSessionSnapshot session = capture_menu_callback_session();
    if (!session || session.widget != view.ingress_context
        || session.widget_identity.internal_index != context_identity.internal_index
        || session.widget_identity.serial_number != context_identity.serial_number)
        return false;

    if (setup.song) {
        SelectionSnapshot initialized;
        const InitializedProfileState initialized_state
            = registry().initialized_profile_state(setup, initialized);
        if (initialized_state == InitializedProfileState::Invalid) return false;
        if (initialized_state == InitializedProfileState::Present
            || initialized_state == InitializedProfileState::Deferred) {
            setup = std::move(initialized);
        } else {
            const int preferred = load_last_played_profile_index(*setup.song);
            const MenuSessionSnapshot revalidated_session
                = capture_menu_callback_session();
            if (!validate_list_identity(view.ingress_context, context_identity)
                || !validate_list_identity(view.ingress_widget, widget_identity)
                || !revalidated_session
                || revalidated_session.generation != session.generation
                || revalidated_session.widget != session.widget
                || revalidated_session.widget_identity.internal_index
                    != session.widget_identity.internal_index
                || revalidated_session.widget_identity.serial_number
                    != session.widget_identity.serial_number) {
                return false;
            }
            if (!registry().initialize_profile_if_absent(
                    setup, preferred, initialized)) return false;
            setup = std::move(initialized);
        }
    }

    {
        std::lock_guard lock(g_dynamic_item_mutex);
        g_dynamic_items.erase(std::remove_if(g_dynamic_items.begin(),
            g_dynamic_items.end(), [=](const DynamicItemReference& item) {
                return item.index == view.ingress_index
                    || item.widget == view.ingress_widget;
            }), g_dynamic_items.end());
        if (setup.song)
            g_dynamic_items.push_back({view.ingress_context, view.ingress_widget,
                context_identity, widget_identity, view.ingress_index});
    }
    const bool active_custom = selection_semantically_matches(
        setup, registry().selection_snapshot());
    view.identity = {view.ingress_context, view.ingress_widget,
        view.ingress_index, setup.base_slot, std::move(setup),
        context_identity, widget_identity, session.generation, active_custom};
    return true;
}

void __fastcall on_setup_item_body_detour(void* context, void* item_widget, int32_t index)
{
    ListReturnCallbacks list;
    list.ingress_context = context;
    list.ingress_widget = item_widget;
    list.ingress_index = index;
    list.retire_scoreinfo = [] { retire_scoreinfo_result_authority_for_list(); };
    list.prepare = [](ListSetupView& view) { return prepare_list_setup_view(view); };
    list.original_setup = [](const ListSetupView& view) {
        ScopedSongRenderContext render(view.identity.setup_selection);
        if (g_original_on_setup_item_body)
            g_original_on_setup_item_body(view.ingress_context, view.ingress_widget,
                view.ingress_index);
    };
    list.cleanup_audio = [](const ListSetupView& view) {
        return release_audio_route_on_piano_list_return(
            view.ingress_index).released();
    };
    list.update_rank = [](const ListSetupView& view) {
        const SelectionSnapshot& setup = view.identity.setup_selection;
        ScopedSongRenderContext render(setup);
        if (setup.song) (void)apply_custom_rank_text(view.identity.widget,
            *setup.song, setup.profile, view.identity.visible_index);
    };
    ProfileListCallbacks profile = make_profile_list_callbacks();
    (void)profile_list_coordinator().run_list_return(list, profile);
}

bool capture_active_list_item_ui_target_impl(const SelectionSnapshot& expected,
    ListItemUiRefreshTarget& out) noexcept
{
    out = {};
    if (!registry().selection_matches(expected) || expected.visible_index < 0) return false;
    {
        std::lock_guard<std::mutex> lock(g_dynamic_item_mutex);
        const auto item = std::find_if(g_dynamic_items.begin(), g_dynamic_items.end(), [&](const DynamicItemReference& candidate) {
            return candidate.index == expected.visible_index;
        });
        if (item != g_dynamic_items.end()) {
            out = {item->context, item->widget, item->context_identity,
                item->widget_identity, item->index};
        }
    }
    return out.context && out.widget
        && validate_list_identity(out.context, out.context_identity)
        && validate_list_identity(out.widget, out.widget_identity);
}

bool validate_active_list_item_ui_target_impl(const SelectionSnapshot& expected,
    const ListItemUiRefreshTarget& target) noexcept
{
    ListItemUiRefreshTarget current;
    return capture_active_list_item_ui_target_impl(expected, current)
        && current.context == target.context && current.widget == target.widget
        && current.visible_index == target.visible_index
        && current.context_identity.internal_index == target.context_identity.internal_index
        && current.context_identity.serial_number == target.context_identity.serial_number
        && current.widget_identity.internal_index == target.widget_identity.internal_index
        && current.widget_identity.serial_number == target.widget_identity.serial_number;
}

bool refresh_active_list_item_ui_impl(const SelectionSnapshot& expected,
    const ListItemUiRefreshTarget& target)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!callback || !g_original_on_setup_item_body
        || !validate_active_list_item_ui_target_impl(expected, target)) return false;

    int32_t source_count = kVanillaPianoSongCount;
    PianoListArrayView list_view{};
    if (read_music_list_array(target.context, list_view))
        source_count = std::max(kVanillaPianoSongCount,
            std::min(target.visible_index, list_view.count - 1));
    SelectionSnapshot render = expected;
    render.base_slot = std::clamp(expected.song->base_slot, 0, source_count);
    ScopedSongRenderContext active_setup(render);
    g_original_on_setup_item_body(target.context, target.widget, target.visible_index);
    (void)apply_custom_rank_text(target.widget, *render.song, render.profile,
        target.visible_index);
    return true;
}

bool install_raw_hook(const HookInstallContext& context, const char* spec_name, void* detour, void** original, core::RawRvaHook& hook)
{
    const HookSpec* spec = find_hook_spec(spec_name);
    if (!spec) {
        std::ostringstream out;
        out << "[list_patch] status=hook_spec_missing name=" << spec_name;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    std::string error;
    if (!hook.install(context.exe_module, spec->rva, spec->expected_prologue, detour, original, error)) {
        std::ostringstream out;
        out << "[list_patch] status=hook_install_failed name=" << spec_name << " error=" << error;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    std::ostringstream out;
    out << "[list_patch] status=hook_installed name=" << spec_name << " rva=0x" << std::hex << spec->rva;
    core::log(core::LogLevel::Info, out.str());
    return true;
}

} // namespace

class PreparedPianoListCatalog final {
public:
    void* widget = nullptr;
    UObjectLiveHandle widget_identity{};
    RegistrySnapshot expected{};
    std::shared_ptr<const SongRegistryStorage> replacement;
    NativeArrayTuple source{};
    NativeArrayTuple target{};
    int32_t source_backing_capacity = 0;
    int32_t target_backing_capacity = 0;
    bool has_expected_owner = false;
    bool consume_republish_candidate = false;
    bool supersede_republish_candidate = false;
    PianoListRepublishCandidate expected_republish_candidate{};
    RegistrySnapshot expected_owner_catalog{};
    uintptr_t expected_owner_entries = 0;
    int32_t expected_owner_count = 0;
    int32_t expected_owner_capacity = 0;
    PianoListEntry* expected_original_array = nullptr;
    int32_t expected_original_count = 0;
    int32_t expected_original_capacity = 0;
    std::list<OwnedPianoListPatch> staged_owner;
    NativeArrayTupleAccess access;
    std::unique_lock<std::mutex> commit_lock;
    bool committed = false;
};

std::shared_ptr<PreparedPianoListCatalog> prepare_piano_list_catalog_impl(
    void* widget, const UObjectLiveHandle& widget_identity,
    const RegistrySnapshot& expected,
    std::shared_ptr<const SongRegistryStorage> replacement,
    const bool republish) noexcept
{
    try {
        if (!widget || !replacement || !validate_list_identity(widget, widget_identity)
            || expected.generation == UINT64_MAX
            || expected.catalog_revision == UINT64_MAX
            || g_catalog_terminal_failure.load(std::memory_order_acquire)) return {};
        auto prepared = std::make_shared<PreparedPianoListCatalog>();
        prepared->widget = widget;
        prepared->widget_identity = widget_identity;
        prepared->expected = expected;
        prepared->replacement = std::move(replacement);

        PianoListArrayView source_view{};
        if (!read_music_list_array(widget, source_view)) return {};
        prepared->source = {reinterpret_cast<uintptr_t>(source_view.data),
            source_view.count, source_view.capacity};

        {
            std::unique_lock lock(g_owned_list_mutex, std::try_to_lock);
            if (!lock) return {};
            auto current = std::find_if(g_owned_lists.begin(), g_owned_lists.end(),
                [widget](const OwnedPianoListPatch& owner) { return owner.context == widget; });
            if (republish) {
                if (current != g_owned_lists.end()
                    || !same_catalog_identity(g_republish_candidate, expected)) return {};
                prepared->consume_republish_candidate = true;
                prepared->expected_republish_candidate = g_republish_candidate;
                prepared->source_backing_capacity = source_view.capacity;
            } else if (current != g_owned_lists.end()) {
                if (!validate_list_identity(current->context, current->context_identity)
                    || current->catalog.catalog_revision != expected.catalog_revision
                    || current->catalog.storage != expected.storage
                    || source_view.data != current->entries.data()
                    || source_view.count != static_cast<int32_t>(current->entries.size())
                    || source_view.capacity != static_cast<int32_t>(current->entries.capacity())) return {};
                prepared->has_expected_owner = true;
                prepared->expected_owner_catalog = current->catalog;
                prepared->expected_owner_entries = reinterpret_cast<uintptr_t>(current->entries.data());
                prepared->expected_owner_count = static_cast<int32_t>(current->entries.size());
                prepared->expected_owner_capacity = static_cast<int32_t>(current->entries.capacity());
                prepared->expected_original_array = current->original_array;
                prepared->expected_original_count = current->original_count;
                prepared->expected_original_capacity = current->original_capacity;
                prepared->source_backing_capacity = prepared->expected_owner_capacity;
            } else {
                prepared->source_backing_capacity = source_view.capacity;
            }
            prepared->supersede_republish_candidate = !republish;
        }

        // The owner lock is deliberately released before all allocation and
        // native-row cloning. Commit refinds and revalidates the exact facts.
        prepared->staged_owner.emplace_back();
        auto& owner = prepared->staged_owner.back();
        owner.context = widget;
        owner.context_identity = widget_identity;
        owner.catalog = republish
            ? expected
            : RegistrySnapshot{expected.generation + 1,
                expected.catalog_revision + 1, prepared->replacement};
        if (!prepared->has_expected_owner) {
            owner.original_array = source_view.data;
            owner.original_count = source_view.count;
            owner.original_capacity = source_view.capacity;
        } else {
            owner.original_array = prepared->expected_original_array;
            owner.original_count = prepared->expected_original_count;
            owner.original_capacity = prepared->expected_original_capacity;
        }
        const PianoListArrayView composition_source = prepared->has_expected_owner
            ? PianoListArrayView{prepared->expected_original_array,
                prepared->expected_original_count,
                prepared->expected_original_capacity}
            : source_view;
        const RegistrySnapshot replacement_view = owner.catalog;
        int32_t desired = 0;
        if (!appended_music_list_count(
                composition_source, replacement_view, desired)) return {};
        owner.entries.reserve(static_cast<size_t>(std::max(source_view.capacity, desired)));
        if (!clone_registry_entries(
                composition_source, replacement_view, owner.entries)) return {};
        prepared->target = {reinterpret_cast<uintptr_t>(owner.entries.data()),
            static_cast<int32_t>(owner.entries.size()), static_cast<int32_t>(owner.entries.capacity())};
        prepared->target_backing_capacity = static_cast<int32_t>(owner.entries.capacity());
        prepared->access = {
            [widget, widget_identity](NativeArrayTuple& tuple) {
                PianoListEntry* pointer = nullptr;
                return validate_list_identity(widget, widget_identity)
                    && core::safe_read_field(widget, kMusicListEntriesOffset, pointer)
                    && core::safe_read_field(widget, kMusicListEntryCountOffset, tuple.count)
                    && core::safe_read_field(widget, kMusicListEntryCapacityOffset, tuple.capacity)
                    && (tuple.pointer = reinterpret_cast<uintptr_t>(pointer), true);
            },
            [widget](uintptr_t value) { return core::safe_write_field(widget,
                kMusicListEntriesOffset, reinterpret_cast<PianoListEntry*>(value)); },
            [widget](int32_t value) { return core::safe_write_field(widget,
                kMusicListEntryCountOffset, value); },
            [widget](int32_t value) { return core::safe_write_field(widget,
                kMusicListEntryCapacityOffset, value); },
            {},
        };
        return prepared;
    } catch (...) { return {}; }
}

std::shared_ptr<PreparedPianoListCatalog> prepare_piano_list_catalog(
    void* widget, const UObjectLiveHandle& widget_identity,
    const RegistrySnapshot& expected,
    std::shared_ptr<const SongRegistryStorage> replacement) noexcept
{
    return prepare_piano_list_catalog_impl(widget, widget_identity,
        expected, std::move(replacement), false);
}

bool piano_list_first_custom_row(void* widget,
    const UObjectLiveHandle& widget_identity, int32_t& first_custom_row) noexcept
{
    PianoListArrayView view{};
    if (!validate_list_identity(widget, widget_identity)
        || !read_music_list_array(widget, view)) return false;
    try {
        std::unique_lock lock(g_owned_list_mutex, std::try_to_lock);
        if (!lock) return false;
        const auto owner = std::find_if(g_owned_lists.begin(), g_owned_lists.end(),
            [widget](const OwnedPianoListPatch& candidate) {
                return candidate.context == widget;
            });
        if (owner == g_owned_lists.end()) {
            first_custom_row = view.count;
            return true;
        }
        if (!same_list_identity(owner->context_identity, widget_identity)
            || !validate_list_identity(owner->context, owner->context_identity)
            || view.data != owner->entries.data()
            || view.count != static_cast<int32_t>(owner->entries.size())
            || view.capacity != static_cast<int32_t>(owner->entries.capacity())
            || !owner->original_array || owner->original_count <= 0
            || owner->original_capacity < owner->original_count
            || owner->original_count
                > runtime_layouts::PianoMusicList::maximum_count) {
            return false;
        }
        first_custom_row = owner->original_count;
        return true;
    } catch (...) {
        return false;
    }
}

std::shared_ptr<PreparedPianoListCatalog> prepare_piano_list_catalog_republish(
    void* widget, const UObjectLiveHandle& widget_identity) noexcept
{
    PianoListRepublishCandidate candidate{};
    {
        std::unique_lock lock(g_owned_list_mutex, std::try_to_lock);
        if (!lock || !g_republish_candidate) return {};
        candidate = g_republish_candidate;
    }
    const RegistrySnapshot candidate_catalog{
        0, candidate.catalog_revision, candidate.storage};
    return prepare_piano_list_catalog_impl(widget, widget_identity,
        candidate_catalog, candidate.storage, true);
}

PianoListCatalogCommitResult commit_prepared_piano_list_catalog(
    PreparedPianoListCatalog& prepared) noexcept
{
    prepared.commit_lock = std::unique_lock<std::mutex>(g_owned_list_mutex, std::try_to_lock);
    if (!prepared.commit_lock) return PianoListCatalogCommitResult::Rejected;
    const auto reject = [&prepared] {
        prepared.commit_lock.unlock();
        return PianoListCatalogCommitResult::Rejected;
    };
    if (g_catalog_terminal_failure.load(std::memory_order_acquire)
        || !validate_list_identity(prepared.widget, prepared.widget_identity))
        return reject();
    auto current = std::find_if(g_owned_lists.begin(), g_owned_lists.end(),
        [&](const OwnedPianoListPatch& owner) { return owner.context == prepared.widget; });
    if ((current != g_owned_lists.end()) != prepared.has_expected_owner)
        return reject();
    if (prepared.consume_republish_candidate
        && !same_catalog_identity(g_republish_candidate,
            prepared.expected_republish_candidate)) return reject();
    if (prepared.has_expected_owner
        && (current->catalog.catalog_revision
                != prepared.expected_owner_catalog.catalog_revision
            || current->catalog.storage != prepared.expected_owner_catalog.storage
            || reinterpret_cast<uintptr_t>(current->entries.data()) != prepared.expected_owner_entries
            || static_cast<int32_t>(current->entries.size()) != prepared.expected_owner_count
            || static_cast<int32_t>(current->entries.capacity()) != prepared.expected_owner_capacity
            || current->original_array != prepared.expected_original_array
            || current->original_count != prepared.expected_original_count
            || current->original_capacity != prepared.expected_original_capacity)) return reject();
    const auto result = publish_native_array_tuple(prepared.source,
        prepared.source_backing_capacity, prepared.target,
        prepared.target_backing_capacity, prepared.access
#ifdef FF7RP_LIST_CATALOG_SELFTEST
        , g_test_fault
#endif
    );
    if (!result.committed) {
        if (!result.rollback_verified) {
            g_owned_lists.splice(g_owned_lists.end(), prepared.staged_owner);
            g_catalog_terminal_failure.store(true, std::memory_order_release);
            prepared.commit_lock.unlock();
            return PianoListCatalogCommitResult::RollbackUnverified;
        }
        return reject();
    }
#ifdef FF7RP_LIST_CATALOG_SELFTEST
    if (g_test_trace) g_test_trace(0);
#endif
    g_owned_lists.splice(g_owned_lists.end(), prepared.staged_owner);
#ifdef FF7RP_LIST_CATALOG_SELFTEST
    if (g_test_trace) g_test_trace(1);
#endif
    prepared.committed = true;
    return PianoListCatalogCommitResult::Committed;
}

void finalize_prepared_piano_list_catalog(PreparedPianoListCatalog& prepared) noexcept
{
    if (!prepared.committed || !prepared.commit_lock) return;
    if (prepared.has_expected_owner) {
        const auto old = std::find_if(g_owned_lists.begin(), g_owned_lists.end(),
            [&](const OwnedPianoListPatch& owner) {
                return owner.context == prepared.widget
                    && owner.catalog.catalog_revision
                        == prepared.expected_owner_catalog.catalog_revision
                    && owner.catalog.storage
                        == prepared.expected_owner_catalog.storage
                    && reinterpret_cast<uintptr_t>(owner.entries.data())
                        == prepared.expected_owner_entries;
            });
        if (old != g_owned_lists.end()) g_owned_lists.erase(old);
    }
    if (prepared.consume_republish_candidate
        && same_catalog_identity(g_republish_candidate,
            prepared.expected_republish_candidate)) {
        g_republish_candidate = {};
    } else if (prepared.supersede_republish_candidate) {
        g_republish_candidate = {};
    }
    prepared.commit_lock.unlock();
}

bool piano_list_catalog_terminal_failure() noexcept
{
    return g_catalog_terminal_failure.load(std::memory_order_acquire);
}

bool restore_last_played_menu_focus(const MenuSessionSnapshot& opening,
    void (*restore_selection)(void*)) noexcept
{
    try {
        if (!restore_selection || opening.phase != MenuSessionPhase::Opening
            || !menu_session_matches(opening.generation, opening.widget,
                opening.widget_identity, false)) return true;
        const auto catalog = registry().registry_snapshot();
        if (piano_list_catalog_owner_state(opening.widget, opening.widget_identity)
            != PianoListOwnerState::Managed) return !piano_list_catalog_terminal_failure();
        PianoListArrayView array{};
        if (!read_music_list_array(opening.widget, array)) return true;
        const auto target = registry().prepare_last_played_focus();
        if (!target) return true;
        int32_t first_custom = 0;
        if (!piano_list_first_custom_row(opening.widget, opening.widget_identity, first_custom)
            || target.storage != catalog.storage || target.visible_index < first_custom
            || target.visible_index >= array.count) return true;
        uint64_t target_name = 0;
        const ptrdiff_t row_offset = static_cast<ptrdiff_t>(target.visible_index) * sizeof(PianoListEntry);
        if (!core::safe_read_field(array.data, row_offset, target_name)) return true;
        const auto exact = [&]() noexcept {
            RegistrySnapshot current;
            PianoListArrayView live{};
            uint16_t word = 0;
            uint64_t row_name = 0;
            void* widget = nullptr;
            UObjectLiveHandle identity{};
            return menu_session_matches(opening.generation, opening.widget, opening.widget_identity, false)
                && validate_list_identity(opening.widget, opening.widget_identity)
                && core::safe_read_field(opening.list, 0x7d8, word) && word == 1
                && resolve_piano_menu_widget_binding(opening.list, widget, identity)
                && widget == opening.widget && same_list_identity(identity, opening.widget_identity)
                && registry().try_registry_snapshot(current)
                && current.storage == catalog.storage && current.catalog_revision == catalog.catalog_revision
                && read_music_list_array(opening.widget, live)
                && live.data == array.data && live.count == array.count && live.capacity == array.capacity
                && core::safe_read_field(live.data, row_offset, row_name) && row_name == target_name;
        };
        const auto result = restore_menu_focus_fields(target.visible_index, exact,
            [&](ptrdiff_t offset, auto& value) noexcept {
                return core::safe_read_field(opening.widget, offset, value);
            }, [&](ptrdiff_t offset, auto value) noexcept {
                return core::safe_write_field(opening.widget, offset, value);
            }, [&] { restore_selection(opening.widget); });
        if (result == MenuFocusRestoreResult::NotApplied) return true;
        bool synchronized = result == MenuFocusRestoreResult::Applied && exact();
        if (synchronized) {
            synchronized = synchronize_restored_menu_selection(opening.widget,
                target.visible_index, target.generation);
            const auto current = registry().selection_snapshot();
            synchronized = synchronized && current.storage == target.storage
                && current.song == target.song && current.profile == target.profile;
            if (synchronized) synchronized = profile_list_coordinator().reconcile_open_focus(
                opening.generation, current);
            ListItemUiRefreshTarget item;
            if (synchronized && capture_active_list_item_ui_target_impl(current, item)) {
                // Reuse list readiness binding: an earlier, different deferred
                // identity is rejected by run_list_return, never retargeted.
                on_setup_item_body_detour(item.context, item.widget, item.visible_index);
            }
            // A virtualized offscreen row may not yet have a live item widget.
            // Its normal setup callback consumes the restored profile when it
            // is realized; absence alone is not a native mutation failure.
        }
        if (!synchronized || !exact()) {
            g_catalog_terminal_failure.store(true, std::memory_order_release);
            core::log(core::LogLevel::Error,
                "[menu_session] focus_restore status=failed ownership=retained ready=blocked");
            return false;
        }
        return true;
    } catch (...) {
        g_catalog_terminal_failure.store(true, std::memory_order_release);
        return false;
    }
}

PianoListRepublishState piano_list_catalog_republish_state() noexcept
{
    std::unique_lock lock(g_owned_list_mutex, std::try_to_lock);
    if (!lock) return PianoListRepublishState::Transient;
    return g_republish_candidate
        ? PianoListRepublishState::Pending : PianoListRepublishState::None;
}

PianoListOwnerState piano_list_catalog_owner_state(
    void* widget, const UObjectLiveHandle& widget_identity) noexcept
{
    RegistrySnapshot authoritative{};
    if (!registry().try_registry_snapshot(authoritative)) {
        return PianoListOwnerState::Transient;
    }
    try {
        std::unique_lock lock(g_owned_list_mutex, std::try_to_lock);
        if (!lock) return PianoListOwnerState::Transient;
        const auto owner = std::find_if(g_owned_lists.begin(), g_owned_lists.end(),
            [widget](const OwnedPianoListPatch& candidate) {
                return candidate.context == widget;
            });
        if (owner == g_owned_lists.end()) {
            if (authoritative.storage && authoritative.storage->empty())
                return PianoListOwnerState::None;
            if (same_catalog_identity(g_republish_candidate, authoritative))
                return PianoListOwnerState::None;
            g_catalog_terminal_failure.store(true, std::memory_order_release);
            return PianoListOwnerState::Uncertain;
        }
        NativeArrayTuple current{};
        const NativeArrayTupleAccess access = owned_list_tuple_access(*owner);
        if (owner->catalog.catalog_revision != authoritative.catalog_revision
            || owner->catalog.storage != authoritative.storage
            || !same_list_identity(owner->context_identity, widget_identity)
            || !validate_list_identity(widget, widget_identity)
            || !access.read || !access.read(current)
            || !(current == redirected_tuple(*owner))) {
            g_catalog_terminal_failure.store(true, std::memory_order_release);
            return PianoListOwnerState::Uncertain;
        }
        return PianoListOwnerState::Managed;
    } catch (...) {
        g_catalog_terminal_failure.store(true, std::memory_order_release);
        return PianoListOwnerState::Uncertain;
    }
}

ExactCloseListRestoreResult restore_owned_list_after_exact_cancel_close(
    const MenuSessionSnapshot& closing, void* post_close_widget,
    const UObjectLiveHandle& post_close_identity) noexcept
{
    ExactCloseListRestoreResult disposition
        = ExactCloseListRestoreResult::NoOwnedPatch;
    RegistrySnapshot authoritative{};
    const bool authoritative_read
        = registry().try_registry_snapshot(authoritative);
    try {
        {
            std::lock_guard lock(g_owned_list_mutex);
            const auto owner = std::find_if(g_owned_lists.begin(), g_owned_lists.end(),
                [&](const OwnedPianoListPatch& candidate) {
                    return candidate.context == closing.widget;
                });
            if (!closing || closing.phase != MenuSessionPhase::Closing
                || post_close_widget != closing.widget
                || !same_list_identity(post_close_identity,
                    closing.widget_identity)) {
                disposition = ExactCloseListRestoreResult::SessionIdentityMismatch;
            } else if (!validate_list_identity(
                    closing.widget, closing.widget_identity)) {
                disposition = ExactCloseListRestoreResult::LiveIdentityInvalid;
            } else if (owner == g_owned_lists.end()) {
                if (closing.list_ownership == MenuListOwnership::ReclassifyOnExactClose
                    && authoritative_read && authoritative.storage
                    && (authoritative.storage->empty()
                        || same_catalog_identity(
                            g_republish_candidate, authoritative))) {
                    disposition = ExactCloseListRestoreResult::VerifiedOwnerless;
                } else if (!authoritative_read
                    || (closing.list_ownership
                            == MenuListOwnership::ReclassifyOnExactClose
                        && authoritative.storage && !authoritative.storage->empty())) {
                    disposition = ExactCloseListRestoreResult::CatalogIdentityMismatch;
                }
            } else {
                if (owner->context != closing.widget
                    || !same_list_identity(owner->context_identity,
                        closing.widget_identity)) {
                    disposition = ExactCloseListRestoreResult::SessionIdentityMismatch;
                } else if (!authoritative_read
                    || owner->catalog.catalog_revision
                        != authoritative.catalog_revision
                    || owner->catalog.storage != authoritative.storage) {
                    disposition = ExactCloseListRestoreResult::CatalogIdentityMismatch;
                } else {
                    const NativeArrayTuple expected_redirected
                        = redirected_tuple(*owner);
                    NativeArrayTuple current{};
                    const NativeArrayTupleAccess access
                        = owned_list_tuple_access(*owner);
                    if (!access.read || !access.read(current)) {
                        disposition = ExactCloseListRestoreResult::TupleUnreadable;
                    } else if (!(current == expected_redirected)) {
                        disposition = ExactCloseListRestoreResult::TupleDrift;
                    } else {
                        const auto transition = restore_owned_list_tuple(*owner);
                        if (!transition.committed) {
                            disposition = transition.rollback_verified
                                ? ExactCloseListRestoreResult::TransitionFailed
                                : ExactCloseListRestoreResult::RollbackUnverified;
                        } else {
                            g_republish_candidate = {
                                authoritative.catalog_revision,
                                authoritative.storage};
                            g_owned_lists.erase(owner);
                            disposition = ExactCloseListRestoreResult::Restored;
                        }
                    }
                }
            }
            if (exact_close_list_restore_is_terminal(disposition))
                g_catalog_terminal_failure.store(true, std::memory_order_release);
        }
    } catch (...) {
        disposition = ExactCloseListRestoreResult::RollbackUnverified;
        g_catalog_terminal_failure.store(true, std::memory_order_release);
    }
    try {
        std::ostringstream out;
        out << "[list_patch] exact_close_restore status="
            << (disposition == ExactCloseListRestoreResult::Restored
                    ? "restored"
                    : disposition == ExactCloseListRestoreResult::VerifiedOwnerless
                        ? "verified_ownerless" : "failed")
            << " generation=" << closing.generation
            << " storage="
            << (disposition == ExactCloseListRestoreResult::Restored
                    ? "retired"
                    : disposition == ExactCloseListRestoreResult::VerifiedOwnerless
                        ? "none" : "retained")
            << " reopen="
            << (!exact_close_list_restore_is_terminal(disposition)
                    ? "allowed" : "blocked")
            << " first_failure="
            << exact_close_restore_failure_name(disposition);
        core::log(!exact_close_list_restore_is_terminal(disposition)
                ? core::LogLevel::Info : core::LogLevel::Error,
            out.str());
    } catch (...) {}
    return disposition;
}

#ifdef FF7RP_LIST_CATALOG_SELFTEST
void configure_list_catalog_selftest(ListCatalogIdentityValidator identity,
    NativeArrayTupleFaultInjection fault, ListCatalogTrace trace) noexcept
{
    g_test_identity = identity;
    g_test_fault = fault;
    g_test_trace = trace;
}

bool list_catalog_selftest_compose(void* source_entries, const int32_t source_count,
    const std::shared_ptr<const SongRegistryStorage>& catalog,
    std::vector<PianoListEntry>& out) noexcept
{
    const PianoListArrayView source{static_cast<PianoListEntry*>(source_entries),
        source_count, source_count};
    return clone_registry_entries(source, RegistrySnapshot{1, 1, catalog}, out);
}

std::size_t list_catalog_selftest_owner_count() noexcept
{
    std::lock_guard lock(g_owned_list_mutex);
    return g_owned_lists.size();
}

bool list_catalog_selftest_matches_registry(void* widget) noexcept
{
    return list_catalog_matches_registry(widget);
}
void list_catalog_selftest_lock_owners() { g_owned_list_mutex.lock(); }
void list_catalog_selftest_unlock_owners() { g_owned_list_mutex.unlock(); }
bool list_profile_initialization_selftest(void* context, void* widget,
    const int32_t index, SelectionSnapshot& initialized) noexcept
{
    initialized = {};
    try {
        ListSetupView view{context, widget, index};
        if (!prepare_list_setup_view(view)) return false;
        initialized = view.identity.setup_selection;
        return static_cast<bool>(initialized);
    } catch (...) { return false; }
}
#endif

bool capture_active_list_item_ui_target(const SelectionSnapshot& expected,
    ListItemUiRefreshTarget& out) noexcept
{
    return capture_active_list_item_ui_target_impl(expected, out);
}

bool validate_active_list_item_ui_target(const SelectionSnapshot& expected,
    const ListItemUiRefreshTarget& target) noexcept
{
    return validate_active_list_item_ui_target_impl(expected, target);
}

bool refresh_active_list_item_ui(const SelectionSnapshot& expected,
    const ListItemUiRefreshTarget& target)
{
    return refresh_active_list_item_ui_impl(expected, target);
}

bool install_list_patch_hooks(const HookInstallContext& context)
{
    g_exe_module = context.exe_module;

    const bool ok = install_raw_hook(
        context,
        "piano_on_setup_item_body",
        reinterpret_cast<void*>(&on_setup_item_body_detour),
        reinterpret_cast<void**>(&g_original_on_setup_item_body),
        g_on_setup_item_body_hook);
    if (!ok) {
        return false;
    }

    return true;
}

bool restore_owned_list_patches()
{
    bool restored_all = true;
    bool rollback_verified = true;
    {
        std::lock_guard<std::mutex> lock(g_owned_list_mutex);
        if (g_catalog_terminal_failure.load(std::memory_order_acquire)) return false;
        for (auto& owned : g_owned_lists) {
            if (!validate_list_identity(owned.context, owned.context_identity)) {
                restored_all = false;
                continue;
            }
            const auto result = restore_owned_list_tuple(owned);
            if (!result.committed) {
                restored_all = false;
                rollback_verified = rollback_verified && result.rollback_verified;
            }
        }
    }
    if (!restored_all) {
        core::log(core::LogLevel::Error,
            "[list_patch] restore status=failed storage=retained rollback="
            + std::string(rollback_verified ? "verified" : "unverified"));
    }
    return restored_all;
}

core::HookShutdownResult shutdown_list_patch()
{
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_on_setup_item_body_hook),
    }, [] {
        return restore_owned_list_patches();
    }, [] {
        g_original_on_setup_item_body = nullptr;
        {
            std::lock_guard<std::mutex> item_lock(g_dynamic_item_mutex);
            g_dynamic_items.clear();
        }
        g_exe_module = nullptr;
        std::lock_guard<std::mutex> lock(g_owned_list_mutex);
        g_owned_lists.clear();
        g_republish_candidate = {};
    });
}

} // namespace ff7r::piano::game
