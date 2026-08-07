#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline {

Status decode_audio_bytes(const std::vector<std::uint8_t>& bytes, WavAudio* out_audio);
Status read_audio_file(const std::string& path, WavAudio* out_audio);
// Decoder output is accepted only when every sample is finite and in normalized [-1, 1] range.
Status validate_decoded_pcm(const WavAudio& audio);

} // namespace ff7rp::pipeline
