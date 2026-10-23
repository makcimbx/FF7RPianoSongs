#include "game/song_preview.h"
#include "game/song_registry.h"
#include "pipeline/song_preview.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
std::atomic_uint64_t tick{0};
std::atomic_bool menu_ready{true}, list_ready{true}, playing{false}, fail_stop{false}, faded{false};
std::atomic_uint plays{0};
std::atomic_int last_clip{0};
std::atomic<float> settings_gain{0.5f}, played_gain{-1};
std::atomic_bool settings_ready{true}, change_session_on_sample{false};
std::atomic_uint64_t session_generation{1};
std::mutex loader_mutex;
std::condition_variable loader_event;
unsigned loads = 0, permits = 0, publications = 0;
ULONGLONG WINAPI test_tick() { return tick.load(); }
void require(bool value) { if (!value) throw std::runtime_error("preview runtime invariant"); }
template<class Predicate> void await(Predicate predicate)
{
    std::unique_lock lock(loader_mutex);
    require(loader_event.wait_for(lock, std::chrono::seconds(5), predicate));
}
void release_load(unsigned count)
{
    std::lock_guard lock(loader_mutex); permits = count; loader_event.notify_all();
}
}
#define GetTickCount64 test_tick
#include "game/song_preview.cpp"
#undef GetTickCount64

namespace ff7rp::pipeline {
float native_music_fixed_gain() noexcept { return 0.8f * 0.535f; }
Status read_song_preview_pcm(const SongPreviewArtifact& artifact, std::vector<std::int16_t>* samples)
{
    std::unique_lock lock(loader_mutex);
    const auto sequence = ++loads; loader_event.notify_all();
    loader_event.wait(lock, [&] { return permits >= sequence; });
    samples->assign(480, static_cast<std::int16_t>(artifact.content_hash));
    return Status::ok_status();
}
std::vector<std::uint8_t> song_preview_spectrum(std::span<const std::int16_t>)
{
    return std::vector<std::uint8_t>(kSongPreviewSpectrumBands * 2, 255);
}
}
namespace ff7r::piano::core {
void log(LogLevel, const std::string& line)
{
    if (line.find("stage=load status=ready") != std::string::npos) {
        std::lock_guard lock(loader_mutex); ++publications; loader_event.notify_all();
    }
}
bool play_preview_audio(std::vector<std::int16_t>&& samples, float gain) noexcept
{
    ++plays; last_clip = samples.front(); played_gain = gain; playing = true; return true;
}
bool stop_preview_audio() noexcept
{
    if (fail_stop) return false;
    playing = false; faded = false; return true;
}
bool fade_preview_audio() noexcept
{
    if (fail_stop) return false;
    playing = false; faded = true; return true;
}
PreviewAudioPosition preview_audio_position() noexcept { return {playing.load(), false, 5, 240}; }
}
namespace ff7r::piano::game {
PreviewGameVolume capture_preview_game_volume() noexcept
{
    if (change_session_on_sample.exchange(false)) ++session_generation;
    return settings_ready ? PreviewGameVolume{PreviewGameVolumeState::Ready, settings_gain.load()}
        : PreviewGameVolume{PreviewGameVolumeState::NotReady};
}
MenuSessionSnapshot capture_ready_menu_session() noexcept
{
    MenuSessionSnapshot result;
    if (menu_ready) { result.generation = session_generation; result.phase = MenuSessionPhase::Ready; }
    return result;
}
bool menu_session_generation_exact(std::uint64_t generation, bool) noexcept
{ return menu_ready && generation == session_generation; }
ProfileListCoordinator& profile_list_coordinator() { static ProfileListCoordinator value; return value; }
ProfileListCoordinatorState ProfileListCoordinator::state() const noexcept
{ return list_ready ? ProfileListCoordinatorState::Ready : ProfileListCoordinatorState::ListWaitingReadiness; }
}

int main()
{
    using namespace ff7r::piano;
    std::thread loader;
    try {
        std::vector<game::SongDescriptor> songs;
        for (int i = 1; i <= 3; ++i) {
            game::SongDescriptor song;
            song.id = std::to_string(i); song.visible_index = 4 + i;
            song.profiles.emplace_back();
            auto artifact = std::make_shared<ff7rp::pipeline::SongPreviewArtifact>();
            artifact->content_hash = i; song.preview = artifact;
            songs.push_back(std::move(song));
        }
        game::registry().replace(std::move(songs));
        game::registry().set_active_selection(5, 0);
        // Own/join the production worker in this harness instead of detaching it.
        game::runtime().worker_started = true;
        game::configure_song_preview(core::PreviewMode::Auto, 7);
        loader = std::thread(game::worker);
        game::poll_song_preview(true, false, false);
        const auto first_render = game::capture_song_preview_render_snapshot();
        require(bool(first_render) && first_render.selection.storage
            && first_render.selection.song->id == "1" && first_render.phase == game::PreviewPhase::Dwell);
        tick = 899; game::poll_song_preview(true, false, false); require(!loads && !plays);
        tick = 900; game::poll_song_preview(true, false, false);
        await([] { return loads == 1; });
        require(game::cancel_song_preview("immediate_confirm") && !playing);
        require(!game::capture_song_preview_render_snapshot());
        // Navigation while the old read is blocked: only the current clip may publish.
        game::registry().set_active_selection(6, 0); tick = 901;
        require(!game::capture_song_preview_render_snapshot());
        require(first_render.selection.song->id == "1"); // Old snapshot retains ownership, never render authority.
        game::poll_song_preview(true, false, false); tick = 1801;
        game::poll_song_preview(true, false, false); release_load(1);
        await([] { return loads == 2; });
        game::poll_song_preview(true, false, false); require(!plays);
        release_load(2); await([] { return publications == 1; });
        game::poll_song_preview(true, false, false); require(plays == 1 && last_clip == 2);
        require(played_gain == 0.5f * ff7rp::pipeline::native_music_fixed_gain());
        require(game::song_preview_position().played_frames == 5);
        const auto active_render = game::capture_song_preview_render_snapshot();
        require(bool(active_render) && active_render.selection.visible_index == 6
            && active_render.position.played_frames == 5 && active_render.position.playing
            && active_render.spectrum[0] == 1.0f);
        menu_ready = false; require(!game::capture_song_preview_render_snapshot()); menu_ready = true;
        require(game::cancel_song_preview("delayed_close") && !playing && !faded);
        tick = 99999; game::poll_song_preview(true, false, false); require(plays == 1);
        // A fresh manual edge can replay, but holding through focus loss cannot.
        game::poll_song_preview(true, true, false); await([] { return loads == 3; });
        game::poll_song_preview(false, true, false); release_load(3);
        require(!game::capture_song_preview_render_snapshot());
        game::poll_song_preview(true, true, false); require(!playing && plays == 1);
        game::poll_song_preview(true, false, false);
        game::configure_song_preview(core::PreviewMode::Manual, 0);
        game::registry().set_active_selection(7, 0); game::poll_song_preview(true, false, false);
        tick = 999999; game::poll_song_preview(true, false, false); require(plays == 1);
        game::poll_song_preview(true, true, false); release_load(4);
        await([] { return publications == 2; });
        settings_gain = 0;
        game::poll_song_preview(true, true, false); require(plays == 2 && last_clip == 3);
        require(played_gain == 0); // Config zero is a valid mute, not unavailable.
        game::poll_song_preview(true, false, false); game::poll_song_preview(true, true, false);
        require(!playing && faded); // Manual toggle fades out, no replay loop.
        tick += 399; game::poll_song_preview(true, false, false); require(faded);
        tick += 1; game::poll_song_preview(true, false, false);
        require(!faded && plays == 2); // Unfinished fade is hard-stopped, never replayed.
        game::configure_song_preview(core::PreviewMode::Off, 7);
        require(!game::capture_song_preview_render_snapshot());
        game::registry().set_active_selection(5, 0);
        game::poll_song_preview(true, false, false); game::poll_song_preview(true, true, false);
        require(plays == 2 && !playing);
        game::configure_song_preview(core::PreviewMode::Manual, 7);
        game::poll_song_preview(true, false, false);
        game::poll_song_preview(true, false, true); // Chord consumes the R3 hold.
        game::poll_song_preview(true, true, false); require(plays == 2);
        game::poll_song_preview(true, false, false);
        list_ready = false; game::poll_song_preview(true, true, false);
        list_ready = true; game::poll_song_preview(true, true, false); require(plays == 2);
        game::poll_song_preview(true, false, false); game::poll_song_preview(true, true, false);
        release_load(5); await([] { return publications == 3; });
        require(game::cancel_song_preview("ready_confirmation"));
        game::poll_song_preview(true, true, false); require(plays == 2 && !playing);
        game::poll_song_preview(true, false, false); game::poll_song_preview(true, true, false);
        release_load(6); await([] { return publications == 4; });
        game::poll_song_preview(true, true, false); require(playing && plays == 3);
        require(game::cancel_song_preview("settings_test"));
        game::poll_song_preview(true, false, false); game::poll_song_preview(true, true, false);
        release_load(7); await([] { return publications == 5; });
        settings_ready = false; game::poll_song_preview(true, true, false);
        require(!playing && plays == 3 && !game::runtime().driver_failed);
        settings_ready = true;
        game::poll_song_preview(true, false, false); game::poll_song_preview(true, true, false);
        release_load(8); await([] { return publications == 6; });
        change_session_on_sample = true; game::poll_song_preview(true, true, false);
        require(!playing && plays == 3 && !game::runtime().driver_failed);
        game::poll_song_preview(true, false, false); game::poll_song_preview(true, true, false);
        release_load(9); await([] { return publications == 7; });
        settings_gain = 0.25f; game::poll_song_preview(true, true, false);
        require(playing && plays == 4
            && played_gain == 0.25f * ff7rp::pipeline::native_music_fixed_gain());
        fail_stop = true;
        require(!game::cancel_song_preview("driver_cancel_failure") && playing);
        game::registry().set_active_selection(6, 0);
        game::poll_song_preview(true, false, false); game::poll_song_preview(true, true, false);
        require(plays == 4); // Failure retains ownership and permanently denies new starts.
        fail_stop = false; require(game::cancel_song_preview("test_cleanup"));
        game::shutdown_song_preview(); loader.join();
        std::cout << "song preview runtime selftest passed\n";
        return 0;
    } catch (const std::exception& e) {
        release_load(999); fail_stop = false; game::shutdown_song_preview();
        if (loader.joinable()) loader.join();
        std::cerr << e.what() << '\n'; return 1;
    }
}
