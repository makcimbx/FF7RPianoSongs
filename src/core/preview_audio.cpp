#include "core/preview_audio.h"

#include <windows.h>
#include <mmeapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <mutex>

namespace ff7r::piano::core {
namespace {
struct WaveApi {
    HMODULE module = nullptr;
    decltype(&waveOutOpen) open = nullptr;
    decltype(&waveOutReset) reset = nullptr;
    decltype(&waveOutUnprepareHeader) unprepare = nullptr;
    decltype(&waveOutPrepareHeader) prepare = nullptr;
    decltype(&waveOutWrite) write = nullptr;
    decltype(&waveOutClose) close = nullptr;
    decltype(&waveOutGetPosition) position = nullptr;
};
struct Stream {
    std::mutex mutex;
    WaveApi api;
    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    std::vector<std::int16_t> samples;
    bool prepared = false;
    bool queued = false;
    bool faded = false;
    bool failed = false;
};
Stream& stream()
{
    // Failed driver cancellation must not release a still-borrowed header/PCM.
    static auto* value = new Stream;
    return *value;
}
bool load_api(Stream& s)
{
    if (s.api.module) return true;
    wchar_t path[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    constexpr wchar_t suffix[] = L"\\winmm.dll";
    if (!length || length + std::size(suffix) > std::size(path)) return false;
    std::wmemcpy(path + length, suffix, std::size(suffix));
    const HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;
    const WaveApi api{module,
        reinterpret_cast<decltype(s.api.open)>(GetProcAddress(module, "waveOutOpen")),
        reinterpret_cast<decltype(s.api.reset)>(GetProcAddress(module, "waveOutReset")),
        reinterpret_cast<decltype(s.api.unprepare)>(GetProcAddress(module, "waveOutUnprepareHeader")),
        reinterpret_cast<decltype(s.api.prepare)>(GetProcAddress(module, "waveOutPrepareHeader")),
        reinterpret_cast<decltype(s.api.write)>(GetProcAddress(module, "waveOutWrite")),
        reinterpret_cast<decltype(s.api.close)>(GetProcAddress(module, "waveOutClose")),
        reinterpret_cast<decltype(s.api.position)>(GetProcAddress(module, "waveOutGetPosition"))};
    if (!api.open || !api.reset || !api.unprepare || !api.prepare
        || !api.write || !api.close || !api.position) {
        FreeLibrary(module);
        return false;
    }
    s.api = api;
    return true;
}
bool stop(Stream& s)
{
    if (s.prepared) {
        if (s.api.reset(s.device) != MMSYSERR_NOERROR) return false;
        s.queued = false;
        if (s.api.unprepare(s.device, &s.header, sizeof(s.header)) != MMSYSERR_NOERROR)
            return false;
        s.prepared = false;
    }
    if (s.device && s.api.close(s.device) != MMSYSERR_NOERROR) return false;
    s.device = nullptr;
    s.samples.clear();
    s.header = {};
    s.faded = false;
    return true;
}
bool played_frames(Stream& s, std::uint64_t& frames)
{
    MMTIME time{};
    time.wType = TIME_SAMPLES;
    if (s.api.position(s.device, &time, sizeof(time)) != MMSYSERR_NOERROR) return false;
    switch (time.wType) {
    case TIME_SAMPLES: frames = time.u.sample; return true;
    case TIME_BYTES: frames = time.u.cb / 4u; return true;
    case TIME_MS: frames = static_cast<std::uint64_t>(time.u.ms) * 48000u / 1000u; return true;
    default: return false;
    }
}
}

bool play_preview_audio(std::vector<std::int16_t>&& samples, float gain) noexcept
{
    auto& s = stream();
    std::lock_guard lock(s.mutex);
    if (s.failed || !std::isfinite(gain) || gain < 0.0f || gain > 1.0f
        || samples.empty() || samples.size() % 2
        || samples.size() > 48000u * 20u * 2u) return false;
    if (!load_api(s) || !stop(s)) { s.failed = true; return false; }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 48000 * 4;
    if (s.api.open(&s.device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        s.device = nullptr;
        s.failed = true;
        return false;
    }
    s.samples = std::move(samples);
    for (auto& sample : s.samples)
        sample = static_cast<std::int16_t>(std::lround(sample * double(gain)));
    s.header = {};
    s.header.lpData = reinterpret_cast<char*>(s.samples.data());
    s.header.dwBufferLength = static_cast<DWORD>(s.samples.size() * sizeof(std::int16_t));
    if (s.api.prepare(s.device, &s.header, sizeof(s.header)) != MMSYSERR_NOERROR) {
        s.failed = true;
        return false;
    }
    s.prepared = true;
    if (s.api.write(s.device, &s.header, sizeof(s.header)) != MMSYSERR_NOERROR) {
        s.failed = true;
        return false;
    }
    s.queued = true;
    return true;
}

bool stop_preview_audio() noexcept
{
    auto& s = stream();
    std::lock_guard lock(s.mutex);
    const bool stopped = stop(s);
    if (!stopped) s.failed = true;
    return stopped;
}

bool fade_preview_audio() noexcept
{
    auto& s = stream();
    std::lock_guard lock(s.mutex);
    if (s.failed) return false;
    if (!s.queued || s.faded || (s.header.dwFlags & WHDR_DONE)) return true;
    std::uint64_t frames = 0;
    if (!played_frames(s, frames)) { s.failed = true; return false; }
    // WinMM has no per-stream gain ramp, so rewrite the borrowed PCM the driver has not
    // consumed yet. ponytail: fixed 100 ms read-ahead guess; measure it if stops still click.
    constexpr std::uint64_t lead = 4800, ramp = 7200;
    const std::uint64_t total = s.samples.size() / 2, from = std::min(frames + lead, total);
    for (auto f = from; f < total; ++f) {
        const double gain = f < from + ramp ? double(from + ramp - f) / ramp : 0.0;
        for (std::uint64_t i = f * 2; i < f * 2 + 2; ++i)
            s.samples[i] = static_cast<std::int16_t>(std::lround(s.samples[i] * gain));
    }
    s.faded = true;
    return true;
}

PreviewAudioPosition preview_audio_position() noexcept
{
    auto& s = stream();
    std::lock_guard lock(s.mutex);
    PreviewAudioPosition result{false, s.failed, 0,
        static_cast<std::uint32_t>(s.samples.size() / 2)};
    if (!s.queued) return result;
    std::uint64_t frames = 0;
    if (!played_frames(s, frames)) {
        s.failed = result.failed = true;
        return result;
    }
    result.played_frames = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(frames, result.total_frames));
    result.playing = !(s.header.dwFlags & WHDR_DONE);
    return result;
}
} // namespace ff7r::piano::core
