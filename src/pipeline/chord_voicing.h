#pragma once

#include <algorithm>
#include "native_chord_constituents.h"
#include "song_types.h"

namespace ff7rp::pipeline {

// Exhaustive ordinary SoundName inventory from
// analysis/PianoMonotoneAssignEvidence-20260905.md: 17 spellings in 1-6,
// plus Cn7. Alternate *_2 rows reference these sounds, not new sound IDs.
inline bool is_verified_native_sound(const std::string_view sound) {
    constexpr std::array<std::string_view, 17> prefixes{
        "Cn", "Cs", "Db", "Dn", "Ds", "Eb", "En", "Fn", "Fs",
        "Gb", "Gn", "Gs", "Ab", "An", "As", "Bb", "Bn"};
    return sound == "Cn7" || (sound.size() == 3u && sound[2] >= '1' && sound[2] <= '6' &&
        std::find(prefixes.begin(), prefixes.end(), sound.substr(0, 2)) != prefixes.end());
}

inline Status validate_chord_voicings(const SongConfig& config,
    const NativeAssetCapabilities assets = selected_native_asset_capabilities()) {
    if (config.chord_voicings.empty()) return Status::ok_status();
    if (!config.notes_provided) return Status::error(StatusCode::InvalidChart,
        "chord_voicings requires explicit notes or profiles; export resolved-song.json then author the chart");
    if (!assets.has_verified_authored_chord_voicing()) return Status::error(StatusCode::InvalidChart,
        "chord_voicings requires verified exact 1.005 authored-voicing capability");
    if (config.chord_voicings.size() > kVerifiedNativeChordConstituents.size())
        return Status::error(StatusCode::InvalidChart, "chord_voicings exceeds the verified chord inventory");
    std::string_view previous;
    for (const auto& voicing : config.chord_voicings) {
        const auto* stock = find_verified_native_chord(voicing.chord_id, assets);
        if (!stock) return Status::error(StatusCode::InvalidChart,
            "chord_voicings has unverified chord ID '" + voicing.chord_id + "'");
        if (voicing.chord_id <= previous) return Status::error(StatusCode::InvalidChart,
            "chord_voicings keys must be unique and in canonical ascending order");
        previous = voicing.chord_id;
        if (voicing.sound_ids.empty() || voicing.sound_ids.size() > stock->sound_count)
            return Status::error(StatusCode::InvalidChart,
                "chord_voicings '" + voicing.chord_id + "' must contain 1 to " +
                std::to_string(stock->sound_count) + " sounds (stock velocity-slot width)");
        for (std::size_t i = 0; i < voicing.sound_ids.size(); ++i) {
            const auto& sound = voicing.sound_ids[i];
            if (!is_verified_native_sound(sound)) return Status::error(StatusCode::InvalidChart,
                "chord_voicings has unverified native sound '" + sound + "'");
            if (std::find(voicing.sound_ids.begin(), voicing.sound_ids.begin() + i, sound) !=
                voicing.sound_ids.begin() + i) return Status::error(StatusCode::InvalidChart,
                    "chord_voicings sounds must be unique within each chord");
        }
    }
    return Status::ok_status();
}

// Mapping validity belongs to validate_chord_voicings at the configuration
// boundary. This query preserves exact spelling and replaces, never adds to,
// the stock sound list.
inline bool effective_chord_contains_sound(const SongConfig& config,
    const std::string_view chord_id, const std::string_view sound,
    const NativeAssetCapabilities assets) {
    for (const auto& voicing : config.chord_voicings) {
        if (voicing.chord_id == chord_id)
            return std::find(voicing.sound_ids.begin(), voicing.sound_ids.end(), sound) != voicing.sound_ids.end();
    }
    const auto* stock = find_verified_native_chord(chord_id, assets);
    return stock && std::find(stock->sound_names.begin(), stock->sound_names.begin() + stock->sound_count,
        sound) != stock->sound_names.begin() + stock->sound_count;
}

} // namespace ff7rp::pipeline
