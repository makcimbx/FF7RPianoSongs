#pragma once

#include <cstdint>
#include <vector>

namespace ff7r::piano::core {

struct PreviewAudioPosition {
    bool playing = false;
    bool failed = false;
    std::uint32_t played_frames = 0;
    std::uint32_t total_frames = 0;
};

// One independent 48 kHz stereo PCM16 stream; no game audio API or global mute.
// Gain is a finite linear attenuation in [0, 1], applied before queueing PCM.
bool play_preview_audio(std::vector<std::int16_t>&& samples, float gain = 1.0f) noexcept;
bool stop_preview_audio() noexcept;
// Short fade-out of unplayed audio, then silence; the device stays owned until the
// next play or stop, so transitions that must claim silence still use stop.
bool fade_preview_audio() noexcept;
PreviewAudioPosition preview_audio_position() noexcept;

} // namespace ff7r::piano::core
