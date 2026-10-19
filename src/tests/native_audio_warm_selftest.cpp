#include "pipeline/native_audio_cache.h"
#include "pipeline/native_asset_hash.h"
#include "pipeline/mabf_builder.h"
#include "pipeline/hca/hca_encoder.h"

#include <fstream>
#include <iostream>

using namespace ff7rp::pipeline;
using Bytes = native_binary::Bytes;
namespace {
int fail(const std::string& message) { std::cerr << message << '\n'; return 1; }
Bytes read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}
bool write(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close(); return bool(out);
}
bool audio(NativeAudioCacheRequest& request, std::uint32_t frames) {
    HcaEncodeConfig config; config.target_samples = frames;
    auto hca = encode_hca_48k_stereo_256k(std::vector<std::int16_t>(frames * 2, 0), config);
    auto bank = build_mabf_from_mode_hca({&hca, &hca, &hca});
    if (!bank.status.ok()) return false;
    request.mabf = std::move(bank.bytes);
    request.timing = {frames, 48000, double(frames) / 48000};
    return true;
}
}
int main(int argc, char* argv[]) {
    NativeAudioCacheSession session;
    NativeAudioCacheRequest request;
    OodleDecoder decoder;
    NativeAudioArtifactSet output; output.identity = "must-clear";
    if (prepare_native_audio_cache(request, decoder, output, &session).ok() || !output.identity.empty())
        return fail("Invalid session request published");
    if (argc == 1) {
        std::cout << "Session invalid-input check passed; external warm/cold checks NOT RUN\n";
        return 0;
    }
    if (argc != 4) return fail("Usage: native_audio_warm_selftest absolute-paks absolute-companion fresh-absolute-scratch-root");
    request.source_paks = argv[1];
    request.cache_root = std::filesystem::path(argv[3]) / "native_audio_warm_selftest";
    if (!request.cache_root.is_absolute() || std::filesystem::exists(request.cache_root))
        return fail("Test requires a fresh absolute scratch directory");
    request.build_id = "ff7rebirth-steam-win64-6a16ced2";
    request.identity = "warm-session-regression"; request.base_score_name = "Journey";
    auto status = OodleDecoder::load(argv[2], &decoder);
    if (!status.ok()) return fail(status.message);
    if (!audio(request, 48001)) return fail("Audio fixture encoding failed");
    NativeAudioArtifactSet baseline, cold, warm;
    // No session and no manifest: also covers already-created legacy directories.
    status = prepare_native_audio_cache(request, decoder, baseline);
    if (!status.ok()) return fail(status.message);
    status = prepare_native_audio_cache(request, decoder, cold, &session);
    if (!status.ok()) return fail(status.message);
    std::array<Bytes, 3> original;
    std::array<std::filesystem::file_time_type, 3> times;
    for (std::size_t i = 0; i < 3; ++i) {
        original[i] = read(baseline.files[i].path);
        times[i] = std::filesystem::last_write_time(baseline.files[i].path);
    }
    // Installed donors are compressed: this unavailable decoder cannot extract
    // them. Only the session's previously authenticated immutable bytes can work.
    OodleDecoder unavailable;
    status = prepare_native_audio_cache(request, unavailable, warm, &session);
    if (!status.ok() || warm.container_prefix != baseline.container_prefix || cold.container_id != warm.container_id)
        return fail("Session did not reuse authenticated templates: " + status.message);
    for (std::size_t i = 0; i < 3; ++i)
        if (read(warm.files[i].path) != original[i] || warm.files[i].content_hash != baseline.files[i].content_hash ||
            std::filesystem::last_write_time(warm.files[i].path) != times[i])
            return fail("Warm/cold bytes, hashes or immutable write times differ");
    NativeAudioCacheSession fresh;
    if (prepare_native_audio_cache(request, unavailable, output, &fresh).ok())
        return fail("Fixture did not require decoding; snapshot-reuse evidence is invalid");
    auto invalid_first = request; invalid_first.timing.logical_source_frames++;
    if (prepare_native_audio_cache(invalid_first, decoder, output, &fresh).ok() ||
        prepare_native_audio_cache(request, unavailable, output, &fresh).ok())
        return fail("Failed preparation retained unqualified templates");
    auto changed = request; changed.source_paks /= "absent";
    if (prepare_native_audio_cache(changed, unavailable, output, &session).ok()) return fail("Source path reused a foreign snapshot");
    changed = request; changed.build_id = "ff7rebirth-steam-win64-68fd6fde";
    if (prepare_native_audio_cache(changed, unavailable, output, &session).ok()) return fail("Build reused a foreign snapshot");
    changed = request; changed.timing.logical_source_frames++;
    if (prepare_native_audio_cache(changed, unavailable, output, &session).ok() || !output.identity.empty())
        return fail("Changed timing bypassed validation");
    changed = request; changed.base_score_name = "TifasTheme";
    if (!prepare_native_audio_cache(changed, unavailable, output, &session).ok() || output.container_prefix == warm.container_prefix)
        return fail("Changed carrier did not invalidate identity");
    changed = request; changed.base_score_name = "NoSuchStockRow";
    if (prepare_native_audio_cache(changed, unavailable, output, &session).ok()) return fail("Invalid carrier accepted");
    changed = request;
    if (!audio(changed, 96001) || !prepare_native_audio_cache(changed, unavailable, output, &session).ok() ||
        output.container_prefix == warm.container_prefix) return fail("Changed audio/timing did not invalidate identity");
    // Compare all bytes, not only TOC hashes, size, endpoints, or cache metadata.
    for (std::size_t i = 0; i < 3; ++i) {
        for (auto at : {std::size_t(0), original[i].size() / 2, original[i].size() - 1}) {
            auto corrupt = original[i]; corrupt[at] ^= 1;
            if (!write(warm.files[i].path, corrupt)) return fail("Corruption fixture write failed");
            if (prepare_native_audio_cache(request, unavailable, output, &session).ok() || !output.identity.empty() ||
                read(warm.files[i].path) != corrupt) return fail("Corrupt warm output accepted or overwritten");
        }
        auto truncated = original[i]; truncated.pop_back();
        if (!write(warm.files[i].path, truncated) || prepare_native_audio_cache(request, unavailable, output, &session).ok())
            return fail("Truncated warm output accepted");
        auto extended = original[i]; extended.push_back(0);
        if (!write(warm.files[i].path, extended) || prepare_native_audio_cache(request, unavailable, output, &session).ok())
            return fail("Extended warm output accepted");
        if (!write(warm.files[i].path, original[i])) return fail("Test-only output restoration failed");
    }
    if (!validate_native_audio_artifacts(request, decoder, warm).ok()) return fail("Restored output failed independent validation");
    auto forged = warm; forged.files[2].content_hash ^= 1;
    if (validate_native_audio_artifacts(request, decoder, forged).ok()) return fail("Forged compact hash accepted");
    changed = request; changed.build_id = "ff7rebirth-steam-win64-68fd6fde";
    NativeAudioArtifactSet older_cold, older_warm;
    if (!prepare_native_audio_cache(changed, decoder, older_cold, &session).ok() ||
        !prepare_native_audio_cache(changed, unavailable, older_warm, &session).ok() ||
        older_warm.container_prefix != older_cold.container_prefix || older_warm.container_prefix == warm.container_prefix ||
        !validate_native_audio_artifacts(changed, decoder, older_warm).ok())
        return fail("Changed-build snapshot replacement or older-profile parity failed");
    std::cout << "native_audio_warm_selftest passed (external warm/cold, template reuse, invalidation, corruption)\n";
    return 0;
}
