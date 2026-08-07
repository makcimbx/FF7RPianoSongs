#pragma once

#include "game/chart_patch.h"
#include "game/song_registry.h"
#include "game/runtime_context_policy.h"

namespace ff7r::piano::game {

using ChartExpandPreparationOutcome = ChartMutationTransactionOutcome;

void capture_active_note_count(int note_count);
int active_captured_note_count();
void reset_active_note_count();
int resolve_note_count(const PlaybackSnapshot& playback);
int resolve_menu_or_playback_note_count(
    const RenderSnapshot& menu, const PlaybackSnapshot& playback);
inline int menu_or_playback_note_count_value(
    const RenderSnapshot& menu, const PlaybackSnapshot& playback,
    const int captured, const int maximum) noexcept
{
    const auto valid = [maximum](const int value) {
        return value > 0 && value <= maximum;
    };
    if (menu.song) {
        const int configured = menu.profile
            ? menu.profile->note_count : menu.song->note_count;
        return valid(configured) ? configured : 0;
    }
    const int configured = playback.profile
        ? playback.profile->note_count
        : (playback.song ? playback.song->note_count : 0);
    if (valid(configured)) return configured;
    return valid(captured) ? captured : 0;
}
void log_active_chart_memory(float playback_seconds);
ChartExpandPreparationOutcome prepare_active_chart_row_patch_before_expand(
    void* wrapper, void* chart_row, uintptr_t caller_rva,
    ChartAudioDiagnosticTransaction* diagnostic = nullptr) noexcept;
void finish_active_chart_row_patch_after_expand(
    void* wrapper, uintptr_t caller_rva) noexcept;
bool restore_chart_patch_for_shutdown();

} // namespace ff7r::piano::game
