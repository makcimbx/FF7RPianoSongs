#include "audio_loudness.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

namespace ff7rp::pipeline {
namespace {

constexpr double kLoudnessOffset = -0.691;
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kRelativeGateLu = -10.0;
constexpr double kMaximumGainDb = 24.0;
constexpr double kMinimumGainDb = -24.0;

struct Biquad {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
    double z1 = 0.0;
    double z2 = 0.0;

    double process(const double input) {
        const double output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }
};

double energy_to_lufs(const double energy) {
    if (!(energy > 0.0) || !std::isfinite(energy)) return -100.0;
    return kLoudnessOffset + 10.0 * std::log10(energy);
}

double integrated_loudness(const WavAudio& audio) {
    if (audio.sample_rate != 48000 || audio.channels != 2 || audio.frame_count() == 0) return -100.0;

    // ITU-R BS.1770 K-weighting coefficients for 48 kHz PCM.
    Biquad shelf_l{1.53512485958697, -2.69169618940638, 1.19839281085285,
        -1.69065929318241, 0.73248077421585};
    Biquad shelf_r = shelf_l;
    Biquad highpass_l{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
    Biquad highpass_r = highpass_l;

    const std::size_t frames = audio.frame_count();
    std::vector<double> frame_energy(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double left = highpass_l.process(shelf_l.process(audio.stereo_samples[frame * 2]));
        const double right = highpass_r.process(shelf_r.process(audio.stereo_samples[frame * 2 + 1]));
        frame_energy[frame] = left * left + right * right;
    }

    const std::size_t block_frames = std::min<std::size_t>(frames, audio.sample_rate * 400ull / 1000ull);
    const std::size_t step_frames = std::max<std::size_t>(1, audio.sample_rate * 100ull / 1000ull);
    std::vector<double> prefix(frames + 1, 0.0);
    for (std::size_t frame = 0; frame < frames; ++frame) prefix[frame + 1] = prefix[frame] + frame_energy[frame];

    std::vector<double> blocks;
    for (std::size_t start = 0;; start += step_frames) {
        const std::size_t end = std::min(frames, start + block_frames);
        if (end <= start) break;
        blocks.push_back((prefix[end] - prefix[start]) / static_cast<double>(end - start));
        if (end == frames) break;
    }

    double absolute_sum = 0.0;
    std::size_t absolute_count = 0;
    for (const double energy : blocks) {
        if (energy_to_lufs(energy) >= kAbsoluteGateLufs) {
            absolute_sum += energy;
            ++absolute_count;
        }
    }
    if (absolute_count == 0) return -100.0;

    const double relative_gate = energy_to_lufs(absolute_sum / static_cast<double>(absolute_count)) + kRelativeGateLu;
    const double final_gate = std::max(kAbsoluteGateLufs, relative_gate);
    double gated_sum = 0.0;
    std::size_t gated_count = 0;
    for (const double energy : blocks) {
        if (energy_to_lufs(energy) >= final_gate) {
            gated_sum += energy;
            ++gated_count;
        }
    }
    return gated_count == 0 ? -100.0 : energy_to_lufs(gated_sum / static_cast<double>(gated_count));
}

double sample_peak(const WavAudio& audio) {
    double peak = 0.0;
    for (const float sample : audio.stereo_samples) peak = std::max(peak, std::fabs(static_cast<double>(sample)));
    return peak;
}

double amplitude_to_dbfs(const double amplitude) {
    return amplitude > 0.0 ? 20.0 * std::log10(amplitude) : -100.0;
}

bool apply_gain_and_limiter(WavAudio* audio, const double gain_db, const double ceiling_dbfs) {
    const double gain = std::pow(10.0, gain_db / 20.0);
    const double ceiling = std::pow(10.0, ceiling_dbfs / 20.0);
    const std::size_t frames = audio->frame_count();
    const std::size_t lookahead = std::max<std::size_t>(1, audio->sample_rate * 5ull / 1000ull);
    const double release = std::exp(-1.0 / (0.100 * static_cast<double>(audio->sample_rate)));

    std::vector<double> peaks(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double left = std::fabs(static_cast<double>(audio->stereo_samples[frame * 2]) * gain);
        const double right = std::fabs(static_cast<double>(audio->stereo_samples[frame * 2 + 1]) * gain);
        peaks[frame] = std::max(left, right);
    }

    std::vector<double> future_peak(frames);
    std::deque<std::size_t> maximums;
    for (std::size_t frame = frames; frame-- > 0;) {
        while (!maximums.empty() && maximums.front() >= frame + lookahead) maximums.pop_front();
        while (!maximums.empty() && peaks[maximums.back()] <= peaks[frame]) maximums.pop_back();
        maximums.push_back(frame);
        future_peak[frame] = peaks[maximums.front()];
    }

    double limiter_gain = 1.0;
    bool limiter_engaged = false;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double desired = future_peak[frame] > ceiling ? ceiling / future_peak[frame] : 1.0;
        if (desired < 1.0) limiter_engaged = true;
        if (desired < limiter_gain) limiter_gain = desired;
        else limiter_gain = release * limiter_gain + (1.0 - release) * desired;
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const double value = static_cast<double>(audio->stereo_samples[frame * 2 + channel]) * gain * limiter_gain;
            audio->stereo_samples[frame * 2 + channel] = static_cast<float>(std::clamp(value, -ceiling, ceiling));
        }
    }
    return limiter_engaged;
}

} // namespace

Status validate_gain_envelope(const std::vector<GainEnvelopePoint>& points) {
    if (points.empty()) return Status::ok_status();
    if (points.size() > kMaximumGainEnvelopePoints) {
        return Status::error(StatusCode::InvalidArgument, "gain envelope exceeds the 64-point safety limit");
    }
    for (std::size_t index = 0; index < points.size(); ++index) {
        const GainEnvelopePoint& point = points[index];
        if (!std::isfinite(point.time_seconds) || point.time_seconds < 0.0) {
            return Status::error(StatusCode::InvalidArgument,
                "gain envelope times must be finite and non-negative");
        }
        if (!std::isfinite(point.gain_db) || point.gain_db < kGainEnvelopeMinimumDb ||
            point.gain_db > kGainEnvelopeMaximumDb) {
            return Status::error(StatusCode::InvalidArgument,
                "gain envelope gains must be finite and between -12 and 12 dB");
        }
        if (index > 0 && point.time_seconds <= points[index - 1].time_seconds) {
            return Status::error(StatusCode::InvalidArgument,
                "gain envelope times must be strictly increasing without duplicates");
        }
    }
    return Status::ok_status();
}

double gain_envelope_amplitude_at(
    const std::vector<GainEnvelopePoint>& points,
    const double time_seconds) {
    if (points.empty()) return 1.0;
    const auto amplitude = [](const double gain_db) { return std::pow(10.0, gain_db / 20.0); };
    if (time_seconds <= points.front().time_seconds) return amplitude(points.front().gain_db);
    if (time_seconds >= points.back().time_seconds) return amplitude(points.back().gain_db);
    const auto next = std::upper_bound(points.begin(), points.end(), time_seconds,
        [](const double time, const GainEnvelopePoint& point) { return time < point.time_seconds; });
    const GainEnvelopePoint& begin = *(next - 1);
    const GainEnvelopePoint& end = *next;
    const double fraction = (time_seconds - begin.time_seconds) / (end.time_seconds - begin.time_seconds);
    return amplitude(begin.gain_db) + (amplitude(end.gain_db) - amplitude(begin.gain_db)) * fraction;
}

Status apply_gain_envelope(WavAudio* audio, const std::vector<GainEnvelopePoint>& points) {
    if (!audio) return Status::error(StatusCode::InvalidArgument, "audio must not be null");
    Status status = validate_gain_envelope(points);
    if (!status.ok()) return status;
    if (points.empty()) return Status::ok_status();
    if (audio->sample_rate != 48000 || audio->channels != 2 || audio->frame_count() == 0) {
        return Status::error(StatusCode::InvalidAudio, "gain envelope requires non-empty 48 kHz stereo PCM");
    }
    const std::size_t frames = audio->frame_count();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double time_seconds = static_cast<double>(frame) / static_cast<double>(audio->sample_rate);
        const double gain = gain_envelope_amplitude_at(points, time_seconds);
        if (!std::isfinite(gain)) {
            return Status::error(StatusCode::InvalidArgument, "gain envelope produced a non-finite amplitude");
        }
        audio->stereo_samples[frame * 2] = static_cast<float>(audio->stereo_samples[frame * 2] * gain);
        audio->stereo_samples[frame * 2 + 1] = static_cast<float>(audio->stereo_samples[frame * 2 + 1] * gain);
    }
    return Status::ok_status();
}

double measure_integrated_loudness(const WavAudio& audio) {
    return integrated_loudness(audio);
}

double measure_sample_peak_dbfs(const WavAudio& audio) {
    return amplitude_to_dbfs(sample_peak(audio));
}

Status limit_audio_peak(
    WavAudio* audio,
    const double peak_ceiling_dbfs,
    bool* out_limiter_engaged) {
    if (!audio || !out_limiter_engaged) {
        return Status::error(StatusCode::InvalidArgument, "audio and limiter result must not be null");
    }
    if (audio->sample_rate != 48000 || audio->channels != 2 || audio->frame_count() == 0) {
        return Status::error(StatusCode::InvalidAudio, "peak limiter requires non-empty 48 kHz stereo PCM");
    }
    if (!std::isfinite(peak_ceiling_dbfs) || peak_ceiling_dbfs < -6.0 || peak_ceiling_dbfs > 0.0) {
        return Status::error(StatusCode::InvalidArgument, "peak ceiling must be between -6 and 0 dBFS");
    }
    *out_limiter_engaged = apply_gain_and_limiter(audio, 0.0, peak_ceiling_dbfs);
    return Status::ok_status();
}

Status normalize_audio_loudness(
    WavAudio* audio,
    const std::vector<GainEnvelopePoint>& gain_envelope,
    const bool enabled,
    const double target_lufs,
    const double peak_ceiling_dbfs,
    AudioLoudnessStats* out_stats) {
    if (!audio || !out_stats) return Status::error(StatusCode::InvalidArgument, "audio and out_stats must not be null");
    if (audio->sample_rate != 48000 || audio->channels != 2 || audio->frame_count() == 0) {
        return Status::error(StatusCode::InvalidAudio, "loudness normalization requires non-empty 48 kHz stereo PCM");
    }
    if (!std::isfinite(target_lufs) || target_lufs < -30.0 || target_lufs > -5.0) {
        return Status::error(StatusCode::InvalidArgument, "loudness target must be between -30 and -5 LUFS");
    }
    if (!std::isfinite(peak_ceiling_dbfs) || peak_ceiling_dbfs < -6.0 || peak_ceiling_dbfs > 0.0) {
        return Status::error(StatusCode::InvalidArgument, "loudness peak ceiling must be between -6 and 0 dBFS");
    }

    AudioLoudnessStats stats;
    Status status = validate_gain_envelope(gain_envelope);
    if (!status.ok()) return status;
    if (!gain_envelope.empty()) {
        status = apply_gain_envelope(audio, gain_envelope);
        if (!status.ok()) return status;
        stats.gain_envelope_applied = true;
        stats.gain_envelope_point_count = gain_envelope.size();
        const auto minimum = std::min_element(gain_envelope.begin(), gain_envelope.end(),
            [](const GainEnvelopePoint& a, const GainEnvelopePoint& b) { return a.gain_db < b.gain_db; });
        const auto maximum = std::max_element(gain_envelope.begin(), gain_envelope.end(),
            [](const GainEnvelopePoint& a, const GainEnvelopePoint& b) { return a.gain_db < b.gain_db; });
        stats.gain_envelope_min_gain_db = minimum->gain_db;
        stats.gain_envelope_max_gain_db = maximum->gain_db;
    }
    stats.input_lufs = integrated_loudness(*audio);
    stats.input_peak_dbfs = amplitude_to_dbfs(sample_peak(*audio));
    stats.output_lufs = stats.input_lufs;
    stats.output_peak_dbfs = stats.input_peak_dbfs;
    if (enabled && stats.input_lufs > -99.0) {
        stats.applied_gain_db = std::clamp(target_lufs - stats.input_lufs, kMinimumGainDb, kMaximumGainDb);
        stats.gain_applied = std::fabs(stats.applied_gain_db) > 0.01;
        stats.limiter_engaged = apply_gain_and_limiter(audio, stats.applied_gain_db, peak_ceiling_dbfs);
        stats.output_lufs = integrated_loudness(*audio);
        stats.output_peak_dbfs = amplitude_to_dbfs(sample_peak(*audio));
        stats.normalized = stats.gain_applied || stats.limiter_engaged;
    }
    *out_stats = stats;
    return Status::ok_status();
}

} // namespace ff7rp::pipeline
