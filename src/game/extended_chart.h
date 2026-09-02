#pragma once

#include <windows.h>

#include <cstdint>

#include "core/hooks.h"
#include "game/chart_patch.h"

namespace ff7r::piano::game {

struct SongDifficultyProfile;

struct ExtendedChartSupport {
    bool requested = false;
    bool shipping_helpers_valid = false;
    bool diagnostic_input_available = false;
    bool mutation_available = false;
    std::uint64_t policy_generation = 0;
};

ExtendedChartSupport configure_extended_chart_experiment(HMODULE exe_module, bool requested);
bool install_extended_chart_reserve_hook(HMODULE exe_module, std::string& error);
void begin_extended_chart_transaction(
    const ChartAudioExpandTlsSnapshot& transaction, void* wrapper,
    void* chart_row, uintptr_t caller_rva) noexcept;
bool finish_extended_chart_transaction(void* wrapper, void* chart_row,
    uintptr_t caller_rva) noexcept;
void abort_extended_chart_transaction() noexcept;
core::HookTeardownOperation extended_chart_reserve_teardown_operation();
void clear_extended_chart_runtime_state() noexcept;
bool playable_513_profile(const SongDifficultyProfile& profile) noexcept;

} // namespace ff7r::piano::game
