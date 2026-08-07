#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "pipeline/midi_chart_generator.h"
#include "pipeline/midi_chart_compilation.h"
#include "pipeline/midi_source_normalizer.h"
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
    {"manual-lv1", 9383096262780216318ull, 0, ""},
    {"manual-lv2", 7109759233311411085ull, 0, ""},
    {"manual-lv3", 8122122307668069772ull, 0, ""},
    {"manual-lv4", 5950161371042300237ull, 0, ""},
    {"manual-lv5", 13350672813022699901ull, 0, ""},
    {"manual-lv6", 13496737671064658873ull, 0, ""},
    {"baseline-lv1", 5365924001158309550ull, 0, ""},
    {"baseline-lv2", 11379686865813140455ull, 0, ""},
    {"baseline-lv3", 6791061375644652601ull, 0, ""},
    {"baseline-lv4", 11285820997262723752ull, 0, ""},
    {"baseline-lv5", 743413156355376538ull, 0, ""},
    {"baseline-lv6", 13820618487773645762ull, 0, ""},
    {"automatic-alignment", 6215222439694637483ull, 0, ""},
    {"timing-domain-empty", 4679187137633012666ull, 7,
        "MIDI generation produced no chart rows inside the lead-in/audio timing domain"},
    {"baseline-witness-error", 8598688527783776924ull, 7,
        "preferred lower-profile action has no source candidate at its native frame"},
    {"visible-growth-bound", 3998143965459286508ull, 8,
        "difficulty target band exceeds the maximum adjacent visible-profile growth"},
    {"selection-failure-witness", 3220803988927548756ull, 8,
        "no target-band state satisfies a coherent local-skill route"},
    {"near-feasible-witness", 15971850738242019385ull, 8,
        "near-feasible witness: rows=299 preferred=192 required_rows=297 required_preferred=192 "
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

    const WavAudio no_audio;
    std::vector<std::pair<std::string, Observation>> observations;
    std::array<Observation, 6> manual;
    for (int difficulty = 1; difficulty <= 6; ++difficulty) {
        manual[static_cast<std::size_t>(difficulty - 1)] =
            generate(midi_path, no_audio, config_for(difficulty));
        observations.emplace_back("manual-lv" + std::to_string(difficulty),
            manual[static_cast<std::size_t>(difficulty - 1)]);
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
        normalized.ticks_per_quarter != original.ticks_per_quarter ||
        std::bit_cast<std::uint64_t>(normalized.source_bpm) != std::bit_cast<std::uint64_t>(original.source_bpm) ||
        normalized.tempos.size() != original.tempos.size() || normalized.meters.size() != original.meters.size() ||
        normalized.notes.size() != original.notes.size() || !tempos_unchanged || !meters_unchanged || !notes_unchanged) {
        return fail("direct compilation diverged from the parent facade oracle or mutated normalized input");
    }
    std::string cleanup_error;
    if (!temporary.cleanup(&cleanup_error)) return fail("temporary cleanup failed: " + cleanup_error);
    return 0;
}
