#pragma once

#include <string_view>

namespace ff7rp::pipeline {

class NativeAssetCapabilities {
public:
    constexpr bool has_verified_pca_db_voicing() const { return verified_pca_db_voicing_; }
    constexpr bool has_verified_authored_chord_voicing() const { return authored_chord_voicing_; }
    constexpr std::string_view cache_identity() const { return cache_identity_; }

private:
    constexpr NativeAssetCapabilities(
        const bool verified_pca_db_voicing, const std::string_view cache_identity,
        const bool authored_chord_voicing = false)
        : verified_pca_db_voicing_(verified_pca_db_voicing), cache_identity_(cache_identity),
          authored_chord_voicing_(authored_chord_voicing) {}

    bool verified_pca_db_voicing_ = false;
    std::string_view cache_identity_;
    bool authored_chord_voicing_ = false;

    friend NativeAssetCapabilities native_asset_capabilities_for_catalog(std::string_view build_id);
};

// Resolve only exact generated catalog identities. Version strings and other
// descriptive metadata are deliberately not accepted as asset authority.
NativeAssetCapabilities native_asset_capabilities_for_catalog(std::string_view build_id);
NativeAssetCapabilities selected_native_asset_capabilities();

} // namespace ff7rp::pipeline
