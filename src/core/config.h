#pragma once

#include "core/logging.h"

#include <string>

namespace ff7r::piano::core {

struct Config {
    bool enabled = true;
    LogLevel log_level = LogLevel::Info;
    bool rebuild_audio_cache = false;
    bool experimental_extended_charts = false;
};

Config load_config(const std::wstring& ini_path);

} // namespace ff7r::piano::core
