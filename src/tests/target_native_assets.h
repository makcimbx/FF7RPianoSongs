#pragma once

// Test-only composition fixture: exercise the same resolved catalog policy as
// the ASI and the offline tool without importing generated hook addresses.
#include "pipeline/chart_compiler.h"
#include "pipeline/midi_chart_generator.h"
#include "pipeline/resolved_song_renderer.h"
#include "pipeline/runtime_cache_codec.h"
#include "pipeline/song_json.h"
#include "pipeline/song_repository.h"
#include "song_descriptor_builder.h"

namespace ff7rp::pipeline {

inline NativeAssetCapabilities test_target_native_assets() {
    return native_asset_capabilities_for_catalog(FF7RP_TARGET_BUILD_ID);
}

inline Status compile_chart(const SongConfig& config, CompiledChart* out,
    DiagnosticChartRetention* diagnostic = nullptr, std::size_t limit = 0) {
    return compile_chart(config, test_target_native_assets(), out, diagnostic, limit);
}

inline Status compile_chart(const SongConfig& config, CompiledChart* out,
    DiagnosticChartRetention* diagnostic, std::size_t limit, NativeAssetCapabilities assets) {
    return compile_chart(config, assets, out, diagnostic, limit);
}

inline Status parse_song_json_string(const std::string& json, SongConfig* out) {
    return parse_song_json_string(json, test_target_native_assets(), out);
}
inline Status parse_song_json_string(const std::string& json, ParsedSongSource* out) {
    return parse_song_json_string(json, test_target_native_assets(), out);
}
inline Status load_song_json_file(const std::string& path, SongConfig* out) {
    return load_song_json_file(path, test_target_native_assets(), out);
}
inline Status load_song_json_file(const std::string& path, ParsedSongSource* out) {
    return load_song_json_file(path, test_target_native_assets(), out);
}

inline Status generate_notes_from_midi(const std::string& path, const WavAudio& audio,
    const SongConfig& config, std::vector<Note>* out, MidiChartStats* stats = nullptr,
    const std::vector<Note>* baseline = nullptr, std::size_t maximum_rows = 0,
    NativeAssetCapabilities assets = test_target_native_assets()) {
    return generate_notes_from_midi(path, audio, config, assets, out, stats, baseline, maximum_rows);
}
inline Status generate_notes_from_normalized_midi(const NormalizedMidiSource& midi,
    const WavAudio& audio, const SongConfig& config, std::vector<Note>* out,
    MidiChartStats* stats = nullptr, const std::vector<Note>* baseline = nullptr,
    std::size_t maximum_rows = 0, NativeAssetCapabilities assets = test_target_native_assets()) {
    return generate_notes_from_normalized_midi(midi, audio, config, assets, out, stats, baseline, maximum_rows);
}
inline std::string infer_native_chord_from_fresh_midi_pitches(const std::vector<int>& pitches) {
    return infer_native_chord_from_fresh_midi_pitches(pitches, test_target_native_assets());
}

inline bool encode_runtime_cache(const LoadedSong& song, std::span<const char, 8> magic,
    std::uint32_t format, std::vector<std::uint8_t>* bytes) {
    return encode_runtime_cache(song, test_target_native_assets(), magic, format, bytes);
}
inline bool decode_runtime_cache(std::span<const std::uint8_t> bytes,
    std::span<const char, 8> magic, std::uint32_t format, LoadedSong* song) {
    return decode_runtime_cache(bytes, test_target_native_assets(), magic, format, song);
}
inline Status render_resolved_song_json(const LoadedSong& song, bool profiles, std::string* out) {
    return render_resolved_song_json(song, test_target_native_assets(), profiles, out);
}

inline Status load_song_directory(const std::string& path, LoadedSong* song,
    SongLoadTrace trace = {}, bool rebuild_invalid_cache = true, SongLoadProgress progress = {}) {
    return load_song_directory(path, test_target_native_assets(), song, trace,
        rebuild_invalid_cache, progress);
}
inline SongRepositoryResult discover_songs(const std::string& root,
    const SongDiscoveryHooks& hooks) {
    return discover_songs(root, test_target_native_assets(), hooks);
}
inline SongRepositoryResult discover_songs(const std::filesystem::path& root,
    const SongDiscoveryHooks& hooks) {
    return discover_songs(root, test_target_native_assets(), hooks);
}
inline SongRepositoryResult discover_songs(const std::filesystem::path& root) {
    return discover_songs(root, test_target_native_assets());
}
inline SongRepositoryResult discover_songs(const std::string& root) {
    return discover_songs(root, test_target_native_assets());
}
inline SongRepositoryResult discover_songs(const char* root) {
    return discover_songs(root, test_target_native_assets());
}
inline SongRepositoryResult discover_songs(const char* root, const SongDiscoveryHooks& hooks) {
    return discover_songs(root, test_target_native_assets(), hooks);
}

} // namespace ff7rp::pipeline

namespace ff7r::piano {
inline game::SongDescriptor build_song_descriptor(const ff7rp::pipeline::LoadedSong& song,
    int visible_index) {
    return build_song_descriptor(song, visible_index, ff7rp::pipeline::test_target_native_assets());
}
} // namespace ff7r::piano
