#include "runtime_cache_codec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

#include "audio_loudness.h"
#include "cache.h"
#include "chart_compiler.h"
#include "pipeline_limits.h"

namespace ff7rp::pipeline {
namespace {

constexpr std::uint32_t kRuntimeSongSection = 0x474e4f53u;
constexpr std::uint32_t kMaxRuntimeCacheString = 1u << 20;
constexpr std::uint32_t kMaxRuntimeCacheNotes = 8192;
constexpr std::size_t kRuntimeCacheEnvelopeBytes = 40u;

class RuntimeCacheWriter {
public:
    template <typename T>
    bool pod(const T& value) {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
        if constexpr (std::is_floating_point_v<T>) {
            static_assert(sizeof(T) == sizeof(std::uint64_t));
            std::uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return pod(bits);
        } else {
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned bits = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index) {
                payload_.push_back(static_cast<std::uint8_t>((bits >> (index * 8u)) & 0xffu));
            }
            return true;
        }
    }

    bool bytes(const void* data, const std::size_t size) {
        if (size != 0 && !data) return false;
        const auto* begin = static_cast<const std::uint8_t*>(data);
        payload_.insert(payload_.end(), begin, begin + size);
        return true;
    }

    bool string(const std::string& value) {
        if (value.size() > kMaxRuntimeCacheString) return false;
        const auto size = static_cast<std::uint32_t>(value.size());
        return pod(size) && (size == 0 || bytes(value.data(), size));
    }

    std::uint64_t payload_hash(const std::uint64_t seed) const {
        return fnv1a64_append(seed, payload_.data(), payload_.size());
    }

    const std::vector<std::uint8_t>& payload() const { return payload_; }

private:
    std::vector<std::uint8_t> payload_;
};

class RuntimeCacheReader {
public:
    explicit RuntimeCacheReader(const std::span<const std::uint8_t> data) : data_(data) {}

    template <typename T>
    bool pod(T* value) {
        if (!value) return false;
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
        if constexpr (std::is_floating_point_v<T>) {
            static_assert(sizeof(T) == sizeof(std::uint64_t));
            std::uint64_t bits = 0;
            if (!pod(&bits)) return false;
            std::memcpy(value, &bits, sizeof(bits));
            return true;
        } else {
            if (position_ > data_.size() || sizeof(T) > data_.size() - position_) return false;
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = 0;
            for (std::size_t index = 0; index < sizeof(T); ++index) {
                bits |= static_cast<Unsigned>(data_[position_++]) << (index * 8u);
            }
            *value = static_cast<T>(bits);
            return true;
        }
    }

    bool bytes(void* data, const std::size_t size) {
        if ((size != 0 && !data) || position_ > data_.size() || size > data_.size() - position_) return false;
        std::memcpy(data, data_.data() + position_, size);
        position_ += size;
        return true;
    }

    bool string(std::string* value) {
        if (!value) return false;
        std::uint32_t size = 0;
        if (!pod(&size) || size > kMaxRuntimeCacheString) return false;
        value->resize(size);
        return size == 0 || bytes(value->data(), size);
    }

    bool at_end() const { return position_ == data_.size(); }

private:
    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
};

void append_le(std::vector<std::uint8_t>* bytes, std::uint64_t value, const std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        bytes->push_back(static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu));
    }
}

std::uint64_t read_le(const std::span<const std::uint8_t> bytes, const std::size_t offset, const std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8u);
    }
    return value;
}

bool write_int_vector(RuntimeCacheWriter& out, const std::vector<int>& values) {
    if (values.size() > 64) return false;
    if (!out.pod(static_cast<std::uint32_t>(values.size()))) return false;
    for (const int value : values) if (!out.pod(static_cast<std::int32_t>(value))) return false;
    return true;
}

bool read_int_vector(RuntimeCacheReader& in, std::vector<int>* values) {
    if (!values) return false;
    std::uint32_t count = 0;
    if (!in.pod(&count) || count > 64) return false;
    values->clear();
    values->reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::int32_t value = 0;
        if (!in.pod(&value)) return false;
        values->push_back(value);
    }
    return true;
}

bool write_gain_envelope(RuntimeCacheWriter& out, const std::vector<GainEnvelopePoint>& points) {
    if (!validate_gain_envelope(points).ok() || !out.pod(static_cast<std::uint32_t>(points.size()))) return false;
    for (const auto& point : points) if (!out.pod(point.time_seconds) || !out.pod(point.gain_db)) return false;
    return true;
}

bool read_gain_envelope(RuntimeCacheReader& in, std::vector<GainEnvelopePoint>* points) {
    if (!points) return false;
    std::uint32_t count = 0;
    if (!in.pod(&count) || count > kMaximumGainEnvelopePoints) return false;
    points->assign(count, {});
    for (auto& point : *points) if (!in.pod(&point.time_seconds) || !in.pod(&point.gain_db)) return false;
    return validate_gain_envelope(*points).ok();
}

bool write_song_config(RuntimeCacheWriter& out, const SongConfig& config) {
    if (!out.string(config.schema) || !out.string(config.title) || !out.pod(config.bpm) ||
        !out.pod(static_cast<std::int32_t>(config.difficulty)) ||
        !write_int_vector(out, config.score_thresholds) || !write_int_vector(out, config.mode_change_combo_counts) ||
        !out.pod(config.midi_audio_offset_seconds) || !out.pod(config.midi_audio_alignment_seconds) ||
        !out.pod(config.midi_minimum_lead_in_seconds) || !out.pod(config.loudness_target_lufs) ||
        !out.pod(config.loudness_peak_ceiling_dbfs) || !out.pod(config.metronome_level) ||
        !out.pod(config.metronome_beat_zero_offset_seconds) || !write_gain_envelope(out, config.gain_envelope)) return false;
    const std::array<std::uint8_t, 10> flags{
        static_cast<std::uint8_t>(config.loudness_normalization),
        static_cast<std::uint8_t>(config.midi_audio_offset_provided),
        static_cast<std::uint8_t>(config.midi_audio_alignment_provided),
        static_cast<std::uint8_t>(config.bpm_provided),
        static_cast<std::uint8_t>(config.score_thresholds_provided),
        static_cast<std::uint8_t>(config.mode_change_combo_counts_provided),
        static_cast<std::uint8_t>(config.notes_provided),
        static_cast<std::uint8_t>(config.metronome_enabled),
        static_cast<std::uint8_t>(config.metronome_beat_zero_offset_provided),
        static_cast<std::uint8_t>(config.diagnostic_extended_chart_fixture)};
    if (!out.bytes(flags.data(), flags.size()) || config.notes.size() > kMaxRuntimeCacheNotes ||
        !out.pod(static_cast<std::uint32_t>(config.notes.size()))) return false;
    for (const auto& note : config.notes) {
        if (!out.pod(note.beat) || !out.pod(note.duration_beats) || !out.string(note.pitch) || !out.string(note.chord_id)) return false;
    }
    return true;
}

bool read_song_config(RuntimeCacheReader& in, SongConfig* config) {
    if (!config || !in.string(&config->schema) || !in.string(&config->title) || !in.pod(&config->bpm)) return false;
    std::int32_t difficulty = 0;
    if (!in.pod(&difficulty) || !read_int_vector(in, &config->score_thresholds) ||
        !read_int_vector(in, &config->mode_change_combo_counts) ||
        !in.pod(&config->midi_audio_offset_seconds) || !in.pod(&config->midi_audio_alignment_seconds) ||
        !in.pod(&config->midi_minimum_lead_in_seconds) || !in.pod(&config->loudness_target_lufs) ||
        !in.pod(&config->loudness_peak_ceiling_dbfs) || !in.pod(&config->metronome_level) ||
        !in.pod(&config->metronome_beat_zero_offset_seconds) || !read_gain_envelope(in, &config->gain_envelope)) return false;
    config->difficulty = difficulty;
    std::array<std::uint8_t, 10> flags{};
    if (!in.bytes(flags.data(), flags.size()) ||
        std::any_of(flags.begin(), flags.end(), [](const auto value) { return value > 1u; })) return false;
    config->loudness_normalization = flags[0] != 0;
    config->midi_audio_offset_provided = flags[1] != 0;
    config->midi_audio_alignment_provided = flags[2] != 0;
    config->bpm_provided = flags[3] != 0;
    config->score_thresholds_provided = flags[4] != 0;
    config->mode_change_combo_counts_provided = flags[5] != 0;
    config->notes_provided = flags[6] != 0;
    config->metronome_enabled = flags[7] != 0;
    config->metronome_beat_zero_offset_provided = flags[8] != 0;
    config->diagnostic_extended_chart_fixture = flags[9] != 0;
    std::uint32_t count = 0;
    if (!in.pod(&count) || count > kMaxRuntimeCacheNotes) return false;
    config->notes.assign(count, {});
    for (auto& note : config->notes) {
        if (!in.pod(&note.beat) || !in.pod(&note.duration_beats) || !in.string(&note.pitch) || !in.string(&note.chord_id)) return false;
    }
    return true;
}

bool write_compiled_chart(RuntimeCacheWriter& out, const CompiledChart& chart) {
    if (chart.notes.size() > kMaxRuntimeCacheNotes || !out.pod(static_cast<std::uint32_t>(chart.notes.size()))) return false;
    for (const auto& note : chart.notes) {
        if (!out.pod(note.beat) || !out.pod(note.duration_beats) || !out.string(note.pitch) ||
            !out.string(note.time_str) || !out.string(note.monotone_id) || !out.string(note.chord_id)) return false;
        for (const std::int32_t value : std::array<std::int32_t, 4>{note.note_type, note.dot_type,
                note.camera_switch_timing, note.group_index}) if (!out.pod(value)) return false;
    }
    return true;
}

bool read_compiled_chart(RuntimeCacheReader& in, CompiledChart* chart) {
    if (!chart) return false;
    std::uint32_t count = 0;
    if (!in.pod(&count) || count > kMaxRuntimeCacheNotes) return false;
    chart->notes.assign(count, {});
    for (auto& note : chart->notes) {
        if (!in.pod(&note.beat) || !in.pod(&note.duration_beats) || !in.string(&note.pitch) ||
            !in.string(&note.time_str) || !in.string(&note.monotone_id) || !in.string(&note.chord_id)) return false;
        std::array<std::int32_t, 4> values{};
        for (auto& value : values) if (!in.pod(&value)) return false;
        note.note_type = values[0]; note.dot_type = values[1];
        note.camera_switch_timing = values[2]; note.group_index = values[3];
    }
    return true;
}

bool write_diagnostic_chart(RuntimeCacheWriter& out, const DiagnosticChartRetention& diagnostic) {
    if (diagnostic.source_row_count > kExperimentalMaxChartRows ||
        diagnostic.native_prefix_row_count > kMaxChartRows || diagnostic.tail_rows.size() > 8u ||
        !out.pod(static_cast<std::uint32_t>(diagnostic.source_row_count)) ||
        !out.pod(static_cast<std::uint32_t>(diagnostic.native_prefix_row_count)) ||
        !out.pod(static_cast<std::uint32_t>(diagnostic.tail_rows.size())) || !out.pod(diagnostic.descriptor_hash)) return false;
    for (const auto& row : diagnostic.tail_rows) {
        if (!out.pod(static_cast<std::uint32_t>(row.source_row)) || !out.pod(row.source.beat) ||
            !out.pod(row.source.duration_beats) || !out.string(row.source.pitch) || !out.string(row.source.chord_id) ||
            !out.pod(row.compiled.beat) || !out.pod(row.compiled.duration_beats) || !out.string(row.compiled.pitch) ||
            !out.string(row.compiled.time_str) || !out.string(row.compiled.monotone_id) || !out.string(row.compiled.chord_id)) return false;
        for (const std::int32_t value : std::array<std::int32_t, 4>{row.compiled.note_type, row.compiled.dot_type,
                row.compiled.camera_switch_timing, row.compiled.group_index}) if (!out.pod(value)) return false;
    }
    return true;
}

bool read_diagnostic_chart(RuntimeCacheReader& in, DiagnosticChartRetention* diagnostic) {
    if (!diagnostic) return false;
    std::uint32_t source = 0, prefix = 0, tail = 0;
    if (!in.pod(&source) || !in.pod(&prefix) || !in.pod(&tail) || !in.pod(&diagnostic->descriptor_hash) ||
        source > kExperimentalMaxChartRows || prefix > kMaxChartRows || tail > 8u) return false;
    diagnostic->source_row_count = source;
    diagnostic->native_prefix_row_count = prefix;
    diagnostic->tail_rows.assign(tail, {});
    for (auto& row : diagnostic->tail_rows) {
        std::uint32_t source_row = 0;
        if (!in.pod(&source_row) || !in.pod(&row.source.beat) || !in.pod(&row.source.duration_beats) ||
            !in.string(&row.source.pitch) || !in.string(&row.source.chord_id) || !in.pod(&row.compiled.beat) ||
            !in.pod(&row.compiled.duration_beats) || !in.string(&row.compiled.pitch) ||
            !in.string(&row.compiled.time_str) || !in.string(&row.compiled.monotone_id) || !in.string(&row.compiled.chord_id)) return false;
        row.source_row = source_row;
        std::array<std::int32_t, 4> values{};
        for (auto& value : values) if (!in.pod(&value)) return false;
        row.compiled.note_type = values[0]; row.compiled.dot_type = values[1];
        row.compiled.camera_switch_timing = values[2]; row.compiled.group_index = values[3];
    }
    return true;
}

#define FF7RP_DIAGNOSTIC_SIZE_FIELDS(X) \
    X(selected_actions) X(candidate_actions) X(candidate_frames) X(protected_baseline_actions) \
    X(target_rows) X(target_minimum_rows) X(target_maximum_rows) X(target_exclusions) \
    X(local_skill_rejections) X(retained_actions) X(removed_actions) X(replaced_actions) X(added_actions) \
    X(scheduled_rows) X(scheduled_conflicts) X(dropped_actions) X(lead_in_rejections) \
    X(audio_duration_rejections) X(strain_rejections)

bool write_profile_diagnostics(RuntimeCacheWriter& out, const DifficultyProfileDiagnostics& value) {
#define WRITE_SIZE(name) if (!out.pod(static_cast<std::uint64_t>(value.name))) return false;
    FF7RP_DIAGNOSTIC_SIZE_FIELDS(WRITE_SIZE)
#undef WRITE_SIZE
    for (const auto item : value.maximum_window_actions) if (!out.pod(static_cast<std::uint64_t>(item))) return false;
    if (!out.pod(static_cast<std::uint64_t>(value.maximum_quarter_second_stream_actions)) ||
        !out.pod(static_cast<std::uint64_t>(value.maximum_half_second_stream_actions)) ||
        !out.pod(static_cast<std::uint64_t>(value.maximum_jack_run)) ||
        !out.pod(static_cast<std::uint64_t>(value.maximum_reversal_run)) ||
        !out.pod(static_cast<std::uint64_t>(value.maximum_octave_movements_in_five_seconds)) ||
        !out.pod(static_cast<std::uint64_t>(value.maximum_large_reversals_in_five_seconds))) return false;
    if (!out.pod(value.joint_strain_p95) || !out.pod(value.joint_strain_peak)) return false;
    for (const double item : value.maximum_window_begin_seconds) if (!out.pod(item)) return false;
#define WRITE_DOUBLE(name) if (!out.pod(value.name)) return false;
    /* The first two fields and window array were emitted above to preserve the established order. */
    WRITE_DOUBLE(maximum_quarter_second_stream_duration)
    WRITE_DOUBLE(maximum_quarter_second_stream_begin_seconds)
    WRITE_DOUBLE(maximum_half_second_stream_duration)
    WRITE_DOUBLE(maximum_half_second_stream_begin_seconds)
    WRITE_DOUBLE(maximum_jack_begin_seconds) WRITE_DOUBLE(maximum_reversal_begin_seconds)
    WRITE_DOUBLE(rapid_movement_p90) WRITE_DOUBLE(rapid_movement_maximum) WRITE_DOUBLE(octave_movement_rate)
    WRITE_DOUBLE(maximum_octave_window_begin_seconds) WRITE_DOUBLE(maximum_large_reversal_window_begin_seconds)
    WRITE_DOUBLE(right_fatigue_peak) WRITE_DOUBLE(right_fatigue_peak_seconds)
    WRITE_DOUBLE(left_fatigue_peak) WRITE_DOUBLE(left_fatigue_peak_seconds) WRITE_DOUBLE(hand_imbalance)
    WRITE_DOUBLE(rhythm_irregularity_p90) WRITE_DOUBLE(rhythm_irregularity_maximum)
    WRITE_DOUBLE(rhythm_irregularity_peak_seconds) WRITE_DOUBLE(hardest_window_begin_seconds)
    WRITE_DOUBLE(hardest_window_end_seconds) WRITE_DOUBLE(overlap_ratio)
    WRITE_DOUBLE(maximum_quarter_second_stream_actions_end_seconds)
    WRITE_DOUBLE(maximum_quarter_second_stream_duration_begin_seconds)
    WRITE_DOUBLE(maximum_quarter_second_stream_duration_end_seconds)
    WRITE_DOUBLE(maximum_half_second_stream_actions_end_seconds)
    WRITE_DOUBLE(maximum_half_second_stream_duration_begin_seconds)
    WRITE_DOUBLE(maximum_half_second_stream_duration_end_seconds)
    WRITE_DOUBLE(rapid_movement_maximum_seconds) WRITE_DOUBLE(satisfied_route_ratio) WRITE_DOUBLE(satisfied_route_margin)
#undef WRITE_DOUBLE
    if (!out.pod(0.0) || !out.pod(static_cast<std::int32_t>(value.dominant_skill)) ||
        !out.pod(static_cast<std::int32_t>(value.satisfied_route)) || !out.string(value.exposure_decision) ||
        !out.string(value.exposure_reason) || !out.string(value.satisfied_route_name)) return false;
    const std::array<std::uint8_t, 4> flags{static_cast<std::uint8_t>(value.complete),
        static_cast<std::uint8_t>(value.row_limit_exceeded), static_cast<std::uint8_t>(value.nested_from_previous),
        static_cast<std::uint8_t>(value.dominant_skill_is_global)};
    return out.bytes(flags.data(), flags.size());
}

bool read_size(RuntimeCacheReader& in, std::size_t* value) {
    std::uint64_t stored = 0;
    if (!in.pod(&stored) || stored > std::numeric_limits<std::size_t>::max()) return false;
    *value = static_cast<std::size_t>(stored);
    return true;
}

bool read_profile_diagnostics(RuntimeCacheReader& in, DifficultyProfileDiagnostics* value) {
    if (!value) return false;
#define READ_SIZE(name) if (!read_size(in, &value->name)) return false;
    FF7RP_DIAGNOSTIC_SIZE_FIELDS(READ_SIZE)
#undef READ_SIZE
    for (auto& item : value->maximum_window_actions) if (!read_size(in, &item)) return false;
    if (!read_size(in, &value->maximum_quarter_second_stream_actions) ||
        !read_size(in, &value->maximum_half_second_stream_actions) ||
        !read_size(in, &value->maximum_jack_run) || !read_size(in, &value->maximum_reversal_run) ||
        !read_size(in, &value->maximum_octave_movements_in_five_seconds) ||
        !read_size(in, &value->maximum_large_reversals_in_five_seconds)) return false;
    std::array<double, 39> doubles{};
    for (auto& item : doubles) if (!in.pod(&item)) return false;
    std::array<std::int32_t, 2> route{};
    for (auto& item : route) if (!in.pod(&item)) return false;
    std::array<std::uint8_t, 4> flags{};
    if (!in.string(&value->exposure_decision) || !in.string(&value->exposure_reason) ||
        !in.string(&value->satisfied_route_name) || !in.bytes(flags.data(), flags.size()) ||
        !std::all_of(doubles.begin(), doubles.end(), [](const double item) { return std::isfinite(item); }) ||
        std::any_of(flags.begin(), flags.end(), [](const auto item) { return item > 1u; })) return false;
    value->joint_strain_p95 = doubles[0]; value->joint_strain_peak = doubles[1];
    for (std::size_t index = 0; index < 5; ++index) value->maximum_window_begin_seconds[index] = doubles[2 + index];
    value->maximum_quarter_second_stream_duration = doubles[7];
    value->maximum_quarter_second_stream_begin_seconds = doubles[8];
    value->maximum_half_second_stream_duration = doubles[9]; value->maximum_half_second_stream_begin_seconds = doubles[10];
    value->maximum_jack_begin_seconds = doubles[11]; value->maximum_reversal_begin_seconds = doubles[12];
    value->rapid_movement_p90 = doubles[13]; value->rapid_movement_maximum = doubles[14];
    value->octave_movement_rate = doubles[15]; value->maximum_octave_window_begin_seconds = doubles[16];
    value->maximum_large_reversal_window_begin_seconds = doubles[17]; value->right_fatigue_peak = doubles[18];
    value->right_fatigue_peak_seconds = doubles[19]; value->left_fatigue_peak = doubles[20];
    value->left_fatigue_peak_seconds = doubles[21]; value->hand_imbalance = doubles[22];
    value->rhythm_irregularity_p90 = doubles[23]; value->rhythm_irregularity_maximum = doubles[24];
    value->rhythm_irregularity_peak_seconds = doubles[25]; value->hardest_window_begin_seconds = doubles[26];
    value->hardest_window_end_seconds = doubles[27]; value->overlap_ratio = doubles[28];
    value->maximum_quarter_second_stream_actions_end_seconds = doubles[29];
    value->maximum_quarter_second_stream_duration_begin_seconds = doubles[30];
    value->maximum_quarter_second_stream_duration_end_seconds = doubles[31];
    value->maximum_half_second_stream_actions_end_seconds = doubles[32];
    value->maximum_half_second_stream_duration_begin_seconds = doubles[33];
    value->maximum_half_second_stream_duration_end_seconds = doubles[34];
    value->rapid_movement_maximum_seconds = doubles[35]; value->satisfied_route_ratio = doubles[36];
    value->satisfied_route_margin = doubles[37]; value->dominant_skill = route[0]; value->satisfied_route = route[1];
    value->complete = flags[0] != 0; value->row_limit_exceeded = flags[1] != 0;
    value->nested_from_previous = flags[2] != 0; value->dominant_skill_is_global = flags[3] != 0;
    return true;
}

#undef FF7RP_DIAGNOSTIC_SIZE_FIELDS

bool notes_equal(const Note& a, const Note& b) {
    return a.beat == b.beat && a.duration_beats == b.duration_beats && a.pitch == b.pitch && a.chord_id == b.chord_id;
}
bool chart_notes_equal(const ChartNote& a, const ChartNote& b) {
    return a.beat == b.beat && a.duration_beats == b.duration_beats && a.pitch == b.pitch &&
        a.time_str == b.time_str && a.monotone_id == b.monotone_id && a.chord_id == b.chord_id &&
        a.note_type == b.note_type && a.dot_type == b.dot_type &&
        a.camera_switch_timing == b.camera_switch_timing && a.group_index == b.group_index;
}
bool song_configs_equal_impl(const SongConfig& a, const SongConfig& b) {
    if (a.schema != b.schema || a.title != b.title || a.bpm != b.bpm || a.difficulty != b.difficulty ||
        a.score_thresholds != b.score_thresholds || a.mode_change_combo_counts != b.mode_change_combo_counts ||
        a.midi_audio_offset_seconds != b.midi_audio_offset_seconds ||
        a.midi_audio_alignment_seconds != b.midi_audio_alignment_seconds ||
        a.midi_minimum_lead_in_seconds != b.midi_minimum_lead_in_seconds ||
        a.loudness_normalization != b.loudness_normalization || a.loudness_target_lufs != b.loudness_target_lufs ||
        a.loudness_peak_ceiling_dbfs != b.loudness_peak_ceiling_dbfs || a.metronome_enabled != b.metronome_enabled ||
        a.metronome_level != b.metronome_level ||
        a.metronome_beat_zero_offset_seconds != b.metronome_beat_zero_offset_seconds ||
        a.metronome_beat_zero_offset_provided != b.metronome_beat_zero_offset_provided ||
        a.midi_audio_offset_provided != b.midi_audio_offset_provided ||
        a.midi_audio_alignment_provided != b.midi_audio_alignment_provided || a.bpm_provided != b.bpm_provided ||
        a.score_thresholds_provided != b.score_thresholds_provided ||
        a.mode_change_combo_counts_provided != b.mode_change_combo_counts_provided ||
        a.notes_provided != b.notes_provided ||
        a.diagnostic_extended_chart_fixture != b.diagnostic_extended_chart_fixture ||
        a.gain_envelope.size() != b.gain_envelope.size() || a.notes.size() != b.notes.size()) return false;
    for (std::size_t i = 0; i < a.gain_envelope.size(); ++i) {
        if (a.gain_envelope[i].time_seconds != b.gain_envelope[i].time_seconds ||
            a.gain_envelope[i].gain_db != b.gain_envelope[i].gain_db) return false;
    }
    for (std::size_t i = 0; i < a.notes.size(); ++i) if (!notes_equal(a.notes[i], b.notes[i])) return false;
    return true;
}
bool compiled_charts_equal(const CompiledChart& a, const CompiledChart& b) {
    if (a.notes.size() != b.notes.size()) return false;
    for (std::size_t i = 0; i < a.notes.size(); ++i) if (!chart_notes_equal(a.notes[i], b.notes[i])) return false;
    return true;
}

bool valid_config_and_chart(const SongConfig& config, const CompiledChart& chart) {
    if ((config.schema != "ff7rpianosongs.song.v2" && config.schema != "v2") || config.title.empty() ||
        !std::isfinite(config.bpm) || config.bpm < 30.0 || config.bpm > 300.0 || config.difficulty < 0 ||
        !std::isfinite(config.midi_audio_offset_seconds) || !std::isfinite(config.midi_audio_alignment_seconds) ||
        !std::isfinite(config.midi_minimum_lead_in_seconds) || !std::isfinite(config.loudness_target_lufs) ||
        !std::isfinite(config.loudness_peak_ceiling_dbfs) || !std::isfinite(config.metronome_level) ||
        !std::isfinite(config.metronome_beat_zero_offset_seconds) || !validate_gain_envelope(config.gain_envelope).ok() ||
        config.midi_audio_offset_seconds < -1.0 || config.midi_audio_offset_seconds > 1.0 ||
        config.midi_audio_alignment_seconds < -1.0 || config.midi_audio_alignment_seconds > 1.0 ||
        config.midi_minimum_lead_in_seconds < 0.0 || config.midi_minimum_lead_in_seconds > 30.0 ||
        config.loudness_target_lufs < -30.0 || config.loudness_target_lufs > -5.0 ||
        config.loudness_peak_ceiling_dbfs < -6.0 || config.loudness_peak_ceiling_dbfs > 0.0 ||
        config.metronome_level < 0.0 || config.metronome_level > 1.0 ||
        config.metronome_beat_zero_offset_seconds < -30.0 || config.metronome_beat_zero_offset_seconds > 30.0 ||
        config.score_thresholds.empty() || config.mode_change_combo_counts.empty() ||
        !std::is_sorted(config.score_thresholds.begin(), config.score_thresholds.end()) ||
        !std::is_sorted(config.mode_change_combo_counts.begin(), config.mode_change_combo_counts.end()) ||
        std::any_of(config.score_thresholds.begin(), config.score_thresholds.end(), [](int value) { return value < 0; }) ||
        std::any_of(config.mode_change_combo_counts.begin(), config.mode_change_combo_counts.end(), [](int value) { return value < 0; }) ||
        config.notes.size() != chart.notes.size()) return false;
    double previous = -1.0;
    for (std::size_t i = 0; i < config.notes.size(); ++i) {
        const auto& source = config.notes[i];
        const auto& compiled = chart.notes[i];
        if (!std::isfinite(source.beat) || !std::isfinite(source.duration_beats) || source.beat < previous ||
            source.beat < 0.0 || source.duration_beats <= 0.0 || (source.pitch.empty() && source.chord_id.empty()) ||
            compiled.beat != source.beat || compiled.duration_beats != source.duration_beats ||
            compiled.pitch != source.pitch || compiled.chord_id != source.chord_id ||
            compiled.time_str != beat_to_time_str(source.beat, config.bpm) ||
            compiled.note_type != (source.duration_beats >= 2.0 ? 2 : 3) || compiled.dot_type != 0 ||
            compiled.camera_switch_timing != 0 || compiled.group_index != 0) return false;
        std::string monotone;
        if (!source.pitch.empty() && (!pitch_to_monotone_id(source.pitch, &monotone).ok() ||
            compiled.monotone_id != monotone)) return false;
        if (source.pitch.empty() && !compiled.monotone_id.empty()) return false;
        previous = source.beat;
    }
    return true;
}

bool valid_cached_profile(const std::string& song_id, const bool extended, const LoadedDifficultyProfile& profile) {
    if (profile.config.notes.size() > kMaxChartRows || profile.chart.notes.size() > kMaxChartRows ||
        profile.config.notes.size() != profile.chart.notes.size()) return false;
    const auto& diagnostic = profile.diagnostic_chart;
    if (!diagnostic.present()) return !profile.config.diagnostic_extended_chart_fixture;
    if (!extended || !profile.config.diagnostic_extended_chart_fixture || diagnostic.source_row_count != 520u ||
        diagnostic.native_prefix_row_count != kMaxChartRows || diagnostic.tail_rows.size() != 8u ||
        profile.chart.notes.size() != kMaxChartRows) return false;
    for (std::size_t i = 0; i < diagnostic.tail_rows.size(); ++i) {
        if (diagnostic.tail_rows[i].source_row != kMaxChartRows + i) return false;
    }
    return diagnostic.descriptor_hash == diagnostic_descriptor_hash(
        song_id, profile.config.difficulty, profile.chart, diagnostic);
}

std::uint64_t profile_semantic_hash_impl(const LoadedDifficultyProfile& profile) {
    RuntimeCacheWriter out;
    if (!write_song_config(out, profile.config) || !write_compiled_chart(out, profile.chart) ||
        !write_profile_diagnostics(out, profile.diagnostics) || !write_diagnostic_chart(out, profile.diagnostic_chart)) return 0;
    return out.payload_hash(kFnv1a64OffsetBasis ^ 0x70726f66696c6531ull);
}
std::uint64_t omission_semantic_hash_impl(const DifficultyProfileOmission& omission) {
    RuntimeCacheWriter out;
    SongConfig witness;
    witness.notes = omission.witness_notes;
    witness.notes_provided = !witness.notes.empty();
    if (!out.pod(static_cast<std::int32_t>(omission.difficulty)) ||
        !out.pod(static_cast<std::uint64_t>(omission.desired_rows)) || !out.string(omission.reason) ||
        !write_profile_diagnostics(out, omission.diagnostics) || !write_song_config(out, witness)) return 0;
    return out.payload_hash(kFnv1a64OffsetBasis ^ 0x6f6d697373696f6eull);
}

bool write_payload(RuntimeCacheWriter& out, const LoadedSong& song) {
    const auto accepted = static_cast<std::uint32_t>(song.accepted_chart_input_limit);
    const auto published = static_cast<std::uint32_t>(song.published_chart_row_limit);
    if (song.accepted_chart_input_limit != accepted || song.config.notes.size() > kMaxChartRows ||
        song.chart.notes.size() > kMaxChartRows || !out.pod(song.cache_key) || !out.pod(accepted) ||
        !out.pod(published) || !out.pod(static_cast<std::uint8_t>(song.chart_policy_enabled)) ||
        !out.pod(song.chart_policy_generation) || !out.string(song.chart_policy_identity) ||
        !out.pod(song.mabf_metadata.digest) || !out.pod(song.mabf_metadata.byte_count) ||
        !out.pod(song.mabf_metadata.logical_source_frames) || !out.pod(song.mabf_metadata.hca_frame_count) ||
        !out.pod(song.mabf_metadata.sample_rate) || !out.pod(song.mabf_metadata.channels) ||
        !out.pod(song.mabf_metadata.inserted_samples) || !out.pod(song.mabf_metadata.appended_samples) ||
        !out.pod(song.mabf_metadata.block_size) || !out.pod(song.manifest_digest) ||
        !out.pod(song.audio.sample_rate) || !out.pod(song.audio.channels) ||
        !out.pod(static_cast<std::uint64_t>(song.audio.source_frame_count)) ||
        !out.pod(static_cast<std::uint8_t>(song.chart_from_midi)) || !out.pod(song.midi_alignment_confidence) ||
        !out.pod(static_cast<std::uint8_t>(song.loudness_normalized)) ||
        !out.pod(static_cast<std::uint8_t>(song.loudness_gain_applied)) ||
        !out.pod(static_cast<std::uint8_t>(song.loudness_limiter_engaged)) ||
        !out.pod(song.loudness_input_lufs) || !out.pod(song.loudness_output_lufs) ||
        !out.pod(song.loudness_input_peak_dbfs) || !out.pod(song.loudness_output_peak_dbfs) ||
        !out.pod(song.loudness_applied_gain_db) || !out.pod(static_cast<std::uint8_t>(song.gain_envelope_applied)) ||
        !out.pod(static_cast<std::uint32_t>(song.gain_envelope_point_count)) ||
        !out.pod(song.gain_envelope_max_gain_db) || !out.pod(song.gain_envelope_min_gain_db) ||
        !out.pod(song.metronome_beat_count) || !out.pod(song.metronome_downbeat_count) ||
        !out.pod(song.metronome_first_beat_seconds) || !out.pod(song.metronome_last_beat_seconds) ||
        !write_song_config(out, song.config) || !write_compiled_chart(out, song.chart) ||
        song.difficulty_profiles.size() > kMaximumDifficultyProfiles ||
        !out.pod(static_cast<std::uint32_t>(song.difficulty_profiles.size()))) return false;
    for (const auto& profile : song.difficulty_profiles) {
        const auto hash = profile_semantic_hash_impl(profile);
        if (!valid_cached_profile(song.id, song.chart_policy_enabled, profile) ||
            !write_song_config(out, profile.config) || !write_compiled_chart(out, profile.chart) ||
            !write_profile_diagnostics(out, profile.diagnostics) || !write_diagnostic_chart(out, profile.diagnostic_chart) ||
            hash == 0 || !out.pod(hash)) return false;
    }
    if (song.difficulty_profile_omissions.size() > kMaximumDifficultyProfiles ||
        !out.pod(static_cast<std::uint32_t>(song.difficulty_profile_omissions.size()))) return false;
    for (const auto& omission : song.difficulty_profile_omissions) {
        SongConfig witness;
        witness.notes = omission.witness_notes;
        witness.notes_provided = !witness.notes.empty();
        const auto hash = omission_semantic_hash_impl(omission);
        if (!out.pod(static_cast<std::int32_t>(omission.difficulty)) ||
            !out.pod(static_cast<std::uint64_t>(omission.desired_rows)) || !out.string(omission.reason) ||
            !write_profile_diagnostics(out, omission.diagnostics) || !write_song_config(out, witness) ||
            hash == 0 || !out.pod(hash)) return false;
    }
    return true;
}

bool read_payload(RuntimeCacheReader& in, LoadedSong* song) {
    const SongConfig source_config = song->config;
    std::uint64_t cache_key = 0, generation = 0, source_frames = 0;
    std::uint32_t accepted = 0, published = 0, envelope_points = 0;
    std::uint8_t diagnostic = 0, midi = 0, normalized = 0, gain = 0, limited = 0, envelope = 0;
    std::string identity;
    MabfArtifactMetadata metadata;
    if (!in.pod(&cache_key) || cache_key != song->cache_key || !in.pod(&accepted) ||
        accepted != song->accepted_chart_input_limit || !in.pod(&published) ||
        published != song->published_chart_row_limit || !in.pod(&diagnostic) || diagnostic > 1u ||
        (diagnostic != 0) != song->chart_policy_enabled || !in.pod(&generation) ||
        generation != song->chart_policy_generation || !in.string(&identity) || identity != song->chart_policy_identity ||
        !in.pod(&metadata.digest) || !in.pod(&metadata.byte_count) || !in.pod(&metadata.logical_source_frames) ||
        !in.pod(&metadata.hca_frame_count) || !in.pod(&metadata.sample_rate) || !in.pod(&metadata.channels) ||
        !in.pod(&metadata.inserted_samples) || !in.pod(&metadata.appended_samples) || !in.pod(&metadata.block_size) ||
        !in.pod(&song->manifest_digest) || !in.pod(&song->audio.sample_rate) || !in.pod(&song->audio.channels) ||
        !in.pod(&source_frames) || source_frames > std::numeric_limits<std::size_t>::max() ||
        !in.pod(&midi) || midi > 1u || !in.pod(&song->midi_alignment_confidence) || !in.pod(&normalized) ||
        !in.pod(&gain) || !in.pod(&limited) || normalized > 1u || gain > 1u || limited > 1u ||
        !in.pod(&song->loudness_input_lufs) || !in.pod(&song->loudness_output_lufs) ||
        !in.pod(&song->loudness_input_peak_dbfs) || !in.pod(&song->loudness_output_peak_dbfs) ||
        !in.pod(&song->loudness_applied_gain_db) || !in.pod(&envelope) || envelope > 1u ||
        !in.pod(&envelope_points) || envelope_points > kMaximumGainEnvelopePoints ||
        !in.pod(&song->gain_envelope_max_gain_db) || !in.pod(&song->gain_envelope_min_gain_db) ||
        !in.pod(&song->metronome_beat_count) || !in.pod(&song->metronome_downbeat_count) ||
        !in.pod(&song->metronome_first_beat_seconds) || !in.pod(&song->metronome_last_beat_seconds) ||
        !read_song_config(in, &song->config) || !read_compiled_chart(in, &song->chart) ||
        song->config.notes.size() > kMaxChartRows || song->chart.notes.size() > kMaxChartRows ||
        song->audio.sample_rate != 48000u || song->audio.channels != 2u || source_frames == 0u ||
        source_frames != metadata.logical_source_frames || !std::isfinite(song->midi_alignment_confidence) ||
        !std::isfinite(song->loudness_input_lufs) || !std::isfinite(song->loudness_output_lufs) ||
        !std::isfinite(song->loudness_input_peak_dbfs) || !std::isfinite(song->loudness_output_peak_dbfs) ||
        !std::isfinite(song->loudness_applied_gain_db) || !std::isfinite(song->gain_envelope_max_gain_db) ||
        !std::isfinite(song->gain_envelope_min_gain_db) || !std::isfinite(song->metronome_first_beat_seconds) ||
        !std::isfinite(song->metronome_last_beat_seconds) || !valid_config_and_chart(song->config, song->chart)) return false;
    song->audio.source_frame_count = static_cast<std::size_t>(source_frames);
    song->audio.stereo_samples.clear(); song->chart_from_midi = midi != 0;
    song->loudness_normalized = normalized != 0; song->loudness_gain_applied = gain != 0;
    song->loudness_limiter_engaged = limited != 0; song->gain_envelope_applied = envelope != 0;
    song->gain_envelope_point_count = envelope_points;
    if (song->chart_from_midi && ((source_config.midi_audio_alignment_provided &&
            (song->config.midi_audio_alignment_seconds != source_config.midi_audio_alignment_seconds ||
                song->midi_alignment_confidence != 1.0)) ||
        (source_config.midi_audio_offset_provided &&
            song->config.midi_audio_offset_seconds != source_config.midi_audio_offset_seconds) ||
        (!source_config.midi_audio_offset_provided && std::fabs(song->config.midi_audio_offset_seconds -
            (song->config.midi_audio_alignment_seconds + 0.007)) > 1e-12) ||
        !song->config.midi_audio_alignment_provided || !song->config.midi_audio_offset_provided)) return false;
    std::uint32_t profiles = 0;
    if (!in.pod(&profiles) || profiles == 0 || profiles > kMaximumDifficultyProfiles) return false;
    song->difficulty_profiles.assign(profiles, {});
    for (auto& profile : song->difficulty_profiles) {
        std::uint64_t hash = 0;
        if (!read_song_config(in, &profile.config) || !read_compiled_chart(in, &profile.chart) ||
            !read_profile_diagnostics(in, &profile.diagnostics) || !read_diagnostic_chart(in, &profile.diagnostic_chart) ||
            !in.pod(&hash) || hash == 0 || hash != profile_semantic_hash_impl(profile) ||
            !valid_cached_profile(song->id, song->chart_policy_enabled, profile) ||
            !valid_config_and_chart(profile.config, profile.chart)) return false;
    }
    if (!song_configs_equal_impl(song->config, song->difficulty_profiles.front().config) ||
        !compiled_charts_equal(song->chart, song->difficulty_profiles.front().chart)) return false;
    for (std::size_t i = 1; i < song->difficulty_profiles.size(); ++i) {
        if (song->difficulty_profiles[i - 1].config.difficulty >= song->difficulty_profiles[i].config.difficulty) return false;
    }
    std::uint32_t omissions = 0;
    if (!in.pod(&omissions) || omissions > kMaximumDifficultyProfiles) return false;
    song->difficulty_profile_omissions.assign(omissions, {});
    for (std::size_t i = 0; i < omissions; ++i) {
        auto& omission = song->difficulty_profile_omissions[i];
        std::int32_t difficulty = 0;
        std::uint64_t desired = 0, hash = 0;
        SongConfig witness;
        if (!in.pod(&difficulty) || !in.pod(&desired) || desired > std::numeric_limits<std::size_t>::max() ||
            !in.string(&omission.reason) || !read_profile_diagnostics(in, &omission.diagnostics) ||
            !read_song_config(in, &witness) || !in.pod(&hash)) return false;
        omission.difficulty = difficulty; omission.desired_rows = static_cast<std::size_t>(desired);
        omission.witness_notes = std::move(witness.notes);
        if (hash == 0 || hash != omission_semantic_hash_impl(omission) || omission.difficulty < 0 || omission.reason.empty() ||
            (omission.desired_rows == 0 && omission.reason != "not_evaluated_after_monotonic_row_limit") ||
            (i > 0 && song->difficulty_profile_omissions[i - 1].difficulty >= omission.difficulty)) return false;
    }
    if (!in.at_end()) return false;
    song->mabf_metadata = metadata; song->status = Status::ok_status(); song->hca_status = Status::ok_status();
    song->accepted_chart_input_limit = accepted; song->published_chart_row_limit = published;
    return true;
}

} // namespace

bool song_configs_equal(const SongConfig& left, const SongConfig& right) {
    return song_configs_equal_impl(left, right);
}

std::uint64_t config_chart_semantic_hash(const SongConfig& config, const CompiledChart& chart) {
    RuntimeCacheWriter descriptor;
    if (!write_song_config(descriptor, config) || !write_compiled_chart(descriptor, chart)) return 0;
    return descriptor.payload_hash(kFnv1a64OffsetBasis ^ 0x636f6e6669677631ull);
}

std::uint64_t profile_semantic_hash(const LoadedDifficultyProfile& profile) {
    return profile_semantic_hash_impl(profile);
}

std::uint64_t omission_semantic_hash(const DifficultyProfileOmission& omission) {
    return omission_semantic_hash_impl(omission);
}

bool encode_runtime_cache(const LoadedSong& song, const std::span<const char, 8> magic,
    const std::uint32_t format, std::vector<std::uint8_t>* bytes) {
    if (!bytes) return false;
    RuntimeCacheWriter payload;
    if (!write_payload(payload, song) || payload.payload().size() > std::numeric_limits<std::uint64_t>::max()) return false;
    std::vector<std::uint8_t> encoded;
    encoded.insert(encoded.end(), magic.begin(), magic.end());
    append_le(&encoded, format, 4); append_le(&encoded, kRuntimeSongSection, 4);
    append_le(&encoded, payload.payload().size(), 8);
    encoded.insert(encoded.end(), payload.payload().begin(), payload.payload().end());
    append_le(&encoded, fnv1a64_append(kFnv1a64OffsetBasis ^ 0x73656d616e746963ull,
        payload.payload().data(), payload.payload().size()), 8);
    append_le(&encoded, fnv1a64_append(kFnv1a64OffsetBasis, encoded.data(), encoded.size()), 8);
    *bytes = std::move(encoded);
    return true;
}

bool decode_runtime_cache(const std::span<const std::uint8_t> bytes, const std::span<const char, 8> magic,
    const std::uint32_t format, LoadedSong* song) {
    if (!song || bytes.size() < kRuntimeCacheEnvelopeBytes || bytes.size() > (64u << 20u) ||
        !std::equal(magic.begin(), magic.end(), bytes.begin()) || read_le(bytes, 8, 4) != format ||
        read_le(bytes, 12, 4) != kRuntimeSongSection || read_le(bytes, 16, 8) != bytes.size() - kRuntimeCacheEnvelopeBytes ||
        fnv1a64_append(kFnv1a64OffsetBasis, bytes.data(), bytes.size() - 8u) != read_le(bytes, bytes.size() - 8u, 8u) ||
        fnv1a64_append(kFnv1a64OffsetBasis ^ 0x73656d616e746963ull, bytes.data() + 24u,
            bytes.size() - kRuntimeCacheEnvelopeBytes) != read_le(bytes, bytes.size() - 16u, 8u)) return false;
    LoadedSong decoded = *song;
    RuntimeCacheReader payload(bytes.subspan(24u, bytes.size() - kRuntimeCacheEnvelopeBytes));
    if (!read_payload(payload, &decoded)) return false;
    *song = std::move(decoded);
    return true;
}

} // namespace ff7rp::pipeline
