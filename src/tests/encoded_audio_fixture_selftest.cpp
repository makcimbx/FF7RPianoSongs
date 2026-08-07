#include "pipeline/audio_reader.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

using ff7rp::pipeline::WavAudio;
double rms(const WavAudio& audio, std::size_t begin, std::size_t end) {
    double sum = 0.0; std::size_t count = 0;
    for (std::size_t frame = begin; frame < std::min(end, audio.frame_count()); ++frame) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const double value = audio.stereo_samples[frame * 2 + channel]; sum += value * value; ++count;
        }
    }
    return count ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}
int main() {
    const std::filesystem::path root = FF7RP_ENCODED_AUDIO_FIXTURE_ROOT;
    WavAudio source;
    const auto source_status = ff7rp::pipeline::read_audio_file((root / "source.wav").string(), &source);
    if (!source_status.ok() || source.sample_rate != 48000 || source.channels != 2 || source.frame_count() != 48000) {
        std::cerr << "source WAV geometry/decode changed: " << source_status.message << '\n'; return 1;
    }
    for (const char* name : {"fixture.mp3", "fixture.flac"}) {
        WavAudio decoded;
        const auto status = ff7rp::pipeline::read_audio_file((root / name).string(), &decoded);
        if (!status.ok()) { std::cerr << name << " decode failed: " << status.message << '\n'; return 1; }
        if (decoded.sample_rate != 48000 || decoded.channels != 2 || decoded.source_frame_count != 48000 || decoded.frame_count() != 48000) {
            std::cerr << name << " geometry changed: " << decoded.frame_count() << '\n'; return 1;
        }
        const double leading = rms(decoded, 0, 4000), body = rms(decoded, 6000, 42000);
        const double trailing = rms(decoded, 44000, std::min<std::size_t>(decoded.frame_count(), 48000));
        if (!(leading < 0.01 && body > 0.15 && trailing < 0.01 && body > leading * 20.0)) {
            std::cerr << name << " timing/gain witness changed\n"; return 1;
        }
    }
    std::cout << "encoded audio fixture self-test passed\n"; return 0;
}
