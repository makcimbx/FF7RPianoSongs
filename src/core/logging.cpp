#include "core/logging.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ff7r::piano::core {
namespace {

std::mutex g_log_mutex;
std::wstring g_log_path;
LogLevel g_log_level = LogLevel::Info;

const char* level_name(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return "error";
    case LogLevel::Info: return "info";
    case LogLevel::Debug: return "debug";
    }
    return "info";
}

} // namespace

std::wstring module_path(HMODULE module)
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(module, &path[0], static_cast<DWORD>(path.size()));
    while (size == path.size()) {
        path.resize(path.size() * 2);
        size = GetModuleFileNameW(module, &path[0], static_cast<DWORD>(path.size()));
    }
    path.resize(size);
    return path;
}

std::wstring parent_dir(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

std::string narrow(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], needed, nullptr, nullptr);
    return result;
}

std::wstring widen(const std::string& value)
{
    if (value.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], needed);
    return result;
}

std::string timestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

void set_log_path(const std::wstring& path)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_path = path;
    if (!g_log_path.empty()) {
        std::ofstream reset(g_log_path, std::ios::binary | std::ios::trunc);
    }
}

std::wstring log_directory()
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return parent_dir(g_log_path);
}

void set_log_level(LogLevel level)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_level = level;
}

LogLevel log_level()
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_log_level;
}

bool should_log(LogLevel level)
{
    return static_cast<int>(level) <= static_cast<int>(log_level());
}

void log(LogLevel level, const std::string& line)
{
    if (!should_log(level)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_path.empty()) {
        return;
    }

    std::ofstream out(g_log_path, std::ios::app | std::ios::binary);
    if (!out) {
        return;
    }
    out << timestamp() << " [" << level_name(level) << "] " << line << "\n";
}

} // namespace ff7r::piano::core
