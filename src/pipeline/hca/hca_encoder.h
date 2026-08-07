#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct HcaEncodeConfig
{
    int sample_rate = 48000;
    int channels = 2;
    int bitrate = 256000;
    std::size_t target_samples = 996000;
};

inline constexpr std::size_t FF7R_HCA_EXPECTED_BYTES = 663682;
inline constexpr int FF7R_HCA_EXPECTED_BLOCK_COUNT = 973;
inline constexpr int FF7R_HCA_EXPECTED_BLOCK_SIZE = 682;
inline constexpr int FF7R_HCA_EXPECTED_DATA_OFFSET = 96;
inline constexpr int FF7R_HCA_INSERTED_SAMPLES = 128;
inline constexpr int FF7R_HCA_SAMPLES_PER_FRAME = 1024;

class HcaEncoderError : public std::runtime_error
{
public:
    explicit HcaEncoderError(const std::string& reason);
};

std::vector<std::uint8_t> encode_hca_48k_stereo_256k(
    const std::vector<std::int16_t>& interleaved_pcm,
    const HcaEncodeConfig& config = HcaEncodeConfig{});
