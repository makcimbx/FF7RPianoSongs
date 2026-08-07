#include "runtime_artifact_validator.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

#include "cache.h"
#include "cache_manifest_renderer.h"
#include "mabf_builder.h"
#include "pipeline_limits.h"

namespace ff7rp::pipeline {
namespace {

constexpr std::size_t kMaxRuntimeManifestBytes = 1u << 20;

bool read_bounded_file(const std::string& path, const std::size_t limit, std::vector<std::uint8_t>* bytes) {
    if (!bytes) return false;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamsize size = static_cast<std::streamsize>(file.tellg());
    if (size < 0 || static_cast<std::uint64_t>(size) > limit) return false;
    file.seekg(0, std::ios::beg);
    bytes->resize(static_cast<std::size_t>(size));
    return size == 0 || static_cast<bool>(file.read(reinterpret_cast<char*>(bytes->data()), size));
}

bool mabf_metadata_equal(const MabfArtifactMetadata& left, const MabfArtifactMetadata& right) {
    return left.digest == right.digest && left.byte_count == right.byte_count &&
        left.logical_source_frames == right.logical_source_frames &&
        left.hca_frame_count == right.hca_frame_count && left.sample_rate == right.sample_rate &&
        left.channels == right.channels && left.inserted_samples == right.inserted_samples &&
        left.appended_samples == right.appended_samples && left.block_size == right.block_size;
}

} // namespace

bool runtime_artifacts_match(
    const LoadedSong& song,
    const SongConfig& source_config,
    const RuntimeArtifactTrace& trace,
    std::string* failure_reason) {
    const auto report = [&](const char* stage) {
        if (trace) trace(stage);
    };
    const auto fail = [&](const std::string& reason) {
        if (failure_reason) *failure_reason = reason;
        return false;
    };
    std::vector<std::uint8_t> mabf;
    report("mabf_read_started");
    if (!read_bounded_file(song.cache_sidecar_path, kMaxMabfBytes, &mabf)) return fail("mabf_read");
    report("mabf_validation_started");
    MabfArtifactMetadata actual;
    const Status validation = song.config.metronome_enabled
        ? validate_adaptive_metronome_mabf(mabf, song.audio.source_frame_count, &actual)
        : validate_clean_mabf(mabf, song.audio.source_frame_count, &actual);
    if (!validation.ok()) return fail("mabf_validation:" + validation.message);
    report("mabf_hash_started");
    actual.digest = fnv1a64_append(kFnv1a64OffsetBasis, mabf.data(), mabf.size());
    if (!mabf_metadata_equal(actual, song.mabf_metadata)) return fail("mabf_metadata");
    std::vector<std::uint8_t> actual_manifest;
    report("manifest_read_started");
    if (!read_bounded_file(song.cache_manifest_path, kMaxRuntimeManifestBytes, &actual_manifest)) {
        return fail("manifest_read");
    }
    std::string expected_manifest;
    report("manifest_render_started");
    const Status render_status = render_cache_manifest(song, source_config, &expected_manifest);
    if (!render_status.ok()) return fail("manifest_render:" + render_status.message);
    if (actual_manifest.size() != expected_manifest.size()) return fail("manifest_size");
    if (!std::equal(actual_manifest.begin(), actual_manifest.end(), expected_manifest.begin())) {
        const auto mismatch = std::mismatch(
            actual_manifest.begin(), actual_manifest.end(), expected_manifest.begin());
        const std::size_t offset = static_cast<std::size_t>(mismatch.first - actual_manifest.begin());
        const std::size_t actual_line_start = offset == 0 ? 0 :
            static_cast<std::size_t>(std::find(
                std::make_reverse_iterator(actual_manifest.begin() + offset),
                actual_manifest.rend(), static_cast<std::uint8_t>('\n')).base() - actual_manifest.begin());
        const auto actual_line_end_it = std::find(
            actual_manifest.begin() + offset, actual_manifest.end(), static_cast<std::uint8_t>('\n'));
        const std::size_t actual_line_end = static_cast<std::size_t>(actual_line_end_it - actual_manifest.begin());
        const std::size_t expected_line_start = offset == 0 ? 0 :
            expected_manifest.rfind('\n', offset - 1) + 1;
        const std::size_t expected_line_end = expected_manifest.find('\n', offset);
        const std::string actual_line(
            actual_manifest.begin() + actual_line_start, actual_manifest.begin() + actual_line_end);
        const std::string expected_line = expected_manifest.substr(
            expected_line_start,
            (expected_line_end == std::string::npos ? expected_manifest.size() : expected_line_end) -
                expected_line_start);
        return fail("manifest_bytes:offset=" + std::to_string(offset) +
            ":actual=" + actual_line + ":expected=" + expected_line);
    }
    if (fnv1a64_append(kFnv1a64OffsetBasis, actual_manifest.data(), actual_manifest.size()) !=
        song.manifest_digest) {
        return fail("manifest_digest");
    }
    report("artifact_validation_ready");
    return true;
}

} // namespace ff7rp::pipeline
