#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline {

Status decode_audio_bytes(const std::vector<std::uint8_t>& bytes, WavAudio* out_audio);
Status read_audio_file(const std::string& path, WavAudio* out_audio);
// Rejects non-finite decoder output and scales a song peaking above full scale down to [-1, 1].
Status sanitize_decoded_pcm(WavAudio* audio);

} // namespace ff7rp::pipeline
