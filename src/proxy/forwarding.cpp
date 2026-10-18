#include "proxy/forwarding.h"

#include <array>
#include <cstddef>
#include <cwchar>

extern "C" {
INT_PTR WINAPI native_proxy_fail_fast();
// Assembly reads one aligned pointer, published only after the table is complete.
__declspec(align(8)) PVOID native_proxy_targets = nullptr;
}

namespace ff7r::piano::proxy {
namespace {
enum Profile : unsigned { Compiler=1, D3d9=2, D3d11=4, Sound=8, Winmm=16, Dxgi=32, Xinput=64 };
struct Name { const char* name; unsigned profiles; unsigned required_profiles = 0; };
// Order is shared with forwarding_thunks.asm and forwarding.def; test the PE table.
// Required masks follow NativeProxyBootstrap-20261018.json's EXE import contract;
// Sound/XInput names also authenticate the required ordinal-to-name relationship.
constexpr Name names[] = {
    {"D3DAssemble",Compiler}, {"DebugSetMute",Compiler|D3d9}, {"D3DCompile",Compiler},
    {"D3DCompressShaders",Compiler}, {"D3DCreateBlob",Compiler}, {"D3DDecompressShaders",Compiler},
    {"D3DDisassemble",Compiler,Compiler}, {"D3DDisassemble10Effect",Compiler}, {"D3DGetBlobPart",Compiler},
    {"D3DGetDebugInfo",Compiler}, {"D3DGetInputAndOutputSignatureBlob",Compiler},
    {"D3DGetInputSignatureBlob",Compiler}, {"D3DGetOutputSignatureBlob",Compiler},
    {"D3DPreprocess",Compiler}, {"D3DReflect",Compiler}, {"D3DReturnFailure1",Compiler},
    {"D3DStripShader",Compiler}, {"Direct3DCreate9On12",D3d9}, {"Direct3DCreate9On12Ex",D3d9},
    {"Direct3DShaderValidatorCreate9",D3d9}, {"PSGPError",D3d9}, {"PSGPSampleTexture",D3d9},
    {"D3DPERF_BeginEvent",D3d9,D3d9}, {"D3DPERF_EndEvent",D3d9,D3d9}, {"D3DPERF_GetStatus",D3d9},
    {"D3DPERF_QueryRepeatFrame",D3d9}, {"D3DPERF_SetMarker",D3d9}, {"D3DPERF_SetOptions",D3d9},
    {"D3DPERF_SetRegion",D3d9}, {"DebugSetLevel",D3d9}, {"Direct3D9EnableMaximizedWindowedModeShim",D3d9},
    {"Direct3DCreate9",D3d9}, {"Direct3DCreate9Ex",D3d9}, {"D3D11CreateDevice",D3d11,D3d11},
    {"D3D11CreateDeviceAndSwapChain",D3d11}, {"DirectSoundCreate8",Sound,Sound},
    {"DirectSoundCaptureEnumerateW",Sound,Sound}, {"DirectSoundCaptureCreate8",Sound,Sound},
    {"timeGetDevCaps",Winmm,Winmm}, {"timeEndPeriod",Winmm,Winmm}, {"timeGetTime",Winmm,Winmm}, {"timeBeginPeriod",Winmm,Winmm},
    {"CreateDXGIFactory",Dxgi,Dxgi}, {"CreateDXGIFactory1",Dxgi,Dxgi}, {"CreateDXGIFactory2",Dxgi},
    {"XInputGetState",Xinput,Xinput}, {"XInputSetState",Xinput,Xinput}, {"XInputGetCapabilities",Xinput},
    {"XInputEnable",Xinput}, {"XInputGetDSoundAudioDeviceGuids",Xinput},
    {"XInputGetBatteryInformation",Xinput}, {"XInputGetKeystroke",Xinput}
};
constexpr std::size_t ordinal_slots = 42; // 1..38, 100..103 (all other ordinals absent).
constexpr std::size_t slot_count = ordinal_slots + std::size(names);
static_assert(slot_count == 94);
static_assert(sizeof(FARPROC) == 8, "Forwarding thunks require Windows x64");
std::array<FARPROC, slot_count> targets{};
volatile LONG state = 0; // 0 untouched, 1 initializing, 2 published, -1 failed; never wait.
HMODULE proxy_module = nullptr;
HMODULE retained_original = nullptr;

bool path(HMODULE module, wchar_t (&out)[32768]) noexcept
{
    const auto n = GetModuleFileNameW(module, out, static_cast<DWORD>(std::size(out)));
    return n != 0 && n < std::size(out) && out[0] && std::wcschr(out, L'\\');
}

bool executable_target(FARPROC proc, HMODULE self, const wchar_t* directory) noexcept
{
    MEMORY_BASIC_INFORMATION info{};
    if (!proc || VirtualQuery(reinterpret_cast<const void*>(proc), &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || info.Type != MEM_IMAGE || info.AllocationBase == self ||
        (info.Protect != PAGE_EXECUTE_READ && info.Protect != PAGE_EXECUTE)) return false;
    wchar_t owner[32768]{};
    if (!path(static_cast<HMODULE>(info.AllocationBase), owner)) return false;
    auto* slash = std::wcsrchr(owner, L'\\');
    *slash = L'\0';
    return _wcsicmp(owner, directory) == 0; // OS forwarders may end in another System32 module.
}

// Name at each conventional ordinal; nullptr means ordinal-only. No cross-profile aliasing.
const char* ordinal_name(unsigned profile, unsigned ordinal, bool& supported) noexcept
{
    supported = false;
    if (profile == Compiler && ordinal >= 1 && ordinal <= 17) {
        supported = true; return names[ordinal-1].name;
    }
    if (profile == D3d9 && ordinal >= 16 && ordinal <= 38) {
        supported = true;
        constexpr const char* map[] = {nullptr,nullptr,nullptr,nullptr,"Direct3DCreate9On12",
            "Direct3DCreate9On12Ex",nullptr,nullptr,"Direct3DShaderValidatorCreate9","PSGPError",
            "PSGPSampleTexture","D3DPERF_BeginEvent","D3DPERF_EndEvent","D3DPERF_GetStatus",
            "D3DPERF_QueryRepeatFrame","D3DPERF_SetMarker","D3DPERF_SetOptions","D3DPERF_SetRegion",
            "DebugSetLevel","DebugSetMute","Direct3D9EnableMaximizedWindowedModeShim",
            "Direct3DCreate9","Direct3DCreate9Ex"};
        return map[ordinal-16];
    }
    if (profile == Sound && (ordinal == 8 || ordinal == 11 || ordinal == 12)) {
        supported = true;
        return ordinal == 8 ? "DirectSoundCaptureEnumerateW" :
            ordinal == 11 ? "DirectSoundCreate8" : "DirectSoundCaptureCreate8";
    }
    if (profile == Xinput && (ordinal == 2 || ordinal == 3 || (ordinal >= 100 && ordinal <= 103))) {
        supported = true;
        return ordinal == 2 ? "XInputGetState" : ordinal == 3 ? "XInputSetState" : nullptr;
    }
    return nullptr;
}

bool required_ordinal(unsigned profile, unsigned ordinal) noexcept
{
    return (profile == Sound && (ordinal == 8 || ordinal == 11 || ordinal == 12)) ||
        (profile == Xinput && (ordinal == 2 || ordinal == 3));
}
} // namespace

ForwardingStatus initialize_forwarding(HMODULE self) noexcept
{
    const DWORD last_error = GetLastError();
    struct PreserveError { DWORD value; ~PreserveError() { SetLastError(value); } } preserve{last_error};
    const LONG previous = InterlockedCompareExchange(&state, 1, 0);
    if (previous == 2) return self == proxy_module ? ForwardingStatus::Ready : ForwardingStatus::InvalidSelf;
    if (previous == 1) return ForwardingStatus::InProgress;
    if (previous != 0) return ForwardingStatus::FailedPreviously;
    HMODULE original = nullptr;
    auto fail = [&](ForwardingStatus result) {
        if (original) FreeLibrary(original); // Only our unpublished reference, outside DllMain.
        InterlockedExchange(&state, -1);
        return result;
    };
    wchar_t own[32768]{}, system[32768]{};
    if (!self || !path(self, own)) return fail(ForwardingStatus::InvalidSelf);
    const wchar_t* basename = std::wcsrchr(own, L'\\') + 1;
    unsigned profile = 0;
    struct Choice { const wchar_t* name; unsigned profile; };
    constexpr Choice choices[] = {{L"d3dcompiler_43.dll",Compiler},{L"d3d9.dll",D3d9},
        {L"d3d11.dll",D3d11},{L"dsound.dll",Sound},{L"winmm.dll",Winmm},
        {L"dxgi.dll",Dxgi},{L"xinput1_3.dll",Xinput}};
    for (const auto& choice : choices) if (_wcsicmp(basename,choice.name)==0) profile=choice.profile;
    if (!profile) return fail(ForwardingStatus::UnsupportedBasename);
    const auto n = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
    if (!n || n >= std::size(system) || n + 1 + std::wcslen(basename) >= std::size(system))
        return fail(ForwardingStatus::SystemDirectoryFailed);
    wchar_t full[32768]{};
    std::wmemcpy(full,system,n); full[n]=L'\\';
    std::wmemcpy(full+n+1,basename,std::wcslen(basename)+1);
    original = LoadLibraryExW(full,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!original) return fail(ForwardingStatus::LoadFailed);
    wchar_t actual[32768]{};
    if (original == self || !path(original,actual) || _wcsicmp(full,actual)!=0)
        return fail(ForwardingStatus::WrongModule);
    targets.fill(&native_proxy_fail_fast);
    for (std::size_t i=0; i<std::size(names); ++i) if (names[i].profiles & profile) {
        const auto proc = GetProcAddress(original,names[i].name);
        if (!proc) {
            if (names[i].required_profiles & profile) return fail(ForwardingStatus::MissingExport);
            continue;
        }
        if (!executable_target(proc,self,system)) return fail(ForwardingStatus::InvalidTarget);
        targets[ordinal_slots+i]=proc;
    }
    for (std::size_t i=0; i<ordinal_slots; ++i) {
        const unsigned ordinal = static_cast<unsigned>(i<38 ? i+1 : i-38+100);
        bool supported = false;
        const char* name = ordinal_name(profile,ordinal,supported);
        if (!supported) continue;
        const auto proc = GetProcAddress(original,MAKEINTRESOURCEA(ordinal));
        if (!proc) {
            if (required_ordinal(profile,ordinal)) return fail(ForwardingStatus::MissingExport);
            continue;
        }
        if (name && proc != GetProcAddress(original,name)) return fail(ForwardingStatus::MissingExport);
        if (!executable_target(proc,self,system)) return fail(ForwardingStatus::InvalidTarget);
        targets[i]=proc;
    }
    proxy_module=self; retained_original=original;
    InterlockedExchangePointer(&native_proxy_targets,targets.data());
    InterlockedExchange(&state,2);
    return ForwardingStatus::Ready;
}

bool forwarding_ready() noexcept { return InterlockedCompareExchange(&state,2,2)==2; }
} // namespace ff7r::piano::proxy
