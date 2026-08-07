#include "game/selection_audio_policy.h"
#include "game/canonical_substrate_policy.h"
#include "game/audio_sead.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "selection_substrate_policy_selftest: " << message << '\n';
        std::exit(1);
    }
}

void test_selection_substrate_policy()
{
    using namespace ff7r::piano::game;

    constexpr uintptr_t primary_action_return = 0x0399f3f9;
    int primary_matches = 0;
    for (uintptr_t index = 0; index < 245; ++index) {
        const uintptr_t observed = index == 117
            ? primary_action_return : 0x04000000 + index;
        if (selection_activation_primary_return_exact(
                observed, primary_action_return)) {
            ++primary_matches;
        }
    }
    require(primary_matches == 1
            && !selection_activation_primary_return_exact(
                0x0399f456, primary_action_return)
            && !selection_activation_primary_return_exact(
                0x0399f473, primary_action_return),
        "245-xref action query filter admitted a secondary or unrelated caller");
    int action_original_calls = 0;
    int action_false_edges = 0;
    int action_primary_calls = 0;
    require(!selection_activation_query_exact_once(
                [&]() { ++action_original_calls; return false; },
                true, true,
                [&]() { ++action_false_edges; },
                [&]() { ++action_primary_calls; return true; })
            && action_original_calls == 1 && action_false_edges == 1
            && action_primary_calls == 0,
        "native false did not discharge action wrapper exactly once");
    action_original_calls = 0;
    const int false_edges_before_secondary = action_false_edges;
    require(!selection_activation_query_exact_once(
                [&]() { ++action_original_calls; return false; },
                true, false,
                [&]() { /* secondary false remains side-effect free */ },
                [&]() { ++action_primary_calls; return false; })
            && action_original_calls == 1
            && action_false_edges == false_edges_before_secondary,
        "secondary false query changed activation denial state");
    action_original_calls = 0;
    require(selection_activation_query_exact_once(
                [&]() { ++action_original_calls; return true; },
                true, false,
                [&]() { ++action_false_edges; },
                [&]() { ++action_primary_calls; return false; })
            && action_original_calls == 1 && action_primary_calls == 0,
        "secondary action query caller was not returned unchanged");
    action_original_calls = 0;
    require(!selection_activation_query_exact_once(
                [&]() { ++action_original_calls; return true; },
                true, true,
                [&]() { ++action_false_edges; },
                [&]() { ++action_primary_calls; return false; })
            && action_original_calls == 1 && action_primary_calls == 1,
        "primary admission denial did not call original exactly once");
    require(!selection_activation_query_result({false, true, true, false, true})
            && selection_activation_query_result({true, false, true, true, false})
            && !selection_activation_query_result({true, true, true, true, true})
            && selection_activation_query_result({true, true, true, false, true}),
        "activation denial latch/native-result edge policy drifted");
    SelectionActivationReservationMachine activation_reservation;
    require(activation_reservation.reserve(),
        "activation reservation could not acquire initial generation");
    const uint64_t activation_generation = activation_reservation.generation();
    require(activation_generation != 0
            && !activation_reservation.consume(activation_generation + 1)
            && activation_reservation.consume(activation_generation)
            && !activation_reservation.consume(activation_generation),
        "activation reservation was not exact single-consume/ABA safe");
    require(!activation_reservation.reserve()
            && !activation_reservation.retire_consumed(
                activation_generation + 1)
            && activation_reservation.retire_consumed(activation_generation)
            && !activation_reservation.retire_consumed(activation_generation)
            && activation_reservation.reserve()
            && activation_reservation.generation() == activation_generation + 1,
        "consumed activation was superseded or not retired exactly");
    activation_reservation.revoke();
    require(activation_reservation.state()
            == SelectionActivationReservationState::Revoked
            && !activation_reservation.consume(
                activation_reservation.generation()),
        "selection/list/shutdown revocation did not close reservation");
    SelectionActivationReservationMachine exhausted_activation(UINT64_MAX);
    require(!exhausted_activation.reserve(),
        "activation reservation generation wrapped");
    constexpr uintptr_t activation_getter_return = 0x0399f420;
    require(selection_activation_getter_return_exact(
                activation_getter_return, activation_getter_return)
            && !selection_activation_getter_return_exact(
                0x0399f41b, activation_getter_return)
            && !selection_activation_getter_return_exact(
                0x039a1210, activation_getter_return),
        "activation handoff admitted a call instruction or unrelated getter");
    SelectionActivationHandoffFacts handoff_facts{
        true, true, true, true, true,
        15, 16,
        true, true, true, true, true,
    };
    require(selection_activation_handoff_first_failure(handoff_facts)
            == SelectionActivationHandoffFailure::None,
        "exact query generation 15 -> activation getter generation 16 handoff failed");
    auto reject_handoff = [&](bool SelectionActivationHandoffFacts::*member,
                              SelectionActivationHandoffFailure expected,
                              const char* reason) {
        auto drifted = handoff_facts;
        drifted.*member = false;
        require(selection_activation_handoff_first_failure(drifted) == expected,
            reason);
    };
    reject_handoff(&SelectionActivationHandoffFacts::pending_reservation,
        SelectionActivationHandoffFailure::NoPendingReservation,
        "activation getter accepted no pending primary reservation");
    reject_handoff(&SelectionActivationHandoffFacts::handoff_unconfirmed,
        SelectionActivationHandoffFailure::AlreadyConfirmed,
        "activation getter accepted duplicate publication");
    reject_handoff(&SelectionActivationHandoffFacts::reservation_generation_exact,
        SelectionActivationHandoffFailure::ReservationGenerationDrift,
        "activation getter accepted reservation ABA");
    reject_handoff(&SelectionActivationHandoffFacts::context_exact,
        SelectionActivationHandoffFailure::ContextDrift,
        "activation getter accepted list-context drift");
    reject_handoff(&SelectionActivationHandoffFacts::prior_selection_exact,
        SelectionActivationHandoffFailure::PriorSelectionDrift,
        "activation getter accepted prior selection drift");
    reject_handoff(&SelectionActivationHandoffFacts::storage_exact,
        SelectionActivationHandoffFailure::StorageDrift,
        "activation getter accepted storage drift");
    reject_handoff(&SelectionActivationHandoffFacts::song_exact,
        SelectionActivationHandoffFailure::SongDrift,
        "activation getter accepted song drift");
    reject_handoff(&SelectionActivationHandoffFacts::profile_exact,
        SelectionActivationHandoffFailure::ProfileDrift,
        "activation getter accepted profile drift");
    reject_handoff(&SelectionActivationHandoffFacts::visible_index_exact,
        SelectionActivationHandoffFailure::VisibleIndexDrift,
        "activation getter accepted visible-index drift");
    reject_handoff(&SelectionActivationHandoffFacts::base_slot_exact,
        SelectionActivationHandoffFailure::BaseSlotDrift,
        "activation getter accepted base-slot drift");
    auto same_generation_handoff = handoff_facts;
    same_generation_handoff.published_generation = 15;
    require(selection_activation_handoff_first_failure(same_generation_handoff)
            == SelectionActivationHandoffFailure::SameGeneration,
        "activation getter accepted same-generation publication");
    auto skipped_generation_handoff = handoff_facts;
    skipped_generation_handoff.published_generation = 17;
    require(selection_activation_handoff_first_failure(skipped_generation_handoff)
            == SelectionActivationHandoffFailure::SkippedGeneration,
        "activation getter accepted skipped generation");
    auto wrapped_generation_handoff = handoff_facts;
    wrapped_generation_handoff.prior_generation = UINT64_MAX;
    wrapped_generation_handoff.published_generation = 0;
    require(selection_activation_handoff_first_failure(wrapped_generation_handoff)
            == SelectionActivationHandoffFailure::GenerationWrap,
        "activation getter accepted generation wrap");
    ChartAudioAdmissionFacts chart_admission{
        true, true, true, true, true, true, true, true, true,
    };
    require(chart_audio_admission_exact(chart_admission),
        "exact chart/audio admission was rejected");
    auto reject_chart_drift = [&](bool ChartAudioAdmissionFacts::*member,
                                  const char* reason) {
        auto drifted = chart_admission;
        drifted.*member = false;
        require(!chart_audio_admission_exact(drifted), reason);
    };
    reject_chart_drift(&ChartAudioAdmissionFacts::chart_plan_complete,
        "admission accepted an incomplete chart plan");
    reject_chart_drift(&ChartAudioAdmissionFacts::callback_and_exit_lease,
        "admission accepted exit beginning after lease acquisition");
    reject_chart_drift(&ChartAudioAdmissionFacts::selection_exact,
        "admission accepted selection/song drift");
    reject_chart_drift(&ChartAudioAdmissionFacts::sidecar_exact,
        "admission accepted sidecar drift");
    reject_chart_drift(&ChartAudioAdmissionFacts::controller_exact,
        "admission accepted controller drift");
    reject_chart_drift(&ChartAudioAdmissionFacts::route_exact_and_idle,
        "admission accepted route drift");
    reject_chart_drift(&ChartAudioAdmissionFacts::frozen_and_cleanup_clear,
        "admission accepted frozen/cleanup ownership");
    reject_chart_drift(&ChartAudioAdmissionFacts::lifecycle_allows_arm,
        "admission accepted lifecycle drift");
    reject_chart_drift(&ChartAudioAdmissionFacts::exit_and_unresolved_clear,
        "admission accepted unresolved exit ownership");
    const ChartAudioAdmissionFailure chart_admission_failures[] = {
        ChartAudioAdmissionFailure::ChartPlanIncomplete,
        ChartAudioAdmissionFailure::CallbackOrExitLeaseMissing,
        ChartAudioAdmissionFailure::SelectionDrift,
        ChartAudioAdmissionFailure::SidecarDrift,
        ChartAudioAdmissionFailure::ControllerDrift,
        ChartAudioAdmissionFailure::RouteNotExactIdle,
        ChartAudioAdmissionFailure::FrozenOrCleanupActive,
        ChartAudioAdmissionFailure::LifecycleArmRejected,
        ChartAudioAdmissionFailure::ExitOrUnresolvedActive,
    };
    bool ChartAudioAdmissionFacts::* const chart_admission_fields[] = {
        &ChartAudioAdmissionFacts::chart_plan_complete,
        &ChartAudioAdmissionFacts::callback_and_exit_lease,
        &ChartAudioAdmissionFacts::selection_exact,
        &ChartAudioAdmissionFacts::sidecar_exact,
        &ChartAudioAdmissionFacts::controller_exact,
        &ChartAudioAdmissionFacts::route_exact_and_idle,
        &ChartAudioAdmissionFacts::frozen_and_cleanup_clear,
        &ChartAudioAdmissionFacts::lifecycle_allows_arm,
        &ChartAudioAdmissionFacts::exit_and_unresolved_clear,
    };
    static_assert(std::size(chart_admission_fields)
        == std::size(chart_admission_failures));
    require(first_chart_audio_admission_failure(chart_admission)
            == ChartAudioAdmissionFailure::None,
        "exact chart admission reported a failure leaf");
    for (size_t index = 0; index < std::size(chart_admission_fields); ++index) {
        auto rejected = chart_admission;
        rejected.*chart_admission_fields[index] = false;
        require(first_chart_audio_admission_failure(rejected)
                == chart_admission_failures[index],
            "chart admission reported the wrong first failure leaf");
    }

    SelectionAdmissionSubstrateLiveFacts substrate_live{
        true, true, true, true, true, true, true, true, true, true,
        true, true, true, true, true, true, true,
    };
    bool SelectionAdmissionSubstrateLiveFacts::* const substrate_fields[] = {
        &SelectionAdmissionSubstrateLiveFacts::proof_active,
        &SelectionAdmissionSubstrateLiveFacts::proof_state_exact,
        &SelectionAdmissionSubstrateLiveFacts::transaction_nonzero,
        &SelectionAdmissionSubstrateLiveFacts::controller_nonnull,
        &SelectionAdmissionSubstrateLiveFacts::controller_pointer_exact,
        &SelectionAdmissionSubstrateLiveFacts::controller_identity_exact,
        &SelectionAdmissionSubstrateLiveFacts::chain_read,
        &SelectionAdmissionSubstrateLiveFacts::sound_read,
        &SelectionAdmissionSubstrateLiveFacts::request_read,
        &SelectionAdmissionSubstrateLiveFacts::state_read,
        &SelectionAdmissionSubstrateLiveFacts::sound_nonnull,
        &SelectionAdmissionSubstrateLiveFacts::sound_identity_read,
        &SelectionAdmissionSubstrateLiveFacts::slot_bgm_exact,
        &SelectionAdmissionSubstrateLiveFacts::sound_pointer_exact,
        &SelectionAdmissionSubstrateLiveFacts::sound_identity_exact,
        &SelectionAdmissionSubstrateLiveFacts::request_exact,
        &SelectionAdmissionSubstrateLiveFacts::state_four,
    };
    const SelectionAdmissionSubstrateLiveFailure substrate_failures[] = {
        SelectionAdmissionSubstrateLiveFailure::ProofInactive,
        SelectionAdmissionSubstrateLiveFailure::ProofStateDrift,
        SelectionAdmissionSubstrateLiveFailure::TransactionInvalid,
        SelectionAdmissionSubstrateLiveFailure::ControllerNull,
        SelectionAdmissionSubstrateLiveFailure::ControllerPointerDrift,
        SelectionAdmissionSubstrateLiveFailure::ControllerIdentityDrift,
        SelectionAdmissionSubstrateLiveFailure::ChainUnreadable,
        SelectionAdmissionSubstrateLiveFailure::SoundUnreadable,
        SelectionAdmissionSubstrateLiveFailure::RequestUnreadable,
        SelectionAdmissionSubstrateLiveFailure::StateUnreadable,
        SelectionAdmissionSubstrateLiveFailure::SoundNull,
        SelectionAdmissionSubstrateLiveFailure::SoundIdentityUnreadable,
        SelectionAdmissionSubstrateLiveFailure::SlotBgmDrift,
        SelectionAdmissionSubstrateLiveFailure::SoundPointerDrift,
        SelectionAdmissionSubstrateLiveFailure::SoundIdentityDrift,
        SelectionAdmissionSubstrateLiveFailure::RequestDrift,
        SelectionAdmissionSubstrateLiveFailure::StateNotFour,
    };
    static_assert(std::size(substrate_fields) == std::size(substrate_failures));
    require(first_selection_admission_substrate_live_failure(substrate_live)
            == SelectionAdmissionSubstrateLiveFailure::None,
        "exact substrate live facts reported a failure");
    for (size_t index = 0; index < std::size(substrate_fields); ++index) {
        auto rejected = substrate_live;
        rejected.*substrate_fields[index] = false;
        require(first_selection_admission_substrate_live_failure(rejected)
                == substrate_failures[index],
            "substrate live classifier reported the wrong first failure");
    }

    constexpr uint64_t stale_canonical_request = UINT64_C(0x0000000500000008);
    constexpr uint64_t advanced_canonical_request = UINT64_C(0x0000000700000008);
    SelectionAdmissionSubstrateRequestRebaseFacts request_rebase{
        true,
        SelectionAdmissionSubstrateLiveFailure::RequestDrift,
        true, true, true, true, true, true,
        stale_canonical_request, advanced_canonical_request,
    };
    require(selection_admission_substrate_request_rebase_exact(request_rebase),
        "ownership-authorized state-4 request-generation advance was not rebased");
    const struct {
        SelectionAdmissionSubstrateLiveFailure failure;
        const char* message;
    } structural_rebase_rejections[] = {
        {SelectionAdmissionSubstrateLiveFailure::ControllerPointerDrift,
            "controller pointer drift authorized substrate rebase"},
        {SelectionAdmissionSubstrateLiveFailure::ControllerIdentityDrift,
            "controller identity drift authorized substrate rebase"},
        {SelectionAdmissionSubstrateLiveFailure::ChainUnreadable,
            "unreadable substrate chain authorized rebase"},
        {SelectionAdmissionSubstrateLiveFailure::RequestUnreadable,
            "unreadable substrate request authorized rebase"},
        {SelectionAdmissionSubstrateLiveFailure::StateUnreadable,
            "unreadable substrate state authorized rebase"},
        {SelectionAdmissionSubstrateLiveFailure::SlotBgmDrift,
            "slot/BGM drift authorized substrate rebase"},
        {SelectionAdmissionSubstrateLiveFailure::SoundPointerDrift,
            "sound pointer drift authorized substrate rebase"},
        {SelectionAdmissionSubstrateLiveFailure::SoundIdentityDrift,
            "sound identity drift authorized substrate rebase"},
        {SelectionAdmissionSubstrateLiveFailure::StateNotFour,
            "non-ready substrate state authorized rebase"},
    };
    for (const auto& rejected_case : structural_rebase_rejections) {
        auto rejected = request_rebase;
        rejected.live_failure = rejected_case.failure;
        require(!selection_admission_substrate_request_rebase_exact(rejected),
            rejected_case.message);
    }
    auto unauthorized_rebase = request_rebase;
    unauthorized_rebase.mutation_authorized = false;
    require(!selection_admission_substrate_request_rebase_exact(unauthorized_rebase),
        "request drift rebased without aggregate mutation authority");
    auto premature_rebase = request_rebase;
    premature_rebase.state_four = false;
    require(!selection_admission_substrate_request_rebase_exact(premature_rebase),
        "request drift rebased before exact native state-4 readiness");
    auto canonical_release_regression = request_rebase;
    canonical_release_regression.canonical_token_nonzero = false;
    require(!selection_admission_substrate_request_rebase_exact(
                canonical_release_regression),
        "request rebase treated a released canonical token as owned");
    auto custom_release_regression = request_rebase;
    custom_release_regression.custom_token_absent = false;
    require(!selection_admission_substrate_request_rebase_exact(
                custom_release_regression),
        "request rebase reused a still-owned or unreleased custom token");
    auto bridge_token_drift = request_rebase;
    bridge_token_drift.bridge_canonical_token_exact = false;
    require(!selection_admission_substrate_request_rebase_exact(bridge_token_drift),
        "request rebase weakened canonical bridge token identity");
    auto bridge_request_drift = request_rebase;
    bridge_request_drift.bridge_old_request_exact = false;
    require(!selection_admission_substrate_request_rebase_exact(bridge_request_drift),
        "request rebase accepted stale bridge request lineage");
    auto request_slot_drift = request_rebase;
    request_slot_drift.observed_request = UINT64_C(0x0000000700010008);
    require(!selection_admission_substrate_request_rebase_exact(request_slot_drift),
        "request rebase accepted a different native request slot");
    auto request_generation_same = request_rebase;
    request_generation_same.observed_request = stale_canonical_request;
    auto request_generation_older = request_rebase;
    request_generation_older.observed_request = UINT64_C(0x0000000400000008);
    auto request_generation_invalid = request_rebase;
    request_generation_invalid.observed_request = UINT64_C(0x0000000700000007);
    require(!selection_admission_substrate_request_rebase_exact(
                 request_generation_same)
            && !selection_admission_substrate_request_rebase_exact(
                request_generation_older)
            && !selection_admission_substrate_request_rebase_exact(
                request_generation_invalid),
        "request rebase accepted same, older, or non-BGM native generation");

    SelectionAdmissionSubstrateRequestRebaseCommitState rebase_commit{
        stale_canonical_request, stale_canonical_request, 41};
    require(commit_selection_admission_substrate_request_rebase(
                request_rebase, rebase_commit)
            && rebase_commit.proof_request == advanced_canonical_request
            && rebase_commit.bridge_old_request == advanced_canonical_request
            && rebase_commit.rearm_epoch == 42,
        "request-rebase orchestration did not commit linked requests and one epoch");
    const auto committed_rebase = rebase_commit;
    require(!commit_selection_admission_substrate_request_rebase(
                bridge_request_drift, rebase_commit)
            && rebase_commit.proof_request == committed_rebase.proof_request
            && rebase_commit.bridge_old_request
                == committed_rebase.bridge_old_request
            && rebase_commit.rearm_epoch == committed_rebase.rearm_epoch,
        "rejected request rebase mutated linked state or advanced the epoch");
    auto bridge_unavailable_rebase = request_rebase;
    bridge_unavailable_rebase.bridge_available = false;
    SelectionAdmissionSubstrateRequestRebaseCommitState unavailable_state{
        stale_canonical_request, stale_canonical_request, 7};
    require(!commit_selection_admission_substrate_request_rebase(
                bridge_unavailable_rebase, unavailable_state)
            && unavailable_state.rearm_epoch == 7,
        "unavailable bridge committed a request rebase or rearm edge");
    SelectionAdmissionSubstrateRequestRebaseCommitState stale_bridge_state{
        stale_canonical_request, advanced_canonical_request, 7};
    require(!commit_selection_admission_substrate_request_rebase(
                request_rebase, stale_bridge_state)
            && stale_bridge_state.rearm_epoch == 7,
        "invalid expected bridge request committed a request rebase");
    auto invalid_expected_rebase = request_rebase;
    invalid_expected_rebase.expected_request = UINT64_C(0x0000000500000007);
    SelectionAdmissionSubstrateRequestRebaseCommitState invalid_expected_state{
        invalid_expected_rebase.expected_request,
        invalid_expected_rebase.expected_request, 7};
    require(!commit_selection_admission_substrate_request_rebase(
                invalid_expected_rebase, invalid_expected_state)
            && invalid_expected_state.proof_request
                == invalid_expected_rebase.expected_request
            && invalid_expected_state.bridge_old_request
                == invalid_expected_rebase.expected_request
            && invalid_expected_state.rearm_epoch == 7,
        "invalid expected native request committed linked state or a rearm edge");

    CanonicalSubstrateResetRequestLineageFacts unchanged_continuity{
        SelectionAdmissionSubstrateLiveFailure::None,
        false, false, false, false,
        true, true, true, true,
    };
    require(canonical_substrate_reset_request_lineage_exact(
                unchanged_continuity),
        "unchanged exact live request lacked a no-rebase continuity path");
    require(canonical_substrate_reset_continuity_route_exact(
                9, 10, AudioRouteLeaseIdentity{}),
        "observed proof-9/reset-10 lease-cleared continuity was rejected");
    require(!canonical_substrate_reset_continuity_route_exact(
                9, 11, AudioRouteLeaseIdentity{}),
        "a non-successor reset generation qualified unchanged continuity");
    require(!canonical_substrate_reset_continuity_route_exact(
                9, 10, AudioRouteLeaseIdentity{1, 2}),
        "a leased reset route qualified lease-cleared continuity");
    for (size_t index = 0; index < 4; ++index) {
        auto drift = unchanged_continuity;
        if (index == 0) drift.continuity_route_absent = false;
        if (index == 1) drift.continuity_lease_absent = false;
        if (index == 2) drift.continuity_lifecycle_absent = false;
        if (index == 3) drift.continuity_reset_route_exact = false;
        require(!canonical_substrate_reset_request_lineage_exact(drift),
            "partial no-rebase or reset-route lineage was accepted");
    }
    auto changed_without_rebase = unchanged_continuity;
    changed_without_rebase.live_failure =
        SelectionAdmissionSubstrateLiveFailure::RequestDrift;
    require(!canonical_substrate_reset_request_lineage_exact(
                changed_without_rebase),
        "changed request qualified continuity without authenticated rebase");
    CanonicalSubstrateResetRequestLineageFacts authenticated_rebase{
        SelectionAdmissionSubstrateLiveFailure::None,
        true, true, true, true,
        false, false, false, false,
    };
    require(canonical_substrate_reset_request_lineage_exact(
                authenticated_rebase),
        "existing authenticated changed-request rebase path regressed");
    for (size_t index = 0; index < 4; ++index) {
        auto drift = authenticated_rebase;
        if (index == 0) drift.authenticated_rebase_route_exact = false;
        if (index == 1) drift.authenticated_rebase_lease_exact = false;
        if (index == 2) drift.authenticated_rebase_lifecycle_exact = false;
        if (index == 3) drift.authenticated_rebase_lease_valid = false;
        require(!canonical_substrate_reset_request_lineage_exact(drift),
            "drifted authenticated route/lease/lifecycle lineage was accepted");
    }

    CanonicalSubstrateResetLineageCommitFacts reset_commit_facts;
    bool CanonicalSubstrateResetLineageCommitFacts::* const reset_commit_fields[] = {
        &CanonicalSubstrateResetLineageCommitFacts::mutation_authorized,
        &CanonicalSubstrateResetLineageCommitFacts::cleanup_verified,
        &CanonicalSubstrateResetLineageCommitFacts::aggregate_ownership_clear,
        &CanonicalSubstrateResetLineageCommitFacts::route_installed,
        &CanonicalSubstrateResetLineageCommitFacts::route_enabled,
        &CanonicalSubstrateResetLineageCommitFacts::reset_route_idle,
        &CanonicalSubstrateResetLineageCommitFacts::reset_route_nonzero,
        &CanonicalSubstrateResetLineageCommitFacts::reset_route_after_proof,
        &CanonicalSubstrateResetLineageCommitFacts::reset_route_generation_room,
        &CanonicalSubstrateResetLineageCommitFacts::reset_state_exact,
        &CanonicalSubstrateResetLineageCommitFacts::request_lineage_exact,
        &CanonicalSubstrateResetLineageCommitFacts::current_zero_idle,
        &CanonicalSubstrateResetLineageCommitFacts::cleanup_clear,
        &CanonicalSubstrateResetLineageCommitFacts::quarantine_clear,
        &CanonicalSubstrateResetLineageCommitFacts::lifecycle_allowed,
        &CanonicalSubstrateResetLineageCommitFacts::substrate_live_exact,
        &CanonicalSubstrateResetLineageCommitFacts::canonical_token_nonzero,
        &CanonicalSubstrateResetLineageCommitFacts::custom_token_absent,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_available,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_generation_nonzero,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_transaction_exact,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_source_exact,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_list_exit_exact,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_canonical_token_exact,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_request_exact,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_controller_exact,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_slot_bgm_exact,
        &CanonicalSubstrateResetLineageCommitFacts::bridge_sound_exact,
        &CanonicalSubstrateResetLineageCommitFacts::authority_absent,
        &CanonicalSubstrateResetLineageCommitFacts::authority_generation_room,
        &CanonicalSubstrateResetLineageCommitFacts::rearm_epoch_room,
    };
    for (const auto field : reset_commit_fields) reset_commit_facts.*field = true;
    require(first_canonical_substrate_reset_lineage_commit_failure(
                reset_commit_facts)
            == CanonicalSubstrateResetLineageCommitFailure::None,
        "exact reset-lineage commit facts were rejected");
    for (const auto field : reset_commit_fields) {
        auto rejected = reset_commit_facts;
        rejected.*field = false;
        require(first_canonical_substrate_reset_lineage_commit_failure(rejected)
                != CanonicalSubstrateResetLineageCommitFailure::None,
            "reset-lineage structural or ownership drift was accepted");
    }

    CanonicalSubstrateResetLineageAuthority linked_reset;
    linked_reset.reset_route_generation = 14;
    linked_reset.reset_observation_generation = 6;
    linked_reset.reset_lease = {31, 37};
    linked_reset.reset_lifecycle_epoch = 17;
    linked_reset.transaction_generation = 3;
    linked_reset.source_ordinal = 5;
    linked_reset.source_version = 7;
    linked_reset.source_collection_version = 11;
    linked_reset.list_exit_epoch = 13;
    linked_reset.proof_route_generation = 9;
    linked_reset.proof_lease = {19, 23};
    linked_reset.proof_lifecycle_epoch = 17;
    linked_reset.request = advanced_canonical_request;
    linked_reset.canonical_token = 29;
    linked_reset.bridge_generation = 2;
    linked_reset.bridge_release_epoch = 31;
    linked_reset.controller = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000));
    linked_reset.controller_proof.mode = ControllerIdentityProofMode::SerialBacked;
    linked_reset.controller_proof.controller = linked_reset.controller;
    linked_reset.controller_proof.object_class =
        reinterpret_cast<void*>(static_cast<uintptr_t>(0x1100));
    linked_reset.controller_proof.raw_internal_index_readable = true;
    linked_reset.controller_proof.raw_internal_index = 47;
    linked_reset.controller_proof.live_capture_succeeded = true;
    linked_reset.controller_proof.live = {47, 53};
    linked_reset.slot = reinterpret_cast<void*>(static_cast<uintptr_t>(0x2000));
    linked_reset.bgm = reinterpret_cast<void*>(static_cast<uintptr_t>(0x3000));
    linked_reset.sound = reinterpret_cast<void*>(static_cast<uintptr_t>(0x4000));
    linked_reset.sound_identity = {41, 43};
    CanonicalSubstrateResetLineageCommitState reset_state{8, 50, 0, {}, 0, {}};
    require(commit_canonical_substrate_reset_lineage(
                reset_commit_facts, linked_reset, reset_state)
            && reset_state.authority_generation == 9
            && reset_state.rearm_epoch == 51
            && reset_state.authority.phase
                == CanonicalSubstrateResetLineagePhase::Qualified
            && reset_state.authority.generation == 9
            && reset_state.authority.rearm_epoch == 51
            && reset_state.request_lineage_route_generation == 14
            && reset_state.request_lineage_lease == linked_reset.reset_lease
            && reset_state.request_lineage_lifecycle_epoch == 17
            && reset_state.authority.reset_route_generation == 14
            && reset_state.authority.reset_observation_generation == 6
            && reset_state.authority.reset_lease == linked_reset.reset_lease
            && reset_state.authority.reset_lifecycle_epoch == 17
            && reset_state.authority.transaction_generation == 3
            && reset_state.authority.source_ordinal == 5
            && reset_state.authority.source_version == 7
            && reset_state.authority.source_collection_version == 11
            && reset_state.authority.list_exit_epoch == 13
            && reset_state.authority.proof_route_generation == 9
            && reset_state.authority.proof_lease == linked_reset.proof_lease
            && reset_state.authority.proof_lifecycle_epoch == 17
            && reset_state.authority.request == advanced_canonical_request
            && reset_state.authority.canonical_token == 29
            && reset_state.authority.bridge_generation == 2
            && reset_state.authority.bridge_release_epoch == 31
            && reset_state.authority.controller == linked_reset.controller
            && controller_identity_proof_matches(
                reset_state.authority.controller_proof,
                linked_reset.controller_proof)
            && reset_state.authority.slot == linked_reset.slot
            && reset_state.authority.bgm == linked_reset.bgm
            && reset_state.authority.sound == linked_reset.sound
            && private_object_handle_matches(
                reset_state.authority.sound_identity,
                linked_reset.sound_identity),
        "no-rebase reset qualification did not commit lineage and one rearm edge");

    const CanonicalSubstrateResetLineageAuthority empty_reset{};
    require(canonical_substrate_reset_lineage_authority_empty(empty_reset)
            && canonical_substrate_reset_lineage_authority_matches(
                empty_reset, empty_reset),
        "two canonical empty reset authorities did not match");
    auto malformed_empty_reset = empty_reset;
    malformed_empty_reset.generation = 1;
    require(!canonical_substrate_reset_lineage_authority_empty(
                malformed_empty_reset)
            && !canonical_substrate_reset_lineage_authority_matches(
                empty_reset, malformed_empty_reset),
        "generation-bearing phase-None reset authority matched absence");
    malformed_empty_reset = empty_reset;
    malformed_empty_reset.reset_lease = {1, 2};
    require(!canonical_substrate_reset_lineage_authority_matches(
                empty_reset, malformed_empty_reset),
        "lease-bearing phase-None reset authority matched absence");
    malformed_empty_reset = empty_reset;
    malformed_empty_reset.controller_proof.raw_internal_index_readable = true;
    require(!canonical_substrate_reset_lineage_authority_matches(
                empty_reset, malformed_empty_reset),
        "identity-bearing phase-None reset authority matched absence");
    malformed_empty_reset = empty_reset;
    malformed_empty_reset.canonical_token = 1;
    malformed_empty_reset.sound =
        reinterpret_cast<void*>(static_cast<uintptr_t>(0x5000));
    require(!canonical_substrate_reset_lineage_authority_matches(
                empty_reset, malformed_empty_reset),
        "token/pointer-bearing phase-None reset authority matched absence");
    require(!canonical_substrate_reset_lineage_authority_matches(
                empty_reset, reset_state.authority),
        "phase-None and active reset authorities matched");
    const auto active_reset_copy = reset_state.authority;
    require(canonical_substrate_reset_lineage_authority_matches(
                reset_state.authority, active_reset_copy),
        "exact valid active reset authorities did not match");
    auto drifted_active_reset = active_reset_copy;
    ++drifted_active_reset.request;
    require(!canonical_substrate_reset_lineage_authority_matches(
                reset_state.authority, drifted_active_reset),
        "active reset authority request drift matched");

    const CanonicalSubstrateResetBridgeSoundFacts distinct_bridge_sound{
        true, true, true, true};
    require(canonical_substrate_reset_bridge_sound_exact(distinct_bridge_sound),
        "distinct valid callback sound was rejected as canonical sound drift");
    auto malformed_callback_sound = distinct_bridge_sound;
    malformed_callback_sound.callback_identity_valid = false;
    require(!canonical_substrate_reset_bridge_sound_exact(
                malformed_callback_sound),
        "malformed historical callback sound was accepted");
    auto old_canonical_pointer_drift = distinct_bridge_sound;
    old_canonical_pointer_drift.old_canonical_pointer_exact = false;
    require(!canonical_substrate_reset_bridge_sound_exact(
                old_canonical_pointer_drift),
        "old canonical sound pointer drift was accepted");
    auto old_canonical_identity_drift = distinct_bridge_sound;
    old_canonical_identity_drift.old_canonical_identity_exact = false;
    require(!canonical_substrate_reset_bridge_sound_exact(
                old_canonical_identity_drift),
        "old canonical sound identity drift was accepted");

    CanonicalSubstrateBridgeAuthority historical_bridge;
    historical_bridge.phase = CanonicalSubstrateBridgePhase::Available;
    historical_bridge.generation = 2;
    historical_bridge.transaction_generation = 3;
    historical_bridge.source_ordinal = 5;
    historical_bridge.source_version = 7;
    historical_bridge.source_collection_version = 11;
    historical_bridge.list_exit_epoch = 13;
    historical_bridge.release_completion_epoch = 17;
    historical_bridge.revocation_epoch = 0;
    historical_bridge.canonical_token = 23;
    historical_bridge.expected_callback_sound =
        reinterpret_cast<void*>(static_cast<uintptr_t>(0x5000));
    historical_bridge.expected_callback_sound_identity = {59, 61};
    historical_bridge.old_canonical_sound = linked_reset.sound;
    historical_bridge.old_canonical_sound_identity = linked_reset.sound_identity;
    historical_bridge.old_canonical_request = linked_reset.request;
    historical_bridge.controller = linked_reset.controller;
    historical_bridge.controller_proof = linked_reset.controller_proof;
    historical_bridge.slot = linked_reset.slot;
    historical_bridge.bgm = linked_reset.bgm;
    historical_bridge.predecessor_route_generation = 9;
    historical_bridge.predecessor_lease = {19, 23};
    historical_bridge.predecessor_lifecycle_epoch = 17;
    require(canonical_substrate_bridge_authority_matches(
                historical_bridge, historical_bridge),
        "exact historical bridge authority did not match");
    require(canonical_substrate_bridge_available_authority_valid(
                historical_bridge),
        "Lets-like cleanup did not produce an exact Available bridge");
    auto drifted_historical_bridge = historical_bridge;
    ++drifted_historical_bridge.source_version;
    require(!canonical_substrate_bridge_authority_matches(
                historical_bridge, drifted_historical_bridge),
        "commit-time historical bridge drift matched");

    auto reserved_historical_bridge = historical_bridge;
    reserved_historical_bridge.phase = CanonicalSubstrateBridgePhase::Reserved;
    reserved_historical_bridge.revocation_epoch = 251;
    require(canonical_substrate_bridge_reservation_successor_exact(
                historical_bridge, reserved_historical_bridge),
        "runtime phase-publication fixture rejected its exact bridge successor");
    const CanonicalSubstrateBridgeAuthority no_bridge;
    require(canonical_substrate_bridge_predecessor_stage_exact(
                no_bridge, CanonicalSubstrateBridgePhase::Available)
            && canonical_substrate_bridge_predecessor_stage_exact(
                no_bridge, CanonicalSubstrateBridgePhase::Reserved)
            && canonical_substrate_bridge_predecessor_stage_exact(
                historical_bridge, CanonicalSubstrateBridgePhase::Available)
            && !canonical_substrate_bridge_predecessor_stage_exact(
                historical_bridge, CanonicalSubstrateBridgePhase::Reserved)
            && !canonical_substrate_bridge_predecessor_stage_exact(
                reserved_historical_bridge,
                CanonicalSubstrateBridgePhase::Available)
            && canonical_substrate_bridge_predecessor_stage_exact(
                reserved_historical_bridge,
                CanonicalSubstrateBridgePhase::Reserved),
        "normal successor bridge stage admitted unowned Reserved authority");
    CanonicalSubstrateReservationPublicationFacts publication_facts{
        true, true, true, true, true, 251, 251};
    CanonicalSubstrateReservationPublicationState publication_state{
        SelectionActivationReservationState::Reserved,
        2,
        historical_bridge,
        CanonicalSubstrateResetLineagePhase::None,
        0,
        false};
    require(commit_canonical_substrate_reservation_publication(
                publication_facts, reserved_historical_bridge, false,
                publication_state)
            && publication_state.record_published
            && publication_state.record_generation == 2
            && canonical_substrate_bridge_authority_matches(
                publication_state.bridge, reserved_historical_bridge)
            && publication_state.reset_phase
                == CanonicalSubstrateResetLineagePhase::None,
        "runtime-confirmed second normal reservation did not publish atomically");
    const auto published_state = publication_state;
    auto failed_publication_facts = publication_facts;
    failed_publication_facts.proof_exact = false;
    CanonicalSubstrateReservationPublicationState failed_publication_state{
        SelectionActivationReservationState::Reserved,
        3,
        historical_bridge,
        CanonicalSubstrateResetLineagePhase::None,
        0,
        false};
    require(!commit_canonical_substrate_reservation_publication(
                failed_publication_facts, reserved_historical_bridge, false,
                failed_publication_state)
            && !failed_publication_state.record_published
            && failed_publication_state.record_generation == 0
            && canonical_substrate_bridge_authority_matches(
                failed_publication_state.bridge, historical_bridge),
        "phase-publication rejection mutated bridge or reservation record");
    auto advanced_epoch_bridge = reserved_historical_bridge;
    advanced_epoch_bridge.revocation_epoch = 252;
    auto stale_snapshot_epoch_facts = publication_facts;
    stale_snapshot_epoch_facts.current_attempt_epoch = 252;
    auto stale_snapshot_epoch_state = CanonicalSubstrateReservationPublicationState{
        SelectionActivationReservationState::Reserved,
        3,
        historical_bridge,
        CanonicalSubstrateResetLineagePhase::None,
        0,
        false};
    require(!commit_canonical_substrate_reservation_publication(
                stale_snapshot_epoch_facts, reserved_historical_bridge, false,
                stale_snapshot_epoch_state)
            && !stale_snapshot_epoch_state.record_published
            && canonical_substrate_bridge_authority_matches(
                stale_snapshot_epoch_state.bridge, historical_bridge),
        "epoch advance after Available snapshot published a stale attempt");
    auto current_epoch_publication_facts = publication_facts;
    current_epoch_publication_facts.expected_attempt_epoch = 252;
    current_epoch_publication_facts.current_attempt_epoch = 252;
    require(commit_canonical_substrate_reservation_publication(
                current_epoch_publication_facts, advanced_epoch_bridge, false,
                stale_snapshot_epoch_state)
            && stale_snapshot_epoch_state.record_published
            && stale_snapshot_epoch_state.bridge.revocation_epoch == 252,
        "Reserved bridge did not bind the authoritative publication epoch");
    require(selection_activation_attempt_epoch_exact(252, 252)
            && !selection_activation_attempt_epoch_exact(251, 252)
            && !selection_activation_attempt_epoch_exact(0, 0),
        "activation attempt epoch accepted stale or absent authority");
    const SelectionActivationCommitEpochFacts consumed_epoch{
        true, 7, 7, 7, 252, 252, 252};
    require(selection_activation_commit_epoch_exact(consumed_epoch),
        "exact consumed attempt epoch was not commit-authorized");
    for (size_t index = 0; index < 7; ++index) {
        auto drift = consumed_epoch;
        switch (index) {
        case 0: drift.machine_consumed = false; break;
        case 1: ++drift.machine_generation; break;
        case 2: ++drift.record_generation; break;
        case 3: drift.expected_generation = 0; break;
        case 4: ++drift.record_attempt_epoch; break;
        case 5: ++drift.expected_attempt_epoch; break;
        case 6: ++drift.current_revocation_epoch; break;
        default: break;
        }
        require(!selection_activation_commit_epoch_exact(drift),
            "drifted consumed attempt retained commit publication authority");
    }
    auto unrelated_reserved = reserved_historical_bridge;
    ++unrelated_reserved.source_version;
    require(!canonical_substrate_bridge_reservation_successor_exact(
                historical_bridge, unrelated_reserved)
            && canonical_substrate_bridge_authority_matches(
                published_state.bridge, reserved_historical_bridge),
        "unrelated Reserved bridge bypassed exact attempt ownership");

    auto qualified_publication_state = CanonicalSubstrateReservationPublicationState{
        SelectionActivationReservationState::Reserved,
        4,
        historical_bridge,
        CanonicalSubstrateResetLineagePhase::Qualified,
        0,
        false};
    require(commit_canonical_substrate_reservation_publication(
                publication_facts, reserved_historical_bridge, true,
                qualified_publication_state)
            && qualified_publication_state.record_published
            && qualified_publication_state.reset_phase
                == CanonicalSubstrateResetLineagePhase::Reserved,
        "qualified reset reservation did not publish with its exact bridge");

    CanonicalSubstratePhaseReservationFacts phase_reservation_facts{
        true, true, true, true, true, true};
    CanonicalSubstratePhaseReservationState normal_phase_state{
        CanonicalSubstrateBridgePhase::Available,
        CanonicalSubstrateResetLineagePhase::None,
        reset_state.rearm_epoch};
    const uint64_t normal_phase_rearm = normal_phase_state.rearm_epoch;
    require(commit_canonical_substrate_phase_reservation(
                phase_reservation_facts, false, normal_phase_state)
            && normal_phase_state.bridge_phase
                == CanonicalSubstrateBridgePhase::Reserved
            && normal_phase_state.reset_phase
                == CanonicalSubstrateResetLineagePhase::None
            && normal_phase_state.rearm_epoch == normal_phase_rearm,
        "normal substrate phase reservation was not transactional");
    CanonicalSubstratePhaseRollbackFacts phase_rollback_facts{
        true, true, true, true, true, true};
    require(rollback_canonical_substrate_phase_reservation(
                phase_rollback_facts, false, normal_phase_state)
            && normal_phase_state.bridge_phase
                == CanonicalSubstrateBridgePhase::Available
            && normal_phase_state.reset_phase
                == CanonicalSubstrateResetLineagePhase::None
            && normal_phase_state.rearm_epoch == normal_phase_rearm,
        "post-consume normal phase rollback did not restore exactly");
    auto confirmed_rejection = phase_reservation_facts;
    confirmed_rejection.confirmed_reservation_absent = false;
    const auto normal_available_state = normal_phase_state;
    require(!commit_canonical_substrate_phase_reservation(
                confirmed_rejection, false, normal_phase_state)
            && normal_phase_state.bridge_phase
                == normal_available_state.bridge_phase
            && normal_phase_state.rearm_epoch == normal_phase_rearm,
        "confirmed-reservation rejection published substrate phases");
    auto exhausted_rejection = phase_reservation_facts;
    exhausted_rejection.reservation_generation_available = false;
    require(!commit_canonical_substrate_phase_reservation(
                exhausted_rejection, false, normal_phase_state)
            && normal_phase_state.bridge_phase
                == CanonicalSubstrateBridgePhase::Available,
        "generation exhaustion published substrate phases");

    CanonicalSubstratePhaseReservationState reset_phase_state{
        CanonicalSubstrateBridgePhase::Available,
        CanonicalSubstrateResetLineagePhase::Qualified,
        reset_state.rearm_epoch};
    const uint64_t reset_phase_rearm = reset_phase_state.rearm_epoch;
    require(commit_canonical_substrate_phase_reservation(
                phase_reservation_facts, true, reset_phase_state)
            && reset_phase_state.bridge_phase
                == CanonicalSubstrateBridgePhase::Reserved
            && reset_phase_state.reset_phase
                == CanonicalSubstrateResetLineagePhase::Reserved,
        "qualified reset phase reservation did not commit together");
    auto drifted_rollback = phase_rollback_facts;
    drifted_rollback.proof_exact = false;
    require(!rollback_canonical_substrate_phase_reservation(
                drifted_rollback, true, reset_phase_state)
            && reset_phase_state.bridge_phase
                == CanonicalSubstrateBridgePhase::Reserved
            && reset_phase_state.reset_phase
                == CanonicalSubstrateResetLineagePhase::Reserved,
        "drifted attempt reopened reserved substrate phases");
    require(rollback_canonical_substrate_phase_reservation(
                phase_rollback_facts, true, reset_phase_state)
            && reset_phase_state.bridge_phase
                == CanonicalSubstrateBridgePhase::Available
            && reset_phase_state.reset_phase
                == CanonicalSubstrateResetLineagePhase::Qualified
            && reset_phase_state.rearm_epoch == reset_phase_rearm,
        "qualified reset rollback did not restore without a rearm edge");
    phase_rollback_facts.attempt_owned = false;
    require(!rollback_canonical_substrate_phase_reservation(
                phase_rollback_facts, true, reset_phase_state),
        "stale rollback authority reopened a completed attempt");

    SelectionActivationSubstrateAttemptFinalizeFacts attempt_finalize_facts{
        true, true, true, true, true, true, true, true, true, true, true, true};
    SelectionActivationSubstrateAttemptFinalizeState consumed_attempt{
        SelectionActivationReservationState::Consumed,
        7,
        false,
        CanonicalSubstrateBridgePhase::Reserved,
        CanonicalSubstrateResetLineagePhase::None,
        reset_phase_rearm};
    const auto exact_consumed_attempt = consumed_attempt;
    require(finalize_selection_activation_substrate_attempt(
                attempt_finalize_facts,
                SelectionActivationSubstrateAttemptDisposition::RestoreIfExact,
                false,
                consumed_attempt)
            && consumed_attempt.machine_retired
            && consumed_attempt.phases_restored
            && consumed_attempt.machine_state
                == SelectionActivationReservationState::Revoked
            && consumed_attempt.bridge_phase
                == CanonicalSubstrateBridgePhase::Available
            && consumed_attempt.rearm_epoch == reset_phase_rearm,
        "exact unsuperseded consumed attempt did not restore and retire");
    auto superseded_attempt_facts = attempt_finalize_facts;
    superseded_attempt_facts.reservation_generation_exact = false;
    auto superseded_attempt = exact_consumed_attempt;
    require(!finalize_selection_activation_substrate_attempt(
                superseded_attempt_facts,
                SelectionActivationSubstrateAttemptDisposition::RestoreIfExact,
                false,
                superseded_attempt)
            && superseded_attempt.bridge_phase
                == CanonicalSubstrateBridgePhase::Reserved,
        "consumed attempt A restored after competing attempt B");
    auto epoch_drift_facts = attempt_finalize_facts;
    epoch_drift_facts.revocation_epoch_exact = false;
    auto epoch_drift_attempt = exact_consumed_attempt;
    require(finalize_selection_activation_substrate_attempt(
                epoch_drift_facts,
                SelectionActivationSubstrateAttemptDisposition::RestoreIfExact,
                false,
                epoch_drift_attempt)
            && epoch_drift_attempt.machine_retired
            && !epoch_drift_attempt.phases_restored
            && epoch_drift_attempt.bridge_phase
                == CanonicalSubstrateBridgePhase::Reserved,
        "revocation-epoch drift reopened substrate authority");
    auto native_drift_attempt = exact_consumed_attempt;
    require(finalize_selection_activation_substrate_attempt(
                attempt_finalize_facts,
                SelectionActivationSubstrateAttemptDisposition::RetainFailClosed,
                false,
                native_drift_attempt)
            && native_drift_attempt.machine_retired
            && !native_drift_attempt.phases_restored
            && native_drift_attempt.bridge_phase
                == CanonicalSubstrateBridgePhase::Reserved,
        "controller/live-substrate failure reopened phases or stranded machine");
    auto qualified_consumed_attempt = exact_consumed_attempt;
    qualified_consumed_attempt.reset_phase =
        CanonicalSubstrateResetLineagePhase::Reserved;
    require(finalize_selection_activation_substrate_attempt(
                attempt_finalize_facts,
                SelectionActivationSubstrateAttemptDisposition::RestoreIfExact,
                true,
                qualified_consumed_attempt)
            && qualified_consumed_attempt.bridge_phase
                == CanonicalSubstrateBridgePhase::Available
            && qualified_consumed_attempt.reset_phase
                == CanonicalSubstrateResetLineagePhase::Qualified
            && qualified_consumed_attempt.rearm_epoch == reset_phase_rearm,
        "qualified reset attempt did not restore without a rearm edge");
    auto duplicate_rollback_facts = attempt_finalize_facts;
    duplicate_rollback_facts.attempt_active = false;
    require(!finalize_selection_activation_substrate_attempt(
                duplicate_rollback_facts,
                SelectionActivationSubstrateAttemptDisposition::RestoreIfExact,
                false,
                consumed_attempt),
        "duplicate rollback reused consumed attempt authority");

    const auto committed_reset = reset_state;
    auto reused_reset = reset_commit_facts;
    reused_reset.authority_absent = false;
    require(!commit_canonical_substrate_reset_lineage(
                reused_reset, linked_reset, reset_state)
            && reset_state.authority_generation
                == committed_reset.authority_generation
            && reset_state.rearm_epoch == committed_reset.rearm_epoch
            && reset_state.request_lineage_route_generation
                == committed_reset.request_lineage_route_generation
            && reset_state.request_lineage_lease
                == committed_reset.request_lineage_lease
            && reset_state.request_lineage_lifecycle_epoch
                == committed_reset.request_lineage_lifecycle_epoch,
        "stale reset authority was reused or advanced another rearm edge");

    CanonicalSubstrateResetLineageUseFacts reset_use;
    bool CanonicalSubstrateResetLineageUseFacts::* const reset_use_fields[] = {
        &CanonicalSubstrateResetLineageUseFacts::bridge_stage_exact,
        &CanonicalSubstrateResetLineageUseFacts::current_zero_idle,
        &CanonicalSubstrateResetLineageUseFacts::authority_phase_exact,
        &CanonicalSubstrateResetLineageUseFacts::authority_generation_valid,
        &CanonicalSubstrateResetLineageUseFacts::reset_observation_exact,
        &CanonicalSubstrateResetLineageUseFacts::reset_route_valid,
        &CanonicalSubstrateResetLineageUseFacts::request_rebase_lineage_exact,
        &CanonicalSubstrateResetLineageUseFacts::reset_lifecycle_exact,
        &CanonicalSubstrateResetLineageUseFacts::transaction_exact,
        &CanonicalSubstrateResetLineageUseFacts::source_exact,
        &CanonicalSubstrateResetLineageUseFacts::list_exit_exact,
        &CanonicalSubstrateResetLineageUseFacts::proof_route_exact,
        &CanonicalSubstrateResetLineageUseFacts::proof_lease_exact,
        &CanonicalSubstrateResetLineageUseFacts::proof_lifecycle_exact,
        &CanonicalSubstrateResetLineageUseFacts::request_exact,
        &CanonicalSubstrateResetLineageUseFacts::canonical_token_exact,
        &CanonicalSubstrateResetLineageUseFacts::bridge_generation_exact,
        &CanonicalSubstrateResetLineageUseFacts::bridge_release_exact,
        &CanonicalSubstrateResetLineageUseFacts::controller_exact,
        &CanonicalSubstrateResetLineageUseFacts::controller_proof_exact,
        &CanonicalSubstrateResetLineageUseFacts::slot_bgm_exact,
        &CanonicalSubstrateResetLineageUseFacts::sound_exact,
        &CanonicalSubstrateResetLineageUseFacts::sound_identity_exact,
    };
    for (const auto field : reset_use_fields) reset_use.*field = true;
    require(canonical_substrate_route_predecessor_exact(reset_use),
        "qualified zero-current reset predecessor was rejected");
    for (const auto field : reset_use_fields) {
        auto rejected = reset_use;
        rejected.*field = false;
        require(!canonical_substrate_route_predecessor_exact(rejected),
            "arbitrary, stale, or structurally drifted reset authority was accepted");
    }
    CanonicalSubstrateResetLineageUseFacts normal_successor;
    normal_successor.normal_successor = true;
    normal_successor.bridge_stage_exact = true;
    require(canonical_substrate_route_predecessor_exact(normal_successor),
        "normal N-to-N+1 predecessor path regressed");
    normal_successor.bridge_stage_exact = false;
    require(!canonical_substrate_route_predecessor_exact(normal_successor),
        "normal successor accepted an unrelated Reserved bridge stage");

    // A denial before the state-4 edge remains fail-closed. The exact rebase
    // advances the production revocation epoch, making the same selection
    // retryable without a selection-generation change or timing delay.
    uint64_t denied_epoch = 41;
    uint64_t current_epoch = 41;
    require(!selection_activation_query_result({true, true, true,
                denied_epoch == current_epoch, false}),
        "early same-selection input bypassed its denial latch");
    ++current_epoch;
    require(selection_activation_query_result({true, true, true,
                denied_epoch == current_epoch, true}),
        "exact readiness edge did not rearm same-selection admission");

    SelectionAdmissionProofMatchFacts proof_match;
    bool SelectionAdmissionProofMatchFacts::* const proof_fields[] = {
        &SelectionAdmissionProofMatchFacts::left_active,
        &SelectionAdmissionProofMatchFacts::right_active,
        &SelectionAdmissionProofMatchFacts::left_state_exact,
        &SelectionAdmissionProofMatchFacts::right_state_exact,
        &SelectionAdmissionProofMatchFacts::transaction_nonzero,
        &SelectionAdmissionProofMatchFacts::transaction_exact,
        &SelectionAdmissionProofMatchFacts::source_ordinal_exact,
        &SelectionAdmissionProofMatchFacts::source_version_exact,
        &SelectionAdmissionProofMatchFacts::collection_version_exact,
        &SelectionAdmissionProofMatchFacts::list_exit_exact,
        &SelectionAdmissionProofMatchFacts::controller_pointer_exact,
        &SelectionAdmissionProofMatchFacts::controller_identity_exact,
        &SelectionAdmissionProofMatchFacts::slot_bgm_exact,
        &SelectionAdmissionProofMatchFacts::sound_pointer_exact,
        &SelectionAdmissionProofMatchFacts::sound_index_exact,
        &SelectionAdmissionProofMatchFacts::sound_serial_exact,
        &SelectionAdmissionProofMatchFacts::request_exact,
        &SelectionAdmissionProofMatchFacts::request_rebase_lineage_exact,
        &SelectionAdmissionProofMatchFacts::route_generation_exact,
        &SelectionAdmissionProofMatchFacts::lease_exact,
        &SelectionAdmissionProofMatchFacts::lifecycle_epoch_exact,
        &SelectionAdmissionProofMatchFacts::canonical_token_exact,
        &SelectionAdmissionProofMatchFacts::custom_token_exact,
        &SelectionAdmissionProofMatchFacts::bridge_generation_exact,
        &SelectionAdmissionProofMatchFacts::bridge_phase_exact,
        &SelectionAdmissionProofMatchFacts::bridge_release_epoch_exact,
        &SelectionAdmissionProofMatchFacts::bridge_callback_pointer_exact,
        &SelectionAdmissionProofMatchFacts::bridge_callback_identity_exact,
        &SelectionAdmissionProofMatchFacts::bridge_canonical_token_exact,
        &SelectionAdmissionProofMatchFacts::bridge_authority_exact,
        &SelectionAdmissionProofMatchFacts::reset_lineage_exact,
    };
    const SelectionAdmissionProofMatchFailure proof_failures[] = {
        SelectionAdmissionProofMatchFailure::GlobalInactive,
        SelectionAdmissionProofMatchFailure::RetainedInactive,
        SelectionAdmissionProofMatchFailure::GlobalStateDrift,
        SelectionAdmissionProofMatchFailure::RetainedStateDrift,
        SelectionAdmissionProofMatchFailure::TransactionInvalid,
        SelectionAdmissionProofMatchFailure::TransactionDrift,
        SelectionAdmissionProofMatchFailure::SourceOrdinalDrift,
        SelectionAdmissionProofMatchFailure::SourceVersionDrift,
        SelectionAdmissionProofMatchFailure::CollectionVersionDrift,
        SelectionAdmissionProofMatchFailure::ListExitDrift,
        SelectionAdmissionProofMatchFailure::ControllerPointerDrift,
        SelectionAdmissionProofMatchFailure::ControllerIdentityDrift,
        SelectionAdmissionProofMatchFailure::SlotBgmDrift,
        SelectionAdmissionProofMatchFailure::SoundPointerDrift,
        SelectionAdmissionProofMatchFailure::SoundIndexDrift,
        SelectionAdmissionProofMatchFailure::SoundSerialDrift,
        SelectionAdmissionProofMatchFailure::RequestDrift,
        SelectionAdmissionProofMatchFailure::RequestRebaseLineageDrift,
        SelectionAdmissionProofMatchFailure::RouteGenerationDrift,
        SelectionAdmissionProofMatchFailure::LeaseDrift,
        SelectionAdmissionProofMatchFailure::LifecycleEpochDrift,
        SelectionAdmissionProofMatchFailure::CanonicalTokenDrift,
        SelectionAdmissionProofMatchFailure::CustomTokenDrift,
        SelectionAdmissionProofMatchFailure::BridgeGenerationDrift,
        SelectionAdmissionProofMatchFailure::BridgePhaseDrift,
        SelectionAdmissionProofMatchFailure::BridgeReleaseEpochDrift,
        SelectionAdmissionProofMatchFailure::BridgeCallbackPointerDrift,
        SelectionAdmissionProofMatchFailure::BridgeCallbackIdentityDrift,
        SelectionAdmissionProofMatchFailure::BridgeCanonicalTokenDrift,
        SelectionAdmissionProofMatchFailure::BridgeAuthorityDrift,
        SelectionAdmissionProofMatchFailure::ResetLineageDrift,
    };
    static_assert(std::size(proof_fields) == std::size(proof_failures));
    for (const auto field : proof_fields) proof_match.*field = true;
    require(first_selection_admission_proof_match_failure(proof_match)
            == SelectionAdmissionProofMatchFailure::None,
        "exact proof-match facts reported a failure");
    for (size_t index = 0; index < std::size(proof_fields); ++index) {
        auto rejected = proof_match;
        rejected.*proof_fields[index] = false;
        require(first_selection_admission_proof_match_failure(rejected)
                == proof_failures[index],
            "proof-match classifier reported the wrong first failure");
    }

    SelectionAdmissionCommitFacts commit_facts;
    bool SelectionAdmissionCommitFacts::* const commit_fields[] = {
        &SelectionAdmissionCommitFacts::admission_exists,
        &SelectionAdmissionCommitFacts::selection_guard_acquired,
        &SelectionAdmissionCommitFacts::not_committed,
        &SelectionAdmissionCommitFacts::aggregate_lease_present,
        &SelectionAdmissionCommitFacts::aggregate_lease_active,
        &SelectionAdmissionCommitFacts::exit_not_requested,
        &SelectionAdmissionCommitFacts::exit_not_pending,
        &SelectionAdmissionCommitFacts::selection_guard_current,
        &SelectionAdmissionCommitFacts::activation_revocation_epoch_exact,
        &SelectionAdmissionCommitFacts::controller_pointer_exact,
        &SelectionAdmissionCommitFacts::controller_identity_exact,
        &SelectionAdmissionCommitFacts::substrate_live_exact,
        &SelectionAdmissionCommitFacts::proof_match_exact,
        &SelectionAdmissionCommitFacts::proof_reservation_succeeded,
        &SelectionAdmissionCommitFacts::route_installed,
        &SelectionAdmissionCommitFacts::route_enabled,
        &SelectionAdmissionCommitFacts::frozen_lease_inactive,
        &SelectionAdmissionCommitFacts::native_route_unowned,
        &SelectionAdmissionCommitFacts::list_cleanup_clear,
        &SelectionAdmissionCommitFacts::route_idle,
        &SelectionAdmissionCommitFacts::route_generation_exact,
        &SelectionAdmissionCommitFacts::lifecycle_epoch_exact,
        &SelectionAdmissionCommitFacts::proof_predecessor_exact,
        &SelectionAdmissionCommitFacts::cleanup_only_clear,
        &SelectionAdmissionCommitFacts::lifecycle_preflight_allowed,
        &SelectionAdmissionCommitFacts::sidecar_pointer_exact,
        &SelectionAdmissionCommitFacts::sidecar_header_exact,
        &SelectionAdmissionCommitFacts::sidecar_allocation_exact,
        &SelectionAdmissionCommitFacts::sidecar_mabf_exact,
        &SelectionAdmissionCommitFacts::final_coordinator_exact,
        &SelectionAdmissionCommitFacts::publication_reached,
        &SelectionAdmissionCommitFacts::controller_bind_exact,
    };
    const SelectionAdmissionCommitFailure commit_failures[] = {
        SelectionAdmissionCommitFailure::AdmissionMissing,
        SelectionAdmissionCommitFailure::SelectionGuardMissing,
        SelectionAdmissionCommitFailure::AlreadyCommitted,
        SelectionAdmissionCommitFailure::AggregateLeaseMissing,
        SelectionAdmissionCommitFailure::AggregateLeaseInactive,
        SelectionAdmissionCommitFailure::AggregateExitRequested,
        SelectionAdmissionCommitFailure::AggregateExitPending,
        SelectionAdmissionCommitFailure::SelectionGuardDrift,
        SelectionAdmissionCommitFailure::ActivationRevocationEpochDrift,
        SelectionAdmissionCommitFailure::ControllerPointerDrift,
        SelectionAdmissionCommitFailure::ControllerIdentityDrift,
        SelectionAdmissionCommitFailure::SubstrateLiveRejected,
        SelectionAdmissionCommitFailure::ProofMatchRejected,
        SelectionAdmissionCommitFailure::ProofReservationFailed,
        SelectionAdmissionCommitFailure::RouteNotInstalled,
        SelectionAdmissionCommitFailure::RouteDisabled,
        SelectionAdmissionCommitFailure::FrozenLeaseActive,
        SelectionAdmissionCommitFailure::NativeRouteOwned,
        SelectionAdmissionCommitFailure::ListCleanupPending,
        SelectionAdmissionCommitFailure::RouteNotIdle,
        SelectionAdmissionCommitFailure::RouteGenerationDrift,
        SelectionAdmissionCommitFailure::LifecycleEpochDrift,
        SelectionAdmissionCommitFailure::ProofPredecessorDrift,
        SelectionAdmissionCommitFailure::CleanupOnlyBlocked,
        SelectionAdmissionCommitFailure::LifecyclePreflightRejected,
        SelectionAdmissionCommitFailure::SidecarPointerDrift,
        SelectionAdmissionCommitFailure::SidecarHeaderDrift,
        SelectionAdmissionCommitFailure::SidecarAllocationDrift,
        SelectionAdmissionCommitFailure::SidecarMabfDrift,
        SelectionAdmissionCommitFailure::FinalCoordinatorRejected,
        SelectionAdmissionCommitFailure::PublicationNotReached,
        SelectionAdmissionCommitFailure::ControllerBindFailed,
    };
    static_assert(std::size(commit_fields) == std::size(commit_failures));
    for (const auto field : commit_fields) commit_facts.*field = true;
    require(first_selection_admission_commit_failure(commit_facts)
            == SelectionAdmissionCommitFailure::None,
        "exact commit facts reported a failure");
    for (size_t index = 0; index < std::size(commit_fields); ++index) {
        auto rejected = commit_facts;
        rejected.*commit_fields[index] = false;
        require(first_selection_admission_commit_failure(rejected)
                == commit_failures[index],
            "commit classifier reported the wrong first failure");
    }
    auto post_consume_epoch_advance = commit_facts;
    post_consume_epoch_advance.activation_revocation_epoch_exact = false;
    require(first_selection_admission_commit_failure(
                post_consume_epoch_advance)
            == SelectionAdmissionCommitFailure::ActivationRevocationEpochDrift,
        "post-consume epoch advance reached proof, route, or token publication");
    require(chart_audio_expand_custom_data_allowed(true, true, true)
            && !chart_audio_expand_custom_data_allowed(true, false, true)
            && !chart_audio_expand_custom_data_allowed(true, true, false)
            && !chart_audio_expand_custom_data_allowed(false, true, true),
        "persistent expand could consume custom chart without matching audio arm");
    require(chart_audio_cancel_complete(true, true, true, true)
            && !chart_audio_cancel_complete(false, true, true, true)
            && !chart_audio_cancel_complete(true, false, true, true)
            && !chart_audio_cancel_complete(true, true, false, true)
            && !chart_audio_cancel_complete(true, true, true, false),
        "chart/audio cancellation accepted stranded chart, guard, freeze, or route");
    const ChartAudioCommittedCancelFacts exact_cancel{
        true, true, true, true, true, true,
    };
    require(chart_audio_committed_cancel_authority_exact(exact_cancel),
        "exact committed chart/audio cancellation authority was rejected");
    auto reject_cancel_drift = [&](bool ChartAudioCommittedCancelFacts::*field,
                                   const char* message) {
        auto drift = exact_cancel;
        drift.*field = false;
        require(!chart_audio_committed_cancel_authority_exact(drift), message);
    };
    reject_cancel_drift(
        &ChartAudioCommittedCancelFacts::frozen_lease_active_and_exact,
        "committed cancellation accepted frozen-lease mismatch");
    reject_cancel_drift(
        &ChartAudioCommittedCancelFacts::native_arm_not_attempted,
        "committed cancellation accepted a native arm attempt");
    reject_cancel_drift(
        &ChartAudioCommittedCancelFacts::unpublished_token_and_guard_exact,
        "committed cancellation accepted token/guard drift");
    ChartAudioAdmissionCoordinator chart_transaction;
    std::vector<const char*> activation_order;
    activation_order.push_back("claim");
    require(chart_transaction.reserve(chart_admission),
        "production chart/audio coordinator rejected exact claim admission");
    activation_order.push_back("admission");
    require(chart_transaction.finish_chart_write(true),
        "production chart/audio coordinator rejected exact chart write");
    activation_order.push_back("chart_write");
    require(chart_transaction.finish_audio_commit(true)
            && chart_transaction.original_may_consume_custom_chart(),
        "production chart/audio coordinator rejected exact committed admission");
    activation_order.push_back("audio_commit");
    require(activation_order == std::vector<const char*>{
                "claim", "admission", "chart_write", "audio_commit"}
            && chart_transaction.original_may_consume_custom_chart(),
        "claim/admission/chart/audio ordering drifted");
    require(!chart_transaction.reserve(chart_admission)
            && chart_transaction.state() == ChartAudioAdmissionState::Cancelled
            && !chart_transaction.finish_chart_write(true)
            && !chart_transaction.finish_audio_commit(true)
            && !chart_transaction.original_may_consume_custom_chart(),
        "duplicate coordinator transition did not cancel fail closed");
    ChartAudioAdmissionCoordinator chart_write_failure;
    require(chart_write_failure.reserve(chart_admission)
            && !chart_write_failure.finish_chart_write(false)
            && !chart_write_failure.original_may_consume_custom_chart(),
        "chart write failure exposed custom chart to original expand");
    ChartAudioAdmissionCoordinator arm_commit_failure;
    require(arm_commit_failure.reserve(chart_admission)
            && arm_commit_failure.finish_chart_write(true)
            && !arm_commit_failure.finish_audio_commit(false)
            && !arm_commit_failure.original_may_consume_custom_chart(),
        "audio arm failure exposed custom chart to original expand");
    auto exit_after_reservation = chart_admission;
    ChartAudioAdmissionCoordinator exit_race;
    require(exit_race.reserve(exit_after_reservation),
        "exact reservation failed before exit race");
    exit_after_reservation.exit_and_unresolved_clear = false;
    exit_race.cancel();
    require(!chart_audio_admission_exact(exit_after_reservation)
            && !exit_race.original_may_consume_custom_chart(),
        "exit beginning after reservation left custom chart consumable");
    struct ProtectedChartWriteFixture {
        std::array<uint8_t, 3> bytes{1, 2, 3};
        std::array<uint8_t, 3> originals{1, 2, 3};
        std::array<uint8_t, 3> patched{4, 5, 6};
        bool journal_owned = false;
        bool journal_retained = false;
        size_t attempted = 0;

        ChartMutationTransactionOutcome run(
            const size_t fail_write, const size_t fail_rollback) noexcept
        {
            journal_owned = true;
            for (size_t index = 0; index < bytes.size(); ++index) {
                attempted = index + 1;
                if (index == fail_write) {
                    bool restored = true;
                    for (size_t rollback = attempted; rollback != 0; --rollback) {
                        if (rollback - 1 == fail_rollback) {
                            restored = false;
                        } else {
                            bytes[rollback - 1] = originals[rollback - 1];
                        }
                    }
                    journal_retained = !restored;
                    if (restored) journal_owned = false;
                    return chart_mutation_rollback_outcome(restored, true);
                }
                bytes[index] = patched[index];
            }
            return ChartMutationTransactionOutcome::CustomCommitted;
        }
    };
    ProtectedChartWriteFixture partial_rollback;
    const auto partial_clean = partial_rollback.run(1, SIZE_MAX);
    int native_expand_calls = 0;
    if (chart_expand_original_allowed(partial_clean)) ++native_expand_calls;
    require(partial_clean == ChartMutationTransactionOutcome::NativePristine
            && partial_rollback.bytes == partial_rollback.originals
            && !partial_rollback.journal_owned
            && native_expand_calls == 1,
        "partial chart write did not fully roll back to one native expand");
    ProtectedChartWriteFixture partial_unresolved;
    const auto partial_failed = partial_unresolved.run(1, 0);
    native_expand_calls = 0;
    if (chart_expand_original_allowed(partial_failed)) ++native_expand_calls;
    require(partial_failed == ChartMutationTransactionOutcome::MutationUnresolved
            && partial_unresolved.journal_owned
            && partial_unresolved.journal_retained
            && native_expand_calls == 0,
        "failed chart rollback lost ownership or invoked original expand");
    require(chart_mutation_rollback_outcome(false, true)
                == ChartMutationTransactionOutcome::MutationUnresolved
            && chart_mutation_rollback_outcome(true, false)
                == ChartMutationTransactionOutcome::MutationUnresolved
            && chart_mutation_rollback_outcome(true, true)
                == ChartMutationTransactionOutcome::NativePristine,
        "audio-commit/invariant rollback accepted stranded chart or route");
    int commit_failure_original_calls = 0;
    const auto commit_failure_clean
        = chart_mutation_rollback_outcome(true, true);
    const auto commit_failure_unresolved
        = chart_mutation_rollback_outcome(false, true);
    const auto invariant_failure_unresolved
        = chart_mutation_rollback_outcome(true, false);
    if (chart_expand_original_allowed(commit_failure_clean)) {
        ++commit_failure_original_calls;
    }
    if (chart_expand_original_allowed(commit_failure_unresolved)) {
        ++commit_failure_original_calls;
    }
    if (chart_expand_original_allowed(invariant_failure_unresolved)) {
        ++commit_failure_original_calls;
    }
    require(commit_failure_original_calls == 1,
        "original expand count was not one for clean rollback and zero for unresolved rollback");
    require(chart_protected_exception_outcome(true, true)
                == ChartMutationTransactionOutcome::NativePristine
            && chart_protected_exception_outcome(false, true)
                == ChartMutationTransactionOutcome::MutationUnresolved
            && chart_protected_exception_outcome(true, false)
                == ChartMutationTransactionOutcome::MutationUnresolved,
        "protected chart exception boundary accepted uncertain rollback");
    require(chart_transaction_journal_may_clear(true, false, false)
            && chart_transaction_journal_may_clear(true, true, true)
            && !chart_transaction_journal_may_clear(true, true, false)
            && !chart_transaction_journal_may_clear(false, true, true),
        "cross-resource chart journal cleared before chart and audio rollback committed");
    int diagnostic_attempts = 0;
    chart_diagnostic_best_effort([&]() {
        ++diagnostic_attempts;
        throw std::bad_alloc{};
    });
    require(diagnostic_attempts == 1,
        "best-effort post-ownership chart diagnostic did not contain allocation failure");
    partial_unresolved.bytes = partial_unresolved.originals;
    partial_unresolved.journal_owned = false;
    partial_unresolved.journal_retained = false;
    require(partial_unresolved.bytes == partial_unresolved.originals
            && !partial_unresolved.journal_owned,
        "retained chart journal could not complete exact later cleanup");
    std::array<uint8_t, 96> chunk_original{};
    std::array<uint8_t, 96> chunk_patched{};
    std::array<uint8_t, 96> chunk_live{};
    chunk_original.fill(0x11);
    chunk_patched.fill(0x22);
    chunk_live = chunk_patched;
    bool chunk_read_fail = false;
    bool chunk_write_fail = false;
    const auto chunk_read = [&](const size_t offset, uint8_t* destination,
                                const size_t count) noexcept {
        if (chunk_read_fail || offset + count > chunk_live.size()) return false;
        std::memcpy(destination, chunk_live.data() + offset, count);
        return true;
    };
    const auto chunk_write = [&](const uint8_t* source,
                                 const size_t count) noexcept {
        if (chunk_write_fail || count != chunk_live.size()) return false;
        std::memcpy(chunk_live.data(), source, count);
        return true;
    };
    require(chart_chunked_restore_exact(
                chunk_original.data(), chunk_patched.data(), chunk_live.size(),
                false, chunk_read, chunk_write)
            && chunk_live == chunk_original,
        "production chunked chart restore rejected exact patched bytes");
    chunk_live = chunk_patched;
    chunk_read_fail = true;
    require(!chart_chunked_restore_exact(
                chunk_original.data(), chunk_patched.data(), chunk_live.size(),
                false, chunk_read, chunk_write),
        "production chunked chart restore accepted unreadable bytes");
    chunk_read_fail = false;
    chunk_write_fail = true;
    require(!chart_chunked_restore_exact(
                chunk_original.data(), chunk_patched.data(), chunk_live.size(),
                false, chunk_read, chunk_write)
            && chunk_live == chunk_patched,
        "production chunked chart restore accepted failed rollback write");
    chunk_write_fail = false;
    chunk_live.fill(0x33);
    require(!chart_chunked_restore_exact(
                chunk_original.data(), chunk_patched.data(), chunk_live.size(),
                false, chunk_read, chunk_write)
            && chunk_live.front() == 0x33,
        "production chunked chart restore overwrote mismatched native bytes");
}

} // namespace

int main()
{
    using namespace ff7r::piano::game;
    static_assert(!std::is_copy_constructible_v<SelectionActivationClaim>);
    static_assert(!std::is_copy_assignable_v<SelectionActivationClaim>);
    static_assert(std::is_nothrow_move_constructible_v<SelectionActivationClaim>);
    static_assert(std::is_nothrow_move_assignable_v<SelectionActivationClaim>);
    MenuSessionRevocationFacts facts{SelectionActivationReservationState::Reserved,
        true, true, true, true, true};
    require(menu_session_revocation_prestate_exact(facts),
        "exact menu-session reservation prestate was rejected");
    for (bool MenuSessionRevocationFacts::* member : {
        &MenuSessionRevocationFacts::expected_selection_valid,
        &MenuSessionRevocationFacts::active_session_exact,
        &MenuSessionRevocationFacts::registry_selection_exact,
        &MenuSessionRevocationFacts::reservation_session_exact,
        &MenuSessionRevocationFacts::reservation_selection_exact}) {
        auto drift = facts; drift.*member = false;
        require(!menu_session_revocation_prestate_exact(drift),
            "selection/session drift admitted menu revocation mutation");
    }
    facts.state = SelectionActivationReservationState::Consumed;
    require(!menu_session_revocation_prestate_exact(facts),
        "consumed reservation admitted menu revocation mutation");

    SelectionActivationTerminalFacts terminal{};
    terminal.reason = SelectionActivationTerminalReason::ListClose;
    terminal.state = SelectionActivationReservationState::Reserved;
    terminal.transfer_state = SelectionActivationTransferState::Pending;
    terminal.handoff_confirmed = true;
    terminal.active_session_exact = true;
    terminal.widget_uobject_exact = true;
    terminal.reservation_generation_exact = true;
    terminal.reservation_session_exact = true;
    terminal.reservation_context_exact = true;
    terminal.reservation_bgm_controller_exact = true;
    terminal.reservation_bgm_identity_exact = true;
    terminal.reservation_substrate_exact = true;
    terminal.registry_selection_exact = true;
    terminal.reservation_selection_exact = true;
    require(selection_activation_terminal_decision(terminal)
            == SelectionActivationTerminalDecision::Preserved,
        "confirmed exact shared list close did not preserve activation transfer");
    for (bool SelectionActivationTerminalFacts::* member : {
        &SelectionActivationTerminalFacts::active_session_exact,
        &SelectionActivationTerminalFacts::widget_uobject_exact,
        &SelectionActivationTerminalFacts::reservation_generation_exact,
        &SelectionActivationTerminalFacts::reservation_session_exact,
        &SelectionActivationTerminalFacts::reservation_context_exact,
        &SelectionActivationTerminalFacts::reservation_bgm_controller_exact,
        &SelectionActivationTerminalFacts::reservation_bgm_identity_exact,
        &SelectionActivationTerminalFacts::reservation_substrate_exact,
        &SelectionActivationTerminalFacts::registry_selection_exact,
        &SelectionActivationTerminalFacts::reservation_selection_exact}) {
        auto drift = terminal; drift.*member = false;
        require(selection_activation_terminal_decision(drift)
                != SelectionActivationTerminalDecision::Preserved,
            "terminal identity drift preserved activation transfer");
    }
    auto unconfirmed = terminal; unconfirmed.handoff_confirmed = false;
    require(selection_activation_terminal_decision(unconfirmed)
            == SelectionActivationTerminalDecision::Revoked,
        "unconfirmed shared list close did not revoke");
    auto state5 = terminal;
    state5.reason = SelectionActivationTerminalReason::State5Exit;
    require(selection_activation_terminal_decision(state5)
            == SelectionActivationTerminalDecision::Preserved,
        "exact state-5 fallback did not preserve activation transfer");
    auto destructor = terminal;
    destructor.reason = SelectionActivationTerminalReason::ControllerDestructor;
    require(selection_activation_terminal_decision(destructor)
            == SelectionActivationTerminalDecision::Revoked,
        "confirmed destructor preserved activation transfer");
    terminal.state = SelectionActivationReservationState::Consumed;
    require(selection_activation_terminal_decision(terminal)
            == SelectionActivationTerminalDecision::Rejected,
        "consumed reservation admitted terminal mutation");
    terminal.state = SelectionActivationReservationState::Reserved;
    for (const auto transferred : {SelectionActivationTransferState::Preserved,
             SelectionActivationTransferState::ClaimedFrozen,
             SelectionActivationTransferState::Consumed,
             SelectionActivationTransferState::AdmissionOwned}) {
        auto duplicate = terminal;
        duplicate.transfer_state = transferred;
        require(selection_activation_terminal_decision(duplicate)
                == SelectionActivationTerminalDecision::Rejected,
            "duplicate close/state-exit admitted destructive ownership mutation");
    }
    auto first_failure = terminal;
    first_failure.widget_uobject_exact = false;
    first_failure.reservation_bgm_identity_exact = false;
    require(selection_activation_terminal_first_failure(first_failure)
            == SelectionActivationTerminalFailure::WidgetUObject,
        "terminal diagnostic did not report the first failed component");

    SelectionActivationClaimFacts claim{};
    claim.reservation_state = SelectionActivationReservationState::Reserved;
    claim.transfer_state = SelectionActivationTransferState::Preserved;
    claim.handoff_confirmed = true;
    claim.no_active_menu_session = true;
    claim.reservation_generation_nonzero = true;
    claim.reservation_generation_not_wrapped = true;
    claim.menu_session_generation_nonzero = true;
    claim.menu_session_generation_not_wrapped = true;
    claim.selection_generation_nonzero = true;
    claim.selection_generation_not_wrapped = true;
    claim.registry_selection_exact = true;
    claim.bgm_controller_exact = true;
    claim.bgm_identity_exact = true;
    claim.caller_exact = true;
    claim.wrapper_safe_read = true;
    claim.wrapper_exact = true;
    claim.substrate_exact_if_active = true;
    require(selection_activation_claim_prestate_exact(claim),
        "exact preserved activation claim was rejected");
    for (bool SelectionActivationClaimFacts::* member : {
        &SelectionActivationClaimFacts::handoff_confirmed,
        &SelectionActivationClaimFacts::no_active_menu_session,
        &SelectionActivationClaimFacts::reservation_generation_nonzero,
        &SelectionActivationClaimFacts::reservation_generation_not_wrapped,
        &SelectionActivationClaimFacts::menu_session_generation_nonzero,
        &SelectionActivationClaimFacts::menu_session_generation_not_wrapped,
        &SelectionActivationClaimFacts::selection_generation_nonzero,
        &SelectionActivationClaimFacts::selection_generation_not_wrapped,
        &SelectionActivationClaimFacts::registry_selection_exact,
        &SelectionActivationClaimFacts::bgm_controller_exact,
        &SelectionActivationClaimFacts::bgm_identity_exact,
        &SelectionActivationClaimFacts::caller_exact,
        &SelectionActivationClaimFacts::wrapper_safe_read,
        &SelectionActivationClaimFacts::wrapper_exact,
        &SelectionActivationClaimFacts::substrate_exact_if_active}) {
        auto drift = claim; drift.*member = false;
        require(!selection_activation_claim_prestate_exact(drift),
            "drifted activation claim acquired ownership");
    }
    auto claim_first_failure = claim;
    claim_first_failure.caller_exact = false;
    claim_first_failure.wrapper_safe_read = false;
    require(selection_activation_claim_first_failure(claim_first_failure)
            == SelectionActivationClaimFailure::Caller,
        "claim diagnostic did not report caller as first failure");
    auto duplicate_claim = claim;
    duplicate_claim.transfer_state = SelectionActivationTransferState::ClaimedFrozen;
    require(!selection_activation_claim_prestate_exact(duplicate_claim),
        "duplicate activation claim acquired ownership");
    auto generation_overflow = claim;
    generation_overflow.selection_generation_not_wrapped = false;
    require(!selection_activation_claim_prestate_exact(generation_overflow),
        "activation claim admitted generation overflow");
    std::mutex claim_mutex;
    SelectionActivationTransferState race_state
        = SelectionActivationTransferState::Preserved;
    std::atomic_int race_winners{0};
    auto contender = [&]() {
        std::lock_guard<std::mutex> lock(claim_mutex);
        auto facts = claim;
        facts.transfer_state = race_state;
        if (selection_activation_claim_prestate_exact(facts)) {
            const auto transition = selection_activation_ownership_transition(
                race_state, SelectionActivationOwnershipEvent::ClaimFrozen, true);
            if (transition.accepted) {
                race_state = transition.next;
                race_winners.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    std::thread first(contender), second(contender);
    first.join(); second.join();
    require(race_winners.load(std::memory_order_relaxed) == 1,
        "activation claim race did not have exactly one winner");

    auto lifecycle_state = SelectionActivationTransferState::Pending;
    auto transition = selection_activation_ownership_transition(lifecycle_state,
        SelectionActivationOwnershipEvent::Preserve, true);
    require(transition.accepted
            && transition.next == SelectionActivationTransferState::Preserved,
        "exact terminal preservation did not enter Preserved");
    lifecycle_state = transition.next;
    transition = selection_activation_ownership_transition(lifecycle_state,
        SelectionActivationOwnershipEvent::ClaimFrozen, true);
    require(transition.accepted
            && transition.next == SelectionActivationTransferState::ClaimedFrozen,
        "exact claim did not enter ClaimedFrozen");
    lifecycle_state = transition.next;
    const auto abandon = selection_activation_ownership_transition(lifecycle_state,
        SelectionActivationOwnershipEvent::Rollback, true);
    require(abandon.accepted
            && abandon.rollback
                == SelectionActivationRollbackPlan::ReservationAndFrozenProfile,
        "claim abandonment did not plan exact reservation/profile rollback");
    const auto stale_abandon = selection_activation_ownership_transition(
        lifecycle_state, SelectionActivationOwnershipEvent::Rollback, false);
    require(!stale_abandon.accepted
            && stale_abandon.rollback == SelectionActivationRollbackPlan::None,
        "stale claim destruction could mutate exact ownership");

    transition = selection_activation_ownership_transition(lifecycle_state,
        SelectionActivationOwnershipEvent::Consume, true);
    require(transition.accepted
            && transition.next == SelectionActivationTransferState::Consumed,
        "exact claim consumption did not enter pre-admission ownership");
    lifecycle_state = transition.next;
    const auto early_failure = selection_activation_ownership_transition(
        lifecycle_state, SelectionActivationOwnershipEvent::Rollback, true);
    require(early_failure.accepted
            && early_failure.rollback
                == SelectionActivationRollbackPlan::ReservationAndFrozenProfile,
        "consumed claim early failure omitted exact profile rollback");

    transition = selection_activation_ownership_transition(lifecycle_state,
        SelectionActivationOwnershipEvent::TransferToAdmission, true);
    require(transition.accepted
            && transition.next == SelectionActivationTransferState::AdmissionOwned,
        "valid admission Impl did not acquire consumed ownership");
    lifecycle_state = transition.next;
    const auto uncommitted = selection_activation_ownership_transition(
        lifecycle_state, SelectionActivationOwnershipEvent::Rollback, true);
    require(uncommitted.accepted
            && uncommitted.rollback
                == SelectionActivationRollbackPlan::ReservationAndFrozenProfile,
        "uncommitted admission omitted exact profile rollback");
    const auto committed = selection_activation_ownership_transition(lifecycle_state,
        SelectionActivationOwnershipEvent::Commit, true);
    require(committed.accepted
            && committed.next == SelectionActivationTransferState::Retired
            && committed.rollback == SelectionActivationRollbackPlan::None,
        "committed admission retained claim rollback ownership");

    const auto reopen_unclaimed = selection_activation_ownership_transition(
        SelectionActivationTransferState::Preserved,
        SelectionActivationOwnershipEvent::Rollback, true);
    const auto destructor_claimed = selection_activation_ownership_transition(
        SelectionActivationTransferState::ClaimedFrozen,
        SelectionActivationOwnershipEvent::Rollback, true);
    require(reopen_unclaimed.rollback
                == SelectionActivationRollbackPlan::ReservationOnly
            && destructor_claimed.rollback
                == SelectionActivationRollbackPlan::ReservationAndFrozenProfile,
        "reopen/destructor rollback plan lost claimed/unclaimed distinction");

    SelectionActivationReservationMachine transfer;
    require(transfer.reserve(), "transfer reservation failed");
    const uint64_t transfer_generation = transfer.generation();
    require(transfer.consume(transfer_generation),
        "preserved confirmed transfer did not consume once");
    require(!transfer.consume(transfer_generation),
        "preserved confirmed transfer consumed twice");
    SelectionActivationReservationMachine never_consumed;
    require(never_consumed.reserve(), "reclaim reservation failed");
    never_consumed.revoke();
    require(never_consumed.state() == SelectionActivationReservationState::Revoked,
        "terminal/shutdown did not reclaim unconsumed transfer");
    require(std::strcmp(selection_activation_terminal_reason_name(
                SelectionActivationTerminalReason::State5Exit), "state5_exit") == 0
            && std::strcmp(selection_activation_terminal_reason_name(
                SelectionActivationTerminalReason::ListClose), "list_close") == 0
            && std::strcmp(selection_activation_terminal_failure_name(
                SelectionActivationTerminalFailure::WidgetUObject),
                "list_widget_uobject") == 0
            && std::strcmp(selection_activation_claim_failure_name(
                SelectionActivationClaimFailure::WrapperSafeRead),
                "wrapper_safe_read") == 0
            && std::strcmp(selection_activation_list_close_classification(
                true, true, SelectionActivationTerminalDecision::Preserved),
                "activation_confirmed") == 0
            && std::strcmp(selection_activation_list_close_classification(
                true, false, SelectionActivationTerminalDecision::Rejected),
                "cancel_unowned") == 0
            && std::strcmp(selection_activation_list_close_classification(
                false, true, SelectionActivationTerminalDecision::Preserved),
                "native_close_rejected") == 0
            && std::strcmp(selection_activation_terminal_decision_name(
                SelectionActivationTerminalDecision::Preserved), "preserved") == 0
            && std::strcmp(selection_activation_terminal_decision_name(
                SelectionActivationTerminalDecision::Revoked), "revoked") == 0,
        "terminal diagnostic facts changed");
    test_selection_substrate_policy();
    std::cout << "selection_substrate_policy_selftest: ok\n";
    return 0;
}
