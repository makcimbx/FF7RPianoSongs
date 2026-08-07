#include "pipeline/audio_artifact_builder.h"

#include "pipeline/cache.h"
#include "pipeline/mabf_builder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using ff7rp::pipeline::MabfArtifactMetadata;
using ff7rp::pipeline::MabfBuildResult;
using ff7rp::pipeline::StatusCode;
using ff7rp::pipeline::WavAudio;

constexpr std::size_t kFixtureFrames = 897u;
constexpr std::size_t kGoldenMabfSize = 5680u;
constexpr std::size_t kGoldenHcaSize = 1460u;
constexpr std::uint64_t kCleanMabfDigest = 0x1e0cfc7e8b55d353ull;
constexpr std::uint64_t kAdaptiveMabfDigest = 0x0d4667c0d86f092ull;
constexpr std::array<std::uint64_t, 3> kCleanModeDigests{
    0xf99ccb339309d017ull, 0xf99ccb339309d017ull, 0xf99ccb339309d017ull};
constexpr std::array<std::uint64_t, 3> kAdaptiveModeDigests{
    0x293cd44b48926192ull, 0xfd6e8380747338d7ull, 0xf99ccb339309d017ull};
constexpr const char* kReleaseValidationNote =
    "Three-mode MABF assembled from explicit per-mode HCA payloads and the proven bgm09 scaffold.";

int fail(const std::string& message) {
    std::cerr << "audio_artifact_builder_selftest: " << message << '\n';
    return 1;
}

WavAudio fixture(const std::array<float, 8>& pattern, const std::size_t frames = kFixtureFrames) {
    WavAudio audio;
    audio.source_frame_count = frames;
    audio.stereo_samples.reserve(frames * 2u);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        audio.stereo_samples.push_back(pattern[(frame * 2u) % pattern.size()]);
        audio.stereo_samples.push_back(pattern[(frame * 2u + 1u) % pattern.size()]);
    }
    return audio;
}

bool audio_equal(const WavAudio& left, const WavAudio& right) {
    return left.sample_rate == right.sample_rate && left.channels == right.channels &&
        left.source_frame_count == right.source_frame_count && left.stereo_samples == right.stereo_samples;
}

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::uint64_t digest(const std::uint8_t* bytes, const std::size_t size) {
    return ff7rp::pipeline::fnv1a64_append(ff7rp::pipeline::kFnv1a64OffsetBasis, bytes, size);
}

std::array<std::uint64_t, 3> mode_digests(const std::vector<std::uint8_t>& mabf) {
    const std::size_t hca_size = ff7rp::pipeline::kMabfHcaHeaderSize + read_u32_le(mabf, 0x448u);
    const std::size_t slot_size = ff7rp::pipeline::mabf_slot_size(hca_size);
    std::array<std::uint64_t, 3> result{};
    for (std::size_t mode = 0; mode < result.size(); ++mode) {
        const std::size_t offset = ff7rp::pipeline::kMabfHeaderSize + mode * slot_size;
        result[mode] = digest(mabf.data() + offset, hca_size);
    }
    return result;
}

bool mode_hca_equal(const std::vector<std::uint8_t>& mabf, const std::size_t left, const std::size_t right) {
    const std::size_t hca_size = ff7rp::pipeline::kMabfHcaHeaderSize + read_u32_le(mabf, 0x448u);
    const std::size_t slot_size = ff7rp::pipeline::mabf_slot_size(hca_size);
    const auto left_begin = mabf.begin() + ff7rp::pipeline::kMabfHeaderSize + left * slot_size;
    const auto right_begin = mabf.begin() + ff7rp::pipeline::kMabfHeaderSize + right * slot_size;
    return std::equal(left_begin, left_begin + hca_size, right_begin);
}

bool metadata_matches(const MabfArtifactMetadata& metadata) {
    return metadata.digest == 0u && metadata.byte_count == kGoldenMabfSize &&
        metadata.logical_source_frames == kFixtureFrames && metadata.hca_frame_count == 2u &&
        metadata.sample_rate == 48000u && metadata.channels == 2u && metadata.inserted_samples == 128u &&
        metadata.appended_samples == 1023u && metadata.block_size == 682u;
}

int golden_fail(const char* message, const MabfBuildResult& result) {
    std::cerr << message << ": size=" << result.bytes.size() << " digest=0x" << std::hex
              << digest(result.bytes.data(), result.bytes.size());
    const auto modes = mode_digests(result.bytes);
    std::cerr << " modes=[0x" << modes[0] << ",0x" << modes[1] << ",0x" << modes[2]
              << "]" << std::dec << '\n';
    return 1;
}

std::vector<std::string> clean_trace() {
    return {"hca_mode2_pcm_started", "hca_mode2_encode_started", "hca_mode2_ready", "mabf_build_started"};
}

std::vector<std::string> adaptive_trace() {
    return {"hca_mode2_pcm_started", "hca_mode2_encode_started", "hca_mode2_ready",
        "hca_mode0_pcm_started", "hca_mode0_encode_started", "hca_mode0_ready",
        "hca_mode1_pcm_started", "hca_mode1_encode_started", "hca_mode1_ready", "mabf_build_started"};
}

} // namespace

int main() {
    const WavAudio clean = fixture({-1.0f, -0.5f, -0.25f, 0.0f, 0.125f, 0.25f, 0.5f, 1.0f});
    const WavAudio strong = fixture({1.0f, 0.75f, 0.5f, 0.25f, -0.25f, -0.5f, -0.75f, -1.0f});
    const WavAudio weak = fixture({0.125f, -0.125f, 0.375f, -0.375f, 0.625f, -0.625f, 0.875f, -0.875f});
    const WavAudio clean_before = clean;
    const WavAudio strong_before = strong;
    const WavAudio weak_before = weak;

    std::vector<std::string> trace;
    MabfBuildResult result = ff7rp::pipeline::build_audio_mabf(
        clean, nullptr, nullptr, false, [&](const char* stage) { trace.emplace_back(stage); });
    if (!result.status.ok() || !result.release_valid || result.used_placeholder_scaffold ||
        result.validation_note != kReleaseValidationNote) return fail("clean build flags changed");
    if (trace != clean_trace()) return fail("clean trace changed");
    if (result.bytes.size() != kGoldenMabfSize || digest(result.bytes.data(), result.bytes.size()) != kCleanMabfDigest ||
        read_u32_le(result.bytes, 0x448u) + ff7rp::pipeline::kMabfHcaHeaderSize != kGoldenHcaSize ||
        mode_digests(result.bytes) != kCleanModeDigests || !mode_hca_equal(result.bytes, 0u, 1u) ||
        !mode_hca_equal(result.bytes, 0u, 2u)) return golden_fail("clean parent byte golden changed", result);
    MabfArtifactMetadata metadata;
    if (!ff7rp::pipeline::validate_clean_mabf(result.bytes, kFixtureFrames, &metadata).ok() ||
        !metadata_matches(metadata)) return fail("clean validator metadata changed");

    trace.clear();
    result = ff7rp::pipeline::build_audio_mabf(
        clean, &strong, &weak, true, [&](const char* stage) { trace.emplace_back(stage); });
    if (!result.status.ok() || !result.release_valid || result.used_placeholder_scaffold ||
        result.validation_note != kReleaseValidationNote) return fail("adaptive build flags changed");
    if (trace != adaptive_trace()) return fail("adaptive trace changed");
    const auto adaptive_modes = mode_digests(result.bytes);
    if (result.bytes.size() != kGoldenMabfSize || digest(result.bytes.data(), result.bytes.size()) != kAdaptiveMabfDigest ||
        read_u32_le(result.bytes, 0x448u) + ff7rp::pipeline::kMabfHcaHeaderSize != kGoldenHcaSize ||
        adaptive_modes != kAdaptiveModeDigests || adaptive_modes[0] == adaptive_modes[1] ||
        adaptive_modes[0] == adaptive_modes[2] || adaptive_modes[1] == adaptive_modes[2] ||
        mode_hca_equal(result.bytes, 0u, 1u) || mode_hca_equal(result.bytes, 0u, 2u) ||
        mode_hca_equal(result.bytes, 1u, 2u)) {
        return golden_fail("adaptive parent byte golden or mode order changed", result);
    }
    metadata = {};
    if (!ff7rp::pipeline::validate_adaptive_metronome_mabf(result.bytes, kFixtureFrames, &metadata).ok() ||
        !metadata_matches(metadata)) return fail("adaptive validator metadata changed");

    const std::vector<std::string> ready_trace{
        "hca_mode2_pcm_started", "hca_mode2_encode_started", "hca_mode2_ready"};
    trace.clear();
    result = ff7rp::pipeline::build_audio_mabf(
        clean, nullptr, &weak, true, [&](const char* stage) { trace.emplace_back(stage); });
    if (result.status.code != StatusCode::MabfNotReleaseValid ||
        result.status.message != "adaptive metronome mode audio is missing or has mismatched duration" ||
        !result.bytes.empty() || trace != ready_trace) return fail("missing-guide failure changed");

    WavAudio mismatched = weak;
    mismatched.stereo_samples.resize(mismatched.stereo_samples.size() - 2u);
    const WavAudio mismatched_before = mismatched;
    trace.clear();
    result = ff7rp::pipeline::build_audio_mabf(
        clean, &strong, &mismatched, true, [&](const char* stage) { trace.emplace_back(stage); });
    if (result.status.code != StatusCode::MabfNotReleaseValid ||
        result.status.message != "adaptive metronome mode audio is missing or has mismatched duration" ||
        !result.bytes.empty() || trace != ready_trace) return fail("mismatched-guide failure changed");

    const WavAudio zero;
    const WavAudio zero_before = zero;
    trace.clear();
    result = ff7rp::pipeline::build_audio_mabf(
        zero, nullptr, nullptr, false, [&](const char* stage) { trace.emplace_back(stage); });
    const std::vector<std::string> zero_trace{"hca_mode2_pcm_started", "hca_mode2_encode_started"};
    if (result.status.code != StatusCode::HcaUnavailable ||
        result.status.message != "native HCA encoder requires non-empty 48 kHz stereo PCM16 at 256 kbps" ||
        !result.bytes.empty() || trace != zero_trace) return fail("zero-frame HCA failure changed");

    if (!audio_equal(clean, clean_before) || !audio_equal(strong, strong_before) ||
        !audio_equal(weak, weak_before) || !audio_equal(mismatched, mismatched_before) ||
        !audio_equal(zero, zero_before)) return fail("builder mutated an input audio buffer");

    std::cout << "audio_artifact_builder_selftest passed\n";
    return 0;
}
