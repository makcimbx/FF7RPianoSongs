#pragma once

#include <cstdint>
#include <string_view>

namespace ff7rp::pipeline {

struct NativeNoteValue {
    std::uint8_t note_type = 0;
    std::uint8_t dot_type = 0;

    bool operator==(const NativeNoteValue&) const = default;
};

struct NoteValueOverride {
    NativeNoteValue value{};
    bool provided = false;

    bool operator==(const NoteValueOverride&) const = default;
};

inline constexpr bool supported_native_note_value(const NativeNoteValue value)
{
    return value.note_type <= 4u && value.dot_type <= 1u;
}

inline constexpr bool valid_note_value_override(const NoteValueOverride& value)
{
    return value.provided ? supported_native_note_value(value.value)
                          : value.value == NativeNoteValue{};
}

inline constexpr NativeNoteValue legacy_native_note_value(const double duration_beats)
{
    return {static_cast<std::uint8_t>(duration_beats >= 2.0 ? 2u : 3u), 0u};
}

inline constexpr NativeNoteValue resolved_native_note_value(
    const NoteValueOverride& override_value, const double duration_beats)
{
    return override_value.provided ? override_value.value : legacy_native_note_value(duration_beats);
}

inline constexpr bool native_note_value_from_name(
    const std::string_view name, NativeNoteValue* out)
{
    if (!out) return false;
    struct Entry { std::string_view name; NativeNoteValue value; };
    constexpr Entry entries[] = {
        {"whole", {0, 0}}, {"dotted_whole", {0, 1}},
        {"half", {1, 0}}, {"dotted_half", {1, 1}},
        {"quarter", {2, 0}}, {"dotted_quarter", {2, 1}},
        {"eighth", {3, 0}}, {"dotted_eighth", {3, 1}},
        {"sixteenth", {4, 0}}, {"dotted_sixteenth", {4, 1}},
    };
    for (const Entry& entry : entries) {
        if (entry.name == name) {
            *out = entry.value;
            return true;
        }
    }
    return false;
}

} // namespace ff7rp::pipeline
