#include "audio_artifact_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <array>
#include <vector>

#include "hca/hca_encoder.h"
#include "wav_reader.h"

namespace ff7rp::pipeline {
namespace {

std::vector<std::int16_t> pcm16_from_wav(const WavAudio& audio) {
    std::vector<std::int16_t> pcm;
    pcm.reserve(audio.stereo_samples.size());
    for (float sample : audio.stereo_samples) {
        const float clamped = std::max(-1.0f, std::min(1.0f, sample));
        const float scaled = clamped < 0.0f ? clamped * 32768.0f : clamped * 32767.0f;
        pcm.push_back(static_cast<std::int16_t>(std::lrintf(scaled)));
    }
    return pcm;
}

} // namespace

MabfBuildResult build_audio_mabf(
    const AudioMabfInputs& inputs,
    const AudioArtifactBuildTrace& trace) {
    const auto report = [&](const char* stage) {
        if (trace) trace(stage);
    };
    try {
        std::array<const WavAudio*, 3> effective_modes{
            &inputs.resolved_modes[0].get(), &inputs.resolved_modes[1].get(), &inputs.resolved_modes[2].get()};
        if (inputs.mode0_guide) effective_modes[0] = &inputs.mode0_guide->get();
        const std::size_t logical_frames = inputs.resolved_modes[0].get().frame_count();
        if (logical_frames == 0) {
            MabfBuildResult result;
            result.status = Status::error(
                StatusCode::MabfNotReleaseValid, "resolved audio must contain at least one logical frame");
            return result;
        }
        for (std::size_t mode = 0; mode < effective_modes.size(); ++mode) {
            const WavAudio& audio = *effective_modes[mode];
            if (audio.sample_rate != kSongWavSampleRate || audio.channels != 2 ||
                audio.frame_count() != logical_frames || audio.source_frame_count != logical_frames) {
                MabfBuildResult result;
                result.status = Status::error(StatusCode::MabfNotReleaseValid,
                    "resolved Mode" + std::to_string(mode) + " audio has mismatched 48 kHz stereo frame geometry");
                return result;
            }
        }

        HcaEncodeConfig config;
        config.sample_rate = kSongWavSampleRate;
        config.channels = 2;
        config.bitrate = 256000;
        config.target_samples = logical_frames;

        std::array<std::vector<std::uint8_t>, 3> encoded;
        std::array<const std::vector<std::uint8_t>*, 3> payloads{};
        for (std::size_t mode = 0; mode < effective_modes.size(); ++mode) {
            for (std::size_t prior = 0; prior < mode; ++prior) {
                if (effective_modes[prior] == effective_modes[mode] ||
                    (effective_modes[prior]->sample_rate == effective_modes[mode]->sample_rate &&
                     effective_modes[prior]->channels == effective_modes[mode]->channels &&
                     effective_modes[prior]->source_frame_count == effective_modes[mode]->source_frame_count &&
                     effective_modes[prior]->stereo_samples == effective_modes[mode]->stereo_samples)) {
                    payloads[mode] = payloads[prior];
                    break;
                }
            }
            if (payloads[mode]) continue;
            const std::string pcm_stage = "hca_mode" + std::to_string(mode) + "_pcm_started";
            report(pcm_stage.c_str());
            const std::vector<std::int16_t> pcm = pcm16_from_wav(*effective_modes[mode]);
            const std::string encode_stage = "hca_mode" + std::to_string(mode) + "_encode_started";
            report(encode_stage.c_str());
            encoded[mode] = encode_hca_48k_stereo_256k(pcm, config);
            payloads[mode] = &encoded[mode];
            const std::string ready_stage = "hca_mode" + std::to_string(mode) + "_ready";
            report(ready_stage.c_str());
        }
        const MabfModeHcaPayloads mode_hca{payloads[0], payloads[1], payloads[2]};
        report("mabf_build_started");
        return build_mabf_from_mode_hca(mode_hca);
    } catch (const std::exception& error) {
        MabfBuildResult result;
        result.status = Status::error(StatusCode::HcaUnavailable, error.what());
        return result;
    }
}

} // namespace ff7rp::pipeline
