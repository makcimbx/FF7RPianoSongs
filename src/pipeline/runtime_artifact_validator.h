#pragma once

#include <functional>
#include <string>

#include "song_types.h"

namespace ff7rp::pipeline {

using RuntimeArtifactTrace = std::function<void(const char* stage)>;

bool runtime_artifacts_match(
    const LoadedSong& song,
    const SongConfig& source_config,
    const RuntimeArtifactTrace& trace,
    std::string* failure_reason = nullptr);

} // namespace ff7rp::pipeline
