#pragma once

#include <windows.h>

namespace ff7r::piano::proxy {

enum class ForwardingStatus {
    Ready, InProgress, UnsupportedBasename, InvalidSelf, SystemDirectoryFailed,
    LoadFailed, WrongModule, MissingExport, InvalidTarget, FailedPreviously
};

// Call only outside DllMain/loader lock. No initialization is attempted by exports.
// One payload supports d3dcompiler_43/d3d9/d3d11/dsound/winmm/dxgi/xinput1_3.dll.
// All EXE-required imports must resolve before atomic publication; additional
// conventional exports are enabled only when present and validated.
// Other names/ordinals in the static union fail-fast, not fabricated success.
// Success retains the genuine module reference until process exit; no unload API.
// Keep this proxy resident too. This API does not detect loader lock or establish
// a cold game-entry context; the entry bridge must qualify those prerequisites.
// Named exports have union ordinals 256..307; compatibility ordinal thunks cover
// 1..38 and 100..103 only. Each profile enables only its explicitly listed subset.
// Absent optional targets remain fail-fast. The static union cannot reproduce
// GetProcAddress-based feature detection or full per-DLL/other-mod compatibility:
// a union name remains exported even when its backing target is unavailable.
ForwardingStatus initialize_forwarding(HMODULE self) noexcept;
bool forwarding_ready() noexcept;

} // namespace ff7r::piano::proxy
