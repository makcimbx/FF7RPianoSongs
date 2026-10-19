#pragma once

#include "core/logging.h"

#include <string>

namespace ff7r::piano::core {

enum class PreviewMode { Auto, Manual, Off };

struct Config {
    bool enabled = true;
    LogLevel log_level = LogLevel::Info;
    bool rebuild_audio_cache = false;
    int transcription_threads = 4;
    int autoplay_function_key = 8;
    PreviewMode preview_mode = PreviewMode::Auto;
    int preview_function_key = 7;
};

Config load_config(const std::wstring& ini_path);

} // namespace ff7r::piano::core
