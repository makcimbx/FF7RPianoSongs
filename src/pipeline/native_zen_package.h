#pragma once
#include "native_binary.h"
#include "native_audio_artifacts.h"
#include "song_types.h"

namespace ff7rp::pipeline {
struct NativeZenPackage {
    NativeAudioPackageRole role{};
    std::string package_name;
    std::string export_name;
    std::string class_path;
    std::uint64_t package_id=0, public_export_hash=0;
    std::vector<std::string> names;
    std::size_t import_at=0, export_at=0, bundle_at=0, body_at=0;
    std::uint32_t export_bundle_count=0;
};
// Bounded UE4 Initial single-export, script-only imports, no package arcs/bulk.
Status inspect_native_zen_package(const native_binary::Bytes&, NativeAudioPackageRole, NativeZenPackage&);
// Preserves import/class/template metadata and supplied body; rebuilds NameMap,
// hashes, mapped identities, offsets, and two independent Create/Serialize bundles.
Status rebuild_native_zen_package(const native_binary::Bytes& source, NativeAudioPackageRole,
    const std::string& package_name, const std::string& export_name,
    const std::vector<std::string>& names, const native_binary::Bytes& body, native_binary::Bytes& output);
std::uint64_t native_object_index(std::string path, unsigned type);
} // namespace ff7rp::pipeline
