#include "pipeline/mabf_builder.h"

#include "pipeline/cache.h"
#include "pipeline/hca/hca_encoder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace {

// Native-derived fixed-profile trailers. These contain no HCA/audio payload.
constexpr std::array<std::array<std::uint8_t, 62>, 3> kNativeFixedTrailers{{
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x02, 0x07, 0x01, 0x00, 0x80, 0xbb,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00,
      0x00, 0x00, 0x22, 0x20, 0x0a, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x01, 0x10,
      0x60, 0x00, 0xaa, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
      0x00, 0x00}},
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x02, 0x07, 0x02, 0x00, 0x80, 0xbb,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00,
      0x00, 0x00, 0x22, 0x20, 0x0a, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x10,
      0x60, 0x00, 0xaa, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
      0x00, 0x00}},
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9a, 0x3f,
      0xe2, 0x03, 0x7a, 0x43, 0x96, 0x4c, 0xa5, 0x4c, 0xc5, 0xa9, 0x70, 0x0d,
      0x15, 0xbe, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00}},
}};

constexpr std::array<std::array<std::uint8_t, 2>, 3> kNativeTwoBytePrefixes{{
    {{0x01, 0x00}}, {{0x01, 0x00}}, {{0x00, 0x00}},
}};

std::vector<uint8_t> fake_hca(const std::uint32_t frames, const std::uint8_t fill = 0x55)
{
    std::vector<uint8_t> hca(96u + static_cast<std::size_t>(frames) * 682u, fill);
    hca[0] = 'H';
    hca[1] = 'C';
    hca[2] = 'A';
    hca[3] = 0;
    hca[6] = 0;
    hca[7] = 96;
    hca[16] = static_cast<uint8_t>((frames >> 24) & 0xffu);
    hca[17] = static_cast<uint8_t>((frames >> 16) & 0xffu);
    hca[18] = static_cast<uint8_t>((frames >> 8) & 0xffu);
    hca[19] = static_cast<uint8_t>(frames & 0xffu);
    hca[28] = 0x02;
    hca[29] = 0xaa;
    return hca;
}

std::uint32_t read_u32_le(const std::vector<uint8_t>& bytes, const std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void write_u32_le(std::vector<uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

void write_u16_be(std::vector<uint8_t>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>(value & 0xffu);
}

void write_u32_be(std::vector<uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>(value & 0xffu);
}

std::uint16_t hca_crc16(const std::uint8_t* data, const std::size_t size)
{
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

} // namespace

int main()
{
    const std::vector<uint8_t> fixed_hca = fake_hca(973);
    const ff7rp::pipeline::MabfModeHcaPayloads fixed_modes{&fixed_hca, &fixed_hca, &fixed_hca};
    auto result = ff7rp::pipeline::build_mabf_from_mode_hca(fixed_modes);
    if (!result.status.ok()) {
        std::cerr << result.status.message << "\n";
        return 1;
    }
    if (!result.release_valid || result.used_placeholder_scaffold) {
        std::cerr << "MABF builder did not use release scaffold\n";
        return 2;
    }
    if (result.bytes.size() != ff7rp::pipeline::kMabfFixedProfileSize ||
        ff7rp::pipeline::mabf_slot_padding(fixed_hca.size()) != 16u) {
        std::cerr << "Unexpected MABF size\n";
        return 3;
    }
    if (result.bytes[0] != 'm' || result.bytes[1] != 'a' || result.bytes[2] != 'b' || result.bytes[3] != 'f') {
        std::cerr << "Missing MABF magic\n";
        return 4;
    }


    const std::vector<uint8_t> variable_hca = fake_hca(47, 0x11);
    const std::vector<uint8_t> mode1_hca = fake_hca(47, 0x22);
    const std::vector<uint8_t> mode2_hca = fake_hca(47, 0x33);
    const ff7rp::pipeline::MabfModeHcaPayloads distinct_modes{&variable_hca, &mode1_hca, &mode2_hca};
    auto variable = ff7rp::pipeline::build_mabf_from_mode_hca(distinct_modes);
    if (!variable.status.ok() || !variable.release_valid) {
        std::cerr << "Variable MABF build failed\n";
        return 5;
    }
    const std::size_t slot_size = ff7rp::pipeline::mabf_slot_size(variable_hca.size());
    const std::size_t expected_size = ff7rp::pipeline::kMabfHeaderSize + slot_size * 3;
    if (variable.bytes.size() != expected_size || read_u32_le(variable.bytes, 0x0c) + 0x30u != expected_size) {
        std::cerr << "Variable MABF total size is incorrect\n";
        return 6;
    }
    if (read_u32_le(variable.bytes, 0x424) != 0x20u + slot_size ||
        read_u32_le(variable.bytes, 0x428) != 0x20u + slot_size * 2 ||
        read_u32_le(variable.bytes, 0x448) != variable_hca.size() - ff7rp::pipeline::kMabfHcaHeaderSize) {
        std::cerr << "Variable MABF slot metadata is incorrect\n";
        return 7;
    }
    const std::size_t fixed_slot_size = ff7rp::pipeline::mabf_slot_size(fixed_hca.size());
    for (std::size_t mode = 0; mode < kNativeFixedTrailers.size(); ++mode) {
        const std::size_t trailer = ff7rp::pipeline::kMabfHeaderSize + mode * fixed_slot_size + fixed_hca.size();
        if (!std::equal(kNativeFixedTrailers[mode].begin(), kNativeFixedTrailers[mode].end(),
                result.bytes.begin() + trailer)) {
            const auto mismatch = std::mismatch(kNativeFixedTrailers[mode].begin(),
                kNativeFixedTrailers[mode].end(), result.bytes.begin() + trailer);
            std::cerr << "Fixed-profile Mode" << mode << " trailer changed at byte "
                      << std::distance(kNativeFixedTrailers[mode].begin(), mismatch.first)
                      << ": expected=0x" << std::hex << static_cast<unsigned>(*mismatch.first)
                      << " actual=0x" << static_cast<unsigned>(*mismatch.second) << std::dec << '\n';
            return 5;
        }
    }

    const std::vector<uint8_t> short_prefix_hca = fake_hca(8, 0x44);
    const ff7rp::pipeline::MabfModeHcaPayloads short_prefix_modes{
        &short_prefix_hca, &short_prefix_hca, &short_prefix_hca};
    const auto short_prefix = ff7rp::pipeline::build_mabf_from_mode_hca(short_prefix_modes);
    const std::size_t short_slot_size = ff7rp::pipeline::mabf_slot_size(short_prefix_hca.size());
    if (!short_prefix.status.ok() || ff7rp::pipeline::mabf_slot_padding(short_prefix_hca.size()) != 2u) {
        std::cerr << "Two-byte native-prefix fixture did not build\n";
        return 6;
    }
    for (std::size_t mode = 0; mode < kNativeTwoBytePrefixes.size(); ++mode) {
        const std::size_t prefix = ff7rp::pipeline::kMabfHeaderSize + mode * short_slot_size +
            short_prefix_hca.size();
        if (!std::equal(kNativeTwoBytePrefixes[mode].begin(), kNativeTwoBytePrefixes[mode].end(),
                short_prefix.bytes.begin() + prefix)) {
            std::cerr << "Two-byte Mode" << mode << " prefix changed\n";
            return 7;
        }
    }
    for (std::size_t mode = 0; mode < distinct_modes.size(); ++mode) {
        const std::size_t offset = ff7rp::pipeline::kMabfHeaderSize + mode * slot_size;
        if (!std::equal(distinct_modes[mode]->begin(), distinct_modes[mode]->end(),
                variable.bytes.begin() + offset)) {
            std::cerr << "MABF builder did not preserve distinct Mode" << mode << " HCA payload\n";
            return 8;
        }
    }
    if (std::equal(variable_hca.begin(), variable_hca.end(), mode1_hca.begin()) ||
        std::equal(mode1_hca.begin(), mode1_hca.end(), mode2_hca.begin())) {
        std::cerr << "Distinct-mode test fixture is not distinct\n";
        return 9;
    }
    const std::size_t trailer0 = ff7rp::pipeline::kMabfHeaderSize + variable_hca.size() +
        ff7rp::pipeline::mabf_slot_padding(variable_hca.size());
    const std::size_t trailer1 = ff7rp::pipeline::kMabfHeaderSize + slot_size + variable_hca.size() +
        ff7rp::pipeline::mabf_slot_padding(variable_hca.size());
    const std::uint32_t payload_size = static_cast<std::uint32_t>(
        variable_hca.size() - ff7rp::pipeline::kMabfHcaHeaderSize);
    if (read_u32_le(variable.bytes, trailer0 + 0x16) != payload_size ||
        read_u32_le(variable.bytes, trailer1 + 0x16) != payload_size) {
        std::cerr << "Variable MABF trailer payload metadata is incorrect\n";
        return 10;
    }

    auto bad_magic = variable_hca;
    bad_magic[0] = 'X';
    const ff7rp::pipeline::MabfModeHcaPayloads bad_magic_modes{&variable_hca, &bad_magic, &mode2_hca};
    if (ff7rp::pipeline::build_mabf_from_mode_hca(bad_magic_modes).status.ok()) {
        std::cerr << "Malformed HCA magic was accepted\n";
        return 11;
    }
    auto bad_frames = variable_hca;
    bad_frames[19] = 48;
    const ff7rp::pipeline::MabfModeHcaPayloads bad_frame_modes{&variable_hca, &bad_frames, &mode2_hca};
    if (ff7rp::pipeline::build_mabf_from_mode_hca(bad_frame_modes).status.ok()) {
        std::cerr << "Mismatched HCA frame count was accepted\n";
        return 12;
    }
    auto truncated = variable_hca;
    truncated.pop_back();
    const ff7rp::pipeline::MabfModeHcaPayloads truncated_modes{&variable_hca, &mode1_hca, &truncated};
    if (ff7rp::pipeline::build_mabf_from_mode_hca(truncated_modes).status.ok()) {
        std::cerr << "Truncated HCA was accepted\n";
        return 13;
    }

    HcaEncodeConfig encoder_config;
    encoder_config.target_samples = 1;
    const std::vector<std::int16_t> pcm(encoder_config.target_samples * 2u, 0);
    const std::vector<std::uint8_t> release_hca = encode_hca_48k_stereo_256k(pcm, encoder_config);
    const ff7rp::pipeline::MabfModeHcaPayloads release_modes{&release_hca, &release_hca, &release_hca};
    const auto release_fixture = ff7rp::pipeline::build_mabf_from_mode_hca(release_modes);
    if (!release_fixture.status.ok() || !release_fixture.release_valid) {
        std::cerr << "Release-valid structural MABF fixture build failed\n";
        return 14;
    }
    constexpr std::size_t kGoldenMabfSize = 3616u;
    constexpr std::uint64_t kGoldenMabfDigest = 0x9ce24baf0bd2cf21ull;
    const std::uint64_t release_digest = ff7rp::pipeline::fnv1a64_append(
        ff7rp::pipeline::kFnv1a64OffsetBasis,
        release_fixture.bytes.data(), release_fixture.bytes.size());
    if (release_fixture.bytes.size() != kGoldenMabfSize ||
        read_u32_le(release_fixture.bytes, 0x0c) != 3568u ||
        read_u32_le(release_fixture.bytes, 0x424) != 864u ||
        read_u32_le(release_fixture.bytes, 0x428) != 1696u ||
        read_u32_le(release_fixture.bytes, 0x448) != 682u ||
        release_digest != kGoldenMabfDigest ||
        ff7rp::pipeline::hex64(release_digest) != "9ce24baf0bd2cf21") {
        std::cerr << "Minimal release MABF byte golden changed: size="
                  << release_fixture.bytes.size() << " digest=0x" << std::hex
                  << release_digest << std::dec << "\n";
        return 15;
    }
    ff7rp::pipeline::MabfArtifactMetadata metadata;
    const auto valid_status = ff7rp::pipeline::validate_structural_mabf(release_fixture.bytes, &metadata);
    if (!valid_status.ok()) {
        std::cerr << "Release-valid structural MABF fixture was rejected: " << valid_status.message << "\n";
        return 16;
    }
    if (metadata.byte_count != release_fixture.bytes.size() ||
        metadata.logical_source_frames != encoder_config.target_samples ||
        metadata.sample_rate != 48000 || metadata.channels != 2 ||
        metadata.inserted_samples != 128 || metadata.block_size != 682) {
        std::cerr << "Structural MABF validation metadata changed\n";
        return 17;
    }

    // Construct a structurally and CRC-valid Mode1 HCA with one additional
    // frame while increasing appended samples so its logical source length
    // remains exactly one frame. Assemble a hybrid container from otherwise
    // valid one-frame and two-frame MABFs because the production builder
    // correctly refuses unequal mode frame counts.
    std::vector<std::uint8_t> extra_frame_hca = release_hca;
    extra_frame_hca.insert(extra_frame_hca.end(),
        release_hca.begin() + ff7rp::pipeline::kMabfHcaHeaderSize, release_hca.end());
    write_u32_be(extra_frame_hca, 16u, 2u);
    write_u16_be(extra_frame_hca, 22u, 1919u);
    write_u16_be(extra_frame_hca, 94u, hca_crc16(extra_frame_hca.data(), 94u));
    const ff7rp::pipeline::MabfModeHcaPayloads extra_frame_modes{
        &extra_frame_hca, &extra_frame_hca, &extra_frame_hca};
    const auto extra_frame_fixture = ff7rp::pipeline::build_mabf_from_mode_hca(extra_frame_modes);
    if (!extra_frame_fixture.status.ok()) {
        std::cerr << "CRC-valid extra-frame source fixture did not build\n";
        return 18;
    }
    const std::size_t one_frame_slot = ff7rp::pipeline::mabf_slot_size(release_hca.size());
    const std::size_t two_frame_slot = ff7rp::pipeline::mabf_slot_size(extra_frame_hca.size());
    std::vector<std::uint8_t> mixed_geometry(
        release_fixture.bytes.begin(), release_fixture.bytes.begin() + ff7rp::pipeline::kMabfHeaderSize);
    mixed_geometry.insert(mixed_geometry.end(),
        release_fixture.bytes.begin() + ff7rp::pipeline::kMabfHeaderSize,
        release_fixture.bytes.begin() + ff7rp::pipeline::kMabfHeaderSize + one_frame_slot);
    mixed_geometry.insert(mixed_geometry.end(),
        extra_frame_fixture.bytes.begin() + ff7rp::pipeline::kMabfHeaderSize + two_frame_slot,
        extra_frame_fixture.bytes.begin() + ff7rp::pipeline::kMabfHeaderSize + 2u * two_frame_slot);
    mixed_geometry.insert(mixed_geometry.end(),
        release_fixture.bytes.begin() + ff7rp::pipeline::kMabfHeaderSize + 2u * one_frame_slot,
        release_fixture.bytes.end());
    write_u32_le(mixed_geometry, 0x0cu, static_cast<std::uint32_t>(mixed_geometry.size() - 0x30u));
    write_u32_le(mixed_geometry, 0x424u, static_cast<std::uint32_t>(0x20u + one_frame_slot));
    write_u32_le(mixed_geometry, 0x428u,
        static_cast<std::uint32_t>(0x20u + one_frame_slot + two_frame_slot));
    const std::size_t mode0_suffix = ff7rp::pipeline::kMabfHeaderSize + release_hca.size() +
        ff7rp::pipeline::mabf_slot_padding(release_hca.size());
    const std::size_t mode1_begin = ff7rp::pipeline::kMabfHeaderSize + one_frame_slot;
    const std::size_t mode1_suffix = mode1_begin + extra_frame_hca.size() +
        ff7rp::pipeline::mabf_slot_padding(extra_frame_hca.size());
    write_u32_le(mixed_geometry, mode0_suffix + 0x16u,
        static_cast<std::uint32_t>(extra_frame_hca.size() - ff7rp::pipeline::kMabfHcaHeaderSize));
    write_u32_le(mixed_geometry, mode1_suffix + 0x16u,
        static_cast<std::uint32_t>(release_hca.size() - ff7rp::pipeline::kMabfHcaHeaderSize));

    ff7rp::pipeline::MabfArtifactMetadata mixed_geometry_metadata;
    const auto mixed_structural =
        ff7rp::pipeline::validate_structural_mabf(mixed_geometry, &mixed_geometry_metadata);
    if (!mixed_structural.ok() || mixed_geometry_metadata.logical_source_frames != 1u) {
        std::cerr << "CRC-valid mixed-geometry MABF was not structurally valid: "
                  << mixed_structural.message << '\n';
        return 19;
    }
    const auto mixed_resolved = ff7rp::pipeline::validate_resolved_mabf(
        mixed_geometry, 1u, {{0, 1, 0}, false}, &mixed_geometry_metadata);
    if (mixed_resolved.ok() || mixed_resolved.code != ff7rp::pipeline::StatusCode::MabfNotReleaseValid ||
        mixed_resolved.message != "MABF Mode1 HCA geometry does not exactly match Mode0") {
        std::cerr << "CRC-valid mixed Mode1 geometry was not rejected authoritatively: "
                  << mixed_resolved.message << '\n';
        return 20;
    }

    const std::array<std::size_t, 3> release_offsets{
        ff7rp::pipeline::kMabfHeaderSize,
        0x440u + read_u32_le(release_fixture.bytes, 0x424u),
        0x440u + read_u32_le(release_fixture.bytes, 0x428u),
    };
    const std::size_t release_prefix_size = ff7rp::pipeline::mabf_slot_padding(release_hca.size());
    const auto expect_prefix_rejected = [&](std::vector<std::uint8_t> malformed, const char* name) {
        ff7rp::pipeline::MabfArtifactMetadata malformed_metadata;
        const auto status = ff7rp::pipeline::validate_structural_mabf(malformed, &malformed_metadata);
        if (status.ok() || status.code != ff7rp::pipeline::StatusCode::MabfNotReleaseValid) {
            std::cerr << name << " was not rejected\n";
            return false;
        }
        return true;
    };
    {
        auto malformed = release_fixture.bytes;
        malformed[release_offsets[0] + release_hca.size() + release_prefix_size - 2u] = 0x00;
        if (!expect_prefix_rejected(std::move(malformed), "missing Mode0 01 00 prefix marker")) return 18;
    }
    {
        auto malformed = release_fixture.bytes;
        malformed[release_offsets[0] + release_hca.size() + release_prefix_size - 1u] = 0x01;
        if (!expect_prefix_rejected(std::move(malformed), "incorrect Mode0 01 00 prefix marker")) return 19;
    }
    {
        auto malformed = release_fixture.bytes;
        malformed[release_offsets[1] + release_hca.size()] = 0x01;
        if (!expect_prefix_rejected(std::move(malformed), "nonzero Mode1 prefix body")) return 20;
    }
    {
        auto malformed = release_fixture.bytes;
        malformed[release_offsets[2] + release_hca.size()] = 0x01;
        if (!expect_prefix_rejected(std::move(malformed), "nonzero Mode2 prefix")) return 21;
    }
    {
        auto malformed = release_fixture.bytes;
        malformed[release_offsets[0] + 28u] = 0x02;
        malformed[release_offsets[0] + 29u] = 0xb1;
        if (!expect_prefix_rejected(std::move(malformed), "undersized Mode0 prefix")) return 22;
    }
    {
        auto malformed = release_fixture.bytes;
        malformed[release_offsets[2] + release_hca.size() + release_prefix_size] ^= 0x01;
        if (!expect_prefix_rejected(std::move(malformed), "bad Mode2 metadata suffix")) return 23;
    }

    constexpr std::size_t kSlot1RelativeOffsetField = 0x424;
    constexpr std::size_t kSlot2RelativeOffsetField = 0x428;
    constexpr std::size_t kRelativeOffsetBase = 0x440;
    constexpr std::size_t kMinimumModeSize =
        ff7rp::pipeline::kMabfHcaHeaderSize + ff7rp::pipeline::kMabfTrailerMetadataSize;
    const std::uint32_t slot1_relative = read_u32_le(release_fixture.bytes, kSlot1RelativeOffsetField);
    const std::uint32_t slot2_relative = read_u32_le(release_fixture.bytes, kSlot2RelativeOffsetField);
    const std::uint32_t file_end_relative = static_cast<std::uint32_t>(
        release_fixture.bytes.size() - kRelativeOffsetBase);
    struct OffsetMutationCase {
        const char* name;
        std::size_t field;
        std::uint32_t value;
    };
    const OffsetMutationCase offset_mutations[] = {
        {"oversized Mode0", kSlot1RelativeOffsetField,
            slot2_relative - static_cast<std::uint32_t>(kMinimumModeSize - 1u)},
        {"oversized Mode1", kSlot2RelativeOffsetField,
            file_end_relative - static_cast<std::uint32_t>(kMinimumModeSize - 1u)},
        {"reversed Mode1", kSlot1RelativeOffsetField, 0x1fu},
        {"overlapping Mode2", kSlot2RelativeOffsetField, slot1_relative - 1u},
        {"overflowed Mode1 offset", kSlot1RelativeOffsetField,
            std::numeric_limits<std::uint32_t>::max()},
        {"overflowed Mode2 offset", kSlot2RelativeOffsetField,
            std::numeric_limits<std::uint32_t>::max()},
        {"out-of-file Mode1 offset", kSlot1RelativeOffsetField, file_end_relative + 1u},
        {"out-of-file Mode2 offset", kSlot2RelativeOffsetField, file_end_relative + 1u},
    };
    for (const OffsetMutationCase& mutation : offset_mutations) {
        auto malformed = release_fixture.bytes;
        write_u32_le(malformed, mutation.field, mutation.value);
        ff7rp::pipeline::MabfArtifactMetadata malformed_metadata;
        const auto status = ff7rp::pipeline::validate_structural_mabf(malformed, &malformed_metadata);
        if (status.ok() || status.code != ff7rp::pipeline::StatusCode::MabfNotReleaseValid) {
            std::cerr << mutation.name << " was not rejected by structural range preflight\n";
            return 18;
        }
    }

    std::cout << "mabf_builder_selftest ok\n";
    return 0;
}
