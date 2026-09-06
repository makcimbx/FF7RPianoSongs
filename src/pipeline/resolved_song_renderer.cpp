#include "resolved_song_renderer.h"

#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string_view>

#include "chart_event_plan.h"
#include "chord_voicing.h"
#include "note_value.h"

namespace ff7rp::pipeline {
namespace {

Status invalid_projection(const std::string& detail) {
    return Status::error(StatusCode::InvalidChart, "cannot render resolved song JSON: " + detail);
}

bool well_formed_utf8(const std::string_view value) {
    const auto continuation = [](const unsigned char byte) {
        return byte >= 0x80u && byte <= 0xbfu;
    };
    for (std::size_t index = 0; index < value.size();) {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fu) {
            ++index;
            continue;
        }
        if (first >= 0xc2u && first <= 0xdfu) {
            if (index + 1u >= value.size() ||
                !continuation(static_cast<unsigned char>(value[index + 1u]))) return false;
            index += 2u;
            continue;
        }
        if (first >= 0xe0u && first <= 0xefu) {
            if (index + 2u >= value.size()) return false;
            const unsigned char second = static_cast<unsigned char>(value[index + 1u]);
            const unsigned char third = static_cast<unsigned char>(value[index + 2u]);
            if (!continuation(third) ||
                (first == 0xe0u ? second < 0xa0u || second > 0xbfu
                                : first == 0xedu ? second < 0x80u || second > 0x9fu
                                                 : !continuation(second))) return false;
            index += 3u;
            continue;
        }
        if (first >= 0xf0u && first <= 0xf4u) {
            if (index + 3u >= value.size()) return false;
            const unsigned char second = static_cast<unsigned char>(value[index + 1u]);
            if ((first == 0xf0u ? second < 0x90u || second > 0xbfu
                                : first == 0xf4u ? second < 0x80u || second > 0x8fu
                                                 : !continuation(second)) ||
                !continuation(static_cast<unsigned char>(value[index + 2u])) ||
                !continuation(static_cast<unsigned char>(value[index + 3u]))) return false;
            index += 4u;
            continue;
        }
        return false;
    }
    return true;
}

bool append_escaped_json_string(std::string* out, const std::string_view value) {
    if (!well_formed_utf8(value)) return false;
    constexpr char kHex[] = "0123456789abcdef";
    out->push_back('"');
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': out->append("\\\""); break;
        case '\\': out->append("\\\\"); break;
        case '\b': out->append("\\b"); break;
        case '\f': out->append("\\f"); break;
        case '\n': out->append("\\n"); break;
        case '\r': out->append("\\r"); break;
        case '\t': out->append("\\t"); break;
        default:
            if (byte < 0x20u) {
                out->append("\\u00");
                out->push_back(kHex[(byte >> 4u) & 0xfu]);
                out->push_back(kHex[byte & 0xfu]);
            } else {
                out->push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    out->push_back('"');
    return true;
}

bool append_number(std::string* out, const double value) {
    if (!std::isfinite(value)) return false;
    char buffer[64]{};
    const auto result = std::to_chars(
        buffer, buffer + sizeof(buffer), value, std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) return false;
    out->append(buffer, result.ptr);
    return true;
}

void append_int_array(std::string* out, const std::vector<int>& values) {
    out->push_back('[');
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0u) out->append(", ");
        out->append(std::to_string(values[index]));
    }
    out->push_back(']');
}

std::string_view note_value_name(const std::int32_t note_type, const std::int32_t dot_type) {
    for (const NamedNativeNoteValue& value : kSupportedNamedNoteValues) {
        if (value.value.note_type == note_type && value.value.dot_type == dot_type) return value.name;
    }
    return {};
}

Status append_note(std::string* out, const Note& source, const ChartNote& compiled,
                   const std::string_view indentation) {
    if (!chart_note_semantics_equal(source, compiled)) {
        return invalid_projection("source and compiled row semantics differ");
    }
    if (!std::isfinite(source.beat) || !std::isfinite(source.duration_beats)) {
        return invalid_projection("row timing is not finite");
    }
    out->append(indentation);
    out->append("{ \"beat\": ");
    if (!append_number(out, source.beat)) return invalid_projection("beat is not renderable");
    out->append(", \"duration_beats\": ");
    if (!append_number(out, source.duration_beats)) return invalid_projection("duration is not renderable");
    if (!source.pitch.empty()) {
        out->append(", \"pitch\": ");
        if (!append_escaped_json_string(out, source.pitch)) {
            return invalid_projection("pitch is not well-formed UTF-8");
        }
        const std::string_view value = note_value_name(
            compiled.monotone_note_type, compiled.monotone_dot_type);
        if (value.empty()) return invalid_projection("monotone note value is unsupported");
        out->append(", \"monotone_note_value\": ");
        if (!append_escaped_json_string(out, value)) {
            return invalid_projection("monotone note value is not well-formed UTF-8");
        }
        if (source.alternate_monotone) out->append(", \"monotone_variant\": \"alternate\"");
    }
    if (!source.chord_id.empty()) {
        out->append(", \"chord_id\": ");
        if (!append_escaped_json_string(out, source.chord_id)) {
            return invalid_projection("chord ID is not well-formed UTF-8");
        }
        const std::string_view value = note_value_name(compiled.chord_note_type, compiled.chord_dot_type);
        if (value.empty()) return invalid_projection("chord note value is unsupported");
        out->append(", \"chord_note_value\": ");
        if (!append_escaped_json_string(out, value)) {
            return invalid_projection("chord note value is not well-formed UTF-8");
        }
    }
    if (source.group_index != 0u) {
        out->append(", \"group_index\": ");
        out->append(std::to_string(source.group_index));
    }
    if (!source.ignore_sound_pitches.empty()) {
        out->append(", \"ignore_sound\": [");
        for (std::size_t index = 0; index < source.ignore_sound_pitches.size(); ++index) {
            if (index != 0u) out->append(", ");
            if (!append_escaped_json_string(out, source.ignore_sound_pitches[index])) {
                return invalid_projection("IgnoreSound identity is not well-formed UTF-8");
            }
        }
        out->push_back(']');
    }
    out->append(" }");
    return Status::ok_status();
}

Status append_complete_notes(std::string* out, const LoadedDifficultyProfile& profile,
                             const std::string_view indentation) {
    ChartEventPlan plan;
    if (!derive_profile_event_plan(profile, &plan)) {
        return invalid_projection("profile event plan is invalid");
    }
    if (profile.config.notes.size() != profile.chart.notes.size()) {
        return invalid_projection("profile prefix source/compiled row counts differ");
    }
    out->append("[\n");
    std::size_t emitted = 0;
    const std::size_t total = plan.source_row_count;
    for (std::size_t index = 0; index < profile.config.notes.size(); ++index) {
        const Status status = append_note(out, profile.config.notes[index], profile.chart.notes[index], indentation);
        if (!status.ok()) return status;
        out->append(++emitted == total ? "\n" : ",\n");
    }
    for (std::size_t index = 0; index < profile.diagnostic_chart.tail_rows.size(); ++index) {
        const DiagnosticChartTailRow& row = profile.diagnostic_chart.tail_rows[index];
        if (row.source_row != profile.config.notes.size() + index) {
            return invalid_projection("profile tail rows are not contiguous");
        }
        const Status status = append_note(out, row.source, row.compiled, indentation);
        if (!status.ok()) return status;
        out->append(++emitted == total ? "\n" : ",\n");
    }
    if (emitted != total) return invalid_projection("profile complete row count differs from event plan");
    out->append(indentation.substr(0, indentation.size() >= 2u ? indentation.size() - 2u : 0u));
    out->push_back(']');
    return Status::ok_status();
}

Status append_root_settings(std::string* out, const LoadedSong& song, const bool emit_profiles) {
    const SongConfig& config = song.config;
    const Status voicing_status = validate_chord_voicings(config);
    if (!voicing_status.ok()) return invalid_projection(voicing_status.message);
    if (!std::isfinite(config.bpm) || !std::isfinite(config.midi_audio_offset_seconds) ||
        !std::isfinite(config.midi_audio_alignment_seconds) ||
        !std::isfinite(config.midi_minimum_lead_in_seconds) ||
        !std::isfinite(config.loudness_target_lufs) ||
        !std::isfinite(config.loudness_peak_ceiling_dbfs) ||
        !std::isfinite(config.metronome_level) ||
        !std::isfinite(config.metronome_beat_zero_offset_seconds)) {
        return invalid_projection("effective public settings are not finite");
    }
    out->append("{\n  \"schema\": \"ff7rpianosongs.song.v2\",\n  \"title\": ");
    if (!append_escaped_json_string(out, config.title)) {
        return invalid_projection("title is not well-formed UTF-8");
    }
    out->append(",\n  \"bpm\": ");
    if (!append_number(out, config.bpm)) return invalid_projection("BPM is not renderable");
    if (!emit_profiles) {
        out->append(",\n  \"difficulty\": ");
        out->append(std::to_string(config.difficulty));
    }
    if (!emit_profiles || config.score_thresholds_provided) {
        out->append(",\n  \"score_thresholds\": ");
        append_int_array(out, config.score_thresholds);
    }
    if (!emit_profiles || config.mode_change_combo_counts_provided) {
        out->append(",\n  \"mode_change_combo_counts\": ");
        append_int_array(out, config.mode_change_combo_counts);
    }
    out->append(",\n  \"midi_audio_offset_seconds\": ");
    if (!append_number(out, config.midi_audio_offset_seconds)) return invalid_projection("MIDI offset is not renderable");
    out->append(",\n  \"midi_audio_alignment_seconds\": ");
    if (!append_number(out, config.midi_audio_alignment_seconds)) return invalid_projection("MIDI alignment is not renderable");
    out->append(",\n  \"midi_minimum_lead_in_seconds\": ");
    if (!append_number(out, config.midi_minimum_lead_in_seconds)) return invalid_projection("MIDI lead-in is not renderable");
    out->append(",\n  \"loudness_normalization\": ");
    out->append(config.loudness_normalization ? "true" : "false");
    out->append(",\n  \"loudness_target_lufs\": ");
    if (!append_number(out, config.loudness_target_lufs)) return invalid_projection("loudness target is not renderable");
    out->append(",\n  \"loudness_peak_ceiling_dbfs\": ");
    if (!append_number(out, config.loudness_peak_ceiling_dbfs)) return invalid_projection("peak ceiling is not renderable");
    if (!config.gain_envelope.empty()) {
        out->append(",\n  \"gain_envelope\": [\n");
        for (std::size_t index = 0; index < config.gain_envelope.size(); ++index) {
            const GainEnvelopePoint& point = config.gain_envelope[index];
            if (!std::isfinite(point.time_seconds) || !std::isfinite(point.gain_db)) {
                return invalid_projection("gain envelope is not finite");
            }
            out->append("    { \"time_seconds\": ");
            if (!append_number(out, point.time_seconds)) return invalid_projection("gain time is not renderable");
            out->append(", \"gain_db\": ");
            if (!append_number(out, point.gain_db)) return invalid_projection("gain value is not renderable");
            out->append(index + 1u == config.gain_envelope.size() ? " }\n" : " },\n");
        }
        out->append("  ]");
    }
    if (!config.chord_voicings.empty()) {
        out->append(",\n  \"chord_voicings\": {\n");
        for (std::size_t index = 0; index < config.chord_voicings.size(); ++index) {
            const auto& voicing = config.chord_voicings[index];
            out->append("    ");
            if (!append_escaped_json_string(out, voicing.chord_id)) return invalid_projection("chord ID is not renderable");
            out->append(": [");
            for (std::size_t slot = 0; slot < voicing.sound_ids.size(); ++slot) {
                if (slot != 0u) out->append(", ");
                if (!append_escaped_json_string(out, voicing.sound_ids[slot])) return invalid_projection("sound ID is not renderable");
            }
            out->append(index + 1u == config.chord_voicings.size() ? "]\n" : "],\n");
        }
        out->append("  }");
    }
    out->append(",\n  \"metronome\": { \"enabled\": ");
    out->append(config.metronome_enabled ? "true" : "false");
    out->append(", \"level\": ");
    if (!append_number(out, config.metronome_level)) return invalid_projection("metronome level is not renderable");
    out->append(", \"beat_zero_offset_seconds\": ");
    if (!append_number(out, config.metronome_beat_zero_offset_seconds)) {
        return invalid_projection("metronome offset is not renderable");
    }
    out->append(" }");
    return Status::ok_status();
}

} // namespace

std::string resolved_song_json_path(const std::string& song_directory) {
    return (std::filesystem::path(song_directory) / ".cache" / kResolvedSongJsonFilename).string();
}

Status render_resolved_song_json(
    const LoadedSong& song, const bool source_declared_profiles, std::string* out_json) {
    if (!out_json) return Status::error(StatusCode::InvalidArgument, "out_json must not be null");
    const bool emit_profiles = song.chart_from_midi || source_declared_profiles;
    if (song.difficulty_profiles.empty()) return invalid_projection("song has no visible profile");
    if (song.chart_from_midi && !song.config.chord_voicings.empty())
        return invalid_projection("MIDI-generated charts cannot carry authored chord_voicings");
    for (const auto& profile : song.difficulty_profiles)
        if (profile.config.chord_voicings != song.config.chord_voicings)
            return invalid_projection("profile chord_voicings differs from song root");
    std::string rendered;
    Status status = append_root_settings(&rendered, song, emit_profiles);
    if (!status.ok()) return status;
    if (emit_profiles) {
        rendered.append(",\n  \"profiles\": [\n");
        for (std::size_t index = 0; index < song.difficulty_profiles.size(); ++index) {
            const LoadedDifficultyProfile& profile = song.difficulty_profiles[index];
            rendered.append("    { \"difficulty\": ");
            rendered.append(std::to_string(profile.config.difficulty));
            rendered.append(", \"notes\": ");
            status = append_complete_notes(&rendered, profile, "      ");
            if (!status.ok()) return status;
            rendered.append(index + 1u == song.difficulty_profiles.size() ? " }\n" : " },\n");
        }
        rendered.append("  ]\n}\n");
    } else {
        rendered.append(",\n  \"notes\": ");
        status = append_complete_notes(&rendered, song.difficulty_profiles.front(), "    ");
        if (!status.ok()) return status;
        rendered.append("\n}\n");
    }
    *out_json = std::move(rendered);
    return Status::ok_status();
}

} // namespace ff7rp::pipeline
