#include "game/bgm_playback_aggregate_policy.h"
#include "game/audio_native_call_policy.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "bgm_playback_aggregate_policy_selftest: " << message << '\n';
        std::exit(1);
    }
}

void test_bgm_playback_aggregate_mutation_lease()
{
    using namespace ff7r::piano::game;
    using Gate = BgmPlaybackAggregateMutationGate;
    using Lease = BgmPlaybackAggregateMutationLease<Gate>;
    constexpr auto failure_bound = std::chrono::seconds(5);
    const auto no_exit = []() noexcept { return false; };

    Gate ordered_gate;
    Lease ordered_lease(ordered_gate, no_exit);
    std::atomic_uint32_t order{0}, published_at{0}, left_at{0}, closed_at{0};
    std::atomic_bool close_returned{false};
    std::promise<void> published, close_complete;
    auto published_future = published.get_future();
    auto close_future = close_complete.get_future();
    std::thread closer([&] {
        ordered_gate.close_and_drain([&]() noexcept {
            published_at = ++order;
            published.set_value();
        });
        closed_at = ++order;
        close_returned = true;
        close_complete.set_value();
    });
    require(published_future.wait_for(failure_bound) == std::future_status::ready,
        "aggregate close did not publish within failure bound");
    require(!close_returned && ordered_lease.active,
        "aggregate close returned before admitted lease release");
    left_at = ++order;
    require(ordered_lease.release(), "aggregate lease release was not owned");
    require(close_future.wait_for(failure_bound) == std::future_status::ready,
        "aggregate close did not return after explicit release");
    closer.join();
    require(published_at < left_at && left_at < closed_at,
        "aggregate publish/leave/close-return order changed");
    Lease rejected(ordered_gate, no_exit);
    require(!rejected.active, "closed aggregate gate admitted a lease");

    struct CountingGate final {
        bool enter() noexcept { ++enters; ++active; return true; }
        void leave() noexcept { ++leaves; --active; }
        uint32_t enters = 0, leaves = 0, active = 0;
    } precheck_count;
    using CountingLease = BgmPlaybackAggregateMutationLease<CountingGate>;
    CountingLease precheck_rejected(
        precheck_count, []() noexcept { return true; });
    require(!precheck_rejected.active && precheck_count.enters == 0
            && precheck_count.leaves == 0,
        "exit precheck reached the aggregate gate");

    CountingGate count;
    uint32_t checks = 0;
    CountingLease raced(count, [&]() noexcept { return ++checks == 2; });
    require(!raced.active && checks == 2 && count.enters == 1
            && count.leaves == 1 && count.active == 0,
        "post-enter exit race did not release exactly once");

    const auto returns_with_lease = [&] {
        CountingLease lease(count, no_exit);
        return lease.active;
    };
    auto leaves = count.leaves;
    require(returns_with_lease() && count.leaves == leaves + 1,
        "return-path destruction did not release exactly once");
    leaves = count.leaves;
    try {
        CountingLease lease(count, no_exit);
        throw std::runtime_error("aggregate lease unwind");
    } catch (const std::runtime_error&) {
    }
    require(count.leaves == leaves + 1,
        "exception unwind did not release exactly once");
    leaves = count.leaves;
    {
        CountingLease source(count, no_exit);
        CountingLease destination(std::move(source));
        require(!source.active && destination.active,
            "aggregate lease move duplicated or lost ownership");
    }
    require(count.leaves == leaves + 1,
        "moved lease did not release exactly once");
    leaves = count.leaves;
    {
        CountingLease lease(count, no_exit);
        require(lease.release() && !lease.release(),
            "aggregate lease release was not idempotent");
    }
    require(count.leaves == leaves + 1,
        "release plus destruction released more than once");

    Gate reopen_gate;
    Lease lease_a(reopen_gate, no_exit);
    std::promise<void> drain_published, drain_complete;
    auto drain_published_future = drain_published.get_future();
    auto drain_future = drain_complete.get_future();
    std::atomic_bool drain_returned{false};
    std::thread drain_closer([&] {
        reopen_gate.close_and_drain(
            [&]() noexcept { drain_published.set_value(); });
        drain_returned = true;
        drain_complete.set_value();
    });
    require(drain_published_future.wait_for(failure_bound)
            == std::future_status::ready,
        "controlled drain did not publish close");
    uint32_t conditional_checks = 0;
    require(!reopen_gate.reopen_after_drain_if([&]() noexcept {
                ++conditional_checks;
                return true;
            }) && conditional_checks == 0,
        "conditional reopen ran while a lease was active");
    reopen_gate.reopen();
    Lease lease_b(reopen_gate, no_exit);
    require(lease_b.active && lease_a.release() && !drain_returned,
        "unconditional reopen did not make close wait for A/B");
    require(lease_b.release(), "reopened lease B was not released");
    require(drain_future.wait_for(failure_bound) == std::future_status::ready,
        "controlled drain did not return after A/B release");
    drain_closer.join();
    require(!reopen_gate.closed() && reopen_gate.drained(),
        "controlled reopen terminal state changed");

    Gate self_gate;
    std::promise<void> self_published, self_complete;
    auto self_signal = self_published.get_future().share();
    auto self_future = self_complete.get_future();
    std::thread harness_release([&] { self_signal.wait(); self_gate.leave(); });
    std::thread self_closer([&] {
        require(self_gate.enter(), "self-close harness admission failed");
        self_gate.close_and_drain(
            [&]() noexcept { self_published.set_value(); });
        self_complete.set_value();
    });
    require(self_future.wait_for(failure_bound) == std::future_status::ready,
        "self-owned close did not return after explicit harness release");
    self_closer.join();
    harness_release.join();
    require(self_gate.closed() && self_gate.drained(),
        "self-owned close did not finish closed and drained");
}

void test_bgm_playback_aggregate_observer_policy()
{
    using namespace ff7r::piano::game;
    const BgmPlaybackTransferSnapshot source{
        true, 0x1000, 0x400010008, 3, 7, 11, 13, 0xabc};
    const BgmPlaybackTransferSnapshot destination{
        true, 0x1000, 0x400010008, 3, 7, 11, 13, 0xabc};
    const BgmPlaybackTransferSnapshot zeroed{true, 0, 0, 0, 0, 0, 0, 0};
    require(bgm_playback_transfer_destination_adopted(source, destination)
            && bgm_playback_transfer_source_zeroed(zeroed),
        "exact playback transfer observation was rejected");
    auto drifted = destination;
    drifted.moved_handle = 0x500010008;
    require(!bgm_playback_transfer_destination_adopted(source, drifted),
        "playback transfer accepted handle-generation drift");
    drifted = destination;
    drifted.optional_token = 0xdef;
    require(!bgm_playback_transfer_destination_adopted(source, drifted),
        "playback transfer accepted optional-token drift");

    BgmPlaybackLineageAdmissionFacts presession_admission{
        true, true, true, true, true, true, true, true, true, true, 0, 0};
    require(classify_bgm_playback_lineage_admission(presession_admission)
            == BgmPlaybackLineageProvenance::PreSession,
        "exact pre-session transfer was unreachable");
    auto session_admission = presession_admission;
    session_admission.session_epoch = 17;
    session_admission.cycle_epoch = 19;
    require(classify_bgm_playback_lineage_admission(session_admission)
            == BgmPlaybackLineageProvenance::SessionScoped,
        "exact session-scoped transfer was rejected");
    auto partial_session = session_admission;
    partial_session.cycle_epoch = 0;
    require(classify_bgm_playback_lineage_admission(partial_session)
            == BgmPlaybackLineageProvenance::Invalid,
        "partial session provenance was invented");
    bool BgmPlaybackLineageAdmissionFacts::* const admission_guards[] = {
        &BgmPlaybackLineageAdmissionFacts::route_lease_exact,
        &BgmPlaybackLineageAdmissionFacts::custom_cleanup_owned,
        &BgmPlaybackLineageAdmissionFacts::lifecycle_restore_applied,
        &BgmPlaybackLineageAdmissionFacts::lifecycle_tokens_exact,
        &BgmPlaybackLineageAdmissionFacts::owner_thread_exact,
        &BgmPlaybackLineageAdmissionFacts::command_exact,
        &BgmPlaybackLineageAdmissionFacts::controller_exact,
        &BgmPlaybackLineageAdmissionFacts::sound_live_exact,
        &BgmPlaybackLineageAdmissionFacts::release_clear,
        &BgmPlaybackLineageAdmissionFacts::failure_clear,
    };
    for (auto guard : admission_guards) {
        auto rejected = presession_admission;
        rejected.*guard = false;
        require(classify_bgm_playback_lineage_admission(rejected)
                == BgmPlaybackLineageProvenance::Invalid,
            "pre-session admission accepted a missing exact guard");
    }

    BgmPlaybackRetirementAnchorFacts anchor{
        true, true, true, true, true, true, true, true, true};
    require(bgm_playback_retirement_anchor_exact(anchor),
        "exact pre-session RetirementCandidate anchor was rejected");
    bool BgmPlaybackRetirementAnchorFacts::* const anchor_guards[] = {
        &BgmPlaybackRetirementAnchorFacts::retirement_candidate,
        &BgmPlaybackRetirementAnchorFacts::zero_session_epochs,
        &BgmPlaybackRetirementAnchorFacts::detached_restore_applied,
        &BgmPlaybackRetirementAnchorFacts::detached_identity_exact,
        &BgmPlaybackRetirementAnchorFacts::detached_token_epoch_exact,
        &BgmPlaybackRetirementAnchorFacts::lifecycle_state_epoch_exact,
        &BgmPlaybackRetirementAnchorFacts::detached_tokens_exact,
        &BgmPlaybackRetirementAnchorFacts::detached_request_exact,
        &BgmPlaybackRetirementAnchorFacts::controller_exact,
    };
    for (auto guard : anchor_guards) {
        auto rejected = anchor;
        rejected.*guard = false;
        require(!bgm_playback_retirement_anchor_exact(rejected),
            "pre-session lineage accepted an incomplete retirement anchor");
    }
    auto ordinal_drift = anchor;
    ordinal_drift.detached_token_epoch_exact = false;
    auto state_epoch_drift = anchor;
    state_epoch_drift.lifecycle_state_epoch_exact = false;
    require(!bgm_playback_retirement_anchor_exact(ordinal_drift)
            && !bgm_playback_retirement_anchor_exact(state_epoch_drift),
        "detached ordinal and lifecycle state epoch were conflated");

    BgmPlaybackCanonicalStopBoundaryFacts stop_boundary{
        true, true, true, true, true, true, true, true, true, true, true};
    require(bgm_playback_canonical_stop_boundary_exact(stop_boundary),
        "exact parent canonical-intermediate Stop boundary was rejected");
    bool BgmPlaybackCanonicalStopBoundaryFacts::* const boundary_guards[] = {
        &BgmPlaybackCanonicalStopBoundaryFacts::parent_active,
        &BgmPlaybackCanonicalStopBoundaryFacts::parent_anchored,
        &BgmPlaybackCanonicalStopBoundaryFacts::canonical_play_confirmed,
        &BgmPlaybackCanonicalStopBoundaryFacts::canonical_handle_exact,
        &BgmPlaybackCanonicalStopBoundaryFacts::canonical_sound_pointer_exact,
        &BgmPlaybackCanonicalStopBoundaryFacts::canonical_sound_identity_exact,
        &BgmPlaybackCanonicalStopBoundaryFacts::controller_chain_exact,
        &BgmPlaybackCanonicalStopBoundaryFacts::owner_thread_exact,
        &BgmPlaybackCanonicalStopBoundaryFacts::owner_command_exact,
        &BgmPlaybackCanonicalStopBoundaryFacts::lease_lifecycle_tokens_exact,
        &BgmPlaybackCanonicalStopBoundaryFacts::boundary_clear,
    };
    for (auto guard : boundary_guards) {
        auto rejected = stop_boundary;
        rejected.*guard = false;
        require(!bgm_playback_canonical_stop_boundary_exact(rejected),
            "canonical Stop boundary accepted identity, command, epoch, or ABA drift");
    }
    BgmPlaybackCanonicalParentLookupFacts parent_lookup{
        true, true, true, true, true, true, true, true, true, true, true, true, true};
    require(bgm_playback_canonical_parent_first_failure(parent_lookup)
            == BgmPlaybackCanonicalParentFailure::None,
        "runtime-shaped canonical parent lookup was rejected");
    require(bgm_playback_canonical_to_custom_command_edge_exact(
                0x4fe83c, 0x4ae, 0x4fe83c)
            && !bgm_playback_canonical_to_custom_command_edge_exact(
                0, 0x4ae, 0x4fe83c)
            && !bgm_playback_canonical_to_custom_command_edge_exact(
                0x4fe83c, 0x4ae, 0x4fe956)
            && !bgm_playback_canonical_to_custom_command_edge_exact(
                0x4ae, 0x4ae, 0x4ae),
        "canonical-to-anchored-custom command edge admitted missing or unrelated command");
    require(bgm_playback_operation_nonce_next(0, 1)
            && bgm_playback_operation_nonce_next(2, 3)
            && !bgm_playback_operation_nonce_next(0, 2)
            && !bgm_playback_operation_nonce_next(2, 4)
            && !bgm_playback_operation_nonce_next(UINT64_MAX, 0)
            && !bgm_playback_set_commit_exact(true, true, true, 3, 5)
            && !bgm_playback_play_join_exact(true, true, true, 5, 7),
        "boundary, Set, or Play accepted a skipped/wrapped operation nonce");
    require(bgm_playback_initial_predecessor_publication_exact(
                true, true, true, true, 8, 8)
            && !bgm_playback_initial_predecessor_publication_exact(
                true, true, true, true, 8, 9)
            && !bgm_playback_initial_predecessor_publication_exact(
                true, true, true, false, 8, 8)
            && bgm_playback_set_commit_exact(true, true, true, 8, 9)
            && bgm_playback_play_join_exact(true, true, true, 9, 10)
            && !bgm_playback_set_commit_exact(true, true, true, 8, 10),
        "initial lineage did not preserve its synchronized operation predecessor");
    require(bgm_playback_exact_successor(8, 9)
            && !bgm_playback_exact_successor(8, 10)
            && !bgm_playback_exact_successor(UINT64_MAX, 0),
        "generic successor admitted a skipped or wrapping value");
    require(bgm_playback_blocked_canonical_boundary_invalidates(
                true, true, true, true)
            && !bgm_playback_blocked_canonical_boundary_invalidates(
                true, true, true, false)
            && !bgm_playback_blocked_canonical_boundary_invalidates(
                true, false, true, true)
            && bgm_playback_boundary_must_clear(
                false, false, false, false, false, true),
        "blocked duplicate or unrelated transfer left a consumable stale boundary");
    require(bgm_playback_intervening_operation_invalidated_exact(
                true, true, true, true, true, true, true)
            && !bgm_playback_intervening_operation_invalidated_exact(
                true, true, true, true, true, true, false)
            && bgm_playback_operation_nonce_next(5, 6)
            && bgm_playback_operation_nonce_next(6, 7),
        "recognized unmatched/null Set or unmatched Play left reusable provenance");
    constexpr uint64_t parent_confirmed_custom_play = 13;
    constexpr uint64_t unrelated_global_snapshot = 19;
    constexpr uint64_t inherited_child_predecessor =
        parent_confirmed_custom_play;
    require(inherited_child_predecessor == parent_confirmed_custom_play
            && inherited_child_predecessor != unrelated_global_snapshot
            && bgm_playback_set_commit_exact(
                true, true, true, inherited_child_predecessor, 14),
        "child predecessor was refreshed from global ordering instead of parent Play");
    struct ParentLookupGuard {
        bool BgmPlaybackCanonicalParentLookupFacts::* member;
        BgmPlaybackCanonicalParentFailure failure;
    };
    const ParentLookupGuard parent_lookup_guards[] = {
        {&BgmPlaybackCanonicalParentLookupFacts::parent_active, BgmPlaybackCanonicalParentFailure::ParentInactive},
        {&BgmPlaybackCanonicalParentLookupFacts::parent_anchored, BgmPlaybackCanonicalParentFailure::ParentUnanchored},
        {&BgmPlaybackCanonicalParentLookupFacts::canonical_play_confirmed, BgmPlaybackCanonicalParentFailure::CanonicalPlayUnconfirmed},
        {&BgmPlaybackCanonicalParentLookupFacts::canonical_handle_exact, BgmPlaybackCanonicalParentFailure::CanonicalHandleMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::canonical_sound_pointer_exact, BgmPlaybackCanonicalParentFailure::CanonicalSoundPointerMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::canonical_sound_identity_exact, BgmPlaybackCanonicalParentFailure::CanonicalSoundIdentityMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::controller_identity_exact, BgmPlaybackCanonicalParentFailure::ControllerIdentityMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::live_slot_bgm_exact, BgmPlaybackCanonicalParentFailure::LiveSlotBgmMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::owner_thread_exact, BgmPlaybackCanonicalParentFailure::OwnerThreadMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::owner_command_exact, BgmPlaybackCanonicalParentFailure::OwnerCommandMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::route_successor_exact, BgmPlaybackCanonicalParentFailure::RouteSuccessorMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::lease_lifecycle_tokens_exact, BgmPlaybackCanonicalParentFailure::LeaseLifecycleTokenMismatch},
        {&BgmPlaybackCanonicalParentLookupFacts::version_and_boundary_clear, BgmPlaybackCanonicalParentFailure::VersionOrBoundaryReuse},
    };
    for (const auto& guard : parent_lookup_guards) {
        auto rejected = parent_lookup;
        rejected.*guard.member = false;
        require(bgm_playback_canonical_parent_first_failure(rejected)
                == guard.failure,
            "canonical parent first-failed predicate was not deterministic");
    }
    constexpr uint64_t runtime_custom_old = 0x0000000400010008ull;
    constexpr uint64_t runtime_canonical = 0x0000000500000008ull;
    constexpr uint64_t runtime_custom_next = 0x0000000600020008ull;
    constexpr uintptr_t runtime_custom_sound = 0x00007fee4fb7f200ull;
    constexpr uintptr_t runtime_canonical_sound = 0x00007fee4fbbdc00ull;
    constexpr int32_t runtime_custom_index = 397459;
    constexpr int32_t runtime_custom_serial = 93227;
    constexpr int32_t runtime_canonical_index = 397469;
    constexpr int32_t runtime_canonical_serial = 93123;
    constexpr int32_t command_edge_custom_index = 397414;
    constexpr int32_t command_edge_custom_serial = 93239;
    constexpr int32_t command_edge_canonical_index = 397480;
    constexpr int32_t command_edge_canonical_serial = 93135;
    require(runtime_custom_old < runtime_canonical
            && runtime_canonical < runtime_custom_next
            && runtime_custom_sound != runtime_canonical_sound
            && runtime_custom_index != runtime_canonical_index
            && runtime_custom_serial > 0 && runtime_canonical_serial > 0
            && command_edge_custom_index != command_edge_canonical_index
            && command_edge_custom_serial > 0
            && command_edge_canonical_serial > 0
            && bgm_playback_exact_successor(5, 6)
            && bgm_playback_exact_successor(2, 3)
            && bgm_playback_exact_successor(3, 4)
            && !bgm_playback_exact_successor(5, 7)
            && !bgm_playback_exact_successor(3, 3),
        "runtime 0x400/0x500/0x600 version, nonce, or route successors drifted");
    require(bgm_playback_boundary_destination_exact(
                true, true, true, true, true, true, false)
            && !bgm_playback_boundary_destination_exact(
                true, false, false, false, false, false, false)
            && !bgm_playback_boundary_destination_exact(
                true, true, true, false, true, true, true)
            && !bgm_playback_boundary_destination_exact(
                false, true, true, true, true, true, false),
        "removed, unreadable, or ABA-reused boundary destination was accepted");
    require(bgm_playback_post_set_snapshot_exact(
                true, true, true, true, true, true, true, true),
        "exact post-Set snapshot was rejected");
    for (int drift = 0; drift < 8; ++drift) {
        bool facts[8] = {true, true, true, true, true, true, true, true};
        facts[drift] = false;
        require(!bgm_playback_post_set_snapshot_exact(
                facts[0], facts[1], facts[2], facts[3], facts[4], facts[5],
                facts[6], facts[7]),
            "post-Set slot/BGM, route, lifecycle, token, owner, or command drift was accepted");
    }
    BgmPlaybackSetCommitFacts set_commit_facts{
        true, true, true, true, true, true, true, true, true, true, true, true};
    require(bgm_playback_set_commit_first_failure(set_commit_facts)
            == BgmPlaybackSetCommitFailure::None,
        "exact inherited-child Set commit facts were rejected");
    struct SetCommitGuard {
        bool BgmPlaybackSetCommitFacts::* member;
        BgmPlaybackSetCommitFailure failure;
    };
    const SetCommitGuard set_commit_guards[] = {
        {&BgmPlaybackSetCommitFacts::target_present, BgmPlaybackSetCommitFailure::TargetMissing},
        {&BgmPlaybackSetCommitFacts::record_live, BgmPlaybackSetCommitFailure::RecordFailed},
        {&BgmPlaybackSetCommitFacts::record_version_exact, BgmPlaybackSetCommitFailure::RecordVersion},
        {&BgmPlaybackSetCommitFacts::collection_version_exact, BgmPlaybackSetCommitFailure::CollectionVersion},
        {&BgmPlaybackSetCommitFacts::owner_exact, BgmPlaybackSetCommitFailure::Owner},
        {&BgmPlaybackSetCommitFacts::thread_exact, BgmPlaybackSetCommitFailure::Thread},
        {&BgmPlaybackSetCommitFacts::command_exact, BgmPlaybackSetCommitFailure::Command},
        {&BgmPlaybackSetCommitFacts::post_set_snapshot_exact, BgmPlaybackSetCommitFailure::PostSetSnapshot},
        {&BgmPlaybackSetCommitFacts::boundary_state_exact, BgmPlaybackSetCommitFailure::BoundaryState},
        {&BgmPlaybackSetCommitFacts::request_generation_exact, BgmPlaybackSetCommitFailure::RequestGeneration},
        {&BgmPlaybackSetCommitFacts::applied_state_exact, BgmPlaybackSetCommitFailure::AppliedState},
        {&BgmPlaybackSetCommitFacts::nonce_exact, BgmPlaybackSetCommitFailure::Nonce},
    };
    for (const auto& guard : set_commit_guards) {
        auto rejected = set_commit_facts;
        rejected.*guard.member = false;
        require(bgm_playback_set_commit_first_failure(rejected) == guard.failure,
            "Set commit first-failed predicate was not deterministic");
    }
    BgmPlaybackPlayFacts play_facts{
        true, true, true, true, true, true, true, true, true, true, true, true,
        true, true};
    require(bgm_playback_play_first_failure(play_facts)
            == BgmPlaybackPlayFailure::None,
        "exact Set-created state-4 Play facts were rejected");
    struct PlayGuard {
        bool BgmPlaybackPlayFacts::* member;
        BgmPlaybackPlayFailure failure;
    };
    const PlayGuard play_guards[] = {
        {&BgmPlaybackPlayFacts::pending_set, BgmPlaybackPlayFailure::PendingSet},
        {&BgmPlaybackPlayFacts::publication_authority_exact,
            BgmPlaybackPlayFailure::PublicationAuthority},
        {&BgmPlaybackPlayFacts::controller_chain_exact, BgmPlaybackPlayFailure::ControllerChain},
        {&BgmPlaybackPlayFacts::owner_thread_exact, BgmPlaybackPlayFailure::OwnerThread},
        {&BgmPlaybackPlayFacts::command_exact, BgmPlaybackPlayFailure::Command},
        {&BgmPlaybackPlayFacts::route_exact, BgmPlaybackPlayFailure::Route},
        {&BgmPlaybackPlayFacts::lease_exact, BgmPlaybackPlayFailure::Lease},
        {&BgmPlaybackPlayFacts::lifecycle_state_exact, BgmPlaybackPlayFailure::LifecycleState},
        {&BgmPlaybackPlayFacts::token_ordinal_exact, BgmPlaybackPlayFailure::TokenOrdinal},
        {&BgmPlaybackPlayFacts::token_values_exact, BgmPlaybackPlayFailure::TokenValues},
        {&BgmPlaybackPlayFacts::request_sound_identity_exact, BgmPlaybackPlayFailure::RequestSoundIdentity},
        {&BgmPlaybackPlayFacts::request_handle_exact, BgmPlaybackPlayFailure::RequestHandle},
        {&BgmPlaybackPlayFacts::state4, BgmPlaybackPlayFailure::State4},
        {&BgmPlaybackPlayFacts::nonce_exact, BgmPlaybackPlayFailure::Nonce},
    };
    for (const auto& guard : play_guards) {
        auto rejected = play_facts;
        rejected.*guard.member = false;
        require(bgm_playback_play_first_failure(rejected) == guard.failure,
            "Play first-failed predicate was not deterministic");
    }
    constexpr uint64_t play_fixture_canonical_command = 0x4ae;
    constexpr uint64_t play_fixture_custom_command = 0x3cda92;
    constexpr uint64_t play_fixture_custom_request = 0x0000000600020008ULL;
    constexpr uint64_t play_fixture_set_nonce = 12;
    constexpr uint64_t play_fixture_play_nonce = 13;
    require(play_fixture_canonical_command != play_fixture_custom_command
            && bgm_playback_custom_play_intended_command_exact(
                true, true, play_fixture_custom_command,
                play_fixture_canonical_command, play_fixture_custom_command)
            && bgm_playback_custom_play_post_native_owner_phase_exact(
                true, true, kBgmPlaybackCustomPlayOwnerState,
                play_fixture_custom_command, play_fixture_custom_command)
            && !bgm_playback_custom_play_post_native_owner_phase_exact(
                true, true, 0, play_fixture_custom_command,
                play_fixture_custom_command)
            && !bgm_playback_custom_play_post_native_owner_phase_exact(
                true, false, kBgmPlaybackCustomPlayOwnerState,
                play_fixture_custom_command, play_fixture_custom_command)
            && !bgm_playback_custom_play_post_native_owner_phase_exact(
                true, true, 7, play_fixture_custom_command,
                play_fixture_custom_command)
            && !bgm_playback_custom_play_post_native_owner_phase_exact(
                true, true, kBgmPlaybackCustomPlayOwnerState,
                play_fixture_custom_command, play_fixture_canonical_command)
            && bgm_playback_operation_nonce_next(
                play_fixture_set_nonce, play_fixture_play_nonce)
            && AudioBgmRequestHandle{
                play_fixture_custom_request}.valid_bgm_request()
            && bgm_playback_play_first_failure(play_facts)
                == BgmPlaybackPlayFailure::None,
        "custom Play publication did not separate pre-native intent from exact post-native proof");
    for (const uint8_t rejected_state : {uint8_t{0}, uint8_t{7}, uint8_t{9}}) {
        auto rejected_phase = play_facts;
        rejected_phase.command_exact =
            bgm_playback_custom_play_post_native_owner_phase_exact(
                true, true, rejected_state, play_fixture_custom_command,
                play_fixture_custom_command);
        require(bgm_playback_play_first_failure(rejected_phase)
                == BgmPlaybackPlayFailure::Command,
            "custom Play publication accepted an unrelated owner state");
    }
    auto unreadable_custom_phase = play_facts;
    unreadable_custom_phase.command_exact =
        bgm_playback_custom_play_post_native_owner_phase_exact(
            true, false, kBgmPlaybackCustomPlayOwnerState,
            play_fixture_custom_command, play_fixture_custom_command);
    auto unrelated_custom_command = play_facts;
    unrelated_custom_command.command_exact =
        bgm_playback_custom_play_post_native_owner_phase_exact(
            true, true, kBgmPlaybackCustomPlayOwnerState,
            play_fixture_custom_command, play_fixture_canonical_command);
    require(bgm_playback_play_first_failure(unreadable_custom_phase)
                == BgmPlaybackPlayFailure::Command
            && bgm_playback_play_first_failure(unrelated_custom_command)
                == BgmPlaybackPlayFailure::Command,
        "custom Play publication accepted unreadable state or unrelated command");
    const struct PlayPublicationFault {
        bool BgmPlaybackPlayFacts::* member;
        BgmPlaybackPlayFailure failure;
    } play_publication_faults[] = {
        {&BgmPlaybackPlayFacts::publication_authority_exact,
            BgmPlaybackPlayFailure::PublicationAuthority},
        {&BgmPlaybackPlayFacts::controller_chain_exact,
            BgmPlaybackPlayFailure::ControllerChain},
        {&BgmPlaybackPlayFacts::owner_thread_exact,
            BgmPlaybackPlayFailure::OwnerThread},
        {&BgmPlaybackPlayFacts::command_exact,
            BgmPlaybackPlayFailure::Command},
        {&BgmPlaybackPlayFacts::route_exact, BgmPlaybackPlayFailure::Route},
        {&BgmPlaybackPlayFacts::lease_exact, BgmPlaybackPlayFailure::Lease},
        {&BgmPlaybackPlayFacts::lifecycle_state_exact,
            BgmPlaybackPlayFailure::LifecycleState},
        {&BgmPlaybackPlayFacts::token_ordinal_exact,
            BgmPlaybackPlayFailure::TokenOrdinal},
        {&BgmPlaybackPlayFacts::token_values_exact,
            BgmPlaybackPlayFailure::TokenValues},
        {&BgmPlaybackPlayFacts::request_sound_identity_exact,
            BgmPlaybackPlayFailure::RequestSoundIdentity},
        {&BgmPlaybackPlayFacts::request_handle_exact,
            BgmPlaybackPlayFailure::RequestHandle},
        {&BgmPlaybackPlayFacts::state4, BgmPlaybackPlayFailure::State4},
        {&BgmPlaybackPlayFacts::nonce_exact, BgmPlaybackPlayFailure::Nonce},
    };
    for (const auto& fault : play_publication_faults) {
        auto rejected = play_facts;
        rejected.*fault.member = false;
        require(bgm_playback_play_first_failure(rejected) == fault.failure,
            "post-native Play publication fault did not fail closed");
    }
    BgmPlaybackPendingPlayTransactionFacts successful_play_transaction{
        true, true, true, 1, true, true, false, false, false};
    require(bgm_playback_pending_play_ownership_durable(
                successful_play_transaction)
            && !bgm_playback_pending_play_release_blocked(
                successful_play_transaction),
        "exact custom Play publication did not discharge pre-native ownership");
    for (const char* fault : {"cpp_exception", "translated_seh", "unreadable"}) {
        (void)fault;
        BgmPlaybackPendingPlayTransactionFacts faulted_transaction{
            true, false, true, 1, true, false, true, false, false};
        require(bgm_playback_pending_play_ownership_durable(
                    faulted_transaction)
                && bgm_playback_pending_play_release_blocked(
                    faulted_transaction)
                && bgm_playback_aggregate_exit_ownership_present(
                    {true, true, false, false, false, false})
                && !bgm_playback_aggregate_menu_ready(
                    {true, true, false, false, false, false}, false, true)
                && !bgm_playback_aggregate_release_exact(
                    true, false, true, true, true, true),
            "faulted post-native capture did not retain unresolved ownership");
        faulted_transaction.exact_request_resolved_or_absent = true;
        require(!bgm_playback_pending_play_release_blocked(
                    faulted_transaction),
            "independent exact request resolution did not discharge unresolved Play");
    }
    BgmPlaybackPendingPlayTransactionFacts unknown_capture_transaction{
        true, false, true, 1, true, false, false, true, false};
    require(bgm_playback_pending_play_ownership_durable(
                unknown_capture_transaction)
            && bgm_playback_pending_play_release_blocked(
                unknown_capture_transaction),
        "unknown post-native capture did not preserve the global latch");
    require(!bgm_playback_pending_play_latch_discharge_allowed(
                unknown_capture_transaction),
        "unknown pending Play discharged its latch before exact resolution");
    auto preparation_fault_transaction = unknown_capture_transaction;
    preparation_fault_transaction.native_state4_observed = false;
    require(bgm_playback_pending_play_ownership_durable(
                preparation_fault_transaction)
            && !bgm_playback_pending_play_latch_discharge_allowed(
                preparation_fault_transaction),
        "pending custom Play preparation fault lost pre-native ownership");
    const BgmPlaybackPendingPlayAuthorityDecisionFacts authority_only_exact{
        true, true, false, true};
    const BgmPlaybackPendingPlayAuthorityDecisionFacts authority_only_inexact{
        true, true, false, false};
    const BgmPlaybackPendingPlayAuthorityDecisionFacts owner_patch_only_fault{
        true, false, true, false};
    const BgmPlaybackPendingPlayAuthorityDecisionFacts both_exact{
        true, true, true, true};
    const BgmPlaybackPendingPlayAuthorityDecisionFacts patch_mutation_failed{
        true, true, true, false};
    const BgmPlaybackPendingPlayAuthorityDecisionFacts no_candidate{
        true, false, false, false};
    require(bgm_playback_pending_play_latch_required(authority_only_exact)
            && bgm_playback_pending_play_normal_publication_allowed(
                authority_only_exact)
            && bgm_playback_pending_play_candidate_source(authority_only_exact)
                == BgmPlaybackPendingPlayCandidateSource::Authority
            && bgm_playback_pending_play_latch_required(authority_only_inexact)
            && !bgm_playback_pending_play_normal_publication_allowed(
                authority_only_inexact)
            && bgm_playback_pending_play_latch_required(owner_patch_only_fault)
            && !bgm_playback_pending_play_normal_publication_allowed(
                owner_patch_only_fault)
            && bgm_playback_pending_play_candidate_source(owner_patch_only_fault)
                == BgmPlaybackPendingPlayCandidateSource::OwnerPatch
            && bgm_playback_pending_play_latch_required(both_exact)
            && bgm_playback_pending_play_normal_publication_allowed(both_exact)
            && bgm_playback_pending_play_candidate_source(both_exact)
                == BgmPlaybackPendingPlayCandidateSource::Both
            && bgm_playback_pending_play_latch_required(patch_mutation_failed)
            && !bgm_playback_pending_play_normal_publication_allowed(
                patch_mutation_failed)
            && bgm_playback_pending_play_candidate_source(patch_mutation_failed)
                == BgmPlaybackPendingPlayCandidateSource::Both
            && !bgm_playback_pending_play_latch_required(no_candidate)
            && !bgm_playback_pending_play_normal_publication_allowed(
                no_candidate)
            && bgm_playback_pending_play_candidate_source(no_candidate)
                == BgmPlaybackPendingPlayCandidateSource::None,
        "pending custom Play candidate/authority latch decision was not fail closed");
    auto repeated_native_play = successful_play_transaction;
    repeated_native_play.native_entry_count = 2;
    require(!bgm_playback_pending_play_ownership_durable(repeated_native_play),
        "repeated native Play was accepted as an exact transaction");
    auto late_latch = successful_play_transaction;
    late_latch.unresolved_latch_armed_before_native = false;
    require(!bgm_playback_pending_play_ownership_durable(late_latch),
        "post-native latch arming was accepted as safe ownership ordering");

    BgmPlaybackBoundarySetFacts boundary_set{
        true, true, true, true, true, true, true, true, true, true};
    require(bgm_playback_boundary_set_exact(boundary_set),
        "exact retained-custom Set did not consume the parent Stop boundary");
    bool BgmPlaybackBoundarySetFacts::* const boundary_set_guards[] = {
        &BgmPlaybackBoundarySetFacts::boundary_active,
        &BgmPlaybackBoundarySetFacts::boundary_unconsumed,
        &BgmPlaybackBoundarySetFacts::parent_version_exact,
        &BgmPlaybackBoundarySetFacts::retained_custom_pointer_exact,
        &BgmPlaybackBoundarySetFacts::retained_custom_identity_exact,
        &BgmPlaybackBoundarySetFacts::controller_chain_exact,
        &BgmPlaybackBoundarySetFacts::owner_thread_exact,
        &BgmPlaybackBoundarySetFacts::owner_command_exact,
        &BgmPlaybackBoundarySetFacts::lease_lifecycle_tokens_exact,
        &BgmPlaybackBoundarySetFacts::request_strictly_newer,
    };
    for (auto guard : boundary_set_guards) {
        auto rejected = boundary_set;
        rejected.*guard = false;
        require(!bgm_playback_boundary_set_exact(rejected),
            "boundary Set accepted command, pointer identity, version, epoch, or reuse drift");
    }
    require(!bgm_playback_transition_consumable(true, false, true, true, false)
            && !bgm_playback_transition_consumable(false, true, true, true, false)
            && !bgm_playback_transition_consumable(true, true, false, true, false)
            && !bgm_playback_transition_consumable(true, true, true, false, false)
            && !bgm_playback_transition_consumable(true, true, true, true, true)
            && bgm_playback_transition_consumable(true, true, true, true, false),
        "Set provenance became consumable before exact state-4 Play or was reused");
    require(bgm_playback_boundary_must_clear(true, false, false, false, false, false)
            && bgm_playback_boundary_must_clear(false, true, false, false, false, false)
            && bgm_playback_boundary_must_clear(false, false, true, false, false, false)
            && bgm_playback_boundary_must_clear(false, false, false, true, false, false)
            && bgm_playback_boundary_must_clear(false, false, false, false, true, false)
            && bgm_playback_boundary_must_clear(false, false, false, false, false, true)
            && !bgm_playback_boundary_must_clear(false, false, false, false, false, false),
        "boundary invalidation did not cover mismatch/fault/list/shutdown/version/transfer");
    BgmPlaybackSetCorrelationFacts set_correlation{
        true, true, true, true, true, true};
    require(bgm_playback_set_correlation_exact(set_correlation),
        "exact anchored Set correlation was rejected");
    bool BgmPlaybackSetCorrelationFacts::* const set_correlation_guards[] = {
        &BgmPlaybackSetCorrelationFacts::anchored,
        &BgmPlaybackSetCorrelationFacts::controller_exact,
        &BgmPlaybackSetCorrelationFacts::slot_bgm_exact,
        &BgmPlaybackSetCorrelationFacts::lease_exact,
        &BgmPlaybackSetCorrelationFacts::lifecycle_exact,
        &BgmPlaybackSetCorrelationFacts::unresolved,
    };
    for (auto guard : set_correlation_guards) {
        auto rejected = set_correlation;
        rejected.*guard = false;
        require(!bgm_playback_set_correlation_exact(rejected),
            "Set observation attached to an unrelated lineage");
    }
    BgmPlaybackLineageInheritanceFacts inheritance{
        true, true, true, true, true, true, true, true};
    require(bgm_playback_lineage_inheritance_exact(inheritance),
        "exact anchored lineage inheritance was rejected");
    bool BgmPlaybackLineageInheritanceFacts::* const inheritance_guards[] = {
        &BgmPlaybackLineageInheritanceFacts::parent_anchor_exact,
        &BgmPlaybackLineageInheritanceFacts::token_epoch_exact,
        &BgmPlaybackLineageInheritanceFacts::lifecycle_state_epoch_exact,
        &BgmPlaybackLineageInheritanceFacts::lease_exact,
        &BgmPlaybackLineageInheritanceFacts::controller_chain_exact,
        &BgmPlaybackLineageInheritanceFacts::handle_generation_exact,
        &BgmPlaybackLineageInheritanceFacts::sound_identity_exact,
        &BgmPlaybackLineageInheritanceFacts::alternating_transition_exact,
    };
    for (auto guard : inheritance_guards) {
        auto rejected = inheritance;
        rejected.*guard = false;
        require(!bgm_playback_lineage_inheritance_exact(rejected),
            "lineage inherited a retirement anchor through drift");
    }
    constexpr uint64_t custom_old = 0x0000000400010008ull;
    constexpr uint64_t canonical_intermediate = 0x0000000500000008ull;
    constexpr uint64_t next_custom = 0x0000000600020008ull;
    constexpr uint64_t child_canonical = 0x0000000700000008ull;
    require(bgm_playback_alternating_transition_exact(
            custom_old, canonical_intermediate, next_custom, true, true),
        "runtime-shaped custom/canonical/custom transition was rejected");
    require(!bgm_playback_alternating_transition_exact(
                custom_old, 0, next_custom, true, true)
            && !bgm_playback_alternating_transition_exact(
                custom_old, canonical_intermediate, canonical_intermediate,
                true, true)
            && !bgm_playback_alternating_transition_exact(
                custom_old, next_custom, canonical_intermediate, true, true)
            && !bgm_playback_alternating_transition_exact(
                custom_old, canonical_intermediate, next_custom, false, true)
            && !bgm_playback_alternating_transition_exact(
                custom_old, canonical_intermediate, next_custom, true, false),
        "alternating transition accepted missing identity or generation order");
    require(bgm_playback_set_commit_exact(true, true, true, 0, 1)
            && bgm_playback_play_join_exact(true, true, true, 1, 2)
            && bgm_playback_operation_nonce_next(2, 3)
            && bgm_playback_set_commit_exact(true, true, true, 3, 4)
            && bgm_playback_play_join_exact(true, true, true, 4, 5)
            && bgm_playback_child_claim_exact(
                false, 1, 2, next_custom, next_custom)
            && child_canonical > next_custom
            && bgm_playback_set_commit_exact(true, true, true, 5, 6)
            && bgm_playback_play_join_exact(true, true, true, 6, 7),
        "0x400 custom -> 0x500 canonical -> 0x600 custom -> child -> "
        "0x700 canonical operation sequence was rejected");
    require(bgm_playback_set_commit_exact(true, true, true, 8, 9)
            && bgm_playback_play_join_exact(true, true, true, 9, 10)
            && bgm_playback_operation_nonce_next(10, 11)
            && bgm_playback_set_commit_exact(true, true, true, 11, 12)
            && bgm_playback_play_join_exact(true, true, true, 12, 13)
            && bgm_playback_child_claim_exact(
                false, 1, 2, next_custom, next_custom)
            && bgm_playback_set_commit_exact(true, true, true, 13, 14)
            && bgm_playback_play_join_exact(true, true, true, 14, 15),
        "nonzero-baseline multi-cycle operation chain was rejected");
    require(classify_bgm_playback_set_provenance(true, true, true)
            == BgmPlaybackSetProvenance::Custom,
        "exact retained-sound Set must establish custom provenance");
    require(classify_bgm_playback_set_provenance(true, false, false)
            == BgmPlaybackSetProvenance::Canonical,
        "distinct-sound Set must establish canonical provenance");
    require(classify_bgm_playback_set_provenance(true, true, false)
            == BgmPlaybackSetProvenance::Canonical,
        "same pointer without exact identity must not establish custom provenance");
    require(classify_bgm_playback_set_provenance(false, true, true)
            == BgmPlaybackSetProvenance::None,
        "failed Set must not establish transition provenance");
    require(!bgm_playback_child_claim_exact(
                true, 1, 2, next_custom, next_custom)
            && !bgm_playback_child_claim_exact(
                false, 1, 3, next_custom, 0x0000000700010008ull),
        "one observed custom transition fed more than one exact child");
    require(!bgm_playback_set_commit_exact(false, true, true, 0, 1)
            && !bgm_playback_set_commit_exact(true, false, true, 0, 1)
            && !bgm_playback_set_commit_exact(true, true, false, 0, 1)
            && !bgm_playback_play_join_exact(false, true, true, 1, 2)
            && !bgm_playback_play_join_exact(true, false, true, 1, 2)
            && !bgm_playback_play_join_exact(true, true, false, 1, 2)
            && !bgm_playback_play_join_exact(true, true, true, 2, 2),
        "failed, stale, mismatched, or non-monotonic Set/Play correlation "
        "was accepted");
    bool observer_called = false;
    require(ff7r::piano::game::bgm_playback_observe_best_effort(
                true, [&observer_called]() {
                observer_called = true;
                throw std::runtime_error("observer failure");
            })
            && observer_called
            && !ff7r::piano::game::bgm_playback_observe_best_effort(
                false, []() {}),
        "best-effort observation changed the native Play result");

    BgmPlaybackSessionCorrelationFacts correlation{
        BgmPlaybackLineageProvenance::PreSession, true,
        0, 0, 0, 0, 0, 0};
    require(bgm_playback_session_correlation_exact(correlation),
        "pre-session provenance was not preserved before a session");
    correlation.current_session_epoch = 17;
    correlation.current_cycle_epoch = 19;
    require(bgm_playback_session_correlation_exact(correlation),
        "pre-session lineage could not correlate through exact continuity");
    correlation.correlated_session_epoch = 17;
    correlation.correlated_cycle_epoch = 19;
    require(bgm_playback_session_correlation_exact(correlation),
        "persisted session correlation was rejected");
    correlation.current_cycle_epoch = 23;
    require(!bgm_playback_session_correlation_exact(correlation),
        "session/cycle drift was accepted after correlation");
    correlation.current_cycle_epoch = 19;
    correlation.continuity_exact = false;
    require(!bgm_playback_session_correlation_exact(correlation),
        "pre-session lineage correlated without exact continuity");

    require(bgm_playback_old_handle_identity_conflict(
            0x400010008, 0x1000, 0x400010008, 0x2000)
            && !bgm_playback_old_handle_identity_conflict(
                0x400010008, 0x1000, 0x500010008, 0x2000),
        "wrong-sound exact-handle conflict was not fail-closed");
    require(bgm_playback_refresh_publish_current(7, 11, 7, 11, 13, 13)
            && !bgm_playback_refresh_publish_current(7, 11, 7, 12, 13, 13)
            && !bgm_playback_refresh_publish_current(7, 11, 7, 11, 13, 14),
        "concurrent Set/release version drift could overwrite refreshed facts");
    require(!bgm_playback_refresh_publish_current(7, 11, 8, 11, 13, 13)
            && bgm_playback_old_handle_identity_conflict(
                next_custom, 0x1000, next_custom, 0x2000),
        "stale ordinal or monotonic ABA identity conflict was accepted");
    require(bgm_playback_marker_batch_current(13, 13, 7, 11, 7, 11)
            && !bgm_playback_marker_batch_current(13, 14, 7, 11, 7, 11)
            && !bgm_playback_marker_batch_current(13, 13, 7, 11, 7, 12),
        "marker batch accepted collection or participant version drift");
    require(!bgm_playback_record_resolved(true, false, false, 0, false, true)
            && !bgm_playback_record_resolved(true, false, true, 9, false, true)
            && bgm_playback_record_resolved(true, false, true, 9, true, true)
            && !bgm_playback_record_resolved(true, false, true, 9, true, false)
            && !bgm_playback_record_resolved(true, true, false, 9, false, true),
        "active-capacity saturation could overwrite an unresolved lineage");

    BgmPlaybackAggregateFacts active{
        true, true, true, true, false, true, true,
        false, false, false, false, false, 2};
    require(classify_bgm_playback_aggregate(active)
            == BgmPlaybackAggregateMarker::FinalActiveBorrower,
        "immediate resumed borrower was not classified");
    auto delayed = active;
    delayed.final_active_ready = false;
    require(classify_bgm_playback_aggregate(delayed)
            == BgmPlaybackAggregateMarker::Invalid,
        "delayed readiness was inferred from timing");
    delayed.final_active_ready = true;
    require(classify_bgm_playback_aggregate(delayed)
            == BgmPlaybackAggregateMarker::FinalActiveBorrower,
        "delayed explicit readiness did not converge deterministically");

    auto release_pending = active;
    release_pending.release_authorized = true;
    require(classify_bgm_playback_aggregate(release_pending)
            == BgmPlaybackAggregateMarker::ReleaseBlockedBorrowers,
        "release authorization was not ordered after outstanding borrowers");
    auto invalid_commit = release_pending;
    invalid_commit.release_committed = true;
    require(classify_bgm_playback_aggregate(invalid_commit)
            == BgmPlaybackAggregateMarker::ReleaseCommittedBorrowers,
        "release commit ordering lost outstanding borrowers");

    auto list_pending = active;
    list_pending.list_exit = true;
    require(classify_bgm_playback_aggregate(list_pending)
            == BgmPlaybackAggregateMarker::ListExitPending,
        "pending list return lost the current borrower");
    auto list_closed = list_pending;
    list_closed.borrower_count = 0;
    list_closed.all_retired_absent = true;
    list_closed.final_active_present = false;
    list_closed.final_active_ready = false;
    require(classify_bgm_playback_aggregate(list_closed)
            == BgmPlaybackAggregateMarker::ListExitClosed,
        "list return did not require exact aggregate closure");
    auto simultaneous = list_pending;
    simultaneous.borrower_count = 2;
    require(classify_bgm_playback_aggregate(simultaneous)
            == BgmPlaybackAggregateMarker::ListExitPending,
        "simultaneous borrowers were prematurely closed");
    simultaneous.borrower_count = 0;
    simultaneous.all_retired_absent = true;
    simultaneous.final_active_present = false;
    simultaneous.final_active_ready = false;
    require(classify_bgm_playback_aggregate(simultaneous)
            == BgmPlaybackAggregateMarker::ListExitClosed,
        "simultaneous borrowers did not close as one refreshed aggregate");

    auto shutdown_pending = active;
    shutdown_pending.shutdown = true;
    require(classify_bgm_playback_aggregate(shutdown_pending)
            == BgmPlaybackAggregateMarker::ShutdownRetained,
        "shutdown did not retain a current borrower");
    auto shutdown_closed = list_closed;
    shutdown_closed.list_exit = false;
    shutdown_closed.shutdown = true;
    require(classify_bgm_playback_aggregate(shutdown_closed)
            == BgmPlaybackAggregateMarker::ShutdownClosed,
        "shutdown did not distinguish exact closure");
    auto presession_list = list_pending;
    presession_list.epochs_exact = true;
    auto presession_shutdown = shutdown_pending;
    presession_shutdown.epochs_exact = true;
    require(classify_bgm_playback_aggregate(presession_list)
                == BgmPlaybackAggregateMarker::ListExitPending
            && classify_bgm_playback_aggregate(presession_shutdown)
                == BgmPlaybackAggregateMarker::ShutdownRetained,
        "pre-session list/shutdown provenance was not observable");

    bool BgmPlaybackAggregateFacts::* const exact_members[] = {
        &BgmPlaybackAggregateFacts::identity_exact,
        &BgmPlaybackAggregateFacts::epochs_exact,
        &BgmPlaybackAggregateFacts::handles_exact,
        &BgmPlaybackAggregateFacts::destination_aba_safe,
    };
    for (auto member : exact_members) {
        auto rejected = active;
        rejected.*member = false;
        require(classify_bgm_playback_aggregate(rejected)
                == BgmPlaybackAggregateMarker::Invalid,
            "aggregate observer accepted missing identity/epoch/ABA proof");
    }
    auto aba = active;
    aba.destination_aba_safe = false;
    require(classify_bgm_playback_aggregate(aba)
            == BgmPlaybackAggregateMarker::Invalid,
        "destination reuse/ABA was accepted");
    require(!bgm_playback_aggregate_authorizes_mutation(active)
            && !bgm_playback_aggregate_authorizes_mutation(list_closed)
            && !bgm_playback_aggregate_authorizes_mutation(shutdown_closed),
        "observation-only aggregate authorized mutation");
    BgmPlaybackAggregateMutationFacts mutation{
        true, true, true, true, true, true, true, true, true, true, true};
    require(bgm_playback_aggregate_mutation_exact(mutation),
        "exact three-cycle aggregate mutation authority was rejected");
    auto rejected_mutation = mutation;
    rejected_mutation.operation_nonce_exact = false;
    require(!bgm_playback_aggregate_mutation_exact(rejected_mutation),
        "skipped aggregate operation nonce authorized mutation");
    rejected_mutation = mutation;
    rejected_mutation.lifecycle_exact = false;
    require(!bgm_playback_aggregate_mutation_exact(rejected_mutation),
        "failed lifecycle authorized aggregate mutation");
    rejected_mutation = mutation;
    rejected_mutation.identity_exact = false;
    require(!bgm_playback_aggregate_mutation_exact(rejected_mutation),
        "ABA/capacity identity failure authorized aggregate mutation");
    BgmPlaybackAggregateCanonicalExitClearFacts aggregate_exit_clear{
        true, true, true, true, true, true, true, true, true, true,
        true, true, true, true, true, true, true, true, true, true,
        true, true, true, true, true, true, true};
    require(bgm_playback_aggregate_canonical_exit_clear_exact(
                aggregate_exit_clear),
        "exact aggregate canonical list-exit clear authority was rejected");
    constexpr std::array aggregate_exit_clear_members{
        &BgmPlaybackAggregateCanonicalExitClearFacts::exit_publication_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::mutation_authority_drained,
        &BgmPlaybackAggregateCanonicalExitClearFacts::latest_record_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::record_version_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::collection_version_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::list_exit_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::current_active_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::record_not_failed,
        &BgmPlaybackAggregateCanonicalExitClearFacts::destination_aba_safe,
        &BgmPlaybackAggregateCanonicalExitClearFacts::identity_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::continuity_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::controller_proof_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::slot_bgm_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::canonical_sound_pointer_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::canonical_sound_identity_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::new_handle_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::canonical_handle_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::state4_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::no_pending_set,
        &BgmPlaybackAggregateCanonicalExitClearFacts::no_pending_boundary,
        &BgmPlaybackAggregateCanonicalExitClearFacts::no_unresolved_request,
        &BgmPlaybackAggregateCanonicalExitClearFacts::no_custom_publication,
        &BgmPlaybackAggregateCanonicalExitClearFacts::route_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::lease_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::lifecycle_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::tokens_exact,
        &BgmPlaybackAggregateCanonicalExitClearFacts::retained_custom_route_exact,
    };
    for (const auto member : aggregate_exit_clear_members) {
        auto rejected = aggregate_exit_clear;
        rejected.*member = false;
        require(!bgm_playback_aggregate_canonical_exit_clear_exact(rejected),
            "inexact aggregate canonical list-exit facts authorized native clear");
    }
    require(!bgm_playback_list_return_clear_authorized(false, false, false)
            && bgm_playback_list_return_clear_authorized(true, false, false)
            && bgm_playback_list_return_clear_authorized(false, true, false)
            && bgm_playback_list_return_clear_authorized(true, true, false)
            && bgm_playback_list_return_clear_authorized(false, false, true),
        "list-return clear authority rejected one source or required two clears");
    {
        // The captured failing session, in the terms this predicate reads: the
        // native stopped the mod's sound and immediately re-Set the canonical
        // BGM, so live_sound_exact=0, live_request_exact=0 and
        // live_sound_identity_exact=0, while live_controller_exact=1,
        // live_slot_exact=1 and live_bgm_exact=1.  The aggregate borrower
        // ordinal was 0 and the route's native clear was never verified.  The
        // lineage was detached (distinct canonical/custom tokens).  None of
        // that bears on whether the mod's own mutation was reverted, so this
        // session must retire.
        BgmPlaybackRouteRestoreRetirementFacts retirement{};
        retirement.lineage_present = true;
        retirement.lineage_sound_exact = true;
        retirement.lineage_route_exact = true;
        retirement.lineage_owner_restore_verified = true;
        retirement.lifecycle_failure_clear = true;
        retirement.owner_patch_restored = true;
        retirement.retired_sound_identity_exact = true;
        retirement.route_cleanup_pending = true;
        retirement.controller_exact = true;
        retirement.slot_bgm_exact = true;
        retirement.live_chain_exact = true;
        retirement.no_custom_publication_pending = true;
        retirement.deferred_handoff_forwarded = true;
        retirement.custom_request_retired = true;
        retirement.patch_journals_restored = true;
        const bool captured_session_proven =
            bgm_playback_route_restore_retirement_proven(retirement);
        require(captured_session_proven,
            "complete route-restore retirement evidence was not accepted");
        // Both older authorities are false in that session -- the mod no longer
        // owns live playback and no borrower record was ever captured for a
        // mod-initiated publication -- so the retirement has to be reachable
        // from this authority alone.  Otherwise list_cleanup_pending stays
        // latched and every later activation is denied.
        require(bgm_playback_list_return_clear_authorized(
                    false, false, captured_session_proven)
                && bgm_playback_route_restore_relinquishment_path(
                    false, false, captured_session_proven),
            "captured stop/re-Set session did not reach the retirement branch");
        // Structural proof that the removal is a removal.  The predicate reads
        // exactly the fifteen facts enumerated below and requires every one of
        // them, so no live-sound or live-request identity fact, and no
        // quiescence, poll-count or elapsed-time fact, can be gating this path.
        // Re-adding one changes the size and fails here.
        static_assert(sizeof(BgmPlaybackRouteRestoreRetirementFacts)
                == 15 * sizeof(bool),
            "route-restore retirement gained or lost a fact; its input surface "
            "must stay exactly the fifteen members enumerated below");
        constexpr bool BgmPlaybackRouteRestoreRetirementFacts::*kRetirementMembers[] = {
            &BgmPlaybackRouteRestoreRetirementFacts::lineage_present,
            &BgmPlaybackRouteRestoreRetirementFacts::lineage_sound_exact,
            &BgmPlaybackRouteRestoreRetirementFacts::lineage_route_exact,
            &BgmPlaybackRouteRestoreRetirementFacts::lineage_owner_restore_verified,
            &BgmPlaybackRouteRestoreRetirementFacts::lifecycle_failure_clear,
            &BgmPlaybackRouteRestoreRetirementFacts::owner_patch_restored,
            &BgmPlaybackRouteRestoreRetirementFacts::retired_sound_identity_exact,
            &BgmPlaybackRouteRestoreRetirementFacts::route_cleanup_pending,
            &BgmPlaybackRouteRestoreRetirementFacts::controller_exact,
            &BgmPlaybackRouteRestoreRetirementFacts::slot_bgm_exact,
            &BgmPlaybackRouteRestoreRetirementFacts::live_chain_exact,
            &BgmPlaybackRouteRestoreRetirementFacts::no_custom_publication_pending,
            &BgmPlaybackRouteRestoreRetirementFacts::deferred_handoff_forwarded,
            &BgmPlaybackRouteRestoreRetirementFacts::custom_request_retired,
            &BgmPlaybackRouteRestoreRetirementFacts::patch_journals_restored,
        };
        static_assert(sizeof(kRetirementMembers) / sizeof(kRetirementMembers[0])
                == 15,
            "route-restore retirement necessity loop must cover every fact");
        for (const auto member : kRetirementMembers) {
            BgmPlaybackRouteRestoreRetirementFacts rejected = retirement;
            rejected.*member = false;
            require(!bgm_playback_route_restore_retirement_proven(rejected),
                "incomplete route-restore evidence authorized cleanup");
        }
        // A route whose mutation is not provably reverted must never retire:
        // that is the one thing this authority exists to prove.
        BgmPlaybackRouteRestoreRetirementFacts unreverted = retirement;
        unreverted.owner_patch_restored = false;
        require(!bgm_playback_route_restore_retirement_proven(unreverted),
            "retirement accepted a sound whose owner patch was still applied");
        // The genuine "the mod may still own live playback" shape: the native
        // has not moved the mod's request handle into the slot's retired
        // vector.  This must not retire, and must not release.
        BgmPlaybackRouteRestoreRetirementFacts request_outstanding = retirement;
        request_outstanding.custom_request_retired = false;
        require(!bgm_playback_route_restore_retirement_proven(request_outstanding)
                && !bgm_playback_route_restore_relinquishment_path(false, false,
                    bgm_playback_route_restore_retirement_proven(
                        request_outstanding)),
            "retirement accepted a request handle the native had not retired");
        // Ambiguous or absent lineage must not retire: whether the mod owes a
        // bank release is decided by exactly one arm-time record, never by
        // ambient live state.
        BgmPlaybackRouteRestoreRetirementFacts no_lineage = retirement;
        no_lineage.lineage_present = false;
        require(!bgm_playback_route_restore_retirement_proven(no_lineage),
            "retirement accepted a route with no single arm-time lineage");
        // The branch runs only when it is the sole authority, so the native
        // slot clear stays unreachable from it.
        require(bgm_playback_route_restore_relinquishment_path(false, false, true)
                && !bgm_playback_route_restore_relinquishment_path(true, false, true)
                && !bgm_playback_route_restore_relinquishment_path(false, true, true)
                && !bgm_playback_route_restore_relinquishment_path(false, false, false),
            "route-restore relinquishment path did not stay mutually exclusive");
    }
    BgmPlaybackCanonicalSubstrateRelinquishmentFacts relinquishment{
        true, true, true, true};
    require(bgm_playback_canonical_substrate_relinquishment_exact(
                relinquishment),
        "exact aggregate canonical substrate relinquishment was rejected");
    constexpr std::array relinquishment_members{
        &BgmPlaybackCanonicalSubstrateRelinquishmentFacts::legacy_route_unowned,
        &BgmPlaybackCanonicalSubstrateRelinquishmentFacts::aggregate_authority_exact,
        &BgmPlaybackCanonicalSubstrateRelinquishmentFacts::pre_publication_revalidated,
        &BgmPlaybackCanonicalSubstrateRelinquishmentFacts::version_epoch_exact,
    };
    for (const auto member : relinquishment_members) {
        auto rejected = relinquishment;
        rejected.*member = false;
        require(!bgm_playback_canonical_substrate_relinquishment_exact(rejected),
            "inexact aggregate canonical substrate facts authorized relinquishment");
    }
    BgmPlaybackCanonicalSubstratePhaseFacts substrate_phase{
        true, true, true, true, true, true, true, true, true, true, true};
    require(bgm_playback_canonical_substrate_phase_exact(substrate_phase),
        "exact canonical substrate phase facts were rejected");
    constexpr std::array substrate_phase_members{
        &BgmPlaybackCanonicalSubstratePhaseFacts::generation_nonzero,
        &BgmPlaybackCanonicalSubstratePhaseFacts::expected_state,
        &BgmPlaybackCanonicalSubstratePhaseFacts::ordinal_exact,
        &BgmPlaybackCanonicalSubstratePhaseFacts::record_version_exact,
        &BgmPlaybackCanonicalSubstratePhaseFacts::collection_version_exact,
        &BgmPlaybackCanonicalSubstratePhaseFacts::list_exit_epoch_exact,
        &BgmPlaybackCanonicalSubstratePhaseFacts::route_generation_exact,
        &BgmPlaybackCanonicalSubstratePhaseFacts::lifecycle_epoch_exact,
        &BgmPlaybackCanonicalSubstratePhaseFacts::controller_exact,
        &BgmPlaybackCanonicalSubstratePhaseFacts::request_sound_exact,
        &BgmPlaybackCanonicalSubstratePhaseFacts::tokens_exact,
    };
    for (const auto member : substrate_phase_members) {
        auto rejected = substrate_phase;
        rejected.*member = false;
        require(!bgm_playback_canonical_substrate_phase_exact(rejected),
            "inexact canonical substrate phase facts advanced transaction state");
    }
    require(bgm_canonical_substrate_bridge_source_version_monotonic(70, 70)
            && bgm_canonical_substrate_bridge_source_version_monotonic(70, 71)
            && bgm_canonical_substrate_bridge_source_version_monotonic(70, 95)
            && !bgm_canonical_substrate_bridge_source_version_monotonic(70, 69)
            && !bgm_canonical_substrate_bridge_source_version_monotonic(0, 95),
        "bridge enrichment required a fixed version increment or accepted regression");
    require(bgm_canonical_substrate_bridge_release_epoch_exact(10, 11, 1)
            && !bgm_canonical_substrate_bridge_release_epoch_exact(0, 11, 1)
            && !bgm_canonical_substrate_bridge_release_epoch_exact(10, 10, 1)
            && !bgm_canonical_substrate_bridge_release_epoch_exact(10, 11, 0),
        "bridge enrichment accepted incomplete or stale release completion");
    BgmCanonicalSubstrateBridgeEnrichmentFacts bridge_enrichment;
    bridge_enrichment.source_found = true;
    bridge_enrichment.proof_active = true;
    bridge_enrichment.proof_state_exact = true;
    bridge_enrichment.proof_transaction_nonzero = true;
    bridge_enrichment.generation_available = true;
    bridge_enrichment.bridge_uninitialized = true;
    bridge_enrichment.source_active = true;
    bridge_enrichment.source_ordinal_exact = true;
    bridge_enrichment.source_version_monotonic = true;
    bridge_enrichment.collection_version_monotonic = true;
    bridge_enrichment.source_state_exact = true;
    bridge_enrichment.transaction_exact = true;
    bridge_enrichment.list_exit_epoch_exact = true;
    bridge_enrichment.controller_exact = true;
    bridge_enrichment.slot_bgm_exact = true;
    bridge_enrichment.canonical_sound_exact = true;
    bridge_enrichment.canonical_request_exact = true;
    bridge_enrichment.route_generation_exact = true;
    bridge_enrichment.lease_exact = true;
    bridge_enrichment.lifecycle_epoch_exact = true;
    bridge_enrichment.tokens_exact = true;
    bridge_enrichment.callback_identity_exact = true;
    bridge_enrichment.callback_distinct_from_canonical = true;
    bridge_enrichment.release_epoch_exact = true;
    bridge_enrichment.lifecycle_complete_custom_absent = true;
    bridge_enrichment.lifecycle_anchor_exact = true;
    bridge_enrichment.substrate_current_exact = true;
    bridge_enrichment.record_healthy = true;
    bridge_enrichment.no_pending_provenance = true;
    bridge_enrichment.no_child_provenance = true;
    require(bgm_canonical_substrate_bridge_enrichment_exact(bridge_enrichment)
            && first_bgm_canonical_substrate_bridge_enrichment_failure(
                bridge_enrichment)
                == BgmCanonicalSubstrateBridgeEnrichmentFailure::None,
        "exact canonical substrate bridge lineage was rejected");
    constexpr std::array bridge_enrichment_members{
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::source_found,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::proof_active,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::proof_state_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::proof_transaction_nonzero,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::generation_available,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::bridge_uninitialized,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::source_active,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::source_ordinal_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::source_version_monotonic,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::collection_version_monotonic,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::source_state_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::transaction_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::list_exit_epoch_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::controller_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::slot_bgm_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::canonical_sound_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::canonical_request_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::route_generation_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::lease_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::lifecycle_epoch_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::tokens_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::callback_identity_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::callback_distinct_from_canonical,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::release_epoch_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::lifecycle_complete_custom_absent,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::lifecycle_anchor_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::substrate_current_exact,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::record_healthy,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::no_pending_provenance,
        &BgmCanonicalSubstrateBridgeEnrichmentFacts::no_child_provenance,
    };
    constexpr std::array bridge_enrichment_failures{
        BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceMissing,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::ProofInactive,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::ProofState,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::ProofTransaction,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::GenerationExhausted,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::BridgeAlreadyInitialized,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceInactive,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceOrdinal,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceVersionRegression,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::CollectionVersionRegression,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::SourceState,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::Transaction,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::ListExitEpoch,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::Controller,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::SlotBgm,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::CanonicalSound,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::CanonicalRequest,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::RouteGeneration,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::Lease,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::LifecycleEpoch,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::Tokens,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::CallbackIdentity,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::CallbackAliasesCanonical,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::ReleaseEpoch,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::LifecycleNotCompleteAbsent,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::LifecycleAnchor,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::SubstrateNotCurrent,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::RecordUnhealthy,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::PendingProvenance,
        BgmCanonicalSubstrateBridgeEnrichmentFailure::ChildProvenance,
    };
    static_assert(bridge_enrichment_members.size()
        == bridge_enrichment_failures.size());
    for (size_t i = 0; i < bridge_enrichment_members.size(); ++i) {
        auto rejected = bridge_enrichment;
        rejected.*bridge_enrichment_members[i] = false;
        require(first_bgm_canonical_substrate_bridge_enrichment_failure(rejected)
                == bridge_enrichment_failures[i],
            "bridge enrichment did not reject its exact first lineage failure");
    }
    require(!bgm_playback_canonical_substrate_partial_blocks_release(
                BgmPlaybackCanonicalSubstrateRelinquishmentState::None)
            && bgm_playback_canonical_substrate_partial_blocks_release(
                BgmPlaybackCanonicalSubstrateRelinquishmentState::Pending)
            && bgm_playback_canonical_substrate_partial_blocks_release(
                BgmPlaybackCanonicalSubstrateRelinquishmentState::RouteCommitted)
            && !bgm_playback_canonical_substrate_partial_blocks_release(
                BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished)
            && bgm_playback_canonical_substrate_partial_blocks_release(
                BgmPlaybackCanonicalSubstrateRelinquishmentState::Failed),
        "partial canonical substrate transaction did not remain release-blocking");
    require(!bgm_playback_list_return_native_clear_required(false, true)
            && bgm_playback_list_return_native_clear_required(true, false)
            && bgm_playback_list_return_native_clear_required(true, true),
        "aggregate-only relinquishment called native clear or legacy ownership skipped it");
    BgmPlaybackCanonicalSubstrateClosureFacts substrate_closure{
        true, true, true, true, true, true, true, true, true};
    require(bgm_playback_canonical_substrate_closure_exact(substrate_closure),
        "exact retained canonical substrate did not close custom borrower ownership");
    constexpr std::array substrate_closure_members{
        &BgmPlaybackCanonicalSubstrateClosureFacts::relinquishment_committed,
        &BgmPlaybackCanonicalSubstrateClosureFacts::handle_exact,
        &BgmPlaybackCanonicalSubstrateClosureFacts::sound_identity_exact,
        &BgmPlaybackCanonicalSubstrateClosureFacts::controller_route_exact,
        &BgmPlaybackCanonicalSubstrateClosureFacts::old_request_absent,
        &BgmPlaybackCanonicalSubstrateClosureFacts::custom_request_absent,
        &BgmPlaybackCanonicalSubstrateClosureFacts::no_pending_or_unresolved,
        &BgmPlaybackCanonicalSubstrateClosureFacts::record_healthy,
        &BgmPlaybackCanonicalSubstrateClosureFacts::substrate_current_or_absent,
    };
    for (const auto member : substrate_closure_members) {
        auto rejected = substrate_closure;
        rejected.*member = false;
        require(!bgm_playback_canonical_substrate_closure_exact(rejected),
            "retained canonical substrate bypassed custom borrower closure proof");
    }
    require(bgm_playback_canonical_substrate_active_sound_exact(
                true, true, true, true, true, true, true)
            && !bgm_playback_canonical_substrate_active_sound_exact(
                false, true, true, true, true, true, true)
            && !bgm_playback_canonical_substrate_active_sound_exact(
                true, false, true, true, true, true, true)
            && !bgm_playback_canonical_substrate_active_sound_exact(
                true, true, false, true, true, true, true)
            && !bgm_playback_canonical_substrate_active_sound_exact(
                true, true, true, false, true, true, true)
            && !bgm_playback_canonical_substrate_active_sound_exact(
                true, true, true, true, true, true, false)
            && !bgm_playback_canonical_substrate_active_sound_exact(
                true, true, true, true, true, false, true)
            && !bgm_playback_canonical_substrate_active_sound_exact(
                true, true, true, true, false, true, true),
        "retained canonical substrate accepted IdleNull, mixed, or identity-drifted state");
    BgmPlaybackListReturnPostClearFacts post_clear{
        true, true, true, true, true, true};
    require(bgm_playback_list_return_post_clear_exact(post_clear),
        "exact list-return native clear postcondition was rejected");
    constexpr std::array post_clear_members{
        &BgmPlaybackListReturnPostClearFacts::native_clear_called_once,
        &BgmPlaybackListReturnPostClearFacts::controller_chain_exact,
        &BgmPlaybackListReturnPostClearFacts::cleared_sound_null,
        &BgmPlaybackListReturnPostClearFacts::cleared_request_zero,
        &BgmPlaybackListReturnPostClearFacts::cleared_state_zero,
        &BgmPlaybackListReturnPostClearFacts::route_state_commit_exact,
    };
    for (const auto member : post_clear_members) {
        auto rejected = post_clear;
        rejected.*member = false;
        require(!bgm_playback_list_return_post_clear_exact(rejected),
            "failed list-return clear postcondition published request absence");
    }
    require(bgm_playback_list_return_synchronous_absence_allowed(
                true, true, true)
            && !bgm_playback_list_return_synchronous_absence_allowed(
                false, true, true)
            && !bgm_playback_list_return_synchronous_absence_allowed(
                true, false, true)
            && !bgm_playback_list_return_synchronous_absence_allowed(
                true, true, false),
        "synchronous aggregate absence ignored authority/version/post-clear proof");
    constexpr uint64_t stale_route_request = 0x0000000400010008ULL;
    constexpr uint64_t aggregate_canonical_request = 0x0000000B00000008ULL;
    require(stale_route_request != aggregate_canonical_request
            && bgm_playback_list_return_clear_authorized(false, true, false),
        "stale custom route tuple did not defer to exact aggregate authority");
    require(!bgm_playback_aggregate_release_exact(
                false, false, true, true, true, true)
            && !bgm_playback_aggregate_release_exact(
                true, false, true, true, true, true)
            && bgm_playback_aggregate_release_exact(
                true, true, true, true, true, true),
        "aggregate bank release ordering accepted an active borrower");
    require(bgm_playback_aggregate_native_callbacks_exact(
                6, 6, 6, 6, 3, 3, false, false)
            && !bgm_playback_aggregate_native_callbacks_exact(
                6, 7, 6, 6, 3, 3, false, false)
            && !bgm_playback_aggregate_native_callbacks_exact(
                6, 6, 6, 6, 3, 3, true, false)
            && !bgm_playback_aggregate_native_callbacks_exact(
                6, 6, 6, 6, 3, 3, false, true),
        "aggregate coordinator violated exact-once native/chart safety");
    require(bgm_playback_aggregate_exit_blocks_mutation(
                true, true, false, false)
            && !bgm_playback_aggregate_exit_blocks_mutation(
                true, true, true, false)
            && !bgm_playback_aggregate_exit_blocks_mutation(
                true, true, false, true),
        "exit-pending aggregate allowed old owner patch or new admission");
    BgmPlaybackAggregateExitClosureFacts exit_closure{
        true, true, true, true, true, true, true, true, true, true};
    require(bgm_playback_aggregate_exit_closure_exact(exit_closure),
        "exact multi-borrower aggregate exit closure was rejected");
    auto blocked_exit_closure = exit_closure;
    blocked_exit_closure.no_pending_set_boundary_or_child = false;
    require(!bgm_playback_aggregate_exit_closure_exact(blocked_exit_closure),
        "aggregate release ignored a pending Set/boundary/child");
    blocked_exit_closure = exit_closure;
    blocked_exit_closure.identity_and_aba_exact = false;
    require(!bgm_playback_aggregate_exit_closure_exact(blocked_exit_closure),
        "aggregate release ignored identity/ABA uncertainty");
    blocked_exit_closure = exit_closure;
    blocked_exit_closure.single_custom_release_claimant = false;
    require(!bgm_playback_aggregate_exit_closure_exact(blocked_exit_closure),
        "aggregate release allowed duplicate custom-token claimants");
    blocked_exit_closure = exit_closure;
    blocked_exit_closure.no_unresolved_native_request = false;
    require(!bgm_playback_aggregate_exit_closure_exact(blocked_exit_closure),
        "aggregate release ignored unresolved native Set request");
    blocked_exit_closure = exit_closure;
    blocked_exit_closure.no_observer_lease_in_flight = false;
    require(!bgm_playback_aggregate_exit_closure_exact(blocked_exit_closure),
        "aggregate release ignored in-flight result publication lease");
    require(!bgm_playback_new_song_arm_allowed(
                true, false, 0x1111, 0x2222)
            && bgm_playback_new_song_arm_allowed(
                false, true, 0x1111, 0x2222)
            && bgm_playback_new_song_arm_allowed(
                false, true, 0x1111, 0x1111),
        "new-song isolation did not reject early input or fresh re-entry");
    require(bgm_playback_no_gain_mutation(0)
            && !bgm_playback_no_gain_mutation(1),
        "aggregate candidate retained a canonical gain mutation");
    require(bgm_playback_exit_request_closed(
                false, true, 0x0000000900000008ULL, true),
        "historically ready request did not close after exact absence");
    require(!bgm_playback_exit_request_closed(
                true, true, 0x0000000900000008ULL, false)
            && !bgm_playback_exit_request_closed(
                false, true, 0x0000000900000008ULL, false),
        "active/present exit request was incorrectly closed");
    const BgmPlaybackLineageTerminalFacts superseded_closed{
        false, true, true, true, true, false, false, false, false};
    require(classify_bgm_playback_lineage_terminal(superseded_closed)
            == BgmPlaybackLineageTerminalState::SupersededClosed,
        "exact inherited parent did not become superseded-closed");
    auto superseded_pending = superseded_closed;
    superseded_pending.canonical_absent = false;
    require(classify_bgm_playback_lineage_terminal(superseded_pending)
            == BgmPlaybackLineageTerminalState::SupersededPending,
        "present canonical intermediate did not retain parent ownership");
    superseded_pending = superseded_closed;
    superseded_pending.custom_absent = false;
    require(classify_bgm_playback_lineage_terminal(superseded_pending)
            == BgmPlaybackLineageTerminalState::SupersededPending,
        "present custom child request did not retain parent ownership");
    auto truly_failed_parent = superseded_closed;
    truly_failed_parent.failed = true;
    require(classify_bgm_playback_lineage_terminal(truly_failed_parent)
            == BgmPlaybackLineageTerminalState::Failed,
        "true failed ancestor was incorrectly superseded");
    require(bgm_playback_expected_custom_successor(
                0x0000000600020008ULL, 0x0000000500000008ULL,
                0x0000000600020008ULL, true)
            && !bgm_playback_expected_custom_successor(
                0x0000000700000008ULL, 0x0000000500000008ULL,
                0x0000000600020008ULL, true),
        "canonical-to-custom successor classification was permissive");
    require(!bgm_playback_lineage_current_reconciliation_allowed(true, 2)
            && bgm_playback_lineage_current_reconciliation_allowed(false, 0)
            && bgm_playback_lineage_current_reconciliation_allowed(false, 2)
            && bgm_playback_lineage_current_reconciliation_allowed(true, 0),
        "superseded-parent current reconciliation did not require an exact consumed child claim");
    constexpr uint64_t runtime_canonical_command = 0x4ae;
    constexpr uint64_t runtime_anchored_custom_command = 0x4af;
    constexpr uint64_t runtime_unrelated_command = 0x7ff;
    require(bgm_playback_owner_command_phase_exact(
                true, true, 0, true, runtime_canonical_command,
                runtime_canonical_command)
            && !bgm_playback_owner_command_phase_exact(
                true, true, 0, true, runtime_canonical_command,
                runtime_anchored_custom_command),
        "canonical refresh command phase accepted a non-canonical key");
    require(bgm_playback_confirmed_custom_owner_command_exact(
                true, true, 0, true, runtime_canonical_command,
                runtime_anchored_custom_command,
                runtime_anchored_custom_command)
            && !bgm_playback_confirmed_custom_owner_command_exact(
                true, true, 0, true, runtime_canonical_command,
                runtime_anchored_custom_command, runtime_canonical_command)
            && !bgm_playback_confirmed_custom_owner_command_exact(
                true, true, 0, true, runtime_canonical_command,
                runtime_anchored_custom_command, runtime_unrelated_command)
            && !bgm_playback_confirmed_custom_owner_command_exact(
                true, true, 0, true, runtime_canonical_command,
                runtime_canonical_command, runtime_canonical_command),
        "confirmed-custom command phase was not exact and distinct");
    require(bgm_playback_confirmed_custom_route_transition_exact(2, 3, 4, 4, 5)
            && !bgm_playback_confirmed_custom_route_transition_exact(3, 3, 4, 4, 5)
            && !bgm_playback_confirmed_custom_route_transition_exact(2, 0, 4, 4, 5)
            && !bgm_playback_confirmed_custom_route_transition_exact(2, 4, 5, 5, 6)
            && !bgm_playback_confirmed_custom_route_transition_exact(2, 3, 4, 5, 6)
            && !bgm_playback_confirmed_custom_route_transition_exact(2, 3, 4, 4, 6)
            && !bgm_playback_confirmed_custom_route_transition_exact(3, 2, 4, 4, 5)
            && !bgm_playback_confirmed_custom_route_transition_exact(2, 3, 3, 3, 4)
            && !bgm_playback_confirmed_custom_route_transition_exact(2, 3, 4, 4, 4)
            && !bgm_playback_confirmed_custom_route_transition_exact(
                UINT64_MAX - 1, UINT64_MAX, 0, 0, 1),
        "confirmed-custom route lineage accepted the transfer snapshot as the retirement predecessor or accepted missing, skipped, reversed, boundary-successor, ABA, or wrapping generations");
    require(bgm_playback_descendant_route_predecessor(4, 5, 5) == 4
            && bgm_playback_descendant_route_predecessor(4, 6, 6) == 0
            && bgm_playback_descendant_route_predecessor(4, 5, 4) == 0
            && bgm_playback_descendant_route_predecessor(4, 5, 6) == 0
            && bgm_playback_descendant_route_predecessor(0, 1, 1) == 0
            && bgm_playback_descendant_route_predecessor(5, 5, 5) == 0
            && bgm_playback_descendant_route_predecessor(5, 4, 4) == 0
            && bgm_playback_descendant_route_predecessor(
                UINT64_MAX, 0, 0) == 0,
        "descendant route rebasing accepted missing, skipped, stale, mismatched, reversed, equal, ABA-like, or wrapping routes");
    require(bgm_playback_confirmed_custom_lease_transition_exact(
                AudioRouteLeaseIdentity{1, 0x1234},
                AudioRouteLeaseIdentity{1, 0x1234},
                AudioRouteLeaseIdentity{1, 0x1234})
            && !bgm_playback_confirmed_custom_lease_transition_exact(
                AudioRouteLeaseIdentity{1, 0x1234},
                AudioRouteLeaseIdentity{2, 0x1234},
                AudioRouteLeaseIdentity{2, 0x1234})
            && !bgm_playback_confirmed_custom_lease_transition_exact(
                AudioRouteLeaseIdentity{1, 0x1234},
                AudioRouteLeaseIdentity{1, 0x1234},
                AudioRouteLeaseIdentity{1, 0x1235}),
        "confirmed-custom lease lineage accepted stale or wrong song identity");
    BgmPlaybackConfirmedCustomRefreshFacts confirmed_custom_refresh{
        true, true, true, true, true, true, true,
        true, true, true, true, true, true, true, true};
    require(bgm_playback_confirmed_custom_refresh_exact(
                confirmed_custom_refresh),
        "exact Play-confirmed custom refresh was rejected");
    struct ConfirmedCustomRefreshGuard final {
        bool BgmPlaybackConfirmedCustomRefreshFacts::*member;
        BgmPlaybackConfirmedCustomRefreshFailure failure;
    };
    const ConfirmedCustomRefreshGuard confirmed_custom_refresh_guards[] = {
        {&BgmPlaybackConfirmedCustomRefreshFacts::prior_play_confirmed,
            BgmPlaybackConfirmedCustomRefreshFailure::PlayUnconfirmed},
        {&BgmPlaybackConfirmedCustomRefreshFacts::provenance_resolved,
            BgmPlaybackConfirmedCustomRefreshFailure::PendingOrUnresolvedProvenance},
        {&BgmPlaybackConfirmedCustomRefreshFacts::handle_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::HandleMismatch},
        {&BgmPlaybackConfirmedCustomRefreshFacts::sound_live,
            BgmPlaybackConfirmedCustomRefreshFailure::SoundNotLive},
        {&BgmPlaybackConfirmedCustomRefreshFacts::sound_pointer_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::SoundPointerMismatch},
        {&BgmPlaybackConfirmedCustomRefreshFacts::sound_identity_positive,
            BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentityNotPositive},
        {&BgmPlaybackConfirmedCustomRefreshFacts::sound_identity_index_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentityIndexMismatch},
        {&BgmPlaybackConfirmedCustomRefreshFacts::sound_identity_serial_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentitySerialMismatch},
        {&BgmPlaybackConfirmedCustomRefreshFacts::state4,
            BgmPlaybackConfirmedCustomRefreshFailure::StateNotPlaying},
        {&BgmPlaybackConfirmedCustomRefreshFacts::controller_slot_bgm_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::ControllerSlotBgmMismatch},
        {&BgmPlaybackConfirmedCustomRefreshFacts::play_owner_proof_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::PlayOwnerProofMissing},
        {&BgmPlaybackConfirmedCustomRefreshFacts::owner_binding_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::OwnerBindingMismatch},
        {&BgmPlaybackConfirmedCustomRefreshFacts::epochs_and_continuity_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::EpochContinuityMismatch},
        {&BgmPlaybackConfirmedCustomRefreshFacts::route_and_lease_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::RouteLeaseMismatch},
        {&BgmPlaybackConfirmedCustomRefreshFacts::lifecycle_and_tokens_exact,
            BgmPlaybackConfirmedCustomRefreshFailure::LifecycleTokenMismatch},
    };
    for (const auto& guard : confirmed_custom_refresh_guards) {
        auto rejected = confirmed_custom_refresh;
        rejected.*guard.member = false;
        require(bgm_playback_confirmed_custom_refresh_first_failure(rejected)
                == guard.failure,
            "confirmed-custom refresh first-failed predicate was not ordered");
    }

    // Production-coupled borrower refresh/release progression for the retained
    // runtime handles. The fixture delegates every decision to the same policy
    // seams used by the production observer and release coordinator.
    struct BorrowerRefreshReleaseFixture final {
        uint64_t old_handle = 0x0000000400010008ULL;
        uint64_t canonical_handle = 0x0000000500000008ULL;
        uint64_t custom_handle = 0x0000000600020008ULL;
        uint64_t canonical_command = runtime_canonical_command;
        uint64_t anchored_custom_command = runtime_anchored_custom_command;
        uint64_t retirement_anchor_route_generation = 2;
        uint64_t lineage_route_predecessor_generation = 2;
        uint64_t transfer_snapshot_route_generation = 3;
        uint64_t canonical_set_input_route_generation = 3;
        uint64_t canonical_route_generation = 4;
        uint64_t canonical_boundary_route_generation = 4;
        uint64_t custom_route_generation = 5;
        AudioRouteLeaseIdentity transfer_lease{1, 0x1234};
        AudioRouteLeaseIdentity canonical_lease{1, 0x1234};
        AudioRouteLeaseIdentity custom_lease{1, 0x1234};
        void* owner = reinterpret_cast<void*>(0x0000000123450000ULL);
        uint32_t owner_thread = 0x1234;
        bool play_owner_proof_confirmed = true;
        void* play_owner = owner;
        uint32_t play_owner_thread = owner_thread;
        uint8_t play_owner_state = kBgmPlaybackCustomPlayOwnerState;
        uint64_t play_owner_command = anchored_custom_command;
        bool parent_custom_transition_consumed = true;
        uint64_t parent_custom_transition_child_ordinal = 2;
        BgmPlaybackLineageTerminalFacts parent{
            false, true, false, false, false, false, false, false, false};
        BgmPlaybackLineageTerminalFacts child{
            false, false, false, true, true, true, false, false, false};
        BgmPlaybackLineageTerminalState persisted_parent_state =
            BgmPlaybackLineageTerminalState::SupersededPending;

        BgmPlaybackLineageTerminalState parent_state() const noexcept
        {
            return classify_bgm_playback_lineage_terminal(parent);
        }

        bool canonical_current(uint64_t active_command) const noexcept
        {
            return bgm_playback_owner_command_phase_exact(true, true, 0, true,
                canonical_command, active_command);
        }

        bool parent_replacement_failure(uint64_t current_handle) const noexcept
        {
            return bgm_playback_lineage_current_reconciliation_allowed(
                       parent_custom_transition_consumed,
                       parent_custom_transition_child_ordinal)
                && canonical_handle != 0 && canonical_handle != current_handle;
        }

        BgmPlaybackConfirmedCustomRefreshFailure refresh_custom_failure(
            const BgmPlaybackConfirmedCustomRefreshFacts& facts,
            void* current_owner, uint32_t current_thread,
            uint64_t active_route_generation,
            const AudioRouteLeaseIdentity& active_lease) const noexcept
        {
            auto phase_facts = facts;
            phase_facts.play_owner_proof_exact =
                bgm_playback_confirmed_custom_play_owner_proof_exact(
                    play_owner_proof_confirmed, owner, owner_thread,
                    canonical_command, anchored_custom_command, play_owner,
                    play_owner_thread, play_owner_state, play_owner_command);
            phase_facts.owner_binding_exact =
                bgm_playback_confirmed_custom_owner_binding_exact(
                    current_owner != nullptr, owner, owner_thread, play_owner,
                    play_owner_thread, current_owner, current_thread);
            phase_facts.route_and_lease_exact =
                bgm_playback_confirmed_custom_route_transition_exact(
                    lineage_route_predecessor_generation,
                    canonical_set_input_route_generation,
                    canonical_route_generation,
                    canonical_boundary_route_generation,
                    custom_route_generation)
                && bgm_playback_confirmed_custom_lease_transition_exact(
                    transfer_lease, canonical_lease, custom_lease)
                && active_route_generation == custom_route_generation
                && active_lease == custom_lease;
            return bgm_playback_confirmed_custom_refresh_first_failure(
                phase_facts);
        }

        bool refresh_custom(const BgmPlaybackConfirmedCustomRefreshFacts& facts,
            void* current_owner, uint32_t current_thread,
            uint64_t active_route_generation,
            const AudioRouteLeaseIdentity& active_lease) const noexcept
        {
            return refresh_custom_failure(facts, current_owner, current_thread,
                       active_route_generation, active_lease)
                == BgmPlaybackConfirmedCustomRefreshFailure::None;
        }

        void publish_rejected_refresh() noexcept
        {
            parent.failed = true;
            persisted_parent_state = classify_bgm_playback_lineage_terminal(
                parent);
        }

        bool release_closed() const noexcept
        {
            if (parent.failed
                || persisted_parent_state
                    == BgmPlaybackLineageTerminalState::Failed) return false;
            const bool parent_closed = parent_state()
                == BgmPlaybackLineageTerminalState::SupersededClosed;
            const bool child_closed = child.old_absent && !child.current_active;
            return bgm_playback_aggregate_release_exact(
                    true, parent_closed && child_closed, true, true, true, true)
                && bgm_playback_aggregate_exit_closure_exact({
                    true, true, parent.old_absent && child.old_absent,
                    true, true, true, true, true, true, true});
        }
    } borrower_fixture;
    const uint64_t observer_inherited_route_predecessor =
        bgm_playback_descendant_route_predecessor(4, 5, 5);
    const uint64_t set_inherited_route_predecessor =
        bgm_playback_descendant_route_predecessor(4, 5, 5);
    auto descendant_borrower_fixture = borrower_fixture;
    descendant_borrower_fixture.old_handle = 0x0000000600020008ULL;
    descendant_borrower_fixture.canonical_handle = 0x0000000700000008ULL;
    descendant_borrower_fixture.custom_handle = 0x0000000800010008ULL;
    descendant_borrower_fixture.transfer_snapshot_route_generation = 5;
    descendant_borrower_fixture.lineage_route_predecessor_generation =
        observer_inherited_route_predecessor;
    descendant_borrower_fixture.canonical_set_input_route_generation = 5;
    descendant_borrower_fixture.canonical_route_generation = 6;
    descendant_borrower_fixture.canonical_boundary_route_generation = 6;
    descendant_borrower_fixture.custom_route_generation = 7;
    auto missing_play_owner_proof_fixture = borrower_fixture;
    missing_play_owner_proof_fixture.play_owner_proof_confirmed = false;
    auto state_zero_play_owner_proof_fixture = borrower_fixture;
    state_zero_play_owner_proof_fixture.play_owner_state = 0;
    auto pre_child_claim_fixture = borrower_fixture;
    pre_child_claim_fixture.parent_custom_transition_consumed = false;
    pre_child_claim_fixture.parent_custom_transition_child_ordinal = 0;
    auto unconsumed_child_ordinal_fixture = borrower_fixture;
    unconsumed_child_ordinal_fixture.parent_custom_transition_consumed = false;
    auto consumed_without_child_fixture = borrower_fixture;
    consumed_without_child_fixture.parent_custom_transition_child_ordinal = 0;
    constexpr uint8_t late_owner_state = 2;
    constexpr uint64_t late_selected_command = runtime_canonical_command;
    bool play_diagnostic_entered = false;
    bool play_failure_called = false;
    const bool durable_play_publication_survived_diagnostic =
        bgm_playback_observe_best_effort(true, [&] {
            play_diagnostic_entered = true;
            throw std::runtime_error("injected post-Play diagnostic failure");
        });
    if (!durable_play_publication_survived_diagnostic) {
        play_failure_called = true;
    }
    auto rejected_borrower_fixture = borrower_fixture;
    bool rejected_diagnostic_entered = false;
    bool rejected_diagnostic_facts_exact = false;
    const BgmPlaybackConfirmedCustomRefreshDiagnostic rejected_diagnostic{
        true, BgmPlaybackConfirmedCustomRefreshFailure::HandleMismatch, 1,
        rejected_borrower_fixture.canonical_handle,
        rejected_borrower_fixture.custom_handle, 0x0000000700000008ULL};
    const bool rejected_refresh_published =
        bgm_playback_publish_before_diagnostic(
            [&] {
                rejected_borrower_fixture.publish_rejected_refresh();
                return true;
            },
            [&] {
                rejected_diagnostic_entered = true;
                rejected_diagnostic_facts_exact = rejected_diagnostic.proposed
                    && rejected_diagnostic.ordinal == 1
                    && rejected_diagnostic.canonical_handle
                        == 0x0000000500000008ULL
                    && rejected_diagnostic.custom_handle
                        == 0x0000000600020008ULL;
                throw std::runtime_error("injected diagnostic failure");
            });
    require(rejected_refresh_published && rejected_diagnostic_entered
            && rejected_diagnostic_facts_exact
            && rejected_borrower_fixture.persisted_parent_state
                == BgmPlaybackLineageTerminalState::Failed
            && rejected_borrower_fixture.parent_state()
                == BgmPlaybackLineageTerminalState::Failed
            && !rejected_borrower_fixture.release_closed(),
        "diagnostic failure escaped before rejected refresh publication or release observed stale facts");
    require(custom_old == 0x0000000400010008ULL
            && canonical_intermediate == 0x0000000500000008ULL
            && next_custom == 0x0000000600020008ULL
            && borrower_fixture.old_handle == custom_old
            && borrower_fixture.canonical_handle == canonical_intermediate
            && borrower_fixture.custom_handle == next_custom
            && !borrower_fixture.parent_replacement_failure(
                0x0000000700000008ULL)
            && pre_child_claim_fixture.parent_replacement_failure(
                0x0000000700000008ULL)
            && unconsumed_child_ordinal_fixture.parent_replacement_failure(
                0x0000000700000008ULL)
            && consumed_without_child_fixture.parent_replacement_failure(
                0x0000000700000008ULL)
            && borrower_fixture.canonical_current(runtime_canonical_command)
            && !borrower_fixture.canonical_current(
                runtime_anchored_custom_command)
            && late_owner_state == 2
            && late_selected_command == runtime_canonical_command
            && bgm_playback_confirmed_custom_route_transition_exact(
                borrower_fixture.lineage_route_predecessor_generation,
                borrower_fixture.canonical_set_input_route_generation,
                borrower_fixture.canonical_route_generation,
                borrower_fixture.canonical_boundary_route_generation,
                borrower_fixture.custom_route_generation)
            && descendant_borrower_fixture.retirement_anchor_route_generation == 2
            && descendant_borrower_fixture.transfer_snapshot_route_generation == 5
            && observer_inherited_route_predecessor
                == set_inherited_route_predecessor
            && descendant_borrower_fixture.lineage_route_predecessor_generation == 4
            && descendant_borrower_fixture.refresh_custom(
                confirmed_custom_refresh, descendant_borrower_fixture.owner,
                descendant_borrower_fixture.owner_thread,
                descendant_borrower_fixture.custom_route_generation,
                descendant_borrower_fixture.custom_lease)
            && !bgm_playback_confirmed_custom_route_transition_exact(
                descendant_borrower_fixture.retirement_anchor_route_generation,
                descendant_borrower_fixture.canonical_set_input_route_generation,
                descendant_borrower_fixture.canonical_route_generation,
                descendant_borrower_fixture.canonical_boundary_route_generation,
                descendant_borrower_fixture.custom_route_generation)
            && !bgm_playback_confirmed_custom_route_transition_exact(
                descendant_borrower_fixture.transfer_snapshot_route_generation,
                descendant_borrower_fixture.canonical_set_input_route_generation,
                descendant_borrower_fixture.canonical_route_generation,
                descendant_borrower_fixture.canonical_boundary_route_generation,
                descendant_borrower_fixture.custom_route_generation)
            && !bgm_playback_confirmed_custom_route_transition_exact(
                borrower_fixture.transfer_snapshot_route_generation,
                borrower_fixture.canonical_set_input_route_generation,
                borrower_fixture.canonical_route_generation,
                borrower_fixture.canonical_boundary_route_generation,
                borrower_fixture.custom_route_generation)
            && borrower_fixture.refresh_custom(confirmed_custom_refresh,
                borrower_fixture.owner, borrower_fixture.owner_thread,
                borrower_fixture.custom_route_generation,
                borrower_fixture.custom_lease)
            && borrower_fixture.refresh_custom_failure(
                confirmed_custom_refresh, reinterpret_cast<void*>(
                    0x0000000123460000ULL), borrower_fixture.owner_thread,
                borrower_fixture.custom_route_generation,
                borrower_fixture.custom_lease)
                == BgmPlaybackConfirmedCustomRefreshFailure::OwnerBindingMismatch
            && missing_play_owner_proof_fixture.refresh_custom_failure(
                confirmed_custom_refresh, borrower_fixture.owner,
                borrower_fixture.owner_thread,
                borrower_fixture.custom_route_generation,
                borrower_fixture.custom_lease)
                == BgmPlaybackConfirmedCustomRefreshFailure::PlayOwnerProofMissing
            && state_zero_play_owner_proof_fixture.refresh_custom_failure(
                confirmed_custom_refresh, borrower_fixture.owner,
                borrower_fixture.owner_thread,
                borrower_fixture.custom_route_generation,
                borrower_fixture.custom_lease)
                == BgmPlaybackConfirmedCustomRefreshFailure::PlayOwnerProofMissing
            && borrower_fixture.refresh_custom_failure(
                confirmed_custom_refresh, borrower_fixture.owner,
                borrower_fixture.owner_thread,
                borrower_fixture.canonical_route_generation,
                borrower_fixture.custom_lease)
                == BgmPlaybackConfirmedCustomRefreshFailure::RouteLeaseMismatch
            && borrower_fixture.refresh_custom_failure(
                confirmed_custom_refresh, borrower_fixture.owner,
                borrower_fixture.owner_thread,
                borrower_fixture.custom_route_generation,
                AudioRouteLeaseIdentity{1, 0x1235})
                == BgmPlaybackConfirmedCustomRefreshFailure::RouteLeaseMismatch
            && durable_play_publication_survived_diagnostic
            && play_diagnostic_entered && !play_failure_called
            && borrower_fixture.parent_state()
                == BgmPlaybackLineageTerminalState::SupersededPending
            && !borrower_fixture.release_closed(),
        "ordinal1 Play-confirmed custom refresh did not retain parent/child ownership");
    borrower_fixture.parent.old_absent = true;
    require(borrower_fixture.parent_state()
                == BgmPlaybackLineageTerminalState::SupersededPending
            && !borrower_fixture.release_closed(),
        "ordinal1 old-request absence closed before canonical/custom borrowers");
    borrower_fixture.parent.canonical_absent = true;
    require(borrower_fixture.parent_state()
                == BgmPlaybackLineageTerminalState::SupersededPending
            && !borrower_fixture.release_closed(),
        "ordinal1 canonical absence closed before custom child ownership transfer");
    borrower_fixture.parent.custom_absent = true;
    require(borrower_fixture.parent_state()
                == BgmPlaybackLineageTerminalState::SupersededClosed
            && !borrower_fixture.release_closed(),
        "parent SupersededClosed did not preserve descendant release blocking");
    borrower_fixture.child.current_active = false;
    borrower_fixture.child.old_absent = true;
    require(borrower_fixture.release_closed(),
        "exact child ordinal2 absence did not close all borrower records");
    const std::array<std::pair<uint64_t, uint64_t>, 3>
        runtime_superseded_generations{{
            {0x0000000500000008ULL, 0x0000000600020008ULL},
            {0x0000000700000008ULL, 0x0000000800010008ULL},
            {0x0000000900000008ULL, 0x0000000a00020008ULL},
        }};
    for (const auto& [canonical, custom] : runtime_superseded_generations) {
        require(bgm_playback_expected_custom_successor(
                    custom, canonical, custom, true)
                && classify_bgm_playback_lineage_terminal(
                    superseded_closed)
                    == BgmPlaybackLineageTerminalState::SupersededClosed,
            "runtime-shaped ancestor did not transfer ownership exactly once");
    }
    auto active_ordinal4 = superseded_closed;
    active_ordinal4.child_claimed_exactly_once = false;
    active_ordinal4.current_active = true;
    active_ordinal4.canonical_absent = false;
    active_ordinal4.custom_absent = false;
    require(classify_bgm_playback_lineage_terminal(active_ordinal4)
            == BgmPlaybackLineageTerminalState::Active,
        "active ordinal 4 was incorrectly superseded");

    // Empty startup/list exits must not poison aggregate admission. The same
    // production gate close/drain/reopen seam is used here so the decision is
    // serialized against callback admission rather than inferred from time.
    BgmPlaybackAggregateExitOwnershipFacts empty_exit_ownership{};
    require(!bgm_playback_aggregate_exit_ownership_present(
                empty_exit_ownership)
            && bgm_playback_aggregate_menu_ready(
                empty_exit_ownership, false, false),
        "empty startup aggregate blocked first custom-song readiness");
    auto owned_exit = empty_exit_ownership;
    owned_exit.active_or_retained_borrower = true;
    require(bgm_playback_aggregate_exit_ownership_present(owned_exit)
            && !bgm_playback_aggregate_menu_ready(
                owned_exit, false, false),
        "active aggregate record was treated as an empty exit");
    owned_exit = empty_exit_ownership;
    owned_exit.unresolved_publication = true;
    require(bgm_playback_aggregate_exit_ownership_present(owned_exit)
            && !bgm_playback_aggregate_menu_ready(
                owned_exit, false, false),
        "global unresolved request did not block menu readiness");
    owned_exit = empty_exit_ownership;
    owned_exit.release_claim_owned = true;
    require(bgm_playback_aggregate_exit_ownership_present(owned_exit),
        "owned release claimant was treated as an empty exit");
    owned_exit = empty_exit_ownership;
    owned_exit.rollback_owned = true;
    require(bgm_playback_aggregate_exit_ownership_present(owned_exit),
        "retained emergency rollback was treated as an empty exit");
    owned_exit = empty_exit_ownership;
    owned_exit.route_or_cleanup_owned = true;
    require(bgm_playback_aggregate_exit_ownership_present(owned_exit),
        "aggregate route/list/frozen cleanup was treated as empty");

    BgmPlaybackAggregateMutationGate empty_exit_gate;
    uint32_t empty_exit_publications = 0;
    uint32_t first_custom_admissions = 0;
    for (uint32_t exit = 0; exit < 3; ++exit) {
        empty_exit_gate.close_and_drain();
        require(empty_exit_gate.closed() && empty_exit_gate.drained(),
            "empty exit did not close/drain atomically");
        const bool reopened = empty_exit_gate.reopen_after_drain_if(
            [&]() noexcept {
                ++empty_exit_publications;
                return !bgm_playback_aggregate_exit_ownership_present(
                    empty_exit_ownership);
            });
        require(reopened && !empty_exit_gate.closed()
                && empty_exit_gate.enter(),
            "repeated empty list return left aggregate admission closed");
        ++first_custom_admissions;
        empty_exit_gate.leave();
    }
    require(empty_exit_publications == 3 && first_custom_admissions == 3,
        "empty exit publication or gate admission count drifted");

    std::array<BgmPlaybackAggregateExitOwnershipFacts, 4> retained_exits{};
    retained_exits[0].active_or_retained_borrower = true;
    retained_exits[1].unresolved_publication = true;
    retained_exits[2].release_claim_owned = true;
    retained_exits[3].rollback_owned = true;
    for (const auto& retained_exit : retained_exits) {
        empty_exit_gate.close_and_drain();
        require(!empty_exit_gate.reopen_after_drain_if(
                    [&]() noexcept {
                        return !bgm_playback_aggregate_exit_ownership_present(
                            retained_exit);
                    })
                && empty_exit_gate.closed(),
            "owned aggregate exit reopened mutation admission");
        empty_exit_gate.reopen();
    }

    // Production mutation gate: exit closes admission first, drains the one
    // callback already inside, then permanently rejects old-lineage patches.
    BgmPlaybackAggregateMutationGate mutation_gate;
    require(mutation_gate.enter(),
        "aggregate mutation gate rejected initial callback");
    std::atomic_bool exit_published{false};
    std::atomic_uint32_t native_calls{0};
    std::atomic_uint32_t old_patches{1};
    std::thread exit_thread([&] {
        mutation_gate.close_and_drain();
        exit_published.store(true, std::memory_order_release);
    });
    while (!mutation_gate.closed()) std::this_thread::yield();
    bool stale_write_attempted = false;
    const bool stale_write_admitted = mutation_gate.while_open([&] {
        stale_write_attempted = true;
    });
    require(!stale_write_admitted && !stale_write_attempted
            && !mutation_gate.enter() && !exit_published.load(
                std::memory_order_acquire),
        "exit publication did not reject after-prepare/before-write mutation");
    ++native_calls; // Existing callback forwards once, then restores its patch.
    old_patches.store(0, std::memory_order_release);
    mutation_gate.leave();
    exit_thread.join();
    require(exit_published.load(std::memory_order_acquire)
            && old_patches.load(std::memory_order_acquire) == 0
            && native_calls.load(std::memory_order_acquire) == 1,
        "exit publication raced native cleanup or duplicated callback");
    require(!mutation_gate.enter(),
        "published exit admitted old Play mutation");
    ++native_calls; // Bypass path is deliberately exact-once and unpatched.
    require(native_calls.load(std::memory_order_acquire) == 2
            && old_patches.load(std::memory_order_acquire) == 0,
        "exit bypass patched old lineage or changed callback count");
    mutation_gate.reopen();
    require(mutation_gate.enter(),
        "completed exit did not permit fresh lineage mutation");
    mutation_gate.leave();

    // Native Set outcome publication is part of the production mutation
    // lease. Exit may close admission while Set is in flight, but cannot
    // publish closure until the exact request or unresolved fallback is
    // durable.
    BgmPlaybackAggregateMutationGate set_publication_gate;
    require(set_publication_gate.enter(),
        "Set publication lease was not admitted");
    std::atomic_uint32_t set_native_calls{1};
    std::atomic_bool set_exit_published{false};
    std::atomic_bool set_request_published{false};
    std::atomic_uint64_t preserved_request{0};
    std::thread set_exit([&] {
        set_publication_gate.close_and_drain();
        set_exit_published.store(true, std::memory_order_release);
    });
    while (!set_publication_gate.closed()) std::this_thread::yield();
    require(!set_exit_published.load(std::memory_order_acquire),
        "exit drain completed before native Set outcome publication");
    preserved_request.store(0x0000000600020008ull,
        std::memory_order_release);
    set_request_published.store(true, std::memory_order_release);
    set_publication_gate.leave();
    set_exit.join();
    require(set_exit_published.load(std::memory_order_acquire)
            && set_request_published.load(std::memory_order_acquire)
            && preserved_request.load(std::memory_order_acquire)
                == 0x0000000600020008ull
            && set_native_calls.load(std::memory_order_acquire) == 1,
        "exit lost or duplicated the Set-created request");
    auto pending_request_closure = exit_closure;
    pending_request_closure.all_old_requests_absent = false;
    require(!bgm_playback_aggregate_exit_closure_exact(
                pending_request_closure),
        "release closed while preserved Set-created request was present");
    pending_request_closure.all_old_requests_absent = true;
    require(bgm_playback_aggregate_exit_closure_exact(
                pending_request_closure),
        "release remained blocked after preserved request absence");

    // The production protected callback and retention policies treat a Set
    // outcome as owed once native entry begins, irrespective of return kind.
    // Model partial state-2 creation followed by both C++ and SEH faults.
    struct FaultedSetFixture final {
        uint64_t request = 0;
        uint8_t state = 0;
        uint64_t owner = 0x2222;
        uint32_t native_calls = 0;
        uint32_t cleanup_calls = 0;
        void cleanup() noexcept
        {
            ++cleanup_calls;
            if (owner == 0x3333) owner = 0x2222;
        }
    } cpp_faulted_set;
    BgmPlaybackAggregateMutationGate faulted_set_gate;
    require(faulted_set_gate.enter(),
        "faulted Set publication lease was not admitted");
    std::atomic_bool faulted_set_exit_published{false};
    std::thread faulted_set_exit([&] {
        faulted_set_gate.close_and_drain();
        faulted_set_exit_published.store(true, std::memory_order_release);
    });
    while (!faulted_set_gate.closed()) std::this_thread::yield();
    const auto cpp_fault_result = bgm_playback_protected_native_boundary(
        [&]() -> BgmPlaybackProtectedNativeResult {
            ++cpp_faulted_set.native_calls;
            cpp_faulted_set.owner = 0x3333;
            cpp_faulted_set.request = 0x0000000600020008ull;
            cpp_faulted_set.state = 2;
            throw std::runtime_error("injected native Set C++ fault");
        },
        [&cpp_faulted_set]() noexcept { cpp_faulted_set.cleanup(); });
    const bool cpp_publication_owed =
        bgm_playback_set_outcome_publication_owed(true, true);
    const bool cpp_fault_retained = cpp_fault_result.fault
                == BgmPlaybackProtectedNativeFault::CppException
            && !cpp_fault_result.succeeded && cpp_publication_owed
            && bgm_playback_set_outcome_retention_required(
                cpp_publication_owed, cpp_fault_result.succeeded, false)
            && bgm_playback_faulted_set_request_exact(true, true,
                AudioBgmRequestHandle{
                    cpp_faulted_set.request}.valid_bgm_request(),
                cpp_faulted_set.state)
            && cpp_faulted_set.native_calls == 1
            && cpp_faulted_set.cleanup_calls == 1
            && cpp_faulted_set.owner == 0x2222;
    require(!faulted_set_exit_published.load(std::memory_order_acquire),
        "exit published before faulted Set request retention");
    faulted_set_gate.leave();
    faulted_set_exit.join();
    require(cpp_fault_retained
            && faulted_set_exit_published.load(std::memory_order_acquire),
        "partially-created C++-faulted Set request was not retained safely");

    FaultedSetFixture seh_faulted_set;
    const auto seh_fault_result = bgm_playback_protected_native_boundary(
        [&]() noexcept {
            ++seh_faulted_set.native_calls;
            seh_faulted_set.owner = 0x3333;
            seh_faulted_set.request = 0x0000000800010008ull;
            seh_faulted_set.state = 2;
            return BgmPlaybackProtectedNativeResult{false,
                BgmPlaybackProtectedNativeFault::StructuredException};
        },
        [&seh_faulted_set]() noexcept { seh_faulted_set.cleanup(); });
    require(seh_fault_result.fault
                == BgmPlaybackProtectedNativeFault::StructuredException
            && bgm_playback_set_outcome_retention_required(true,
                seh_fault_result.succeeded, false)
            && bgm_playback_faulted_set_request_exact(true, true, true,
                seh_faulted_set.state)
            && seh_faulted_set.native_calls == 1
            && seh_faulted_set.cleanup_calls == 1
            && seh_faulted_set.owner == 0x2222,
        "partially-created SEH-faulted Set request was not retained safely");

    require(bgm_playback_set_outcome_retention_required(true, false, false)
            && !bgm_playback_faulted_set_request_exact(true, true, false, 2)
            && !bgm_playback_faulted_set_request_exact(false, false, false, 0),
        "unreadable/zero faulted Set did not require unknown retention");
    require(bgm_playback_set_outcome_retention_required(true, true, false),
        "successful Set observer fault did not require retention fallback");

    // The same lease contains the observer-fault fallback. An unresolved
    // request is durable before exit publishes and blocks both release and a
    // fresh song until an exact later observation resolves or retires it.
    BgmPlaybackAggregateMutationGate fault_publication_gate;
    require(fault_publication_gate.enter(),
        "observer-fault publication lease was not admitted");
    std::atomic_bool fault_exit_published{false};
    bool unresolved_request = false;
    std::thread fault_exit([&] {
        fault_publication_gate.close_and_drain();
        fault_exit_published.store(true, std::memory_order_release);
    });
    while (!fault_publication_gate.closed()) std::this_thread::yield();
    unresolved_request = true;
    fault_publication_gate.leave();
    fault_exit.join();
    auto unresolved_closure = exit_closure;
    unresolved_closure.no_unresolved_native_request = !unresolved_request;
    require(fault_exit_published.load(std::memory_order_acquire)
            && !bgm_playback_aggregate_exit_closure_exact(unresolved_closure)
            && !bgm_playback_new_song_arm_allowed(true, false,
                0x1111, 0x2222),
        "observer fault did not retain request or block new-song readiness");

    // Play confirmation is covered by the same lease lifetime: exit cannot
    // publish between native Play and state-4 publication.
    BgmPlaybackAggregateMutationGate play_publication_gate;
    require(play_publication_gate.enter(),
        "Play publication lease was not admitted");
    std::atomic_uint32_t play_native_calls{1};
    std::atomic_bool play_exit_published{false};
    bool state4_published = false;
    std::thread play_exit([&] {
        play_publication_gate.close_and_drain();
        play_exit_published.store(true, std::memory_order_release);
    });
    while (!play_publication_gate.closed()) std::this_thread::yield();
    require(!play_exit_published.load(std::memory_order_acquire),
        "exit drain completed inside native Play publication window");
    state4_published = true;
    play_publication_gate.leave();
    play_exit.join();
    require(state4_published
            && play_exit_published.load(std::memory_order_acquire)
            && play_native_calls.load(std::memory_order_acquire) == 1,
        "Play publication window duplicated or lost native outcome");

    // Production release transition machine under the same exclusive mutex
    // shape used by audio_sead: list/shutdown/tick contenders can only take one
    // generation/claim pair and stale completion cannot clear its successor.
    BgmPlaybackAggregateReleaseClaimMachine release_machine;
    require(release_machine.publish_pending(true, 1),
        "release claimant did not publish Pending");
    std::mutex release_mutex;
    std::atomic_uint32_t release_callbacks{0};
    std::vector<std::thread> release_contenders;
    for (uint64_t i = 1; i <= 8; ++i) {
        release_contenders.emplace_back([&, i] {
            bool taken = false;
            {
                std::lock_guard<std::mutex> lock(release_mutex);
                taken = release_machine.take(1, i);
            }
            if (taken) ++release_callbacks;
        });
    }
    for (auto& contender : release_contenders) contender.join();
    require(release_callbacks.load(std::memory_order_acquire) == 1
            && release_machine.state
                == BgmPlaybackAggregateReleaseClaimState::InFlight,
        "concurrent release executors invoked native release more than once");
    const uint64_t first_claim = release_machine.claim_id;
    require(!release_machine.take(1, first_claim + 1)
            && !release_machine.complete(0, first_claim, true)
            && !release_machine.complete(1, first_claim + 1, true)
            && release_machine.complete(1, first_claim, true),
        "release claimant accepted duplicate take or stale completion");
    require(release_machine.publish_pending(true, 2)
            && release_machine.take(2, 9)
            && !release_machine.complete(1, first_claim, true)
            && release_machine.complete(2, 9, false)
            && release_machine.state
                == BgmPlaybackAggregateReleaseClaimState::Retained
            && !release_machine.take(2, 10),
        "release failure lost unique ownership or generation ABA protection");

    require(bgm_playback_initial_anchor_state_clean(
                false, false, false, 0, 0, false)
            && !bgm_playback_initial_anchor_state_clean(
                false, false, true, 0, 0, false)
            && !bgm_playback_initial_anchor_state_clean(
                true, false, false, 0, 0, false),
        "pre-anchor recognized operation poisoned fresh boundary state");

    struct ProtectedWrapperFixture final {
        uint64_t owner = 0x1111;
        uint32_t native_calls = 0;
        uint32_t cleanup_calls = 0;
        bool authority = true;
        bool retained = false;

        void arm() noexcept { owner = 0x2222; }
        void cleanup() noexcept
        {
            ++cleanup_calls;
            if (owner == 0x2222) {
                if (authority) owner = 0x1111;
                else retained = true;
            }
        }
    } wrapper;
    static_assert(noexcept(wrapper.cleanup()));

    wrapper.arm();
    auto protected_result = bgm_playback_protected_native_boundary(
        [&]() -> BgmPlaybackProtectedNativeResult {
            ++wrapper.native_calls;
            return {true, BgmPlaybackProtectedNativeFault::None};
        }, [&wrapper]() noexcept { wrapper.cleanup(); });
    require(protected_result.succeeded && wrapper.native_calls == 1
            && wrapper.cleanup_calls == 1 && wrapper.owner == 0x1111
            && !wrapper.retained,
        "normal protected native callback did not restore owner mutation once");

    wrapper = {}; wrapper.arm();
    protected_result = bgm_playback_protected_native_boundary(
        [&]() -> BgmPlaybackProtectedNativeResult {
            ++wrapper.native_calls;
            throw std::runtime_error("injected native C++ fault");
        }, [&wrapper]() noexcept { wrapper.cleanup(); });
    require(!protected_result.succeeded
            && protected_result.fault
                == BgmPlaybackProtectedNativeFault::CppException
            && wrapper.native_calls == 1 && wrapper.cleanup_calls == 1
            && wrapper.owner == 0x1111,
        "C++ native fault skipped exact-once owner restoration");

    wrapper = {}; wrapper.arm();
    protected_result = bgm_playback_protected_native_boundary(
        [&]() noexcept -> BgmPlaybackProtectedNativeResult {
            ++wrapper.native_calls;
            return {false,
                BgmPlaybackProtectedNativeFault::StructuredException};
        }, [&wrapper]() noexcept { wrapper.cleanup(); });
    require(!protected_result.succeeded
            && protected_result.fault
                == BgmPlaybackProtectedNativeFault::StructuredException
            && wrapper.native_calls == 1 && wrapper.cleanup_calls == 1
            && wrapper.owner == 0x1111,
        "production-equivalent SEH seam skipped exact-once cleanup");

    wrapper = {}; wrapper.arm();
    protected_result = bgm_playback_protected_native_boundary(
        [&]() noexcept -> BgmPlaybackProtectedNativeResult {
            ++wrapper.native_calls;
            wrapper.owner = 0x3333;
            return {true, BgmPlaybackProtectedNativeFault::None};
        }, [&wrapper]() noexcept { wrapper.cleanup(); });
    require(protected_result.succeeded && wrapper.native_calls == 1
            && wrapper.owner == 0x3333,
        "protected cleanup overwrote native-owned callback values");

    wrapper = {}; wrapper.authority = false; wrapper.arm();
    protected_result = bgm_playback_protected_native_boundary(
        [&]() noexcept -> BgmPlaybackProtectedNativeResult {
            ++wrapper.native_calls;
            return {false, BgmPlaybackProtectedNativeFault::NativeFailure};
        }, [&wrapper]() noexcept { wrapper.cleanup(); });
    require(!protected_result.succeeded && wrapper.native_calls == 1
            && wrapper.cleanup_calls == 1 && wrapper.retained
            && wrapper.owner == 0x2222,
        "lost rollback authority was not retained fail-closed");

    wrapper = {};
    AudioPatchRestoreField prepared_journal{
        &wrapper.owner, 0, 0x1111, 0x2222,
        sizeof(wrapper.owner), "fault_injection", false};
    AudioPatchRestoreField rollback_owned{};
    static_assert(std::is_trivially_copyable_v<AudioPatchRestoreField>);
    static_assert(noexcept(rollback_owned = prepared_journal));
    rollback_owned = prepared_journal;
    bool journal_prepared = rollback_owned.object == &wrapper.owner;
    try {
        if (journal_prepared) throw std::bad_alloc{};
    } catch (const std::bad_alloc&) {
    }
    require(journal_prepared && wrapper.native_calls == 0
            && wrapper.owner == 0x1111,
        "allocation failure after journal preparation mutated wrapper state");

    BgmPlaybackRollbackAuthorityFacts rollback_authority{
        true, true, true, true, true, true, true, true, true, true, true};
    require(bgm_playback_rollback_authority_exact(rollback_authority),
        "exact owner rollback authority rejected");
    auto reject_authority = [&](bool BgmPlaybackRollbackAuthorityFacts::*field,
                                const char* message) {
        auto drift = rollback_authority;
        drift.*field = false;
        require(!bgm_playback_rollback_authority_exact(drift), message);
    };
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::descriptor_exact,
        "rollback accepted descriptor/slot generation ABA");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::positive_identity_exact,
        "rollback accepted pointer/serial ABA");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::controller_proof_exact,
        "rollback accepted controller proof drift");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::slot_bgm_chain_exact,
        "rollback accepted slot/BGM drift");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::record_version_exact,
        "rollback accepted reentrant record version drift");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::collection_version_exact,
        "rollback accepted collection version drift");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::operation_nonce_exact,
        "rollback accepted operation nonce drift");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::route_lease_exact,
        "rollback accepted route/lease drift");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::lifecycle_tokens_exact,
        "rollback accepted lifecycle/token drift");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::owner_thread_command_exact,
        "rollback accepted owner/thread/command drift");
    reject_authority(&BgmPlaybackRollbackAuthorityFacts::request_exact,
        "rollback accepted Set-created request drift");

    uint64_t protected_field = 0x2222;
    uint64_t protected_current = 0;
    bool protected_authority = true;
    const auto production_cleanup = [&]() noexcept {
        return bgm_playback_protected_rollback_cleanup(
            UINT64_C(0x1111), UINT64_C(0x2222),
            [&](uint64_t& value) noexcept {
                value = protected_field;
                return true;
            },
            [&]() noexcept { return protected_authority; },
            [&](uint64_t value) noexcept {
                protected_field = value;
                return true;
            }, protected_current);
    };
    require(production_cleanup() == BgmPlaybackProtectedRollbackResult::Restored
            && protected_field == 0x1111,
        "production rollback cleanup did not restore exact owner authority");
    protected_field = 0x3333;
    require(production_cleanup()
            == BgmPlaybackProtectedRollbackResult::NativeOverwrite
            && protected_field == 0x3333,
        "production rollback cleanup overwrote native-changed value");
    protected_field = 0x2222;
    protected_authority = false;
    require(production_cleanup() == BgmPlaybackProtectedRollbackResult::Retained
            && protected_field == 0x2222,
        "production rollback cleanup wrote through lost authority");

    int seh_cleanup_calls = 0;
    const auto seh_production_result = bgm_playback_protected_native_boundary(
        []() -> BgmPlaybackProtectedNativeResult {
            return {false, BgmPlaybackProtectedNativeFault::StructuredException};
        }, [&]() noexcept {
            ++seh_cleanup_calls;
            protected_authority = true;
            (void)production_cleanup();
        });
    require(seh_production_result.fault
                == BgmPlaybackProtectedNativeFault::StructuredException
            && seh_cleanup_calls == 1 && protected_field == 0x1111,
        "actual SEH-result boundary did not enter production cleanup exactly once");

    require(bgm_playback_emergency_init_allowed(true, true)
            && !bgm_playback_emergency_init_allowed(false, true)
            && !bgm_playback_emergency_init_allowed(true, false),
        "init discarded armed/retained or durable rollback ownership");
    struct InstallFirstOperationFixture final {
        bool ownership_clear = false;
        uint64_t descriptor_generation = 41;
        uint64_t descriptor_contents = 0xabcddcba;
        int ownership_reads = 0;
        int shared_mutations = 0;
        bool begin() noexcept {
            return bgm_playback_protected_install_begin(
                [&]() noexcept {
                    ++ownership_reads;
                    return ownership_clear;
                }, [&]() noexcept { ++shared_mutations; });
        }
    } install_order;
    require(!install_order.begin() && !install_order.begin()
            && install_order.ownership_reads == 2
            && install_order.shared_mutations == 0
            && install_order.descriptor_generation == 41
            && install_order.descriptor_contents == 0xabcddcba,
        "retained rollback refusal was not the read-only first install operation");
    install_order.ownership_clear = true;
    require(install_order.begin() && install_order.shared_mutations == 1,
        "clear rollback ownership did not permit the first install mutation");

    require(bgm_playback_custom_set_post_request_exact(
                true, true, true, true, true)
            && !bgm_playback_custom_set_post_request_exact(
                false, true, true, true, true)
            && !bgm_playback_custom_set_post_request_exact(
                true, false, true, true, true)
            && !bgm_playback_custom_set_post_request_exact(
                true, true, false, true, true)
            && !bgm_playback_custom_set_post_request_exact(
                true, true, true, false, true)
            && !bgm_playback_custom_set_post_request_exact(
                true, true, true, true, false),
        "custom Set rollback accepted zero/drifted post-Set request authority");
    require(bgm_playback_custom_set_cleanup_authority_exact(
                false, true, false)
            && bgm_playback_custom_set_cleanup_authority_exact(
                true, true, true)
            && !bgm_playback_custom_set_cleanup_authority_exact(
                true, true, false),
        "successful custom Set fell back to pre-Set cleanup authority");
    constexpr uint64_t custom_set_request = UINT64_C(0x0000000600020008);
    constexpr uint64_t custom_set_nonce = 12;
    require(bgm_playback_custom_play_rollback_request_exact(
                custom_set_request, custom_set_request, custom_set_nonce)
            && !bgm_playback_custom_play_rollback_request_exact(
                custom_set_nonce, custom_set_request, custom_set_nonce)
            && !bgm_playback_custom_play_rollback_request_exact(
                0, custom_set_request, custom_set_nonce),
        "custom Play rollback confused the Set nonce with its request handle");
    uint64_t exact_play_owner = UINT64_C(0x2222);
    uint64_t exact_play_current = 0;
    bool exact_play_retained = false;
    const auto exact_play_cleanup = [&]() noexcept {
        const auto result = bgm_playback_protected_rollback_cleanup(
            UINT64_C(0x1111), UINT64_C(0x2222),
            [&](uint64_t& value) noexcept {
                value = exact_play_owner;
                return true;
            },
            [&]() noexcept {
                return bgm_playback_custom_play_rollback_request_exact(
                    custom_set_request, custom_set_request, custom_set_nonce);
            },
            [&](uint64_t value) noexcept {
                exact_play_owner = value;
                return true;
            }, exact_play_current);
        exact_play_retained = result
            == BgmPlaybackProtectedRollbackResult::Retained;
        return result;
    };
    require(exact_play_cleanup()
                == BgmPlaybackProtectedRollbackResult::Restored
            && exact_play_owner == UINT64_C(0x1111)
            && !exact_play_retained,
        "exact custom Play request did not restore owner without retention");
    require(bgm_playback_emergency_slot_arm_allowed(true, 0)
            && !bgm_playback_emergency_slot_arm_allowed(false, 0)
            && !bgm_playback_emergency_slot_arm_allowed(true, UINT64_MAX)
            && bgm_playback_emergency_migration_complete(true, true, true)
            && !bgm_playback_emergency_migration_complete(true, false, false)
            && !bgm_playback_emergency_migration_complete(true, true, false),
        "production emergency slot/migration policy accepted exhaustion or loss");
    require(bgm_playback_emergency_shutdown_succeeded(true, true, true, true)
            && !bgm_playback_emergency_shutdown_succeeded(false, true, true, true)
            && !bgm_playback_emergency_shutdown_succeeded(true, false, true, true)
            && !bgm_playback_emergency_shutdown_succeeded(true, true, false, true)
            && !bgm_playback_emergency_shutdown_succeeded(true, true, true, false),
        "shutdown reported success before drain/rollback ownership closure");

    struct EmergencyLifecycleFixture final {
        enum class State : uint8_t { Free, Armed, Retained, Durable };
        std::array<State, 2> slots{State::Free, State::Free};
        uint64_t generations[2]{0, 0};
        bool durable = false;
        bool allocation_fails = false;
        int arm() noexcept {
            for (int i = 0; i < 2; ++i) {
                if (slots[i] == State::Free) {
                    slots[i] = State::Armed;
                    ++generations[i];
                    return i;
                }
            }
            return -1;
        }
        void retain(int slot) noexcept { slots[slot] = State::Retained; }
        bool migrate(int slot) noexcept {
            if (allocation_fails) return false;
            durable = true;
            slots[slot] = State::Free;
            return true;
        }
        bool clear() const noexcept {
            return slots[0] == State::Free && slots[1] == State::Free && !durable;
        }
    } emergency;
    const int emergency0 = emergency.arm();
    const int emergency1 = emergency.arm();
    require(emergency0 == 0 && emergency1 == 1 && emergency.arm() == -1,
        "emergency rollback slot exhaustion did not fail before mutation");
    const uint64_t first_generation = emergency.generations[0];
    emergency.retain(emergency0);
    emergency.allocation_fails = true;
    require(!emergency.migrate(emergency0) && !emergency.clear()
            && !bgm_playback_emergency_init_allowed(false, true),
        "vector allocation failure discarded retained emergency descriptor");
    emergency.allocation_fails = false;
    require(emergency.migrate(emergency0) && emergency.durable
            && !bgm_playback_emergency_init_allowed(false, false),
        "durable migration lost authority or allowed premature init");
    emergency.slots[emergency1] = EmergencyLifecycleFixture::State::Free;
    emergency.durable = false;
    require(emergency.clear()
            && bgm_playback_emergency_shutdown_succeeded(true, true, true, true),
        "exact restoration/durable resolution did not close shutdown ownership");
    const int reused = emergency.arm();
    require(reused == 0 && emergency.generations[0] == first_generation + 1,
        "rollback slot reuse did not advance generation against ABA");
}

void test_bgm_playback_aggregate_retirement_successor_policy()
{
    using namespace ff7r::piano::game;
    using Classification = BgmPlaybackAggregateRetirementSuccessorClassification;
    BgmPlaybackAggregateRetirementSuccessorFacts valid;
    valid.direct_successor_cleanup_refused = true;
    valid.monitor_origin = 2;
    valid.monitor_epoch = 70;
    valid.monitor_route_generation = 100;
    valid.monitor_request_handle = UINT64_C(0x2800000008);
    valid.monitor_request_generation = 40;
    valid.live_route_generation = 102;
    valid.aggregate_snapshot_found = true;
    valid.aggregate_ordinal = 5;
    valid.aggregate_version = 9;
    valid.collection_version = 12;
    valid.aggregate_active = true;
    valid.terminal_state = BgmPlaybackLineageTerminalState::Active;
    valid.aggregate_marker_closed = true;
    valid.anchor_exact = true;
    valid.controller_exact = true;
    valid.lease_exact = true;
    valid.lifecycle_exact = true;
    valid.tokens_exact = true;
    valid.sound_exact = true;
    valid.old_request_generation = 40;
    valid.canonical_request_generation = 41;
    valid.latest_custom_request_generation = 42;
    valid.old_request_handle = UINT64_C(0x2800000008);
    valid.canonical_request_handle = UINT64_C(0x2900000008);
    valid.latest_custom_request_handle = UINT64_C(0x2a00000008);
    valid.old_absent = true;
    valid.canonical_absent = true;
    valid.latest_custom_absent = true;
    valid.latest_custom_consumed = true;
    valid.anchor_route_generation = 99;
    valid.canonical_input_route_generation = 100;
    valid.canonical_route_generation = 101;
    valid.latest_custom_route_generation = 102;
    valid.operation_predecessor_nonce = 72;
    valid.transition_set_nonce = 71;
    valid.transition_play_nonce = 72;
    valid.operation_nonce_exact = true;
    valid.route_chain_exact = true;
    valid.journals_restored = true;
    valid.frozen_fields_restored = true;
    valid.owner_field_observed = true;
    valid.owner_field_empty = true;
    valid.native_route_empty = true;
    valid.version_recheck_exact = true;

    require(classify_bgm_playback_aggregate_retirement_successor(valid)
            == Classification::ActiveExactSuccessor,
        "exact active aggregate successor was not classified");
    auto terminal = valid;
    terminal.aggregate_active = false;
    terminal.terminal_state = BgmPlaybackLineageTerminalState::SupersededClosed;
    require(classify_bgm_playback_aggregate_retirement_successor(terminal)
            == Classification::TerminalExactSuccessor,
        "exact terminal aggregate successor was not classified");

    auto arithmetic_only = BgmPlaybackAggregateRetirementSuccessorFacts{};
    arithmetic_only.direct_successor_cleanup_refused = true;
    arithmetic_only.monitor_route_generation = 100;
    arithmetic_only.live_route_generation = 102;
    require(classify_bgm_playback_aggregate_retirement_successor(arithmetic_only)
            == Classification::IncompleteSuccessor,
        "arithmetic +2 alone authorized aggregate successor authority");

    auto not_refused = valid;
    not_refused.direct_successor_cleanup_refused = false;
    require(classify_bgm_playback_aggregate_retirement_successor(not_refused)
            == Classification::NotDirectSuccessorRefusal,
        "non-refusal was classified as an aggregate successor");
    auto wrong_generation = valid;
    wrong_generation.live_route_generation = 103;
    require(classify_bgm_playback_aggregate_retirement_successor(wrong_generation)
            == Classification::NotDirectSuccessorRefusal,
        "non-direct generation was classified as aggregate successor");

    auto failed = valid;
    failed.aggregate_failed = true;
    require(classify_bgm_playback_aggregate_retirement_successor(failed)
            == Classification::AggregateFailure,
        "aggregate failure was not classified first");
    failed = valid;
    failed.terminal_state = BgmPlaybackLineageTerminalState::Failed;
    require(classify_bgm_playback_aggregate_retirement_successor(failed)
            == Classification::AggregateFailure,
        "terminal failure was not classified");
    auto version = valid;
    version.version_recheck_exact = false;
    require(classify_bgm_playback_aggregate_retirement_successor(version)
            == Classification::VersionDrift,
        "aggregate version drift was accepted");

    const auto require_identity_failure = [&](auto member) {
        auto facts = valid;
        facts.*member = false;
        require(classify_bgm_playback_aggregate_retirement_successor(facts)
                == Classification::IdentityMismatch,
            "aggregate identity mismatch was accepted");
    };
    require_identity_failure(&BgmPlaybackAggregateRetirementSuccessorFacts::anchor_exact);
    require_identity_failure(&BgmPlaybackAggregateRetirementSuccessorFacts::controller_exact);
    require_identity_failure(&BgmPlaybackAggregateRetirementSuccessorFacts::lease_exact);
    require_identity_failure(&BgmPlaybackAggregateRetirementSuccessorFacts::lifecycle_exact);
    require_identity_failure(&BgmPlaybackAggregateRetirementSuccessorFacts::tokens_exact);
    require_identity_failure(&BgmPlaybackAggregateRetirementSuccessorFacts::sound_exact);

    const auto require_incomplete = [&](auto mutate) {
        auto facts = valid;
        mutate(facts);
        require(classify_bgm_playback_aggregate_retirement_successor(facts)
                == Classification::IncompleteSuccessor,
            "incomplete aggregate successor was accepted");
    };
    require_incomplete([](auto& f) { f.aggregate_snapshot_found = false; });
    require_incomplete([](auto& f) { f.aggregate_marker_closed = false; });
    require_incomplete([](auto& f) { f.monitor_origin = 0; });
    require_incomplete([](auto& f) { f.monitor_epoch = 0; });
    require_incomplete([](auto& f) { f.monitor_request_handle = 0; });
    require_incomplete([](auto& f) { f.aggregate_ordinal = 0; });
    require_incomplete([](auto& f) { f.aggregate_version = 0; });
    require_incomplete([](auto& f) { f.collection_version = 0; });
    require_incomplete([](auto& f) { f.old_request_generation = 0; });
    require_incomplete([](auto& f) { ++f.old_request_generation; });
    require_incomplete([](auto& f) { ++f.old_request_handle; });
    require_incomplete([](auto& f) { f.canonical_request_generation = 0; });
    require_incomplete([](auto& f) { f.latest_custom_request_generation = 0; });
    require_incomplete([](auto& f) { f.canonical_request_handle = 0; });
    require_incomplete([](auto& f) { f.latest_custom_request_handle = 0; });
    require_incomplete([](auto& f) { f.old_absent = false; });
    require_incomplete([](auto& f) { f.canonical_absent = false; });
    require_incomplete([](auto& f) { f.latest_custom_absent = false; });
    require_incomplete([](auto& f) { f.latest_custom_consumed = false; });
    require_incomplete([](auto& f) { f.operation_nonce_exact = false; });
    require_incomplete([](auto& f) { f.route_chain_exact = false; });
    require_incomplete([](auto& f) { f.journals_restored = false; });
    require_incomplete([](auto& f) { f.frozen_fields_restored = false; });
    require_incomplete([](auto& f) { f.owner_field_observed = false; });
    require_incomplete([](auto& f) { f.owner_field_empty = false; });
    require_incomplete([](auto& f) { f.native_route_empty = false; });
    require_incomplete([](auto& f) {
        f.terminal_state = BgmPlaybackLineageTerminalState::SupersededPending;
    });
    require_incomplete([](auto& f) { f.aggregate_active = false; });
    require_incomplete([](auto& f) {
        f.terminal_state = BgmPlaybackLineageTerminalState::SupersededClosed;
    });
}

void test_bgm_playback_aggregate_terminal_handoff_policy()
{
    using namespace ff7r::piano::game;
    using Classification = BgmPlaybackAggregateTerminalHandoffClassification;
    using Node = BgmPlaybackAggregateTerminalLineageNode;

    require(bgm_playback_aggregate_terminal_backing_exact(
                AudioStopRetirementOrigin::OrdinaryStop,
                OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence,
                true, false),
        "ordinary terminal backing evidence was rejected");
    require(!bgm_playback_aggregate_terminal_backing_exact(
                AudioStopRetirementOrigin::OrdinaryStop,
                OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached,
                true, true),
        "detached backing was admitted as ordinary vector evidence");
    require(bgm_playback_aggregate_terminal_backing_exact(
                AudioStopRetirementOrigin::ExactNaturalCompletion,
                OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached,
                true, true)
            && bgm_playback_aggregate_terminal_backing_exact(
                AudioStopRetirementOrigin::ExactNaturalCompletion,
                OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector,
                true, true),
        "exact detached terminal backing evidence was rejected");
    require(!bgm_playback_aggregate_terminal_backing_exact(
                AudioStopRetirementOrigin::ExactNaturalCompletion,
                OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector,
                true, false)
            && !bgm_playback_aggregate_terminal_backing_exact(
                AudioStopRetirementOrigin::None,
                OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence,
                true, true)
            && !bgm_playback_aggregate_terminal_backing_exact(
                AudioStopRetirementOrigin::OrdinaryStop,
                OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence,
                false, false),
        "terminal backing evidence ignored origin or identity drift");
    require(!bgm_playback_aggregate_terminal_pause_conflict(false, true)
            && !bgm_playback_aggregate_terminal_pause_conflict(true, false)
            && bgm_playback_aggregate_terminal_pause_conflict(true, true)
            && !bgm_playback_aggregate_terminal_pause_conflict(
                true, true, true),
        "terminal pause same-lineage conflict projection drifted");
    const auto lifecycle_exact = [](bool restore_applied,
                                     bool release_attempted,
                                     bool release_absent) {
        return bgm_playback_aggregate_terminal_lifecycle_exact(
            true, restore_applied, true, true, true, true, true, true,
            true, true, true, release_attempted, release_absent, true);
    };
    require(lifecycle_exact(true, false, true)
            && !lifecycle_exact(false, false, true)
            && !lifecycle_exact(true, true, true)
            && !lifecycle_exact(true, false, false),
        "RestoreApplied release-unattempted lifecycle projection drifted");
    const auto registry_exact = [](bool playback_absent,
                                    bool playback_active_exact,
                                    const size_t false_cleanup_fact) {
        std::array<bool, 8> cleanup{};
        cleanup.fill(true);
        if (false_cleanup_fact < cleanup.size()) cleanup[false_cleanup_fact] = false;
        return bgm_playback_aggregate_terminal_registry_exact(
            playback_absent, playback_active_exact,
            cleanup[0], cleanup[1], cleanup[2], cleanup[3], cleanup[4],
            cleanup[5], cleanup[6], cleanup[7]);
    };
    require(registry_exact(true, false, 8)
            && registry_exact(false, true, 8)
            && !registry_exact(false, false, 8),
        "terminal playback absent/active registry projection drifted");
    for (size_t i = 0; i < 8; ++i) {
        require(!registry_exact(true, false, i),
            "terminal cleanup-token field drift was admitted");
    }

    const auto leaf = [](uint64_t ordinal, uint32_t request_generation,
                          uint64_t set_route, uint64_t canonical_set_nonce) {
        Node node;
        node.present = true;
        node.domain_exact = true;
        node.active = true;
        node.aggregate_closed = true;
        node.borrowers_zero = true;
        node.terminal_state = BgmPlaybackLineageTerminalState::Active;
        node.ordinal = ordinal;
        node.version = 10 + ordinal;
        node.old_request = (uint64_t{request_generation} << 32) | 8;
        node.canonical_request =
            (uint64_t{request_generation + 1} << 32) | 8;
        node.latest_custom_request =
            (uint64_t{request_generation + 2} << 32) | 8;
        node.old_request_generation = request_generation;
        node.canonical_request_generation = request_generation + 1;
        node.latest_custom_request_generation = request_generation + 2;
        node.old_absent = true;
        node.canonical_absent = true;
        node.latest_custom_absent = true;
        node.anchor_route_generation = 100;
        node.canonical_set_input_route_generation = set_route;
        node.canonical_route_generation = set_route + 1;
        node.latest_custom_route_generation = set_route + 2;
        node.canonical_set_nonce = canonical_set_nonce;
        node.canonical_play_nonce = canonical_set_nonce + 1;
        node.stop_boundary_nonce = canonical_set_nonce + 2;
        node.custom_set_nonce = canonical_set_nonce + 3;
        node.custom_play_nonce = canonical_set_nonce + 4;
        node.operation_predecessor_nonce = node.custom_play_nonce;
        return node;
    };
    const auto complete_facts = [](const Node& root) {
        BgmPlaybackAggregateTerminalHandoffFacts facts;
        facts.candidate = true;
        facts.quiescent_monitor = true;
        facts.monitor_origin = 1;
        facts.monitor_epoch = 9;
        facts.monitor_route_generation = 101;
        facts.monitor_request = root.old_request;
        facts.live_route_generation = root.latest_custom_route_generation;
        facts.collection_version = 30;
        facts.record_count = 1;
        facts.records[0] = root;
        facts.version_recheck_exact = true;
        facts.backing_exact = true;
        facts.lifecycle_present = true;
        facts.lifecycle_restore_applied = true;
        facts.lifecycle_state_epoch_exact = true;
        facts.lifecycle_ordinal_exact = true;
        facts.lifecycle_route_exact = true;
        facts.lifecycle_cleanup_exact = true;
        facts.lifecycle_request_exact = true;
        facts.lifecycle_sound_exact = true;
        facts.lifecycle_canonical_exact = true;
        facts.lifecycle_custom_exact = true;
        facts.lifecycle_owner_restored = true;
        facts.lifecycle_release_attempted = false;
        facts.lifecycle_release_in_flight_absent = true;
        facts.lifecycle_exact = true;
        facts.owner_category = BgmPlaybackAggregateTerminalOwnerCategory::Canonical;
        facts.owner_exact = true;
        facts.frozen_exact = true;
        facts.frozen_generation_exact = true;
        facts.frozen_lease_exact = true;
        facts.frozen_sound_exact = true;
        facts.frozen_non_owner_fields_restored = true;
        facts.frozen_owner_observed = true;
        facts.journals_restored = true;
        facts.registry_exact = true;
        facts.native_route_empty = true;
        facts.setup_absent = true;
        facts.handoff_absent = true;
        facts.release_absent = true;
        return facts;
    };

    Node single = leaf(1, 40, 101, 23);
    auto exact = complete_facts(single);
    require(classify_bgm_playback_aggregate_terminal_handoff(exact)
            == Classification::ExactAtAggregateClosed,
        "one-record terminal handoff was not exact at AggregateClosed");
    auto nonordinary_monitor = exact;
    nonordinary_monitor.monitor_origin =
        static_cast<uint8_t>(AudioStopRetirementOrigin::ExactNaturalCompletion);
    require(classify_bgm_playback_aggregate_terminal_handoff(nonordinary_monitor)
            == Classification::NoQuiescentMonitor,
        "nonordinary monitor armed terminal AggregateClosed authority");
    auto owner_zero = exact;
    owner_zero.owner_category = BgmPlaybackAggregateTerminalOwnerCategory::Zero;
    require(classify_bgm_playback_aggregate_terminal_handoff(owner_zero)
            == Classification::ExactAtAggregateClosed,
        "exact zero owner category was rejected");
    BgmPlaybackAggregateTerminalAuthority authority;
    require(arm_bgm_playback_aggregate_terminal_authority(authority, 16, exact)
            && authority.state
                == BgmPlaybackAggregateTerminalAuthorityState::Armed
            && authority.generation == 16,
        "exact AggregateClosed authority was not armed");
    require(!arm_bgm_playback_aggregate_terminal_authority(authority, 17, exact)
            && !claim_bgm_playback_aggregate_terminal_authority(authority, 15)
            && claim_bgm_playback_aggregate_terminal_authority(authority, 16)
            && authority.state
                == BgmPlaybackAggregateTerminalAuthorityState::InFlight,
        "terminal authority one-use transition drifted");
    fail_bgm_playback_aggregate_terminal_authority(authority);
    require(authority.state == BgmPlaybackAggregateTerminalAuthorityState::Failed,
        "terminal authority failure did not retain ownership");
    clear_bgm_playback_aggregate_terminal_authority(authority);
    require(authority.state == BgmPlaybackAggregateTerminalAuthorityState::None,
        "terminal authority was not cleared after successful route-reset model");
    auto stop = exact;
    stop.natural_stop_recheck = true;
    stop.route_unchanged = true;
    stop.null_chain_exact = true;
    require(classify_bgm_playback_aggregate_terminal_handoff(stop)
            == Classification::ExactAtNaturalStop,
        "exact null-chain Stop recheck was not classified");
    auto stop_post = stop;
    stop_post.post_stop_recheck = true;
    stop_post.post_diagnostic_count = 1;
    stop_post.post_observation_ran = true;
    stop_post.post_observation_succeeded = true;
    stop_post.native_stop_called_once = true;
    stop_post.post_null_chain_exact = true;
    require(classify_bgm_playback_aggregate_terminal_handoff(stop_post)
            == Classification::ExactAtNaturalStop,
        "exactly-once post-Stop diagnostic was not exact");
    auto cleanup_exact = stop_post;
    cleanup_exact.aggregate_closed_to_pre_stop.classification =
        BgmPlaybackAggregateTerminalSemanticDeltaClassification::NoDelta;
    cleanup_exact.aggregate_closed_to_pre_stop.node_set_exact = true;
    cleanup_exact.pre_to_post_stop.classification =
        BgmPlaybackAggregateTerminalSemanticDeltaClassification::NoDelta;
    cleanup_exact.pre_to_post_stop.node_set_exact = true;
    cleanup_exact.post_live_route_generation = cleanup_exact.live_route_generation;
    cleanup_exact.post_monitor_exact = true;
    cleanup_exact.post_backing_exact = true;
    cleanup_exact.post_owner_exact = true;
    cleanup_exact.post_frozen_exact = true;
    cleanup_exact.post_lifecycle_exact = true;
    cleanup_exact.post_pause_exact = true;
    cleanup_exact.post_registry_exact = true;
    cleanup_exact.post_setup_absent = true;
    cleanup_exact.post_handoff_absent = true;
    cleanup_exact.post_release_absent = true;
    require(bgm_playback_aggregate_terminal_cleanup_exact(
                cleanup_exact, Classification::ExactAtNaturalStop),
        "exact authenticated terminal cleanup evidence was rejected");
    require(bgm_playback_aggregate_terminal_callback_retirement_authorized(
                true, true, false),
        "exact terminal cleanup did not authorize callback retirement");
    require(!bgm_playback_aggregate_terminal_callback_retirement_authorized(
                false, true, false)
            && !bgm_playback_aggregate_terminal_callback_retirement_authorized(
                true, false, false)
            && !bgm_playback_aggregate_terminal_callback_retirement_authorized(
                true, true, true),
        "callback retirement admitted incomplete, stale, or repeated authority");
    BgmPlaybackAggregateCallbackRetirementTransaction retirement_tx;
    retirement_tx.authorized = true;
    retirement_tx.lineage_exact = true;
    retirement_tx.root_ordinal = 0x401;
    retirement_tx.leaf_ordinal = 0x402;
    retirement_tx.expected_collection_version = 50;
    retirement_tx.current_collection_version = 50;
    retirement_tx.record_count = 2;
    retirement_tx.records[0] = {0x401, 7, 7, true, false, false};
    retirement_tx.records[1] = {0x402, 11, 11, true, false, false};
    const auto retirement =
        plan_bgm_playback_aggregate_callback_retirement(retirement_tx);
    require(retirement.exact && retirement.record_count == 2
            && retirement.ordinals[0] == 0x401
            && retirement.ordinals[1] == 0x402
            && retirement.next_versions[0] == 8
            && retirement.next_versions[1] == 12
            && retirement.next_collection_version == 51,
        "root/leaf callback retirement was not one versioned transaction");
    auto applied_retirement = retirement_tx;
    applied_retirement.expected_collection_version =
        retirement.next_collection_version;
    applied_retirement.current_collection_version =
        retirement.next_collection_version;
    for (size_t i = 0; i < retirement.record_count; ++i) {
        applied_retirement.records[i].expected_version =
            retirement.next_versions[i];
        applied_retirement.records[i].current_version =
            retirement.next_versions[i];
        applied_retirement.records[i].retired = true;
    }
    require(!bgm_playback_refresh_publish_current(
                0x401, 7, retirement.ordinals[0], retirement.next_versions[0],
                50, retirement.next_collection_version),
        "pre-retirement observer remained publishable after versioned retirement");
    require(!bgm_playback_aggregate_callback_match_eligible(
                applied_retirement.records[0].active,
                applied_retirement.records[0].failed,
                applied_retirement.records[0].retired)
            && !bgm_playback_aggregate_callback_match_eligible(
                applied_retirement.records[1].active,
                applied_retirement.records[1].failed,
                applied_retirement.records[1].retired),
        "applied root/leaf retirement remained matchable by immediate Set or Play");

    auto partial_apply = retirement_tx;
    partial_apply.records[1].active = false;
    require(!plan_bgm_playback_aggregate_callback_retirement(partial_apply).exact,
        "callback retirement partially admitted an ineligible leaf");
    auto drifted_apply = retirement_tx;
    ++drifted_apply.records[1].current_version;
    require(!plan_bgm_playback_aggregate_callback_retirement(drifted_apply).exact,
        "callback retirement admitted record-version drift");
    drifted_apply = retirement_tx;
    ++drifted_apply.current_collection_version;
    require(!plan_bgm_playback_aggregate_callback_retirement(drifted_apply).exact,
        "callback retirement admitted collection-version drift");
    auto aliased_topology = retirement_tx;
    aliased_topology.leaf_ordinal = aliased_topology.root_ordinal;
    require(!plan_bgm_playback_aggregate_callback_retirement(aliased_topology).exact,
        "callback retirement admitted aliased root/leaf topology");

    const auto rollback_tx = applied_retirement;
    const auto rollback =
        plan_bgm_playback_aggregate_callback_retirement_rollback(rollback_tx);
    require(rollback.exact && rollback.next_collection_version == 52
            && rollback.next_versions[0] == 9
            && rollback.next_versions[1] == 13,
        "exact callback-retirement rollback did not advance versions");
    auto drifted_rollback = rollback_tx;
    ++drifted_rollback.records[1].current_version;
    require(!plan_bgm_playback_aggregate_callback_retirement_rollback(
                drifted_rollback).exact,
        "drifted rollback reattached changed terminal lineage");
    drifted_rollback = rollback_tx;
    ++drifted_rollback.current_collection_version;
    require(!plan_bgm_playback_aggregate_callback_retirement_rollback(
                drifted_rollback).exact,
        "rollback ignored collection drift instead of retaining retirement");
    require(bgm_playback_aggregate_callback_match_eligible(true, false, false),
        "live aggregate callback lineage was rejected");
    require(!bgm_playback_aggregate_callback_match_eligible(true, false, true),
        "terminal-retired aggregate remained eligible for immediate Set/Play");
    require(!bgm_playback_aggregate_callback_match_eligible(false, false, false)
            && !bgm_playback_aggregate_callback_match_eligible(true, true, false),
        "incomplete or unrelated callback lineage became eligible");
    BgmPlaybackAggregateExitOwnershipFacts terminal_retirement_ownership{};
    terminal_retirement_ownership.active_or_retained_borrower = true;
    require(!bgm_playback_aggregate_menu_ready(
                terminal_retirement_ownership, false, false),
        "terminal retirement exposed results/list before retained release drained");
    terminal_retirement_ownership.active_or_retained_borrower = false;
    require(bgm_playback_aggregate_menu_ready(
                terminal_retirement_ownership, false, false)
            && bgm_playback_new_song_arm_allowed(
                false, true, 0x7001, 0x7002),
        "drained terminal retirement blocked next difficulty/custom admission");
    bool BgmPlaybackAggregateTerminalHandoffFacts::*post_checks[] = {
        &BgmPlaybackAggregateTerminalHandoffFacts::post_monitor_exact,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_backing_exact,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_owner_exact,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_frozen_exact,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_lifecycle_exact,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_pause_exact,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_registry_exact,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_setup_absent,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_handoff_absent,
        &BgmPlaybackAggregateTerminalHandoffFacts::post_release_absent,
    };
    for (const auto check : post_checks) {
        auto rejected = cleanup_exact;
        (rejected.*check) = false;
        require(!bgm_playback_aggregate_terminal_cleanup_exact(
                    rejected, Classification::ExactAtNaturalStop),
            "terminal cleanup accepted post-Stop authority drift");
    }
    auto post_route_drift = cleanup_exact;
    ++post_route_drift.post_live_route_generation;
    require(!bgm_playback_aggregate_terminal_cleanup_exact(
                post_route_drift, Classification::ExactAtNaturalStop),
        "terminal cleanup accepted post-Stop route drift");
    const auto reject_post = [&](auto mutate) {
        auto rejected = stop_post;
        mutate(rejected);
        require(classify_bgm_playback_aggregate_terminal_handoff(rejected)
                == Classification::LineageMismatch,
            "invalid post-Stop diagnostic evidence was admitted");
    };
    reject_post([](auto& f) { f.post_diagnostic_count = 0; });
    reject_post([](auto& f) { f.post_diagnostic_count = 2; });
    reject_post([](auto& f) { f.post_observation_ran = false; });
    reject_post([](auto& f) { f.post_observation_succeeded = false; });
    reject_post([](auto& f) { f.native_stop_called_once = false; });
    reject_post([](auto& f) { f.post_null_chain_exact = false; });

    Node root = leaf(1, 40, 101, 23);
    Node last = leaf(2, 42, 103, 28);
    root.aggregate_closed = false;
    root.borrowers_zero = false;
    root.terminal_state = BgmPlaybackLineageTerminalState::SupersededClosed;
    root.latest_custom_consumed = true;
    root.child_ordinal = last.ordinal;
    root.stop_boundary_consumed = true;
    last.old_request = root.latest_custom_request;
    last.lineage_route_predecessor_generation = root.canonical_route_generation;
    auto session = complete_facts(root);
    session.record_count = 2;
    session.records[0] = root;
    session.records[1] = last;
    session.live_route_generation = last.latest_custom_route_generation;
    require(classify_bgm_playback_aggregate_terminal_handoff(session)
            == Classification::ExactAtAggregateClosed,
        "session-like two-record lineage was not exact");
    const auto session_lineage = analyze_bgm_playback_aggregate_terminal_lineage(
        session.records.data(), session.record_count, session.monitor_request);
    require(session_lineage.path_count == 2 && session_lineage.root_index == 0
            && session_lineage.leaf_index == 1,
        "session-like lineage did not resolve path=2/root=0/leaf=1");

    auto stop_rebuild = session;
    stop_rebuild.aggregate_closed_semantic_count = 2;
    stop_rebuild.aggregate_closed_semantics[0].ordinal = root.ordinal;
    stop_rebuild.aggregate_closed_semantics[1].ordinal = last.ordinal;
    stop_rebuild.pre_stop_semantic_count = 2;
    stop_rebuild.pre_stop_semantics[0].ordinal = root.ordinal;
    stop_rebuild.pre_stop_semantics[1].ordinal = last.ordinal;
    reset_bgm_playback_aggregate_terminal_stop_snapshot(stop_rebuild);
    require(stop_rebuild.record_count == 0
            && stop_rebuild.pre_stop_semantic_count == 0
            && !stop_rebuild.records[0].present
            && stop_rebuild.pre_stop_semantics[0].ordinal == 0
            && stop_rebuild.aggregate_closed_semantic_count == 2
            && stop_rebuild.aggregate_closed_semantics[0].ordinal == root.ordinal
            && stop_rebuild.aggregate_closed_semantics[1].ordinal == last.ordinal,
        "terminal Stop rebuild retained armed nodes or erased closed semantics");
    stop_rebuild.record_count = 2;
    stop_rebuild.records[0] = root;
    stop_rebuild.records[1] = last;
    const auto stop_rebuild_lineage =
        analyze_bgm_playback_aggregate_terminal_lineage(
            stop_rebuild.records.data(), stop_rebuild.record_count,
            stop_rebuild.monitor_request);
    require(stop_rebuild_lineage.exact && stop_rebuild_lineage.path_count == 2
            && stop_rebuild_lineage.root_index == 0
            && stop_rebuild_lineage.leaf_index == 1,
        "terminal Stop rebuild did not contain each semantic ordinal once");
    require(root.canonical_set_nonce == 23 && root.canonical_play_nonce == 24
            && root.stop_boundary_nonce == 25 && root.custom_set_nonce == 26
            && root.custom_play_nonce == 27 && last.canonical_set_nonce == 28
            && last.canonical_play_nonce == 29 && last.stop_boundary_nonce == 30
            && last.custom_set_nonce == 31 && last.custom_play_nonce == 32,
        "session-like nonce fixture did not model recovered 23..32 sequence");

    auto environment_incomplete = session;
    environment_incomplete.lifecycle_exact = false;
    environment_incomplete.lifecycle_restore_applied = false;
    environment_incomplete.owner_category =
        BgmPlaybackAggregateTerminalOwnerCategory::DetachedCustom;
    environment_incomplete.owner_exact = false;
    environment_incomplete.frozen_exact = false;
    environment_incomplete.pause_conflict = true;
    environment_incomplete.registry_exact = false;
    require(bgm_playback_aggregate_terminal_lineage_cacheable(
                environment_incomplete)
            && classify_bgm_playback_aggregate_terminal_handoff(
                environment_incomplete) == Classification::LifecycleMismatch,
        "lineage-exact environment-incomplete diagnostic candidate was not cached");
    auto cache_version_drift = environment_incomplete;
    cache_version_drift.version_recheck_exact = false;
    require(!bgm_playback_aggregate_terminal_lineage_cacheable(cache_version_drift),
        "version-drift terminal lineage was cached");
    auto cache_branch = environment_incomplete;
    cache_branch.records[1].old_request = cache_branch.monitor_request;
    require(!bgm_playback_aggregate_terminal_lineage_cacheable(cache_branch),
        "branched terminal lineage was cached");

    const auto expect = [&](BgmPlaybackAggregateTerminalHandoffFacts facts,
                            Classification expected, const char* message) {
        require(classify_bgm_playback_aggregate_terminal_handoff(facts) == expected,
            message);
    };
    auto changed = exact;
    changed.candidate = false;
    expect(changed, Classification::NotCandidate, "non-candidate was admitted");
    changed = exact;
    changed.quiescent_monitor = false;
    expect(changed, Classification::NoQuiescentMonitor,
        "missing Quiescent monitor was admitted");
    changed = exact;
    changed.records[0].failed = true;
    expect(changed, Classification::AggregateFailure,
        "aggregate failure was not first");
    changed = exact;
    changed.version_recheck_exact = false;
    expect(changed, Classification::VersionDrift, "version drift was admitted");
    changed = exact;
    changed.collection_version = 0;
    expect(changed, Classification::VersionDrift,
        "missing aggregate collection identity was admitted");
    changed = exact;
    ++changed.monitor_request;
    expect(changed, Classification::RootNotFound, "missing root was admitted");
    changed = session;
    changed.records[1].old_request = changed.monitor_request;
    expect(changed, Classification::LineageBranch, "branched root was admitted");
    changed = session;
    changed.records[1].latest_custom_request = changed.records[0].old_request;
    changed.records[1].latest_custom_consumed = true;
    changed.records[1].child_ordinal = changed.records[0].ordinal;
    changed.records[1].stop_boundary_consumed = true;
    changed.records[0].old_request = changed.records[1].latest_custom_request;
    expect(changed, Classification::LineageCycle,
        "cyclic lineage was not classified");
    changed = session;
    ++changed.records[1].ordinal;
    expect(changed, Classification::LineageMismatch, "ordinal skip was admitted");
    changed = session;
    changed.records[1].domain_exact = false;
    expect(changed, Classification::LineageMismatch,
        "same-domain identity drift was excluded from the lineage");
    changed = session;
    ++changed.records[1].old_request_generation;
    expect(changed, Classification::LineageMismatch,
        "request-generation skip was admitted");
    changed = session;
    changed.records[1].canonical_request ^= 1;
    expect(changed, Classification::LineageMismatch,
        "wrong canonical request handle was admitted");
    changed = session;
    ++changed.records[1].canonical_set_input_route_generation;
    expect(changed, Classification::LineageMismatch,
        "route arithmetic drift was admitted");
    changed = session;
    ++changed.records[1].canonical_set_nonce;
    expect(changed, Classification::LineageMismatch,
        "wrong parent-custom-Play to child-canonical-Set boundary was admitted");
    changed = session;
    ++changed.records[0].custom_set_nonce;
    expect(changed, Classification::LineageMismatch,
        "intra-record nonce continuity drift was admitted");
    changed = session;
    changed.records[0].latest_custom_consumed = false;
    expect(changed, Classification::LineageMismatch,
        "unconsumed nonleaf was admitted");
    changed = session;
    changed.records[1].latest_custom_consumed = true;
    expect(changed, Classification::LineageMismatch,
        "consumed leaf was admitted");
    changed = session;
    changed.records[0].terminal_state = BgmPlaybackLineageTerminalState::Active;
    expect(changed, Classification::LineageMismatch,
        "active nonleaf was admitted");
    changed = session;
    changed.records[1].active = false;
    expect(changed, Classification::LineageMismatch,
        "inactive closed leaf was admitted");

    changed = exact;
    changed.backing_exact = false;
    expect(changed, Classification::BackingMismatch,
        "ordinary/detached backing mismatch was admitted");
    changed = exact;
    changed.lifecycle_exact = false;
    expect(changed, Classification::LifecycleMismatch,
        "lifecycle mismatch was admitted");
    changed = exact;
    changed.lifecycle_restore_applied = false;
    changed.lifecycle_exact = false;
    expect(changed, Classification::LifecycleMismatch,
        "Complete/non-RestoreApplied lifecycle was admitted");
    changed = exact;
    changed.lifecycle_release_attempted = true;
    changed.lifecycle_exact = false;
    expect(changed, Classification::LifecycleMismatch,
        "release-attempted RestoreApplied lifecycle was admitted");
    changed = exact;
    changed.lifecycle_release_in_flight_absent = false;
    changed.lifecycle_exact = false;
    expect(changed, Classification::LifecycleMismatch,
        "release-in-flight RestoreApplied lifecycle was admitted");
    for (const auto category : {
             BgmPlaybackAggregateTerminalOwnerCategory::Unreadable,
             BgmPlaybackAggregateTerminalOwnerCategory::DetachedCustom,
             BgmPlaybackAggregateTerminalOwnerCategory::Foreign}) {
        changed = exact;
        changed.owner_category = category;
        changed.owner_exact = false;
        expect(changed, Classification::OwnerMismatch,
            "unsafe owner category was admitted");
    }
    changed = exact;
    changed.frozen_exact = false;
    expect(changed, Classification::FrozenOrJournalMismatch,
        "frozen mismatch was admitted");
    changed = exact;
    changed.journals_restored = false;
    expect(changed, Classification::FrozenOrJournalMismatch,
        "journal mismatch was admitted");
    changed = exact;
    changed.setup_absent = false;
    expect(changed, Classification::FrozenOrJournalMismatch,
        "pending setup was admitted");
    changed = exact;
    changed.handoff_absent = false;
    expect(changed, Classification::FrozenOrJournalMismatch,
        "pending handoff was admitted");
    changed = exact;
    changed.release_absent = false;
    expect(changed, Classification::FrozenOrJournalMismatch,
        "pending release was admitted");
    changed = exact;
    changed.pause_conflict = true;
    expect(changed, Classification::PauseConflict,
        "same-lineage active pause was admitted");
    changed = exact;
    changed.registry_exact = false;
    expect(changed, Classification::RegistryMismatch,
        "stale playback/cleanup registry relation was admitted");
    changed = exact;
    changed.native_route_empty = false;
    expect(changed, Classification::NativeRouteNotEmpty,
        "nonempty native route was admitted");
    changed = stop;
    changed.route_unchanged = false;
    expect(changed, Classification::LineageMismatch,
        "route drift at Stop was admitted");
    changed = stop;
    changed.null_chain_exact = false;
    expect(changed, Classification::LineageMismatch,
        "nonnull Stop chain was admitted");
    changed = stop;
    changed.version_recheck_exact = false;
    expect(changed, Classification::VersionDrift,
        "borrower drift at Stop was admitted");

    auto arithmetic_only = BgmPlaybackAggregateTerminalHandoffFacts{};
    arithmetic_only.candidate = true;
    arithmetic_only.quiescent_monitor = true;
    arithmetic_only.monitor_origin = 1;
    arithmetic_only.monitor_epoch = 1;
    arithmetic_only.monitor_route_generation = 100;
    arithmetic_only.monitor_request = 8;
    arithmetic_only.live_route_generation = 102;
    arithmetic_only.collection_version = 1;
    arithmetic_only.version_recheck_exact = true;
    expect(arithmetic_only, Classification::RootNotFound,
        "arithmetic +2 alone authorized terminal handoff");

    using DeltaClassification =
        BgmPlaybackAggregateTerminalSemanticDeltaClassification;
    using SemanticNode = BgmPlaybackAggregateTerminalSemanticNode;
    const auto semantic = [](uint64_t ordinal, uint64_t version) {
        SemanticNode node;
        node.present = true;
        node.ordinal = ordinal;
        node.version = version;
        node.active = true;
        node.terminal_state = BgmPlaybackLineageTerminalState::Active;
        node.marker = BgmPlaybackAggregateMarker::AggregateClosed;
        node.old_request = 0x10008;
        node.canonical_request = 0x20008;
        node.latest_custom_request = 0x30008;
        node.old_absent = true;
        node.canonical_absent = true;
        node.latest_custom_absent = true;
        node.stop_boundary_active = true;
        node.stop_boundary_nonce = 25;
        node.stop_boundary_route_generation = 102;
        node.operation_predecessor_nonce = 27;
        node.transition_set_nonce = 26;
        node.transition_play_nonce = 27;
        node.canonical_set_input_route_generation = 101;
        node.canonical_route_generation = 102;
        node.latest_custom_route_generation = 103;
        return node;
    };
    std::array<SemanticNode, 32> semantic_before{};
    std::array<SemanticNode, 32> semantic_after{};
    semantic_before[0] = semantic(1, 10);
    semantic_after[0] = semantic_before[0];
    const auto classify_delta = [&](size_t before_count, uint64_t before_collection,
                                    size_t after_count, uint64_t after_collection) {
        return classify_bgm_playback_aggregate_terminal_semantic_delta(
            semantic_before.data(), before_count, before_collection,
            semantic_after.data(), after_count, after_collection);
    };
    const auto no_delta = classify_delta(1, 20, 1, 20);
    require(no_delta.classification == DeltaClassification::NoDelta
            && bgm_playback_aggregate_terminal_semantics_unchanged(no_delta),
        "no-delta terminal Stop snapshot was not stable");
    semantic_after[0].version = 11;
    const auto version_only = classify_delta(1, 20, 1, 21);
    require(version_only.classification == DeltaClassification::VersionOnly
            && bgm_playback_aggregate_terminal_semantics_unchanged(version_only),
        "version-only terminal Stop delta was not classified");
    semantic_after[0] = semantic_before[0];
    require(classify_delta(1, 20, 1, 25).classification
            == DeltaClassification::VersionOnly
            && bgm_playback_aggregate_terminal_semantics_unchanged(
                classify_delta(1, 20, 1, 25)),
        "collection-only delta was treated as semantic authority");
    semantic_after[0] = semantic_before[0];
    semantic_after[0].version = 11;
    semantic_after[0].marker = BgmPlaybackAggregateMarker::FinalActiveBorrower;
    require(classify_delta(1, 20, 1, 21).classification
            == DeltaClassification::MarkerOnly
            && !bgm_playback_aggregate_terminal_semantics_unchanged(
                classify_delta(1, 20, 1, 21)),
        "marker-only terminal Stop delta was not classified");
    semantic_after[0] = semantic_before[0];
    semantic_after[0].version = 11;
    semantic_after[0].stop_boundary_active = false;
    semantic_after[0].stop_boundary_consumed = true;
    const auto boundary_delta = classify_delta(1, 20, 1, 21);
    require(boundary_delta.classification
            == DeltaClassification::ExactBoundaryInvalidation
            && boundary_delta.boundary_invalidated_node_count == 1,
        "exact boundary invalidation was not distinguished");
    semantic_after[0].version = 12;
    require(classify_delta(1, 20, 1, 21).classification
            == DeltaClassification::BoundaryMutation,
        "inexact boundary invalidation was admitted as exact");

    const auto require_semantic = [&](auto mutate,
                                      DeltaClassification expected,
                                      const char* message) {
        semantic_after[0] = semantic_before[0];
        mutate(semantic_after[0]);
        const auto delta = classify_delta(1, 20, 1, 20);
        require(delta.classification == expected, message);
    };
    require_semantic([](auto& n) { n.active = false; },
        DeltaClassification::ActiveMutation, "active mutation was missed");
    require_semantic([](auto& n) { n.failed = true; },
        DeltaClassification::FailureMutation, "failure mutation was missed");
    require_semantic([](auto& n) {
        n.terminal_state = BgmPlaybackLineageTerminalState::SupersededPending;
    }, DeltaClassification::TerminalMutation, "terminal mutation was missed");
    require_semantic([](auto& n) { ++n.old_request; },
        DeltaClassification::RequestMutation, "request mutation was missed");
    require_semantic([](auto& n) { n.old_absent = false; },
        DeltaClassification::AbsenceMutation, "absence mutation was missed");
    require_semantic([](auto& n) { n.transition_set_pending = true; },
        DeltaClassification::PendingSetMutation, "pending Set mutation was missed");
    require_semantic([](auto& n) { n.latest_custom_consumed = true; },
        DeltaClassification::ChildMutation, "child ownership mutation was missed");
    require_semantic([](auto& n) { ++n.transition_play_nonce; },
        DeltaClassification::NonceMutation, "nonce mutation was missed");
    require_semantic([](auto& n) { ++n.latest_custom_route_generation; },
        DeltaClassification::RouteMutation, "route mutation was missed");
    require_semantic([](auto& n) { n.active = false; n.failed = true; },
        DeltaClassification::MultipleSemanticMutations,
        "multiple semantic mutations were not distinguished");

    semantic_before[1] = semantic(2, 30);
    semantic_after[0] = semantic_before[0];
    semantic_after[1] = semantic_before[1];
    ++semantic_after[0].version;
    ++semantic_after[1].version;
    const auto multi_version = classify_delta(2, 30, 2, 32);
    require(multi_version.classification == DeltaClassification::VersionOnly
            && multi_version.version_changed_node_count == 2,
        "multi-record version delta was not bounded and exact");
    require(classify_delta(2, 30, 1, 31).classification
            == DeltaClassification::NodeSetDrift,
        "pre/post node-set drift was admitted");
    const auto semantic_before_copy = semantic_before;
    (void)classify_delta(2, 30, 2, 32);
    require(std::memcmp(semantic_before_copy.data(), semantic_before.data(),
                sizeof(semantic_before)) == 0,
        "semantic delta classifier mutated pre-Stop snapshots");
    require(bgm_playback_aggregate_terminal_cache_claimable(true, false)
            && !bgm_playback_aggregate_terminal_cache_claimable(true, true)
            && !bgm_playback_aggregate_terminal_cache_claimable(false, false),
        "one-use terminal diagnostic cache policy drifted");

    auto mutation_probe = stop;
    const auto before = mutation_probe;
    (void)classify_bgm_playback_aggregate_terminal_handoff(mutation_probe);
    require(std::memcmp(&before, &mutation_probe, sizeof(before)) == 0,
        "terminal handoff classifier mutated captured facts");
}

} // namespace

int main()
{
    ff7r::piano::game::BgmPlaybackAggregateMutationGate adoption_gate;
    require(adoption_gate.try_drained(),
        "idle aggregate mutation domain rejected nonblocking adoption check");
    require(adoption_gate.enter() && !adoption_gate.try_drained(),
        "active aggregate mutation domain passed nonblocking adoption check");
    adoption_gate.leave();
    require(adoption_gate.try_drained(),
        "retired aggregate mutation domain did not become adoption-ready");
    test_bgm_playback_aggregate_mutation_lease();
    test_bgm_playback_aggregate_observer_policy();
    test_bgm_playback_aggregate_retirement_successor_policy();
    test_bgm_playback_aggregate_terminal_handoff_policy();
    std::cout << "bgm_playback_aggregate_policy_selftest: ok\n";
    return 0;
}
