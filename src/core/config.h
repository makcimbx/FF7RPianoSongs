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
    std::string selected_theme = "original";
    int theme_function_key = 6;
};

Config load_config(const std::wstring& ini_path);
bool save_selected_theme(const std::wstring& ini_path, const std::string& theme_id);

} // namespace ff7r::piano::core
