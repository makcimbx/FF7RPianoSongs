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

void test_onmemory_bank_residency_diagnostic_policy()
{
    using namespace ff7r::piano::game;

    const uint64_t known_tokens[] = {
        0x21c021b0001ULL,
        0x22202210001ULL,
        0x22302220001ULL,
    };
    const uint16_t known_indexes[] = {0x21b, 0x221, 0x222};
    const uint32_t known_generations[] = {0x21c, 0x222, 0x223};
    for (size_t index = 0; index < std::size(known_tokens); ++index) {
        DecodedOnMemoryBankToken decoded;
        require(decode_onmemory_bank_token(known_tokens[index], decoded)
                && decoded.type == 1
                && decoded.index == known_indexes[index]
                && decoded.generation == known_generations[index]
                && decoded.encode() == known_tokens[index],
            "OnMemoryBank token decoder changed the supported token key");
    }
    DecodedOnMemoryBankToken rejected;
    require(!decode_onmemory_bank_token(0x21c021b0101ULL, rejected),
        "OnMemoryBank token decoder accepted a nonzero reserved byte");
    require(classify_onmemory_bank_kind(0) == OnMemoryBankPresence::Absent
            && classify_onmemory_bank_kind(1) == OnMemoryBankPresence::PresentSabf
            && classify_onmemory_bank_kind(2)
                == OnMemoryBankPresence::PresentSupportedBank
            && classify_onmemory_bank_kind(3) == OnMemoryBankPresence::Unexpected
            && classify_onmemory_bank_kind(UINT32_MAX) == OnMemoryBankPresence::Unexpected,
        "OnMemoryBank kind classifier did not fail closed");

    OnMemoryBankDiagnosticPair pair{
        31,
        17,
        {1, 0x21b, 0x21c},
        {1, 0x222, 0x223},
    };
    std::vector<uint64_t> looked_up;
    std::vector<OnMemoryBankDiagnosticRole> roles;
    const auto lookup = [&](const uint64_t token) {
        looked_up.push_back(token);
        return token == known_tokens[0] ? 1U : 0U;
    };
    const auto observe = [&](const OnMemoryBankDiagnosticRole role,
                             const DecodedOnMemoryBankToken&,
                             const uint32_t,
                             const OnMemoryBankPresence) {
        roles.push_back(role);
    };
    observe_onmemory_bank_pair(false, lookup, pair, observe);
    require(looked_up.empty() && roles.empty(),
        "OnMemoryBank lookup ran after catalog/signature gate failure");
    observe_onmemory_bank_pair(true, lookup, pair, observe);
    require(looked_up.size() == 2
            && looked_up[0] == pair.original.encode()
            && looked_up[1] == pair.custom.encode()
            && roles.size() == 2
            && roles[0] == OnMemoryBankDiagnosticRole::Original
            && roles[1] == OnMemoryBankDiagnosticRole::Custom,
        "OnMemoryBank diagnostic did not pass the copied tokens exactly once");

    int callback_depth = 0;
    int audio_state_lock_depth = 0;
    int route_lock_depth = 0;
    int native_calls = 0;
    int formatted = 0;
    bool exact_erased = false;
    OnMemoryBankDiagnosticPairState production_state;
    production_state.retain(pair);
    OnMemoryBankDiagnosticPair emitted_pair;
    const uint64_t retirement_generation = pair.route_generation + 1;
    struct DepthScope {
        explicit DepthScope(int& depth) : depth_(depth) { ++depth_; }
        ~DepthScope() { --depth_; }
        int& depth_;
    };
    {
        DepthScope callback(callback_depth);
        auto deferred = make_deferred_noexcept_action([&] {
            observe_onmemory_bank_pair(
                true,
                [&](const uint64_t) {
                    require(audio_state_lock_depth == 0 && route_lock_depth == 0
                            && callback_depth == 1,
                        "native OnMemoryBank wrapper ran under plugin locks or without callback lifetime");
                    ++native_calls;
                    return 1U;
                },
                emitted_pair,
                [&](const OnMemoryBankDiagnosticRole,
                    const DecodedOnMemoryBankToken&, const uint32_t,
                    const OnMemoryBankPresence) {
                    require(audio_state_lock_depth == 0 && route_lock_depth == 0
                            && callback_depth == 1,
                        "OnMemoryBank formatting ran under plugin locks or without callback lifetime");
                    ++formatted;
                });
            DepthScope audio_state_lock(audio_state_lock_depth);
            exact_erased = production_state.erase_exact(emitted_pair);
        });
        DepthScope route_lock(route_lock_depth);
        {
            DepthScope audio_state_lock(audio_state_lock_depth);
            OnMemoryBankDiagnosticPair wrong_generation;
            require(!production_state.copy_exact(
                        retirement_generation,
                        pair.cleanup_generation,
                        wrong_generation),
                "retirement generation N+1 incorrectly matched frozen generation N");
            require(production_state.copy_exact(
                        pair.route_generation,
                        pair.cleanup_generation,
                        emitted_pair),
                "frozen PlaySetup generation N did not match at retirement N+1");
            if (emitted_pair) deferred.make_eligible();
        }
    }
    require(native_calls == 2 && formatted == 2
            && exact_erased && !production_state.pair
            && callback_depth == 0 && audio_state_lock_depth == 0
            && route_lock_depth == 0,
        "production-order OnMemoryBank retirement emission or exact erase failed");
    OnMemoryBankDiagnosticPair stale_copy;
    require(!production_state.copy_exact(
                pair.route_generation, pair.cleanup_generation, stale_copy),
        "erased OnMemoryBank generation remained copyable");
    OnMemoryBankDiagnosticPair newer_pair = pair;
    newer_pair.route_generation += 7;
    production_state.retain(newer_pair);
    require(!production_state.erase_exact(emitted_pair)
            && production_state.matches(
                newer_pair.route_generation, newer_pair.cleanup_generation),
        "old deferred erase removed a newer OnMemoryBank generation");
    require(!production_state.copy_exact(
                pair.route_generation, pair.cleanup_generation, stale_copy),
        "stale OnMemoryBank generation copied over a newer retained pair");

    OnMemoryBankDiagnosticPairState retained;
    retained.retain(pair);
    struct RouteOutcomes {
        bool restored = true;
        bool published = true;
        bool retired = true;
        unsigned route = 7;
        bool operator==(const RouteOutcomes& other) const
        {
            return restored == other.restored && published == other.published
                && retired == other.retired && route == other.route;
        }
    };
    const auto run_cleanup = [&](const bool diagnostics_enabled) {
        RouteOutcomes outcomes;
        OnMemoryBankDiagnosticPairState state;
        state.retain(pair);
        OnMemoryBankDiagnosticPair copied;
        require(state.copy_exact(31, 17, copied),
            "decoded diagnostic pair did not survive cleanup commit");
        if (diagnostics_enabled) {
            observe_onmemory_bank_pair(true,
                [](const uint64_t) { return 0U; }, copied,
                [](const OnMemoryBankDiagnosticRole,
                    const DecodedOnMemoryBankToken&, const uint32_t,
                    const OnMemoryBankPresence) {});
        }
        require(state.erase_exact(copied) && !state.pair,
            "diagnostic pair was not erased after post-retirement emission");
        return outcomes;
    };
    require(run_cleanup(false) == run_cleanup(true),
        "diagnostics changed restore/publication/retirement/route outcomes");
    OnMemoryBankDiagnosticPair wrong_generation = pair;
    ++wrong_generation.cleanup_generation;
    require(!retained.erase_exact(wrong_generation) && retained.pair,
        "non-exact retirement erased an active diagnostic generation");
}

void test_onmemory_bank_sequential_lifecycle()
{
    using namespace ff7r::piano::game;
    const OnMemoryBankSoundIdentity arm_sound{
        reinterpret_cast<void*>(0xf000), {15, 25}};
    const DecodedOnMemoryBankToken canonical{1, 0x21b, 0x21c};
    const uint64_t canonical_value = canonical.encode();
    const OnMemoryBankSoundIdentity sound{
        reinterpret_cast<void*>(0x10000), {17, 29}};

    const CanonicalSubstrateBridgePlaySetupFacts accepted_bridge{
        true, true, true, true, true, true, true, true, true,
        true, true, true, true, true, true, true, true, true};
    bool CanonicalSubstrateBridgePlaySetupFacts::* const bridge_facts[] = {
        &CanonicalSubstrateBridgePlaySetupFacts::owner_zero,
        &CanonicalSubstrateBridgePlaySetupFacts::phase_stop_observed,
        &CanonicalSubstrateBridgePlaySetupFacts::generation_valid,
        &CanonicalSubstrateBridgePlaySetupFacts::transaction_valid,
        &CanonicalSubstrateBridgePlaySetupFacts::release_epoch_valid,
        &CanonicalSubstrateBridgePlaySetupFacts::revocation_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::selection_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::route_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::lease_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::song_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::controller_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::controller_proof_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::slot_bgm_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::old_sound_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::old_request_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::callback_sound_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::callback_identity_exact,
        &CanonicalSubstrateBridgePlaySetupFacts::canonical_token_nonzero,
    };
    const CanonicalSubstrateBridgePlaySetupFailure bridge_failures[] = {
        CanonicalSubstrateBridgePlaySetupFailure::OwnerNotZero,
        CanonicalSubstrateBridgePlaySetupFailure::PhaseNotStopObserved,
        CanonicalSubstrateBridgePlaySetupFailure::GenerationInvalid,
        CanonicalSubstrateBridgePlaySetupFailure::TransactionInvalid,
        CanonicalSubstrateBridgePlaySetupFailure::ReleaseEpochInvalid,
        CanonicalSubstrateBridgePlaySetupFailure::RevocationDrift,
        CanonicalSubstrateBridgePlaySetupFailure::SelectionDrift,
        CanonicalSubstrateBridgePlaySetupFailure::RouteDrift,
        CanonicalSubstrateBridgePlaySetupFailure::LeaseDrift,
        CanonicalSubstrateBridgePlaySetupFailure::SongDrift,
        CanonicalSubstrateBridgePlaySetupFailure::ControllerDrift,
        CanonicalSubstrateBridgePlaySetupFailure::ControllerProofDrift,
        CanonicalSubstrateBridgePlaySetupFailure::SlotBgmDrift,
        CanonicalSubstrateBridgePlaySetupFailure::OldSoundDrift,
        CanonicalSubstrateBridgePlaySetupFailure::OldRequestDrift,
        CanonicalSubstrateBridgePlaySetupFailure::CallbackSoundDrift,
        CanonicalSubstrateBridgePlaySetupFailure::CallbackIdentityDrift,
        CanonicalSubstrateBridgePlaySetupFailure::CanonicalTokenInvalid,
    };
    static_assert(std::size(bridge_facts) == std::size(bridge_failures));
    require(canonical_substrate_bridge_play_setup_exact(accepted_bridge),
        "exact substrate bridge facts were rejected");
    require(first_canonical_substrate_bridge_play_setup_failure(accepted_bridge)
            == CanonicalSubstrateBridgePlaySetupFailure::None,
        "exact substrate bridge facts reported a failure");
    for (size_t index = 0; index < std::size(bridge_facts); ++index) {
        auto rejected = accepted_bridge;
        rejected.*bridge_facts[index] = false;
        require(!canonical_substrate_bridge_play_setup_exact(rejected),
            "substrate bridge accepted a missing authority fact");
        require(first_canonical_substrate_bridge_play_setup_failure(rejected)
                == bridge_failures[index],
            "substrate bridge did not report the first missing authority fact");
    }

    constexpr uint64_t bridge_route_n = 42;
    const auto exact_patch_transition =
        canonical_substrate_bridge_patch_transition(
            CanonicalSubstrateBridgePhase::OwnerZeroQualified,
            true,
            bridge_route_n,
            bridge_route_n,
            bridge_route_n,
            bridge_route_n + 1);
    require(exact_patch_transition.failure
                == CanonicalSubstrateBridgePatchTransitionFailure::None
            && exact_patch_transition.phase
                == CanonicalSubstrateBridgePhase::PatchedOriginalInFlight
            && exact_patch_transition.route_generation == bridge_route_n + 1
            && exact_patch_transition.generation_advanced,
        "exact bridge patch transition did not advance N to N+1");

    struct PatchTransitionFailureCase {
        CanonicalSubstrateBridgePhase phase;
        bool authority_generation_exact;
        uint64_t bridge_generation;
        uint64_t setup_generation;
        uint64_t route_generation;
        uint64_t patched_generation;
        CanonicalSubstrateBridgePatchTransitionFailure expected;
    };
    const PatchTransitionFailureCase patch_transition_failures[] = {
        {CanonicalSubstrateBridgePhase::PatchedOriginalInFlight, true,
            bridge_route_n, bridge_route_n, bridge_route_n, bridge_route_n + 1,
            CanonicalSubstrateBridgePatchTransitionFailure::PhaseNotOwnerZeroQualified},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, false,
            bridge_route_n, bridge_route_n, bridge_route_n, bridge_route_n + 1,
            CanonicalSubstrateBridgePatchTransitionFailure::AuthorityGenerationDrift},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, true,
            0, bridge_route_n, bridge_route_n, bridge_route_n + 1,
            CanonicalSubstrateBridgePatchTransitionFailure::BridgeRouteGenerationZero},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, true,
            bridge_route_n, 0, bridge_route_n, bridge_route_n + 1,
            CanonicalSubstrateBridgePatchTransitionFailure::SetupRouteGenerationZero},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, true,
            bridge_route_n, bridge_route_n - 1, bridge_route_n - 1, bridge_route_n,
            CanonicalSubstrateBridgePatchTransitionFailure::BridgeSetupGenerationDrift},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, true,
            bridge_route_n, bridge_route_n, bridge_route_n - 1, bridge_route_n + 1,
            CanonicalSubstrateBridgePatchTransitionFailure::RouteSetupGenerationDrift},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, true,
            UINT64_MAX, UINT64_MAX, UINT64_MAX, 0,
            CanonicalSubstrateBridgePatchTransitionFailure::PatchedRouteGenerationZero},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, true,
            bridge_route_n, bridge_route_n, bridge_route_n, bridge_route_n,
            CanonicalSubstrateBridgePatchTransitionFailure::PatchedRouteNotSuccessor},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, true,
            bridge_route_n, bridge_route_n, bridge_route_n, bridge_route_n + 2,
            CanonicalSubstrateBridgePatchTransitionFailure::PatchedRouteNotSuccessor},
        {CanonicalSubstrateBridgePhase::OwnerZeroQualified, true,
            bridge_route_n, bridge_route_n, bridge_route_n, bridge_route_n - 1,
            CanonicalSubstrateBridgePatchTransitionFailure::PatchedRouteNotSuccessor},
    };
    for (const auto& failure_case : patch_transition_failures) {
        const auto transition = canonical_substrate_bridge_patch_transition(
            failure_case.phase,
            failure_case.authority_generation_exact,
            failure_case.bridge_generation,
            failure_case.setup_generation,
            failure_case.route_generation,
            failure_case.patched_generation);
        require(transition.failure == failure_case.expected
                && transition.phase == CanonicalSubstrateBridgePhase::Failed
                && !transition.generation_advanced,
            "inexact bridge patch transition was not rejected before original");
    }

    const auto no_pending_restore =
        canonical_substrate_bridge_restore_prepatch(
            true,
            CanonicalSubstrateBridgePhase::OwnerZeroQualified,
            true,
            bridge_route_n,
            bridge_route_n,
            bridge_route_n,
            bridge_route_n + 1);
    require(no_pending_restore.failure
                == CanonicalSubstrateBridgeRestorePrepatchFailure::None
            && no_pending_restore.post_restore_check_attempted
            && no_pending_restore.post_restore_exact
            && no_pending_restore.new_patch_allowed
            && no_pending_restore.original_allowed,
        "no-pending exact restore path did not authorize the post-restore patch");
    const auto pending_restore_unchanged =
        canonical_substrate_bridge_restore_prepatch(
            true,
            CanonicalSubstrateBridgePhase::OwnerZeroQualified,
            true,
            bridge_route_n,
            bridge_route_n,
            bridge_route_n,
            bridge_route_n + 1);
    require(pending_restore_unchanged.post_restore_exact
            && pending_restore_unchanged.new_patch_allowed,
        "successful unchanged pending restoration was rejected");
    const auto pending_restore_advanced_route =
        canonical_substrate_bridge_restore_prepatch(
            true,
            CanonicalSubstrateBridgePhase::OwnerZeroQualified,
            true,
            bridge_route_n,
            bridge_route_n,
            bridge_route_n + 1,
            bridge_route_n + 2);
    require(pending_restore_advanced_route.failure
                == CanonicalSubstrateBridgeRestorePrepatchFailure::PatchTransitionRejected
            && pending_restore_advanced_route.transition.failure
                == CanonicalSubstrateBridgePatchTransitionFailure::RouteSetupGenerationDrift
            && pending_restore_advanced_route.transition.phase
                == CanonicalSubstrateBridgePhase::Failed
            && !pending_restore_advanced_route.new_patch_allowed
            && !pending_restore_advanced_route.original_allowed,
        "route-changing restoration authorized a new patch or original call");
    const auto restore_failed = canonical_substrate_bridge_restore_prepatch(
        false,
        CanonicalSubstrateBridgePhase::OwnerZeroQualified,
        true,
        bridge_route_n,
        bridge_route_n,
        bridge_route_n,
        bridge_route_n + 1);
    require(restore_failed.failure
                == CanonicalSubstrateBridgeRestorePrepatchFailure::RestoreFailed
            && !restore_failed.post_restore_check_attempted
            && restore_failed.transition.phase
                == CanonicalSubstrateBridgePhase::Failed
            && !restore_failed.new_patch_allowed
            && !restore_failed.original_allowed,
        "failed restoration authorized a new patch or original call");
    const auto synchronized_restored_state =
        canonical_substrate_bridge_restore_prepatch(
            true,
            CanonicalSubstrateBridgePhase::OwnerZeroQualified,
            true,
            bridge_route_n + 1,
            bridge_route_n + 1,
            bridge_route_n + 1,
            bridge_route_n + 2);
    require(synchronized_restored_state.post_restore_exact
            && synchronized_restored_state.transition.route_generation
                == bridge_route_n + 2
            && synchronized_restored_state.new_patch_allowed,
        "legitimately synchronized restored authority was rejected");
    for (const auto& failure_case : patch_transition_failures) {
        const auto rejected = canonical_substrate_bridge_restore_prepatch(
            true,
            failure_case.phase,
            failure_case.authority_generation_exact,
            failure_case.bridge_generation,
            failure_case.setup_generation,
            failure_case.route_generation,
            failure_case.patched_generation);
        require(rejected.failure
                    == CanonicalSubstrateBridgeRestorePrepatchFailure::PatchTransitionRejected
                && rejected.transition.failure == failure_case.expected
                && !rejected.new_patch_allowed
                && !rejected.original_allowed,
            "post-restore stale/wrap transition authorized patch or original");
    }

    const CanonicalSubstrateBridgeSetCandidateFacts accepted_set_bridge{
        true, true, true, true, true, true, true,
        true, true, true, true, true, true};
    bool CanonicalSubstrateBridgeSetCandidateFacts::* const set_bridge_facts[] = {
        &CanonicalSubstrateBridgeSetCandidateFacts::phase_patched_original,
        &CanonicalSubstrateBridgeSetCandidateFacts::generation_valid,
        &CanonicalSubstrateBridgeSetCandidateFacts::original_inflight,
        &CanonicalSubstrateBridgeSetCandidateFacts::route_phase_exact,
        &CanonicalSubstrateBridgeSetCandidateFacts::route_generation_exact,
        &CanonicalSubstrateBridgeSetCandidateFacts::lease_exact,
        &CanonicalSubstrateBridgeSetCandidateFacts::song_exact,
        &CanonicalSubstrateBridgeSetCandidateFacts::setup_token_exact,
        &CanonicalSubstrateBridgeSetCandidateFacts::selection_exact,
        &CanonicalSubstrateBridgeSetCandidateFacts::revocation_exact,
        &CanonicalSubstrateBridgeSetCandidateFacts::controller_exact,
        &CanonicalSubstrateBridgeSetCandidateFacts::sound_nonnull,
        &CanonicalSubstrateBridgeSetCandidateFacts::sound_pointer_exact,
    };
    const CanonicalSubstrateBridgeSetFailure set_bridge_failures[] = {
        CanonicalSubstrateBridgeSetFailure::PhaseNotPatchedOriginal,
        CanonicalSubstrateBridgeSetFailure::GenerationInvalid,
        CanonicalSubstrateBridgeSetFailure::OriginalNotInFlight,
        CanonicalSubstrateBridgeSetFailure::RoutePhaseDrift,
        CanonicalSubstrateBridgeSetFailure::RouteGenerationDrift,
        CanonicalSubstrateBridgeSetFailure::LeaseDrift,
        CanonicalSubstrateBridgeSetFailure::SongDrift,
        CanonicalSubstrateBridgeSetFailure::SetupTokenDrift,
        CanonicalSubstrateBridgeSetFailure::SelectionDrift,
        CanonicalSubstrateBridgeSetFailure::RevocationDrift,
        CanonicalSubstrateBridgeSetFailure::ControllerDrift,
        CanonicalSubstrateBridgeSetFailure::SoundNull,
        CanonicalSubstrateBridgeSetFailure::SoundPointerDrift,
    };
    static_assert(std::size(set_bridge_facts) == std::size(set_bridge_failures));
    require(canonical_substrate_bridge_set_identity_read_required(accepted_set_bridge)
            && first_canonical_substrate_bridge_set_candidate_failure(accepted_set_bridge)
                == CanonicalSubstrateBridgeSetFailure::None,
        "exact Set bridge candidate did not request one identity read");
    for (size_t index = 0; index < std::size(set_bridge_facts); ++index) {
        auto rejected = accepted_set_bridge;
        rejected.*set_bridge_facts[index] = false;
        require(!canonical_substrate_bridge_set_identity_read_required(rejected)
                && first_canonical_substrate_bridge_set_candidate_failure(rejected)
                    == set_bridge_failures[index],
            "inexact Set bridge candidate requested an identity read");
    }

    const CanonicalSubstrateBridgeSetIdentityFacts exact_set_identity{
        true, true, true};
    require(first_canonical_substrate_bridge_set_identity_failure(exact_set_identity)
            == CanonicalSubstrateBridgeSetFailure::None,
        "exact Set bridge identity was rejected");
    require(canonical_substrate_bridge_set_result_phase(
                CanonicalSubstrateBridgePhase::PatchedOriginalInFlight,
                CanonicalSubstrateBridgeSetFailure::None)
            == CanonicalSubstrateBridgePhase::SetBound,
        "exact Set bridge did not bind once");
    require(exact_patch_transition.phase
                == CanonicalSubstrateBridgePhase::PatchedOriginalInFlight
            && exact_patch_transition.route_generation == bridge_route_n + 1
            && canonical_substrate_bridge_set_identity_read_required(
                accepted_set_bridge)
            && first_canonical_substrate_bridge_set_identity_failure(
                exact_set_identity) == CanonicalSubstrateBridgeSetFailure::None
            && canonical_substrate_bridge_set_result_phase(
                exact_patch_transition.phase,
                CanonicalSubstrateBridgeSetFailure::None)
                == CanonicalSubstrateBridgePhase::SetBound,
        "N-to-N+1 bridge transition did not reach exact SetBound");
    const CanonicalSubstrateBridgeSetIdentityFacts failed_set_identities[] = {
        {false, false, false},
        {true, false, true},
        {true, true, false},
    };
    const CanonicalSubstrateBridgeSetFailure set_identity_failures[] = {
        CanonicalSubstrateBridgeSetFailure::IdentityReadFailed,
        CanonicalSubstrateBridgeSetFailure::IdentityIndexDrift,
        CanonicalSubstrateBridgeSetFailure::IdentitySerialDrift,
    };
    for (size_t index = 0; index < std::size(failed_set_identities); ++index) {
        const auto failure = first_canonical_substrate_bridge_set_identity_failure(
            failed_set_identities[index]);
        require(failure == set_identity_failures[index]
                && canonical_substrate_bridge_set_result_phase(
                       CanonicalSubstrateBridgePhase::PatchedOriginalInFlight,
                       failure) == CanonicalSubstrateBridgePhase::Failed,
            "Set bridge identity failure was not terminal");
    }
    require(canonical_substrate_bridge_set_result_phase(
                CanonicalSubstrateBridgePhase::SetBound,
                CanonicalSubstrateBridgeSetFailure::DuplicateSet)
            == CanonicalSubstrateBridgePhase::Failed,
        "duplicate Set bridge use was not terminal");

    require(!cleanup_only_post_callback_observation_required(false, 0)
            && !cleanup_only_post_callback_observation_required(true, 1)
            && cleanup_only_post_callback_observation_required(true, 0),
        "cleanup-only observation was not restricted to post-body outermost callbacks");
    require(cleanup_only_backing_is_detached(true, nullptr)
            && !cleanup_only_backing_is_detached(false, nullptr)
            && !cleanup_only_backing_is_detached(
                true, reinterpret_cast<void*>(0x5a5a5a5a)),
        "cleanup-only backing policy accepted unreadable or arbitrary nonnull backing");
    require(!deferred_mutation_log_may_emit({false, true, true})
            && !deferred_mutation_log_may_emit({true, false, true})
            && !deferred_mutation_log_may_emit({true, true, false})
            && deferred_mutation_log_may_emit({true, true, true}),
        "borrower mutation log policy admitted formatting before mutation authority release");
    BgmPlaybackDeferredLogProposal set_failure_proposal{};
    set_failure_proposal.kind = BgmPlaybackDeferredLogKind::SetFailure;
    set_failure_proposal.set_failure =
        BgmPlaybackSetCommitFailure::RecordVersion;
    set_failure_proposal.ordinal = 11;
    set_failure_proposal.expected_version = 12;
    set_failure_proposal.current_version = 13;
    set_failure_proposal.previous_nonce = 14;
    set_failure_proposal.nonce = 15;
    set_failure_proposal.mutation_complete = true;
    require(set_failure_proposal.kind == BgmPlaybackDeferredLogKind::SetFailure
            && set_failure_proposal.set_failure
                == BgmPlaybackSetCommitFailure::RecordVersion
            && set_failure_proposal.ordinal == 11
            && set_failure_proposal.expected_version == 12
            && set_failure_proposal.current_version == 13
            && set_failure_proposal.previous_nonce == 14
            && set_failure_proposal.nonce == 15
            && deferred_mutation_log_may_emit({true,
                set_failure_proposal.mutation_complete, true}),
        "deferred Set failure proposal did not preserve exact mutation facts");
    BgmPlaybackDeferredLogProposal play_failure_proposal{};
    play_failure_proposal.kind = BgmPlaybackDeferredLogKind::PlayFailure;
    play_failure_proposal.play_failure = BgmPlaybackPlayFailure::Route;
    play_failure_proposal.expected_route = 21;
    play_failure_proposal.current_route = 22;
    play_failure_proposal.expected_lifecycle = 23;
    play_failure_proposal.current_lifecycle = 24;
    play_failure_proposal.expected_nonce = 25;
    play_failure_proposal.nonce = 26;
    play_failure_proposal.mutation_complete = true;
    require(play_failure_proposal.kind == BgmPlaybackDeferredLogKind::PlayFailure
            && play_failure_proposal.play_failure
                == BgmPlaybackPlayFailure::Route
            && play_failure_proposal.expected_route == 21
            && play_failure_proposal.current_route == 22
            && play_failure_proposal.expected_lifecycle == 23
            && play_failure_proposal.current_lifecycle == 24
            && play_failure_proposal.expected_nonce == 25
            && play_failure_proposal.nonce == 26,
        "deferred Play failure proposal did not preserve exact mutation facts");
    BgmPlaybackDeferredLogProposal play_success_proposal{};
    play_success_proposal.kind = BgmPlaybackDeferredLogKind::PlaySuccess;
    play_success_proposal.ordinal = 31;
    play_success_proposal.old_handle = 32;
    play_success_proposal.canonical_handle = 33;
    play_success_proposal.custom_handle = 34;
    play_success_proposal.set_epoch = 35;
    play_success_proposal.play_epoch = 36;
    play_success_proposal.play_owner_rebound = true;
    play_success_proposal.mutation_complete = true;
    require(play_success_proposal.kind == BgmPlaybackDeferredLogKind::PlaySuccess
            && play_success_proposal.ordinal == 31
            && play_success_proposal.old_handle == 32
            && play_success_proposal.canonical_handle == 33
            && play_success_proposal.custom_handle == 34
            && play_success_proposal.set_epoch == 35
            && play_success_proposal.play_epoch == 36
            && play_success_proposal.play_owner_rebound,
        "deferred Play success proposal did not preserve exact mutation facts");
    BgmPlaybackPreparationLogProposal preparation_proposal{};
    preparation_proposal.entries[0].kind =
        BgmPlaybackPreparationLogKind::StalePlay;
    preparation_proposal.entries[0].ordinal = 41;
    preparation_proposal.entries[0].version = 42;
    preparation_proposal.entries[1].kind =
        BgmPlaybackPreparationLogKind::LineageInherited;
    preparation_proposal.entries[1].parent_ordinal = 43;
    preparation_proposal.entries[1].child_ordinal = 44;
    preparation_proposal.entries[2].kind =
        BgmPlaybackPreparationLogKind::SetPrepared;
    preparation_proposal.entries[2].requested_sound =
        reinterpret_cast<void*>(0x45000);
    preparation_proposal.count = 3;
    require(!bgm_playback_preparation_log_may_emit(preparation_proposal,
                BgmPlaybackPreparationEmissionPhase::Capturing, false),
        "Set preparation proposal emitted while borrower authority was held");
    preparation_proposal.mutation_complete = true;
    require(!bgm_playback_preparation_log_may_emit(preparation_proposal,
                BgmPlaybackPreparationEmissionPhase::Capturing, true)
            && !bgm_playback_preparation_log_may_emit(preparation_proposal,
                BgmPlaybackPreparationEmissionPhase::BorrowerAuthorityReleased,
                false)
            && bgm_playback_preparation_log_may_emit(preparation_proposal,
                BgmPlaybackPreparationEmissionPhase::BorrowerAuthorityReleased,
                true)
            && preparation_proposal.entries[0].ordinal == 41
            && preparation_proposal.entries[0].version == 42
            && preparation_proposal.entries[1].parent_ordinal == 43
            && preparation_proposal.entries[1].child_ordinal == 44
            && preparation_proposal.entries[2].requested_sound
                == reinterpret_cast<void*>(0x45000),
        "Set preparation proposal lost stale, lineage, or prepared facts");
    BgmAggregateOwnerPatchLogProposal owner_patch_proposal{};
    owner_patch_proposal.status =
        BgmAggregateOwnerPatchLogStatus::AuthorizationBlocked;
    owner_patch_proposal.play = true;
    owner_patch_proposal.ordinal = 51;
    owner_patch_proposal.version = 52;
    owner_patch_proposal.predecessor = 53;
    owner_patch_proposal.route_generation = 54;
    owner_patch_proposal.lifecycle_epoch = 55;
    owner_patch_proposal.sound_index = 56;
    owner_patch_proposal.sound_serial = 57;
    owner_patch_proposal.exact_record = true;
    owner_patch_proposal.lifecycle_available = true;
    owner_patch_proposal.owner_binding_exact = true;
    owner_patch_proposal.owner_command_exact = false;
    require(!bgm_aggregate_owner_patch_log_may_emit(
                owner_patch_proposal, false),
        "owner-patch authorization marker emitted before aggregate release");
    owner_patch_proposal.mutation_complete = true;
    require(!bgm_aggregate_owner_patch_log_may_emit(
                owner_patch_proposal, false)
            && bgm_aggregate_owner_patch_log_may_emit(
                owner_patch_proposal, true)
            && owner_patch_proposal.ordinal == 51
            && owner_patch_proposal.version == 52
            && owner_patch_proposal.predecessor == 53
            && owner_patch_proposal.route_generation == 54
            && owner_patch_proposal.lifecycle_epoch == 55
            && owner_patch_proposal.sound_index == 56
            && owner_patch_proposal.sound_serial == 57,
        "owner-patch authorization proposal lost exact authority facts");
    owner_patch_proposal.status = BgmAggregateOwnerPatchLogStatus::PatchFailed;
    owner_patch_proposal.patch_planned = true;
    owner_patch_proposal.rollback_armed = true;
    owner_patch_proposal.gate_open = false;
    owner_patch_proposal.write_exact = false;
    owner_patch_proposal.rollback_cleanup_attempted = true;
    require(bgm_aggregate_owner_patch_log_may_emit(
                owner_patch_proposal, true)
            && owner_patch_proposal.patch_planned
            && owner_patch_proposal.rollback_armed
            && !owner_patch_proposal.gate_open
            && !owner_patch_proposal.write_exact
            && owner_patch_proposal.rollback_cleanup_attempted,
        "owner-patch failure proposal lost patch or rollback facts");
    BgmAggregateSetForwardLogProposal aggregate_set_forward{};
    aggregate_set_forward.valid = true;
    aggregate_set_forward.mutation_complete = true;
    aggregate_set_forward.ordinal = 61;
    aggregate_set_forward.version = 62;
    aggregate_set_forward.predecessor = 63;
    aggregate_set_forward.route_generation_before = 64;
    aggregate_set_forward.route_generation_after = 65;
    aggregate_set_forward.lease_generation = 66;
    aggregate_set_forward.song_key = 67;
    BgmAggregateSetForwardResult aggregate_set_forward_result{};
    aggregate_set_forward_result.log = aggregate_set_forward;
    require(!bgm_aggregate_set_forward_log_may_emit(
                aggregate_set_forward_result),
        "aggregate Set-forward marker emitted without a completed route advance");
    aggregate_set_forward_result.route_advanced = true;
    require(bgm_aggregate_set_forward_log_may_emit(
                aggregate_set_forward_result)
            && aggregate_set_forward.ordinal == 61
            && aggregate_set_forward.version == 62
            && aggregate_set_forward.predecessor == 63
            && aggregate_set_forward.route_generation_before == 64
            && aggregate_set_forward.route_generation_after == 65
            && aggregate_set_forward.lease_generation == 66
            && aggregate_set_forward.song_key == 67,
        "aggregate Set-forward proposal lost route mutation facts or emitted under audio state authority");
    aggregate_set_forward_result.log.route_generation_after = 66;
    require(!bgm_aggregate_set_forward_log_may_emit(
                aggregate_set_forward_result),
        "aggregate Set-forward marker accepted a skipped route generation");
    require(!cleanup_only_record_requires_exclusive_observation(
                false, true, false, false)
            && cleanup_only_record_requires_exclusive_observation(
                true, false, false, false)
            && !cleanup_only_record_requires_exclusive_observation(
                true, false, true, false)
            && !cleanup_only_record_requires_exclusive_observation(
                true, false, false, true),
        "cleanup observation presence policy acquired exclusive authority without active cleanup");
    size_t cleanup_exclusive_attempts = 0;
    for (size_t i = 0; i < 32; ++i) {
        if (cleanup_only_record_requires_exclusive_observation(
                false, true, false, false)) {
            ++cleanup_exclusive_attempts;
        }
    }
    require(cleanup_exclusive_attempts == 0,
        "repeated no-record callbacks attempted cleanup exclusive observation");
    require(cleanup_only_observation_generation_exact(71, true, 71)
            && !cleanup_only_observation_generation_exact(71, false, 71)
            && !cleanup_only_observation_generation_exact(71, true, 72)
            && !cleanup_only_observation_generation_exact(0, true, 0),
        "cleanup exclusive recheck accepted vanished, stale, or zero generation");
    CleanupOnlyObservationFingerprint cleanup_observation{};
    cleanup_observation.valid = true;
    cleanup_observation.generation = 71;
    cleanup_observation.phase = 1;
    cleanup_observation.fact_mask = 0x11;
    const auto first_cleanup_observation =
        cleanup_only_observation_emission_decision(
            {}, cleanup_observation, 32, 32);
    require(first_cleanup_observation.emit
            && first_cleanup_observation.reset_fact_budget
            && !cleanup_only_observation_emission_decision(
                    cleanup_observation, cleanup_observation, 0, 32).emit,
        "cleanup observation budget lost first evidence or repeated identical evidence");
    const auto require_cleanup_change = [&](auto mutate, const char* message) {
        auto changed = cleanup_observation;
        mutate(changed);
        require(cleanup_only_observation_emission_decision(
                cleanup_observation, changed, 32, 32).emit, message);
    };
    require_cleanup_change([](auto& f) { ++f.generation; },
        "cleanup observation generation change did not reset evidence budget");
    require_cleanup_change([](auto& f) { ++f.phase; },
        "cleanup observation phase change was suppressed");
    require_cleanup_change([](auto& f) { ++f.failure; },
        "cleanup observation failure change was suppressed");
    require_cleanup_change([](auto& f) { f.release_eligible = true; },
        "cleanup observation eligibility change was suppressed");
    require_cleanup_change([](auto& f) { f.release_claimed = true; },
        "cleanup observation release claim was suppressed");
    require_cleanup_change([](auto& f) { ++f.release_outcome; },
        "cleanup observation release outcome was suppressed");
    auto cleanup_fact_change = cleanup_observation;
    cleanup_fact_change.fact_mask ^= 0x40;
    const auto allowed_fact_change = cleanup_only_observation_emission_decision(
        cleanup_observation, cleanup_fact_change, 0, 1);
    require(allowed_fact_change.emit && allowed_fact_change.spend_fact_budget
            && !cleanup_only_observation_emission_decision(
                    cleanup_observation, cleanup_fact_change, 1, 1).emit,
        "cleanup observation fact changes were not bounded per generation");
    require(committed_custom_play_setup_may_forward_unmodified(false, false)
            && !committed_custom_play_setup_may_forward_unmodified(true, false)
            && !committed_custom_play_setup_may_forward_unmodified(false, true),
        "committed or quarantined custom PlaySetup could forward vanilla");
    require(!custom_activation_quarantine_callback_suppressed(
                false, true, true, true, true)
            && !custom_activation_quarantine_callback_suppressed(
                true, false, true, true, true)
            && custom_activation_quarantine_callback_suppressed(
                true, true, true, true, true)
            && custom_activation_quarantine_callback_suppressed(
                true, true, true, false, false)
            && custom_activation_quarantine_callback_suppressed(
                true, true, false, false, false)
            && !custom_activation_quarantine_callback_suppressed(
                true, true, true, true, false),
        "quarantine callback policy did not isolate exact/ambiguous duplicate callbacks");
    CustomActivationQuarantineRecord pre_original_quarantine{};
    pre_original_quarantine.active = true;
    pre_original_quarantine.generation = 1;
    pre_original_quarantine.failure =
        CustomActivationQuarantineFailure::BankQualificationFailed;
    pre_original_quarantine.selection_generation = 2;
    pre_original_quarantine.route_generation = 3;
    pre_original_quarantine.lease_generation = 4;
    pre_original_quarantine.song_key = 5;
    pre_original_quarantine.original_forwarded = false;
    pre_original_quarantine.token_state_none = true;
    require(static_cast<bool>(pre_original_quarantine),
        "exact pre-original no-token quarantine was invalid");
    require(!custom_activation_quarantine_clear_event(
                CustomActivationQuarantineClearReason::None)
            && custom_activation_quarantine_clear_event(
                CustomActivationQuarantineClearReason::ListReturn)
            && custom_activation_quarantine_clear_event(
                CustomActivationQuarantineClearReason::TitleOrReselection)
            && custom_activation_quarantine_clear_event(
                CustomActivationQuarantineClearReason::Shutdown),
        "quarantine clear accepted a non-event or rejected an explicit lifecycle event");
    const CustomActivationQuarantineFailure pre_original_failures[] = {
        CustomActivationQuarantineFailure::ArmMissing,
        CustomActivationQuarantineFailure::SidecarMissing,
        CustomActivationQuarantineFailure::SoundIdentityInvalid,
        CustomActivationQuarantineFailure::BankQualificationFailed,
        CustomActivationQuarantineFailure::PendingRestoreFailed,
        CustomActivationQuarantineFailure::PostRestoreAuthorityFailed,
        CustomActivationQuarantineFailure::PatchFailed,
        CustomActivationQuarantineFailure::RouteGenerationFailed,
        CustomActivationQuarantineFailure::SetupTokenAdvanceFailed,
        CustomActivationQuarantineFailure::DuplicateCallback,
    };
    for (const auto failure : pre_original_failures) {
        auto quarantined = pre_original_quarantine;
        quarantined.failure = failure;
        require(static_cast<bool>(quarantined)
                && !quarantined.original_forwarded
                && quarantined.token_state_none,
            "pre-original committed-custom failure did not remain no-token quarantine");
    }
    const CustomActivationQuarantineClearFacts exact_quarantine_clear{
        true, true, true, true, true, true, true, true, true, true,
        true, true, true, true, true, true, true, true, true, true,
        true, true, true, true, true, true, true};
    require(first_custom_activation_quarantine_clear_failure(
                exact_quarantine_clear)
            == CustomActivationQuarantineClearFailure::None,
        "fully quiescent no-token quarantine did not clear");
    const std::array<bool CustomActivationQuarantineClearFacts::*, 27>
        quarantine_clear_members{
            &CustomActivationQuarantineClearFacts::record_exact,
            &CustomActivationQuarantineClearFacts::explicit_lifecycle_event,
            &CustomActivationQuarantineClearFacts::original_not_forwarded,
            &CustomActivationQuarantineClearFacts::token_state_none,
            &CustomActivationQuarantineClearFacts::unpublished_setup_absent,
            &CustomActivationQuarantineClearFacts::route_idle,
            &CustomActivationQuarantineClearFacts::route_disabled,
            &CustomActivationQuarantineClearFacts::route_identity_empty,
            &CustomActivationQuarantineClearFacts::custom_resource_absent,
            &CustomActivationQuarantineClearFacts::request_absent,
            &CustomActivationQuarantineClearFacts::pending_patch_empty,
            &CustomActivationQuarantineClearFacts::frozen_patch_empty,
            &CustomActivationQuarantineClearFacts::failed_patch_empty,
            &CustomActivationQuarantineClearFacts::active_journal_empty,
            &CustomActivationQuarantineClearFacts::frozen_lease_inactive,
            &CustomActivationQuarantineClearFacts::playback_absent,
            &CustomActivationQuarantineClearFacts::cleanup_lease_absent,
            &CustomActivationQuarantineClearFacts::normal_token_ownership_absent,
            &CustomActivationQuarantineClearFacts::cleanup_only_ownership_absent,
            &CustomActivationQuarantineClearFacts::aggregate_borrowers_absent,
            &CustomActivationQuarantineClearFacts::substrate_bridge_absent,
            &CustomActivationQuarantineClearFacts::selection_reservation_absent,
            &CustomActivationQuarantineClearFacts::list_cleanup_clear,
            &CustomActivationQuarantineClearFacts::retirement_authority_absent,
            &CustomActivationQuarantineClearFacts::deferred_handoff_absent,
            &CustomActivationQuarantineClearFacts::pause_resume_absent,
            &CustomActivationQuarantineClearFacts::aggregate_cleanup_absent,
        };
    const std::array<CustomActivationQuarantineClearFailure, 27>
        quarantine_clear_failures{
            CustomActivationQuarantineClearFailure::RecordMismatch,
            CustomActivationQuarantineClearFailure::NoExplicitLifecycleEvent,
            CustomActivationQuarantineClearFailure::OriginalForwarded,
            CustomActivationQuarantineClearFailure::TokenStatePresent,
            CustomActivationQuarantineClearFailure::UnpublishedSetupActive,
            CustomActivationQuarantineClearFailure::RouteNotIdle,
            CustomActivationQuarantineClearFailure::RouteNotDisabled,
            CustomActivationQuarantineClearFailure::RouteIdentityActive,
            CustomActivationQuarantineClearFailure::CustomResourceOwned,
            CustomActivationQuarantineClearFailure::RequestActive,
            CustomActivationQuarantineClearFailure::PendingPatchActive,
            CustomActivationQuarantineClearFailure::FrozenPatchActive,
            CustomActivationQuarantineClearFailure::FailedPatchActive,
            CustomActivationQuarantineClearFailure::ActiveJournal,
            CustomActivationQuarantineClearFailure::FrozenLeaseActive,
            CustomActivationQuarantineClearFailure::PlaybackActive,
            CustomActivationQuarantineClearFailure::CleanupLeaseActive,
            CustomActivationQuarantineClearFailure::NormalTokenOwnershipActive,
            CustomActivationQuarantineClearFailure::CleanupOnlyOwnershipActive,
            CustomActivationQuarantineClearFailure::AggregateBorrowerActive,
            CustomActivationQuarantineClearFailure::SubstrateBridgeActive,
            CustomActivationQuarantineClearFailure::SelectionReservationActive,
            CustomActivationQuarantineClearFailure::ListCleanupPending,
            CustomActivationQuarantineClearFailure::RetirementAuthorityActive,
            CustomActivationQuarantineClearFailure::DeferredHandoffActive,
            CustomActivationQuarantineClearFailure::PauseResumeActive,
            CustomActivationQuarantineClearFailure::AggregateCleanupActive,
        };
    for (size_t index = 0; index < quarantine_clear_members.size(); ++index) {
        auto rejected = exact_quarantine_clear;
        rejected.*quarantine_clear_members[index] = false;
        require(first_custom_activation_quarantine_clear_failure(rejected)
                == quarantine_clear_failures[index],
            "quarantine clear did not reject its first active owner");
    }
    const CleanupOnlyCallbackQuiescenceFacts exact_callback_quiescence{
        true, true, true, true, true};
    require(cleanup_only_callback_quiescent(exact_callback_quiescence),
        "exclusive post-callback quiescence was rejected");
    const std::array<bool CleanupOnlyCallbackQuiescenceFacts::*, 5>
        callback_quiescence_members{
            &CleanupOnlyCallbackQuiescenceFacts::outermost_body_completed,
            &CleanupOnlyCallbackQuiescenceFacts::lifecycle_lease_released,
            &CleanupOnlyCallbackQuiescenceFacts::shared_gate_released,
            &CleanupOnlyCallbackQuiescenceFacts::exclusive_gate_acquired,
            &CleanupOnlyCallbackQuiescenceFacts::native_and_mod_facts_reobserved,
        };
    for (const auto member : callback_quiescence_members) {
        auto rejected = exact_callback_quiescence;
        rejected.*member = false;
        require(!cleanup_only_callback_quiescent(rejected),
            "stale or nonexclusive callback facts authorized cleanup");
    }
    const CleanupOnlyRouteQuiescenceFacts exact_route_quiescence{
        true, true, true, true, true, true, true, true, true, true,
        true, true, true, true, true, true, true, true, true};
    require(cleanup_only_route_quiescent(exact_route_quiescence)
            && first_cleanup_only_route_quiescence_failure(exact_route_quiescence)
                == CleanupOnlyRouteQuiescenceFailure::None,
        "fully empty cleanup route was not quiescent");
    const std::array<bool CleanupOnlyRouteQuiescenceFacts::*, 19>
        route_quiescence_members{
            &CleanupOnlyRouteQuiescenceFacts::cleanup_generation_exact,
            &CleanupOnlyRouteQuiescenceFacts::unpublished_setup_absent,
            &CleanupOnlyRouteQuiescenceFacts::substrate_bridge_absent,
            &CleanupOnlyRouteQuiescenceFacts::selection_reservation_absent,
            &CleanupOnlyRouteQuiescenceFacts::route_idle,
            &CleanupOnlyRouteQuiescenceFacts::route_disabled,
            &CleanupOnlyRouteQuiescenceFacts::route_identity_empty,
            &CleanupOnlyRouteQuiescenceFacts::list_cleanup_clear,
            &CleanupOnlyRouteQuiescenceFacts::pending_patch_empty,
            &CleanupOnlyRouteQuiescenceFacts::frozen_patch_empty,
            &CleanupOnlyRouteQuiescenceFacts::failed_patch_empty,
            &CleanupOnlyRouteQuiescenceFacts::active_journal_empty,
            &CleanupOnlyRouteQuiescenceFacts::stop_retirement_absent,
            &CleanupOnlyRouteQuiescenceFacts::deferred_handoff_absent,
            &CleanupOnlyRouteQuiescenceFacts::frozen_lease_inactive,
            &CleanupOnlyRouteQuiescenceFacts::playback_absent,
            &CleanupOnlyRouteQuiescenceFacts::aggregate_borrowers_absent,
            &CleanupOnlyRouteQuiescenceFacts::pause_resume_absent,
            &CleanupOnlyRouteQuiescenceFacts::aggregate_cleanup_absent,
        };
    const std::array<CleanupOnlyRouteQuiescenceFailure, 19>
        route_quiescence_failures{
            CleanupOnlyRouteQuiescenceFailure::CleanupGenerationDrift,
            CleanupOnlyRouteQuiescenceFailure::UnpublishedSetupActive,
            CleanupOnlyRouteQuiescenceFailure::SubstrateBridgeActive,
            CleanupOnlyRouteQuiescenceFailure::SelectionReservationActive,
            CleanupOnlyRouteQuiescenceFailure::RouteNotIdle,
            CleanupOnlyRouteQuiescenceFailure::RouteNotDisabled,
            CleanupOnlyRouteQuiescenceFailure::RouteIdentityActive,
            CleanupOnlyRouteQuiescenceFailure::ListCleanupPending,
            CleanupOnlyRouteQuiescenceFailure::PendingPatchActive,
            CleanupOnlyRouteQuiescenceFailure::FrozenPatchActive,
            CleanupOnlyRouteQuiescenceFailure::FailedPatchRetained,
            CleanupOnlyRouteQuiescenceFailure::ActiveJournal,
            CleanupOnlyRouteQuiescenceFailure::StopRetirementActive,
            CleanupOnlyRouteQuiescenceFailure::DeferredHandoffActive,
            CleanupOnlyRouteQuiescenceFailure::FrozenLeaseActive,
            CleanupOnlyRouteQuiescenceFailure::PlaybackPublished,
            CleanupOnlyRouteQuiescenceFailure::AggregateBorrowerActive,
            CleanupOnlyRouteQuiescenceFailure::PauseResumeActive,
            CleanupOnlyRouteQuiescenceFailure::AggregateCleanupActive,
        };
    for (size_t index = 0; index < route_quiescence_members.size(); ++index) {
        auto rejected = exact_route_quiescence;
        rejected.*route_quiescence_members[index] = false;
        require(!cleanup_only_route_quiescent(rejected)
                && first_cleanup_only_route_quiescence_failure(rejected)
                    == route_quiescence_failures[index],
            "cleanup route quiescence did not reject its first active owner");
    }
    const CleanupOnlyPostOriginalOwnershipFacts ordinary_ownership{};
    require(classify_cleanup_only_post_original_ownership(ordinary_ownership)
                == CleanupOnlyPostOriginalOwnership::None,
        "ordinary PlaySetup acquired cleanup-only ownership");
    auto cleanup_ownership = ordinary_ownership;
    cleanup_ownership.deferred_bridge_original_entered = true;
    require(classify_cleanup_only_post_original_ownership(cleanup_ownership)
                == CleanupOnlyPostOriginalOwnership::FailedRetained,
        "failed post-original evidence was not quarantined");
    cleanup_ownership.playback_authority_absent = true;
    cleanup_ownership.sound_identity_exact = true;
    cleanup_ownership.distinct_type1_tokens = true;
    cleanup_ownership.both_kinds2 = true;
    cleanup_ownership.owner_restored = true;
    require(classify_cleanup_only_post_original_ownership(cleanup_ownership)
                == CleanupOnlyPostOriginalOwnership::CleanupOnly,
        "exact late publication failure did not select cleanup-only ownership");
    cleanup_ownership.normal_detached_retained = true;
    require(classify_cleanup_only_post_original_ownership(cleanup_ownership)
                == CleanupOnlyPostOriginalOwnership::NormalDetached,
        "successful publication did not retain only normal ownership");

    const DecodedOnMemoryBankToken cleanup_custom{1, 0x31b, 0x31c};
    OnMemoryBankCleanupOnlyRecord cleanup_record{};
    cleanup_record.phase = OnMemoryBankCleanupOnlyPhase::CleanupOnlyRestoreApplied;
    cleanup_record.generation = 41;
    cleanup_record.bridge_generation = 51;
    cleanup_record.sound = sound;
    cleanup_record.canonical = canonical;
    cleanup_record.custom = cleanup_custom;
    cleanup_record.raw_custom_token = cleanup_custom.encode();
    cleanup_record.selection_generation = 61;
    cleanup_record.route_generation = 71;
    cleanup_record.cleanup_generation = 81;
    cleanup_record.song_key = 91;
    cleanup_record.list_exit_epoch = 101;
    cleanup_record.release_completion_epoch = 111;
    cleanup_record.revocation_epoch = 121;
    cleanup_record.controller = reinterpret_cast<void*>(0x20000);
    cleanup_record.slot = reinterpret_cast<void*>(0x21000);
    cleanup_record.bgm = reinterpret_cast<void*>(0x22000);
    cleanup_record.owner_restore_verified = true;
    OnMemoryBankCleanupOnlyReleaseFacts cleanup_facts{};
    cleanup_facts.generation = cleanup_record.generation;
    cleanup_facts.provenance_exact = true;
    cleanup_facts.identity_exact = true;
    cleanup_facts.owner_canonical = true;
    cleanup_facts.callback_quiescence_exact = true;
    cleanup_facts.route_and_setup_absent = true;
    cleanup_facts.route_quiescence_exact = true;
    cleanup_facts.aggregate_and_pause_absent = true;
    cleanup_facts.request_zero = true;
    cleanup_facts.backing_read_succeeded = true;
    cleanup_facts.backing_null = true;
    cleanup_facts.backing_detached = true;
    cleanup_facts.sound_inactive = true;
    cleanup_facts.lookup_signature_valid = true;
    cleanup_facts.release_signature_valid = true;
    cleanup_facts.runtime_enabled = true;
    cleanup_facts.canonical_kind = 2;
    cleanup_facts.custom_kind = 2;
    OnMemoryBankCleanupOnlyState cleanup_state;
    require(cleanup_state.retain(cleanup_record)
            && cleanup_state.blocks_custom_routes(),
        "cleanup-only record was not retained as blocking authority");
    OnMemoryBankReleaseAction cleanup_action;
    auto blocked_cleanup = cleanup_facts;
    blocked_cleanup.request_zero = false;
    require(!cleanup_state.claim_release(blocked_cleanup, cleanup_action)
            && !cleanup_action
            && cleanup_state.record().release_call_count == 0,
        "cleanup-only active request authorized release");
    blocked_cleanup = cleanup_facts;
    blocked_cleanup.backing_null = false;
    blocked_cleanup.backing_detached = false;
    require(!cleanup_state.claim_release(blocked_cleanup, cleanup_action)
            && cleanup_state.record().release_call_count == 0,
        "cleanup-only arbitrary nonnull backing authorized release");
    blocked_cleanup = cleanup_facts;
    blocked_cleanup.backing_read_succeeded = false;
    blocked_cleanup.backing_null = false;
    blocked_cleanup.backing_detached = false;
    require(!cleanup_state.claim_release(blocked_cleanup, cleanup_action)
            && cleanup_state.record().release_call_count == 0,
        "cleanup-only unreadable backing authorized release");
    blocked_cleanup = cleanup_facts;
    blocked_cleanup.sound_inactive = false;
    require(!cleanup_state.claim_release(blocked_cleanup, cleanup_action)
            && cleanup_state.record().release_call_count == 0,
        "cleanup-only active sound authorized release");
    require(cleanup_state.claim_release(cleanup_facts, cleanup_action)
            && cleanup_action
            && cleanup_state.record().phase
                == OnMemoryBankCleanupOnlyPhase::ReleaseInFlight
            && cleanup_state.record().release_call_count == 1,
        "cleanup-only exact release was not claimed once");
    OnMemoryBankReleaseAction duplicate_cleanup_action;
    require(!cleanup_state.claim_release(cleanup_facts, duplicate_cleanup_action)
            && !duplicate_cleanup_action
            && cleanup_state.record().release_call_count == 1,
        "cleanup-only duplicate release was claimed");
    require(cleanup_state.observe(cleanup_record.generation, 2, 2)
            && cleanup_state.record().phase
                == OnMemoryBankCleanupOnlyPhase::ReleaseInFlight
            && cleanup_state.observe(cleanup_record.generation, 2, 0)
            && cleanup_state.record().phase
                == OnMemoryBankCleanupOnlyPhase::Complete
            && !cleanup_state.blocks_custom_routes(),
        "cleanup-only asynchronous completion was not event-driven");

    OnMemoryBankCleanupOnlyState absent_cleanup_state;
    require(absent_cleanup_state.retain(cleanup_record),
        "cleanup-only already-absent fixture was not retained");
    auto absent_facts = cleanup_facts;
    absent_facts.custom_kind = 0;
    OnMemoryBankReleaseAction cleanup_absent_action;
    require(absent_cleanup_state.claim_release(absent_facts, cleanup_absent_action)
            && !cleanup_absent_action
            && absent_cleanup_state.record().phase
                == OnMemoryBankCleanupOnlyPhase::Complete
            && absent_cleanup_state.record().release_call_count == 0,
        "cleanup-only already-absent token submitted a release");

    OnMemoryBankCleanupOnlyState failed_cleanup_state;
    auto failed_cleanup_record = cleanup_record;
    failed_cleanup_record.phase = OnMemoryBankCleanupOnlyPhase::FailedRetained;
    failed_cleanup_record.custom = {};
    failed_cleanup_record.raw_custom_token = 0xdeadbeef;
    require(failed_cleanup_state.retain_failed(failed_cleanup_record,
                OnMemoryBankCleanupOnlyFailure::CustomTokenInvalid)
            && failed_cleanup_state.blocks_custom_routes(),
        "cleanup-only undecodable token was not quarantined");

    OnMemoryBankCleanupOnlyState alias_cleanup_state;
    auto alias_record = cleanup_record;
    alias_record.custom = canonical;
    alias_record.raw_custom_token = canonical.encode();
    require(!alias_cleanup_state.retain(alias_record),
        "cleanup-only canonical/custom token alias was retained");

    OnMemoryBankLifecycleState state;
    int arm_lookup_calls = 0;
    const auto arm_preflight = [&](bool build, bool lookup, bool release) {
        const OnMemoryBankRouteDecision decision =
            state.preflight_arm(true, build, lookup, release);
        // Arm preflight deliberately has no token and no native-query callback.
        return decision;
    };
    require(arm_sound != sound
            && arm_preflight(true, true, true)
                == OnMemoryBankRouteDecision::Allowed
            && !state.canonical() && arm_lookup_calls == 0,
        "arm bound the pre-Stop sound or performed native kind lookup");
    require(arm_preflight(false, true, true)
                == OnMemoryBankRouteDecision::UnsupportedBuild
            && arm_preflight(true, false, true)
                == OnMemoryBankRouteDecision::LookupSignatureUnavailable
            && arm_preflight(true, true, false)
                == OnMemoryBankRouteDecision::ReleaseSignatureUnavailable
            && !state.canonical() && arm_lookup_calls == 0,
        "arm callable preflight mutated lifecycle or queried a sound token");

    const PlaySetupQualificationRevalidation accepted_revalidation{
        true, true, true, true, true, true, true, true, true, true};
    const PlaySetupQualificationEntryFacts accepted_entry{
        true, true, true, true, true, true, true, true};
    bool PlaySetupQualificationEntryFacts::* const entry_facts[] = {
        &PlaySetupQualificationEntryFacts::sound_identity_read_succeeded,
        &PlaySetupQualificationEntryFacts::sound_live_capture_succeeded,
        &PlaySetupQualificationEntryFacts::sound_index_valid,
        &PlaySetupQualificationEntryFacts::sound_serial_valid,
        &PlaySetupQualificationEntryFacts::owner_read_succeeded,
        &PlaySetupQualificationEntryFacts::route_matches,
        &PlaySetupQualificationEntryFacts::setup_matches,
        &PlaySetupQualificationEntryFacts::sidecar_matches,
    };
    const PlaySetupQualificationEntryFailure entry_failures[] = {
        PlaySetupQualificationEntryFailure::SoundIdentityReadFailed,
        PlaySetupQualificationEntryFailure::SoundLiveCaptureFailed,
        PlaySetupQualificationEntryFailure::SoundIndexInvalid,
        PlaySetupQualificationEntryFailure::SoundSerialInvalid,
        PlaySetupQualificationEntryFailure::OwnerFieldUnreadable,
        PlaySetupQualificationEntryFailure::RouteMismatch,
        PlaySetupQualificationEntryFailure::SetupMismatch,
        PlaySetupQualificationEntryFailure::SidecarMismatch,
    };
    require(first_play_setup_qualification_entry_failure(accepted_entry)
            == PlaySetupQualificationEntryFailure::None,
        "accepted PlaySetup entry reported a failure leaf");
    for (size_t index = 0; index < std::size(entry_facts); ++index) {
        PlaySetupQualificationEntryFacts rejected_entry = accepted_entry;
        rejected_entry.*entry_facts[index] = false;
        require(first_play_setup_qualification_entry_failure(rejected_entry)
                == entry_failures[index],
            "PlaySetup entry first-failure ordering changed");
    }
    bool PlaySetupQualificationRevalidation::* const drift_facts[] = {
        &PlaySetupQualificationRevalidation::sound_identity_unchanged,
        &PlaySetupQualificationRevalidation::owner_unchanged,
        &PlaySetupQualificationRevalidation::registry_generation_unchanged,
        &PlaySetupQualificationRevalidation::route_generation_unchanged,
        &PlaySetupQualificationRevalidation::lease_unchanged,
        &PlaySetupQualificationRevalidation::song_unchanged,
        &PlaySetupQualificationRevalidation::controller_unchanged,
        &PlaySetupQualificationRevalidation::phase_armed,
        &PlaySetupQualificationRevalidation::setup_token_unchanged,
        &PlaySetupQualificationRevalidation::sidecar_unchanged,
    };
    const PlaySetupQualificationRevalidationFailure drift_failures[] = {
        PlaySetupQualificationRevalidationFailure::SoundIdentityChanged,
        PlaySetupQualificationRevalidationFailure::OwnerChanged,
        PlaySetupQualificationRevalidationFailure::RegistryGenerationChanged,
        PlaySetupQualificationRevalidationFailure::RouteGenerationChanged,
        PlaySetupQualificationRevalidationFailure::LeaseChanged,
        PlaySetupQualificationRevalidationFailure::SongChanged,
        PlaySetupQualificationRevalidationFailure::ControllerChanged,
        PlaySetupQualificationRevalidationFailure::PhaseNotArmed,
        PlaySetupQualificationRevalidationFailure::SetupTokenChanged,
        PlaySetupQualificationRevalidationFailure::SidecarChanged,
    };
    require(play_setup_qualification_revalidated(accepted_revalidation),
        "exact PlaySetup snapshot/query/revalidate facts were rejected");
    require(first_play_setup_qualification_revalidation_failure(
                accepted_revalidation)
            == PlaySetupQualificationRevalidationFailure::None,
        "accepted PlaySetup revalidation reported a failure leaf");
    for (size_t index = 0; index < std::size(drift_facts); ++index) {
        PlaySetupQualificationRevalidation drifted = accepted_revalidation;
        drifted.*drift_facts[index] = false;
        require(!play_setup_qualification_revalidated(drifted)
                && first_play_setup_qualification_revalidation_failure(drifted)
                    == drift_failures[index],
            "PlaySetup drift reached field writes");
    }
    OnMemoryBankCanonicalRebaseFacts rebase_facts{
        true, true, true, true, true, true,
        true, true, true, true, true, true};
    require(onmemory_bank_canonical_rebase_allowed(rebase_facts),
        "exact completed-lifecycle canonical rebase facts were rejected");
    bool OnMemoryBankCanonicalRebaseFacts::* const rebase_fact_members[] = {
        &OnMemoryBankCanonicalRebaseFacts::lifecycle_healthy,
        &OnMemoryBankCanonicalRebaseFacts::no_release_in_flight,
        &OnMemoryBankCanonicalRebaseFacts::no_active_pending,
        &OnMemoryBankCanonicalRebaseFacts::canonical_ready,
        &OnMemoryBankCanonicalRebaseFacts::same_sound_identity,
        &OnMemoryBankCanonicalRebaseFacts::candidate_type1,
        &OnMemoryBankCanonicalRebaseFacts::candidate_differs_stale_canonical,
        &OnMemoryBankCanonicalRebaseFacts::completed_custom_absence,
        &OnMemoryBankCanonicalRebaseFacts::completed_sound_matches,
        &OnMemoryBankCanonicalRebaseFacts::completed_canonical_matches,
        &OnMemoryBankCanonicalRebaseFacts::prior_custom_type1,
        &OnMemoryBankCanonicalRebaseFacts::candidate_differs_prior_custom,
    };
    const OnMemoryBankCanonicalRebaseFailure rebase_failures[] = {
        OnMemoryBankCanonicalRebaseFailure::LifecycleUnhealthy,
        OnMemoryBankCanonicalRebaseFailure::ReleaseInFlight,
        OnMemoryBankCanonicalRebaseFailure::ActivePending,
        OnMemoryBankCanonicalRebaseFailure::CanonicalNotReady,
        OnMemoryBankCanonicalRebaseFailure::SoundIdentityMismatch,
        OnMemoryBankCanonicalRebaseFailure::CandidateNotType1,
        OnMemoryBankCanonicalRebaseFailure::CandidateMatchesStaleCanonical,
        OnMemoryBankCanonicalRebaseFailure::CompletedCustomNotAbsent,
        OnMemoryBankCanonicalRebaseFailure::CompletedSoundMismatch,
        OnMemoryBankCanonicalRebaseFailure::CompletedCanonicalMismatch,
        OnMemoryBankCanonicalRebaseFailure::PriorCustomNotType1,
        OnMemoryBankCanonicalRebaseFailure::CandidateMatchesPriorCustom,
    };
    require(first_onmemory_bank_canonical_rebase_failure(rebase_facts)
            == OnMemoryBankCanonicalRebaseFailure::None,
        "accepted canonical rebase reported a failure leaf");
    for (size_t index = 0; index < std::size(rebase_fact_members); ++index) {
        OnMemoryBankCanonicalRebaseFacts rejected_rebase = rebase_facts;
        rejected_rebase.*rebase_fact_members[index] = false;
        require(!onmemory_bank_canonical_rebase_allowed(rejected_rebase)
                && first_onmemory_bank_canonical_rebase_failure(rejected_rebase)
                    == rebase_failures[index],
            "incomplete canonical rebase proof was admitted");
    }

    OnMemoryBankLifecycleState rejected;
    const auto zero_owner = rejected.snapshot_play_setup(
        true, true, true, true, sound, 0, 1);
    const auto malformed_owner = rejected.snapshot_play_setup(
        true, true, true, true, sound, 0x21c021b0101ULL, 1);
    const auto wrong_type_owner = rejected.snapshot_play_setup(
        true, true, true, true, sound,
        DecodedOnMemoryBankToken{2, 0x21b, 0x21c}.encode(), 1);
    const auto deferred_owner = rejected.snapshot_play_setup(
        true, true, true, true, sound, 0, 1, canonical_value);
    const auto malformed_deferred_owner = rejected.snapshot_play_setup(
        true, true, true, true, sound, 0, 1, 0x21c021b0101ULL);
    require(zero_owner.decision == OnMemoryBankRouteDecision::OwnerTokenInvalid
            && zero_owner.first_failure
                == OnMemoryBankPlaySetupPreflightFailure::OwnerTokenZero
            && !zero_owner.owner_token_decode_attempted,
        "zero first owner established a canonical bank");
    require(malformed_owner.decision == OnMemoryBankRouteDecision::OwnerTokenInvalid
            && malformed_owner.first_failure
                == OnMemoryBankPlaySetupPreflightFailure::OwnerTokenDecodeInvalid
            && malformed_owner.owner_token_decode_attempted
            && !malformed_owner.owner_token_decoded,
        "malformed first owner established a canonical bank");
    require(wrong_type_owner.decision == OnMemoryBankRouteDecision::OwnerTokenInvalid
            && wrong_type_owner.first_failure
                == OnMemoryBankPlaySetupPreflightFailure::OwnerTokenNotType1
            && wrong_type_owner.owner_token_decoded
            && !wrong_type_owner.owner_token_type1,
        "non-type1 first owner established a canonical bank");
    require(deferred_owner.decision == OnMemoryBankRouteDecision::Allowed
            && deferred_owner.snapshot.deferred_canonical_establishment
            && deferred_owner.snapshot.query_token.encode() == canonical_value,
        "exact deferred canonical token did not qualify owner-zero setup");
    require(malformed_deferred_owner.decision
                == OnMemoryBankRouteDecision::OwnerTokenInvalid
            && malformed_deferred_owner.first_failure
                == OnMemoryBankPlaySetupPreflightFailure::OwnerTokenZero,
        "malformed deferred canonical token qualified owner-zero setup");
    require(rejected.snapshot_play_setup(false, true, true, true, sound,
                canonical_value, 1).first_failure
                == OnMemoryBankPlaySetupPreflightFailure::VanillaRoute
            && rejected.snapshot_play_setup(true, false, true, true, sound,
                canonical_value, 1).first_failure
                == OnMemoryBankPlaySetupPreflightFailure::UnsupportedBuild
            && rejected.snapshot_play_setup(true, true, false, true, sound,
                canonical_value, 1).first_failure
                == OnMemoryBankPlaySetupPreflightFailure::LookupSignatureUnavailable
            && rejected.snapshot_play_setup(true, true, true, false, sound,
                canonical_value, 1).first_failure
                == OnMemoryBankPlaySetupPreflightFailure::ReleaseSignatureUnavailable
            && rejected.snapshot_play_setup(true, true, true, true, {},
                canonical_value, 1).first_failure
                == OnMemoryBankPlaySetupPreflightFailure::SoundIdentityInvalid
            && rejected.snapshot_play_setup(true, true, true, true, sound,
                canonical_value, 0).first_failure
                == OnMemoryBankPlaySetupPreflightFailure::RouteEpochInvalid,
        "ordered PlaySetup preflight leaf classification drifted");
    require(rejected.snapshot_play_setup(true, false, true, true, sound,
                canonical_value, 1).decision
            == OnMemoryBankRouteDecision::UnsupportedBuild
            && rejected.snapshot_play_setup(true, true, false, true, sound,
                canonical_value, 1).decision
            == OnMemoryBankRouteDecision::LookupSignatureUnavailable
            && rejected.snapshot_play_setup(true, true, true, false, sound,
                canonical_value, 1).decision
            == OnMemoryBankRouteDecision::ReleaseSignatureUnavailable,
        "signature/build preflight did not reject before custom mutation");
    require(rejected.snapshot_play_setup(false, false, false, false, {}, 0, 0).decision
            == OnMemoryBankRouteDecision::VanillaUnaffected,
        "vanilla route was blocked by custom lifecycle gates");
    OnMemoryBankPlaySetupPreflight wrong_kind = rejected.snapshot_play_setup(
        true, true, true, true, sound, canonical_value, 1);
    const OnMemoryBankPlaySetupCommitResult wrong_kind_result =
        rejected.commit_play_setup_qualification(
            wrong_kind.snapshot, sound, canonical_value, 1, 0);
    require(wrong_kind.allowed()
            && wrong_kind_result.decision
                == OnMemoryBankRouteDecision::CanonicalKindInvalid
            && wrong_kind_result.first_failure
                == OnMemoryBankPlaySetupCommitFailure::CanonicalKindInvalid
            && !rejected.canonical(),
        "kind0 first owner established canonical state");
    OnMemoryBankPlaySetupPreflight drift = rejected.snapshot_play_setup(
        true, true, true, true, sound, canonical_value, 2);
    require(rejected.commit_play_setup_qualification(
                drift.snapshot, {sound.object, {18, 29}}, canonical_value, 2, 2)
                == OnMemoryBankRouteDecision::SnapshotDrift
            && rejected.commit_play_setup_qualification(
                drift.snapshot, {sound.object, {17, 30}}, canonical_value, 2, 2)
                == OnMemoryBankRouteDecision::SnapshotDrift
            && rejected.commit_play_setup_qualification(
                drift.snapshot, sound, canonical_value, 3, 2)
                == OnMemoryBankRouteDecision::SnapshotDrift,
        "sound index/serial or route epoch drift survived revalidation");
    const auto commit_failure = [&](OnMemoryBankPlaySetupSnapshot snapshot,
                                    OnMemoryBankSoundIdentity revalidated_sound,
                                    uint64_t owner, uint64_t route, uint32_t kind) {
        return rejected.commit_play_setup_qualification(
            snapshot, revalidated_sound, owner, route, kind).first_failure;
    };
    OnMemoryBankPlaySetupSnapshot invalid_snapshot = drift.snapshot;
    invalid_snapshot.sound = {};
    require(commit_failure(invalid_snapshot, {}, canonical_value, 2, 2)
                == OnMemoryBankPlaySetupCommitFailure::SnapshotSoundInvalid,
        "invalid snapshot sound did not classify first");
    invalid_snapshot = drift.snapshot;
    invalid_snapshot.route_epoch = 0;
    require(commit_failure(invalid_snapshot, sound, canonical_value, 0, 2)
                == OnMemoryBankPlaySetupCommitFailure::SnapshotRouteEpochInvalid,
        "invalid snapshot route did not classify first");
    invalid_snapshot = drift.snapshot;
    invalid_snapshot.state_epoch = 0;
    require(commit_failure(invalid_snapshot, sound, canonical_value, 2, 2)
                == OnMemoryBankPlaySetupCommitFailure::SnapshotStateEpochInvalid,
        "invalid snapshot state epoch did not classify first");
    invalid_snapshot = drift.snapshot;
    invalid_snapshot.query_token = {2, 0x21b, 0x21c};
    require(commit_failure(invalid_snapshot, sound, canonical_value, 2, 2)
                == OnMemoryBankPlaySetupCommitFailure::SnapshotQueryTokenInvalid,
        "invalid snapshot query token did not classify first");
    invalid_snapshot = drift.snapshot;
    ++invalid_snapshot.state_epoch;
    require(commit_failure(invalid_snapshot, sound, canonical_value, 2, 2)
                == OnMemoryBankPlaySetupCommitFailure::StateEpochDrift
            && commit_failure(drift.snapshot, {sound.object, {18, 29}},
                    canonical_value, 2, 2)
                == OnMemoryBankPlaySetupCommitFailure::SoundIdentityDrift
            && commit_failure(drift.snapshot, sound, canonical_value, 3, 2)
                == OnMemoryBankPlaySetupCommitFailure::RouteEpochDrift
            && commit_failure(drift.snapshot, sound, canonical_value + 1, 2, 2)
                == OnMemoryBankPlaySetupCommitFailure::OwnerTokenDrift,
        "ordered commit snapshot-drift leaf classification changed");
    invalid_snapshot = wrong_kind.snapshot;
    invalid_snapshot.establishing_canonical = false;
    require(commit_failure(invalid_snapshot, sound, canonical_value, 1, 2)
                == OnMemoryBankPlaySetupCommitFailure::CanonicalMissing,
        "missing existing canonical did not classify first");

    OnMemoryBankLifecycleState committed_state;
    const auto establish = committed_state.snapshot_play_setup(
        true, true, true, true, sound, canonical_value, 20);
    const auto established = committed_state.commit_play_setup_qualification(
        establish.snapshot, sound, canonical_value, 20, 2);
    require(established.decision == OnMemoryBankRouteDecision::Allowed
            && established.first_failure == OnMemoryBankPlaySetupCommitFailure::None,
        "successful commit reported a failure leaf");
    const auto reuse = committed_state.snapshot_play_setup(
        true, true, true, true, sound, canonical_value, 21);
    OnMemoryBankPlaySetupSnapshot canonical_drift = reuse.snapshot;
    canonical_drift.query_token = {1, 0x21c, 0x21c};
    require(committed_state.commit_play_setup_qualification(canonical_drift,
                sound, canonical_value, 21, 2).first_failure
                == OnMemoryBankPlaySetupCommitFailure::CanonicalTokenDrift,
        "canonical token drift did not classify first");
    canonical_drift = reuse.snapshot;
    ++canonical_drift.canonical_epoch;
    require(committed_state.commit_play_setup_qualification(canonical_drift,
                sound, canonical_value, 21, 2).first_failure
                == OnMemoryBankPlaySetupCommitFailure::CanonicalEpochDrift,
        "canonical epoch drift did not classify first");
    canonical_drift = reuse.snapshot;
    const OnMemoryBankSoundIdentity replacement_sound{
        reinterpret_cast<void*>(0x10900), {19, 31}};
    canonical_drift.sound = replacement_sound;
    require(committed_state.commit_play_setup_qualification(canonical_drift,
                replacement_sound, canonical_value, 21, 2).first_failure
                == OnMemoryBankPlaySetupCommitFailure::ExistingCanonicalSoundDrift,
        "canonical sound drift did not classify first");
    OnMemoryBankLifecycleState incomplete_rebase_state;
    const OnMemoryBankPlaySetupPreflight incomplete_canonical =
        incomplete_rebase_state.snapshot_play_setup(
            true, true, true, true, sound, canonical_value, 10);
    const DecodedOnMemoryBankToken incomplete_candidate{1, 0x2f0, 0x4f0};
    const auto incomplete_commit =
        incomplete_rebase_state.commit_play_setup_qualification(
            incomplete_canonical.snapshot, sound, canonical_value, 10, 2);
    const auto incomplete_rebase = incomplete_rebase_state.snapshot_play_setup(
        true, true, true, true, sound, incomplete_candidate.encode(), 11);
    require(incomplete_canonical.allowed()
            && incomplete_commit.decision == OnMemoryBankRouteDecision::Allowed
            && incomplete_commit.first_failure
                == OnMemoryBankPlaySetupCommitFailure::None
            && incomplete_rebase.decision
                == OnMemoryBankRouteDecision::OwnerTokenConflict
            && incomplete_rebase.first_failure
                == OnMemoryBankPlaySetupPreflightFailure::CanonicalRebaseRejected
            && incomplete_rebase.canonical_rebase_first_failure
                == OnMemoryBankCanonicalRebaseFailure::CompletedCustomNotAbsent,
        "canonical rebase did not require completed custom absence");

    DecodedOnMemoryBankToken previous_custom{};
    uint64_t previous_ordinal = 0;
    int release_calls = 0;
    int callback_depth = 0;
    int audio_lock_depth = 0;
    int route_lock_depth = 0;
    int retirement_diagnostic_lookup_calls = 0;
    int rejected_release_calls = 0;
    for (uint64_t index = 0; index < 10; ++index) {
        const uint64_t route = 101 + index * 2;
        const uint64_t cleanup = 301 + index;
        const uint64_t owner = index == 0 || index == 2 ? canonical_value : 0;
        if (index) {
             require(state.snapshot_play_setup(true, true, true, true, sound,
                        previous_custom.encode(), route).decision
                    == OnMemoryBankRouteDecision::OwnerTokenConflict,
                "prior custom bank was promoted to canonical");
        }
        const OnMemoryBankPlaySetupPreflight play_setup = state.snapshot_play_setup(
            true, true, true, true, sound, owner, route);
        require(play_setup.allowed()
                && play_setup.snapshot.query_token.encode() == canonical_value
                && state.commit_play_setup_qualification(
                    play_setup.snapshot, sound, owner, route, 2)
                    == OnMemoryBankRouteDecision::Allowed
                && state.qualification_matches(sound, route, owner),
            "same sound did not reuse canonical for canonical/zero owner");
        if (index == 0) {
            require(state.preflight_arm(true, true, false, true)
                        == OnMemoryBankRouteDecision::LookupSignatureUnavailable
                    && state.preflight_arm(true, true, true, false)
                        == OnMemoryBankRouteDecision::ReleaseSignatureUnavailable
                    && state.preflight_arm(false, false, false, false)
                        == OnMemoryBankRouteDecision::VanillaUnaffected
                    && state.qualification_matches(sound, route, owner)
                    && !state.active(),
                "PlaySetup signature preflight mutated custom state or blocked vanilla");
        }

        const DecodedOnMemoryBankToken custom{
            1, static_cast<uint16_t>(0x300 + index),
            static_cast<uint32_t>(0x500 + index)};
        const uint64_t request = 0x400010008ull
            + static_cast<uint64_t>(index) * 0x100000000ull;
        void* const backing = reinterpret_cast<void*>(0x9000 + index * 0x10);
        require(state.retain_detached(sound, custom, route, route, cleanup, request,
                    backing, true, true, true, true, 2, 2)
                && state.active().ordinal == previous_ordinal + 1,
            "restored/published custom bank was not retained sequentially");
        if (!index) {
            const OnMemoryBankSoundIdentity different_sound{
                reinterpret_cast<void*>(0x10800), {18, 30}};
            const auto active_pending_preflight = state.snapshot_play_setup(
                true, true, true, true, different_sound, canonical_value,
                route + 1);
            require(active_pending_preflight.decision
                        == OnMemoryBankRouteDecision::ReleasePending
                    && active_pending_preflight.first_failure
                        == OnMemoryBankPlaySetupPreflightFailure::ActivePending,
                "different sound replaced canonical during detached lifecycle");
            const DecodedOnMemoryBankToken pending_rebase_candidate{
                1, 0x2f1, 0x4f1};
            require(state.snapshot_play_setup(true, true, true, true,
                        sound, pending_rebase_candidate.encode(), route + 1).decision
                    == OnMemoryBankRouteDecision::ReleasePending,
                "same-sound canonical rebase bypassed active lifecycle barrier");
        }
        previous_ordinal = state.active().ordinal;
        OnMemoryBankDiagnosticPairState retirement_diagnostics;
        retirement_diagnostics.retain({route, cleanup, canonical, custom});

        AudioStopRetirementObservation retired_present;
        retired_present.valid = true;
        retired_present.route_generation = route + 1;
        retired_present.lease_identity = {cleanup, 0x7000 + index};
        retired_present.controller = reinterpret_cast<void*>(0xc000);
        retired_present.slot = reinterpret_cast<void*>(0xc100);
        retired_present.bgm = reinterpret_cast<void*>(0xc200);
        retired_present.custom_sound = sound.object;
        retired_present.request_handle = request;
        retired_present.request_handle_retired = true;
        retired_present.retired_count = 1;
        AudioStopRetirementState retirement_monitor;
        OnMemoryBankRetiredBackingEvidence retained_backing;
        const uint64_t retirement_epoch = index + 1;
        AudioStopRetirementObservation initially_absent = retired_present;
        initially_absent.request_handle_retired = false;
        const OnMemoryBankRetiredBackingEvidence empty_backing = retained_backing;
        require(!record_onmemory_bank_retired_backing(retained_backing,
                    {route + 1, cleanup, retirement_epoch, request, sound.live,
                        false, false, nullptr})
                && same_onmemory_bank_retired_backing_evidence(
                    retained_backing, empty_backing),
            "initial absence mutated nonnull retirement backing evidence");
        AudioStopRetirementObservation monitor_start = initially_absent;
        monitor_start.request_handle_retired = true;
        require(begin_audio_stop_retirement_monitor(
                    retirement_monitor, monitor_start)
                && advance_audio_stop_retirement_monitor(
                    retirement_monitor, monitor_start, 5)
                    == AudioStopRetirementPhase::Waiting
                && record_onmemory_bank_retired_backing(retained_backing,
                    {route + 1, cleanup, retirement_epoch, request, sound.live,
                        true, true, backing})
                && advance_audio_stop_retirement_monitor(
                    retirement_monitor, retired_present, 5)
                    == AudioStopRetirementPhase::Waiting,
            "initially absent nonnull request did not retain later backing evidence");
        AudioStopRetirementObservation retired_absent = retired_present;
        retired_absent.request_handle_retired = false;
        retired_absent.current_request_handle = request + 0x100;
        retired_absent.current_sound = reinterpret_cast<void*>(0xd000);
        retired_absent.current_state = 4;
        void* proven_retired_backing = nullptr;
        require(advance_audio_stop_retirement_monitor(
                    retirement_monitor, retired_absent, 5)
                    == AudioStopRetirementPhase::Quiescent
                && onmemory_bank_exact_request_retired(retained_backing,
                    route + 1, cleanup, retirement_epoch, request, sound.live,
                    !retired_absent.request_handle_retired,
                    proven_retired_backing)
                && proven_retired_backing == backing,
            "exact request disappearance lost retained nonnull backing evidence");

        OnMemoryBankRetirementFacts facts;
        facts.route_generation = route + 1;
        facts.cleanup_generation = cleanup;
        facts.retired_request_handle = request;
        facts.current_request_handle = request + 0x100;
        facts.current_backing = reinterpret_cast<void*>(0xa000 + index * 0x10);
        facts.retired_backing = proven_retired_backing;
        facts.current_backing_observed = true;
        facts.retired_backing_observed = true;
        facts.current_owner = classify_onmemory_bank_retirement_owner(
            state.active(), sound, true, true,
            index == 1 ? 0 : canonical_value);
        facts.exact_request_retired = true;
        facts.route_released = true;
        facts.playback_released = true;
        facts.cleanup_released = true;
        facts.owner_restore_verified = true;
        facts.runtime_installed = true;
        facts.lookup_signature_valid = true;
        facts.release_signature_valid = true;
        OnMemoryBankReleaseAction action;
        if (!index) {
            OnMemoryBankRetirementFacts bad = facts;
            bad.current_request_handle = request;
            require(!state.claim_release(bad, action),
                "unretired custom request passed release eligibility");
            bad = facts;
            bad.current_backing = backing;
            require(!state.claim_release(bad, action),
                "attached custom backing passed release eligibility");
            bad = facts;
            bad.retired_backing = reinterpret_cast<void*>(0xb000);
            require(!state.claim_release(bad, action),
                "wrong retired backing passed release eligibility");

            const uint64_t retained_ordinal = state.active().ordinal;
            const OnMemoryBankLifecyclePhase retained_phase = state.active().phase;
            const auto owner_rejected_without_mutation = [&](
                const OnMemoryBankRetirementOwnerFact& owner,
                const char* message) {
                OnMemoryBankRetirementFacts owner_bad = facts;
                owner_bad.current_owner = owner;
                const int lookup_calls_before = retirement_diagnostic_lookup_calls;
                const OnMemoryBankRetirementSchedule rejected =
                    coordinate_onmemory_bank_retirement(
                        state, owner_bad, retirement_diagnostics, route, cleanup);
                observe_onmemory_bank_pair(
                    true,
                    [&](uint64_t) {
                        ++retirement_diagnostic_lookup_calls;
                        return 2U;
                    },
                    rejected.diagnostic,
                    [](OnMemoryBankDiagnosticRole, const DecodedOnMemoryBankToken&,
                        uint32_t, OnMemoryBankPresence) {});
                if (rejected.release) {
                    (void)execute_onmemory_bank_release(
                        true, true, false, rejected.release,
                        [](uint64_t) { return 2U; },
                        [&](const uint64_t*, uint8_t) {
                            ++rejected_release_calls;
                            return OnMemoryBankNativeReleaseResult{true, 0};
                        });
                }
                require(!rejected.claimed
                        && !rejected.diagnostic
                        && !rejected.release
                        && state.active().ordinal == retained_ordinal
                        && state.active().phase == retained_phase
                        && state.active().canonical.encode() == canonical_value
                        && state.active().custom.encode() == custom.encode()
                        && state.canonical().token.encode() == canonical_value
                        && release_calls == 0
                        && rejected_release_calls == 0
                        && retirement_diagnostic_lookup_calls == lookup_calls_before
                        && !state.release_in_flight()
                        && state.custom_route_blocked()
                        && state.snapshot_play_setup(
                            false, false, false, false, {}, 0, 0).decision
                            == OnMemoryBankRouteDecision::VanillaUnaffected,
                    message);
            };
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), sound, true, true, custom.encode()),
                "detached custom owner permitted release or mutated its record");
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), sound, true, true,
                    DecodedOnMemoryBankToken{1, 0x2ff, 0x4ff}.encode()),
                "prior custom owner permitted release or mutated its record");
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), sound, true, true, 0x77707770001ULL),
                "foreign nonzero owner permitted release or mutated its record");
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), sound, true, false, canonical_value),
                "unreadable current owner permitted release or mutated its record");
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), {reinterpret_cast<void*>(0x10008), sound.live},
                    true, true, canonical_value),
                "sound pointer drift permitted release or mutated its record");
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), {sound.object, {18, 29}},
                    true, true, canonical_value),
                "sound live-index drift permitted release or mutated its record");
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), {sound.object, {17, 30}},
                    true, true, canonical_value),
                "sound live-serial drift permitted release or mutated its record");
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), sound, false, true, canonical_value),
                "sound structural drift permitted release or mutated its record");

            uint64_t interleaved_owner = canonical_value;
            const OnMemoryBankRetirementOwnerFact stale_owner =
                classify_onmemory_bank_retirement_owner(
                    state.active(), sound, true, true, interleaved_owner);
            require(stale_owner.category
                    == OnMemoryBankRetirementOwnerCategory::Canonical,
                "canonical retirement owner did not classify as eligible");
            interleaved_owner = 0x77807780001ULL;
            owner_rejected_without_mutation(
                classify_onmemory_bank_retirement_owner(
                    state.active(), sound, true, true, interleaved_owner),
                "owner changed during forced interleaving and permitted release");
        }
        const int lookup_calls_before = retirement_diagnostic_lookup_calls;
        const OnMemoryBankRetirementSchedule scheduled =
            coordinate_onmemory_bank_retirement(
                state, facts, retirement_diagnostics, route, cleanup);
        action = scheduled.release;
        observe_onmemory_bank_pair(
            true,
            [&](uint64_t) {
                ++retirement_diagnostic_lookup_calls;
                return 2U;
            },
            scheduled.diagnostic,
            [](OnMemoryBankDiagnosticRole, const DecodedOnMemoryBankToken&,
                uint32_t, OnMemoryBankPresence) {});
        require(scheduled.claimed && scheduled.diagnostic && action
                && retirement_diagnostic_lookup_calls == lookup_calls_before + 2
                && state.release_in_flight(),
            "exact N/N+1 retirement did not claim release barrier");
        if (index == 1) {
            require(facts.current_owner.category
                    == OnMemoryBankRetirementOwnerCategory::Zero,
                "Lets zero-owner retirement regression did not remain eligible");
        }
        const auto release_pending_preflight = state.snapshot_play_setup(
            true, true, true, true, sound, canonical_value, route + 2);
        require(release_pending_preflight.decision
                    == OnMemoryBankRouteDecision::ReleasePending
                && release_pending_preflight.first_failure
                    == OnMemoryBankPlaySetupPreflightFailure::ReleaseInFlight
                && state.snapshot_play_setup(false, false, false, false, {}, 0, 0).decision
                == OnMemoryBankRouteDecision::VanillaUnaffected,
            "release barrier did not block only a forced custom interleaving");
        OnMemoryBankReleaseAction stale = action;
        ++stale.route_generation;
        require(!state.finish_release(stale,
                    OnMemoryBankReleaseOutcome::AsyncReleaseRequested)
                && state.release_in_flight(),
            "stale action cleared a newer release barrier");

        ++callback_depth;
        const OnMemoryBankReleaseExecution execution = execute_onmemory_bank_release(
            true, true, false, action,
            [&](uint64_t token) {
                require(callback_depth == 1 && !audio_lock_depth && !route_lock_depth,
                    "bank lookup ran under locks or outside callback lifetime");
                return token == canonical_value || token == custom.encode() ? 2U : 3U;
            },
            [&](const uint64_t* token, uint8_t asynchronous) {
                require(callback_depth == 1 && !audio_lock_depth && !route_lock_depth,
                    "bank release ran under locks or outside callback lifetime");
                require(token && *token == custom.encode()
                        && *token != canonical_value && asynchronous == 1,
                    "release did not receive copied custom token and async byte one");
                ++release_calls;
                return OnMemoryBankNativeReleaseResult{true, 0};
            });
        --callback_depth;
        require(execution.outcome == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                && state.finish_release(action, execution.outcome)
                && !state.finish_release(action, execution.outcome),
            "native zero return or once-only release completion was mishandled");
        OnMemoryBankPendingObservation pending;
        require(state.copy_pending_observation(pending)
                && state.observe(pending, 2, 2)
                && !state.completed_owner_zero_rearm_authority()
                && state.copy_pending_observation(pending)
                && state.observe(pending, 2, 0)
                && state.active().phase == OnMemoryBankLifecyclePhase::Complete
                && state.completed_owner_zero_rearm_authority(),
            "custom kind2-to-kind0 observation did not preserve canonical kind2");
        previous_custom = custom;
    }
    require(release_calls == 10 && retirement_diagnostic_lookup_calls == 20
            && rejected_release_calls == 0 && previous_ordinal == 10
            && !state.custom_route_blocked(),
        "ten sequential custom banks hit a hard limit or skipped release");

    const DecodedOnMemoryBankToken rebase_candidate{1, 0x470, 0x670};
    const DecodedOnMemoryBankToken wrong_kind_candidate{1, 0x471, 0x671};
    const DecodedOnMemoryBankToken changed_live_candidate{1, 0x472, 0x672};
    const uint64_t rebase_route = 901;
    require(state.active().phase == OnMemoryBankLifecyclePhase::Complete
            && state.active().release_attempted
            && state.snapshot_play_setup(true, true, true, true, sound,
                previous_custom.encode(), rebase_route).decision
                == OnMemoryBankRouteDecision::OwnerTokenConflict,
        "prior custom token was accepted as a replacement canonical");
    OnMemoryBankPlaySetupPreflight wrong_kind_rebase =
        state.snapshot_play_setup(true, true, true, true, sound,
            wrong_kind_candidate.encode(), rebase_route);
    require(wrong_kind_rebase.allowed()
            && wrong_kind_rebase.canonical_rebase_attempted
            && wrong_kind_rebase.snapshot.rebasing_canonical
            && state.commit_play_setup_qualification(
                wrong_kind_rebase.snapshot, sound,
                wrong_kind_candidate.encode(), rebase_route, 3)
                == OnMemoryBankRouteDecision::CanonicalKindInvalid
            && state.canonical().token.encode() == canonical_value,
        "non-kind2 replacement changed canonical ownership");
    OnMemoryBankPlaySetupPreflight changed_live_rebase =
        state.snapshot_play_setup(true, true, true, true, sound,
            rebase_candidate.encode(), rebase_route);
    require(changed_live_rebase.allowed()
            && state.commit_play_setup_qualification(
                changed_live_rebase.snapshot, sound,
                changed_live_candidate.encode(), rebase_route, 2)
                == OnMemoryBankRouteDecision::SnapshotDrift
            && state.commit_play_setup_qualification(
                changed_live_rebase.snapshot, sound,
                rebase_candidate.encode(), rebase_route + 1, 2)
                == OnMemoryBankRouteDecision::SnapshotDrift
            && state.canonical().token.encode() == canonical_value,
        "live token or route drift committed a replacement canonical");
    OnMemoryBankPlaySetupPreflight stale_state_rebase =
        state.snapshot_play_setup(true, true, true, true, sound,
            rebase_candidate.encode(), rebase_route);
    const OnMemoryBankPlaySetupPreflight epoch_advance =
        state.snapshot_play_setup(true, true, true, true, sound,
            canonical_value, rebase_route + 2);
    require(epoch_advance.allowed()
            && state.commit_play_setup_qualification(epoch_advance.snapshot,
                sound, canonical_value, rebase_route + 2, 2)
                == OnMemoryBankRouteDecision::Allowed
            && state.commit_play_setup_qualification(stale_state_rebase.snapshot,
                sound, rebase_candidate.encode(), rebase_route, 2)
                == OnMemoryBankRouteDecision::SnapshotDrift,
        "stale lifecycle epoch committed a replacement canonical");
    const OnMemoryBankPlaySetupPreflight successful_rebase =
        state.snapshot_play_setup(true, true, true, true, sound,
            rebase_candidate.encode(), rebase_route + 3);
    require(successful_rebase.allowed()
            && successful_rebase.canonical_rebase_attempted
            && successful_rebase.snapshot.rebasing_canonical
            && successful_rebase.snapshot.stale_canonical.encode()
                == canonical_value
            && successful_rebase.snapshot.prior_custom.encode()
                == previous_custom.encode()
            && state.commit_play_setup_qualification(successful_rebase.snapshot,
                sound, rebase_candidate.encode(), rebase_route + 3, 2)
                == OnMemoryBankRouteDecision::Allowed
            && state.canonical().token.encode() == rebase_candidate.encode()
            && state.qualification_matches(
                sound, rebase_route + 3, rebase_candidate.encode()),
        "completed custom absence did not rebase and qualify canonical token");
    const DecodedOnMemoryBankToken post_rebase_custom{1, 0x473, 0x673};
    const uint64_t post_rebase_route = rebase_route + 3;
    const uint64_t post_rebase_cleanup = 1904;
    const uint64_t post_rebase_request = 0x900010008ull;
    void* const post_rebase_backing = reinterpret_cast<void*>(0xd000);
    require(state.retain_detached(sound, post_rebase_custom,
                post_rebase_route, post_rebase_route, post_rebase_cleanup,
                post_rebase_request, post_rebase_backing,
                true, true, true, true, 2, 2),
        "rebased canonical could not retain a later custom bank");
    OnMemoryBankRetirementFacts post_rebase_retirement;
    post_rebase_retirement.route_generation = post_rebase_route + 1;
    post_rebase_retirement.cleanup_generation = post_rebase_cleanup;
    post_rebase_retirement.retired_request_handle = post_rebase_request;
    post_rebase_retirement.current_request_handle = post_rebase_request + 1;
    post_rebase_retirement.current_backing = reinterpret_cast<void*>(0xd100);
    post_rebase_retirement.retired_backing = post_rebase_backing;
    post_rebase_retirement.current_backing_observed = true;
    post_rebase_retirement.retired_backing_observed = true;
    post_rebase_retirement.current_owner =
        classify_onmemory_bank_retirement_owner(state.active(), sound,
            true, true, rebase_candidate.encode());
    post_rebase_retirement.exact_request_retired = true;
    post_rebase_retirement.route_released = true;
    post_rebase_retirement.playback_released = true;
    post_rebase_retirement.cleanup_released = true;
    post_rebase_retirement.owner_restore_verified = true;
    post_rebase_retirement.runtime_installed = true;
    post_rebase_retirement.lookup_signature_valid = true;
    post_rebase_retirement.release_signature_valid = true;
    OnMemoryBankReleaseAction post_rebase_release;
    require(state.claim_release(post_rebase_retirement, post_rebase_release),
        "rebased lifecycle could not claim its custom release");
    require(post_rebase_release
            && post_rebase_release.custom.encode()
                == post_rebase_custom.encode()
            && post_rebase_release.custom.encode()
                != rebase_candidate.encode()
            && post_rebase_release.custom.encode() != canonical_value
            && state.finish_release(post_rebase_release,
                OnMemoryBankReleaseOutcome::AlreadyAbsent),
        "canonical rebase made an old or new canonical token releasable");

    const auto prepare_release = [&](OnMemoryBankLifecycleState& target,
                                     const DecodedOnMemoryBankToken& custom,
                                     uint64_t route) {
        const OnMemoryBankPlaySetupPreflight play_setup = target.snapshot_play_setup(
            true, true, true, true, sound, canonical_value, route);
        require(play_setup.allowed()
                && target.commit_play_setup_qualification(
                    play_setup.snapshot, sound, canonical_value, route, 2)
                    == OnMemoryBankRouteDecision::Allowed,
            "edge-case route could not qualify canonical bank");
        void* const backing = reinterpret_cast<void*>(0xc000 + route);
        require(target.retain_detached(sound, custom, route, route, route + 100,
                    route + 200, backing, true, true, true, true, 2, 2),
            "edge-case detached bank was not retained");
        OnMemoryBankRetirementFacts facts;
        facts.route_generation = route + 1;
        facts.cleanup_generation = route + 100;
        facts.retired_request_handle = route + 200;
        facts.current_request_handle = route + 201;
        facts.current_backing = reinterpret_cast<void*>(0xd000 + route);
        facts.retired_backing = backing;
        facts.current_backing_observed = true;
        facts.retired_backing_observed = true;
        facts.current_owner = classify_onmemory_bank_retirement_owner(
            target.active(), sound, true, true, canonical_value);
        facts.exact_request_retired = true;
        facts.route_released = true;
        facts.playback_released = true;
        facts.cleanup_released = true;
        facts.owner_restore_verified = true;
        facts.runtime_installed = true;
        facts.lookup_signature_valid = true;
        facts.release_signature_valid = true;
        OnMemoryBankReleaseAction action;
        require(target.claim_release(facts, action),
            "edge-case exact retirement did not claim release");
        return action;
    };

    OnMemoryBankLifecycleState absent_state;
    const DecodedOnMemoryBankToken absent_custom{1, 0x430, 0x630};
    const OnMemoryBankReleaseAction absent_action = prepare_release(
        absent_state, absent_custom, 2001);
    int absent_release_calls = 0;
    int unavailable_calls = 0;
    require(execute_onmemory_bank_release(false, true, false, absent_action,
                [&](uint64_t) { ++unavailable_calls; return 2U; },
                [&](const uint64_t*, uint8_t) {
                    ++unavailable_calls;
                    return OnMemoryBankNativeReleaseResult{true, 0};
                }).outcome == OnMemoryBankReleaseOutcome::Failed
            && execute_onmemory_bank_release(true, false, false, absent_action,
                [&](uint64_t) { ++unavailable_calls; return 2U; },
                [&](const uint64_t*, uint8_t) {
                    ++unavailable_calls;
                    return OnMemoryBankNativeReleaseResult{true, 0};
                }).outcome == OnMemoryBankReleaseOutcome::Failed
            && unavailable_calls == 0,
        "unavailable signature invoked a bank callable");
    const OnMemoryBankReleaseExecution absent = execute_onmemory_bank_release(
        true, true, false, absent_action,
        [&](uint64_t token) { return token == canonical_value ? 2U : 0U; },
        [&](const uint64_t*, uint8_t) {
            ++absent_release_calls;
            return OnMemoryBankNativeReleaseResult{true, 0};
        });
    require(absent.outcome == OnMemoryBankReleaseOutcome::AlreadyAbsent
            && absent_release_calls == 0
            && absent_state.finish_release(absent_action, absent.outcome)
            && absent_state.active().phase == OnMemoryBankLifecyclePhase::Complete
            && !absent_state.completed_owner_zero_rearm_authority(),
        "already absent custom bank invoked native release");

    OnMemoryBankLifecycleState timeout_state;
    const DecodedOnMemoryBankToken timeout_custom{1, 0x431, 0x631};
    const OnMemoryBankReleaseAction timeout_action = prepare_release(
        timeout_state, timeout_custom, 2101);
    int timeout_release_calls = 0;
    const OnMemoryBankReleaseExecution timeout_release = execute_onmemory_bank_release(
        true, true, false, timeout_action,
        [](uint64_t) { return 2U; },
        [&](const uint64_t*, uint8_t) {
            ++timeout_release_calls;
            return OnMemoryBankNativeReleaseResult{true, 0};
        });
    require(timeout_state.finish_release(timeout_action, timeout_release.outcome),
        "timeout route did not enter residency observation");
    OnMemoryBankPendingObservation timeout_observation;
    OnMemoryBankPlaySetupPreflight failed_preflight;
    require(timeout_state.copy_pending_observation(timeout_observation)
            && timeout_state.observe(timeout_observation, 2, 2, 1)
            && timeout_state.failed() && timeout_release_calls == 1
            && !timeout_state.copy_pending_observation(timeout_observation)
            && ((failed_preflight = timeout_state.snapshot_play_setup(
                    true, true, true, true, sound, canonical_value, 2201)).decision)
                == OnMemoryBankRouteDecision::LifecycleFailed
            && failed_preflight.first_failure
                == OnMemoryBankPlaySetupPreflightFailure::LifecycleFailed
            && timeout_state.snapshot_play_setup(false, false, false, false, {}, 0, 0).decision
                == OnMemoryBankRouteDecision::VanillaUnaffected,
        "timeout retried release or failed to block only future custom routes");

    OnMemoryBankLifecycleState wrong_release_state;
    const DecodedOnMemoryBankToken wrong_custom{1, 0x432, 0x632};
    const OnMemoryBankReleaseAction wrong_action = prepare_release(
        wrong_release_state, wrong_custom, 2301);
    int wrong_release_calls = 0;
    const OnMemoryBankReleaseExecution wrong_execution = execute_onmemory_bank_release(
        true, true, false, wrong_action,
        [&](uint64_t token) { return token == canonical_value ? 2U : 3U; },
        [&](const uint64_t*, uint8_t) {
            ++wrong_release_calls;
            return OnMemoryBankNativeReleaseResult{true, 0};
        });
    require(wrong_execution.outcome == OnMemoryBankReleaseOutcome::Failed
            && wrong_release_calls == 0
            && wrong_release_state.finish_release(wrong_action, wrong_execution.outcome)
            && wrong_release_state.failed(),
        "unexpected custom kind did not fail closed before native release");

    const OnMemoryBankSoundIdentity new_sound{
        reinterpret_cast<void*>(0x11000), {44, 55}};
    const DecodedOnMemoryBankToken new_canonical{1, 0x420, 0x620};
    require(state.snapshot_play_setup(
                true, true, true, true, new_sound, 0, 1001).decision
            == OnMemoryBankRouteDecision::OwnerTokenInvalid,
        "different PlaySetup sound reused a prior canonical from zero owner");
    const OnMemoryBankPlaySetupPreflight new_play_setup = state.snapshot_play_setup(
        true, true, true, true, new_sound, new_canonical.encode(), 1001);
    require(new_play_setup.allowed()
            && new_play_setup.snapshot.establishing_canonical
            && state.commit_play_setup_qualification(new_play_setup.snapshot, new_sound,
                new_canonical.encode(), 1001, 2) == OnMemoryBankRouteDecision::Allowed
            && state.canonical().sound == new_sound,
        "idle new sound did not establish its own canonical bank");

    const std::string log = format_onmemory_bank_lifecycle_log(
        "residency_observed", OnMemoryBankLifecyclePhase::Complete,
        12, 13, 14, "custom_absent", 2, 0);
    require(log.find("route_generation=12") != std::string::npos
            && log.find("cleanup_generation=13") != std::string::npos
            && log.find("ordinal=14") != std::string::npos
            && log.find("token=") == std::string::npos
            && log.find("type=") == std::string::npos
            && log.find("index=") == std::string::npos
            && log.find("path=") == std::string::npos
            && log.find("0x") == std::string::npos,
        "lifecycle log exposed reconstructable token/pointer/path fields");
}

void test_onmemory_bank_completed_owner_zero_rearm()
{
    using namespace ff7r::piano::game;
    const OnMemoryBankSoundIdentity sound{
        reinterpret_cast<void*>(0x19000), {91, 101}};
    const DecodedOnMemoryBankToken canonical{1, 0x601, 0x801};
    const DecodedOnMemoryBankToken released_custom{1, 0x602, 0x802};
    constexpr uint64_t route = 701;
    constexpr uint64_t cleanup = 801;
    constexpr uint64_t request = UINT64_C(0x5100000008);

    const auto completed = [&] {
        OnMemoryBankLifecycleState state;
        const auto setup = state.snapshot_play_setup(
            true, true, true, true, sound, canonical.encode(), route);
        require(setup.allowed()
                && state.commit_play_setup_qualification(
                    setup.snapshot, sound, canonical.encode(), route, 2)
                    == OnMemoryBankRouteDecision::Allowed
                && state.retain_detached(sound, released_custom, route, route,
                    cleanup, request, reinterpret_cast<void*>(0x19100), true,
                    true, true, true, 2, 2),
            "completed-rearm fixture did not retain exact detached bank");
        OnMemoryBankRetirementFacts facts;
        facts.route_generation = route + 1;
        facts.cleanup_generation = cleanup;
        facts.retired_request_handle = request;
        facts.current_request_handle = request + UINT64_C(0x100000000);
        facts.current_backing = reinterpret_cast<void*>(0x19200);
        facts.retired_backing = reinterpret_cast<void*>(0x19100);
        facts.current_backing_observed = true;
        facts.retired_backing_observed = true;
        facts.current_owner = classify_onmemory_bank_retirement_owner(
            state.active(), sound, true, true, canonical.encode());
        facts.exact_request_retired = true;
        facts.route_released = true;
        facts.playback_released = true;
        facts.cleanup_released = true;
        facts.owner_restore_verified = true;
        facts.runtime_installed = true;
        facts.lookup_signature_valid = true;
        facts.release_signature_valid = true;
        OnMemoryBankReleaseAction action;
        require(state.claim_release(facts, action) && action,
            "completed-rearm fixture did not claim custom release");
        int canonical_release_calls = 0;
        int custom_release_calls = 0;
        const auto execution = execute_onmemory_bank_release(
            true, true, false, action,
            [&](const uint64_t token) {
                return token == canonical.encode() || token == released_custom.encode()
                    ? 2U : 3U;
            },
            [&](const uint64_t* token, const uint8_t asynchronous) {
                require(token && *token == released_custom.encode()
                        && *token != canonical.encode() && asynchronous == 1,
                    "completed-rearm fixture did not release only copied custom token");
                canonical_release_calls += *token == canonical.encode();
                ++custom_release_calls;
                return OnMemoryBankNativeReleaseResult{true, 0};
            });
        require(execution.outcome
                    == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                && custom_release_calls == 1 && canonical_release_calls == 0
                && state.finish_release(action, execution.outcome),
            "completed-rearm fixture release did not remain custom-only");
        OnMemoryBankPendingObservation pending;
        require(state.copy_pending_observation(pending)
                && state.observe(pending, 2, 2)
                && state.copy_pending_observation(pending)
                && state.observe(pending, 2, 0)
                && state.active().phase == OnMemoryBankLifecyclePhase::Complete
                && state.completed_owner_zero_rearm_authority(),
            "exact asynchronous completion did not produce rearm authority");
        return state;
    };

    const auto rearmed = [&] {
        auto state = completed();
        const auto active = state.active();
        const auto authority = state.completed_owner_zero_rearm_authority();
        const uint64_t prior_epoch = state.state_epoch();
        require(state.completed_reset_matches(active, authority)
                && state.reset_completed_for_next_activation(active, authority)
                && !state.active() && !state.canonical()
                && state.state_epoch() > prior_epoch
                && state.completed_owner_zero_rearm_authority()
                && state.completed_owner_zero_rearm_authority().generation
                    > authority.generation
                && state.completed_owner_zero_rearm_authority().aba_ordinal
                    > authority.aba_ordinal,
            "specialized completed reset did not preserve advanced rearm authority");
        return state;
    };

    {
        const auto state = completed();
        const auto authority = state.completed_owner_zero_rearm_authority();
        require(authority && authority.source_ordinal == state.active().ordinal
                && authority.sound == sound
                && authority.canonical.encode() == canonical.encode()
                && authority.released_custom.encode() == released_custom.encode()
                && authority.canonical_kind == 2
                && authority.released_custom_kind == 0
                && authority.owner_restore_verified && authority.release_attempted,
            "completion authority did not retain exact immutable witness");
        auto invalid = authority;
        invalid.generation = 0;
        require(!invalid, "zero rearm generation remained valid");
        invalid = authority;
        invalid.source_ordinal = 0;
        require(!invalid, "zero source ordinal remained valid");
        invalid = authority;
        invalid.state_epoch = 0;
        require(!invalid, "zero state epoch remained valid rearm authority");
        invalid = authority;
        invalid.aba_ordinal = 0;
        require(!invalid, "zero ABA ordinal remained valid rearm authority");
        invalid = authority;
        invalid.route_generation = 0;
        require(!invalid, "zero route lineage remained valid rearm authority");
        invalid = authority;
        invalid.cleanup_generation = 0;
        require(!invalid, "zero cleanup lineage remained valid rearm authority");
        invalid = authority;
        invalid.canonical_kind = 3;
        require(!invalid, "non-kind2 canonical witness remained valid");
        invalid = authority;
        invalid.released_custom = canonical;
        require(!invalid, "non-distinct released token remained valid rearm authority");
        invalid = authority;
        invalid.sound.live.serial_number += 1;
        require(invalid && !same_onmemory_bank_completed_owner_zero_rearm_authority(
                    authority, invalid),
            "sound serial drift was not retained as exact witness drift");
        invalid = authority;
        invalid.released_custom_kind = 2;
        require(!invalid, "nonzero released-custom kind remained valid");
        invalid = authority;
        invalid.owner_restore_verified = false;
        require(!invalid, "unrestored owner remained valid rearm authority");
        invalid = authority;
        invalid.release_attempted = false;
        require(!invalid, "unattempted release remained valid rearm authority");

        auto expected = state.active();
        auto copy = state;
        ++expected.ordinal;
        require(!copy.reset_completed_for_next_activation(expected, authority),
            "ordinal drift passed specialized completed reset");
        expected = state.active();
        copy = state;
        ++expected.route_generation;
        require(!copy.reset_completed_for_next_activation(expected, authority),
            "route-generation drift passed specialized completed reset");
        expected = state.active();
        copy = state;
        ++expected.cleanup_generation;
        require(!copy.reset_completed_for_next_activation(expected, authority),
            "cleanup-generation drift passed specialized completed reset");
        expected = state.active();
        copy = state;
        expected.sound.live.internal_index += 1;
        require(!copy.reset_completed_for_next_activation(expected, authority),
            "sound identity drift passed specialized completed reset");
        expected = state.active();
        copy = state;
        expected.release_attempted = false;
        require(!copy.reset_completed_for_next_activation(expected, authority),
            "release witness drift passed specialized completed reset");
        copy = state;
        invalid = authority;
        ++invalid.generation;
        require(!copy.reset_completed_for_next_activation(state.active(), invalid),
            "authority-generation drift passed specialized completed reset");
    }

    {
        auto state = rearmed();
        const auto setup = state.snapshot_play_setup(
            true, true, true, true, sound, 0, route + 2);
        require(setup.allowed() && setup.snapshot.completed_owner_zero_rearm
                && setup.snapshot.establishing_canonical
                && !setup.snapshot.deferred_canonical_establishment
                && setup.snapshot.query_token.encode() == canonical.encode()
                && setup.snapshot.query_token.encode() != released_custom.encode(),
            "owner-zero same-sound rearm fabricated a bridge or reused custom token");
        auto drift = setup.snapshot;
        ++drift.rearm_generation;
        require(state.commit_play_setup_qualification(
                    drift, sound, 0, route + 2, 2).first_failure
                    == OnMemoryBankPlaySetupCommitFailure::CompletedRearmDrift
                && state.completed_owner_zero_rearm_authority(),
            "stale rearm generation consumed authority");
        drift = setup.snapshot;
        ++drift.rearm_source_ordinal;
        require(state.commit_play_setup_qualification(
                    drift, sound, 0, route + 2, 2).first_failure
                    == OnMemoryBankPlaySetupCommitFailure::CompletedRearmDrift
                && state.completed_owner_zero_rearm_authority(),
            "stale rearm source ordinal consumed authority");
        drift = setup.snapshot;
        ++drift.rearm_aba_ordinal;
        require(state.commit_play_setup_qualification(
                    drift, sound, 0, route + 2, 2).first_failure
                    == OnMemoryBankPlaySetupCommitFailure::CompletedRearmDrift
                && state.completed_owner_zero_rearm_authority(),
            "stale rearm ABA lineage consumed authority");
        drift = setup.snapshot;
        drift.query_token = released_custom;
        require(state.commit_play_setup_qualification(
                    drift, sound, 0, route + 2, 2).first_failure
                    == OnMemoryBankPlaySetupCommitFailure::CompletedRearmDrift
                && state.completed_owner_zero_rearm_authority(),
            "released custom query substitution consumed rearm authority");
        require(state.commit_play_setup_qualification(
                    setup.snapshot, sound, 0, route + 2, 3).decision
                    == OnMemoryBankRouteDecision::CanonicalKindInvalid
                && state.completed_owner_zero_rearm_authority(),
            "failed kind lookup consumed rearm authority");
        require(state.commit_play_setup_qualification(
                    setup.snapshot, sound, canonical.encode(), route + 2, 2).decision
                    == OnMemoryBankRouteDecision::SnapshotDrift
                && state.completed_owner_zero_rearm_authority(),
            "owner drift consumed rearm authority");
        OnMemoryBankSoundIdentity sound_drift = sound;
        ++sound_drift.live.serial_number;
        require(state.commit_play_setup_qualification(
                    setup.snapshot, sound_drift, 0, route + 2, 2).decision
                    == OnMemoryBankRouteDecision::SnapshotDrift
                && state.completed_owner_zero_rearm_authority(),
            "sound drift consumed rearm authority");
        require(state.commit_play_setup_qualification(
                    setup.snapshot, sound, 0, route + 2, 2)
                    == OnMemoryBankRouteDecision::Allowed
                && state.canonical().token.encode() == canonical.encode()
                && !state.completed_owner_zero_rearm_authority(),
            "exact rearm did not commit once or consume authority");
        const DecodedOnMemoryBankToken next_custom{1, 0x603, 0x803};
        require(state.retain_detached(sound, next_custom, route + 2, route + 2,
                    cleanup + 1, request + UINT64_C(0x100000000),
                    reinterpret_cast<void*>(0x19300), true, true, true, true, 2, 2),
            "rearmed canonical did not retain subsequent custom ownership");
    }

    {
        auto state = rearmed();
        const auto same_bridge = state.snapshot_play_setup(
            true, true, true, true, sound, 0, route + 3, canonical.encode());
        require(same_bridge.allowed()
                && same_bridge.snapshot.completed_owner_zero_rearm
                && !same_bridge.snapshot.deferred_canonical_establishment,
            "same-token bridge/rearm coexistence did not select rearm authority");
        const DecodedOnMemoryBankToken mismatch{1, 0x604, 0x804};
        const auto conflict = state.snapshot_play_setup(
            true, true, true, true, sound, 0, route + 3, mismatch.encode());
        require(conflict.decision == OnMemoryBankRouteDecision::OwnerTokenConflict
                && conflict.first_failure
                    == OnMemoryBankPlaySetupPreflightFailure::CompletedRearmConflict,
            "mismatched bridge/rearm tokens did not fail closed");
    }

    {
        OnMemoryBankLifecycleState bridge_only;
        const auto setup = bridge_only.snapshot_play_setup(
            true, true, true, true, sound, 0, route, canonical.encode());
        require(setup.allowed() && setup.snapshot.deferred_canonical_establishment
                && !setup.snapshot.completed_owner_zero_rearm,
            "bridge-only owner-zero path changed semantics");
        OnMemoryBankLifecycleState empty;
        require(empty.snapshot_play_setup(
                    true, true, true, true, sound, 0, route).first_failure
                    == OnMemoryBankPlaySetupPreflightFailure::OwnerTokenZero,
            "generic empty owner-zero bypassed OwnerTokenZero");
    }

    {
        auto state = rearmed();
        const DecodedOnMemoryBankToken newer{1, 0x605, 0x805};
        const auto setup = state.snapshot_play_setup(
            true, true, true, true, sound, newer.encode(), route + 4);
        require(setup.allowed() && !setup.snapshot.completed_owner_zero_rearm
                && state.commit_play_setup_qualification(
                    setup.snapshot, sound, newer.encode(), route + 4, 2)
                    == OnMemoryBankRouteDecision::Allowed
                && !state.completed_owner_zero_rearm_authority(),
            "newer nonzero establishment retained stale rearm authority");
        state = rearmed();
        OnMemoryBankSoundIdentity different_sound = sound;
        different_sound.object = reinterpret_cast<void*>(0x19400);
        ++different_sound.live.internal_index;
        const DecodedOnMemoryBankToken bridge_token{1, 0x606, 0x806};
        const auto bridge_setup = state.snapshot_play_setup(true, true, true, true,
            different_sound, 0, route + 5, bridge_token.encode());
        require(bridge_setup.allowed()
                && bridge_setup.snapshot.deferred_canonical_establishment
                && !bridge_setup.snapshot.completed_owner_zero_rearm
                && state.commit_play_setup_qualification(bridge_setup.snapshot,
                    different_sound, 0, route + 5, 2)
                    == OnMemoryBankRouteDecision::Allowed
                && !state.completed_owner_zero_rearm_authority(),
            "successful external bridge retained stale rearm authority");
        state = rearmed();
        const auto stale = state.snapshot_play_setup(
            true, true, true, true, sound, 0, route + 6);
        state.reset();
        require(!state.completed_owner_zero_rearm_authority()
                && !state.canonical() && !state.active()
                && state.commit_play_setup_qualification(
                    stale.snapshot, sound, 0, route + 6, 2).decision
                    == OnMemoryBankRouteDecision::SnapshotDrift,
            "full lifecycle reset retained completed rearm authority");
    }
}

void test_audio_patch_exact_override_metadata_normalization()
{
    using namespace ff7r::piano::game;

    uint64_t owner = 0;
    const uint64_t custom = 0x10101;
    const uint64_t canonical = 0x20201;
    AudioPatchRestoreField field{
        &owner, 0x548, 0, 0, sizeof(uint64_t), "sound+0x548", true};
    AudioPatchRestoreExactOverride exact;
    exact.enabled = true;
    exact.fresh_native_owner_proof = true;
    exact.lookup_signature_valid = true;
    exact.release_signature_valid = true;
    exact.canonical_kind2_qualified = true;
    exact.custom_kind2_qualified = true;
    exact.object = &owner;
    exact.offset = 0x548;
    exact.size = sizeof(uint64_t);
    exact.label = "sound+0x548";
    exact.expected_journal_original = 0;
    exact.expected_current = custom;
    exact.restore_value = canonical;
    exact.applied = true;

    struct WitnessFailure {
        void (*break_witness)(AudioPatchRestoreField&, AudioPatchRestoreExactOverride&);
        const char* message;
    };
    const WitnessFailure failures[] = {
        {[](auto&, auto& value) { value.applied = false; }, "unapplied override normalized metadata"},
        {[](auto&, auto& value) { value.enabled = false; }, "disabled override normalized metadata"},
        {[](auto&, auto& value) { value.fresh_native_owner_proof = false; }, "stale native owner normalized metadata"},
        {[](auto&, auto& value) { value.lookup_signature_valid = false; }, "missing lookup normalized metadata"},
        {[](auto&, auto& value) { value.release_signature_valid = false; }, "missing release normalized metadata"},
        {[](auto&, auto& value) { value.canonical_kind2_qualified = false; }, "invalid canonical kind normalized metadata"},
        {[](auto&, auto& value) { value.custom_kind2_qualified = false; }, "invalid custom kind normalized metadata"},
        {[](auto& value, auto&) { value.object = nullptr; }, "null journal object normalized metadata"},
        {[](auto&, auto& value) { value.object = nullptr; }, "null override object normalized metadata"},
        {[](auto&, auto& value) { value.object = reinterpret_cast<void*>(0x1); }, "object drift normalized metadata"},
        {[](auto&, auto& value) { ++value.offset; }, "offset drift normalized metadata"},
        {[](auto& value, auto&) { value.size = 0; }, "zero journal size normalized metadata"},
        {[](auto&, auto& value) { value.size = sizeof(uint32_t); }, "size drift normalized metadata"},
        {[](auto& value, auto&) { value.label = nullptr; }, "null journal label normalized metadata"},
        {[](auto&, auto& value) { value.label = nullptr; }, "null override label normalized metadata"},
        {[](auto&, auto& value) { value.label = "other"; }, "label drift normalized metadata"},
        {[](auto& value, auto&) { value.original = 7; }, "journal-original drift normalized metadata"},
        {[](auto&, auto& value) { value.expected_journal_original = 7; }, "override-original drift normalized metadata"},
        {[](auto& value, auto&) { value.replacement = 1; }, "nonzero replacement normalized metadata"},
        {[](auto&, auto& value) { value.expected_current = 0; }, "zero custom token normalized metadata"},
        {[](auto&, auto& value) { value.restore_value = 0; }, "zero canonical token normalized metadata"},
        {[](auto&, auto& value) { value.restore_value = value.expected_current; }, "identical canonical/custom token normalized metadata"},
    };
    for (const auto& failure : failures) {
        auto candidate = field;
        auto candidate_exact = exact;
        failure.break_witness(candidate, candidate_exact);
        const auto before = candidate;
        require(!rebase_audio_patch_original_after_exact_override(
                    candidate, candidate_exact)
                && candidate.object == before.object
                && candidate.offset == before.offset
                && candidate.original == before.original
                && candidate.replacement == before.replacement
                && candidate.size == before.size
                && candidate.label == before.label
                && candidate.redact_values_in_report == before.redact_values_in_report,
            failure.message);
    }

    AudioPatchOriginalRebaseContextFacts context{
        true, true, true, true, true, true, true, true};
    require(audio_patch_original_rebase_context_exact(context),
        "exact metadata normalization context was rejected");
    bool AudioPatchOriginalRebaseContextFacts::* context_witnesses[] = {
        &AudioPatchOriginalRebaseContextFacts::route_generation_exact,
        &AudioPatchOriginalRebaseContextFacts::lease_exact,
        &AudioPatchOriginalRebaseContextFacts::sound_pointer_exact,
        &AudioPatchOriginalRebaseContextFacts::sound_live_identity_exact,
        &AudioPatchOriginalRebaseContextFacts::controller_pointer_exact,
        &AudioPatchOriginalRebaseContextFacts::controller_live_identity_exact,
        &AudioPatchOriginalRebaseContextFacts::setup_exact,
        &AudioPatchOriginalRebaseContextFacts::frozen_snapshot_valid,
    };
    for (auto witness : context_witnesses) {
        auto drift = context;
        drift.*witness = false;
        require(!audio_patch_original_rebase_context_exact(drift),
            "route/sound/UObject/setup drift passed normalization context");
    }

    AudioPatchOriginalRebasePostRestoreSetupFacts post_restore_setup{
        true, true, true, true, true, true, true};
    require(audio_patch_original_rebase_post_restore_setup_exact(
                post_restore_setup),
        "exact post-restore setup relation was rejected");
    bool AudioPatchOriginalRebasePostRestoreSetupFacts::* setup_witnesses[] = {
        &AudioPatchOriginalRebasePostRestoreSetupFacts::selection_song_exact,
        &AudioPatchOriginalRebasePostRestoreSetupFacts::selection_profile_valid,
        &AudioPatchOriginalRebasePostRestoreSetupFacts::desired_song_exact,
        &AudioPatchOriginalRebasePostRestoreSetupFacts::patched_song_cleared,
        &AudioPatchOriginalRebasePostRestoreSetupFacts::transient_sound_cleared,
        &AudioPatchOriginalRebasePostRestoreSetupFacts::pending_patch_journal_empty,
        &AudioPatchOriginalRebasePostRestoreSetupFacts::unpublished_setup_cleared,
    };
    for (auto witness : setup_witnesses) {
        auto stale = post_restore_setup;
        stale.*witness = false;
        require(!audio_patch_original_rebase_post_restore_setup_exact(stale),
            "stale/missing post-restore setup fact was accepted");
    }
    const bool old_pre_restore_patched_song_requirement =
        !post_restore_setup.patched_song_cleared;
    require(!old_pre_restore_patched_song_requirement
            && audio_patch_original_rebase_post_restore_setup_exact(
                post_restore_setup),
        "restored setup did not distinguish cleared patched song from old requirement");
    const auto post_restore_publication_model = [&](const bool owner_zero) {
        const bool normalized =
            audio_patch_original_rebase_post_restore_setup_exact(
                post_restore_setup);
        const bool publication_remains = normalized;
        const bool publication_revoked = !normalized;
        const bool detached_retained = normalized;
        return std::array<bool, 4>{
            owner_zero, publication_remains, publication_revoked,
            detached_retained};
    };
    require(post_restore_publication_model(false)
                == std::array<bool, 4>{false, true, false, true},
        "ordinary restored setup did not retain publication/detached ownership");
    require(post_restore_publication_model(true)
                == std::array<bool, 4>{true, true, false, true},
        "owner-zero restored setup did not retain publication/detached ownership");

    constexpr uint64_t pre_restore_generation = 70;
    require(audio_patch_original_rebase_route_successor_exact(
                pre_restore_generation, pre_restore_generation,
                pre_restore_generation + 1),
        "exact frozen N to live N+1 owner-normalization route was rejected");
    require(!audio_patch_original_rebase_route_successor_exact(
                pre_restore_generation, pre_restore_generation,
                pre_restore_generation),
        "same-generation owner-normalization route was accepted");
    require(!audio_patch_original_rebase_route_successor_exact(
                pre_restore_generation, pre_restore_generation,
                pre_restore_generation + 2),
        "skipped owner-normalization route successor was accepted");
    require(!audio_patch_original_rebase_route_successor_exact(
                pre_restore_generation - 1, pre_restore_generation,
                pre_restore_generation + 1),
        "frozen-generation drift passed owner normalization");
    require(!audio_patch_original_rebase_route_successor_exact(0, 0, 1),
        "zero owner-normalization generation was accepted");
    require(!audio_patch_original_rebase_route_successor_exact(
                UINT64_MAX, UINT64_MAX, 0),
        "maximum owner-normalization generation was accepted");
    require(!audio_patch_original_rebase_route_successor_exact(
                UINT64_MAX, UINT64_MAX, UINT64_MAX),
        "wrapped owner-normalization generation was accepted");

    auto owner_zero_bridge = field;
    require(rebase_audio_patch_original_after_exact_override(
                owner_zero_bridge, exact)
            && owner_zero_bridge.original == canonical
            && owner_zero_bridge.replacement == 0
            && owner_zero_bridge.object == field.object
            && owner_zero_bridge.offset == field.offset
            && owner_zero_bridge.size == field.size
            && owner_zero_bridge.label == field.label
            && owner_zero_bridge.redact_values_in_report,
        "exact owner-zero bridge override did not normalize only journal original");
    auto owner_zero_rearm = field;
    require(rebase_audio_patch_original_after_exact_override(
                owner_zero_rearm, exact)
            && owner_zero_rearm.original == canonical
            && owner_zero_rearm.replacement == 0,
        "exact owner-zero rearm override did not normalize frozen metadata");

    auto ordinary = field;
    ordinary.original = canonical;
    auto ordinary_exact = exact;
    ordinary_exact.expected_journal_original = canonical;
    require(rebase_audio_patch_original_after_exact_override(
                ordinary, ordinary_exact)
            && ordinary.original == canonical && ordinary.replacement == 0,
        "ordinary canonical journal original was not preserved");

    const bool first_activation_normalized =
        audio_patch_original_rebase_route_successor_exact(
            pre_restore_generation, pre_restore_generation,
            pre_restore_generation + 1)
        && rebase_audio_patch_original_after_exact_override(
            ordinary, ordinary_exact);
    require(first_activation_normalized && ordinary.original == canonical
            && ordinary.replacement == 0,
        "first-activation canonical metadata was not idempotent on N to N+1");

    const auto publication_model = [&](const uint64_t frozen_generation,
                                       const uint64_t live_generation) {
        auto candidate = field;
        const bool normalized = audio_patch_original_rebase_route_successor_exact(
                frozen_generation, pre_restore_generation, live_generation)
            && rebase_audio_patch_original_after_exact_override(candidate, exact);
        const bool publication_remains = normalized;
        const bool publication_revoked = !normalized;
        const bool detached_retained = publication_remains;
        return std::array<bool, 3>{
            publication_remains, publication_revoked, detached_retained};
    };
    require(publication_model(pre_restore_generation,
                pre_restore_generation + 1)
                == std::array<bool, 3>{true, false, true},
        "exact owner-zero bridge rebase did not retain publication/ownership");
    require(publication_model(pre_restore_generation,
                pre_restore_generation)
                == std::array<bool, 3>{false, true, false},
        "same-generation normalization did not revoke publication/retention");
    require(publication_model(pre_restore_generation,
                pre_restore_generation + 2)
                == std::array<bool, 3>{false, true, false},
        "skipped successor normalization retained detached publication");
    auto custom_original = field;
    custom_original.original = custom;
    auto custom_original_exact = exact;
    custom_original_exact.expected_journal_original = custom;
    require(!rebase_audio_patch_original_after_exact_override(
                custom_original, custom_original_exact)
            && custom_original.original == custom
            && custom_original.replacement == 0,
        "custom token was accepted as frozen journal original");
    constexpr uint64_t unrelated_original = 0x30301;
    auto arbitrary_original = field;
    arbitrary_original.original = unrelated_original;
    auto arbitrary_original_exact = exact;
    arbitrary_original_exact.expected_journal_original = unrelated_original;
    require(!rebase_audio_patch_original_after_exact_override(
                arbitrary_original, arbitrary_original_exact)
            && arbitrary_original.original == unrelated_original
            && arbitrary_original.replacement == 0,
        "unrelated token was accepted as frozen journal original");
    auto arbitrary_current = field;
    auto unapplied = exact;
    unapplied.applied = false;
    require(!rebase_audio_patch_original_after_exact_override(
                arbitrary_current, unapplied)
            && arbitrary_current.original == 0,
        "arbitrary canonical current normalized without exact override");

    std::vector<AudioPatchRestoreField> frozen{
        AudioPatchRestoreField{&owner, 0x38, 1, 2, sizeof(uint64_t), "sound+0x38", false},
        field,
    };
    uint32_t matching_count = 0;
    require(rebase_unique_audio_patch_original_after_exact_override(
                frozen, exact, matching_count)
            && matching_count == 1 && frozen[1].original == canonical,
        "unique frozen owner metadata was not normalized");
    auto duplicate = frozen;
    duplicate[1] = field;
    duplicate.push_back(field);
    require(!rebase_unique_audio_patch_original_after_exact_override(
                duplicate, exact, matching_count)
            && matching_count == 2 && duplicate[1].original == 0
            && duplicate[2].original == 0,
        "duplicate owner patches did not fail without metadata mutation");

    AudioNaturalCompletionRetirementFacts completion;
    completion.route_custom_owned = true;
    completion.route_lease_valid = true;
    completion.route_controller_exact = true;
    completion.pre_stop_chain_read = true;
    completion.pre_stop_sound_null = true;
    completion.pre_stop_request_zero = true;
    completion.pre_stop_state_idle = true;
    completion.detached_present = true;
    completion.detached_restore_applied = true;
    completion.detached_route_predecessor = true;
    completion.detached_cleanup_generation_exact = true;
    completion.detached_sound_exact = true;
    completion.detached_request_available = true;
    completion.detached_request_exact = true;
    completion.detached_tokens_valid = true;
    completion.detached_owner_restored = true;
    completion.frozen_patch_valid = true;
    completion.frozen_route_predecessor = true;
    completion.frozen_lease_exact = true;
    completion.frozen_sound_exact = true;
    completion.journals_restored = true;
    completion.pause_resume_clear = true;
    completion.setup_clear = true;
    completion.registry_playback_exact = true;
    completion.registry_cleanup_clear_or_exact = true;
    completion.aggregate_snapshot_available = true;
    completion.active_aggregate_borrower = false;
    completion.canonical_proof_snapshot_available = true;
    completion.canonical_proof_active = false;
    require(classify_audio_natural_completion_retirement(completion)
                == AudioNaturalCompletionRetirementFailure::FrozenOwnerPatchMismatch,
        "unnormalized frozen owner metadata passed natural completion");
    completion.frozen_owner_patch_exact = owner_zero_bridge.original == canonical
        && owner_zero_bridge.replacement == 0;
    require(classify_audio_natural_completion_retirement(completion)
                == AudioNaturalCompletionRetirementFailure::None,
        "normalized frozen owner metadata did not pass natural completion");
}

void test_onmemory_bank_exact_owner_restore()
{
    using namespace ff7r::piano::game;
    const DecodedOnMemoryBankToken canonical{1, 0x21b, 0x21c};
    const DecodedOnMemoryBankToken custom{1, 0x222, 0x223};
    uint64_t backing = 0x44;
    uint64_t owner = custom.encode();
    std::vector<AudioPatchRestoreField> fields = {
        {&backing, 0x510, 0x33, 0x44, sizeof(uint64_t), "sound+0x510", true},
        {&owner, 0x548, 0, 0, sizeof(uint64_t), "sound+0x548", true},
    };
    struct AccessState {
        bool fail_backing_write = false;
    } state;
    AudioPatchRestoreAccess access;
    access.context = &state;
    access.read = [](const AudioPatchRestoreField& field, uint64_t& value, void*) {
        std::memcpy(&value, field.object, sizeof(value));
        return true;
    };
    access.write = [](const AudioPatchRestoreField& field, uint64_t value, void* context) {
        auto* state = static_cast<AccessState*>(context);
        if (state->fail_backing_write && field.offset == 0x510) return false;
        std::memcpy(field.object, &value, sizeof(value));
        return true;
    };
    access.preserve_native = [](const AudioPatchRestoreField&, uint64_t, void*) {};

    const auto make_override = [&] {
        AudioPatchRestoreExactOverride result;
        result.enabled = true;
        result.fresh_native_owner_proof = true;
        result.lookup_signature_valid = true;
        result.release_signature_valid = true;
        result.canonical_kind2_qualified = true;
        result.custom_kind2_qualified = true;
        result.object = &owner;
        result.offset = 0x548;
        result.size = sizeof(uint64_t);
        result.label = "sound+0x548";
        result.expected_journal_original = 0;
        result.expected_current = custom.encode();
        result.restore_value = canonical.encode();
        return result;
    };
    AudioPatchRestoreExactOverride exact = make_override();
    require(restore_audio_patches_reverse(fields, true, access, nullptr, &exact)
            && exact.applied && owner == canonical.encode() && backing == 0x33,
        "exact owner override did not write/verify canonical and ordinary fields");

    owner = custom.encode();
    backing = 0x44;
    state.fail_backing_write = true;
    exact = make_override();
    AudioPatchRestoreFailureReport report;
    require(!restore_audio_patches_reverse(fields, true, access, &report, &exact)
            && !exact.applied && owner == custom.encode() && backing == 0x44,
        "owner restore failure did not roll back exact inspected values");
    OnMemoryBankLifecycleState lifecycle;
    require(!lifecycle.active() && !lifecycle.release_in_flight(),
        "failed owner restoration created a detached release record");
}

void test_onmemory_bank_observed_null_backing()
{
    using namespace ff7r::piano::game;
    const OnMemoryBankSoundIdentity sound{
        reinterpret_cast<void*>(0x12000), {61, 71}};
    const DecodedOnMemoryBankToken canonical{1, 0x521, 0x721};
    const DecodedOnMemoryBankToken custom{1, 0x522, 0x722};
    constexpr uint64_t qualification_route = 31;
    constexpr uint64_t route = 32;
    constexpr uint64_t cleanup = 41;
    constexpr uint64_t request = 0x400010008ull;

    OnMemoryBankLifecycleState lifecycle;
    const OnMemoryBankPlaySetupPreflight play_setup =
        lifecycle.snapshot_play_setup(true, true, true, true,
            sound, canonical.encode(), qualification_route);
    require(play_setup.allowed()
            && lifecycle.commit_play_setup_qualification(
                play_setup.snapshot, sound, canonical.encode(),
                qualification_route, 2) == OnMemoryBankRouteDecision::Allowed,
        "null-backing PlaySetup could not qualify the exact native bank");

    uint64_t owner = custom.encode();
    std::vector<AudioPatchRestoreField> fields = {
        {&owner, 0x548, 0, 0, sizeof(uint64_t), "sound+0x548", true},
    };
    AudioPatchRestoreAccess access;
    access.read = [](const AudioPatchRestoreField& field, uint64_t& value, void*) {
        std::memcpy(&value, field.object, sizeof(value));
        return true;
    };
    access.write = [](const AudioPatchRestoreField& field, uint64_t value, void*) {
        std::memcpy(field.object, &value, sizeof(value));
        return true;
    };
    access.preserve_native = [](const AudioPatchRestoreField&, uint64_t, void*) {};
    AudioPatchRestoreExactOverride exact;
    const bool backing_observed = true;
    exact.enabled = backing_observed;
    exact.fresh_native_owner_proof = true;
    exact.lookup_signature_valid = true;
    exact.release_signature_valid = true;
    exact.canonical_kind2_qualified = true;
    exact.custom_kind2_qualified = true;
    exact.object = &owner;
    exact.offset = 0x548;
    exact.size = sizeof(uint64_t);
    exact.label = "sound+0x548";
    exact.expected_journal_original = 0;
    exact.expected_current = custom.encode();
    exact.restore_value = canonical.encode();
    require(restore_audio_patches_reverse(fields, true, access, nullptr, &exact)
            && exact.applied && owner == canonical.encode()
            && lifecycle.retain_detached(sound, custom, qualification_route,
                route, cleanup, request, nullptr, backing_observed,
                true, exact.applied, true, 2, 2)
            && lifecycle.active().backing_observed
            && lifecycle.active().backing_identity == nullptr,
        "observed-null backing blocked exact owner restore/publication/retention");

    OnMemoryBankLifecycleState unreadable;
    const OnMemoryBankPlaySetupPreflight unreadable_setup =
        unreadable.snapshot_play_setup(true, true, true, true,
            sound, canonical.encode(), qualification_route);
    bool unreadable_native_play_claimed = true;
    bool unreadable_publication_attempted = false;
    const bool unreadable_backing_observed = false;
    unreadable_native_play_claimed = unreadable_native_play_claimed
        && unreadable_backing_observed;
    if (unreadable_native_play_claimed) {
        unreadable_publication_attempted = true;
    }
    AudioPatchRestoreExactOverride unreadable_restore;
    unreadable_restore.enabled = unreadable_native_play_claimed
        && unreadable_backing_observed;
    require(unreadable_setup.allowed()
            && unreadable.commit_play_setup_qualification(
                unreadable_setup.snapshot, sound, canonical.encode(),
                qualification_route, 2) == OnMemoryBankRouteDecision::Allowed
            && !unreadable_native_play_claimed
            && !unreadable_publication_attempted
            && !unreadable_restore.enabled
            && !unreadable_restore.applied
            && !unreadable.retain_detached(sound, custom, qualification_route,
                route, cleanup, request, nullptr, false,
                true, true, true, 2, 2)
            && !unreadable.active(),
        "unreadable backing was treated as observed-null and retained");

    AudioStopRetirementObservation retired_present;
    retired_present.valid = true;
    retired_present.route_generation = route + 1;
    retired_present.lease_identity = {cleanup, 0x901};
    retired_present.controller = reinterpret_cast<void*>(0x13000);
    retired_present.slot = reinterpret_cast<void*>(0x14000);
    retired_present.bgm = reinterpret_cast<void*>(0x15000);
    retired_present.custom_sound = sound.object;
    retired_present.request_handle = request;
    retired_present.request_handle_retired = true;
    retired_present.retired_count = 1;
    constexpr uint64_t retirement_epoch = 81;
    AudioStopRetirementState retirement_monitor;
    OnMemoryBankRetiredBackingEvidence retained_backing;
    OnMemoryBankRetiredEntryEvidence single_null_entry;
    require(record_onmemory_bank_retired_entry(single_null_entry,
                request, request, true, sound.object, sound.object,
                true, nullptr)
            && single_null_entry.observed && !single_null_entry.failed
            && single_null_entry.backing == nullptr,
        "single readable observed-null retired entry was rejected");

    OnMemoryBankRetiredEntryEvidence duplicate_identical;
    require(record_onmemory_bank_retired_entry(duplicate_identical,
                request, request, true, sound.object, sound.object,
                true, nullptr)
            && !record_onmemory_bank_retired_entry(duplicate_identical,
                request, request, true, sound.object, sound.object,
                true, nullptr)
            && duplicate_identical.failed,
        "duplicate identical exact retired handles were not rejected");
    OnMemoryBankRetiredEntryEvidence unreadable_sound_entry;
    OnMemoryBankRetiredEntryEvidence unreadable_backing_entry;
    OnMemoryBankRetiredEntryEvidence different_sound_entry;
    require(!record_onmemory_bank_retired_entry(unreadable_sound_entry,
                request, request, false, nullptr, sound.object, true, nullptr)
            && unreadable_sound_entry.failed
            && !record_onmemory_bank_retired_entry(unreadable_backing_entry,
                request, request, true, sound.object, sound.object, false, nullptr)
            && unreadable_backing_entry.failed
            && !record_onmemory_bank_retired_entry(unreadable_backing_entry,
                request, request, true, sound.object, sound.object, true, nullptr)
            && unreadable_backing_entry.failed
            && !record_onmemory_bank_retired_entry(different_sound_entry,
                request, request, true, reinterpret_cast<void*>(0x17000),
                sound.object, true, nullptr)
            && different_sound_entry.failed,
        "unreadable or different-sound exact retired entry was accepted");
    OnMemoryBankRetiredEntryEvidence different_backing_entry;
    require(record_onmemory_bank_retired_entry(different_backing_entry,
                request, request, true, sound.object, sound.object,
                true, reinterpret_cast<void*>(0x17100))
            && !record_onmemory_bank_retired_entry(different_backing_entry,
                request, request, true, sound.object, sound.object,
                true, reinterpret_cast<void*>(0x17200))
            && different_backing_entry.failed,
        "duplicate different nonnull backing did not poison the scan");
    OnMemoryBankRetiredEntryEvidence mixed_backing_entry;
    require(record_onmemory_bank_retired_entry(mixed_backing_entry,
                request, request, true, sound.object, sound.object,
                true, nullptr)
            && !record_onmemory_bank_retired_entry(mixed_backing_entry,
                request, request, true, sound.object, sound.object,
                true, reinterpret_cast<void*>(0x17300))
            && mixed_backing_entry.failed,
        "duplicate mixed null/non-null backing did not poison the scan");
    const OnMemoryBankRetiredBackingObservation observed_null{
        route + 1, cleanup, retirement_epoch, request, sound.live,
        true, true, single_null_entry.backing};
    AudioStopRetirementObservation initially_absent = retired_present;
    initially_absent.request_handle_retired = false;
    const OnMemoryBankRetiredBackingEvidence initially_empty = retained_backing;
    const OnMemoryBankRetiredBackingObservation absent_observation{
        route + 1, cleanup, retirement_epoch, request, sound.live,
        false, false, nullptr};
    require(!record_onmemory_bank_retired_backing(
                retained_backing, absent_observation)
            && same_onmemory_bank_retired_backing_evidence(
                retained_backing, initially_empty),
        "initial absence mutated or authorized null-backing evidence");
    AudioStopRetirementObservation monitor_start = initially_absent;
    monitor_start.request_handle_retired = true;
    require(begin_audio_stop_retirement_monitor(
                retirement_monitor, monitor_start)
            && advance_audio_stop_retirement_monitor(
                retirement_monitor, monitor_start, 5)
                == AudioStopRetirementPhase::Waiting
            && record_onmemory_bank_retired_backing(
                retained_backing, observed_null)
            && record_onmemory_bank_retired_backing(
                retained_backing, observed_null),
        "initially absent null request did not retain later matching evidence");
    void* proven_retired_backing = reinterpret_cast<void*>(1);
    require(!onmemory_bank_exact_request_retired(retained_backing,
                route + 1, cleanup, retirement_epoch, request, sound.live,
                false, proven_retired_backing)
            && advance_audio_stop_retirement_monitor(
                retirement_monitor, retired_present, 5)
                == AudioStopRetirementPhase::Waiting,
        "present exact request prematurely authorized null-backed release");

    AudioStopRetirementObservation retired_absent = retired_present;
    retired_absent.request_handle_retired = false;
    retired_absent.current_request_handle = request + 1;
    retired_absent.current_sound = reinterpret_cast<void*>(0x16000);
    retired_absent.current_state = 4;
    require(advance_audio_stop_retirement_monitor(
                retirement_monitor, retired_absent, 5)
                == AudioStopRetirementPhase::Quiescent
            && onmemory_bank_exact_request_retired(retained_backing,
                route + 1, cleanup, retirement_epoch, request, sound.live,
                true, proven_retired_backing)
            && proven_retired_backing == nullptr,
        "quiescent exact disappearance did not prove observed-null backing");

    OnMemoryBankRetiredBackingEvidence never_seen;
    AudioStopRetirementState never_seen_monitor;
    require(begin_audio_stop_retirement_monitor(
                never_seen_monitor, monitor_start)
            && advance_audio_stop_retirement_monitor(
                never_seen_monitor, initially_absent, 5)
                == AudioStopRetirementPhase::Quiescent
            && !onmemory_bank_exact_request_retired(never_seen,
                route + 1, cleanup, retirement_epoch, request, sound.live,
                true, proven_retired_backing),
        "quiescence without prior request presence authorized retirement");
    AudioStopRetirementState never_seen_wait;
    AudioStopRetirementObservation artificial_wait = initially_absent;
    require(onmemory_bank_retirement_presence_pending(never_seen,
                artificial_wait.valid, artificial_wait.request_handle_retired),
        "never-present request was not classified for bounded artificial wait");
    artificial_wait.request_handle_retired = true;
    require(begin_audio_stop_retirement_monitor(
                never_seen_wait, artificial_wait)
            && advance_audio_stop_retirement_monitor(
                never_seen_wait, artificial_wait, 2)
                == AudioStopRetirementPhase::Waiting
            && advance_audio_stop_retirement_monitor(
                never_seen_wait, artificial_wait, 2)
                == AudioStopRetirementPhase::TimedOut
            && !never_seen,
        "never-present artificial wait did not time out without evidence");
    OnMemoryBankRetiredBackingEvidence unreadable_evidence;
    OnMemoryBankRetiredBackingObservation unreadable_observation = observed_null;
    unreadable_observation.backing_observed = false;
    require(!record_onmemory_bank_retired_backing(
                unreadable_evidence, unreadable_observation)
            && !onmemory_bank_exact_request_retired(unreadable_evidence,
                route + 1, cleanup, retirement_epoch, request, sound.live,
                true, proven_retired_backing),
        "unreadable retired backing authorized retirement");
    OnMemoryBankRetiredBackingEvidence mismatched_evidence = retained_backing;
    OnMemoryBankRetiredBackingObservation mismatched_observation = observed_null;
    mismatched_observation.backing = reinterpret_cast<void*>(0x17000);
    require(!record_onmemory_bank_retired_backing(
                mismatched_evidence, mismatched_observation)
            && mismatched_evidence.failed
            && !onmemory_bank_exact_request_retired(mismatched_evidence,
                route + 1, cleanup, retirement_epoch, request, sound.live,
                true, proven_retired_backing),
        "mismatched repeated backing evidence overwrote retained evidence");
    require(!onmemory_bank_exact_request_retired(retained_backing,
                route + 2, cleanup, retirement_epoch, request, sound.live,
                true, proven_retired_backing)
            && !onmemory_bank_exact_request_retired(retained_backing,
                route + 1, cleanup + 1, retirement_epoch, request, sound.live,
                true, proven_retired_backing)
            && !onmemory_bank_exact_request_retired(retained_backing,
                route + 1, cleanup, retirement_epoch + 1, request, sound.live,
                true, proven_retired_backing)
            && !onmemory_bank_exact_request_retired(retained_backing,
                route + 1, cleanup, retirement_epoch, request + 1, sound.live,
                true, proven_retired_backing),
        "stale generation, cleanup epoch, monitor epoch, or request authorized retirement");
    OnMemoryBankRetiredBackingEvidence reset_evidence = retained_backing;
    reset_evidence = {};
    require(!onmemory_bank_exact_request_retired(reset_evidence,
                route + 1, cleanup, retirement_epoch, request, sound.live,
                true, proven_retired_backing),
        "reset retained old retirement backing evidence");
    OnMemoryBankRetiredBackingEvidence replacement_evidence;
    OnMemoryBankRetiredBackingObservation replacement_observation = observed_null;
    replacement_observation.monitor_epoch = retirement_epoch + 1;
    replacement_observation.request_handle = request + 1;
    require(record_onmemory_bank_retired_backing(
                replacement_evidence, replacement_observation)
            && !onmemory_bank_exact_request_retired(replacement_evidence,
                route + 1, cleanup, retirement_epoch, request, sound.live,
                true, proven_retired_backing),
        "new retirement reused prior request evidence");
    AudioStopRetirementState timed_out_monitor;
    OnMemoryBankRetiredBackingEvidence timed_out_evidence;
    require(begin_audio_stop_retirement_monitor(
                timed_out_monitor, retired_present)
            && record_onmemory_bank_retired_backing(
                timed_out_evidence, observed_null)
            && advance_audio_stop_retirement_monitor(
                timed_out_monitor, retired_present, 1)
                == AudioStopRetirementPhase::TimedOut,
        "retirement evidence timeout setup failed");
    timed_out_evidence = {};
    require(!onmemory_bank_exact_request_retired(timed_out_evidence,
                route + 1, cleanup, retirement_epoch, request, sound.live,
                true, proven_retired_backing),
        "timed-out retirement retained release authority");

    void* const retained_backing_a = reinterpret_cast<void*>(0x7100);
    void* const unrelated_backing_b = reinterpret_cast<void*>(0x7200);
    require(!onmemory_bank_current_backing_detached(
                retained_backing_a, true, retained_backing_a, true)
            && onmemory_bank_current_backing_detached(
                retained_backing_a, true, unrelated_backing_b, true)
            && onmemory_bank_current_backing_detached(
                retained_backing_a, true, nullptr, true)
            && onmemory_bank_current_backing_detached(
                nullptr, true, nullptr, true)
            && onmemory_bank_current_backing_detached(
                nullptr, true, unrelated_backing_b, true),
        "observed backing detachment truth table drifted");
    require(!onmemory_bank_current_backing_detached(
                retained_backing_a, false, unrelated_backing_b, true)
            && !onmemory_bank_current_backing_detached(
                retained_backing_a, true, unrelated_backing_b, false)
            && !onmemory_bank_current_backing_detached(
                nullptr, false, nullptr, true)
            && !onmemory_bank_current_backing_detached(
                nullptr, true, nullptr, false),
        "unreadable backing was treated as observed absence");

    OnMemoryBankRetirementFacts facts;
    facts.route_generation = route + 1;
    facts.cleanup_generation = cleanup;
    facts.retired_request_handle = request;
    facts.current_request_handle = request + 1;
    facts.current_backing = nullptr;
    facts.retired_backing = proven_retired_backing;
    facts.current_backing_observed = true;
    facts.retired_backing_observed = true;
    facts.current_owner = classify_onmemory_bank_retirement_owner(
        lifecycle.active(), sound, true, true, canonical.encode());
    facts.exact_request_retired = true;
    facts.route_released = true;
    facts.playback_released = true;
    facts.cleanup_released = true;
    facts.owner_restore_verified = true;
    facts.runtime_installed = true;
    facts.lookup_signature_valid = true;
    facts.release_signature_valid = true;

    const auto require_rejected_without_native_work = [&] (
        const OnMemoryBankRetirementFacts& rejected,
        const char* message) {
        OnMemoryBankReleaseAction action;
        require(!lifecycle.claim_release(rejected, action)
                && !action
                && lifecycle.active().phase
                    == OnMemoryBankLifecyclePhase::RestoreApplied
                && !lifecycle.release_in_flight(),
            message);
    };
    OnMemoryBankRetirementFacts rejected = facts;
    rejected.exact_request_retired = false;
    require_rejected_without_native_work(rejected,
        "null backing released without exact request retirement");
    rejected = facts;
    rejected.current_owner = classify_onmemory_bank_retirement_owner(
        lifecycle.active(), sound, true, true, custom.encode());
    require_rejected_without_native_work(rejected,
        "null backing released after owner drift");
    rejected = facts;
    ++rejected.route_generation;
    require_rejected_without_native_work(rejected,
        "null backing released after generation drift");
    rejected = facts;
    rejected.playback_released = false;
    require_rejected_without_native_work(rejected,
        "null backing released with active playback");
    rejected = facts;
    rejected.cleanup_released = false;
    require_rejected_without_native_work(rejected,
        "null backing released with active cleanup");
    rejected = facts;
    rejected.retired_backing_observed = false;
    require_rejected_without_native_work(rejected,
        "unreadable retired backing was treated as observed-null");
    rejected = facts;
    rejected.current_backing_observed = false;
    require_rejected_without_native_work(rejected,
        "unreadable replacement backing was treated as observed-null");

    OnMemoryBankReleaseAction action;
    require(lifecycle.claim_release(facts, action) && action,
        "exact observed-null retirement did not claim release once");
    int lookup_calls = 0;
    int release_calls = 0;
    const OnMemoryBankReleaseExecution execution = execute_onmemory_bank_release(
        true, true, false, action,
        [&](uint64_t token) {
            ++lookup_calls;
            return token == canonical.encode() || token == custom.encode() ? 2U : 3U;
        },
        [&](const uint64_t* token, uint8_t asynchronous) {
            require(token && *token == custom.encode() && asynchronous == 1,
                "null-backing release lost exact request token or async mode");
            ++release_calls;
            return OnMemoryBankNativeReleaseResult{true, 0};
        });
    require(execution.outcome == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
            && lookup_calls == 2 && release_calls == 1
            && lifecycle.finish_release(action, execution.outcome)
            && !lifecycle.finish_release(action, execution.outcome),
        "observed-null release was retried or skipped exact native work");
    OnMemoryBankPendingObservation pending;
    require(lifecycle.copy_pending_observation(pending)
            && lifecycle.observe(pending, 2, 2)
            && lifecycle.copy_pending_observation(pending)
            && lifecycle.observe(pending, 2, 0)
            && lifecycle.active().phase == OnMemoryBankLifecyclePhase::Complete,
        "observed-null release did not complete kind2-to-zero observation");
}

void test_frozen_profile_lease_lifecycle()
{
    using namespace ff7r::piano::game;
    require(evaluate_audio_patch_restore(10, 10, 20, false)
            == AudioPatchRestoreDecision::AlreadyRestored,
        "restored patch journal was not retry-idempotent");
    require(evaluate_audio_patch_restore(20, 10, 20, false)
            == AudioPatchRestoreDecision::RestoreOriginal,
        "active patch journal did not require original restoration");
    require(evaluate_audio_patch_restore(30, 10, 20, false)
            == AudioPatchRestoreDecision::Conflict,
        "route-change rollback conflict did not retain the journal");
    require(evaluate_audio_patch_restore(30, 10, 20, true)
            == AudioPatchRestoreDecision::NativeOwned,
        "native-owned route change was not distinguished from restoration conflict");
    constexpr uint64_t original_mabf_source = 0x7fee00001000ull;
    constexpr uint64_t custom_sidecar_source = 0x7fee00002000ull;
    require(evaluate_audio_patch_restore(custom_sidecar_source, original_mabf_source,
                custom_sidecar_source, false)
            == AudioPatchRestoreDecision::RestoreOriginal,
        "stale custom MABF source was not scheduled for exact restoration");
    require(evaluate_audio_patch_restore(0x22200080001ull, 0x11100080001ull, 0, true)
            == AudioPatchRestoreDecision::NativeOwned,
        "shipping sound +0x548 third-party transition was speculatively overwritten");

    SongDescriptor song = make_song("lease", 3);
    song.profiles.push_back({});
    SongRegistry& registry_under_test = registry();
    registry_under_test.replace({song});
    FrozenProfileLeaseState lease;
    const auto profile_cycle_allowed = [&] {
        return registry_under_test.cycle_active_profile(1)
            || registry_under_test.cycle_active_profile(-1);
    };
    uint64_t generation = 40;
    const auto begin_route = [&](bool native_attempted) {
        registry_under_test.set_active_selection(3, 0);
        require(registry_under_test.freeze_active_profile(), "failed to freeze cleanup-policy profile");
        const AudioRouteLeaseIdentity identity{++generation, 0xabc};
        lease.acquire(identity);
        if (native_attempted) {
            require(lease.mark_native_arm_attempt(identity), "native patch attempt was not recorded");
        }
        return identity;
    };
    const auto apply = [&](AudioRouteLeaseIdentity identity, const AudioCleanupEvidence& evidence) {
        const AudioRouteCleanupResult result = apply_audio_cleanup(lease, identity, evidence);
        if (result.thaw_profile) registry_under_test.clear_frozen_profile();
        return result;
    };
    const auto require_retained = [&](const AudioRouteCleanupResult& result, const char* message) {
        require(result.status == AudioRouteCleanupStatus::Retained
                && !result.thaw_profile && !result.clear_route_metadata
                && lease.active() && lease.cleanup_metadata_retained()
                && !registry_under_test.cycle_active_profile(1)
                && !registry_under_test.cycle_active_profile(-1),
            message);
    };
    const auto armed_evidence = [] {
        AudioCleanupEvidence value;
        value.immutable_identity_matches = true;
        value.native_attempted = true;
        value.ambiguous_route_state = true;
        return value;
    };

    const AudioNativeRouteObservation owned_route{
        reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000),
        reinterpret_cast<void*>(0x3000), 0x400000008ull, 4, true,
    };
    AudioNativeRouteObservation replaced_route{
        owned_route.slot, owned_route.bgm, reinterpret_cast<void*>(0x5000),
        0x500000008ull, 2, true,
    };
    require(native_route_replacement_proven(
            owned_route, replaced_route, replaced_route.sound),
        "verified native Set replacement did not release custom ownership");
    replaced_route.request_handle = owned_route.request_handle;
    require(!native_route_replacement_proven(
            owned_route, replaced_route, replaced_route.sound),
        "native replacement accepted a still-owned custom resource");
    replaced_route.request_handle = 0x500000008ull;
    replaced_route.valid = false;
    require(!native_route_replacement_proven(
            owned_route, replaced_route, replaced_route.sound),
        "unreadable native replacement was treated as verified");

    AudioRouteLeaseIdentity route = begin_route(false);
    AudioPatchJournalState active_patch_journal;
    require(active_patch_journal.begin(lease, route),
        "production patch policy did not record native attempt before first write");
    AudioCleanupEvidence evidence = armed_evidence();
    evidence.active_journal_empty = active_patch_journal.empty();
    evidence.failed_journal_empty = false;
    require_retained(apply(route, evidence),
        "partial native patch failure before arm completion released the profile");
    active_patch_journal.finish();
    evidence.active_journal_empty = active_patch_journal.empty();
    require_retained(apply(route, evidence),
        "failed patch journal permitted list-return release");
    require(lease.identity() == route, "partial patch failure replaced immutable route identity");

    evidence.failed_journal_empty = true;
    evidence.pending_journal_empty = false;
    require_retained(apply(route, evidence),
        "route-change rollback failure discarded the pending journal lease");
    evidence.native_clear_verified = true;
    evidence.route_state_unchanged = true;
    require_retained(apply(route, evidence),
        "successful native clear committed while a pending journal remained");
    evidence.pending_journal_empty = true;
    const auto pending_retry = apply(route, evidence);
    require(pending_retry.status == AudioRouteCleanupStatus::Released
            && pending_retry.clear_route_metadata && !lease.active()
            && profile_cycle_allowed(),
        "pending journal retry did not atomically commit verified cleanup");

    route = begin_route(true);
    evidence = armed_evidence();
    evidence.failed_journal_empty = false;
    evidence.native_clear_verified = true;
    evidence.route_state_unchanged = true;
    require_retained(apply(route, evidence),
        "successful native clear plus failed journal thawed the profile");
    evidence.failed_journal_empty = true;
    const auto journal_retry = apply(route, evidence);
    require(journal_retry.status == AudioRouteCleanupStatus::Released,
        "failed-journal retry did not commit retained native-clear proof");
    require(!lease.active(), "failed-journal retry retained the route lease after commit");
    require(profile_cycle_allowed(),
        "failed-journal retry left the registry profile frozen");

    route = begin_route(true);
    evidence = armed_evidence();
    evidence.operation = AudioCleanupOperation::Shutdown;
    evidence.native_clear_verified = true;
    evidence.route_state_unchanged = true;
    evidence.hooks_disabled = false;
    require_retained(apply(route, evidence),
        "native clear committed after hook-disable failure");
    evidence.hooks_disabled = true;
    evidence.callbacks_drained = false;
    require_retained(apply(route, evidence),
        "native clear committed after callback-drain failure");
    evidence.callbacks_drained = true;
    evidence.auxiliary_journals_restored = false;
    require_retained(apply(route, evidence),
        "native clear committed after auxiliary journal failure");
    evidence.auxiliary_journals_restored = true;
    const auto shutdown = apply(route, evidence);
    require(shutdown.status == AudioRouteCleanupStatus::Released
            && shutdown.thaw_profile && shutdown.clear_route_metadata
            && !lease.active() && profile_cycle_allowed(),
        "complete two-phase shutdown did not commit cleanup");

    route = begin_route(false);
    AudioCleanupEvidence no_route;
    no_route.immutable_identity_matches = true;
    const auto verified_no_route = apply(route, no_route);
    require(verified_no_route.status == AudioRouteCleanupStatus::VerifiedNoRoute
            && !lease.active() && profile_cycle_allowed(),
        "verified never-armed route did not thaw after clear prerequisites");

    route = begin_route(true);
    const auto stale_substrate_relinquishment = lease.transition(
        AudioRouteCleanupEvent::CanonicalSubstrateRelinquished,
        {route.generation + 1, route.song_key});
    require(stale_substrate_relinquishment.status == AudioRouteCleanupStatus::Retained
            && lease.active(),
        "stale canonical substrate lease released frozen-profile ownership");
    const auto substrate_relinquished = lease.transition(
        AudioRouteCleanupEvent::CanonicalSubstrateRelinquished, route);
    require(substrate_relinquished.status == AudioRouteCleanupStatus::Released
            && substrate_relinquished.thaw_profile
            && substrate_relinquished.clear_route_metadata
            && !lease.active(),
        "canonical substrate relinquishment did not release only mod route metadata");
    registry_under_test.clear_frozen_profile();
    require(profile_cycle_allowed(),
        "canonical substrate relinquishment left the registry profile frozen");
}

} // namespace ff7r::piano::tests::runtime_lifecycle
