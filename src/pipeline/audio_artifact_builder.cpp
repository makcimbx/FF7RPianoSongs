#include "audio_artifact_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
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
    const WavAudio& clean_audio,
    const WavAudio* metronome_mode0_audio,
    const bool adaptive_metronome,
    const AudioArtifactBuildTrace& trace) {
    const auto report = [&](const char* stage) {
        if (trace) trace(stage);
    };
    try {
        HcaEncodeConfig config;
        config.sample_rate = kSongWavSampleRate;
        config.channels = 2;
        config.bitrate = 256000;
        config.target_samples = clean_audio.frame_count();

        report("hca_clean_pcm_started");
        const std::vector<std::int16_t> pcm = pcm16_from_wav(clean_audio);
        report("hca_clean_encode_started");
        const std::vector<std::uint8_t> clean_hca = encode_hca_48k_stereo_256k(pcm, config);
        report("hca_clean_ready");
        std::vector<std::uint8_t> mode0_hca;
        if (adaptive_metronome) {
            if (!metronome_mode0_audio ||
                metronome_mode0_audio->frame_count() != clean_audio.frame_count()) {
                MabfBuildResult result;
                result.status = Status::error(StatusCode::MabfNotReleaseValid,
                    "metronome Mode0 audio is missing or has mismatched duration");
                return result;
            }
            report("hca_mode0_pcm_started");
            const std::vector<std::int16_t> mode0_pcm = pcm16_from_wav(*metronome_mode0_audio);
            report("hca_mode0_encode_started");
            mode0_hca = encode_hca_48k_stereo_256k(mode0_pcm, config);
            report("hca_mode0_ready");
        }
        const std::vector<std::uint8_t>& mode0_or_clean = adaptive_metronome ? mode0_hca : clean_hca;
        const MabfModeHcaPayloads mode_hca{&mode0_or_clean, &clean_hca, &clean_hca};
        report("mabf_build_started");
        return build_mabf_from_mode_hca(mode_hca);
    } catch (const std::exception& error) {
        MabfBuildResult result;
        result.status = Status::error(StatusCode::HcaUnavailable, error.what());
        return result;
    }
}

} // namespace ff7rp::pipeline
