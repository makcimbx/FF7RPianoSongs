#include "audio_reader.h"

#include <array>
#include <filesystem>
#include <cmath>
#include <limits>

#include "pipeline_limits.h"
#include "wav_reader.h"

#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace ff7rp::pipeline {
namespace {

Status decode_initialized(ma_decoder* decoder, WavAudio* out_audio) {
    if (!decoder || !out_audio) {
        return Status::error(StatusCode::InvalidArgument, "decoder and out_audio must not be null");
    }

    constexpr ma_uint64 kReadFrames = 4096;
    std::array<float, static_cast<std::size_t>(kReadFrames) * 2> block{};
    std::vector<float> samples;
    while (true) {
        ma_uint64 frames_read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(decoder, block.data(), kReadFrames, &frames_read);
        if (result != MA_SUCCESS && result != MA_AT_END) {
            return Status::error(StatusCode::InvalidAudio,
                std::string("audio decode failed: ") + ma_result_description(result));
        }
        if (frames_read > kMaxSongFrames - samples.size() / 2) {
            return Status::error(StatusCode::InvalidAudio, "audio exceeds the 10 minute safety limit");
        }
        samples.insert(samples.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(frames_read * 2));
        if (result == MA_AT_END || frames_read == 0) {
            break;
        }
    }
    if (samples.empty()) {
        return Status::error(StatusCode::InvalidAudio, "audio contains no decoded PCM frames");
    }

    WavAudio audio;
    audio.sample_rate = kSongWavSampleRate;
    audio.channels = 2;
    audio.source_frame_count = samples.size() / 2;
    audio.stereo_samples = std::move(samples);
    const Status validation = validate_decoded_pcm(audio);
    if (!validation.ok()) return validation;
    *out_audio = std::move(audio);
    return Status::ok_status();
}

ma_decoder_config decoder_config() {
    return ma_decoder_config_init(ma_format_f32, 2, kSongWavSampleRate);
}

} // namespace

Status validate_decoded_pcm(const WavAudio& audio) {
    if (audio.sample_rate != kSongWavSampleRate || audio.channels != 2 ||
        audio.stereo_samples.empty() || (audio.stereo_samples.size() & 1u) != 0) {
        return Status::error(StatusCode::InvalidAudio, "decoded PCM must be non-empty 48 kHz stereo");
    }
    for (const float sample : audio.stereo_samples) {
        if (!std::isfinite(sample)) {
            return Status::error(StatusCode::InvalidAudio, "decoded PCM contains NaN or infinity");
        }
        if (sample < -1.0f || sample > 1.0f) {
            return Status::error(StatusCode::InvalidAudio, "decoded PCM sample is outside normalized [-1, 1] range");
        }
    }
    return Status::ok_status();
}

Status decode_audio_bytes(const std::vector<std::uint8_t>& bytes, WavAudio* out_audio) {
    if (!out_audio) {
        return Status::error(StatusCode::InvalidArgument, "out_audio must not be null");
    }
    if (bytes.empty()) {
        return Status::error(StatusCode::InvalidAudio, "audio input must not be empty");
    }
    if (bytes.size() > kMaxWavFileBytes) {
        return Status::error(StatusCode::InvalidAudio, "audio exceeds the 512 MiB safety limit");
    }

    ma_decoder decoder{};
    ma_decoder_config config = decoder_config();
    const ma_result result = ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder);
    if (result != MA_SUCCESS) {
        return Status::error(StatusCode::InvalidAudio,
            std::string("unsupported or invalid WAV/MP3/FLAC data: ") + ma_result_description(result));
    }
    Status status = decode_initialized(&decoder, out_audio);
    ma_decoder_uninit(&decoder);
    return status;
}

Status read_audio_file(const std::string& path, WavAudio* out_audio) {
    if (!out_audio) {
        return Status::error(StatusCode::InvalidArgument, "out_audio must not be null");
    }
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return Status::error(StatusCode::NotFound, "failed to open audio source: " + path);
    }
    if (size > kMaxWavFileBytes) {
        return Status::error(StatusCode::InvalidAudio, "audio exceeds the 512 MiB safety limit: " + path);
    }

    ma_decoder decoder{};
    ma_decoder_config config = decoder_config();
    const ma_result result = ma_decoder_init_file(path.c_str(), &config, &decoder);
    if (result != MA_SUCCESS) {
        return Status::error(StatusCode::InvalidAudio,
            path + ": unsupported or invalid WAV/MP3/FLAC audio: " + ma_result_description(result));
    }
    Status status = decode_initialized(&decoder, out_audio);
    ma_decoder_uninit(&decoder);
    if (!status.ok()) {
        status.message = path + ": " + status.message;
    }
    return status;
}

} // namespace ff7rp::pipeline
