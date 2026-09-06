#pragma once

#include <windows.h>

#include <cstdint>
#include <array>
#include <string_view>

#include "core/hooks.h"
#include "game/chart_patch.h"

namespace ff7r::piano::game {

struct SongDifficultyProfile;
struct RenderSnapshot;
struct PlaybackSnapshot;
struct SelectionAudioAdmissionAuthority;

struct ExtendedChartCapability {
    bool shipping_helpers_valid = false;
    bool helper_spec_valid = false;
    bool diagnostic_input_available = false;
    bool playable_authority_available = false;
    std::uint64_t policy_generation = 0;
};

struct ExtendedChartBuildSpec {
    std::string_view build_id;
    std::array<std::uint8_t, 5> persistent_expand_call;
    std::array<std::uint8_t, 32> fname_signature;
    std::array<std::uintptr_t, 4> callback_vtable_slots;
};

inline const ExtendedChartBuildSpec* find_extended_chart_build_spec(
    const std::string_view build_id) noexcept
{
    static constexpr ExtendedChartBuildSpec build_1005{
        "ff7rebirth-steam-win64-6a16ced2",
        {0xe8, 0xb1, 0xf1, 0x01, 0x00},
        {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0xb8,0x40,
         0x04,0x00,0x00,0xe8,0xa4,0xa3,0x61,0x01,0x48,0x2b,0xe0,0x48,0x8b,0x05,0x1a,0xb0},
        {0x02805da4, 0x01958660, 0x007a5680, 0x027fcc44}};
    static constexpr ExtendedChartBuildSpec build_1004{
        "ff7rebirth-steam-win64-68fd6fde",
        {0xe8, 0xc5, 0xe8, 0x01, 0x00},
        {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0xb8,0x40,
         0x04,0x00,0x00,0xe8,0x80,0xcc,0x82,0x01,0x48,0x2b,0xe0,0x48,0x8b,0x05,0x46,0x75},
        {0x020d1d40, 0x018d5bd0, 0x020b6dc0, 0x020b5820}};
    if (build_id == build_1005.build_id) return &build_1005;
    if (build_id == build_1004.build_id) return &build_1004;
    return nullptr;
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
void begin_extended_chart_transaction(
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
    ChartAudioDiagnosticTerminalOutcome outcome,
    std::uint64_t successful_lifecycle_epoch = 0) noexcept;
core::HookTeardownOperation extended_chart_reserve_teardown_operation();
void clear_extended_chart_runtime_state() noexcept;
bool playable_extended_profile(const SongDifficultyProfile& profile) noexcept;
bool playable_extended_playback(const PlaybackSnapshot& playback) noexcept;
bool playable_extended_presentation(
    const RenderSnapshot& menu, const PlaybackSnapshot& playback) noexcept;

} // namespace ff7r::piano::game
