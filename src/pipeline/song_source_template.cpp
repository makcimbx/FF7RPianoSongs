#include "song_source_template.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ff7rp::pipeline {
namespace {

enum class CreateFileResult { Created, AlreadyExists, Failed };

std::string path_string(const std::filesystem::path& path) {
    return path.string();
}

CreateFileResult atomic_create_file(
    const std::filesystem::path& target,
    const std::string& contents,
    std::error_code* error) {
    static std::atomic<std::uint64_t> sequence{0};
#ifdef _WIN32
    const unsigned long process_id = GetCurrentProcessId();
#else
    const long process_id = static_cast<long>(::getpid());
#endif
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        const std::filesystem::path temporary = target.parent_path() /
            ("." + target.filename().string() + ".tmp." + std::to_string(process_id) + "." +
                std::to_string(sequence.fetch_add(1)));
#ifdef _WIN32
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) continue;
            if (error) *error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            return CreateFileResult::Failed;
        }
        DWORD written = 0;
        const bool wrote = contents.size() <= std::numeric_limits<DWORD>::max() &&
            WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) &&
            written == contents.size() && FlushFileBuffers(file);
        const DWORD write_error = wrote ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!wrote) {
            DeleteFileW(temporary.c_str());
            if (error) *error = std::error_code(static_cast<int>(write_error), std::system_category());
            return CreateFileResult::Failed;
        }
        if (MoveFileW(temporary.c_str(), target.c_str())) {
            if (error) error->clear();
            return CreateFileResult::Created;
        }
        const DWORD publish_error = GetLastError();
        DeleteFileW(temporary.c_str());
        if (publish_error == ERROR_FILE_EXISTS || publish_error == ERROR_ALREADY_EXISTS) {
            if (error) error->clear();
            return CreateFileResult::AlreadyExists;
        }
        if (error) *error = std::error_code(static_cast<int>(publish_error), std::system_category());
        return CreateFileResult::Failed;
#else
        const int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file < 0) {
            if (errno == EEXIST) continue;
            if (error) *error = std::error_code(errno, std::generic_category());
            return CreateFileResult::Failed;
        }
        std::size_t position = 0;
        while (position < contents.size()) {
            const ssize_t count = ::write(file, contents.data() + position, contents.size() - position);
            if (count <= 0) break;
            position += static_cast<std::size_t>(count);
        }
        const bool flushed = position == contents.size() && ::fsync(file) == 0;
        const bool closed = ::close(file) == 0;
        const bool wrote = flushed && closed;
        if (!wrote) {
            const int write_error = errno;
            ::unlink(temporary.c_str());
            if (error) *error = std::error_code(write_error, std::generic_category());
            return CreateFileResult::Failed;
        }
        if (::link(temporary.c_str(), target.c_str()) == 0) {
            ::unlink(temporary.c_str());
            if (error) error->clear();
            return CreateFileResult::Created;
        }
        const int publish_error = errno;
        ::unlink(temporary.c_str());
        if (publish_error == EEXIST) {
            if (error) error->clear();
            return CreateFileResult::AlreadyExists;
        }
        if (error) *error = std::error_code(publish_error, std::generic_category());
        return CreateFileResult::Failed;
#endif
    }
    if (error) *error = std::make_error_code(std::errc::file_exists);
    return CreateFileResult::Failed;
}

std::string escape_json_string(const std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (byte < 0x20) {
                escaped += "\\u00";
                escaped.push_back(kHex[(byte >> 4) & 0x0f]);
                escaped.push_back(kHex[byte & 0x0f]);
            } else {
                escaped.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    return escaped;
}

} // namespace

std::string render_default_song_json(const std::filesystem::path& directory) {
    const auto title_utf8 = directory.filename().u8string();
    const std::string_view title_bytes{
        reinterpret_cast<const char*>(title_utf8.data()), title_utf8.size()};
    std::ostringstream out;
    out << "{\n"
        << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
        << "  \"title\": \"" << escape_json_string(title_bytes) << "\",\n"
        << "  \"metronome\": { \"enabled\": true, \"level\": 0.12 }\n"
        << "}\n";
    return out.str();
}

Status create_default_song_json(const std::filesystem::path& directory) {
    const std::filesystem::path path = directory / "song.json";
    std::error_code ec;
    const CreateFileResult result = atomic_create_file(path, render_default_song_json(directory), &ec);
    if (result == CreateFileResult::Failed) return Status::error(StatusCode::IoError,
        "failed to atomically create default song JSON: " + path_string(path) + ": " + ec.message());
    return Status::ok_status();
}

} // namespace ff7rp::pipeline
