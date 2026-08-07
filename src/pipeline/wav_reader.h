#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "song_types.h"
#include "pipeline_limits.h"

namespace ff7rp::pipeline {

constexpr std::uint32_t kSongWavSampleRate = 48000;

Status parse_wav_bytes(const std::vector<std::uint8_t>& bytes, WavAudio* out_audio);
Status read_wav_file(const std::string& path, WavAudio* out_audio);

} // namespace ff7rp::pipeline
