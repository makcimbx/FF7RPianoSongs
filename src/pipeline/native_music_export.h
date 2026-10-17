#pragma once

#include "song_types.h"

namespace ff7rp::pipeline {

struct NativeMusicExportResult {
    Status status;
    std::vector<std::uint8_t> bytes;
};

struct NativeMusicCueTiming {
    std::uint64_t logical_source_frames=0;
    std::uint32_t sample_rate=0;
    double duration_seconds=0;
};
// Older output explicitly selects nonstreaming/loading/channel fields and uses
// the qualified StoreEntry exponent-zero profile: prefix160 + bank + suffix40.
enum class NativeMusicExportProfile { Current1005, Older1004 };
NativeMusicExportResult build_native_music_export(
    const std::vector<std::uint8_t>& source_export,
    const std::vector<std::uint8_t>& replacement_mabf, const NativeMusicCueTiming&,
    NativeMusicExportProfile);
Status verify_native_music_export(const std::vector<std::uint8_t>& source_export,
    const std::vector<std::uint8_t>& replacement_mabf, const NativeMusicCueTiming&,
    NativeMusicExportProfile, const std::vector<std::uint8_t>& candidate_export);

// Exact reflected single-cue profile, with variable mapped-bank extent. Frames
// and rate are authoritative; supplied seconds must agree with their ratio.
NativeMusicExportResult build_native_music_export(
    const std::vector<std::uint8_t>& source_export,
    const std::vector<std::uint8_t>& replacement_mabf, const NativeMusicCueTiming&);
Status verify_native_music_export(const std::vector<std::uint8_t>& source_export,
    const std::vector<std::uint8_t>& replacement_mabf, const NativeMusicCueTiming&,
    const std::vector<std::uint8_t>& candidate_export);

// Export body only (no Zen/legacy package header or .uexp tag). Supports only
// the qualified bgm_piano_01 wrapper at its observed fixed geometry. The MABF
// artifact includes its 48-byte tail; that tail must match, and is not replaced.
// This is not a loadable package, cache admission, or playback qualification.
NativeMusicExportResult build_native_music_export(
    const std::vector<std::uint8_t>& source_export,
    const std::vector<std::uint8_t>& replacement_mabf);

// Independently revalidates inputs, exact replacement, and outside-bank identity.
Status verify_native_music_export(
    const std::vector<std::uint8_t>& source_export,
    const std::vector<std::uint8_t>& replacement_mabf,
    const std::vector<std::uint8_t>& candidate_export);

} // namespace ff7rp::pipeline
