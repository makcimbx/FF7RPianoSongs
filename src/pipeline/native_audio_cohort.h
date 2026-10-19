#pragma once
#include "native_iostore_writer.h"
#include "native_music_export.h"
#include <memory>

namespace ff7rp::pipeline {
struct NativeAudioCohortRequest {
    std::string build_id;
    std::string identity; // Descriptor/song identity, preserved in NativeAudioArtifactSet.
    // Exact stock table row identity, not an offset or numeric fallback. Existing
    // chart/profile projection supplies authored notes; this is only its carrier.
    std::string base_score_name;
    std::array<native_binary::Bytes,4> source_zen;
    native_binary::Bytes mabf;
    NativeMusicCueTiming timing;
    // Optional immutable session snapshot instead of source_zen. This is storage
    // reuse only: build_native_audio_cohort still authenticates every source.
    std::shared_ptr<const std::array<native_binary::Bytes,4>> source_snapshot;
};
struct NativeAudioCohortResult {
    Status status;
    std::string content_id, music_name, bgm_key, score_name;
    std::array<NativeIoStorePackageInput,4> packages;
    NativeIoStoreBuildResult container;
};
// Current build: exact stock receipts and qualified single-cue Music profile.
// Older build: exhaustive source-profile checks and an explicit nonstreaming,
// suffix40 Music projection. Never runtime admission or chart compilation.
NativeAudioCohortResult build_native_audio_cohort(const NativeAudioCohortRequest&);
// Full SHA256 content-addressed directory; existing bytes are verified, NEVER
// replaced. files[].content_hash is the first eight SHA256 bytes, little endian;
// verification rederives and compares full bytes, not only this compact summary.
Status prepare_native_audio_cohort(const NativeAudioCohortRequest&,
    const std::filesystem::path& cache_root, NativeAudioArtifactSet&);
Status verify_native_audio_cohort(const NativeAudioCohortRequest&, const NativeAudioArtifactSet&);
} // namespace ff7rp::pipeline
