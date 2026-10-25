// Same-TU seam: no game process, native calls, hooks or engine allocation.
#include "game/menu_theme_observation.cpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace obs = ff7r::piano::game::menu_theme_observation;
namespace obj = ff7r::piano::game::uobject_locator_core;
namespace game = ff7r::piano::game;
namespace core = ff7r::piano::core;
namespace rv = ff7r::piano::game::rva;
using Address = obj::Address;
namespace {
void require(bool ok, const char* message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
struct Fixture {
    static constexpr Address base = 0x1000000000, registry = 0x2000000000,
        chunks = registry + 0x100, items = registry + 0x200,
        package = registry + 0x1000, klass = registry + 0x1100,
        tls_vector = registry + 0x2000, tls_block = registry + 0x3000;
    std::map<Address, std::uint8_t> bytes;
    std::array<game::RvaSignatureSpec, 5> specs{};
    std::string missing;
    Address blocked = 0;
    bool replace_on_reread = false, unreadable_reread = false;
    unsigned reads = 0, slot_reads = 0, code_reads = 0, spec_lookups = 0, identity_captures = 0, session_captures = 0;
    game::MenuSessionSnapshot session{};
    template<class T> void put(Address p, const T& value)
    {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[p + i] = raw[i];
    }
    void code(std::size_t index)
    {
        for (std::size_t i = 0; i < specs[index].expected_prologue.size(); ++i)
            bytes[base + specs[index].rva + i] = specs[index].expected_prologue[i];
    }
    void displacement(std::size_t index, std::size_t offset, Address end, Address target)
    {
        const auto delta = static_cast<std::int32_t>(static_cast<std::int64_t>(target)
            - static_cast<std::int64_t>(specs[index].rva + end));
        std::memcpy(specs[index].expected_prologue.data() + offset, &delta, sizeof(delta));
    }
    Fixture()
    {
        for (std::size_t i = 0; i < obs::sites.size(); ++i) {
            specs[i] = {obs::sites[i].id, obs::sites[i].rva,
                std::vector<std::uint8_t>(obs::sites[i].size, 0x90)};
        }
        displacement(0, 3, 7, rv::NativeGameThreadInitialized);
        displacement(0, 9, 13, rv::NativeGameThreadId);
        displacement(1, 2, 6, rv::NativeGarbageCollecting);
        displacement(2, 2, 7, rv::NativeUObjectExitPurge);
        displacement(3, 3, 7, rv::NativeGameThreadInitialized);
        std::copy(obs::tls_tail.begin(), obs::tls_tail.end(), specs[3].expected_prologue.begin() + 45);
        for (std::size_t i = 0; i < specs.size(); ++i) code(i);
        put(base + rv::NativeGameThreadInitialized, std::uint8_t{1});
        put(base + rv::NativeGameThreadId, std::uint32_t{42});
        put(base + rv::NativeGarbageCollecting, std::uint32_t{0});
        put(base + rv::NativeUObjectExitPurge, std::uint8_t{0});
        put(base + rv::NativeTransientPackage, package);
        put(tls_vector, tls_block); put(tls_block + 0x398, std::uint32_t{2});
        put(registry + 0x10, chunks); put(registry + 0x20, std::int32_t{2});
        put(registry + 0x24, std::int32_t{2}); put(registry + 0x28, std::int32_t{1});
        put(registry + 0x2c, std::int32_t{1}); put(chunks, items);
        for (std::int32_t i = 0; i < 2; ++i) {
            const auto object = i == 0 ? package : klass;
            put(items + i * 0x18, object); put(items + i * 0x18 + 8, std::int32_t{1 << 30});
            put(items + i * 0x18 + 0x10, std::int32_t{5 + i});
            put(object + 8, std::uint32_t{0}); put(object + 0xc, i);
            put(object + 0x10, klass); put(object + 0x18, std::uint32_t{100 + static_cast<unsigned>(i)});
            put(object + 0x1c, std::uint32_t{9}); put(object + 0x20, Address{0});
        }
        session.generation = 7; session.phase = game::MenuSessionPhase::Ready;
        session.widget = reinterpret_cast<void*>(registry + 0x4000);
        session.widget_identity = {3, 10};
    }
    obj::MemoryReader reader()
    {
        return {this, [](void* context, Address p, void* to, std::size_t n) noexcept {
            auto& f = *static_cast<Fixture*>(context); ++f.reads;
            for (const auto& spec : f.specs)
                if (p >= base + spec.rva && p - (base + spec.rva) < spec.expected_prologue.size()) {
                    ++f.code_reads; break;
                }
            if (f.blocked >= p && f.blocked - p < n) return false;
            if (p == base + rv::NativeTransientPackage && ++f.slot_reads == 2) {
                if (f.unreadable_reread) return false;
                if (f.replace_on_reread) {
                    if (n != sizeof(Address)) return false;
                    const auto replacement = klass; std::memcpy(to, &replacement, n); return true;
                }
            }
            auto* out = static_cast<std::uint8_t*>(to);
            for (std::size_t i = 0; i < n; ++i) {
                const auto it = f.bytes.find(p + i); if (it == f.bytes.end()) return false;
                out[i] = it->second;
            }
            return true;
        }};
    }
    obj::ImageView image() { return {base, 0x20000000, reader()}; }
    obj::UObjectIdentityAccess access() { return {reader(), {}, registry, {}}; }
};
Fixture* fixture = nullptr;
std::vector<std::string> logs;
obs::Snapshot snapshot(Address tls = Fixture::tls_vector, bool open_provenance = true)
{
    obs::Snapshot s{}; s.session = fixture->session; s.expected_generation = 7;
    s.current_thread = 42;
    if (open_provenance) s.caller = s.expected_caller = Fixture::base + rv::PianoMenuListOpenCall + 5;
    require(obs::authenticate(fixture->image(), FF7RP_TARGET_BUILD_ID, true, s), "fixture authentication");
    fixture->slot_reads = 0;
    obs::capture_reads(fixture->image(), fixture->access(), tls, s);
    return s;
}
} // namespace

namespace ff7r::piano::game {
const RvaSignatureSpec* find_rva_signature(std::string_view id)
{
    if (!fixture || id == fixture->missing) return nullptr;
    ++fixture->spec_lookups;
    for (const auto& spec : fixture->specs) if (spec.id == id) return &spec;
    return nullptr;
}
MenuSessionSnapshot capture_ready_menu_session() noexcept { ++fixture->session_captures; return fixture->session; }
bool capture_current_uobject_identity_access(obj::UObjectIdentityAccess& out) noexcept
{
    ++fixture->identity_captures;
    out = fixture->access(); return true;
}
} // namespace ff7r::piano::game
namespace ff7r::piano::core {
void log(LogLevel, const std::string& message) { logs.push_back(message); }
} // namespace ff7r::piano::core

namespace {
template<class Capture> void metrics_neutrality(Capture&& capture, bool complete)
{
    namespace perf = game::menu_theme_metrics;
    auto& f = *fixture;
    const auto counts = [&] { return std::array{f.reads, f.code_reads, f.spec_lookups, f.identity_captures, f.session_captures}; };
    perf::enable(false);
    perf::Snapshot metrics{};
    while (perf::take_snapshot(metrics)) {}
    const auto before = counts();
    const auto plain = capture(); const auto plain_failure = obs::classify_native_facts(plain);
    const auto middle = counts();
    obs::Snapshot measured{}; obs::Failure measured_failure{};
    perf::enable(true);
    {
        perf::Span root(perf::Metric::QualifyCallback);
        measured = capture(); measured_failure = obs::classify_native_facts(measured);
    }
    perf::enable(false);
    const auto after = counts();
    require(obs::equal_facts(plain, measured) && std::string_view(plain.failure_site) == measured.failure_site
        && plain_failure == measured_failure, "metrics preserve every copied facts field and classification");
    for (std::size_t i = 0; i < before.size(); ++i)
        require(middle[i] - before[i] == after[i] - middle[i], "metrics add no memory reads, authentication or service calls");
    require(perf::take_snapshot(metrics) && !metrics.dropped, "facts metrics root captured without drops");
    const auto stat = [&](perf::Metric metric) { return metrics.stats[static_cast<std::size_t>(metric)].count; };
    require(stat(perf::Metric::QualifyCallback) == 1 && stat(perf::Metric::FactsClassify) == 1
        && stat(perf::Metric::FactsSessionSnapshot) == 1
        && stat(perf::Metric::FactsCapture) == (complete ? 0u : 1u)
        && stat(perf::Metric::FactsIdentityAccess) == (complete ? 1u : 0u)
        && stat(perf::Metric::FactsDynamicReads) == (complete ? 1u : 0u)
        && stat(perf::Metric::FactsObjectSnapshot) == (complete ? 4u : 0u),
        "production facts scopes count actual calls, including consistency rereads and early rejection");
    require(metrics.counters[static_cast<std::size_t>(perf::Counter::MalformedScopes)] == 0
        && metrics.counters[static_cast<std::size_t>(perf::Counter::DepthOverflow)] == 0,
        "facts metric nesting balances");
    for (std::size_t i = 0; i < metrics.stats.size(); ++i)
        if (std::string_view(perf::name(static_cast<perf::Metric>(i))).starts_with("Native"))
            require(metrics.stats[i].count == 0, "read-only facts never invoke native helpers");
    std::cout << "facts metrics " << (complete ? "actual-reader" : "exported rejection")
        << ": reads " << middle[0] - before[0] << " -> " << after[0] - middle[0] << "; native helpers 0 -> 0\n";
}
void facts_authentication_tests()
{
    Fixture f; fixture = &f;
    const auto module = reinterpret_cast<HMODULE>(Fixture::base);
    obs::FactsAuthentication cache;
    unsigned initializations = 0;
    bool supported = true, maps_valid = true;
    auto initialize = [&](HMODULE, std::string_view profile, obs::Snapshot& s, obj::ImageView& image) {
        ++initializations;
        image = f.image();
        if (!maps_valid) { obs::fail(s, obs::Failure::Mapping, "test_mapping"); return false; }
        return obs::authenticate(image, profile, supported, s);
    };
    auto capture = [&] {
        obs::Snapshot s{};
        s.expected_generation = 7;
        s.session = obs::facts_session_snapshot();
        s.current_thread = 42;
        obj::ImageView image{};
        if (!obs::facts_image(cache, module, module, FF7RP_TARGET_BUILD_ID, s, image, initialize)) return s;
        obj::UObjectIdentityAccess access{};
        require(obs::facts_identity_access(access), "fresh identity service");
        f.slot_reads = 0;
        obs::capture_reads(image, access, Fixture::tls_vector, s);
        return s;
    };
    require(obs::classify_native_facts(capture()) == obs::Failure::None, "first authenticated facts");
    const auto code_reads = f.code_reads, lookups = f.spec_lookups, reads = f.reads;
    require(code_reads == 10 && lookups == 5 && initializations == 1,
        "one complete five-signature/five-RIP authentication");
    for (unsigned i = 0; i < 20; ++i)
        require(obs::classify_native_facts(capture()) == obs::Failure::None, "reused immutable contract");
    require(initializations == 1 && f.code_reads == code_reads && f.spec_lookups == lookups
        && f.reads > reads && f.identity_captures == 21,
        "hot facts repeat dynamic reads/service capture, not immutable authentication");
    metrics_neutrality(capture, true);
    metrics_neutrality([&] { return obs::capture_native_facts(GetModuleHandleW(nullptr), "unsupported", 7); }, false);

    f.put(Fixture::base + rv::NativeGarbageCollecting, std::uint32_t{0x10000});
    require(obs::classify_native_facts(capture()) == obs::Failure::GarbageCollecting, "fresh GC");
    f.put(Fixture::base + rv::NativeGarbageCollecting, std::uint32_t{0});
    f.put(Fixture::base + rv::NativeUObjectExitPurge, std::uint8_t{1});
    require(obs::classify_native_facts(capture()) == obs::Failure::ExitPurge, "fresh purge");
    f.put(Fixture::base + rv::NativeUObjectExitPurge, std::uint8_t{0});
    f.put(Fixture::tls_block + 0x398, std::uint32_t{3});
    require(obs::classify_native_facts(capture()) == obs::Failure::WrongTlsRole, "fresh TLS role");
    f.put(Fixture::tls_block + 0x398, std::uint32_t{2});
    f.blocked = Fixture::tls_vector;
    require(obs::classify_native_facts(capture()) == obs::Failure::TlsBlockRead, "fresh TLS pointer read");
    f.blocked = 0;
    f.put(Fixture::base + rv::NativeGameThreadInitialized, std::uint8_t{0});
    require(obs::classify_native_facts(capture()) == obs::Failure::ThreadUninitialized, "fresh initialized");
    f.put(Fixture::base + rv::NativeGameThreadInitialized, std::uint8_t{1});
    f.put(Fixture::base + rv::NativeGameThreadId, std::uint32_t{43});
    require(obs::classify_native_facts(capture()) == obs::Failure::WrongThread, "fresh native thread");
    f.put(Fixture::base + rv::NativeGameThreadId, std::uint32_t{42});
    f.session.generation = 8;
    require(obs::classify_native_facts(capture()) == obs::Failure::Session, "fresh generation");
    f.session.generation = 7; f.session.phase = game::MenuSessionPhase::Ending;
    require(obs::classify_native_facts(capture()) == obs::Failure::Session, "fresh ending phase");
    f.session.phase = game::MenuSessionPhase::Ready;
    f.put(Fixture::items + 8, std::int32_t{0});
    require(obs::classify_native_facts(capture()) == obs::Failure::PackageNotRooted, "fresh package root");
    f.put(Fixture::items + 8, std::int32_t{1 << 28});
    require(obs::classify_native_facts(capture()) == obs::Failure::PackageMembership, "fresh dead membership");
    f.put(Fixture::items + 8, std::int32_t{1 << 30});
    f.put(Fixture::items + 0x10, std::int32_t{0});
    require(capture().package.diagnostic_zero_serial, "fresh positive to diagnostic zero identity");
    f.replace_on_reread = true;
    require(obs::classify_native_facts(capture()) == obs::Failure::PackageDrift, "fresh global reread");
    f.replace_on_reread = false;
    f.put(Fixture::base + rv::NativeTransientPackage, Address{0});
    require(obs::classify_native_facts(capture()) == obs::Failure::PackageNull, "fresh null package");
    f.put(Fixture::base + rv::NativeTransientPackage, Fixture::package);
    require(f.code_reads == code_reads && f.spec_lookups == lookups && initializations == 1,
        "unsafe dynamic values never trigger or bypass immutable authentication");

    for (const auto bad_profile : {false, true}) {
        obs::Snapshot s{}; obj::ImageView image{};
        require(!obs::facts_image(cache, bad_profile ? module : reinterpret_cast<HMODULE>(Fixture::base + 1),
            module, bad_profile ? "unsupported-profile" : FF7RP_TARGET_BUILD_ID, s, image, initialize)
            && s.failure == obs::Failure::UnsupportedImage && !s.authenticated && initializations == 1,
            "unsupported module/profile cannot reuse valid entry");
    }
    // Open observations still independently read signatures/RIP and retain exact caller attribution.
    auto open = snapshot();
    require(obs::classify(open) == obs::Failure::None && f.code_reads == code_reads + 10,
        "open observation authentication remains independent");

    obs::FactsAuthentication rejected;
    auto rejected_capture = [&] {
        obs::Snapshot s{}; obj::ImageView image{};
        obs::facts_image(rejected, module, module, FF7RP_TARGET_BUILD_ID, s, image, initialize);
        return s;
    };
    supported = false;
    require(rejected_capture().failure == obs::Failure::UnsupportedImage && !rejected.module,
        "unsupported executable never published");
    supported = true; maps_valid = false;
    require(rejected_capture().failure == obs::Failure::Mapping && !rejected.module, "bad maps not published");
    maps_valid = true; f.missing = f.specs[2].id;
    require(rejected_capture().failure == obs::Failure::MissingSpec && !rejected.module, "missing spec not published");
    f.missing.clear(); f.bytes[Fixture::base + f.specs[2].rva] ^= 1;
    require(rejected_capture().failure == obs::Failure::Signature && !rejected.module, "bad prefix not published");
    f.code(2);
    f.blocked = Fixture::base + f.specs[1].rva;
    require(rejected_capture().failure == obs::Failure::Mapping && !rejected.module, "unsafe code read not published");
    f.blocked = 0;
    f.displacement(2, 2, 7, rv::NativeUObjectExitPurge + 1); f.code(2);
    require(rejected_capture().failure == obs::Failure::RipReference && !rejected.module, "partial RIP auth not published");
    f.displacement(2, 2, 7, rv::NativeUObjectExitPurge); f.code(2);
    {
        obs::Snapshot s{}; obj::ImageView image{};
        auto wrong_base = [&](HMODULE m, std::string_view p, obs::Snapshot& result, obj::ImageView& candidate) {
            if (!initialize(m, p, result, candidate)) return false;
            ++candidate.base; return true;
        };
        require(!obs::facts_image(rejected, module, module, FF7RP_TARGET_BUILD_ID, s, image, wrong_base)
            && s.failure == obs::Failure::Mapping && !s.authenticated && !rejected.module,
            "authentication cannot publish a different image base");
    }
    require(rejected_capture().authenticated && rejected.module == module, "complete retry can publish");
    // Hot readers don't contend on initialization; incomplete publication fails closed.
    obs::Snapshot busy{};
    {
        std::lock_guard lock(cache.mutex);
        std::thread contender([&] {
            obj::ImageView image{};
            obs::facts_image(cache, module, module, FF7RP_TARGET_BUILD_ID, busy, image, initialize);
        });
        contender.join();
    }
    require(busy.authenticated && busy.failure == obs::Failure::None,
        "published immutable contract reused without the initialization mutex");
    obs::FactsAuthentication pending;
    busy = {};
    {
        std::lock_guard lock(pending.mutex);
        std::thread contender([&] {
            obj::ImageView image{};
            obs::facts_image(pending, module, module, FF7RP_TARGET_BUILD_ID, busy, image, initialize);
        });
        contender.join();
    }
    require(busy.failure == obs::Failure::AuthenticationBusy && !busy.authenticated,
        "concurrent capture cannot see or wait for partial authentication");
    fixture = nullptr;
}
} // namespace

int main()
{
    using obs::Failure;
    using obs::Status;
    facts_authentication_tests();
    Fixture good; fixture = &good;
    auto healthy = snapshot();
    require(obs::classify(healthy) == Failure::None && healthy.complete
        && healthy.package.membership && !healthy.package.diagnostic_zero_serial, "positive-serial projection");
    require(obs::classify(healthy, healthy).status == Status::Observed, "stable healthy observation only");
    auto facts = snapshot(Fixture::tls_vector, false);
    require(obs::classify_native_facts(facts) == Failure::None
        && obs::classify(facts) == Failure::Caller
        && obs::classify(facts, facts).status == Status::Unavailable,
        "native facts never manufacture callback provenance");
    facts.complete = false;
    require(obs::classify_native_facts(facts) == Failure::Incomplete, "incomplete facts rejected");
    facts = healthy; facts.caller = facts.expected_caller = 0;
    facts.session.phase = game::MenuSessionPhase::Closing;
    require(obs::classify_native_facts(facts) == Failure::Session, "facts require current Ready session");
    facts.session={}; facts.expected_generation=0;
    require(obs::classify_engine_facts(facts)==Failure::None
        && obs::classify_native_facts(facts)==Failure::Session,
        "gameplay engine facts do not manufacture Ready-menu authority");
    facts.garbage_collecting=1;
    require(obs::classify_engine_facts(facts)==Failure::GarbageCollecting,"gameplay still rejects GC");
    facts.garbage_collecting=0; facts.native_thread=99;
    require(obs::classify_engine_facts(facts)==Failure::WrongThread,"gameplay still requires actual engine game thread");
    facts.native_thread=facts.current_thread; facts.complete=false;
    require(obs::classify_engine_facts(facts)==Failure::Incomplete,"gameplay cannot use an incomplete capture");
    good.put(Fixture::items + 0x10, std::int32_t{0});
    const auto zero = snapshot();
    require(obs::classify(zero) == Failure::None && zero.package.diagnostic_zero_serial
        && zero.package.name_number == 9, "zero serial and full raw name IDs diagnostic only");
    require(obs::classify(healthy, zero).status == Status::Drift, "serial drift retained");
    const auto reads = good.reads;
    obs::report(zero, zero);
    require(good.reads == reads && logs.size() == 1
        && logs[0].find("status=observed") != std::string::npos
        && logs[0].find("native_mutation=false names_verified=false") != std::string::npos,
        "one copied-facts-only non-authoritative event");
    require(obs::capture(GetModuleHandleW(nullptr), "unsupported", 7, 0).failure == Failure::UnsupportedImage
        && good.reads == reads, "unsupported production profile never reads globals");
    const auto unsupported_facts = obs::capture_native_facts(GetModuleHandleW(nullptr), "unsupported", 7);
    require(unsupported_facts.failure == Failure::UnsupportedImage && !unsupported_facts.caller
        && !unsupported_facts.expected_caller && good.reads == reads, "facts unsupported profile no reads/provenance");

    const auto authentication_failure = [](Fixture& f, Failure expected, bool supported = true) {
        fixture = &f; obs::Snapshot s{};
        require(!obs::authenticate(f.image(), FF7RP_TARGET_BUILD_ID, supported, s)
            && s.failure == expected, "authentication first failure");
    };
    Fixture unsupported; authentication_failure(unsupported, Failure::UnsupportedImage, false);
    Fixture missing; missing.missing = "native_uobject_thread_role_site";
    authentication_failure(missing, Failure::MissingSpec);
    Fixture mismatch; mismatch.bytes[Fixture::base + rv::NativeGcFlagExchangeSite] ^= 0xff;
    authentication_failure(mismatch, Failure::Signature);
    Fixture wrong_rva; ++wrong_rva.specs[0].rva; authentication_failure(wrong_rva, Failure::Signature);
    Fixture unreadable_site; unreadable_site.blocked = Fixture::base + rv::NativeExitPurgePublishSite;
    authentication_failure(unreadable_site, Failure::Mapping);
    Fixture wrong_rip; wrong_rip.specs[0].expected_prologue[3] ^= 1; wrong_rip.code(0);
    authentication_failure(wrong_rip, Failure::RipReference);
    Fixture wrong_tail; wrong_tail.specs[3].expected_prologue[64] = 3; wrong_tail.code(3);
    authentication_failure(wrong_tail, Failure::TlsContract);

    const auto value_failure = [&](auto change, Failure expected) {
        auto s = healthy; change(s); require(obs::classify(s) == expected, "value classifier failure");
    };
    value_failure([](auto& s) { s.session.phase = game::MenuSessionPhase::Ending; }, Failure::Session);
    value_failure([](auto& s) { ++s.session.generation; }, Failure::Session);
    value_failure([](auto& s) { ++s.caller; }, Failure::Caller);
    value_failure([](auto& s) { s.thread_initialized = 0; }, Failure::ThreadUninitialized);
    value_failure([](auto& s) { ++s.native_thread; }, Failure::WrongThread);
    value_failure([](auto& s) { s.tls_role = 1; }, Failure::WrongTlsRole);
    value_failure([](auto& s) { s.garbage_collecting = 0x100; }, Failure::GarbageCollecting);
    value_failure([](auto& s) { s.exit_purge = 1; }, Failure::ExitPurge);
    value_failure([](auto& s) { s.package.item_flags = 0; }, Failure::PackageNotRooted);

    const auto capture_failure = [](Fixture& f, Failure expected, Address tls = Fixture::tls_vector) {
        fixture = &f; const auto s = snapshot(tls);
        require(obs::classify(s) == expected, "fake-reader capture failure");
        auto facts = s; facts.caller = facts.expected_caller = 0;
        require(obs::classify_native_facts(facts) == expected, "shared native fact failure"); return s;
    };
    Fixture no_vector; capture_failure(no_vector, Failure::TlsVectorRead, 0);
    Fixture no_block; no_block.put(Fixture::tls_vector, Address{0}); capture_failure(no_block, Failure::TlsBlockRead);
    Fixture tls_unreadable; tls_unreadable.blocked = Fixture::tls_block + 0x398;
    capture_failure(tls_unreadable, Failure::TlsRoleRead);
    Fixture global_unreadable; global_unreadable.blocked = Fixture::base + rv::NativeGameThreadInitialized;
    capture_failure(global_unreadable, Failure::InitializedRead);
    Fixture thread_unreadable; thread_unreadable.blocked = Fixture::base + rv::NativeGameThreadId;
    capture_failure(thread_unreadable, Failure::ThreadRead);
    Fixture gc_unreadable; gc_unreadable.blocked = Fixture::base + rv::NativeGarbageCollecting;
    capture_failure(gc_unreadable, Failure::GarbageRead);
    Fixture exit_unreadable; exit_unreadable.blocked = Fixture::base + rv::NativeUObjectExitPurge;
    capture_failure(exit_unreadable, Failure::ExitRead);
    Fixture slot_unreadable; slot_unreadable.blocked = Fixture::base + rv::NativeTransientPackage;
    capture_failure(slot_unreadable, Failure::PackageSlotRead);
    Fixture null_package; null_package.put(Fixture::base + rv::NativeTransientPackage, Address{0});
    capture_failure(null_package, Failure::PackageNull);
    Fixture dead; dead.put(Fixture::items + 8, std::int32_t{1 << 28});
    auto dead_sample = capture_failure(dead, Failure::PackageMembership);
    require(dead_sample.package.item_flags == (1 << 28)
        && dead_sample.package.live_result == obj::UObjectLiveHandleCaptureResult::ItemFlagsDead,
        "dead item raw reason retained");
    Fixture unrooted; unrooted.put(Fixture::items + 8, std::int32_t{0});
    capture_failure(unrooted, Failure::PackageNotRooted);
    Fixture destroyed; destroyed.put(Fixture::package + 8, std::uint32_t{0x8000});
    capture_failure(destroyed, Failure::PackageMembership);
    Fixture fields; fields.blocked = Fixture::package + 0x18;
    capture_failure(fields, Failure::PackageFields);
    Fixture replacement; replacement.replace_on_reread = true;
    const auto replaced = capture_failure(replacement, Failure::PackageDrift);
    require(obs::classify(replaced, replaced).status == Status::Drift,
        "within-capture package drift never reported as merely unavailable");
    Fixture reread; reread.unreadable_reread = true;
    capture_failure(reread, Failure::PackageReread);
    Fixture between; fixture = &between; const auto before = snapshot();
    between.put(Fixture::base + rv::NativeTransientPackage, Fixture::klass);
    const auto after = snapshot();
    require(obs::classify(after) == Failure::None && obs::classify(before, after).status == Status::Drift,
        "healthy replacement still before/after drift");
    auto gc_after = healthy; gc_after.garbage_collecting = 1;
    const auto result = obs::classify(healthy, gc_after);
    require(result.status == Status::Drift && result.first_failure == Failure::GarbageCollecting,
        "before/after unsafe fact has precise failure");
    auto ending = healthy; ending.session.phase = game::MenuSessionPhase::Ending;
    require(obs::classify(healthy, ending).status == Status::Drift
        && obs::classify(healthy, ending).first_failure == Failure::Session,
        "session ending between snapshots is diagnostic drift");
    std::cout << "menu_theme_observation_selftest passed (synthetic reads only)\n";
}
