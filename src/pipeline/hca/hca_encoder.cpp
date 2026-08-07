#include "hca_encoder.h"
#include "pipeline/pipeline_limits.h"

#include "hca_tables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Fixed-profile CRI HCA encoder ported from VGAudio by Alex Barney (MIT License).
// Source: VGAudio.Codecs.CriHca.CriHcaEncoder, CriHcaPacking, HcaWriter,
// Utilities.Mdct, Utilities.BitWriter, and Utilities.Crc16.

namespace
{
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kBitrate = 256000;
constexpr int kSubframesPerFrame = 8;
constexpr int kSamplesPerSubframe = 128;
constexpr int kSamplesPerFrame = kSubframesPerFrame * kSamplesPerSubframe;
constexpr int kBandCount = 128;
constexpr int kFrameSize = 682;
constexpr int kHeaderSize = 96;
constexpr int kInsertedSamples = 128;
constexpr double kPi = 3.141592653589793238462643383279502884;

using SampleBlock = std::array<double, kSamplesPerSubframe>;
using SubframeBands = std::array<double, kBandCount>;

int clamp_int(int value, int low, int high)
{
    return std::min(std::max(value, low), high);
}

double clamp_double(double value, double low, double high)
{
    return std::min(std::max(value, low), high);
}

int resolution_max_value(int resolution)
{
    return resolution < 8 ? resolution : (1 << (resolution - 4)) - 1;
}

double dequantizer_scaling(int scale_factor)
{
    const double base = std::pow(2.0, 53.0 / 128.0);
    return std::sqrt(128.0) * std::pow(base, scale_factor - 63);
}

double quantizer_scaling(int scale_factor)
{
    return 1.0 / dequantizer_scaling(scale_factor);
}

double quantizer_inverse_step_size(int resolution)
{
    return resolution_max_value(resolution) + 0.5;
}

double quantizer_dead_zone(int resolution)
{
    const double step = 1.0 / quantizer_inverse_step_size(resolution);
    return std::nextafter(step / 2.0, 0.0);
}

std::uint32_t reverse_bits(std::uint32_t value)
{
    value = ((value & 0xaaaaaaaau) >> 1) | ((value & 0x55555555u) << 1);
    value = ((value & 0xccccccccu) >> 2) | ((value & 0x33333333u) << 2);
    value = ((value & 0xf0f0f0f0u) >> 4) | ((value & 0x0f0f0f0fu) << 4);
    value = ((value & 0xff00ff00u) >> 8) | ((value & 0x00ff00ffu) << 8);
    return (value >> 16) | (value << 16);
}

std::uint32_t reverse_bits(std::uint32_t value, int bit_count)
{
    return reverse_bits(value) >> (32 - bit_count);
}

class BitWriter
{
public:
    explicit BitWriter(std::vector<std::uint8_t>& buffer)
        : buffer_(buffer)
    {
    }

    int position() const
    {
        return position_;
    }

    int length_bits() const
    {
        return static_cast<int>(buffer_.size() * 8);
    }

    void set_position(int position)
    {
        if (position < 0 || position > length_bits())
        {
            throw HcaEncoderError("HCA bit writer position is out of range");
        }
        position_ = position;
    }

    void align(int multiple)
    {
        const int remainder = position_ % multiple;
        if (remainder != 0)
        {
            write(0, multiple - remainder);
        }
    }

    void write(std::uint32_t value, int bit_count)
    {
        if (bit_count < 0 || position_ + bit_count > length_bits())
        {
            throw HcaEncoderError("HCA bit writer overflow while packing frame");
        }

        for (int bit = bit_count - 1; bit >= 0; --bit)
        {
            const int byte_index = position_ / 8;
            const int bit_index = 7 - (position_ % 8);
            const std::uint8_t mask = static_cast<std::uint8_t>(1u << bit_index);
            if (((value >> bit) & 1u) != 0)
            {
                buffer_[byte_index] |= mask;
            }
            else
            {
                buffer_[byte_index] &= static_cast<std::uint8_t>(~mask);
            }
            ++position_;
        }
    }

private:
    std::vector<std::uint8_t>& buffer_;
    int position_ = 0;
};

class Crc16
{
public:
    Crc16()
    {
        for (int i = 0; i < 256; ++i)
        {
            std::uint16_t value = static_cast<std::uint16_t>(i << 8);
            for (int bit = 0; bit < 8; ++bit)
            {
                value = (value & 0x8000u) != 0
                    ? static_cast<std::uint16_t>((value << 1) ^ 0x8005u)
                    : static_cast<std::uint16_t>(value << 1);
            }
            table_[i] = value;
        }
    }

    std::uint16_t compute(const std::uint8_t* data, std::size_t size) const
    {
        std::uint16_t crc = 0;
        for (std::size_t i = 0; i < size; ++i)
        {
            crc = static_cast<std::uint16_t>((crc << 8) ^ table_[((crc >> 8) ^ data[i]) & 0xffu]);
        }
        return crc;
    }

private:
    std::array<std::uint16_t, 256> table_{};
};

class Mdct
{
public:
    Mdct()
    {
        generate_tables(7);
    }

    void run(const SampleBlock& input, SubframeBands& output)
    {
        const int size = kSamplesPerSubframe;
        const int half = size / 2;
        const auto& window = ff7r::hca::tables::mdct_window;

        for (int i = 0; i < half; ++i)
        {
            const double a = window[half - i - 1] * -input[half + i];
            const double b = window[half + i] * input[half - i - 1];
            const double c = window[i] * previous_[i];
            const double d = window[size - i - 1] * previous_[size - i - 1];
            scratch_mdct_[i] = a - b;
            scratch_mdct_[half + i] = c - d;
        }

        dct4(scratch_mdct_, output);
        std::copy(input.begin(), input.end(), previous_.begin());
    }

private:
    void generate_tables(int max_bits)
    {
        for (int bits = 0; bits <= max_bits; ++bits)
        {
            const int size = 1 << bits;
            sin_tables_[bits].assign(size, 0.0);
            cos_tables_[bits].assign(size, 0.0);
            shuffle_tables_[bits].assign(size, 0);
            for (int i = 0; i < size; ++i)
            {
                const double value = kPi * (4 * i + 1) / (4.0 * size);
                sin_tables_[bits][i] = std::sin(value);
                cos_tables_[bits][i] = std::cos(value);
                shuffle_tables_[bits][i] = static_cast<int>(reverse_bits(static_cast<std::uint32_t>(i ^ (i / 2)), bits));
            }
        }
    }

    void dct4(const std::array<double, kSamplesPerSubframe>& input, SubframeBands& output)
    {
        constexpr int mdct_bits = 7;
        constexpr int size = kSamplesPerSubframe;
        constexpr int last_index = size - 1;
        constexpr int half_size = size / 2;

        const auto& sin_table = sin_tables_[mdct_bits];
        const auto& cos_table = cos_tables_[mdct_bits];

        for (int i = 0; i < half_size; ++i)
        {
            const int i2 = i * 2;
            const double a = input[i2];
            const double b = input[last_index - i2];
            const double sin = sin_table[i];
            const double cos = cos_table[i];
            scratch_dct_[i2] = a * cos + b * sin;
            scratch_dct_[i2 + 1] = a * sin - b * cos;
        }

        constexpr int stage_count = mdct_bits - 1;
        for (int stage = 0; stage < stage_count; ++stage)
        {
            const int block_count = 1 << stage;
            const int block_size_bits = stage_count - stage;
            const int block_half_size_bits = block_size_bits - 1;
            const int block_size = 1 << block_size_bits;
            const int block_half_size = 1 << block_half_size_bits;
            const auto& stage_sin = sin_tables_[block_half_size_bits];
            const auto& stage_cos = cos_tables_[block_half_size_bits];

            for (int block = 0; block < block_count; ++block)
            {
                for (int i = 0; i < block_half_size; ++i)
                {
                    const int front_pos = (block * block_size + i) * 2;
                    const int back_pos = front_pos + block_size;
                    const double a = scratch_dct_[front_pos] - scratch_dct_[back_pos];
                    const double b = scratch_dct_[front_pos + 1] - scratch_dct_[back_pos + 1];
                    const double sin = stage_sin[i];
                    const double cos = stage_cos[i];
                    scratch_dct_[front_pos] += scratch_dct_[back_pos];
                    scratch_dct_[front_pos + 1] += scratch_dct_[back_pos + 1];
                    scratch_dct_[back_pos] = a * cos + b * sin;
                    scratch_dct_[back_pos + 1] = a * sin - b * cos;
                }
            }
        }

        const auto& shuffle = shuffle_tables_[mdct_bits];
        constexpr double scale = 0.125; // sqrt(2.0 / 128.0)
        for (int i = 0; i < size; ++i)
        {
            output[i] = scratch_dct_[shuffle[i]] * scale;
        }
    }

    std::array<double, kSamplesPerSubframe> previous_{};
    std::array<double, kSamplesPerSubframe> scratch_mdct_{};
    std::array<double, kSamplesPerSubframe> scratch_dct_{};
    std::array<std::vector<double>, 8> sin_tables_{};
    std::array<std::vector<double>, 8> cos_tables_{};
    std::array<std::vector<int>, 8> shuffle_tables_{};
};

struct Channel
{
    std::array<SampleBlock, kSubframesPerFrame> pcm_float{};
    std::array<SubframeBands, kSubframesPerFrame> spectra{};
    std::array<std::array<double, kSubframesPerFrame>, kBandCount> scaled_spectra{};
    std::array<std::array<int, kBandCount>, kSubframesPerFrame> quantized_spectra{};
    std::array<int, kBandCount> scale_factors{};
    std::array<int, kBandCount> resolution{};
    int header_length_bits = 3;
    int scale_factor_delta_bits = 0;
    Mdct mdct;
};

struct FrameState
{
    std::array<Channel, kChannels>* channels = nullptr;
    int acceptable_noise_level = 0;
    int evaluation_boundary = 0;
};

int calculate_resolution(int scale_factor, int noise_level)
{
    if (scale_factor == 0)
    {
        return 0;
    }
    const int curve_position = clamp_int(noise_level - 5 * scale_factor / 2 + 2, 0, 58);
    return ff7r::hca::tables::scale_to_resolution_curve[curve_position];
}

int find_scale_factor(double value)
{
    int low = 0;
    int high = 63;
    while (low < high)
    {
        const int mid = (low + high) / 2;
        if (dequantizer_scaling(mid) <= value)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return low;
}

void pcm_to_float(const std::array<std::array<std::int16_t, kSamplesPerFrame>, kChannels>& pcm,
    std::array<Channel, kChannels>& channels)
{
    for (int c = 0; c < kChannels; ++c)
    {
        int pcm_index = 0;
        for (int sf = 0; sf < kSubframesPerFrame; ++sf)
        {
            for (int i = 0; i < kSamplesPerSubframe; ++i)
            {
                channels[c].pcm_float[sf][i] = pcm[c][pcm_index++] * (1.0 / 32768.0);
            }
        }
    }
}

void run_mdct(std::array<Channel, kChannels>& channels)
{
    for (Channel& channel : channels)
    {
        for (int sf = 0; sf < kSubframesPerFrame; ++sf)
        {
            channel.mdct.run(channel.pcm_float[sf], channel.spectra[sf]);
        }
    }
}

void calculate_scale_factors(std::array<Channel, kChannels>& channels)
{
    for (Channel& channel : channels)
    {
        for (int band = 0; band < kBandCount; ++band)
        {
            double max_coeff = 0.0;
            for (int sf = 0; sf < kSubframesPerFrame; ++sf)
            {
                max_coeff = std::max(max_coeff, std::abs(channel.spectra[sf][band]));
            }
            channel.scale_factors[band] = find_scale_factor(max_coeff);
        }
    }
}

void scale_spectra(std::array<Channel, kChannels>& channels)
{
    for (Channel& channel : channels)
    {
        for (int band = 0; band < kBandCount; ++band)
        {
            const int scale_factor = channel.scale_factors[band];
            const double scaling = scale_factor == 0 ? 0.0 : quantizer_scaling(scale_factor);
            for (int sf = 0; sf < kSubframesPerFrame; ++sf)
            {
                channel.scaled_spectra[band][sf] = scale_factor == 0 ? 0.0
                    : clamp_double(channel.spectra[sf][band] * scaling, -0.999999999999, 0.999999999999);
            }
        }
    }
}

void calculate_optimal_delta_length(Channel& channel)
{
    const bool empty_channel = std::all_of(channel.scale_factors.begin(), channel.scale_factors.end(),
        [](int scale_factor) { return scale_factor == 0; });
    if (empty_channel)
    {
        channel.header_length_bits = 3;
        channel.scale_factor_delta_bits = 0;
        return;
    }

    int min_delta_bits = 6;
    int min_length = 3 + 6 * kBandCount;
    for (int delta_bits = 1; delta_bits < 6; ++delta_bits)
    {
        const int max_delta = (1 << (delta_bits - 1)) - 1;
        int length = 3 + 6;
        for (int band = 1; band < kBandCount; ++band)
        {
            const int delta = channel.scale_factors[band] - channel.scale_factors[band - 1];
            length += std::abs(delta) > max_delta ? delta_bits + 6 : delta_bits;
        }
        if (length < min_length)
        {
            min_length = length;
            min_delta_bits = delta_bits;
        }
    }

    channel.header_length_bits = min_length;
    channel.scale_factor_delta_bits = min_delta_bits;
}

void calculate_frame_header_length(FrameState& frame)
{
    for (Channel& channel : *frame.channels)
    {
        calculate_optimal_delta_length(channel);
    }
}

int calculate_used_bits(const std::array<Channel, kChannels>& channels, int noise_level, int eval_boundary)
{
    int length = 16 + 16 + 16;
    for (const Channel& channel : channels)
    {
        length += channel.header_length_bits;
        for (int band = 0; band < kBandCount; ++band)
        {
            const int noise = band < eval_boundary ? noise_level - 1 : noise_level;
            const int resolution = calculate_resolution(channel.scale_factors[band], noise);
            if (resolution >= 8)
            {
                const int bits = ff7r::hca::tables::quantized_spectrum_max_bits[resolution] - 1;
                const double dead_zone = quantizer_dead_zone(resolution);
                for (double scaled : channel.scaled_spectra[band])
                {
                    length += bits;
                    if (std::abs(scaled) >= dead_zone)
                    {
                        ++length;
                    }
                }
            }
            else
            {
                const double step_size_inv = quantizer_inverse_step_size(resolution);
                const double shift_up = step_size_inv + 1.0;
                const int shift_down = static_cast<int>(step_size_inv + 0.5 - 8.0);
                for (double scaled : channel.scaled_spectra[band])
                {
                    const int quantized = static_cast<int>(scaled * step_size_inv + shift_up) - shift_down;
                    length += ff7r::hca::tables::quantize_spectrum_bits[resolution][quantized];
                }
            }
        }
    }
    return length;
}

int binary_search_level(const std::array<Channel, kChannels>& channels, int available_bits, int low, int high)
{
    const int max = high;
    int mid_value = 0;
    while (low != high)
    {
        const int mid = (low + high) / 2;
        mid_value = calculate_used_bits(channels, mid, 0);
        if (mid_value > available_bits)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return low == max && mid_value > available_bits ? -1 : low;
}

int binary_search_boundary(const std::array<Channel, kChannels>& channels, int available_bits,
    int noise_level, int low, int high)
{
    const int max = high;
    while (std::abs(high - low) > 1)
    {
        const int mid = (low + high) / 2;
        const int mid_value = calculate_used_bits(channels, noise_level, mid);
        if (available_bits < mid_value)
        {
            high = mid - 1;
        }
        else
        {
            low = mid;
        }
    }

    if (low == high)
    {
        return low < max ? low : -1;
    }

    const int high_value = calculate_used_bits(channels, noise_level, high);
    return high_value > available_bits ? low : high;
}

void calculate_noise_level(FrameState& frame)
{
    int highest_band = kBandCount - 1;
    constexpr int available_bits = kFrameSize * 8;
    int level = binary_search_level(*frame.channels, available_bits, 0, 255);

    while (level < 0)
    {
        highest_band -= 2;
        if (highest_band < 0)
        {
            throw HcaEncoderError("HCA bitrate is too low for the fixed FF7R profile");
        }
        for (Channel& channel : *frame.channels)
        {
            channel.scale_factors[highest_band + 1] = 0;
            channel.scale_factors[highest_band + 2] = 0;
        }
        calculate_frame_header_length(frame);
        level = binary_search_level(*frame.channels, available_bits, 0, 255);
    }

    frame.acceptable_noise_level = level;
}

void calculate_evaluation_boundary(FrameState& frame)
{
    if (frame.acceptable_noise_level == 0)
    {
        frame.evaluation_boundary = 0;
        return;
    }

    constexpr int available_bits = kFrameSize * 8;
    const int level = binary_search_boundary(*frame.channels, available_bits,
        frame.acceptable_noise_level, 0, 127);
    if (level < 0)
    {
        throw HcaEncoderError("HCA frame bit allocation failed to satisfy the fixed FF7R profile");
    }
    frame.evaluation_boundary = level;
}

void calculate_frame_resolutions(FrameState& frame)
{
    for (Channel& channel : *frame.channels)
    {
        for (int band = 0; band < frame.evaluation_boundary; ++band)
        {
            channel.resolution[band] = calculate_resolution(channel.scale_factors[band], frame.acceptable_noise_level - 1);
        }
        for (int band = frame.evaluation_boundary; band < kBandCount; ++band)
        {
            channel.resolution[band] = calculate_resolution(channel.scale_factors[band], frame.acceptable_noise_level);
        }
    }
}

void quantize_spectra(std::array<Channel, kChannels>& channels)
{
    for (Channel& channel : channels)
    {
        for (int band = 0; band < kBandCount; ++band)
        {
            const int resolution = channel.resolution[band];
            const double step_size_inv = quantizer_inverse_step_size(resolution);
            const double shift_up = step_size_inv + 1.0;
            const int shift_down = static_cast<int>(step_size_inv + 0.5);
            for (int sf = 0; sf < kSubframesPerFrame; ++sf)
            {
                channel.quantized_spectra[sf][band] =
                    static_cast<int>(channel.scaled_spectra[band][sf] * step_size_inv + shift_up) - shift_down;
            }
        }
    }
}

void write_scale_factors(BitWriter& writer, const Channel& channel)
{
    const int delta_bits = channel.scale_factor_delta_bits;
    writer.write(static_cast<std::uint32_t>(delta_bits), 3);
    if (delta_bits == 0)
    {
        return;
    }

    if (delta_bits == 6)
    {
        for (int scale : channel.scale_factors)
        {
            writer.write(static_cast<std::uint32_t>(scale), 6);
        }
        return;
    }

    writer.write(static_cast<std::uint32_t>(channel.scale_factors[0]), 6);
    const int max_delta = (1 << (delta_bits - 1)) - 1;
    const int escape_value = (1 << delta_bits) - 1;
    for (int band = 1; band < kBandCount; ++band)
    {
        const int delta = channel.scale_factors[band] - channel.scale_factors[band - 1];
        if (std::abs(delta) > max_delta)
        {
            writer.write(static_cast<std::uint32_t>(escape_value), delta_bits);
            writer.write(static_cast<std::uint32_t>(channel.scale_factors[band]), 6);
        }
        else
        {
            writer.write(static_cast<std::uint32_t>(max_delta + delta), delta_bits);
        }
    }
}

void write_spectra(BitWriter& writer, const Channel& channel, int subframe)
{
    for (int band = 0; band < kBandCount; ++band)
    {
        const int resolution = channel.resolution[band];
        const int quantized = channel.quantized_spectra[subframe][band];
        if (resolution == 0)
        {
            continue;
        }
        if (resolution < 8)
        {
            const int table_index = quantized + 8;
            writer.write(ff7r::hca::tables::quantize_spectrum_value[resolution][table_index],
                ff7r::hca::tables::quantize_spectrum_bits[resolution][table_index]);
        }
        else if (resolution < 16)
        {
            const int bits = ff7r::hca::tables::quantized_spectrum_max_bits[resolution] - 1;
            writer.write(static_cast<std::uint32_t>(std::abs(quantized)), bits);
            if (quantized != 0)
            {
                writer.write(quantized > 0 ? 0u : 1u, 1);
            }
        }
    }
}

std::vector<std::uint8_t> pack_frame(const FrameState& frame, const Crc16& crc)
{
    std::vector<std::uint8_t> out(kFrameSize, 0);
    BitWriter writer(out);
    writer.write(0xffffu, 16);
    writer.write(static_cast<std::uint32_t>(frame.acceptable_noise_level), 9);
    writer.write(static_cast<std::uint32_t>(frame.evaluation_boundary), 7);

    for (const Channel& channel : *frame.channels)
    {
        write_scale_factors(writer, channel);
    }

    for (int sf = 0; sf < kSubframesPerFrame; ++sf)
    {
        for (const Channel& channel : *frame.channels)
        {
            write_spectra(writer, channel, sf);
        }
    }

    writer.align(8);
    const int byte_position = writer.position() / 8;
    if (byte_position > kFrameSize - 2)
    {
        throw HcaEncoderError("HCA frame exceeded the fixed FF7R block size");
    }
    std::fill(out.begin() + byte_position, out.end() - 2, std::uint8_t{0});

    writer.set_position(writer.length_bits() - 16);
    writer.write(crc.compute(out.data(), out.size() - 2), 16);
    return out;
}

void encode_frame(const std::array<std::array<std::int16_t, kSamplesPerFrame>, kChannels>& pcm,
    std::array<Channel, kChannels>& channels, const Crc16& crc, std::vector<std::uint8_t>& output)
{
    pcm_to_float(pcm, channels);
    run_mdct(channels);
    calculate_scale_factors(channels);
    scale_spectra(channels);

    FrameState frame{&channels, 0, 0};
    calculate_frame_header_length(frame);
    calculate_noise_level(frame);
    calculate_evaluation_boundary(frame);
    calculate_frame_resolutions(frame);
    quantize_spectra(channels);

    std::vector<std::uint8_t> frame_bytes = pack_frame(frame, crc);
    output.insert(output.end(), frame_bytes.begin(), frame_bytes.end());
}

void write_u16_be(std::vector<std::uint8_t>& out, std::size_t& pos, std::uint16_t value)
{
    out[pos++] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    out[pos++] = static_cast<std::uint8_t>(value & 0xffu);
}

void write_u32_be(std::vector<std::uint8_t>& out, std::size_t& pos, std::uint32_t value)
{
    out[pos++] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
    out[pos++] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    out[pos++] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    out[pos++] = static_cast<std::uint8_t>(value & 0xffu);
}

void write_bytes(std::vector<std::uint8_t>& out, std::size_t& pos, const char* bytes, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        out[pos++] = static_cast<std::uint8_t>(bytes[i]);
    }
}

void write_header(
    std::vector<std::uint8_t>& output,
    const Crc16& crc,
    const std::uint32_t frame_count,
    const std::uint16_t appended_samples)
{
    std::vector<std::uint8_t> header(kHeaderSize, 0);
    std::size_t pos = 0;

    write_bytes(header, pos, "HCA\0", 4);
    write_u16_be(header, pos, 0x0200);
    write_u16_be(header, pos, kHeaderSize);

    write_bytes(header, pos, "fmt\0", 4);
    header[pos++] = kChannels;
    header[pos++] = static_cast<std::uint8_t>((kSampleRate >> 16) & 0xff);
    write_u16_be(header, pos, static_cast<std::uint16_t>(kSampleRate & 0xffff));
    write_u32_be(header, pos, frame_count);
    write_u16_be(header, pos, kInsertedSamples);
    write_u16_be(header, pos, appended_samples);

    write_bytes(header, pos, "comp", 4);
    write_u16_be(header, pos, kFrameSize);
    header[pos++] = 1;
    header[pos++] = 15;
    header[pos++] = 1;
    header[pos++] = 0;
    header[pos++] = kBandCount;
    header[pos++] = kBandCount;
    header[pos++] = 0;
    header[pos++] = 0;
    write_u16_be(header, pos, 0);

    write_bytes(header, pos, "ciph", 4);
    write_u16_be(header, pos, 0);
    write_bytes(header, pos, "pad", 3);

    if (pos > kHeaderSize - 2)
    {
        throw HcaEncoderError("HCA header exceeded the fixed FF7R header size");
    }

    const std::uint16_t header_crc = crc.compute(header.data(), kHeaderSize - 2);
    std::size_t crc_pos = kHeaderSize - 2;
    write_u16_be(header, crc_pos, header_crc);
    output.insert(output.end(), header.begin(), header.end());
}

void validate_config(const std::vector<std::int16_t>& interleaved_pcm, const HcaEncodeConfig& config)
{
    if (config.sample_rate != kSampleRate || config.channels != kChannels ||
        config.bitrate != kBitrate || config.target_samples == 0)
    {
        throw HcaEncoderError("native HCA encoder requires non-empty 48 kHz stereo PCM16 at 256 kbps");
    }

    if (config.target_samples > ff7rp::pipeline::kMaxSongFrames)
    {
        throw HcaEncoderError("native HCA input exceeds the 10 minute safety limit");
    }
    if (config.target_samples > std::numeric_limits<std::size_t>::max() / kChannels)
    {
        throw HcaEncoderError("native HCA sample count overflows addressable memory");
    }
    const std::size_t expected_values = config.target_samples * kChannels;
    if (interleaved_pcm.size() != expected_values)
    {
        throw HcaEncoderError("native HCA PCM size does not match target_samples");
    }

    static_assert(FF7R_HCA_EXPECTED_BLOCK_SIZE == kFrameSize, "FF7R HCA block size mismatch");
    static_assert(FF7R_HCA_EXPECTED_DATA_OFFSET == kHeaderSize, "FF7R HCA data offset mismatch");
}
}

HcaEncoderError::HcaEncoderError(const std::string& reason)
    : std::runtime_error(reason)
{
}

std::vector<std::uint8_t> encode_hca_48k_stereo_256k(
    const std::vector<std::int16_t>& interleaved_pcm,
    const HcaEncodeConfig& config)
{
    validate_config(interleaved_pcm, config);

    if (config.target_samples > std::numeric_limits<std::size_t>::max() - kInsertedSamples)
    {
        throw HcaEncoderError("native HCA frame count overflow");
    }
    const std::size_t encoded_samples = config.target_samples + kInsertedSamples;
    const std::size_t frame_count_size = (encoded_samples + kSamplesPerFrame - 1) / kSamplesPerFrame;
    if (frame_count_size == 0 || frame_count_size > std::numeric_limits<std::uint32_t>::max())
    {
        throw HcaEncoderError("native HCA frame count is out of range");
    }
    const auto frame_count = static_cast<std::uint32_t>(frame_count_size);
    const auto appended_samples = static_cast<std::uint16_t>(
        frame_count_size * kSamplesPerFrame - encoded_samples);
    if (frame_count_size > (std::numeric_limits<std::size_t>::max() - kHeaderSize) / kFrameSize)
    {
        throw HcaEncoderError("native HCA output size overflow");
    }
    const std::size_t expected_bytes = kHeaderSize + frame_count_size * kFrameSize;

    const Crc16 crc;
    std::vector<std::uint8_t> output;
    output.reserve(expected_bytes);
    write_header(output, crc, frame_count, appended_samples);

    std::array<Channel, kChannels> channels{};
    for (std::uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
    {
        std::array<std::array<std::int16_t, kSamplesPerFrame>, kChannels> pcm{};
        for (int sample = 0; sample < kSamplesPerFrame; ++sample)
        {
            const std::ptrdiff_t encoded_sample =
                static_cast<std::ptrdiff_t>(frame_index) * kSamplesPerFrame + sample;
            // The inserted-sample field describes the MDCT analysis/synthesis delay that the
            // decoder trims.  It is not an instruction to prepend another block of silence.
            const std::ptrdiff_t input_sample = encoded_sample;
            if (input_sample >= static_cast<std::ptrdiff_t>(config.target_samples))
            {
                continue;
            }
            for (int channel = 0; channel < kChannels; ++channel)
            {
                pcm[channel][sample] = interleaved_pcm[static_cast<std::size_t>(input_sample) * kChannels + channel];
            }
        }
        encode_frame(pcm, channels, crc, output);
    }

    if (output.size() != expected_bytes)
    {
        throw HcaEncoderError("native HCA encoder did not produce the computed byte count");
    }
    return output;
}
