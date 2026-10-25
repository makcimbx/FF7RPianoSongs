#include "game/menu_theme_observation.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "core/version_gate.h"
#include "game/generated/rvas.generated.h"
#include "game/hook_specs.h"
#include "game/menu_theme_metrics.h"
#include "game/uobject_identity.h"

#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <sstream>

namespace ff7r::piano::game::menu_theme_observation {
namespace {
namespace obj = uobject_locator_core;
namespace perf = menu_theme_metrics;
using Address = obj::Address;
struct Span { const char* id; Address rva; std::size_t size; };
constexpr std::array data_spans{
    Span{"native_game_thread_initialized", rva::NativeGameThreadInitialized, 1},
    Span{"native_game_thread_id", rva::NativeGameThreadId, 4},
    Span{"native_garbage_collecting", rva::NativeGarbageCollecting, 4},
    Span{"native_uobject_exit_purge", rva::NativeUObjectExitPurge, 1},
    Span{"native_transient_package", rva::NativeTransientPackage, 8},
};
constexpr std::array sites{
    Span{"native_game_thread_publish_site", rva::NativeGameThreadPublishSite, 13},
    Span{"native_gc_flag_exchange_site", rva::NativeGcFlagExchangeSite, 15},
    Span{"native_exit_purge_publish_site", rva::NativeExitPurgePublishSite, 12},
    Span{"native_uobject_thread_role_site", rva::NativeUObjectThreadRoleSite, 69},
    Span{"piano_menu_list_open_call", rva::PianoMenuListOpenCall, 5},
};
constexpr std::array<std::uint8_t, 24> tls_tail{
    0x65,0x48,0x8b,0x04,0x25,0x58,0,0,0,0xba,0x98,0x03,0,0,
    0x48,0x8b,0x08,0x83,0x3c,0x0a,0x02,0x0f,0x94,0xc0};

void fail(Snapshot& s, Failure f, const char* site) noexcept
{
    if (s.failure == Failure::None) { s.failure = f; s.failure_site = site; }
}
bool add(Address base, Address offset, Address& out) noexcept
{
    if (!base || offset > std::numeric_limits<Address>::max() - base) return false;
    out = base + offset; return true;
}
template<class T> bool read(obj::MemoryReader m, Address address, T& out) noexcept
{
    return address && sizeof(T) - 1 <= std::numeric_limits<Address>::max() - address
        && m.read && m.read(m.context, address, &out, sizeof(T));
}
template<class T> bool field(obj::MemoryReader m, Address base, Address offset, T& out) noexcept
{
    Address address = 0; return add(base, offset, address) && read(m, address, out);
}
bool inside(const obj::ImageView& image, Address rva_value, std::size_t size) noexcept
{
    return image.base && rva_value && rva_value < image.size && size <= image.size - rva_value
        && image.size <= std::numeric_limits<Address>::max() - image.base;
}
bool mapped(HMODULE module, const obj::ImageView& image, const Span& span, bool code) noexcept
{
    if (!inside(image, span.rva, span.size)) return false;
    const Address address = image.base + span.rva;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info))
        || info.AllocationBase != module || info.State != MEM_COMMIT || info.Type != MEM_IMAGE
        || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto start = reinterpret_cast<Address>(info.BaseAddress);
    if (address < start || address - start > info.RegionSize
        || span.size > info.RegionSize - (address - start)) return false;
    const auto p = info.Protect & 0xff;
    return code ? p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY
        : p == PAGE_READWRITE || p == PAGE_WRITECOPY;
}
bool rip(const obj::ImageView& image, Address site, Address displacement,
    Address end, Address target) noexcept
{
    std::int32_t delta = 0;
    if (!inside(image, site, end) || !field(image.memory, image.base + site, displacement, delta))
        return false;
    const auto result = static_cast<std::int64_t>(site) + static_cast<std::int64_t>(end) + delta;
    return result > 0 && static_cast<std::uint64_t>(result) == target;
}
// Value/reader seam: production additionally checks MEM_IMAGE mappings before entry.
bool authenticate(const obj::ImageView& image, std::string_view profile, bool supported,
    Snapshot& s)
{
    if (profile != FF7RP_TARGET_BUILD_ID || !supported) {
        fail(s, Failure::UnsupportedImage, "executable_identity"); return false;
    }
    for (const auto& data : data_spans) {
        if (!inside(image, data.rva, data.size)) {
            fail(s, Failure::Mapping, data.id); return false;
        }
    }
    for (const auto& site : sites) {
        const auto* spec = find_rva_signature(site.id);
        if (!spec || spec->expected_prologue.empty()) {
            fail(s, Failure::MissingSpec, site.id); return false;
        }
        if (spec->rva != site.rva || spec->expected_prologue.size() < site.size
            || spec->expected_prologue.size() > 128) {
            fail(s, Failure::Signature, site.id); return false;
        }
        std::array<std::uint8_t, 128> actual{};
        if (!inside(image, spec->rva, spec->expected_prologue.size())
            || !image.memory.read || !image.memory.read(image.memory.context,
                image.base + site.rva, actual.data(), spec->expected_prologue.size())) {
            fail(s, Failure::Mapping, site.id); return false;
        }
        if (std::memcmp(actual.data(), spec->expected_prologue.data(), spec->expected_prologue.size())) {
            fail(s, Failure::Signature, site.id); return false;
        }
        if (site.rva == rva::NativeUObjectThreadRoleSite
            && (spec->expected_prologue.size() != 69
                || std::memcmp(actual.data() + 45, tls_tail.data(), tls_tail.size()))) {
            fail(s, Failure::TlsContract, site.id); return false;
        }
    }
    if (!rip(image, rva::NativeGameThreadPublishSite, 3, 7, rva::NativeGameThreadInitialized)
        || !rip(image, rva::NativeGameThreadPublishSite, 9, 13, rva::NativeGameThreadId)
        || !rip(image, rva::NativeGcFlagExchangeSite, 2, 6, rva::NativeGarbageCollecting)
        || !rip(image, rva::NativeExitPurgePublishSite, 2, 7, rva::NativeUObjectExitPurge)
        || !rip(image, rva::NativeUObjectThreadRoleSite, 3, 7, rva::NativeGameThreadInitialized)) {
        fail(s, Failure::RipReference, "signed_rip_targets"); return false;
    }
    s.authenticated = true; return true;
}

bool authenticate_module(HMODULE module, std::string_view profile, Snapshot& s, obj::ImageView& image)
{
    if (!module || profile != FF7RP_TARGET_BUILD_ID || !core::is_supported_exe(module)) {
        fail(s, Failure::UnsupportedImage, "executable_identity"); return false;
    }
    const auto range = core::image_range(module);
    image = {reinterpret_cast<Address>(range.base), range.size,
        {nullptr, [](void*, Address p, void* to, std::size_t n) noexcept {
            return core::safe_copy_bytes(reinterpret_cast<void*>(p), to, n);
        }}};
    for (const auto& data : data_spans) {
        if (!mapped(module, image, data, false)) { fail(s, Failure::Mapping, data.id); return false; }
    }
    for (const auto& site : sites) {
        const auto* spec = find_rva_signature(site.id);
        if (!spec || spec->expected_prologue.empty()) { fail(s, Failure::MissingSpec, site.id); return false; }
        if (!mapped(module, image, {site.id, site.rva, spec->expected_prologue.size()}, true)) {
            fail(s, Failure::Mapping, site.id); return false;
        }
    }
    return authenticate(image, profile, true, s);
}

struct FactsAuthentication {
    std::mutex mutex;
    std::atomic<bool> ready{false};
    HMODULE module = nullptr;
    obj::ImageView image{};
};
// One fixed entry, only for the main executable: it cannot unload/reload within
// this process. Never retain a DLL/borrowed reloadable module or dynamic facts.
// The initializer seam exercises the same publication path with actual fake reads.
template<class Initialize>
bool facts_image(FactsAuthentication& state, HMODULE module, HMODULE process_image,
    std::string_view profile, Snapshot& s, obj::ImageView& image, Initialize&& initialize)
{
    if (!module || module != process_image || profile != FF7RP_TARGET_BUILD_ID) {
        fail(s, Failure::UnsupportedImage, "executable_identity"); return false;
    }
    auto reuse = [&] {
        if (state.module != module) {
            fail(s, Failure::UnsupportedImage, "authenticated_module"); return false;
        }
        image = state.image; s.authenticated = true; return true;
    };
    // Published fields never change; hot concurrent captures need no mutex.
    if (state.ready.load(std::memory_order_acquire)) return reuse();
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        if (state.ready.load(std::memory_order_acquire)) return reuse();
        fail(s, Failure::AuthenticationBusy, "facts_authentication"); return false;
    }
    if (state.ready.load(std::memory_order_acquire)) return reuse();
    obj::ImageView candidate{};
    if (!initialize(module, profile, s, candidate)) return false;
    if (!s.authenticated || s.failure != Failure::None || candidate.base != reinterpret_cast<Address>(module)) {
        s.authenticated = false;
        fail(s, Failure::Mapping, "authenticated_image_base"); return false;
    }
    state.image = candidate;
    state.module = module;
    state.ready.store(true, std::memory_order_release); // Complete immutable contract only; no reset/reload.
    image = candidate;
    return true;
}

// Only a projection of the shared registry layout, not a second lifetime service.
bool item_flags(const obj::UObjectIdentityAccess& a, ObjectFacts& out) noexcept
{
    obj::GUObjectArrayHeader h{};
    if (!obj::read_guobject_array_header(a.memory, a.array_address, a.facts.array, h)
        || !obj::chart_header_accepts(a.array_address, h) || out.index < 0
        || out.index >= h.num_elements || a.facts.elements_per_chunk <= 0) return false;
    const auto chunk_index = out.index / a.facts.elements_per_chunk;
    if (chunk_index >= h.num_chunks) return false;
    const auto within = static_cast<Address>(out.index % a.facts.elements_per_chunk);
    if (!a.facts.item.size || within > std::numeric_limits<Address>::max() / a.facts.item.size)
        return false;
    Address chunk = 0, item = 0;
    return field(a.memory, h.chunks, static_cast<Address>(chunk_index) * sizeof(Address), chunk)
        && chunk && add(chunk, within * a.facts.item.size, item)
        && field(a.memory, item, a.facts.item.flags, out.item_flags)
        && field(a.memory, item, a.facts.item.serial_number, out.serial);
}
ObjectFacts object_facts(const obj::UObjectIdentityAccess& a, Address object) noexcept
{
    perf::Span metric(perf::Metric::FactsObjectSnapshot);
    ObjectFacts out{}; out.object = object;
    out.fields = field(a.memory, object, a.facts.object.internal_index, out.index)
        && field(a.memory, object, a.facts.object.flags, out.object_flags)
        && field(a.memory, object, 0x10, out.klass)
        && field(a.memory, object, 0x18, out.name_comparison)
        && field(a.memory, object, 0x1c, out.name_number)
        && field(a.memory, object, 0x20, out.outer) && item_flags(a, out);
    obj::UObjectLiveHandle live{};
    out.live_result = obj::capture_live_handle_diagnostic(a, object, live);
    if (out.live_result == obj::UObjectLiveHandleCaptureResult::Success) {
        out.membership = out.index == live.internal_index && out.serial == live.serial_number;
    } else if (out.live_result == obj::UObjectLiveHandleCaptureResult::SerialInvalid) {
        obj::UObjectItemBackedZeroSerialSnapshot zero{};
        out.zero_result = obj::capture_item_backed_zero_serial_snapshot_diagnostic(a, object, zero);
        out.diagnostic_zero_serial = out.zero_result == obj::UObjectItemBackedZeroSerialCaptureResult::Success;
        out.membership = out.diagnostic_zero_serial && out.index == zero.internal_index && out.serial == 0;
    }
    return out;
}
// GS read is isolated; no DLL/PE TLS-index fallback or native helper invocation.
Address native_tls_vector() noexcept
{
    __try { return static_cast<Address>(__readgsqword(0x58)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void capture_reads(const obj::ImageView& image, const obj::UObjectIdentityAccess& a,
    Address tls_vector, Snapshot& s) noexcept
{
    perf::Span metric(perf::Metric::FactsDynamicReads);
    if (!s.authenticated) return;
    if (!read(image.memory, image.base + rva::NativeGameThreadInitialized, s.thread_initialized))
        fail(s, Failure::InitializedRead, "initialized");
    if (!read(image.memory, image.base + rva::NativeGameThreadId, s.native_thread))
        fail(s, Failure::ThreadRead, "thread_id");
    if (!read(image.memory, image.base + rva::NativeGarbageCollecting, s.garbage_collecting))
        fail(s, Failure::GarbageRead, "garbage_collecting");
    if (!read(image.memory, image.base + rva::NativeUObjectExitPurge, s.exit_purge))
        fail(s, Failure::ExitRead, "exit_purge");
    s.tls_vector = tls_vector;
    if (!tls_vector) fail(s, Failure::TlsVectorRead, "native_tls_vector");
    else if (!read(image.memory, tls_vector, s.tls_block) || !s.tls_block)
        fail(s, Failure::TlsBlockRead, "native_tls_slot0");
    else if (!field(image.memory, s.tls_block, 0x398, s.tls_role))
        fail(s, Failure::TlsRoleRead, "native_tls_role");

    Address package = 0, after = 0;
    if (!read(image.memory, image.base + rva::NativeTransientPackage, package)) {
        fail(s, Failure::PackageSlotRead, "transient_slot"); return;
    }
    if (!package) { fail(s, Failure::PackageNull, "transient_slot"); return; }
    s.package = object_facts(a, package);
    if (!s.package.fields) fail(s, Failure::PackageFields, "package_fields");
    if (!s.package.membership) fail(s, Failure::PackageMembership, "package_membership");
    s.package_class = object_facts(a, s.package.klass);
    if (!s.package_class.fields) fail(s, Failure::PackageClassFields, "class_fields");
    if (!s.package_class.membership) fail(s, Failure::PackageClassMembership, "class_membership");
    const auto package_after = object_facts(a, package);
    const auto class_after = object_facts(a, s.package.klass);
    if (!read(image.memory, image.base + rva::NativeTransientPackage, after))
        fail(s, Failure::PackageReread, "transient_slot_reread");
    else if (package != after || s.package != package_after || s.package_class != class_after)
        fail(s, Failure::PackageDrift, "package_reread");
    s.complete = s.failure == Failure::None;
}
bool same_session(const MenuSessionSnapshot& a, const MenuSessionSnapshot& b) noexcept
{
    return a.generation == b.generation && a.phase == b.phase && a.controller == b.controller
        && a.list == b.list && a.widget == b.widget && a.list_ownership == b.list_ownership
        && a.widget_identity.internal_index == b.widget_identity.internal_index
        && a.widget_identity.serial_number == b.widget_identity.serial_number;
}
bool equal_facts(const Snapshot& a, const Snapshot& b) noexcept
{
    return a.failure == b.failure && a.authenticated == b.authenticated && a.complete == b.complete
        && same_session(a.session, b.session) && a.expected_generation == b.expected_generation
        && a.caller == b.caller && a.expected_caller == b.expected_caller
        && a.current_thread == b.current_thread && a.native_thread == b.native_thread
        && a.thread_initialized == b.thread_initialized && a.tls_vector == b.tls_vector
        && a.tls_block == b.tls_block && a.tls_role == b.tls_role
        && a.garbage_collecting == b.garbage_collecting && a.exit_purge == b.exit_purge
        && a.package == b.package && a.package_class == b.package_class;
}
const char* name(Failure f) noexcept
{
#define OBS_CASE(x) case Failure::x: return #x
    switch (f) {
    OBS_CASE(None); OBS_CASE(UnsupportedImage); OBS_CASE(MissingSpec); OBS_CASE(Signature); OBS_CASE(AuthenticationBusy);
    OBS_CASE(Mapping); OBS_CASE(RipReference); OBS_CASE(TlsContract); OBS_CASE(Session); OBS_CASE(Caller);
    OBS_CASE(InitializedRead); OBS_CASE(ThreadRead); OBS_CASE(GarbageRead); OBS_CASE(ExitRead);
    OBS_CASE(TlsVectorRead); OBS_CASE(TlsBlockRead); OBS_CASE(TlsRoleRead); OBS_CASE(IdentityAccess);
    OBS_CASE(PackageSlotRead); OBS_CASE(PackageNull); OBS_CASE(PackageFields); OBS_CASE(PackageMembership);
    OBS_CASE(PackageClassFields); OBS_CASE(PackageClassMembership); OBS_CASE(PackageReread); OBS_CASE(PackageDrift);
    OBS_CASE(ThreadUninitialized); OBS_CASE(WrongThread); OBS_CASE(WrongTlsRole);
    OBS_CASE(GarbageCollecting); OBS_CASE(ExitPurge); OBS_CASE(PackageNotRooted);
    OBS_CASE(PackageOuter); OBS_CASE(BetweenSnapshots); OBS_CASE(ObserverException); OBS_CASE(Incomplete);
    }
#undef OBS_CASE
    return "unknown";
}
void format_object(std::ostringstream& out, const ObjectFacts& o)
{
    out << "{ptr=0x" << std::hex << o.object << " class=0x" << o.klass << " outer=0x" << o.outer
        << " name_ids=" << o.name_comparison << ':' << o.name_number
        << " object_flags=0x" << o.object_flags << " item_flags=0x" << o.item_flags << std::dec
        << " index=" << o.index << " serial=" << o.serial << " fields=" << o.fields
        << " membership=" << o.membership << " zero_diagnostic=" << o.diagnostic_zero_serial
        << " live_result=" << static_cast<unsigned>(o.live_result)
        << " zero_result=" << static_cast<unsigned>(o.zero_result) << '}';
}
void format(std::ostringstream& out, const char* label, const Snapshot& s)
{
    out << ' ' << label << "={generation=" << s.session.generation
        << " phase=" << static_cast<unsigned>(s.session.phase) << " widget=0x" << std::hex
        << reinterpret_cast<Address>(s.session.widget) << " caller=0x" << s.caller
        << " expected_caller=0x" << s.expected_caller << std::dec
        << " failure=" << name(classify(s)) << " failure_site=" << s.failure_site
        << " authenticated=" << s.authenticated << " complete=" << s.complete
        << " initialized=" << static_cast<unsigned>(s.thread_initialized)
        << " native_thread=" << s.native_thread << " current_thread=" << s.current_thread
        << " tls_vector=0x" << std::hex << s.tls_vector << " tls_block=0x" << s.tls_block << std::dec
        << " tls_role=" << s.tls_role << " gc=" << s.garbage_collecting
        << " exit_purge=" << static_cast<unsigned>(s.exit_purge) << " package=";
    format_object(out, s.package); out << " package_class="; format_object(out, s.package_class); out << '}';
}
} // namespace

static Failure classify_session_facts(const Snapshot& s) noexcept
{
    if (s.failure != Failure::None) return s.failure;
    if (!s.authenticated) return Failure::UnsupportedImage;
    if (!s.expected_generation || s.session.generation != s.expected_generation
        || s.session.phase != MenuSessionPhase::Ready || !s.session.widget) return Failure::Session;
    return Failure::None;
}
static Failure classify_thread_package_facts(const Snapshot& s) noexcept
{
    if (!s.thread_initialized) return Failure::ThreadUninitialized;
    if (!s.native_thread || s.native_thread != s.current_thread) return Failure::WrongThread;
    if (!s.tls_vector) return Failure::TlsVectorRead;
    if (!s.tls_block) return Failure::TlsBlockRead;
    if (s.tls_role != 2) return Failure::WrongTlsRole;
    if (s.garbage_collecting) return Failure::GarbageCollecting;
    if (s.exit_purge) return Failure::ExitPurge;
    if (!s.package.object) return Failure::PackageNull;
    if (!s.package.fields) return Failure::PackageFields;
    if (!s.package.membership) return Failure::PackageMembership;
    if (!(s.package.item_flags & obj::UObjectIdentityFacts{}.root_set)) return Failure::PackageNotRooted;
    if (s.package.outer) return Failure::PackageOuter;
    if (!s.package_class.fields) return Failure::PackageClassFields;
    if (!s.package_class.membership) return Failure::PackageClassMembership;
    return Failure::None;
}
Failure classify_native_facts(const Snapshot& s) noexcept
{
    perf::Span metric(perf::Metric::FactsClassify);
    const auto failure = classify_session_facts(s);
    if (failure != Failure::None) return failure;
    if (!s.complete) return Failure::Incomplete;
    return classify_thread_package_facts(s);
}
Failure classify_engine_facts(const Snapshot& s) noexcept
{
    if (s.failure != Failure::None) return s.failure;
    if (!s.authenticated) return Failure::UnsupportedImage;
    if (!s.complete) return Failure::Incomplete;
    return classify_thread_package_facts(s);
}
Failure classify(const Snapshot& s) noexcept
{
    const auto failure = classify_session_facts(s);
    if (failure != Failure::None) return failure;
    if (!s.expected_caller || s.caller != s.expected_caller) return Failure::Caller;
    return classify_thread_package_facts(s);
}
Classification classify(const Snapshot& before, const Snapshot& after) noexcept
{
    const bool equal = equal_facts(before, after);
    const bool stable = before.complete && after.complete && equal;
    const auto first = classify(before), second = classify(after);
    const auto failure = first != Failure::None ? first : second != Failure::None ? second
        : stable ? Failure::None : Failure::BetweenSnapshots;
    const auto status = before.failure == Failure::PackageDrift || after.failure == Failure::PackageDrift
        || (before.authenticated && after.authenticated && !equal) ? Status::Drift
        : stable && failure == Failure::None ? Status::Observed : Status::Unavailable;
    return {status, failure, stable};
}
static MenuSessionSnapshot facts_session_snapshot() noexcept
{
    perf::Span metric(perf::Metric::FactsSessionSnapshot);
    return capture_ready_menu_session();
}
static bool facts_identity_access(obj::UObjectIdentityAccess& access) noexcept
{
    perf::Span metric(perf::Metric::FactsIdentityAccess);
    return capture_current_uobject_identity_access(access);
}
static Snapshot capture_impl(HMODULE module, std::string_view profile, std::uint64_t generation,
    Address caller, bool open_provenance = true, bool require_session = true)
{
    Snapshot s{}; s.expected_generation = generation; s.caller = caller;
    s.current_thread = GetCurrentThreadId();
    s.session = facts_session_snapshot(); // existing synchronized copy, no observer ownership
    obj::ImageView image{};
    if (open_provenance) {
        if (!authenticate_module(module, profile, s, image)) return s;
    } else {
        static const HMODULE process_image = GetModuleHandleW(nullptr);
        static FactsAuthentication authentication;
        if (!facts_image(authentication, module, process_image, profile, s, image, authenticate_module)) return s;
    }
    if (open_provenance) s.expected_caller = image.base + rva::PianoMenuListOpenCall + 5;
    if (require_session && (!generation || s.session.generation != generation || s.session.phase != MenuSessionPhase::Ready
        || !s.session.widget)) { fail(s, Failure::Session, "ready_generation"); return s; }
    if (open_provenance && caller != s.expected_caller) {
        fail(s, Failure::Caller, "open_return_site"); return s;
    }
    obj::UObjectIdentityAccess access{};
    if (!facts_identity_access(access)) {
        fail(s, Failure::IdentityAccess, "shared_identity_service"); return s;
    }
    capture_reads(image, access, native_tls_vector(), s);
    return s;
}
Snapshot capture(HMODULE module, std::string_view profile, std::uint64_t generation, Address caller) noexcept
{
    try { return capture_impl(module, profile, generation, caller); }
    catch (...) {
        Snapshot s{}; s.expected_generation = generation; s.caller = caller;
        fail(s, Failure::ObserverException, "capture_exception"); return s;
    }
}
Snapshot capture_native_facts(HMODULE module, std::string_view profile, std::uint64_t generation) noexcept
{
    perf::Span metric(perf::Metric::FactsCapture);
    try { return capture_impl(module, profile, generation, 0, false); }
    catch (...) {
        Snapshot s{}; s.expected_generation = generation;
        fail(s, Failure::ObserverException, "capture_exception"); return s;
    }
}
Snapshot capture_engine_facts(HMODULE module, std::string_view profile) noexcept
{
    try { return capture_impl(module, profile, 0, 0, false, false); }
    catch (...) {
        Snapshot s{}; fail(s, Failure::ObserverException, "engine_capture_exception"); return s;
    }
}
void report(const Snapshot& before, const Snapshot& after) noexcept
{
    try {
        const auto c = classify(before, after);
        std::ostringstream out;
        out << "[menu_theme_observation] status=" << (c.status == Status::Observed ? "observed"
            : c.status == Status::Drift ? "drift" : "unavailable")
            << " generation=" << before.expected_generation << " first_failure=" << name(c.first_failure)
            << " stable=" << c.stable
            << " native_mutation=false names_verified=false serial_zero_diagnostic_only=true";
        format(out, "before", before); format(out, "after", after);
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {}
}
} // namespace ff7r::piano::game::menu_theme_observation
