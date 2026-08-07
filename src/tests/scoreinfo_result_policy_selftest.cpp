#include "game/scoreinfo_result_policy.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

using namespace ff7r::piano::game;

void require(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "scoreinfo_result_policy_selftest: " << message << '\n';
        std::exit(1);
    }
}

ScoreInfoResultAuthorityIdentity valid_authority()
{
    ScoreInfoResultAuthorityIdentity authority{};
    authority.storage_identity = 0x1000;
    authority.song_identity = 0x2000;
    authority.profile_identity = 0x3000;
    authority.registry_generation = 11;
    authority.profile_index = 2;
    authority.token_registry_generation = 11;
    authority.token_route_generation = 21;
    authority.token_lease_generation = 31;
    authority.token_song_key = 41;
    authority.token_controller = 0x5000;
    authority.token_slot = 0x6000;
    authority.token_bgm = 0x7000;
    authority.token_sound = 0x8000;
    authority.token_request_handle = 0x1238;
    return authority;
}

ScoreInfoResultSourceIdentity valid_source()
{
    return {0x9000, 0xa000, 105425, 0};
}

ScoreInfoResultInvocationFacts valid_facts(const ScoreInfoResultCaller caller)
{
    ScoreInfoResultInvocationFacts facts{};
    facts.caller = caller;
    facts.original_called = true;
    facts.result_wrapper_exact = true;
    facts.wrapper_readable = true;
    facts.wrapper_discriminator_readable = true;
    facts.source_row_readable = true;
    facts.source_row_revalidated = true;
    facts.cleanup = ScoreInfoResultCleanupAuthority::Absent;
    facts.consumer_thread = 77;
    facts.detail_scope_exact = caller == ScoreInfoResultCaller::Detail;
    facts.source = valid_source();
    facts.authority = valid_authority();
    return facts;
}

ScoreInfoResultPolicyState style_state()
{
    auto facts = valid_facts(ScoreInfoResultCaller::StyleSetup);
    facts.playback_present = true;
    facts.next_generation = 1;
    const auto transition = begin_scoreinfo_result_authority({}, facts);
    require(transition.accepted && transition.publish
            && transition.state.phase == ScoreInfoResultPhase::StylePublished
            && transition.state.retained_row_count == 1,
        "style setup did not publish row one");
    return transition.state;
}

ScoreInfoResultPolicyState revoked_state()
{
    const auto transition = revoke_scoreinfo_result_playback(style_state(), valid_authority());
    require(transition.accepted
            && transition.state.phase == ScoreInfoResultPhase::PlaybackRevoked
            && transition.state.retained_row_count == 1,
        "playback revoke destroyed result authority");
    return transition.state;
}

ScoreInfoResultPolicyState detail_state()
{
    const auto transition = consume_scoreinfo_result_authority(
        revoked_state(), valid_facts(ScoreInfoResultCaller::Detail));
    require(transition.accepted && transition.publish
            && transition.state.phase == ScoreInfoResultPhase::DetailPublished
            && transition.state.consumer_thread == 77
            && transition.state.retained_row_count == 2,
        "detail did not publish row two or establish the consumer thread");
    return transition.state;
}

ScoreInfoResultPolicyState progress_state()
{
    const auto transition = consume_scoreinfo_result_authority(
        detail_state(), valid_facts(ScoreInfoResultCaller::ProgressSource));
    require(transition.accepted && !transition.publish
            && transition.state.phase == ScoreInfoResultPhase::ProgressObserved
            && transition.state.retained_row_count == 2,
        "progress source published a row or failed to advance");
    return transition.state;
}

ScoreInfoResultPolicyState rank_state()
{
    const auto transition = consume_scoreinfo_result_authority(
        progress_state(), valid_facts(ScoreInfoResultCaller::RankText));
    require(transition.accepted && transition.publish
            && transition.state.phase == ScoreInfoResultPhase::RankPublished
            && transition.state.retained_row_count == 3,
        "rank text did not publish row three");
    return transition.state;
}

void test_exact_sequence_and_lifetime()
{
    const std::array<int32_t, 4> custom_thresholds{0, 11100, 13400, 15800};
    const auto rank_index = [](const std::array<int32_t, 4>& thresholds, const int score) {
        int result = 0;
        for (int index = 1; index < 4; ++index) {
            if (score >= thresholds[static_cast<std::size_t>(index)]) result = index;
        }
        return result;
    };
    require(rank_index(custom_thresholds, 15000) == 2,
        "custom score 15000 did not project rank A");

    auto storage = std::make_shared<int>(1);
    std::array<std::shared_ptr<std::array<int32_t, 4>>, 4> rows{
        std::make_shared<std::array<int32_t, 4>>(custom_thresholds),
        std::make_shared<std::array<int32_t, 4>>(custom_thresholds),
        std::make_shared<std::array<int32_t, 4>>(custom_thresholds),
        std::make_shared<std::array<int32_t, 4>>(custom_thresholds),
    };
    std::weak_ptr<int> weak_storage = storage;
    std::weak_ptr<std::array<int32_t, 4>> weak_row = rows[0];
    auto runtime_storage = storage;
    auto runtime_rows = rows;

    auto transition = consume_scoreinfo_result_authority(
        rank_state(), valid_facts(ScoreInfoResultCaller::Thresholds));
    require(transition.accepted && transition.publish
            && transition.state.phase == ScoreInfoResultPhase::Closed
            && transition.state.retained_row_count == 4,
        "thresholds did not publish row four and close authority");

    storage.reset();
    rows = {};
    require(!weak_storage.expired() && !weak_row.expired(),
        "rows or immutable storage expired before list retirement");
    bool native_list_called = false;
    transition = retire_scoreinfo_result_authority(
        transition.state, ScoreInfoResultFailure::ListRetired);
    require(transition.state.phase == ScoreInfoResultPhase::Retired
            && transition.state.retained_row_count == 0 && !native_list_called,
        "list retirement did not precede native list processing");
    runtime_storage.reset();
    runtime_rows = {};
    native_list_called = true;
    require(native_list_called && weak_storage.expired() && weak_row.expired(),
        "list retirement retained row ownership");
}

void test_optional_revoke_detail_admission()
{
    auto revoked_detail = valid_facts(ScoreInfoResultCaller::Detail);
    const auto revoked = consume_scoreinfo_result_authority(
        revoked_state(), revoked_detail);
    require(revoked.accepted
            && revoked.state.phase == ScoreInfoResultPhase::DetailPublished
            && revoked.state.playback_released,
        "exact revoked detail path was rejected");

    auto live_detail = valid_facts(ScoreInfoResultCaller::Detail);
    live_detail.playback_present = true;
    live_detail.next_generation = 999;
    const auto live = consume_scoreinfo_result_authority(style_state(), live_detail);
    require(live.accepted && live.publish
            && live.state.phase == ScoreInfoResultPhase::DetailPublished
            && !live.state.playback_released,
        "exact live-playback detail path was rejected despite exact full token identity");

    auto different = live_detail;
    ++different.authority.token_request_handle;
    auto transition = consume_scoreinfo_result_authority(style_state(), different);
    require(!transition.accepted
            && transition.failure == ScoreInfoResultFailure::TokenMismatch
            && transition.state.phase == ScoreInfoResultPhase::Poisoned,
        "different live playback was admitted");

    auto missing = valid_facts(ScoreInfoResultCaller::Detail);
    transition = consume_scoreinfo_result_authority(style_state(), missing);
    require(!transition.accepted
            && transition.failure == ScoreInfoResultFailure::PlaybackUnavailable
            && transition.state.phase == ScoreInfoResultPhase::Poisoned,
        "missing playback without exact release evidence was admitted");

    auto selection_fallback = missing;
    selection_fallback.authority = valid_authority();
    transition = consume_scoreinfo_result_authority(style_state(), selection_fallback);
    require(!transition.accepted
            && transition.failure == ScoreInfoResultFailure::PlaybackUnavailable,
        "selection-only identity substituted for playback or release evidence");

    auto outside_scope = live_detail;
    outside_scope.detail_scope_exact = false;
    transition = consume_scoreinfo_result_authority(style_state(), outside_scope);
    require(!transition.accepted
            && transition.failure == ScoreInfoResultFailure::DetailScopeMismatch
            && transition.state.phase == ScoreInfoResultPhase::Poisoned,
        "detail outside exact TLS scope was admitted");

    auto live_progress_facts = valid_facts(ScoreInfoResultCaller::ProgressSource);
    live_progress_facts.playback_present = true;
    const auto live_progress = consume_scoreinfo_result_authority(
        live.state, live_progress_facts);
    require(live_progress.accepted
            && live_progress.state.phase == ScoreInfoResultPhase::ProgressObserved,
        "immediate live-playback progress transition was rejected");

    const auto late_release = revoke_scoreinfo_result_playback(
        live_progress.state, valid_authority());
    require(late_release.accepted && late_release.state.playback_released
            && late_release.state.phase == ScoreInfoResultPhase::ProgressObserved,
        "late exact revoke reset the consumer phase");
    const auto rank = consume_scoreinfo_result_authority(
        late_release.state, valid_facts(ScoreInfoResultCaller::RankText));
    require(rank.accepted && rank.state.phase == ScoreInfoResultPhase::RankPublished,
        "delayed consumer chain did not continue after late exact revoke");
}

void test_catalog_scope_classification()
{
    require(scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::StyleSetup, false)
            == ScoreInfoResultCaller::StyleSetup
            && scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::Detail, true)
            == ScoreInfoResultCaller::Detail
            && scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::Thresholds, false)
            == ScoreInfoResultCaller::Thresholds,
        "specific result consumers unexpectedly require detail scope");
    require(scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::Detail, false)
            == ScoreInfoResultCaller::Unavailable,
        "detail caller escaped exact piano-detail TLS scope");
    require(scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::ProgressSource, false)
            == ScoreInfoResultCaller::Unavailable
            && scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::RankText, false)
            == ScoreInfoResultCaller::Unavailable
            && scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::ProgressSource, true)
            == ScoreInfoResultCaller::ProgressSource
            && scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::RankText, true)
            == ScoreInfoResultCaller::RankText,
        "generic helper scope classification drifted");
    require(scoreinfo_result_caller_for_catalog_role(
                ScoreInfoResultCatalogRole::MenuDetail, true)
            == ScoreInfoResultCaller::Unavailable,
        "old menu-detail return entered result authority");

    const auto before = detail_state();
    ScoreInfoResultControlFlowFacts outside{};
    outside.callback_accepted = true;
    outside.caller = scoreinfo_result_caller_for_catalog_role(
        ScoreInfoResultCatalogRole::ProgressSource, false);
    outside.caller_in_image = true;
    outside.arg0_present = true;
    outside.outermost = true;
    require(scoreinfo_result_control_flow_reason(outside)
            == ScoreInfoResultControlFlowReason::CallerUnavailable
            && before.phase == ScoreInfoResultPhase::DetailPublished
            && before.retained_row_count == 2,
        "out-of-scope helper did not remain native without state mutation");
}

void test_fail_closed_order_and_drift()
{
    const auto detail = detail_state();
    const auto progress = progress_state();
    const auto rank = rank_state();

    const auto expect_failure = [](const ScoreInfoResultPolicyState& state,
                                   ScoreInfoResultInvocationFacts facts,
                                   const ScoreInfoResultFailure failure,
                                   const char* message) {
        const auto transition = consume_scoreinfo_result_authority(state, facts);
        require(!transition.accepted && transition.failure == failure
                && transition.state.phase == ScoreInfoResultPhase::Poisoned, message);
    };

    expect_failure(revoked_state(), valid_facts(ScoreInfoResultCaller::ProgressSource),
        ScoreInfoResultFailure::OutOfOrder, "progress skipped detail");
    expect_failure(detail, valid_facts(ScoreInfoResultCaller::RankText),
        ScoreInfoResultFailure::OutOfOrder, "rank skipped progress");
    expect_failure(progress, valid_facts(ScoreInfoResultCaller::Thresholds),
        ScoreInfoResultFailure::OutOfOrder, "thresholds skipped rank");
    expect_failure(detail, valid_facts(ScoreInfoResultCaller::Detail),
        ScoreInfoResultFailure::DuplicateCaller, "duplicate detail was accepted");
    expect_failure(progress, valid_facts(ScoreInfoResultCaller::ProgressSource),
        ScoreInfoResultFailure::DuplicateCaller, "duplicate progress was accepted");
    expect_failure(rank, valid_facts(ScoreInfoResultCaller::RankText),
        ScoreInfoResultFailure::DuplicateCaller, "duplicate rank was accepted");

    auto closed = consume_scoreinfo_result_authority(
        rank, valid_facts(ScoreInfoResultCaller::Thresholds)).state;
    expect_failure(closed, valid_facts(ScoreInfoResultCaller::Thresholds),
        ScoreInfoResultFailure::DuplicateCaller, "duplicate thresholds was accepted");

    for (const auto caller : {ScoreInfoResultCaller::ProgressSource,
                              ScoreInfoResultCaller::RankText,
                              ScoreInfoResultCaller::Thresholds}) {
        const auto state = caller == ScoreInfoResultCaller::ProgressSource ? detail
            : (caller == ScoreInfoResultCaller::RankText ? progress : rank);
        auto facts = valid_facts(caller);
        facts.consumer_thread = 78;
        expect_failure(state, facts, ScoreInfoResultFailure::ConsumerThreadMismatch,
            "late consumer thread drift was accepted");
    }

    const auto expect_late_drift = [&](const ScoreInfoResultPolicyState& state,
                                       const ScoreInfoResultCaller caller) {
        auto facts = valid_facts(caller);
        ++facts.source.bgm_number;
        expect_failure(state, facts, ScoreInfoResultFailure::SourceBgmMismatch,
            "late source drift was accepted");
        facts = valid_facts(caller);
        ++facts.authority.profile_identity;
        expect_failure(state, facts, ScoreInfoResultFailure::ProfileMismatch,
            "late profile drift was accepted");
        facts = valid_facts(caller);
        ++facts.authority.token_request_handle;
        expect_failure(state, facts, ScoreInfoResultFailure::TokenMismatch,
            "late token drift was accepted");
        facts = valid_facts(caller);
        facts.authority.storage_identity = 0x1010;
        expect_failure(state, facts, ScoreInfoResultFailure::StorageMismatch,
            "late storage drift was accepted");
    };
    expect_late_drift(revoked_state(), ScoreInfoResultCaller::Detail);
    expect_late_drift(detail, ScoreInfoResultCaller::ProgressSource);
    expect_late_drift(progress, ScoreInfoResultCaller::RankText);
    expect_late_drift(rank, ScoreInfoResultCaller::Thresholds);

    auto exact_cleanup = valid_facts(ScoreInfoResultCaller::Detail);
    exact_cleanup.cleanup = ScoreInfoResultCleanupAuthority::Exact;
    require(consume_scoreinfo_result_authority(revoked_state(), exact_cleanup).accepted,
        "exact cleanup authority was rejected");
    auto unrelated_cleanup = valid_facts(ScoreInfoResultCaller::Detail);
    unrelated_cleanup.cleanup = ScoreInfoResultCleanupAuthority::Unrelated;
    expect_failure(revoked_state(), unrelated_cleanup,
        ScoreInfoResultFailure::CleanupUnrelated,
        "unrelated cleanup authority was accepted");

    const auto expect_detail_drift = [&](auto mutate, const ScoreInfoResultFailure failure,
                                         const char* message) {
        auto facts = valid_facts(ScoreInfoResultCaller::Detail);
        mutate(facts);
        expect_failure(revoked_state(), facts, failure, message);
    };
    expect_detail_drift([](auto& f) { f.authority.storage_identity = 0x1010; },
        ScoreInfoResultFailure::StorageMismatch, "storage drift was accepted");
    expect_detail_drift([](auto& f) { ++f.authority.registry_generation; },
        ScoreInfoResultFailure::RegistryGenerationMismatch, "generation drift was accepted");
    expect_detail_drift([](auto& f) { ++f.authority.profile_identity; },
        ScoreInfoResultFailure::ProfileMismatch, "profile drift was accepted");
    expect_detail_drift([](auto& f) { ++f.authority.token_request_handle; },
        ScoreInfoResultFailure::TokenMismatch, "token drift was accepted");
    expect_detail_drift([](auto& f) { ++f.source.row_key; },
        ScoreInfoResultFailure::SourceKeyMismatch, "source drift was accepted");

    auto active_style = valid_facts(ScoreInfoResultCaller::StyleSetup);
    active_style.next_generation = 2;
    const auto supersession = begin_scoreinfo_result_authority(style_state(), active_style);
    require(!supersession.accepted
            && supersession.failure == ScoreInfoResultFailure::SupersessionBeforeClose
            && supersession.state.phase == ScoreInfoResultPhase::Poisoned,
        "active style supersession no longer poisons before playback checks");

    const std::array<ScoreInfoResultPolicyState, 4> active_states{
        style_state(), revoked_state(), detail_state(), progress_state()};
    for (const auto& state : active_states) {
        auto playback_missing = valid_facts(ScoreInfoResultCaller::StyleSetup);
        playback_missing.playback_present = false;
        const auto playback_transition =
            begin_scoreinfo_result_authority(state, playback_missing);
        require(!playback_transition.accepted
                && playback_transition.failure
                    == ScoreInfoResultFailure::SupersessionBeforeClose
                && playback_transition.state.phase == ScoreInfoResultPhase::Poisoned,
            "active style with missing playback did not poison before availability checks");

        auto authority_missing = valid_facts(ScoreInfoResultCaller::StyleSetup);
        authority_missing.playback_present = true;
        authority_missing.authority = {};
        const auto authority_transition =
            begin_scoreinfo_result_authority(state, authority_missing);
        require(!authority_transition.accepted
                && authority_transition.failure
                    == ScoreInfoResultFailure::SupersessionBeforeClose
                && authority_transition.state.phase == ScoreInfoResultPhase::Poisoned,
            "active style with missing authority did not poison before availability checks");
    }
}

void test_control_flow_diagnostic_projection()
{
    ScoreInfoResultControlFlowFacts facts{};
    require(scoreinfo_result_control_flow_reason(facts)
            == ScoreInfoResultControlFlowReason::CallbackRejected,
        "callback rejection ordering drifted");
    facts.callback_accepted = true;
    facts.caller_in_image = true;
    require(scoreinfo_result_control_flow_reason(facts)
            == ScoreInfoResultControlFlowReason::CallerUnavailable,
        "unknown in-image caller was not projected");
    facts.caller = ScoreInfoResultCaller::ProgressSource;
    require(scoreinfo_result_control_flow_reason(facts)
            == ScoreInfoResultControlFlowReason::Arg0Null,
        "null wrapper ordering drifted");
    facts.arg0_present = true;
    require(scoreinfo_result_control_flow_reason(facts)
            == ScoreInfoResultControlFlowReason::NonOutermost,
        "recursion ordering drifted");
    facts.outermost = true;
    require(scoreinfo_result_control_flow_reason(facts)
            == ScoreInfoResultControlFlowReason::PolicyDispatched
            && scoreinfo_result_control_flow_diagnostic_allowed(
                ScoreInfoResultPhase::ProgressObserved, facts, 0)
            && scoreinfo_result_control_flow_diagnostic_allowed(
                ScoreInfoResultPhase::ProgressObserved, facts, 7)
            && !scoreinfo_result_control_flow_diagnostic_allowed(
                ScoreInfoResultPhase::ProgressObserved, facts, 8),
        "bounded active-generation diagnostic projection drifted");
}

} // namespace

int main()
{
    test_exact_sequence_and_lifetime();
    test_optional_revoke_detail_admission();
    test_catalog_scope_classification();
    test_fail_closed_order_and_drift();
    test_control_flow_diagnostic_projection();
    std::cout << "scoreinfo_result_policy_selftest: ok\n";
    return 0;
}
