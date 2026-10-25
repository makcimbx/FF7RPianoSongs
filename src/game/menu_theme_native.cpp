#include "game/menu_theme_native.h"
#include "game/song_preview_anchor.h"
#include "game/menu_theme_metrics.h"
#include "game/song_hud_runtime.h"

#include "core/pe_image.h"
#include "core/version_gate.h"
#include "game/hook_specs.h"
#include "game/rvas.h"
#include "game/uobject_identity.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace ff7r::piano::game::menu_theme_native {
namespace perf = menu_theme_metrics;
namespace {
struct ClassifierPhase {
    const CallbackAuthority* authority = nullptr;
    bool qualified = false;
    bool revoked = false;
};
thread_local ClassifierPhase* classifier_phase = nullptr;
void end_read_phase() noexcept;
void reject_read_phase() noexcept;
struct ReadPhase;
ReadPhase* suspend_reads() noexcept;
void resume_reads(ReadPhase*) noexcept;
void revoke_classifier() noexcept
{
    if (classifier_phase) classifier_phase->revoked = true;
}
// Explicit SEH-finally balances diagnostics without consuming or retrying faults.
template<class F, class... Args>
auto measured_native(perf::Metric metric, F function, Args... args) -> decltype(function(args...))
{
    auto* suspended = suspend_reads();
    perf::Stamp stamp{}; perf::begin(stamp, metric);
    bool completed = false;
    __try {
        if constexpr (std::is_void_v<decltype(function(args...))>) {
            function(args...); completed = true;
        } else {
            auto result = function(args...); completed = true; return result;
        }
    } __finally { resume_reads(suspended); perf::finish(stamp, completed); }
}
namespace objects = uobject_locator_core;
using Address = objects::Address;
using FNameCtor = void* (__fastcall*)(std::uint64_t*, const wchar_t*, std::int32_t);
using StaticClass = void* (__fastcall*)();
using Construct = void* (__fastcall*)(const void*);
using BrushConstruct = void* (__fastcall*)(void*);
using Unary = void (__fastcall*)(void*);
using Binary = void (__fastcall*)(void*, void*);
using TextureCreate = void* (__fastcall*)(std::int32_t, std::int32_t, std::int32_t, std::uint64_t);
using FindChild = void* (__fastcall*)(void*, const std::uint64_t*);
using Assign = void* (__fastcall*)(void*, const void*);
using Invalidate = void (__fastcall*)(void*, std::uint8_t);
using HudString = runtime_layouts::FString;
struct HudPosition { float x, y; };
using HudTextFromString = void* (__fastcall*)(void*, const HudString*);
using HudItemConstruct = void* (__fastcall*)(void*, const HudPosition*, const void*, void*,
    const runtime_layouts::FLinearColor*);
struct HudTextFunctions {
    HudTextFromString from_string = nullptr;
    HudItemConstruct construct = nullptr;
    Unary destroy_text = nullptr, destroy_item = nullptr;
    Binary submit = nullptr;
    explicit operator bool() const noexcept {
        return from_string && construct && destroy_text && destroy_item && submit;
    }
};
using HudInfoConstruct = void* (__fastcall*)(void*,void*,std::int32_t,const std::uint64_t*,const void*);
using HudItemMeasure = HudPosition* (__fastcall*)(void*,HudPosition*,float);
using HudCanvasDpi = float (__fastcall*)(void*);
struct HudSharpFunctions {
    HudInfoConstruct construct = nullptr;
    HudItemMeasure measure = nullptr;
    HudCanvasDpi dpi = nullptr;
    explicit operator bool() const noexcept { return construct && measure && dpi; }
};
enum class NativeClass : std::size_t { UserWidget, WidgetTree, Image, Pie, Tiled, ImageSet, Button, Text, Canvas, SongList, SpeedList, Count };
constexpr auto kClassCount = static_cast<std::size_t>(NativeClass::Count);
struct ClassSpec { const char* reference; Address slot, site; const wchar_t* name; bool umg; };
const std::array<ClassSpec, kClassCount> class_specs{{
    {"theme_class_user_widget_reference", rva::ThemeClassUserWidget, rva::ThemeClassUserWidgetReference, L"UserWidget", true},
    {"theme_class_widget_tree_reference", rva::ThemeClassWidgetTree, rva::ThemeClassWidgetTreeReference, L"WidgetTree", true},
    {"end_image_static_class", rva::ThemeClassEndImage, rva::EndImageStaticClass, L"EndImage", false},
    {"theme_class_end_pie_image_reference", rva::ThemeClassEndPieImage, rva::ThemeClassEndPieImageReference, L"EndPieImage", false},
    {"theme_class_end_tiled_image_reference", rva::ThemeClassEndTiledImage, rva::ThemeClassEndTiledImageReference, L"EndTiledImage", false},
    {"theme_class_end_image_set_reference", rva::ThemeClassEndImageSet, rva::ThemeClassEndImageSetReference, L"EndImageSet", false},
    {"theme_class_end_button_reference", rva::ThemeClassEndButton, rva::ThemeClassEndButtonReference, L"EndButton", false},
    {"theme_class_end_text_block_reference", rva::ThemeClassEndTextBlock, rva::ThemeClassEndTextBlockReference, L"EndTextBlock", false},
    {"theme_class_end_canvas_panel_reference", rva::ThemeClassEndCanvasPanel, rva::ThemeClassEndCanvasPanelReference, L"EndCanvasPanel", false},
    {"theme_class_end_virtual_scroll_box_reference", rva::ThemeClassEndVirtualScrollBox, rva::ThemeClassEndVirtualScrollBoxReference, L"EndVirtualScrollBox", false},
    {"theme_class_end_list_box_reference", rva::ThemeClassEndListBox, rva::ThemeClassEndListBoxReference, L"EndListBox", false},
}};
struct GenericFunctions {
    bool classes = false, styles = false;
    Assign color_assign = nullptr;
    std::array<void*, kClassCount> slots{};
    Assign brush_assign = nullptr, style_assign = nullptr;
    BrushConstruct style_construct = nullptr;
    Unary style_destroy = nullptr, tiled_sync = nullptr, images_sync = nullptr;
    Binary pie_brush = nullptr, tiled_brush = nullptr, button_style = nullptr;
    Invalidate invalidate = nullptr;
};

struct Functions {
    FNameCtor fname = nullptr;
    StaticClass end_image_class = nullptr;
    Construct construct = nullptr;
    BrushConstruct brush_construct = nullptr;
    Unary brush_destroy = nullptr, materialize = nullptr, unlock = nullptr, update = nullptr;
    Binary set_resource = nullptr, set_brush = nullptr;
    TextureCreate texture_create = nullptr;
    void* package_slot = nullptr;
    FindChild find_child = nullptr;
    void* panel_class_slot = nullptr;
    void* panel_slot_class_slot = nullptr;
    Unary mark_not_offline_processed = nullptr;
};
using Object = detail::ObjectSnapshot;
using Target = detail::TargetState;
enum class RecordKind { Prepared, Original, Composed, Tinted };
// Canonical native EndImage size in both profiles; each retained record owns one.
constexpr std::uint64_t kRootedCarrierBytes = 0x1e8;
struct Record {
    RecordKind kind = RecordKind::Prepared;
    std::size_t original_slot = kInvalidImageSlot, prepared_slot = kInvalidImageSlot;
    std::array<float, 4> tint_rgba{};
    // Original resources may be absent or arbitrary assets (not just Texture2D);
    // their Outer is unrestricted. Prepared/Composed resources stay transient.
    Object package{}, holder{}, texture{};
    Object package_class{}, holder_class{}, texture_class{};
    alignas(8) std::array<std::uint8_t, kOriginalCaptureByteCharge> local_brush{};
    bool bound = false, published = false;
    DWORD creation_thread = 0;
    std::uint32_t width = 0, height = 0;
};
std::uint64_t hud_title_name = 0, hud_panel_name = 0;
// Reader policy caps, NOT recovered engine maxima.
constexpr std::size_t kContainmentDepth = 32;
constexpr std::int32_t kPanelSlots = 256, kAncestryIndex = 128;
struct PanelBases { Object panel{}, slot{}, meta{}, package{}, package_class{}, core_package{}; };
struct FastChain {
    void* actual = nullptr;
    void* base = nullptr;
    std::int32_t base_index = -1, actual_last = -1;
    Address base_array = 0, actual_array = 0, selected = 0;
};
struct Ancestry { Object actual{}, metaclass{}; FastChain chain{}, meta_chain{}; };
struct ContainmentEdge {
    Object child{}, slot{}, parent{};
    Ancestry slot_type{}, parent_type{};
    void* slots = nullptr;
    std::int32_t count = 0, capacity = 0, index = -1;
};
// Bounded stack-only read facts, never a cross-callback admission cache.
struct ReadPhase {
    const CallbackAuthority* authority = nullptr;
    ClassifierPhase admission{};
    std::array<Target, 32> targets{};
    std::array<const Record*, kSubjectTraversalLimit> graphs{};
    std::array<ContainmentEdge, kSubjectTraversalLimit> edges{};
    std::array<PanelBases, 32> bases{};
    std::size_t target_count = 0, graph_count = 0, edge_count = 0, base_count = 0;
    bool witnessed = false;
    bool reset = true, rejected = false, classifying = false, checking_owners = false, reading_graph = false;
};
thread_local ReadPhase* read_phase = nullptr;
void end_read_phase() noexcept
{
    if (!read_phase) return;
    if (read_phase->classifying || read_phase->checking_owners || read_phase->reading_graph) read_phase->rejected = true;
    read_phase->target_count = read_phase->graph_count = read_phase->edge_count = read_phase->base_count = 0;
    read_phase->reset = true;
}
void reject_read_phase() noexcept
{
    if (read_phase) { read_phase->rejected = true; read_phase->admission.revoked = true; }
}
struct ReadScope {
    ReadPhase phase{};
    ReadPhase* previous = read_phase;
    ClassifierPhase* previous_classifier = classifier_phase;
    explicit ReadScope(const CallbackAuthority& a) noexcept
    {
        reject_read_phase(); revoke_classifier();
        phase.authority = &a; phase.admission.authority = &a;
        read_phase = a.phase_current ? &phase : nullptr;
        classifier_phase = read_phase ? &phase.admission : nullptr;
    }
    ~ReadScope() { read_phase = previous; classifier_phase = previous_classifier; }
};
ReadPhase* suspend_reads() noexcept
{
    auto* saved = read_phase;
    end_read_phase(); revoke_classifier();
    read_phase = nullptr; // Reentrant readers must establish independent authority.
    return saved;
}
void resume_reads(ReadPhase* saved) noexcept
{
    read_phase = saved;
    end_read_phase(); revoke_classifier();
}
struct ReadSuspension {
    ReadPhase* saved = suspend_reads();
    ~ReadSuspension() { resume_reads(saved); }
};
struct Names {
    std::uint64_t transient = 0, package = 0, klass = 0, core_package = 0;
    std::uint64_t holder = 0, texture = 0;
};
struct RowBinding {
    std::uint64_t id = 0;
    RowAssociation association{};
    Object main{}, list{}, row{};
    Object speed_tree{}, speed_root{};
    void* speed_members = nullptr;
    std::int32_t speed_count = 0, speed_capacity = 0;
};
struct GenericNames {
    bool ready = false;
    std::array<std::uint64_t, kClassCount> classes{};
    std::uint64_t meta = 0, package = 0, core = 0, umg = 0, game = 0, panel = 0, slot = 0;
};
struct LedgerBudgetExceeded {};
#ifdef FF7RP_MENU_THEME_NATIVE_SELFTEST
bool fail_ledger_allocation = false;
#endif
// Track actual standard-container allocation requests, including its map/blocks.
// Successful requests remain charged even if a later operation fails; no native
// object or failed-attempt reservation is ever refunded.
template<class T> struct LedgerAllocator {
    using value_type = T;
    std::uint64_t* used;
    std::uint64_t* pending;
    LedgerAllocator(std::uint64_t* u, std::uint64_t* p) noexcept : used(u), pending(p) {}
    template<class U> LedgerAllocator(const LedgerAllocator<U>& a) noexcept : used(a.used), pending(a.pending) {}
    T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)
            || *used > kRetainedByteLimit || *pending > kRetainedByteLimit - *used
            || n * sizeof(T) > kRetainedByteLimit - *used - *pending) throw LedgerBudgetExceeded{};
#ifdef FF7RP_MENU_THEME_NATIVE_SELFTEST
        if (fail_ledger_allocation) { fail_ledger_allocation = false; throw std::bad_alloc{}; }
#endif
        auto* result = std::allocator<T>{}.allocate(n);
        *used += n * sizeof(T);
        return result;
    }
    void deallocate(T* p, std::size_t n) noexcept { std::allocator<T>{}.deallocate(p, n); }
    template<class U> bool operator==(const LedgerAllocator<U>& a) const noexcept {
        return used == a.used && pending == a.pending;
    }
};
struct RetainedRecords {
    using Storage = std::deque<Record, LedgerAllocator<Record>>;
    // Lazy construction keeps C++ allocation failures inside the reported admission path.
    std::optional<Storage> storage;
    Record& operator[](std::size_t slot) noexcept { return (*storage)[slot]; }
    const Record& operator[](std::size_t slot) const noexcept { return (*storage)[slot]; }
};
struct State {
    std::mutex mutex;
    Functions functions{};
    HMODULE module = nullptr;
    bool available = false;
    bool poisoned = false;
    std::size_t reserved_images = 0;
    std::uint64_t reserved_bytes = 0;
    std::uint64_t pending_payload = 0;
    RetainedRecords retained{};
    GenericFunctions generic{};
    HudTextFunctions hud_text{};
    HudSharpFunctions hud_sharp{};
    std::array<core::ThemeImage,kSongHudIconCount> hud_icon_pixels{};
    std::array<OwnedImage,kSongHudIconCount> hud_icons{};
    std::array<bool,kSongHudIconCount> hud_icons_ready{};
    GenericNames generic_names{};
    Names carrier_names{}; // FName values only; package/class/owner facts are always fresh.
    std::optional<std::deque<RowBinding, LedgerAllocator<RowBinding>>> rows;
    std::uint64_t row_nonce = 0;
    alignas(8) std::array<std::uint8_t, 0x278> style_scratch{};
    bool style_charged = false;
};
State state; // Native roots are deliberately never released, including at C++ destruction.
Result reserve_record(std::uint64_t payload, std::size_t& slot) noexcept
{
    ReadSuspension suspension;
    slot = kInvalidImageSlot;
    if (state.reserved_images == kInvalidImageSlot || state.reserved_bytes > kRetainedByteLimit
        || payload > kRetainedByteLimit - state.reserved_bytes) return {Outcome::Budget, "ledger_byte_budget"};
    state.pending_payload = payload; // Container growth must leave room for the native payload.
    try {
        if (!state.retained.storage) state.retained.storage.emplace(
            LedgerAllocator<Record>{&state.reserved_bytes, &state.pending_payload});
        state.retained.storage->emplace_back(); // deque preserves all existing Record addresses.
    } catch (const LedgerBudgetExceeded&) {
        state.pending_payload = 0; return {Outcome::Budget, "ledger_byte_budget"};
    } catch (const std::bad_alloc&) {
        state.pending_payload = 0; return {Outcome::Memory, "ledger_allocation"};
    } catch (const std::length_error&) {
        state.pending_payload = 0; return {Outcome::Memory, "ledger_length"};
    }
    state.pending_payload = 0;
    state.reserved_bytes += payload;
    slot = state.reserved_images++;
    return {Outcome::Ready, "ledger_reserved"};
}
thread_local bool entered = false;
struct EntryScope {
    bool previous = entered;
    EntryScope() noexcept { reject_read_phase(); revoke_classifier(); entered = true; }
    ~EntryScope() { entered = previous; }
};

template<class T> bool read(void* p, std::uintptr_t offset, T& value) noexcept
{
    return core::safe_read_field(p, offset, value);
}

bool admitted(const CallbackAuthority& authority) noexcept
{
    if (read_phase && read_phase->authority == &authority) {
        if (read_phase->rejected) return false;
        if (read_phase->reset) {
            read_phase->admission = {&authority};
            read_phase->reset = false;
        }
    }
    auto* phase = classifier_phase;
    const bool sharing = phase && phase->authority == &authority && authority.phase_current;
    if (!sharing) {
        const bool result = state.available && !state.poisoned && authority.game_thread_id != 0
            && authority.game_thread_id == GetCurrentThreadId()
            && authority.session.phase == MenuSessionPhase::Ready
            && authority.session.generation != 0 && authority.session.widget != nullptr
            && authority.qualify_current_callback
            && (!authority.phase_current || authority.phase_current(authority.context, authority.session))
            && authority.qualify_current_callback(authority.context, authority.session)
            && menu_session_matches(authority.session.generation, authority.session.widget,
                authority.session.widget_identity, true)
            && (!authority.phase_current || authority.phase_current(authority.context, authority.session));
        if (!result) { reject_read_phase(); revoke_classifier(); }
        return result;
    }
    const bool current = state.available && !state.poisoned && authority.game_thread_id != 0
        && authority.game_thread_id == GetCurrentThreadId()
        && authority.session.phase == MenuSessionPhase::Ready
        && authority.session.generation != 0 && authority.session.widget != nullptr
        && authority.qualify_current_callback
        && authority.phase_current(authority.context, authority.session);
    if (!current || phase->revoked) {
        reject_read_phase(); revoke_classifier(); return false;
    }
    const bool result = (phase->qualified || authority.qualify_current_callback(authority.context, authority.session))
        && menu_session_matches(authority.session.generation, authority.session.widget,
            authority.session.widget_identity, true)
        && authority.phase_current(authority.context, authority.session)
        && !phase->revoked && classifier_phase == phase;
    if (!result) { reject_read_phase(); revoke_classifier(); }
    else phase->qualified = true;
    return result;
}
bool session_current(const MenuSessionSnapshot& s) noexcept
{
    return s.phase == MenuSessionPhase::Ready && s.generation && s.widget
        && menu_session_matches(s.generation, s.widget, s.widget_identity, true);
}
bool authority_current(const CallbackAuthority& a, bool facts_only) noexcept
{
    return facts_only ? session_current(a.session) : admitted(a);
}
bool row_owner_valid(const objects::UObjectIdentityAccess&, const CallbackAuthority&,
    const Target&, bool) noexcept;
bool generic_type_valid(const objects::UObjectIdentityAccess&, const Target&) noexcept;
bool generic_owner_type_valid(const objects::UObjectIdentityAccess&, const Target&) noexcept;
bool member_valid(const Target&) noexcept;
bool shared_owner(const Target&) noexcept;
bool phase_target_valid(const objects::UObjectIdentityAccess&, const CallbackAuthority&, const Target&) noexcept;

bool capture(const objects::UObjectIdentityAccess& access, void* pointer, Object& out) noexcept
{
    Object value{};
    value.pointer = pointer;
    if (!pointer) return false;
    if (!objects::capture_live_handle(access, reinterpret_cast<Address>(pointer), value.live)
        && objects::capture_item_backed_zero_serial_snapshot_diagnostic(access,
            reinterpret_cast<Address>(pointer), value.zero)
            != objects::UObjectItemBackedZeroSerialCaptureResult::Success) return false;
    if (!read(pointer, 0x10, value.klass) || !value.klass
        || !read(pointer, 0x18, value.name) || !static_cast<std::uint32_t>(value.name)
        || !read(pointer, 0x20, value.outer)) return false;
    out = value;
    return true;
}

bool unchanged(const objects::UObjectIdentityAccess& access, const Object& expected) noexcept
{
    const bool identity = expected.live.serial_number > 0
        ? objects::validate_live_handle(access, reinterpret_cast<Address>(expected.pointer), expected.live)
        : objects::validate_item_backed_zero_serial_snapshot(access,
            reinterpret_cast<Address>(expected.pointer), expected.zero);
    Object now{};
    return identity && capture(access, expected.pointer, now)
        && now.klass == expected.klass && now.outer == expected.outer && now.name == expected.name;
}

bool load_item_flags(const objects::UObjectIdentityAccess& access, const Object& object,
    std::int32_t& flags) noexcept
{
    // Item facts only, after shared identity validation: zero membership is NOT
    // lifetime authority. Rooted ownership is checked separately; no manual CAS.
    const auto index = object.live.serial_number > 0
        ? object.live.internal_index : object.zero.internal_index;
    objects::GUObjectArrayHeader header{};
    Address chunk = 0;
    const auto& facts = access.facts;
    if (index < 0 || facts.elements_per_chunk <= 0
        || !objects::read_guobject_array_header(access.memory, access.array_address,
            facts.array, header) || !objects::chart_header_accepts(access.array_address, header)
        || index >= header.num_elements) return false;
    const auto chunk_index = index / facts.elements_per_chunk;
    if (chunk_index >= header.num_chunks || !access.memory.read
        || !access.memory.read(access.memory.context,
            header.chunks + static_cast<Address>(chunk_index) * sizeof(Address), &chunk, sizeof(chunk))
        || !chunk || !access.atomic_flags.load) return false;
    const Address item = chunk + static_cast<Address>(index % facts.elements_per_chunk) * facts.item.size;
    return access.atomic_flags.load(access.atomic_flags.context, item + facts.item.flags, flags);
}
bool rooted(const objects::UObjectIdentityAccess& access, const Object& object) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::ValidateRooted);
    std::int32_t flags = 0;
    const bool result = unchanged(access, object) && load_item_flags(access, object, flags)
        && (flags & access.facts.root_set) != 0
        && (flags & access.facts.item_unreachable_or_pending_kill) == 0;
    perf::finish(stamp, result);
    return result;
}

bool find_name(FNameCtor ctor, const wchar_t* text, std::uint64_t& name) noexcept
{
    name = 0;
    __try {
        measured_native(perf::Metric::NativeFindName, ctor, &name, text, 0); // canonical FNAME_Find; never create names
        return static_cast<std::uint32_t>(name) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool names(const Functions& f, Names& n) noexcept
{
    return find_name(f.fname, L"/Engine/Transient", n.transient)
        && find_name(f.fname, L"Package", n.package)
        && find_name(f.fname, L"Class", n.klass)
        && find_name(f.fname, L"/Script/CoreUObject", n.core_package)
        && find_name(f.fname, L"EndImage", n.holder)
        && find_name(f.fname, L"Texture2D", n.texture);
}

bool package_valid(const Functions& f, const objects::UObjectIdentityAccess& access,
    const Names& n, Object& package) noexcept
{
    void* pointer = nullptr;
    Object klass{}, metaclass{}, outer{};
    return read(f.package_slot, 0, pointer) && capture(access, pointer, package)
        && package.name == n.transient && package.outer == nullptr && rooted(access, package)
        && capture(access, package.klass, klass) && klass.name == n.package
        && capture(access, klass.klass, metaclass) && metaclass.name == n.klass
        && capture(access, klass.outer, outer) && outer.name == n.core_package
        && outer.outer == nullptr && outer.klass == package.klass;
}

bool package_same(const Functions& f, const objects::UObjectIdentityAccess& access,
    const Object& package) noexcept
{
    void* current = nullptr;
    return read(f.package_slot, 0, current) && current == package.pointer && rooted(access, package);
}

bool graph_valid_sequence(const Functions& f, const objects::UObjectIdentityAccess& access,
    const Record& record) noexcept
{
    void* resource = nullptr;
    void* slate = nullptr;
    void* slate_owner = nullptr;
    return record.bound && package_same(f, access, record.package)
        && rooted(access, record.holder)
        && (record.texture.pointer ? unchanged(access, record.texture)
            && unchanged(access, record.texture_class)
            : (record.kind == RecordKind::Original || record.kind == RecordKind::Tinted) && !record.texture_class.pointer)
        && unchanged(access, record.package_class) && unchanged(access, record.holder_class)
        && record.holder.outer == record.package.pointer
        && (record.kind == RecordKind::Original || record.kind == RecordKind::Tinted
            || record.texture.outer == record.package.pointer)
        && read(record.holder.pointer, 0x1d8, slate) && !slate
        && read(record.holder.pointer, 0x1e0, slate_owner) && !slate_owner
        && read(record.holder.pointer, 0x150, resource) && resource == record.texture.pointer;
}
bool graph_valid(const Functions& f, const objects::UObjectIdentityAccess& access,
    const Record& record) noexcept
{
    auto* phase = read_phase;
    if (!phase) return graph_valid_sequence(f, access, record);
    if (phase->reading_graph) { reject_read_phase(); return false; }
    if (!admitted(*phase->authority)) return false;
    if (record.published) for (std::size_t i = 0; i < phase->graph_count; ++i)
        if (phase->graphs[i] == &record) return true;
    phase->reading_graph = true;
    const bool graph = graph_valid_sequence(f, access, record);
    phase->reading_graph = false;
    const bool valid = graph && admitted(*phase->authority);
    if (!valid) { reject_read_phase(); return false; }
    if (record.published && phase->graph_count < phase->graphs.size()) phase->graphs[phase->graph_count++] = &record;
    return true;
}

bool read_mip(void* texture, detail::FreshMip& m, Result* diagnostic = nullptr) noexcept
{
    const auto fail = [diagnostic](const char* stage) noexcept {
        if (diagnostic) diagnostic->stage = stage;
        return false;
    };
    void* platform = nullptr;
    void* array = nullptr;
    void* mip = nullptr;
    void* data = nullptr;
    if (!read(texture, 0x138, platform) || !platform) return fail("fresh_mip_platform_read");
    if (!read(platform, 0, m.width) || !read(platform, 4, m.height)
        || !read(platform, 0x0c, m.format) || !read(platform, 0x18, array) || !array
        || !read(platform, 0x20, m.count) || !read(platform, 0x24, m.capacity))
        return fail("fresh_mip_header_read");
    if (diagnostic) {
        diagnostic->mip_header_read = true;
        diagnostic->mip_count = m.count;
        diagnostic->mip_capacity = m.capacity;
    }
    if (m.count != 1) return fail("fresh_mip_count");
    // Native first growth allocates at least four pointers, then quantizes size.
    // Only element zero is read; capacity is neither a count nor an iteration bound.
    if (m.capacity < 4) return fail("fresh_mip_capacity");
    if (!read(array, 0, mip) || !mip) return fail("fresh_mip_pointer_read");
    if (!read(mip, 0, m.mip_width) || !read(mip, 4, m.mip_height)
        || !read(mip, 0x18, data) || !read(mip, 0x20, m.bytes)
        || !read(mip, 0x30, m.flags) || !read(mip, 0x34, m.lock))
        return fail("fresh_mip_payload_read");
    m.platform = reinterpret_cast<Address>(platform);
    m.array = reinterpret_cast<Address>(array);
    m.mip = reinterpret_cast<Address>(mip);
    m.data = reinterpret_cast<Address>(data);
    return true;
}

struct Work {
    const Functions* f = nullptr;
    const core::ThemeImage* image = nullptr;
    const CallbackAuthority* authority = nullptr;
    objects::UObjectIdentityAccess access{};
    Names n{};
    Record* record = nullptr;
    bool brush_constructed = false;
    bool locked = false;
    bool cleanup_compromised = false;
    void* bulk = nullptr;
    Result result{Outcome::NativeFault, "begin"};
};

Result sequence(Work& w) noexcept
{
    const auto& f = *w.f;
    auto& r = *w.record;
    const auto& image = *w.image;
    w.result.stage = "find_names";
    if (!names(f, w.n)) return {Outcome::Identity, w.result.stage};
    w.result.stage = "transient_package";
    if (!package_valid(f, w.access, w.n, r.package)) return {Outcome::Package, w.result.stage};
    if (!capture(w.access, r.package.klass, r.package_class)) return {Outcome::Identity, w.result.stage};
    w.result.stage = "holder_class";
    Object holder_class{}, metaclass{};
    void* klass = measured_native(perf::Metric::NativeStaticClass, f.end_image_class);
    if (!capture(w.access, klass, holder_class) || holder_class.name != w.n.holder
        || !capture(w.access, holder_class.klass, metaclass) || metaclass.name != w.n.klass)
        return {Outcome::Identity, w.result.stage};
    r.holder_class = holder_class;
    // Catalog-owned 0x40-byte ABI. Native construction publishes RootSet before
    // the class constructor; no raw UObject allocation or post-hoc rooting.
    alignas(8) std::array<std::uint8_t, 0x40> parameters{};
    std::memcpy(parameters.data(), &klass, sizeof(klass));
    std::memcpy(parameters.data() + 8, &r.package.pointer, sizeof(void*));
    const std::uint32_t public_flags = 0xc0;
    std::memcpy(parameters.data() + 0x18, &public_flags, sizeof(public_flags));
    w.result.stage = "construct_rooted_holder";
    if (!admitted(*w.authority) || !package_same(f, w.access, r.package))
        return {Outcome::Authority, w.result.stage};
    r.holder.pointer = measured_native(perf::Metric::NativeConstruct, f.construct, parameters.data());
    if (!admitted(*w.authority) || !package_same(f, w.access, r.package))
        return {Outcome::NativeFault, "post_holder_construction"};
    if (!capture(w.access, r.holder.pointer, r.holder) || !rooted(w.access, r.holder)
        || r.holder.klass != klass || r.holder.outer != r.package.pointer)
        return {Outcome::Holder, w.result.stage};
    void* slate = nullptr;
    void* owner = nullptr;
    void* resource = nullptr;
    if (!read(r.holder.pointer, 0x1d8, slate) || slate
        || !read(r.holder.pointer, 0x1e0, owner) || owner
        || !read(r.holder.pointer, 0x150, resource) || resource)
        return {Outcome::Holder, w.result.stage};
    w.result.stage = "construct_local_brush";
    auto* brush = r.local_brush.data();
    void* constructed = measured_native(perf::Metric::NativeBrushConstruct, f.brush_construct, brush);
    w.brush_constructed = true;
    if (constructed != brush) return {Outcome::Holder, w.result.stage};
    std::uint8_t uv = 1;
    if (!read(brush, 0x48, resource) || resource
        || !read(brush, 0x68, uv) || uv
        || !read(brush, 0x70, slate) || slate
        || !read(brush, 0x78, owner) || owner)
        return {Outcome::Holder, w.result.stage};
    w.result.stage = "create_and_bind_texture";
    if (!admitted(*w.authority) || !package_same(f, w.access, r.package)
        || !rooted(w.access, r.holder)) return {Outcome::Authority, w.result.stage};
    // Do not yield or publish before rooted retention; native listeners may
    // retire this scope, so recheck admission before further binding calls.
    r.texture.pointer = measured_native(perf::Metric::NativeTextureCreate, f.texture_create, static_cast<std::int32_t>(image.width),
        static_cast<std::int32_t>(image.height), 2, 0);
    if (!r.texture.pointer) return {Outcome::Memory, w.result.stage};
    w.result.stage = "post_texture_admission";
    if (!admitted(*w.authority) || !package_same(f, w.access, r.package)
        || !rooted(w.access, r.holder)) {
        w.cleanup_compromised = true;
        return {Outcome::NativeFault, w.result.stage};
    }
    measured_native(perf::Metric::NativeBrushSetResource, f.set_resource, brush, r.texture.pointer);
    w.result.stage = "post_resource_admission";
    if (!admitted(*w.authority) || !package_same(f, w.access, r.package)
        || !rooted(w.access, r.holder)) {
        w.cleanup_compromised = true;
        return {Outcome::NativeFault, w.result.stage};
    }
    measured_native(perf::Metric::NativeBrushCopy, f.set_brush, r.holder.pointer, brush);
    r.bound = true;
    w.result.stage = "post_holder_admission";
    if (!admitted(*w.authority)) return {Outcome::NativeFault, w.result.stage};
    w.result.stage = "validate_rooted_graph";
    Object texture_class{};
    if (!capture(w.access, r.texture.pointer, r.texture)
        || !capture(w.access, r.texture.klass, texture_class) || texture_class.name != w.n.texture
        || !capture(w.access, texture_class.klass, metaclass) || metaclass.name != w.n.klass)
        return {Outcome::Identity, w.result.stage};
    r.texture_class = texture_class;
    if (!admitted(*w.authority) || !graph_valid(f, w.access, r))
        return {Outcome::NativeFault, w.result.stage};
    w.result.stage = "fresh_mip";
    detail::FreshMip before{}, after{};
    if (!read_mip(r.texture.pointer, before, &w.result))
        return {Outcome::FreshLayout, w.result.stage};
    if (const auto failure = detail::fresh_mip_failure(before, image.width, image.height, image.bgra.size()))
        return {Outcome::FreshLayout, failure};
    w.result.stage = "materialize_mip";
    w.bulk = reinterpret_cast<void*>(before.mip + 0x10);
    measured_native(perf::Metric::NativeMaterialize, f.materialize, w.bulk);
    w.result.stage = "post_materialize_admission";
    if (!admitted(*w.authority) || !graph_valid(f, w.access, r))
        return {Outcome::NativeFault, w.result.stage};
    w.result.stage = "materialize_mip_recheck";
    if (!read_mip(r.texture.pointer, after, &w.result))
        return {Outcome::FreshLayout, w.result.stage};
    if (const auto failure = detail::fresh_mip_failure(after, image.width, image.height, image.bgra.size()))
        return {Outcome::FreshLayout, failure};
    if (after.platform != before.platform || after.array != before.array || after.mip != before.mip
        || after.capacity != before.capacity)
        return {Outcome::FreshLayout, "materialize_mip_identity"};
    w.result.stage = "copy_pixels";
    const std::uint8_t lock = 2;
    if (!core::safe_write_field(reinterpret_cast<void*>(after.mip), 0x34, lock))
        return {Outcome::Memory, w.result.stage};
    w.locked = true;
    if (!core::safe_write_bytes(reinterpret_cast<void*>(after.data), image.bgra.data(), image.bgra.size()))
        return {Outcome::Memory, w.result.stage};
    w.result.stage = "unlock_mip";
    w.locked = false;
    measured_native(perf::Metric::NativeUnlock, f.unlock, w.bulk); // an attempted unlock is never retried after an SEH fault
    w.bulk = nullptr; // CPU pointer must not escape Unlock.
    w.result.stage = "mark_not_offline_processed";
    if (!admitted(*w.authority) || !graph_valid(f, w.access, r))
        return {Outcome::Authority, w.result.stage};
    measured_native(perf::Metric::NativeTextureMark, f.mark_not_offline_processed, r.texture.pointer);
    w.result.stage = "post_texture_mark_admission";
    if (!admitted(*w.authority) || !graph_valid(f, w.access, r))
        return {Outcome::NativeFault, w.result.stage};
    w.result.stage = "dispatch_upload";
    measured_native(perf::Metric::NativeTextureUpload, f.update, r.texture.pointer); // exactly once; immutable after this dispatch
    w.result.stage = "publish_owned_image";
    if (!admitted(*w.authority) || !graph_valid(f, w.access, r))
        return {Outcome::Authority, w.result.stage};
    return {Outcome::Ready, w.result.stage};
}

Result guarded_sequence(Work& w) noexcept
{
    __try { return sequence(w); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return {Outcome::NativeFault, w.result.stage}; }
}
bool cleanup(Work& w) noexcept
{
    // Only synchronous callback cleanup, NEVER a static destructor/shutdown call.
    if (w.cleanup_compromised) return false;
    if (!w.locked && !w.brush_constructed) return true;
    w.result.stage = "cleanup_admission";
    if (!admitted(*w.authority)
        || (w.record->bound && !graph_valid(*w.f, w.access, *w.record))) return false;
    __try {
        if (w.locked) {
            w.result.stage = "cleanup_unlock";
            w.locked = false;
            measured_native(perf::Metric::NativeUnlock, w.f->unlock, w.bulk);
            w.bulk = nullptr;
            w.result.stage = "post_cleanup_unlock_admission";
            if (!admitted(*w.authority)
                || (w.record->bound && !graph_valid(*w.f, w.access, *w.record))) return false;
        }
        if (w.brush_constructed) {
            w.result.stage = "destroy_local_brush";
            measured_native(perf::Metric::NativeBrushDestroy, w.f->brush_destroy, w.record->local_brush.data());
            w.brush_constructed = false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool mapped(HMODULE module, std::uintptr_t rva_value, std::size_t bytes, bool executable) noexcept
{
    const auto image = core::image_range(module);
    if (!image.base || !rva_value || rva_value >= image.size || bytes > image.size - rva_value) return false;
    MEMORY_BASIC_INFORMATION memory{};
    const auto pointer = image.base + rva_value;
    if (!VirtualQuery(pointer, &memory, sizeof(memory)) || memory.AllocationBase != module
        || memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE
        || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto region_end = reinterpret_cast<Address>(memory.BaseAddress) + memory.RegionSize;
    if (reinterpret_cast<Address>(pointer) > region_end || bytes > region_end - reinterpret_cast<Address>(pointer))
        return false;
    const auto protection = memory.Protect & 0xff;
    return executable
        ? protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ
            || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY
        : protection == PAGE_READWRITE || protection == PAGE_WRITECOPY;
}

bool signature(HMODULE module, const char* id, std::uintptr_t rva_value) noexcept
{
    const auto* spec = find_rva_signature(id);
    return spec && spec->rva == rva_value && !spec->expected_prologue.empty()
        && mapped(module, rva_value, spec->expected_prologue.size(), true)
        && core::bytes_equal(reinterpret_cast<const std::uint8_t*>(module) + rva_value,
            spec->expected_prologue);
}

bool class_reference_target(Address site, Address slot, std::int32_t displacement) noexcept
{
    return site && slot && site <= std::numeric_limits<std::uint32_t>::max()
        && slot <= std::numeric_limits<std::uint32_t>::max()
        && static_cast<std::int64_t>(site) + 20 + displacement == static_cast<std::int64_t>(slot);
}
bool panel_class_reference(HMODULE module, const char* id, Address site, Address slot) noexcept
{
    const auto* spec = find_rva_signature(id);
    std::int32_t displacement = 0;
    return mapped(module, slot, sizeof(void*), false)
        && signature(module, id, site) && spec && spec->expected_prologue.size() >= 20
        && read(reinterpret_cast<std::uint8_t*>(module) + site, 16, displacement)
        && class_reference_target(site, slot, displacement);
}

bool ancestry_contract(HMODULE module) noexcept
{
    // Full recovered noncallable predicate pins the address comparison and
    // signed indices. Never invoke this code or silently substitute a getter.
    constexpr std::uint8_t predicate[] = {
        0x48,0x85,0xd2,0x74,0x1d,0x48,0x63,0x42,0x38,0x4c,0x8d,0x42,0x30,
        0x3b,0x41,0x38,0x7f,0x10,0x48,0x8b,0xd0,0x48,0x8b,0x41,0x30,
        0x4c,0x39,0x04,0xd0,0x75,0x03,0xb0,0x01,0xc3,0x32,0xc0,0xc3};
    const auto* spec = find_rva_signature("native_class_ancestry_site");
    return spec && spec->expected_prologue.size() == sizeof(predicate)
        && std::memcmp(spec->expected_prologue.data(), predicate, sizeof(predicate)) == 0
        && signature(module, "native_class_ancestry_site", rva::NativeClassAncestrySite);
}

bool cursor_owner_valid(const objects::UObjectIdentityAccess&, const CallbackAuthority&, const Target&, bool) noexcept;
bool owner_valid(const objects::UObjectIdentityAccess& access,
    const CallbackAuthority& a, const Target& t, bool facts_only = false) noexcept
{
    if (!facts_only && read_phase && !read_phase->checking_owners && shared_owner(t)) return admitted(a);
    void* tree = nullptr;
    void* root = nullptr;
    return authority_current(a, facts_only) && t.generation == a.session.generation
        && t.receiver.live.serial_number > 0
        && (t.cursor_owner ? cursor_owner_valid(access, a, t, facts_only)
            : t.row_id ? row_owner_valid(access, a, t, facts_only)
            : t.receiver.pointer == a.session.widget
                && t.receiver.live.internal_index == a.session.widget_identity.internal_index
                && t.receiver.live.serial_number == a.session.widget_identity.serial_number)
        && unchanged(access, t.receiver)
        && read(t.receiver.pointer, 0x1d8, tree) && tree == t.tree.pointer
        && unchanged(access, t.tree) && t.tree.outer == t.receiver.pointer
        && unchanged(access, t.tree_class)
        && (!t.generic || generic_owner_type_valid(access, t))
        && read(t.tree.pointer, 0x28, root) && root == t.root.pointer
        && unchanged(access, t.root);
    // Zero tree/root serials are diagnostic membership only. These current
    // strong forward edges explain their retention, NOT target containment.
}

bool target_basic_valid(const objects::UObjectIdentityAccess& access,
    const CallbackAuthority& a, const Target& t, bool facts_only = false) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::ValidateTargetBasic);
    perf::count(perf::Counter::TargetBasicChecks);
    const bool result = [&]() noexcept {
    std::uint32_t flags = 0;
    std::int32_t item_flags = 0;
    // Membership is checked before metadata; zero endpoints are authorized ONLY
    // when the caller also completes the fresh strong containment walk below.
    return owner_valid(access, a, t, facts_only)
        && unchanged(access, t.target) && unchanged(access, t.target_class)
        && read(t.target.pointer, 8, flags) && flags == t.target_flags
        && load_item_flags(access, t.target, item_flags) && item_flags == t.target_item_flags
        && t.target.klass == t.target_class.pointer && t.target.outer == t.tree.pointer
        && (!t.generic || (generic_type_valid(access, t) && member_valid(t)));
    }();
    perf::finish(stamp, result);
    return result;
}

bool panel_bases(const objects::UObjectIdentityAccess& access, const Target& t,
    PanelBases& b) noexcept
{
    void* panel = nullptr;
    void* slot = nullptr;
    return read(state.functions.panel_class_slot, 0, panel) && panel
        && read(state.functions.panel_slot_class_slot, 0, slot) && slot
        && capture(access, panel, b.panel) && capture(access, slot, b.slot)
        && b.panel.name == t.panel_name && b.slot.name == t.slot_name
        && (t.generic || b.panel.klass == t.target_class.klass) && b.slot.klass == b.panel.klass
        && capture(access, b.panel.klass, b.meta) && b.meta.name == t.class_name
        && b.meta.klass == b.meta.pointer
        && capture(access, b.panel.outer, b.package) && b.slot.outer == b.package.pointer
        && b.package.name == t.umg_name && !b.package.outer
        && capture(access, b.package.klass, b.package_class)
        && b.package_class.name == t.package_name && b.package_class.klass == b.meta.pointer
        && capture(access, b.package_class.outer, b.core_package)
        && b.core_package.name == t.core_package_name && !b.core_package.outer
        && b.core_package.klass == b.package_class.pointer && b.meta.outer == b.core_package.pointer;
}

bool bases_unchanged(const objects::UObjectIdentityAccess& access, const PanelBases& b) noexcept
{
    void* panel = nullptr;
    void* slot = nullptr;
    return read(state.functions.panel_class_slot, 0, panel) && panel == b.panel.pointer
        && read(state.functions.panel_slot_class_slot, 0, slot) && slot == b.slot.pointer
        && unchanged(access, b.panel) && unchanged(access, b.slot) && unchanged(access, b.meta)
        && unchanged(access, b.package) && unchanged(access, b.package_class)
        && unchanged(access, b.core_package);
}

bool fast_chain(void* actual, void* base, FastChain& out) noexcept
{
    out.base = base;
    out.actual = actual;
    if (!read(base, 0x38, out.base_index) || out.base_index < 0
        || out.base_index > kAncestryIndex
        || !read(actual, 0x38, out.actual_last)
        || out.actual_last < out.base_index || out.actual_last > kAncestryIndex
        || !read(base, 0x30, out.base_array) || !out.base_array
        || !read(actual, 0x30, out.actual_array) || !out.actual_array) return false;
    const auto offset = static_cast<Address>(out.base_index) * sizeof(Address);
    if (out.actual_array > std::numeric_limits<Address>::max() - offset - sizeof(Address)
        || reinterpret_cast<Address>(base) > std::numeric_limits<Address>::max() - 0x30)
        return false;
    return read(reinterpret_cast<void*>(out.actual_array), offset, out.selected)
        && out.selected == reinterpret_cast<Address>(base) + 0x30;
}

bool chain_unchanged(const FastChain& a) noexcept
{
    std::int32_t base_index = -1, last = -1;
    Address base_array = 0, array = 0, selected = 0;
    return read(a.base, 0x38, base_index) && base_index == a.base_index
        && read(a.base, 0x30, base_array) && base_array == a.base_array
        && read(a.actual, 0x38, last) && last == a.actual_last
        && read(a.actual, 0x30, array) && array == a.actual_array
        && read(reinterpret_cast<void*>(array), static_cast<Address>(base_index) * sizeof(Address), selected)
        && selected == a.selected;
}

bool ancestry(const objects::UObjectIdentityAccess& access, const Object& object,
    const Object& base, const PanelBases& b, Ancestry& out) noexcept
{
    if (!capture(access, object.klass, out.actual)) return false;
    if (out.actual.klass != b.meta.pointer) {
        // A BlueprintGeneratedClass may describe a real PanelWidget subclass.
        // Authenticate its metaclass as a UClass descendant with the SAME bounded
        // predicate, rather than accepting an arbitrary class-named UObject.
        if (!capture(access, out.actual.klass, out.metaclass)
            || out.metaclass.klass != b.meta.pointer
            || !fast_chain(out.metaclass.pointer, b.meta.pointer, out.meta_chain)) return false;
    }
    return fast_chain(out.actual.pointer, base.pointer, out.chain);
}
bool ancestry_unchanged(const objects::UObjectIdentityAccess& access, const Ancestry& a) noexcept
{
    return unchanged(access, a.actual) && chain_unchanged(a.chain)
        && (!a.metaclass.pointer || (unchanged(access, a.metaclass) && chain_unchanged(a.meta_chain)));
}

bool edge_unchanged(const objects::UObjectIdentityAccess& access, const ContainmentEdge& e) noexcept
{
    void* slot = nullptr;
    void* parent = nullptr;
    void* content = nullptr;
    void* slots = nullptr;
    void* entry = nullptr;
    std::int32_t count = 0, capacity = 0;
    return unchanged(access, e.child) && unchanged(access, e.slot) && unchanged(access, e.parent)
        && ancestry_unchanged(access, e.slot_type) && ancestry_unchanged(access, e.parent_type)
        && read(e.child.pointer, 0x28, slot) && slot == e.slot.pointer
        && read(e.slot.pointer, 0x28, parent) && parent == e.parent.pointer
        && read(e.slot.pointer, 0x30, content) && content == e.child.pointer
        && read(e.parent.pointer, 0x108, slots) && slots == e.slots
        && read(e.parent.pointer, 0x110, count) && count == e.count
        && read(e.parent.pointer, 0x114, capacity) && capacity == e.capacity
        && read(slots, static_cast<Address>(e.index) * sizeof(void*), entry) && entry == slot;
}

#ifdef FF7RP_MENU_THEME_NATIVE_SELFTEST
std::size_t target_validation_calls = 0;
#endif
bool target_valid_sequence(const objects::UObjectIdentityAccess& access,
    const CallbackAuthority& a, const Target& t, bool facts_only = false) noexcept
{
    const bool result = [&]() noexcept {
#ifdef FF7RP_MENU_THEME_NATIVE_SELFTEST
    ++target_validation_calls;
#endif
    if (!target_basic_valid(access, a, t, facts_only)) return false;
    PanelBases bases{};
    if (!panel_bases(access, t, bases)) return false;
    std::array<ContainmentEdge, kContainmentDepth> path{};
    std::size_t count = 0;
    Object child = t.target;
    while (child.pointer != t.root.pointer) {
        perf::count(perf::Counter::TreeNodes);
        if (count == path.size()) return false;
        for (std::size_t i = 0; i < count; ++i)
            if (path[i].child.pointer == child.pointer) return false;
        auto& e = path[count++];
        e.child = child;
        void* slot = nullptr;
        void* parent = nullptr;
        void* content = nullptr;
        if (!unchanged(access, child) || !read(child.pointer, 0x28, slot)
            || !capture(access, slot, e.slot) || !ancestry(access, e.slot, bases.slot, bases, e.slot_type)
            || !read(slot, 0x28, parent) || !capture(access, parent, e.parent)
            || !ancestry(access, e.parent, bases.panel, bases, e.parent_type)
            || !read(slot, 0x30, content) || content != child.pointer
            || !read(parent, 0x108, e.slots) || !e.slots
            || !read(parent, 0x110, e.count) || e.count <= 0 || e.count > kPanelSlots
            || !read(parent, 0x114, e.capacity) || e.capacity < e.count || e.capacity > kPanelSlots
            || reinterpret_cast<Address>(e.slots) > std::numeric_limits<Address>::max()
                - static_cast<Address>(e.count) * sizeof(void*)) return false;
        for (std::int32_t i = 0; i < e.count; ++i) {
            void* entry = nullptr;
            if (!read(e.slots, static_cast<Address>(i) * sizeof(void*), entry)) return false;
            if (entry == slot) { e.index = i; break; }
        }
        if (e.index < 0) return false;
        child = e.parent;
    }
    // Fresh stack-only path: forward strong RootWidget/Slots/Content edges,
    // NOT ancillary zero-serial leases, explain retention in the qualified scope.
    for (std::size_t i = 0; i < count; ++i)
        if (!edge_unchanged(access, path[i])) return false;
    if (!bases_unchanged(access, bases) || !target_basic_valid(access, a, t, facts_only)) return false;
    if (!facts_only && read_phase && read_phase->classifying) {
        auto& phase = *read_phase;
        // Overflow falls back to uncached classification, never a partial path.
        phase.witnessed = phase.target_count < phase.targets.size()
            && count <= phase.edges.size() - phase.edge_count && phase.base_count < phase.bases.size();
        if (phase.witnessed) {
            for (std::size_t i = 0; i < count; ++i) phase.edges[phase.edge_count++] = path[i];
            phase.bases[phase.base_count++] = bases;
        }
    }
    return true;
    }();
    return result;
}

bool target_valid(const objects::UObjectIdentityAccess& access,
    const CallbackAuthority& a, const Target& t, bool facts_only = false) noexcept
{
    if (!facts_only && read_phase && read_phase->authority == &a) return phase_target_valid(access, a, t);
    // A nested facts-only validator may not leave a surrounding batch proof alive.
    reject_read_phase();
    // Only this allocation/native-call-free classifier owns the proof; nesting revokes its parent.
    auto* previous = classifier_phase;
    revoke_classifier();
    ClassifierPhase phase{facts_only ? nullptr : &a};
    classifier_phase = &phase;
    bool result = false;
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::ValidateTarget);
    __try {
        const bool classified = target_valid_sequence(access, a, t, facts_only);
        result = classified && (facts_only || !a.phase_current || (!phase.revoked && admitted(a)));
    } __finally {
        phase.revoked = true;
        classifier_phase = previous;
        perf::finish(stamp, result);
    }
    return result;
}

struct ClassWitness { Object klass{}, meta{}, package{}, package_class{}, core{}; void* slot = nullptr; };
NativeClass native_class(SubjectKind kind) noexcept
{
    return static_cast<NativeClass>(static_cast<std::size_t>(NativeClass::Image) + static_cast<std::size_t>(kind));
}
void generic_witness_names(Target& t) noexcept
{
    const auto& n = state.generic_names;
    t.generic = true;
    t.panel_name = n.panel; t.slot_name = n.slot; t.umg_name = n.umg;
    t.class_name = n.meta; t.package_name = n.package; t.core_package_name = n.core;
}
bool published_class(const objects::UObjectIdentityAccess& access, NativeClass id, ClassWitness& w) noexcept
{
    const auto i = static_cast<std::size_t>(id);
    if (!state.generic.classes || !state.generic_names.ready || i >= kClassCount) return false;
    const auto& n = state.generic_names;
    void* pointer = nullptr;
    w.slot = state.generic.slots[i];
    return read(w.slot, 0, pointer) && pointer && capture(access, pointer, w.klass)
        && w.klass.name == n.classes[i]
        && capture(access, w.klass.klass, w.meta) && w.meta.name == n.meta && w.meta.klass == w.meta.pointer
        && capture(access, w.klass.outer, w.package)
        && w.package.name == (class_specs[i].umg ? n.umg : n.game) && !w.package.outer
        && capture(access, w.package.klass, w.package_class)
        && w.package_class.name == n.package && w.package_class.klass == w.meta.pointer
        && capture(access, w.package_class.outer, w.core) && w.core.name == n.core && !w.core.outer
        && w.core.klass == w.package_class.pointer && w.meta.outer == w.core.pointer;
}
bool class_unchanged(const objects::UObjectIdentityAccess& access, const ClassWitness& w) noexcept
{
    void* pointer = nullptr;
    return read(w.slot, 0, pointer) && pointer == w.klass.pointer && unchanged(access, w.klass)
        && unchanged(access, w.meta) && unchanged(access, w.package)
        && unchanged(access, w.package_class) && unchanged(access, w.core);
}
bool typed_object(const objects::UObjectIdentityAccess& access, const Object& object, NativeClass id) noexcept
{
    ClassWitness w{};
    Target names{}; generic_witness_names(names);
    PanelBases bases{}; Ancestry type{};
    return published_class(access, id, w) && panel_bases(access, names, bases)
        && ancestry(access, object, w.klass, bases, type)
        && unchanged(access, object) && ancestry_unchanged(access, type)
        && bases_unchanged(access, bases) && class_unchanged(access, w);
}
bool generic_type_valid(const objects::UObjectIdentityAccess& access, const Target& t) noexcept
{
    return static_cast<unsigned>(t.kind) <= static_cast<unsigned>(SubjectKind::CanvasPanel)
        && typed_object(access, t.target, native_class(t.kind));
}
bool generic_owner_type_valid(const objects::UObjectIdentityAccess& access, const Target& t) noexcept
{
    return typed_object(access, t.receiver, NativeClass::UserWidget)
        && typed_object(access, t.tree, NativeClass::WidgetTree);
}
bool main_identity(const objects::UObjectIdentityAccess& access, const CallbackAuthority& a, Object& out) noexcept
{
    return admitted(a) && capture(access, a.session.widget, out) && out.live.serial_number > 0
        && out.live.internal_index == a.session.widget_identity.internal_index
        && out.live.serial_number == a.session.widget_identity.serial_number;
}
Result find_generic_name(const wchar_t* text, std::uint64_t& value) noexcept
{
    __try { measured_native(perf::Metric::NativeFindName, state.functions.fname, &value, text, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return {Outcome::NativeFault, "subject_name_native_fault"}; }
    return static_cast<std::uint32_t>(value) ? Result{Outcome::Ready, "subject_name"}
        : Result{Outcome::Unavailable, "subject_name_not_published"};
}
Result ensure_generic_names(const objects::UObjectIdentityAccess& access, const CallbackAuthority& a) noexcept
{
    if (!state.generic.classes) return {Outcome::Unavailable, "subject_classes"};
    if (state.generic_names.ready) return {Outcome::Ready, "subject_names"};
    Object main{};
    if (!main_identity(access, a, main)) return {Outcome::Identity, "subject_main_owner"};
    GenericNames n{};
    const auto find = [&](const wchar_t* text, std::uint64_t& value) noexcept -> Result {
        if (!admitted(a) || !unchanged(access, main)) return {Outcome::NativeFault, "pre_subject_name_admission"};
        const auto result = find_generic_name(text, value);
        if (!admitted(a) || !unchanged(access, main)) return {Outcome::NativeFault, "post_subject_name_admission"};
        return result;
    };
    for (std::size_t i = 0; i < kClassCount; ++i) {
        const auto result = find(class_specs[i].name, n.classes[i]);
        if (!result) return result;
    }
    const wchar_t* texts[] = {L"Class", L"Package", L"/Script/CoreUObject", L"/Script/UMG", L"/Script/EndGame", L"PanelWidget", L"PanelSlot"};
    std::uint64_t* values[] = {&n.meta, &n.package, &n.core, &n.umg, &n.game, &n.panel, &n.slot};
    for (std::size_t i = 0; i < std::size(texts); ++i) {
        const auto result = find(texts[i], *values[i]);
        if (!result) return result;
    }
    n.ready = true;
    state.generic_names = n;
    return {Outcome::Ready, "subject_names"};
}
struct PointerArray { void* data = nullptr; std::int32_t count = 0, capacity = 0; };
bool pointer_array(void* owner, Address offset, PointerArray& out) noexcept
{
    return read(owner, offset, out.data) && read(owner, offset + 8, out.count)
        && read(owner, offset + 12, out.capacity) && out.count >= 0 && out.capacity >= out.count
        && out.capacity <= 256 && (!out.count || out.data)
        && reinterpret_cast<Address>(out.data) <= std::numeric_limits<Address>::max()
            - static_cast<Address>(out.count) * sizeof(void*);
}
bool same_array(void* owner, Address offset, const PointerArray& expected) noexcept
{
    PointerArray now{};
    return pointer_array(owner, offset, now) && now.data == expected.data
        && now.count == expected.count && now.capacity == expected.capacity;
}
bool current_list(const objects::UObjectIdentityAccess& access, void* main, RowKind kind, Object& out) noexcept
{
    const Address offset = kind == RowKind::Song ? 0x3c8 : 0x410;
    objects::UObjectLiveHandle weak{};
    Address pointer = 0;
    return read(main, offset, weak.internal_index) && read(main, offset + 4, weak.serial_number)
        && weak.internal_index >= 0 && weak.serial_number > 0
        && objects::read_uobject_item_pointer(access, weak.internal_index, pointer)
        && capture(access, reinterpret_cast<void*>(pointer), out)
        && out.live.internal_index == weak.internal_index && out.live.serial_number == weak.serial_number
        && typed_object(access, out, kind == RowKind::Song ? NativeClass::SongList : NativeClass::SpeedList);
}
bool speed_tree(const objects::UObjectIdentityAccess& access, const Object& row,
    Object& tree, Object& root) noexcept
{
    void* pointer = nullptr; void* again = nullptr;
    return row.live.serial_number > 0 && unchanged(access, row)
        && read(row.pointer, 0x1d8, pointer) && capture(access, pointer, tree)
        && tree.outer == row.pointer && typed_object(access, tree, NativeClass::WidgetTree)
        && read(tree.pointer, 0x28, pointer) && capture(access, pointer, root)
        && root.outer == tree.pointer && unchanged(access, row) && unchanged(access, tree)
        && unchanged(access, root) && read(row.pointer, 0x1d8, again) && again == tree.pointer
        && read(tree.pointer, 0x28, again) && again == root.pointer;
}
bool cursor_owner_valid(const objects::UObjectIdentityAccess& access, const CallbackAuthority& a,
    const Target& t, bool facts_only) noexcept
{
    Object list{}, again{}; void* cursor = nullptr; void* reread = nullptr;
    const auto& main = t.cursor_main;
    return !t.row_id && authority_current(a, facts_only)
        && main.pointer == a.session.widget && main.live.serial_number > 0
        && main.live.internal_index == a.session.widget_identity.internal_index
        && main.live.serial_number == a.session.widget_identity.serial_number
        && unchanged(access, main) && typed_object(access, main, NativeClass::UserWidget)
        && current_list(access, main.pointer, RowKind::Song, list)
        && list.pointer == t.cursor_list.pointer && unchanged(access, t.cursor_list)
        && list.live.internal_index == t.cursor_list.live.internal_index
        && list.live.serial_number == t.cursor_list.live.serial_number
        && read(list.pointer, 0x8f8, cursor) && cursor == t.receiver.pointer
        && t.receiver.live.serial_number > 0 && unchanged(access, t.receiver)
        && current_list(access, main.pointer, RowKind::Song, again)
        && again.pointer == list.pointer && unchanged(access, list)
        && read(list.pointer, 0x8f8, reread) && reread == cursor
        && unchanged(access, main) && authority_current(a, facts_only);
}
bool row_member(const objects::UObjectIdentityAccess& access, const RowBinding& r) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::RowMembership);
    perf::count(perf::Counter::RowMembershipChecks);
    const bool result = [&]() noexcept {
    const Address offset = r.association.kind == RowKind::Song ? 0x8d8 : 0x188;
    PointerArray array{}; std::int32_t position = -1;
    if (!pointer_array(r.list.pointer, offset, array)) return false;
    const auto speed_valid = [&]() noexcept {
        if (!r.association.membership_epoch) return true;
        Object tree{}, root{};
        return array.data == r.speed_members && array.count == r.speed_count && array.capacity == r.speed_capacity
            && speed_tree(access, r.row, tree, root) && tree.pointer == r.speed_tree.pointer
            && root.pointer == r.speed_root.pointer && unchanged(access, r.speed_tree)
            && unchanged(access, r.speed_root);
    };
    if (!speed_valid()) return false;
    for (std::int32_t i = 0; i < array.count; ++i) {
        perf::count(perf::Counter::RowMembershipEntries);
        void* pointer = nullptr;
        if (!read(array.data, static_cast<Address>(i) * sizeof(void*), pointer)) return false;
        if (pointer == r.row.pointer) { if (position >= 0) return false; position = i; }
    }
    void* again = nullptr;
    return position >= 0 && unchanged(access, r.list) && unchanged(access, r.row)
        && typed_object(access, r.row, NativeClass::UserWidget)
        && same_array(r.list.pointer, offset, array)
        && read(array.data, static_cast<Address>(position) * sizeof(void*), again) && again == r.row.pointer
        && speed_valid() && same_array(r.list.pointer, offset, array);
    }();
    perf::finish(stamp, result);
    return result;
}
const RowBinding* row_binding(std::uint64_t id) noexcept
{
    if (state.rows) for (const auto& r : *state.rows) if (r.id == id && id) return &r;
    return nullptr;
}
bool row_owner_valid(const objects::UObjectIdentityAccess& access, const CallbackAuthority& a,
    const Target& t, bool facts_only) noexcept
{
    const auto* r = row_binding(t.row_id);
    Object list{}, recheck{};
    return r && authority_current(a, facts_only) && r->association.session_generation == a.session.generation
        && r->main.pointer == a.session.widget && r->main.live.serial_number > 0
        && r->main.live.internal_index == a.session.widget_identity.internal_index
        && r->main.live.serial_number == a.session.widget_identity.serial_number
        && unchanged(access, r->main) && r->row.pointer == t.receiver.pointer
        && r->row.live.serial_number > 0 && unchanged(access, r->row)
        && t.receiver.live.internal_index == r->row.live.internal_index
        && t.receiver.live.serial_number == r->row.live.serial_number
        && current_list(access, r->main.pointer, r->association.kind, list)
        && list.pointer == r->list.pointer && unchanged(access, r->list) && row_member(access, *r)
        && current_list(access, r->main.pointer, r->association.kind, recheck)
        && recheck.pointer == list.pointer && authority_current(a, facts_only);
}
void* target_brush(const Target& t) noexcept
{
    if (!t.brush_selected) return nullptr;
    if (t.generic && t.kind == SubjectKind::ImageSet)
        return reinterpret_cast<void*>(t.images + static_cast<Address>(t.image_index) * 0xb0);
    return static_cast<std::uint8_t*>(t.target.pointer) + t.brush_offset;
}
bool member_valid(const Target& t) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::MemberValidation);
    const bool result = [&]() noexcept {
    if (!t.brush_selected) return true;
    if (t.kind != SubjectKind::Image && !state.generic.styles) return false;
    if (t.kind != SubjectKind::ImageSet) return target_brush(t) != nullptr;
    void* data = nullptr; std::int32_t count = 0, capacity = 0;
    return read(t.target.pointer, 0x108, data) && reinterpret_cast<Address>(data) == t.images
        && read(t.target.pointer, 0x110, count) && count == t.image_count
        && read(t.target.pointer, 0x114, capacity) && capacity == t.image_capacity
        && t.image_index >= 0 && t.image_index < count && capacity >= count && capacity <= 256
        && t.images && t.images <= std::numeric_limits<Address>::max() - static_cast<Address>(capacity) * 0xb0;
    }();
    perf::finish(stamp, result);
    return result;
}
bool select_member(Target& t, BrushSelection selection) noexcept
{
    t.brush_selected = true;
    switch (t.kind) {
    case SubjectKind::Image: t.brush_offset = 0x108; return selection.index == 0;
    case SubjectKind::PieImage: t.brush_offset = 0x108; return selection.index == 0 && state.generic.styles;
    case SubjectKind::TiledImage: t.brush_offset = 0x110; return selection.index == 0 && state.generic.styles;
    case SubjectKind::Button: {
        constexpr Address offsets[] = {0x128, 0x1b0, 0x238, 0x2c0};
        const auto i = static_cast<unsigned>(selection.button);
        if (i >= 4 || selection.index != 0 || !state.generic.styles) return false;
        t.brush_offset = offsets[i]; return true;
    }
    case SubjectKind::ImageSet: {
        void* data = nullptr;
        if (!state.generic.styles || !read(t.target.pointer, 0x108, data)
            || !read(t.target.pointer, 0x110, t.image_count)
            || !read(t.target.pointer, 0x114, t.image_capacity)) return false;
        t.images = reinterpret_cast<Address>(data); t.image_index = selection.index;
        return member_valid(t);
    }
    default: return false;
    }
}

using BrushFacts = std::array<std::uint8_t, 0x88>;
bool read_brush(const void* brush, BrushFacts& out) noexcept
{
    return brush && core::safe_copy_bytes(brush, out.data(), out.size());
}
bool brush_facts(void* owner, BrushFacts& out) noexcept
{
    return core::safe_copy_bytes(static_cast<std::uint8_t*>(owner) + 0x108, out.data(), out.size());
}
bool same_object(const Object& a, const Object& b) noexcept
{
    return a.pointer == b.pointer && a.klass == b.klass && a.outer == b.outer && a.name == b.name
        && a.live.internal_index == b.live.internal_index && a.live.serial_number == b.live.serial_number
        && a.zero.internal_index == b.zero.internal_index && a.zero.serial_number == b.zero.serial_number;
}
bool same_target(const Target& a, const Target& b) noexcept
{
    const auto& x = a.target;
    const auto& y = b.target;
    return a.generation == b.generation && same_object(a.receiver, b.receiver)
        && same_object(a.tree, b.tree) && same_object(a.root, b.root)
        && same_object(a.tree_class, b.tree_class) && same_object(a.target_class, b.target_class)
        && x.pointer == y.pointer && x.klass == y.klass && x.name == y.name && x.outer == y.outer
        && x.live.internal_index == y.live.internal_index && x.live.serial_number == y.live.serial_number
        && x.zero.internal_index == y.zero.internal_index && x.zero.serial_number == y.zero.serial_number
        && a.target_flags == b.target_flags && a.target_item_flags == b.target_item_flags
        && a.generic == b.generic && a.kind == b.kind && a.row_id == b.row_id
        && a.cursor_owner == b.cursor_owner && same_object(a.cursor_main, b.cursor_main)
        && same_object(a.cursor_list, b.cursor_list)
        && a.brush_selected == b.brush_selected && a.brush_offset == b.brush_offset
        && a.images == b.images && a.image_count == b.image_count && a.image_capacity == b.image_capacity
        && a.image_index == b.image_index;
}
bool same_owner(const Target& a, const Target& b) noexcept
{
    return a.generation == b.generation && a.row_id == b.row_id && a.cursor_owner == b.cursor_owner
        && a.generic == b.generic && same_object(a.receiver, b.receiver)
        && same_object(a.tree, b.tree) && same_object(a.tree_class, b.tree_class)
        && same_object(a.root, b.root) && same_object(a.cursor_main, b.cursor_main)
        && same_object(a.cursor_list, b.cursor_list)
        && a.panel_name == b.panel_name && a.slot_name == b.slot_name && a.umg_name == b.umg_name
        && a.class_name == b.class_name && a.package_name == b.package_name
        && a.core_package_name == b.core_package_name;
}
bool shared_owner(const Target& t) noexcept
{
    if (!read_phase || read_phase->reset || read_phase->rejected) return false;
    for (std::size_t i = 0; i < read_phase->target_count; ++i)
        if (same_owner(read_phase->targets[i], t)) return true;
    return false;
}
bool same_subject(const Target& a, const Target& b) noexcept
{
    // Strip member selection only; all identity/type/owner/array witnesses stay exact.
    Target x = a, y = b;
    x.brush_selected = y.brush_selected = false;
    x.brush_offset = y.brush_offset = 0; x.image_index = y.image_index = 0;
    return same_target(x, y) && same_owner(a, b);
}
bool phase_target_valid(const objects::UObjectIdentityAccess& access,
    const CallbackAuthority& a, const Target& t) noexcept
{
    auto* phase = read_phase;
    if (phase->classifying || phase->checking_owners || phase->reading_graph) { reject_read_phase(); return false; }
    if (!admitted(a)) return false;
    for (std::size_t i = 0; i < phase->target_count; ++i) {
        if (same_subject(phase->targets[i], t)) {
            const bool valid = (!t.generic || member_valid(t)) && admitted(a);
            if (!valid) reject_read_phase();
            return valid;
        }
    }
    bool result = false;
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::ValidateTarget);
    phase->classifying = true;
    phase->witnessed = false;
    __try {
        result = target_valid_sequence(access, a, t, false) && admitted(a);
        if (result && phase->witnessed) phase->targets[phase->target_count++] = t;
    } __finally {
        phase->classifying = false;
        if (!result) reject_read_phase();
        perf::finish(stamp, result);
    }
    return result;
}
bool finish_reads(const objects::UObjectIdentityAccess& access, const CallbackAuthority& a) noexcept
{
    if (!admitted(a)) return false;
    if (!read_phase) return true; // Null guard never shared any proof.
    auto& phase = *read_phase;
    phase.checking_owners = true;
    bool valid = true;
    for (std::size_t i = 0; i < phase.base_count && valid; ++i) valid = bases_unchanged(access, phase.bases[i]);
    for (std::size_t i = 0; i < phase.edge_count && valid; ++i) valid = edge_unchanged(access, phase.edges[i]);
    for (std::size_t i = 0; i < phase.target_count && valid; ++i) {
        const auto& t = phase.targets[i];
        bool duplicate = false;
        for (std::size_t j = 0; j < i; ++j) if (same_owner(phase.targets[j], t)) duplicate = true;
        std::uint32_t flags = 0; std::int32_t item_flags = 0;
        valid = (duplicate || owner_valid(access, a, t, false))
            && unchanged(access, t.target) && unchanged(access, t.target_class)
            && read(t.target.pointer, 8, flags) && flags == t.target_flags
            && load_item_flags(access, t.target, item_flags) && item_flags == t.target_item_flags
            && (!t.generic || (generic_type_valid(access, t) && member_valid(t)));
    }
    phase.checking_owners = false;
    valid = valid && admitted(a);
    if (!valid) reject_read_phase();
    return valid;
}
bool finish_target(const objects::UObjectIdentityAccess& access, const CallbackAuthority& a,
    const Target& target) noexcept
{
    if (read_phase && !read_phase->reset && !read_phase->rejected)
        for (std::size_t i = 0; i < read_phase->target_count; ++i)
            if (same_subject(read_phase->targets[i], target)) return true;
    // No saved containment witness: reacquire after every retained graph read.
    return target_valid(access, a, target);
}
bool resource_valid(const objects::UObjectIdentityAccess& access, const Object& resource,
    const Object& klass) noexcept
{
    return resource.pointer ? unchanged(access, resource) && unchanged(access, klass) : !klass.pointer;
}

enum class TargetOperation { Find, Capture, Compose, Tint, Apply };
struct TargetWork {
    TargetOperation operation = TargetOperation::Find;
    const CallbackAuthority* authority = nullptr;
    objects::UObjectIdentityAccess access{};
    Target target{};
    Record* record = nullptr;
    const Record* source = nullptr;
    const Record* prepared = nullptr;
    Object source_resource{}, source_class{};
    BrushFacts source_facts{}; // comparison facts only, NEVER a native brush owner
    std::array<float, 4> tint_rgba{};
    std::array<wchar_t, 128> leaf{};
    const char* stage = "target_begin";
    bool native_inflight = false, style_live = false;
    bool prevalidated = false; // private uninterrupted read phase, never a caller token
};
bool sources_valid(const TargetWork& w) noexcept
{
    return target_valid(w.access, *w.authority, w.target)
        && (!w.source || graph_valid(state.functions, w.access, *w.source))
        && (!w.prepared || graph_valid(state.functions, w.access, *w.prepared));
}
struct MemberFrame {
    Address slate_offset = 0, properties_offset = 0;
    void* slate = nullptr;
    void* original_resource = nullptr;
    std::size_t properties_size = 0;
    std::array<std::uint8_t, 0x278> properties{};
    std::array<std::uint8_t, 0x28> entry_geometry{};
};
bool member_frame(const Target& t, MemberFrame& f) noexcept
{
    switch (t.kind) {
    case SubjectKind::Image: f.slate_offset = 0x1d8; break;
    case SubjectKind::PieImage: f.slate_offset = 0x1f0; f.properties_offset = 0x190; f.properties_size = 0x60; break;
    case SubjectKind::TiledImage: f.slate_offset = 0x208; f.properties_offset = 0x198; f.properties_size = 0x70; break;
    case SubjectKind::ImageSet: f.slate_offset = 0x160; f.properties_offset = 0x13c; f.properties_size = 0x24; break;
    case SubjectKind::Button: f.slate_offset = 0x448; f.properties_offset = 0x120; f.properties_size = 0x278; break;
    default: return false;
    }
    return member_valid(t) && read(t.target.pointer, f.slate_offset, f.slate)
        && read(target_brush(t), 0x48, f.original_resource)
        && (!f.properties_size || core::safe_copy_bytes(static_cast<std::uint8_t*>(t.target.pointer)
            + f.properties_offset, f.properties.data(), f.properties_size))
        && (t.kind != SubjectKind::ImageSet || core::safe_copy_bytes(
            static_cast<std::uint8_t*>(target_brush(t)) + 0x88, f.entry_geometry.data(), f.entry_geometry.size()));
}
bool member_frame_valid(const TargetWork& w, const MemberFrame& f, bool assigned) noexcept
{
    if (!target_valid(w.access, *w.authority, w.target)
        || !graph_valid(state.functions, w.access, *w.record)) return false;
    MemberFrame now{};
    if (!member_frame(w.target, now) || now.slate != f.slate
        || now.original_resource != (assigned ? w.record->texture.pointer : f.original_resource)
        || now.entry_geometry != f.entry_geometry) return false;
    for (std::size_t i = 0; i < f.properties_size; ++i) {
        if (w.target.kind == SubjectKind::Button && assigned
            && i >= w.target.brush_offset - 0x120 && i < w.target.brush_offset - 0x120 + 0x88) continue;
        if (now.properties[i] != f.properties[i]) return false;
    }
    return true;
}
Result apply_generic(TargetWork& w) noexcept
{
    const auto& g = state.generic;
    auto& t = w.target;
    if (!t.brush_selected || (t.kind != SubjectKind::Image && !g.styles))
        return {Outcome::Unavailable, "brush_adapter"};
    MemberFrame before{};
    if (!member_frame(t, before) || (!w.prevalidated && !member_frame_valid(w, before, false)))
        return {Outcome::Identity, "brush_member_frame"};
    auto* source = static_cast<std::uint8_t*>(w.record->holder.pointer) + 0x108;
    bool assigned = false;
    const auto check = [&]() noexcept { return member_frame_valid(w, before, assigned); };
    if (t.kind == SubjectKind::Image) {
        w.stage = "subject_image_set_brush";
        w.native_inflight = true; measured_native(perf::Metric::NativeBrushPublish, state.functions.set_brush, t.target.pointer, source); w.native_inflight = false;
        assigned = true;
    } else if (t.kind == SubjectKind::Button) {
        if (!state.style_charged) {
            if (state.reserved_bytes > kRetainedByteLimit
                || state.style_scratch.size() > kRetainedByteLimit - state.reserved_bytes)
                return {Outcome::Budget, "button_style_scratch_budget"};
            state.reserved_bytes += state.style_scratch.size();
            state.style_charged = true; // single fixed process scratch, including failed attempts
        }
        state.style_scratch.fill(0);
        void* style = state.style_scratch.data();
        w.stage = "button_style_construct";
        w.native_inflight = true;
        const auto constructed = measured_native(perf::Metric::NativeStyleConstruct, g.style_construct, style);
        w.native_inflight = false; w.style_live = true;
        if (constructed != style || !check()) return {Outcome::NativeFault, w.stage};
        w.stage = "button_style_copy";
        w.native_inflight = true;
        const auto copied = measured_native(perf::Metric::NativeStyleCopy, g.style_assign, style, static_cast<std::uint8_t*>(t.target.pointer) + 0x120);
        w.native_inflight = false;
        if (copied != style || !check()) return {Outcome::NativeFault, w.stage};
        w.stage = "button_style_replace_brush";
        auto* destination = static_cast<std::uint8_t*>(style) + t.brush_offset - 0x120;
        w.native_inflight = true;
        const auto brush = measured_native(perf::Metric::NativeBrushAssign, g.brush_assign, destination, source);
        w.native_inflight = false;
        if (brush != destination || !check()) return {Outcome::NativeFault, w.stage};
        w.stage = "button_set_complete_style";
        w.native_inflight = true; measured_native(perf::Metric::NativeStylePublish, g.button_style, t.target.pointer, style); w.native_inflight = false;
        assigned = true;
        if (!check()) return {Outcome::NativeFault, w.stage};
        w.stage = "button_style_destroy";
        w.native_inflight = true; measured_native(perf::Metric::NativeStyleDestroy, g.style_destroy, style); w.native_inflight = false; w.style_live = false;
    } else {
        w.stage = "subject_assign_permanent_brush";
        auto* destination = target_brush(t);
        w.native_inflight = true;
        const auto result = measured_native(perf::Metric::NativeBrushAssign, g.brush_assign, destination, source);
        w.native_inflight = false; assigned = true;
        if (result != destination || !check()) return {Outcome::NativeFault, w.stage};
        if ((t.kind == SubjectKind::PieImage || t.kind == SubjectKind::TiledImage) && before.slate) {
            alignas(8) std::array<std::uint8_t, 0x20> attribute{};
            std::memcpy(attribute.data(), &destination, sizeof(destination));
            attribute[8] = 1; // researched constant pointer attribute; no delegate owners
            w.stage = "subject_consume_brush_attribute";
            w.native_inflight = true;
            measured_native(perf::Metric::NativeBrushPublish, t.kind == SubjectKind::PieImage ? g.pie_brush : g.tiled_brush, before.slate, attribute.data());
            w.native_inflight = false;
            if (!check()) return {Outcome::NativeFault, w.stage};
        }
        if (t.kind == SubjectKind::TiledImage || t.kind == SubjectKind::ImageSet) {
            w.stage = "subject_synchronize_geometry";
            w.native_inflight = true;
            measured_native(perf::Metric::NativeSynchronize, t.kind == SubjectKind::TiledImage ? g.tiled_sync : g.images_sync, t.target.pointer);
            w.native_inflight = false;
        }
    }
    if (!check()) return {Outcome::NativeFault, "post_subject_brush_assignment"};
    if (t.kind != SubjectKind::Image && before.slate) {
        w.stage = "subject_invalidate";
        w.native_inflight = true; measured_native(perf::Metric::NativeInvalidate, g.invalidate, before.slate, std::uint8_t{1}); w.native_inflight = false;
        if (!check()) return {Outcome::NativeFault, w.stage};
    }
    perf::count(perf::Counter::PublishedMembers);
    return {Outcome::Ready, "subject_native_brush_assigned"};
}

struct BatchMember {
    Record* source = nullptr;
    Address offset = 0;
    std::int32_t index = 0;
    void* original_resource = nullptr;
    bool written = false;
};
struct BatchWork {
    Target target{};
    const CallbackAuthority* authority = nullptr;
    objects::UObjectIdentityAccess access{};
    std::array<BatchMember, kSubjectTraversalLimit> members{};
    std::size_t count = 0;
    MemberFrame before{};
    std::array<BrushFacts, kSubjectTraversalLimit> brushes{};
    std::array<std::array<std::uint8_t, 0x28>, kSubjectTraversalLimit> geometry{};
    ApplyProgress progress{};
    const char* stage = "batch_begin";
};
void* batch_brush(const BatchWork& w, const BatchMember& m) noexcept
{
    return w.target.kind == SubjectKind::Button
        ? static_cast<std::uint8_t*>(w.target.target.pointer) + m.offset
        : reinterpret_cast<void*>(w.target.images + static_cast<Address>(m.index) * 0xb0);
}
bool batch_check(const BatchWork& w) noexcept
{
    if (!target_valid(w.access, *w.authority, w.target)) return false;
    MemberFrame now{};
    if (!member_frame(w.target, now) || now.slate != w.before.slate) return false;
    for (std::size_t i = 0; i < w.count; ++i) {
        const auto& m = w.members[i];
        bool duplicate = false;
        for (std::size_t j = 0; j < i; ++j) if (w.members[j].source == m.source) duplicate = true;
        if (!duplicate && !graph_valid(state.functions, w.access, *m.source)) return false;
        void* resource = nullptr;
        if (!read(batch_brush(w, m), 0x48, resource)
            || resource != (m.written ? m.source->texture.pointer : m.original_resource)) return false;
    }
    for (std::size_t i = 0; i < now.properties_size; ++i) {
        bool changed = false;
        if (w.target.kind == SubjectKind::Button) for (std::size_t j = 0; j < w.count; ++j) {
            const auto& m = w.members[j];
            if (m.written && i >= m.offset - 0x120 && i < m.offset - 0x120 + 0x88) changed = true;
        }
        if (!changed && now.properties[i] != w.before.properties[i]) return false;
    }
    if (w.target.kind == SubjectKind::ImageSet) {
        for (std::int32_t i = 0; i < w.target.image_count; ++i) {
            const auto* brush = reinterpret_cast<const std::uint8_t*>(w.target.images + static_cast<Address>(i) * 0xb0);
            std::array<std::uint8_t, 0x28> geometry{};
            if (!core::safe_copy_bytes(brush + 0x88, geometry.data(), geometry.size())
                || geometry != w.geometry[static_cast<std::size_t>(i)]) return false;
            bool changed = false;
            for (std::size_t j = 0; j < w.count; ++j)
                if (w.members[j].written && w.members[j].index == i) changed = true;
            BrushFacts facts{};
            if (!changed && (!read_brush(brush, facts) || facts != w.brushes[static_cast<std::size_t>(i)])) return false;
        }
    }
    return true;
}
Result batch_sequence(BatchWork& w) noexcept
{
    const auto& g = state.generic;
    const bool button = w.target.kind == SubjectKind::Button;
    if (button) {
        if (!state.style_charged) {
            if (state.reserved_bytes > kRetainedByteLimit
                || state.style_scratch.size() > kRetainedByteLimit - state.reserved_bytes)
                return {Outcome::Budget, "button_style_scratch_budget"};
            state.reserved_bytes += state.style_scratch.size();
            state.style_charged = true;
        }
        state.style_scratch.fill(0); // previous successful use was natively destroyed
        void* style = state.style_scratch.data();
        w.stage = "batch_style_construct";
        const auto constructed = measured_native(perf::Metric::NativeStyleConstruct, g.style_construct, style);
        if (constructed != style || !batch_check(w)) return {Outcome::NativeFault, w.stage};
        w.stage = "batch_style_copy";
        const auto copied = measured_native(perf::Metric::NativeStyleCopy, g.style_assign, style, static_cast<std::uint8_t*>(w.target.target.pointer) + 0x120);
        if (copied != style || !batch_check(w)) return {Outcome::NativeFault, w.stage};
        for (std::size_t i = 0; i < w.count; ++i) {
            const auto& m = w.members[i];
            auto* destination = static_cast<std::uint8_t*>(style) + m.offset - 0x120;
            w.stage = "batch_style_member";
            const auto assigned = measured_native(perf::Metric::NativeBrushAssign, g.brush_assign, destination, static_cast<std::uint8_t*>(m.source->holder.pointer) + 0x108);
            if (assigned != destination || !batch_check(w)) return {Outcome::NativeFault, w.stage};
        }
        w.stage = "batch_set_style";
        w.progress.attempted_members = w.count;
        measured_native(perf::Metric::NativeStylePublish, g.button_style, w.target.target.pointer, style);
        for (std::size_t i = 0; i < w.count; ++i) w.members[i].written = true;
        if (!batch_check(w)) return {Outcome::NativeFault, w.stage};
        w.progress.completed_members = w.count;
        perf::count(perf::Counter::PublishedMembers, w.progress.completed_members);
        w.stage = "batch_style_destroy";
        measured_native(perf::Metric::NativeStyleDestroy, g.style_destroy, style);
        if (!batch_check(w)) return {Outcome::NativeFault, w.stage};
    } else {
        for (std::size_t i = 0; i < w.count; ++i) {
            auto& m = w.members[i];
            void* destination = batch_brush(w, m);
            w.stage = "batch_image_member";
            ++w.progress.attempted_members;
            const auto assigned = measured_native(perf::Metric::NativeBrushAssign, g.brush_assign, destination, static_cast<std::uint8_t*>(m.source->holder.pointer) + 0x108);
            m.written = true;
            if (assigned != destination || !batch_check(w)) return {Outcome::NativeFault, w.stage};
            ++w.progress.completed_members;
            perf::count(perf::Counter::PublishedMembers);
        }
        w.stage = "batch_images_synchronize";
        measured_native(perf::Metric::NativeSynchronize, g.images_sync, w.target.target.pointer);
        if (!batch_check(w)) return {Outcome::NativeFault, w.stage};
    }
    if (w.before.slate) {
        w.stage = "batch_invalidate";
        measured_native(perf::Metric::NativeInvalidate, g.invalidate, w.before.slate, std::uint8_t{1});
        if (!batch_check(w)) return {Outcome::NativeFault, w.stage};
    }
    w.progress.refresh_complete = true;
    return {Outcome::Ready, "batch_native_brushes_assigned"};
}
Result guarded_batch_sequence(BatchWork& w) noexcept
{
    __try { return batch_sequence(w); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return {Outcome::NativeFault, w.stage}; }
}

Result target_sequence(TargetWork& w) noexcept
{
    const auto& a = *w.authority;
    const auto& f = state.functions;
    auto& t = w.target;
    if (w.operation == TargetOperation::Find) {
        w.stage = "lookup_owner";
        t.generation = a.session.generation;
        void* tree = nullptr;
        void* root = nullptr;
        if (!capture(w.access, a.session.widget, t.receiver)
            || t.receiver.live.serial_number <= 0
            || t.receiver.live.internal_index != a.session.widget_identity.internal_index
            || t.receiver.live.serial_number != a.session.widget_identity.serial_number
            || !read(t.receiver.pointer, 0x1d8, tree) || !capture(w.access, tree, t.tree)
            || !capture(w.access, t.tree.klass, t.tree_class)
            || !read(tree, 0x28, root) || !capture(w.access, root, t.root)
            || !owner_valid(w.access, a, t)) return {Outcome::Identity, w.stage};
        std::uint64_t tree_name = 0, class_name = 0, image_name = 0, leaf_name = 0;
        const wchar_t* texts[] = {L"WidgetTree", L"Class", L"EndImage", w.leaf.data(),
            L"PanelWidget", L"PanelSlot", L"/Script/UMG", L"Package", L"/Script/CoreUObject"};
        std::uint64_t* values[] = {&tree_name, &class_name, &image_name, &leaf_name,
            &t.panel_name, &t.slot_name, &t.umg_name, &t.package_name, &t.core_package_name};
        for (unsigned i = 0; i < 9; ++i) {
            w.stage = "lookup_find_name";
            if (!owner_valid(w.access, a, t)) return {Outcome::NativeFault, w.stage};
            measured_native(perf::Metric::NativeFindName, f.fname, values[i], texts[i], 0);
            if (!owner_valid(w.access, a, t)) return {Outcome::NativeFault, w.stage};
            if (!static_cast<std::uint32_t>(*values[i])) return {Outcome::Identity, w.stage};
        }
        t.class_name = class_name;
        w.stage = "lookup_native_class";
        if (!owner_valid(w.access, a, t)) return {Outcome::NativeFault, w.stage};
        void* klass = measured_native(perf::Metric::NativeStaticClass, f.end_image_class);
        if (!owner_valid(w.access, a, t)) return {Outcome::NativeFault, w.stage};
        Object meta{};
        if (!capture(w.access, klass, t.target_class) || t.target_class.name != image_name
            || !capture(w.access, t.target_class.klass, meta) || meta.name != class_name
            || t.tree_class.name != tree_name || t.tree_class.klass != meta.pointer)
            return {Outcome::Identity, w.stage};
        w.stage = "find_named_image";
        if (!owner_valid(w.access, a, t) || !unchanged(w.access, t.target_class))
            return {Outcome::NativeFault, w.stage};
        void* target = measured_native(perf::Metric::NativeFindChild, f.find_child, t.receiver.pointer, &leaf_name);
        if (!owner_valid(w.access, a, t) || !unchanged(w.access, t.target_class))
            return {Outcome::NativeFault, "post_lookup_admission"};
        if (!target) return {Outcome::Input, "image_not_found"};
        if (!capture(w.access, target, t.target)
            || !read(target, 8, t.target_flags) || !load_item_flags(w.access, t.target, t.target_item_flags)
            || t.target.name != leaf_name || !target_valid(w.access, a, t))
            return {Outcome::Identity, "lookup_target"};
        return {Outcome::Ready, "image_target_checked"};
    }

    auto& r = *w.record;
    if (w.operation == TargetOperation::Capture || w.operation == TargetOperation::Compose || w.operation == TargetOperation::Tint) {
        Names n = state.carrier_names;
        const wchar_t* texts[] = {L"/Engine/Transient", L"Package", L"Class", L"/Script/CoreUObject"};
        std::uint64_t* values[] = {&n.transient, &n.package, &n.klass, &n.core_package};
        for (unsigned i = 0; i < 4; ++i) {
            if (static_cast<std::uint32_t>(*values[i])) continue;
            w.stage = "original_find_name";
            if (!sources_valid(w)) return {Outcome::NativeFault, w.stage};
            measured_native(perf::Metric::NativeFindName, f.fname, values[i], texts[i], 0);
            if (!sources_valid(w)) return {Outcome::NativeFault, w.stage};
            if (!static_cast<std::uint32_t>(*values[i])) return {Outcome::Identity, w.stage};
        }
        state.carrier_names = n; // Only complete FNAME_Find values; no package/object authority.
        w.stage = "original_package";
        if (!package_valid(f, w.access, n, r.package)
            || !capture(w.access, r.package.klass, r.package_class)) return {Outcome::Package, w.stage};
        r.holder_class = t.target_class;
        if (t.generic) {
            ClassWitness holder{};
            if (!published_class(w.access, NativeClass::Image, holder)
                || !class_unchanged(w.access, holder)) return {Outcome::Identity, "original_carrier_class"};
            r.holder_class = holder.klass;
        }
        alignas(8) std::array<std::uint8_t, 0x40> parameters{};
        std::memcpy(parameters.data(), &r.holder_class.pointer, sizeof(void*));
        std::memcpy(parameters.data() + 8, &r.package.pointer, sizeof(void*));
        const std::uint32_t flags = 0xc0;
        std::memcpy(parameters.data() + 0x18, &flags, sizeof(flags));
        w.stage = "original_construct_holder";
        if (!sources_valid(w) || !package_same(f, w.access, r.package))
            return {Outcome::NativeFault, w.stage};
        r.holder.pointer = measured_native(perf::Metric::NativeConstruct, f.construct, parameters.data());
        // Creation listeners can retire/recycle/detach the source. Do not even
        // read its brush (or touch the new holder) before these rechecks.
        if (!sources_valid(w) || !package_same(f, w.access, r.package))
            return {Outcome::NativeFault, "post_original_construction"};
        void* slate = nullptr;
        void* slate_owner = nullptr;
        void* resource = nullptr;
        if (!capture(w.access, r.holder.pointer, r.holder) || !rooted(w.access, r.holder)
            || r.holder.klass != r.holder_class.pointer || r.holder.outer != r.package.pointer
            || !read(r.holder.pointer, 0x1d8, slate) || slate
            || !read(r.holder.pointer, 0x1e0, slate_owner) || slate_owner
            || !read(r.holder.pointer, 0x150, resource) || resource)
            return {Outcome::Holder, w.stage};
        w.stage = "original_source_resource";
        if (!sources_valid(w)) return {Outcome::NativeFault, w.stage};
        void* source_brush = w.source ? static_cast<std::uint8_t*>(w.source->holder.pointer) + 0x108 : target_brush(t);
        BrushFacts now{};
        if (!read_brush(source_brush, now) || now != w.source_facts
            || !resource_valid(w.access, w.source_resource, w.source_class))
            return {Outcome::NativeFault, "original_source_drift"};
        r.texture = w.source_resource;
        r.texture_class = w.source_class;
        w.stage = "capture_original_brush";
        if (!sources_valid(w) || !package_same(f, w.access, r.package)
            || !rooted(w.access, r.holder)
            || !resource_valid(w.access, r.texture, r.texture_class)) return {Outcome::NativeFault, w.stage};
        measured_native(perf::Metric::NativeBrushCopy, f.set_brush, r.holder.pointer, source_brush);
        r.bound = true;
        if (!sources_valid(w) || !graph_valid(f, w.access, r)
            || !read_brush(source_brush, now) || now != w.source_facts)
            return {Outcome::NativeFault, "post_original_capture"};
        if (w.operation == TargetOperation::Compose) {
            // This is an owned native brush in a rooted holder, not copied C++
            // shared-owner storage. Invalidate UV; let the native setter release
            // cached owners, even if the original already used this texture.
            auto* brush = static_cast<std::uint8_t*>(r.holder.pointer) + 0x108;
            w.stage = "compose_invalidate_uv";
            if (!core::safe_write_field(brush, 0x68, std::uint8_t{0}))
                return {Outcome::NativeFault, w.stage};
            if (r.texture.pointer) {
                w.stage = "compose_clear_resource_cache";
                measured_native(perf::Metric::NativeBrushSetResource, f.set_resource, brush, nullptr);
                if (!sources_valid(w)) return {Outcome::NativeFault, w.stage};
                r.texture = {}; r.texture_class = {};
                if (!graph_valid(f, w.access, r)) return {Outcome::NativeFault, w.stage};
            }
            w.stage = "compose_bind_texture";
            measured_native(perf::Metric::NativeBrushSetResource, f.set_resource, brush, w.prepared->texture.pointer);
            if (!sources_valid(w)) return {Outcome::NativeFault, w.stage};
            r.texture = w.prepared->texture; r.texture_class = w.prepared->texture_class;
            r.kind = RecordKind::Composed;
            std::uint8_t uv = 1;
            void* cache = nullptr;
            void* cache_owner = nullptr;
            if (!graph_valid(f, w.access, r) || !read(brush, 0x68, uv) || uv
                || !read(brush, 0x70, cache) || cache
                || !read(brush, 0x78, cache_owner) || cache_owner)
                return {Outcome::NativeFault, "post_composition"};
            return {Outcome::Ready, "composed_brush_retained"};
        }
        if (w.operation == TargetOperation::Tint) {
            // Same ABI as the existing palette's specified constant: initialized
            // scalar value, mode zero, NO shared controller to copy/destroy.
            alignas(8) std::array<std::uint8_t, 0x28> constant{};
            std::memcpy(constant.data(), w.tint_rgba.data(), sizeof(float) * 4);
            auto* brush = static_cast<std::uint8_t*>(r.holder.pointer) + 0x108;
            w.stage = "own_brush_tint";
            w.native_inflight = true;
            const auto assigned = measured_native(perf::Metric::NativeColorAssign, state.generic.color_assign, brush + 0x20, constant.data());
            w.native_inflight = false;
            if (!sources_valid(w) || assigned != brush + 0x20 || !graph_valid(f, w.access, r)
                || !read_brush(brush, now)) return {Outcome::NativeFault, "post_brush_tint"};
            for (std::size_t i = 0; i < now.size(); ++i) {
                // Setter payload contract, not a semantic dedup mask: native
                // SlateColor assignment does not promise padding contents.
                if (i >= 0x31 && i < 0x38) continue;
                const auto expected = i >= 0x20 && i < 0x48 ? constant[i - 0x20] : w.source_facts[i];
                if (now[i] != expected) return {Outcome::NativeFault, "brush_tint_or_shape_drift"};
            }
            return {Outcome::Ready, "tinted_brush_retained"};
        }
        return {Outcome::Ready, "original_brush_retained"};
    }

    w.stage = "apply_retained_brush";
    if (!w.prevalidated) {
        if (!graph_valid(f, w.access, r)) return {Outcome::NativeFault, w.stage};
        if (!target_valid(w.access, a, t)) return {Outcome::Identity, w.stage};
    }
    if (t.generic) return apply_generic(w);
    measured_native(perf::Metric::NativeBrushPublish, f.set_brush, t.target.pointer, static_cast<std::uint8_t*>(r.holder.pointer) + 0x108);
    w.stage = "post_brush_assignment";
    if (!target_valid(w.access, a, t) || !graph_valid(f, w.access, r))
        return {Outcome::NativeFault, w.stage};
    void* resource = nullptr;
    if (!read(t.target.pointer, 0x150, resource) || resource != r.texture.pointer)
        return {Outcome::NativeFault, w.stage};
    perf::count(perf::Counter::PublishedMembers);
    return {Outcome::Ready, "native_brush_assigned"};
}

Result guarded_target_sequence(TargetWork& w) noexcept
{
    __try { return target_sequence(w); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return {Outcome::NativeFault, w.stage}; }
}

Result latch(Result result) noexcept
{
    if (result.outcome == Outcome::NativeFault) {
        state.available = false;
        state.poisoned = true;
    }
    return result;
}
Result find_subject_sequence(std::wstring_view leaf, SubjectKind kind, std::uint64_t row_id,
    const CallbackAuthority& a, const objects::UObjectIdentityAccess& access, Target& t, bool cursor_owner = false) noexcept
{
    if (leaf.empty() || leaf.size() >= 128 || static_cast<unsigned>(kind) > static_cast<unsigned>(SubjectKind::CanvasPanel))
        return {Outcome::Input, "subject_leaf_or_kind"};
    std::array<wchar_t, 128> text{};
    for (std::size_t i = 0; i < leaf.size(); ++i) {
        const auto c = leaf[i];
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')
            || (c >= L'0' && c <= L'9') || c == L'_')) return {Outcome::Input, "subject_leaf"};
        text[i] = c;
    }
    const auto names = ensure_generic_names(access, a);
    if (!names) return names;
    generic_witness_names(t);
    t.generation = a.session.generation; t.kind = kind; t.brush_selected = false; t.row_id = row_id;
    void* receiver = a.session.widget;
    if (cursor_owner) {
        t.cursor_owner = true;
        if (row_id || !main_identity(access, a, t.cursor_main)
            || !current_list(access, t.cursor_main.pointer, RowKind::Song, t.cursor_list)
            || !read(t.cursor_list.pointer, 0x8f8, receiver) || !receiver)
            return {Outcome::Identity, "subject_current_cursor"};
    } else if (row_id) {
        const auto* binding = row_binding(row_id);
        if (!binding) return {Outcome::Identity, "subject_row_ticket"};
        receiver = binding->row.pointer;
    }
    void* tree = nullptr; void* root = nullptr;
    if (!capture(access, receiver, t.receiver) || t.receiver.live.serial_number <= 0
        || !typed_object(access, t.receiver, NativeClass::UserWidget)
        || !read(receiver, 0x1d8, tree) || !capture(access, tree, t.tree)
        || !typed_object(access, t.tree, NativeClass::WidgetTree)
        || !capture(access, t.tree.klass, t.tree_class)
        || !read(tree, 0x28, root) || !capture(access, root, t.root) || !owner_valid(access, a, t))
        return {Outcome::Identity, "subject_receiver_tree"};
    std::uint64_t name = 0;
    measured_native(perf::Metric::NativeFindName, state.functions.fname, &name, text.data(), 0);
    if (!owner_valid(access, a, t)) return {Outcome::NativeFault, "post_subject_name"};
    if (!static_cast<std::uint32_t>(name)) return {Outcome::Input, "subject_name_missing"};
    PanelBases bases{};
    if (!panel_bases(access, t, bases)) return {Outcome::Identity, "subject_panel_bases"};
    std::array<Object, 256> pending{};
    std::array<void*, 256> visited{};
    std::size_t pending_count = 1, visited_count = 0;
    pending[0] = t.root;
    bool found = false;
    while (pending_count) {
        perf::count(perf::Counter::TreeNodes);
        const Object node = pending[--pending_count];
        if (visited_count == visited.size() || !unchanged(access, node))
            return {Outcome::Identity, "subject_traversal_bounds"};
        for (std::size_t i = 0; i < visited_count; ++i)
            if (visited[i] == node.pointer) return {Outcome::Identity, "subject_traversal_cycle"};
        visited[visited_count++] = node.pointer;
        if (node.name == name) {
            if (found) return {Outcome::Identity, "subject_name_ambiguous"};
            t.target = node;
            if (!capture(access, node.klass, t.target_class) || !read(node.pointer, 8, t.target_flags)
                || !load_item_flags(access, node, t.target_item_flags)) return {Outcome::Identity, "subject_identity"};
            found = true;
        }
        Ancestry panel{};
        if (!ancestry(access, node, bases.panel, bases, panel)) continue;
        PointerArray slots{};
        if (!pointer_array(node.pointer, 0x108, slots)) return {Outcome::Identity, "subject_slots_header"};
        for (std::int32_t i = 0; i < slots.count; ++i) {
            void* slot_pointer = nullptr; void* child_pointer = nullptr; void* parent = nullptr; void* child_slot = nullptr;
            Object slot{}, child{}; Ancestry slot_type{};
            if (!read(slots.data, static_cast<Address>(i) * sizeof(void*), slot_pointer)
                || !capture(access, slot_pointer, slot) || !ancestry(access, slot, bases.slot, bases, slot_type)
                || !read(slot_pointer, 0x28, parent) || parent != node.pointer
                || !read(slot_pointer, 0x30, child_pointer) || !capture(access, child_pointer, child)
                || !read(child_pointer, 0x28, child_slot) || child_slot != slot_pointer
                || !unchanged(access, slot) || !unchanged(access, child)
                || !ancestry_unchanged(access, slot_type) || !same_array(node.pointer, 0x108, slots))
                return {Outcome::Identity, "subject_strong_edge"};
            void* again = nullptr;
            if (!read(slots.data, static_cast<Address>(i) * sizeof(void*), again) || again != slot_pointer
                || pending_count == pending.size()) return {Outcome::Identity, "subject_edge_reread"};
            pending[pending_count++] = child;
        }
        if (!ancestry_unchanged(access, panel) || !unchanged(access, node))
            return {Outcome::Identity, "subject_panel_reread"};
    }
    if (!owner_valid(access, a, t) || !bases_unchanged(access, bases))
        return {Outcome::NativeFault, "subject_owner_reread"};
    if (!found) return {Outcome::Input, "subject_not_found"};
    if (!target_valid(access, a, t)) return {Outcome::Identity, "subject_type_containment"};
    return {Outcome::Ready, "subject_checked"};
}
Result guarded_subject_find(std::wstring_view leaf, SubjectKind kind, std::uint64_t row_id,
    const CallbackAuthority& a, const objects::UObjectIdentityAccess& access, Target& target, bool cursor_owner = false) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::FindSubject);
    __try {
        const auto result = find_subject_sequence(leaf, kind, row_id, a, access, target, cursor_owner);
        perf::finish(stamp, static_cast<bool>(result)); return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        perf::finish(stamp, false); return {Outcome::NativeFault, "subject_find_native_fault"};
    }
}
} // namespace

namespace detail {
Result preflight(std::uint32_t width, std::uint32_t height, std::size_t bytes,
    std::uint64_t reserved_bytes) noexcept
{
    const std::uint64_t expected = static_cast<std::uint64_t>(width) * height * 4;
    if (!width || !height || width > 4096 || height > 4096
        || expected > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
        || expected != bytes) return {Outcome::Input, "preflight_pixels"};
    if (reserved_bytes > kRetainedByteLimit
        || expected > kRetainedByteLimit - reserved_bytes) return {Outcome::Budget, "reserve_lifetime_budget"};
    return {Outcome::Ready, "preflight_pixels"};
}
const char* fresh_mip_failure(const FreshMip& m, std::uint32_t width, std::uint32_t height,
    std::size_t bytes) noexcept
{
    if (!width || !height || width > 4096 || height > 4096) return "fresh_mip_input";
    if (!m.platform || !m.array || !m.mip || !m.data) return "fresh_mip_pointers";
    if (m.count != 1) return "fresh_mip_count";
    if (m.capacity < 4) return "fresh_mip_capacity";
    if (m.width != static_cast<std::int32_t>(width) || m.height != static_cast<std::int32_t>(height))
        return "fresh_mip_platform_dimensions";
    if (m.format != 2) return "fresh_mip_format";
    if (m.mip_width != static_cast<std::int32_t>(width) || m.mip_height != static_cast<std::int32_t>(height))
        return "fresh_mip_dimensions";
    if (m.bytes != bytes) return "fresh_mip_bytes";
    if (m.flags != 0) return "fresh_mip_flags";
    if (m.lock != 0) return "fresh_mip_lock";
    return nullptr;
}
bool fresh_mip_matches(const FreshMip& m, std::uint32_t width, std::uint32_t height,
    std::size_t bytes) noexcept
{
    return !fresh_mip_failure(m, width, height, bytes);
}
} // namespace detail

GenericFunctions authenticate_generic(HMODULE module) noexcept
{
    GenericFunctions g{};
    bool complete = true;
    for (std::size_t i = 0; i < kClassCount; ++i) {
        const auto& spec = class_specs[i];
        if (!panel_class_reference(module, spec.reference, spec.site, spec.slot)) complete = false;
        g.slots[i] = reinterpret_cast<std::uint8_t*>(module) + spec.slot;
    }
    if (!complete) return {};
    g.classes = true;
    if (signature(module, "native_slate_color_assign", rva::NativeSlateColorAssign))
        g.color_assign = reinterpret_cast<Assign>(reinterpret_cast<Address>(module) + rva::NativeSlateColorAssign);
    const struct { const char* id; Address rva; } styles[] = {
        {"native_slate_brush_assign", rva::NativeSlateBrushAssign},
        {"end_pie_image_slate_set_brush", rva::EndPieImageSlateSetBrush},
        {"end_tiled_image_slate_set_brush", rva::EndTiledImageSlateSetBrush},
        {"end_tiled_image_synchronize", rva::EndTiledImageSynchronize},
        {"end_image_set_synchronize", rva::EndImageSetSynchronize},
        {"end_button_set_style", rva::EndButtonSetStyle},
        {"native_button_style_construct", rva::NativeButtonStyleConstruct},
        {"native_button_style_assign", rva::NativeButtonStyleAssign},
        {"native_button_style_destroy", rva::NativeButtonStyleDestroy},
        {"native_slate_invalidate", rva::NativeSlateInvalidate},
    };
    for (const auto& spec : styles) if (!signature(module, spec.id, spec.rva)) return g;
    const auto address = [module](Address value) { return reinterpret_cast<Address>(module) + value; };
    g.brush_assign = reinterpret_cast<Assign>(address(rva::NativeSlateBrushAssign));
    g.pie_brush = reinterpret_cast<Binary>(address(rva::EndPieImageSlateSetBrush));
    g.tiled_brush = reinterpret_cast<Binary>(address(rva::EndTiledImageSlateSetBrush));
    g.tiled_sync = reinterpret_cast<Unary>(address(rva::EndTiledImageSynchronize));
    g.images_sync = reinterpret_cast<Unary>(address(rva::EndImageSetSynchronize));
    g.button_style = reinterpret_cast<Binary>(address(rva::EndButtonSetStyle));
    g.style_construct = reinterpret_cast<BrushConstruct>(address(rva::NativeButtonStyleConstruct));
    g.style_assign = reinterpret_cast<Assign>(address(rva::NativeButtonStyleAssign));
    g.style_destroy = reinterpret_cast<Unary>(address(rva::NativeButtonStyleDestroy));
    g.invalidate = reinterpret_cast<Invalidate>(address(rva::NativeSlateInvalidate));
    g.styles = true;
    return g;
}

Result configure(HMODULE module, std::string_view profile) noexcept
{
    if (entered) return {Outcome::Busy, "configure"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "configure"};
    state.available = false;
    state.functions = {};
    state.generic = {};
    state.hud_text = {};
    state.hud_sharp = {};
    state.generic_names = {};
    state.carrier_names = {};
    state.module = nullptr;
    if (state.poisoned) return {Outcome::Unavailable, "native_fault_latched"};
    if (!module || profile != FF7RP_TARGET_BUILD_ID || !core::is_supported_exe(module))
        return {Outcome::UnsupportedImage, "executable_identity"};
    // FNameCtor's canonical production policy is build_identity, not signature.
    if (!mapped(module, rva::FNameCtor, 1, true)) return {Outcome::UnsupportedImage, "fname_callable"};
    Functions f{};
    const auto base = reinterpret_cast<Address>(module);
#define THEME_FUNCTION(field, type, id, symbol) \
    if (!signature(module, id, rva::symbol)) return {Outcome::Signature, id}; \
    f.field = reinterpret_cast<type>(base + rva::symbol)
    THEME_FUNCTION(construct, Construct, "static_construct_object", StaticConstructObject);
    THEME_FUNCTION(end_image_class, StaticClass, "end_image_static_class", EndImageStaticClass);
    THEME_FUNCTION(find_child, FindChild, "end_image_find_named_child", EndImageFindNamedChild);
    THEME_FUNCTION(brush_construct, BrushConstruct, "native_slate_brush_construct", NativeSlateBrushConstruct);
    THEME_FUNCTION(brush_destroy, Unary, "native_slate_brush_destroy", NativeSlateBrushDestroy);
    THEME_FUNCTION(set_resource, Binary, "native_slate_brush_set_resource", NativeSlateBrushSetResource);
    THEME_FUNCTION(set_brush, Binary, "end_image_set_brush", EndImageSetBrush);
    THEME_FUNCTION(texture_create, TextureCreate, "native_texture2d_create_transient", NativeTexture2DCreateTransient);
    THEME_FUNCTION(materialize, Unary, "native_bulk_data_materialize", NativeBulkDataMaterialize);
    THEME_FUNCTION(unlock, Unary, "native_bulk_data_unlock", NativeBulkDataUnlock);
    THEME_FUNCTION(update, Unary, "native_texture2d_update_resource", NativeTexture2DUpdateResource);
    THEME_FUNCTION(mark_not_offline_processed, Unary, "native_texture_mark_not_offline_processed", NativeTextureMarkNotOfflineProcessed);
#undef THEME_FUNCTION
    if (!panel_class_reference(module, "native_panel_widget_class_read_site",
            rva::NativePanelWidgetClassReadSite, rva::NativePanelWidgetClass)
        || !panel_class_reference(module, "native_panel_slot_class_read_site",
            rva::NativePanelSlotClassReadSite, rva::NativePanelSlotClass)
        || !ancestry_contract(module)) return {Outcome::Signature, "panel_containment_contract"};
    f.panel_class_slot = reinterpret_cast<void*>(base + rva::NativePanelWidgetClass);
    f.panel_slot_class_slot = reinterpret_cast<void*>(base + rva::NativePanelSlotClass);
    if (!mapped(module, rva::NativeTransientPackage, sizeof(void*), false))
        return {Outcome::UnsupportedImage, "transient_package_slot"};
    f.package_slot = reinterpret_cast<void*>(base + rva::NativeTransientPackage);
    f.fname = reinterpret_cast<FNameCtor>(base + rva::FNameCtor);
    state.generic = authenticate_generic(module); // optional capabilities publish ONLY complete tables
    // All seven default-build contracts must be present; older builds omit this capability.
    HudTextFunctions hud{};
    if (signature(module,"song_hud_text_from_string",rva::SongHudTextFromString)
        && signature(module,"song_hud_text_item_construct",rva::SongHudTextItemConstruct)
        && signature(module,"song_hud_text_destroy",rva::SongHudTextDestroy)
        && signature(module,"song_hud_text_submit",rva::SongHudTextSubmit)
        && signature(module,"song_hud_text_item_destroy",rva::SongHudTextItemDestroy)
        && signature(module,"song_hud_text_scale_layout",rva::SongHudTextScaleLayout)
        && signature(module,"song_hud_text_foreground_layout",rva::SongHudTextForegroundLayout)) {
        hud.from_string=reinterpret_cast<HudTextFromString>(base+rva::SongHudTextFromString);
        hud.construct=reinterpret_cast<HudItemConstruct>(base+rva::SongHudTextItemConstruct);
        hud.destroy_text=reinterpret_cast<Unary>(base+rva::SongHudTextDestroy);
        hud.submit=reinterpret_cast<Binary>(base+rva::SongHudTextSubmit);
        hud.destroy_item=reinterpret_cast<Unary>(base+rva::SongHudTextItemDestroy);
    }
    state.hud_text = hud;
    if (hud && signature(module,"song_hud_font_info_construct",rva::SongHudFontInfoConstruct)
        && signature(module,"song_hud_item_measure",rva::SongHudItemMeasure)
        && signature(module,"song_hud_canvas_dpi",rva::SongHudCanvasDpi)
        && signature(module,"song_hud_font_cache_dispatch",rva::SongHudFontCacheDispatch)
        && signature(module,"song_hud_font_info_layout",rva::SongHudFontInfoLayout)
        && signature(module,"song_hud_font_optional_layout",rva::SongHudFontOptionalLayout)
        && signature(module,"song_hud_font_typeface_source",rva::SongHudFontTypefaceSource)
        && signature(module,"song_hud_font_optional_init",rva::SongHudFontOptionalInit)
        && signature(module,"song_hud_font_optional_destroy",rva::SongHudFontOptionalDestroy)) {
        state.hud_sharp={reinterpret_cast<HudInfoConstruct>(base+rva::SongHudFontInfoConstruct),
            reinterpret_cast<HudItemMeasure>(base+rva::SongHudItemMeasure),
            reinterpret_cast<HudCanvasDpi>(base+rva::SongHudCanvasDpi)};
    }
    try {
        if (!state.hud_icon_pixels.front().width) state.hud_icon_pixels=make_song_hud_icons();
    } catch (...) { return {Outcome::Memory,"hud_icon_pixels"}; }
    state.functions = f;
    state.module = module;
    state.available = true;
    return {Outcome::Ready, "authenticated_callables"};
}

void disable() noexcept
{
    if (entered) {
        // Same-thread listener retirement: retain the in-use function table, but
        // revoke admission immediately. No waiting on our own nonrecursive lock.
        state.available = false;
        state.poisoned = true;
        return;
    }
    std::lock_guard lock(state.mutex);
    state.available = false;
    state.functions = {};
    state.module = nullptr;
}

Result create(const core::ThemeImage& image, const CallbackAuthority& authority, OwnedImage& out) noexcept
{
    perf::Span metric(perf::Metric::Upload);
    out = {};
    if (entered) return {Outcome::Busy, "create"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "create"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "create"};
    if (!admitted(authority)) return {Outcome::Authority, "callback_admission"};
    const auto input = detail::preflight(image.width, image.height, image.bgra.size(),
        state.reserved_bytes);
    if (!input) return input;
    Work work{};
    if (!capture_current_uobject_identity_access(work.access)) return {Outcome::Identity, "identity_service"};
    // Reserve BEFORE any native allocation, including static-class lazy creation.
    // Failures consume their reservation too. Native OOM is not recoverable rollback.
    std::size_t slot = kInvalidImageSlot;
    const auto reservation = reserve_record(image.bgra.size() + kRootedCarrierBytes, slot);
    if (!reservation) return reservation;
    if (!admitted(authority)) {
        state.available = false; state.poisoned = true;
        return {Outcome::NativeFault, "post_ledger_admission"};
    }
    work.f = &state.functions;
    work.image = &image;
    work.authority = &authority;
    work.record = &state.retained[slot];
    auto result = guarded_sequence(work);
    if (!cleanup(work)) {
        result = {Outcome::NativeFault, work.result.stage};
    } else if (!admitted(authority)
        || (work.record->bound && !graph_valid(state.functions, work.access, *work.record))) {
        result = {Outcome::NativeFault, "post_cleanup_admission"};
    }
    if (result.outcome == Outcome::NativeFault) {
        state.available = false;
        state.poisoned = true;
    }
    if (result) {
        work.record->creation_thread = GetCurrentThreadId();
        work.record->width = image.width; work.record->height = image.height;
        state.retained[slot].published = true;
        out.slot_ = slot;
    }
    result.mip_header_read = work.result.mip_header_read;
    result.mip_count = work.result.mip_count;
    result.mip_capacity = work.result.mip_capacity;
    return result;
}

Result borrow_brush(const OwnedImage& image, const CallbackAuthority& authority, const void*& brush) noexcept
{
    perf::Span metric(perf::Metric::BorrowBrush);
    brush = nullptr;
    if (entered) return {Outcome::Busy, "borrow_brush"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "borrow_brush"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "borrow_brush"};
    if (!admitted(authority)) return {Outcome::Authority, "borrow_callback_admission"};
    if (image.slot_ >= state.reserved_images || !state.retained[image.slot_].published)
        return {Outcome::Input, "owned_image"};
    objects::UObjectIdentityAccess access{};
    if (!capture_current_uobject_identity_access(access)
        || !graph_valid(state.functions, access, state.retained[image.slot_]))
        return {Outcome::Identity, "borrow_rooted_graph"};
    if (state.retained[image.slot_].kind != RecordKind::Prepared
        && (!image.target_bound_ || !target_valid(access, authority, image.affinity_)))
        return {Outcome::Identity, "borrow_original_target"};
    brush = static_cast<const std::uint8_t*>(state.retained[image.slot_].holder.pointer) + 0x108;
    return {Outcome::Ready, "borrow_owned_brush"};
}

Result find_image(std::wstring_view leaf, const CallbackAuthority& a, ImageTarget& out) noexcept
{
    out = {};
    if (entered) return {Outcome::Busy, "find_image"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "find_image"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "find_image"};
    if (!admitted(a)) return {Outcome::Authority, "lookup_admission"};
    TargetWork w{};
    if (leaf.empty() || leaf.size() >= w.leaf.size()) return {Outcome::Input, "leaf_name"};
    for (std::size_t i = 0; i < leaf.size(); ++i) {
        const auto c = leaf[i];
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')
            || (c >= L'0' && c <= L'9') || c == L'_')) return {Outcome::Input, "leaf_name"};
        w.leaf[i] = c;
    }
    if (!capture_current_uobject_identity_access(w.access)) return {Outcome::Identity, "identity_service"};
    w.authority = &a;
    const auto result = latch(guarded_target_sequence(w));
    if (result) out.facts_ = w.target;
    return result;
}

Result capture_original(const ImageTarget& target, const CallbackAuthority& a, OwnedImage& out) noexcept
{
    perf::Span metric(perf::Metric::Capture);
    perf::count(perf::Counter::CaptureRequestedMembers);
    out = {};
    if (entered) return {Outcome::Busy, "capture_original"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "capture_original"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "capture_original"};
    if (!admitted(a)) return {Outcome::Authority, "original_admission"};
    TargetWork w{};
    w.operation = TargetOperation::Capture;
    w.authority = &a;
    w.target = target.facts_;
    if (!capture_current_uobject_identity_access(w.access) || !target_valid(w.access, a, w.target))
        return {Outcome::Identity, "original_target"};
    if (!read_brush(target_brush(w.target), w.source_facts)) return {Outcome::Memory, "original_brush_facts"};
    void* resource = nullptr;
    std::memcpy(&resource, w.source_facts.data() + 0x48, sizeof(resource));
    if (resource && (!capture(w.access, resource, w.source_resource)
        || !capture(w.access, w.source_resource.klass, w.source_class)))
        return {Outcome::Identity, "original_resource_facts"};
    BrushFacts reread{};
    if (!target_valid(w.access, a, w.target) || !read_brush(target_brush(w.target), reread)
        || reread != w.source_facts || !resource_valid(w.access, w.source_resource, w.source_class))
        return latch({Outcome::NativeFault, "original_source_drift"});
    // Equality facts only. Assignments ALWAYS use the native owning SetBrush.
    // A false miss (including padding/cache differences) is preferable to masks.
    for (std::size_t i = 0; i < state.reserved_images; ++i) {
        const auto& retained = state.retained[i];
        if (!retained.published || retained.kind != RecordKind::Original) continue;
        if (!graph_valid(state.functions, w.access, retained))
            return latch({Outcome::NativeFault, "original_cache_graph"});
        BrushFacts owned{};
        if (!brush_facts(retained.holder.pointer, owned))
            return latch({Outcome::NativeFault, "original_cache_facts"});
        if (owned != w.source_facts) continue;
        if (!target_valid(w.access, a, w.target) || !graph_valid(state.functions, w.access, retained)
            || !read_brush(target_brush(w.target), reread) || reread != owned)
            return latch({Outcome::NativeFault, "original_cache_recheck"});
        out.slot_ = i; out.affinity_ = w.target; out.target_bound_ = true;
        perf::count(perf::Counter::CaptureHits);
        perf::count(perf::Counter::CapturePublishedMembers);
        return {Outcome::Ready, "original_brush_reused"};
    }
    // Original resources already exist. Charge the copied native brush payload
    // plus one holder slot against the SAME irreversible process-lifetime limits.
    constexpr std::uint64_t bytes = kRootedCarrierBytes;
    perf::count(perf::Counter::CaptureMisses);
    std::size_t slot = kInvalidImageSlot;
    const auto reservation = reserve_record(bytes, slot);
    if (!reservation) return reservation;
    auto& r = state.retained[slot];
    r.kind = RecordKind::Original;
    w.record = &r;
    const auto result = latch(guarded_target_sequence(w));
    if (result) {
        r.published = true;
        out.slot_ = slot;
        out.affinity_ = w.target;
        out.target_bound_ = true;
        perf::count(perf::Counter::CapturePublishedMembers);
    }
    return result;
}

Result compose(const OwnedImage& original, const OwnedImage& prepared,
    const CallbackAuthority& a, OwnedImage& out) noexcept
{
    perf::Span metric(perf::Metric::Compose);
    out = {};
    if (entered) return {Outcome::Busy, "compose"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "compose"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "compose"};
    if (!admitted(a)) return {Outcome::Authority, "compose_admission"};
    if (original.slot_ >= state.reserved_images || prepared.slot_ >= state.reserved_images
        || !original.target_bound_) return {Outcome::Input, "composition_inputs"};
    TargetWork w{};
    w.operation = TargetOperation::Compose;
    w.authority = &a; w.target = original.affinity_;
    w.source = &state.retained[original.slot_]; w.prepared = &state.retained[prepared.slot_];
    if (!w.source->published || !w.prepared->published || w.source->kind != RecordKind::Original
        || w.prepared->kind != RecordKind::Prepared) return {Outcome::Input, "composition_kinds"};
    if (!capture_current_uobject_identity_access(w.access) || !target_valid(w.access, a, w.target))
        return {Outcome::Identity, "compose_target"};
    if (!sources_valid(w)) return latch({Outcome::NativeFault, "compose_source_graphs"});
    for (std::size_t i = 0; i < state.reserved_images; ++i) {
        const auto& r = state.retained[i];
        if (!r.published || r.kind != RecordKind::Composed
            || r.original_slot != original.slot_ || r.prepared_slot != prepared.slot_) continue;
        if (!sources_valid(w) || !graph_valid(state.functions, w.access, r))
            return latch({Outcome::NativeFault, "composition_cache_graph"});
        out.slot_ = i; out.affinity_ = w.target; out.target_bound_ = true;
        perf::count(perf::Counter::ComposeHits);
        return {Outcome::Ready, "composed_brush_reused"};
    }
    if (!brush_facts(w.source->holder.pointer, w.source_facts))
        return latch({Outcome::NativeFault, "composition_source_facts"});
    perf::count(perf::Counter::ComposeMisses);
    w.source_resource = w.source->texture; w.source_class = w.source->texture_class;
    constexpr std::uint64_t bytes = kRootedCarrierBytes;
    std::size_t slot = kInvalidImageSlot;
    const auto reservation = reserve_record(bytes, slot);
    if (!reservation) return reservation;
    w.record = &state.retained[slot];
    w.record->kind = RecordKind::Original; // null intermediate resource is legal until final binding
    w.record->original_slot = original.slot_; w.record->prepared_slot = prepared.slot_;
    const auto result = latch(guarded_target_sequence(w));
    if (result) {
        w.record->published = true;
        out.slot_ = slot; out.affinity_ = w.target; out.target_bound_ = true;
    }
    return result;
}

Result apply(const ImageTarget& target, const OwnedImage& image, const CallbackAuthority& a) noexcept
{
    perf::Span metric(perf::Metric::Apply);
    perf::count(perf::Counter::ApplyRequestedMembers);
    if (entered) return {Outcome::Busy, "apply"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "apply"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "apply"};
    if (!admitted(a)) return {Outcome::Authority, "apply_admission"};
    if (image.slot_ >= state.reserved_images || !state.retained[image.slot_].published)
        return {Outcome::Input, "retained_brush"};
    TargetWork w{};
    w.operation = TargetOperation::Apply;
    w.authority = &a;
    w.target = target.facts_;
    w.record = &state.retained[image.slot_];
    if (!capture_current_uobject_identity_access(w.access) || !target_valid(w.access, a, w.target))
        return {Outcome::Identity, "apply_target"};
    const auto& r = *w.record;
    if (r.kind != RecordKind::Prepared
        && (!image.target_bound_ || !same_target(image.affinity_, w.target)))
        return {Outcome::Identity, "original_owner_binding"};
    if (!graph_valid(state.functions, w.access, r)) return latch({Outcome::NativeFault, "apply_source_graph"});
    w.prevalidated = true;
    return latch(guarded_target_sequence(w));
}
Result tint(const OwnedImage& source, const std::array<float, 4>& rgba,
    const CallbackAuthority& a, OwnedImage& out) noexcept
{
    perf::Span metric(perf::Metric::Tint);
    out = {};
    if (entered) return {Outcome::Busy, "tint"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "tint"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "tint"};
    if (!admitted(a)) return {Outcome::Authority, "tint_admission"};
    if (!state.generic.color_assign) return {Outcome::Unavailable, "brush_tint_capability"};
    for (float channel : rgba) if (!std::isfinite(channel) || channel < 0 || channel > 1)
        return {Outcome::Input, "brush_tint_rgba"};
    if (!source.target_bound_ || source.slot_ >= state.reserved_images)
        return {Outcome::Input, "brush_tint_source"};
    TargetWork w{};
    w.operation = TargetOperation::Tint; w.authority = &a; w.target = source.affinity_;
    w.source = &state.retained[source.slot_]; w.tint_rgba = rgba;
    if (!w.source->published || w.source->kind == RecordKind::Prepared)
        return {Outcome::Input, "brush_tint_source"};
    if (!capture_current_uobject_identity_access(w.access) || !target_valid(w.access, a, w.target))
        return {Outcome::Identity, "brush_tint_target"};
    if (!sources_valid(w)) return latch({Outcome::NativeFault, "brush_tint_source_graph"});
    for (std::size_t i = 0; i < state.reserved_images; ++i) {
        const auto& cached = state.retained[i];
        if (!cached.published || cached.kind != RecordKind::Tinted || cached.original_slot != source.slot_
            || std::memcmp(cached.tint_rgba.data(), rgba.data(), sizeof(float) * 4) != 0) continue;
        if (!sources_valid(w) || !graph_valid(state.functions, w.access, cached))
            return latch({Outcome::NativeFault, "brush_tint_cache_graph"});
        out.slot_ = i; out.affinity_ = source.affinity_; out.target_bound_ = true;
        perf::count(perf::Counter::TintHits);
        return {Outcome::Ready, "tinted_brush_reused"};
    }
    if (!brush_facts(w.source->holder.pointer, w.source_facts))
        return {Outcome::Memory, "brush_tint_source_facts"};
    perf::count(perf::Counter::TintMisses);
    std::size_t slot = kInvalidImageSlot;
    const auto reservation = reserve_record(kRootedCarrierBytes, slot);
    if (!reservation) return reservation;
    w.record = &state.retained[slot];
    w.record->kind = RecordKind::Tinted;
    w.record->original_slot = source.slot_; w.record->tint_rgba = rgba;
    w.source_resource = w.source->texture; w.source_class = w.source->texture_class;
    const auto result = latch(guarded_target_sequence(w));
    if (result) {
        w.record->published = true;
        out.slot_ = slot; out.affinity_ = source.affinity_; out.target_bound_ = true;
    }
    return result;
}

Capabilities capabilities() noexcept
{
    std::unique_lock lock(state.mutex, std::defer_lock);
    if (!entered && !lock.try_lock()) return {};
    return {state.available && state.generic.classes, state.available && state.generic.styles,
        state.available && state.generic.color_assign != nullptr};
}
Result prepare_subjects(const CallbackAuthority& a) noexcept
{
    if (entered) return {Outcome::Busy, "prepare_subjects"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "prepare_subjects"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "prepare_subjects"};
    if (!admitted(a)) return {Outcome::Authority, "subject_prepare_admission"};
    objects::UObjectIdentityAccess access{}; Object main{};
    if (!capture_current_uobject_identity_access(access) || !main_identity(access, a, main))
        return {Outcome::Identity, "subject_prepare_owner"};
    const auto result = latch(ensure_generic_names(access, a));
    if (!result) return result;
    if (!admitted(a) || !unchanged(access, main)) {
        state.generic_names = {};
        return latch({Outcome::NativeFault, "post_subject_prepare_admission"});
    }
    return {Outcome::Ready, "subjects_prepared"};
}
static Result bind_row_impl(RowKind kind, void* row, std::int32_t logical_index, std::uint64_t setup_generation,
    std::uint64_t membership_epoch, const CallbackAuthority& a, std::uint64_t& id) noexcept
{
    id = 0;
    if (entered) return {Outcome::Busy, "bind_row"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "bind_row"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "bind_row"};
    if (!admitted(a)) return {Outcome::Authority, "row_setup_admission"};
    const bool uniform_speed = kind == RowKind::Speed && membership_epoch && logical_index == -1 && !setup_generation;
    if ((kind != RowKind::Song && kind != RowKind::Speed) || !row
        || (!uniform_speed && (membership_epoch || logical_index < 0 || !setup_generation)))
        return {Outcome::Input, "row_setup_facts"};
    objects::UObjectIdentityAccess access{};
    if (!capture_current_uobject_identity_access(access)) return {Outcome::Identity, "row_identity_service"};
    const auto names = latch(ensure_generic_names(access, a));
    if (!names) return names;
    RowBinding candidate{};
    candidate.association = {kind, logical_index, a.session.generation, setup_generation, membership_epoch};
    RowBinding* available = nullptr;
    // Invalidate BEFORE reread: even a failed rebind must retire the old setup ticket.
    if (state.rows) for (auto& old : *state.rows) {
        if (old.id && (old.row.pointer == row || old.association.session_generation != a.session.generation)) old = {};
        if (!old.id && !available) available = &old;
    }
    if (!main_identity(access, a, candidate.main) || !current_list(access, candidate.main.pointer, kind, candidate.list)
        || !capture(access, row, candidate.row) || candidate.row.live.serial_number <= 0)
        return {Outcome::Identity, "current_strong_row"};
    if (uniform_speed) {
        PointerArray array{};
        if (!pointer_array(candidate.list.pointer, 0x188, array)
            || !speed_tree(access, candidate.row, candidate.speed_tree, candidate.speed_root))
            return {Outcome::Identity, "speed_row_tree"};
        candidate.speed_members = array.data;
        candidate.speed_count = array.count; candidate.speed_capacity = array.capacity;
    }
    if (!row_member(access, candidate)) return {Outcome::Identity, "current_strong_row"};
    Object again{};
    if (!current_list(access, candidate.main.pointer, kind, again) || again.pointer != candidate.list.pointer
        || !unchanged(access, candidate.main) || !row_member(access, candidate) || !admitted(a))
        return latch({Outcome::NativeFault, "row_setup_reread"});
    // Reuse bindings from replaced lists/rows, not just same-pointer setup calls.
    // This is one bounded current-list projection, not a retained-row quota.
    PointerArray members{};
    const Address member_offset = kind == RowKind::Song ? 0x8d8 : 0x188;
    std::array<void*, kSubjectTraversalLimit> current{};
    if (!pointer_array(candidate.list.pointer, member_offset, members))
        return {Outcome::Identity, "row_members_projection"};
    for (std::int32_t i = 0; i < members.count; ++i)
        if (!read(members.data, static_cast<Address>(i) * sizeof(void*), current[static_cast<std::size_t>(i)]))
            return {Outcome::Identity, "row_members_projection"};
    if (!same_array(candidate.list.pointer, member_offset, members))
        return latch({Outcome::NativeFault, "row_members_projection_reread"});
    if (state.rows) for (auto& old : *state.rows) {
        if (old.id && old.association.kind == kind && (old.list.pointer != candidate.list.pointer
            || std::find(current.begin(), current.begin() + members.count, old.row.pointer) == current.begin() + members.count))
            old = {};
        if (!old.id && !available) available = &old;
    }
    if (state.row_nonce == std::numeric_limits<std::uint64_t>::max())
        return {Outcome::Budget, "row_ticket_overflow"};
    if (!available) {
        try {
            if (!state.rows) state.rows.emplace(LedgerAllocator<RowBinding>{&state.reserved_bytes, &state.pending_payload});
            state.rows->emplace_back();
            available = &state.rows->back();
        } catch (const LedgerBudgetExceeded&) { return {Outcome::Budget, "row_ledger_bytes"}; }
        catch (const std::bad_alloc&) { return {Outcome::Memory, "row_ledger_allocation"}; }
        catch (const std::length_error&) { return {Outcome::Memory, "row_ledger_length"}; }
    }
    if (!admitted(a) || !unchanged(access, candidate.main)
        || !current_list(access, candidate.main.pointer, kind, again) || again.pointer != candidate.list.pointer
        || !row_member(access, candidate)) return latch({Outcome::NativeFault, "post_row_ledger_admission"});
    candidate.id = ++state.row_nonce;
    *available = candidate;
    id = candidate.id;
    return {Outcome::Ready, "row_setup_bound"};
}
Result bind_row(RowKind kind, void* row, std::int32_t logical_index, std::uint64_t setup_generation,
    const CallbackAuthority& a, RowTicket& out) noexcept
{
    out = {};
    return bind_row_impl(kind, row, logical_index, setup_generation, 0, a, out.id_);
}
Result bind_speed_row(void* row, std::uint64_t membership_epoch, const CallbackAuthority& a, RowTicket& out) noexcept
{
    out = {};
    if (!membership_epoch) return {Outcome::Input, "speed_membership_epoch"};
    return bind_row_impl(RowKind::Speed, row, -1, 0, membership_epoch, a, out.id_);
}
Result read_speed_rows(const CallbackAuthority& a, SpeedRowsSnapshot& out) noexcept
{
    out = {};
    if (entered) return {Outcome::Busy, "read_speed_rows"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "read_speed_rows"};
    EntryScope entry;
    if (!state.available || !state.generic_names.ready) return {Outcome::Unavailable, "speed_row_names"};
    if (!admitted(a)) return {Outcome::Authority, "speed_row_read_admission"};
    objects::UObjectIdentityAccess access{}; Object main{}, list{}, again{}; PointerArray array{};
    if (!capture_current_uobject_identity_access(access) || !main_identity(access, a, main)
        || !current_list(access, main.pointer, RowKind::Speed, list)
        || !pointer_array(list.pointer, 0x188, array)) return {Outcome::Identity, "speed_row_list"};
    SpeedRowsSnapshot copied{};
    std::array<Object, 256> rows{}, trees{}, roots{};
    for (std::int32_t i = 0; i < array.count; ++i) {
        auto& row = rows[i];
        if (!read(array.data, static_cast<Address>(i) * sizeof(void*), copied.rows[i])
            || !capture(access, copied.rows[i], row) || !typed_object(access, row, NativeClass::UserWidget)
            || !speed_tree(access, row, trees[i], roots[i])) return {Outcome::Identity, "speed_row_member"};
        for (std::int32_t j = 0; j < i; ++j)
            if (copied.rows[j] == copied.rows[i]) return {Outcome::Identity, "speed_row_duplicate"};
    }
    for (std::int32_t i = 0; i < array.count; ++i) {
        void* member = nullptr; Object tree{}, root{};
        if (!read(array.data, static_cast<Address>(i) * sizeof(void*), member) || member != copied.rows[i]
            || !speed_tree(access, rows[i], tree, root) || tree.pointer != trees[i].pointer
            || root.pointer != roots[i].pointer || !unchanged(access, trees[i]) || !unchanged(access, roots[i]))
            return {Outcome::Identity, "speed_row_member_reread"};
    }
    if (!same_array(list.pointer, 0x188, array) || !unchanged(access, main) || !unchanged(access, list)
        || !current_list(access, main.pointer, RowKind::Speed, again) || again.pointer != list.pointer || !admitted(a))
        return {Outcome::Identity, "speed_row_list_reread"};
    copied.count = static_cast<std::size_t>(array.count);
    out = copied;
    return {Outcome::Ready, "speed_rows_read"};
}
Result find_subject(std::wstring_view leaf, SubjectKind kind, const RowTicket& row,
    const CallbackAuthority& a, Subject& out) noexcept
{
    out = {};
    if (entered) return {Outcome::Busy, "find_subject"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "find_subject"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "find_subject"};
    if (!admitted(a)) return {Outcome::Authority, "subject_admission"};
    objects::UObjectIdentityAccess access{}; Target facts{};
    if (!capture_current_uobject_identity_access(access)) return {Outcome::Identity, "subject_identity_service"};
    const auto result = latch(guarded_subject_find(leaf, kind, row.id_, a, access, facts));
    if (result) out.facts_ = facts;
    return result;
}
Result find_cursor_subject(std::wstring_view leaf, const CallbackAuthority& a, Subject& out, SubjectKind kind) noexcept
{
    out = {};
    if (entered) return {Outcome::Busy, "find_cursor_subject"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "find_cursor_subject"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "find_cursor_subject"};
    if (!admitted(a)) return {Outcome::Authority, "cursor_subject_admission"};
    objects::UObjectIdentityAccess access{}; Target facts{};
    if (!capture_current_uobject_identity_access(access)) return {Outcome::Identity, "cursor_identity_service"};
    const auto result = latch(guarded_subject_find(leaf, kind, 0, a, access, facts, true));
    if (result) out.facts_ = facts;
    return result;
}
bool validate_subject(const Subject& subject, const MenuSessionSnapshot& current) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::ValidateSubject);
    const bool result = [&]() noexcept {
    std::unique_lock lock(state.mutex, std::defer_lock);
    // Safe nested READ inside a native helper/palette detour; never a nested
    // mutation entry or a saved callback authority. No native functions here.
    if (!entered && !lock.try_lock()) return false;
    EntryScope entry;
    if (!state.available || state.poisoned || !state.generic.classes || !state.generic_names.ready) return false;
    objects::UObjectIdentityAccess access{};
    CallbackAuthority facts_only{}; facts_only.session = current;
    return capture_current_uobject_identity_access(access)
        && target_valid(access, facts_only, subject.facts_, true)
        && state.available && !state.poisoned && session_current(current);
    }();
    perf::finish(stamp, result);
    return result;
}
bool subject_row_association(const Subject& subject, RowAssociation& out) noexcept
{
    out = {};
    std::unique_lock lock(state.mutex, std::defer_lock);
    if (!entered && !lock.try_lock()) return false;
    const auto* binding = row_binding(subject.facts_.row_id);
    if (!binding) return false;
    out = binding->association; // diagnostic provenance only; not validation/admission
    return true;
}
Result access_subject(const Subject& subject, const CallbackAuthority& a, void*& object) noexcept
{
    object = nullptr;
    if (entered) return {Outcome::Busy, "access_subject"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "access_subject"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "access_subject"};
    if (!admitted(a)) return {Outcome::Authority, "subject_access_admission"};
    objects::UObjectIdentityAccess access{};
    if (!capture_current_uobject_identity_access(access) || !target_valid(access, a, subject.facts_))
        return {Outcome::Identity, "subject_access_identity"};
    object = subject.facts_.target.pointer;
    return {Outcome::Ready, "subject_borrowed_in_callback"};
}
Result capture_original(const Subject& subject, BrushSelection selection, const CallbackAuthority& a, OwnedImage& out) noexcept
{
    CaptureProgress progress{};
    return capture_original_members(subject, std::span<const BrushSelection>(&selection, 1),
        a, std::span<OwnedImage>(&out, 1), progress);
}
Result apply(const Subject& subject, BrushSelection selection, const OwnedImage& image, const CallbackAuthority& a) noexcept
{
    if (subject.facts_.kind == SubjectKind::Button || subject.facts_.kind == SubjectKind::ImageSet) {
        const BrushAssignment member{selection, &image};
        ApplyProgress progress{};
        return apply_members(subject, std::span<const BrushAssignment>(&member, 1), a, progress);
    }
    perf::Span metric(perf::Metric::Apply);
    perf::count(perf::Counter::ApplyRequestedMembers);
    if (entered) return {Outcome::Busy, "apply_subject"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "apply_subject"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "apply_subject"};
    if (!admitted(a)) return {Outcome::Authority, "apply_subject_admission"};
    if (image.slot_ >= state.reserved_images || !state.retained[image.slot_].published)
        return {Outcome::Input, "retained_brush"};
    TargetWork w{};
    w.operation = TargetOperation::Apply; w.authority = &a; w.target = subject.facts_;
    w.record = &state.retained[image.slot_];
    if (!capture_current_uobject_identity_access(w.access) || !select_member(w.target, selection)
        || !target_valid(w.access, a, w.target)) return {Outcome::Identity, "apply_subject_member"};
    if (w.record->kind != RecordKind::Prepared
        && (!image.target_bound_ || !same_target(image.affinity_, w.target)))
        return {Outcome::Identity, "original_owner_binding"};
    if (!graph_valid(state.functions, w.access, *w.record)) return latch({Outcome::NativeFault, "apply_source_graph"});
    w.prevalidated = true;
    return latch(guarded_target_sequence(w));
}
Result apply_members(const Subject& subject, std::span<const BrushAssignment> assignments,
    const CallbackAuthority& a, ApplyProgress& progress) noexcept
{
    perf::Span metric(perf::Metric::ApplyMembers);
    perf::count(perf::Counter::ApplyRequestedMembers, assignments.size());
    progress = {};
    if (entered) return {Outcome::Busy, "apply_members"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "apply_members"};
    EntryScope entry;
    if (!state.available || !state.generic.styles) return {Outcome::Unavailable, "apply_members"};
    if (!admitted(a)) return {Outcome::Authority, "batch_admission"};
    const auto& target = subject.facts_;
    const bool button = target.kind == SubjectKind::Button;
    if (!target.generic || (!button && target.kind != SubjectKind::ImageSet) || assignments.empty()
        || assignments.size() > (button ? 4 : kSubjectTraversalLimit)) return {Outcome::Input, "batch_members"};
    BatchWork w{};
    w.authority = &a; w.count = assignments.size();
    if (!capture_current_uobject_identity_access(w.access)) return {Outcome::Identity, "batch_identity_service"};
    for (std::size_t i = 0; i < assignments.size(); ++i) {
        const auto* image = assignments[i].image;
        if (!image || image->slot_ >= state.reserved_images || !state.retained[image->slot_].published)
            return {Outcome::Input, "batch_retained_brush"};
        Target member = target;
        if (!select_member(member, assignments[i].member)) return {Outcome::Input, "batch_member_selection"};
        if (!i) w.target = member;
        else if (member.images != w.target.images || member.image_count != w.target.image_count
            || member.image_capacity != w.target.image_capacity) return {Outcome::Identity, "batch_member_header"};
        for (std::size_t j = 0; j < i; ++j)
            if (button ? w.members[j].offset == member.brush_offset : w.members[j].index == member.image_index)
                return {Outcome::Input, "batch_duplicate_member"};
        auto& m = w.members[i];
        m.source = &state.retained[image->slot_]; m.offset = member.brush_offset; m.index = member.image_index;
        if (m.source->kind != RecordKind::Prepared
            && (!image->target_bound_ || !same_target(image->affinity_, member)))
            return {Outcome::Identity, "batch_original_owner_binding"};
        bool duplicate = false;
        for (std::size_t j = 0; j < i; ++j) if (w.members[j].source == m.source) duplicate = true;
        if (!duplicate && !graph_valid(state.functions, w.access, *m.source))
            return latch({Outcome::NativeFault, "batch_source_graph"});
        if (!read(batch_brush(w, m), 0x48, m.original_resource)) return {Outcome::Memory, "batch_member_resource"};
    }
    if (!member_frame(w.target, w.before)) return {Outcome::Memory, "batch_member_frame"};
    if (!button) for (std::int32_t i = 0; i < w.target.image_count; ++i) {
        const auto* brush = reinterpret_cast<const std::uint8_t*>(w.target.images + static_cast<Address>(i) * 0xb0);
        auto& geometry = w.geometry[static_cast<std::size_t>(i)];
        if (!read_brush(brush, w.brushes[static_cast<std::size_t>(i)])
            || !core::safe_copy_bytes(brush + 0x88, geometry.data(), geometry.size()))
            return {Outcome::Memory, "batch_entry_frame"};
    }
    // No native helper/allocation separated selection, exact affinity checks and
    // these reads: one fresh containment admission covers this private phase.
    if (!target_valid(w.access, a, w.target)) return {Outcome::Identity, "batch_current_subject"};
    const auto result = guarded_batch_sequence(w);
    progress = w.progress;
    return latch(result);
}
Result capture_original_members(const Subject& subject, std::span<const BrushSelection> selections,
    const CallbackAuthority& a, std::span<OwnedImage> outputs, CaptureProgress& progress) noexcept
{
    perf::Span metric(perf::Metric::CaptureMembers);
    perf::count(perf::Counter::CaptureRequestedMembers, selections.size());
    for (auto& output : outputs) output = {};
    progress = {};
    if (entered) return {Outcome::Busy, "capture_original_members"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "capture_original_members"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "capture_original_members"};
    if (!admitted(a)) return {Outcome::Authority, "capture_members_admission"};
    const auto& base = subject.facts_;
    const auto limit = base.kind == SubjectKind::Button ? std::size_t{4}
        : base.kind == SubjectKind::ImageSet ? kSubjectTraversalLimit : std::size_t{1};
    if (!base.generic || selections.empty() || selections.size() > limit || outputs.size() != selections.size())
        return {Outcome::Input, "capture_members_inputs"};
    if (base.kind != SubjectKind::Image && !state.generic.styles)
        return {Outcome::Unavailable, "subject_style_capability"};
    struct Member {
        Address offset = 0;
        std::int32_t index = 0;
        BrushFacts facts{};
        Object resource{}, klass{};
        std::size_t slot = kInvalidImageSlot;
    };
    std::array<Member, kSubjectTraversalLimit> members{};
    Target common{};
    objects::UObjectIdentityAccess access{};
    if (!capture_current_uobject_identity_access(access)) return {Outcome::Identity, "capture_members_identity"};
    for (std::size_t i = 0; i < selections.size(); ++i) {
        Target selected = base;
        if (!select_member(selected, selections[i])) return {Outcome::Input, "capture_member_selection"};
        if (!i) common = selected;
        else if (selected.images != common.images || selected.image_count != common.image_count
            || selected.image_capacity != common.image_capacity) return {Outcome::Identity, "capture_member_header"};
        for (std::size_t j = 0; j < i; ++j)
            if (members[j].offset == selected.brush_offset && members[j].index == selected.image_index)
                return {Outcome::Input, "capture_duplicate_member"};
        members[i].offset = selected.brush_offset; members[i].index = selected.image_index;
    }
    const auto target_for = [&](const Member& m) noexcept {
        Target t = common; t.brush_offset = m.offset; t.image_index = m.index; return t;
    };
    if (!target_valid(access, a, common)) return {Outcome::Identity, "capture_members_subject"};
    bool uninterrupted = true;
    for (std::size_t i = 0; i < selections.size(); ++i) {
        auto& m = members[i];
        if (!read_brush(target_brush(target_for(m)), m.facts)) return {Outcome::Memory, "capture_member_facts"};
        void* resource = nullptr;
        std::memcpy(&resource, m.facts.data() + 0x48, sizeof(resource));
        if (resource && (!capture(access, resource, m.resource) || !capture(access, m.resource.klass, m.klass)))
            return {Outcome::Identity, "capture_member_resource"};
    }
    const auto source_same = [&](const Member& m) noexcept {
        BrushFacts now{};
        return read_brush(target_brush(target_for(m)), now) && now == m.facts && resource_valid(access, m.resource, m.klass);
    };
    const auto cached = [&](Member& m) noexcept -> Result {
        m.slot = kInvalidImageSlot;
        for (std::size_t slot = 0; slot < state.reserved_images; ++slot) {
            const auto& r = state.retained[slot];
            if (!r.published || r.kind != RecordKind::Original) continue;
            BrushFacts owned{};
            if (!brush_facts(r.holder.pointer, owned)) return {Outcome::NativeFault, "capture_cache_facts"};
            if (owned != m.facts) continue;
            // Comparison is never ownership: even exact bytes need the rooted graph.
            if (!graph_valid(state.functions, access, r)) return {Outcome::NativeFault, "capture_cache_graph"};
            m.slot = slot; break;
        }
        return {Outcome::Ready, "capture_cache_checked"};
    };
    CaptureProgress current{};
    for (std::size_t i = 0; i < selections.size(); ++i) {
        auto& m = members[i];
        const auto hit = cached(m);
        if (!hit) { progress = current; return latch(hit); }
        if (m.slot == kInvalidImageSlot) {
            perf::count(perf::Counter::CaptureMisses);
            uninterrupted = false; // Allocation/native work ends the shared read-only proof.
            const auto reserved = reserve_record(kRootedCarrierBytes, m.slot);
            if (!reserved) { progress = current; return reserved; }
            // C++ allocation can reenter too. Never dereference stale sources or
            // invoke native construction using the preceding read-only phase.
            if (!target_valid(access, a, target_for(m)) || !source_same(m)) {
                progress = current; return latch({Outcome::NativeFault, "post_capture_members_allocation"});
            }
            TargetWork work{};
            work.operation = TargetOperation::Capture; work.authority = &a; work.access = access;
            work.target = target_for(m); work.source_facts = m.facts;
            work.source_resource = m.resource; work.source_class = m.klass;
            work.record = &state.retained[m.slot]; work.record->kind = RecordKind::Original;
            const auto captured = guarded_target_sequence(work);
            if (!captured) { progress = current; return latch(captured); }
            work.record->published = true;
            ++current.created_members;
        } else perf::count(perf::Counter::CaptureHits);
        ++current.prepared_members;
    }
    for (std::size_t i = 0; i < selections.size(); ++i) {
        const auto& m = members[i];
        if (!source_same(m) || !graph_valid(state.functions, access, state.retained[m.slot])) {
            progress = current; return latch({Outcome::NativeFault, "capture_members_final_source"});
        }
    }
    const bool current_subject = uninterrupted && a.phase_current
        ? state.available && !state.poisoned && a.game_thread_id == GetCurrentThreadId()
            && session_current(a.session) && a.phase_current(a.context, a.session)
        : target_valid(access, a, common);
    if (!current_subject) {
        progress = current; return latch({Outcome::NativeFault, "capture_members_final_subject"});
    }
    for (std::size_t i = 0; i < selections.size(); ++i) {
        outputs[i].slot_ = members[i].slot; outputs[i].affinity_ = target_for(members[i]); outputs[i].target_bound_ = true;
    }
    current.published_members = selections.size(); progress = current;
    perf::count(perf::Counter::CapturePublishedMembers, current.published_members);
    return {Outcome::Ready, "original_members_retained"};
}
namespace {
Result resolve_record(TargetWork& w, RecordKind kind, std::size_t source_slot,
    std::size_t prepared_slot, BatchProgress& progress, std::size_t& slot) noexcept
{
    if (!sources_valid(w)) return {Outcome::Identity, "resolve_current_source"};
    if (kind == RecordKind::Original) {
        if (!read_brush(target_brush(w.target), w.source_facts)) return {Outcome::Memory, "resolve_capture_facts"};
        void* resource = nullptr;
        std::memcpy(&resource, w.source_facts.data() + 0x48, sizeof(resource));
        if (resource && (!capture(w.access, resource, w.source_resource)
            || !capture(w.access, w.source_resource.klass, w.source_class))) return {Outcome::Identity, "resolve_capture_resource"};
    } else {
        if (!brush_facts(w.source->holder.pointer, w.source_facts)) return {Outcome::Memory, "resolve_source_facts"};
        w.source_resource = w.source->texture; w.source_class = w.source->texture_class;
    }
    for (std::size_t i = 0; i < state.reserved_images; ++i) {
        const auto& r = state.retained[i];
        if (!r.published || r.kind != kind) continue;
        if (kind == RecordKind::Original) {
            BrushFacts owned{};
            if (!brush_facts(r.holder.pointer, owned)) return latch({Outcome::NativeFault, "resolve_cache_facts"});
            if (owned != w.source_facts) continue;
        } else if (r.original_slot != source_slot || (kind == RecordKind::Composed
            ? r.prepared_slot != prepared_slot
            : std::memcmp(r.tint_rgba.data(), w.tint_rgba.data(), sizeof(float) * 4) != 0)) continue;
        if (!graph_valid(state.functions, w.access, r)) return latch({Outcome::NativeFault, "resolve_cache_graph"});
        slot = i;
        perf::count(kind == RecordKind::Original ? perf::Counter::CaptureHits
            : kind == RecordKind::Composed ? perf::Counter::ComposeHits : perf::Counter::TintHits);
        return {Outcome::Ready, "resolve_cache_hit"};
    }
    perf::count(kind == RecordKind::Original ? perf::Counter::CaptureMisses
        : kind == RecordKind::Composed ? perf::Counter::ComposeMisses : perf::Counter::TintMisses);
    const auto reserved = reserve_record(kRootedCarrierBytes, slot);
    if (!reserved) return reserved;
    // Allocators, construction listeners and every owning helper end the phase.
    if (!sources_valid(w)) return {Outcome::Identity, "resolve_post_allocation_source"};
    w.record = &state.retained[slot];
    w.record->kind = kind == RecordKind::Composed ? RecordKind::Original : kind;
    w.record->original_slot = source_slot; w.record->prepared_slot = prepared_slot;
    w.record->tint_rgba = w.tint_rgba;
    const auto result = latch(guarded_target_sequence(w));
    if (result) {
        w.record->kind = kind; w.record->published = true;
        ++progress.created;
    }
    return result;
}
}
Result resolve_members(std::span<const ResolveMember> requests, const CallbackAuthority& a,
    std::span<ResolvedMember> outputs, BatchProgress& progress) noexcept
{
    perf::Span metric(perf::Metric::Resolve);
    progress = {};
    for (auto& output : outputs) output = {};
    if (entered) return {Outcome::Busy, "resolve_members"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "resolve_members"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "resolve_members"};
    if (requests.empty() || requests.size() > kSubjectTraversalLimit || outputs.size() != requests.size())
        return {Outcome::Input, "resolve_member_count"};
    ReadScope reads(a);
    if (!admitted(a)) return {Outcome::Authority, "resolve_admission"};
    struct Plan { Target target{}; std::size_t original = kInvalidImageSlot, image = kInvalidImageSlot; };
    std::array<Plan, kSubjectTraversalLimit> plans{};
    objects::UObjectIdentityAccess access{};
    if (!capture_current_uobject_identity_access(access)) return {Outcome::Identity, "resolve_identity_service"};
    // Validate the complete request shape and existing inputs before ANY carrier
    // construction. Texture upload remains exclusively upstream in create().
    for (std::size_t i = 0; i < requests.size(); ++i) {
        progress.failed_index = i;
        const auto& request = requests[i];
        if (!request.subject) return {Outcome::Input, "resolve_subject"};
        auto& plan = plans[i]; plan.target = request.subject->facts_;
        if (!plan.target.generic || !select_member(plan.target, request.member)
            || (plan.target.kind != SubjectKind::Image && !state.generic.styles)) return {Outcome::Input, "resolve_member"};
        if (request.original) {
            const auto& image = *request.original;
            if (image.slot_ >= state.reserved_images || !state.retained[image.slot_].published
                || state.retained[image.slot_].kind != RecordKind::Original || !image.target_bound_
                || !same_target(image.affinity_, plan.target)) return {Outcome::Identity, "resolve_original_affinity"};
            plan.original = image.slot_;
        }
        if (request.prepared && (request.prepared->slot_ >= state.reserved_images
            || !state.retained[request.prepared->slot_].published
            || state.retained[request.prepared->slot_].kind != RecordKind::Prepared)) return {Outcome::Input, "resolve_prepared_kind"};
        if (request.tint) {
            if (!state.generic.color_assign) return {Outcome::Unavailable, "resolve_tint_capability"};
            for (float channel : request.linear_rgba) if (!std::isfinite(channel) || channel < 0 || channel > 1)
                return {Outcome::Input, "resolve_tint_rgba"};
        }
        if (!target_valid(access, a, plan.target)) return {Outcome::Identity, "resolve_target"};
        if (request.original && !graph_valid(state.functions, access, state.retained[plan.original]))
            return latch({Outcome::NativeFault, "resolve_original_graph"});
        if (request.prepared && !graph_valid(state.functions, access, state.retained[request.prepared->slot_]))
            return latch({Outcome::NativeFault, "resolve_prepared_graph"});
    }
    for (std::size_t i = 0; i < requests.size(); ++i) {
        progress.failed_index = i;
        const auto& request = requests[i]; auto& plan = plans[i];
        TargetWork w{}; w.authority = &a; w.access = access; w.target = plan.target;
        w.operation = TargetOperation::Capture;
        if (!request.original) {
            perf::Span capture_metric(perf::Metric::CaptureMembers);
            perf::count(perf::Counter::CaptureRequestedMembers);
            const auto result = resolve_record(w, RecordKind::Original, kInvalidImageSlot, kInvalidImageSlot, progress, plan.original);
            if (!result) return result;
        }
        plan.image = plan.original;
        if (request.prepared) {
            perf::Span compose_metric(perf::Metric::Compose);
            w.operation = TargetOperation::Compose; w.source = &state.retained[plan.original];
            w.prepared = &state.retained[request.prepared->slot_];
            const auto result = resolve_record(w, RecordKind::Composed, plan.original, request.prepared->slot_, progress, plan.image);
            if (!result) return result;
        }
        if (request.tint) {
            perf::Span tint_metric(perf::Metric::Tint);
            w.operation = TargetOperation::Tint; w.source = &state.retained[plan.image];
            w.prepared = nullptr; w.tint_rgba = request.linear_rgba;
            const auto source_slot = plan.image;
            const auto result = resolve_record(w, RecordKind::Tinted, source_slot, kInvalidImageSlot, progress, plan.image);
            if (!result) return result;
        }
        ++progress.completed;
    }
    // A later request can cross a helper after earlier results were resolved.
    // Reacquire their exact Subjects/graphs in the final uninterrupted phase.
    for (std::size_t i = 0; i < requests.size(); ++i) {
        progress.failed_index = i;
        const auto& plan = plans[i];
        if (!target_valid(access, a, plan.target)) return {Outcome::Identity, "resolve_final_target"};
        if (!graph_valid(state.functions, access, state.retained[plan.original])
            || !graph_valid(state.functions, access, state.retained[plan.image]))
            return latch({Outcome::NativeFault, "resolve_final_graph"});
        if (!requests[i].original) {
            BrushFacts live{}, retained{};
            if (!read_brush(target_brush(plan.target), live)
                || !brush_facts(state.retained[plan.original].holder.pointer, retained) || live != retained)
                return latch({Outcome::NativeFault, "resolve_final_capture_source"});
        }
    }
    for (std::size_t i = 0; i < requests.size(); ++i) {
        progress.failed_index = i;
        if (!finish_target(access, a, plans[i].target)) return {Outcome::Identity, "resolve_final_target"};
    }
    if (!finish_reads(access, a)) return {Outcome::Authority, "resolve_final_admission"};
    for (std::size_t i = 0; i < requests.size(); ++i) {
        auto& output = outputs[i]; const auto& plan = plans[i];
        output.original.slot_ = plan.original; output.original.affinity_ = plan.target; output.original.target_bound_ = true;
        output.image.slot_ = plan.image; output.image.affinity_ = plan.target; output.image.target_bound_ = true;
        if (!requests[i].original) perf::count(perf::Counter::CapturePublishedMembers);
    }
    progress.failed_index = kInvalidImageSlot;
    return {Outcome::Ready, "members_resolved"};
}
Result preflight_subjects(std::span<const SubjectPreflight> groups, const CallbackAuthority& a,
    BatchProgress& progress) noexcept
{
    progress = {};
    if (entered) return {Outcome::Busy, "preflight_subjects"};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy, "preflight_subjects"};
    EntryScope entry;
    if (!state.available) return {Outcome::Unavailable, "preflight_subjects"};
    if (groups.empty() || groups.size() > kSubjectTraversalLimit) return {Outcome::Input, "preflight_group_count"};
    std::size_t image_count = 0;
    for (const auto& group : groups) {
        if (!group.subject || group.images.size() > kSubjectTraversalLimit - image_count) return {Outcome::Input, "preflight_inputs"};
        image_count += group.images.size();
    }
    ReadScope reads(a);
    if (!admitted(a)) return {Outcome::Authority, "preflight_admission"};
    objects::UObjectIdentityAccess access{};
    if (!capture_current_uobject_identity_access(access)) return {Outcome::Identity, "preflight_identity_service"};
    for (std::size_t i = 0; i < groups.size(); ++i) {
        progress.failed_index = i;
        const auto& group = groups[i]; const auto& target = group.subject->facts_;
        if (group.images.empty() && !target_valid(access, a, target)) return {Outcome::Identity, "preflight_subject"};
        for (const auto& member : group.images) {
            const auto* image = member.image;
            if (!image || image->slot_ >= state.reserved_images || !state.retained[image->slot_].published)
                return {Outcome::Input, "preflight_image"};
            Target selected = target;
            if (!select_member(selected, member.member) || !target_valid(access, a, selected)) return {Outcome::Identity, "preflight_member"};
            const auto& record = state.retained[image->slot_];
            if (record.kind != RecordKind::Prepared && (!image->target_bound_ || !same_target(image->affinity_, selected)))
                return {Outcome::Identity, "preflight_affinity"};
            if (!graph_valid(state.functions, access, record)) return latch({Outcome::NativeFault, "preflight_graph"});
        }
        ++progress.completed;
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
        progress.failed_index = i;
        const auto& group = groups[i];
        if (group.images.empty() && !finish_target(access, a, group.subject->facts_))
            return {Outcome::Identity, "preflight_final_subject"};
        for (const auto& member : group.images) {
            Target selected = group.subject->facts_;
            if (!select_member(selected, member.member) || !finish_target(access, a, selected))
                return {Outcome::Identity, "preflight_final_member"};
        }
    }
    if (!finish_reads(access, a)) return {Outcome::Authority, "preflight_final_admission"};
    progress.failed_index = kInvalidImageSlot;
    return {Outcome::Ready, "subjects_preflighted"};
}
static Result prepare_song_hud_names(const CallbackAuthority& a) noexcept
{
    if (entered) return {Outcome::Busy,"hud_names"};
    std::unique_lock lock(state.mutex,std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy,"hud_names"};
    EntryScope entry;
    if (!admitted(a)) return {Outcome::Authority,"hud_names_admission"};
    objects::UObjectIdentityAccess access{};
    if (!capture_current_uobject_identity_access(access)) return {Outcome::Identity,"hud_names_identity"};
    const auto prepared=ensure_generic_names(access,a);
    if (!prepared) return prepared;
    std::uint64_t title=0,panel=0;
    if (!find_name(state.functions.fname,L"Txt_MusicTitle",title) || !admitted(a)
        || !find_name(state.functions.fname,L"Pnl_MusicTitle",panel) || !admitted(a))
        return {Outcome::Unavailable,"hud_names_not_published"};
    hud_title_name=title; hud_panel_name=panel;
    return {Outcome::Ready,"hud_names"};
}

Result prepare_song_hud_subjects(const CallbackAuthority& a) noexcept
{
    const auto names=prepare_song_hud_names(a);
    if (!names) return names;
    for (std::size_t i=0;i<kSongHudIconCount;++i) {
        {
            std::unique_lock lock(state.mutex,std::try_to_lock);
            if (!lock.owns_lock()) return {Outcome::Busy,"hud_icons"};
            if (!admitted(a)) return {Outcome::Authority,"hud_icons_admission"};
            if (state.hud_icons_ready[i]) continue;
        }
        // CPU images are immutable after configure; creation still needs real menu authority.
        OwnedImage image;
        const auto prepared=create(state.hud_icon_pixels[i],a,image);
        if (!prepared) return prepared;
        std::unique_lock lock(state.mutex,std::try_to_lock);
        if (!lock.owns_lock()) return {Outcome::Busy,"hud_icons_publish"};
        if (!admitted(a)) return {Outcome::Authority,"hud_icons_publish"};
        state.hud_icons[i]=image; state.hud_icons_ready[i]=true;
    }
    return {Outcome::Ready,"hud_names"};
}

namespace {
template<class F, class... Args>
bool hud_native_call(F function, Args... args) noexcept
{
    __try { function(args...); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool hud_dpi_call(HudCanvasDpi function,void* canvas,float& out) noexcept
{
    __try { out=function(canvas); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct HudSharpText {
    HudSharpFunctions functions{};
    std::uint64_t typeface=0;
    std::int32_t size=0;
    float dpi=0;
};
template<class Measure>
Result hud_fit_font_size(float target,HudSharpText& sharp,Measure&& measure,float& height)
{
    if (!std::isfinite(target) || target<=0 || !std::isfinite(sharp.dpi) || sharp.dpi<=0)
        return {Outcome::Input,"hud_font_pixel_size_input"};
    int low=1,high=static_cast<int>(std::clamp(std::ceil(target/sharp.dpi),1.0f,128.0f));
    int chosen=0;
    while (low<=high) {
        sharp.size=low+(high-low)/2;
        HudPosition size{};
        if (auto r=measure(size); !r) return r;
        if (!std::isfinite(size.y) || size.y<=0) return {Outcome::Unavailable,"hud_font_measure_invalid"};
        if (size.y<=target) { chosen=sharp.size; height=size.y; low=sharp.size+1; }
        else high=sharp.size-1;
    }
    sharp.size=chosen;
    return chosen ? Result{Outcome::Ready,"hud_font_pixel_size"}
        : Result{Outcome::Unavailable,"hud_font_pixel_size_no_fit"};
}
struct HudTextWork {
    HudTextFunctions functions{};
    const void* context = nullptr;
    bool (*current)(const void*) noexcept = nullptr;
    void* canvas = nullptr;
    void* font = nullptr;
    const wchar_t* value = nullptr;
    HudPosition position{};
    runtime_layouts::FLinearColor color{};
    float scale = 0;
    const HudSharpText* sharp = nullptr;
    HudPosition* measurement = nullptr; // Copy only; native item never escapes.
};
Result hud_scaled_text(const HudTextWork& w) noexcept
{
    if (!w.functions) return {Outcome::Unavailable,"hud_text_contract"};
    if (!w.current || !w.current(w.context)) return {Outcome::Authority,"hud_text_admission"};
    if (!w.canvas || !w.font || !w.value || !std::isfinite(w.position.x) || !std::isfinite(w.position.y)
        || !std::isfinite(w.scale) || w.scale<=0 || !std::isfinite(w.color.r)
        || !std::isfinite(w.color.g) || !std::isfinite(w.color.b) || !std::isfinite(w.color.a)
        || w.color.a<0 || w.color.a>1) return {Outcome::Input,"hud_text_input"};
    // Pure renderer owns this terminated string for the entire synchronous draw.
    std::int32_t length=0;
    while (length<4096 && w.value[length]) ++length;
    if (length==4096) return {Outcome::Input,"hud_text_length"};
    if (!length) return {Outcome::Ready,"hud_text_empty"};
    if (w.sharp) {
        if (!w.sharp->functions) return {Outcome::Unavailable,"hud_font_info_contract"};
        if (w.sharp->size<1 || w.sharp->size>128 || w.scale!=1
            || !std::isfinite(w.sharp->dpi) || w.sharp->dpi<=0)
            return {Outcome::Input,"hud_font_info_input"};
        float dpi=0;
        if (!hud_dpi_call(w.sharp->functions.dpi,w.canvas,dpi))
            return {Outcome::Unavailable,"hud_font_dpi_fault"};
        if (!w.current(w.context) || dpi!=w.sharp->dpi)
            return {Outcome::Authority,"hud_font_dpi_changed"};
    } else if (w.measurement) return {Outcome::Unavailable,"hud_font_info_contract"};
    if (!song_hud_font_draw_scale_allowed(w.font,w.scale))
        return {Outcome::Authority,"hud_font_scale"};
    const HudString source{w.value,length+1,length+1};
    static_assert(sizeof(HudString)==0x10 && offsetof(HudString,num)==8 && offsetof(HudString,max)==0xc);
    alignas(16) std::array<std::uint8_t,0x18> text{};
    alignas(16) std::array<std::uint8_t,0x1b0> item{};
    bool text_owned=false, item_owned=false;
    const auto admission=[&](const char* stage) -> Result {
        if (w.current(w.context)) return {Outcome::Ready,stage};
        // Lost lifetime authority forbids native cleanup of a still-owned local.
        return {text_owned || item_owned ? Outcome::NativeFault : Outcome::Authority,stage};
    };
    if (auto r=admission("hud_text_before_construct"); !r) return r;
    if (!hud_native_call(w.functions.from_string,text.data(),&source))
        return {Outcome::NativeFault,"hud_text_construct_fault"};
    text_owned=true;
    if (auto r=admission("hud_text_before_item"); !r) return r;
    if (!hud_native_call(w.functions.construct,item.data(),&w.position,text.data(),w.font,&w.color))
        return {Outcome::NativeFault,"hud_text_item_construct_fault"};
    item_owned=true;
    if (auto r=admission("hud_text_before_text_destroy"); !r) return r;
    if (!hud_native_call(w.functions.destroy_text,text.data()))
        return {Outcome::NativeFault,"hud_text_destroy_fault"};
    text_owned=false;
    if (w.sharp) {
        if (auto r=admission("hud_font_info_before_construct"); !r) return r;
        if (item[0x1a8]) return {Outcome::NativeFault,"hud_font_optional_not_empty"};
        struct alignas(8) Outline {
            std::array<std::uint8_t,16> flags{};
            runtime_layouts::FLinearColor color{0,0,0,1};
        } outline;
        static_assert(sizeof(Outline)==0x20);
        if (!hud_native_call(w.sharp->functions.construct,item.data()+0x150,w.font,
                w.sharp->size,&w.sharp->typeface,&outline))
            return {Outcome::NativeFault,"hud_font_info_construct_fault"};
        item[0x1a8]=1; // Successful native construction transfers this local owner to item destruction.
    }
    if (auto r=admission("hud_text_before_submit"); !r) return r;
    // Only the authenticated local Scale pair is changed; foreground is native-constructed RGBA.
    std::memcpy(item.data()+0xa0,&w.scale,sizeof(w.scale));
    std::memcpy(item.data()+0xa4,&w.scale,sizeof(w.scale));
    HudPosition measured{NAN,NAN};
    if (w.measurement) {
        if (!hud_native_call(w.sharp->functions.measure,item.data(),&measured,w.sharp->dpi))
            return {Outcome::NativeFault,"hud_font_measure_fault"};
    } else if (!hud_native_call(w.functions.submit,w.canvas,item.data()))
        return {Outcome::NativeFault,"hud_text_submit_fault"};
    if (auto r=admission("hud_text_before_item_destroy"); !r) return r;
    if (!hud_native_call(w.functions.destroy_item,item.data()))
        return {Outcome::NativeFault,"hud_text_item_destroy_fault"};
    item_owned=false;
    if (auto r=admission("hud_text_submitted"); !r) return r;
    if (w.measurement) {
        if (!std::isfinite(measured.x) || measured.x<0 || !std::isfinite(measured.y) || measured.y<=0)
            return {Outcome::Unavailable,"hud_font_measure_invalid"};
        *w.measurement=measured;
    }
    return {Outcome::Ready,"hud_text_submitted"};
}
bool hud_resource(const Record& record,const objects::UObjectIdentityAccess& access,const void*& resource) noexcept
{
    resource=nullptr;
    void* platform=nullptr; std::int32_t mips=0,w=0,h=0;
    const void* candidate=nullptr; const void* again=nullptr;
    if (!record.published || record.kind!=RecordKind::Prepared
        || record.creation_thread!=GetCurrentThreadId() || !graph_valid_sequence(state.functions,access,record)
        || !read(record.texture.pointer,0x138,platform) || !platform
        || !read(platform,0,w) || !read(platform,4,h) || !read(platform,0x20,mips)
        || w!=static_cast<std::int32_t>(record.width) || h!=static_cast<std::int32_t>(record.height) || mips!=1
        || !read(record.texture.pointer,0xa8,candidate) || !candidate
        || !read(record.texture.pointer,0xa8,again) || again!=candidate) return false;
    resource=candidate;
    return true;
}
bool hud_path(const objects::UObjectIdentityAccess& access, const Target& t,
    const PanelBases& bases) noexcept
{
    std::array<ContainmentEdge,kContainmentDepth> path{};
    std::size_t count=0;
    Object child=t.target;
    while (child.pointer!=t.root.pointer) {
        if (count==path.size()) return false;
        for (std::size_t i=0;i<count;++i) if (path[i].child.pointer==child.pointer) return false;
        auto& e=path[count++]; e.child=child; e.index=-1;
        void* slot=nullptr; void* parent=nullptr; void* content=nullptr;
        if (!unchanged(access,child) || !read(child.pointer,0x28,slot)
            || !capture(access,slot,e.slot) || !ancestry(access,e.slot,bases.slot,bases,e.slot_type)
            || !read(slot,0x28,parent) || !capture(access,parent,e.parent)
            || !ancestry(access,e.parent,bases.panel,bases,e.parent_type)
            || !read(slot,0x30,content) || content!=child.pointer
            || !read(parent,0x108,e.slots) || !e.slots
            || !read(parent,0x110,e.count) || e.count<=0 || e.count>kPanelSlots
            || !read(parent,0x114,e.capacity) || e.capacity<e.count || e.capacity>kPanelSlots) return false;
        for (std::int32_t i=0;i<e.count;++i) {
            void* item=nullptr;
            if (!read(e.slots,static_cast<Address>(i)*sizeof(void*),item)) return false;
            if (item==slot) { e.index=i; break; }
        }
        if (e.index<0) return false;
        child=e.parent;
    }
    for (std::size_t i=0;i<count;++i) if (!edge_unchanged(access,path[i])) return false;
    return bases_unchanged(access,bases) && unchanged(access,t.root) && unchanged(access,t.tree)
        && unchanged(access,t.receiver);
}
bool hud_title(const objects::UObjectIdentityAccess& access, void* receiver,
    const UObjectLiveHandle& identity, Target& title, Target& panel) noexcept
{
    Target owner{}; generic_witness_names(owner);
    void* pointer=nullptr;
    if (!hud_title_name || !hud_panel_name || identity.serial_number<=0
        || !capture(access,receiver,owner.receiver)
        || owner.receiver.live.serial_number!=identity.serial_number
        || owner.receiver.live.internal_index!=identity.internal_index
        || !typed_object(access,owner.receiver,NativeClass::UserWidget)
        || !read(receiver,0x1d8,pointer) || !capture(access,pointer,owner.tree)
        || owner.tree.outer!=receiver || !typed_object(access,owner.tree,NativeClass::WidgetTree)
        || !read(pointer,0x28,pointer) || !capture(access,pointer,owner.root)
        || owner.root.outer!=owner.tree.pointer) return false;
    PanelBases bases{};
    if (!panel_bases(access,owner,bases)) return false;
    std::array<Object,256> pending{};
    std::array<void*,256> visited{};
    std::size_t n=1,seen=0;
    pending[0]=owner.root;
    while (n) {
        const auto node=pending[--n];
        if (seen==visited.size() || !unchanged(access,node)) return false;
        for (std::size_t i=0;i<seen;++i) if (visited[i]==node.pointer) return false;
        visited[seen++]=node.pointer;
        if (node.name==hud_title_name || node.name==hud_panel_name) {
            auto& found=node.name==hud_title_name ? title : panel;
            if (found.target.pointer || node.outer!=owner.tree.pointer) return false;
            found=owner; found.target=node;
            if (!typed_object(access,node,node.name==hud_title_name ? NativeClass::Text : NativeClass::Canvas)) return false;
        }
        Ancestry type{};
        if (!ancestry(access,node,bases.panel,bases,type)) continue;
        PointerArray slots{};
        if (!pointer_array(node.pointer,0x108,slots)) return false;
        for (std::int32_t i=0;i<slots.count;++i) {
            void* slot_pointer=nullptr; void* child_pointer=nullptr; void* parent=nullptr; void* child_slot=nullptr;
            Object slot{},child{}; Ancestry slot_type{};
            if (n==pending.size() || !read(slots.data,static_cast<Address>(i)*sizeof(void*),slot_pointer)
                || !capture(access,slot_pointer,slot) || !ancestry(access,slot,bases.slot,bases,slot_type)
                || !read(slot_pointer,0x28,parent) || parent!=node.pointer
                || !read(slot_pointer,0x30,child_pointer) || !capture(access,child_pointer,child)
                || !read(child_pointer,0x28,child_slot) || child_slot!=slot_pointer
                || !unchanged(access,slot) || !ancestry_unchanged(access,slot_type)
                || !same_array(node.pointer,0x108,slots)) return false;
            pending[n++]=child;
        }
        if (!ancestry_unchanged(access,type)) return false;
    }
    void* tree=nullptr; void* root=nullptr;
    return title.target.pointer && panel.target.pointer && hud_path(access,title,bases) && hud_path(access,panel,bases)
        && read(receiver,0x1d8,tree) && tree==owner.tree.pointer
        && read(tree,0x28,root) && root==owner.root.pointer;
}
}

Result draw_song_hud(const OwnedImage* image, void* root, const UObjectLiveHandle& identity,
    std::uintptr_t root_pair, float width, float height, const SongHudMetadata& metadata,
    const SongHudFontMetrics& metrics, void* fcanvas, void* font, const OverlayDrawText& text,
    const OverlayDrawTile& tile, const SongHudCanvasAuthority& authority) noexcept
{
    (void)text; // Runtime ABI retains the legacy six-argument helper; HUD uses its isolated owning item.
    if (entered) return {Outcome::Busy,"hud_draw"};
    std::unique_lock lock(state.mutex,std::try_to_lock);
    if (!lock.owns_lock()) return {Outcome::Busy,"hud_draw"};
    EntryScope entry;
    try {
        const auto current=[&] { return state.available && !state.poisoned && authority.current
            && authority.current(authority.frame); };
        if (!current()) return {Outcome::Authority,"hud_post_render"};
        if (!state.hud_text) return {Outcome::Unavailable,"hud_text_contract"};
        objects::UObjectIdentityAccess access{}; Target title{},panel{};
        if (!capture_current_uobject_identity_access(access) || !hud_title(access,root,identity,title,panel))
            return {Outcome::Identity,"hud_title_tree"};
        const auto identity_current=[&] {
            PanelBases bases{}; void* tree=nullptr; void* root_widget=nullptr;
            return current() && panel_bases(access,title,bases)
                && unchanged(access,title.target) && unchanged(access,panel.target)
                && hud_path(access,title,bases) && hud_path(access,panel,bases)
                && read(root,0x1d8,tree) && tree==title.tree.pointer
                && read(tree,0x28,root_widget) && root_widget==title.root.pointer;
        };
        PreviewSelectedRowBounds bounds{}; PreviewAnchorPresentation presentation{};
        const PreviewAnchorRead reader=[](std::uintptr_t p,void* out,std::size_t bytes) {
            return p && core::safe_copy_bytes(reinterpret_cast<const void*>(p),out,bytes);
        };
        if (!read_preview_anchor({root_pair,reinterpret_cast<Address>(title.target.pointer),
                reinterpret_cast<Address>(panel.target.pointer),8,true},reader,identity_current,
                width,height,bounds,&presentation)) return {Outcome::Unavailable,"hud_current_paint"};
        const void* resource=nullptr;
        if (image && image->slot_<state.reserved_images) {
            auto& record=state.retained[image->slot_];
            (void)hud_resource(record,access,resource);
        }
        SongHudIconTextures icons{};
        for (std::size_t i=0;i<icons.size();++i) {
            const auto slot=state.hud_icons[i].slot_;
            if (state.hud_icons_ready[i] && slot<state.reserved_images)
                (void)hud_resource(state.retained[slot],access,icons[i]);
        }
        SongHudAnchor anchor{{bounds.row.x,bounds.row.y,bounds.row.width,bounds.row.height},width,height,presentation.ui_scale};
        anchor.title_block = SongHudRect{bounds.panel.x,bounds.panel.y,bounds.panel.width,bounds.panel.height};
        bool sharp_font=false;
        std::uint8_t cache_type=0;
        HudSharpText sharp{state.hud_sharp};
        if (state.hud_sharp) {
            if (!identity_current() || !read(font,runtime_layouts::UFont::cache_type,cache_type))
                return {Outcome::Identity,"hud_font_cache_read"};
            if (cache_type>1) return {Outcome::Unavailable,"hud_font_cache_unsupported"};
            sharp_font=cache_type==1;
            if (sharp_font && !read(font,runtime_layouts::UFont::legacy_typeface,sharp.typeface))
                return {Outcome::Identity,"hud_font_typeface_read"};
        }
        const auto font_current=[&] {
            std::uint8_t type=0; std::uint64_t face=0;
            return identity_current() && (!sharp_font
                || (read(font,runtime_layouts::UFont::cache_type,type) && type==1
                    && read(font,runtime_layouts::UFont::legacy_typeface,face) && face==sharp.typeface));
        };
        Result text_result{Outcome::Ready,"hud_text_unused"};
        SongHudFontMetrics actual=metrics;
        const auto measure_pixels=[&](const wchar_t* value,HudPosition& size) {
            if (!text_result) return false;
            HudTextWork work{state.hud_text,&font_current,
                [](const void* context) noexcept { return (*static_cast<const decltype(font_current)*>(context))(); },
                fcanvas,font,value,{0,0},{1,1,1,1},1,&sharp,&size};
            text_result=latch(hud_scaled_text(work));
            return static_cast<bool>(text_result);
        };
        if (sharp_font) {
            if (!font_current()) return {Outcome::Authority,"hud_font_info_admission"};
            if (!hud_dpi_call(sharp.functions.dpi,fcanvas,sharp.dpi))
                return {Outcome::Unavailable,"hud_font_dpi_fault"};
            if (!font_current() || !std::isfinite(sharp.dpi) || sharp.dpi<=0)
                return {Outcome::Unavailable,"hud_font_dpi_invalid"};
            const SongHudFontMetrics copied_text{1,[](std::wstring_view value) { return static_cast<float>(value.size()); }};
            const auto probe=L"Mg "+song_hud_metadata(metadata,std::numeric_limits<float>::max(),copied_text);
            const float target=anchor.title.height*.5f;
            // Bound cache requests; choose a measured integer size, never enlarge its glyph geometry.
            float line_height=0;
            const auto fitted=hud_fit_font_size(target,sharp,[&](HudPosition& size) {
                (void)measure_pixels(probe.c_str(),size); return text_result;
            },line_height);
            if (!fitted) return fitted;
            actual={line_height,[&](std::wstring_view value) {
                if (value.empty()) return 0.0f;
                const std::wstring terminated(value);
                HudPosition size{};
                return measure_pixels(terminated.c_str(),size) ? size.x : NAN;
            },1};
        }
        const OverlayDrawTile checked_tile=[&](void* c,float x,float y,float w,float h,float u,float v,
            float su,float sv,const runtime_layouts::FLinearColor* color,const void* texture,bool alpha) {
            return font_current() && tile && tile(c,x,y,w,h,u,v,su,sv,color,texture,alpha);
        };
        const SongHudDrawText checked_text=[&](void* c,float x,float y,const wchar_t* value,void* f,
            const runtime_layouts::FLinearColor* color,float scale) -> std::int32_t {
            if (!text_result || !color || c!=fcanvas || f!=font) throw 0;
            HudTextWork work{state.hud_text,&font_current,
                [](const void* context) noexcept { return (*static_cast<const decltype(font_current)*>(context))(); },
                c,f,value,{x,y},*color,scale,sharp_font ? &sharp : nullptr};
            text_result=latch(hud_scaled_text(work));
            if (!text_result) throw 0;
            return 0; // Submit is void; the renderer does not interpret the legacy layout metric.
        };
        const bool drawn=draw_song_hud_overlay(anchor,metadata,actual,resource,fcanvas,font,checked_text,checked_tile,presentation.opacity,icons);
        if (!text_result) return text_result;
        return drawn ? Result{Outcome::Ready,sharp_font ? "hud_submitted_runtime_font_info"
            : state.hud_sharp ? "hud_submitted_offline_blur_unresolved" : "hud_submitted_legacy_blur_unresolved"}
            : Result{Outcome::Unavailable,"hud_draw_failed"};
    } catch (...) { return {Outcome::Unavailable,"hud_exception"}; }
}
} // namespace ff7r::piano::game::menu_theme_native
