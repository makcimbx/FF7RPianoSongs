#include "core/toggle_feedback.h"

#include <windows.h>
#include <mmeapi.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <numbers>

namespace ff7r::piano::core {
namespace {

constexpr unsigned kSampleRate = 44100;
constexpr unsigned kNoteSamples = kSampleRate * 80 / 1000;
using Tone = std::array<std::int16_t, kNoteSamples * 2>;

Tone make_tone(bool enabled)
{
    Tone samples{};
    for (unsigned note = 0; note < 2; ++note) {
        const double frequency = (note == 0) == enabled ? 660.0 : 990.0;
        for (unsigned i = 0; i < kNoteSamples; ++i) {
            const double envelope = std::sin(std::numbers::pi * i / (kNoteSamples - 1));
            samples[note * kNoteSamples + i] = static_cast<std::int16_t>(
                4000.0 * envelope * envelope
                * std::sin(2.0 * std::numbers::pi * frequency * i / kSampleRate));
        }
    }
    return samples;
}

std::mutex g_feedback_mutex;
HWAVEOUT g_device = nullptr;
WAVEHDR g_header{};
bool g_prepared = false;

struct WaveApi {
    HMODULE module = nullptr;
    decltype(&waveOutOpen) open = nullptr;
    decltype(&waveOutReset) reset = nullptr;
    decltype(&waveOutUnprepareHeader) unprepare = nullptr;
    decltype(&waveOutPrepareHeader) prepare = nullptr;
    decltype(&waveOutWrite) write = nullptr;
    decltype(&waveOutClose) close = nullptr;
} g_wave;

bool load_wave_api()
{
    if (g_wave.module) return true;
    // A static winmm import would break the supported winmm.dll proxy filename.
    wchar_t path[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    constexpr wchar_t suffix[] = L"\\winmm.dll";
    if (!length || length + std::size(suffix) > std::size(path)) return false;
    std::wmemcpy(path + length, suffix, std::size(suffix));
    const HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;
    const WaveApi api{module,
        reinterpret_cast<decltype(g_wave.open)>(GetProcAddress(module, "waveOutOpen")),
        reinterpret_cast<decltype(g_wave.reset)>(GetProcAddress(module, "waveOutReset")),
        reinterpret_cast<decltype(g_wave.unprepare)>(GetProcAddress(module, "waveOutUnprepareHeader")),
        reinterpret_cast<decltype(g_wave.prepare)>(GetProcAddress(module, "waveOutPrepareHeader")),
        reinterpret_cast<decltype(g_wave.write)>(GetProcAddress(module, "waveOutWrite")),
        reinterpret_cast<decltype(g_wave.close)>(GetProcAddress(module, "waveOutClose"))};
    if (!api.open || !api.reset || !api.unprepare || !api.prepare || !api.write || !api.close) {
        FreeLibrary(module);
        return false;
    }
    g_wave = api; // Retain the genuine module with its process-lived driver resources.
    return true;
}

bool retire_buffer()
{
    if (!g_prepared) return true;
    // Reset returns this stream's buffers before unprepare; never stop game audio.
    if (g_wave.reset(g_device) != MMSYSERR_NOERROR
        || g_wave.unprepare(g_device, &g_header, sizeof(g_header)) != MMSYSERR_NOERROR) {
        return false;
    }
    g_prepared = false;
    return true;
}

} // namespace

bool play_toggle_feedback(bool enabled)
{
    // Static buffers remain owned even when a driver rejects reset/unprepare.
    static const Tone on = make_tone(true);
    static const Tone off = make_tone(false);
    std::lock_guard lock(g_feedback_mutex);
    if (!load_wave_api() || !retire_buffer()) return false;
    if (!g_device) {
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = kSampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 2;
        format.nAvgBytesPerSec = kSampleRate * format.nBlockAlign;
        if (g_wave.open(&g_device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            g_device = nullptr;
            return false;
        }
    }
    const Tone& tone = enabled ? on : off;
    g_header = {};
    g_header.lpData = reinterpret_cast<char*>(const_cast<std::int16_t*>(tone.data()));
    g_header.dwBufferLength = static_cast<DWORD>(sizeof(tone));
    if (g_wave.prepare(g_device, &g_header, sizeof(g_header)) != MMSYSERR_NOERROR) return false;
    g_prepared = true;
    return g_wave.write(g_device, &g_header, sizeof(g_header)) == MMSYSERR_NOERROR;
}

bool stop_toggle_feedback()
{
    std::lock_guard lock(g_feedback_mutex);
    if (!retire_buffer()) return false;
    if (g_device && g_wave.close(g_device) != MMSYSERR_NOERROR) return false;
    g_device = nullptr;
    return true;
}

} // namespace ff7r::piano::core
