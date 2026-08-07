#include "pipeline/audio_metronome.h"
#include "pipeline/audio_loudness.h"
#include "tests/test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct MidiEvent {
    int tick = 0;
    int priority = 0;
    std::vector<unsigned char> bytes;
};

int fail(const std::string& message) {
    std::cerr << "metronome_selftest: " << message << '\n';
    return 1;
}

void append_be16(std::vector<unsigned char>* bytes, const std::uint32_t value) {
    bytes->push_back(static_cast<unsigned char>((value >> 8u) & 0xffu));
    bytes->push_back(static_cast<unsigned char>(value & 0xffu));
}

void append_be32(std::vector<unsigned char>* bytes, const std::uint32_t value) {
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

std::vector<unsigned char> build_midi(std::vector<MidiEvent> events) {
    events.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}}); // 120 BPM
    events.push_back({0, 1, {0xff, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08}}); // 3/4
    events.push_back({1440, 0, {0xff, 0x51, 0x03, 0x0f, 0x42, 0x40}}); // 60 BPM
    events.push_back({1440, 1, {0xff, 0x58, 0x04, 0x02, 0x02, 0x18, 0x08}}); // 2/4
    events.push_back({2880, 2, {0x90, 60, 100}});
    events.push_back({3000, 2, {0x80, 60, 0}});
    std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.priority < b.priority;
    });
    std::vector<unsigned char> track;
    int previous_tick = 0;
    for (const MidiEvent& event : events) {
        append_variable(&track, event.tick - previous_tick);
        track.insert(track.end(), event.bytes.begin(), event.bytes.end());
        previous_tick = event.tick;
    }
    append_variable(&track, 0);
    track.insert(track.end(), {0xff, 0x2f, 0x00});
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_be16(&file, 0);
    append_be16(&file, 1);
    append_be16(&file, 480);
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    append_be32(&file, static_cast<std::uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());
    return file;
}

ff7rp::pipeline::WavAudio silent_audio(const std::uint32_t sample_rate, const double seconds) {
    ff7rp::pipeline::WavAudio audio;
    audio.sample_rate = sample_rate;
    audio.channels = 2;
    audio.source_frame_count = static_cast<std::size_t>(std::llround(seconds * sample_rate));
    audio.stereo_samples.assign(audio.source_frame_count * 2, 0.0f);
    return audio;
}

double window_energy(const ff7rp::pipeline::WavAudio& audio, const std::size_t frame) {
    double energy = 0.0;
    const std::size_t end = std::min(audio.frame_count(), frame + audio.sample_rate / 50);
    for (std::size_t i = frame; i < end; ++i) {
        const double value = audio.stereo_samples[i * 2];
        energy += value * value;
    }
    return energy;
}

double range_energy(const ff7rp::pipeline::WavAudio& audio, const std::size_t begin,
    const std::size_t end) {
    double energy = 0.0;
    for (std::size_t frame = begin; frame < std::min(end, audio.frame_count()); ++frame) {
        const double value = audio.stereo_samples[frame * 2];
        energy += value * value;
    }
    return energy;
}

struct SpectralShape {
    double centroid_hz = 0.0;
    double below_1000_ratio = 0.0;
    double above_4000_ratio = 0.0;
};

SpectralShape measure_spectral_shape(const ff7rp::pipeline::WavAudio& audio,
    const std::size_t frame, const std::size_t sample_count) {
    double total = 0.0;
    double weighted = 0.0;
    double below_1000 = 0.0;
    double above_4000 = 0.0;
    for (double frequency = 50.0; frequency <= 12000.0; frequency += 50.0) {
        double real = 0.0;
        double imaginary = 0.0;
        for (std::size_t offset = 0; offset < sample_count; ++offset) {
            const double phase = 2.0 * 3.14159265358979323846 * frequency *
                static_cast<double>(offset) / static_cast<double>(audio.sample_rate);
            const double sample = audio.stereo_samples[(frame + offset) * 2];
            real += sample * std::cos(phase);
            imaginary -= sample * std::sin(phase);
        }
        const double power = real * real + imaginary * imaginary;
        total += power;
        weighted += power * frequency;
        if (frequency < 1000.0) below_1000 += power;
        if (frequency >= 4000.0) above_4000 += power;
    }
    return {weighted / total, below_1000 / total, above_4000 / total};
}

} // namespace

int main() {
    using namespace ff7rp::pipeline;

    SongConfig explicit_config;
    explicit_config.bpm = 120.0;
    explicit_config.bpm_provided = true;
    explicit_config.metronome_enabled = true;
    explicit_config.metronome_level = 0.12;
    WavAudio explicit_audio = silent_audio(1000, 3.0);
    std::vector<MetronomeBeat> beats;
    Status status = build_metronome_beats({}, false, explicit_audio, explicit_config, &beats);
    const std::vector<std::size_t> expected_frames{0, 500, 1000, 1500, 2000, 2500};
    if (!status.ok() || beats.size() != expected_frames.size()) return fail("constant-tempo beat count changed");
    for (std::size_t i = 0; i < beats.size(); ++i) {
        if (beats[i].frame != expected_frames[i] || beats[i].downbeat != (i % 4 == 0)) {
            return fail("constant-tempo timing or 4/4 downbeat changed");
        }
    }
    if (beats.back().frame >= explicit_audio.frame_count()) return fail("beat was invented after audio duration");

    SongConfig offset_config = explicit_config;
    offset_config.metronome_beat_zero_offset_seconds = 0.25;
    offset_config.metronome_beat_zero_offset_provided = true;
    status = build_metronome_beats({}, false, explicit_audio, offset_config, &beats);
    if (!status.ok() || beats.empty() || beats.front().frame != 250) {
        return fail("explicit beat-zero offset was not applied");
    }

    SongConfig disabled_config = explicit_config;
    disabled_config.metronome_enabled = false;
    beats = {{100, true}};
    status = build_metronome_beats({}, false, explicit_audio, disabled_config, &beats);
    MetronomeStats disabled_stats;
    const WavAudio disabled_before = explicit_audio;
    status = status.ok() ? mix_metronome_clicks(
        &explicit_audio, disabled_config, beats, MetronomeVoice::Strong, &disabled_stats) : status;
    if (!status.ok() || !beats.empty() || explicit_audio.stereo_samples != disabled_before.stereo_samples ||
        disabled_stats.beat_count != 0) {
        return fail("disabled metronome changed PCM");
    }

    WavAudio first = silent_audio(48000, 3.0);
    WavAudio second = first;
    status = build_metronome_beats({}, false, first, explicit_config, &beats);
    MetronomeStats first_stats;
    MetronomeStats second_stats;
    const std::size_t original_frames = first.frame_count();
    status = status.ok() ? mix_metronome_clicks(
        &first, explicit_config, beats, MetronomeVoice::Strong, &first_stats) : status;
    status = status.ok() ? mix_metronome_clicks(
        &second, explicit_config, beats, MetronomeVoice::Strong, &second_stats) : status;
    if (!status.ok() || first.stereo_samples != second.stereo_samples || first_stats.beat_count != 6 ||
        first_stats.downbeat_count != 2 || first.frame_count() != original_frames) {
        return fail("click synthesis is not deterministic");
    }
    for (std::size_t frame = 0; frame < first.frame_count(); ++frame) {
        if (first.stereo_samples[frame * 2] != first.stereo_samples[frame * 2 + 1]) {
            return fail("click synthesis is not centered/stereo-safe");
        }
    }
    if (!(window_energy(first, beats[0].frame) > window_energy(first, beats[1].frame))) {
        return fail("downbeat accent is not distinguishable from an ordinary beat");
    }
    const double downbeat_to_beat_energy =
        window_energy(first, beats[0].frame) / window_energy(first, beats[1].frame);
    const std::size_t voice_frames = static_cast<std::size_t>(std::llround(0.055 * first.sample_rate));
    const std::size_t strike_frames = static_cast<std::size_t>(std::llround(0.012 * first.sample_rate));
    const SpectralShape shape = measure_spectral_shape(first, beats[1].frame, strike_frames);
    if (!(shape.centroid_hz > 650.0 && shape.centroid_hz < 1100.0 &&
            shape.below_1000_ratio > 0.80 && shape.below_1000_ratio < 0.96 &&
            shape.above_4000_ratio > 0.01 && shape.above_4000_ratio < 0.05)) {
        return fail("procedural guide lost its measured dull, low-frequency spectral shape");
    }
    const double attack_energy = range_energy(first, beats[1].frame,
        beats[1].frame + static_cast<std::size_t>(0.012 * first.sample_rate));
    const double body_energy = range_energy(first,
        beats[1].frame + static_cast<std::size_t>(0.012 * first.sample_rate),
        beats[1].frame + static_cast<std::size_t>(0.035 * first.sample_rate));
    const double tail_energy = range_energy(first,
        beats[1].frame + static_cast<std::size_t>(0.035 * first.sample_rate),
        beats[1].frame + voice_frames);
    if (!(attack_energy > body_energy && body_energy > tail_energy && tail_energy > 0.0)) {
        return fail("wooden impact envelope lost its decaying body or audible tail");
    }

    WavAudio weak = silent_audio(48000, 1.0);
    WavAudio clean = weak;
    SongConfig weak_config = explicit_config;
    weak_config.metronome_level *= 0.60;
    MetronomeStats weak_stats;
    const std::vector<MetronomeBeat> one_beat{{0, true}};
    status = mix_metronome_clicks(&weak, weak_config, one_beat, MetronomeVoice::Weak, &weak_stats);
    if (!status.ok() || weak.stereo_samples == clean.stereo_samples ||
        !(window_energy(first, beats[0].frame) > window_energy(weak, 0)) ||
        std::any_of(clean.stereo_samples.begin(), clean.stereo_samples.end(),
            [](const float sample) { return sample != 0.0f; })) {
        return fail("strong, weak, and clean adaptive voices are not distinct");
    }
    const double strong_to_weak_energy = window_energy(first, 0) / window_energy(weak, 0);

    WavAudio hot_guide = silent_audio(48000, 1.0);
    std::fill(hot_guide.stereo_samples.begin(), hot_guide.stereo_samples.end(), 0.95f);
    SongConfig hot_config = explicit_config;
    hot_config.metronome_level = 1.0;
    std::vector<MetronomeBeat> hot_beats{{0, true}};
    MetronomeStats hot_stats;
    const std::size_t hot_frames = hot_guide.frame_count();
    status = mix_metronome_clicks(
        &hot_guide, hot_config, hot_beats, MetronomeVoice::Strong, &hot_stats);
    if (!status.ok() || measure_sample_peak_dbfs(hot_guide) <= 0.0) {
        return fail("hot guide fixture did not exercise the post-mix limiter");
    }
    bool guide_limiter_engaged = false;
    status = limit_audio_peak(&hot_guide, -1.0, &guide_limiter_engaged);
    if (!status.ok() || !guide_limiter_engaged || hot_guide.frame_count() != hot_frames ||
        measure_sample_peak_dbfs(hot_guide) > -0.999) {
        return fail("guide-only limiting did not prevent clipping without changing duration");
    }

    const ff7rp::tests::TemporaryDirectory midi_root("ff7rp-metronome-selftest");
    const std::filesystem::path midi_path = midi_root.path() / "tempo-map.mid";
    const std::vector<unsigned char> midi = build_midi({});
    {
        std::ofstream out(midi_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(midi.data()), static_cast<std::streamsize>(midi.size()));
    }
    SongConfig midi_config;
    midi_config.metronome_enabled = true;
    midi_config.midi_audio_offset_seconds = 0.5;
    midi_config.midi_audio_offset_provided = true;
    midi_config.midi_minimum_lead_in_seconds = 1.0;
    WavAudio midi_audio = silent_audio(1000, 5.5);
    status = build_metronome_beats(midi_path.string(), true, midi_audio, midi_config, &beats);
    const std::vector<std::size_t> expected_midi_frames{500, 1000, 1500, 2000, 3000, 4000, 5000};
    const std::vector<bool> expected_midi_downbeats{true, false, false, true, false, true, false};
    if (!status.ok() || beats.size() != expected_midi_frames.size()) {
        return fail("tempo/meter/alignment fixture produced an unexpected beat count: " + status.message);
    }
    for (std::size_t i = 0; i < beats.size(); ++i) {
        if (beats[i].frame != expected_midi_frames[i] || beats[i].downbeat != expected_midi_downbeats[i]) {
            return fail("complete MIDI tempo/meter map, phase, or 60 Hz alignment changed");
        }
    }
    if (!(static_cast<double>(beats.front().frame) / midi_audio.sample_rate <
            midi_config.midi_minimum_lead_in_seconds)) {
        return fail("MIDI guide did not establish tempo before the first playable-prompt lead-in");
    }

    WavAudio alternate_rate = silent_audio(44100, 3.0);
    status = build_metronome_beats({}, false, alternate_rate, explicit_config, &beats);
    MetronomeStats alternate_stats;
    status = status.ok() ? mix_metronome_clicks(
        &alternate_rate, explicit_config, beats, MetronomeVoice::Strong, &alternate_stats) : status;
    if (!status.ok() || alternate_stats.beat_count != first_stats.beat_count ||
        std::fabs(alternate_stats.last_beat_seconds - first_stats.last_beat_seconds) > 1e-9) {
        return fail("sample-rate-independent timing changed");
    }

    std::cout << "metronome profile centroid_hz=" << shape.centroid_hz
              << " below_1000_ratio=" << shape.below_1000_ratio
              << " above_4000_ratio=" << shape.above_4000_ratio
              << " downbeat_to_beat_energy=" << downbeat_to_beat_energy
              << " strong_to_weak_energy=" << strong_to_weak_energy << '\n';
    std::cout << "metronome_selftest ok\n";
    return 0;
}
