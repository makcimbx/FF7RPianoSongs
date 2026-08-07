#include "cache_artifact_writer.h"

#include "cache.h"
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ff7rp::pipeline::cache_artifact_writer {
namespace {

enum class WriteFailure {
    None,
    Open,
    Write,
    Close,
    Replace,
};

struct WriteResult {
    WriteFailure failure{WriteFailure::None};
    std::string replace_error;
};

bool atomic_replace_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& target,
    std::error_code* error) {
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) error->clear();
        return true;
    }
    if (error) *error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (error) *error = ec;
    return !ec;
#endif
}

WriteResult write_binary_file_body(
    const std::string& path,
    const std::vector<std::uint8_t>& bytes,
    const bool check_write_before_flush) {
    const std::string temporary = path + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return {WriteFailure::Open, {}};
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (check_write_before_flush && !out) return {WriteFailure::Write, {}};
    }
    out.flush();
    if (!out) return {WriteFailure::Write, {}};
    out.close();
    if (!out) return {WriteFailure::Close, {}};
    std::error_code ec;
    if (!atomic_replace_file(temporary, path, &ec)) {
        const std::string message = ec.message();
        std::filesystem::remove(temporary, ec);
        return {WriteFailure::Replace, message};
    }
    return {};
}

} // namespace

Status write_binary_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) {
        return Status::error(StatusCode::IoError, "failed to create cache directory for " + path + ": " + ec.message());
    }
    const WriteResult result = write_binary_file_body(path, bytes, false);
    switch (result.failure) {
    case WriteFailure::None:
        return Status::ok_status();
    case WriteFailure::Open:
        return Status::error(StatusCode::IoError, "failed to open cache file for writing: " + path);
    case WriteFailure::Write:
        return Status::error(StatusCode::IoError, "failed to write cache file: " + path);
    case WriteFailure::Close:
        return Status::error(StatusCode::IoError, "failed to close cache file: " + path);
    case WriteFailure::Replace:
        return Status::error(StatusCode::IoError, "failed to atomically replace cache file: " + result.replace_error);
    }
    return Status::error(StatusCode::IoError, "failed to write cache file: " + path);
}

std::error_code create_parent_directories(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    return ec;
}

bool write_binary_file_unreported(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    return write_binary_file_body(path, bytes, true).failure == WriteFailure::None;
}

void write_last_error(const std::string& song_directory, const Status& status) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(cache_directory_path(song_directory)), ec);
    std::ofstream out(cache_last_error_path(song_directory), std::ios::binary);
    if (out) {
        out << status.message << "\n";
    }
}

void clear_last_error(const std::string& song_directory) {
    std::error_code ec;
    std::filesystem::remove(cache_last_error_path(song_directory), ec);
}

} // namespace ff7rp::pipeline::cache_artifact_writer
