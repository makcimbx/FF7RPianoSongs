#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ff7r::piano::core {

struct ImageRange {
    const uint8_t* base = nullptr;
    size_t size = 0;
};

struct PeIdentity {
    uint32_t timestamp = 0;
    uint32_t size_of_image = 0;
    uint32_t checksum = 0;
};

PeIdentity pe_identity(HMODULE module);
uint32_t pe_timestamp(HMODULE module);
ImageRange image_range(HMODULE module);
// Fresh PE bounds/mapping/current page protection, not callback or lifetime authority.
// Optional resident-page queries fall back to the original VirtualQuery predicate.
// Callers explicitly retain their existing execute-only acceptance policy.
bool is_executable_image_address(HMODULE module, std::uintptr_t address, bool allow_execute_only) noexcept;
bool bytes_equal(const uint8_t* address, const std::vector<uint8_t>& expected);
bool safe_copy_bytes(const void* source, void* dest, size_t size);
bool safe_write_bytes(void* dest, const void* source, size_t size);

template <typename T>
bool safe_read_field(void* base, const uintptr_t offset, T& out)
{
    if (!base) {
        return false;
    }
    __try {
        out = *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(base) + offset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool safe_write_field(void* base, const uintptr_t offset, const T& value)
{
    if (!base) {
        return false;
    }
    __try {
        *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + offset) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace ff7r::piano::core
