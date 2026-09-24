#pragma once

#include <string>

#include "song_types.h"
#include "native_asset_capabilities.h"

namespace ff7rp::pipeline {

inline constexpr const char* kResolvedSongJsonFilename = "resolved-song.json";

std::string resolved_song_json_path(const std::string& song_directory);

// Renders a source-compatible, non-authoritative projection of the final
// playable rows. source_declared_profiles preserves the authored one-profile
// shape, which cannot otherwise be distinguished from root notes after load.
Status render_resolved_song_json(
    const LoadedSong& song,
    NativeAssetCapabilities native_assets,
    bool source_declared_profiles,
    std::string* out_json);

} // namespace ff7rp::pipeline
