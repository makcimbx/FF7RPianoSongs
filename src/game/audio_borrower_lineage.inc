bool lookup_current_piano_audio_owner(void*& owner);

struct PlaybackTransferObservationContext final {
    bool exact = false;
    bool pre_session_candidate = false;
    bool canonical_stop_boundary = false;
    uint64_t parent_ordinal = 0;
    uint64_t parent_version = 0;
    uint64_t collection_version = 0;
    uint64_t boundary_route_generation = 0;
    uint64_t parent_canonical_command = 0;
    uint64_t anchored_custom_command = 0;
    uint64_t live_next_command = 0;
    const char* blocked_reason = "not_custom_context";
    const char* first_failed_predicate = "none";
    BgmPlaybackBorrowerRecord record{};
};

const char* canonical_parent_failure_name(
    BgmPlaybackCanonicalParentFailure failure) noexcept
{
    switch (failure) {
    case BgmPlaybackCanonicalParentFailure::None: return "none";
    case BgmPlaybackCanonicalParentFailure::ParentInactive: return "parent_active";
    case BgmPlaybackCanonicalParentFailure::ParentUnanchored: return "parent_anchored";
    case BgmPlaybackCanonicalParentFailure::CanonicalPlayUnconfirmed: return "canonical_play_confirmed";
    case BgmPlaybackCanonicalParentFailure::CanonicalHandleMismatch: return "canonical_handle_exact";
    case BgmPlaybackCanonicalParentFailure::CanonicalSoundPointerMismatch: return "canonical_sound_pointer_exact";
    case BgmPlaybackCanonicalParentFailure::CanonicalSoundIdentityMismatch: return "canonical_sound_identity_exact";
    case BgmPlaybackCanonicalParentFailure::ControllerIdentityMismatch: return "controller_identity_exact";
    case BgmPlaybackCanonicalParentFailure::LiveSlotBgmMismatch: return "live_slot_bgm_exact";
    case BgmPlaybackCanonicalParentFailure::OwnerThreadMismatch: return "owner_thread_exact";
    case BgmPlaybackCanonicalParentFailure::OwnerCommandMismatch: return "owner_command_exact";
    case BgmPlaybackCanonicalParentFailure::RouteSuccessorMismatch: return "route_successor_exact";
    case BgmPlaybackCanonicalParentFailure::LeaseLifecycleTokenMismatch: return "lease_lifecycle_tokens_exact";
    case BgmPlaybackCanonicalParentFailure::VersionOrBoundaryReuse: return "version_or_boundary_clear";
    }
    return "unknown";
}

const char* set_commit_failure_name(BgmPlaybackSetCommitFailure failure) noexcept
{
    switch (failure) {
    case BgmPlaybackSetCommitFailure::None: return "none";
    case BgmPlaybackSetCommitFailure::TargetMissing: return "target_present";
    case BgmPlaybackSetCommitFailure::RecordFailed: return "record_live";
    case BgmPlaybackSetCommitFailure::RecordVersion: return "record_version_exact";
    case BgmPlaybackSetCommitFailure::CollectionVersion: return "collection_version_exact";
    case BgmPlaybackSetCommitFailure::Owner: return "owner_exact";
    case BgmPlaybackSetCommitFailure::Thread: return "thread_exact";
    case BgmPlaybackSetCommitFailure::Command: return "command_exact";
    case BgmPlaybackSetCommitFailure::PostSetSnapshot: return "post_set_snapshot_exact";
    case BgmPlaybackSetCommitFailure::BoundaryState: return "boundary_state_exact";
    case BgmPlaybackSetCommitFailure::RequestGeneration: return "request_generation_exact";
    case BgmPlaybackSetCommitFailure::AppliedState: return "applied_state_exact";
    case BgmPlaybackSetCommitFailure::Nonce: return "nonce_exact";
    }
    return "unknown";
}

const char* play_failure_name(BgmPlaybackPlayFailure failure) noexcept
{
    switch (failure) {
    case BgmPlaybackPlayFailure::None: return "none";
    case BgmPlaybackPlayFailure::PendingSet: return "pending_set";
    case BgmPlaybackPlayFailure::PublicationAuthority: return "publication_authority_exact";
    case BgmPlaybackPlayFailure::ControllerChain: return "controller_chain_exact";
    case BgmPlaybackPlayFailure::OwnerThread: return "owner_thread_exact";
    case BgmPlaybackPlayFailure::Command: return "command_exact";
    case BgmPlaybackPlayFailure::Route: return "route_exact";
    case BgmPlaybackPlayFailure::Lease: return "lease_exact";
    case BgmPlaybackPlayFailure::LifecycleState: return "lifecycle_state_exact";
    case BgmPlaybackPlayFailure::TokenOrdinal: return "token_ordinal_exact";
    case BgmPlaybackPlayFailure::TokenValues: return "token_values_exact";
    case BgmPlaybackPlayFailure::RequestSoundIdentity: return "request_sound_identity_exact";
    case BgmPlaybackPlayFailure::RequestHandle: return "request_handle_exact";
    case BgmPlaybackPlayFailure::State4: return "state4";
    case BgmPlaybackPlayFailure::Nonce: return "nonce_exact";
    }
    return "unknown";
}

bool read_playback_transfer_snapshot(
    void* object, BgmPlaybackTransferSnapshot& out) noexcept
{
    out = {};
    if (!object) return false;
    out.readable = core::safe_read_field(object, 0x28, out.sound)
        && core::safe_read_field(object, 0x48, out.moved_handle)
        && core::safe_read_field(object, 0x50, out.index50)
        && core::safe_read_field(object, 0x54, out.index54)
        && core::safe_read_field(object, 0x68, out.metadata68)
        && core::safe_read_field(object, 0x6c, out.metadata6c)
        && core::safe_read_field(object, 0x70, out.optional_token);
    return out.readable;
}

bool canonical_boundary_destination_exact(
    const BgmPlaybackBorrowerRecord& record) noexcept
{
    try {
        void* data = nullptr;
        int32_t count = 0;
        int32_t capacity = 0;
        bool vector_readable = core::safe_read_field(record.slot, 0xc8, data)
            && core::safe_read_field(record.slot, 0xd0, count)
            && core::safe_read_field(record.slot, 0xd4, capacity)
            && count >= 0 && capacity >= count && capacity <= 256
            && (count == 0 || data);
        bool member_present = false;
        bool pointer_exact = false;
        bool handle_exact = false;
        bool sound_exact = false;
        bool identity_exact = false;
        bool destination_reused = false;
        if (vector_readable) {
            for (int32_t i = 0; i < count; ++i) {
                void* entry = nullptr;
                uint64_t handle = 0;
                void* sound = nullptr;
                if (!core::safe_read_field(data,
                        static_cast<size_t>(i) * sizeof(void*), entry)
                    || !entry
                    || !core::safe_read_field(entry,
                        runtime_layouts::SqexSeadBgm::request_handle, handle)
                    || !core::safe_read_field(entry,
                        runtime_layouts::SqexSeadBgm::sound, sound)) {
                    vector_readable = false;
                    break;
                }
                if (entry != record.canonical_stop_boundary_destination) continue;
                member_present = true;
                pointer_exact = true;
                handle_exact = handle
                    == record.canonical_stop_boundary_handle;
                sound_exact = sound
                    == record.canonical_stop_boundary_sound;
                destination_reused = !handle_exact || !sound_exact;
                UObjectIdentity current{};
                identity_exact = sound_exact
                    && read_uobject_identity(sound, current)
                    && current.live.serial_number > 0
                    && validate_live_uobject_handle(sound, current.live)
                    && current.live.internal_index
                        == record.canonical_stop_boundary_sound_identity.internal_index
                    && current.live.serial_number
                        == record.canonical_stop_boundary_sound_identity.serial_number;
                break;
            }
        }
        return bgm_playback_boundary_destination_exact(
            vector_readable, member_present, pointer_exact, handle_exact,
            sound_exact, identity_exact, destination_reused);
    } catch (...) {
        return false;
    }
}

PlaybackTransferObservationContext snapshot_playback_transfer_context(
    const BgmPlaybackTransferSnapshot& source_before) noexcept
{
    PlaybackTransferObservationContext result;
    try {
        if (source_before.readable && source_before.sound
            && source_before.moved_handle) {
            UObjectIdentity source_identity{};
            void* current_owner = nullptr;
            uint64_t current_command = 0;
            uint64_t route_generation = 0;
            AudioRouteLeaseIdentity lease;
            void* route_slot = nullptr;
            void* route_bgm = nullptr;
            OnMemoryBankDetachedRecord lifecycle;
            uint64_t lifecycle_state_epoch = 0;
            {
                std::lock_guard<std::mutex> state_lock(g_audio_state_mutex);
                route_generation = g_audio_route_state.generation;
                lease = g_audio_route_state.lease_identity;
                route_slot = g_audio_route_state.owned_slot;
                route_bgm = g_audio_route_state.owned_bgm;
                lifecycle = g_onmemory_bank_lifecycle.active();
                lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
            }
            const bool source_identity_positive = read_uobject_identity(
                    reinterpret_cast<void*>(source_before.sound), source_identity)
                && source_identity.live.serial_number > 0
                && validate_live_uobject_handle(
                    reinterpret_cast<void*>(source_before.sound),
                    source_identity.live);
            const bool owner_command_read = lookup_current_piano_audio_owner(
                    current_owner)
                && current_owner
                && core::safe_read_field(current_owner,
                    runtime_layouts::PianoAudioOwner::packed_key,
                    current_command)
                && current_command != 0;
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            BgmPlaybackBorrowerRecord* candidate = nullptr;
            bool handle_candidate = false;
            for (auto& record : g_bgm_playback_borrowers) {
                if (record.canonical_intermediate_handle
                    != source_before.moved_handle) continue;
                handle_candidate = true;
                void* live_slot = nullptr;
                void* live_bgm = nullptr;
                const bool live_chain_read = read_controller_bgm_chain(
                    record.controller, live_slot, live_bgm);
                const BgmPlaybackCanonicalParentLookupFacts facts{
                    record.active && !record.failed,
                    record.retirement_anchored,
                    record.transition_play_epoch != 0,
                    record.canonical_intermediate_handle
                        == source_before.moved_handle,
                    record.expected_new_sound
                        == reinterpret_cast<void*>(source_before.sound),
                    source_identity_positive
                        && record.expected_new_sound_identity.internal_index
                            == source_identity.live.internal_index
                        && record.expected_new_sound_identity.serial_number
                            == source_identity.live.serial_number,
                    controller_identity_proof_matches_live(
                        record.controller_proof, record.controller),
                    live_chain_read && record.slot == live_slot
                        && record.bgm == live_bgm,
                    record.owner == current_owner
                        && record.owner_thread_id == GetCurrentThreadId(),
                    owner_command_read
                        && bgm_playback_canonical_to_custom_command_edge_exact(
                            record.anchored_custom_command,
                            record.transition_command_key,
                            current_command)
                        && record.transition_command_key == record.desired_key,
                    route_generation != 0
                        && route_generation == record.transition_route_generation,
                    lease == record.lease
                        && lifecycle
                        && lifecycle.ordinal == record.token_epoch
                        && lifecycle_state_epoch == record.anchor_state_epoch
                        && lifecycle.canonical.encode() == record.canonical
                        && lifecycle.custom.encode() == record.custom,
                    !record.canonical_stop_boundary_active
                        && !record.canonical_stop_boundary_consumed,
                };
                const auto failure = bgm_playback_canonical_parent_first_failure(facts);
                if (result.parent_ordinal == 0 || record.ordinal > result.parent_ordinal) {
                    result.parent_ordinal = record.ordinal;
                    result.parent_version = record.version;
                    result.parent_canonical_command =
                        record.transition_command_key;
                    result.anchored_custom_command =
                        record.anchored_custom_command;
                    result.live_next_command = current_command;
                    result.first_failed_predicate = canonical_parent_failure_name(failure);
                }
                if (failure != BgmPlaybackCanonicalParentFailure::None) continue;
                if (!candidate || record.ordinal > candidate->ordinal) {
                    candidate = &record;
                }
            }
            if (candidate) {
                result.exact = true;
                result.canonical_stop_boundary = true;
                result.parent_ordinal = candidate->ordinal;
                result.parent_version = candidate->version;
                result.collection_version = g_bgm_playback_collection_version;
                result.boundary_route_generation = route_generation;
                result.blocked_reason = "ready";
                result.first_failed_predicate = "none";
                return result;
            }
            if (handle_candidate) {
                result.canonical_stop_boundary = true;
                result.blocked_reason = !source_identity_positive
                    ? "canonical_identity" : "canonical_context";
                return result;
            }
        }
        auto& out = result.record;
        UObjectIdentity controller_identity{};
        OnMemoryBankDetachedRecord lifecycle{};
        bool route_owned = false;
        bool cleanup_pending = false;
        bool release_in_flight = false;
        bool lifecycle_failed = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            out.controller = g_audio_route_state.controller;
            controller_identity = g_audio_route_state.controller_identity;
            out.slot = g_audio_route_state.owned_slot;
            out.bgm = g_audio_route_state.owned_bgm;
            out.sound = g_audio_route_state.owned_sound;
            out.sound_identity = g_audio_route_state.owned_sound_identity.live;
            out.route_generation = g_audio_route_state.generation;
            out.lease = g_audio_route_state.lease_identity;
            route_owned = g_audio_route_state.custom_resource_owned;
            cleanup_pending = g_audio_route_state.list_cleanup_pending;
            out.session_epoch = g_pause_resume_bank.session_epoch;
            out.cycle_epoch = g_pause_resume_bank.cycle_epoch;
            lifecycle = g_onmemory_bank_lifecycle.active();
            out.lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
            release_in_flight = g_onmemory_bank_lifecycle.release_in_flight();
            lifecycle_failed = g_onmemory_bank_lifecycle.failed();
        }
        result.pre_session_candidate = out.session_epoch == 0
            && out.cycle_epoch == 0;
        if (!lifecycle) {
            result.blocked_reason = "lifecycle_missing";
            return result;
        }
        out.canonical = lifecycle.canonical.encode();
        out.custom = lifecycle.custom.encode();
        out.token_epoch = lifecycle.ordinal;
        out.owner = g_piano_audio_owner_tick;
        out.owner_thread_id = GetCurrentThreadId();
        void* current_owner = nullptr;
        const bool owner_exact = out.owner && g_piano_audio_owner_tick_nonce != 0
            && lookup_current_piano_audio_owner(current_owner)
            && current_owner == out.owner;
        bool command_exact = false;
        if (owner_exact) {
            command_exact = core::safe_read_field(out.owner,
                    kPianoAudioOwnerActiveKeyOffset,
                    out.anchored_custom_command)
                && out.anchored_custom_command != 0
                && core::safe_read_field(out.owner,
                runtime_layouts::PianoAudioOwner::packed_key, out.desired_key)
                && out.desired_key != 0
                && out.desired_key != out.anchored_custom_command;
        }
        out.controller_proof = controller_identity_proof(
            out.controller, controller_identity);
        const bool lifecycle_tokens_exact =
            exact_distinct_type1_onmemory_tokens(
                lifecycle.canonical, lifecycle.custom);
        const bool sound_exact = lifecycle.sound.object == out.sound
            && lifecycle.sound.live.internal_index
                == out.sound_identity.internal_index
            && lifecycle.sound.live.serial_number
                == out.sound_identity.serial_number
            && out.sound && out.sound_identity.serial_number > 0
            && validate_live_uobject_handle(out.sound, out.sound_identity);
        const BgmPlaybackLineageAdmissionFacts admission{
            out.lease.valid() && out.route_generation != 0
                && out.slot && out.bgm,
            route_owned && cleanup_pending,
            lifecycle.phase == OnMemoryBankLifecyclePhase::RestoreApplied,
            lifecycle_tokens_exact,
            owner_exact,
            command_exact,
            controller_identity_proof_matches_live(
                out.controller_proof, out.controller),
            sound_exact,
            !release_in_flight,
            !lifecycle_failed,
            out.session_epoch,
            out.cycle_epoch,
        };
        out.provenance = classify_bgm_playback_lineage_admission(admission);
        result.exact = out.provenance != BgmPlaybackLineageProvenance::Invalid;
        if (result.exact) result.blocked_reason = "admitted";
        else if (!admission.route_lease_exact) result.blocked_reason = "route_lease";
        else if (!admission.custom_cleanup_owned) result.blocked_reason = "custom_cleanup";
        else if (!admission.lifecycle_restore_applied) result.blocked_reason = "lifecycle_phase";
        else if (!admission.lifecycle_tokens_exact) result.blocked_reason = "lifecycle_tokens";
        else if (!admission.owner_thread_exact) result.blocked_reason = "owner_thread";
        else if (!admission.command_exact) result.blocked_reason = "owner_command";
        else if (!admission.controller_exact) result.blocked_reason = "controller";
        else if (!admission.sound_live_exact) result.blocked_reason = "sound_identity";
        else if (!admission.release_clear) result.blocked_reason = "release_in_flight";
        else if (!admission.failure_clear) result.blocked_reason = "lifecycle_failed";
        else result.blocked_reason = "partial_session_epoch";
    } catch (...) {
        result = {};
    }
    return result;
}

const char* bgm_playback_aggregate_marker_name(
    BgmPlaybackAggregateMarker marker) noexcept
{
    switch (marker) {
    case BgmPlaybackAggregateMarker::FinalActiveBorrower:
        return "final_active_borrower";
    case BgmPlaybackAggregateMarker::ReleaseBlockedBorrowers:
        return "release_blocked_borrowers";
    case BgmPlaybackAggregateMarker::ReleaseCommittedBorrowers:
        return "release_committed_borrowers";
    case BgmPlaybackAggregateMarker::AggregateClosed:
        return "aggregate_closed";
    case BgmPlaybackAggregateMarker::ListExitPending:
        return "list_exit_pending";
    case BgmPlaybackAggregateMarker::ListExitClosed:
        return "list_exit_closed";
    case BgmPlaybackAggregateMarker::ShutdownRetained:
        return "shutdown_retained";
    case BgmPlaybackAggregateMarker::ShutdownClosed:
        return "shutdown_closed";
    default: return "invalid";
    }
}

void log_bgm_playback_aggregate(
    const BgmPlaybackBorrowerRecord& record,
    BgmPlaybackAggregateMarker marker, uint32_t borrowers) noexcept
{
    try {
        if (g_bgm_playback_observation_logs.fetch_add(
                1, std::memory_order_relaxed) >= 96) return;
        std::ostringstream out;
        out << "[audio_sead] playback_aggregate status="
            << bgm_playback_aggregate_marker_name(marker)
            << " ordinal=" << record.ordinal
            << " version=" << record.version
            << " observation_epoch=" << record.observation_epoch
            << " list_exit_epoch=" << record.list_exit_epoch
            << " shutdown_epoch=" << record.shutdown_epoch
            << " route_invalidation_epoch=" << record.route_invalidation_epoch
            << " failure_epoch=" << record.failure_epoch
            << " release_authorized_epoch=" << record.release_authorized_epoch
            << " release_committed_epoch=" << record.release_committed_epoch
            << " borrowers=" << borrowers
            // Do not infer whole-aggregate closure from a custom release. These
            // captured facts allow exact canonical-retention correlation without new native reads.
            << " canonical_substrate_relinquished=" << record.canonical_substrate_relinquished
            << " canonical_substrate_state="
            << static_cast<unsigned>(record.canonical_substrate_relinquishment_state)
            << " canonical_substrate_transaction_generation="
            << record.canonical_substrate_transaction_generation
            << " owner=0x" << std::hex << reinterpret_cast<uintptr_t>(record.owner)
            << " controller=0x" << reinterpret_cast<uintptr_t>(record.controller)
            << " slot=0x" << reinterpret_cast<uintptr_t>(record.slot)
            << " bgm=0x" << reinterpret_cast<uintptr_t>(record.bgm)
            << " source=0x" << reinterpret_cast<uintptr_t>(record.source)
            << " destination=0x" << reinterpret_cast<uintptr_t>(record.destination)
            << " sound=0x" << reinterpret_cast<uintptr_t>(record.sound)
            << " old=0x" << record.old_handle
            << " old_generation=0x" << (record.old_handle >> 32)
            << " new=0x" << record.new_handle
            << " new_generation=0x" << (record.new_handle >> 32)
            << " canonical_intermediate=0x"
            << record.canonical_intermediate_handle
            << " latest_custom_transition=0x"
            << record.latest_custom_transition_handle
            << " command=0x" << record.desired_key << std::dec
            << " sound_serial=" << record.sound_identity.serial_number
            << " expected_new_sound_serial="
            << record.expected_new_sound_identity.serial_number
            << " session=" << record.session_epoch
            << " cycle=" << record.cycle_epoch
            << " correlated_session=" << record.correlated_session_epoch
            << " correlated_cycle=" << record.correlated_cycle_epoch
            << " correlation_epoch=" << record.session_correlation_epoch
            << " provenance="
            << (record.provenance == BgmPlaybackLineageProvenance::PreSession
                ? "pre_session" : "session_scoped")
            << " lifecycle_state_epoch=" << record.lifecycle_state_epoch
            << " token_epoch=" << record.token_epoch
            << " retirement_anchored=" << (record.retirement_anchored ? 1 : 0)
            << " anchor_state_epoch=" << record.anchor_state_epoch
            << " anchor_route=" << record.anchor_route_generation
            << " anchor_cleanup=" << record.anchor_cleanup_generation
            << " anchor_request=0x" << std::hex << record.anchor_request_handle
            << std::dec
            << " mutation_authorized=0";
        core::log(marker == BgmPlaybackAggregateMarker::Invalid
                || marker == BgmPlaybackAggregateMarker::ReleaseBlockedBorrowers
                || marker == BgmPlaybackAggregateMarker::ReleaseCommittedBorrowers
            ? core::LogLevel::Error : core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

void try_finish_bgm_aggregate_release(bool exit_authority) noexcept;
void try_claim_bgm_aggregate_exit_release() noexcept;
void defer_bgm_aggregate_release(
    const OnMemoryBankReleaseAction& action) noexcept;
void complete_bgm_aggregate_exit_cleanup(
    const OnMemoryBankDetachedRecord& lifecycle) noexcept;
bool same_bgm_terminal_domain(
    const BgmPlaybackBorrowerRecord& left,
    const BgmPlaybackBorrowerRecord& right) noexcept;
void observe_bgm_aggregate_closed_terminal_handoff(
    const BgmPlaybackBorrowerRecord& trigger,
    uint64_t collection_version,
    const std::array<BgmPlaybackBorrowerRecord, 32>& records,
    uint8_t record_count) noexcept;
BgmPlaybackAggregateTerminalStopDiagnostic
observe_bgm_aggregate_terminal_handoff_at_stop(
    void* controller, bool chain_read, void* slot, void* bgm,
    void* sound, uint64_t request, uint8_t state) noexcept;
void finish_bgm_aggregate_terminal_handoff_after_stop(
    BgmPlaybackAggregateTerminalStopDiagnostic& diagnostic,
    uint32_t native_call_count, bool post_observation_ran,
    bool post_observation_succeeded, void* controller, void* slot,
    void* bgm, void* sound, uint64_t request, uint8_t state) noexcept;
struct StopRetirementRead;
bool commit_audio_cleanup_after_retirement(
    const AudioStopRetirementState& retirement,
    const StopRetirementRead& retirement_read,
    uint64_t retirement_epoch,
    const OnMemoryBankRetiredBackingEvidence& retired_backing_evidence,
    OnMemoryBankDiagnosticPair* post_retirement_diagnostic,
    OnMemoryBankReleaseAction* post_retirement_release,
    PauseResumeBankHoldProbe* pause_hold_probe,
    const AudioRetirementRouteAuthorityFacts* terminal_route_authority);

const char* confirmed_custom_refresh_failure_name(
    const BgmPlaybackConfirmedCustomRefreshFailure failure) noexcept
{
    switch (failure) {
    case BgmPlaybackConfirmedCustomRefreshFailure::None: return "none";
    case BgmPlaybackConfirmedCustomRefreshFailure::PlayUnconfirmed: return "play_unconfirmed";
    case BgmPlaybackConfirmedCustomRefreshFailure::PendingOrUnresolvedProvenance: return "pending_or_unresolved_provenance";
    case BgmPlaybackConfirmedCustomRefreshFailure::HandleMismatch: return "handle_mismatch";
    case BgmPlaybackConfirmedCustomRefreshFailure::SoundNotLive: return "sound_not_live";
    case BgmPlaybackConfirmedCustomRefreshFailure::SoundPointerMismatch: return "sound_pointer_mismatch";
    case BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentityNotPositive: return "sound_identity_not_positive";
    case BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentityIndexMismatch: return "sound_identity_index_mismatch";
    case BgmPlaybackConfirmedCustomRefreshFailure::SoundIdentitySerialMismatch: return "sound_identity_serial_mismatch";
    case BgmPlaybackConfirmedCustomRefreshFailure::StateNotPlaying: return "state_not_playing";
    case BgmPlaybackConfirmedCustomRefreshFailure::ControllerSlotBgmMismatch: return "controller_slot_bgm_mismatch";
    case BgmPlaybackConfirmedCustomRefreshFailure::PlayOwnerProofMissing: return "play_owner_proof_missing";
    case BgmPlaybackConfirmedCustomRefreshFailure::OwnerBindingMismatch: return "owner_binding_mismatch";
    case BgmPlaybackConfirmedCustomRefreshFailure::EpochContinuityMismatch: return "epoch_continuity_mismatch";
    case BgmPlaybackConfirmedCustomRefreshFailure::RouteLeaseMismatch: return "route_lease_mismatch";
    case BgmPlaybackConfirmedCustomRefreshFailure::LifecycleTokenMismatch: return "lifecycle_token_mismatch";
    }
    return "unknown";
}

void observe_bgm_playback_aggregate(bool list_exit, bool shutdown) noexcept
{
    try {
        publish_bgm_aggregate_exit(list_exit, shutdown);
        std::array<BgmPlaybackBorrowerRecord, 32> snapshot{};
        uint64_t snapshot_collection_version = 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            snapshot = g_bgm_playback_borrowers;
            snapshot_collection_version = g_bgm_playback_collection_version;
        }
        auto refreshed = snapshot;
        std::array<BgmPlaybackConfirmedCustomRefreshDiagnostic, 32>
            refresh_diagnostics{};
        for (auto& child : refreshed) {
            if (!bgm_playback_aggregate_callback_match_eligible(child.active,
                    child.failed, child.terminal_callback_retired)
                || child.retirement_anchored) continue;
            for (size_t parent_index = 0; parent_index < refreshed.size();
                 ++parent_index) {
                auto& parent = refreshed[parent_index];
                if (!bgm_playback_aggregate_callback_match_eligible(parent.active,
                        parent.failed, parent.terminal_callback_retired)) continue;
                const uint64_t lineage_route_predecessor =
                    bgm_playback_descendant_route_predecessor(
                        parent.canonical_stop_boundary_route_generation,
                        parent.latest_custom_transition_route_generation,
                        child.route_generation);
                if (lineage_route_predecessor == 0
                    || !bgm_playback_lineage_inheritance_exact({
                        parent.retirement_anchored,
                        parent.custom == child.custom
                            && parent.token_epoch == child.token_epoch,
                        parent.anchor_state_epoch
                            == child.lifecycle_state_epoch,
                        parent.lease == child.lease,
                        parent.controller == child.controller
                            && parent.slot == child.slot
                            && parent.bgm == child.bgm,
                        bgm_playback_child_claim_exact(
                            parent.latest_custom_transition_consumed,
                            parent.ordinal, child.ordinal,
                            parent.latest_custom_transition_handle,
                            child.old_handle),
                        parent.latest_custom_transition_sound == child.sound
                            && parent.latest_custom_transition_sound_identity.internal_index
                                == child.sound_identity.internal_index
                            && parent.latest_custom_transition_sound_identity.serial_number
                                == child.sound_identity.serial_number,
                        bgm_playback_alternating_transition_exact(
                            parent.old_handle,
                            parent.canonical_intermediate_handle,
                            parent.latest_custom_transition_handle,
                            parent.expected_new_sound
                                && parent.expected_new_sound_identity.serial_number > 0,
                            parent.latest_custom_transition_sound == child.sound
                                && parent.latest_custom_transition_sound_identity.internal_index
                                    == child.sound_identity.internal_index
                                && parent.latest_custom_transition_sound_identity.serial_number
                                    == child.sound_identity.serial_number)})) continue;
                child.retirement_anchored = true;
                child.anchor_state_epoch = parent.anchor_state_epoch;
                child.anchor_route_generation = parent.anchor_route_generation;
                child.lineage_route_predecessor_generation =
                    lineage_route_predecessor;
                child.anchor_cleanup_generation = parent.anchor_cleanup_generation;
                child.anchor_request_handle = parent.anchor_request_handle;
                child.operation_predecessor_nonce =
                    parent.transition_play_nonce;
                parent.latest_custom_transition_consumed = true;
                parent.latest_custom_transition_child_ordinal = child.ordinal;
                parent.terminal_state =
                    BgmPlaybackLineageTerminalState::SupersededPending;
                break;
            }
        }
        for (size_t record_index = 0; record_index < snapshot.size();
             ++record_index) {
            const auto record = refreshed[record_index];
            if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)) continue;
            bool vector_exact = false;
            bool old_present = false;
            bool old_identity_conflict = false;
            bool new_present_retired = false;
            bool canonical_present_retired = false;
            bool custom_present_retired = false;
            bool canonical_identity_conflict = false;
            bool custom_identity_conflict = false;
            bool destination_aba = false;
            int32_t count = 0;
            int32_t capacity = 0;
            void* data = nullptr;
            if (controller_identity_proof_matches_live(
                    record.controller_proof, record.controller)
                && core::safe_read_field(record.slot, 0xc8, data)
                && core::safe_read_field(record.slot, 0xd0, count)
                && core::safe_read_field(record.slot, 0xd4, capacity)
                && count >= 0 && capacity >= count && capacity <= 256
                && (count == 0 || data)) {
                vector_exact = true;
                for (int32_t i = 0; i < count; ++i) {
                    void* entry = nullptr;
                    uint64_t handle = 0;
                    void* sound = nullptr;
                    if (!core::safe_read_field(data,
                            static_cast<size_t>(i) * sizeof(void*), entry)
                        || !entry
                        || !core::safe_read_field(entry,
                            runtime_layouts::SqexSeadBgm::request_handle, handle)
                        || !core::safe_read_field(entry,
                            runtime_layouts::SqexSeadBgm::sound, sound)) {
                        vector_exact = false;
                        break;
                    }
                    if (handle == record.old_handle) {
                        if (bgm_playback_old_handle_identity_conflict(
                                record.old_handle,
                                reinterpret_cast<uintptr_t>(record.sound),
                                handle, reinterpret_cast<uintptr_t>(sound))) {
                            old_identity_conflict = true;
                        } else {
                            old_present = true;
                        }
                    }
                    if (record.new_handle != 0 && handle == record.new_handle) {
                        new_present_retired = true;
                    }
                    if (record.canonical_intermediate_handle != 0
                        && handle == record.canonical_intermediate_handle) {
                        canonical_present_retired = true;
                        void* expected_canonical_sound =
                            record.canonical_stop_boundary_sound;
                        if (!expected_canonical_sound
                            && record.latest_custom_transition_handle == 0) {
                            expected_canonical_sound = record.expected_new_sound;
                        }
                        canonical_identity_conflict = expected_canonical_sound
                            && sound != expected_canonical_sound;
                    }
                    if (record.latest_custom_transition_handle != 0
                        && handle == record.latest_custom_transition_handle) {
                        custom_present_retired = true;
                        custom_identity_conflict =
                            record.latest_custom_transition_sound
                            && sound != record.latest_custom_transition_sound;
                    }
                    if (entry == record.destination
                        && handle != record.old_handle && handle != 0) {
                        destination_aba = true;
                    }
                }
            }
            void* slot = nullptr;
            void* bgm = nullptr;
            void* sound = nullptr;
            uint64_t current_handle = 0;
            uint8_t state = 0;
            UObjectIdentity sound_identity{};
            const bool chain_exact = read_controller_audio_chain(
                    record.controller, slot, bgm, sound, current_handle, state)
                && slot == record.slot && bgm == record.bgm;
            const bool sound_live = chain_exact && sound
                && read_uobject_identity(sound, sound_identity)
                && sound_identity.live.serial_number > 0
                && validate_live_uobject_handle(sound, sound_identity.live);
            const bool expected_sound = sound_live && record.set_observed
                && sound == record.expected_new_sound
                && sound_identity.live.internal_index
                    == record.expected_new_sound_identity.internal_index
                && sound_identity.live.serial_number
                    == record.expected_new_sound_identity.serial_number;
            uint8_t owner_state = 0xff;
            uint64_t active_key = 0;
            void* current_owner = nullptr;
            const bool owner_exact = lookup_current_piano_audio_owner(current_owner)
                && current_owner == record.owner
                && GetCurrentThreadId() == record.owner_thread_id;
            const bool owner_state_read = core::safe_read_field(record.owner,
                runtime_layouts::PianoAudioOwner::state, owner_state);
            const bool active_key_read = core::safe_read_field(record.owner,
                runtime_layouts::PianoAudioOwner::packed_key, active_key);
            const bool command_ready =
                bgm_playback_owner_command_phase_exact(owner_exact,
                    owner_state_read, owner_state, active_key_read,
                    record.desired_key, active_key);
            const bool confirmed_custom_play_owner_proof_exact =
                bgm_playback_confirmed_custom_play_owner_proof_exact(
                    record.latest_custom_transition_owner_proof, record.owner,
                    record.owner_thread_id, record.desired_key,
                    record.anchored_custom_command,
                    record.latest_custom_transition_owner,
                    record.latest_custom_transition_owner_thread_id,
                    record.latest_custom_transition_owner_state,
                    record.latest_custom_transition_command);
            const bool confirmed_custom_owner_binding_exact =
                bgm_playback_confirmed_custom_owner_binding_exact(owner_exact,
                    record.owner, record.owner_thread_id,
                    record.latest_custom_transition_owner,
                    record.latest_custom_transition_owner_thread_id,
                    current_owner, GetCurrentThreadId());
            bool current_epochs_exact = false;
            bool route_metadata_exact = false;
            bool retirement_anchor_exact = false;
            bool lifecycle_tokens_exact = false;
            bool current_failure = false;
            uint64_t current_session_epoch = 0;
            uint64_t current_cycle_epoch = 0;
            {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                const auto lifecycle = g_onmemory_bank_lifecycle.active();
                current_session_epoch = g_pause_resume_bank.session_epoch;
                current_cycle_epoch = g_pause_resume_bank.cycle_epoch;
                const bool lifecycle_anchor_exact = lifecycle
                    && g_onmemory_bank_lifecycle.state_epoch()
                        == record.anchor_state_epoch
                    && lifecycle.ordinal == record.token_epoch
                    && lifecycle.route_generation
                        == record.anchor_route_generation
                    && lifecycle.cleanup_generation
                        == record.anchor_cleanup_generation
                    && lifecycle.canonical.encode() == record.canonical
                    && lifecycle.custom.encode() == record.custom
                    && lifecycle.sound.object == record.sound
                    && lifecycle.sound.live.internal_index
                        == record.sound_identity.internal_index
                    && lifecycle.sound.live.serial_number
                        == record.sound_identity.serial_number;
                lifecycle_tokens_exact = lifecycle_anchor_exact;
                route_metadata_exact = g_audio_route_state.generation
                        == record.route_generation
                    && g_audio_route_state.lease_identity == record.lease
                    && g_audio_route_state.controller == record.controller
                    && g_audio_route_state.owned_slot == record.slot
                    && g_audio_route_state.owned_bgm == record.bgm
                    && g_audio_route_state.owned_sound == record.sound
                    && controller_identity_proof_matches_live(
                        record.controller_proof, record.controller);
                const bool controller_sound_exact =
                    controller_identity_proof_matches_live(
                        record.controller_proof, record.controller)
                    && validate_live_uobject_handle(
                        record.sound, record.sound_identity);
                const bool persisted_identity_exact = record.controller
                    && record.sound && record.sound_identity.serial_number > 0;
                const bool zero_session_anchor = current_session_epoch == 0
                    && current_cycle_epoch == 0;
                const bool later_session_anchor = current_session_epoch != 0
                    && current_cycle_epoch != 0
                    && g_pause_resume_bank.detached.ordinal
                        == record.token_epoch
                    && g_pause_resume_bank.lifecycle_state_epoch
                        == record.anchor_state_epoch
                    && g_pause_resume_bank.detached.sound.object == record.sound
                    && g_pause_resume_bank.detached.sound.live.internal_index
                        == record.sound_identity.internal_index
                    && g_pause_resume_bank.detached.sound.live.serial_number
                        == record.sound_identity.serial_number
                    && g_pause_resume_bank.detached.canonical.encode()
                        == record.canonical
                    && g_pause_resume_bank.detached.custom.encode()
                        == record.custom
                    && g_pause_resume_bank.detached.route_generation
                        == record.anchor_route_generation
                    && g_pause_resume_bank.detached.cleanup_generation
                        == record.anchor_cleanup_generation
                    && g_pause_resume_bank.controller == record.controller
                    && g_pause_resume_bank.lease_identity == record.lease
                    && g_pause_resume_bank.slot == record.slot
                    && g_pause_resume_bank.bgm == record.bgm;
                const bool persisted_later_session =
                    (record.release_authorized_epoch != 0 || list_exit || shutdown)
                    && record.correlated_session_epoch != 0
                    && record.correlated_cycle_epoch != 0
                    && current_session_epoch == record.correlated_session_epoch
                    && current_cycle_epoch == record.correlated_cycle_epoch;
                retirement_anchor_exact = record.retirement_anchored
                    && record.anchor_state_epoch != 0
                    && record.anchor_route_generation != 0
                    && record.anchor_cleanup_generation != 0
                    && ((lifecycle_anchor_exact && controller_sound_exact)
                        || ((record.release_authorized_epoch != 0
                                || list_exit || shutdown)
                            && persisted_identity_exact))
                    && (zero_session_anchor || later_session_anchor
                        || persisted_later_session);
                current_epochs_exact = retirement_anchor_exact
                    && bgm_playback_session_correlation_exact({
                        record.provenance,
                        retirement_anchor_exact,
                        record.session_epoch, record.cycle_epoch,
                        record.correlated_session_epoch,
                        record.correlated_cycle_epoch,
                        current_session_epoch, current_cycle_epoch});
                current_failure = g_onmemory_bank_lifecycle.failed()
                    && record.release_authorized_epoch == 0;
            }
            bool confirmed_custom_route_metadata_exact = false;
            {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                confirmed_custom_route_metadata_exact =
                    bgm_playback_confirmed_custom_route_transition_exact(
                        record.lineage_route_predecessor_generation,
                        record.canonical_set_input_route_generation,
                        record.canonical_intermediate_route_generation,
                        record.canonical_stop_boundary_route_generation,
                        record.latest_custom_transition_route_generation)
                    && bgm_playback_confirmed_custom_lease_transition_exact(
                        record.lease, record.canonical_intermediate_lease,
                        record.latest_custom_transition_lease)
                    && g_audio_route_state.generation
                        == record.latest_custom_transition_route_generation
                    && g_audio_route_state.lease_identity
                        == record.latest_custom_transition_lease
                    && g_audio_route_state.controller == record.controller
                    && g_audio_route_state.owned_slot == record.slot
                    && g_audio_route_state.owned_bgm == record.bgm
                    && g_audio_route_state.owned_sound
                        == record.latest_custom_transition_sound
                    && controller_identity_proof_matches_live(
                        record.controller_proof, record.controller);
            }
            const bool current_candidate = current_epochs_exact
                && record.set_observed && chain_exact && current_handle != 0
                && current_handle != record.old_handle && state == 4
                && expected_sound;
            const bool unresolved_candidate =
                record.exit_unresolved_native_request
                && retirement_anchor_exact && chain_exact && expected_sound
                && AudioBgmRequestHandle{current_handle}.valid_bgm_request()
                && current_handle != record.old_handle
                && (state == 2 || state == 4);
            const BgmPlaybackConfirmedCustomRefreshFacts custom_refresh_facts{
                record.transition_play_epoch != 0
                    && record.transition_play_nonce != 0
                    && record.transition_set_custom
                    && record.latest_custom_transition_handle != 0,
                !record.transition_set_pending
                    && !record.exit_unresolved_native_request
                    && !record.canonical_stop_boundary_active,
                current_handle != 0
                    && current_handle == record.latest_custom_transition_handle,
                sound_live,
                sound && sound == record.latest_custom_transition_sound,
                sound_identity.live.serial_number > 0,
                sound_identity.live.internal_index
                    == record.latest_custom_transition_sound_identity.internal_index,
                sound_identity.live.serial_number
                    == record.latest_custom_transition_sound_identity.serial_number,
                state == 4,
                chain_exact && controller_identity_proof_matches_live(
                    record.controller_proof, record.controller),
                confirmed_custom_play_owner_proof_exact,
                confirmed_custom_owner_binding_exact,
                current_epochs_exact && record.continuity_exact,
                confirmed_custom_route_metadata_exact,
                lifecycle_tokens_exact,
            };
            const auto custom_refresh_failure =
                bgm_playback_confirmed_custom_refresh_first_failure(
                    custom_refresh_facts);
            const bool confirmed_custom_candidate =
                custom_refresh_failure
                    == BgmPlaybackConfirmedCustomRefreshFailure::None;
            const bool confirmed_custom_refresh_requires_validation =
                record.latest_custom_transition_handle != 0
                && !record.latest_custom_transition_consumed
                && record.transition_play_epoch != 0
                && record.transition_play_nonce != 0
                && chain_exact && current_handle != 0
                && current_handle != record.old_handle;
            const bool current_reconciliation_allowed =
                bgm_playback_lineage_current_reconciliation_allowed(
                    record.latest_custom_transition_consumed,
                    record.latest_custom_transition_child_ordinal);

            auto& updated = refreshed[record_index];
            updated.observation_epoch = ++g_bgm_playback_observation_epoch;
            updated.list_exit = updated.list_exit || list_exit;
            updated.shutdown = updated.shutdown || shutdown;
            if (list_exit && updated.list_exit_epoch == 0) {
                updated.list_exit_epoch = updated.observation_epoch;
            }
            if (shutdown && updated.shutdown_epoch == 0) {
                updated.shutdown_epoch = updated.observation_epoch;
            }
            if (!route_metadata_exact
                && updated.route_invalidation_epoch == 0) {
                updated.route_invalidation_epoch = updated.observation_epoch;
            }
            if (current_failure) {
                updated.failed = true;
                if (updated.failure_epoch == 0) {
                    updated.failure_epoch = updated.observation_epoch;
                }
            }
            if (current_epochs_exact
                && updated.provenance == BgmPlaybackLineageProvenance::PreSession
                && current_session_epoch != 0 && current_cycle_epoch != 0
                && updated.correlated_session_epoch == 0
                && updated.correlated_cycle_epoch == 0) {
                updated.correlated_session_epoch = current_session_epoch;
                updated.correlated_cycle_epoch = current_cycle_epoch;
                updated.session_correlation_epoch = updated.observation_epoch;
            }
            updated.old_retired_seen = updated.old_retired_seen || old_present;
            updated.old_identity_conflict = updated.old_identity_conflict
                || old_identity_conflict;
            if (updated.old_identity_conflict) {
                updated.old_absent = false;
                updated.failed = true;
                if (updated.failure_epoch == 0) {
                    updated.failure_epoch = updated.observation_epoch;
                }
            }
            if (updated.old_retired_seen && vector_exact && !old_present
                && !updated.old_identity_conflict) {
                updated.old_absent = true;
            }
            if (confirmed_custom_refresh_requires_validation
                && !confirmed_custom_candidate) {
                updated.failed = true;
                updated.terminal_state = BgmPlaybackLineageTerminalState::Failed;
                if (updated.failure_epoch == 0) {
                    updated.failure_epoch = updated.observation_epoch;
                }
                refresh_diagnostics[record_index] = {
                    true, custom_refresh_failure, updated.ordinal,
                    updated.new_handle,
                    updated.latest_custom_transition_handle, current_handle};
            }
            if (current_reconciliation_allowed
                && (current_candidate || confirmed_custom_candidate
                    || unresolved_candidate)) {
                if (updated.new_handle != 0
                    && updated.new_handle != current_handle) {
                    const bool expected_custom_successor =
                        confirmed_custom_candidate
                        && bgm_playback_expected_custom_successor(
                            current_handle, updated.new_handle,
                            updated.latest_custom_transition_handle,
                            true);
                    if (!expected_custom_successor) {
                        updated.failed = true;
                        updated.terminal_state =
                            BgmPlaybackLineageTerminalState::Failed;
                        if (updated.failure_epoch == 0) {
                            updated.failure_epoch = updated.observation_epoch;
                        }
                    }
                } else {
                    updated.new_handle = current_handle;
                    updated.new_ready = updated.new_ready
                        || (state == 4
                            && (confirmed_custom_candidate
                                    ? confirmed_custom_play_owner_proof_exact
                                        && confirmed_custom_owner_binding_exact
                                    : command_ready));
                    if (updated.exit_unresolved_native_request) {
                        updated.exit_unresolved_native_request = false;
                        updated.exit_unresolved_native_request_faulted = false;
                        updated.exit_unresolved_request_handle = 0;
                        updated.exit_unresolved_request_generation = 0;
                        updated.exit_unresolved_request_sound = nullptr;
                        updated.exit_unresolved_request_sound_identity = {};
                    }
                }
            }
            if (updated.new_handle != 0 && vector_exact && chain_exact
                && current_handle != updated.new_handle
                && !new_present_retired) {
                updated.new_absent = true;
            }
            if (updated.canonical_intermediate_handle != 0 && vector_exact
                && chain_exact
                && current_handle != updated.canonical_intermediate_handle
                && !canonical_present_retired) {
                updated.canonical_intermediate_absent = true;
            }
            if (updated.latest_custom_transition_handle != 0 && vector_exact
                && chain_exact
                && current_handle != updated.latest_custom_transition_handle
                && !custom_present_retired) {
                updated.latest_custom_transition_absent = true;
            }
            if (updated.exit_unresolved_native_request
                && updated.exit_unresolved_request_handle != 0
                && updated.new_handle == updated.exit_unresolved_request_handle
                && updated.new_absent) {
                updated.exit_unresolved_native_request = false;
                updated.exit_unresolved_native_request_faulted = false;
                updated.exit_unresolved_request_handle = 0;
                updated.exit_unresolved_request_generation = 0;
                updated.exit_unresolved_request_sound = nullptr;
                updated.exit_unresolved_request_sound_identity = {};
            }
            updated.destination_aba = updated.destination_aba || destination_aba;
            if (canonical_identity_conflict || custom_identity_conflict) {
                updated.failed = true;
                updated.lineage_identity_conflict = true;
                if (updated.failure_epoch == 0) {
                    updated.failure_epoch = updated.observation_epoch;
                }
            }
            updated.failed = updated.failed || updated.destination_aba;
            if (updated.destination_aba && updated.failure_epoch == 0) {
                updated.failure_epoch = updated.observation_epoch;
            }
            const bool relinquished_substrate_live_exact =
                updated.canonical_substrate_relinquished
                && chain_exact && state == 4
                && current_handle == updated.canonical_substrate_handle
                && sound == updated.canonical_substrate_sound
                && sound_live
                && sound_identity.live.internal_index
                    == updated.canonical_substrate_sound_identity.internal_index
                && sound_identity.live.serial_number
                    == updated.canonical_substrate_sound_identity.serial_number
                && slot == updated.slot && bgm == updated.bgm;
            if (updated.canonical_substrate_relinquished) {
                updated.canonical_substrate_current_exact =
                    relinquished_substrate_live_exact;
            }
            updated.current_active = !updated.canonical_substrate_relinquished
                && state == 4
                && ((updated.new_handle != 0
                        && current_handle == updated.new_handle)
                    || (updated.latest_custom_transition_handle != 0
                        && current_handle
                            == updated.latest_custom_transition_handle));
            const bool current_is_confirmed_custom =
                updated.latest_custom_transition_handle != 0
                && current_handle == updated.latest_custom_transition_handle
                && updated.transition_play_epoch != 0
                && updated.transition_play_nonce != 0;
            updated.current_command_ready = updated.current_active
                && updated.new_ready
                && (current_is_confirmed_custom
                        ? confirmed_custom_play_owner_proof_exact
                            && confirmed_custom_owner_binding_exact
                        : command_ready);
            updated.terminal_state = classify_bgm_playback_lineage_terminal({
                updated.failed,
                updated.latest_custom_transition_child_ordinal != 0,
                updated.old_absent,
                updated.canonical_intermediate_handle != 0
                    && updated.canonical_intermediate_absent,
                updated.latest_custom_transition_handle != 0
                    && updated.latest_custom_transition_absent,
                updated.current_active,
                updated.transition_set_pending,
                updated.canonical_stop_boundary_active,
                updated.exit_unresolved_native_request});
            if (updated.retirement_anchored) {
                updated.continuity_exact = updated.continuity_exact
                    && (current_epochs_exact || updated.list_exit || updated.shutdown
                        || updated.release_authorized_epoch != 0);
            }
            uint32_t blocked = 0;
            if (!updated.retirement_anchored) blocked |= 1u << 0;
            if (!vector_exact) blocked |= 1u << 1;
            if (!updated.old_retired_seen) blocked |= 1u << 2;
            if (!updated.set_observed) blocked |= 1u << 3;
            if (!chain_exact) blocked |= 1u << 4;
            if (!current_epochs_exact) blocked |= 1u << 5;
            if (!current_candidate && updated.new_handle == 0) blocked |= 1u << 6;
            if (current_candidate && !command_ready) blocked |= 1u << 7;
            updated.blocked_stage_mask |= blocked;
        }

        std::array<BgmPlaybackBorrowerRecord, 32> published{};
        uint64_t published_collection_version = 0;
        bool publish_current = true;
        bool overflow_latched = false;
        const bool refresh_publication_completed =
            bgm_playback_publish_before_diagnostic(
            [&]() {
                std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
                if (g_bgm_playback_collection_version
                    != snapshot_collection_version) {
                    publish_current = false;
                }
                if (publish_current) {
                    for (size_t i = 0; i < snapshot.size(); ++i) {
                        if (!snapshot[i].active) continue;
                        const auto& current = g_bgm_playback_borrowers[i];
                        if (!bgm_playback_refresh_publish_current(
                                snapshot[i].ordinal, snapshot[i].version,
                                current.ordinal, current.version,
                                snapshot_collection_version,
                                g_bgm_playback_collection_version)) {
                            publish_current = false;
                            break;
                        }
                    }
                }
                if (publish_current) {
                    for (size_t i = 0; i < refreshed.size(); ++i) {
                        if (!refreshed[i].active) continue;
                        refreshed[i].version = snapshot[i].version + 1;
                        g_bgm_playback_borrowers[i] = refreshed[i];
                    }
                    ++g_bgm_playback_collection_version;
                    published_collection_version =
                        g_bgm_playback_collection_version;
                    published = g_bgm_playback_borrowers;
                    overflow_latched = g_bgm_playback_capacity_overflow;
                } else {
                    bool monotonic_merged = false;
                    for (size_t i = 0; i < refreshed.size(); ++i) {
                        auto& current = g_bgm_playback_borrowers[i];
                        if (!snapshot[i].active || !current.active
                            || current.ordinal != snapshot[i].ordinal) continue;
                        bool record_merged = false;
                        if (refreshed[i].old_identity_conflict
                            && !current.old_identity_conflict) {
                            current.old_identity_conflict = true;
                            current.old_absent = false;
                            record_merged = true;
                        }
                        if (refreshed[i].destination_aba
                            && !current.destination_aba) {
                            current.destination_aba = true;
                            record_merged = true;
                        }
                        if (refreshed[i].lineage_identity_conflict
                            && !current.lineage_identity_conflict) {
                            current.lineage_identity_conflict = true;
                            record_merged = true;
                        }
                        if (refreshed[i].failed && !current.failed) {
                            current.failed = true;
                            record_merged = true;
                        }
                        if (!refreshed[i].continuity_exact
                            && current.continuity_exact) {
                            current.continuity_exact = false;
                            record_merged = true;
                        }
                        if (current.failed
                            && current.terminal_state
                                != BgmPlaybackLineageTerminalState::Failed) {
                            current.terminal_state =
                                BgmPlaybackLineageTerminalState::Failed;
                            record_merged = true;
                        }
                        if (record_merged) {
                            if (current.failure_epoch == 0 && current.failed) {
                                current.failure_epoch =
                                    ++g_bgm_playback_observation_epoch;
                            }
                            ++current.version;
                            monotonic_merged = true;
                        }
                    }
                    if (monotonic_merged) {
                        ++g_bgm_playback_collection_version;
                    }
                }
                return true;
            },
            [&] {
                for (const auto& diagnostic : refresh_diagnostics) {
                    if (!diagnostic.proposed) continue;
                    if (g_bgm_playback_observation_logs.fetch_add(
                            1, std::memory_order_relaxed) >= 96) continue;
                    std::ostringstream out;
                    out << "[audio_sead] playback_aggregate status=confirmed_custom_refresh_rejected"
                        << " first_failed_predicate="
                        << confirmed_custom_refresh_failure_name(
                            diagnostic.failure)
                        << " ordinal=" << diagnostic.ordinal
                        << " canonical=0x" << std::hex
                        << diagnostic.canonical_handle
                        << " custom=0x" << diagnostic.custom_handle
                        << " current=0x" << diagnostic.current_handle
                        << std::dec << " mutation_authorized=0";
                    core::log(core::LogLevel::Error, out.str());
                }
            });
        if (!refresh_publication_completed || !publish_current) {
            if (g_bgm_playback_observation_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                try {
                    core::log(core::LogLevel::Info,
                        "[audio_sead] playback_aggregate status=refresh_version_drift mutation_authorized=0");
                } catch (...) {
                }
            }
            return;
        }

        for (size_t i = 0; i < published.size(); ++i) {
            if (published[i].active
                && published[i].provenance
                    == BgmPlaybackLineageProvenance::PreSession
                && snapshot[i].correlated_session_epoch == 0
                && published[i].correlated_session_epoch != 0
                && g_bgm_playback_observation_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                std::ostringstream out;
                out << "[audio_sead] playback_aggregate status=presession_session_correlated"
                    << " ordinal=" << published[i].ordinal
                    << " session=" << published[i].correlated_session_epoch
                    << " cycle=" << published[i].correlated_cycle_epoch
                    << " correlation_epoch="
                    << published[i].session_correlation_epoch
                    << " mutation_authorized=0";
                core::log(core::LogLevel::Info, out.str());
            }
            if (published[i].active && published[i].old_identity_conflict
                && !snapshot[i].old_identity_conflict
                && g_bgm_playback_observation_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                std::ostringstream out;
                out << "[audio_sead] playback_aggregate status=old_handle_identity_conflict"
                    << " ordinal=" << published[i].ordinal
                    << " old=0x" << std::hex << published[i].old_handle
                    << " failure_epoch=" << std::dec
                    << published[i].failure_epoch
                    << " mutation_authorized=0";
                core::log(core::LogLevel::Error, out.str());
            }
            const uint32_t new_blocked = published[i].blocked_stage_mask
                & ~snapshot[i].blocked_stage_mask;
            static constexpr const char* stages[] = {
                "retirement_anchor", "retired_vector", "old_retired_membership",
                "set_correlation", "active_chain", "anchor_continuity",
                "new_handle_association", "active_command_ready"};
            for (uint32_t bit = 0; bit < 8; ++bit) {
                if ((new_blocked & (1u << bit)) == 0) continue;
                if (g_bgm_playback_blocked_logs.fetch_add(
                        1, std::memory_order_relaxed) >= 32) break;
                std::ostringstream out;
                out << "[audio_sead] playback_aggregate status=aggregate_stage_blocked"
                    << " stage=" << stages[bit]
                    << " ordinal=" << published[i].ordinal
                    << " token_epoch=" << published[i].token_epoch
                    << " lifecycle_state_epoch="
                    << published[i].lifecycle_state_epoch
                    << " anchor_state_epoch=" << published[i].anchor_state_epoch
                    << " old=0x" << std::hex << published[i].old_handle
                    << " canonical=0x"
                    << published[i].canonical_intermediate_handle
                    << " custom=0x"
                    << published[i].latest_custom_transition_handle
                    << " new=0x" << published[i].new_handle << std::dec
                    << " set_epoch=" << published[i].transition_set_epoch
                    << " play_epoch=" << published[i].transition_play_epoch
                    << " mutation_authorized=0";
                core::log(core::LogLevel::Info, out.str());
                break;
            }
        }

        struct MarkerProposal {
            size_t index = 0;
            BgmPlaybackAggregateMarker marker = BgmPlaybackAggregateMarker::Invalid;
            uint32_t borrowers = 0;
            bool emit = false;
        };
        std::array<MarkerProposal, 32> proposals{};
        size_t proposal_count = 0;
        for (size_t updated_index = 0; updated_index < published.size();
             ++updated_index) {
            const auto& updated = published[updated_index];
            if (!updated.active || updated.failed
                || !updated.retirement_anchored) continue;
            uint64_t latest_ordinal = updated.ordinal;
            for (const auto& peer : published) {
                if (peer.active && peer.retirement_anchored
                    && peer.custom == updated.custom
                    && peer.token_epoch == updated.token_epoch) {
                    latest_ordinal = (std::max)(latest_ordinal, peer.ordinal);
                }
            }
            if (updated.ordinal != latest_ordinal) continue;
            std::array<uint64_t, 64> borrower_handles{};
            uint32_t borrowers = 0;
            const auto add_borrower = [&](const uint64_t handle) {
                if (handle == 0) return;
                for (uint32_t i = 0; i < borrowers; ++i) {
                    if (borrower_handles[i] == handle) return;
                }
                if (borrowers < borrower_handles.size()) {
                    borrower_handles[borrowers++] = handle;
                }
            };
            bool aggregate_absent = true;
            bool aggregate_identity = true;
            bool aggregate_aba_safe = true;
            bool aggregate_failed = false;
            bool aggregate_list_exit = false;
            bool aggregate_shutdown = false;
            bool aggregate_release_authorized = false;
            bool aggregate_release_committed = false;
            bool aggregate_continuity = true;
            for (const auto& peer : published) {
                if (!peer.active || peer.custom != updated.custom
                    || peer.token_epoch != updated.token_epoch) continue;
                if (!peer.old_absent) add_borrower(peer.old_handle);
                if (peer.new_handle != 0 && !peer.new_absent) {
                    add_borrower(peer.new_handle);
                }
                if (peer.canonical_intermediate_handle != 0
                    && !peer.canonical_intermediate_absent) {
                    add_borrower(peer.canonical_intermediate_handle);
                }
                if (peer.latest_custom_transition_handle != 0
                    && !peer.latest_custom_transition_absent) {
                    add_borrower(peer.latest_custom_transition_handle);
                }
                aggregate_absent = aggregate_absent && peer.old_absent
                    && (peer.new_handle == 0 || peer.new_absent)
                    && (peer.canonical_intermediate_handle == 0
                        || peer.canonical_intermediate_absent)
                    && (peer.latest_custom_transition_handle == 0
                        || peer.latest_custom_transition_absent);
                aggregate_identity = aggregate_identity
                    && peer.retirement_anchored
                    && peer.controller == updated.controller
                    && controller_identity_proof_matches(
                        updated.controller_proof, peer.controller_proof)
                    && peer.sound_identity.serial_number > 0
                    && peer.lease == updated.lease
                    && peer.anchor_state_epoch == updated.anchor_state_epoch
                    && peer.anchor_route_generation
                        == updated.anchor_route_generation
                    && peer.anchor_cleanup_generation
                        == updated.anchor_cleanup_generation
                    && peer.anchor_request_handle
                        == updated.anchor_request_handle;
                aggregate_continuity = aggregate_continuity
                    && peer.continuity_exact;
                aggregate_aba_safe = aggregate_aba_safe && !peer.destination_aba;
                aggregate_failed = aggregate_failed || peer.failed;
                aggregate_list_exit = aggregate_list_exit || peer.list_exit;
                aggregate_shutdown = aggregate_shutdown || peer.shutdown;
                aggregate_release_authorized = aggregate_release_authorized
                    || peer.release_authorized_epoch != 0;
                aggregate_release_committed = aggregate_release_committed
                    || peer.release_committed_epoch != 0;
            }
            BgmPlaybackAggregateFacts facts{
                aggregate_identity && controller_identity_proof_matches_live(
                    updated.controller_proof, updated.controller)
                    && updated.sound_identity.serial_number > 0,
                aggregate_continuity && updated.route_generation != 0
                    && updated.lease.valid() && updated.lifecycle_state_epoch != 0
                    && updated.token_epoch != 0,
                updated.old_handle != 0
                    && (updated.new_handle == 0
                        || updated.new_handle != updated.old_handle),
                aggregate_aba_safe,
                aggregate_absent,
                updated.current_active,
                updated.current_command_ready,
                aggregate_list_exit,
                aggregate_shutdown,
                aggregate_release_authorized,
                aggregate_release_committed,
                aggregate_failed || overflow_latched,
                borrowers,
            };
            const auto marker = classify_bgm_playback_aggregate(facts);
            proposals[proposal_count++] = {
                updated_index, marker, borrowers, updated.last_marker != marker};
        }
        bool marker_batch_current = true;
        BgmPlaybackBorrowerRecord closed_trigger{};
        bool closed_trigger_captured = false;
        std::array<BgmPlaybackBorrowerRecord, 32> closed_records{};
        uint8_t closed_record_count = 0;
        uint64_t closed_collection_version = 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            if (g_bgm_playback_collection_version != published_collection_version) {
                marker_batch_current = false;
            }
            for (size_t i = 0; marker_batch_current && i < published.size(); ++i) {
                if (!published[i].active) continue;
                const auto& current = g_bgm_playback_borrowers[i];
                if (!bgm_playback_marker_batch_current(
                        published_collection_version,
                        g_bgm_playback_collection_version,
                        published[i].ordinal, published[i].version,
                        current.ordinal, current.version)) {
                    marker_batch_current = false;
                }
            }
            if (marker_batch_current) {
                bool changed = false;
                for (size_t i = 0; i < proposal_count; ++i) {
                    const auto& proposal = proposals[i];
                    auto& current = g_bgm_playback_borrowers[proposal.index];
                    if (current.last_marker == proposal.marker) continue;
                    current.last_marker = proposal.marker;
                    ++current.version;
                    if (proposal.emit
                        && proposal.marker
                            == BgmPlaybackAggregateMarker::AggregateClosed
                        && !closed_trigger_captured) {
                        closed_trigger = current;
                        closed_trigger_captured = true;
                    }
                    changed = true;
                }
                if (changed) {
                    ++g_bgm_playback_collection_version;
                    closed_collection_version = g_bgm_playback_collection_version;
                    if (closed_trigger_captured) {
                        for (const auto& record : g_bgm_playback_borrowers) {
                            if (!same_bgm_terminal_domain(record, closed_trigger)) continue;
                            if (closed_record_count >= closed_records.size()) {
                                closed_trigger_captured = false;
                                closed_record_count = 0;
                                break;
                            }
                            closed_records[closed_record_count++] = record;
                        }
                    }
                }
            }
        }
        if (!marker_batch_current) {
            if (g_bgm_playback_observation_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                core::log(core::LogLevel::Info,
                    "[audio_sead] playback_aggregate status=marker_batch_version_drift mutation_authorized=0");
            }
            return;
        }
        if (closed_trigger_captured) {
            observe_bgm_aggregate_closed_terminal_handoff(
                closed_trigger, closed_collection_version,
                closed_records, closed_record_count);
        }
        for (size_t i = 0; i < proposal_count; ++i) {
            const auto& proposal = proposals[i];
            if (proposal.emit) {
                log_bgm_playback_aggregate(
                    published[proposal.index], proposal.marker,
                    proposal.borrowers);
            }
        }
        for (size_t i = 0; i < snapshot.size(); ++i) {
            if (!snapshot[i].active || !snapshot[i].exit_unresolved_native_request
                || !published[i].active
                || published[i].exit_unresolved_native_request) continue;
            const uint64_t request = published[i].new_handle != 0
                ? published[i].new_handle
                : snapshot[i].exit_unresolved_request_handle;
            log_bgm_exit_request(published[i].new_absent
                    ? "exit_request_absent" : "exit_request_resolved",
                published[i].ordinal, request, request >> 32);
        }
        for (size_t i = 0; i < snapshot.size(); ++i) {
            if (!snapshot[i].active || !published[i].active) continue;
            if (snapshot[i].terminal_state
                    != BgmPlaybackLineageTerminalState::SupersededClosed
                && published[i].terminal_state
                    == BgmPlaybackLineageTerminalState::SupersededClosed) {
                log_bgm_aggregate_exit_stage(
                    "superseded_closed", "exact_child_ownership_transfer",
                    published[i].ordinal, published[i].old_handle,
                    published[i].latest_custom_transition_handle,
                    published[i].old_absent,
                    published[i].latest_custom_transition_absent,
                    published[i].current_active);
            }
            if (!snapshot[i].failed && published[i].failed) {
                log_bgm_aggregate_exit_stage(
                    "lineage_failed",
                    published[i].lineage_identity_conflict
                        ? "lineage_identity_conflict"
                        : (published[i].destination_aba
                            ? "destination_aba" : "runtime_failure"),
                    published[i].ordinal, published[i].old_handle,
                    published[i].new_handle, published[i].old_absent,
                    published[i].new_absent,
                    published[i].current_active);
            }
        }
        try_claim_bgm_aggregate_exit_release();
        try_finish_bgm_aggregate_release(
            g_bgm_aggregate_exit_pending.load(std::memory_order_acquire));
    } catch (...) {
        // Observation failure is intentionally isolated from release behavior.
    }
}

void observe_bgm_playback_release(
    const OnMemoryBankReleaseAction& action, bool committed) noexcept
{
    if (!action) return;
    try {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        bool changed = false;
        for (auto& record : g_bgm_playback_borrowers) {
            if (!record.active || record.custom != action.custom.encode()) continue;
            const uint64_t epoch = ++g_bgm_playback_observation_epoch;
            if (committed) record.release_committed_epoch = epoch;
            else record.release_authorized_epoch = epoch;
            ++record.version;
            changed = true;
        }
        if (changed) ++g_bgm_playback_collection_version;
    } catch (...) {
    }
}

struct BgmAggregateExitClosureResult final {
    bool closed = false;
    bool canonical_substrate_retained = false;
    const char* reason = "unknown";
    uint64_t ordinal = 0;
    uint64_t old_handle = 0;
    uint64_t new_handle = 0;
    bool old_absent = false;
    bool new_absent = false;
    bool current_active = false;
};

enum class RetainedNativeStateReleaseBlocker : uint8_t {
    None,
    SlotMissing,
    BgmMissing,
    SoundReadFailed,
    RequestReadFailed,
    BackingReadFailed,
    RetainedRequestStillCurrent,
    RetainedBackingStillCurrent,
    RetainedSoundMissing,
    RetainedSoundIdentityReadFailed,
    RetainedSoundIndexMismatch,
    RetainedSoundSerialMismatch,
    RetainedOwnerReadFailed,
};

struct RetainedNativeStateReleaseDiagnostic final {
    bool proposed = false;
    RetainedNativeStateReleaseBlocker blocker =
        RetainedNativeStateReleaseBlocker::None;
    bool release_claimed = false;
    bool release_committed = false;
    void* current_sound = nullptr;
    uint64_t current_request = 0;
    void* current_backing = nullptr;
    uint64_t retained_request = 0;
    void* retained_backing = nullptr;
    void* retained_sound = nullptr;
    int32_t expected_sound_index = -1;
    int32_t expected_sound_serial = 0;
    int32_t observed_sound_index = -1;
    int32_t observed_sound_serial = 0;
    uint64_t owner_token = 0;
    uint64_t lifecycle_ordinal = 0;
    uint64_t lifecycle_state_epoch = 0;
    uint64_t lifecycle_route_generation = 0;
    uint64_t lifecycle_cleanup_generation = 0;
    uint64_t lifecycle_canonical = 0;
    uint64_t lifecycle_custom = 0;
    uint64_t borrower_ordinal = 0;
    uint64_t borrower_version = 0;
    uint64_t collection_version = 0;
    uint64_t list_exit_epoch = 0;
    bool borrower_list_exit = false;
    bool borrower_shutdown = false;
    bool old_absent = false;
    bool new_absent = false;
    bool canonical_absent = false;
    bool custom_absent = false;
    bool current_active = false;
};
static_assert(std::is_trivially_copyable_v<
    RetainedNativeStateReleaseDiagnostic>);

const char* retained_native_state_release_blocker_name(
    const RetainedNativeStateReleaseBlocker blocker) noexcept
{
    switch (blocker) {
    case RetainedNativeStateReleaseBlocker::None: return "none";
    case RetainedNativeStateReleaseBlocker::SlotMissing: return "slot_missing";
    case RetainedNativeStateReleaseBlocker::BgmMissing: return "bgm_missing";
    case RetainedNativeStateReleaseBlocker::SoundReadFailed:
        return "sound_read_failed";
    case RetainedNativeStateReleaseBlocker::RequestReadFailed:
        return "request_read_failed";
    case RetainedNativeStateReleaseBlocker::BackingReadFailed:
        return "backing_read_failed";
    case RetainedNativeStateReleaseBlocker::RetainedRequestStillCurrent:
        return "retained_request_still_current";
    case RetainedNativeStateReleaseBlocker::RetainedBackingStillCurrent:
        return "retained_backing_still_current";
    case RetainedNativeStateReleaseBlocker::RetainedSoundMissing:
        return "retained_sound_missing";
    case RetainedNativeStateReleaseBlocker::RetainedSoundIdentityReadFailed:
        return "retained_sound_identity_read_failed";
    case RetainedNativeStateReleaseBlocker::RetainedSoundIndexMismatch:
        return "retained_sound_index_mismatch";
    case RetainedNativeStateReleaseBlocker::RetainedSoundSerialMismatch:
        return "retained_sound_serial_mismatch";
    case RetainedNativeStateReleaseBlocker::RetainedOwnerReadFailed:
        return "retained_owner_read_failed";
    }
    return "unknown";
}

void publish_retained_native_state_release_diagnostic(
    const RetainedNativeStateReleaseDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed) return;
    bgm_playback_observe_best_effort(true, [&]() {
        std::ostringstream out;
        out << "[audio_sead] retained_native_state_release"
            << " blocker="
            << retained_native_state_release_blocker_name(diagnostic.blocker)
            << " release_claimed=" << diagnostic.release_claimed
            << " release_committed=" << diagnostic.release_committed
            << " current_sound=" << diagnostic.current_sound
            << " current_request=0x" << std::hex << diagnostic.current_request
            << " current_backing=" << diagnostic.current_backing
            << " retained_request=0x" << diagnostic.retained_request
            << " retained_backing=" << diagnostic.retained_backing
            << " retained_sound=" << diagnostic.retained_sound
            << std::dec
            << " expected_sound_index=" << diagnostic.expected_sound_index
            << " expected_sound_serial=" << diagnostic.expected_sound_serial
            << " observed_sound_index=" << diagnostic.observed_sound_index
            << " observed_sound_serial=" << diagnostic.observed_sound_serial
            << " owner_token=0x" << std::hex << diagnostic.owner_token
            << std::dec
            << " lifecycle_ordinal=" << diagnostic.lifecycle_ordinal
            << " lifecycle_state_epoch=" << diagnostic.lifecycle_state_epoch
            << " lifecycle_route=" << diagnostic.lifecycle_route_generation
            << " lifecycle_cleanup=" << diagnostic.lifecycle_cleanup_generation
            << " lifecycle_canonical=0x" << std::hex
            << diagnostic.lifecycle_canonical
            << " lifecycle_custom=0x" << diagnostic.lifecycle_custom
            << std::dec
            << " borrower_ordinal=" << diagnostic.borrower_ordinal
            << " borrower_version=" << diagnostic.borrower_version
            << " collection_version=" << diagnostic.collection_version
            << " list_exit_epoch=" << diagnostic.list_exit_epoch
            << " borrower_list_exit=" << diagnostic.borrower_list_exit
            << " borrower_shutdown=" << diagnostic.borrower_shutdown
            << " old_absent=" << diagnostic.old_absent
            << " new_absent=" << diagnostic.new_absent
            << " canonical_absent=" << diagnostic.canonical_absent
            << " custom_absent=" << diagnostic.custom_absent
            << " current_active=" << diagnostic.current_active;
        core::log(core::LogLevel::Info, out.str());
    });
}

BgmAggregateExitClosureResult evaluate_bgm_aggregate_release_closed(
    const OnMemoryBankReleaseAction& action, bool exit_authority) noexcept
{
    BgmAggregateExitClosureResult result;
    if (!action) { result.reason = "invalid_action"; return result; }
    if (!exit_authority) { result.reason = "no_exit_authority"; return result; }
    if (!g_bgm_aggregate_mutation_gate.drained()) {
        result.reason = "observer_lease_in_flight";
        return result;
    }
    if (g_bgm_aggregate_unresolved_publications.load(
            std::memory_order_acquire) != 0) {
        result.reason = "unresolved_publication";
        return result;
    }
    try {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        bool matched = false;
        for (const auto& record : g_bgm_playback_borrowers) {
            if (!record.active || record.custom != action.custom.encode()) continue;
            matched = true;
            if (record.ordinal >= result.ordinal) {
                result.ordinal = record.ordinal;
                result.old_handle = record.old_handle;
                result.new_handle = record.new_handle;
                result.old_absent = record.old_absent;
                result.new_absent = record.new_absent;
                result.current_active = record.current_active;
            }
            const bool exit_exact = record.list_exit || record.shutdown;
            if (bgm_playback_canonical_substrate_partial_blocks_release(
                    record.canonical_substrate_relinquishment_state)) {
                result.reason = "canonical_substrate_relinquishment_partial";
                return result;
            }
            const bool canonical_substrate_closed =
                bgm_playback_canonical_substrate_closure_exact({
                    record.canonical_substrate_relinquished
                        && record.canonical_substrate_relinquishment_state
                            == BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished
                        && record.canonical_substrate_transaction_generation != 0,
                    record.canonical_substrate_handle != 0
                        && record.canonical_substrate_handle == record.new_handle
                        && record.canonical_substrate_handle
                            == record.canonical_intermediate_handle,
                    record.canonical_substrate_sound != nullptr
                        && record.canonical_substrate_sound
                            == record.expected_new_sound
                        && record.canonical_substrate_sound_identity.internal_index
                            == record.expected_new_sound_identity.internal_index
                        && record.canonical_substrate_sound_identity.serial_number
                            == record.expected_new_sound_identity.serial_number,
                    record.controller != nullptr && record.slot != nullptr
                        && record.bgm != nullptr
                        && record.canonical_substrate_route_generation
                            == record.canonical_intermediate_route_generation
                        && record.canonical_substrate_lease == record.lease,
                    record.old_absent,
                    record.latest_custom_transition_handle == 0
                        || record.latest_custom_transition_absent,
                    !record.transition_set_pending
                        && !record.canonical_stop_boundary_active
                        && !record.exit_unresolved_native_request,
                    !record.failed && !record.destination_aba
                        && !record.old_identity_conflict
                        && !record.lineage_identity_conflict
                        && record.continuity_exact,
                    record.canonical_substrate_current_exact
                        || record.new_absent,
                });
            const bool request_closed = canonical_substrate_closed
                || bgm_playback_exit_request_closed(
                record.current_active, record.new_ready,
                record.new_handle, record.new_absent);
            const bool transition_closed = !record.transition_set_pending
                && !record.canonical_stop_boundary_active
                && (record.latest_custom_transition_handle == 0
                    || record.latest_custom_transition_absent);
            if (record.failed) { result.reason = "record_failed"; return result; }
            if (record.terminal_state
                == BgmPlaybackLineageTerminalState::Failed) {
                result.reason = "lineage_failed";
                return result;
            }
            if (record.destination_aba) { result.reason = "destination_aba"; return result; }
            if (record.old_identity_conflict) { result.reason = "identity_conflict"; return result; }
            if (record.lineage_identity_conflict) {
                result.reason = "lineage_identity_conflict";
                return result;
            }
            if (!record.continuity_exact) { result.reason = "continuity_drift"; return result; }
            if (record.exit_unresolved_native_request) { result.reason = "unresolved_request"; return result; }
            if (!exit_exact) { result.reason = "exit_not_published"; return result; }
            if (!record.old_absent) { result.reason = "old_request_present"; return result; }
            if (record.canonical_intermediate_handle != 0
                && !record.canonical_intermediate_absent
                && !canonical_substrate_closed) {
                result.reason = "canonical_request_present";
                return result;
            }
            if (record.latest_custom_transition_handle != 0
                && !record.latest_custom_transition_absent) {
                result.reason = record.latest_custom_transition_child_ordinal != 0
                    ? "superseded_custom_request_present"
                    : "unconsumed_custom_request_present";
                return result;
            }
            if (!request_closed) {
                result.reason = "new_request_present";
                return result;
            }
            result.canonical_substrate_retained =
                result.canonical_substrate_retained || canonical_substrate_closed;
            if (!transition_closed) { result.reason = "transition_provenance_pending"; return result; }
            if (record.latest_custom_transition_child_ordinal != 0
                && record.terminal_state
                    != BgmPlaybackLineageTerminalState::SupersededClosed) {
                result.reason = "superseded_lineage_pending";
                return result;
            }
        }
        if (!matched) { result.reason = "no_matching_borrower"; return result; }
        result.closed = bgm_playback_aggregate_release_exact(
                exit_authority, matched, action.custom.encode() != 0,
                action.canonical.encode() != action.custom.encode(), true, true)
            && bgm_playback_aggregate_exit_closure_exact({
                exit_authority, matched, true, true, true, true, true,
                action.canonical.encode() != action.custom.encode(), true,
                g_bgm_aggregate_mutation_gate.drained()});
        result.reason = result.closed ? "closed" : "policy_rejected";
        return result;
    } catch (...) {
        result.reason = "observer_exception";
        return result;
    }
}

bool bgm_aggregate_release_closed(
    const OnMemoryBankReleaseAction& action, bool exit_authority) noexcept
{
    return evaluate_bgm_aggregate_release_closed(action, exit_authority).closed;
}

bool bgm_aggregate_release_has_borrowers(
    const OnMemoryBankReleaseAction& action) noexcept
{
    if (!action) return false;
    try {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        for (const auto& record : g_bgm_playback_borrowers) {
            if (record.active && record.custom == action.custom.encode()) return true;
        }
    } catch (...) {
    }
    return false;
}

void try_claim_bgm_aggregate_exit_release() noexcept
{
    if (!g_bgm_aggregate_exit_pending.load(std::memory_order_acquire)) return;
    try {
        OnMemoryBankDetachedRecord lifecycle;
        uint64_t lifecycle_epoch = 0;
        bool cleanup_exact = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            lifecycle = g_onmemory_bank_lifecycle.active();
            lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
            cleanup_exact = g_failed_patch_journal.empty()
                && !g_sound_patch_rollback.active()
                && !g_controller_patch_rollback.active()
                && g_pending_play_setup_patch.patches.empty();
        }
        if (!lifecycle
            || lifecycle.phase != OnMemoryBankLifecyclePhase::RestoreApplied
            || !cleanup_exact || !lifecycle.backing_observed) {
            log_bgm_aggregate_exit_stage("exit_release_blocked",
                !lifecycle ? "lifecycle_missing"
                    : lifecycle.phase != OnMemoryBankLifecyclePhase::RestoreApplied
                        ? "lifecycle_phase"
                        : !cleanup_exact ? "cleanup_pending"
                        : "backing_unobserved",
                lifecycle.ordinal, lifecycle.request_handle, 0,
                false, false, false);
            return;
        }

        const OnMemoryBankReleaseAction expected{
            lifecycle.ordinal, lifecycle.route_generation,
            lifecycle.cleanup_generation, lifecycle.canonical,
            lifecycle.custom};
        const auto closure = evaluate_bgm_aggregate_release_closed(expected, true);
        if (!closure.closed) {
            log_bgm_aggregate_exit_stage("exit_release_blocked",
                closure.reason, closure.ordinal, closure.old_handle,
                closure.new_handle, closure.old_absent, closure.new_absent,
                closure.current_active);
            return;
        }
        if (closure.canonical_substrate_retained) {
            core::log(core::LogLevel::Info,
                "[audio_sead] borrower_closure status=retained_canonical_substrate custom_release_required=1");
        }

        BgmPlaybackBorrowerRecord latest;
        bool found = false;
        uint64_t latest_collection_version = 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            latest_collection_version = g_bgm_playback_collection_version;
            for (const auto& record : g_bgm_playback_borrowers) {
                if (!record.active
                    || record.custom != lifecycle.custom.encode()) continue;
                if (!found || record.ordinal > latest.ordinal) {
                    latest = record;
                    found = true;
                }
            }
        }
        if (!found) return;

        void* bgm = latest.bgm;
        void* current_sound = nullptr;
        void* current_backing = nullptr;
        uint64_t current_request = 0;
        uint64_t owner_token = 0;
        UObjectIdentity detached_identity;
        RetainedNativeStateReleaseDiagnostic native_diagnostic;
        native_diagnostic.proposed = true;
        native_diagnostic.retained_request = lifecycle.request_handle;
        native_diagnostic.retained_backing = lifecycle.backing_identity;
        native_diagnostic.retained_sound = lifecycle.sound.object;
        native_diagnostic.expected_sound_index =
            lifecycle.sound.live.internal_index;
        native_diagnostic.expected_sound_serial =
            lifecycle.sound.live.serial_number;
        native_diagnostic.lifecycle_ordinal = lifecycle.ordinal;
        native_diagnostic.lifecycle_state_epoch = lifecycle_epoch;
        native_diagnostic.lifecycle_route_generation =
            lifecycle.route_generation;
        native_diagnostic.lifecycle_cleanup_generation =
            lifecycle.cleanup_generation;
        native_diagnostic.lifecycle_canonical = lifecycle.canonical.encode();
        native_diagnostic.lifecycle_custom = lifecycle.custom.encode();
        native_diagnostic.borrower_ordinal = latest.ordinal;
        native_diagnostic.borrower_version = latest.version;
        native_diagnostic.collection_version = latest_collection_version;
        native_diagnostic.list_exit_epoch = latest.list_exit_epoch;
        native_diagnostic.borrower_list_exit = latest.list_exit;
        native_diagnostic.borrower_shutdown = latest.shutdown;
        native_diagnostic.old_absent = latest.old_absent;
        native_diagnostic.new_absent = latest.new_absent;
        native_diagnostic.canonical_absent =
            latest.canonical_intermediate_absent;
        native_diagnostic.custom_absent =
            latest.latest_custom_transition_absent;
        native_diagnostic.current_active = latest.current_active;

        auto& native_blocker = native_diagnostic.blocker;
        if (!latest.slot) {
            native_blocker = RetainedNativeStateReleaseBlocker::SlotMissing;
        } else if (!bgm) {
            native_blocker = RetainedNativeStateReleaseBlocker::BgmMissing;
        } else if (!core::safe_read_field(bgm,
                runtime_layouts::SqexSeadBgm::sound, current_sound)) {
            native_blocker =
                RetainedNativeStateReleaseBlocker::SoundReadFailed;
        } else if (!core::safe_read_field(bgm,
                runtime_layouts::SqexSeadBgm::request_handle,
                current_request)) {
            native_blocker =
                RetainedNativeStateReleaseBlocker::RequestReadFailed;
        } else if (!core::safe_read_field(bgm,
                runtime_layouts::SqexSeadBgm::backing_resource,
                current_backing)) {
            native_blocker =
                RetainedNativeStateReleaseBlocker::BackingReadFailed;
        } else if (current_request == lifecycle.request_handle) {
            native_blocker = RetainedNativeStateReleaseBlocker::
                RetainedRequestStillCurrent;
        } else if (!onmemory_bank_current_backing_detached(
                lifecycle.backing_identity,
                lifecycle.backing_observed,
                current_backing,
                true)) {
            native_blocker = RetainedNativeStateReleaseBlocker::
                RetainedBackingStillCurrent;
        } else if (!lifecycle.sound.object) {
            native_blocker =
                RetainedNativeStateReleaseBlocker::RetainedSoundMissing;
        } else if (!read_uobject_identity(
                lifecycle.sound.object, detached_identity)) {
            native_blocker = RetainedNativeStateReleaseBlocker::
                RetainedSoundIdentityReadFailed;
        } else if (detached_identity.live.internal_index
                != lifecycle.sound.live.internal_index) {
            native_blocker = RetainedNativeStateReleaseBlocker::
                RetainedSoundIndexMismatch;
        } else if (detached_identity.live.serial_number
                != lifecycle.sound.live.serial_number) {
            native_blocker = RetainedNativeStateReleaseBlocker::
                RetainedSoundSerialMismatch;
        } else if (!core::safe_read_field(lifecycle.sound.object,
                runtime_layouts::SqexSeadSound::observed_field548,
                owner_token)) {
            native_blocker = RetainedNativeStateReleaseBlocker::
                RetainedOwnerReadFailed;
        }
        native_diagnostic.current_sound = current_sound;
        native_diagnostic.current_request = current_request;
        native_diagnostic.current_backing = current_backing;
        native_diagnostic.observed_sound_index =
            detached_identity.live.internal_index;
        native_diagnostic.observed_sound_serial =
            detached_identity.live.serial_number;
        native_diagnostic.owner_token = owner_token;
        const bool native_exact = native_blocker
            == RetainedNativeStateReleaseBlocker::None;
        if (!native_exact) {
            publish_retained_native_state_release_diagnostic(
                native_diagnostic);
            log_bgm_aggregate_exit_stage("exit_release_blocked",
                "retained_native_state_unreadable", latest.ordinal,
                latest.old_handle, latest.new_handle, latest.old_absent,
                latest.new_absent, latest.current_active);
            return;
        }

        OnMemoryBankRetirementFacts facts;
        facts.route_generation = lifecycle.route_generation + 1;
        facts.cleanup_generation = lifecycle.cleanup_generation;
        facts.retired_request_handle = lifecycle.request_handle;
        facts.current_request_handle = current_request;
        facts.current_backing = current_backing;
        facts.retired_backing = lifecycle.backing_identity;
        facts.current_backing_observed = true;
        facts.retired_backing_observed = true;
        facts.current_owner = classify_onmemory_bank_retirement_owner(
            lifecycle, lifecycle.sound, true, true, owner_token);
        facts.exact_request_retired = true;
        facts.route_released = true;
        facts.playback_released = true;
        facts.cleanup_released = true;
        facts.owner_restore_verified = owner_token == 0
            || owner_token == lifecycle.canonical.encode();
        facts.runtime_installed = g_audio_route_installed.load(
            std::memory_order_acquire);
        facts.lookup_signature_valid = g_onmemory_bank_kind_lookup_available.load(
            std::memory_order_acquire);
        facts.release_signature_valid = g_onmemory_bank_release_available.load(
            std::memory_order_acquire);
        facts.shutdown_or_disabled = false;

        OnMemoryBankReleaseAction action;
        bool release_claimed = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            release_claimed = g_onmemory_bank_lifecycle.state_epoch()
                    == lifecycle_epoch
                && g_onmemory_bank_lifecycle.claim_release(facts, action);
        }
        if (!release_claimed) {
            log_bgm_aggregate_exit_stage("exit_release_blocked",
                "lifecycle_claim_rejected", latest.ordinal,
                latest.old_handle, latest.new_handle, latest.old_absent,
                latest.new_absent, latest.current_active);
            return;
        }
        observe_bgm_playback_release(action, false);
        defer_bgm_aggregate_release(action);
        log_bgm_aggregate_mutation("exit_release_claimed", "aggregate_closed",
            action.ordinal, action.route_generation, action.cleanup_generation);
    } catch (...) {
    }
}

void defer_bgm_aggregate_release(
    const OnMemoryBankReleaseAction& action) noexcept
{
    if (!action) return;
    bool retained = false;
    try {
        {
            std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
            auto& claim = g_bgm_aggregate_release_claim;
            const bool same_action = claim.action.ordinal == action.ordinal
                && claim.action.route_generation == action.route_generation
                && claim.action.cleanup_generation == action.cleanup_generation
                && claim.action.canonical.encode() == action.canonical.encode()
                && claim.action.custom.encode() == action.custom.encode();
            if ((claim.state == AggregateReleaseClaimState::Complete
                    || !claim.action) && g_bgm_aggregate_release_generation
                    != UINT64_MAX) {
                const uint64_t next_generation =
                    g_bgm_aggregate_release_generation + 1;
                if (claim.publish_pending(true, next_generation)) {
                    g_bgm_aggregate_release_generation = next_generation;
                    claim.action = action;
                    retained = true;
                }
            } else if (same_action) {
                retained = true;
            }
        }
        if (retained) log_bgm_aggregate_mutation(
            "release_retained", "borrowers_active",
            action.ordinal, action.route_generation, action.cleanup_generation);
    } catch (...) {
    }
}

void try_finish_bgm_aggregate_release(bool exit_authority) noexcept
{
    OnMemoryBankReleaseAction action;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
        const auto& claim = g_bgm_aggregate_release_claim;
        if (claim.state != AggregateReleaseClaimState::Pending) return;
        action = claim.action;
        generation = claim.generation;
    }
    if (!action) return;
    const auto pending_closure =
        evaluate_bgm_aggregate_release_closed(action, exit_authority);
    if (!pending_closure.closed) {
        log_bgm_aggregate_exit_stage("exit_release_blocked",
            pending_closure.reason, pending_closure.ordinal,
            pending_closure.old_handle, pending_closure.new_handle,
            pending_closure.old_absent, pending_closure.new_absent,
            pending_closure.current_active);
        return;
    }

    uint64_t claim_id = 0;
    {
        std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
        auto& claim = g_bgm_aggregate_release_claim;
        const bool same_action = claim.action.ordinal == action.ordinal
            && claim.action.route_generation == action.route_generation
            && claim.action.cleanup_generation == action.cleanup_generation
            && claim.action.canonical.encode() == action.canonical.encode()
            && claim.action.custom.encode() == action.custom.encode();
        if (!same_action || claim.generation != generation
            || claim.state != AggregateReleaseClaimState::Pending
            || g_bgm_aggregate_release_claim_id == UINT64_MAX) return;
        const uint64_t next_claim_id = g_bgm_aggregate_release_claim_id + 1;
        if (!claim.take(generation, next_claim_id)) return;
        g_bgm_aggregate_release_claim_id = next_claim_id;
        claim_id = next_claim_id;
    }
    const auto inflight_closure =
        evaluate_bgm_aggregate_release_closed(action, exit_authority);
    if (!inflight_closure.closed) {
        {
            std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
            auto& claim = g_bgm_aggregate_release_claim;
            if (claim.generation == generation && claim.claim_id == claim_id
                && claim.state == AggregateReleaseClaimState::InFlight) {
                claim.state = AggregateReleaseClaimState::Pending;
                claim.claim_id = 0;
            }
        }
        log_bgm_aggregate_exit_stage("exit_release_blocked",
            inflight_closure.reason, inflight_closure.ordinal,
            inflight_closure.old_handle, inflight_closure.new_handle,
            inflight_closure.old_absent, inflight_closure.new_absent,
            inflight_closure.current_active);
        return;
    }
    log_bgm_aggregate_mutation("release_inflight", "aggregate_closed",
        action.ordinal, generation, claim_id);

    const auto execution = execute_onmemory_bank_release(
        g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
        g_onmemory_bank_release_available.load(std::memory_order_acquire),
        !g_audio_route_installed.load(std::memory_order_acquire)
            || g_audio_route_disabled.load(std::memory_order_acquire),
        action,
        [](const uint64_t token) noexcept {
            return lookup_onmemory_bank_kind_noexcept(token);
        },
        [](const uint64_t* token, const uint8_t asynchronous) noexcept {
            return release_onmemory_bank_async_noexcept(token, asynchronous);
        });
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        finished = g_onmemory_bank_lifecycle.finish_release(
            action, execution.outcome);
    }
    bool completion_current = false;
    bool retained_failure = false;
    {
        std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
        auto& claim = g_bgm_aggregate_release_claim;
        const bool same_action = claim.action.ordinal == action.ordinal
            && claim.action.route_generation == action.route_generation
            && claim.action.cleanup_generation == action.cleanup_generation
            && claim.action.canonical.encode() == action.canonical.encode()
            && claim.action.custom.encode() == action.custom.encode();
        completion_current = same_action && claim.generation == generation
            && claim.claim_id == claim_id
            && claim.state == AggregateReleaseClaimState::InFlight;
        if (completion_current) {
            const bool succeeded = finished
                && execution.outcome != OnMemoryBankReleaseOutcome::Failed;
            completion_current = claim.complete(generation, claim_id, succeeded);
            retained_failure = completion_current && !succeeded;
        }
    }
    if (!completion_current) {
        log_bgm_aggregate_mutation("release_stale_completion",
            "aggregate_closed", action.ordinal, generation, claim_id);
        return;
    }
    if (finished && execution.outcome != OnMemoryBankReleaseOutcome::Failed) {
        observe_bgm_playback_release(action, true);
        log_bgm_aggregate_mutation("release_committed", "aggregate_closed",
            action.ordinal, generation, claim_id);
    } else if (retained_failure) {
        log_bgm_aggregate_mutation("release_failed_retained",
            "aggregate_closed", action.ordinal, generation, claim_id);
    }
    if (finished && execution.outcome == OnMemoryBankReleaseOutcome::AlreadyAbsent) {
        OnMemoryBankDetachedRecord lifecycle;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            lifecycle = g_onmemory_bank_lifecycle.active();
        }
        complete_bgm_aggregate_exit_cleanup(lifecycle);
    }
}

struct CanonicalSubstrateBridgeEnrichmentDiagnostic final {
    bool proposed = false;
    bool attempted = false;
    bool succeeded = false;
    BgmCanonicalSubstrateBridgeEnrichmentFailure first_failure =
        BgmCanonicalSubstrateBridgeEnrichmentFailure::None;
    uint64_t source_ordinal = 0;
    uint64_t proof_source_version = 0;
    uint64_t observed_source_version = 0;
    uint64_t resulting_source_version = 0;
    uint64_t proof_collection_version = 0;
    uint64_t resulting_collection_version = 0;
    uint64_t bridge_generation = 0;
    uint64_t release_completion_epoch = 0;
    void* callback_sound = nullptr;
    int32_t callback_sound_index = -1;
    int32_t callback_sound_serial = 0;
    bool custom_token_cleared = false;
    bool source_inactive = false;
};
static_assert(std::is_trivially_copyable_v<
    CanonicalSubstrateBridgeEnrichmentDiagnostic>);

const char* canonical_substrate_bridge_enrichment_failure_name(
    const BgmCanonicalSubstrateBridgeEnrichmentFailure failure) noexcept
{
    using Failure = BgmCanonicalSubstrateBridgeEnrichmentFailure;
    switch (failure) {
    case Failure::None: return "none";
    case Failure::SourceMissing: return "source_missing";
    case Failure::ProofInactive: return "proof_inactive";
    case Failure::ProofState: return "proof_state";
    case Failure::ProofTransaction: return "proof_transaction";
    case Failure::GenerationExhausted: return "generation_exhausted";
    case Failure::BridgeAlreadyInitialized: return "bridge_already_initialized";
    case Failure::SourceInactive: return "source_inactive";
    case Failure::SourceOrdinal: return "source_ordinal";
    case Failure::SourceVersionRegression: return "source_version_regression";
    case Failure::CollectionVersionRegression: return "collection_version_regression";
    case Failure::SourceState: return "source_state";
    case Failure::Transaction: return "transaction";
    case Failure::ListExitEpoch: return "list_exit_epoch";
    case Failure::Controller: return "controller";
    case Failure::SlotBgm: return "slot_bgm";
    case Failure::CanonicalSound: return "canonical_sound";
    case Failure::CanonicalRequest: return "canonical_request";
    case Failure::RouteGeneration: return "route_generation";
    case Failure::Lease: return "lease";
    case Failure::LifecycleEpoch: return "lifecycle_epoch";
    case Failure::Tokens: return "tokens";
    case Failure::CallbackIdentity: return "callback_identity";
    case Failure::CallbackAliasesCanonical: return "callback_aliases_canonical";
    case Failure::ReleaseEpoch: return "release_epoch";
    case Failure::LifecycleNotCompleteAbsent: return "lifecycle_not_complete_absent";
    case Failure::LifecycleAnchor: return "lifecycle_anchor";
    case Failure::SubstrateNotCurrent: return "substrate_not_current";
    case Failure::RecordUnhealthy: return "record_unhealthy";
    case Failure::PendingProvenance: return "pending_provenance";
    case Failure::ChildProvenance: return "child_provenance";
    }
    return "unknown";
}

void complete_bgm_aggregate_exit_cleanup(
    const OnMemoryBankDetachedRecord& lifecycle) noexcept
{
    if (!lifecycle
        || lifecycle.phase != OnMemoryBankLifecyclePhase::Complete) return;
    try {
        CanonicalSubstrateBridgeEnrichmentDiagnostic bridge_diagnostic;
        bridge_diagnostic.proposed = true;
        auto deferred_bridge_diagnostic = make_deferred_noexcept_action(
            [&]() noexcept {
                if (!bridge_diagnostic.proposed) return;
                bgm_playback_observe_best_effort(true, [&]() {
                    std::ostringstream out;
                    out << "[audio_sead] canonical_substrate_bridge_enrichment"
                        << " attempted=" << bridge_diagnostic.attempted
                        << " succeeded=" << bridge_diagnostic.succeeded
                        << " first_failure="
                        << canonical_substrate_bridge_enrichment_failure_name(
                            bridge_diagnostic.first_failure)
                        << " source_ordinal=" << bridge_diagnostic.source_ordinal
                        << " proof_source_version="
                        << bridge_diagnostic.proof_source_version
                        << " observed_source_version="
                        << bridge_diagnostic.observed_source_version
                        << " resulting_source_version="
                        << bridge_diagnostic.resulting_source_version
                        << " proof_collection_version="
                        << bridge_diagnostic.proof_collection_version
                        << " resulting_collection_version="
                        << bridge_diagnostic.resulting_collection_version
                        << " bridge_generation="
                        << bridge_diagnostic.bridge_generation
                        << " release_completion_epoch="
                        << bridge_diagnostic.release_completion_epoch
                        << " callback_sound=" << bridge_diagnostic.callback_sound
                        << " callback_sound_index="
                        << bridge_diagnostic.callback_sound_index
                        << " callback_sound_serial="
                        << bridge_diagnostic.callback_sound_serial
                        << " custom_token_cleared="
                        << bridge_diagnostic.custom_token_cleared
                        << " source_inactive="
                        << bridge_diagnostic.source_inactive;
                    core::log(core::LogLevel::Info, out.str());
                });
            });
        bool thaw_profile = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const auto current = g_onmemory_bank_lifecycle.active();
            if (!current || current.ordinal != lifecycle.ordinal
                || current.phase != OnMemoryBankLifecyclePhase::Complete) return;
            const auto rearm_authority =
                g_onmemory_bank_lifecycle.completed_owner_zero_rearm_authority();
            const bool preserve_completed_rearm = rearm_authority.present();
            if (preserve_completed_rearm
                && !g_onmemory_bank_lifecycle.completed_reset_matches(
                    current, rearm_authority)) return;
            const auto lease = g_frozen_profile_lease.identity();
            if (g_audio_route_state.phase
                    == AudioRoutePhase::CanonicalRelinquished) {
                // The distinct relinquishment event already released the
                // frozen profile lease without asserting a native clear.
                if (g_frozen_profile_lease.active()
                    || g_audio_route_state.canonical_relinquishment_generation
                        == 0) {
                    return;
                }
            }
            const auto old_transition_state =
                capture_audio_route_transition_projection_locked();
            if (g_audio_route_state.phase
                    == AudioRoutePhase::CanonicalRelinquished) {
                g_audio_route_state.phase = AudioRoutePhase::Idle;
                g_audio_route_state.canonical_relinquishment_generation = 0;
                thaw_profile = true;
            } else if (g_frozen_profile_lease.active()) {
                const auto decision = g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::NativeReleaseCommitted, lease);
                if (!decision.clear_route_metadata || !decision.thaw_profile) return;
                thaw_profile = true;
            } else thaw_profile = true;
            clear_owned_audio_route_locked();
            g_audio_route_state.list_cleanup_pending = false;
            enqueue_audio_route_transition_locked(
                AudioRouteTransitionReason::AggregateExitComplete,
                AudioRouteTransitionKind::RouteReset, old_transition_state);
            g_pause_resume_bank = {};
            if (preserve_completed_rearm) {
                if (!g_onmemory_bank_lifecycle.reset_completed_for_next_activation(
                        current, rearm_authority)) return;
            } else {
                g_onmemory_bank_lifecycle.reset();
            }
        }
        if (thaw_profile) registry().clear_frozen_profile();
        bool still_pending = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            bridge_diagnostic.attempted = true;
            bridge_diagnostic.source_ordinal =
                g_bgm_canonical_substrate_proof.source_ordinal;
            bridge_diagnostic.proof_source_version =
                g_bgm_canonical_substrate_proof.source_version;
            bridge_diagnostic.proof_collection_version =
                g_bgm_canonical_substrate_proof.source_collection_version;

            size_t source_index = g_bgm_playback_borrowers.size();
            for (size_t i = 0; i < g_bgm_playback_borrowers.size(); ++i) {
                if (g_bgm_playback_borrowers[i].ordinal
                        == g_bgm_canonical_substrate_proof.source_ordinal) {
                    source_index = i;
                    break;
                }
            }

            BgmCanonicalSubstrateBridgeEnrichmentFacts enrichment_facts;
            enrichment_facts.source_found =
                source_index < g_bgm_playback_borrowers.size();
            if (enrichment_facts.source_found) {
                const auto& source = g_bgm_playback_borrowers[source_index];
                const auto& proof = g_bgm_canonical_substrate_proof;
                bridge_diagnostic.observed_source_version = source.version;
                enrichment_facts.proof_active = proof.active;
                enrichment_facts.proof_state_exact = proof.state
                    == BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished;
                enrichment_facts.proof_transaction_nonzero =
                    proof.transaction_generation != 0;
                enrichment_facts.generation_available =
                    g_canonical_substrate_bridge_generation != UINT64_MAX;
                enrichment_facts.bridge_uninitialized =
                    proof.bridge.phase == CanonicalSubstrateBridgePhase::None
                    && proof.bridge.generation == 0
                    && proof.bridge.release_completion_epoch == 0
                    && proof.bridge.expected_callback_sound == nullptr
                    && proof.bridge.canonical_token == 0;
                enrichment_facts.source_active = source.active;
                enrichment_facts.source_ordinal_exact =
                    source.ordinal == proof.source_ordinal;
                enrichment_facts.source_version_monotonic =
                    bgm_canonical_substrate_bridge_source_version_monotonic(
                        proof.source_version, source.version);
                enrichment_facts.collection_version_monotonic =
                    g_bgm_playback_collection_version
                        >= proof.source_collection_version;
                enrichment_facts.source_state_exact =
                    source.canonical_substrate_relinquished
                    && source.canonical_substrate_relinquishment_state
                        == BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished;
                enrichment_facts.transaction_exact =
                    source.canonical_substrate_transaction_generation
                        == proof.transaction_generation;
                enrichment_facts.list_exit_epoch_exact =
                    source.list_exit_epoch == proof.list_exit_epoch;
                enrichment_facts.controller_exact =
                    source.controller == proof.controller
                    && controller_identity_proof_matches(
                        source.controller_proof, proof.controller_proof);
                enrichment_facts.slot_bgm_exact =
                    source.slot == proof.slot && source.bgm == proof.bgm;
                enrichment_facts.canonical_sound_exact =
                    source.canonical_substrate_sound == proof.sound
                    && source.expected_new_sound == proof.sound
                    && source.canonical_substrate_sound_identity.internal_index
                        == proof.sound_identity.internal_index
                    && source.canonical_substrate_sound_identity.serial_number
                        == proof.sound_identity.serial_number
                    && source.expected_new_sound_identity.internal_index
                        == proof.sound_identity.internal_index
                    && source.expected_new_sound_identity.serial_number
                        == proof.sound_identity.serial_number;
                enrichment_facts.canonical_request_exact =
                    source.canonical_substrate_handle == proof.request
                    && source.canonical_intermediate_handle == proof.request
                    && source.new_handle == proof.request;
                enrichment_facts.route_generation_exact =
                    source.canonical_substrate_route_generation
                        == proof.route_generation
                    && source.canonical_intermediate_route_generation
                        == proof.route_generation;
                enrichment_facts.lease_exact =
                    source.canonical_substrate_lease == proof.lease
                    && source.lease == proof.lease;
                enrichment_facts.lifecycle_epoch_exact =
                    source.lifecycle_state_epoch == proof.lifecycle_state_epoch;
                enrichment_facts.tokens_exact =
                    proof.canonical != 0 && proof.custom != 0
                    && source.canonical == proof.canonical
                    && source.custom == proof.custom
                    && lifecycle.canonical.encode() == proof.canonical
                    && lifecycle.custom.encode() == proof.custom;
                enrichment_facts.callback_identity_exact =
                    source.sound != nullptr
                    && private_object_handle_valid(source.sound_identity);
                enrichment_facts.callback_distinct_from_canonical =
                    source.sound != proof.sound
                    && (source.sound_identity.internal_index
                            != proof.sound_identity.internal_index
                        || source.sound_identity.serial_number
                            != proof.sound_identity.serial_number);
                enrichment_facts.release_epoch_exact =
                    bgm_canonical_substrate_bridge_release_epoch_exact(
                        source.canonical_substrate_relinquishment_epoch,
                        source.release_committed_epoch,
                        lifecycle.cleanup_generation);
                enrichment_facts.lifecycle_complete_custom_absent =
                    lifecycle.phase == OnMemoryBankLifecyclePhase::Complete
                    && lifecycle.release_attempted;
                enrichment_facts.lifecycle_anchor_exact =
                    lifecycle.ordinal == source.token_epoch
                    && lifecycle.route_generation == source.anchor_route_generation
                    && lifecycle.cleanup_generation == source.anchor_cleanup_generation
                    && lifecycle.request_handle == source.anchor_request_handle
                    && lifecycle.sound.object == source.sound
                    && lifecycle.sound.live.internal_index
                        == source.sound_identity.internal_index
                    && lifecycle.sound.live.serial_number
                        == source.sound_identity.serial_number;
                enrichment_facts.substrate_current_exact =
                    source.canonical_substrate_current_exact;
                enrichment_facts.record_healthy =
                    !source.failed && !source.destination_aba
                    && !source.old_identity_conflict
                    && !source.lineage_identity_conflict
                    && source.continuity_exact
                    && source.terminal_state
                        != BgmPlaybackLineageTerminalState::Failed;
                enrichment_facts.no_pending_provenance =
                    !source.transition_set_pending
                    && !source.canonical_stop_boundary_active
                    && !source.exit_unresolved_native_request;
                enrichment_facts.no_child_provenance =
                    source.latest_custom_transition_child_ordinal == 0;
            }
            bridge_diagnostic.first_failure =
                first_bgm_canonical_substrate_bridge_enrichment_failure(
                    enrichment_facts);
            const bool enrichment_exact =
                bridge_diagnostic.first_failure
                    == BgmCanonicalSubstrateBridgeEnrichmentFailure::None;

            bool changed = false;
            for (auto& record : g_bgm_playback_borrowers) {
                if (!record.active
                    || record.custom != lifecycle.custom.encode()) continue;
                record.active = false;
                ++record.version;
                changed = true;
            }
            if (changed) ++g_bgm_playback_collection_version;
            if (changed && enrichment_exact
                && source_index < g_bgm_playback_borrowers.size()) {
                    const auto& record = g_bgm_playback_borrowers[source_index];
                    auto& proof = g_bgm_canonical_substrate_proof;
                    auto& bridge = proof.bridge;
                    bridge.phase = CanonicalSubstrateBridgePhase::Available;
                    bridge.generation = ++g_canonical_substrate_bridge_generation;
                    bridge.transaction_generation =
                        proof.transaction_generation;
                    bridge.source_ordinal = record.ordinal;
                    bridge.source_version = record.version;
                    bridge.source_collection_version =
                        g_bgm_playback_collection_version;
                    bridge.list_exit_epoch = record.list_exit_epoch;
                    bridge.release_completion_epoch = lifecycle.cleanup_generation;
                    bridge.revocation_epoch = 0;
                    bridge.canonical_token = lifecycle.canonical.encode();
                    bridge.expected_callback_sound = record.sound;
                    bridge.expected_callback_sound_identity = record.sound_identity;
                    bridge.old_canonical_sound =
                        proof.sound;
                    bridge.old_canonical_sound_identity =
                        proof.sound_identity;
                    bridge.old_canonical_request =
                        proof.request;
                    bridge.controller = proof.controller;
                    bridge.controller_proof =
                        proof.controller_proof;
                    bridge.slot = proof.slot;
                    bridge.bgm = proof.bgm;
                    bridge.predecessor_route_generation =
                        proof.route_generation;
                    bridge.predecessor_lease = proof.lease;
                    bridge.predecessor_lifecycle_epoch =
                        proof.lifecycle_state_epoch;
                    bridge.selection_generation = 0;
                    bridge.route_generation = 0;
                    bridge.lease = {};
                    bridge.song_key = 0;
                    proof.custom = 0;
                    proof.source_version = record.version;
                    proof.source_collection_version =
                        g_bgm_playback_collection_version;
                    bridge_diagnostic.succeeded = true;
                    bridge_diagnostic.resulting_source_version = record.version;
                    bridge_diagnostic.resulting_collection_version =
                        g_bgm_playback_collection_version;
                    bridge_diagnostic.bridge_generation = bridge.generation;
                    bridge_diagnostic.release_completion_epoch =
                        bridge.release_completion_epoch;
                    bridge_diagnostic.callback_sound =
                        bridge.expected_callback_sound;
                    bridge_diagnostic.callback_sound_index =
                        bridge.expected_callback_sound_identity.internal_index;
                    bridge_diagnostic.callback_sound_serial =
                        bridge.expected_callback_sound_identity.serial_number;
                    bridge_diagnostic.custom_token_cleared = proof.custom == 0;
                    bridge_diagnostic.source_inactive = !record.active;
            }
            for (const auto& record : g_bgm_playback_borrowers) {
                still_pending = still_pending || (record.active
                    && (record.list_exit || record.shutdown));
            }
            g_bgm_aggregate_exit_pending.store(still_pending,
                std::memory_order_release);
        }
        if (!still_pending) {
            g_bgm_aggregate_exit_requested.store(false,
                std::memory_order_release);
            g_bgm_aggregate_mutation_gate.reopen();
            // Advance the activation revocation epoch after exact aggregate
            // cleanup so an exit-pending action denial cannot suppress the
            // first fresh selection.
            revoke_selection_audio_activation();
        }
        log_bgm_aggregate_mutation("exit_cleanup_complete", "custom_absent",
            lifecycle.ordinal, lifecycle.route_generation,
            lifecycle.cleanup_generation);
    } catch (...) {
    }
}

void anchor_bgm_playback_retirement_candidate(
    const PauseResumeBankSession& candidate) noexcept
{
    try {
        const auto& detached = candidate.detached;
        if (candidate.phase != PauseResumeBankPhase::RetirementCandidate
            || candidate.session_epoch != 0 || candidate.cycle_epoch != 0
            || detached.phase != OnMemoryBankLifecyclePhase::RestoreApplied) return;
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        for (auto& record : g_bgm_playback_borrowers) {
            if (!record.active || record.failed || record.retirement_anchored) continue;
            const bool exact = bgm_playback_retirement_anchor_exact({
                true, true, true,
                detached.sound.object == record.sound
                    && detached.sound.live.internal_index
                        == record.sound_identity.internal_index
                    && detached.sound.live.serial_number
                        == record.sound_identity.serial_number,
                detached.ordinal == record.token_epoch,
                candidate.lifecycle_state_epoch == record.lifecycle_state_epoch,
                detached.canonical.encode() == record.canonical
                    && detached.custom.encode() == record.custom,
                detached.request_handle == record.old_handle,
                candidate.controller == record.controller
                    && uobject_identity_matches(
                        candidate.controller, candidate.controller_identity)
                    && controller_identity_proof_matches_live(
                        record.controller_proof, record.controller)});
            if (!exact) continue;
            const uint64_t initial_predecessor =
                g_bgm_playback_operation_nonce;
            if (!bgm_playback_initial_predecessor_publication_exact(
                    record.ordinal != 0,
                    record.provenance
                        == BgmPlaybackLineageProvenance::PreSession,
                    true, record.operation_predecessor_nonce == 0,
                    initial_predecessor, g_bgm_playback_operation_nonce)) {
                continue;
            }
            record.retirement_anchored = true;
            record.anchor_state_epoch = candidate.lifecycle_state_epoch;
            record.anchor_route_generation = detached.route_generation;
            record.lineage_route_predecessor_generation =
                detached.route_generation;
            record.anchor_cleanup_generation = detached.cleanup_generation;
            record.anchor_request_handle = detached.request_handle;
            record.operation_predecessor_nonce = initial_predecessor;
            // Pre-anchor system operations advance ordering only. A fresh
            // authoritative anchor starts with no reusable boundary or Set.
            record.transition_set_pending = false;
            record.transition_set_nonce = 0;
            record.transition_set_handle = 0;
            record.transition_set_sound = nullptr;
            record.transition_set_sound_identity = {};
            record.canonical_stop_boundary_active = false;
            record.canonical_stop_boundary_consumed = false;
            record.canonical_stop_boundary_nonce = 0;
            record.canonical_stop_boundary_handle = 0;
            record.canonical_stop_boundary_destination = nullptr;
            record.latest_custom_transition_handle = 0;
            record.latest_custom_transition_sound = nullptr;
            record.latest_custom_transition_sound_identity = {};
            record.latest_custom_transition_consumed = false;
            record.latest_custom_transition_route_generation = 0;
            record.latest_custom_transition_lease = {};
            record.latest_custom_transition_owner = nullptr;
            record.latest_custom_transition_owner_thread_id = 0;
            record.latest_custom_transition_command = 0;
            record.latest_custom_transition_owner_state = 0xff;
            record.latest_custom_transition_owner_proof = false;
            record.canonical_set_input_route_generation = 0;
            record.canonical_intermediate_route_generation = 0;
            record.canonical_intermediate_lease = {};
            record.canonical_substrate_relinquished = false;
            record.canonical_substrate_current_exact = false;
            record.canonical_substrate_relinquishment_state =
                BgmPlaybackCanonicalSubstrateRelinquishmentState::None;
            record.canonical_substrate_transaction_generation = 0;
            record.canonical_substrate_relinquishment_epoch = 0;
            record.canonical_substrate_handle = 0;
            record.canonical_substrate_sound = nullptr;
            record.canonical_substrate_sound_identity = {};
            record.canonical_substrate_route_generation = 0;
            record.canonical_substrate_lease = {};
            ++record.version;
            ++g_bgm_playback_collection_version;
            if (g_bgm_playback_observation_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                std::ostringstream out;
                out << "[audio_sead] playback_aggregate status=retirement_candidate_anchored"
                    << " ordinal=" << record.ordinal
                    << " old=0x" << std::hex << record.old_handle << std::dec
                    << " token_epoch=" << record.token_epoch
                    << " lifecycle_state_epoch=" << record.lifecycle_state_epoch
                    << " route=" << record.anchor_route_generation
                    << " cleanup=" << record.anchor_cleanup_generation
                    << " initial_operation_predecessor="
                    << record.operation_predecessor_nonce
                    << " global_operation_nonce="
                    << g_bgm_playback_operation_nonce
                    << " baseline_source=retirement_candidate"
                    << " mutation_authorized=0";
                core::log(core::LogLevel::Info, out.str());
            }
        }
    } catch (...) {
    }
}

void log_bgm_playback_operation(
    std::atomic_uint32_t& budget,
    const char* phase,
    const char* status,
    const char* reason,
    uint64_t ordinal,
    uint64_t version,
    uint64_t nonce,
    void* requested_sound,
    const UObjectLiveHandle& requested_identity,
    void* live_sound,
    uint64_t request,
    uint8_t state,
    const char* kind) noexcept
{
    if (budget.fetch_add(1, std::memory_order_relaxed) >= 32) return;
    try {
        std::ostringstream out;
        out << "[audio_sead] playback_aggregate status=" << status
            << " phase=" << phase
            << " reason=" << reason
            << " ordinal=" << ordinal
            << " version=" << version
            << " nonce=" << nonce
            << " requested_sound=" << requested_sound
            << " requested_identity=" << requested_identity.internal_index
            << ':' << requested_identity.serial_number
            << " live_sound=" << live_sound
            << " request=0x" << std::hex << request << std::dec
            << " state=" << static_cast<unsigned>(state)
            << " kind=" << kind
            << " mutation_authorized=0";
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

using BgmPlaybackResultLogKind = BgmPlaybackDeferredLogKind;
using BgmPlaybackResultLogProposal = BgmPlaybackDeferredLogProposal;

void emit_bgm_playback_result_log(const BgmPlaybackResultLogProposal& p,
    const AggregateMutationReleaseWitness& witness) noexcept
{
    if (!deferred_mutation_log_may_emit({
            p.kind != BgmPlaybackResultLogKind::None,
            p.mutation_complete,
            witness.released()})) return;
    try {
        if (p.kind == BgmPlaybackResultLogKind::SetFailure
            || p.kind == BgmPlaybackResultLogKind::SetSuccess) {
            log_bgm_playback_operation(g_bgm_playback_set_logs,
                "set_commit",
                p.kind == BgmPlaybackResultLogKind::SetSuccess
                    ? "transition_set_committed"
                    : "transition_set_commit_failed",
                p.operation_reason ? p.operation_reason
                                   : set_commit_failure_name(p.set_failure),
                p.ordinal, p.version, p.nonce, p.requested_sound,
                p.requested_identity, p.live_sound, p.request, p.state,
                p.custom ? "custom" : "canonical");
            if (p.kind == BgmPlaybackResultLogKind::SetFailure
                && g_bgm_playback_set_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                std::ostringstream detail;
                detail << "[audio_sead] playback_aggregate status=transition_set_commit_predicate"
                    << " first_failed_predicate="
                    << set_commit_failure_name(p.set_failure)
                    << " ordinal=" << p.ordinal
                    << " expected_version=" << p.expected_version
                    << " current_version=" << p.current_version
                    << " expected_collection=" << p.expected_collection
                    << " current_collection=" << p.current_collection
                    << " expected_route=" << p.expected_route
                    << " current_route=" << p.current_route
                    << " expected_lifecycle=" << p.expected_lifecycle
                    << " current_lifecycle=" << p.current_lifecycle
                    << " previous_nonce=" << p.previous_nonce
                    << " current_nonce=" << p.nonce
                    << " mutation_authorized=0";
                core::log(core::LogLevel::Info, detail.str());
            }
            return;
        }
        if (p.kind == BgmPlaybackResultLogKind::PlayFailure) {
            log_bgm_playback_operation(g_bgm_playback_play_logs,
                "play_join", "transition_play_failed",
                play_failure_name(p.play_failure), p.ordinal, p.version,
                p.nonce, p.requested_sound, p.requested_identity,
                p.live_sound, p.request, p.state,
                p.custom ? "custom" : "canonical");
            if (g_bgm_playback_play_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                std::ostringstream detail;
                detail << "[audio_sead] playback_aggregate status=transition_play_predicate"
                    << " first_failed_predicate="
                    << play_failure_name(p.play_failure)
                    << " ordinal=" << p.ordinal
                    << " expected_route=" << p.expected_route
                    << " current_route=" << p.current_route
                    << " expected_lifecycle=" << p.expected_lifecycle
                    << " current_lifecycle=" << p.current_lifecycle
                    << " expected_nonce=" << p.expected_nonce
                    << " current_nonce=" << p.nonce
                    << " mutation_authorized=0";
                core::log(core::LogLevel::Info, detail.str());
            }
            return;
        }
        if (g_bgm_playback_play_logs.fetch_add(
                1, std::memory_order_relaxed) < 96) {
            std::ostringstream out;
            out << "[audio_sead] playback_aggregate status=transition_play_observed"
                << " kind=" << (p.custom ? "custom" : "canonical")
                << " ordinal=" << p.ordinal
                << " old=0x" << std::hex << p.old_handle
                << " canonical=0x" << p.canonical_handle
                << " custom=0x" << p.custom_handle << std::dec
                << " token_epoch=" << p.token_epoch
                << " lifecycle_state_epoch=" << p.lifecycle_state_epoch
                << " set_epoch=" << p.set_epoch
                << " play_epoch=" << p.play_epoch
                << " set_nonce=" << p.set_nonce
                << " play_nonce=" << p.play_nonce
                << " mutation_authorized=1";
            core::log(core::LogLevel::Info, out.str());
        }
        if (p.custom) {
            log_bgm_aggregate_mutation(
                p.play_owner_rebound ? "custom_play_confirmed"
                                     : "custom_play_unpatched",
                "play_confirmation", p.ordinal, p.version, p.play_nonce);
        }
    } catch (...) {
    }
}

void emit_bgm_playback_preparation_log(
    const BgmPlaybackPreparationLogProposal& proposal,
    BgmPlaybackPreparationEmissionPhase phase,
    const AggregateMutationReleaseWitness& witness) noexcept
{
    if (!bgm_playback_preparation_log_may_emit(
            proposal, phase, witness.released())) return;
    try {
        for (uint32_t i = 0; i < proposal.count; ++i) {
            const auto& entry = proposal.entries[i];
            if (entry.kind == BgmPlaybackPreparationLogKind::LineageInherited) {
                if (g_bgm_playback_observation_logs.fetch_add(
                        1, std::memory_order_relaxed) >= 96) continue;
                std::ostringstream out;
                out << "[audio_sead] playback_aggregate status=lineage_anchor_inherited"
                    << " parent_ordinal=" << entry.parent_ordinal
                    << " child_ordinal=" << entry.child_ordinal
                    << " token_epoch=" << entry.token_epoch
                    << " lifecycle_state_epoch=" << entry.lifecycle_state_epoch
                    << " parent_old=0x" << std::hex << entry.parent_old_handle
                    << " canonical=0x" << entry.canonical_handle
                    << " child_old=0x" << entry.child_old_handle << std::dec
                    << " mutation_authorized=0";
                core::log(core::LogLevel::Info, out.str());
                continue;
            }
            const bool stale_play =
                entry.kind == BgmPlaybackPreparationLogKind::StalePlay;
            log_bgm_playback_operation(
                stale_play ? g_bgm_playback_play_logs : g_bgm_playback_set_logs,
                stale_play ? "play_join" : "set_prepare",
                stale_play ? "transition_play_failed"
                           : (entry.ordinal ? "transition_set_prepared"
                                            : "transition_set_prepare_failed"),
                entry.reason ? entry.reason : "no_exact_record",
                entry.ordinal, entry.version, entry.nonce,
                entry.requested_sound, entry.requested_identity, nullptr,
                entry.request, 0, entry.custom ? "custom" : "canonical");
        }
    } catch (...) {
    }
}

void emit_bgm_aggregate_owner_patch_log(
    const BgmAggregateOwnerPatchLogProposal& proposal,
    const AggregateMutationReleaseWitness& witness) noexcept
{
    if (!bgm_aggregate_owner_patch_log_may_emit(
            proposal, witness.released())) return;
    const char* status = proposal.status
            == BgmAggregateOwnerPatchLogStatus::AuthorizationBlocked
        ? "authorization_blocked" : "patch_failed";
    log_bgm_aggregate_mutation(status, proposal.play ? "play" : "set",
        proposal.ordinal, proposal.version, proposal.predecessor);
}

void invalidate_bgm_playback_operation(
    void* controller, bool set_operation, const char* reason,
    BgmPlaybackDeferredLogProposal* proposal) noexcept
{
    try {
        uint64_t nonce = 0;
        uint64_t ordinal = 0;
        uint64_t version = 0;
        void* requested_sound = nullptr;
        UObjectLiveHandle requested_identity{};
        bool custom = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            if (g_bgm_playback_operation_nonce != UINT64_MAX) {
                nonce = ++g_bgm_playback_operation_nonce;
            }
            bool changed = false;
            BgmPlaybackBorrowerRecord* diagnostic = nullptr;
            for (auto& record : g_bgm_playback_borrowers) {
                if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                        record.failed, record.terminal_callback_retired)
                    || (controller && record.controller != controller))
                    continue;
                if (!diagnostic || record.ordinal > diagnostic->ordinal) {
                    diagnostic = &record;
                }
                record.canonical_stop_boundary_active = false;
                record.canonical_stop_boundary_consumed = true;
                record.transition_set_pending = false;
                record.transition_set_nonce = 0;
                record.transition_set_handle = 0;
                record.transition_command_key = 0;
                record.transition_set_custom = false;
                record.transition_set_sound = nullptr;
                record.transition_set_sound_identity = {};
                ++record.version;
                changed = true;
            }
            if (changed) ++g_bgm_playback_collection_version;
            if (diagnostic) {
                ordinal = diagnostic->ordinal;
                version = diagnostic->version;
                requested_sound = diagnostic->transition_set_sound;
                requested_identity = diagnostic->transition_set_sound_identity;
                custom = diagnostic->transition_set_custom;
            }
        }
        if (proposal) {
            proposal->kind = set_operation
                ? BgmPlaybackDeferredLogKind::SetFailure
                : BgmPlaybackDeferredLogKind::PlayFailure;
            proposal->operation_reason = reason;
            proposal->ordinal = ordinal;
            proposal->version = version;
            proposal->nonce = nonce;
            proposal->requested_sound = requested_sound;
            proposal->requested_identity = requested_identity;
            proposal->custom = custom;
            proposal->mutation_complete = true;
        }
    } catch (...) {
    }
}

void prepare_bgm_playback_set_observation(
    void* controller, void* requested_sound) noexcept
{
    g_bgm_playback_set_observation_candidate = {};
    g_bgm_playback_set_preparation_log = {};
    if (!controller || !requested_sound) return;
    const auto invalidate_boundary = [controller]() noexcept {
        try {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            bool changed = false;
            for (auto& record : g_bgm_playback_borrowers) {
                if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                        record.failed, record.terminal_callback_retired)
                    || record.controller != controller
                    || !record.canonical_stop_boundary_active) continue;
                record.canonical_stop_boundary_active = false;
                record.canonical_stop_boundary_consumed = true;
                ++record.version;
                changed = true;
            }
            if (changed) ++g_bgm_playback_collection_version;
        } catch (...) {
        }
    };
    if (g_bgm_aggregate_exit_requested.load(std::memory_order_acquire)
        || g_bgm_aggregate_exit_pending.load(std::memory_order_acquire)) {
        invalidate_boundary();
        return;
    }
    try {
        UObjectIdentity identity{};
        if (!read_uobject_identity(requested_sound, identity)
            || identity.live.serial_number <= 0
            || !validate_live_uobject_handle(requested_sound, identity.live)) {
            invalidate_boundary();
            return;
        }
        uint64_t route_generation = 0;
        AudioRouteLeaseIdentity lease;
        void* route_slot = nullptr;
        void* route_bgm = nullptr;
        OnMemoryBankDetachedRecord lifecycle;
        uint64_t lifecycle_state_epoch = 0;
        void* current_owner = nullptr;
        uint64_t current_command = 0;
        void* live_slot = nullptr;
        void* live_bgm = nullptr;
        {
            std::lock_guard<std::mutex> state_lock(g_audio_state_mutex);
            route_generation = g_audio_route_state.generation;
            lease = g_audio_route_state.lease_identity;
            route_slot = g_audio_route_state.owned_slot;
            route_bgm = g_audio_route_state.owned_bgm;
            lifecycle = g_onmemory_bank_lifecycle.active();
            lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
        }
        if (!lookup_current_piano_audio_owner(current_owner) || !current_owner
            || !core::safe_read_field(current_owner,
                runtime_layouts::PianoAudioOwner::packed_key, current_command)
            || current_command == 0) {
            invalidate_boundary();
            return;
        }
        const bool live_chain_read = read_controller_bgm_chain(
            controller, live_slot, live_bgm);
        auto& preparation_log = g_bgm_playback_set_preparation_log;
        const auto append_preparation_log = [&preparation_log](
            const BgmPlaybackPreparationLogEntry& entry) noexcept {
            if (preparation_log.count >= preparation_log.entries.size()) return;
            preparation_log.entries[preparation_log.count++] = entry;
        };
        {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        bool changed = false;
        BgmPlaybackBorrowerRecord* diagnostic = nullptr;
        for (auto& record : g_bgm_playback_borrowers) {
            if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)
                || record.controller != controller
                || !record.transition_set_pending) continue;
            if (!diagnostic || record.ordinal > diagnostic->ordinal) {
                diagnostic = &record;
            }
        }
        if (diagnostic) {
            BgmPlaybackPreparationLogEntry entry{};
            entry.kind = BgmPlaybackPreparationLogKind::StalePlay;
            entry.reason = "native_or_observer_fault";
            entry.ordinal = diagnostic->ordinal;
            entry.version = diagnostic->version;
            entry.nonce = diagnostic->transition_set_nonce;
            entry.requested_sound = diagnostic->transition_set_sound;
            entry.requested_identity = diagnostic->transition_set_sound_identity;
            entry.request = diagnostic->transition_set_handle;
            entry.custom = diagnostic->transition_set_custom;
            append_preparation_log(entry);
        }
        for (auto& record : g_bgm_playback_borrowers) {
            if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)
                || record.controller != controller
                || !record.transition_set_pending) continue;
            record.transition_set_pending = false;
            record.transition_set_nonce = 0;
            record.transition_set_handle = 0;
            record.transition_command_key = 0;
            record.transition_set_custom = false;
            record.transition_set_sound = nullptr;
            record.transition_set_sound_identity = {};
            ++record.version;
            changed = true;
        }
        for (auto& record : g_bgm_playback_borrowers) {
            if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)) continue;
            if (!record.retirement_anchored) {
                for (auto& parent : g_bgm_playback_borrowers) {
                    if (!bgm_playback_aggregate_callback_match_eligible(parent.active,
                            parent.failed, parent.terminal_callback_retired)) continue;
                    const uint64_t lineage_route_predecessor =
                        bgm_playback_descendant_route_predecessor(
                            parent.canonical_stop_boundary_route_generation,
                            parent.latest_custom_transition_route_generation,
                            record.route_generation);
                    if (lineage_route_predecessor == 0
                        || !bgm_playback_lineage_inheritance_exact({
                            parent.retirement_anchored,
                            parent.custom == record.custom
                                && parent.token_epoch == record.token_epoch,
                            parent.anchor_state_epoch
                                == record.lifecycle_state_epoch,
                            parent.lease == record.lease,
                            parent.controller == record.controller
                                && parent.slot == record.slot
                                && parent.bgm == record.bgm,
                        bgm_playback_child_claim_exact(
                            parent.latest_custom_transition_consumed,
                            parent.ordinal, record.ordinal,
                            parent.latest_custom_transition_handle,
                            record.old_handle),
                            parent.latest_custom_transition_sound == record.sound
                                && parent.latest_custom_transition_sound_identity.internal_index
                                    == record.sound_identity.internal_index
                                && parent.latest_custom_transition_sound_identity.serial_number
                                    == record.sound_identity.serial_number,
                            bgm_playback_alternating_transition_exact(
                                parent.old_handle,
                                parent.canonical_intermediate_handle,
                                parent.latest_custom_transition_handle,
                                parent.expected_new_sound
                                    && parent.expected_new_sound_identity.serial_number > 0,
                                parent.latest_custom_transition_sound == record.sound
                                    && parent.latest_custom_transition_sound_identity.internal_index
                                        == record.sound_identity.internal_index
                                    && parent.latest_custom_transition_sound_identity.serial_number
                                        == record.sound_identity.serial_number)})) continue;
                    record.retirement_anchored = true;
                    record.anchor_state_epoch = parent.anchor_state_epoch;
                    record.anchor_route_generation = parent.anchor_route_generation;
                    record.lineage_route_predecessor_generation =
                        lineage_route_predecessor;
                    record.anchor_cleanup_generation = parent.anchor_cleanup_generation;
                    record.anchor_request_handle = parent.anchor_request_handle;
                    record.operation_predecessor_nonce =
                        parent.transition_play_nonce;
                    parent.latest_custom_transition_consumed = true;
                    parent.latest_custom_transition_child_ordinal = record.ordinal;
                    parent.terminal_state =
                        BgmPlaybackLineageTerminalState::SupersededPending;
                    ++parent.version;
                    ++record.version;
                    changed = true;
                    BgmPlaybackPreparationLogEntry entry{};
                    entry.kind = BgmPlaybackPreparationLogKind::LineageInherited;
                    entry.parent_ordinal = parent.ordinal;
                    entry.child_ordinal = record.ordinal;
                    entry.token_epoch = record.token_epoch;
                    entry.lifecycle_state_epoch = record.lifecycle_state_epoch;
                    entry.parent_old_handle = parent.old_handle;
                    entry.canonical_handle = parent.canonical_intermediate_handle;
                    entry.child_old_handle = record.old_handle;
                    append_preparation_log(entry);
                    break;
                }
            }
        }
        BgmPlaybackBorrowerRecord* target = nullptr;
        bool target_from_boundary = false;
        bool saw_boundary = false;
        const char* prepare_reason = "no_exact_record";
        for (auto& record : g_bgm_playback_borrowers) {
            if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)
                || !record.canonical_stop_boundary_active) continue;
            saw_boundary = true;
            const bool exact = bgm_playback_boundary_set_exact({
                record.canonical_stop_boundary_active,
                !record.canonical_stop_boundary_consumed,
                record.canonical_stop_boundary_parent_ordinal == record.ordinal
                    && record.canonical_stop_boundary_parent_version != 0
                    && bgm_playback_exact_successor(
                        record.canonical_stop_boundary_parent_version,
                        record.version),
                requested_sound == record.sound,
                identity.live.internal_index
                        == record.sound_identity.internal_index
                    && identity.live.serial_number
                        == record.sound_identity.serial_number,
                record.controller == controller && live_chain_read
                    && live_slot == record.slot && live_bgm == record.bgm
                    && controller_identity_proof_matches_live(
                        record.controller_proof, controller)
                    && canonical_boundary_destination_exact(record),
                record.owner == current_owner
                    && record.owner_thread_id == GetCurrentThreadId(),
                current_command == record.canonical_stop_boundary_command
                    && current_command == record.anchored_custom_command,
                route_generation
                        == record.canonical_stop_boundary_route_generation
                    && route_generation != 0 && lease == record.lease
                    && lifecycle
                    && lifecycle.ordinal
                        == record.canonical_stop_boundary_token_epoch
                    && lifecycle_state_epoch
                        == record.canonical_stop_boundary_lifecycle_state_epoch
                    && lifecycle.canonical.encode() == record.canonical
                    && lifecycle.custom.encode() == record.custom
                    && lifecycle.sound.object == record.sound
                    && lifecycle.sound.live.internal_index
                        == record.sound_identity.internal_index
                    && lifecycle.sound.live.serial_number
                        == record.sound_identity.serial_number,
                bgm_playback_operation_nonce_next(
                    record.canonical_stop_boundary_nonce,
                    g_bgm_playback_operation_nonce + 1),
            });
            if (exact && (!target || record.ordinal > target->ordinal)) {
                target = &record;
                target_from_boundary = true;
                prepare_reason = "canonical_stop_boundary";
            } else if (!exact) {
                record.canonical_stop_boundary_active = false;
                record.canonical_stop_boundary_consumed = true;
                ++record.version;
                changed = true;
                prepare_reason = "canonical_stop_boundary_mismatch";
            }
        }
        for (auto& record : g_bgm_playback_borrowers) {
            if (target_from_boundary || saw_boundary) break;
            if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)
                || !record.retirement_anchored) continue;
            if (!bgm_playback_set_correlation_exact({
                    record.retirement_anchored,
                    record.controller == controller
                        && controller_identity_proof_matches_live(
                            record.controller_proof, controller)
                        && record.owner == current_owner
                        && record.owner_thread_id == GetCurrentThreadId()
                        && record.desired_key == current_command,
                    record.slot == route_slot && record.bgm == route_bgm,
                    route_generation >= record.route_generation
                        && route_generation != 0 && lease == record.lease,
                    lifecycle.phase == OnMemoryBankLifecyclePhase::RestoreApplied
                        && lifecycle.ordinal == record.token_epoch
                        && lifecycle_state_epoch == record.anchor_state_epoch
                        && lifecycle.route_generation
                            == record.anchor_route_generation
                        && lifecycle.cleanup_generation
                            == record.anchor_cleanup_generation
                        && lifecycle.sound.object == record.sound
                        && lifecycle.sound.live.internal_index
                            == record.sound_identity.internal_index
                        && lifecycle.sound.live.serial_number
                            == record.sound_identity.serial_number
                        && lifecycle.canonical.encode() == record.canonical
                        && lifecycle.custom.encode() == record.custom,
                    record.old_handle != 0})) continue;
            if (!target || record.ordinal > target->ordinal) target = &record;
        }
        if (changed) ++g_bgm_playback_collection_version;
        if (target) {
            auto& candidate = g_bgm_playback_set_observation_candidate;
            candidate.active = true;
            candidate.ordinal = target->ordinal;
            candidate.version = target->version;
            candidate.collection_version = g_bgm_playback_collection_version;
            candidate.route_generation = route_generation;
            candidate.token_epoch = lifecycle.ordinal;
            candidate.lifecycle_state_epoch = lifecycle_state_epoch;
            candidate.boundary_nonce = target_from_boundary
                ? target->canonical_stop_boundary_nonce : 0;
            candidate.operation_predecessor_nonce = target_from_boundary
                ? target->canonical_stop_boundary_nonce
                : target->operation_predecessor_nonce;
            candidate.owner_command = current_command;
            candidate.canonical_token = lifecycle.canonical.encode();
            candidate.custom_token = lifecycle.custom.encode();
            candidate.lease = lease;
            candidate.owner = current_owner;
            candidate.from_canonical_stop_boundary = target_from_boundary;
            candidate.controller = controller;
            candidate.slot = live_slot;
            candidate.bgm = live_bgm;
            candidate.requested_sound = requested_sound;
            candidate.requested_sound_identity = identity.live;
        }
        BgmPlaybackPreparationLogEntry prepared{};
        prepared.kind = BgmPlaybackPreparationLogKind::SetPrepared;
        prepared.reason = prepare_reason;
        prepared.ordinal = target ? target->ordinal : 0;
        prepared.version = target ? target->version : 0;
        prepared.requested_sound = requested_sound;
        prepared.requested_identity = identity.live;
        prepared.custom = target && requested_sound == target->sound;
        append_preparation_log(prepared);
        preparation_log.mutation_complete = true;
        }
    } catch (...) {
        g_bgm_playback_set_observation_candidate = {};
        g_bgm_playback_set_preparation_log = {};
        invalidate_boundary();
    }
}

void observe_bgm_playback_set_result(
    void* controller, void* requested_sound, bool native_succeeded,
    BgmPlaybackDeferredLogProposal* proposal) noexcept
{
    if (!proposal) return;
    try {
        auto candidate = g_bgm_playback_set_observation_candidate;
        if (!candidate.active) {
            invalidate_bgm_playback_operation(
                controller, true, "unmatched_or_invalid_set", proposal);
            return;
        }
        if (!native_succeeded || controller != candidate.controller) {
            invalidate_bgm_playback_operation(controller, true,
                !native_succeeded ? "native_failed" : "controller_mismatch",
                proposal);
            g_bgm_playback_set_observation_candidate = {};
            return;
        }
        if (requested_sound != candidate.requested_sound) {
            invalidate_bgm_playback_operation(
                controller, true, "requested_sound_mismatch", proposal);
            g_bgm_playback_set_observation_candidate = {};
            return;
        }
        void* slot = nullptr;
        void* bgm = nullptr;
        void* applied_sound = nullptr;
        uint64_t handle = 0;
        uint8_t state = 0;
        UObjectIdentity applied_identity{};
        const bool applied = read_controller_audio_chain(
                controller, slot, bgm, applied_sound, handle, state)
            && applied_sound == requested_sound && handle != 0
            && (state == 2 || state == 4)
            && read_uobject_identity(applied_sound, applied_identity)
            && applied_identity.live.internal_index
                == candidate.requested_sound_identity.internal_index
            && applied_identity.live.serial_number
                == candidate.requested_sound_identity.serial_number
            && validate_live_uobject_handle(
                applied_sound, applied_identity.live);
        void* current_owner = nullptr;
        uint64_t intended_command_key = 0;
        const bool command_observed = lookup_current_piano_audio_owner(current_owner)
            && current_owner
            && core::safe_read_field(current_owner,
                runtime_layouts::PianoAudioOwner::packed_key,
                intended_command_key)
            && intended_command_key != 0;
        uint64_t post_route_generation = 0;
        AudioRouteLeaseIdentity post_lease;
        OnMemoryBankDetachedRecord post_lifecycle;
        uint64_t post_lifecycle_state_epoch = 0;
        {
            std::lock_guard<std::mutex> state_lock(g_audio_state_mutex);
            post_route_generation = g_audio_route_state.generation;
            post_lease = g_audio_route_state.lease_identity;
            post_lifecycle = g_onmemory_bank_lifecycle.active();
            post_lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
        }
        const bool post_snapshot_exact = bgm_playback_post_set_snapshot_exact(
            slot == candidate.slot && bgm == candidate.bgm,
            bgm_playback_exact_successor(
                candidate.route_generation, post_route_generation),
            post_lease == candidate.lease,
            post_lifecycle && post_lifecycle_state_epoch
                == candidate.lifecycle_state_epoch,
            post_lifecycle && post_lifecycle.ordinal == candidate.token_epoch,
            post_lifecycle
                && post_lifecycle.canonical.encode() == candidate.canonical_token
                && post_lifecycle.custom.encode() == candidate.custom_token,
            current_owner == candidate.owner,
            command_observed
                && intended_command_key == candidate.owner_command);
        {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        BgmPlaybackBorrowerRecord* target = nullptr;
        for (auto& record : g_bgm_playback_borrowers) {
            if (bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)
                && record.ordinal == candidate.ordinal) {
                target = &record;
                break;
            }
        }
        const uint64_t nonce = g_bgm_playback_operation_nonce != UINT64_MAX
            ? g_bgm_playback_operation_nonce + 1 : 0;
        const uint64_t previous_nonce = candidate.operation_predecessor_nonce;
        const bool boundary_state_exact = !candidate.from_canonical_stop_boundary
            || (target && target->canonical_stop_boundary_active
                && !target->canonical_stop_boundary_consumed
                && target->canonical_stop_boundary_nonce
                    == candidate.boundary_nonce);
        const bool request_generation_exact = !candidate.from_canonical_stop_boundary
            || (target && (handle >> 32)
                > (target->canonical_stop_boundary_handle >> 32));
        const BgmPlaybackSetCommitFacts commit_facts{
            target != nullptr,
            target && !target->failed,
            target && target->version == candidate.version,
            g_bgm_playback_collection_version == candidate.collection_version,
            target && current_owner == target->owner
                && current_owner == candidate.owner,
            target && target->owner_thread_id == GetCurrentThreadId(),
            target && command_observed
                && intended_command_key == candidate.owner_command
                && intended_command_key
                    == (candidate.from_canonical_stop_boundary
                        ? target->anchored_custom_command : target->desired_key),
            post_snapshot_exact,
            boundary_state_exact,
            request_generation_exact,
            applied && command_observed && post_snapshot_exact,
            bgm_playback_operation_nonce_next(previous_nonce, nonce),
        };
        const auto first_failure =
            bgm_playback_set_commit_first_failure(commit_facts);
        const bool current = first_failure == BgmPlaybackSetCommitFailure::None;
        if (!bgm_playback_set_commit_exact(
                native_succeeded, current,
                applied && command_observed && post_snapshot_exact,
                previous_nonce, nonce)) {
            if (nonce != 0) g_bgm_playback_operation_nonce = nonce;
            *proposal = {
                BgmPlaybackResultLogKind::SetFailure,
                first_failure,
                BgmPlaybackPlayFailure::None,
                !native_succeeded ? "native_failed"
                    : !current ? "stale_candidate"
                    : !command_observed ? "command_unreadable"
                    : intended_command_key != candidate.owner_command
                        ? "command_drift"
                    : !post_snapshot_exact ? "post_set_snapshot_drift"
                    : !applied ? "postcondition"
                    : "boundary_or_version_drift",
                target ? target->ordinal : candidate.ordinal,
                target ? target->version : candidate.version,
                nonce,
                requested_sound,
                candidate.requested_sound_identity,
                applied_sound,
                handle,
                state,
                target && requested_sound == target->sound,
                candidate.version,
                target ? target->version : 0,
                candidate.collection_version,
                g_bgm_playback_collection_version,
                candidate.route_generation + 1,
                post_route_generation,
                candidate.lifecycle_state_epoch,
                post_lifecycle_state_epoch,
                previous_nonce,
            };
            if (target) {
                target->transition_set_pending = false;
                target->transition_set_nonce = 0;
                target->canonical_stop_boundary_active = false;
                target->canonical_stop_boundary_consumed = true;
                target->transition_set_handle = 0;
                target->transition_set_sound = nullptr;
                target->transition_set_sound_identity = {};
                if (candidate.from_canonical_stop_boundary) {
                    target->canonical_stop_boundary_active = false;
                    target->canonical_stop_boundary_consumed = true;
                }
                ++target->version;
                ++g_bgm_playback_collection_version;
            }
            g_bgm_playback_set_observation_candidate = {};
            proposal->mutation_complete = true;
            return;
        }
        g_bgm_playback_operation_nonce = nonce;
        target->transition_set_pending = true;
        target->transition_set_sound = requested_sound;
        target->transition_set_sound_identity = applied_identity.live;
        target->transition_set_epoch = ++g_bgm_playback_observation_epoch;
        target->transition_set_nonce = nonce;
        target->transition_set_handle = handle;
        target->transition_command_key = intended_command_key;
        target->transition_set_custom =
            classify_bgm_playback_set_provenance(
                true,
                requested_sound == target->sound,
                applied_identity.live.internal_index
                        == target->sound_identity.internal_index
                    && applied_identity.live.serial_number
                        == target->sound_identity.serial_number)
            == BgmPlaybackSetProvenance::Custom;
        if (!target->transition_set_custom) {
            target->canonical_set_input_route_generation =
                candidate.route_generation;
        }
        target->transition_set_owner_rebound =
            target->transition_set_custom
            && g_bgm_aggregate_set_owner_rebound;
        target->transition_play_owner_rebound = false;
        target->transition_route_generation = post_route_generation;
        target->transition_token_epoch = candidate.token_epoch;
        target->transition_lifecycle_state_epoch =
            post_lifecycle_state_epoch;
        target->transition_lease = post_lease;
        target->transition_canonical_token = post_lifecycle.canonical.encode();
        target->transition_custom_token = post_lifecycle.custom.encode();
        if (candidate.from_canonical_stop_boundary) {
            target->canonical_stop_boundary_active = false;
            target->canonical_stop_boundary_consumed = true;
        }
        if (!target->set_observed && requested_sound != target->sound) {
            target->set_observed = true;
            target->expected_new_sound = requested_sound;
            target->expected_new_sound_identity = applied_identity.live;
        }
        ++target->version;
        ++g_bgm_playback_collection_version;
        g_bgm_playback_set_observation_candidate = {};
        proposal->kind = BgmPlaybackResultLogKind::SetSuccess;
        proposal->operation_reason = "ready";
        proposal->ordinal = target->ordinal;
        proposal->version = target->version;
        proposal->nonce = nonce;
        proposal->requested_sound = requested_sound;
        proposal->requested_identity = applied_identity.live;
        proposal->live_sound = applied_sound;
        proposal->request = handle;
        proposal->state = state;
        proposal->custom = target->transition_set_custom;
        proposal->mutation_complete = true;
        }
    } catch (...) {
        g_bgm_playback_set_observation_candidate = {};
        invalidate_bgm_playback_operation(
            controller, true, "observer_fault", proposal);
    }
}

class BgmPlaybackSetObservationScope final {
public:
    BgmPlaybackSetObservationScope(void* controller, void* requested_sound)
    {
        prepare_bgm_playback_set_observation(controller, requested_sound);
    }

    ~BgmPlaybackSetObservationScope()
    {
        g_bgm_playback_set_observation_candidate = {};
    }
};

void observe_bgm_playback_play_failed(void* controller,
    BgmPlaybackDeferredLogProposal* proposal) noexcept
{
    invalidate_bgm_playback_operation(
        controller, false, "native_or_observer_fault", proposal);
}

void observe_bgm_playback_play_transition(void* controller,
    const BgmPlaybackPendingPlayOutcome& outcome,
    BgmPlaybackUnpublishedPlayDiagnostic* unpublished,
    BgmPlaybackDeferredLogProposal* proposal) noexcept
{
    if (!proposal) return;
    if (!controller) {
        invalidate_bgm_playback_operation(
            nullptr, false, "null_controller", proposal);
        return;
    }
    try {
        const void* slot = outcome.slot;
        const void* bgm = outcome.bgm;
        void* sound = outcome.sound;
        const uint64_t handle = outcome.request;
        const uint8_t state = outcome.state;
        const bool chain_read = outcome.chain_read
            && outcome.controller == controller;
        const bool identity_read = outcome.sound_identity_read;
        const UObjectLiveHandle sound_identity = outcome.sound_identity;
        void* owner = outcome.owner;
        const uint64_t command = outcome.owner_command;
        const uint8_t owner_state = outcome.owner_state;
        const bool owner_read = outcome.owner_read;
        const bool owner_state_read = outcome.owner_state_read;
        const uint64_t route_generation = outcome.route_generation;
        const AudioRouteLeaseIdentity lease = outcome.lease;
        const uint64_t lifecycle_state_epoch = outcome.lifecycle_state_epoch;
        {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        BgmPlaybackBorrowerRecord* target = nullptr;
        BgmPlaybackBorrowerRecord* diagnostic = nullptr;
        bool diagnostic_matches_outcome = false;
        BgmPlaybackPlayFailure diagnostic_failure =
            BgmPlaybackPlayFailure::PendingSet;
        BgmPlaybackPlayFacts diagnostic_facts{};
        const uint64_t play_nonce = outcome.play_nonce;
        for (auto& record : g_bgm_playback_borrowers) {
            if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)
                || record.controller != controller)
                continue;
            const BgmPlaybackPlayFacts facts{
                record.retirement_anchored && record.transition_set_pending,
                outcome.active && outcome.publication_authority_exact
                    && outcome.ordinal == record.ordinal
                    && outcome.record_version == record.version
                    && outcome.collection_version
                        == g_bgm_playback_collection_version,
                chain_read && record.slot == slot && record.bgm == bgm
                    && controller_identity_proof_matches(
                        record.controller_proof, outcome.controller_proof),
                owner_read && record.owner == owner
                    && record.owner_thread_id == outcome.owner_thread,
                record.transition_set_custom
                    ? bgm_playback_custom_play_post_native_owner_phase_exact(
                        owner_read, owner_state_read, owner_state,
                        record.transition_command_key, command)
                    : owner_read && record.transition_command_key == command,
                route_generation == record.transition_route_generation
                    && (!record.transition_set_custom
                        || bgm_playback_confirmed_custom_route_transition_exact(
                            record.lineage_route_predecessor_generation,
                            record.canonical_set_input_route_generation,
                            record.canonical_intermediate_route_generation,
                            record.canonical_stop_boundary_route_generation,
                            route_generation)),
                lease == record.transition_lease
                    && (!record.transition_set_custom
                        || bgm_playback_confirmed_custom_lease_transition_exact(
                            record.lease,
                            record.canonical_intermediate_lease, lease)),
                lifecycle_state_epoch != 0 && lifecycle_state_epoch
                    == record.transition_lifecycle_state_epoch,
                outcome.token_ordinal != 0
                    && outcome.token_ordinal == record.transition_token_epoch,
                outcome.canonical_token != 0 && outcome.custom_token != 0
                    && outcome.canonical_token
                        == record.transition_canonical_token
                    && outcome.custom_token == record.transition_custom_token,
                identity_read && record.transition_set_sound == sound
                    && record.transition_set_sound_identity.internal_index
                        == sound_identity.internal_index
                    && record.transition_set_sound_identity.serial_number
                        == sound_identity.serial_number,
                chain_read && record.transition_set_handle == handle
                    && handle != 0
                    && (handle >> 32) > (record.old_handle >> 32),
                chain_read && state == 4,
                bgm_playback_operation_nonce_next(
                    record.transition_set_nonce, play_nonce),
            };
            const auto failure = bgm_playback_play_first_failure(facts);
            const bool matches_outcome = record.ordinal == outcome.ordinal;
            if (!diagnostic
                || (!diagnostic_matches_outcome
                    && (matches_outcome
                        || record.ordinal > diagnostic->ordinal))) {
                diagnostic = &record;
                diagnostic_matches_outcome = matches_outcome;
                diagnostic_failure = failure;
                diagnostic_facts = facts;
            }
            if (failure == BgmPlaybackPlayFailure::None
                && (!target || record.ordinal > target->ordinal)) {
                target = &record;
            }
        }
        if (!target || !bgm_playback_play_join_exact(
                true, target->transition_set_pending, true,
                target->transition_set_nonce, play_nonce)) {
            if (outcome.active) {
                if (unpublished) {
                    unpublished->proposed = true;
                    unpublished->publication_authority_exact =
                        outcome.publication_authority_exact;
                    unpublished->first_failure = diagnostic_failure;
                    unpublished->ordinal = diagnostic
                        ? diagnostic->ordinal : outcome.ordinal;
                    unpublished->expected_record_version =
                        outcome.record_version;
                    unpublished->current_record_version = diagnostic
                        ? diagnostic->version : 0;
                    unpublished->expected_collection_version =
                        outcome.collection_version;
                    unpublished->current_collection_version =
                        g_bgm_playback_collection_version;
                    unpublished->expected_route = diagnostic
                        ? diagnostic->transition_route_generation : 0;
                    unpublished->current_route = route_generation;
                    unpublished->expected_lifecycle = diagnostic
                        ? diagnostic->transition_lifecycle_state_epoch : 0;
                    unpublished->current_lifecycle = lifecycle_state_epoch;
                    unpublished->expected_request = diagnostic
                        ? diagnostic->transition_set_handle
                        : outcome.authorized_request;
                    unpublished->current_request = handle;
                    unpublished->expected_command = diagnostic
                        ? diagnostic->transition_command_key : 0;
                    unpublished->captured_owner_command = command;
                    unpublished->captured_owner_state_read = owner_state_read;
                    unpublished->captured_owner_state = owner_state;
                    unpublished->expected_nonce = diagnostic
                        ? (diagnostic->transition_set_nonce
                                != std::numeric_limits<uint64_t>::max()
                            ? diagnostic->transition_set_nonce + 1 : 0)
                        : (outcome.set_nonce
                                != std::numeric_limits<uint64_t>::max()
                            ? outcome.set_nonce + 1 : 0);
                    unpublished->current_nonce = play_nonce;
                    unpublished->pending_set_exact =
                        diagnostic_facts.pending_set;
                    unpublished->controller_chain_exact =
                        diagnostic_facts.controller_chain_exact;
                    unpublished->owner_thread_exact =
                        diagnostic_facts.owner_thread_exact;
                    unpublished->command_exact = diagnostic_facts.command_exact;
                    unpublished->route_exact = diagnostic_facts.route_exact;
                    unpublished->lease_exact = diagnostic_facts.lease_exact;
                    unpublished->lifecycle_exact =
                        diagnostic_facts.lifecycle_state_exact;
                    unpublished->token_ordinal_exact =
                        diagnostic_facts.token_ordinal_exact;
                    unpublished->token_values_exact =
                        diagnostic_facts.token_values_exact;
                    unpublished->request_sound_identity_exact =
                        diagnostic_facts.request_sound_identity_exact;
                    unpublished->request_handle_exact =
                        diagnostic_facts.request_handle_exact;
                    unpublished->state4_exact = diagnostic_facts.state4;
                    unpublished->nonce_exact = diagnostic_facts.nonce_exact;
                }
                return;
            }
            if (play_nonce != 0) g_bgm_playback_operation_nonce = play_nonce;
            proposal->kind = BgmPlaybackResultLogKind::PlayFailure;
            proposal->play_failure = diagnostic_failure;
            proposal->operation_reason =
                play_failure_name(diagnostic_failure);
            proposal->ordinal = diagnostic ? diagnostic->ordinal : 0;
            proposal->version = diagnostic ? diagnostic->version : 0;
            proposal->nonce = play_nonce;
            proposal->requested_sound = diagnostic
                ? diagnostic->transition_set_sound : nullptr;
            proposal->requested_identity = diagnostic
                ? diagnostic->transition_set_sound_identity
                : UObjectLiveHandle{};
            proposal->live_sound = sound;
            proposal->request = handle;
            proposal->state = state;
            proposal->custom = diagnostic
                && diagnostic->transition_set_custom;
            proposal->expected_route = diagnostic
                ? diagnostic->transition_route_generation : 0;
            proposal->current_route = route_generation;
            proposal->expected_lifecycle = diagnostic
                ? diagnostic->transition_lifecycle_state_epoch : 0;
            proposal->current_lifecycle = lifecycle_state_epoch;
            proposal->expected_nonce = diagnostic
                ? diagnostic->transition_set_nonce + 1 : 0;
            bool changed = false;
            for (auto& record : g_bgm_playback_borrowers) {
                if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                        record.failed, record.terminal_callback_retired)
                    || record.controller != controller) continue;
                record.transition_set_pending = false;
                record.transition_set_nonce = 0;
                record.transition_set_handle = 0;
                record.transition_command_key = 0;
                record.transition_set_custom = false;
                record.transition_set_sound = nullptr;
                record.transition_set_sound_identity = {};
                record.canonical_stop_boundary_active = false;
                record.canonical_stop_boundary_consumed = true;
                ++record.version;
                changed = true;
            }
            if (changed) ++g_bgm_playback_collection_version;
            proposal->mutation_complete = true;
            return;
        }
        g_bgm_playback_operation_nonce = play_nonce;
        const bool custom_transition = target->transition_set_custom
            && sound == target->sound
            && sound_identity.internal_index
                == target->sound_identity.internal_index
            && sound_identity.serial_number
                == target->sound_identity.serial_number;
        if (custom_transition) {
            if (target->canonical_intermediate_handle == 0
                || (handle >> 32)
                    <= (target->canonical_intermediate_handle >> 32)) {
                return;
            }
        }
        const bool consumable = bgm_playback_transition_consumable(
            target->transition_set_pending, state == 4,
            target->transition_set_handle == handle,
            sound_identity.serial_number > 0,
            custom_transition
                && target->latest_custom_transition_handle != 0
                && !target->latest_custom_transition_consumed);
        if (!consumable) {
            return;
        }
        if (custom_transition) {
            target->latest_custom_transition_handle = handle;
            target->latest_custom_transition_sound = sound;
            target->latest_custom_transition_sound_identity =
                sound_identity;
            target->latest_custom_transition_consumed = false;
            target->latest_custom_transition_absent = false;
            target->latest_custom_transition_child_ordinal = 0;
            target->latest_custom_transition_route_generation =
                route_generation;
            target->latest_custom_transition_lease = lease;
            target->latest_custom_transition_owner = owner;
            target->latest_custom_transition_owner_thread_id =
                outcome.owner_thread;
            target->latest_custom_transition_command = command;
            target->latest_custom_transition_owner_state = owner_state;
            target->latest_custom_transition_owner_proof = true;
            target->terminal_state = BgmPlaybackLineageTerminalState::Active;
            target->transition_play_owner_rebound =
                target->transition_set_owner_rebound
                && g_bgm_aggregate_play_owner_rebound;
        } else {
            target->canonical_intermediate_handle = handle;
            target->canonical_intermediate_absent = false;
            target->canonical_intermediate_route_generation = route_generation;
            target->canonical_intermediate_lease = lease;
            target->terminal_state = BgmPlaybackLineageTerminalState::Active;
            target->transition_play_owner_rebound = false;
        }
        target->transition_set_pending = false;
        target->transition_play_nonce = play_nonce;
        target->operation_predecessor_nonce = play_nonce;
        target->transition_play_epoch =
            ++g_bgm_playback_observation_epoch;
        ++target->version;
        ++g_bgm_playback_collection_version;
        proposal->kind = BgmPlaybackResultLogKind::PlaySuccess;
        proposal->custom = custom_transition;
        proposal->ordinal = target->ordinal;
        proposal->version = target->version;
        proposal->old_handle = target->old_handle;
        proposal->canonical_handle =
            target->canonical_intermediate_handle;
        proposal->custom_handle =
            target->latest_custom_transition_handle;
        proposal->token_epoch = target->token_epoch;
        proposal->lifecycle_state_epoch =
            target->lifecycle_state_epoch;
        proposal->set_epoch = target->transition_set_epoch;
        proposal->play_epoch = target->transition_play_epoch;
        proposal->set_nonce = target->transition_set_nonce;
        proposal->play_nonce = target->transition_play_nonce;
        proposal->play_owner_rebound =
            target->transition_play_owner_rebound;
        proposal->mutation_complete = true;
        }
    } catch (...) {
        observe_bgm_playback_play_failed(controller, proposal);
    }
}

void __fastcall bgm_playback_transfer_detour(void* destination, void* source)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::PlaybackTransfer);
    PlaybackTransferObservationContext context{};
    BgmPlaybackTransferSnapshot source_before{};
    try {
        if (callback_scope) {
            (void)read_playback_transfer_snapshot(source, source_before);
            context = snapshot_playback_transfer_context(source_before);
        }
    } catch (...) {
    }
    if (g_original_bgm_playback_transfer) {
        g_original_bgm_playback_transfer(destination, source);
    }
    if (!callback_scope) return;
    try {
        BgmPlaybackTransferSnapshot source_after{};
        BgmPlaybackTransferSnapshot destination_after{};
        (void)read_playback_transfer_snapshot(source, source_after);
        (void)read_playback_transfer_snapshot(destination, destination_after);
        if (context.pre_session_candidate && !context.exact) {
            if (g_bgm_playback_observation_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                std::ostringstream out;
                out << "[audio_sead] playback_aggregate status=presession_capture_blocked"
                    << " reason=" << context.blocked_reason
                    << " mutation_authorized=0";
                core::log(core::LogLevel::Info, out.str());
            }
        }
        const bool transfer_postconditions = source != destination
            && bgm_playback_transfer_source_zeroed(source_after)
            && bgm_playback_transfer_destination_adopted(
                source_before, destination_after);
        if (context.canonical_stop_boundary) {
            if (g_bgm_aggregate_exit_requested.load(std::memory_order_acquire)
                || g_bgm_aggregate_exit_pending.load(
                    std::memory_order_acquire)) {
                {
                    std::lock_guard<std::mutex> lock(
                        g_bgm_playback_borrower_mutex);
                    bool changed = false;
                    for (auto& record : g_bgm_playback_borrowers) {
                        if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                                record.failed, record.terminal_callback_retired)
                            || !record.canonical_stop_boundary_active) continue;
                        record.canonical_stop_boundary_active = false;
                        record.canonical_stop_boundary_consumed = true;
                        ++record.version;
                        changed = true;
                    }
                    if (changed) ++g_bgm_playback_collection_version;
                }
                log_bgm_aggregate_mutation(
                    "exit_boundary_blocked", "transfer", 0, 0, 0);
                return;
            }
            bool admitted = false;
            const char* reason = transfer_postconditions
                ? "stale_parent" : "transfer_postcondition";
            UObjectIdentity canonical_identity{};
            const bool identity_exact = source_before.sound
                && read_uobject_identity(
                    reinterpret_cast<void*>(source_before.sound),
                    canonical_identity)
                && canonical_identity.live.serial_number > 0
                && validate_live_uobject_handle(
                    reinterpret_cast<void*>(source_before.sound),
                    canonical_identity.live);
            if (context.exact && transfer_postconditions && identity_exact) {
                std::lock_guard<std::mutex> lock(
                    g_bgm_playback_borrower_mutex);
                BgmPlaybackBorrowerRecord* parent = nullptr;
                for (auto& record : g_bgm_playback_borrowers) {
                    if (bgm_playback_aggregate_callback_match_eligible(record.active,
                            record.failed, record.terminal_callback_retired)
                        && record.ordinal == context.parent_ordinal) {
                        parent = &record;
                        break;
                    }
                }
                if (parent && parent->version == context.parent_version
                    && g_bgm_playback_collection_version
                        == context.collection_version
                    && parent->canonical_intermediate_handle
                        == source_before.moved_handle
                    && parent->expected_new_sound
                        == reinterpret_cast<void*>(source_before.sound)
                    && parent->expected_new_sound_identity.internal_index
                        == canonical_identity.live.internal_index
                    && parent->expected_new_sound_identity.serial_number
                        == canonical_identity.live.serial_number
                    && !parent->canonical_stop_boundary_active
                    && !parent->canonical_stop_boundary_consumed) {
                    const uint64_t boundary_nonce =
                        g_bgm_playback_operation_nonce + 1;
                    if (bgm_playback_operation_nonce_next(
                            g_bgm_playback_operation_nonce, boundary_nonce)
                        && bgm_playback_operation_nonce_next(
                            parent->transition_play_nonce, boundary_nonce)) {
                        g_bgm_playback_operation_nonce = boundary_nonce;
                        parent->canonical_stop_boundary_active = true;
                        parent->canonical_stop_boundary_consumed = false;
                        parent->canonical_stop_boundary_nonce = boundary_nonce;
                        parent->canonical_stop_boundary_handle =
                            source_before.moved_handle;
                        parent->canonical_stop_boundary_parent_ordinal =
                            parent->ordinal;
                        parent->canonical_stop_boundary_parent_version =
                            parent->version;
                        parent->canonical_stop_boundary_route_generation =
                            context.boundary_route_generation;
                        parent->canonical_stop_boundary_token_epoch =
                            parent->token_epoch;
                        parent->canonical_stop_boundary_lifecycle_state_epoch =
                            parent->anchor_state_epoch;
                        parent->canonical_stop_boundary_command =
                            parent->anchored_custom_command;
                        parent->canonical_stop_boundary_destination = destination;
                        parent->canonical_stop_boundary_sound =
                            reinterpret_cast<void*>(source_before.sound);
                        parent->canonical_stop_boundary_sound_identity =
                            canonical_identity.live;
                        ++parent->version;
                        ++g_bgm_playback_collection_version;
                        admitted = true;
                        reason = "ready";
                    } else {
                        reason = "nonce";
                    }
                }
            } else if (!identity_exact) {
                reason = "canonical_identity";
            } else if (!context.exact) {
                reason = context.blocked_reason;
            }
            bool invalidated_boundary = false;
            if (!admitted && context.parent_ordinal != 0) {
                std::lock_guard<std::mutex> lock(
                    g_bgm_playback_borrower_mutex);
                for (auto& record : g_bgm_playback_borrowers) {
                    if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                            record.failed, record.terminal_callback_retired)
                        || record.ordinal != context.parent_ordinal
                        || !bgm_playback_blocked_canonical_boundary_invalidates(
                            context.canonical_stop_boundary, true,
                            record.canonical_stop_boundary_active,
                            record.canonical_intermediate_handle
                                == source_before.moved_handle)
                        || record.canonical_intermediate_handle
                            != source_before.moved_handle) continue;
                    record.canonical_stop_boundary_active = false;
                    record.canonical_stop_boundary_consumed = true;
                    ++record.version;
                    ++g_bgm_playback_collection_version;
                    invalidated_boundary = true;
                    break;
                }
            }
            if (g_bgm_playback_boundary_logs.fetch_add(
                    1, std::memory_order_relaxed) < 32) {
                std::ostringstream out;
                out << "[audio_sead] playback_aggregate status="
                    << (admitted ? "canonical_stop_boundary_admitted"
                                 : "canonical_stop_boundary_blocked")
                    << " reason=" << reason
                    << " first_failed_predicate="
                    << context.first_failed_predicate
                    << " parent_ordinal=" << context.parent_ordinal
                    << " parent_version=" << context.parent_version
                    << " boundary_nonce="
                    << (admitted ? g_bgm_playback_operation_nonce : 0)
                    << " boundary_invalidated="
                    << (invalidated_boundary ? 1 : 0)
                    << " canonical=0x" << std::hex
                    << source_before.moved_handle
                    << " canonical_command=0x"
                    << context.parent_canonical_command
                    << " anchored_custom_command=0x"
                    << context.anchored_custom_command
                    << " live_next_command=0x"
                    << context.live_next_command
                    << " destination=0x"
                    << reinterpret_cast<uintptr_t>(destination)
                    << std::dec << " mutation_authorized=0";
                core::log(core::LogLevel::Info, out.str());
            }
            return;
        }
        const bool transfer_exact = context.exact && transfer_postconditions
            && reinterpret_cast<void*>(source_before.sound)
                == context.record.sound;
        if (context.exact
            && context.record.provenance
                == BgmPlaybackLineageProvenance::PreSession
            && !transfer_exact
            && g_bgm_playback_observation_logs.fetch_add(
                1, std::memory_order_relaxed) < 96) {
            core::log(core::LogLevel::Info,
                "[audio_sead] playback_aggregate status=presession_capture_blocked reason=transfer_postcondition mutation_authorized=0");
        }
        if (!transfer_exact) {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            bool changed = false;
            for (auto& record : g_bgm_playback_borrowers) {
                if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                        record.failed, record.terminal_callback_retired)
                    || !record.canonical_stop_boundary_active)
                    continue;
                record.canonical_stop_boundary_active = false;
                record.canonical_stop_boundary_consumed = true;
                ++record.version;
                changed = true;
            }
            if (changed) ++g_bgm_playback_collection_version;
            return;
        }
        if (g_bgm_aggregate_exit_requested.load(std::memory_order_acquire)
            || g_bgm_aggregate_exit_pending.load(
                std::memory_order_acquire)) {
            log_bgm_aggregate_mutation(
                "exit_admission_blocked", "transfer", 0, 0, 0);
            return;
        }
        auto record = context.record;
        record.active = true;
        record.source = source;
        record.destination = destination;
        record.old_handle = source_before.moved_handle;
        bool admitted = false;
        bool overflow = false;
        uint64_t overflow_epoch = 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            for (auto& existing : g_bgm_playback_borrowers) {
                if (!existing.active
                    || !existing.canonical_stop_boundary_active) continue;
                existing.canonical_stop_boundary_active = false;
                existing.canonical_stop_boundary_consumed = true;
                ++existing.version;
                ++g_bgm_playback_collection_version;
            }
            record.ordinal = ++g_bgm_playback_borrower_ordinal;
            if (record.ordinal == 0) record.ordinal = ++g_bgm_playback_borrower_ordinal;
            record.observation_epoch = ++g_bgm_playback_observation_epoch;
            record.version = 1;
            size_t target = g_bgm_playback_borrowers.size();
            uint64_t oldest_resolved = UINT64_MAX;
            for (size_t i = 0; i < g_bgm_playback_borrowers.size(); ++i) {
                const auto& existing = g_bgm_playback_borrowers[i];
                if (!existing.active) {
                    target = i;
                    break;
                }
                if (bgm_playback_record_resolved(existing.active,
                        existing.failed, existing.old_absent,
                        existing.new_handle, existing.new_absent,
                        existing.latest_custom_transition_handle == 0
                            || existing.latest_custom_transition_consumed)
                    && existing.ordinal < oldest_resolved) {
                    target = i;
                    oldest_resolved = existing.ordinal;
                }
            }
            if (target < g_bgm_playback_borrowers.size()) {
                g_bgm_playback_borrowers[target] = record;
                ++g_bgm_playback_collection_version;
                admitted = true;
            } else {
                overflow = true;
                g_bgm_playback_capacity_overflow = true;
                g_bgm_playback_capacity_failure_epoch =
                    ++g_bgm_playback_observation_epoch;
                overflow_epoch = g_bgm_playback_capacity_failure_epoch;
                for (auto& existing : g_bgm_playback_borrowers) {
                    if (!existing.active) continue;
                    existing.failed = true;
                    if (existing.failure_epoch == 0) {
                        existing.failure_epoch =
                            g_bgm_playback_capacity_failure_epoch;
                    }
                    ++existing.version;
                }
                ++g_bgm_playback_collection_version;
            }
        }
        if (overflow) {
            if (g_bgm_playback_observation_logs.fetch_add(
                    1, std::memory_order_relaxed) < 96) {
                std::ostringstream out;
                out << "[audio_sead] playback_aggregate status=capacity_overflow"
                    << " failure_epoch=" << overflow_epoch
                    << " cleanup=unchanged mutation_authorized=0";
                core::log(core::LogLevel::Error, out.str());
            }
            return;
        }
        if (!admitted) return;
        if (g_bgm_playback_observation_logs.fetch_add(
                1, std::memory_order_relaxed) < 96) {
            std::ostringstream out;
            out << "[audio_sead] playback_aggregate status=transfer_adopted"
                << " ordinal=" << record.ordinal
                << " observation_epoch=" << record.observation_epoch
                << " old=0x" << std::hex << record.old_handle << std::dec
                << " session=" << record.session_epoch
                << " cycle=" << record.cycle_epoch
                << " provenance="
                << (record.provenance == BgmPlaybackLineageProvenance::PreSession
                    ? "pre_session" : "session_scoped")
                << " mutation_authorized=0";
            core::log(core::LogLevel::Info, out.str());
        }
        if (record.provenance == BgmPlaybackLineageProvenance::PreSession
            && g_bgm_playback_observation_logs.fetch_add(
                1, std::memory_order_relaxed) < 96) {
            core::log(core::LogLevel::Info,
                "[audio_sead] playback_aggregate status=presession_capture_admitted session=0 cycle=0 mutation_authorized=0");
        }
    } catch (...) {
    }
}
