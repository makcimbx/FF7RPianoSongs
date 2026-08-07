#pragma once

#include <windows.h>

namespace ff7r::piano::game {

bool install_release_hooks(HMODULE exe_module);
bool shutdown_release_hooks();

} // namespace ff7r::piano::game
