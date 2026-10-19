#include "game/song_preview_overlay.h"
#include "pipeline/song_preview.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>

namespace ff7r::piano::game {

std::optional<PreviewBadgeLayout> preview_badge_layout(
    const PreviewSelectedRowBounds& bounds, const float width, const float height) noexcept
{
    const auto valid = [](const PreviewPixelBounds& b) {
        return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.width)
            && std::isfinite(b.height) && b.width > 0 && b.height > 0;
    };
    const auto& row = bounds.row;
    const auto& cull = bounds.inherited_cull;
    if (!valid(row) || !valid(cull) || !std::isfinite(width) || !std::isfinite(height)
        || width <= 0 || height <= 0 || row.x < 0 || row.y < 0
        || row.width > width - row.x || row.height > height - row.y) return {};
    constexpr float badge_width = 200, gap = 32;
    if (!std::isfinite(cull.x+cull.width)
        || !std::isfinite(cull.y+cull.height)) return {};
    const float top = std::max(0.0f, cull.y);
    const float bottom = std::min(height, cull.y + cull.height);
    const float center = row.y + row.height*.5f;
    const float badge_height = std::min({64.0f,row.height,2*(center-top),2*(bottom-center)});
    if (badge_height < 48) return {};
    const float x = row.x - gap - badge_width;
    const float y = row.y + row.height * .5f - badge_height * .5f;
    // The separate Canvas badge sits outside native horizontal clipping and its selection diamond.
    if (x < 0 || x + badge_width > width || y < top || y + badge_height > bottom) return {};
    // Text row on top (font is fixed-size), bars in the middle, song timeline at the bottom.
    return PreviewBadgeLayout{{x, y, badge_width, badge_height},
        {x + 6, y + 22, badge_width - 12, badge_height - 32},
        {x + 6, y + badge_height - 7, badge_width - 12, 3}};
}

bool draw_song_preview_badge(const SongPreviewRenderSnapshot& snapshot,
    const PreviewBadgeLayout& layout, void* fcanvas, void* font,
    const OverlayDrawText& text, const OverlayDrawTile& tile, const float opacity) noexcept
{
    if (!snapshot || snapshot.position.failed || !fcanvas || !tile
        || !(opacity > 0 && opacity <= 1)) return false;
    const auto& song = *snapshot.selection.song;
    const auto& artifact = *song.preview;
    if (!artifact.frame_count || artifact.frame_count > ff7rp::pipeline::kSongPreviewMaxFrames)
        return false;
    try {
        using Color = runtime_layouts::FLinearColor;
        const auto fade = [opacity](Color color) { color.a *= opacity; return color; };
        // Linear colors matched to the native selected row: navy fill, gold trim, cyan accents.
        const Color gold = fade({.64f, .49f, .22f, 1}), navy = fade({.012f, .024f, .30f, .94f});
        const Color cyan = fade({.12f, .66f, .94f, 1}), groove = fade({.03f, .07f, .45f, 1});
        const Color white = fade({1, 1, 1, 1});
        const auto rect = [&](const PreviewPixelBounds& b, const Color& color) {
            return tile(fcanvas, b.x, b.y, b.width, b.height, 0, 0, 1, 1,
                &color, nullptr, true);
        };
        const auto& p = layout.panel;
        if (!rect(p, gold) || !rect({p.x + 1, p.y + 1, p.width - 2, p.height - 2}, navy))
            return false;
        const bool playing = snapshot.phase == PreviewPhase::Playing && snapshot.position.playing;
        const auto total = snapshot.position.total_frames;
        const auto played = playing && total ? std::min(snapshot.position.played_frames,
            std::min(total, artifact.frame_count)) : 0;
        const auto rate = ff7rp::pipeline::kSongPreviewSampleRate;
        const double start = std::isfinite(artifact.source_start_seconds)
            ? std::max(0.0, artifact.source_start_seconds) : 0;
        const double clip = static_cast<double>(artifact.frame_count) / rate;
        const double length = std::max(std::isfinite(song.duration_seconds)
            ? static_cast<double>(song.duration_seconds) : 0, start + clip);

        // Stop icon, then the song clock: excerpt start plus device position, over the whole song.
        if (!rect({p.x + 8, p.y + 7, 9, 9}, white)) return false;
        // The native string shadow is opaque black, so text waits for a mostly opaque panel.
        if (text && opacity >= .5f) {
            // Clamped so corrupt metadata cannot overflow the casts.
            const auto at = static_cast<unsigned>(std::min(start + static_cast<double>(played) / rate, 359999.0));
            const auto end = static_cast<unsigned>(std::min(length, 359999.0));
            wchar_t label[32]{};
            std::swprintf(label, std::size(label), L"%u:%02u / %u:%02u",
                at / 60, at % 60, end / 60, end % 60);
            (void)text(fcanvas, p.x + 24, p.y + 3, label, font, &white);
        }

        // Live equalizer from the prepared spectrum at the device position; a floor until playing.
        const auto& w = layout.waveform;
        constexpr auto bands = ff7rp::pipeline::kSongPreviewSpectrumBands;
        const float pitch = w.width / static_cast<float>(bands);
        for (std::size_t b = 0; b < bands; ++b) {
            const float level = std::isfinite(snapshot.spectrum[b])
                ? std::clamp(snapshot.spectrum[b], 0.0f, 1.0f) : 0;
            const float h = std::max(2.0f, level * w.height);
            const PreviewPixelBounds bar{w.x + static_cast<float>(b) * pitch,
                w.y + w.height - h, pitch - 2, h};
            if (!rect(bar, cyan) || !rect({bar.x, bar.y, bar.width, 2}, white)) return false;
        }

        // Whole-song timeline: where the excerpt sits, and the device playhead inside it.
        const auto& t = layout.progress;
        const float ww = std::max(2.0f, t.width * static_cast<float>(clip / length));
        const float wx = std::min(t.x + t.width * static_cast<float>(start / length), t.x + t.width - ww);
        if (!rect(t, groove) || !rect({wx, t.y, ww, t.height}, gold)) return false;
        if (playing) {
            const float x = wx + (ww - 2) * static_cast<float>(played) / static_cast<float>(total);
            if (!rect({x, t.y - 2, 2, t.height + 4}, white)) return false;
        }
        return true;
    } catch (...) { return false; }
}

} // namespace ff7r::piano::game
