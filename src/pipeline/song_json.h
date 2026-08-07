#pragma once

#include <string>

#include "song_types.h"

namespace ff7rp::pipeline {

Status parse_song_json_string(const std::string& json, SongConfig* out_config);
Status load_song_json_file(const std::string& path, SongConfig* out_config);

} // namespace ff7rp::pipeline
