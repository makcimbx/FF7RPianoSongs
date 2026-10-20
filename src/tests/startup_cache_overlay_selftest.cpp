#include "game/startup_cache_overlay.h"
#include "game/song_preview_overlay.h"

namespace ff7r::piano::game {
SongPreviewRenderSnapshot capture_song_preview_render_snapshot() noexcept { return {}; }
void configure_song_preview_anchor(HMODULE) noexcept {}
bool capture_song_preview_row_bounds(const SongPreviewRenderSnapshot&,void*,void*,
    PreviewSelectedRowBounds&) noexcept { return false; }
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace ff7r::piano;

bool require(const bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "startup_cache_overlay_selftest: " << message << '\n';
    return false;
}

bool contains_catalog_ux(const std::vector<std::wstring>& lines)
{
    for (const auto& line : lines) {
        std::wstring lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        if (lower.find(L"current menu") != std::wstring::npos ||
            lower.find(L"current-menu") != std::wstring::npos ||
            lower.find(L"reopen") != std::wstring::npos ||
            lower.find(L"adoption") != std::wstring::npos ||
            lower.find(L"deferred") != std::wstring::npos ||
            lower.find(L"repository") != std::wstring::npos ||
            lower.find(L"registry") != std::wstring::npos ||
            lower.find(L"active") != std::wstring::npos ||
            lower.find(L"->") != std::wstring::npos) return true;
    }
    return false;
}

int original_calls = 0;
int overlay_calls = 0;
std::vector<int> dispatch_order;

void __fastcall original(void*, void*)
{
    ++original_calls;
    dispatch_order.push_back(1);
}

void overlay(void*, void*) noexcept
{
    ++overlay_calls;
    dispatch_order.push_back(2);
}

struct NativeFixture {
    std::map<std::pair<void*, uintptr_t>, std::vector<unsigned char>> values;

    template <typename T>
    void put(void* base, const uintptr_t offset, const T& value)
    {
        std::vector<unsigned char> bytes(sizeof(value));
        std::memcpy(bytes.data(), &value, sizeof(value));
        values[{base, offset}] = std::move(bytes);
    }

    bool read(void* base, const uintptr_t offset, void* output, const std::size_t size) const
    {
        const auto found = values.find({base, offset});
        if (found == values.end() || found->second.size() != size) return false;
        std::memcpy(output, found->second.data(), size);
        return true;
    }
};

struct TileCall {
    void* canvas = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float size_u = 0.0f;
    float size_v = 0.0f;
    game::runtime_layouts::FLinearColor color{};
    const void* texture = reinterpret_cast<void*>(1);
    bool alpha_blend = false;
};

bool close_float(const float left, const float right)
{
    return std::fabs(left - right) < 0.001f;
}

bool same_color(const game::runtime_layouts::FLinearColor& left,
    const game::runtime_layouts::FLinearColor& right)
{
    return close_float(left.r, right.r) && close_float(left.g, right.g) &&
        close_float(left.b, right.b) && close_float(left.a, right.a);
}

// Every tile must be an untextured alpha-blended rectangle inside the panel.
bool tiles_inside(const std::vector<TileCall>& tiles, void* fcanvas, const float x,
    const float y, const float width, const float height)
{
    return std::all_of(tiles.begin(), tiles.end(), [&](const TileCall& tile) {
        return tile.canvas == fcanvas && tile.texture == nullptr && tile.alpha_blend &&
            close_float(tile.u, 0.0f) && close_float(tile.v, 0.0f) &&
            close_float(tile.size_u, 1.0f) && close_float(tile.size_v, 1.0f) &&
            tile.width > 0.0f && tile.height > 0.0f && tile.x >= x && tile.y >= y &&
            tile.x + tile.width <= x + width && tile.y + tile.height <= y + height;
    });
}

// Leftmost chocobo feather, i.e. where the sprite is currently running.
float chocobo_x(const std::vector<TileCall>& tiles)
{
    float left = 1e9f;
    for (const auto& tile : tiles) {
        if (same_color(tile.color, {1.0f, 0.625f, 0.025f, 1.0f})) left = std::min(left, tile.x);
    }
    return left;
}

struct Capture {
    std::vector<TileCall> tiles;
    std::vector<std::wstring> text;

    game::OverlayDrawText draw_text()
    {
        return [this](void*, float, float, const wchar_t* value, void*,
            const game::runtime_layouts::FLinearColor*) {
            text.emplace_back(value);
            return int32_t{1};
        };
    }

    game::OverlayDrawTile draw_tile()
    {
        return [this](void* observed_canvas, float x, float y, float width, float height,
            float u, float v, float size_u, float size_v,
            const game::runtime_layouts::FLinearColor* color, const void* texture,
            bool alpha_blend) {
            tiles.push_back({observed_canvas, x, y, width, height, u, v,
                size_u, size_v, *color, texture, alpha_blend});
            return true;
        };
    }
};

} // namespace

int main()
{
    bool ok = true;
    static_assert(std::is_trivially_copyable_v<startup::StartupCacheProgressSnapshot>);
    static_assert(sizeof(startup::StartupCacheProgressSnapshot) <= 128,
        "overlay snapshot must remain a fixed bounded O(1) value");
    game::dispatch_startup_post_render(original, reinterpret_cast<void*>(1),
        reinterpret_cast<void*>(2), overlay);
    ok &= require(original_calls == 1 && overlay_calls == 1 &&
        dispatch_order == std::vector<int>({1, 2}),
        "dispatch must call the original exactly once before the overlay");
    game::dispatch_startup_post_render(original, nullptr, nullptr, nullptr);
    ok &= require(original_calls == 2 && overlay_calls == 1,
        "pass-through dispatch must call the original exactly once");

    startup::StartupCacheProgress progress;
    progress.begin(2);
    progress.observe({1, ff7rp::pipeline::SongLoadProgressStage::PublishingCache});
    const auto advanced = progress.snapshot();
    progress.observe({0, ff7rp::pipeline::SongLoadProgressStage::DecodingAudio});
    progress.observe({1, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    progress.observe({1, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto reversed = progress.snapshot();
    ok &= require(reversed.completed_stage_units >= advanced.completed_stage_units &&
        reversed.completed_candidates == 1 &&
        reversed.active_candidate_count == 1 &&
        reversed.latest_stage == ff7rp::pipeline::SongLoadProgressStage::DecodingAudio,
        "out-of-order workers must remain monotonic and display an active candidate stage");
    startup::StartupCacheProgressSnapshot nonblocking_snapshot;
    ok &= require(progress.try_snapshot(nonblocking_snapshot) &&
        nonblocking_snapshot.completed_candidates == reversed.completed_candidates,
        "render-facing snapshot acquisition must expose the latest state when uncontended");

    startup::StartupCacheProgress activity;
    activity.begin(2);
    activity.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    activity.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    activity.observe({0, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    activity.observe({1, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    activity.observe({1, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Failed});
    auto activity_snapshot = activity.snapshot();
    ok &= require(activity_snapshot.completed_candidates == 2 &&
        activity_snapshot.active_candidate_count == 0,
        "direct, duplicate, stale lower-stage, and failed terminal events must not leak active workers");
    activity.begin(1);
    activity.observe({0, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    ok &= require(activity.snapshot().active_candidate_count == 1,
        "an unfinished candidate must count as active before reset");
    activity.begin(1);
    ok &= require(activity.snapshot().active_candidate_count == 0,
        "a second begin must clear a nonzero active count immediately");
    activity.observe({0, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    activity.publishing();
    ok &= require(activity.snapshot().active_candidate_count == 0,
        "Publishing must clear terminal active-work accounting");
    activity.begin(1);
    activity.observe({0, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    activity.ready(0);
    ok &= require(activity.snapshot().active_candidate_count == 0,
        "Ready must clear terminal active-work accounting");
    activity.begin(1);
    activity.observe({0, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    activity.failed();
    ok &= require(activity.snapshot().active_candidate_count == 0,
        "Failed must clear terminal active-work accounting");

    NativeFixture native;
    void* viewport = reinterpret_cast<void*>(0x1000);
    void* canvas = reinterpret_cast<void*>(0x2000);
    void* engine = reinterpret_cast<void*>(0x3000);
    void* font = reinterpret_cast<void*>(0x4000);
    void* fcanvas = reinterpret_cast<void*>(0x5000);
    const float clip_x = 1920.0f;
    const float clip_y = 1080.0f;
    native.put(viewport, game::runtime_layouts::UObject::outer, engine);
    native.put(engine, game::runtime_layouts::UEngine::small_font, font);
    native.put(canvas, game::runtime_layouts::UCanvas::canvas, fcanvas);
    native.put(canvas, game::runtime_layouts::UCanvas::clip_x, clip_x);
    native.put(canvas, game::runtime_layouts::UCanvas::clip_y, clip_y);

    const auto native_read = [&](void* base, uintptr_t offset, void* output, std::size_t size) {
        return native.read(base, offset, output, size);
    };
    const auto epoch = std::chrono::steady_clock::time_point{};

    startup::StartupCacheProgress starting_progress;
    Capture starting;
    game::StartupCacheOverlayRenderer starting_renderer(starting_progress, native_read,
        starting.draw_text());
    ok &= require(starting_renderer.render(viewport, canvas, epoch) &&
        starting.text == std::vector<std::wstring>({
            L"Piano Songs: Looking for songs",
            L"Scanning the Music folder",
            L""}) && !contains_catalog_ux(starting.text),
        "Starting must use bounded player-facing cache vocabulary");

    startup::StartupCacheProgress zero_progress;
    zero_progress.begin(2);
    Capture zero;
    std::vector<int> zero_order;
    game::StartupCacheOverlayRenderer zero_renderer(zero_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            zero_order.push_back(2);
            zero.text.emplace_back(text);
            return int32_t{0};
        },
        [&](void* observed_canvas, float x, float y, float width, float height,
            float u, float v, float size_u, float size_v,
            const game::runtime_layouts::FLinearColor* color, const void* texture,
            bool alpha_blend) {
            zero_order.push_back(1);
            zero.tiles.push_back({observed_canvas, x, y, width, height, u, v,
                size_u, size_v, *color, texture, alpha_blend});
            return true;
        });
    ok &= require(zero_renderer.render(viewport, canvas, epoch) && zero.text.size() == 3 &&
        zero.tiles.size() > 3 && zero.tiles.size() < 256 &&
        std::is_sorted(zero_order.begin(), zero_order.end()),
        "Loading must draw every tile before all three text lines even when text metrics are zero");
    ok &= require(close_float(zero.tiles[0].x, 1332.0f) && close_float(zero.tiles[0].y, 16.0f) &&
        close_float(zero.tiles[0].width, 560.0f) && close_float(zero.tiles[0].height, 150.0f) &&
        close_float(zero.tiles[2].x, 1348.0f) && close_float(zero.tiles[2].y, 148.0f) &&
        close_float(zero.tiles[2].width, 508.0f) && close_float(zero.tiles[2].height, 4.0f) &&
        tiles_inside(zero.tiles, fcanvas, 1332.0f, 16.0f, 560.0f, 150.0f),
        "panel, track, and sprite geometry must stay bounded and deterministic");
    ok &= require(same_color(zero.tiles[0].color, {0.64f, 0.49f, 0.22f, 1.0f}) &&
        same_color(zero.tiles[1].color, {0.012f, 0.024f, 0.30f, 0.94f}) &&
        same_color(zero.tiles[2].color, {0.03f, 0.07f, 0.45f, 1.0f}),
        "the panel must use the native FF7R gold trim, navy fill, and groove colors");
    ok &= require(zero.text[0] == L"Piano Songs: Checking songs" &&
            zero.text[1] == L"0 of 2 songs checked  |  0 ready" &&
            zero.text[2] == L"Verifying cached songs - 0%" && !contains_catalog_ux(zero.text),
        "startup without a menu must show only bounded cache initialization facts");
    const auto first_frame = zero.tiles;
    zero.tiles.clear();
    ok &= require(zero_renderer.render(viewport, canvas, epoch + std::chrono::milliseconds(110)) &&
        zero.tiles.size() != first_frame.size() &&
        close_float(chocobo_x(zero.tiles), chocobo_x(first_frame)),
        "the chocobo must run in place over time while progress is unchanged");
    zero_progress.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto eased = zero_progress.snapshot();
    const float eased_target = 508.0f * static_cast<float>(eased.completed_stage_units) /
        static_cast<float>(eased.total_stage_units);
    zero.tiles.clear();
    ok &= require(zero_renderer.render(viewport, canvas, epoch + std::chrono::milliseconds(210)) &&
        eased_target > 0.0f && zero.tiles[3].width > 0.0f &&
        zero.tiles[3].width < eased_target * 0.5f &&
        chocobo_x(zero.tiles) > chocobo_x(first_frame),
        "progress jumps must ease the fill and chocobo forward instead of teleporting");

    startup::StartupCacheProgress partial_progress;
    partial_progress.begin(2);
    partial_progress.observe({0, ff7rp::pipeline::SongLoadProgressStage::DecodingAudio});
    partial_progress.available_on_reopen(1);
    Capture partial;
    game::StartupCacheOverlayRenderer partial_renderer(partial_progress, native_read,
        partial.draw_text(), partial.draw_tile());
    ok &= require(partial_renderer.render(viewport, canvas, epoch) &&
        partial.tiles.size() > 3 && partial.tiles[3].width > 0.0f &&
        partial.tiles[3].width < partial.tiles[2].width &&
        same_color(partial.tiles[3].color, {0.12f, 0.66f, 0.94f, 1.0f}) &&
        partial.text.size() == 3 &&
        partial.text[0] == L"Piano Songs: Building new songs" &&
        partial.text[1] == L"0 of 2 songs checked  |  1 ready" &&
        partial.text[2].starts_with(L"Decoding song audio - ") &&
        partial.text[2].ends_with(L" - estimating time left") &&
        !contains_catalog_ux(partial.text),
        "partial startup progress must distinguish rebuilding without catalog guidance");

    startup::StartupCacheProgress between_song_progress;
    between_song_progress.begin(2);
    between_song_progress.catalog_prepared(1);
    between_song_progress.catalog_adopted(1);
    between_song_progress.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    Capture between_song;
    game::StartupCacheOverlayRenderer between_song_renderer(between_song_progress, native_read,
        between_song.draw_text());
    ok &= require(between_song_renderer.render(viewport, canvas, epoch) &&
        between_song.text.size() == 3 &&
        between_song.text[0] == L"Piano Songs: Checking songs" &&
        between_song.text[2].starts_with(L"Verifying cached songs") &&
        !contains_catalog_ux(between_song.text),
        "a gap between candidates must be presented as ordinary cache checking");

    startup::StartupCacheProgress background_lifecycle_progress;
    background_lifecycle_progress.begin(3);
    background_lifecycle_progress.catalog_prepared(1);
    background_lifecycle_progress.catalog_adopted(1);
    background_lifecycle_progress.observe(
        {0, ff7rp::pipeline::SongLoadProgressStage::GeneratingChart});
    background_lifecycle_progress.catalog_prepared(2);
    background_lifecycle_progress.catalog_adoption_deferred();
    Capture background_lifecycle;
    game::StartupCacheOverlayRenderer background_lifecycle_renderer(
        background_lifecycle_progress, native_read, background_lifecycle.draw_text());
    ok &= require(background_lifecycle_renderer.render(viewport, canvas, epoch) &&
        background_lifecycle.text.size() == 3 &&
        background_lifecycle.text[0] == L"Piano Songs: Building new songs" &&
        background_lifecycle.text[1] == L"0 of 3 songs checked  |  2 ready" &&
        background_lifecycle.text[2].ends_with(L" - estimating time left") &&
        !contains_catalog_ux(background_lifecycle.text),
        "internal catalog lifecycle must not affect startup overlay vocabulary");

    startup::StartupCacheProgress full_progress;
    full_progress.begin(2);
    full_progress.publishing();
    Capture full;
    game::StartupCacheOverlayRenderer full_renderer(full_progress, native_read,
        full.draw_text(), full.draw_tile());
    ok &= require(full_renderer.render(viewport, canvas, epoch) && full.tiles.size() > 3 &&
        close_float(full.tiles[3].width, full.tiles[2].width) &&
        chocobo_x(full.tiles) > chocobo_x(first_frame) + 400.0f &&
        tiles_inside(full.tiles, fcanvas, 1332.0f, 16.0f, 560.0f, 150.0f) &&
        full.text == std::vector<std::wstring>({
            L"Piano Songs: Almost done",
            L"2 of 2 songs checked  |  0 ready",
            L"Finishing up"}) && !contains_catalog_ux(full.text),
        "Publishing must render a full track with the chocobo at the Gysahl Green");

    auto queue_now = epoch;
    startup::StartupCacheProgress queue_eta([&] { return queue_now; });
    queue_eta.begin(8);
    queue_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    queue_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::GeneratingChart});
    queue_now += std::chrono::minutes(2);
    queue_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto seven_unresolved = queue_eta.snapshot();
    ok &= require(seven_unresolved.estimates_available &&
        seven_unresolved.estimated_per_song_seconds == 120 &&
        seven_unresolved.estimated_total_remaining_seconds == 420,
        "seven unresolved candidates on two workers must estimate about seven minutes");
    queue_eta.observe({1, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    Capture warm_validation;
    game::StartupCacheOverlayRenderer warm_validation_renderer(queue_eta, native_read,
        warm_validation.draw_text());
    ok &= require(queue_eta.snapshot().estimates_available &&
        warm_validation_renderer.render(viewport, canvas, epoch) &&
        warm_validation.text.size() == 3 &&
        warm_validation.text[0] == L"Piano Songs: Checking songs" &&
        warm_validation.text[2].starts_with(L"Verifying cached songs") &&
        warm_validation.text[2].find(L"left") == std::wstring::npos &&
        !contains_catalog_ux(warm_validation.text),
        "ordinary warm validation must hide a persisted cold-rebuild estimate");
    queue_now += std::chrono::seconds(1);
    queue_eta.observe({1, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto six_unresolved = queue_eta.snapshot();
    ok &= require(six_unresolved.estimated_per_song_seconds == 120 &&
        six_unresolved.estimated_total_remaining_seconds == 360,
        "a warm completion must reduce cached total ETA without changing rebuild EMA");

    auto one_now = epoch;
    startup::StartupCacheProgress one_eta([&] { return one_now; });
    one_eta.begin(2);
    one_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::DecodingAudio});
    one_now += std::chrono::minutes(2);
    one_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto one_unresolved = one_eta.snapshot();
    ok &= require(one_unresolved.estimated_per_song_seconds == 120 &&
        one_unresolved.estimated_total_remaining_seconds == 120,
        "one unresolved candidate must retain one full per-song estimate");

    auto worker_now = epoch;
    startup::StartupCacheProgress worker_eta([&] { return worker_now; });
    worker_eta.begin(3);
    worker_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::DecodingAudio});
    worker_eta.observe({1, ff7rp::pipeline::SongLoadProgressStage::GeneratingChart});
    worker_now += std::chrono::minutes(2);
    worker_eta.observe({1, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto second_worker_first = worker_eta.snapshot();
    worker_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto first_worker_last = worker_eta.snapshot();
    ok &= require(second_worker_first.estimated_total_remaining_seconds == 120 &&
        first_worker_last.estimated_per_song_seconds == 120 &&
        first_worker_last.estimated_total_remaining_seconds == 120,
        "out-of-order worker completions must preserve count-based concurrency estimates");

    auto eta_now = epoch;
    startup::StartupCacheProgress eta_progress([&] { return eta_now; });
    eta_progress.begin(8);
    for (std::size_t candidate = 0; candidate < 3; ++candidate) {
        eta_progress.observe({candidate, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
        eta_now += std::chrono::seconds(1);
        eta_progress.observe({candidate, ff7rp::pipeline::SongLoadProgressStage::Complete,
            ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    }
    ok &= require(!eta_progress.snapshot().estimates_available,
        "three warm-cache completions must remain calculating");

    eta_progress.observe({3, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    eta_progress.observe({3, ff7rp::pipeline::SongLoadProgressStage::DecodingAudio});
    eta_now += std::chrono::seconds(40);
    eta_progress.observe({3, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto first_cold_estimate = eta_progress.snapshot();
    ok &= require(first_cold_estimate.estimates_available &&
        first_cold_estimate.estimated_per_song_seconds == 40 &&
        first_cold_estimate.estimated_total_remaining_seconds == 120,
        "the first completed cold rebuild must expose rebuild-only estimates");
    eta_progress.observe({4, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    eta_progress.observe({4, ff7rp::pipeline::SongLoadProgressStage::GeneratingChart});
    eta_now += std::chrono::seconds(80);
    eta_progress.observe({4, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto second_cold_estimate = eta_progress.snapshot();
    ok &= require(second_cold_estimate.estimates_available &&
        second_cold_estimate.estimated_per_song_seconds == 50,
        "later cold rebuilds must smooth the estimate after the first sample");

    eta_progress.observe({5, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    eta_progress.observe({5, ff7rp::pipeline::SongLoadProgressStage::PublishingCache});
    eta_now += std::chrono::seconds(60);
    ok &= require(eta_progress.snapshot().estimated_per_song_seconds == 50,
        "entering a rebuild stage without completion must not alter the estimate");
    eta_progress.observe({5, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    eta_progress.observe({6, ff7rp::pipeline::SongLoadProgressStage::GeneratingChart});
    const auto estimated = eta_progress.snapshot();
    ok &= require(estimated.phase == startup::StartupCachePhase::Loading &&
        estimated.estimates_available && estimated.estimated_per_song_seconds == 55 &&
        estimated.estimated_total_remaining_seconds == 55,
        "completed cold rebuilds must continue smoothing rebuild-only estimates");

    eta_progress.observe({7, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    eta_now += std::chrono::seconds(1);
    eta_progress.observe({7, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto after_warm = eta_progress.snapshot();
    ok &= require(after_warm.estimated_per_song_seconds == 55 &&
        after_warm.estimated_total_remaining_seconds == 55,
        "a later warm hit must update remaining work without contaminating rebuild EMA");

    eta_progress.observe({6, ff7rp::pipeline::SongLoadProgressStage::BuildingAudioCache});
    const auto event_updated = eta_progress.snapshot();
    ok &= require(event_updated.estimated_per_song_seconds == 55 &&
        event_updated.estimated_total_remaining_seconds == 55,
        "a typed progress event must refresh cached estimates without stage-fraction discounting");
    eta_now += std::chrono::hours(3);
    const auto time_only = eta_progress.snapshot();
    ok &= require(time_only.phase == startup::StartupCachePhase::Loading &&
        time_only.estimated_per_song_seconds == event_updated.estimated_per_song_seconds &&
        time_only.estimated_total_remaining_seconds ==
            event_updated.estimated_total_remaining_seconds,
        "clock advancement and snapshot alone must alter neither estimates nor readiness");
    Capture eta;
    game::StartupCacheOverlayRenderer eta_renderer(eta_progress, native_read, eta.draw_text());
    ok &= require(eta_renderer.render(viewport, canvas, eta_now) &&
        !eta_renderer.hidden() && eta.text.size() == 3 &&
        eta.text[0] == L"Piano Songs: Building new songs" &&
        eta.text[2].starts_with(L"Converting audio for the game - ") &&
        eta.text[2].ends_with(L" - about 55 sec left") &&
        !contains_catalog_ux(eta.text),
        "Loading must show the remaining rebuild time without catalog-internal state");

    auto bounded_now = epoch;
    startup::StartupCacheProgress bounded_eta([&] { return bounded_now; });
    bounded_eta.begin(4);
    bounded_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::Inspecting});
    bounded_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::DecodingAudio});
    bounded_now += std::chrono::hours(100);
    bounded_eta.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    const auto bounded_snapshot = bounded_eta.snapshot();
    ok &= require(bounded_snapshot.estimates_available &&
        bounded_snapshot.estimated_per_song_seconds == 86400 &&
        bounded_snapshot.estimated_total_remaining_seconds == 86400,
        "per-song and total estimates must remain bounded to one day");

    auto failure_now = epoch;
    startup::StartupCacheProgress failure_eta([&] { return failure_now; });
    failure_eta.begin(8);
    for (std::size_t candidate = 0; candidate < 3; ++candidate) {
        failure_eta.observe({candidate, ff7rp::pipeline::SongLoadProgressStage::DecodingAudio});
        failure_now += std::chrono::seconds(30 + candidate * 10);
        failure_eta.observe({candidate, ff7rp::pipeline::SongLoadProgressStage::Complete,
            ff7rp::pipeline::SongLoadTerminalOutcome::Failed});
    }
    ok &= require(!failure_eta.snapshot().estimates_available,
        "three failed cold rebuilds must not enable ETA confidence");
    for (std::size_t candidate = 3; candidate < 6; ++candidate) {
        failure_eta.observe({candidate, ff7rp::pipeline::SongLoadProgressStage::BuildingAudioCache});
        failure_now += std::chrono::seconds(20);
        failure_eta.observe({candidate, ff7rp::pipeline::SongLoadProgressStage::Complete,
            ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    }
    const auto before_late_failure = failure_eta.snapshot();
    failure_eta.observe({6, ff7rp::pipeline::SongLoadProgressStage::PublishingCache});
    failure_now += std::chrono::hours(2);
    failure_eta.observe({6, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Failed});
    const auto after_late_failure = failure_eta.snapshot();
    ok &= require(before_late_failure.estimates_available &&
        after_late_failure.estimated_per_song_seconds ==
            before_late_failure.estimated_per_song_seconds,
        "a cold failure after confidence must not alter rebuild EMA");

    startup::StartupCacheProgress failed_progress;
    failed_progress.begin(2);
    failed_progress.failed();
    Capture failed;
    game::StartupCacheOverlayRenderer failed_renderer(failed_progress, native_read,
        failed.draw_text(), failed.draw_tile());
    ok &= require(failed_renderer.render(viewport, canvas, epoch) &&
        failed.text == std::vector<std::wstring>({
            L"Piano Songs: Setup failed",
            L"0 of 2 songs checked  |  0 ready",
            L"See FF7RPianoSongs.log for details"}) && !contains_catalog_ux(failed.text) &&
        !failed.tiles.empty() && close_float(failed.tiles[0].color.r, 0.60f) &&
        close_float(failed.tiles[0].color.g, 0.06f),
        "Failed must use bounded player-facing vocabulary and an error trim");

    startup::StartupCacheProgress skipped_progress;
    skipped_progress.begin(3);
    skipped_progress.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Failed});
    skipped_progress.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Failed});
    skipped_progress.observe({1, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    skipped_progress.available_on_reopen(1);
    Capture skipped;
    game::StartupCacheOverlayRenderer skipped_renderer(skipped_progress, native_read,
        skipped.draw_text());
    ok &= require(skipped_progress.snapshot().failed_candidates == 1 &&
        skipped_renderer.render(viewport, canvas, epoch) && skipped.text.size() == 3 &&
        skipped.text[1] == L"2 of 3 songs checked  |  1 ready  |  1 skipped",
        "Loading must count each skipped song once");
    skipped_progress.observe({2, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    skipped_progress.publishing();
    skipped_progress.ready(1);
    skipped.text.clear();
    ok &= require(skipped_renderer.render(viewport, canvas, epoch) && skipped.text ==
        std::vector<std::wstring>({L"Piano Songs: Ready!",
            L"1 song ready to play  |  1 skipped (see FF7RPianoSongs.log)", L""}),
        "Ready must report skipped songs with singular wording");

    Capture celebration;
    game::StartupCacheOverlayRenderer celebration_renderer(skipped_progress, native_read,
        celebration.draw_text(), celebration.draw_tile());
    ok &= require(celebration_renderer.render(viewport, canvas, epoch) &&
        celebration.text.size() == 4 && celebration.text[3] == L"Kweh!" &&
        close_float(celebration.tiles[0].color.a, 1.0f) &&
        tiles_inside(celebration.tiles, fcanvas, 1332.0f, 16.0f, 560.0f, 150.0f),
        "Ready must celebrate once the chocobo reaches the Gysahl Green");
    celebration.text.clear();
    celebration.tiles.clear();
    ok &= require(celebration_renderer.render(viewport, canvas,
            epoch + std::chrono::milliseconds(2300)) &&
        celebration.text.empty() && !celebration.tiles.empty() &&
        close_float(celebration.tiles[0].color.a, 0.25f) && !celebration_renderer.hidden(),
        "Ready must fade out without drawing opaque text shadows");

    std::vector<std::wstring> drawn;
    game::StartupCacheOverlayRenderer renderer(progress,
        [&](void* base, uintptr_t offset, void* output, std::size_t size) {
            return native.read(base, offset, output, size);
        },
        [&](void* observed_canvas, float, float, const wchar_t* text, void* observed_font,
            const game::runtime_layouts::FLinearColor*) {
            if (observed_canvas != fcanvas || observed_font != font) return int32_t{0};
            drawn.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(!renderer.rectangles_enabled(),
        "an unavailable DrawTile helper must select text-only rendering immediately");
    ok &= require(!renderer.render(nullptr, nullptr, epoch) && drawn.empty() && renderer.enabled(),
        "missing native objects must retain state without disabling the overlay");
    ok &= require(renderer.render(viewport, canvas, epoch) && drawn.size() == 3,
        "a valid Loading frame must draw exactly three bounded text lines");

    progress.publishing();
    progress.ready(2);
    progress.catalog_adoption_deferred();
    drawn.clear();
    ok &= require(renderer.render(viewport, canvas, epoch + std::chrono::seconds(5)) &&
        drawn == std::vector<std::wstring>({
            L"Piano Songs: Ready!", L"2 songs ready to play", L""}) &&
        !contains_catalog_ux(drawn) && !renderer.hidden(),
        "repository settlement before any menu must show a bounded completion notice only");
    drawn.clear();
    ok &= require(renderer.render(viewport, canvas,
            epoch + std::chrono::seconds(6) + std::chrono::milliseconds(999)) &&
        drawn.size() == 3 && !renderer.hidden(),
        "completion notice must remain readable until its fade begins");
    drawn.clear();
    ok &= require(renderer.render(viewport, canvas,
            epoch + std::chrono::seconds(7) + std::chrono::milliseconds(300)) &&
        drawn.empty() && !renderer.hidden(),
        "a fading completion notice must drop text before its shadow outweighs the panel");
    ok &= require(!renderer.render(viewport, canvas,
            epoch + std::chrono::seconds(7) + std::chrono::milliseconds(400)) &&
        renderer.hidden() && drawn.empty(),
        "completion notice must hide even when a catalog refresh remains pending");
    progress.catalog_adopted(2);
    const auto aligned = progress.snapshot();
    ok &= require(aligned.active_song_count == 2 && !aligned.catalog_update_pending,
        "successful adoption must align active and prepared counts");

    const std::vector<std::wstring> ready_two({
        L"Piano Songs: Ready!", L"2 songs ready to play", L""});
    startup::StartupCacheProgress ready_pending;
    ready_pending.begin(2);
    ready_pending.catalog_prepared(1);
    ready_pending.catalog_adopted(1);
    ready_pending.catalog_prepared(2);
    ready_pending.publishing();
    ready_pending.ready(2);
    Capture ready_pending_capture;
    game::StartupCacheOverlayRenderer ready_pending_renderer(ready_pending, native_read,
        ready_pending_capture.draw_text());
    ok &= require(ready_pending_renderer.render(viewport, canvas, epoch) &&
        ready_pending_capture.text == ready_two &&
        !ready_pending_renderer.render(viewport, canvas,
            epoch + std::chrono::milliseconds(2400)) &&
        ready_pending_renderer.hidden(),
        "Ready with pending adoption must show one bounded catalog-neutral notice");

    ready_pending.catalog_adopted(2);
    Capture ready_aligned;
    game::StartupCacheOverlayRenderer ready_aligned_renderer(ready_pending, native_read,
        ready_aligned.draw_text());
    ok &= require(ready_aligned_renderer.render(viewport, canvas, epoch) &&
        ready_aligned.text == ready_two &&
        !ready_aligned_renderer.render(viewport, canvas,
            epoch + std::chrono::milliseconds(2400)) &&
        ready_aligned_renderer.hidden(),
        "Ready with aligned adoption must show the same bounded catalog-neutral notice");

    startup::StartupCacheProgress already_ready;
    already_ready.begin(0);
    already_ready.publishing();
    already_ready.ready(0);
    Capture delayed_capture;
    game::StartupCacheOverlayRenderer delayed(already_ready, native_read,
        delayed_capture.draw_text());
    ok &= require(!delayed.render(nullptr, nullptr, epoch + std::chrono::seconds(20)) &&
        delayed.render(viewport, canvas, epoch + std::chrono::seconds(30)) &&
        delayed_capture.text == std::vector<std::wstring>({
            L"Piano Songs: Ready!", L"No songs found in the Music folder", L""}),
        "loading completed before Canvas readiness must show Ready on the first valid frame");

    int zero_metric_calls = 0;
    game::StartupCacheOverlayRenderer zero_metric(already_ready,
        [&](void* base, uintptr_t offset, void* output, std::size_t size) {
            return native.read(base, offset, output, size);
        },
        [&](void*, float, float, const wchar_t*, void*,
            const game::runtime_layouts::FLinearColor*) {
            ++zero_metric_calls;
            return int32_t{0};
        });
    ok &= require(zero_metric.render(viewport, canvas, epoch) &&
        zero_metric.enabled() && zero_metric_calls == 3 &&
        zero_metric.render(viewport, canvas, epoch + std::chrono::seconds(1)) &&
        zero_metric_calls == 6,
        "zero native text-layout metrics must not disable or short-circuit the overlay");

    int throwing_tile_calls = 0;
    int fallback_text_calls = 0;
    game::StartupCacheOverlayRenderer rectangle_failure(already_ready, native_read,
        [&](void*, float, float, const wchar_t*, void*,
            const game::runtime_layouts::FLinearColor*) {
            ++fallback_text_calls;
            return int32_t{1};
        },
        [&](void*, float, float, float, float, float, float, float, float,
            const game::runtime_layouts::FLinearColor*, const void*, bool) -> bool {
            ++throwing_tile_calls;
            throw std::runtime_error("injected DrawTile failure");
        });
    ok &= require(rectangle_failure.render(viewport, canvas, epoch) &&
        rectangle_failure.enabled() && !rectangle_failure.rectangles_enabled() &&
        throwing_tile_calls == 1 && fallback_text_calls == 3 &&
        rectangle_failure.render(viewport, canvas, epoch + std::chrono::seconds(1)) &&
        throwing_tile_calls == 1 && fallback_text_calls == 6,
        "throwing rectangle calls must permanently degrade to the proven text-only path");

    game::StartupCacheOverlayRenderer read_failure(already_ready,
        [](void*, uintptr_t, void*, std::size_t) -> bool {
            throw std::runtime_error("injected native read failure");
        },
        [](void*, float, float, const wchar_t*, void*,
            const game::runtime_layouts::FLinearColor*) { return int32_t{1}; });
    ok &= require(!read_failure.render(viewport, canvas, epoch) && !read_failure.enabled(),
        "unexpected render failures must disable only the overlay");

    if (!ok) return 1;
    std::cout << "startup_cache_overlay_selftest ok\n";
    return 0;
}
