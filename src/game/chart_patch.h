#pragma once

#include "game/runtime_context_policy.h"

#include <cstdint>
#include <type_traits>

namespace ff7r::piano::game {

enum class ChartAudioDiagnosticStartupMode : uint8_t {
    Unknown,
    Unreadable,
    IdleNull,
    ActiveSound,
    InvalidMixed,
};

enum class ChartAudioDiagnosticTerminalOutcome : uint8_t {
    None,
    NativePristine,
    MutationUnresolved,
    CustomPrepared,
    ExpandFinished,
    ExpandException,
    AudioPublished,
    AudioFailed,
    StopFailed,
    Superseded,
    ListExit,
};

struct ChartAudioDiagnosticControllerProof final {
    bool valid = false;
    ControllerIdentityProofMode mode = ControllerIdentityProofMode::Invalid;
    bool raw_index_readable = false;
    int32_t raw_index = -1;
    bool live_capture_succeeded = false;
    int32_t live_index = -1;
    int32_t live_serial = 0;
    bool item_capture_succeeded = false;
    int32_t item_index = -1;
    int32_t item_serial = 0;
};

struct ChartAudioDiagnosticPrewriteSnapshot final {
    bool attempted = false;
    bool exact_admission = false;
    uint64_t selection_generation = 0;
    uint64_t route_generation = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
    uintptr_t controller = 0;
    ChartAudioDiagnosticControllerProof controller_proof{};
    bool chain_read = false;
    bool sound_read = false;
    bool request_read = false;
    bool state_read = false;
    uintptr_t slot = 0;
    uintptr_t bgm = 0;
    uintptr_t sound = 0;
    uint64_t request = 0;
    uint8_t state = 0xff;
    ChartAudioDiagnosticStartupMode startup_mode
        = ChartAudioDiagnosticStartupMode::Unknown;
};

struct ChartAudioDiagnosticTransaction final {
    bool active = false;
    uint64_t generation = 0;
    uint64_t preparation_ordinal = 0;
    uint64_t route_lifecycle_epoch = 0;
    uintptr_t wrapper = 0;
    uintptr_t chart_row = 0;
    uint8_t preparation_outcome = 0;
    bool journal_active = false;
    bool audio_committed = false;
    ChartAudioDiagnosticTerminalOutcome terminal_outcome
        = ChartAudioDiagnosticTerminalOutcome::None;
    ChartAudioDiagnosticPrewriteSnapshot prewrite{};
};

struct ChartAudioDiagnosticArraySnapshot final {
    bool data_read = false;
    bool count_read = false;
    bool capacity_read = false;
    uintptr_t data = 0;
    int32_t count = -1;
    int32_t capacity = -1;
};

struct ChartAudioExpandTlsSnapshot final {
    bool active = false;
    uint64_t generation = 0;
    uint64_t selection_generation = 0;
    uint64_t route_generation = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
    uint32_t depth = 0;
    uint64_t enter_ordinal = 0;
    uint64_t exit_ordinal = 0;
    bool original_inflight = false;
    ChartAudioDiagnosticArraySnapshot enter_time{};
    ChartAudioDiagnosticArraySnapshot enter_event{};
    ChartAudioDiagnosticArraySnapshot exit_time{};
    ChartAudioDiagnosticArraySnapshot exit_event{};
};

static_assert(std::is_trivially_copyable_v<ChartAudioDiagnosticTransaction>);
static_assert(std::is_trivially_copyable_v<ChartAudioExpandTlsSnapshot>);

bool chart_audio_diagnostic_transaction_exact(
    uint64_t selection_generation,
    uint64_t route_generation,
    uint64_t lease_generation,
    uint64_t song_key,
    ChartAudioDiagnosticTransaction& out) noexcept;
bool chart_audio_diagnostic_generation_exact(
    uint64_t generation,
    ChartAudioDiagnosticTransaction& out) noexcept;
ChartAudioExpandTlsSnapshot current_chart_audio_expand_tls() noexcept;
void chart_audio_diagnostic_expand_completed(
    const ChartAudioDiagnosticTransaction& transaction) noexcept;
void finish_chart_audio_diagnostic_transaction(
    const ChartAudioDiagnosticTransaction& transaction,
    ChartAudioDiagnosticTerminalOutcome outcome,
    uint64_t successful_lifecycle_epoch = 0) noexcept;

} // namespace ff7r::piano::game
