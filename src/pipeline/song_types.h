#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace ff7rp::pipeline {

enum class AudioSourceRole : std::uint8_t {
    Base = 0,
    Mode1 = 1,
    Mode2 = 2,
};

struct AuthoredAudioSource {
    AudioSourceRole role = AudioSourceRole::Base;
    std::string path;
    std::string filename;
    bool present = false;
};

struct ResolvedAudioSources {
    std::array<AuthoredAudioSource, 3> authored{{
        {AudioSourceRole::Base}, {AudioSourceRole::Mode1}, {AudioSourceRole::Mode2}}};
    // Mode0 always resolves to Base. Mode1/Mode2 contain their authored role
    // index when present and zero for direct Base fallback.
    std::array<std::uint8_t, 3> resolved_authored_indices{0, 0, 0};
};

enum class StatusCode {
    Ok,
    NotFound,
    IoError,
    InvalidJson,
    InvalidWav,
    InvalidAudio,
    InvalidMidi,
    InvalidChart,
    ChartStrainLimitExceeded,
    ChartRowLimitExceeded,
    UnsupportedPitch,
    CacheMiss,
    HcaUnavailable,
    MabfNotReleaseValid,
    InvalidArgument,
};

struct Status {
    StatusCode code = StatusCode::Ok;
    std::string message;

    bool ok() const { return code == StatusCode::Ok; }

    static Status ok_status() { return Status{}; }
    static Status error(StatusCode code, std::string message) { return Status{code, std::move(message)}; }
};

struct Note {
    double beat = 0.0;
    double duration_beats = 0.0;
    std::string pitch;
    std::string chord_id;
};

struct GainEnvelopePoint {
    double time_seconds = 0.0;
    double gain_db = 0.0;
};

struct SongConfig {
    std::string schema;
    std::string title;
    double bpm = 0.0;
    int difficulty = 1;
    std::vector<int> score_thresholds{0, 1200, 2400, 3600};
    std::vector<int> mode_change_combo_counts{8, 16};
    double midi_audio_offset_seconds = 0.0;
    double midi_audio_alignment_seconds = 0.0;
    double midi_minimum_lead_in_seconds = 2.0;
    bool loudness_normalization = true;
    double loudness_target_lufs = -13.0;
    double loudness_peak_ceiling_dbfs = -1.0;
    std::vector<GainEnvelopePoint> gain_envelope;
    bool metronome_enabled = false;
    double metronome_level = 0.12;
    double metronome_beat_zero_offset_seconds = 0.0;
    bool metronome_beat_zero_offset_provided = false;
    bool midi_audio_offset_provided = false;
    bool midi_audio_alignment_provided = false;
    bool bpm_provided = false;
    bool score_thresholds_provided = false;
    bool mode_change_combo_counts_provided = false;
    bool notes_provided = false;
    bool diagnostic_extended_chart_fixture = false;
    std::vector<Note> notes;
};

struct ChartNote {
    double beat = 0.0;
    double duration_beats = 0.0;
    std::string pitch;
    std::string time_str;
    std::string monotone_id;
    std::string chord_id;
    int note_type = 3;
    int dot_type = 0;
    int camera_switch_timing = 0;
    int group_index = 0;
};

struct CompiledChart {
    std::vector<ChartNote> notes;
};

struct DiagnosticChartTailRow {
    std::size_t source_row = 0;
    Note source;
    ChartNote compiled;
};

struct DiagnosticChartRetention {
    std::size_t source_row_count = 0;
    std::size_t native_prefix_row_count = 0;
    std::vector<DiagnosticChartTailRow> tail_rows;
    std::uint64_t descriptor_hash = 0;

    bool present() const { return !tail_rows.empty(); }
};

struct DifficultyProfileDiagnostics {
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
    std::size_t scheduled_rows = 0;
    std::size_t scheduled_conflicts = 0;
    std::size_t dropped_actions = 0;
    std::size_t lead_in_rejections = 0;
    std::size_t audio_duration_rejections = 0;
    std::size_t strain_rejections = 0;
    double joint_strain_p95 = 0.0;
    double joint_strain_peak = 0.0;
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
    double overlap_ratio = 0.0;
    std::string exposure_decision;
    std::string exposure_reason;
    bool complete = true;
    bool row_limit_exceeded = false;
    bool nested_from_previous = true;
};

struct LoadedDifficultyProfile {
    SongConfig config;
    CompiledChart chart;
    DifficultyProfileDiagnostics diagnostics;
    DiagnosticChartRetention diagnostic_chart;
};

struct DifficultyProfileOmission {
    int difficulty = 1;
    std::size_t desired_rows = 0;
    std::string reason;
    DifficultyProfileDiagnostics diagnostics;
    std::vector<Note> witness_notes;
};

struct WavAudio {
    std::uint32_t sample_rate = 48000;
    std::uint16_t channels = 2;
    std::size_t source_frame_count = 0;
    std::vector<float> stereo_samples;

    std::size_t frame_count() const { return stereo_samples.size() / 2; }
    double source_duration_seconds() const { return sample_rate == 0 ? 0.0 : static_cast<double>(source_frame_count) / static_cast<double>(sample_rate); }
};

struct MabfArtifactMetadata {
    std::uint64_t digest = 0;
    std::uint64_t byte_count = 0;
    std::uint64_t logical_source_frames = 0;
    std::uint32_t hca_frame_count = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::uint16_t inserted_samples = 0;
    std::uint16_t appended_samples = 0;
    std::uint16_t block_size = 0;
};

struct LoadedSong {
    std::string id;
    std::string directory;
    std::string audio_source_path;
    ResolvedAudioSources audio_sources;
    std::string midi_source_path;
    bool chart_from_midi = false;
    bool loaded_from_runtime_cache = false;
    std::size_t accepted_chart_input_limit = 512;
    std::size_t published_chart_row_limit = 512;
    bool chart_policy_enabled = false;
    std::uint64_t chart_policy_generation = 0;
    std::string chart_policy_identity;
    double midi_alignment_confidence = 0.0;
    bool loudness_normalized = false;
    bool loudness_gain_applied = false;
    bool loudness_limiter_engaged = false;
    double loudness_input_lufs = -100.0;
    double loudness_output_lufs = -100.0;
    double loudness_input_peak_dbfs = -100.0;
    double loudness_output_peak_dbfs = -100.0;
    double loudness_applied_gain_db = 0.0;
    bool gain_envelope_applied = false;
    std::size_t gain_envelope_point_count = 0;
    double gain_envelope_max_gain_db = 0.0;
    double gain_envelope_min_gain_db = 0.0;
    std::size_t metronome_beat_count = 0;
    std::size_t metronome_downbeat_count = 0;
    double metronome_first_beat_seconds = -1.0;
    double metronome_last_beat_seconds = -1.0;
    SongConfig config;
    WavAudio audio;
    CompiledChart chart;
    std::vector<LoadedDifficultyProfile> difficulty_profiles;
    std::vector<DifficultyProfileOmission> difficulty_profile_omissions;
    std::uint64_t cache_key = 0;
    std::string cache_manifest_path;
    std::string cache_sidecar_path;
    MabfArtifactMetadata mabf_metadata;
    std::uint64_t manifest_digest = 0;
    Status status;
    Status hca_status = Status::error(StatusCode::HcaUnavailable, "HCA cache has not been built yet");
};

} // namespace ff7rp::pipeline
