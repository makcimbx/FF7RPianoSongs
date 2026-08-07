#include "cache.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ff7rp::pipeline {

std::uint64_t fnv1a64_append(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnv1a64Prime;
    }
    return hash;
}

std::uint64_t fnv1a64_string(const std::string& text) {
    return fnv1a64_append(kFnv1a64OffsetBasis, text.data(), text.size());
}

Status fnv1a64_file(const std::string& path, std::uint64_t* out_hash, std::uint64_t seed) {
    if (!out_hash) {
        return Status::error(StatusCode::InvalidArgument, "out_hash must not be null");
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Status::error(StatusCode::NotFound, "failed to open file for hashing: " + path);
    }

    std::uint64_t hash = seed;
    std::array<char, 64 * 1024> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0) {
            hash = fnv1a64_append(hash, buffer.data(), static_cast<std::size_t>(count));
        }
    }
    if (!file.eof()) {
        return Status::error(StatusCode::IoError, "failed while hashing file: " + path);
    }

    *out_hash = hash;
    return Status::ok_status();
}

Status fnv1a64_files_and_strings(const std::vector<std::string>& file_paths, const std::vector<std::string>& strings, std::uint64_t* out_hash) {
    if (!out_hash) {
        return Status::error(StatusCode::InvalidArgument, "out_hash must not be null");
    }

    std::uint64_t hash = kFnv1a64OffsetBasis;
    for (const std::string& text : strings) {
        hash = fnv1a64_append(hash, text.data(), text.size());
        const char separator = '\0';
        hash = fnv1a64_append(hash, &separator, 1);
    }
    for (const std::string& path : file_paths) {
        Status status = fnv1a64_file(path, &hash, hash);
        if (!status.ok()) {
            return status;
        }
        const char separator = '\0';
        hash = fnv1a64_append(hash, &separator, 1);
    }

    *out_hash = hash;
    return Status::ok_status();
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

std::string cache_directory_path(const std::string& song_directory) {
    return (std::filesystem::path(song_directory) / ".cache").string();
}

std::string cache_manifest_path(const std::string& song_directory) {
    return (std::filesystem::path(cache_directory_path(song_directory)) / "manifest.json").string();
}

std::string cache_sidecar_mabf_path(const std::string& song_directory) {
    return (std::filesystem::path(cache_directory_path(song_directory)) / "song.mabf.bin").string();
}

std::string cache_last_error_path(const std::string& song_directory) {
    return (std::filesystem::path(cache_directory_path(song_directory)) / "last_error.txt").string();
}

} // namespace ff7rp::pipeline
