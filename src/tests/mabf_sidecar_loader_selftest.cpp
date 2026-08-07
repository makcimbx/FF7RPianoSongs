#include "game/mabf_sidecar_loader.h"
#include "game/audio_sidecar_runtime.h"
#include "game/song_registry.h"

#include "core/logging.h"

#include "pipeline/hca/hca_encoder.h"
#include "pipeline/mabf_builder.h"
#include "pipeline/pipeline_limits.h"
#include "tests/test_support.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace {

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

bool write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(file);
}

bool rejects_with_empty_bytes(const std::filesystem::path& path, const std::string& expected_status)
{
    const auto result = ff7r::piano::game::load_sidecar_bytes_file(path.wstring());
    return !result.ok && result.status == expected_status && result.bytes.empty();
}

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

void write_u32_le(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

} // namespace

int main()
{
    try {
        ff7rp::tests::TemporaryDirectory temporary("ff7rp-mabf-sidecar-loader");

        HcaEncodeConfig config;
        config.target_samples = 1;
        const std::vector<std::int16_t> pcm(config.target_samples * 2u, 0);
        const std::vector<std::uint8_t> hca = encode_hca_48k_stereo_256k(pcm, config);
        const ff7rp::pipeline::MabfModeHcaPayloads modes{&hca, &hca, &hca};
        const auto built = ff7rp::pipeline::build_mabf_from_mode_hca(modes);
        if (!built.status.ok() || !built.release_valid) {
            return fail("production MABF build failed");
        }
        ff7rp::pipeline::MabfArtifactMetadata metadata;
        if (!ff7rp::pipeline::validate_structural_mabf(built.bytes, &metadata).ok()) {
            return fail("production MABF fixture did not independently validate");
        }

        const auto valid_path = temporary.path() / "valid.mabf";
        if (!write_bytes(valid_path, built.bytes)) return fail("could not write valid fixture");
        const auto valid = ff7r::piano::game::load_sidecar_bytes_file(valid_path.wstring());
        if (!valid.ok || valid.status != "ok" || valid.bytes != built.bytes) {
            return fail("valid sidecar was not loaded byte-for-byte");
        }

        namespace detail = ff7r::piano::game::audio_sead_detail;
        detail::MabfVirtualAllocation allocation;
        if (allocation.allocate_from_mabf({}) || allocation.sead_header() ||
            allocation.mabf_bytes() || allocation.allocation_size() != 0 ||
            allocation.mabf_size() != 0) {
            return fail("empty MABF allocation did not fail closed");
        }
        if (!allocation.allocate_from_mabf(built.bytes)) {
            return fail("valid MABF allocation failed");
        }
        std::uint64_t header_words[2]{};
        std::memcpy(header_words, allocation.sead_header(), sizeof(header_words));
        const std::uint64_t size32 = static_cast<std::uint32_t>(built.bytes.size());
        if (header_words[0] != 0x21 || header_words[1] != ((size32 << 32u) | size32) ||
            allocation.allocation_size() != built.bytes.size() + detail::kSeadPayloadHeaderSize ||
            allocation.mabf_size() != built.bytes.size() ||
            std::memcmp(allocation.mabf_bytes(), built.bytes.data(), built.bytes.size()) != 0) {
            return fail("MABF virtual allocation layout changed");
        }
        void* const moved_header = allocation.sead_header();
        detail::MabfVirtualAllocation moved(std::move(allocation));
        if (allocation.sead_header() || allocation.allocation_size() != 0 ||
            allocation.mabf_size() != 0 || moved.sead_header() != moved_header) {
            return fail("MABF allocation move construction changed ownership");
        }
        detail::MabfVirtualAllocation assigned;
        if (!assigned.allocate_from_mabf(std::vector<std::uint8_t>{1u})) {
            return fail("MABF move-assignment setup failed");
        }
        assigned = std::move(moved);
        if (moved.sead_header() || assigned.sead_header() != moved_header ||
            assigned.mabf_size() != built.bytes.size()) {
            return fail("MABF allocation move assignment changed ownership");
        }
        assigned.reset();
        if (assigned.sead_header() || assigned.mabf_bytes() ||
            assigned.allocation_size() != 0 || assigned.mabf_size() != 0) {
            return fail("MABF allocation reset did not clear state");
        }

        const auto runtime_log = temporary.path() / "sidecar-runtime.log";
        ff7r::piano::core::set_log_path(runtime_log.wstring());
        ff7r::piano::core::set_log_level(ff7r::piano::core::LogLevel::Debug);
        ff7r::piano::game::SongDescriptor song;
        song.id = "runtime-song";
        song.sidecar_path = valid_path.wstring();
        if (detail::sidecar_key_for_song(song) != song.id) {
            return fail("sidecar key did not prefer song id");
        }
        ff7r::piano::game::SongDescriptor path_key_song;
        path_key_song.sidecar_path = valid_path.wstring();
        if (detail::sidecar_key_for_song(path_key_song) !=
            ff7r::piano::core::narrow(valid_path.wstring())) {
            return fail("sidecar key path fallback changed");
        }
        const auto candidates = detail::sidecar_path_candidates(song);
        const std::vector<std::wstring> expected_candidates{
            (valid_path.parent_path() / "song.mabf.bin").wstring(),
            valid_path.wstring(),
            (runtime_log.parent_path() / "Music" / L"runtime-song" / L".cache" /
                L"song.mabf.bin").wstring(),
        };
        if (candidates != expected_candidates) {
            return fail("sidecar candidate ordering changed");
        }
        ff7r::piano::game::SongDescriptor duplicate_song;
        duplicate_song.sidecar_path = (valid_path.parent_path() / "song.mabf.bin").wstring();
        const auto duplicate_candidates = detail::sidecar_path_candidates(duplicate_song);
        if (duplicate_candidates.size() != 1 ||
            duplicate_candidates[0] != duplicate_song.sidecar_path) {
            return fail("sidecar candidate deduplication changed");
        }
        auto state = detail::build_sidecar_state(song);
        if (state.key != song.id || state.song_id != song.id || state.status != "ready" ||
            state.resolved_path != valid_path.wstring() || !state.allocation.sead_header() ||
            state.allocation.mabf_size() != built.bytes.size()) {
            return fail("sidecar runtime build did not use ordered fallback candidate");
        }
        detail::log_sidecar_state(state);
        ff7r::piano::game::SongDescriptor no_path_song;
        auto no_path_state = detail::build_sidecar_state(no_path_song);
        if (no_path_state.status != "no_path" || no_path_state.allocation.sead_header()) {
            return fail("sidecar runtime no-path state changed");
        }
        detail::log_sidecar_state(no_path_state);

        const auto prefix_path_a = temporary.path() / "prefix-a.mabf";
        const auto prefix_path_b = temporary.path() / "prefix-b.mabf";
        if (!write_bytes(prefix_path_a, built.bytes) || !write_bytes(prefix_path_b, built.bytes)) {
            return fail("could not write persistent-prefix fixtures");
        }
        detail::reset_audio_sidecar_selftest_counts();
        auto builder = std::make_unique<detail::ProgressiveAudioCatalogBuilder>();
        ff7r::piano::game::SongDescriptor prefix_song_a;
        prefix_song_a.id = "prefix-a";
        prefix_song_a.sidecar_path = prefix_path_a.wstring();
        auto prefix_a = builder->append(prefix_song_a);
        if (!prefix_a || detail::prepared_audio_prefix_size(*prefix_a) != 1) {
            return fail("first persistent sidecar prefix was not admitted");
        }
        const auto* stable_a = detail::find_prepared_audio_sidecar(*prefix_a, "prefix-a");
        if (!stable_a || !stable_a->allocation.sead_header()) {
            return fail("first persistent sidecar node was not addressable");
        }
        if (builder->append(prefix_song_a) || builder->snapshot() != prefix_a) {
            return fail("duplicate sidecar key changed the persistent prefix");
        }
        ff7r::piano::game::SongDescriptor failed_prefix_song;
        failed_prefix_song.id = "prefix-failed";
        if (builder->append(failed_prefix_song) || builder->snapshot() != prefix_a) {
            return fail("failed sidecar append changed the persistent prefix");
        }
        ff7r::piano::game::SongDescriptor prefix_song_b;
        prefix_song_b.id = "prefix-b";
        prefix_song_b.sidecar_path = prefix_path_b.wstring();
        auto prefix_b = builder->append(prefix_song_b);
        if (!prefix_b || detail::prepared_audio_prefix_size(*prefix_b) != 2
            || detail::find_prepared_audio_sidecar(*prefix_b, "prefix-a") != stable_a) {
            return fail("newer prefix moved an existing sidecar node");
        }
        if (detail::audio_sidecar_selftest_build_count() != 3
            || detail::audio_sidecar_selftest_allocation_count() != 2) {
            return fail("sidecar builder repeated preparation or allocation for an admitted song");
        }
        const std::vector<ff7r::piano::game::SongDescriptor> matching_prefix{
            prefix_song_a, prefix_song_b};
        if (!detail::prepared_audio_prefix_matches(*prefix_b, matching_prefix)) {
            return fail("persistent prefix identity validation rejected its descriptor sequence");
        }
        auto mismatched_prefix = matching_prefix;
        mismatched_prefix[1].id = "wrong";
        if (detail::prepared_audio_prefix_matches(*prefix_b, mismatched_prefix)) {
            return fail("persistent prefix identity validation accepted a mismatch");
        }
        builder->log_newly_accepted(prefix_a);
        builder->log_newly_accepted(prefix_b);
        builder->log_newly_accepted(prefix_b);
        std::filesystem::remove(prefix_path_a);
        std::filesystem::remove(prefix_path_b);
        builder.reset();
        if (detail::find_prepared_audio_sidecar(*prefix_b, "prefix-a") != stable_a
            || !stable_a->allocation.sead_header()) {
            return fail("prefix snapshot did not retain stable sidecar ownership");
        }
        if (detail::audio_sidecar_selftest_build_count() != 3
            || detail::audio_sidecar_selftest_allocation_count() != 2
            || detail::audio_sidecar_selftest_free_count() != 0) {
            return fail("snapshot, validation, or reoffer repeated sidecar I/O/allocation");
        }

        std::ifstream runtime_log_file(runtime_log);
        const std::string runtime_log_text{
            std::istreambuf_iterator<char>(runtime_log_file), std::istreambuf_iterator<char>()};
        if (runtime_log_text.find("[info] [audio_sead_sidecar] key=\"runtime-song\"") == std::string::npos ||
            runtime_log_text.find("status=ready") == std::string::npos ||
            runtime_log_text.find("mabf_size=0x") == std::string::npos ||
            runtime_log_text.find("[debug] [audio_sead_sidecar] key=\"\"") == std::string::npos ||
            runtime_log_text.find("status=no_path") == std::string::npos ||
            runtime_log_text.find("key=\"prefix-a\"") == std::string::npos ||
            runtime_log_text.find("key=\"prefix-b\"") == std::string::npos) {
            return fail("sidecar runtime log projection changed");
        }
        const auto prefix_a_log = runtime_log_text.find("key=\"prefix-a\"");
        const auto prefix_b_log = runtime_log_text.find("key=\"prefix-b\"");
        if (prefix_a_log >= prefix_b_log
            || prefix_a_log != runtime_log_text.rfind("key=\"prefix-a\"")
            || prefix_b_log != runtime_log_text.rfind("key=\"prefix-b\"")) {
            return fail("accepted persistent sidecar evidence was not lexical and exactly once");
        }
        prefix_a.reset();
        if (detail::audio_sidecar_selftest_free_count() != 0) {
            return fail("older prefix release freed sidecars retained by the active prefix");
        }
        prefix_b.reset();
        if (detail::audio_sidecar_selftest_free_count() != 2) {
            return fail("final prefix release did not free each sidecar allocation exactly once");
        }

        const auto empty_path_result = ff7r::piano::game::load_sidecar_bytes_file(L"");
        if (empty_path_result.ok || empty_path_result.status != "empty_path" ||
            !empty_path_result.bytes.empty()) {
            return fail("empty path did not fail closed");
        }
        const auto missing_path = temporary.path() / "missing.mabf";
        if (!rejects_with_empty_bytes(missing_path, "missing")) {
            return fail("missing sidecar did not fail closed");
        }
        const auto empty_file_path = temporary.path() / "empty.mabf";
        if (!write_bytes(empty_file_path, {})) return fail("could not write empty fixture");
        if (!rejects_with_empty_bytes(empty_file_path, "size_too_small")) {
            return fail("empty sidecar file did not fail closed");
        }

        auto truncated = built.bytes;
        truncated.pop_back();
        const auto truncated_path = temporary.path() / "truncated.mabf";
        if (!write_bytes(truncated_path, truncated)) return fail("could not write truncated fixture");
        if (!rejects_with_empty_bytes(truncated_path, "structural_validation_failed")) {
            return fail("truncated sidecar did not fail structural validation");
        }

        const auto oversized_path = temporary.path() / "oversized.mabf";
        {
            std::ofstream oversized(oversized_path, std::ios::binary | std::ios::trunc);
            oversized.seekp(static_cast<std::streamoff>(ff7rp::pipeline::kMaxMabfBytes));
            oversized.put('\0');
            if (!oversized) return fail("could not write oversized fixture");
        }
        if (!rejects_with_empty_bytes(oversized_path, "size_too_large")) {
            return fail("oversized sidecar did not fail closed");
        }

        const std::size_t hca_offset = ff7rp::pipeline::kMabfSlot0Offset;
        const std::size_t frame_offset = hca_offset + ff7rp::pipeline::kMabfHcaHeaderSize;
        const std::size_t trailer_offset = hca_offset + hca.size();
        struct MutationCase {
            const char* name;
            std::size_t offset;
        };
        const MutationCase mutations[] = {
            {"scaffold", 0x100},
            {"hca_fmt", hca_offset + 8},
            {"hca_metadata", hca_offset + 12},
            {"frame_crc", frame_offset + 2},
            {"trailer", trailer_offset},
        };
        for (const MutationCase& mutation : mutations) {
            auto malformed = built.bytes;
            malformed[mutation.offset] ^= 0x01u;
            const auto path = temporary.path() / (std::string(mutation.name) + ".mabf");
            if (!write_bytes(path, malformed)) {
                return fail(std::string("could not write ") + mutation.name + " fixture");
            }
            if (!rejects_with_empty_bytes(path, "structural_validation_failed")) {
                return fail(std::string(mutation.name) + " mutation did not fail closed");
            }
        }

        constexpr std::size_t kSlot1RelativeOffsetField = 0x424;
        constexpr std::size_t kSlot2RelativeOffsetField = 0x428;
        constexpr std::size_t kRelativeOffsetBase = 0x440;
        constexpr std::size_t kMinimumModeSize =
            ff7rp::pipeline::kMabfHcaHeaderSize + ff7rp::pipeline::kMabfTrailerMetadataSize;
        const std::uint32_t slot1_relative = read_u32_le(built.bytes, kSlot1RelativeOffsetField);
        const std::uint32_t slot2_relative = read_u32_le(built.bytes, kSlot2RelativeOffsetField);
        const std::uint32_t file_end_relative = static_cast<std::uint32_t>(
            built.bytes.size() - kRelativeOffsetBase);
        struct OffsetMutationCase {
            const char* name;
            std::size_t field;
            std::uint32_t value;
        };
        const OffsetMutationCase offset_mutations[] = {
            {"offset_424_oversized_mode0", kSlot1RelativeOffsetField,
                slot2_relative - static_cast<std::uint32_t>(kMinimumModeSize - 1u)},
            {"offset_428_oversized_mode1", kSlot2RelativeOffsetField,
                file_end_relative - static_cast<std::uint32_t>(kMinimumModeSize - 1u)},
            {"offset_424_reversed", kSlot1RelativeOffsetField, 0x1fu},
            {"offset_428_overlapping", kSlot2RelativeOffsetField, slot1_relative - 1u},
            {"offset_424_overflowed", kSlot1RelativeOffsetField,
                std::numeric_limits<std::uint32_t>::max()},
            {"offset_428_overflowed", kSlot2RelativeOffsetField,
                std::numeric_limits<std::uint32_t>::max()},
            {"offset_424_out_of_file", kSlot1RelativeOffsetField, file_end_relative + 1u},
            {"offset_428_out_of_file", kSlot2RelativeOffsetField, file_end_relative + 1u},
        };
        for (const OffsetMutationCase& mutation : offset_mutations) {
            auto malformed = built.bytes;
            write_u32_le(malformed, mutation.field, mutation.value);
            const auto path = temporary.path() / (std::string(mutation.name) + ".mabf");
            if (!write_bytes(path, malformed)) {
                return fail(std::string("could not write ") + mutation.name + " fixture");
            }
            if (!rejects_with_empty_bytes(path, "structural_validation_failed")) {
                return fail(std::string(mutation.name) + " mutation crossed the sidecar boundary");
            }
        }
    } catch (const std::exception& ex) {
        return fail(std::string("mabf_sidecar_loader_selftest failed: ") + ex.what());
    }

    std::cout << "mabf_sidecar_loader_selftest ok\n";
    return 0;
}
