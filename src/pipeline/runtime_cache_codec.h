#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "song_types.h"
#include "native_asset_capabilities.h"

namespace ff7rp::pipeline {

bool encode_runtime_cache(
    const LoadedSong& song,
    NativeAssetCapabilities native_assets,
    std::span<const char, 8> magic,
    std::uint32_t format,
    std::vector<std::uint8_t>* bytes);

bool decode_runtime_cache(
    std::span<const std::uint8_t> bytes,
    NativeAssetCapabilities native_assets,
    std::span<const char, 8> magic,
    std::uint32_t format,
    LoadedSong* song);

bool song_configs_equal(const SongConfig& left, const SongConfig& right);
std::uint64_t config_chart_semantic_hash(const SongConfig& config, const CompiledChart& chart);
std::uint64_t profile_semantic_hash(const LoadedDifficultyProfile& profile);
std::uint64_t omission_semantic_hash(const DifficultyProfileOmission& omission);

} // namespace ff7rp::pipeline
