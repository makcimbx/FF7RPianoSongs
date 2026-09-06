#include "midi_source_normalizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <tuple>
#include <utility>

#include "MidiFile.h"

namespace ff7rp::pipeline {
namespace {

constexpr double kComparisonEpsilon = 1e-9;

Status validate_midi_format(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 14> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const std::streamsize bytes_read = input.gcount();
    if (bytes_read < 10 ||
        header[0] != 'M' || header[1] != 'T' || header[2] != 'h' || header[3] != 'd') {
        return Status::error(StatusCode::InvalidMidi, "MIDI file has no valid MThd header: " + path);
    }
    const unsigned int header_size = (static_cast<unsigned int>(header[4]) << 24u) |
        (static_cast<unsigned int>(header[5]) << 16u) |
        (static_cast<unsigned int>(header[6]) << 8u) | static_cast<unsigned int>(header[7]);
    if (header_size < 6u) {
        return Status::error(StatusCode::InvalidMidi, "MIDI header is shorter than the SMF header");
    }
    if (bytes_read < static_cast<std::streamsize>(header.size())) {
        return Status::error(StatusCode::InvalidMidi, "MIDI header is shorter than the SMF header");
    }
    const int format = static_cast<int>((static_cast<unsigned int>(header[8]) << 8u) | header[9]);
    if (format == 2) {
        return Status::error(StatusCode::InvalidMidi,
            "MIDI format 2 contains independent sequences and is not supported");
    }
    if (format != 0 && format != 1) {
        return Status::error(StatusCode::InvalidMidi, "unsupported MIDI format " + std::to_string(format));
    }
    const unsigned int division = (static_cast<unsigned int>(header[12]) << 8u) | header[13];
    if ((division & 0x8000u) != 0u) {
        return Status::error(StatusCode::InvalidMidi, "SMPTE MIDI timing is not supported");
    }
    return Status::ok_status();
}

double median(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if ((values.size() & 1u) != 0u) return static_cast<double>(values[middle]);
    return (static_cast<double>(values[middle - 1]) + values[middle]) * 0.5;
}

void assign_stream_priors(std::vector<NormalizedMidiNoteEvent>* notes) {
    if (!notes || notes->empty()) return;
    using Stream = std::pair<int, int>;
    std::map<Stream, std::vector<int>> pitches;
    for (const NormalizedMidiNoteEvent& note : *notes) {
        pitches[{note.source.track, note.source.channel}].push_back(note.source.pitch);
    }
    std::vector<std::pair<Stream, double>> ordered;
    ordered.reserve(pitches.size());
    for (const auto& entry : pitches) ordered.emplace_back(entry.first, median(entry.second));
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        if (std::fabs(a.second - b.second) > kComparisonEpsilon) return a.second < b.second;
        return a.first < b.first;
    });
    std::map<Stream, double> priors;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const double rank = ordered.size() == 1 ? 0.5 :
            static_cast<double>(i) / static_cast<double>(ordered.size() - 1);
        const double pitch_prior = 1.0 / (1.0 + std::exp(-(ordered[i].second - 60.0) / 8.0));
        priors[ordered[i].first] = 0.65 * rank + 0.35 * pitch_prior;
    }
    for (NormalizedMidiNoteEvent& note : *notes) {
        note.stream_prior = priors[{note.source.track, note.source.channel}];
    }
}

} // namespace

Status normalize_midi_source(const std::string& path, NormalizedMidiSource* out_source) {
    if (!out_source) return Status::error(StatusCode::InvalidArgument, "out_source must not be null");
    const Status format_status = validate_midi_format(path);
    if (!format_status.ok()) return format_status;
    smf::MidiFile midi;
    if (!midi.read(path) || !midi.status()) {
        return Status::error(StatusCode::InvalidMidi, "failed to parse MIDI file: " + path);
    }
    midi.makeAbsoluteTicks();
    const int ticks_per_quarter = midi.getTicksPerQuarterNote();
    if (ticks_per_quarter <= 0) {
        return Status::error(StatusCode::InvalidMidi, "SMPTE MIDI timing is not supported");
    }

    std::vector<MidiTempoChange> tempos{{0, 120.0, -1, -1}};
    std::vector<MidiMeterChange> meters{{0, 4, 4, -1, -1, false}};
    std::vector<MidiKeySignatureChange> key_signatures;
    for (int track = 0; track < midi.getTrackCount(); ++track) {
        for (int ordinal = 0; ordinal < midi.getEventCount(track); ++ordinal) {
            const smf::MidiEvent& event = midi[track][ordinal];
            if (event.isTempo()) {
                const double bpm = event.getTempoBPM();
                if (std::isfinite(bpm) && bpm > 0.0) tempos.push_back({event.tick, bpm, track, ordinal});
            }
            if (event.isTimeSignature() && event.size() >= 7) {
                const int numerator = event[3];
                const int exponent = event[4];
                if (numerator > 0 && exponent >= 0 && exponent <= 6) {
                    meters.push_back({event.tick, numerator, 1 << exponent, track, ordinal, true});
                }
            }
            if (event.isMetaMessage() && event.getMetaType() == 0x59) {
                if (!event.isKeySignature() || event.size() != 5u || event[2] != 2u) {
                    return Status::error(StatusCode::InvalidMidi,
                        "MIDI key-signature event has malformed shape");
                }
                const int fifths = event[3] <= 127u ? static_cast<int>(event[3]) :
                    static_cast<int>(event[3]) - 256;
                const int mode = event[4];
                if (fifths < -7 || fifths > 7 || (mode != 0 && mode != 1)) {
                    return Status::error(StatusCode::InvalidMidi,
                        "MIDI key-signature event has invalid fifths or mode");
                }
                key_signatures.push_back({event.tick, fifths, mode == 1, track, ordinal});
            }
        }
    }
    const auto change_less = [](const auto& a, const auto& b) {
        return std::tie(a.tick, a.track, a.ordinal) < std::tie(b.tick, b.track, b.ordinal);
    };
    std::sort(tempos.begin(), tempos.end(), change_less);
    std::sort(meters.begin(), meters.end(), change_less);
    std::sort(key_signatures.begin(), key_signatures.end(), change_less);
    std::vector<MidiTempoChange> collapsed_tempos;
    for (const MidiTempoChange& change : tempos) {
        if (!collapsed_tempos.empty() && collapsed_tempos.back().tick == change.tick) {
            collapsed_tempos.back() = change;
        } else {
            collapsed_tempos.push_back(change);
        }
    }
    std::vector<MidiMeterChange> collapsed_meters;
    for (const MidiMeterChange& change : meters) {
        if (!collapsed_meters.empty() && collapsed_meters.back().tick == change.tick) {
            collapsed_meters.back() = change;
        } else {
            collapsed_meters.push_back(change);
        }
    }
    tempos = std::move(collapsed_tempos);
    meters = std::move(collapsed_meters);
    double source_bpm = 120.0;
    for (const MidiTempoChange& change : tempos) {
        if (change.track >= 0) {
            source_bpm = change.bpm;
            break;
        }
    }

    midi.doTimeAnalysis();
    midi.linkNotePairs();
    std::vector<NormalizedMidiNoteEvent> source;
    std::size_t source_ordinal = 0;
    std::size_t unsupported_pitch_events = 0;
    int unsupported_pitch_min = 128;
    int unsupported_pitch_max = -1;
    for (int track = 0; track < midi.getTrackCount(); ++track) {
        for (int event_index = 0; event_index < midi.getEventCount(track); ++event_index) {
            const smf::MidiEvent& event = midi[track][event_index];
            if (!event.isNoteOn()) continue;
            const std::size_t ordinal = source_ordinal++;
            if (event.getChannel() == 9 || !event.isLinked()) continue;
            const smf::MidiEvent* linked = event.getLinkedEvent();
            const int pitch = event.getKeyNumber();
            const double duration = event.getDurationInSeconds();
            if (!linked || duration <= 0.0 ||
                !std::isfinite(event.seconds) || !std::isfinite(duration)) {
                continue;
            }
            if (pitch < 24 || pitch > 96) {
                ++unsupported_pitch_events;
                unsupported_pitch_min = std::min(unsupported_pitch_min, pitch);
                unsupported_pitch_max = std::max(unsupported_pitch_max, pitch);
                continue;
            }
            NormalizedMidiNoteEvent note;
            note.source = {event.tick, linked->tick, pitch, event.getVelocity(),
                track, event.getChannel(), ordinal};
            note.start = event.seconds;
            note.end = event.seconds + duration;
            note.beat = static_cast<double>(event.tick) / ticks_per_quarter;
            source.push_back(std::move(note));
        }
    }
    NormalizedMidiSource normalized;
    normalized.unsupported_pitch_events = unsupported_pitch_events;
    normalized.unsupported_pitch_min = unsupported_pitch_min;
    normalized.unsupported_pitch_max = unsupported_pitch_max;
    if (source.empty()) {
        return Status::error(StatusCode::InvalidMidi,
            "MIDI contains no supported pitched notes in C1-C7" +
                (unsupported_pitch_events == 0 ? std::string{} :
                    "; " + midi_pitch_exclusion_warning(normalized)));
    }
    assign_stream_priors(&source);

    normalized.ticks_per_quarter = ticks_per_quarter;
    normalized.source_bpm = source_bpm;
    normalized.tempos = std::move(tempos);
    normalized.meters = std::move(meters);
    normalized.key_signatures = std::move(key_signatures);
    normalized.notes = std::move(source);
    *out_source = std::move(normalized);
    return Status::ok_status();
}

std::string midi_pitch_exclusion_warning(const NormalizedMidiSource& source) {
    if (source.unsupported_pitch_events == 0) return {};
    return "excluded=" + std::to_string(source.unsupported_pitch_events) +
        " total_linked_pitched_attacks=" +
        std::to_string(source.notes.size() + source.unsupported_pitch_events) +
        " excluded_midi_range=" + std::to_string(source.unsupported_pitch_min) + ".." +
        std::to_string(source.unsupported_pitch_max) +
        " supported_midi_range=24..96 (C1-C7); unsupported attacks excluded without transposition; "
        "edit the source MIDI to retain those attacks; remaining profiles still require validation";
}

} // namespace ff7rp::pipeline
