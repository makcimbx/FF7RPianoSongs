#include "song_preview.h"

#include "cache.h"
#include "cache_artifact_writer.h"
#include "native_asset_hash.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <fstream>
#include <numbers>

namespace ff7rp::pipeline {
namespace {
constexpr std::size_t kHeaderSize = 32;
constexpr std::array<std::uint8_t, 8> kMagic{'F','7','P','R','E','V','0','1'};
constexpr std::uint64_t kMaximumSourceFrames = 600ull * kSongPreviewSampleRate;
using Bytes = std::vector<std::uint8_t>;

Status invalid() { return Status::error(StatusCode::InvalidAudio, "invalid song preview artifact"); }
void append(Bytes& bytes, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::uint64_t get(std::span<const std::uint8_t> bytes, std::size_t offset, unsigned count) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= std::uint64_t(bytes[offset + i]) << (i * 8);
    return value;
}
std::uint64_t digest(const Bytes& bytes) {
    const auto sha = native_sha256(bytes);
    return get(sha, 0, 8);
}
std::array<float, 64> waveform(const std::vector<std::int16_t>& samples) {
    std::array<float, 64> result{};
    const auto frames = samples.size() / 2;
    for (std::size_t bin = 0; bin < result.size(); ++bin) {
        const auto begin = bin * frames / result.size();
        const auto end = (bin + 1) * frames / result.size();
        double energy = 0;
        for (auto f = begin; f < end; ++f)
            for (unsigned c = 0; c < 2; ++c) {
                const double value = samples[f * 2 + c] / 32768.0;
                energy += value * value;
            }
        if (end > begin) result[bin] = static_cast<float>(std::sqrt(energy / (2 * (end - begin))));
    }
    return result;
}
void fft(std::vector<std::complex<double>>& a) {
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const auto step = std::polar(1.0, -2 * std::numbers::pi / double(len));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w = 1;
            for (std::size_t j = 0; j < len / 2; ++j, w *= step) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
            }
        }
    }
}
}

std::vector<std::uint8_t> song_preview_spectrum(std::span<const std::int16_t> samples) {
    constexpr std::size_t n = 2048, bands = kSongPreviewSpectrumBands, hop = kSongPreviewSpectrumHop;
    constexpr double range_db = 36, decay = 0.06;
    const auto frames = samples.size() / 2, hops = (frames + hop - 1) / hop;
    // Log-spaced 50 Hz-16 kHz bands, at least one FFT bin each.
    std::array<std::size_t, bands + 1> edge{};
    for (std::size_t b = 0; b <= bands; ++b)
        edge[b] = std::max<std::size_t>(b ? edge[b - 1] + 1 : 1, static_cast<std::size_t>(std::lround(
            50.0 * std::pow(16000.0 / 50.0, double(b) / bands) * n / kSongPreviewSampleRate)));
    std::vector<double> db(hops * bands);
    std::vector<std::complex<double>> x(n);
    for (std::size_t h = 0; h < hops; ++h) {
        for (std::size_t i = 0; i < n; ++i) {
            const auto f = h * hop + i - n / 2;
            const double mono = h * hop + i < n / 2 || f >= frames ? 0.0
                : (samples[f * 2] + samples[f * 2 + 1]) / 65536.0;
            x[i] = mono * (0.5 - 0.5 * std::cos(2 * std::numbers::pi * double(i) / n));
        }
        fft(x);
        for (std::size_t b = 0; b < bands; ++b) {
            double energy = 1e-12;
            for (auto k = edge[b]; k < edge[b + 1]; ++k) energy += std::norm(x[k]);
            db[h * bands + b] = 10 * std::log10(energy);
        }
    }
    // Per-band peak normalization, floored relative to the loudest band and near -56 dBFS
    // (0 dB here) so near-silent bands and clips stay dark instead of filling the bars.
    const double loudest = db.empty() ? 0 : *std::max_element(db.begin(), db.end());
    std::vector<std::uint8_t> levels(db.size());
    for (std::size_t b = 0; b < bands; ++b) {
        double top = std::max(loudest - 2 * range_db, 0.0), level = 0;
        for (std::size_t h = 0; h < hops; ++h) top = std::max(top, db[h * bands + b]);
        for (std::size_t h = 0; h < hops; ++h) {
            level = std::max(std::clamp((db[h * bands + b] - top) / range_db + 1, 0.0, 1.0), level - decay);
            levels[h * bands + b] = static_cast<std::uint8_t>(std::lround(level * 255));
        }
    }
    return levels;
}

Status read_song_preview_pcm(const SongPreviewArtifact& artifact, std::vector<std::int16_t>* out_samples) {
    if (!out_samples) return Status::error(StatusCode::InvalidArgument, "null preview samples output");
    out_samples->clear();
    if (artifact.frame_count == 0 || artifact.frame_count > kSongPreviewMaxFrames ||
        !std::isfinite(artifact.source_start_seconds) || artifact.source_start_seconds < 0 ||
        artifact.source_start_seconds + double(artifact.frame_count) / kSongPreviewSampleRate > 600.0)
        return invalid();
    for (float value : artifact.waveform) if (!std::isfinite(value) || value < 0 || value > 1) return invalid();
    std::ifstream in(artifact.path, std::ios::binary | std::ios::ate);
    const auto expected = kHeaderSize + std::size_t(artifact.frame_count) * 4;
    if (!in || in.tellg() != static_cast<std::streamoff>(expected)) return invalid();
    Bytes bytes(expected);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
        get(bytes, 8, 4) != kSongPreviewSampleRate || get(bytes, 12, 2) != kSongPreviewChannels ||
        get(bytes, 14, 2) != 16 || get(bytes, 16, 4) != artifact.frame_count ||
        get(bytes, 20, 4) != 0 || get(bytes, 24, 8) > kMaximumSourceFrames ||
        double(get(bytes, 24, 8)) / kSongPreviewSampleRate != artifact.source_start_seconds ||
        digest(bytes) != artifact.content_hash) return invalid();
    std::vector<std::int16_t> samples(artifact.frame_count * 2u);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(get(bytes, kHeaderSize + i * 2, 2)));
    if (waveform(samples) != artifact.waveform) return invalid();
    *out_samples = std::move(samples);
    return Status::ok_status();
}

Status prepare_song_preview(const WavAudio& audio, const std::filesystem::path& cache_directory,
    std::shared_ptr<const SongPreviewArtifact>* out_preview) {
    if (!out_preview) return Status::error(StatusCode::InvalidArgument, "null preview output");
    out_preview->reset();
    const auto frames = audio.source_frame_count;
    if (audio.sample_rate != kSongPreviewSampleRate || audio.channels != kSongPreviewChannels ||
        frames == 0 || frames > kMaximumSourceFrames || audio.stereo_samples.size() % 2 != 0 ||
        audio.frame_count() < frames) return invalid();
    for (float value : audio.stereo_samples) if (!std::isfinite(value)) return invalid();
    const auto count = std::min<std::size_t>(frames, kSongPreviewMaxFrames);
    constexpr std::size_t hop = kSongPreviewSampleRate / 2;
    std::vector<double> activity((frames + hop - 1) / hop);
    for (std::size_t block = 0; block < activity.size(); ++block) {
        const auto end = std::min(frames, (block + 1) * hop);
        double energy = 0;
        for (auto f = block * hop; f < end; ++f)
            for (unsigned c = 0; c < 2; ++c) {
                const double value = std::clamp(double(audio.stereo_samples[f * 2 + c]), -1.0, 1.0);
                energy += value * value;
            }
        // RMS summed across blocks favors sustained activity over a single loud transient.
        activity[block] = std::sqrt(energy / (2 * (end - block * hop)));
    }
    std::size_t start = 0;
    if (frames > count) {
        const auto margin = std::min<std::size_t>(5 * kSongPreviewSampleRate, (frames - count) / 4);
        double best = -1;
        for (auto candidate = margin; candidate <= frames - count - margin; candidate += hop) {
            double score = 0;
            for (auto block = candidate / hop; block < (candidate + count) / hop; ++block)
                score += activity[block];
            if (score > best) { best = score; start = candidate; }
        }
    }
    std::vector<std::int16_t> samples(count * 2);
    const auto fade = std::min<std::size_t>(kSongPreviewSampleRate / 4, count / 2);
    for (std::size_t f = 0; f < count; ++f) {
        const double gain = fade == 0 ? 1.0 : std::min({1.0, double(f) / fade, double(count - 1 - f) / fade});
        for (unsigned c = 0; c < 2; ++c)
            samples[f * 2 + c] = static_cast<std::int16_t>(std::lround(
                std::clamp(double(audio.stereo_samples[(start + f) * 2 + c]), -1.0, 1.0) * gain * 32767.0));
    }
    Bytes bytes(kMagic.begin(), kMagic.end());
    append(bytes, kSongPreviewSampleRate, 4); append(bytes, kSongPreviewChannels, 2);
    append(bytes, 16, 2); append(bytes, count, 4); append(bytes, 0, 4); append(bytes, start, 8);
    for (auto sample : samples) append(bytes, static_cast<std::uint16_t>(sample), 2);
    auto artifact = std::make_shared<SongPreviewArtifact>();
    artifact->content_hash = digest(bytes);
    artifact->path = cache_directory / ("preview-" + hex64(artifact->content_hash) + ".pcm");
    artifact->frame_count = static_cast<std::uint32_t>(count);
    artifact->source_start_seconds = double(start) / kSongPreviewSampleRate;
    artifact->waveform = waveform(samples);
    std::error_code ec;
    bool needs_write = true;
    if (std::filesystem::exists(artifact->path, ec)) {
        std::vector<std::int16_t> verified;
        needs_write = !read_song_preview_pcm(*artifact, &verified).ok();
    }
    if (ec) return Status::error(StatusCode::IoError, ec.message());
    if (needs_write) {
        // Repair corrupt bytes atomically; never rewrite a valid content-addressed artifact.
        const auto status = cache_artifact_writer::write_binary_file(artifact->path.string(), bytes);
        if (!status.ok()) return status;
    }
    *out_preview = std::move(artifact);
    return Status::ok_status();
}
} // namespace ff7rp::pipeline
