#pragma once

#include "game/runtime_layouts.h"
#include "startup/startup_cache_progress.h"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace ff7r::piano::game {

inline constexpr std::size_t kStartupCacheOverlayTextCapacity = 192;
inline constexpr float kStartupCacheOverlayPanelWidth = 680.0f;

using PostRenderFn = void(__fastcall*)(void* viewport, void* canvas);
using DrawShadowedStringFn = int32_t(__fastcall*)(void* fcanvas, float x, float y,
    const wchar_t* text, void* font, const runtime_layouts::FLinearColor* color);
using DrawTileFn = void(__fastcall*)(void* fcanvas, float x, float y, float size_x,
    float size_y, float u, float v, float size_u, float size_v,
    const runtime_layouts::FLinearColor* color, const void* texture, bool alpha_blend);

using OverlayNativeRead =
    std::function<bool(void* base, uintptr_t offset, void* output, std::size_t size)>;
using OverlayDrawText = std::function<int32_t(void* fcanvas, float x, float y,
    const wchar_t* text, void* font, const runtime_layouts::FLinearColor* color)>;
using OverlayDrawTile = std::function<bool(void* fcanvas, float x, float y, float size_x,
    float size_y, float u, float v, float size_u, float size_v,
    const runtime_layouts::FLinearColor* color, const void* texture, bool alpha_blend)>;

class StartupCacheOverlayRenderer {
public:
    StartupCacheOverlayRenderer(startup::StartupCacheProgress& progress,
        OverlayNativeRead read, OverlayDrawText draw, OverlayDrawTile draw_tile = {});

    bool render(void* viewport, void* canvas,
        std::chrono::steady_clock::time_point now) noexcept;
    bool hidden() const noexcept { return hidden_; }
    bool enabled() const noexcept { return enabled_; }
    bool rectangles_enabled() const noexcept { return rectangles_enabled_; }

private:
    startup::StartupCacheProgress& progress_;
    OverlayNativeRead read_;
    OverlayDrawText draw_;
    OverlayDrawTile draw_tile_;
    bool enabled_ = true;
    bool rectangles_enabled_ = true;
    bool hidden_ = false;
    bool ready_drawn_ = false;
    std::chrono::steady_clock::time_point hide_deadline_{};
    startup::StartupCacheProgressSnapshot last_snapshot_{};
};

using OverlayRenderCallback = void(*)(void* viewport, void* canvas) noexcept;
void dispatch_startup_post_render(PostRenderFn original, void* viewport, void* canvas,
    OverlayRenderCallback overlay) noexcept;

bool install_startup_cache_overlay(HMODULE exe_module,
    startup::StartupCacheProgress& progress) noexcept;

} // namespace ff7r::piano::game
