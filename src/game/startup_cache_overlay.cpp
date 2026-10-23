#include "game/startup_cache_overlay.h"

#include "core/hooks.h"
#include "core/logging.h"
#include "core/pe_image.h"
#include "game/hook_specs.h"
#include "game/rvas.h"
#include "game/song_preview_overlay.h"
#include "game/song_preview_anchor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <span>
#include <string>
#include <string_view>

namespace ff7r::piano::game {
namespace {

constexpr auto kReadyHold = std::chrono::seconds(2);
constexpr auto kReadyFade = std::chrono::milliseconds(400);
constexpr float kRightMargin = 28.0f;
constexpr float kTopMargin = 28.0f;
constexpr float kLineHeight = 22.0f;
constexpr float kPanelWidth = kStartupCacheOverlayPanelWidth;
constexpr float kPanelHeight = kStartupCacheOverlayPanelHeight;
constexpr float kPanelPadding = 16.0f;
constexpr float kTrackHeight = 4.0f;
constexpr float kTrackBottomMargin = 14.0f;
constexpr float kCell = 2.0f;

using Color = runtime_layouts::FLinearColor;

// Pixel art, one canvas cell per character; '.' is transparent. Cloud rides a chocobo
// facing right; the legs alternate under the body to make a four-frame run cycle.
constexpr std::array<std::string_view, 21> kChocoboBody{
    "............H......KK...........",
    "..........H.HH....KYK..KK.......",
    "...G.....HHHHHH...KYYKKYYK......",
    "...gG...hHHHHHHH...KYYYYYYKK....",
    "....gG...hHHHHHSS.KYYYYYYYYYK...",
    "....gG...hHHHSKS.KYYYYYYYKWYK...",
    ".....gG...hHSSSS.KYYYYYYYKKYOOK.",
    "......gG...KSSS..KYYYYYYYYYOOOOK",
    ".......gGBPGGPp...KYYYYYYYKKKK..",
    "........BPPGGPPS...KYYYYYK......",
    "..KK.....PPPPPPS..KYYYYYK.......",
    ".KYYK....pBBBBP..KYYYYYK........",
    "KYyYYK..KYYYPPPPPYYYYYYYK.......",
    "KYyyYYK.KYYYYYYPPYYYYYYK........",
    ".KYyyYYKYYYYYYYPPYYYYYYK........",
    "..KYyyYYYYYYYYYPPYYYYYYK........",
    "...KYYYYYYYYYYYBBYYYYYK.........",
    "....KyYYYYYYYYYBBYYYYK..........",
    ".....KyyYYYYYYYYYYYYyK..........",
    "......KKyyyyyyyyyyyyK...........",
    "........KKKKKKKKKKKK............",
};
constexpr std::array<std::array<std::string_view, 5>, 4> kChocoboLegs{{
    {"..........KOK...KOK.............", ".........KOK.....KOK............",
     "........KOK.......KOK...........", ".......KOK.........KOK..........",
     "....KOOOK...........KOOOK......."},
    {"...........KOK..KOK.............", "............KOK.KOK.............",
     "............KOKKOK..............", "...........KOOOOOK..............", ""},
    {"..........KOK...KOK.............", "..........KOK....KOK............",
     ".........KOK......KOK...........", "........KOK........KOK..........",
     ".....KOOOK..........KOOOK......."},
    {"..........KOK...KOK.............", "........KOOK.....KOK............",
     "......KOOK........KOK...........", ".....KK...........KOOOK.........", ""},
}};
constexpr std::array<std::string_view, 7> kGysahlGreen{
    "..l.L.", ".lLLl.", "lLLlLl", ".lLLl.", "..CC..", "..CC..", "..Cc..",
};
constexpr float kSpriteWidth = 32 * kCell;
constexpr float kSpriteHeight = 26 * kCell;

// Linear canvas colours (sRGB^2.2); alpha 0 marks a transparent cell.
constexpr Color sprite_color(const char cell) noexcept
{
    switch (cell) {
    case 'K': return {.004f, .002f, .008f, 1};   // outline
    case 'Y': return {1, .625f, .025f, 1};       // chocobo feathers
    case 'y': return {.68f, .267f, .006f, 1};
    case 'O': return {.875f, .19f, .01f, 1};     // beak and legs
    case 'W': return {1, 1, 1, 1};
    case 'H': return {1, .843f, .19f, 1};        // Cloud's hair
    case 'h': return {.68f, .41f, .041f, 1};
    case 'S': return {.957f, .586f, .359f, 1};   // skin
    case 'P': return {.058f, .038f, .19f, 1};    // SOLDIER uniform
    case 'p': return {.017f, .01f, .074f, 1};
    case 'B': return {.157f, .051f, .012f, 1};   // belts and boots
    case 'G': return {.523f, .586f, .68f, 1};    // Buster Sword
    case 'g': return {.157f, .184f, .243f, 1};
    case 'L': return {.101f, .586f, .058f, 1};   // Gysahl Green
    case 'l': return {.017f, .227f, .028f, 1};
    case 'C': return {.797f, .875f, .586f, 1};
    case 'c': return {.465f, .523f, .311f, 1};
    default: return {};
    }
}

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
    case Stage::Inspecting: return L"Reading song files";
    case Stage::ValidatingCache: return L"Verifying cached songs";
    case Stage::DecodingAudio: return L"Decoding song audio";
    case Stage::GeneratingChart: return L"Generating piano charts";
    case Stage::BuildingAudioCache: return L"Converting audio for the game";
    case Stage::PublishingCache: return L"Saving song cache";
    case Stage::Complete: return L"Finishing up";
    }
    return L"Preparing piano songs";
}

const wchar_t* plural(const std::size_t count) noexcept { return count == 1 ? L"" : L"s"; }

void append_text(wchar_t* output, const std::size_t output_size, const wchar_t* format,
    const std::size_t value) noexcept
{
    const std::size_t used = std::wcslen(output);
    if (used + 1 < output_size) std::swprintf(output + used, output_size - used, format, value);
}

// One tile per run of equal cells keeps a sprite row to a handful of DrawTile calls.
bool draw_sprite(const OverlayDrawTile& tile, void* fcanvas, std::span<const std::string_view> rows,
    const float x, const float y, const float opacity)
{
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const auto row = rows[r];
        for (std::size_t c = 0; c < row.size();) {
            std::size_t end = c + 1;
            while (end < row.size() && row[end] == row[c]) ++end;
            Color color = sprite_color(row[c]);
            color.a *= opacity;
            if (color.a > 0 && !tile(fcanvas, x + static_cast<float>(c) * kCell,
                    y + static_cast<float>(r) * kCell, static_cast<float>(end - c) * kCell, kCell,
                    0, 0, 1, 1, &color, nullptr, true)) return false;
            c = end;
        }
    }
    return true;
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

// Render-thread only: the last shown badge keeps fading out after the preview stops
// or the row changes. Leaked so process exit never releases its storage reference.
struct PreviewBadgeFade {
    SongPreviewRenderSnapshot shown;
    PreviewBadgeLayout layout;
    menu_theme_plan::PreviewPalette palette;
    float opacity = 0;
    std::chrono::steady_clock::time_point last;
};

bool preview_badge_active(const SongPreviewRenderSnapshot& snapshot) noexcept
{
    return snapshot && (snapshot.phase == PreviewPhase::Loading
        || snapshot.phase == PreviewPhase::Ready || snapshot.phase == PreviewPhase::Playing);
}

void render_preview_badge(void* viewport, void* canvas,
    const std::chrono::steady_clock::time_point now) noexcept
{
    static auto& fade = *new PreviewBadgeFade{};
    constexpr float fade_seconds = .15f;
    const float step = fade.last == std::chrono::steady_clock::time_point{} ? 0
        : std::chrono::duration<float>(now - fade.last).count() / fade_seconds;
    fade.last = now;
    void* engine = nullptr;
    void* font = nullptr;
    void* fcanvas = nullptr;
    float width = 0, height = 0;
    const OverlayNativeRead read = production_read;
    if (!read_value(read, viewport, runtime_layouts::UObject::outer, engine)
        || !read_value(read, engine, runtime_layouts::UEngine::small_font, font)
        || !read_value(read, canvas, runtime_layouts::UCanvas::canvas, fcanvas)
        || !read_value(read, canvas, runtime_layouts::UCanvas::clip_x, width)
        || !read_value(read, canvas, runtime_layouts::UCanvas::clip_y, height)) {
        fade.shown = {};
        fade.opacity = 0;
        return;
    }
    // Shown only once a selection has dwelled into loading or playback, never while scrolling.
    const auto snapshot = capture_song_preview_render_snapshot();
    PreviewSelectedRowBounds row;
    std::optional<PreviewBadgeLayout> layout;
    if (preview_badge_active(snapshot) && capture_song_preview_row_bounds(snapshot, viewport, canvas, row))
        layout = preview_badge_layout(row, width, height);
    const auto current = layout ? capture_song_preview_render_snapshot() : SongPreviewRenderSnapshot{};
    if (preview_badge_active(current) && current.session_generation == snapshot.session_generation
        && current.selection.generation == snapshot.selection.generation
        && current.ticket == snapshot.ticket) {
        if (fade.shown.session_generation != current.session_generation) fade.opacity = 0;
        fade.shown = current;
        fade.layout = *layout;
        fade.opacity = std::min(1.0f, fade.opacity + step);
    } else {
        fade.opacity = std::max(0.0f, fade.opacity - step);
    }
    if (fade.opacity <= 0) { fade.shown = {}; return; }
    // Copy once, release the mailbox lock before any Canvas/native draw. A closed
    // or newer owner must not repaint a fading old-session selection.
    if (!preview_badge_palette(capture_song_preview_palette(), fade.shown.session_generation, fade.palette)) {
        fade.shown = {}; fade.opacity = 0; return;
    }
    (void)draw_song_preview_badge(fade.shown, fade.layout, fcanvas, font,
        production_draw, production_draw_tile, fade.opacity, fade.palette);
}

void render_callback(void* viewport, void* canvas) noexcept
{
    auto lease = g_callback_gate.try_enter();
    if (!lease || !g_renderer) return;
    const auto now = std::chrono::steady_clock::now();
    (void)g_renderer->render(viewport, canvas, now);
    // Startup hiding never retires the shared PostRender owner or preview rendering.
    render_preview_badge(viewport, canvas, now);
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
        using Phase = startup::StartupCachePhase;
        std::array<wchar_t, kStartupCacheOverlayTextCapacity> counts{};
        std::array<wchar_t, kStartupCacheOverlayTextCapacity> activity{};
        const wchar_t* headline = L"Piano Songs: Looking for songs";
        if (state.phase == Phase::Ready) {
            headline = L"Piano Songs: Ready!";
            if (state.candidate_count == 0) {
                std::swprintf(counts.data(), counts.size(), L"No songs found in the Music folder");
            } else {
                std::swprintf(counts.data(), counts.size(), L"%zu song%ls ready to play",
                    state.published_song_count, plural(state.published_song_count));
            }
            if (state.failed_candidates != 0) append_text(counts.data(), counts.size(),
                L"  |  %zu skipped (see FF7RPianoSongs.log)", state.failed_candidates);
        } else if (state.phase == Phase::Starting) {
            std::swprintf(counts.data(), counts.size(), L"Scanning the Music folder");
        } else {
            std::swprintf(counts.data(), counts.size(), L"%zu of %zu songs checked  |  %zu ready",
                state.completed_candidates, state.candidate_count,
                state.available_on_reopen_song_count);
            if (state.failed_candidates != 0) append_text(counts.data(), counts.size(),
                L"  |  %zu skipped", state.failed_candidates);
        }
        if (state.phase == Phase::Failed) {
            headline = L"Piano Songs: Setup failed";
            std::swprintf(activity.data(), activity.size(), L"See FF7RPianoSongs.log for details");
        } else if (state.phase == Phase::Publishing) {
            headline = L"Piano Songs: Almost done";
            std::swprintf(activity.data(), activity.size(), L"Finishing up");
        } else if (state.phase == Phase::Loading) {
            const std::size_t percent = state.total_stage_units == 0 ? 100 :
                std::min<std::size_t>(99,
                    state.completed_stage_units * 100 / state.total_stage_units);
            if (state.active_candidate_count != 0 && rebuild_stage(state.latest_stage)) {
                headline = L"Piano Songs: Building new songs";
                std::array<wchar_t, 32> total{};
                if (state.estimates_available) format_duration(
                    state.estimated_total_remaining_seconds, total.data(), total.size());
                std::swprintf(activity.data(), activity.size(),
                    state.estimates_available ? L"%ls - %zu%% - about %ls left"
                                              : L"%ls - %zu%% - estimating time left",
                    stage_text(state.latest_stage), percent, total.data());
            } else {
                headline = L"Piano Songs: Checking songs";
                std::swprintf(activity.data(), activity.size(),
                    L"Verifying cached songs - %zu%%", percent);
            }
        }

        const float panel_width = std::clamp(std::min(kPanelWidth, clip_x), 0.0f, clip_x);
        const float panel_height = std::clamp(std::min(kPanelHeight, clip_y), 0.0f, clip_y);
        const float panel_x = std::clamp(clip_x - panel_width - kRightMargin,
            0.0f, std::max(0.0f, clip_x - panel_width));
        const float panel_y = std::clamp(kTopMargin - 12.0f,
            0.0f, std::max(0.0f, clip_y - panel_height));
        const float x = std::clamp(panel_x + kPanelPadding, 0.0f, clip_x);
        const float title_y = std::clamp(panel_y + 12.0f, 0.0f, clip_y);
        const float counts_y = std::clamp(title_y + kLineHeight, 0.0f, clip_y);
        const float activity_y = std::clamp(counts_y + kLineHeight, 0.0f, clip_y);

        // The track ends short of the Gysahl Green the chocobo runs toward.
        const float green_x = panel_x + panel_width - kPanelPadding - 6 * kCell;
        const float track_x = panel_x + kPanelPadding;
        const float track_width = std::max(0.0f, green_x - 8.0f - track_x);
        const float track_y = panel_y + panel_height - kTrackBottomMargin - kTrackHeight;
        double target = 0.0;
        if (state.phase == Phase::Publishing || state.phase == Phase::Ready) {
            target = 1.0;
        } else if (state.total_stage_units != 0) {
            target = static_cast<double>(state.completed_stage_units) /
                static_cast<double>(state.total_stage_units);
        }
        if (!std::isfinite(target)) target = 0.0;
        target = std::clamp(target, 0.0, 1.0);
        // Eases toward stage-unit jumps so the chocobo runs instead of teleporting.
        const double elapsed = std::clamp(
            std::chrono::duration<double>(now - last_frame_).count(), 0.0, 0.25);
        shown_fraction_ = shown_fraction_ < 0.0 || target < shown_fraction_ ? target
            : shown_fraction_ + (target - shown_fraction_) * std::min(1.0, elapsed * 3.0);
        last_frame_ = now;
        const float fraction = static_cast<float>(shown_fraction_);

        // Ready fades out at the end; the native text shadow is opaque black,
        // so text stops once the panel is mostly transparent.
        float opacity = 1.0f;
        if (ready_drawn_) {
            opacity = std::clamp(std::chrono::duration<float>(hide_deadline_ - now).count() /
                std::chrono::duration<float>(kReadyFade).count(), 0.0f, 1.0f);
        }
        const auto fade = [opacity](Color color) { color.a *= opacity; return color; };

        const bool running = state.phase == Phase::Starting || state.phase == Phase::Loading ||
            state.phase == Phase::Publishing;
        const auto ticks = std::max<long long>(0,
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        // Run cycle while working, a happy hop when ready, standing still on failure.
        const std::size_t frame = running ? static_cast<std::size_t>(ticks / 110 % 4)
            : state.phase == Phase::Ready ? static_cast<std::size_t>(ticks / 220 % 2) : 0;
        const float sprite_x = track_x + std::max(0.0f, track_width - kSpriteWidth) * fraction;
        const float sprite_y = track_y - kSpriteHeight + (frame % 2 ? 0.0f : kCell);
        const bool sprite_lane = panel_width == kPanelWidth && panel_height == kPanelHeight;
        bool sprite_drawn = false;

        if (rectangles_enabled_ && panel_width > 0.0f && panel_height > 0.0f) {
            // Native FF7R menu trim, as on the song preview badge.
            const Color trim = fade(state.phase == Phase::Failed
                ? Color{.60f, .06f, .04f, 1} : Color{.64f, .49f, .22f, 1});
            const Color navy = fade({.012f, .024f, .30f, .94f});
            const Color groove = fade({.03f, .07f, .45f, 1});
            const Color cyan = fade({.12f, .66f, .94f, 1});
            const auto rect = [&](const float rx, const float ry, const float width,
                const float height, const Color& color) {
                return draw_tile_(fcanvas, rx, ry, width, height, 0.0f, 0.0f, 1.0f, 1.0f,
                    &color, nullptr, true);
            };
            try {
                bool ok = rect(panel_x, panel_y, panel_width, panel_height, trim) &&
                    rect(panel_x + 1, panel_y + 1, panel_width - 2, panel_height - 2, navy);
                if (ok && sprite_lane) {
                    ok = rect(track_x, track_y, track_width, kTrackHeight, groove) &&
                        (fraction <= 0.0f || rect(track_x, track_y, track_width * fraction,
                            kTrackHeight, cyan)) &&
                        draw_sprite(draw_tile_, fcanvas, kGysahlGreen, green_x,
                            track_y + kTrackHeight - 7 * kCell, opacity);
                    // Dust puffs trail behind the running chocobo and shrink as they drift.
                    for (int puff = 0; ok && running && puff < 3; ++puff) {
                        const float t = std::fmod(static_cast<float>(ticks % 600) / 600.0f +
                            static_cast<float>(puff) / 3.0f, 1.0f);
                        const float size = kCell * (1.0f + 2.0f * (1.0f - t));
                        ok = rect(sprite_x + 6 * kCell - t * 10 * kCell - size,
                            track_y - size - t * 3 * kCell, size, size,
                            fade({.45f, .47f, .62f, .6f * (1.0f - t)}));
                    }
                    ok = ok && draw_sprite(draw_tile_, fcanvas, kChocoboBody, sprite_x, sprite_y,
                        opacity) && draw_sprite(draw_tile_, fcanvas, kChocoboLegs[frame], sprite_x,
                        sprite_y + static_cast<float>(kChocoboBody.size()) * kCell, opacity);
                    sprite_drawn = ok;
                }
                if (!ok) rectangles_enabled_ = false;
            } catch (...) {
                rectangles_enabled_ = false;
            }
        }

        if (!draw_) {
            enabled_ = false;
            return false;
        }
        if (opacity >= 0.5f) {
            const Color white = fade({0.96f, 0.96f, 1.0f, 1.0f});
            const Color title = fade(state.phase == Phase::Failed
                ? Color{1.0f, .18f, .12f, 1} : Color{1.0f, .74f, .30f, 1});
            (void)draw_(fcanvas, x, title_y, headline, font, &title);
            (void)draw_(fcanvas, x, counts_y, counts.data(), font, &white);
            (void)draw_(fcanvas, x, activity_y, activity.data(), font, &white);
            if (sprite_drawn && state.phase == Phase::Ready) {
                (void)draw_(fcanvas, sprite_x + 36.0f, activity_y, L"Kweh!", font, &title);
            }
        }
        if (state.phase == Phase::Ready && !ready_drawn_) {
            ready_drawn_ = true;
            hide_deadline_ = now + kReadyHold + kReadyFade;
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
        configure_song_preview_anchor(exe_module);
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
