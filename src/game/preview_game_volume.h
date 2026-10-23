#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ff7r::piano::game {
enum class PreviewGameVolumeState { Unavailable, NotReady, Ready };
struct PreviewGameVolume {
    PreviewGameVolumeState state = PreviewGameVolumeState::Unavailable;
    float gain = 0;
    explicit operator bool() const noexcept { return state == PreviewGameVolumeState::Ready; }
};

// Stateless Config target (master included), not the transient envelope or final mix.
// Presentation-only copy; never grants native audio/lifetime authority.
PreviewGameVolume capture_preview_game_volume() noexcept;

namespace preview_game_volume_detail {
struct Memory {
    void* context = nullptr;
    bool (*read)(void*, std::uintptr_t, void*, std::size_t) noexcept = nullptr;
};
PreviewGameVolume project(Memory memory, std::uintptr_t image_base, std::size_t image_size,
    std::string_view profile, bool supported_image) noexcept;
}
} // namespace ff7r::piano::game
