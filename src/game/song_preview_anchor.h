#pragma once

#include "game/song_preview_overlay.h"

#include <array>
#include <functional>
#include <string_view>
#include <vector>

namespace ff7r::piano::game {

// Borrowed layout recovered by the authenticated 2026-10-19 researcher handoff;
// its exact function gates/global targets and provenance are canonical in rva_catalog.json.
struct PreviewPaintGeometry {
    float width = 0, height = 0;
    std::array<float, 5> reserved{};
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    float tail = 0;
};
static_assert(sizeof(PreviewPaintGeometry) == 56);
static_assert(offsetof(PreviewPaintGeometry, a) == 0x1c);
struct PreviewRootLayout {
    float origin_x = 0, origin_y = 0, render_width = 0, render_height = 0;
    float output_width = 0, output_height = 0, viewport_scale = 0;
};
struct PreviewCullRect { float left = 0, top = 0, right = 0, bottom = 0; };

bool map_preview_anchor_geometry(const PreviewPaintGeometry& row,
    const PreviewPaintGeometry& list, const PreviewCullRect& inherited_cull,
    const PreviewRootLayout& root, float canvas_width, float canvas_height,
    PreviewSelectedRowBounds& out) noexcept;

using PreviewAnchorRead = std::function<bool(std::uintptr_t, void*, std::size_t)>;
struct PreviewAnchorSource {
    std::uintptr_t root_pair = 0;
    std::uintptr_t row_widget = 0, list_widget = 0;
    std::uint32_t strong_count_offset = 0;
    bool require_visible_paint = false;
};
struct PreviewAnchorPresentation { float opacity = 0, ui_scale = 0; };
struct PreviewAnchorVerifier {
    std::string_view name;
    std::uintptr_t address = 0;
    std::vector<std::uint8_t> bytes;
};
struct PreviewAnchorGate {
    // Root getter first (48 bytes, including both RIP loads); remaining gates are 32 bytes.
    std::array<PreviewAnchorVerifier,6> verifiers;
    std::uintptr_t root_pointer = 0, root_controller = 0;
};
bool validate_preview_anchor_gate(const PreviewAnchorGate& gate, const PreviewAnchorRead& read,
    std::string_view& failure) noexcept;

void configure_song_preview_anchor(HMODULE module) noexcept;
std::uintptr_t song_preview_anchor_root_pair() noexcept;
// Borrows only within PostRender. No pin/unpin, native calls, or registry scanning.
bool read_preview_anchor(const PreviewAnchorSource& source, const PreviewAnchorRead& read,
    const std::function<bool()>& identity_current,
    float canvas_width, float canvas_height, PreviewSelectedRowBounds& out,
    PreviewAnchorPresentation* presentation = nullptr) noexcept;

} // namespace ff7r::piano::game
