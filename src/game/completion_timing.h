#pragma once

#include "game/song_registry.h"

namespace ff7r::piano::game {

bool chart_time_seconds(const std::string& value, float& seconds);
float profile_last_prompt_seconds(const SongDifficultyProfile* profile);
float completion_target_seconds(const SongDescriptor* song, const SongDifficultyProfile* profile,
    float native_chart_max_seconds);

} // namespace ff7r::piano::game
