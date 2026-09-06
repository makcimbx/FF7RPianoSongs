#pragma once

#include <cstddef>
#include <string>
#include <tuple>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline {

struct MidiSourceIdentity {
    int tick = 0;
    int end_tick = 0;
    int pitch = 0;
    int velocity = 0;
    int track = 0;
    int channel = 0;
    std::size_t ordinal = 0;

    bool operator<(const MidiSourceIdentity& other) const {
        return std::tie(tick, end_tick, pitch, velocity, track, channel, ordinal) <
            std::tie(other.tick, other.end_tick, other.pitch, other.velocity,
                other.track, other.channel, other.ordinal);
    }

    bool operator==(const MidiSourceIdentity& other) const {
        return std::tie(tick, end_tick, pitch, velocity, track, channel, ordinal) ==
            std::tie(other.tick, other.end_tick, other.pitch, other.velocity,
                other.track, other.channel, other.ordinal);
    }
};

struct NormalizedMidiNoteEvent {
    MidiSourceIdentity source;
    double start = 0.0;
    double end = 0.0;
    double beat = 0.0;
    double stream_prior = 0.5;
};

struct MidiTempoChange {
    int tick = 0;
    double bpm = 120.0;
    int track = 0;
    int ordinal = 0;
};

struct MidiMeterChange {
    int tick = 0;
    int numerator = 4;
    int denominator = 4;
    int track = 0;
    int ordinal = 0;
    bool explicit_event = false;
};

struct MidiKeySignatureChange {
    int tick = 0;
    int fifths = 0;
    bool minor = false;
    int track = 0;
    int ordinal = 0;
};

struct NormalizedMidiSource {
    int ticks_per_quarter = 0;
    double source_bpm = 120.0;
    std::vector<MidiTempoChange> tempos;
    std::vector<MidiMeterChange> meters;
    std::vector<MidiKeySignatureChange> key_signatures;
    std::vector<NormalizedMidiNoteEvent> notes;
    std::size_t unsupported_pitch_events = 0;
    int unsupported_pitch_min = 128;
    int unsupported_pitch_max = -1;
};

Status normalize_midi_source(const std::string& path, NormalizedMidiSource* out_source);
// Nonterminal source diagnostic, not a promise that difficulty selection will succeed.
std::string midi_pitch_exclusion_warning(const NormalizedMidiSource& source);

} // namespace ff7rp::pipeline
