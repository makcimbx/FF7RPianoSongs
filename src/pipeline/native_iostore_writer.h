#pragma once
#include "native_zen_package.h"
#include <array>

namespace ff7rp::pipeline {
struct NativeIoStorePackageInput {
    NativeAudioPackageRole role{};
    native_binary::Bytes zen;
};
struct NativeIoStoreBuildResult {
    Status status;
    native_binary::Bytes utoc, ucas;
    std::uint64_t container_id=0;
    std::array<NativeAudioPackage,4> packages{};
};
// UE4_26 DirectoryIndex TOC + Initial ContainerHeader. Four qualified single-export
// packages, no compression/encryption/redirects/imported packages. No mount authority.
NativeIoStoreBuildResult build_native_iostore(const std::string& container_name,
    const std::array<NativeIoStorePackageInput,4>& packages);
Status verify_native_iostore(const std::string& container_name,
    const std::array<NativeIoStorePackageInput,4>& packages,
    const native_binary::Bytes& utoc, const native_binary::Bytes& ucas);
} // namespace ff7rp::pipeline
