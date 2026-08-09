struct SelectionActivationClaim::Impl final {
    SelectionSnapshot frozen_selection;
    uint64_t reservation_generation = 0;
    uint64_t menu_session_generation = 0;
    uint64_t handoff_selection_generation = 0;
    void* menu_controller = nullptr;
    void* bgm_controller = nullptr;
    ControllerIdentityProof bgm_controller_proof{};
    void* wrapper = nullptr;
    bool armed = false;
};

namespace {

struct SelectionActivationReservationRecord final {
    SelectionSnapshot selection;
    SelectionActivationListContext list_context{};
    uint64_t generation = 0;
    uint64_t menu_session_generation = 0;
    void* menu_controller = nullptr;
    bool selection_handoff_confirmed = false;
    bool same_selection_notification_confirmed = false;
    SelectionActivationTransferState transfer_state
        = SelectionActivationTransferState::Pending;
    const SidecarRuntimeState* sidecar = nullptr;
    void* sidecar_header = nullptr;
    size_t sidecar_allocation_size = 0;
    size_t sidecar_mabf_size = 0;
    uint64_t route_generation = 0;
    uint64_t next_lease_generation = 0;
    uint64_t lifecycle_state_epoch = 0;
    void* controller = nullptr;
    ControllerIdentityProof controller_proof{};
    bool canonical_substrate_proof_exact = false;
    uint64_t canonical_substrate_request = 0;
    UObjectLiveHandle canonical_substrate_sound_identity{};
    uint64_t canonical_substrate_source_ordinal = 0;
    uint64_t canonical_substrate_list_exit_epoch = 0;
    uint64_t canonical_substrate_transaction_generation = 0;
    CanonicalSubstrateBridgeAuthority substrate_bridge{};
    bool substrate_phase_attempt_active = false;
    uint64_t substrate_phase_attempt_revocation_epoch = 0;
    BgmCanonicalSubstrateProof substrate_available_proof{};
    BgmCanonicalSubstrateProof substrate_reserved_proof{};
};

bool canonical_substrate_reset_lineage_matches(
    const CanonicalSubstrateResetLineageAuthority& left,
    const CanonicalSubstrateResetLineageAuthority& right) noexcept
{
    return canonical_substrate_reset_lineage_authority_matches(left, right);
}

CanonicalSubstrateResetLineageUseFacts canonical_substrate_route_use_facts(
    const BgmCanonicalSubstrateProof& proof,
    const uint64_t current_route_generation,
    const AudioRouteLeaseIdentity current_lease,
    const uint64_t current_lifecycle_epoch,
    const uint64_t reset_observation_generation,
    const uint64_t reset_lineage_generation,
    const CanonicalSubstrateResetLineagePhase expected_authority_phase,
    const CanonicalSubstrateBridgePhase expected_bridge_phase) noexcept
{
    const auto& authority = proof.reset_lineage;
    const bool bridge_stage_exact =
        canonical_substrate_bridge_predecessor_stage_exact(
            proof.bridge, expected_bridge_phase);
    return {
        bgm_playback_exact_successor(
            proof.route_generation, current_route_generation)
            && authority.phase == CanonicalSubstrateResetLineagePhase::None,
        bridge_stage_exact,
        current_route_generation == 0 && !current_lease.valid(),
        authority.phase == expected_authority_phase
            && proof.bridge.phase == expected_bridge_phase,
        authority.generation != 0 && authority.rearm_epoch != 0
            && authority.generation == reset_lineage_generation,
        authority.reset_observation_generation == reset_observation_generation,
        authority.reset_route_generation > authority.proof_route_generation
            && authority.reset_route_generation != UINT64_MAX,
        authority.reset_route_generation
                == proof.request_rebase_route_generation
            && authority.reset_lease == proof.request_rebase_lease
            && authority.reset_lifecycle_epoch
                == proof.request_rebase_lifecycle_epoch,
        authority.reset_lifecycle_epoch == current_lifecycle_epoch,
        authority.transaction_generation == proof.transaction_generation,
        authority.source_ordinal == proof.source_ordinal
            && authority.source_version == proof.source_version
            && authority.source_collection_version
                == proof.source_collection_version,
        authority.list_exit_epoch == proof.list_exit_epoch,
        authority.proof_route_generation == proof.route_generation,
        authority.proof_lease == proof.lease,
        authority.proof_lifecycle_epoch == proof.lifecycle_state_epoch,
        authority.request == proof.request,
        authority.canonical_token == proof.canonical && proof.canonical != 0
            && proof.custom == 0,
        authority.bridge_generation == proof.bridge.generation,
        authority.bridge_release_epoch == proof.bridge.release_completion_epoch,
        authority.controller == proof.controller,
        controller_identity_proof_matches(
            authority.controller_proof, proof.controller_proof),
        authority.slot == proof.slot && authority.bgm == proof.bgm,
        authority.sound == proof.sound,
        private_object_handle_matches(
            authority.sound_identity, proof.sound_identity),
    };
}

bool canonical_substrate_proof_matches(
    const BgmCanonicalSubstrateProof& left,
    const BgmCanonicalSubstrateProof& right,
    SelectionAdmissionProofMatchFacts* const diagnostic = nullptr) noexcept
{
    SelectionAdmissionProofMatchFacts facts;
    auto finish = [&](const bool value) noexcept {
        if (diagnostic) *diagnostic = facts;
        return value;
    };
    if (!(facts.left_active = left.active)) return finish(false);
    if (!(facts.right_active = right.active)) return finish(false);
    if (!(facts.left_state_exact = left.state
            == BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished)) {
        return finish(false);
    }
    if (!(facts.right_state_exact = right.state
            == BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished)) {
        return finish(false);
    }
    if (!(facts.transaction_nonzero = left.transaction_generation != 0)) return finish(false);
    if (!(facts.transaction_exact = left.transaction_generation == right.transaction_generation)) return finish(false);
    if (!(facts.source_ordinal_exact = left.source_ordinal == right.source_ordinal)) return finish(false);
    if (!(facts.source_version_exact = left.source_version == right.source_version)) return finish(false);
    if (!(facts.collection_version_exact = left.source_collection_version == right.source_collection_version)) return finish(false);
    if (!(facts.list_exit_exact = left.list_exit_epoch == right.list_exit_epoch)) return finish(false);
    if (!(facts.controller_pointer_exact = left.controller == right.controller)) return finish(false);
    if (!(facts.controller_identity_exact = controller_identity_proof_matches(
            left.controller_proof, right.controller_proof))) return finish(false);
    if (!(facts.slot_bgm_exact = left.slot == right.slot && left.bgm == right.bgm)) return finish(false);
    if (!(facts.sound_pointer_exact = left.sound == right.sound)) return finish(false);
    if (!(facts.sound_index_exact = left.sound_identity.internal_index
            == right.sound_identity.internal_index)) return finish(false);
    if (!(facts.sound_serial_exact = left.sound_identity.serial_number
            == right.sound_identity.serial_number)) return finish(false);
    if (!(facts.request_exact = left.request == right.request)) return finish(false);
    facts.request_rebase_lineage_exact = left.request_rebase_route_generation
            == right.request_rebase_route_generation
        && left.request_rebase_lease == right.request_rebase_lease
        && left.request_rebase_lifecycle_epoch
            == right.request_rebase_lifecycle_epoch;
    if (!facts.request_rebase_lineage_exact) return finish(false);
    if (!(facts.route_generation_exact = left.route_generation == right.route_generation)) return finish(false);
    if (!(facts.lease_exact = left.lease == right.lease)) return finish(false);
    if (!(facts.lifecycle_epoch_exact = left.lifecycle_state_epoch == right.lifecycle_state_epoch)) return finish(false);
    if (!(facts.canonical_token_exact = left.canonical == right.canonical)) return finish(false);
    if (!(facts.custom_token_exact = left.custom == right.custom)) return finish(false);
    if (!(facts.bridge_generation_exact = left.bridge.generation == right.bridge.generation)) return finish(false);
    if (!(facts.bridge_phase_exact = left.bridge.phase == right.bridge.phase)) return finish(false);
    if (!(facts.bridge_release_epoch_exact = left.bridge.release_completion_epoch
            == right.bridge.release_completion_epoch)) return finish(false);
    if (!(facts.bridge_callback_pointer_exact = left.bridge.expected_callback_sound
            == right.bridge.expected_callback_sound)) return finish(false);
    if (!(facts.bridge_callback_identity_exact = private_object_handle_matches(
            left.bridge.expected_callback_sound_identity,
            right.bridge.expected_callback_sound_identity))) return finish(false);
    facts.bridge_canonical_token_exact = left.bridge.canonical_token
        == right.bridge.canonical_token;
    if (!facts.bridge_canonical_token_exact) return finish(false);
    facts.bridge_authority_exact = canonical_substrate_bridge_authority_matches(
        left.bridge, right.bridge);
    if (!facts.bridge_authority_exact) return finish(false);
    facts.reset_lineage_exact = canonical_substrate_reset_lineage_matches(
        left.reset_lineage, right.reset_lineage);
    return finish(facts.reset_lineage_exact);
}

std::mutex g_selection_activation_reservation_mutex;
SelectionActivationReservationMachine g_selection_activation_reservation_machine;
SelectionActivationReservationRecord g_selection_activation_reservation;

bool rollback_selection_activation_substrate_phases(
    SelectionActivationReservationRecord& attempt,
    const bool reservation_lock_held = false,
    const SelectionActivationSubstrateAttemptDisposition disposition =
        SelectionActivationSubstrateAttemptDisposition::RestoreIfExact,
    const bool native_identity_exact = true,
    const bool preserve_attempt_on_failure = false) noexcept
{
    if (!attempt.substrate_phase_attempt_active) return false;
    try {
        const auto rollback_locked = [&]() noexcept {
            const bool attempt_active =
                attempt.substrate_phase_attempt_active;
            const auto& expected = attempt.substrate_reserved_proof;
            const auto& available = attempt.substrate_available_proof;
            const auto machine_state =
                g_selection_activation_reservation_machine.state();
            const bool reserved_owner =
                machine_state == SelectionActivationReservationState::Reserved
                && g_selection_activation_reservation.generation
                    == attempt.generation
                && g_selection_activation_reservation
                    .substrate_phase_attempt_active;
            const bool consumed_owner =
                machine_state == SelectionActivationReservationState::Consumed
                && g_selection_activation_reservation.generation
                    == attempt.generation
                && !g_selection_activation_reservation
                    .substrate_phase_attempt_active;
            auto linked_available = available;
            linked_available.bridge = expected.bridge;
            linked_available.reset_lineage.phase = expected.reset_lineage.phase;
            const bool proof_exact = canonical_substrate_proof_matches(
                    g_bgm_canonical_substrate_proof, expected)
                && canonical_substrate_proof_matches(
                    linked_available, expected)
                && canonical_substrate_bridge_reservation_successor_exact(
                    available.bridge, expected.bridge);
            const bool reserved_reset_lineage = expected.reset_lineage.phase
                == CanonicalSubstrateResetLineagePhase::Reserved;
            SelectionActivationSubstrateAttemptFinalizeState state{
                machine_state,
                g_selection_activation_reservation.generation,
                g_selection_activation_reservation
                    .substrate_phase_attempt_active,
                g_bgm_canonical_substrate_proof.bridge.phase,
                g_bgm_canonical_substrate_proof.reset_lineage.phase,
                expected.reset_lineage.rearm_epoch};
            const SelectionActivationSubstrateAttemptFinalizeFacts facts{
                attempt_active,
                g_selection_activation_reservation_machine.generation()
                    == attempt.generation,
                reserved_owner || consumed_owner,
                g_selection_activation_reservation.generation
                    == attempt.generation,
                reserved_owner || consumed_owner,
                g_selection_activation_revocation_epoch.load(
                    std::memory_order_acquire)
                    == attempt.substrate_phase_attempt_revocation_epoch,
                native_identity_exact,
                proof_exact,
                g_bgm_canonical_substrate_proof.bridge.generation
                    == expected.bridge.generation,
                g_bgm_canonical_substrate_proof.transaction_generation
                    == expected.transaction_generation,
                g_bgm_canonical_substrate_proof.bridge.phase
                    == CanonicalSubstrateBridgePhase::Reserved,
                g_bgm_canonical_substrate_proof.reset_lineage.phase
                    == expected.reset_lineage.phase};
            const bool finalized =
                finalize_selection_activation_substrate_attempt(
                    facts, disposition, reserved_reset_lineage, state);
            if (!finalized) {
                if (!preserve_attempt_on_failure)
                    attempt.substrate_phase_attempt_active = false;
                return false;
            }
            attempt.substrate_phase_attempt_active = false;
            const bool retired = reserved_owner
                ? (g_selection_activation_reservation_machine.revoke(), true)
                : g_selection_activation_reservation_machine.retire_consumed(
                    attempt.generation);
            if (!retired) return false;
            g_selection_activation_reservation = {};
            if (state.phases_restored) {
                g_bgm_canonical_substrate_proof.bridge = available.bridge;
                g_bgm_canonical_substrate_proof.reset_lineage.phase =
                    available.reset_lineage.phase;
            }
            return state.phases_restored;
        };
        if (reservation_lock_held) {
            std::lock_guard<std::mutex> borrower_lock(
                g_bgm_playback_borrower_mutex);
            return rollback_locked();
        }
        std::unique_lock<std::recursive_mutex> operation_lock(
            g_audio_route_operations.mutex());
        std::lock_guard<std::mutex> reservation_lock(
            g_selection_activation_reservation_mutex);
        std::lock_guard<std::mutex> borrower_lock(
            g_bgm_playback_borrower_mutex);
        return rollback_locked();
    } catch (...) {
        if (!preserve_attempt_on_failure)
            attempt.substrate_phase_attempt_active = false;
        return false;
    }
}

bool retire_consumed_selection_activation_reservation(
    const uint64_t expected_generation) noexcept
{
    if (expected_generation == 0) return false;
    try {
        std::unique_lock<std::recursive_mutex> operation_lock(
            g_audio_route_operations.mutex());
        std::lock_guard<std::mutex> reservation_lock(
            g_selection_activation_reservation_mutex);
        if (g_selection_activation_reservation_machine.state()
                != SelectionActivationReservationState::Consumed
            || g_selection_activation_reservation.generation
                != expected_generation
            || g_selection_activation_reservation
                .substrate_phase_attempt_active
            || !g_selection_activation_reservation_machine.retire_consumed(
                expected_generation)) {
            return false;
        }
        g_selection_activation_reservation = {};
        return true;
    } catch (...) {
        return false;
    }
}

bool canonical_substrate_active_sound_live_exact(
    const BgmCanonicalSubstrateProof& proof,
    void* controller,
    const ControllerIdentityProof& controller_proof,
    SelectionAdmissionSubstrateLiveFacts* const diagnostic = nullptr) noexcept
{
    SelectionAdmissionSubstrateLiveFacts facts;
    facts.observed_controller = reinterpret_cast<uintptr_t>(controller);
    auto finish = [&](const bool value) noexcept {
        if (diagnostic) *diagnostic = facts;
        return value;
    };
    if (!(facts.proof_active = proof.active)) return finish(false);
    if (!(facts.proof_state_exact = proof.state
            == BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished)) {
        return finish(false);
    }
    if (!(facts.transaction_nonzero = proof.transaction_generation != 0)) return finish(false);
    if (!(facts.controller_nonnull = controller != nullptr)) return finish(false);
    if (!(facts.controller_pointer_exact = controller == proof.controller)) return finish(false);
    if (!(facts.controller_identity_exact = controller_identity_proof_matches(
            controller_proof, proof.controller_proof))) return finish(false);
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint64_t request = 0;
    uint8_t state = 0;
    UObjectIdentity sound_identity{};
    if (!(facts.chain_read = read_controller_bgm_chain(controller, slot, bgm))) return finish(false);
    facts.observed_slot = reinterpret_cast<uintptr_t>(slot);
    facts.observed_bgm = reinterpret_cast<uintptr_t>(bgm);
    if (!(facts.sound_read = core::safe_read_field(
            bgm, runtime_layouts::SqexSeadBgm::sound, sound))) return finish(false);
    facts.observed_sound = reinterpret_cast<uintptr_t>(sound);
    if (!(facts.request_read = core::safe_read_field(
            bgm, runtime_layouts::SqexSeadBgm::request_handle, request))) return finish(false);
    facts.observed_request = request;
    if (!(facts.state_read = core::safe_read_field(
            slot, runtime_layouts::SqexSeadSlot::state, state))) return finish(false);
    facts.observed_state = state;
    if (!(facts.sound_nonnull = sound != nullptr)) return finish(false);
    if (!(facts.sound_identity_read = read_uobject_identity(sound, sound_identity))) return finish(false);
    facts.observed_sound_index = sound_identity.live.internal_index;
    facts.observed_sound_serial = sound_identity.live.serial_number;
    if (!(facts.slot_bgm_exact = slot == proof.slot && bgm == proof.bgm)) return finish(false);
    if (!(facts.sound_pointer_exact = sound == proof.sound)) return finish(false);
    if (!(facts.sound_identity_exact =
            sound_identity.live.internal_index == proof.sound_identity.internal_index
            && sound_identity.live.serial_number == proof.sound_identity.serial_number)) {
        return finish(false);
    }
    facts.request_exact = request == proof.request;
    facts.state_four = state == 4;
    return finish(facts.request_exact && facts.state_four);
}

bool advance_selection_activation_revocation_epoch_noexcept(
    uint64_t* advanced_epoch = nullptr) noexcept;

bool try_rebase_ready_canonical_substrate_request(
    void* controller, CanonicalSubstrateRequestRebaseMarker& marker) noexcept
{
    try {
        AggregateMutationLease aggregate_lease;
        if (!aggregate_lease.active) return false;
        const auto ownership = snapshot_bgm_aggregate_exit_ownership(true, false);
        if (!bgm_playback_aggregate_menu_ready(ownership,
                g_bgm_aggregate_exit_requested.load(std::memory_order_acquire),
                g_bgm_aggregate_exit_pending.load(std::memory_order_acquire))) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (!g_audio_route_installed.load(std::memory_order_acquire)
                || g_audio_route_disabled.load(std::memory_order_acquire)
                || native_audio_route_owned_locked()
                || g_audio_route_state.list_cleanup_pending
                || g_audio_route_state.phase != AudioRoutePhase::Idle) {
                return false;
            }
        }
        AudioRouteState rebase_route;
        uint64_t rebase_lifecycle_epoch = 0;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            rebase_route = g_audio_route_state;
            rebase_lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
        }

        UObjectIdentity controller_identity{};
        if (!controller || lookup_current_bgm_controller() != controller
            || !read_uobject_identity(controller, controller_identity)) {
            return false;
        }
        const ControllerIdentityProof controller_proof =
            controller_identity_proof(controller, controller_identity);
        if (!controller_identity_proof_valid(controller_proof)) return false;

        BgmCanonicalSubstrateProof retained;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            retained = g_bgm_canonical_substrate_proof;
        }
        SelectionAdmissionSubstrateLiveFacts live;
        if (canonical_substrate_active_sound_live_exact(
                retained, controller, controller_proof, &live)) {
            return false;
        }
        const auto rebase_facts = [&]() noexcept {
            return SelectionAdmissionSubstrateRequestRebaseFacts{
                aggregate_lease.active,
                first_selection_admission_substrate_live_failure(live),
                live.state_four,
                retained.canonical != 0,
                retained.custom == 0,
                retained.bridge.phase == CanonicalSubstrateBridgePhase::Available,
                retained.bridge.canonical_token == retained.canonical,
                retained.bridge.old_canonical_request == retained.request,
                retained.request,
                live.observed_request,
            };
        }();
        const auto failure =
            first_selection_admission_substrate_request_rebase_failure(rebase_facts);
        if (failure != SelectionAdmissionSubstrateRequestRebaseFailure::None) {
            return false;
        }

        const uint64_t old_request = retained.request;
        uint64_t new_request = 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            auto& current = g_bgm_canonical_substrate_proof;
            if (!canonical_substrate_proof_matches(current, retained)) return false;

            SelectionAdmissionSubstrateLiveFacts commit_live;
            if (canonical_substrate_active_sound_live_exact(
                    current, controller, controller_proof, &commit_live)) {
                return false;
            }
            const SelectionAdmissionSubstrateRequestRebaseFacts commit_facts{
                aggregate_lease.active,
                first_selection_admission_substrate_live_failure(commit_live),
                commit_live.state_four,
                current.canonical != 0,
                current.custom == 0,
                current.bridge.phase == CanonicalSubstrateBridgePhase::Available,
                current.bridge.canonical_token == current.canonical,
                current.bridge.old_canonical_request == current.request,
                current.request,
                commit_live.observed_request,
            };
            if (!selection_admission_substrate_request_rebase_exact(commit_facts)) {
                return false;
            }
            if (current.reset_lineage.phase
                != CanonicalSubstrateResetLineagePhase::None) return false;
            SelectionAdmissionSubstrateRequestRebaseCommitState commit_state{
                current.request,
                current.bridge.old_canonical_request,
                g_selection_activation_revocation_epoch.load(
                    std::memory_order_acquire),
            };
            if (!commit_selection_admission_substrate_request_rebase(
                    commit_facts, commit_state)) return false;
            uint64_t expected_epoch = commit_state.rearm_epoch - 1;
            if (!g_selection_activation_revocation_epoch.compare_exchange_strong(
                    expected_epoch, commit_state.rearm_epoch,
                    std::memory_order_release, std::memory_order_relaxed)) {
                return false;
            }
            current.request = commit_state.proof_request;
            current.bridge.old_canonical_request = commit_state.bridge_old_request;
            current.request_rebase_route_generation = rebase_route.generation;
            current.request_rebase_lease = rebase_route.lease_identity;
            current.request_rebase_lifecycle_epoch = rebase_lifecycle_epoch;
            new_request = commit_state.proof_request;
        }

        // The orchestration commit atomically advanced exactly one rearm epoch
        // together with the two linked request observations.
        marker = {true, old_request, new_request,
            retained.transaction_generation, retained.source_ordinal,
            retained.sound_identity};
        return true;
    } catch (...) {
        return false;
    }
}

struct CanonicalSubstrateResetLineageMarker final {
    bool eligible = false;
    uint64_t authority_generation = 0;
    uint64_t reset_observation_generation = 0;
    uint64_t reset_route_generation = 0;
    uint64_t proof_route_generation = 0;
    uint64_t request = 0;
    uint64_t transaction_generation = 0;
};

bool try_qualify_canonical_substrate_reset_lineage(
    const AudioRouteState& reset_route,
    const bool cleanup_verified,
    const bool reset_native_unowned,
    const uint64_t reset_observation_generation,
    CanonicalSubstrateResetLineageMarker& marker) noexcept
{
    try {
        AggregateMutationLease aggregate_lease;
        if (!aggregate_lease.active) return false;
        const auto ownership = snapshot_bgm_aggregate_exit_ownership(true, false);
        const bool aggregate_clear = bgm_playback_aggregate_menu_ready(ownership,
            g_bgm_aggregate_exit_requested.load(std::memory_order_acquire),
            g_bgm_aggregate_exit_pending.load(std::memory_order_acquire));

        bool reset_state_exact = false;
        bool current_zero_idle = false;
        bool cleanup_clear = false;
        bool quarantine_clear = false;
        bool lifecycle_allowed = false;
        uint64_t current_lifecycle_epoch = 0;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            reset_state_exact = reset_route.phase == AudioRoutePhase::Idle
                && reset_route.generation != 0
                && !reset_route.custom_resource_owned
                && !reset_route.list_cleanup_pending;
            current_zero_idle = g_audio_route_state.phase == AudioRoutePhase::Idle
                && g_audio_route_state.generation == 0
                && g_audio_route_state.lease_identity
                    == AudioRouteLeaseIdentity{};
            cleanup_clear = !g_audio_route_state.list_cleanup_pending
                && !g_onmemory_bank_cleanup_only.blocks_custom_routes();
            quarantine_clear = !g_custom_activation_quarantine;
            lifecycle_allowed = g_onmemory_bank_lifecycle.preflight_arm(
                true,
                g_audio_route_installed.load(std::memory_order_acquire),
                g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
                g_onmemory_bank_release_available.load(std::memory_order_acquire))
                == OnMemoryBankRouteDecision::Allowed;
            current_lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
        }

        void* const controller = lookup_current_bgm_controller();
        UObjectIdentity controller_identity{};
        if (!controller || !read_uobject_identity(controller, controller_identity)) {
            return false;
        }
        const auto controller_proof = controller_identity_proof(
            controller, controller_identity);
        if (!controller_identity_proof_valid(controller_proof)) return false;

        BgmCanonicalSubstrateProof retained;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            retained = g_bgm_canonical_substrate_proof;
        }
        SelectionAdmissionSubstrateLiveFacts live;
        const bool substrate_live_exact = canonical_substrate_active_sound_live_exact(
            retained, controller, controller_proof, &live);
        const CanonicalSubstrateResetRequestLineageFacts request_lineage_facts{
            first_selection_admission_substrate_live_failure(live),
            retained.request_rebase_route_generation == reset_route.generation
                && retained.request_rebase_route_generation != 0,
            retained.request_rebase_lease == reset_route.lease_identity,
            retained.request_rebase_lifecycle_epoch == current_lifecycle_epoch,
            retained.request_rebase_lease.valid()
                && reset_route.lease_identity.valid(),
            retained.request_rebase_route_generation == 0,
            retained.request_rebase_lease == AudioRouteLeaseIdentity{},
            retained.request_rebase_lifecycle_epoch == 0,
            canonical_substrate_reset_continuity_route_exact(
                retained.route_generation,
                reset_route.generation,
                reset_route.lease_identity),
        };
        const auto& bridge = retained.bridge;
        const CanonicalSubstrateResetLineageCommitFacts facts{
            aggregate_lease.active,
            cleanup_verified,
            aggregate_clear,
            g_audio_route_installed.load(std::memory_order_acquire),
            !g_audio_route_disabled.load(std::memory_order_acquire),
            reset_route.phase == AudioRoutePhase::Idle,
            reset_route.generation != 0,
            reset_route.generation > retained.route_generation,
            reset_route.generation != UINT64_MAX,
            reset_state_exact
                && reset_native_unowned
                && reset_observation_generation != 0
                && reset_observation_generation
                    == g_canonical_substrate_reset_observation_generation,
            canonical_substrate_reset_request_lineage_exact(
                request_lineage_facts),
            current_zero_idle,
            cleanup_clear,
            quarantine_clear,
            lifecycle_allowed,
            substrate_live_exact,
            retained.canonical != 0,
            retained.custom == 0,
            bridge.phase == CanonicalSubstrateBridgePhase::Available,
            bridge.generation != 0,
            bridge.transaction_generation == retained.transaction_generation,
            bridge.source_ordinal == retained.source_ordinal
                && bridge.source_version == retained.source_version
                && bridge.source_collection_version
                    == retained.source_collection_version,
            bridge.list_exit_epoch == retained.list_exit_epoch,
            bridge.canonical_token == retained.canonical,
            bridge.old_canonical_request == retained.request,
            bridge.controller == retained.controller
                && controller_identity_proof_matches(
                    bridge.controller_proof, retained.controller_proof),
            bridge.slot == retained.slot && bridge.bgm == retained.bgm,
            canonical_substrate_reset_bridge_sound_exact({
                bridge.expected_callback_sound != nullptr,
                private_object_handle_valid(
                    bridge.expected_callback_sound_identity),
                bridge.old_canonical_sound == retained.sound,
                private_object_handle_matches(
                    bridge.old_canonical_sound_identity,
                    retained.sound_identity),
            }),
            retained.reset_lineage.phase
                == CanonicalSubstrateResetLineagePhase::None,
            g_canonical_substrate_reset_lineage_generation != UINT64_MAX,
            g_selection_activation_revocation_epoch.load(
                std::memory_order_acquire) != UINT64_MAX,
        };
        if (first_canonical_substrate_reset_lineage_commit_failure(facts)
            != CanonicalSubstrateResetLineageCommitFailure::None) return false;

        CanonicalSubstrateResetLineageAuthority linked;
        linked.reset_route_generation = reset_route.generation;
        linked.reset_observation_generation = reset_observation_generation;
        linked.reset_lease = reset_route.lease_identity;
        linked.reset_lifecycle_epoch = current_lifecycle_epoch;
        linked.transaction_generation = retained.transaction_generation;
        linked.source_ordinal = retained.source_ordinal;
        linked.source_version = retained.source_version;
        linked.source_collection_version = retained.source_collection_version;
        linked.list_exit_epoch = retained.list_exit_epoch;
        linked.proof_route_generation = retained.route_generation;
        linked.proof_lease = retained.lease;
        linked.proof_lifecycle_epoch = retained.lifecycle_state_epoch;
        linked.request = retained.request;
        linked.canonical_token = retained.canonical;
        linked.bridge_generation = bridge.generation;
        linked.bridge_release_epoch = bridge.release_completion_epoch;
        linked.controller = retained.controller;
        linked.controller_proof = retained.controller_proof;
        linked.slot = retained.slot;
        linked.bgm = retained.bgm;
        linked.sound = retained.sound;
        linked.sound_identity = retained.sound_identity;

        uint64_t authority_generation = 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            auto& current = g_bgm_canonical_substrate_proof;
            if (!canonical_substrate_proof_matches(current, retained)
                || !canonical_substrate_active_sound_live_exact(
                    current, controller, controller_proof)) return false;
            CanonicalSubstrateResetLineageCommitState state{
                g_canonical_substrate_reset_lineage_generation,
                g_selection_activation_revocation_epoch.load(
                    std::memory_order_acquire),
                current.request_rebase_route_generation,
                current.request_rebase_lease,
                current.request_rebase_lifecycle_epoch,
                {},
            };
            if (!commit_canonical_substrate_reset_lineage(facts, linked, state)) {
                return false;
            }
            uint64_t expected_epoch = state.rearm_epoch - 1;
            if (!g_selection_activation_revocation_epoch.compare_exchange_strong(
                    expected_epoch, state.rearm_epoch,
                    std::memory_order_release, std::memory_order_relaxed)) {
                return false;
            }
            g_canonical_substrate_reset_lineage_generation =
                state.authority_generation;
            current.request_rebase_route_generation =
                state.request_lineage_route_generation;
            current.request_rebase_lease = state.request_lineage_lease;
            current.request_rebase_lifecycle_epoch =
                state.request_lineage_lifecycle_epoch;
            current.reset_lineage = state.authority;
            authority_generation = state.authority_generation;
        }
        marker = {true, authority_generation, reset_observation_generation,
            reset_route.generation,
            retained.route_generation, retained.request,
            retained.transaction_generation};
        return true;
    } catch (...) {
        return false;
    }
}

bool selection_snapshot_exact(
    const SelectionSnapshot& left, const SelectionSnapshot& right) noexcept
{
    return left && right && left.generation == right.generation
        && left.storage == right.storage && left.song == right.song
        && left.profile == right.profile
        && left.profile_index == right.profile_index
        && left.visible_index == right.visible_index
        && left.base_slot == right.base_slot;
}

bool activation_list_context_exact(
    const SelectionActivationListContext& left,
    const SelectionActivationListContext& right) noexcept
{
    return left && right && left.object == right.object
        && left.internal_index == right.internal_index
        && left.serial_number == right.serial_number;
}

bool activation_reservation_record_exact(
    const SelectionActivationReservationRecord& left,
    const SelectionActivationReservationRecord& right) noexcept
{
    return selection_snapshot_exact(left.selection, right.selection)
        && activation_list_context_exact(left.list_context, right.list_context)
        && left.menu_session_generation == right.menu_session_generation
        && left.menu_controller == right.menu_controller
        && left.selection_handoff_confirmed
            == right.selection_handoff_confirmed
        && left.same_selection_notification_confirmed
            == right.same_selection_notification_confirmed
        && left.transfer_state == right.transfer_state
        && left.sidecar == right.sidecar
        && left.sidecar_header == right.sidecar_header
        && left.sidecar_allocation_size == right.sidecar_allocation_size
        && left.sidecar_mabf_size == right.sidecar_mabf_size
        && left.route_generation == right.route_generation
        && left.next_lease_generation == right.next_lease_generation
        && left.lifecycle_state_epoch == right.lifecycle_state_epoch
        && left.controller == right.controller
        && left.canonical_substrate_proof_exact
            == right.canonical_substrate_proof_exact
        && left.canonical_substrate_request
            == right.canonical_substrate_request
        && left.canonical_substrate_sound_identity.internal_index
            == right.canonical_substrate_sound_identity.internal_index
        && left.canonical_substrate_sound_identity.serial_number
            == right.canonical_substrate_sound_identity.serial_number
        && left.canonical_substrate_source_ordinal
            == right.canonical_substrate_source_ordinal
        && left.canonical_substrate_list_exit_epoch
            == right.canonical_substrate_list_exit_epoch
        && left.canonical_substrate_transaction_generation
            == right.canonical_substrate_transaction_generation
        && left.substrate_bridge.generation
            == right.substrate_bridge.generation
        && left.substrate_bridge.phase == right.substrate_bridge.phase
        && controller_identity_proof_matches(
            left.controller_proof, right.controller_proof);
}

bool consume_selection_activation_reservation(
    SelectionActivationClaim::Impl& claim,
    SelectionActivationReservationRecord& out) noexcept
{
    out = {};
    try {
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        if (!claim.armed
            || g_selection_activation_reservation_machine.state()
                != SelectionActivationReservationState::Reserved
            || !g_selection_activation_reservation.selection_handoff_confirmed
            || g_selection_activation_reservation.transfer_state
                != SelectionActivationTransferState::ClaimedFrozen
            || g_selection_activation_reservation.generation
                != claim.reservation_generation
            || g_selection_activation_reservation.menu_session_generation
                != claim.menu_session_generation
            || g_selection_activation_reservation.menu_controller
                != claim.menu_controller
            || g_selection_activation_reservation.controller
                != claim.bgm_controller
            || !controller_identity_proof_matches(
                g_selection_activation_reservation.controller_proof,
                claim.bgm_controller_proof)
            || !selection_snapshot_exact(
                claim.frozen_selection,
                g_selection_activation_reservation.selection)) {
            return false;
        }
        if (!selection_activation_attempt_epoch_exact(
                g_selection_activation_reservation
                    .substrate_phase_attempt_revocation_epoch,
                g_selection_activation_revocation_epoch.load(
                    std::memory_order_acquire))) {
            if (g_selection_activation_reservation
                    .substrate_phase_attempt_active) {
                (void)rollback_selection_activation_substrate_phases(
                    g_selection_activation_reservation,
                    true,
                    SelectionActivationSubstrateAttemptDisposition::RetainFailClosed,
                    false);
            } else {
                g_selection_activation_reservation_machine.revoke();
                g_selection_activation_reservation = {};
            }
            return false;
        }
        const auto ownership = selection_activation_ownership_transition(
            g_selection_activation_reservation.transfer_state,
            SelectionActivationOwnershipEvent::Consume, true);
        if (!ownership.accepted
            || !g_selection_activation_reservation_machine.consume(
                g_selection_activation_reservation.generation)) return false;
        g_selection_activation_reservation.transfer_state = ownership.next;
        out = g_selection_activation_reservation;
        g_selection_activation_reservation.substrate_phase_attempt_active = false;
        claim.armed = false;
        return true;
    } catch (...) {
        return false;
    }
}

// Moved up unchanged from the later diagnostic block: the reservation refusal
// ladder below needs it, and that block is a nested unnamed namespace, so a
// forward declaration here would declare a second, distinct function.
const char* selection_admission_substrate_live_failure_name(
    const SelectionAdmissionSubstrateLiveFailure failure) noexcept
{
    switch (failure) {
    case SelectionAdmissionSubstrateLiveFailure::None: return "none";
    case SelectionAdmissionSubstrateLiveFailure::ProofInactive: return "proof_inactive";
    case SelectionAdmissionSubstrateLiveFailure::ProofStateDrift: return "proof_state";
    case SelectionAdmissionSubstrateLiveFailure::TransactionInvalid: return "transaction";
    case SelectionAdmissionSubstrateLiveFailure::ControllerNull: return "controller_null";
    case SelectionAdmissionSubstrateLiveFailure::ControllerPointerDrift: return "controller_pointer";
    case SelectionAdmissionSubstrateLiveFailure::ControllerIdentityDrift: return "controller_identity";
    case SelectionAdmissionSubstrateLiveFailure::ChainUnreadable: return "chain_read";
    case SelectionAdmissionSubstrateLiveFailure::SoundUnreadable: return "sound_read";
    case SelectionAdmissionSubstrateLiveFailure::RequestUnreadable: return "request_read";
    case SelectionAdmissionSubstrateLiveFailure::StateUnreadable: return "state_read";
    case SelectionAdmissionSubstrateLiveFailure::SoundNull: return "sound_null";
    case SelectionAdmissionSubstrateLiveFailure::SoundIdentityUnreadable: return "sound_identity_read";
    case SelectionAdmissionSubstrateLiveFailure::SlotBgmDrift: return "slot_bgm";
    case SelectionAdmissionSubstrateLiveFailure::SoundPointerDrift: return "sound_pointer";
    case SelectionAdmissionSubstrateLiveFailure::SoundIdentityDrift: return "sound_identity";
    case SelectionAdmissionSubstrateLiveFailure::RequestDrift: return "request";
    case SelectionAdmissionSubstrateLiveFailure::StateNotFour: return "state_four";
    }
    return "unknown";
}

// Names the first false term of bgm_playback_aggregate_menu_ready, walking
// !exit_requested -> !exit_pending -> the ownership disjunction in the exact
// order bgm_playback_aggregate_exit_ownership_present short-circuits them.
// Called only after that predicate has already returned false, so it re-derives
// a name and never contributes to the decision.
const char* first_aggregate_menu_ready_failure_name(
    const BgmPlaybackAggregateExitOwnershipFacts& f,
    const bool exit_requested, const bool exit_pending) noexcept
{
    if (exit_requested) return "exit_requested";
    if (exit_pending) return "exit_pending";
    if (f.active_or_retained_borrower) return "active_or_retained_borrower";
    if (f.unresolved_publication) return "unresolved_publication";
    if (f.release_claim_owned) return "release_claim_owned";
    if (f.rollback_owned) return "rollback_owned";
    if (f.route_or_cleanup_owned) return "route_or_cleanup_owned";
    if (f.observer_lease_in_flight) return "observer_lease_in_flight";
    return "none";
}

// Mirrors canonical_substrate_route_predecessor_exact term for term, including
// its normal-successor short circuit: on that branch bridge_stage_exact is the
// whole predicate, so a failure there is reported under its own name rather
// than being confused with the long-form ladder.
const char* first_canonical_substrate_route_use_failure_name(
    const CanonicalSubstrateResetLineageUseFacts& f) noexcept
{
    if (f.normal_successor) {
        return f.bridge_stage_exact ? "none" : "successor_bridge_stage";
    }
    if (!f.bridge_stage_exact) return "bridge_stage";
    if (!f.current_zero_idle) return "current_zero_idle";
    if (!f.authority_phase_exact) return "authority_phase";
    if (!f.authority_generation_valid) return "authority_generation";
    if (!f.reset_route_valid) return "reset_route";
    if (!f.reset_observation_exact) return "reset_observation";
    if (!f.request_rebase_lineage_exact) return "request_rebase_lineage";
    if (!f.reset_lifecycle_exact) return "reset_lifecycle";
    if (!f.transaction_exact) return "transaction";
    if (!f.source_exact) return "source";
    if (!f.list_exit_exact) return "list_exit";
    if (!f.proof_route_exact) return "proof_route";
    if (!f.proof_lease_exact) return "proof_lease";
    if (!f.proof_lifecycle_exact) return "proof_lifecycle";
    if (!f.request_exact) return "request";
    if (!f.canonical_token_exact) return "canonical_token";
    if (!f.bridge_generation_exact) return "bridge_generation";
    if (!f.bridge_release_exact) return "bridge_release";
    if (!f.controller_exact) return "controller";
    if (!f.controller_proof_exact) return "controller_proof";
    if (!f.slot_bgm_exact) return "slot_bgm";
    if (!f.sound_exact) return "sound";
    if (!f.sound_identity_exact) return "sound_identity";
    return "none";
}

// Mirrors the rejection disjunction of
// commit_canonical_substrate_reservation_publication in its exact order, so a
// state-side rejection is never reported under a facts-side name.
const char* first_canonical_substrate_reservation_publication_failure_name(
    const CanonicalSubstrateReservationPublicationFacts& f,
    const CanonicalSubstrateReservationPublicationState& s,
    const CanonicalSubstrateBridgeAuthority& reserved_bridge,
    const bool reserve_reset_lineage) noexcept
{
    if (!f.reservation_machine_reserved) return "machine_not_reserved";
    if (!f.reservation_generation_exact) return "reservation_generation";
    if (!f.proof_exact) return "proof_drift";
    if (!f.bridge_successor_exact) return "bridge_successor";
    if (!f.reset_phase_exact) return "reset_phase";
    if (f.expected_attempt_epoch == 0) return "attempt_epoch_zero";
    if (f.current_attempt_epoch != f.expected_attempt_epoch) {
        return "attempt_epoch_drift";
    }
    if (reserved_bridge.revocation_epoch != f.expected_attempt_epoch) {
        return "reserved_bridge_epoch";
    }
    if (s.machine_state != SelectionActivationReservationState::Reserved) {
        return "state_machine_state";
    }
    if (s.machine_generation == 0) return "state_machine_generation";
    if (!canonical_substrate_bridge_available_authority_valid(s.bridge)) {
        return "state_bridge_authority";
    }
    if (!canonical_substrate_bridge_reservation_successor_exact(
            s.bridge, reserved_bridge)) {
        return "state_bridge_successor";
    }
    if (reserve_reset_lineage
        ? s.reset_phase != CanonicalSubstrateResetLineagePhase::Qualified
        : s.reset_phase != CanonicalSubstrateResetLineagePhase::None) {
        return "state_reset_phase";
    }
    return "none";
}

} // namespace

bool reserve_selection_audio_activation(
    const SelectionSnapshot& expected_selection,
    const SelectionActivationListContext& list_context) noexcept
{
    // Refusal ladder.  Every exit other than the two `return true` paths is a
    // refusal, so the reason defaults to "exception" and is cleared only on
    // success: a `return false` added later cannot become silent by omission.
    // Each rung below names the single condition that is actually false, in the
    // order the original conjunctions and disjunctions short-circuited them.
    // Fields are populated only once their stage is reached, and `first_failure`
    // says which stage that was; everything a later rung reports was necessarily
    // true at the earlier ones.
    const char* refusal_first_failure = "exception";
    const char* refusal_detail = "none";
    uint64_t refusal_value = 0;
    bool diagnostic_exit_requested = false;
    bool diagnostic_exit_pending = false;
    bool diagnostic_substrate_active = false;
    uint64_t diagnostic_substrate_request = 0;
    uint64_t diagnostic_substrate_route_generation = 0;
    uint64_t diagnostic_substrate_rebase_route_generation = 0;
    unsigned diagnostic_substrate_bridge_phase = 0;
    unsigned diagnostic_substrate_reset_phase = 0;
    uint64_t diagnostic_live_request = 0;
    uint64_t diagnostic_route_generation = 0;
    unsigned diagnostic_reservation_state = 0;
    uint64_t diagnostic_reservation_generation = 0;
    auto refusal_diagnostic = make_deferred_noexcept_action([&]() noexcept {
        if (!refusal_first_failure) return;
        try {
            std::ostringstream out;
            out << "[audio_sead] activation_reservation_rejected first_failure="
                << refusal_first_failure
                << " detail=" << refusal_detail
                << " value=" << refusal_value
                << " exit_requested=" << (diagnostic_exit_requested ? 1 : 0)
                << " exit_pending=" << (diagnostic_exit_pending ? 1 : 0)
                << " substrate_active=" << (diagnostic_substrate_active ? 1 : 0)
                << " substrate_request=0x" << std::hex
                << diagnostic_substrate_request << std::dec
                << " substrate_route_gen=" << diagnostic_substrate_route_generation
                << " substrate_rebase_route_gen="
                << diagnostic_substrate_rebase_route_generation
                << " substrate_bridge_phase=" << diagnostic_substrate_bridge_phase
                << " substrate_reset_phase=" << diagnostic_substrate_reset_phase
                << " live_request=0x" << std::hex << diagnostic_live_request
                << std::dec
                << " route_gen=" << diagnostic_route_generation
                << " reservation_state=" << diagnostic_reservation_state
                << " reservation_gen=" << diagnostic_reservation_generation;
            core::log(core::LogLevel::Info, out.str());
        } catch (...) {}
    });
    refusal_diagnostic.make_eligible();
    try {
        AudioCallbackScope callback_scope(AudioRouteCallbackKind::SelectionReservation);
        if (!callback_scope) {
            refusal_first_failure = "callback_scope_unavailable";
            return false;
        }
        if (!expected_selection) {
            refusal_first_failure = "expected_selection_invalid";
            return false;
        }
        if (!expected_selection.song) {
            refusal_first_failure = "expected_selection_song_null";
            return false;
        }
        if (!list_context) {
            refusal_first_failure = "list_context_invalid";
            return false;
        }
        if (expected_selection.song->id.empty()) {
            refusal_first_failure = "expected_selection_song_id_empty";
            return false;
        }
        const MenuSessionSnapshot menu_session = capture_ready_menu_session();
        if (!menu_session) {
            refusal_first_failure = "menu_session_not_ready";
            return false;
        }
        if (menu_session.widget != list_context.object) {
            refusal_first_failure = "menu_widget_pointer_drift";
            return false;
        }
        if (menu_session.widget_identity.internal_index
            != list_context.internal_index) {
            refusal_first_failure = "menu_widget_index_drift";
            refusal_value = menu_session.widget_identity.internal_index;
            return false;
        }
        if (menu_session.widget_identity.serial_number
            != list_context.serial_number) {
            refusal_first_failure = "menu_widget_serial_drift";
            refusal_value = menu_session.widget_identity.serial_number;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_onmemory_bank_cleanup_only.blocks_custom_routes()) {
                refusal_first_failure = "cleanup_only_blocks_custom_routes";
                return false;
            }
            if (g_custom_activation_quarantine) {
                refusal_first_failure = "custom_activation_quarantine";
                return false;
            }
        }

        bool substrate_diagnostic_proposed = false;
        uint64_t substrate_diagnostic_source_ordinal = 0;
        uint64_t substrate_diagnostic_list_exit_epoch = 0;
        uint64_t substrate_diagnostic_request = 0;
        UObjectLiveHandle substrate_diagnostic_sound_identity{};
        auto deferred_substrate_diagnostic = make_deferred_noexcept_action(
            [&]() noexcept {
                if (!substrate_diagnostic_proposed) return;
                bgm_playback_observe_best_effort(true, [&]() {
                    std::ostringstream marker;
                    marker
                        << "[audio_sead] next_activation_substrate_proof status=exact"
                        << " source_ordinal="
                        << substrate_diagnostic_source_ordinal
                        << " list_exit_epoch="
                        << substrate_diagnostic_list_exit_epoch
                        << " request=0x" << std::hex
                        << substrate_diagnostic_request << std::dec
                        << " sound_index="
                        << substrate_diagnostic_sound_identity.internal_index
                        << " sound_serial="
                        << substrate_diagnostic_sound_identity.serial_number
                        << " state=4";
                    core::log(core::LogLevel::Info, marker.str());
                });
            });

        std::unique_lock<std::recursive_mutex> operation_lock(
            g_audio_route_operations.mutex());
        const auto initial_ownership = snapshot_bgm_aggregate_exit_ownership(
            false, false);
        // The two loads were already unconditional call arguments; naming them
        // only fixes their previously unspecified evaluation order.
        diagnostic_exit_requested =
            g_bgm_aggregate_exit_requested.load(std::memory_order_acquire);
        diagnostic_exit_pending =
            g_bgm_aggregate_exit_pending.load(std::memory_order_acquire);
        if (!bgm_playback_aggregate_menu_ready(initial_ownership,
                diagnostic_exit_requested, diagnostic_exit_pending)) {
            refusal_first_failure = "aggregate_menu_not_ready_initial";
            refusal_detail = first_aggregate_menu_ready_failure_name(
                initial_ownership, diagnostic_exit_requested,
                diagnostic_exit_pending);
            return false;
        }
        AggregateMutationLease aggregate_lease;
        if (!aggregate_lease.active) {
            refusal_first_failure = "aggregate_mutation_lease_unavailable";
            return false;
        }
        const auto protected_ownership = snapshot_bgm_aggregate_exit_ownership(
            true, false);
        diagnostic_exit_requested =
            g_bgm_aggregate_exit_requested.load(std::memory_order_acquire);
        diagnostic_exit_pending =
            g_bgm_aggregate_exit_pending.load(std::memory_order_acquire);
        if (!bgm_playback_aggregate_menu_ready(protected_ownership,
                diagnostic_exit_requested, diagnostic_exit_pending)) {
            refusal_first_failure = "aggregate_menu_not_ready_protected";
            refusal_detail = first_aggregate_menu_ready_failure_name(
                protected_ownership, diagnostic_exit_requested,
                diagnostic_exit_pending);
            return false;
        }

        SelectionActivationReservationRecord candidate;
        candidate.selection = registry().selection_snapshot();
        candidate.list_context = list_context;
        candidate.menu_session_generation = menu_session.generation;
        candidate.menu_controller = menu_session.controller;
        if (!selection_snapshot_exact(candidate.selection, expected_selection)) {
            refusal_first_failure = "registry_selection_drift";
            refusal_value = candidate.selection.generation;
            return false;
        }
        candidate.controller = lookup_current_bgm_controller();
        UObjectIdentity controller_identity{};
        if (!candidate.controller) {
            refusal_first_failure = "bgm_controller_absent";
            return false;
        }
        if (!read_uobject_identity(candidate.controller, controller_identity)) {
            refusal_first_failure = "bgm_controller_identity_unreadable";
            return false;
        }
        candidate.controller_proof = controller_identity_proof(
            candidate.controller, controller_identity);
        if (!controller_identity_proof_valid(candidate.controller_proof)) {
            refusal_first_failure = "bgm_controller_proof_invalid";
            return false;
        }

        BgmCanonicalSubstrateProof substrate_proof;
        bool reserve_reset_lineage = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            substrate_proof = g_bgm_canonical_substrate_proof;
        }
        // Plain reads of the copy just taken; no predicate is evaluated here.
        diagnostic_substrate_active = substrate_proof.active;
        diagnostic_substrate_request = substrate_proof.request;
        diagnostic_substrate_route_generation = substrate_proof.route_generation;
        diagnostic_substrate_rebase_route_generation =
            substrate_proof.request_rebase_route_generation;
        diagnostic_substrate_bridge_phase =
            static_cast<unsigned>(substrate_proof.bridge.phase);
        diagnostic_substrate_reset_phase =
            static_cast<unsigned>(substrate_proof.reset_lineage.phase);
        if (substrate_proof.active) {
            SelectionAdmissionSubstrateLiveFacts live_facts;
            if (!canonical_substrate_active_sound_live_exact(
                    substrate_proof, candidate.controller,
                    candidate.controller_proof, &live_facts)) {
                refusal_first_failure = "substrate_live_drift";
                refusal_detail = selection_admission_substrate_live_failure_name(
                    first_selection_admission_substrate_live_failure(live_facts));
                refusal_value = live_facts.observed_state;
                diagnostic_live_request = live_facts.observed_request;
                return false;
            }
            candidate.canonical_substrate_proof_exact = true;
            candidate.canonical_substrate_request = substrate_proof.request;
            candidate.canonical_substrate_sound_identity =
                substrate_proof.sound_identity;
            candidate.canonical_substrate_source_ordinal =
                substrate_proof.source_ordinal;
            candidate.canonical_substrate_list_exit_epoch =
                substrate_proof.list_exit_epoch;
            candidate.canonical_substrate_transaction_generation =
                substrate_proof.transaction_generation;
            if (substrate_proof.bridge.phase
                    == CanonicalSubstrateBridgePhase::Available
                && substrate_proof.custom == 0) {
                candidate.substrate_bridge = substrate_proof.bridge;
                candidate.substrate_bridge.phase =
                    CanonicalSubstrateBridgePhase::Reserved;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            // Same disjuncts, same order, same short-circuiting: each rung is
            // reached only when every rung above it was false, exactly as the
            // original `||` chain evaluated them.  preflight_arm stays last and
            // is still called at most once.
            if (!g_audio_route_installed.load(std::memory_order_acquire)) {
                refusal_first_failure = "route_not_installed";
                return false;
            }
            if (g_audio_route_disabled.load(std::memory_order_acquire)) {
                refusal_first_failure = "route_disabled";
                return false;
            }
            if (g_frozen_profile_lease.active()) {
                refusal_first_failure = "frozen_profile_lease_active";
                return false;
            }
            if (native_audio_route_owned_locked()) {
                refusal_first_failure = "native_route_owned";
                return false;
            }
            if (g_audio_route_state.list_cleanup_pending) {
                refusal_first_failure = "route_list_cleanup_pending";
                return false;
            }
            if (g_audio_route_state.phase != AudioRoutePhase::Idle) {
                refusal_first_failure = "route_phase_not_idle";
                refusal_value = static_cast<uint64_t>(
                    static_cast<unsigned>(g_audio_route_state.phase));
                diagnostic_route_generation = g_audio_route_state.generation;
                return false;
            }
            if (g_audio_route_state.generation == UINT64_MAX) {
                refusal_first_failure = "route_generation_wrapped";
                return false;
            }
            if (g_next_audio_route_generation == UINT64_MAX) {
                refusal_first_failure = "next_route_generation_wrapped";
                return false;
            }
            if (g_onmemory_bank_cleanup_only.blocks_custom_routes()) {
                refusal_first_failure = "cleanup_only_blocks_custom_routes_armed";
                return false;
            }
            if (g_custom_activation_quarantine) {
                refusal_first_failure = "custom_activation_quarantine_armed";
                return false;
            }
            const auto preflight = g_onmemory_bank_lifecycle.preflight_arm(
                true,
                g_audio_route_installed.load(std::memory_order_acquire),
                g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
                g_onmemory_bank_release_available.load(std::memory_order_acquire));
            if (preflight != OnMemoryBankRouteDecision::Allowed) {
                refusal_first_failure = "onmemory_preflight_denied";
                refusal_value = static_cast<uint64_t>(
                    static_cast<unsigned>(preflight));
                return false;
            }
            if (substrate_proof.active) {
                // canonical_substrate_route_use_facts was already evaluated only
                // on this branch; naming the result lets the refusal report the
                // single term that is false instead of the whole predicate.
                const auto route_use_facts = canonical_substrate_route_use_facts(
                    substrate_proof,
                    g_audio_route_state.generation,
                    g_audio_route_state.lease_identity,
                    g_onmemory_bank_lifecycle.state_epoch(),
                    g_canonical_substrate_reset_observation_generation,
                    g_canonical_substrate_reset_lineage_generation,
                    CanonicalSubstrateResetLineagePhase::Qualified,
                    CanonicalSubstrateBridgePhase::Available);
                if (!canonical_substrate_route_predecessor_exact(
                        route_use_facts)) {
                    refusal_first_failure = "substrate_route_predecessor_drift";
                    refusal_detail =
                        first_canonical_substrate_route_use_failure_name(
                            route_use_facts);
                    diagnostic_route_generation = g_audio_route_state.generation;
                    return false;
                }
            }
            if (substrate_proof.active) {
                reserve_reset_lineage = !bgm_playback_exact_successor(
                    substrate_proof.route_generation,
                    g_audio_route_state.generation);
            }
            candidate.sidecar = find_ready_sidecar_locked(*candidate.selection.song);
            if (!candidate.sidecar) {
                refusal_first_failure = "sidecar_not_ready";
                return false;
            }
            candidate.sidecar_header = candidate.sidecar->allocation.sead_header();
            candidate.sidecar_allocation_size
                = candidate.sidecar->allocation.allocation_size();
            candidate.sidecar_mabf_size = candidate.sidecar->allocation.mabf_size();
            if (!candidate.sidecar_header) {
                refusal_first_failure = "sidecar_header_null";
                return false;
            }
            if (candidate.sidecar_allocation_size == 0) {
                refusal_first_failure = "sidecar_allocation_empty";
                return false;
            }
            if (candidate.sidecar_mabf_size == 0) {
                refusal_first_failure = "sidecar_mabf_empty";
                return false;
            }
            candidate.route_generation = g_audio_route_state.generation;
            candidate.next_lease_generation = g_next_audio_route_generation;
            candidate.lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
            diagnostic_route_generation = candidate.route_generation;
        }

        // Transaction lock order is reservation -> borrower. Rollback uses the
        // same order; no path nests borrower -> reservation.
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        diagnostic_reservation_state = static_cast<unsigned>(
            g_selection_activation_reservation_machine.state());
        diagnostic_reservation_generation =
            g_selection_activation_reservation_machine.generation();
        if (!menu_session_generation_exact(candidate.menu_session_generation, true)) {
            refusal_first_failure = "menu_session_generation_drift";
            refusal_value = candidate.menu_session_generation;
            return false;
        }
        if (g_selection_activation_reservation_machine.state()
                == SelectionActivationReservationState::Reserved
            && g_selection_activation_reservation.selection_handoff_confirmed) {
            refusal_first_failure = "reservation_handoff_already_confirmed";
            return false;
        }
        if (g_selection_activation_reservation_machine.state()
                == SelectionActivationReservationState::Reserved
            && activation_reservation_record_exact(
                g_selection_activation_reservation, candidate)) {
            if (candidate.substrate_bridge.phase
                == CanonicalSubstrateBridgePhase::Reserved) {
                if (!g_selection_activation_reservation
                        .substrate_phase_attempt_active) {
                    refusal_first_failure = "idempotent_substrate_attempt_inactive";
                    return false;
                }
                std::lock_guard<std::mutex> borrower_lock(
                    g_bgm_playback_borrower_mutex);
                if (!canonical_substrate_proof_matches(
                        g_bgm_canonical_substrate_proof,
                        g_selection_activation_reservation
                            .substrate_reserved_proof)) {
                    refusal_first_failure = "idempotent_substrate_proof_drift";
                    return false;
                }
            }
            refusal_first_failure = nullptr;
            return true;
        }
        if (g_selection_activation_reservation_machine.generation()
            == UINT64_MAX) {
            refusal_first_failure = "reservation_generation_wrapped";
            return false;
        }
        if (g_selection_activation_reservation_machine.state()
            == SelectionActivationReservationState::Consumed) {
            refusal_first_failure = "reservation_consumed";
            return false;
        }
        if (g_selection_activation_reservation_machine.state()
            == SelectionActivationReservationState::Reserved) {
            (void)rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true);
        }
        g_selection_activation_reservation_machine.revoke();
        g_selection_activation_reservation = {};
        const uint64_t publication_revocation_epoch =
            g_selection_activation_revocation_epoch.load(
                std::memory_order_acquire);
        if (publication_revocation_epoch == 0) {
            refusal_first_failure = "revocation_epoch_zero";
            return false;
        }
        candidate.substrate_phase_attempt_revocation_epoch =
            publication_revocation_epoch;
        if (candidate.substrate_bridge.phase
            == CanonicalSubstrateBridgePhase::Reserved) {
            candidate.substrate_bridge.revocation_epoch =
                publication_revocation_epoch;
        }
        if (!g_selection_activation_reservation_machine.reserve()) {
            refusal_first_failure = "reservation_machine_reserve_failed";
            return false;
        }
        bool reservation_transaction_committed = false;
        auto rollback_reservation_transaction = make_deferred_noexcept_action(
            [&]() noexcept {
                if (reservation_transaction_committed) return;
                (void)rollback_selection_activation_substrate_phases(
                    candidate, true);
                g_selection_activation_reservation_machine.revoke();
            });
        rollback_reservation_transaction.make_eligible();
        candidate.generation = g_selection_activation_reservation_machine.generation();
        diagnostic_reservation_generation = candidate.generation;
        bool reservation_record_published = false;
        if (candidate.substrate_bridge.phase
                == CanonicalSubstrateBridgePhase::Reserved) {
            std::lock_guard<std::mutex> borrower_lock(
                g_bgm_playback_borrower_mutex);
            const bool proof_exact = canonical_substrate_proof_matches(
                g_bgm_canonical_substrate_proof, substrate_proof);
            CanonicalSubstrateReservationPublicationState publication_state{
                g_selection_activation_reservation_machine.state(),
                g_selection_activation_reservation_machine.generation(),
                g_bgm_canonical_substrate_proof.bridge,
                g_bgm_canonical_substrate_proof.reset_lineage.phase,
                0,
                false};
            const CanonicalSubstrateReservationPublicationFacts publication_facts{
                g_selection_activation_reservation_machine.state()
                    == SelectionActivationReservationState::Reserved,
                candidate.generation != 0,
                proof_exact,
                canonical_substrate_bridge_reservation_successor_exact(
                    g_bgm_canonical_substrate_proof.bridge,
                    candidate.substrate_bridge),
                reserve_reset_lineage
                    ? g_bgm_canonical_substrate_proof.reset_lineage.phase
                        == CanonicalSubstrateResetLineagePhase::Qualified
                    : g_bgm_canonical_substrate_proof.reset_lineage.phase
                        == CanonicalSubstrateResetLineagePhase::None,
                publication_revocation_epoch,
                g_selection_activation_revocation_epoch.load(
                    std::memory_order_acquire)};
            if (!commit_canonical_substrate_reservation_publication(
                    publication_facts, candidate.substrate_bridge,
                    reserve_reset_lineage, publication_state)) {
                // The commit rejects before it mutates state, so the state read
                // back here is the one it judged.
                refusal_first_failure = "substrate_reservation_publication_rejected";
                refusal_detail =
                    first_canonical_substrate_reservation_publication_failure_name(
                        publication_facts, publication_state,
                        candidate.substrate_bridge, reserve_reset_lineage);
                refusal_value = publication_facts.current_attempt_epoch;
                return false;
            }
            candidate.substrate_available_proof =
                g_bgm_canonical_substrate_proof;
            g_bgm_canonical_substrate_proof.bridge = publication_state.bridge;
            g_bgm_canonical_substrate_proof.reset_lineage.phase =
                publication_state.reset_phase;
            candidate.substrate_phase_attempt_active = true;
            candidate.substrate_reserved_proof =
                g_bgm_canonical_substrate_proof;
            g_selection_activation_reservation = candidate;
            reservation_record_published = true;
        }
        if (!reservation_record_published) {
            g_selection_activation_reservation = candidate;
        }
        reservation_transaction_committed = true;
        if (candidate.canonical_substrate_proof_exact) {
            substrate_diagnostic_source_ordinal
                = candidate.canonical_substrate_source_ordinal;
            substrate_diagnostic_list_exit_epoch
                = candidate.canonical_substrate_list_exit_epoch;
            substrate_diagnostic_request
                = candidate.canonical_substrate_request;
            substrate_diagnostic_sound_identity
                = candidate.canonical_substrate_sound_identity;
            substrate_diagnostic_proposed = true;
            deferred_substrate_diagnostic.make_eligible();
        }
        refusal_first_failure = nullptr;
        return true;
    } catch (...) {
        return false;
    }
}

SelectionActivationHandoff prepare_selection_audio_activation_handoff(
    const SelectionSnapshot& selection,
    const SelectionActivationListContext& list_context) noexcept
{
    SelectionActivationHandoff out;
    try {
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        if (g_selection_activation_reservation_machine.state()
            != SelectionActivationReservationState::Reserved) {
            return out;
        }
        if (g_selection_activation_reservation.selection_handoff_confirmed) {
            out.result = SelectionActivationHandoffResult::AlreadyConfirmed;
            return out;
        }
        if (!activation_list_context_exact(
                list_context, g_selection_activation_reservation.list_context)) {
            out.result = SelectionActivationHandoffResult::ContextDrift;
            return out;
        }
        if (!selection_snapshot_exact(
                selection, g_selection_activation_reservation.selection)) {
            out.result = SelectionActivationHandoffResult::PriorSelectionDrift;
            return out;
        }
        if (selection.generation == UINT64_MAX) {
            out.result = SelectionActivationHandoffResult::GenerationWrap;
            return out;
        }
        out.reservation_generation
            = g_selection_activation_reservation.generation;
        out.prior_selection_generation = selection.generation;
        out.result = SelectionActivationHandoffResult::Prepared;
        return out;
    } catch (...) {
        return out;
    }
}

namespace {

SelectionActivationHandoffResult public_handoff_result(
    const SelectionActivationHandoffFailure failure) noexcept
{
    switch (failure) {
    case SelectionActivationHandoffFailure::None:
        return SelectionActivationHandoffResult::Confirmed;
    case SelectionActivationHandoffFailure::NoPendingReservation:
        return SelectionActivationHandoffResult::NoPendingReservation;
    case SelectionActivationHandoffFailure::AlreadyConfirmed:
        return SelectionActivationHandoffResult::AlreadyConfirmed;
    case SelectionActivationHandoffFailure::ReservationGenerationDrift:
        return SelectionActivationHandoffResult::ReservationGenerationDrift;
    case SelectionActivationHandoffFailure::ContextDrift:
        return SelectionActivationHandoffResult::ContextDrift;
    case SelectionActivationHandoffFailure::PriorSelectionDrift:
        return SelectionActivationHandoffResult::PriorSelectionDrift;
    case SelectionActivationHandoffFailure::GenerationWrap:
        return SelectionActivationHandoffResult::GenerationWrap;
    case SelectionActivationHandoffFailure::SameGeneration:
        return SelectionActivationHandoffResult::SameGeneration;
    case SelectionActivationHandoffFailure::SkippedGeneration:
        return SelectionActivationHandoffResult::SkippedGeneration;
    case SelectionActivationHandoffFailure::StorageDrift:
        return SelectionActivationHandoffResult::StorageDrift;
    case SelectionActivationHandoffFailure::SongDrift:
        return SelectionActivationHandoffResult::SongDrift;
    case SelectionActivationHandoffFailure::ProfileDrift:
        return SelectionActivationHandoffResult::ProfileDrift;
    case SelectionActivationHandoffFailure::VisibleIndexDrift:
        return SelectionActivationHandoffResult::VisibleIndexDrift;
    case SelectionActivationHandoffFailure::BaseSlotDrift:
        return SelectionActivationHandoffResult::BaseSlotDrift;
    }
    return SelectionActivationHandoffResult::NoPendingReservation;
}

bool advance_selection_activation_revocation_epoch_noexcept(
    uint64_t* const advanced_epoch) noexcept
{
    uint64_t epoch = g_selection_activation_revocation_epoch.load(
        std::memory_order_relaxed);
    while (epoch != UINT64_MAX
        && !g_selection_activation_revocation_epoch.compare_exchange_weak(
            epoch, epoch + 1, std::memory_order_release,
            std::memory_order_relaxed)) {
    }
    if (epoch == UINT64_MAX) return false;
    if (advanced_epoch) *advanced_epoch = epoch + 1;
    return true;
}

} // namespace

SelectionActivationHandoffResult confirm_selection_audio_activation_handoff(
    const SelectionActivationHandoff& handoff,
    const SelectionSnapshot& published_selection,
    const SelectionActivationListContext& list_context) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        const SelectionSnapshot& prior
            = g_selection_activation_reservation.selection;
        const SelectionActivationHandoffFacts facts{
            g_selection_activation_reservation_machine.state()
                == SelectionActivationReservationState::Reserved,
            !g_selection_activation_reservation.selection_handoff_confirmed,
            handoff.reservation_generation != 0
                && handoff.reservation_generation
                    == g_selection_activation_reservation.generation,
            activation_list_context_exact(
                list_context, g_selection_activation_reservation.list_context),
            handoff.prior_selection_generation == prior.generation,
            prior.generation,
            published_selection.generation,
            prior.storage == published_selection.storage,
            prior.song == published_selection.song,
            prior.profile == published_selection.profile
                && prior.profile_index == published_selection.profile_index,
            prior.visible_index == published_selection.visible_index,
            prior.base_slot == published_selection.base_slot,
        };
        const auto failure = selection_activation_handoff_first_failure(facts);
        if (failure != SelectionActivationHandoffFailure::None) {
            (void)rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true);
            g_selection_activation_reservation_machine.revoke();
            g_selection_activation_reservation = {};
            advance_selection_activation_revocation_epoch_noexcept();
            return public_handoff_result(failure);
        }
        g_selection_activation_reservation.selection = published_selection;
        g_selection_activation_reservation.selection_handoff_confirmed = true;
        return SelectionActivationHandoffResult::Confirmed;
    } catch (...) {
        revoke_selection_audio_activation();
        return SelectionActivationHandoffResult::NoPendingReservation;
    }
}

SelectionActivationHandoffResult publish_selection_audio_activation_handoff(
    const SelectionActivationHandoff& handoff,
    const SelectionActivationListContext& list_context,
    const int32_t visible_index, const int32_t base_slot,
    const bool alias_state_exact,
    SelectionSnapshot& published_selection) noexcept
{
    published_selection = {};
    try {
        // Lock order is reservation -> SongRegistry. No registry path enters the
        // reservation state machine while holding its state mutex.
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        const SelectionSnapshot& prior
            = g_selection_activation_reservation.selection;
        if (g_selection_activation_reservation_machine.state()
                != SelectionActivationReservationState::Reserved
            || g_selection_activation_reservation.selection_handoff_confirmed
            || handoff.reservation_generation == 0
            || handoff.reservation_generation
                != g_selection_activation_reservation.generation
            || !activation_list_context_exact(
                list_context, g_selection_activation_reservation.list_context)
            || handoff.prior_selection_generation != prior.generation
            || prior.generation == UINT64_MAX
            || !registry().publish_activation_selection_handoff(
                prior, visible_index, base_slot,
                handoff.reservation_generation, alias_state_exact,
                published_selection)) {
            (void)rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true);
            g_selection_activation_reservation_machine.revoke();
            g_selection_activation_reservation = {};
            advance_selection_activation_revocation_epoch_noexcept();
            return SelectionActivationHandoffResult::PriorSelectionDrift;
        }

        const SelectionActivationHandoffFacts facts{
            true,
            true,
            true,
            true,
            true,
            prior.generation,
            published_selection.generation,
            prior.storage == published_selection.storage,
            prior.song == published_selection.song,
            prior.profile == published_selection.profile
                && prior.profile_index == published_selection.profile_index,
            prior.visible_index == published_selection.visible_index,
            prior.base_slot == published_selection.base_slot,
        };
        const auto failure = selection_activation_handoff_first_failure(facts);
        if (failure != SelectionActivationHandoffFailure::None) {
            (void)rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true);
            g_selection_activation_reservation_machine.revoke();
            g_selection_activation_reservation = {};
            advance_selection_activation_revocation_epoch_noexcept();
            return public_handoff_result(failure);
        }
        g_selection_activation_reservation.selection = published_selection;
        g_selection_activation_reservation.selection_handoff_confirmed = true;
        return SelectionActivationHandoffResult::Confirmed;
    } catch (...) {
        revoke_selection_audio_activation();
        published_selection = {};
        return SelectionActivationHandoffResult::NoPendingReservation;
    }
}

SelectionActivationNotificationResult
observe_selection_audio_activation_index_notification(
    const SelectionSnapshot& selection,
    const SelectionActivationListContext& list_context,
    const int32_t visible_index, const int32_t base_slot) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        if (g_selection_activation_reservation_machine.state()
                != SelectionActivationReservationState::Reserved
            || !g_selection_activation_reservation.selection_handoff_confirmed) {
            if (g_selection_activation_reservation_machine.state()
                == SelectionActivationReservationState::Reserved) {
                (void)rollback_selection_activation_substrate_phases(
                    g_selection_activation_reservation, true);
                g_selection_activation_reservation_machine.revoke();
                g_selection_activation_reservation = {};
                advance_selection_activation_revocation_epoch_noexcept();
            }
            return SelectionActivationNotificationResult::NoPendingReservation;
        }
        if (g_selection_activation_reservation.same_selection_notification_confirmed) {
            (void)rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true);
            g_selection_activation_reservation_machine.revoke();
            g_selection_activation_reservation = {};
            advance_selection_activation_revocation_epoch_noexcept();
            return SelectionActivationNotificationResult::DuplicateNotification;
        }
        if (!activation_list_context_exact(
                list_context, g_selection_activation_reservation.list_context)) {
            (void)rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true);
            g_selection_activation_reservation_machine.revoke();
            g_selection_activation_reservation = {};
            advance_selection_activation_revocation_epoch_noexcept();
            return SelectionActivationNotificationResult::ContextDrift;
        }
        if (!selection_snapshot_exact(
                selection, g_selection_activation_reservation.selection)
            || !registry().preserve_confirmed_activation_index_notification(
                g_selection_activation_reservation.selection,
                visible_index, base_slot, true)) {
            (void)rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true);
            g_selection_activation_reservation_machine.revoke();
            g_selection_activation_reservation = {};
            advance_selection_activation_revocation_epoch_noexcept();
            return SelectionActivationNotificationResult::SelectionDrift;
        }
        g_selection_activation_reservation.same_selection_notification_confirmed = true;
        return SelectionActivationNotificationResult::ConfirmedSameSelection;
    } catch (...) {
        revoke_selection_audio_activation();
        return SelectionActivationNotificationResult::SelectionDrift;
    }
}

namespace {
bool abandon_selection_activation_claim(
    SelectionActivationClaim::Impl& claim) noexcept;
}

SelectionActivationClaim::SelectionActivationClaim() noexcept = default;
SelectionActivationClaim::SelectionActivationClaim(
    SelectionActivationClaim&&) noexcept = default;
SelectionActivationClaim& SelectionActivationClaim::operator=(
    SelectionActivationClaim&& other) noexcept
{
    if (this == &other) return *this;
    if (impl_ && impl_->armed) {
        (void)abandon_selection_activation_claim(*impl_);
    }
    impl_ = std::move(other.impl_);
    return *this;
}

SelectionActivationClaim::~SelectionActivationClaim() noexcept
{
    if (impl_ && impl_->armed) {
        (void)abandon_selection_activation_claim(*impl_);
    }
}

SelectionActivationClaim::operator bool() const noexcept
{
    return impl_ && impl_->armed && impl_->frozen_selection;
}

SelectionSnapshot selection_audio_activation_claim_selection(
    const SelectionActivationClaim& claim) noexcept
{
    return claim.impl_ && claim.impl_->armed
        ? claim.impl_->frozen_selection : SelectionSnapshot{};
}

SelectionActivationClaim claim_selection_audio_activation(
    void* const wrapper, const uintptr_t caller_rva) noexcept
{
    SelectionActivationClaim claim;
    SelectionActivationClaimFailure first_failure
        = SelectionActivationClaimFailure::Exception;
    SelectionActivationReservationState diagnostic_state{};
    SelectionActivationTransferState diagnostic_transfer{};
    uint64_t diagnostic_reservation_generation = 0;
    uint64_t diagnostic_menu_generation = 0;
    auto diagnostic = make_deferred_noexcept_action([&] {
        if (first_failure == SelectionActivationClaimFailure::None) return;
        try {
            std::ostringstream out;
            out << "[audio_sead] activation_claim_rejected first_failure="
                << selection_activation_claim_failure_name(first_failure)
                << " reservation_state=" << static_cast<unsigned>(diagnostic_state)
                << " transfer_state="
                << selection_activation_transfer_state_name(diagnostic_transfer)
                << " reservation_generation=" << diagnostic_reservation_generation
                << " menu_generation=" << diagnostic_menu_generation
                << " caller_rva=0x" << std::hex << caller_rva;
            core::log(core::LogLevel::Info, out.str());
        } catch (...) {}
    });
    diagnostic.make_eligible();
    try {
        void* const live_bgm_controller = lookup_current_bgm_controller();
        ControllerIdentityProof live_bgm_proof{};
        const bool live_bgm_proof_available = live_bgm_controller
            && read_controller_identity_proof(
                live_bgm_controller, live_bgm_proof);
        std::lock_guard<std::mutex> lock(
            g_selection_activation_reservation_mutex);
        auto& reservation = g_selection_activation_reservation;
        void* persistent_wrapper = nullptr;
        bool substrate_exact = true;
        if (reservation.substrate_phase_attempt_active) {
            std::lock_guard<std::mutex> borrower_lock(
                g_bgm_playback_borrower_mutex);
            substrate_exact = canonical_substrate_proof_matches(
                g_bgm_canonical_substrate_proof,
                reservation.substrate_reserved_proof);
        }
        const bool live_bgm_exact = reservation.controller
            && live_bgm_controller == reservation.controller
            && live_bgm_proof_available
            && controller_identity_proof_matches(
                reservation.controller_proof, live_bgm_proof);
        const bool wrapper_safe_read = reservation.menu_controller
            && core::safe_read_field(
                reservation.menu_controller, 0xF48, persistent_wrapper);
        SelectionActivationClaimFacts facts{};
        facts.reservation_state =
            g_selection_activation_reservation_machine.state();
        facts.transfer_state = reservation.transfer_state;
        facts.handoff_confirmed = reservation.selection_handoff_confirmed;
        facts.no_active_menu_session = active_menu_session_generation() == 0;
        facts.reservation_generation_nonzero = reservation.generation != 0
            && reservation.generation
                == g_selection_activation_reservation_machine.generation();
        facts.reservation_generation_not_wrapped = reservation.generation != UINT64_MAX;
        facts.menu_session_generation_nonzero = reservation.menu_session_generation != 0;
        facts.menu_session_generation_not_wrapped = reservation.menu_session_generation != UINT64_MAX;
        facts.selection_generation_nonzero = reservation.selection.generation != 0;
        facts.selection_generation_not_wrapped = reservation.selection.generation != UINT64_MAX;
        facts.registry_selection_exact = registry().selection_matches(reservation.selection);
        facts.bgm_controller_exact = reservation.controller
            && live_bgm_controller == reservation.controller;
        facts.bgm_identity_exact = live_bgm_exact;
        facts.caller_exact = caller_rva == rva::PersistentChartExpandCaller;
        facts.wrapper_safe_read = wrapper_safe_read;
        facts.wrapper_exact = wrapper && persistent_wrapper == wrapper;
        facts.substrate_exact_if_active = substrate_exact;
        diagnostic_state = facts.reservation_state;
        diagnostic_transfer = facts.transfer_state;
        diagnostic_reservation_generation = reservation.generation;
        diagnostic_menu_generation = reservation.menu_session_generation;
        first_failure = selection_activation_claim_first_failure(facts);
        if (first_failure != SelectionActivationClaimFailure::None) return claim;

        auto state = std::make_unique<SelectionActivationClaim::Impl>();
        const SelectionSnapshot handoff = reservation.selection;
        SelectionSnapshot frozen;
        if (!registry().freeze_active_profile_for_activation(
                handoff, reservation.generation, frozen)) {
            first_failure = SelectionActivationClaimFailure::FreezeProfile;
            return claim;
        }
        if (frozen.generation != handoff.generation + 1
            || frozen.storage != handoff.storage || frozen.song != handoff.song
            || frozen.profile != handoff.profile
            || frozen.profile_index != handoff.profile_index
            || frozen.visible_index != handoff.visible_index
            || frozen.base_slot != handoff.base_slot) {
            SelectionSnapshot thawed;
            (void)registry().thaw_active_profile_for_activation(
                frozen, reservation.generation, thawed);
            first_failure = SelectionActivationClaimFailure::FrozenIdentity;
            return claim;
        }
        const auto claimed = selection_activation_ownership_transition(
            reservation.transfer_state,
            SelectionActivationOwnershipEvent::ClaimFrozen, true);
        if (!claimed.accepted) {
            SelectionSnapshot thawed;
            (void)registry().thaw_active_profile_for_activation(
                frozen, reservation.generation, thawed);
            first_failure = SelectionActivationClaimFailure::OwnershipTransition;
            return claim;
        }
        reservation.selection = frozen;
        reservation.transfer_state = claimed.next;
        state->frozen_selection = frozen;
        state->reservation_generation = reservation.generation;
        state->menu_session_generation = reservation.menu_session_generation;
        state->handoff_selection_generation = handoff.generation;
        state->menu_controller = reservation.menu_controller;
        state->bgm_controller = reservation.controller;
        state->bgm_controller_proof = reservation.controller_proof;
        state->wrapper = wrapper;
        state->armed = true;
        claim.impl_ = std::move(state);
        first_failure = SelectionActivationClaimFailure::None;
    } catch (...) {
    }
    return claim;
}

bool revoke_selection_audio_activation_if_pending() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        if (g_selection_activation_reservation_machine.state()
            != SelectionActivationReservationState::Reserved) {
            return false;
        }
        const auto ownership = selection_activation_ownership_transition(
            g_selection_activation_reservation.transfer_state,
            SelectionActivationOwnershipEvent::Rollback, true);
        const SelectionSnapshot frozen
            = g_selection_activation_reservation.selection;
        const uint64_t reservation_generation
            = g_selection_activation_reservation.generation;
        (void)rollback_selection_activation_substrate_phases(
            g_selection_activation_reservation, true);
        g_selection_activation_reservation_machine.revoke();
        g_selection_activation_reservation = {};
        advance_selection_activation_revocation_epoch_noexcept();
        if (ownership.rollback
            == SelectionActivationRollbackPlan::ReservationAndFrozenProfile) {
            SelectionSnapshot thawed;
            (void)registry().thaw_active_profile_for_activation(
                frozen, reservation_generation, thawed);
        }
        return true;
    } catch (...) {
        return false;
    }
}

namespace {
bool revoke_selection_audio_activation_locked_exact() noexcept
{
    if (g_selection_activation_reservation_machine.state()
        != SelectionActivationReservationState::Reserved) return false;
    const auto ownership = selection_activation_ownership_transition(
        g_selection_activation_reservation.transfer_state,
        SelectionActivationOwnershipEvent::Rollback, true);
    if (!ownership.accepted) return false;
    const SelectionSnapshot frozen = g_selection_activation_reservation.selection;
    const uint64_t reservation_generation
        = g_selection_activation_reservation.generation;
    if (g_selection_activation_reservation.substrate_phase_attempt_active) {
        if (!rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true,
                SelectionActivationSubstrateAttemptDisposition::RestoreIfExact,
                true, true)) return false;
    } else {
        g_selection_activation_reservation_machine.revoke();
        g_selection_activation_reservation = {};
    }
    advance_selection_activation_revocation_epoch_noexcept();
    if (ownership.rollback !=
        SelectionActivationRollbackPlan::ReservationAndFrozenProfile) return true;
    SelectionSnapshot thawed;
    return registry().thaw_active_profile_for_activation(
        frozen, reservation_generation, thawed);
}

bool abandon_selection_activation_claim(
    SelectionActivationClaim::Impl& claim) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(
            g_selection_activation_reservation_mutex);
        const auto& reservation = g_selection_activation_reservation;
        if (!claim.armed
            || g_selection_activation_reservation_machine.state()
                != SelectionActivationReservationState::Reserved
            || reservation.transfer_state
                != SelectionActivationTransferState::ClaimedFrozen
            || reservation.generation != claim.reservation_generation
            || reservation.menu_session_generation
                != claim.menu_session_generation
            || reservation.menu_controller != claim.menu_controller
            || reservation.controller != claim.bgm_controller
            || !controller_identity_proof_matches(
                reservation.controller_proof, claim.bgm_controller_proof)
            || !selection_snapshot_exact(
                reservation.selection, claim.frozen_selection)) {
            return false;
        }
        const bool reclaimed = revoke_selection_audio_activation_locked_exact();
        if (reclaimed) claim.armed = false;
        return reclaimed;
    } catch (...) {
        return false;
    }
}

}

SelectionActivationTerminalResult terminate_selection_audio_activation_for_menu_session(
    const MenuSessionSnapshot& session, const SelectionSnapshot& expected,
    const SelectionActivationTerminalReason reason) noexcept
{
    SelectionActivationTerminalResult result;
    SelectionActivationTerminalFacts facts{};
    facts.reason = reason;
    uint64_t reservation_generation = 0;
    try {
        void* const live_bgm_controller = lookup_current_bgm_controller();
        ControllerIdentityProof live_bgm_proof{};
        const bool live_bgm_proof_available = live_bgm_controller
            && read_controller_identity_proof(
                live_bgm_controller, live_bgm_proof);
        const bool widget_uobject_exact = session
            && validate_live_uobject_handle(
                session.widget, session.widget_identity);
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        auto& reservation = g_selection_activation_reservation;
        facts.state = g_selection_activation_reservation_machine.state();
        facts.transfer_state = reservation.transfer_state;
        result.transfer_before = reservation.transfer_state;
        result.transfer_after = reservation.transfer_state;
        reservation_generation = reservation.generation;
        facts.handoff_confirmed = reservation.selection_handoff_confirmed;
        facts.active_session_exact = session && menu_session_generation_exact(
            session.generation, false);
        facts.widget_uobject_exact = widget_uobject_exact;
        facts.reservation_generation_exact = reservation.generation != 0
            && reservation.generation
                == g_selection_activation_reservation_machine.generation();
        facts.reservation_session_exact = session.generation != 0
            && reservation.menu_session_generation == session.generation
            && reservation.menu_controller == session.controller;
        const SelectionActivationListContext terminal_context{
            session.widget, session.widget_identity.internal_index,
            session.widget_identity.serial_number};
        facts.reservation_context_exact = terminal_context
            && activation_list_context_exact(
                reservation.list_context, terminal_context);
        facts.reservation_bgm_controller_exact = reservation.controller
            && live_bgm_controller == reservation.controller;
        facts.reservation_bgm_identity_exact = live_bgm_proof_available
            && controller_identity_proof_matches(
                reservation.controller_proof, live_bgm_proof);
        facts.registry_selection_exact = expected
            && registry().selection_matches(expected);
        facts.reservation_selection_exact = expected
            && selection_snapshot_exact(reservation.selection, expected);
        facts.reservation_substrate_exact = true;
        if (reservation.substrate_phase_attempt_active) {
            std::lock_guard<std::mutex> borrower_lock(g_bgm_playback_borrower_mutex);
            facts.reservation_substrate_exact = canonical_substrate_proof_matches(
                g_bgm_canonical_substrate_proof,
                reservation.substrate_reserved_proof);
        }
        result.first_failure = selection_activation_terminal_first_failure(facts);
        result.identity_exact = selection_activation_terminal_identity_exact(facts);
        result.decision = selection_activation_terminal_decision(facts);
        if (result.decision == SelectionActivationTerminalDecision::Preserved) {
            const auto preserved = selection_activation_ownership_transition(
                reservation.transfer_state,
                SelectionActivationOwnershipEvent::Preserve, true);
            if (preserved.accepted) reservation.transfer_state = preserved.next;
            else result.decision = SelectionActivationTerminalDecision::Rejected;
        } else if (facts.state == SelectionActivationReservationState::Reserved
            && facts.transfer_state == SelectionActivationTransferState::Pending) {
            if (revoke_selection_audio_activation_locked_exact())
                result.decision = SelectionActivationTerminalDecision::Revoked;
            else
                result.decision = SelectionActivationTerminalDecision::Rejected;
        }
        result.reservation_state = facts.state;
        result.reservation_generation = reservation_generation;
        result.handoff_confirmed = facts.handoff_confirmed;
        result.transfer_after = result.decision
                == SelectionActivationTerminalDecision::Revoked
            ? SelectionActivationTransferState::Retired
            : reservation.transfer_state;
    } catch (...) {
        result.decision = SelectionActivationTerminalDecision::Rejected;
    }
    try {
        std::ostringstream out;
        out << "[audio_sead] activation_terminal reason="
            << selection_activation_terminal_reason_name(reason)
            << " menu_generation=" << session.generation
            << " reservation_generation=" << reservation_generation
            << " reservation_state=" << static_cast<unsigned>(facts.state)
            << " confirmed=" << facts.handoff_confirmed
            << " identity_exact=" << result.identity_exact
            << " transfer_before="
            << selection_activation_transfer_state_name(result.transfer_before)
            << " transfer_after="
            << selection_activation_transfer_state_name(result.transfer_after)
            << " first_failure="
            << selection_activation_terminal_failure_name(result.first_failure)
            << " decision="
            << selection_activation_terminal_decision_name(result.decision);
        core::log(result.decision == SelectionActivationTerminalDecision::Rejected
                ? core::LogLevel::Error : core::LogLevel::Info,
            out.str());
    } catch (...) {}
    return result;
}

bool reclaim_selection_audio_activation_for_controller(
    void* controller, SelectionActivationTerminalReason reason) noexcept
{
    uint64_t menu_generation = 0;
    uint64_t reservation_generation = 0;
    SelectionActivationReservationState state
        = SelectionActivationReservationState::Empty;
    bool confirmed = false;
    bool identity_exact = false;
    SelectionActivationTerminalDecision decision
        = SelectionActivationTerminalDecision::Rejected;
    try {
        {
            std::lock_guard<std::mutex> lock(
                g_selection_activation_reservation_mutex);
            auto& reservation = g_selection_activation_reservation;
            state = g_selection_activation_reservation_machine.state();
            menu_generation = reservation.menu_session_generation;
            reservation_generation = reservation.generation;
            confirmed = reservation.selection_handoff_confirmed;
            identity_exact = controller
                && reservation.menu_controller == controller
                && active_menu_session_generation() == 0;
            if (state == SelectionActivationReservationState::Reserved
                && identity_exact && revoke_selection_audio_activation_locked_exact())
                decision = SelectionActivationTerminalDecision::Revoked;
        }
        std::ostringstream out;
        out << "[audio_sead] activation_terminal reason="
            << selection_activation_terminal_reason_name(reason)
            << " menu_generation=" << menu_generation
            << " reservation_generation=" << reservation_generation
            << " reservation_state=" << static_cast<unsigned>(state)
            << " confirmed=" << confirmed
            << " identity_exact=" << identity_exact
            << " decision="
            << selection_activation_terminal_decision_name(decision);
        core::log(decision == SelectionActivationTerminalDecision::Rejected
                ? core::LogLevel::Error : core::LogLevel::Info,
            out.str());
        return decision == SelectionActivationTerminalDecision::Revoked;
    } catch (...) { return false; }
}

bool revoke_selection_audio_activation_for_menu_session(
    const SelectionSnapshot& expected, uint64_t session_generation) noexcept
{
    try {
        if (!expected || !session_generation) return false;
        std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
        const auto state = g_selection_activation_reservation_machine.state();
        const MenuSessionRevocationFacts facts{
            state, true,
            menu_session_generation_exact(session_generation, false),
            registry().selection_matches(expected),
            g_selection_activation_reservation.menu_session_generation == session_generation,
            selection_snapshot_exact(g_selection_activation_reservation.selection, expected)};
        if (!menu_session_revocation_prestate_exact(facts)) return false;
        if (state == SelectionActivationReservationState::Empty
            || state == SelectionActivationReservationState::Revoked) return true;
        if (g_selection_activation_reservation.substrate_phase_attempt_active) {
            if (!rollback_selection_activation_substrate_phases(
                    g_selection_activation_reservation, true,
                    SelectionActivationSubstrateAttemptDisposition::RestoreIfExact,
                    true, true)) return false;
            advance_selection_activation_revocation_epoch_noexcept();
            return true;
        }
        g_selection_activation_reservation_machine.revoke();
        g_selection_activation_reservation = {};
        advance_selection_activation_revocation_epoch_noexcept();
        return true;
    } catch (...) { return false; }
}

void revoke_selection_audio_activation() noexcept
{
    try {
        {
            std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
            const auto ownership = selection_activation_ownership_transition(
                g_selection_activation_reservation.transfer_state,
                SelectionActivationOwnershipEvent::Rollback, true);
            const SelectionSnapshot frozen
                = g_selection_activation_reservation.selection;
            const uint64_t reservation_generation
                = g_selection_activation_reservation.generation;
            (void)rollback_selection_activation_substrate_phases(
                g_selection_activation_reservation, true);
            if (g_selection_activation_reservation_machine.state()
                == SelectionActivationReservationState::Consumed) {
                (void)g_selection_activation_reservation_machine.retire_consumed(
                    g_selection_activation_reservation.generation);
            } else {
                g_selection_activation_reservation_machine.revoke();
            }
            g_selection_activation_reservation = {};
            advance_selection_activation_revocation_epoch_noexcept();
            if (ownership.rollback
                == SelectionActivationRollbackPlan::ReservationAndFrozenProfile) {
                SelectionSnapshot thawed;
                (void)registry().thaw_active_profile_for_activation(
                    frozen, reservation_generation, thawed);
            }
        }
        (void)try_clear_custom_activation_quarantine(
            CustomActivationQuarantineClearReason::TitleOrReselection);
    } catch (...) {
    }
}

uint64_t selection_audio_activation_revocation_epoch() noexcept
{
    return g_selection_activation_revocation_epoch.load(std::memory_order_acquire);
}

enum class CleanupOnlyObservationOutcome : uint8_t {
    Observed = 0,
    ReleaseClaimed,
    ReleaseAlreadyAbsent,
    ReleaseSubmitted,
    ReleaseFailed,
    Completed,
    FailedRetained,
};

struct CleanupOnlyObservationLogProposal {
    CleanupOnlyObservationFingerprint fingerprint{};
    CleanupOnlyRouteQuiescenceFailure quiescence_failure =
        CleanupOnlyRouteQuiescenceFailure::None;
    bool request_zero = false;
    bool backing_read = false;
    bool backing_null = false;
    bool sound_inactive = false;
    bool route_setup_absent = false;
    bool route_quiescent = false;
    bool aggregate_pause_absent = false;
    uint32_t canonical_kind = 0;
    uint32_t custom_kind = 0;
    uint32_t release_call_count = 0;
};

static_assert(std::is_trivially_copyable_v<CleanupOnlyObservationLogProposal>);

uint64_t cleanup_only_observation_fact_mask(
    const CleanupOnlyRouteQuiescenceFacts& q,
    const OnMemoryBankCleanupOnlyReleaseFacts& f) noexcept
{
    uint64_t mask = 0;
    unsigned bit = 0;
    const auto add = [&mask, &bit](const bool value) noexcept {
        if (value) mask |= uint64_t{1} << bit;
        ++bit;
    };
    add(q.cleanup_generation_exact);
    add(q.unpublished_setup_absent);
    add(q.route_idle);
    add(q.route_disabled);
    add(q.route_identity_empty);
    add(q.list_cleanup_clear);
    add(q.pending_patch_empty);
    add(q.frozen_patch_empty);
    add(q.failed_patch_empty);
    add(q.active_journal_empty);
    add(q.stop_retirement_absent);
    add(q.deferred_handoff_absent);
    add(q.frozen_lease_inactive);
    add(q.playback_absent);
    add(q.substrate_bridge_absent);
    add(q.selection_reservation_absent);
    add(q.pause_resume_absent);
    add(q.aggregate_borrowers_absent);
    add(q.aggregate_cleanup_absent);
    add(f.provenance_exact);
    add(f.identity_exact);
    add(f.owner_canonical);
    add(f.callback_quiescence_exact);
    add(f.route_and_setup_absent);
    add(f.route_quiescence_exact);
    add(f.aggregate_and_pause_absent);
    add(f.request_zero);
    add(f.backing_read_succeeded);
    add(f.backing_null);
    add(f.backing_detached);
    add(f.sound_inactive);
    add(f.lookup_signature_valid);
    add(f.release_signature_valid);
    add(f.runtime_enabled);
    add(f.canonical_kind == 2);
    add(f.custom_kind == 0 || f.custom_kind == 2);
    return mask;
}

void emit_cleanup_only_observation(
    const CleanupOnlyObservationLogProposal proposal) noexcept
{
    try {
        bool emit = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            constexpr uint32_t kFactLogBudgetPerGeneration = 32;
            const auto decision = cleanup_only_observation_emission_decision(
                g_cleanup_only_observation_fingerprint,
                proposal.fingerprint,
                g_cleanup_only_observation_fact_logs,
                kFactLogBudgetPerGeneration);
            emit = decision.emit;
            if (decision.reset_fact_budget) {
                g_cleanup_only_observation_fact_logs = 0;
            }
            if (decision.spend_fact_budget) {
                ++g_cleanup_only_observation_fact_logs;
            }
            if (emit) {
                g_cleanup_only_observation_fingerprint = proposal.fingerprint;
            }
        }
        if (!emit) return;
        std::ostringstream out;
        out << "[audio_sead] cleanup_only_observe generation="
            << proposal.fingerprint.generation
            << " phase=" << static_cast<unsigned>(proposal.fingerprint.phase)
            << " failure=" << static_cast<unsigned>(proposal.fingerprint.failure)
            << " fact_mask=0x" << std::hex << proposal.fingerprint.fact_mask
            << std::dec
            << " request_zero=" << proposal.request_zero
            << " backing_read=" << proposal.backing_read
            << " backing_null=" << proposal.backing_null
            << " sound_inactive=" << proposal.sound_inactive
            << " route_setup_absent=" << proposal.route_setup_absent
            << " route_quiescent=" << proposal.route_quiescent
            << " quiescence_failure="
            << cleanup_only_route_quiescence_failure_name(
                proposal.quiescence_failure)
            << " aggregate_pause_absent=" << proposal.aggregate_pause_absent
            << " canonical_kind=" << proposal.canonical_kind
            << " custom_kind=" << proposal.custom_kind
            << " release_eligible=" << proposal.fingerprint.release_eligible
            << " release_claimed=" << proposal.fingerprint.release_claimed
            << " release_outcome="
            << static_cast<unsigned>(proposal.fingerprint.release_outcome)
            << " release_call_count=" << proposal.release_call_count;
        core::log(core::LogLevel::Debug, out.str());
    } catch (...) {
    }
}

struct DeferredCleanupOnlyObservationLog final {
    CleanupOnlyObservationLogProposal proposal{};
    ~DeferredCleanupOnlyObservationLog() noexcept
    {
        emit_cleanup_only_observation(proposal);
    }
};

void observe_cleanup_only_onmemory_bank_noexcept() noexcept
{
    static thread_local bool observing = false;
    if (observing) return;
    observing = true;
    struct Reset final { bool& value; ~Reset() { value = false; } } reset{observing};
    try {
        OnMemoryBankCleanupOnlyRecord precheck_record;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            precheck_record = g_onmemory_bank_cleanup_only.record();
        }
        if (!precheck_record) return;
        DeferredCleanupOnlyObservationLog deferred_log;
        deferred_log.proposal.fingerprint.valid = true;
        deferred_log.proposal.fingerprint.generation = precheck_record.generation;
        deferred_log.proposal.fingerprint.phase =
            static_cast<uint8_t>(precheck_record.phase);
        deferred_log.proposal.fingerprint.failure =
            static_cast<uint8_t>(precheck_record.first_failure);
        deferred_log.proposal.release_call_count = precheck_record.release_call_count;
        if (!cleanup_only_record_requires_exclusive_observation(
                true,
                precheck_record.phase == OnMemoryBankCleanupOnlyPhase::None,
                precheck_record.phase == OnMemoryBankCleanupOnlyPhase::Complete,
                precheck_record.phase
                    == OnMemoryBankCleanupOnlyPhase::FailedRetained)) {
            deferred_log.proposal.fingerprint.release_outcome =
                static_cast<uint8_t>(precheck_record.phase
                        == OnMemoryBankCleanupOnlyPhase::Complete
                    ? CleanupOnlyObservationOutcome::Completed
                    : CleanupOnlyObservationOutcome::FailedRetained);
            return;
        }
        OnMemoryBankReleaseAction claimed_action;
        bool claimed_lookup_signature_valid = false;
        bool claimed_release_signature_valid = false;
        bool claimed_runtime_enabled = false;
        uint64_t claimed_generation = 0;
        CleanupOnlyRouteQuiescenceFailure quiescence_failure =
            CleanupOnlyRouteQuiescenceFailure::None;
        {
            // No route, borrower, selection, chart, or audio-state lock is held
            // while entering this global callback-quiescence boundary.
            AudioShutdownScope callback_quiescence;
        OnMemoryBankCleanupOnlyRecord record;
        CleanupOnlyRouteQuiescenceFacts quiescence{};
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            record = g_onmemory_bank_cleanup_only.record();
            if (!cleanup_only_observation_generation_exact(
                    precheck_record.generation, static_cast<bool>(record),
                    record.generation)) {
                deferred_log.proposal.fingerprint.valid = false;
                return;
            }
            deferred_log.proposal.fingerprint.phase =
                static_cast<uint8_t>(record.phase);
            deferred_log.proposal.fingerprint.failure =
                static_cast<uint8_t>(record.first_failure);
            deferred_log.proposal.release_call_count = record.release_call_count;
            quiescence.cleanup_generation_exact = true;
            quiescence.unpublished_setup_absent = !g_unpublished_audio_setup;
            quiescence.route_idle = g_audio_route_state.phase == AudioRoutePhase::Idle;
            quiescence.route_disabled = g_audio_route_disabled.load(std::memory_order_acquire);
            quiescence.route_identity_empty = !g_audio_route_state.lease_identity.valid()
                && g_audio_route_state.canonical_relinquishment_generation == 0
                && g_audio_route_state.desired_song_id.empty()
                && g_audio_route_state.patched_song_id.empty()
                && !g_audio_route_state.sound && !g_audio_route_state.controller
                && !g_audio_route_state.set_play_handoff_pending
                && !g_audio_route_state.handoff_slot && !g_audio_route_state.handoff_bgm
                && !g_audio_route_state.handoff_sound
                && g_audio_route_state.handoff_request_handle == 0
                && !g_audio_route_state.custom_resource_owned
                && !g_audio_route_state.aggregate_awaiting_transition
                && !g_audio_route_state.owned_slot && !g_audio_route_state.owned_bgm
                && !g_audio_route_state.owned_sound
                && g_audio_route_state.owned_request_handle == 0
                && !g_audio_route_state.reusable_sound
                && !g_audio_route_state.reusable_slot && !g_audio_route_state.reusable_bgm;
            quiescence.list_cleanup_clear = !g_audio_route_state.list_cleanup_pending;
            quiescence.pending_patch_empty = !g_pending_play_setup_patch.sound
                && g_pending_play_setup_patch.song_id.empty()
                && g_pending_play_setup_patch.patches.empty()
                && g_pending_play_setup_patch.restore_safe;
            quiescence.frozen_patch_empty = !g_audio_route_state.frozen_sound_patch.valid()
                && g_audio_route_state.frozen_sound_patch.route_generation == 0
                && !g_audio_route_state.frozen_sound_patch.lease_identity.valid()
                && !g_audio_route_state.frozen_sound_patch.sound
                && g_audio_route_state.frozen_sound_patch.patches.empty();
            quiescence.failed_patch_empty = g_failed_patch_journal.empty();
            quiescence.active_journal_empty = g_active_patch_journal.empty();
            quiescence.stop_retirement_absent =
                g_audio_route_state.stop_retirement.phase == AudioStopRetirementPhase::None
                && g_audio_route_state.stop_retirement.route_generation == 0
                && !g_audio_route_state.stop_retirement.lease_identity.valid()
                && !g_audio_route_state.stop_retirement.controller
                && !g_audio_route_state.stop_retirement.slot
                && !g_audio_route_state.stop_retirement.bgm
                && !g_audio_route_state.stop_retirement.custom_sound
                && g_audio_route_state.stop_retirement.request_handle == 0
                && g_audio_route_state.stop_retirement.origin()
                    == AudioStopRetirementOrigin::None
                && g_audio_route_state.stop_retirement_epoch == 0
                && same_onmemory_bank_retired_backing_evidence(
                    g_audio_route_state.stop_retirement_backing, {});
            quiescence.deferred_handoff_absent =
                !g_audio_route_state.deferred_native_handoff.active();
            quiescence.frozen_lease_inactive = !g_frozen_profile_lease.active();
            quiescence.pause_resume_absent = !g_pause_resume_bank.detached
                && (g_pause_resume_bank.phase == PauseResumeBankPhase::Idle
                    || g_pause_resume_bank.phase == PauseResumeBankPhase::Complete);
        }
        if (record.phase == OnMemoryBankCleanupOnlyPhase::Complete
            || record.phase == OnMemoryBankCleanupOnlyPhase::FailedRetained) {
            deferred_log.proposal.fingerprint.phase =
                static_cast<uint8_t>(record.phase);
            deferred_log.proposal.fingerprint.failure =
                static_cast<uint8_t>(record.first_failure);
            deferred_log.proposal.fingerprint.release_outcome =
                static_cast<uint8_t>(record.phase
                        == OnMemoryBankCleanupOnlyPhase::Complete
                    ? CleanupOnlyObservationOutcome::Completed
                    : CleanupOnlyObservationOutcome::FailedRetained);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            quiescence.substrate_bridge_absent = !g_bgm_canonical_substrate_proof.active
                && !g_bgm_canonical_substrate_proof.bridge;
            quiescence.aggregate_borrowers_absent = true;
            for (const auto& borrower : g_bgm_playback_borrowers) {
                if (borrower.active && (borrower.sound == record.sound.object
                        || borrower.canonical == record.canonical.encode()
                        || borrower.custom == record.custom.encode())) {
                    quiescence.aggregate_borrowers_absent = false;
                    break;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
            quiescence.selection_reservation_absent =
                g_selection_activation_reservation_machine.state()
                    != SelectionActivationReservationState::Reserved;
        }
        quiescence.playback_absent = !registry().playback_snapshot();
        bool aggregate_claim_absent = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
            aggregate_claim_absent =
                g_bgm_aggregate_release_claim.state == AggregateReleaseClaimState::Complete
                && !g_bgm_aggregate_release_claim.action;
        }
        bool aggregate_durable_empty = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_aggregate_durable_rollback_mutex);
            aggregate_durable_empty = g_bgm_aggregate_durable_rollbacks.empty();
        }
        bool aggregate_slots_free = true;
        for (const auto& slot_state : g_bgm_aggregate_rollback_slots) {
            if (slot_state.state.load(std::memory_order_acquire)
                    != AggregateRollbackSlotState::Free) {
                aggregate_slots_free = false;
                break;
            }
        }
        quiescence.aggregate_cleanup_absent = aggregate_claim_absent
            && aggregate_durable_empty && aggregate_slots_free
            && !g_bgm_aggregate_retained_rollback.load(std::memory_order_acquire)
            && !g_bgm_aggregate_exit_pending.load(std::memory_order_acquire)
            && !g_bgm_aggregate_exit_requested.load(std::memory_order_acquire)
            && g_bgm_aggregate_unresolved_publications.load(std::memory_order_acquire) == 0;
        quiescence_failure = first_cleanup_only_route_quiescence_failure(quiescence);

        void* const controller = lookup_current_bgm_controller();
        void* slot = nullptr;
        void* bgm = nullptr;
        void* sound = nullptr;
        uint64_t request = 0;
        void* backing = nullptr;
        uint64_t owner = 0;
        UObjectIdentity sound_identity{};
        const bool chain_exact = controller && controller == record.controller
            && read_controller_bgm_chain(controller, slot, bgm)
            && slot == record.slot && bgm == record.bgm;
        const bool sound_read = chain_exact
            && core::safe_read_field(
                bgm, runtime_layouts::SqexSeadBgm::sound, sound);
        const bool request_read = chain_exact
            && core::safe_read_field(
                bgm, runtime_layouts::SqexSeadBgm::request_handle, request);
        const bool backing_read = chain_exact
            && core::safe_read_field(
                bgm, runtime_layouts::SqexSeadBgm::backing_resource, backing);
        const bool identity_read = chain_exact
            && read_uobject_identity(record.sound.object, sound_identity);
        const bool identity_exact = identity_read
            && sound_identity.live.internal_index >= 0
            && sound_identity.live.serial_number > 0
            && sound_identity.live.internal_index == record.sound.live.internal_index
            && sound_identity.live.serial_number == record.sound.live.serial_number;
        const bool owner_read = identity_exact
            && core::safe_read_field(record.sound.object,
                runtime_layouts::SqexSeadSound::observed_field548, owner);
        const bool provenance_exact = record.revocation_epoch
                == selection_audio_activation_revocation_epoch()
            && record.controller == controller && record.slot == slot && record.bgm == bgm;
        if (!chain_exact || !identity_exact || !owner_read || !provenance_exact
            || owner != record.canonical.encode()) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_onmemory_bank_cleanup_only.fail(record.generation,
                !identity_exact ? OnMemoryBankCleanupOnlyFailure::SoundIdentityInvalid
                    : !provenance_exact ? OnMemoryBankCleanupOnlyFailure::ProvenanceDrift
                    : OnMemoryBankCleanupOnlyFailure::OwnerDrift);
            const auto failed = g_onmemory_bank_cleanup_only.record();
            deferred_log.proposal.fingerprint.phase =
                static_cast<uint8_t>(failed.phase);
            deferred_log.proposal.fingerprint.failure =
                static_cast<uint8_t>(failed.first_failure);
            deferred_log.proposal.fingerprint.release_outcome =
                static_cast<uint8_t>(CleanupOnlyObservationOutcome::FailedRetained);
            return;
        }

        const uint32_t canonical_kind = lookup_onmemory_bank_kind_noexcept(record.canonical.encode());
        const uint32_t custom_kind = lookup_onmemory_bank_kind_noexcept(record.custom.encode());
        if (canonical_kind != 2 || (custom_kind != 0 && custom_kind != 2)) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_onmemory_bank_cleanup_only.fail(record.generation,
                canonical_kind != 2
                    ? OnMemoryBankCleanupOnlyFailure::CanonicalKindInvalid
                    : OnMemoryBankCleanupOnlyFailure::CustomKindInvalid);
            const auto failed = g_onmemory_bank_cleanup_only.record();
            deferred_log.proposal.fingerprint.phase =
                static_cast<uint8_t>(failed.phase);
            deferred_log.proposal.fingerprint.failure =
                static_cast<uint8_t>(failed.first_failure);
            deferred_log.proposal.fingerprint.release_outcome =
                static_cast<uint8_t>(CleanupOnlyObservationOutcome::FailedRetained);
            deferred_log.proposal.canonical_kind = canonical_kind;
            deferred_log.proposal.custom_kind = custom_kind;
            return;
        }
        OnMemoryBankCleanupOnlyReleaseFacts facts{};
        facts.generation = record.generation;
        facts.provenance_exact = provenance_exact;
        facts.identity_exact = identity_exact;
        facts.owner_canonical = owner == record.canonical.encode();
        facts.callback_quiescence_exact = cleanup_only_callback_quiescent(
            {true, true, true, true, true});
        facts.route_and_setup_absent = quiescence.unpublished_setup_absent
            && quiescence.route_identity_empty;
        facts.route_quiescence_exact = cleanup_only_route_quiescent(quiescence);
        facts.aggregate_and_pause_absent = quiescence.aggregate_borrowers_absent
            && quiescence.pause_resume_absent && quiescence.aggregate_cleanup_absent;
        facts.request_zero = request_read && request == 0;
        facts.backing_read_succeeded = backing_read;
    facts.backing_null = cleanup_only_backing_is_detached(backing_read, backing);
        facts.backing_detached = facts.backing_null;
        facts.sound_inactive = sound_read && sound != record.sound.object;
        facts.lookup_signature_valid = g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire);
        facts.release_signature_valid = g_onmemory_bank_release_available.load(std::memory_order_acquire);
        facts.runtime_enabled = g_audio_route_installed.load(std::memory_order_acquire);
        facts.canonical_kind = canonical_kind;
        facts.custom_kind = custom_kind;
        deferred_log.proposal.fingerprint.fact_mask =
            cleanup_only_observation_fact_mask(quiescence, facts);
        deferred_log.proposal.quiescence_failure = quiescence_failure;
        deferred_log.proposal.request_zero = facts.request_zero;
        deferred_log.proposal.backing_read = facts.backing_read_succeeded;
        deferred_log.proposal.backing_null = facts.backing_null;
        deferred_log.proposal.sound_inactive = facts.sound_inactive;
        deferred_log.proposal.route_setup_absent = facts.route_and_setup_absent;
        deferred_log.proposal.route_quiescent = facts.route_quiescence_exact;
        deferred_log.proposal.aggregate_pause_absent = facts.aggregate_and_pause_absent;
        deferred_log.proposal.canonical_kind = canonical_kind;
        deferred_log.proposal.custom_kind = custom_kind;

        if (record.phase == OnMemoryBankCleanupOnlyPhase::ReleaseInFlight) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_onmemory_bank_cleanup_only.observe(
                record.generation, canonical_kind, custom_kind);
            const auto observed = g_onmemory_bank_cleanup_only.record();
            deferred_log.proposal.fingerprint.phase =
                static_cast<uint8_t>(observed.phase);
            deferred_log.proposal.fingerprint.failure =
                static_cast<uint8_t>(observed.first_failure);
            deferred_log.proposal.fingerprint.release_eligible = true;
            deferred_log.proposal.fingerprint.release_claimed = true;
            deferred_log.proposal.fingerprint.release_outcome =
                static_cast<uint8_t>(observed.phase
                        == OnMemoryBankCleanupOnlyPhase::Complete
                    ? CleanupOnlyObservationOutcome::Completed
                    : observed.phase == OnMemoryBankCleanupOnlyPhase::FailedRetained
                        ? CleanupOnlyObservationOutcome::FailedRetained
                        : CleanupOnlyObservationOutcome::Observed);
            deferred_log.proposal.release_call_count = observed.release_call_count;
            return;
        }
        OnMemoryBankReleaseAction action;
        bool claimed = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            claimed = g_onmemory_bank_cleanup_only.claim_release(facts, action);
            const auto after_claim = g_onmemory_bank_cleanup_only.record();
            deferred_log.proposal.fingerprint.phase =
                static_cast<uint8_t>(after_claim.phase);
            deferred_log.proposal.fingerprint.failure =
                static_cast<uint8_t>(after_claim.first_failure);
            deferred_log.proposal.fingerprint.release_eligible = claimed
                || after_claim.phase == OnMemoryBankCleanupOnlyPhase::Complete;
            deferred_log.proposal.fingerprint.release_claimed = claimed;
            deferred_log.proposal.fingerprint.release_outcome =
                static_cast<uint8_t>(claimed
                    ? CleanupOnlyObservationOutcome::ReleaseClaimed
                    : after_claim.phase == OnMemoryBankCleanupOnlyPhase::Complete
                        ? CleanupOnlyObservationOutcome::ReleaseAlreadyAbsent
                        : CleanupOnlyObservationOutcome::Observed);
            deferred_log.proposal.release_call_count = after_claim.release_call_count;
        }
        if (!claimed || !action) {
            return;
        }
        claimed_action = action;
        claimed_lookup_signature_valid = facts.lookup_signature_valid;
        claimed_release_signature_valid = facts.release_signature_valid;
        claimed_runtime_enabled = facts.runtime_enabled;
        claimed_generation = record.generation;
        } // release global callback-quiescence gate before native release

        if (claimed_action) {
            // Preserve the exact one-time claim as its own post-gate event. The
            // deferred proposal will later emit the execution/completion change.
            emit_cleanup_only_observation(deferred_log.proposal);
        }
        if (!claimed_action) return;
        bool claim_still_exact = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const auto current = g_onmemory_bank_cleanup_only.record();
            claim_still_exact = current
                && current.generation == claimed_generation
                && current.phase == OnMemoryBankCleanupOnlyPhase::ReleaseInFlight
                && current.release_call_count == 1
                && current.canonical.encode() == claimed_action.canonical.encode()
                && current.custom.encode() == claimed_action.custom.encode();
            if (!claim_still_exact) {
                g_onmemory_bank_cleanup_only.fail(
                    claimed_generation, OnMemoryBankCleanupOnlyFailure::ProvenanceDrift);
                const auto failed = g_onmemory_bank_cleanup_only.record();
                deferred_log.proposal.fingerprint.phase =
                    static_cast<uint8_t>(failed.phase);
                deferred_log.proposal.fingerprint.failure =
                    static_cast<uint8_t>(failed.first_failure);
                deferred_log.proposal.fingerprint.release_outcome =
                    static_cast<uint8_t>(
                        CleanupOnlyObservationOutcome::FailedRetained);
            }
        }
        if (!claim_still_exact) return;
        const auto execution = execute_onmemory_bank_release(
            claimed_lookup_signature_valid, claimed_release_signature_valid,
            !claimed_runtime_enabled, claimed_action,
            [](const uint64_t token) noexcept {
                return lookup_onmemory_bank_kind_noexcept(token);
            },
            [](const uint64_t* token, const uint8_t asynchronous) noexcept {
                return release_onmemory_bank_async_noexcept(token, asynchronous);
            });
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_onmemory_bank_cleanup_only.finish_release(
                claimed_action, execution.outcome);
            const auto finished = g_onmemory_bank_cleanup_only.record();
            deferred_log.proposal.fingerprint.phase =
                static_cast<uint8_t>(finished.phase);
            deferred_log.proposal.fingerprint.failure =
                static_cast<uint8_t>(finished.first_failure);
            deferred_log.proposal.fingerprint.release_outcome =
                static_cast<uint8_t>(execution.outcome
                        == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                    ? CleanupOnlyObservationOutcome::ReleaseSubmitted
                    : execution.outcome == OnMemoryBankReleaseOutcome::AlreadyAbsent
                        ? CleanupOnlyObservationOutcome::ReleaseAlreadyAbsent
                        : CleanupOnlyObservationOutcome::ReleaseFailed);
            deferred_log.proposal.release_call_count = finished.release_call_count;
        }
    } catch (...) {
    }
}

bool try_clear_custom_activation_quarantine(
    const CustomActivationQuarantineClearReason reason) noexcept
{
    try {
        std::unique_lock<std::recursive_mutex> operation_lock(
            g_audio_route_operations.mutex());
        CustomActivationQuarantineRecord record;
        CustomActivationQuarantineClearFacts facts{};
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            record = g_custom_activation_quarantine;
            if (!record) return true;
            facts.record_exact = true;
            facts.explicit_lifecycle_event =
                custom_activation_quarantine_clear_event(reason);
            facts.original_not_forwarded = !record.original_forwarded;
            facts.token_state_none = record.token_state_none;
            facts.unpublished_setup_absent = !g_unpublished_audio_setup;
            facts.route_idle = g_audio_route_state.phase == AudioRoutePhase::Idle;
            facts.route_disabled =
                g_audio_route_disabled.load(std::memory_order_acquire);
            facts.route_identity_empty = !g_audio_route_state.lease_identity.valid()
                && g_audio_route_state.canonical_relinquishment_generation == 0
                && g_audio_route_state.desired_song_id.empty()
                && g_audio_route_state.patched_song_id.empty()
                && !g_audio_route_state.controller && !g_audio_route_state.sound
                && !g_audio_route_state.set_play_handoff_pending
                && !g_audio_route_state.handoff_slot
                && !g_audio_route_state.handoff_bgm
                && !g_audio_route_state.handoff_sound
                && g_audio_route_state.handoff_request_handle == 0
                && !g_audio_route_state.aggregate_awaiting_transition
                && !g_audio_route_state.owned_slot && !g_audio_route_state.owned_bgm
                && !g_audio_route_state.owned_sound
                && !g_audio_route_state.reusable_sound
                && !g_audio_route_state.reusable_slot
                && !g_audio_route_state.reusable_bgm;
            facts.custom_resource_absent = !g_audio_route_state.custom_resource_owned;
            facts.request_absent = g_audio_route_state.owned_request_handle == 0;
            facts.pending_patch_empty = !g_pending_play_setup_patch.sound
                && g_pending_play_setup_patch.song_id.empty()
                && g_pending_play_setup_patch.patches.empty()
                && g_pending_play_setup_patch.restore_safe;
            facts.frozen_patch_empty =
                g_audio_route_state.frozen_sound_patch.route_generation == 0
                && !g_audio_route_state.frozen_sound_patch.lease_identity.valid()
                && !g_audio_route_state.frozen_sound_patch.sound
                && g_audio_route_state.frozen_sound_patch.patches.empty();
            facts.failed_patch_empty = g_failed_patch_journal.empty();
            facts.active_journal_empty = g_active_patch_journal.empty();
            facts.frozen_lease_inactive = !g_frozen_profile_lease.active();
            facts.normal_token_ownership_absent =
                !static_cast<bool>(g_onmemory_bank_lifecycle.active())
                && !g_onmemory_bank_lifecycle.failed()
                && !g_onmemory_bank_lifecycle.release_in_flight();
            facts.cleanup_only_ownership_absent =
                !g_onmemory_bank_cleanup_only.blocks_custom_routes();
            facts.list_cleanup_clear = !g_audio_route_state.list_cleanup_pending;
            facts.retirement_authority_absent =
                g_audio_route_state.stop_retirement.phase
                    == AudioStopRetirementPhase::None
                && g_audio_route_state.stop_retirement.route_generation == 0
                && !g_audio_route_state.stop_retirement.lease_identity.valid()
                && !g_audio_route_state.stop_retirement.controller
                && !g_audio_route_state.stop_retirement.slot
                && !g_audio_route_state.stop_retirement.bgm
                && !g_audio_route_state.stop_retirement.custom_sound
                && g_audio_route_state.stop_retirement.request_handle == 0
                && g_audio_route_state.stop_retirement.origin()
                    == AudioStopRetirementOrigin::None
                && g_audio_route_state.stop_retirement_epoch == 0
                && same_onmemory_bank_retired_backing_evidence(
                    g_audio_route_state.stop_retirement_backing, {});
            facts.deferred_handoff_absent =
                !g_audio_route_state.deferred_native_handoff.active();
            facts.pause_resume_absent = !g_pause_resume_bank.detached
                && (g_pause_resume_bank.phase == PauseResumeBankPhase::Idle
                    || g_pause_resume_bank.phase == PauseResumeBankPhase::Complete)
                && !g_pause_resume_bank.selection.song
                && !g_pause_resume_bank.lease_identity.valid()
                && !g_pause_resume_bank.owner_tick
                && !g_pause_resume_bank.controller
                && !g_pause_resume_bank.slot && !g_pause_resume_bank.bgm
                && g_pause_resume_bank.request_handle == 0
                && !g_pause_resume_bank.backing
                && !g_pause_resume_bank.backing_observed
                && !g_pause_resume_bank.owner_patch.object
                && g_pause_resume_bank.retirement_monitor.phase
                    == AudioStopRetirementPhase::None
                && g_pause_resume_bank.retirement_monitor.route_generation == 0
                && !g_pause_resume_bank.retirement_monitor.lease_identity.valid()
                && !g_pause_resume_bank.retirement_monitor.controller
                && !g_pause_resume_bank.retirement_monitor.slot
                && !g_pause_resume_bank.retirement_monitor.bgm
                && !g_pause_resume_bank.retirement_monitor.custom_sound
                && g_pause_resume_bank.retirement_monitor.request_handle == 0
                && g_pause_resume_bank.retirement_epoch == 0
                && !g_pause_resume_bank.retired_backing.observed
                && !g_pause_resume_bank.retired_backing.failed
                && g_pause_resume_bank.retired_backing.route_generation == 0
                && g_pause_resume_bank.retired_backing.cleanup_generation == 0
                && g_pause_resume_bank.retired_backing.monitor_epoch == 0
                && g_pause_resume_bank.retired_backing.request_handle == 0
                && !g_pause_resume_bank.retired_backing.value
                && g_pause_resume_bank.pending_release_probe.ordinal == 0
                && g_pause_resume_bank.pending_release_probe.session_epoch == 0
                && g_pause_resume_bank.pending_release_probe.cycle_epoch == 0
                && g_pause_resume_bank.pending_release_probe.lifecycle_state_epoch == 0
                && g_pause_resume_bank.pending_release_probe.retirement_epoch == 0
                && g_pause_resume_bank.pending_release_probe.request_identity == 0
                && !g_pause_resume_bank.pending_release_evidence.observed
                && !g_pause_resume_bank.pending_release_evidence.failed
                && g_pause_resume_bank.pending_release_evidence.route_generation == 0
                && g_pause_resume_bank.pending_release_evidence.cleanup_generation == 0
                && g_pause_resume_bank.pending_release_evidence.monitor_epoch == 0
                && g_pause_resume_bank.pending_release_evidence.request_handle == 0
                && !g_pause_resume_bank.pending_release_evidence.value
                && g_pause_resume_bank.retirement_facts.route_generation == 0
                && g_pause_resume_bank.retirement_facts.cleanup_generation == 0
                && g_pause_resume_bank.retirement_facts.retired_request_handle == 0
                && g_pause_resume_bank.retirement_facts.current_request_handle == 0
                && !g_pause_resume_bank.retirement_facts.current_backing
                && !g_pause_resume_bank.retirement_facts.retired_backing
                && !g_pause_resume_bank.retirement_facts.current_backing_observed
                && !g_pause_resume_bank.retirement_facts.retired_backing_observed
                && !g_pause_resume_bank.exit_requested;
        }
        const PlaybackSnapshot playback = registry().playback_snapshot();
        const CleanupLease cleanup = registry().cleanup_lease();
        facts.playback_absent = !playback.song;
        facts.cleanup_lease_absent = !cleanup.song;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            facts.substrate_bridge_absent =
                !g_bgm_canonical_substrate_proof.active
                && !g_bgm_canonical_substrate_proof.bridge;
            facts.aggregate_borrowers_absent = std::none_of(
                g_bgm_playback_borrowers.begin(), g_bgm_playback_borrowers.end(),
                [](const BgmPlaybackBorrowerRecord& borrower) {
                    return borrower.active;
                });
        }
        {
            std::lock_guard<std::mutex> lock(g_selection_activation_reservation_mutex);
            facts.selection_reservation_absent =
                g_selection_activation_reservation_machine.state()
                    != SelectionActivationReservationState::Reserved;
        }
        bool aggregate_claim_absent = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
            aggregate_claim_absent =
                g_bgm_aggregate_release_claim.state
                    == AggregateReleaseClaimState::Complete
                && !g_bgm_aggregate_release_claim.action;
        }
        bool aggregate_durable_empty = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_aggregate_durable_rollback_mutex);
            aggregate_durable_empty = g_bgm_aggregate_durable_rollbacks.empty();
        }
        bool aggregate_slots_free = true;
        for (const auto& slot_state : g_bgm_aggregate_rollback_slots) {
            if (slot_state.state.load(std::memory_order_acquire)
                    != AggregateRollbackSlotState::Free) {
                aggregate_slots_free = false;
                break;
            }
        }
        facts.aggregate_cleanup_absent = aggregate_claim_absent
            && aggregate_durable_empty && aggregate_slots_free
            && !g_bgm_aggregate_retained_rollback.load(std::memory_order_acquire)
            && !g_bgm_aggregate_exit_pending.load(std::memory_order_acquire)
            && !g_bgm_aggregate_exit_requested.load(std::memory_order_acquire)
            && g_bgm_aggregate_unresolved_publications.load(
                   std::memory_order_acquire) == 0;
        const auto failure =
            first_custom_activation_quarantine_clear_failure(facts);
        if (failure != CustomActivationQuarantineClearFailure::None) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (!g_custom_activation_quarantine
                || g_custom_activation_quarantine.generation != record.generation) {
                return false;
            }
            g_custom_activation_quarantine = {};
            if (g_audio_route_installed.load(std::memory_order_acquire)) {
                g_audio_route_disabled.store(false, std::memory_order_release);
            }
        }
        operation_lock.unlock();
        const char* reason_name = reason
                == CustomActivationQuarantineClearReason::ListReturn ? "list_return"
            : reason == CustomActivationQuarantineClearReason::Shutdown ? "shutdown"
            : "title_or_reselection";
        core::log(core::LogLevel::Info,
            std::string("[audio_sead] quarantine_cleared reason=") + reason_name
                + " generation=" + std::to_string(record.generation)
                + " setup_absent=1 route_idle=1 patch_absent=1 token_state=none");
        return true;
    } catch (...) {
        return false;
    }
}

struct SelectionAdmissionCommitDiagnostic final {
    bool attempted = false;
    SelectionAdmissionCommitFacts facts{};
    SelectionAdmissionSubstrateLiveFacts substrate_live{};
    SelectionAdmissionProofMatchFacts proof_match{};
    ChartAudioAdmissionFacts final_coordinator{};
    SelectionAdmissionCommitFailure first_failure =
        SelectionAdmissionCommitFailure::AdmissionMissing;
    SelectionAdmissionSubstrateLiveFailure substrate_live_failure =
        SelectionAdmissionSubstrateLiveFailure::None;
    SelectionAdmissionProofMatchFailure proof_match_failure =
        SelectionAdmissionProofMatchFailure::None;
    ChartAudioAdmissionFailure final_coordinator_failure =
        ChartAudioAdmissionFailure::None;
    OnMemoryBankRouteDecision arm_decision =
        OnMemoryBankRouteDecision::VanillaUnaffected;
    bool proof_reservation_attempted = false;
    bool proof_global_active_after_reservation = false;
    bool publication_reached = false;
    bool controller_bind_reached = false;
    bool controller_bind_succeeded = false;
    uintptr_t expected_controller = 0;
    uintptr_t current_controller = 0;
    uint64_t activation_reservation_generation = 0;
    uint64_t selection_generation = 0;
    uint64_t route_generation_before = 0;
    uint64_t current_route_generation = 0;
    uint64_t reserved_route_generation = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
    uint64_t expected_lifecycle_epoch = 0;
    uint64_t current_lifecycle_epoch = 0;
    uint64_t proof_route_generation = 0;
    uint64_t global_transaction_generation = 0;
    uint64_t retained_transaction_generation = 0;
    uint64_t global_source_ordinal = 0;
    uint64_t retained_source_ordinal = 0;
    uint64_t global_source_version = 0;
    uint64_t retained_source_version = 0;
    uint64_t global_collection_version = 0;
    uint64_t retained_collection_version = 0;
    uint64_t global_list_exit_epoch = 0;
    uint64_t retained_list_exit_epoch = 0;
    uint64_t global_bridge_generation = 0;
    uint64_t retained_bridge_generation = 0;
    uint8_t global_bridge_phase = 0;
    uint8_t retained_bridge_phase = 0;
    uint64_t global_bridge_release_epoch = 0;
    uint64_t retained_bridge_release_epoch = 0;
    uint64_t global_canonical_token = 0;
    uint64_t retained_canonical_token = 0;
    uint64_t global_custom_token = 0;
    uint64_t retained_custom_token = 0;
    uint8_t global_proof_state = 0;
    uint8_t retained_proof_state = 0;
    uintptr_t global_proof_controller = 0;
    uintptr_t retained_proof_controller = 0;
    uintptr_t global_proof_slot = 0;
    uintptr_t retained_proof_slot = 0;
    uintptr_t global_proof_bgm = 0;
    uintptr_t retained_proof_bgm = 0;
    uintptr_t global_proof_sound = 0;
    uintptr_t retained_proof_sound = 0;
    uint32_t global_proof_sound_index = 0;
    uint32_t retained_proof_sound_index = 0;
    uint32_t global_proof_sound_serial = 0;
    uint32_t retained_proof_sound_serial = 0;
    uint64_t global_proof_request = 0;
    uint64_t retained_proof_request = 0;
    uint64_t global_proof_route_generation = 0;
    uint64_t retained_proof_route_generation = 0;
    uint64_t global_proof_lease_generation = 0;
    uint64_t retained_proof_lease_generation = 0;
    uint64_t global_proof_lease_song = 0;
    uint64_t retained_proof_lease_song = 0;
    uint64_t global_proof_lifecycle_epoch = 0;
    uint64_t retained_proof_lifecycle_epoch = 0;
    uintptr_t global_bridge_callback_sound = 0;
    uintptr_t retained_bridge_callback_sound = 0;
    uint32_t global_bridge_callback_index = 0;
    uint32_t retained_bridge_callback_index = 0;
    uint32_t global_bridge_callback_serial = 0;
    uint32_t retained_bridge_callback_serial = 0;
    uint8_t cleanup_only_phase = 0;
};

static_assert(std::is_trivially_copyable_v<SelectionAdmissionCommitDiagnostic>);

const char* selection_admission_commit_failure_name(
    const SelectionAdmissionCommitFailure failure) noexcept
{
    switch (failure) {
    case SelectionAdmissionCommitFailure::None: return "none";
    case SelectionAdmissionCommitFailure::AdmissionMissing: return "admission_missing";
    case SelectionAdmissionCommitFailure::SelectionGuardMissing: return "selection_guard_missing";
    case SelectionAdmissionCommitFailure::AlreadyCommitted: return "already_committed";
    case SelectionAdmissionCommitFailure::AggregateLeaseMissing: return "aggregate_lease_missing";
    case SelectionAdmissionCommitFailure::AggregateLeaseInactive: return "aggregate_lease_inactive";
    case SelectionAdmissionCommitFailure::AggregateExitRequested: return "aggregate_exit_requested";
    case SelectionAdmissionCommitFailure::AggregateExitPending: return "aggregate_exit_pending";
    case SelectionAdmissionCommitFailure::SelectionGuardDrift: return "selection_guard_drift";
    case SelectionAdmissionCommitFailure::ActivationRevocationEpochDrift: return "activation_revocation_epoch_drift";
    case SelectionAdmissionCommitFailure::ControllerPointerDrift: return "controller_pointer_drift";
    case SelectionAdmissionCommitFailure::ControllerIdentityDrift: return "controller_identity_drift";
    case SelectionAdmissionCommitFailure::SubstrateLiveRejected: return "substrate_live";
    case SelectionAdmissionCommitFailure::ProofMatchRejected: return "proof_match";
    case SelectionAdmissionCommitFailure::ProofReservationFailed: return "proof_reservation";
    case SelectionAdmissionCommitFailure::RouteNotInstalled: return "route_not_installed";
    case SelectionAdmissionCommitFailure::RouteDisabled: return "route_disabled";
    case SelectionAdmissionCommitFailure::FrozenLeaseActive: return "frozen_lease_active";
    case SelectionAdmissionCommitFailure::NativeRouteOwned: return "native_route_owned";
    case SelectionAdmissionCommitFailure::ListCleanupPending: return "list_cleanup_pending";
    case SelectionAdmissionCommitFailure::RouteNotIdle: return "route_not_idle";
    case SelectionAdmissionCommitFailure::RouteGenerationDrift: return "route_generation_drift";
    case SelectionAdmissionCommitFailure::LifecycleEpochDrift: return "lifecycle_epoch_drift";
    case SelectionAdmissionCommitFailure::ProofPredecessorDrift: return "proof_predecessor_drift";
    case SelectionAdmissionCommitFailure::CleanupOnlyBlocked: return "cleanup_only_blocked";
    case SelectionAdmissionCommitFailure::LifecyclePreflightRejected: return "lifecycle_preflight";
    case SelectionAdmissionCommitFailure::SidecarPointerDrift: return "sidecar_pointer_drift";
    case SelectionAdmissionCommitFailure::SidecarHeaderDrift: return "sidecar_header_drift";
    case SelectionAdmissionCommitFailure::SidecarAllocationDrift: return "sidecar_allocation_drift";
    case SelectionAdmissionCommitFailure::SidecarMabfDrift: return "sidecar_mabf_drift";
    case SelectionAdmissionCommitFailure::FinalCoordinatorRejected: return "final_coordinator";
    case SelectionAdmissionCommitFailure::PublicationNotReached: return "publication_not_reached";
    case SelectionAdmissionCommitFailure::ControllerBindFailed: return "controller_bind_failed";
    case SelectionAdmissionCommitFailure::Exception: return "exception";
    }
    return "unknown";
}

const char* selection_admission_proof_match_failure_name(
    const SelectionAdmissionProofMatchFailure failure) noexcept
{
    switch (failure) {
    case SelectionAdmissionProofMatchFailure::None: return "none";
    case SelectionAdmissionProofMatchFailure::GlobalInactive: return "global_active";
    case SelectionAdmissionProofMatchFailure::RetainedInactive: return "retained_active";
    case SelectionAdmissionProofMatchFailure::GlobalStateDrift: return "global_state";
    case SelectionAdmissionProofMatchFailure::RetainedStateDrift: return "retained_state";
    case SelectionAdmissionProofMatchFailure::TransactionInvalid: return "transaction_nonzero";
    case SelectionAdmissionProofMatchFailure::TransactionDrift: return "transaction";
    case SelectionAdmissionProofMatchFailure::SourceOrdinalDrift: return "source_ordinal";
    case SelectionAdmissionProofMatchFailure::SourceVersionDrift: return "source_version";
    case SelectionAdmissionProofMatchFailure::CollectionVersionDrift: return "collection_version";
    case SelectionAdmissionProofMatchFailure::ListExitDrift: return "list_exit";
    case SelectionAdmissionProofMatchFailure::ControllerPointerDrift: return "controller_pointer";
    case SelectionAdmissionProofMatchFailure::ControllerIdentityDrift: return "controller_identity";
    case SelectionAdmissionProofMatchFailure::SlotBgmDrift: return "slot_bgm";
    case SelectionAdmissionProofMatchFailure::SoundPointerDrift: return "sound_pointer";
    case SelectionAdmissionProofMatchFailure::SoundIndexDrift: return "sound_index";
    case SelectionAdmissionProofMatchFailure::SoundSerialDrift: return "sound_serial";
    case SelectionAdmissionProofMatchFailure::RequestDrift: return "request";
    case SelectionAdmissionProofMatchFailure::RequestRebaseLineageDrift: return "request_rebase_lineage";
    case SelectionAdmissionProofMatchFailure::RouteGenerationDrift: return "route_generation";
    case SelectionAdmissionProofMatchFailure::LeaseDrift: return "lease";
    case SelectionAdmissionProofMatchFailure::LifecycleEpochDrift: return "lifecycle_epoch";
    case SelectionAdmissionProofMatchFailure::CanonicalTokenDrift: return "canonical_token";
    case SelectionAdmissionProofMatchFailure::CustomTokenDrift: return "custom_token";
    case SelectionAdmissionProofMatchFailure::BridgeGenerationDrift: return "bridge_generation";
    case SelectionAdmissionProofMatchFailure::BridgePhaseDrift: return "bridge_phase";
    case SelectionAdmissionProofMatchFailure::BridgeReleaseEpochDrift: return "bridge_release_epoch";
    case SelectionAdmissionProofMatchFailure::BridgeCallbackPointerDrift: return "bridge_callback_pointer";
    case SelectionAdmissionProofMatchFailure::BridgeCallbackIdentityDrift: return "bridge_callback_identity";
    case SelectionAdmissionProofMatchFailure::BridgeCanonicalTokenDrift: return "bridge_canonical_token";
    case SelectionAdmissionProofMatchFailure::BridgeAuthorityDrift: return "bridge_authority";
    case SelectionAdmissionProofMatchFailure::ResetLineageDrift: return "reset_lineage";
    }
    return "unknown";
}

const char* chart_audio_admission_failure_name(
    const ChartAudioAdmissionFailure failure) noexcept
{
    switch (failure) {
    case ChartAudioAdmissionFailure::None: return "none";
    case ChartAudioAdmissionFailure::ChartPlanIncomplete: return "chart_plan";
    case ChartAudioAdmissionFailure::CallbackOrExitLeaseMissing: return "callback_exit_lease";
    case ChartAudioAdmissionFailure::SelectionDrift: return "selection";
    case ChartAudioAdmissionFailure::SidecarDrift: return "sidecar";
    case ChartAudioAdmissionFailure::ControllerDrift: return "controller";
    case ChartAudioAdmissionFailure::RouteNotExactIdle: return "route_idle";
    case ChartAudioAdmissionFailure::FrozenOrCleanupActive: return "frozen_cleanup";
    case ChartAudioAdmissionFailure::LifecycleArmRejected: return "lifecycle_arm";
    case ChartAudioAdmissionFailure::ExitOrUnresolvedActive: return "exit_unresolved";
    }
    return "unknown";
}

void log_selection_admission_commit_diagnostic_noexcept(
    const SelectionAdmissionCommitDiagnostic& d) noexcept
{
    if (!d.attempted) return;
    try {
        const auto mask = [](const auto& values) noexcept {
            uint64_t result = 0;
            for (size_t i = 0; i < std::size(values); ++i) {
                if (values[i]) result |= uint64_t{1} << i;
            }
            return result;
        };
        const bool commit_values[] = {
            d.facts.admission_exists, d.facts.selection_guard_acquired,
            d.facts.not_committed, d.facts.aggregate_lease_present,
            d.facts.aggregate_lease_active, d.facts.exit_not_requested,
            d.facts.exit_not_pending, d.facts.selection_guard_current,
            d.facts.controller_pointer_exact, d.facts.controller_identity_exact,
            d.facts.substrate_live_exact, d.facts.proof_match_exact,
            d.facts.proof_reservation_succeeded, d.facts.route_installed,
            d.facts.route_enabled, d.facts.frozen_lease_inactive,
            d.facts.native_route_unowned, d.facts.list_cleanup_clear,
            d.facts.route_idle, d.facts.route_generation_exact,
            d.facts.lifecycle_epoch_exact, d.facts.proof_predecessor_exact,
            d.facts.cleanup_only_clear, d.facts.lifecycle_preflight_allowed,
            d.facts.sidecar_pointer_exact, d.facts.sidecar_header_exact,
            d.facts.sidecar_allocation_exact, d.facts.sidecar_mabf_exact,
            d.facts.final_coordinator_exact, d.facts.publication_reached,
            d.facts.controller_bind_exact};
        const bool substrate_values[] = {
            d.substrate_live.proof_active, d.substrate_live.proof_state_exact,
            d.substrate_live.transaction_nonzero, d.substrate_live.controller_nonnull,
            d.substrate_live.controller_pointer_exact,
            d.substrate_live.controller_identity_exact, d.substrate_live.chain_read,
            d.substrate_live.sound_read, d.substrate_live.request_read,
            d.substrate_live.state_read, d.substrate_live.sound_nonnull,
            d.substrate_live.sound_identity_read, d.substrate_live.slot_bgm_exact,
            d.substrate_live.sound_pointer_exact, d.substrate_live.sound_identity_exact,
            d.substrate_live.request_exact, d.substrate_live.state_four};
        const bool proof_values[] = {
            d.proof_match.left_active, d.proof_match.right_active,
            d.proof_match.left_state_exact, d.proof_match.right_state_exact,
            d.proof_match.transaction_nonzero, d.proof_match.transaction_exact,
            d.proof_match.source_ordinal_exact, d.proof_match.source_version_exact,
            d.proof_match.collection_version_exact, d.proof_match.list_exit_exact,
            d.proof_match.controller_pointer_exact,
            d.proof_match.controller_identity_exact, d.proof_match.slot_bgm_exact,
            d.proof_match.sound_pointer_exact, d.proof_match.sound_index_exact,
            d.proof_match.sound_serial_exact, d.proof_match.request_exact,
            d.proof_match.route_generation_exact, d.proof_match.lease_exact,
            d.proof_match.lifecycle_epoch_exact, d.proof_match.canonical_token_exact,
            d.proof_match.custom_token_exact, d.proof_match.bridge_generation_exact,
            d.proof_match.bridge_phase_exact, d.proof_match.bridge_release_epoch_exact,
            d.proof_match.bridge_callback_pointer_exact,
            d.proof_match.bridge_callback_identity_exact,
            d.proof_match.bridge_canonical_token_exact};
        const bool final_values[] = {
            d.final_coordinator.chart_plan_complete,
            d.final_coordinator.callback_and_exit_lease,
            d.final_coordinator.selection_exact,
            d.final_coordinator.sidecar_exact,
            d.final_coordinator.controller_exact,
            d.final_coordinator.route_exact_and_idle,
            d.final_coordinator.frozen_and_cleanup_clear,
            d.final_coordinator.lifecycle_allows_arm,
            d.final_coordinator.exit_and_unresolved_clear};
        std::ostringstream out;
        out << "[audio_sead] selection_admission_commit"
            << " first_failed_predicate="
            << selection_admission_commit_failure_name(d.first_failure);
        if (d.first_failure == SelectionAdmissionCommitFailure::SubstrateLiveRejected) {
            out << '.' << selection_admission_substrate_live_failure_name(
                d.substrate_live_failure);
        } else if (d.first_failure
                == SelectionAdmissionCommitFailure::ProofMatchRejected) {
            out << '.' << selection_admission_proof_match_failure_name(
                d.proof_match_failure);
        } else if (d.first_failure
                == SelectionAdmissionCommitFailure::FinalCoordinatorRejected) {
            out << '.' << chart_audio_admission_failure_name(
                d.final_coordinator_failure);
        }
        out
            << " failure=" << static_cast<unsigned>(d.first_failure)
            << " commit_facts=0x" << std::hex << mask(commit_values)
            << " substrate_facts=0x" << mask(substrate_values)
            << " proof_facts=0x" << mask(proof_values)
            << " final_facts=0x" << mask(final_values) << std::dec
            << " substrate_live_failure="
            << selection_admission_substrate_live_failure_name(d.substrate_live_failure)
            << " proof_match_failure="
            << selection_admission_proof_match_failure_name(d.proof_match_failure)
            << " final_coordinator_failure="
            << chart_audio_admission_failure_name(d.final_coordinator_failure)
            << " proof_reservation_attempted=" << d.proof_reservation_attempted
            << " proof_reservation_succeeded=" << d.facts.proof_reservation_succeeded
            << " global_active_after_reservation="
            << d.proof_global_active_after_reservation
            << " publication_reached=" << d.publication_reached
            << " bind_reached=" << d.controller_bind_reached
            << " bind_succeeded=" << d.controller_bind_succeeded
            << " selection_generation=" << d.selection_generation
            << " activation_reservation=" << d.activation_reservation_generation
            << " route_before=" << d.route_generation_before
            << " route_current=" << d.current_route_generation
            << " route_reserved=" << d.reserved_route_generation
            << " lease=" << d.lease_generation
            << " song=" << d.song_key
            << " lifecycle_expected=" << d.expected_lifecycle_epoch
            << " lifecycle_current=" << d.current_lifecycle_epoch
            << " proof_route=" << d.proof_route_generation
            << " controller_expected=0x" << std::hex << d.expected_controller
            << " controller_current=0x" << d.current_controller
            << " live_slot=0x" << d.substrate_live.observed_slot
            << " live_bgm=0x" << d.substrate_live.observed_bgm
            << " live_sound=0x" << d.substrate_live.observed_sound
            << " live_request=0x" << d.substrate_live.observed_request
            << " live_sound_index=0x" << d.substrate_live.observed_sound_index
            << " live_sound_serial=0x" << d.substrate_live.observed_sound_serial
            << " live_state=0x" << static_cast<unsigned>(
                d.substrate_live.observed_state)
            << " global_transaction=0x" << d.global_transaction_generation
            << " retained_transaction=0x" << d.retained_transaction_generation
            << " global_bridge=0x" << d.global_bridge_generation
            << " retained_bridge=0x" << d.retained_bridge_generation
            << " global_canonical=0x" << d.global_canonical_token
            << " retained_canonical=0x" << d.retained_canonical_token
            << " global_custom=0x" << d.global_custom_token
            << " retained_custom=0x" << d.retained_custom_token
            << " global_controller=0x" << d.global_proof_controller
            << " retained_controller=0x" << d.retained_proof_controller
            << " global_slot=0x" << d.global_proof_slot
            << " retained_slot=0x" << d.retained_proof_slot
            << " global_bgm=0x" << d.global_proof_bgm
            << " retained_bgm=0x" << d.retained_proof_bgm
            << " global_sound=0x" << d.global_proof_sound
            << " retained_sound=0x" << d.retained_proof_sound
            << " global_request=0x" << d.global_proof_request
            << " retained_request=0x" << d.retained_proof_request
            << " global_bridge_callback=0x" << d.global_bridge_callback_sound
            << " retained_bridge_callback=0x" << d.retained_bridge_callback_sound
            << std::dec
            << " global_proof_state=" << static_cast<unsigned>(d.global_proof_state)
            << " retained_proof_state=" << static_cast<unsigned>(d.retained_proof_state)
            << " global_sound_index=" << d.global_proof_sound_index
            << " retained_sound_index=" << d.retained_proof_sound_index
            << " global_sound_serial=" << d.global_proof_sound_serial
            << " retained_sound_serial=" << d.retained_proof_sound_serial
            << " global_proof_route=" << d.global_proof_route_generation
            << " retained_proof_route=" << d.retained_proof_route_generation
            << " global_proof_lease=" << d.global_proof_lease_generation
            << " retained_proof_lease=" << d.retained_proof_lease_generation
            << " global_proof_lease_song=" << d.global_proof_lease_song
            << " retained_proof_lease_song=" << d.retained_proof_lease_song
            << " global_proof_lifecycle=" << d.global_proof_lifecycle_epoch
            << " retained_proof_lifecycle=" << d.retained_proof_lifecycle_epoch
            << " global_bridge_callback_index=" << d.global_bridge_callback_index
            << " retained_bridge_callback_index=" << d.retained_bridge_callback_index
            << " global_bridge_callback_serial=" << d.global_bridge_callback_serial
            << " retained_bridge_callback_serial=" << d.retained_bridge_callback_serial
            << " global_source_ordinal=" << d.global_source_ordinal
            << " retained_source_ordinal=" << d.retained_source_ordinal
            << " global_source_version=" << d.global_source_version
            << " retained_source_version=" << d.retained_source_version
            << " global_collection_version=" << d.global_collection_version
            << " retained_collection_version=" << d.retained_collection_version
            << " global_list_exit=" << d.global_list_exit_epoch
            << " retained_list_exit=" << d.retained_list_exit_epoch
            << " global_bridge_phase=" << static_cast<unsigned>(d.global_bridge_phase)
            << " retained_bridge_phase=" << static_cast<unsigned>(d.retained_bridge_phase)
            << " global_bridge_release=" << d.global_bridge_release_epoch
            << " retained_bridge_release=" << d.retained_bridge_release_epoch
            << " cleanup_phase=" << static_cast<unsigned>(d.cleanup_only_phase)
            << " arm_decision=" << static_cast<unsigned>(d.arm_decision);
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

struct DeferredSelectionAdmissionCommitMarker final {
    SelectionAdmissionCommitDiagnostic* diagnostic = nullptr;
    ~DeferredSelectionAdmissionCommitMarker() noexcept
    {
        if (diagnostic) log_selection_admission_commit_diagnostic_noexcept(*diagnostic);
    }
};

struct SelectionAudioAdmission::Impl final {
    SelectionAdmissionCommitDiagnostic commit_diagnostic{};
    DeferredSelectionAdmissionCommitMarker commit_marker{&commit_diagnostic};
    AudioCallbackScope callback_scope{AudioRouteCallbackKind::SelectionAdmission};
    std::unique_lock<std::recursive_mutex> operation_lock{
        g_audio_route_operations.mutex()};
    std::unique_ptr<AggregateMutationLease> aggregate_lease;
    SelectionSnapshot selection;
    std::string song_id;
    const SidecarRuntimeState* sidecar = nullptr;
    void* sidecar_header = nullptr;
    size_t sidecar_allocation_size = 0;
    size_t sidecar_mabf_size = 0;
    uint64_t route_generation_before = 0;
    uint64_t reserved_route_generation = 0;
    AudioRouteLeaseIdentity reserved_lease{};
    uint64_t lifecycle_state_epoch = 0;
    void* controller = nullptr;
    UObjectIdentity controller_identity{};
    ControllerIdentityProof controller_proof{};
    BgmCanonicalSubstrateProof canonical_substrate_proof{};
    CanonicalSubstrateBridgeAuthority substrate_bridge{};
    CustomContextToken selection_token{};
    bool selection_guard_acquired = false;
    bool committed = false;
    bool commit_attempted = false;
    uint64_t activation_reservation_generation = 0;
    SelectionActivationReservationRecord substrate_phase_attempt{};

    ~Impl() noexcept
    {
        const auto ownership = selection_activation_ownership_transition(
            substrate_phase_attempt.transfer_state,
            SelectionActivationOwnershipEvent::Rollback, true);
        if (!committed && !commit_attempted) {
            (void)rollback_selection_activation_substrate_phases(
                substrate_phase_attempt);
            (void)retire_consumed_selection_activation_reservation(
                activation_reservation_generation);
        }
        if (selection_guard_acquired && !committed) {
            (void)registry().release_selection_guard(selection_token);
        }
        if (!committed && ownership.rollback
                == SelectionActivationRollbackPlan::ReservationAndFrozenProfile
            && selection && activation_reservation_generation != 0) {
            SelectionSnapshot thawed;
            (void)registry().thaw_active_profile_for_activation(
                selection, activation_reservation_generation, thawed);
        }
    }
};

SelectionAudioAdmission::SelectionAudioAdmission() noexcept = default;
SelectionAudioAdmission::SelectionAudioAdmission(SelectionAudioAdmission&&) noexcept = default;
SelectionAudioAdmission& SelectionAudioAdmission::operator=(SelectionAudioAdmission&&) noexcept = default;
SelectionAudioAdmission::~SelectionAudioAdmission() noexcept = default;

SelectionAudioAdmission::operator bool() const noexcept
{
    return impl_ && impl_->selection_guard_acquired && !impl_->committed;
}

SelectionAudioAdmission begin_selection_audio_admission(
    SelectionActivationClaim claim,
    const bool chart_plan_complete) noexcept
{
    SelectionAudioAdmission admission;
    SelectionActivationReservationRecord activation_reservation;
    if (!chart_plan_complete || !claim.impl_ || !claim.impl_->armed) {
        return admission;
    }
    const SelectionSnapshot expected_selection = claim.impl_->frozen_selection;
    if (!expected_selection
        || !consume_selection_activation_reservation(
            *claim.impl_, activation_reservation)) {
        return admission;
    }
    bool ownership_transferred = false;
    auto phase_attempt_disposition =
        SelectionActivationSubstrateAttemptDisposition::RestoreIfExact;
    bool phase_attempt_native_identity_exact = true;
    auto rollback_consumed_phase_attempt = make_deferred_noexcept_action(
        [&]() noexcept {
            if (!ownership_transferred) {
                (void)rollback_selection_activation_substrate_phases(
                    activation_reservation,
                    false,
                    phase_attempt_disposition,
                    phase_attempt_native_identity_exact);
                (void)retire_consumed_selection_activation_reservation(
                    activation_reservation.generation);
                const auto ownership = selection_activation_ownership_transition(
                    activation_reservation.transfer_state,
                    SelectionActivationOwnershipEvent::Rollback, true);
                if (ownership.rollback
                    == SelectionActivationRollbackPlan::ReservationAndFrozenProfile) {
                    SelectionSnapshot thawed;
                    (void)registry().thaw_active_profile_for_activation(
                        activation_reservation.selection,
                        activation_reservation.generation, thawed);
                }
            }
        });
    rollback_consumed_phase_attempt.make_eligible();
    try {
        RegistrySnapshot sidecar_catalog;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            sidecar_catalog = g_sidecar_catalog;
        }
        if (!registry_snapshot_owns_selection(
                sidecar_catalog, expected_selection)) return admission;
        if (!selection_activation_attempt_epoch_exact(
                activation_reservation
                    .substrate_phase_attempt_revocation_epoch,
                g_selection_activation_revocation_epoch.load(
                    std::memory_order_acquire))) {
            phase_attempt_disposition =
                SelectionActivationSubstrateAttemptDisposition::RetainFailClosed;
            phase_attempt_native_identity_exact = false;
            return admission;
        }
        auto state = std::make_unique<SelectionAudioAdmission::Impl>();
        if (!state->callback_scope || !expected_selection
            || !expected_selection.song || expected_selection.song->id.empty()) {
            return admission;
        }

        const auto initial_ownership = snapshot_bgm_aggregate_exit_ownership(
            false, false);
        if (!bgm_playback_aggregate_menu_ready(initial_ownership,
                g_bgm_aggregate_exit_requested.load(std::memory_order_acquire),
                g_bgm_aggregate_exit_pending.load(std::memory_order_acquire))) {
            return admission;
        }

        state->aggregate_lease = std::make_unique<AggregateMutationLease>();
        if (!state->aggregate_lease->active) return admission;
        const auto protected_ownership = snapshot_bgm_aggregate_exit_ownership(
            true, false);
        if (!bgm_playback_aggregate_menu_ready(protected_ownership,
                g_bgm_aggregate_exit_requested.load(std::memory_order_acquire),
                g_bgm_aggregate_exit_pending.load(std::memory_order_acquire))) {
            return admission;
        }

        const SelectionSnapshot current_selection = registry().selection_snapshot();
        if (!selection_snapshot_exact(current_selection, expected_selection)
            || !selection_snapshot_exact(
                current_selection, activation_reservation.selection)) {
            return admission;
        }

        state->controller = lookup_current_bgm_controller();
        if (!state->controller
            || !read_uobject_identity(state->controller, state->controller_identity)) {
            phase_attempt_disposition =
                SelectionActivationSubstrateAttemptDisposition::RetainFailClosed;
            phase_attempt_native_identity_exact = false;
            return admission;
        }
        state->controller_proof = controller_identity_proof(
            state->controller, state->controller_identity);
        if (!controller_identity_proof_valid(state->controller_proof)) {
            phase_attempt_disposition =
                SelectionActivationSubstrateAttemptDisposition::RetainFailClosed;
            phase_attempt_native_identity_exact = false;
            return admission;
        }
        if (state->controller != activation_reservation.controller
            || !controller_identity_proof_matches(
                state->controller_proof,
                activation_reservation.controller_proof)) {
            phase_attempt_disposition =
                SelectionActivationSubstrateAttemptDisposition::RetainFailClosed;
            phase_attempt_native_identity_exact = false;
            return admission;
        }

        if (activation_reservation.canonical_substrate_proof_exact) {
            BgmCanonicalSubstrateProof proof;
            {
                std::lock_guard<std::mutex> lock(
                    g_bgm_playback_borrower_mutex);
                proof = g_bgm_canonical_substrate_proof;
            }
            const bool reservation_proof_exact = proof.active
                && proof.state
                    == BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished
                && proof.transaction_generation
                    == activation_reservation
                        .canonical_substrate_transaction_generation
                && proof.transaction_generation != 0
                && proof.request
                    == activation_reservation.canonical_substrate_request
                && proof.sound_identity.internal_index
                    == activation_reservation
                        .canonical_substrate_sound_identity.internal_index
                && proof.sound_identity.serial_number
                    == activation_reservation
                        .canonical_substrate_sound_identity.serial_number
                && proof.source_ordinal
                    == activation_reservation
                        .canonical_substrate_source_ordinal
                && proof.list_exit_epoch
                    == activation_reservation
                        .canonical_substrate_list_exit_epoch
                && canonical_substrate_active_sound_live_exact(
                    proof, state->controller, state->controller_proof);
            if (!reservation_proof_exact) {
                phase_attempt_disposition =
                    SelectionActivationSubstrateAttemptDisposition::RetainFailClosed;
                phase_attempt_native_identity_exact = false;
                return admission;
            }
            state->canonical_substrate_proof = proof;
            state->substrate_bridge = activation_reservation.substrate_bridge;
        }

        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (!g_audio_route_installed.load(std::memory_order_acquire)
                || g_audio_route_disabled.load(std::memory_order_acquire)
                || g_frozen_profile_lease.active()
                || native_audio_route_owned_locked()
                || g_audio_route_state.list_cleanup_pending
                || g_audio_route_state.phase != AudioRoutePhase::Idle
                || g_audio_route_state.generation
                    != activation_reservation.route_generation
                || g_audio_route_state.generation == UINT64_MAX
                || g_next_audio_route_generation == UINT64_MAX
                || g_next_audio_route_generation
                    != activation_reservation.next_lease_generation
                || g_onmemory_bank_lifecycle.state_epoch()
                    != activation_reservation.lifecycle_state_epoch) {
                return admission;
            }
            state->sidecar = find_ready_sidecar_locked(*expected_selection.song);
            if (!state->sidecar) return admission;
            state->sidecar_header = state->sidecar->allocation.sead_header();
            state->sidecar_allocation_size = state->sidecar->allocation.allocation_size();
            state->sidecar_mabf_size = state->sidecar->allocation.mabf_size();
            if (!state->sidecar_header || state->sidecar_allocation_size == 0
                || state->sidecar_mabf_size == 0
                || state->sidecar != activation_reservation.sidecar
                || state->sidecar_header
                    != activation_reservation.sidecar_header
                || state->sidecar_allocation_size
                    != activation_reservation.sidecar_allocation_size
                || state->sidecar_mabf_size
                    != activation_reservation.sidecar_mabf_size
                || g_onmemory_bank_cleanup_only.blocks_custom_routes()
                || g_custom_activation_quarantine
                || g_onmemory_bank_lifecycle.preflight_arm(
                       true,
                       g_audio_route_installed.load(std::memory_order_acquire),
                       g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
                       g_onmemory_bank_release_available.load(std::memory_order_acquire))
                    != OnMemoryBankRouteDecision::Allowed) {
                return admission;
            }
            state->route_generation_before = g_audio_route_state.generation;
            state->reserved_route_generation = g_audio_route_state.generation + 1;
            state->reserved_lease = {
                ++g_next_audio_route_generation,
                audio_route_song_key(expected_selection.song->id),
            };
            state->lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
        }

        state->selection = current_selection;
        state->activation_reservation_generation
            = activation_reservation.generation;
        state->song_id = current_selection.song->id;
        state->selection_token = {
            current_selection.generation,
            state->reserved_route_generation,
            state->reserved_lease.generation,
            state->reserved_lease.song_key,
            state->controller,
            nullptr,
            nullptr,
            nullptr,
            0,
        };
        if (state->substrate_bridge.phase
                == CanonicalSubstrateBridgePhase::Reserved) {
            state->substrate_bridge.phase = CanonicalSubstrateBridgePhase::Armed;
            state->substrate_bridge.selection_generation =
                current_selection.generation;
            state->substrate_bridge.route_generation =
                state->reserved_route_generation;
            state->substrate_bridge.lease = state->reserved_lease;
            state->substrate_bridge.song_key = state->reserved_lease.song_key;
        }
        if (!chart_audio_admission_exact({
                chart_plan_complete,
                state->callback_scope && state->aggregate_lease
                    && state->aggregate_lease->active,
                true,
                state->sidecar && state->sidecar_header,
                !state->controller || controller_identity_proof_valid(
                    state->controller_proof),
                true,
                true,
                true,
                true,
            })) {
            return admission;
        }
        if (!registry().acquire_selection_guard(
                state->selection, state->selection_token)) {
            return admission;
        }
        state->selection_guard_acquired = true;
        const auto transferred = selection_activation_ownership_transition(
            activation_reservation.transfer_state,
            SelectionActivationOwnershipEvent::TransferToAdmission, true);
        if (!transferred.accepted) return admission;
        activation_reservation.transfer_state = transferred.next;
        state->substrate_phase_attempt = activation_reservation;
        admission.impl_ = std::move(state);
        ownership_transferred = true;
        static std::atomic_int s_reserved_logs{0};
        if (s_reserved_logs.fetch_add(1, std::memory_order_relaxed) < 64) {
            std::ostringstream out;
            out << "[audio_sead] selection_admission status=reserved"
                << " song_id=" << admission.impl_->song_id
                << " selection_generation="
                << admission.impl_->selection.generation
                << " route_generation="
                << admission.impl_->reserved_route_generation
                << " lease_generation="
                << admission.impl_->reserved_lease.generation
                << " controller=0x" << std::hex
                << reinterpret_cast<uintptr_t>(admission.impl_->controller)
                << std::dec << " native_callback=0";
            core::log(core::LogLevel::Info, out.str());
        }
    } catch (...) {
        admission.impl_.reset();
    }
    return admission;
}

AudioRouteArmLease commit_selection_audio_admission(
    SelectionAudioAdmission& admission) noexcept
{
    if (!admission.impl_) {
        SelectionAdmissionCommitDiagnostic diagnostic;
        diagnostic.attempted = true;
        diagnostic.first_failure = SelectionAdmissionCommitFailure::AdmissionMissing;
        log_selection_admission_commit_diagnostic_noexcept(diagnostic);
        return {AudioRouteArmStatus::Failed, {}};
    }
    auto& state = *admission.impl_;
    state.commit_attempted = true;
    auto retire_commit_attempt = make_deferred_noexcept_action([&]() noexcept {
        (void)rollback_selection_activation_substrate_phases(
            state.substrate_phase_attempt,
            false,
            SelectionActivationSubstrateAttemptDisposition::RetainFailClosed,
            false);
        (void)retire_consumed_selection_activation_reservation(
            state.activation_reservation_generation);
    });
    retire_commit_attempt.make_eligible();
    const auto committed_ownership = selection_activation_ownership_transition(
        state.substrate_phase_attempt.transfer_state,
        SelectionActivationOwnershipEvent::Commit, true);
    if (!committed_ownership.accepted) {
        return {AudioRouteArmStatus::Failed, {}};
    }
    auto& diagnostic = state.commit_diagnostic;
    diagnostic = {};
    diagnostic.attempted = true;
    diagnostic.facts.admission_exists = true;
    diagnostic.facts.selection_guard_acquired = state.selection_guard_acquired;
    diagnostic.facts.not_committed = !state.committed;
    diagnostic.activation_reservation_generation =
        state.activation_reservation_generation;
    diagnostic.selection_generation = state.selection.generation;
    diagnostic.route_generation_before = state.route_generation_before;
    diagnostic.reserved_route_generation = state.reserved_route_generation;
    diagnostic.lease_generation = state.reserved_lease.generation;
    diagnostic.song_key = state.reserved_lease.song_key;
    diagnostic.expected_lifecycle_epoch = state.lifecycle_state_epoch;
    diagnostic.proof_route_generation =
        state.canonical_substrate_proof.route_generation;
    diagnostic.expected_controller =
        reinterpret_cast<uintptr_t>(state.controller);
    diagnostic.retained_transaction_generation =
        state.canonical_substrate_proof.transaction_generation;
    diagnostic.retained_source_ordinal =
        state.canonical_substrate_proof.source_ordinal;
    diagnostic.retained_source_version =
        state.canonical_substrate_proof.source_version;
    diagnostic.retained_collection_version =
        state.canonical_substrate_proof.source_collection_version;
    diagnostic.retained_list_exit_epoch =
        state.canonical_substrate_proof.list_exit_epoch;
    diagnostic.retained_bridge_generation =
        state.canonical_substrate_proof.bridge.generation;
    diagnostic.retained_bridge_phase = static_cast<uint8_t>(
        state.canonical_substrate_proof.bridge.phase);
    diagnostic.retained_bridge_release_epoch =
        state.canonical_substrate_proof.bridge.release_completion_epoch;
    diagnostic.retained_canonical_token =
        state.canonical_substrate_proof.canonical;
    diagnostic.retained_custom_token = state.canonical_substrate_proof.custom;
    diagnostic.retained_proof_state = static_cast<uint8_t>(
        state.canonical_substrate_proof.state);
    diagnostic.retained_proof_controller = reinterpret_cast<uintptr_t>(
        state.canonical_substrate_proof.controller);
    diagnostic.retained_proof_slot = reinterpret_cast<uintptr_t>(
        state.canonical_substrate_proof.slot);
    diagnostic.retained_proof_bgm = reinterpret_cast<uintptr_t>(
        state.canonical_substrate_proof.bgm);
    diagnostic.retained_proof_sound = reinterpret_cast<uintptr_t>(
        state.canonical_substrate_proof.sound);
    diagnostic.retained_proof_sound_index =
        state.canonical_substrate_proof.sound_identity.internal_index;
    diagnostic.retained_proof_sound_serial =
        state.canonical_substrate_proof.sound_identity.serial_number;
    diagnostic.retained_proof_request = state.canonical_substrate_proof.request;
    diagnostic.retained_proof_route_generation =
        state.canonical_substrate_proof.route_generation;
    diagnostic.retained_proof_lease_generation =
        state.canonical_substrate_proof.lease.generation;
    diagnostic.retained_proof_lease_song =
        state.canonical_substrate_proof.lease.song_key;
    diagnostic.retained_proof_lifecycle_epoch =
        state.canonical_substrate_proof.lifecycle_state_epoch;
    diagnostic.retained_bridge_callback_sound = reinterpret_cast<uintptr_t>(
        state.canonical_substrate_proof.bridge.expected_callback_sound);
    diagnostic.retained_bridge_callback_index = state.canonical_substrate_proof
        .bridge.expected_callback_sound_identity.internal_index;
    diagnostic.retained_bridge_callback_serial = state.canonical_substrate_proof
        .bridge.expected_callback_sound_identity.serial_number;
    auto fail = [&]() noexcept -> AudioRouteArmLease {
        diagnostic.first_failure =
            first_selection_admission_commit_failure(diagnostic.facts);
        return {AudioRouteArmStatus::Failed, {}};
    };
    if (!diagnostic.facts.selection_guard_acquired
        || !diagnostic.facts.not_committed) {
        return fail();
    }
    try {
        if (!(diagnostic.facts.aggregate_lease_present =
                state.aggregate_lease != nullptr)) return fail();
        if (!(diagnostic.facts.aggregate_lease_active =
                state.aggregate_lease->active)) return fail();
        if (!(diagnostic.facts.exit_not_requested =
                !g_bgm_aggregate_exit_requested.load(std::memory_order_acquire))) {
            return fail();
        }
        if (!(diagnostic.facts.exit_not_pending =
                !g_bgm_aggregate_exit_pending.load(std::memory_order_acquire))) {
            return fail();
        }
        if (!(diagnostic.facts.selection_guard_current =
                registry().selection_guard_matches(
                    state.selection, state.selection_token))) return fail();
        diagnostic.facts.activation_revocation_epoch_exact =
            selection_activation_attempt_epoch_exact(
                state.substrate_phase_attempt
                    .substrate_phase_attempt_revocation_epoch,
                g_selection_activation_revocation_epoch.load(
                    std::memory_order_acquire));
        if (!diagnostic.facts.activation_revocation_epoch_exact) return fail();

        void* current_controller = lookup_current_bgm_controller();
        diagnostic.current_controller =
            reinterpret_cast<uintptr_t>(current_controller);
        if (!(diagnostic.facts.controller_pointer_exact =
                current_controller == state.controller)) return fail();
        diagnostic.facts.controller_identity_exact = !current_controller
            || controller_identity_proof_matches_live(
                state.controller_proof, current_controller);
        if (!diagnostic.facts.controller_identity_exact) return fail();

        diagnostic.facts.substrate_live_exact = true;
        diagnostic.facts.proof_match_exact = true;
        diagnostic.facts.proof_reservation_succeeded =
            !state.canonical_substrate_proof.active;
        diagnostic.facts.proof_predecessor_exact = true;
        if (state.canonical_substrate_proof.active) {
            diagnostic.facts.substrate_live_exact =
                canonical_substrate_active_sound_live_exact(
                    state.canonical_substrate_proof, current_controller,
                    state.controller_proof, &diagnostic.substrate_live);
            diagnostic.substrate_live_failure =
                first_selection_admission_substrate_live_failure(
                    diagnostic.substrate_live);
            if (!diagnostic.facts.substrate_live_exact) return fail();
        }

        // Impl already owns route-operation authority. Acquire reservation
        // ownership only after native identity reads, then retain it through
        // proof and route publication using the established operation ->
        // reservation -> borrower/audio order. All other epoch writers use
        // reservation or route-operation authority.
        std::unique_lock<std::mutex> reservation_epoch_lock(
            g_selection_activation_reservation_mutex);
        diagnostic.facts.activation_revocation_epoch_exact =
            selection_activation_commit_epoch_exact({
                g_selection_activation_reservation_machine.state()
                    == SelectionActivationReservationState::Consumed,
                g_selection_activation_reservation_machine.generation(),
                g_selection_activation_reservation.generation,
                state.activation_reservation_generation,
                g_selection_activation_reservation
                    .substrate_phase_attempt_revocation_epoch,
                state.substrate_phase_attempt
                    .substrate_phase_attempt_revocation_epoch,
                g_selection_activation_revocation_epoch.load(
                    std::memory_order_acquire)});
        if (!diagnostic.facts.activation_revocation_epoch_exact) return fail();

        bool canonical_substrate_proof_reserved = false;
        if (state.canonical_substrate_proof.active) {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            diagnostic.facts.activation_revocation_epoch_exact =
                selection_activation_attempt_epoch_exact(
                    state.substrate_phase_attempt
                        .substrate_phase_attempt_revocation_epoch,
                    g_selection_activation_revocation_epoch.load(
                        std::memory_order_acquire));
            if (!diagnostic.facts.activation_revocation_epoch_exact) return fail();
            diagnostic.proof_reservation_attempted = true;
            diagnostic.global_transaction_generation =
                g_bgm_canonical_substrate_proof.transaction_generation;
            diagnostic.global_source_ordinal =
                g_bgm_canonical_substrate_proof.source_ordinal;
            diagnostic.global_source_version =
                g_bgm_canonical_substrate_proof.source_version;
            diagnostic.global_collection_version =
                g_bgm_canonical_substrate_proof.source_collection_version;
            diagnostic.global_list_exit_epoch =
                g_bgm_canonical_substrate_proof.list_exit_epoch;
            diagnostic.global_bridge_generation =
                g_bgm_canonical_substrate_proof.bridge.generation;
            diagnostic.global_bridge_phase = static_cast<uint8_t>(
                g_bgm_canonical_substrate_proof.bridge.phase);
            diagnostic.global_bridge_release_epoch =
                g_bgm_canonical_substrate_proof.bridge.release_completion_epoch;
            diagnostic.global_canonical_token =
                g_bgm_canonical_substrate_proof.canonical;
            diagnostic.global_custom_token =
                g_bgm_canonical_substrate_proof.custom;
            diagnostic.global_proof_state = static_cast<uint8_t>(
                g_bgm_canonical_substrate_proof.state);
            diagnostic.global_proof_controller = reinterpret_cast<uintptr_t>(
                g_bgm_canonical_substrate_proof.controller);
            diagnostic.global_proof_slot = reinterpret_cast<uintptr_t>(
                g_bgm_canonical_substrate_proof.slot);
            diagnostic.global_proof_bgm = reinterpret_cast<uintptr_t>(
                g_bgm_canonical_substrate_proof.bgm);
            diagnostic.global_proof_sound = reinterpret_cast<uintptr_t>(
                g_bgm_canonical_substrate_proof.sound);
            diagnostic.global_proof_sound_index =
                g_bgm_canonical_substrate_proof.sound_identity.internal_index;
            diagnostic.global_proof_sound_serial =
                g_bgm_canonical_substrate_proof.sound_identity.serial_number;
            diagnostic.global_proof_request = g_bgm_canonical_substrate_proof.request;
            diagnostic.global_proof_route_generation =
                g_bgm_canonical_substrate_proof.route_generation;
            diagnostic.global_proof_lease_generation =
                g_bgm_canonical_substrate_proof.lease.generation;
            diagnostic.global_proof_lease_song =
                g_bgm_canonical_substrate_proof.lease.song_key;
            diagnostic.global_proof_lifecycle_epoch =
                g_bgm_canonical_substrate_proof.lifecycle_state_epoch;
            diagnostic.global_bridge_callback_sound = reinterpret_cast<uintptr_t>(
                g_bgm_canonical_substrate_proof.bridge.expected_callback_sound);
            diagnostic.global_bridge_callback_index = g_bgm_canonical_substrate_proof
                .bridge.expected_callback_sound_identity.internal_index;
            diagnostic.global_bridge_callback_serial = g_bgm_canonical_substrate_proof
                .bridge.expected_callback_sound_identity.serial_number;
            diagnostic.facts.proof_match_exact =
                canonical_substrate_proof_matches(
                    g_bgm_canonical_substrate_proof,
                    state.canonical_substrate_proof,
                    &diagnostic.proof_match);
            diagnostic.proof_match_failure =
                first_selection_admission_proof_match_failure(
                    diagnostic.proof_match);
            if (!diagnostic.facts.proof_match_exact) return fail();
            // Reserve the one-time proof before route publication. A later
            // failure leaves it unavailable and therefore fail-closed; the
            // canonical request itself is never mutated or cleared here.
            g_bgm_canonical_substrate_proof.active = false;
            canonical_substrate_proof_reserved = true;
            diagnostic.facts.proof_reservation_succeeded = true;
            diagnostic.proof_global_active_after_reservation =
                g_bgm_canonical_substrate_proof.active;
        }

        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            diagnostic.current_route_generation = g_audio_route_state.generation;
            diagnostic.current_lifecycle_epoch =
                g_onmemory_bank_lifecycle.state_epoch();
            diagnostic.facts.route_installed =
                g_audio_route_installed.load(std::memory_order_acquire);
            if (!diagnostic.facts.route_installed) return fail();
            diagnostic.facts.route_enabled =
                !g_audio_route_disabled.load(std::memory_order_acquire);
            if (!diagnostic.facts.route_enabled) return fail();
            diagnostic.facts.frozen_lease_inactive =
                !g_frozen_profile_lease.active();
            if (!diagnostic.facts.frozen_lease_inactive) return fail();
            diagnostic.facts.native_route_unowned =
                !native_audio_route_owned_locked();
            if (!diagnostic.facts.native_route_unowned) return fail();
            diagnostic.facts.list_cleanup_clear =
                !g_audio_route_state.list_cleanup_pending;
            if (!diagnostic.facts.list_cleanup_clear) return fail();
            diagnostic.facts.route_idle =
                g_audio_route_state.phase == AudioRoutePhase::Idle;
            if (!diagnostic.facts.route_idle) return fail();
            diagnostic.facts.route_generation_exact =
                g_audio_route_state.generation == state.route_generation_before;
            if (!diagnostic.facts.route_generation_exact) return fail();
            diagnostic.facts.lifecycle_epoch_exact =
                diagnostic.current_lifecycle_epoch == state.lifecycle_state_epoch;
            if (!diagnostic.facts.lifecycle_epoch_exact) return fail();
            if (state.canonical_substrate_proof.active) {
                diagnostic.facts.proof_predecessor_exact =
                    canonical_substrate_proof_reserved
                    && canonical_substrate_route_predecessor_exact(
                        canonical_substrate_route_use_facts(
                            state.canonical_substrate_proof,
                            g_audio_route_state.generation,
                            g_audio_route_state.lease_identity,
                            diagnostic.current_lifecycle_epoch,
                            g_canonical_substrate_reset_observation_generation,
                            g_canonical_substrate_reset_lineage_generation,
                            CanonicalSubstrateResetLineagePhase::Reserved,
                            CanonicalSubstrateBridgePhase::Reserved));
                if (!diagnostic.facts.proof_predecessor_exact) return fail();
            }
            const auto cleanup_record = g_onmemory_bank_cleanup_only.record();
            diagnostic.cleanup_only_phase = static_cast<uint8_t>(
                cleanup_record.phase);
            diagnostic.facts.cleanup_only_clear =
                !g_onmemory_bank_cleanup_only.blocks_custom_routes()
                && !g_custom_activation_quarantine;
            if (!diagnostic.facts.cleanup_only_clear) return fail();
            diagnostic.arm_decision = g_onmemory_bank_lifecycle.preflight_arm(
                true, diagnostic.facts.route_installed,
                g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
                g_onmemory_bank_release_available.load(std::memory_order_acquire));
            diagnostic.facts.lifecycle_preflight_allowed =
                diagnostic.arm_decision == OnMemoryBankRouteDecision::Allowed;
            if (!diagnostic.facts.lifecycle_preflight_allowed) return fail();
            diagnostic.facts.sidecar_pointer_exact =
                find_ready_sidecar_locked(*state.selection.song) == state.sidecar;
            if (!diagnostic.facts.sidecar_pointer_exact) return fail();
            diagnostic.facts.sidecar_header_exact =
                state.sidecar->allocation.sead_header() == state.sidecar_header;
            if (!diagnostic.facts.sidecar_header_exact) return fail();
            diagnostic.facts.sidecar_allocation_exact =
                state.sidecar->allocation.allocation_size()
                    == state.sidecar_allocation_size;
            if (!diagnostic.facts.sidecar_allocation_exact) return fail();
            diagnostic.facts.sidecar_mabf_exact =
                state.sidecar->allocation.mabf_size() == state.sidecar_mabf_size;
            if (!diagnostic.facts.sidecar_mabf_exact) return fail();

            diagnostic.final_coordinator = {
                    true,
                    state.aggregate_lease && state.aggregate_lease->active,
                    true,
                    true,
                    true,
                    true,
                    true,
                    true,
                    !g_bgm_aggregate_exit_requested.load(
                         std::memory_order_acquire)
                        && !g_bgm_aggregate_exit_pending.load(
                            std::memory_order_acquire),
                };
            diagnostic.final_coordinator_failure =
                first_chart_audio_admission_failure(
                    diagnostic.final_coordinator);
            diagnostic.facts.final_coordinator_exact =
                diagnostic.final_coordinator_failure
                    == ChartAudioAdmissionFailure::None;
            if (!diagnostic.facts.final_coordinator_exact) return fail();

            diagnostic.facts.activation_revocation_epoch_exact =
                selection_activation_attempt_epoch_exact(
                    state.substrate_phase_attempt
                        .substrate_phase_attempt_revocation_epoch,
                    g_selection_activation_revocation_epoch.load(
                        std::memory_order_acquire));
            if (!diagnostic.facts.activation_revocation_epoch_exact) return fail();

            diagnostic.publication_reached = true;
            diagnostic.facts.publication_reached = true;
            const auto old_transition_state =
                capture_audio_route_transition_projection_locked();
            g_audio_route_state.generation = state.reserved_route_generation;
            g_audio_route_state.lease_identity = state.reserved_lease;
            g_audio_route_state.frozen_sound_patch = {};
            g_audio_route_state.stop_retirement = {};
            g_audio_route_state.stop_retirement_epoch = 0;
            g_audio_route_state.stop_retirement_backing = {};
            g_audio_route_state.phase = AudioRoutePhase::Armed;
            g_audio_route_state.desired_song_id = state.song_id;
            g_audio_route_state.controller = state.controller;
            g_audio_route_state.controller_identity = state.controller_identity;
            g_audio_route_state.controller_arm_proof_attempted = state.controller != nullptr;
            g_audio_route_state.controller_arm_attempted_proof = state.controller_proof;
            g_audio_route_state.controller_arm_bind_succeeded = false;
            g_frozen_profile_lease.acquire(state.reserved_lease);
            g_unpublished_audio_setup = {
                state.selection,
                state.selection_token,
            };
            g_unpublished_audio_setup.substrate_bridge = state.substrate_bridge;
            if (state.controller) {
                diagnostic.controller_bind_reached = true;
                g_audio_route_state.controller_arm_bind_succeeded =
                    g_unpublished_audio_setup.bind_route_controller(
                        state.controller_proof);
                diagnostic.controller_bind_succeeded =
                    g_audio_route_state.controller_arm_bind_succeeded;
                diagnostic.facts.controller_bind_exact =
                    diagnostic.controller_bind_succeeded;
                if (!g_audio_route_state.controller_arm_bind_succeeded) {
                    AudioRouteTransitionRecorder bind_failure_transition_record(
                        AudioRouteTransitionReason::AdmissionArmBindFailure,
                        AudioRouteTransitionKind::Combined);
                    g_unpublished_audio_setup.invalidate();
                    g_audio_route_state = {};
                    const auto cleanup = g_frozen_profile_lease.transition(
                        AudioRouteCleanupEvent::VerifiedNoRoute,
                        state.reserved_lease);
                    (void)cleanup;
                    return fail();
                }
            } else {
                diagnostic.facts.controller_bind_exact = true;
            }
            enqueue_audio_route_transition_locked(
                AudioRouteTransitionReason::AdmissionArmPublished,
                AudioRouteTransitionKind::Combined, old_transition_state);
        }

        if (canonical_substrate_proof_reserved) {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            if (!g_bgm_canonical_substrate_proof.active
                && g_bgm_canonical_substrate_proof.state
                    == BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished
                && g_bgm_canonical_substrate_proof.transaction_generation
                    == state.canonical_substrate_proof.transaction_generation) {
                g_bgm_canonical_substrate_proof = {};
            }
        }

        state.substrate_phase_attempt.transfer_state = committed_ownership.next;
        state.committed = true;
        state.selection_guard_acquired = false; // Ownership moved to g_unpublished_audio_setup.
        diagnostic.first_failure =
            first_selection_admission_commit_failure(diagnostic.facts);
        return {AudioRouteArmStatus::Armed, state.reserved_lease};
    } catch (...) {
        diagnostic.first_failure = SelectionAdmissionCommitFailure::Exception;
        return {AudioRouteArmStatus::Failed, {}};
    }
}

AudioRouteLeaseIdentity selection_audio_admission_lease(
    const SelectionAudioAdmission& admission) noexcept
{
    return admission.impl_ ? admission.impl_->reserved_lease
                           : AudioRouteLeaseIdentity{};
}

bool capture_selection_audio_admission_diagnostic(
    const SelectionAudioAdmission& admission,
    ChartAudioDiagnosticPrewriteSnapshot& out) noexcept
{
    out = {};
    if (!admission.impl_) return false;
    try {
        const auto& state = *admission.impl_;
        out.attempted = true;
        out.selection_generation = state.selection.generation;
        out.route_generation = state.reserved_route_generation;
        out.lease_generation = state.reserved_lease.generation;
        out.song_key = state.reserved_lease.song_key;
        out.controller = reinterpret_cast<uintptr_t>(state.controller);

        UObjectIdentity current_identity{};
        ControllerIdentityProof current_proof{};
        if (state.controller
            && read_uobject_identity(state.controller, current_identity)) {
            current_proof = controller_identity_proof(
                state.controller, current_identity);
        }
        out.controller_proof.valid
            = controller_identity_proof_valid(current_proof);
        out.controller_proof.mode = current_proof.mode;
        out.controller_proof.raw_index_readable
            = current_proof.raw_internal_index_readable;
        out.controller_proof.raw_index = current_proof.raw_internal_index;
        out.controller_proof.live_capture_succeeded
            = current_proof.live_capture_succeeded;
        out.controller_proof.live_index
            = current_proof.live.internal_index;
        out.controller_proof.live_serial
            = current_proof.live.serial_number;
        out.controller_proof.item_capture_succeeded
            = current_proof.item_backed_zero_serial_capture_succeeded;
        out.controller_proof.item_index
            = current_proof.item_backed_zero_serial.internal_index;
        out.controller_proof.item_serial
            = current_proof.item_backed_zero_serial.serial_number;

        void* slot = nullptr;
        void* bgm = nullptr;
        void* sound = nullptr;
        uint64_t request = 0;
        uint8_t native_state = 0xff;
        out.chain_read = state.controller
            && read_controller_bgm_chain(state.controller, slot, bgm);
        out.slot = reinterpret_cast<uintptr_t>(slot);
        out.bgm = reinterpret_cast<uintptr_t>(bgm);
        out.sound_read = out.chain_read
            && core::safe_read_field(
                bgm, runtime_layouts::SqexSeadBgm::sound, sound);
        out.sound = reinterpret_cast<uintptr_t>(sound);
        out.request_read = out.sound_read
            && core::safe_read_field(
                bgm, runtime_layouts::SqexSeadBgm::request_handle, request);
        out.request = request;
        out.state_read = out.request_read
            && core::safe_read_field(
                slot, runtime_layouts::SqexSeadSlot::state, native_state);
        out.state = native_state;
        if (!(out.chain_read && out.sound_read && out.request_read
                && out.state_read)) {
            out.startup_mode = ChartAudioDiagnosticStartupMode::Unreadable;
        } else if (!sound && request == 0 && native_state == 0) {
            out.startup_mode = ChartAudioDiagnosticStartupMode::IdleNull;
        } else if (sound && request != 0 && native_state == 4) {
            out.startup_mode = ChartAudioDiagnosticStartupMode::ActiveSound;
        } else {
            out.startup_mode = ChartAudioDiagnosticStartupMode::InvalidMixed;
        }
        out.exact_admission = state.selection_guard_acquired
            && !state.committed
            && state.aggregate_lease && state.aggregate_lease->active
            && state.reserved_lease.valid()
            && controller_identity_proof_valid(current_proof)
            && controller_identity_proof_matches(
                state.controller_proof, current_proof);
        return true;
    } catch (...) {
        return false;
    }
}

bool cancel_selection_audio_admission(
    SelectionAudioAdmission& admission) noexcept
{
    try {
        if (!admission.impl_) return true;
        auto& state = *admission.impl_;
        if (!state.committed) {
            admission = {};
            return true;
        }

        bool cancelled = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const bool exact = chart_audio_committed_cancel_authority_exact({
                g_audio_route_state.phase == AudioRoutePhase::Armed
                    && g_audio_route_state.generation
                        == state.reserved_route_generation
                    && g_audio_route_state.lease_identity == state.reserved_lease
                    && g_audio_route_state.desired_song_id == state.song_id
                    && g_audio_route_state.controller == state.controller,
                g_frozen_profile_lease.active()
                    && g_frozen_profile_lease.identity() == state.reserved_lease,
                !g_frozen_profile_lease.native_arm_attempted(),
                g_unpublished_audio_setup
                    && g_unpublished_audio_setup.token == state.selection_token,
                g_onmemory_bank_lifecycle.state_epoch()
                    == state.lifecycle_state_epoch,
                !g_audio_route_state.custom_resource_owned
                    && !native_audio_route_owned_locked(),
            });
            if (exact) {
                const auto old_transition_state =
                    capture_audio_route_transition_projection_locked();
                const auto cleanup = g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::VerifiedNoRoute,
                    state.reserved_lease);
                if (cleanup.clear_route_metadata && cleanup.thaw_profile) {
                    invalidate_unpublished_audio_setup_locked(
                        AudioRouteTransitionReason::AdmissionCancel, false);
                    g_audio_route_state = {};
                    g_audio_route_state.generation
                        = state.reserved_route_generation;
                    g_audio_route_state.phase = AudioRoutePhase::Idle;
                    enqueue_audio_route_transition_locked(
                        AudioRouteTransitionReason::AdmissionCancel,
                        AudioRouteTransitionKind::Combined,
                        old_transition_state);
                    state.committed = false;
                    cancelled = true;
                }
            }
            if (!cancelled) {
                g_audio_route_disabled.store(true, std::memory_order_release);
                g_audio_route_state.list_cleanup_pending = true;
            }
        }
        if (cancelled) admission = {};
        return cancelled;
    } catch (...) {
        g_audio_route_disabled.store(true, std::memory_order_release);
        return false;
    }
}

bool selection_audio_admission_cancellation_complete(
    const SelectionSnapshot& selection,
    const AudioRouteLeaseIdentity& lease) noexcept
{
    try {
        if (!selection || !lease.valid()) return false;
        bool matching_unpublished = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_frozen_profile_lease.active()
                && g_frozen_profile_lease.identity() == lease) {
                return false;
            }
            if (g_audio_route_state.lease_identity == lease
                && g_audio_route_state.phase != AudioRoutePhase::Idle) {
                return false;
            }
            if (g_unpublished_audio_setup
                && g_unpublished_audio_setup.selection.generation
                    == selection.generation
                && g_unpublished_audio_setup.selection.song == selection.song
                && g_unpublished_audio_setup.token.lease_generation
                    == lease.generation
                && g_unpublished_audio_setup.token.song_key == lease.song_key) {
                matching_unpublished = true;
            }
        }
        if (matching_unpublished) return false;
        return true;
    } catch (...) {
        return false;
    }
}
