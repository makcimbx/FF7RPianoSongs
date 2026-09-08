#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ff7rp::pipeline {

inline constexpr std::size_t kMaxSongFrames = 48000u * 60u * 10u;
inline constexpr std::size_t kMaxWavFileBytes = 512u * 1024u * 1024u;
inline constexpr std::size_t kMaxMabfBytes = 64u * 1024u * 1024u;
// FUN_1439B3608 advances its row cursor by three fields until 0x600,
// so the shipping PianoScore parser consumes at most 512 rows.
inline constexpr std::size_t kMaxChartRows = 512u;
inline constexpr std::size_t kMinimumExtendedChartRows = kMaxChartRows + 1u;
inline constexpr std::size_t kMaximumExtendedChartRows = 8192u;
inline constexpr std::size_t kMaximumNativeChartEvents = 8192u;
inline constexpr std::size_t kMaximumExtendedChartTailRows =
    kMaximumExtendedChartRows - kMaxChartRows;
inline constexpr std::size_t kLegacyDiagnosticChartInputRows = 1024u;
inline constexpr const char* kDisabledChartRowPolicyIdentity =
    "chart_rows=native512;extended=disabled";
inline constexpr const char* kDiagnosticChartRowPolicyIdentity =
    // Compatibility token: format-14 diagnostic caches retain this identity even
    // though the bounded internal retention ceiling is now 8192 rows.
    "chart_rows=native512+diagnostic1024;extended=verified";
inline constexpr const char* kPlayableExtendedChartRowPolicyIdentity =
    "chart_rows=8192;events=8192;groups+dual+chords=verified1004+1005";
inline constexpr const char* kGeneratedMidiGenerationIdentity =
    "midi_generation=independent_ungrouped:key_signature_spelling+exact_note_values+exclude_unsupported_pitches+soft_density_growth+bounded_feasible_beam+salient_alternates+exact_partial_lh:v15";
inline constexpr std::size_t kExperimentalMaxChartRows = kMaximumExtendedChartRows;

struct ChartRowPolicySnapshot {
    bool enabled = false;
    bool playable_extended_available = false;
    std::size_t accepted_input_limit = kMaxChartRows;
    std::size_t publication_limit = kMaxChartRows;
    std::uint64_t generation = 0;

    std::string identity() const
    {
        return enabled
            ? (playable_extended_available
                ? kPlayableExtendedChartRowPolicyIdentity
                : kDiagnosticChartRowPolicyIdentity)
            : kDisabledChartRowPolicyIdentity;
    }
};

// Bits 0/1 are diagnostic/playable capability; the remaining bits are one coherent generation.
inline std::atomic_uint64_t g_chart_row_policy_state{0};

inline void configure_chart_row_limit(bool diagnostic_input_available,
    bool playable_extended_available = false)
{
    const std::uint64_t flags = (diagnostic_input_available ? 1ull : 0ull) |
        (diagnostic_input_available && playable_extended_available ? 2ull : 0ull);
    std::uint64_t current = g_chart_row_policy_state.load(std::memory_order_acquire);
    while ((current & 3ull) != flags) {
        const std::uint64_t generation = (current >> 2u) + 1u;
        const std::uint64_t desired = (generation << 2u) | flags;
        if (g_chart_row_policy_state.compare_exchange_weak(
                current, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return;
        }
    }
}

inline ChartRowPolicySnapshot chart_row_policy_snapshot()
{
    const std::uint64_t state = g_chart_row_policy_state.load(std::memory_order_acquire);
    ChartRowPolicySnapshot snapshot;
    snapshot.enabled = (state & 1ull) != 0;
    snapshot.playable_extended_available = (state & 2ull) != 0;
    snapshot.accepted_input_limit = snapshot.enabled ? kExperimentalMaxChartRows : kMaxChartRows;
    snapshot.publication_limit = snapshot.playable_extended_available
        ? kMaximumExtendedChartRows : kMaxChartRows;
    snapshot.generation = state >> 2u;
    return snapshot;
}

inline bool extended_chart_input_enabled()
{
    return chart_row_policy_snapshot().enabled;
}

inline bool playable_extended_transport_available()
{
    return chart_row_policy_snapshot().playable_extended_available;
}

inline std::size_t effective_chart_row_limit()
{
    return kMaxChartRows;
}

// Retention may reach the explicit safety ceiling; publication authority remains
// descriptor-gated to a complete, strictly eligible count-driven chart.
inline std::size_t chart_input_row_limit()
{
    return chart_row_policy_snapshot().accepted_input_limit;
}

inline std::uint64_t chart_row_policy_generation()
{
    return chart_row_policy_snapshot().generation;
}

inline std::string chart_row_policy_identity()
{
    return chart_row_policy_snapshot().identity();
}

} // namespace ff7rp::pipeline
