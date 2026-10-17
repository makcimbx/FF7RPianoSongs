#pragma once

#include "song_types.h"

#include <array>
#include <filesystem>

namespace ff7rp::pipeline {
class OodleDecoder;

using NativeIoChunkId = std::array<std::uint8_t, 12>;

Status extract_native_chunk(const std::vector<std::uint8_t>& toc,
    const std::vector<std::uint8_t>& cas, const NativeIoChunkId& chunk,
    std::vector<std::uint8_t>& output, const OodleDecoder* decoder=nullptr);
Status read_native_chunk(const std::filesystem::path& toc_path,
    const NativeIoChunkId& chunk, std::vector<std::uint8_t>& output,
    const OodleDecoder* decoder=nullptr);

// Read-only, bounded UE4 DirectoryIndex-version source extraction. Stored
// blocks only: encrypted/signed/partitioned containers and compressed chunks
// fail closed. This is not template provenance or package/content validation.
Status extract_native_stored_chunk(const std::vector<std::uint8_t>& toc,
    const std::vector<std::uint8_t>& cas, const NativeIoChunkId& chunk,
    std::vector<std::uint8_t>& output);

// Streams only the requested bounded ranges, never loads an entire game UCAS.
// Output is cleared on every failure. Does not write or mount anything.
Status read_native_stored_chunk(const std::filesystem::path& toc_path,
    const NativeIoChunkId& chunk, std::vector<std::uint8_t>& output);

} // namespace ff7rp::pipeline
