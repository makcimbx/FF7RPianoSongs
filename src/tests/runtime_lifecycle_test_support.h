#pragma once

#include "game/song_registry.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define FF7RP_RUNTIME_LIFECYCLE_CASES(X) \
    X(test_shutdown_order_and_failures) \
    X(test_paused_callback_timeout) \
    X(test_callback_gate_exclusive_conversion) \
    X(test_transactional_native_restore) \
    X(test_native_array_publication_journal) \
    X(test_render_context_and_registry_generation) \
    X(test_runtime_context_isolation) \
    X(test_private_controller_stop_diagnostics) \
    X(test_bgm_playback_native_play_pause_resume_integration) \
    X(test_bgm_playback_aggregate_observer_integration) \
    X(test_pause_resume_marker_delivery_policy) \
    X(test_pause_resume_retirement_mismatch_diagnostic) \
    X(test_identity_prefilter_and_deferred_emission) \
    X(test_onmemory_bank_residency_diagnostic_policy) \
    X(test_onmemory_bank_sequential_lifecycle) \
    X(test_onmemory_bank_completed_owner_zero_rearm) \
    X(test_audio_patch_exact_override_metadata_normalization) \
    X(test_onmemory_bank_exact_owner_restore) \
    X(test_onmemory_bank_observed_null_backing) \
    X(test_onmemory_bank_shared_resident_retirement) \
    X(test_pause_resume_latest_completed_retirement_release) \
    X(test_production_context_transition_policies) \
    X(test_shutdown_aggregation) \
    X(test_immutable_publication_and_capture_identity) \
    X(test_audio_journal_commit_policy) \
    X(test_audio_patch_restore_failure_report) \
    X(test_frozen_profile_lease_lifecycle) \
    X(test_native_handoff_cleanup_policy) \
    X(test_deferred_native_handoff_state_machine) \
    X(test_deferred_native_handoff_recovery_forward_retires) \
    X(test_audio_stop_retirement_monitor) \
    X(test_audio_natural_completion_retirement_policy) \
    X(test_reentrant_play_setup_private_claim_policy)

namespace ff7r::piano::tests::runtime_lifecycle {

inline void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "runtime_lifecycle_selftest: " << message << '\n';
        std::exit(1);
    }
}

inline ff7r::piano::game::SongDescriptor make_song(
    const std::string& id, int visible_index)
{
    ff7r::piano::game::SongDescriptor song;
    song.id = id;
    song.visible_index = visible_index;
    song.profiles.push_back({});
    return song;
}

} // namespace ff7r::piano::tests::runtime_lifecycle
