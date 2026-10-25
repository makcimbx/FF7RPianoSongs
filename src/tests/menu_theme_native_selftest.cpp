// Same-TU native seam: fake engine calls only, no game loading or invocation.
#define FF7RP_MENU_THEME_NATIVE_SELFTEST
#include "game/menu_theme_native.cpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace theme = ff7r::piano::game::menu_theme_native;
namespace game = ff7r::piano::game;
namespace core = ff7r::piano::core;
namespace obj = game::uobject_locator_core;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "menu_theme_native_selftest: " << message << '\n'; std::exit(1); }
}
template<class T> void put(void* p, std::size_t offset, T value)
{
    require(core::safe_write_field(p, offset, value), "fixture write");
}
enum Node { Meta, PackageClass, CorePackage, Transient, HolderClass, TextureClass, Holder, Texture,
    Receiver, Tree, TreeClass, Root, TargetNode, AssetPackage, AssetResource, SpriteClass,
    OldTree, OtherRoot, OtherTarget, HolderB, HolderC, HolderD, TextureB,
    PanelClass, SlotClass, PanelDerivedClass, SlotDerivedClass, UmgPackage, EdgeSlot, OtherEdgeSlot,
    DerivedMeta, HolderE, HolderF, UserClass, PieClass, TiledClass, ImagesClass, ButtonClass, TextClass,
    SongListClass, SpeedListClass, GamePackage, SongList, SpeedList, Row, RowTree,
    CursorOwner, CursorTree, CursorRoot, CursorPanel, CursorPanelSlot,
    CursorSlotA, CursorSlotB, CursorSlotC, CursorArrow, CursorGlow, CursorBase, CachedCursor, CursorClass, Nodes };
enum class CursorDrift { None, Current, List, Tree, Root, Recycle, Retire };
enum class Drift { None, Authority, Graph };
enum class TargetDrift { None, Retire, RecycleTarget, RecycleReceiver, ReplaceTree, Root, Detach, Name, Disable,
    Membership, Ancestry };
enum class ReadDrift { None, Membership, Content, Parent, ChildSlot, Header, Chain, Class,
    BaseSlot, BaseIndex, Root, Receiver, Item, MetaChain };
struct Fixture {
    // Heap backing keeps the larger actual-reader UI fixtures off the test stack.
    std::vector<std::array<std::uint8_t, 0xa00>> nodes = std::vector<std::array<std::uint8_t, 0xa00>>(Nodes);
    std::vector<std::uint8_t> items = std::vector<std::uint8_t>(128 * 0x18);
    bool grow_holders = false;
    bool drift_previous_after_capture = false;
    unsigned capture_revocation = 0;
    std::deque<std::array<std::uint8_t, 0x200>> extra_holders;
    alignas(8) std::array<std::uint8_t, 0x30> registry{};
    std::uintptr_t chunk = reinterpret_cast<std::uintptr_t>(items.data());
    void* package = nodes[Transient].data();
    alignas(8) std::array<std::uint8_t, 0x28> platform{};
    alignas(8) std::array<std::uint8_t, 0x38> mip{};
    std::array<void*, 16> mip_entries{mip.data()};
    std::int32_t mip_count = 1, mip_capacity = 4;
    std::array<std::uint8_t, 16> pixels{};
    bool qualified = false, ready = true, bad_mip = false, lose_root = false, drift_after_materialize = false;
    std::size_t qualifier_calls = 0;
    std::uintptr_t classifier_epoch = 1;
    bool classifier_current = true;
    void (*read_probe)() noexcept = nullptr;
    void (*graph_probe)() noexcept = nullptr;
    bool fault_unlock = false;
    bool prepare_reentry = false, prepare_reentry_observed = false;
    TargetDrift fname_drift = TargetDrift::None;
    bool fault_mark = false;
    bool disable_during_mark = false;
    Drift mark_drift = Drift::None;
    bool capacity_drift = false;
    Drift materialize_drift = Drift::None, unlock_drift = Drift::None, destroy_drift = Drift::None;
    std::vector<std::string> events;
    unsigned constructions = 0, textures = 0;
    unsigned resource_calls = 0, fault_resource_at = 0, drift_resource_at = 0;
    bool fault_resource_after_write = false;
    void* lookup_result = nodes[TargetNode].data();
    bool fault_lookup = false, fault_construct = false, fault_capture = false, fault_apply = false;
    bool fault_fname = false, fault_class = false;
    bool fault_after_copy = false;
    bool nested_construct = false;
    TargetDrift lookup_drift{}, construct_drift{}, capture_drift{}, apply_drift{};
    TargetDrift texture_drift{}, resource_drift{};
    void* panel_class = nodes[PanelClass].data();
    void* slot_class = nodes[SlotClass].data();
    std::array<std::uintptr_t, 4> panel_chain{}, slot_chain{}, derived_panel_chain{}, derived_slot_chain{};
    std::array<std::uintptr_t, 4> class_chain{}, derived_meta_chain{};
    std::array<void*, 2> root_slots{nodes[EdgeSlot].data(), nodes[OtherEdgeSlot].data()};
    ReadDrift read_drift = ReadDrift::None;
    unsigned slot_index_reads = 0;
    game::MenuSessionSnapshot session{};
    std::array<void*, theme::kClassCount> generic_slots{};
    std::array<std::array<std::uintptr_t, 4>, theme::kClassCount> generic_chains{};
    std::array<void*, 1> rows{nodes[Row].data()};
    alignas(8) std::array<std::uint8_t, 0x160> images{};
    unsigned adapter_calls = 0, adapter_drift_at = 0, adapter_fault_at = 0;
    unsigned adapter_attempts = 0, adapter_before_fault_at = 0, adapter_before_drift_at = 0;
    const theme::Subject* batching_subject = nullptr;
    std::span<const theme::BrushAssignment> batching_members{};
    unsigned batch_reentries = 0;
    std::size_t memory_reads = 0;
    bool retire_on_cached_holder_read = false;
    const theme::Subject* capturing_subject = nullptr;
    std::span<const theme::BrushSelection> capturing_members{};
    unsigned capture_reentries = 0;
    bool fail_after_capture = false;
    std::array<void*, 1> cursor_root_slots{};
    std::array<void*, 3> cursor_slots{};
    std::array<std::uintptr_t, 4> cursor_class_chain{};
    CursorDrift cursor_name_drift{}, cursor_construct_drift{}, cursor_capture_drift{}, cursor_apply_drift{};
    const theme::Subject* cursor_validating_subject = nullptr;
    bool cursor_nested_validation = false, cursor_lookup_reentry = false;
    TargetDrift adapter_drift = TargetDrift::None;
    bool style_live = false;
    unsigned style_references = 0;
    bool adapter_array_drift = false, adapter_geometry_drift = false, adapter_property_drift = false;
    const theme::Subject* validating_subject = nullptr;
    bool validated_during_sync = false;
    unsigned tint_releases = 0;
    Fixture()
    {
        session.generation = 7; session.phase = game::MenuSessionPhase::Ready;
        session.widget = nodes[Holder].data(); session.widget_identity = {6, 10};
        for (int i = 0; i < Nodes; ++i) {
            void* p = nodes[i].data();
            put(p, 0x0c, i);
            put(p, 0x10, static_cast<void*>(nodes[Meta].data()));
            put(p, 0x18, std::uint64_t{100} + i);
            put(items.data(), i * 0x18, p);
        }
        // Complete packed names intentionally have nonzero high halves too.
        put(nodes[Meta].data(), 0x18, std::uint64_t{0x100000003});
        put(nodes[PackageClass].data(), 0x18, std::uint64_t{0x200000002});
        put(nodes[PackageClass].data(), 0x20, static_cast<void*>(nodes[CorePackage].data()));
        put(nodes[CorePackage].data(), 0x18, std::uint64_t{0x300000004});
        put(nodes[CorePackage].data(), 0x10, static_cast<void*>(nodes[PackageClass].data()));
        put(nodes[Transient].data(), 0x18, std::uint64_t{0x400000001});
        put(nodes[Transient].data(), 0x10, static_cast<void*>(nodes[PackageClass].data()));
        put(items.data(), Transient * 0x18 + 8, std::int32_t{1 << 30});
        put(nodes[HolderClass].data(), 0x18, std::uint64_t{0x500000005});
        put(nodes[TextureClass].data(), 0x18, std::uint64_t{0x600000006});
        put(registry.data(), 0x10, &chunk);
        put(registry.data(), 0x20, std::int32_t{Nodes});
        put(registry.data(), 0x24, std::int32_t{Nodes});
        put(registry.data(), 0x28, std::int32_t{1});
        put(registry.data(), 0x2c, std::int32_t{1});
    }
    obj::UObjectIdentityAccess access()
    {
        obj::UObjectIdentityAccess a{};
        a.array_address = reinterpret_cast<std::uintptr_t>(registry.data());
        a.memory.context = this;
        a.memory.read = [](void* context, std::uintptr_t address, void* out, std::size_t n) noexcept {
            auto& f = *static_cast<Fixture*>(context);
            ++f.memory_reads;
            if (f.graph_probe && address == reinterpret_cast<std::uintptr_t>(f.nodes[Holder].data()) + 0x0c) {
                auto probe = f.graph_probe; f.graph_probe = nullptr; probe();
            }
            if (f.retire_on_cached_holder_read && address == reinterpret_cast<std::uintptr_t>(f.nodes[Holder].data()) + 0x0c)
                f.ready = false;
            if ((f.read_drift != ReadDrift::None || f.read_probe)
                && address == reinterpret_cast<std::uintptr_t>(f.nodes[EdgeSlot].data()) + 0x0c
                && ++f.slot_index_reads == 3) {
                f.inject_read_drift();
                if (f.read_probe) { auto probe = f.read_probe; f.read_probe = nullptr; probe(); }
            }
            return core::safe_copy_bytes(reinterpret_cast<void*>(address), out, n);
        };
        a.atomic_flags.load = [](void*, std::uintptr_t address, std::int32_t& out) noexcept {
            return core::safe_copy_bytes(reinterpret_cast<void*>(address), &out, sizeof(out));
        };
        return a;
    }
    void inject_read_drift()
    {
        if (read_drift == ReadDrift::Membership) root_slots[0] = nullptr;
        if (read_drift == ReadDrift::Content) put(nodes[EdgeSlot].data(), 0x30, static_cast<void*>(nodes[OtherTarget].data()));
        if (read_drift == ReadDrift::Parent) put(nodes[EdgeSlot].data(), 0x28, static_cast<void*>(nodes[OtherRoot].data()));
        if (read_drift == ReadDrift::ChildSlot) put(nodes[TargetNode].data(), 0x28, static_cast<void*>(nodes[OtherEdgeSlot].data()));
        if (read_drift == ReadDrift::Header) put(nodes[Root].data(), 0x114, std::int32_t{3});
        if (read_drift == ReadDrift::Chain) derived_slot_chain[1] = reinterpret_cast<std::uintptr_t>(slot_class);
        if (read_drift == ReadDrift::Class) put(nodes[EdgeSlot].data(), 0x10, static_cast<void*>(nodes[TextureClass].data()));
        if (read_drift == ReadDrift::BaseSlot) panel_class = nodes[SlotClass].data();
        if (read_drift == ReadDrift::BaseIndex) put(nodes[SlotClass].data(), 0x38, std::int32_t{2});
        if (read_drift == ReadDrift::Root) put(nodes[Tree].data(), 0x28, static_cast<void*>(nodes[OtherRoot].data()));
        if (read_drift == ReadDrift::Receiver) put(items.data(), Receiver * 0x18 + 0x10, std::int32_t{34});
        if (read_drift == ReadDrift::Item) put(items.data(), EdgeSlot * 0x18 + 8, std::int32_t{1 << 28});
        if (read_drift == ReadDrift::MetaChain) derived_meta_chain[1] = reinterpret_cast<std::uintptr_t>(nodes[Meta].data());
    }
    void enable_target()
    {
        qualified = true;
        session.widget = nodes[Receiver].data(); session.widget_identity = {Receiver, 33};
        put(items.data(), Receiver * 0x18 + 0x10, std::int32_t{33});
        put(items.data(), TargetNode * 0x18 + 0x10, std::int32_t{44});
        put(nodes[Receiver].data(), 0x1d8, static_cast<void*>(nodes[Tree].data()));
        put(nodes[Tree].data(), 0x10, static_cast<void*>(nodes[TreeClass].data()));
        put(nodes[Tree].data(), 0x20, session.widget);
        put(nodes[Tree].data(), 0x28, static_cast<void*>(nodes[Root].data()));
        put(nodes[TreeClass].data(), 0x18, std::uint64_t{0x800000008});
        put(nodes[Root].data(), 0x20, static_cast<void*>(nodes[Tree].data()));
        put(nodes[TargetNode].data(), 0x10, static_cast<void*>(nodes[HolderClass].data()));
        put(nodes[TargetNode].data(), 0x18, std::uint64_t{0x700000007});
        put(nodes[TargetNode].data(), 0x20, static_cast<void*>(nodes[Tree].data()));
        put(nodes[OldTree].data(), 0x10, static_cast<void*>(nodes[TreeClass].data()));
        put(nodes[OldTree].data(), 0x20, session.widget);
        put(nodes[OldTree].data(), 0x28, static_cast<void*>(nodes[Root].data()));
        put(nodes[AssetPackage].data(), 0x10, static_cast<void*>(nodes[PackageClass].data()));
        put(nodes[SpriteClass].data(), 0x18, std::uint64_t{0x900000009});
        put(nodes[AssetResource].data(), 0x10, static_cast<void*>(nodes[SpriteClass].data()));
        put(nodes[AssetResource].data(), 0x20, static_cast<void*>(nodes[AssetPackage].data()));
        put(nodes[Meta].data(), 0x20, static_cast<void*>(nodes[CorePackage].data()));
        put(nodes[UmgPackage].data(), 0x10, static_cast<void*>(nodes[PackageClass].data()));
        put(nodes[UmgPackage].data(), 0x18, std::uint64_t{0xd0000000d});
        put(nodes[PanelClass].data(), 0x18, std::uint64_t{0xb0000000b});
        put(nodes[SlotClass].data(), 0x18, std::uint64_t{0xc0000000c});
        for (auto id : {PanelClass, SlotClass, PanelDerivedClass, SlotDerivedClass})
            put(nodes[id].data(), 0x20, static_cast<void*>(nodes[UmgPackage].data()));
        panel_chain[1] = derived_panel_chain[1] = reinterpret_cast<std::uintptr_t>(panel_class) + 0x30;
        slot_chain[1] = derived_slot_chain[1] = reinterpret_cast<std::uintptr_t>(slot_class) + 0x30;
        put(nodes[PanelClass].data(), 0x30, panel_chain.data());
        put(nodes[PanelClass].data(), 0x38, std::int32_t{1});
        put(nodes[SlotClass].data(), 0x30, slot_chain.data());
        put(nodes[SlotClass].data(), 0x38, std::int32_t{1});
        put(nodes[PanelDerivedClass].data(), 0x30, derived_panel_chain.data());
        put(nodes[PanelDerivedClass].data(), 0x38, std::int32_t{2});
        put(nodes[SlotDerivedClass].data(), 0x30, derived_slot_chain.data());
        put(nodes[SlotDerivedClass].data(), 0x38, std::int32_t{2});
        class_chain[1] = derived_meta_chain[1] = reinterpret_cast<std::uintptr_t>(nodes[Meta].data()) + 0x30;
        put(nodes[Meta].data(), 0x30, class_chain.data());
        put(nodes[Meta].data(), 0x38, std::int32_t{1});
        put(nodes[DerivedMeta].data(), 0x20, static_cast<void*>(nodes[UmgPackage].data()));
        put(nodes[DerivedMeta].data(), 0x30, derived_meta_chain.data());
        put(nodes[DerivedMeta].data(), 0x38, std::int32_t{2});
        put(nodes[TargetNode].data(), 0x28, static_cast<void*>(nodes[EdgeSlot].data()));
        put(nodes[OtherTarget].data(), 0x28, static_cast<void*>(nodes[OtherEdgeSlot].data()));
        put(nodes[Root].data(), 0x10, static_cast<void*>(nodes[PanelDerivedClass].data()));
        put(nodes[Root].data(), 0x108, root_slots.data());
        put(nodes[Root].data(), 0x110, std::int32_t{2});
        put(nodes[Root].data(), 0x114, std::int32_t{2});
        for (auto id : {EdgeSlot, OtherEdgeSlot}) {
            put(nodes[id].data(), 0x10, static_cast<void*>(nodes[SlotDerivedClass].data()));
            put(nodes[id].data(), 0x28, static_cast<void*>(nodes[Root].data()));
        }
        put(nodes[EdgeSlot].data(), 0x30, static_cast<void*>(nodes[TargetNode].data()));
        put(nodes[OtherEdgeSlot].data(), 0x30, static_cast<void*>(nodes[OtherTarget].data()));
        // Fake native source owns all these fields (margin, UV and cached owner).
        for (unsigned i = 0; i < 0x88; ++i) nodes[TargetNode][0x108 + i] = static_cast<std::uint8_t>(i + 1);
        put(nodes[TargetNode].data(), 0x150, static_cast<void*>(nodes[AssetResource].data()));
    }
};
Fixture* fixture = nullptr;
game::RvaSignatureSpec signature_spec{};
unsigned native_calls = 0;
theme::CallbackAuthority authority();
void cursor_drift(CursorDrift drift)
{
    auto& f = *fixture;
    if (drift == CursorDrift::Current) put(f.nodes[SongList].data(), 0x8f8, static_cast<void*>(f.nodes[CachedCursor].data()));
    if (drift == CursorDrift::List) put(f.nodes[Receiver].data(), 0x3c8, std::int32_t{-1});
    if (drift == CursorDrift::Tree) put(f.nodes[CursorOwner].data(), 0x1d8, static_cast<void*>(f.nodes[OldTree].data()));
    if (drift == CursorDrift::Root) put(f.nodes[CursorTree].data(), 0x28, static_cast<void*>(f.nodes[OtherRoot].data()));
    if (drift == CursorDrift::Recycle) put(f.items.data(), CursorOwner * 0x18 + 0x10, std::int32_t{67});
    if (drift == CursorDrift::Retire) f.ready = false;
}
void event(const char* stage)
{
    ++native_calls; fixture->events.emplace_back(stage);
    if (fixture->capturing_subject) {
        const auto calls = native_calls;
        std::array<theme::OwnedImage, 4> outputs{};
        theme::CaptureProgress progress{};
        require(theme::capture_original_members(*fixture->capturing_subject, fixture->capturing_members, authority(),
            std::span<theme::OwnedImage>(outputs.data(), fixture->capturing_members.size()), progress).outcome == theme::Outcome::Busy
            && !progress.published_members && native_calls == calls, "capture native reentry rejected without publication");
        ++fixture->capture_reentries;
    }
}
void apply_drift(Drift drift)
{
    if (drift == Drift::Authority) fixture->qualified = false;
    if (drift == Drift::Graph) put(fixture->items.data(), Holder * 0x18 + 8, std::int32_t{0});
}
void target_drift(TargetDrift drift)
{
    if (drift == TargetDrift::Retire) fixture->ready = false;
    if (drift == TargetDrift::RecycleTarget) put(fixture->items.data(), TargetNode * 0x18 + 0x10, std::int32_t{45});
    if (drift == TargetDrift::RecycleReceiver) put(fixture->items.data(), Receiver * 0x18 + 0x10, std::int32_t{34});
    if (drift == TargetDrift::ReplaceTree) put(fixture->nodes[Receiver].data(), 0x1d8, static_cast<void*>(fixture->nodes[OldTree].data()));
    if (drift == TargetDrift::Root) put(fixture->nodes[Tree].data(), 0x28, static_cast<void*>(fixture->nodes[OtherRoot].data()));
    if (drift == TargetDrift::Detach) put(fixture->nodes[TargetNode].data(), 0x20, static_cast<void*>(fixture->nodes[OldTree].data()));
    if (drift == TargetDrift::Name) put(fixture->nodes[TargetNode].data(), 0x18, std::uint64_t{0x710000007});
    if (drift == TargetDrift::Disable) theme::disable();
    if (drift == TargetDrift::Membership) fixture->root_slots[0] = nullptr;
    if (drift == TargetDrift::Ancestry) fixture->derived_slot_chain[1] = reinterpret_cast<std::uintptr_t>(fixture->slot_class);
}

theme::CallbackAuthority authority();
void* __fastcall fake_fname(std::uint64_t* out, const wchar_t* text, std::int32_t find)
{
    event("find"); require(find == 0, "FNAME_Find only");
    if (fixture->fault_fname) RaiseException(0xe0000001, 0, 0, nullptr);
    if (fixture->prepare_reentry) {
        const auto calls = native_calls;
        require(theme::prepare_subjects(authority()).outcome == theme::Outcome::Busy
            && !theme::state.generic_names.ready && native_calls == calls,
            "nested preparation cannot publish or invoke native calls");
        fixture->prepare_reentry_observed = true;
    }
    target_drift(fixture->fname_drift);
    const std::wstring_view name(text);
    *out = name == L"/Engine/Transient" ? 0x400000001ull : name == L"Package" ? 0x200000002ull
        : name == L"Class" ? 0x100000003ull : name == L"/Script/CoreUObject" ? 0x300000004ull
        : name == L"EndImage" ? 0x500000005ull : name == L"Texture2D" ? 0x600000006ull
        : name == L"Bg_1" ? 0x700000007ull : name == L"Bg_2" ? 0xa0000000aull
        : name == L"WidgetTree" ? 0x800000008ull : name == L"PanelWidget" ? 0xb0000000bull
        : name == L"PanelSlot" ? 0xc0000000cull : name == L"/Script/UMG" ? 0xd0000000dull : 0;
    if (name == L"/Script/EndGame") *out = 0xe0000000eull;
    if (name == L"Cursor_Left") *out = 0x200000031ull;
    if (name == L"Img_BtnLight") *out = 0x200000032ull;
    if (name == L"HoverBase") *out = 0x200000033ull;
    if (name == L"Txt_MusicTitle") *out = 0x200000034ull;
    if (name == L"Pnl_MusicTitle") *out = 0x200000035ull;
    for (std::size_t i = 0; i < theme::kClassCount; ++i)
        if (name == theme::class_specs[i].name && !*out) *out = 0x100000010ull + i;
    if (fixture->cursor_lookup_reentry) {
        const auto calls = native_calls; theme::Subject nested;
        require(theme::find_cursor_subject(L"Cursor_Left", authority(), nested).outcome == theme::Outcome::Busy
            && native_calls == calls, "cursor lookup native reentry rejected");
    }
    cursor_drift(fixture->cursor_name_drift);
    return out;
}
void* __fastcall fake_class()
{
    event("class");
    if (fixture->fault_class) RaiseException(0xe0000001, 0, 0, nullptr);
    return fixture->nodes[HolderClass].data();
}
void* __fastcall fake_lookup(void* receiver, const std::uint64_t* name)
{
    event("lookup");
    require(receiver == fixture->session.widget && (*name == 0x700000007ull || *name == 0xa0000000aull),
        "typed receiver/leaf FName ABI");
    if (fixture->fault_lookup) RaiseException(0xe0000001, 0, 0, nullptr);
    target_drift(fixture->lookup_drift);
    return fixture->lookup_result;
}
void* __fastcall fake_construct(const void* parameters)
{
    event("root_holder");
    if (fixture->fault_construct) RaiseException(0xe0000001, 0, 0, nullptr);
    const auto* p = static_cast<const std::uint8_t*>(parameters);
    std::uint32_t flags = 0;
    std::memcpy(&flags, p + 0x18, 4); require(flags == 0xc0, "constructor root flags");
    const Node holder_nodes[] = {Holder, HolderB, HolderC, HolderD, HolderE, HolderF};
    std::size_t holder_node = 0;
    std::uint8_t* holder = nullptr;
    if (fixture->constructions < 6) {
        holder_node = holder_nodes[fixture->constructions];
        holder = fixture->nodes[holder_node].data();
    } else {
        require(fixture->grow_holders, "small fake holder pool requires explicit growth fixture");
        fixture->extra_holders.emplace_back();
        holder_node = Nodes + fixture->extra_holders.size() - 1;
        require(holder_node < fixture->items.size() / 0x18, "growth fixture registry capacity");
        holder = fixture->extra_holders.back().data();
        put(holder, 0x0c, static_cast<std::int32_t>(holder_node));
        // Native NAME_None construction assigns a real object name, just as the
        // fixed fixture nodes do; zero-initialized extra storage is not a UObject.
        put(holder, 0x18, std::uint64_t{100} + holder_node);
        put(fixture->items.data(), holder_node * 0x18, static_cast<void*>(holder));
        put(fixture->registry.data(), 0x24, static_cast<std::int32_t>(holder_node + 1));
    }
    ++fixture->constructions;
    void* klass = nullptr; void* outer = nullptr;
    std::memcpy(&klass, p, 8); std::memcpy(&outer, p + 8, 8);
    require(klass == fixture->nodes[HolderClass].data() && outer == fixture->package, "constructor graph");
    for (std::size_t i = 0x10; i < 0x40; ++i)
        if (i < 0x18 || i >= 0x1c) require(p[i] == 0, "zero constructor options");
    put(holder, 0x10, klass); put(holder, 0x20, outer);
    put(fixture->items.data(), holder_node * 0x18 + 8, std::int32_t{1 << 30});
    if (fixture->nested_construct) {
        theme::ImageTarget nested;
        require(theme::find_image(L"Bg_1", authority(), nested).outcome == theme::Outcome::Busy,
            "native construction listener cannot recursively enter bridge");
    }
    target_drift(fixture->construct_drift);
    cursor_drift(fixture->cursor_construct_drift);
    return holder;
}
void* __fastcall fake_brush(void* brush)
{
    event("brush_ctor");
    const auto* bytes = static_cast<const std::uint8_t*>(brush);
    for (int i = 0; i < 0x88; ++i) require(bytes[i] == 0, "fresh zero brush");
    return brush;
}
void __fastcall fake_destroy(void*)
{
    event("brush_dtor");
    apply_drift(fixture->destroy_drift);
}
void __fastcall fake_set_resource(void* brush, void* texture)
{
    event("set_resource");
    const bool fault = ++fixture->resource_calls == fixture->fault_resource_at;
    if (fault && !fixture->fault_resource_after_write)
        RaiseException(0xe0000001, 0, 0, nullptr);
    void* old = nullptr;
    require(core::safe_read_field(brush, 0x48, old), "native resource read");
    if (old != texture) {
        put(brush, 0x48, texture);
        put(brush, 0x70, static_cast<void*>(nullptr));
        put(brush, 0x78, static_cast<void*>(nullptr));
    }
    if (fault) RaiseException(0xe0000001, 0, 0, nullptr);
    if (!fixture->drift_resource_at || fixture->resource_calls == fixture->drift_resource_at)
        target_drift(fixture->resource_drift);
}
void __fastcall fake_set_brush(void* holder, void* brush)
{
    const bool target = holder == fixture->nodes[TargetNode].data() || holder == fixture->nodes[OtherTarget].data()
        || holder == fixture->nodes[CursorArrow].data() || holder == fixture->nodes[CursorGlow].data()
        || holder == fixture->nodes[CursorBase].data();
    event(target ? "assign_target" : "bind_holder");
    const bool fault = (target && fixture->fault_apply) || (!target && fixture->fault_capture);
    if (!target && !fixture->lose_root) {
        std::int32_t index = -1, flags = 0;
        require(core::safe_read_field(holder, 0x0c, index) && index >= 0
            && static_cast<std::size_t>(index) < fixture->items.size() / 0x18
            && core::safe_read_field(fixture->items.data(), index * 0x18 + 8, flags)
            && (flags & (1 << 30)), "rooted holder BEFORE original/prepared brush retention");
    }
    if (fault && !fixture->fault_after_copy)
        RaiseException(0xe0000001, 0, 0, nullptr);
    // This is the FAKE native owning operation, not production brush memcpy.
    std::memcpy(static_cast<std::uint8_t*>(holder) + 0x108, brush, 0x88);
    if (fault) RaiseException(0xe0000001, 0, 0, nullptr);
    target_drift(target ? fixture->apply_drift : fixture->capture_drift);
    if (!target && fixture->drift_previous_after_capture && fixture->constructions == 2)
        fixture->nodes[TargetNode][0x128] ^= 1;
    if (!target && fixture->capture_revocation == 1) ++fixture->classifier_epoch;
    if (!target && fixture->capture_revocation == 2) fixture->qualified = false;
    cursor_drift(target ? fixture->cursor_apply_drift : fixture->cursor_capture_drift);
    if (fixture->cursor_validating_subject) {
        const auto calls = native_calls;
        require(theme::validate_subject(*fixture->cursor_validating_subject, fixture->session)
            && native_calls == calls, "nested palette validator reads current cursor provider without native calls");
        fixture->cursor_nested_validation = true;
    }
    if (!target && fixture->fail_after_capture) {
        fixture->fail_after_capture = false;
        theme::fail_ledger_allocation = true;
    }
}
void* __fastcall fake_texture(std::int32_t w, std::int32_t h, std::int32_t format, std::uint64_t name)
{
    event("texture");
    require(w == 2 && h == 2 && format == 2 && name == 0, "texture ABI");
    std::int32_t flags = 0;
    require(core::safe_read_field(fixture->items.data(), Holder * 0x18 + 8, flags)
        && (flags & (1 << 30)), "root BEFORE texture allocation");
    auto* texture = fixture->nodes[fixture->textures++ ? TextureB : Texture].data();
    put(texture, 0x91, std::uint8_t{0xa5}); // constructor SRGB plus unrelated neighboring bits
    put(texture, 0x10, static_cast<void*>(fixture->nodes[TextureClass].data()));
    put(texture, 0x20, fixture->package);
    put(texture, 0x138, static_cast<void*>(fixture->platform.data()));
    put(fixture->platform.data(), 0, w); put(fixture->platform.data(), 4, h);
    put(fixture->platform.data(), 0xc, format);
    put(fixture->platform.data(), 0x18, fixture->mip_entries.data());
    put(fixture->platform.data(), 0x20, fixture->mip_count);
    put(fixture->platform.data(), 0x24, fixture->mip_capacity);
    put(fixture->mip.data(), 0, w); put(fixture->mip.data(), 4, h);
    put(fixture->mip.data(), 0x18, static_cast<void*>(fixture->pixels.data()));
    put(fixture->mip.data(), 0x20, std::uint64_t{16});
    put(fixture->mip.data(), 0x30, std::uint32_t{fixture->bad_mip ? 0x20000000u : 0u});
    if (fixture->lose_root) put(fixture->items.data(), Holder * 0x18 + 8, std::int32_t{0});
    target_drift(fixture->texture_drift);
    return texture;
}
void __fastcall fake_materialize(void* bulk)
{
    event("materialize"); require(bulk == fixture->mip.data() + 0x10, "bulk ABI");
    require(fixture->events[fixture->events.size() - 2] == "bind_holder", "bind BEFORE materialize");
    if (fixture->drift_after_materialize) put(fixture->mip.data(), 0x20, std::uint64_t{15});
    if (fixture->capacity_drift) put(fixture->platform.data(), 0x24, std::int32_t{8});
    apply_drift(fixture->materialize_drift);
}
void __fastcall fake_unlock(void* bulk)
{
    event("unlock"); std::uint8_t lock = 0;
    require(core::safe_read_field(bulk, 0x24, lock) && lock == 2, "write lock before unlock");
    put(bulk, 0x24, std::uint8_t{0});
    apply_drift(fixture->unlock_drift);
    if (fixture->fault_unlock) RaiseException(0xe0000001, 0, 0, nullptr);
}
void __fastcall fake_mark(void* texture)
{
    event("mark_not_offline_processed");
    require(fixture->events[fixture->events.size() - 2] == "unlock", "unlock BEFORE policy mark");
    std::uint8_t flags = 0;
    require(core::safe_read_field(texture, 0x91, flags) && flags == 0xa5, "fresh constructor flags");
    put(texture, 0x91, static_cast<std::uint8_t>(flags | 0x10));
    apply_drift(fixture->mark_drift);
    if (fixture->disable_during_mark) theme::disable();
    if (fixture->fault_mark) RaiseException(0xe0000001, 0, 0, nullptr);
}
void __fastcall fake_update(void* texture)
{
    event("update");
    require(fixture->events[fixture->events.size() - 2] == "mark_not_offline_processed", "policy mark BEFORE upload");
    std::uint8_t flags = 0;
    require(core::safe_read_field(texture, 0x91, flags) && flags == 0xb5, "policy preserves SRGB and neighboring bits");
}
void install_fake_functions()
{
    // Each fixture is a DIFFERENT synthetic engine world. Never scan immutable
    // storage pointing into a preceding fixture's expired stack; keep charges.
    if (theme::state.retained.storage)
        for (auto& record : *theme::state.retained.storage) record.published = false;
    theme::state.generic = {};
    theme::state.generic_names = {};
    theme::state.carrier_names = {};
    theme::hud_title_name = theme::hud_panel_name = 0;
    theme::state.hud_icons_ready.fill(false);
    if (theme::state.rows) for (auto& row : *theme::state.rows) row = {};
    theme::state.poisoned = false; // isolate synthetic engine-failure cases, not a production reset
    theme::state.available = true;
    theme::state.functions = {fake_fname, fake_class, fake_construct, fake_brush,
        fake_destroy, fake_materialize, fake_unlock, fake_update,
        fake_set_resource, fake_set_brush, fake_texture, &fixture->package, fake_lookup,
        &fixture->panel_class, &fixture->slot_class, fake_mark};
}
theme::CallbackAuthority authority()
{
    return {fixture->session, GetCurrentThreadId(), fixture,
        [](void* context, const game::MenuSessionSnapshot&) noexcept {
            auto& f = *static_cast<Fixture*>(context);
            ++f.qualifier_calls;
            return f.qualified;
        }};
}

theme::CallbackAuthority classifier_authority()
{
    auto a = authority();
    a.context = reinterpret_cast<void*>(fixture->classifier_epoch);
    a.qualify_current_callback = [](void*, const game::MenuSessionSnapshot&) noexcept {
        ++fixture->qualifier_calls; return fixture->qualified;
    };
    a.phase_current = [](void* context, const game::MenuSessionSnapshot& session) noexcept {
        return fixture->classifier_current
            && reinterpret_cast<std::uintptr_t>(context) == fixture->classifier_epoch
            && fixture->ready && session.phase == game::MenuSessionPhase::Ready
            && session.generation == fixture->session.generation && session.widget == fixture->session.widget
            && session.widget_identity.internal_index == fixture->session.widget_identity.internal_index
            && session.widget_identity.serial_number == fixture->session.widget_identity.serial_number;
    };
    return a;
}
struct ProofCounts {
    std::size_t qualifiers = 0, reads = 0;
    unsigned native = 0;
    std::uint64_t targets = 0, basic = 0, membership = 0, names = 0, constructs = 0, copies = 0, colors = 0;
};
template<class F> ProofCounts measure_proofs(F&& operation)
{
    namespace perf = game::menu_theme_metrics;
    perf::Snapshot snapshot{};
    while (perf::take_snapshot(snapshot)) {}
    const auto q = fixture->qualifier_calls, reads = fixture->memory_reads;
    const auto native = native_calls;
    perf::enable(true);
    { perf::Span root(perf::Metric::Resolve); operation(); }
    perf::enable(false);
    require(perf::take_snapshot(snapshot), "batch proof metrics captured");
    require(snapshot.counters[static_cast<std::size_t>(perf::Counter::MalformedScopes)] == 0,
        "batch native/SEH/reentry measurements balance metric scopes");
    auto stat = [&](perf::Metric metric) { return snapshot.stats[static_cast<std::size_t>(metric)].count; };
    auto counter = [&](perf::Counter value) { return snapshot.counters[static_cast<std::size_t>(value)]; };
    return {fixture->qualifier_calls - q, fixture->memory_reads - reads, native_calls - native,
        stat(perf::Metric::ValidateTarget), counter(perf::Counter::TargetBasicChecks), counter(perf::Counter::RowMembershipEntries),
        stat(perf::Metric::NativeFindName), stat(perf::Metric::NativeConstruct), stat(perf::Metric::NativeBrushCopy), stat(perf::Metric::NativeColorAssign)};
}
void print_proofs(const char* label, const ProofCounts& before, const ProofCounts& after)
{
    std::cout << label << ": qualifiers " << before.qualifiers << " -> " << after.qualifiers
        << ", targets " << before.targets << " -> " << after.targets << ", basic " << before.basic << " -> " << after.basic
        << ", membership " << before.membership << " -> " << after.membership
        << ", reads " << before.reads << " -> " << after.reads << ", native " << before.native << " -> " << after.native
        << ", FName " << before.names << " -> " << after.names << ", construct " << before.constructs << " -> " << after.constructs
        << ", brush copy " << before.copies << " -> " << after.copies << ", color assign " << before.colors << " -> " << after.colors << '\n';
}
void row_preflight_tests(const theme::Subject& subject, const theme::RowTicket& ticket, theme::RowKind kind)
{
    using Kind = theme::SubjectKind;
    auto& f = *fixture;
    put(f.nodes[OtherTarget].data(), 0x10, f.generic_slots[static_cast<std::size_t>(Kind::TextBlock) + 2]);
    put(f.nodes[OtherTarget].data(), 0x18, std::uint64_t{0xa0000000a});
    put(f.nodes[OtherTarget].data(), 0x20, static_cast<void*>(f.nodes[RowTree].data()));
    const auto a = classifier_authority();
    theme::Subject sibling;
    require(static_cast<bool>(theme::find_subject(L"Bg_2", Kind::TextBlock, ticket, a, sibling)), "second actual row property subject");
    const std::array<theme::SubjectPreflight, 4> groups{{{&subject, {}}, {&sibling, {}}, {&subject, {}}, {&sibling, {}}}};
    const auto before = measure_proofs([&] {
        for (const auto& group : groups) {
            void* object = nullptr;
            require(static_cast<bool>(theme::access_subject(*group.subject, a, object)) && object, "individual row property preflight");
        }
    });
    theme::BatchProgress progress{};
    const auto after = measure_proofs([&] {
        require(static_cast<bool>(theme::preflight_subjects(groups, a, progress)) && progress.completed == groups.size()
            && !progress.created && progress.failed_index == theme::kInvalidImageSlot, "batch row property preflight");
    });
    require(after.targets == 2 && after.basic == 4 && after.qualifiers == 1 && after.reads < before.reads
        && after.membership < before.membership && !before.native && !after.native,
        "row group shares real receiver/membership reads, not only qualifier calls");
    print_proofs(kind == theme::RowKind::Song ? "row preflight song" : "row preflight speed", before, after);
    auto legacy = a; legacy.phase_current = nullptr;
    const auto unshared = measure_proofs([&] { require(static_cast<bool>(theme::preflight_subjects(groups, legacy, progress)), "null guard row preflight"); });
    require(unshared.targets == groups.size() * 2 && unshared.reads > after.reads && unshared.qualifiers > after.qualifiers,
        "null guard uses independent full target/owner proofs");
    f.classifier_current = false;
    const auto calls = native_calls;
    const auto qualifiers = f.qualifier_calls;
    require(!theme::preflight_subjects(groups, a, progress) && !progress.completed
        && native_calls == calls && f.qualifier_calls == qualifiers, "revoked preflight guard fails before qualification");
    f.classifier_current = true;
}

void adapter_entry()
{
    const auto phase = ++fixture->adapter_attempts;
    if (fixture->batching_subject) {
        const auto calls = native_calls;
        theme::ApplyProgress progress{};
        require(theme::apply_members(*fixture->batching_subject, fixture->batching_members, authority(), progress).outcome
            == theme::Outcome::Busy && !progress.attempted_members && native_calls == calls,
            "native reentry cannot obtain a second batch or mutate");
        ++fixture->batch_reentries;
    }
    if (phase == fixture->adapter_before_drift_at) target_drift(fixture->adapter_drift);
    if (phase == fixture->adapter_before_fault_at) RaiseException(0xe0000001, 0, 0, nullptr);
}
void adapter_event(const char* name)
{
    event(name);
    const auto call = ++fixture->adapter_calls;
    if (call == fixture->adapter_fault_at) RaiseException(0xe0000001, 0, 0, nullptr);
    if (call == fixture->adapter_drift_at) target_drift(fixture->adapter_drift);
}
void* __fastcall fake_assign(void* destination, const void* source)
{
    adapter_entry();
    // Fake native owning assignment. Production never byte-assigns shared owners.
    std::memcpy(destination, source, 0x88);
    adapter_event("own_brush");
    return destination;
}
void* __fastcall fake_color_assign(void* destination, const void* source)
{
    auto* value = static_cast<const std::uint8_t*>(source);
    void* controller = nullptr; void* old = nullptr;
    std::memcpy(&controller, value + 0x20, sizeof(controller));
    std::memcpy(&old, static_cast<const std::uint8_t*>(destination) + 0x20, sizeof(old));
    require(value[0x10] == 0 && !controller, "specified initialized non-owning color source");
    if (old) ++fixture->tint_releases; // fake native release of DESTINATION owner only
    auto* output = static_cast<std::uint8_t*>(destination);
    std::memcpy(output, value, 0x10); output[0x10] = value[0x10];
    std::memcpy(output + 0x18, value + 0x18, 0x10); // padding deliberately untouched
    adapter_event("own_tint");
    return destination;
}
void __fastcall fake_attribute(void*, void* attribute)
{
    void* source = nullptr;
    std::memcpy(&source, attribute, sizeof(source));
    auto* bytes = static_cast<std::uint8_t*>(attribute);
    require(bytes[8] == 1 && source != attribute && source != theme::state.style_scratch.data(), "permanent consuming attribute");
    for (std::size_t i = 0x10; i < 0x20; ++i) require(bytes[i] == 0, "no delegate owners");
    require(source == fixture->nodes[TargetNode].data() + 0x108
        || source == fixture->nodes[TargetNode].data() + 0x110, "attribute points into live widget");
    adapter_event("consume_attribute");
}
void __fastcall fake_sync(void* widget)
{
    adapter_entry();
    require(widget == fixture->nodes[TargetNode].data(), "synchronize receiver");
    adapter_event("synchronize");
    if (fixture->validating_subject) {
        const auto calls = native_calls;
        require(theme::validate_subject(*fixture->validating_subject, fixture->session) && native_calls == calls,
            "nested palette source validation invokes no native code");
        fixture->validated_during_sync = true;
    }
    if (fixture->adapter_array_drift) put(widget, 0x108, fixture->images.data() + 0xb0);
    if (fixture->adapter_geometry_drift) fixture->images[0x88] ^= 1;
}
void __fastcall fake_invalidate(void*, std::uint8_t reason)
{
    adapter_entry();
    require(reason == 1, "native invalidate reason"); adapter_event("invalidate");
}
void* __fastcall fake_style_construct(void* style)
{
    adapter_entry();
    require(!fixture->style_live, "fresh initialized style scratch");
    fixture->style_live = true; fixture->style_references = 0;
    std::memset(style, 0, 0x278); adapter_event("style_construct"); return style;
}
void* __fastcall fake_style_assign(void* style, const void* source)
{
    adapter_entry();
    require(fixture->style_live, "own style into initialized storage");
    std::memcpy(style, source, 0x278); fixture->style_references = 4;
    adapter_event("own_style"); return style;
}
void __fastcall fake_button_style(void* widget, void* style)
{
    adapter_entry();
    require(fixture->style_live && fixture->style_references == 4, "live full style owners during SetStyle");
    std::memcpy(static_cast<std::uint8_t*>(widget) + 0x120, style, 0x278);
    adapter_event("button_style");
    if (fixture->adapter_property_drift) static_cast<std::uint8_t*>(widget)[0x120] ^= 1;
}
void __fastcall fake_style_destroy(void*)
{
    adapter_entry();
    require(fixture->style_live && fixture->style_references == 4, "destroy owned temporary once");
    fixture->style_live = false; fixture->style_references = 0; adapter_event("style_destroy");
}
void enable_generic(theme::SubjectKind kind)
{
    auto& f = *fixture;
    const Node classes[] = {UserClass, TreeClass, HolderClass, PieClass, TiledClass, ImagesClass,
        ButtonClass, TextClass, PanelDerivedClass, SongListClass, SpeedListClass};
    put(f.nodes[GamePackage].data(), 0x10, static_cast<void*>(f.nodes[PackageClass].data()));
    put(f.nodes[GamePackage].data(), 0x18, std::uint64_t{0xe0000000e});
    for (std::size_t i = 0; i < theme::kClassCount; ++i) {
        f.generic_slots[i] = f.nodes[classes[i]].data();
        std::uint64_t name = i == 1 ? 0x800000008ull : i == 2 ? 0x500000005ull : 0x100000010ull + i;
        put(f.generic_slots[i], 0x18, name);
        put(f.generic_slots[i], 0x20, static_cast<void*>(f.nodes[i < 2 ? UmgPackage : GamePackage].data()));
        f.generic_chains[i][2] = reinterpret_cast<std::uintptr_t>(f.generic_slots[i]) + 0x30;
        if (classes[i] == ButtonClass || classes[i] == PanelDerivedClass)
            f.generic_chains[i][1] = reinterpret_cast<std::uintptr_t>(f.panel_class) + 0x30;
        put(f.generic_slots[i], 0x30, f.generic_chains[i].data());
        put(f.generic_slots[i], 0x38, std::int32_t{2});
        theme::state.generic.slots[i] = &f.generic_slots[i];
    }
    put(f.nodes[Receiver].data(), 0x10, static_cast<void*>(f.nodes[UserClass].data()));
    put(f.nodes[TargetNode].data(), 0x10, f.generic_slots[static_cast<std::size_t>(kind) + 2]);
    theme::state.generic.classes = theme::state.generic.styles = true;
    auto& g = theme::state.generic;
    g.brush_assign = fake_assign; g.pie_brush = g.tiled_brush = fake_attribute;
    g.tiled_sync = g.images_sync = fake_sync; g.button_style = fake_button_style;
    g.style_construct = fake_style_construct; g.style_assign = fake_style_assign;
    g.style_destroy = fake_style_destroy; g.invalidate = fake_invalidate;
    g.color_assign = fake_color_assign;
    auto* widget = f.nodes[TargetNode].data();
    if (kind == theme::SubjectKind::ImageSet) {
        std::memcpy(f.images.data(), widget + 0x108, 0x88);
        std::memcpy(f.images.data() + 0xb0, widget + 0x108, 0x88);
        put(widget, 0x108, f.images.data()); put(widget, 0x110, std::int32_t{2}); put(widget, 0x114, std::int32_t{2});
        put(widget, 0x160, static_cast<void*>(f.nodes[OtherRoot].data()));
    } else if (kind == theme::SubjectKind::Button) {
        const auto brush = std::array<std::uint8_t, 0x88>{};
        // Other style fields/sounds/padding remain distinguishable comparison facts.
        for (std::size_t i = 0; i < 0x278; ++i) widget[0x120 + i] = static_cast<std::uint8_t>(i);
        for (auto offset : {0x128, 0x1b0, 0x238, 0x2c0}) {
            std::memcpy(widget + offset, brush.data(), brush.size());
            put(widget, offset + 0x48, static_cast<void*>(f.nodes[AssetResource].data()));
        }
        put(widget, 0x108, static_cast<void*>(nullptr)); put(widget, 0x110, std::int32_t{0}); put(widget, 0x114, std::int32_t{0});
        put(widget, 0x448, static_cast<void*>(f.nodes[OtherRoot].data()));
    } else if (kind == theme::SubjectKind::PieImage) {
        put(widget, 0x1f0, static_cast<void*>(f.nodes[OtherRoot].data()));
    } else if (kind == theme::SubjectKind::TiledImage) {
        std::memmove(widget + 0x110, widget + 0x108, 0x88); // fixture construction only
        put(widget, 0x208, static_cast<void*>(f.nodes[OtherRoot].data()));
    } else if (kind == theme::SubjectKind::CanvasPanel) {
        put(widget, 0x108, static_cast<void*>(nullptr)); put(widget, 0x110, std::int32_t{0}); put(widget, 0x114, std::int32_t{0});
    }
}

void enable_cursor()
{
    auto& f = *fixture;
    put(f.nodes[SongList].data(), 0x10, static_cast<void*>(f.nodes[SongListClass].data()));
    put(f.items.data(), SongList * 0x18 + 0x10, std::int32_t{88});
    put(f.nodes[Receiver].data(), 0x3c8, std::int32_t{SongList});
    put(f.nodes[Receiver].data(), 0x3cc, std::int32_t{88});
    put(f.nodes[SongList].data(), 0x8f8, static_cast<void*>(f.nodes[CursorOwner].data()));
    put(f.nodes[SongList].data(), 0x900, static_cast<void*>(f.nodes[AssetResource].data()));
    f.cursor_class_chain[2] = reinterpret_cast<std::uintptr_t>(f.nodes[UserClass].data()) + 0x30;
    f.cursor_class_chain[3] = reinterpret_cast<std::uintptr_t>(f.nodes[CursorClass].data()) + 0x30;
    put(f.nodes[CursorClass].data(), 0x20, static_cast<void*>(f.nodes[GamePackage].data()));
    put(f.nodes[CursorClass].data(), 0x30, f.cursor_class_chain.data());
    put(f.nodes[CursorClass].data(), 0x38, std::int32_t{3});
    put(f.nodes[CursorOwner].data(), 0x10, static_cast<void*>(f.nodes[CursorClass].data()));
    put(f.items.data(), CursorOwner * 0x18 + 0x10, std::int32_t{66});
    put(f.nodes[CursorOwner].data(), 0x1d8, static_cast<void*>(f.nodes[CursorTree].data()));
    put(f.nodes[CursorTree].data(), 0x10, static_cast<void*>(f.nodes[TreeClass].data()));
    put(f.nodes[CursorTree].data(), 0x20, static_cast<void*>(f.nodes[CursorOwner].data()));
    put(f.nodes[CursorTree].data(), 0x28, static_cast<void*>(f.nodes[CursorRoot].data()));
    for (auto node : {CursorRoot, CursorPanel}) {
        put(f.nodes[node].data(), 0x10, static_cast<void*>(f.nodes[PanelDerivedClass].data()));
        put(f.nodes[node].data(), 0x20, static_cast<void*>(f.nodes[CursorTree].data()));
    }
    f.cursor_root_slots = {f.nodes[CursorPanelSlot].data()};
    f.cursor_slots = {f.nodes[CursorSlotA].data(), f.nodes[CursorSlotB].data(), f.nodes[CursorSlotC].data()};
    put(f.nodes[CursorRoot].data(), 0x108, f.cursor_root_slots.data());
    put(f.nodes[CursorRoot].data(), 0x110, std::int32_t{1}); put(f.nodes[CursorRoot].data(), 0x114, std::int32_t{1});
    put(f.nodes[CursorPanel].data(), 0x108, f.cursor_slots.data());
    put(f.nodes[CursorPanel].data(), 0x110, std::int32_t{3}); put(f.nodes[CursorPanel].data(), 0x114, std::int32_t{3});
    const Node slots[] = {CursorPanelSlot, CursorSlotA, CursorSlotB, CursorSlotC};
    const Node children[] = {CursorPanel, CursorArrow, CursorGlow, CursorBase};
    for (std::size_t i = 0; i < std::size(slots); ++i) {
        const auto parent = i ? CursorPanel : CursorRoot;
        put(f.nodes[slots[i]].data(), 0x10, static_cast<void*>(f.nodes[SlotDerivedClass].data()));
        put(f.nodes[slots[i]].data(), 0x20, static_cast<void*>(f.nodes[parent].data()));
        put(f.nodes[slots[i]].data(), 0x28, static_cast<void*>(f.nodes[parent].data()));
        put(f.nodes[slots[i]].data(), 0x30, static_cast<void*>(f.nodes[children[i]].data()));
        put(f.nodes[children[i]].data(), 0x28, static_cast<void*>(f.nodes[slots[i]].data()));
        if (!i) continue;
        auto* child = f.nodes[children[i]].data();
        put(child, 0x10, static_cast<void*>(f.nodes[HolderClass].data()));
        put(child, 0x18, std::uint64_t{0x200000030ull} + i);
        put(child, 0x20, static_cast<void*>(f.nodes[CursorTree].data()));
        put(f.items.data(), children[i] * 0x18 + 0x10, static_cast<std::int32_t>(100 + i));
        // Fake fixture initialization, not a production owning assignment.
        std::memcpy(child + 0x108, f.nodes[TargetNode].data() + 0x108, 0x88);
    }
}
void cursor_subject_tests(const core::ThemeImage& image)
{
    const wchar_t* leaves[] = {L"Cursor_Left", L"Img_BtnLight", L"HoverBase"};
    for (bool positive_tree : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image); enable_cursor();
        if (positive_tree) {
            put(f.items.data(), CursorTree * 0x18 + 0x10, std::int32_t{80});
            put(f.items.data(), CursorRoot * 0x18 + 0x10, std::int32_t{81});
        }
        auto a = authority(); std::array<theme::Subject, 3> subjects{}; std::array<theme::OwnedImage, 3> originals{};
        const auto records = theme::state.reserved_images;
        for (std::size_t i = 0; i < subjects.size(); ++i) {
            require(static_cast<bool>(theme::find_cursor_subject(leaves[i], a, subjects[i])), "current cursor own-tree leaf found");
            void* target = nullptr;
            require(static_cast<bool>(theme::access_subject(subjects[i], a, target)) && target == f.nodes[CursorArrow + i].data(),
                "cursor subject borrows actual child, not main receiver/cache");
            require(static_cast<bool>(theme::capture_original(subjects[i], {}, a, originals[i])), "cursor native original capture");
        }
        require(theme::state.reserved_images == records + 1, "cursor originals dedup storage without merging affinities");
        const auto calls = native_calls; f.qualified = false;
        for (const auto& subject : subjects) {
            theme::RowAssociation association{};
            require(theme::validate_subject(subject, f.session) && !theme::subject_row_association(subject, association),
                "palette validator uses current cursor witness, not Song ticket/mutation authority");
        }
        require(native_calls == calls, "cursor facts-only validation invokes no native functions"); f.qualified = true;
        require(theme::apply(subjects[1], {}, originals[0], a).outcome == theme::Outcome::Identity,
            "cursor original affinity cannot cross siblings");
        theme::OwnedImage texture, themed;
        require(static_cast<bool>(theme::create(image, a, texture))
            && static_cast<bool>(theme::compose(originals[0], texture, a, themed)), "cursor reuses prepared texture bridge");
        f.cursor_validating_subject = &subjects[0];
        require(static_cast<bool>(theme::apply(subjects[0], {}, themed, a)) && f.cursor_nested_validation
            && static_cast<bool>(theme::apply(subjects[0], {}, originals[0], a)), "cursor native assignment and original restoration");
        f.cursor_validating_subject = nullptr;
        require(std::memcmp(f.nodes[CursorArrow].data() + 0x108, f.nodes[TargetNode].data() + 0x108, 0x88) == 0,
            "complete original cursor brush restored");
        ++f.session.generation;
        require(!theme::validate_subject(subjects[0], f.session)
            && !theme::apply(subjects[0], {}, originals[0], authority()), "cursor originals cannot cross menu generation");
        require(std::count(f.events.begin(), f.events.end(), "lookup") == 0, "cursor search never invokes native named-child cache");
    }
    for (unsigned change = 0; change < 17; ++change) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image); enable_cursor();
        const auto a = authority(); theme::Subject subject; theme::OwnedImage original;
        require(static_cast<bool>(theme::find_cursor_subject(L"Cursor_Left", a, subject))
            && static_cast<bool>(theme::capture_original(subject, {}, a, original)), "cursor drift fixture");
        const auto calls = native_calls;
        if (change == 0) { // A completely valid cached old cursor is still not the current cursor.
            put(f.nodes[SongList].data(), 0x900, static_cast<void*>(f.nodes[CursorOwner].data()));
            put(f.nodes[SongList].data(), 0x8f8, static_cast<void*>(nullptr));
        }
        if (change == 1) {
            put(f.nodes[SpeedList].data(), 0x10, static_cast<void*>(f.nodes[SongListClass].data()));
            put(f.items.data(), SpeedList * 0x18 + 0x10, std::int32_t{99});
            put(f.nodes[SpeedList].data(), 0x8f8, static_cast<void*>(f.nodes[CursorOwner].data()));
            put(f.nodes[Receiver].data(), 0x3c8, std::int32_t{SpeedList});
            put(f.nodes[Receiver].data(), 0x3cc, std::int32_t{99});
        }
        if (change == 2) cursor_drift(CursorDrift::Current);
        if (change == 3) cursor_drift(CursorDrift::Tree);
        if (change == 4) cursor_drift(CursorDrift::Root);
        if (change == 5) cursor_drift(CursorDrift::Recycle);
        if (change == 6) put(f.items.data(), Receiver * 0x18 + 0x10, std::int32_t{34});
        if (change == 7) put(f.items.data(), SongList * 0x18 + 0x10, std::int32_t{89});
        if (change == 8) put(f.nodes[CursorOwner].data(), 8, std::uint32_t{0x8000});
        if (change == 9) put(f.items.data(), CursorArrow * 0x18 + 0x10, std::int32_t{999});
        if (change == 10) f.cursor_slots[0] = nullptr;
        if (change == 11) put(f.nodes[CursorArrow].data(), 0x20, static_cast<void*>(f.nodes[Tree].data()));
        if (change == 12) put(f.nodes[CursorOwner].data(), 0x10, static_cast<void*>(f.nodes[HolderClass].data()));
        if (change == 13) put(f.nodes[SongList].data(), 0x10, static_cast<void*>(f.nodes[SpeedListClass].data()));
        if (change == 14) put(f.items.data(), CursorTree * 0x18 + 8, std::int32_t{1 << 28});
        if (change == 15) put(f.nodes[CursorArrow].data(), 0x18, std::uint64_t{0x200000032ull});
        if (change == 16) f.session.phase = game::MenuSessionPhase::Ending;
        void* borrowed = nullptr;
        require(!theme::validate_subject(subject, f.session) && !theme::access_subject(subject, authority(), borrowed)
            && !borrowed && !theme::apply(subject, {}, original, authority()) && native_calls == calls,
            "changed cursor/main/list/tree/path/identity cannot mutate or restore stale subject");
        if (change == 0) {
            theme::Subject missing;
            require(theme::find_cursor_subject(L"Cursor_Left", a, missing).outcome == theme::Outcome::Identity
                && native_calls == calls, "CachedCursor is not a lookup fallback");
        }
    }
    for (unsigned stage = 0; stage < 4; ++stage) {
        for (auto drift : {CursorDrift::Current, CursorDrift::List, CursorDrift::Tree,
            CursorDrift::Root, CursorDrift::Recycle, CursorDrift::Retire}) {
            Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image); enable_cursor();
            const auto a = authority(); theme::Subject subject; theme::OwnedImage original;
            require(static_cast<bool>(theme::find_cursor_subject(L"Cursor_Left", a, subject)), "cursor reentry fixture");
            const auto records = theme::state.reserved_images;
            theme::Result result{};
            if (stage == 0) {
                f.cursor_name_drift = drift; f.cursor_lookup_reentry = true;
                result = theme::find_cursor_subject(L"Img_BtnLight", a, subject);
            } else if (stage == 3) {
                require(static_cast<bool>(theme::capture_original(subject, {}, a, original)), "cursor restoration before retirement");
                f.cursor_apply_drift = drift; result = theme::apply(subject, {}, original, a);
            } else {
                if (stage == 1) f.cursor_construct_drift = drift;
                else f.cursor_capture_drift = drift;
                result = theme::capture_original(subject, {}, a, original);
                require(theme::state.reserved_images == records + 1, "failed cursor native capture remains charged");
            }
            require(result.outcome == theme::Outcome::NativeFault && theme::state.poisoned && !theme::state.available,
                "post-native cursor provider drift latches without stale follow-up");
            const auto calls = native_calls;
            require(!theme::apply(subject, {}, original, a) && native_calls == calls
                && std::count(f.events.begin(), f.events.end(), "brush_dtor") == 0,
                "no restoration/native cleanup after lost cursor admission");
            require(f.events.back() == (stage == 0 ? "find" : stage == 1 ? "root_holder" : stage == 2 ? "bind_holder" : "assign_target"),
                "cursor drift stops at exact reentrant helper");
        }
    }
    {
        Fixture missing; fixture = &missing; missing.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image); enable_cursor();
        put(missing.nodes[TargetNode].data(), 0x18, std::uint64_t{0x200000031ull});
        put(missing.nodes[CursorArrow].data(), 0x18, std::uint64_t{0x700000007ull});
        theme::Subject absent;
        require(theme::find_cursor_subject(L"Cursor_Left", authority(), absent).outcome == theme::Outcome::Input,
            "missing cursor leaf never falls back to MusicList tree");
    }
    Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image); enable_cursor();
    put(f.items.data(), CursorArrow * 0x18 + 0x10, std::int32_t{0});
    f.cursor_lookup_reentry = true;
    theme::Subject zero;
    require(static_cast<bool>(theme::find_cursor_subject(L"Cursor_Left", authority(), zero))
        && theme::validate_subject(zero, f.session), "diagnostic-zero cursor leaf requires full current strong path");
    put(f.nodes[CursorArrow].data(), 8, std::uint32_t{0x10000});
    require(!theme::validate_subject(zero, f.session), "strong cursor path never excuses endpoint destruction");
}
void generic_subject_tests(const core::ThemeImage& pixels)
{
    using Kind = theme::SubjectKind;
    require(theme::class_reference_target(0x2000, 0x1000, -0x1014)
        && !theme::class_reference_target(0x2000, 0x1000, 0x1014)
        && !theme::class_reference_target(0, 0x1000, 0), "actual signed RIP target classifier");
    const auto calls_before_specs = native_calls;
    require(!theme::authenticate_generic(GetModuleHandleW(nullptr)).classes && native_calls == calls_before_specs,
        "missing/mismatched generic specs publish no capability or native calls");
    for (auto kind : {Kind::Image, Kind::PieImage, Kind::TiledImage, Kind::ImageSet, Kind::Button, Kind::TextBlock, Kind::CanvasPanel}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(kind);
        auto a = authority(); theme::Subject subject;
        put(f.items.data(), TargetNode * 0x18 + 0x10, std::int32_t{0});
        require(static_cast<bool>(theme::find_subject(L"Bg_1", kind, {}, a, subject)), "generic strong zero subject");
        require(std::find(f.events.begin(), f.events.end(), "lookup") == f.events.end(), "no native name cache traversal");
        require(theme::validate_subject(subject, f.session), "read-only source validator");
        const auto before_read = native_calls;
        f.qualified = false;
        require(theme::validate_subject(subject, f.session) && native_calls == before_read, "facts validator is not callback admission");
        void* object = nullptr;
        require(theme::access_subject(subject, a, object).outcome == theme::Outcome::Authority && !object, "facts do not authorize access");
        f.qualified = true;
        require(static_cast<bool>(theme::access_subject(subject, a, object)) && object == f.nodes[TargetNode].data(), "qualified subject access");
        theme::Subject wrong;
        require(!theme::find_subject(L"Bg_1", kind == Kind::Image ? Kind::TextBlock : Kind::Image, {}, a, wrong), "wrong native class rejected");
        require(theme::capabilities().subjects && theme::capabilities().extended_brushes && theme::capabilities().brush_tint, "explicit independent capabilities");
        if (kind == Kind::TextBlock || kind == Kind::CanvasPanel) {
            theme::OwnedImage unsupported;
            require(theme::capture_original(subject, {}, a, unsupported).outcome == theme::Outcome::Input, "palette-only subject has no invented brush");
            continue;
        }
        theme::BrushSelection selection{};
        if (kind == Kind::ImageSet) selection.index = 1;
        if (kind == Kind::Button) selection.button = theme::ButtonBrush::Pressed;
        theme::Target facts{}; // comparison-only test helper uses the production selector
        require(static_cast<bool>(theme::guarded_subject_find(L"Bg_1", kind, 0, a, f.access(), facts))
            && theme::select_member(facts, selection), "actual member selector");
        std::array<std::uint8_t, 0x88> original_bytes{};
        require(theme::read_brush(theme::target_brush(facts), original_bytes), "source native brush facts");
        std::array<std::uint8_t, 0x278> untouched_style{};
        std::memcpy(untouched_style.data(), f.nodes[TargetNode].data() + 0x120, untouched_style.size());
        theme::OwnedImage original, texture, composed;
        require(static_cast<bool>(theme::capture_original(subject, selection, a, original)), "generic original owning snapshot");
        const auto snapshots = theme::state.reserved_images;
        theme::OwnedImage reused;
        require(static_cast<bool>(theme::capture_original(subject, selection, a, reused))
            && theme::state.reserved_images == snapshots, "generic immutable original dedup");
        require(static_cast<bool>(theme::create(pixels, a, texture)) && static_cast<bool>(theme::compose(original, texture, a, composed)), "generic shaped preparation");
        theme::OwnedImage tinted, cached_tint;
        const std::array<float, 4> color{0.25f, 0.5f, 0.75f, 0.8f};
        require(static_cast<bool>(theme::tint(composed, color, a, tinted)), "native owning brush tint");
        const auto tint_count = theme::state.reserved_images;
        const auto tint_calls = native_calls;
        require(static_cast<bool>(theme::tint(composed, color, a, cached_tint))
            && theme::state.reserved_images == tint_count && native_calls == tint_calls, "cached tint no allocation/native update");
        require(!theme::tint(composed, {0, 0, 0, std::numeric_limits<float>::quiet_NaN()}, a, cached_tint)
            && native_calls == tint_calls, "invalid tint before native calls");
        const auto uploads = std::count(f.events.begin(), f.events.end(), "update");
        f.validating_subject = &subject;
        require(static_cast<bool>(theme::apply(subject, selection, tinted, a)), "class-correct owning apply/refresh");
        require(std::memcmp(static_cast<std::uint8_t*>(theme::target_brush(facts)) + 0x20, color.data(), sizeof(float) * 4) == 0,
            "brush tint reaches selected permanent native member");
        require(static_cast<bool>(theme::apply(subject, selection, original, a)), "class-correct original restoration");
        require(std::memcmp(theme::target_brush(facts), original_bytes.data(), original_bytes.size()) == 0, "complete original including native tint/cache restored");
        require(std::count(f.events.begin(), f.events.end(), "update") == uploads, "switch never uploads");
        if (kind == Kind::PieImage || kind == Kind::TiledImage)
            require(std::count(f.events.begin(), f.events.end(), "consume_attribute") == 2, "permanent consuming attributes refreshed");
        if (kind == Kind::TiledImage || kind == Kind::ImageSet)
            require(std::count(f.events.begin(), f.events.end(), "synchronize") == 2 && f.validated_during_sync,
                "renderer geometry synchronized with nested read-only source validation");
        if (kind == Kind::Button) {
            require(!f.style_live && f.style_references == 0 && std::count(f.events.begin(), f.events.end(), "style_destroy") == 2, "full owning style cleanup exactly once");
            require(std::memcmp(untouched_style.data(), f.nodes[TargetNode].data() + 0x120, untouched_style.size()) == 0, "other states sounds padding unchanged");
            auto other = selection; other.button = theme::ButtonBrush::Normal;
            require(theme::apply(subject, other, original, a).outcome == theme::Outcome::Identity, "button state affinity");
            other.button = static_cast<theme::ButtonBrush>(4);
            require(theme::capture_original(subject, other, a, reused).outcome == theme::Outcome::Input, "button state bounds");
        }
        if (kind == Kind::ImageSet) {
            auto invalid = selection; invalid.index = -1;
            require(theme::capture_original(subject, invalid, a, reused).outcome == theme::Outcome::Input, "negative image index");
            invalid.index = 2;
            require(theme::capture_original(subject, invalid, a, reused).outcome == theme::Outcome::Input, "image index bound");
            put(f.nodes[TargetNode].data(), 0x114, std::int32_t{257});
            require(!theme::apply(subject, selection, original, a), "changed/oversized image header");
        }
    }
    // Current strong rows only. Cache-array position is never a logical index.
    for (auto row_kind : {theme::RowKind::Song, theme::RowKind::Speed}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(Kind::TextBlock);
        auto a = authority();
        put(f.nodes[Row].data(), 0x10, static_cast<void*>(f.nodes[UserClass].data()));
        put(f.items.data(), Row * 0x18 + 0x10, std::int32_t{77});
        put(f.nodes[Row].data(), 0x1d8, static_cast<void*>(f.nodes[RowTree].data()));
        put(f.nodes[RowTree].data(), 0x10, static_cast<void*>(f.nodes[TreeClass].data()));
        put(f.nodes[RowTree].data(), 0x20, static_cast<void*>(f.nodes[Row].data()));
        put(f.nodes[RowTree].data(), 0x28, static_cast<void*>(f.nodes[Root].data()));
        put(f.nodes[Root].data(), 0x20, static_cast<void*>(f.nodes[RowTree].data()));
        put(f.nodes[TargetNode].data(), 0x20, static_cast<void*>(f.nodes[RowTree].data()));
        const auto list = row_kind == theme::RowKind::Song ? SongList : SpeedList;
        const auto weak = row_kind == theme::RowKind::Song ? 0x3c8 : 0x410;
        const auto active = row_kind == theme::RowKind::Song ? 0x8d8 : 0x188;
        put(f.nodes[list].data(), 0x10, static_cast<void*>(f.nodes[row_kind == theme::RowKind::Song ? SongListClass : SpeedListClass].data()));
        put(f.items.data(), list * 0x18 + 0x10, std::int32_t{88});
        put(f.nodes[Receiver].data(), weak, static_cast<std::int32_t>(list)); put(f.nodes[Receiver].data(), weak + 4, std::int32_t{88});
        put(f.nodes[list].data(), active, f.rows.data()); put(f.nodes[list].data(), active + 8, std::int32_t{1}); put(f.nodes[list].data(), active + 12, std::int32_t{1});
        theme::RowTicket ticket;
        require(static_cast<bool>(theme::bind_row(row_kind, f.nodes[Row].data(), 91, 6, a, ticket)), "actual row setup association");
        theme::Subject subject;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", Kind::TextBlock, ticket, a, subject)), "current row child");
        theme::RowAssociation association{};
        require(theme::subject_row_association(subject, association) && association.logical_index == 91
            && association.setup_generation == 6 && association.session_generation == a.session.generation, "logical index not pool position");
        require(theme::validate_subject(subject, f.session), "row source validation");
        row_preflight_tests(subject, ticket, row_kind);
        {
            theme::Target row_target{};
            require(static_cast<bool>(theme::find_subject_sequence(L"Bg_1", Kind::TextBlock,
                theme::state.rows->back().id, a, f.access(), row_target)), "actual-reader row classifier facts");
            namespace perf = game::menu_theme_metrics;
            perf::Snapshot before{}, after{};
            const auto calls = native_calls;
            const auto legacy_qualifiers = f.qualifier_calls, legacy_reads = f.memory_reads;
            perf::enable(true);
            require(theme::target_valid(f.access(), a, row_target), "legacy row classifier");
            perf::enable(false); require(perf::take_snapshot(before), "legacy classifier metrics");
            const auto qualifiers = f.qualifier_calls - legacy_qualifiers, reads = f.memory_reads - legacy_reads;
            const auto guarded = classifier_authority();
            const auto shared_qualifiers = f.qualifier_calls, shared_reads = f.memory_reads;
            perf::enable(true);
            require(theme::target_valid(f.access(), guarded, row_target), "guarded row classifier");
            perf::enable(false); require(perf::take_snapshot(after), "guarded classifier metrics");
            require(qualifiers == 6 && f.qualifier_calls - shared_qualifiers == 1
                && f.memory_reads - shared_reads == reads && native_calls == calls,
                "row classifier shares qualification only: six to one, identical reads and zero native calls");
            require(before.counters[static_cast<std::size_t>(perf::Counter::TargetBasicChecks)] == 2
                && after.counters[static_cast<std::size_t>(perf::Counter::TargetBasicChecks)] == 2
                && before.counters[static_cast<std::size_t>(perf::Counter::TreeNodes)]
                    == after.counters[static_cast<std::size_t>(perf::Counter::TreeNodes)]
                && before.counters[static_cast<std::size_t>(perf::Counter::RowMembershipEntries)]
                    == after.counters[static_cast<std::size_t>(perf::Counter::RowMembershipEntries)],
                "both full owner/membership and containment rereads preserved");
            std::cout << "row classifier " << (row_kind == theme::RowKind::Song ? "song" : "speed")
                << ": qualifiers " << qualifiers << " -> " << f.qualifier_calls - shared_qualifiers
                << ", basic 2 -> 2, reads " << reads << " -> " << f.memory_reads - shared_reads << ", native 0 -> 0\n";
            const auto completed = f.qualifier_calls;
            require(theme::target_valid(f.access(), guarded, row_target) && f.qualifier_calls == completed + 1,
                "classifier proof never escapes return");
            f.classifier_current = false;
            const auto rejected = f.qualifier_calls;
            require(!theme::target_valid(f.access(), guarded, row_target) && f.qualifier_calls == rejected,
                "revoked guard rejects before qualification");
            f.classifier_current = true; ++f.classifier_epoch;
            require(!theme::target_valid(f.access(), guarded, row_target), "saved callback epoch rejected");
            const auto replacement = classifier_authority();
            require(theme::target_valid(f.access(), replacement, row_target)
                && !theme::target_valid(f.access(), guarded, row_target), "new epoch cannot revive saved authority");
        }
        theme::RowTicket newer;
        require(static_cast<bool>(theme::bind_row(row_kind, f.nodes[Row].data(), 92, 7, a, newer))
            && !theme::validate_subject(subject, f.session), "rebind retires old setup ticket");
        require(!theme::find_subject(L"Bg_1", Kind::TextBlock, ticket, a, subject), "stale row ticket rejected");
        put(f.nodes[list].data(), active + 8, std::int32_t{0});
        put(f.nodes[list].data(), active + 0x10, f.rows.data()); put(f.nodes[list].data(), active + 0x18, std::int32_t{1});
        require(!theme::find_subject(L"Bg_1", Kind::TextBlock, newer, a, subject), "cache row is not current strong content");
        require(!theme::bind_row(row_kind, f.nodes[Row].data(), 93, 8, a, newer), "failed rebind cannot retain stale association");
    }
    for (unsigned corruption = 0; corruption < 7; ++corruption) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(Kind::PieImage);
        const auto a = authority(); theme::Subject subject;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", Kind::PieImage, {}, a, subject)), "class corruption baseline");
        const auto i = static_cast<std::size_t>(theme::NativeClass::Pie);
        if (corruption == 0) f.generic_slots[i] = nullptr;
        if (corruption == 1) put(f.nodes[PieClass].data(), 0x18, std::uint64_t{99});
        if (corruption == 2) put(f.nodes[PieClass].data(), 0x20, static_cast<void*>(f.nodes[UmgPackage].data()));
        if (corruption == 3) put(f.nodes[PieClass].data(), 0x38, std::int32_t{-1});
        if (corruption == 4) f.generic_chains[i][2] = reinterpret_cast<std::uintptr_t>(f.nodes[PieClass].data());
        if (corruption == 5) f.generic_slots[static_cast<std::size_t>(theme::NativeClass::WidgetTree)] = nullptr;
        if (corruption == 6) put(f.items.data(), TargetNode * 0x18 + 8, std::int32_t{1 << 28});
        const auto calls = native_calls;
        require(!theme::validate_subject(subject, f.session) && native_calls == calls, "fresh native class/package/chain/owner membership rejection");
    }
    // Each potentially reentrant native phase must stop immediately on drift or
    // fault, including style destruction. Never stale-restore or retry cleanup.
    for (auto kind : {Kind::PieImage, Kind::TiledImage, Kind::ImageSet, Kind::Button}) {
        const unsigned phases = kind == Kind::Button ? 6 : kind == Kind::TiledImage ? 4 : 3;
        for (bool fault : {false, true}) for (unsigned phase = 1; phase <= phases; ++phase) {
            Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(kind);
            const auto a = authority(); theme::Subject subject;
            theme::OwnedImage original, texture, composed;
            require(static_cast<bool>(theme::find_subject(L"Bg_1", kind, {}, a, subject))
                && static_cast<bool>(theme::capture_original(subject, {}, a, original))
                && static_cast<bool>(theme::create(pixels, a, texture))
                && static_cast<bool>(theme::compose(original, texture, a, composed)), "adapter phase baseline");
            if (fault) f.adapter_fault_at = phase;
            else { f.adapter_drift_at = phase; f.adapter_drift = TargetDrift::Retire; }
            require(theme::apply(subject, {}, composed, a).outcome == theme::Outcome::NativeFault
                && f.adapter_calls == phase && theme::state.poisoned, "stop at exact native adapter drift/fault");
            const auto calls = native_calls;
            require(!theme::apply(subject, {}, original, a) && native_calls == calls, "no stale rollback or cleanup after compromised phase");
            if (kind == Kind::Button && phase < 5)
                require(std::find(f.events.begin(), f.events.end(), "style_destroy") == f.events.end(), "no unadmitted style destruction");
        }
    }
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(Kind::PieImage);
        const auto a = authority(); theme::Subject subject; theme::OwnedImage original;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", Kind::PieImage, {}, a, subject)), "optional style baseline");
        theme::state.generic.styles = false;
        const auto calls = native_calls;
        require(theme::capture_original(subject, {}, a, original).outcome == theme::Outcome::Unavailable
            && native_calls == calls && theme::capabilities().subjects, "absent style profile leaves read-only subjects available");
    }
    for (unsigned drift = 0; drift < 3; ++drift) {
        const auto kind = drift == 2 ? Kind::Button : Kind::ImageSet;
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(kind);
        const auto a = authority(); theme::Subject subject; theme::OwnedImage original, texture, composed;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", kind, {}, a, subject))
            && static_cast<bool>(theme::capture_original(subject, {}, a, original))
            && static_cast<bool>(theme::create(pixels, a, texture))
            && static_cast<bool>(theme::compose(original, texture, a, composed)), "member drift baseline");
        f.adapter_array_drift = drift == 0; f.adapter_geometry_drift = drift == 1; f.adapter_property_drift = drift == 2;
        require(theme::apply(subject, {}, composed, a).outcome == theme::Outcome::NativeFault && theme::state.poisoned,
            "post-native array geometry or untouched style drift rejected");
        require(f.events.back() == (drift == 2 ? "button_style" : "synchronize"), "no stale destructor/invalidation after member drift");
    }
}

theme::Target reader_target()
{
    theme::Target t{};
    auto access = fixture->access();
    t.generation = fixture->session.generation;
    require(theme::capture(access, fixture->session.widget, t.receiver)
        && theme::capture(access, fixture->nodes[Tree].data(), t.tree)
        && theme::capture(access, fixture->nodes[TreeClass].data(), t.tree_class), "reader owner snapshots");
    void* root = nullptr;
    require(core::safe_read_field(fixture->nodes[Tree].data(), 0x28, root)
        && theme::capture(access, root, t.root)
        && theme::capture(access, fixture->nodes[TargetNode].data(), t.target)
        && theme::capture(access, fixture->nodes[HolderClass].data(), t.target_class), "reader target snapshots");
    t.panel_name = 0xb0000000b; t.slot_name = 0xc0000000c; t.umg_name = 0xd0000000d;
    t.class_name = 0x100000003; t.package_name = 0x200000002; t.core_package_name = 0x300000004;
    return t;
}

void classifier_boundary_tests()
{
    static unsigned boundary = 0;
    static const theme::Target* target = nullptr;
    static bool observed = false;
    for (boundary = 0; boundary < 7; ++boundary) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        const auto t = reader_target(); target = &t; observed = false;
        const auto a = classifier_authority();
        const auto calls = native_calls;
        f.read_probe = []() noexcept {
            observed = true;
            auto& current = *fixture;
            if (boundary == 3) {
                theme::measured_native(game::menu_theme_metrics::Metric::NativeStaticClass, fake_class);
                return;
            }
            if (boundary == 4) {
                std::size_t slot = 0;
                require(theme::reserve_record(UINT64_MAX, slot).outcome == theme::Outcome::Budget,
                    "allocation boundary rejects oversized reservation");
                return;
            }
            if (boundary == 5) { theme::EntryScope nested_callback; return; }
            const auto nested_authority = classifier_authority();
            if (boundary == 0) current.classifier_current = false;
            if (boundary == 1) current.qualified = false; // Fresh full qualification sees GC/engine loss.
            if (boundary == 6) ++current.session.generation;
            const bool nested = theme::target_valid(current.access(), nested_authority, *target);
            require(nested == (boundary == 2), "nested classifier uses independent current proof");
            current.classifier_current = true; current.qualified = true;
            current.session = nested_authority.session;
        };
        namespace perf = game::menu_theme_metrics;
        perf::enable(true);
        require(!theme::target_valid(f.access(), a, t) && observed && !theme::classifier_phase,
            "read-injected nested/revoked/native/allocation/callback boundary permanently revokes parent classifier");
        perf::enable(false);
        perf::Snapshot metrics{};
        require(perf::take_snapshot(metrics)
            && metrics.counters[static_cast<std::size_t>(perf::Counter::MalformedScopes)] == 0
            && metrics.stats[static_cast<std::size_t>(perf::Metric::ValidateTarget)].failed >= 1,
            "nested classifier diagnostics balance actual rejected proofs");
        require(native_calls == calls + (boundary == 3 ? 1 : 0), "no implicit native work on classifier rejection");
        require(theme::target_valid(f.access(), a, t), "later independent classifier reacquires proof after boundary");
    }
    target = nullptr;
}

void containment_tests()
{
    classifier_boundary_tests();
    const auto calls = native_calls;
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        const auto t = reader_target();
        namespace perf = game::menu_theme_metrics;
        perf::enable(true);
        require(theme::target_valid(f.access(), authority(), t), "actual-reader panel ancestry + forward containment");
        f.root_slots[0] = f.nodes[OtherEdgeSlot].data();
        require(!theme::target_valid(f.access(), authority(), t), "replacement membership rejected on fresh check");
        f.root_slots[0] = f.nodes[EdgeSlot].data();
        require(theme::target_valid(f.access(), authority(), t), "path rebuilt, not cached between checks");
        perf::enable(false);
        perf::Snapshot metrics{};
        require(perf::take_snapshot(metrics)
            && metrics.stats[static_cast<std::size_t>(perf::Metric::ValidateTarget)].count == 3
            && metrics.stats[static_cast<std::size_t>(perf::Metric::ValidateTarget)].failed == 1,
            "validator metrics preserve actual successful and rejected containment passes");
    }
    for (unsigned bad = 0; bad < 35; ++bad) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        if (bad == 0) f.slot_class = nullptr;
        if (bad == 1) f.panel_class = f.nodes[SlotClass].data();
        if (bad == 2) put(f.nodes[PanelClass].data(), 0x18, std::uint64_t{0xb1000000b});
        if (bad == 3) put(f.nodes[PanelClass].data(), 0x20, static_cast<void*>(f.nodes[AssetPackage].data()));
        if (bad == 4) put(f.nodes[Meta].data(), 0x20, static_cast<void*>(f.nodes[AssetPackage].data()));
        if (bad == 5) put(f.nodes[UmgPackage].data(), 0x10, static_cast<void*>(f.nodes[TextureClass].data()));
        if (bad == 6) put(f.nodes[EdgeSlot].data(), 0x10, static_cast<void*>(f.nodes[TextureClass].data()));
        if (bad == 7) put(f.nodes[Root].data(), 0x10, static_cast<void*>(f.nodes[TextureClass].data()));
        if (bad == 8) put(f.nodes[SlotClass].data(), 0x38, std::int32_t{-1});
        if (bad == 9) put(f.nodes[PanelClass].data(), 0x38, theme::kAncestryIndex + 1);
        if (bad == 10) put(f.nodes[SlotDerivedClass].data(), 0x38, std::int32_t{-1});
        if (bad == 11) put(f.nodes[PanelDerivedClass].data(), 0x38, theme::kAncestryIndex + 1);
        if (bad == 12) f.derived_slot_chain[1] = reinterpret_cast<std::uintptr_t>(f.slot_class);
        if (bad == 13) f.derived_panel_chain[1] = reinterpret_cast<std::uintptr_t>(f.panel_chain.data());
        if (bad == 14) put(f.nodes[SlotDerivedClass].data(), 0x38, std::int32_t{0});
        if (bad == 15) put(f.nodes[SlotDerivedClass].data(), 0x30, static_cast<void*>(nullptr));
        if (bad == 16) f.root_slots[0] = nullptr;
        if (bad == 17) put(f.nodes[TargetNode].data(), 0x28, static_cast<void*>(nullptr));
        if (bad == 18) put(f.nodes[EdgeSlot].data(), 0x28, static_cast<void*>(nullptr));
        if (bad == 19) put(f.nodes[EdgeSlot].data(), 0x30, static_cast<void*>(f.nodes[OtherTarget].data()));
        if (bad == 20) put(f.nodes[Root].data(), 0x110, std::int32_t{-1});
        if (bad == 21) put(f.nodes[Root].data(), 0x110, theme::kPanelSlots + 1);
        if (bad == 22) put(f.nodes[Root].data(), 0x114, std::int32_t{1});
        if (bad == 23) put(f.nodes[Root].data(), 0x114, theme::kPanelSlots + 1);
        if (bad == 24) put(f.nodes[Root].data(), 0x108, static_cast<void*>(nullptr));
        if (bad == 25) put(f.nodes[Root].data(), 0x108, std::numeric_limits<std::uintptr_t>::max() - 7);
        if (bad == 26) put(f.items.data(), EdgeSlot * 0x18 + 8, std::int32_t{1 << 28});
        if (bad == 27) put(f.nodes[SlotDerivedClass].data(), 0x10, static_cast<void*>(f.nodes[TextureClass].data()));
        if (bad == 28) put(f.nodes[UmgPackage].data(), 0x18, std::uint64_t{0xd1000000d});
        if (bad == 29) put(f.items.data(), PanelClass * 0x18 + 8, std::int32_t{1 << 28});
        if (bad == 30) f.panel_class = nullptr;
        if (bad == 31) put(f.nodes[SlotDerivedClass].data(), 0x30, std::numeric_limits<std::uintptr_t>::max() - 7);
        if (bad == 32) put(f.nodes[SlotClass].data(), 0x30, static_cast<void*>(nullptr));
        if (bad == 33 || bad == 34) {
            put(f.nodes[SlotDerivedClass].data(), 0x10, static_cast<void*>(f.nodes[DerivedMeta].data()));
            if (bad == 33) f.derived_meta_chain[1] = reinterpret_cast<std::uintptr_t>(f.nodes[Meta].data());
            else put(f.nodes[DerivedMeta].data(), 0x10, static_cast<void*>(f.nodes[TextureClass].data()));
        }
        const auto t = reader_target();
        require(!theme::target_valid(f.access(), authority(), t),
            "reject wrong base/type/name/package/chain address, membership, identity or policy bounds");
    }
    for (bool sharing : {false, true}) for (auto drift : {ReadDrift::Membership, ReadDrift::Content, ReadDrift::Parent, ReadDrift::ChildSlot,
        ReadDrift::Header, ReadDrift::Chain, ReadDrift::Class, ReadDrift::BaseSlot,
        ReadDrift::BaseIndex, ReadDrift::Root, ReadDrift::Receiver, ReadDrift::Item, ReadDrift::MetaChain}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        const auto t = reader_target();
        if (drift == ReadDrift::MetaChain)
            put(f.nodes[SlotDerivedClass].data(), 0x10, static_cast<void*>(f.nodes[DerivedMeta].data()));
        f.read_drift = drift;
        require(!theme::target_valid(f.access(), sharing ? classifier_authority() : authority(), t) && f.slot_index_reads >= 3,
            "actual identity-reader injects drift during edge reread; no stale path acceptance");
    }
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        put(f.nodes[SlotDerivedClass].data(), 0x10, static_cast<void*>(f.nodes[DerivedMeta].data()));
        put(f.nodes[PanelDerivedClass].data(), 0x10, static_cast<void*>(f.nodes[DerivedMeta].data()));
        const auto t = reader_target();
        require(theme::target_valid(f.access(), authority(), t),
            "real panel ancestry also authenticates derived UClass metaclass, not names alone");
    }
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        put(f.nodes[Tree].data(), 0x28, static_cast<void*>(f.nodes[TargetNode].data()));
        const auto t = reader_target();
        require(theme::target_valid(f.access(), authority(), t),
            "root==target terminates without reading/requiring null root.Slot");
    }
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        put(f.nodes[OtherRoot].data(), 0x10, static_cast<void*>(f.nodes[PanelDerivedClass].data()));
        put(f.nodes[OtherRoot].data(), 0x28, static_cast<void*>(f.nodes[OtherEdgeSlot].data()));
        put(f.nodes[OtherRoot].data(), 0x108, f.root_slots.data());
        put(f.nodes[OtherRoot].data(), 0x110, std::int32_t{2});
        put(f.nodes[OtherRoot].data(), 0x114, std::int32_t{2});
        put(f.nodes[EdgeSlot].data(), 0x28, static_cast<void*>(f.nodes[OtherRoot].data()));
        put(f.nodes[OtherEdgeSlot].data(), 0x28, static_cast<void*>(f.nodes[OtherRoot].data()));
        put(f.nodes[OtherEdgeSlot].data(), 0x30, static_cast<void*>(f.nodes[OtherRoot].data()));
        const auto t = reader_target();
        require(!theme::target_valid(f.access(), authority(), t), "validly typed forward-edge cycle rejected");
    }
    for (const auto edges : {theme::kContainmentDepth, theme::kContainmentDepth + 1}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        // Extra synthetic objects are heap-owned only by this offline test.
        std::vector<std::array<std::uint8_t, 0x200>> extra(2 * edges - 1);
        require(Nodes + extra.size() <= 128, "fixture registry capacity");
        for (std::size_t i = 0; i < extra.size(); ++i) {
            auto* p = extra[i].data();
            put(p, 0x0c, static_cast<std::int32_t>(Nodes + i));
            put(p, 0x18, std::uint64_t{500} + i);
            put(p, 0x20, static_cast<void*>(f.nodes[Tree].data()));
            put(f.items.data(), (Nodes + i) * 0x18, static_cast<void*>(p));
        }
        const auto elements = static_cast<std::int32_t>(Nodes + extra.size());
        put(f.registry.data(), 0x20, elements); put(f.registry.data(), 0x24, elements);
        for (std::size_t i = 0; i < edges; ++i) {
            void* child = i == 0 ? static_cast<void*>(f.nodes[TargetNode].data()) : extra[i - 1].data();
            void* parent = i + 1 == edges ? static_cast<void*>(f.nodes[Root].data()) : extra[i].data();
            void* slot = extra[edges - 1 + i].data();
            put(slot, 0x10, static_cast<void*>(f.nodes[SlotDerivedClass].data()));
            put(slot, 0x28, parent); put(slot, 0x30, child);
            put(child, 0x28, slot);
            put(parent, 0x10, static_cast<void*>(f.nodes[PanelDerivedClass].data()));
            put(parent, 0x180, slot);
            put(parent, 0x108, static_cast<std::uint8_t*>(parent) + 0x180);
            put(parent, 0x110, std::int32_t{1}); put(parent, 0x114, std::int32_t{1});
        }
        const auto t = reader_target();
        require(theme::target_valid(f.access(), authority(), t) == (edges == theme::kContainmentDepth),
            "explicit depth policy accepts exact cap, rejects one beyond");
    }
    require(calls == native_calls, "containment classifier performs NO native calls or getters");
}

void read_batch_boundary_tests()
{
    static unsigned boundary = 0;
    static const theme::Subject* subject = nullptr;
    static bool observed = false;
    for (boundary = 0; boundary < 7; ++boundary) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image);
        const auto a = classifier_authority(); theme::Subject current;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::Image, {}, a, current)), "read batch boundary subject");
        subject = &current; observed = false;
        const std::array<theme::SubjectPreflight, 2> groups{{{subject, {}}, {subject, {}}}};
        f.read_probe = []() noexcept {
            observed = true;
            auto& f = *fixture;
            if (boundary == 3) {
                theme::measured_native(game::menu_theme_metrics::Metric::NativeStaticClass, fake_class); return;
            }
            if (boundary == 4) {
                std::size_t slot = 0;
                require(theme::reserve_record(UINT64_MAX, slot).outcome == theme::Outcome::Budget, "read batch allocation boundary"); return;
            }
            if (boundary == 5) { theme::EntryScope callback; return; }
            const auto nested = classifier_authority();
            if (boundary == 0) f.classifier_current = false;
            if (boundary == 1) f.qualified = false;
            if (boundary == 6) ++f.classifier_epoch;
            // Even restored admission cannot resurrect an interrupted parent.
            (void)theme::validate_subject(*subject, nested.session);
            f.classifier_current = true; f.qualified = true;
            f.classifier_epoch = reinterpret_cast<std::uintptr_t>(nested.context);
        };
        theme::BatchProgress progress{};
        const auto measured = measure_proofs([&] {
            require(!theme::preflight_subjects(groups, a, progress) && observed, "interrupted read batch rejects nested/revoked/helper/allocation/callback proof");
        });
        require(measured.native == (boundary == 3 ? 1u : 0u) && !theme::read_phase && !theme::classifier_phase,
            "read batch boundary performs no retries and clears private authority");
        require(static_cast<bool>(theme::preflight_subjects(groups, a, progress)), "new independent batch reacquires after boundary");
    }
    subject = nullptr;
    for (bool guarded : {false, true}) for (auto drift : {ReadDrift::Membership, ReadDrift::Content, ReadDrift::Parent,
        ReadDrift::ChildSlot, ReadDrift::Header, ReadDrift::Chain, ReadDrift::Class, ReadDrift::BaseSlot,
        ReadDrift::BaseIndex, ReadDrift::Root, ReadDrift::Receiver, ReadDrift::Item, ReadDrift::MetaChain}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image);
        const auto a = guarded ? classifier_authority() : authority(); theme::Subject current;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::Image, {}, a, current)), "read batch drift subject");
        if (drift == ReadDrift::MetaChain) put(f.nodes[SlotDerivedClass].data(), 0x10, static_cast<void*>(f.nodes[DerivedMeta].data()));
        f.read_drift = drift;
        const std::array<theme::SubjectPreflight, 1> groups{{{&current, {}}}};
        theme::BatchProgress progress{}; const auto calls = native_calls;
        require(!theme::preflight_subjects(groups, a, progress) && f.slot_index_reads >= 3 && native_calls == calls,
            "guarded/null-guard batch rejects actual-reader containment/header/class/identity drift");
    }
    for (bool guarded : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image);
        const auto a = guarded ? classifier_authority() : authority(); theme::Subject current; theme::OwnedImage original;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::Image, {}, a, current))
            && static_cast<bool>(theme::capture_original(current, {}, a, original)), "post-classifier containment fixture");
        const std::array<theme::BrushAssignment, 1> images{{{{}, &original}}};
        const std::array<theme::SubjectPreflight, 1> groups{{{&current, images}}};
        f.graph_probe = []() noexcept { fixture->root_slots[0] = fixture->nodes[OtherEdgeSlot].data(); };
        theme::BatchProgress progress{}; const auto calls = native_calls;
        require(!theme::preflight_subjects(groups, a, progress) && !f.graph_probe && native_calls == calls,
            "batch rechecks saved containment edges after retained graph reads, not merely leaf identity");
        f.root_slots[0] = f.nodes[EdgeSlot].data();
        require(static_cast<bool>(theme::preflight_subjects(groups, a, progress)), "restored path needs independent fresh batch proof");
        const std::array<theme::ResolveMember, 1> requests{{{&current, {}, &original}}};
        std::array<theme::ResolvedMember, 1> outputs{};
        f.graph_probe = []() noexcept { fixture->root_slots[0] = fixture->nodes[OtherEdgeSlot].data(); };
        require(!theme::resolve_members(requests, a, outputs, progress) && !f.graph_probe && native_calls == calls,
            "resolver final containment follows graph reads with and without metadata guard");
    }
    for (bool target_capacity : {false, true}) for (bool drift : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image);
        const auto a = classifier_authority(); theme::Subject current; theme::OwnedImage original;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::Image, {}, a, current))
            && static_cast<bool>(theme::capture_original(current, {}, a, original)), "witness overflow source");
        put(f.nodes[OtherTarget].data(), 0x10, static_cast<void*>(f.nodes[HolderClass].data()));
        put(f.nodes[OtherTarget].data(), 0x20, static_cast<void*>(f.nodes[Tree].data()));
        const auto target = reader_target(); auto sibling = target; auto access = f.access();
        require(theme::capture(access, f.nodes[OtherTarget].data(), sibling.target), "witness overflow sibling");
        const auto calls = native_calls;
        theme::ReadScope reads(a);
        require(theme::target_valid(access, a, sibling) && theme::read_phase->target_count == 1
            && theme::read_phase->edge_count, "witness overflow authentic saved path");
        auto& phase = *theme::read_phase;
        // Fill bounded scratch with genuine witnesses: capacity exhaustion is a
        // fallback, never a limit on the number of menu objects admitted.
        if (target_capacity) {
            phase.targets.fill(sibling); phase.target_count = phase.targets.size();
        } else {
            phase.edges.fill(phase.edges[0]); phase.edge_count = phase.edges.size();
        }
        const auto saved = phase.target_count;
        require(theme::target_valid(access, a, target) && phase.target_count == saved && !phase.witnessed,
            "full witness scratch leaves subsequent target untracked, not rejected");
        if (drift) f.graph_probe = []() noexcept { fixture->root_slots[0] = fixture->nodes[OtherEdgeSlot].data(); };
        require(theme::graph_valid(theme::state.functions, access, theme::state.retained[theme::state.reserved_images - 1]) && !f.graph_probe,
            "untracked target graph read is real");
        require(theme::finish_target(access, a, target) == !drift && native_calls == calls,
            "untracked target reacquires full containment after graph reads");
        if (!drift) require(theme::finish_reads(access, a), "witness overflow fallback remains usable");
    }
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image);
        const auto a = classifier_authority(); theme::Subject current;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::Image, {}, a, current)), "nested native reader fixture");
        f.cursor_validating_subject = &current;
        const std::array<theme::ResolveMember, 1> requests{{{&current, {}}}};
        std::array<theme::ResolvedMember, 1> outputs{}; theme::BatchProgress progress{};
        require(static_cast<bool>(theme::resolve_members(requests, a, outputs, progress)) && progress.created == 1
            && f.cursor_nested_validation, "actual native helper suspends private batch proof for independent nested facts-only reader");
    }
}
void carrier_name_cache_tests()
{
    ProofCounts uncached{};
    for (bool cached : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Button);
        const auto a = classifier_authority(); theme::Subject subject;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::Button, {}, a, subject)), "carrier name cache subject");
        for (unsigned i = 0; i < 4; ++i) f.nodes[TargetNode][0x128 + i * 0x88] = static_cast<std::uint8_t>(11 + i);
        const auto measured = measure_proofs([&] {
            for (unsigned i = 0; i < 4; ++i) {
                if (!cached) theme::state.carrier_names = {}; // Controlled former four-lookups-per-carrier path.
                theme::OwnedImage original;
                require(static_cast<bool>(theme::capture_original(subject, {0, static_cast<theme::ButtonBrush>(i)}, a, original)),
                    "carrier name cache real owning capture");
            }
        });
        if (!cached) uncached = measured;
        else {
            require(uncached.names == 16 && measured.names == 4 && measured.native + 12 == uncached.native
                && measured.constructs == 4 && measured.constructs == uncached.constructs
                && measured.copies == 4 && measured.copies == uncached.copies,
                "constant name cache removes lookups, not rooted construction or owning brush copies");
            print_proofs("carrier FName cache", uncached, measured);
        }
    }
}
void resolution_batch_tests()
{
    read_batch_boundary_tests();
    carrier_name_cache_tests();
    using Kind = theme::SubjectKind;
    const std::array<float, 4> rgba{0.15f, 0.35f, 0.55f, 0.75f};
    for (const auto kind : {Kind::Button, Kind::ImageSet}) {
        ProofCounts cold_before{}, warm_before{}, preflight_before{};
        for (bool batch : {false, true}) {
            Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(kind); f.grow_holders = true;
            put(f.registry.data(), 0x20, static_cast<std::int32_t>(f.items.size() / 0x18));
            const auto a = classifier_authority(); theme::Subject subject;
            require(static_cast<bool>(theme::find_subject(L"Bg_1", kind, {}, a, subject)), "resolver actual-reader subject");
            const std::size_t count = kind == Kind::Button ? 4 : 2;
            std::array<theme::BrushSelection, 4> members{};
            for (std::size_t i = 0; i < count; ++i) {
                if (kind == Kind::Button) members[i].button = static_cast<theme::ButtonBrush>(i);
                else members[i].index = static_cast<std::int32_t>(i);
                // Distinct real owning brush facts prevent accidental dedup from
                // making the cold native-call comparison vacuous.
                auto* brush = kind == Kind::Button ? f.nodes[TargetNode].data() + 0x128 + i * 0x88 : f.images.data() + i * 0xb0;
                brush[0] = static_cast<std::uint8_t>(11 + i);
            }
            theme::OwnedImage texture;
            core::ThemeImage pixels{std::vector<std::uint8_t>(16, 77), 2, 2};
            require(static_cast<bool>(theme::create(pixels, a, texture)), "resolver upload remains explicit and upstream");
            std::array<theme::ResolveMember, 4> requests{};
            std::array<theme::ResolvedMember, 4> resolved{};
            std::array<theme::OwnedImage, 4> originals{}, themed{};
            for (std::size_t i = 0; i < count; ++i) requests[i] = {&subject, members[i], nullptr, &texture, true, rgba};
            theme::BatchProgress progress{};
            const auto execute = [&] {
                if (batch) {
                    require(static_cast<bool>(theme::resolve_members(std::span(requests).first(count), a,
                        std::span(resolved).first(count), progress)) && progress.completed == count
                        && progress.failed_index == theme::kInvalidImageSlot, "combined resolution completes owning values");
                    for (std::size_t i = 0; i < count; ++i) { originals[i] = resolved[i].original; themed[i] = resolved[i].image; }
                } else {
                    theme::CaptureProgress capture{};
                    require(static_cast<bool>(theme::capture_original_members(subject, std::span(members).first(count), a,
                        std::span(originals).first(count), capture)), "individual capture stage baseline");
                    for (std::size_t i = 0; i < count; ++i) {
                        theme::OwnedImage composed;
                        const auto compose_result = theme::compose(originals[i], texture, a, composed);
                        if (!compose_result) std::cerr << "individual compose: " << compose_result.stage << " outcome " << static_cast<unsigned>(compose_result.outcome) << '\n';
                        require(static_cast<bool>(compose_result), "individual compose baseline");
                        const auto tint_result = theme::tint(composed, rgba, a, themed[i]);
                        if (!tint_result) std::cerr << "individual tint: " << tint_result.stage << " outcome " << static_cast<unsigned>(tint_result.outcome) << '\n';
                        require(static_cast<bool>(tint_result), "individual tint baseline");
                    }
                }
            };
            const auto cold = measure_proofs(execute);
            require(cold.names == 4 && cold.constructs == count * 3 && cold.copies == count * 3 && cold.colors == count,
                "carrier names resolve once; real construction/copy/owning tint counts unchanged");
            if (!batch) cold_before = cold;
            else {
                require(progress.created == count * 3 && cold.native == cold_before.native
                    && cold.reads < cold_before.reads && cold.qualifiers < cold_before.qualifiers,
                    "cold batch removes read proofs, not owning helpers or baseline work");
                print_proofs(kind == Kind::Button ? "cold resolve button" : "cold resolve imageset", cold_before, cold);
            }
            const auto records = theme::state.reserved_images, bytes = theme::state.reserved_bytes;
            const auto warm = measure_proofs(execute);
            require(!warm.native && theme::state.reserved_images == records && theme::state.reserved_bytes == bytes,
                "warm resolution never allocates/uploads/constructs");
            if (!batch) warm_before = warm;
            else {
                require(warm.targets == 1 && warm.basic == 2 && warm.qualifiers == 1 && !progress.created
                    && warm.reads < warm_before.reads / 2, "warm whole member group shares actual target/resource reads");
                print_proofs(kind == Kind::Button ? "warm resolve button" : "warm resolve imageset", warm_before, warm);
            }
            std::array<theme::BrushAssignment, 8> assignments{};
            for (std::size_t i = 0; i < count; ++i) { assignments[i * 2] = {members[i], &themed[i]}; assignments[i * 2 + 1] = {members[i], &originals[i]}; }
            const std::array<theme::SubjectPreflight, 1> group{{{&subject, std::span(assignments).first(count * 2)}}};
            const auto checked = measure_proofs([&] {
                if (batch) require(static_cast<bool>(theme::preflight_subjects(group, a, progress)) && progress.completed == 1, "next/prior grouped preflight");
                else for (std::size_t i = 0; i < count * 2; ++i) {
                    const void* brush = nullptr;
                    require(static_cast<bool>(theme::borrow_brush(*assignments[i].image, a, brush)) && brush, "individual next/prior borrow preflight");
                }
            });
            if (!batch) preflight_before = checked;
            else {
                require(checked.targets == 1 && checked.basic == 2 && checked.qualifiers == 1 && !checked.native
                    && checked.reads < preflight_before.reads / 2, "member preflight avoids next/prior target/graph rereads");
                print_proofs(kind == Kind::Button ? "member preflight button" : "member preflight imageset", preflight_before, checked);
                assignments[1].image = &originals[1];
                const auto calls = native_calls;
                require(!theme::preflight_subjects(group, a, progress) && native_calls == calls, "batched preflight rejects cross-member affinity");
                assignments[1].image = &originals[0];
                requests[0].original = &originals[1];
                require(!theme::resolve_members(std::span(requests).first(count), a, std::span(resolved).first(count), progress)
                    && native_calls == calls && !progress.completed, "resolver rejects foreign member baseline before construction");
                requests[0].original = &originals[0];
                for (std::size_t i = 1; i < count; ++i) requests[i].original = &originals[i];
                require(static_cast<bool>(theme::resolve_members(std::span(requests).first(count), a, std::span(resolved).first(count), progress))
                    && !progress.created && native_calls == calls, "retained Original route never rerecaptures genuine source");
                auto unshared = a; unshared.phase_current = nullptr;
                const auto fallback = measure_proofs([&] {
                    require(static_cast<bool>(theme::resolve_members(std::span(requests).first(count), unshared,
                        std::span(resolved).first(count), progress)), "null guard combined resolver");
                });
                require(!fallback.native && fallback.targets > warm.targets && fallback.reads > warm.reads,
                    "null guard does not reuse target/source proofs");
            }
            // Inspect only inside a fresh real callback admission; output remains
            // affinity-bound and preserves exact specified tint, including alpha.
            for (std::size_t i = 0; i < count; ++i) {
                const void* brush = nullptr;
                require(static_cast<bool>(theme::borrow_brush(themed[i], a, brush))
                    && std::memcmp(static_cast<const std::uint8_t*>(brush) + 0x20, rgba.data(), sizeof(rgba)) == 0,
                    "combined output keeps exact native tint alpha");
            }
        }
    }
    for (unsigned failure = 0; failure < 9; ++failure) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(Kind::Button); f.grow_holders = true;
        const auto a = classifier_authority(); theme::Subject subject;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", Kind::Button, {}, a, subject)), "resolver failure subject");
        f.nodes[TargetNode][0x1b0] = 9;
        std::array<theme::ResolveMember, 2> requests{{{&subject, {}}, {&subject, {0, theme::ButtonBrush::Hovered}}}};
        std::array<theme::ResolvedMember, 2> outputs{}; theme::BatchProgress progress{};
        if (failure == 0) f.fault_construct = true;
        if (failure == 1) f.construct_drift = TargetDrift::Retire;
        if (failure == 2) f.construct_drift = TargetDrift::ReplaceTree;
        if (failure == 3) f.capture_drift = TargetDrift::RecycleTarget;
        if (failure == 4) f.fail_after_capture = true;
        if (failure == 5) { requests[1].tint = true; requests[1].linear_rgba[0] = 2; }
        if (failure == 6) f.drift_previous_after_capture = true;
        if (failure >= 7) f.capture_revocation = failure - 6;
        const auto records = theme::state.reserved_images;
        theme::Result result{};
        const auto measured = measure_proofs([&] { result = theme::resolve_members(requests, a, outputs, progress); });
        theme::fail_ledger_allocation = false;
        require(!result && progress.failed_index < requests.size(), "batch resolver preserves factual failure index");
        require(failure != 4 || (progress.completed == 1 && progress.created == 1 && result.outcome == theme::Outcome::Memory),
            "partial allocation failure retains completed carrier charges but publishes no partial outputs");
        require(failure != 5 || (!measured.native && theme::state.reserved_images == records && !progress.created),
            "all request shapes preflight before first irreversible carrier");
        require(failure != 6 || (progress.completed == 2 && progress.created == 2 && progress.failed_index == 0),
            "later native helper cannot change an earlier genuine baseline unnoticed");
        const auto calls = native_calls;
        for (const auto& output : outputs) {
            const void* brush = nullptr;
            require(!theme::borrow_brush(output.image, a, brush) && !brush && native_calls == calls,
                "failed resolver publishes no partial owning values or stale cleanup");
        }
        require(!theme::read_phase && !theme::classifier_phase, "resolver clears stack-only proof on failure");
    }
}
void capture_member_tests()
{
    using Kind = theme::SubjectKind;
    for (const auto kind : {Kind::Button, Kind::ImageSet}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(kind);
        const auto a = authority(); theme::Subject subject;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", kind, {}, a, subject)), "capture batch subject");
        const std::size_t count = kind == Kind::Button ? 4 : 2;
        std::array<theme::BrushSelection, 4> selections{};
        std::array<theme::OwnedImage, 4> originals{}, warm{};
        for (std::size_t i = 0; i < count; ++i) {
            if (kind == Kind::Button) selections[i].button = static_cast<theme::ButtonBrush>(i);
            else selections[i].index = static_cast<std::int32_t>(i);
            require(static_cast<bool>(theme::capture_original(subject, selections[i], a, originals[i])), "warm native original fixture");
        }
        const auto input = std::span<const theme::BrushSelection>(selections.data(), count);
        auto output = std::span<theme::OwnedImage>(warm.data(), count);
        theme::target_validation_calls = 0;
        for (std::size_t i = 0; i < count; ++i)
            require(static_cast<bool>(theme::capture_original(subject, selections[i], a, warm[i])), "single warm capture baseline");
        require(theme::target_validation_calls == count * 2, "single capture shares the two-validation grouped route");
        const auto calls = native_calls; const auto records = theme::state.reserved_images;
        const auto bytes = theme::state.reserved_bytes;
        theme::target_validation_calls = 0; theme::CaptureProgress progress{};
        namespace perf = game::menu_theme_metrics;
        perf::enable(true);
        require(static_cast<bool>(theme::capture_original_members(subject, input, a, output, progress))
            && theme::target_validation_calls == 2 && progress.prepared_members == count && !progress.created_members
            && progress.published_members == count && native_calls == calls && theme::state.reserved_images == records
            && theme::state.reserved_bytes == bytes, "warm capture uses two real validations, no native call or allocation");
        perf::enable(false);
        perf::Snapshot metrics{};
        require(perf::take_snapshot(metrics)
            && metrics.stats[static_cast<std::size_t>(perf::Metric::ValidateTarget)].count == 2
            && !metrics.stats[static_cast<std::size_t>(perf::Metric::ValidateTarget)].failed
            && metrics.counters[static_cast<std::size_t>(perf::Counter::TargetBasicChecks)] == 4
            && metrics.counters[static_cast<std::size_t>(perf::Counter::CaptureHits)] == count
            && metrics.counters[static_cast<std::size_t>(perf::Counter::CapturePublishedMembers)] == count
            && !metrics.stats[static_cast<std::size_t>(perf::Metric::NativeConstruct)].count,
            "warm metrics measure real validator passes/cache hits, not native construction requests");
        auto phased = a;
        phased.phase_current = [](void* context, const game::MenuSessionSnapshot& expected) noexcept {
            const auto& current = *static_cast<Fixture*>(context);
            return current.ready && expected.phase == game::MenuSessionPhase::Ready
                && current.session.generation == expected.generation && current.session.widget == expected.widget
                && current.session.widget_identity.internal_index == expected.widget_identity.internal_index
                && current.session.widget_identity.serial_number == expected.widget_identity.serial_number;
        };
        theme::target_validation_calls = 0;
        perf::enable(true);
        require(static_cast<bool>(theme::capture_original_members(subject, input, phased, output, progress))
            && theme::target_validation_calls == 1 && native_calls == calls
            && theme::state.reserved_images == records && theme::state.reserved_bytes == bytes,
            "guarded warm read phase shares one containment proof with no native call/allocation");
        perf::enable(false);
        require(perf::take_snapshot(metrics)
            && metrics.stats[static_cast<std::size_t>(perf::Metric::ValidateTarget)].count == 1
            && metrics.counters[static_cast<std::size_t>(perf::Counter::TargetBasicChecks)] == 2
            && metrics.counters[static_cast<std::size_t>(perf::Counter::CapturePublishedMembers)] == count,
            "guarded warm capture halves actual target/basic proofs and preserves member publications");
        for (std::size_t i = 0; i < count; ++i)
            require(static_cast<bool>(theme::apply(subject, selections[i], warm[i], a)), "each warm output has current member affinity");
        require(theme::apply(subject, selections[1], warm[0], a).outcome == theme::Outcome::Identity,
            "shared original storage does not merge distinct member affinity");
        if (kind == Kind::Button) {
            auto* sibling = f.nodes[OtherTarget].data();
            put(sibling, 0x10, f.generic_slots[static_cast<std::size_t>(kind) + 2]);
            put(sibling, 0x18, std::uint64_t{0xa0000000a});
            put(sibling, 0x20, static_cast<void*>(f.nodes[Tree].data()));
            // Fake native fixture owns this initial full style, not production assignment.
            std::memcpy(sibling + 0x120, f.nodes[TargetNode].data() + 0x120, 0x278);
            theme::Subject other; std::array<theme::OwnedImage, 1> foreign{};
            require(static_cast<bool>(theme::find_subject(L"Bg_2", kind, {}, a, other))
                && static_cast<bool>(theme::capture_original_members(other, input.first(1), a, foreign, progress)),
                "equivalent foreign subject receives its own new current affinity");
            const auto before_apply = native_calls;
            require(theme::apply(subject, {}, foreign[0], a).outcome == theme::Outcome::Identity
                && native_calls == before_apply, "foreign capture output cannot restore this subject");
        }
        // A legitimate resource change is a real miss, never a stale cache hit.
        void* changed = kind == Kind::Button ? static_cast<void*>(f.nodes[TargetNode].data() + 0x1b0)
            : static_cast<void*>(f.images.data() + 0xb0);
        put(changed, 0x48, static_cast<void*>(nullptr));
        theme::target_validation_calls = 0;
        require(static_cast<bool>(theme::capture_original_members(subject, input, phased, output, progress))
            && progress.created_members == 1 && progress.published_members == count && theme::target_validation_calls > 1,
            "a miss ends the shared proof: allocation/native work retains fresh full validations and current affinity");
        put(changed, 0x48, static_cast<void*>(f.nodes[AssetResource].data()));
        std::array<theme::BrushSelection, 2> duplicate{selections[0], selections[0]};
        const auto rejected_calls = native_calls;
        require(theme::capture_original_members(subject, duplicate, a, output.first(2), progress).outcome == theme::Outcome::Input
            && !progress.published_members && native_calls == rejected_calls, "duplicate capture members fail before native work");
        require(theme::capture_original_members(subject, input, a, output.first(1), progress).outcome == theme::Outcome::Input,
            "capture output shape mismatch");
        auto invalid = selections;
        if (kind == Kind::Button) invalid[0].button = static_cast<theme::ButtonBrush>(4);
        else invalid[0].index = 2;
        theme::target_validation_calls = 0;
        require(theme::capture_original_members(subject, std::span<const theme::BrushSelection>(invalid.data(), count),
            a, output, progress).outcome == theme::Outcome::Input && !theme::target_validation_calls
            && native_calls == rejected_calls, "invalid member rejects in metadata-only phase");
        if (kind == Kind::ImageSet) {
            put(f.nodes[TargetNode].data(), 0x110, std::int32_t{1});
            require(theme::capture_original_members(subject, input, a, output, progress).outcome == theme::Outcome::Input
                && native_calls == rejected_calls, "changed current array bounds reject requested entry");
            put(f.nodes[TargetNode].data(), 0x110, std::int32_t{2});
        }
        ++f.session.generation;
        require(theme::capture_original_members(subject, input, authority(), output, progress).outcome == theme::Outcome::Identity,
            "old subject cannot manufacture current generation affinities");
    }
    for (unsigned failure = 0; failure < 10; ++failure) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(Kind::Button);
        const auto a = authority(); theme::Subject subject;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", Kind::Button, {}, a, subject)), "cold capture batch subject");
        // Distinct true native brush facts, so the second capture is a real miss.
        f.nodes[TargetNode][0x1b0] = 9;
        const std::array<theme::BrushSelection, 2> selections{{{}, {0, theme::ButtonBrush::Hovered}}};
        std::array<theme::OwnedImage, 2> outputs{}; theme::CaptureProgress progress{};
        const auto records = theme::state.reserved_images; const auto bytes = theme::state.reserved_bytes;
        if (failure == 0) f.fault_fname = true;
        if (failure == 1) { f.fault_construct = true; game::menu_theme_metrics::enable(true); }
        if (failure == 2) f.construct_drift = TargetDrift::Retire;
        if (failure == 3 || failure == 4) { f.fault_capture = true; f.fault_after_copy = failure == 4; }
        if (failure == 5) f.capture_drift = TargetDrift::Retire;
        if (failure == 6) f.capture_drift = TargetDrift::Disable;
        if (failure == 7) {
            require(static_cast<bool>(theme::capture_original(subject, selections[0], a, outputs[0])), "partial allocation failure warm first member");
            theme::fail_ledger_allocation = true;
        }
        if (failure == 8) { f.capturing_subject = &subject; f.capturing_members = selections; }
        if (failure == 9) f.fail_after_capture = true;
        const auto result = theme::capture_original_members(subject, selections, a, outputs, progress);
        if (failure == 1) {
            namespace perf = game::menu_theme_metrics;
            perf::enable(false);
            perf::Snapshot metrics{};
            require(perf::take_snapshot(metrics)
                && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeConstruct)].count == 1
                && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeConstruct)].failed == 1
                && !metrics.counters[static_cast<std::size_t>(perf::Counter::MalformedScopes)],
                "native SEH measurement balances its scope and records one failed call without retry");
        }
        f.capturing_subject = nullptr;
        if (failure == 8) {
            require(static_cast<bool>(result) && progress.created_members == 2 && progress.published_members == 2
                && f.capture_reentries, "cold owning captures reject native reentry and publish full outputs");
        } else {
            require(result.outcome == (failure == 7 || failure == 9 ? theme::Outcome::Memory : theme::Outcome::NativeFault)
                && !progress.published_members && theme::state.reserved_images > records
                && theme::state.reserved_bytes > bytes, "failed attempts stay charged, outputs publish atomically");
            if (failure == 7) require(progress.prepared_members == 1 && !progress.created_members && !theme::state.poisoned,
                "allocation failure reports completed warm work without partial outputs");
            if (failure == 9) require(progress.prepared_members == 1 && progress.created_members == 1 && !theme::state.poisoned,
                "later allocation failure retains completed native holder but publishes no partial handles");
            const void* brush = nullptr;
            for (auto& out : outputs) require(!theme::borrow_brush(out, a, brush), "no usable partial capture output");
            require(std::count(f.events.begin(), f.events.end(), "brush_dtor") == 0
                && std::count(f.events.begin(), f.events.end(), "style_destroy") == 0, "no stale native cleanup");
        }
    }
    for (bool destroy_cache : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(Kind::Button);
        const auto a = authority(); theme::Subject subject; theme::OwnedImage original;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", Kind::Button, {}, a, subject))
            && static_cast<bool>(theme::capture_original(subject, {}, a, original)), "cached graph failure fixture");
        if (destroy_cache) put(f.items.data(), Holder * 0x18 + 8, std::int32_t{0});
        else f.retire_on_cached_holder_read = true;
        std::array<theme::BrushSelection, 1> selections{}; std::array<theme::OwnedImage, 1> outputs{};
        const auto calls = native_calls; theme::CaptureProgress progress{};
        require(theme::capture_original_members(subject, selections, a, outputs, progress).outcome == theme::Outcome::NativeFault
            && !progress.published_members && theme::state.poisoned && native_calls == calls,
            "byte equality never admits lost root or retirement during warm graph reads");
    }
}
void multi_member_tests(const core::ThemeImage& pixels)
{
    using Kind = theme::SubjectKind;
    for (const auto kind : {Kind::Button, Kind::ImageSet}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(kind);
        auto a = authority(); theme::Subject subject;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", kind, {}, a, subject)), "batch subject");
        const std::size_t count = kind == Kind::Button ? 4 : 2;
        std::array<theme::OwnedImage, 4> originals{}, composed{};
        std::array<theme::BrushAssignment, 4> writes{}, restores{};
        theme::OwnedImage texture;
        require(static_cast<bool>(theme::create(pixels, a, texture)), "batch texture prepared once");
        std::array<std::uint8_t, 0x278> original_style{};
        std::memcpy(original_style.data(), f.nodes[TargetNode].data() + 0x120, original_style.size());
        const auto original_images = f.images;
        for (std::size_t i = 0; i < count; ++i) {
            theme::BrushSelection selection{};
            if (kind == Kind::Button) selection.button = static_cast<theme::ButtonBrush>(i);
            else selection.index = static_cast<std::int32_t>(i);
            require(static_cast<bool>(theme::capture_original(subject, selection, a, originals[i]))
                && static_cast<bool>(theme::compose(originals[i], texture, a, composed[i])), "batch immutable member sources");
            writes[i] = {selection, &composed[i]}; restores[i] = {selection, &originals[i]};
        }
        const auto members = std::span<const theme::BrushAssignment>(writes.data(), count);
        const auto restore = std::span<const theme::BrushAssignment>(restores.data(), count);
        theme::ApplyProgress progress{};
        const auto reject = [&](std::span<const theme::BrushAssignment> input, theme::Outcome outcome) {
            const auto calls = native_calls;
            require(theme::apply_members(subject, input, a, progress).outcome == outcome
                && !progress.attempted_members && !progress.completed_members && !progress.refresh_complete
                && native_calls == calls, "batch preflight rejects without native mutation");
        };
        reject({}, theme::Outcome::Input);
        std::array<theme::BrushAssignment, 2> duplicate{writes[0], writes[0]};
        reject(duplicate, theme::Outcome::Input);
        auto invalid = writes[0]; invalid.image = nullptr;
        reject(std::span<const theme::BrushAssignment>(&invalid, 1), theme::Outcome::Input);
        theme::OwnedImage missing;
        invalid.image = &missing;
        reject(std::span<const theme::BrushAssignment>(&invalid, 1), theme::Outcome::Input);
        invalid = writes[0];
        if (kind == Kind::Button) invalid.member.button = static_cast<theme::ButtonBrush>(4);
        else invalid.member.index = 2;
        reject(std::span<const theme::BrushAssignment>(&invalid, 1), theme::Outcome::Input);
        invalid = writes[1]; invalid.image = &originals[0];
        reject(std::span<const theme::BrushAssignment>(&invalid, 1), theme::Outcome::Identity);
        // An otherwise well-typed sibling is still a foreign restoration owner.
        put(f.nodes[OtherTarget].data(), 0x10, f.generic_slots[static_cast<std::size_t>(kind) + 2]);
        put(f.nodes[OtherTarget].data(), 0x18, std::uint64_t{0xa0000000a});
        put(f.nodes[OtherTarget].data(), 0x20, static_cast<void*>(f.nodes[Tree].data()));
        if (kind == Kind::ImageSet) {
            put(f.nodes[OtherTarget].data(), 0x108, f.images.data());
            put(f.nodes[OtherTarget].data(), 0x110, std::int32_t{2});
            put(f.nodes[OtherTarget].data(), 0x114, std::int32_t{2});
        }
        theme::Subject sibling; theme::OwnedImage foreign;
        require(static_cast<bool>(theme::find_subject(L"Bg_2", kind, {}, a, sibling))
            && static_cast<bool>(theme::capture_original(sibling, {}, a, foreign)), "foreign native brush owner");
        invalid = writes[0]; invalid.image = &foreign;
        reject(std::span<const theme::BrushAssignment>(&invalid, 1), theme::Outcome::Identity);
        f.qualified = false; reject(members, theme::Outcome::Authority); f.qualified = true;
        const auto constructions = f.constructions, textures = f.textures;
        f.memory_reads = 0; f.events.clear();
        for (std::size_t i = 0; i < count; ++i)
            require(static_cast<bool>(theme::apply(subject, writes[i].member, composed[i], a)), "individual member baseline");
        const auto individual_reads = f.memory_reads;
        require(static_cast<bool>(theme::apply_members(subject, restore, a, progress)), "restore individual baseline");
        f.memory_reads = 0; f.events.clear(); f.adapter_calls = f.adapter_attempts = 0;
        f.batching_subject = &subject; f.batching_members = members;
        require(static_cast<bool>(theme::apply_members(subject, members, a, progress))
            && progress.attempted_members == count && progress.completed_members == count && progress.refresh_complete,
            "batch completes all members and renderer refresh");
        require(f.memory_reads < individual_reads && f.batch_reentries == (kind == Kind::Button ? 9u : 4u),
            "actual-reader reduction and Busy nested batch at every native helper");
        require(f.adapter_calls == (kind == Kind::Button ? 9u : 4u)
            && std::count(f.events.begin(), f.events.end(), "own_brush") == static_cast<std::ptrdiff_t>(count)
            && std::count(f.events.begin(), f.events.end(), "invalidate") == 1,
            "one refresh with one owning assignment per changed member");
        if (kind == Kind::Button) require(std::count(f.events.begin(), f.events.end(), "style_construct") == 1
            && std::count(f.events.begin(), f.events.end(), "own_style") == 1
            && std::count(f.events.begin(), f.events.end(), "button_style") == 1
            && std::count(f.events.begin(), f.events.end(), "style_destroy") == 1 && !f.style_live,
            "four Button brushes use one complete owning style transaction");
        else require(std::count(f.events.begin(), f.events.end(), "synchronize") == 1,
            "multiple ImageSet entries use one synchronization");
        f.batching_subject = nullptr;
        require(static_cast<bool>(theme::apply_members(subject, restore, a, progress)), "owning batch original restoration");
        if (kind == Kind::Button) require(std::memcmp(original_style.data(), f.nodes[TargetNode].data() + 0x120,
            original_style.size()) == 0, "all Button brushes and untouched style fields restored exactly");
        else require(f.images == original_images, "all ImageSet entries, transforms and pivots restored exactly");
        require(static_cast<bool>(theme::apply_members(subject, members.last(1), a, progress)), "sparse member batch");
        if (kind == Kind::Button) {
            const std::size_t changed = 0x1a0;
            for (std::size_t i = 0; i < original_style.size(); ++i)
                if (i < changed || i >= changed + 0x88)
                    require(f.nodes[TargetNode][0x120 + i] == original_style[i], "untouched style members/sounds/padding preserved");
        } else require(std::memcmp(f.images.data(), original_images.data(), 0xb0) == 0,
            "unselected entry brush and geometry remain unchanged");
        require(static_cast<bool>(theme::apply_members(subject, restore.last(1), a, progress)), "sparse original restore");
        require(f.constructions == constructions && f.textures == textures
            && std::count(f.events.begin(), f.events.end(), "update") == 0, "no switch allocation or texture reupload");
    }
    for (const auto kind : {Kind::Button, Kind::ImageSet}) {
        const unsigned phases = kind == Kind::Button ? 9 : 4;
        for (unsigned phase = 1; phase <= phases; ++phase) for (unsigned failure = 0; failure < 4; ++failure) {
            Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(kind);
            auto a = authority(); theme::Subject subject; theme::OwnedImage texture;
            require(static_cast<bool>(theme::find_subject(L"Bg_1", kind, {}, a, subject))
                && static_cast<bool>(theme::create(pixels, a, texture)), "batch failure sources");
            const std::size_t count = kind == Kind::Button ? 4 : 2;
            std::array<theme::BrushAssignment, 4> writes{};
            for (std::size_t i = 0; i < count; ++i) {
                if (kind == Kind::Button) writes[i].member.button = static_cast<theme::ButtonBrush>(i);
                else writes[i].member.index = static_cast<std::int32_t>(i);
                writes[i].image = &texture;
            }
            f.adapter_calls = f.adapter_attempts = 0; f.events.clear();
            f.adapter_drift = TargetDrift::Retire;
            if (failure == 0) f.adapter_before_fault_at = phase;
            if (failure == 1) f.adapter_fault_at = phase;
            if (failure == 2) f.adapter_before_drift_at = phase;
            if (failure == 3) f.adapter_drift_at = phase;
            theme::ApplyProgress progress{};
            require(theme::apply_members(subject, {writes.data(), count}, a, progress).outcome == theme::Outcome::NativeFault
                && theme::state.poisoned && !theme::state.available && f.adapter_attempts == phase
                && !progress.refresh_complete, "every native boundary stops on fault/retirement without next helper");
            const auto attempted = kind == Kind::Button ? (phase >= 7 ? count : 0) : std::min<std::size_t>(phase, count);
            const auto completed = kind == Kind::Button ? (phase >= 8 ? count : 0) : std::min<std::size_t>(phase - 1, count);
            require(progress.attempted_members == attempted && progress.completed_members == completed,
                "partial/uncertain native mutation progress is not falsely atomic");
            const auto calls = native_calls;
            require(!theme::apply_members(subject, {writes.data(), count}, a, progress)
                && native_calls == calls, "compromised batch cannot attempt stale restoration/cleanup");
        }
    }
    for (unsigned drift = 0; drift < 3; ++drift) {
        const auto kind = drift == 2 ? Kind::Button : Kind::ImageSet;
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(kind);
        auto a = authority(); theme::Subject subject; theme::OwnedImage texture;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", kind, {}, a, subject))
            && static_cast<bool>(theme::create(pixels, a, texture)), "batch preserved-field drift fixture");
        theme::BrushAssignment last{}; last.image = &texture;
        if (kind == Kind::Button) last.member.button = theme::ButtonBrush::Disabled;
        else last.member.index = 1;
        f.adapter_array_drift = drift == 0; f.adapter_geometry_drift = drift == 1;
        f.adapter_property_drift = drift == 2;
        theme::ApplyProgress progress{};
        require(theme::apply_members(subject, {&last, 1}, a, progress).outcome == theme::Outcome::NativeFault
            && theme::state.poisoned && !progress.refresh_complete
            && f.events.back() == (drift == 2 ? "button_style" : "synchronize"),
            "header/unselected geometry/untouched Button field drift stops before cleanup or refresh");
    }
}
void brush_tint_tests()
{
    const std::array<float, 4> rgba{0.1f, 0.2f, 0.3f, 0.4f};
    for (bool empty : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image);
        put(f.items.data(), TargetNode * 0x18 + 0x10, std::int32_t{0});
        if (empty) put(f.nodes[TargetNode].data(), 0x150, static_cast<void*>(nullptr));
        put(f.nodes[OtherTarget].data(), 0x10, static_cast<void*>(f.nodes[HolderClass].data()));
        put(f.nodes[OtherTarget].data(), 0x18, std::uint64_t{0xa0000000a});
        put(f.nodes[OtherTarget].data(), 0x20, static_cast<void*>(f.nodes[Tree].data()));
        std::memcpy(f.nodes[OtherTarget].data() + 0x108, f.nodes[TargetNode].data() + 0x108, 0x88); // fixture owning source
        const auto a = authority(); theme::Subject first, second;
        theme::OwnedImage original, other_original, tinted, other_tint;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::Image, {}, a, first))
            && static_cast<bool>(theme::find_subject(L"Bg_2", theme::SubjectKind::Image, {}, a, second))
            && static_cast<bool>(theme::capture_original(first, {}, a, original))
            && static_cast<bool>(theme::capture_original(second, {}, a, other_original)), "tint-only equal original subjects");
        const void* before = nullptr;
        require(static_cast<bool>(theme::borrow_brush(original, a, before)), "immutable source tint witness");
        std::array<std::uint8_t, 0x88> facts{}; std::memcpy(facts.data(), before, facts.size());
        require(static_cast<bool>(theme::tint(original, rgba, a, tinted)) && f.tint_releases == 1,
            "native assignment releases copied tint owner, not original owner");
        const auto count = theme::state.reserved_images; const auto calls = native_calls;
        require(static_cast<bool>(theme::tint(other_original, rgba, a, other_tint))
            && theme::state.reserved_images == count && native_calls == calls, "shared tinted storage, separate target affinity");
        require(std::memcmp(before, facts.data(), facts.size()) == 0, "original shared tint/controller untouched");
        require(theme::apply(second, {}, tinted, a).outcome == theme::Outcome::Identity, "tinted brush cannot cross affinity");
        require(static_cast<bool>(theme::apply(second, {}, other_tint, a))
            && static_cast<bool>(theme::apply(second, {}, other_original, a)), "null/asset tint-only apply and full restoration");
        require(std::memcmp(f.nodes[OtherTarget].data() + 0x108, facts.data(), facts.size()) == 0, "original tint and resource restored exactly");
        require(std::find(f.events.begin(), f.events.end(), "create_texture") == f.events.end()
            && std::find(f.events.begin(), f.events.end(), "update") == f.events.end(), "tint-only allocates no texture or upload");
        f.session.generation++;
        require(!theme::tint(original, rgba, a, other_tint), "stale tinted source rejected");
    }
    for (bool fault : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::Image);
        const auto a = authority(); theme::Subject subject; theme::OwnedImage original, tinted;
        require(static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::Image, {}, a, subject))
            && static_cast<bool>(theme::capture_original(subject, {}, a, original)), "tint phase baseline");
        const auto count = theme::state.reserved_images;
        if (fault) f.adapter_fault_at = 1;
        else { f.adapter_drift_at = 1; f.adapter_drift = TargetDrift::Retire; }
        require(theme::tint(original, rgba, a, tinted).outcome == theme::Outcome::NativeFault
            && theme::state.poisoned && theme::state.reserved_images == count + 1
            && !theme::state.retained[count].published && f.events.back() == "own_tint", "tint fault/retirement charged, unpublished, no cleanup");
        const auto calls = native_calls;
        require(!theme::apply(subject, {}, original, a) && native_calls == calls, "no stale tint rollback");
    }
}

void image_foundation_tests(const core::ThemeImage& pixels)
{
    using theme::Outcome;
    const auto lookup = [](bool other = false) {
        fixture->lookup_result = fixture->nodes[other ? OtherTarget : TargetNode].data();
        theme::ImageTarget target;
        require(theme::find_image(other ? L"Bg_2" : L"Bg_1", authority(), target).outcome == Outcome::Ready,
            "independent positive or strongly retained zero endpoint");
        return target;
    };
    const auto shape_matches = [](void* owner, const theme::BrushFacts& original, void* resource) {
        theme::BrushFacts actual{};
        require(theme::brush_facts(owner, actual), "composed brush facts");
        for (std::size_t i = 0; i < actual.size(); ++i) {
            // ONLY the fields intentionally changed by resource composition.
            if ((i >= 0x48 && i < 0x50) || i == 0x68 || (i >= 0x70 && i < 0x80)) continue;
            require(actual[i] == original[i], "native size/margins/tiling/draw mode preserved");
        }
        void* actual_resource = nullptr;
        void* cache = nullptr;
        void* owner_cache = nullptr;
        require(core::safe_read_field(owner, 0x150, actual_resource) && actual_resource == resource
            && actual[0x68] == 0 && core::safe_read_field(owner, 0x178, cache) && !cache
            && core::safe_read_field(owner, 0x180, owner_cache) && !owner_cache,
            "standalone texture uses invalid UV and cleared native cached owners");
    };
    for (const bool empty : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        put(f.items.data(), TargetNode * 0x18 + 0x10, std::int32_t{0});
        put(f.nodes[OtherTarget].data(), 0x10, static_cast<void*>(f.nodes[HolderClass].data()));
        put(f.nodes[OtherTarget].data(), 0x18, std::uint64_t{0xa0000000a});
        put(f.nodes[OtherTarget].data(), 0x20, static_cast<void*>(f.nodes[Tree].data()));
        if (empty) put(f.nodes[TargetNode].data(), 0x150, static_cast<void*>(nullptr));
        put(f.nodes[TargetNode].data() + 0x108, 0x68, std::uint8_t{1});
        fake_set_brush(f.nodes[OtherTarget].data(), f.nodes[TargetNode].data() + 0x108);
        auto a = lookup(); auto b = lookup(true);
        theme::BrushFacts original{};
        require(theme::brush_facts(f.nodes[TargetNode].data(), original), "original full native shape");
        theme::OwnedImage oa, ob, texture, composed_a, composed_b;
        const auto start = theme::state.reserved_images;
        require(theme::capture_original(a, authority(), oa).outcome == Outcome::Ready, "capture first zero original");
        const auto calls = native_calls;
        require(theme::capture_original(b, authority(), ob).outcome == Outcome::Ready
            && theme::state.reserved_images == start + 1 && native_calls == calls,
            "two distinct targets share exact immutable owned storage, not capture affinity");
        const void* pa = nullptr; const void* pb = nullptr;
        require(theme::borrow_brush(oa, authority(), pa).outcome == Outcome::Ready
            && theme::borrow_brush(ob, authority(), pb).outcome == Outcome::Ready && pa == pb,
            "actual rooted original storage shared");
        require(theme::apply(b, oa, authority()).outcome == Outcome::Identity && native_calls == calls,
            "shared storage NEVER permits restoring another target");
        require(theme::create(pixels, authority(), texture).outcome == Outcome::Ready, "prepare reusable texture");
        require(theme::compose(oa, texture, authority(), composed_a).outcome == Outcome::Ready,
            "prepare resource-only composition without reupload");
        const auto composed_count = theme::state.reserved_images;
        const auto composed_calls = native_calls;
        require(theme::compose(ob, texture, authority(), composed_b).outcome == Outcome::Ready
            && native_calls == composed_calls && theme::state.reserved_images == composed_count,
            "equal original/texture pair shares immutable composition but has new target affinity");
        theme::OwnedImage invalid, rejected;
        require(theme::compose(invalid, texture, authority(), rejected).outcome == Outcome::Input
            && theme::compose(texture, texture, authority(), rejected).outcome == Outcome::Input
            && theme::compose(oa, oa, authority(), rejected).outcome == Outcome::Input
            && theme::compose(composed_a, texture, authority(), rejected).outcome == Outcome::Input
            && theme::compose(oa, composed_a, authority(), rejected).outcome == Outcome::Input
            && native_calls == composed_calls && theme::state.reserved_images == composed_count,
            "composition requires distinct typed original and prepared inputs, no native calls/charges on rejection");
        require(theme::apply(b, composed_a, authority()).outcome == Outcome::Identity,
            "composed brush keeps exact original target affinity");
        theme::OwnedImage texture_b, composed_ba, composed_bb;
        require(theme::create(pixels, authority(), texture_b).outcome == Outcome::Ready
            && theme::compose(oa, texture_b, authority(), composed_ba).outcome == Outcome::Ready
            && theme::compose(ob, texture_b, authority(), composed_bb).outcome == Outcome::Ready,
            "second cached texture/composition prepares independently of switching");
        f.events.clear();
        require(theme::apply(a, composed_a, authority()).outcome == Outcome::Ready
            && theme::apply(b, composed_b, authority()).outcome == Outcome::Ready,
            "multiple static zero endpoints apply cached composition");
        shape_matches(f.nodes[TargetNode].data(), original, f.nodes[Texture].data());
        shape_matches(f.nodes[OtherTarget].data(), original, f.nodes[Texture].data());
        require(theme::apply(a, composed_ba, authority()).outcome == Outcome::Ready
            && theme::apply(b, composed_bb, authority()).outcome == Outcome::Ready, "switch both endpoints to B");
        shape_matches(f.nodes[TargetNode].data(), original, f.nodes[TextureB].data());
        shape_matches(f.nodes[OtherTarget].data(), original, f.nodes[TextureB].data());
        require(theme::apply(a, oa, authority()).outcome == Outcome::Ready
            && theme::apply(b, ob, authority()).outcome == Outcome::Ready, "restore both native originals");
        theme::BrushFacts restored{};
        require(theme::brush_facts(f.nodes[TargetNode].data(), restored) && restored == original,
            "restore exact full original including null resource/UV/cache owners");
        require(f.events == std::vector<std::string>(6, "assign_target"),
            "switch/restore never constructs brushes/holders, uploads or binds resources");
        ++f.session.generation;
        a = lookup();
        require(theme::apply(a, oa, authority()).outcome == Outcome::Identity,
            "dedup does not weaken stale-generation rejection");
        const auto reused = theme::state.reserved_images;
        for (std::size_t i = 0; i < 1030; ++i) {
            ++f.session.generation;
            a = lookup();
            theme::OwnedImage current, current_composed;
            require(theme::capture_original(a, authority(), current).outcome == Outcome::Ready
                && theme::compose(current, texture, authority(), current_composed).outcome == Outcome::Ready,
                "repeated equivalent capture/composition reuses process storage across generations");
        }
        require(theme::state.reserved_images == reused, "equivalent captures never exhaust ledger");
        theme::OwnedImage current, cached;
        const auto bytes = theme::state.reserved_bytes;
        theme::state.reserved_bytes = theme::kRetainedByteLimit;
        require(theme::capture_original(a, authority(), current).outcome == Outcome::Ready
            && theme::compose(current, texture, authority(), cached).outcome == Outcome::Ready,
            "equivalent owned cache hits work even at irreversible allocation budget");
        const auto before_miss = native_calls;
        f.nodes[TargetNode][0x110] ^= 1; // synthetic input shape, not shared-owner bytes
        theme::OwnedImage miss;
        require(theme::capture_original(a, authority(), miss).outcome == Outcome::Budget && native_calls == before_miss,
            "full-byte difference is a conservative cache miss, no budget bypass");
        theme::state.reserved_bytes = bytes;
    }
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        auto target = lookup();
        theme::OwnedImage texture, original, composed;
        require(theme::create(pixels, authority(), texture).outcome == Outcome::Ready, "prepare same-resource texture");
        fake_set_resource(f.nodes[TargetNode].data() + 0x108, f.nodes[Texture].data());
        put(f.nodes[TargetNode].data() + 0x108, 0x70, std::uintptr_t{0x1234});
        put(f.nodes[TargetNode].data() + 0x108, 0x78, std::uintptr_t{0x5678});
        require(theme::capture_original(target, authority(), original).outcome == Outcome::Ready, "capture preexisting same texture/cache");
        const auto count = f.resource_calls;
        require(theme::compose(original, texture, authority(), composed).outcome == Outcome::Ready
            && f.resource_calls == count + 2, "force different resource before rebinding SAME texture to reset native cache");
        require(theme::apply(target, composed, authority()).outcome == Outcome::Ready, "apply same-resource composition");
        void* cache = nullptr;
        require(core::safe_read_field(f.nodes[TargetNode].data(), 0x178, cache) && !cache,
            "setter early-return cannot preserve inherited atlas cache");
    }
    for (const bool original_cache : {false, true}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        auto target = lookup(); theme::OwnedImage original, texture, composed, rejected;
        require(theme::capture_original(target, authority(), original).outcome == Outcome::Ready
            && theme::create(pixels, authority(), texture).outcome == Outcome::Ready
            && theme::compose(original, texture, authority(), composed).outcome == Outcome::Ready,
            "cache ownership test setup");
        put(f.items.data(), (original_cache ? Holder : HolderC) * 0x18 + 8, std::int32_t{0});
        const auto count = theme::state.reserved_images;
        const auto calls = native_calls;
        const auto result = original_cache ? theme::capture_original(target, authority(), rejected)
            : theme::compose(original, texture, authority(), rejected);
        require(result.outcome == Outcome::NativeFault && theme::state.poisoned
            && calls == native_calls && count == theme::state.reserved_images,
            "exact brush/cache key equality never substitutes for immutable rooted graph ownership");
    }
    for (unsigned drift = 0; drift < 8; ++drift) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        put(f.items.data(), TargetNode * 0x18 + 0x10, std::int32_t{0});
        auto target = lookup(); theme::OwnedImage original;
        require(theme::capture_original(target, authority(), original).outcome == Outcome::Ready, "zero source retained");
        if (drift == 0) put(f.nodes[TargetNode].data(), 8, std::uint32_t{0x8000});
        if (drift == 1) f.root_slots[0] = nullptr;
        if (drift == 2) put(f.nodes[TargetNode].data(), 0x20, static_cast<void*>(f.nodes[OldTree].data()));
        if (drift == 3) put(f.items.data(), TargetNode * 0x18, static_cast<void*>(f.nodes[OtherTarget].data()));
        if (drift == 4) put(f.items.data(), TargetNode * 0x18 + 0x10, std::int32_t{99});
        if (drift == 5) put(f.items.data(), TargetNode * 0x18 + 8, std::int32_t{64});
        if (drift == 6) put(f.nodes[TargetNode].data(), 0x0c, static_cast<std::int32_t>(OtherTarget));
        if (drift == 7) put(f.nodes[TargetNode].data(), 0x18, std::uint64_t{0x710000007});
        const auto calls = native_calls;
        require(theme::apply(target, original, authority()).outcome == Outcome::Identity && calls == native_calls,
            "zero observations never authorize destroyed/recycled/detached/replaced/item-drift endpoints");
    }
    for (unsigned failure = 0; failure < 8; ++failure) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        put(f.items.data(), TargetNode * 0x18 + 0x10, std::int32_t{0});
        auto target = lookup(); theme::OwnedImage original, texture, composed;
        require(theme::capture_original(target, authority(), original).outcome == Outcome::Ready
            && theme::create(pixels, authority(), texture).outcome == Outcome::Ready, "composition failure inputs");
        if (failure == 0) f.construct_drift = TargetDrift::Retire;
        if (failure == 1) f.capture_drift = TargetDrift::Retire;
        if (failure == 2) f.construct_drift = TargetDrift::RecycleTarget;
        if (failure == 3 || failure == 4) {
            f.resource_drift = TargetDrift::Retire;
            f.drift_resource_at = f.resource_calls + (failure == 3 ? 1 : 2);
        }
        if (failure >= 5) {
            f.fault_resource_at = f.resource_calls + (failure == 5 ? 1 : 2);
            f.fault_resource_after_write = failure == 7;
        }
        const auto slots = theme::state.reserved_images;
        f.events.clear();
        require(theme::compose(original, texture, authority(), composed).outcome == Outcome::NativeFault
            && theme::state.poisoned && !theme::state.retained[slots].published
            && theme::state.reserved_images == slots + 1,
            "composition retirement/recycle/native cache-cleanup faults latch and retain charged unpublished storage");
        require(f.events.back() == (failure == 0 || failure == 2 ? "root_holder"
            : failure == 1 ? "bind_holder" : "set_resource")
            && std::count(f.events.begin(), f.events.end(), "brush_dtor") == 0
            && std::count(f.events.begin(), f.events.end(), "assign_target") == 0,
            "no unsafe follow-up, restore, or post-authority cleanup");
        const auto calls = native_calls;
        require(theme::apply(target, original, authority()).outcome == Outcome::Unavailable && calls == native_calls,
            "compromised composition never triggers automatic restoration");
    }
}

void target_api_tests(const core::ThemeImage& pixels)
{
    using theme::Outcome;
    const auto checked_target = [] {
        theme::ImageTarget target;
        require(theme::find_image(L"Bg_1", authority(), target).outcome == Outcome::Ready,
            "typed named EndImage lookup with positive target and zero-serial current tree");
        return target;
    };
    const auto latched = [] {
        require(theme::state.poisoned && !theme::state.available, "target operation latches compromise");
        const auto calls = native_calls;
        theme::ImageTarget rejected;
        require(theme::find_image(L"Bg_1", authority(), rejected).outcome == Outcome::Unavailable
            && calls == native_calls, "no native follow-up after compromised operation");
    };
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        auto target = checked_target();
        std::array<std::uint8_t, 0x88> original{};
        std::memcpy(original.data(), f.nodes[TargetNode].data() + 0x108, original.size());
        const auto slots = theme::state.reserved_images;
        const auto bytes = theme::state.reserved_bytes;
        theme::OwnedImage saved, a, b;
        f.nested_construct = true;
        require(theme::capture_original(target, authority(), saved).outcome == Outcome::Ready,
            "rooted native original capture, nested entry rejected without deadlock");
        f.nested_construct = false;
        const auto& retained = theme::state.retained[slots];
        require(retained.kind == theme::RecordKind::Original
            && retained.texture.pointer == f.nodes[AssetResource].data()
            && retained.texture.outer == f.nodes[AssetPackage].data()
            && theme::state.reserved_images == slots + 1
            && theme::state.reserved_bytes >= bytes + 0x88
            && std::count(f.events.begin(), f.events.end(), "texture") == 0,
            "PaperSprite/asset-Outer original uses same charged holder ledger, no throwaway texture");
        require(std::memcmp(original.data(), f.nodes[Holder].data() + 0x108, 0x88) == 0,
            "native original capture preserves full margin/UV/cache brush");
        require(theme::create(pixels, authority(), a).outcome == Outcome::Ready, "prepare image A");
        require(theme::create(pixels, authority(), b).outcome == Outcome::Ready, "prepare image B");
        f.events.clear();
        for (const auto* image : {&a, &b, &saved, &b, &a, &saved})
            require(theme::apply(target, *image, authority()).outcome == Outcome::Ready,
                "A/B/original native assignment reuse");
        require(std::memcmp(original.data(), f.nodes[TargetNode].data() + 0x108, 0x88) == 0,
            "restored exact original native full brush including cache/margin/UV");
        require(f.events == std::vector<std::string>(6, "assign_target"),
            "switches do only native owning assignment: no upload/constructor/destructor");
        put(f.nodes[OtherTarget].data(), 0x10, static_cast<void*>(f.nodes[HolderClass].data()));
        put(f.nodes[OtherTarget].data(), 0x18, std::uint64_t{0xa0000000a});
        put(f.nodes[OtherTarget].data(), 0x20, static_cast<void*>(f.nodes[Tree].data()));
        put(f.items.data(), OtherTarget * 0x18 + 0x10, std::int32_t{55});
        f.lookup_result = f.nodes[OtherTarget].data();
        theme::ImageTarget other;
        require(theme::find_image(L"Bg_2", authority(), other).outcome == Outcome::Ready,
            "second independently checked EndImage");
        const auto assignments = std::count(f.events.begin(), f.events.end(), "assign_target");
        require(theme::apply(other, saved, authority()).outcome == Outcome::Identity
            && std::count(f.events.begin(), f.events.end(), "assign_target") == assignments,
            "original cannot be assigned to a different otherwise-valid target");
        target_drift(TargetDrift::RecycleTarget);
        const auto stale_calls = native_calls;
        require(theme::apply(target, a, authority()).outcome == Outcome::Identity
            && native_calls == stale_calls, "recycled target rejected before assignment");
        ++f.session.generation;
        const auto calls = native_calls;
        require(theme::apply(target, saved, authority()).outcome == Outcome::Identity
            && calls == native_calls, "old generation cannot restore original");
    }
    for (unsigned bad = 0; bad < 8; ++bad) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        if (bad == 0) f.lookup_result = nullptr;
        if (bad == 1) put(f.nodes[TargetNode].data(), 0x10, static_cast<void*>(f.nodes[TextureClass].data()));
        if (bad == 2) put(f.nodes[TargetNode].data(), 0x18, std::uint64_t{0x710000007});
        if (bad == 3) put(f.nodes[TargetNode].data(), 0x20, static_cast<void*>(f.nodes[OldTree].data()));
        if (bad == 4) put(f.items.data(), TargetNode * 0x18 + 0x10, std::int32_t{-1});
        if (bad == 5) put(f.items.data(), Receiver * 0x18 + 0x10, std::int32_t{34});
        if (bad == 6) put(f.nodes[TreeClass].data(), 0x18, std::uint64_t{1234});
        if (bad == 7) put(f.nodes[Tree].data(), 0x28, static_cast<void*>(nullptr));
        theme::ImageTarget target;
        const auto slots = theme::state.reserved_images;
        require(!theme::find_image(L"Bg_1", authority(), target),
            "reject null/cache-hit wrong class/name/old-tree/negative serial/receiver/tree metadata");
        require(theme::state.reserved_images == slots
            && std::count(f.events.begin(), f.events.end(), "assign_target") == 0,
            "rejected lookup allocates no holder and never mutates a brush");
    }
    {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        theme::ImageTarget target;
        const auto calls = native_calls;
        for (auto leaf : {L"", L"MusicList/Bg_1", L"Bg.1", L"Bg 1"})
            require(theme::find_image(leaf, authority(), target).outcome == Outcome::Input,
                "bounded leaf only, no selector parser");
        require(native_calls == calls, "invalid leaf invokes nothing");
        require(!theme::find_image(std::wstring(128, L'A'), authority(), target), "leaf bound");
        require(theme::find_image(L"Missing", authority(), target).outcome == Outcome::Identity
            && f.events.back() == "find", "FNAME_Find miss never calls cache or returns success");
    }
    for (auto drift : {TargetDrift::Retire, TargetDrift::RecycleReceiver, TargetDrift::ReplaceTree,
        TargetDrift::Root, TargetDrift::Disable}) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); f.lookup_drift = drift;
        theme::ImageTarget target;
        require(theme::find_image(L"Bg_1", authority(), target).outcome == Outcome::NativeFault
            && f.events.back() == "lookup", "post-lookup ownership/admission drift stops immediately");
        latched();
    }
    for (auto drift : {TargetDrift::Retire, TargetDrift::RecycleTarget, TargetDrift::RecycleReceiver,
        TargetDrift::ReplaceTree, TargetDrift::Root, TargetDrift::Detach, TargetDrift::Name, TargetDrift::Disable,
        TargetDrift::Membership, TargetDrift::Ancestry}) {
        for (unsigned operation = 0; operation < 3; ++operation) {
            Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
            auto target = checked_target();
            theme::OwnedImage saved;
            if (operation == 0) f.construct_drift = drift;
            if (operation == 1) f.capture_drift = drift;
            if (operation == 2) {
                require(theme::capture_original(target, authority(), saved).outcome == Outcome::Ready,
                    "prepare original before apply drift");
                f.apply_drift = drift;
            }
            const auto slots = theme::state.reserved_images;
            f.events.clear();
            const auto result = operation == 2 ? theme::apply(target, saved, authority())
                : theme::capture_original(target, authority(), saved);
            require(result.outcome == Outcome::NativeFault, "post-native drift must not return Ready");
            require(f.events.back() == (operation == 0 ? "root_holder" : operation == 1 ? "bind_holder" : "assign_target")
                && std::count(f.events.begin(), f.events.end(), "brush_dtor") == 0
                && std::count(f.events.begin(), f.events.end(), "update") == 0,
                "retirement/recycling/detach prevents follow-up writes or native cleanup");
            if (operation != 2)
                require(theme::state.reserved_images == slots + 1 && !theme::state.retained[slots].published,
                    "failed original retention remains charged/unpublished");
            latched();
        }
    }
    for (unsigned fault = 0; fault < 8; ++fault) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        theme::ImageTarget target;
        theme::OwnedImage saved;
        if (fault < 3) {
            f.fault_lookup = fault == 0; f.fault_fname = fault == 1; f.fault_class = fault == 2;
            require(theme::find_image(L"Bg_1", authority(), target).outcome == Outcome::NativeFault,
                "lookup/name/static-class SEH fails closed");
        } else {
            target = checked_target();
            const bool applying = fault == 5 || fault == 7;
            if (applying)
                require(theme::capture_original(target, authority(), saved).outcome == Outcome::Ready,
                    "prepare original before apply SEH");
            f.fault_construct = fault == 3;
            f.fault_capture = fault == 4 || fault == 6;
            f.fault_apply = applying;
            f.fault_after_copy = fault >= 6;
            const auto slots = theme::state.reserved_images;
            f.events.clear();
            const auto result = applying ? theme::apply(target, saved, authority())
                : theme::capture_original(target, authority(), saved);
            require(result.outcome == Outcome::NativeFault, "construct/capture/apply SEH fails closed");
            require(f.events.back() == (applying ? "assign_target" : fault == 3 ? "root_holder" : "bind_holder")
                && std::count(f.events.begin(), f.events.end(), "brush_dtor") == 0,
                "SEH, including after owning copy, has no unsafe follow-up or cleanup");
            if (!applying) require(theme::state.reserved_images == slots + 1
                && !theme::state.retained[slots].published, "SEH reservation irreversible");
        }
        latched();
    }
    for (unsigned rejection = 0; rejection < 5; ++rejection) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions();
        auto target = checked_target();
        theme::OwnedImage saved;
        if (rejection == 0) put(f.nodes[TargetNode].data(), 0x150, static_cast<void*>(nullptr));
        const auto slots = theme::state.reserved_images;
        const auto bytes = theme::state.reserved_bytes;
        if (rejection == 1) theme::state.reserved_bytes = theme::kRetainedByteLimit;
        if (rejection == 2) theme::state.reserved_bytes = theme::kRetainedByteLimit - 0x87;
        if (rejection == 3) target_drift(TargetDrift::RecycleTarget);
        if (rejection == 4) target_drift(TargetDrift::ReplaceTree);
        const auto calls = native_calls;
        const auto result = theme::capture_original(target, authority(), saved);
        if (rejection == 0) require(result.outcome == Outcome::Ready
            && theme::state.reserved_images == slots + 1 && theme::state.retained[slots].published,
            "empty native brush retained without a throwaway texture");
        else {
            require(!result, "budget/stale target/replaced tree rejected");
            require(calls == native_calls, "budget/stale admission rejects before native work");
            theme::state.reserved_bytes = bytes;
        }
    }
}
void prepare_subjects_tests()
{
    for (unsigned scenario = 0; scenario != 7; ++scenario) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::TextBlock);
        require(!theme::state.generic_names.ready, "fresh configured generic witness state");
        f.qualified = scenario != 0;
        f.prepare_reentry = scenario == 1 || scenario == 3;
        f.fault_fname = scenario == 2;
        if (scenario == 3) f.fname_drift = TargetDrift::Retire;
        if (scenario == 4) f.fname_drift = TargetDrift::Disable;
        if (scenario == 5) f.fname_drift = TargetDrift::RecycleReceiver;
        if (scenario == 6) theme::state.generic.classes = false;
        const auto calls = native_calls;
        const auto slots = theme::state.reserved_images;
        const auto result = theme::prepare_subjects(authority());
        require(f.constructions == 0 && f.textures == 0 && theme::state.reserved_images == slots
            && std::all_of(f.events.begin(), f.events.end(), [](const auto& event) { return event == "find"; }),
            "preparation only finds existing names, never searches UI or allocates native objects");
        if (scenario == 1) {
            require(result && theme::state.generic_names.ready && f.prepare_reentry_observed,
                "outer preparation succeeds; nested preparation never publishes");
            const auto prepared_calls = native_calls;
            require(static_cast<bool>(theme::prepare_subjects(authority())) && native_calls == prepared_calls,
                "prepared witnesses reused without native calls");
            f.qualified = false;
            require(!theme::prepare_subjects(authority()) && native_calls == prepared_calls,
                "cached readiness never bypasses callback admission");
        } else {
            require(!result && !theme::state.generic_names.ready, "failed preparation publishes no partial witnesses");
            if (scenario == 0 || scenario == 6) require(native_calls == calls, "early rejection invokes no native names");
            else require(result.outcome == theme::Outcome::NativeFault && theme::state.poisoned
                && !theme::state.available && native_calls == calls + 1,
                "native name fault/retirement stops at first call and latches");
        }
    }
}
void prepare_hud_icons_tests()
{
    for (bool fail:{false,true}) {
        Fixture f; fixture=&f; f.enable_target(); install_fake_functions();
        enable_generic(theme::SubjectKind::TextBlock);
        f.qualified=true; f.fault_construct=fail;
        // Exercise one pending image with the existing 2x2 native upload double.
        theme::state.hud_icon_pixels[0]={std::vector<std::uint8_t>(16,127),2,2};
        theme::state.hud_icons_ready.fill(true); theme::state.hud_icons_ready[0]=false;
        const auto slots=theme::state.reserved_images;
        const auto result=theme::prepare_song_hud_subjects(authority());
        if (fail) {
            require(!result && !theme::state.hud_icons_ready[0] && theme::state.poisoned,
                "faulted icon creation never publishes readiness or retries unsafe native work");
        } else {
            require(result && theme::state.hud_icons_ready[0]
                && theme::state.reserved_images==slots+1 && f.textures==1,
                "real menu admission creates and publishes one pending prepared icon");
            const auto calls=native_calls;
            require(theme::prepare_song_hud_subjects(authority()) && native_calls==calls+2
                && f.textures==1,
                "prepared icon resources are immutable and reused without native allocations");
            f.qualified=false;
            const auto prepared_calls=native_calls;
            require(!theme::prepare_song_hud_subjects(authority()) && prepared_calls==native_calls,
                "cached icon readiness does not bypass current menu authority");
        }
    }
}
void speed_membership_tests()
{
    using Kind = theme::SubjectKind;
    for (unsigned change = 0; change != 10; ++change) {
        Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(Kind::ImageSet);
        auto a = authority(); theme::SpeedRowsSnapshot copied{};
        const auto initial_calls = native_calls;
        require(!theme::read_speed_rows(a, copied) && copied.count == 0 && native_calls == initial_calls,
            "read-only enumeration never initializes native name witnesses");
        require(static_cast<bool>(theme::prepare_subjects(a)), "speed-only names preparation without dummy UI lookup");
        put(f.nodes[Row].data(), 0x10, static_cast<void*>(f.nodes[UserClass].data()));
        put(f.items.data(), Row * 0x18 + 0x10, std::int32_t{77});
        put(f.nodes[Row].data(), 0x1d8, static_cast<void*>(f.nodes[RowTree].data()));
        put(f.nodes[RowTree].data(), 0x10, static_cast<void*>(f.nodes[TreeClass].data()));
        put(f.nodes[RowTree].data(), 0x20, static_cast<void*>(f.nodes[Row].data()));
        put(f.nodes[RowTree].data(), 0x28, static_cast<void*>(f.nodes[Root].data()));
        put(f.nodes[Root].data(), 0x20, static_cast<void*>(f.nodes[RowTree].data()));
        put(f.nodes[TargetNode].data(), 0x20, static_cast<void*>(f.nodes[RowTree].data()));
        put(f.nodes[SpeedList].data(), 0x10, static_cast<void*>(f.nodes[SpeedListClass].data()));
        put(f.items.data(), SpeedList * 0x18 + 0x10, std::int32_t{88});
        put(f.nodes[Receiver].data(), 0x410, static_cast<std::int32_t>(SpeedList));
        put(f.nodes[Receiver].data(), 0x414, std::int32_t{88});
        put(f.nodes[SpeedList].data(), 0x188, f.rows.data());
        put(f.nodes[SpeedList].data(), 0x190, std::int32_t{1});
        put(f.nodes[SpeedList].data(), 0x194, std::int32_t{1});
        const auto calls = native_calls;
        require(static_cast<bool>(theme::read_speed_rows(a, copied)) && copied.count == 1
            && copied.rows[0] == f.nodes[Row].data() && native_calls == calls, "bounded current speed members without native calls");
        theme::RowTicket ticket; theme::Subject subject;
        require(!theme::bind_speed_row(copied.rows[0], 0, a, ticket), "membership epoch must be explicit");
        require(static_cast<bool>(theme::bind_speed_row(copied.rows[0], 12, a, ticket))
            && static_cast<bool>(theme::find_subject(L"Bg_1", Kind::ImageSet, ticket, a, subject)), "uniform speed subject");
        theme::RowAssociation association{};
        require(theme::subject_row_association(subject, association) && association.kind == theme::RowKind::Speed
            && association.membership_epoch == 12 && association.logical_index == -1
            && association.setup_generation == 0 && association.session_generation == a.session.generation,
            "local membership epoch is not song index or native setup generation");
        put(f.nodes[TargetNode].data(), 0x13c, std::int32_t{2});
        require(theme::validate_subject(subject, f.session), "native speed SelectedIndex change preserves membership ticket");
        std::array<void*, 1> replacement{f.nodes[Row].data()};
        if (change == 0) {
            theme::RowTicket newer;
            require(static_cast<bool>(theme::bind_speed_row(f.nodes[Row].data(), 13, a, newer)), "fresh local membership epoch");
        }
        if (change == 1) put(f.nodes[SpeedList].data(), 0x188, replacement.data());
        if (change == 2) f.rows[0] = f.nodes[OtherTarget].data();
        if (change == 3) put(f.nodes[Row].data(), 0x1d8, static_cast<void*>(f.nodes[OldTree].data()));
        if (change == 4) put(f.nodes[RowTree].data(), 0x28, static_cast<void*>(f.nodes[OtherRoot].data()));
        if (change == 5) put(f.items.data(), Row * 0x18 + 0x10, std::int32_t{78});
        if (change == 6) put(f.items.data(), Receiver * 0x18 + 0x10, std::int32_t{34});
        if (change == 7) put(f.nodes[Receiver].data(), 0x414, std::int32_t{89});
        if (change == 8) {
            put(f.nodes[SpeedList].data(), 0x190, std::int32_t{0});
            put(f.nodes[SpeedList].data(), 0x198, f.rows.data()); // cache is NOT active membership
            put(f.nodes[SpeedList].data(), 0x1a0, std::int32_t{1});
        }
        if (change == 9) put(f.nodes[SpeedList].data(), 0x194, std::int32_t{257});
        const auto before_validation = native_calls;
        require(!theme::validate_subject(subject, f.session) && native_calls == before_validation,
            "stale speed epoch/list/array/row/tree/owner rejected read-only");
        if (change == 1) {
            require(static_cast<bool>(theme::read_speed_rows(a, copied)) && copied.count == 1,
                "fresh enumeration sees replacement array but does not renew old ticket");
            theme::RowTicket renewed;
            require(static_cast<bool>(theme::bind_speed_row(copied.rows[0], 13, a, renewed))
                && static_cast<bool>(theme::find_subject(L"Bg_1", Kind::ImageSet, renewed, a, subject)), "replacement membership reconciled explicitly");
        }
        if (change == 8) require(static_cast<bool>(theme::read_speed_rows(a, copied)) && copied.count == 0,
            "cached-only rows excluded from enumeration");
        f.qualified = false;
        require(!theme::read_speed_rows(a, copied) && copied.count == 0, "speed enumeration still requires callback admission");
    }
}
void growing_row_association_tests()
{
    Fixture f; fixture = &f; f.enable_target(); install_fake_functions(); enable_generic(theme::SubjectKind::ImageSet);
    constexpr auto song_count = theme::kSubjectTraversalLimit;
    f.items.resize((Nodes + song_count) * 0x18);
    f.chunk = reinterpret_cast<std::uintptr_t>(f.items.data());
    put(f.registry.data(), 0x20, static_cast<std::int32_t>(Nodes + song_count));
    put(f.registry.data(), 0x24, static_cast<std::int32_t>(Nodes + song_count));
    std::deque<std::array<std::uint8_t, 0x200>> song_rows(song_count);
    std::array<void*, song_count> songs{};
    for (std::size_t i = 0; i < song_count; ++i) {
        songs[i] = song_rows[i].data();
        const auto index = static_cast<std::int32_t>(Nodes + i);
        put(songs[i], 0xc, index);
        put(songs[i], 0x10, static_cast<void*>(f.nodes[UserClass].data()));
        put(songs[i], 0x18, std::uint64_t{1000} + i);
        put(songs[i], 0x20, f.package);
        put(f.items.data(), static_cast<std::size_t>(index) * 0x18, songs[i]);
        put(f.items.data(), static_cast<std::size_t>(index) * 0x18 + 0x10, static_cast<std::int32_t>(300 + i));
    }
    put(f.nodes[SongList].data(), 0x10, static_cast<void*>(f.nodes[SongListClass].data()));
    put(f.items.data(), SongList * 0x18 + 0x10, std::int32_t{88});
    put(f.nodes[Receiver].data(), 0x3c8, static_cast<std::int32_t>(SongList));
    put(f.nodes[Receiver].data(), 0x3cc, std::int32_t{88});
    put(f.nodes[SongList].data(), 0x8d8, songs.data());
    put(f.nodes[SongList].data(), 0x8e0, static_cast<std::int32_t>(song_count));
    put(f.nodes[SongList].data(), 0x8e4, static_cast<std::int32_t>(song_count));
    put(f.nodes[Row].data(), 0x10, static_cast<void*>(f.nodes[UserClass].data()));
    put(f.items.data(), Row * 0x18 + 0x10, std::int32_t{77});
    put(f.nodes[Row].data(), 0x1d8, static_cast<void*>(f.nodes[RowTree].data()));
    put(f.nodes[RowTree].data(), 0x10, static_cast<void*>(f.nodes[TreeClass].data()));
    put(f.nodes[RowTree].data(), 0x20, static_cast<void*>(f.nodes[Row].data()));
    put(f.nodes[RowTree].data(), 0x28, static_cast<void*>(f.nodes[Root].data()));
    put(f.nodes[Root].data(), 0x20, static_cast<void*>(f.nodes[RowTree].data()));
    put(f.nodes[TargetNode].data(), 0x20, static_cast<void*>(f.nodes[RowTree].data()));
    put(f.nodes[SpeedList].data(), 0x10, static_cast<void*>(f.nodes[SpeedListClass].data()));
    put(f.items.data(), SpeedList * 0x18 + 0x10, std::int32_t{89});
    put(f.nodes[Receiver].data(), 0x410, static_cast<std::int32_t>(SpeedList));
    put(f.nodes[Receiver].data(), 0x414, std::int32_t{89});
    put(f.nodes[SpeedList].data(), 0x188, f.rows.data());
    put(f.nodes[SpeedList].data(), 0x190, std::int32_t{1});
    put(f.nodes[SpeedList].data(), 0x194, std::int32_t{1});
    auto a = authority();
    theme::RowTicket speed;
    theme::Subject subject;
    const auto before_bytes = theme::state.reserved_bytes;
    const auto before_records = theme::state.reserved_images;
    require(static_cast<bool>(theme::bind_speed_row(f.rows[0], 12, a, speed))
        && static_cast<bool>(theme::find_subject(L"Bg_1", theme::SubjectKind::ImageSet, speed, a, subject)),
        "speed association before row storage growth");
    std::array<theme::RowTicket, song_count> tickets;
    for (std::size_t i = 0; i < song_count; ++i)
        require(static_cast<bool>(theme::bind_row(theme::RowKind::Song, songs[i], static_cast<std::int32_t>(2000 + i),
            1, a, tickets[i])), "independent strong lists may retain more associations than one traversal bound");
    require(theme::state.rows && theme::state.rows->size() >= song_count + 1
        && theme::validate_subject(subject, f.session), "257 associations retain earlier speed ticket/source identity");
    require(theme::state.reserved_bytes > before_bytes && theme::state.reserved_images == before_records,
        "actual row storage allocations charge bytes without phantom image records");
    const auto size = theme::state.rows->size();
    const auto bytes = theme::state.reserved_bytes;
    theme::RowTicket rebound;
    require(static_cast<bool>(theme::bind_speed_row(f.rows[0], 13, a, rebound))
        && !theme::validate_subject(subject, f.session) && theme::state.rows->size() == size
        && theme::state.reserved_bytes == bytes, "same-row rebind reuses storage and invalidates old epoch");
    // Refresh replaces a row within the same Ready generation; obsolete membership is reusable.
    put(f.nodes[OtherTarget].data(), 0x10, static_cast<void*>(f.nodes[UserClass].data()));
    put(f.items.data(), OtherTarget * 0x18 + 0x10, std::int32_t{91});
    songs[0] = f.nodes[OtherTarget].data();
    require(static_cast<bool>(theme::bind_row(theme::RowKind::Song, songs[0], 9000, 2, a, rebound))
        && theme::state.rows->size() == size && theme::state.reserved_bytes == bytes,
        "replaced active membership reclaims its association slot, not another reservation");
    for (unsigned reopen = 0; reopen < 5; ++reopen) {
        ++f.session.generation; a = authority();
        require(static_cast<bool>(theme::bind_row(theme::RowKind::Song, songs[1], 2001, reopen + 3, a, rebound))
            && theme::state.rows->size() == size && theme::state.reserved_bytes == bytes,
            "reopens reuse association storage without a count or byte leak");
    }
}
void growing_ledger_tests()
{
    using theme::Outcome;
    Fixture f; fixture = &f; install_fake_functions(); f.enable_target(); enable_generic(theme::SubjectKind::Image);
    constexpr std::size_t resources = 1100; // Regression past both former 256/1024 ceilings, NOT a policy cap.
    f.items.resize((Nodes + resources) * 0x18);
    f.chunk = reinterpret_cast<std::uintptr_t>(f.items.data());
    put(f.registry.data(), 0x20, static_cast<std::int32_t>(Nodes + resources));
    f.grow_holders = true;
    auto a = authority();
    theme::ImageTarget target;
    theme::OwnedImage original;
    require(static_cast<bool>(theme::find_image(L"Bg_1", a, target))
        && static_cast<bool>(theme::capture_original(target, a, original)), "grow fixture original");
    const auto first_slot = theme::state.reserved_images - 1;
    const auto* first_address = &theme::state.retained[first_slot];
    const auto before_bytes = theme::state.reserved_bytes;
    const auto before_count = theme::state.reserved_images;
    std::vector<theme::OwnedImage> images(resources);
    for (std::size_t i = 0; i < resources; ++i) {
        const std::array<float, 4> rgba{static_cast<float>(i) / 2048.0f, 0.25f, 0.5f, 1.0f};
        const auto result = theme::tint(original, rgba, a, images[i]);
        const auto diagnostic = "real native retention beyond former cap: index=" + std::to_string(i)
            + " stage=" + result.stage + " outcome=" + std::to_string(static_cast<unsigned>(result.outcome))
            + " records=" + std::to_string(theme::state.reserved_images)
            + " bytes=" + std::to_string(theme::state.reserved_bytes);
        require(static_cast<bool>(result), diagnostic.c_str());
    }
    require(theme::state.reserved_images == before_count + resources
        && &theme::state.retained[first_slot] == first_address
        && theme::state.reserved_bytes >= before_bytes + resources * (sizeof(theme::Record) + theme::kRootedCarrierBytes)
        && theme::state.reserved_bytes < theme::kRetainedByteLimit,
        "stable deque addresses and actual carrier/ledger byte accounting, no count quota");
    require(static_cast<bool>(theme::apply(target, images.front(), a))
        && static_cast<bool>(theme::apply(target, images.back(), a))
        && static_cast<bool>(theme::apply(target, original, a)), "early/late opaque handles survive growth");
    const auto count = theme::state.reserved_images;
    const auto bytes = theme::state.reserved_bytes;
    for (unsigned reopen = 0; reopen < 3; ++reopen) {
        ++f.session.generation; a = authority();
        theme::OwnedImage rebound, tint;
        require(static_cast<bool>(theme::find_image(L"Bg_1", a, target))
            && static_cast<bool>(theme::capture_original(target, a, rebound)), "reopen exact original reuse");
        const auto calls = native_calls;
        require(static_cast<bool>(theme::tint(rebound, {0.25f, 0.25f, 0.5f, 1.0f}, a, tint))
            && native_calls == calls && theme::state.reserved_images == count && theme::state.reserved_bytes == bytes,
            "reopen cache reuse consumes no reservation or native call");
    }
    const auto calls = native_calls;
    std::size_t slot = 0;
    require(theme::reserve_record(UINT64_MAX, slot).outcome == Outcome::Budget
        && slot == theme::kInvalidImageSlot && native_calls == calls
        && theme::state.reserved_bytes == bytes, "overflow payload is not an allocation or native attempt");
    theme::state.reserved_images = theme::kInvalidImageSlot;
    require(theme::reserve_record(1, slot).outcome == Outcome::Budget
        && slot == theme::kInvalidImageSlot && theme::state.reserved_bytes == bytes,
        "representational slot overflow cannot collide with the invalid handle sentinel");
    theme::state.reserved_images = count;
    theme::OwnedImage invalid;
    require(theme::apply(target, invalid, a).outcome == Outcome::Input && native_calls == calls,
        "default sentinel remains invalid beyond former record ceilings");
    std::uint64_t used = 0, pending = 0;
    bool overflow = false;
    try {
        theme::LedgerAllocator<theme::Record> allocator{&used, &pending};
        auto* p = allocator.allocate(std::numeric_limits<std::size_t>::max() / sizeof(theme::Record) + 1);
        allocator.deallocate(p, 1);
    } catch (const theme::LedgerBudgetExceeded&) { overflow = true; }
    require(overflow && !used, "allocator multiplication overflow rejected before allocation");
    theme::disable();
    require(theme::state.reserved_images == count && theme::state.reserved_bytes == bytes,
        "disable never resets retained records or bytes");
    require(theme::configure(GetModuleHandleW(nullptr), "unsupported").outcome == Outcome::UnsupportedImage
        && theme::state.reserved_images == count && theme::state.reserved_bytes == bytes,
        "reconfigure failure never resets retained resources");
}
void texture_policy_tests()
{
    // Independent synthetic engine world, not a production reconfiguration:
    // earlier scenario groups deliberately consumed the process ledger.
    theme::state.retained.storage.reset(); // distinct synthetic engine world, never a production reset
    theme::state.rows.reset();
    theme::state.reserved_images = 0;
    theme::state.reserved_bytes = 0;
    core::ThemeImage image; image.width = 2; image.height = 2; image.bgra.assign(16, 0x7f);
    for (int scenario = 0; scenario != 9; ++scenario) {
        Fixture f; fixture = &f; install_fake_functions(); f.qualified = scenario != 1;
        f.bad_mip = scenario == 2;
        if (scenario == 3) f.materialize_drift = Drift::Authority;
        if (scenario == 4) f.unlock_drift = Drift::Authority;
        if (scenario == 5) f.mark_drift = Drift::Authority;
        if (scenario == 6) f.mark_drift = Drift::Graph;
        f.fault_mark = scenario == 7;
        f.disable_during_mark = scenario == 8;
        const auto slot = theme::state.reserved_images;
        theme::OwnedImage image_handle;
        const auto result = theme::create(image, authority(), image_handle);
        const auto marks = std::count(f.events.begin(), f.events.end(), "mark_not_offline_processed");
        const auto updates = std::count(f.events.begin(), f.events.end(), "update");
        const auto destructors = std::count(f.events.begin(), f.events.end(), "brush_dtor");
        if (scenario == 0) {
            require(result && marks == 1 && updates == 1 && destructors == 1,
                "fresh upload marks policy exactly once before exactly one update");
        } else {
            const auto diagnostic = "failed admission/layout/mark never dispatches upload: scenario="
                + std::to_string(scenario) + " stage=" + result.stage
                + " outcome=" + std::to_string(static_cast<unsigned>(result.outcome))
                + " marks=" + std::to_string(marks) + " updates=" + std::to_string(updates);
            require(!result && updates == 0 && marks == (scenario >= 5 ? 1 : 0), diagnostic.c_str());
            if (scenario >= 3) {
                require(result.outcome == theme::Outcome::NativeFault && theme::state.poisoned
                    && !theme::state.available && !theme::state.retained[slot].published
                    && theme::state.reserved_images == slot + 1,
                    "compromised upload remains charged, unpublished and latched");
                require(destructors == (scenario == 7 ? 1 : 0),
                    "mark fault permits only still-admitted cleanup; drift permits none");
                const auto calls = native_calls;
                require(!theme::create(image, authority(), image_handle) && native_calls == calls,
                    "failed policy mark never retries or follows with native work");
            }
        }
    }
}
} // namespace

namespace { bool allow_hud_font_scale=true; }
namespace ff7r::piano::game {
bool song_hud_font_draw_scale_allowed(void*,float) noexcept { return allow_hud_font_scale; }
bool capture_current_uobject_identity_access(obj::UObjectIdentityAccess& out) noexcept
{
    out = fixture->access(); return true;
}
bool menu_session_matches(std::uint64_t generation, void* widget,
    const UObjectLiveHandle& identity, bool ready) noexcept
{
    return ready && fixture->ready && generation == fixture->session.generation
        && widget == fixture->session.widget
        && identity.internal_index == fixture->session.widget_identity.internal_index
        && identity.serial_number == fixture->session.widget_identity.serial_number;
}
const RvaSignatureSpec* find_rva_signature(std::string_view id)
{
    return id == "selftest_signature" ? &signature_spec : nullptr;
}
} // namespace ff7r::piano::game

namespace {
struct HudTextFixture {
    std::vector<int> events;
    int fault_at=0, revoke_after=0;
    bool current=true, text_owned=false, item_owned=false;
    void* text=nullptr;
    void* item=nullptr;
    float scale=1;
    bool sharp=false,info_owned=false,invalid_measure=false;
    float dpi=1;
    std::int32_t size=18;
    std::uint64_t typeface=0x12340000832a6ull;
    game::runtime_layouts::FLinearColor color{.2f,.3f,.4f,.25f};
};
HudTextFixture* hud=nullptr;
void hud_event(int stage)
{
    hud->events.push_back(stage);
    if (stage==hud->revoke_after) hud->current=false;
    if (stage==hud->fault_at) RaiseException(0xe0000001,0,0,nullptr);
}
void* __fastcall hud_from_string(void* destination,const theme::HudString* source)
{
    require(reinterpret_cast<std::uintptr_t>(destination)%16==0,"FText storage alignment");
    require(source && source->num==5 && source->max==5
        && std::wstring_view(source->data)==L"Test","borrowed bounded FString includes NUL");
    require(!hud->text_owned && !hud->item_owned,"temporary FText constructs first");
    hud->text=destination; hud->text_owned=true;
    hud_event(1); return destination;
}
void* __fastcall hud_construct(void* destination,const theme::HudPosition* position,
    const void* text,void* font,const game::runtime_layouts::FLinearColor* color)
{
    require(reinterpret_cast<std::uintptr_t>(destination)%16==0 && destination!=text,"separate aligned item storage");
    require(hud->text_owned && text==hud->text && !hud->item_owned,"item retains initialized FText");
    require(position->x==12 && position->y==34 && font==reinterpret_cast<void*>(2),"native constructor arguments");
    require(color->r==hud->color.r && color->g==hud->color.g && color->b==hud->color.b
        && color->a==hud->color.a,"actual foreground preserves opacity");
    std::memcpy(static_cast<std::uint8_t*>(destination)+0x28,color,sizeof(*color));
    hud->item=destination; hud->item_owned=true;
    hud_event(2); return destination;
}
void __fastcall hud_destroy_text(void* text)
{
    require(text==hud->text && hud->text_owned && hud->item_owned,"temporary text destroyed only after item retains it");
    hud_event(3); hud->text_owned=false; hud->text=nullptr;
}
void __fastcall hud_submit(void* canvas,void* item)
{
    require(canvas==reinterpret_cast<void*>(1) && item==hud->item && hud->item_owned && !hud->text_owned,
        "submission owns item, not temporary text");
    float x=0,y=0; game::runtime_layouts::FLinearColor color{};
    require(core::safe_read_field(item,0xa0,x) && core::safe_read_field(item,0xa4,y)
        && x==hud->scale && y==hud->scale,"uniform local scale at native offsets");
    require(core::safe_read_field(item,0x28,color) && color.a==hud->color.a,"foreground alpha survives scale write");
    if (hud->sharp) {
        std::uint8_t initialized=0;
        require(core::safe_read_field(item,0x1a8,initialized) && initialized==1 && hud->info_owned
            && x==1 && y==1,"size-keyed draw owns FontInfo and never magnifies glyph geometry");
    }
    hud_event(4);
}
void __fastcall hud_destroy_item(void* item)
{
    require(item==hud->item && hud->item_owned && !hud->text_owned,"item destruction follows submit");
    hud_event(5); hud->item_owned=false; hud->item=nullptr;
    if (hud->sharp) {
        std::uint8_t initialized=0;
        require(core::safe_read_field(item,0x1a8,initialized) && initialized==1 && hud->info_owned,
            "native item destruction owns optional FontInfo release");
        put(item,0x1a8,std::uint8_t{0}); hud->info_owned=false;
    }
}
void* __fastcall hud_info_construct(void* info,void* font,std::int32_t size,
    const std::uint64_t* face,const void* outline)
{
    std::uint8_t initialized=1;
    require(info==static_cast<std::uint8_t*>(hud->item)+0x150
        && reinterpret_cast<std::uintptr_t>(info)%8==0 && hud->item_owned && !hud->text_owned
        && core::safe_read_field(hud->item,0x1a8,initialized) && initialized==0,
        "construct FontInfo in uninitialized owning item payload after temporary FText release");
    require(font==reinterpret_cast<void*>(2) && size==hud->size && *face==hud->typeface,
        "retain genuine full typeface and requested integer size, not legacy size");
    std::array<std::uint8_t,16> flags{};
    game::runtime_layouts::FLinearColor color{};
    require(core::safe_copy_bytes(outline,flags.data(),flags.size()) && flags==std::array<std::uint8_t,16>{}
        && core::safe_copy_bytes(static_cast<const std::uint8_t*>(outline)+16,&color,sizeof(color))
        && color.r==0 && color.g==0 && color.b==0 && color.a==1,
        "native default outline flags and black alpha-one color");
    hud->info_owned=true; hud_event(6); return info;
}
theme::HudPosition* __fastcall hud_measure_item(void* item,theme::HudPosition* out,float dpi)
{
    std::uint8_t initialized=0; float x=0,y=0;
    require(item==hud->item && hud->info_owned && core::safe_read_field(item,0x1a8,initialized) && initialized==1
        && core::safe_read_field(item,0xa0,x) && core::safe_read_field(item,0xa4,y) && x==1 && y==1,
        "matching measurement uses initialized size-keyed FontInfo at unit local geometry scale");
    require(dpi==hud->dpi,"measurement receives actual Canvas DPI as float");
    *out={hud->size*dpi*2,hud->invalid_measure ? NAN : hud->size*dpi};
    hud_event(7); return out;
}
float __fastcall hud_canvas_dpi(void* canvas)
{
    require(canvas==reinterpret_cast<void*>(1) && !hud->item_owned && !hud->text_owned,
        "DPI is read under authority before owning native construction");
    hud_event(8); return hud->dpi;
}
theme::HudTextWork hud_work(HudTextFixture& fixture)
{
    return {{hud_from_string,hud_construct,hud_destroy_text,hud_destroy_item,hud_submit},&fixture,
        [](const void* context) noexcept { return static_cast<const HudTextFixture*>(context)->current; },
        reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),L"Test",{12,34},fixture.color,fixture.scale};
}
void test_hud_text()
{
    for (const float scale:{.75f,1.0f,2.5f}) {
        HudTextFixture fixture; hud=&fixture; fixture.scale=scale;
        require(static_cast<bool>(theme::hud_scaled_text(hud_work(fixture))),"scaled native draw completes");
        require(fixture.events==std::vector<int>({1,2,3,4,5}) && !fixture.text_owned && !fixture.item_owned,
            "native ownership/order balances without pointer escape");
    }
    for (int stage=1;stage<=5;++stage) {
        HudTextFixture fixture; hud=&fixture; fixture.fault_at=stage;
        require(theme::hud_scaled_text(hud_work(fixture)).outcome==theme::Outcome::NativeFault,
            "native constructor/submit/destructor faults report uncertain ownership");
        require(fixture.events.size()==static_cast<std::size_t>(stage),"fault never triggers unsafe cleanup or retry");
    }
    for (int stage=1;stage<=5;++stage) {
        HudTextFixture fixture; hud=&fixture; fixture.revoke_after=stage;
        const auto result=theme::hud_scaled_text(hud_work(fixture));
        require(result.outcome==(stage<5 ? theme::Outcome::NativeFault : theme::Outcome::Authority),
            "authority loss poisons only while native owning state remains");
        require(fixture.events.size()==static_cast<std::size_t>(stage),"revocation suppresses further native operations");
    }
    HudTextFixture fixture; hud=&fixture;
    allow_hud_font_scale=false;
    require(theme::hud_scaled_text(hud_work(fixture)).outcome==theme::Outcome::Authority
        && fixture.events.empty(),"unapproved font scale constructs no native owner");
    allow_hud_font_scale=true;
    auto work=hud_work(fixture); work.functions={};
    require(theme::hud_scaled_text(work).outcome==theme::Outcome::Unavailable && fixture.events.empty(),
        "absent older-build capability invokes no native code");
    for (int missing=0;missing<5;++missing) {
        work=hud_work(fixture);
        switch (missing) {
        case 0: work.functions.from_string=nullptr; break;
        case 1: work.functions.construct=nullptr; break;
        case 2: work.functions.destroy_text=nullptr; break;
        case 3: work.functions.submit=nullptr; break;
        case 4: work.functions.destroy_item=nullptr; break;
        }
        require(theme::hud_scaled_text(work).outcome==theme::Outcome::Unavailable && fixture.events.empty(),
            "partial native ownership table cannot construct anything");
    }
    fixture.current=false;
    require(theme::hud_scaled_text(hud_work(fixture)).outcome==theme::Outcome::Authority && fixture.events.empty(),
        "immediate stale authority invokes nothing");
    fixture.current=true;
    require(static_cast<bool>(theme::hud_scaled_text(hud_work(fixture))),"delayed current admission needs no wait");
    fixture.events.clear(); work=hud_work(fixture); work.scale=std::numeric_limits<float>::quiet_NaN();
    require(theme::hud_scaled_text(work).outcome==theme::Outcome::Input && fixture.events.empty(),"nonfinite scale rejected");
    work=hud_work(fixture); work.value=L"";
    require(static_cast<bool>(theme::hud_scaled_text(work)) && fixture.events.empty(),"empty text needs no owning construction");
    hud=nullptr;
}
void test_hud_sharp_text()
{
    for (float dpi:{1.0f,1.5f,2.0f}) {
        HudTextFixture fixture; hud=&fixture; fixture.sharp=true; fixture.dpi=dpi;
        theme::HudSharpText sharp{{hud_info_construct,hud_measure_item,hud_canvas_dpi},fixture.typeface,fixture.size,dpi};
        auto work=hud_work(fixture); work.sharp=&sharp;
        theme::HudPosition measured{}; work.measurement=&measured;
        require(static_cast<bool>(theme::hud_scaled_text(work)),"size-keyed native measurement completes");
        require(fixture.events==std::vector<int>({8,1,2,3,6,7,5}) && !fixture.info_owned
            && !fixture.item_owned && !fixture.text_owned && measured.x==36*dpi && measured.y==18*dpi,
            "copy actual DPI-aware pixels only after native item/FontInfo owners are destroyed");
        fixture.events.clear(); work.measurement=nullptr;
        require(static_cast<bool>(theme::hud_scaled_text(work))
            && fixture.events==std::vector<int>({8,1,2,3,6,4,5}) && !fixture.info_owned,
            "draw uses identical size/typeface and unit scale then releases local optional owner");
    }
    const std::vector<int> order{8,1,2,3,6,7,5};
    for (const int stage:order) {
        for (bool revoke:{false,true}) {
            HudTextFixture fixture; hud=&fixture; fixture.sharp=true;
            if (revoke) fixture.revoke_after=stage; else fixture.fault_at=stage;
            theme::HudSharpText sharp{{hud_info_construct,hud_measure_item,hud_canvas_dpi},fixture.typeface,fixture.size,1};
            auto work=hud_work(fixture); work.sharp=&sharp;
            theme::HudPosition measured{-1,-1}; work.measurement=&measured;
            const auto result=theme::hud_scaled_text(work);
            const auto expected=stage==8 ? (revoke ? theme::Outcome::Authority : theme::Outcome::Unavailable)
                : revoke && stage==5 ? theme::Outcome::Authority : theme::Outcome::NativeFault;
            require(result.outcome==expected && measured.x==-1 && measured.y==-1,
                "fault/revocation never publishes measurement or cleans uncertain ownership; read-only DPI cannot poison");
            const auto end=std::find(order.begin(),order.end(),stage)+1;
            require(fixture.events==std::vector<int>(order.begin(),end),"no native operation after first failure");
        }
    }
    HudTextFixture fixture; hud=&fixture; fixture.sharp=true;
    theme::HudSharpText sharp{{hud_info_construct,hud_measure_item,hud_canvas_dpi},fixture.typeface,fixture.size,1};
    auto work=hud_work(fixture); work.sharp=&sharp;
    for (float scale:{.75f,1.01f}) {
        work.scale=scale;
        require(theme::hud_scaled_text(work).outcome==theme::Outcome::Input && fixture.events.empty(),
            "no fractional geometry scaling after intended-size raster generation");
    }
    work.scale=1; sharp.dpi=2;
    require(theme::hud_scaled_text(work).outcome==theme::Outcome::Authority
        && fixture.events==std::vector<int>({8}),"changed Canvas DPI denies before native owners");
    fixture.events.clear(); sharp.dpi=1;
    for (int missing=0;missing<3;++missing) {
        auto partial=sharp;
        if (missing==0) partial.functions.construct=nullptr;
        if (missing==1) partial.functions.measure=nullptr;
        if (missing==2) partial.functions.dpi=nullptr;
        work.sharp=&partial;
        require(theme::hud_scaled_text(work).outcome==theme::Outcome::Unavailable && fixture.events.empty(),
            "incomplete/older capability performs no Runtime invocation");
    }
    work.sharp=&sharp; fixture.invalid_measure=true;
    theme::HudPosition measured{-1,-1}; work.measurement=&measured;
    require(theme::hud_scaled_text(work).outcome==theme::Outcome::Unavailable && measured.x==-1
        && !fixture.info_owned && !fixture.item_owned,"invalid copied metrics release healthy owners without poisoning");
    for (const float dpi:{1.0f,1.5f,2.0f}) {
        sharp.dpi=dpi; float height=0; int calls=0;
        const auto fit=theme::hud_fit_font_size(25,sharp,[&](theme::HudPosition& out) {
            ++calls; out={1,sharp.size*dpi*1.25f}; return theme::Result{theme::Outcome::Ready,"fixture_measure"};
        },height);
        require(static_cast<bool>(fit) && calls<=8 && height<=25 && (sharp.size+1)*dpi*1.25f>25,
            "bounded integer native raster size fits actual pixel height at native DPI without enlarging geometry");
    }
    sharp.dpi=1; float height=0;
    require(theme::hud_fit_font_size(.25f,sharp,[](theme::HudPosition& out) {
        out={1,1}; return theme::Result{theme::Outcome::Ready,"fixture_measure"};
    },height).outcome==theme::Outcome::Unavailable,"unrepresentable tiny line omits rather than geometrically rescaling");
    hud=nullptr;
}
} // namespace

int main()
{
    test_hud_text();
    test_hud_sharp_text();
    using theme::Outcome;
    require(theme::detail::preflight(0, 2, 0, 0).outcome == Outcome::Input, "zero dimension");
    require(theme::detail::preflight(4097, 1, 16388, 0).outcome == Outcome::Input, "dimension bound");
    require(theme::detail::preflight(2, 2, 15, 0).outcome == Outcome::Input, "byte size");
    require(theme::detail::preflight(2, 2, 16, theme::kRetainedByteLimit - 15).outcome == Outcome::Budget, "byte budget");
    require(static_cast<bool>(theme::detail::preflight(2, 2, 16, theme::kRetainedByteLimit - 16)), "exact payload budget before metadata");
    require(theme::detail::preflight(2, 2, 16, UINT64_MAX).outcome == Outcome::Budget, "byte overflow");
    Fixture first; fixture = &first;
    require(!theme::configure(GetModuleHandleW(nullptr), "not_the_profile"), "unsupported profile");
    require(!theme::state.available && !native_calls, "no native calls during configure");
    const auto module = GetModuleHandleW(nullptr);
    const auto address = reinterpret_cast<std::uintptr_t>(&fake_class);
    const auto rva = address - reinterpret_cast<std::uintptr_t>(module);
    signature_spec = {"selftest_signature", rva, std::vector<std::uint8_t>(8)};
    require(core::safe_copy_bytes(reinterpret_cast<void*>(address), signature_spec.expected_prologue.data(), 8), "signature fixture");
    require(theme::signature(module, "selftest_signature", rva), "matching executable signature");
    signature_spec.expected_prologue[0] ^= 0xff;
    require(!theme::signature(module, "selftest_signature", rva), "signature mismatch");
    require(!theme::signature(module, "missing_spec", rva), "missing spec");
    require(native_calls == 0, "signature validation invokes nothing");
    install_fake_functions();
    core::ThemeImage image; image.width = 2; image.height = 2; image.bgra.assign(16, 0x7f);
    theme::OwnedImage owned;
    auto a = authority();
    require(theme::create(image, a, owned).outcome == Outcome::Authority && !native_calls, "unqualified early callback");
    first.qualified = true; a.game_thread_id ^= 0x80000000;
    require(theme::create(image, a, owned).outcome == Outcome::Authority && !native_calls, "wrong thread");
    a = authority(); ++a.session.generation;
    require(theme::create(image, a, owned).outcome == Outcome::Authority && !native_calls, "stale session");
    a = authority(); a.session.phase = game::MenuSessionPhase::Ending;
    require(theme::create(image, a, owned).outcome == Outcome::Authority && !native_calls, "ending session");
    a = authority();
    const auto reservations = theme::state.reserved_bytes;
    theme::state.reserved_bytes = theme::kRetainedByteLimit;
    require(theme::create(image, a, owned).outcome == Outcome::Budget && !native_calls,
        "actual process budget enforced before native calls");
    theme::state.reserved_bytes = theme::kRetainedByteLimit - image.bgra.size() - theme::kRootedCarrierBytes;
    require(theme::create(image, a, owned).outcome == Outcome::Budget && !native_calls
        && theme::state.reserved_images == 0
        && (!theme::state.retained.storage || theme::state.retained.storage->empty()),
        "payload-only headroom cannot bypass actual C++ map/block memory admission");
    theme::state.reserved_bytes = reservations;
    theme::fail_ledger_allocation = true;
    require(theme::create(image, a, owned).outcome == Outcome::Memory && !native_calls
        && theme::state.reserved_images == 0 && theme::state.reserved_bytes == reservations,
        "C++ allocation failure is reported before native mutation, without imaginary payload charge");
    namespace perf = game::menu_theme_metrics;
    perf::enable(true);
    require(static_cast<bool>(theme::create(image, a, owned)), "immediate qualified create");
    perf::enable(false);
    {
        auto& record=theme::state.retained[theme::state.reserved_images-1];
        const void* resource=reinterpret_cast<const void*>(1);
        const auto access=first.access();
        const auto calls=native_calls;
        require(!theme::hud_resource(record,access,resource) && !resource,
            "upload dispatch with absent Canvas resource omits artwork without waiting");
        const void* expected=first.pixels.data(); // Deliberately not a UObject.
        put(record.texture.pointer,0xa8,expected);
        require(theme::hud_resource(record,access,resource) && resource==expected,
            "delayed publication projects the exact resource, without a UObject cast");
        require(theme::hud_resource(record,access,resource) && resource==expected,
            "immediate retained resource is usable on the creation game thread");
        record.kind=theme::RecordKind::Original;
        require(!theme::hud_resource(record,access,resource) && !resource,"captured originals cannot enter Canvas projection");
        record.kind=theme::RecordKind::Prepared;
        record.creation_thread^=0x80000000;
        require(!theme::hud_resource(record,access,resource),"different thread cannot order initialization before Canvas consumption");
        record.creation_thread^=0x80000000;
        put(first.platform.data(),0x20,std::int32_t{2});
        require(!theme::hud_resource(record,access,resource),"streamable multi-mip graph is ineligible");
        put(first.platform.data(),0x20,std::int32_t{1});
        put(record.texture.pointer,0xa8,static_cast<const void*>(nullptr));
        require(!theme::hud_resource(record,access,resource) && !resource,"a prior draw never supplies a saved resource pointer");
        put(record.texture.pointer,0xa8,expected);
        put(record.holder.pointer,0x150,static_cast<void*>(nullptr));
        require(!theme::hud_resource(record,access,resource) && !resource,"lost owning brush graph omits resource");
        put(record.holder.pointer,0x150,record.texture.pointer);
        put(record.texture.pointer,0xa8,static_cast<const void*>(nullptr));
        require(native_calls==calls,"resource projection invokes no native helper or extra upload");
        require(theme::draw_song_hud(&owned,nullptr,{},0,1920,1080,{}, {},nullptr,nullptr,{}, {},{}).outcome
                ==theme::Outcome::Authority,"saved/missing PostRender scope is not Ready-menu authority");
    }
    perf::Snapshot metrics{};
    require(perf::take_snapshot(metrics)
        && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeConstruct)].count == 1
        && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeTextureUpload)].count == 1
        && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeBrushCopy)].count == 1
        && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeBrushDestroy)].count == 1,
        "cold create measures actual construction/upload/copy/cleanup calls once");
    require(first.pixels == std::array<std::uint8_t, 16>{0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f}, "copied BGRA");
    require(first.events.back() == "brush_dtor", "local native brush cleanup");
    const void* brush = nullptr;
    require(static_cast<bool>(theme::borrow_brush(owned, a, brush)) && brush == first.nodes[Holder].data() + 0x108, "qualified graph borrow");
    first.ready = false;
    require(theme::borrow_brush(owned, a, brush).outcome == Outcome::Authority && !brush, "ending invalidates borrow");
    first.ready = true;
    put(first.items.data(), Holder * 0x18 + 8, std::int32_t{0});
    require(theme::borrow_brush(owned, a, brush).outcome == Outcome::Identity && !brush, "zero serial alone NOT ownership");
    Fixture delayed; fixture = &delayed; install_fake_functions(); a = authority();
    const auto before = native_calls;
    require(!theme::create(image, a, owned) && native_calls == before, "delayed callback not admitted");
    delayed.qualified = true; // transition, not a timer or sleep
    require(static_cast<bool>(theme::create(image, a, owned)), "delayed qualified transition");
    for (const auto capacity : {4, 8, 16}) {
        Fixture grown; fixture = &grown; grown.qualified = true; grown.mip_capacity = capacity;
        install_fake_functions();
        const auto result = theme::create(image, authority(), owned);
        require(result && result.mip_header_read && result.mip_count == 1
            && result.mip_capacity == capacity, "native first-growth/quantized capacity, not mip count");
        require(std::count(grown.events.begin(), grown.events.end(), "update") == 1,
            "quantized capacity uploads exactly the sole mip");
    }
    for (const auto capacity : {-1, 0, 1, 2, 3}) {
        Fixture invalid; fixture = &invalid; invalid.qualified = true; invalid.mip_capacity = capacity;
        install_fake_functions();
        const auto result = theme::create(image, authority(), owned);
        require(result.outcome == Outcome::FreshLayout && result.mip_header_read
            && result.mip_count == 1 && result.mip_capacity == capacity
            && std::string_view(result.stage) == "fresh_mip_capacity",
            "invalid first-growth capacity retains first-failure copied header");
        require(std::count(invalid.events.begin(), invalid.events.end(), "materialize") == 0
            && invalid.pixels == std::array<std::uint8_t, 16>{}, "bad capacity rejects before bulk/pixel work");
    }
    for (const auto count : {0, 2}) {
        Fixture invalid; fixture = &invalid; invalid.qualified = true; invalid.mip_count = count;
        install_fake_functions();
        const auto result = theme::create(image, authority(), owned);
        require(result.outcome == Outcome::FreshLayout && result.mip_header_read
            && result.mip_count == count && std::string_view(result.stage) == "fresh_mip_count",
            "capacity slack does not permit multiple or absent mips");
        require(std::count(invalid.events.begin(), invalid.events.end(), "materialize") == 0,
            "invalid count never materializes");
    }
    Fixture malformed; fixture = &malformed; malformed.qualified = true; malformed.bad_mip = true;
    install_fake_functions();
    const auto reserved = theme::state.reserved_images;
    const auto failed_bytes = theme::state.reserved_bytes;
    const auto bad_flags = theme::create(image, authority(), owned);
    require(bad_flags.outcome == Outcome::FreshLayout && std::string_view(bad_flags.stage) == "fresh_mip_flags"
        && bad_flags.mip_header_read && bad_flags.mip_capacity == 4, "reject async/unexpected fresh flags with copied header");
    require(theme::state.reserved_images == reserved + 1 && !theme::state.retained[reserved].published
        && theme::state.reserved_bytes >= failed_bytes + image.bgra.size() + theme::kRootedCarrierBytes,
        "failed native attempt permanently charges its carrier/pixels plus actual ledger growth");
    require(malformed.events.back() == "brush_dtor", "failure brush cleanup");
    require(std::find(malformed.events.begin(), malformed.events.end(), "materialize") == malformed.events.end(), "no materialize on bad flags");
    Fixture drift; fixture = &drift; drift.qualified = true; drift.drift_after_materialize = true;
    install_fake_functions();
    require(theme::create(image, authority(), owned).outcome == Outcome::FreshLayout, "reread after materialize");
    require(std::find(drift.events.begin(), drift.events.end(), "update") == drift.events.end(), "no upload on drift");
    Fixture capacity_changed; fixture = &capacity_changed; capacity_changed.qualified = true;
    capacity_changed.capacity_drift = true; install_fake_functions();
    const auto changed = theme::create(image, authority(), owned);
    require(changed.outcome == Outcome::FreshLayout && changed.mip_capacity == 8
        && std::string_view(changed.stage) == "materialize_mip_identity"
        && std::count(capacity_changed.events.begin(), capacity_changed.events.end(), "update") == 0,
        "capacity must remain unchanged across materialization");
    Fixture unrooted; fixture = &unrooted; unrooted.qualified = true; unrooted.lose_root = true;
    install_fake_functions();
    require(theme::create(image, authority(), owned).outcome == Outcome::NativeFault, "root-before-publication predicate");
    require(std::find(unrooted.events.begin(), unrooted.events.end(), "update") == unrooted.events.end(), "no upload without holder root");
    require(unrooted.events.back() == "texture", "compromised holder prevents binding and native cleanup");
    const auto require_latched_slot = [&](const theme::Result& result, std::size_t slot) {
        require(result.outcome == Outcome::NativeFault && theme::state.poisoned
            && !theme::state.available, "drift latches unavailable, not Ready");
        require(theme::state.reserved_images == slot + 1 && !theme::state.retained[slot].published,
            "compromised allocation remains charged and unpublished");
        const auto calls = native_calls;
        require(theme::create(image, authority(), owned).outcome == Outcome::Unavailable
            && native_calls == calls, "latch prevents further native work");
    };
    for (const auto kind : {TargetDrift::Retire, TargetDrift::Disable}) {
        for (const auto boundary : {0, 1, 2}) {
            Fixture retired; fixture = &retired; retired.qualified = true;
            install_fake_functions();
            if (boundary == 0) retired.texture_drift = kind;
            else if (boundary == 1) retired.resource_drift = kind;
            else retired.capture_drift = kind;
            const auto slot = theme::state.reserved_images;
            const auto result = theme::create(image, authority(), owned);
            require_latched_slot(result, slot);
            const char* expected_event = boundary == 0 ? "texture"
                : boundary == 1 ? "set_resource" : "bind_holder";
            require(std::string_view(result.stage) == (boundary == 0 ? "post_texture_admission"
                    : boundary == 1 ? "post_resource_admission" : "cleanup_admission")
                && retired.events.back() == expected_event,
                "retired construction/binding has no unauthorized cleanup or further native call");
            require(std::count(retired.events.begin(), retired.events.end(), "materialize") == 0
                && std::count(retired.events.begin(), retired.events.end(), "brush_dtor") == 0,
                "no materialization/destructor after construction or binding retirement");
        }
    }
    for (const auto kind : {Drift::Authority, Drift::Graph}) {
        Fixture materialized; fixture = &materialized; materialized.qualified = true;
        materialized.materialize_drift = kind; install_fake_functions();
        auto slot = theme::state.reserved_images;
        auto result = theme::create(image, authority(), owned);
        require_latched_slot(result, slot);
        std::uint8_t lock = 0xff;
        theme::detail::FreshMip fresh{};
        require(theme::read_mip(materialized.nodes[Texture].data(), fresh)
            && theme::detail::fresh_mip_matches(fresh, 2, 2, 16), "drift leaves valid fresh layout");
        require(materialized.pixels == std::array<std::uint8_t, 16>{}
            && core::safe_read_field(materialized.mip.data(), 0x34, lock) && lock == 0,
            "post-materialize drift prevents lock and pixel writes");
        require(materialized.events.back() == "materialize"
            && std::count(materialized.events.begin(), materialized.events.end(), "unlock") == 0
            && std::count(materialized.events.begin(), materialized.events.end(), "brush_dtor") == 0,
            "no native work after materialize drift");

        Fixture unlocking; fixture = &unlocking; unlocking.qualified = true;
        install_fake_functions();
        auto cleanup_authority = authority();
        theme::Record cleanup_record{};
        theme::Work cleanup_work{};
        cleanup_work.f = &theme::state.functions;
        cleanup_work.image = &image;
        cleanup_work.authority = &cleanup_authority;
        cleanup_work.access = unlocking.access();
        cleanup_work.record = &cleanup_record;
        require(static_cast<bool>(theme::guarded_sequence(cleanup_work)), "prepare cleanup graph");
        // Enter the locked failure state directly: safe_write_bytes can unprotect read-only pages.
        cleanup_work.bulk = unlocking.mip.data() + 0x10;
        cleanup_work.locked = true;
        put(unlocking.mip.data(), 0x34, std::uint8_t{2});
        unlocking.events.clear();
        unlocking.unlock_drift = kind;
        require(!theme::cleanup(cleanup_work), "cleanup rejects unlock drift");
        require(std::string_view(cleanup_work.result.stage) == "post_cleanup_unlock_admission"
            && unlocking.events.back() == "unlock"
            && std::count(unlocking.events.begin(), unlocking.events.end(), "unlock") == 1
            && std::count(unlocking.events.begin(), unlocking.events.end(), "brush_dtor") == 0
            && std::count(unlocking.events.begin(), unlocking.events.end(), "update") == 0
            && !cleanup_record.published,
            "cleanup-unlock drift prevents destructor and any retry/upload");

        Fixture destroyed; fixture = &destroyed; destroyed.qualified = true;
        destroyed.destroy_drift = kind; install_fake_functions();
        slot = theme::state.reserved_images;
        result = theme::create(image, authority(), owned);
        require_latched_slot(result, slot);
        require(std::string_view(result.stage) == "post_cleanup_admission"
            && destroyed.events.back() == "brush_dtor"
            && std::count(destroyed.events.begin(), destroyed.events.end(), "brush_dtor") == 1
            && std::count(destroyed.events.begin(), destroyed.events.end(), "update") == 1,
            "successful destructor drift is detected before publication, without retry");
    }
    containment_tests();
    target_api_tests(image);
    image_foundation_tests(image);
    generic_subject_tests(image);
    cursor_subject_tests(image);
    capture_member_tests();
    resolution_batch_tests();
    multi_member_tests(image);
    brush_tint_tests();
    growing_ledger_tests();
    texture_policy_tests();
    prepare_subjects_tests();
    prepare_hud_icons_tests();
    speed_membership_tests();
    growing_row_association_tests();
    Fixture fault; fixture = &fault; fault.qualified = true; fault.fault_unlock = true;
    install_fake_functions();
    require(theme::create(image, authority(), owned).outcome == Outcome::NativeFault,
        "native fault fails closed");
    require(std::count(fault.events.begin(), fault.events.end(), "unlock") == 1
        && fault.events.back() == "brush_dtor" && theme::state.poisoned,
        "uncertain unlock not retried; brush cleaned; fault latched");
    require(theme::configure(GetModuleHandleW(nullptr), FF7RP_TARGET_BUILD_ID).outcome
        == Outcome::Unavailable, "configure cannot erase native fault latch");
    const auto total = theme::state.reserved_images;
    theme::disable();
    require(theme::state.reserved_images == total, "disable retains lifetime budget");
    std::cout << "menu_theme_native_selftest passed (fake native calls only)\n";
}
