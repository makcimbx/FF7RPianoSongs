#include "chart_compiler.h"
#include "pipeline_limits.h"

#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ff7rp::pipeline {

namespace {

constexpr int kLowestSemitone = 12; // C1
constexpr int kHighestSemitone = 84; // C7
constexpr std::array<const char*, 12> kCanonicalPitchClasses{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
constexpr std::array<const char*, 12> kMonotonePitchClasses{
    "Cn", "Cs", "Dn", "Ds", "En", "Fn", "Fs", "Gn", "Gs", "An", "As", "Bn"
};
constexpr std::array<int, 7> kNaturalSemitones{9, 11, 0, 2, 4, 5, 7}; // A through G

bool parse_pitch_semitone(const std::string& pitch, int* out_semitone) {
    if (!out_semitone || (pitch.size() != 2 && pitch.size() != 3)
        || pitch[0] < 'A' || pitch[0] > 'G') {
        return false;
    }

    int accidental = 0;
    std::size_t octave_index = 1;
    if (pitch.size() == 3) {
        if (pitch[1] == '#') {
            accidental = 1;
        } else if (pitch[1] == 'b') {
            accidental = -1;
        } else {
            return false;
        }
        octave_index = 2;
    }
    if (pitch[octave_index] < '0' || pitch[octave_index] > '9') {
        return false;
    }

    const int octave = pitch[octave_index] - '0';
    *out_semitone = octave * 12 + kNaturalSemitones[pitch[0] - 'A'] + accidental;
    return true;
}

} // namespace

const std::vector<PitchMapEntry>& supported_pitch_map() {
    static const std::vector<PitchMapEntry> map = [] {
        std::vector<PitchMapEntry> result;
        result.reserve(kHighestSemitone - kLowestSemitone + 1);
        for (int semitone = kLowestSemitone; semitone <= kHighestSemitone; ++semitone) {
            const int pitch_class = semitone % 12;
            const std::string octave = std::to_string(semitone / 12);
            result.push_back({
                std::string(kCanonicalPitchClasses[pitch_class]) + octave,
                std::string(kMonotonePitchClasses[pitch_class]) + octave,
            });
        }
        return result;
    }();
    return map;
}

Status pitch_to_monotone_id(const std::string& pitch, std::string* out_monotone_id) {
    if (!out_monotone_id) {
        return Status::error(StatusCode::InvalidArgument, "out_monotone_id must not be null");
    }
    int semitone = 0;
    if (parse_pitch_semitone(pitch, &semitone)
        && semitone >= kLowestSemitone && semitone <= kHighestSemitone) {
        *out_monotone_id = supported_pitch_map()[semitone - kLowestSemitone].monotone_id;
        return Status::ok_status();
    }

    return Status::error(
        StatusCode::UnsupportedPitch,
        "unsupported pitch '" + pitch + "'; pitches must resolve to C1 through C7");
}

std::string beat_to_time_str(double beat, double bpm) {
    const double seconds = beat * 60.0 / bpm;
    const auto frames = static_cast<long long>(std::llround(seconds * 60.0));
    const long long whole_seconds = frames / 60;
    const long long remainder = frames % 60;

    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << whole_seconds
        << '_' << std::setw(2) << std::setfill('0') << remainder;
    return out.str();
}

Status compile_chart(const SongConfig& config, CompiledChart* out_chart,
    DiagnosticChartRetention* out_diagnostic, const std::size_t input_row_limit) {
    if (!out_chart) {
        return Status::error(StatusCode::InvalidArgument, "out_chart must not be null");
    }
    if (!std::isfinite(config.bpm) || config.bpm <= 0.0) {
        return Status::error(StatusCode::InvalidChart, "bpm must be positive before chart compilation");
    }
    const std::size_t row_limit = input_row_limit == 0 ? chart_input_row_limit() : input_row_limit;
    if (config.notes.size() > row_limit) {
        return Status::error(StatusCode::ChartRowLimitExceeded,
            "complete chart requires " + std::to_string(config.notes.size()) +
            " rows, above the accepted chart input limit of " + std::to_string(row_limit));
    }
    const bool has_diagnostic_tail = config.notes.size() > kMaxChartRows;
    if (has_diagnostic_tail && (!config.diagnostic_extended_chart_fixture || !out_diagnostic ||
        config.notes.size() != 520u || row_limit < config.notes.size())) {
        return Status::error(StatusCode::ChartRowLimitExceeded,
            "over-limit input is restricted to the explicit exactly-520 diagnostic fixture");
    }
    if (config.diagnostic_extended_chart_fixture && config.notes.size() != 520u) {
        return Status::error(StatusCode::InvalidChart,
            "diagnostic_extended_chart_fixture requires exactly 520 source rows");
    }

    CompiledChart chart;
    chart.notes.reserve(config.notes.size());
    for (std::size_t i = 0; i < config.notes.size(); ++i) {
        const Note& source = config.notes[i];
        if (!std::isfinite(source.beat) || source.beat < 0.0 || !std::isfinite(source.duration_beats) || source.duration_beats <= 0.0) {
            return Status::error(StatusCode::InvalidChart, "invalid beat or duration at note index " + std::to_string(i));
        }

        ChartNote note;
        note.beat = source.beat;
        note.duration_beats = source.duration_beats;
        note.pitch = source.pitch;
        note.chord_id = source.chord_id;
        note.time_str = beat_to_time_str(source.beat, config.bpm);
        note.note_type = source.duration_beats >= 2.0 ? 2 : 3;
        note.dot_type = 0;
        note.camera_switch_timing = 0;
        note.group_index = 0;

        if (!source.pitch.empty()) {
            Status status = pitch_to_monotone_id(source.pitch, &note.monotone_id);
            if (!status.ok()) {
                status.message += " at note index " + std::to_string(i);
                return status;
            }
        }
        chart.notes.push_back(std::move(note));
    }

    DiagnosticChartRetention diagnostic;
    if (has_diagnostic_tail) {
        diagnostic.source_row_count = chart.notes.size();
        diagnostic.native_prefix_row_count = kMaxChartRows;
        diagnostic.tail_rows.reserve(chart.notes.size() - kMaxChartRows);
        for (std::size_t index = kMaxChartRows; index < chart.notes.size(); ++index) {
            diagnostic.tail_rows.push_back({index, config.notes[index], chart.notes[index]});
        }
        chart.notes.resize(kMaxChartRows);
    }
    *out_chart = std::move(chart);
    if (out_diagnostic) *out_diagnostic = std::move(diagnostic);
    return Status::ok_status();
}

std::uint64_t diagnostic_descriptor_hash(
    const std::string& song_id, const int difficulty, const CompiledChart& playable_chart,
    const DiagnosticChartRetention& diagnostic) {
    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    const auto append = [&](const void* data, const std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= kPrime;
        }
    };
    const auto append_string = [&](const std::string& value) {
        const std::uint64_t size = value.size();
        append(&size, sizeof(size));
        append(value.data(), value.size());
    };
    const auto append_source = [&](const Note& note) {
        append(&note.beat, sizeof(note.beat));
        append(&note.duration_beats, sizeof(note.duration_beats));
        append_string(note.pitch);
        append_string(note.chord_id);
    };
    const auto append_compiled = [&](const ChartNote& note) {
        append(&note.beat, sizeof(note.beat));
        append(&note.duration_beats, sizeof(note.duration_beats));
        append_string(note.pitch);
        append_string(note.time_str);
        append_string(note.monotone_id);
        append_string(note.chord_id);
        append(&note.note_type, sizeof(note.note_type));
        append(&note.dot_type, sizeof(note.dot_type));
        append(&note.camera_switch_timing, sizeof(note.camera_switch_timing));
        append(&note.group_index, sizeof(note.group_index));
    };
    append_string(song_id);
    append(&difficulty, sizeof(difficulty));
    const std::uint64_t playable_count = playable_chart.notes.size();
    append(&playable_count, sizeof(playable_count));
    for (const ChartNote& note : playable_chart.notes) {
        append_compiled(note);
    }
    const std::uint64_t source_count = diagnostic.source_row_count;
    const std::uint64_t prefix_count = diagnostic.native_prefix_row_count;
    const std::uint64_t tail_count = diagnostic.tail_rows.size();
    append(&source_count, sizeof(source_count));
    append(&prefix_count, sizeof(prefix_count));
    append(&tail_count, sizeof(tail_count));
    for (const DiagnosticChartTailRow& row : diagnostic.tail_rows) {
        const std::uint64_t source_row = row.source_row;
        append(&source_row, sizeof(source_row));
        append_source(row.source);
        append_compiled(row.compiled);
    }
    return hash;
}

bool diagnostic_charts_equal(
    const DiagnosticChartRetention& left, const DiagnosticChartRetention& right) {
    const auto source_equal = [](const Note& a, const Note& b) {
        return a.beat == b.beat && a.duration_beats == b.duration_beats &&
            a.pitch == b.pitch && a.chord_id == b.chord_id;
    };
    const auto compiled_equal = [](const ChartNote& a, const ChartNote& b) {
        return a.beat == b.beat && a.duration_beats == b.duration_beats &&
            a.pitch == b.pitch && a.time_str == b.time_str && a.monotone_id == b.monotone_id &&
            a.chord_id == b.chord_id && a.note_type == b.note_type && a.dot_type == b.dot_type &&
            a.camera_switch_timing == b.camera_switch_timing && a.group_index == b.group_index;
    };
    if (left.source_row_count != right.source_row_count ||
        left.native_prefix_row_count != right.native_prefix_row_count ||
        left.tail_rows.size() != right.tail_rows.size() ||
        left.descriptor_hash != right.descriptor_hash) return false;
    for (std::size_t index = 0; index < left.tail_rows.size(); ++index) {
        const auto& a = left.tail_rows[index];
        const auto& b = right.tail_rows[index];
        if (a.source_row != b.source_row || !source_equal(a.source, b.source) ||
            !compiled_equal(a.compiled, b.compiled)) return false;
    }
    return true;
}

} // namespace ff7rp::pipeline
