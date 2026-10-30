#pragma once

#include <windows.h>

namespace ff7r::piano::game {

bool install_native_protection_hooks(HMODULE exe_module,
    void (*native_initialized)() noexcept = nullptr);
bool install_feature_hooks(HMODULE exe_module);
bool shutdown_release_hooks();

} // namespace ff7r::piano::game
