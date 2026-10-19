#include "pipeline/song_preview.h"
#include "pipeline/native_asset_hash.h"
#include "tests/test_support.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ff7rp::pipeline;
void check(bool value) { if (!value) throw std::runtime_error("song preview check failed"); }
int main() {
    try {
        ff7rp::tests::TemporaryDirectory root("song-preview");
        for (std::size_t frames : {1u, 2u, 63u, 4800u, kSongPreviewMaxFrames}) {
            WavAudio audio;
            audio.source_frame_count = frames;
            audio.stereo_samples.resize(frames * 2);
            std::shared_ptr<const SongPreviewArtifact> preview;
            check(prepare_song_preview(audio, root.path(), &preview).ok());
            check(preview && preview->frame_count == frames && preview->source_start_seconds == 0);
            check(std::all_of(preview->waveform.begin(), preview->waveform.end(), [](float x) { return x == 0; }));
            std::vector<std::int16_t> samples;
            check(read_song_preview_pcm(*preview, &samples).ok() && samples.size() == frames * 2);
        }
        WavAudio audio;
        audio.source_frame_count = 60 * 48000;
        audio.stereo_samples.resize(audio.source_frame_count * 2);
        for (std::size_t f = 20 * 48000; f < 40 * 48000; ++f) {
            audio.stereo_samples[f * 2] = float(0.3 * std::sin(double(f) * 0.05));
            audio.stereo_samples[f * 2 + 1] = -audio.stereo_samples[f * 2];
        }
        audio.stereo_samples[10 * 48000 * 2] = 1; // An isolated peak must not win.
        std::shared_ptr<const SongPreviewArtifact> first, second;
        check(prepare_song_preview(audio, root.path(), &first).ok());
        check(prepare_song_preview(audio, root.path(), &second).ok());
        check(first->source_start_seconds == 20 && first->frame_count == kSongPreviewMaxFrames);
        check(first->path == second->path && first->waveform == second->waveform && first->content_hash == second->content_hash);
        std::vector<std::int16_t> samples;
        check(read_song_preview_pcm(*first, &samples).ok());
        check(samples.front() == 0 && samples.back() == 0);
        check(*std::max_element(first->waveform.begin(), first->waveform.end()) > 0.15f);
        std::vector<std::int16_t> tone(kSongPreviewMaxFrames * 2);
        for (std::size_t i = 0; i < tone.size(); ++i) // 382 Hz, inside the 303-434 Hz band.
            tone[i] = static_cast<std::int16_t>(std::lround(9830 * std::sin(double(i / 2) * 0.05)));
        const auto spectrum = song_preview_spectrum(tone);
        const auto mid = 300 * kSongPreviewSpectrumBands;
        check(spectrum.size() == 600 * kSongPreviewSpectrumBands);
        check(spectrum[mid + 5] >= 250 && spectrum[mid + 1] == 0 && spectrum[mid + 15] == 0);
        check(std::ranges::all_of(song_preview_spectrum(std::vector<std::int16_t>(9600)),
            [](std::uint8_t level) { return level == 0; })); // Silence stays dark.
        check(song_preview_spectrum({}).empty());
        std::ifstream input(first->path, std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        input.close();
        const auto hash = [](const auto& data) {
            const auto sha = native_sha256(data);
            std::uint64_t result = 0;
            for (unsigned i = 0; i < 8; ++i) result |= std::uint64_t(sha[i]) << (8 * i);
            return result;
        };
        check(hash(bytes) == first->content_hash);
        auto header_corruption = *first;
        bytes[8] ^= 1;
        header_corruption.content_hash = hash(bytes);
        { std::ofstream file(first->path, std::ios::binary | std::ios::trunc);
          file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
        check(!read_song_preview_pcm(header_corruption, &samples).ok());
        bytes[8] ^= 1;
        { std::ofstream file(first->path, std::ios::binary | std::ios::trunc);
          file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
        auto invalid = *first;
        invalid.frame_count = kSongPreviewMaxFrames + 1;
        check(!read_song_preview_pcm(invalid, &samples).ok() && samples.empty());
        invalid = *first; invalid.source_start_seconds = std::numeric_limits<double>::infinity();
        check(!read_song_preview_pcm(invalid, &samples).ok());
        invalid = *first; invalid.content_hash ^= 1;
        check(!read_song_preview_pcm(invalid, &samples).ok());
        invalid = *first; invalid.waveform[0] = std::numeric_limits<float>::quiet_NaN();
        check(!read_song_preview_pcm(invalid, &samples).ok());
        audio.stereo_samples[0] = std::numeric_limits<float>::quiet_NaN();
        check(!prepare_song_preview(audio, root.path(), &second).ok() && !second);
        { std::fstream file(first->path, std::ios::binary | std::ios::in | std::ios::out);
          file.seekp(40); file.put('\x7f'); }
        check(!read_song_preview_pcm(*first, &samples).ok());
        std::filesystem::resize_file(first->path, 35);
        check(!read_song_preview_pcm(*first, &samples).ok());
        std::filesystem::resize_file(first->path, 32ull + 4ull * (kSongPreviewMaxFrames + 1));
        check(!read_song_preview_pcm(*first, &samples).ok());
        audio.stereo_samples[0] = 0;
        check(prepare_song_preview(audio, root.path(), &second).ok());
        check(second->content_hash == first->content_hash && read_song_preview_pcm(*second, &samples).ok());
        std::cout << "song_preview_selftest passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
