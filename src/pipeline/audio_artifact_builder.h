#pragma once

#include <array>
#include <functional>
#include <optional>

#include "mabf_builder.h"
#include "song_types.h"

namespace ff7rp::pipeline {

using AudioArtifactBuildTrace = std::function<void(const char*)>;

struct AudioMabfInputs {
    std::array<std::reference_wrapper<const WavAudio>, 3> resolved_modes;
    std::optional<std::reference_wrapper<const WavAudio>> mode0_guide;
};

MabfBuildResult build_audio_mabf(
    const AudioMabfInputs& inputs,
    const AudioArtifactBuildTrace& trace = {});

} // namespace ff7rp::pipeline
