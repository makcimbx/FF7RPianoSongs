#pragma once

#include "game/audio_cleanup_policy.h"
#include "game/audio_memory_read_policy.h"
#include "game/selection_audio_policy.h"

#include <cstdint>
#include <type_traits>

namespace ff7r::piano::game {

// Temporary Debug-only authority carried from an exact completed canonical
// relinquishment. It never owns or carries the released custom token.
enum class CanonicalSubstrateBridgePhase : uint8_t {
    None,
    Available,
    Reserved,
    Armed,
    StopObserved,
    OwnerZeroQualified,
    PatchedOriginalInFlight,
    SetBound,
    Published,
    Failed,
};

struct CanonicalSubstrateBridgeAuthority {
    CanonicalSubstrateBridgePhase phase = CanonicalSubstrateBridgePhase::None;
    uint64_t generation = 0;
    uint64_t transaction_generation = 0;
    uint64_t source_ordinal = 0;
    uint64_t source_version = 0;
    uint64_t source_collection_version = 0;
    uint64_t list_exit_epoch = 0;
    uint64_t release_completion_epoch = 0;
    uint64_t revocation_epoch = 0;
    uint64_t canonical_token = 0;
    void* expected_callback_sound = nullptr;
    UObjectLiveHandle expected_callback_sound_identity{};
    void* old_canonical_sound = nullptr;
    UObjectLiveHandle old_canonical_sound_identity{};
    uint64_t old_canonical_request = 0;
    void* controller = nullptr;
    ControllerIdentityProof controller_proof{};
    void* slot = nullptr;
    void* bgm = nullptr;
    uint64_t predecessor_route_generation = 0;
    AudioRouteLeaseIdentity predecessor_lease{};
    uint64_t predecessor_lifecycle_epoch = 0;
    uint64_t selection_generation = 0;
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease{};
    uint64_t song_key = 0;

    constexpr explicit operator bool() const noexcept
    {
        return phase != CanonicalSubstrateBridgePhase::None
            && phase != CanonicalSubstrateBridgePhase::Failed
            && generation != 0 && transaction_generation != 0
            && source_ordinal != 0 && source_version != 0
            && source_collection_version != 0 && list_exit_epoch != 0
            && release_completion_epoch != 0 && revocation_epoch != 0
            && canonical_token != 0 && expected_callback_sound
            && private_object_handle_valid(expected_callback_sound_identity)
            && old_canonical_sound
            && private_object_handle_valid(old_canonical_sound_identity)
            && old_canonical_request != 0 && controller
            && controller_identity_proof_valid(controller_proof)
            && slot && bgm && predecessor_route_generation != 0
            && predecessor_lease.generation != 0
            && predecessor_lease.song_key != 0
            && predecessor_lifecycle_epoch != 0;
    }
};

inline bool canonical_substrate_bridge_authority_matches(
    const CanonicalSubstrateBridgeAuthority& expected,
    const CanonicalSubstrateBridgeAuthority& current) noexcept;

static_assert(std::is_trivially_copyable_v<CanonicalSubstrateBridgeAuthority>);

enum class CanonicalSubstrateResetLineagePhase : uint8_t {
    None,
    Qualified,
    Reserved,
};

// Exact, one-use authority for the game-owned list-return metadata reset from
// an observed nonzero idle route to the canonical zero/Idle route. It carries
// no ownership token; all fields are retained identity/lineage observations.
struct CanonicalSubstrateResetLineageAuthority {
    CanonicalSubstrateResetLineagePhase phase =
        CanonicalSubstrateResetLineagePhase::None;
    uint64_t generation = 0;
    uint64_t rearm_epoch = 0;
    uint64_t reset_observation_generation = 0;
    uint64_t reset_route_generation = 0;
    AudioRouteLeaseIdentity reset_lease{};
    uint64_t reset_lifecycle_epoch = 0;
    uint64_t transaction_generation = 0;
    uint64_t source_ordinal = 0;
    uint64_t source_version = 0;
    uint64_t source_collection_version = 0;
    uint64_t list_exit_epoch = 0;
    uint64_t proof_route_generation = 0;
    AudioRouteLeaseIdentity proof_lease{};
    uint64_t proof_lifecycle_epoch = 0;
    uint64_t request = 0;
    uint64_t canonical_token = 0;
    uint64_t bridge_generation = 0;
    uint64_t bridge_release_epoch = 0;
    void* controller = nullptr;
    ControllerIdentityProof controller_proof{};
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    UObjectLiveHandle sound_identity{};
};

constexpr bool canonical_empty_private_object_handle(
    const UObjectLiveHandle& handle) noexcept
{
    return handle.internal_index == -1 && handle.serial_number == 0;
}

constexpr bool canonical_empty_controller_identity_proof(
    const ControllerIdentityProof& proof) noexcept
{
    return proof.mode == ControllerIdentityProofMode::Invalid
        && !proof.controller && !proof.object_class
        && proof.name_comparison_id == 0 && proof.name_number == 0
        && !proof.outer && !proof.raw_internal_index_readable
        && proof.raw_internal_index == -1 && !proof.live_capture_succeeded
        && canonical_empty_private_object_handle(proof.live)
        && !proof.item_backed_zero_serial_capture_succeeded
        && proof.item_backed_zero_serial.internal_index == -1
        && proof.item_backed_zero_serial.serial_number == 0;
}

constexpr bool canonical_substrate_reset_lineage_authority_empty(
    const CanonicalSubstrateResetLineageAuthority& authority) noexcept
{
    return authority.phase == CanonicalSubstrateResetLineagePhase::None
        && authority.generation == 0 && authority.rearm_epoch == 0
        && authority.reset_observation_generation == 0
        && authority.reset_route_generation == 0
        && authority.reset_lease.generation == 0
        && authority.reset_lease.song_key == 0
        && authority.reset_lifecycle_epoch == 0
        && authority.transaction_generation == 0
        && authority.source_ordinal == 0 && authority.source_version == 0
        && authority.source_collection_version == 0
        && authority.list_exit_epoch == 0
        && authority.proof_route_generation == 0
        && authority.proof_lease.generation == 0
        && authority.proof_lease.song_key == 0
        && authority.proof_lifecycle_epoch == 0 && authority.request == 0
        && authority.canonical_token == 0 && authority.bridge_generation == 0
        && authority.bridge_release_epoch == 0 && !authority.controller
        && canonical_empty_controller_identity_proof(authority.controller_proof)
        && !authority.slot && !authority.bgm && !authority.sound
        && canonical_empty_private_object_handle(authority.sound_identity);
}

// Liveness of the route predecessor a retained canonical substrate proof
// observes, and therefore the retirement rule for the proof itself.
//
// A proof names exactly one route predecessor: the route generation observed
// when the canonical relinquishment it describes was captured. That
// predecessor is still reachable in exactly two ways, matching the two
// branches of canonical_substrate_route_predecessor_exact below:
//   - the current route generation is its exact successor, so the proof
//     describes the generation the route just left; or
//   - a reset-lineage authority has been established (phase != None), so the
//     observation was carried across the route reset that followed it.
// When neither holds, the generation the proof names was superseded by route
// transitions the mod never observed and no later generation can restore it.
// The predecessor no longer exists, the proof is dead, and it can only refuse
// every subsequent activation.
//
// Retiring a dead proof discards retained observations and nothing else. The
// reset lineage "carries no ownership token; all fields are retained
// identity/lineage observations" and the bridge authority likewise never owns
// the released custom token (see both authority headers above), so retirement
// releases no bank, reverts no native mutation, and completes no lineage. It
// is deliberately weaker than rebasing: it refuses to assert that the
// unobserved intervening transitions were authenticated, and instead lets the
// next activation capture a fresh proof from live state.
//
// The rule is strictly contained by the use predicate: whenever it reports
// dead, normal_successor is false (it requires the same successor test) and
// authority_phase_exact is false (it requires phase == Qualified), so
// canonical_substrate_route_predecessor_exact would already have rejected the
// proof on both branches. Retirement can never remove a usable proof.
constexpr bool canonical_substrate_proof_route_predecessor_live(
    const uint64_t proof_route_generation,
    const uint64_t current_route_generation,
    const CanonicalSubstrateResetLineagePhase authority_phase) noexcept
{
    return (proof_route_generation != 0
               && proof_route_generation != UINT64_MAX
               && current_route_generation == proof_route_generation + 1)
        || authority_phase != CanonicalSubstrateResetLineagePhase::None;
}

constexpr bool canonical_substrate_bridge_authority_empty(
    const CanonicalSubstrateBridgeAuthority& authority) noexcept
{
    return authority.phase == CanonicalSubstrateBridgePhase::None
        && authority.generation == 0 && authority.transaction_generation == 0
        && authority.source_ordinal == 0 && authority.source_version == 0
        && authority.source_collection_version == 0
        && authority.list_exit_epoch == 0
        && authority.release_completion_epoch == 0
        && authority.revocation_epoch == 0 && authority.canonical_token == 0
        && !authority.expected_callback_sound
        && canonical_empty_private_object_handle(
            authority.expected_callback_sound_identity)
        && !authority.old_canonical_sound
        && canonical_empty_private_object_handle(
            authority.old_canonical_sound_identity)
        && authority.old_canonical_request == 0 && !authority.controller
        && canonical_empty_controller_identity_proof(authority.controller_proof)
        && !authority.slot && !authority.bgm
        && authority.predecessor_route_generation == 0
        && authority.predecessor_lease.generation == 0
        && authority.predecessor_lease.song_key == 0
        && authority.predecessor_lifecycle_epoch == 0
        && authority.selection_generation == 0
        && authority.route_generation == 0
        && authority.lease.generation == 0 && authority.lease.song_key == 0
        && authority.song_key == 0;
}

inline bool canonical_substrate_bridge_historical_authority_valid(
    const CanonicalSubstrateBridgeAuthority& authority) noexcept
{
    return authority.generation != 0 && authority.transaction_generation != 0
        && authority.source_ordinal != 0 && authority.source_version != 0
        && authority.source_collection_version != 0
        && authority.list_exit_epoch != 0
        && authority.release_completion_epoch != 0
        && authority.canonical_token != 0 && authority.expected_callback_sound
        && private_object_handle_valid(authority.expected_callback_sound_identity)
        && authority.old_canonical_sound
        && private_object_handle_valid(authority.old_canonical_sound_identity)
        && authority.old_canonical_request != 0 && authority.controller
        && controller_identity_proof_valid(authority.controller_proof)
        && authority.slot && authority.bgm
        && authority.predecessor_route_generation != 0
        && authority.predecessor_lease.generation != 0
        && authority.predecessor_lease.song_key != 0
        && authority.predecessor_lifecycle_epoch != 0;
}

inline bool canonical_substrate_bridge_available_authority_valid(
    const CanonicalSubstrateBridgeAuthority& authority) noexcept
{
    return authority.phase == CanonicalSubstrateBridgePhase::Available
        && canonical_substrate_bridge_historical_authority_valid(authority)
        && authority.revocation_epoch == 0
        && authority.selection_generation == 0
        && authority.route_generation == 0 && !authority.lease.valid()
        && authority.song_key == 0;
}

inline bool canonical_substrate_bridge_reserved_authority_valid(
    const CanonicalSubstrateBridgeAuthority& authority) noexcept
{
    return authority.phase == CanonicalSubstrateBridgePhase::Reserved
        && canonical_substrate_bridge_historical_authority_valid(authority)
        && authority.revocation_epoch != 0
        && authority.selection_generation == 0
        && authority.route_generation == 0 && !authority.lease.valid()
        && authority.song_key == 0;
}

inline bool canonical_substrate_bridge_predecessor_stage_exact(
    const CanonicalSubstrateBridgeAuthority& authority,
    const CanonicalSubstrateBridgePhase expected_phase) noexcept
{
    if (canonical_substrate_bridge_authority_empty(authority)) return true;
    return expected_phase == CanonicalSubstrateBridgePhase::Available
        ? canonical_substrate_bridge_available_authority_valid(authority)
        : expected_phase == CanonicalSubstrateBridgePhase::Reserved
            && canonical_substrate_bridge_reserved_authority_valid(authority);
}

inline bool canonical_substrate_bridge_reservation_successor_exact(
    const CanonicalSubstrateBridgeAuthority& available,
    const CanonicalSubstrateBridgeAuthority& reserved) noexcept
{
    if (!canonical_substrate_bridge_available_authority_valid(available)
        || !canonical_substrate_bridge_reserved_authority_valid(reserved)) {
        return false;
    }
    auto expected = available;
    expected.phase = CanonicalSubstrateBridgePhase::Reserved;
    expected.revocation_epoch = reserved.revocation_epoch;
    return expected.phase == reserved.phase
        && expected.generation == reserved.generation
        && expected.transaction_generation == reserved.transaction_generation
        && expected.source_ordinal == reserved.source_ordinal
        && expected.source_version == reserved.source_version
        && expected.source_collection_version
            == reserved.source_collection_version
        && expected.list_exit_epoch == reserved.list_exit_epoch
        && expected.release_completion_epoch == reserved.release_completion_epoch
        && expected.revocation_epoch == reserved.revocation_epoch
        && expected.canonical_token == reserved.canonical_token
        && expected.expected_callback_sound == reserved.expected_callback_sound
        && private_object_handle_matches(
            expected.expected_callback_sound_identity,
            reserved.expected_callback_sound_identity)
        && expected.old_canonical_sound == reserved.old_canonical_sound
        && private_object_handle_matches(expected.old_canonical_sound_identity,
            reserved.old_canonical_sound_identity)
        && expected.old_canonical_request == reserved.old_canonical_request
        && expected.controller == reserved.controller
        && controller_identity_proof_matches(
            expected.controller_proof, reserved.controller_proof)
        && expected.slot == reserved.slot && expected.bgm == reserved.bgm
        && expected.predecessor_route_generation
            == reserved.predecessor_route_generation
        && expected.predecessor_lease == reserved.predecessor_lease
        && expected.predecessor_lifecycle_epoch
            == reserved.predecessor_lifecycle_epoch
        && expected.selection_generation == reserved.selection_generation
        && expected.route_generation == reserved.route_generation
        && expected.lease == reserved.lease
        && expected.song_key == reserved.song_key;
}

inline bool canonical_substrate_bridge_authority_matches(
    const CanonicalSubstrateBridgeAuthority& expected,
    const CanonicalSubstrateBridgeAuthority& current) noexcept
{
    if (expected.phase != current.phase) return false;
    const bool stage_valid = expected.phase == CanonicalSubstrateBridgePhase::None
        ? canonical_substrate_bridge_authority_empty(expected)
            && canonical_substrate_bridge_authority_empty(current)
        : expected.phase == CanonicalSubstrateBridgePhase::Available
            ? canonical_substrate_bridge_available_authority_valid(expected)
                && canonical_substrate_bridge_available_authority_valid(current)
            : expected.phase == CanonicalSubstrateBridgePhase::Reserved
                ? canonical_substrate_bridge_reserved_authority_valid(expected)
                    && canonical_substrate_bridge_reserved_authority_valid(current)
                : static_cast<bool>(expected) && static_cast<bool>(current);
    if (!stage_valid) return false;
    return expected.generation == current.generation
        && expected.transaction_generation == current.transaction_generation
        && expected.source_ordinal == current.source_ordinal
        && expected.source_version == current.source_version
        && expected.source_collection_version == current.source_collection_version
        && expected.list_exit_epoch == current.list_exit_epoch
        && expected.release_completion_epoch == current.release_completion_epoch
        && expected.revocation_epoch == current.revocation_epoch
        && expected.canonical_token == current.canonical_token
        && expected.expected_callback_sound == current.expected_callback_sound
        && (expected.phase == CanonicalSubstrateBridgePhase::None
            || private_object_handle_matches(expected.expected_callback_sound_identity,
                current.expected_callback_sound_identity))
        && expected.old_canonical_sound == current.old_canonical_sound
        && (expected.phase == CanonicalSubstrateBridgePhase::None
            || private_object_handle_matches(expected.old_canonical_sound_identity,
                current.old_canonical_sound_identity))
        && expected.old_canonical_request == current.old_canonical_request
        && expected.controller == current.controller
        && (expected.phase == CanonicalSubstrateBridgePhase::None
            || controller_identity_proof_matches(
                expected.controller_proof, current.controller_proof))
        && expected.slot == current.slot && expected.bgm == current.bgm
        && expected.predecessor_route_generation
            == current.predecessor_route_generation
        && expected.predecessor_lease == current.predecessor_lease
        && expected.predecessor_lifecycle_epoch
            == current.predecessor_lifecycle_epoch
        && expected.selection_generation == current.selection_generation
        && expected.route_generation == current.route_generation
        && expected.lease == current.lease
        && expected.song_key == current.song_key;
}

inline bool canonical_substrate_reset_lineage_authority_matches(
    const CanonicalSubstrateResetLineageAuthority& left,
    const CanonicalSubstrateResetLineageAuthority& right) noexcept
{
    if (left.phase == CanonicalSubstrateResetLineagePhase::None
        || right.phase == CanonicalSubstrateResetLineagePhase::None) {
        return canonical_substrate_reset_lineage_authority_empty(left)
            && canonical_substrate_reset_lineage_authority_empty(right);
    }
    if (left.phase != right.phase
        || (left.phase != CanonicalSubstrateResetLineagePhase::Qualified
            && left.phase != CanonicalSubstrateResetLineagePhase::Reserved)) {
        return false;
    }
    return left.generation == right.generation
        && left.rearm_epoch == right.rearm_epoch
        && left.reset_observation_generation
            == right.reset_observation_generation
        && left.reset_route_generation == right.reset_route_generation
        && left.reset_lease == right.reset_lease
        && left.reset_lifecycle_epoch == right.reset_lifecycle_epoch
        && left.transaction_generation == right.transaction_generation
        && left.source_ordinal == right.source_ordinal
        && left.source_version == right.source_version
        && left.source_collection_version == right.source_collection_version
        && left.list_exit_epoch == right.list_exit_epoch
        && left.proof_route_generation == right.proof_route_generation
        && left.proof_lease == right.proof_lease
        && left.proof_lifecycle_epoch == right.proof_lifecycle_epoch
        && left.request == right.request
        && left.canonical_token == right.canonical_token
        && left.bridge_generation == right.bridge_generation
        && left.bridge_release_epoch == right.bridge_release_epoch
        && left.controller == right.controller
        && controller_identity_proof_matches(
            left.controller_proof, right.controller_proof)
        && left.slot == right.slot && left.bgm == right.bgm
        && left.sound == right.sound
        && private_object_handle_matches(
            left.sound_identity, right.sound_identity);
}

struct CanonicalSubstratePhaseReservationFacts {
    bool confirmed_reservation_absent = false;
    bool reservation_generation_available = false;
    bool reservation_machine_reserved = false;
    bool proof_exact = false;
    bool bridge_available = false;
    bool reset_phase_exact = false;
};

struct CanonicalSubstratePhaseReservationState {
    CanonicalSubstrateBridgePhase bridge_phase =
        CanonicalSubstrateBridgePhase::None;
    CanonicalSubstrateResetLineagePhase reset_phase =
        CanonicalSubstrateResetLineagePhase::None;
    uint64_t rearm_epoch = 0;
};

struct CanonicalSubstrateReservationPublicationFacts {
    bool reservation_machine_reserved = false;
    bool reservation_generation_exact = false;
    bool proof_exact = false;
    bool bridge_successor_exact = false;
    bool reset_phase_exact = false;
    uint64_t expected_attempt_epoch = 0;
    uint64_t current_attempt_epoch = 0;
};

struct CanonicalSubstrateReservationPublicationState {
    SelectionActivationReservationState machine_state =
        SelectionActivationReservationState::Empty;
    uint64_t machine_generation = 0;
    CanonicalSubstrateBridgeAuthority bridge{};
    CanonicalSubstrateResetLineagePhase reset_phase =
        CanonicalSubstrateResetLineagePhase::None;
    uint64_t record_generation = 0;
    bool record_published = false;
};

inline bool commit_canonical_substrate_reservation_publication(
    const CanonicalSubstrateReservationPublicationFacts& facts,
    const CanonicalSubstrateBridgeAuthority& reserved_bridge,
    const bool reserve_reset_lineage,
    CanonicalSubstrateReservationPublicationState& state) noexcept
{
    if (!facts.reservation_machine_reserved
        || !facts.reservation_generation_exact || !facts.proof_exact
        || !facts.bridge_successor_exact || !facts.reset_phase_exact
        || facts.expected_attempt_epoch == 0
        || facts.current_attempt_epoch != facts.expected_attempt_epoch
        || reserved_bridge.revocation_epoch != facts.expected_attempt_epoch
        || state.machine_state != SelectionActivationReservationState::Reserved
        || state.machine_generation == 0
        || !canonical_substrate_bridge_available_authority_valid(state.bridge)
        || !canonical_substrate_bridge_reservation_successor_exact(
            state.bridge, reserved_bridge)
        || (reserve_reset_lineage
            ? state.reset_phase
                != CanonicalSubstrateResetLineagePhase::Qualified
            : state.reset_phase != CanonicalSubstrateResetLineagePhase::None)) {
        return false;
    }
    state.bridge = reserved_bridge;
    if (reserve_reset_lineage) {
        state.reset_phase = CanonicalSubstrateResetLineagePhase::Reserved;
    }
    state.record_generation = state.machine_generation;
    state.record_published = true;
    return true;
}

constexpr bool selection_activation_attempt_epoch_exact(
    const uint64_t expected_attempt_epoch,
    const uint64_t current_revocation_epoch) noexcept
{
    return expected_attempt_epoch != 0
        && current_revocation_epoch == expected_attempt_epoch;
}

struct SelectionActivationCommitEpochFacts {
    bool machine_consumed = false;
    uint64_t machine_generation = 0;
    uint64_t record_generation = 0;
    uint64_t expected_generation = 0;
    uint64_t record_attempt_epoch = 0;
    uint64_t expected_attempt_epoch = 0;
    uint64_t current_revocation_epoch = 0;
};

constexpr bool selection_activation_commit_epoch_exact(
    const SelectionActivationCommitEpochFacts& facts) noexcept
{
    return facts.machine_consumed && facts.expected_generation != 0
        && facts.machine_generation == facts.expected_generation
        && facts.record_generation == facts.expected_generation
        && facts.record_attempt_epoch == facts.expected_attempt_epoch
        && selection_activation_attempt_epoch_exact(
            facts.expected_attempt_epoch, facts.current_revocation_epoch);
}

constexpr bool commit_canonical_substrate_phase_reservation(
    const CanonicalSubstratePhaseReservationFacts& facts,
    const bool reserve_reset_lineage,
    CanonicalSubstratePhaseReservationState& state) noexcept
{
    if (!facts.confirmed_reservation_absent
        || !facts.reservation_generation_available
        || !facts.reservation_machine_reserved || !facts.proof_exact
        || !facts.bridge_available || !facts.reset_phase_exact
        || state.bridge_phase != CanonicalSubstrateBridgePhase::Available
        || (reserve_reset_lineage
            ? state.reset_phase
                != CanonicalSubstrateResetLineagePhase::Qualified
            : state.reset_phase != CanonicalSubstrateResetLineagePhase::None)) {
        return false;
    }
    state.bridge_phase = CanonicalSubstrateBridgePhase::Reserved;
    if (reserve_reset_lineage) {
        state.reset_phase = CanonicalSubstrateResetLineagePhase::Reserved;
    }
    return true;
}

struct CanonicalSubstratePhaseRollbackFacts {
    bool attempt_owned = false;
    bool proof_exact = false;
    bool bridge_generation_exact = false;
    bool transaction_exact = false;
    bool bridge_reserved = false;
    bool reset_phase_exact = false;
};

struct CanonicalSubstrateResetBridgeSoundFacts {
    bool callback_sound_nonnull = false;
    bool callback_identity_valid = false;
    bool old_canonical_pointer_exact = false;
    bool old_canonical_identity_exact = false;
};

constexpr bool canonical_substrate_reset_bridge_sound_exact(
    const CanonicalSubstrateResetBridgeSoundFacts& facts) noexcept
{
    return facts.callback_sound_nonnull && facts.callback_identity_valid
        && facts.old_canonical_pointer_exact
        && facts.old_canonical_identity_exact;
}

enum class SelectionActivationSubstrateAttemptDisposition : uint8_t {
    RestoreIfExact,
    RetainFailClosed,
};

struct SelectionActivationSubstrateAttemptFinalizeFacts {
    bool attempt_active = false;
    bool reservation_generation_exact = false;
    bool machine_state_exact = false;
    bool record_generation_exact = false;
    bool record_ownership_exact = false;
    bool revocation_epoch_exact = false;
    bool native_identity_exact = false;
    bool proof_exact = false;
    bool bridge_generation_exact = false;
    bool transaction_exact = false;
    bool bridge_reserved = false;
    bool reset_phase_exact = false;
};

struct SelectionActivationSubstrateAttemptFinalizeState {
    SelectionActivationReservationState machine_state =
        SelectionActivationReservationState::Empty;
    uint64_t record_generation = 0;
    bool record_attempt_active = false;
    CanonicalSubstrateBridgePhase bridge_phase =
        CanonicalSubstrateBridgePhase::None;
    CanonicalSubstrateResetLineagePhase reset_phase =
        CanonicalSubstrateResetLineagePhase::None;
    uint64_t rearm_epoch = 0;
    bool machine_retired = false;
    bool phases_restored = false;
};

constexpr bool finalize_selection_activation_substrate_attempt(
    const SelectionActivationSubstrateAttemptFinalizeFacts& facts,
    const SelectionActivationSubstrateAttemptDisposition disposition,
    const bool reserved_reset_lineage,
    SelectionActivationSubstrateAttemptFinalizeState& state) noexcept
{
    if (!facts.attempt_active || !facts.reservation_generation_exact
        || !facts.machine_state_exact || !facts.record_generation_exact
        || !facts.record_ownership_exact) {
        return false;
    }
    state.machine_state = SelectionActivationReservationState::Revoked;
    state.record_generation = 0;
    state.record_attempt_active = false;
    state.machine_retired = true;
    if (disposition == SelectionActivationSubstrateAttemptDisposition::RetainFailClosed
        || !facts.revocation_epoch_exact || !facts.native_identity_exact) {
        return true;
    }
    if (!facts.proof_exact || !facts.bridge_generation_exact
        || !facts.transaction_exact || !facts.bridge_reserved
        || !facts.reset_phase_exact
        || state.bridge_phase != CanonicalSubstrateBridgePhase::Reserved
        || (reserved_reset_lineage
            && state.reset_phase
                != CanonicalSubstrateResetLineagePhase::Reserved)
        || (!reserved_reset_lineage
            && state.reset_phase
                != CanonicalSubstrateResetLineagePhase::None)) {
        return true;
    }
    state.bridge_phase = CanonicalSubstrateBridgePhase::Available;
    if (reserved_reset_lineage) {
        state.reset_phase = CanonicalSubstrateResetLineagePhase::Qualified;
    }
    state.phases_restored = true;
    return true;
}

constexpr bool rollback_canonical_substrate_phase_reservation(
    const CanonicalSubstratePhaseRollbackFacts& facts,
    const bool reserved_reset_lineage,
    CanonicalSubstratePhaseReservationState& state) noexcept
{
    if (!facts.attempt_owned || !facts.proof_exact
        || !facts.bridge_generation_exact || !facts.transaction_exact
        || !facts.bridge_reserved || !facts.reset_phase_exact
        || state.bridge_phase != CanonicalSubstrateBridgePhase::Reserved
        || (reserved_reset_lineage
            ? state.reset_phase
                != CanonicalSubstrateResetLineagePhase::Reserved
            : state.reset_phase != CanonicalSubstrateResetLineagePhase::None)) {
        return false;
    }
    state.bridge_phase = CanonicalSubstrateBridgePhase::Available;
    if (reserved_reset_lineage) {
        state.reset_phase = CanonicalSubstrateResetLineagePhase::Qualified;
    }
    return true;
}

static_assert(std::is_trivially_copyable_v<
    CanonicalSubstratePhaseReservationFacts>);
static_assert(std::is_trivially_copyable_v<
    CanonicalSubstratePhaseReservationState>);
static_assert(std::is_trivially_copyable_v<
    CanonicalSubstratePhaseRollbackFacts>);
static_assert(std::is_trivially_copyable_v<
    CanonicalSubstrateReservationPublicationFacts>);
static_assert(std::is_trivially_copyable_v<
    CanonicalSubstrateReservationPublicationState>);
static_assert(std::is_trivially_copyable_v<
    SelectionActivationCommitEpochFacts>);
static_assert(std::is_trivially_copyable_v<
    CanonicalSubstrateResetBridgeSoundFacts>);
static_assert(std::is_trivially_copyable_v<
    SelectionActivationSubstrateAttemptFinalizeFacts>);
static_assert(std::is_trivially_copyable_v<
    SelectionActivationSubstrateAttemptFinalizeState>);

struct CanonicalSubstrateResetLineageCommitFacts {
    bool mutation_authorized = false;
    bool cleanup_verified = false;
    bool aggregate_ownership_clear = false;
    bool route_installed = false;
    bool route_enabled = false;
    bool reset_route_idle = false;
    bool reset_route_nonzero = false;
    bool reset_route_after_proof = false;
    bool reset_route_generation_room = false;
    bool reset_state_exact = false;
    bool request_lineage_exact = false;
    bool current_zero_idle = false;
    bool cleanup_clear = false;
    bool quarantine_clear = false;
    bool lifecycle_allowed = false;
    bool substrate_live_exact = false;
    bool canonical_token_nonzero = false;
    bool custom_token_absent = false;
    bool bridge_available = false;
    bool bridge_generation_nonzero = false;
    bool bridge_transaction_exact = false;
    bool bridge_source_exact = false;
    bool bridge_list_exit_exact = false;
    bool bridge_canonical_token_exact = false;
    bool bridge_request_exact = false;
    bool bridge_controller_exact = false;
    bool bridge_slot_bgm_exact = false;
    bool bridge_sound_exact = false;
    bool authority_absent = false;
    bool authority_generation_room = false;
    bool rearm_epoch_room = false;
};

enum class CanonicalSubstrateResetLineageCommitFailure : uint8_t {
    None,
    MutationUnauthorized,
    CleanupUnverified,
    AggregateOwnershipPresent,
    RouteNotInstalled,
    RouteDisabled,
    ResetRouteNotIdle,
    ResetRouteZero,
    ResetRouteNotAfterProof,
    ResetRouteGenerationWrap,
    ResetStateDrift,
    RequestLineageDrift,
    CurrentRouteNotZeroIdle,
    CleanupPending,
    Quarantined,
    LifecycleRejected,
    SubstrateLiveRejected,
    CanonicalTokenInvalid,
    CustomTokenPresent,
    BridgeUnavailable,
    BridgeGenerationInvalid,
    BridgeTransactionDrift,
    BridgeSourceDrift,
    BridgeListExitDrift,
    BridgeCanonicalTokenDrift,
    BridgeRequestDrift,
    BridgeControllerDrift,
    BridgeSlotBgmDrift,
    BridgeSoundDrift,
    AuthorityAlreadyPresent,
    AuthorityGenerationWrap,
    RearmEpochWrap,
};

constexpr CanonicalSubstrateResetLineageCommitFailure
first_canonical_substrate_reset_lineage_commit_failure(
    const CanonicalSubstrateResetLineageCommitFacts& f) noexcept
{
    if (!f.mutation_authorized) return CanonicalSubstrateResetLineageCommitFailure::MutationUnauthorized;
    if (!f.cleanup_verified) return CanonicalSubstrateResetLineageCommitFailure::CleanupUnverified;
    if (!f.aggregate_ownership_clear) return CanonicalSubstrateResetLineageCommitFailure::AggregateOwnershipPresent;
    if (!f.route_installed) return CanonicalSubstrateResetLineageCommitFailure::RouteNotInstalled;
    if (!f.route_enabled) return CanonicalSubstrateResetLineageCommitFailure::RouteDisabled;
    if (!f.reset_route_idle) return CanonicalSubstrateResetLineageCommitFailure::ResetRouteNotIdle;
    if (!f.reset_route_nonzero) return CanonicalSubstrateResetLineageCommitFailure::ResetRouteZero;
    if (!f.reset_route_after_proof) return CanonicalSubstrateResetLineageCommitFailure::ResetRouteNotAfterProof;
    if (!f.reset_route_generation_room) return CanonicalSubstrateResetLineageCommitFailure::ResetRouteGenerationWrap;
    if (!f.reset_state_exact) return CanonicalSubstrateResetLineageCommitFailure::ResetStateDrift;
    if (!f.request_lineage_exact) return CanonicalSubstrateResetLineageCommitFailure::RequestLineageDrift;
    if (!f.current_zero_idle) return CanonicalSubstrateResetLineageCommitFailure::CurrentRouteNotZeroIdle;
    if (!f.cleanup_clear) return CanonicalSubstrateResetLineageCommitFailure::CleanupPending;
    if (!f.quarantine_clear) return CanonicalSubstrateResetLineageCommitFailure::Quarantined;
    if (!f.lifecycle_allowed) return CanonicalSubstrateResetLineageCommitFailure::LifecycleRejected;
    if (!f.substrate_live_exact) return CanonicalSubstrateResetLineageCommitFailure::SubstrateLiveRejected;
    if (!f.canonical_token_nonzero) return CanonicalSubstrateResetLineageCommitFailure::CanonicalTokenInvalid;
    if (!f.custom_token_absent) return CanonicalSubstrateResetLineageCommitFailure::CustomTokenPresent;
    if (!f.bridge_available) return CanonicalSubstrateResetLineageCommitFailure::BridgeUnavailable;
    if (!f.bridge_generation_nonzero) return CanonicalSubstrateResetLineageCommitFailure::BridgeGenerationInvalid;
    if (!f.bridge_transaction_exact) return CanonicalSubstrateResetLineageCommitFailure::BridgeTransactionDrift;
    if (!f.bridge_source_exact) return CanonicalSubstrateResetLineageCommitFailure::BridgeSourceDrift;
    if (!f.bridge_list_exit_exact) return CanonicalSubstrateResetLineageCommitFailure::BridgeListExitDrift;
    if (!f.bridge_canonical_token_exact) return CanonicalSubstrateResetLineageCommitFailure::BridgeCanonicalTokenDrift;
    if (!f.bridge_request_exact) return CanonicalSubstrateResetLineageCommitFailure::BridgeRequestDrift;
    if (!f.bridge_controller_exact) return CanonicalSubstrateResetLineageCommitFailure::BridgeControllerDrift;
    if (!f.bridge_slot_bgm_exact) return CanonicalSubstrateResetLineageCommitFailure::BridgeSlotBgmDrift;
    if (!f.bridge_sound_exact) return CanonicalSubstrateResetLineageCommitFailure::BridgeSoundDrift;
    if (!f.authority_absent) return CanonicalSubstrateResetLineageCommitFailure::AuthorityAlreadyPresent;
    if (!f.authority_generation_room) return CanonicalSubstrateResetLineageCommitFailure::AuthorityGenerationWrap;
    if (!f.rearm_epoch_room) return CanonicalSubstrateResetLineageCommitFailure::RearmEpochWrap;
    return CanonicalSubstrateResetLineageCommitFailure::None;
}

struct CanonicalSubstrateResetLineageCommitState {
    uint64_t authority_generation = 0;
    uint64_t rearm_epoch = 0;
    uint64_t request_lineage_route_generation = 0;
    AudioRouteLeaseIdentity request_lineage_lease{};
    uint64_t request_lineage_lifecycle_epoch = 0;
    CanonicalSubstrateResetLineageAuthority authority{};
};

inline bool commit_canonical_substrate_reset_lineage(
    const CanonicalSubstrateResetLineageCommitFacts& facts,
    const CanonicalSubstrateResetLineageAuthority& linked,
    CanonicalSubstrateResetLineageCommitState& state) noexcept
{
    if (first_canonical_substrate_reset_lineage_commit_failure(facts)
            != CanonicalSubstrateResetLineageCommitFailure::None
        || state.authority_generation == UINT64_MAX
        || state.rearm_epoch == UINT64_MAX) {
        return false;
    }
    state.authority = linked;
    state.authority.phase = CanonicalSubstrateResetLineagePhase::Qualified;
    state.authority.generation = ++state.authority_generation;
    state.authority.rearm_epoch = ++state.rearm_epoch;
    state.request_lineage_route_generation = linked.reset_route_generation;
    state.request_lineage_lease = linked.reset_lease;
    state.request_lineage_lifecycle_epoch = linked.reset_lifecycle_epoch;
    return true;
}

struct CanonicalSubstrateResetLineageUseFacts {
    bool normal_successor = false;
    bool bridge_stage_exact = false;
    bool current_zero_idle = false;
    bool authority_phase_exact = false;
    bool authority_generation_valid = false;
    bool reset_observation_exact = false;
    bool reset_route_valid = false;
    bool request_rebase_lineage_exact = false;
    bool reset_lifecycle_exact = false;
    bool transaction_exact = false;
    bool source_exact = false;
    bool list_exit_exact = false;
    bool proof_route_exact = false;
    bool proof_lease_exact = false;
    bool proof_lifecycle_exact = false;
    bool request_exact = false;
    bool canonical_token_exact = false;
    bool bridge_generation_exact = false;
    bool bridge_release_exact = false;
    bool controller_exact = false;
    bool controller_proof_exact = false;
    bool slot_bgm_exact = false;
    bool sound_exact = false;
    bool sound_identity_exact = false;
};

constexpr bool canonical_substrate_route_predecessor_exact(
    const CanonicalSubstrateResetLineageUseFacts& f) noexcept
{
    if (f.normal_successor) return f.bridge_stage_exact;
    return f.bridge_stage_exact && f.current_zero_idle && f.authority_phase_exact
        && f.authority_generation_valid && f.reset_route_valid
        && f.reset_observation_exact && f.request_rebase_lineage_exact
        && f.reset_lifecycle_exact
        && f.transaction_exact && f.source_exact
        && f.list_exit_exact && f.proof_route_exact && f.proof_lease_exact
        && f.proof_lifecycle_exact && f.request_exact
        && f.canonical_token_exact && f.bridge_generation_exact
        && f.bridge_release_exact && f.controller_exact
        && f.controller_proof_exact && f.slot_bgm_exact && f.sound_exact
        && f.sound_identity_exact;
}

struct SelectionAdmissionSubstrateRequestRebaseCommitState {
    uint64_t proof_request = 0;
    uint64_t bridge_old_request = 0;
    uint64_t rearm_epoch = 0;
};

struct SelectionAdmissionSubstrateLiveFacts {
    bool proof_active = false;
    bool proof_state_exact = false;
    bool transaction_nonzero = false;
    bool controller_nonnull = false;
    bool controller_pointer_exact = false;
    bool controller_identity_exact = false;
    bool chain_read = false;
    bool sound_read = false;
    bool request_read = false;
    bool state_read = false;
    bool sound_nonnull = false;
    bool sound_identity_read = false;
    bool slot_bgm_exact = false;
    bool sound_pointer_exact = false;
    bool sound_identity_exact = false;
    bool request_exact = false;
    bool state_four = false;
    uintptr_t observed_controller = 0;
    uintptr_t observed_slot = 0;
    uintptr_t observed_bgm = 0;
    uintptr_t observed_sound = 0;
    uint64_t observed_request = 0;
    uint32_t observed_sound_index = 0;
    uint32_t observed_sound_serial = 0;
    uint8_t observed_state = 0;
};

enum class SelectionAdmissionSubstrateLiveFailure : uint8_t {
    None,
    ProofInactive,
    ProofStateDrift,
    TransactionInvalid,
    ControllerNull,
    ControllerPointerDrift,
    ControllerIdentityDrift,
    ChainUnreadable,
    SoundUnreadable,
    RequestUnreadable,
    StateUnreadable,
    SoundNull,
    SoundIdentityUnreadable,
    SlotBgmDrift,
    SoundPointerDrift,
    SoundIdentityDrift,
    RequestDrift,
    StateNotFour,
};

constexpr SelectionAdmissionSubstrateLiveFailure
first_selection_admission_substrate_live_failure(
    const SelectionAdmissionSubstrateLiveFacts& f) noexcept
{
    if (!f.proof_active) return SelectionAdmissionSubstrateLiveFailure::ProofInactive;
    if (!f.proof_state_exact) return SelectionAdmissionSubstrateLiveFailure::ProofStateDrift;
    if (!f.transaction_nonzero) return SelectionAdmissionSubstrateLiveFailure::TransactionInvalid;
    if (!f.controller_nonnull) return SelectionAdmissionSubstrateLiveFailure::ControllerNull;
    if (!f.controller_pointer_exact) return SelectionAdmissionSubstrateLiveFailure::ControllerPointerDrift;
    if (!f.controller_identity_exact) return SelectionAdmissionSubstrateLiveFailure::ControllerIdentityDrift;
    if (!f.chain_read) return SelectionAdmissionSubstrateLiveFailure::ChainUnreadable;
    if (!f.sound_read) return SelectionAdmissionSubstrateLiveFailure::SoundUnreadable;
    if (!f.request_read) return SelectionAdmissionSubstrateLiveFailure::RequestUnreadable;
    if (!f.state_read) return SelectionAdmissionSubstrateLiveFailure::StateUnreadable;
    if (!f.sound_nonnull) return SelectionAdmissionSubstrateLiveFailure::SoundNull;
    if (!f.sound_identity_read) return SelectionAdmissionSubstrateLiveFailure::SoundIdentityUnreadable;
    if (!f.slot_bgm_exact) return SelectionAdmissionSubstrateLiveFailure::SlotBgmDrift;
    if (!f.sound_pointer_exact) return SelectionAdmissionSubstrateLiveFailure::SoundPointerDrift;
    if (!f.sound_identity_exact) return SelectionAdmissionSubstrateLiveFailure::SoundIdentityDrift;
    if (!f.request_exact) return SelectionAdmissionSubstrateLiveFailure::RequestDrift;
    if (!f.state_four) return SelectionAdmissionSubstrateLiveFailure::StateNotFour;
    return SelectionAdmissionSubstrateLiveFailure::None;
}

struct CanonicalSubstrateResetRequestLineageFacts {
    SelectionAdmissionSubstrateLiveFailure live_failure =
        SelectionAdmissionSubstrateLiveFailure::ProofInactive;
    bool authenticated_rebase_route_exact = false;
    bool authenticated_rebase_lease_exact = false;
    bool authenticated_rebase_lifecycle_exact = false;
    bool authenticated_rebase_lease_valid = false;
    bool continuity_route_absent = false;
    bool continuity_lease_absent = false;
    bool continuity_lifecycle_absent = false;
    bool continuity_reset_route_exact = false;
};

constexpr bool canonical_substrate_reset_continuity_route_exact(
    const uint64_t proof_route_generation,
    const uint64_t reset_route_generation,
    const AudioRouteLeaseIdentity& reset_lease) noexcept
{
    return proof_route_generation != 0
        && proof_route_generation != UINT64_MAX
        && reset_route_generation == proof_route_generation + 1
        && reset_lease == AudioRouteLeaseIdentity{};
}

constexpr bool canonical_substrate_reset_request_lineage_exact(
    const CanonicalSubstrateResetRequestLineageFacts& facts) noexcept
{
    if (facts.live_failure != SelectionAdmissionSubstrateLiveFailure::None) {
        return false;
    }
    const bool authenticated_rebase = facts.authenticated_rebase_route_exact
        && facts.authenticated_rebase_lease_exact
        && facts.authenticated_rebase_lifecycle_exact
        && facts.authenticated_rebase_lease_valid;
    const bool unchanged_continuity = facts.continuity_route_absent
        && facts.continuity_lease_absent
        && facts.continuity_lifecycle_absent
        && facts.continuity_reset_route_exact;
    return authenticated_rebase || unchanged_continuity;
}

struct SelectionAdmissionSubstrateRequestRebaseFacts {
    bool mutation_authorized = false;
    SelectionAdmissionSubstrateLiveFailure live_failure =
        SelectionAdmissionSubstrateLiveFailure::ProofInactive;
    bool state_four = false;
    bool canonical_token_nonzero = false;
    bool custom_token_absent = false;
    bool bridge_available = false;
    bool bridge_canonical_token_exact = false;
    bool bridge_old_request_exact = false;
    uint64_t expected_request = 0;
    uint64_t observed_request = 0;
};

enum class SelectionAdmissionSubstrateRequestRebaseFailure : uint8_t {
    None,
    MutationUnauthorized,
    NotRequestOnlyDrift,
    StateNotFour,
    CanonicalTokenInvalid,
    CustomTokenPresent,
    BridgeUnavailable,
    BridgeCanonicalTokenDrift,
    BridgeOldRequestDrift,
    ExpectedRequestInvalid,
    ObservedRequestInvalid,
    RequestSlotDrift,
    RequestGenerationNotNewer,
};

inline SelectionAdmissionSubstrateRequestRebaseFailure
first_selection_admission_substrate_request_rebase_failure(
    const SelectionAdmissionSubstrateRequestRebaseFacts& f) noexcept
{
    if (!f.mutation_authorized) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::MutationUnauthorized;
    }
    if (f.live_failure != SelectionAdmissionSubstrateLiveFailure::RequestDrift) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::NotRequestOnlyDrift;
    }
    if (!f.state_four) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::StateNotFour;
    }
    if (!f.canonical_token_nonzero) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::CanonicalTokenInvalid;
    }
    if (!f.custom_token_absent) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::CustomTokenPresent;
    }
    if (!f.bridge_available) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::BridgeUnavailable;
    }
    if (!f.bridge_canonical_token_exact) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::BridgeCanonicalTokenDrift;
    }
    if (!f.bridge_old_request_exact) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::BridgeOldRequestDrift;
    }
    const AudioBgmRequestHandle expected{f.expected_request};
    if (!expected.valid_bgm_request()) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::ExpectedRequestInvalid;
    }
    const AudioBgmRequestHandle observed{f.observed_request};
    if (!observed.valid_bgm_request()) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::ObservedRequestInvalid;
    }
    if (expected.type() != observed.type()
        || expected.pool_index() != observed.pool_index()) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::RequestSlotDrift;
    }
    if (observed.generation() <= expected.generation()) {
        return SelectionAdmissionSubstrateRequestRebaseFailure::RequestGenerationNotNewer;
    }
    return SelectionAdmissionSubstrateRequestRebaseFailure::None;
}

inline bool selection_admission_substrate_request_rebase_exact(
    const SelectionAdmissionSubstrateRequestRebaseFacts& f) noexcept
{
    return first_selection_admission_substrate_request_rebase_failure(f)
        == SelectionAdmissionSubstrateRequestRebaseFailure::None;
}

inline bool commit_selection_admission_substrate_request_rebase(
    const SelectionAdmissionSubstrateRequestRebaseFacts& facts,
    SelectionAdmissionSubstrateRequestRebaseCommitState& state) noexcept
{
    if (!selection_admission_substrate_request_rebase_exact(facts)
        || state.proof_request != facts.expected_request
        || state.bridge_old_request != facts.expected_request
        || state.rearm_epoch == UINT64_MAX) {
        return false;
    }
    state.proof_request = facts.observed_request;
    state.bridge_old_request = facts.observed_request;
    ++state.rearm_epoch;
    return true;
}

struct SelectionAdmissionProofMatchFacts {
    bool left_active = false;
    bool right_active = false;
    bool left_state_exact = false;
    bool right_state_exact = false;
    bool transaction_nonzero = false;
    bool transaction_exact = false;
    bool source_ordinal_exact = false;
    bool source_version_exact = false;
    bool collection_version_exact = false;
    bool list_exit_exact = false;
    bool controller_pointer_exact = false;
    bool controller_identity_exact = false;
    bool slot_bgm_exact = false;
    bool sound_pointer_exact = false;
    bool sound_index_exact = false;
    bool sound_serial_exact = false;
    bool request_exact = false;
    bool request_rebase_lineage_exact = false;
    bool route_generation_exact = false;
    bool lease_exact = false;
    bool lifecycle_epoch_exact = false;
    bool canonical_token_exact = false;
    bool custom_token_exact = false;
    bool bridge_generation_exact = false;
    bool bridge_phase_exact = false;
    bool bridge_release_epoch_exact = false;
    bool bridge_callback_pointer_exact = false;
    bool bridge_callback_identity_exact = false;
    bool bridge_canonical_token_exact = false;
    bool bridge_authority_exact = false;
    bool reset_lineage_exact = false;
};

enum class SelectionAdmissionProofMatchFailure : uint8_t {
    None,
    GlobalInactive,
    RetainedInactive,
    GlobalStateDrift,
    RetainedStateDrift,
    TransactionInvalid,
    TransactionDrift,
    SourceOrdinalDrift,
    SourceVersionDrift,
    CollectionVersionDrift,
    ListExitDrift,
    ControllerPointerDrift,
    ControllerIdentityDrift,
    SlotBgmDrift,
    SoundPointerDrift,
    SoundIndexDrift,
    SoundSerialDrift,
    RequestDrift,
    RequestRebaseLineageDrift,
    RouteGenerationDrift,
    LeaseDrift,
    LifecycleEpochDrift,
    CanonicalTokenDrift,
    CustomTokenDrift,
    BridgeGenerationDrift,
    BridgePhaseDrift,
    BridgeReleaseEpochDrift,
    BridgeCallbackPointerDrift,
    BridgeCallbackIdentityDrift,
    BridgeCanonicalTokenDrift,
    BridgeAuthorityDrift,
    ResetLineageDrift,
};

constexpr SelectionAdmissionProofMatchFailure
first_selection_admission_proof_match_failure(
    const SelectionAdmissionProofMatchFacts& f) noexcept
{
    if (!f.left_active) return SelectionAdmissionProofMatchFailure::GlobalInactive;
    if (!f.right_active) return SelectionAdmissionProofMatchFailure::RetainedInactive;
    if (!f.left_state_exact) return SelectionAdmissionProofMatchFailure::GlobalStateDrift;
    if (!f.right_state_exact) return SelectionAdmissionProofMatchFailure::RetainedStateDrift;
    if (!f.transaction_nonzero) return SelectionAdmissionProofMatchFailure::TransactionInvalid;
    if (!f.transaction_exact) return SelectionAdmissionProofMatchFailure::TransactionDrift;
    if (!f.source_ordinal_exact) return SelectionAdmissionProofMatchFailure::SourceOrdinalDrift;
    if (!f.source_version_exact) return SelectionAdmissionProofMatchFailure::SourceVersionDrift;
    if (!f.collection_version_exact) return SelectionAdmissionProofMatchFailure::CollectionVersionDrift;
    if (!f.list_exit_exact) return SelectionAdmissionProofMatchFailure::ListExitDrift;
    if (!f.controller_pointer_exact) return SelectionAdmissionProofMatchFailure::ControllerPointerDrift;
    if (!f.controller_identity_exact) return SelectionAdmissionProofMatchFailure::ControllerIdentityDrift;
    if (!f.slot_bgm_exact) return SelectionAdmissionProofMatchFailure::SlotBgmDrift;
    if (!f.sound_pointer_exact) return SelectionAdmissionProofMatchFailure::SoundPointerDrift;
    if (!f.sound_index_exact) return SelectionAdmissionProofMatchFailure::SoundIndexDrift;
    if (!f.sound_serial_exact) return SelectionAdmissionProofMatchFailure::SoundSerialDrift;
    if (!f.request_exact) return SelectionAdmissionProofMatchFailure::RequestDrift;
    if (!f.request_rebase_lineage_exact) return SelectionAdmissionProofMatchFailure::RequestRebaseLineageDrift;
    if (!f.route_generation_exact) return SelectionAdmissionProofMatchFailure::RouteGenerationDrift;
    if (!f.lease_exact) return SelectionAdmissionProofMatchFailure::LeaseDrift;
    if (!f.lifecycle_epoch_exact) return SelectionAdmissionProofMatchFailure::LifecycleEpochDrift;
    if (!f.canonical_token_exact) return SelectionAdmissionProofMatchFailure::CanonicalTokenDrift;
    if (!f.custom_token_exact) return SelectionAdmissionProofMatchFailure::CustomTokenDrift;
    if (!f.bridge_generation_exact) return SelectionAdmissionProofMatchFailure::BridgeGenerationDrift;
    if (!f.bridge_phase_exact) return SelectionAdmissionProofMatchFailure::BridgePhaseDrift;
    if (!f.bridge_release_epoch_exact) return SelectionAdmissionProofMatchFailure::BridgeReleaseEpochDrift;
    if (!f.bridge_callback_pointer_exact) return SelectionAdmissionProofMatchFailure::BridgeCallbackPointerDrift;
    if (!f.bridge_callback_identity_exact) return SelectionAdmissionProofMatchFailure::BridgeCallbackIdentityDrift;
    if (!f.bridge_canonical_token_exact) return SelectionAdmissionProofMatchFailure::BridgeCanonicalTokenDrift;
    if (!f.bridge_authority_exact) return SelectionAdmissionProofMatchFailure::BridgeAuthorityDrift;
    if (!f.reset_lineage_exact) return SelectionAdmissionProofMatchFailure::ResetLineageDrift;
    return SelectionAdmissionProofMatchFailure::None;
}

struct SelectionAdmissionCommitFacts {
    bool admission_exists = false;
    bool selection_guard_acquired = false;
    bool not_committed = false;
    bool aggregate_lease_present = false;
    bool aggregate_lease_active = false;
    bool exit_not_requested = false;
    bool exit_not_pending = false;
    bool selection_guard_current = false;
    bool activation_revocation_epoch_exact = false;
    bool controller_pointer_exact = false;
    bool controller_identity_exact = false;
    bool substrate_live_exact = false;
    bool proof_match_exact = false;
    bool proof_reservation_succeeded = false;
    bool route_installed = false;
    bool route_enabled = false;
    bool frozen_lease_inactive = false;
    bool native_route_unowned = false;
    bool list_cleanup_clear = false;
    bool route_idle = false;
    bool route_generation_exact = false;
    bool lifecycle_epoch_exact = false;
    bool proof_predecessor_exact = false;
    bool cleanup_only_clear = false;
    bool lifecycle_preflight_allowed = false;
    bool sidecar_pointer_exact = false;
    bool sidecar_header_exact = false;
    bool sidecar_allocation_exact = false;
    bool sidecar_mabf_exact = false;
    bool final_coordinator_exact = false;
    bool publication_reached = false;
    bool controller_bind_exact = false;
};

enum class SelectionAdmissionCommitFailure : uint8_t {
    None,
    AdmissionMissing,
    SelectionGuardMissing,
    AlreadyCommitted,
    AggregateLeaseMissing,
    AggregateLeaseInactive,
    AggregateExitRequested,
    AggregateExitPending,
    SelectionGuardDrift,
    ActivationRevocationEpochDrift,
    ControllerPointerDrift,
    ControllerIdentityDrift,
    SubstrateLiveRejected,
    ProofMatchRejected,
    ProofReservationFailed,
    RouteNotInstalled,
    RouteDisabled,
    FrozenLeaseActive,
    NativeRouteOwned,
    ListCleanupPending,
    RouteNotIdle,
    RouteGenerationDrift,
    LifecycleEpochDrift,
    ProofPredecessorDrift,
    CleanupOnlyBlocked,
    LifecyclePreflightRejected,
    SidecarPointerDrift,
    SidecarHeaderDrift,
    SidecarAllocationDrift,
    SidecarMabfDrift,
    FinalCoordinatorRejected,
    PublicationNotReached,
    ControllerBindFailed,
    Exception,
};

constexpr SelectionAdmissionCommitFailure first_selection_admission_commit_failure(
    const SelectionAdmissionCommitFacts& f) noexcept
{
    if (!f.admission_exists) return SelectionAdmissionCommitFailure::AdmissionMissing;
    if (!f.selection_guard_acquired) return SelectionAdmissionCommitFailure::SelectionGuardMissing;
    if (!f.not_committed) return SelectionAdmissionCommitFailure::AlreadyCommitted;
    if (!f.aggregate_lease_present) return SelectionAdmissionCommitFailure::AggregateLeaseMissing;
    if (!f.aggregate_lease_active) return SelectionAdmissionCommitFailure::AggregateLeaseInactive;
    if (!f.exit_not_requested) return SelectionAdmissionCommitFailure::AggregateExitRequested;
    if (!f.exit_not_pending) return SelectionAdmissionCommitFailure::AggregateExitPending;
    if (!f.selection_guard_current) return SelectionAdmissionCommitFailure::SelectionGuardDrift;
    if (!f.activation_revocation_epoch_exact) {
        return SelectionAdmissionCommitFailure::ActivationRevocationEpochDrift;
    }
    if (!f.controller_pointer_exact) return SelectionAdmissionCommitFailure::ControllerPointerDrift;
    if (!f.controller_identity_exact) return SelectionAdmissionCommitFailure::ControllerIdentityDrift;
    if (!f.substrate_live_exact) return SelectionAdmissionCommitFailure::SubstrateLiveRejected;
    if (!f.proof_match_exact) return SelectionAdmissionCommitFailure::ProofMatchRejected;
    if (!f.proof_reservation_succeeded) return SelectionAdmissionCommitFailure::ProofReservationFailed;
    if (!f.route_installed) return SelectionAdmissionCommitFailure::RouteNotInstalled;
    if (!f.route_enabled) return SelectionAdmissionCommitFailure::RouteDisabled;
    if (!f.frozen_lease_inactive) return SelectionAdmissionCommitFailure::FrozenLeaseActive;
    if (!f.native_route_unowned) return SelectionAdmissionCommitFailure::NativeRouteOwned;
    if (!f.list_cleanup_clear) return SelectionAdmissionCommitFailure::ListCleanupPending;
    if (!f.route_idle) return SelectionAdmissionCommitFailure::RouteNotIdle;
    if (!f.route_generation_exact) return SelectionAdmissionCommitFailure::RouteGenerationDrift;
    if (!f.lifecycle_epoch_exact) return SelectionAdmissionCommitFailure::LifecycleEpochDrift;
    if (!f.proof_predecessor_exact) return SelectionAdmissionCommitFailure::ProofPredecessorDrift;
    if (!f.cleanup_only_clear) return SelectionAdmissionCommitFailure::CleanupOnlyBlocked;
    if (!f.lifecycle_preflight_allowed) return SelectionAdmissionCommitFailure::LifecyclePreflightRejected;
    if (!f.sidecar_pointer_exact) return SelectionAdmissionCommitFailure::SidecarPointerDrift;
    if (!f.sidecar_header_exact) return SelectionAdmissionCommitFailure::SidecarHeaderDrift;
    if (!f.sidecar_allocation_exact) return SelectionAdmissionCommitFailure::SidecarAllocationDrift;
    if (!f.sidecar_mabf_exact) return SelectionAdmissionCommitFailure::SidecarMabfDrift;
    if (!f.final_coordinator_exact) return SelectionAdmissionCommitFailure::FinalCoordinatorRejected;
    if (!f.publication_reached) return SelectionAdmissionCommitFailure::PublicationNotReached;
    if (!f.controller_bind_exact) return SelectionAdmissionCommitFailure::ControllerBindFailed;
    return SelectionAdmissionCommitFailure::None;
}

static_assert(std::is_trivially_copyable_v<SelectionAdmissionSubstrateLiveFacts>);
static_assert(std::is_trivially_copyable_v<CanonicalSubstrateResetRequestLineageFacts>);
static_assert(std::is_trivially_copyable_v<SelectionAdmissionSubstrateRequestRebaseFacts>);
static_assert(std::is_trivially_copyable_v<SelectionAdmissionSubstrateRequestRebaseCommitState>);
static_assert(std::is_trivially_copyable_v<CanonicalSubstrateResetLineageAuthority>);
static_assert(std::is_trivially_copyable_v<CanonicalSubstrateResetLineageCommitFacts>);
static_assert(std::is_trivially_copyable_v<CanonicalSubstrateResetLineageCommitState>);
static_assert(std::is_trivially_copyable_v<CanonicalSubstrateResetLineageUseFacts>);
static_assert(std::is_trivially_copyable_v<SelectionAdmissionProofMatchFacts>);
static_assert(std::is_trivially_copyable_v<SelectionAdmissionCommitFacts>);

struct CanonicalSubstrateBridgePlaySetupFacts {
    bool owner_zero = false;
    bool phase_stop_observed = false;
    bool generation_valid = false;
    bool transaction_valid = false;
    bool release_epoch_valid = false;
    bool revocation_exact = false;
    bool selection_exact = false;
    bool route_exact = false;
    bool lease_exact = false;
    bool song_exact = false;
    bool controller_exact = false;
    bool controller_proof_exact = false;
    bool slot_bgm_exact = false;
    bool old_sound_exact = false;
    bool old_request_exact = false;
    bool callback_sound_exact = false;
    bool callback_identity_exact = false;
    bool canonical_token_nonzero = false;
};

enum class CanonicalSubstrateBridgePlaySetupFailure : uint8_t {
    None,
    OwnerNotZero,
    PhaseNotStopObserved,
    GenerationInvalid,
    TransactionInvalid,
    ReleaseEpochInvalid,
    RevocationDrift,
    SelectionDrift,
    RouteDrift,
    LeaseDrift,
    SongDrift,
    ControllerDrift,
    ControllerProofDrift,
    SlotBgmDrift,
    OldSoundDrift,
    OldRequestDrift,
    CallbackSoundDrift,
    CallbackIdentityDrift,
    CanonicalTokenInvalid,
};

constexpr CanonicalSubstrateBridgePlaySetupFailure
first_canonical_substrate_bridge_play_setup_failure(
    const CanonicalSubstrateBridgePlaySetupFacts& f) noexcept
{
    if (!f.owner_zero) return CanonicalSubstrateBridgePlaySetupFailure::OwnerNotZero;
    if (!f.phase_stop_observed) return CanonicalSubstrateBridgePlaySetupFailure::PhaseNotStopObserved;
    if (!f.generation_valid) return CanonicalSubstrateBridgePlaySetupFailure::GenerationInvalid;
    if (!f.transaction_valid) return CanonicalSubstrateBridgePlaySetupFailure::TransactionInvalid;
    if (!f.release_epoch_valid) return CanonicalSubstrateBridgePlaySetupFailure::ReleaseEpochInvalid;
    if (!f.revocation_exact) return CanonicalSubstrateBridgePlaySetupFailure::RevocationDrift;
    if (!f.selection_exact) return CanonicalSubstrateBridgePlaySetupFailure::SelectionDrift;
    if (!f.route_exact) return CanonicalSubstrateBridgePlaySetupFailure::RouteDrift;
    if (!f.lease_exact) return CanonicalSubstrateBridgePlaySetupFailure::LeaseDrift;
    if (!f.song_exact) return CanonicalSubstrateBridgePlaySetupFailure::SongDrift;
    if (!f.controller_exact) return CanonicalSubstrateBridgePlaySetupFailure::ControllerDrift;
    if (!f.controller_proof_exact) return CanonicalSubstrateBridgePlaySetupFailure::ControllerProofDrift;
    if (!f.slot_bgm_exact) return CanonicalSubstrateBridgePlaySetupFailure::SlotBgmDrift;
    if (!f.old_sound_exact) return CanonicalSubstrateBridgePlaySetupFailure::OldSoundDrift;
    if (!f.old_request_exact) return CanonicalSubstrateBridgePlaySetupFailure::OldRequestDrift;
    if (!f.callback_sound_exact) return CanonicalSubstrateBridgePlaySetupFailure::CallbackSoundDrift;
    if (!f.callback_identity_exact) return CanonicalSubstrateBridgePlaySetupFailure::CallbackIdentityDrift;
    if (!f.canonical_token_nonzero) return CanonicalSubstrateBridgePlaySetupFailure::CanonicalTokenInvalid;
    return CanonicalSubstrateBridgePlaySetupFailure::None;
}

constexpr bool canonical_substrate_bridge_play_setup_exact(
    const CanonicalSubstrateBridgePlaySetupFacts& f) noexcept
{
    return first_canonical_substrate_bridge_play_setup_failure(f)
        == CanonicalSubstrateBridgePlaySetupFailure::None;
}

struct CanonicalSubstrateBridgePatchTransitionFacts {
    bool phase_owner_zero_qualified = false;
    bool authority_generation_exact = false;
    bool old_bridge_route_nonzero = false;
    bool old_setup_route_nonzero = false;
    bool old_bridge_matches_setup = false;
    bool old_route_matches_setup = false;
    bool patched_route_nonzero = false;
    bool patched_route_exact_successor = false;
};

enum class CanonicalSubstrateBridgePatchTransitionFailure : uint8_t {
    None,
    PhaseNotOwnerZeroQualified,
    AuthorityGenerationDrift,
    BridgeRouteGenerationZero,
    SetupRouteGenerationZero,
    BridgeSetupGenerationDrift,
    RouteSetupGenerationDrift,
    PatchedRouteGenerationZero,
    PatchedRouteNotSuccessor,
};

constexpr CanonicalSubstrateBridgePatchTransitionFailure
first_canonical_substrate_bridge_patch_transition_failure(
    const CanonicalSubstrateBridgePatchTransitionFacts& f) noexcept
{
    if (!f.phase_owner_zero_qualified) {
        return CanonicalSubstrateBridgePatchTransitionFailure::PhaseNotOwnerZeroQualified;
    }
    if (!f.authority_generation_exact) {
        return CanonicalSubstrateBridgePatchTransitionFailure::AuthorityGenerationDrift;
    }
    if (!f.old_bridge_route_nonzero) {
        return CanonicalSubstrateBridgePatchTransitionFailure::BridgeRouteGenerationZero;
    }
    if (!f.old_setup_route_nonzero) {
        return CanonicalSubstrateBridgePatchTransitionFailure::SetupRouteGenerationZero;
    }
    if (!f.old_bridge_matches_setup) {
        return CanonicalSubstrateBridgePatchTransitionFailure::BridgeSetupGenerationDrift;
    }
    if (!f.old_route_matches_setup) {
        return CanonicalSubstrateBridgePatchTransitionFailure::RouteSetupGenerationDrift;
    }
    if (!f.patched_route_nonzero) {
        return CanonicalSubstrateBridgePatchTransitionFailure::PatchedRouteGenerationZero;
    }
    if (!f.patched_route_exact_successor) {
        return CanonicalSubstrateBridgePatchTransitionFailure::PatchedRouteNotSuccessor;
    }
    return CanonicalSubstrateBridgePatchTransitionFailure::None;
}

constexpr bool canonical_substrate_bridge_route_generation_successor(
    const uint64_t old_generation,
    const uint64_t patched_generation) noexcept
{
    return old_generation != 0 && old_generation != UINT64_MAX
        && patched_generation == old_generation + 1;
}

struct CanonicalSubstrateBridgePatchTransitionResult {
    CanonicalSubstrateBridgePatchTransitionFailure failure =
        CanonicalSubstrateBridgePatchTransitionFailure::None;
    CanonicalSubstrateBridgePhase phase = CanonicalSubstrateBridgePhase::None;
    uint64_t route_generation = 0;
    bool generation_advanced = false;
};

constexpr CanonicalSubstrateBridgePatchTransitionResult
canonical_substrate_bridge_patch_transition(
    const CanonicalSubstrateBridgePhase current_phase,
    const bool authority_generation_exact,
    const uint64_t bridge_route_generation,
    const uint64_t setup_route_generation,
    const uint64_t route_generation,
    const uint64_t patched_route_generation) noexcept
{
    const CanonicalSubstrateBridgePatchTransitionFacts facts{
        current_phase == CanonicalSubstrateBridgePhase::OwnerZeroQualified,
        authority_generation_exact,
        bridge_route_generation != 0,
        setup_route_generation != 0,
        bridge_route_generation == setup_route_generation,
        route_generation == setup_route_generation,
        patched_route_generation != 0,
        canonical_substrate_bridge_route_generation_successor(
            setup_route_generation, patched_route_generation),
    };
    CanonicalSubstrateBridgePatchTransitionResult result;
    result.failure = first_canonical_substrate_bridge_patch_transition_failure(facts);
    result.phase = result.failure
            == CanonicalSubstrateBridgePatchTransitionFailure::None
        ? CanonicalSubstrateBridgePhase::PatchedOriginalInFlight
        : CanonicalSubstrateBridgePhase::Failed;
    result.route_generation = result.failure
            == CanonicalSubstrateBridgePatchTransitionFailure::None
        ? patched_route_generation : bridge_route_generation;
    result.generation_advanced = result.failure
            == CanonicalSubstrateBridgePatchTransitionFailure::None
        && result.route_generation == patched_route_generation;
    return result;
}

enum class CanonicalSubstrateBridgeRestorePrepatchFailure : uint8_t {
    None,
    RestoreFailed,
    PatchTransitionRejected,
};

struct CanonicalSubstrateBridgeRestorePrepatchResult {
    CanonicalSubstrateBridgeRestorePrepatchFailure failure =
        CanonicalSubstrateBridgeRestorePrepatchFailure::None;
    CanonicalSubstrateBridgePatchTransitionResult transition{};
    bool post_restore_check_attempted = false;
    bool post_restore_exact = false;
    bool new_patch_allowed = false;
    bool original_allowed = false;
};

constexpr CanonicalSubstrateBridgeRestorePrepatchResult
canonical_substrate_bridge_restore_prepatch(
    const bool restore_succeeded,
    const CanonicalSubstrateBridgePhase current_phase,
    const bool authority_generation_exact,
    const uint64_t bridge_route_generation,
    const uint64_t setup_route_generation,
    const uint64_t route_generation_after_restore,
    const uint64_t patched_route_generation) noexcept
{
    CanonicalSubstrateBridgeRestorePrepatchResult result;
    if (!restore_succeeded) {
        result.failure = CanonicalSubstrateBridgeRestorePrepatchFailure::RestoreFailed;
        result.transition.phase = CanonicalSubstrateBridgePhase::Failed;
        result.transition.route_generation = bridge_route_generation;
        return result;
    }
    result.post_restore_check_attempted = true;
    result.transition = canonical_substrate_bridge_patch_transition(
        current_phase,
        authority_generation_exact,
        bridge_route_generation,
        setup_route_generation,
        route_generation_after_restore,
        patched_route_generation);
    result.post_restore_exact = result.transition.failure
        == CanonicalSubstrateBridgePatchTransitionFailure::None;
    result.failure = result.post_restore_exact
        ? CanonicalSubstrateBridgeRestorePrepatchFailure::None
        : CanonicalSubstrateBridgeRestorePrepatchFailure::PatchTransitionRejected;
    result.new_patch_allowed = result.post_restore_exact;
    result.original_allowed = result.post_restore_exact;
    return result;
}

struct CanonicalSubstrateBridgeSetCandidateFacts {
    bool phase_patched_original = false;
    bool generation_valid = false;
    bool original_inflight = false;
    bool route_phase_exact = false;
    bool route_generation_exact = false;
    bool lease_exact = false;
    bool song_exact = false;
    bool setup_token_exact = false;
    bool selection_exact = false;
    bool revocation_exact = false;
    bool controller_exact = false;
    bool sound_nonnull = false;
    bool sound_pointer_exact = false;
};

enum class CanonicalSubstrateBridgeSetFailure : uint8_t {
    None,
    PhaseNotPatchedOriginal,
    GenerationInvalid,
    OriginalNotInFlight,
    RoutePhaseDrift,
    RouteGenerationDrift,
    LeaseDrift,
    SongDrift,
    SetupTokenDrift,
    SelectionDrift,
    RevocationDrift,
    ControllerDrift,
    SoundNull,
    SoundPointerDrift,
    IdentityReadFailed,
    IdentityIndexDrift,
    IdentitySerialDrift,
    DuplicateSet,
};

constexpr CanonicalSubstrateBridgeSetFailure
first_canonical_substrate_bridge_set_candidate_failure(
    const CanonicalSubstrateBridgeSetCandidateFacts& f) noexcept
{
    if (!f.phase_patched_original) return CanonicalSubstrateBridgeSetFailure::PhaseNotPatchedOriginal;
    if (!f.generation_valid) return CanonicalSubstrateBridgeSetFailure::GenerationInvalid;
    if (!f.original_inflight) return CanonicalSubstrateBridgeSetFailure::OriginalNotInFlight;
    if (!f.route_phase_exact) return CanonicalSubstrateBridgeSetFailure::RoutePhaseDrift;
    if (!f.route_generation_exact) return CanonicalSubstrateBridgeSetFailure::RouteGenerationDrift;
    if (!f.lease_exact) return CanonicalSubstrateBridgeSetFailure::LeaseDrift;
    if (!f.song_exact) return CanonicalSubstrateBridgeSetFailure::SongDrift;
    if (!f.setup_token_exact) return CanonicalSubstrateBridgeSetFailure::SetupTokenDrift;
    if (!f.selection_exact) return CanonicalSubstrateBridgeSetFailure::SelectionDrift;
    if (!f.revocation_exact) return CanonicalSubstrateBridgeSetFailure::RevocationDrift;
    if (!f.controller_exact) return CanonicalSubstrateBridgeSetFailure::ControllerDrift;
    if (!f.sound_nonnull) return CanonicalSubstrateBridgeSetFailure::SoundNull;
    if (!f.sound_pointer_exact) return CanonicalSubstrateBridgeSetFailure::SoundPointerDrift;
    return CanonicalSubstrateBridgeSetFailure::None;
}

constexpr bool canonical_substrate_bridge_set_identity_read_required(
    const CanonicalSubstrateBridgeSetCandidateFacts& f) noexcept
{
    return first_canonical_substrate_bridge_set_candidate_failure(f)
        == CanonicalSubstrateBridgeSetFailure::None;
}

struct CanonicalSubstrateBridgeSetIdentityFacts {
    bool read_succeeded = false;
    bool index_exact = false;
    bool serial_exact = false;
};

constexpr CanonicalSubstrateBridgeSetFailure
first_canonical_substrate_bridge_set_identity_failure(
    const CanonicalSubstrateBridgeSetIdentityFacts& f) noexcept
{
    if (!f.read_succeeded) return CanonicalSubstrateBridgeSetFailure::IdentityReadFailed;
    if (!f.index_exact) return CanonicalSubstrateBridgeSetFailure::IdentityIndexDrift;
    if (!f.serial_exact) return CanonicalSubstrateBridgeSetFailure::IdentitySerialDrift;
    return CanonicalSubstrateBridgeSetFailure::None;
}

constexpr CanonicalSubstrateBridgePhase canonical_substrate_bridge_set_result_phase(
    const CanonicalSubstrateBridgePhase current,
    const CanonicalSubstrateBridgeSetFailure failure) noexcept
{
    if (current == CanonicalSubstrateBridgePhase::PatchedOriginalInFlight) {
        return failure == CanonicalSubstrateBridgeSetFailure::None
            ? CanonicalSubstrateBridgePhase::SetBound
            : CanonicalSubstrateBridgePhase::Failed;
    }
    if (current == CanonicalSubstrateBridgePhase::SetBound) {
        return CanonicalSubstrateBridgePhase::Failed;
    }
    return current;
}

} // namespace ff7r::piano::game
