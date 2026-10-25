#include "game/song_hud_overlay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace ff7r::piano::game {
namespace {
bool positive(float n) noexcept { return std::isfinite(n) && n > 0; }
bool contained(const SongHudRect& r, float w, float h) noexcept
{
    return std::isfinite(r.x) && std::isfinite(r.y) && r.x >= 0 && r.y >= 0
        && positive(r.width) && positive(r.height) && r.x <= w && r.y <= h
        && r.width <= w - r.x && r.height <= h - r.y;
}
// Bounded UTF-16 scalar copy: never leave a dangling surrogate or embedded newline/NUL.
std::wstring clean(std::wstring_view value)
{
    std::wstring out;
    for (std::size_t i = 0; i < value.size() && i < 256 && out.size() < 256; ++i) {
        const auto c = static_cast<unsigned>(value[i]);
        if (c >= 0xd800 && c <= 0xdbff) {
            if (i + 1 < value.size() && value[i+1] >= 0xdc00 && value[i+1] <= 0xdfff) {
                out += value[i]; out += value[++i];
            }
        } else if (c >= 0xdc00 && c <= 0xdfff) {
            continue;
        } else out += c < 0x20 || c == 0x7f || c == 0x2028 || c == 0x2029 ? L' ' : value[i];
    }
    const auto begin = out.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos) return {};
    return out.substr(begin, out.find_last_not_of(L" \t\r\n") - begin + 1);
}
}

std::optional<SongHudLayout> song_hud_layout(const SongHudAnchor& a, float line_height,
    bool artwork) noexcept
{
    if (!positive(a.viewport_width) || !positive(a.viewport_height) || !positive(a.ui_scale)
        || !positive(line_height) || !contained(a.title, a.viewport_width, a.viewport_height)) return {};
    SongHudLayout out;
    const auto& block = a.title_block;
    const bool complete_block = block && contained(*block,a.viewport_width,a.viewport_height)
        && a.title.x >= block->x && a.title.y >= block->y
        && a.title.x+a.title.width <= block->x+block->width
        && a.title.y+a.title.height <= block->y+block->height;
    // Match the reference's screen-relative artwork, not font-widget padding.
    const float side = a.viewport_height * (110.0f / 942.0f);
    const SongHudRect cover{(complete_block ? block->x : a.title.x) - 8*a.ui_scale - side,
        a.title.y + a.title.height*.5f - side*.5f, side, side};
    if (artwork && complete_block && side >= 8*a.ui_scale
        && contained(cover, a.viewport_width, a.viewport_height)) {
        out.cover = cover;
    }
    // Use the full title block's left edge, beside (not over) the cover.
    const float left=complete_block ? block->x : a.title.x;
    const SongHudRect row{left,
        a.title.y + a.title.height + 8*a.ui_scale,
        a.viewport_width - left - std::max(1.0f,8*a.ui_scale), line_height};
    if (contained({row.x, row.y, row.width + 1, row.height + 1},
        a.viewport_width, a.viewport_height) && positive(row.width)) out.metadata = row;
    return out;
}

std::wstring song_hud_metadata(const SongHudMetadata& m, float width, const SongHudFontMetrics& font)
{
    if (!positive(width) || !font.measure_width) return {};
    std::wstring row;
    const auto append = [&](std::wstring_view label, std::wstring_view value) {
        auto field = clean(value);
        if (field.empty()) return;
        if (!row.empty()) row += L"   ";
        row += label; row += field;
    };
    append(L"", m.album); append(L"", m.bpm); append(L"", m.duration);
    append(L"", m.author);
    const auto fits = [&](std::wstring_view s) {
        const float measured = font.measure_width(s);
        return std::isfinite(measured) && measured >= 0 && measured <= width;
    };
    if (row.empty() || fits(row)) return row;
    if (!fits(L"...")) return {};
    // Bound native font measurements even for long credits on every Canvas frame.
    std::wstring best = L"...";
    std::size_t low = 0, high = row.size();
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        auto end = middle;
        if (end && row[end-1] >= 0xd800 && row[end-1] <= 0xdbff) --end;
        auto candidate = row.substr(0, end) + L"...";
        if (fits(candidate)) { best = std::move(candidate); low = middle + 1; }
        else high = middle;
    }
    return best;
}

bool draw_song_hud_overlay(const SongHudAnchor& anchor, const SongHudMetadata& metadata,
    const SongHudFontMetrics& metrics, const void* resource, void* canvas, void* font,
    const SongHudDrawText& text, const OverlayDrawTile& tile, const float opacity,
    const SongHudIconTextures& icons) noexcept
{
    if (!canvas || !std::isfinite(opacity) || opacity <= 0 || opacity > 1) return false;
    try {
        if (!positive(metrics.line_height) || !positive(metrics.max_scale)) return false;
        const float text_scale = std::min(anchor.title.height*.5f / metrics.line_height,metrics.max_scale);
        if (!positive(text_scale)) return false;
        const SongHudFontMetrics scaled{metrics.line_height*text_scale,
            [&](std::wstring_view value) {
                return metrics.measure_width ? metrics.measure_width(value)*text_scale
                    : std::numeric_limits<float>::quiet_NaN();
            }};
        const auto layout = song_hud_layout(anchor, scaled.line_height, resource != nullptr);
        if (!layout) return false;
        struct Field { std::wstring_view value; std::size_t icon; };
        const auto none=kSongHudIconCount;
        const std::array<Field,4> fields{{{metadata.album,none},{metadata.bpm,0},
            {metadata.duration,1},{metadata.author,2}}};
        struct Run { std::wstring value; float x; const void* icon; };
        std::vector<Run> runs;
        const float icon_side=scaled.line_height, icon_gap=scaled.line_height*.2f,
            group_gap=scaled.line_height*.55f;
        if (layout->metadata) {
            const auto row=*layout->metadata;
            float core_width=0;
            for (std::size_t i=1;i<=3;++i) {
                const auto value=clean(fields[i].value);
                if (value.empty()) continue;
                const float width=scaled.measure_width(value);
                if (!std::isfinite(width) || width<0) return false;
                core_width+=width+group_gap
                    +(icons[fields[i].icon] ? icon_side+icon_gap : 0);
            }
            float x=row.x;
            for (std::size_t i=0;i<fields.size();++i) {
                const auto& field=fields[i];
                auto value=clean(field.value);
                if (value.empty()) continue;
                const void* icon=field.icon<icons.size() ? icons[field.icon] : nullptr;
                const float inset=icon ? icon_side+icon_gap : 0;
                const float gap=runs.empty() ? 0 : group_gap;
                const auto label=song_hud_metadata({value},row.x+row.width-x-gap-inset
                    -(i==0 ? core_width : 0),scaled);
                if (label.empty()) { if (i==0) continue; break; }
                const float measured=scaled.measure_width(label);
                if (!std::isfinite(measured) || measured<0) return false;
                runs.push_back({label,x+gap,icon});
                x+=gap+inset+measured;
                if (label!=value && i!=0) break;
            }
        }
        if ((!runs.empty() && (!text || !font)) || (layout->cover && !tile)) return false;
        const runtime_layouts::FLinearColor white{1,1,1,opacity}, grey{.8f,.8f,.8f,opacity};
        const auto rect = [&](SongHudRect r, const void* texture, const auto& color) {
            return tile && tile(canvas, r.x, r.y, r.width, r.height, 0,0,1,1, &color, texture, true);
        };
        if (layout->cover) {
            if (!rect(*layout->cover,resource,white)) return false;
        }
        for (const auto& run:runs) {
            const float y=layout->metadata->y;
            if (run.icon && !rect({run.x,y,icon_side,icon_side},run.icon,grey)) return false;
            (void)text(canvas,run.x+(run.icon ? icon_side+icon_gap : 0),y,
                run.value.c_str(),font,&grey,text_scale);
        }
        return true;
    } catch (...) { return false; }
}
} // namespace ff7r::piano::game
