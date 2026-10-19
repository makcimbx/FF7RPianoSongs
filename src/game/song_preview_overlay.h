#pragma once

#include "game/song_preview.h"
#include "game/startup_cache_overlay.h"

#include <optional>

namespace ff7r::piano::game {

struct PreviewPixelBounds {
    float x = 0, y = 0, width = 0, height = 0;
};
struct PreviewSelectedRowBounds {
    PreviewPixelBounds row, inherited_cull;
};
struct PreviewBadgeLayout {
    PreviewPixelBounds panel, waveform, progress;
};

std::optional<PreviewBadgeLayout> preview_badge_layout(
    const PreviewSelectedRowBounds& selected_row, float viewport_width, float viewport_height) noexcept;

// Provider must qualify this exact session/selection's visible row in canvas pixels,
// including the authenticated UI-render transform and inherited clipping; no extra OS DPI.
bool capture_song_preview_row_bounds(const SongPreviewRenderSnapshot& snapshot,
    void* viewport, void* canvas, PreviewSelectedRowBounds& bounds) noexcept;

bool draw_song_preview_badge(const SongPreviewRenderSnapshot& snapshot,
    const PreviewBadgeLayout& layout, void* fcanvas, void* font,
    const OverlayDrawText& text, const OverlayDrawTile& tile, float opacity = 1) noexcept;

} // namespace ff7r::piano::game
