#pragma once

#include "core/config.h"
#include "core/preview_audio.h"
#include "game/song_preview_policy.h"
#include "game/song_registry.h"
#include "pipeline/song_preview.h"

#include <array>

namespace ff7r::piano::game {

void configure_song_preview(core::PreviewMode mode, int function_key) noexcept;
int preview_function_key() noexcept;
// Existing title input owner supplies focus and merged keyboard/R3-only facts.
void poll_song_preview(bool focused, bool manual_held, bool autoplay_chord_held) noexcept;
// Invalidates pending loads before native transition. False retains live driver
// ownership and must reject new gameplay admission, not claim silence.
// fade leaves the device ramping out briefly; only for in-menu navigation.
bool cancel_song_preview(const char* reason, bool fade = false) noexcept;
bool shutdown_song_preview() noexcept;
core::PreviewAudioPosition song_preview_position() noexcept;

struct SongPreviewRenderSnapshot {
    SelectionSnapshot selection;
    std::uint64_t session_generation = 0;
    std::uint64_t ticket = 0;
    PreviewPhase phase = PreviewPhase::Idle;
    core::PreviewMode mode = core::PreviewMode::Off;
    core::PreviewAudioPosition position;
    // Band levels 0-1 at the device position; zero unless playing.
    std::array<float, ff7rp::pipeline::kSongPreviewSpectrumBands> spectrum{};
    explicit operator bool() const noexcept {
        return session_generation && selection && selection.storage && selection.song->preview
            && mode != core::PreviewMode::Off;
    }
};
// Fresh, owning snapshot only; never retain it or row bounds across render frames.
SongPreviewRenderSnapshot capture_song_preview_render_snapshot() noexcept;

} // namespace ff7r::piano::game
