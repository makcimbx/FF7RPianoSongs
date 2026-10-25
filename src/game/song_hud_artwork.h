#pragma once

#include "core/theme.h"

namespace ff7r::piano::game {

enum class SongHudIcon : std::size_t { Metronome, Clock, Pencil, Count };
constexpr auto kSongHudIconCount = static_cast<std::size_t>(SongHudIcon::Count);
using SongHudIconTextures = std::array<const void*, kSongHudIconCount>;

// CPU-only preparation; callers publish immutable images before native callbacks.
void round_song_hud_cover(core::ThemeImage&) noexcept;
std::array<core::ThemeImage, kSongHudIconCount> make_song_hud_icons();

} // namespace ff7r::piano::game
