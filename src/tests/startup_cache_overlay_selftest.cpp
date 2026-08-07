#include "game/startup_cache_overlay.h"

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
    std::vector<std::wstring> starting_text;
    game::StartupCacheOverlayRenderer starting_renderer(starting_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            starting_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(starting_renderer.render(viewport, canvas, epoch) &&
        starting_text == std::vector<std::wstring>({
            L"FF7RPianoSongs: Preparing piano songs",
            L"Processed 0/0 | Ready 0",
            L"Discovering Music folders"}) && !contains_catalog_ux(starting_text),
        "Starting must use bounded player-facing cache vocabulary");

    startup::StartupCacheProgress zero_progress;
    zero_progress.begin(2);
    std::vector<TileCall> zero_tiles;
    std::vector<int> zero_order;
    std::vector<std::wstring> zero_text;
    game::StartupCacheOverlayRenderer zero_renderer(zero_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            zero_order.push_back(20 + static_cast<int>(zero_text.size()));
            zero_text.emplace_back(text);
            return int32_t{0};
        },
        [&](void* observed_canvas, float x, float y, float width, float height,
            float u, float v, float size_u, float size_v,
            const game::runtime_layouts::FLinearColor* color, const void* texture,
            bool alpha_blend) {
            zero_order.push_back(10 + static_cast<int>(zero_tiles.size()));
            zero_tiles.push_back({observed_canvas, x, y, width, height, u, v,
                size_u, size_v, *color, texture, alpha_blend});
            return true;
        });
    ok &= require(zero_renderer.render(viewport, canvas, epoch) && zero_tiles.size() == 2 &&
        zero_text.size() == 4 && zero_order == std::vector<int>({10, 11, 20, 21, 22, 23}),
        "Loading must submit all four text lines even when native layout metrics are zero");
    ok &= require(close_float(zero_tiles[0].x, 1212.0f) && close_float(zero_tiles[0].y, 16.0f) &&
        close_float(zero_tiles[0].width, 680.0f) && close_float(zero_tiles[0].height, 132.0f) &&
        close_float(zero_tiles[1].x, 1228.0f) && close_float(zero_tiles[1].y, 132.0f) &&
        close_float(zero_tiles[1].width, 648.0f) && close_float(zero_tiles[1].height, 6.0f),
        "panel and progress track geometry must stay bounded and deterministic");
    ok &= require(zero_tiles[0].canvas == fcanvas && zero_tiles[0].texture == nullptr &&
        zero_tiles[1].texture == nullptr && zero_tiles[0].alpha_blend &&
        zero_tiles[1].alpha_blend && close_float(zero_tiles[0].u, 0.0f) &&
        close_float(zero_tiles[0].v, 0.0f) && close_float(zero_tiles[0].size_u, 1.0f) &&
        close_float(zero_tiles[0].size_v, 1.0f) &&
        close_float(zero_tiles[0].color.r, 0.02f) &&
        close_float(zero_tiles[0].color.g, 0.03f) &&
        close_float(zero_tiles[0].color.b, 0.05f) &&
        close_float(zero_tiles[0].color.a, 0.72f) &&
        close_float(zero_tiles[1].color.r, 0.15f) &&
        close_float(zero_tiles[1].color.g, 0.18f) &&
        close_float(zero_tiles[1].color.b, 0.24f) &&
        close_float(zero_tiles[1].color.a, 0.85f),
        "rectangle calls must use native white texture UVs and proven alpha semantics");
    ok &= require(zero_text[0] == L"FF7RPianoSongs: Checking song caches" &&
            zero_text[1] == L"Processed 0/2 | Ready 0" &&
            zero_text[2] == L"Validating existing caches - 0%" && zero_text[3].empty() &&
            !contains_catalog_ux(zero_text)
            && zero_text[1].size() < game::kStartupCacheOverlayTextCapacity,
        "startup without a menu must show only bounded cache initialization facts");

    startup::StartupCacheProgress partial_progress;
    partial_progress.begin(2);
    partial_progress.observe({0, ff7rp::pipeline::SongLoadProgressStage::DecodingAudio});
    partial_progress.available_on_reopen(1);
    std::vector<TileCall> partial_tiles;
    std::vector<std::wstring> partial_text;
    game::StartupCacheOverlayRenderer partial_renderer(partial_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            partial_text.emplace_back(text);
            return int32_t{1};
        },
        [&](void* observed_canvas, float x, float y, float width, float height,
            float u, float v, float size_u, float size_v,
            const game::runtime_layouts::FLinearColor* color, const void* texture,
            bool alpha_blend) {
            partial_tiles.push_back({observed_canvas, x, y, width, height, u, v,
                size_u, size_v, *color, texture, alpha_blend});
            return true;
        });
    ok &= require(partial_renderer.render(viewport, canvas, epoch) &&
        partial_tiles.size() == 3 && partial_tiles[2].width > 0.0f &&
        partial_tiles[2].width < partial_tiles[1].width &&
        partial_tiles[2].texture == nullptr && partial_tiles[2].alpha_blend &&
        close_float(partial_tiles[2].color.r, 0.20f) &&
        close_float(partial_tiles[2].color.g, 0.70f) &&
        close_float(partial_tiles[2].color.b, 1.0f) &&
        close_float(partial_tiles[2].color.a, 0.95f) && partial_text.size() == 4 &&
        partial_text[0] == L"FF7RPianoSongs: Rebuilding song cache" &&
        partial_text[1] == L"Processed 0/2 | Ready 1" &&
        partial_text[2].find(L"Decoding song audio") != std::wstring::npos &&
        partial_text[3] == L"Per song: calculating... | Total: calculating..." &&
        !contains_catalog_ux(partial_text),
        "partial startup progress must distinguish rebuilding without catalog guidance");

    startup::StartupCacheProgress between_song_progress;
    between_song_progress.begin(2);
    between_song_progress.catalog_prepared(1);
    between_song_progress.catalog_adopted(1);
    between_song_progress.observe({0, ff7rp::pipeline::SongLoadProgressStage::Complete,
        ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded});
    std::vector<std::wstring> between_song_text;
    game::StartupCacheOverlayRenderer between_song_renderer(between_song_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            between_song_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(between_song_renderer.render(viewport, canvas, epoch) &&
        between_song_text.size() == 4 &&
        between_song_text[0] == L"FF7RPianoSongs: Checking song caches" &&
        between_song_text[2].find(L"Validating existing caches") != std::wstring::npos &&
        !contains_catalog_ux(between_song_text),
        "a gap between candidates must be presented as ordinary cache checking");

    startup::StartupCacheProgress background_lifecycle_progress;
    background_lifecycle_progress.begin(3);
    background_lifecycle_progress.catalog_prepared(1);
    background_lifecycle_progress.catalog_adopted(1);
    background_lifecycle_progress.observe(
        {0, ff7rp::pipeline::SongLoadProgressStage::GeneratingChart});
    background_lifecycle_progress.catalog_prepared(2);
    background_lifecycle_progress.catalog_adoption_deferred();
    std::vector<std::wstring> background_lifecycle_text;
    game::StartupCacheOverlayRenderer background_lifecycle_renderer(
        background_lifecycle_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            background_lifecycle_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(background_lifecycle_renderer.render(viewport, canvas, epoch) &&
        background_lifecycle_text.size() == 4 &&
        background_lifecycle_text[0] == L"FF7RPianoSongs: Rebuilding song cache" &&
        background_lifecycle_text[1] == L"Processed 0/3 | Ready 2" &&
        background_lifecycle_text[3] ==
            L"Per song: calculating... | Total: calculating..." &&
        !contains_catalog_ux(background_lifecycle_text),
        "internal catalog lifecycle must not affect startup overlay vocabulary");

    startup::StartupCacheProgress full_progress;
    full_progress.begin(2);
    full_progress.publishing();
    std::vector<TileCall> full_tiles;
    std::vector<std::wstring> publishing_text;
    game::StartupCacheOverlayRenderer full_renderer(full_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            publishing_text.emplace_back(text);
            return int32_t{1};
        },
        [&](void* observed_canvas, float x, float y, float width, float height,
            float u, float v, float size_u, float size_v,
            const game::runtime_layouts::FLinearColor* color, const void* texture,
            bool alpha_blend) {
            full_tiles.push_back({observed_canvas, x, y, width, height, u, v,
                size_u, size_v, *color, texture, alpha_blend});
            return true;
        });
    ok &= require(full_renderer.render(viewport, canvas, epoch) && full_tiles.size() == 3 &&
        close_float(full_tiles[2].width, full_tiles[1].width) &&
        publishing_text == std::vector<std::wstring>({
            L"FF7RPianoSongs: Preparing piano songs",
            L"Processed 2/2 | Ready 0",
            L"Finalizing song cache"}) && !contains_catalog_ux(publishing_text),
        "Publishing and Ready progress must render a clamped 100 percent fill");

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
    std::vector<std::wstring> warm_validation_text;
    game::StartupCacheOverlayRenderer warm_validation_renderer(queue_eta, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            warm_validation_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(queue_eta.snapshot().estimates_available &&
        warm_validation_renderer.render(viewport, canvas, epoch) &&
        warm_validation_text.size() == 4 &&
        warm_validation_text[0] == L"FF7RPianoSongs: Checking song caches" &&
        warm_validation_text[2].find(L"Validating existing caches") != std::wstring::npos &&
        warm_validation_text[3].empty() && !contains_catalog_ux(warm_validation_text),
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
    std::vector<std::wstring> eta_text;
    game::StartupCacheOverlayRenderer eta_renderer(eta_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            eta_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(eta_renderer.render(viewport, canvas, eta_now) &&
        !eta_renderer.hidden() && eta_text.size() == 4 &&
        eta_text[0] == L"FF7RPianoSongs: Rebuilding song cache" &&
        eta_text[2].find(L"Building game audio cache") != std::wstring::npos &&
        eta_text[3] == L"Per song: approx. 55 sec | Total: approx. 55 sec" &&
        !contains_catalog_ux(eta_text),
        "Loading must retain bounded rebuild estimates without catalog-internal state");

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
    std::vector<std::wstring> failed_text;
    game::StartupCacheOverlayRenderer failed_renderer(failed_progress, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            failed_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(failed_renderer.render(viewport, canvas, epoch) &&
        failed_text == std::vector<std::wstring>({
            L"FF7RPianoSongs: Song cache setup failed",
            L"Processed 0/2 | Ready 0",
            L"See FF7RPianoSongs.log for details"}) && !contains_catalog_ux(failed_text),
        "Failed must use bounded player-facing cache vocabulary");

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
    ok &= require(renderer.render(viewport, canvas, epoch) && drawn.size() == 4,
        "a valid Loading frame must draw exactly four bounded text lines");

    progress.publishing();
    progress.ready(2);
    progress.catalog_adoption_deferred();
    drawn.clear();
    ok &= require(renderer.render(viewport, canvas, epoch + std::chrono::seconds(5)) &&
        drawn.size() == 3 && drawn[0] == L"FF7RPianoSongs: Piano songs ready" &&
        drawn[1].empty() && drawn[2].empty() && !contains_catalog_ux(drawn) &&
        !renderer.hidden(),
        "repository settlement before any menu must show a bounded completion notice only");
    drawn.clear();
    ok &= require(renderer.render(viewport, canvas,
            epoch + std::chrono::seconds(6) + std::chrono::milliseconds(999)) &&
        drawn.size() == 3 && !renderer.hidden(),
        "completion notice must remain visible until its cosmetic deadline");
    drawn.clear();
    ok &= require(!renderer.render(viewport, canvas, epoch + std::chrono::seconds(7)) &&
        renderer.hidden() && drawn.empty(),
        "completion notice must hide even when a catalog refresh remains pending");
    progress.catalog_adopted(2);
    const auto aligned = progress.snapshot();
    ok &= require(aligned.active_song_count == 2 && !aligned.catalog_update_pending,
        "successful adoption must align active and prepared counts");

    startup::StartupCacheProgress ready_pending;
    ready_pending.begin(2);
    ready_pending.catalog_prepared(1);
    ready_pending.catalog_adopted(1);
    ready_pending.catalog_prepared(2);
    ready_pending.publishing();
    ready_pending.ready(2);
    std::vector<std::wstring> ready_pending_text;
    game::StartupCacheOverlayRenderer ready_pending_renderer(ready_pending, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            ready_pending_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(ready_pending_renderer.render(viewport, canvas, epoch) &&
        ready_pending_text.size() == 3 &&
        ready_pending_text[0] == L"FF7RPianoSongs: Piano songs ready" &&
        ready_pending_text[1].empty() && ready_pending_text[2].empty() &&
        !contains_catalog_ux(ready_pending_text) &&
        !ready_pending_renderer.render(viewport, canvas, epoch + std::chrono::seconds(2)) &&
        ready_pending_renderer.hidden(),
        "Ready with pending adoption must show one bounded catalog-neutral notice");

    ready_pending.catalog_adopted(2);
    std::vector<std::wstring> ready_aligned_text;
    game::StartupCacheOverlayRenderer ready_aligned_renderer(ready_pending, native_read,
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            ready_aligned_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(ready_aligned_renderer.render(viewport, canvas, epoch) &&
        ready_aligned_text.size() == 3 &&
        ready_aligned_text[0] == L"FF7RPianoSongs: Piano songs ready" &&
        ready_aligned_text[1].empty() && ready_aligned_text[2].empty() &&
        !contains_catalog_ux(ready_aligned_text) &&
        !ready_aligned_renderer.render(viewport, canvas, epoch + std::chrono::seconds(2)) &&
        ready_aligned_renderer.hidden(),
        "Ready with aligned adoption must show the same bounded catalog-neutral notice");

    startup::StartupCacheProgress already_ready;
    already_ready.begin(0);
    already_ready.publishing();
    already_ready.ready(0);
    std::vector<std::wstring> delayed_text;
    game::StartupCacheOverlayRenderer delayed(already_ready,
        [&](void* base, uintptr_t offset, void* output, std::size_t size) {
            return native.read(base, offset, output, size);
        },
        [&](void*, float, float, const wchar_t* text, void*,
            const game::runtime_layouts::FLinearColor*) {
            delayed_text.emplace_back(text);
            return int32_t{1};
        });
    ok &= require(!delayed.render(nullptr, nullptr, epoch + std::chrono::seconds(20)) &&
        delayed.render(viewport, canvas, epoch + std::chrono::seconds(30)) &&
        delayed_text.size() == 3 &&
        delayed_text[0] == L"FF7RPianoSongs: Piano songs ready" &&
        !contains_catalog_ux(delayed_text),
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
