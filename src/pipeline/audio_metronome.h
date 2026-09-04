#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline {

inline constexpr std::string_view kMetronomeSynthesisIdentity =
    "metronome_synthesis=shared_woodblock_envelope:v3";

struct MetronomeBeat {
    std::size_t frame = 0;
    bool downbeat = false;
};

struct MetronomeStats {
    std::size_t beat_count = 0;
    std::size_t downbeat_count = 0;
    double first_beat_seconds = -1.0;
    double last_beat_seconds = -1.0;
};

enum class MetronomeVoice {
    Strong,
    Weak,
};

Status build_metronome_beats(
    const std::string& midi_path,
    bool chart_from_midi,
    const WavAudio& audio,
    const SongConfig& config,
    std::vector<MetronomeBeat>* out_beats);

Status mix_metronome_clicks(
    WavAudio* audio,
    const SongConfig& config,
    const std::vector<MetronomeBeat>& beats,
    MetronomeVoice voice,
    MetronomeStats* out_stats);

} // namespace ff7rp::pipeline
