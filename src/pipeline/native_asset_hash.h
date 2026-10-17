#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ff7rp::pipeline {
// UE4 name/package hashing and IoStore's BLAKE3 chunk digest (not cache admission).
std::uint64_t native_cityhash64(std::span<const std::uint8_t> bytes);
std::uint64_t native_name_hash(std::string_view ascii, bool utf16 = false);
std::array<std::uint8_t, 32> native_blake3(std::span<const std::uint8_t> bytes);
std::array<std::uint8_t, 32> native_sha256(std::span<const std::uint8_t> bytes);
} // namespace ff7rp::pipeline
