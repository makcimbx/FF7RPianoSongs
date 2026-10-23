#include "core/config.h"
#include "core/theme.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <iterator>

namespace ff7r::piano::core {
namespace {

LogLevel parse_log_level(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    if (value == L"debug") {
        return LogLevel::Debug;
    }
    if (value == L"error") {
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

} // namespace

Config load_config(const std::wstring& ini_path)
{
    Config config;
    config.enabled = GetPrivateProfileIntW(L"General", L"Enabled", 1, ini_path.c_str()) != 0;
    config.rebuild_audio_cache = GetPrivateProfileIntW(L"Advanced", L"RebuildAudioCache", 0, ini_path.c_str()) != 0;
    config.transcription_threads = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Advanced", L"TranscriptionThreads", 4, ini_path.c_str())), 1, 64);
    const int autoplay_key = static_cast<int>(GetPrivateProfileIntW(
        L"Autoplay", L"KeyboardFunctionKey", 8, ini_path.c_str()));
    config.autoplay_function_key = autoplay_key >= 0 && autoplay_key <= 12 ? autoplay_key : 8;

    wchar_t preview_mode[32]{};
    GetPrivateProfileStringW(L"Preview", L"Mode", L"auto", preview_mode,
        static_cast<DWORD>(std::size(preview_mode)), ini_path.c_str());
    if (_wcsicmp(preview_mode, L"manual") == 0) config.preview_mode = PreviewMode::Manual;
    else if (_wcsicmp(preview_mode, L"off") == 0) config.preview_mode = PreviewMode::Off;
    const int preview_key = static_cast<int>(GetPrivateProfileIntW(
        L"Preview", L"KeyboardFunctionKey", 7, ini_path.c_str()));
    config.preview_function_key = preview_key >= 0 && preview_key <= 12 ? preview_key : 7;

    wchar_t selected_theme[66]{};
    GetPrivateProfileStringW(L"Theme", L"Selected", L"original", selected_theme,
        static_cast<DWORD>(std::size(selected_theme)), ini_path.c_str());
    std::string theme_id;
    for (const wchar_t ch : std::wstring_view(selected_theme)) {
        if (ch > 127) { theme_id.clear(); break; }
        theme_id.push_back(static_cast<char>(ch));
    }
    if (valid_theme_id(theme_id)) config.selected_theme = theme_id;
    const int theme_key = static_cast<int>(GetPrivateProfileIntW(
        L"Theme", L"KeyboardFunctionKey", 6, ini_path.c_str()));
    config.theme_function_key = theme_key >= 0 && theme_key <= 12 ? theme_key : 6;

    wchar_t level[32]{};
    GetPrivateProfileStringW(L"General", L"LogLevel", L"info", level, static_cast<DWORD>(std::size(level)), ini_path.c_str());
    config.log_level = parse_log_level(level);
    return config;
}

bool save_selected_theme(const std::wstring& ini_path, const std::string& theme_id)
{
    if (!valid_theme_id(theme_id)) return false;
    const std::wstring value(theme_id.begin(), theme_id.end());
    return WritePrivateProfileStringW(L"Theme", L"Selected", value.c_str(), ini_path.c_str()) != FALSE;
}

} // namespace ff7r::piano::core
