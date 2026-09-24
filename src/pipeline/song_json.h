#pragma once

#include <string>

#include "song_types.h"
#include "native_asset_capabilities.h"

namespace ff7rp::pipeline {

Status parse_song_json_string(const std::string& json, NativeAssetCapabilities native_assets,
    SongConfig* out_config);
Status load_song_json_file(const std::string& path, NativeAssetCapabilities native_assets,
    SongConfig* out_config);
Status parse_song_json_string(const std::string& json, NativeAssetCapabilities native_assets,
    ParsedSongSource* out_source);
Status load_song_json_file(const std::string& path, NativeAssetCapabilities native_assets,
    ParsedSongSource* out_source);

} // namespace ff7rp::pipeline
