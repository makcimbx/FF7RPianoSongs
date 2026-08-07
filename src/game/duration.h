#pragma once

#include "game/song_registry.h"
#include "game/uobject_lifetime.h"

namespace ff7r::piano::game {

struct RetainedChartOwnerObservation {
    void* owner = nullptr;
    void* chart = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t registry_generation = 0;
    UObjectLiveHandle owner_identity{};
    bool owner_identity_valid = false;
    std::string song_id;
    int profile_index = -1;
    int difficulty = 0;
    bool owner_observed = false;
    bool chart_read_succeeded = false;
};

inline bool bind_retained_chart_owner_observation(
    RetainedChartOwnerObservation& observation, void* current_chart,
    std::uint64_t current_registry_generation) noexcept
{
    if (!observation.owner_observed || !current_chart
        || (observation.chart && observation.chart != current_chart)) {
        observation.chart_read_succeeded = false;
        return false;
    }
    if (!observation.chart) {
        observation.chart = current_chart;
        observation.registry_generation = current_registry_generation;
    }
    observation.chart_read_succeeded = true;
    return true;
}

bool has_descriptor_duration_override(const SongDescriptor* song);
float descriptor_duration_or_original(const SongDescriptor* song, float original_seconds);
inline float playback_duration_or_original(
    const PlaybackSnapshot& playback, float original_seconds) noexcept
{
    return playback.song && playback.song->duration_seconds > 0.0f
            && playback.song->duration_seconds <= 600.0f
        ? playback.song->duration_seconds : original_seconds;
}
inline float menu_or_playback_duration_or_original(
    const RenderSnapshot& menu, const PlaybackSnapshot& playback,
    float original_seconds) noexcept
{
    if (menu.song) {
        return menu.song->duration_seconds > 0.0f
                && menu.song->duration_seconds <= 600.0f
            ? menu.song->duration_seconds : original_seconds;
    }
    return playback_duration_or_original(playback, original_seconds);
}
void log_completion_owner_memory(float playback_seconds);
RetainedChartOwnerObservation retained_chart_owner_observation();

} // namespace ff7r::piano::game
