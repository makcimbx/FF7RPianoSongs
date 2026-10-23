#include "game/menu_theme_plan.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ff7r::piano::game::menu_theme_plan {
namespace {
bool identifier(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= 127
        && std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_';
        });
}
Target target(std::string_view text, bool image)
{
    Target out;
    if (text.empty() || text.size() > 192) throw std::runtime_error("invalid selector length");
    const auto slash = text.find('/');
    if (slash == text.npos || text.find('/', slash + 1) != text.npos)
        throw std::runtime_error("expected scope/widget selector");
    const auto scope = text.substr(0, slash);
    if (scope == "MusicList") out.scope = Scope::MusicList;
    else if (scope == "MusicListCell") out.scope = Scope::MusicListCell;
    else if (scope == "SpeedIcon") out.scope = Scope::SpeedIcon;
    else if (scope == "Cursor") out.scope = Scope::Cursor;
    else throw std::runtime_error("unknown menu scope");
    text.remove_prefix(slash + 1);
    const auto at = text.find('@');
    if (at != text.npos) {
        const auto state = text.substr(at + 1);
        if (out.scope != Scope::MusicListCell)
            throw std::runtime_error("row state requires MusicListCell scope");
        if (state == "normal") out.state = State::Normal;
        else if (state == "selected") out.state = State::Selected;
        else throw std::runtime_error("unknown row state");
        text = text.substr(0, at);
    }
    const auto dot = text.find('.');
    const auto name = text.substr(0, dot);
    if (!identifier(name)) throw std::runtime_error("invalid widget name");
    out.widget = name;
    if (dot == text.npos) return out;
    const auto property = text.substr(dot + 1);
    if (property == "Normal") out.brush = Brush::Normal;
    else if (property == "Hovered") out.brush = Brush::Hovered;
    else if (property == "Pressed") out.brush = Brush::Pressed;
    else if (property == "Disabled") out.brush = Brush::Disabled;
    else if (!property.empty() && property.front() >= '0' && property.front() <= '9') {
        unsigned index = 0;
        const auto result = std::from_chars(property.data(), property.data() + property.size(), index);
        // This is the plan's array-entry bound, not a recovered native array size.
        if (result.ec != std::errc{} || result.ptr != property.data() + property.size()
            || index >= 64 || (property.size() > 1 && property.front() == '0'))
            throw std::runtime_error("invalid image-set index");
        out.brush = Brush::Indexed; out.index = index;
    } else if (image) {
        if (property != "Brush") throw std::runtime_error("unknown image property");
    } else if (property == "ColorAndOpacity") out.color = Color::Primary;
    else if (property == "BottomColorAndOpacity") out.color = Color::Bottom;
    else if (property == "ContentColorAndOpacity") out.color = Color::Content;
    else if (property == "BackgroundColor") out.color = Color::Background;
    else if (property == "OutlineColorAndOpacity") out.color = Color::Outline;
    else if (property == "ShadowColorAndOpacity") out.color = Color::Shadow;
    else if (property == "BrushTint") out.color = Color::BrushTint;
    else throw std::runtime_error("unknown palette property");
    if (!image && out.brush != Brush::Primary) out.color = Color::BrushTint;
    return out;
}
template<class Entry>
void unique(const std::vector<Entry>& entries, const Target& candidate)
{
    if (std::any_of(entries.begin(), entries.end(), [&](const Entry& entry) {
        return entry.target == candidate;
    })) throw std::runtime_error("duplicate normalized selector");
}
PreviewRole preview_role(std::string_view selector)
{
    if (selector == "Preview/Background") return PreviewRole::Background;
    if (selector == "Preview/Border") return PreviewRole::Border;
    if (selector == "Preview/Accent") return PreviewRole::Accent;
    if (selector == "Preview/Track") return PreviewRole::Track;
    if (selector == "Preview/Text") return PreviewRole::Text;
    throw std::runtime_error("unknown Preview role; expected Background, Border, Accent, Track or Text without property/state");
}
}

std::array<float, 4> linear_color(const std::array<std::uint8_t, 4>& srgb) noexcept
{
    std::array<float, 4> out{};
    for (unsigned i = 0; i < 3; ++i) {
        const float value = static_cast<float>(srgb[i]) / 255.0f;
        out[i] = value <= 0.04045f ? value / 12.92f
            : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
    out[3] = static_cast<float>(srgb[3]) / 255.0f;
    return out;
}

bool compile(core::ThemeDefinition& source, Program& output, std::string& error)
{
    std::string selector;
    try {
        if (!core::valid_theme_id(source.id)) throw std::runtime_error("invalid theme id");
        if (source.id == "original" && (!source.images.empty() || !source.colors.empty()))
            throw std::runtime_error("original cannot contain overrides");
        if (source.id != "original" && source.images.empty() && source.colors.empty())
            throw std::runtime_error("theme contains no overrides");
        Program candidate; candidate.id = source.id;
        candidate.images.reserve(source.images.size()); candidate.colors.reserve(source.colors.size());
        for (const auto& [name, pixels] : source.images) {
            selector = name;
            if (name.starts_with("Preview/")) throw std::runtime_error("Preview supports Colors only, not Images");
            auto binding = target(name, true); unique(candidate.images, binding);
            const auto bytes = static_cast<std::uint64_t>(pixels.width) * pixels.height * 4;
            if (!pixels.width || !pixels.height || pixels.width > 4096 || pixels.height > 4096
                || bytes != pixels.bgra.size() || bytes > 128ull * 1024 * 1024 - candidate.pixel_bytes)
                throw std::runtime_error("invalid prepared image or pixel budget");
            candidate.pixel_bytes += bytes;
            candidate.images.push_back({std::move(binding), {}});
        }
        for (const auto& [name, color] : source.colors) {
            selector = name;
            if (name.starts_with("Preview/")) {
                const auto role = static_cast<unsigned>(preview_role(name));
                const auto bit = static_cast<std::uint8_t>(1u << role);
                if (candidate.preview.overrides & bit) throw std::runtime_error("duplicate Preview role");
                candidate.preview.linear[role] = linear_color(color); candidate.preview.overrides |= bit;
                continue;
            }
            auto binding = target(name, false); unique(candidate.colors, binding);
            candidate.colors.push_back({std::move(binding), linear_color(color)});
        }
        // All validation/allocation has completed: pixel transfer and publication cannot throw.
        std::size_t i = 0;
        for (auto& [name, pixels] : source.images) candidate.images[i++].pixels = std::move(pixels);
        output = std::move(candidate); error.clear(); return true;
    } catch (const std::exception& failure) {
        error = (selector.empty() ? std::string{} : selector + ": ") + failure.what(); return false;
    }
}
} // namespace ff7r::piano::game::menu_theme_plan
