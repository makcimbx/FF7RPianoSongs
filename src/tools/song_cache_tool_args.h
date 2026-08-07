#pragma once

#include <charconv>
#include <string>
#include <string_view>

namespace ff7rp::tools {

inline bool parse_dump_difficulty(
    const std::string_view text, int* value, std::string* error)
{
    if (!value || text.empty()) {
        if (error) *error = "difficulty must be an integer from 1 through 6";
        return false;
    }
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || parsed < 1 || parsed > 6) {
        if (error) *error = "difficulty must be an integer from 1 through 6 with no trailing characters";
        return false;
    }
    *value = parsed;
    return true;
}

} // namespace ff7rp::tools
