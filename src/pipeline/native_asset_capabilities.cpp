#include "native_asset_capabilities.h"

#include "core/generated/build_identity.generated.h"

namespace ff7rp::pipeline {

NativeAssetCapabilities native_asset_capabilities_for_catalog(const std::string_view build_id) {
    if (build_id == "ff7rebirth-steam-win64-68fd6fde"
        || build_id == "ff7rebirth-steam-win64-6a16ced2") {
        // Shared stock Db asset parity does not authorize the authored projection
        // seam. Only exact 1.005 has both sound inventory and runtime evidence.
        return {true, "native_assets=pca_Db_voicing:verified1004+1005",
            build_id == "ff7rebirth-steam-win64-6a16ced2"};
    }
    return {false, "native_assets=pca_Db_voicing:unverified_unknown"};
}

NativeAssetCapabilities selected_native_asset_capabilities() {
    return native_asset_capabilities_for_catalog(ff7r::piano::core::generated::kBuildId);
}

} // namespace ff7rp::pipeline
