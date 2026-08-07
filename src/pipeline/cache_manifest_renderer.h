#pragma once

#include <string>

#include "song_types.h"

namespace ff7rp::pipeline {

Status render_cache_manifest(
    const LoadedSong& song,
    const SongConfig& source_config,
    std::string* manifest);

} // namespace ff7rp::pipeline
