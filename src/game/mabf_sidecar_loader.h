#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ff7r::piano::game {

struct SidecarLoadResult {
    bool ok = false;
    std::string status;
    std::vector<std::uint8_t> bytes;
};

SidecarLoadResult load_sidecar_bytes_file(const std::wstring& path);

} // namespace ff7r::piano::game
