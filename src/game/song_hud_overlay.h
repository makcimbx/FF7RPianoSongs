#pragma once
#include <limits>

#include "game/startup_cache_overlay.h"
#include "game/song_hud_artwork.h"

#include <optional>
#include <string>
#include <string_view>

namespace ff7r::piano::game {

struct SongHudRect { float x = 0, y = 0, width = 0, height = 0; };
struct SongHudAnchor {
    // Qualified visible title bounds in Canvas pixels, excluding its separator.
    SongHudRect title;
    float viewport_width = 0, viewport_height = 0, ui_scale = 1;
    // Complete native block, including its note icon; unavailable means no artwork.
    std::optional<SongHudRect> title_block;
};
struct SongHudMetadata {
    // Prepared display values; empty fields disappear. BPM/duration include units/formatting.
    std::wstring_view album, author, bpm, duration;
};
struct SongHudFontMetrics {
    // Actual unit-scale native font metrics, including fallback glyph extents.
    float line_height = 0;
    std::function<float(std::wstring_view)> measure_width;
    float max_scale = std::numeric_limits<float>::max();
};

using SongHudDrawText = std::function<int32_t(void* fcanvas, float x, float y,
    const wchar_t* text, void* font, const runtime_layouts::FLinearColor* color, float scale)>;
struct SongHudLayout {
    std::optional<SongHudRect> cover, metadata;
};

std::optional<SongHudLayout> song_hud_layout(const SongHudAnchor&, float font_line_height,
    bool has_artwork) noexcept;
std::wstring song_hud_metadata(const SongHudMetadata&, float available_width,
    const SongHudFontMetrics&);

// No title draw, background, resource construction, native reads, or retained state.
// texture_resource is an already-qualified Canvas resource, NEVER a UObject.
// Text's native return value is a layout metric, not a success flag. Exceptions
// and tile failures propagate as false; already submitted draws cannot roll back.
bool draw_song_hud_overlay(const SongHudAnchor&, const SongHudMetadata&,
    const SongHudFontMetrics&, const void* texture_resource, void* fcanvas, void* font,
    const SongHudDrawText&, const OverlayDrawTile&, float opacity = 1,
    const SongHudIconTextures& icons = {}) noexcept;

} // namespace ff7r::piano::game
