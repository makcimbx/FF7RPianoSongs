#pragma once
#include <windows.h>

namespace ff7r::piano::proxy {
inline constexpr const wchar_t* kProfileNames[] = {
    L"d3dcompiler_43.dll", L"d3d9.dll", L"d3d11.dll", L"dsound.dll",
    L"winmm.dll", L"dxgi.dll", L"xinput1_3.dll"
};
// Minimal static process-attach transport; never initializes the product here.
BOOL attach_process_start(HMODULE module, DWORD reason, LPVOID reserved) noexcept;
// Internal same-image callback, after restoration and System32 forwarding.
bool initialize_owned_product(HMODULE executable, HMODULE self) noexcept;
}
