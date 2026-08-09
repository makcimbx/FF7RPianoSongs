#pragma once

#include <functional>

#include "mabf_builder.h"
#include "song_types.h"

namespace ff7rp::pipeline {

using AudioArtifactBuildTrace = std::function<void(const char*)>;

MabfBuildResult build_audio_mabf(
    const WavAudio& clean_audio,
    const WavAudio* metronome_mode0_audio,
    bool adaptive_metronome,
    const AudioArtifactBuildTrace& trace = {});

} // namespace ff7rp::pipeline
