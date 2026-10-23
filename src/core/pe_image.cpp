#include "core/pe_image.h"

#include <cstring>
#include <psapi.h>

namespace ff7r::piano::core {

PeIdentity pe_identity(HMODULE module)
{
    if (!module) {
        return {};
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return {};
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return {};
    }
    return {nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage, nt->OptionalHeader.CheckSum};
}

uint32_t pe_timestamp(HMODULE module)
{
    return pe_identity(module).timestamp;
}

ImageRange image_range(HMODULE module)
{
    ImageRange range;
    if (!module) {
        return range;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return range;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return range;
    }
    range.base = reinterpret_cast<const uint8_t*>(module);
    range.size = nt->OptionalHeader.SizeOfImage;
    return range;
}

namespace {
using RegionQuery = BOOL (WINAPI*)(HANDLE, const VOID*, WIN32_MEMORY_INFORMATION_CLASS, PVOID, SIZE_T, PSIZE_T);
using WorkingSetQuery = BOOL (WINAPI*)(HANDLE, PVOID, DWORD);
using MappingQuery = SIZE_T (WINAPI*)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
struct MappingApis {
    RegionQuery region = nullptr;
    WorkingSetQuery working_set = nullptr;
    MappingQuery mapping = &VirtualQuery;
};
FARPROC mapping_export(const char* name) noexcept
{
    for (const auto name_module : {L"kernelbase.dll", L"kernel32.dll"}) {
        if (const auto module = GetModuleHandleW(name_module))
            if (const auto proc = GetProcAddress(module, name)) return proc;
    }
    return nullptr;
}
const MappingApis& mapping_apis() noexcept
{
    // Process-lifetime system modules only; no mapping/protection results are cached.
    static const MappingApis apis{
        reinterpret_cast<RegionQuery>(mapping_export("QueryVirtualMemoryInformation")),
        reinterpret_cast<WorkingSetQuery>(mapping_export("K32QueryWorkingSetEx")), &VirtualQuery};
    return apis;
}
bool executable_protection(DWORD protection, bool allow_execute_only) noexcept
{
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    const auto p = protection & 0xff;
    return p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY
        || (allow_execute_only && p == PAGE_EXECUTE);
}
bool original_executable_mapping(HMODULE module, std::uintptr_t address, bool allow_execute_only, MappingQuery query) noexcept
{
    MEMORY_BASIC_INFORMATION info{};
    return query(reinterpret_cast<LPCVOID>(address), &info, sizeof(info))
        && info.AllocationBase == module && info.Type == MEM_IMAGE && info.State == MEM_COMMIT
        && executable_protection(info.Protect, allow_execute_only);
}
enum class MappingDecision { Fallback, Reject, Accept };
MappingDecision region_decision(const WIN32_MEMORY_REGION_INFORMATION& info,
    HMODULE module) noexcept
{
    if (info.AllocationBase != module) return MappingDecision::Reject;
    if (info.Private || info.MappedDataFile || info.MappedPageFile || info.MappedPhysical)
        return MappingDecision::Reject;
    if (!info.MappedImage || info.DirectMapped || info.Reserved || !info.RegionSize) return MappingDecision::Fallback;
    return MappingDecision::Accept;
}
bool query_region(const MappingApis& apis, std::uintptr_t address,
    WIN32_MEMORY_REGION_INFORMATION& info) noexcept
{
    SIZE_T returned = 0;
    return apis.region(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), MemoryRegionInfo,
        &info, sizeof(info), &returned) && returned == sizeof(info);
}
MappingDecision resident_executable_mapping(HMODULE module, std::uintptr_t address, bool allow_execute_only,
    const MappingApis& apis) noexcept
{
    if (!apis.region || !apis.working_set) return MappingDecision::Fallback;
    WIN32_MEMORY_REGION_INFORMATION before{};
    if (!query_region(apis, address, before)) return MappingDecision::Fallback;
    const auto first = region_decision(before, module);
    if (first != MappingDecision::Accept) return first;
    PSAPI_WORKING_SET_EX_INFORMATION page{};
    page.VirtualAddress = reinterpret_cast<PVOID>(address);
    if (!apis.working_set(GetCurrentProcess(), &page, sizeof(page))
        || page.VirtualAddress != reinterpret_cast<PVOID>(address)
        || !page.VirtualAttributes.Valid || page.VirtualAttributes.Bad)
        return MappingDecision::Fallback;
    // PSAPI documents Reserved/ReservedUlong as reserved, not zero-valued validity conditions.
    const auto protection = static_cast<DWORD>(page.VirtualAttributes.Win32Protection);
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return MappingDecision::Reject;
    const auto p = protection & 0xff;
    if (p != PAGE_NOACCESS && p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY
        && p != PAGE_EXECUTE && p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY)
        return MappingDecision::Fallback;
    if (!executable_protection(protection, allow_execute_only)) return MappingDecision::Reject;
    WIN32_MEMORY_REGION_INFORMATION after{};
    if (!query_region(apis, address, after)) return MappingDecision::Fallback;
    const auto last = region_decision(after, module);
    if (last != MappingDecision::Accept) return last;
    // Not atomic: detect observable allocation changes, never retain a page proof.
    // Shared=0 is legal for an image's copy-on-write page.
    if (before.AllocationBase != after.AllocationBase || before.Flags != after.Flags
        || before.RegionSize != after.RegionSize || before.AllocationProtect != after.AllocationProtect
        || before.CommitSize != after.CommitSize) return MappingDecision::Reject;
    return MappingDecision::Accept;
}
bool executable_image_address(HMODULE module, std::uintptr_t address, bool allow_execute_only, const MappingApis& apis) noexcept
{
    // Borrowed module headers can disappear; contain that failure without touching the target page.
    ImageRange image{};
    __try { image = image_range(module); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(image.base);
    if (!module || address < base || address - base >= image.size) return false;
    const auto decision = resident_executable_mapping(module, address, allow_execute_only, apis);
    return decision == MappingDecision::Accept || (decision == MappingDecision::Fallback
        && original_executable_mapping(module, address, allow_execute_only, apis.mapping));
}
} // namespace

bool is_executable_image_address(HMODULE module, std::uintptr_t address, bool allow_execute_only) noexcept
{
    return executable_image_address(module, address, allow_execute_only, mapping_apis());
}

bool bytes_equal(const uint8_t* address, const std::vector<uint8_t>& expected)
{
    if (!address || expected.empty()) {
        return false;
    }
    __try {
        return std::memcmp(address, expected.data(), expected.size()) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_copy_bytes(const void* source, void* dest, size_t size)
{
    if (!source || !dest || size == 0) {
        return false;
    }
    __try {
        std::memcpy(dest, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_write_bytes(void* dest, const void* source, size_t size)
{
    if (!source || !dest || size == 0) {
        return false;
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(dest, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    bool ok = false;
    __try {
        std::memcpy(dest, source, size);
        FlushInstructionCache(GetCurrentProcess(), dest, size);
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    DWORD ignored = 0;
    VirtualProtect(dest, size, old_protect, &ignored);
    return ok;
}

} // namespace ff7r::piano::core
