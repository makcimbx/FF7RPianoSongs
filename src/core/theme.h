#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ff7r::piano::core {

struct ThemeImage {
    // Top-down tightly packed BGRA8, straight alpha; no encoded input survives preparation.
    std::vector<std::uint8_t> bgra;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct ThemeDefinition {
    std::string id = "original";
    std::map<std::string, ThemeImage> images;
    // sRGB RGB channels, linear alpha; conversion belongs to the rendering adapter.
    std::map<std::string, std::array<std::uint8_t, 4>> colors;
};

bool valid_theme_id(std::string_view id) noexcept;

// Worker-only, contained PNG input; aspect-preserving thumbnail, at most 256px per side.
// Failure leaves output unchanged, including when a symlink escapes the directory.
bool load_png_thumbnail(const std::filesystem::path& directory,
    const std::filesystem::path& filename, ThemeImage& output, std::string& error);

// Worker-side file preparation and PNG decoding only; native readiness is separate.
// Failure leaves output unchanged; prepared images own their bytes, not file paths.
bool load_theme(const std::filesystem::path& themes_directory, std::string_view id,
    ThemeDefinition& output, std::string& error);

} // namespace ff7r::piano::core
