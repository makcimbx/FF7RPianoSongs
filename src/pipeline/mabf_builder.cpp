#include "mabf_builder.h"

#include "mabf_scaffold.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace ff7rp::pipeline {
namespace {

constexpr std::size_t kContainerSizeOffset = 0x0c;
constexpr std::size_t kSlot0RelativeOffset = 0x20;
constexpr std::size_t kSlotRelativeOffsetBase = kMabfSlot0Offset - kSlot0RelativeOffset;
constexpr std::size_t kSlot1RelativeOffsetField = 0x424;
constexpr std::size_t kSlot2RelativeOffsetField = 0x428;
constexpr std::size_t kHcaPayloadSizeField = 0x448;
constexpr std::size_t kScaffoldTrailerPrefixSize = 16;
constexpr std::size_t kTrailerPayloadSizeFieldInMetadata = 0x16;

static_assert(ff7r::piano::pipeline::mabf_scaffold::kTrailers[0].size() ==
    kScaffoldTrailerPrefixSize + kMabfTrailerMetadataSize);

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::uint16_t read_u16_be(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::uint32_t read_u32_be(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
        static_cast<std::uint32_t>(bytes[offset + 3]);
}

void write_u32_le(std::uint8_t* bytes, const std::size_t offset, const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::uint16_t hca_crc16(const std::uint8_t* data, const std::size_t size) {
    std::uint16_t crc = 0;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= static_cast<std::uint16_t>(data[index] << 8u);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0
                ? static_cast<std::uint16_t>((crc << 1u) ^ 0x8005u)
                : static_cast<std::uint16_t>(crc << 1u);
        }
    }
    return crc;
}

bool valid_hca_crc(const std::uint8_t* bytes, const std::size_t size) {
    if (size < 2) return false;
    const std::uint16_t stored = static_cast<std::uint16_t>((bytes[size - 2] << 8u) | bytes[size - 1]);
    return stored == hca_crc16(bytes, size - 2);
}

bool checked_add_size(const std::size_t left, const std::size_t right, std::size_t* out) {
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    *out = left + right;
    return true;
}

} // namespace

MabfBuildResult build_mabf_from_mode_hca(const MabfModeHcaPayloads& mode_hca) {
    MabfBuildResult result;
    result.release_valid = false;
    result.used_placeholder_scaffold = false;

    std::array<std::size_t, 3> payload_sizes{};
    std::array<std::size_t, 3> slot_sizes{};
    for (std::size_t mode = 0; mode < mode_hca.size(); ++mode) {
        if (!mode_hca[mode]) {
            result.status = Status::error(StatusCode::InvalidArgument,
                "Mode" + std::to_string(mode) + " HCA must not be null");
            return result;
        }
        const auto& hca = *mode_hca[mode];
        if (hca.size() < kMabfHcaHeaderSize || hca[0] != 'H' || hca[1] != 'C' || hca[2] != 'A' || hca[3] != 0) {
            result.status = Status::error(StatusCode::InvalidArgument,
                "Mode" + std::to_string(mode) + " HCA is missing a valid header");
            return result;
        }
        const std::size_t data_offset = read_u16_be(hca, 6);
        const std::size_t frame_count = read_u32_be(hca, 16);
        const std::size_t block_size = read_u16_be(hca, 28);
        if (data_offset != kMabfHcaHeaderSize || frame_count == 0 || block_size == 0 ||
            frame_count > (std::numeric_limits<std::size_t>::max() - data_offset) / block_size ||
            data_offset + frame_count * block_size != hca.size()) {
            result.status = Status::error(StatusCode::InvalidArgument,
                "Mode" + std::to_string(mode) + " HCA size does not match its frame metadata");
            return result;
        }
        if (mode > 0 && frame_count != read_u32_be(*mode_hca[0], 16)) {
            result.status = Status::error(StatusCode::InvalidArgument,
                "adaptive MABF mode HCAs must have identical frame counts");
            return result;
        }
        payload_sizes[mode] = hca.size() - kMabfHcaHeaderSize;
        slot_sizes[mode] = mabf_slot_size(hca.size());
        if (mode < 2u && mabf_slot_padding(hca.size()) < kMabfMode01MinimumPrefixSize) {
            result.status = Status::error(StatusCode::InvalidArgument,
                "Mode0/1 MABF prefix is too short for the native 01 00 marker");
            return result;
        }
    }

    if (slot_sizes[0] > std::numeric_limits<std::size_t>::max() - kMabfHeaderSize ||
        slot_sizes[1] > std::numeric_limits<std::size_t>::max() - kMabfHeaderSize - slot_sizes[0] ||
        slot_sizes[2] > std::numeric_limits<std::size_t>::max() - kMabfHeaderSize - slot_sizes[0] - slot_sizes[1]) {
        result.status = Status::error(StatusCode::InvalidArgument, "MABF size overflows addressable memory");
        return result;
    }
    const std::size_t mabf_size = kMabfHeaderSize + slot_sizes[0] + slot_sizes[1] + slot_sizes[2];
    if (mabf_size > kMaxMabfBytes) {
        result.status = Status::error(StatusCode::InvalidArgument, "MABF exceeds the 64 MiB safety limit");
        return result;
    }
    const std::size_t slot1_relative = kSlot0RelativeOffset + slot_sizes[0];
    const std::size_t slot2_relative = slot1_relative + slot_sizes[1];
    if (mabf_size < 0x30 || mabf_size - 0x30 > std::numeric_limits<std::uint32_t>::max() ||
        payload_sizes[0] > std::numeric_limits<std::uint32_t>::max() ||
        payload_sizes[1] > std::numeric_limits<std::uint32_t>::max() ||
        payload_sizes[2] > std::numeric_limits<std::uint32_t>::max() ||
        slot2_relative > std::numeric_limits<std::uint32_t>::max()) {
        result.status = Status::error(StatusCode::InvalidArgument, "MABF metadata exceeds 32-bit container fields");
        return result;
    }

    result.bytes.assign(mabf_size, 0);
    std::memcpy(result.bytes.data(), ff7r::piano::pipeline::mabf_scaffold::kHeader.data(), kMabfHeaderSize);
    write_u32_le(result.bytes.data(), kContainerSizeOffset, static_cast<std::uint32_t>(mabf_size - 0x30));
    write_u32_le(result.bytes.data(), kSlot1RelativeOffsetField, static_cast<std::uint32_t>(slot1_relative));
    write_u32_le(result.bytes.data(), kSlot2RelativeOffsetField, static_cast<std::uint32_t>(slot2_relative));
    write_u32_le(result.bytes.data(), kHcaPayloadSizeField, static_cast<std::uint32_t>(payload_sizes[0]));

    std::size_t offset = kMabfSlot0Offset;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& hca = *mode_hca[slot];
        std::memcpy(result.bytes.data() + offset, hca.data(), hca.size());
        auto trailer = ff7r::piano::pipeline::mabf_scaffold::kTrailers[slot];
        const std::size_t prefix_size = mabf_slot_padding(hca.size());
        if (slot < 2) {
            write_u32_le(trailer.data() + kScaffoldTrailerPrefixSize, kTrailerPayloadSizeFieldInMetadata,
                static_cast<std::uint32_t>(payload_sizes[slot + 1]));
        }
        std::memcpy(result.bytes.data() + offset + hca.size(),
            trailer.data() + kScaffoldTrailerPrefixSize - prefix_size, prefix_size);
        std::memcpy(result.bytes.data() + offset + hca.size() + prefix_size,
            trailer.data() + kScaffoldTrailerPrefixSize, kMabfTrailerMetadataSize);
        offset += slot_sizes[slot];
    }

    result.status = Status::ok_status();
    result.release_valid = true;
    result.validation_note = "Three-mode MABF assembled from explicit per-mode HCA payloads and the proven bgm09 scaffold.";
    return result;
}

namespace {

Status validate_release_mabf(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t expected_logical_source_frames,
    const MabfResolvedModePolicy* mode_policy,
    MabfArtifactMetadata* out_metadata) {
    if (!out_metadata) {
        return Status::error(StatusCode::InvalidArgument, "MABF metadata output must not be null");
    }
    if (bytes.size() < kMabfHeaderSize + 3u * (kMabfHcaHeaderSize + kMabfTrailerMetadataSize) ||
        bytes.size() > kMaxMabfBytes) {
        return Status::error(StatusCode::MabfNotReleaseValid, "MABF size is outside release bounds");
    }

    std::vector<std::uint8_t> expected_header(
        ff7r::piano::pipeline::mabf_scaffold::kHeader.begin(),
        ff7r::piano::pipeline::mabf_scaffold::kHeader.end());
    for (const std::size_t offset : {kContainerSizeOffset, kSlot1RelativeOffsetField,
             kSlot2RelativeOffsetField, kHcaPayloadSizeField}) {
        std::memcpy(expected_header.data() + offset, bytes.data() + offset, sizeof(std::uint32_t));
    }
    if (!std::equal(expected_header.begin(), expected_header.end(), bytes.begin()) ||
        read_u32_le(bytes, kContainerSizeOffset) + 0x30ull != bytes.size()) {
        return Status::error(StatusCode::MabfNotReleaseValid, "MABF scaffold or container size is invalid");
    }

    std::array<std::size_t, 3> offsets{kMabfSlot0Offset, 0, 0};
    if (!checked_add_size(kSlotRelativeOffsetBase,
            static_cast<std::size_t>(read_u32_le(bytes, kSlot1RelativeOffsetField)), &offsets[1]) ||
        !checked_add_size(kSlotRelativeOffsetBase,
            static_cast<std::size_t>(read_u32_le(bytes, kSlot2RelativeOffsetField)), &offsets[2])) {
        return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode boundaries are invalid");
    }

    struct ModeRange {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::size_t hca_size = 0;
    };
    std::array<ModeRange, 3> mode_ranges{};
    constexpr std::size_t kMinimumModeSize = kMabfHcaHeaderSize + kMabfTrailerMetadataSize;
    for (std::size_t mode = 0; mode < mode_ranges.size(); ++mode) {
        const std::size_t begin = offsets[mode];
        const std::size_t end = mode + 1u < offsets.size() ? offsets[mode + 1u] : bytes.size();
        if (begin > bytes.size() || end > bytes.size() || begin > end || end - begin < kMinimumModeSize) {
            return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode boundaries are invalid");
        }
        if (begin + kMabfHcaHeaderSize > end) {
            return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode HCA header is truncated");
        }
        const std::uint32_t declared_frames = read_u32_be(bytes, begin + 16);
        const std::uint16_t declared_block_size = read_u16_be(bytes, begin + 28);
        if (declared_frames == 0 || declared_block_size == 0 ||
            declared_frames > (std::numeric_limits<std::size_t>::max() - kMabfHcaHeaderSize) /
                declared_block_size) {
            return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode HCA size metadata is invalid");
        }
        const std::size_t hca_size = kMabfHcaHeaderSize +
            static_cast<std::size_t>(declared_frames) * declared_block_size;
        if (hca_size > end - begin || end - begin != mabf_slot_size(hca_size)) {
            return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode is not native 16-byte aligned");
        }
        mode_ranges[mode] = ModeRange{begin, end, hca_size};
    }

    MabfArtifactMetadata metadata;
    metadata.byte_count = bytes.size();
    metadata.logical_source_frames = expected_logical_source_frames;
    std::size_t first_hca_size = 0;
    std::array<std::size_t, 3> hca_sizes{};
    for (std::size_t mode = 0; mode < mode_ranges.size(); ++mode) {
        const std::size_t begin = mode_ranges[mode].begin;
        const std::size_t hca_size = mode_ranges[mode].hca_size;
        const std::size_t prefix_size = mabf_slot_padding(hca_size);
        hca_sizes[mode] = hca_size;
        const std::uint8_t* hca = bytes.data() + begin;
        auto expected_trailer = ff7r::piano::pipeline::mabf_scaffold::kTrailers[mode];
        if (mode < 2u && prefix_size < kMabfMode01MinimumPrefixSize) {
            return Status::error(StatusCode::MabfNotReleaseValid,
                "MABF Mode0/1 prefix is too short for the native 01 00 marker");
        }
        if (!std::equal(expected_trailer.begin() + kScaffoldTrailerPrefixSize - prefix_size,
                expected_trailer.begin() + kScaffoldTrailerPrefixSize, bytes.begin() + begin + hca_size)) {
            return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode prefix is invalid");
        }
        if (std::memcmp(hca, "HCA\0", 4) != 0 || std::memcmp(hca + 8, "fmt\0", 4) != 0 ||
            std::memcmp(hca + 24, "comp", 4) != 0 || read_u16_be(bytes, begin + 6) != kMabfHcaHeaderSize) {
            return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode HCA header is invalid");
        }
        const std::uint16_t channels = hca[12];
        const std::uint32_t sample_rate = (static_cast<std::uint32_t>(hca[13]) << 16u) |
            (static_cast<std::uint32_t>(hca[14]) << 8u) | hca[15];
        const std::uint32_t frames = read_u32_be(bytes, begin + 16);
        const std::uint16_t inserted = read_u16_be(bytes, begin + 20);
        const std::uint16_t appended = read_u16_be(bytes, begin + 22);
        const std::uint16_t block_size = read_u16_be(bytes, begin + 28);
        const std::uint64_t logical_frames = static_cast<std::uint64_t>(frames) * 1024u - inserted - appended;
        if (channels != 2 || sample_rate != 48000 || inserted != 128 || block_size != 682 || frames == 0 ||
            kMabfHcaHeaderSize + static_cast<std::uint64_t>(frames) * block_size != hca_size ||
            static_cast<std::uint64_t>(frames) * 1024u < inserted + appended ||
            (expected_logical_source_frames != 0 && logical_frames != expected_logical_source_frames) ||
            (mode != 0u && logical_frames != metadata.logical_source_frames) ||
            !valid_hca_crc(hca, kMabfHcaHeaderSize)) {
            return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode HCA metadata or header CRC is invalid");
        }
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            if (!valid_hca_crc(hca + kMabfHcaHeaderSize + static_cast<std::size_t>(frame) * block_size,
                    block_size)) {
                return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode HCA frame CRC is invalid");
            }
        }
        if (mode < 2u) {
            write_u32_le(expected_trailer.data() + kScaffoldTrailerPrefixSize, kTrailerPayloadSizeFieldInMetadata,
                static_cast<std::uint32_t>(mode_ranges[mode + 1u].hca_size - kMabfHcaHeaderSize));
        }
        if (!std::equal(expected_trailer.begin() + kScaffoldTrailerPrefixSize, expected_trailer.end(),
                bytes.begin() + begin + hca_size + prefix_size)) {
            return Status::error(StatusCode::MabfNotReleaseValid, "MABF mode metadata suffix is invalid");
        }
        if (mode == 0u) {
            first_hca_size = hca_size;
            metadata.logical_source_frames = static_cast<std::size_t>(logical_frames);
            metadata.hca_frame_count = frames;
            metadata.sample_rate = sample_rate;
            metadata.channels = channels;
            metadata.inserted_samples = inserted;
            metadata.appended_samples = appended;
            metadata.block_size = block_size;
        }
    }
    const auto modes_equal = [&](const std::size_t left, const std::size_t right) {
        return hca_sizes[left] == hca_sizes[right] &&
            std::equal(bytes.begin() + offsets[left], bytes.begin() + offsets[left] + hca_sizes[left],
                bytes.begin() + offsets[right]);
    };
    if (mode_policy) {
        if (mode_policy->resolved_authored_indices[0] != 0u ||
            (mode_policy->resolved_authored_indices[1] != 0u &&
                mode_policy->resolved_authored_indices[1] != 1u) ||
            (mode_policy->resolved_authored_indices[2] != 0u &&
                mode_policy->resolved_authored_indices[2] != 2u)) {
            return Status::error(StatusCode::InvalidArgument, "resolved MABF mode policy is invalid");
        }
        for (std::size_t left = 0; left < 3u; ++left) {
            for (std::size_t right = left + 1u; right < 3u; ++right) {
                const bool guide_pair = mode_policy->mode0_guide && left == 0u;
                if (!guide_pair && mode_policy->resolved_authored_indices[left] ==
                        mode_policy->resolved_authored_indices[right] && !modes_equal(left, right)) {
                    return Status::error(StatusCode::MabfNotReleaseValid,
                        "MABF modes resolving to the same authored source must have byte-identical HCA payloads");
                }
            }
        }
    }
    if (read_u32_le(bytes, kHcaPayloadSizeField) + kMabfHcaHeaderSize != first_hca_size) {
        return Status::error(StatusCode::MabfNotReleaseValid, "MABF primary HCA payload size is invalid");
    }
    *out_metadata = metadata;
    return Status::ok_status();
}

} // namespace

Status validate_resolved_mabf(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t expected_logical_source_frames,
    const MabfResolvedModePolicy& policy,
    MabfArtifactMetadata* out_metadata) {
    return validate_release_mabf(bytes, expected_logical_source_frames, &policy, out_metadata);
}

Status validate_structural_mabf(
    const std::vector<std::uint8_t>& bytes,
    MabfArtifactMetadata* out_metadata) {
    return validate_release_mabf(bytes, 0, nullptr, out_metadata);
}

} // namespace ff7rp::pipeline
