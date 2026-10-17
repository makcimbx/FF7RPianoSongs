#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline {

// This extractor accepts independent IoStore blocks, not arbitrary Oodle streams.
inline constexpr std::size_t kOodleMaximumBlockBytes = 64u * 1024u;

namespace detail {
// retoc 234cd0d8 Cargo.lock -> repak 71d06644, oodle_loader/src/lib.rs.
using OodleDecompressFunction = std::intptr_t (
#ifdef _WIN32
    __stdcall
#endif
    *)(const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t,
        std::uint32_t, std::uint32_t, std::uint32_t, std::uint64_t, std::size_t,
        std::uint64_t, std::uint64_t, std::uint8_t*, std::size_t, std::uint32_t);

// Isolated calling seam; function must be trusted and remain alive for this call.
Status decode_oodle_block(OodleDecompressFunction function,
    std::span<const std::uint8_t> input, std::size_t decoded_bytes,
    std::vector<std::uint8_t>* output);
} // namespace detail

class OodleDecoder {
public:
    // Explicit absolute companion path only. No search, download or installation.
    // Failed load leaves decoder unavailable; copies keep the authenticated module alive.
    // Publish copies after loading; do not reload/reset the same object concurrently.
    static Status load(const std::filesystem::path& companion, OodleDecoder* decoder);
    // Caller must verify the exact function ABI/build/code and keep it alive through
    // every copy/call. This factory authenticates nothing and acquires no module lease.
    // Copies share wrapper state only; failed creation leaves decoder unavailable.
    static Status from_verified_function(detail::OodleDecompressFunction function, OodleDecoder* decoder);
    bool available() const { return static_cast<bool>(module_); }
    Status decompress(std::span<const std::uint8_t> input, std::size_t decoded_bytes,
        std::vector<std::uint8_t>* output) const;

private:
    struct Module;
    std::shared_ptr<const Module> module_;
};

} // namespace ff7rp::pipeline
