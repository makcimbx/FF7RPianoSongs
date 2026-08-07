#include "cache_manifest_renderer.h"

#include <algorithm>
#include <filesystem>
#include <locale>
#include <sstream>

#include "cache.h"
#include "runtime_cache_codec.h"

namespace ff7rp::pipeline {
namespace {

struct ProfileActionCounts {
    std::size_t right = 0;
    std::size_t left = 0;
    std::size_t dual = 0;
};

ProfileActionCounts profile_action_counts(const SongConfig& config) {
    ProfileActionCounts counts;
    for (const Note& note : config.notes) {
        const bool right = !note.pitch.empty();
        const bool left = !note.chord_id.empty();
        counts.right += right ? 1u : 0u;
        counts.left += left ? 1u : 0u;
        counts.dual += right && left ? 1u : 0u;
    }
    return counts;
}

} // namespace
Status render_cache_manifest(const LoadedSong& song, const SongConfig& source_config, std::string* manifest) {
    if (!manifest) return Status::error(StatusCode::InvalidArgument, "manifest output must not be null");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "version=" << kPipelineCacheVersion << "\n";
    out << "cache_key=" << hex64(song.cache_key) << "\n";
    out << "chart_row_policy=" << song.chart_policy_identity << "\n";
    out << "accepted_chart_input_limit=" << song.accepted_chart_input_limit << "\n";
    out << "published_chart_row_limit=" << song.published_chart_row_limit << "\n";
    out << "chart_policy_enabled=" << static_cast<int>(song.chart_policy_enabled) << "\n";
    out << "chart_policy_generation=" << song.chart_policy_generation << "\n";
    out << "mabf_digest=" << hex64(song.mabf_metadata.digest) << "\n";
    out << "mabf_bytes=" << song.mabf_metadata.byte_count << "\n";
    out << "mabf_logical_source_frames=" << song.mabf_metadata.logical_source_frames << "\n";
    out << "mabf_sample_rate=" << song.mabf_metadata.sample_rate << "\n";
    out << "mabf_channels=" << song.mabf_metadata.channels << "\n";
    out << "mabf_block_size=" << song.mabf_metadata.block_size << "\n";
    out << "title=" << song.config.title << "\n";
    out << "config_chart_semantic_hash=" << hex64(config_chart_semantic_hash(song.config, song.chart)) << "\n";
    out << "notes=" << song.chart.notes.size() << "\n";
    out << "profiles=" << song.difficulty_profiles.size() << "\n";
    out << "profile_rows=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << song.difficulty_profiles[index].chart.notes.size();
    }
    out << "\n";
    out << "profile_difficulties=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << song.difficulty_profiles[index].config.difficulty;
    }
    out << "\n";
    const auto write_action_counts = [&](const char* name, const auto member) {
        out << name << "=";
        for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
            if (index > 0) out << ",";
            out << profile_action_counts(song.difficulty_profiles[index].config).*member;
        }
        out << "\n";
    };
    write_action_counts("profile_right_actions", &ProfileActionCounts::right);
    write_action_counts("profile_left_actions", &ProfileActionCounts::left);
    write_action_counts("profile_dual_rows", &ProfileActionCounts::dual);
    out << "profile_complete=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << static_cast<int>(song.difficulty_profiles[index].diagnostics.complete);
    }
    out << "\n";
    out << "profile_diagnostic_source_rows=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << song.difficulty_profiles[index].diagnostic_chart.source_row_count;
    }
    out << "\nprofile_diagnostic_native_prefix_rows=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << song.difficulty_profiles[index].diagnostic_chart.native_prefix_row_count;
    }
    out << "\nprofile_diagnostic_tail_rows=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << song.difficulty_profiles[index].diagnostic_chart.tail_rows.size();
    }
    out << "\n";
    out << "profile_diagnostic_descriptor_hashes=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << hex64(song.difficulty_profiles[index].diagnostic_chart.descriptor_hash);
    }
    out << "\nprofile_semantic_hashes=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << hex64(profile_semantic_hash(song.difficulty_profiles[index]));
    }
    out << "\n";
    out << "profile_row_limit_exceeded=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << static_cast<int>(song.difficulty_profiles[index].diagnostics.row_limit_exceeded);
    }
    out << "\n";
    const auto write_diagnostic_counts = [&](const char* name, const auto member) {
        out << name << "=";
        for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
            if (index > 0) out << ",";
            out << song.difficulty_profiles[index].diagnostics.*member;
        }
        out << "\n";
    };
    write_diagnostic_counts("profile_selected_actions", &DifficultyProfileDiagnostics::selected_actions);
    write_diagnostic_counts("profile_candidate_actions", &DifficultyProfileDiagnostics::candidate_actions);
    write_diagnostic_counts("profile_candidate_frames", &DifficultyProfileDiagnostics::candidate_frames);
    write_diagnostic_counts("profile_protected_baseline_actions", &DifficultyProfileDiagnostics::protected_baseline_actions);
    write_diagnostic_counts("profile_target_rows", &DifficultyProfileDiagnostics::target_rows);
    write_diagnostic_counts("profile_target_minimum_rows", &DifficultyProfileDiagnostics::target_minimum_rows);
    write_diagnostic_counts("profile_target_maximum_rows", &DifficultyProfileDiagnostics::target_maximum_rows);
    write_diagnostic_counts("profile_target_exclusions", &DifficultyProfileDiagnostics::target_exclusions);
    write_diagnostic_counts("profile_local_skill_rejections", &DifficultyProfileDiagnostics::local_skill_rejections);
    write_diagnostic_counts("profile_retained_actions", &DifficultyProfileDiagnostics::retained_actions);
    write_diagnostic_counts("profile_removed_actions", &DifficultyProfileDiagnostics::removed_actions);
    write_diagnostic_counts("profile_replaced_actions", &DifficultyProfileDiagnostics::replaced_actions);
    write_diagnostic_counts("profile_added_actions", &DifficultyProfileDiagnostics::added_actions);
    write_diagnostic_counts("profile_scheduled_rows", &DifficultyProfileDiagnostics::scheduled_rows);
    write_diagnostic_counts("profile_scheduled_conflicts", &DifficultyProfileDiagnostics::scheduled_conflicts);
    write_diagnostic_counts("profile_dropped_actions", &DifficultyProfileDiagnostics::dropped_actions);
    write_diagnostic_counts("profile_lead_in_rejections", &DifficultyProfileDiagnostics::lead_in_rejections);
    write_diagnostic_counts("profile_audio_duration_rejections", &DifficultyProfileDiagnostics::audio_duration_rejections);
    write_diagnostic_counts("profile_strain_rejections", &DifficultyProfileDiagnostics::strain_rejections);
    const auto write_diagnostic_values = [&](const char* name, const auto member) {
        out << name << "=";
        for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
            if (index > 0) out << ",";
            out << song.difficulty_profiles[index].diagnostics.*member;
        }
        out << "\n";
    };
    write_diagnostic_values("profile_joint_strain_p95", &DifficultyProfileDiagnostics::joint_strain_p95);
    write_diagnostic_values("profile_joint_strain_peak", &DifficultyProfileDiagnostics::joint_strain_peak);
    write_diagnostic_values("profile_quarter_stream_duration", &DifficultyProfileDiagnostics::maximum_quarter_second_stream_duration);
    write_diagnostic_counts("profile_quarter_stream_actions", &DifficultyProfileDiagnostics::maximum_quarter_second_stream_actions);
    write_diagnostic_values("profile_half_stream_duration", &DifficultyProfileDiagnostics::maximum_half_second_stream_duration);
    write_diagnostic_counts("profile_half_stream_actions", &DifficultyProfileDiagnostics::maximum_half_second_stream_actions);
    write_diagnostic_counts("profile_jack_run", &DifficultyProfileDiagnostics::maximum_jack_run);
    write_diagnostic_counts("profile_reversal_run", &DifficultyProfileDiagnostics::maximum_reversal_run);
    write_diagnostic_values("profile_rapid_movement_p90", &DifficultyProfileDiagnostics::rapid_movement_p90);
    write_diagnostic_values("profile_rapid_movement_maximum", &DifficultyProfileDiagnostics::rapid_movement_maximum);
    write_diagnostic_values("profile_octave_movement_rate", &DifficultyProfileDiagnostics::octave_movement_rate);
    write_diagnostic_counts("profile_octave_movements_5s", &DifficultyProfileDiagnostics::maximum_octave_movements_in_five_seconds);
    write_diagnostic_counts("profile_large_reversals_5s", &DifficultyProfileDiagnostics::maximum_large_reversals_in_five_seconds);
    write_diagnostic_values("profile_right_fatigue_peak", &DifficultyProfileDiagnostics::right_fatigue_peak);
    write_diagnostic_values("profile_left_fatigue_peak", &DifficultyProfileDiagnostics::left_fatigue_peak);
    write_diagnostic_values("profile_hand_imbalance", &DifficultyProfileDiagnostics::hand_imbalance);
    write_diagnostic_values("profile_rhythm_irregularity_p90", &DifficultyProfileDiagnostics::rhythm_irregularity_p90);
    write_diagnostic_values("profile_rhythm_irregularity_maximum", &DifficultyProfileDiagnostics::rhythm_irregularity_maximum);
    write_diagnostic_values("profile_hardest_window_begin_seconds", &DifficultyProfileDiagnostics::hardest_window_begin_seconds);
    write_diagnostic_values("profile_hardest_window_end_seconds", &DifficultyProfileDiagnostics::hardest_window_end_seconds);
    write_diagnostic_values("profile_overlap_ratio", &DifficultyProfileDiagnostics::overlap_ratio);
    out << "profile_window_actions=";
    for (size_t profile_index = 0; profile_index < song.difficulty_profiles.size(); ++profile_index) {
        if (profile_index > 0) out << ";";
        const auto& values = song.difficulty_profiles[profile_index].diagnostics.maximum_window_actions;
        for (size_t index = 0; index < values.size(); ++index) {
            if (index > 0) out << ",";
            out << values[index];
        }
    }
    out << "\n";
    out << "profile_window_begin_seconds=";
    for (size_t profile_index = 0; profile_index < song.difficulty_profiles.size(); ++profile_index) {
        if (profile_index > 0) out << ";";
        const auto& values = song.difficulty_profiles[profile_index].diagnostics.maximum_window_begin_seconds;
        for (size_t index = 0; index < values.size(); ++index) {
            if (index > 0) out << ",";
            out << values[index];
        }
    }
    out << "\n";
    out << "profile_route_and_dominant_skill=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        const auto& diagnostics = song.difficulty_profiles[index].diagnostics;
        out << diagnostics.satisfied_route << ":" << diagnostics.satisfied_route_name << ":"
            << diagnostics.satisfied_route_ratio << "/" << diagnostics.satisfied_route_margin << ":"
            << diagnostics.dominant_skill << ":"
            << (diagnostics.dominant_skill_is_global ? "global" : "local") << ":"
            << diagnostics.hardest_window_begin_seconds << "-" << diagnostics.hardest_window_end_seconds;
    }
    out << "\n";
    out << "profile_omission_witnesses=";
    for (size_t index = 0; index < song.difficulty_profile_omissions.size(); ++index) {
        if (index > 0) out << ";";
        const auto& omission = song.difficulty_profile_omissions[index];
        const auto& diagnostics = omission.diagnostics;
        out << omission.difficulty << ":rows=" << diagnostics.scheduled_rows
            << ":route=" << diagnostics.satisfied_route_name
            << ":ratio=" << diagnostics.satisfied_route_ratio << "/" << diagnostics.satisfied_route_margin
            << ":skill=" << diagnostics.dominant_skill
            << ":scope=" << (diagnostics.dominant_skill_is_global ? "global" : "local")
            << ":interval=" << diagnostics.hardest_window_begin_seconds << "-"
            << diagnostics.hardest_window_end_seconds << ":windows=";
        for (size_t window = 0; window < diagnostics.maximum_window_actions.size(); ++window) {
            if (window > 0) out << ",";
            out << diagnostics.maximum_window_actions[window];
        }
        out << ":streams=" << diagnostics.maximum_quarter_second_stream_actions << "/"
            << diagnostics.maximum_quarter_second_stream_duration << ","
            << diagnostics.maximum_half_second_stream_actions << "/"
            << diagnostics.maximum_half_second_stream_duration
            << ":movement=" << diagnostics.rapid_movement_p90 << "/"
            << diagnostics.rapid_movement_maximum
            << ":octaves=" << diagnostics.octave_movement_rate << "/"
            << diagnostics.maximum_octave_movements_in_five_seconds
            << ":fatigue=" << diagnostics.right_fatigue_peak << "/"
            << diagnostics.left_fatigue_peak
            << ":balance=" << diagnostics.hand_imbalance
            << ":rhythm=" << diagnostics.rhythm_irregularity_p90 << "/"
            << diagnostics.rhythm_irregularity_maximum;
    }
    out << "\n";
    out << "profile_omission_witness_rows=";
    for (size_t index = 0; index < song.difficulty_profile_omissions.size(); ++index) {
        if (index > 0) out << ";";
        const auto& omission = song.difficulty_profile_omissions[index];
        out << omission.difficulty << ":";
        for (size_t row = 0; row < omission.witness_notes.size(); ++row) {
            if (row > 0) out << ",";
            const Note& note = omission.witness_notes[row];
            out << note.beat << "/" << note.duration_beats << "/" << note.pitch << "/" << note.chord_id;
        }
    }
    out << "\n";
    out << "profile_exposure=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ";";
        const auto& diagnostics = song.difficulty_profiles[index].diagnostics;
        out << diagnostics.exposure_decision << ":" << diagnostics.exposure_reason;
    }
    out << "\n";
    out << "profile_nested_from_previous=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << static_cast<int>(song.difficulty_profiles[index].diagnostics.nested_from_previous);
    }
    out << "\n";
    out << "profile_omissions=";
    for (size_t index = 0; index < song.difficulty_profile_omissions.size(); ++index) {
        if (index > 0) out << ";";
        const DifficultyProfileOmission& omission = song.difficulty_profile_omissions[index];
        out << omission.difficulty << ":" << omission.desired_rows << ":" << omission.reason;
    }
    out << "\n";
    out << "profile_omission_semantic_hashes=";
    for (size_t index = 0; index < song.difficulty_profile_omissions.size(); ++index) {
        if (index > 0) out << ",";
        out << hex64(omission_semantic_hash(song.difficulty_profile_omissions[index]));
    }
    out << "\n";
    out << "profile_bpm=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        out << song.difficulty_profiles[index].config.bpm;
    }
    out << "\n";
    out << "profile_scores=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ";";
        const auto& values = song.difficulty_profiles[index].config.score_thresholds;
        for (size_t value_index = 0; value_index < values.size(); ++value_index) {
            if (value_index > 0) out << ",";
            out << values[value_index];
        }
    }
    out << "\n";
    out << "profile_combos=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ";";
        const auto& values = song.difficulty_profiles[index].config.mode_change_combo_counts;
        for (size_t value_index = 0; value_index < values.size(); ++value_index) {
            if (value_index > 0) out << ",";
            out << values[value_index];
        }
    }
    out << "\n";
    out << "midi_audio_alignment_seconds=" << song.config.midi_audio_alignment_seconds << "\n";
    out << "midi_audio_offset_seconds=" << song.config.midi_audio_offset_seconds << "\n";
    out << "midi_alignment_source=" << (!song.chart_from_midi ? "json" :
        (source_config.midi_audio_alignment_provided ? "manual" : "automatic")) << "\n";
    out << "midi_offset_source=" << (!song.chart_from_midi ? "json" :
        (source_config.midi_audio_offset_provided ? "manual" : "derived")) << "\n";
    out << "midi_minimum_lead_in_seconds=" << song.config.midi_minimum_lead_in_seconds << "\n";
    out << "profile_first_prompt_seconds=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        const auto& profile = song.difficulty_profiles[index];
        if (profile.config.notes.empty() || profile.config.bpm <= 0.0) {
            out << -1;
        } else {
            out << profile.config.notes.front().beat * 60.0 / profile.config.bpm;
        }
    }
    out << "\n";
    out << "profile_last_prompt_seconds=";
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        const auto& profile = song.difficulty_profiles[index];
        if (profile.config.notes.empty() || profile.config.bpm <= 0.0) {
            out << -1;
        } else {
            out << profile.config.notes.back().beat * 60.0 / profile.config.bpm;
        }
    }
    out << "\n";
    out << "profile_trailing_audio_seconds=";
    const double audio_duration = song.audio.source_duration_seconds();
    for (size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (index > 0) out << ",";
        const auto& profile = song.difficulty_profiles[index];
        if (profile.config.notes.empty() || profile.config.bpm <= 0.0) {
            out << -1;
        } else {
            const double last_prompt = profile.config.notes.back().beat * 60.0 / profile.config.bpm;
            out << std::max(0.0, audio_duration - last_prompt);
        }
    }
    out << "\n";
    out << "midi_alignment_confidence=" << song.midi_alignment_confidence << "\n";
    out << "loudness_normalization=" << (song.config.loudness_normalization ? 1 : 0) << "\n";
    out << "loudness_target_lufs=" << song.config.loudness_target_lufs << "\n";
    out << "loudness_peak_ceiling_dbfs=" << song.config.loudness_peak_ceiling_dbfs << "\n";
    out << "loudness_input_lufs=" << song.loudness_input_lufs << "\n";
    out << "loudness_output_lufs=" << song.loudness_output_lufs << "\n";
    out << "loudness_input_peak_dbfs=" << song.loudness_input_peak_dbfs << "\n";
    out << "loudness_output_peak_dbfs=" << song.loudness_output_peak_dbfs << "\n";
    out << "loudness_applied_gain_db=" << song.loudness_applied_gain_db << "\n";
    out << "gain_envelope_present=" << (song.gain_envelope_applied ? 1 : 0) << "\n";
    out << "gain_envelope_points=" << song.gain_envelope_point_count << "\n";
    out << "gain_envelope_max_gain_db=" << song.gain_envelope_max_gain_db << "\n";
    out << "gain_envelope_min_gain_db=" << song.gain_envelope_min_gain_db << "\n";
    out << "gain_envelope_interpolation=linear_amplitude\n";
    out << "gain_envelope_boundaries=hold_first_then_last\n";
    out << "gain_envelope=";
    for (size_t index = 0; index < song.config.gain_envelope.size(); ++index) {
        if (index > 0) out << ",";
        out << song.config.gain_envelope[index].time_seconds << "/"
            << song.config.gain_envelope[index].gain_db;
    }
    out << "\n";
    out << "metronome_enabled=" << (song.config.metronome_enabled ? 1 : 0) << "\n";
    out << "metronome_adaptive_mode_mapping=mode0_strong_guide,mode1_weak_guide,mode2_clean\n";
    out << "metronome_audio_baked=" << (song.config.metronome_enabled ? 1 : 0) << "\n";
    out << "metronome_level=" << song.config.metronome_level << "\n";
    out << "metronome_beat_zero_offset_seconds=" << song.config.metronome_beat_zero_offset_seconds << "\n";
    out << "metronome_beats=" << song.metronome_beat_count << "\n";
    out << "metronome_downbeats=" << song.metronome_downbeat_count << "\n";
    out << "metronome_first_beat_seconds=" << song.metronome_first_beat_seconds << "\n";
    out << "metronome_last_beat_seconds=" << song.metronome_last_beat_seconds << "\n";
    out << "source_frames=" << song.audio.source_frame_count << "\n";
    out << "logical_pcm_frames=" << song.audio.source_frame_count << "\n";
    out << "resident_pcm_frames=" << song.audio.source_frame_count << "\n";
    out << "hca_frames=" << song.mabf_metadata.hca_frame_count << "\n";
    out << "hca_inserted_samples=" << song.mabf_metadata.inserted_samples << "\n";
    out << "hca_appended_samples=" << song.mabf_metadata.appended_samples << "\n";
    out << "loudness_gain_applied=" << (song.loudness_gain_applied ? 1 : 0) << "\n";
    out << "loudness_limiter_engaged=" << (song.loudness_limiter_engaged ? 1 : 0) << "\n";
    out << "source_seconds=" << song.audio.source_duration_seconds() << "\n";
    out << "sidecar=" << std::filesystem::path(song.cache_sidecar_path).generic_string() << "\n";
    out << "audio_source=" << std::filesystem::path(song.audio_source_path).filename().string() << "\n";
    out << "chart_source=" << (song.chart_from_midi ? "midi" : "json") << "\n";
    out << "midi_source=";
    if (song.chart_from_midi) out << std::filesystem::path(song.midi_source_path).filename().string();
    out << "\n";
    if (!out) return Status::error(StatusCode::IoError, "failed to render cache manifest");
    *manifest = out.str();
    return Status::ok_status();
}


} // namespace ff7rp::pipeline
