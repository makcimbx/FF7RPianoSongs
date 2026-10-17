#include "game/native_audio_bindings.h"
#include "game/rvas.h"
#include "core/generated/build_identity.generated.h"

#include <cstring>
#include <limits>
#include <new>

namespace ff7r::piano::game::native_audio {
const NativeAudioLayout* native_audio_layout(std::string_view build) noexcept {
    if(build!=core::generated::kBuildId)return nullptr;
    // Explicit old/new imported-program contracts, not an address delta.
    static constexpr NativeAudioLayout newer{"ff7rebirth-steam-win64-6a16ced2",0x60,0x50,0x288,0x2b8,0x2c0};
    // Older FUN_141348ed0 reads Music at +0x60; +0x50 is an alternate BGM key.
    static constexpr NativeAudioLayout older{"ff7rebirth-steam-win64-68fd6fde",0x60,0x50,0x280,0x2b0,0x2b8};
    if(build==newer.build_id)return &newer;
    if(build==older.build_id)return &older;
    return nullptr;
}
namespace {
namespace gu = uobject_locator_core;
using Failure = BindingFailure;

struct Capture {
    struct Span { Address address; std::size_t size; std::array<unsigned char, 80> bytes; };
    // Includes nested three-role captures and their rereads at the group bound.
    static constexpr std::size_t kSpanLimit = kNativeAudioEntryLimit * 512;
    MemoryReader source;
    std::unique_ptr<Span[]> spans{new (std::nothrow) Span[kSpanLimit]};
    std::size_t used = 0;
    Failure failure = Failure::None;
    Address failure_address = 0;
    bool reread_failed = false;
    static bool read(void* context, Address address, void* out, std::size_t size) noexcept
    {
        auto& c = *static_cast<Capture*>(context);
        if (!address || address < 0x10000 || size > 80 || !size
            || address > std::numeric_limits<Address>::max() - size || !c.source.read) {
            c.failure = Failure::Read; return false;
        }
        if (!c.spans || c.used == kSpanLimit) { c.failure = Failure::Budget; return false; }
        auto& s = c.spans[c.used]; s.address = address; s.size = size;
        if (!c.source.read(c.source.context, address, s.bytes.data(), size)) {
            c.failure = Failure::Read; c.failure_address = address; return false;
        }
        ++c.used;
        std::memcpy(out, s.bytes.data(), size); return true;
    }
    MemoryReader reader() noexcept { return {this, read}; }
    template<class T> bool get(Address a, T& out) noexcept { return read(this, a, &out, sizeof(out)); }
    bool stable() noexcept
    {
        std::array<unsigned char, 80> bytes{};
        for (std::size_t i = 0; i < used; ++i) {
            const auto& s = spans[i];
            if (!source.read(source.context, s.address, bytes.data(), s.size)) {
                failure_address = s.address; reread_failed = true; return false;
            }
            if (std::memcmp(s.bytes.data(), bytes.data(), s.size)) {
                failure_address = s.address; return false;
            }
        }
        return true;
    }
};
template<class T, std::size_t N> T field(const std::array<unsigned char, N>& bytes, std::size_t offset) noexcept
{ T value{}; std::memcpy(&value, bytes.data() + offset, sizeof(value)); return value; }

struct Profile { Address manager, object_vtable, backing_vtable, interface_vtable, lookup, append; };
Profile profile(unsigned char role) noexcept
{
    switch (role) {
    case 0: return {rva::BgmListRegistry, rva::BgmListObjectVtable, 0, 0, 0, rva::BgmListAppendBridge};
    case 1: return {rva::ScoreInfoRegistry, rva::ScoreInfoObjectVtable,
        rva::ScoreInfoBackingVtable, rva::ScoreInfoInterfaceVtable, rva::ScoreInfoTypedLookup, rva::ScoreInfoAppendBridge};
    case 2: return {rva::PianoScoreRegistry, rva::PianoScoreObjectVtable,
        rva::PianoScoreBackingVtable, rva::PianoScoreInterfaceVtable, rva::PianoScoreTypedLookup, rva::PianoScoreAppendBridge};
    default: return {};
    }
}

bool named(ResolvedNames names, std::uint64_t packed, std::string_view expected) noexcept
{
    if (!names.data || names.count > 64 || !static_cast<std::uint32_t>(packed) || expected.empty()) return false;
    unsigned matches = 0;
    for (std::size_t i = 0; i < names.count; ++i) {
        const auto& n = names.data[i];
        if (n.text == expected) { if (n.packed != packed) return false; ++matches; }
        else if (n.packed == packed) return false;
    }
    return matches == 1;
}

bool expected_names(ResolvedNames names, const BindingSeed& s,
    const ff7rp::pipeline::NativeAudioPackage& package, unsigned char role) noexcept
{
    constexpr std::string_view classes[]{"EndDataObjectBGMList", "EndDataObjectPianoScoreInfo", "EndDataObjectPianoScore"};
    constexpr std::string_view objects[]{"BGMList", "PianoScoreInfo", "PianoScore"};
    constexpr std::string_view stock_packages[]{"/Game/DataObject/Resident/BGMList", "/Game/DataObject/Resident/PianoScoreInfo", "/Game/DataObject/Resident/PianoScore"};
    const auto dot = package.object_path.rfind('.');
    const std::string_view class_path{package.class_path};
    constexpr std::string_view prefix = "/Script/EndDataObject.";
    if (dot == std::string::npos || std::string_view(package.object_path).substr(dot + 1) != objects[role]
        || class_path.substr(0, prefix.size()) != prefix || class_path.substr(prefix.size()) != classes[role]) return false;
    const std::string_view path{package.object_path};
    if (!named(names, s.owner.requested_path, path)
        || !named(names, s.candidate_names.package, path.substr(0, dot))
        || !named(names, s.stock_names.package, stock_packages[role])
        || !named(names, s.stock_association, "Resident")) return false;
    for (const auto* n : {&s.candidate_names, &s.stock_names}) {
        if (!named(names, n->instance, objects[role]) || !named(names, n->class_name, classes[role])
            || !named(names, n->metaclass, "Class") || !named(names, n->script_package, "/Script/EndDataObject")
            || !named(names, n->package_class, "Package")) return false;
    }
    return true;
}

// One-time bounded discovery only. Per-selection validation touches the pinned
// seven-node graphs, not unrelated UObjects. A failed read is not an empty slot.
BindingResult unique_candidates(const gu::UObjectIdentityAccess& access,
    const std::array<BindingSeed, 3>& seeds) noexcept
{
    struct Scan {
        MemoryReader source;
        bool failed = false;
        static bool read(void* ctx, Address address, void* out, std::size_t size) noexcept {
            auto& s = *static_cast<Scan*>(ctx);
            if (!s.source.read || !s.source.read(s.source.context, address, out, size)) {
                s.failed = true; return false;
            }
            return true;
        }
    } scan{access.memory};
    auto a = access; a.memory = {&scan, Scan::read};
    gu::GUObjectArrayHeader before{}, after{};
    BindingDiagnostic facts{}; facts.stage = "uniqueness";
    const auto fail = [&](const char* predicate, unsigned char role = 255) {
        facts.predicate = predicate; facts.read_failed = scan.failed;
        return BindingResult{Failure::Identity, role, facts};
    };
    facts.before_observed = gu::read_guobject_array_header(a.memory, a.array_address, {}, before);
    facts.before = before;
    if (!facts.before_observed || !gu::list_header_accepts(before)) return fail("initial_gu_header");
    std::array<unsigned, 3> found{};
    for (std::int32_t index = 0; index < before.num_elements; ++index) {
        facts.index = index; ++facts.scanned;
        Address object = 0;
        if (!gu::read_uobject_item_pointer(a, index, object)) {
            if (scan.failed) return fail("item_read");
            continue;
        }
        std::array<unsigned char, 40> h{};
        if (!Scan::read(&scan, object, h.data(), h.size())) return fail("object_header_read");
        for (unsigned i = 0; i < 3; ++i) {
            if (field<std::uint64_t>(h, 24) != seeds[i].candidate_names.instance
                || field<Address>(h, 16) != seeds[i].candidate.objects[1]) continue;
            const auto outer = field<Address>(h, 32);
            std::array<unsigned char, 40> package{};
            if (!outer || !Scan::read(&scan, outer, package.data(), package.size())) return fail("package_header_read", static_cast<unsigned char>(i));
            if (field<std::uint64_t>(package, 24) != seeds[i].candidate_names.package
                || field<Address>(package, 16) != seeds[i].candidate.objects[6]
                || field<Address>(package, 32)) continue;
            ++found[i]; facts.matches = found;
            if (object != seeds[i].candidate.objects[0] || found[i] != 1)
                return fail("candidate_ambiguous", static_cast<unsigned char>(i));
        }
    }
    facts.after_observed = gu::read_guobject_array_header(a.memory, a.array_address, {}, after);
    facts.after = after;
    if (!facts.after_observed) return fail("final_gu_header_read");
    if (before.chunks != after.chunks || before.num_elements != after.num_elements
        || before.max_elements != after.max_elements || before.num_chunks != after.num_chunks
        || before.max_chunks != after.max_chunks) return fail("gu_header_drift");
    for (unsigned char i = 0; i < 3; ++i) if (found[i] != 1) return fail("candidate_missing", i);
    return {};
}

bool owner(Capture& c, Address base, const RetainedAcquisition& o) noexcept
{
    std::array<unsigned char, 3> flags{};
    std::int32_t strong = 0, weak = 0, count = 0, capacity = 0;
    Address paths = 0; std::array<unsigned char, 24> path{};
    Address service = 0, manager = 0, vtable = 0;
    return o.handle && o.control && !((o.handle | o.control) % 8) && o.requested_path
        && c.get(base + rva::StreamableServiceGlobal, service) && service
        && service <= std::numeric_limits<Address>::max() - 88
        && c.get(o.handle + 152, manager) && manager == service + 88
        && c.get(manager, vtable) && vtable == base + rva::StreamableManagerVtable
        && c.get(o.handle + 16, flags) && flags[0] == 1 && flags[1] == 0 && flags[2] == 0
        && c.get(o.control + 8, strong) && strong > 0
        && c.get(o.control + 12, weak) && weak > 0
        && c.get(o.handle + 104, paths) && paths && paths % 8 == 0
        && c.get(o.handle + 112, count) && count == 1
        && c.get(o.handle + 116, capacity) && capacity >= 1 && capacity <= 64
        && c.get(paths, path) && field<std::uint64_t>(path, 0) == o.requested_path
        && field<Address>(path, 8) == 0 && field<std::uint64_t>(path, 16) == 0;
}

Failure map_binding(Capture& c, Address base, std::uint64_t key, Address manager,
    Address& root, std::array<unsigned char, 80>& header) noexcept
{
    if (!c.get(base + rva::DataObjectNameMap, root) || !root || root % 8 || !c.get(root, header)) return Failure::Read;
    const auto storage = field<Address>(header, 0), heap = field<Address>(header, 64);
    const auto slots = field<std::int32_t>(header, 8), capacity = field<std::int32_t>(header, 12);
    const auto free = field<std::int32_t>(header, 52), buckets = field<std::int32_t>(header, 72);
    if (slots < 0 || capacity < slots || capacity > 2'000'000 || free < 0 || free > slots
        || buckets <= 0 || buckets > 1'048'576 || (buckets & (buckets - 1))
        || (capacity && !storage) || storage % 8 || heap % 4 || (!heap && buckets != 1)) return Failure::MapBounds;
    const auto id = static_cast<std::uint32_t>(key), number = static_cast<std::uint32_t>(key >> 32);
    const auto lo = id & 0xffffu, hi = id >> 16;
    const std::uint32_t hash = number + lo * 0x10001u + (lo >> 4) + hi * 0x80001u;
    std::int32_t index = -1;
    if (!c.get(heap ? heap + (hash & (buckets - 1)) * 4 : root + 56, index)) return Failure::Read;
    std::array<std::int32_t, 64> seen{}; std::size_t visited = 0, found = 0;
    while (index != -1) {
        if (index < 0 || index >= slots || visited >= seen.size()
            || visited >= static_cast<std::size_t>(slots - free)) return Failure::MapChain;
        for (std::size_t i = 0; i < visited; ++i) if (seen[i] == index) return Failure::MapChain;
        seen[visited++] = index;
        std::array<unsigned char, 24> entry{};
        if (!c.get(storage + static_cast<Address>(index) * 24, entry)) return Failure::Read;
        if (field<std::uint64_t>(entry, 0) == key) {
            if (field<Address>(entry, 8) != manager) return Failure::MapBinding;
            ++found;
        }
        index = field<std::int32_t>(entry, 16);
    }
    return found == 1 ? Failure::None : Failure::MapBinding;
}
} // namespace

BindingResult CohortBindings::read_record(const gu::UObjectIdentityAccess& access,
    unsigned char role, Record& record, BindingPhase phase, bool initial) const noexcept
{
    Capture c{access.memory}; auto identity = access; identity.memory = c.reader();
    const auto p = profile(role);
    BindingDiagnostic facts{}; facts.stage = "profile"; facts.phase = phase;
    const auto fail = [&](Failure f) {
        if (std::string_view(facts.predicate) == "none") facts.predicate = binding_failure_name(f);
        facts.read_failed = c.failure == Failure::Read || c.reread_failed;
        return BindingResult{c.failure == Failure::None ? f : c.failure, role, facts};
    };
    if (!p.manager || !p.object_vtable || !rva::DataObjectNameMap) return fail(Failure::UnsupportedBuild);
    for (unsigned side = 0; side < 2; ++side) {
        facts.stage = "identity"; facts.side = side ? BindingSide::Stock : BindingSide::Candidate;
        const auto& witness = side ? record.seed.stock : record.seed.candidate;
        const auto& names = side ? record.seed.stock_names : record.seed.candidate_names;
        auto& saved = side ? record.stock : record.candidate;
        const std::array<std::uint64_t, 7> expected_names{names.instance, names.class_name,
            names.metaclass, names.script_package, names.package_class, names.package, names.package_class};
        for (unsigned i = 0; i < 7; ++i) {
            facts.node = static_cast<unsigned char>(i); facts.index = -1;
            facts.serial = 0; facts.serial_observed = false;
            facts.identity_result = facts.zero_result = 0; facts.predicate = "object_header";
            facts.zero_attempted = false;
            Identity current{};
            if (witness.objects[i] % 8 || !c.get(witness.objects[i], current.header)) return fail(Failure::Read);
            facts.index = field<std::int32_t>(current.header, 12);
            if (field<std::uint32_t>(current.header, 8) & 0x10) {
                facts.predicate = "default_object"; return fail(Failure::Relationship);
            }
            gu::UObjectLiveHandle live{};
            const auto first_span = c.used;
            const auto live_result = gu::capture_live_handle_diagnostic(identity, witness.objects[i], live);
            facts.identity_result = static_cast<unsigned>(live_result);
            // These outcomes follow a complete item read; its final copied field is serial.
            using Live = gu::UObjectLiveHandleCaptureResult;
            if (live_result == Live::Success || live_result == Live::ObjectMismatch
                || live_result == Live::SerialInvalid || live_result == Live::ItemFlagsDead
                || live_result == Live::UObjectDestroyed) {
                if (c.used > first_span && c.spans[c.used - 1].size == sizeof(std::int32_t)) {
                    std::memcpy(&facts.serial, c.spans[c.used - 1].bytes.data(), sizeof(facts.serial));
                    facts.serial_observed = true;
                }
            }
            facts.predicate = i ? "live_metadata" : "positive_instance";
            if (live_result == Live::Success) {
                current.index = live.internal_index; current.serial = live.serial_number;
            } else if (live_result == Live::SerialInvalid
                && (i || (!side && phase == BindingPhase::BeforeBridge && !record.registered))) {
                // Metadata zero is an item/header observation, not a weak handle or lease.
                // Its saved zero remains exact: only our candidate instance may transition.
                gu::UObjectItemBackedZeroSerialSnapshot zero{};
                facts.zero_attempted = true;
                const auto zero_result = gu::capture_item_backed_zero_serial_snapshot_diagnostic(identity, witness.objects[i], zero);
                facts.zero_result = static_cast<unsigned>(zero_result); facts.predicate = "item_backed_zero";
                if (!gu::item_backed_zero_serial_capture_succeeded(zero_result)) return fail(Failure::Identity);
                current.index = zero.internal_index;
            } else return fail(Failure::Identity);
            facts.index = current.index; facts.serial = current.serial; facts.serial_observed = true;
            facts.predicate = "name";
            if (!expected_names[i] || field<std::uint64_t>(current.header, 24) != expected_names[i]) return fail(Failure::Identity);
            if (initial) saved[i] = current;
            else {
                facts.predicate = "pinned_header_index";
                if (saved[i].header != current.header || saved[i].index != current.index) return fail(Failure::Identity);
                const bool assignment = !side && !i && !record.registered && phase == BindingPhase::AfterOwnBridge;
                const auto expected_serial = record.registered && !side && !i ? record.assigned_serial : saved[i].serial;
                facts.predicate = "pinned_serial";
                if (assignment ? current.serial <= 0 || (saved[i].serial && saved[i].serial != current.serial)
                               : expected_serial != current.serial) return fail(Failure::Identity);
                if (assignment) record.assigned_serial = current.serial;
            }
        }
        facts.stage = "graph"; facts.node = 255; facts.predicate = "relationships";
        facts.index = -1; facts.serial_observed = false;
        const auto& o = witness.objects;
        const auto cls = [&](unsigned i) { return field<Address>(saved[i].header, 16); };
        const auto outer = [&](unsigned i) { return field<Address>(saved[i].header, 32); };
        if (cls(0) != o[1] || cls(1) != o[2] || outer(1) != o[3] || outer(3)
            || cls(3) != o[4] || cls(4) != o[2] || outer(0) != o[5]
            || outer(5) || cls(5) != o[6] || cls(6) != o[2]
            || field<Address>(saved[0].header, 0) != image_base_ + p.object_vtable
             || (field<std::uint32_t>(saved[0].header, 8) & 0x10)) return fail(Failure::Relationship);
        const auto* layout=native_audio_layout(core::generated::kBuildId);
        Address append=0;
        facts.predicate="append_slot";
        if(!layout || !c.get(image_base_+p.object_vtable+layout->append_slot,append)
            || append!=image_base_+p.append)return fail(Failure::Relationship);
        if (p.backing_vtable) {
            facts.stage = "backing"; facts.predicate = "backing";
            facts.node = 0; facts.index = saved[0].index; facts.serial = saved[0].serial;
            facts.serial_observed = true;
            std::uint8_t mode = 0; Address backing = 0, vtable = 0, iface = 0, lookup = 0;
            if (!c.get(o[0] + 84, mode) || !c.get(o[0] + 88, backing) || !backing || backing % 8
                || !c.get(backing, vtable) || !c.get(backing + 88, iface)
                || !c.get(image_base_ + p.interface_vtable + 40, lookup)) return fail(Failure::Read);
            if (mode != 1 || vtable != image_base_ + p.backing_vtable
                || iface != image_base_ + p.interface_vtable || lookup != image_base_ + p.lookup)
                return fail(Failure::Backing);
            // Both qualified builds use these exact leaf bodies, at different
            // virtual slots. Validate without calling a borrowed native getter.
            Address getter=0,serialized=0;
            std::array<unsigned char,5> backing_code{};
            std::array<unsigned char,9> serialized_code{};
            facts.predicate="getter_slots";
            if(!c.get(image_base_+p.object_vtable+layout->backing_slot,getter)
                || !c.get(image_base_+p.object_vtable+layout->serialized_interface_slot,serialized)
                || getter<image_base_ || serialized<image_base_
                || getter-image_base_>core::generated::kSizeOfImage-backing_code.size()
                || serialized-image_base_>core::generated::kSizeOfImage-serialized_code.size()
                || !c.get(getter,backing_code) || !c.get(serialized,serialized_code)
                || backing_code!=std::array<unsigned char,5>{0x48,0x8b,0x41,0x58,0xc3}
                || serialized_code!=std::array<unsigned char,9>{0x48,0x8b,0x41,0x58,0x48,0x83,0xc0,0x58,0xc3})
                return fail(Failure::Backing);
            if (initial) record.backing[side] = backing;
            else if (record.backing[side] != backing) return fail(Failure::Backing);
        }
    }
    facts.side = BindingSide::None; facts.node = 255; facts.index = -1;
    facts.serial = 0; facts.serial_observed = false;
    facts.stage = "owner"; facts.predicate = "retained_owner";
    if (!owner(c, image_base_, record.seed.owner)) return fail(Failure::Owner);
    facts.stage = "registry"; facts.predicate = "registry";
    unsigned candidates = 0, stocks = 0;
    const bool post = phase != BindingPhase::BeforeBridge || record.registered;
    for (unsigned group = 0; group < 3; ++group) {
        std::array<unsigned char, 16> h{};
        if (!c.get(image_base_ + p.manager + group * 16, h)) return fail(Failure::Read);
        const auto data = field<Address>(h, 0); const auto count = field<std::int32_t>(h, 8), capacity = field<std::int32_t>(h, 12);
        const int limit = role ? 100000 : 4096;
        if (count < 0 || count > static_cast<std::int32_t>(kNativeAudioEntryLimit) || capacity < count || capacity > limit || (capacity && !data)
            || (data && (data < 0x10000 || data % 8))) return fail(Failure::RegistryBounds);
        for (int n = 0; n < count; ++n) {
            std::array<unsigned char, 16> entry{}; Address object = 0;
            if (!c.get(data + n * 16, entry)) return fail(Failure::Read);
            gu::UObjectLiveHandle weak{field<std::int32_t>(entry, 0), field<std::int32_t>(entry, 4)};
            if (weak.internal_index < 0 || weak.serial_number <= 0
                || !gu::read_uobject_item_pointer(identity, weak.internal_index, object)
                || !gu::validate_live_handle(identity, object, weak)) return fail(Failure::RegistryWeak);
            const auto association = field<std::uint64_t>(entry, 8);
            if (object == record.seed.candidate.objects[0]) {
                ++candidates;
                if (!post || group != 2 || association != record.seed.association
                    || weak.serial_number != record.assigned_serial) return fail(Failure::RegistryMembership);
            }
            if (object == record.seed.stock.objects[0]) {
                ++stocks;
                if (group != 0 || association != record.seed.stock_association) return fail(Failure::RegistryMembership);
            }
        }
    }
    if (stocks != 1 || candidates != (post ? 1u : 0u)) return fail(Failure::RegistryMembership);
    Address root = 0; std::array<unsigned char, 80> header{};
    facts.stage = "map"; facts.predicate = "map_binding";
    const auto mapped = map_binding(c, image_base_, record.seed.stock_names.instance, image_base_ + p.manager, root, header);
    if (mapped != Failure::None) return fail(mapped);
    if (initial) { record.map_root = root; record.map_header = header; }
    else if (root != record.map_root || (phase != BindingPhase::CurrentRegistered && header != record.map_header))
        return fail(Failure::HistoricalMapDrift);
    facts.stage = "record_reread"; facts.predicate = "captured_span_drift";
    if (!c.stable()) {
        for (unsigned side = 0; side < 2; ++side) {
            const auto& witness = side ? record.seed.stock : record.seed.candidate;
            const auto& saved = side ? record.stock : record.candidate;
            for (unsigned i = 0; i < 7; ++i) if (c.failure_address >= witness.objects[i]
                && c.failure_address - witness.objects[i] < 40) {
                facts.side = side ? BindingSide::Stock : BindingSide::Candidate;
                facts.node = static_cast<unsigned char>(i); facts.index = saved[i].index;
                facts.serial = saved[i].serial; facts.serial_observed = true;
                return fail(Failure::Drift);
            }
        }
        return fail(Failure::Drift);
    }
    return {};
}

BindingResult CohortBindings::capture(std::string_view build, Address base,
    const gu::UObjectIdentityAccess& identity,
    std::shared_ptr<const ff7rp::pipeline::NativeAudioArtifactSet> artifacts,
    const std::array<BindingSeed, 3>& seeds, ResolvedNames names) noexcept
{
    if (captured_ || terminal_failure_.failure != Failure::None) return {Failure::Input};
    if (!native_audio_layout(build) || !rva::DataObjectNameMap) return {Failure::UnsupportedBuild};
    if (!artifacts || artifacts->build_id != build || !base
        || base > std::numeric_limits<Address>::max() - core::generated::kSizeOfImage) return {Failure::Input};
    CohortBindings pending; pending.image_base_ = base; pending.artifacts_ = std::move(artifacts);
    const auto unique = unique_candidates(identity, seeds);
    if (!unique) return unique;
    Capture batch{identity.memory}; auto all = identity; all.memory = batch.reader();
    for (unsigned char i = 0; i < 3; ++i) {
        const auto& s = seeds[i];
        for (const unsigned node : {1u, 2u, 3u, 4u, 6u})
            if (s.candidate.objects[node] != s.stock.objects[node]) return {Failure::Relationship, i};
        if (i) for (const unsigned node : {2u, 3u, 4u, 6u})
            if (s.candidate.objects[node] != seeds[0].candidate.objects[node]) return {Failure::Relationship, i};
        const auto& package = pending.artifacts_->packages[i + 1];
        if (package.role != static_cast<ff7rp::pipeline::NativeAudioPackageRole>(i + 1)
            || !expected_names(names, s, package, i)) return {Failure::Input, i};
        if (!static_cast<std::uint32_t>(s.association) || !s.stock_association || s.candidate.objects[0] == s.stock.objects[0]
            || s.association == s.stock_association
            || s.candidate_names.instance != s.stock_names.instance) return {Failure::Input, i};
        for (unsigned char j = 0; j < i; ++j)
            if (s.candidate.objects[0] == seeds[j].candidate.objects[0]
                || s.owner.handle == seeds[j].owner.handle || s.owner.control == seeds[j].owner.control)
                return {Failure::Input, i};
        pending.records_[i].seed = s;
        const auto result = pending.read_record(all, i, pending.records_[i], BindingPhase::BeforeBridge, true);
        if (!result) return result;
    }
    if (!batch.stable()) { BindingResult result{Failure::Drift}; result.diagnostic.stage = "cohort_reread";
        result.diagnostic.predicate = "captured_span_drift"; result.diagnostic.read_failed = batch.reread_failed; return result; }
    pending.captured_ = true; *this = std::move(pending); return {};
}

BindingResult CohortBindings::validate(const gu::UObjectIdentityAccess& identity, BindingPhase phase) const noexcept
{
    if (!captured_ || phase == BindingPhase::AfterOwnBridge) return {Failure::Input};
    if (!terminal_failure_) return {Failure::PartialMutation, terminal_failure_.role, terminal_failure_.diagnostic};
    Capture batch{identity.memory}; auto all = identity; all.memory = batch.reader();
    for (unsigned char i = 0; i < 3; ++i) {
        if (phase == BindingPhase::CurrentRegistered && !records_[i].registered) return {Failure::Input, i};
        auto copy = records_[i]; const auto result = read_record(all, i, copy, phase, false);
        if (!result) return result;
    }
    if (batch.stable()) return {};
    BindingResult result{Failure::Drift}; result.diagnostic.stage = "cohort_reread";
    result.diagnostic.predicate = "captured_span_drift"; result.diagnostic.phase = phase;
    result.diagnostic.read_failed = batch.reread_failed; return result;
}

BindingResult CohortBindings::latch_after_own_bridge(const gu::UObjectIdentityAccess& identity, unsigned char role) noexcept
{
    if (!captured_ || role >= 3 || records_[role].registered) return {Failure::Input, role};
    if (!terminal_failure_) return {Failure::PartialMutation, terminal_failure_.role, terminal_failure_.diagnostic};
    Capture batch{identity.memory}; auto all = identity; all.memory = batch.reader();
    auto copy = records_[role];
    const auto result = read_record(all, role, copy, BindingPhase::AfterOwnBridge, false);
    if (!result) { terminal_failure_ = result; return result; }
    for (unsigned char i = 0; i < 3; ++i) {
        if (i == role) continue;
        auto other = records_[i];
        const auto checked = read_record(all, i, other, BindingPhase::BeforeBridge, false);
        if (!checked) { terminal_failure_ = checked; return checked; }
    }
    if (!batch.stable()) { terminal_failure_ = {Failure::Drift, role};
        terminal_failure_.diagnostic.stage = "cohort_reread"; terminal_failure_.diagnostic.predicate = "captured_span_drift";
        terminal_failure_.diagnostic.phase = BindingPhase::AfterOwnBridge;
        terminal_failure_.diagnostic.read_failed = batch.reread_failed; return terminal_failure_; }
    copy.registered = true; records_[role] = copy; return {};
}

const char* binding_failure_name(BindingFailure value) noexcept
{
    switch (value) {
#define NAMED(x) case Failure::x: return #x
    NAMED(None); NAMED(UnsupportedBuild); NAMED(Input); NAMED(Read); NAMED(Identity);
    NAMED(Relationship); NAMED(Owner); NAMED(RegistryBounds); NAMED(RegistryWeak);
    NAMED(RegistryMembership); NAMED(MapBounds); NAMED(MapChain); NAMED(MapBinding);
    NAMED(HistoricalMapDrift); NAMED(Backing); NAMED(Drift); NAMED(Budget); NAMED(PartialMutation);
#undef NAMED
    }
    return "Unknown";
}
} // namespace ff7r::piano::game::native_audio
