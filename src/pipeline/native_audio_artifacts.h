#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace ff7rp::pipeline {

// Repository IDs are opaque bytes; only generated native names must be ASCII.
inline bool valid_native_audio_identity(std::string_view identity) noexcept {
    if(identity.empty() || identity.size()>1024)return false;
    for(unsigned char c:identity)if(c<0x20 || c==0x7f)return false;
    return true;
}

enum class NativeAudioPackageRole : std::uint8_t {
    Music,
    BgmList,
    ScoreInfo,
    PianoScore,
};

struct NativeAudioPackage {
    NativeAudioPackageRole role{};
    std::string object_path;
    std::string class_path;
    std::uint64_t package_id = 0;
    std::uint64_t public_export_hash = 0;
};

struct NativeAudioArtifactFile {
    std::filesystem::path path;
    std::uint64_t bytes = 0;
    // First eight SHA-256 bytes, interpreted little-endian; not the cache FNV key.
    std::uint64_t content_hash = 0;
};

// Immutable output of offline construction/validation, not game admission.
// Content-addressed files must remain unchanged while a runtime owner holds them.
struct NativeAudioArtifactSet {
    std::string build_id;
    std::string identity;
    // Authenticated source-table carrier; independent of any prior UI activation.
    std::string base_score_name;
    std::filesystem::path container_prefix;
    std::uint64_t container_id = 0;
    std::array<NativeAudioPackage, 4> packages;
    std::array<NativeAudioArtifactFile, 3> files;
    std::string music_name;
    std::string bgm_key;
    std::string score_name;
};

} // namespace ff7rp::pipeline
