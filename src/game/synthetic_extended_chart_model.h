#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// Offline synthetic data model only. It does not model native ABI, allocation, or ownership.
namespace ff7r::piano::game::synthetic_model {

inline constexpr std::size_t kNoEvent = std::numeric_limits<std::size_t>::max();
inline constexpr std::size_t kNativeMaximumRows = 512u;
inline constexpr std::size_t kExperimentalMaximumRows = 1024u;

struct SourceRow {
    double time = 0.0;
    uint32_t group = 0;
    int camera_transition = 0;
    bool monotone = true;
    bool chord = false;
    std::vector<uint64_t> owned_values;
};

struct Event {
    std::size_t source_row = 0;
    bool chord = false;
    int camera_transition = 0;
    std::vector<uint64_t> owned_values;
    std::size_t parent = kNoEvent;
    std::size_t successor = kNoEvent;
};

struct Chart {
    std::vector<double> times;
    std::vector<Event> events;
    double max_time = 0.0;
    std::size_t displayed_note_count = 0;
    bool published = false;
};

enum class FailurePoint {
    None,
    AfterTimeReserve,
    AfterEventReserve,
    AfterNativeEventConstruction,
    AfterTailEventConstruction,
    AfterLink,
    BeforePublish,
};

struct BuildRequest {
    const std::vector<SourceRow>* rows = nullptr;
    std::size_t native_prefix_rows = 0;
    std::size_t maximum_rows = 0;
    bool experiment_enabled = false;
    bool persistent_caller = false;
    bool active_custom_descriptor = false;
    bool playback_active = false;
    bool another_chart_active = false;
    FailurePoint failure_point = FailurePoint::None;
    std::size_t failure_step = 0;
};

bool build_transactionally(const BuildRequest& request, Chart* destination);
bool links_are_internal(const Chart& chart);

} // namespace ff7r::piano::game::synthetic_model
