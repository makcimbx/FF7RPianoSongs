#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <Windows.h>

#include "pipeline/midi_chart_generator.h"
#include "pipeline/pipeline_limits.h"
#include "tests/test_support.h"

namespace {

std::string imported_fixture_group;
bool use_process_fixture_partitions = true;
DWORD_PTR imported_worker_affinity = 0;

unsigned int bit_count(DWORD_PTR value)
{
    unsigned int count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

DWORD_PTR take_lowest_bit(DWORD_PTR* value)
{
    const DWORD_PTR bit = *value & (0 - *value);
    *value &= ~bit;
    return bit;
}

DWORD_PTR highest_bit(DWORD_PTR value)
{
    DWORD_PTR bit = 0;
    while (value != 0) bit = take_lowest_bit(&value);
    return bit;
}

bool perf_diagnostics_enabled()
{
    return GetEnvironmentVariableA("FF7RP_PERF_DIAGNOSTICS", nullptr, 0) != 0;
}

template <typename Function>
int run_profiled(const char* name, Function&& function)
{
    const auto started = std::chrono::steady_clock::now();
    const int result = function();
    if (perf_diagnostics_enabled()) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        std::cerr << "midi_generator_selftest test_ms=" << elapsed.count()
                  << " test=" << name << '\n';
    }
    return result;
}

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) : value_(value) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) reset(other.release());
        return *this;
    }
    HANDLE get() const { return value_; }
    HANDLE release()
    {
        const HANDLE value = value_;
        value_ = nullptr;
        return value;
    }
    void reset(HANDLE value = nullptr)
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = value;
    }
    explicit operator bool() const { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE value_ = nullptr;
};

struct MidiEvent {
    int tick = 0;
    int priority = 0;
    std::vector<unsigned char> bytes;
};

using MidiTrack = std::vector<MidiEvent>;

int fail(const std::string& message) {
    std::cerr << "midi_generator_selftest: " << message << '\n';
    return 1;
}

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
    while ((value >>= 7) != 0) {
        encoded[3 - count] = static_cast<unsigned char>((value & 0x7f) | 0x80);
        ++count;
    }
    bytes->insert(bytes->end(), encoded.end() - count, encoded.end());
}

void add_note(
    MidiTrack* track,
    const int tick,
    const int duration,
    const int pitch,
    const int velocity = 100,
    const int channel = 0) {
    track->push_back({tick, 2,
        {static_cast<unsigned char>(0x90 | channel), static_cast<unsigned char>(pitch),
            static_cast<unsigned char>(velocity)}});
    track->push_back({tick + duration, 1,
        {static_cast<unsigned char>(0x80 | channel), static_cast<unsigned char>(pitch), 0}});
}

void add_tempo(MidiTrack* track, const int tick, const int microseconds = 500000) {
    track->push_back({tick, 0, {0xff, 0x51, 0x03,
        static_cast<unsigned char>((microseconds >> 16) & 0xff),
        static_cast<unsigned char>((microseconds >> 8) & 0xff),
        static_cast<unsigned char>(microseconds & 0xff)}});
}

void add_time_signature(
    MidiTrack* track,
    const int tick,
    const int numerator,
    const int denominator_exponent) {
    track->push_back({tick, 0, {0xff, 0x58, 0x04,
        static_cast<unsigned char>(numerator), static_cast<unsigned char>(denominator_exponent),
        24, 8}});
}

std::vector<unsigned char> build_midi(
    const int format,
    const int ticks_per_quarter,
    std::vector<MidiTrack> tracks) {
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, format);
    append_u16(&file, static_cast<int>(tracks.size()));
    append_u16(&file, ticks_per_quarter);
    for (MidiTrack& events : tracks) {
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
        file.insert(file.end(), {'M', 'T', 'r', 'k'});
        append_u32(&file, data.size());
        file.insert(file.end(), data.begin(), data.end());
    }
    return file;
}

std::vector<unsigned char> build_raw_track_midi(
    const int division,
    const std::vector<unsigned char>& track_data) {
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_u16(&file, 0);
    append_u16(&file, 1);
    append_u16(&file, division);
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    append_u32(&file, track_data.size());
    file.insert(file.end(), track_data.begin(), track_data.end());
    return file;
}

bool cleanup_temp_directory(
    ff7rp::tests::TemporaryDirectory* directory,
    std::string* error_message = nullptr)
{
    for (unsigned int attempt = 0; attempt < 40; ++attempt) {
        if (directory->cleanup(error_message)) return true;
        Sleep(25);
    }
    return false;
}

class TemporaryMidi {
public:
    explicit TemporaryMidi(const std::vector<unsigned char>& bytes)
        : path_(temporary_root().path() /
              ("fixture-" + std::to_string(sequence().fetch_add(1, std::memory_order_relaxed)) + ".mid")) {
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) throw std::runtime_error("could not write temporary MIDI fixture");
    }

    const std::filesystem::path& path() const { return path_; }
    static ff7rp::tests::TemporaryDirectory& temporary_root()
    {
        static ff7rp::tests::TemporaryDirectory root("ff7rp-midi-selftest");
        return root;
    }

private:
    static std::atomic<unsigned long long>& sequence()
    {
        static std::atomic<unsigned long long> value{0};
        return value;
    }
    std::filesystem::path path_;
};

ff7rp::pipeline::SongConfig fixture_config(const int difficulty = 6) {
    ff7rp::pipeline::SongConfig config;
    config.difficulty = difficulty;
    config.midi_audio_alignment_provided = true;
    config.midi_audio_alignment_seconds = 0.0;
    config.midi_audio_offset_provided = true;
    config.midi_audio_offset_seconds = 0.0;
    config.midi_minimum_lead_in_seconds = 0.0;
    return config;
}

ff7rp::pipeline::Status generate(
    const std::filesystem::path& path,
    const ff7rp::pipeline::SongConfig& config,
    std::vector<ff7rp::pipeline::Note>* notes,
    ff7rp::pipeline::MidiChartStats* stats,
    const std::vector<ff7rp::pipeline::Note>* baseline = nullptr,
    const std::size_t maximum_visible_rows = 0) {
    const ff7rp::pipeline::WavAudio no_audio;
    return ff7rp::pipeline::generate_notes_from_midi(
        path.string(), no_audio, config, notes, stats, baseline, maximum_visible_rows);
}

struct NoteMetrics {
    std::size_t actions = 0;
    double actions_per_minute = 0.0;
    double interval_p10 = 0.0;
    double interval_p25 = 0.0;
    double interval_p50 = 0.0;
    std::array<std::size_t, 3> bursts{};
};

NoteMetrics note_metrics(
    const std::vector<ff7rp::pipeline::Note>& notes,
    const double seconds_per_beat) {
    std::vector<double> times;
    for (const auto& note : notes) {
        if (!note.pitch.empty()) times.push_back(note.beat * seconds_per_beat);
    }
    NoteMetrics result;
    result.actions = times.size();
    if (times.size() < 2) return result;
    const double span = times.back() - times.front();
    if (span > 0.0) result.actions_per_minute = times.size() * 60.0 / span;
    std::vector<double> gaps;
    for (std::size_t index = 1; index < times.size(); ++index) {
        gaps.push_back(times[index] - times[index - 1]);
    }
    std::sort(gaps.begin(), gaps.end());
    const auto quantile = [&](const double fraction) {
        const double position = (gaps.size() - 1) * fraction;
        const std::size_t begin = static_cast<std::size_t>(position);
        const std::size_t end = std::min(begin + 1, gaps.size() - 1);
        return gaps[begin] + (gaps[end] - gaps[begin]) * (position - begin);
    };
    result.interval_p10 = quantile(0.10);
    result.interval_p25 = quantile(0.25);
    result.interval_p50 = quantile(0.50);
    const std::array<double, 3> windows{{1.0, 2.0, 5.0}};
    for (std::size_t window = 0; window < windows.size(); ++window) {
        for (std::size_t begin = 0, end = 0; begin < times.size(); ++begin) {
            end = std::max(end, begin);
            while (end < times.size() &&
                   times[end] < times[begin] + windows[window] - 0.000001) ++end;
            result.bursts[window] = std::max(result.bursts[window], end - begin);
        }
    }
    return result;
}

bool notes_equal(
    const std::vector<ff7rp::pipeline::Note>& a,
    const std::vector<ff7rp::pipeline::Note>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].beat != b[i].beat || a[i].duration_beats != b[i].duration_beats ||
            a[i].pitch != b[i].pitch || a[i].chord_id != b[i].chord_id ||
            a[i].monotone_note_value != b[i].monotone_note_value ||
            a[i].chord_note_value != b[i].chord_note_value) {
            return false;
        }
    }
    return true;
}

bool local_skills_equal(
    const ff7rp::pipeline::MidiLocalSkillMetrics& a,
    const ff7rp::pipeline::MidiLocalSkillMetrics& b) {
    return a.maximum_window_actions == b.maximum_window_actions &&
        a.maximum_window_begin_seconds == b.maximum_window_begin_seconds &&
        a.maximum_quarter_second_stream_actions == b.maximum_quarter_second_stream_actions &&
        a.maximum_quarter_second_stream_duration == b.maximum_quarter_second_stream_duration &&
        a.maximum_quarter_second_stream_begin_seconds == b.maximum_quarter_second_stream_begin_seconds &&
        a.maximum_quarter_second_stream_actions_end_seconds == b.maximum_quarter_second_stream_actions_end_seconds &&
        a.maximum_quarter_second_stream_duration_begin_seconds == b.maximum_quarter_second_stream_duration_begin_seconds &&
        a.maximum_quarter_second_stream_duration_end_seconds == b.maximum_quarter_second_stream_duration_end_seconds &&
        a.maximum_half_second_stream_actions == b.maximum_half_second_stream_actions &&
        a.maximum_half_second_stream_duration == b.maximum_half_second_stream_duration &&
        a.maximum_half_second_stream_begin_seconds == b.maximum_half_second_stream_begin_seconds &&
        a.maximum_half_second_stream_actions_end_seconds == b.maximum_half_second_stream_actions_end_seconds &&
        a.maximum_half_second_stream_duration_begin_seconds == b.maximum_half_second_stream_duration_begin_seconds &&
        a.maximum_half_second_stream_duration_end_seconds == b.maximum_half_second_stream_duration_end_seconds &&
        a.maximum_jack_run == b.maximum_jack_run && a.maximum_jack_begin_seconds == b.maximum_jack_begin_seconds &&
        a.maximum_reversal_run == b.maximum_reversal_run &&
        a.maximum_reversal_begin_seconds == b.maximum_reversal_begin_seconds &&
        a.rapid_movement_p90 == b.rapid_movement_p90 &&
        a.rapid_movement_maximum == b.rapid_movement_maximum &&
        a.rapid_movement_maximum_seconds == b.rapid_movement_maximum_seconds &&
        a.octave_movement_rate == b.octave_movement_rate &&
        a.maximum_octave_movements_in_five_seconds == b.maximum_octave_movements_in_five_seconds &&
        a.maximum_octave_window_begin_seconds == b.maximum_octave_window_begin_seconds &&
        a.maximum_large_reversals_in_five_seconds == b.maximum_large_reversals_in_five_seconds &&
        a.maximum_large_reversal_window_begin_seconds == b.maximum_large_reversal_window_begin_seconds &&
        a.right_fatigue_peak == b.right_fatigue_peak &&
        a.right_fatigue_peak_seconds == b.right_fatigue_peak_seconds &&
        a.left_fatigue_peak == b.left_fatigue_peak &&
        a.left_fatigue_peak_seconds == b.left_fatigue_peak_seconds &&
        a.hand_imbalance == b.hand_imbalance &&
        a.rhythm_irregularity_p90 == b.rhythm_irregularity_p90 &&
        a.rhythm_irregularity_maximum == b.rhythm_irregularity_maximum &&
        a.rhythm_irregularity_peak_seconds == b.rhythm_irregularity_peak_seconds &&
        a.hardest_window_begin_seconds == b.hardest_window_begin_seconds &&
        a.hardest_window_end_seconds == b.hardest_window_end_seconds &&
        a.dominant_skill == b.dominant_skill && a.satisfied_route == b.satisfied_route &&
        a.satisfied_route_ratio == b.satisfied_route_ratio &&
        a.satisfied_route_margin == b.satisfied_route_margin &&
        a.dominant_skill_is_global == b.dominant_skill_is_global &&
        a.satisfied_route_name == b.satisfied_route_name;
}

int pitch_to_midi(const std::string& pitch) {
    static const std::array<std::string, 12> names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const std::size_t octave_position = pitch.find_last_not_of("0123456789-") + 1;
    const std::string name = pitch.substr(0, octave_position);
    const int octave = std::stoi(pitch.substr(octave_position));
    const auto found = std::find(names.begin(), names.end(), name);
    return (octave + 1) * 12 + static_cast<int>(found - names.begin());
}

void write_le16(std::ofstream& out, const std::uint16_t value) {
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu)};
    out.write(bytes.data(), bytes.size());
}

void write_le32(std::ofstream& out, const std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu), static_cast<char>((value >> 24u) & 0xffu)};
    out.write(bytes.data(), bytes.size());
}

bool write_synthetic_wav(const std::filesystem::path& path) {
    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint32_t frames = sample_rate;
    constexpr std::uint32_t data_size = frames * channels * 2u;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("RIFF", 4); write_le32(out, 36u + data_size);
    out.write("WAVEfmt ", 8); write_le32(out, 16u); write_le16(out, 1u);
    write_le16(out, channels); write_le32(out, sample_rate);
    write_le32(out, sample_rate * channels * 2u); write_le16(out, channels * 2u);
    write_le16(out, 16u); out.write("data", 4); write_le32(out, data_size);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto sample = static_cast<std::int16_t>((static_cast<int>(frame % 64u) - 32) * 32);
        write_le16(out, static_cast<std::uint16_t>(sample));
        write_le16(out, static_cast<std::uint16_t>(sample));
    }
    return static_cast<bool>(out);
}

std::vector<std::filesystem::path> synthetic_profile_fixtures() {
    struct FixtureSpec {
        const char* name;
        int onset_count;
        int tick_step;
        int pitch_stride;
        bool harmony;
        bool humanized;
        bool meter_change;
    };
    constexpr std::array<FixtureSpec, 6> specs{{
        {"SyntheticLongScale", 480, 240, 5, false, false, false},
        {"SyntheticLongHarmony", 480, 240, 3, true, false, false},
        {"SyntheticHumanized", 360, 240, 7, true, true, false},
        {"SyntheticMeterChanges", 360, 240, 4, false, false, true},
        {"SyntheticPhraseBursts", 300, 288, 2, true, true, false},
        {"SyntheticCrossingVoices", 320, 270, 11, true, false, true},
    }};
    const std::filesystem::path root = TemporaryMidi::temporary_root().path() / "synthetic-profile-fixtures";
    std::filesystem::create_directories(root);
    std::vector<std::filesystem::path> fixtures;
    fixtures.reserve(specs.size());
    for (const FixtureSpec& spec : specs) {
        const std::filesystem::path directory = root / spec.name;
        std::filesystem::create_directories(directory);
        MidiTrack melody;
        MidiTrack harmony;
        add_tempo(&melody, 0);
        if (spec.meter_change) {
            add_time_signature(&melody, 0, 3, 2);
            add_time_signature(&melody, spec.onset_count * spec.tick_step / 2, 4, 2);
        }
        for (int index = 0; index < spec.onset_count; ++index) {
            const int jitter = spec.humanized && index % 5 == 2 ? 8 : 0;
            const int tick = index * spec.tick_step + jitter;
            add_note(&melody, tick, std::max(60, spec.tick_step - 24),
                67 + (index * spec.pitch_stride) % 17, 88 + index % 32);
            if (spec.harmony && index % 4 == 0) {
                const int root_pitch = 43 + (index / 4) % 8;
                add_note(&harmony, tick, spec.tick_step * 2, root_pitch, 78);
                add_note(&harmony, tick, spec.tick_step * 2, root_pitch + 4, 76);
                add_note(&harmony, tick, spec.tick_step * 2, root_pitch + 7, 74);
            }
        }
        const std::vector<unsigned char> midi = build_midi(
            spec.harmony ? 1 : 0, 480,
            spec.harmony ? std::vector<MidiTrack>{melody, harmony} : std::vector<MidiTrack>{melody});
        const std::filesystem::path midi_path = directory / "song.mid";
        std::ofstream midi_out(midi_path, std::ios::binary | std::ios::trunc);
        midi_out.write(reinterpret_cast<const char*>(midi.data()), static_cast<std::streamsize>(midi.size()));
        midi_out.close();
        std::ofstream config(directory / "song.json", std::ios::binary | std::ios::trunc);
        config << "{\n  \"schema\": \"ff7rpianosongs.song.v2\",\n"
               << "  \"title\": \"" << spec.name << "\",\n"
               << "  \"midi_audio_alignment_seconds\": 0.0,\n"
               << "  \"midi_audio_offset_seconds\": 0.0,\n"
               << "  \"midi_minimum_lead_in_seconds\": 0.0,\n"
               << "  \"loudness_normalization\": false\n}\n";
        config.close();
        if (!midi_out || !config || !write_synthetic_wav(directory / "song.wav")) {
            throw std::runtime_error(std::string("could not create synthetic profile fixture ") + spec.name);
        }
        fixtures.push_back(midi_path);
    }
    return fixtures;
}

int test_chord_inference() {
    using ff7rp::pipeline::infer_native_chord_from_fresh_midi_pitches;
    if (infer_native_chord_from_fresh_midi_pitches({64, 67, 72}) != "pca_C") {
        return fail("C major inversion was not recognized");
    }
    if (infer_native_chord_from_fresh_midi_pitches({57, 60, 62, 65}) != "pca_D_m7") {
        return fail("D minor seventh inversion was not recognized");
    }
    if (infer_native_chord_from_fresh_midi_pitches({60, 62, 64, 67, 70}) != "pca_C_9") {
        return fail("C ninth was not recognized");
    }
    if (!infer_native_chord_from_fresh_midi_pitches({62, 64, 67, 70}).empty()) {
        return fail("rootless chord was accepted");
    }
    if (!infer_native_chord_from_fresh_midi_pitches({60, 67}).empty()) {
        return fail("dyad was accepted");
    }
    if (!infer_native_chord_from_fresh_midi_pitches({60, 62, 64, 65, 67}).empty()) {
        return fail("non-chord tones were accepted");
    }
    if (!infer_native_chord_from_fresh_midi_pitches({60, 64, 68}).empty()) {
        return fail("unsupported augmented chord was accepted");
    }
    if (!infer_native_chord_from_fresh_midi_pitches({62, 66, 69, 72}).empty()) {
        return fail("unverified native D7 identifier was emitted");
    }
    if (infer_native_chord_from_fresh_midi_pitches({60, 65, 67}) != "pca_C_sus4") {
        return fail("C suspended fourth was not recognized");
    }
    for (const std::array<int, 3>& sequential :
        {std::array<int, 3>{59, 62, 65}, std::array<int, 3>{54, 57, 60}}) {
        MidiTrack track;
        add_tempo(&track, 0);
        for (std::size_t index = 0; index < sequential.size(); ++index) {
            add_note(&track, static_cast<std::uint32_t>(index * 480), 180, sequential[index], 100);
        }
        const TemporaryMidi midi(build_midi(0, 480, {track}));
        std::vector<ff7rp::pipeline::Note> notes;
        ff7rp::pipeline::MidiChartStats stats;
        const auto status = generate(midi.path(), fixture_config(6), &notes, &stats);
        if (!status.ok()) return fail("sequential diminished false-positive fixture failed: " + status.message);
        if (std::any_of(notes.begin(), notes.end(), [](const auto& note) {
                return note.chord_id == "pca_B_dim" || note.chord_id == "pca_Fs_dim";
            })) {
            return fail("sequential melody pitches emitted a diminished chord false positive");
        }
    }
    return 0;
}

int test_alignment() {
    MidiTrack track;
    add_tempo(&track, 0);
    add_note(&track, 0, 480, 60);
    add_note(&track, 480, 480, 62);
    add_note(&track, 960, 480, 64);
    const TemporaryMidi midi(build_midi(0, 480, {track}));

    ff7rp::pipeline::WavAudio audio;
    audio.sample_rate = 48000;
    audio.channels = 2;
    audio.source_frame_count = 96000;
    audio.stereo_samples.assign(audio.source_frame_count * 2, 0.0f);
    constexpr double expected_alignment = 0.12;
    const int pitches[] = {60, 62, 64};
    for (int note_index = 0; note_index < 3; ++note_index) {
        const double frequency = 440.0 * std::pow(2.0, (pitches[note_index] - 69) / 12.0);
        const std::size_t start = static_cast<std::size_t>(
            std::llround((note_index * 0.5 + expected_alignment) * audio.sample_rate));
        const std::size_t length = static_cast<std::size_t>(0.12 * audio.sample_rate);
        for (std::size_t frame = 0; frame < length && start + frame < audio.source_frame_count; ++frame) {
            const double seconds = static_cast<double>(frame) / audio.sample_rate;
            const double attack = std::min(1.0, seconds / 0.06);
            const float sample = static_cast<float>(0.3 * attack * std::exp(-8.0 * seconds) *
                std::sin(2.0 * 3.14159265358979323846 * frequency * seconds));
            audio.stereo_samples[(start + frame) * 2] += sample;
            audio.stereo_samples[(start + frame) * 2 + 1] += sample;
        }
    }
    ff7rp::pipeline::SongConfig config;
    config.difficulty = 3;
    config.midi_minimum_lead_in_seconds = 0.0;
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = ff7rp::pipeline::generate_notes_from_midi(
        midi.path().string(), audio, config, &notes, &stats);
    if (!status.ok()) return fail(status.message);
    if (notes.size() != 3 || notes[0].pitch != "C4" ||
        notes[1].pitch != "D4" || notes[2].pitch != "E4") {
        std::string emitted;
        for (const auto& note : notes) emitted += note.pitch + "@" + std::to_string(note.beat) + ",";
        return fail("unexpected generated aligned melody: " + emitted + " candidates=" +
            std::to_string(stats.melody_candidates) + "/" +
            std::to_string(stats.fallback_candidates) + " strain=" +
            std::to_string(stats.joint_strain_p95) + "/" +
            std::to_string(stats.joint_strain_peak) + " rejected=" +
            std::to_string(stats.strain_rejections) + "/" +
            std::to_string(stats.cooldown_rejections));
    }
    if (stats.source_tracks != 1 || stats.melody_tracks != 1 || stats.harmony_tracks != 0) {
        return fail("unexpected stream accounting");
    }
    if (std::fabs(stats.source_bpm - 120.0) > 0.000001) return fail("unexpected source tempo");
    if (std::fabs(stats.audio_alignment_seconds - expected_alignment) > 0.025) {
        return fail("automatic MIDI/audio alignment was inaccurate: " +
            std::to_string(stats.audio_alignment_seconds));
    }
    if (std::fabs(stats.audio_offset_seconds - (stats.audio_alignment_seconds + 0.007)) > 0.000001) {
        return fail("automatic playback-delay compensation was inaccurate");
    }
    return 0;
}

int test_format_two_rejected() {
    MidiTrack first;
    MidiTrack second;
    add_note(&first, 0, 120, 60);
    add_note(&second, 0, 120, 64);
    const TemporaryMidi midi(build_midi(2, 480, {first, second}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(), &notes, &stats);
    if (status.ok() || status.message.find("format 2") == std::string::npos) {
        return fail("format 2 MIDI was not rejected explicitly");
    }
    return 0;
}

int require_single_generated_note(
    const std::vector<unsigned char>& bytes,
    const std::string& expected_pitch,
    const std::string& fixture_name) {
    const TemporaryMidi midi(bytes);
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(), &notes, &stats);
    if (!status.ok()) return fail(fixture_name + " failed: " + status.message);
    if (notes.size() != 1 || notes.front().pitch != expected_pitch ||
        !notes.front().chord_id.empty() || notes.front().beat != 0.0 ||
        notes.front().duration_beats != 0.25) {
        return fail(fixture_name + " did not preserve its generated note semantics");
    }
    return 0;
}

int test_running_status_after_text_meta() {
    return require_single_generated_note(build_raw_track_midi(480, {
        0x00, 0x90, 0x3c, 0x64,
        0x00, 0xff, 0x01, 0x01, 0x78,
        0x83, 0x60, 0x3c, 0x00,
        0x00, 0xff, 0x2f, 0x00,
    }), "C4", "running status after text Meta");
}

int test_running_status_after_sysex() {
    return require_single_generated_note(build_raw_track_midi(480, {
        0x00, 0x90, 0x3e, 0x64,
        0x00, 0xf0, 0x02, 0x7d, 0xf7,
        0x83, 0x60, 0x3e, 0x00,
        0x00, 0xff, 0x2f, 0x00,
    }), "D4", "running status after SysEx");
}

int test_running_status_without_prior_channel_status_rejected() {
    const TemporaryMidi midi(build_raw_track_midi(480, {
        0x00, 0xff, 0x01, 0x01, 0x78,
        0x00, 0x3c, 0x00,
        0x00, 0xff, 0x2f, 0x00,
    }));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(), &notes, &stats);
    if (status.ok() || status.code != ff7rp::pipeline::StatusCode::InvalidMidi || !notes.empty()) {
        return fail("running status without a prior channel status was not rejected");
    }
    return 0;
}

int test_running_status_after_unsupported_system_status_rejected() {
    const TemporaryMidi midi(build_raw_track_midi(480, {
        0x00, 0x90, 0x3c, 0x64,
        0x00, 0xf1,
        0x00, 0x3c, 0x00,
        0x00, 0xff, 0x2f, 0x00,
    }));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(), &notes, &stats);
    if (status.ok() || status.code != ff7rp::pipeline::StatusCode::InvalidMidi || !notes.empty()) {
        return fail("unsupported system status incorrectly preserved channel running status");
    }
    return 0;
}

int test_smpte_division_rejected() {
    MidiTrack track;
    add_note(&track, 0, 40, 60);
    const TemporaryMidi midi(build_midi(0, 0xe728, {track}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(), &notes, &stats);
    if (status.ok() || status.code != ff7rp::pipeline::StatusCode::InvalidMidi ||
        status.message.find("SMPTE MIDI timing is not supported") == std::string::npos || !notes.empty()) {
        return fail("true SMPTE division was not rejected explicitly");
    }
    return 0;
}

int test_crossing_voice_and_pitch_witness() {
    MidiTrack track;
    const std::array<std::pair<int, int>, 5> voices{{
        {60, 72}, {62, 70}, {64, 68}, {65, 67}, {68, 64}
    }};
    std::set<int> source_pitches;
    for (std::size_t i = 0; i < voices.size(); ++i) {
        add_note(&track, static_cast<int>(i) * 480, 360, voices[i].first);
        add_note(&track, static_cast<int>(i) * 480, 360, voices[i].second);
        source_pitches.insert(voices[i].first);
        source_pitches.insert(voices[i].second);
    }
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    std::vector<ff7rp::pipeline::Note> first;
    std::vector<ff7rp::pipeline::Note> second;
    ff7rp::pipeline::MidiChartStats first_stats;
    ff7rp::pipeline::MidiChartStats second_stats;
    auto status = generate(midi.path(), fixture_config(), &first, &first_stats);
    if (!status.ok()) return fail(status.message);
    status = generate(midi.path(), fixture_config(), &second, &second_stats);
    if (!status.ok()) return fail(status.message);
    if (!notes_equal(first, second) || first_stats.right_events != second_stats.right_events ||
        first_stats.voice_stream_changes != second_stats.voice_stream_changes) {
        return fail("repeated reduction was not deterministic");
    }
    const std::array<std::string, 5> expected{{"C5", "A#4", "G#4", "G4", "E4"}};
    if (first.size() != 2 * expected.size() || first_stats.selected_actions != expected.size())
        return fail("crossing voice fixture lost melody inputs or simultaneous followers");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& root = first[2 * i];
        const auto& follower = first[2 * i + 1];
        if (root.pitch != expected[i]) {
            return fail("voice DP switched at the crossing: expected " + expected[i] +
                ", got " + root.pitch);
        }
        if (source_pitches.find(pitch_to_midi(root.pitch)) == source_pitches.end() ||
            source_pitches.find(pitch_to_midi(follower.pitch)) == source_pitches.end() ||
            root.group_index == 0 || root.group_index != follower.group_index ||
            root.beat != follower.beat || root.pitch == follower.pitch) {
            return fail("generated pitch had no source-event witness");
        }
    }
    if (first_stats.octave_fixes != 0 || first_stats.source_pitch_witness_failures != 0) {
        return fail("source pitches were transposed or lost their witness");
    }
    return 0;
}

int test_anchor_based_humanization_boundary() {
    MidiTrack track;
    add_note(&track, 0, 120, 60);
    add_note(&track, 15, 120, 64);
    add_note(&track, 16, 120, 67);
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(), &notes, &stats);
    if (!status.ok()) return fail(status.message);
    if (stats.exact_tick_groups != 3 || stats.humanized_clusters != 2 ||
        stats.humanized_events != 1) {
        return fail("humanization clustering crossed its anchor boundary");
    }
    return 0;
}

int test_joint_cooldown_and_collision_policy() {
    MidiTrack melody;
    MidiTrack accompaniment;
    add_note(&melody, 0, 240, 84, 110);
    add_note(&melody, 3840, 240, 84, 110);
    add_note(&accompaniment, 0, 240, 48, 90);
    add_note(&accompaniment, 0, 240, 52, 90);
    add_note(&accompaniment, 0, 240, 55, 90);
    add_note(&accompaniment, 1920, 180, 36, 90);
    add_note(&accompaniment, 1980, 180, 38, 90);
    const TemporaryMidi midi(build_midi(1, 480, {melody, accompaniment}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(5), &notes, &stats);
    if (!status.ok()) return fail(status.message);
    if (stats.fallback_candidates < 2 || stats.cooldown_rejections == 0) {
        return fail("primary and fallback candidates did not share one cooldown");
    }
    // The salient tracked C6 now wins the same-frame competition with the
    // accompaniment chord. The isolated C2 fills the source gap, but its nearby
    // weaker D2 still loses at actual selection rather than preliminary vicinity.
    if (stats.fallback_events != 1 || stats.right_events != 3 || notes.size() != 3 ||
        notes[0].pitch != "C6" || notes[0].beat != 0.0 ||
        notes[1].pitch != "C2" || notes[1].beat != 4.0 ||
        notes[2].pitch != "C6" || notes[2].beat != 8.0) {
        std::string emitted;
        for (const auto& note : notes) emitted += " " + note.pitch + "@" + std::to_string(note.beat);
        return fail("cooldown retained the weaker near-primary fallback: fallback=" +
            std::to_string(stats.fallback_events) + ", right=" +
            std::to_string(stats.right_events) + ", candidates=" +
            std::to_string(stats.fallback_candidates) + ", emitted=" + emitted);
    }
    if (stats.merged_events != 0 || stats.cross_hand_conflicts != 1 ||
        stats.scheduled_conflicts != 0 || stats.dropped_conflicts < 1 || notes.empty()) {
        return fail("same-frame cross-hand events were not reduced to one exact-onset action");
    }
    for (const auto& note : notes) {
        if (note.pitch.empty() == note.chord_id.empty()) {
            return fail("cross-hand scheduler emitted a dual-action or empty row");
        }
    }
    // These counters describe candidate competition, not emitted collisions.
    // The same-frame alternate is now retained until selection; the exact rows
    // above and scheduled_conflicts still prove collision-free publication.
    if (stats.right_collisions != 1 || stats.left_collisions != 0) {
        return fail("same-frame source alternate did not reach actual selection");
    }
    return 0;
}

int test_three_four_metric_accents() {
    MidiTrack track;
    add_time_signature(&track, 0, 3, 2);
    for (int beat = 0; beat < 6; ++beat) add_note(&track, beat * 480, 180, 60 + beat);
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(), &notes, &stats);
    if (!status.ok()) return fail(status.message);
    if (stats.time_signature_changes != 1 || stats.metric_downbeat_candidates != 2 ||
        stats.metric_downbeat_right_events != 2) {
        return fail("3/4 downbeat phase was not applied at beats one and four");
    }
    return 0;
}

int test_track_permutation_invariance() {
    MidiTrack upper;
    MidiTrack lower;
    add_tempo(&upper, 0);
    for (int index = 0; index < 4; ++index) {
        add_note(&upper, index * 480, 360, 72 + index, 105);
        add_note(&lower, index * 480, 360, 48 + index, 90);
    }
    TemporaryMidi first_midi(build_midi(1, 480, {upper, lower}));
    TemporaryMidi second_midi(build_midi(1, 480, {lower, upper}));
    std::vector<ff7rp::pipeline::Note> first;
    std::vector<ff7rp::pipeline::Note> second;
    ff7rp::pipeline::MidiChartStats first_stats;
    ff7rp::pipeline::MidiChartStats second_stats;
    auto status = generate(first_midi.path(), fixture_config(), &first, &first_stats);
    if (!status.ok()) return fail("track permutation fixture A failed: " + status.message);
    status = generate(second_midi.path(), fixture_config(), &second, &second_stats);
    if (!status.ok()) return fail("track permutation fixture B failed: " + status.message);
    if (!notes_equal(first, second)) return fail("track permutation changed generated chart");
    return 0;
}

int test_tempo_change_timing() {
    MidiTrack track;
    add_tempo(&track, 0, 500000);
    add_tempo(&track, 480, 1000000);
    add_note(&track, 0, 240, 60);
    add_note(&track, 480, 240, 62);
    add_note(&track, 960, 240, 64);
    TemporaryMidi midi(build_midi(0, 480, {track}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(), &notes, &stats);
    if (!status.ok()) return fail("tempo-change fixture failed: " + status.message);
    if (notes.size() != 3 || std::fabs(notes[0].beat - 0.0) > 0.001 ||
        std::fabs(notes[1].beat - 1.0) > 0.001 || std::fabs(notes[2].beat - 3.0) > 0.001) {
        return fail("tempo-map timing was not preserved");
    }
    return 0;
}

bool notes_include(
    const std::vector<ff7rp::pipeline::Note>& superset,
    const std::vector<ff7rp::pipeline::Note>& subset) {
    return std::all_of(subset.begin(), subset.end(), [&](const auto& expected) {
        return std::any_of(superset.begin(), superset.end(), [&](const auto& actual) {
            return std::fabs(actual.beat - expected.beat) <= 1e-9 &&
                actual.pitch == expected.pitch && actual.chord_id == expected.chord_id;
        });
    });
}

int test_half_beat_phrase_profiles() {
    MidiTrack track;
    add_tempo(&track, 0, 857143); // 70 BPM: half a beat is about 429 ms.
    constexpr int note_count = 16;
    for (int index = 0; index < note_count; ++index) {
        add_note(&track, index * 240, 1920, 72 + index % 5, 110);
    }
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    const std::array<double, 6> minimum_seconds{{0.11, 0.11, 0.09, 0.08, 0.08, 0.08}};
    std::array<std::size_t, 6> counts{};
    std::vector<ff7rp::pipeline::Note> level_six;
    for (int difficulty = 1; difficulty <= 6; ++difficulty) {
        std::vector<ff7rp::pipeline::Note> notes;
        ff7rp::pipeline::MidiChartStats stats;
        const auto status = generate(midi.path(), fixture_config(difficulty), &notes, &stats);
        if (!status.ok()) return fail("half-beat phrase fixture failed: " + status.message);
        counts[static_cast<std::size_t>(difficulty - 1)] = stats.right_events;
        if (stats.right_events > 1 &&
            stats.minimum_right_gap_seconds + 0.000001 <
                minimum_seconds[static_cast<std::size_t>(difficulty - 1)]) {
            return fail("half-beat phrase violated the wall-clock safety floor");
        }
        if (difficulty == 6) level_six = notes;
    }
    if (!std::is_sorted(counts.begin(), counts.end())) {
        return fail("half-beat phrase profiles were not monotonic by difficulty");
    }
    if (counts[5] != note_count || counts[0] >= counts[5]) {
        std::string observed;
        for (const std::size_t count : counts) observed += " " + std::to_string(count);
        return fail("candidate budgets did not produce a monotonic path to the complete phrase:" + observed);
    }
    std::vector<ff7rp::pipeline::Note> repeated;
    ff7rp::pipeline::MidiChartStats repeated_stats;
    const auto repeated_status = generate(midi.path(), fixture_config(6), &repeated, &repeated_stats);
    if (!repeated_status.ok() || !notes_equal(level_six, repeated)) {
        return fail("half-beat phrase selection was not deterministic");
    }
    return 0;
}

int test_authored_vanilla_display_envelopes() {
    // Provenance: Analysis/VanillaPiano/piano_dataobject_decoded.json, PianoScore
    // TimeStr rows decoded at 60 Hz. Unresolved actions/times are excluded and
    // duplicate native frames are coalesced using the shipping UI mapping.
    struct VanillaMetrics {
        const char* song;
        int display_level;
        std::size_t raw_rows;
        std::size_t resolved_rows;
        double joint_strain_p95;
        double joint_strain_peak;
    };
    // Provenance: Analysis/VanillaPiano/piano_dataobject_decoded.json, PianoScore rows,
    // user-verified shipping UI mapping. Only resolved TimeStr/pitch/chord values are
    // measured. The unresolved remainder is unknown evidence, never zero-demand input.
    const std::array<VanillaMetrics, 8> authored{{
        {"Journey / On Our Way", 1, 212, 124, 2.791, 3.325},
        {"TifasTheme", 2, 199, 57, 2.663, 2.815},
        {"BarretTheme", 3, 378, 360, 4.214, 5.288},
        {"SyncoDeChocobo", 3, 287, 219, 3.443, 3.871},
        {"DifficultToStandOnTwoLegs", 4, 433, 395, 5.009, 5.731},
        {"AerithsTheme", 4, 262, 257, 2.995, 4.296},
        {"Fighters / Let the Battles Begin", 5, 337, 277, 4.252, 4.723},
        {"OneWingedAngel", 6, 277, 218, 3.261, 4.620},
    }};
    const std::array<double, 6> expected_p95{{2.80, 3.00, 4.25, 5.05, 5.20, 5.40}};
    const std::array<double, 6> expected_peak{{3.35, 3.55, 5.30, 5.75, 6.00, 6.25}};
    const std::array<double, 6> expected_apm{{78.0, 90.0, 104.0, 120.0, 138.0, 158.0}};
    for (int difficulty = 1; difficulty <= 6; ++difficulty) {
        const auto envelope = ff7rp::pipeline::midi_difficulty_envelope(difficulty);
        const std::size_t index = static_cast<std::size_t>(difficulty - 1);
        if (envelope.maximum_joint_strain_p95 != expected_p95[index] ||
            envelope.maximum_joint_strain_peak != expected_peak[index]) {
            return fail("custom profile does not match the joint strain envelope at Lv." +
                std::to_string(difficulty));
        }
        const auto spec = ff7rp::pipeline::midi_difficulty_spec(difficulty);
        if (spec.target_actions_per_minute != expected_apm[index] ||
            spec.target_tolerance != 0.06 || spec.route_count != (difficulty == 3 || difficulty == 4 ? 2u : 1u)) {
            return fail("custom profile lost its smooth target or coherent authored-route calibration at Lv." +
                std::to_string(difficulty));
        }
    }
    for (const VanillaMetrics& metrics : authored) {
        if (metrics.resolved_rows == 0 || metrics.resolved_rows >= metrics.raw_rows ||
            metrics.joint_strain_p95 <= 0.0 || metrics.joint_strain_peak < metrics.joint_strain_p95) {
            return fail(std::string(metrics.song) + " lost resolved/unresolved Vanilla provenance");
        }
    }
    MidiTrack track;
    add_note(&track, 0, 120, 60);
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto level_seven = generate(midi.path(), fixture_config(7), &notes, &stats);
    if (level_seven.ok() || level_seven.message.find("between 1 and 6") == std::string::npos) {
        return fail("generator still exposes custom Lv.7");
    }
    return 0;
}

int test_duration_normalized_dense_texture() {
    const auto make_track = [](const int note_count) {
        MidiTrack track;
        add_tempo(&track, 0);
        for (int index = 0; index < note_count; ++index) {
            add_note(&track, index * 60, 180, 72 + index % 7, 108);
        }
        return track;
    };
    const TemporaryMidi short_midi(build_midi(0, 480, {make_track(240)}));
    const TemporaryMidi long_midi(build_midi(0, 480, {make_track(480)}));
    for (int difficulty = 1; difficulty <= 6; ++difficulty) {
        std::vector<ff7rp::pipeline::Note> short_notes;
        std::vector<ff7rp::pipeline::Note> long_notes;
        ff7rp::pipeline::MidiChartStats short_stats;
        ff7rp::pipeline::MidiChartStats long_stats;
        auto status = generate(short_midi.path(), fixture_config(difficulty), &short_notes, &short_stats);
        if (!status.ok()) return fail("short duration-invariance fixture failed: " + status.message);
        status = generate(long_midi.path(), fixture_config(difficulty), &long_notes, &long_stats);
        if (!status.ok()) return fail("long duration-invariance fixture failed: " + status.message);
        if (difficulty == 1 && short_stats.local_skill_rejections == 0) {
            return fail("selector did not prune any locally impossible dense-texture branches");
        }
        const NoteMetrics short_metrics = note_metrics(short_notes, 0.5);
        const NoteMetrics long_metrics = note_metrics(long_notes, 0.5);
        const double count_ratio = static_cast<double>(long_metrics.actions) / short_metrics.actions;
        if (count_ratio < 1.85 || count_ratio > 2.15 ||
            short_notes.size() < short_stats.target_minimum_rows || short_notes.size() > short_stats.target_maximum_rows ||
            long_notes.size() < long_stats.target_minimum_rows || long_notes.size() > long_stats.target_maximum_rows ||
            short_notes.size() >= short_stats.candidate_frames || long_notes.size() >= long_stats.candidate_frames) {
            return fail("target-demand objective did not select the calibrated band below maximum feasible count at Lv." +
                std::to_string(difficulty));
        }
        for (std::size_t window = 0; window < short_metrics.bursts.size(); ++window) {
            if (std::abs(static_cast<int>(short_metrics.bursts[window]) -
                    static_cast<int>(long_metrics.bursts[window])) > 5) {
                return fail("same texture changed rolling burst demand with duration at Lv." +
                    std::to_string(difficulty) + ": " +
                    std::to_string(short_metrics.bursts[window]) + " vs " +
                    std::to_string(long_metrics.bursts[window]));
            }
        }
    }

    return 0;
}

int test_local_multi_skill_metrics() {
    const auto right_note = [](const double seconds, const std::string& pitch) {
        ff7rp::pipeline::Note note;
        note.beat = seconds;
        note.duration_beats = 0.1;
        note.pitch = pitch;
        return note;
    };
    std::vector<ff7rp::pipeline::Note> hidden_passage;
    for (int index = 0; index < 400; ++index) hidden_passage.push_back(right_note(index * 1.0, "Cn4"));
    for (int index = 0; index < 20; ++index) hidden_passage.push_back(right_note(200.0 + index * 0.05, "Dn4"));
    const auto hidden = ff7rp::pipeline::analyze_midi_local_skills(hidden_passage, 60.0);
    if (hidden.maximum_window_actions[0] < 10 || hidden.maximum_window_actions[1] < 20) {
        return fail("impossible 20-action local passage hid inside 400 easy actions");
    }

    std::vector<ff7rp::pipeline::Note> burst;
    std::vector<ff7rp::pipeline::Note> stream;
    for (int index = 0; index < 60; ++index) {
        burst.push_back(right_note(10.0 + index * 3.0 / 59.0, "Cn4"));
        stream.push_back(right_note(10.0 + index * 25.0 / 59.0, "Cn4"));
    }
    const auto burst_metrics = ff7rp::pipeline::analyze_midi_local_skills(burst, 60.0);
    const auto stream_metrics = ff7rp::pipeline::analyze_midi_local_skills(stream, 60.0);
    if (burst_metrics.maximum_window_actions[3] <= stream_metrics.maximum_window_actions[3] ||
        burst_metrics.maximum_quarter_second_stream_duration <= stream_metrics.maximum_quarter_second_stream_duration ||
        stream_metrics.maximum_half_second_stream_duration < 24.0) {
        return fail("3s burst and 25s stream were not distinguished by local windows and stream duration");
    }

    const std::array<const char*, 6> scale{{"Cn4", "Dn4", "En4", "Fn4", "Gn4", "An4"}};
    std::vector<ff7rp::pipeline::Note> jacks;
    std::vector<ff7rp::pipeline::Note> alternation;
    std::vector<ff7rp::pipeline::Note> octave_reversals;
    std::vector<ff7rp::pipeline::Note> small_movements;
    for (int index = 0; index < 24; ++index) {
        const double seconds = index * 0.25;
        jacks.push_back(right_note(seconds, "Cn4"));
        alternation.push_back(right_note(seconds, scale[static_cast<std::size_t>(index) % scale.size()]));
        octave_reversals.push_back(right_note(seconds, index % 2 == 0 ? "Cn4" : "Cn6"));
        small_movements.push_back(right_note(seconds, index % 2 == 0 ? "Cn4" : "Dn4"));
    }
    const auto jack_metrics = ff7rp::pipeline::analyze_midi_local_skills(jacks, 60.0);
    const auto alternate_metrics = ff7rp::pipeline::analyze_midi_local_skills(alternation, 60.0);
    const auto octave_metrics = ff7rp::pipeline::analyze_midi_local_skills(octave_reversals, 60.0);
    const auto small_metrics = ff7rp::pipeline::analyze_midi_local_skills(small_movements, 60.0);
    if (jack_metrics.maximum_jack_run < 20 || alternate_metrics.maximum_jack_run > 1 ||
        octave_metrics.rapid_movement_p90 < 20.0 || octave_metrics.maximum_large_reversals_in_five_seconds < 10 ||
        small_metrics.rapid_movement_p90 >= octave_metrics.rapid_movement_p90) {
        return fail("jack, alternation, octave, and reversal skills collapsed into one scalar");
    }

    std::vector<ff7rp::pipeline::Note> regular;
    std::vector<ff7rp::pipeline::Note> irregular;
    double irregular_seconds = 0.0;
    for (int index = 0; index < 40; ++index) {
        regular.push_back(right_note(index * 0.5, "Cn4"));
        irregular.push_back(right_note(irregular_seconds, "Cn4"));
        irregular_seconds += index % 2 == 0 ? 0.1 : 0.9;
    }
    const auto regular_metrics = ff7rp::pipeline::analyze_midi_local_skills(regular, 60.0);
    const auto irregular_metrics = ff7rp::pipeline::analyze_midi_local_skills(irregular, 60.0);
    if (irregular_metrics.rhythm_irregularity_p90 <= regular_metrics.rhythm_irregularity_p90 + 2.0) {
        return fail("regular and irregular rhythm produced indistinguishable IOI-ratio demand");
    }

    std::vector<ff7rp::pipeline::Note> recovered = regular;
    for (std::size_t index = 1; index < recovered.size(); index += 2) {
        recovered[index].pitch.clear();
        recovered[index].chord_id = "pca_C_major";
    }
    const auto recovered_metrics = ff7rp::pipeline::analyze_midi_local_skills(recovered, 60.0);
    if (regular_metrics.right_fatigue_peak <= recovered_metrics.right_fatigue_peak + 1.0 ||
        recovered_metrics.left_fatigue_peak <= 1.0 || recovered_metrics.hand_imbalance >= regular_metrics.hand_imbalance) {
        return fail("single-hand fatigue and alternating-hand recovery were not separated");
    }
    std::vector<ff7rp::pipeline::Note> sparse_easy;
    for (int index = 0; index < 18; ++index) {
        sparse_easy.push_back(right_note(index * 14.875 / 17.0,
            scale[static_cast<std::size_t>(index) % scale.size()]));
    }
    const auto easy_validation = ff7rp::pipeline::validate_midi_difficulty_route(sparse_easy, 60.0, 1);
    if (!easy_validation.feasible) {
        return fail("shared route validator rejected a feasible sparse easy chart: ratio=" +
            std::to_string(easy_validation.ratio) + " margin=" + std::to_string(easy_validation.margin) +
            " skill=" + std::to_string(easy_validation.metrics.dominant_skill));
    }
    return 0;
}

int test_global_native_frame_conflict_policy() {
    MidiTrack track;
    add_tempo(&track, 0);
    constexpr int onset_count = 5;
    for (int index = 0; index < onset_count; ++index) {
        const int tick = index * 960;
        add_note(&track, tick, 18, 48, 90);
        add_note(&track, tick, 18, 52, 90);
        add_note(&track, tick, 18, 55, 90);
        add_note(&track, tick, 18, 72 + index % 5, 110);
    }
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    std::vector<ff7rp::pipeline::Note> notes;
    std::vector<ff7rp::pipeline::Note> repeated;
    ff7rp::pipeline::MidiChartStats stats;
    ff7rp::pipeline::MidiChartStats repeated_stats;
    auto status = generate(midi.path(), fixture_config(6), &notes, &stats);
    if (!status.ok()) return fail("native-frame conflict fixture failed: " + status.message);
    status = generate(midi.path(), fixture_config(6), &repeated, &repeated_stats);
    if (!status.ok() || !notes_equal(notes, repeated)) {
        return fail("native-frame conflict resolution was not deterministic");
    }
    // Each frame now offers the melody, one bounded alternate, and the chord.
    // Exactly one wins; neither discarded action is retimed into another frame.
    if (stats.right_events + stats.left_events != notes.size() ||
        stats.merged_events != 0 || stats.cross_hand_conflicts != onset_count ||
        stats.scheduled_conflicts != 0 || stats.dropped_conflicts != onset_count * 2 ||
        stats.selected_actions != onset_count || stats.candidate_actions != onset_count * 3 ||
        stats.candidate_frames != onset_count ||
        stats.source_pitch_witness_failures != 0) {
        return fail("cross-hand conflicts were not deterministically reduced to exact-frame winners");
    }
    std::set<long long> frames;
    for (const auto& note : notes) {
        if (note.pitch.empty() == note.chord_id.empty()) {
            return fail("generated row did not contain exactly one gameplay action");
        }
        frames.insert(std::llround(note.beat * 0.5 * 60.0));
    }
    if (frames.size() != stats.selected_actions) return fail("generated chart changed independent input frames");
    const long long final_source_frame = (onset_count - 1) * 60;
    if (frames.rbegin() == frames.rend() || *frames.rbegin() != final_source_frame ||
        notes.back().pitch.empty() ||
        std::llround(notes.back().beat * 0.5 * 60.0) != final_source_frame) {
        return fail("conflict resolution changed the final real source onset");
    }
    return 0;
}

int test_globally_unschedulable_conflict_drop() {
    MidiTrack track;
    add_tempo(&track, 0);
    add_note(&track, 0, 60, 48, 100);
    add_note(&track, 0, 60, 52, 100);
    add_note(&track, 0, 60, 55, 100);
    add_note(&track, 0, 60, 72, 120);
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(6), &notes, &stats);
    if (!status.ok() || notes.size() != 4 || notes.front().pitch != "C5" ||
        stats.selected_actions != 1 || stats.candidate_actions != 3 || stats.cross_hand_conflicts != 1 ||
        stats.scheduled_conflicts != 0 || stats.dropped_conflicts != 2 ||
        stats.desired_rows != 4) {
        return fail("globally full one-frame timing domain did not report its dropped alternate and chord actions");
    }
    return 0;
}

int test_timing_domain_bounds() {
    MidiTrack pre_lead_track;
    add_tempo(&pre_lead_track, 0);
    add_note(&pre_lead_track, 0, 60, 72, 110);
    add_note(&pre_lead_track, 480, 60, 74, 110);
    add_note(&pre_lead_track, 960, 60, 76, 110);
    const TemporaryMidi pre_lead_midi(build_midi(0, 480, {pre_lead_track}));
    auto pre_lead_config = fixture_config(6);
    pre_lead_config.midi_minimum_lead_in_seconds = 1.501;
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    auto status = generate(pre_lead_midi.path(), pre_lead_config, &notes, &stats);
    if (status.code != ff7rp::pipeline::StatusCode::InvalidChart || !notes.empty() ||
        stats.lead_in_rejections == 0) {
        return fail("all-pre-lead-in source attacks were reinserted by a later transformation");
    }

    MidiTrack fractional_track;
    add_tempo(&fractional_track, 0);
    add_note(&fractional_track, 1440, 8, 72, 110); // 1.500s, native frame 90.
    add_note(&fractional_track, 1456, 8, 74, 110); // 1.5167s, native frame 91.
    add_note(&fractional_track, 1920, 8, 76, 110);
    const TemporaryMidi fractional_midi(build_midi(0, 480, {fractional_track}));
    notes.clear();
    stats = {};
    status = generate(fractional_midi.path(), pre_lead_config, &notes, &stats);
    if (!status.ok() || notes.empty() || stats.lead_in_rejections == 0) {
        return fail("fractional lead-in fixture did not generate its eligible source actions");
    }
    for (const auto& note : notes) {
        if (std::llround(note.beat * 0.5 * 60.0) < 91) {
            return fail("fractional lead-in emitted a row below the native-frame minimum");
        }
    }
    if (std::llround(notes.front().beat * 0.5 * 60.0) != 91) {
        return fail("fractional lead-in did not retain the first eligible native frame");
    }

    MidiTrack audio_track;
    add_tempo(&audio_track, 0);
    add_note(&audio_track, 960, 60, 72, 110);
    add_note(&audio_track, 2400, 60, 76, 110);
    const TemporaryMidi audio_midi(build_midi(0, 480, {audio_track}));
    ff7rp::pipeline::WavAudio audio;
    audio.sample_rate = 60;
    audio.source_frame_count = 120;
    notes.clear();
    stats = {};
    status = ff7rp::pipeline::generate_notes_from_midi(
        audio_midi.path().string(), audio, fixture_config(6), &notes, &stats);
    if (!status.ok() || notes.empty() || stats.audio_duration_rejections == 0) {
        return fail("source onset beyond known audio duration was not rejected explicitly");
    }
    for (const auto& note : notes) {
        if (std::llround(note.beat * 0.5 * 60.0) > 120) {
            return fail("generated chart extended beyond known audio duration");
        }
    }
    return 0;
}

int test_runtime_row_ceiling_omits_instead_of_truncating() {
    MidiTrack exact_track;
    add_tempo(&exact_track, 0);
    int exact_tick = 0;
    for (int index = 0; index < 512; ++index) {
        if (index % 16 == 0) {
            add_note(&exact_track, exact_tick, 60, 48, 100);
            add_note(&exact_track, exact_tick, 60, 52, 100);
            add_note(&exact_track, exact_tick, 60, 55, 100);
        } else {
            add_note(&exact_track, exact_tick, 60, 60 + index % 7, 110);
        }
        exact_tick += index % 2 == 0 ? 240 : 528;
    }
    const TemporaryMidi exact_midi(build_midi(0, 480, {exact_track}));
    std::vector<ff7rp::pipeline::Note> exact_notes;
    ff7rp::pipeline::MidiChartStats exact_stats;
    const auto exact_status = generate(exact_midi.path(), fixture_config(6), &exact_notes, &exact_stats);
    if (!exact_status.ok() || exact_notes.size() != ff7rp::pipeline::kMaxChartRows ||
        exact_stats.row_limit_exceeded) {
        return fail("complete 512-row profile was not accepted: " + exact_status.message +
            " rows=" + std::to_string(exact_notes.size()));
    }

    MidiTrack track;
    add_tempo(&track, 0);
    constexpr int source_onsets = 1800;
    for (int index = 0; index < source_onsets; ++index) {
        add_note(&track, index * 120, 60, 72, 110);
    }
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), fixture_config(6), &notes, &stats);
    if (!status.ok() || notes.empty() || notes.size() > ff7rp::pipeline::kMaxChartRows ||
        stats.row_limit_exceeded || stats.source_pitch_witness_failures != 0 ||
        !ff7rp::pipeline::validate_midi_difficulty_route(notes, stats.source_bpm, 6).feasible ||
        (notes.back().beat - notes.front().beat) * 60.0 / stats.source_bpm < 215.0) {
        return fail("soft-density 512-row capacity lost complete feasible coverage: status=" +
            std::to_string(static_cast<int>(status.code)) + " notes=" + std::to_string(notes.size()) +
            " target_minimum=" + std::to_string(stats.target_minimum_rows) + " right=" +
            std::to_string(stats.right_events) + " row_limit=" +
            std::to_string(stats.row_limit_exceeded));
    }
    return 0;
}

int test_authored_vanilla_mode_change_counts() {
    using ff7rp::pipeline::vanilla_mode_change_counts_for_route;
    const std::array<std::pair<const char*, std::array<int, 2>>, 8> expected{{
        {"Journey", {5, 10}},
        {"Tifa", {14, 24}},
        {"Barret", {12, 20}},
        {"Synco", {10, 20}},
        {"Difficult", {11, 22}},
        {"Aerith", {10, 20}},
        {"Fighters", {15, 30}},
        {"OneWingedAngel", {8, 24}},
    }};
    for (const auto& [route, counts] : expected) {
        if (vanilla_mode_change_counts_for_route(route) != counts) {
            return fail(std::string("authored Vanilla mode-change counts changed for route ") + route);
        }
    }
    if (vanilla_mode_change_counts_for_route("Unknown") != std::array<int, 2>{0, 0}) {
        return fail("unknown Vanilla route did not preserve fallback mode-change counts");
    }
    return 0;
}

int test_imported_profile_bands() {
    const std::vector<std::filesystem::path> fixtures = synthetic_profile_fixtures();
    const std::set<std::string> expected_fixtures{
        "SyntheticCrossingVoices", "SyntheticHumanized", "SyntheticLongHarmony",
        "SyntheticLongScale", "SyntheticMeterChanges", "SyntheticPhraseBursts",
    };
    std::set<std::string> discovered_fixtures;
    for (const auto& path : fixtures) discovered_fixtures.insert(path.parent_path().filename().string());
    if (discovered_fixtures != expected_fixtures || fixtures.size() != expected_fixtures.size()) {
        return fail("synthetic profile matrix did not create exactly six fixtures");
    }
    std::vector<std::filesystem::path> selected_fixtures;
    for (std::size_t fixture_index = 0; fixture_index < fixtures.size(); ++fixture_index) {
        if (!imported_fixture_group.empty()) {
            const std::string group = "," + imported_fixture_group + ",";
            if (group.find("," + std::to_string(fixture_index) + ",") == std::string::npos) continue;
        }
        selected_fixtures.push_back(fixtures[fixture_index]);
    }
    struct FixtureResult {
        std::string failures;
        std::string observed;
        std::string diagnostics;
    };
    const auto test_fixture = [](const std::filesystem::path& path) {
        FixtureResult result;
        const std::string fixture = path.parent_path().filename().string();
        const auto fixture_started = std::chrono::steady_clock::now();
        std::size_t complete_profiles = 0;
        std::set<int> visible_difficulties;
        std::vector<ff7rp::pipeline::Note> visible_baseline;
        int previous_visible_difficulty = 0;
        for (int difficulty = 1; difficulty <= 6; ++difficulty) {
            const auto profile_started = std::chrono::steady_clock::now();
            std::vector<ff7rp::pipeline::Note> notes;
            ff7rp::pipeline::MidiChartStats stats;
            auto config = fixture_config(difficulty);
            config.midi_minimum_lead_in_seconds = 2.0;
            const std::size_t maximum_visible_rows = visible_baseline.empty() ? 0 :
                ff7rp::pipeline::maximum_midi_visible_profile_actions(visible_baseline.size());
            const auto status = generate(path, config, &notes, &stats,
                visible_baseline.empty() ? nullptr : &visible_baseline, maximum_visible_rows);
            if (status.code == ff7rp::pipeline::StatusCode::ChartRowLimitExceeded ||
                status.code == ff7rp::pipeline::StatusCode::ChartStrainLimitExceeded) {
                if (status.code == ff7rp::pipeline::StatusCode::ChartStrainLimitExceeded && !notes.empty()) {
                    const auto witness = ff7rp::pipeline::validate_midi_difficulty_route(
                        notes, stats.source_bpm, difficulty);
                    if (notes.size() != stats.selected_actions ||
                        !local_skills_equal(witness.metrics, stats.local_skills)) {
                        result.failures += fixture + " Lv." + std::to_string(difficulty) +
                            " did not report one exact near-feasible witness; ";
                    }
                }
                result.observed += fixture + " L" + std::to_string(difficulty) + " omitted[" +
                    status.message + "]; ";
                continue;
            }
            if (!status.ok()) {
                result.failures += fixture + ": " + status.message + "; ";
                break;
            }
            if (!visible_baseline.empty() && stats.protected_baseline_actions != visible_baseline.size()) {
                result.failures += fixture + " Lv." + std::to_string(difficulty) +
                    " lost previous source-preference diagnostics; ";
            }
            const double seconds_per_beat = 60.0 / stats.source_bpm;
            const NoteMetrics metrics = note_metrics(notes, seconds_per_beat);
            result.observed += fixture + " L" + std::to_string(difficulty) + "=" +
                std::to_string(notes.size()) + "[c" + std::to_string(stats.melody_candidates + stats.fallback_candidates) +
                ",e" + std::to_string(stats.evidence_rejections) + ",cd" +
                std::to_string(stats.cooldown_rejections) + ",b" +
                std::to_string(stats.burst_rejections) + ",r" +
                std::to_string(stats.retention_rejections) + ",a" +
                std::to_string(stats.action_rate_rejections) + ",apm" +
                std::to_string(static_cast<int>(std::lround(stats.actions_per_minute))) + "]; ";
            if (notes.empty() || notes.size() > ff7rp::pipeline::effective_chart_row_limit() ||
                stats.target_minimum_rows > stats.target_rows || stats.target_rows > stats.target_maximum_rows ||
                stats.local_skills.satisfied_route < 0) {
                result.failures += fixture + " Lv." + std::to_string(difficulty) +
                    " lost coherent soft goals, physical capacity, or every real route; ";
            }
            const auto validation = ff7rp::pipeline::validate_midi_difficulty_route(
                notes, stats.source_bpm, difficulty);
            if (!validation.feasible || !local_skills_equal(validation.metrics, stats.local_skills)) {
                result.failures += fixture + " Lv." + std::to_string(difficulty) +
                    " diverged from the shared production full-route validator; ";
            }
            std::set<long long> frames;
            double previous_seconds = -1.0;
            for (const auto& note : notes) {
                if (note.pitch.empty() == note.chord_id.empty()) {
                    result.failures += fixture + " emitted a row without exactly one action; ";
                }
                const double seconds = note.beat * seconds_per_beat;
                frames.insert(std::llround(seconds * 60.0));
                previous_seconds = seconds;
            }
            if (frames.size() != notes.size()) {
                result.failures += fixture + " emitted simultaneous native-frame actions; ";
            }
            if (stats.source_pitch_witness_failures != 0 || stats.row_limit_exceeded) {
                result.failures += fixture + " lost a source witness or exposed an incomplete profile; ";
            }
            if ((fixture == "SyntheticLongScale" || fixture == "SyntheticLongHarmony") &&
                notes.back().beat * seconds_per_beat - notes.front().beat * seconds_per_beat < 115.0) {
                result.failures += fixture + " integration fixture no longer covers at least 115 seconds; ";
            }
            if (difficulty <= previous_visible_difficulty) {
                result.failures += fixture + " exposed non-monotonic difficulty labels; ";
            }
            visible_baseline = notes;
            previous_visible_difficulty = difficulty;
            ++complete_profiles;
            visible_difficulties.insert(difficulty);
            if (perf_diagnostics_enabled()) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - profile_started);
                result.diagnostics += "midi_generator_selftest profile_ms=" +
                    std::to_string(elapsed.count()) + " fixture=" + fixture + " difficulty=" +
                    std::to_string(difficulty) + "\n";
            }
        }
        const std::size_t required_profiles =
            fixture == "SyntheticLongScale" || fixture == "SyntheticLongHarmony" ? 3 : 1;
        if (complete_profiles < required_profiles) {
            result.failures += fixture + " hid too many complete higher profiles; ";
        }
        if (fixture == "SyntheticLongHarmony" &&
            (!visible_difficulties.count(1) || !visible_difficulties.count(2) ||
                !visible_difficulties.count(3))) {
            result.failures += "synthetic long-harmony fixture lost an easy profile; ";
        }
        if (perf_diagnostics_enabled()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - fixture_started);
            result.diagnostics += "midi_generator_selftest fixture_ms=" +
                std::to_string(elapsed.count()) + " fixture=" + fixture + "\n";
        }
        return result;
    };

    std::vector<FixtureResult> results;
    results.reserve(selected_fixtures.size());
    for (const auto& path : selected_fixtures) results.push_back(test_fixture(path));
    std::string failures;
    std::string observed;
    for (const auto& result : results) {
        failures += result.failures;
        observed += result.observed;
        if (perf_diagnostics_enabled()) std::cerr << result.diagnostics;
    }
    if (!failures.empty()) return fail(failures + " observed: " + observed);
    return 0;
}

int test_minimum_lead_in() {
    MidiTrack track;
    add_tempo(&track, 0);
    for (int index = 0; index < 6; ++index) {
        add_note(&track, index * 480, 240, 60 + index, 100);
    }
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    auto config = fixture_config();
    config.midi_minimum_lead_in_seconds = 1.5;
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), config, &notes, &stats);
    if (!status.ok() || notes.empty()) return fail("minimum lead-in fixture did not generate notes");
    for (const auto& note : notes) {
        const double seconds = note.beat * 0.5;
        if (seconds + 0.000001 < 1.5) return fail("minimum lead-in emitted an early prompt");
    }
    if (std::fabs(notes.front().beat * 0.5 - 1.5) > 0.01) {
        return fail("minimum lead-in did not preserve the first eligible MIDI attack");
    }
    return 0;
}

int test_source_supported_gap_coverage() {
    const auto maximum_gap_seconds = [](const std::vector<ff7rp::pipeline::Note>& notes) {
        double maximum = 0.0;
        for (std::size_t index = 1; index < notes.size(); ++index) {
            maximum = std::max(maximum, (notes[index].beat - notes[index - 1].beat) * 0.5);
        }
        return maximum;
    };
    MidiTrack avoidable;
    add_tempo(&avoidable, 0);
    for (int index = 0; index < 320; ++index) {
        add_note(&avoidable, index * 120, 60, 60 + index % 7, 100);
    }
    const TemporaryMidi avoidable_midi(build_midi(0, 480, {avoidable}));
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    auto status = generate(avoidable_midi.path(), fixture_config(1), &notes, &stats);
    if (!status.ok() || maximum_gap_seconds(notes) > 8.01) {
        return fail("selector left an avoidable source-supported gap above eight seconds");
    }

    MidiTrack genuine_rest;
    add_tempo(&genuine_rest, 0);
    for (int index = 0; index < 10; ++index) {
        add_note(&genuine_rest, index * 480, 120, 60 + index % 7, 100);
        add_note(&genuine_rest, (40 + index) * 480, 120, 67 + index % 7, 100);
    }
    const TemporaryMidi genuine_rest_midi(build_midi(0, 480, {genuine_rest}));
    notes.clear();
    stats = {};
    status = generate(genuine_rest_midi.path(), fixture_config(1), &notes, &stats);
    if (!status.ok() || maximum_gap_seconds(notes) <= 8.01) {
        return fail("selector rejected or filled a genuine source rest above eight seconds");
    }
    return 0;
}

int test_tail_coverage() {
    MidiTrack track;
    add_tempo(&track, 0);
    constexpr int note_count = 120;
    for (int index = 0; index < note_count; ++index) {
        add_note(&track, index * 120, 60, 72 + index % 7, index + 1 == note_count ? 1 : 127);
    }
    const TemporaryMidi midi(build_midi(0, 480, {track}));
    auto config = fixture_config(1);
    std::vector<ff7rp::pipeline::Note> notes;
    ff7rp::pipeline::MidiChartStats stats;
    const auto status = generate(midi.path(), config, &notes, &stats);
    if (!status.ok() || notes.empty()) return fail("tail-coverage fixture did not generate notes: " + status.message);
    constexpr double expected_last_seconds = (note_count - 1) * 0.125;
    const double last_seconds = notes.back().beat * 0.5;
    if (std::fabs(last_seconds - expected_last_seconds) > 0.01) {
        return fail("difficulty reduction dropped the final real MIDI onset");
    }
    return 0;
}

int run_imported_fixture_processes(const bool inject_unexpected_exception = false) {
    constexpr std::array<const wchar_t*, 4> fixture_groups{L"3", L"4", L"0,2", L"1,5"};
    constexpr std::array<const char*, 4> fixture_group_names{
        "SyntheticLongScale", "SyntheticLongHarmony", "SyntheticMixedA", "SyntheticMixedB"};
    constexpr std::array<unsigned int, 4> partition_sizes{5, 4, 4, 2};
    constexpr DWORD kWorkerTimeoutMilliseconds = 120000;
    constexpr DWORD kTerminationWaitMilliseconds = 10000;
    const unsigned int processor_count = bit_count(imported_worker_affinity);
    if (!use_process_fixture_partitions || processor_count < fixture_groups.size() * 2u) {
        return test_imported_profile_bands();
    }

    std::array<wchar_t, MAX_PATH> executable{};
    const DWORD executable_length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (executable_length == 0 || executable_length >= executable.size()) {
        return fail("could not resolve the selftest executable path");
    }
    struct ChildProcess {
        UniqueHandle process;
        std::filesystem::path output_path;
    };
    std::array<ChildProcess, fixture_groups.size()> children;
    ff7rp::tests::TemporaryDirectory output_root("ff7rp-midi-worker-logs");
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) return fail("could not create imported-fixture worker job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
    job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
            &job_limits, sizeof(job_limits))) {
        return fail("could not configure imported-fixture worker job");
    }

    const auto emit_and_remove_outputs = [&]() {
        bool cleanup_ok = true;
        for (ChildProcess& child : children) {
            if (child.output_path.empty()) continue;
            std::ifstream output(child.output_path, std::ios::binary);
            if (output.peek() != std::ifstream::traits_type::eof()) std::cerr << output.rdbuf();
            output.close();
            std::error_code error;
            std::filesystem::remove(child.output_path, error);
            if (error) {
                cleanup_ok = false;
                std::cerr << "worker-log cleanup failed: " << child.output_path.string()
                          << ": " << error.message() << '\n';
            }
        }
        return cleanup_ok;
    };
    const auto terminate_wait_and_close = [&]() noexcept {
        bool cleanup_ok = true;
        if (job && !TerminateJobObject(job.get(), 1)) cleanup_ok = false;
        job.reset(); // KILL_ON_JOB_CLOSE is the final guarantee if explicit termination fails.
        std::array<HANDLE, fixture_groups.size()> processes{};
        DWORD process_count = 0;
        for (const ChildProcess& child : children) {
            if (child.process) processes[process_count++] = child.process.get();
        }
        if (process_count > 0) {
            cleanup_ok = WaitForMultipleObjects(
                process_count, processes.data(), TRUE, kTerminationWaitMilliseconds) == WAIT_OBJECT_0
                && cleanup_ok;
        }
        for (ChildProcess& child : children) {
            child.process.reset();
        }
        return cleanup_ok;
    };

    const auto cleanup_output_root = [&]() {
        std::string cleanup_error;
        if (cleanup_temp_directory(&output_root, &cleanup_error)) return true;
        std::cerr << "worker temp-root cleanup failed: " << cleanup_error << '\n';
        return false;
    };

    try {
        const auto workers_started = std::chrono::steady_clock::now();
        std::string launch_error;
        DWORD_PTR unassigned_affinity = imported_worker_affinity;
        for (std::size_t index = 0; index < children.size(); ++index) {
            DWORD_PTR affinity = 0;
            for (unsigned int offset = 0; offset < partition_sizes[index]; ++offset) {
                affinity |= take_lowest_bit(&unassigned_affinity);
            }

            children[index].output_path = output_root.path() /
                ("fixture-group-" + std::to_string(index) + ".log");
            SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
            UniqueHandle output(CreateFileW(children[index].output_path.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr));
            if (!output) {
                launch_error = "could not create imported-fixture output file for " +
                    std::string(fixture_group_names[index]);
                break;
            }

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            startup.hStdOutput = output.get();
            startup.hStdError = output.get();
            PROCESS_INFORMATION process{};
            std::wstring command = L"\"" + std::wstring(executable.data()) +
                L"\" --imported-fixtures-only " + fixture_groups[index];
            const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
            output.reset();
            if (!created) {
                launch_error = "could not start imported-fixture worker process for " +
                    std::string(fixture_group_names[index]);
                break;
            }
            UniqueHandle process_handle(process.hProcess);
            UniqueHandle thread_handle(process.hThread);
            if (!AssignProcessToJobObject(job.get(), process_handle.get())) {
                const BOOL terminated = TerminateProcess(process_handle.get(), 1);
                const DWORD wait_result = WaitForSingleObject(
                    process_handle.get(), kTerminationWaitMilliseconds);
                launch_error = "could not assign imported-fixture worker job for " +
                    std::string(fixture_group_names[index]) +
                    (terminated && wait_result == WAIT_OBJECT_0 ? "" : " (partial process cleanup failed)");
                break;
            }
            children[index].process = std::move(process_handle);
            if (!SetProcessAffinityMask(children[index].process.get(), affinity)) {
                launch_error = "could not constrain imported-fixture worker affinity for " +
                    std::string(fixture_group_names[index]);
                break;
            }
            if (ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
                launch_error = "could not resume imported-fixture worker for " +
                    std::string(fixture_group_names[index]);
                break;
            }
            if (inject_unexpected_exception && index == 0u) {
                throw std::runtime_error("injected unexpected worker-orchestration exception");
            }
        }
        if (!launch_error.empty()) {
            const bool process_cleanup = terminate_wait_and_close();
            const bool output_cleanup = emit_and_remove_outputs();
            const bool root_cleanup = cleanup_output_root();
            if (!process_cleanup || !output_cleanup || !root_cleanup) launch_error += " (worker cleanup failed)";
            return fail(launch_error);
        }

        std::array<HANDLE, fixture_groups.size()> processes{};
        for (std::size_t index = 0; index < children.size(); ++index) {
            processes[index] = children[index].process.get();
        }
        const DWORD wait_result = WaitForMultipleObjects(
            static_cast<DWORD>(processes.size()), processes.data(), TRUE, kWorkerTimeoutMilliseconds);
        if (wait_result != WAIT_OBJECT_0) {
            std::string unfinished;
            for (std::size_t index = 0; index < children.size(); ++index) {
                if (WaitForSingleObject(children[index].process.get(), 0) == WAIT_TIMEOUT) {
                    if (!unfinished.empty()) unfinished += ", ";
                    unfinished += fixture_group_names[index];
                }
            }
            std::string reason = wait_result == WAIT_TIMEOUT ?
                "imported-fixture workers timed out: " + unfinished :
                "waiting for imported-fixture workers failed; unfinished: " + unfinished;
            const bool process_cleanup = terminate_wait_and_close();
            const bool output_cleanup = emit_and_remove_outputs();
            const bool root_cleanup = cleanup_output_root();
            if (!process_cleanup || !output_cleanup || !root_cleanup) reason += " (worker cleanup failed)";
            return fail(reason);
        }

        bool failed = false;
        for (ChildProcess& child : children) {
            DWORD exit_code = 1;
            const BOOL queried = GetExitCodeProcess(child.process.get(), &exit_code);
            child.process.reset();
            failed = failed || !queried || exit_code != 0;
        }
        job.reset();
        const bool output_cleanup = emit_and_remove_outputs();
        const bool root_cleanup = cleanup_output_root();
        if (perf_diagnostics_enabled()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - workers_started);
            std::cerr << "midi_generator_selftest worker_wall_ms=" << elapsed.count() << '\n';
        }
        return failed || !output_cleanup || !root_cleanup ? 1 : 0;
    } catch (const std::exception& error) {
        const bool process_cleanup = terminate_wait_and_close();
        bool output_cleanup = false;
        bool root_cleanup = false;
        try { output_cleanup = emit_and_remove_outputs(); } catch (...) {}
        try { root_cleanup = cleanup_output_root(); } catch (...) {}
        if (inject_unexpected_exception && process_cleanup && output_cleanup && root_cleanup) return 2;
        return fail(std::string("unexpected imported-fixture worker exception: ") + error.what() +
            (process_cleanup && output_cleanup && root_cleanup ? "" : " (worker cleanup failed)"));
    } catch (...) {
        const bool process_cleanup = terminate_wait_and_close();
        bool output_cleanup = false;
        bool root_cleanup = false;
        try { output_cleanup = emit_and_remove_outputs(); } catch (...) {}
        try { root_cleanup = cleanup_output_root(); } catch (...) {}
        return fail(std::string("unknown imported-fixture worker exception") +
            (process_cleanup && output_cleanup && root_cleanup ? "" : " (worker cleanup failed)"));
    }
}

} // namespace

int main(const int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--imported-fixtures-only") {
        imported_fixture_group = argv[2];
        return test_imported_profile_bands() == 0 ? 0 : 1;
    }
    DWORD_PTR original_affinity = 0;
    DWORD_PTR system_affinity = 0;
    bool partition_workers =
        GetProcessAffinityMask(GetCurrentProcess(), &original_affinity, &system_affinity) != FALSE;
    DWORD_PTR parent_affinity = 0;
    if (partition_workers) {
        DWORD_PTR available = original_affinity;
        unsigned int available_count = bit_count(available);
        if (available_count > 16) {
            DWORD_PTR bounded = 0;
            for (unsigned int index = 0; index < 16; ++index) {
                bounded |= take_lowest_bit(&available);
            }
            available = bounded;
            available_count = 16;
        }
        partition_workers = available_count >= 16;
        if (partition_workers) {
            parent_affinity = highest_bit(available);
            imported_worker_affinity = available & ~parent_affinity;
            partition_workers = SetProcessAffinityMask(GetCurrentProcess(), parent_affinity) != FALSE;
        }
    }
    use_process_fixture_partitions = partition_workers;
    if (partition_workers && run_imported_fixture_processes(true) != 2) {
        return fail("unexpected worker exception cleanup path did not complete explicitly");
    }
    auto imported_fixtures = std::async(
        std::launch::async, [] { return run_imported_fixture_processes(); });
    const std::array<std::pair<const char*, int (*)()>, 26> tests{{
        {"chord_inference", test_chord_inference},
        {"alignment", test_alignment},
        {"format_two_rejected", test_format_two_rejected},
        {"running_status_after_text_meta", test_running_status_after_text_meta},
        {"running_status_after_sysex", test_running_status_after_sysex},
        {"running_status_without_prior_channel_status_rejected", test_running_status_without_prior_channel_status_rejected},
        {"running_status_after_unsupported_system_status_rejected", test_running_status_after_unsupported_system_status_rejected},
        {"smpte_division_rejected", test_smpte_division_rejected},
        {"crossing_voice_and_pitch_witness", test_crossing_voice_and_pitch_witness},
        {"anchor_based_humanization_boundary", test_anchor_based_humanization_boundary},
        {"joint_cooldown_and_collision_policy", test_joint_cooldown_and_collision_policy},
        {"three_four_metric_accents", test_three_four_metric_accents},
        {"track_permutation_invariance", test_track_permutation_invariance},
        {"tempo_change_timing", test_tempo_change_timing},
        {"half_beat_phrase_profiles", test_half_beat_phrase_profiles},
        {"local_multi_skill_metrics", test_local_multi_skill_metrics},
        {"duration_normalized_dense_texture", test_duration_normalized_dense_texture},
        {"authored_vanilla_display_envelopes", test_authored_vanilla_display_envelopes},
        {"global_native_frame_conflict_policy", test_global_native_frame_conflict_policy},
        {"globally_unschedulable_conflict_drop", test_globally_unschedulable_conflict_drop},
        {"timing_domain_bounds", test_timing_domain_bounds},
        {"runtime_row_ceiling_omits_instead_of_truncating", test_runtime_row_ceiling_omits_instead_of_truncating},
        {"authored_vanilla_mode_change_counts", test_authored_vanilla_mode_change_counts},
        {"minimum_lead_in", test_minimum_lead_in},
        {"source_supported_gap_coverage", test_source_supported_gap_coverage},
        {"tail_coverage", test_tail_coverage},
    }};
    for (const auto& [name, test] : tests) {
        if (run_profiled(name, test) == 0) continue;
        if (partition_workers) SetProcessAffinityMask(GetCurrentProcess(), original_affinity);
        std::string cleanup_error;
        if (!cleanup_temp_directory(&TemporaryMidi::temporary_root(), &cleanup_error)) {
            std::cerr << "temporary MIDI cleanup failed: " << cleanup_error << '\n';
        }
        return 1;
    }
    const int imported_result = imported_fixtures.get();
    if (partition_workers) SetProcessAffinityMask(GetCurrentProcess(), original_affinity);
    if (imported_result != 0) return 1;
    std::string cleanup_error;
    if (!cleanup_temp_directory(&TemporaryMidi::temporary_root(), &cleanup_error)) {
        return fail("temporary MIDI cleanup failed: " + cleanup_error);
    }
    std::cout << "midi_generator_selftest ok\n";
    return 0;
}
