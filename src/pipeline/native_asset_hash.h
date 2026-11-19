#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ff7rp::pipeline {
// trumank/retoc at the pin in native_iostore_source.cpp: src/name_map.rs
// writes FNAME_HASH_ALGORITHM_ID for split NameMaps, including empty maps.
inline constexpr std::uint64_t kNativeNameMapHashAlgorithmId = 0xc1640000;

// UE4 name/package hashing and IoStore's BLAKE3 chunk digest (not cache admission).
std::uint64_t native_cityhash64(std::span<const std::uint8_t> bytes);
std::uint64_t native_name_hash(std::string_view ascii, bool utf16 = false);
std::array<std::uint8_t, 32> native_blake3(std::span<const std::uint8_t> bytes);
std::array<std::uint8_t, 32> native_sha256(std::span<const std::uint8_t> bytes);
} // namespace ff7rp::pipeline
