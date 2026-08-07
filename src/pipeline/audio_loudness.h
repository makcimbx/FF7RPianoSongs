#pragma once

#include "song_types.h"

namespace ff7rp::pipeline {

constexpr double kGainEnvelopeMinimumDb = -12.0;
constexpr double kGainEnvelopeMaximumDb = 12.0;
constexpr std::size_t kMaximumGainEnvelopePoints = 64;

struct AudioLoudnessStats {
    double input_lufs = -100.0;
    double output_lufs = -100.0;
    double input_peak_dbfs = -100.0;
    double output_peak_dbfs = -100.0;
    double applied_gain_db = 0.0;
    bool normalized = false;
    bool gain_applied = false;
    bool limiter_engaged = false;
    bool gain_envelope_applied = false;
    std::size_t gain_envelope_point_count = 0;
    double gain_envelope_max_gain_db = 0.0;
    double gain_envelope_min_gain_db = 0.0;
};

Status validate_gain_envelope(const std::vector<GainEnvelopePoint>& points);
double gain_envelope_amplitude_at(const std::vector<GainEnvelopePoint>& points, double time_seconds);
Status apply_gain_envelope(WavAudio* audio, const std::vector<GainEnvelopePoint>& points);
double measure_integrated_loudness(const WavAudio& audio);
double measure_sample_peak_dbfs(const WavAudio& audio);
Status limit_audio_peak(WavAudio* audio, double peak_ceiling_dbfs, bool* out_limiter_engaged);

Status normalize_audio_loudness(
    WavAudio* audio,
    const std::vector<GainEnvelopePoint>& gain_envelope,
    bool enabled,
    double target_lufs,
    double peak_ceiling_dbfs,
    AudioLoudnessStats* out_stats);

} // namespace ff7rp::pipeline
