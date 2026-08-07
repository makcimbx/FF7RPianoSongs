#include "wav_reader.h"
#include "audio_reader.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace ff7rp::pipeline {
namespace {

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

bool fourcc_equals(const std::vector<std::uint8_t>& bytes, std::size_t offset, const char (&tag)[5]) {
    return offset + 4 <= bytes.size() && std::memcmp(bytes.data() + offset, tag, 4) == 0;
}

float read_f32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    static_assert(sizeof(float) == 4, "float32 is required");
    std::uint32_t raw = read_u32(bytes, offset);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

Status fail(const std::string& message) {
    return Status::error(StatusCode::InvalidWav, message);
}

} // namespace

Status parse_wav_bytes(const std::vector<std::uint8_t>& bytes, WavAudio* out_audio) {
    if (!out_audio) {
        return Status::error(StatusCode::InvalidArgument, "out_audio must not be null");
    }
    if (bytes.size() > kMaxWavFileBytes) {
        return fail("WAV exceeds the 512 MiB safety limit");
    }
    if (bytes.size() < 12 || !fourcc_equals(bytes, 0, "RIFF") || !fourcc_equals(bytes, 8, "WAVE")) {
        return fail("WAV must be a RIFF/WAVE file");
    }

    bool saw_fmt = false;
    bool saw_data = false;
    std::uint16_t format_tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint16_t block_align = 0;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;

    std::size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const std::size_t chunk_start = offset;
        const std::uint32_t chunk_size_u32 = read_u32(bytes, offset + 4);
        const std::size_t chunk_size = static_cast<std::size_t>(chunk_size_u32);
        offset += 8;
        if (offset + chunk_size > bytes.size()) {
            return fail("WAV chunk overruns file at byte " + std::to_string(chunk_start));
        }

        if (fourcc_equals(bytes, chunk_start, "fmt ")) {
            if (chunk_size < 16) {
                return fail("WAV fmt chunk is too small");
            }
            format_tag = read_u16(bytes, offset);
            channels = read_u16(bytes, offset + 2);
            sample_rate = read_u32(bytes, offset + 4);
            block_align = read_u16(bytes, offset + 12);
            bits_per_sample = read_u16(bytes, offset + 14);
            saw_fmt = true;
        } else if (fourcc_equals(bytes, chunk_start, "data")) {
            data_offset = offset;
            data_size = chunk_size;
            saw_data = true;
        }

        offset += chunk_size + (chunk_size & 1U);
    }

    if (!saw_fmt) {
        return fail("WAV is missing fmt chunk");
    }
    if (!saw_data) {
        return fail("WAV is missing data chunk");
    }
    if (sample_rate != kSongWavSampleRate) {
        return fail("WAV sample rate must be 48000 Hz");
    }
    if (channels != 1 && channels != 2) {
        return fail("WAV channel count must be mono or stereo");
    }
    if (!((format_tag == 1 && bits_per_sample == 16) || (format_tag == 3 && bits_per_sample == 32))) {
        return fail("WAV format must be PCM16 or IEEE float32");
    }

    const std::uint16_t expected_block_align = static_cast<std::uint16_t>(channels * (bits_per_sample / 8));
    if (block_align != expected_block_align || block_align == 0) {
        return fail("WAV block align does not match channel/sample format");
    }
    if (data_size % block_align != 0) {
        return fail("WAV data size is not aligned to complete frames");
    }

    const std::size_t input_frames = data_size / block_align;
    if (input_frames == 0) {
        return fail("WAV data chunk must contain at least one frame");
    }
    if (input_frames > kMaxSongFrames) {
        return fail("WAV exceeds the 10 minute safety limit");
    }
    if (input_frames > std::numeric_limits<std::size_t>::max() / 2) {
        return fail("WAV frame count overflows addressable memory");
    }

    WavAudio audio;
    audio.sample_rate = kSongWavSampleRate;
    audio.channels = 2;
    audio.source_frame_count = input_frames;
    audio.stereo_samples.assign(input_frames * 2, 0.0f);

    for (std::size_t frame = 0; frame < input_frames; ++frame) {
        const std::size_t frame_offset = data_offset + frame * block_align;
        float left = 0.0f;
        float right = 0.0f;
        if (format_tag == 1) {
            const auto sample0 = static_cast<std::int16_t>(read_u16(bytes, frame_offset));
            left = static_cast<float>(sample0) / 32768.0f;
            if (channels == 2) {
                const auto sample1 = static_cast<std::int16_t>(read_u16(bytes, frame_offset + 2));
                right = static_cast<float>(sample1) / 32768.0f;
            } else {
                right = left;
            }
        } else {
            left = read_f32(bytes, frame_offset);
            right = channels == 2 ? read_f32(bytes, frame_offset + 4) : left;
        }
        audio.stereo_samples[frame * 2] = left;
        audio.stereo_samples[frame * 2 + 1] = right;
    }

    Status validation = validate_decoded_pcm(audio);
    if (!validation.ok()) return validation;
    *out_audio = std::move(audio);
    return Status::ok_status();
}

Status read_wav_file(const std::string& path, WavAudio* out_audio) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return Status::error(StatusCode::NotFound, "failed to open WAV: " + path);
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return Status::error(StatusCode::IoError, "failed to determine WAV size: " + path);
    }
    if (static_cast<std::uint64_t>(size) > kMaxWavFileBytes) {
        return Status::error(StatusCode::InvalidWav, "WAV exceeds the 512 MiB safety limit: " + path);
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return Status::error(StatusCode::IoError, "failed to read WAV: " + path);
    }
    Status status = parse_wav_bytes(bytes, out_audio);
    if (!status.ok()) {
        status.message = path + ": " + status.message;
    }
    return status;
}

} // namespace ff7rp::pipeline
