#pragma once

#include <filesystem>
#include <string>

#include "song_types.h"

namespace ff7rp::pipeline {

std::string render_default_song_json(const std::filesystem::path& directory);
Status create_default_song_json(const std::filesystem::path& directory);

} // namespace ff7rp::pipeline
