#pragma once

#include <windows.h>

#include <cstdint>

namespace ff7r::piano::game {

struct ExtendedChartSupport {
    bool requested = false;
    bool shipping_helpers_valid = false;
    bool diagnostic_input_available = false;
    bool mutation_available = false;
    std::uint64_t policy_generation = 0;
};

ExtendedChartSupport configure_extended_chart_experiment(HMODULE exe_module, bool requested);

} // namespace ff7r::piano::game
