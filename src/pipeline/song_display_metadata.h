#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ff7rp::pipeline {

inline constexpr std::size_t kMaxSongDisplayMetadataBytes = 256u;

// Decode strictly before applying single-line display constraints.
inline bool valid_song_display_metadata(const std::string_view text) {
    if (text.size() > kMaxSongDisplayMetadataBytes) return false;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        std::uint32_t codepoint = first;
        unsigned remaining = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = first & 0x1fu; remaining = 1; minimum = 0x80u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = first & 0x0fu; remaining = 2; minimum = 0x800u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = first & 0x07u; remaining = 3; minimum = 0x10000u;
        } else if (first > 0x7fu) return false;
        if (remaining > text.size() - i) return false;
        while (remaining != 0u) {
            --remaining;
            const auto next = static_cast<unsigned char>(text[i++]);
            if (next < 0x80u || next > 0xbfu) return false;
            codepoint = (codepoint << 6u) | (next & 0x3fu);
        }
        if (codepoint < minimum || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
            codepoint < 0x20u || (codepoint >= 0x7fu && codepoint <= 0x9fu) ||
            codepoint == 0x2028u || codepoint == 0x2029u) return false;
    }
    return true;
}

} // namespace ff7rp::pipeline
