#pragma once

#include <filesystem>
#include <string_view>

#include "game/song_descriptor.h"
#include "pipeline/oodle_decoder.h"
#include "pipeline/native_audio_cache.h"

namespace ff7r::piano::startup {

// Repository worker only; attaches immutable output before descriptor publication.
ff7rp::pipeline::Status prepare_native_audio_descriptor(game::SongDescriptor& descriptor,
    std::string_view build_id, const std::filesystem::path& source_paks,
    const ff7rp::pipeline::OodleDecoder& decoder, const std::filesystem::path& cache_root,
    ff7rp::pipeline::NativeAudioCacheSession* session = nullptr);

} // namespace ff7r::piano::startup
