#pragma once

#include "core/theme.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ff7r::piano::game::menu_theme_plan {

enum class Scope : unsigned char { MusicList, MusicListCell, SpeedIcon, Cursor };
enum class State : unsigned char { Any, Normal, Selected };
enum class Brush : unsigned char { Primary, Normal, Hovered, Pressed, Disabled, Indexed };
enum class Color : unsigned char { Primary, Bottom, Content, Background, Outline, Shadow, BrushTint };

enum class PreviewRole : unsigned char { Background, Border, Accent, Track, Text, Count };
struct PreviewPalette {
    // Linear defaults are the existing Canvas badge, not sRGB authoring values.
    std::array<std::array<float, 4>, 5> linear{{
        {.012f, .024f, .30f, .94f}, {.64f, .49f, .22f, 1},
        {.12f, .66f, .94f, 1}, {.03f, .07f, .45f, 1}, {1, 1, 1, 1}}};
    std::uint8_t overrides = 0;
    bool operator==(const PreviewPalette&) const = default;
};

struct Target {
    Scope scope = Scope::MusicList;
    std::string widget;
    State state = State::Any;
    Brush brush = Brush::Primary;
    unsigned index = 0;
    Color color = Color::Primary;
    bool operator==(const Target&) const = default;
};
struct Image {
    Target target;
    core::ThemeImage pixels;
};
struct Palette {
    Target target;
    std::array<float, 4> linear{};
};
struct Program {
    std::string id = "original";
    std::vector<Image> images;
    std::vector<Palette> colors;
    std::uint64_t pixel_bytes = 0;
    PreviewPalette preview;
};

// Worker-side, value-only compilation. Native binding/type admission remains separate.
// Failure preserves both source and output; success transfers the prepared pixels.
bool compile(core::ThemeDefinition& source, Program& output, std::string& error);
std::array<float, 4> linear_color(const std::array<std::uint8_t, 4>& srgb) noexcept;

} // namespace ff7r::piano::game::menu_theme_plan
