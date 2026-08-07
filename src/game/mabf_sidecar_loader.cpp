#include "game/mabf_sidecar_loader.h"

#include "pipeline/mabf_builder.h"
#include "pipeline/pipeline_limits.h"

#include <fstream>
#include <utility>

namespace ff7r::piano::game {

SidecarLoadResult load_sidecar_bytes_file(const std::wstring& path)
{
    SidecarLoadResult result;
    if (path.empty()) {
        result.status = "empty_path";
        return result;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        result.status = "missing";
        return result;
    }

    const std::streampos end = file.tellg();
    if (end < 0) {
        result.status = "size_failed";
        return result;
    }

    const auto size = static_cast<std::size_t>(end);
    if (size > ff7rp::pipeline::kMaxMabfBytes) {
        result.status = "size_too_large";
        return result;
    }
    if (size < ff7rp::pipeline::kMabfHeaderSize
            + 3u * (ff7rp::pipeline::kMabfHcaHeaderSize + ff7rp::pipeline::kMabfTrailerSize)) {
        result.status = "size_too_small";
        return result;
    }

    std::vector<std::uint8_t> bytes(size);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        result.status = "read_failed";
        return result;
    }

    ff7rp::pipeline::MabfArtifactMetadata metadata;
    if (!ff7rp::pipeline::validate_structural_mabf(bytes, &metadata).ok()) {
        result.status = "structural_validation_failed";
        return result;
    }

    result.ok = true;
    result.status = "ok";
    result.bytes = std::move(bytes);
    return result;
}

} // namespace ff7r::piano::game
