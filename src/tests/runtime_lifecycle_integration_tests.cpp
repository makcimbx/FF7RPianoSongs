#include "runtime_lifecycle_test_support.h"

#include "core/hooks.h"
#include "game/audio_cleanup_policy.h"
#include "game/completion_capture.h"
#include "game/duration.h"
#include "game/frozen_profile_lifecycle.h"
#include "game/native_array_publication.h"
#include "game/onmemory_bank_diagnostic.h"
#include "game/onmemory_bank_lifecycle.h"
#include "game/progress.h"
#include "game/runtime_context_policy.h"
#include "game/scoreinfo_overlay.h"
#include "game/selection_audio_policy.h"
#include "game/title.h"
#include "game/uobject_locator_core.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ff7r::piano::tests::runtime_lifecycle {

void test_bgm_playback_native_play_pause_resume_integration()
{
    using namespace ff7r::piano::game;
    const BgmPlaybackNativePlayCallOutcome failed_orchestration_native{
        BgmPlaybackNativePlayTarget::Trampoline,
        BgmPlaybackNativePlayCallResult::CalledFailed, 1};
    uint32_t fallback_calls = 0;
    const auto forwarded_once = pause_resume_forward_disposition(
        PauseResumeNativeForwardResult::NativeFailedRestored,
        failed_orchestration_native.call_count, false);
    require(forwarded_once.disposition
                == PauseResumeDetourDisposition::OriginalAlreadyForwarded
            && dispatch_pause_resume_detour(forwarded_once, [&] {
                   ++fallback_calls;
                   return true;
               })
            && fallback_calls == 0,
        "pause/resume Play attempted a second fallback after exact forward");
}

void test_bgm_playback_aggregate_observer_integration()
{
    using namespace ff7r::piano::game;
    {
        SongRegistry catalog_registry;
        catalog_registry.replace({make_song("catalog-owner", 40)});
        catalog_registry.set_active_selection(40, 0);
        const RegistrySnapshot catalog = catalog_registry.registry_snapshot();
        const SelectionSnapshot selection = catalog_registry.selection_snapshot();
        CustomContextToken token{selection.generation, 1, 2, 3};
        require(ff7r::piano::game::registry_snapshot_owns_selection(catalog, selection)
                && catalog_registry.acquire_selection_guard(selection, token),
            "sidecar catalog fixture did not bind exact selection storage");
        const uint64_t guarded_generation = catalog_registry.registry_snapshot().generation;
        catalog_registry.replace({make_song("blocked-replacement", 41)});
        require(catalog_registry.registry_snapshot().generation == guarded_generation
                && catalog_registry.registry_snapshot().storage == catalog.storage
                && ff7r::piano::game::registry_snapshot_owns_selection(catalog, selection),
            "selection guard did not block catalog replacement");
        require(catalog_registry.publish_playback_from_selection_guard(selection, token),
            "guarded publication did not transfer exact selection identity");
        const PlaybackSnapshot playback = catalog_registry.playback_snapshot();
        require(playback.storage == selection.storage
                && playback.song == selection.song
                && playback.profile == selection.profile
                && playback.token == token,
            "playback publication changed storage/song/profile/lease identity");
        require(catalog_registry.release_selection_guard(token)
                && catalog_registry.revoke_playback(token)
                && catalog_registry.retire_cleanup_lease(token),
            "catalog fixture did not cancel publication ownership exactly");
        catalog_registry.replace({make_song("accepted-replacement", 42)});
        require(catalog_registry.registry_snapshot().generation > guarded_generation
                && catalog_registry.registry_snapshot().storage != catalog.storage
                && ff7r::piano::game::registry_snapshot_owns_selection(catalog, selection),
            "released guard did not permit replacement or retain frozen catalog snapshot");
    }
    {
        SongRegistry handoff_registry;
        handoff_registry.replace({make_song("handoff", 41)});
        handoff_registry.set_active_selection(41, 6);
        while (handoff_registry.selection_snapshot().generation < 15) {
            handoff_registry.set_active_selection(41, 6);
        }
        const SelectionSnapshot generation_15
            = handoff_registry.selection_snapshot();
        require(generation_15.generation == 15,
            "production handoff fixture did not reach generation 15");
        SelectionSnapshot published_selection;
        require(handoff_registry.publish_activation_selection_handoff(
                    generation_15, 41, 6, 9, true, published_selection)
                && published_selection.generation == 16
                && handoff_registry.selection_snapshot().generation == 16,
            "exact prepared activation getter did not force one registry publication");
        require(handoff_registry.preserve_confirmed_activation_index_notification(
                    published_selection, 41, 6, true)
                && handoff_registry.selection_snapshot().generation == 16,
            "exact same-selection activation notification changed generation 16");
        require(!handoff_registry.preserve_confirmed_activation_index_notification(
                    published_selection, 41, 6, false)
                && handoff_registry.selection_snapshot().generation == 16,
            "revoked/duplicate notification changed registry generation");
        SelectionSnapshot duplicate_selection;
        require(!handoff_registry.publish_activation_selection_handoff(
                    generation_15, 41, 6, 9, true, duplicate_selection)
                && !duplicate_selection
                && handoff_registry.selection_snapshot().generation == 16,
            "duplicate activation getter forced a second registry publication");
        handoff_registry.set_active_selection(44, 6);
        require(handoff_registry.selection_snapshot().generation == 17
                && handoff_registry.selection_snapshot().visible_index == 44,
            "unrelated index notification did not publish normal navigation generation");
    }
    {
        SongRegistry freeze_handoff_registry;
        freeze_handoff_registry.replace({make_song("freeze-handoff", 43)});
        freeze_handoff_registry.set_active_selection(43, 8);
        while (freeze_handoff_registry.selection_snapshot().generation < 15) {
            freeze_handoff_registry.set_active_selection(43, 8);
        }
        const SelectionSnapshot generation_15
            = freeze_handoff_registry.selection_snapshot();
        SelectionSnapshot generation_16;
        require(freeze_handoff_registry.publish_activation_selection_handoff(
                    generation_15, 43, 8, 12, true, generation_16)
                && generation_16.generation == 16,
            "profile-freeze fixture did not publish getter generation 16");
        SelectionSnapshot generation_17;
        require(freeze_handoff_registry.freeze_active_profile_for_activation(
                    generation_16, 12, generation_17)
                && generation_17.generation == 17
                && generation_17.storage == generation_16.storage
                && generation_17.song == generation_16.song
                && generation_17.profile == generation_16.profile
                && generation_17.visible_index == generation_16.visible_index
                && generation_17.base_slot == generation_16.base_slot,
            "exact activation profile freeze did not publish generation 17");
        auto consumed_ownership = selection_activation_ownership_transition(
            SelectionActivationTransferState::ClaimedFrozen,
            SelectionActivationOwnershipEvent::Consume, true);
        require(consumed_ownership.accepted
                && consumed_ownership.next
                    == SelectionActivationTransferState::Consumed,
            "production ownership policy rejected exact consumed claim");
        const auto early_rollback = selection_activation_ownership_transition(
            consumed_ownership.next,
            SelectionActivationOwnershipEvent::Rollback, true);
        require(early_rollback.rollback
                == SelectionActivationRollbackPlan::ReservationAndFrozenProfile,
            "pre-admission failure did not require exact thaw");
        SelectionSnapshot duplicate_freeze;
        require(!freeze_handoff_registry.freeze_active_profile_for_activation(
                    generation_16, 12, duplicate_freeze)
                && !duplicate_freeze
                && freeze_handoff_registry.selection_snapshot().generation == 17,
            "duplicate/stale profile freeze forced another registry generation");
        SelectionSnapshot thawed_18;
        require(freeze_handoff_registry.thaw_active_profile_for_activation(
                    generation_17, 12, thawed_18)
                && thawed_18.generation == 18,
            "exact activation owner did not thaw its profile freeze");
        SelectionSnapshot duplicate_thaw;
        require(!freeze_handoff_registry.thaw_active_profile_for_activation(
                    generation_17, 12, duplicate_thaw),
            "stale activation owner thawed profile twice");
        SelectionSnapshot newer_19;
        require(freeze_handoff_registry.freeze_active_profile_for_activation(
                    thawed_18, 13, newer_19)
                && newer_19.generation == 19,
            "new activation owner did not acquire profile freeze");
        require(!freeze_handoff_registry.thaw_active_profile_for_activation(
                    generation_17, 12, duplicate_thaw)
                && freeze_handoff_registry.selection_snapshot().generation == 19,
            "stale activation owner cleared newer profile freeze");
        const auto admission_owned = selection_activation_ownership_transition(
            consumed_ownership.next,
            SelectionActivationOwnershipEvent::TransferToAdmission, true);
        require(admission_owned.accepted
                && admission_owned.next
                    == SelectionActivationTransferState::AdmissionOwned,
            "valid admission Impl did not acquire exact frozen ownership");
        const auto admission_rollback = selection_activation_ownership_transition(
            admission_owned.next,
            SelectionActivationOwnershipEvent::Rollback, true);
        require(admission_rollback.rollback
                == SelectionActivationRollbackPlan::ReservationAndFrozenProfile,
            "uncommitted admission did not require one exact thaw");
        SelectionSnapshot thawed_20;
        require(freeze_handoff_registry.thaw_active_profile_for_activation(
                    newer_19, 13, thawed_20)
                && thawed_20.generation == 20,
            "new activation owner could not reclaim exact profile freeze");
        SelectionSnapshot committed_21;
        require(freeze_handoff_registry.freeze_active_profile_for_activation(
                    thawed_20, 14, committed_21)
                && committed_21.generation == 21,
            "committed-admission fixture did not acquire exact profile freeze");
        const auto committed = selection_activation_ownership_transition(
            admission_owned.next,
            SelectionActivationOwnershipEvent::Commit, true);
        require(committed.accepted
                && committed.rollback == SelectionActivationRollbackPlan::None
                && freeze_handoff_registry.selection_snapshot().generation == 21,
            "committed admission incorrectly planned or performed claim thaw");
        SelectionSnapshot lifecycle_thawed_22;
        require(freeze_handoff_registry.thaw_active_profile_for_activation(
                    committed_21, 14, lifecycle_thawed_22)
                && lifecycle_thawed_22.generation == 22,
            "normal lifecycle could not reclaim committed profile freeze");
    }
    {
        SongRegistry rejected_handoff_registry;
        rejected_handoff_registry.replace({make_song("rejected-handoff", 42)});
        rejected_handoff_registry.set_active_selection(42, 7);
        const SelectionSnapshot expected
            = rejected_handoff_registry.selection_snapshot();
        SelectionSnapshot published_selection;
        require(!rejected_handoff_registry.publish_activation_selection_handoff(
                    expected, 42, 7, 11, false, published_selection)
                && !rejected_handoff_registry.publish_activation_selection_handoff(
                    expected, 42, 7, 0, true, published_selection)
                && !rejected_handoff_registry.publish_activation_selection_handoff(
                    expected, 42, 8, 11, true, published_selection)
                && rejected_handoff_registry.selection_snapshot().generation
                    == expected.generation,
            "unrelated/invalid handoff ticket changed registry generation");
        rejected_handoff_registry.clear_active_selection();
        require(!rejected_handoff_registry.publish_activation_selection_handoff(
                    expected, 42, 7, 11, true, published_selection),
            "selection clear did not revoke production handoff publication authority");
    }
    ChartAudioAdmissionFacts chart_admission{
        true, true, true, true, true, true, true, true, true,
    };
    BgmPlaybackAggregateMutationGate selection_admission_gate;
    require(selection_admission_gate.enter(),
        "selection admission could not acquire production exit lease");
    ChartAudioAdmissionCoordinator gated_selection;
    require(gated_selection.reserve(chart_admission),
        "selection admission coordinator could not reserve under exit lease");
    std::atomic_bool selection_exit_published{false};
    std::thread selection_exit([&]() {
        selection_admission_gate.close_and_drain();
        selection_exit_published.store(true, std::memory_order_release);
    });
    while (!selection_admission_gate.closed()) std::this_thread::yield();
    require(!selection_exit_published.load(std::memory_order_acquire),
        "exit publication did not wait for selection audio admission");
    gated_selection.cancel();
    selection_admission_gate.leave();
    selection_exit.join();
    require(selection_exit_published.load(std::memory_order_acquire)
            && !gated_selection.original_may_consume_custom_chart(),
        "exit race left custom chart visible without committed audio arm");
}

void test_pause_resume_marker_delivery_policy()
{
    using namespace ff7r::piano::game;

    require(pause_resume_owner_tick_deferred_admitted(
                false, false, false, false, false, false, 3),
        "Set/Play/restore marker-only batch did not admit deferred drain");
    std::vector<std::string> order;
    const char* expected[] = {"set_rebound", "play_resumed", "canonical_restored"};
    drain_pause_resume_marker_batch(3, [&](const uint32_t index) {
        order.emplace_back(expected[index]);
    });
    require(order == std::vector<std::string>{
                "set_rebound", "play_resumed", "canonical_restored"},
        "marker-only batch did not drain exactly once in native order");

    require(pause_resume_owner_tick_deferred_admitted(
                false, false, false, false, false, true, 0),
        "single eligible marker did not admit deferred drain");
    int missing_set_emissions = 0;
    drain_pause_resume_marker_batch(1, [&](uint32_t) { ++missing_set_emissions; });
    require(missing_set_emissions == 1,
        "owner_tick_missing_set marker did not emit exactly once");

    int suspended_ready_emissions = 0;
    require(pause_resume_owner_tick_deferred_admitted(
                false, false, false, true, false, false, 0),
        "initial hold action no longer admitted deferred callback");
    drain_pause_resume_marker_batch(1, [&](uint32_t) {
        ++suspended_ready_emissions;
    });
    require(suspended_ready_emissions == 1,
        "initial hold modeled duplicate suspended_ready emission");

    require(!pause_resume_owner_tick_deferred_admitted(
                false, false, false, false, false, false, 0),
        "empty owner tick admitted deferred callback");
    require(pause_resume_owner_tick_deferred_admitted(
                true, false, false, false, false, false, 0)
            && pause_resume_owner_tick_deferred_admitted(
                false, true, false, false, false, false, 0)
            && pause_resume_owner_tick_deferred_admitted(
                false, false, true, false, false, false, 0),
        "existing bank-work admission changed");

    bool route_lock_held = true;
    bool callback_lifetime = true;
    bool emitted_after_unlock = false;
    {
        auto deferred = make_deferred_noexcept_action([&] {
            emitted_after_unlock = !route_lock_held && callback_lifetime;
            throw 1;
        });
        deferred.make_eligible();
        route_lock_held = false;
        // DeferredNoexceptAction catches the injected formatter/sink failure.
    }
    require(emitted_after_unlock,
        "marker formatting did not run after route unlock with callback held");
    require(pause_resume_marker_log_admitted(0)
            && pause_resume_marker_log_admitted(63)
            && !pause_resume_marker_log_admitted(64),
        "pause/resume marker process cap changed");

    // A second cycle marker is admitted only after exact retirement advances
    // the durable cycle; Set/Play restoration alone cannot synthesize it.
    uint64_t cycle = 1;
    const auto resumed = pause_resume_retirement_outcome(
        PauseResumeBankPhase::ResumedActive, true, false, false);
    require(!resumed.advance_cycle && cycle == 1,
        "resume without exact retirement advanced marker cycle");
    const auto retired = pause_resume_retirement_outcome(
        PauseResumeBankPhase::RetirementWaiting, true, true, false);
    if (retired.advance_cycle) cycle = next_pause_resume_bank_epoch(cycle);
    require(cycle == 2
            && retired.phase == PauseResumeBankPhase::AwaitingResumeOrListReturn,
        "cycle2 marker became eligible before exact request retirement");
}

void test_pause_resume_retirement_mismatch_diagnostic()
{
    using namespace ff7r::piano::game;
    PauseResumeRetirementAdmissionFacts exact{
        true, true, true, true, true, true, true, true, true};
    require(classify_pause_resume_retirement_mismatch(exact)
            == PauseResumeRetirementMismatch::None,
        "exact resumed Stop produced a retirement mismatch");

    struct Case {
        bool PauseResumeRetirementAdmissionFacts::* fact;
        PauseResumeRetirementMismatch expected;
        const char* name;
    };
    const Case cases[] = {
        {&PauseResumeRetirementAdmissionFacts::pre_chain_readable,
            PauseResumeRetirementMismatch::PreChainUnreadable, "pre_chain_unreadable"},
        {&PauseResumeRetirementAdmissionFacts::controller_matches,
            PauseResumeRetirementMismatch::ControllerMismatch, "controller_mismatch"},
        {&PauseResumeRetirementAdmissionFacts::sound_matches,
            PauseResumeRetirementMismatch::SoundMismatch, "sound_mismatch"},
        {&PauseResumeRetirementAdmissionFacts::request_matches,
            PauseResumeRetirementMismatch::RequestMismatch, "request_mismatch"},
        {&PauseResumeRetirementAdmissionFacts::state_matches,
            PauseResumeRetirementMismatch::StateMismatch, "state_mismatch"},
        {&PauseResumeRetirementAdmissionFacts::retirement_read_valid,
            PauseResumeRetirementMismatch::RetirementReadInvalid, "retirement_read_invalid"},
        {&PauseResumeRetirementAdmissionFacts::monitor_started,
            PauseResumeRetirementMismatch::MonitorStartRejected, "monitor_start_rejected"},
        {&PauseResumeRetirementAdmissionFacts::commit_current,
            PauseResumeRetirementMismatch::CommitStale, "commit_stale"},
    };
    for (size_t index = 0; index < std::size(cases); ++index) {
        auto facts = exact;
        facts.*cases[index].fact = false;
        for (size_t later = index + 1; later < std::size(cases); ++later) {
            facts.*cases[later].fact = false;
        }
        const auto mismatch = classify_pause_resume_retirement_mismatch(facts);
        require(mismatch == cases[index].expected
                && std::string(pause_resume_retirement_mismatch_name(mismatch))
                    == cases[index].name,
            "retirement mismatch precedence/category changed");
    }
    auto outside_scope = exact;
    outside_scope.session_scoped = false;
    outside_scope.pre_chain_readable = false;
    require(classify_pause_resume_retirement_mismatch(outside_scope)
            == PauseResumeRetirementMismatch::None,
        "Stop outside resumed session scope emitted mismatch");

    require(pause_resume_retirement_mismatch_admitted(false,
                PauseResumeRetirementMismatch::RequestMismatch)
            && !pause_resume_retirement_mismatch_admitted(true,
                PauseResumeRetirementMismatch::RequestMismatch)
            && !pause_resume_retirement_mismatch_admitted(false,
                PauseResumeRetirementMismatch::None),
        "retirement mismatch one-shot admission changed");

    // Initial absence is still a retryable Waiting observation, not a mismatch.
    OnMemoryBankRetiredBackingEvidence no_evidence;
    require(onmemory_bank_retirement_presence_pending(no_evidence, true, false)
            && classify_pause_resume_retirement_mismatch(exact)
                == PauseResumeRetirementMismatch::None,
        "initial absent retired entry emitted a mismatch");
    require(pause_resume_retirement_mismatch_admitted(false,
                PauseResumeRetirementMismatch::QuiescentProofMissing),
        "missing quiescent proof was not admitted");

    bool route_lock_held = true;
    bool callback_lifetime = true;
    int emitted = 0;
    {
        auto deferred = make_deferred_noexcept_action([&] {
            require(!route_lock_held && callback_lifetime,
                "retirement mismatch emitted before route unlock");
            drain_pause_resume_marker_batch(1, [&](uint32_t) { ++emitted; });
        });
        deferred.make_eligible();
        route_lock_held = false;
    }
    require(emitted == 1,
        "retirement mismatch batch did not emit exactly once");

    const auto cycle2 = pause_resume_retirement_outcome(
        PauseResumeBankPhase::RetirementWaiting, true, true, false);
    require(cycle2.advance_cycle
            && cycle2.phase == PauseResumeBankPhase::AwaitingResumeOrListReturn,
        "exact present-to-absent retirement did not preserve cycle2 success");

    const std::string schema =
        "pause_resume_bank status=retirement_mismatch reason=request_mismatch "
        "session=1 cycle=2";
    const char* forbidden[] = {"0x", "pointer", "address", "request_handle",
        "token", "song_id", "song_key", "path", "generation", "index", "serial"};
    for (const char* field : forbidden) {
        require(schema.find(field) == std::string::npos,
            "retirement mismatch schema exposed forbidden data");
    }
}

void test_pause_resume_latest_completed_retirement_release()
{
    using namespace ff7r::piano::game;
    const OnMemoryBankSoundIdentity sound{
        reinterpret_cast<void*>(0x18100), {81, 91}};
    const DecodedOnMemoryBankToken canonical{1, 0x581, 0x781};
    const DecodedOnMemoryBankToken custom{1, 0x582, 0x782};
    const auto run = [&](void* final_backing) {
        constexpr uint64_t route = 502;
        constexpr uint64_t cleanup = 601;
        constexpr uint64_t initial_request = 0x400010008ull;
        OnMemoryBankLifecycleState lifecycle;
        const auto setup = lifecycle.snapshot_play_setup(
            true, true, true, true, sound, canonical.encode(), 501);
        require(setup.allowed()
                && lifecycle.commit_play_setup_qualification(
                    setup.snapshot, sound, canonical.encode(), 501, 2)
                    == OnMemoryBankRouteDecision::Allowed
                && lifecycle.retain_detached(sound, custom, 501, route, cleanup,
                    initial_request, reinterpret_cast<void*>(0x18200), true,
                    true, true, true, 2, 2).detached(),
            "three-cycle fixture could not retain initial hold");

        OnMemoryBankRetirementFacts baseline;
        baseline.exact_request_retired = true;
        baseline.route_released = true;
        baseline.playback_released = true;
        baseline.cleanup_released = true;
        baseline.owner_restore_verified = true;
        baseline.runtime_installed = true;
        baseline.lookup_signature_valid = true;
        baseline.release_signature_valid = true;
        baseline.current_owner = classify_onmemory_bank_retirement_owner(
            lifecycle.active(), sound, true, true, canonical.encode());
        const auto initial_facts = bind_onmemory_bank_retirement_facts(
            baseline, lifecycle.active(), initial_request + 1,
            reinterpret_cast<void*>(0x18300), true,
            lifecycle.active().backing_identity, true);

        OnMemoryBankRetirementFacts latest = initial_facts;
        uint64_t cycle = 1;
        for (uint64_t index = 0; index < 3; ++index) {
            const auto expected = lifecycle.active();
            const uint64_t resumed_request = initial_request
                + (index + 1) * 0x100000000ull;
            void* const backing = index == 2 ? final_backing
                : reinterpret_cast<void*>(0x18400 + index * 0x100);
            require(lifecycle.rebind_detached_request(expected,
                        lifecycle.state_epoch(), resumed_request, backing, true),
                "completed cycle did not rebind detached request");
            latest = bind_onmemory_bank_retirement_facts(baseline,
                lifecycle.active(), resumed_request + 1,
                reinterpret_cast<void*>(0x18800 + index * 0x100), true,
                backing, true);
            latest.current_owner = classify_onmemory_bank_retirement_owner(
                lifecycle.active(), sound, true, true, canonical.encode());
            const auto completed = pause_resume_retirement_outcome(
                PauseResumeBankPhase::RetirementWaiting, true, true, false);
            require(completed.advance_cycle && !completed.release_ready,
                "completed cycle did not preserve list-return release");
            cycle = next_pause_resume_bank_epoch(cycle);
        }
        require(cycle == 4
                && latest.retired_request_handle == lifecycle.active().request_handle
                && latest.retired_backing == final_backing
                && latest.retired_backing_observed,
            "latest completed-cycle evidence was not exact");

        OnMemoryBankReleaseAction action;
        require(!lifecycle.claim_release(initial_facts, action) && !action,
            "stale initial facts claimed rebound request");
        require(lifecycle.claim_release(latest, action) && action,
            "latest facts did not claim current request exactly once");
        OnMemoryBankReleaseAction duplicate_action;
        require(!lifecycle.claim_release(latest, duplicate_action)
                && !duplicate_action,
            "latest facts claimed current request more than once");
        bool plugin_lock_held = false;
        int release_calls = 0;
        int canonical_release_calls = 0;
        const auto execution = execute_onmemory_bank_release(
            true, true, false, action,
            [&](uint64_t token) {
                require(!plugin_lock_held, "lookup executed under plugin lock");
                return token == canonical.encode() || token == custom.encode()
                    ? 2U : 3U;
            },
            [&](const uint64_t* token, uint8_t asynchronous) {
                require(!plugin_lock_held && token
                        && *token == custom.encode()
                        && *token != canonical.encode() && asynchronous == 1,
                    "release did not use copied custom token after lock drop");
                canonical_release_calls += *token == canonical.encode() ? 1 : 0;
                ++release_calls;
                return OnMemoryBankNativeReleaseResult{true, 0};
            });
        require(execution.outcome
                    == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                && release_calls == 1 && canonical_release_calls == 0
                && lifecycle.finish_release(action, execution.outcome),
            "list-return release did not execute custom exactly once");
        OnMemoryBankPendingObservation pending;
        require(lifecycle.copy_pending_observation(pending)
                && lifecycle.observe(pending, 2, 2)
                && lifecycle.copy_pending_observation(pending)
                && lifecycle.observe(pending, 2, 0)
                && !lifecycle.custom_route_blocked(),
            "kind2-to-kind0 did not unblock next custom route");
    };
    run(nullptr);
    run(reinterpret_cast<void*>(0x18f00));
}

void test_production_context_transition_policies()
{
    using namespace ff7r::piano::game;
    SongRegistry arm_registry;
    SongDescriptor song = make_song("arm", 3);
    song.duration_seconds = 42.0f;
    arm_registry.replace({song});
    arm_registry.set_active_selection(3, 0);
    const SelectionSnapshot selection = arm_registry.selection_snapshot();
    CustomContextToken token;
    token.registry_generation = selection.generation;
    token.route_generation = 10;
    token.lease_generation = 20;
    token.song_key = 30;
    token.sound = reinterpret_cast<void*>(0x4400);

    const AudioArmPublicationProof failures[] = {
        AudioArmPublicationProof::NativeHandoffBeforeCommit,
        AudioArmPublicationProof::RequestValidationFailed,
        AudioArmPublicationProof::RequestException,
        AudioArmPublicationProof::RebuildFailed,
        AudioArmPublicationProof::WaitingForPlaySetup,
        AudioArmPublicationProof::RequestReturnedNoClaim,
    };
    for (const AudioArmPublicationProof failure : failures) {
        require(!publish_audio_arm_if_proven(arm_registry, selection, token, failure),
            "failed production arm proof published playback");
        const PlaybackSnapshot absent = arm_registry.playback_snapshot();
        require(!absent.song && playback_duration_or_original(absent, 10000.0f) == 10000.0f,
            "failed production arm exposed custom duration");
        require(!scoreinfo_playback_eligible(absent),
            "failed production arm exposed ScoreInfo eligibility");
        require(!title_resolver_token_matches({}, absent, reinterpret_cast<void*>(0x4500)),
            "failed production arm exposed title eligibility");
    }

    // Runtime trace: private arm -> no PlaySetup -> Stop -> Set -> Play -> claim.
    SongRegistry controller_registry;
    controller_registry.replace({song});
    controller_registry.set_active_selection(3, 0);
    const SelectionSnapshot controller_selection = controller_registry.selection_snapshot();
    CustomContextToken armed_token = token;
    armed_token.registry_generation = controller_selection.generation;
    armed_token.route_generation = 40;
    armed_token.lease_generation = 50;
    armed_token.song_key = 60;
    armed_token.sound = nullptr;
    UnpublishedAudioSetupContext controller_setup{controller_selection, armed_token};
    void* const setup_controller = reinterpret_cast<void*>(0x6100);
    void* const setup_slot = reinterpret_cast<void*>(0x6200);
    void* const setup_bgm = reinterpret_cast<void*>(0x6300);
    void* const setup_sound = reinterpret_cast<void*>(0x6400);
    const UObjectLiveHandle setup_controller_handle{61, 610};
    const UObjectLiveHandle setup_sound_handle{64, 640};
    const ControllerIdentityProof setup_controller_proof = make_controller_identity_proof(
        setup_controller, reinterpret_cast<void*>(0x6110), 0x61, 3,
        reinterpret_cast<void*>(0x6120), true,
        setup_controller_handle.internal_index, true, setup_controller_handle);
    require(controller_setup.bind_route_controller(setup_controller_proof),
        "private controller setup did not bind its route proof");
    int stop_prefilter_validations = 0;
    const UObjectIdentityPrefilterResult stop_prefilter_result =
        evaluate_uobject_identity_prefilter(
            true,
            [&] { ++stop_prefilter_validations; return false; },
            [] { return true; },
            [] { return UObjectIdentityPrefilterResult::Passed; });
    const UObjectIdentityStopDiagnosticObservation stop_identity_observation{
        stop_prefilter_result,
        true,
        uobject_locator_core::UObjectLiveHandleCaptureResult::SerialInvalid,
    };
    require(controller_identity_proof_valid(controller_setup.controller_proof)
            && controller_setup.controller_proof.mode
                == ControllerIdentityProofMode::SerialBacked
            && stop_prefilter_validations == 1
            && stop_identity_observation.prefilter_result
                == UObjectIdentityPrefilterResult::ExpectedLiveHandleValidationFailed
            && stop_identity_observation.observed_live_capture_attempted
            && stop_identity_observation.observed_live_capture_result
                == uobject_locator_core::UObjectLiveHandleCaptureResult::SerialInvalid,
        "successful serial-backed arm lost independent Stop prefilter/capture diagnostics");
    require(private_controller_setup_eligible({true, true, true, true, true, true, true}),
        "exact private controller setup evidence was rejected");
    require(!private_controller_setup_eligible({true, false, true, true, true, true, true}),
        "existing cleanup lease permitted private initial setup classification");
    require(controller_registry.acquire_selection_guard(controller_selection, armed_token)
            && controller_setup.observe_controller_stop(
                armed_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008),
        "private controller Stop did not bind exact route identities");
    controller_registry.clear_active_selection();
    require(controller_registry.selection_guard_matches(controller_selection, armed_token)
            && controller_registry.selection_snapshot().song == controller_selection.song,
        "selection mutation crossed the private controller Set guard");
    CustomContextToken set_token = armed_token;
    set_token.route_generation = 41;
    set_token.controller = setup_controller;
    set_token.slot = setup_slot;
    set_token.bgm = setup_bgm;
    set_token.sound = setup_sound;
    set_token.request_handle = 0x1008;
    require(controller_setup.capture_controller_set(
                armed_token, set_token, setup_controller, setup_controller_proof, setup_slot,
                setup_bgm, setup_sound, setup_sound_handle),
        "private native Set was not captured");
    require(!controller_setup.capture_controller_set(
                armed_token, set_token, setup_controller, setup_controller_proof, setup_slot,
                setup_bgm, setup_sound, setup_sound_handle),
        "repeated/recursive native Set restarted a private transaction");
    require(!controller_registry.playback_snapshot().song
            && playback_duration_or_original(
                controller_registry.playback_snapshot(), 10000.0f) == 10000.0f,
        "private Set exposed global playback before post-Play proof");
    require(controller_setup.mark_controller_set_patched(set_token)
            && !controller_setup.mark_controller_set_forwarded(
                set_token, setup_controller_proof, 0x1008, 2, true)
            && !controller_setup.mark_controller_set_forwarded(
                set_token, setup_controller_proof, 0x2008, 4, true)
            && controller_setup.mark_controller_set_forwarded(
                set_token, setup_controller_proof, 0x2008, 2, true),
        "private Set patch/forward transaction was not recorded");
    require(private_controller_play_forwardable(
                controller_registry, controller_setup, set_token,
                setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle,
                0x2008, 2),
        "exact prepared Set state was not forwardable to native Play");
    require(controller_registry.release_selection_guard(set_token)
            && !private_controller_play_forwardable(
                controller_registry, controller_setup, set_token,
                setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle,
                0x2008, 2)
            && controller_registry.acquire_selection_guard(controller_selection, set_token),
        "selection guard loss did not block native Play forwarding");
    controller_registry.clear_active_selection();
    require(controller_registry.selection_guard_matches(controller_selection, set_token),
        "selection mutation crossed the private controller Play guard");
    ControllerIdentityProof drifted_controller_serial = setup_controller_proof;
    ++drifted_controller_serial.live.serial_number;
    require(!controller_setup.controller_play_forwardable(
                set_token, setup_controller, drifted_controller_serial,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle,
                0x2008, 2)
            && !controller_setup.controller_play_forwardable(
                set_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle,
                0x3008, 2),
        "controller serial or Set request drift remained Play-forwardable");
    require(!controller_setup.controller_play_claimable(
                set_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008, 4),
        "stale request handle passed post-Play claim proof");
    require(!controller_setup.controller_play_claimable(
                set_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x3008, 4),
        "Play request-handle replacement passed same-request proof");
    require(!controller_setup.controller_play_claimable(
                set_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x2008, 2),
        "Play that left the slot prepared passed transition proof");
    require(controller_setup.controller_play_claimable(
                set_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x2008, 4),
        "exact private native Play did not satisfy claim proof");
    CustomContextToken controller_claim = set_token;
    controller_claim.route_generation = 42;
    controller_claim.request_handle = 0x2008;
    require(controller_setup.mark_controller_claimed(set_token, controller_claim),
        "post-Play claim did not advance the private token");
    require(!controller_setup.controller_play_claimable(
                controller_claim, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x4008, 4)
            && !controller_setup.mark_controller_claimed(set_token, controller_claim),
        "repeated/recursive native Play reclaimed a completed private transaction");
    require(!controller_registry.playback_snapshot().song,
        "marking a private claim published playback before commit");
    require(publish_private_controller_claim_if_proven(
                controller_registry, controller_setup),
        "post-Play exact controller claim was not published");
    require(controller_registry.playback_snapshot().token == controller_claim
            && playback_duration_or_original(
                controller_registry.playback_snapshot(), 10000.0f) == 42.0f,
        "post-Play publication did not expose descriptor metadata exactly once");
    require(publish_private_controller_claim_if_proven(
                controller_registry, controller_setup),
        "repeated post-Play publication was not idempotent");
    require(!private_controller_setup_eligible({false, false, true, true, true, true, true}),
        "established custom playback was reclassified as initial setup");
    require(controller_registry.revoke_playback(controller_claim)
            && !private_controller_setup_eligible({true, false, true, true, true, true, true}),
        "custom-to-native handoff bypassed retained cleanup exclusion");
    require(controller_registry.retire_cleanup_lease(controller_claim),
        "private controller setup cleanup identity did not retire");
    require(!controller_registry.playback_snapshot().song
            && !controller_registry.cleanup_lease().song
            && playback_duration_or_original(
                controller_registry.playback_snapshot(), 10000.0f) == 10000.0f,
        "retired custom route contaminated subsequent Vanilla metadata");

    const auto bound_controller_setup = [&]() {
        UnpublishedAudioSetupContext setup{controller_selection, armed_token};
        require(setup.bind_route_controller(setup_controller_proof),
            "controller fixture did not bind its route proof");
        return setup;
    };
    UnpublishedAudioSetupContext partial_patch = bound_controller_setup();
    require(partial_patch.observe_controller_stop(
                armed_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008)
            && partial_patch.capture_controller_set(
                armed_token, set_token, setup_controller, setup_controller_proof, setup_slot,
                setup_bgm, setup_sound, setup_sound_handle)
            && partial_patch.mark_controller_set_patched(set_token),
        "partial-patch rollback fixture did not reach production Patched state");
    partial_patch.invalidate();
    require(!partial_patch && !controller_registry.playback_snapshot().song,
        "partial patch rollback left a publishable private transaction");
    UnpublishedAudioSetupContext play_exception = bound_controller_setup();
    require(play_exception.observe_controller_stop(
                armed_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008)
            && play_exception.capture_controller_set(
                armed_token, set_token, setup_controller, setup_controller_proof, setup_slot,
                setup_bgm, setup_sound, setup_sound_handle)
            && play_exception.mark_controller_set_patched(set_token)
            && play_exception.mark_controller_set_forwarded(
                set_token, setup_controller_proof, 0x2008, 2, true),
        "Play exception fixture did not reach production SetForwarded state");
    play_exception.invalidate();
    require(!play_exception && !controller_registry.playback_snapshot().song,
        "Play exception rollback left a publishable private transaction");

    UnpublishedAudioSetupContext different_set = bound_controller_setup();
    require(different_set.observe_controller_stop(
                armed_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008)
            && !different_set.capture_controller_set(
                armed_token, set_token, setup_controller, setup_controller_proof, setup_slot,
                setup_bgm, reinterpret_cast<void*>(0x6f00), setup_sound_handle),
        "different sound on the same chain was classified as private custom setup");
    UnpublishedAudioSetupContext reused_sound = bound_controller_setup();
    const UObjectLiveHandle reused_sound_serial{setup_sound_handle.internal_index,
        setup_sound_handle.serial_number + 1};
    require(reused_sound.observe_controller_stop(
                armed_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008)
            && !reused_sound.capture_controller_set(
                armed_token, set_token, setup_controller, setup_controller_proof, setup_slot,
                setup_bgm, setup_sound, reused_sound_serial),
        "expected sound pointer reuse with a different serial was accepted");
    UnpublishedAudioSetupContext reused_controller = bound_controller_setup();
    require(reused_controller.observe_controller_stop(
                armed_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008)
            && !reused_controller.capture_controller_set(
                armed_token, set_token, setup_controller, drifted_controller_serial,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle),
        "controller pointer reuse with a different serial was accepted before Set");

    SongRegistry structural_registry;
    structural_registry.replace({song});
    structural_registry.set_active_selection(3, 0);
    const SelectionSnapshot structural_selection = structural_registry.selection_snapshot();
    CustomContextToken structural_armed = armed_token;
    structural_armed.registry_generation = structural_selection.generation;
    structural_armed.route_generation = 70;
    structural_armed.lease_generation = 71;
    structural_armed.song_key = 72;
    const ControllerIdentityProof structural_proof = make_controller_identity_proof(
        setup_controller, setup_controller_proof.object_class,
        setup_controller_proof.name_comparison_id, setup_controller_proof.name_number,
        setup_controller_proof.outer, true, -1, false, {-1, 0});
    const auto structural_context_unchanged = [](
        const UnpublishedAudioSetupContext& left,
        const UnpublishedAudioSetupContext& right) {
        return left.selection.generation == right.selection.generation
            && left.selection.storage == right.selection.storage
            && left.selection.song == right.selection.song
            && left.selection.profile == right.selection.profile
            && left.selection.profile_index == right.selection.profile_index
            && left.selection.visible_index == right.selection.visible_index
            && left.selection.base_slot == right.selection.base_slot
            && left.token == right.token
            && left.controller_stage == right.controller_stage
            && left.controller == right.controller
            && left.slot == right.slot
            && left.bgm == right.bgm
            && left.expected_sound == right.expected_sound
            && left.sound == right.sound
            && left.controller_proof.mode == right.controller_proof.mode
            && left.controller_proof.controller == right.controller_proof.controller
            && left.controller_proof.object_class == right.controller_proof.object_class
            && left.controller_proof.name_comparison_id
                == right.controller_proof.name_comparison_id
            && left.controller_proof.name_number == right.controller_proof.name_number
            && left.controller_proof.outer == right.controller_proof.outer
            && left.controller_proof.raw_internal_index_readable
                == right.controller_proof.raw_internal_index_readable
            && left.controller_proof.raw_internal_index
                == right.controller_proof.raw_internal_index
            && left.controller_proof.live_capture_succeeded
                == right.controller_proof.live_capture_succeeded
            && left.controller_proof.live.internal_index
                == right.controller_proof.live.internal_index
            && left.controller_proof.live.serial_number
                == right.controller_proof.live.serial_number
            && left.controller_proof.item_backed_zero_serial_capture_succeeded
                == right.controller_proof.item_backed_zero_serial_capture_succeeded
            && left.controller_proof.item_backed_zero_serial.internal_index
                == right.controller_proof.item_backed_zero_serial.internal_index
            && left.controller_proof.item_backed_zero_serial.serial_number
                == right.controller_proof.item_backed_zero_serial.serial_number
            && left.expected_sound_handle.internal_index
                == right.expected_sound_handle.internal_index
            && left.expected_sound_handle.serial_number
                == right.expected_sound_handle.serial_number
            && left.request_before_set == right.request_before_set
            && left.request_after_set == right.request_after_set
            && left.state_after_set == right.state_after_set
            && left.custom_patch_applied == right.custom_patch_applied
            && left.custom_patch_restored == right.custom_patch_restored;
    };
    CustomContextToken structural_set = structural_armed;
    structural_set.route_generation = 71;
    structural_set.controller = setup_controller;
    structural_set.slot = setup_slot;
    structural_set.bgm = setup_bgm;
    structural_set.sound = setup_sound;
    structural_set.request_handle = 0x1008;
    const auto structural_stopped_setup = [&]() {
        UnpublishedAudioSetupContext setup{structural_selection, structural_armed};
        require(setup.bind_route_controller(structural_proof)
                && setup.observe_controller_stop(
                    structural_armed, setup_controller, structural_proof,
                    setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008),
            "structural controller proof did not pass Stop with a valid sound handle");
        return setup;
    };
    const auto structural_set_forwarded_setup = [&]() {
        UnpublishedAudioSetupContext setup = structural_stopped_setup();
        require(setup.capture_controller_set(
                    structural_armed, structural_set, setup_controller, structural_proof,
                    setup_slot, setup_bgm, setup_sound, setup_sound_handle)
                && setup.mark_controller_set_patched(structural_set)
                && setup.mark_controller_set_forwarded(
                    structural_set, structural_proof, 0x2008, 2, true),
            "structural controller proof did not pass Set");
        return setup;
    };
    require(structural_registry.acquire_selection_guard(
                structural_selection, structural_armed),
        "structural controller fixture did not acquire its selection guard");
    const ControllerIdentityProof positive_capture_failed =
        make_controller_identity_proof(
            setup_controller, structural_proof.object_class,
            structural_proof.name_comparison_id, structural_proof.name_number,
            structural_proof.outer, true, setup_controller_handle.internal_index,
            false, {-1, 0});
    const ControllerIdentityProof positive_capture_mismatched =
        make_controller_identity_proof(
            setup_controller, structural_proof.object_class,
            structural_proof.name_comparison_id, structural_proof.name_number,
            structural_proof.outer, true, setup_controller_handle.internal_index,
            true, {setup_controller_handle.internal_index + 1,
                      setup_controller_handle.serial_number});
    const ControllerIdentityProof invalid_capture_proofs[] = {
        positive_capture_failed,
        positive_capture_mismatched,
    };
    for (const ControllerIdentityProof& invalid_capture_proof : invalid_capture_proofs) {
        require(invalid_capture_proof.mode == ControllerIdentityProofMode::Invalid,
            "positive-index capture failure or mismatch was classified as usable");
        UnpublishedAudioSetupContext stop_rejected{
            structural_selection, structural_armed};
        require(stop_rejected.bind_route_controller(structural_proof)
                && !stop_rejected.observe_controller_stop(
                    structural_armed, setup_controller, invalid_capture_proof,
                    setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008),
            "invalid positive-index controller proof passed Stop");
        UnpublishedAudioSetupContext set_rejected = structural_stopped_setup();
        require(!set_rejected.capture_controller_set(
                    structural_armed, structural_set, setup_controller,
                    invalid_capture_proof, setup_slot, setup_bgm,
                    setup_sound, setup_sound_handle),
            "invalid positive-index controller proof passed Set");
        UnpublishedAudioSetupContext play_rejected =
            structural_set_forwarded_setup();
        require(!private_controller_play_forwardable(
                    structural_registry, play_rejected, structural_set,
                    setup_controller, invalid_capture_proof, setup_slot, setup_bgm,
                    setup_sound, setup_sound_handle, 0x2008, 2)
                && !play_rejected.controller_play_claimable(
                    structural_set, setup_controller, invalid_capture_proof,
                    setup_slot, setup_bgm, setup_sound, setup_sound_handle,
                    0x2008, 4)
                && !publish_private_controller_claim_if_proven(
                    structural_registry, play_rejected)
                && !structural_registry.playback_snapshot().song,
            "invalid positive-index controller proof passed Play/claim or published");
    }
    ControllerIdentityProof structural_negative_index_drift = structural_proof;
    --structural_negative_index_drift.raw_internal_index;
    require(controller_identity_proof_valid(structural_negative_index_drift),
        "negative-index drift fixture did not remain a structural proof");

    UnpublishedAudioSetupContext structural_stop_rejected{
        structural_selection, structural_armed};
    require(structural_stop_rejected.bind_route_controller(structural_proof),
        "structural Stop drift fixture did not bind");
    const UnpublishedAudioSetupContext structural_before_stop_rejection =
        structural_stop_rejected;
    require(!structural_stop_rejected.observe_controller_stop(
                structural_armed, setup_controller,
                structural_negative_index_drift, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle, 0x1008)
            && structural_context_unchanged(
                structural_stop_rejected, structural_before_stop_rejection)
            && structural_registry.selection_guard_matches(
                structural_selection, structural_armed)
            && !structural_registry.playback_snapshot().song,
        "structural negative-index drift at Stop mutated context or ownership");

    UnpublishedAudioSetupContext structural_set_index_rejected =
        structural_stopped_setup();
    const UnpublishedAudioSetupContext structural_before_set_index_rejection =
        structural_set_index_rejected;
    require(!structural_set_index_rejected.capture_controller_set(
                structural_armed, structural_set, setup_controller,
                structural_negative_index_drift, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle)
            && structural_context_unchanged(
                structural_set_index_rejected,
                structural_before_set_index_rejection)
            && structural_registry.selection_guard_matches(
                structural_selection, structural_armed)
            && !structural_registry.playback_snapshot().song,
        "structural negative-index drift at Set mutated context or ownership");

    UnpublishedAudioSetupContext structural_play_index_rejected =
        structural_set_forwarded_setup();
    const UnpublishedAudioSetupContext structural_before_play_index_rejection =
        structural_play_index_rejected;
    require(!private_controller_play_forwardable(
                structural_registry, structural_play_index_rejected,
                structural_set, setup_controller,
                structural_negative_index_drift, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle, 0x2008, 2)
            && structural_context_unchanged(
                structural_play_index_rejected,
                structural_before_play_index_rejection)
            && structural_registry.selection_guard_matches(
                structural_selection, structural_armed)
            && !structural_registry.playback_snapshot().song,
        "structural negative-index drift at Play mutated context or ownership");

    UnpublishedAudioSetupContext structural_claim_index_rejected =
        structural_set_forwarded_setup();
    const UnpublishedAudioSetupContext structural_before_claim_index_rejection =
        structural_claim_index_rejected;
    require(!structural_claim_index_rejected.controller_play_claimable(
                structural_set, setup_controller,
                structural_negative_index_drift, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle, 0x2008, 4)
            && structural_context_unchanged(
                structural_claim_index_rejected,
                structural_before_claim_index_rejection)
            && structural_registry.selection_guard_matches(
                structural_selection, structural_armed)
            && !publish_private_controller_claim_if_proven(
                structural_registry, structural_claim_index_rejected)
            && structural_context_unchanged(
                structural_claim_index_rejected,
                structural_before_claim_index_rejection)
            && structural_registry.selection_guard_matches(
                structural_selection, structural_armed)
            && !structural_registry.playback_snapshot().song,
        "structural negative-index drift at claim mutated context or publication");

    UnpublishedAudioSetupContext structural_setup = structural_set_forwarded_setup();
    require(private_controller_play_forwardable(
                structural_registry, structural_setup, structural_set,
                setup_controller, structural_proof, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle, 0x2008, 2)
            && structural_setup.controller_play_claimable(
                structural_set, setup_controller, structural_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x2008, 4),
        "structural controller proof did not pass Play/claim checks");
    CustomContextToken structural_claim = structural_set;
    structural_claim.route_generation = 72;
    structural_claim.request_handle = 0x2008;
    require(structural_setup.mark_controller_claimed(structural_set, structural_claim)
            && publish_private_controller_claim_if_proven(
                structural_registry, structural_setup),
        "structural-controller Stop->Set->Play claim did not publish");

    SongRegistry zero_serial_registry;
    zero_serial_registry.replace({song});
    zero_serial_registry.set_active_selection(3, 0);
    const SelectionSnapshot zero_serial_selection =
        zero_serial_registry.selection_snapshot();
    CustomContextToken zero_serial_armed = armed_token;
    zero_serial_armed.registry_generation = zero_serial_selection.generation;
    zero_serial_armed.route_generation = 80;
    zero_serial_armed.lease_generation = 81;
    zero_serial_armed.song_key = 82;
    CustomContextToken zero_serial_set = zero_serial_armed;
    zero_serial_set.route_generation = 81;
    zero_serial_set.controller = setup_controller;
    zero_serial_set.slot = setup_slot;
    zero_serial_set.bgm = setup_bgm;
    zero_serial_set.sound = setup_sound;
    zero_serial_set.request_handle = 0x1008;
    const ControllerIdentityProof zero_serial_proof = make_controller_identity_proof(
        setup_controller, setup_controller_proof.object_class,
        setup_controller_proof.name_comparison_id,
        setup_controller_proof.name_number, setup_controller_proof.outer,
        true, setup_controller_handle.internal_index, false, {-1, 0}, true,
        {setup_controller_handle.internal_index, 0});
    ControllerIdentityProof zero_item_index_drift = zero_serial_proof;
    ++zero_item_index_drift.raw_internal_index;
    ++zero_item_index_drift.item_backed_zero_serial.internal_index;
    ControllerIdentityProof zero_serial_positive_drift = zero_serial_proof;
    zero_serial_positive_drift.item_backed_zero_serial.serial_number = 1;
    ControllerIdentityProof zero_serial_negative_drift = zero_serial_proof;
    zero_serial_negative_drift.item_backed_zero_serial.serial_number = -1;
    require(zero_serial_proof.mode
                == ControllerIdentityProofMode::ItemBackedZeroSerial
            && zero_serial_registry.acquire_selection_guard(
                zero_serial_selection, zero_serial_armed),
        "zero-serial controller proof or selection guard was not admitted");

    UnpublishedAudioSetupContext zero_stop_rejected{
        zero_serial_selection, zero_serial_armed};
    require(zero_stop_rejected.bind_route_controller(zero_serial_proof)
            && !zero_stop_rejected.observe_controller_stop(
                zero_serial_armed, setup_controller,
                zero_serial_positive_drift, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle, 0x1008)
            && zero_stop_rejected.controller_stage
                == PrivateControllerSetupStage::AwaitingStop
            && !zero_serial_registry.playback_snapshot().song,
        "zero-serial Stop rejection mutated ownership or publication");

    UnpublishedAudioSetupContext zero_setup{
        zero_serial_selection, zero_serial_armed};
    require(zero_setup.bind_route_controller(zero_serial_proof)
            && zero_setup.observe_controller_stop(
                zero_serial_armed, setup_controller, zero_serial_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x1008),
        "zero-serial controller proof did not pass Stop");
    const UnpublishedAudioSetupContext zero_before_set_rejection = zero_setup;
    require(!zero_setup.capture_controller_set(
                zero_serial_armed, zero_serial_set, setup_controller,
                zero_item_index_drift, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle)
            && zero_setup.controller_stage
                == zero_before_set_rejection.controller_stage
            && zero_setup.token == zero_before_set_rejection.token
            && zero_setup.controller == zero_before_set_rejection.controller
            && zero_setup.expected_sound
                == zero_before_set_rejection.expected_sound
            && zero_setup.request_before_set
                == zero_before_set_rejection.request_before_set
            && !zero_serial_registry.playback_snapshot().song,
        "zero-serial Set rejection mutated ownership or publication");
    require(zero_setup.capture_controller_set(
                zero_serial_armed, zero_serial_set, setup_controller,
                zero_serial_proof, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle)
            && zero_setup.mark_controller_set_patched(zero_serial_set)
            && zero_setup.mark_controller_set_forwarded(
                zero_serial_set, zero_serial_proof, 0x2008, 2, true),
        "zero-serial controller proof did not pass Set forwarding");
    require(!private_controller_play_forwardable(
                zero_serial_registry, zero_setup, zero_serial_set,
                setup_controller, zero_serial_negative_drift,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle,
                0x2008, 2)
            && !zero_serial_registry.playback_snapshot().song,
        "zero-serial Play-forward rejection mutated publication");
    require(private_controller_play_forwardable(
                zero_serial_registry, zero_setup, zero_serial_set,
                setup_controller, zero_serial_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle,
                0x2008, 2)
            && !zero_setup.controller_play_claimable(
                zero_serial_set, setup_controller,
                zero_serial_positive_drift, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle, 0x2008, 4)
            && !zero_serial_registry.playback_snapshot().song,
        "zero-serial post-Play rejection mutated publication");
    require(zero_setup.controller_play_claimable(
                zero_serial_set, setup_controller, zero_serial_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle,
                0x2008, 4),
        "zero-serial controller proof did not pass post-Play claim");
    CustomContextToken zero_serial_claim = zero_serial_set;
    zero_serial_claim.route_generation = 82;
    zero_serial_claim.request_handle = 0x2008;
    require(zero_setup.mark_controller_claimed(
                zero_serial_set, zero_serial_claim)
            && publish_private_controller_claim_if_proven(
                zero_serial_registry, zero_setup)
            && zero_serial_registry.playback_snapshot().token
                == zero_serial_claim,
        "zero-serial Stop->Set->Play claim did not publish exactly");

    UnpublishedAudioSetupContext invalid_sound{
        structural_selection, structural_armed};
    UObjectLiveHandle invalid_sound_handle = setup_sound_handle;
    invalid_sound_handle.serial_number = 0;
    UObjectLiveHandle negative_sound_handle = setup_sound_handle;
    negative_sound_handle.serial_number = -1;
    require(invalid_sound.bind_route_controller(structural_proof)
            && !invalid_sound.observe_controller_stop(
                structural_armed, setup_controller, structural_proof,
                setup_slot, setup_bgm, setup_sound, invalid_sound_handle, 0x1008)
            && !invalid_sound.observe_controller_stop(
                structural_armed, setup_controller, structural_proof,
                setup_slot, setup_bgm, setup_sound, negative_sound_handle, 0x1008)
            && !publish_private_controller_claim_if_proven(
                structural_registry, invalid_sound),
        "structural controller bypassed the mandatory sound handle");

    ControllerIdentityProof structural_set_drift = structural_proof;
    structural_set_drift.object_class = reinterpret_cast<void*>(0x6f10);
    UnpublishedAudioSetupContext structural_set_rejected = structural_stopped_setup();
    require(!structural_set_rejected.capture_controller_set(
                structural_armed, structural_set, setup_controller, structural_set_drift,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle)
            && !publish_private_controller_claim_if_proven(
                structural_registry, structural_set_rejected),
        "structural controller drift at Set published ownership");
    ControllerIdentityProof structural_play_drift = structural_proof;
    ++structural_play_drift.name_number;
    UnpublishedAudioSetupContext structural_play_rejected =
        structural_set_forwarded_setup();
    require(!private_controller_play_forwardable(
                structural_registry, structural_play_rejected, structural_set,
                setup_controller, structural_play_drift, setup_slot, setup_bgm,
                setup_sound, setup_sound_handle, 0x2008, 2)
            && !publish_private_controller_claim_if_proven(
                structural_registry, structural_play_rejected),
        "structural controller drift at Play published ownership");
    ControllerIdentityProof structural_claim_drift = structural_proof;
    structural_claim_drift.outer = reinterpret_cast<void*>(0x6f20);
    UnpublishedAudioSetupContext structural_claim_rejected =
        structural_set_forwarded_setup();
    require(!structural_claim_rejected.controller_play_claimable(
                structural_set, setup_controller, structural_claim_drift,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0x2008, 4)
            && !publish_private_controller_claim_if_proven(
                structural_registry, structural_claim_rejected),
        "structural controller drift at post-Play claim published ownership");

    SongRegistry selection_drift_registry;
    selection_drift_registry.replace({song});
    selection_drift_registry.set_active_selection(3, 0);
    const SelectionSnapshot selection_before_drift =
        selection_drift_registry.selection_snapshot();
    selection_drift_registry.clear_active_selection();
    require(!selection_drift_registry.acquire_selection_guard(
                selection_before_drift, armed_token)
            && !selection_drift_registry.playback_snapshot().song,
        "selection drift acquired a Set guard or produced custom side effects");

    UnpublishedAudioSetupContext stale_controller = bound_controller_setup();
    SelectionSnapshot drifted_selection = controller_selection;
    ++drifted_selection.generation;
    require(!private_controller_setup_eligible({true, true, true, true, false, true, true}),
        "selection generation drift remained private-setup eligible");
    require(!stale_controller.observe_controller_stop(
                set_token, setup_controller, setup_controller_proof,
                setup_slot, setup_bgm, setup_sound, setup_sound_handle, 0),
        "stale route token captured a controller Stop");
    stale_controller.invalidate();
    require(!stale_controller && !publish_audio_arm_if_proven(
                controller_registry, drifted_selection, controller_claim,
                AudioArmPublicationProof::ControllerRebuildClaimed),
        "failed/partial private transaction remained publishable");

    require(!publish_audio_arm_if_proven(arm_registry, selection, token,
                AudioArmPublicationProof::PlaySetupClaimed),
        "incomplete PlaySetup identity published playback");
    UnpublishedAudioSetupContext delayed{selection, token};
    CustomContextToken claimed = token;
    claimed.route_generation = 11;
    claimed.controller = reinterpret_cast<void*>(0x4100);
    claimed.slot = reinterpret_cast<void*>(0x4200);
    claimed.bgm = reinterpret_cast<void*>(0x4300);
    claimed.request_handle = 0x400010008ull;
    require(delayed.advance(token, claimed),
        "exact delayed PlaySetup callback did not advance setup identity");
    require(publish_audio_arm_if_proven(arm_registry, selection, delayed.token,
                AudioArmPublicationProof::PlaySetupClaimed),
        "exact delayed PlaySetup claim was not promoted");
    require(publish_audio_arm_if_proven(arm_registry, selection, delayed.token,
                AudioArmPublicationProof::PlaySetupClaimed)
            && arm_registry.playback_snapshot().token == claimed,
        "repeated custom PlaySetup publication was not idempotent");

    CustomContextToken stale_callback = claimed;
    ++stale_callback.route_generation;
    require(!delayed.advance(token, stale_callback),
        "stale delayed callback advanced a newer setup identity");
    CustomContextToken pointer_reuse = claimed;
    pointer_reuse.route_generation = 12;
    pointer_reuse.lease_generation = 21;
    require(!arm_registry.revoke_playback(pointer_reuse)
            && arm_registry.playback_snapshot().token == claimed,
        "pointer reuse bypassed route and lease generations");

    UnpublishedAudioSetupContext retained_failure{selection, token};
    retained_failure.invalidate();
    require(!retained_failure && !retained_failure.advance(token, claimed),
        "retained rebuild failure left setup publishable");
    UnpublishedAudioSetupContext list_return{selection, token};
    list_return.invalidate();
    require(!list_return, "list return left unpublished setup valid");
    UnpublishedAudioSetupContext disabled_route{selection, token};
    disabled_route.invalidate();
    require(!disabled_route, "disabled routing left unpublished setup valid");

    ScoreInfoPublicationEpoch epoch;
    std::atomic_bool observed{false};
    std::atomic_bool invalidated{false};
    std::atomic_bool stale_committed{true};
    std::thread publisher([&] {
        std::uint64_t ticket = 0;
        {
            std::lock_guard<std::mutex> lock(epoch.mutex());
            ticket = epoch.observe_locked();
        }
        observed.store(true, std::memory_order_release);
        while (!invalidated.load(std::memory_order_acquire)) std::this_thread::yield();
        std::lock_guard<std::mutex> lock(epoch.mutex());
        stale_committed.store(epoch.current_locked(ticket, claimed,
            arm_registry.playback_snapshot()), std::memory_order_release);
    });
    while (!observed.load(std::memory_order_acquire)) std::this_thread::yield();
    require(arm_registry.revoke_playback(claimed),
        "concurrent ScoreInfo test did not revoke playback first");
    {
        std::lock_guard<std::mutex> lock(epoch.mutex());
        epoch.invalidate_locked();
    }
    invalidated.store(true, std::memory_order_release);
    publisher.join();
    require(!stale_committed.load(std::memory_order_acquire),
        "concurrent ScoreInfo invalidation missed an in-flight publication");
    require(arm_registry.retire_cleanup_lease(claimed),
        "repeated custom request identity did not retire");

    unsigned resolver_depth = 0;
    {
        ScoreInfoResolverRecursionScope outer(resolver_depth);
        require(outer.outermost(), "outer ScoreInfo resolver was rejected");
        {
            ScoreInfoResolverRecursionScope recursive(resolver_depth);
            require(!recursive.outermost(), "recursive ScoreInfo resolver was publishable");
        }
        require(resolver_depth == 1, "recursive ScoreInfo depth was not restored");
    }
    require(resolver_depth == 0, "ScoreInfo resolver scope leaked recursion depth");

    CustomContextToken current = claimed;
    current.route_generation = 13;
    current.lease_generation = 23;
    require(publish_audio_arm_if_proven(arm_registry, selection, current,
                AudioArmPublicationProof::ControllerRebuildClaimed),
        "proven controller rebuild claim was not promoted");
    int commits = 0;
    require(!commit_progress_if_playback_token(arm_registry, claimed, [&] {
                ++commits;
                return true;
            }) && commits == 0,
        "stale progress token committed a custom score");
    require(commit_progress_if_playback_token(arm_registry, current, [&] {
                ++commits;
                return true;
            }) && commits == 1,
        "current progress token failed its guarded commit");
    require(arm_registry.revoke_playback(current)
            && arm_registry.retire_cleanup_lease(current),
        "successful rebuild identity did not retire");

    AudioRouteOperationCoordinator operations;
    void* const expected_bgm = reinterpret_cast<void*>(0x5100);
    void* const expected_sound = reinterpret_cast<void*>(0x5200);
    void* const vanilla_replacement = reinterpret_cast<void*>(0x5300);
    std::atomic<void*> live_sound{expected_sound};
    std::atomic<void*> patched_sound{nullptr};
    std::atomic_bool prepare_entered{false};
    std::atomic_bool allow_prepare{false};
    std::atomic_bool set_attempted{false};
    std::atomic_bool set_completed{false};
    std::thread prepare([&] {
        std::lock_guard<std::recursive_mutex> lock(operations.mutex());
        prepare_entered.store(true, std::memory_order_release);
        while (!allow_prepare.load(std::memory_order_acquire)) std::this_thread::yield();
        void* const current_sound = live_sound.load(std::memory_order_acquire);
        if (bgm_prepare_identity_matches(
                expected_bgm, expected_sound, expected_bgm, current_sound)) {
            patched_sound.store(current_sound, std::memory_order_release);
        }
    });
    for (int i = 0; i < 100000
        && !prepare_entered.load(std::memory_order_acquire); ++i) std::this_thread::yield();
    if (!prepare_entered.load(std::memory_order_acquire)) {
        allow_prepare.store(true, std::memory_order_release);
        prepare.join();
        require(false, "serialized BGM Prepare worker did not start");
    }
    std::thread native_set([&] {
        set_attempted.store(true, std::memory_order_release);
        std::lock_guard<std::recursive_mutex> lock(operations.mutex());
        live_sound.store(vanilla_replacement, std::memory_order_release);
        set_completed.store(true, std::memory_order_release);
    });
    for (int i = 0; i < 100000
        && !set_attempted.load(std::memory_order_acquire); ++i) std::this_thread::yield();
    require(set_attempted.load(std::memory_order_acquire),
        "native Set worker did not reach the serialized operation");
    require(!set_completed.load(std::memory_order_acquire),
        "native Set entered while BGM Prepare held the route operation");
    allow_prepare.store(true, std::memory_order_release);
    prepare.join();
    native_set.join();
    require(patched_sound.load(std::memory_order_acquire) == expected_sound
            && live_sound.load(std::memory_order_acquire) == vanilla_replacement,
        "serialized BGM Prepare patched replacement Vanilla sound");
    require(!bgm_prepare_identity_matches(
                expected_bgm, expected_sound, expected_bgm, vanilla_replacement),
        "BGM Prepare accepted changed bgm+0x28 identity");
    {
        std::unique_lock<std::recursive_mutex> outer(operations.mutex());
        std::unique_lock<std::recursive_mutex> nested(
            operations.mutex(), std::try_to_lock);
        require(nested.owns_lock(), "nested BGM Prepare operation would deadlock");
    }
}

void test_reentrant_play_setup_private_claim_policy()
{
    using namespace ff7r::piano::game;
    const SongDescriptor song = make_song("reentrant-play-setup", 9);
    void* const controller = reinterpret_cast<void*>(0x8100);
    void* const slot = reinterpret_cast<void*>(0x8200);
    void* const bgm = reinterpret_cast<void*>(0x8300);
    void* const sound = reinterpret_cast<void*>(0x8400);
    void* const played_sound = reinterpret_cast<void*>(0x8500);
    const UObjectLiveHandle sound_handle{84, 840};
    const UObjectLiveHandle played_sound_handle{85, 850};
    const ControllerIdentityProof proof = make_controller_identity_proof(
        controller, reinterpret_cast<void*>(0x8110), 81, 8,
        reinterpret_cast<void*>(0x8120), true, 18, false, {-1, 0}, true,
        {18, 0});
    require(proof.mode == ControllerIdentityProofMode::ItemBackedZeroSerial
            && private_object_handle_valid(sound_handle),
        "reentrant PlaySetup fixture lacked zero-serial controller/positive-serial sound proof");

    const auto prepare = [&](SongRegistry& registry_under_test,
                             SelectionSnapshot& selection,
                             CustomContextToken& armed,
                             CustomContextToken& set,
                             UnpublishedAudioSetupContext& setup) {
        registry_under_test.replace({song});
        registry_under_test.set_active_selection(9, 0);
        selection = registry_under_test.selection_snapshot();
        armed.registry_generation = selection.generation;
        armed.route_generation = 90;
        armed.lease_generation = 91;
        armed.song_key = 92;
        set = armed;
        set.route_generation = 91;
        set.controller = controller;
        set.slot = slot;
        set.bgm = bgm;
        set.sound = sound;
        set.request_handle = 0x9008;
        setup = {selection, armed};
        return registry_under_test.acquire_selection_guard(selection, armed)
            && setup.bind_route_controller(proof)
            && setup.observe_controller_stop(
                armed, controller, proof, slot, bgm, sound, sound_handle, 0x8008)
            && setup.capture_controller_set(
                armed, set, controller, proof, slot, bgm, sound, sound_handle)
            && setup.mark_controller_set_patched(set)
            && setup.mark_controller_set_forwarded(set, proof, 0x9008, 2, true);
    };

    SongRegistry successful_registry;
    SelectionSnapshot successful_selection;
    CustomContextToken successful_armed;
    CustomContextToken successful_set;
    UnpublishedAudioSetupContext successful_setup;
    require(prepare(successful_registry, successful_selection, successful_armed,
                successful_set, successful_setup),
        "reentrant PlaySetup fixture did not reach SetForwarded");
    CustomContextToken successful_claim = successful_set;
    successful_claim.route_generation = 92;
    successful_claim.request_handle = 0x9008;
    require(claim_private_controller_play_after_forward(
                successful_registry, successful_setup, successful_set,
                controller, proof, slot, bgm, sound, sound_handle, 0x9008, 2,
                proof, slot, bgm, sound, sound_handle, 0x9008, 4,
                successful_claim)
            && successful_setup.controller_stage
                == PrivateControllerSetupStage::Claimed
            && successful_setup.advance_or_confirm_claimed(
                successful_set, successful_claim)
            && classify_audio_arm_publication(
                AudioArmPublicationProof::PlaySetupClaimed,
                successful_setup.controller_stage)
                == AudioArmPublicationPath::PrivateControllerClaim
            && publish_private_controller_claim_if_proven(
                successful_registry, successful_setup)
            && successful_registry.playback_snapshot().token == successful_claim,
        "SetForwarded -> reentrant Play -> Claimed did not use guarded publication");

    enum class Drift {
        Request,
        StateRemainsSet,
        ControllerIndex,
        ControllerSerial,
        ControllerMode,
        SoundHandle,
        SelectionGuard,
    };
    const auto rejected_without_mutation = [&](const Drift drift) {
        SongRegistry rejected_registry;
        SelectionSnapshot rejected_selection;
        CustomContextToken rejected_armed;
        CustomContextToken rejected_set;
        UnpublishedAudioSetupContext rejected_setup;
        if (!prepare(rejected_registry, rejected_selection, rejected_armed,
                rejected_set, rejected_setup)) return false;
        ControllerIdentityProof post_proof = proof;
        UObjectLiveHandle post_sound_handle = sound_handle;
        uint64_t post_request = 0x9008;
        uint8_t post_state = 4;
        if (drift == Drift::Request) ++post_request;
        if (drift == Drift::StateRemainsSet) post_state = 2;
        if (drift == Drift::ControllerIndex) {
            ++post_proof.raw_internal_index;
            ++post_proof.item_backed_zero_serial.internal_index;
        }
        if (drift == Drift::ControllerSerial) {
            post_proof.item_backed_zero_serial.serial_number = 1;
        }
        if (drift == Drift::ControllerMode) {
            post_proof.mode = ControllerIdentityProofMode::Structural;
        }
        if (drift == Drift::SoundHandle) ++post_sound_handle.serial_number;
        if (drift == Drift::SelectionGuard
            && !rejected_registry.release_selection_guard(rejected_set)) return false;
        const CustomContextToken token_before = rejected_setup.token;
        const PrivateControllerSetupStage stage_before = rejected_setup.controller_stage;
        CustomContextToken claim = rejected_set;
        claim.route_generation = 92;
        claim.request_handle = 0x9008;
        const bool claimed = claim_private_controller_play_after_forward(
            rejected_registry, rejected_setup, rejected_set,
            controller, proof, slot, bgm, sound, sound_handle, 0x9008, 2,
            post_proof, slot, bgm, sound, post_sound_handle,
            post_request, post_state, claim);
        const AudioArmPublicationPath path = classify_audio_arm_publication(
            AudioArmPublicationProof::PlaySetupClaimed,
            rejected_setup.controller_stage);
        const bool published = path == AudioArmPublicationPath::PrivateControllerClaim
            ? publish_private_controller_claim_if_proven(rejected_registry, rejected_setup)
            : path == AudioArmPublicationPath::GuardedPlaySetupClaim
                ? publish_guarded_play_setup_claim_if_proven(
                    rejected_registry, rejected_setup, rejected_setup.token,
                    AudioArmPublicationProof::PlaySetupClaimed, {}, {})
            : path == AudioArmPublicationPath::Generic
                && publish_audio_arm_if_proven(rejected_registry,
                    rejected_selection, rejected_setup.token,
                    AudioArmPublicationProof::PlaySetupClaimed);
        return !claimed && !published && rejected_setup.token == token_before
            && rejected_setup.controller_stage == stage_before
            && !rejected_registry.playback_snapshot().song;
    };
    require(rejected_without_mutation(Drift::Request)
            && rejected_without_mutation(Drift::StateRemainsSet)
            && rejected_without_mutation(Drift::ControllerIndex)
            && rejected_without_mutation(Drift::ControllerSerial)
            && rejected_without_mutation(Drift::ControllerMode)
            && rejected_without_mutation(Drift::SoundHandle)
            && rejected_without_mutation(Drift::SelectionGuard),
        "reentrant PlaySetup drift claimed, published, or mutated private ownership");

    const auto prepare_direct_pre_restore = [&](SongRegistry& registry_under_test,
                                    SelectionSnapshot& selection,
                                    CustomContextToken& armed,
                                    CustomContextToken& pre_restore,
                                    CustomContextToken& post_restore,
                                    UnpublishedAudioSetupContext& setup,
                                    GuardedPlaySetupClaimObservation& pre_observation,
                                    GuardedPlaySetupClaimObservation& post_observation) {
        registry_under_test.replace({song});
        registry_under_test.set_active_selection(9, 0);
        selection = registry_under_test.selection_snapshot();
        armed.registry_generation = selection.generation;
        armed.route_generation = 120;
        armed.lease_generation = 121;
        armed.song_key = 122;
        pre_observation.controller = controller;
        pre_observation.controller_proof = proof;
        pre_observation.slot = slot;
        pre_observation.bgm = bgm;
        pre_observation.sound = played_sound;
        pre_observation.sound_handle = played_sound_handle;
        pre_observation.request_handle = 0xa008;
        pre_observation.state = 4;
        post_observation = pre_observation;
        pre_restore = armed;
        pre_restore.route_generation = 121;
        pre_restore.controller = pre_observation.controller;
        pre_restore.slot = pre_observation.slot;
        pre_restore.bgm = pre_observation.bgm;
        pre_restore.sound = pre_observation.sound;
        pre_restore.request_handle = pre_observation.request_handle;
        post_restore = pre_restore;
        post_restore.route_generation = 122;
        setup = {selection, armed};
        return registry_under_test.acquire_selection_guard(selection, armed)
            && setup.bind_route_controller(proof)
            && setup.observe_controller_stop(
                armed, controller, proof, slot, bgm, sound, sound_handle, 0x8008)
            && setup.advance(armed, pre_restore);
    };

    SongRegistry direct_registry;
    SelectionSnapshot direct_selection;
    CustomContextToken direct_armed;
    CustomContextToken direct_pre_restore;
    CustomContextToken direct_complete;
    UnpublishedAudioSetupContext direct_setup;
    GuardedPlaySetupClaimObservation direct_pre_observation;
    GuardedPlaySetupClaimObservation direct_observation;
    require(prepare_direct_pre_restore(
                direct_registry, direct_selection, direct_armed,
                direct_pre_restore, direct_complete, direct_setup,
                direct_pre_observation, direct_observation)
            && direct_setup.expected_sound == sound
            && private_object_handle_matches(
                direct_setup.expected_sound_handle, sound_handle)
            && direct_complete.sound == played_sound
            && direct_observation.sound == played_sound
            && direct_setup.expected_sound != direct_observation.sound
            && classify_audio_arm_publication(
                AudioArmPublicationProof::PlaySetupClaimed,
                direct_setup.controller_stage)
                == AudioArmPublicationPath::GuardedPlaySetupClaim,
        "StopObserved direct PlaySetup pre-restore fixture was invalid");

    const GuardedPlaySetupNativeOwnerFacts complete_owner_facts{
        true,
        true,
        direct_pre_restore.route_generation,
        direct_pre_restore.route_generation,
        true,
        true,
        played_sound,
        controller,
        slot,
        bgm,
        played_sound,
        played_sound_handle,
        direct_pre_restore.request_handle,
        direct_pre_restore,
        direct_pre_observation,
    };
    require(guarded_play_setup_native_changes_allowed(
            direct_setup, complete_owner_facts),
        "complete routed native Play ownership did not allow native restore changes");
    enum class OwnerDrift {
        RoutedClaim,
        CustomOwnership,
        ExpectedGeneration,
        RouteGeneration,
        RoutePhase,
        RouteSong,
        PlaySetupSound,
        RouteController,
        OwnedSlot,
        OwnedBgm,
        OwnedSound,
        OwnedSoundHandle,
        OwnedRequest,
        CurrentToken,
        FreshController,
        FreshControllerProof,
        FreshSlot,
        FreshBgm,
        FreshSound,
        FreshSoundHandle,
        FreshRequest,
        FreshRequestType,
        FreshState,
    };
    const auto native_changes_rejected = [&](const OwnerDrift drift) {
        GuardedPlaySetupNativeOwnerFacts facts = complete_owner_facts;
        if (drift == OwnerDrift::RoutedClaim) facts.routed_play_claimed = false;
        if (drift == OwnerDrift::CustomOwnership) facts.custom_resource_owned = false;
        if (drift == OwnerDrift::ExpectedGeneration) facts.expected_route_generation = 0;
        if (drift == OwnerDrift::RouteGeneration) ++facts.route_generation;
        if (drift == OwnerDrift::RoutePhase) facts.route_playing = false;
        if (drift == OwnerDrift::RouteSong) facts.route_song_matches = false;
        if (drift == OwnerDrift::PlaySetupSound) facts.play_setup_sound = nullptr;
        if (drift == OwnerDrift::RouteController) {
            facts.route_controller = reinterpret_cast<void*>(0x8101);
        }
        if (drift == OwnerDrift::OwnedSlot) {
            facts.owned_slot = reinterpret_cast<void*>(0x8201);
        }
        if (drift == OwnerDrift::OwnedBgm) {
            facts.owned_bgm = reinterpret_cast<void*>(0x8301);
        }
        if (drift == OwnerDrift::OwnedSound) {
            facts.owned_sound = reinterpret_cast<void*>(0x8501);
        }
        if (drift == OwnerDrift::OwnedSoundHandle) facts.owned_sound_handle = {};
        if (drift == OwnerDrift::OwnedRequest) facts.owned_request_handle = 0xa009;
        if (drift == OwnerDrift::CurrentToken) ++facts.current_route_token.route_generation;
        if (drift == OwnerDrift::FreshController) {
            facts.observation.controller = reinterpret_cast<void*>(0x8101);
            facts.observation.controller_proof.controller = facts.observation.controller;
        }
        if (drift == OwnerDrift::FreshControllerProof) {
            ++facts.observation.controller_proof.name_comparison_id;
        }
        if (drift == OwnerDrift::FreshSlot) {
            facts.observation.slot = reinterpret_cast<void*>(0x8201);
        }
        if (drift == OwnerDrift::FreshBgm) {
            facts.observation.bgm = reinterpret_cast<void*>(0x8301);
        }
        if (drift == OwnerDrift::FreshSound) {
            facts.observation.sound = reinterpret_cast<void*>(0x8501);
        }
        if (drift == OwnerDrift::FreshSoundHandle) {
            ++facts.observation.sound_handle.serial_number;
        }
        if (drift == OwnerDrift::FreshRequest) facts.observation.request_handle = 0xb008;
        if (drift == OwnerDrift::FreshRequestType) facts.observation.request_handle = 0xa009;
        if (drift == OwnerDrift::FreshState) facts.observation.state = 2;
        return !guarded_play_setup_native_changes_allowed(direct_setup, facts);
    };
    require(native_changes_rejected(OwnerDrift::RoutedClaim)
            && native_changes_rejected(OwnerDrift::CustomOwnership)
            && native_changes_rejected(OwnerDrift::ExpectedGeneration)
            && native_changes_rejected(OwnerDrift::RouteGeneration)
            && native_changes_rejected(OwnerDrift::RoutePhase)
            && native_changes_rejected(OwnerDrift::RouteSong)
            && native_changes_rejected(OwnerDrift::PlaySetupSound)
            && native_changes_rejected(OwnerDrift::RouteController)
            && native_changes_rejected(OwnerDrift::OwnedSlot)
            && native_changes_rejected(OwnerDrift::OwnedBgm)
            && native_changes_rejected(OwnerDrift::OwnedSound)
            && native_changes_rejected(OwnerDrift::OwnedSoundHandle)
            && native_changes_rejected(OwnerDrift::OwnedRequest)
            && native_changes_rejected(OwnerDrift::CurrentToken)
            && native_changes_rejected(OwnerDrift::FreshController)
            && native_changes_rejected(OwnerDrift::FreshControllerProof)
            && native_changes_rejected(OwnerDrift::FreshSlot)
            && native_changes_rejected(OwnerDrift::FreshBgm)
            && native_changes_rejected(OwnerDrift::FreshSound)
            && native_changes_rejected(OwnerDrift::FreshSoundHandle)
            && native_changes_rejected(OwnerDrift::FreshRequest)
            && native_changes_rejected(OwnerDrift::FreshRequestType)
            && native_changes_rejected(OwnerDrift::FreshState),
        "incomplete routed native Play ownership allowed native restore changes");

    require(advance_guarded_play_setup_after_restore(
                direct_setup, direct_pre_restore, direct_complete)
            && direct_registry.selection_guard_matches(
                direct_selection, direct_complete)
            && publish_guarded_play_setup_claim_if_proven(
                direct_registry, direct_setup, direct_complete,
                AudioArmPublicationProof::PlaySetupClaimed,
                direct_observation, played_sound_handle)
            && direct_registry.playback_snapshot().token == direct_complete
            && !direct_registry.selection_guard_matches(
                direct_selection, direct_complete),
        "post-restore guarded PlaySetup claim did not advance and consume its guard");

    enum class DirectDrift {
        GuardLoss,
        GuardMismatch,
        MissingPostAdvance,
        StalePreAdvance,
        IncompleteToken,
        StaleToken,
        WrongSelection,
        WrongProof,
        ControllerPointer,
        ControllerClass,
        ControllerNameComparison,
        ControllerNameNumber,
        ControllerOuter,
        ControllerMode,
        ControllerRawIndex,
        ControllerSerial,
        RequestMissing,
        RequestWrongType,
        RequestMismatch,
        WrongState,
        SlotMismatch,
        BgmMismatch,
        SoundMismatch,
        SoundHandleDrift,
        ExistingPlayback,
    };
    const auto direct_rejected = [&](const DirectDrift drift) {
        SongRegistry rejected_registry;
        SelectionSnapshot selection;
        CustomContextToken armed;
        CustomContextToken pre_restore;
        CustomContextToken complete;
        UnpublishedAudioSetupContext setup;
        GuardedPlaySetupClaimObservation pre_observation;
        GuardedPlaySetupClaimObservation observation;
        if (!prepare_direct_pre_restore(
                rejected_registry, selection, armed, pre_restore, complete,
                setup, pre_observation, observation)) return false;
        if (drift == DirectDrift::StalePreAdvance) {
            CustomContextToken stale_pre = pre_restore;
            --stale_pre.route_generation;
            if (advance_guarded_play_setup_after_restore(
                    setup, stale_pre, complete)) return false;
        } else if (drift != DirectDrift::MissingPostAdvance
            && !advance_guarded_play_setup_after_restore(
                setup, pre_restore, complete)) return false;
        CustomContextToken current = complete;
        CustomContextToken active_guard = complete;
        bool guard_expected = true;
        AudioArmPublicationProof publication_proof =
            AudioArmPublicationProof::PlaySetupClaimed;
        if (drift == DirectDrift::GuardLoss) {
            if (!rejected_registry.release_selection_guard(complete)) return false;
            guard_expected = false;
        }
        if (drift == DirectDrift::GuardMismatch) {
            CustomContextToken other_guard = complete;
            ++other_guard.lease_generation;
            ++other_guard.song_key;
            if (!rejected_registry.release_selection_guard(complete)
                || !rejected_registry.acquire_selection_guard(
                    selection, other_guard)) return false;
            active_guard = other_guard;
        }
        if (drift == DirectDrift::IncompleteToken) {
            current.request_handle = 0;
            setup.token = current;
        }
        if (drift == DirectDrift::StaleToken) {
            ++current.route_generation;
        }
        if (drift == DirectDrift::WrongSelection) {
            ++setup.selection.visible_index;
        }
        if (drift == DirectDrift::WrongProof) {
            publication_proof = AudioArmPublicationProof::ControllerRebuildClaimed;
        }
        if (drift == DirectDrift::ControllerPointer) {
            observation.controller = reinterpret_cast<void*>(0x8101);
            observation.controller_proof.controller = observation.controller;
        }
        if (drift == DirectDrift::ControllerClass) {
            observation.controller_proof.object_class =
                reinterpret_cast<void*>(0x8111);
        }
        if (drift == DirectDrift::ControllerNameComparison) {
            ++observation.controller_proof.name_comparison_id;
        }
        if (drift == DirectDrift::ControllerNameNumber) {
            ++observation.controller_proof.name_number;
        }
        if (drift == DirectDrift::ControllerOuter) {
            observation.controller_proof.outer = reinterpret_cast<void*>(0x8121);
        }
        if (drift == DirectDrift::ControllerMode) {
            observation.controller_proof.mode = ControllerIdentityProofMode::Structural;
        }
        if (drift == DirectDrift::ControllerRawIndex) {
            ++observation.controller_proof.raw_internal_index;
            ++observation.controller_proof.item_backed_zero_serial.internal_index;
        }
        if (drift == DirectDrift::ControllerSerial) {
            ++observation.controller_proof.item_backed_zero_serial.serial_number;
        }
        if (drift == DirectDrift::RequestMissing) observation.request_handle = 0;
        if (drift == DirectDrift::RequestWrongType) observation.request_handle = 0xa009;
        if (drift == DirectDrift::RequestMismatch) observation.request_handle = 0xb008;
        if (drift == DirectDrift::WrongState) observation.state = 2;
        if (drift == DirectDrift::SlotMismatch) {
            observation.slot = reinterpret_cast<void*>(0x8201);
        }
        if (drift == DirectDrift::BgmMismatch) {
            observation.bgm = reinterpret_cast<void*>(0x8301);
        }
        if (drift == DirectDrift::SoundMismatch) {
            observation.sound = reinterpret_cast<void*>(0x8401);
        }
        if (drift == DirectDrift::SoundHandleDrift) {
            ++observation.sound_handle.serial_number;
        }
        if (drift == DirectDrift::ExistingPlayback) {
            if (!rejected_registry.release_selection_guard(complete)
                || !rejected_registry.publish_playback(selection, complete)
                || !rejected_registry.acquire_selection_guard(
                    selection, complete)) return false;
        }
        const CustomContextToken setup_token_before = setup.token;
        const PrivateControllerSetupStage setup_stage_before = setup.controller_stage;
        const PlaybackSnapshot playback_before = rejected_registry.playback_snapshot();
        const bool rejected = !publish_guarded_play_setup_claim_if_proven(
            rejected_registry, setup, current, publication_proof, observation,
            played_sound_handle);
        const PlaybackSnapshot playback_after = rejected_registry.playback_snapshot();
        const bool playback_unchanged = playback_before.song
            ? playback_after.song == playback_before.song
                && playback_after.token == playback_before.token
            : !playback_after.song;
        const bool guard_unchanged = guard_expected
            ? rejected_registry.selection_guard_matches(selection, active_guard)
            : !rejected_registry.selection_guard_matches(selection, complete);
        return rejected && playback_unchanged && guard_unchanged
            && setup.token == setup_token_before
            && setup.controller_stage == setup_stage_before;
    };
    require(direct_rejected(DirectDrift::GuardLoss)
            && direct_rejected(DirectDrift::GuardMismatch)
            && direct_rejected(DirectDrift::MissingPostAdvance)
            && direct_rejected(DirectDrift::StalePreAdvance)
            && direct_rejected(DirectDrift::IncompleteToken)
            && direct_rejected(DirectDrift::StaleToken)
            && direct_rejected(DirectDrift::WrongSelection)
            && direct_rejected(DirectDrift::WrongProof)
            && direct_rejected(DirectDrift::ControllerPointer)
            && direct_rejected(DirectDrift::ControllerClass)
            && direct_rejected(DirectDrift::ControllerNameComparison)
            && direct_rejected(DirectDrift::ControllerNameNumber)
            && direct_rejected(DirectDrift::ControllerOuter)
            && direct_rejected(DirectDrift::ControllerMode)
            && direct_rejected(DirectDrift::ControllerRawIndex)
            && direct_rejected(DirectDrift::ControllerSerial)
            && direct_rejected(DirectDrift::RequestMissing)
            && direct_rejected(DirectDrift::RequestWrongType)
            && direct_rejected(DirectDrift::RequestMismatch)
            && direct_rejected(DirectDrift::WrongState)
            && direct_rejected(DirectDrift::SlotMismatch)
            && direct_rejected(DirectDrift::BgmMismatch)
            && direct_rejected(DirectDrift::SoundMismatch)
            && direct_rejected(DirectDrift::SoundHandleDrift)
            && direct_rejected(DirectDrift::ExistingPlayback),
        "guarded direct PlaySetup mismatch published playback");

    require(classify_audio_arm_publication(
                AudioArmPublicationProof::ControllerRebuildClaimed,
                PrivateControllerSetupStage::StopObserved)
                == AudioArmPublicationPath::Reject
            && classify_audio_arm_publication(
                AudioArmPublicationProof::PlaySetupClaimed,
                PrivateControllerSetupStage::SetCaptured)
                == AudioArmPublicationPath::Reject
            && classify_audio_arm_publication(
                AudioArmPublicationProof::PlaySetupClaimed,
                PrivateControllerSetupStage::Patched)
                == AudioArmPublicationPath::Reject
            && classify_audio_arm_publication(
                AudioArmPublicationProof::PlaySetupClaimed,
                PrivateControllerSetupStage::SetForwarded)
                == AudioArmPublicationPath::Reject
            && classify_audio_arm_publication(
                AudioArmPublicationProof::PlaySetupClaimed,
                PrivateControllerSetupStage::Claimed)
                == AudioArmPublicationPath::PrivateControllerClaim,
        "private publication stages were broadened beyond the observed direct path");

    SongRegistry generic_registry;
    generic_registry.replace({song});
    generic_registry.set_active_selection(9, 0);
    const SelectionSnapshot generic_selection = generic_registry.selection_snapshot();
    CustomContextToken generic_claim = successful_claim;
    generic_claim.registry_generation = generic_selection.generation;
    generic_claim.route_generation = 100;
    generic_claim.lease_generation = 101;
    generic_claim.song_key = 102;
    UnpublishedAudioSetupContext generic_setup{generic_selection, generic_claim};
    require(classify_audio_arm_publication(
                AudioArmPublicationProof::PlaySetupClaimed,
                generic_setup.controller_stage) == AudioArmPublicationPath::Generic
            && publish_audio_arm_if_proven(generic_registry, generic_selection,
                generic_claim, AudioArmPublicationProof::PlaySetupClaimed),
        "generic PlaySetup without a private transaction stopped publishing generically");

    require(successful_registry.revoke_playback(successful_claim)
            && successful_registry.cleanup_lease().token == successful_claim
            && successful_registry.retire_cleanup_lease(successful_claim)
            && !successful_registry.cleanup_lease().song,
        "successful reentrant claim did not retain cleanup through retirement");
    successful_registry.set_active_selection(9, 0);
    const SelectionSnapshot next_selection = successful_registry.selection_snapshot();
    CustomContextToken next_arm = successful_armed;
    next_arm.registry_generation = next_selection.generation;
    next_arm.route_generation = 110;
    next_arm.lease_generation = 111;
    next_arm.song_key = 112;
    require(successful_registry.acquire_selection_guard(next_selection, next_arm),
        "retired reentrant claim prevented a new route arm");

    require(direct_registry.revoke_playback(direct_complete)
            && direct_registry.cleanup_lease().token == direct_complete
            && direct_registry.retire_cleanup_lease(direct_complete)
            && !direct_registry.cleanup_lease().song,
        "guarded direct claim did not retain cleanup through retirement");
    direct_registry.set_active_selection(9, 0);
    const SelectionSnapshot next_direct_selection =
        direct_registry.selection_snapshot();
    CustomContextToken next_direct_arm = direct_armed;
    next_direct_arm.registry_generation = next_direct_selection.generation;
    next_direct_arm.route_generation = 130;
    next_direct_arm.lease_generation = 131;
    next_direct_arm.song_key = 132;
    require(direct_registry.acquire_selection_guard(
                next_direct_selection, next_direct_arm),
        "retired guarded direct claim prevented a second route arm");
}

} // namespace ff7r::piano::tests::runtime_lifecycle
