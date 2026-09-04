#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline {

constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ull;
constexpr const char* kPipelineCacheVersion = "ff7rpianosongs.pipeline.v45";

std::uint64_t fnv1a64_append(std::uint64_t hash, const void* data, std::size_t size);
std::uint64_t fnv1a64_string(const std::string& text);
Status fnv1a64_file(const std::string& path, std::uint64_t* out_hash, std::uint64_t seed = kFnv1a64OffsetBasis);
Status fnv1a64_files_and_strings(const std::vector<std::string>& file_paths, const std::vector<std::string>& strings, std::uint64_t* out_hash);
std::string hex64(std::uint64_t value);

std::string cache_directory_path(const std::string& song_directory);
std::string cache_manifest_path(const std::string& song_directory);
std::string cache_sidecar_mabf_path(const std::string& song_directory);
std::string cache_last_error_path(const std::string& song_directory);

} // namespace ff7rp::pipeline
