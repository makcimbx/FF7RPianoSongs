#include "game/pause_resume_policy.h"
#include "game/onmemory_bank_lifecycle.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "pause_resume_policy_selftest: " << message << '\n';
        std::exit(1);
    }
}

void test_durable_pause_resume_bank_policy()
{
    using namespace ff7r::piano::game;
    auto storage = std::make_shared<int>(7);
    SongDescriptor song;
    SongDescriptor other_song;
    SongDifficultyProfile profile;
    SongDifficultyProfile other_profile;
    SelectionSnapshot retained{11, storage, &song, &profile, 1, 2, 3};
    SelectionSnapshot current = retained;
    current.generation = 29;
    auto continuity = classify_pause_resume_selection_continuity(retained, current);
    require(continuity.semantics == PauseResumeSelectionSemantics::Match
            && !continuity.revision_same,
        "pause/resume semantic selection rejected revision-only change");
    const auto expect_selection_drift = [&](SelectionSnapshot changed,
                                             const char* message) {
        require(classify_pause_resume_selection_continuity(retained, changed).semantics
                != PauseResumeSelectionSemantics::Match,
            message);
    };
    SelectionSnapshot changed = current;
    changed.storage = std::make_shared<int>(7);
    expect_selection_drift(changed, "pause/resume accepted different storage");
    changed = current; changed.song = &other_song;
    expect_selection_drift(changed, "pause/resume accepted different song");
    changed = current; changed.profile = &other_profile;
    expect_selection_drift(changed, "pause/resume accepted different profile");
    changed = current; ++changed.profile_index;
    expect_selection_drift(changed, "pause/resume accepted different profile index");
    changed = current; ++changed.visible_index;
    expect_selection_drift(changed, "pause/resume accepted different visible index");
    changed = current; ++changed.base_slot;
    expect_selection_drift(changed, "pause/resume accepted different base slot");

    PauseResumeBankSetFacts set_facts{
        true, true, true, true, true, true, true,
        true, true, true, true, true, true,
    };
    require(pause_resume_bank_set_eligible(set_facts),
        "complete durable pause/resume Set facts rejected");
    bool PauseResumeBankSetFacts::* const set_members[] = {
        &PauseResumeBankSetFacts::awaiting_resume,
        &PauseResumeBankSetFacts::owner_tick_active,
        &PauseResumeBankSetFacts::exact_tick_nonce,
        &PauseResumeBankSetFacts::outside_play_setup,
        &PauseResumeBankSetFacts::resume_stop_observed,
        &PauseResumeBankSetFacts::selection_semantics_match,
        &PauseResumeBankSetFacts::exact_controller,
        &PauseResumeBankSetFacts::exact_sound_identity,
        &PauseResumeBankSetFacts::exact_canonical_owner,
        &PauseResumeBankSetFacts::exact_lifecycle,
        &PauseResumeBankSetFacts::no_release_pending,
        &PauseResumeBankSetFacts::sidecar_ready,
        &PauseResumeBankSetFacts::runtime_ready,
    };
    for (auto member : set_members) {
        auto rejected = set_facts;
        rejected.*member = false;
        require(!pause_resume_bank_set_eligible(rejected),
            "incomplete durable pause/resume Set facts accepted");
    }
    PauseResumeBankPlayFacts play_facts{true, true, true, true, true, true, true, true};
    require(pause_resume_bank_play_eligible(play_facts),
        "complete durable pause/resume Play facts rejected");

    const auto exact_stop = pause_resume_stop_outcome(
        PauseResumeBankPhase::AwaitingResumeOrListReturn, true, true);
    require(exact_stop.phase == PauseResumeBankPhase::ResumeStopObserved
            && exact_stop.consume_cycle,
        "exact resume Stop did not advance durable session");
    const auto mismatched_stop = pause_resume_stop_outcome(
        PauseResumeBankPhase::AwaitingResumeOrListReturn, true, false);
    require(mismatched_stop.phase == PauseResumeBankPhase::Failed
            && mismatched_stop.consume_cycle,
        "first mismatching resume Stop remained retryable");

    uint64_t inspected_owner = 0x111;
    bool rollback_called = false;
    const auto applied = apply_pause_resume_patch_transaction(
        [&] { inspected_owner = 0x222; return true; },
        [&] { return inspected_owner == 0x222; },
        [&] { return inspected_owner == 0x111; },
        [&] {
            rollback_called = true;
            inspected_owner = 0x111;
            return PauseResumeRestorationProof{true, true};
        });
    require(applied == PauseResumePatchApplyOutcome::AppliedVerified
            && !rollback_called && inspected_owner == 0x222,
        "verified durable pause/resume owner apply failed");
    int order = 0;
    const auto forwarded = coordinate_pause_resume_native_forward(
        [&] { require(order++ == 0, "native order changed"); return true; },
        [&] { require(order++ == 1, "verify order changed"); return true; },
        [&] {
            require(order++ == 2, "restore order changed");
            inspected_owner = 0x111;
            return true;
        }, true);
    require(forwarded == PauseResumeNativeForwardResult::Succeeded
            && order == 3 && inspected_owner == 0x111,
        "durable pause/resume Play did not restore canonical owner");
    int native_calls = 0;
    require(dispatch_pause_resume_detour(
            {PauseResumeDetourDisposition::OriginalAlreadyForwarded, 1, true},
            [&] { ++native_calls; return true; })
            && native_calls == 0,
        "already-forwarded durable Play called native twice");
    require(dispatch_pause_resume_detour(
            {PauseResumeDetourDisposition::ForwardAfterVerifiedCanonical, 0, true},
            [&] { ++native_calls; return true; })
            && native_calls == 1,
        "verified canonical fallback did not call native exactly once");
    require(dispatch_pause_resume_detour(
            {PauseResumeDetourDisposition::SuppressFailClosed, 0, false},
            [&] { ++native_calls; return true; })
            && native_calls == 1,
        "fail-closed durable disposition forwarded native audio");
    int fault_order = 0;
    const auto native_fault = coordinate_pause_resume_native_forward(
        [&] { ++fault_order; return false; },
        [&] { ++fault_order; return true; },
        [&] { ++fault_order; return true; },
        true);
    require(native_fault == PauseResumeNativeForwardResult::NativeFailedRestored
            && fault_order == 2,
        "native fault did not restore canonical without verification/forward retry");

    uint64_t session_epoch = 0;
    uint64_t cycle_epoch = 0;
    session_epoch = next_pause_resume_bank_epoch(session_epoch);
    for (int cycle = 0; cycle < 3; ++cycle) {
        cycle_epoch = next_pause_resume_bank_epoch(cycle_epoch);
        require(pause_resume_epoch_matches(session_epoch, cycle_epoch,
                session_epoch, cycle_epoch),
            "durable pause/resume cycle epoch mismatch");
    }
    require(!pause_resume_epoch_matches(session_epoch, cycle_epoch,
            session_epoch, next_pause_resume_bank_epoch(cycle_epoch)),
        "stale durable pause/resume action consumed newer cycle");

    PauseResumeReleaseAuthorityFacts release_authority{
        true, true, true, true, true, true, true, true, true, true};
    require(pause_resume_release_authority_valid(release_authority),
        "exact fresh release authority was rejected");
    bool PauseResumeReleaseAuthorityFacts::* const authority_facts[] = {
        &PauseResumeReleaseAuthorityFacts::exact_session_cycle,
        &PauseResumeReleaseAuthorityFacts::exact_route_snapshot,
        &PauseResumeReleaseAuthorityFacts::route_released,
        &PauseResumeReleaseAuthorityFacts::playback_released,
        &PauseResumeReleaseAuthorityFacts::cleanup_released,
        &PauseResumeReleaseAuthorityFacts::exact_lifecycle,
        &PauseResumeReleaseAuthorityFacts::exact_lifecycle_epoch,
        &PauseResumeReleaseAuthorityFacts::owner_canonical_or_zero,
        &PauseResumeReleaseAuthorityFacts::exact_cleanup_generation,
        &PauseResumeReleaseAuthorityFacts::no_release_or_shutdown,
    };
    for (auto fact : authority_facts) {
        auto drifted = release_authority;
        drifted.*fact = false;
        require(!pause_resume_release_authority_valid(drifted),
            "release authority accepted playback/cleanup/route/lifecycle drift");
    }

    const auto rebound_list = pause_resume_list_return_outcome(
        PauseResumeBankPhase::OwnerRebound);
    require(rebound_list.phase == PauseResumeBankPhase::OwnerRebound
            && rebound_list.restore_owner,
        "list return changed authoritative rebound phase before restoration");
    const auto restored_list = pause_resume_list_restore_outcome(
        PauseResumeBankPhase::OwnerRebound, true);
    const auto failed_list = pause_resume_list_restore_outcome(
        PauseResumeBankPhase::OwnerRebound, false);
    require(restored_list.continue_list_return
            && restored_list.phase == PauseResumeBankPhase::ExitPending
            && !failed_list.continue_list_return
            && failed_list.phase == PauseResumeBankPhase::Failed,
        "list return restoration ordering did not fail closed");
    const auto pending_probe = pause_resume_list_return_outcome(
        PauseResumeBankPhase::RetirementCandidate);
    require(pending_probe.phase == PauseResumeBankPhase::RetirementCandidate
            && !pending_probe.release_ready,
        "list return invalidated an in-flight initial hold probe");
    const auto held_after_exit = pause_resume_initial_hold_outcome(
        true, true, true, true);
    const auto held_before_exit = pause_resume_initial_hold_outcome(
        true, true, true, false);
    require(held_after_exit.release_ready && !held_after_exit.resume_ready
            && held_before_exit.resume_ready && !held_before_exit.release_ready
            && !pause_resume_initial_hold_outcome(false, true, true, true).release_ready,
        "initial hold/list-return interleaving did not select exactly one path");

    std::vector<std::string> coordinator_events;
    int operation_lock_depth = 1;
    bool callback_lifetime = true;
    PauseResumeBankPhase coordinated_phase = PauseResumeBankPhase::OwnerRebound;
    int list_cleanup_count = 0;
    int list_fail_count = 0;
    const auto coordinated_list = coordinate_pause_resume_list_return(
        coordinated_phase,
        [&] { coordinator_events.emplace_back("persist"); return true; },
        [&] {
            coordinator_events.emplace_back("restore");
            return callback_lifetime && operation_lock_depth == 1
                && coordinated_phase == PauseResumeBankPhase::OwnerRebound;
        },
        [&](PauseResumeBankPhase expected, PauseResumeBankPhase next) {
            coordinator_events.emplace_back("revalidate_transition");
            if (coordinated_phase != expected) return false;
            coordinated_phase = next;
            return true;
        },
        [&](bool release_ready) {
            coordinator_events.emplace_back("cleanup");
            ++list_cleanup_count;
            return !release_ready && coordinated_phase == PauseResumeBankPhase::ExitPending;
        },
        [&] { coordinator_events.emplace_back("fail"); ++list_fail_count; });
    require(coordinated_list.committed && list_cleanup_count == 1
            && list_fail_count == 0
            && coordinator_events == std::vector<std::string>{"persist", "restore",
                "revalidate_transition", "cleanup"},
        "production list coordinator violated restore/revalidate/transition/cleanup order");

    coordinated_phase = PauseResumeBankPhase::OwnerRebound;
    list_cleanup_count = 0;
    const auto failed_restore_list = coordinate_pause_resume_list_return(
        coordinated_phase, [] { return true; }, [] { return false; },
        [&](PauseResumeBankPhase, PauseResumeBankPhase) { return true; },
        [&](bool) { ++list_cleanup_count; return true; }, [] {});
    require(failed_restore_list.failed && list_cleanup_count == 0,
        "list coordinator continued cleanup after restoration failure");
    coordinated_phase = PauseResumeBankPhase::OwnerRebound;
    const auto stale_after_restore = coordinate_pause_resume_list_return(
        coordinated_phase, [] { return true; }, [&] {
            coordinated_phase = PauseResumeBankPhase::Failed;
            return true;
        },
        [&](PauseResumeBankPhase expected, PauseResumeBankPhase) {
            return coordinated_phase == expected;
        },
        [&](bool) { ++list_cleanup_count; return true; }, [] {});
    require(stale_after_restore.failed && list_cleanup_count == 0,
        "list coordinator accepted stale state after canonical restoration");

    bool persisted_exit = false;
    coordinated_phase = PauseResumeBankPhase::RetirementCandidate;
    list_cleanup_count = 0;
    const auto candidate_list = coordinate_pause_resume_list_return(
        coordinated_phase, [&] { persisted_exit = true; return true; },
        [] { return false; },
        [&](PauseResumeBankPhase expected, PauseResumeBankPhase next) {
            return expected == PauseResumeBankPhase::RetirementCandidate
                && next == expected && coordinated_phase == expected;
        },
        [&](bool release_ready) { ++list_cleanup_count; return !release_ready; },
        [] {});
    require(candidate_list.committed && persisted_exit && list_cleanup_count == 1
            && coordinated_phase == PauseResumeBankPhase::RetirementCandidate,
        "list coordinator lost in-flight deferred hold action");
    coordinated_phase = PauseResumeBankPhase::AwaitingResumeOrListReturn;
    int list_release_schedule_count = 0;
    const auto awaiting_list = coordinate_pause_resume_list_return(
        coordinated_phase, [] { return true; }, [] { return false; },
        [&](PauseResumeBankPhase expected, PauseResumeBankPhase next) {
            if (coordinated_phase != expected) return false;
            coordinated_phase = next;
            return true;
        },
        [&](bool release_ready) {
            if (release_ready) ++list_release_schedule_count;
            return release_ready;
        }, [] {});
    require(awaiting_list.committed && awaiting_list.action_exposed
            && list_release_schedule_count == 1,
        "list coordinator did not schedule retained-bank release exactly once");

    int hold_commit_count = 0;
    int hold_fail_count = 0;
    int exposed_release_count = 0;
    bool hold_exit_requested = false;
    bool hold_action_current = true;
    const auto probe_before_list = coordinate_pause_resume_initial_hold(
        true, true,
        [&](bool& exit) { exit = hold_exit_requested; return hold_action_current; },
        [&](const PauseResumeInitialHoldOutcome& outcome) {
            ++hold_commit_count;
            return outcome.resume_ready && !outcome.release_ready;
        },
        [&] { ++hold_fail_count; });
    require(probe_before_list.committed && probe_before_list.resume_ready
            && hold_commit_count == 1 && hold_fail_count == 0,
        "deferred hold coordinator rejected probe-before-list ordering");
    hold_exit_requested = true;
    const auto list_before_probe = coordinate_pause_resume_initial_hold(
        true, true,
        [&](bool& exit) { exit = hold_exit_requested; return hold_action_current; },
        [&](const PauseResumeInitialHoldOutcome& outcome) {
            ++hold_commit_count;
            if (outcome.release_ready) ++exposed_release_count;
            hold_action_current = false;
            return outcome.release_ready;
        },
        [&] { ++hold_fail_count; });
    const auto duplicate_probe = coordinate_pause_resume_initial_hold(
        true, true,
        [&](bool& exit) { exit = true; return hold_action_current; },
        [&](const PauseResumeInitialHoldOutcome&) { ++exposed_release_count; return true; },
        [&] { ++hold_fail_count; });
    require(list_before_probe.release_action_exposed && !duplicate_probe.committed
            && exposed_release_count == 1 && hold_fail_count == 1,
        "deferred hold coordinator exposed duplicate or stale release action");
    operation_lock_depth = 0;
    int hold_release_execute_count = 0;
    require(execute_pause_resume_deferred_action_once(
                list_before_probe.release_action_exposed, [&] {
                    ++hold_release_execute_count;
                    return operation_lock_depth == 0 && callback_lifetime;
                }) && hold_release_execute_count == 1,
        "deferred hold release did not execute once after lock drop");
    const auto failed_kind_hold = coordinate_pause_resume_initial_hold(
        false, true, [&](bool& exit) { exit = false; return true; },
        [&](const PauseResumeInitialHoldOutcome&) { ++hold_commit_count; return true; },
        [&] { ++hold_fail_count; });
    require(!failed_kind_hold.committed && hold_fail_count == 2,
        "deferred hold coordinator committed failed kind proof");

    coordinator_events.clear();
    operation_lock_depth = 0;
    int release_claim_count = 0;
    int release_execute_count = 0;
    const auto coordinated_release = coordinate_pause_resume_release_authority(
        [&] {
            ++operation_lock_depth;
            coordinator_events.emplace_back("snapshot");
            --operation_lock_depth;
            return true;
        },
        [&] {
            coordinator_events.emplace_back("observe");
            return operation_lock_depth == 0;
        },
        [&] {
            ++operation_lock_depth;
            coordinator_events.emplace_back("revalidate_claim");
            const bool valid = pause_resume_release_authority_valid(release_authority);
            if (valid) ++release_claim_count;
            --operation_lock_depth;
            return PauseResumeCoordinatorResult{valid, valid, !valid};
        },
        [&] { coordinator_events.emplace_back("fail"); });
    require(coordinated_release.action_exposed && release_claim_count == 1
            && coordinator_events == std::vector<std::string>{"snapshot", "observe",
                "revalidate_claim"},
        "fresh release coordinator violated snapshot/observe/revalidate/claim order");
    require(execute_pause_resume_deferred_action_once(
                coordinated_release.action_exposed, [&] {
                    ++release_execute_count;
                    return operation_lock_depth == 0 && callback_lifetime;
                })
            && release_execute_count == 1,
        "deferred release action did not execute once after lock drop with callback lifetime");
    require(!execute_pause_resume_deferred_action_once(false, [&] {
                ++release_execute_count; return true;
            }) && release_execute_count == 1,
        "unexposed release action executed");
    int early_claims = 0;
    const auto failed_snapshot = coordinate_pause_resume_release_authority(
        [] { return false; }, [] { return true; }, [&] {
            ++early_claims;
            return PauseResumeCoordinatorResult{true, true, false};
        }, [] {});
    const auto failed_observation = coordinate_pause_resume_release_authority(
        [] { return true; }, [] { return false; }, [&] {
            ++early_claims;
            return PauseResumeCoordinatorResult{true, true, false};
        }, [] {});
    require(failed_snapshot.failed && failed_observation.failed
            && !failed_snapshot.action_exposed
            && !failed_observation.action_exposed && early_claims == 0,
        "fresh release coordinator claimed after snapshot/observation failure");
    for (auto fact : authority_facts) {
        auto drifted = release_authority;
        drifted.*fact = false;
        int drift_claims = 0;
        const auto drift = coordinate_pause_resume_release_authority(
            [] { return true; }, [] { return true; }, [&] {
                const bool valid = pause_resume_release_authority_valid(drifted);
                if (valid) ++drift_claims;
                return PauseResumeCoordinatorResult{valid, valid, !valid};
            }, [] {});
        require(drift.failed && !drift.action_exposed && drift_claims == 0,
            "fresh release coordinator exposed action after authority drift");
    }

    const auto first_probe = expose_pause_resume_release_probe(
        UINT64_MAX, {}, PauseResumeBankPhase::ExitPending,
        41, 42, 43, 44, 0x400010008ull);
    require(first_probe.exposed && first_probe.next_ordinal == 1
            && first_probe.identity.ordinal == 1,
        "release-authority probe ordinal did not wrap to nonzero");
    const auto overlapping_probe = expose_pause_resume_release_probe(
        first_probe.next_ordinal, first_probe.identity,
        PauseResumeBankPhase::ExitPending, 41, 42, 43, 44,
        0x400010008ull);
    require(!overlapping_probe.exposed,
        "pending release-authority probe did not suppress second exposure");

    auto pending_release_identity = first_probe.identity;
    const OnMemoryBankRetiredBackingEvidence frozen_probe_evidence{
        true, false, 71, 72, 44, 0x400010008ull, {73, 74},
        reinterpret_cast<void*>(0x75000)};
    auto pending_probe_evidence = frozen_probe_evidence;
    auto current_probe_evidence = frozen_probe_evidence;
    bool current_monitor_matches = true;
    auto probe_phase = PauseResumeBankPhase::ExitPending;
    int probe_failure_count = 0;
    int probe_action_count = 0;
    const auto run_probe = [&](const PauseResumeReleaseProbeIdentity& probe,
                               bool observe_ok,
                               bool claim_ok) {
        return coordinate_pause_resume_release_authority(
            [&] {
                return pause_resume_release_probe_matches(probe, pending_release_identity)
                    && same_onmemory_bank_retired_backing_evidence(
                        frozen_probe_evidence, pending_probe_evidence)
                    && same_onmemory_bank_retired_backing_evidence(
                        frozen_probe_evidence, current_probe_evidence)
                    && current_monitor_matches
                    && probe_phase == probe.source_phase;
            },
            [&] { return observe_ok; },
            [&] {
                if (!claim_ok
                    || !pause_resume_release_probe_matches(probe, pending_release_identity)
                    || !same_onmemory_bank_retired_backing_evidence(
                        frozen_probe_evidence, pending_probe_evidence)
                    || !same_onmemory_bank_retired_backing_evidence(
                        frozen_probe_evidence, current_probe_evidence)
                    || !current_monitor_matches
                    || probe_phase != probe.source_phase) {
                    return PauseResumeCoordinatorResult{};
                }
                pending_release_identity = {};
                probe_phase = PauseResumeBankPhase::ReleaseRequested;
                return PauseResumeCoordinatorResult{true, true, false};
            },
            [&] {
                if (pause_resume_release_probe_matches(probe, pending_release_identity)
                    && same_onmemory_bank_retired_backing_evidence(
                        frozen_probe_evidence, pending_probe_evidence)) {
                    pending_release_identity = {};
                    probe_phase = PauseResumeBankPhase::Failed;
                    ++probe_failure_count;
                }
            });
    };
    const auto first_claim = run_probe(first_probe.identity, true, true);
    require(first_claim.action_exposed
            && probe_phase == PauseResumeBankPhase::ReleaseRequested
            && !pending_release_identity,
        "exact pending release-authority probe did not claim atomically");
    const auto stale_failure = run_probe(first_probe.identity, false, false);
    require(stale_failure.failed
            && probe_phase == PauseResumeBankPhase::ReleaseRequested
            && probe_failure_count == 0 && !stale_failure.action_exposed,
        "stale probe failure overwrote a claimed release");
    require(execute_pause_resume_deferred_action_once(
                first_claim.action_exposed, [&] {
                    ++probe_action_count;
                    return operation_lock_depth == 0 && callback_lifetime;
                }) && probe_action_count == 1,
        "claimed probe action did not execute once after lock drop");
    require(!execute_pause_resume_deferred_action_once(
                stale_failure.action_exposed, [&] {
                    ++probe_action_count;
                    return true;
                }) && probe_action_count == 1,
        "stale duplicate probe exposed a second release action");

    const auto exact_failure_probe = expose_pause_resume_release_probe(
        first_probe.next_ordinal, {}, PauseResumeBankPhase::RetirementWaiting,
        51, 52, 53, 54, 0x500010008ull);
    pending_release_identity = exact_failure_probe.identity;
    pending_probe_evidence = frozen_probe_evidence;
    current_probe_evidence = frozen_probe_evidence;
    current_monitor_matches = true;
    probe_phase = PauseResumeBankPhase::RetirementWaiting;
    const auto exact_failure = run_probe(exact_failure_probe.identity, false, false);
    require(exact_failure.failed && probe_phase == PauseResumeBankPhase::Failed
            && probe_failure_count == 1 && !pending_release_identity,
        "current exact probe failure did not terminalize exactly once");

    uint64_t PauseResumeReleaseProbeIdentity::* const probe_identity_facts[] = {
        &PauseResumeReleaseProbeIdentity::ordinal,
        &PauseResumeReleaseProbeIdentity::session_epoch,
        &PauseResumeReleaseProbeIdentity::cycle_epoch,
        &PauseResumeReleaseProbeIdentity::lifecycle_state_epoch,
        &PauseResumeReleaseProbeIdentity::retirement_epoch,
        &PauseResumeReleaseProbeIdentity::request_identity,
    };
    for (const auto fact : probe_identity_facts) {
        pending_release_identity = exact_failure_probe.identity;
        pending_probe_evidence = frozen_probe_evidence;
        current_probe_evidence = frozen_probe_evidence;
        current_monitor_matches = true;
        pending_release_identity.*fact += 1;
        probe_phase = exact_failure_probe.identity.source_phase;
        const int failures_before = probe_failure_count;
        const auto stale = run_probe(exact_failure_probe.identity, false, false);
        require(stale.failed && probe_phase == exact_failure_probe.identity.source_phase
                && probe_failure_count == failures_before && !stale.action_exposed,
            "stale probe identity drift mutated current release authority");
    }
    pending_release_identity = exact_failure_probe.identity;
    pending_probe_evidence = frozen_probe_evidence;
    current_probe_evidence = frozen_probe_evidence;
    current_monitor_matches = true;
    probe_phase = PauseResumeBankPhase::ExitPending;
    const int failures_before_phase_drift = probe_failure_count;
    const auto stale_phase = run_probe(exact_failure_probe.identity, false, false);
    require(stale_phase.failed && probe_phase == PauseResumeBankPhase::Failed
            && probe_failure_count == failures_before_phase_drift + 1
            && !pending_release_identity,
        "current pending probe with source drift did not fail closed");
    const auto require_current_authority_failure = [&](auto drift_current,
                                                       const char* message) {
        pending_release_identity = exact_failure_probe.identity;
        pending_probe_evidence = frozen_probe_evidence;
        current_probe_evidence = frozen_probe_evidence;
        current_monitor_matches = true;
        probe_phase = exact_failure_probe.identity.source_phase;
        drift_current();
        const int failures_before = probe_failure_count;
        const auto failed = run_probe(exact_failure_probe.identity, false, false);
        require(failed.failed && probe_phase == PauseResumeBankPhase::Failed
                && probe_failure_count == failures_before + 1
                && !pending_release_identity && !failed.action_exposed,
            message);
    };
    require_current_authority_failure([&] {
        current_probe_evidence.value = reinterpret_cast<void*>(0x76000);
    }, "changed current backing did not fail exact pending probe");
    require_current_authority_failure([&] {
        current_probe_evidence.observed = false;
    }, "unread current backing did not fail exact pending probe");
    require_current_authority_failure([&] {
        current_probe_evidence.failed = true;
    }, "failed current backing did not fail exact pending probe");
    require_current_authority_failure([&] {
        current_monitor_matches = false;
    }, "changed monitor epoch did not fail exact pending probe");
    require_current_authority_failure([&] {
        current_monitor_matches = false;
    }, "changed monitor request did not fail exact pending probe");
    require_current_authority_failure([&] {
        current_monitor_matches = false;
    }, "nonquiescent monitor did not fail exact pending probe");

    pending_release_identity = exact_failure_probe.identity;
    pending_probe_evidence = frozen_probe_evidence;
    pending_probe_evidence.value = reinterpret_cast<void*>(0x77000);
    current_probe_evidence = frozen_probe_evidence;
    current_monitor_matches = true;
    probe_phase = PauseResumeBankPhase::ReleaseRequested;
    const int failures_before_new_evidence = probe_failure_count;
    const auto stale_evidence = run_probe(
        exact_failure_probe.identity, false, false);
    require(stale_evidence.failed
            && probe_phase == PauseResumeBankPhase::ReleaseRequested
            && probe_failure_count == failures_before_new_evidence
            && pending_release_identity,
        "stale copy consumed or poisoned newer pending evidence");

    uint32_t modeled_poll_count = 19;
    auto modeled_poll_evidence = frozen_probe_evidence;
    require(!pause_resume_retirement_poll_allowed(
                PauseResumeBankPhase::RetirementWaiting, true,
                exact_failure_probe.identity),
        "pending authority probe did not suppress retirement polling");
    require(modeled_poll_count == 19
            && same_onmemory_bank_retired_backing_evidence(
                modeled_poll_evidence, frozen_probe_evidence),
        "suppressed retirement poll mutated count or evidence");
    require(pause_resume_retirement_poll_allowed(
                PauseResumeBankPhase::RetirementWaiting, true, {}),
        "cleared pending authority did not permit a fresh poll");
    const auto fresh_after_list_invalidation = expose_pause_resume_release_probe(
        exact_failure_probe.next_ordinal, {}, PauseResumeBankPhase::ExitPending,
        61, 62, 63, 64, 0x600010008ull);
    require(fresh_after_list_invalidation.exposed,
        "list-style pending invalidation did not permit one fresh probe");

    PauseResumeReleaseProbeCurrentFacts current_probe_facts{
        true, true, true, true, true, true, true};
    require(pause_resume_release_probe_current(current_probe_facts),
        "exact current and frozen probe authority was rejected");
    bool PauseResumeReleaseProbeCurrentFacts::* const current_probe_members[] = {
        &PauseResumeReleaseProbeCurrentFacts::exact_pending_identity,
        &PauseResumeReleaseProbeCurrentFacts::pending_evidence_matches,
        &PauseResumeReleaseProbeCurrentFacts::current_evidence_matches,
        &PauseResumeReleaseProbeCurrentFacts::monitor_identity_matches,
        &PauseResumeReleaseProbeCurrentFacts::exact_source_state,
        &PauseResumeReleaseProbeCurrentFacts::lifecycle_epoch_matches,
        &PauseResumeReleaseProbeCurrentFacts::detached_record_matches,
    };
    for (const auto member : current_probe_members) {
        auto drifted = current_probe_facts;
        drifted.*member = false;
        require(!pause_resume_release_probe_current(drifted),
            "drifted current probe authority was accepted");
    }

    for (const auto terminal_phase : {PauseResumeBankPhase::ReleasePending,
             PauseResumeBankPhase::Complete}) {
        pending_release_identity = {};
        pending_probe_evidence = {};
        current_probe_evidence = frozen_probe_evidence;
        current_monitor_matches = true;
        probe_phase = terminal_phase;
        const int failures_before = probe_failure_count;
        const auto stale_terminal = run_probe(
            first_probe.identity, false, false);
        require(stale_terminal.failed && !stale_terminal.action_exposed
                && probe_phase == terminal_phase
                && probe_failure_count == failures_before,
            "stale callback mutated terminal release state");
    }

    OnMemoryBankLifecycleState lifecycle;
    const OnMemoryBankSoundIdentity sound_identity{
        reinterpret_cast<void*>(0x21000), {31, 41}};
    const DecodedOnMemoryBankToken canonical{1, 0x121, 0x221};
    const DecodedOnMemoryBankToken custom{1, 0x122, 0x222};
    const uint64_t route = 701;
    const uint64_t cleanup = 801;
    const auto play_setup = lifecycle.snapshot_play_setup(true, true, true, true,
        sound_identity, canonical.encode(), route);
    require(play_setup.allowed()
            && lifecycle.commit_play_setup_qualification(play_setup.snapshot,
                sound_identity, canonical.encode(), route, 2)
                == OnMemoryBankRouteDecision::Allowed
            && lifecycle.retain_detached(sound_identity, custom, route, route,
                cleanup, 0x400010008ull, nullptr, true,
                true, true, true, 2, 2).detached(),
        "durable pause/resume lifecycle fixture did not retain custom bank");
    const auto first_detached = lifecycle.active();
    const uint64_t first_lifecycle_epoch = lifecycle.state_epoch();
    require(lifecycle.rebind_detached_request(first_detached,
                first_lifecycle_epoch,
                0x500010008ull, nullptr, true)
            && lifecycle.active().request_handle == 0x500010008ull
            && lifecycle.active().backing_observed
            && lifecycle.active().backing_identity == nullptr,
        "observed-null resumed request did not rebind authoritative lifecycle");
    const auto second_detached = lifecycle.active();
    const uint64_t second_lifecycle_epoch = lifecycle.state_epoch();
    require(lifecycle.rebind_detached_request(second_detached,
                second_lifecycle_epoch, second_detached.request_handle,
                second_detached.backing_identity, true)
            && !lifecycle.rebind_detached_request(second_detached,
                second_lifecycle_epoch, second_detached.request_handle,
                second_detached.backing_identity, true),
        "identical handle/backing ABA did not reject stale lifecycle epoch");
    const auto third_detached = lifecycle.active();
    const uint64_t third_lifecycle_epoch = lifecycle.state_epoch();
    void* const nonnull_backing = reinterpret_cast<void*>(0x22000);
    require(lifecycle.rebind_detached_request(third_detached,
                third_lifecycle_epoch,
                0x600010008ull, nonnull_backing, true)
            && lifecycle.active().request_handle == 0x600010008ull
            && lifecycle.active().backing_identity == nonnull_backing
            && !lifecycle.rebind_detached_request(first_detached,
                first_lifecycle_epoch,
                0x700010008ull, nullptr, true),
        "nonnull rebind failed or stale cycle consumed newer lifecycle state");
    const auto cycle_retired = pause_resume_retirement_outcome(
        PauseResumeBankPhase::RetirementWaiting, true, true, false);
    require(cycle_retired.advance_cycle && !cycle_retired.release_ready
            && cycle_retired.phase
                == PauseResumeBankPhase::AwaitingResumeOrListReturn,
        "quiescent resumed request did not return to repeatable suspension");
    const auto exit_retired = pause_resume_retirement_outcome(
        PauseResumeBankPhase::ExitPending, true, true, true);
    require(!exit_retired.advance_cycle && exit_retired.release_ready
            && exit_retired.phase == PauseResumeBankPhase::ReleaseRequested,
        "quiescent exit did not request exactly one retained-bank release");
    require(pause_resume_retirement_outcome(
                PauseResumeBankPhase::RetirementWaiting,
                false, true, false).phase == PauseResumeBankPhase::Failed,
        "stale resumed retirement action consumed newer cycle");

    const auto awaiting_exit = pause_resume_list_return_outcome(
        PauseResumeBankPhase::AwaitingResumeOrListReturn);
    require(awaiting_exit.release_ready
            && awaiting_exit.phase == PauseResumeBankPhase::ExitPending,
        "list return did not request retained-bank release");
    const auto active_exit = pause_resume_list_return_outcome(
        PauseResumeBankPhase::ResumedActive);
    require(active_exit.phase == PauseResumeBankPhase::ExitPending
            && !active_exit.release_ready,
        "active list return released before request quiescence");

    constexpr const char* marker_schema =
        "pause_resume_bank status=suspended_ready reason=cycle_retired "
        "session=1 cycle=3";
    const std::string schema(marker_schema);
    const char* forbidden[] = {"0x", "pointer", "address", "request_handle",
        "token", "song_id", "song_key", "path", "generation", "serial"};
    for (const char* field : forbidden) {
        require(schema.find(field) == std::string::npos,
            "durable pause/resume marker schema exposed forbidden data");
    }
}
} // namespace

int main()
{
    test_durable_pause_resume_bank_policy();
    return 0;
}
