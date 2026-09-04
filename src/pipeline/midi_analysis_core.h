#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "midi_source_normalizer.h"
#include "song_types.h"

namespace ff7rp::pipeline {

struct OnsetCluster {
    int anchor_tick = 0;
    double start = 0.0;
    double beat = 0.0;
    double metric_accent = 0.0;
    std::vector<NormalizedMidiNoteEvent> notes;
};

struct Attack {
    NormalizedMidiNoteEvent event;
    double start = 0.0;
    double end = 0.0;
    double beat = 0.0;
    double metric_accent = 0.0;
    double melody_evidence = 0.0;
    double audio_prominence = 0.0;
    double rhythmic_quality = 0.0;
    bool fallback = false;
    std::string chord_id;
    std::vector<std::string> ignore_sound_pitches;
    std::vector<std::string> source_chord_pitches;
    std::vector<MidiSourceIdentity> chord_sources;
};

struct MidiMelodyTrackingResult {
    std::vector<Attack> attacks;
    std::size_t stream_changes = 0;
};

struct MidiAudioAlignmentResult {
    double seconds = 0.0;
    double confidence = 0.0;
};

enum class MidiAccidentalOrientation : std::uint8_t {
    SharpFallback = 0,
    Flat = 1,
};

struct MidiAccidentalOrientationChange {
    int tick = 0;
    MidiAccidentalOrientation orientation = MidiAccidentalOrientation::SharpFallback;
};

std::vector<MidiAccidentalOrientationChange> build_midi_accidental_orientation_timeline(
    const std::vector<MidiKeySignatureChange>& changes);

const MidiAccidentalOrientationChange* midi_accidental_context_at_tick(
    const std::vector<MidiAccidentalOrientationChange>& timeline, int tick);

MidiAccidentalOrientation midi_accidental_orientation_at_tick(
    const std::vector<MidiAccidentalOrientationChange>& timeline, int tick);

double tempo_at_tick(const std::vector<MidiTempoChange>& changes, int tick);

double humanization_window_ticks(
    int ticks_per_quarter,
    const std::vector<MidiTempoChange>& tempos,
    int anchor_tick);

double metric_accent_at_tick(
    int tick,
    int ticks_per_quarter,
    const std::vector<MidiMeterChange>& meters);

std::vector<OnsetCluster> build_onset_clusters(
    const std::vector<NormalizedMidiNoteEvent>& source,
    int ticks_per_quarter,
    const std::vector<MidiTempoChange>& tempos,
    const std::vector<MidiMeterChange>& meters,
    std::size_t* exact_group_count,
    std::size_t* humanized_event_count);

std::vector<Attack> build_alignment_attacks(const std::vector<OnsetCluster>& clusters);

MidiMelodyTrackingResult track_midi_melody_voice(
    const std::vector<OnsetCluster>& clusters);

MidiAudioAlignmentResult estimate_midi_audio_alignment(
    const WavAudio& audio,
    const std::vector<Attack>& attacks);

double measure_midi_attack_audio_novelty(
    const WavAudio& audio,
    const Attack& attack,
    double alignment_seconds);

} // namespace ff7rp::pipeline

namespace ff7rp::pipeline::midi_melody_scoring_detail {

double voice_emission(
    int pitch,
    int velocity,
    double start,
    double end,
    double stream_prior,
    double metric_accent);

double melody_evidence(
    int pitch,
    int velocity,
    double start,
    double end,
    double stream_prior);

} // namespace ff7rp::pipeline::midi_melody_scoring_detail
