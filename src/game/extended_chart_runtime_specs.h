#pragma once

#include "game/rvas.h"
#include "game/runtime_layouts.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ff7r::piano::game {

struct ExtendedChartCanonicalSpec {
    std::string_view signature_id;
    uintptr_t rva = 0;
};

inline constexpr uintptr_t kPersistentChartOwnerOffset = runtime_layouts::PianoCompletionOwner::chart;

inline constexpr std::array<ExtendedChartCanonicalSpec, 6> kExtendedChartCanonicalSpecs{{
    {"piano_score_expand", rva::PianoScoreExpand},
    {"piano_score_parser", rva::PianoScoreParser},
    {"piano_event_deep_copy", rva::PianoEventDeepCopy},
    {"piano_event_construct", rva::PianoEventConstruct},
    {"piano_event_link", rva::PianoEventLink},
    {"piano_event_destruct", rva::PianoEventDestruct},
}};

} // namespace ff7r::piano::game
