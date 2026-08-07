#include "game/duration.h"

#include "core/hooks.h"
#include "core/pe_image.h"
#include "game/completion_timing.h"
#include "game/module_hooks.h"
#include "game/hook_specs.h"
#include "game/note_count.h"
#include "game/runtime_layouts.h"

#include "core/logging.h"

#include <intrin.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <mutex>

namespace ff7r::piano::game {
namespace {

bool valid_duration(float seconds)
{
    return seconds > 0.0f && seconds <= 600.0f;
}

using PianoDurationSecondsFn = float(__fastcall*)(void* duration_object, uint64_t key);
using PianoScoreModeSetupFn = void(__fastcall*)(void* owner);
using PianoCompletionTimingInitFn = void(__fastcall*)(void* owner);

core::RawRvaHook g_duration_hook;
core::RawRvaHook g_score_mode_setup_hook;
core::RawRvaHook g_completion_timing_init_hook;
PianoDurationSecondsFn g_original_duration_seconds = nullptr;
PianoScoreModeSetupFn g_original_score_mode_setup = nullptr;
PianoCompletionTimingInitFn g_original_completion_timing_init = nullptr;
uintptr_t g_module_base = 0;
std::mutex g_completion_owner_mutex;
RetainedChartOwnerObservation g_completion_owner_observation;

void __fastcall completion_timing_init_detour(void* owner)
{
    auto callback = non_audio_hook_gate().try_enter();
    void* caller = _ReturnAddress();
    float before = 0.0f;
    void* chart_before = nullptr;
    const bool before_valid = owner && core::safe_read_field(
        owner, runtime_layouts::PianoCompletionOwner::duration_seconds, before);
    const bool chart_before_valid = owner && core::safe_read_field(
        owner, runtime_layouts::PianoCompletionOwner::chart, chart_before);

    if (g_original_completion_timing_init) {
        g_original_completion_timing_init(owner);
    }
    if (!callback) return;

    float after = 0.0f;
    void* chart_after = nullptr;
    const bool after_valid = owner && core::safe_read_field(
        owner, runtime_layouts::PianoCompletionOwner::duration_seconds, after);
    const bool chart_after_valid = owner && core::safe_read_field(
        owner, runtime_layouts::PianoCompletionOwner::chart, chart_after);
    if (owner && after_valid && chart_after_valid) {
        UObjectLiveHandle identity{};
        if (owner) {
            const bool identity_valid = capture_live_uobject_handle(owner, identity);
            const PlaybackSnapshot snapshot = registry().playback_snapshot();
            const void* initial_chart = chart_before_valid && chart_before == chart_after
                ? nullptr : chart_after;
            std::lock_guard<std::mutex> lock(g_completion_owner_mutex);
            RetainedChartOwnerObservation next;
            next.owner = owner;
            next.chart = const_cast<void*>(initial_chart);
            next.generation = g_completion_owner_observation.generation + 1;
            next.registry_generation = snapshot.generation;
            next.owner_identity = identity;
            next.owner_identity_valid = identity_valid;
            next.song_id = snapshot.song ? snapshot.song->id : std::string{};
            next.profile_index = snapshot.profile_index;
            next.difficulty = snapshot.profile ? snapshot.profile->difficulty : 0;
            next.owner_observed = true;
            next.chart_read_succeeded = initial_chart != nullptr;
            g_completion_owner_observation = std::move(next);
        }
    }
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const SongDescriptor* song = playback.song;
    const SongDifficultyProfile* profile = playback.profile;

    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) < 32) {
        const auto caller_address = reinterpret_cast<uintptr_t>(caller);
        const uintptr_t caller_rva = g_module_base && caller_address >= g_module_base
            ? caller_address - g_module_base
            : 0;
        std::ostringstream out;
        out << "[duration] completion_timing_init status=observed"
            << " owner=0x" << std::hex << reinterpret_cast<uintptr_t>(owner)
            << " caller_rva=0x" << caller_rva
            << " chart_before=0x" << reinterpret_cast<uintptr_t>(chart_before)
            << " chart_after=0x" << reinterpret_cast<uintptr_t>(chart_after)
            << std::dec
            << " before_valid=" << before_valid
            << " before=" << before
            << " after_valid=" << after_valid
            << " after=" << after
            << " chart_before_valid=" << chart_before_valid
            << " chart_after_valid=" << chart_after_valid
            << " active_song=" << (song ? song->id : "<none>")
            << " difficulty=" << (profile ? profile->difficulty : 0);
        core::log(core::LogLevel::Info, out.str());
    }
}

RetainedChartOwnerObservation retained_chart_owner_observation_impl()
{
    std::lock_guard<std::mutex> lock(g_completion_owner_mutex);
    RetainedChartOwnerObservation observation = g_completion_owner_observation;
    void* current_chart = nullptr;
    observation.owner_observed = observation.owner
        && (!observation.owner_identity_valid
            || validate_live_uobject_handle(observation.owner, observation.owner_identity));
    const PlaybackSnapshot snapshot = registry().playback_snapshot();
    const bool song_identity_matches = snapshot.song && snapshot.profile
        && observation.song_id == snapshot.song->id
        && observation.profile_index == snapshot.profile_index
        && observation.difficulty == snapshot.profile->difficulty;
    const bool chart_read = observation.owner_observed
        && song_identity_matches
        && core::safe_read_field(
            observation.owner, runtime_layouts::PianoCompletionOwner::chart, current_chart);
    if (!chart_read || !bind_retained_chart_owner_observation(
            observation, current_chart, snapshot.generation)) {
        observation.chart = nullptr;
        return observation;
    }
    if (!g_completion_owner_observation.chart) {
        g_completion_owner_observation.chart = current_chart;
        g_completion_owner_observation.registry_generation = snapshot.generation;
        g_completion_owner_observation.chart_read_succeeded = true;
    }
    return observation;
}

void __fastcall score_mode_setup_detour(void* owner)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (g_original_score_mode_setup) {
        g_original_score_mode_setup(owner);
    }
    if (!callback) return;
    log_completion_owner_memory(-1.0f);
    log_active_chart_memory(-1.0f);

    float native_after_blank = 0.0f;
    const SelectionSnapshot selection = registry().selection_snapshot();
    const SongDescriptor* song = selection.song;
    const SongDifficultyProfile* profile = selection.profile;
    if (!owner || !song || !profile
        || !core::safe_copy_bytes(reinterpret_cast<const uint8_t*>(owner)
                + runtime_layouts::PianoCompletionOwner::duration_seconds,
            &native_after_blank, sizeof(native_after_blank))) {
        return;
    }

    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) < 32) {
        std::ostringstream out;
        out << "[duration] completion_after_blank source=score_mode_setup status=observed_late"
            << " song_id=" << song->id
            << " difficulty=" << profile->difficulty
            << " last_prompt=" << profile_last_prompt_seconds(profile)
            << " audio_duration=" << song->duration_seconds
            << " native=" << native_after_blank;
        core::log(core::LogLevel::Info, out.str());
    }
}

float __fastcall duration_seconds_detour(void* duration_object, uint64_t key)
{
    auto callback = non_audio_hook_gate().try_enter();
    const float original = g_original_duration_seconds ? g_original_duration_seconds(duration_object, key) : -1.0f;
    if (!callback) return original;
    const RenderSnapshot menu = registry().render_snapshot();
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const SongDescriptor* song = menu.song ? menu.song : playback.song;
    const float replacement = menu_or_playback_duration_or_original(
        menu, playback, original);

    static std::atomic_int s_logs{0};
    const int log_index = s_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 64) {
        std::ostringstream out;
        out << "[duration] status=" << (song && replacement != original ? "overridden" : "observed")
            << " duration_object=0x" << std::hex << reinterpret_cast<uintptr_t>(duration_object)
            << " key=0x" << key
            << std::dec
            << " original=" << original
            << " replacement=" << replacement
            << " source=" << (menu.song ? "menu" : "playback")
            << " active_song=" << (song ? song->id : "<none>");
        core::log(core::LogLevel::Info, out.str());
    }

    return replacement;
}

} // namespace

RetainedChartOwnerObservation retained_chart_owner_observation()
{
    return retained_chart_owner_observation_impl();
}

bool has_descriptor_duration_override(const SongDescriptor* song)
{
    return song && valid_duration(song->duration_seconds);
}

float descriptor_duration_or_original(const SongDescriptor* song, float original_seconds)
{
    return has_descriptor_duration_override(song) ? song->duration_seconds : original_seconds;
}

void log_completion_owner_memory(float playback_seconds)
{
    constexpr std::size_t kSnapshotOffset = 0xe80;
    constexpr std::size_t kSnapshotSize = 0x1c0;
    std::array<uint32_t, kSnapshotSize / sizeof(uint32_t)> words{};
    const RetainedChartOwnerObservation observation = retained_chart_owner_observation_impl();
    void* const owner = observation.owner_observed ? observation.owner : nullptr;
    if (!owner || !core::safe_copy_bytes(reinterpret_cast<const uint8_t*>(owner) + kSnapshotOffset,
            words.data(), kSnapshotSize)) {
        return;
    }

    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 64) {
        return;
    }
    std::ostringstream out;
    out << "[completion_mem] owner=0x" << std::hex << reinterpret_cast<uintptr_t>(owner)
        << std::dec << " playback_seconds=" << playback_seconds
        << " offset=0x" << std::hex << kSnapshotOffset << " words=";
    for (const uint32_t word : words) {
        out << std::setw(8) << std::setfill('0') << word;
    }
    core::log(core::LogLevel::Info, out.str());
}

bool install_duration_hooks(const HookInstallContext& context)
{
    g_module_base = reinterpret_cast<uintptr_t>(context.exe_module);
    const HookSpec* spec = find_hook_spec("duration_seconds");
    if (!spec) {
        core::log(core::LogLevel::Error, "[duration] status=install_failed error=missing_hook_spec");
        return false;
    }

    std::string error;
    const bool ok = g_duration_hook.install(context.exe_module, spec->rva, spec->expected_prologue,
        reinterpret_cast<void*>(&duration_seconds_detour), reinterpret_cast<void**>(&g_original_duration_seconds), error);

    const HookSpec* completion_spec = find_hook_spec("piano_score_mode_setup");
    std::string completion_error;
    const bool completion_ok = completion_spec
        && g_score_mode_setup_hook.install(context.exe_module, completion_spec->rva, completion_spec->expected_prologue,
            reinterpret_cast<void*>(&score_mode_setup_detour), reinterpret_cast<void**>(&g_original_score_mode_setup),
            completion_error);

    const HookSpec* init_spec = find_hook_spec("piano_completion_timing_init");
    std::string init_error;
    const bool init_ok = init_spec
        && g_completion_timing_init_hook.install(context.exe_module, init_spec->rva, init_spec->expected_prologue,
            reinterpret_cast<void*>(&completion_timing_init_detour),
            reinterpret_cast<void**>(&g_original_completion_timing_init), init_error);

    std::ostringstream out;
    out << "[duration] status=" << (ok ? "live_hook_installed" : "install_failed")
        << " seam=descriptor_duration_seconds"
        << (ok ? "" : " error=") << (ok ? "" : error)
        << " custom_songs=" << registry().custom_count();
    core::log(ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    core::log(completion_ok ? core::LogLevel::Info : core::LogLevel::Error,
        completion_ok ? "[duration] completion_after_blank status=hook_installed"
                      : "[duration] completion_after_blank status=hook_disabled error=" + completion_error);
    core::log(init_ok ? core::LogLevel::Info : core::LogLevel::Error,
        init_ok ? "[duration] completion_timing_init status=diagnostic_hook_installed"
                : "[duration] completion_timing_init status=hook_disabled error=" + init_error);
    return ok;
}

core::HookShutdownResult shutdown_duration()
{
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_completion_timing_init_hook),
        core::teardown_operation(g_duration_hook),
        core::teardown_operation(g_score_mode_setup_hook),
    }, {}, [] {
        g_original_completion_timing_init = nullptr;
        g_original_duration_seconds = nullptr;
        g_original_score_mode_setup = nullptr;
        g_module_base = 0;
        std::lock_guard<std::mutex> lock(g_completion_owner_mutex);
        g_completion_owner_observation = {};
    });
}

} // namespace ff7r::piano::game
