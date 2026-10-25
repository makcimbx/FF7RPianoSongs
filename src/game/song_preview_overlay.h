#pragma once

#include "game/song_preview.h"
#include "game/startup_cache_overlay.h"
#include "game/menu_theme_plan.h"

#include <optional>

namespace ff7r::piano::game {

struct PreviewPaletteSnapshot {
    std::uint64_t generation = 0; // zero: optional menu theming never adopted an owner
    bool active = false;
    menu_theme_plan::PreviewPalette palette;
};
// Fixed value mailbox, never native authority: no allocation, native access,
// callbacks or runtime-state lock. Its lock covers only this fixed copy.
// Writers publish only a whole committed program; retirement suppresses old-session fades.
void publish_song_preview_palette(std::uint64_t generation, const menu_theme_plan::PreviewPalette&) noexcept;
void retire_song_preview_palette(std::uint64_t generation) noexcept;
PreviewPaletteSnapshot capture_song_preview_palette() noexcept;
// Active publication requires its exact owner. Retired/older fades are rejected;
// a strictly newer session without a publication uses original badge defaults.
bool preview_badge_palette(const PreviewPaletteSnapshot&, std::uint64_t shown_generation,
    menu_theme_plan::PreviewPalette&) noexcept;

struct PreviewPixelBounds {
    float x = 0, y = 0, width = 0, height = 0;
};
struct PreviewSelectedRowBounds {
    PreviewPixelBounds row, inherited_cull;
    // Full mapped list/panel rectangle, not the inherited clipping intersection.
    PreviewPixelBounds panel;
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
    const OverlayDrawText& text, const OverlayDrawTile& tile, float opacity = 1,
    const menu_theme_plan::PreviewPalette& palette = {}) noexcept;

} // namespace ff7r::piano::game
