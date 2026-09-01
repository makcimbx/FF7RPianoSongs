#include "core/generated/build_identity.generated.h"
#include "pipeline/chart_compiler.h"
#include "pipeline/native_chord_constituents.h"
#include "pipeline/cache.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/song_json.h"
#include "tests/documentation_parity.h"
#include "tests/test_support.h"
#include "tools/song_cache_tool_args.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

std::string input_pitch(char note, char accidental, int octave)
{
    std::string pitch(1, note);
    if (accidental != '\0') {
        pitch += accidental;
    }
    pitch += std::to_string(octave);
    return pitch;
}

std::string expected_monotone(int semitone)
{
    static constexpr std::array<const char*, 12> ids{
        "Cn", "Cs", "Dn", "Ds", "En", "Fn", "Fs", "Gn", "Gs", "An", "As", "Bn"
    };
    return std::string(ids[semitone % 12]) + std::to_string(semitone / 12);
}

bool test_staged_documentation_failures(std::string* error_message)
{
    namespace fs = std::filesystem;
    const fs::path source_root = FF7RPIANOSONGS_SOURCE_DIR;
    ff7rp::tests::TemporaryDirectory temporary("ff7rp-documentation-parity");
    const fs::path package_root = temporary.path();
    ff7rp::tests::DocumentationRegistry registry;
    if (!ff7rp::tests::load_documentation_registry(source_root, &registry, error_message)) return false;
    for (const auto& document : ff7rp::tests::package_documents(registry)) {
        const fs::path staged = package_root / document.destination;
        fs::create_directories(staged.parent_path());
        fs::copy_file(source_root / document.source, staged, fs::copy_options::overwrite_existing);
    }
    std::string parity_error;
    if (!ff7rp::tests::verify_staged_documentation_parity(
            source_root, package_root, &parity_error)) {
        *error_message = "valid staged documentation was rejected: " + parity_error;
        return false;
    }

    {
        std::ofstream stale(package_root / "README.md", std::ios::binary | std::ios::app);
        stale << "\nmutated\n";
    }
    if (ff7rp::tests::verify_staged_documentation_parity(source_root, package_root, &parity_error)) {
        *error_message = "mutated entrypoint documentation was accepted";
        return false;
    }
    fs::copy_file(source_root / "README.md", package_root / "README.md",
        fs::copy_options::overwrite_existing);

    fs::remove(package_root / "docs/SongFormat.md");
    if (ff7rp::tests::verify_staged_documentation_parity(source_root, package_root, &parity_error)) {
        *error_message = "missing staged documentation was accepted";
        return false;
    }
    fs::copy_file(source_root / "docs/SongFormat.md", package_root / "docs/SongFormat.md");

    {
        std::ofstream mutated(package_root / "docs/SongFormat.md", std::ios::binary | std::ios::app);
        mutated << "\nmutated\n";
    }
    if (ff7rp::tests::verify_staged_documentation_parity(source_root, package_root, &parity_error)) {
        *error_message = "mutated staged documentation was accepted";
        return false;
    }
    if (!temporary.cleanup(&parity_error)) {
        *error_message = "documentation fixture cleanup failed: " + parity_error;
        return false;
    }
    return true;
}

bool replace_once(std::string* text, const std::string& from, const std::string& to)
{
    const std::size_t position = text->find(from);
    if (position == std::string::npos) return false;
    text->replace(position, from.size(), to);
    return true;
}

bool test_documentation_registry_parser(std::string* error_message)
{
    std::ifstream input(
        std::filesystem::path(FF7RPIANOSONGS_SOURCE_DIR) / "package-docs.json", std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string canonical = buffer.str();
    if (!input.good() && !input.eof()) {
        *error_message = "could not read package-docs.json parser fixture";
        return false;
    }

    const auto expect_valid = [&](std::string json, const char* description) {
        ff7rp::tests::DocumentationRegistry registry;
        std::string parse_error;
        if (!ff7rp::tests::parse_documentation_registry(json, &registry, &parse_error)) {
            *error_message = std::string(description) + " was rejected: " + parse_error;
            return false;
        }
        return true;
    };
    const auto expect_invalid = [&](std::string json, const char* description) {
        ff7rp::tests::DocumentationRegistry registry;
        std::string parse_error;
        if (ff7rp::tests::parse_documentation_registry(json, &registry, &parse_error)) {
            *error_message = std::string(description) + " was accepted";
            return false;
        }
        return true;
    };

    std::string reordered = canonical;
    if (!replace_once(&reordered,
            R"json({ "role": "entrypoint", "distribution": "package", "source": "README.md", "destination": "README.md" })json",
            R"json({ "destination": "README.md", "source": "README.md", "distribution": "package", "role": "entrypoint" })json")
        || !expect_valid(reordered, "reordered registry keys")) {
        return false;
    }

    std::vector<std::pair<std::string, std::string>> invalid;
    std::string unknown = canonical;
    replace_once(&unknown, "\"documents\": [", "\"unknown\": \"value\", \"documents\": [");
    invalid.emplace_back(std::move(unknown), "unknown registry key");
    std::string duplicate_role = canonical;
    replace_once(&duplicate_role, "\"role\": \"docs-index\"", "\"role\": \"entrypoint\"");
    invalid.emplace_back(std::move(duplicate_role), "duplicate documentation role");
    std::string traversal = canonical;
    replace_once(&traversal, "\"source\": \"README.md\"", "\"source\": \"../README.md\"");
    invalid.emplace_back(std::move(traversal), "source traversal");
    std::string distribution = canonical;
    replace_once(&distribution,
        "\"role\": \"entrypoint\", \"distribution\": \"package\"",
        "\"role\": \"entrypoint\", \"distribution\": \"repository\"");
    invalid.emplace_back(std::move(distribution), "invalid role distribution");
    std::string missing_role = canonical;
    const std::string required_line =
        "    { \"role\": \"first-party-license\", \"distribution\": \"package\", \"source\": \"LICENSE\", \"destination\": \"LICENSE\" },\n";
    replace_once(&missing_role, required_line, "");
    invalid.emplace_back(std::move(missing_role), "missing mandatory package role");
    std::string duplicate_field = canonical;
    replace_once(&duplicate_field, "{ \"role\": \"entrypoint\"",
        "{ \"role\": \"entrypoint\", \"role\": \"entrypoint\"");
    invalid.emplace_back(std::move(duplicate_field), "duplicate document field");
    for (const auto& fixture : invalid) {
        if (!expect_invalid(fixture.first, fixture.second.c_str())) return false;
    }
    return true;
}

bool test_release_authority_parser(std::string* error_message)
{
    std::ifstream input(
        std::filesystem::path(FF7RPIANOSONGS_SOURCE_DIR) / "release.json", std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string canonical = buffer.str();
    ff7rp::tests::ReleaseAuthority release;
    if (!ff7rp::tests::parse_release_authority(canonical, &release, error_message)) return false;
    if (release.version != "0.1.3") {
        *error_message = "release authority did not expose the canonical public version";
        return false;
    }
    const std::string build_id{ff7r::piano::core::generated::kBuildId};
    const ff7rp::tests::ReleaseTarget* target = ff7rp::tests::find_release_target(release, build_id);
    if (!target) {
        *error_message = "release.json declares no target for build identity " + build_id;
        return false;
    }
    {
        // The archive basename is derived from the target's game build, not free text.
        std::string fixture = canonical;
        if (!replace_once(&fixture, '"' + target->archive_basename + '"',
                '"' + release.product + '-' + release.version + '-' + release.platform + '"')) {
            *error_message = "could not construct malformed release target fixture";
            return false;
        }
        ff7rp::tests::ReleaseAuthority rejected;
        std::string parse_error;
        if (ff7rp::tests::parse_release_authority(fixture, &rejected, &parse_error)) {
            *error_message = "release archive basename was accepted without its game build";
            return false;
        }
    }
    for (const std::string& bad : {"39", "v39", "01.0.0", "0.1", "0.1.0-beta"}) {
        std::string fixture = canonical;
        if (!replace_once(&fixture, "\"version\": \"0.1.3\"", "\"version\": \"" + bad + "\"")) {
            *error_message = "could not construct malformed release fixture";
            return false;
        }
        ff7rp::tests::ReleaseAuthority rejected;
        std::string parse_error;
        if (ff7rp::tests::parse_release_authority(fixture, &rejected, &parse_error)) {
            *error_message = "malformed public release version was accepted: " + bad;
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    const char* json = R"json({
        "schema": "ff7rpianosongs.song.v2",
        "title": "Self Test Song",
        "bpm": 120,
        "difficulty": 1,
        "score_thresholds": [0, 100, 200, 300],
        "mode_change_combo_counts": [4, 8],
        "notes": [
            { "beat": 0.0, "duration_beats": 1.0, "pitch": "C4", "chord_id": "pca_C" },
            { "beat": 1.0, "duration_beats": 2.0, "pitch": "B5" },
            { "beat": 2.0, "duration_beats": 1.0, "chord_id": "pca_D_m" }
        ]
    })json";

    ff7rp::pipeline::SongConfig config;
    auto status = ff7rp::pipeline::parse_song_json_string(json, &config);
    if (!status.ok()) {
        return fail("parse failed: " + status.message);
    }
    if (!config.notes_provided) {
        return fail("explicit notes were not marked as provided");
    }
    if (!config.bpm_provided || !config.score_thresholds_provided || !config.mode_change_combo_counts_provided) {
        return fail("explicit gameplay metadata was not marked as provided");
    }
    const char* profiles_json = R"json({
        "schema": "ff7rpianosongs.song.v2",
        "title": "Authored Profiles",
        "bpm": 120,
        "profiles": [
            { "difficulty": 0, "notes": [
                { "beat": 0, "duration_beats": 1, "pitch": "C4" }
            ] },
            { "difficulty": 3, "notes": [
                { "beat": 0, "duration_beats": 1, "pitch": "C4" },
                { "beat": 1, "duration_beats": 1, "chord_id": "pca_C" }
            ] }
        ]
    })json";
    ff7rp::pipeline::ParsedSongSource profiled_source;
    status = ff7rp::pipeline::parse_song_json_string(profiles_json, &profiled_source);
    if (!status.ok() || profiled_source.authored_profiles.size() != 2u ||
        profiled_source.authored_profiles[0].difficulty != 0 ||
        profiled_source.authored_profiles[1].difficulty != 3 ||
        profiled_source.config.difficulty != 0 || profiled_source.config.notes.size() != 1u ||
        !profiled_source.config.notes_provided || !profiled_source.config.bpm_provided) {
        return fail("authored profiles did not parse into an ordered source container");
    }
    ff7rp::pipeline::SongConfig compatible_profile_config;
    status = ff7rp::pipeline::parse_song_json_string(profiles_json, &compatible_profile_config);
    if (!status.ok() || compatible_profile_config.difficulty != 0 ||
        compatible_profile_config.notes.size() != 1u) {
        return fail("SongConfig parser compatibility did not project the first authored profile");
    }
    for (const char* invalid_profiles : {
            R"json({"schema":"v2","title":"bad","bpm":120,"profiles":[]})json",
            R"json({"schema":"v2","title":"bad","profiles":[{"difficulty":0,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"difficulty":0,"profiles":[{"difficulty":1,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}],"profiles":[{"difficulty":1,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"profiles":[{"difficulty":0}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"profiles":[{"difficulty":2,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]},{"difficulty":2,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"profiles":[{"difficulty":2,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]},{"difficulty":1,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"profiles":[{"difficulty":0,"notes":[],"title":"nested"}]})json"}) {
        ff7rp::pipeline::ParsedSongSource rejected;
        if (ff7rp::pipeline::parse_song_json_string(invalid_profiles, &rejected).ok()) {
            return fail("invalid authored profiles were accepted: " + std::string(invalid_profiles));
        }
    }
    std::ostringstream too_many_profiles;
    too_many_profiles << R"json({"schema":"v2","title":"bad","bpm":120,"profiles":[)json";
    for (std::size_t index = 0; index <= ff7rp::pipeline::kMaximumDifficultyProfiles; ++index) {
        if (index) too_many_profiles << ',';
        too_many_profiles << "{\"difficulty\":" << index
            << R"json(,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]})json";
    }
    too_many_profiles << "]}";
    if (ff7rp::pipeline::parse_song_json_string(too_many_profiles.str(), &profiled_source).ok()) {
        return fail("authored profile capacity was not enforced");
    }
    for (const char* invalid_json : {
            R"json({"schema":"v2","title":"one","title":"two"})json",
            R"json({"schema":"v2","title":"bad","difficulty":2147483648})json",
            R"json({"schema":"v2","title":"bad","bpm":01})json",
            R"json({"schema":"v2","title":"bad","bpm":1.})json",
            R"json({"schema":"v2","title":"bad","bpm":.1})json",
            R"json({"schema":"v2","title":"bad","bpm":1e})json",
            R"json({"schema":"v2","title":"\uD834"})json",
            R"json({"schema":"v2","title":"\uDD1E"})json"}) {
        ff7rp::pipeline::SongConfig rejected;
        if (ff7rp::pipeline::parse_song_json_string(invalid_json, &rejected).ok()) {
            return fail("non-authoritative JSON input was accepted: " + std::string(invalid_json));
        }
    }
    ff7rp::pipeline::SongConfig unicode_config;
    status = ff7rp::pipeline::parse_song_json_string(
        R"json({"schema":"v2","title":"A\u00DF\uD834\uDD1E"})json", &unicode_config);
    if (!status.ok() || unicode_config.title != "A\xC3\x9F\xF0\x9D\x84\x9E") {
        return fail("valid Unicode escapes were not decoded exactly");
    }

    ff7rp::pipeline::CompiledChart chart;
    status = ff7rp::pipeline::compile_chart(config, &chart);
    if (!status.ok()) {
        return fail("compile failed: " + status.message);
    }
    if (chart.notes.size() != 3) {
        return fail("unexpected note count");
    }
    if (chart.notes[0].monotone_id != "Cn4" || chart.notes[1].monotone_id != "Bn5" || !chart.notes[2].monotone_id.empty()) {
        return fail("unexpected pitch mapping");
    }
    if (chart.notes[0].chord_id != "pca_C" || !chart.notes[1].chord_id.empty() || chart.notes[2].chord_id != "pca_D_m") {
        return fail("unexpected chord mapping");
    }
    if (chart.notes[0].camera_switch_timing != 0 || chart.notes[1].camera_switch_timing != 0 || chart.notes[2].camera_switch_timing != 0) {
        return fail("custom chart unexpectedly requests per-note camera switches");
    }
    for (std::size_t index = 0; index < config.notes.size(); ++index) {
        if (config.notes[index].group_index != 0 || config.notes[index].alternate_monotone ||
            !config.notes[index].ignore_sound_pitches.empty() ||
            !config.notes[index].source_chord_pitches.empty() || chart.notes[index].group_index != 0 ||
            !chart.notes[index].ignore_sound_ids[0].empty() || !chart.notes[index].ignore_sound_ids[1].empty() ||
            !chart.notes[index].ignore_sound_ids[2].empty()) {
            return fail("omitted note extensions changed schema-v2 chart behavior");
        }
    }
    if (chart.notes[0].note_type != 3 || chart.notes[1].note_type != 2) {
        return fail("unexpected note type mapping");
    }
    std::size_t three_sound_chords = 0;
    std::size_t four_sound_chords = 0;
    std::set<std::string_view> verified_chord_ids;
    for (const auto& chord : ff7rp::pipeline::kVerifiedNativeChordConstituents) {
        if (!verified_chord_ids.insert(chord.chord_id).second) return fail("verified chord table contains duplicate IDs");
        if (chord.sound_count == 3u) ++three_sound_chords;
        else if (chord.sound_count == 4u) ++four_sound_chords;
        else return fail("verified chord table contains an unsupported constituent count");
        std::set<std::string_view> sounds;
        for (std::size_t sound = 0; sound < chord.sound_count; ++sound) {
            if (chord.sound_names[sound].empty() || !sounds.insert(chord.sound_names[sound]).second) {
                return fail("verified chord table contains empty or duplicate constituents");
            }
        }
    }
    if (verified_chord_ids.size() != 63u || three_sound_chords != 40u || four_sound_chords != 23u) {
        return fail("verified chord table coverage changed");
    }
    const char* extended_notes_json = R"json({
        "schema":"ff7rpianosongs.song.v2","title":"Extended notes","bpm":120,"notes":[
            {"beat":0,"duration_beats":0.25,"pitch":"C4","group_index":17,"monotone_variant":"alternate"},
            {"beat":0.05,"duration_beats":0.25,"pitch":"C#4","group_index":17}
        ]})json";
    ff7rp::pipeline::SongConfig extended_config;
    ff7rp::pipeline::CompiledChart extended_chart;
    status = ff7rp::pipeline::parse_song_json_string(extended_notes_json, &extended_config);
    if (!status.ok() || !ff7rp::pipeline::compile_chart(extended_config, &extended_chart).ok() ||
        extended_chart.notes.size() != 2u || extended_chart.notes[0].monotone_id != "Cn4_2" ||
        extended_chart.notes[1].monotone_id != "Cs4" || extended_chart.notes[0].group_index != 17 ||
        extended_chart.notes[1].group_index != 17) {
        return fail("extended note semantics did not parse and compile exactly");
    }
    const char* ignore_sound_json = R"json({
        "schema":"v2","title":"Exact ignores","bpm":120,"notes":[
            {"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["En2"]},
            {"beat":1,"duration_beats":1,"chord_id":"pca_C_7","ignore_sound":["Cn2","En2","Gn2"]}
        ]})json";
    ff7rp::pipeline::SongConfig ignore_config;
    ff7rp::pipeline::CompiledChart ignore_chart;
    status = ff7rp::pipeline::parse_song_json_string(ignore_sound_json, &ignore_config);
    if (!status.ok() || !ff7rp::pipeline::compile_chart(ignore_config, &ignore_chart).ok() ||
        ignore_chart.notes[0].ignore_sound_ids != std::array<std::string, 3>{"En2", "", ""} ||
        ignore_chart.notes[1].ignore_sound_ids != std::array<std::string, 3>{"Cn2", "En2", "Gn2"}) {
        return fail("exact verified IgnoreSound members did not compile");
    }
    for (const char* invalid_note_semantics : {
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"pitch":"D4","monotone_variant":"alternate"}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4","group_index":256}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4","group_index":1}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["Fn2"]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["Fb2"]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["En3"]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C_9","ignore_sound":["As2"]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C_7","ignore_sound":["Cn2","En2","Gn2","As2"]}]})json",
            R"json({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["C4","C4"]}]})json"}) {
        ff7rp::pipeline::SongConfig rejected;
        ff7rp::pipeline::CompiledChart rejected_chart;
        status = ff7rp::pipeline::parse_song_json_string(invalid_note_semantics, &rejected);
        if (status.ok() && ff7rp::pipeline::compile_chart(rejected, &rejected_chart).ok()) {
            return fail("malformed or unsupported note semantics were accepted: " +
                std::string(invalid_note_semantics));
        }
    }
    if (ff7rp::pipeline::beat_to_time_str(154.84, 60.0) != "154_50"
        || ff7rp::pipeline::beat_to_time_str(0.999, 60.0) != "01_00") {
        return fail("TimeStr did not use native 60-frame encoding");
    }

    const char* midi_json = R"json({
        "schema": "ff7rpianosongs.song.v2",
        "title": "MIDI Self Test",
        "difficulty": 3,
        "midi_audio_offset_seconds": 0.035,
        "midi_audio_alignment_seconds": 0.031,
        "midi_minimum_lead_in_seconds": 2.5,
        "loudness_normalization": true,
        "loudness_target_lufs": -14.5,
        "loudness_peak_ceiling_dbfs": -1.5,
        "gain_envelope": [
            { "time_seconds": 0, "gain_db": 5 },
            { "time_seconds": 45, "gain_db": 5 },
            { "time_seconds": 60, "gain_db": 0 }
        ],
        "metronome": { "enabled": true, "level": 0.2 }
    })json";
    ff7rp::pipeline::SongConfig midi_config;
    status = ff7rp::pipeline::parse_song_json_string(midi_json, &midi_config);
    if (!status.ok() || midi_config.notes_provided || !midi_config.notes.empty()) {
        return fail("missing notes did not select MIDI eligibility");
    }
    if (midi_config.bpm_provided || midi_config.bpm != 0.0 || midi_config.score_thresholds_provided ||
        midi_config.mode_change_combo_counts_provided) {
        return fail("omitted MIDI metadata unexpectedly selected JSON overrides");
    }
    if (std::fabs(midi_config.midi_audio_offset_seconds - 0.035) > 0.000001 ||
        std::fabs(midi_config.midi_audio_alignment_seconds - 0.031) > 0.000001 ||
        std::fabs(midi_config.midi_minimum_lead_in_seconds - 2.5) > 0.000001 ||
        !midi_config.midi_audio_offset_provided || !midi_config.midi_audio_alignment_provided) {
        return fail("MIDI timing metadata was not preserved");
    }
    if (!midi_config.loudness_normalization || std::fabs(midi_config.loudness_target_lufs - (-14.5)) > 0.000001 ||
        std::fabs(midi_config.loudness_peak_ceiling_dbfs - (-1.5)) > 0.000001) {
        return fail("loudness normalization overrides were not preserved");
    }
    if (midi_config.gain_envelope.size() != 3 ||
        midi_config.gain_envelope[0].time_seconds != 0.0 || midi_config.gain_envelope[0].gain_db != 5.0 ||
        midi_config.gain_envelope[1].time_seconds != 45.0 || midi_config.gain_envelope[1].gain_db != 5.0 ||
        midi_config.gain_envelope[2].time_seconds != 60.0 || midi_config.gain_envelope[2].gain_db != 0.0) {
        return fail("gain envelope was not parsed exactly");
    }
    if (!midi_config.metronome_enabled || std::fabs(midi_config.metronome_level - 0.2) > 0.000001 ||
        midi_config.metronome_beat_zero_offset_provided) {
        return fail("metronome object was not preserved");
    }
    const char* default_metronome_json = R"json({
        "schema": "ff7rpianosongs.song.v2", "title": "Defaults"
    })json";
    status = ff7rp::pipeline::parse_song_json_string(default_metronome_json, &midi_config);
    if (!status.ok() || midi_config.metronome_enabled ||
        std::fabs(midi_config.metronome_level - 0.12) > 0.000001 || !midi_config.gain_envelope.empty()) {
        return fail("omitted metronome did not preserve the explicit-config disabled default");
    }
    for (const char* invalid_envelope : {
            R"json({"schema":"v2","title":"Bad","gain_envelope":{}})json",
            R"json({"schema":"v2","title":"Bad","gain_envelope":[]})json",
            R"json({"schema":"v2","title":"Bad","gain_envelope":[{"time_seconds":0,"gain_db":13}]})json",
            R"json({"schema":"v2","title":"Bad","gain_envelope":[{"time_seconds":-1,"gain_db":0}]})json",
            R"json({"schema":"v2","title":"Bad","gain_envelope":[{"time_seconds":1,"gain_db":0},{"time_seconds":1,"gain_db":1}]})json",
            R"json({"schema":"v2","title":"Bad","gain_envelope":[{"time_seconds":2,"gain_db":0},{"time_seconds":1,"gain_db":1}]})json",
            R"json({"schema":"v2","title":"Bad","gain_envelope":[{"time_seconds":0}]})json",
            R"json({"schema":"v2","title":"Bad","gain_envelope":[true]})json"}) {
        status = ff7rp::pipeline::parse_song_json_string(invalid_envelope, &midi_config);
        if (status.ok()) return fail("invalid gain envelope input did not fail closed");
    }
    for (const char* invalid_metronome : {
            R"json({"schema":"v2","title":"Bad","metronome":{"level":2}})json",
            R"json({"schema":"v2","title":"Bad","metronome":{"enabled":true,"level":0}})json",
            R"json({"schema":"v2","title":"Bad","metronome":{"level":"loud"}})json",
            R"json({"schema":"v2","title":"Bad","metronome":{"beat_zero_offset_seconds":31}})json",
            R"json({"schema":"v2","title":"Bad","metronome":true})json"}) {
        status = ff7rp::pipeline::parse_song_json_string(invalid_metronome, &midi_config);
        if (status.ok()) return fail("invalid metronome input did not fail closed");
    }
    const char* empty_notes_json = R"json({
        "schema": "ff7rpianosongs.song.v2", "title": "Invalid", "bpm": 120, "notes": []
    })json";
    status = ff7rp::pipeline::parse_song_json_string(empty_notes_json, &midi_config);
    if (status.ok()) {
        return fail("explicit empty notes unexpectedly fell back to MIDI");
    }

    std::ostringstream fixture_json;
    fixture_json << "{\"schema\":\"ff7rpianosongs.song.v2\",\"title\":\"Diagnostic 520\","
                 << "\"bpm\":120,\"difficulty\":2,\"diagnostic_extended_chart_fixture\":true,\"notes\":[";
    for (std::size_t row = 0; row < 520u; ++row) {
        if (row) fixture_json << ',';
        fixture_json << "{\"beat\":" << row * 0.25
                     << ",\"duration_beats\":0.125,\"pitch\":\"C4\"}";
    }
    fixture_json << "]}";
    ff7rp::pipeline::SongConfig fixture_config;
    status = ff7rp::pipeline::parse_song_json_string(fixture_json.str(), &fixture_config);
    if (!status.ok() || !fixture_config.diagnostic_extended_chart_fixture ||
        fixture_config.notes.size() != 520u) {
        return fail("exactly-520 diagnostic fixture did not parse authoritatively");
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    ff7rp::pipeline::DiagnosticChartRetention diagnostic;
    status = ff7rp::pipeline::compile_chart(fixture_config, &chart, &diagnostic);
    if (status.ok()) return fail("diagnostic fixture compiled while policy was disabled");
    ff7rp::pipeline::configure_chart_row_limit(true, false);
    status = ff7rp::pipeline::compile_chart(fixture_config, &chart, &diagnostic);
    if (status.ok()) return fail("diagnostic fixture compiled after helper mismatch");
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    status = ff7rp::pipeline::compile_chart(fixture_config, &chart, &diagnostic);
    if (!status.ok() || chart.notes.size() != 512u || diagnostic.source_row_count != 520u ||
        diagnostic.native_prefix_row_count != 512u || diagnostic.tail_rows.size() != 8u ||
        diagnostic.tail_rows.front().source_row != 512u || diagnostic.tail_rows.back().source_row != 519u ||
        chart.notes.back().beat != fixture_config.notes[511].beat ||
        diagnostic.tail_rows.front().source.beat != fixture_config.notes[512].beat) {
        return fail("diagnostic compiler did not isolate the exact 512+8 split");
    }
    auto mutated_diagnostic = diagnostic;
    if (!ff7rp::pipeline::diagnostic_charts_equal(diagnostic, mutated_diagnostic)) {
        return fail("identical diagnostic tails did not compare equal");
    }
    mutated_diagnostic.tail_rows.front().source.duration_beats += 0.25;
    if (ff7rp::pipeline::diagnostic_charts_equal(diagnostic, mutated_diagnostic)) {
        return fail("mutated diagnostic source tail was accepted");
    }
    mutated_diagnostic = diagnostic;
    mutated_diagnostic.tail_rows.front().compiled.time_str += "_mutated";
    if (ff7rp::pipeline::diagnostic_charts_equal(diagnostic, mutated_diagnostic)) {
        return fail("mutated compiled diagnostic tail was accepted");
    }
    mutated_diagnostic = diagnostic;
    std::swap(mutated_diagnostic.tail_rows[0], mutated_diagnostic.tail_rows[1]);
    if (ff7rp::pipeline::diagnostic_charts_equal(diagnostic, mutated_diagnostic)) {
        return fail("reordered diagnostic tail was accepted");
    }
    mutated_diagnostic = diagnostic;
    mutated_diagnostic.descriptor_hash ^= 1u;
    if (ff7rp::pipeline::diagnostic_charts_equal(diagnostic, mutated_diagnostic)) {
        return fail("mutated diagnostic descriptor identity was accepted");
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    std::string invalid_fixture = fixture_json.str();
    const std::size_t marker = invalid_fixture.rfind(",{");
    invalid_fixture.erase(marker, invalid_fixture.find("]}", marker) - marker);
    status = ff7rp::pipeline::parse_song_json_string(invalid_fixture, &fixture_config);
    if (status.ok()) return fail("non-520 diagnostic fixture did not fail strict schema validation");

    static constexpr std::array<const char*, 12> canonical_names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const auto& pitch_map = ff7rp::pipeline::supported_pitch_map();
    if (pitch_map.size() != 73 || pitch_map.front().pitch != "C1" || pitch_map.back().pitch != "C7") {
        return fail("unexpected canonical pitch-map boundaries");
    }
    for (int semitone = 12; semitone <= 84; ++semitone) {
        const std::string pitch = std::string(canonical_names[semitone % 12]) + std::to_string(semitone / 12);
        const std::string expected = expected_monotone(semitone);
        const auto& entry = pitch_map[semitone - 12];
        if (entry.pitch != pitch || entry.monotone_id != expected) {
            return fail("unexpected canonical map entry for " + pitch);
        }

        std::string monotone;
        status = ff7rp::pipeline::pitch_to_monotone_id(pitch, &monotone);
        if (!status.ok() || monotone != expected) {
            return fail("canonical pitch mapping failed for " + pitch);
        }
    }

    static constexpr std::array<int, 7> natural_semitones{9, 11, 0, 2, 4, 5, 7}; // A through G
    for (char note = 'A'; note <= 'G'; ++note) {
        for (char accidental : {'b', '#'}) {
            for (int octave = 0; octave <= 8; ++octave) {
                const std::string pitch = input_pitch(note, accidental, octave);
                const int semitone = octave * 12 + natural_semitones[note - 'A'] + (accidental == '#' ? 1 : -1);
                std::string monotone;
                status = ff7rp::pipeline::pitch_to_monotone_id(pitch, &monotone);
                if (semitone >= 12 && semitone <= 84) {
                    if (!status.ok() || monotone != expected_monotone(semitone)) {
                        return fail("enharmonic pitch mapping failed for " + pitch);
                    }
                } else if (status.ok()) {
                    return fail("out-of-range enharmonic pitch unexpectedly accepted: " + pitch);
                }
            }
        }
    }

    for (const char* pitch : {"B0", "Cb1", "C#7", "B#7", "C8", "H4", "c4", "DB4", "Dbb4", "D4x", "D10", ""}) {
        std::string monotone;
        status = ff7rp::pipeline::pitch_to_monotone_id(pitch, &monotone);
        if (status.ok()) {
            return fail("invalid or out-of-range pitch unexpectedly accepted: " + std::string(pitch));
        }
    }
    std::string monotone;
    status = ff7rp::pipeline::pitch_to_monotone_id("B#0", &monotone);
    if (!status.ok() || monotone != "Cn1") {
        return fail("lower octave-crossing enharmonic failed");
    }
    status = ff7rp::pipeline::pitch_to_monotone_id("B#6", &monotone);
    if (!status.ok() || monotone != "Cn7") {
        return fail("upper octave-crossing enharmonic failed");
    }
    status = ff7rp::pipeline::pitch_to_monotone_id("Cb7", &monotone);
    if (!status.ok() || monotone != "Bn6") {
        return fail("downward octave-crossing enharmonic failed");
    }

    const std::string sidecar_path = ff7rp::pipeline::cache_sidecar_mabf_path("Music/TestSong");
    if (sidecar_path.find(".cache") == std::string::npos || sidecar_path.find("song.mabf.bin") == std::string::npos) {
        return fail("unexpected cache sidecar path: " + sidecar_path);
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    const auto policy_before = ff7rp::pipeline::chart_row_policy_snapshot();
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    const auto policy_equivalent = ff7rp::pipeline::chart_row_policy_snapshot();
    if (policy_equivalent.generation != policy_before.generation ||
        policy_equivalent.identity() != policy_before.identity()) {
        return fail("equivalent chart-policy configuration churned generation");
    }
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    const auto policy_changed = ff7rp::pipeline::chart_row_policy_snapshot();
    if (policy_changed.generation == policy_before.generation || !policy_changed.enabled ||
        policy_changed.accepted_input_limit != 1024u || policy_changed.publication_limit != 512u) {
        return fail("chart-policy snapshot did not publish one coherent configuration");
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);

    int dump_difficulty = 0;
    std::string argument_error;
    if (!ff7rp::tools::parse_dump_difficulty("6", &dump_difficulty, &argument_error)
        || dump_difficulty != 6) {
        return fail("song_cache_tool rejected a canonical dump difficulty");
    }
    for (const char* invalid : {"", "0", "7", "2x", "+2", " 2", "2 "}) {
        if (ff7rp::tools::parse_dump_difficulty(invalid, &dump_difficulty, &argument_error)) {
            return fail("song_cache_tool accepted invalid dump difficulty: " + std::string(invalid));
        }
    }
    std::string documentation_error;
    if (!ff7rp::tests::verify_documentation_parity(&documentation_error)) {
        return fail("documentation parity failed: " + documentation_error);
    }
    if (!test_documentation_registry_parser(&documentation_error)) {
        return fail("documentation registry parser failed: " + documentation_error);
    }
    if (!test_release_authority_parser(&documentation_error)) {
        return fail("release authority parser failed: " + documentation_error);
    }
    if (!test_staged_documentation_failures(&documentation_error)) {
        return fail("staged documentation parity failed: " + documentation_error);
    }

    std::cout << "pipeline_selftest ok\n";
    return 0;
}
