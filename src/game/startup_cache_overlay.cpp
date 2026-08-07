#include "game/startup_cache_overlay.h"

#include "core/hooks.h"
#include "core/logging.h"
#include "core/pe_image.h"
#include "game/hook_specs.h"
#include "game/rvas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>
#include <string_view>

namespace ff7r::piano::game {
namespace {

constexpr auto kReadyVisibility = std::chrono::seconds(2);
constexpr float kRightMargin = 28.0f;
constexpr float kTopMargin = 28.0f;
constexpr float kLineHeight = 22.0f;
constexpr float kPanelWidth = kStartupCacheOverlayPanelWidth;
constexpr float kPanelHeight = 132.0f;
constexpr float kPanelPadding = 16.0f;
constexpr float kTrackHeight = 6.0f;
constexpr float kTrackBottomMargin = 10.0f;

core::RawRvaHook g_post_render_hook;
core::HookCallbackGate g_callback_gate;
PostRenderFn g_original_post_render = nullptr;
DrawShadowedStringFn g_draw_shadowed_string = nullptr;
DrawTileFn g_draw_tile = nullptr;
StartupCacheOverlayRenderer* g_renderer = nullptr;
bool g_install_attempted = false;

bool production_read(void* base, const uintptr_t offset, void* output, const std::size_t size)
{
    return core::safe_copy_bytes(reinterpret_cast<const std::byte*>(base) + offset, output, size);
}

int32_t production_draw(void* fcanvas, const float x, const float y, const wchar_t* text,
    void* font, const runtime_layouts::FLinearColor* color)
{
    return g_draw_shadowed_string
        ? g_draw_shadowed_string(fcanvas, x, y, text, font, color) : 0;
}

bool production_draw_tile(void* fcanvas, const float x, const float y, const float size_x,
    const float size_y, const float u, const float v, const float size_u, const float size_v,
    const runtime_layouts::FLinearColor* color, const void* texture, const bool alpha_blend)
{
    if (!g_draw_tile) return false;
    __try {
        g_draw_tile(fcanvas, x, y, size_x, size_y, u, v, size_u, size_v,
            color, texture, alpha_blend);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool read_value(const OverlayNativeRead& read, void* base, const uintptr_t offset, T& value)
{
    return base && read && read(base, offset, &value, sizeof(value));
}

const wchar_t* stage_text(const ff7rp::pipeline::SongLoadProgressStage stage) noexcept
{
    using Stage = ff7rp::pipeline::SongLoadProgressStage;
    switch (stage) {
    case Stage::Inspecting: return L"Inspecting song sources";
    case Stage::ValidatingCache: return L"Validating cached songs";
    case Stage::DecodingAudio: return L"Decoding song audio";
    case Stage::GeneratingChart: return L"Generating piano charts";
    case Stage::BuildingAudioCache: return L"Building game audio cache";
    case Stage::PublishingCache: return L"Publishing cache artifacts";
    case Stage::Complete: return L"Finalizing song cache";
    }
    return L"Preparing piano songs";
}

void format_duration(const std::size_t unbounded_seconds,
    wchar_t* output, const std::size_t output_size) noexcept
{
    const std::size_t seconds = std::min<std::size_t>(unbounded_seconds, 86400);
    if (seconds < 60) {
        std::swprintf(output, output_size, L"%zu sec", seconds);
    } else if (seconds < 3600) {
        std::swprintf(output, output_size, L"%zu min", (seconds + 59) / 60);
    } else {
        std::swprintf(output, output_size, L"%zu hr", (seconds + 3599) / 3600);
    }
}

constexpr bool rebuild_stage(
    const ff7rp::pipeline::SongLoadProgressStage stage) noexcept
{
    using Stage = ff7rp::pipeline::SongLoadProgressStage;
    return stage == Stage::DecodingAudio || stage == Stage::GeneratingChart
        || stage == Stage::BuildingAudioCache || stage == Stage::PublishingCache;
}

void render_callback(void* viewport, void* canvas) noexcept
{
    auto lease = g_callback_gate.try_enter();
    if (!lease || !g_renderer) return;
    (void)g_renderer->render(viewport, canvas, std::chrono::steady_clock::now());
}

void __fastcall post_render_detour(void* viewport, void* canvas) noexcept
{
    dispatch_startup_post_render(g_original_post_render, viewport, canvas, render_callback);
}

bool validate_signature(HMODULE exe_module, const char* id,
    const uintptr_t expected_rva) noexcept
{
    const RvaSignatureSpec* spec = find_rva_signature(id);
    return exe_module && spec && spec->rva == expected_rva && !spec->expected_prologue.empty() &&
        core::bytes_equal(reinterpret_cast<const uint8_t*>(
            reinterpret_cast<uintptr_t>(exe_module) + spec->rva), spec->expected_prologue);
}

void log_overlay_noexcept(const core::LogLevel level, const std::string_view message) noexcept
{
    try {
        core::log(level, std::string(message));
    } catch (...) {
    }
}

} // namespace

StartupCacheOverlayRenderer::StartupCacheOverlayRenderer(
    startup::StartupCacheProgress& progress, OverlayNativeRead read, OverlayDrawText draw,
    OverlayDrawTile draw_tile)
    : progress_(progress), read_(std::move(read)), draw_(std::move(draw)),
      draw_tile_(std::move(draw_tile)), rectangles_enabled_(static_cast<bool>(draw_tile_))
{
}

bool StartupCacheOverlayRenderer::render(void* viewport, void* canvas,
    const std::chrono::steady_clock::time_point now) noexcept
{
    try {
        if (!enabled_ || hidden_) return false;
        if (ready_drawn_ && now >= hide_deadline_) {
            hidden_ = true;
            return false;
        }

        void* engine = nullptr;
        void* font = nullptr;
        void* fcanvas = nullptr;
        float clip_x = 0.0f;
        float clip_y = 0.0f;
        if (!read_value(read_, viewport, runtime_layouts::UObject::outer, engine) ||
            !read_value(read_, engine, runtime_layouts::UEngine::small_font, font) ||
            !read_value(read_, canvas, runtime_layouts::UCanvas::canvas, fcanvas) ||
            !read_value(read_, canvas, runtime_layouts::UCanvas::clip_x, clip_x) ||
            !read_value(read_, canvas, runtime_layouts::UCanvas::clip_y, clip_y) ||
            !font || !fcanvas || !std::isfinite(clip_x) || !std::isfinite(clip_y) ||
            clip_x <= 0.0f || clip_y <= 0.0f) return false;

        startup::StartupCacheProgressSnapshot snapshot;
        if (progress_.try_snapshot(snapshot)) last_snapshot_ = snapshot;
        const startup::StartupCacheProgressSnapshot state = last_snapshot_;
        std::array<wchar_t, kStartupCacheOverlayTextCapacity> detail{};
        std::array<wchar_t, kStartupCacheOverlayTextCapacity> status{};
        std::array<wchar_t, kStartupCacheOverlayTextCapacity> estimates{};
        const wchar_t* headline = L"FF7RPianoSongs: Preparing piano songs";
        switch (state.phase) {
        case startup::StartupCachePhase::Ready:
            headline = L"FF7RPianoSongs: Piano songs ready";
            break;
        case startup::StartupCachePhase::Failed:
            headline = L"FF7RPianoSongs: Song cache setup failed";
            break;
        case startup::StartupCachePhase::Publishing: break;
        case startup::StartupCachePhase::Loading: break;
        case startup::StartupCachePhase::Starting:
            break;
        }

        if (state.phase != startup::StartupCachePhase::Ready) {
            std::swprintf(detail.data(), detail.size(),
                L"Processed %zu/%zu | Ready %zu",
                state.completed_candidates, state.candidate_count,
                state.available_on_reopen_song_count);
        }
        if (state.phase == startup::StartupCachePhase::Ready) {
            // The headline is the sole completion notice. Catalog adoption and
            // native menu lifecycle are intentionally not startup UX inputs.
        } else if (state.phase == startup::StartupCachePhase::Failed) {
            std::swprintf(status.data(), status.size(),
                L"See FF7RPianoSongs.log for details");
        } else if (state.phase == startup::StartupCachePhase::Loading) {
            const std::size_t percent = state.total_stage_units == 0 ? 100 :
                std::min<std::size_t>(99,
                    state.completed_stage_units * 100 / state.total_stage_units);
            const bool rebuilding = state.active_candidate_count != 0 &&
                rebuild_stage(state.latest_stage);
            if (rebuilding) {
                headline = L"FF7RPianoSongs: Rebuilding song cache";
                std::swprintf(status.data(), status.size(), L"%ls - %zu%%",
                    stage_text(state.latest_stage), percent);
            } else {
                headline = L"FF7RPianoSongs: Checking song caches";
                std::swprintf(status.data(), status.size(),
                    L"Validating existing caches - %zu%%", percent);
            }
            if (rebuilding) {
                if (!state.estimates_available) {
                    std::swprintf(estimates.data(), estimates.size(),
                        L"Per song: calculating... | Total: calculating...");
                } else {
                    std::array<wchar_t, 32> per_song{};
                    std::array<wchar_t, 32> total{};
                    format_duration(state.estimated_per_song_seconds,
                        per_song.data(), per_song.size());
                    format_duration(state.estimated_total_remaining_seconds,
                        total.data(), total.size());
                    std::swprintf(estimates.data(), estimates.size(),
                        L"Per song: approx. %ls | Total: approx. %ls",
                        per_song.data(), total.data());
                }
            }
        } else if (state.phase == startup::StartupCachePhase::Publishing) {
            std::swprintf(status.data(), status.size(), L"Finalizing song cache");
        } else {
            std::swprintf(status.data(), status.size(), L"Discovering Music folders");
        }

        const float panel_width = std::clamp(std::min(kPanelWidth, clip_x), 0.0f, clip_x);
        const float panel_height = std::clamp(std::min(kPanelHeight, clip_y), 0.0f, clip_y);
        const float panel_x = std::clamp(clip_x - panel_width - kRightMargin,
            0.0f, std::max(0.0f, clip_x - panel_width));
        const float panel_y = std::clamp(kTopMargin - 12.0f,
            0.0f, std::max(0.0f, clip_y - panel_height));
        const float x = std::clamp(panel_x + kPanelPadding, 0.0f, clip_x);
        const float title_y = std::clamp(panel_y + 12.0f, 0.0f, clip_y);
        const float detail_y = std::clamp(title_y + kLineHeight, 0.0f, clip_y);
        const float estimates_y = std::clamp(detail_y + kLineHeight, 0.0f, clip_y);
        const float eta_y = std::clamp(estimates_y + kLineHeight, 0.0f, clip_y);
        const float track_x = std::clamp(panel_x + kPanelPadding, 0.0f, clip_x);
        const float track_width = std::clamp(panel_width - 2.0f * kPanelPadding,
            0.0f, clip_x - track_x);
        const float track_y = std::clamp(
            panel_y + panel_height - kTrackBottomMargin - kTrackHeight, 0.0f, clip_y);
        const float track_height = std::clamp(kTrackHeight, 0.0f, clip_y - track_y);
        double progress_fraction = 0.0;
        if (state.phase == startup::StartupCachePhase::Publishing ||
            state.phase == startup::StartupCachePhase::Ready) {
            progress_fraction = 1.0;
        } else if (state.total_stage_units != 0) {
            progress_fraction = static_cast<double>(state.completed_stage_units) /
                static_cast<double>(state.total_stage_units);
        }
        if (!std::isfinite(progress_fraction)) progress_fraction = 0.0;
        const float fill_width = std::clamp(
            track_width * static_cast<float>(std::clamp(progress_fraction, 0.0, 1.0)),
            0.0f, track_width);

        if (rectangles_enabled_ && panel_width > 0.0f && panel_height > 0.0f &&
            track_width > 0.0f && track_height > 0.0f) {
            const runtime_layouts::FLinearColor panel_color{0.02f, 0.03f, 0.05f, 0.72f};
            const runtime_layouts::FLinearColor track_color{0.15f, 0.18f, 0.24f, 0.85f};
            const runtime_layouts::FLinearColor fill_color{0.20f, 0.70f, 1.00f, 0.95f};
            try {
                bool rectangles_ok = draw_tile_(fcanvas, panel_x, panel_y, panel_width,
                    panel_height, 0.0f, 0.0f, 1.0f, 1.0f, &panel_color, nullptr, true);
                rectangles_ok = rectangles_ok && draw_tile_(fcanvas, track_x, track_y,
                    track_width, track_height, 0.0f, 0.0f, 1.0f, 1.0f,
                    &track_color, nullptr, true);
                if (rectangles_ok && fill_width > 0.0f) {
                    rectangles_ok = draw_tile_(fcanvas, track_x, track_y, fill_width,
                        track_height, 0.0f, 0.0f, 1.0f, 1.0f,
                        &fill_color, nullptr, true);
                }
                if (!rectangles_ok) rectangles_enabled_ = false;
            } catch (...) {
                rectangles_enabled_ = false;
            }
        }

        if (!draw_) {
            enabled_ = false;
            return false;
        }
        const runtime_layouts::FLinearColor color{0.96f, 0.96f, 1.0f, 1.0f};
        (void)draw_(fcanvas, x, title_y, headline, font, &color);
        (void)draw_(fcanvas, x, detail_y, detail.data(), font, &color);
        (void)draw_(fcanvas, x, estimates_y, status.data(), font, &color);
        if (state.phase == startup::StartupCachePhase::Loading) {
            (void)draw_(fcanvas, x, eta_y, estimates.data(), font, &color);
        }
        if (state.phase == startup::StartupCachePhase::Ready &&
            !ready_drawn_) {
            ready_drawn_ = true;
            hide_deadline_ = now + kReadyVisibility;
        }
        return true;
    } catch (...) {
        enabled_ = false;
        return false;
    }
}

void dispatch_startup_post_render(PostRenderFn original, void* viewport, void* canvas,
    OverlayRenderCallback overlay) noexcept
{
    if (original) original(viewport, canvas);
    if (overlay) {
        try {
            overlay(viewport, canvas);
        } catch (...) {
        }
    }
}

bool install_startup_cache_overlay(HMODULE exe_module,
    startup::StartupCacheProgress& progress) noexcept
{
    try {
        if (g_install_attempted) return g_post_render_hook.installed();
        g_install_attempted = true;
        const HookSpec* hook = find_hook_spec("game_viewport_client_post_render");
        if (!hook || hook->rva != rva::GameViewportClientPostRender ||
            !validate_signature(exe_module, "game_viewport_client_draw_post_render_call",
                rva::GameViewportClientDrawPostRenderCall) ||
            !validate_signature(exe_module, "fcanvas_draw_shadowed_string_default_black",
                rva::FCanvasDrawShadowedStringDefaultBlack)) {
            log_overlay_noexcept(core::LogLevel::Error,
                "[startup_overlay] status=disabled_signature_validation_failed");
            return false;
        }

        g_draw_shadowed_string = reinterpret_cast<DrawShadowedStringFn>(
            reinterpret_cast<uintptr_t>(exe_module) + rva::FCanvasDrawShadowedStringDefaultBlack);
        if (validate_signature(exe_module, "fcanvas_draw_tile", rva::FCanvasDrawTile)) {
            g_draw_tile = reinterpret_cast<DrawTileFn>(
                reinterpret_cast<uintptr_t>(exe_module) + rva::FCanvasDrawTile);
        } else {
            log_overlay_noexcept(core::LogLevel::Info,
                "[startup_overlay] rectangles=disabled signature_validation_failed");
        }
        static StartupCacheOverlayRenderer renderer(progress, production_read, production_draw,
            g_draw_tile ? OverlayDrawTile(production_draw_tile) : OverlayDrawTile{});
        g_renderer = &renderer;
        std::string error;
        if (!g_post_render_hook.install(exe_module, hook->rva, hook->expected_prologue,
                reinterpret_cast<void*>(post_render_detour),
                reinterpret_cast<void**>(&g_original_post_render), error)) {
            g_renderer = nullptr;
            g_draw_shadowed_string = nullptr;
            g_draw_tile = nullptr;
            log_overlay_noexcept(core::LogLevel::Error,
                "[startup_overlay] status=disabled_hook_install_failed error=" + error);
            return false;
        }
        g_callback_gate.open();
        log_overlay_noexcept(core::LogLevel::Info, "[startup_overlay] status=installed");
        return true;
    } catch (...) {
        // A successfully enabled hook remains a process-lifetime pass-through while its gate is
        // closed. This keeps overlay setup failures isolated from repository and release startup.
        log_overlay_noexcept(core::LogLevel::Error,
            "[startup_overlay] status=disabled_unexpected_setup_failure");
        return false;
    }
}

} // namespace ff7r::piano::game
