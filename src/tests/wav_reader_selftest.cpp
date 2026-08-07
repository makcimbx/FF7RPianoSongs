#include "pipeline/wav_reader.h"
#include "pipeline/audio_reader.h"
#include "pipeline/audio_loudness.h"

#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

void write_u16_le(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
}

void write_u32_le(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::vector<std::uint8_t> pcm16_wav(const std::uint16_t channels, const std::uint32_t frames)
{
    const std::uint16_t block_align = static_cast<std::uint16_t>(channels * 2u);
    const std::uint32_t data_size = frames * block_align;
    std::vector<std::uint8_t> bytes(44u + data_size, 0);
    bytes[0] = 'R'; bytes[1] = 'I'; bytes[2] = 'F'; bytes[3] = 'F';
    write_u32_le(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8u));
    bytes[8] = 'W'; bytes[9] = 'A'; bytes[10] = 'V'; bytes[11] = 'E';
    bytes[12] = 'f'; bytes[13] = 'm'; bytes[14] = 't'; bytes[15] = ' ';
    write_u32_le(bytes, 16, 16);
    write_u16_le(bytes, 20, 1);
    write_u16_le(bytes, 22, channels);
    write_u32_le(bytes, 24, 48000);
    write_u32_le(bytes, 28, 48000u * block_align);
    write_u16_le(bytes, 32, block_align);
    write_u16_le(bytes, 34, 16);
    bytes[36] = 'd'; bytes[37] = 'a'; bytes[38] = 't'; bytes[39] = 'a';
    write_u32_le(bytes, 40, data_size);
    return bytes;
}

std::vector<std::uint8_t> float32_wav(const float sample)
{
    std::vector<std::uint8_t> bytes(52u, 0);
    bytes[0] = 'R'; bytes[1] = 'I'; bytes[2] = 'F'; bytes[3] = 'F';
    write_u32_le(bytes, 4, 44u);
    bytes[8] = 'W'; bytes[9] = 'A'; bytes[10] = 'V'; bytes[11] = 'E';
    bytes[12] = 'f'; bytes[13] = 'm'; bytes[14] = 't'; bytes[15] = ' ';
    write_u32_le(bytes, 16, 16u);
    write_u16_le(bytes, 20, 3u);
    write_u16_le(bytes, 22, 2u);
    write_u32_le(bytes, 24, 48000u);
    write_u32_le(bytes, 28, 48000u * 8u);
    write_u16_le(bytes, 32, 8u);
    write_u16_le(bytes, 34, 32u);
    bytes[36] = 'd'; bytes[37] = 'a'; bytes[38] = 't'; bytes[39] = 'a';
    write_u32_le(bytes, 40, 8u);
    std::memcpy(bytes.data() + 44u, &sample, sizeof(sample));
    std::memcpy(bytes.data() + 48u, &sample, sizeof(sample));
    return bytes;
}

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    ff7rp::pipeline::WavAudio audio;
    auto status = ff7rp::pipeline::parse_wav_bytes(pcm16_wav(1, 1), &audio);
    if (!status.ok() || audio.frame_count() != 1 || audio.source_frame_count != 1 || audio.stereo_samples.size() != 2) {
        return fail("one-frame mono WAV was not preserved exactly");
    }

    status = ff7rp::pipeline::parse_wav_bytes(pcm16_wav(2, 48000), &audio);
    if (!status.ok() || audio.frame_count() != 48000 || audio.source_frame_count != 48000 ||
        audio.stereo_samples.size() != 96000) {
        return fail("one-second stereo WAV was padded or truncated");
    }

    status = ff7rp::pipeline::parse_wav_bytes(pcm16_wav(2, 0), &audio);
    if (status.ok()) {
        return fail("zero-frame WAV was accepted");
    }

    auto malformed = pcm16_wav(2, 1);
    malformed.resize(43);
    status = ff7rp::pipeline::parse_wav_bytes(malformed, &audio);
    if (status.ok()) {
        return fail("truncated WAV was accepted");
    }

    status = ff7rp::pipeline::decode_audio_bytes(pcm16_wav(1, 48000), &audio);
    if (!status.ok() || audio.frame_count() != 48000 || audio.channels != 2 || audio.sample_rate != 48000) {
        return fail("generic audio decoder did not normalize mono WAV to 48 kHz stereo");
    }
    for (const float invalid_sample : {std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 1.0001f, -1.0001f}) {
        const auto bytes = float32_wav(invalid_sample);
        if (ff7rp::pipeline::parse_wav_bytes(bytes, &audio).ok() ||
            ff7rp::pipeline::decode_audio_bytes(bytes, &audio).ok()) {
            return fail("nonfinite or out-of-range decoded PCM was accepted");
        }
    }
    for (const float boundary_sample : {-1.0f, 1.0f}) {
        if (!ff7rp::pipeline::parse_wav_bytes(float32_wav(boundary_sample), &audio).ok()) {
            return fail("finite PCM boundary sample was rejected");
        }
    }

    using ff7rp::pipeline::GainEnvelopePoint;
    const std::vector<GainEnvelopePoint> measured_envelope{{0.0, 5.0}, {45.0, 5.0}, {60.0, 0.0}};
    status = ff7rp::pipeline::validate_gain_envelope(measured_envelope);
    const double boosted = std::pow(10.0, 5.0 / 20.0);
    if (!status.ok() ||
        std::fabs(ff7rp::pipeline::gain_envelope_amplitude_at(measured_envelope, -1.0) - boosted) > 1e-12 ||
        std::fabs(ff7rp::pipeline::gain_envelope_amplitude_at(measured_envelope, 45.0) - boosted) > 1e-12 ||
        std::fabs(ff7rp::pipeline::gain_envelope_amplitude_at(measured_envelope, 52.5) -
            (boosted + 1.0) * 0.5) > 1e-12 ||
        std::fabs(ff7rp::pipeline::gain_envelope_amplitude_at(measured_envelope, 60.0) - 1.0) > 1e-12 ||
        std::fabs(ff7rp::pipeline::gain_envelope_amplitude_at(measured_envelope, 90.0) - 1.0) > 1e-12) {
        return fail("gain envelope boundary or amplitude interpolation behavior changed");
    }
    for (const auto& invalid : std::vector<std::vector<GainEnvelopePoint>>{
            {{-0.1, 0.0}}, {{0.0, 12.1}}, {{0.0, -12.1}},
            {{1.0, 0.0}, {0.5, 0.0}}, {{1.0, 0.0}, {1.0, 1.0}},
            {{std::numeric_limits<double>::infinity(), 0.0}},
            {{0.0, std::numeric_limits<double>::quiet_NaN()}}}) {
        if (ff7rp::pipeline::validate_gain_envelope(invalid).ok()) {
            return fail("invalid gain envelope was accepted");
        }
    }
    if (ff7rp::pipeline::validate_gain_envelope(
            std::vector<GainEnvelopePoint>(ff7rp::pipeline::kMaximumGainEnvelopePoints + 1)).ok()) {
        return fail("oversized gain envelope was accepted");
    }

    ff7rp::pipeline::WavAudio envelope_audio;
    envelope_audio.sample_rate = 48000;
    envelope_audio.channels = 2;
    envelope_audio.source_frame_count = 48001;
    envelope_audio.stereo_samples.resize(envelope_audio.source_frame_count * 2);
    for (std::size_t frame = 0; frame < envelope_audio.frame_count(); ++frame) {
        envelope_audio.stereo_samples[frame * 2] = 0.1f;
        envelope_audio.stereo_samples[frame * 2 + 1] = -0.2f;
    }
    const std::size_t original_frames = envelope_audio.frame_count();
    const auto original_samples = envelope_audio.stereo_samples;
    ff7rp::pipeline::AudioLoudnessStats passthrough_stats;
    status = ff7rp::pipeline::normalize_audio_loudness(
        &envelope_audio, {}, false, -13.0, -1.0, &passthrough_stats);
    if (!status.ok() || envelope_audio.stereo_samples != original_samples ||
        passthrough_stats.gain_envelope_applied || passthrough_stats.gain_envelope_point_count != 0) {
        return fail("missing gain envelope changed disabled-normalization PCM behavior");
    }
    status = ff7rp::pipeline::apply_gain_envelope(&envelope_audio, {});
    if (!status.ok() || envelope_audio.stereo_samples != original_samples) {
        return fail("missing gain envelope was not bit-for-bit sample equivalent");
    }
    status = ff7rp::pipeline::apply_gain_envelope(&envelope_audio, {{0.0, 6.0}, {1.0, 0.0}});
    if (!status.ok() || envelope_audio.frame_count() != original_frames ||
        envelope_audio.source_frame_count != original_frames ||
        std::fabs(envelope_audio.stereo_samples.front() * 2.0f + envelope_audio.stereo_samples[1]) > 1e-6f ||
        std::fabs(envelope_audio.stereo_samples[48000 * 2] - 0.1f) > 1e-6f ||
        std::fabs(envelope_audio.stereo_samples[48000 * 2 + 1] + 0.2f) > 1e-6f) {
        return fail("gain envelope changed frame alignment or broke stereo-linked identity");
    }

    audio.source_frame_count = 96000;
    audio.stereo_samples.resize(audio.source_frame_count * 2);
    for (std::size_t frame = 0; frame < audio.source_frame_count; ++frame) {
        const float sample = static_cast<float>(0.1 * std::sin(2.0 * 3.141592653589793 * 1000.0 * frame / 48000.0));
        audio.stereo_samples[frame * 2] = sample;
        audio.stereo_samples[frame * 2 + 1] = sample;
    }
    ff7rp::pipeline::AudioLoudnessStats loudness;
    status = ff7rp::pipeline::normalize_audio_loudness(&audio, {}, true, -13.0, -1.0, &loudness);
    if (!status.ok() || std::fabs(loudness.output_lufs - (-13.0)) > 0.3 || loudness.applied_gain_db < 0.5 ||
        loudness.output_peak_dbfs > -0.99) {
        return fail("automatic loudness normalization did not reach the requested safe target");
    }

    std::cout << "wav_reader_selftest ok\n";
    return 0;
}
