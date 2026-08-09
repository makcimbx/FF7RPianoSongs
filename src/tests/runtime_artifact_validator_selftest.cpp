#include "pipeline/runtime_artifact_validator.h"

#include "pipeline/cache.h"
#include "pipeline/cache_manifest_renderer.h"
#include "pipeline/hca/hca_encoder.h"
#include "pipeline/mabf_builder.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/runtime_cache_codec.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using ff7rp::pipeline::LoadedSong;
using ff7rp::pipeline::MabfArtifactMetadata;
using ff7rp::pipeline::SongConfig;

struct ArtifactData {
    std::vector<std::uint8_t> mabf;
    MabfArtifactMetadata metadata;
    bool adaptive = false;
};

struct Fixture {
    LoadedSong song;
    SongConfig source_config;
    std::string manifest;
};

bool write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(out);
}

bool write_text(const std::filesystem::path& path, const std::string& text) {
    return write_bytes(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

ArtifactData make_artifact(const bool adaptive) {
    HcaEncodeConfig config;
    config.target_samples = 1;
    const std::vector<std::int16_t> clean_pcm(2, 0);
    const std::vector<std::int16_t> strong_pcm{12000, -9000};
    const auto clean = encode_hca_48k_stereo_256k(clean_pcm, config);
    const auto strong = adaptive ? encode_hca_48k_stereo_256k(strong_pcm, config) : clean;
    const ff7rp::pipeline::MabfModeHcaPayloads modes{&strong, &clean, &clean};
    const auto built = ff7rp::pipeline::build_mabf_from_mode_hca(modes);
    if (!built.status.ok()) throw std::runtime_error("failed to build MABF fixture: " + built.status.message);

    ArtifactData artifact;
    artifact.mabf = built.bytes;
    artifact.adaptive = adaptive;
    const auto status = adaptive
        ? ff7rp::pipeline::validate_adaptive_metronome_mabf(artifact.mabf, 1, &artifact.metadata)
        : ff7rp::pipeline::validate_clean_mabf(artifact.mabf, 1, &artifact.metadata);
    if (!status.ok()) throw std::runtime_error("failed to validate MABF fixture: " + status.message);
    artifact.metadata.digest = ff7rp::pipeline::fnv1a64_append(
        ff7rp::pipeline::kFnv1a64OffsetBasis, artifact.mabf.data(), artifact.mabf.size());
    return artifact;
}

Fixture make_fixture(
    const std::filesystem::path& root,
    const std::string& name,
    const ArtifactData& artifact) {
    Fixture fixture;
    fixture.song.id = "validator-" + name;
    fixture.song.directory = root.string();
    fixture.song.cache_sidecar_path = (root / (name + ".mabf")).string();
    fixture.song.cache_manifest_path = (root / (name + ".manifest")).string();
    fixture.song.cache_key = 0x123456789abcdef0ull;
    fixture.song.chart_policy_identity = "validator-policy";
    fixture.song.config.schema = "ff7rpianosongs.song.v2";
    fixture.song.config.title = "Validator\ncontrol:\x01:end";
    fixture.song.config.bpm = 123.5;
    fixture.song.config.metronome_enabled = artifact.adaptive;
    fixture.song.audio.source_frame_count = 1;
    fixture.song.mabf_metadata = artifact.metadata;
    fixture.song.audio_source_path = "audio.wav";
    fixture.source_config = fixture.song.config;
    fixture.source_config.title = "Source\ncontrol:\x02:end";
    const auto render = ff7rp::pipeline::render_cache_manifest(
        fixture.song, fixture.source_config, &fixture.manifest);
    if (!render.ok()) throw std::runtime_error("failed to render manifest fixture: " + render.message);
    fixture.song.manifest_digest = ff7rp::pipeline::fnv1a64_append(
        ff7rp::pipeline::kFnv1a64OffsetBasis, fixture.manifest.data(), fixture.manifest.size());
    std::string confirmed_manifest;
    const auto confirmed_render = ff7rp::pipeline::render_cache_manifest(
        fixture.song, fixture.source_config, &confirmed_manifest);
    if (!confirmed_render.ok() || confirmed_manifest != fixture.manifest) {
        throw std::runtime_error("manifest fixture changed after digest assignment");
    }
    if (!write_bytes(fixture.song.cache_sidecar_path, artifact.mabf) ||
        !write_text(fixture.song.cache_manifest_path, fixture.manifest)) {
        throw std::runtime_error("failed to write validator fixture");
    }
    return fixture;
}

struct ValidationResult {
    bool matched = false;
    std::string reason = "unchanged-sentinel";
    std::vector<std::string> trace;
};

ValidationResult validate(const Fixture& fixture) {
    ValidationResult result;
    result.matched = ff7rp::pipeline::runtime_artifacts_match(
        fixture.song, fixture.source_config,
        [&](const char* stage) { result.trace.emplace_back(stage); },
        &result.reason);
    return result;
}

bool expect(
    const ValidationResult& result,
    const bool matched,
    const std::string& reason,
    const std::vector<std::string>& trace,
    const char* name) {
    if (result.matched == matched && result.reason == reason && result.trace == trace) return true;
    std::cerr << name << " changed: matched=" << result.matched << " reason=" << result.reason << "\n";
    return false;
}

bool metadata_equal(const MabfArtifactMetadata& left, const MabfArtifactMetadata& right) {
    return left.digest == right.digest && left.byte_count == right.byte_count &&
        left.logical_source_frames == right.logical_source_frames &&
        left.hca_frame_count == right.hca_frame_count && left.sample_rate == right.sample_rate &&
        left.channels == right.channels && left.inserted_samples == right.inserted_samples &&
        left.appended_samples == right.appended_samples && left.block_size == right.block_size;
}

bool validator_observables_unchanged(
    const Fixture& fixture,
    const LoadedSong& song_before,
    const SongConfig& source_before,
    const std::string& manifest_before) {
    std::string manifest_after;
    return ff7rp::pipeline::song_configs_equal(fixture.song.config, song_before.config) &&
        ff7rp::pipeline::song_configs_equal(fixture.source_config, source_before) &&
        fixture.song.cache_sidecar_path == song_before.cache_sidecar_path &&
        fixture.song.cache_manifest_path == song_before.cache_manifest_path &&
        fixture.song.audio.sample_rate == song_before.audio.sample_rate &&
        fixture.song.audio.channels == song_before.audio.channels &&
        fixture.song.audio.source_frame_count == song_before.audio.source_frame_count &&
        fixture.song.audio.stereo_samples == song_before.audio.stereo_samples &&
        metadata_equal(fixture.song.mabf_metadata, song_before.mabf_metadata) &&
        fixture.song.manifest_digest == song_before.manifest_digest &&
        ff7rp::pipeline::render_cache_manifest(
            fixture.song, fixture.source_config, &manifest_after).ok() &&
        manifest_after == manifest_before;
}

struct MetadataMutation {
    const char* name;
    std::function<void(MabfArtifactMetadata*)> apply;
};

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "ff7rp_runtime_artifact_validator_selftest";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        std::cerr << "failed to create temporary fixture directory\n";
        return 1;
    }

    try {
        const ArtifactData clean = make_artifact(false);
        const ArtifactData adaptive = make_artifact(true);
        const std::vector<std::string> success_trace{
            "mabf_read_started", "mabf_validation_started", "mabf_hash_started",
            "manifest_read_started", "manifest_render_started", "artifact_validation_ready"};

        const Fixture clean_fixture = make_fixture(root, "clean", clean);
        const Fixture adaptive_fixture = make_fixture(root, "adaptive", adaptive);
        const LoadedSong clean_before = clean_fixture.song;
        const SongConfig source_before = clean_fixture.source_config;
        if (!expect(validate(clean_fixture), true, "unchanged-sentinel", success_trace, "clean success") ||
            !expect(validate(adaptive_fixture), true, "unchanged-sentinel", success_trace, "adaptive success")) {
            return 2;
        }
        if (!validator_observables_unchanged(
                clean_fixture, clean_before, source_before, clean_fixture.manifest)) {
            std::cerr << "validator mutated song or source configuration\n";
            return 3;
        }

        Fixture missing_mabf = make_fixture(root, "missing-mabf", clean);
        std::filesystem::remove(missing_mabf.song.cache_sidecar_path, ec);
        if (!expect(validate(missing_mabf), false, "mabf_read", {"mabf_read_started"}, "missing MABF")) {
            return 4;
        }

        Fixture oversized_mabf = make_fixture(root, "oversized-mabf", clean);
        {
            std::ofstream out(oversized_mabf.song.cache_sidecar_path, std::ios::binary | std::ios::trunc);
            out.seekp(static_cast<std::streamoff>(ff7rp::pipeline::kMaxMabfBytes));
            out.put('\0');
        }
        if (!expect(validate(oversized_mabf), false, "mabf_read", {"mabf_read_started"}, "oversized MABF")) {
            return 5;
        }

        Fixture corrupt_mabf = make_fixture(root, "corrupt-mabf", clean);
        if (!write_bytes(corrupt_mabf.song.cache_sidecar_path, {0x6d, 0x61, 0x62, 0x66}) ||
            !expect(validate(corrupt_mabf), false,
                "mabf_validation:MABF size is outside release bounds",
                {"mabf_read_started", "mabf_validation_started"}, "corrupt MABF")) {
            return 6;
        }

        Fixture wrong_selection = make_fixture(root, "wrong-selection", adaptive);
        wrong_selection.song.config.metronome_enabled = false;
        const ValidationResult wrong_selection_result = validate(wrong_selection);
        if (!expect(wrong_selection_result, false,
                "mabf_validation:disabled metronome requires byte-identical clean Mode0/Mode1/Mode2 payloads",
                {"mabf_read_started", "mabf_validation_started"}, "clean/adaptive validator selection")) {
            return 7;
        }

        Fixture legacy_disabled_manifest = make_fixture(root, "legacy-disabled-manifest", clean);
        const std::string current_mapping =
            "metronome_adaptive_mode_mapping=mode0_clean,mode1_clean,mode2_clean";
        const std::string legacy_mapping =
            "metronome_adaptive_mode_mapping=mode0_strong_guide,mode1_weak_guide,mode2_clean";
        const std::size_t mapping_offset = legacy_disabled_manifest.manifest.find(current_mapping);
        if (mapping_offset == std::string::npos) return 7;
        legacy_disabled_manifest.manifest.replace(mapping_offset, current_mapping.size(), legacy_mapping);
        if (!write_text(legacy_disabled_manifest.song.cache_manifest_path, legacy_disabled_manifest.manifest) ||
            !expect(validate(legacy_disabled_manifest), false, "manifest_size",
                {"mabf_read_started", "mabf_validation_started", "mabf_hash_started",
                    "manifest_read_started", "manifest_render_started"},
                "legacy disabled manifest invalidation")) {
            return 7;
        }

        const std::vector<MetadataMutation> metadata_mutations{
            {"digest", [](MabfArtifactMetadata* value) { ++value->digest; }},
            {"byte_count", [](MabfArtifactMetadata* value) { ++value->byte_count; }},
            {"logical_source_frames", [](MabfArtifactMetadata* value) { ++value->logical_source_frames; }},
            {"hca_frame_count", [](MabfArtifactMetadata* value) { ++value->hca_frame_count; }},
            {"sample_rate", [](MabfArtifactMetadata* value) { ++value->sample_rate; }},
            {"channels", [](MabfArtifactMetadata* value) { ++value->channels; }},
            {"inserted_samples", [](MabfArtifactMetadata* value) { ++value->inserted_samples; }},
            {"appended_samples", [](MabfArtifactMetadata* value) { ++value->appended_samples; }},
            {"block_size", [](MabfArtifactMetadata* value) { ++value->block_size; }},
        };
        for (const MetadataMutation& mutation : metadata_mutations) {
            Fixture metadata = make_fixture(root, "metadata-" + std::string(mutation.name), clean);
            mutation.apply(&metadata.song.mabf_metadata);
            const LoadedSong metadata_before = metadata.song;
            const SongConfig metadata_source_before = metadata.source_config;
            std::string metadata_manifest_before;
            if (!ff7rp::pipeline::render_cache_manifest(
                    metadata.song, metadata.source_config, &metadata_manifest_before).ok()) {
                return 8;
            }
            if (!expect(validate(metadata), false, "mabf_metadata",
                    {"mabf_read_started", "mabf_validation_started", "mabf_hash_started"},
                    mutation.name) ||
                !validator_observables_unchanged(
                    metadata, metadata_before, metadata_source_before, metadata_manifest_before)) {
                std::cerr << mutation.name << " metadata failure mutated validator inputs\n";
                return 8;
            }
        }

        Fixture missing_manifest = make_fixture(root, "missing-manifest", clean);
        std::filesystem::remove(missing_manifest.song.cache_manifest_path, ec);
        if (!expect(validate(missing_manifest), false, "manifest_read",
                {"mabf_read_started", "mabf_validation_started", "mabf_hash_started",
                    "manifest_read_started"}, "missing manifest")) {
            return 9;
        }

        Fixture oversized_manifest = make_fixture(root, "oversized-manifest", clean);
        {
            std::ofstream out(oversized_manifest.song.cache_manifest_path, std::ios::binary | std::ios::trunc);
            out.seekp(static_cast<std::streamoff>(1u << 20));
            out.put('\0');
        }
        if (!expect(validate(oversized_manifest), false, "manifest_read",
                {"mabf_read_started", "mabf_validation_started", "mabf_hash_started",
                    "manifest_read_started"}, "oversized manifest")) {
            return 10;
        }

        Fixture size_mismatch = make_fixture(root, "size-mismatch", clean);
        if (!write_text(size_mismatch.song.cache_manifest_path, size_mismatch.manifest + "x") ||
            !expect(validate(size_mismatch), false, "manifest_size",
                {"mabf_read_started", "mabf_validation_started", "mabf_hash_started",
                    "manifest_read_started", "manifest_render_started"}, "manifest size")) {
            return 12;
        }

        const std::vector<std::pair<std::string, std::string>> parent_mismatch_cases{
            {"first-line",
                "manifest_bytes:offset=0:actual=xersion=ff7rpianosongs.pipeline.v39:"
                "expected=version=ff7rpianosongs.pipeline.v39"},
            {"line-boundary",
                "manifest_bytes:offset=36:actual=xache_key=123456789abcdef0:"
                "expected=cache_key=123456789abcdef0"},
            {"final-unterminated", ""},
        };
        for (const auto& [name, parent_expected_reason] : parent_mismatch_cases) {
            Fixture mismatch = make_fixture(root, name, clean);
            std::string actual = mismatch.manifest;
            std::string expected_reason = parent_expected_reason;
            if (name == "first-line") {
                actual[0] = 'x';
            } else if (name == "line-boundary") {
                actual[actual.find('\n') + 1] = 'x';
            } else {
                const std::size_t final_line_offset = actual.rfind('\n', actual.size() - 2) + 1;
                actual[final_line_offset] = 'x';
                actual.back() = '!';
                expected_reason = "manifest_bytes:offset=" + std::to_string(final_line_offset) +
                    ":actual=xidi_source=!:expected=midi_source=";
            }
            // Exact reasons independently produced by the unextracted parent validator at
            // 923237c3acb005e6e50dcb16dc41b65a7205c85e in the approved isolated oracle tree.
            if (!write_text(mismatch.song.cache_manifest_path, actual) ||
                !expect(validate(mismatch), false, expected_reason,
                    {"mabf_read_started", "mabf_validation_started", "mabf_hash_started",
                        "manifest_read_started", "manifest_render_started"}, name.c_str())) {
                return 13;
            }
        }

        Fixture byte_mismatch = make_fixture(root, "byte-mismatch", clean);
        std::string mismatched_manifest = byte_mismatch.manifest;
        const std::size_t mismatch_offset = mismatched_manifest.find("control:\x01:end");
        if (mismatch_offset == std::string::npos) return 13;
        mismatched_manifest[mismatch_offset] = 'z';
        if (!write_text(byte_mismatch.song.cache_manifest_path, mismatched_manifest)) return 13;
        // Derived by invoking the unextracted runtime_artifacts_match at parent
        // 923237c3acb005e6e50dcb16dc41b65a7205c85e in the approved isolated oracle tree.
        const std::string expected_mismatch =
            "manifest_bytes:offset=356:actual=zontrol:\x01:end:expected=control:\x01:end";
        if (!expect(validate(byte_mismatch), false, expected_mismatch,
                {"mabf_read_started", "mabf_validation_started", "mabf_hash_started",
                    "manifest_read_started", "manifest_render_started"}, "manifest bytes")) {
            return 14;
        }

        Fixture digest = make_fixture(root, "digest", clean);
        digest.song.manifest_digest ^= 1u;
        if (!expect(validate(digest), false, "manifest_digest",
                {"mabf_read_started", "mabf_validation_started", "mabf_hash_started",
                    "manifest_read_started", "manifest_render_started"}, "manifest digest")) {
            return 15;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 16;
    }

    std::filesystem::remove_all(root, ec);
    std::cout << "runtime artifact validator self-test passed\n";
    return 0;
}
