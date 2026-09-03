#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "pipeline/midi_chart_generator.h"
#include "pipeline/midi_chart_compilation.h"
#include "pipeline/midi_source_normalizer.h"
#include "pipeline/chart_compiler.h"
#include "pipeline/chart_event_plan.h"
#include "pipeline/pipeline_limits.h"
#include "tests/test_support.h"

namespace {

using ff7rp::pipeline::MidiChartStats;
using ff7rp::pipeline::MidiLocalSkillMetrics;
using ff7rp::pipeline::Note;
using ff7rp::pipeline::SongConfig;
using ff7rp::pipeline::Status;
using ff7rp::pipeline::WavAudio;

struct MidiEvent {
    int tick = 0;
    int priority = 0;
    std::vector<unsigned char> bytes;
};

void append_u16(std::vector<unsigned char>* bytes, const int value) {
    bytes->push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    bytes->push_back(static_cast<unsigned char>(value & 0xff));
}

void append_u32(std::vector<unsigned char>* bytes, const std::size_t value) {
    bytes->push_back(static_cast<unsigned char>((value >> 24u) & 0xffu));
    bytes->push_back(static_cast<unsigned char>((value >> 16u) & 0xffu));
    bytes->push_back(static_cast<unsigned char>((value >> 8u) & 0xffu));
    bytes->push_back(static_cast<unsigned char>(value & 0xffu));
}

void append_variable(std::vector<unsigned char>* bytes, int value) {
    std::array<unsigned char, 4> encoded{};
    int count = 1;
    encoded[3] = static_cast<unsigned char>(value & 0x7f);
    while ((value >>= 7) != 0) encoded[3 - count++] = static_cast<unsigned char>((value & 0x7f) | 0x80);
    bytes->insert(bytes->end(), encoded.end() - count, encoded.end());
}

void add_note(std::vector<MidiEvent>* track, const int tick, const int duration,
              const int pitch, const int velocity, const int channel = 0) {
    track->push_back({tick, 2, {static_cast<unsigned char>(0x90 | channel),
        static_cast<unsigned char>(pitch), static_cast<unsigned char>(velocity)}});
    track->push_back({tick + duration, 1, {static_cast<unsigned char>(0x80 | channel),
        static_cast<unsigned char>(pitch), 0}});
}

void append_midi_track(std::vector<unsigned char>* file, std::vector<MidiEvent> events) {
    std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.priority < b.priority;
    });
    std::vector<unsigned char> data;
    int previous_tick = 0;
    for (const MidiEvent& event : events) {
        append_variable(&data, event.tick - previous_tick);
        data.insert(data.end(), event.bytes.begin(), event.bytes.end());
        previous_tick = event.tick;
    }
    append_variable(&data, 0);
    data.insert(data.end(), {0xff, 0x2f, 0x00});
    file->insert(file->end(), {'M', 'T', 'r', 'k'});
    append_u32(file, data.size());
    file->insert(file->end(), data.begin(), data.end());
}

std::vector<unsigned char> oracle_midi_bytes() {
    std::vector<MidiEvent> melody;
    std::vector<MidiEvent> harmony;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    melody.push_back({5760, 0, {0xff, 0x51, 0x03, 0x09, 0x27, 0xc0}});
    melody.push_back({5760, 0, {0xff, 0x58, 0x04, 0x03, 0x02, 24, 8}});
    for (int index = 0; index < 48; ++index) {
        const int tick = index * 240 + (index % 7 == 3 ? 9 : 0);
        add_note(&melody, tick, 180 + index % 4 * 30, 72 + (index * 5) % 12,
            86 + index % 5 * 8);
        if (index == 10) add_note(&melody, tick + 32, 180, 73, 101);
        if (index % 4 == 0) {
            const int root = index % 8 == 0 ? 48 : 53;
            add_note(&harmony, tick + (index % 8 == 0 ? 0 : 6), 360, root, 82);
            add_note(&harmony, tick + (index % 8 == 0 ? 0 : 6), 360, root + 4, 80);
            add_note(&harmony, tick + (index % 8 == 0 ? 0 : 6), 360, root + 7, 78);
        }
    }
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 1);
    append_u16(&file, 2);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(melody));
    append_midi_track(&file, std::move(harmony));
    return file;
}

std::vector<unsigned char> profile_witness_midi_bytes() {
    std::vector<MidiEvent> melody;
    std::vector<MidiEvent> harmony;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    for (int index = 0; index < 480; ++index) {
        const int tick = 1920 + index * 240;
        add_note(&melody, tick, 180, 48 + (index % 2) * 36, 88 + index % 24);
        if (index % 4 == 0) {
            const int root = 43 + (index / 4) % 12;
            add_note(&harmony, tick, 360, root, 82);
            add_note(&harmony, tick, 360, root + 7, 78);
        }
    }
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 1);
    append_u16(&file, 2);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(melody));
    append_midi_track(&file, std::move(harmony));
    return file;
}

std::vector<unsigned char> single_chord_midi_bytes(const std::initializer_list<int> pitches) {
    std::vector<MidiEvent> melody;
    std::vector<MidiEvent> harmony;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    for (int event = 0; event < 8; ++event) {
        const int tick = event * 960;
        add_note(&melody, tick, 360, 72 + event % 5, 104);
        int velocity = 82;
        for (const int pitch : pitches) add_note(&harmony, tick, 720, pitch, velocity--);
    }

    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 1);
    append_u16(&file, 2);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(melody));
    append_midi_track(&file, std::move(harmony));
    return file;
}

std::vector<unsigned char> melody_fixture_midi_bytes(
    const std::vector<int>& pitches, const int spacing_ticks) {
    std::vector<MidiEvent> melody;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    for (std::size_t index = 0; index < pitches.size(); ++index) {
        add_note(&melody, 1920 + static_cast<int>(index) * spacing_ticks,
            std::min(180, spacing_ticks - 1), pitches[index], 100);
    }
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 0);
    append_u16(&file, 1);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(melody));
    return file;
}

std::vector<unsigned char> profile_group_fixture_midi_bytes(const int fast_spacing_ticks) {
    std::vector<MidiEvent> melody;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    for (int index = 0; index < 12; ++index) {
        add_note(&melody, 1920 + index * 960, 240, 67 + index % 5, 84);
    }
    const std::vector<int> fast_pitches = fast_spacing_ticks == 105 ?
        std::vector<int>{60, 62} : std::vector<int>{60, 62, 64, 65};
    for (std::size_t index = 0; index < fast_pitches.size(); ++index) {
        add_note(&melody, 14420 + static_cast<int>(index) * fast_spacing_ticks,
            std::min(90, fast_spacing_ticks - 1), fast_pitches[index], 112);
    }
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 0);
    append_u16(&file, 1);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(melody));
    return file;
}

class Fingerprint {
public:
    void integer(const std::uint64_t value) {
        for (unsigned int shift = 0; shift < 64; shift += 8) byte(static_cast<unsigned char>(value >> shift));
    }
    void number(const double value) { integer(std::bit_cast<std::uint64_t>(value)); }
    void text(const std::string_view value) {
        integer(value.size());
        for (const unsigned char character : value) byte(character);
    }
    std::uint64_t value() const { return value_; }
private:
    void byte(const unsigned char value) {
        value_ ^= value;
        value_ *= 1099511628211ull;
    }
    std::uint64_t value_ = 1469598103934665603ull;
};

std::vector<std::uint8_t> canonical_note_bytes(const std::vector<Note>& notes) {
    std::vector<std::uint8_t> bytes;
    const auto append_u64 = [&](const std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    };
    const auto append_string = [&](const std::string& value) {
        append_u64(value.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
    };
    append_u64(notes.size());
    for (const Note& note : notes) {
        append_u64(std::bit_cast<std::uint64_t>(note.beat));
        append_u64(std::bit_cast<std::uint64_t>(note.duration_beats));
        append_string(note.pitch);
        append_string(note.chord_id);
        bytes.push_back(note.group_index);
        bytes.push_back(note.alternate_monotone ? 1u : 0u);
        append_u64(note.ignore_sound_pitches.size());
        for (const std::string& value : note.ignore_sound_pitches) append_string(value);
        append_u64(note.source_chord_pitches.size());
        for (const std::string& value : note.source_chord_pitches) append_string(value);
    }
    return bytes;
}

void append_local_skills(Fingerprint* out, const MidiLocalSkillMetrics& value) {
    for (const auto item : value.maximum_window_actions) out->integer(item);
    for (const auto item : value.maximum_window_begin_seconds) out->number(item);
    out->integer(value.maximum_quarter_second_stream_actions);
    out->number(value.maximum_quarter_second_stream_duration);
    out->number(value.maximum_quarter_second_stream_begin_seconds);
    out->number(value.maximum_quarter_second_stream_actions_end_seconds);
    out->number(value.maximum_quarter_second_stream_duration_begin_seconds);
    out->number(value.maximum_quarter_second_stream_duration_end_seconds);
    out->integer(value.maximum_half_second_stream_actions);
    out->number(value.maximum_half_second_stream_duration);
    out->number(value.maximum_half_second_stream_begin_seconds);
    out->number(value.maximum_half_second_stream_actions_end_seconds);
    out->number(value.maximum_half_second_stream_duration_begin_seconds);
    out->number(value.maximum_half_second_stream_duration_end_seconds);
    out->integer(value.maximum_jack_run);
    out->number(value.maximum_jack_begin_seconds);
    out->integer(value.maximum_reversal_run);
    out->number(value.maximum_reversal_begin_seconds);
    out->number(value.rapid_movement_p90);
    out->number(value.rapid_movement_maximum);
    out->number(value.rapid_movement_maximum_seconds);
    out->number(value.octave_movement_rate);
    out->integer(value.maximum_octave_movements_in_five_seconds);
    out->number(value.maximum_octave_window_begin_seconds);
    out->integer(value.maximum_large_reversals_in_five_seconds);
    out->number(value.maximum_large_reversal_window_begin_seconds);
    out->number(value.right_fatigue_peak);
    out->number(value.right_fatigue_peak_seconds);
    out->number(value.left_fatigue_peak);
    out->number(value.left_fatigue_peak_seconds);
    out->number(value.hand_imbalance);
    out->number(value.rhythm_irregularity_p90);
    out->number(value.rhythm_irregularity_maximum);
    out->number(value.rhythm_irregularity_peak_seconds);
    out->number(value.hardest_window_begin_seconds);
    out->number(value.hardest_window_end_seconds);
    out->integer(static_cast<std::uint64_t>(value.dominant_skill));
    out->integer(static_cast<std::uint64_t>(value.satisfied_route));
    out->number(value.satisfied_route_ratio);
    out->number(value.satisfied_route_margin);
    out->integer(value.dominant_skill_is_global);
    out->text(value.satisfied_route_name);
}

void append_stats(Fingerprint* out, const MidiChartStats& value) {
#define APPEND_INTEGER(field) out->integer(value.field)
#define APPEND_NUMBER(field) out->number(value.field)
    APPEND_INTEGER(source_tracks); APPEND_INTEGER(source_events); APPEND_INTEGER(melody_tracks);
    APPEND_INTEGER(harmony_tracks); APPEND_INTEGER(melody_onsets); APPEND_INTEGER(exact_tick_groups);
    APPEND_INTEGER(humanized_clusters); APPEND_INTEGER(humanized_events); APPEND_INTEGER(melody_candidates);
    APPEND_INTEGER(fallback_candidates); APPEND_INTEGER(chord_candidates); APPEND_INTEGER(right_events);
    APPEND_INTEGER(left_events); APPEND_INTEGER(fallback_events); APPEND_INTEGER(merged_events);
    APPEND_INTEGER(evidence_rejections); APPEND_INTEGER(cooldown_rejections); APPEND_INTEGER(burst_rejections);
    APPEND_INTEGER(strain_rejections); APPEND_INTEGER(retention_rejections); APPEND_INTEGER(action_rate_rejections);
    APPEND_INTEGER(right_collisions); APPEND_INTEGER(left_collisions); APPEND_INTEGER(cross_hand_conflicts);
    APPEND_INTEGER(scheduled_conflicts); APPEND_INTEGER(dropped_conflicts); APPEND_INTEGER(selected_actions);
    APPEND_INTEGER(candidate_actions); APPEND_INTEGER(candidate_frames); APPEND_INTEGER(protected_baseline_actions);
    APPEND_INTEGER(target_rows); APPEND_INTEGER(target_minimum_rows); APPEND_INTEGER(target_maximum_rows);
    APPEND_INTEGER(target_exclusions); APPEND_INTEGER(local_skill_rejections); APPEND_INTEGER(retained_actions);
    APPEND_INTEGER(removed_actions); APPEND_INTEGER(replaced_actions); APPEND_INTEGER(added_actions);
    APPEND_INTEGER(desired_rows); APPEND_INTEGER(lead_in_rejections); APPEND_INTEGER(audio_duration_rejections);
    APPEND_INTEGER(row_limit_exceeded); APPEND_INTEGER(voice_stream_changes); APPEND_INTEGER(time_signature_changes);
    APPEND_INTEGER(metric_downbeat_candidates); APPEND_INTEGER(metric_downbeat_right_events);
    APPEND_INTEGER(source_pitch_witness_failures); APPEND_INTEGER(octave_fixes);
    APPEND_NUMBER(source_bpm); APPEND_NUMBER(selected_retention); APPEND_NUMBER(actions_per_minute);
    APPEND_NUMBER(joint_strain_p95); APPEND_NUMBER(joint_strain_peak);
    append_local_skills(out, value.local_skills);
    APPEND_NUMBER(minimum_right_gap_seconds); APPEND_NUMBER(minimum_right_gap_beats);
    APPEND_NUMBER(audio_alignment_seconds); APPEND_NUMBER(audio_offset_seconds); APPEND_NUMBER(alignment_confidence);
#undef APPEND_INTEGER
#undef APPEND_NUMBER
}

struct Observation {
    Status status;
    std::vector<Note> notes;
    MidiChartStats stats;
};

std::uint64_t fingerprint(const Observation& value) {
    Fingerprint out;
    out.integer(static_cast<std::uint64_t>(value.status.code));
    out.text(value.status.message);
    out.integer(value.notes.size());
    for (const Note& note : value.notes) {
        out.number(note.beat);
        out.number(note.duration_beats);
        out.text(note.pitch);
        out.text(note.chord_id);
        out.integer(note.group_index);
        out.integer(note.alternate_monotone ? 1u : 0u);
        out.integer(note.ignore_sound_pitches.size());
        for (const auto& pitch : note.ignore_sound_pitches) out.text(pitch);
        out.integer(note.source_chord_pitches.size());
        for (const auto& pitch : note.source_chord_pitches) out.text(pitch);
    }
    append_stats(&out, value.stats);
    return out.value();
}

Observation generate(const std::filesystem::path& path, const WavAudio& audio,
                     const SongConfig& config, const std::vector<Note>* baseline = nullptr,
                     const std::size_t maximum_visible_rows = 0) {
    Observation value;
    value.status = ff7rp::pipeline::generate_notes_from_midi(path.string(), audio, config,
        &value.notes, &value.stats, baseline, maximum_visible_rows);
    return value;
}

SongConfig config_for(const int difficulty) {
    SongConfig config;
    config.difficulty = difficulty;
    config.midi_audio_alignment_provided = true;
    config.midi_audio_alignment_seconds = -0.03125;
    config.midi_audio_offset_provided = true;
    config.midi_audio_offset_seconds = 0.0625;
    config.midi_minimum_lead_in_seconds = 0.0;
    return config;
}

struct Expected {
    const char* name;
    std::uint64_t digest;
    int status_code;
    const char* status_message;
};

constexpr std::array<Expected, 18> expected{{
    {"manual-lv1", 812876795192241011ull, 0, ""},
    {"manual-lv2", 15657760307793846569ull, 0, ""},
    {"manual-lv3", 2229671639194785098ull, 0, ""},
    {"manual-lv4", 15842221501682194707ull, 0, ""},
    {"manual-lv5", 18439861697433635978ull, 0, ""},
    {"manual-lv6", 17145848053232314335ull, 0, ""},
    {"baseline-lv1", 18251479632695410273ull, 0, ""},
    {"baseline-lv2", 7582551267464411896ull, 0, ""},
    {"baseline-lv3", 13079214097419897187ull, 0, ""},
    {"baseline-lv4", 7857690322218724421ull, 0, ""},
    {"baseline-lv5", 13756716813077710487ull, 0, ""},
    {"baseline-lv6", 16762614854703495817ull, 0, ""},
    {"automatic-alignment", 5522470204471226344ull, 0, ""},
    {"timing-domain-empty", 12345897120421995243ull, 7,
        "MIDI generation produced no chart rows inside the lead-in/audio timing domain"},
    {"baseline-witness-error", 8598688527783776924ull, 7,
        "preferred lower-profile action has no source candidate at its native frame"},
    {"visible-growth-bound", 3998143965459286508ull, 8,
        "difficulty target band exceeds the maximum adjacent visible-profile growth"},
    {"selection-failure-witness", 17110472156722411997ull, 8,
        "no target-band state satisfies a coherent local-skill route"},
    {"near-feasible-witness", 16264490516427515963ull, 8,
        "near-feasible witness: rows=294 preferred=192 required_rows=297 required_preferred=192 "
        "route=OneWingedAngel ratio=3.000000 margin=1.150000 dominant_skill=5 interval=global"},
}};

int fail(const std::string& message) {
    std::cerr << "midi_chart_compilation_selftest: " << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    ff7rp::tests::TemporaryDirectory temporary("ff7rp-midi-compilation-oracle");
    const std::filesystem::path midi_path = temporary.path() / "oracle.mid";
    const std::vector<unsigned char> bytes = oracle_midi_bytes();
    std::ofstream output(midi_path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) return fail("could not write oracle MIDI");
    const std::filesystem::path witness_path = temporary.path() / "profile-witness.mid";
    const std::vector<unsigned char> witness_bytes = profile_witness_midi_bytes();
    std::ofstream witness_output(witness_path, std::ios::binary | std::ios::trunc);
    witness_output.write(reinterpret_cast<const char*>(witness_bytes.data()),
        static_cast<std::streamsize>(witness_bytes.size()));
    witness_output.close();
    if (!witness_output) return fail("could not write synthetic profile witness MIDI");
    const auto write_chord_fixture = [&](const char* name, const std::initializer_list<int> pitches) {
        const std::filesystem::path path = temporary.path() / name;
        const std::vector<unsigned char> fixture = single_chord_midi_bytes(pitches);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(fixture.data()), static_cast<std::streamsize>(fixture.size()));
        return path;
    };
    const std::filesystem::path superset_path = write_chord_fixture("chord-superset.mid", {43, 47, 54});
    const std::filesystem::path ambiguous_path = write_chord_fixture("chord-ambiguous.mid", {48, 52, 58});
    const std::filesystem::path exact_path = write_chord_fixture("chord-exact.mid", {48, 52, 55});
    const std::filesystem::path duplicate_path = write_chord_fixture("duplicate-pitch.mid", {48, 52, 48});
    const std::filesystem::path unsupported_pitch_path =
        write_chord_fixture("unsupported-pitch.mid", {20, 48, 52});
    const auto write_melody_fixture = [&](const char* name, const std::vector<int>& pitches, const int spacing) {
        const std::filesystem::path path = temporary.path() / name;
        const std::vector<unsigned char> fixture = melody_fixture_midi_bytes(pitches, spacing);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(fixture.data()), static_cast<std::streamsize>(fixture.size()));
        return path;
    };
    const std::filesystem::path lower_context_path =
        write_melody_fixture("lower-context.mid", {57, 60, 59}, 1920);
    const std::filesystem::path upper_context_path =
        write_melody_fixture("upper-context.mid", {62, 60, 64}, 1920);
    const std::filesystem::path repeated_c_path =
        write_melody_fixture("repeated-c.mid", {60, 60, 60}, 1920);
    const auto write_group_fixture = [&](const char* name, const int spacing) {
        const std::filesystem::path path = temporary.path() / name;
        const std::vector<unsigned char> fixture = profile_group_fixture_midi_bytes(spacing);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(fixture.data()), static_cast<std::streamsize>(fixture.size()));
        return path;
    };
    const std::filesystem::path profile_group_path = write_group_fixture("profile-group.mid", 105);
    const std::filesystem::path multi_follower_path = write_group_fixture("multi-follower-group.mid", 35);

    const WavAudio no_audio;
    const auto find_pitch = [](const Observation& observation, const std::string_view pitch) {
        return std::find_if(observation.notes.begin(), observation.notes.end(), [&](const Note& note) {
            return note.pitch == pitch;
        });
    };
    const Observation lower_easy = generate(lower_context_path, no_audio, config_for(1));
    const Observation lower_hard = generate(lower_context_path, no_audio, config_for(6));
    const Observation upper_easy = generate(upper_context_path, no_audio, config_for(1));
    const Observation upper_hard = generate(upper_context_path, no_audio, config_for(6));
    if (!lower_easy.status.ok() || !lower_hard.status.ok() || !upper_easy.status.ok() || !upper_hard.status.ok() ||
        find_pitch(lower_easy, "C4") == lower_easy.notes.end() ||
        find_pitch(lower_hard, "C4") == lower_hard.notes.end() ||
        !find_pitch(lower_easy, "C4")->alternate_monotone ||
        !find_pitch(lower_hard, "C4")->alternate_monotone ||
        find_pitch(upper_easy, "C4") == upper_easy.notes.end() ||
        find_pitch(upper_hard, "C4") == upper_hard.notes.end() ||
        find_pitch(upper_easy, "C4")->alternate_monotone ||
        find_pitch(upper_hard, "C4")->alternate_monotone) {
        return fail("contour planner did not preserve upper/lower-boundary identity across profiles");
    }
    const Observation repeated_c = generate(repeated_c_path, no_audio, config_for(6));
    if (!repeated_c.status.ok() || repeated_c.notes.size() != 3u ||
        std::any_of(repeated_c.notes.begin(), repeated_c.notes.end(), [](const Note& note) {
            return note.pitch != "C4" || note.alternate_monotone;
        })) {
        return fail("contour planner did not resolve repeated eligible-note ties deterministically");
    }
    const Observation grouped_easy = generate(profile_group_path, no_audio, config_for(1));
    const Observation grouped_easy_repeat = generate(profile_group_path, no_audio, config_for(1));
    const Observation grouped_hard = generate(profile_group_path, no_audio, config_for(6));
    const Observation multi_follower = generate(multi_follower_path, no_audio, config_for(1));
    const Observation multi_follower_repeat = generate(multi_follower_path, no_audio, config_for(1));
    bool easy_group_beyond_six_frames = false;
    std::size_t largest_easy_group = 0;
    std::size_t current_easy_group = 0;
    std::uint8_t current_easy_group_id = 0;
    std::size_t easy_required_actions = 0;
    std::uint8_t previous_easy_group_id = 0;
    for (std::size_t index = 1; index < grouped_easy.notes.size(); ++index) {
        const Note& previous = grouped_easy.notes[index - 1];
        const Note& current = grouped_easy.notes[index];
        const long long previous_frame = static_cast<long long>(std::llround(previous.beat * 30.0));
        const long long current_frame = static_cast<long long>(std::llround(current.beat * 30.0));
        easy_group_beyond_six_frames = easy_group_beyond_six_frames ||
            (current.group_index != 0 && current.group_index == previous.group_index &&
                current_frame - previous_frame > 6);
    }
    for (const Note& note : multi_follower.notes) {
        if (!note.pitch.empty() && (note.group_index == 0 || note.group_index != previous_easy_group_id)) {
            ++easy_required_actions;
        }
        previous_easy_group_id = note.group_index;
        if (note.group_index != 0 && note.group_index == current_easy_group_id) {
            ++current_easy_group;
        } else {
            current_easy_group_id = note.group_index;
            current_easy_group = note.group_index == 0 ? 0u : 1u;
        }
        largest_easy_group = std::max(largest_easy_group, current_easy_group);
    }
    if (!grouped_easy.status.ok() || !grouped_hard.status.ok() ||
        canonical_note_bytes(grouped_easy.notes) != canonical_note_bytes(grouped_easy_repeat.notes) ||
        fingerprint(grouped_easy) != fingerprint(grouped_easy_repeat) || !easy_group_beyond_six_frames ||
        !multi_follower.status.ok() ||
        canonical_note_bytes(multi_follower.notes) != canonical_note_bytes(multi_follower_repeat.notes) ||
        fingerprint(multi_follower) != fingerprint(multi_follower_repeat) || largest_easy_group < 4u ||
        multi_follower.stats.selected_actions != easy_required_actions || grouped_easy.notes.size() != 14u ||
        grouped_hard.notes.size() != 14u ||
        grouped_hard.stats.selected_actions != grouped_hard.notes.size() ||
        std::any_of(grouped_hard.notes.begin(), grouped_hard.notes.end(), [](const Note& note) {
            return note.group_index != 0;
        })) {
        return fail("profile spacing did not deterministically automate only the easier fast passage: easy_rows=" +
            std::to_string(grouped_easy.notes.size()) + " easy_group=" + std::to_string(largest_easy_group) +
            " easy_actions=" + std::to_string(grouped_easy.stats.selected_actions) + " hard_rows=" +
            std::to_string(grouped_hard.notes.size()) + " hard_actions=" +
            std::to_string(grouped_hard.stats.selected_actions) + " easy_status=[" +
            grouped_easy.status.message + "] hard_status=[" + grouped_hard.status.message + "] multi_rows=" +
            std::to_string(multi_follower.notes.size()) + " multi_actions=" +
            std::to_string(multi_follower.stats.selected_actions) + " multi_required=" +
            std::to_string(easy_required_actions) + " beyond6=" +
            std::to_string(easy_group_beyond_six_frames ? 1 : 0) + " easy_repeat=" +
            std::to_string(fingerprint(grouped_easy) == fingerprint(grouped_easy_repeat) ? 1 : 0) +
            " multi_repeat=" +
            std::to_string(fingerprint(multi_follower) == fingerprint(multi_follower_repeat) ? 1 : 0));
    }
    std::vector<std::pair<std::string, Observation>> observations;
    std::array<Observation, 6> manual;
    for (int difficulty = 1; difficulty <= 6; ++difficulty) {
        manual[static_cast<std::size_t>(difficulty - 1)] =
            generate(midi_path, no_audio, config_for(difficulty));
        observations.emplace_back("manual-lv" + std::to_string(difficulty),
            manual[static_cast<std::size_t>(difficulty - 1)]);
    }
    bool observed_grouped_follower = false;
    bool observed_exact_voicing = false;
    for (const Observation& profile : manual) {
        std::uint8_t previous_group = 0;
        std::size_t required_actions = 0;
        for (const Note& note : profile.notes) {
            const bool follower = note.group_index != 0 && note.group_index == previous_group;
            observed_grouped_follower = observed_grouped_follower || follower;
            if (!note.pitch.empty() && !follower) ++required_actions;
            if (!note.chord_id.empty()) {
                ++required_actions;
                observed_exact_voicing = observed_exact_voicing || !note.source_chord_pitches.empty();
                if (!note.ignore_sound_pitches.empty()) return fail("generated IgnoreSound guessed without a verified mapping");
            }
            previous_group = note.group_index;
        }
        if (profile.status.ok() && profile.stats.selected_actions != required_actions) {
            return fail("grouped follower was counted as a required action");
        }
    }
    if (!observed_grouped_follower) return fail("fast representable right-hand follower was not grouped");
    if (!observed_exact_voicing) return fail("exact source chord voicing was not preserved");
    const Observation superset = generate(superset_path, no_audio, config_for(6));
    const Observation repeated_superset = generate(superset_path, no_audio, config_for(6));
    if (!superset.status.ok() || fingerprint(superset) != fingerprint(repeated_superset)) {
        return fail("native chord-superset generation was not successful and deterministic");
    }
    const auto unique_superset = std::find_if(superset.notes.begin(), superset.notes.end(), [](const Note& note) {
        return note.chord_id == "pca_G_Maj7";
    });
    if (unique_superset == superset.notes.end() ||
        unique_superset->ignore_sound_pitches != std::vector<std::string>{"Dn3"} ||
        unique_superset->source_chord_pitches != std::vector<std::string>{"G2", "B2", "F#3"}) {
        return fail("unique native chord superset did not derive exact mapped IgnoreSound semantics");
    }
    SongConfig compiled_superset_config = config_for(6);
    compiled_superset_config.bpm = 120.0;
    compiled_superset_config.notes_provided = true;
    compiled_superset_config.notes = superset.notes;
    ff7rp::pipeline::CompiledChart compiled_superset;
    const Status compiled_superset_status =
        ff7rp::pipeline::compile_chart(compiled_superset_config, &compiled_superset);
    if (!compiled_superset_status.ok() ||
        std::none_of(compiled_superset.notes.begin(), compiled_superset.notes.end(), [](const auto& note) {
            return note.chord_id == "pca_G_Maj7" &&
                note.ignore_sound_ids == std::array<std::string, 3>{"Dn3", "", ""};
        })) {
        return fail("generated mapped IgnoreSound semantics did not compile: " + compiled_superset_status.message);
    }
    const Observation exact = generate(exact_path, no_audio, config_for(6));
    const auto exact_match = std::find_if(exact.notes.begin(), exact.notes.end(), [](const Note& note) {
        return note.chord_id == "pca_C";
    });
    if (!exact.status.ok() || exact_match == exact.notes.end() || !exact_match->ignore_sound_pitches.empty()) {
        return fail("exact native chord matching changed when superset support was added");
    }
    const Observation ambiguous = generate(ambiguous_path, no_audio, config_for(6));
    if (!ambiguous.status.ok() || std::any_of(ambiguous.notes.begin(), ambiguous.notes.end(), [](const Note& note) {
            return note.chord_id == "pca_C_7" || note.chord_id == "pca_C_9";
        })) {
        return fail("ambiguous partial chord was guessed as a native superset");
    }
    for (std::size_t easier = 0; easier < manual.size(); ++easier) {
        for (std::size_t harder = easier + 1; harder < manual.size(); ++harder) {
            for (const Note& left : manual[easier].notes) {
                const auto match = std::find_if(manual[harder].notes.begin(), manual[harder].notes.end(),
                    [&](const Note& right) { return right.beat == left.beat && right.pitch == left.pitch; });
                if (match != manual[harder].notes.end() && match->alternate_monotone != left.alternate_monotone) {
                    return fail("alternate monotone identity changed across difficulty profiles");
                }
            }
        }
    }
    for (int difficulty = 1; difficulty <= 6; ++difficulty) {
        const Observation& source = manual[static_cast<std::size_t>(difficulty - 1)];
        observations.emplace_back("baseline-lv" + std::to_string(difficulty),
            generate(midi_path, no_audio, config_for(difficulty), &source.notes));
    }
    WavAudio audio;
    audio.sample_rate = 48000;
    audio.channels = 2;
    audio.source_frame_count = 700000;
    audio.stereo_samples.assign(audio.source_frame_count * 2, 0.0f);
    for (int index = 0; index < 24; ++index) {
        const std::size_t frame = static_cast<std::size_t>((0.125 + index * 0.25) * audio.sample_rate);
        for (std::size_t offset = 0; offset < 1200; ++offset) {
            const float sample = static_cast<float>((1200 - offset) / 1200.0 * 0.2);
            audio.stereo_samples[(frame + offset) * 2] = sample;
            audio.stereo_samples[(frame + offset) * 2 + 1] = sample;
        }
    }
    SongConfig automatic = config_for(4);
    automatic.midi_audio_alignment_provided = false;
    automatic.midi_audio_offset_provided = false;
    observations.emplace_back("automatic-alignment", generate(midi_path, audio, automatic));
    SongConfig timing_empty = config_for(6);
    timing_empty.midi_minimum_lead_in_seconds = 30.0;
    observations.emplace_back("timing-domain-empty", generate(midi_path, no_audio, timing_empty));
    const std::vector<Note> invalid_baseline{{999.0, 0.25, "C4", ""}};
    observations.emplace_back("baseline-witness-error",
        generate(midi_path, no_audio, config_for(6), &invalid_baseline));
    observations.emplace_back("visible-growth-bound",
        generate(midi_path, no_audio, config_for(6), nullptr, 1));
    const std::vector<Note> overcommitted_baseline = manual[5].notes;
    observations.emplace_back("selection-failure-witness",
        generate(midi_path, no_audio, config_for(1), &overcommitted_baseline));
    // Exercise the near-feasible diagnostic through the same deterministic,
    // generated source used by the parent-compilation oracle.  The witness is
    // therefore pinned to synthetic events rather than a bundled song.
    std::vector<Note> witness_baseline;
    Observation witness;
    for (int difficulty = 1; difficulty <= 6; ++difficulty) {
        SongConfig witness_config = config_for(difficulty);
        witness_config.midi_audio_alignment_seconds = 0.0;
        witness_config.midi_audio_offset_seconds = 0.0;
        witness_config.midi_minimum_lead_in_seconds = 2.0;
        const std::size_t maximum = witness_baseline.empty() || difficulty == 6 ? 0 :
            ff7rp::pipeline::maximum_midi_visible_profile_actions(witness_baseline.size());
        witness = generate(witness_path, no_audio, witness_config,
            witness_baseline.empty() ? nullptr : &witness_baseline, maximum);
        if (witness.status.ok() && (witness_baseline.empty() ||
            ff7rp::pipeline::has_meaningful_midi_profile_growth(witness_baseline.size(), witness.notes.size()))) {
            witness_baseline = witness.notes;
        }
    }
    observations.emplace_back("near-feasible-witness", std::move(witness));

    const bool dump = argc == 2 && std::string_view(argv[1]) == "--dump";
    if (observations.size() != expected.size()) return fail("oracle case count drifted");
    for (std::size_t index = 0; index < observations.size(); ++index) {
        const auto& [name, observed] = observations[index];
        const std::uint64_t digest = fingerprint(observed);
        if (dump) {
            std::cout << name << ' ' << digest << ' ' << static_cast<int>(observed.status.code)
                      << ' ' << observed.notes.size() << " [" << observed.status.message << "]\n";
            continue;
        }
        const Expected& oracle = expected[index];
        if (name != oracle.name || digest != oracle.digest ||
            static_cast<int>(observed.status.code) != oracle.status_code ||
            observed.status.message != oracle.status_message) {
            return fail(name + " drifted: digest=" + std::to_string(digest) +
                " status=" + std::to_string(static_cast<int>(observed.status.code)) +
                " message=[" + observed.status.message + "]");
        }
        const Observation repeated = index < 6 ? generate(midi_path, no_audio, config_for(static_cast<int>(index) + 1)) : observed;
        if (index < 6 && fingerprint(repeated) != digest) return fail(name + " was not deterministic");
    }
    ff7rp::pipeline::NormalizedMidiSource normalized;
    const Status normalization = ff7rp::pipeline::normalize_midi_source(midi_path.string(), &normalized);
    if (!normalization.ok()) return fail("oracle normalization failed: " + normalization.message);
    const ff7rp::pipeline::NormalizedMidiSource original = normalized;
    const SongConfig direct_config = config_for(4);
    const auto direct = ff7rp::pipeline::compile_normalized_midi_chart(
        {normalized, no_audio, direct_config, nullptr, 0});
    const Observation direct_observation{direct.status, direct.notes, direct.stats};
    std::vector<Note> normalized_facade_notes;
    MidiChartStats normalized_facade_stats;
    const Status normalized_facade_status = ff7rp::pipeline::generate_notes_from_normalized_midi(
        normalized, no_audio, direct_config, &normalized_facade_notes, &normalized_facade_stats);
    const Observation normalized_facade{
        normalized_facade_status, std::move(normalized_facade_notes), std::move(normalized_facade_stats)};
    const bool tempos_unchanged = std::equal(normalized.tempos.begin(), normalized.tempos.end(),
        original.tempos.begin(), original.tempos.end(), [](const auto& a, const auto& b) {
            return a.tick == b.tick && std::bit_cast<std::uint64_t>(a.bpm) == std::bit_cast<std::uint64_t>(b.bpm) &&
                a.track == b.track && a.ordinal == b.ordinal;
        });
    const bool meters_unchanged = std::equal(normalized.meters.begin(), normalized.meters.end(),
        original.meters.begin(), original.meters.end(), [](const auto& a, const auto& b) {
            return a.tick == b.tick && a.numerator == b.numerator && a.denominator == b.denominator &&
                a.track == b.track && a.ordinal == b.ordinal && a.explicit_event == b.explicit_event;
        });
    const bool notes_unchanged = std::equal(normalized.notes.begin(), normalized.notes.end(),
        original.notes.begin(), original.notes.end(), [](const auto& a, const auto& b) {
            return a.source == b.source && std::bit_cast<std::uint64_t>(a.start) == std::bit_cast<std::uint64_t>(b.start) &&
                std::bit_cast<std::uint64_t>(a.end) == std::bit_cast<std::uint64_t>(b.end) &&
                std::bit_cast<std::uint64_t>(a.beat) == std::bit_cast<std::uint64_t>(b.beat) &&
                std::bit_cast<std::uint64_t>(a.stream_prior) == std::bit_cast<std::uint64_t>(b.stream_prior);
        });
    if (fingerprint(direct_observation) != expected[3].digest ||
        fingerprint(normalized_facade) != expected[3].digest ||
        normalized.ticks_per_quarter != original.ticks_per_quarter ||
        std::bit_cast<std::uint64_t>(normalized.source_bpm) != std::bit_cast<std::uint64_t>(original.source_bpm) ||
        normalized.tempos.size() != original.tempos.size() || normalized.meters.size() != original.meters.size() ||
        normalized.notes.size() != original.notes.size() ||
        normalized.unsupported_pitch_events != original.unsupported_pitch_events ||
        !tempos_unchanged || !meters_unchanged || !notes_unchanged) {
        return fail("normalized compilation facade diverged from the path facade oracle or mutated normalized input");
    }

    ff7rp::pipeline::configure_chart_row_limit(true, true, true);
    const Observation physical_ambiguous_easy = generate(ambiguous_path, no_audio, config_for(1));
    const Observation physical_ambiguous = generate(ambiguous_path, no_audio, config_for(6));
    const Observation physical_ambiguous_repeat = generate(ambiguous_path, no_audio, config_for(6));
    const Observation physical_exact = generate(exact_path, no_audio, config_for(6));
    const Observation physical_duplicate = generate(duplicate_path, no_audio, config_for(6));
    ff7rp::pipeline::NormalizedMidiSource unsupported_pitch_source;
    const Status unsupported_pitch_normalization = ff7rp::pipeline::normalize_midi_source(
        unsupported_pitch_path.string(), &unsupported_pitch_source);
    const Observation unsupported_pitch = generate(unsupported_pitch_path, no_audio, config_for(6));
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    if (!physical_ambiguous_easy.status.ok() || !physical_ambiguous.status.ok()
        || !physical_exact.status.ok() || !physical_duplicate.status.ok()
        || canonical_note_bytes(physical_ambiguous.notes) != canonical_note_bytes(physical_ambiguous_repeat.notes)) {
        return fail("generalized physical MIDI generation failed or was nondeterministic");
    }
    if (!unsupported_pitch_normalization.ok() || unsupported_pitch_source.unsupported_pitch_events == 0
        || unsupported_pitch.status.ok() || !unsupported_pitch.notes.empty()
        || unsupported_pitch.status.message.find("outside C1-C7") == std::string::npos) {
        return fail("generalized out-of-range source pitches did not fail closed before publication");
    }
    std::size_t equal_frame_grouped_rows = 0;
    std::size_t equal_frame_group_run = 1;
    std::size_t maximum_equal_frame_group_run = 0;
    for (std::size_t index = 1; index < physical_ambiguous.notes.size(); ++index) {
        const Note& previous = physical_ambiguous.notes[index - 1];
        const Note& current = physical_ambiguous.notes[index];
        if (current.beat == previous.beat && current.group_index != 0
            && current.group_index == previous.group_index) {
            ++equal_frame_grouped_rows;
            ++equal_frame_group_run;
            maximum_equal_frame_group_run = std::max(maximum_equal_frame_group_run,
                equal_frame_group_run);
        } else {
            equal_frame_group_run = 1;
        }
    }
    if (equal_frame_grouped_rows == 0 || maximum_equal_frame_group_run < 3u
        || std::any_of(physical_ambiguous.notes.begin(), physical_ambiguous.notes.end(), [](const Note& note) {
            return !note.chord_id.empty();
        })) {
        return fail("ambiguous same-frame harmony was not preserved as grouped RH monotones");
    }
    bool split_cross_hand = false;
    bool hard_chord_is_independent = false;
    for (std::size_t index = 1; index < physical_exact.notes.size(); ++index) {
        const Note& previous = physical_exact.notes[index - 1];
        const Note& current = physical_exact.notes[index];
        split_cross_hand = split_cross_hand || (previous.beat == current.beat
            && !previous.pitch.empty() && previous.chord_id.empty()
            && current.pitch.empty() && !current.chord_id.empty());
        if (previous.beat == current.beat && !previous.pitch.empty()
            && !current.chord_id.empty()) {
            hard_chord_is_independent = current.group_index == 0
                || current.group_index != previous.group_index;
        }
    }
    if (!split_cross_hand || !hard_chord_is_independent
        || std::any_of(physical_exact.notes.begin(), physical_exact.notes.end(),
            [](const Note& note) { return !note.pitch.empty() && !note.chord_id.empty(); })) {
        return fail("generated same-frame RH/chord material was not split into stable one-event rows");
    }
    std::set<std::pair<double, std::string>> duplicate_pitch_rows;
    for (const Note& note : physical_duplicate.notes) {
        if (!note.pitch.empty() && !duplicate_pitch_rows.emplace(note.beat, note.pitch).second) {
            return fail("exact same-frame RH duplicate was not coalesced");
        }
    }
    if (physical_duplicate.stats.dropped_conflicts == 0) {
        return fail("exact duplicate coalescing was not reported");
    }
    const auto physical_plan = [](const Observation& observation, const int difficulty,
                                  ff7rp::pipeline::ChartEventPlan* plan) {
        SongConfig config = config_for(difficulty);
        config.notes = observation.notes;
        config.notes_provided = true;
        config.bpm = observation.stats.source_bpm;
        ff7rp::pipeline::CompiledChart chart;
        if (!ff7rp::pipeline::compile_chart(config, &chart).ok()) return false;
        return ff7rp::pipeline::derive_chart_event_plan(config.notes, chart.notes, plan);
    };
    ff7rp::pipeline::ChartEventPlan easy_plan;
    ff7rp::pipeline::ChartEventPlan hard_plan;
    if (!physical_plan(physical_ambiguous_easy, 1, &easy_plan)
        || !physical_plan(physical_ambiguous, 6, &hard_plan)
        || easy_plan.physical_digest != hard_plan.physical_digest
        || easy_plan.source_row_count != hard_plan.source_row_count
        || easy_plan.native_event_count != hard_plan.native_event_count) {
        return fail("generalized profiles did not preserve one physical chart identity: easy_rows="
            + std::to_string(easy_plan.source_row_count) + " hard_rows="
            + std::to_string(hard_plan.source_row_count) + " easy_events="
            + std::to_string(easy_plan.native_event_count) + " hard_events="
            + std::to_string(hard_plan.native_event_count) + " easy_digest="
            + std::to_string(easy_plan.physical_digest) + " hard_digest="
            + std::to_string(hard_plan.physical_digest));
    }
    const auto selected_inside_band = [](const Observation& observation) {
        return observation.stats.selected_actions >= observation.stats.target_minimum_rows
            && observation.stats.selected_actions <= observation.stats.target_maximum_rows;
    };
    if (!selected_inside_band(physical_ambiguous_easy) || !selected_inside_band(physical_ambiguous)
        || easy_plan.required_action_count != physical_ambiguous_easy.stats.selected_actions
        || hard_plan.required_action_count != physical_ambiguous.stats.selected_actions
        || easy_plan.source_row_count != physical_ambiguous_easy.notes.size()
        || hard_plan.source_row_count != physical_ambiguous.notes.size()) {
        return fail("physical-domain selector changed rows or missed its calibrated root band");
    }
    const auto right_root = [](const std::vector<Note>& notes, const std::size_t index) {
        const Note& note = notes[index];
        return !note.pitch.empty() && (note.group_index == 0 || index == 0
            || notes[index - 1].group_index != note.group_index);
    };
    for (std::size_t index = 0; index < physical_ambiguous_easy.notes.size(); ++index) {
        if (right_root(physical_ambiguous_easy.notes, index)
            && !right_root(physical_ambiguous.notes, index)) {
            return fail("generalized easy-profile roots were not nested in the hard profile");
        }
    }
    std::string cleanup_error;
    if (!temporary.cleanup(&cleanup_error)) return fail("temporary cleanup failed: " + cleanup_error);
    return 0;
}
