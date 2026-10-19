#include "game/song_preview_anchor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ff7r::piano::game {
namespace {
bool transformed(const PreviewPaintGeometry& g, PreviewPixelBounds& out)
{
    const std::array values{g.width,g.height,g.a,g.b,g.c,g.d,g.tx,g.ty};
    for (const auto value : values) if (!std::isfinite(value)) return false;
    if (g.width <= 0 || g.height <= 0
        || static_cast<double>(g.a) * g.d - static_cast<double>(g.b) * g.c == 0) return false;
    float left = std::numeric_limits<float>::infinity(), top = left;
    float right = -left, bottom = -left;
    for (const float x : {0.0f, g.width}) for (const float y : {0.0f, g.height}) {
        const float px = g.a * x + g.c * y + g.tx;
        const float py = g.b * x + g.d * y + g.ty;
        if (!std::isfinite(px) || !std::isfinite(py)) return false;
        left = std::min(left, px); right = std::max(right, px);
        top = std::min(top, py); bottom = std::max(bottom, py);
    }
    out = {left, top, right-left, bottom-top};
    return std::isfinite(out.width) && std::isfinite(out.height) && out.width > 0 && out.height > 0;
}
struct Pair { std::uintptr_t pointer = 0, controller = 0; bool operator==(const Pair&) const = default; };
struct WidgetPairs { Pair fallback, preferred; bool operator==(const WidgetPairs&) const = default; };
struct SlateBlock { PreviewPaintGeometry paint; PreviewCullRect cull; };
struct ParentLink { Pair child, parent; };
struct BorrowedWidget {
    WidgetPairs pairs;
    Pair slate;
    SlateBlock block;
    std::array<ParentLink,31> parents{}; // At most 32 nodes including the root.
    std::size_t parent_count = 0;
};
template<class T> bool copy(const PreviewAnchorRead& read, std::uintptr_t base,
    std::uintptr_t offset, T& out)
{
    return base && offset <= std::numeric_limits<std::uintptr_t>::max() - base
        && read(base + offset, &out, sizeof(out));
}
bool live(const PreviewAnchorRead& read, const Pair& pair, const std::uint32_t count_offset)
{
    std::int32_t count = 0;
    return pair.pointer && pair.controller && copy(read, pair.controller, count_offset, count) && count > 0;
}
bool resolve(const PreviewAnchorRead& read, const WidgetPairs& pairs,
    const std::uint32_t count_offset, Pair& selected)
{
    std::int32_t preferred_count = 0;
    if (pairs.preferred.controller && !copy(read,pairs.preferred.controller,count_offset,preferred_count))
        return false;
    selected = preferred_count > 0 ? pairs.preferred : pairs.fallback;
    return live(read,selected,count_offset);
}
bool borrow(const PreviewAnchorSource& source, const PreviewAnchorRead& read,
    std::uintptr_t widget, const Pair& root, BorrowedWidget& out)
{
    if (!copy(read, widget, 0xd8, out.pairs)) return false;
    if (!resolve(read,out.pairs,source.strong_count_offset,out.slate)
        || !copy(read, out.slate.pointer, 0x90, out.block.paint)
        || !copy(read, out.slate.pointer, 0x100, out.block.cull)) return false;
    Pair current = out.slate;
    while (current != root) {
        if (out.parent_count == out.parents.size()) return false;
        for (std::size_t i = 0; i < out.parent_count; ++i)
            if (out.parents[i].child == current) return false;
        Pair parent;
        if (!copy(read, current.pointer, 0x38, parent)
            || !live(read, parent, source.strong_count_offset)) return false;
        out.parents[out.parent_count++] = {current, parent};
        current = parent;
    }
    return true;
}
bool unchanged(const PreviewAnchorSource& source, const PreviewAnchorRead& read,
    std::uintptr_t widget, const BorrowedWidget& prior)
{
    WidgetPairs pairs;
    Pair selected;
    SlateBlock block;
    if (!copy(read, widget, 0xd8, pairs) || pairs != prior.pairs
        || !resolve(read,pairs,source.strong_count_offset,selected) || selected != prior.slate
        || !copy(read, prior.slate.pointer, 0x90, block.paint)
        || !copy(read, prior.slate.pointer, 0x100, block.cull)
        || std::memcmp(&block, &prior.block, sizeof(block)) != 0) return false;
    for (std::size_t i = 0; i < prior.parent_count; ++i) {
        Pair parent;
        if (!copy(read, prior.parents[i].child.pointer, 0x38, parent)
            || parent != prior.parents[i].parent
            || !live(read, parent, source.strong_count_offset)) return false;
    }
    return true;
}
} // namespace

bool validate_preview_anchor_gate(const PreviewAnchorGate& gate, const PreviewAnchorRead& read,
    std::string_view& failure) noexcept
{
    failure = "missing_catalog_entry";
    if (!read || !gate.root_pointer || gate.root_pointer > UINTPTR_MAX-8
        || gate.root_pointer+8 != gate.root_controller) return false;
    try {
        for (std::size_t i=0;i<gate.verifiers.size();++i) {
            const auto& verifier = gate.verifiers[i];
            failure = verifier.name.empty() ? "missing_catalog_entry" : verifier.name;
            if (!verifier.address || verifier.bytes.size() != (i==0 ? 48u : 32u)) return false;
            std::vector<std::uint8_t> actual(verifier.bytes.size());
            if (!read(verifier.address,actual.data(),actual.size()) || actual != verifier.bytes) return false;
        }
        const auto& getter = gate.verifiers.front();
        const auto matches_rip = [&](std::size_t disp_offset,std::size_t end_offset,std::uintptr_t target) {
            std::int32_t disp=0;
            std::memcpy(&disp,getter.bytes.data()+disp_offset,sizeof(disp));
            if (getter.address>UINTPTR_MAX-end_offset) return false;
            const auto end=getter.address+end_offset;
            const auto delta=static_cast<std::int64_t>(disp);
            return delta>=0 ? static_cast<std::uint64_t>(delta)<=UINTPTR_MAX-end
                    && end+static_cast<std::uintptr_t>(delta)==target
                : static_cast<std::uint64_t>(-delta)<=end
                    && end-static_cast<std::uintptr_t>(-delta)==target;
        };
        failure = "root_getter_rip_targets";
        if (!matches_rip(9,0x0d,gate.root_controller) || !matches_rip(0x2b,0x2f,gate.root_pointer))
            return false;
        failure = {};
        return true;
    } catch (...) { return false; }
}

bool map_preview_anchor_geometry(const PreviewPaintGeometry& row,
    const PreviewPaintGeometry& list, const PreviewCullRect& cull,
    const PreviewRootLayout& root, const float width, const float height,
    PreviewSelectedRowBounds& out) noexcept
{
    out = {};
    const std::array values{root.origin_x,root.origin_y,root.render_width,root.render_height,
        root.output_width,root.output_height,root.viewport_scale,width,height,
        cull.left,cull.top,cull.right,cull.bottom};
    for (const auto value : values) if (!std::isfinite(value)) return false;
    constexpr float tolerance = 2;
    if (root.origin_x != 0 || root.origin_y != 0 || width <= 0 || height <= 0
        || root.render_width <= 0 || root.render_height <= 0 || root.viewport_scale <= 0
        || root.output_width <= 0 || root.output_height <= 0
        || std::abs(root.output_width-width) > tolerance || std::abs(root.output_height-height) > tolerance
        || cull.right <= cull.left || cull.bottom <= cull.top) return false;
    PreviewPixelBounds row_ui, list_ui;
    if (!transformed(row, row_ui) || !transformed(list, list_ui)) return false;
    const float ui_scale = std::max(.01f, std::min(root.render_width/1920,root.render_height/1080));
    const float k = root.viewport_scale/ui_scale;
    if (!std::isfinite(k) || k <= 0) return false;
    const auto convert = [&](const PreviewPixelBounds& ui) {
        return PreviewPixelBounds{root.output_width*.5f+(ui.x-root.render_width*.5f)*k,
            root.output_height*.5f+(ui.y-root.render_height*.5f)*k,ui.width*k,ui.height*k};
    };
    const auto row_pixels = convert(row_ui), list_pixels = convert(list_ui);
    const auto cull_pixels = convert({cull.left,cull.top,cull.right-cull.left,cull.bottom-cull.top});
    for (const auto& bounds : {row_pixels,list_pixels,cull_pixels}) {
        if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.width)
            || !std::isfinite(bounds.height) || !std::isfinite(bounds.x+bounds.width)
            || !std::isfinite(bounds.y+bounds.height) || bounds.width <= 0 || bounds.height <= 0) return false;
    }
    const float top = std::max(list_pixels.y,cull_pixels.y);
    const float bottom = std::min(list_pixels.y+list_pixels.height,cull_pixels.y+cull_pixels.height);
    if (!std::isfinite(top) || !std::isfinite(bottom) || bottom<=top
        || row_pixels.y < top-tolerance || row_pixels.y+row_pixels.height > bottom+tolerance) return false;
    out = {row_pixels,{cull_pixels.x,top,cull_pixels.width,bottom-top}};
    return true;
}

bool read_preview_anchor(const PreviewAnchorSource& source, const PreviewAnchorRead& read,
    const std::function<bool()>& identity_current,
    const float width, const float height, PreviewSelectedRowBounds& out) noexcept
{
    out = {};
    if (!read || !identity_current || !source.strong_count_offset) return false;
    try {
        Pair root;
        PreviewRootLayout layout, again;
        BorrowedWidget row, list;
        if (!identity_current() || !copy(read,source.root_pair,0,root) || !live(read,root,source.strong_count_offset)
            || !copy(read,root.pointer,0x318,layout)
            || !borrow(source,read,source.row_widget,root,row)
            || !borrow(source,read,source.list_widget,root,list)) return false;
        if (std::none_of(row.parents.begin(),row.parents.begin()+row.parent_count,
            [&](const ParentLink& link) { return link.parent==list.slate; })) return false;
        PreviewSelectedRowBounds mapped;
        if (!map_preview_anchor_geometry(row.block.paint,list.block.paint,row.block.cull,
                layout,width,height,mapped)) return false;
        Pair root_again;
        if (!unchanged(source,read,source.row_widget,row) || !unchanged(source,read,source.list_widget,list)
            || !copy(read,source.root_pair,0,root_again) || root_again != root
            || !live(read,root_again,source.strong_count_offset)
            || !copy(read,root.pointer,0x318,again) || std::memcmp(&layout,&again,sizeof(layout)) != 0
            || !identity_current()) return false;
        out = mapped;
        return true;
    } catch (...) { return false; }
}
} // namespace ff7r::piano::game
