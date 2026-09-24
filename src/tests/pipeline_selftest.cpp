#include "tests/target_native_assets.h"
#include "game/song_registry.h"
#include "pipeline/chart_compiler.h"
#include "pipeline/chart_event_plan.h"
#include "pipeline/native_chord_constituents.h"
#include "pipeline/chord_voicing.h"
#include "pipeline/midi_chart_compilation.h"
#include "pipeline/runtime_cache_codec.h"
#include "pipeline/cache.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/song_json.h"
#include "tools/song_cache_tool_args.h"

#include <array>
#include <cmath>
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

int test_chord_inventory()
{
    using namespace ff7rp::pipeline;
    const auto current = native_asset_capabilities_for_catalog("ff7rebirth-steam-win64-6a16ced2");
    const auto older = native_asset_capabilities_for_catalog("ff7rebirth-steam-win64-68fd6fde");
    const auto unknown = native_asset_capabilities_for_catalog("unknown");
    std::size_t three = 0, four = 0;
    std::set<std::string_view> ids;
    for (const auto& entry : kVerifiedNativeChordConstituents) {
        if (!ids.insert(entry.chord_id).second) return fail("duplicate native chord ID");
        if (entry.sound_count == 3u) ++three;
        else if (entry.sound_count == 4u) ++four;
        else return fail("unsupported native chord width");
        std::set<std::string_view> sounds;
        for (std::size_t index = 0; index < entry.sound_names.size(); ++index) {
            const auto sound = entry.sound_names[index];
            if (index >= entry.sound_count) {
                if (!sound.empty()) return fail("nonempty unused native chord slot");
                continue;
            }
            if (!is_verified_native_sound(sound) || !sounds.insert(sound).second)
                return fail("invalid or duplicate exact native chord sound");
            for (const auto assets : {older, current}) {
                SongConfig config;
                config.bpm = 120;
                config.notes_provided = true;
                config.notes = {{0, 1, "", std::string(entry.chord_id)}};
                config.notes.front().ignore_sound_pitches = {std::string(sound)};
                CompiledChart chart;
                if (find_verified_native_chord(entry.chord_id, assets) != &entry ||
                    !compile_chart(config, &chart, nullptr, 512, assets).ok() ||
                    chart.notes.front().ignore_sound_ids != std::array<std::string, 3>{std::string(sound), "", ""})
                    return fail("stock native constituent failed exact IgnoreSound compilation");
                config.notes.front().ignore_sound_pitches = {"Cn7"};
                if (compile_chart(config, &chart, nullptr, 512, assets).ok())
                    return fail("stock native chord accepted a nonconstituent");
            }
        }
    }
    if (ids.size() != 170u || three != 68u || four != 102u)
        return fail("complete native chord inventory count/width mismatch");
    const auto* fdim = find_verified_native_chord("pca_F_dim", current);
    if (!fdim || fdim->sound_count != 3u ||
        fdim->sound_names != std::array<std::string_view, 4>{"Fn2", "Gs2", "Bn2", ""} ||
        find_verified_native_chord("pca_Db", unknown) ||
        find_verified_native_chord("pca_unknown", current))
        return fail("Fdim native spelling or fail-closed Db/unknown identity changed");
    SongConfig parsed;
    CompiledChart chart;
    if (!parse_song_json_string(R"({"schema":"v2","title":"Fdim","bpm":120,
            "notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_F_dim","ignore_sound":["Gs2"]}]})", &parsed).ok() ||
        !compile_chart(parsed, &chart).ok() || chart.notes.front().ignore_sound_ids[0] != "Gs2")
        return fail("authored Fdim stock IgnoreSound JSON failed");
    if (test_target_native_assets().has_verified_authored_chord_voicing() &&
        (!parse_song_json_string(R"({"schema":"v2","title":"Fdim revoiced","bpm":120,
            "chord_voicings":{"pca_F_dim":["Fn3","Gs3","Bn3"]},
            "notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_F_dim","ignore_sound":["Gs3"]}]})", &parsed).ok() ||
         !compile_chart(parsed, &chart).ok() || chart.notes.front().ignore_sound_ids[0] != "Gs3"))
        return fail("authored Fdim ordered three-slot voicing JSON failed");
    return 0;
}

int test_authored_chord_voicings()
{
    using namespace ff7rp::pipeline;
    const auto current = native_asset_capabilities_for_catalog("ff7rebirth-steam-win64-6a16ced2");
    const auto older = native_asset_capabilities_for_catalog("ff7rebirth-steam-win64-68fd6fde");
    const auto unknown = native_asset_capabilities_for_catalog("unknown");
    SongConfig config;
    config.bpm = 120;
    config.notes_provided = true;
    config.notes = {{0, 1, "", "pca_C"}};
    config.notes.front().ignore_sound_pitches = {"En3"};
    config.chord_voicings = {{"pca_C", {"Cn3", "En3", "Gn3"}}};
    const NormalizedMidiSource unused_source;
    const WavAudio unused_audio;
    const auto midi_attempt = compile_normalized_midi_chart(
        {unused_source, unused_audio, config, nullptr, 0, test_target_native_assets()});
    if (midi_attempt.status.ok() || !midi_attempt.notes.empty() ||
        midi_attempt.status.message.find("cannot revoice automatic MIDI inference") == std::string::npos)
        return fail("automatic MIDI compilation silently applied authored voicing");
    CompiledChart chart;
    if (!current.has_verified_authored_chord_voicing() || !older.has_verified_authored_chord_voicing() ||
        unknown.has_verified_authored_chord_voicing() ||
        !compile_chart(config, &chart, nullptr, 512, current).ok() ||
        chart.notes.front().ignore_sound_ids != std::array<std::string, 3>{"En3", "", ""} ||
        !compile_chart(config, &chart, nullptr, 512, older).ok() ||
        chart.notes.front().ignore_sound_ids != std::array<std::string, 3>{"En3", "", ""} ||
        compile_chart(config, &chart, nullptr, 512, unknown).ok())
        return fail("authored chord voicing exact-build/effective IgnoreSound contract failed");
    for (const auto& ignored : std::vector<std::vector<std::string>>{
            {"En2"}, {"E3"}, {"en3"}, {"En3", "En3"}, {"Cn3", "En3", "Gn3", "Bn3"}}) {
        auto invalid = config;
        invalid.notes.front().ignore_sound_pitches = ignored;
        if (compile_chart(invalid, &chart, nullptr, 512, current).ok() ||
            compile_chart(invalid, &chart, nullptr, 512, older).ok())
            return fail("authored voicing admitted invalid effective IgnoreSound");
    }
    auto silent = config;
    silent.notes.front().ignore_sound_pitches = {"Cn3", "En3", "Gn3"};
    if (!compile_chart(silent, &chart, nullptr, 512, current).ok() || chart.notes.size() != 1u)
        return fail("silent-after-filter chord lost its original row/action");
    auto stock = config;
    stock.chord_voicings.clear();
    if (compile_chart(stock, &chart, nullptr, 512, current).ok())
        return fail("unoverridden stock chord accepted an octave-shifted filter");
    stock.notes.front().ignore_sound_pitches = {"En2"};
    if (!compile_chart(stock, &chart, nullptr, 512, older).ok())
        return fail("ordinary stock chord behavior changed");

    // Exercise the entire bounded chord inventory without song-specific rules.
    for (const auto& entry : kVerifiedNativeChordConstituents) {
        auto all_chords = config;
        all_chords.notes.front().chord_id = std::string(entry.chord_id);
        all_chords.notes.front().ignore_sound_pitches.clear();
        all_chords.chord_voicings = {{std::string(entry.chord_id), {"Cn3", "En3", "Gn3"}}};
        if (!compile_chart(all_chords, &chart, nullptr, 512, current).ok() ||
            !compile_chart(all_chords, &chart, nullptr, 512, older).ok())
            return fail("verified chord inventory rejected same-width or shortened voicing");
        all_chords.chord_voicings.front().sound_ids.push_back("Bn3");
        if (compile_chart(all_chords, &chart, nullptr, 512, current).ok() != (entry.sound_count == 4u) ||
            compile_chart(all_chords, &chart, nullptr, 512, older).ok() != (entry.sound_count == 4u))
            return fail("authored chord voicing extended beyond original velocity-slot width");
    }
    const std::string prefix = R"({"schema":"v2","title":"voicing","bpm":120,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["En3"]}],"chord_voicings":)";
    const std::string mapping = R"({"pca_C":["Cn3","En3","Gn3"]})";
    SongConfig parsed;
    const auto parsed_status = parse_song_json_string(prefix + mapping + "}", &parsed);
    if (!test_target_native_assets().has_verified_authored_chord_voicing()) {
        if (parsed_status.ok()) return fail("unsupported selected catalog accepted authored voicing JSON");
        return 0;
    }
    if (!parsed_status.ok() || parsed.chord_voicings != config.chord_voicings)
        return fail("authored chord voicing JSON failed to preserve ordered slots");
    for (const auto* invalid : {"null", "[]", "{}", "true",
            R"({"pca_C":"Cn3"})", R"({"pca_C":[]})", R"({"pca_C":[3]})",
            R"({"pca_C":["Cn3","Cn3"]})", R"({"pca_C":["Cn3","En3","Gn3","Bn3"]})",
            R"({"pca_C":["Cn3"],"pca_C":["En3"]})", R"({"pca_unknown":["Cn3"]})",
            R"({"pca_C":["C3"]})", R"({"pca_C":["Cn3_2"]})", R"({"pca_C":["Cb3"]})",
            R"({"pca_C":["Cs7"]})", R"({"pca_C":["Cn0"]})", R"({"pca_C":["en3"]})"}) {
        if (parse_song_json_string(prefix + invalid + "}", &parsed).ok())
            return fail("malformed chord_voicings JSON accepted: " + std::string(invalid));
    }
    for (const auto* sound : {"Cn1", "Db1", "Eb3", "Gb4", "Ab5", "Bb6", "Bn6", "Cn7"}) {
        if (!parse_song_json_string(prefix + "{\"pca_C\":[\"" + sound + "\"]}}", &parsed).ok())
            return fail("verified exact sound spelling was rejected");
    }
    const auto midi_only = parse_song_json_string(
        R"({"schema":"v2","title":"midi","chord_voicings":)" + mapping + "}", &parsed);
    if (midi_only.ok() || midi_only.message.find("export resolved-song.json then author") == std::string::npos)
        return fail("MIDI-only authored voicing rejection was not actionable");
    const std::string profiles = R"({"schema":"v2","title":"profiles","bpm":120,"profiles":[{"difficulty":1,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C","ignore_sound":["En3"]}]},{"difficulty":3,"notes":[{"beat":1,"duration_beats":1,"chord_id":"pca_C"}]}],"chord_voicings":)";
    ParsedSongSource source;
    if (!parse_song_json_string(profiles + mapping + "}", &source).ok() || !source.config.notes_provided)
        return fail("shared authored-profile mapping did not parse");
    for (const auto& profile : source.authored_profiles) {
        auto shared = source.config;
        shared.notes = profile.notes;
        if (!compile_chart(shared, &chart).ok()) return fail("authored profile did not inherit root voicing");
    }
    if (parse_song_json_string(R"({"schema":"v2","title":"bad","bpm":120,"profiles":[{"difficulty":1,"chord_voicings":{"pca_C":["Cn3"]},"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_C"}]}]})", &source).ok())
        return fail("profile-local chord_voicings escaped closed schema");

    SongConfig a, b;
    if (!parse_song_json_string(prefix + R"({"pca_D":["Dn3"],"pca_C":["Cn3","En3","Gn3"]}})", &a).ok() ||
        !parse_song_json_string(prefix + R"({"pca_C":["Cn3","En3","Gn3"],"pca_D":["Dn3"]}})", &b).ok() ||
        !song_configs_equal(a, b) || !compile_chart(a, &chart).ok() ||
        config_chart_semantic_hash(a, chart) != config_chart_semantic_hash(b, chart))
        return fail("chord key order changed canonical configuration identity");
    std::swap(b.chord_voicings.front().sound_ids[0], b.chord_voicings.front().sound_ids[1]);
    if (song_configs_equal(a, b) || config_chart_semantic_hash(a, chart) == config_chart_semantic_hash(b, chart))
        return fail("sound-slot order lost semantic identity");
    return 0;
}

int test_note_values()
{
    using namespace ff7rp::pipeline;
    constexpr std::array<const char*, 14> names{{
        "whole", "dotted_whole", "half", "dotted_half", "quarter", "dotted_quarter",
        "eighth", "dotted_eighth", "sixteenth", "dotted_sixteenth",
        "one_third", "dotted_one_third", "one_sixth", "dotted_one_sixth"}};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const NativeNoteValue expected{static_cast<std::uint8_t>(index / 2u),
            static_cast<std::uint8_t>(index % 2u)};
        // Exercise each present side and absent-side zeroing, plus independent dual values.
        for (const int sides : {1, 2, 3}) {
            std::string row = R"({"beat":1.25,"duration_beats":0.375)";
            if (sides & 1) row += std::string(R"(,"pitch":"C4","monotone_note_value":")") + names[index] + '"';
            if (sides & 2) row += std::string(R"(,"chord_id":"pca_C","chord_note_value":")") +
                names[sides == 3 ? (index + 1u) % names.size() : index] + '"';
            row += '}';
            SongConfig config;
            const auto status = parse_song_json_string(
                R"({"schema":"v2","title":"values","bpm":120,"notes":[)" + row + "]}", &config);
            CompiledChart chart;
            ChartEventPlan plan;
            if (!status.ok() || !compile_chart(config, &chart).ok() ||
                !derive_chart_event_plan(config.notes, chart.notes, &plan))
                return fail("authored note value failed parsing, compilation, or event planning");
            const auto& source = config.notes.front();
            const auto& compiled = chart.notes.front();
            const std::size_t chord_index = sides == 3 ? (index + 1u) % names.size() : index;
            const NativeNoteValue chord_expected{static_cast<std::uint8_t>(chord_index / 2u),
                static_cast<std::uint8_t>(chord_index % 2u)};
            if (source.monotone_note_value != ((sides & 1) ? NoteValueOverride{expected, true} : NoteValueOverride{}) ||
                source.chord_note_value != ((sides & 2) ? NoteValueOverride{chord_expected, true} : NoteValueOverride{}) ||
                compiled.monotone_note_type != ((sides & 1) ? expected.note_type : 0) ||
                compiled.monotone_dot_type != ((sides & 1) ? expected.dot_type : 0) ||
                compiled.chord_note_type != ((sides & 2) ? chord_expected.note_type : 0) ||
                compiled.chord_dot_type != ((sides & 2) ? chord_expected.dot_type : 0) ||
                compiled.beat != 1.25 || compiled.duration_beats != 0.375 || compiled.time_str != "00_38" ||
                plan.native_event_count != (sides == 3 ? 2u : 1u) ||
                plan.required_action_count != plan.native_event_count)
                return fail("note-value extension changed exact pairs, timing, absent sides, or actions");
            auto event = chart_event_row_from_compiled(compiled);
            for (const int invalid : {-1, 7, 255, 256}) {
                auto bad = event;
                if (sides & 1) bad.monotone_note_type = invalid;
                else bad.chord_note_type = invalid;
                if (derive_chart_event_plan({bad}).error != ChartEventPlanError::InvalidRow)
                    return fail("event planner accepted an unsupported note type");
            }
            for (const int invalid : {-1, 2, 255, 256}) {
                auto bad = event;
                if (sides & 2) bad.chord_dot_type = invalid;
                else bad.monotone_dot_type = invalid;
                if (derive_chart_event_plan({bad}).error != ChartEventPlanError::InvalidRow)
                    return fail("event planner accepted an unsupported dot type");
            }
        }
    }
    for (unsigned type = 0; type < 256; ++type) {
        for (unsigned dot = 0; dot < 256; ++dot) {
            if (supported_native_note_value({static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(dot)}) !=
                (type <= 6u && dot <= 1u)) return fail("native byte domain is not exactly 0..6 / 0..1");
        }
    }
    for (const char* value : {"\"thirty_second\"", "\"One_third\"", "\"one_third \"", "5", "null"}) {
        SongConfig rejected;
        if (parse_song_json_string(std::string(R"({"schema":"v2","title":"bad","bpm":120,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4","monotone_note_value":)") +
                value + "}]}", &rejected).ok()) return fail("malformed note-value token accepted");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (test_note_values() != 0) return 1;
    if (argc == 2 && std::string_view(argv[1]) == "--note-values-only") return 0;
    if (std::string_view(ff7rp::pipeline::kPipelineCacheVersion)
        != "ff7rpianosongs.pipeline.v48") {
        return fail("pipeline cache identity did not invalidate pre-voicing serialized configurations");
    }
    if (test_authored_chord_voicings() != 0) return 1;
    if (test_chord_inventory() != 0) return 1;
    if (argc == 2 && std::string_view(argv[1]) == "--chords-only") return 0;
    const auto assets_1004 = ff7rp::pipeline::native_asset_capabilities_for_catalog(
        "ff7rebirth-steam-win64-68fd6fde");
    const auto assets_1005 = ff7rp::pipeline::native_asset_capabilities_for_catalog(
        "ff7rebirth-steam-win64-6a16ced2");
    const auto assets_unknown = ff7rp::pipeline::native_asset_capabilities_for_catalog(
        "ff7rebirth-steam-win64-unknown");
    const auto selected_assets = ff7rp::pipeline::test_target_native_assets();
    if (!assets_1004.has_verified_pca_db_voicing() || !assets_1005.has_verified_pca_db_voicing()
        || assets_unknown.has_verified_pca_db_voicing()
        || assets_1004.cache_identity() != assets_1005.cache_identity()
        || assets_1004.cache_identity() == assets_unknown.cache_identity()
        || assets_1005.cache_identity() == assets_unknown.cache_identity()
        || selected_assets.cache_identity() != ff7rp::pipeline::native_asset_capabilities_for_catalog(
             FF7RP_TARGET_BUILD_ID).cache_identity()) {
        return fail("exact catalog native-asset capabilities were not fail-closed and deterministic");
    }
    std::uint64_t assets_1004_key = 0;
    std::uint64_t assets_1005_key = 0;
    if (!ff7rp::pipeline::fnv1a64_files_and_strings({},
            {"fixture", std::string(assets_1004.cache_identity())}, &assets_1004_key).ok()
        || !ff7rp::pipeline::fnv1a64_files_and_strings({},
            {"fixture", std::string(assets_1005.cache_identity())}, &assets_1005_key).ok()
        || assets_1004_key != assets_1005_key) {
        return fail("equivalent verified native-asset capabilities did not produce one cache identity");
    }
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
    ff7rp::pipeline::SongConfig duplicate_metadata_config;
    status = ff7rp::pipeline::parse_song_json_string(
        R"json({"schema":2,"title":"duplicate boundaries","score_thresholds":[0,0,100],"mode_change_combo_counts":[4,4]})json",
        &duplicate_metadata_config);
    if (!status.ok()) {
        return fail("nondecreasing metadata with duplicate values was rejected: " + status.message);
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
            R"json({"schema":2.0000001,"title":"bad"})json",
            R"json({"schema":1.9999999,"title":"bad"})json",
            R"json({"schema":"v2","title":"bad","score_thresholds":[0,200,100]})json",
            R"json({"schema":"v2","title":"bad","mode_change_combo_counts":[8,4]})json",
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
    if (chart.notes[0].monotone_note_type != 3 || chart.notes[0].chord_note_type != 3 ||
        chart.notes[1].monotone_note_type != 2 || chart.notes[1].chord_note_type != 0 ||
        chart.notes[2].monotone_note_type != 0 || chart.notes[2].chord_note_type != 3) {
        return fail("unexpected note type mapping");
    }
    const char* note_values_json = R"json({
      "schema":"v2","title":"note values","bpm":120,"notes":[
        {"beat":0,"duration_beats":0.25,"pitch":"C4","chord_id":"pca_C",
         "monotone_note_value":"whole","chord_note_value":"dotted_sixteenth"},
        {"beat":1,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"dotted_whole"},
        {"beat":2,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"half"},
        {"beat":3,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"dotted_half"},
        {"beat":4,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"quarter"},
        {"beat":5,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"dotted_quarter"},
        {"beat":6,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"eighth"},
        {"beat":7,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"dotted_eighth"},
        {"beat":8,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"sixteenth"},
        {"beat":9,"duration_beats":0.25,"pitch":"C4","monotone_note_value":"dotted_sixteenth"}
      ]})json";
    ff7rp::pipeline::SongConfig note_values_config;
    ff7rp::pipeline::CompiledChart note_values_chart;
    status = ff7rp::pipeline::parse_song_json_string(note_values_json, &note_values_config);
    if (!status.ok() || !ff7rp::pipeline::compile_chart(note_values_config, &note_values_chart).ok()
        || note_values_chart.notes.size() != 10u
        || note_values_chart.notes[0].monotone_note_type != 0
        || note_values_chart.notes[0].monotone_dot_type != 0
        || note_values_chart.notes[0].chord_note_type != 4
        || note_values_chart.notes[0].chord_dot_type != 1) {
        return fail("independent dual-side note values did not compile exactly");
    }
    const std::array<ff7rp::pipeline::NativeNoteValue, 9> expected_note_values{{
        {0, 1}, {1, 0}, {1, 1}, {2, 0}, {2, 1}, {3, 0}, {3, 1}, {4, 0}, {4, 1}}};
    if (!ff7rp::pipeline::supported_native_note_value({0, 0})
        || !ff7rp::pipeline::supported_native_note_value({4, 1})
        || !ff7rp::pipeline::supported_native_note_value({5, 0})
        || !ff7rp::pipeline::supported_native_note_value({6, 1})
        || ff7rp::pipeline::supported_native_note_value({7, 0})) {
        return fail("native note-value authoring domain did not remain exactly types 0 through 6");
    }
    for (std::size_t index = 1; index < note_values_chart.notes.size(); ++index) {
        const auto expected = expected_note_values[index - 1u];
        if (note_values_chart.notes[index].monotone_note_type != expected.note_type
            || note_values_chart.notes[index].monotone_dot_type != expected.dot_type
            || note_values_chart.notes[index].chord_note_type != 0
            || note_values_chart.notes[index].chord_dot_type != 0) {
            return fail("authored note-value mapping or absent-side zeroing changed");
        }
    }
    for (const char* invalid_note : {
            R"json({"beat":0,"duration_beats":1,"pitch":"C4","monotone_note_value":4})json",
            R"json({"beat":0,"duration_beats":1,"pitch":"C4","monotone_note_value":"thirty_second"})json",
            R"json({"beat":0,"duration_beats":1,"chord_id":"pca_C","monotone_note_value":"quarter"})json",
            R"json({"beat":0,"duration_beats":1,"pitch":"C4","chord_note_value":"quarter"})json"}) {
        ff7rp::pipeline::SongConfig rejected;
        const std::string invalid_json = std::string(R"json({"schema":"v2","title":"bad","notes":[)json")
            + invalid_note + "]}";
        if (ff7rp::pipeline::parse_song_json_string(invalid_json, &rejected).ok())
            return fail("invalid or side-less note-value override was accepted");
    }
    const char* exact_accidental_json = R"json({
        "schema":"v2","title":"Exact accidentals","bpm":120,"notes":[
          {"beat":0,"duration_beats":1,"pitch":"C#4"},
          {"beat":1,"duration_beats":1,"pitch":"Db4"},
          {"beat":2,"duration_beats":1,"pitch":"D#4"},
          {"beat":3,"duration_beats":1,"pitch":"Eb4"},
          {"beat":4,"duration_beats":1,"pitch":"F#4"},
          {"beat":5,"duration_beats":1,"pitch":"Gb4"},
          {"beat":6,"duration_beats":1,"pitch":"G#4"},
          {"beat":7,"duration_beats":1,"pitch":"Ab4"},
          {"beat":8,"duration_beats":1,"pitch":"A#4"},
          {"beat":9,"duration_beats":1,"pitch":"Bb4"}
        ]})json";
    ff7rp::pipeline::SongConfig exact_accidental_config;
    ff7rp::pipeline::CompiledChart exact_accidental_chart;
    status = ff7rp::pipeline::parse_song_json_string(exact_accidental_json, &exact_accidental_config);
    const std::array<std::string_view, 10> exact_accidental_ids{
        "Cs4", "Db4", "Ds4", "Eb4", "Fs4", "Gb4", "Gs4", "Ab4", "As4", "Bb4"
    };
    const std::array<std::string_view, 10> exact_accidental_pitches{
        "C#4", "Db4", "D#4", "Eb4", "F#4", "Gb4", "G#4", "Ab4", "A#4", "Bb4"
    };
    if (!status.ok() || !ff7rp::pipeline::compile_chart(
            exact_accidental_config, &exact_accidental_chart).ok()
        || exact_accidental_chart.notes.size() != exact_accidental_ids.size()) {
        return fail("exact authored accidental JSON did not compile");
    }
    for (std::size_t index = 0; index < exact_accidental_ids.size(); ++index) {
        if (exact_accidental_config.notes[index].pitch != exact_accidental_pitches[index]
            || exact_accidental_chart.notes[index].monotone_id != exact_accidental_ids[index]) {
            return fail("authored accidental spelling was not preserved exactly");
        }
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
    ff7rp::pipeline::SongConfig reused_group_config;
    reused_group_config.schema = "v2";
    reused_group_config.title = "reused group";
    reused_group_config.bpm = 120.0;
    for (int index = 0; index < 5; ++index) {
        ff7rp::pipeline::Note note;
        note.beat = index * 0.25;
        note.duration_beats = 0.25;
        note.pitch = "C4";
        note.group_index = index == 2 ? 0 : 1;
        reused_group_config.notes.push_back(std::move(note));
    }
    reused_group_config.notes_provided = true;
    ff7rp::pipeline::CompiledChart reused_group_chart;
    if (!ff7rp::pipeline::compile_chart(reused_group_config, &reused_group_chart).ok()) {
        return fail("run-local GroupIndex reuse after a zero separator was rejected");
    }
    ff7rp::pipeline::SongConfig mixed_group_config;
    mixed_group_config.schema = "v2";
    mixed_group_config.title = "mixed native groups";
    mixed_group_config.bpm = 120.0;
    mixed_group_config.notes_provided = true;
    const auto append_mixed = [&](const double beat, const char* pitch, const char* chord,
                                  const std::uint8_t group) {
        ff7rp::pipeline::Note note;
        note.beat = beat;
        note.duration_beats = 0.25;
        note.pitch = pitch;
        note.chord_id = chord;
        note.group_index = group;
        mixed_group_config.notes.push_back(std::move(note));
    };
    append_mixed(0.0, "", "pca_C", 9);       // chord root: one action
    append_mixed(0.0, "C4", "", 9);          // equal-time monotone continuation
    append_mixed(0.5, "D4", "pca_D", 9);    // explicit-dual continuation
    append_mixed(0.75, "E4", "pca_E", 10);  // explicit-dual root: two actions
    append_mixed(1.0, "", "pca_F", 10);      // chord continuation
    mixed_group_config.notes[2].monotone_note_value = {{0, 0}, true};
    mixed_group_config.notes[2].chord_note_value = {{3, 1}, true};
    ff7rp::pipeline::CompiledChart mixed_group_chart;
    ff7rp::pipeline::ChartEventPlan mixed_group_plan;
    if (!ff7rp::pipeline::compile_chart(mixed_group_config, &mixed_group_chart).ok()
        || !ff7rp::pipeline::derive_chart_event_plan(
            mixed_group_config.notes, mixed_group_chart.notes, &mixed_group_plan)
        || mixed_group_plan.source_row_count != 5u
        || mixed_group_plan.native_event_count != 7u
        || mixed_group_plan.required_action_count != 3u
        || mixed_group_chart.notes[2].monotone_note_type != 0
        || mixed_group_chart.notes[2].monotone_dot_type != 0
        || mixed_group_chart.notes[2].chord_note_type != 3
        || mixed_group_chart.notes[2].chord_dot_type != 1) {
        return fail("mixed row-level GroupIndex roots and continuations were not derived exactly");
    }
    std::vector<ff7rp::pipeline::ChartEventRow> runtime_rows;
    runtime_rows.reserve(mixed_group_chart.notes.size());
    for (const auto& compiled : mixed_group_chart.notes) {
        ff7r::piano::game::SongChartNote runtime;
        runtime.time_str = compiled.time_str;
        runtime.monotone_id = compiled.monotone_id;
        runtime.chord_id = compiled.chord_id;
        runtime.monotone_note_type = compiled.monotone_note_type;
        runtime.monotone_dot_type = compiled.monotone_dot_type;
        runtime.chord_note_type = compiled.chord_note_type;
        runtime.chord_dot_type = compiled.chord_dot_type;
        runtime.camera_switch_timing = compiled.camera_switch_timing;
        runtime.group_index = compiled.group_index;
        runtime.ignore_sound_ids = compiled.ignore_sound_ids;
        runtime_rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(runtime));
    }
    const auto runtime_mixed_plan = ff7rp::pipeline::derive_chart_event_plan(runtime_rows);
    if (!runtime_mixed_plan.valid()
        || !ff7rp::pipeline::chart_event_plans_equal(mixed_group_plan, runtime_mixed_plan)
        || runtime_mixed_plan.events.size() != 7u || runtime_mixed_plan.links.size() != 4u
        || runtime_mixed_plan.events[0].kind != ff7rp::pipeline::ChartEventKind::Chord
        || runtime_mixed_plan.events[0].ordinal != 1u
        || runtime_mixed_plan.events[1].root_event_index != 0u
        || runtime_mixed_plan.events[2].ordinal != 4u
        || runtime_mixed_plan.events[3].ordinal != 5u
        || !runtime_mixed_plan.events[4].parentless
        || !runtime_mixed_plan.events[5].parentless
        || runtime_mixed_plan.events[6].root_event_index != 4u
        || runtime_mixed_plan.final_group_index != 10u) {
        return fail("runtime-neutral mixed event/link plan diverged from pipeline derivation");
    }
    auto plan_mutation = runtime_mixed_plan;
    ++plan_mutation.events[1].source_row_index;
    if (ff7rp::pipeline::chart_event_plans_equal(runtime_mixed_plan, plan_mutation))
        return fail("event-row mutation did not change canonical plan equality");
    plan_mutation = runtime_mixed_plan;
    plan_mutation.events[0].kind = ff7rp::pipeline::ChartEventKind::Monotone;
    if (ff7rp::pipeline::chart_event_plans_equal(runtime_mixed_plan, plan_mutation))
        return fail("event-kind mutation did not change canonical plan equality");
    plan_mutation = runtime_mixed_plan;
    ++plan_mutation.events[0].ordinal;
    if (ff7rp::pipeline::chart_event_plans_equal(runtime_mixed_plan, plan_mutation))
        return fail("event-ordinal mutation did not change canonical plan equality");
    plan_mutation = runtime_mixed_plan;
    ++plan_mutation.links[0].child_event_index;
    if (ff7rp::pipeline::chart_event_plans_equal(runtime_mixed_plan, plan_mutation))
        return fail("event-link mutation did not change canonical plan equality");
    auto topology_mutation = runtime_rows;
    topology_mutation[2].group_index = 0;
    const auto regrouped_plan = ff7rp::pipeline::derive_chart_event_plan(topology_mutation);
    if (!regrouped_plan.valid() || regrouped_plan.physical_digest != runtime_mixed_plan.physical_digest
        || regrouped_plan.required_action_count == runtime_mixed_plan.required_action_count
        || ff7rp::pipeline::chart_event_plans_equal(runtime_mixed_plan, regrouped_plan)) {
        return fail("group topology mutation did not preserve physical identity and change links/actions");
    }
    auto physical_mutation = runtime_rows;
    physical_mutation[1].monotone_id = "Dn4";
    const auto changed_physical_plan = ff7rp::pipeline::derive_chart_event_plan(physical_mutation);
    if (!changed_physical_plan.valid()
        || changed_physical_plan.physical_digest == runtime_mixed_plan.physical_digest) {
        return fail("compiled physical-row mutation did not change the physical digest");
    }
    physical_mutation = runtime_rows;
    physical_mutation[2].chord_dot_type = 0;
    const auto changed_articulation_plan = ff7rp::pipeline::derive_chart_event_plan(physical_mutation);
    if (!changed_articulation_plan.valid()
        || changed_articulation_plan.physical_digest == runtime_mixed_plan.physical_digest) {
        return fail("side-specific articulation mutation did not change the physical digest");
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
    const char* db_ignore_sound_json = R"json({
        "schema":"v2","title":"Exact Db ignores","bpm":120,"notes":[
            {"beat":0,"duration_beats":1,"chord_id":"pca_Db"},
            {"beat":1,"duration_beats":1,"chord_id":"pca_Db","ignore_sound":["Db2"]},
            {"beat":2,"duration_beats":1,"chord_id":"pca_Db","ignore_sound":["Fn2"]},
            {"beat":3,"duration_beats":1,"chord_id":"pca_Db","ignore_sound":["Ab2"]},
            {"beat":4,"duration_beats":1,"chord_id":"pca_Db","ignore_sound":["Db2","Fn2"]},
            {"beat":5,"duration_beats":1,"chord_id":"pca_Db","ignore_sound":["Db2","Ab2"]},
            {"beat":6,"duration_beats":1,"chord_id":"pca_Db","ignore_sound":["Fn2","Ab2"]},
            {"beat":7,"duration_beats":1,"chord_id":"pca_Db","ignore_sound":["Db2","Fn2","Ab2"]}
        ]})json";
    ff7rp::pipeline::SongConfig db_ignore_config;
    ff7rp::pipeline::CompiledChart db_ignore_chart;
    status = ff7rp::pipeline::parse_song_json_string(db_ignore_sound_json, &db_ignore_config);
    if (!status.ok() || !ff7rp::pipeline::compile_chart(
            db_ignore_config, &db_ignore_chart, nullptr, 0, assets_1005).ok()
        || db_ignore_chart.notes.size() != 8u
        || db_ignore_chart.notes.front().ignore_sound_ids !=
            std::array<std::string, 3>{"", "", ""}
        || db_ignore_chart.notes.back().ignore_sound_ids !=
            std::array<std::string, 3>{"Db2", "Fn2", "Ab2"}) {
        return fail("exact verified pca_Db IgnoreSound members did not compile");
    }
    ff7rp::pipeline::CompiledChart db_ignore_1004_chart;
    const auto db_ignore_1004 = ff7rp::pipeline::compile_chart(
        db_ignore_config, &db_ignore_1004_chart, nullptr, 0, assets_1004);
    ff7rp::pipeline::CompiledChart db_ignore_unknown_chart;
    const auto db_ignore_unknown = ff7rp::pipeline::compile_chart(
        db_ignore_config, &db_ignore_unknown_chart, nullptr, 0, assets_unknown);
    if (!db_ignore_1004.ok()
        || db_ignore_1004_chart.notes.back().ignore_sound_ids !=
            std::array<std::string, 3>{"Db2", "Fn2", "Ab2"}
        || db_ignore_unknown.code != ff7rp::pipeline::StatusCode::InvalidChart
        || db_ignore_unknown.message !=
            "ignore_sound must name unique exact constituents of the row's verified native chord") {
        return fail("build-scoped pca_Db IgnoreSound constituent authority was not exact");
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
        if (status.ok() && ff7rp::pipeline::compile_chart(
                rejected, &rejected_chart, nullptr, 0, assets_1005).ok()) {
            return fail("malformed or unsupported note semantics were accepted: " +
                std::string(invalid_note_semantics));
        }
    }
    for (const char* invalid_db_ignore : {
            "db2", "Cs2", "Db3", "Db2\",\"Db2", "Gn2"}) {
        const std::string invalid = std::string(
            R"json({"schema":"v2","title":"bad Db","bpm":120,"notes":[{"beat":0,"duration_beats":1,"chord_id":"pca_Db","ignore_sound":[")json") +
            invalid_db_ignore + R"json("]}]})json";
        ff7rp::pipeline::SongConfig rejected;
        ff7rp::pipeline::CompiledChart rejected_chart;
        status = ff7rp::pipeline::parse_song_json_string(invalid, &rejected);
        if (status.ok() && ff7rp::pipeline::compile_chart(
                rejected, &rejected_chart, nullptr, 0, assets_1005).ok()) {
            return fail("invalid exact pca_Db IgnoreSound member was accepted: " +
                std::string(invalid_db_ignore));
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
    fixture_json << "{\"schema\":\"ff7rpianosongs.song.v2\",\"title\":\"Extended 520\","
                 << "\"bpm\":120,\"difficulty\":2,\"notes\":[";
    for (std::size_t row = 0; row < 520u; ++row) {
        if (row) fixture_json << ',';
        fixture_json << "{\"beat\":" << row * 0.25
                     << ",\"duration_beats\":0.125,\"pitch\":\"C4\"";
        if (row == 511u) fixture_json << ",\"monotone_note_value\":\"whole\"";
        if (row == 512u) fixture_json << ",\"monotone_note_value\":\"dotted_sixteenth\"";
        fixture_json << '}';
    }
    fixture_json << "]}";
    ff7rp::pipeline::SongConfig fixture_config;
    status = ff7rp::pipeline::parse_song_json_string(fixture_json.str(), &fixture_config);
    if (!status.ok() || fixture_config.diagnostic_extended_chart_fixture ||
        fixture_config.notes.size() != 520u) {
        return fail("exactly-520 extended chart did not parse without the legacy flag");
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    ff7rp::pipeline::DiagnosticChartRetention diagnostic;
    auto native_boundary_config = fixture_config;
    native_boundary_config.notes.resize(ff7rp::pipeline::kMaxChartRows);
    status = ff7rp::pipeline::compile_chart(native_boundary_config, &chart, &diagnostic);
    if (!status.ok() || chart.notes.size() != ff7rp::pipeline::kMaxChartRows || diagnostic.present()) {
        return fail("exactly-512 authored chart changed under native policy");
    }
    status = ff7rp::pipeline::compile_chart(fixture_config, &chart, &diagnostic);
    if (status.ok()) return fail("extended chart compiled while policy was disabled");
    ff7rp::pipeline::configure_chart_row_limit(true, false);
    status = ff7rp::pipeline::compile_chart(fixture_config, &chart, &diagnostic);
    if (status.ok()) return fail("extended chart compiled without playable authority");
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    status = ff7rp::pipeline::compile_chart(fixture_config, &chart, &diagnostic);
    if (!status.ok() || chart.notes.size() != 512u || diagnostic.source_row_count != 520u ||
        diagnostic.native_prefix_row_count != 512u || diagnostic.tail_rows.size() != 8u ||
        diagnostic.tail_rows.front().source_row != 512u || diagnostic.tail_rows.back().source_row != 519u ||
        chart.notes.back().beat != fixture_config.notes[511].beat ||
        chart.notes.back().monotone_note_type != 0 || chart.notes.back().monotone_dot_type != 0 ||
        diagnostic.tail_rows.front().source.beat != fixture_config.notes[512].beat ||
        diagnostic.tail_rows.front().source.monotone_note_value
            != ff7rp::pipeline::NoteValueOverride{{4, 1}, true} ||
        diagnostic.tail_rows.front().compiled.monotone_note_type != 4 ||
        diagnostic.tail_rows.front().compiled.monotone_dot_type != 1) {
        return fail("extended compiler did not isolate the exact 512+8 split");
    }
    ff7rp::pipeline::SongConfig reused_256_groups;
    reused_256_groups.schema = "v2";
    reused_256_groups.title = "256 run-local groups";
    reused_256_groups.bpm = 120.0;
    reused_256_groups.notes_provided = true;
    reused_256_groups.diagnostic_extended_chart_fixture = true;
    for (std::size_t run = 0; run < 256u; ++run) {
        const std::uint8_t id = static_cast<std::uint8_t>(1u + run % 255u);
        for (int follower = 0; follower < 2; ++follower) {
            ff7rp::pipeline::Note note;
            note.beat = reused_256_groups.notes.size() * 0.25;
            note.duration_beats = 0.25;
            note.pitch = "C4";
            note.group_index = id;
            reused_256_groups.notes.push_back(std::move(note));
        }
        ff7rp::pipeline::Note separator;
        separator.beat = reused_256_groups.notes.size() * 0.25;
        separator.duration_beats = 0.25;
        separator.pitch = "D4";
        reused_256_groups.notes.push_back(std::move(separator));
    }
    ff7rp::pipeline::CompiledChart reused_256_chart;
    ff7rp::pipeline::DiagnosticChartRetention reused_256_tail;
    if (!ff7rp::pipeline::compile_chart(reused_256_groups, &reused_256_chart,
            &reused_256_tail, ff7rp::pipeline::kMaximumExtendedChartRows).ok()) {
        return fail("256 disjoint generated groups could not reuse byte IDs safely");
    }
    ff7rp::pipeline::LoadedDifficultyProfile reused_profile;
    reused_profile.config = reused_256_groups;
    reused_profile.config.notes.resize(ff7rp::pipeline::kMaxChartRows);
    reused_profile.chart = reused_256_chart;
    reused_profile.diagnostic_chart = reused_256_tail;
    ff7rp::pipeline::ChartEventPlan reused_plan;
    if (!ff7rp::pipeline::derive_profile_event_plan(reused_profile, &reused_plan)
        || reused_plan.native_event_count != 768u || reused_plan.required_action_count != 512u) {
        return fail("run-local GroupIndex reuse changed row/event/action derivation");
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

    std::ostringstream playable_json;
    playable_json << "{\"schema\":\"ff7rpianosongs.song.v2\",\"title\":\"Playable 513\","
                  << "\"bpm\":120,\"difficulty\":2,\"notes\":[";
    for (std::size_t row = 0; row < 513u; ++row) {
        if (row) playable_json << ',';
        playable_json << "{\"beat\":" << row * 0.25
                      << ",\"duration_beats\":0.125,\"pitch\":\"C4\"}";
    }
    playable_json << "]}";
    status = ff7rp::pipeline::parse_song_json_string(playable_json.str(), &fixture_config);
    if (!status.ok() || fixture_config.notes.size() != 513u) return fail("exact 513 fixture did not parse");
    status = ff7rp::pipeline::compile_chart(fixture_config, &chart, &diagnostic);
    diagnostic.descriptor_hash = ff7rp::pipeline::diagnostic_descriptor_hash("playable-513", 2, chart, diagnostic);
    if (!status.ok() || chart.notes.size() != 512u || diagnostic.source_row_count != 513u
        || diagnostic.tail_rows.size() != 1u || diagnostic.tail_rows.front().source_row != 512u
        || diagnostic.descriptor_hash == 0) return fail("exact 513 fixture was not retained as 512+1: status=" + status.message
            + " chart=" + std::to_string(chart.notes.size()) + " source=" + std::to_string(diagnostic.source_row_count)
            + " tail=" + std::to_string(diagnostic.tail_rows.size()) + " hash=" + std::to_string(diagnostic.descriptor_hash));
    auto prohibited = fixture_config;
    prohibited.notes.back().chord_id = "pca_C";
    if (!ff7rp::pipeline::compile_chart(prohibited, &chart, &diagnostic).ok()
        || diagnostic.tail_rows.size() != 1u)
        return fail("nonplayable 513 fixture was not retained diagnostically");

    std::string invalid_fixture = fixture_json.str();
    const std::size_t marker = invalid_fixture.rfind(",{");
    invalid_fixture.erase(marker, invalid_fixture.find("]}", marker) - marker);
    status = ff7rp::pipeline::parse_song_json_string(invalid_fixture, &fixture_config);
    if (!status.ok() || fixture_config.notes.size() != 519u
        || !ff7rp::pipeline::compile_chart(fixture_config, &chart, &diagnostic).ok()
        || diagnostic.tail_rows.size() != 7u) {
        return fail("general in-range diagnostic fixture was not retained");
    }

    ff7rp::pipeline::SongConfig maximum_fixture = fixture_config;
    maximum_fixture.notes.assign(ff7rp::pipeline::kMaximumExtendedChartRows,
        ff7rp::pipeline::Note{0.0, 1.0, "C4", ""});
    for (std::size_t index = 0; index < maximum_fixture.notes.size(); ++index)
        maximum_fixture.notes[index].beat = static_cast<double>(index);
    if (!ff7rp::pipeline::compile_chart(maximum_fixture, &chart, &diagnostic,
            ff7rp::pipeline::kMaximumExtendedChartRows).ok()
        || chart.notes.size() != ff7rp::pipeline::kMaxChartRows
        || diagnostic.tail_rows.size() != ff7rp::pipeline::kMaximumExtendedChartTailRows
        || diagnostic.tail_rows.back().source_row + 1u != ff7rp::pipeline::kMaximumExtendedChartRows) {
        return fail("8192-row extended boundary was not retained exactly");
    }
    ff7rp::pipeline::SongConfig maximum_event_fixture = fixture_config;
    maximum_event_fixture.notes.assign(ff7rp::pipeline::kMaximumNativeChartEvents / 2u,
        ff7rp::pipeline::Note{0.0, 1.0, "C4", "pca_C"});
    for (std::size_t index = 0; index < maximum_event_fixture.notes.size(); ++index)
        maximum_event_fixture.notes[index].beat = static_cast<double>(index);
    if (!ff7rp::pipeline::compile_chart(maximum_event_fixture, &chart, &diagnostic,
            ff7rp::pipeline::kMaximumExtendedChartRows).ok()) {
        return fail("8192-native-event dual-row boundary was rejected");
    }
    std::vector<ff7rp::pipeline::ChartNote> maximum_event_compiled = chart.notes;
    for (const auto& row : diagnostic.tail_rows) maximum_event_compiled.push_back(row.compiled);
    ff7rp::pipeline::ChartEventPlan maximum_event_plan;
    if (!ff7rp::pipeline::derive_chart_event_plan(
            maximum_event_fixture.notes, maximum_event_compiled, &maximum_event_plan)
        || maximum_event_plan.source_row_count != 4096u
        || maximum_event_plan.native_prefix_event_count != 1024u
        || maximum_event_plan.native_event_count != 8192u
        || maximum_event_plan.required_action_count != 8192u) {
        return fail("dual-row R/P/E/A derivation changed at the native-event ceiling");
    }
    std::vector<ff7rp::pipeline::ChartEventRow> representative_runtime_rows(4099u);
    for (std::size_t row = 0; row < representative_runtime_rows.size(); ++row) {
        auto& compiled = representative_runtime_rows[row];
        compiled.time_str = ff7rp::pipeline::beat_to_time_str(static_cast<double>(row), 60.0);
        compiled.monotone_id = "Cn4";
        compiled.monotone_note_type = 3;
        const bool single = row == 510u || row == 511u || row >= 4095u;
        if (!single) {
            compiled.chord_id = "pca_C";
            compiled.chord_note_type = 3;
        }
        if (row >= 510u && row <= 512u) compiled.group_index = 1;
        if (row >= 513u && row <= 516u) compiled.group_index = 2;
    }
    const auto representative_runtime_plan =
        ff7rp::pipeline::derive_chart_event_plan(representative_runtime_rows);
    if (!representative_runtime_plan.valid()
        || representative_runtime_plan.source_row_count != 4099u
        || representative_runtime_plan.native_prefix_event_count != 1022u
        || representative_runtime_plan.native_event_count != 8192u
        || representative_runtime_plan.required_action_count != 8183u
        || representative_runtime_plan.events.size() != 8192u
        || representative_runtime_plan.links.size() != 9u
        || representative_runtime_plan.events[1020].source_row_index != 510u
        || representative_runtime_plan.events[1021].root_event_index != 1020u
        || representative_runtime_plan.events[1022].root_event_index != 1020u
        || representative_runtime_plan.events[1023].root_event_index != 1020u) {
        return fail("representative R4099/P1022/E8192/A8183 runtime plan was not exact");
    }
    maximum_event_fixture.notes.push_back(
        {static_cast<double>(maximum_event_fixture.notes.size()), 1.0, "C4", ""});
    if (ff7rp::pipeline::compile_chart(maximum_event_fixture, &chart, &diagnostic,
            ff7rp::pipeline::kMaximumExtendedChartRows).ok()) {
        return fail("8193+ native events were not rejected without truncation");
    }
    maximum_fixture.notes.push_back(
        {static_cast<double>(maximum_fixture.notes.size()), 1.0, "C4", ""});
    if (ff7rp::pipeline::compile_chart(maximum_fixture, &chart, &diagnostic,
            maximum_fixture.notes.size()).ok()) {
        return fail("8193-row extended boundary did not fail closed");
    }
    ff7rp::pipeline::configure_chart_row_limit(false, false);

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
                    std::string expected = expected_monotone(semitone);
                    if (accidental == 'b' &&
                        (note == 'D' || note == 'E' || note == 'G' || note == 'A' || note == 'B')) {
                        expected = std::string(1, note) + 'b' + std::to_string(octave);
                    }
                    if (!status.ok() || monotone != expected) {
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
    const auto compile_alternate = [](const char* pitch, std::string* resolved = nullptr) {
        ff7rp::pipeline::SongConfig alternate;
        alternate.schema = "v2";
        alternate.title = "alternate accidental boundary";
        alternate.bpm = 120.0;
        alternate.notes_provided = true;
        ff7rp::pipeline::Note note;
        note.pitch = pitch;
        note.duration_beats = 1.0;
        note.alternate_monotone = true;
        alternate.notes.push_back(std::move(note));
        ff7rp::pipeline::CompiledChart compiled;
        const auto result = ff7rp::pipeline::compile_chart(alternate, &compiled);
        if (result.ok() && resolved) *resolved = compiled.notes.front().monotone_id;
        return result;
    };
    for (int octave = 2; octave <= 6; ++octave) {
        const std::string suffix = std::to_string(octave);
        for (const auto& [pitch, expected] : std::array<std::pair<std::string, std::string>, 3>{{
                 {"C" + suffix, "Cn" + suffix + "_2"},
                 {"C#" + suffix, "Cs" + suffix + "_2"},
                 {"B#" + std::to_string(octave - 1), "Cn" + suffix + "_2"},
             }}) {
            std::string resolved;
            if (!compile_alternate(pitch.c_str(), &resolved).ok() || resolved != expected) {
                return fail("verified alternate monotone mapping failed: " + pitch);
            }
        }
    }
    for (const char* pitch : {"C1", "C7", "B#0", "B#6", "C#1", "C#7",
             "Db1", "Db4", "Db6", "Cb2", "Cb4", "Cb7", "B3"}) {
        if (compile_alternate(pitch).ok()) {
            return fail("unsupported accidental alternate was accepted: " + std::string(pitch));
        }
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
    ff7rp::pipeline::configure_chart_row_limit(true, false);
    const auto policy_changed = ff7rp::pipeline::chart_row_policy_snapshot();
    if (policy_changed.generation == policy_before.generation || !policy_changed.enabled ||
        policy_changed.accepted_input_limit != ff7rp::pipeline::kMaximumExtendedChartRows
        || policy_changed.publication_limit != 512u) {
        return fail("chart-policy snapshot did not publish one coherent configuration");
    }
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    const auto playable_extended = ff7rp::pipeline::chart_row_policy_snapshot();
    if (!playable_extended.playable_extended_available
        || playable_extended.publication_limit != ff7rp::pipeline::kMaximumExtendedChartRows
        || playable_extended.identity() != ff7rp::pipeline::kPlayableExtendedChartRowPolicyIdentity) {
        return fail("playable extended policy did not publish bounded count-driven authority");
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
    std::cout << "pipeline_selftest ok\n";
    return 0;
}
