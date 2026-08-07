#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "song_types.h"
#include "pipeline_limits.h"

namespace ff7rp::pipeline {

constexpr std::size_t kMabfHeaderSize = 0x460;
// Native slots end with a mode-specific 1-16 byte prefix followed by 46 bytes
// of metadata. Mode0/1 prefixes have a zero body and 01 00 tail; Mode2 is all
// zero. The complete slot ends on a 16-byte boundary.
constexpr std::size_t kMabfTrailerMetadataSize = 46;
constexpr std::size_t kMabfTrailerSize = kMabfTrailerMetadataSize;
constexpr std::size_t kMabfSlotAlignment = 16;
constexpr std::size_t kMabfMode01MinimumPrefixSize = 2;
constexpr std::size_t kMabfSlot0Offset = 0x460;
constexpr std::size_t kMabfHcaHeaderSize = 96;
constexpr std::size_t kMabfFixedProfileSize = 0x1e66a0;
constexpr std::size_t kMabfFixedProfileHcaSize = 663682;

constexpr std::size_t mabf_slot_padding(const std::size_t hca_size) {
    return kMabfSlotAlignment - ((hca_size + kMabfTrailerMetadataSize) % kMabfSlotAlignment);
}

constexpr std::size_t mabf_slot_size(const std::size_t hca_size) {
    return hca_size + mabf_slot_padding(hca_size) + kMabfTrailerMetadataSize;
}

struct MabfBuildResult {
    Status status;
    std::vector<std::uint8_t> bytes;
    bool release_valid = false;
    bool used_placeholder_scaffold = false;
    std::string validation_note;
};

using MabfModeHcaPayloads = std::array<const std::vector<std::uint8_t>*, 3>;

MabfBuildResult build_mabf_from_mode_hca(const MabfModeHcaPayloads& mode_hca);
Status validate_clean_mabf(
    const std::vector<std::uint8_t>& bytes,
    std::size_t expected_logical_source_frames,
    MabfArtifactMetadata* out_metadata);

Status validate_adaptive_metronome_mabf(
    const std::vector<std::uint8_t>& bytes,
    std::size_t expected_logical_source_frames,
    MabfArtifactMetadata* out_metadata);

// Validates the native three-mode container without assuming any mode payloads are equal.
Status validate_structural_mabf(
    const std::vector<std::uint8_t>& bytes,
    MabfArtifactMetadata* out_metadata);

} // namespace ff7rp::pipeline
