#include <windows.h>
#include <mmeapi.h>

#include <algorithm>
#include <iostream>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <vector>

namespace {
bool fail_open = false, fail_reset = false, fail_unprepare = false;
bool fail_prepare = false, fail_write = false, fail_close = false;
bool queued = false, prepared = false;
bool fail_load = false, fail_exports = false, long_system_path = false;
unsigned loads = 0, frees = 0;
unsigned opens = 0, writes = 0;
WAVEHDR* borrowed = nullptr;
std::vector<short> first_tone;
void require(bool condition) { if (!condition) throw std::runtime_error("toggle feedback invariant"); }
MMRESULT WINAPI mock_open(LPHWAVEOUT out, UINT, LPCWAVEFORMATEX format, DWORD_PTR callback,
    DWORD_PTR, DWORD flags)
{
    require(!callback && flags == CALLBACK_NULL && format->wFormatTag == WAVE_FORMAT_PCM
        && format->nChannels == 1 && format->wBitsPerSample == 16);
    ++opens;
    if (fail_open) return MMSYSERR_NODRIVER;
    *out = reinterpret_cast<HWAVEOUT>(1);
    return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_reset(HWAVEOUT)
{
    if (fail_reset) return MMSYSERR_ERROR;
    queued = false;
    return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_unprepare(HWAVEOUT, LPWAVEHDR header, UINT)
{
    require(prepared && !queued && header == borrowed);
    if (fail_unprepare) return MMSYSERR_ERROR;
    prepared = false;
    borrowed = nullptr;
    return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_prepare(HWAVEOUT, LPWAVEHDR header, UINT)
{
    require(!prepared);
    if (fail_prepare) return MMSYSERR_ERROR;
    prepared = true;
    borrowed = header;
    return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_write(HWAVEOUT, LPWAVEHDR header, UINT)
{
    require(prepared && header == borrowed && !queued);
    if (fail_write) return MMSYSERR_ERROR;
    queued = true;
    ++writes;
    return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_close(HWAVEOUT)
{
    require(!prepared && !queued);
    return fail_close ? MMSYSERR_ERROR : MMSYSERR_NOERROR;
}
UINT WINAPI mock_system_directory(LPWSTR path, UINT size)
{
    if (long_system_path) return size + 1;
    constexpr wchar_t system[] = L"C:\\Windows\\System32";
    require(size >= std::size(system));
    std::wmemcpy(path, system, std::size(system));
    return static_cast<UINT>(std::size(system) - 1);
}
HMODULE WINAPI mock_load_library(LPCWSTR path, HANDLE file, DWORD flags)
{
    require(std::wcscmp(path, L"C:\\Windows\\System32\\winmm.dll") == 0
        && !file && flags == LOAD_LIBRARY_SEARCH_SYSTEM32);
    ++loads;
    return fail_load ? nullptr : reinterpret_cast<HMODULE>(2);
}
FARPROC WINAPI mock_get_proc(HMODULE module, LPCSTR name)
{
    require(module == reinterpret_cast<HMODULE>(2));
    if (fail_exports) return nullptr;
    if (std::strcmp(name, "waveOutOpen") == 0) return reinterpret_cast<FARPROC>(&mock_open);
    if (std::strcmp(name, "waveOutReset") == 0) return reinterpret_cast<FARPROC>(&mock_reset);
    if (std::strcmp(name, "waveOutUnprepareHeader") == 0) return reinterpret_cast<FARPROC>(&mock_unprepare);
    if (std::strcmp(name, "waveOutPrepareHeader") == 0) return reinterpret_cast<FARPROC>(&mock_prepare);
    if (std::strcmp(name, "waveOutWrite") == 0) return reinterpret_cast<FARPROC>(&mock_write);
    if (std::strcmp(name, "waveOutClose") == 0) return reinterpret_cast<FARPROC>(&mock_close);
    throw std::runtime_error("unexpected export");
}
BOOL WINAPI mock_free_library(HMODULE module)
{
    require(module == reinterpret_cast<HMODULE>(2)); ++frees; return TRUE;
}
}

#define GetSystemDirectoryW mock_system_directory
#define LoadLibraryExW mock_load_library
#define GetProcAddress mock_get_proc
#define FreeLibrary mock_free_library
#include "core/toggle_feedback.cpp"

int main()
{
    using namespace ff7r::piano::core;
    try {
        require(stop_toggle_feedback());
        long_system_path = true;
        require(!play_toggle_feedback(true) && !loads && !opens);
        long_system_path = false; fail_load = true;
        require(!play_toggle_feedback(true) && !opens);
        fail_load = false; fail_exports = true;
        require(!play_toggle_feedback(true) && !opens && frees == 1);
        fail_exports = false;
        fail_open = true;
        require(!play_toggle_feedback(true) && !prepared);
        fail_open = false;
        require(play_toggle_feedback(true));
        auto* samples = reinterpret_cast<short*>(borrowed->lpData);
        first_tone.assign(samples, samples + borrowed->dwBufferLength / sizeof(short));
        require(first_tone.front() == 0 && first_tone.back() == 0
            && *std::max_element(first_tone.begin(), first_tone.end()) <= 4000);
        fail_reset = true;
        const auto* original_buffer = borrowed->lpData;
        require(!play_toggle_feedback(false) && borrowed->lpData == original_buffer && queued);
        require(!stop_toggle_feedback());
        fail_reset = false;
        fail_unprepare = true;
        require(!play_toggle_feedback(false) && borrowed->lpData == original_buffer && prepared);
        fail_unprepare = false;
        require(play_toggle_feedback(false));
        samples = reinterpret_cast<short*>(borrowed->lpData);
        require(!std::equal(first_tone.begin(), first_tone.end(), samples) && writes == 2 && opens == 2);
        fail_write = true;
        require(!play_toggle_feedback(true) && prepared && !queued);
        fail_write = false;
        fail_prepare = true;
        require(!play_toggle_feedback(true) && !prepared);
        fail_prepare = false;
        require(play_toggle_feedback(true));
        fail_close = true;
        require(!stop_toggle_feedback() && !prepared && !queued);
        fail_close = false;
        require(stop_toggle_feedback() && stop_toggle_feedback());
        require(play_toggle_feedback(false) && opens == 3);
        require(stop_toggle_feedback());
        require(loads == 3 && frees == 1); // Published API stays loaded across device cleanup.
        std::cout << "toggle feedback selftest passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
