#pragma once

#include "game/menu_theme_native.h"
#include "game/song_hud_overlay.h"

namespace ff7r::piano::game {
struct PlaybackSnapshot;
// Startup only; gates the new contracts independently for the compiled build.
void configure_song_hud(HMODULE) noexcept;
// Real qualified menu callback only, independent of the selected theme.
void prepare_song_hud_covers(const menu_theme_native::CallbackAuthority&) noexcept;
// Explicit exit/phase/authority revocation clears the root.
void begin_song_hud_tick() noexcept;
// Admitted tick entry: invalidate in-flight frames; retain only the same controller's root.
void begin_song_hud_tick(void* controller) noexcept;
// Qualified chart scope only: pass its actual chart and synchronously captured playback.
// Never retain a controller pointer for later native reads.
void observe_song_hud_controller(void* controller, void* chart,
    const PlaybackSnapshot& playback, int current, int pending) noexcept;
// Called synchronously by the shared admitted PostRender, never a worker.
// Callback-free pause may retain a current root; each draw requalifies identity, containment and paint.
void render_song_hud(void* viewport, void* canvas,
    const OverlayDrawText&, const OverlayDrawTile&) noexcept;
// Owning HUD text adapter only: fresh retained-font authority. Size-keyed items
// enforce scale one in the adapter; the observable legacy fallback keeps its ABI.
bool song_hud_font_draw_scale_allowed(void* font, float scale) noexcept;
}
