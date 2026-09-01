#include "pipeline/song_repository.h"
#include "pipeline/cache.h"
#include "tools/song_cache_tool_args.h"

#include "MidiFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: song_cache_tool <song-directory> [dump-difficulty]\n";
        return 2;
    }
    std::optional<int> dump_difficulty;
    if (argc == 3) {
        int parsed = 0;
        std::string error;
        if (!ff7rp::tools::parse_dump_difficulty(argv[2], &parsed, &error)) {
            std::cerr << "invalid dump-difficulty '" << argv[2] << "': " << error << '\n';
            return 2;
        }
        dump_difficulty = parsed;
    }

    ff7rp::pipeline::LoadedSong song;
    const ff7rp::pipeline::Status status = ff7rp::pipeline::load_song_directory(argv[1], &song);
    if (!status.ok()) {
        std::cerr << status.message << '\n';
        return 1;
    }
    if (song.difficulty_profiles.empty()) {
        std::cerr << "loaded song has no published difficulty profiles\n";
        return 3;
    }
    if (dump_difficulty) {
        const auto profile = std::find_if(song.difficulty_profiles.begin(), song.difficulty_profiles.end(),
            [&](const auto& value) { return value.config.difficulty == *dump_difficulty; });
        if (profile == song.difficulty_profiles.end()) {
            const auto omission = std::find_if(
                song.difficulty_profile_omissions.begin(), song.difficulty_profile_omissions.end(),
                [&](const auto& value) { return value.difficulty == *dump_difficulty; });
            std::cerr << "requested difficulty " << *dump_difficulty;
            if (omission != song.difficulty_profile_omissions.end()) {
                std::cerr << " was omitted: " << omission->reason;
            } else {
                std::cerr << " is not available; published difficulties=";
                for (const auto& value : song.difficulty_profiles) {
                    std::cerr << value.config.difficulty << ',';
                }
            }
            std::cerr << '\n';
            return 4;
        }
    }

    std::string runtime_magic;
    std::uint32_t runtime_format = 0;
    {
        std::array<unsigned char, 12> header{};
        std::ifstream runtime_cache(
            std::filesystem::path(song.directory) / ".cache" / "runtime.bin", std::ios::binary);
        runtime_cache.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (runtime_cache.gcount() == static_cast<std::streamsize>(header.size())) {
            runtime_magic.assign(header.begin(), header.begin() + 8);
            runtime_format = static_cast<std::uint32_t>(header[8])
                | (static_cast<std::uint32_t>(header[9]) << 8u)
                | (static_cast<std::uint32_t>(header[10]) << 16u)
                | (static_cast<std::uint32_t>(header[11]) << 24u);
        }
    }
    if (runtime_magic != "F7RPRT14" || runtime_format != 14u) {
        std::cerr << "generated runtime cache header is missing or invalid\n";
        return 5;
    }
    std::cout << "song_cache_tool ok id=" << song.id
               << " mutation=local-cache-refresh"
               << " pipeline=" << ff7rp::pipeline::kPipelineCacheVersion
               << " runtime_magic=" << runtime_magic
               << " runtime_format=" << runtime_format
               << " cache_source=" << (song.loaded_from_runtime_cache ? "runtime" : "generated")
               << " duration=" << song.audio.source_duration_seconds()
              << " notes=" << song.chart.notes.size()
              << " profiles=" << song.difficulty_profiles.size()
              << " profile_rows=";
    for (size_t i = 0; i < song.difficulty_profiles.size(); ++i) {
        if (i != 0) std::cout << ',';
        std::cout << song.difficulty_profiles[i].chart.notes.size();
    }
    std::cout << " sidecar=" << song.cache_sidecar_path << '\n';
    std::cout << "policy accepted_rows=" << song.accepted_chart_input_limit
              << " published_rows=" << song.published_chart_row_limit
              << " enabled=" << static_cast<int>(song.chart_policy_enabled)
              << " generation=" << song.chart_policy_generation
              << " identity=" << song.chart_policy_identity << '\n';
    std::cout << "gain applied=" << static_cast<int>(song.gain_envelope_applied)
              << " points=" << song.gain_envelope_point_count
              << " min_db=" << song.gain_envelope_min_gain_db
              << " max_db=" << song.gain_envelope_max_gain_db
              << " normalized=" << static_cast<int>(song.loudness_normalized)
              << " input_lufs=" << song.loudness_input_lufs
              << " output_lufs=" << song.loudness_output_lufs
              << " applied_db=" << song.loudness_applied_gain_db << '\n';
    std::cout << "metronome enabled=" << static_cast<int>(song.config.metronome_enabled)
              << " level=" << song.config.metronome_level
              << " beats=" << song.metronome_beat_count
              << " downbeats=" << song.metronome_downbeat_count
              << " first=" << song.metronome_first_beat_seconds
              << " last=" << song.metronome_last_beat_seconds << '\n';
    for (const auto& omission : song.difficulty_profile_omissions) {
        std::cout << "profile omitted difficulty=" << omission.difficulty
                  << " desired_rows=" << omission.desired_rows
                  << " reason=" << omission.reason << '\n';
    }

    const auto quantile = [](std::vector<double> values, const double fraction) {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        const double position = (values.size() - 1) * fraction;
        const std::size_t begin = static_cast<std::size_t>(position);
        const std::size_t end = std::min(begin + 1, values.size() - 1);
        return values[begin] + (values[end] - values[begin]) * (position - begin);
    };
    const auto pitch_number = [](const std::string& value) {
        if (value.size() < 3 || value[0] < 'A' || value[0] > 'G') return -1;
        const int base[] = {9, 11, 0, 2, 4, 5, 7};
        int accidental = 0;
        if (value[1] == 's') accidental = 1;
        else if (value[1] == 'b') accidental = -1;
        else if (value[1] != 'n') return -1;
        try {
            return (std::stoi(value.substr(2)) + 1) * 12 + base[value[0] - 'A'] + accidental;
        } catch (...) {
            return -1;
        }
    };

    for (const auto& profile : song.difficulty_profiles) {
        if (profile.chart.notes.empty()) {
            std::cerr << "published difficulty " << profile.config.difficulty << " has no chart rows\n";
            return 3;
        }
        const ff7rp::pipeline::ChartNote* last_pitch = nullptr;
        const ff7rp::pipeline::ChartNote* last_chord = nullptr;
        for (auto note = profile.chart.notes.rbegin(); note != profile.chart.notes.rend(); ++note) {
            if (!last_pitch && !note->monotone_id.empty()) last_pitch = &*note;
            if (!last_chord && !note->chord_id.empty()) last_chord = &*note;
            if (last_pitch && last_chord) break;
        }
        const auto seconds = [&](const ff7rp::pipeline::ChartNote* note) {
            return note && profile.config.bpm > 0.0 ? note->beat * 60.0 / profile.config.bpm : -1.0;
        };
        const auto& last = profile.chart.notes.back();
        double maximum_gap = 0.0;
        double gap_begin = -1.0;
        double gap_end = -1.0;
        std::vector<double> times;
        std::vector<double> gaps;
        std::vector<double> movements;
        std::size_t hand_actions = 0;
        std::size_t dual_rows = 0;
        std::size_t chord_actions = 0;
        std::size_t chord_changes = 0;
        std::size_t octave_moves = 0;
        std::string previous_chord;
        int previous_pitch = -1;
        for (const auto& note : profile.chart.notes) {
            times.push_back(seconds(&note));
            const bool has_pitch = !note.monotone_id.empty();
            const bool has_chord = !note.chord_id.empty();
            hand_actions += static_cast<std::size_t>(has_pitch) + static_cast<std::size_t>(has_chord);
            if (has_pitch && has_chord) ++dual_rows;
            if (has_chord) {
                ++chord_actions;
                if (!previous_chord.empty() && previous_chord != note.chord_id) ++chord_changes;
                previous_chord = note.chord_id;
            }
            if (has_pitch) {
                const int pitch = pitch_number(note.monotone_id);
                if (pitch >= 0 && previous_pitch >= 0) {
                    const double movement = std::abs(pitch - previous_pitch);
                    movements.push_back(movement);
                    if (movement >= 12.0) ++octave_moves;
                }
                if (pitch >= 0) previous_pitch = pitch;
            }
        }
        for (std::size_t i = 1; i < profile.chart.notes.size(); ++i) {
            const double begin = seconds(&profile.chart.notes[i - 1]);
            const double end = seconds(&profile.chart.notes[i]);
            gaps.push_back(end - begin);
            if (end - begin > maximum_gap) {
                maximum_gap = end - begin;
                gap_begin = begin;
                gap_end = end;
            }
        }
        const double active_span = times.size() > 1 ? times.back() - times.front() : 0.0;
        const double active_minutes = active_span > 0.0 ? active_span / 60.0 : 0.0;
        const auto burst = [&](const double window) {
            std::size_t maximum_rows = 0;
            std::size_t maximum_hands = 0;
            for (std::size_t begin = 0; begin < times.size(); ++begin) {
                std::size_t rows = 0;
                std::size_t hands = 0;
                for (std::size_t end = begin;
                     end < times.size() && times[end] < times[begin] + window - 1e-9; ++end) {
                    ++rows;
                    hands += static_cast<std::size_t>(!profile.chart.notes[end].monotone_id.empty()) +
                        static_cast<std::size_t>(!profile.chart.notes[end].chord_id.empty());
                }
                maximum_rows = std::max(maximum_rows, rows);
                maximum_hands = std::max(maximum_hands, hands);
            }
            return std::pair<std::size_t, std::size_t>{maximum_rows, maximum_hands};
        };
        const auto densest_window = [&](const double window) {
            std::size_t best_begin = 0;
            std::size_t best_end = 0;
            for (std::size_t begin = 0; begin < times.size(); ++begin) {
                std::size_t end = begin;
                while (end < times.size() && times[end] < times[begin] + window - 1e-9) ++end;
                if (end - begin > best_end - best_begin) {
                    best_begin = begin;
                    best_end = end;
                }
            }
            std::string result;
            for (std::size_t index = best_begin; index < best_end; ++index) {
                if (!result.empty()) result += ',';
                result += std::to_string(times[index]) + ':';
                result += profile.chart.notes[index].monotone_id.empty()
                    ? profile.chart.notes[index].chord_id
                    : profile.chart.notes[index].monotone_id;
            }
            return result;
        };
        const auto burst_1 = burst(1.0);
        const auto burst_2 = burst(2.0);
        const auto burst_5 = burst(5.0);
        std::cout << "profile difficulty=" << profile.config.difficulty
                  << " rows=" << profile.chart.notes.size()
                  << " last=" << seconds(&last)
                  << " last_pitch=" << seconds(last_pitch)
                  << " last_chord=" << seconds(last_chord)
                  << " last_has_pitch=" << (!last.monotone_id.empty() ? 1 : 0)
                  << " last_has_chord=" << (!last.chord_id.empty() ? 1 : 0)
                  << " maximum_gap=" << maximum_gap
                  << " gap_range=" << gap_begin << '-' << gap_end
                  << " active_span=" << active_span
                  << " onset_apm=" << (active_minutes > 0.0 ? times.size() / active_minutes : 0.0)
                  << " hand_apm=" << (active_minutes > 0.0 ? hand_actions / active_minutes : 0.0)
                  << " interval_min=" << (gaps.empty() ? 0.0 : *std::min_element(gaps.begin(), gaps.end()))
                  << " interval_p10=" << quantile(gaps, 0.10)
                  << " interval_p25=" << quantile(gaps, 0.25)
                  << " interval_p50=" << quantile(gaps, 0.50)
                  << " interval_p90=" << quantile(gaps, 0.90)
                   << " burst_1s=" << burst_1.first << '/' << burst_1.second
                   << " burst_2s=" << burst_2.first << '/' << burst_2.second
                   << " burst_5s=" << burst_5.first << '/' << burst_5.second
                   << " dense_1s=" << densest_window(1.0)
                   << " dense_5s=" << densest_window(5.0)
                  << " dual_rate=" << (times.empty() ? 0.0 : static_cast<double>(dual_rows) / times.size())
                  << " chord_apm=" << (active_minutes > 0.0 ? chord_actions / active_minutes : 0.0)
                  << " chord_change_apm=" << (active_minutes > 0.0 ? chord_changes / active_minutes : 0.0)
                  << " movement_p50=" << quantile(movements, 0.50)
                  << " movement_p90=" << quantile(movements, 0.90)
                  << " octave_rate=" << (movements.empty() ? 0.0 :
                      static_cast<double>(octave_moves) / movements.size())
                    << " selected_actions=" << profile.diagnostics.selected_actions
                    << " candidate_actions=" << profile.diagnostics.candidate_actions
                    << " candidate_frames=" << profile.diagnostics.candidate_frames
                    << " protected_baseline_actions=" << profile.diagnostics.protected_baseline_actions
                    << " target_band=" << profile.diagnostics.target_minimum_rows << '-'
                        << profile.diagnostics.target_rows << '-' << profile.diagnostics.target_maximum_rows
                    << " target_exclusions=" << profile.diagnostics.target_exclusions
                    << " local_skill_rejections=" << profile.diagnostics.local_skill_rejections
                    << " overlap=" << profile.diagnostics.overlap_ratio
                    << " changes=" << profile.diagnostics.retained_actions << '/'
                        << profile.diagnostics.removed_actions << '/'
                        << profile.diagnostics.replaced_actions << '/'
                        << profile.diagnostics.added_actions
                  << " scheduled_rows=" << profile.diagnostics.scheduled_rows
                  << " scheduled_conflicts=" << profile.diagnostics.scheduled_conflicts
                  << " dropped_actions=" << profile.diagnostics.dropped_actions
                  << " lead_in_rejections=" << profile.diagnostics.lead_in_rejections
                   << " audio_duration_rejections=" << profile.diagnostics.audio_duration_rejections
                   << " strain_rejections=" << profile.diagnostics.strain_rejections
                   << " joint_strain_p95=" << profile.diagnostics.joint_strain_p95
                    << " joint_strain_peak=" << profile.diagnostics.joint_strain_peak
                    << " windows=" << profile.diagnostics.maximum_window_actions[0] << '/'
                        << profile.diagnostics.maximum_window_actions[1] << '/'
                        << profile.diagnostics.maximum_window_actions[2] << '/'
                        << profile.diagnostics.maximum_window_actions[3] << '/'
                        << profile.diagnostics.maximum_window_actions[4]
                    << " window_starts=" << profile.diagnostics.maximum_window_begin_seconds[0] << '/'
                        << profile.diagnostics.maximum_window_begin_seconds[1] << '/'
                        << profile.diagnostics.maximum_window_begin_seconds[2] << '/'
                        << profile.diagnostics.maximum_window_begin_seconds[3] << '/'
                        << profile.diagnostics.maximum_window_begin_seconds[4]
                    << " streams=" << profile.diagnostics.maximum_quarter_second_stream_actions << '/'
                        << profile.diagnostics.maximum_quarter_second_stream_begin_seconds << '-'
                        << profile.diagnostics.maximum_quarter_second_stream_actions_end_seconds << ':'
                        << profile.diagnostics.maximum_quarter_second_stream_duration << '@'
                        << profile.diagnostics.maximum_quarter_second_stream_duration_begin_seconds << '-'
                        << profile.diagnostics.maximum_quarter_second_stream_duration_end_seconds << ','
                        << profile.diagnostics.maximum_half_second_stream_actions << '/'
                        << profile.diagnostics.maximum_half_second_stream_begin_seconds << '-'
                        << profile.diagnostics.maximum_half_second_stream_actions_end_seconds << ':'
                        << profile.diagnostics.maximum_half_second_stream_duration << '@'
                        << profile.diagnostics.maximum_half_second_stream_duration_begin_seconds << '-'
                        << profile.diagnostics.maximum_half_second_stream_duration_end_seconds
                    << " jack=" << profile.diagnostics.maximum_jack_run << ':'
                        << profile.diagnostics.maximum_jack_begin_seconds
                    << " reversal=" << profile.diagnostics.maximum_reversal_run << ':'
                        << profile.diagnostics.maximum_reversal_begin_seconds
                    << " rapid_movement=" << profile.diagnostics.rapid_movement_p90 << '/'
                        << profile.diagnostics.rapid_movement_maximum << '@'
                        << profile.diagnostics.rapid_movement_maximum_seconds
                    << " octave=" << profile.diagnostics.octave_movement_rate << '/'
                        << profile.diagnostics.maximum_octave_movements_in_five_seconds << ':'
                        << profile.diagnostics.maximum_octave_window_begin_seconds
                    << " large_reversals_5s=" << profile.diagnostics.maximum_large_reversals_in_five_seconds
                        << ':' << profile.diagnostics.maximum_large_reversal_window_begin_seconds
                    << " fatigue=" << profile.diagnostics.right_fatigue_peak << '/'
                        << profile.diagnostics.left_fatigue_peak
                    << " imbalance=" << profile.diagnostics.hand_imbalance
                    << " rhythm_irregularity=" << profile.diagnostics.rhythm_irregularity_p90 << '/'
                        << profile.diagnostics.rhythm_irregularity_maximum
                    << " hardest=" << (profile.diagnostics.dominant_skill_is_global ? "global" :
                        std::to_string(profile.diagnostics.hardest_window_begin_seconds) + "-" +
                        std::to_string(profile.diagnostics.hardest_window_end_seconds)) << ':'
                        << profile.diagnostics.dominant_skill
                    << " route=" << profile.diagnostics.satisfied_route << ':'
                        << profile.diagnostics.satisfied_route_name << ':'
                        << profile.diagnostics.satisfied_route_ratio << '/'
                        << profile.diagnostics.satisfied_route_margin
                    << " exposure=" << profile.diagnostics.exposure_decision << ':'
                        << profile.diagnostics.exposure_reason
                   << " complete=" << static_cast<int>(profile.diagnostics.complete)
                   << " row_limit_exceeded=" << static_cast<int>(profile.diagnostics.row_limit_exceeded)
                   << " nested_from_previous=" << static_cast<int>(profile.diagnostics.nested_from_previous)
                   << " diagnostic_tail=" << profile.diagnostic_chart.source_row_count << '/'
                       << profile.diagnostic_chart.native_prefix_row_count << '/'
                       << profile.diagnostic_chart.tail_rows.size() << '/'
                       << profile.diagnostic_chart.descriptor_hash
                   << " tail=";
        for (const auto& note : profile.chart.notes) {
            const double time = seconds(&note);
            if (time >= 130.0) {
                std::cout << time << (note.monotone_id.empty() ? 'L' : 'R') << ',';
            }
        }
        std::cout
                   << '\n';
        if (dump_difficulty && profile.config.difficulty == *dump_difficulty) {
            std::cout << "profile_events difficulty=" << *dump_difficulty << ' ';
            for (const auto& note : profile.chart.notes) {
                std::cout << seconds(&note) << ':'
                          << (note.monotone_id.empty() ? note.chord_id : note.monotone_id) << ',';
            }
            std::cout << '\n';
        }
    }

    if (!song.midi_source_path.empty()) {
        smf::MidiFile midi;
        if (midi.read(song.midi_source_path) && midi.status()) {
            midi.makeAbsoluteTicks();
            midi.doTimeAnalysis();
            midi.linkNotePairs();
            struct TailNote {
                int pitch = 0;
                double end = 0.0;
            };
            std::map<long long, std::vector<TailNote>> tail_onsets;
            double final_midi_end = 0.0;
            for (int track = 0; track < midi.getTrackCount(); ++track) {
                for (int index = 0; index < midi.getEventCount(track); ++index) {
                    const smf::MidiEvent& event = midi[track][index];
                    if (!event.isNoteOn() || event.getChannel() == 9 || !event.isLinked()
                        || event.getKeyNumber() < 24 || event.getKeyNumber() > 96
                        || !std::isfinite(event.seconds) || event.seconds < 130.0) {
                        continue;
                    }
                    const double end = event.seconds + event.getDurationInSeconds();
                    tail_onsets[static_cast<long long>(std::llround(event.seconds * 1000.0))]
                        .push_back({event.getKeyNumber(), end});
                    final_midi_end = std::max(final_midi_end, end);
                }
            }
            std::cout << "midi_tail_onsets final_end=" << final_midi_end << ' ';
            for (const auto& [milliseconds, notes] : tail_onsets) {
                std::cout << static_cast<double>(milliseconds) / 1000.0 << '[';
                for (const TailNote& note : notes) std::cout << note.pitch << ':' << note.end << ',';
                std::cout << "],";
            }
            std::cout << '\n';
        }
    }
    return 0;
}
