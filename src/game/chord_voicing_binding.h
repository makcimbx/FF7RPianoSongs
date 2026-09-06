#pragma once

#include "game/song_registry.h"
#include "pipeline/chart_event_plan.h"

namespace ff7r::piano::game {

struct ResolvedChordVoicing {
    uint64_t chord = 0;
    std::array<uint64_t, 5> sounds{};
    uint32_t count = 0;
    uint32_t stock_width = 0;
};

// Immutable after preflight. Native addresses are identity-only, not ownership.
// Guard/playback/cleanup carry the one readiness record; no side registry.
struct ChordVoicingBinding {
    SelectionSnapshot selection;
    CustomContextToken lease;
    uint64_t preparation_ordinal = 0;
    void* wrapper = nullptr;
    void* controller = nullptr;
    void* control_block = nullptr;
    void* left = nullptr;
    void* right = nullptr;
    ff7rp::pipeline::ChartEventPlan plan;
    std::vector<uint64_t> event_names;
    std::vector<size_t> event_successors;
    std::vector<ResolvedChordVoicing> voicings;
};

} // namespace ff7r::piano::game
