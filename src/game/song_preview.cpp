#include "game/song_preview.h"

#include "core/logging.h"
#include "game/menu_session_authority.h"
#include "game/profile_list_coordinator.h"
#include "game/song_preview_policy.h"
#include "game/song_registry.h"
#include "pipeline/song_preview.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace ff7r::piano::game {
namespace {
struct PreviewRuntime {
    std::mutex mutex;
    std::condition_variable wake;
    SongPreviewPolicy policy;
    PreviewInputEdge input;
    core::PreviewMode mode = core::PreviewMode::Off;
    bool worker_started = false;
    bool shutdown = false;
    bool driver_failed = false;
    bool render_eligible = false;
    std::uint64_t pending_ticket = 0;
    ULONGLONG fade_deadline = 0;
    std::shared_ptr<const ff7rp::pipeline::SongPreviewArtifact> pending;
    std::vector<std::int16_t> ready;
    std::vector<std::uint8_t> ready_spectrum, spectrum;
    std::atomic_int function_key{7};
};
PreviewRuntime& runtime()
{
    // Attach-only product: worker and any failed driver borrow live to process exit.
    static auto* value = new PreviewRuntime;
    return *value;
}
void event(const char* stage, const char* status, const char* reason = "none") noexcept
{
    try {
        core::log(std::string(status) == "failed" ? core::LogLevel::Error : core::LogLevel::Info,
            std::string("[preview] stage=") + stage + " status=" + status + " reason=" + reason);
    } catch (...) {}
}
// Fade only for in-menu stops; transitions that must claim silence stop outright.
// A fade the driver already buffered past is hard-stopped at the deadline.
bool silence_locked(PreviewRuntime& r, bool fade)
{
    r.ready.clear();
    r.ready_spectrum.clear();
    r.fade_deadline = fade ? GetTickCount64() + 400 : 0;
    return fade ? core::fade_preview_audio() : core::stop_preview_audio();
}
bool stop_locked(PreviewRuntime& r, const char* reason, bool fade = false)
{
    const auto before = r.policy.phase();
    r.render_eligible = false;
    r.policy.cancel();
    r.pending.reset();
    const bool stopped = silence_locked(r, fade);
    if (!stopped && !r.driver_failed) event("stop", "failed", reason);
    if (!stopped) r.driver_failed = true;
    else if (before == PreviewPhase::Playing || before == PreviewPhase::Loading
        || before == PreviewPhase::Ready) event("stop", "stopped", reason);
    return stopped;
}
void worker()
{
    auto& r = runtime();
    for (;;) {
        std::unique_lock lock(r.mutex);
        r.wake.wait(lock, [&] { return r.shutdown || r.pending != nullptr; });
        if (r.shutdown) return;
        auto artifact = std::move(r.pending);
        const auto ticket = r.pending_ticket;
        lock.unlock();
        std::vector<std::int16_t> samples;
        bool loaded = false;
        std::vector<std::uint8_t> spectrum;
        try {
            loaded = ff7rp::pipeline::read_song_preview_pcm(*artifact, &samples).ok();
            if (loaded) spectrum = ff7rp::pipeline::song_preview_spectrum(samples);
        } catch (...) {}
        lock.lock();
        if (r.shutdown) return;
        if (!r.policy.complete(ticket, loaded)) continue;
        if (loaded) {
            r.ready = std::move(samples);
            r.ready_spectrum = std::move(spectrum);
        }
        event("load", loaded ? "ready" : "failed", loaded ? "none" : "artifact_validation");
    }
}
void request_locked(PreviewRuntime& r, const SelectionSnapshot& selection)
{
    r.ready.clear();
    r.pending = selection.song->preview;
    r.pending_ticket = r.policy.ticket();
    event("load", "requested");
    r.wake.notify_one();
}
}

void configure_song_preview(core::PreviewMode mode, int function_key) noexcept
{
    try {
        auto& r = runtime();
        std::lock_guard lock(r.mutex);
        if (r.shutdown) return;
        (void)stop_locked(r, "configure");
        r.mode = mode;
        r.policy.configure(mode);
        r.function_key.store(function_key >= 0 && function_key <= 12 ? function_key : 7);
        if (mode != core::PreviewMode::Off && !r.worker_started) {
            std::thread thread(worker);
            thread.detach();
            r.worker_started = true;
        }
    } catch (...) { event("initialize", "failed", "worker_start"); }
}

int preview_function_key() noexcept { return runtime().function_key.load(); }

bool cancel_song_preview(const char* reason, bool fade) noexcept
{
    auto& r = runtime();
    std::lock_guard lock(r.mutex);
    return stop_locked(r, reason, fade);
}

void poll_song_preview(bool focused, bool manual_held, bool autoplay_chord_held) noexcept
{
    try {
        auto& r = runtime();
        // Cancellation owns this same lock through stream reset. Capture authority
        // inside it so a pre-transition snapshot cannot restart after cancellation.
        std::lock_guard lock(r.mutex);
        const auto session = capture_ready_menu_session();
        const auto selection = registry().selection_snapshot();
        const bool eligible = focused && session && selection && selection.storage
            && selection.song->preview && !registry().playback_snapshot().song
            && profile_list_coordinator().state() == ProfileListCoordinatorState::Ready
            && menu_session_generation_exact(session.generation, true)
            && registry().selection_matches(selection);
        const bool edge = r.input.update(manual_held || autoplay_chord_held,
            eligible && !autoplay_chord_held);
        if (r.shutdown || r.mode == core::PreviewMode::Off || !r.worker_started || r.driver_failed) return;
        if (r.fade_deadline && GetTickCount64() >= r.fade_deadline && !silence_locked(r, false)) {
            r.driver_failed = true;
            event("stop", "failed", "fade_timeout");
            return;
        }
        if (!eligible) {
            if (r.policy.phase() != PreviewPhase::Finished)
                (void)stop_locked(r, focused ? "menu_not_ready" : "focus_loss");
            return;
        }
        const PreviewIdentity identity{session.generation, selection.generation,
            selection.storage.get(), selection.song, selection.visible_index};
        if (identity != r.policy.identity()) {
            const bool same_row = identity.same_row(r.policy.identity());
            const auto prior = r.policy.phase();
            if (!silence_locked(r, true)) {
                r.driver_failed = true;
                event("stop", "failed", "selection_change");
                return;
            }
            r.pending.reset();
            r.policy.select(identity, GetTickCount64());
            if (prior == PreviewPhase::Playing || prior == PreviewPhase::Loading)
                event("stop", "stopped", same_row ? "selection_generation" : "row_change");
        }
        if (edge) {
            if (r.policy.toggle()) request_locked(r, selection);
            else (void)stop_locked(r, "manual_toggle", true);
        } else if (r.policy.dwell(GetTickCount64())) request_locked(r, selection);
        if (r.policy.phase() == PreviewPhase::Ready) {
            r.fade_deadline = 0;
            const bool started = core::play_preview_audio(std::move(r.ready));
            r.policy.started(started);
            r.spectrum = std::move(r.ready_spectrum);
            r.ready_spectrum.clear();
            if (!started) { r.driver_failed = true; (void)core::stop_preview_audio(); }
            event("start", started ? "playing" : "failed", started ? "none" : "audio_driver");
        }
        if (r.policy.phase() == PreviewPhase::Playing) {
            const auto position = core::preview_audio_position();
            if (position.failed) {
                r.driver_failed = true;
                (void)stop_locked(r, "device_position");
                event("position", "failed", "audio_driver");
            } else if (!position.playing) {
                (void)stop_locked(r, "clip_complete");
            }
        }
        r.render_eligible = !r.driver_failed;
    } catch (...) { (void)cancel_song_preview("runtime_exception"); }
}

bool shutdown_song_preview() noexcept
{
    auto& r = runtime();
    std::lock_guard lock(r.mutex);
    r.shutdown = true;
    const bool stopped = stop_locked(r, "teardown");
    r.wake.notify_one();
    return stopped;
}

core::PreviewAudioPosition song_preview_position() noexcept
{
    return core::preview_audio_position();
}

SongPreviewRenderSnapshot capture_song_preview_render_snapshot() noexcept
{
    try {
        auto& r = runtime();
        std::lock_guard lock(r.mutex);
        if (!r.render_eligible || r.shutdown || r.driver_failed
            || r.mode == core::PreviewMode::Off) return {};
        const auto session = capture_ready_menu_session();
        const auto selection = registry().selection_snapshot();
        if (!session || !selection || !selection.storage || !selection.song->preview
            || registry().playback_snapshot().song
            || profile_list_coordinator().state() != ProfileListCoordinatorState::Ready
            || !menu_session_generation_exact(session.generation, true)
            || !registry().selection_matches(selection)) return {};
        const PreviewIdentity identity{session.generation, selection.generation,
            selection.storage.get(), selection.song, selection.visible_index};
        if (identity != r.policy.identity()) return {};
        auto position = r.policy.phase() == PreviewPhase::Playing
            ? core::preview_audio_position() : core::PreviewAudioPosition{};
        if (position.failed) return {};
        SongPreviewRenderSnapshot snapshot{selection, session.generation, r.policy.ticket(),
            r.policy.phase(), r.mode, position};
        constexpr auto bands = ff7rp::pipeline::kSongPreviewSpectrumBands;
        const auto hops = r.spectrum.size() / bands;
        if (position.playing && hops) {
            // Interpolate the 30 Hz analysis so bars move smoothly at render rate.
            const double at = double(position.played_frames) / ff7rp::pipeline::kSongPreviewSpectrumHop;
            const auto k = std::min(static_cast<std::size_t>(at), hops - 1), next = std::min(k + 1, hops - 1);
            const float t = static_cast<float>(at - double(k));
            for (std::size_t b = 0; b < bands; ++b)
                snapshot.spectrum[b] = (r.spectrum[k * bands + b] * (1 - t)
                    + r.spectrum[next * bands + b] * t) / 255.0f;
        }
        return snapshot;
    } catch (...) { return {}; }
}
} // namespace ff7r::piano::game
