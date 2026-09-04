#pragma once

#include <cstddef>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "native_asset_capabilities.h"

#include "song_types.h"

namespace ff7rp::pipeline {

struct NormalizedMidiSource;

inline constexpr int kLowestMidiDifficulty = 1;
inline constexpr int kHighestMidiDifficulty = 6;
inline constexpr std::size_t kMinimumAbsoluteProfileGrowth = 4;
inline constexpr double kMinimumRelativeProfileGrowth = 0.05;
inline constexpr double kMaximumRelativeVisibleProfileGrowth = 0.35;

inline constexpr std::array<double, 5> kMidiSkillWindowSeconds{{0.5, 1.0, 2.0, 5.0, 10.0}};

struct MidiDifficultyEnvelope {
    double maximum_joint_strain_p95 = 0.0;
    double maximum_joint_strain_peak = 0.0;
};

struct MidiJointStrainMetrics {
    double p95 = 0.0;
    double peak = 0.0;
};

struct MidiLocalSkillMetrics {
    std::array<std::size_t, 5> maximum_window_actions{};
    std::array<double, 5> maximum_window_begin_seconds{};
    std::size_t maximum_quarter_second_stream_actions = 0;
    double maximum_quarter_second_stream_duration = 0.0;
    double maximum_quarter_second_stream_begin_seconds = 0.0;
    double maximum_quarter_second_stream_actions_end_seconds = 0.0;
    double maximum_quarter_second_stream_duration_begin_seconds = 0.0;
    double maximum_quarter_second_stream_duration_end_seconds = 0.0;
    std::size_t maximum_half_second_stream_actions = 0;
    double maximum_half_second_stream_duration = 0.0;
    double maximum_half_second_stream_begin_seconds = 0.0;
    double maximum_half_second_stream_actions_end_seconds = 0.0;
    double maximum_half_second_stream_duration_begin_seconds = 0.0;
    double maximum_half_second_stream_duration_end_seconds = 0.0;
    std::size_t maximum_jack_run = 0;
    double maximum_jack_begin_seconds = 0.0;
    std::size_t maximum_reversal_run = 0;
    double maximum_reversal_begin_seconds = 0.0;
    double rapid_movement_p90 = 0.0;
    double rapid_movement_maximum = 0.0;
    double rapid_movement_maximum_seconds = 0.0;
    double octave_movement_rate = 0.0;
    std::size_t maximum_octave_movements_in_five_seconds = 0;
    double maximum_octave_window_begin_seconds = 0.0;
    std::size_t maximum_large_reversals_in_five_seconds = 0;
    double maximum_large_reversal_window_begin_seconds = 0.0;
    double right_fatigue_peak = 0.0;
    double right_fatigue_peak_seconds = 0.0;
    double left_fatigue_peak = 0.0;
    double left_fatigue_peak_seconds = 0.0;
    double hand_imbalance = 0.0;
    double rhythm_irregularity_p90 = 0.0;
    double rhythm_irregularity_maximum = 0.0;
    double rhythm_irregularity_peak_seconds = 0.0;
    double hardest_window_begin_seconds = 0.0;
    double hardest_window_end_seconds = 0.0;
    int dominant_skill = 0;
    int satisfied_route = -1;
    double satisfied_route_ratio = 0.0;
    double satisfied_route_margin = 0.0;
    bool dominant_skill_is_global = false;
    std::string satisfied_route_name;
};

struct MidiDifficultySpec {
    double target_actions_per_minute = 0.0;
    double target_tolerance = 0.0;
    std::size_t route_count = 0;
};

struct MidiRouteValidation {
    bool feasible = false;
    int route_index = -1;
    double ratio = 0.0;
    double margin = 0.0;
    MidiLocalSkillMetrics metrics;
};

MidiDifficultyEnvelope midi_difficulty_envelope(int difficulty);
MidiDifficultySpec midi_difficulty_spec(int difficulty);
MidiJointStrainMetrics analyze_midi_joint_strain(const std::vector<Note>& notes, double bpm);
MidiLocalSkillMetrics analyze_midi_local_skills(const std::vector<Note>& notes, double bpm);
MidiRouteValidation validate_midi_difficulty_route(
    const std::vector<Note>& notes, double bpm, int difficulty);
std::size_t minimum_midi_profile_growth(std::size_t previous_actions);
bool has_meaningful_midi_profile_growth(std::size_t previous_actions, std::size_t next_actions);
std::size_t maximum_midi_visible_profile_actions(std::size_t previous_actions);

struct MidiChartStats {
    std::size_t source_tracks = 0;
    std::size_t source_events = 0;
    std::size_t melody_tracks = 0;
    std::size_t harmony_tracks = 0;
    std::size_t melody_onsets = 0;
    std::size_t exact_tick_groups = 0;
    std::size_t humanized_clusters = 0;
    std::size_t humanized_events = 0;
    std::size_t melody_candidates = 0;
    std::size_t fallback_candidates = 0;
    std::size_t chord_candidates = 0;
    std::size_t right_events = 0;
    std::size_t left_events = 0;
    std::size_t fallback_events = 0;
    std::size_t merged_events = 0;
    std::size_t evidence_rejections = 0;
    std::size_t cooldown_rejections = 0;
    std::size_t burst_rejections = 0;
    std::size_t strain_rejections = 0;
    std::size_t retention_rejections = 0;
    std::size_t action_rate_rejections = 0;
    std::size_t right_collisions = 0;
    std::size_t left_collisions = 0;
    std::size_t cross_hand_conflicts = 0;
    std::size_t scheduled_conflicts = 0;
    std::size_t dropped_conflicts = 0;
    std::size_t selected_actions = 0;
    std::size_t candidate_actions = 0;
    std::size_t candidate_frames = 0;
    std::size_t protected_baseline_actions = 0;
    std::size_t target_rows = 0;
    std::size_t target_minimum_rows = 0;
    std::size_t target_maximum_rows = 0;
    std::size_t target_exclusions = 0;
    std::size_t local_skill_rejections = 0;
    std::size_t retained_actions = 0;
    std::size_t removed_actions = 0;
    std::size_t replaced_actions = 0;
    std::size_t added_actions = 0;
    std::size_t desired_rows = 0;
    std::size_t lead_in_rejections = 0;
    std::size_t audio_duration_rejections = 0;
    bool row_limit_exceeded = false;
    std::size_t voice_stream_changes = 0;
    std::size_t time_signature_changes = 0;
    std::size_t metric_downbeat_candidates = 0;
    std::size_t metric_downbeat_right_events = 0;
    std::size_t source_pitch_witness_failures = 0;
    std::size_t octave_fixes = 0;
    double source_bpm = 120.0;
    double selected_retention = 0.0;
    double actions_per_minute = 0.0;
    double joint_strain_p95 = 0.0;
    double joint_strain_peak = 0.0;
    MidiLocalSkillMetrics local_skills;
    double minimum_right_gap_seconds = 0.0;
    double minimum_right_gap_beats = 0.0;
    double audio_alignment_seconds = 0.0;
    double audio_offset_seconds = 0.0;
    double alignment_confidence = 0.0;
};

// Returns an observed native pca_* ID only for one unambiguous, complete chord.
// Input pitches must be fresh non-melody MIDI attacks from one onset cluster.
std::string infer_native_chord_from_fresh_midi_pitches(const std::vector<int>& midi_pitches);

// Returns the authored Vanilla adaptive-mode thresholds for a calibrated route.
// Unknown routes return {0, 0} so callers can preserve their existing fallback.
std::array<int, 2> vanilla_mode_change_counts_for_route(std::string_view route_name);

Status generate_notes_from_midi(
    const std::string& midi_path,
    const WavAudio& audio,
    const SongConfig& config,
    std::vector<Note>* out_notes,
    MidiChartStats* out_stats = nullptr,
    const std::vector<Note>* preferred_baseline = nullptr,
    std::size_t maximum_visible_rows = 0,
    NativeAssetCapabilities native_assets = selected_native_asset_capabilities());

Status generate_notes_from_normalized_midi(
    const NormalizedMidiSource& source,
    const WavAudio& audio,
    const SongConfig& config,
    std::vector<Note>* out_notes,
    MidiChartStats* out_stats = nullptr,
    const std::vector<Note>* preferred_baseline = nullptr,
    std::size_t maximum_visible_rows = 0,
    NativeAssetCapabilities native_assets = selected_native_asset_capabilities());

} // namespace ff7rp::pipeline
