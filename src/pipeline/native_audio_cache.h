#pragma once
#include "native_audio_cohort.h"
#include "oodle_decoder.h"

namespace ff7rp::pipeline {
struct NativeAudioCacheRequest {
    std::filesystem::path source_paks;
    std::filesystem::path cache_root;
    std::string build_id;
    std::string identity;
    std::string base_score_name;
    native_binary::Bytes mabf;
    NativeMusicCueTiming timing;
};
// Caller supplies an authenticated decoder; no function resolution or DLL search here.
// Reads qualified stock chunks and constructs/verifies immutable output only.
Status prepare_native_audio_cache(const NativeAudioCacheRequest&,
    const OodleDecoder&, NativeAudioArtifactSet&);
Status validate_native_audio_artifacts(const NativeAudioCacheRequest&,
    const OodleDecoder&, const NativeAudioArtifactSet&);
} // namespace ff7rp::pipeline
