#pragma once

#include <windows.h>

#include <cstdint>

#include "core/generated/build_identity.generated.h"
#include "core/pe_image.h"

namespace ff7r::piano::core {

constexpr uint32_t kExpectedExeTimestamp = generated::kExeTimestamp;
constexpr uint32_t kExpectedExeSizeOfImage = generated::kSizeOfImage;
constexpr uint32_t kExpectedExeChecksum = generated::kPeChecksum;

bool is_supported_exe(HMODULE exe_module, PeIdentity* actual_identity = nullptr);

} // namespace ff7r::piano::core
