#include "core/config.h"

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
    config.experimental_extended_charts =
        GetPrivateProfileIntW(L"Experimental", L"ExtendedCharts", 0, ini_path.c_str()) != 0;

    wchar_t level[32]{};
    GetPrivateProfileStringW(L"General", L"LogLevel", L"info", level, static_cast<DWORD>(std::size(level)), ini_path.c_str());
    config.log_level = parse_log_level(level);
    return config;
}

} // namespace ff7r::piano::core
