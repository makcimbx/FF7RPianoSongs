#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "pipeline/midi_analysis_core.h"

namespace timing_tests {
namespace {

using ff7rp::pipeline::MidiMeterChange;
using ff7rp::pipeline::MidiTempoChange;
using ff7rp::pipeline::NormalizedMidiNoteEvent;

int fail(const std::string& message) {
    std::cerr << "midi_analysis_core_selftest timing: " << message << '\n';
    return 1;
}

bool near(const double actual, const double expected) {
    return std::fabs(actual - expected) <= 1e-12;
}

NormalizedMidiNoteEvent note(
    const int tick,
    const int end_tick,
    const int pitch,
    const int velocity,
    const int track,
    const int channel,
    const std::size_t ordinal,
    const double start,
    const double end,
    const double stream_prior) {
    return {{tick, end_tick, pitch, velocity, track, channel, ordinal},
        start, end, static_cast<double>(tick) / 480.0, stream_prior};
}

bool same_note(const NormalizedMidiNoteEvent& left, const NormalizedMidiNoteEvent& right) {
    return left.source == right.source && left.start == right.start && left.end == right.end &&
        left.beat == right.beat && left.stream_prior == right.stream_prior;
}

bool same_notes(
    const std::vector<NormalizedMidiNoteEvent>& left,
    const std::vector<NormalizedMidiNoteEvent>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_note(left[index], right[index])) return false;
    }
    return true;
}

int test_empty_and_parent_resolution_behavior() {
    using namespace ff7rp::pipeline;
    if (tempo_at_tick({}, 77) != 120.0) return fail("empty tempo map lost the parent default");
    if (humanization_window_ticks(0, {}, 0) != 1.0 ||
        humanization_window_ticks(-480, {}, 0) != 1.0) {
        return fail("invalid resolution changed the parent humanization floor");
    }
    const std::vector<MidiMeterChange> meters{{0, 4, 4, -1, -1, false}};
    if (metric_accent_at_tick(0, 0, meters) != 0.0 ||
        metric_accent_at_tick(0, -480, meters) != 0.0) {
        return fail("invalid resolution changed the parent zero-accent behavior");
    }
    std::size_t exact_groups = 9;
    std::size_t humanized_events = 9;
    const auto clusters = build_onset_clusters(
        {}, 0, {}, {}, &exact_groups, &humanized_events);
    if (!clusters.empty() || exact_groups != 0 || humanized_events != 0) {
        return fail("empty source did not remain empty at invalid resolution");
    }
    if (!build_onset_clusters({}, 0, {}, {}, nullptr, nullptr).empty()) {
        return fail("empty source with omitted counters did not remain empty");
    }
    return 0;
}

int test_tempo_lookup_and_windows() {
    using namespace ff7rp::pipeline;
    const std::vector<MidiTempoChange> tempos{
        {0, 60.0, 0, 0},
        {480, 240.0, 0, 1},
    };
    const auto original = tempos;
    if (tempo_at_tick(tempos, -1) != 120.0 || tempo_at_tick(tempos, 0) != 60.0 ||
        tempo_at_tick(tempos, 479) != 60.0 || tempo_at_tick(tempos, 480) != 240.0) {
        return fail("tempo lookup changed before, at, or after a tempo boundary");
    }
    if (!near(humanization_window_ticks(480, tempos, 0), 9.6) ||
        humanization_window_ticks(480, tempos, 480) != 15.0) {
        return fail("tempo-dependent humanization window changed");
    }
    if (tempos.size() != original.size()) return fail("tempo input size was mutated");
    for (std::size_t index = 0; index < tempos.size(); ++index) {
        if (tempos[index].tick != original[index].tick || tempos[index].bpm != original[index].bpm ||
            tempos[index].track != original[index].track ||
            tempos[index].ordinal != original[index].ordinal) {
            return fail("tempo input was mutated");
        }
    }
    return 0;
}

int test_meter_accents() {
    using namespace ff7rp::pipeline;
    const std::vector<MidiMeterChange> meters{
        {0, 3, 4, 0, 0, true},
        {1440, 6, 8, 0, 1, true},
    };
    const auto original = meters;
    if (metric_accent_at_tick(0, 480, meters) != 1.0 ||
        metric_accent_at_tick(240, 480, meters) != 0.20 ||
        metric_accent_at_tick(480, 480, meters) != 0.55 ||
        metric_accent_at_tick(720, 480, meters) != 0.20 ||
        metric_accent_at_tick(1440, 480, meters) != 1.0 ||
        metric_accent_at_tick(1560, 480, meters) != 0.20 ||
        metric_accent_at_tick(1680, 480, meters) != 0.55 ||
        metric_accent_at_tick(2880, 480, meters) != 1.0) {
        return fail("numerator/denominator accent phases changed");
    }
    if (meters.size() != original.size()) return fail("meter input size was mutated");
    for (std::size_t index = 0; index < meters.size(); ++index) {
        if (meters[index].tick != original[index].tick ||
            meters[index].numerator != original[index].numerator ||
            meters[index].denominator != original[index].denominator ||
            meters[index].track != original[index].track ||
            meters[index].ordinal != original[index].ordinal ||
            meters[index].explicit_event != original[index].explicit_event) {
            return fail("meter input was mutated");
        }
    }
    return 0;
}

int test_accidental_context_boundaries() {
    using namespace ff7rp::pipeline;
    const std::vector<MidiKeySignatureChange> signatures{
        {100, -2, false, 0, 0},
        {200, -3, false, 0, 1},
        {300, 2, false, 1, 0},
        {400, -1, false, 1, 1},
    };
    const auto timeline = build_midi_accidental_orientation_timeline(signatures);
    const MidiAccidentalOrientationChange* first = midi_accidental_context_at_tick(timeline, 199);
    const MidiAccidentalOrientationChange* second = midi_accidental_context_at_tick(timeline, 200);
    const MidiAccidentalOrientationChange* conflict = midi_accidental_context_at_tick(timeline, 300);
    const MidiAccidentalOrientationChange* resolved = midi_accidental_context_at_tick(timeline, 400);
    if (timeline.size() != 4u || midi_accidental_context_at_tick(timeline, 99) != nullptr
        || !first || first->tick != 100 || first->orientation != MidiAccidentalOrientation::Flat
        || !second || second->tick != 200 || second->orientation != MidiAccidentalOrientation::Flat
        || !conflict || conflict->tick != 300
        || conflict->orientation != MidiAccidentalOrientation::SharpFallback
        || !resolved || resolved->tick != 400
        || resolved->orientation != MidiAccidentalOrientation::Flat) {
        return fail("accidental context boundaries or per-track consensus changed");
    }
    return 0;
}

int test_source_order_and_humanization_boundaries() {
    using namespace ff7rp::pipeline;
    const std::vector<MidiTempoChange> tempos{{0, 120.0, -1, -1}};
    const std::vector<MidiMeterChange> meters{{0, 4, 4, -1, -1, false}};
    const std::vector<NormalizedMidiNoteEvent> source{
        note(16, 136, 67, 80, 0, 0, 4, 0.016, 0.136, 0.2),
        note(0, 120, 60, 110, 2, 0, 1, 0.002, 0.120, 0.7),
        note(15, 135, 65, 125, 0, 0, 3, 0.015, 0.315, 0.1),
        note(0, 90, 72, 100, 1, 0, 0, 0.001, 0.090, 0.9),
        note(14, 134, 64, 120, 0, 0, 2, 0.014, 0.214, 0.8),
    };
    const auto original_source = source;
    const auto original_tempos = tempos;
    const auto original_meters = meters;
    std::size_t exact_groups = 0;
    std::size_t humanized_events = 0;
    const auto clusters = build_onset_clusters(
        source, 480, tempos, meters, &exact_groups, &humanized_events);
    if (exact_groups != 4 || clusters.size() != 2 || humanized_events != 2) {
        return fail("inside/at/outside window counts changed");
    }
    if (clusters[0].anchor_tick != 0 || clusters[0].start != 0.001 ||
        clusters[0].beat != 0.0 || clusters[0].metric_accent != 1.0 ||
        clusters[0].notes.size() != 4 || clusters[1].anchor_tick != 16 ||
        !near(clusters[1].beat, 16.0 / 480.0)) {
        return fail("cluster anchor timing changed");
    }
    const std::vector<std::size_t> expected_ordinals{0, 1, 2, 3};
    for (std::size_t index = 0; index < expected_ordinals.size(); ++index) {
        if (clusters[0].notes[index].source.ordinal != expected_ordinals[index]) {
            return fail("same-tick/source identity order changed");
        }
    }
    if (!same_notes(source, original_source) || tempos.size() != original_tempos.size() ||
        meters.size() != original_meters.size()) {
        return fail("clustering mutated an input collection");
    }
    return 0;
}

int test_alignment_attack_derivation() {
    using namespace ff7rp::pipeline;
    OnsetCluster empty;
    OnsetCluster cluster;
    cluster.anchor_tick = 240;
    cluster.start = 0.25;
    cluster.beat = 0.5;
    cluster.metric_accent = 0.20;
    cluster.notes = {
        note(240, 360, 72, 110, 0, 0, 0, 0.251, 0.451, 0.9),
        note(240, 360, 60, 111, 0, 0, 1, 0.252, 0.352, 0.1),
        note(240, 360, 84, 111, 0, 0, 2, 0.253, 0.303, 0.1),
        note(240, 360, 50, 111, 0, 0, 3, 0.254, 0.294, 0.2),
        note(240, 360, 50, 111, 0, 0, 4, 0.255, 0.335, 0.2),
    };
    const std::vector<OnsetCluster> clusters{empty, cluster};
    const auto original_notes = clusters[1].notes;
    const auto attacks = build_alignment_attacks(clusters);
    if (attacks.size() != 1 || attacks[0].event.source.ordinal != 4 ||
        attacks[0].start != 0.255 || attacks[0].end != 0.335 ||
        attacks[0].beat != 0.5 || attacks[0].metric_accent != 0.20 ||
        attacks[0].melody_evidence != 0.0 || attacks[0].audio_prominence != 0.0 ||
        attacks[0].rhythmic_quality != 0.0 || attacks[0].fallback ||
        !attacks[0].chord_id.empty()) {
        return fail("alignment strongest-attack derivation changed");
    }
    if (!same_notes(clusters[1].notes, original_notes) || clusters[1].start != 0.25 ||
        clusters[1].beat != 0.5 || clusters[1].metric_accent != 0.20) {
        return fail("alignment derivation mutated its clusters");
    }
    return 0;
}

} // namespace

int run() {
    if (test_empty_and_parent_resolution_behavior() != 0) return 1;
    if (test_tempo_lookup_and_windows() != 0) return 1;
    if (test_meter_accents() != 0) return 1;
    if (test_accidental_context_boundaries() != 0) return 1;
    if (test_source_order_and_humanization_boundaries() != 0) return 1;
    if (test_alignment_attack_derivation() != 0) return 1;
    return 0;
}

} // namespace timing_tests

namespace melody_tests {
namespace {

using ff7rp::pipeline::Attack;
using ff7rp::pipeline::NormalizedMidiNoteEvent;
using ff7rp::pipeline::OnsetCluster;

int fail(const std::string& message) {
    std::cerr << "midi_analysis_core_selftest melody: " << message << '\n';
    return 1;
}

NormalizedMidiNoteEvent note(
    const int tick, const int end_tick, const int pitch, const int velocity,
    const int track, const int channel, const std::size_t ordinal,
    const double start, const double end, const double stream_prior) {
    return {{tick, end_tick, pitch, velocity, track, channel, ordinal},
        start, end, static_cast<double>(tick) / 480.0, stream_prior};
}

OnsetCluster cluster(
    const int tick, const double start, const double beat, const double accent,
    std::vector<NormalizedMidiNoteEvent> notes) {
    return {tick, start, beat, accent, std::move(notes)};
}

std::uint64_t bits(const double value) {
    return std::bit_cast<std::uint64_t>(value);
}

bool same_note(const NormalizedMidiNoteEvent& left, const NormalizedMidiNoteEvent& right) {
    return left.source == right.source && left.start == right.start && left.end == right.end &&
        left.beat == right.beat && left.stream_prior == right.stream_prior;
}

bool same_clusters(const std::vector<OnsetCluster>& left, const std::vector<OnsetCluster>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t cluster_index = 0; cluster_index < left.size(); ++cluster_index) {
        const OnsetCluster& a = left[cluster_index];
        const OnsetCluster& b = right[cluster_index];
        if (a.anchor_tick != b.anchor_tick || a.start != b.start || a.beat != b.beat ||
            a.metric_accent != b.metric_accent || a.notes.size() != b.notes.size()) {
            return false;
        }
        for (std::size_t note_index = 0; note_index < a.notes.size(); ++note_index) {
            if (!same_note(a.notes[note_index], b.notes[note_index])) return false;
        }
    }
    return true;
}

bool has_default_annotations(const Attack& attack) {
    return bits(attack.audio_prominence) == 0 && bits(attack.rhythmic_quality) == 0 &&
        !attack.fallback && attack.chord_id.empty();
}

int test_shared_scoring_bits_and_clamps() {
    using ff7rp::pipeline::midi_melody_scoring_detail::melody_evidence;
    using ff7rp::pipeline::midi_melody_scoring_detail::voice_emission;
    if (bits(voice_emission(72, 110, 0.51, 0.25, 0.8, 0.2)) !=
            0x400139015ee6c349ULL ||
        bits(melody_evidence(72, 110, 0.51, 0.25, 0.8)) !=
            0x3fe5335c7b8668fbULL) {
        return fail("shared scoring authority changed the parent projection bits");
    }
    if (bits(voice_emission(24, 0, 0.0, 2.0, 0.0, 0.0)) !=
            0x3fd0000000000000ULL ||
        bits(melody_evidence(24, 0, 0.0, 2.0, 0.0)) !=
            0x3fbeb851eb851eb8ULL ||
        bits(voice_emission(108, 127, 0.0, 0.75, 1.0, 1.0)) !=
            0x4009e66666666666ULL ||
        bits(melody_evidence(108, 127, 0.0, 0.75, 1.0)) !=
            0x3ff0000000000000ULL) {
        return fail("shared scoring authority changed clamp-boundary bits");
    }
    return 0;
}

int test_empty_and_final_empty() {
    using ff7rp::pipeline::track_midi_melody_voice;
    const auto empty = track_midi_melody_voice({});
    if (!empty.attacks.empty() || empty.stream_changes != 0) {
        return fail("empty input changed the parent caller-visible result");
    }
    const std::vector<OnsetCluster> clusters{
        cluster(0, 0.0, 0.0, 1.0, {note(0, 240, 60, 100, 0, 0, 1, 0.01, 0.25, 0.5)}),
        {},
    };
    const auto final_empty = track_midi_melody_voice(clusters);
    if (!final_empty.attacks.empty() || final_empty.stream_changes != 0) {
        return fail("empty final cluster changed the parent terminal-cluster behavior");
    }
    return 0;
}

int test_projection_defaults_anchor_end_and_immutability() {
    using ff7rp::pipeline::track_midi_melody_voice;
    const std::vector<OnsetCluster> clusters{
        cluster(240, 0.5, 0.75, 0.2,
            {note(240, 360, 72, 110, 2, 3, 7, 0.51, 0.25, 0.8)}),
    };
    const auto original = clusters;
    const auto result = track_midi_melody_voice(clusters);
    if (result.attacks.size() != 1 || result.stream_changes != 0) {
        return fail("single-cluster projection count changed");
    }
    const Attack& attack = result.attacks[0];
    if (!(attack.event.source == clusters[0].notes[0].source) ||
        bits(attack.start) != 0x3fe0000000000000ULL ||
        bits(attack.end) != 0x3fe0000000000000ULL ||
        bits(attack.beat) != 0x3fe8000000000000ULL ||
        bits(attack.metric_accent) != 0x3fc999999999999aULL ||
        bits(attack.melody_evidence) != 0x3fe5335c7b8668fbULL ||
        !has_default_annotations(attack)) {
        return fail("parent-derived projection bits or default fields changed");
    }
    if (!same_note(attack.event, clusters[0].notes[0])) {
        return fail("projected attack did not retain the complete source event");
    }
    if (!same_clusters(clusters, original)) return fail("projection mutated its input clusters");
    return 0;
}

int test_tie_and_epsilon_retention() {
    using ff7rp::pipeline::track_midi_melody_voice;
    const auto run = [](const double first_prior) {
        return track_midi_melody_voice({
            cluster(0, 0.0, 0.0, 1.0, {
                note(0, 240, 60, 100, 0, 0, 10, 0.0, 0.25, first_prior),
                note(0, 240, 60, 100, 0, 0, 11, 0.0, 0.25, 0.5),
            }),
            cluster(480, 0.5, 1.0, 0.55,
                {note(480, 720, 62, 100, 0, 0, 20, 0.5, 0.75, 0.5)}),
        });
    };
    const auto exact_tie = run(0.5);
    const auto below_epsilon = run(0.5000000001);
    for (const auto* result : {&exact_tie, &below_epsilon}) {
        if (result->attacks.size() != 2 || result->stream_changes != 0 ||
            result->attacks[0].event.source.ordinal != 11 ||
            result->attacks[1].event.source.ordinal != 20 ||
            bits(result->attacks[0].melody_evidence) != 0x3fe0f9a1572933b0ULL ||
            bits(result->attacks[1].melody_evidence) != 0x3fe14b8cdc47ec02ULL) {
            return fail("reverse traversal tie or comparison-epsilon retention changed");
        }
    }
    return 0;
}

std::vector<OnsetCluster> crossing_fixture() {
    std::vector<OnsetCluster> clusters;
    const int pitches[5][2] = {{60, 72}, {62, 70}, {64, 68}, {65, 67}, {68, 64}};
    for (int index = 0; index < 5; ++index) {
        clusters.push_back(cluster(
            index * 480, index * 0.5, static_cast<double>(index), index == 0 ? 1.0 : 0.55, {
                note(index * 480, index * 480 + 360, pitches[index][0], 100, 0, 0,
                    static_cast<std::size_t>(index * 2), index * 0.5,
                    index * 0.5 + 0.375, 0.5),
                note(index * 480, index * 480 + 360, pitches[index][1], 100, 0, 0,
                    static_cast<std::size_t>(index * 2 + 1), index * 0.5,
                    index * 0.5 + 0.375, 0.5),
            }));
    }
    return clusters;
}

int test_crossing_voice_order_and_bits() {
    using ff7rp::pipeline::track_midi_melody_voice;
    const auto clusters = crossing_fixture();
    const auto original = clusters;
    const auto result = track_midi_melody_voice(clusters);
    const std::size_t ordinals[]{1, 3, 5, 7, 9};
    const int pitches[]{72, 70, 68, 67, 64};
    const std::uint64_t evidence[]{
        0x3fe388fd801ef63eULL, 0x3fe33711fb003decULL, 0x3fe2e52675e1859aULL,
        0x3fe2bc30b3522972ULL, 0x3fe2414f6ba414f6ULL,
    };
    if (result.attacks.size() != 5 || result.stream_changes != 0) {
        return fail("crossing-voice path count changed");
    }
    for (std::size_t index = 0; index < result.attacks.size(); ++index) {
        const Attack& attack = result.attacks[index];
        if (attack.event.source.ordinal != ordinals[index] ||
            attack.event.source.pitch != pitches[index] ||
            bits(attack.melody_evidence) != evidence[index] || !has_default_annotations(attack)) {
            return fail("parent-derived crossing-voice identity, order, or evidence changed");
        }
    }
    if (!same_clusters(clusters, original)) return fail("crossing tracking mutated its input");
    return 0;
}

int test_stream_changes_reversal_and_overlap() {
    using ff7rp::pipeline::track_midi_melody_voice;
    const auto stream = track_midi_melody_voice({
        cluster(0, 0.0, 0.0, 1.0, {note(0, 240, 60, 100, 0, 0, 0, 0.0, 0.25, 0.5)}),
        cluster(480, 0.5, 1.0, 0.55, {note(480, 720, 62, 100, 0, 0, 1, 0.5, 0.75, 0.5)}),
        cluster(960, 1.0, 2.0, 0.55, {note(960, 1200, 64, 100, 1, 0, 2, 1.0, 1.25, 0.5)}),
        cluster(1440, 1.5, 3.0, 0.55, {note(1440, 1680, 65, 100, 1, 1, 3, 1.5, 1.75, 0.5)}),
    });
    if (stream.attacks.size() != 4 || stream.stream_changes != 2) {
        return fail("track/channel stream-change counter changed");
    }
    const auto reversal = track_midi_melody_voice({
        cluster(0, 0.0, 0.0, 1.0, {note(0, 240, 60, 100, 0, 0, 0, 0.0, 0.25, 0.5)}),
        cluster(480, 0.5, 1.0, 0.55, {note(480, 720, 70, 100, 0, 0, 1, 0.5, 0.75, 0.5)}),
        cluster(960, 1.0, 2.0, 0.55, {
            note(960, 1200, 60, 100, 0, 0, 2, 1.0, 1.25, 0.5),
            note(960, 1200, 72, 100, 0, 0, 3, 1.0, 1.25, 0.5),
        }),
    });
    if (reversal.attacks.size() != 3 || reversal.attacks.back().event.source.ordinal != 3 ||
        bits(reversal.attacks.back().melody_evidence) != 0x3fe2e52675e1859bULL) {
        return fail("direction-reversal penalty changed the parent path");
    }
    const auto overlap = track_midi_melody_voice({
        cluster(0, 0.0, 0.0, 1.0, {
            note(0, 479, 60, 100, 0, 0, 0, 0.0, 0.25, 0.5),
            note(0, 480, 60, 100, 0, 0, 1, 0.0, 0.25, 0.5),
        }),
        cluster(480, 0.5, 1.0, 0.55,
            {note(480, 720, 62, 100, 0, 0, 2, 0.5, 0.75, 0.5)}),
    });
    if (overlap.attacks.size() != 2 || overlap.attacks[0].event.source.ordinal != 1 ||
        overlap.attacks[0].event.source.end_tick != 480) {
        return fail("sustained-overlap transition bonus changed the parent path");
    }
    return 0;
}

int test_lookback_boundary() {
    using ff7rp::pipeline::track_midi_melody_voice;
    std::vector<OnsetCluster> gap64(65);
    gap64[0] = cluster(0, 0.0, 0.0, 1.0,
        {note(0, 240, 60, 100, 0, 0, 0, 0.0, 0.25, 0.5)});
    gap64[64] = cluster(30720, 32.0, 64.0, 1.0,
        {note(30720, 30960, 62, 100, 0, 0, 1, 32.0, 32.25, 0.5)});
    const auto accepted = track_midi_melody_voice(gap64);
    if (accepted.attacks.size() != 2 || accepted.attacks[1].event.source.tick != 30720 ||
        bits(accepted.attacks[1].start) != 0x4040000000000000ULL) {
        return fail("inclusive 64-cluster lookback changed");
    }
    std::vector<OnsetCluster> gap65(66);
    gap65[0] = gap64[0];
    gap65[65] = cluster(31200, 32.5, 65.0, 0.55,
        {note(31200, 31440, 62, 100, 0, 0, 1, 32.5, 32.75, 0.5)});
    const auto rejected = track_midi_melody_voice(gap65);
    if (!rejected.attacks.empty() || rejected.stream_changes != 0) {
        return fail("65-cluster lookback rejection changed");
    }
    return 0;
}

} // namespace

int run() {
    if (test_shared_scoring_bits_and_clamps() != 0) return 1;
    if (test_empty_and_final_empty() != 0) return 1;
    if (test_projection_defaults_anchor_end_and_immutability() != 0) return 1;
    if (test_tie_and_epsilon_retention() != 0) return 1;
    if (test_crossing_voice_order_and_bits() != 0) return 1;
    if (test_stream_changes_reversal_and_overlap() != 0) return 1;
    if (test_lookback_boundary() != 0) return 1;
    return 0;
}

} // namespace melody_tests

namespace alignment_tests {
namespace {

using ff7rp::pipeline::Attack;
using ff7rp::pipeline::MidiAudioAlignmentResult;
using ff7rp::pipeline::WavAudio;

int fail(const std::string& message) {
    std::cerr << "midi_analysis_core_selftest alignment: " << message << '\n';
    return 1;
}

Attack attack(const double start, const int pitch, const int velocity, const std::size_t ordinal = 0) {
    Attack value;
    value.event.source.pitch = pitch;
    value.event.source.velocity = velocity;
    value.event.source.ordinal = ordinal;
    value.event.start = start;
    value.event.end = start + 0.25;
    value.start = start;
    value.end = start + 0.25;
    return value;
}

WavAudio silent_audio(const std::uint32_t sample_rate, const double seconds) {
    WavAudio audio;
    audio.sample_rate = sample_rate;
    audio.channels = 2;
    audio.source_frame_count = static_cast<std::size_t>(std::llround(seconds * sample_rate));
    audio.stereo_samples.assign(audio.source_frame_count * 2, 0.0f);
    return audio;
}

void add_tone(WavAudio* audio, const double start_seconds, const int pitch, const double gain = 0.3) {
    const double frequency = 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
    const std::size_t start = static_cast<std::size_t>(std::llround(start_seconds * audio->sample_rate));
    const std::size_t length = static_cast<std::size_t>(0.12 * audio->sample_rate);
    for (std::size_t frame = 0; frame < length && start + frame < audio->frame_count(); ++frame) {
        const double seconds = static_cast<double>(frame) / audio->sample_rate;
        const double envelope = std::min(1.0, seconds / 0.06) * std::exp(-8.0 * seconds);
        const float sample = static_cast<float>(gain * envelope *
            std::sin(2.0 * 3.14159265358979323846 * frequency * seconds));
        audio->stereo_samples[(start + frame) * 2] += sample;
        audio->stereo_samples[(start + frame) * 2 + 1] += sample;
    }
}

bool same_audio(const WavAudio& left, const WavAudio& right) {
    return left.sample_rate == right.sample_rate && left.channels == right.channels &&
        left.source_frame_count == right.source_frame_count &&
        left.stereo_samples == right.stereo_samples;
}

bool same_attack(const Attack& left, const Attack& right) {
    return left.event.source == right.event.source && left.event.start == right.event.start &&
        left.event.end == right.event.end && left.event.beat == right.event.beat &&
        left.event.stream_prior == right.event.stream_prior && left.start == right.start &&
        left.end == right.end && left.beat == right.beat &&
        left.metric_accent == right.metric_accent &&
        left.melody_evidence == right.melody_evidence &&
        left.audio_prominence == right.audio_prominence &&
        left.rhythmic_quality == right.rhythmic_quality && left.fallback == right.fallback &&
        left.chord_id == right.chord_id;
}

bool same_attacks(const std::vector<Attack>& left, const std::vector<Attack>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_attack(left[index], right[index])) return false;
    }
    return true;
}

bool has_bits(const double value, const std::uint64_t expected) {
    return std::bit_cast<std::uint64_t>(value) == expected;
}

int expect_result(
    const std::string& name,
    const MidiAudioAlignmentResult& result,
    const std::uint64_t seconds,
    const std::uint64_t confidence) {
    if (!has_bits(result.seconds, seconds) || !has_bits(result.confidence, confidence)) {
        return fail(name + " changed from its parent bit oracle");
    }
    return 0;
}

int test_invalid_and_silent_inputs() {
    using namespace ff7rp::pipeline;
    const Attack base = attack(1.0, 60, 100);
    WavAudio audio = silent_audio(2000, 4.0);
    if (expect_result("empty attacks", estimate_midi_audio_alignment(audio, {}), 0, 0) != 0 ||
        expect_result("empty audio", estimate_midi_audio_alignment(WavAudio{}, {base}), 0, 0) != 0) {
        return 1;
    }
    WavAudio zero_rate = audio;
    zero_rate.sample_rate = 0;
    WavAudio non_stereo = audio;
    non_stereo.channels = 1;
    if (expect_result("zero rate", estimate_midi_audio_alignment(zero_rate, {base}), 0, 0) != 0 ||
        expect_result("non-stereo", estimate_midi_audio_alignment(non_stereo, {base}), 0, 0) != 0 ||
        expect_result("silence", estimate_midi_audio_alignment(audio, {base}), 0, 0) != 0) {
        return 1;
    }
    return 0;
}

int test_alignment_search_oracles() {
    using namespace ff7rp::pipeline;
    const Attack base = attack(1.0, 60, 100);
    WavAudio aligned = silent_audio(2000, 4.0);
    add_tone(&aligned, 1.12, 60);
    if (expect_result("aligned", estimate_midi_audio_alignment(aligned, {base}),
        0x3fbd916872b020c6, 0x3fe23352570fb9cc) != 0) return 1;

    WavAudio negative_limit = silent_audio(2000, 4.0);
    add_tone(&negative_limit, 0.52, 60);
    if (expect_result("negative limit", estimate_midi_audio_alignment(
        negative_limit, {attack(1.5, 60, 100)}),
        0xbfef810624dd2f1a, 0x3fe23352570fb9cc) != 0) return 1;

    WavAudio positive_limit = silent_audio(2000, 4.0);
    add_tone(&positive_limit, 2.48, 60);
    if (expect_result("positive limit", estimate_midi_audio_alignment(
        positive_limit, {attack(1.5, 60, 100)}),
        0x3fef1eb851eb851f, 0x3fe22589cabe2cd6) != 0) return 1;

    WavAudio competing = silent_audio(2000, 4.0);
    add_tone(&competing, 0.8, 60);
    add_tone(&competing, 2.2, 60);
    if (expect_result("competing equal peaks", estimate_midi_audio_alignment(
        competing, {attack(1.5, 60, 100)}),
        0xbfe68b4395810625, 0x3f6fd69f0486dbfc) != 0) return 1;
    return 0;
}

int test_anchor_cap_and_tie_oracles() {
    using namespace ff7rp::pipeline;
    std::vector<Attack> attacks(193, attack(-10.0, 48, 1));
    for (std::size_t index = 0; index < attacks.size(); ++index) {
        attacks[index].event.source.ordinal = index;
    }
    attacks[191] = attack(1.5, 60, 50, 191);
    attacks[192] = attack(1.5, 72, 100, 192);
    WavAudio audio = silent_audio(2000, 4.0);
    add_tone(&audio, 1.62, 72);
    if (expect_result("anchor strongest velocity", estimate_midi_audio_alignment(audio, attacks),
        0x3fbd916872b020c6, 0x3fe22ad9183d182a) != 0) return 1;
    attacks[191].event.source.velocity = 100;
    if (expect_result("anchor equal velocity", estimate_midi_audio_alignment(audio, attacks),
        0x3fbd916872b020c6, 0x3fe46f096341a7db) != 0) return 1;
    return 0;
}

int test_novelty_bounds_and_oracles() {
    using namespace ff7rp::pipeline;
    WavAudio boundary = silent_audio(2000, 1.0);
    add_tone(&boundary, 0.055, 60);
    add_tone(&boundary, 0.945, 60);
    if (!has_bits(measure_midi_attack_audio_novelty(
            boundary, attack(0.054, 60, 100), 0.0), 0) ||
        !has_bits(measure_midi_attack_audio_novelty(
            boundary, attack(0.055, 60, 100), 0.0), 0x4007470f0480651d) ||
        !has_bits(measure_midi_attack_audio_novelty(
            boundary, attack(0.945, 60, 100), 0.0), 0x4007470f0480651d) ||
        !has_bits(measure_midi_attack_audio_novelty(
            boundary, attack(0.946, 60, 100), 0.0), 0)) {
        return fail("novelty window bounds changed from parent bit oracles");
    }
    WavAudio centered = silent_audio(2000, 4.0);
    add_tone(&centered, 1.12, 60);
    if (!has_bits(measure_midi_attack_audio_novelty(
        centered, attack(1.0, 60, 100), 0.12), 0x4007470f0480651d)) {
        return fail("centered novelty changed from parent bit oracle");
    }
    WavAudio zero_rate = centered;
    zero_rate.sample_rate = 0;
    WavAudio non_stereo = centered;
    non_stereo.channels = 1;
    if (measure_midi_attack_audio_novelty(zero_rate, attack(1.0, 60, 100), 0.12) != 0.0 ||
        measure_midi_attack_audio_novelty(non_stereo, attack(1.0, 60, 100), 0.12) != 0.0) {
        return fail("invalid audio no longer returns zero novelty");
    }
    return 0;
}

int test_input_immutability() {
    using namespace ff7rp::pipeline;
    WavAudio audio = silent_audio(2000, 4.0);
    add_tone(&audio, 1.12, 60);
    std::vector<Attack> attacks{attack(1.0, 60, 100, 3), attack(1.5, 64, 80, 4)};
    attacks[0].metric_accent = 0.55;
    attacks[0].melody_evidence = 0.75;
    attacks[0].audio_prominence = 1.25;
    attacks[0].rhythmic_quality = -0.5;
    attacks[0].fallback = true;
    attacks[0].chord_id = "pca_C_Maj";
    const WavAudio original_audio = audio;
    const std::vector<Attack> original_attacks = attacks;
    (void)estimate_midi_audio_alignment(audio, attacks);
    (void)measure_midi_attack_audio_novelty(audio, attacks[0], 0.12);
    if (!same_audio(audio, original_audio) || !same_attacks(attacks, original_attacks)) {
        return fail("alignment measurement mutated an input");
    }
    return 0;
}

} // namespace

int run() {
    if (test_invalid_and_silent_inputs() != 0) return 1;
    if (test_alignment_search_oracles() != 0) return 1;
    if (test_anchor_cap_and_tie_oracles() != 0) return 1;
    if (test_novelty_bounds_and_oracles() != 0) return 1;
    if (test_input_immutability() != 0) return 1;
    return 0;
}

} // namespace alignment_tests

int main() {
    if (timing_tests::run() != 0) return 1;
    if (melody_tests::run() != 0) return 1;
    if (alignment_tests::run() != 0) return 1;
    std::cout << "midi_analysis_core_selftest ok\n";
    return 0;
}
