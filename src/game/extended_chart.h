#pragma once

#include <windows.h>

#include <cstdint>
#include <array>
#include <string_view>

#include "core/hooks.h"
#include "game/chart_patch.h"
#include "game/runtime_build_profile.h"

namespace ff7r::piano::game {

struct SongDifficultyProfile;
struct PlaybackSnapshot;
struct SelectionAudioAdmissionAuthority;

struct ExtendedChartCapability {
    bool shipping_helpers_valid = false;
    bool helper_spec_valid = false;
    bool diagnostic_input_available = false;
    bool playable_authority_available = false;
    std::uint64_t policy_generation = 0;
};

inline const ExtendedChartBuildSpec* find_extended_chart_build_spec(
    const std::string_view build_id) noexcept
{
    const auto* profile = find_runtime_build_profile(build_id);
    return profile ? &profile->extended_chart : nullptr;
}
inline constexpr bool extended_chart_capability_ready(
    const bool helper_spec_valid, const bool reserve_hook_installed) noexcept
{
    return helper_spec_valid && reserve_hook_installed;
}
ExtendedChartCapability configure_extended_chart_capability(HMODULE exe_module);
// Diagnostic applicability only: never grants admission or bypasses a native guard.
inline constexpr bool extended_chart_begin_rejection_relevant(
    bool native_transaction_active, bool activation_present,
    bool profile_present, bool extended_rows_present) noexcept
{
    return native_transaction_active || extended_rows_present
        || (activation_present && !profile_present);
}
bool install_extended_chart_reserve_hook(HMODULE exe_module, std::string& error);
bool begin_extended_chart_transaction(
    const ChartAudioExpandTlsSnapshot& transaction,
    const SelectionAudioAdmissionAuthority& authority, void* wrapper,
    void* chart_row, uintptr_t caller_rva) noexcept;
bool finish_extended_chart_transaction(void* wrapper, void* chart_row,
    uintptr_t caller_rva) noexcept;
// Read-only synchronous handoff used by chart-source cleanup. True only while
// the exact admitted transaction still owns the parser-produced P-event header.
bool extended_chart_parser_count_preservation_exact(
    void* wrapper, uintptr_t caller_rva) noexcept;
void abort_extended_chart_transaction() noexcept;
void invalidate_extended_chart_commit(const char* reason) noexcept;
void extended_chart_activation_terminal(
    std::uint64_t activation_generation,
    std::uint64_t route_lifecycle_epoch,
    ChartAudioDiagnosticTerminalOutcome outcome) noexcept;
core::HookTeardownOperation extended_chart_reserve_teardown_operation();
void clear_extended_chart_runtime_state() noexcept;
bool playable_extended_profile(const SongDifficultyProfile& profile) noexcept;
bool playable_extended_playback(const PlaybackSnapshot& playback) noexcept;

} // namespace ff7r::piano::game
