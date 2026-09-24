#include "pipeline/cache.h"
#include "pipeline/audio_loudness.h"
#include "pipeline/audio_reader.h"
#include "pipeline/chart_event_plan.h"
#include "pipeline/chart_compiler.h"
#include "pipeline/mabf_builder.h"
#include "pipeline/midi_chart_generator.h"
#include "pipeline/native_asset_capabilities.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/resolved_song_renderer.h"
#include "pipeline/runtime_cache_codec.h"
#include "pipeline/song_repository.h"
#include "pipeline/song_json.h"
#include "tests/test_support.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

class CommaDecimalPoint final : public std::numpunct<char> {
protected:
    char do_decimal_point() const override { return ','; }
};

struct MidiEvent {
    int tick = 0;
    int priority = 0;
    std::vector<unsigned char> bytes;
};

using MidiTrack = std::vector<MidiEvent>;

int fail(const std::string& message) {
    std::cerr << "song_repository_selftest: " << message << '\n';
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
    while ((value >>= 7) != 0) {
        encoded[3 - count] = static_cast<unsigned char>((value & 0x7f) | 0x80);
        ++count;
    }
    bytes->insert(bytes->end(), encoded.end() - count, encoded.end());
}

void add_note(MidiTrack* track, const int tick, const int duration, const int pitch, const int velocity) {
    track->push_back({tick, 2, {0x90, static_cast<unsigned char>(pitch), static_cast<unsigned char>(velocity)}});
    track->push_back({tick + duration, 1, {0x80, static_cast<unsigned char>(pitch), 0}});
}

std::vector<unsigned char> build_midi(MidiTrack events, const std::uint32_t tempo = 500000u) {
    events.push_back({0, 0, {0xff, 0x51, 0x03,
        static_cast<unsigned char>((tempo >> 16u) & 0xffu),
        static_cast<unsigned char>((tempo >> 8u) & 0xffu),
        static_cast<unsigned char>(tempo & 0xffu)}});
    std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.priority < b.priority;
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

std::vector<unsigned char> build_format_one_midi(MidiTrack first, MidiTrack second) {
    first.push_back({0, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}});
    const auto append_track = [](std::vector<unsigned char>* file, MidiTrack events) {
        std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.priority < b.priority;
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
        file->insert(file->end(), {'M', 'T', 'r', 'k'});
        append_be32(file, static_cast<std::uint32_t>(track.size()));
        file->insert(file->end(), track.begin(), track.end());
    };
    std::vector<unsigned char> file{'M', 'T', 'h', 'd', 0, 0, 0, 6};
    append_be16(&file, 1);
    append_be16(&file, 2);
    append_be16(&file, 480);
    append_track(&file, std::move(first));
    append_track(&file, std::move(second));
    return file;
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

bool write_silent_wav(const std::filesystem::path& path, const double duration_seconds) {
    constexpr std::uint32_t sample_rate = 48000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bits_per_sample = 16;
    const auto frames = static_cast<std::uint32_t>(duration_seconds * sample_rate);
    const std::uint32_t data_size = frames * channels * (bits_per_sample / 8);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("RIFF", 4);
    write_le32(out, 36 + data_size);
    out.write("WAVEfmt ", 8);
    write_le32(out, 16);
    write_le16(out, 1);
    write_le16(out, channels);
    write_le32(out, sample_rate);
    write_le32(out, sample_rate * channels * (bits_per_sample / 8));
    write_le16(out, channels * (bits_per_sample / 8));
    write_le16(out, bits_per_sample);
    out.write("data", 4);
    write_le32(out, data_size);
    std::array<char, 8192> zeros{};
    for (std::uint32_t remaining = data_size; remaining > 0;) {
        const std::uint32_t count = std::min<std::uint32_t>(
            remaining, static_cast<std::uint32_t>(zeros.size()));
        out.write(zeros.data(), count);
        remaining -= count;
    }
    return static_cast<bool>(out);
}

bool write_tone_wav(
    const std::filesystem::path& path,
    const double duration_seconds,
    const double amplitude = 4096.0) {
    constexpr std::uint32_t sample_rate = 48000;
    constexpr std::uint16_t channels = 2;
    const auto frames = static_cast<std::uint32_t>(duration_seconds * sample_rate);
    const std::uint32_t data_size = frames * channels * 2u;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("RIFF", 4);
    write_le32(out, 36 + data_size);
    out.write("WAVEfmt ", 8);
    write_le32(out, 16);
    write_le16(out, 1);
    write_le16(out, channels);
    write_le32(out, sample_rate);
    write_le32(out, sample_rate * channels * 2u);
    write_le16(out, channels * 2u);
    write_le16(out, 16);
    out.write("data", 4);
    write_le32(out, data_size);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto sample = static_cast<std::int16_t>(std::lround(
            amplitude * std::sin(2.0 * 3.141592653589793 * 440.0 * frame / sample_rate)));
        write_le16(out, static_cast<std::uint16_t>(sample));
        write_le16(out, static_cast<std::uint16_t>(sample));
    }
    return static_cast<bool>(out);
}

bool write_bytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool write_song_json(const std::filesystem::path& path, const std::string& title,
    const bool automatic_alignment = false, const bool metronome_enabled = false) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"" << title << "\",\n";
    if (!automatic_alignment) {
        out << "  \"midi_audio_alignment_seconds\": 0.0,\n"
            << "  \"midi_audio_offset_seconds\": 0.0,\n";
    }
    out
        << "  \"midi_minimum_lead_in_seconds\": 0.0,\n"
        << "  \"loudness_normalization\": false";
    if (metronome_enabled) {
        out << ",\n  \"metronome\": { \"enabled\": true, \"level\": 0.12 }";
    }
    out << "\n}\n";
    return static_cast<bool>(out);
}

bool write_synthetic_profile_song_json(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"Synthetic Reviewed Profiles\",\n"
        << "  \"midi_audio_alignment_seconds\": 0.0,\n"
        << "  \"midi_audio_offset_seconds\": 0.0,\n"
        << "  \"midi_minimum_lead_in_seconds\": 0.0,\n"
        << "  \"loudness_normalization\": false,\n"
        << "  \"metronome\": { \"enabled\": true, \"level\": 0.12 }\n"
        << "}\n";
    return static_cast<bool>(out);
}

bool write_synthetic_envelope_song_json(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"Synthetic Envelope Integration\",\n"
        << "  \"midi_audio_alignment_seconds\": 0.0,\n"
        << "  \"midi_audio_offset_seconds\": 0.0,\n"
        << "  \"midi_minimum_lead_in_seconds\": 0.0,\n"
        << "  \"loudness_normalization\": true,\n"
        << "  \"loudness_target_lufs\": -14.0,\n"
        << "  \"loudness_peak_ceiling_dbfs\": -1.0,\n"
        << "  \"metronome\": { \"enabled\": true, \"level\": 0.12 },\n"
        << "  \"gain_envelope\": [\n"
        << "    { \"time_seconds\": 0, \"gain_db\": 5 },\n"
        << "    { \"time_seconds\": 20, \"gain_db\": 5 },\n"
        << "    { \"time_seconds\": 35, \"gain_db\": 0 }\n"
        << "  ]\n"
        << "}\n";
    return static_cast<bool>(out);
}

bool write_explicit_song_json(
    const std::filesystem::path& path,
    const std::string& title,
    const bool metronome_enabled) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"" << title << "\",\n"
        << "  \"bpm\": 120,\n"
        << "  \"loudness_normalization\": false,\n"
        << "  \"metronome\": { \"enabled\": " << (metronome_enabled ? "true" : "false")
        << ", \"level\": 0.12 },\n"
        << "  \"notes\": [{ \"beat\": 0, \"duration_beats\": 1, \"pitch\": \"C4\" }]\n"
        << "}\n";
    return static_cast<bool>(out);
}

bool write_authored_profiles_song_json(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"Authored Profile Integration\",\n"
        << "  \"bpm\": 120,\n"
        << "  \"loudness_normalization\": false,\n"
        << "  \"profiles\": [\n";
    for (const auto [profile, difficulty, rows] :
            std::array<std::array<int, 3>, 2>{{{{0, 0, 4}}, {{1, std::numeric_limits<int>::max(), 20}}}}) {
        out << "    { \"difficulty\": " << difficulty << ", \"notes\": [\n";
        for (int row = 0; row < rows; ++row) {
            out << "      { \"beat\": " << row * 0.25
                << ", \"duration_beats\": 0.125, \"pitch\": \"C4\"";
            if (profile == 1 && row == 0) out << ", \"monotone_note_value\": \"dotted_quarter\"";
            out << " }"
                << (row + 1 == rows ? "\n" : ",\n");
        }
        out << "    ] }" << (profile == 0 ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    return static_cast<bool>(out);
}

bool write_extended_song_json(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"Extended Authored Chart 520\",\n"
        << "  \"bpm\": 120,\n  \"difficulty\": 0,\n"
        << "  \"loudness_normalization\": false,\n  \"notes\": [\n";
    for (std::size_t row = 0; row < 520u; ++row) {
        out << "    { \"beat\": " << row * 0.25
            << ", \"duration_beats\": 0.125";
        if (row == 513u) {
            out << ", \"pitch\": \"C4\", \"chord_id\": \"pca_C\"";
        } else if (row == 514u) {
            out << ", \"chord_id\": \"pca_C_7\", \"ignore_sound\": [\"As2\"]";
        } else {
            out << ", \"pitch\": \"C4\"";
            if (row >= 510u && row <= 512u) out << ", \"group_index\": 7";
        }
        out << " }"
            << (row + 1u == 520u ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    return static_cast<bool>(out);
}

bool write_extended_profiles_song_json(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"Extended Authored Profiles\",\n"
        << "  \"bpm\": 120,\n  \"loudness_normalization\": false,\n"
        << "  \"profiles\": [{ \"difficulty\": 2, \"notes\": [\n";
    for (std::size_t row = 0; row < 513u; ++row) {
        out << "    { \"beat\": " << row * 0.25
            << ", \"duration_beats\": 0.125, \"pitch\": \"D4\" }"
            << (row + 1u == 513u ? "\n" : ",\n");
    }
    out << "  ] }]\n}\n";
    return static_cast<bool>(out);
}

bool write_envelope_song_json(const std::filesystem::path& path, const double boost_db) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"Envelope Fixture\",\n"
        << "  \"bpm\": 120,\n"
        << "  \"loudness_normalization\": true,\n"
        << "  \"loudness_target_lufs\": -5,\n"
        << "  \"loudness_peak_ceiling_dbfs\": -6,\n"
        << "  \"gain_envelope\": [{ \"time_seconds\": 0, \"gain_db\": " << boost_db
        << " }, { \"time_seconds\": 1, \"gain_db\": 0 }],\n"
        << "  \"notes\": [{ \"beat\": 0, \"duration_beats\": 1, \"pitch\": \"C4\" }]\n"
        << "}\n";
    return static_cast<bool>(out);
}

bool write_normalized_song_json(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"Normalized Non-Limiting Fixture\",\n"
        << "  \"bpm\": 120,\n"
        << "  \"loudness_normalization\": true,\n"
        << "  \"loudness_target_lufs\": -18,\n"
        << "  \"loudness_peak_ceiling_dbfs\": -1,\n"
        << "  \"notes\": [{ \"beat\": 0, \"duration_beats\": 1, \"pitch\": \"C4\" }]\n"
        << "}\n";
    return static_cast<bool>(out);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

void write_u64_le(std::vector<std::uint8_t>* bytes, const std::size_t offset, const std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        (*bytes)[offset + index] = static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu);
    }
}

std::uint64_t read_u64_le(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8u; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8u);
    }
    return value;
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes, const std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t fnv1a64_range(const std::vector<std::uint8_t>& bytes, const std::size_t begin,
    const std::size_t end, std::uint64_t hash) {
    for (std::size_t index = begin; index < end; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

void write_double_le(std::vector<std::uint8_t>* bytes, const std::size_t offset, const double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    write_u64_le(bytes, offset, bits);
}

double read_double_le(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    const std::uint64_t bits = read_u64_le(bytes, offset);
    double value = 0.0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::size_t runtime_diagnostics_offset(const std::vector<std::uint8_t>& bytes) {
    const std::size_t policy_length_offset = 24u + 8u + 4u + 4u + 1u + 8u;
    return policy_length_offset + 4u + read_u32_le(bytes, policy_length_offset) + 48u;
}

void rechecksum_runtime_cache(std::vector<std::uint8_t>* bytes) {
    const std::size_t semantic_offset = bytes->size() - 16u;
    const std::uint64_t semantic = fnv1a64_range(*bytes, 24u, semantic_offset,
        14695981039346656037ull ^ 0x73656d616e746963ull);
    write_u64_le(bytes, semantic_offset, semantic);
    write_u64_le(bytes, bytes->size() - 8u, fnv1a64(*bytes, bytes->size() - 8u));
}

struct RuntimeSectionOffsets {
    std::size_t root_midi_offset = 0;
    std::size_t root_midi_alignment = 0;
    std::size_t first_chart_beat = 0;
    std::size_t first_chart_duration = 0;
    std::size_t second_chart_beat = 0;
    std::size_t first_chart_pitch = 0;
    std::size_t candidate_actions = 0;
    std::size_t candidate_frames = 0;
    std::size_t dropped_actions = 0;
    std::size_t target_rows = 0;
    std::size_t rejection_count = 0;
    std::size_t strain_value = 0;
    std::size_t overlap_ratio = 0;
    std::size_t exposure_decision = 0;
    std::size_t diagnostic_flags = 0;
    std::size_t descriptor_hash = 0;
    std::size_t profile_semantic_hash = 0;
    std::size_t tail_source_beat = 0;
    std::size_t tail_compiled_beat = 0;
    std::size_t omission_desired_rows = 0;
    std::size_t omission_candidate_actions = 0;
};

struct RuntimeCursor {
    const std::vector<std::uint8_t>& bytes;
    std::size_t position = 0;

    std::uint32_t u32() {
        const std::uint32_t value = read_u32_le(bytes, position);
        position += 4u;
        return value;
    }
    void skip_string() { position += 4u + read_u32_le(bytes, position); }
    void skip_int_vector() { position += 4u + 4u * read_u32_le(bytes, position); }
    void skip_string_vector() {
        const std::uint32_t count = u32();
        for (std::uint32_t index = 0; index < count; ++index) skip_string();
    }
    void skip_note() {
        position += 16u;
        skip_string();
        skip_string();
        position += 2u;
        skip_string_vector();
        skip_string_vector();
        position += 6u;
    }
    void skip_config(RuntimeSectionOffsets* offsets = nullptr) {
        skip_string();
        skip_string();
        position += 12u;
        skip_int_vector();
        skip_int_vector();
        if (offsets) {
            offsets->root_midi_offset = position;
            offsets->root_midi_alignment = position + 8u;
        }
        position += 7u * 8u;
        position += 4u + 16u * read_u32_le(bytes, position);
        position += 10u;
        const std::uint32_t notes = u32();
        for (std::uint32_t index = 0; index < notes; ++index) skip_note();
        const std::uint32_t voicings = u32();
        for (std::uint32_t index = 0; index < voicings; ++index) {
            skip_string();
            skip_string_vector();
        }
    }
    void skip_chart(RuntimeSectionOffsets* offsets) {
        const std::uint32_t notes = u32();
        for (std::uint32_t index = 0; index < notes; ++index) {
            if (index == 0u && offsets) offsets->first_chart_beat = position;
            if (index == 0u && offsets) offsets->first_chart_duration = position + 8u;
            if (index == 1u && offsets) offsets->second_chart_beat = position;
            position += 16u;
            if (index == 0u && offsets) offsets->first_chart_pitch = position + 4u;
            skip_string();
            skip_string();
            skip_string();
            skip_string();
            position += 24u;
            skip_string();
            skip_string();
            skip_string();
        }
    }
    void skip_diagnostics(RuntimeSectionOffsets* offsets) {
        if (offsets) {
            offsets->candidate_actions = position + 8u;
            offsets->candidate_frames = position + 16u;
            offsets->dropped_actions = position + 15u * 8u;
            offsets->target_rows = position + 4u * 8u;
            offsets->rejection_count = position + 18u * 8u;
            offsets->strain_value = position + 30u * 8u;
            offsets->overlap_ratio = position + 30u * 8u + 28u * 8u;
        }
        position += 30u * 8u + 39u * 8u + 8u;
        if (offsets) offsets->exposure_decision = position;
        skip_string();
        skip_string();
        skip_string();
        if (offsets) offsets->diagnostic_flags = position;
        position += 4u;
    }
    void skip_diagnostic_chart(RuntimeSectionOffsets* offsets) {
        position += 8u;
        const std::uint32_t tails = u32();
        if (offsets) offsets->descriptor_hash = position;
        position += 8u;
        for (std::uint32_t index = 0; index < tails; ++index) {
            position += 4u;
            if (index == 0u && offsets) offsets->tail_source_beat = position;
            skip_note();
            if (index == 0u && offsets) offsets->tail_compiled_beat = position;
            position += 16u;
            skip_string();
            skip_string();
            skip_string();
            skip_string();
            position += 24u;
            skip_string();
            skip_string();
            skip_string();
        }
    }
};

RuntimeSectionOffsets locate_runtime_sections(const std::vector<std::uint8_t>& bytes,
    const std::size_t config_offset) {
    RuntimeSectionOffsets offsets;
    RuntimeCursor cursor{bytes, config_offset};
    cursor.skip_config(&offsets);
    cursor.skip_chart(nullptr);
    const std::uint32_t profiles = cursor.u32();
    for (std::uint32_t index = 0; index < profiles; ++index) {
        cursor.skip_config();
        cursor.skip_chart(index == 0u ? &offsets : nullptr);
        cursor.skip_diagnostics(index == 0u ? &offsets : nullptr);
        cursor.skip_diagnostic_chart(index == 0u ? &offsets : nullptr);
        if (index == 0u) offsets.profile_semantic_hash = cursor.position;
        cursor.position += 8u;  // Profile semantic descriptor hash.
    }
    const std::uint32_t omissions = cursor.u32();
    if (omissions != 0u) {
        cursor.position += 4u;
        offsets.omission_desired_rows = cursor.position;
        cursor.position += 8u;
        cursor.skip_string();
        offsets.omission_candidate_actions = cursor.position + 8u;
    }
    return offsets;
}

std::string manifest_line(const std::string& manifest, const std::string& key) {
    const std::string prefix = key + "=";
    const std::size_t begin = manifest.find(prefix);
    if (begin == std::string::npos) return {};
    const std::size_t end = manifest.find('\n', begin);
    return manifest.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

bool charts_equal(const ff7rp::pipeline::CompiledChart& a, const ff7rp::pipeline::CompiledChart& b) {
    if (a.notes.size() != b.notes.size()) return false;
    for (std::size_t i = 0; i < a.notes.size(); ++i) {
        const auto& x = a.notes[i];
        const auto& y = b.notes[i];
        if (x.beat != y.beat || x.duration_beats != y.duration_beats || x.pitch != y.pitch ||
            x.time_str != y.time_str || x.monotone_id != y.monotone_id || x.chord_id != y.chord_id ||
            x.monotone_note_type != y.monotone_note_type || x.monotone_dot_type != y.monotone_dot_type ||
            x.chord_note_type != y.chord_note_type || x.chord_dot_type != y.chord_dot_type ||
            x.camera_switch_timing != y.camera_switch_timing || x.group_index != y.group_index ||
            x.ignore_sound_ids != y.ignore_sound_ids) {
            return false;
        }
    }
    return true;
}

bool configs_equal(const ff7rp::pipeline::SongConfig& a, const ff7rp::pipeline::SongConfig& b) {
    if (a.chord_voicings != b.chord_voicings) return false;
    if (a.schema != b.schema || a.title != b.title || a.bpm != b.bpm || a.difficulty != b.difficulty ||
        a.score_thresholds != b.score_thresholds || a.mode_change_combo_counts != b.mode_change_combo_counts ||
        a.midi_audio_offset_seconds != b.midi_audio_offset_seconds ||
        a.midi_audio_alignment_seconds != b.midi_audio_alignment_seconds ||
        a.midi_minimum_lead_in_seconds != b.midi_minimum_lead_in_seconds ||
        a.loudness_normalization != b.loudness_normalization ||
        a.loudness_target_lufs != b.loudness_target_lufs ||
        a.loudness_peak_ceiling_dbfs != b.loudness_peak_ceiling_dbfs ||
        a.metronome_enabled != b.metronome_enabled || a.metronome_level != b.metronome_level ||
        a.metronome_beat_zero_offset_seconds != b.metronome_beat_zero_offset_seconds ||
        a.metronome_beat_zero_offset_provided != b.metronome_beat_zero_offset_provided ||
        a.midi_audio_offset_provided != b.midi_audio_offset_provided ||
        a.midi_audio_alignment_provided != b.midi_audio_alignment_provided ||
        a.bpm_provided != b.bpm_provided || a.score_thresholds_provided != b.score_thresholds_provided ||
        a.mode_change_combo_counts_provided != b.mode_change_combo_counts_provided ||
        a.notes_provided != b.notes_provided ||
        a.diagnostic_extended_chart_fixture != b.diagnostic_extended_chart_fixture ||
        a.gain_envelope.size() != b.gain_envelope.size() || a.notes.size() != b.notes.size()) return false;
    for (std::size_t index = 0; index < a.gain_envelope.size(); ++index) {
        if (a.gain_envelope[index].time_seconds != b.gain_envelope[index].time_seconds ||
            a.gain_envelope[index].gain_db != b.gain_envelope[index].gain_db) return false;
    }
    for (std::size_t index = 0; index < a.notes.size(); ++index) {
        if (a.notes[index].beat != b.notes[index].beat ||
            a.notes[index].duration_beats != b.notes[index].duration_beats ||
            a.notes[index].pitch != b.notes[index].pitch || a.notes[index].chord_id != b.notes[index].chord_id ||
            a.notes[index].group_index != b.notes[index].group_index ||
            a.notes[index].alternate_monotone != b.notes[index].alternate_monotone ||
            a.notes[index].ignore_sound_pitches != b.notes[index].ignore_sound_pitches ||
            a.notes[index].source_chord_pitches != b.notes[index].source_chord_pitches ||
            a.notes[index].monotone_note_value != b.notes[index].monotone_note_value ||
            a.notes[index].chord_note_value != b.notes[index].chord_note_value) return false;
    }
    return true;
}

bool diagnostics_equal(
    const ff7rp::pipeline::DifficultyProfileDiagnostics& a,
    const ff7rp::pipeline::DifficultyProfileDiagnostics& b) {
    return a.selected_actions == b.selected_actions &&
        a.candidate_actions == b.candidate_actions && a.candidate_frames == b.candidate_frames &&
        a.protected_baseline_actions == b.protected_baseline_actions &&
        a.target_rows == b.target_rows && a.target_minimum_rows == b.target_minimum_rows &&
        a.target_maximum_rows == b.target_maximum_rows &&
        a.target_exclusions == b.target_exclusions &&
        a.local_skill_rejections == b.local_skill_rejections &&
        a.retained_actions == b.retained_actions && a.removed_actions == b.removed_actions &&
        a.replaced_actions == b.replaced_actions && a.added_actions == b.added_actions &&
        a.scheduled_rows == b.scheduled_rows &&
        a.scheduled_conflicts == b.scheduled_conflicts && a.dropped_actions == b.dropped_actions &&
        a.lead_in_rejections == b.lead_in_rejections &&
        a.audio_duration_rejections == b.audio_duration_rejections &&
        a.strain_rejections == b.strain_rejections &&
        a.joint_strain_p95 == b.joint_strain_p95 &&
        a.joint_strain_peak == b.joint_strain_peak &&
        a.maximum_window_actions == b.maximum_window_actions &&
        a.maximum_window_begin_seconds == b.maximum_window_begin_seconds &&
        a.maximum_quarter_second_stream_actions == b.maximum_quarter_second_stream_actions &&
        a.maximum_quarter_second_stream_duration == b.maximum_quarter_second_stream_duration &&
        a.maximum_half_second_stream_actions == b.maximum_half_second_stream_actions &&
        a.maximum_half_second_stream_duration == b.maximum_half_second_stream_duration &&
        a.maximum_quarter_second_stream_begin_seconds == b.maximum_quarter_second_stream_begin_seconds &&
        a.maximum_quarter_second_stream_actions_end_seconds == b.maximum_quarter_second_stream_actions_end_seconds &&
        a.maximum_quarter_second_stream_duration_begin_seconds == b.maximum_quarter_second_stream_duration_begin_seconds &&
        a.maximum_quarter_second_stream_duration_end_seconds == b.maximum_quarter_second_stream_duration_end_seconds &&
        a.maximum_half_second_stream_begin_seconds == b.maximum_half_second_stream_begin_seconds &&
        a.maximum_half_second_stream_actions_end_seconds == b.maximum_half_second_stream_actions_end_seconds &&
        a.maximum_half_second_stream_duration_begin_seconds == b.maximum_half_second_stream_duration_begin_seconds &&
        a.maximum_half_second_stream_duration_end_seconds == b.maximum_half_second_stream_duration_end_seconds &&
        a.maximum_jack_run == b.maximum_jack_run &&
        a.maximum_jack_begin_seconds == b.maximum_jack_begin_seconds &&
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
        a.right_fatigue_peak == b.right_fatigue_peak && a.left_fatigue_peak == b.left_fatigue_peak &&
        a.right_fatigue_peak_seconds == b.right_fatigue_peak_seconds &&
        a.left_fatigue_peak_seconds == b.left_fatigue_peak_seconds &&
        a.hand_imbalance == b.hand_imbalance &&
        a.rhythm_irregularity_p90 == b.rhythm_irregularity_p90 &&
        a.rhythm_irregularity_maximum == b.rhythm_irregularity_maximum &&
        a.rhythm_irregularity_peak_seconds == b.rhythm_irregularity_peak_seconds &&
        a.dominant_skill == b.dominant_skill && a.satisfied_route == b.satisfied_route &&
        a.satisfied_route_ratio == b.satisfied_route_ratio &&
        a.satisfied_route_margin == b.satisfied_route_margin &&
        a.dominant_skill_is_global == b.dominant_skill_is_global &&
        a.satisfied_route_name == b.satisfied_route_name &&
        a.hardest_window_begin_seconds == b.hardest_window_begin_seconds &&
        a.hardest_window_end_seconds == b.hardest_window_end_seconds &&
        a.overlap_ratio == b.overlap_ratio &&
        a.exposure_decision == b.exposure_decision && a.exposure_reason == b.exposure_reason &&
        a.complete == b.complete && a.row_limit_exceeded == b.row_limit_exceeded &&
        a.nested_from_previous == b.nested_from_previous;
}

bool omissions_equal(
    const std::vector<ff7rp::pipeline::DifficultyProfileOmission>& a,
    const std::vector<ff7rp::pipeline::DifficultyProfileOmission>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].difficulty != b[i].difficulty || a[i].desired_rows != b[i].desired_rows ||
            a[i].reason != b[i].reason || !diagnostics_equal(a[i].diagnostics, b[i].diagnostics) ||
            a[i].witness_notes.size() != b[i].witness_notes.size()) {
            return false;
        }
        for (std::size_t row = 0; row < a[i].witness_notes.size(); ++row) {
            const auto& expected = a[i].witness_notes[row];
            const auto& actual = b[i].witness_notes[row];
            if (expected.beat != actual.beat || expected.duration_beats != actual.duration_beats ||
                expected.pitch != actual.pitch || expected.chord_id != actual.chord_id ||
                expected.group_index != actual.group_index ||
                expected.alternate_monotone != actual.alternate_monotone ||
                expected.ignore_sound_pitches != actual.ignore_sound_pitches ||
                expected.source_chord_pitches != actual.source_chord_pitches) return false;
        }
    }
    return true;
}

double window_loudness(
    const ff7rp::pipeline::WavAudio& audio,
    const double begin_seconds,
    const double end_seconds) {
    const std::size_t begin = static_cast<std::size_t>(begin_seconds * audio.sample_rate);
    const std::size_t end = std::min(audio.frame_count(),
        static_cast<std::size_t>(end_seconds * audio.sample_rate));
    ff7rp::pipeline::WavAudio window;
    window.sample_rate = audio.sample_rate;
    window.channels = audio.channels;
    window.source_frame_count = end - begin;
    window.stereo_samples.assign(
        audio.stereo_samples.begin() + begin * 2,
        audio.stereo_samples.begin() + end * 2);
    return ff7rp::pipeline::measure_integrated_loudness(window);
}

using TemporaryRoot = ff7rp::tests::TemporaryDirectory;

int test_growth_cache_and_manifest(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "GrowthFixture";
    std::filesystem::create_directories(song_directory);
    MidiTrack track;
    for (int index = 0; index < 8; ++index) add_note(&track, index * 960, 120, 72 + index % 5, 110);
    const std::vector<std::uint8_t> midi = build_midi(std::move(track));
    if (!write_bytes(song_directory / "song.mid", midi) ||
        !write_silent_wav(song_directory / "song.wav", 8.5) ||
        !write_song_json(song_directory / "song.json", "Growth Fixture", true)) {
        return fail("failed to write growth fixture");
    }

    ff7rp::pipeline::LoadedSong generated;
    auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &generated);
    if (!status.ok()) return fail("growth fixture generation failed: " + status.message);
    if (generated.loaded_from_runtime_cache || generated.difficulty_profiles.size() != 6 ||
        !generated.difficulty_profile_omissions.empty()) {
        return fail("independently feasible sparse profiles were rejected solely for no growth");
    }
    for (int difficulty = 2; difficulty <= 6; ++difficulty) {
        const auto& profile = generated.difficulty_profiles[difficulty - 1];
        if (profile.config.difficulty != difficulty ||
            !ff7rp::pipeline::validate_midi_difficulty_route(profile.config.notes,
                profile.config.bpm, difficulty).feasible) {
            return fail("repository did not independently validate each sparse difficulty");
        }
    }
    const auto& generated_profile = generated.difficulty_profiles.front();
    if (!generated_profile.diagnostics.complete || generated_profile.diagnostics.row_limit_exceeded ||
        generated_profile.diagnostics.scheduled_rows != generated_profile.chart.notes.size()) {
        return fail("generated profile completeness diagnostics are inconsistent");
    }

    const std::filesystem::path manifest_path = ff7rp::pipeline::cache_manifest_path(song_directory.string());
    const std::filesystem::path runtime_path = song_directory / ".cache" / "runtime.bin";
    const std::string full_manifest = read_text(manifest_path);
    if (!std::filesystem::is_regular_file(runtime_path) ||
        full_manifest.find("profile_complete=1") == std::string::npos ||
        full_manifest.find("profile_dropped_actions=") == std::string::npos ||
        full_manifest.find("midi_alignment_source=automatic") == std::string::npos ||
        full_manifest.find("midi_offset_source=derived") == std::string::npos ||
        full_manifest.find("config_chart_semantic_hash=") == std::string::npos ||
        full_manifest.find("profile_semantic_hashes=") == std::string::npos ||
        full_manifest.find("profile_omission_semantic_hashes=") == std::string::npos ||
        full_manifest.find("mabf_logical_source_frames=") == std::string::npos ||
        full_manifest.find("mabf_sample_rate=48000") == std::string::npos ||
        full_manifest.find("mabf_channels=2") == std::string::npos ||
        full_manifest.find("resident_pcm_frames=" +
            std::to_string(generated.audio.source_frame_count)) == std::string::npos) {
        return fail("repository did not write the canonical cross-artifact semantic manifest");
    }
    const std::string version = manifest_line(full_manifest, "version");
    const std::string cache_key = manifest_line(full_manifest, "cache_key");
    const std::string row_policy = manifest_line(full_manifest, "chart_row_policy");
    const std::string accepted_limit = manifest_line(full_manifest, "accepted_chart_input_limit");
    {
        std::ofstream manifest(manifest_path, std::ios::binary | std::ios::trunc);
        manifest << version << '\n' << cache_key << '\n' << row_policy << '\n' << accepted_limit << '\n';
    }

    ff7rp::pipeline::LoadedSong reloaded;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &reloaded);
    if (!status.ok()) return fail("runtime cache reload failed: " + status.message);
    if (reloaded.loaded_from_runtime_cache ||
        reloaded.difficulty_profiles.size() != generated.difficulty_profiles.size() ||
        !omissions_equal(reloaded.difficulty_profile_omissions, generated.difficulty_profile_omissions) ||
        read_text(manifest_path) != full_manifest) {
        return fail("manifest substitution was not rejected and rebuilt identically");
    }
    for (std::size_t i = 0; i < generated.difficulty_profiles.size(); ++i) {
        if (!configs_equal(reloaded.difficulty_profiles[i].config, generated.difficulty_profiles[i].config) ||
            !charts_equal(reloaded.difficulty_profiles[i].chart, generated.difficulty_profiles[i].chart) ||
            !diagnostics_equal(reloaded.difficulty_profiles[i].diagnostics,
                generated.difficulty_profiles[i].diagnostics) ||
            !ff7rp::pipeline::diagnostic_charts_equal(reloaded.difficulty_profiles[i].diagnostic_chart,
                generated.difficulty_profiles[i].diagnostic_chart)) {
            return fail("runtime cache changed profile chart/action/completeness diagnostics");
        }
    }
    ff7rp::pipeline::LoadedSong exact_cached;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &exact_cached);
    if (!status.ok() || !exact_cached.loaded_from_runtime_cache ||
        !configs_equal(exact_cached.config, reloaded.config) || !charts_equal(exact_cached.chart, reloaded.chart) ||
        exact_cached.gain_envelope_applied != reloaded.gain_envelope_applied ||
        exact_cached.gain_envelope_point_count != reloaded.gain_envelope_point_count ||
        exact_cached.gain_envelope_max_gain_db != reloaded.gain_envelope_max_gain_db ||
        exact_cached.gain_envelope_min_gain_db != reloaded.gain_envelope_min_gain_db ||
        exact_cached.metronome_beat_count != reloaded.metronome_beat_count ||
        exact_cached.metronome_downbeat_count != reloaded.metronome_downbeat_count ||
        exact_cached.metronome_first_beat_seconds != reloaded.metronome_first_beat_seconds ||
        exact_cached.metronome_last_beat_seconds != reloaded.metronome_last_beat_seconds) {
        return fail("runtime cache did not reconstruct exact config, envelope, and metronome metadata");
    }

    const std::vector<std::uint8_t> valid_runtime = read_binary(runtime_path);
    const std::vector<std::uint8_t> valid_mabf = read_binary(generated.cache_sidecar_path);
    const auto expect_rebuild = [&] {
        const auto runtime_before = read_binary(runtime_path);
        const auto mabf_before = read_binary(generated.cache_sidecar_path);
        const std::string manifest_before = read_text(manifest_path);
        bool entered_cold_pipeline = false;
        ff7rp::pipeline::LoadedSong rejected;
        const auto rejected_status = ff7rp::pipeline::load_song_directory(
            song_directory.string(), &rejected,
            [&](const char* stage) {
                if (std::string(stage) == "audio_decode_started") entered_cold_pipeline = true;
            },
            false);
        const bool rejected_without_writes = rejected_status.code == ff7rp::pipeline::StatusCode::CacheMiss &&
            !rejected.loaded_from_runtime_cache && !entered_cold_pipeline &&
            read_binary(runtime_path) == runtime_before &&
            read_binary(generated.cache_sidecar_path) == mabf_before &&
            read_text(manifest_path) == manifest_before;
        const std::vector<std::uint8_t> manifest_bytes(full_manifest.begin(), full_manifest.end());
        const bool restored = write_bytes(runtime_path, valid_runtime) &&
            write_bytes(generated.cache_sidecar_path, valid_mabf) &&
            write_bytes(manifest_path, manifest_bytes);
        return rejected_without_writes && restored;
    };
    for (const std::size_t offset : std::array<std::size_t, 3>{0u, 24u, valid_runtime.size() - 1u}) {
        auto corrupted = valid_runtime;
        corrupted[offset] ^= 0x40u;
        if (!write_bytes(runtime_path, corrupted) || !expect_rebuild()) {
            return fail("runtime header/body/checksum corruption was not rejected and rebuilt");
        }
    }
    auto trailing_runtime = valid_runtime;
    trailing_runtime.push_back(0u);
    if (!write_bytes(runtime_path, trailing_runtime) || !expect_rebuild()) {
        return fail("runtime trailing bytes were not rejected and rebuilt");
    }
    auto metadata_runtime = valid_runtime;
    const std::size_t policy_length_offset = 24u + 8u + 4u + 4u + 1u + 8u;
    const std::size_t metadata_offset = policy_length_offset + 4u + read_u32_le(metadata_runtime, policy_length_offset);
    metadata_runtime[metadata_offset + 16u] ^= 1u;
    rechecksum_runtime_cache(&metadata_runtime);
    if (!write_bytes(runtime_path, metadata_runtime) || !expect_rebuild()) {
        return fail("checksummed inconsistent MABF metadata was not rejected and rebuilt");
    }
    const std::size_t diagnostics_offset = metadata_offset + 48u;
    struct RuntimeMutation {
        const char* name;
        std::size_t offset;
        double value;
        bool byte_value;
    };
    const std::array<RuntimeMutation, 14> semantic_mutations{{
        {"chart source boolean", diagnostics_offset + 14u, 2.0, true},
        {"alignment confidence", diagnostics_offset + 15u, 2.0, false},
        {"normalized flag", diagnostics_offset + 23u, 0.0, true},
        {"gain-applied flag", diagnostics_offset + 24u, 0.0, true},
        {"limiter flag", diagnostics_offset + 25u, 0.0, true},
        {"input loudness", diagnostics_offset + 26u, 100.0, false},
        {"output peak", diagnostics_offset + 50u, 100.0, false},
        {"applied gain", diagnostics_offset + 58u, 100.0, false},
        {"gain envelope flag", diagnostics_offset + 66u, 0.0, true},
        {"gain envelope maximum", diagnostics_offset + 71u, 1.0, false},
        {"gain envelope minimum", diagnostics_offset + 79u, -1.0, false},
        {"metronome beats", diagnostics_offset + 87u, 0.0, true},
        {"metronome downbeats", diagnostics_offset + 95u, 0.0, true},
        {"metronome first time", diagnostics_offset + 103u, 0.0, false},
    }};
    for (const RuntimeMutation& mutation : semantic_mutations) {
        auto corrupted = valid_runtime;
        if (mutation.byte_value) {
            corrupted[mutation.offset] = mutation.name[0] == 'c' ? 2u :
                static_cast<std::uint8_t>(corrupted[mutation.offset] == 0u);
        } else {
            write_double_le(&corrupted, mutation.offset, mutation.value);
        }
        rechecksum_runtime_cache(&corrupted);
        if (!write_bytes(runtime_path, corrupted) || !expect_rebuild()) {
            return fail(std::string("rechecksummed runtime ") + mutation.name +
                " corruption was not rejected and rebuilt");
        }
    }
    auto plausible_confidence_runtime = valid_runtime;
    const double original_confidence = read_double_le(valid_runtime, diagnostics_offset + 15u);
    write_double_le(&plausible_confidence_runtime, diagnostics_offset + 15u,
        original_confidence == 0.7 ? 0.6 : 0.7);
    rechecksum_runtime_cache(&plausible_confidence_runtime);
    if (!write_bytes(runtime_path, plausible_confidence_runtime) || !expect_rebuild()) {
        return fail("plausible automatic alignment confidence mutation was authorized without manifest equality");
    }
    auto coordinated_runtime = valid_runtime;
    std::string coordinated_manifest = full_manifest;
    const std::string mabf_bytes_line = "mabf_bytes=" + std::to_string(generated.mabf_metadata.byte_count);
    const std::size_t mabf_bytes_position = coordinated_manifest.find(mabf_bytes_line);
    if (mabf_bytes_position == std::string::npos) {
        return fail("canonical manifest omitted MABF byte count");
    }
    coordinated_manifest.replace(mabf_bytes_position, mabf_bytes_line.size(),
        "mabf_bytes=" + std::to_string(generated.mabf_metadata.byte_count + 1u));
    const std::vector<std::uint8_t> coordinated_manifest_bytes(
        coordinated_manifest.begin(), coordinated_manifest.end());
    write_u64_le(&coordinated_runtime, metadata_offset + 8u, generated.mabf_metadata.byte_count + 1u);
    write_u64_le(&coordinated_runtime, metadata_offset + 40u,
        fnv1a64(coordinated_manifest_bytes, coordinated_manifest_bytes.size()));
    rechecksum_runtime_cache(&coordinated_runtime);
    if (!write_bytes(runtime_path, coordinated_runtime) ||
        !write_bytes(manifest_path, coordinated_manifest_bytes) || !expect_rebuild()) {
        return fail("coordinated runtime and manifest artifact mutation bypassed actual MABF validation");
    }
    const RuntimeSectionOffsets sections = locate_runtime_sections(valid_runtime, diagnostics_offset + 119u);
    if (sections.candidate_actions < 24u || sections.candidate_actions + 8u > valid_runtime.size() ||
        read_u64_le(valid_runtime, sections.candidate_actions) != generated_profile.diagnostics.candidate_actions ||
        sections.root_midi_offset < 24u || sections.root_midi_alignment < 24u ||
        sections.first_chart_beat < 24u || sections.first_chart_duration < 24u ||
        sections.second_chart_beat < 24u || sections.first_chart_pitch < 24u ||
        sections.first_chart_pitch >= valid_runtime.size() || sections.overlap_ratio < 24u ||
        sections.target_rows < 24u || sections.rejection_count < 24u || sections.strain_value < 24u ||
        sections.exposure_decision < 24u || sections.diagnostic_flags < 24u ||
        sections.profile_semantic_hash < 24u) {
        return fail("independent runtime section locator did not match serialized profile diagnostics");
    }
    const auto expect_semantic_mutation_rebuild = [&](const char* name, auto mutate) {
        auto corrupted = valid_runtime;
        mutate(&corrupted);
        rechecksum_runtime_cache(&corrupted);
        if (!write_bytes(runtime_path, corrupted) || !expect_rebuild()) {
            return fail(std::string("rechecksummed runtime ") + name +
                " corruption was not rejected and rebuilt");
        }
        return 0;
    };
    if (expect_semantic_mutation_rebuild("profile candidate count", [&](auto* bytes) {
            (*bytes)[sections.candidate_actions] ^= 1u;
        }) != 0 ||
        expect_semantic_mutation_rebuild("alignment offset", [&](auto* bytes) {
            write_double_le(bytes, sections.root_midi_offset, 0.75);
        }) != 0 ||
        expect_semantic_mutation_rebuild("alignment estimate", [&](auto* bytes) {
            write_double_le(bytes, sections.root_midi_alignment, -0.75);
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile target count", [&](auto* bytes) {
            (*bytes)[sections.target_rows] ^= 1u;
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile rejection count", [&](auto* bytes) {
            (*bytes)[sections.rejection_count] ^= 1u;
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile strain", [&](auto* bytes) {
            write_double_le(bytes, sections.strain_value, 123.0);
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile overlap", [&](auto* bytes) {
            write_double_le(bytes, sections.overlap_ratio, 2.0);
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile exposure", [&](auto* bytes) {
            (*bytes)[sections.exposure_decision + 4u] ^= 1u;
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile completeness", [&](auto* bytes) {
            (*bytes)[sections.diagnostic_flags] ^= 1u;
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile row limit", [&](auto* bytes) {
            (*bytes)[sections.diagnostic_flags + 1u] ^= 1u;
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile nesting", [&](auto* bytes) {
            (*bytes)[sections.diagnostic_flags + 2u] ^= 1u;
        }) != 0 ||
        expect_semantic_mutation_rebuild("profile descriptor hash", [&](auto* bytes) {
            (*bytes)[sections.profile_semantic_hash] ^= 1u;
        }) != 0 ||
        expect_semantic_mutation_rebuild("compiled chart timing", [&](auto* bytes) {
            write_double_le(bytes, sections.first_chart_beat, 999.0);
        }) != 0 ||
        expect_semantic_mutation_rebuild("compiled chart duration", [&](auto* bytes) {
            write_double_le(bytes, sections.first_chart_duration, 0.0);
        }) != 0 ||
        expect_semantic_mutation_rebuild("compiled chart order", [&](auto* bytes) {
            write_double_le(bytes, sections.second_chart_beat, -1.0);
        }) != 0 ||
        expect_semantic_mutation_rebuild("compiled chart action identity", [&](auto* bytes) {
            (*bytes)[sections.first_chart_pitch] ^= 1u;
        }) != 0) {
        return 1;
    }
    // This sparse song now legitimately publishes all six labels. Keep the
    // omission-tamper checks on an explicit codec fixture, not a requirement
    // that the generator manufacture an omission to exercise serialization.
    auto omission_fixture = generated;
    ff7rp::pipeline::DifficultyProfileOmission omission;
    omission.difficulty = omission_fixture.difficulty_profiles.back().config.difficulty;
    omission.desired_rows = 77;
    omission.reason = "explicit omission serialization fixture";
    omission.diagnostics = omission_fixture.difficulty_profiles.back().diagnostics;
    omission_fixture.difficulty_profiles.pop_back();
    omission_fixture.difficulty_profile_omissions.push_back(omission);
    constexpr std::array<char, 8> magic{'F', '7', 'R', 'P', 'R', 'T', '1', '6'};
    std::vector<std::uint8_t> omission_bytes;
    auto decoded_omission = omission_fixture;
    ff7rp::pipeline::SongConfig authored_source_config;
    if (!ff7rp::pipeline::load_song_json_file((song_directory / "song.json").string(),
            &authored_source_config).ok()) return fail("could not reread omission fixture source context");
    decoded_omission.config = authored_source_config;
    if (!ff7rp::pipeline::encode_runtime_cache(omission_fixture, magic, 16, &omission_bytes) ||
        !ff7rp::pipeline::decode_runtime_cache(omission_bytes, magic, 16, &decoded_omission)) {
        return fail("explicit omission codec fixture did not round-trip");
    }
    const auto omission_sections = locate_runtime_sections(omission_bytes, diagnostics_offset + 119u);
    for (const auto offset : {omission_sections.omission_desired_rows,
                             omission_sections.omission_candidate_actions}) {
        if (offset < 24u || offset + 8u > omission_bytes.size()) {
            return fail("explicit omission section locator failed");
        }
        auto corrupted = omission_bytes;
        corrupted[offset] ^= 1u;
        rechecksum_runtime_cache(&corrupted);
        decoded_omission.config = authored_source_config;
        if (ff7rp::pipeline::decode_runtime_cache(corrupted, magic, 16, &decoded_omission)) {
            return fail("rechecksummed omission semantic corruption was accepted");
        }
    }
    const std::size_t slot_size = (valid_mabf.size() - ff7rp::pipeline::kMabfHeaderSize) / 3u;
    for (std::size_t mode = 0; mode < 3u; ++mode) {
        auto corrupted = valid_mabf;
        corrupted[ff7rp::pipeline::kMabfHeaderSize + mode * slot_size + 96u] ^= 1u;
        if (!write_bytes(generated.cache_sidecar_path, corrupted) || !expect_rebuild()) {
            return fail("MABF mode bytes were not rejected and rebuilt");
        }
    }
    const std::filesystem::path substitute_directory = root / "GrowthMabfSubstitute";
    std::filesystem::create_directories(substitute_directory);
    if (!write_bytes(substitute_directory / "song.mid", midi) ||
        !write_tone_wav(substitute_directory / "song.wav", 8.5) ||
        !write_song_json(substitute_directory / "song.json", "Growth MABF Substitute")) {
        return fail("failed to create structurally valid MABF substitution fixture");
    }
    ff7rp::pipeline::LoadedSong substitute;
    status = ff7rp::pipeline::load_song_directory(substitute_directory.string(), &substitute);
    const auto substitute_mabf = read_binary(substitute.cache_sidecar_path);
    if (!status.ok() || substitute_mabf == valid_mabf ||
        !write_bytes(generated.cache_sidecar_path, substitute_mabf) ||
        !expect_rebuild()) {
        return fail("structurally valid substituted MABF was authorized by stale runtime metadata");
    }
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    ff7rp::pipeline::LoadedSong policy_equivalent;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &policy_equivalent);
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    if (!status.ok() || policy_equivalent.loaded_from_runtime_cache ||
        policy_equivalent.chart_policy_identity != ff7rp::pipeline::kPlayableExtendedChartRowPolicyIdentity ||
        policy_equivalent.accepted_chart_input_limit != ff7rp::pipeline::kMaximumExtendedChartRows ||
        policy_equivalent.cache_key == generated.cache_key) {
        return fail("stale native512 cache was promoted under playable extended policy");
    }
    return 0;
}

int test_physical_midi_cache_round_trip(const std::filesystem::path& root) {
    struct PolicyReset {
        ~PolicyReset() { ff7rp::pipeline::configure_chart_row_limit(false, false); }
    } reset;
    ff7rp::pipeline::configure_chart_row_limit(true, true);

    const std::filesystem::path song_directory = root / "PhysicalMidiCacheFixture";
    std::filesystem::create_directories(song_directory);
    MidiTrack track;
    constexpr int onset_count = 630;
    for (int index = 0; index < onset_count; ++index) {
        add_note(&track, index * 348, 80, 72 + index % 5, 110);
        add_note(&track, index * 348, 80, 60 + index % 5, 60);
        add_note(&track, index * 348, 80, 55 + index % 5, 50);
    }
    if (!write_bytes(song_directory / "song.mid", build_midi(std::move(track), 460000u))
        || !write_silent_wav(song_directory / "song.wav", 220.0)
        || !write_song_json(song_directory / "song.json", "Physical MIDI Cache Fixture", true)) {
        return fail("failed to write generalized physical MIDI cache fixture");
    }

    ff7rp::pipeline::LoadedSong cold;
    auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &cold);
    if (!status.ok() || cold.loaded_from_runtime_cache || cold.difficulty_profiles.empty()
        || cold.chart_policy_identity != ff7rp::pipeline::kPlayableExtendedChartRowPolicyIdentity) {
        return fail("generalized physical MIDI cold generation failed: " + status.message);
    }
    const std::string manifest = read_text(cold.cache_manifest_path);
    if (manifest.find("profile_source_rows=") == std::string::npos
        || manifest.find("profile_native_events=") == std::string::npos
        || manifest.find("profile_required_actions=") == std::string::npos
        || manifest.find("profile_physical_digests=") == std::string::npos) {
        return fail("generalized physical MIDI manifest omitted derived event counts");
    }
    const auto extended = std::find_if(cold.difficulty_profiles.begin(), cold.difficulty_profiles.end(),
        [](const auto& profile) { return profile.diagnostic_chart.source_row_count > 512u; });
    ff7rp::pipeline::ChartEventPlan extended_plan;
    if (extended == cold.difficulty_profiles.end()
        || extended->config.notes.size() != ff7rp::pipeline::kMaxChartRows
        || extended->chart.notes.size() != ff7rp::pipeline::kMaxChartRows
        || extended->diagnostic_chart.native_prefix_row_count != ff7rp::pipeline::kMaxChartRows
        || extended->diagnostic_chart.tail_rows.empty()
        || !ff7rp::pipeline::derive_profile_event_plan(*extended, &extended_plan)
        || extended_plan.source_row_count <= ff7rp::pipeline::kMaxChartRows
        || extended_plan.native_prefix_event_count != ff7rp::pipeline::kMaxChartRows
        || extended_plan.source_row_count != extended_plan.native_event_count
        || extended_plan.native_event_count != 3 * extended_plan.required_action_count
        || extended_plan.required_action_count != extended->diagnostics.selected_actions
        || extended_plan.physical_digest == 0
        || std::any_of(extended->config.notes.begin(), extended->config.notes.end(), [](const auto& note) {
            return note.group_index == 0;
        }) || std::any_of(extended->diagnostic_chart.tail_rows.begin(),
            extended->diagnostic_chart.tail_rows.end(), [](const auto& row) {
                return row.source.group_index == 0 || row.compiled.group_index != row.source.group_index;
            })) {
        return fail("generated extended MIDI groups were clipped or had inconsistent R/P/E/A");
    }
    const std::string cold_resolved = read_text(
        ff7rp::pipeline::resolved_song_json_path(song_directory.string()));
    ff7rp::pipeline::ParsedSongSource parsed_resolved;
    if (!ff7rp::pipeline::parse_song_json_string(cold_resolved, &parsed_resolved).ok() ||
        parsed_resolved.authored_profiles.size() != cold.difficulty_profiles.size()) {
        return fail("generated MIDI resolved output was absent or not source-compatible");
    }
    const auto resolved_extended = std::find_if(parsed_resolved.authored_profiles.begin(),
        parsed_resolved.authored_profiles.end(), [&](const auto& profile) {
            return profile.difficulty == extended->config.difficulty;
        });
    if (resolved_extended == parsed_resolved.authored_profiles.end() ||
        resolved_extended->notes.size() != extended_plan.source_row_count ||
        std::any_of(resolved_extended->notes.begin(), resolved_extended->notes.end(),
            [](const auto& note) {
                return note.group_index == 0 ||
                    (!note.pitch.empty() && !note.monotone_note_value.provided) ||
                    (!note.chord_id.empty() && !note.chord_note_value.provided);
            })) {
        return fail("generated extended MIDI resolved output clipped its tail or lost groups");
    }

    ff7rp::pipeline::LoadedSong warm;
    std::string warm_trace;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &warm,
        [&](const char* stage) {
            if (!warm_trace.empty()) warm_trace += ',';
            warm_trace += stage;
        });
    if (!status.ok() || !warm.loaded_from_runtime_cache
        || warm.difficulty_profiles.size() != cold.difficulty_profiles.size()
        || read_text(warm.cache_manifest_path) != manifest
        || read_text(ff7rp::pipeline::resolved_song_json_path(song_directory.string())) != cold_resolved) {
        return fail("generalized physical MIDI runtime cache did not round-trip: " + warm_trace);
    }
    for (std::size_t index = 0; index < cold.difficulty_profiles.size(); ++index) {
        if (!configs_equal(warm.difficulty_profiles[index].config, cold.difficulty_profiles[index].config)
            || !charts_equal(warm.difficulty_profiles[index].chart, cold.difficulty_profiles[index].chart)
            || !diagnostics_equal(warm.difficulty_profiles[index].diagnostics,
                cold.difficulty_profiles[index].diagnostics)
            || !ff7rp::pipeline::diagnostic_charts_equal(
                warm.difficulty_profiles[index].diagnostic_chart,
                cold.difficulty_profiles[index].diagnostic_chart)) {
            return fail("generalized physical MIDI cold/warm profile semantics changed");
        }
    }
    const auto warm_extended = std::find_if(warm.difficulty_profiles.begin(), warm.difficulty_profiles.end(),
        [&](const auto& profile) { return profile.config.difficulty == extended->config.difficulty; });
    ff7rp::pipeline::ChartEventPlan warm_extended_plan;
    if (warm_extended == warm.difficulty_profiles.end()
        || !ff7rp::pipeline::derive_profile_event_plan(*warm_extended, &warm_extended_plan)
        || warm_extended_plan.source_row_count != extended_plan.source_row_count
        || warm_extended_plan.native_prefix_event_count != extended_plan.native_prefix_event_count
        || warm_extended_plan.native_event_count != extended_plan.native_event_count
        || warm_extended_plan.required_action_count != extended_plan.required_action_count
        || warm_extended_plan.physical_digest != extended_plan.physical_digest) {
        return fail("generated extended MIDI event plan changed across cold/warm cache round-trip");
    }

    ff7rp::pipeline::configure_chart_row_limit(false, false);
    ff7rp::pipeline::LoadedSong unsupported;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &unsupported);
    const bool all_oversized = status.code == ff7rp::pipeline::StatusCode::InvalidChart
        && unsupported.difficulty_profiles.empty() && unsupported.difficulty_profile_omissions.size() == 6
        && std::all_of(unsupported.difficulty_profile_omissions.begin(), unsupported.difficulty_profile_omissions.end(),
            [](const auto& omission) { return omission.desired_rows > ff7rp::pipeline::kMaxChartRows; });
    if ((!status.ok() && !all_oversized) || unsupported.loaded_from_runtime_cache
        || std::any_of(unsupported.difficulty_profiles.begin(), unsupported.difficulty_profiles.end(),
            [](const auto& profile) {
                return profile.config.notes.size() > ff7rp::pipeline::kMaxChartRows
                    || profile.chart.notes.size() > ff7rp::pipeline::kMaxChartRows
                    || profile.diagnostic_chart.present();
            })) {
        return fail("unsupported generated MIDI policy clipped or published an extended profile");
    }
    for (const auto& profile : unsupported.difficulty_profiles) {
        if (!profile.diagnostics.complete || !ff7rp::pipeline::validate_midi_difficulty_route(
                profile.config.notes, profile.config.bpm, profile.config.difficulty).feasible) {
            return fail("ordinary policy published an incomplete or load-unsafe reduced chart");
        }
    }
    return 0;
}

int test_dense_collision_cache_round_trip(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "DenseCollisionFixture";
    std::filesystem::create_directories(song_directory);
    MidiTrack track;
    for (int onset = 0; onset < 80; ++onset) {
        const int tick = 480 + onset * 60;
        const int melody_pitch = 72 + onset % 8;
        track.push_back({tick, 2, {0x90, static_cast<unsigned char>(melody_pitch), 110}});
        track.push_back({tick + 30, 1, {0x80, static_cast<unsigned char>(melody_pitch), 0}});
        for (const int chord_pitch : {48, 52, 55}) {
            track.push_back({tick, 2, {0x91, static_cast<unsigned char>(chord_pitch), 80}});
            track.push_back({tick + 30, 1, {0x81, static_cast<unsigned char>(chord_pitch), 0}});
        }
    }
    if (!write_bytes(song_directory / "song.mid", build_midi(std::move(track))) ||
        !write_silent_wav(song_directory / "song.wav", 12.0) ||
        !write_song_json(song_directory / "song.json", "Dense Collision Fixture", true, true)) {
        return fail("failed to write dense collision fixture");
    }

    ff7rp::pipeline::LoadedSong generated;
    auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &generated);
    if (!status.ok() || generated.loaded_from_runtime_cache || generated.difficulty_profiles.empty()) {
        return fail("dense collision fixture did not cold-build");
    }
    const auto& diagnostics = generated.difficulty_profiles.front().diagnostics;
    if (diagnostics.dropped_actions <= diagnostics.selected_actions || !generated.config.metronome_enabled) {
        return fail("dense collision fixture did not reproduce independent dropped-candidate counts: frames=" +
            std::to_string(diagnostics.candidate_frames) + " candidates=" +
            std::to_string(diagnostics.candidate_actions) + " selected=" +
            std::to_string(diagnostics.selected_actions) + " dropped=" +
            std::to_string(diagnostics.dropped_actions));
    }

    const std::filesystem::path runtime_path = song_directory / ".cache" / "runtime.bin";
    const auto valid_runtime = read_binary(runtime_path);
    const auto valid_mabf = read_binary(generated.cache_sidecar_path);
    const std::string valid_manifest = read_text(generated.cache_manifest_path);
    bool entered_cold_pipeline = false;
    ff7rp::pipeline::LoadedSong cached;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &cached,
        [&](const char* stage) {
            if (std::string(stage) == "audio_decode_started" ||
                std::string(stage) == "midi_generation_started") {
                entered_cold_pipeline = true;
            }
        });
    if (!status.ok() || !cached.loaded_from_runtime_cache || entered_cold_pipeline ||
        read_binary(runtime_path) != valid_runtime ||
        read_binary(generated.cache_sidecar_path) != valid_mabf ||
        read_text(generated.cache_manifest_path) != valid_manifest) {
        return fail("valid dense collision cache did not warm-load without rebuild");
    }

    auto invalid_runtime = valid_runtime;
    const std::size_t diagnostics_offset = 24u + 8u + 4u + 4u + 1u + 8u + 4u +
        read_u32_le(invalid_runtime, 24u + 8u + 4u + 4u + 1u + 8u) + 48u + 119u;
    const RuntimeSectionOffsets sections = locate_runtime_sections(invalid_runtime, diagnostics_offset);
    if (sections.candidate_frames < 24u || sections.dropped_actions < 24u ||
        sections.profile_semantic_hash < 24u) {
        return fail("dense collision runtime section locator failed");
    }
    write_u64_le(&invalid_runtime, sections.candidate_frames, diagnostics.candidate_actions + 2u);
    auto invalid_profile = generated.difficulty_profiles.front();
    invalid_profile.diagnostics.candidate_frames = diagnostics.candidate_actions + 2u;
    write_u64_le(&invalid_runtime, sections.profile_semantic_hash,
        ff7rp::pipeline::profile_semantic_hash(invalid_profile));
    rechecksum_runtime_cache(&invalid_runtime);
    if (!write_bytes(runtime_path, invalid_runtime)) {
        return fail("failed to write invalid dense collision cache");
    }

    entered_cold_pipeline = false;
    bool rejected_semantic_prerequisite = false;
    ff7rp::pipeline::LoadedSong rebuilt;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &rebuilt,
        [&](const char* stage) {
            if (std::string(stage) == "audio_decode_started" ||
                std::string(stage) == "midi_generation_started") {
                entered_cold_pipeline = true;
            }
            if (std::string(stage) == "runtime_artifacts_rejected:semantic_prerequisite") {
                rejected_semantic_prerequisite = true;
            }
        });
    if (!status.ok() || rebuilt.loaded_from_runtime_cache || !entered_cold_pipeline ||
        !rejected_semantic_prerequisite ||
        read_binary(runtime_path) != valid_runtime ||
        read_binary(rebuilt.cache_sidecar_path) != valid_mabf ||
        read_text(rebuilt.cache_manifest_path) != valid_manifest) {
        return fail("invalid dense collision prerequisite was not rejected and rebuilt deterministically");
    }

    invalid_runtime = valid_runtime;
    write_u64_le(&invalid_runtime, sections.dropped_actions, diagnostics.dropped_actions + 1u);
    invalid_profile = generated.difficulty_profiles.front();
    invalid_profile.diagnostics.dropped_actions = diagnostics.dropped_actions + 1u;
    write_u64_le(&invalid_runtime, sections.profile_semantic_hash,
        ff7rp::pipeline::profile_semantic_hash(invalid_profile));
    rechecksum_runtime_cache(&invalid_runtime);
    if (!write_bytes(runtime_path, invalid_runtime)) {
        return fail("failed to write impossible dropped-action count");
    }

    entered_cold_pipeline = false;
    rejected_semantic_prerequisite = false;
    ff7rp::pipeline::LoadedSong dropped_rebuilt;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &dropped_rebuilt,
        [&](const char* stage) {
            if (std::string(stage) == "audio_decode_started" ||
                std::string(stage) == "midi_generation_started") {
                entered_cold_pipeline = true;
            }
            if (std::string(stage) == "runtime_artifacts_rejected:semantic_prerequisite") {
                rejected_semantic_prerequisite = true;
            }
        });
    if (!status.ok() || dropped_rebuilt.loaded_from_runtime_cache || !entered_cold_pipeline ||
        !rejected_semantic_prerequisite ||
        read_binary(runtime_path) != valid_runtime ||
        read_binary(dropped_rebuilt.cache_sidecar_path) != valid_mabf ||
        read_text(dropped_rebuilt.cache_manifest_path) != valid_manifest) {
        return fail("impossible dropped-action count was not rejected and rebuilt deterministically");
    }
    return 0;
}

int test_normal_chart_cache_policy_normalization(const std::filesystem::path& root) {
    const auto run_direction = [&](const char* name, const bool generate_playable,
                                   const bool next_playable) {
        const std::filesystem::path song_directory = root / name;
        std::filesystem::create_directories(song_directory);
        if (!write_silent_wav(song_directory / "song.wav", 1.0) ||
            !write_explicit_song_json(song_directory / "song.json", name, false)) {
            return fail(std::string("failed to create normal policy fixture ") + name);
        }

        ff7rp::pipeline::configure_chart_row_limit(generate_playable, generate_playable);
        ff7rp::pipeline::LoadedSong generated;
        auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &generated);
        const std::string generated_identity = generate_playable
            ? ff7rp::pipeline::kPlayableExtendedChartRowPolicyIdentity
            : ff7rp::pipeline::kDisabledChartRowPolicyIdentity;
        if (!status.ok() || generated.loaded_from_runtime_cache ||
            generated.chart_policy_identity != generated_identity) {
            ff7rp::pipeline::configure_chart_row_limit(false, false);
            return fail(std::string("normal policy fixture generation used the wrong global identity: ") + status.message);
        }
        ff7rp::pipeline::configure_chart_row_limit(next_playable, next_playable);
        ff7rp::pipeline::LoadedSong rebuilt;
        status = ff7rp::pipeline::load_song_directory(song_directory.string(), &rebuilt);
        const std::string rebuilt_identity = next_playable
            ? ff7rp::pipeline::kPlayableExtendedChartRowPolicyIdentity
            : ff7rp::pipeline::kDisabledChartRowPolicyIdentity;
        if (!status.ok() || rebuilt.loaded_from_runtime_cache ||
            rebuilt.chart_policy_identity != rebuilt_identity || rebuilt.cache_key == generated.cache_key) {
            ff7rp::pipeline::configure_chart_row_limit(false, false);
            return fail(std::string("stale explicit cache crossed a changed global policy identity: ") + name);
        }
        ff7rp::pipeline::LoadedSong warm;
        status = ff7rp::pipeline::load_song_directory(song_directory.string(), &warm);
        if (!status.ok() || !warm.loaded_from_runtime_cache || warm.cache_key != rebuilt.cache_key ||
            !configs_equal(warm.config, rebuilt.config) || !charts_equal(warm.chart, rebuilt.chart)) {
            ff7rp::pipeline::configure_chart_row_limit(false, false);
            return fail(std::string("rebuilt explicit cache did not warm-load under its global policy: ") + name);
        }
        ff7rp::pipeline::configure_chart_row_limit(false, false);
        return 0;
    };

    if (run_direction("NormalDisabledToExtended", false, true) != 0) return 1;
    if (run_direction("NormalExtendedToDisabled", true, false) != 0) return 1;

    const std::filesystem::path accidental_directory = root / "AccidentalWarmCache";
    std::filesystem::create_directories(accidental_directory);
    MidiTrack accidental_melody;
    MidiTrack accidental_harmony;
    accidental_melody.push_back({0, 0, {0xff, 0x59, 0x02, 0xfe, 0x00}});
    for (int index = 0; index < 8; ++index) {
        const int tick = 1920 + index * 960;
        add_note(&accidental_melody, tick, 240, 84 + index % 3, 108);
        add_note(&accidental_harmony, tick, 480, 49, 82);
        add_note(&accidental_harmony, tick, 480, 53, 81);
        add_note(&accidental_harmony, tick, 480, 56, 80);
    }
    if (!write_bytes(accidental_directory / "song.mid", build_format_one_midi(
            std::move(accidental_melody), std::move(accidental_harmony)))
        || !write_silent_wav(accidental_directory / "song.wav", 20.0)
        || !write_song_json(accidental_directory / "song.json", "Accidental Warm Cache")) {
        return fail("failed to create accidental warm-cache fixture");
    }
    ff7rp::pipeline::LoadedSong accidental_cold;
    auto status = ff7rp::pipeline::load_song_directory(accidental_directory.string(), &accidental_cold);
    const std::string expected_flat_chord =
        ff7rp::pipeline::selected_native_asset_capabilities().has_verified_pca_db_voicing()
        ? "pca_Db" : "pca_Cs";
    const auto all_flat = [&expected_flat_chord](const ff7rp::pipeline::LoadedSong& song) {
        if (song.difficulty_profiles.empty()) return false;
        bool found_db_chord = false;
        bool found_db_melody = false;
        // Selection is independent per label. The source-backed Ab3 fallback
        // carries Db3/F3 followers; the root profile need not select harmony.
        // Check exact source identities/ticks and per-side lengths, not an old
        // selector's choice of hand in the first visible profile.
        for (const auto& profile : song.difficulty_profiles) {
            if (profile.config.notes.empty() ||
                profile.config.notes.size() != profile.chart.notes.size()) return false;
            for (std::size_t row = 0; row < profile.config.notes.size(); ++row) {
                const auto& note = profile.config.notes[row];
                const auto& compiled = profile.chart.notes[row];
                const double onset_index = (note.beat - 4.0) / 2.0;
                if (!std::isfinite(onset_index) || onset_index < 0 || onset_index > 7 ||
                    onset_index != std::floor(onset_index) || compiled.group_index != note.group_index ||
                    compiled.beat != note.beat || compiled.time_str !=
                        ff7rp::pipeline::beat_to_time_str(note.beat, profile.config.bpm)) return false;
                const std::array<std::string, 3> melody{"C6", "Db6", "D6"};
                const std::array<std::string, 3> native_melody{"Cn6", "Db6", "Dn6"};
                const auto index = static_cast<std::size_t>(onset_index) % 3u;
                if (!note.pitch.empty()) {
                    const bool fallback = note.pitch == "Ab3" || note.pitch == "Db3" || note.pitch == "F3";
                    const ff7rp::pipeline::NoteValueOverride value{
                        {static_cast<std::uint8_t>(fallback ? 2 : 3), 0}, true};
                    if ((!fallback && note.pitch != melody[index]) ||
                        compiled.monotone_id != (fallback ? (note.pitch == "F3" ? "Fn3" : note.pitch) : native_melody[index]) ||
                        note.monotone_note_value != value) return false;
                    found_db_melody |= note.pitch == "Db6";
                } else if (!compiled.monotone_id.empty()) return false;
                if (!note.chord_id.empty()) {
                    if (note.chord_id != expected_flat_chord || compiled.chord_id != expected_flat_chord ||
                        note.chord_note_value != ff7rp::pipeline::NoteValueOverride{{2, 0}, true}) return false;
                    found_db_chord = true;
                } else if (!compiled.chord_id.empty()) return false;
            }
        }
        return found_db_chord && found_db_melody;
    };
    if (!status.ok() || accidental_cold.loaded_from_runtime_cache || !all_flat(accidental_cold)) {
        return fail("key-signature accidental cache fixture did not cold-generate exact flat identities: " +
            status.message);
    }
    ff7rp::pipeline::LoadedSong accidental_warm;
    status = ff7rp::pipeline::load_song_directory(accidental_directory.string(), &accidental_warm);
    if (!status.ok() || !accidental_warm.loaded_from_runtime_cache
        || accidental_warm.cache_key != accidental_cold.cache_key
        || !configs_equal(accidental_warm.config, accidental_cold.config)
        || !charts_equal(accidental_warm.chart, accidental_cold.chart)
        || !all_flat(accidental_warm)) {
        return fail("key-signature accidental identities did not round-trip through the warm cache");
    }
    return 0;
}

int test_offline_artifact_goldens(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "OfflineGolden";
    std::filesystem::create_directories(song_directory);
    MidiTrack track;
    add_note(&track, 0, 480, 60, 100);
    if (!write_bytes(song_directory / "song.mid", build_midi(std::move(track))) ||
        !write_silent_wav(song_directory / "song.wav", 2.0) ||
        !write_song_json(song_directory / "song.json", "Offline Golden")) {
        return fail("failed to create offline artifact golden fixture");
    }

    ff7rp::pipeline::LoadedSong generated;
    const auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &generated);
    if (!status.ok()) return fail("offline artifact golden generation failed: " + status.message);

    std::string manifest = read_text(generated.cache_manifest_path);
    const std::string sidecar = "sidecar=" +
        std::filesystem::path(generated.cache_sidecar_path).generic_string() + "\n";
    const std::size_t sidecar_begin = manifest.find("sidecar=");
    const std::size_t sidecar_end = sidecar_begin == std::string::npos
        ? std::string::npos : manifest.find('\n', sidecar_begin);
    if (sidecar_begin == std::string::npos || sidecar_end == std::string::npos ||
        manifest.substr(sidecar_begin, sidecar_end - sidecar_begin + 1u) != sidecar) {
        return fail("manifest sidecar path boundary changed");
    }
    manifest.replace(sidecar_begin, sidecar_end - sidecar_begin + 1u, "sidecar=<cache-sidecar>\n");
    // This process-local generation depends on which cases ran before this one,
    // not the song. Check its real value before normalizing the golden identity
    // so both a filtered run and the complete suite exercise the same artifact.
    const std::string generation_line = "chart_policy_generation=" +
        std::to_string(generated.chart_policy_generation) + "\n";
    const auto generation_begin = manifest.find(generation_line);
    if (generation_begin == std::string::npos) {
        return fail("manifest chart policy generation does not match the generated song");
    }
    manifest.replace(generation_begin, generation_line.size(), "chart_policy_generation=0\n");
    const std::uint64_t normalized_manifest_digest = ff7rp::pipeline::fnv1a64_append(
        ff7rp::pipeline::kFnv1a64OffsetBasis, manifest.data(), manifest.size());

    const auto assets_1004 = ff7rp::pipeline::native_asset_capabilities_for_catalog(
        "ff7rebirth-steam-win64-68fd6fde");
    const auto assets_1005 = ff7rp::pipeline::native_asset_capabilities_for_catalog(
        "ff7rebirth-steam-win64-6a16ced2");
    if (assets_1004.cache_identity() != "native_assets=pca_Db_voicing:verified1004+1005" ||
        assets_1005.cache_identity() != "native_assets=pca_Db_voicing:verified1004+1005" ||
        assets_1004.cache_identity() != assets_1005.cache_identity()) {
        return fail("offline artifact golden native-asset identities changed");
    }

    std::uint64_t expected_cache_key = 0;
    std::uint64_t expected_normalized_manifest_digest = 0;
    const auto selected_asset_identity =
        ff7rp::pipeline::selected_native_asset_capabilities().cache_identity();
    if (selected_asset_identity == assets_1004.cache_identity()) {
        // Generated-MIDI v18 expands note values and chord coverage. Relative to
        // v16, this one-note fixture retains exact semantics and all six labels:
        // changing only the policy token reproduces the cache-key delta, and
        // restoring the old cache_key line recovers the old manifest digest.
        // Pipeline v48 and binary format 16 are unchanged.
        expected_cache_key = 0x4573351caad21631ull;
        expected_normalized_manifest_digest = 0x645b7fc861ae049eull;
    } else {
        return fail("offline artifact golden has no expectation for selected native-asset identity: " +
            std::string(selected_asset_identity));
    }
    constexpr std::size_t kExpectedNormalizedManifestBytes = 5373u;
    constexpr const char* kExpectedSemanticHash = "config_chart_semantic_hash=037ea322cb1e9dad";
    if (generated.cache_key != expected_cache_key ||
        normalized_manifest_digest != expected_normalized_manifest_digest ||
        manifest.size() != kExpectedNormalizedManifestBytes ||
        manifest_line(manifest, "config_chart_semantic_hash") != kExpectedSemanticHash) {
        return fail("offline artifact golden changed: cache=" + ff7rp::pipeline::hex64(generated.cache_key) +
            ", manifest=" + ff7rp::pipeline::hex64(normalized_manifest_digest) +
            ", bytes=" + std::to_string(manifest.size()) + ", semantic=" +
            manifest_line(manifest, "config_chart_semantic_hash"));
    }
    if (!generated.chart_from_midi || generated.config.notes.size() != 1u ||
        generated.chart.notes.size() != 1u) {
        return fail("offline MIDI/chart golden row count changed");
    }
    if (generated.difficulty_profiles.size() != 6u || !generated.difficulty_profile_omissions.empty()) {
        return fail("single-source golden lost independently feasible difficulty labels");
    }
    for (std::size_t i = 0; i < generated.difficulty_profiles.size(); ++i) {
        const auto& profile = generated.difficulty_profiles[i];
        if (profile.config.difficulty != static_cast<int>(i + 1) ||
            !charts_equal(profile.chart, generated.chart) ||
            !ff7rp::pipeline::validate_midi_difficulty_route(profile.config.notes,
                profile.config.bpm, profile.config.difficulty).feasible) {
            return fail("single-source golden changed musical semantics or real route safety");
        }
    }
    const auto& source = generated.config.notes.front();
    const auto& compiled = generated.chart.notes.front();
    if (generated.config.bpm != 120.0 || source.beat != 0.0 || source.duration_beats != 0.25 ||
        source.pitch != "C4" || !source.chord_id.empty() ||
        source.monotone_note_value != ff7rp::pipeline::NoteValueOverride{{2, 0}, true} ||
        source.chord_note_value.provided ||
        compiled.beat != 0.0 || compiled.duration_beats != 0.25 || compiled.pitch != "C4" ||
        compiled.time_str != "00_00" || compiled.monotone_id != "Cn4" ||
        !compiled.chord_id.empty() || compiled.monotone_note_type != 2 ||
        compiled.monotone_dot_type != 0 || compiled.chord_note_type != 0 || compiled.chord_dot_type != 0 ||
        compiled.camera_switch_timing != 0 || compiled.group_index != 0) {
        return fail("offline MIDI/chart compiled value golden changed: bpm=" +
            std::to_string(generated.config.bpm) + ", beat=" + std::to_string(source.beat) +
            ", duration=" + std::to_string(source.duration_beats) + ", pitch=" + source.pitch +
            ", chord=" + source.chord_id + ", time=" + compiled.time_str +
            ", monotone=" + compiled.monotone_id);
    }
    return 0;
}

int test_boundary_group_warm_cache(const std::filesystem::path& root) {
    for (const bool non_root_profile : {false, true}) {
        const auto directory = root / (non_root_profile ? "BoundaryGroupNonRoot" : "BoundaryGroupRoot");
        std::filesystem::create_directories(directory);
        if (!write_silent_wav(directory / "song.wav", 1.0)) return fail("boundary group audio setup failed");
        {
            std::ofstream out(directory / "song.json", std::ios::binary | std::ios::trunc);
            out << R"({"schema":"v2","title":"Boundary Group","bpm":120,"loudness_normalization":false,)";
            if (non_root_profile) {
                out << R"("profiles":[{"difficulty":0,"notes":[{"beat":0,"duration_beats":0.125,"pitch":"D4"}]},{"difficulty":3,)";
            }
            out << R"("notes":[)";
            out.precision(std::numeric_limits<double>::max_digits10);
            for (std::size_t row = 0; row < 513u; ++row) {
                if (row != 0) out << ',';
                // Distinct exact binary fractions avoid duplicate-source rejection
                // while keeping the complete boundary fixture within one second.
                out << R"({"beat":)" << (row / 512.0)
                    << R"(,"duration_beats":0.125,"pitch":"C4")";
                if (row >= 511u) out << R"(,"group_index":7)";
                out << '}';
            }
            out << (non_root_profile ? "]}]}" : "]}");
            if (!out) return fail("boundary group JSON setup failed");
        }
        ff7rp::pipeline::LoadedSong cold;
        auto status = ff7rp::pipeline::load_song_directory(directory.string(), &cold);
        const std::size_t index = non_root_profile ? 1u : 0u;
        if (!status.ok() || cold.loaded_from_runtime_cache || cold.difficulty_profiles.size() != index + 1u) {
            return fail("boundary group cold preparation failed: " + status.message);
        }
        const auto runtime_path = directory / ".cache" / "runtime.bin";
        const auto runtime_bytes = read_binary(runtime_path);
        const auto sidecar_bytes = read_binary(cold.cache_sidecar_path);
        const auto manifest = read_text(cold.cache_manifest_path);
        const auto resolved_path = ff7rp::pipeline::resolved_song_json_path(directory.string());
        const auto resolved = read_text(resolved_path);
        ff7rp::pipeline::LoadedSong warm;
        std::string warm_trace;
        status = ff7rp::pipeline::load_song_directory(directory.string(), &warm,
            [&](const char* stage) {
                if (!warm_trace.empty()) warm_trace += ',';
                warm_trace += stage;
            }, false);
        if (!status.ok() || !warm.loaded_from_runtime_cache) {
            return fail(std::string(non_root_profile ? "non-root" : "root") +
                " boundary profile warm cache rejected: " + status.message + "; trace=" + warm_trace);
        }
        if (warm.cache_key != cold.cache_key ||
            warm.difficulty_profiles.size() != cold.difficulty_profiles.size() ||
            runtime_bytes.empty() || read_binary(runtime_path) != runtime_bytes ||
            read_binary(cold.cache_sidecar_path) != sidecar_bytes ||
            read_text(cold.cache_manifest_path) != manifest || read_text(resolved_path) != resolved) {
            return fail(non_root_profile ? "non-root boundary profile did not reuse unchanged warm artifacts" :
                "root boundary profile did not reuse unchanged warm artifacts");
        }
        const auto& profile = warm.difficulty_profiles[index];
        ff7rp::pipeline::ChartEventPlan plan;
        if (!ff7rp::pipeline::derive_profile_event_plan(profile, &plan) ||
            plan.source_row_count != 513u || plan.native_prefix_event_count != 512u ||
            plan.native_event_count != 513u || plan.required_action_count != 512u ||
            profile.config.notes.back().group_index != 7u ||
            profile.diagnostic_chart.tail_rows.front().source.group_index != 7u ||
            !configs_equal(profile.config, cold.difficulty_profiles[index].config) ||
            !charts_equal(profile.chart, cold.difficulty_profiles[index].chart) ||
            !ff7rp::pipeline::diagnostic_charts_equal(profile.diagnostic_chart,
                cold.difficulty_profiles[index].diagnostic_chart)) {
            return fail("boundary group warm profile changed complete source/compiled topology");
        }
    }
    return 0;
}

int test_extended_chart_diagnostic_cache_isolation(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "ExtendedChartDiagnostic520";
    std::filesystem::create_directories(song_directory);
    if (!write_silent_wav(song_directory / "song.wav", 70.0) ||
        !write_extended_song_json(song_directory / "song.json")) {
        return fail("failed to create exactly-520 authored extended fixture");
    }
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    if (test_boundary_group_warm_cache(root) != 0) return 1;
    ff7rp::pipeline::LoadedSong generated;
    auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &generated);
    if (!status.ok() || generated.loaded_from_runtime_cache || generated.config.notes.size() != 512u ||
        generated.chart.notes.size() != 512u || generated.difficulty_profiles.size() != 1u) {
        return fail("playable extended generation did not preserve a 512-row prefix: " +
            status.message + ", cache=" + std::to_string(generated.loaded_from_runtime_cache) +
            ", config=" + std::to_string(generated.config.notes.size()) +
            ", chart=" + std::to_string(generated.chart.notes.size()) +
            ", profiles=" + std::to_string(generated.difficulty_profiles.size()));
    }
    const auto& profile = generated.difficulty_profiles.front();
    ff7rp::pipeline::ChartEventPlan plan;
    if (profile.config.notes.size() != 512u || profile.chart.notes.size() != 512u ||
        !profile.config.diagnostic_extended_chart_fixture ||
        profile.diagnostic_chart.source_row_count != 520u ||
        profile.diagnostic_chart.native_prefix_row_count != 512u ||
        profile.diagnostic_chart.tail_rows.size() != 8u ||
        profile.diagnostic_chart.tail_rows.front().source_row != 512u ||
        profile.diagnostic_chart.tail_rows.back().source_row != 519u ||
        profile.diagnostic_chart.descriptor_hash == 0 ||
        !ff7rp::pipeline::derive_profile_event_plan(profile, &plan) ||
        plan.source_row_count != 520u || plan.native_prefix_event_count != 512u ||
        plan.native_event_count != 521u || plan.required_action_count != 519u ||
        plan.physical_digest == 0u || profile.diagnostics.selected_actions != 519u ||
        profile.diagnostic_chart.tail_rows.front().source.group_index != 7u ||
        profile.diagnostic_chart.tail_rows[1].source.chord_id != "pca_C" ||
        profile.diagnostic_chart.tail_rows[2].source.ignore_sound_pitches != std::vector<std::string>{"As2"}) {
        return fail("authored extended profile did not preserve its complete generalized 512+8 plan");
    }
    const std::string resolved = read_text(
        ff7rp::pipeline::resolved_song_json_path(song_directory.string()));
    ff7rp::pipeline::ParsedSongSource parsed_resolved;
    if (!ff7rp::pipeline::parse_song_json_string(resolved, &parsed_resolved).ok() ||
        !parsed_resolved.authored_profiles.empty() || parsed_resolved.config.notes.size() != 520u ||
        parsed_resolved.config.notes[510].group_index != 7u ||
        parsed_resolved.config.notes[511].group_index != 7u ||
        parsed_resolved.config.notes[512].group_index != 7u ||
        parsed_resolved.config.notes[513].pitch.empty() ||
        parsed_resolved.config.notes[513].chord_id != "pca_C" ||
        parsed_resolved.config.notes[514].ignore_sound_pitches != std::vector<std::string>{"As2"}) {
        return fail("authored extended resolved output clipped or changed generalized row semantics");
    }
    const std::string manifest = read_text(generated.cache_manifest_path);
    if (manifest.find(std::string("chart_row_policy=") +
            ff7rp::pipeline::kPlayableExtendedChartRowPolicyIdentity) == std::string::npos ||
        manifest.find("profile_diagnostic_source_rows=520") == std::string::npos ||
        manifest.find("profile_diagnostic_native_prefix_rows=512") == std::string::npos ||
        manifest.find("profile_diagnostic_tail_rows=8") == std::string::npos) {
        return fail("extended cache manifest omitted playable row-policy identity");
    }
    ff7rp::pipeline::LoadedSong cached;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &cached);
    if (!status.ok() || !cached.loaded_from_runtime_cache ||
        cached.difficulty_profiles.front().diagnostic_chart.tail_rows.size() != 8u ||
        read_text(ff7rp::pipeline::resolved_song_json_path(song_directory.string())) != resolved) {
        return fail("playable extended cache did not round-trip its complete tail");
    }
    const std::filesystem::path runtime_path = song_directory / ".cache" / "runtime.bin";
    const std::vector<std::uint8_t> valid_runtime = read_binary(runtime_path);
    const std::vector<std::uint8_t> valid_mabf = read_binary(generated.cache_sidecar_path);
    const std::size_t policy_length_offset = 24u + 8u + 4u + 4u + 1u + 8u;
    const std::size_t metadata_offset = policy_length_offset + 4u +
        read_u32_le(valid_runtime, policy_length_offset);
    const RuntimeSectionOffsets sections = locate_runtime_sections(valid_runtime, metadata_offset + 48u + 119u);
    if (sections.descriptor_hash < 24u || sections.tail_source_beat < 24u ||
        sections.tail_compiled_beat < 24u || sections.profile_semantic_hash < 24u) {
        return fail("independent extended runtime section locator missed tail fields");
    }
    const auto expect_tail_rebuild = [&](const char* name, auto mutate) {
        auto corrupted = valid_runtime;
        mutate(&corrupted);
        rechecksum_runtime_cache(&corrupted);
        if (!write_bytes(runtime_path, corrupted)) return fail("failed to write extended corruption");
        ff7rp::pipeline::LoadedSong rebuilt;
        const auto rebuilt_status = ff7rp::pipeline::load_song_directory(song_directory.string(), &rebuilt);
        if (!rebuilt_status.ok() || rebuilt.loaded_from_runtime_cache ||
            read_binary(runtime_path) != valid_runtime ||
            read_binary(generated.cache_sidecar_path) != valid_mabf ||
            read_text(generated.cache_manifest_path) != manifest) {
            return fail(std::string("rechecksummed extended ") + name +
                " corruption was not rejected and rebuilt");
        }
        return 0;
    };
    if (expect_tail_rebuild("descriptor hash", [&](auto* bytes) {
            (*bytes)[sections.descriptor_hash] ^= 1u;
        }) != 0 ||
        expect_tail_rebuild("source tail timing", [&](auto* bytes) {
            write_double_le(bytes, sections.tail_source_beat, 999.0);
        }) != 0 ||
        expect_tail_rebuild("compiled tail timing", [&](auto* bytes) {
            write_double_le(bytes, sections.tail_compiled_beat, 999.0);
        }) != 0) {
        return 1;
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    ff7rp::pipeline::LoadedSong disabled;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &disabled);
    if (status.ok() || disabled.loaded_from_runtime_cache ||
        disabled.accepted_chart_input_limit != 512u || disabled.chart_policy_enabled ||
        disabled.chart_policy_identity != "chart_rows=native512;extended=disabled" ||
        read_binary(runtime_path) != valid_runtime || read_binary(generated.cache_sidecar_path) != valid_mabf ||
        read_text(generated.cache_manifest_path) != manifest) {
        return fail("playable cache loaded or rebuilt under native-512 policy");
    }
    ff7rp::pipeline::configure_chart_row_limit(true, false);
    ff7rp::pipeline::LoadedSong mismatched;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &mismatched);
    if (status.ok() || mismatched.loaded_from_runtime_cache ||
        mismatched.accepted_chart_input_limit != 512u || !mismatched.chart_policy_enabled ||
        mismatched.chart_policy_identity != ff7rp::pipeline::kDiagnosticChartRowPolicyIdentity ||
        read_binary(runtime_path) != valid_runtime || read_binary(generated.cache_sidecar_path) != valid_mabf ||
        read_text(generated.cache_manifest_path) != manifest) {
        return fail("playable cache gained authority from diagnostic-only capability");
    }
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    ff7rp::pipeline::LoadedSong recovered;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &recovered);
    if (!status.ok() || recovered.loaded_from_runtime_cache ||
        recovered.config.notes.size() != 512u || recovered.chart.notes.size() != 512u) {
        return fail("changed policy generation did not safely rebuild the playable extended cache");
    }
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    ff7rp::pipeline::LoadedSong recovered_cached;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &recovered_cached);
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    if (!status.ok() || !recovered_cached.loaded_from_runtime_cache ||
        recovered_cached.config.notes.size() != 512u || recovered_cached.chart.notes.size() != 512u) {
        return fail("rebuilt extended cache did not warm-load under an equivalent policy");
    }

    const std::filesystem::path profiles_directory = root / "ExtendedAuthoredProfiles513";
    std::filesystem::create_directories(profiles_directory);
    if (!write_silent_wav(profiles_directory / "song.wav", 1.0) ||
        !write_extended_profiles_song_json(profiles_directory / "song.json")) {
        return fail("failed to create authored extended-profiles fixture");
    }
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    ff7rp::pipeline::LoadedSong profiles_song;
    status = ff7rp::pipeline::load_song_directory(profiles_directory.string(), &profiles_song);
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    if (!status.ok() || profiles_song.difficulty_profiles.size() != 1u ||
        profiles_song.difficulty_profiles.front().config.notes.size() != 512u ||
        profiles_song.difficulty_profiles.front().diagnostic_chart.source_row_count != 513u ||
        profiles_song.difficulty_profiles.front().diagnostic_chart.tail_rows.size() != 1u) {
        return fail("authored profiles[].notes above 512 did not use complete extended transport");
    }
    return 0;
}

int test_row_limit_omission(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "RowLimitFixture";
    std::filesystem::create_directories(song_directory);
    MidiTrack track;
    constexpr int onset_count = 1200;
    for (int index = 0; index < onset_count; ++index) {
        const int tick = index * 180;
        if (index % 12 == 0) {
            add_note(&track, tick, 40, 48, 90);
            add_note(&track, tick, 40, 52, 90);
            add_note(&track, tick, 40, 55, 90);
        }
        add_note(&track, tick, 40, 60 + index % 7, 110);
    }
    if (!write_bytes(song_directory / "song.mid", build_midi(std::move(track), 460000u)) ||
        !write_silent_wav(song_directory / "song.wav", 210.0) ||
        !write_song_json(song_directory / "song.json", "Row Limit Fixture")) {
        return fail("failed to write row-limit fixture");
    }

    ff7rp::pipeline::LoadedSong song;
    const auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &song);
    // The independent root routes fit, but exact simultaneous followers push
    // every complete profile beyond 512. Do not repair by dropping sounds.
    if (status.code != ff7rp::pipeline::StatusCode::InvalidChart ||
        !song.difficulty_profiles.empty() || song.difficulty_profile_omissions.size() != 6 ||
        song.loaded_from_runtime_cache) return fail("oversized enriched profiles were published");
    for (const auto& omission : song.difficulty_profile_omissions) {
        if (omission.desired_rows <= ff7rp::pipeline::kMaxChartRows ||
            !omission.diagnostics.row_limit_exceeded || omission.diagnostics.complete)
            return fail("enriched row-limit omission lost its complete physical count");
    }
    if (ff7rp::pipeline::maximum_midi_visible_profile_actions(221) > 299 ||
        ff7rp::pipeline::maximum_midi_visible_profile_actions(256) > 346) {
        return fail("maximum adjacent visible-step policy permits the proven 221/256-row jumps");
    }
    return 0;
}

int test_adaptive_metronome_modes(const std::filesystem::path& root) {
    const std::filesystem::path enabled_directory = root / "MetronomeEnabled";
    const std::filesystem::path disabled_directory = root / "MetronomeDisabled";
    std::filesystem::create_directories(enabled_directory);
    std::filesystem::create_directories(disabled_directory);
    if (!write_tone_wav(enabled_directory / "song.wav", 2.1, 30000.0) ||
        !write_silent_wav(disabled_directory / "song.wav", 2.1) ||
        !write_explicit_song_json(enabled_directory / "song.json", "Metronome Enabled", true) ||
        !write_explicit_song_json(disabled_directory / "song.json", "Metronome Disabled", false)) {
        return fail("failed to write metronome MABF fixtures");
    }

    ff7rp::pipeline::LoadedSong enabled;
    ff7rp::pipeline::LoadedSong disabled;
    auto status = ff7rp::pipeline::load_song_directory(enabled_directory.generic_string(), &enabled);
    if (!status.ok() || !enabled.config.metronome_enabled || enabled.metronome_beat_count == 0 ||
        enabled.metronome_downbeat_count == 0 || enabled.metronome_first_beat_seconds < 0.0 ||
        enabled.metronome_last_beat_seconds < enabled.metronome_first_beat_seconds) {
        return fail("adaptive metronome fixture did not produce beat diagnostics");
    }
    if (enabled.cache_key == 0) return fail("metronome fixture has no canonical cache identity");
    const std::vector<std::uint8_t> enabled_mabf = read_binary(enabled.cache_sidecar_path);
    if (enabled_mabf.size() <= ff7rp::pipeline::kMabfHeaderSize ||
        (enabled_mabf.size() - ff7rp::pipeline::kMabfHeaderSize) % 3u != 0u) {
        return fail("adaptive metronome MABF was not produced");
    }
    const std::size_t enabled_slot_size =
        (enabled_mabf.size() - ff7rp::pipeline::kMabfHeaderSize) / 3u;
    const std::size_t enabled_hca_size =
        ff7rp::pipeline::kMabfHcaHeaderSize + read_u32_le(enabled_mabf, 0x448);
    const auto mode_equal = [&](const std::size_t left, const std::size_t right) {
        const std::size_t left_offset = ff7rp::pipeline::kMabfHeaderSize + left * enabled_slot_size;
        const std::size_t right_offset = ff7rp::pipeline::kMabfHeaderSize + right * enabled_slot_size;
        return std::equal(enabled_mabf.begin() + left_offset,
            enabled_mabf.begin() + left_offset + enabled_hca_size,
            enabled_mabf.begin() + right_offset);
    };
    if (mode_equal(0, 1) || mode_equal(0, 2) || !mode_equal(1, 2)) {
        return fail("enabled metronome did not preserve Mode0 guide with clean Mode1/Mode2 payloads");
    }
    ff7rp::pipeline::MabfArtifactMetadata enabled_metadata;
    if (!ff7rp::pipeline::validate_resolved_mabf(enabled_mabf, enabled.audio.source_frame_count,
            {enabled.audio_sources.resolved_authored_indices, true}, &enabled_metadata).ok() ||
        enabled_metadata.logical_source_frames != enabled.audio.source_frame_count ||
        enabled_metadata.sample_rate != enabled.audio.sample_rate ||
        enabled_metadata.channels != enabled.audio.channels) {
        return fail("enabled metronome changed MABF duration or geometry");
    }
    status = ff7rp::pipeline::load_song_directory(disabled_directory.string(), &disabled);
    if (!status.ok()) return fail("disabled metronome fixture failed: " + status.message);
    if (disabled.metronome_beat_count != 0 || disabled.metronome_downbeat_count != 0 ||
        std::any_of(disabled.audio.stereo_samples.begin(), disabled.audio.stereo_samples.end(),
            [](const float sample) { return sample != 0.0f; })) {
        return fail("clean fallback audio was contaminated by metronome synthesis");
    }

    const std::vector<std::uint8_t> disabled_mabf = read_binary(disabled.cache_sidecar_path);
    if (disabled_mabf.size() <= ff7rp::pipeline::kMabfHeaderSize) {
        return fail("clean fallback MABF was not produced");
    }
    const std::size_t hca_size = ff7rp::pipeline::kMabfHcaHeaderSize + read_u32_le(disabled_mabf, 0x448);
    const std::size_t slot_size = ff7rp::pipeline::mabf_slot_size(hca_size);
    const std::size_t first = ff7rp::pipeline::kMabfHeaderSize;
    for (std::size_t slot = 1; slot < 3; ++slot) {
        const std::size_t offset = first + slot * slot_size;
        if (offset + hca_size > disabled_mabf.size() ||
            !std::equal(disabled_mabf.begin() + first, disabled_mabf.begin() + first + hca_size,
                disabled_mabf.begin() + offset)) {
            return fail("disabled fallback did not preserve clean audio in every MABF mode");
        }
    }

    ff7rp::pipeline::LoadedSong enabled_cached;
    status = ff7rp::pipeline::load_song_directory(enabled_directory.string(), &enabled_cached);
    if (!status.ok() || !enabled_cached.loaded_from_runtime_cache ||
        enabled_cached.metronome_beat_count != enabled.metronome_beat_count ||
        enabled_cached.metronome_downbeat_count != enabled.metronome_downbeat_count) {
        return fail("adaptive metronome diagnostics did not survive runtime-cache round trip");
    }
    const std::locale previous_locale = std::locale();
    std::locale::global(std::locale(previous_locale, new CommaDecimalPoint));
    ff7rp::pipeline::LoadedSong locale_cached;
    status = ff7rp::pipeline::load_song_directory(enabled_directory.string(), &locale_cached);
    std::locale::global(previous_locale);
    if (!status.ok() || !locale_cached.loaded_from_runtime_cache) {
        return fail("process-global numeric locale invalidated the canonical cache manifest");
    }
    std::vector<std::uint8_t> adaptive_substitution = enabled_mabf;
    const std::size_t mode0_offset = ff7rp::pipeline::kMabfHeaderSize;
    const std::size_t mode2_offset = ff7rp::pipeline::kMabfHeaderSize + 2u * enabled_slot_size;
    std::copy(
        adaptive_substitution.begin() + mode0_offset,
        adaptive_substitution.begin() + mode0_offset + enabled_hca_size,
        adaptive_substitution.begin() + mode2_offset);
    if (!write_bytes(enabled.cache_sidecar_path, adaptive_substitution)) {
        return fail("failed to write structurally valid adaptive mode substitution");
    }
    ff7rp::pipeline::LoadedSong repaired_adaptive;
    status = ff7rp::pipeline::load_song_directory(enabled_directory.string(), &repaired_adaptive);
    if (!status.ok() || repaired_adaptive.loaded_from_runtime_cache ||
        read_binary(enabled.cache_sidecar_path) != enabled_mabf) {
        return fail("adaptive Mode2 substitution was not rejected and rebuilt identically");
    }

    ff7rp::pipeline::LoadedSong cached;
    status = ff7rp::pipeline::load_song_directory(disabled_directory.string(), &cached);
    if (!status.ok() || !cached.loaded_from_runtime_cache || cached.metronome_beat_count != 0 ||
        cached.metronome_downbeat_count != 0 || cached.config.metronome_enabled ||
        std::fabs(cached.config.metronome_level - 0.12) > 1e-9) {
        return fail("disabled adaptive-metronome contract did not survive runtime-cache round trip");
    }
    if (!write_explicit_song_json(disabled_directory / "song.json", "Metronome Disabled", true)) {
        return fail("failed to enable metronome over the disabled cache fixture");
    }
    ff7rp::pipeline::LoadedSong enabled_over_cache;
    status = ff7rp::pipeline::load_song_directory(disabled_directory.string(), &enabled_over_cache);
    if (!status.ok() || enabled_over_cache.loaded_from_runtime_cache ||
        !enabled_over_cache.config.metronome_enabled ||
        read_binary(disabled.cache_sidecar_path) == disabled_mabf) {
        return fail("enabling metronome did not invalidate and rebuild the disabled cache");
    }
    if (!write_explicit_song_json(disabled_directory / "song.json", "Metronome Disabled", false)) {
        return fail("failed to restore disabled metronome fixture");
    }
    ff7rp::pipeline::LoadedSong restored;
    status = ff7rp::pipeline::load_song_directory(disabled_directory.string(), &restored);
    if (!status.ok() || restored.loaded_from_runtime_cache || restored.config.metronome_enabled ||
        read_binary(restored.cache_sidecar_path) != disabled_mabf) {
        return fail("disabling metronome did not rebuild the canonical clean cache");
    }
    return 0;
}

int test_midi_metronome_offset_source_rule(const std::filesystem::path& root) {
    const std::filesystem::path directory = root / "MidiMetronomeOffset";
    std::filesystem::create_directories(directory);
    MidiTrack events;
    for (int index = 0; index < 8; ++index) {
        add_note(&events, 1920 + index * 480, 240, 60 + index, 96);
    }
    if (!write_silent_wav(directory / "song.wav", 8.0) ||
        !write_bytes(directory / "song.mid", build_midi(std::move(events)))) {
        return fail("failed to write MIDI metronome-offset fixture inputs");
    }
    const auto write_config = [&](const bool include_offset) {
        std::ofstream out(directory / "song.json", std::ios::binary | std::ios::trunc);
        out << "{\n  \"schema\": \"v2\",\n  \"title\": \"MIDI metronome offset\",\n"
            << "  \"metronome\": { \"enabled\": false";
        if (include_offset) out << ", \"beat_zero_offset_seconds\": 0";
        out << " }\n}\n";
        return out.good();
    };
    if (!write_config(true)) return fail("failed to write explicit-zero MIDI metronome fixture");

    ff7rp::pipeline::SongConfig parsed;
    auto status = ff7rp::pipeline::load_song_json_file((directory / "song.json").string(), &parsed);
    if (!status.ok() || parsed.metronome_enabled ||
        !parsed.metronome_beat_zero_offset_provided || parsed.metronome_beat_zero_offset_seconds != 0.0) {
        return fail("parser did not preserve explicit zero on a disabled metronome");
    }
    ff7rp::pipeline::LoadedSong rejected;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &rejected);
    if (status.code != ff7rp::pipeline::StatusCode::InvalidJson ||
        status.message != "metronome.beat_zero_offset_seconds is only valid with explicit JSON notes") {
        return fail("MIDI source selection did not reject explicit disabled zero offset: " + status.message);
    }

    if (!write_config(false)) return fail("failed to omit MIDI metronome offset");
    ff7rp::pipeline::LoadedSong accepted;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &accepted);
    if (!status.ok() || !accepted.chart_from_midi || accepted.difficulty_profiles.empty() ||
        accepted.config.metronome_beat_zero_offset_provided) {
        return fail("omitting the MIDI beat-zero offset did not restore normal generation: " + status.message);
    }
    return 0;
}

int test_mode_specific_audio_sources(const std::filesystem::path& root) {
    const auto mode_equal = [](const std::vector<std::uint8_t>& mabf,
                                const std::size_t left, const std::size_t right) {
        if (mabf.size() <= ff7rp::pipeline::kMabfHeaderSize) return false;
        const std::size_t hca_size = ff7rp::pipeline::kMabfHcaHeaderSize + read_u32_le(mabf, 0x448);
        const std::size_t slot_size = ff7rp::pipeline::mabf_slot_size(hca_size);
        const std::size_t left_offset = ff7rp::pipeline::kMabfHeaderSize + left * slot_size;
        const std::size_t right_offset = ff7rp::pipeline::kMabfHeaderSize + right * slot_size;
        return right_offset + hca_size <= mabf.size() &&
            std::equal(mabf.begin() + left_offset, mabf.begin() + left_offset + hca_size,
                mabf.begin() + right_offset);
    };
    const auto same_mode_across = [](const std::vector<std::uint8_t>& left,
                                     const std::vector<std::uint8_t>& right,
                                     const std::size_t mode) {
        if (left.size() <= ff7rp::pipeline::kMabfHeaderSize ||
            right.size() <= ff7rp::pipeline::kMabfHeaderSize) return false;
        const std::size_t left_hca_size =
            ff7rp::pipeline::kMabfHcaHeaderSize + read_u32_le(left, 0x448);
        const std::size_t right_hca_size =
            ff7rp::pipeline::kMabfHcaHeaderSize + read_u32_le(right, 0x448);
        if (left_hca_size != right_hca_size) return false;
        const std::size_t left_offset = ff7rp::pipeline::kMabfHeaderSize +
            mode * ff7rp::pipeline::mabf_slot_size(left_hca_size);
        const std::size_t right_offset = ff7rp::pipeline::kMabfHeaderSize +
            mode * ff7rp::pipeline::mabf_slot_size(right_hca_size);
        return left_offset + left_hca_size <= left.size() &&
            right_offset + right_hca_size <= right.size() &&
            std::equal(left.begin() + left_offset, left.begin() + left_offset + left_hca_size,
                right.begin() + right_offset);
    };

    const std::filesystem::path directory = root / "ModeSources";
    std::filesystem::create_directories(directory);
    if (!write_tone_wav(directory / "SoNg.WaV", 0.6, 3000.0) ||
        !write_explicit_song_json(directory / "song.json", "Mode Sources", false)) {
        return fail("failed to create case-insensitive base fixture");
    }
    ff7rp::pipeline::LoadedSong legacy;
    auto status = ff7rp::pipeline::load_song_directory(directory.string(), &legacy);
    if (!status.ok() || legacy.audio_sources.authored[0].filename != "song.wav" ||
        legacy.audio_sources.resolved_authored_indices != std::array<std::uint8_t, 3>{0, 0, 0}) {
        return fail("legacy one-file source did not resolve directly to base for every mode");
    }
    const auto legacy_mabf = read_binary(legacy.cache_sidecar_path);
    if (!mode_equal(legacy_mabf, 0, 1) || !mode_equal(legacy_mabf, 0, 2)) {
        return fail("legacy one-file MABF changed mode payload compatibility");
    }
    const std::uint64_t legacy_key = legacy.cache_key;

    if (!write_tone_wav(directory / "SONG.MODE1.WAV", 0.6, 9000.0)) {
        return fail("failed to create Mode1 override fixture");
    }
    ff7rp::pipeline::LoadedSong mode1;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &mode1);
    const auto mode1_mabf = read_binary(mode1.cache_sidecar_path);
    if (!status.ok() || mode1.loaded_from_runtime_cache || mode1.cache_key == legacy_key ||
        mode1.audio_sources.resolved_authored_indices != std::array<std::uint8_t, 3>{0, 1, 0} ||
        mode_equal(mode1_mabf, 0, 1) || !mode_equal(mode1_mabf, 0, 2)) {
        return fail("Mode1 override did not preserve direct Mode2-to-base fallback");
    }
    const std::uint64_t mode1_key = mode1.cache_key;
    if (!write_tone_wav(directory / "SONG.MODE1.WAV", 0.6, 10000.0)) {
        return fail("failed to mutate Mode1 override content");
    }
    ff7rp::pipeline::LoadedSong changed_mode1;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &changed_mode1);
    if (!status.ok() || changed_mode1.loaded_from_runtime_cache || changed_mode1.cache_key == mode1_key) {
        return fail("Mode1 override content did not invalidate cache identity");
    }

    if (!write_tone_wav(directory / "song.mode2.wav", 0.6, 15000.0)) {
        return fail("failed to create Mode2 override fixture");
    }
    ff7rp::pipeline::LoadedSong both;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &both);
    const auto both_mabf = read_binary(both.cache_sidecar_path);
    if (!status.ok() || both.cache_key == changed_mode1.cache_key ||
        both.audio_sources.resolved_authored_indices != std::array<std::uint8_t, 3>{0, 1, 2} ||
        mode_equal(both_mabf, 0, 1) || mode_equal(both_mabf, 0, 2) || mode_equal(both_mabf, 1, 2)) {
        return fail("independent Mode1/Mode2 overrides did not produce three resolved payloads");
    }
    const std::string both_manifest = read_text(both.cache_manifest_path);
    if (both_manifest.find("mode0_resolved_role=base\n") == std::string::npos ||
        both_manifest.find("mode1_resolved_role=mode1\n") == std::string::npos ||
        both_manifest.find("mode2_resolved_role=mode2\n") == std::string::npos ||
        both_manifest.find("mode1_direct_base_fallback=0\n") == std::string::npos ||
        both_manifest.find("mode2_metronome=0\n") == std::string::npos) {
        return fail("mode-specific manifest resolution is incomplete");
    }
    ff7rp::pipeline::LoadedSong both_cached;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &both_cached);
    if (!status.ok() || !both_cached.loaded_from_runtime_cache ||
        both_cached.audio_sources.resolved_authored_indices != std::array<std::uint8_t, 3>{0, 1, 2} ||
        both_cached.audio.source_frame_count != both.audio.source_frame_count) {
        return fail("mode resolution or base duration did not survive runtime-cache reload");
    }

    if (!write_explicit_song_json(directory / "song.json", "Mode Sources", true)) {
        return fail("failed to enable metronome for explicit override composition fixture");
    }
    ff7rp::pipeline::LoadedSong guided;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &guided);
    const auto guided_mabf = read_binary(guided.cache_sidecar_path);
    ff7rp::pipeline::MabfArtifactMetadata guided_metadata;
    const auto guided_validation = ff7rp::pipeline::validate_resolved_mabf(
        guided_mabf, guided.audio.source_frame_count, {{0, 1, 2}, true}, &guided_metadata);
    const std::string guided_manifest = read_text(guided.cache_manifest_path);
    if (!status.ok() || guided.loaded_from_runtime_cache || !guided_validation.ok() ||
        same_mode_across(both_mabf, guided_mabf, 0u) ||
        !same_mode_across(both_mabf, guided_mabf, 1u) ||
        !same_mode_across(both_mabf, guided_mabf, 2u) ||
        guided_metadata.logical_source_frames != guided.audio.source_frame_count ||
        guided_manifest.find("metronome_mode_mapping=mode0_only\n") == std::string::npos ||
        guided_manifest.find("mode0_resolved_role=base\n") == std::string::npos ||
        guided_manifest.find("mode1_resolved_role=mode1\n") == std::string::npos ||
        guided_manifest.find("mode2_resolved_role=mode2\n") == std::string::npos ||
        guided_manifest.find("mode0_metronome=1\n") == std::string::npos ||
        guided_manifest.find("mode1_metronome=0\n") == std::string::npos ||
        guided_manifest.find("mode2_metronome=0\n") == std::string::npos ||
        guided_manifest.find("mode0_hca_frames=" + std::to_string(guided_metadata.hca_frame_count) + "\n") ==
            std::string::npos ||
        guided_manifest.find("mode1_hca_frames=" + std::to_string(guided_metadata.hca_frame_count) + "\n") ==
            std::string::npos ||
        guided_manifest.find("mode2_hca_frames=" + std::to_string(guided_metadata.hca_frame_count) + "\n") ==
            std::string::npos) {
        return fail("metronome plus explicit overrides did not preserve Mode0-only guide composition");
    }
    if (!write_explicit_song_json(directory / "song.json", "Mode Sources", false)) {
        return fail("failed to restore clean explicit override fixture");
    }

    std::filesystem::remove(directory / "SONG.MODE1.WAV");
    ff7rp::pipeline::LoadedSong mode2;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &mode2);
    const auto mode2_mabf = read_binary(mode2.cache_sidecar_path);
    if (!status.ok() || mode2.audio_sources.resolved_authored_indices !=
            std::array<std::uint8_t, 3>{0, 0, 2} ||
        !mode_equal(mode2_mabf, 0, 1) || mode_equal(mode2_mabf, 0, 2)) {
        return fail("Mode2-only override cascaded through another mode instead of base");
    }

    std::filesystem::remove(directory / "song.mode2.wav");
    ff7rp::pipeline::LoadedSong legacy_again;
    status = ff7rp::pipeline::load_song_directory(directory.string(), &legacy_again);
    if (!status.ok() || legacy_again.cache_key != legacy_key ||
        read_binary(legacy_again.cache_sidecar_path) != legacy_mabf) {
        return fail("removing overrides did not restore deterministic legacy identity and output");
    }

    const std::filesystem::path equal_directory = root / "EqualOverrides";
    std::filesystem::create_directories(equal_directory);
    if (!write_tone_wav(equal_directory / "song.wav", 0.6, 5000.0) ||
        !write_tone_wav(equal_directory / "song.mode1.wav", 0.6, 5000.0) ||
        !write_explicit_song_json(equal_directory / "song.json", "Equal Overrides", false)) {
        return fail("failed to create byte-identical override fixture");
    }
    ff7rp::pipeline::LoadedSong equal_override;
    status = ff7rp::pipeline::load_song_directory(equal_directory.string(), &equal_override);
    if (!status.ok() || !mode_equal(read_binary(equal_override.cache_sidecar_path), 0, 1)) {
        return fail("byte-identical authored override was rejected or forced distinct");
    }

    const std::filesystem::path mixed_directory = root / "MixedFormats";
    const std::filesystem::path encoded_fixtures =
        std::filesystem::path(__FILE__).parent_path() / "fixtures" / "encoded_audio";
    std::filesystem::create_directories(mixed_directory);
    std::error_code copy_error;
    std::filesystem::copy_file(encoded_fixtures / "source.wav", mixed_directory / "song.wav",
        std::filesystem::copy_options::overwrite_existing, copy_error);
    if (!copy_error) {
        std::filesystem::copy_file(encoded_fixtures / "fixture.mp3", mixed_directory / "song.mode1.mp3",
            std::filesystem::copy_options::overwrite_existing, copy_error);
    }
    if (!copy_error) {
        std::filesystem::copy_file(encoded_fixtures / "fixture.flac", mixed_directory / "song.mode2.flac",
            std::filesystem::copy_options::overwrite_existing, copy_error);
    }
    if (copy_error || !write_explicit_song_json(mixed_directory / "song.json", "Mixed Formats", false)) {
        return fail("failed to create mixed WAV/MP3/FLAC fixture");
    }
    ff7rp::pipeline::LoadedSong mixed;
    status = ff7rp::pipeline::load_song_directory(mixed_directory.string(), &mixed);
    if (!status.ok() || mixed.audio_sources.resolved_authored_indices !=
            std::array<std::uint8_t, 3>{0, 1, 2} || mixed.audio.source_frame_count != 48000u) {
        return fail("mixed WAV/MP3/FLAC roles did not decode to equal resolved geometry");
    }

    const auto expect_rejected = [&](const std::string& name,
                                     const std::function<bool(const std::filesystem::path&)>& prepare,
                                     const bool json_must_remain_absent = false) {
        const std::filesystem::path rejected = root / name;
        std::filesystem::create_directories(rejected);
        if (!prepare(rejected)) return false;
        ff7rp::pipeline::LoadedSong song;
        const auto rejected_status = ff7rp::pipeline::load_song_directory(rejected.string(), &song);
        return !rejected_status.ok() && !std::filesystem::exists(rejected / ".cache" / "song.mabf.bin") &&
            !std::filesystem::exists(rejected / ".cache" / "manifest.json") &&
            (!json_must_remain_absent || !std::filesystem::exists(rejected / "song.json"));
    };
    if (!expect_rejected("MissingBase", [](const auto& path) {
            return write_tone_wav(path / "song.mode1.wav", 0.6);
        }, true) ||
        !expect_rejected("AmbiguousBase", [](const auto& path) {
            return write_tone_wav(path / "song.wav", 0.6) && write_tone_wav(path / "song.mp3", 0.6);
        }, true) ||
        !expect_rejected("AmbiguousMode1", [](const auto& path) {
            return write_tone_wav(path / "song.wav", 0.6) &&
                write_tone_wav(path / "song.mode1.wav", 0.6) &&
                write_tone_wav(path / "song.mode1.flac", 0.6);
        }, true) ||
        !expect_rejected("ForbiddenMode0", [](const auto& path) {
            return write_tone_wav(path / "song.wav", 0.6) && write_tone_wav(path / "song.mode0.wav", 0.6);
        }, true) ||
        !expect_rejected("Mode1Duration", [](const auto& path) {
            return write_tone_wav(path / "song.wav", 0.6) &&
                write_tone_wav(path / "song.mode1.wav", 0.5) &&
                write_explicit_song_json(path / "song.json", "Mismatch", false);
        }) ||
        !expect_rejected("Mode2Duration", [](const auto& path) {
            return write_tone_wav(path / "song.wav", 0.6) &&
                write_tone_wav(path / "song.mode2.wav", 0.7) &&
                write_explicit_song_json(path / "song.json", "Mismatch", false);
        })) {
        return fail("ambiguous, missing, forbidden, or mismatched role input was published");
    }
    return 0;
}

int test_gain_envelope_cache_and_hca(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "EnvelopeFixture";
    std::filesystem::create_directories(song_directory);
    if (!write_tone_wav(song_directory / "song.wav", 2.1) ||
        !write_envelope_song_json(song_directory / "song.json", 6.0)) {
        return fail("failed to write gain-envelope fixture");
    }

    ff7rp::pipeline::LoadedSong generated;
    auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &generated);
    if (!status.ok()) return fail("gain-envelope fixture generation failed: " + status.message);
    const std::string first_manifest = read_text(ff7rp::pipeline::cache_manifest_path(song_directory.string()));
    const std::string first_key = manifest_line(first_manifest, "cache_key");
    const std::vector<std::uint8_t> first_mabf = read_binary(generated.cache_sidecar_path);
    if (generated.loaded_from_runtime_cache || generated.audio.frame_count() != 100800 ||
        generated.audio.source_frame_count != 100800 || generated.config.gain_envelope.size() != 2 ||
        !generated.gain_envelope_applied || generated.gain_envelope_point_count != 2 ||
        generated.gain_envelope_max_gain_db != 6.0 || generated.gain_envelope_min_gain_db != 0.0 ||
        !generated.loudness_gain_applied || !generated.loudness_limiter_engaged ||
        first_manifest.find("version=ff7rpianosongs.pipeline.v48") == std::string::npos ||
        first_manifest.find("gain_envelope_present=1") == std::string::npos ||
        first_manifest.find("gain_envelope_points=2") == std::string::npos ||
        first_manifest.find("gain_envelope_interpolation=linear_amplitude") == std::string::npos ||
        first_manifest.find("logical_pcm_frames=100800") == std::string::npos ||
        first_manifest.find("resident_pcm_frames=100800") == std::string::npos ||
        first_manifest.find("hca_frames=" + std::to_string(generated.mabf_metadata.hca_frame_count)) ==
            std::string::npos ||
        first_manifest.find("loudness_gain_applied=1") == std::string::npos ||
        first_manifest.find("loudness_limiter_engaged=1") == std::string::npos) {
        return fail("gain envelope did not preserve frames/config/diagnostics through generation");
    }

    ff7rp::pipeline::LoadedSong cached;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &cached);
    if (!status.ok() || !cached.loaded_from_runtime_cache || cached.config.gain_envelope.size() != 2 ||
        cached.config.gain_envelope[0].gain_db != 6.0 || !cached.gain_envelope_applied ||
        cached.gain_envelope_point_count != 2 || cached.gain_envelope_max_gain_db != 6.0 ||
        cached.gain_envelope_min_gain_db != 0.0 || !cached.loudness_gain_applied ||
        !cached.loudness_limiter_engaged || cached.audio.source_frame_count != 100800 ||
        cached.audio.frame_count() != 0) {
        return fail("gain envelope config/diagnostics did not survive runtime-cache round trip");
    }

    const std::filesystem::path runtime_path = song_directory / ".cache" / "runtime.bin";
    const std::vector<std::uint8_t> valid_runtime = read_binary(runtime_path);
    auto plausible_lufs_runtime = valid_runtime;
    const std::size_t diagnostics_offset = runtime_diagnostics_offset(valid_runtime);
    write_double_le(&plausible_lufs_runtime, diagnostics_offset + 34u,
        generated.loudness_output_lufs + 0.25);
    rechecksum_runtime_cache(&plausible_lufs_runtime);
    if (!write_bytes(runtime_path, plausible_lufs_runtime)) {
        return fail("failed to write plausible limiter-active output LUFS mutation");
    }
    ff7rp::pipeline::LoadedSong rebuilt_lufs;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &rebuilt_lufs);
    if (!status.ok() || rebuilt_lufs.loaded_from_runtime_cache || read_binary(runtime_path) != valid_runtime ||
        read_binary(generated.cache_sidecar_path) != first_mabf ||
        read_text(generated.cache_manifest_path) != first_manifest) {
        return fail("plausible limiter-active output LUFS mutation was authorized without manifest equality");
    }

    if (!write_envelope_song_json(song_directory / "song.json", 3.0)) {
        return fail("failed to modify gain-envelope cache-key fixture");
    }
    ff7rp::pipeline::LoadedSong invalidated;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &invalidated);
    const std::string second_manifest = read_text(ff7rp::pipeline::cache_manifest_path(song_directory.string()));
    const std::vector<std::uint8_t> second_mabf = read_binary(invalidated.cache_sidecar_path);
    if (!status.ok() || invalidated.loaded_from_runtime_cache || invalidated.config.gain_envelope.size() != 2 ||
        invalidated.config.gain_envelope[0].gain_db != 3.0 ||
        manifest_line(second_manifest, "cache_key") == first_key || second_mabf == first_mabf) {
        return fail("gain-envelope JSON change did not invalidate cache and reach encoded MABF audio");
    }
    return 0;
}

int test_authored_profiles(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "AuthoredProfiles";
    std::filesystem::create_directories(song_directory);
    if (!write_silent_wav(song_directory / "song.wav", 5.0) ||
        !write_authored_profiles_song_json(song_directory / "song.json") ||
        !write_bytes(song_directory / "song.mid", {0xffu, 0x00u})) {
        return fail("failed to create authored-profile fixture");
    }
    ff7rp::pipeline::LoadedSong generated;
    auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &generated);
    if (!status.ok() || generated.loaded_from_runtime_cache || generated.chart_from_midi ||
        !generated.midi_source_path.empty() || generated.difficulty_profiles.size() != 2u ||
        generated.config.difficulty != 0 || generated.chart.notes.size() != 4u ||
        !configs_equal(generated.config, generated.difficulty_profiles.front().config) ||
        !charts_equal(generated.chart, generated.difficulty_profiles.front().chart)) {
        return fail("authored profiles did not publish first-profile root invariants: " + status.message);
    }
    const auto& first = generated.difficulty_profiles[0];
    const auto& second = generated.difficulty_profiles[1];
    if (first.config.difficulty != 0 || second.config.difficulty != std::numeric_limits<int>::max() ||
        first.chart.notes.size() != 4u || second.chart.notes.size() != 20u ||
        first.config.score_thresholds == second.config.score_thresholds ||
        first.config.mode_change_combo_counts == second.config.mode_change_combo_counts ||
        second.config.mode_change_combo_counts != std::vector<int>({10, 20}) ||
        first.diagnostics.selected_actions != 4u || second.diagnostics.selected_actions != 20u ||
        first.diagnostics.scheduled_rows != 4u || second.diagnostics.scheduled_rows != 20u ||
        second.diagnostics.retained_actions != 3u || second.diagnostics.added_actions != 16u ||
        second.diagnostics.removed_actions != 0u || second.diagnostics.replaced_actions != 1u ||
        second.diagnostics.overlap_ratio != 0.75 || second.diagnostics.nested_from_previous) {
        return fail("authored profile metadata or deterministic comparison diagnostics changed");
    }
    const std::string resolved = read_text(
        ff7rp::pipeline::resolved_song_json_path(song_directory.string()));
    ff7rp::pipeline::ParsedSongSource parsed_resolved;
    if (!ff7rp::pipeline::parse_song_json_string(resolved, &parsed_resolved).ok() ||
        parsed_resolved.authored_profiles.size() != 2u ||
        parsed_resolved.authored_profiles[0].difficulty != 0 ||
        parsed_resolved.authored_profiles[1].difficulty != std::numeric_limits<int>::max() ||
        parsed_resolved.authored_profiles[0].notes.size() != 4u ||
        parsed_resolved.authored_profiles[1].notes.size() != 20u) {
        return fail("authored sparse profile resolved output changed source shape or labels");
    }
    ff7rp::pipeline::LoadedSong cached;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &cached);
    if (!status.ok() || !cached.loaded_from_runtime_cache || cached.difficulty_profiles.size() != 2u ||
        !configs_equal(cached.config, generated.config) || !charts_equal(cached.chart, generated.chart) ||
        read_text(ff7rp::pipeline::resolved_song_json_path(song_directory.string())) != resolved) {
        return fail("authored profiles did not survive runtime-cache reuse unchanged");
    }
    for (std::size_t index = 0; index < generated.difficulty_profiles.size(); ++index) {
        if (!configs_equal(cached.difficulty_profiles[index].config,
                generated.difficulty_profiles[index].config) ||
            !charts_equal(cached.difficulty_profiles[index].chart,
                generated.difficulty_profiles[index].chart) ||
            !diagnostics_equal(cached.difficulty_profiles[index].diagnostics,
                generated.difficulty_profiles[index].diagnostics)) {
            return fail("authored profile cache round trip changed profile semantics");
        }
    }
    return 0;
}

int test_resolved_song_output(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "ResolvedSongOutput";
    std::filesystem::create_directories(song_directory);
    const auto write_source = [&](const std::string& title) {
        const std::string json =
            "{\n"
            "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
            "  \"title\": \"" + title + "\",\n"
            "  \"bpm\": 120,\n"
            "  \"loudness_normalization\": false,\n"
            "  \"notes\": [\n"
            "    {\"beat\":0,\"duration_beats\":1,\"pitch\":\"C4\",\"chord_id\":\"pca_C\","
            "\"group_index\":7,\"monotone_variant\":\"alternate\",\"ignore_sound\":[\"En2\"],"
            "\"monotone_note_value\":\"whole\",\"chord_note_value\":\"dotted_eighth\"},\n"
            "    {\"beat\":1,\"duration_beats\":0.5,\"pitch\":\"D4\",\"group_index\":7}\n"
            "  ]\n"
            "}\n";
        return write_bytes(song_directory / "song.json",
            std::vector<std::uint8_t>(json.begin(), json.end()));
    };
    if (!write_silent_wav(song_directory / "song.wav", 3.0) || !write_source("Resolved One")) {
        return fail("failed to create resolved-song repository fixture");
    }

    ff7rp::pipeline::LoadedSong cold;
    auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &cold);
    const std::filesystem::path resolved =
        ff7rp::pipeline::resolved_song_json_path(song_directory.string());
    const std::string cold_json = read_text(resolved);
    ff7rp::pipeline::ParsedSongSource parsed;
    if (!status.ok() || cold.loaded_from_runtime_cache || cold_json.empty() ||
        !ff7rp::pipeline::parse_song_json_string(cold_json, &parsed).ok() ||
        !parsed.authored_profiles.empty() || parsed.config.title != "Resolved One" ||
        parsed.config.notes.size() != 2u || parsed.config.notes[0].group_index != 7u ||
        parsed.config.notes[0].chord_id != "pca_C" ||
        parsed.config.notes[0].ignore_sound_pitches != std::vector<std::string>{"En2"} ||
        parsed.config.notes[0].monotone_note_value !=
            ff7rp::pipeline::NoteValueOverride{{0, 0}, true} ||
        parsed.config.notes[0].chord_note_value !=
            ff7rp::pipeline::NoteValueOverride{{3, 1}, true}) {
        return fail("cold resolved-song output was absent or not source-compatible");
    }
    const std::uint64_t cold_key = cold.cache_key;

    std::filesystem::remove(resolved);
    ff7rp::pipeline::LoadedSong regenerated;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &regenerated);
    if (!status.ok() || !regenerated.loaded_from_runtime_cache || regenerated.cache_key != cold_key ||
        read_text(resolved) != cold_json) {
        return fail("warm cache did not recreate missing deterministic resolved-song output");
    }

    const std::filesystem::path obstruction = resolved.string() + ".tmp";
    std::filesystem::create_directory(obstruction);
    std::string trace;
    ff7rp::pipeline::LoadedSong obstructed;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &obstructed,
        [&](const char* stage) {
            if (!trace.empty()) trace += ',';
            trace += stage;
        });
    if (!status.ok() || !obstructed.loaded_from_runtime_cache ||
        trace.find("resolved_song_write_failed") == std::string::npos ||
        read_text(resolved) != cold_json ||
        std::filesystem::exists(ff7rp::pipeline::cache_last_error_path(song_directory.string()))) {
        return fail("resolved-song write obstruction rejected the song or damaged prior output");
    }
    ff7rp::pipeline::LoadedSong throwing_trace;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &throwing_trace,
        [](const char* stage) {
            if (std::string_view(stage) == "resolved_song_write_failed") {
                throw std::runtime_error("test-only resolved output trace failure");
            }
        });
    if (!status.ok() || !throwing_trace.loaded_from_runtime_cache ||
        read_text(resolved) != cold_json ||
        std::filesystem::exists(ff7rp::pipeline::cache_last_error_path(song_directory.string()))) {
        return fail("resolved-song failure reporting escaped best-effort publication");
    }
    std::filesystem::remove_all(obstruction);

    if (!write_source("Resolved Two")) return fail("failed to update resolved-song source fixture");
    ff7rp::pipeline::LoadedSong changed;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &changed);
    const std::string changed_json = read_text(resolved);
    if (!status.ok() || changed.loaded_from_runtime_cache || changed.cache_key == cold_key ||
        changed_json == cold_json ||
        !ff7rp::pipeline::parse_song_json_string(changed_json, &parsed).ok() ||
        parsed.config.title != "Resolved Two") {
        return fail("source change did not atomically refresh stale resolved-song output");
    }

    const std::string malformed_title("Invalid \xc0\xaf", 10);
    if (!write_source(malformed_title)) return fail("failed to write malformed-UTF-8 source fixture");
    trace.clear();
    ff7rp::pipeline::LoadedSong invalid_utf8;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &invalid_utf8,
        [&](const char* stage) {
            if (!trace.empty()) trace += ',';
            trace += stage;
        });
    if (!status.ok() || invalid_utf8.loaded_from_runtime_cache ||
        trace.find("resolved_song_render_failed") == std::string::npos ||
        read_text(resolved) != changed_json ||
        std::filesystem::exists(ff7rp::pipeline::cache_last_error_path(song_directory.string()))) {
        return fail("resolved-song UTF-8 render failure escaped cold best-effort publication");
    }
    return 0;
}

int test_authored_chord_voicings(const std::filesystem::path& root) {
    using namespace ff7rp::pipeline;
    const auto directory = root / "AuthoredChordVoicings";
    std::filesystem::create_directories(directory);
    const auto write_source = [&](const std::string& slots) {
        const std::string json = R"({"schema":"v2","title":"Voicing","bpm":120,"loudness_normalization":false,"chord_voicings":{"pca_C":)" + slots +
            R"(},"profiles":[{"difficulty":1,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["En3"]}]},{"difficulty":3,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["En3"]},{"beat":1,"duration_beats":1,"pitch":"C4"}]}]})";
        return write_bytes(directory / "song.json", {json.begin(), json.end()});
    };
    if (!write_source(R"(["Cn3","En3","Gn3"])") || !write_silent_wav(directory / "song.wav", 3.0) ||
        !write_bytes(directory / "song.mid", {0xffu, 0x00u}))
        return fail("failed to create authored voicing fixture");
    LoadedSong cold;
    auto status = load_song_directory(directory.string(), &cold);
    if (!selected_native_asset_capabilities().has_verified_authored_chord_voicing())
        return !status.ok() && status.message.find("verified exact 1.005") != std::string::npos
            ? 0 : fail("unsupported catalog accepted authored voicing repository source");
    const std::vector<ChordVoicing> expected{{"pca_C", {"Cn3", "En3", "Gn3"}}};
    if (!status.ok() || cold.loaded_from_runtime_cache || cold.chart_from_midi || !cold.midi_source_path.empty() ||
        cold.config.chord_voicings != expected || cold.difficulty_profiles.size() != 2u)
        return fail("authored voicing cold load failed: " + status.message);
    for (const auto& profile : cold.difficulty_profiles)
        if (profile.config.chord_voicings != expected ||
            profile.chart.notes.front().ignore_sound_ids != std::array<std::string, 3>{"En3", "", ""})
            return fail("authored profile lost shared effective voicing/filter");
    const auto resolved = read_text(resolved_song_json_path(directory.string()));
    ParsedSongSource parsed;
    if (!parse_song_json_string(resolved, &parsed).ok() || parsed.config.chord_voicings != expected ||
        parsed.authored_profiles.size() != 2u) return fail("complete repository export lost authored voicing");
    LoadedSong warm;
    status = load_song_directory(directory.string(), &warm);
    if (!status.ok() || !warm.loaded_from_runtime_cache || warm.config.chord_voicings != expected ||
        warm.difficulty_profiles.back().config.chord_voicings != expected ||
        read_text(resolved_song_json_path(directory.string())) != resolved)
        return fail("voicing cache reuse changed source/profile/export semantics: " + status.message);
    // Ordered velocity slots are semantic even when the row/event plan is identical.
    if (!write_source(R"(["En3","Cn3","Gn3"])") ) return fail("failed to reorder authored sound slots");
    LoadedSong reordered;
    status = load_song_directory(directory.string(), &reordered);
    if (!status.ok() || reordered.loaded_from_runtime_cache || reordered.cache_key == cold.cache_key ||
        reordered.config.chord_voicings.front().sound_ids != std::vector<std::string>{"En3", "Cn3", "Gn3"} ||
        !charts_equal(reordered.chart, cold.chart))
        return fail("sound-slot order failed cache invalidation or changed physical chart rows");
    if (!write_source(R"(["Cn2","En2","Gn2"])") ) return fail("failed to write invalid effective filter fixture");
    LoadedSong invalid;
    if (load_song_directory(directory.string(), &invalid).ok())
        return fail("cached authored voicing bypassed changed effective IgnoreSound validation");
    return 0;
}

int test_non_limiting_limiter_manifest_binding(const std::filesystem::path& root) {
    const std::filesystem::path song_directory = root / "NormalizedNonLimitingFixture";
    std::filesystem::create_directories(song_directory);
    if (!write_tone_wav(song_directory / "song.wav", 2.1) ||
        !write_normalized_song_json(song_directory / "song.json")) {
        return fail("failed to write normalized non-limiting fixture");
    }
    ff7rp::pipeline::LoadedSong generated;
    auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &generated);
    if (!status.ok() || !generated.loudness_normalized || !generated.loudness_gain_applied ||
        generated.loudness_limiter_engaged ||
        generated.loudness_output_peak_dbfs > generated.config.loudness_peak_ceiling_dbfs) {
        return fail("normalized fixture did not establish a valid gain-only non-limiting route");
    }
    ff7rp::pipeline::LoadedSong cached;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &cached);
    if (!status.ok() || !cached.loaded_from_runtime_cache || cached.loudness_limiter_engaged) {
        return fail("normalized non-limiting fixture did not warm-load canonically");
    }

    const std::filesystem::path runtime_path = song_directory / ".cache" / "runtime.bin";
    const std::vector<std::uint8_t> valid_runtime = read_binary(runtime_path);
    const std::vector<std::uint8_t> valid_mabf = read_binary(generated.cache_sidecar_path);
    const std::string valid_manifest = read_text(generated.cache_manifest_path);
    auto plausible_limiter_runtime = valid_runtime;
    const std::size_t diagnostics_offset = runtime_diagnostics_offset(valid_runtime);
    plausible_limiter_runtime[diagnostics_offset + 25u] = 1u;
    rechecksum_runtime_cache(&plausible_limiter_runtime);
    if (!write_bytes(runtime_path, plausible_limiter_runtime)) {
        return fail("failed to write plausible false-to-true limiter mutation");
    }
    ff7rp::pipeline::LoadedSong rebuilt;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &rebuilt);
    if (!status.ok() || rebuilt.loaded_from_runtime_cache || read_binary(runtime_path) != valid_runtime ||
        read_binary(generated.cache_sidecar_path) != valid_mabf ||
        read_text(generated.cache_manifest_path) != valid_manifest) {
        return fail("plausible false-to-true limiter mutation was authorized without manifest equality");
    }
    return 0;
}

int test_midi_pitch_exclusions(const std::filesystem::path& root) {
    for (const bool extended : {false, true}) {
        ff7rp::pipeline::configure_chart_row_limit(extended, extended);
        const auto directory = root / (extended ? "PitchExclusionExtended" : "PitchExclusionOrdinary");
        std::filesystem::create_directories(directory);
        MidiTrack clean;
        for (int index = 0; index < 16; ++index) {
            add_note(&clean, 1920 + index * 960, 180, 60 + index % 5, 100);
        }
        MidiTrack mixed = clean;
        for (int index = 0; index < 16; ++index) {
            add_note(&mixed, 1920 + index * 960 + 240, 120, 23, 100);
            add_note(&mixed, 1920 + index * 960 + 480, 120, 97, 100);
        }
        if (!write_bytes(directory / "song.mid", build_midi(mixed)) ||
            !write_tone_wav(directory / "song.wav", 20.0, 2048.0) ||
            !write_synthetic_profile_song_json(directory / "song.json")) {
            return fail("could not write bounded pitch-exclusion repository fixture");
        }
        std::vector<std::string> warnings;
        const auto trace = [&](const char* stage) {
            if (std::string_view(stage).starts_with("midi_source_warning:")) warnings.emplace_back(stage);
        };
        ff7rp::pipeline::LoadedSong cold;
        auto status = ff7rp::pipeline::load_song_directory(directory.string(), &cold, trace);
        if (!status.ok() || cold.difficulty_profiles.empty() || cold.loaded_from_runtime_cache ||
            warnings.size() != 1 || warnings.front().find(
                "excluded=32 total_linked_pitched_attacks=48 excluded_midi_range=23..97") == std::string::npos ||
            warnings.front().find("remaining profiles still require validation") == std::string::npos) {
            return fail("usable mixed source failed or lost truthful exclusion warning: " + status.message);
        }
        ff7rp::pipeline::LoadedSong warm;
        warnings.clear();
        status = ff7rp::pipeline::load_song_directory(directory.string(), &warm, trace);
        if (!status.ok() || !warm.loaded_from_runtime_cache || !warnings.empty() ||
            warm.difficulty_profiles.size() != cold.difficulty_profiles.size()) {
            return fail("pitch-exclusion cache failed or pretended to reanalyze the source on a cache hit");
        }
        // Removing only the excluded attacks changes input identity, not any
        // permitted chart row, slot, source witness, or visible difficulty.
        if (!write_bytes(directory / "song.mid", build_midi(clean))) return fail("could not write clean range source");
        ff7rp::pipeline::LoadedSong supported;
        status = ff7rp::pipeline::load_song_directory(directory.string(), &supported, trace);
        if (!status.ok() || supported.loaded_from_runtime_cache || supported.cache_key == cold.cache_key ||
            !warnings.empty() || supported.difficulty_profiles.size() != cold.difficulty_profiles.size()) {
            return fail("supported-source rewrite reused stale identity or changed visible profile count");
        }
        for (std::size_t index = 0; index < cold.difficulty_profiles.size(); ++index) {
            if (!configs_equal(cold.difficulty_profiles[index].config, warm.difficulty_profiles[index].config) ||
                !charts_equal(cold.difficulty_profiles[index].chart, warm.difficulty_profiles[index].chart) ||
                !configs_equal(cold.difficulty_profiles[index].config, supported.difficulty_profiles[index].config) ||
                !charts_equal(cold.difficulty_profiles[index].chart, supported.difficulty_profiles[index].chart)) {
                return fail("excluded attacks changed permitted profile semantics or warm-cache representation");
            }
        }
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    return 0;
}

int test_synthetic_reviewed_profiles_for_policy(const std::filesystem::path& root, bool extended) {
    ff7rp::pipeline::configure_chart_row_limit(extended, extended);
    const std::filesystem::path song_directory = root / "SyntheticReviewedProfiles";
    std::filesystem::create_directories(song_directory);
    MidiTrack source_track;
    for (int index = 0; index < 480; ++index) {
        add_note(&source_track, 1920 + index * 240, 180, 60 + (index * 5) % 17, 88 + index % 32);
    }
    if (!write_bytes(song_directory / "song.mid", build_midi(std::move(source_track))) ||
        !write_tone_wav(song_directory / "song.wav", 123.0, 2048.0) ||
        !write_synthetic_profile_song_json(song_directory / "song.json")) {
        return fail("failed to create synthetic reviewed-profile source");
    }

    ff7rp::pipeline::LoadedSong song;
    const auto status = ff7rp::pipeline::load_song_directory(song_directory.string(), &song);
    if (!status.ok() || song.loaded_from_runtime_cache) {
        return fail("synthetic reviewed-profile cold generation failed: " + status.message);
    }
    // The original positional {1,2,4} and stream snapshots date to 2d9f265.
    // 5fcf529 deliberately changed selection to independent ungrouped profiles
    // and restored authoritative source timing, without updating this fixture.
    // Keep the reviewed labels/routes, but do not require Lv.3 to be omitted:
    // its presence also changes Lv.4's preceding-profile growth cap.
    constexpr std::array<int, 3> reviewed_difficulties{1, 2, 4};
    constexpr std::array<const char*, 3> reviewed_routes{"Journey", "Tifa", "Difficult"};
    for (const int difficulty : reviewed_difficulties) {
        if (std::none_of(song.difficulty_profiles.begin(), song.difficulty_profiles.end(),
                [&](const auto& profile) { return profile.config.difficulty == difficulty; })) {
            return fail("synthetic fixture lost reviewed Lv." + std::to_string(difficulty));
        }
    }

    struct FrameActivity {
        std::array<std::size_t, 5> windows{};
        std::array<std::size_t, 2> stream_actions{};
        std::array<long long, 2> stream_frames{};
    };
    // Independent integer-frame oracle: half-open windows; inclusive adjacent
    // stream gaps. Count and duration maxima need not describe the same run.
    const auto measure_frames = [](const std::vector<long long>& frames) {
        FrameActivity measured;
        constexpr std::array<long long, 5> widths{30, 60, 120, 300, 600};
        for (std::size_t begin = 0; begin < frames.size(); ++begin) {
            for (std::size_t window = 0; window < widths.size(); ++window) {
                const auto count = static_cast<std::size_t>(std::count_if(
                    frames.begin() + begin, frames.end(), [&](const long long frame) {
                        return frame - frames[begin] < widths[window];
                    }));
                measured.windows[window] = std::max(measured.windows[window], count);
            }
            for (std::size_t stream = 0; stream < 2; ++stream) {
                std::size_t end = begin;
                while (end + 1 < frames.size() &&
                    frames[end + 1] - frames[end] <= (stream == 0 ? 15 : 30)) ++end;
                measured.stream_actions[stream] = std::max(measured.stream_actions[stream], end - begin + 1);
                measured.stream_frames[stream] = std::max(measured.stream_frames[stream], frames[end] - frames[begin]);
            }
        }
        return measured;
    };
    const auto boundary = measure_frames({0, 15, 45, 76});
    if (boundary.windows != std::array<std::size_t, 5>{2, 3, 4, 4, 4} ||
        boundary.stream_actions != std::array<std::size_t, 2>{2, 3} ||
        boundary.stream_frames != std::array<long long, 2>{15, 45}) {
        return fail("independent frame oracle lost half-open windows or inclusive stream gaps");
    }
    ff7rp::pipeline::WavAudio source_audio;
    if (!ff7rp::pipeline::read_audio_file((song_directory / "song.wav").string(), &source_audio).ok()) {
        return fail("could not independently decode synthetic source for deterministic regeneration");
    }
    std::vector<ff7rp::pipeline::Note> baseline;
    int previous_difficulty = 0;
    for (std::size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        const auto& profile = song.difficulty_profiles[index];
        const int difficulty = profile.config.difficulty;
        if (difficulty <= previous_difficulty || difficulty > 6 ||
            profile.diagnostics.selected_actions != profile.chart.notes.size() ||
            profile.diagnostics.scheduled_rows != profile.chart.notes.size() ||
            (difficulty == 1 && profile.chart.notes.size() != 157)) {
            return fail("synthetic reviewed Lv." + std::to_string(difficulty) +
                " label or row count changed (difficulty=" +
                std::to_string(profile.config.difficulty) + ", chart=" +
                std::to_string(profile.chart.notes.size()) + ", selected=" +
                std::to_string(profile.diagnostics.selected_actions) + ", scheduled=" +
                std::to_string(profile.diagnostics.scheduled_rows) + ", target=" +
                std::to_string(profile.diagnostics.target_rows) + ", minimum=" +
                std::to_string(profile.diagnostics.target_minimum_rows) + ", maximum=" +
                std::to_string(profile.diagnostics.target_maximum_rows) + ", route=" +
                profile.diagnostics.satisfied_route_name + ")");
        }
        // The old Lv.2 count 174 was not established by the baseline audit,
        // which stopped at Lv.1 and separately inspected only stream metrics.
        // Bind selection to calibration/source semantics, not that snapshot.
        // 480 quarter-second attacks span exactly 119.75 seconds. Pin the
        // calibrated APM/tolerance and derive the band before its visible cap.
        constexpr std::array<int, 6> apm{78, 90, 104, 120, 138, 158};
        const auto raw_target = static_cast<std::size_t>(std::llround(119.75 * apm[difficulty - 1] / 60.0)) + 1;
        const auto minimum = static_cast<std::size_t>(std::floor(raw_target * 0.94));
        const auto maximum = static_cast<std::size_t>(std::ceil(raw_target * 1.06));
        const std::size_t visible_cap = baseline.empty() ? 480 :
            ff7rp::pipeline::maximum_midi_visible_profile_actions(baseline.size());
        if (profile.diagnostics.target_rows != std::min(raw_target, visible_cap) ||
            profile.diagnostics.target_minimum_rows != std::min(minimum, std::min(raw_target, visible_cap)) ||
            profile.diagnostics.target_maximum_rows != maximum ||
            profile.chart.notes.empty() || profile.chart.notes.size() > 480) {
            return fail("synthetic reviewed Lv." + std::to_string(difficulty) +
                " lost its calibrated soft target or physical source bounds");
        }

        std::vector<ff7rp::pipeline::Note> generated;
        ff7rp::pipeline::MidiChartStats stats;
        const std::size_t maximum_visible_rows = baseline.empty() ? 0 :
            ff7rp::pipeline::maximum_midi_visible_profile_actions(baseline.size());
        const auto generated_status = ff7rp::pipeline::generate_notes_from_midi(
            song.midi_source_path, source_audio, profile.config, &generated, &stats,
            baseline.empty() ? nullptr : &baseline, maximum_visible_rows);
        if (!generated_status.ok() || stats.source_pitch_witness_failures != 0 ||
            stats.scheduled_conflicts != 0) {
            return fail("synthetic reviewed Lv." + std::to_string(difficulty) +
                " gained a source-witness, retiming, or scheduling failure (status=" +
                generated_status.message + ", rows=" + std::to_string(generated.size()) +
                ", witnesses=" + std::to_string(stats.source_pitch_witness_failures) +
                ", conflicts=" + std::to_string(stats.scheduled_conflicts) + ")");
        }

        auto regenerated_config = profile.config;
        regenerated_config.notes = generated;
        ff7rp::pipeline::CompiledChart regenerated_chart;
        if (!configs_equal(regenerated_config, profile.config) ||
            !ff7rp::pipeline::compile_chart(regenerated_config, &regenerated_chart).ok() ||
            !charts_equal(regenerated_chart, profile.chart)) {
            return fail("synthetic profile changed complete source/compiled semantics on regeneration");
        }
        // Do not reconstruct Note from ChartNote: that drops alternate,
        // filtering, voicing, and independent notation metadata.
        const auto& reviewed_notes = profile.config.notes;
        const auto validation = ff7rp::pipeline::validate_midi_difficulty_route(
            reviewed_notes, stats.source_bpm, difficulty);
        const auto reviewed = std::find(reviewed_difficulties.begin(), reviewed_difficulties.end(), difficulty);
        if (!validation.feasible ||
            validation.ratio > validation.margin + 0.000001 ||
            profile.diagnostics.satisfied_route != validation.route_index ||
            profile.diagnostics.satisfied_route_name != validation.metrics.satisfied_route_name ||
            (reviewed != reviewed_difficulties.end() &&
                (validation.route_index != 0 || profile.diagnostics.satisfied_route_name !=
                    reviewed_routes[reviewed - reviewed_difficulties.begin()]))) {
            return fail("synthetic reviewed Lv." + std::to_string(difficulty) +
                " no longer satisfies its reviewed strict route");
        }

        std::vector<long long> frames;
        frames.reserve(reviewed_notes.size());
        for (const auto& note : reviewed_notes) {
            if (note.pitch.empty() || !note.chord_id.empty() || note.group_index != 0) {
                return fail("synthetic reviewed Lv." + std::to_string(difficulty) +
                    " lost its ungrouped source-backed right-hand action");
            }
            const double frame = note.beat * 60.0 / stats.source_bpm * 60.0;
            if (std::fabs(frame - std::round(frame)) > 0.000001) {
                return fail("synthetic reviewed Lv." + std::to_string(difficulty) +
                    " retimed a native-frame action");
            }
            frames.push_back(std::llround(frame));
            const long long ordinal = (frames.back() - 120) / 15;
            constexpr std::array<const char*, 12> names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const int pitch = 60 + static_cast<int>((ordinal * 5) % 17);
            if (ordinal < 0 || ordinal >= 480 || frames.back() != 120 + ordinal * 15 ||
                note.pitch != std::string(names[pitch % 12]) + std::to_string(pitch / 12 - 1) ||
                note.duration_beats != 0.25) {
                return fail("synthetic profile invented a pitch, retimed a source attack, or changed generated duration");
            }
        }
        if (!std::is_sorted(frames.begin(), frames.end()) ||
            std::adjacent_find(frames.begin(), frames.end()) != frames.end() ||
            frames.empty() || frames.back() != 7305) {
            return fail("synthetic reviewed Lv." + std::to_string(difficulty) +
                " changed its unique actions or exact final onset");
        }
        const auto measured = measure_frames(frames);
        if (profile.diagnostics.maximum_window_actions != measured.windows ||
            profile.diagnostics.maximum_quarter_second_stream_actions != measured.stream_actions[0] ||
            std::fabs(profile.diagnostics.maximum_quarter_second_stream_duration - measured.stream_frames[0] / 60.0) > 0.000001 ||
            profile.diagnostics.maximum_half_second_stream_actions != measured.stream_actions[1] ||
            std::fabs(profile.diagnostics.maximum_half_second_stream_duration - measured.stream_frames[1] / 60.0) > 0.000001 ||
            (difficulty == 4 && (std::fabs(validation.margin - 1.05) > 0.000001 || validation.ratio > 1.05))) {
            std::string windows;
            for (const auto count : profile.diagnostics.maximum_window_actions) {
                if (!windows.empty()) windows += ",";
                windows += std::to_string(count);
            }
            return fail("synthetic reviewed Lv." + std::to_string(difficulty) +
                " window, stream, or strict-route ceiling changed (windows=" + windows +
                ", quarter_actions=" + std::to_string(profile.diagnostics.maximum_quarter_second_stream_actions) +
                ", quarter_duration=" + std::to_string(profile.diagnostics.maximum_quarter_second_stream_duration) +
                ", half_actions=" + std::to_string(profile.diagnostics.maximum_half_second_stream_actions) +
                ", half_duration=" + std::to_string(profile.diagnostics.maximum_half_second_stream_duration) +
                ", ratio=" + std::to_string(validation.ratio) +
                ", margin=" + std::to_string(validation.margin) +
                ", published_ratio=" + std::to_string(profile.diagnostics.satisfied_route_ratio) +
                ", published_margin=" + std::to_string(profile.diagnostics.satisfied_route_margin) + ")");
        }
        baseline = std::move(generated);
        previous_difficulty = difficulty;
    }
    const std::string manifest = read_text(song.cache_manifest_path);
    ff7rp::pipeline::LoadedSong warm;
    const auto warm_status = ff7rp::pipeline::load_song_directory(song_directory.string(), &warm);
    if (!warm_status.ok() || !warm.loaded_from_runtime_cache ||
        warm.difficulty_profiles.size() != song.difficulty_profiles.size() ||
        manifest.find("profile_semantic_hashes=") == std::string::npos ||
        manifest.find("config_chart_semantic_hash=") == std::string::npos) {
        return fail("synthetic reviewed profiles lost warm-cache or manifest semantic binding");
    }
    for (std::size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
        if (!configs_equal(song.difficulty_profiles[index].config, warm.difficulty_profiles[index].config) ||
            !charts_equal(song.difficulty_profiles[index].chart, warm.difficulty_profiles[index].chart) ||
            !diagnostics_equal(song.difficulty_profiles[index].diagnostics, warm.difficulty_profiles[index].diagnostics)) {
            return fail("synthetic warm cache changed exact profile semantics or diagnostic witnesses");
        }
    }
    return 0;
}

int test_synthetic_reviewed_profiles(const std::filesystem::path& root) {
    struct PolicyReset {
        ~PolicyReset() { ff7rp::pipeline::configure_chart_row_limit(false, false); }
    } reset;
    for (const bool extended : {false, true}) {
        const auto policy_root = root / (extended ? "SyntheticExtended" : "SyntheticOrdinary");
        if (test_synthetic_reviewed_profiles_for_policy(policy_root, extended) != 0) return 1;
    }
    return 0;
}

int test_synthetic_gain_envelope_integration(const std::filesystem::path& root) {
    // This end-to-end fixture intentionally uses generated WAV bytes. It
    // preserves repository/audio/cache behavior without bundling encoded music;
    // it does not independently qualify MP3 or FLAC decoder compatibility.
    const std::filesystem::path song_directory = root / "SyntheticEnvelopeIntegration";
    std::filesystem::create_directories(song_directory);
    MidiTrack source_track;
    for (int index = 0; index < 240; ++index) {
        add_note(&source_track, 2064 + index * 240, 180, 64 + (index * 7) % 13, 96 + index % 24);
    }
    if (!write_bytes(song_directory / "song.mid", build_midi(std::move(source_track))) ||
        !write_tone_wav(song_directory / "song.wav", 65.5, 2048.0) ||
        !write_synthetic_envelope_song_json(song_directory / "song.json")) {
        return fail("failed to create synthetic gain-envelope integration source");
    }

    ff7rp::pipeline::WavAudio source_audio;
    auto status = ff7rp::pipeline::read_audio_file((song_directory / "song.wav").string(), &source_audio);
    if (!status.ok()) return fail("failed to decode synthetic envelope source: " + status.message);
    ff7rp::pipeline::LoadedSong song;
    status = ff7rp::pipeline::load_song_directory(song_directory.string(), &song);
    if (!status.ok()) return fail("synthetic envelope integration failed: " + status.message);
    if (song.loaded_from_runtime_cache || source_audio.frame_count() != 3144000 ||
        song.audio.frame_count() != source_audio.frame_count() ||
        song.audio.source_frame_count != source_audio.source_frame_count ||
        !song.gain_envelope_applied || song.gain_envelope_point_count != 3 ||
        song.gain_envelope_max_gain_db != 5.0 || song.gain_envelope_min_gain_db != 0.0) {
        return fail("synthetic envelope changed decoded frame duration or lost diagnostics");
    }

    for (const auto [begin, end] : std::array<std::pair<double, double>, 3>{
            std::pair<double, double>{0.0, 5.0}, {5.0, 10.0}, {10.0, 15.0}}) {
        const double rise = window_loudness(song.audio, begin, end) -
            window_loudness(source_audio, begin, end);
        if (!std::isfinite(rise) || rise < 10.0) {
            return fail("synthetic envelope opening window did not materially rise");
        }
    }
    std::array<double, 3> transition_rises{};
    for (std::size_t index = 0; index < transition_rises.size(); ++index) {
        const double begin = 20.0 + index * 5.0;
        transition_rises[index] = window_loudness(song.audio, begin, begin + 5.0) -
            window_loudness(source_audio, begin, begin + 5.0);
    }
    if (!(transition_rises[0] > transition_rises[1] && transition_rises[1] > transition_rises[2]) ||
        transition_rises[0] - transition_rises[1] > 4.0 ||
        transition_rises[1] - transition_rises[2] > 4.0) {
        return fail("synthetic 20-35 second gain transition is not smooth and descending");
    }
    const double output_lufs = ff7rp::pipeline::measure_integrated_loudness(song.audio);
    const double output_peak = ff7rp::pipeline::measure_sample_peak_dbfs(song.audio);
    const bool clipped = std::any_of(song.audio.stereo_samples.begin(), song.audio.stereo_samples.end(),
        [](const float sample) { return !std::isfinite(sample) || std::fabs(sample) >= 1.0f; });
    if (std::fabs(output_lufs - song.config.loudness_target_lufs) > 0.35 ||
        output_peak > song.config.loudness_peak_ceiling_dbfs + 0.01 || clipped) {
        return fail("synthetic envelope final loudness, limiter ceiling, or clipping invariant failed");
    }
    for (const auto& profile : song.difficulty_profiles) {
        if (profile.config.notes.empty()) return fail("synthetic envelope profile lost prompt timing");
        const double first = profile.config.notes.front().beat * 60.0 / profile.config.bpm;
        const double last = profile.config.notes.back().beat * 60.0 / profile.config.bpm;
        const double tail = song.audio.source_duration_seconds() - last;
        const std::string timing = " profile=" + std::to_string(profile.config.difficulty) +
            " first=" + std::to_string(first) + " last=" + std::to_string(last) +
            " tail=" + std::to_string(tail) +
            " audio_duration=" + std::to_string(song.audio.source_duration_seconds());
        // Independent source-backed reduction may skip the first attack (v14
        // selects the second at 2.4s for labels 1-3). It must not retime selected
        // attacks or lose the final source attack and its exact audio tail.
        long long previous_frame = -1;
        for (const auto& note : profile.config.notes) {
            const double frame = note.beat * 60.0 / profile.config.bpm * 60.0;
            if (!std::isfinite(frame) || frame < 129.0 || frame > 3714.0 ||
                std::fabs(frame - std::round(frame)) > 0.000001) {
                return fail("synthetic envelope retimed source onset" + timing);
            }
            const long long source_frame = std::llround(frame);
            const long long ordinal = (source_frame - 129) / 15;
            constexpr std::array<const char*, 12> names{
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const int pitch = 64 + static_cast<int>((ordinal * 7) % 13);
            if (source_frame != 129 + ordinal * 15 || source_frame <= previous_frame ||
                note.pitch != std::string(names[pitch % 12]) + std::to_string(pitch / 12 - 1) ||
                !note.chord_id.empty() || note.group_index != 0 || note.duration_beats != 0.25) {
                return fail("synthetic envelope lost exact source witness at row=" +
                    std::to_string(ordinal) + timing);
            }
            previous_frame = source_frame;
        }
        const auto route = ff7rp::pipeline::validate_midi_difficulty_route(
            profile.config.notes, profile.config.bpm, profile.config.difficulty);
        if (!route.feasible || route.ratio > route.margin + 0.000001) {
            return fail("synthetic envelope lost profile feasibility" + timing);
        }
        if (std::fabs(last - 61.9) > 0.001 ||
            std::fabs(tail - 3.6) > 0.001) {
            return fail("synthetic envelope final prompt or trailing-audio timing changed" + timing);
        }
    }
    if (!song.config.metronome_enabled || song.metronome_beat_count == 0 ||
        song.metronome_downbeat_count == 0 || song.metronome_first_beat_seconds < 0.0 ||
        !(song.metronome_first_beat_seconds < 2.15) ||
        !std::filesystem::is_regular_file(song.cache_sidecar_path)) {
        return fail("synthetic envelope disturbed metronome/MABF payload contracts");
    }
    return 0;
}

int test_dual_action_profile_comparison() {
    ff7rp::pipeline::SongConfig previous;
    previous.notes = {
        {0.0, 1.0, "C4", "ChordA"},
        {1.0, 0.5, "D4", ""},
    };
    ff7rp::pipeline::SongConfig current;
    current.notes = {
        {0.0, 1.0, "C4", "ChordB"},
        {1.0, 0.5, "D4", ""},
        {2.0, 0.5, "", "ChordC"},
    };
    const auto comparison = ff7rp::pipeline::compare_profile_actions(previous, current);
    if (comparison.retained != 2u || comparison.replaced != 1u || comparison.removed != 0u ||
        comparison.added != 1u || std::fabs(comparison.overlap - (2.0 / 3.0)) > 1e-12 || comparison.nested) {
        return fail("dual-action profile replacement accounting is inconsistent");
    }
    previous.notes.push_back(previous.notes.front());
    const auto duplicate_comparison = ff7rp::pipeline::compare_profile_actions(previous, current);
    if (duplicate_comparison.retained != 2u || duplicate_comparison.replaced != 1u ||
        duplicate_comparison.removed != 2u || duplicate_comparison.added != 1u ||
        std::fabs(duplicate_comparison.overlap - 0.4) > 1e-12 || duplicate_comparison.nested) {
        return fail("dual-action profile multiset duplicate accounting is inconsistent");
    }
    return 0;
}

int test_closed_song_json_schema(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "closed-schema.json";
    const std::array<std::string, 4> invalid_documents{
        R"({"schema":"ff7rpianosongs.song.v2","title":"x","metronome":{"enabled":false},"titel":"typo"})",
        R"({"schema":"ff7rpianosongs.song.v2","title":"x","bpm":120,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4","duraton_beats":1}],"metronome":{"enabled":false}})",
        R"({"schema":"ff7rpianosongs.song.v2","title":"x","gain_envelope":[{"time_seconds":0,"gain_db":0,"gain":1}],"metronome":{"enabled":false}})",
        R"({"schema":"ff7rpianosongs.song.v2","title":"x","metronome":{"enable":false}})",
    };
    for (const std::string& document : invalid_documents) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << document;
        out.close();
        ff7rp::pipeline::SongConfig config;
        if (ff7rp::pipeline::load_song_json_file(path.string(), &config).ok()) {
            return fail("closed song JSON schema accepted an unknown key");
        }
    }
    return 0;
}

int test_atomic_default_song_json_creation(const std::filesystem::path& root) {
    const std::filesystem::path directory = root / "AtomicSource";
    std::filesystem::create_directories(directory);
    if (!write_bytes(directory / "song.wav", std::vector<std::uint8_t>{0u})) {
        return fail("failed to create atomic source fixture audio placeholder");
    }
    std::vector<std::thread> creators;
    for (int index = 0; index < 16; ++index) {
        creators.emplace_back([&] {
            ff7rp::pipeline::LoadedSong ignored;
            (void)ff7rp::pipeline::load_song_directory(directory.string(), &ignored);
        });
    }
    for (auto& creator : creators) creator.join();

    ff7rp::pipeline::SongConfig config;
    const std::filesystem::path song_json = directory / "song.json";
    const auto status = ff7rp::pipeline::load_song_json_file(song_json.string(), &config);
    constexpr const char* kExpectedStarter =
        "{\n"
        "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        "  \"title\": \"AtomicSource\",\n"
        "  \"metronome\": { \"enabled\": true, \"level\": 0.12 }\n"
        "}\n";
    if (!status.ok() || config.title != "AtomicSource" || !config.metronome_enabled ||
        read_text(song_json) != kExpectedStarter) {
        return fail("concurrent default song JSON creation did not publish one valid canonical source: " +
            status.message + ", title=" + config.title +
            ", metronome=" + std::to_string(config.metronome_enabled) +
            ", exists=" + std::to_string(std::filesystem::is_regular_file(song_json)));
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().find(".song.json.tmp.") == 0u) {
            return fail("atomic default song JSON creation left a temporary file");
        }
    }
    const std::vector<std::uint8_t> malformed{'{', '\n'};
    if (!write_bytes(song_json, malformed)) return fail("failed to write no-overwrite fixture");
    ff7rp::pipeline::LoadedSong rejected;
    const auto rejected_status = ff7rp::pipeline::load_song_directory(directory.string(), &rejected);
    if (rejected_status.code != ff7rp::pipeline::StatusCode::InvalidJson ||
        read_binary(song_json) != malformed) {
        return fail("existing malformed song JSON was overwritten instead of rejected unchanged");
    }

    constexpr std::string_view kExpectedUnicodeStarter =
        "{\n"
        "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        "  \"title\": \"\xC3\x89" "tude\",\n"
        "  \"metronome\": { \"enabled\": true, \"level\": 0.12 }\n"
        "}\n";
    if (ff7rp::pipeline::render_default_song_json(std::filesystem::path(L"\u00c9tude")) !=
        kExpectedUnicodeStarter) {
        return fail("Unicode default song JSON creation did not preserve exact UTF-8 starter bytes");
    }
    return 0;
}

int test_bounded_discovery_order_and_cache_race(const std::filesystem::path& root) {
    const std::filesystem::path music_root = root / "BoundedDiscovery";
    const auto make_valid = [&](const char* name) {
        const std::filesystem::path directory = music_root / name;
        std::filesystem::create_directories(directory);
        return write_silent_wav(directory / "song.wav", 0.25) &&
            write_explicit_song_json(directory / "song.json", name, false);
    };
    if (!make_valid("AValid") || !make_valid("BValid")) {
        return fail("failed to create bounded discovery valid fixtures");
    }
    const std::filesystem::path bad_json = music_root / "CBadJson";
    const std::filesystem::path bad_audio = music_root / "DBadAudio";
    std::filesystem::create_directories(bad_json);
    std::filesystem::create_directories(bad_audio);
    if (!write_silent_wav(bad_json / "song.wav", 0.25) ||
        !write_bytes(bad_json / "song.json", std::vector<std::uint8_t>{'{'}) ||
        !write_bytes(bad_audio / "song.wav", std::vector<std::uint8_t>{0u}) ||
        !write_explicit_song_json(bad_audio / "song.json", "DBadAudio", false)) {
        return fail("failed to create bounded discovery invalid fixtures");
    }

    const auto discovered = ff7rp::pipeline::discover_songs(music_root.string());
    if (discovered.songs.size() != 2u || discovered.songs[0].id != "AValid" ||
        discovered.songs[1].id != "BValid" || discovered.errors.size() != 2u ||
        discovered.errors[0].code != ff7rp::pipeline::StatusCode::InvalidJson ||
        discovered.errors[1].code != ff7rp::pipeline::StatusCode::InvalidAudio) {
        return fail("bounded discovery did not publish successes and errors in directory order");
    }
    if (discovered.songs[0].chart_policy_generation != discovered.songs[1].chart_policy_generation ||
        discovered.songs[0].chart_policy_identity != discovered.songs[1].chart_policy_identity) {
        return fail("bounded discovery captured inequivalent global policy snapshots");
    }
    if (discovered.songs[0].audio.stereo_samples.empty() ||
        discovered.songs[1].audio.stereo_samples.empty()) {
        return fail("default discovery unexpectedly released decoded PCM");
    }
    const auto first_frames = discovered.songs[0].audio.source_frame_count;
    const auto second_frames = discovered.songs[1].audio.source_frame_count;
    std::error_code compact_cache_error;
    std::filesystem::remove_all(music_root / "AValid" / ".cache", compact_cache_error);
    if (!compact_cache_error) {
        std::filesystem::remove_all(music_root / "BValid" / ".cache", compact_cache_error);
    }
    if (compact_cache_error) return fail("failed to clear compacted cold-discovery fixture caches");
    ff7rp::pipeline::SongDiscoveryHooks compact_hooks;
    compact_hooks.audio_retention =
        ff7rp::pipeline::SongDiscoveryHooks::AudioRetention::ReleaseDecodedPcmAfterLoad;
    bool compact_settlement_valid = true;
    std::size_t compact_settled_songs = 0;
    compact_hooks.on_settled_delta = [&](const ff7rp::pipeline::SongRepositorySettlementDelta& delta) {
        for (const auto& candidate : delta.candidates) {
            if (!candidate.status.ok()) continue;
            ++compact_settled_songs;
            compact_settlement_valid = compact_settlement_valid &&
                candidate.song.audio.stereo_samples.empty() &&
                candidate.song.audio.source_frame_count != 0;
        }
    };
    const auto compacted = ff7rp::pipeline::discover_songs(music_root.string(), compact_hooks);
    if (compacted.songs.size() != 2u || !compacted.songs[0].audio.stereo_samples.empty() ||
        !compacted.songs[1].audio.stereo_samples.empty() ||
        compacted.songs[0].audio.source_frame_count != first_frames ||
        compacted.songs[1].audio.source_frame_count != second_frames ||
        !configs_equal(compacted.songs[0].config, discovered.songs[0].config) ||
        !configs_equal(compacted.songs[1].config, discovered.songs[1].config) ||
        !charts_equal(compacted.songs[0].chart, discovered.songs[0].chart) ||
        !charts_equal(compacted.songs[1].chart, discovered.songs[1].chart) ||
        !compact_settlement_valid || compact_settled_songs != 2u) {
        return fail("opt-in discovery PCM release changed song metadata or chart semantics");
    }

    ff7rp::pipeline::SongDiscoveryHooks setup_failure_hooks;
    setup_failure_hooks.before_setup = [] { throw std::runtime_error("injected setup failure"); };
    const auto setup_failure = ff7rp::pipeline::discover_songs(music_root.string(), setup_failure_hooks);
    if (!setup_failure.songs.empty() || setup_failure.errors.size() != 1u ||
        setup_failure.errors[0].code != ff7rp::pipeline::StatusCode::IoError ||
        setup_failure.errors[0].message != "unexpected song discovery setup failure") {
        return fail("outer discovery setup exception did not return one deterministic fail-closed result");
    }

    ff7rp::pipeline::SongDiscoveryHooks body_failure_hooks;
    body_failure_hooks.before_candidate_load = [](const std::size_t index) {
        if (index == 0u) throw std::runtime_error("injected candidate failure");
    };
    const auto body_failure = ff7rp::pipeline::discover_songs(music_root.string(), body_failure_hooks);
    if (body_failure.songs.size() != 1u || body_failure.songs[0].id != "BValid" ||
        body_failure.errors.size() != 3u ||
        body_failure.errors[0].code != ff7rp::pipeline::StatusCode::IoError ||
        body_failure.errors[0].message != "unexpected song load failure: injected candidate failure" ||
        body_failure.errors[1].code != ff7rp::pipeline::StatusCode::InvalidJson ||
        body_failure.errors[2].code != ff7rp::pipeline::StatusCode::InvalidAudio) {
        return fail("worker-body exception was not captured in deterministic candidate order");
    }

    std::atomic<bool> partial_candidate_entered{false};
    ff7rp::pipeline::SongDiscoveryHooks partial_start_hooks;
    partial_start_hooks.before_candidate_load = [&](const std::size_t) {
        partial_candidate_entered.store(true, std::memory_order_release);
    };
    partial_start_hooks.before_worker_start = [&](const std::size_t worker_index) {
        if (worker_index != 1u) return;
        throw std::runtime_error("injected partial worker construction failure");
    };
    const auto partial_start = ff7rp::pipeline::discover_songs(music_root.string(), partial_start_hooks);
    if (!partial_start.songs.empty() || partial_start.errors.size() != 1u ||
        partial_start.errors[0].code != ff7rp::pipeline::StatusCode::IoError ||
        partial_start.errors[0].message != "failed to start song discovery workers"
        || partial_candidate_entered.load(std::memory_order_acquire)) {
        return fail("partial worker construction did not join and suppress partial publication");
    }

    const std::filesystem::path raced_directory = music_root / "AValid";
    std::error_code remove_error;
    std::filesystem::remove_all(raced_directory / ".cache", remove_error);
    if (remove_error) return fail("failed to clear bounded discovery race fixture cache");
    std::array<ff7rp::pipeline::Status, 2> statuses;
    std::array<std::thread, 2> creators;
    for (std::size_t index = 0; index < creators.size(); ++index) {
        creators[index] = std::thread([&, index] {
            ff7rp::pipeline::LoadedSong ignored;
            statuses[index] = ff7rp::pipeline::load_song_directory(raced_directory.string(), &ignored);
        });
    }
    for (auto& creator : creators) creator.join();
    ff7rp::pipeline::LoadedSong cached;
    const auto cached_status = ff7rp::pipeline::load_song_directory(raced_directory.string(), &cached);
    if (!statuses[0].ok() || !statuses[1].ok() || !cached_status.ok() ||
        !cached.loaded_from_runtime_cache) {
        return fail("concurrent cold cache creation did not leave one valid warm cache: first=" +
            statuses[0].message + ", second=" + statuses[1].message +
            ", warm=" + cached_status.message +
            ", warm_cache=" + std::to_string(cached.loaded_from_runtime_cache));
    }
    for (const auto& entry : std::filesystem::directory_iterator(raced_directory / ".cache")) {
        if (entry.path().filename().string().find(".tmp.") != std::string::npos) {
            return fail("concurrent cold cache creation left a temporary file");
        }
    }
    return 0;
}

int test_partial_lh_cache(const std::filesystem::path& root) {
    ff7rp::pipeline::configure_chart_row_limit(false);
    for (const int scenario : {0, 1, 2, 3}) {
        const bool expect_left = scenario == 1 || scenario == 2;
        const auto directory = root / ("partial-lh-" + std::to_string(scenario));
        std::filesystem::create_directories(directory);
        std::vector<MidiEvent> melody, bass;
        for (int i = 0; i < 8; ++i) {
            const int tick = 1920 + i * 960;
            add_note(&melody, tick, scenario == 2 ? 720 : scenario == 3 ? 480 : 360, 72 + i % 5, 104);
            if (scenario != 0) {
                const int bass_tick = tick + (scenario >= 2 ? 480 : 0);
                const int bass_duration = scenario >= 2 ? 240 : 720;
                add_note(&bass, bass_tick, bass_duration, 36, 82);
                if (i % 2) add_note(&bass, bass_tick, bass_duration, 43, 81);
            }
        }
        if (!write_bytes(directory / "song.mid", build_format_one_midi(melody, bass)) ||
            !write_silent_wav(directory / "song.wav", 12.0) ||
            !write_song_json(directory / "song.json", "Partial LH Cache"))
            return fail("partial LH cache fixture creation failed");
        ff7rp::pipeline::LoadedSong cold, warm;
        auto status = ff7rp::pipeline::load_song_directory(directory.string(), &cold);
        if (!status.ok() || cold.loaded_from_runtime_cache)
            return fail("partial LH cold generation failed: " + status.message);
        const auto runtime = read_binary(directory / ".cache" / "runtime.bin");
        const auto manifest = read_text(cold.cache_manifest_path);
        const auto sidecar = read_binary(cold.cache_sidecar_path);
        status = ff7rp::pipeline::load_song_directory(directory.string(), &warm, {}, false);
        if (!status.ok() || !warm.loaded_from_runtime_cache || cold.cache_key != warm.cache_key ||
            cold.difficulty_profiles.size() != warm.difficulty_profiles.size() || runtime.empty() ||
            runtime != read_binary(directory / ".cache" / "runtime.bin") ||
            manifest != read_text(warm.cache_manifest_path) || sidecar != read_binary(warm.cache_sidecar_path))
            return fail("partial LH warm artifacts rejected or changed: " + status.message);
        std::size_t left = 0;
        for (std::size_t i = 0; i < cold.difficulty_profiles.size(); ++i) {
            const auto& profile = cold.difficulty_profiles[i];
            if (!song_configs_equal(profile.config, warm.difficulty_profiles[i].config) ||
                !charts_equal(profile.chart, warm.difficulty_profiles[i].chart))
                return fail("partial LH warm source/compiled semantics changed");
            for (const auto& note : profile.config.notes) {
                if (note.chord_id.empty()) continue;
                ++left;
                if (note.source_chord_pitches.empty() || note.ignore_sound_pitches.empty())
                    return fail("partial LH warm profile lost source/filtered voicing");
            }
        }
        if (expect_left != (left != 0)) return fail("partial LH cache hand control failed, scenario=" +
            std::to_string(scenario));
    }
    return 0;
}

} // namespace

int main() {
    TemporaryRoot root("ff7rp-repository-selftest");
    const char* filter = std::getenv("FF7RP_SELFTEST_FILTER");
    const auto run = [filter](const char* name, auto test) {
        if (filter != nullptr && *filter != '\0' && std::string(filter) != name) return 0;
        const auto start = std::chrono::steady_clock::now();
        const int result = test();
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "benchmark " << name << " " << milliseconds << " ms\n";
        return result;
    };
    if (run("closed_schema", [&] { return test_closed_song_json_schema(root.path()); }) != 0) return 1;
    if (run("atomic_default_json", [&] { return test_atomic_default_song_json_creation(root.path()); }) != 0) return 1;
    if (run("bounded_discovery", [&] { return test_bounded_discovery_order_and_cache_race(root.path()); }) != 0) return 1;
    if (run("profile_comparison", test_dual_action_profile_comparison) != 0) return 1;
    if (run("authored_profiles", [&] { return test_authored_profiles(root.path()); }) != 0) return 1;
    if (run("chord_voicings", [&] { return test_authored_chord_voicings(root.path()); }) != 0) return 1;
    if (run("resolved_song", [&] { return test_resolved_song_output(root.path()); }) != 0) return 1;
    if (run("growth_cache_manifest", [&] { return test_growth_cache_and_manifest(root.path()); }) != 0) return 1;
    if (run("physical_midi_cache", [&] { return test_physical_midi_cache_round_trip(root.path()); }) != 0) return 1;
    if (run("dense_collision_cache", [&] { return test_dense_collision_cache_round_trip(root.path()); }) != 0) return 1;
    if (run("offline_goldens", [&] { return test_offline_artifact_goldens(root.path()); }) != 0) return 1;
    if (run("partial_lh_cache", [&] { return test_partial_lh_cache(root.path()); }) != 0) return 1;
    if (run("normal_policy", [&] { return test_normal_chart_cache_policy_normalization(root.path()); }) != 0) return 1;
    if (run("extended_diagnostic", [&] { return test_extended_chart_diagnostic_cache_isolation(root.path()); }) != 0) return 1;
    if (run("adaptive_metronome", [&] { return test_adaptive_metronome_modes(root.path()); }) != 0) return 1;
    if (run("midi_metronome_offset", [&] { return test_midi_metronome_offset_source_rule(root.path()); }) != 0) return 1;
    if (run("mode_specific_audio", [&] { return test_mode_specific_audio_sources(root.path()); }) != 0) return 1;
    if (run("gain_envelope_hca", [&] { return test_gain_envelope_cache_and_hca(root.path()); }) != 0) return 1;
    if (run("limiter_manifest", [&] { return test_non_limiting_limiter_manifest_binding(root.path()); }) != 0) return 1;
    if (run("row_limit", [&] { return test_row_limit_omission(root.path()); }) != 0) return 1;
    if (run("synthetic_envelope", [&] { return test_synthetic_gain_envelope_integration(root.path()); }) != 0) return 1;
    if (run("midi_pitch_exclusions", [&] { return test_midi_pitch_exclusions(root.path()); }) != 0) return 1;
    if (run("synthetic_profiles", [&] { return test_synthetic_reviewed_profiles(root.path()); }) != 0) return 1;
    std::cout << "song_repository_selftest ok\n";
    return 0;
}
