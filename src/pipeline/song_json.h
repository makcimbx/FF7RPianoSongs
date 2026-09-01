#pragma once

#include <string>

#include "song_types.h"

namespace ff7rp::pipeline {

Status parse_song_json_string(const std::string& json, SongConfig* out_config);
Status load_song_json_file(const std::string& path, SongConfig* out_config);
Status parse_song_json_string(const std::string& json, ParsedSongSource* out_source);
Status load_song_json_file(const std::string& path, ParsedSongSource* out_source);

} // namespace ff7rp::pipeline
