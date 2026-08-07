#include "game/completion_timing.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace ff7r::piano::game {

bool chart_time_seconds(const std::string& value, float& seconds)
{
    const size_t separator = value.find('_');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
        return false;
    }

    int whole = 0;
    int frames = 0;
    const auto whole_result = std::from_chars(value.data(), value.data() + separator, whole);
    const auto fraction_result = std::from_chars(value.data() + separator + 1, value.data() + value.size(), frames);
    if (whole_result.ec != std::errc{} || whole_result.ptr != value.data() + separator
        || fraction_result.ec != std::errc{} || fraction_result.ptr != value.data() + value.size()
        || whole < 0 || frames < 0 || frames >= 60) {
        return false;
    }

    seconds = static_cast<float>(whole) + static_cast<float>(frames) / 60.0f;
    return true;
}

float profile_last_prompt_seconds(const SongDifficultyProfile* profile)
{
    float last = -1.0f;
    if (!profile) {
        return last;
    }
    for (const SongChartNote& note : profile->chart_notes) {
        float seconds = 0.0f;
        if (chart_time_seconds(note.time_str, seconds)) {
            last = std::max(last, seconds);
        }
    }
    return last;
}

float completion_target_seconds(const SongDescriptor* song, const SongDifficultyProfile* profile,
    float native_chart_max_seconds)
{
    constexpr float kReleaseMarginSeconds = 0.25f;
    constexpr float kMaximumSupportedSeconds = 24.0f * 60.0f * 60.0f;
    if (!song) {
        return -1.0f;
    }

    float target = -1.0f;
    const auto include = [&](float value) {
        if (std::isfinite(value) && value >= 0.0f && value <= kMaximumSupportedSeconds) {
            target = std::max(target, value);
        }
    };
    include(song->duration_seconds);
    include(profile_last_prompt_seconds(profile));
    include(native_chart_max_seconds);
    return target >= 0.0f ? target + kReleaseMarginSeconds : -1.0f;
}

} // namespace ff7r::piano::game
