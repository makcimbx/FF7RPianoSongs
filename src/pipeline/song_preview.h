#pragma once

#include "song_types.h"
#include <array>
#include <filesystem>
#include <span>

namespace ff7rp::pipeline {

inline constexpr std::uint32_t kSongPreviewSampleRate = 48000;
inline constexpr std::uint16_t kSongPreviewChannels = 2;
inline constexpr std::uint32_t kSongPreviewMaxDurationSeconds = 20;
inline constexpr std::uint32_t kSongPreviewMaxFrames =
    kSongPreviewSampleRate * kSongPreviewMaxDurationSeconds;
inline constexpr std::size_t kSongPreviewSpectrumBands = 16;
inline constexpr std::uint32_t kSongPreviewSpectrumHop = kSongPreviewSampleRate / 30;
inline constexpr char kSongPreviewIdentity[] = "preview=clean_base:sustained_rms_half_second:interior:pcm16_fade250ms:wave64:v1";

struct SongPreviewArtifact {
    std::filesystem::path path;
    std::uint64_t content_hash = 0;
    std::uint32_t frame_count = 0;
    double source_start_seconds = 0.0;
    std::array<float, 64> waveform{};
};

Status prepare_song_preview(const WavAudio& audio, const std::filesystem::path& cache_directory,
    std::shared_ptr<const SongPreviewArtifact>* out_preview);
// Bounded cache I/O for offline/background callers, never a game callback.
Status read_song_preview_pcm(const SongPreviewArtifact& artifact, std::vector<std::int16_t>* out_samples);
// Background-only: per-hop band levels (0-255), instant attack with decaying release.
std::vector<std::uint8_t> song_preview_spectrum(std::span<const std::int16_t> samples);

} // namespace ff7rp::pipeline
