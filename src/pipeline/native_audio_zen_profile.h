#pragma once
#include <cstddef>
#include <cstdint>

namespace ff7rp::pipeline::native_audio_zen_profile {

inline constexpr std::size_t kSummaryFlagsOffset = 0x10;
inline constexpr std::uint32_t kUnversionedPropertiesFlag = 1u << 13;

// The four 1.004 stock samples (docs/CurrentStatus.md) have
// editor-filtered, mapped and cooked flags (bits 31, 23, 9), without bit 13.
inline constexpr std::uint32_t kOlderFlagsWithoutUnversionedProperties = 0x80800200;

// Generated Music always needs unversioned properties (bit 13) for its
// serialized body; see analysis/NativeAudioAssetProfile-OlderBuild-20261017.json,
// music.mapped_bank.target_zen_archive.target_flag_rule.
inline constexpr std::uint32_t kOlderFlagsWithUnversionedProperties =
    kOlderFlagsWithoutUnversionedProperties | kUnversionedPropertiesFlag;

} // namespace ff7rp::pipeline::native_audio_zen_profile
