#include "game/synthetic_extended_chart_model.h"
#include "game/extended_chart_runtime_specs.h"
#include "game/hook_specs.h"
#include "pipeline/pipeline_limits.h"
#include "tests/test_support.h"
#include "tools/extended_chart_fixture.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using ff7r::piano::game::synthetic_model::BuildRequest;
using ff7r::piano::game::synthetic_model::Chart;
using ff7r::piano::game::synthetic_model::FailurePoint;
using ff7r::piano::game::synthetic_model::SourceRow;

std::vector<SourceRow> make_rows(std::size_t count)
{
    std::vector<SourceRow> rows(count);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].time = static_cast<double>(i) * 0.125;
        rows[i].owned_values = {i, i + 1000u};
    }
    return rows;
}

BuildRequest extended_request(const std::vector<SourceRow>& rows,
    FailurePoint failure_point = FailurePoint::None, std::size_t failure_step = 0)
{
    BuildRequest request;
    request.rows = &rows;
    request.native_prefix_rows = 512u;
    request.maximum_rows = 1024u;
    request.experiment_enabled = true;
    request.persistent_caller = true;
    request.active_custom_descriptor = true;
    request.failure_point = failure_point;
    request.failure_step = failure_step;
    return request;
}

bool same_chart(const Chart& left, const Chart& right)
{
    if (left.times != right.times || left.max_time != right.max_time
        || left.displayed_note_count != right.displayed_note_count
        || left.published != right.published || left.events.size() != right.events.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.events.size(); ++i) {
        const auto& a = left.events[i];
        const auto& b = right.events[i];
        if (a.source_row != b.source_row || a.chord != b.chord
            || a.camera_transition != b.camera_transition
            || a.owned_values != b.owned_values || a.parent != b.parent
            || a.successor != b.successor) {
            return false;
        }
    }
    return true;
}

bool test_default_policy()
{
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    if (ff7rp::pipeline::effective_chart_row_limit() != 512u
        || ff7rp::pipeline::chart_input_row_limit() != 512u) {
        return false;
    }
    ff7rp::pipeline::configure_chart_row_limit(true, false);
    if (!ff7rp::pipeline::experimental_extended_charts_requested()
        || ff7rp::pipeline::experimental_extended_charts_enabled()
        || ff7rp::pipeline::effective_chart_row_limit() != 512u
        || ff7rp::pipeline::chart_input_row_limit() != 512u) {
        return false;
    }
    ff7rp::pipeline::configure_chart_row_limit(true, true);
    const bool enabled = ff7rp::pipeline::effective_chart_row_limit() == 512u
        && ff7rp::pipeline::chart_input_row_limit() == 1024u
        && ff7rp::pipeline::chart_row_policy_identity()
            == "chart_rows=native512+diagnostic1024;extended=verified";
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    return enabled && !ff7rp::pipeline::experimental_extended_charts_requested();
}

bool test_sizes_and_final_onset()
{
    for (const std::size_t count : {511u, 512u}) {
        const auto rows = make_rows(count);
        Chart unchanged;
        unchanged.displayed_note_count = 17u;
        const Chart before = unchanged;
        BuildRequest request = extended_request(rows);
        request.native_prefix_rows = count;
        if (ff7r::piano::game::synthetic_model::build_transactionally(request, &unchanged)
            || !same_chart(unchanged, before)) {
            return false;
        }
    }
    for (const std::size_t count : {513u, 520u}) {
        const auto rows = make_rows(count);
        Chart chart;
        if (!ff7r::piano::game::synthetic_model::build_transactionally(extended_request(rows), &chart)
            || chart.times.size() != count || chart.events.size() != count
            || chart.times.back() != rows.back().time
            || chart.max_time != rows.back().time
            || chart.displayed_note_count != count || !chart.published) {
            return false;
        }
    }
    return true;
}

bool test_synthetic_copy_isolation()
{
    auto rows = make_rows(520);
    Chart chart;
    if (!ff7r::piano::game::synthetic_model::build_transactionally(extended_request(rows), &chart)
        || !ff7r::piano::game::synthetic_model::links_are_internal(chart)) {
        return false;
    }

    const auto find_event = [&chart](std::size_t row) {
        return std::find_if(chart.events.begin(), chart.events.end(), [row](const auto& event) {
            return event.source_row == row;
        });
    };
    const auto row513 = find_event(513);
    if (row513 == chart.events.end() || row513->owned_values != rows[513].owned_values) {
        return false;
    }
    rows[513].owned_values[0] = 999999u;
    return row513->owned_values[0] != rows[513].owned_values[0];
}

bool test_synthetic_boundary_semantics()
{
    auto rows = make_rows(520);
    for (std::size_t row = 508; row < rows.size(); ++row) {
        rows[row].camera_transition = static_cast<int>(row - 500u);
        rows[row].owned_values = {row, row * 10u, row * 100u};
    }
    rows[511].group = 41;
    rows[512].group = 41;
    rows[512].monotone = false;
    rows[512].chord = true;
    rows[513].group = 41;
    rows[513].chord = true;

    Chart chart;
    if (!ff7r::piano::game::synthetic_model::build_transactionally(extended_request(rows), &chart)
        || chart.times.size() != 520u || chart.events.size() != 521u
        || chart.displayed_note_count != 520u || chart.max_time != rows[519].time
        || !chart.published || !ff7r::piano::game::synthetic_model::links_are_internal(chart)) {
        return false;
    }

    std::vector<std::size_t> event_indices[12];
    for (std::size_t event = 0; event < chart.events.size(); ++event) {
        const auto& item = chart.events[event];
        if (item.source_row >= 508u && item.source_row <= 519u) {
            event_indices[item.source_row - 508u].push_back(event);
            if (item.camera_transition != rows[item.source_row].camera_transition
                || item.owned_values != rows[item.source_row].owned_values) {
                return false;
            }
        }
    }
    for (const auto& indices : event_indices) {
        if (indices.empty()) return false;
    }
    if (event_indices[4].size() != 1u || !chart.events[event_indices[4][0]].chord
        || event_indices[5].size() != 2u
        || chart.events[event_indices[5][0]].chord
        || !chart.events[event_indices[5][1]].chord) {
        return false;
    }

    const std::size_t root = event_indices[3][0];
    const std::size_t row512 = event_indices[4][0];
    const std::size_t row513_first = event_indices[5][0];
    const std::size_t row513_second = event_indices[5][1];
    return chart.events[row512].parent == root
        && chart.events[row513_first].parent == root
        && chart.events[row513_second].parent == root
        && chart.events[root].successor == row513_second
        && chart.events[row513_second].successor == row513_first
        && chart.events[row513_first].successor == row512;
}

bool test_rollback_and_guards()
{
    const auto rows = make_rows(520);
    Chart original;
    original.times = {1.0};
    original.displayed_note_count = 1u;
    original.published = true;
    const Chart baseline = original;
    for (const FailurePoint point : {FailurePoint::AfterTimeReserve,
             FailurePoint::AfterEventReserve, FailurePoint::BeforePublish}) {
        if (ff7r::piano::game::synthetic_model::build_transactionally(extended_request(rows, point), &original)
            || !same_chart(original, baseline)) {
            return false;
        }
    }
    for (std::size_t event = 0; event < 512u; ++event) {
        if (ff7r::piano::game::synthetic_model::build_transactionally(
                extended_request(rows, FailurePoint::AfterNativeEventConstruction, event), &original)
            || !same_chart(original, baseline)) {
            return false;
        }
    }
    for (std::size_t event = 0; event < 8u; ++event) {
        if (ff7r::piano::game::synthetic_model::build_transactionally(
                extended_request(rows, FailurePoint::AfterTailEventConstruction, event), &original)
            || !same_chart(original, baseline)) {
            return false;
        }
    }
    auto linked_rows = rows;
    for (SourceRow& row : linked_rows) row.group = 1u;
    for (std::size_t link = 0; link + 1u < linked_rows.size(); ++link) {
        if (ff7r::piano::game::synthetic_model::build_transactionally(
                extended_request(linked_rows, FailurePoint::AfterLink, link), &original)
            || !same_chart(original, baseline)) {
            return false;
        }
    }
    BuildRequest guarded = extended_request(rows);
    guarded.playback_active = true;
    if (ff7r::piano::game::synthetic_model::build_transactionally(guarded, &original)) {
        return false;
    }
    guarded = extended_request(rows);
    guarded.another_chart_active = true;
    if (ff7r::piano::game::synthetic_model::build_transactionally(guarded, &original)) {
        return false;
    }
    for (int guard = 0; guard < 3; ++guard) {
        guarded = extended_request(rows);
        if (guard == 0) guarded.experiment_enabled = false;
        if (guard == 1) guarded.persistent_caller = false;
        if (guard == 2) guarded.active_custom_descriptor = false;
        if (ff7r::piano::game::synthetic_model::build_transactionally(guarded, &original)
            || !same_chart(original, baseline)) {
            return false;
        }
    }
    auto nonmonotone = rows;
    nonmonotone[513].time = nonmonotone[512].time - 1.0;
    return !ff7r::piano::game::synthetic_model::build_transactionally(
        extended_request(nonmonotone), &original);
}

bool test_shipping_specs()
{
    using namespace ff7r::piano::game;
    // These checks exercise only a synthetic transaction algorithm. They prove no native ABI,
    // native ownership, runtime event construction, or playable >512 readiness.
    if (rva::PersistentChartExpandCaller == 0
        || kPersistentChartOwnerOffset == 0
        || kExtendedChartCanonicalSpecs.size() != 6u) {
        return false;
    }
    for (const ExtendedChartCanonicalSpec& canonical : kExtendedChartCanonicalSpecs) {
        const RvaSignatureSpec* spec = find_rva_signature(canonical.signature_id);
        if (!spec || spec->rva != canonical.rva || spec->expected_prologue.empty()) {
            return false;
        }
    }
    return true;
}

bool test_fixture_tool_contract()
{
    namespace fs = std::filesystem;
    ff7rp::tests::TemporaryDirectory root("ff7rp-extended-chart-tool-selftest");
    std::string error;
    const fs::path fresh = root.path() / "fresh";
    if (!ff7rp::tools::create_extended_chart_fixture(fresh, &error)) {
        std::cerr << "fresh fixture creation failed: " << error << '\n';
        return false;
    }
    std::ifstream json(fresh / "song.json", std::ios::binary);
    std::ostringstream text;
    text << json.rdbuf();
    json.close();
    const std::string json_text = text.str();
    std::size_t note_count = 0;
    for (std::size_t offset = 0; (offset = json_text.find("\"pitch\"", offset)) != std::string::npos;
         offset += 7) {
        ++note_count;
    }
    if (json_text.empty() || note_count != 520u
        || fs::file_size(fresh / "song.wav") != 44u + 48000u * 70u * 2u) {
        std::cerr << "fresh fixture validation failed: notes=" << note_count << '\n';
        return false;
    }
    const auto original_size = fs::file_size(fresh / "song.json");
    if (ff7rp::tools::create_extended_chart_fixture(fresh, &error)
        || fs::file_size(fresh / "song.json") != original_size) {
        std::cerr << "fixture non-overwrite check failed: " << error << '\n';
        return false;
    }

    const fs::path empty = root.path() / "empty";
    fs::create_directory(empty);
    if (!ff7rp::tools::create_extended_chart_fixture(empty, &error)
        || !fs::exists(empty / "song.json") || !fs::exists(empty / "song.wav")) {
        std::cerr << "existing-empty fixture creation failed: " << error << '\n';
        return false;
    }
    const fs::path file_destination = root.path() / "existing-file";
    {
        std::ofstream file(file_destination, std::ios::binary);
        file << "preserve";
    }
    if (ff7rp::tools::create_extended_chart_fixture(file_destination, &error)
        || fs::file_size(file_destination) != 8u) {
        std::cerr << "existing-file preservation failed: " << error << '\n';
        return false;
    }
    for (const auto failure : {
             ff7rp::tools::ExtendedChartFixtureFailure::after_json,
             ff7rp::tools::ExtendedChartFixtureFailure::after_wav,
             ff7rp::tools::ExtendedChartFixtureFailure::before_publish,
             ff7rp::tools::ExtendedChartFixtureFailure::after_destination_preserved,
             ff7rp::tools::ExtendedChartFixtureFailure::publish_rename,
             ff7rp::tools::ExtendedChartFixtureFailure::after_publish}) {
        const fs::path preserved_empty = root.path() /
            ("injected-" + std::to_string(static_cast<int>(failure)));
        fs::create_directory(preserved_empty);
        const bool unexpectedly_created =
            ff7rp::tools::create_extended_chart_fixture(preserved_empty, &error, failure);
        if (unexpectedly_created || !fs::is_directory(preserved_empty) || !fs::is_empty(preserved_empty)) {
            std::cerr << "injected fixture failure did not preserve empty destination: mode="
                      << static_cast<int>(failure) << " created=" << unexpectedly_created
                      << " error=" << error << '\n';
            return false;
        }
    }
    for (const auto failure : {
             ff7rp::tools::ExtendedChartFixtureFailure::rollback_output_cleanup_failure,
             ff7rp::tools::ExtendedChartFixtureFailure::rollback_destination_obstruction}) {
        const fs::path obstructed = root.path() /
            ("diagnostic-" + std::to_string(static_cast<int>(failure)));
        fs::create_directory(obstructed);
        error.clear();
        if (ff7rp::tools::create_extended_chart_fixture(obstructed, &error, failure)) {
            std::cerr << "fixture rollback diagnostic injection unexpectedly succeeded\n";
            return false;
        }
        std::vector<fs::path> previous_directories;
        const std::string previous_prefix = "." + obstructed.filename().string() + ".ff7rp-previous-";
        for (const fs::directory_entry& entry : fs::directory_iterator(root.path())) {
            if (entry.is_directory()
                && entry.path().filename().string().find(previous_prefix) == 0u) {
                previous_directories.push_back(entry.path());
            }
        }
        const std::string staging_prefix =
            (root.path() / ("." + obstructed.filename().string() + ".ff7rp-staging-")).string();
        if (previous_directories.size() != 1u
            || error.find(previous_directories.front().string()) == std::string::npos
            || error.find(obstructed.string()) == std::string::npos
            || error.find(staging_prefix) == std::string::npos) {
            std::cerr << "fixture rollback diagnostic omitted a recovery path: " << error << '\n';
            return false;
        }
        std::error_code cleanup_error;
        fs::remove_all(obstructed, cleanup_error);
        if (cleanup_error) {
            std::cerr << "fixture diagnostic obstruction cleanup failed: "
                      << cleanup_error.message() << '\n';
            return false;
        }
        fs::rename(previous_directories.front(), obstructed, cleanup_error);
        if (cleanup_error || !fs::is_empty(obstructed)) {
            std::cerr << "fixture diagnostic prior-directory restoration failed: "
                      << cleanup_error.message() << '\n';
            return false;
        }
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(root.path())) {
        const std::string name = entry.path().filename().string();
        if (name.find(".ff7rp-staging-") != std::string::npos
            || name.find(".ff7rp-previous-") != std::string::npos) {
            std::cerr << "fixture transaction leftover: " << entry.path().string() << '\n';
            return false;
        }
    }
    if (!root.cleanup(&error)) {
        std::cerr << "fixture root cleanup failed: " << error << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const struct Test {
        const char* name;
        bool (*run)();
    } tests[] = {
        {"default_policy", test_default_policy},
        {"synthetic_sizes_and_final_onset", test_sizes_and_final_onset},
        {"synthetic_copy_isolation", test_synthetic_copy_isolation},
        {"synthetic_boundary_semantics", test_synthetic_boundary_semantics},
        {"synthetic_rollback_and_guards", test_rollback_and_guards},
        {"shipping_specs", test_shipping_specs},
        {"fixture_tool_contract", test_fixture_tool_contract},
    };
    for (const Test& test : tests) {
        if (!test.run()) {
            std::cerr << "synthetic_extended_chart_selftest failed: " << test.name << '\n';
            return 1;
        }
    }
    std::cout << "synthetic_extended_chart_selftest passed; no native readiness claim\n";
    return 0;
}
