#include <array>
#include <array>
#include <iostream>
#include <string>
#include <vector>

#include "pipeline/chart_compiler.h"
#include "pipeline/chord_voicing.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/resolved_song_renderer.h"
#include "pipeline/song_json.h"
#include "tests/target_native_assets.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "resolved_song_renderer_selftest: " << message << '\n';
    return 1;
}

ff7rp::pipeline::SongConfig base_config(const std::string& title, const int difficulty) {
    ff7rp::pipeline::SongConfig config;
    config.title = title;
    config.bpm = 120.0;
    config.bpm_provided = true;
    config.difficulty = difficulty;
    config.score_thresholds = {0, 100, 200, 300};
    config.score_thresholds_provided = true;
    config.mode_change_combo_counts = {4, 8};
    config.mode_change_combo_counts_provided = true;
    return config;
}

bool compile_profile(ff7rp::pipeline::SongConfig config,
                     ff7rp::pipeline::LoadedDifficultyProfile* out) {
    ff7rp::pipeline::CompiledChart chart;
    ff7rp::pipeline::DiagnosticChartRetention tail;
    const auto status = ff7rp::pipeline::compile_chart(config, &chart, &tail);
    if (!status.ok()) return false;
    out->config = std::move(config);
    out->chart = std::move(chart);
    out->diagnostic_chart = std::move(tail);
    if (!out->diagnostic_chart.tail_rows.empty()) {
        out->config.notes.resize(ff7rp::pipeline::kMaxChartRows);
        out->config.diagnostic_extended_chart_fixture = true;
    }
    return true;
}

} // namespace

int main() {
    using namespace ff7rp::pipeline;

    const std::string cyrillic_text = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    const std::string cyrillic_title = "Resolved \"" + cyrillic_text + "\"";
    SongConfig config = base_config(cyrillic_title, 3);
    Note dual;
    dual.beat = 0.0;
    dual.duration_beats = 1.0;
    dual.pitch = "C4";
    dual.chord_id = "pca_C";
    dual.group_index = 7;
    dual.alternate_monotone = true;
    dual.ignore_sound_pitches = {"En2"};
    dual.monotone_note_value = {{0, 0}, true};
    dual.chord_note_value = {{3, 1}, true};
    Note follower;
    follower.beat = 1.0;
    follower.duration_beats = 0.5;
    follower.pitch = "D4";
    follower.group_index = 7;
    config.notes = {dual, follower};

    LoadedDifficultyProfile root_profile;
    if (!compile_profile(config, &root_profile)) return fail("could not compile representative root chart");
    LoadedSong root_song;
    root_song.config = root_profile.config;
    root_song.chart = root_profile.chart;
    root_song.difficulty_profiles = {root_profile};

    std::string root_json;
    std::string root_json_again;
    if (!render_resolved_song_json(root_song, false, &root_json).ok() ||
        !render_resolved_song_json(root_song, false, &root_json_again).ok() ||
        root_json != root_json_again) {
        return fail("root projection was not deterministic");
    }
    ParsedSongSource parsed_root;
    if (!parse_song_json_string(root_json, &parsed_root).ok() ||
        !parsed_root.authored_profiles.empty() || parsed_root.config.title != cyrillic_title ||
        root_json.find(cyrillic_text) == std::string::npos ||
        parsed_root.config.notes.size() != 2u) {
        return fail("root projection did not parse as explicit source JSON");
    }
    const Note& parsed_dual = parsed_root.config.notes.front();
    if (parsed_dual.pitch != "C4" || parsed_dual.chord_id != "pca_C" ||
        parsed_dual.group_index != 7u || !parsed_dual.alternate_monotone ||
        parsed_dual.ignore_sound_pitches != std::vector<std::string>{"En2"} ||
        parsed_dual.monotone_note_value != NoteValueOverride{{0, 0}, true} ||
        parsed_dual.chord_note_value != NoteValueOverride{{3, 1}, true} ||
        root_json.find("source_chord_pitches") != std::string::npos ||
        root_json.find("camera") != std::string::npos || root_json.find("cache_key") != std::string::npos) {
        return fail("root projection lost public row semantics or exposed internal fields");
    }

    const std::array<std::string, 5> malformed_utf8{{
        std::string("\xe2\x82", 2),
        std::string("\xc0\xaf", 2),
        std::string("\xed\xa0\x80", 3),
        std::string("\xf4\x90\x80\x80", 4),
        std::string("\xe2\x28\xa1", 3),
    }};
    if (test_target_native_assets().has_verified_authored_chord_voicing()) {
        auto voiced_config = config;
        voiced_config.notes_provided = true;
        voiced_config.chord_voicings = {{"pca_C", {"Cn3", "En3", "Gn3"}}, {"pca_G_9", {"Gn2", "Bn2", "Dn3"}}};
        voiced_config.notes.front().ignore_sound_pitches = {"En3"};
        LoadedDifficultyProfile voiced_profile;
        if (!compile_profile(voiced_config, &voiced_profile)) return fail("voiced export fixture compilation failed");
        LoadedSong voiced_song;
        voiced_song.config = voiced_profile.config;
        voiced_song.chart = voiced_profile.chart;
        voiced_song.difficulty_profiles = {voiced_profile};
        for (const bool as_profiles : {false, true}) {
            std::string exported, repeated;
            ParsedSongSource imported;
            if (!render_resolved_song_json(voiced_song, as_profiles, &exported).ok() ||
                !render_resolved_song_json(voiced_song, as_profiles, &repeated).ok() || exported != repeated ||
                !parse_song_json_string(exported, &imported).ok() ||
                imported.config.chord_voicings != voiced_config.chord_voicings ||
                imported.config.notes.front().ignore_sound_pitches != std::vector<std::string>{"En3"})
                return fail("complete resolved export lost shared voicing/filter contract");
            CompiledChart imported_chart;
            if (!compile_chart(imported.config, &imported_chart).ok() ||
                imported_chart.notes.front().ignore_sound_ids != voiced_song.chart.notes.front().ignore_sound_ids ||
                imported_chart.notes.front().group_index != voiced_song.chart.notes.front().group_index ||
                imported_chart.notes.front().monotone_id != voiced_song.chart.notes.front().monotone_id)
                return fail("resolved voicing copy did not recompile with unchanged row semantics");
        }
        auto mismatch = voiced_song;
        mismatch.difficulty_profiles.front().config.chord_voicings.clear();
        std::string sentinel = "sentinel";
        if (render_resolved_song_json(mismatch, true, &sentinel).ok() || sentinel != "sentinel")
            return fail("resolved export published inconsistent profile voicing");
        voiced_song.chart_from_midi = true;
        if (render_resolved_song_json(voiced_song, true, &sentinel).ok())
            return fail("resolved export silently authorized MIDI voicing");
    }
    for (const std::string& malformed : malformed_utf8) {
        LoadedSong invalid_utf8 = root_song;
        invalid_utf8.config.title = malformed;
        std::string rejected = "unchanged";
        if (render_resolved_song_json(invalid_utf8, false, &rejected).ok() ||
            rejected != "unchanged") {
            return fail("malformed UTF-8 was rendered or changed the prior output buffer");
        }
    }

    SongConfig harder = config;
    harder.difficulty = 5;
    harder.notes[1].beat = 2.0;
    LoadedDifficultyProfile harder_profile;
    if (!compile_profile(harder, &harder_profile)) return fail("could not compile sparse profile fixture");
    LoadedSong profile_song = root_song;
    profile_song.difficulty_profiles = {root_profile, harder_profile};
    profile_song.config.score_thresholds_provided = false;
    profile_song.config.mode_change_combo_counts_provided = false;
    std::string profiles_json;
    if (!render_resolved_song_json(profile_song, true, &profiles_json).ok()) {
        return fail("authored profile projection failed");
    }
    ParsedSongSource parsed_profiles;
    if (!parse_song_json_string(profiles_json, &parsed_profiles).ok() ||
        parsed_profiles.authored_profiles.size() != 2u ||
        parsed_profiles.authored_profiles[0].difficulty != 3 ||
        parsed_profiles.authored_profiles[1].difficulty != 5 ||
        profiles_json.find("\"score_thresholds\"") != std::string::npos ||
        profiles_json.find("\"mode_change_combo_counts\"") != std::string::npos) {
        return fail("profile projection changed sparse labels or froze derived metadata");
    }

    configure_chart_row_limit(true, true);
    SongConfig extended = base_config("Resolved Extended", 6);
    extended.notes.reserve(513);
    for (int index = 0; index < 513; ++index) {
        Note note;
        note.beat = static_cast<double>(index);
        note.duration_beats = 0.25;
        note.pitch = "C4";
        extended.notes.push_back(std::move(note));
    }
    LoadedDifficultyProfile extended_profile;
    const bool extended_compiled = compile_profile(extended, &extended_profile);
    configure_chart_row_limit(false, false);
    if (!extended_compiled || extended_profile.config.notes.size() != 512u ||
        extended_profile.diagnostic_chart.tail_rows.size() != 1u) {
        return fail("could not build 512-prefix plus retained-tail fixture");
    }
    LoadedSong extended_song;
    extended_song.config = extended_profile.config;
    extended_song.chart = extended_profile.chart;
    extended_song.difficulty_profiles = {extended_profile};
    std::string extended_json;
    if (!render_resolved_song_json(extended_song, false, &extended_json).ok()) {
        return fail("complete extended projection failed");
    }
    ParsedSongSource parsed_extended;
    if (!parse_song_json_string(extended_json, &parsed_extended).ok() ||
        parsed_extended.config.notes.size() != 513u ||
        parsed_extended.config.notes.back().beat != 512.0 ||
        parsed_extended.config.notes.back().monotone_note_value != NoteValueOverride{{3, 0}, true}) {
        return fail("extended projection clipped or changed its retained tail");
    }

    if (test_target_native_assets().has_verified_authored_chord_voicing()) {
        extended_song.config.notes_provided = true;
        extended_song.config.chord_voicings = {{"pca_C", {"Cn3", "En3", "Gn3"}}};
        extended_song.difficulty_profiles.front().config = extended_song.config;
        if (!render_resolved_song_json(extended_song, false, &extended_json).ok() ||
            !parse_song_json_string(extended_json, &parsed_extended).ok() ||
            parsed_extended.config.notes.size() != 513u ||
            parsed_extended.config.chord_voicings != extended_song.config.chord_voicings)
            return fail("prefix/tail resolved export clipped song-wide voicing");
    }
    std::cout << "resolved_song_renderer_selftest ok\n";
    return 0;
}
