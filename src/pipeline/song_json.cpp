#include "song_json.h"
#include "pipeline_limits.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <map>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

#include "audio_loudness.h"

namespace ff7rp::pipeline {
namespace {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    bool parse(JsonValue* out, std::string* error) {
        skip_ws();
        if (!parse_value(out, error)) {
            return false;
        }
        skip_ws();
        if (pos_ != input_.size()) {
            set_error(error, "unexpected trailing data");
            return false;
        }
        return true;
    }

private:
    const std::string& input_;
    std::size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < input_.size()) {
            const char c = input_[pos_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                break;
            }
            ++pos_;
        }
    }

    void set_error(std::string* error, const std::string& message) const {
        if (error) {
            std::ostringstream stream;
            stream << "JSON parse error at byte " << pos_ << ": " << message;
            *error = stream.str();
        }
    }

    bool consume(char expected) {
        if (pos_ < input_.size() && input_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool match_literal(const char* literal) {
        const std::size_t start = pos_;
        for (const char* p = literal; *p; ++p) {
            if (pos_ >= input_.size() || input_[pos_] != *p) {
                pos_ = start;
                return false;
            }
            ++pos_;
        }
        return true;
    }

    bool parse_value(JsonValue* out, std::string* error) {
        skip_ws();
        if (pos_ >= input_.size()) {
            set_error(error, "expected value");
            return false;
        }

        const char c = input_[pos_];
        if (c == '{') {
            return parse_object(out, error);
        }
        if (c == '[') {
            return parse_array(out, error);
        }
        if (c == '"') {
            out->type = JsonValue::Type::String;
            return parse_string(&out->string, error);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number(out, error);
        }
        if (match_literal("true")) {
            out->type = JsonValue::Type::Bool;
            out->boolean = true;
            return true;
        }
        if (match_literal("false")) {
            out->type = JsonValue::Type::Bool;
            out->boolean = false;
            return true;
        }
        if (match_literal("null")) {
            out->type = JsonValue::Type::Null;
            return true;
        }

        set_error(error, "expected object, array, string, number, true, false, or null");
        return false;
    }

    bool parse_object(JsonValue* out, std::string* error) {
        out->type = JsonValue::Type::Object;
        out->object.clear();
        consume('{');
        skip_ws();
        if (consume('}')) {
            return true;
        }

        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(&key, error)) {
                return false;
            }
            skip_ws();
            if (!consume(':')) {
                set_error(error, "expected ':' after object key");
                return false;
            }
            JsonValue value;
            if (!parse_value(&value, error)) {
                return false;
            }
            const auto inserted = out->object.emplace(std::move(key), std::move(value));
            if (!inserted.second) {
                set_error(error, "duplicate object key '" + inserted.first->first + "'");
                return false;
            }
            skip_ws();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                set_error(error, "expected ',' or '}' in object");
                return false;
            }
        }
    }

    bool parse_array(JsonValue* out, std::string* error) {
        out->type = JsonValue::Type::Array;
        out->array.clear();
        consume('[');
        skip_ws();
        if (consume(']')) {
            return true;
        }

        while (true) {
            JsonValue value;
            if (!parse_value(&value, error)) {
                return false;
            }
            out->array.push_back(std::move(value));
            skip_ws();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                set_error(error, "expected ',' or ']' in array");
                return false;
            }
        }
    }

    bool parse_string(std::string* out, std::string* error) {
        if (!consume('"')) {
            set_error(error, "expected string");
            return false;
        }

        out->clear();
        while (pos_ < input_.size()) {
            const char c = input_[pos_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                set_error(error, "control character in string");
                return false;
            }
            if (c != '\\') {
                out->push_back(c);
                continue;
            }

            if (pos_ >= input_.size()) {
                set_error(error, "unterminated escape sequence");
                return false;
            }
            const char escaped = input_[pos_++];
            switch (escaped) {
            case '"': out->push_back('"'); break;
            case '\\': out->push_back('\\'); break;
            case '/': out->push_back('/'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u':
                if (!append_unicode_escape(out, error)) return false;
                break;
            default:
                set_error(error, "unsupported string escape");
                return false;
            }
        }

        set_error(error, "unterminated string");
        return false;
    }

    bool read_unicode_unit(std::uint16_t* out, std::string* error) {
        std::uint16_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= input_.size()) {
                set_error(error, "unterminated unicode escape");
                return false;
            }
            const char c = input_[pos_++];
            unsigned digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else {
                set_error(error, "invalid unicode escape");
                return false;
            }
            value = static_cast<std::uint16_t>((value << 4u) | digit);
        }
        *out = value;
        return true;
    }

    bool append_unicode_escape(std::string* out, std::string* error) {
        std::uint16_t first = 0;
        if (!read_unicode_unit(&first, error)) return false;
        std::uint32_t codepoint = first;
        if (first >= 0xd800u && first <= 0xdbffu) {
            if (pos_ + 2u > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1u] != 'u') {
                set_error(error, "high surrogate must be followed by a low surrogate");
                return false;
            }
            pos_ += 2u;
            std::uint16_t second = 0;
            if (!read_unicode_unit(&second, error)) return false;
            if (second < 0xdc00u || second > 0xdfffu) {
                set_error(error, "invalid low surrogate");
                return false;
            }
            codepoint = 0x10000u + ((first - 0xd800u) << 10u) + (second - 0xdc00u);
        } else if (first >= 0xdc00u && first <= 0xdfffu) {
            set_error(error, "unpaired low surrogate");
            return false;
        }
        if (codepoint <= 0x7fu) out->push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7ffu) {
            out->push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
            out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else if (codepoint <= 0xffffu) {
            out->push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
            out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else {
            out->push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
            out->push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
        return true;
    }

    bool parse_number(JsonValue* out, std::string* error) {
        const std::size_t start_pos = pos_;
        if (consume('-') && pos_ >= input_.size()) {
            set_error(error, "invalid number");
            return false;
        }
        if (consume('0')) {
            if (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') {
                set_error(error, "leading zero in number");
                return false;
            }
        } else {
            if (pos_ >= input_.size() || input_[pos_] < '1' || input_[pos_] > '9') {
                set_error(error, "invalid integer part");
                return false;
            }
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        }
        if (consume('.')) {
            if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9') {
                set_error(error, "fraction requires a digit");
                return false;
            }
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9') {
                set_error(error, "exponent requires a digit");
                return false;
            }
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        }
        const std::string token = input_.substr(start_pos, pos_ - start_pos);
        const char* start = token.c_str();
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(start, &end);
        if (end != start + token.size() || errno == ERANGE || !std::isfinite(value)) {
            set_error(error, "invalid number");
            return false;
        }
        out->type = JsonValue::Type::Number;
        out->number = value;
        return true;
    }
};

const JsonValue* find_member(const JsonValue& object, const char* key) {
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const auto it = object.object.find(key);
    return it == object.object.end() ? nullptr : &it->second;
}

Status reject_unknown_members(
    const JsonValue& object,
    std::initializer_list<std::string_view> allowed,
    std::string_view context);

Status require_string(const JsonValue& object, const char* key, std::string* out) {
    const JsonValue* value = find_member(object, key);
    if (!value) {
        return Status::error(StatusCode::InvalidJson, std::string("missing required field '") + key + "'");
    }
    if (value->type != JsonValue::Type::String) {
        return Status::error(StatusCode::InvalidJson, std::string("field '") + key + "' must be a string");
    }
    *out = value->string;
    return Status::ok_status();
}

Status require_number(const JsonValue& object, const char* key, double* out) {
    const JsonValue* value = find_member(object, key);
    if (!value) {
        return Status::error(StatusCode::InvalidJson, std::string("missing required field '") + key + "'");
    }
    if (value->type != JsonValue::Type::Number) {
        return Status::error(StatusCode::InvalidJson, std::string("field '") + key + "' must be a number");
    }
    *out = value->number;
    return Status::ok_status();
}

Status parse_int_array(const JsonValue& root, const char* key, std::vector<int>* out) {
    const JsonValue* value = find_member(root, key);
    if (!value) {
        return Status::ok_status();
    }
    if (value->type != JsonValue::Type::Array) {
        return Status::error(StatusCode::InvalidJson, std::string("field '") + key + "' must be an array");
    }

    std::vector<int> parsed;
    for (std::size_t i = 0; i < value->array.size(); ++i) {
        const JsonValue& item = value->array[i];
        if (item.type != JsonValue::Type::Number || !std::isfinite(item.number)) {
            return Status::error(StatusCode::InvalidJson, std::string("field '") + key + "' contains a non-number at index " + std::to_string(i));
        }
        const double rounded = std::round(item.number);
        if (std::fabs(item.number - rounded) > 0.000001 || rounded < 0.0 ||
            rounded > static_cast<double>(std::numeric_limits<int>::max())) {
            return Status::error(StatusCode::InvalidJson, std::string("field '") + key + "' must contain non-negative integers");
        }
        parsed.push_back(static_cast<int>(rounded));
    }
    if (parsed.empty()) {
        return Status::error(StatusCode::InvalidJson, std::string("field '") + key + "' must not be empty when present");
    }
    *out = std::move(parsed);
    return Status::ok_status();
}

Status parse_non_negative_integer(const JsonValue& object, const char* key, int* out) {
    const JsonValue* value = find_member(object, key);
    if (!value) {
        return Status::error(StatusCode::InvalidJson, std::string("missing required field '") + key + "'");
    }
    if (value->type != JsonValue::Type::Number) {
        return Status::error(StatusCode::InvalidJson, std::string("field '") + key + "' must be a number");
    }
    const double rounded = std::round(value->number);
    if (std::fabs(value->number - rounded) > 0.000001 || rounded < 0.0 ||
        rounded > static_cast<double>(std::numeric_limits<int>::max())) {
        return Status::error(StatusCode::InvalidJson,
            std::string("field '") + key + "' must be a non-negative integer");
    }
    *out = static_cast<int>(rounded);
    return Status::ok_status();
}

Status parse_optional_note_extensions(const JsonValue& object, Note* note, const std::size_t index) {
    if (const JsonValue* group = find_member(object, "group_index")) {
        if (group->type != JsonValue::Type::Number || !std::isfinite(group->number) ||
            std::floor(group->number) != group->number || group->number < 0.0 || group->number > 255.0) {
            return Status::error(StatusCode::InvalidJson,
                "note group_index must be an integer between 0 and 255 at index " + std::to_string(index));
        }
        note->group_index = static_cast<std::uint8_t>(group->number);
    }
    if (const JsonValue* variant = find_member(object, "monotone_variant")) {
        if (variant->type != JsonValue::Type::String ||
            (variant->string != "default" && variant->string != "alternate")) {
            return Status::error(StatusCode::InvalidJson,
                "note monotone_variant must be 'default' or 'alternate' at index " + std::to_string(index));
        }
        note->alternate_monotone = variant->string == "alternate";
    }
    if (const JsonValue* ignores = find_member(object, "ignore_sound")) {
        if (ignores->type != JsonValue::Type::Array || ignores->array.empty() || ignores->array.size() > 3u) {
            return Status::error(StatusCode::InvalidJson,
                "note ignore_sound must contain between 1 and 3 pitch names at index " + std::to_string(index));
        }
        for (const JsonValue& value : ignores->array) {
            if (value.type != JsonValue::Type::String || value.string.empty() ||
                std::find(note->ignore_sound_pitches.begin(), note->ignore_sound_pitches.end(), value.string) !=
                    note->ignore_sound_pitches.end()) {
                return Status::error(StatusCode::InvalidJson,
                    "note ignore_sound must contain unique non-empty pitch strings at index " + std::to_string(index));
            }
            note->ignore_sound_pitches.push_back(value.string);
        }
    }
    return Status::ok_status();
}

Status parse_notes(const JsonValue& root, std::vector<Note>* out, bool* out_provided) {
    const JsonValue* notes = find_member(root, "notes");
    if (!notes) {
        *out_provided = false;
        out->clear();
        return Status::ok_status();
    }
    *out_provided = true;
    if (notes->type != JsonValue::Type::Array) {
        return Status::error(StatusCode::InvalidJson, "field 'notes' must be an array");
    }
    if (notes->array.empty()) {
        return Status::error(StatusCode::InvalidJson, "field 'notes' must not be empty");
    }

    std::vector<Note> parsed;
    double previous_beat = -1.0;
    for (std::size_t i = 0; i < notes->array.size(); ++i) {
        const JsonValue& note_object = notes->array[i];
        if (note_object.type != JsonValue::Type::Object) {
            return Status::error(StatusCode::InvalidJson, "each note must be an object; invalid note index " + std::to_string(i));
        }

        Note note;
        Status status = reject_unknown_members(note_object,
            {"beat", "duration_beats", "pitch", "chord_id", "group_index", "monotone_variant", "ignore_sound"},
            "note index " + std::to_string(i));
        if (!status.ok()) return status;
        status = require_number(note_object, "beat", &note.beat);
        if (!status.ok()) {
            status.message += " in note index " + std::to_string(i);
            return status;
        }
        status = require_number(note_object, "duration_beats", &note.duration_beats);
        if (!status.ok()) {
            status.message += " in note index " + std::to_string(i);
            return status;
        }
        if (const JsonValue* pitch = find_member(note_object, "pitch")) {
            if (pitch->type != JsonValue::Type::String || pitch->string.empty()) {
                return Status::error(StatusCode::InvalidJson, "note pitch must be a non-empty string at index " + std::to_string(i));
            }
            note.pitch = pitch->string;
        }
        if (const JsonValue* chord_id = find_member(note_object, "chord_id")) {
            if (chord_id->type != JsonValue::Type::String) {
                return Status::error(StatusCode::InvalidJson, "note chord_id must be a string at index " + std::to_string(i));
            }
            note.chord_id = chord_id->string;
            if (note.chord_id.size() <= 4 || note.chord_id.compare(0, 4, "pca_") != 0) {
                return Status::error(StatusCode::InvalidJson, "note chord_id must use a native pca_* identifier at index " + std::to_string(i));
            }
            for (const char ch : note.chord_id) {
                const bool valid = (ch >= 'A' && ch <= 'Z')
                    || (ch >= 'a' && ch <= 'z')
                    || (ch >= '0' && ch <= '9')
                    || ch == '_';
                if (!valid) {
                    return Status::error(StatusCode::InvalidJson, "note chord_id contains an invalid character at index " + std::to_string(i));
                }
            }
        }
        status = parse_optional_note_extensions(note_object, &note, i);
        if (!status.ok()) return status;

        if (!std::isfinite(note.beat) || note.beat < 0.0) {
            return Status::error(StatusCode::InvalidJson, "note beat must be non-negative at index " + std::to_string(i));
        }
        if (!std::isfinite(note.duration_beats) || note.duration_beats <= 0.0) {
            return Status::error(StatusCode::InvalidJson, "note duration_beats must be positive at index " + std::to_string(i));
        }
        if (note.pitch.empty() && note.chord_id.empty()) {
            return Status::error(StatusCode::InvalidJson, "note must contain pitch, chord_id, or both at index " + std::to_string(i));
        }
        if (note.alternate_monotone && note.pitch.empty()) {
            return Status::error(StatusCode::InvalidJson,
                "note monotone_variant requires pitch at index " + std::to_string(i));
        }
        if (!note.ignore_sound_pitches.empty() && note.chord_id.empty()) {
            return Status::error(StatusCode::InvalidJson,
                "note ignore_sound requires chord_id at index " + std::to_string(i));
        }
        if (previous_beat > note.beat) {
            return Status::error(StatusCode::InvalidJson, "notes must be sorted by non-decreasing beat; invalid note index " + std::to_string(i));
        }
        previous_beat = note.beat;
        parsed.push_back(std::move(note));
    }

    *out = std::move(parsed);
    return Status::ok_status();
}

Status reject_unknown_members(
    const JsonValue& object,
    const std::initializer_list<std::string_view> allowed,
    const std::string_view context) {
    if (object.type != JsonValue::Type::Object) {
        return Status::error(StatusCode::InvalidJson, std::string(context) + " must be an object");
    }
    for (const auto& [key, value] : object.object) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            return Status::error(StatusCode::InvalidJson,
                "unknown field '" + key + "' in " + std::string(context));
        }
    }
    return Status::ok_status();
}

Status parse_metronome(const JsonValue& root, SongConfig* config) {
    const JsonValue* metronome = find_member(root, "metronome");
    if (!metronome) return Status::ok_status();
    if (metronome->type != JsonValue::Type::Object) {
        return Status::error(StatusCode::InvalidJson, "field 'metronome' must be an object");
    }
    Status status = reject_unknown_members(
        *metronome, {"enabled", "level", "beat_zero_offset_seconds"}, "field 'metronome'");
    if (!status.ok()) return status;
    if (const JsonValue* enabled = find_member(*metronome, "enabled")) {
        if (enabled->type != JsonValue::Type::Bool) {
            return Status::error(StatusCode::InvalidJson, "field 'metronome.enabled' must be a boolean");
        }
        config->metronome_enabled = enabled->boolean;
    }
    if (const JsonValue* level = find_member(*metronome, "level")) {
        if (level->type != JsonValue::Type::Number || !std::isfinite(level->number) ||
            level->number < 0.0 || level->number > 1.0) {
            return Status::error(StatusCode::InvalidJson,
                "field 'metronome.level' must be a finite number between 0 and 1");
        }
        config->metronome_level = level->number;
    }
    if (const JsonValue* offset = find_member(*metronome, "beat_zero_offset_seconds")) {
        if (offset->type != JsonValue::Type::Number || !std::isfinite(offset->number) ||
            offset->number < -30.0 || offset->number > 30.0) {
            return Status::error(StatusCode::InvalidJson,
                "field 'metronome.beat_zero_offset_seconds' must be a finite number between -30 and 30");
        }
        config->metronome_beat_zero_offset_seconds = offset->number;
        config->metronome_beat_zero_offset_provided = true;
    }
    if (config->metronome_enabled && config->metronome_level <= 0.0) {
        return Status::error(StatusCode::InvalidJson,
            "field 'metronome.level' must be greater than 0 when metronome is enabled");
    }
    return Status::ok_status();
}

Status parse_gain_envelope(const JsonValue& root, SongConfig* config) {
    const JsonValue* envelope = find_member(root, "gain_envelope");
    if (!envelope) return Status::ok_status();
    if (envelope->type != JsonValue::Type::Array) {
        return Status::error(StatusCode::InvalidJson, "field 'gain_envelope' must be an array");
    }
    if (envelope->array.empty() || envelope->array.size() > kMaximumGainEnvelopePoints) {
        return Status::error(StatusCode::InvalidJson,
            "field 'gain_envelope' must contain between 1 and 64 points when present");
    }
    std::vector<GainEnvelopePoint> points;
    points.reserve(envelope->array.size());
    for (std::size_t index = 0; index < envelope->array.size(); ++index) {
        const JsonValue& value = envelope->array[index];
        if (value.type != JsonValue::Type::Object) {
            return Status::error(StatusCode::InvalidJson,
                "each gain_envelope point must be an object; invalid point index " + std::to_string(index));
        }
        Status status = reject_unknown_members(
            value, {"time_seconds", "gain_db"}, "gain_envelope point index " + std::to_string(index));
        if (!status.ok()) return status;
        GainEnvelopePoint point;
        status = require_number(value, "time_seconds", &point.time_seconds);
        if (!status.ok()) {
            status.message += " in gain_envelope point index " + std::to_string(index);
            return status;
        }
        status = require_number(value, "gain_db", &point.gain_db);
        if (!status.ok()) {
            status.message += " in gain_envelope point index " + std::to_string(index);
            return status;
        }
        points.push_back(point);
    }
    const Status status = validate_gain_envelope(points);
    if (!status.ok()) return Status::error(StatusCode::InvalidJson, status.message);
    config->gain_envelope = std::move(points);
    return Status::ok_status();
}

Status validate_schema(const JsonValue& root, SongConfig* out_config) {
    const Status keys = reject_unknown_members(root,
        {"schema", "title", "bpm", "difficulty", "score_thresholds", "mode_change_combo_counts",
            "midi_audio_offset_seconds", "midi_audio_alignment_seconds", "midi_minimum_lead_in_seconds",
            "loudness_normalization", "loudness_target_lufs", "loudness_peak_ceiling_dbfs",
            "gain_envelope", "metronome", "notes", "profiles", "diagnostic_extended_chart_fixture"},
        "song root");
    if (!keys.ok()) return keys;
    const JsonValue* schema = find_member(root, "schema");
    if (!schema) {
        return Status::error(StatusCode::InvalidJson, "missing required field 'schema'");
    }
    if (schema->type == JsonValue::Type::String) {
        if (schema->string != "ff7rpianosongs.song.v2" && schema->string != "v2") {
            return Status::error(StatusCode::InvalidJson, "unsupported schema '" + schema->string + "'; expected 'ff7rpianosongs.song.v2'");
        }
        out_config->schema = schema->string;
        return Status::ok_status();
    }
    if (schema->type == JsonValue::Type::Number && std::fabs(schema->number - 2.0) < 0.000001) {
        out_config->schema = "ff7rpianosongs.song.v2";
        return Status::ok_status();
    }
    return Status::error(StatusCode::InvalidJson, "field 'schema' must be 'ff7rpianosongs.song.v2'");
}

} // namespace

Status parse_song_json_string(const std::string& json, ParsedSongSource* out_source) {
    if (!out_source) {
        return Status::error(StatusCode::InvalidArgument, "out_source must not be null");
    }

    JsonValue root;
    std::string error;
    JsonParser parser(json);
    if (!parser.parse(&root, &error)) {
        return Status::error(StatusCode::InvalidJson, error);
    }
    if (root.type != JsonValue::Type::Object) {
        return Status::error(StatusCode::InvalidJson, "song JSON root must be an object");
    }

    SongConfig config;
    Status status = validate_schema(root, &config);
    if (!status.ok()) {
        return status;
    }

    status = require_string(root, "title", &config.title);
    if (!status.ok()) {
        return status;
    }
    if (config.title.empty()) {
        return Status::error(StatusCode::InvalidJson, "field 'title' must not be empty");
    }

    if (const JsonValue* bpm = find_member(root, "bpm")) {
        if (bpm->type != JsonValue::Type::Number || !std::isfinite(bpm->number) ||
            bpm->number < 30.0 || bpm->number > 300.0) {
            return Status::error(StatusCode::InvalidJson, "field 'bpm' must be between 30 and 300");
        }
        config.bpm = bpm->number;
        config.bpm_provided = true;
    }

    if (find_member(root, "difficulty")) {
        status = parse_non_negative_integer(root, "difficulty", &config.difficulty);
        if (!status.ok()) return status;
    }

    config.score_thresholds_provided = find_member(root, "score_thresholds") != nullptr;
    status = parse_int_array(root, "score_thresholds", &config.score_thresholds);
    if (!status.ok()) {
        return status;
    }
    config.mode_change_combo_counts_provided = find_member(root, "mode_change_combo_counts") != nullptr;
    status = parse_int_array(root, "mode_change_combo_counts", &config.mode_change_combo_counts);
    if (!status.ok()) {
        return status;
    }
    const auto parse_midi_offset = [&root](const char* key, double* out, bool* provided) -> Status {
        if (const JsonValue* value = find_member(root, key)) {
            if (value->type != JsonValue::Type::Number || !std::isfinite(value->number) ||
                value->number < -1.0 || value->number > 1.0) {
                return Status::error(StatusCode::InvalidJson,
                    std::string("field '") + key + "' must be a finite number between -1 and 1");
            }
            *out = value->number;
            *provided = true;
        }
        return Status::ok_status();
    };
    status = parse_midi_offset("midi_audio_offset_seconds", &config.midi_audio_offset_seconds,
        &config.midi_audio_offset_provided);
    if (!status.ok()) {
        return status;
    }
    status = parse_midi_offset("midi_audio_alignment_seconds", &config.midi_audio_alignment_seconds,
        &config.midi_audio_alignment_provided);
    if (!status.ok()) {
        return status;
    }
    if (const JsonValue* lead_in = find_member(root, "midi_minimum_lead_in_seconds")) {
        if (lead_in->type != JsonValue::Type::Number || !std::isfinite(lead_in->number) ||
            lead_in->number < 0.0 || lead_in->number > 30.0) {
            return Status::error(StatusCode::InvalidJson,
                "field 'midi_minimum_lead_in_seconds' must be between 0 and 30");
        }
        config.midi_minimum_lead_in_seconds = lead_in->number;
    }
    if (const JsonValue* enabled = find_member(root, "loudness_normalization")) {
        if (enabled->type != JsonValue::Type::Bool) {
            return Status::error(StatusCode::InvalidJson, "field 'loudness_normalization' must be a boolean");
        }
        config.loudness_normalization = enabled->boolean;
    }
    if (const JsonValue* target = find_member(root, "loudness_target_lufs")) {
        if (target->type != JsonValue::Type::Number || !std::isfinite(target->number) ||
            target->number < -30.0 || target->number > -5.0) {
            return Status::error(StatusCode::InvalidJson, "field 'loudness_target_lufs' must be between -30 and -5");
        }
        config.loudness_target_lufs = target->number;
    }
    if (const JsonValue* ceiling = find_member(root, "loudness_peak_ceiling_dbfs")) {
        if (ceiling->type != JsonValue::Type::Number || !std::isfinite(ceiling->number) ||
            ceiling->number < -6.0 || ceiling->number > 0.0) {
            return Status::error(StatusCode::InvalidJson,
                "field 'loudness_peak_ceiling_dbfs' must be between -6 and 0");
        }
        config.loudness_peak_ceiling_dbfs = ceiling->number;
    }
    status = parse_gain_envelope(root, &config);
    if (!status.ok()) {
        return status;
    }
    status = parse_metronome(root, &config);
    if (!status.ok()) {
        return status;
    }
    status = parse_notes(root, &config.notes, &config.notes_provided);
    if (!status.ok()) {
        return status;
    }
    if (const JsonValue* fixture = find_member(root, "diagnostic_extended_chart_fixture")) {
        if (fixture->type != JsonValue::Type::Bool) {
            return Status::error(StatusCode::InvalidJson,
                "field 'diagnostic_extended_chart_fixture' must be a boolean");
        }
        config.diagnostic_extended_chart_fixture = fixture->boolean;
    }
    std::vector<AuthoredDifficultyProfile> authored_profiles;
    if (const JsonValue* profiles = find_member(root, "profiles")) {
        if (find_member(root, "notes") || find_member(root, "difficulty")) {
            return Status::error(StatusCode::InvalidJson,
                "field 'profiles' cannot coexist with root 'notes' or 'difficulty'");
        }
        if (profiles->type != JsonValue::Type::Array) {
            return Status::error(StatusCode::InvalidJson, "field 'profiles' must be an array");
        }
        if (profiles->array.empty() || profiles->array.size() > kMaximumDifficultyProfiles) {
            return Status::error(StatusCode::InvalidJson,
                "field 'profiles' must contain between 1 and 32 profiles");
        }
        if (!config.bpm_provided) {
            return Status::error(StatusCode::InvalidJson,
                "field 'bpm' is required when explicit profiles are provided");
        }
        authored_profiles.reserve(profiles->array.size());
        int previous_difficulty = -1;
        for (std::size_t index = 0; index < profiles->array.size(); ++index) {
            const JsonValue& profile_object = profiles->array[index];
            status = reject_unknown_members(
                profile_object, {"difficulty", "notes"}, "profile index " + std::to_string(index));
            if (!status.ok()) return status;
            AuthoredDifficultyProfile profile;
            status = parse_non_negative_integer(profile_object, "difficulty", &profile.difficulty);
            if (!status.ok()) {
                status.message += " in profile index " + std::to_string(index);
                return status;
            }
            bool notes_provided = false;
            status = parse_notes(profile_object, &profile.notes, &notes_provided);
            if (!status.ok()) {
                status.message += " in profile index " + std::to_string(index);
                return status;
            }
            if (!notes_provided) {
                return Status::error(StatusCode::InvalidJson,
                    "missing required field 'notes' in profile index " + std::to_string(index));
            }
            if (profile.difficulty <= previous_difficulty) {
                return Status::error(StatusCode::InvalidJson,
                    "profile difficulties must be unique and strictly increasing; invalid profile index " +
                    std::to_string(index));
            }
            previous_difficulty = profile.difficulty;
            authored_profiles.push_back(std::move(profile));
        }
        config.difficulty = authored_profiles.front().difficulty;
        config.notes = authored_profiles.front().notes;
        config.notes_provided = true;
    }
    if (config.diagnostic_extended_chart_fixture &&
        (!authored_profiles.empty() || !config.notes_provided
            || (config.notes.size() != kPlayable513ChartRows && config.notes.size() != 520u))) {
        return Status::error(StatusCode::InvalidJson,
            "diagnostic_extended_chart_fixture requires exactly 513 or 520 root explicit notes");
    }

    out_source->config = std::move(config);
    out_source->authored_profiles = std::move(authored_profiles);
    return Status::ok_status();
}

Status parse_song_json_string(const std::string& json, SongConfig* out_config) {
    if (!out_config) {
        return Status::error(StatusCode::InvalidArgument, "out_config must not be null");
    }
    ParsedSongSource source;
    Status status = parse_song_json_string(json, &source);
    if (status.ok()) *out_config = std::move(source.config);
    return status;
}

Status load_song_json_file(const std::string& path, ParsedSongSource* out_source) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Status::error(StatusCode::NotFound, "failed to open song JSON: " + path);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        return Status::error(StatusCode::IoError, "failed to read song JSON: " + path);
    }
    Status status = parse_song_json_string(buffer.str(), out_source);
    if (!status.ok()) {
        status.message = path + ": " + status.message;
    }
    return status;
}

Status load_song_json_file(const std::string& path, SongConfig* out_config) {
    if (!out_config) {
        return Status::error(StatusCode::InvalidArgument, "out_config must not be null");
    }
    ParsedSongSource source;
    Status status = load_song_json_file(path, &source);
    if (status.ok()) *out_config = std::move(source.config);
    return status;
}

} // namespace ff7rp::pipeline
