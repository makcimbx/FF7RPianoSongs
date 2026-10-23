// Same-TU private API injection; do not also link pe_image.cpp into this target.
#include "core/pe_image.cpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>

namespace core = ff7r::piano::core;
namespace {
void require(bool ok, const char* message)
{
    if (!ok) { std::cerr << "executable mapping: " << message << '\n'; std::exit(1); }
}
void headers(void* memory, DWORD size)
{
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = sizeof(dos);
    IMAGE_NT_HEADERS nt{}; nt.Signature = IMAGE_NT_SIGNATURE; nt.OptionalHeader.SizeOfImage = size;
    std::memcpy(memory, &dos, sizeof(dos));
    std::memcpy(static_cast<unsigned char*>(memory) + sizeof(dos), &nt, sizeof(nt));
}
struct Fake {
    alignas(4096) std::array<unsigned char, 8192> image{};
    std::array<WIN32_MEMORY_REGION_INFORMATION, 2> regions{};
    std::array<SIZE_T, 2> lengths{sizeof(regions[0]), sizeof(regions[0])};
    std::array<BOOL, 2> region_ok{TRUE, TRUE};
    PSAPI_WORKING_SET_EX_BLOCK page{};
    MEMORY_BASIC_INFORMATION basic{};
    BOOL working_ok = TRUE;
    SIZE_T mapping_length = sizeof(basic);
    bool wrong_page = false;
    void (*during_working_set)(Fake&) = nullptr;
    unsigned region_calls = 0, working_calls = 0, mapping_calls = 0;
    HMODULE module() { return reinterpret_cast<HMODULE>(image.data()); }
    std::uintptr_t address() { return reinterpret_cast<std::uintptr_t>(image.data()) + 4096; }
    Fake()
    {
        headers(image.data(), static_cast<DWORD>(image.size()));
        for (auto& region : regions) {
            region.AllocationBase = module(); region.MappedImage = 1; region.RegionSize = image.size();
            // Neither allocation protection nor aggregate commit proves current page permissions.
            region.AllocationProtect = PAGE_READONLY; region.CommitSize = 0;
        }
        page.Valid = 1; page.Shared = 1; page.Win32Protection = PAGE_EXECUTE_READ;
        basic.AllocationBase = module(); basic.Type = MEM_IMAGE; basic.State = MEM_COMMIT;
        basic.Protect = PAGE_EXECUTE_READ;
    }
    void reset_counts() { region_calls = working_calls = mapping_calls = 0; }
};
Fake* fake = nullptr;
BOOL WINAPI region_query(HANDLE process, const VOID* address, WIN32_MEMORY_INFORMATION_CLASS kind,
    PVOID output, SIZE_T size, PSIZE_T returned)
{
    require(process == GetCurrentProcess() && kind == MemoryRegionInfo
        && reinterpret_cast<std::uintptr_t>(address) == fake->address()
        && size == sizeof(fake->regions[0]), "exact current-process region query");
    const auto index = fake->region_calls++;
    require(index < 2, "bounded region queries");
    std::memcpy(output, &fake->regions[index], size); *returned = fake->lengths[index];
    return fake->region_ok[index];
}
BOOL WINAPI working_query(HANDLE process, PVOID output, DWORD size)
{
    require(process == GetCurrentProcess() && size == sizeof(PSAPI_WORKING_SET_EX_INFORMATION),
        "one current-process working-set entry");
    ++fake->working_calls;
    auto& entry = *static_cast<PSAPI_WORKING_SET_EX_INFORMATION*>(output);
    require(reinterpret_cast<std::uintptr_t>(entry.VirtualAddress) == fake->address(), "exact requested page");
    if (fake->during_working_set) fake->during_working_set(*fake);
    entry.VirtualAttributes = fake->page;
    if (fake->wrong_page) entry.VirtualAddress = nullptr;
    return fake->working_ok;
}
SIZE_T WINAPI mapping_query(LPCVOID address, PMEMORY_BASIC_INFORMATION output, SIZE_T size)
{
    require(reinterpret_cast<std::uintptr_t>(address) == fake->address() && size == sizeof(*output),
        "exact original fallback query");
    ++fake->mapping_calls; *output = fake->basic; return fake->mapping_length;
}
const core::MappingApis injected{region_query, working_query, mapping_query};
void check_policy(Fake& f, bool allow_execute_only, bool expected, unsigned regions, unsigned working, unsigned mappings,
    core::MappingApis apis = injected)
{
    fake = &f; f.reset_counts();
    require(core::executable_image_address(f.module(), f.address(), allow_execute_only, apis) == expected, "injected decision");
    require(f.region_calls == regions && f.working_calls == working && f.mapping_calls == mappings,
        "actual API counts, no hidden VirtualQuery on resident fast path");
}
void check(Fake& f, bool expected, unsigned regions, unsigned working, unsigned mappings,
    core::MappingApis apis = injected)
{
    for (const bool policy : {false, true}) check_policy(f, policy, expected, regions, working, mappings, apis);
}
void deterministic_tests()
{
    { Fake f; check(f, true, 2, 1, 0); f.page.Shared = 0; check(f, true, 2, 1, 0); }
    for (const unsigned reserved : {1u, 5u, 127u}) {
        Fake f; f.page.Shared = 0; f.page.Reserved = reserved;
        if (reserved == 5) require(f.page.Flags == 0x5000201, "reproduce captured resident COW flags");
        check(f, true, 2, 1, 0);
        f.page.ReservedUlong = 0xffffffffu; check(f, true, 2, 1, 0);
        f.page.Valid = 0; check(f, true, 1, 1, 1);
        f.basic.Protect = PAGE_READONLY; check(f, false, 1, 1, 1);
        f.page.Valid = 1; f.page.Bad = 1; check(f, false, 1, 1, 1);
        f.page.Bad = 0; auto apis = injected; apis.region = nullptr;
        check(f, false, 0, 0, 1, apis);
    }
    for (const DWORD protection : {PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY,
        PAGE_EXECUTE_READ | PAGE_NOCACHE, PAGE_EXECUTE_READWRITE | PAGE_WRITECOMBINE}) {
        Fake f; f.page.Win32Protection = protection; check(f, true, 2, 1, 0);
    }
    for (const DWORD protection : {PAGE_NOACCESS, PAGE_READONLY, PAGE_READWRITE, PAGE_WRITECOPY,
        PAGE_EXECUTE_READ | PAGE_GUARD, PAGE_EXECUTE | PAGE_GUARD, PAGE_GUARD}) {
        Fake f; f.page.Win32Protection = protection;
        check(f, false, 1, 1, 0); // A contradictory accepting fallback must never run.
    }
    { Fake f; f.regions[0].MappedImage = 0; f.regions[0].Private = 1; check(f, false, 1, 0, 0); }
    { Fake f; f.regions[0].AllocationBase = f.image.data() + 64; check(f, false, 1, 0, 0); }
    for (unsigned mode = 0; mode < 13; ++mode) {
        Fake f; unsigned regions = 1, working = 1;
        switch (mode) {
        case 0: f.page.Valid = 0; break;
        case 1: f.page.Bad = 1; break;
        case 2: f.page.Win32Protection = 0; break;
        case 3: f.page.Valid = 0; f.page.Reserved = 5; f.page.ReservedUlong = 0xffffffffu; break;
        case 4: f.working_ok = FALSE; break;
        case 5: f.wrong_page = true; break;
        case 6: f.region_ok[0] = FALSE; working = 0; break;
        case 7: --f.lengths[0]; working = 0; break;
        case 8: f.regions[0].RegionSize = 0; working = 0; break;
        case 9: f.region_ok[1] = FALSE; regions = 2; break;
        case 10: --f.lengths[1]; regions = 2; break;
        case 11: f.regions[0].Reserved = 1; working = 0; break;
        case 12: f.regions[0].DirectMapped = 1; working = 0; break;
        }
        check(f, true, regions, working, 1);
        f.basic.Protect = PAGE_READONLY; check(f, false, regions, working, 1);
    }
    { Fake f; auto apis = injected; apis.region = nullptr; check(f, true, 0, 0, 1, apis); }
    { Fake f; auto apis = injected; apis.working_set = nullptr; check(f, true, 0, 0, 1, apis); }
    for (unsigned mode = 0; mode < 6; ++mode) {
        Fake f; auto apis = injected; apis.region = nullptr;
        switch (mode) {
        case 0: f.basic.AllocationBase = f.image.data() + 64; break;
        case 1: f.basic.Type = MEM_PRIVATE; break;
        case 2: f.basic.State = MEM_RESERVE; break;
        case 3: f.basic.Protect = PAGE_EXECUTE_READ | PAGE_GUARD; break;
        case 4: f.basic.Protect = PAGE_NOACCESS; break;
        case 5: f.mapping_length = 0; break;
        }
        check(f, false, 0, 0, 1, apis);
    }
    { Fake f; f.page.Win32Protection = PAGE_EXECUTE; f.basic.Protect = PAGE_EXECUTE;
      check_policy(f, false, false, 1, 1, 0); check_policy(f, true, true, 2, 1, 0);
      check_policy(f, false, false, 1, 1, 0);
      auto apis = injected; apis.region = nullptr;
      check_policy(f, false, false, 0, 0, 1, apis); check_policy(f, true, true, 0, 0, 1, apis);
      check_policy(f, false, false, 0, 0, 1, apis);
      f.basic.Protect |= PAGE_GUARD; check(f, false, 0, 0, 1, apis);
      std::cout << "execute-only false/true/false: resident VirtualQuery=0/0/0; fallback VirtualQuery=1/1/1\n"; }
    // Mutations happen between real query boundaries, never via a retained mapping cache.
    { Fake f; f.during_working_set = [](Fake& s) { s.page.Win32Protection = PAGE_READONLY; };
      check(f, false, 1, 1, 0); }
    { Fake f; f.during_working_set = [](Fake& s) { s.regions[1].MappedImage = 0; s.regions[1].Private = 1; };
      check(f, false, 2, 1, 0); }
    { Fake f; f.regions[1].AllocationBase = f.image.data() + 64; check(f, false, 2, 1, 0); }
    { Fake f; f.regions[1].RegionSize -= 4096; check(f, false, 2, 1, 0); }
    { Fake f; ++f.regions[1].CommitSize; check(f, false, 2, 1, 0); }
    { Fake f; check(f, true, 2, 1, 0); f.page.Win32Protection = PAGE_READONLY; check(f, false, 1, 1, 0);
      f.page.Win32Protection = PAGE_EXECUTE_READ; check(f, true, 2, 1, 0);
      f.regions[0].MappedImage = 0; f.regions[0].Private = 1; check(f, false, 1, 0, 0); }
    { Fake f; fake = &f;
      for (const bool policy : {false, true})
      require(!core::executable_image_address(f.module(), reinterpret_cast<std::uintptr_t>(f.module()) - 1, policy, injected)
        && !core::executable_image_address(f.module(), reinterpret_cast<std::uintptr_t>(f.module()) + f.image.size(), policy, injected)
        && !core::executable_image_address(nullptr, f.address(), policy, injected)
        && !core::executable_image_address(reinterpret_cast<HMODULE>(1), f.address(), policy, injected)
        && f.region_calls == 0 && f.working_calls == 0 && f.mapping_calls == 0, "fresh PE bounds before APIs"); }
    std::cout << "injected resident/COW: region=2 working-set=1 VirtualQuery=0; fallback and mutation cases passed\n";
}

// Dedicated image page; its harmless RET is executed only after all protection tests restore it.
#pragma section(".xprobe", read, execute)
__declspec(allocate(".xprobe")) __declspec(align(4096)) unsigned char image_probe[8192] = {0xc3, 0x77};
#pragma section(".xlarge", read, execute)
__declspec(allocate(".xlarge")) __declspec(align(4096)) unsigned char large_image_probe[16 * 1024 * 1024] = {0xc3, 0x77};
struct Protection {
    void* page; SIZE_T bytes; DWORD old = 0;
    Protection(void* p, DWORD protection, SIZE_T size = 4096) : page(p), bytes(size)
    { require(VirtualProtect(page, bytes, protection, &old) != FALSE, "change isolated image probe protection"); }
    ~Protection() { DWORD ignored = 0; require(VirtualProtect(page, bytes, old, &ignored) != FALSE, "restore image protection"); }
};
bool legacy(HMODULE module, std::uintptr_t address, bool allow_execute_only) noexcept
{
    __try {
        const auto image = core::image_range(module);
        const auto base = reinterpret_cast<std::uintptr_t>(image.base);
        if (!module || address < base || address - base >= image.size) return false;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info))
            || info.AllocationBase != module || info.Type != MEM_IMAGE || info.State != MEM_COMMIT
            || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto p = info.Protect & 0xff;
        return p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY
            || (allow_execute_only && p == PAGE_EXECUTE);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned real_regions = 0, real_working = 0, real_mappings = 0;
struct RegionEvidence {
    BOOL ok = FALSE;
    DWORD error = 0;
    SIZE_T returned = 0;
    WIN32_MEMORY_REGION_INFORMATION info{};
};
std::array<RegionEvidence, 2> region_evidence{};
BOOL working_ok = FALSE;
DWORD working_error = 0;
PSAPI_WORKING_SET_EX_INFORMATION working_evidence{};
BOOL WINAPI counted_region(HANDLE process, const VOID* address, WIN32_MEMORY_INFORMATION_CLASS kind,
    PVOID out, SIZE_T size, PSIZE_T returned)
{
    const auto index = real_regions++;
    require(index < region_evidence.size() && size == sizeof(region_evidence[0].info), "real region API shape");
    auto& evidence = region_evidence[index];
    evidence.ok = core::mapping_apis().region(process, address, kind, out, size, returned);
    evidence.error = evidence.ok ? ERROR_SUCCESS : GetLastError();
    evidence.returned = *returned;
    evidence.info = *static_cast<WIN32_MEMORY_REGION_INFORMATION*>(out);
    return evidence.ok;
}
BOOL WINAPI counted_working(HANDLE process, PVOID out, DWORD size)
{
    ++real_working;
    require(size == sizeof(working_evidence), "real working-set API shape");
    working_ok = core::mapping_apis().working_set(process, out, size);
    working_error = working_ok ? ERROR_SUCCESS : GetLastError();
    working_evidence = *static_cast<PSAPI_WORKING_SET_EX_INFORMATION*>(out);
    return working_ok;
}
SIZE_T WINAPI counted_mapping(LPCVOID address, PMEMORY_BASIC_INFORMATION out, SIZE_T size)
{ ++real_mappings; return VirtualQuery(address, out, size); }
void observe_real(const char* label, HMODULE module, std::uintptr_t address, const core::MappingApis& apis)
{
    real_regions = real_working = real_mappings = 0;
    region_evidence = {}; working_ok = FALSE; working_error = 0; working_evidence = {};
    const auto accepted = core::executable_image_address(module, address, false, apis);
    std::cout << label << ": region=" << real_regions << " working-set=" << real_working << " VirtualQuery=" << real_mappings << '\n';
    for (unsigned i = 0; i < real_regions; ++i) {
        const auto& e = region_evidence[i];
        std::cout << " region[" << i << "] ok=" << e.ok << " error=" << e.error
            << " bytes=" << e.returned << '/' << sizeof(e.info) << " allocation=" << e.info.AllocationBase
            << " flags=0x" << std::hex << e.info.Flags << " allocation-protect=0x" << e.info.AllocationProtect
            << std::dec << " size=" << e.info.RegionSize << " commit=" << e.info.CommitSize << '\n';
    }
    const auto& p = working_evidence.VirtualAttributes;
    std::cout << " WS ok=" << working_ok << " error=" << working_error << " address=" << working_evidence.VirtualAddress
        << " flags=0x" << std::hex << p.Flags << " protection=0x" << p.Win32Protection << std::dec
        << " Valid=" << p.Valid << " Bad=" << p.Bad << " Shared=" << p.Shared
        << " Reserved=" << p.Reserved << " ReservedUlong=" << p.ReservedUlong << '\n';
    require(accepted == legacy(module, address, false), "real counted query matches original");
}
__declspec(noinline) void execute_image_probe(void* page)
{
    using Probe = void (*)();
    Probe volatile probe = reinterpret_cast<Probe>(page);
    probe();
}
void benchmark_image(const char* label, HMODULE module, void* probe, SIZE_T span, const core::MappingApis& apis)
{
    const auto address = reinterpret_cast<std::uintptr_t>(probe);
    MEMORY_BASIC_INFORMATION info{};
    require(VirtualQuery(probe, &info, sizeof(info)) == sizeof(info)
        && info.AllocationBase == module && info.Type == MEM_IMAGE && info.State == MEM_COMMIT
        && info.Protect == PAGE_EXECUTE_READ, "benchmark has expected current image owner/type/protection");
    const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    require(address >= base && address - base <= info.RegionSize
        && span <= info.RegionSize - (address - base), "benchmark has a contiguous executable protection run");
    std::cout << label << " normalized span=" << span << " VQ.RegionSize=" << info.RegionSize
        << " run-bytes=" << info.RegionSize - (address - base) << " owner=" << info.AllocationBase
        << " expected-owner=" << module << " type=0x" << std::hex << info.Type << " state=0x" << info.State
        << " protection=0x" << info.Protect << std::dec << '\n';
    require(legacy(module, address, false), "benchmark executable probe admitted before execution");
    execute_image_probe(probe);
    observe_real(label, module, address, apis);
    if (apis.region && apis.working_set)
        require(real_regions == 2 && real_working == 1 && real_mappings == 0
            && working_evidence.VirtualAttributes.Valid && !working_evidence.VirtualAttributes.Bad
            && !working_evidence.VirtualAttributes.Shared
            && working_evidence.VirtualAttributes.Win32Protection == PAGE_EXECUTE_READ,
            "both benchmark pages must demonstrate resident COW fast acceptance");
    else std::cout << "SKIP " << label << " fast acceptance: optional API unavailable\n";
    constexpr unsigned repeats = 2000;
    const auto measure = [&](auto query) {
        unsigned accepted = 0;
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < repeats; ++i) accepted += query(module, address, false) ? 1u : 0u;
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        require(accepted == repeats, "benchmark decisions remain true");
        return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    };
    const auto original_ns = measure(legacy);
    const auto shared_ns = measure(core::is_executable_image_address);
    std::cout << label << " informational clock, " << repeats << " queries: original=" << original_ns
        << "ns shared=" << shared_ns << "ns; no timing pass threshold\n";
}
void real_tests()
{
    const auto module = GetModuleHandleW(nullptr);
    const auto address = reinterpret_cast<std::uintptr_t>(image_probe);
    volatile const unsigned char warm = *static_cast<volatile unsigned char*>(image_probe); (void)warm;
    for (const DWORD protection : {PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY,
        PAGE_READONLY, PAGE_READWRITE, PAGE_NOACCESS, PAGE_EXECUTE, PAGE_EXECUTE_READ | PAGE_GUARD}) {
        Protection restore(image_probe, protection);
        for (const bool policy : {false, true}) {
            const auto old = legacy(module, address, policy);
            require(core::is_executable_image_address(module, address, policy) == old, "real isolated image page equals original policy");
        }
    }
    {
        Protection restore(image_probe, PAGE_EXECUTE_WRITECOPY);
        auto* cow = static_cast<volatile unsigned char*>(image_probe);
        cow[1] = static_cast<unsigned char>(cow[1] ^ 1u); // Own data page, force actual COW.
        for (const bool policy : {false, true})
            require(legacy(module, address, policy) && core::is_executable_image_address(module, address, policy), "real COW image remains executable");
        PSAPI_WORKING_SET_EX_INFORMATION page{}; page.VirtualAddress = image_probe;
        if (core::mapping_apis().working_set
            && core::mapping_apis().working_set(GetCurrentProcess(), &page, sizeof(page)) && page.VirtualAttributes.Valid)
            require(!page.VirtualAttributes.Shared, "real image probe is copy-on-write private");
        cow[1] = static_cast<unsigned char>(cow[1] ^ 1u);
    }
    const auto allocation = VirtualAlloc(nullptr, 8192, MEM_RESERVE, PAGE_NOACCESS);
    require(allocation != nullptr, "reserve isolated private allocation");
    require(VirtualAlloc(allocation, 4096, MEM_COMMIT, PAGE_READWRITE) == allocation, "commit private fake PE headers");
    headers(allocation, 8192);
    const auto private_module = reinterpret_cast<HMODULE>(allocation);
    const auto target = reinterpret_cast<std::uintptr_t>(allocation) + 4096;
    for (const bool policy : {false, true})
        require(!legacy(private_module, target, policy) && !core::is_executable_image_address(private_module, target, policy), "reserved page rejected");
    require(VirtualAlloc(reinterpret_cast<void*>(target), 4096, MEM_COMMIT, PAGE_EXECUTE_READWRITE) != nullptr, "commit private executable page");
    for (const bool policy : {false, true})
        require(!legacy(private_module, target, policy) && !core::is_executable_image_address(private_module, target, policy), "private executable page is not image");
    require(VirtualFree(reinterpret_cast<void*>(target), 4096, MEM_DECOMMIT) != FALSE, "decommit isolated target");
    for (const bool policy : {false, true})
        require(!legacy(private_module, target, policy) && !core::is_executable_image_address(private_module, target, policy), "decommitted page rejected");
    require(VirtualFree(allocation, 0, MEM_RELEASE) != FALSE, "release isolated allocation");
    for (const bool policy : {false, true})
        require(!legacy(private_module, target, policy) && !core::is_executable_image_address(private_module, target, policy), "released headers/target fail closed");

    auto apis = core::mapping_apis();
    if (apis.region) apis.region = counted_region;
    if (apis.working_set) apis.working_set = counted_working;
    apis.mapping = counted_mapping;
    observe_real("pre-execution image query", module, address, apis);
    require(legacy(module, address, false), "guard/noaccess tests restored executable probe before call");
    execute_image_probe(static_cast<void*>(image_probe));
    observe_real("warm executed image query", module, address, apis);
    if (apis.region && apis.working_set)
        require(real_regions == 2 && real_working == 1 && real_mappings == 0,
            "available APIs must demonstrate real warm image fast acceptance");
    else std::cout << "SKIP real fast acceptance: optional region=" << (apis.region != nullptr)
        << " working-set=" << (apis.working_set != nullptr) << "; original fallback still tested\n";
    {
        Protection boundary(image_probe + 4096, PAGE_READONLY);
        Protection normalize(image_probe, PAGE_EXECUTE_READ);
        benchmark_image("tiny run", module, image_probe, 4096, apis);
    }
    MEMORY_BASIC_INFORMATION original_large{};
    const auto large_address = reinterpret_cast<std::uintptr_t>(large_image_probe);
    require(VirtualQuery(large_image_probe, &original_large, sizeof(original_large)) == sizeof(original_large)
        && original_large.AllocationBase == module && original_large.Type == MEM_IMAGE
        && original_large.State == MEM_COMMIT && original_large.Protect == PAGE_EXECUTE_READ
        && reinterpret_cast<std::uintptr_t>(original_large.BaseAddress) <= large_address
        && large_address - reinterpret_cast<std::uintptr_t>(original_large.BaseAddress) <= original_large.RegionSize
        && sizeof(large_image_probe) <= original_large.RegionSize
            - (large_address - reinterpret_cast<std::uintptr_t>(original_large.BaseAddress)),
        "dedicated large section has uniform original protection for full-span restoration");
    {
        Protection normalize(large_image_probe, PAGE_EXECUTE_READ, sizeof(large_image_probe));
        {
            Protection copy_on_write(large_image_probe, PAGE_EXECUTE_WRITECOPY);
            auto* cow = static_cast<volatile unsigned char*>(large_image_probe);
            cow[1] = static_cast<unsigned char>(cow[1] ^ 1u);
            cow[1] = static_cast<unsigned char>(cow[1] ^ 1u);
        }
        benchmark_image("large run", module, large_image_probe, sizeof(large_image_probe), apis);
    }
}
} // namespace
int main()
{
    deterministic_tests(); real_tests();
    std::cout << "executable mapping selftest passed\n";
}
