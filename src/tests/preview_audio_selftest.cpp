#include <windows.h>
#include <mmeapi.h>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
bool fail_load = false, fail_exports = false, fail_open = false, fail_reset = false;
bool fail_unprepare = false, fail_prepare = false, fail_write = false, fail_close = false;
bool fail_position = false, queued = false, prepared = false;
UINT position_type = TIME_SAMPLES;
WAVEHDR* borrowed = nullptr;
DWORD position = 0;
void require(bool value) { if (!value) throw std::runtime_error("preview audio invariant"); }
MMRESULT WINAPI mock_open(LPHWAVEOUT out, UINT, LPCWAVEFORMATEX f, DWORD_PTR cb, DWORD_PTR, DWORD flags)
{
    require(f->nChannels == 2 && f->nSamplesPerSec == 48000 && f->wBitsPerSample == 16
        && f->nBlockAlign == 4 && !cb && flags == CALLBACK_NULL);
    if (fail_open) return MMSYSERR_ERROR;
    *out = reinterpret_cast<HWAVEOUT>(1); return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_reset(HWAVEOUT)
{
    if (fail_reset) return MMSYSERR_ERROR;
    queued = false; return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_unprepare(HWAVEOUT, LPWAVEHDR h, UINT)
{
    require(prepared && !queued && h == borrowed);
    if (fail_unprepare) return MMSYSERR_ERROR;
    prepared = false; borrowed = nullptr; return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_prepare(HWAVEOUT, LPWAVEHDR h, UINT)
{
    require(!prepared);
    if (fail_prepare) return MMSYSERR_ERROR;
    prepared = true; borrowed = h; return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_write(HWAVEOUT, LPWAVEHDR h, UINT)
{
    require(prepared && h == borrowed && !queued);
    if (fail_write) return MMSYSERR_ERROR;
    queued = true; return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_close(HWAVEOUT)
{
    require(!prepared && !queued); return fail_close ? MMSYSERR_ERROR : MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_position(HWAVEOUT, LPMMTIME time, UINT)
{
    require(time->wType == TIME_SAMPLES);
    if (fail_position) return MMSYSERR_ERROR;
    time->wType = position_type;
    switch (position_type) {
    case TIME_SAMPLES: time->u.sample = position; break;
    case TIME_BYTES: time->u.cb = position; break;
    case TIME_MS: time->u.ms = position; break;
    default: time->u.ticks = position; break;
    }
    return MMSYSERR_NOERROR;
}
UINT WINAPI mock_system(LPWSTR p, UINT)
{
    constexpr wchar_t system[] = L"C:\\Windows\\System32";
    std::wmemcpy(p, system, std::size(system)); return std::size(system) - 1;
}
HMODULE WINAPI mock_load(LPCWSTR p, HANDLE file, DWORD flags)
{
    require(std::wcscmp(p, L"C:\\Windows\\System32\\winmm.dll") == 0 && !file
        && flags == LOAD_LIBRARY_SEARCH_SYSTEM32);
    return fail_load ? nullptr : reinterpret_cast<HMODULE>(2);
}
FARPROC WINAPI mock_proc(HMODULE, LPCSTR name)
{
    if (fail_exports) return nullptr;
#define EXPORT(n, f) if (std::strcmp(name, n) == 0) return reinterpret_cast<FARPROC>(&f)
    EXPORT("waveOutOpen", mock_open); EXPORT("waveOutReset", mock_reset);
    EXPORT("waveOutUnprepareHeader", mock_unprepare); EXPORT("waveOutPrepareHeader", mock_prepare);
    EXPORT("waveOutWrite", mock_write); EXPORT("waveOutClose", mock_close);
    EXPORT("waveOutGetPosition", mock_position);
#undef EXPORT
    throw std::runtime_error("unexpected WinMM export");
}
BOOL WINAPI mock_free(HMODULE) { return TRUE; }
}
#define GetSystemDirectoryW mock_system
#define LoadLibraryExW mock_load
#define GetProcAddress mock_proc
#define FreeLibrary mock_free
#include "core/preview_audio.cpp"

int main()
{
    using namespace ff7r::piano::core;
    try {
        auto reset_failure = [] { stream().failed = false; }; // Test-only driver recovery between cases.
        auto clip = [] { return std::vector<std::int16_t>(48000 * 2, 123); };
        require(stop_preview_audio() && !play_preview_audio({}) && !play_preview_audio({1}));
        require(!play_preview_audio(std::vector<std::int16_t>(48000 * 20 * 2 + 2)));
        fail_load = true; require(!play_preview_audio(clip())); fail_load = false; reset_failure();
        fail_exports = true; require(!play_preview_audio(clip())); fail_exports = false; reset_failure();
        fail_open = true; require(!play_preview_audio(clip())); fail_open = false; reset_failure();
        require(play_preview_audio(clip()) && queued);
        const auto* data = borrowed->lpData;
        position = 125;
        auto p = preview_audio_position();
        require(p.playing && !p.failed && p.played_frames == 125 && p.total_frames == 48000);
        position = 999999; require(preview_audio_position().played_frames == 48000);
        position_type = TIME_MS; position = 125;
        require(!preview_audio_position().failed && preview_audio_position().played_frames == 6000);
        position = MAXDWORD; require(preview_audio_position().played_frames == 48000);
        position_type = TIME_BYTES; position = 503;
        require(!preview_audio_position().failed && preview_audio_position().played_frames == 125);
        position_type = TIME_SAMPLES;
        fail_reset = true;
        require(!stop_preview_audio() && queued && prepared && borrowed->lpData == data);
        require(!play_preview_audio(clip()) && borrowed->lpData == data);
        require(reinterpret_cast<short*>(borrowed->lpData)[0] == 123);
        fail_reset = false; fail_unprepare = true;
        require(!stop_preview_audio() && prepared && borrowed->lpData == data);
        fail_unprepare = false; require(stop_preview_audio()); reset_failure();
        require(play_preview_audio(clip())); borrowed->dwFlags |= WHDR_DONE;
        require(!preview_audio_position().playing && stop_preview_audio());
        fail_prepare = true; require(!play_preview_audio(clip()) && !prepared);
        fail_prepare = false; require(stop_preview_audio()); reset_failure();
        fail_write = true; require(!play_preview_audio(clip()) && prepared && !queued);
        fail_write = false; require(stop_preview_audio()); reset_failure();
        require(play_preview_audio(clip())); fail_close = true;
        require(!stop_preview_audio() && stream().device && !prepared);
        fail_close = false; require(stop_preview_audio()); reset_failure();
        require(play_preview_audio(clip())); fail_position = true;
        require(preview_audio_position().failed && queued && prepared);
        fail_position = false; require(stop_preview_audio()); reset_failure();
        require(play_preview_audio(clip())); position = 1000;
        require(fade_preview_audio() && fade_preview_audio() && queued);
        const auto* pcm = reinterpret_cast<short*>(borrowed->lpData);
        require(pcm[2 * 5799] == 123 && pcm[2 * 5800] == 123 && pcm[2 * 9400 + 1] == 62
            && pcm[2 * 13000] == 0 && pcm[2 * 47999 + 1] == 0); // Unplayed audio ramps once, then silence.
        require(stop_preview_audio() && fade_preview_audio()); // Nothing queued is already silent.
        require(play_preview_audio(clip())); position_type = TIME_TICKS;
        require(preview_audio_position().failed && queued && prepared);
        require(stop_preview_audio());
        std::cout << "preview audio selftest passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
