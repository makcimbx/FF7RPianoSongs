#include "core/version_gate.h"

#include "core/pe_image.h"

namespace ff7r::piano::core {

bool is_supported_exe(HMODULE exe_module, PeIdentity* actual_identity)
{
    const PeIdentity actual = pe_identity(exe_module);
    if (actual_identity) {
        *actual_identity = actual;
    }
    return actual.timestamp == kExpectedExeTimestamp
        && actual.size_of_image == kExpectedExeSizeOfImage
        && actual.checksum == kExpectedExeChecksum;
}

} // namespace ff7r::piano::core
