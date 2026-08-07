#pragma once

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline::cache_artifact_writer {

Status write_binary_file(const std::string& path, const std::vector<std::uint8_t>& bytes);
std::error_code create_parent_directories(const std::string& path);
bool write_binary_file_unreported(const std::string& path, const std::vector<std::uint8_t>& bytes);
void write_last_error(const std::string& song_directory, const Status& status);
void clear_last_error(const std::string& song_directory);

} // namespace ff7rp::pipeline::cache_artifact_writer
