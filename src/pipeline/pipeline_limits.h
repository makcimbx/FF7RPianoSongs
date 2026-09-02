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
inline constexpr std::size_t kPlayable513ChartRows = 513u;
// 1024 rows bound modeled event storage to roughly 144 KiB plus owned fields,
// while limiting construction and validation work to twice the shipping path.
inline constexpr std::size_t kExperimentalMaxChartRows = 1024u;

struct ChartRowPolicySnapshot {
    bool requested = false;
    bool enabled = false;
    bool playable_513_available = false;
    std::size_t accepted_input_limit = kMaxChartRows;
    std::size_t publication_limit = kMaxChartRows;
    std::uint64_t generation = 0;

    std::string identity() const
    {
        return enabled
            ? (playable_513_available
                ? "chart_rows=native512+playable513+diagnostic520;extended=verified1005"
                : "chart_rows=native512+diagnostic1024;extended=verified")
            : "chart_rows=native512;extended=disabled";
    }
};

// Bits 0/1 are requested/enabled; the remaining bits are one coherent generation.
inline std::atomic_uint64_t g_chart_row_policy_state{0};

inline void configure_chart_row_limit(bool requested, bool diagnostic_input_available,
    bool playable_513_available = false)
{
    const std::uint64_t flags = (requested ? 1ull : 0ull) |
        (requested && diagnostic_input_available ? 2ull : 0ull) |
        (requested && diagnostic_input_available && playable_513_available ? 4ull : 0ull);
    std::uint64_t current = g_chart_row_policy_state.load(std::memory_order_acquire);
    while ((current & 7ull) != flags) {
        const std::uint64_t generation = (current >> 3u) + 1u;
        const std::uint64_t desired = (generation << 3u) | flags;
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
    snapshot.requested = (state & 1ull) != 0;
    snapshot.enabled = (state & 2ull) != 0;
    snapshot.playable_513_available = (state & 4ull) != 0;
    snapshot.accepted_input_limit = snapshot.enabled ? kExperimentalMaxChartRows : kMaxChartRows;
    snapshot.generation = state >> 3u;
    return snapshot;
}

inline bool experimental_extended_charts_enabled()
{
    return chart_row_policy_snapshot().enabled;
}

inline bool experimental_extended_charts_requested()
{
    return chart_row_policy_snapshot().requested;
}

inline std::size_t effective_chart_row_limit()
{
    return kMaxChartRows;
}

// Only verified shipping helpers permit retention. Runtime publication remains fixed at 512.
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
