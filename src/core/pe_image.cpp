#include "core/pe_image.h"

#include <cstring>

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
