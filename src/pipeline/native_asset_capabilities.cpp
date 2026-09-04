#include "native_asset_capabilities.h"

#include "core/generated/build_identity.generated.h"

namespace ff7rp::pipeline {

NativeAssetCapabilities native_asset_capabilities_for_catalog(const std::string_view build_id) {
    if (build_id == "ff7rebirth-steam-win64-6a16ced2") {
        return {true, "native_assets=pca_Db_voicing:verified1005"};
    }
    if (build_id == "ff7rebirth-steam-win64-68fd6fde") {
        return {false, "native_assets=pca_Db_voicing:unverified1004"};
    }
    return {false, "native_assets=pca_Db_voicing:unverified_unknown"};
}

NativeAssetCapabilities selected_native_asset_capabilities() {
    return native_asset_capabilities_for_catalog(ff7r::piano::core::generated::kBuildId);
}

} // namespace ff7rp::pipeline
