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
#include "pipeline/midi_analysis_core.h"
#include "pipeline/midi_source_normalizer.h"
#include "pipeline/native_asset_capabilities.h"
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

std::vector<unsigned char> single_chord_midi_bytes(
    const std::initializer_list<int> pitches, const bool flat_key = false,
    const bool heterogeneous_lengths = false) {
    std::vector<MidiEvent> melody;
    std::vector<MidiEvent> harmony;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    if (flat_key) melody.push_back({0, 0, {0xff, 0x59, 0x02, 0xfe, 0x00}});
    for (int event = 0; event < 8; ++event) {
        const int tick = event * 960;
        add_note(&melody, tick, 360, 72 + event % 5, 104);
        int velocity = 82;
        std::size_t pitch_index = 0;
        for (const int pitch : pitches) {
            add_note(&harmony, tick, heterogeneous_lengths && pitch_index++ == 1u ? 480 : 720,
                pitch, velocity--);
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

enum class DbChordContext {
    Missing,
    Neutral,
    Sharp,
    Flat,
    Conflicting,
    ExactTickFlat,
    Straddling,
};

std::vector<unsigned char> db_chord_context_midi_bytes(const DbChordContext context) {
    std::vector<MidiEvent> melody;
    std::vector<MidiEvent> harmony;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    if (context == DbChordContext::Neutral || context == DbChordContext::ExactTickFlat) {
        melody.push_back({0, 0, {0xff, 0x59, 0x02, 0x00, 0x00}});
    } else if (context == DbChordContext::Sharp) {
        melody.push_back({0, 0, {0xff, 0x59, 0x02, 0x02, 0x00}});
    } else if (context == DbChordContext::Flat || context == DbChordContext::Conflicting) {
        melody.push_back({0, 0, {0xff, 0x59, 0x02, 0xfe, 0x00}});
    }
    if (context == DbChordContext::Conflicting) {
        harmony.push_back({0, 0, {0xff, 0x59, 0x02, 0x02, 0x00}});
    }
    constexpr int first_tick = 1920;
    for (int event = 0; event < 8; ++event) {
        const int tick = first_tick + event * 960;
        if (context == DbChordContext::ExactTickFlat && event == 0) {
            melody.push_back({tick, 0, {0xff, 0x59, 0x02, 0xfe, 0x00}});
        }
        if (context == DbChordContext::Straddling) {
            melody.push_back({tick - 12, 0, {0xff, 0x59, 0x02, 0xfe, 0x00}});
            melody.push_back({tick + 3, 0, {0xff, 0x59, 0x02, 0x00, 0x00}});
        }
        add_note(&melody, tick, 360, 84 + event % 3, 108);
        add_note(&harmony, tick, 720, 49, 82);
        const int upper_tick = context == DbChordContext::Straddling ? tick + 6 : tick;
        add_note(&harmony, upper_tick, 720, 53, 81);
        add_note(&harmony, upper_tick, 720, 56, 80);
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

std::vector<unsigned char> accidental_context_midi_bytes(const bool include_signatures) {
    std::vector<MidiEvent> melody;
    std::vector<MidiEvent> context;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    for (const int tick : {960, 1920, 2880, 3840, 4800, 5760, 6720}) {
        add_note(&melody, tick, 240, 61, 100);
    }
    if (include_signatures) {
        melody.push_back({1920, 0, {0xff, 0x59, 0x02, 0xfe, 0x00}}); // two flats
        melody.push_back({2880, 0, {0xff, 0x59, 0x02, 0x00, 0x00}}); // neutral
        melody.push_back({3840, 0, {0xff, 0x59, 0x02, 0x02, 0x00}}); // two sharps
        melody.push_back({4800, 0, {0xff, 0x59, 0x02, 0xfd, 0x01}}); // three flats, minor
        context.push_back({5760, 0, {0xff, 0x59, 0x02, 0x02, 0x00}}); // conflicting sharp
        context.push_back({6720, 0, {0xff, 0x59, 0x02, 0xff, 0x00}}); // resolves flat
    }
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 1);
    append_u16(&file, 2);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(melody));
    append_midi_track(&file, std::move(context));
    return file;
}

std::vector<unsigned char> malformed_key_signature_midi_bytes(
    const std::vector<unsigned char>& event) {
    std::vector<MidiEvent> track;
    track.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    track.push_back({0, 0, event});
    add_note(&track, 1920, 240, 61, 100);
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 0);
    append_u16(&file, 1);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(track));
    return file;
}

struct PhysicalFrame {
    int frame = 0;
    std::vector<int> pitches;
    int velocity = 100;
    int duration_ticks = 8;
};

std::vector<unsigned char> physical_frame_midi_bytes(
    const std::vector<PhysicalFrame>& frames,
    const std::vector<std::pair<int, int>>& meter_changes = {}) {
    constexpr int base_tick = 1920;
    constexpr int ticks_per_frame = 16;
    std::vector<MidiEvent> melody;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    if (std::none_of(meter_changes.begin(), meter_changes.end(),
            [](const auto& change) { return change.first == 0; })) {
        melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    }
    for (const auto& [frame, numerator] : meter_changes) {
        melody.push_back({base_tick + frame * ticks_per_frame, 0,
            {0xff, 0x58, 0x04, static_cast<unsigned char>(numerator), 0x02, 24, 8}});
    }
    for (const PhysicalFrame& frame : frames) {
        for (const int pitch : frame.pitches) {
            add_note(&melody, base_tick + frame.frame * ticks_per_frame, frame.duration_ticks,
                pitch, frame.velocity);
        }
    }
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 0);
    append_u16(&file, 1);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(melody));
    return file;
}

std::vector<unsigned char> verified_chord_runs_midi_bytes(
    const std::size_t frame_count, const int spacing_frames) {
    constexpr int base_tick = 1920;
    constexpr int ticks_per_frame = 16;
    std::vector<MidiEvent> melody;
    std::vector<MidiEvent> harmony;
    melody.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    melody.push_back({0, 0, {0xff, 0x58, 0x04, 0x04, 0x02, 24, 8}});
    for (std::size_t index = 0; index < frame_count; ++index) {
        const int tick = base_tick + static_cast<int>(index) * spacing_frames * ticks_per_frame;
        add_note(&melody, tick, 8, 72 + static_cast<int>(index % 5u), 104);
        add_note(&harmony, tick, 8, 48, 84);
        add_note(&harmony, tick, 8, 52, 82);
        add_note(&harmony, tick, 8, 55, 80);
    }
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 1);
    append_u16(&file, 2);
    append_u16(&file, 480);
    append_midi_track(&file, std::move(melody));
    append_midi_track(&file, std::move(harmony));
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
        bytes.push_back(note.monotone_note_value.provided ? 1u : 0u);
        bytes.push_back(note.monotone_note_value.value.note_type);
        bytes.push_back(note.monotone_note_value.value.dot_type);
        bytes.push_back(note.chord_note_value.provided ? 1u : 0u);
        bytes.push_back(note.chord_note_value.value.note_type);
        bytes.push_back(note.chord_note_value.value.dot_type);
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
        out.integer(note.monotone_note_value.provided);
        out.integer(note.monotone_note_value.value.note_type);
        out.integer(note.monotone_note_value.value.dot_type);
        out.integer(note.chord_note_value.provided);
        out.integer(note.chord_note_value.value.note_type);
        out.integer(note.chord_note_value.value.dot_type);
    }
    append_stats(&out, value.stats);
    return out.value();
}

Observation generate(const std::filesystem::path& path, const WavAudio& audio,
                     const SongConfig& config, const std::vector<Note>* baseline = nullptr,
                     const std::size_t maximum_visible_rows = 0,
                     const ff7rp::pipeline::NativeAssetCapabilities native_assets =
                         ff7rp::pipeline::selected_native_asset_capabilities()) {
    Observation value;
    value.status = ff7rp::pipeline::generate_notes_from_midi(path.string(), audio, config,
        &value.notes, &value.stats, baseline, maximum_visible_rows, native_assets);
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
    {"manual-lv1", 6773119318625811841ull, 0, ""},
    {"manual-lv2", 13917796721266263578ull, 0, ""},
    {"manual-lv3", 3422253880615500361ull, 0, ""},
    {"manual-lv4", 8893295841173198285ull, 0, ""},
    {"manual-lv5", 14940100557045089030ull, 0, ""},
    {"manual-lv6", 2786189282108214522ull, 0, ""},
    {"baseline-lv1", 18382197050995942130ull, 0, ""},
    {"baseline-lv2", 14470395282069359180ull, 0, ""},
    {"baseline-lv3", 8443132770364117ull, 0, ""},
    {"baseline-lv4", 9589817132494063679ull, 0, ""},
    {"baseline-lv5", 4163578994208223256ull, 0, ""},
    {"baseline-lv6", 5932381222701315157ull, 0, ""},
    {"automatic-alignment", 17191488344814720703ull, 0, ""},
    {"timing-domain-empty", 12345897120421995243ull, 7,
        "MIDI generation produced no chart rows inside the lead-in/audio timing domain"},
    {"baseline-witness-error", 8598688527783776924ull, 7,
        "preferred lower-profile action has no source candidate at its native frame"},
    {"visible-growth-bound", 3998143965459286508ull, 8,
        "difficulty target band exceeds the maximum adjacent visible-profile growth"},
    {"selection-failure-witness", 17716276405075993264ull, 8,
        "no target-band state satisfies a coherent local-skill route"},
    {"near-feasible-witness", 16306373482813534199ull, 8,
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
    const auto write_physical_fixture = [&](const char* name, const std::vector<PhysicalFrame>& frames,
                                             const std::vector<std::pair<int, int>>& meters = {}) {
        const std::filesystem::path path = temporary.path() / name;
        const std::vector<unsigned char> fixture = physical_frame_midi_bytes(frames, meters);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(fixture.data()), static_cast<std::streamsize>(fixture.size()));
        return path;
    };
    const auto write_bytes_fixture = [&](const char* name, const std::vector<unsigned char>& fixture) {
        const std::filesystem::path path = temporary.path() / name;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(fixture.data()), static_cast<std::streamsize>(fixture.size()));
        return path;
    };
    const std::filesystem::path accidental_context_path = write_bytes_fixture(
        "accidental-context.mid", accidental_context_midi_bytes(true));
    const std::filesystem::path accidental_legacy_path = write_bytes_fixture(
        "accidental-no-signature.mid", accidental_context_midi_bytes(false));
    const std::array<std::filesystem::path, 3> malformed_key_signature_paths{
        write_bytes_fixture("key-signature-shape.mid",
            malformed_key_signature_midi_bytes({0xff, 0x59, 0x01, 0x00})),
        write_bytes_fixture("key-signature-fifths.mid",
            malformed_key_signature_midi_bytes({0xff, 0x59, 0x02, 0x08, 0x00})),
        write_bytes_fixture("key-signature-mode.mid",
            malformed_key_signature_midi_bytes({0xff, 0x59, 0x02, 0x00, 0x02})),
    };
    const std::filesystem::path flat_c_sharp_chord_path = write_bytes_fixture(
        "flat-context-c-sharp-chord.mid", db_chord_context_midi_bytes(DbChordContext::Flat));
    const std::array<std::filesystem::path, 4> sharp_fallback_chord_paths{
        write_bytes_fixture("missing-context-c-sharp-chord.mid",
            db_chord_context_midi_bytes(DbChordContext::Missing)),
        write_bytes_fixture("neutral-context-c-sharp-chord.mid",
            db_chord_context_midi_bytes(DbChordContext::Neutral)),
        write_bytes_fixture("sharp-context-c-sharp-chord.mid",
            db_chord_context_midi_bytes(DbChordContext::Sharp)),
        write_bytes_fixture("conflicting-context-c-sharp-chord.mid",
            db_chord_context_midi_bytes(DbChordContext::Conflicting)),
    };
    const std::filesystem::path exact_tick_db_chord_path = write_bytes_fixture(
        "exact-tick-flat-db-chord.mid", db_chord_context_midi_bytes(DbChordContext::ExactTickFlat));
    const std::filesystem::path straddling_db_chord_path = write_bytes_fixture(
        "straddling-db-chord.mid", db_chord_context_midi_bytes(DbChordContext::Straddling));
    const std::filesystem::path heterogeneous_db_chord_path = write_bytes_fixture(
        "heterogeneous-db-chord-lengths.mid", single_chord_midi_bytes({49, 53, 56}, true, true));
    const std::filesystem::path exact_note_values_path = write_physical_fixture(
        "exact-note-values.mid", {
            {0, {60}, 100, 1920}, {200, {61}, 100, 2880}, {400, {62}, 100, 960},
            {600, {63}, 100, 1440}, {800, {64}, 100, 480}, {1000, {65}, 100, 720},
            {1200, {66}, 100, 240}, {1400, {67}, 100, 360}, {1600, {68}, 100, 120},
            {1800, {69}, 100, 180}});

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
    if (!grouped_easy.status.ok() || !grouped_hard.status.ok() ||
        canonical_note_bytes(grouped_easy.notes) != canonical_note_bytes(grouped_easy_repeat.notes) ||
        fingerprint(grouped_easy) != fingerprint(grouped_easy_repeat) ||
        multi_follower.status.code != multi_follower_repeat.status.code ||
        multi_follower.status.message != multi_follower_repeat.status.message ||
        canonical_note_bytes(multi_follower.notes) != canonical_note_bytes(multi_follower_repeat.notes) ||
        fingerprint(multi_follower) != fingerprint(multi_follower_repeat) ||
        grouped_easy.notes.size() >= grouped_hard.notes.size() ||
        std::any_of(grouped_easy.notes.begin(), grouped_easy.notes.end(), [](const Note& note) {
            return note.group_index != 0;
        }) || std::any_of(grouped_hard.notes.begin(), grouped_hard.notes.end(), [](const Note& note) {
            return note.group_index != 0;
        }) || std::any_of(multi_follower.notes.begin(), multi_follower.notes.end(), [](const Note& note) {
            return note.group_index != 0;
        })) {
        return fail("independent profile spacing was not deterministic and ungrouped: easy=" +
            std::to_string(grouped_easy.notes.size()) + " hard=" +
            std::to_string(grouped_hard.notes.size()) + " multi=" +
            std::to_string(multi_follower.notes.size()) + " statuses=[" +
            grouped_easy.status.message + "][" + grouped_hard.status.message + "][" +
            multi_follower.status.message + "]");
    }
    std::vector<std::pair<std::string, Observation>> observations;
    std::array<Observation, 6> manual;
    for (int difficulty = 1; difficulty <= 6; ++difficulty) {
        manual[static_cast<std::size_t>(difficulty - 1)] =
            generate(midi_path, no_audio, config_for(difficulty));
        observations.emplace_back("manual-lv" + std::to_string(difficulty),
            manual[static_cast<std::size_t>(difficulty - 1)]);
    }
    bool observed_exact_voicing = false;
    for (const Observation& profile : manual) {
        std::size_t required_actions = 0;
        for (const Note& note : profile.notes) {
            if (note.group_index != 0) return fail("automatic MIDI row serialized a generated group");
            if (!note.pitch.empty()) ++required_actions;
            if (!note.chord_id.empty()) {
                ++required_actions;
                observed_exact_voicing = observed_exact_voicing || !note.source_chord_pitches.empty();
                if (!note.ignore_sound_pitches.empty()) return fail("generated IgnoreSound guessed without a verified mapping");
            }
        }
        if (profile.status.ok() && profile.stats.selected_actions != required_actions) {
            return fail("ungrouped generated event was not counted as a required action");
        }
    }
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
    const bool key_signatures_unchanged = std::equal(
        normalized.key_signatures.begin(), normalized.key_signatures.end(),
        original.key_signatures.begin(), original.key_signatures.end(), [](const auto& a, const auto& b) {
            return a.tick == b.tick && a.fifths == b.fifths && a.minor == b.minor &&
                a.track == b.track && a.ordinal == b.ordinal;
        });
    if (fingerprint(direct_observation) != expected[3].digest ||
        fingerprint(normalized_facade) != expected[3].digest ||
        normalized.ticks_per_quarter != original.ticks_per_quarter ||
        std::bit_cast<std::uint64_t>(normalized.source_bpm) != std::bit_cast<std::uint64_t>(original.source_bpm) ||
        normalized.tempos.size() != original.tempos.size() || normalized.meters.size() != original.meters.size() ||
        normalized.key_signatures.size() != original.key_signatures.size() ||
        normalized.notes.size() != original.notes.size() ||
        normalized.unsupported_pitch_events != original.unsupported_pitch_events ||
        !tempos_unchanged || !meters_unchanged || !key_signatures_unchanged || !notes_unchanged) {
        return fail("normalized compilation facade diverged from the path facade oracle or mutated normalized input");
    }

    ff7rp::pipeline::NormalizedMidiSource accidental_source;
    const Status accidental_normalization = ff7rp::pipeline::normalize_midi_source(
        accidental_context_path.string(), &accidental_source);
    if (!accidental_normalization.ok() || accidental_source.key_signatures.size() != 6u) {
        return fail("valid MIDI key-signature events were not normalized exactly");
    }
    const auto& signatures = accidental_source.key_signatures;
    if (signatures[0].tick != 1920 || signatures[0].fifths != -2 || signatures[0].minor
        || signatures[3].tick != 4800 || signatures[3].fifths != -3 || !signatures[3].minor
        || signatures[4].tick != 5760 || signatures[4].track != 1 || signatures[4].fifths != 2
        || signatures[5].tick != 6720 || signatures[5].ordinal <= signatures[4].ordinal) {
        return fail("normalized MIDI key-signature source identity drifted");
    }
    const auto orientation = ff7rp::pipeline::build_midi_accidental_orientation_timeline(signatures);
    using ff7rp::pipeline::MidiAccidentalOrientation;
    const auto orientation_at = [&](const int tick) {
        return ff7rp::pipeline::midi_accidental_orientation_at_tick(orientation, tick);
    };
    if (orientation_at(1919) != MidiAccidentalOrientation::SharpFallback
        || orientation_at(1920) != MidiAccidentalOrientation::Flat
        || orientation_at(2880) != MidiAccidentalOrientation::SharpFallback
        || orientation_at(4800) != MidiAccidentalOrientation::Flat
        || orientation_at(5760) != MidiAccidentalOrientation::SharpFallback
        || orientation_at(6720) != MidiAccidentalOrientation::Flat) {
        return fail("MIDI accidental orientation did not apply exact-tick changes or conflicts deterministically");
    }
    for (const std::filesystem::path& malformed : malformed_key_signature_paths) {
        ff7rp::pipeline::NormalizedMidiSource rejected;
        const Status rejected_status = ff7rp::pipeline::normalize_midi_source(malformed.string(), &rejected);
        if (rejected_status.code != ff7rp::pipeline::StatusCode::InvalidMidi) {
            return fail("malformed MIDI key-signature event was not rejected: " + malformed.filename().string());
        }
    }
    const Observation accidental_easy = generate(accidental_context_path, no_audio, config_for(1));
    const Observation accidental_hard = generate(accidental_context_path, no_audio, config_for(6));
    const Observation accidental_repeat = generate(accidental_context_path, no_audio, config_for(6));
    const Observation accidental_legacy = generate(accidental_legacy_path, no_audio, config_for(6));
    const auto assets_1004 = ff7rp::pipeline::native_asset_capabilities_for_catalog(
        "ff7rebirth-steam-win64-68fd6fde");
    const auto assets_1005 = ff7rp::pipeline::native_asset_capabilities_for_catalog(
        "ff7rebirth-steam-win64-6a16ced2");
    const Observation flat_c_sharp_chord = generate(
        flat_c_sharp_chord_path, no_audio, config_for(6), nullptr, 0, assets_1005);
    const Observation flat_c_sharp_chord_easy = generate(
        flat_c_sharp_chord_path, no_audio, config_for(1), nullptr, 0, assets_1005);
    const Observation flat_c_sharp_chord_repeat = generate(
        flat_c_sharp_chord_path, no_audio, config_for(6), nullptr, 0, assets_1005);
    const Observation flat_c_sharp_chord_1004 = generate(
        flat_c_sharp_chord_path, no_audio, config_for(6), nullptr, 0, assets_1004);
    const Observation exact_tick_db_chord = generate(
        exact_tick_db_chord_path, no_audio, config_for(6), nullptr, 0, assets_1005);
    const Observation straddling_db_chord = generate(
        straddling_db_chord_path, no_audio, config_for(6), nullptr, 0, assets_1005);
    const Observation heterogeneous_db_chord = generate(
        heterogeneous_db_chord_path, no_audio, config_for(6), nullptr, 0, assets_1005);
    const Observation exact_note_values = generate(exact_note_values_path, no_audio, config_for(6));
    const std::array<std::string_view, 7> expected_accidentals{
        "C#4", "Db4", "C#4", "C#4", "Db4", "C#4", "Db4"
    };
    const auto exact_spelling = [&](const Observation& observed) {
        if (!observed.status.ok() || observed.notes.size() != expected_accidentals.size()) return false;
        for (std::size_t index = 0; index < expected_accidentals.size(); ++index) {
            if (observed.notes[index].pitch != expected_accidentals[index]
                || (expected_accidentals[index] == "Db4" && observed.notes[index].alternate_monotone)) return false;
        }
        return true;
    };
    if (!exact_spelling(accidental_easy) || !exact_spelling(accidental_hard)
        || canonical_note_bytes(accidental_hard.notes) != canonical_note_bytes(accidental_repeat.notes)
        || !accidental_legacy.status.ok() || accidental_legacy.notes.size() != 7u
        || std::any_of(accidental_legacy.notes.begin(), accidental_legacy.notes.end(), [](const Note& note) {
            return note.pitch != "C#4";
        })) {
        return fail("MIDI key-signature accidental spelling was unstable across contexts or profiles");
    }
    const auto exact_chord_identity = [](const Observation& observed, const std::string_view id) {
        bool found = false;
        if (!observed.status.ok()) return false;
        for (const Note& note : observed.notes) {
            if (note.chord_id.empty()) continue;
            if (note.chord_id != id || note.source_chord_pitches.empty()
                || !note.ignore_sound_pitches.empty() || note.group_index != 0) return false;
            found = true;
        }
        return found;
    };
    if (!exact_chord_identity(flat_c_sharp_chord, "pca_Db")
        || !exact_chord_identity(flat_c_sharp_chord_easy, "pca_Db")
        || canonical_note_bytes(flat_c_sharp_chord.notes) !=
            canonical_note_bytes(flat_c_sharp_chord_repeat.notes)
        || !exact_chord_identity(exact_tick_db_chord, "pca_Db")
        || !exact_chord_identity(straddling_db_chord, "pca_Cs")
        || !exact_chord_identity(flat_c_sharp_chord_1004, "pca_Db")) {
        return fail("Db chord inference did not preserve exact flat context and boundary fallback");
    }
    const std::array<ff7rp::pipeline::NativeNoteValue, 10> exact_values{{
        {0, 0}, {0, 1}, {1, 0}, {1, 1}, {2, 0},
        {2, 1}, {3, 0}, {3, 1}, {4, 0}, {4, 1}}};
    if (!exact_note_values.status.ok() || exact_note_values.notes.size() != exact_values.size()) {
        return fail("exact MIDI source lengths did not survive reduction");
    }
    for (std::size_t index = 0; index < exact_values.size(); ++index) {
        if (exact_note_values.notes[index].monotone_note_value !=
                ff7rp::pipeline::NoteValueOverride{exact_values[index], true}
            || exact_note_values.notes[index].chord_note_value.provided) {
            return fail("exact MIDI monotone length did not map to the expected native notation");
        }
    }
    const auto chord_notation = [](const Observation& observed) -> ff7rp::pipeline::NoteValueOverride {
        for (const Note& note : observed.notes) if (!note.chord_id.empty()) return note.chord_note_value;
        return {};
    };
    if (chord_notation(flat_c_sharp_chord) != ff7rp::pipeline::NoteValueOverride{{2, 1}, true}
        || chord_notation(heterogeneous_db_chord).provided
        || !exact_chord_identity(heterogeneous_db_chord, "pca_Db")) {
        return fail("homogeneous or heterogeneous MIDI chord length notation was not exact");
    }
    for (const std::filesystem::path& path : sharp_fallback_chord_paths) {
        if (!exact_chord_identity(generate(path, no_audio, config_for(6)), "pca_Cs")) {
            return fail("Db chord inference did not retain canonical fallback for " + path.filename().string());
        }
    }
    const auto compile_plan = [&](const Observation& observed, ff7rp::pipeline::ChartEventPlan* plan) {
        SongConfig plan_config = config_for(6);
        plan_config.notes = observed.notes;
        plan_config.notes_provided = true;
        plan_config.bpm = observed.stats.source_bpm;
        ff7rp::pipeline::CompiledChart compiled;
        return ff7rp::pipeline::compile_chart(plan_config, &compiled).ok()
            && ff7rp::pipeline::derive_chart_event_plan(plan_config.notes, compiled.notes, plan);
    };
    ff7rp::pipeline::ChartEventPlan flat_plan;
    ff7rp::pipeline::ChartEventPlan sharp_plan;
    if (!compile_plan(accidental_hard, &flat_plan) || !compile_plan(accidental_legacy, &sharp_plan)
        || flat_plan.physical_digest == sharp_plan.physical_digest) {
        return fail("enharmonic generated identities did not change the physical digest");
    }
    ff7rp::pipeline::ChartEventPlan flat_chord_plan;
    ff7rp::pipeline::ChartEventPlan heterogeneous_chord_plan;
    const Observation sharp_chord = generate(sharp_fallback_chord_paths.front(), no_audio, config_for(6));
    ff7rp::pipeline::ChartEventPlan sharp_chord_plan;
    if (!compile_plan(flat_c_sharp_chord, &flat_chord_plan)
        || !compile_plan(heterogeneous_db_chord, &heterogeneous_chord_plan)
        || !compile_plan(sharp_chord, &sharp_chord_plan)
        || flat_chord_plan.source_row_count != flat_chord_plan.native_event_count
        || flat_chord_plan.native_prefix_event_count != flat_chord_plan.source_row_count
        || flat_chord_plan.source_row_count != flat_chord_plan.required_action_count
        || flat_chord_plan.physical_digest == sharp_chord_plan.physical_digest
        || flat_chord_plan.physical_digest == heterogeneous_chord_plan.physical_digest) {
        return fail("automatic Db chord accounting or physical identity was not exact");
    }

    ff7rp::pipeline::configure_chart_row_limit(true, true);
    if (std::string_view(ff7rp::pipeline::kGeneratedMidiGenerationIdentity)
        != "midi_generation=independent_ungrouped:key_signature_spelling+exact_note_values:v11") {
        return fail("generated MIDI semantic identity did not invalidate legacy accidental spelling");
    }
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
    const auto generated_rows_are_ungrouped = [](const Observation& observation) {
        return std::all_of(observation.notes.begin(), observation.notes.end(), [](const Note& note) {
            return note.group_index == 0;
        });
    };
    if (!generated_rows_are_ungrouped(physical_ambiguous_easy)
        || !generated_rows_are_ungrouped(physical_ambiguous)
        || !generated_rows_are_ungrouped(physical_exact)
        || canonical_note_bytes(grouped_easy.notes) == canonical_note_bytes(grouped_hard.notes)) {
        return fail("verified MIDI profiles were not independently reduced and ungrouped");
    }
    if (std::none_of(physical_exact.notes.begin(), physical_exact.notes.end(),
            [](const Note& note) { return !note.chord_id.empty() && !note.source_chord_pitches.empty(); })) {
        return fail("verified source chord did not survive independent MIDI reduction");
    }
    std::set<std::pair<double, std::string>> reduced_duplicate_rows;
    const bool repeated_same_key = std::any_of(physical_duplicate.notes.begin(), physical_duplicate.notes.end(),
        [&](const Note& note) { return !note.pitch.empty()
            && !reduced_duplicate_rows.emplace(note.beat, note.pitch).second; });
    if (repeated_same_key || physical_duplicate.stats.source_events <= physical_duplicate.notes.size()) {
        return fail("same-key doubling was not removed by independent reduction");
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
    if (!physical_plan(grouped_easy, 1, &easy_plan)
        || !physical_plan(grouped_hard, 6, &hard_plan)
        || easy_plan.physical_digest == hard_plan.physical_digest
        || easy_plan.source_row_count != grouped_easy.notes.size()
        || hard_plan.source_row_count != grouped_hard.notes.size()) {
        return fail("independent generated profiles did not preserve distinct source-backed plans");
    }
    const auto selected_inside_band = [](const Observation& observation) {
        return observation.stats.selected_actions >= observation.stats.target_minimum_rows
            && observation.stats.selected_actions <= observation.stats.target_maximum_rows;
    };
    if (!selected_inside_band(grouped_easy) || !selected_inside_band(grouped_hard)
        || easy_plan.required_action_count != grouped_easy.stats.selected_actions
        || hard_plan.required_action_count != grouped_hard.stats.selected_actions
        || easy_plan.source_row_count != easy_plan.native_event_count
        || easy_plan.native_event_count != easy_plan.required_action_count
        || hard_plan.source_row_count != hard_plan.native_event_count
        || hard_plan.native_event_count != hard_plan.required_action_count) {
        return fail("ungrouped generated profile violated exact R/P/E/A accounting or its target band");
    }
    const auto envelope_config = [](const int difficulty) {
        SongConfig config = config_for(difficulty);
        config.midi_audio_alignment_seconds = 0.0;
        config.midi_audio_offset_seconds = 0.0;
        return config;
    };
    const auto generate_physical_fixture = [&](const char* name, const std::vector<PhysicalFrame>& frames,
                                                const std::vector<std::pair<int, int>>& meters = {}) {
        return generate(write_physical_fixture(name, frames, meters), no_audio, envelope_config(1));
    };

    const Observation dense_stack = generate_physical_fixture(
        "dense-inner-stack.mid", {{0, {48, 49, 50, 51, 52, 53, 54, 55, 56}}});
    const Observation dense_stack_repeat = generate_physical_fixture(
        "dense-inner-stack-repeat.mid", {{0, {48, 49, 50, 51, 52, 53, 54, 55, 56}}});
    std::set<double> dense_stack_frames;
    const bool repeated_dense_frame = std::any_of(dense_stack.notes.begin(), dense_stack.notes.end(),
        [&](const Note& note) { return !dense_stack_frames.insert(note.beat).second; });
    if (!dense_stack.status.ok() || repeated_dense_frame
        || canonical_note_bytes(dense_stack.notes) != canonical_note_bytes(dense_stack_repeat.notes)) {
        return fail("dense same-frame inner voices were not reduced deterministically to one melody tone");
    }

    const Observation physical_exact_easy = generate(exact_path, no_audio, config_for(1));
    if (!physical_exact_easy.status.ok()) return fail("easy generalized chord fixture failed");
    for (const Note& note : physical_exact_easy.notes) {
        if (note.group_index != 0) return fail("generated chord row was not an independent action");
    }

    const std::filesystem::path extended_path = write_bytes_fixture("extended-ungrouped.mid",
        verified_chord_runs_midi_bytes(600u, 54));
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    const Observation extended = generate(extended_path, no_audio, envelope_config(6));
    const std::filesystem::path pathological_path = write_bytes_fixture("pathological-selector-work.mid",
        verified_chord_runs_midi_bytes(2000u, 54));
    const Observation pathological = generate(pathological_path, no_audio, envelope_config(6));
    const Observation pathological_repeat = generate(pathological_path, no_audio, envelope_config(6));

    ff7rp::pipeline::configure_chart_row_limit(false, false);
    const Observation ordinary = generate(extended_path, no_audio, envelope_config(6));
    constexpr std::string_view work_budget_diagnostic =
        "incremental MIDI selector projected analysis work exceeds deterministic budget";
    if (!extended.status.ok() || extended.notes.size() <= ff7rp::pipeline::kMaxChartRows
        || extended.notes.size() > ff7rp::pipeline::kMaximumExtendedChartRows
        || extended.stats.selected_actions != extended.notes.size()
        || std::any_of(extended.notes.begin(), extended.notes.end(), [](const Note& note) {
            return note.group_index != 0;
        }) || ordinary.status.code != ff7rp::pipeline::StatusCode::ChartRowLimitExceeded
        || !ordinary.notes.empty() || ordinary.stats.selected_actions != 0u) {
        return fail("verified extended or ordinary MIDI row policy violated ungrouped publication bounds: extended="
            + std::to_string(extended.notes.size()) + "/" + std::to_string(extended.stats.selected_actions)
            + " [" + extended.status.message + "] ordinary=" + std::to_string(ordinary.notes.size())
            + "/" + std::to_string(ordinary.stats.selected_actions) + " [" + ordinary.status.message + "]");
    }
    if (pathological.status.code != ff7rp::pipeline::StatusCode::ChartStrainLimitExceeded
        || pathological.status.message != work_budget_diagnostic || !pathological.notes.empty()
        || pathological_repeat.status.code != pathological.status.code
        || pathological_repeat.status.message != pathological.status.message
        || !pathological_repeat.notes.empty()) {
        return fail("pathological incremental MIDI selection did not fail fast and deterministically: "
            + pathological.status.message);
    }

    ff7rp::pipeline::ChartEventPlan exact_easy_plan;
    ff7rp::pipeline::ChartEventPlan exact_hard_plan;
    if (!physical_plan(physical_exact_easy, 1, &exact_easy_plan)
        || !physical_plan(physical_exact, 6, &exact_hard_plan)
        || exact_easy_plan.source_row_count != physical_exact_easy.notes.size()
        || exact_easy_plan.native_event_count != physical_exact_easy.notes.size()
        || exact_easy_plan.required_action_count != physical_exact_easy.stats.selected_actions
        || exact_hard_plan.source_row_count != physical_exact.notes.size()
        || exact_hard_plan.native_event_count != physical_exact.notes.size()
        || exact_hard_plan.required_action_count != physical_exact.stats.selected_actions) {
        return fail("independent ungrouped exact R/P/E/A accounting changed");
    }

    std::string cleanup_error;
    if (!temporary.cleanup(&cleanup_error)) return fail("temporary cleanup failed: " + cleanup_error);
    return 0;
}
