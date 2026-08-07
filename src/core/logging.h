#pragma once

#include <windows.h>

#include <mutex>
#include <string>

namespace ff7r::piano::core {

enum class LogLevel {
    Error = 0,
    Info = 1,
    Debug = 2,
};

std::wstring module_path(HMODULE module);
std::wstring parent_dir(const std::wstring& path);
std::string narrow(const std::wstring& value);
std::wstring widen(const std::string& value);
std::string timestamp();

void set_log_path(const std::wstring& path);
std::wstring log_directory();
void set_log_level(LogLevel level);
LogLevel log_level();
bool should_log(LogLevel level);
void log(LogLevel level, const std::string& line);

} // namespace ff7r::piano::core
