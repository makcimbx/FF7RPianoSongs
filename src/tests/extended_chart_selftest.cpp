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

namespace ff7r::piano::game {
bool chart_patch_ignore_sound_selftest();
}

namespace {

using ff7r::piano::game::synthetic_model::BuildRequest;
using ff7r::piano::game::synthetic_model::Chart;
using ff7r::piano::game::synthetic_model::CommitIdentity;
using ff7r::piano::game::synthetic_model::FailurePoint;
using ff7r::piano::game::synthetic_model::PublicationState;
using ff7r::piano::game::synthetic_model::SourceRow;

std::vector<SourceRow> make_rows(std::size_t count)
{
    std::vector<SourceRow> rows(count);
    for (std::size_t i = 0; i < rows.size(); ++i) rows[i].time = static_cast<float>(i) * 0.125f;
    return rows;
}

BuildRequest extended_request(const std::vector<SourceRow>& rows, FailurePoint failure = FailurePoint::None)
{
    BuildRequest request;
    request.rows = &rows;
    request.failure = failure;
    return request;
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
    ff7rp::pipeline::configure_chart_row_limit(true, true, true);
    if (ff7rp::pipeline::effective_chart_row_limit() != 512u
        || ff7rp::pipeline::chart_row_policy_snapshot().publication_limit != 512u
        || !ff7rp::pipeline::chart_row_policy_snapshot().playable_513_available) return false;
    ff7rp::pipeline::configure_chart_row_limit(false, false);
    return enabled && !ff7rp::pipeline::experimental_extended_charts_requested();
}

bool test_exact_success_and_lifecycle()
{
    const auto rows = make_rows(513); Chart chart;
    const auto result = ff7r::piano::game::synthetic_model::run(extended_request(rows), chart);
    if (!result.substituted || result.forwarded || !result.prefix_validated || !result.tail_constructed
        || !result.callback_validated || !result.count_committed || chart.count != 513
        || chart.capacity < 513 || chart.storage[512].ordinal != 1024
        || chart.max_time != rows.back().time) return false;
    ff7r::piano::game::synthetic_model::model_next_parser_reset(chart);
    return chart.count == 0 && chart.storage.empty();
}

bool test_forwarding_and_restrictions()
{
    auto rows = make_rows(513); Chart chart;
    for (int guard = 0; guard < 8; ++guard) {
        auto request = extended_request(rows);
        if (guard == 0) request.experiment_enabled = false;
        if (guard == 1) request.verified_1005 = false;
        if (guard == 2) request.exact_caller = false;
        if (guard == 3) request.exact_header = false;
        if (guard == 4) request.exact_thread = false;
        if (guard == 5) request.depth_one = false;
        if (guard == 6) request.exact_generation = false;
        if (guard == 7) request.global_claim = false;
        const auto result = ff7r::piano::game::synthetic_model::run(request, chart);
        if (!result.forwarded || result.substituted) return false;
    }
    for (int invalid = 0; invalid < 8; ++invalid) {
        rows = make_rows(513);
        if (invalid == 0) rows.back().monotone = false;
        if (invalid == 1) rows.back().chord = true;
        if (invalid == 2) rows.back().group = 1;
        if (invalid == 3) rows.back().camera_transition = 1;
        if (invalid == 4) rows.back().ignore_sound = true;
        if (invalid == 5) rows.back().strength = 1;
        if (invalid == 6) rows.back().lookup_path = 0;
        if (invalid == 7) rows.back().assignment = 7;
        if (!ff7r::piano::game::synthetic_model::run(extended_request(rows), chart).forwarded) return false;
    }
    rows = make_rows(520);
    if (!ff7r::piano::game::synthetic_model::run(extended_request(rows), chart).forwarded) return false;
    rows = make_rows(513);
    auto second_hit = extended_request(rows);
    second_hit.second_reserve_hit = true;
    const auto result = ff7r::piano::game::synthetic_model::run(second_hit, chart);
    auto excessive_capacity = extended_request(rows);
    excessive_capacity.reserve_capacity = 1025;
    const auto capacity_result = ff7r::piano::game::synthetic_model::run(excessive_capacity, chart);
    if (!(result.substituted && result.forwarded && !result.count_committed
            && capacity_result.substituted && !capacity_result.count_committed)) return false;

    // The synchronous controller is deliberately modeled as a non-UObject. Only
    // safe capture/reciprocal/header relations authorize mutation.
    for (int failure = 0; failure < 7; ++failure) {
        auto request = extended_request(rows);
        if (failure == 0) request.controller_capture_read = false;
        if (failure == 1) request.controller_nonnull = false;
        if (failure == 2) request.control_block_nonnull = false;
        if (failure == 3) request.reciprocal_pre = false;
        if (failure == 4) request.header_pre = false;
        if (failure == 5) request.reciprocal_reserve = false;
        if (failure == 6) request.header_reserve = false;
        Chart rejected;
        const auto rejected_result = ff7r::piano::game::synthetic_model::run(request, rejected);
        if (!rejected_result.forwarded || rejected_result.count_committed
            || rejected.tail_constructor_entered) return false;
    }
    for (int failure = 0; failure < 2; ++failure) {
        auto request = extended_request(rows);
        if (failure == 0) request.reciprocal_post = false;
        if (failure == 1) request.header_post = false;
        Chart rejected;
        const auto rejected_result = ff7r::piano::game::synthetic_model::run(request, rejected);
        if (!rejected_result.substituted || rejected_result.count_committed
            || rejected.tail_constructor_entered || rejected.count != 512) return false;
    }
    return true;
}

bool test_failure_cleanup_and_drift()
{
    const auto rows = make_rows(513);
    for (const FailurePoint point : {FailurePoint::PrefixValidation, FailurePoint::FNameFind}) {
        Chart chart; const auto result = ff7r::piano::game::synthetic_model::run(extended_request(rows, point), chart);
        if (result.count_committed || chart.tail_constructor_entered || chart.tail_destructed
            || chart.count != 512 || chart.storage.size() != 512 || chart.capacity < 513) return false;
    }
    Chart non_tail;
    const auto non_tail_result = ff7r::piano::game::synthetic_model::run(
        extended_request(rows, FailurePoint::ConstructorReturnedNonTail), non_tail);
    if (non_tail_result.count_committed || non_tail_result.constructor_returned_tail
        || !non_tail_result.route_blocked || non_tail.tail_destructed
        || !non_tail.ownership_preserved || non_tail.storage.size() != 513) return false;
    for (const FailurePoint point : {FailurePoint::ConstructorValidation,
             FailurePoint::CallbackBuild, FailurePoint::CallbackValidation}) {
        Chart chart; const auto result = ff7r::piano::game::synthetic_model::run(extended_request(rows, point), chart);
        if (result.count_committed || !result.constructor_returned_tail || !chart.tail_destructed
            || chart.count != 512 || chart.storage.size() != 512) return false;
    }
    Chart rolled; const auto rollback = ff7r::piano::game::synthetic_model::run(
        extended_request(rows, FailurePoint::PostCountValidation), rolled);
    if (!rollback.rolled_back || rolled.count != 512 || !rolled.tail_destructed) return false;
    if (!rollback.count_restore_proved || !rollback.max_restore_proved || rollback.route_blocked) return false;
    Chart count_restore_failed; const auto failed_restore = ff7r::piano::game::synthetic_model::run(
        extended_request(rows, FailurePoint::PostCountRestoreFailure), count_restore_failed);
    if (!failed_restore.count_committed || !failed_restore.route_blocked
        || count_restore_failed.tail_destructed || !count_restore_failed.ownership_preserved) return false;
    for (const FailurePoint point : {FailurePoint::CallbackCleanupIdentityFailure}) {
        Chart identity_lost;
        const auto result = ff7r::piano::game::synthetic_model::run(
            extended_request(rows, point), identity_lost);
        if (result.count_committed || !result.route_blocked || identity_lost.tail_destructed
            || !identity_lost.ownership_preserved || identity_lost.count != 512) return false;
    }
    for (const FailurePoint point : {FailurePoint::PreWriteMaxReadFailure,
             FailurePoint::PreWriteMaxDrift}) {
        Chart prewrite;
        const auto result = ff7r::piano::game::synthetic_model::run(
            extended_request(rows, point), prewrite);
        if (result.count_committed || !result.route_blocked || prewrite.tail_destructed
            || !prewrite.ownership_preserved || prewrite.max_write_count != 0
            || prewrite.count != 512 || prewrite.storage.size() != 513) return false;
    }
    for (const FailurePoint point : {FailurePoint::PostWriteMaxReadFailure,
             FailurePoint::PostWriteMaxMismatch}) {
        Chart postwrite;
        const auto result = ff7r::piano::game::synthetic_model::run(
            extended_request(rows, point), postwrite);
        if (result.count_committed || !result.route_blocked || postwrite.tail_destructed
            || !postwrite.ownership_preserved || postwrite.max_write_count != 1
            || postwrite.count != 512 || postwrite.storage.size() != 513) return false;
    }
    Chart drift; const auto drift_result = ff7r::piano::game::synthetic_model::run(
        extended_request(rows, FailurePoint::PostCountExternalDrift), drift);
    return drift_result.count_committed && drift_result.route_blocked
        && drift.ownership_preserved && !drift.tail_destructed;
}

bool test_committed_publication_state()
{
    using namespace ff7r::piano::game::synthetic_model;
    const auto rows = make_rows(513);
    const CommitIdentity identity{};
    PublicationState state;
    Chart chart;
    const Result success = run(extended_request(rows), chart);
    if (model_published_count(state, identity, true) != 512) return false;
    model_publish_result(success, identity, state);
    if (model_published_count(state, identity, true, false) != 512
        || !state.pending || state.active) return false;
    if (model_published_count(state, identity, true) != 513
        || state.pending || !state.active) return false;

    auto reserve_unavailable = extended_request(rows);
    reserve_unavailable.verified_1005 = false;
    Chart unavailable_chart;
    model_publish_result(run(reserve_unavailable, unavailable_chart), identity, state);
    if (model_published_count(state, identity, true) != 512) return false;
    auto excessive_capacity = extended_request(rows);
    excessive_capacity.reserve_capacity = 1025;
    Chart capacity_chart;
    model_publish_result(run(excessive_capacity, capacity_chart), identity, state);
    if (model_published_count(state, identity, true) != 512) return false;

    model_publish_result(success, identity, state);

    CommitIdentity mismatch = identity;
    mismatch.selection_generation++;
    if (model_published_count(state, mismatch, true) != 512
        || model_published_count(state, identity, false) != 512) return false;
    model_publish_result(success, identity, state);
    mismatch = identity;
    mismatch.route_generation++;
    if (model_published_count(state, mismatch, true) != 512) return false;

    model_publish_result(success, identity, state);
    if (model_presentation_published_count(state, identity, &identity, true) != 513) return false;
    model_publish_result(success, identity, state);
    if (model_presentation_published_count(state, identity, nullptr, true) != 512
        || !state.pending || state.active) return false;
    for (auto mutate : {1, 2, 3}) {
        model_publish_result(success, identity, state);
        CommitIdentity changed_playback = identity;
        if (mutate == 1) ++changed_playback.route_generation;
        if (mutate == 2) ++changed_playback.lease_generation;
        if (mutate == 3) ++changed_playback.song_key;
        if (model_presentation_published_count(
                state, identity, &changed_playback, true) != 512
            || state.pending || state.active) return false;
    }

    model_publish_result(success, identity, state);
    CommitIdentity changed_activation = identity;
    ++changed_activation.activation_generation;
    if (model_published_count(state, changed_activation, true) != 512
        || state.pending || state.active) return false;

    model_publish_result(success, identity, state);
    model_activation_abort(state);
    if (model_published_count(state, identity, true) != 512) return false;

    for (const FailurePoint point : {FailurePoint::PrefixValidation, FailurePoint::FNameFind,
             FailurePoint::ConstructorReturnedNonTail, FailurePoint::CallbackValidation,
             FailurePoint::CallbackCleanupIdentityFailure,
             FailurePoint::PreWriteMaxReadFailure, FailurePoint::PreWriteMaxDrift,
             FailurePoint::PostWriteMaxReadFailure, FailurePoint::PostWriteMaxMismatch,
             FailurePoint::PostCountValidation,
             FailurePoint::PostCountRestoreFailure, FailurePoint::PostCountExternalDrift}) {
        Chart failed_chart;
        const Result failed = run(extended_request(rows, point), failed_chart);
        model_publish_result(failed, identity, state);
        if (model_published_count(state, identity, true) != 512) return false;
    }

    model_publish_result(success, identity, state);
    model_expansion_begin(state);
    if (model_published_count(state, identity, true) != 512) return false;
    model_publish_result(success, identity, state);
    model_shutdown(state);
    return model_published_count(state, identity, true) == 512;
}

bool test_terminal_publication_order()
{
    using namespace ff7r::piano::game::synthetic_model;
    const auto rows = make_rows(513);
    const CommitIdentity identity{};

    // A failed terminal is authoritative even if it wins before transaction
    // registration: no reserve call or native mutation may begin for N.
    PublicationState before_state;
    model_terminal(before_state, identity.activation_generation,
        identity.route_lifecycle_epoch, TerminalOutcome::AudioFailed);
    Chart before_begin_chart;
    if (model_transaction_begin(before_state, identity)
        || before_begin_chart.count != 0 || !before_begin_chart.storage.empty()) return false;

    // Failure between begin and publication rejects the native commit and uses
    // the already-proven synchronous rollback path.
    PublicationState between_state;
    if (!model_transaction_begin(between_state, identity)) return false;
    Chart before_chart;
    Result before = run(extended_request(rows), before_chart);
    model_terminal(between_state, identity.activation_generation,
        identity.route_lifecycle_epoch, TerminalOutcome::AudioFailed);
    if (model_publish_result(before, before_chart, identity, between_state)
        || !before.rolled_back || before_chart.count != 512
        || !before_chart.tail_destructed) return false;

    for (const TerminalOutcome outcome : {TerminalOutcome::AudioFailed,
             TerminalOutcome::StopFailed, TerminalOutcome::Superseded,
             TerminalOutcome::ListExit}) {
        Chart after_chart;
        Result after = run(extended_request(rows), after_chart);
        PublicationState after_state;
        if (!model_publish_result(after, after_chart, identity, after_state)) return false;
        model_terminal(after_state, identity.activation_generation,
            identity.route_lifecycle_epoch, outcome);
        if (after_state.pending || after_state.active) return false;
    }

    PublicationState ordered;
    model_terminal(ordered, identity.activation_generation,
        identity.route_lifecycle_epoch, TerminalOutcome::AudioFailed);
    CommitIdentity next = identity;
    ++next.activation_generation;
    ++next.preparation_ordinal;
    if (!model_transaction_begin(ordered, next)) return false;
    Chart next_chart;
    Result next_result = run(extended_request(rows), next_chart);
    if (!model_publish_result(next_result, next_chart, next, ordered)) return false;
    model_terminal(ordered, identity.activation_generation,
        identity.route_lifecycle_epoch, TerminalOutcome::ListExit);
    if (!ordered.pending) return false;

    PublicationState success_terminal;
    model_terminal(success_terminal, identity.activation_generation,
        identity.route_lifecycle_epoch, TerminalOutcome::ExpandFinished);
    model_terminal(success_terminal, identity.activation_generation,
        identity.route_lifecycle_epoch, TerminalOutcome::AudioPublished);
    if (!model_transaction_begin(success_terminal, identity)) return false;
    Chart success_chart;
    Result success = run(extended_request(rows), success_chart);
    if (!model_publish_result(success, success_chart, identity, success_terminal)
        || !success_terminal.pending) return false;

    model_terminal(success_terminal, identity.activation_generation,
        identity.route_lifecycle_epoch, TerminalOutcome::StopFailed);
    model_configuration_reset(success_terminal);
    if (!model_transaction_begin(success_terminal, identity)) return false;
    model_terminal(success_terminal, identity.activation_generation,
        identity.route_lifecycle_epoch, TerminalOutcome::StopFailed);
    model_shutdown(success_terminal);
    return model_transaction_begin(success_terminal, identity)
        && !success_terminal.pending && !success_terminal.active;
}

bool test_shipping_specs()
{
    using namespace ff7r::piano::game;
    // These checks exercise only a synthetic transaction algorithm. They prove no native ABI,
    // native ownership, runtime event construction, or playable >512 readiness.
    // `persistent_chart_expand_caller` and `piano_score_expand` are release entries, so the
    // catalog requires them in every build and they must never render the absent sentinel.
    if (rva::PersistentChartExpandCaller == 0
        || rva::PianoScoreExpand == 0
        || kPersistentChartOwnerOffset == 0
        || kExtendedChartCanonicalSpecs.size() != 6u
        || rva::PianoEventVectorReserve == 0 || rva::PianoEventVectorReserveCall == 0
        || rva::PianoEventCallbackBuild == 0 || rva::PianoEventResultCallback == 0
        || rva::PianoEventCallbackVtable == 0) {
        return false;
    }
    // The remaining canonical specs are research entries. A build that does not catalog one
    // renders `0x0` and emits no signature line at all, which is what
    // `configure_extended_chart_experiment` already degrades on. Require that absence to be
    // consistent on both sides instead of asserting presence unconditionally.
    for (const ExtendedChartCanonicalSpec& canonical : kExtendedChartCanonicalSpecs) {
        const RvaSignatureSpec* spec = find_rva_signature(canonical.signature_id);
        if (canonical.rva == 0) {
            if (spec) return false;
            continue;
        }
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
        {"exact_success_and_lifecycle", test_exact_success_and_lifecycle},
        {"forwarding_and_restrictions", test_forwarding_and_restrictions},
        {"failure_cleanup_and_drift", test_failure_cleanup_and_drift},
        {"committed_publication_state", test_committed_publication_state},
        {"terminal_publication_order", test_terminal_publication_order},
        {"chart_patch_ignore_sound", ff7r::piano::game::chart_patch_ignore_sound_selftest},
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
