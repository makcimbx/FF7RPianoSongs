#include "audio_metronome.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>

#include "MidiFile.h"

namespace ff7rp::pipeline {
namespace {

constexpr double kComparisonEpsilon = 1e-9;
constexpr std::size_t kMaximumMetronomeBeats = 100000;
// The original guide residual is a dull wooden impact dominated by 586 Hz,
// with sub-resonances and a short, band-limited noisy strike.
constexpr double kPrimaryResonanceHz = 586.0;
constexpr double kSubResonanceHz = 293.0;
constexpr double kLowResonanceHz = 152.0;
constexpr double kClickDurationSeconds = 0.055;
constexpr double kClickAttackSeconds = 0.00035;

struct TempoChange {
    double tick = 0.0;
    double bpm = 120.0;
    int track = -1;
    int ordinal = -1;
    double seconds = 0.0;
};

struct MeterChange {
    double tick = 0.0;
    int numerator = 4;
    int denominator = 4;
    int track = -1;
    int ordinal = -1;
};

template <typename Change>
void sort_and_collapse(std::vector<Change>* changes) {
    std::sort(changes->begin(), changes->end(), [](const Change& a, const Change& b) {
        return std::tie(a.tick, a.track, a.ordinal) < std::tie(b.tick, b.track, b.ordinal);
    });
    std::vector<Change> collapsed;
    for (const Change& change : *changes) {
        if (!collapsed.empty() && collapsed.back().tick == change.tick) collapsed.back() = change;
        else collapsed.push_back(change);
    }
    *changes = std::move(collapsed);
}

double seconds_at_tick(
    const std::vector<TempoChange>& tempos,
    const int ticks_per_quarter,
    const double tick) {
    auto position = std::upper_bound(tempos.begin(), tempos.end(), tick,
        [](const double value, const TempoChange& change) { return value < change.tick; });
    const TempoChange& tempo = position == tempos.begin() ? tempos.front() : *std::prev(position);
    return tempo.seconds + (tick - tempo.tick) * 60.0 /
        (tempo.bpm * static_cast<double>(ticks_per_quarter));
}

bool append_audio_beat(
    const double source_seconds,
    const bool downbeat,
    const WavAudio& audio,
    const SongConfig& config,
    std::vector<MetronomeBeat>* beats) {
    const long long native_frame = std::llround((source_seconds + config.midi_audio_offset_seconds) * 60.0);
    const double aligned_seconds = static_cast<double>(native_frame) / 60.0;
    if (aligned_seconds < 0.0) return true;
    const auto sample_frame = static_cast<std::size_t>(std::llround(
        aligned_seconds * static_cast<double>(audio.sample_rate)));
    if (sample_frame >= audio.frame_count()) return false;
    if (beats->size() >= kMaximumMetronomeBeats) return false;
    if (beats->empty() || beats->back().frame != sample_frame) beats->push_back({sample_frame, downbeat});
    else beats->back().downbeat = beats->back().downbeat || downbeat;
    return true;
}

Status build_midi_beats(
    const std::string& midi_path,
    const WavAudio& audio,
    const SongConfig& config,
    std::vector<MetronomeBeat>* beats) {
    if (!config.midi_audio_offset_provided || !std::isfinite(config.midi_audio_offset_seconds) ||
        !std::isfinite(config.midi_minimum_lead_in_seconds)) {
        return Status::error(StatusCode::InvalidMidi,
            "MIDI metronome requires the resolved finite MIDI audio offset and lead-in");
    }
    if (config.metronome_beat_zero_offset_provided) {
        return Status::error(StatusCode::InvalidJson,
            "metronome.beat_zero_offset_seconds is only valid for explicit JSON notes");
    }

    smf::MidiFile midi;
    if (!midi.read(midi_path) || !midi.status()) {
        return Status::error(StatusCode::InvalidMidi, "failed to parse MIDI file: " + midi_path);
    }
    midi.makeAbsoluteTicks();
    const int ticks_per_quarter = midi.getTicksPerQuarterNote();
    if (ticks_per_quarter <= 0) {
        return Status::error(StatusCode::InvalidMidi, "SMPTE MIDI timing is not supported");
    }

    std::vector<TempoChange> tempos{{0.0, 120.0, -1, -1, 0.0}};
    std::vector<MeterChange> meters{{0.0, 4, 4, -1, -1}};
    for (int track = 0; track < midi.getTrackCount(); ++track) {
        for (int ordinal = 0; ordinal < midi.getEventCount(track); ++ordinal) {
            const smf::MidiEvent& event = midi[track][ordinal];
            if (event.isTempo()) {
                const double bpm = event.getTempoBPM();
                if (std::isfinite(bpm) && bpm > 0.0) tempos.push_back(
                    {static_cast<double>(event.tick), bpm, track, ordinal, 0.0});
            }
            if (event.isTimeSignature() && event.size() >= 7) {
                const int numerator = event[3];
                const int exponent = event[4];
                if (numerator > 0 && exponent >= 0 && exponent <= 6) meters.push_back(
                    {static_cast<double>(event.tick), numerator, 1 << exponent, track, ordinal});
            }
        }
    }
    sort_and_collapse(&tempos);
    sort_and_collapse(&meters);
    for (std::size_t index = 1; index < tempos.size(); ++index) {
        const TempoChange& previous = tempos[index - 1];
        tempos[index].seconds = previous.seconds + (tempos[index].tick - previous.tick) * 60.0 /
            (previous.bpm * static_cast<double>(ticks_per_quarter));
    }

    for (std::size_t meter_index = 0; meter_index < meters.size(); ++meter_index) {
        const MeterChange& meter = meters[meter_index];
        const double next_meter_tick = meter_index + 1 < meters.size() ? meters[meter_index + 1].tick :
            std::numeric_limits<double>::infinity();
        const double beat_ticks = static_cast<double>(ticks_per_quarter) * 4.0 /
            static_cast<double>(meter.denominator);
        if (!std::isfinite(beat_ticks) || beat_ticks <= 0.0) {
            return Status::error(StatusCode::InvalidMidi, "MIDI time signature has an invalid beat unit");
        }
        for (std::size_t beat_index = 0; beats->size() < kMaximumMetronomeBeats; ++beat_index) {
            const double tick = meter.tick + static_cast<double>(beat_index) * beat_ticks;
            if (tick + kComparisonEpsilon >= next_meter_tick) break;
            const double seconds = seconds_at_tick(tempos, ticks_per_quarter, tick);
            if (!std::isfinite(seconds)) {
                return Status::error(StatusCode::InvalidMidi, "MIDI metronome produced a non-finite beat time");
            }
            if (!append_audio_beat(seconds, beat_index % static_cast<std::size_t>(meter.numerator) == 0,
                    audio, config, beats)) break;
        }
        if (beats->size() >= kMaximumMetronomeBeats) {
            return Status::error(StatusCode::InvalidMidi, "MIDI metronome exceeded its beat safety limit");
        }
    }
    return Status::ok_status();
}

Status build_explicit_beats(
    const WavAudio& audio,
    const SongConfig& config,
    std::vector<MetronomeBeat>* beats) {
    if (!std::isfinite(config.bpm) || config.bpm < 30.0 || config.bpm > 300.0 ||
        !std::isfinite(config.metronome_beat_zero_offset_seconds)) {
        return Status::error(StatusCode::InvalidJson,
            "explicit-note metronome requires finite BPM 30-300 and beat-zero offset");
    }
    const double seconds_per_beat = 60.0 / config.bpm;
    const double beat_zero = config.metronome_beat_zero_offset_seconds;
    const long long first_beat = std::max<long long>(0,
        static_cast<long long>(std::ceil(-beat_zero / seconds_per_beat - kComparisonEpsilon)));
    for (long long beat = first_beat; beats->size() < kMaximumMetronomeBeats; ++beat) {
        const double seconds = beat_zero + static_cast<double>(beat) * seconds_per_beat;
        if (!std::isfinite(seconds) || seconds < 0.0) continue;
        const auto frame = static_cast<std::size_t>(std::llround(seconds * audio.sample_rate));
        if (frame >= audio.frame_count()) break;
        beats->push_back({frame, beat % 4 == 0});
    }
    if (beats->size() >= kMaximumMetronomeBeats) {
        return Status::error(StatusCode::InvalidJson, "explicit-note metronome exceeded its beat safety limit");
    }
    return Status::ok_status();
}

} // namespace

Status build_metronome_beats(
    const std::string& midi_path,
    const bool chart_from_midi,
    const WavAudio& audio,
    const SongConfig& config,
    std::vector<MetronomeBeat>* out_beats) {
    if (!out_beats) return Status::error(StatusCode::InvalidArgument, "out_beats must not be null");
    out_beats->clear();
    if (!config.metronome_enabled) return Status::ok_status();
    if (audio.sample_rate == 0 || audio.channels != 2 || audio.frame_count() == 0) {
        return Status::error(StatusCode::InvalidAudio,
            "metronome synthesis requires non-empty stereo PCM with a valid sample rate");
    }
    if (!std::isfinite(config.metronome_level) || config.metronome_level < 0.0 ||
        config.metronome_level > 1.0) {
        return Status::error(StatusCode::InvalidJson, "metronome.level must be between 0 and 1");
    }
    return chart_from_midi ? build_midi_beats(midi_path, audio, config, out_beats) :
        build_explicit_beats(audio, config, out_beats);
}

Status mix_metronome_clicks(
    WavAudio* audio,
    const SongConfig& config,
    const std::vector<MetronomeBeat>& beats,
    const MetronomeVoice voice,
    MetronomeStats* out_stats) {
    if (!audio || !out_stats) {
        return Status::error(StatusCode::InvalidArgument, "audio and out_stats must not be null");
    }
    *out_stats = MetronomeStats{};
    if (!config.metronome_enabled || beats.empty()) {
        return Status::ok_status();
    }
    if (audio->sample_rate == 0 || audio->channels != 2 || audio->frame_count() == 0 ||
        !std::isfinite(config.metronome_level) || config.metronome_level < 0.0 ||
        config.metronome_level > 1.0) {
        return Status::error(StatusCode::InvalidAudio, "invalid audio or level for metronome synthesis");
    }

    const std::size_t click_frames = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::ceil(kClickDurationSeconds * audio->sample_rate)));
    for (const MetronomeBeat& beat : beats) {
        if (beat.frame >= audio->frame_count()) continue;
        const double accent = beat.downbeat ? 1.25 : 1.0;
        const double richness = voice == MetronomeVoice::Strong ? 1.0 : 0.35;
        std::uint32_t noise_state = beat.downbeat ? 0xa341316cu : 0xc8013ea4u;
        double noise_low = 0.0;
        double noise_floor = 0.0;
        const double noise_alpha = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 1800.0 /
            static_cast<double>(audio->sample_rate));
        const double floor_alpha = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 180.0 /
            static_cast<double>(audio->sample_rate));
        for (std::size_t offset = 0; offset < click_frames && beat.frame + offset < audio->frame_count(); ++offset) {
            const double seconds = static_cast<double>(offset) / static_cast<double>(audio->sample_rate);
            const double attack = std::min(1.0, seconds / kClickAttackSeconds);
            noise_state ^= noise_state << 13u;
            noise_state ^= noise_state >> 17u;
            noise_state ^= noise_state << 5u;
            const double white = static_cast<double>(noise_state & 0x00ffffffu) /
                static_cast<double>(0x00800000u) - 1.0;
            noise_low += noise_alpha * (white - noise_low);
            noise_floor += floor_alpha * (noise_low - noise_floor);
            const double impact_noise = noise_low - noise_floor;
            const double body =
                0.62 * std::sin(2.0 * 3.14159265358979323846 * kPrimaryResonanceHz * seconds) *
                    std::exp(-seconds / 0.018) +
                0.22 * std::sin(2.0 * 3.14159265358979323846 * kSubResonanceHz * seconds + 0.35) *
                    std::exp(-seconds / 0.028) +
                (beat.downbeat ? 0.16 : 0.10) *
                    std::sin(2.0 * 3.14159265358979323846 * kLowResonanceHz * seconds + 0.7) *
                    std::exp(-seconds / 0.034);
            const double strike = richness * 1.50 * impact_noise * std::exp(-seconds / 0.0065);
            const double click = config.metronome_level * accent * attack * (body + strike);
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const std::size_t sample = (beat.frame + offset) * 2 + channel;
                audio->stereo_samples[sample] = static_cast<float>(
                    static_cast<double>(audio->stereo_samples[sample]) + click);
            }
        }
        ++out_stats->beat_count;
        if (beat.downbeat) ++out_stats->downbeat_count;
        const double seconds = static_cast<double>(beat.frame) / audio->sample_rate;
        if (out_stats->first_beat_seconds < 0.0) out_stats->first_beat_seconds = seconds;
        out_stats->last_beat_seconds = seconds;
    }
    return Status::ok_status();
}

} // namespace ff7rp::pipeline
