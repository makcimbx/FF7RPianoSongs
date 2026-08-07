struct StopRetirementRead {
    AudioStopRetirementObservation observation{};
    void* bgm = nullptr;
    uint64_t current_request_handle = 0;
    void* current_backing = nullptr;
    bool current_backing_observed = false;
    void* retired_bgm = nullptr;
    void* retired_sound = nullptr;
    void* retired_backing = nullptr;
    bool retired_backing_observed = false;
    uint8_t slot_state = 0xff;
    uint8_t transition_mode = 0xff;
    uint8_t flag5c = 0xff;
    uint8_t flag5d = 0xff;
    float stop_parameter = 0.0f;
    int32_t retired_capacity = 0;
};

const char* audio_stop_retirement_phase_name(AudioStopRetirementPhase phase)
{
    switch (phase) {
    case AudioStopRetirementPhase::None: return "none";
    case AudioStopRetirementPhase::Waiting: return "waiting";
    case AudioStopRetirementPhase::Quiescent: return "quiescent";
    case AudioStopRetirementPhase::TimedOut: return "timed_out";
    case AudioStopRetirementPhase::Invalid: return "invalid";
    }
    return "unknown";
}

const char* audio_retirement_cleanup_failure_name(
    AudioRetirementCleanupFailure failure) noexcept
{
    switch (failure) {
    case AudioRetirementCleanupFailure::None: return "none";
    case AudioRetirementCleanupFailure::InvalidOrigin: return "invalid_origin";
    case AudioRetirementCleanupFailure::RetirementNotQuiescent:
        return "retirement_not_quiescent";
    case AudioRetirementCleanupFailure::ImmutableIdentityMismatch:
        return "immutable_identity_mismatch";
    case AudioRetirementCleanupFailure::BackingNotProven: return "backing_not_proven";
    case AudioRetirementCleanupFailure::LiveRouteNotSuccessor:
        return "live_route_not_successor";
    case AudioRetirementCleanupFailure::JournalsNotRestored:
        return "journals_not_restored";
    case AudioRetirementCleanupFailure::FrozenFieldsNotRestored:
        return "frozen_fields_not_restored";
    case AudioRetirementCleanupFailure::NativeRouteNotProven:
        return "native_route_not_proven";
    case AudioRetirementCleanupFailure::CleanupPolicyRejected:
        return "cleanup_policy_rejected";
    }
    return "unknown";
}

const char* onmemory_bank_retired_backing_provenance_name(
    OnMemoryBankRetiredBackingProvenance provenance) noexcept
{
    switch (provenance) {
    case OnMemoryBankRetiredBackingProvenance::None: return "none";
    case OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence:
        return "retired_vector_presence";
    case OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached:
        return "exact_natural_completion_detached";
    case OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector:
        return "detached_and_retired_vector";
    }
    return "unknown";
}

void log_audio_retirement_cleanup_failure(
    const AudioRetirementCleanupFailure failure,
    const AudioStopRetirementState& retirement,
    const uint64_t live_route_generation,
    const uint64_t frozen_route_generation,
    const OnMemoryBankRetiredBackingEvidence& backing) noexcept
{
    if (failure == AudioRetirementCleanupFailure::None) return;
    static std::atomic_uint32_t s_failure_logs{0};
    if (s_failure_logs.fetch_add(1, std::memory_order_relaxed) >= 64) return;
    try {
        std::ostringstream out;
        out << "[audio_sead] retirement_cleanup_failure first="
            << audio_retirement_cleanup_failure_name(failure)
            << " origin=" << static_cast<unsigned>(retirement.origin())
            << " frozen_generation=" << frozen_route_generation
            << " monitor_generation=" << retirement.route_generation
            << " live_generation=" << live_route_generation
            << " backing_generation=" << backing.route_generation
            << " backing_provenance="
            << onmemory_bank_retired_backing_provenance_name(backing.provenance)
            << " lifecycle_epoch=" << backing.lifecycle_epoch
            << " detached_ordinal=" << backing.detached_ordinal;
        core::log(core::LogLevel::Error, out.str());
    } catch (...) {
        // Diagnostic projection is best effort and cannot affect cleanup.
    }
}

void log_audio_retirement_request_identity(
    const bool terminal,
    const bool root_valid,
    const bool owned_root_exact,
    const bool leaf_valid,
    const bool root_leaf_equal,
    const bool owned_leaf_equal) noexcept
{
    if (!terminal && owned_root_exact) return;
    static std::atomic_uint32_t s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 32) return;
    try {
        std::ostringstream out;
        out << "[audio_sead] retirement_request_identity"
            << " terminal=" << terminal
            << " root_valid=" << root_valid
            << " owned_root_exact=" << owned_root_exact
            << " leaf_valid=" << leaf_valid
            << " root_leaf_equal=" << root_leaf_equal
            << " owned_leaf_equal=" << owned_leaf_equal;
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
        // Diagnostic projection is best effort and cannot affect cleanup.
    }
}

const char* bgm_aggregate_retirement_successor_name(
    const BgmPlaybackAggregateRetirementSuccessorClassification classification) noexcept
{
    switch (classification) {
    case BgmPlaybackAggregateRetirementSuccessorClassification::NotDirectSuccessorRefusal:
        return "not_direct_successor_refusal";
    case BgmPlaybackAggregateRetirementSuccessorClassification::ActiveExactSuccessor:
        return "active_exact_successor";
    case BgmPlaybackAggregateRetirementSuccessorClassification::TerminalExactSuccessor:
        return "terminal_exact_successor";
    case BgmPlaybackAggregateRetirementSuccessorClassification::AggregateFailure:
        return "aggregate_failure";
    case BgmPlaybackAggregateRetirementSuccessorClassification::IdentityMismatch:
        return "identity_mismatch";
    case BgmPlaybackAggregateRetirementSuccessorClassification::VersionDrift:
        return "version_drift";
    case BgmPlaybackAggregateRetirementSuccessorClassification::IncompleteSuccessor:
        return "incomplete_successor";
    }
    return "unknown";
}

void log_bgm_aggregate_retirement_successor(
    const BgmPlaybackAggregateRetirementSuccessorFacts& facts) noexcept
{
    const auto classification =
        classify_bgm_playback_aggregate_retirement_successor(facts);
    if (classification
        == BgmPlaybackAggregateRetirementSuccessorClassification::NotDirectSuccessorRefusal) {
        return;
    }
    static std::atomic_uint32_t s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 64) return;
    try {
        std::ostringstream stream;
        stream << "[audio_retirement_aggregate_successor] classification="
            << bgm_aggregate_retirement_successor_name(classification)
            << " monitor_origin=" << static_cast<unsigned>(facts.monitor_origin)
            << " monitor_epoch=" << facts.monitor_epoch
            << " monitor_route_generation=" << facts.monitor_route_generation
            << " monitor_request_generation=" << facts.monitor_request_generation
            << " live_route_generation=" << facts.live_route_generation
            << " aggregate_found=" << facts.aggregate_snapshot_found
            << " aggregate_ordinal=" << facts.aggregate_ordinal
            << " aggregate_version=" << facts.aggregate_version
            << " collection_version=" << facts.collection_version
            << " active=" << facts.aggregate_active
            << " failed=" << facts.aggregate_failed
            << " terminal=" << static_cast<unsigned>(facts.terminal_state)
            << " aggregate_closed=" << facts.aggregate_marker_closed
            << " anchor_exact=" << facts.anchor_exact
            << " controller_exact=" << facts.controller_exact
            << " lease_exact=" << facts.lease_exact
            << " lifecycle_exact=" << facts.lifecycle_exact
            << " tokens_exact=" << facts.tokens_exact
            << " sound_exact=" << facts.sound_exact
            << " old_request_generation=" << facts.old_request_generation
            << " canonical_request_generation=" << facts.canonical_request_generation
            << " latest_custom_request_generation="
            << facts.latest_custom_request_generation
            << " old_absent=" << facts.old_absent
            << " canonical_absent=" << facts.canonical_absent
            << " latest_custom_absent=" << facts.latest_custom_absent
            << " latest_custom_consumed=" << facts.latest_custom_consumed
            << " anchor_route_generation=" << facts.anchor_route_generation
            << " canonical_input_route_generation="
            << facts.canonical_input_route_generation
            << " canonical_route_generation=" << facts.canonical_route_generation
            << " latest_custom_route_generation="
            << facts.latest_custom_route_generation
            << " predecessor_nonce=" << facts.operation_predecessor_nonce
            << " set_nonce=" << facts.transition_set_nonce
            << " play_nonce=" << facts.transition_play_nonce
            << " nonce_exact=" << facts.operation_nonce_exact
            << " route_chain_exact=" << facts.route_chain_exact
            << " journals_restored=" << facts.journals_restored
            << " frozen_restored=" << facts.frozen_fields_restored
            << " owner_observed=" << facts.owner_field_observed
            << " owner_empty=" << facts.owner_field_empty
            << " native_empty=" << facts.native_route_empty
            << " native_identity_exact=" << facts.native_route_identity_exact
            << " version_recheck_exact=" << facts.version_recheck_exact;
        core::log(core::LogLevel::Info, stream.str());
    } catch (...) {
    }
}

StopRetirementRead read_stop_retirement(
    void* controller,
    const UObjectIdentity& controller_identity,
    uint64_t route_generation,
    const AudioRouteLeaseIdentity& lease_identity,
    void* expected_slot,
    void* expected_bgm,
    void* expected_custom_sound,
    uint64_t expected_request_handle)
{
    StopRetirementRead result;
    result.observation.route_generation = route_generation;
    result.observation.lease_identity = lease_identity;
    result.observation.controller = controller;
    result.observation.slot = expected_slot;
    result.observation.bgm = expected_bgm;
    result.observation.custom_sound = expected_custom_sound;
    result.observation.request_handle = expected_request_handle;

    void* slot = nullptr;
    void* retired_data = nullptr;
    int32_t retired_count = 0;
    const bool header_valid = controller
        && uobject_identity_matches(controller, controller_identity)
        && read_controller_bgm_chain(controller, slot, result.bgm)
        && slot == expected_slot
        && result.bgm == expected_bgm
        && core::safe_read_field(result.bgm, runtime_layouts::SqexSeadBgm::sound, result.observation.current_sound)
        && core::safe_read_field(result.bgm, runtime_layouts::SqexSeadBgm::request_handle, result.current_request_handle)
        && (result.current_backing_observed = core::safe_read_field(
            result.bgm,
            runtime_layouts::SqexSeadBgm::backing_resource,
            result.current_backing))
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, result.slot_state)
        && core::safe_read_field(slot, 0x31, result.transition_mode)
        && core::safe_read_field(slot, 0x40, result.stop_parameter)
        && core::safe_read_field(slot, 0x5c, result.flag5c)
        && core::safe_read_field(slot, 0x5d, result.flag5d)
        && core::safe_read_field(slot, 0xc8, retired_data)
        && core::safe_read_field(slot, 0xd0, retired_count)
        && core::safe_read_field(slot, 0xd4, result.retired_capacity)
        && retired_count >= 0
        && retired_count <= result.retired_capacity
        && result.retired_capacity >= 0
        && result.retired_capacity <= 256
        && (retired_count == 0 || retired_data != nullptr);
    if (!header_valid) {
        return result;
    }
    result.observation.current_request_handle = result.current_request_handle;
    result.observation.current_state = result.slot_state;

    bool entries_valid = true;
    OnMemoryBankRetiredEntryEvidence exact_entry;
    for (int32_t index = 0; index < retired_count; ++index) {
        void* retired_bgm = nullptr;
        uint64_t retired_handle = 0;
        if (!core::safe_read_field(
                retired_data, static_cast<size_t>(index) * sizeof(void*), retired_bgm)
            || !retired_bgm
            || !core::safe_read_field(
                retired_bgm, runtime_layouts::SqexSeadBgm::request_handle, retired_handle)) {
            entries_valid = false;
            break;
        }
        if (retired_handle == expected_request_handle) {
            void* retired_sound = nullptr;
            void* retired_backing = nullptr;
            const bool sound_observed = core::safe_read_field(
                retired_bgm, runtime_layouts::SqexSeadBgm::sound, retired_sound);
            const bool backing_observed = core::safe_read_field(
                retired_bgm,
                runtime_layouts::SqexSeadBgm::backing_resource,
                retired_backing);
            if (!record_onmemory_bank_retired_entry(exact_entry,
                    retired_handle, expected_request_handle,
                    sound_observed, retired_sound, expected_custom_sound,
                    backing_observed, retired_backing)) {
                entries_valid = false;
                break;
            }
            result.retired_bgm = retired_bgm;
        }
    }
    if (entries_valid && exact_entry.observed) {
        result.observation.request_handle_retired = true;
        result.retired_sound = exact_entry.sound;
        result.retired_backing_observed = true;
        result.retired_backing = exact_entry.backing;
    }
    result.observation.retired_count = static_cast<uint32_t>(retired_count);
    result.observation.valid = entries_valid;
    return result;
}

void log_stop_retirement(
    const char* stage,
    const StopRetirementRead& current,
    AudioStopRetirementPhase phase,
    uint32_t poll_count)
{
    std::ostringstream out;
    out << "[audio_sead] stop_retirement stage=" << stage
        << " phase=" << audio_stop_retirement_phase_name(phase)
        << " valid=" << (current.observation.valid ? 1 : 0)
        << " expected_handle_retired="
        << (current.observation.request_handle_retired ? 1 : 0)
        << " retired_count=" << current.observation.retired_count
        << " retired_capacity=" << current.retired_capacity
        << " poll_count=" << poll_count
        << " slot_state=" << static_cast<unsigned>(current.slot_state)
        << " transition_mode=" << static_cast<unsigned>(current.transition_mode)
        << " flag5c=" << static_cast<unsigned>(current.flag5c)
        << " flag5d=" << static_cast<unsigned>(current.flag5d)
        << " stop_parameter=" << current.stop_parameter
        << " route_generation=" << current.observation.route_generation
        << " lease_generation=" << current.observation.lease_identity.generation
        << " controller=0x" << std::hex
        << reinterpret_cast<uintptr_t>(current.observation.controller)
        << " slot=0x" << reinterpret_cast<uintptr_t>(current.observation.slot)
        << " bgm=0x" << reinterpret_cast<uintptr_t>(current.bgm)
        << " expected_request_handle=0x" << current.observation.request_handle
        << " current_request_handle=0x" << current.current_request_handle
        << " current_backing=0x" << reinterpret_cast<uintptr_t>(current.current_backing)
        << " retired_bgm=0x" << reinterpret_cast<uintptr_t>(current.retired_bgm)
        << " retired_sound=0x" << reinterpret_cast<uintptr_t>(current.retired_sound)
        << " retired_backing=0x" << reinterpret_cast<uintptr_t>(current.retired_backing)
        << std::dec;
    core::log(current.observation.valid ? core::LogLevel::Info : core::LogLevel::Error, out.str());
}

bool frozen_sound_fields_restored_for_retirement(
    const FrozenSoundPatchSnapshot& snapshot,
    const AudioStopRetirementState& retirement,
    const uint64_t live_route_generation,
    bool* owner_field_observed = nullptr,
    bool* owner_field_empty = nullptr,
    const AudioRetirementRouteAuthorityFacts* route_authority = nullptr)
{
    if (owner_field_observed) *owner_field_observed = false;
    if (owner_field_empty) *owner_field_empty = false;
    const bool generation_proven = route_authority
        ? audio_retirement_route_authority_proven(*route_authority)
        : audio_retirement_frozen_generation_proven(
            snapshot.route_generation, retirement, live_route_generation);
    if (!snapshot.valid() || !generation_proven
        || !(snapshot.lease_identity == retirement.lease_identity)
        || snapshot.sound != retirement.custom_sound
        || !uobject_identity_matches(snapshot.sound, snapshot.sound_identity)) {
        return false;
    }
    for (const AudioFieldPatch& patch : snapshot.patches) {
        if (patch.object != snapshot.sound) {
            return false;
        }
        uint64_t current = 0;
        const bool read_ok = patch.size == sizeof(uint32_t)
            ? read_field_u32_as_u64(patch.object, patch.offset, current)
            : read_field_u64(patch.object, patch.offset, current);
        if (!read_ok) {
            return false;
        }
        const AudioPatchRestoreDecision decision = evaluate_audio_patch_restore(
            current, patch.original, patch.replacement, true);
        if (patch.label && std::strcmp(patch.label, "sound+0x548") == 0) {
            if (owner_field_observed) *owner_field_observed = true;
            if (owner_field_empty) *owner_field_empty = current == 0;
        }
        if (decision == AudioPatchRestoreDecision::RestoreOriginal
            || decision == AudioPatchRestoreDecision::Conflict) {
            return false;
        }
    }
    return true;
}

const char* bgm_aggregate_terminal_handoff_name(
    BgmPlaybackAggregateTerminalHandoffClassification value) noexcept
{
    using Value = BgmPlaybackAggregateTerminalHandoffClassification;
    switch (value) {
    case Value::NotCandidate: return "not_candidate";
    case Value::NoQuiescentMonitor: return "no_quiescent_monitor";
    case Value::AggregateFailure: return "aggregate_failure";
    case Value::VersionDrift: return "version_drift";
    case Value::RootNotFound: return "root_not_found";
    case Value::LineageBranch: return "lineage_branch";
    case Value::LineageCycle: return "lineage_cycle";
    case Value::LineageMismatch: return "lineage_mismatch";
    case Value::BackingMismatch: return "backing_mismatch";
    case Value::LifecycleMismatch: return "lifecycle_mismatch";
    case Value::OwnerMismatch: return "owner_mismatch";
    case Value::FrozenOrJournalMismatch: return "frozen_or_journal_mismatch";
    case Value::PauseConflict: return "pause_conflict";
    case Value::RegistryMismatch: return "registry_mismatch";
    case Value::NativeRouteNotEmpty: return "native_route_not_empty";
    case Value::ExactAtAggregateClosed: return "exact_at_aggregate_closed";
    case Value::ExactAtNaturalStop: return "exact_at_natural_stop";
    }
    return "unknown";
}

const char* bgm_aggregate_terminal_semantic_delta_name(
    BgmPlaybackAggregateTerminalSemanticDeltaClassification value) noexcept
{
    using Value = BgmPlaybackAggregateTerminalSemanticDeltaClassification;
    switch (value) {
    case Value::NotCompared: return "not_compared";
    case Value::NoDelta: return "no_delta";
    case Value::VersionOnly: return "version_only";
    case Value::MarkerOnly: return "marker_only";
    case Value::ExactBoundaryInvalidation: return "exact_boundary_invalidation";
    case Value::NodeSetDrift: return "node_set_drift";
    case Value::ActiveMutation: return "active_mutation";
    case Value::FailureMutation: return "failure_mutation";
    case Value::TerminalMutation: return "terminal_mutation";
    case Value::RequestMutation: return "request_mutation";
    case Value::AbsenceMutation: return "absence_mutation";
    case Value::PendingSetMutation: return "pending_set_mutation";
    case Value::BoundaryMutation: return "boundary_mutation";
    case Value::ChildMutation: return "child_mutation";
    case Value::NonceMutation: return "nonce_mutation";
    case Value::RouteMutation: return "route_mutation";
    case Value::MultipleSemanticMutations: return "multiple_semantic_mutations";
    }
    return "unknown";
}

void log_bgm_aggregate_terminal_handoff(
    const char* prefix,
    const BgmPlaybackAggregateTerminalHandoffFacts& facts,
    const BgmPlaybackAggregateTerminalHandoffClassification pre_stop =
        BgmPlaybackAggregateTerminalHandoffClassification::NotCandidate) noexcept
{
    static std::atomic_uint32_t s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 64) return;
    try {
        const auto classification =
            classify_bgm_playback_aggregate_terminal_handoff(facts);
        const auto lineage = analyze_bgm_playback_aggregate_terminal_lineage(
            facts.records.data(), facts.record_count, facts.monitor_request);
        std::ostringstream out;
        out << prefix
            << " classification=" << bgm_aggregate_terminal_handoff_name(classification)
            << " records=" << static_cast<unsigned>(facts.record_count)
            << " path=" << static_cast<unsigned>(lineage.path_count)
            << " root=" << static_cast<unsigned>(lineage.root_index)
            << " leaf=" << static_cast<unsigned>(lineage.leaf_index)
            << " monitor_origin=" << static_cast<unsigned>(facts.monitor_origin)
            << " monitor_epoch=" << facts.monitor_epoch
            << " monitor_route=" << facts.monitor_route_generation
            << " live_route=" << facts.live_route_generation
            << " collection=" << facts.collection_version
            << " version_exact=" << facts.version_recheck_exact
            << " marker_only_successor=" << facts.marker_only_successor
            << " backing=" << facts.backing_exact
            << " backing_provenance=" << static_cast<unsigned>(facts.backing_provenance)
            << " lifecycle=" << facts.lifecycle_exact
            << " lifecycle_present=" << facts.lifecycle_present
            << " lifecycle_restore_applied=" << facts.lifecycle_restore_applied
            << " lifecycle_epoch=" << facts.lifecycle_state_epoch_exact
            << " lifecycle_ordinal=" << facts.lifecycle_ordinal_exact
            << " lifecycle_route=" << facts.lifecycle_route_exact
            << " lifecycle_cleanup=" << facts.lifecycle_cleanup_exact
            << " lifecycle_request=" << facts.lifecycle_request_exact
            << " lifecycle_sound=" << facts.lifecycle_sound_exact
            << " lifecycle_canonical=" << facts.lifecycle_canonical_exact
            << " lifecycle_custom=" << facts.lifecycle_custom_exact
            << " lifecycle_owner_restored=" << facts.lifecycle_owner_restored
            << " lifecycle_release_attempted=" << facts.lifecycle_release_attempted
            << " lifecycle_release_absent="
            << facts.lifecycle_release_in_flight_absent
            << " owner_category=" << static_cast<unsigned>(facts.owner_category)
            << " owner=" << facts.owner_exact
            << " frozen=" << facts.frozen_exact
            << " frozen_generation=" << facts.frozen_generation_exact
            << " frozen_lease=" << facts.frozen_lease_exact
            << " frozen_sound=" << facts.frozen_sound_exact
            << " frozen_non_owner=" << facts.frozen_non_owner_fields_restored
            << " frozen_owner_observed=" << facts.frozen_owner_observed
            << " journals=" << facts.journals_restored
            << " pause_phase=" << static_cast<unsigned>(facts.pause_phase)
            << " pause_session=" << facts.pause_session_epoch
            << " pause_cycle=" << facts.pause_cycle_epoch
            << " pause_source=" << facts.pause_source_available
            << " pause_request=" << facts.pause_request_exact
            << " pause_sound=" << facts.pause_sound_exact
            << " pause_lease=" << facts.pause_lease_exact
            << " pause_exit=" << facts.pause_exit_requested
            << " pause_release=" << facts.pause_release_pending
            << " pause_retirement=" << facts.pause_retirement_active
            << " pause_same_lineage=" << facts.pause_same_lineage
            << " pause_retirement_candidate="
            << facts.pause_retirement_candidate
            << " pause_active=" << facts.pause_active
            << " pause_conflict=" << facts.pause_conflict
            << " playback_absent=" << facts.registry_playback_absent
            << " playback_present=" << facts.registry_playback_present
            << " playback_selection=" << facts.registry_playback_selection_exact
            << " playback_controller=" << facts.registry_playback_controller_exact
            << " playback_slot=" << facts.registry_playback_slot_exact
            << " playback_bgm=" << facts.registry_playback_bgm_exact
            << " playback_sound=" << facts.registry_playback_sound_exact
            << " playback_request=" << facts.registry_playback_request_exact
            << " playback_route=" << facts.registry_playback_route_exact
            << " playback_lease=" << facts.registry_playback_lease_exact
            << " playback_exact=" << facts.registry_playback_active_exact
            << " cleanup_present=" << facts.registry_cleanup_present
            << " cleanup_selection=" << facts.registry_cleanup_selection_exact
            << " cleanup_valid=" << facts.registry_cleanup_valid
            << " cleanup_controller=" << facts.registry_cleanup_controller_exact
            << " cleanup_slot=" << facts.registry_cleanup_slot_exact
            << " cleanup_bgm=" << facts.registry_cleanup_bgm_exact
            << " cleanup_sound=" << facts.registry_cleanup_sound_exact
            << " cleanup_request=" << facts.registry_cleanup_request_exact
            << " cleanup_route=" << facts.registry_cleanup_route_exact
            << " cleanup_lease=" << facts.registry_cleanup_lease_exact
            << " registry=" << facts.registry_exact
            << " native_empty=" << facts.native_route_empty
            << " route_unchanged=" << facts.route_unchanged
            << " null_chain=" << facts.null_chain_exact
            << " pre_classification="
            << bgm_aggregate_terminal_handoff_name(pre_stop)
            << " post_stop_recheck=" << facts.post_stop_recheck
            << " post_diagnostic_count="
            << static_cast<unsigned>(facts.post_diagnostic_count)
            << " post_observation_ran=" << facts.post_observation_ran
            << " post_observation_succeeded=" << facts.post_observation_succeeded
            << " native_stop_called_once=" << facts.native_stop_called_once
            << " post_null_chain=" << facts.post_null_chain_exact
            << " closed_to_pre_delta="
            << bgm_aggregate_terminal_semantic_delta_name(
                facts.aggregate_closed_to_pre_stop.classification)
            << " closed_to_pre_mask="
            << facts.aggregate_closed_to_pre_stop.semantic_mask
            << " stop_delta="
            << bgm_aggregate_terminal_semantic_delta_name(
                facts.pre_to_post_stop.classification)
            << " stop_delta_mask=" << facts.pre_to_post_stop.semantic_mask
            << " stop_changed_nodes="
            << static_cast<unsigned>(facts.pre_to_post_stop.changed_node_count)
            << " stop_version_nodes="
            << static_cast<unsigned>(
                facts.pre_to_post_stop.version_changed_node_count)
            << " stop_marker_nodes="
            << static_cast<unsigned>(
                facts.pre_to_post_stop.marker_changed_node_count)
            << " stop_boundary_nodes="
            << static_cast<unsigned>(
                facts.pre_to_post_stop.boundary_invalidated_node_count)
            << " stop_collection_changed="
            << facts.pre_to_post_stop.collection_changed
            << " post_live_route=" << facts.post_live_route_generation
            << " post_monitor=" << facts.post_monitor_exact
            << " post_backing=" << facts.post_backing_exact
            << " post_owner_category="
            << static_cast<unsigned>(facts.post_owner_category)
            << " post_owner=" << facts.post_owner_exact
            << " post_frozen=" << facts.post_frozen_exact
            << " post_lifecycle=" << facts.post_lifecycle_exact
            << " post_pause=" << facts.post_pause_exact
            << " post_registry=" << facts.post_registry_exact
            << " post_setup_absent=" << facts.post_setup_absent
            << " post_handoff_absent=" << facts.post_handoff_absent
            << " post_release_absent=" << facts.post_release_absent
            << " mutation_authorized=" << facts.cleanup_committed;
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

bool same_bgm_terminal_domain(
    const BgmPlaybackBorrowerRecord& left,
    const BgmPlaybackBorrowerRecord& right) noexcept
{
    return left.retirement_anchored && right.retirement_anchored
        && left.anchor_state_epoch == right.anchor_state_epoch
        && left.anchor_route_generation == right.anchor_route_generation
        && left.anchor_cleanup_generation == right.anchor_cleanup_generation
        && left.anchor_request_handle == right.anchor_request_handle
        && left.controller == right.controller
        && controller_identity_proof_matches(
            left.controller_proof, right.controller_proof)
        && left.lease == right.lease && left.token_epoch == right.token_epoch
        && left.lifecycle_state_epoch == right.lifecycle_state_epoch
        && left.canonical == right.canonical && left.custom == right.custom
        && left.sound == right.sound
        && left.sound_identity.internal_index == right.sound_identity.internal_index
        && left.sound_identity.serial_number == right.sound_identity.serial_number;
}

BgmPlaybackAggregateTerminalLineageNode bgm_terminal_node(
    const BgmPlaybackBorrowerRecord& record, bool borrowers_zero) noexcept
{
    BgmPlaybackAggregateTerminalLineageNode node;
    node.present = true;
    DecodedOnMemoryBankToken canonical;
    DecodedOnMemoryBankToken custom;
    const bool canonical_decoded =
        decode_onmemory_bank_token(record.canonical, canonical);
    const bool custom_decoded = decode_onmemory_bank_token(record.custom, custom);
    node.domain_exact = record.retirement_anchored
        && record.anchor_state_epoch != 0
        && record.anchor_route_generation != 0
        && record.anchor_cleanup_generation != 0
        && AudioBgmRequestHandle{record.anchor_request_handle}.valid_bgm_request()
        && record.controller != nullptr
        && controller_identity_proof_valid(record.controller_proof)
        && record.lease.valid() && record.token_epoch != 0
        && record.lifecycle_state_epoch != 0
        && OnMemoryBankSoundIdentity{record.sound, record.sound_identity}
        && canonical_decoded && custom_decoded
        && canonical.type == 1 && custom.type == 1
        && canonical.encode() != custom.encode()
        && AudioBgmRequestHandle{record.old_handle}.valid_bgm_request()
        && AudioBgmRequestHandle{
            record.canonical_intermediate_handle}.valid_bgm_request()
        && AudioBgmRequestHandle{
            record.latest_custom_transition_handle}.valid_bgm_request();
    node.active = record.active;
    node.failed = record.failed;
    node.aggregate_closed =
        record.last_marker == BgmPlaybackAggregateMarker::AggregateClosed;
    node.borrowers_zero = borrowers_zero;
    node.terminal_state = record.terminal_state;
    node.ordinal = record.ordinal;
    node.version = record.version;
    node.old_request = record.old_handle;
    node.canonical_request = record.canonical_intermediate_handle;
    node.latest_custom_request = record.latest_custom_transition_handle;
    node.old_request_generation = AudioBgmRequestHandle{record.old_handle}.generation();
    node.canonical_request_generation = AudioBgmRequestHandle{
        record.canonical_intermediate_handle}.generation();
    node.latest_custom_request_generation = AudioBgmRequestHandle{
        record.latest_custom_transition_handle}.generation();
    node.old_absent = record.old_absent;
    node.canonical_absent = record.canonical_intermediate_absent;
    node.latest_custom_absent = record.latest_custom_transition_absent;
    node.latest_custom_consumed = record.latest_custom_transition_consumed;
    node.child_ordinal = record.latest_custom_transition_child_ordinal;
    node.anchor_route_generation = record.anchor_route_generation;
    node.lineage_route_predecessor_generation =
        record.lineage_route_predecessor_generation;
    node.canonical_set_input_route_generation =
        record.canonical_set_input_route_generation;
    node.canonical_route_generation =
        record.canonical_intermediate_route_generation;
    node.latest_custom_route_generation =
        record.latest_custom_transition_route_generation;
    node.operation_predecessor_nonce = record.operation_predecessor_nonce;
    node.canonical_set_nonce = record.canonical_stop_boundary_nonce > 1
        ? record.canonical_stop_boundary_nonce - 2 : 0;
    node.canonical_play_nonce = record.canonical_stop_boundary_nonce > 0
        ? record.canonical_stop_boundary_nonce - 1 : 0;
    node.stop_boundary_nonce = record.canonical_stop_boundary_nonce;
    node.custom_set_nonce = record.transition_set_nonce;
    node.custom_play_nonce = record.transition_play_nonce;
    node.stop_boundary_consumed = record.canonical_stop_boundary_consumed;
    return node;
}

BgmPlaybackAggregateTerminalSemanticNode bgm_terminal_semantic_node(
    const BgmPlaybackBorrowerRecord& record) noexcept
{
    BgmPlaybackAggregateTerminalSemanticNode node;
    node.present = record.ordinal != 0;
    node.ordinal = record.ordinal;
    node.version = record.version;
    node.active = record.active;
    node.failed = record.failed;
    node.terminal_state = record.terminal_state;
    node.marker = record.last_marker;
    node.old_request = record.old_handle;
    node.canonical_request = record.canonical_intermediate_handle;
    node.latest_custom_request = record.latest_custom_transition_handle;
    node.old_absent = record.old_absent;
    node.canonical_absent = record.canonical_intermediate_absent;
    node.latest_custom_absent = record.latest_custom_transition_absent;
    node.transition_set_pending = record.transition_set_pending;
    node.stop_boundary_active = record.canonical_stop_boundary_active;
    node.stop_boundary_consumed = record.canonical_stop_boundary_consumed;
    node.stop_boundary_nonce = record.canonical_stop_boundary_nonce;
    node.stop_boundary_route_generation =
        record.canonical_stop_boundary_route_generation;
    node.latest_custom_consumed = record.latest_custom_transition_consumed;
    node.child_ordinal = record.latest_custom_transition_child_ordinal;
    node.operation_predecessor_nonce = record.operation_predecessor_nonce;
    node.transition_set_nonce = record.transition_set_nonce;
    node.transition_play_nonce = record.transition_play_nonce;
    node.canonical_set_input_route_generation =
        record.canonical_set_input_route_generation;
    node.canonical_route_generation =
        record.canonical_intermediate_route_generation;
    node.latest_custom_route_generation =
        record.latest_custom_transition_route_generation;
    return node;
}

bool same_bgm_terminal_record_except_marker_version(
    BgmPlaybackBorrowerRecord left,
    BgmPlaybackBorrowerRecord right) noexcept
{
    left.version = right.version = 0;
    left.last_marker = right.last_marker = BgmPlaybackAggregateMarker::Invalid;
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

void populate_bgm_terminal_environment(
    BgmPlaybackAggregateTerminalHandoffFacts& facts,
    const std::array<BgmPlaybackBorrowerRecord, 32>& records,
    const bool observe_native = true) noexcept
{
    AudioRouteState route;
    OnMemoryBankDetachedRecord lifecycle;
    uint64_t lifecycle_epoch = 0;
    PauseResumeBankSession pause;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route = g_audio_route_state;
        lifecycle = g_onmemory_bank_lifecycle.active();
        lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
        pause = g_pause_resume_bank;
        facts.journals_restored = g_active_patch_journal.empty()
            && g_pending_play_setup_patch.patches.empty()
            && g_failed_patch_journal.empty();
        facts.setup_absent = g_pending_play_setup_patch.patches.empty()
            && !g_unpublished_audio_setup;
        facts.handoff_absent = !route.set_play_handoff_pending
            && !route.handoff_slot && !route.handoff_bgm && !route.handoff_sound
            && route.handoff_request_handle == 0
            && route.deferred_native_handoff.phase
                == AudioDeferredNativeHandoffPhase::None;
        facts.release_absent = !g_onmemory_bank_lifecycle.release_in_flight();
        facts.lifecycle_release_in_flight_absent =
            !g_onmemory_bank_lifecycle.release_in_flight();
    }
    facts.release_absent = facts.release_absent
        && !g_bgm_aggregate_exit_pending.load(std::memory_order_acquire);
    facts.live_route_generation = route.generation;
    const auto& monitor = route.stop_retirement;
    facts.quiescent_monitor = monitor.phase == AudioStopRetirementPhase::Quiescent;
    facts.monitor_origin = static_cast<uint8_t>(monitor.origin());
    facts.monitor_epoch = route.stop_retirement_epoch;
    facts.monitor_route_generation = monitor.route_generation;
    facts.monitor_request = monitor.request_handle;
    if (!facts.quiescent_monitor) return;

    const auto lineage = analyze_bgm_playback_aggregate_terminal_lineage(
        facts.records.data(), facts.record_count, facts.monitor_request);
    if (!lineage.root_found || lineage.root_index >= facts.record_count) return;
    const BgmPlaybackBorrowerRecord* root = nullptr;
    for (const auto& record : records) {
        if (record.ordinal == facts.records[lineage.root_index].ordinal) {
            root = &record;
            break;
        }
    }
    if (!root) return;
    const auto& backing = route.stop_retirement_backing;
    facts.backing_provenance = static_cast<uint8_t>(backing.provenance);
    const bool backing_common = backing && backing.route_generation
            == monitor.route_generation
        && backing.cleanup_generation == monitor.lease_identity.generation
        && backing.monitor_epoch == route.stop_retirement_epoch
        && backing.request_handle == monitor.request_handle
        && monitor.custom_sound == root->sound
        && backing.sound_identity.internal_index
            == root->sound_identity.internal_index
        && backing.sound_identity.serial_number
            == root->sound_identity.serial_number;
    const bool detached_current =
        onmemory_bank_exact_natural_detached_backing_current(
            backing, lifecycle_epoch, lifecycle);
    facts.backing_exact = bgm_playback_aggregate_terminal_backing_exact(
        monitor.origin(), backing.provenance, backing_common, detached_current);
    const OnMemoryBankSoundIdentity observed_sound{
        root->sound, root->sound_identity};
    facts.lifecycle_present = static_cast<bool>(lifecycle);
    facts.lifecycle_restore_applied = lifecycle.phase
        == OnMemoryBankLifecyclePhase::RestoreApplied;
    facts.lifecycle_state_epoch_exact = lifecycle_epoch == root->anchor_state_epoch;
    facts.lifecycle_ordinal_exact = lifecycle.ordinal == root->token_epoch;
    facts.lifecycle_route_exact = lifecycle.route_generation
        == root->anchor_route_generation;
    facts.lifecycle_cleanup_exact = lifecycle.cleanup_generation
        == root->anchor_cleanup_generation;
    facts.lifecycle_request_exact = lifecycle.request_handle
        == root->anchor_request_handle
        && monitor.request_handle == root->anchor_request_handle;
    facts.lifecycle_sound_exact = lifecycle.sound == observed_sound
        && monitor.custom_sound == root->sound;
    facts.lifecycle_canonical_exact = lifecycle.canonical.encode() == root->canonical;
    facts.lifecycle_custom_exact = lifecycle.custom.encode() == root->custom;
    facts.lifecycle_owner_restored = lifecycle.owner_restore_verified;
    facts.lifecycle_release_attempted = lifecycle.release_attempted;
    facts.lifecycle_exact = bgm_playback_aggregate_terminal_lifecycle_exact(
        facts.lifecycle_present,
        facts.lifecycle_restore_applied,
        facts.lifecycle_state_epoch_exact,
        facts.lifecycle_ordinal_exact,
        facts.lifecycle_route_exact,
        facts.lifecycle_cleanup_exact,
        facts.lifecycle_request_exact,
        facts.lifecycle_sound_exact,
        facts.lifecycle_canonical_exact,
        facts.lifecycle_custom_exact,
        facts.lifecycle_owner_restored,
        facts.lifecycle_release_attempted,
        facts.lifecycle_release_in_flight_absent,
        monitor.lease_identity == root->lease
            && monitor.controller == root->controller
            && monitor.slot == root->slot && monitor.bgm == root->bgm);

    uint64_t owner = 0;
    const bool owner_readable = observed_sound
        && core::safe_read_field(observed_sound.object,
            runtime_layouts::SqexSeadSound::observed_field548, owner);
    const auto owner_fact = classify_onmemory_bank_retirement_owner(
        lifecycle, observed_sound,
        facts.lifecycle_present && facts.lifecycle_sound_exact,
        owner_readable, owner);
    switch (owner_fact.category) {
    case OnMemoryBankRetirementOwnerCategory::Unreadable:
        facts.owner_category = BgmPlaybackAggregateTerminalOwnerCategory::Unreadable;
        break;
    case OnMemoryBankRetirementOwnerCategory::IdentityMismatch:
        facts.owner_category = BgmPlaybackAggregateTerminalOwnerCategory::Foreign;
        break;
    case OnMemoryBankRetirementOwnerCategory::Zero:
        facts.owner_category = BgmPlaybackAggregateTerminalOwnerCategory::Zero;
        break;
    case OnMemoryBankRetirementOwnerCategory::Canonical:
        facts.owner_category = BgmPlaybackAggregateTerminalOwnerCategory::Canonical;
        break;
    case OnMemoryBankRetirementOwnerCategory::DetachedCustom:
        facts.owner_category = BgmPlaybackAggregateTerminalOwnerCategory::DetachedCustom;
        break;
    case OnMemoryBankRetirementOwnerCategory::ForeignNonzero:
        facts.owner_category = BgmPlaybackAggregateTerminalOwnerCategory::Foreign;
        break;
    }
    facts.owner_exact = facts.owner_category
            == BgmPlaybackAggregateTerminalOwnerCategory::Zero
        || facts.owner_category
            == BgmPlaybackAggregateTerminalOwnerCategory::Canonical;

    facts.frozen_generation_exact = route.frozen_sound_patch.route_generation
        == root->lineage_route_predecessor_generation;
    facts.frozen_lease_exact = route.frozen_sound_patch.lease_identity == root->lease;
    facts.frozen_sound_exact = route.frozen_sound_patch.sound == root->sound
        && route.frozen_sound_patch.sound_identity.live.internal_index
            == root->sound_identity.internal_index
        && route.frozen_sound_patch.sound_identity.live.serial_number
            == root->sound_identity.serial_number;
    facts.frozen_non_owner_fields_restored = route.frozen_sound_patch.valid();
    size_t frozen_owner_count = 0;
    if (route.frozen_sound_patch.valid()) {
        for (const auto& patch : route.frozen_sound_patch.patches) {
            const bool owner_patch = patch.label
                && std::strcmp(patch.label, "sound+0x548") == 0;
            if (owner_patch) {
                ++frozen_owner_count;
                continue;
            }
            uint64_t current = 0;
            const bool read_ok = patch.size == sizeof(uint32_t)
                ? read_field_u32_as_u64(patch.object, patch.offset, current)
                : read_field_u64(patch.object, patch.offset, current);
            const auto decision = read_ok
                ? evaluate_audio_patch_restore(
                    current, patch.original, patch.replacement, true)
                : AudioPatchRestoreDecision::Conflict;
            if (decision == AudioPatchRestoreDecision::RestoreOriginal
                || decision == AudioPatchRestoreDecision::Conflict) {
                facts.frozen_non_owner_fields_restored = false;
                break;
            }
        }
    }
    facts.frozen_owner_observed = frozen_owner_count == 1 && owner_readable;
    facts.frozen_exact = route.frozen_sound_patch.valid()
        && facts.frozen_generation_exact && facts.frozen_lease_exact
        && facts.frozen_sound_exact && facts.frozen_non_owner_fields_restored
        && facts.frozen_owner_observed;
    facts.pause_phase = static_cast<uint8_t>(pause.phase);
    facts.pause_session_epoch = pause.session_epoch;
    facts.pause_cycle_epoch = pause.cycle_epoch;
    facts.pause_source_available = pause.selection.song != nullptr;
    facts.pause_retirement_candidate =
        pause.phase == PauseResumeBankPhase::RetirementCandidate;
    facts.pause_request_exact = facts.pause_retirement_candidate
        ? pause.detached.request_handle == root->anchor_request_handle
        : pause.request_handle == root->anchor_request_handle;
    facts.pause_sound_exact = pause.detached
        && pause.detached.sound == lifecycle.sound;
    facts.pause_lease_exact = facts.pause_retirement_candidate
        ? pause.detached.cleanup_generation == root->anchor_cleanup_generation
        : pause.lease_identity == root->lease;
    facts.pause_exit_requested = pause.exit_requested;
    facts.pause_release_pending = static_cast<bool>(pause.pending_release_probe)
        || static_cast<bool>(pause.pending_release_evidence);
    facts.pause_retirement_active = pause.retirement_monitor.active();
    facts.pause_active = pause.phase != PauseResumeBankPhase::Idle
        && pause.phase != PauseResumeBankPhase::Complete;
    const bool detached_same_lineage = pause.detached
        && pause.detached.ordinal == root->token_epoch
        && pause.detached.route_generation == root->anchor_route_generation
        && pause.detached.cleanup_generation == root->anchor_cleanup_generation
        && pause.detached.request_handle == root->anchor_request_handle
        && pause.detached.sound == lifecycle.sound;
    facts.pause_same_lineage = facts.pause_retirement_candidate
        ? detached_same_lineage
        : detached_same_lineage && pause.lease_identity == root->lease
            && pause.session_epoch == root->correlated_session_epoch
            && pause.cycle_epoch == root->correlated_cycle_epoch;
    facts.pause_conflict = bgm_playback_aggregate_terminal_pause_conflict(
        facts.pause_active, facts.pause_same_lineage,
        facts.pause_retirement_candidate);

    const PlaybackSnapshot playback = registry().playback_snapshot();
    const CleanupLease cleanup = registry().cleanup_lease();
    facts.registry_playback_present = playback.token.valid();
    facts.registry_playback_absent = !facts.registry_playback_present;
    if (playback.token.valid()) {
        facts.registry_playback_selection_exact = playback.song != nullptr
            && playback.profile != nullptr && playback.storage != nullptr;
        facts.registry_playback_controller_exact =
            playback.token.controller == monitor.controller;
        facts.registry_playback_slot_exact = playback.token.slot == monitor.slot;
        facts.registry_playback_bgm_exact = playback.token.bgm == monitor.bgm;
        facts.registry_playback_sound_exact =
            playback.token.sound == monitor.custom_sound;
        facts.registry_playback_request_exact = playback.token.request_handle
            == monitor.request_handle;
        facts.registry_playback_route_exact = playback.token.route_generation
            == monitor.route_generation;
        facts.registry_playback_lease_exact = playback.token.lease_generation
            == monitor.lease_identity.generation;
        facts.registry_playback_active_exact =
            facts.registry_playback_selection_exact
            && facts.registry_playback_controller_exact
            && facts.registry_playback_slot_exact
            && facts.registry_playback_bgm_exact
            && facts.registry_playback_sound_exact
            && facts.registry_playback_request_exact
            && facts.registry_playback_route_exact
            && facts.registry_playback_lease_exact;
    }
    facts.registry_cleanup_present = cleanup.token.valid();
    facts.registry_cleanup_valid = facts.registry_cleanup_present;
    facts.registry_cleanup_selection_exact = cleanup.song != nullptr
        && cleanup.profile != nullptr && cleanup.storage != nullptr
        && (!playback.token.valid()
            || (playback.generation == cleanup.generation
                && playback.storage == cleanup.storage
                && playback.song == cleanup.song
                && playback.profile == cleanup.profile
                && playback.profile_index == cleanup.profile_index
                && playback.visible_index == cleanup.visible_index
                && playback.base_slot == cleanup.base_slot));
    facts.registry_cleanup_controller_exact =
        cleanup.token.controller == monitor.controller;
    facts.registry_cleanup_slot_exact = cleanup.token.slot == monitor.slot;
    facts.registry_cleanup_bgm_exact = cleanup.token.bgm == monitor.bgm;
    facts.registry_cleanup_sound_exact = cleanup.token.sound == monitor.custom_sound;
    facts.registry_cleanup_request_exact =
        cleanup.token.request_handle == monitor.request_handle;
    facts.registry_cleanup_route_exact =
        cleanup.token.route_generation == monitor.route_generation;
    facts.registry_cleanup_lease_exact =
        cleanup.token.lease_generation == monitor.lease_identity.generation;
    facts.registry_exact = facts.registry_cleanup_selection_exact
        && bgm_playback_aggregate_terminal_registry_exact(
        facts.registry_playback_absent,
        facts.registry_playback_active_exact,
        facts.registry_cleanup_valid,
        facts.registry_cleanup_controller_exact,
        facts.registry_cleanup_slot_exact,
        facts.registry_cleanup_bgm_exact,
        facts.registry_cleanup_sound_exact,
        facts.registry_cleanup_request_exact,
        facts.registry_cleanup_route_exact,
        facts.registry_cleanup_lease_exact);

    if (observe_native) {
        const AudioNativeRouteObservation native = observe_native_route(
            route.controller, route.controller_identity, route.generation,
            route.lease_identity);
        facts.native_route_empty = native.valid && !native.sound
            && native.request_handle == 0 && native.state == 0;
    }
}

void observe_bgm_aggregate_closed_terminal_handoff(
    const BgmPlaybackBorrowerRecord& trigger,
    const uint64_t collection_version,
    const std::array<BgmPlaybackBorrowerRecord, 32>& records,
    const uint8_t record_count) noexcept
{
    try {
        BgmPlaybackAggregateTerminalHandoffFacts facts;
        facts.candidate = trigger.last_marker
            == BgmPlaybackAggregateMarker::AggregateClosed;
        if (!facts.candidate) return;
        facts.collection_version = collection_version;
        bool borrowers_zero = true;
        for (size_t i = 0; i < record_count; ++i) {
            if (records[i].current_active) borrowers_zero = false;
        }
        for (size_t i = 0; i < record_count; ++i) {
            facts.records[facts.record_count++] =
                bgm_terminal_node(records[i], borrowers_zero);
            facts.aggregate_closed_semantics[
                facts.aggregate_closed_semantic_count++] =
                bgm_terminal_semantic_node(records[i]);
        }
        populate_bgm_terminal_environment(facts, records);
        if (!facts.quiescent_monitor) return;
        const auto closed_lineage =
            analyze_bgm_playback_aggregate_terminal_lineage(
                facts.records.data(), facts.record_count, facts.monitor_request);
        const uint64_t closed_leaf_ordinal =
            closed_lineage.leaf_index < facts.record_count
            ? facts.records[closed_lineage.leaf_index].ordinal : 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            if (g_bgm_playback_collection_version == facts.collection_version) {
                facts.version_recheck_exact = true;
                for (size_t i = 0; i < facts.record_count; ++i) {
                    bool found = false;
                    for (const auto& record : g_bgm_playback_borrowers) {
                        if (record.ordinal != facts.records[i].ordinal) continue;
                        found = record.version == facts.records[i].version
                            && (record.ordinal != closed_leaf_ordinal
                                || record.last_marker
                                    == BgmPlaybackAggregateMarker::AggregateClosed);
                        break;
                    }
                    facts.version_recheck_exact =
                        facts.version_recheck_exact && found;
                }
            }
        }
        if (classify_bgm_playback_aggregate_terminal_handoff(facts)
            == BgmPlaybackAggregateTerminalHandoffClassification::ExactAtAggregateClosed) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            AudioRouteState& route = g_audio_route_state;
            if (route.generation == facts.live_route_generation
                && route.stop_retirement.phase == AudioStopRetirementPhase::Quiescent
                && route.stop_retirement.route_generation
                    == facts.monitor_route_generation) {
                (void)arm_bgm_playback_aggregate_terminal_authority(
                    route.terminal_handoff, route.generation, facts);
            }
        }
        log_bgm_aggregate_terminal_handoff(
            "[audio_aggregate_terminal_handoff_closed]", facts);
    } catch (...) {
    }
}

BgmPlaybackAggregateTerminalStopDiagnostic
observe_bgm_aggregate_terminal_handoff_at_stop(
    void* controller,
    const bool chain_read,
    void* slot,
    void* bgm,
    void* sound,
    const uint64_t request,
    const uint8_t state) noexcept
{
    BgmPlaybackAggregateTerminalStopDiagnostic diagnostic;
    try {
        auto& facts = diagnostic.facts;
        auto& records = diagnostic.records;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            AudioRouteState& route = g_audio_route_state;
            if (!claim_bgm_playback_aggregate_terminal_authority(
                    route.terminal_handoff, route.generation)) {
                return diagnostic;
            }
            diagnostic.authority_generation = route.terminal_handoff.generation;
            facts = route.terminal_handoff.facts;
        }
        const auto armed_facts = facts;
        const auto armed_lineage = analyze_bgm_playback_aggregate_terminal_lineage(
            armed_facts.records.data(), armed_facts.record_count,
            armed_facts.monitor_request);
        const uint64_t armed_root_ordinal =
            armed_lineage.root_index < armed_facts.record_count
            ? armed_facts.records[armed_lineage.root_index].ordinal : 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            facts.candidate = true;
            facts.natural_stop_recheck = true;
            facts.collection_version = g_bgm_playback_collection_version;
            facts.version_recheck_exact = true;
            reset_bgm_playback_aggregate_terminal_stop_snapshot(facts);
            records = {};
            BgmPlaybackBorrowerRecord root;
            bool root_found = false;
            for (const auto& record : g_bgm_playback_borrowers) {
                if (record.ordinal != armed_root_ordinal) continue;
                root = record;
                root_found = true;
                break;
            }
            if (root_found) {
                for (const auto& record : g_bgm_playback_borrowers) {
                    if (!same_bgm_terminal_domain(root, record)) continue;
                    if (facts.record_count >= facts.records.size()) {
                        facts.version_recheck_exact = false;
                        break;
                    }
                    const size_t index = facts.record_count++;
                    records[index] = record;
                    facts.records[index] = bgm_terminal_node(record, true);
                    facts.pre_stop_semantics[facts.pre_stop_semantic_count++] =
                        bgm_terminal_semantic_node(record);
                }
            } else {
                facts.version_recheck_exact = false;
            }
            facts.aggregate_closed_to_pre_stop =
                classify_bgm_playback_aggregate_terminal_semantic_delta(
                    facts.aggregate_closed_semantics.data(),
                    facts.aggregate_closed_semantic_count,
                    armed_facts.collection_version,
                    facts.pre_stop_semantics.data(),
                    facts.pre_stop_semantic_count,
                    facts.collection_version);
            facts.version_recheck_exact =
                bgm_playback_aggregate_terminal_semantics_unchanged(
                    facts.aggregate_closed_to_pre_stop);
        }
        populate_bgm_terminal_environment(facts, records);
        facts.route_unchanged = facts.live_route_generation
            == armed_facts.live_route_generation;
        const auto lineage = analyze_bgm_playback_aggregate_terminal_lineage(
            facts.records.data(), facts.record_count, facts.monitor_request);
        bool identity_exact = false;
        if (lineage.leaf_index < facts.record_count) {
            const auto& leaf = records[lineage.leaf_index];
            identity_exact = controller == leaf.controller
                && slot == leaf.slot && bgm == leaf.bgm;
        }
        facts.null_chain_exact = chain_read && identity_exact
            && sound == nullptr && request == 0 && state == 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            facts.version_recheck_exact = facts.version_recheck_exact
                && g_bgm_playback_collection_version == facts.collection_version;
            if (facts.version_recheck_exact) {
                for (size_t i = 0; i < facts.record_count; ++i) {
                    bool found = false;
                    for (const auto& record : g_bgm_playback_borrowers) {
                        if (record.ordinal != facts.records[i].ordinal) continue;
                        found = record.version == facts.records[i].version;
                        break;
                    }
                    facts.version_recheck_exact =
                        facts.version_recheck_exact && found;
                }
            }
        }
        diagnostic.pre_classification =
            classify_bgm_playback_aggregate_terminal_handoff(facts);
        if (diagnostic.pre_classification
            != BgmPlaybackAggregateTerminalHandoffClassification::ExactAtNaturalStop) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            fail_bgm_playback_aggregate_terminal_authority(
                g_audio_route_state.terminal_handoff);
        }
        diagnostic.active = true;
    } catch (...) {
        try {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            fail_bgm_playback_aggregate_terminal_authority(
                g_audio_route_state.terminal_handoff);
        } catch (...) {
        }
        return {};
    }
    return diagnostic;
}

void finish_bgm_aggregate_terminal_handoff_after_stop(
    BgmPlaybackAggregateTerminalStopDiagnostic& diagnostic,
    const uint32_t native_call_count,
    const bool post_observation_ran,
    const bool post_observation_succeeded,
    void* controller,
    void* slot,
    void* bgm,
    void* sound,
    const uint64_t request,
    const uint8_t state) noexcept
{
    if (!diagnostic.active) return;
    BgmPlaybackAggregateCallbackRetirementResult callback_retirement{};
    bool callback_retirement_applied = false;
    const auto rollback_callback_retirement = [&]() noexcept {
        if (!callback_retirement_applied) return;
        try {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            BgmPlaybackAggregateCallbackRetirementTransaction tx;
            tx.authorized = true;
            tx.lineage_exact = true;
            tx.expected_collection_version =
                callback_retirement.next_collection_version;
            tx.current_collection_version = g_bgm_playback_collection_version;
            tx.record_count = callback_retirement.record_count;
            for (size_t i = 0; i < tx.record_count; ++i) {
                auto& facts = tx.records[i];
                facts.ordinal = callback_retirement.ordinals[i];
                facts.expected_version = callback_retirement.next_versions[i];
                size_t match_count = 0;
                for (const auto& record : g_bgm_playback_borrowers) {
                    if (record.ordinal != facts.ordinal) continue;
                    ++match_count;
                    facts.current_version = record.version;
                    facts.active = record.active;
                    facts.failed = record.failed;
                    facts.retired = record.terminal_callback_retired;
                }
                if (match_count != 1) facts.active = false;
            }
            const auto rollback =
                plan_bgm_playback_aggregate_callback_retirement_rollback(tx);
            if (!rollback.exact) {
                // Drifted or reused lineage remains detached fail-closed.
                callback_retirement_applied = false;
                return;
            }
            for (size_t i = 0; i < rollback.record_count; ++i) {
                for (auto& record : g_bgm_playback_borrowers) {
                    if (record.ordinal != rollback.ordinals[i]) continue;
                    record.terminal_callback_retired = false;
                    record.version = rollback.next_versions[i];
                    break;
                }
            }
            g_bgm_playback_collection_version = rollback.next_collection_version;
        } catch (...) {
        }
        callback_retirement_applied = false;
    };
    try {
        auto& facts = diagnostic.facts;
        facts.post_stop_recheck = true;
        facts.post_diagnostic_count = 1;
        facts.post_observation_ran = post_observation_ran;
        facts.post_observation_succeeded = post_observation_succeeded;
        facts.native_stop_called_once = native_call_count == 1;
        const auto lineage = analyze_bgm_playback_aggregate_terminal_lineage(
            facts.records.data(), facts.record_count, facts.monitor_request);
        bool identity_exact = false;
        if (lineage.leaf_index < facts.record_count) {
            const auto& leaf = diagnostic.records[lineage.leaf_index];
            identity_exact = controller == leaf.controller
                && slot == leaf.slot && bgm == leaf.bgm;
        }
        facts.post_null_chain_exact = post_observation_succeeded
            && identity_exact && sound == nullptr && request == 0 && state == 0;
        std::array<BgmPlaybackBorrowerRecord, 32> post_records{};
        uint64_t post_collection_version = 0;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            post_collection_version = g_bgm_playback_collection_version;
            BgmPlaybackBorrowerRecord root;
            bool root_found = false;
            const uint64_t root_ordinal = lineage.root_index < facts.record_count
                ? facts.records[lineage.root_index].ordinal : 0;
            for (const auto& record : g_bgm_playback_borrowers) {
                if (record.ordinal != root_ordinal) continue;
                root = record;
                root_found = true;
                break;
            }
            if (root_found) {
                for (const auto& record : g_bgm_playback_borrowers) {
                    if (!same_bgm_terminal_domain(root, record)) continue;
                    if (facts.post_stop_semantic_count
                        >= facts.post_stop_semantics.size()) break;
                    const size_t post_index = facts.post_stop_semantic_count++;
                    post_records[post_index] = record;
                    facts.post_stop_semantics[post_index] =
                        bgm_terminal_semantic_node(record);
                }
            }
        }
        facts.pre_to_post_stop =
            classify_bgm_playback_aggregate_terminal_semantic_delta(
                facts.pre_stop_semantics.data(), facts.pre_stop_semantic_count,
                facts.collection_version,
                facts.post_stop_semantics.data(), facts.post_stop_semantic_count,
                post_collection_version);
        BgmPlaybackAggregateTerminalHandoffFacts post_environment = facts;
        post_environment.record_count = facts.post_stop_semantic_count;
        post_environment.collection_version = post_collection_version;
        for (size_t i = 0; i < post_environment.record_count; ++i) {
            post_environment.records[i] = bgm_terminal_node(post_records[i], true);
        }
        populate_bgm_terminal_environment(post_environment, post_records, false);
        facts.post_live_route_generation =
            post_environment.live_route_generation;
        facts.post_monitor_exact = post_environment.quiescent_monitor
            && post_environment.monitor_origin == facts.monitor_origin
            && post_environment.monitor_epoch == facts.monitor_epoch
            && post_environment.monitor_route_generation
                == facts.monitor_route_generation
            && post_environment.monitor_request == facts.monitor_request;
        facts.post_backing_exact = post_environment.backing_exact;
        facts.post_owner_category = post_environment.owner_category;
        facts.post_owner_exact = post_environment.owner_exact;
        facts.post_frozen_exact = post_environment.frozen_exact;
        facts.post_lifecycle_exact = post_environment.lifecycle_exact;
        facts.post_pause_exact = !post_environment.pause_conflict;
        facts.post_registry_exact = post_environment.registry_exact;
        facts.post_setup_absent = post_environment.setup_absent;
        facts.post_handoff_absent = post_environment.handoff_absent;
        facts.post_release_absent = post_environment.release_absent;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            bool stable = g_bgm_playback_collection_version
                == post_collection_version;
            for (size_t i = 0; stable && i < facts.post_stop_semantic_count; ++i) {
                bool found = false;
                for (const auto& record : g_bgm_playback_borrowers) {
                    if (record.ordinal != facts.post_stop_semantics[i].ordinal) continue;
                    found = record.version == facts.post_stop_semantics[i].version;
                    break;
                }
                stable = stable && found;
            }
            facts.version_recheck_exact = facts.version_recheck_exact && stable;
        }
        const bool handoff_exact = bgm_playback_aggregate_terminal_cleanup_exact(
            facts, diagnostic.pre_classification);
        AudioStopRetirementState retirement;
        OnMemoryBankRetiredBackingEvidence backing;
        uint64_t retirement_epoch = 0;
        FrozenSoundPatchSnapshot frozen;
        UObjectIdentity retirement_controller_identity;
        bool authority_current = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            AudioRouteState& route = g_audio_route_state;
            authority_current = handoff_exact
                && route.terminal_handoff.state
                    == BgmPlaybackAggregateTerminalAuthorityState::InFlight
                && route.terminal_handoff.generation
                    == diagnostic.authority_generation
                && route.generation == facts.post_live_route_generation;
            if (authority_current) {
                retirement = route.stop_retirement;
                retirement_epoch = route.stop_retirement_epoch;
                backing = route.stop_retirement_backing;
                frozen = route.frozen_sound_patch;
                retirement_controller_identity = route.controller_identity;
            } else {
                fail_bgm_playback_aggregate_terminal_authority(
                    route.terminal_handoff);
            }
        }
        if (!authority_current) return;
        const auto final_lineage = analyze_bgm_playback_aggregate_terminal_lineage(
            post_environment.records.data(), post_environment.record_count,
            retirement.request_handle);
        if (!final_lineage.exact
            || final_lineage.root_index >= post_environment.record_count
            || final_lineage.leaf_index >= post_environment.record_count) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            fail_bgm_playback_aggregate_terminal_authority(
                g_audio_route_state.terminal_handoff);
            return;
        }
        const auto& leaf = post_environment.records[final_lineage.leaf_index];
        const AudioRetirementRouteAuthorityFacts route_authority{
            AudioRetirementRouteAuthority::TerminalAggregateHandoff,
            true,
            retirement.route_generation,
            facts.post_live_route_generation,
            leaf.latest_custom_route_generation,
            leaf.latest_custom_request,
            frozen.route_generation,
            retirement.request_handle,
        };
        StopRetirementRead retirement_read = read_stop_retirement(
            retirement.controller, retirement_controller_identity,
            retirement.route_generation, retirement.lease_identity,
            retirement.slot, retirement.bgm,
            retirement.custom_sound, retirement.request_handle);
        OnMemoryBankDiagnosticPair release_diagnostic;
        OnMemoryBankReleaseAction release_action;
        PauseResumeBankHoldProbe hold_probe;
        if (!bgm_playback_aggregate_terminal_callback_retirement_authorized(
                handoff_exact, authority_current, facts.cleanup_committed)) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            fail_bgm_playback_aggregate_terminal_authority(
                g_audio_route_state.terminal_handoff);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            BgmPlaybackAggregateCallbackRetirementTransaction tx;
            tx.authorized = true;
            tx.lineage_exact = final_lineage.exact;
            tx.root_ordinal = post_environment.records[final_lineage.root_index].ordinal;
            tx.leaf_ordinal = post_environment.records[final_lineage.leaf_index].ordinal;
            tx.expected_collection_version = post_environment.collection_version;
            tx.current_collection_version = g_bgm_playback_collection_version;
            tx.record_count = post_environment.record_count;
            for (size_t i = 0; i < tx.record_count; ++i) {
                auto& record_facts = tx.records[i];
                record_facts.ordinal = post_environment.records[i].ordinal;
                record_facts.expected_version = post_environment.records[i].version;
                size_t match_count = 0;
                for (const auto& record : g_bgm_playback_borrowers) {
                    if (record.ordinal != record_facts.ordinal) continue;
                    ++match_count;
                    record_facts.current_version = record.version;
                    record_facts.active = record.active;
                    record_facts.failed = record.failed;
                    record_facts.retired = record.terminal_callback_retired;
                }
                if (match_count != 1) record_facts.active = false;
            }
            callback_retirement =
                plan_bgm_playback_aggregate_callback_retirement(tx);
            if (callback_retirement.exact) {
                for (size_t i = 0; i < callback_retirement.record_count; ++i) {
                    for (auto& record : g_bgm_playback_borrowers) {
                        if (record.ordinal != callback_retirement.ordinals[i]) continue;
                        record.terminal_callback_retired = true;
                        record.version = callback_retirement.next_versions[i];
                        break;
                    }
                }
                g_bgm_playback_collection_version =
                    callback_retirement.next_collection_version;
                callback_retirement_applied = true;
            }
        }
        if (!callback_retirement_applied) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            fail_bgm_playback_aggregate_terminal_authority(
                g_audio_route_state.terminal_handoff);
            return;
        }
        if (!commit_audio_cleanup_after_retirement(
                retirement, retirement_read, retirement_epoch, backing,
                &release_diagnostic, &release_action, &hold_probe,
                &route_authority)) {
            rollback_callback_retirement();
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            fail_bgm_playback_aggregate_terminal_authority(
                g_audio_route_state.terminal_handoff);
            return;
        }
        // Cleanup and callback retirement now form one committed outcome.
        // Subsequent exception containment must not reattach stale callbacks.
        callback_retirement_applied = false;
        facts.cleanup_committed = true;
        diagnostic.retirement_diagnostic = release_diagnostic;
        diagnostic.retirement_release = release_action;
    } catch (...) {
        rollback_callback_retirement();
        try {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            fail_bgm_playback_aggregate_terminal_authority(
                g_audio_route_state.terminal_handoff);
        } catch (...) {
        }
    }
}

bool commit_audio_cleanup_after_retirement(
    const AudioStopRetirementState& retirement,
    const StopRetirementRead& retirement_read,
    uint64_t retirement_epoch,
    const OnMemoryBankRetiredBackingEvidence& retired_backing_evidence,
    OnMemoryBankDiagnosticPair* post_retirement_diagnostic,
    OnMemoryBankReleaseAction* post_retirement_release,
    PauseResumeBankHoldProbe* pause_hold_probe,
    const AudioRetirementRouteAuthorityFacts* terminal_route_authority = nullptr)
{
    if (post_retirement_diagnostic) *post_retirement_diagnostic = {};
    if (post_retirement_release) *post_retirement_release = {};
    if (pause_hold_probe) *pause_hold_probe = {};
    BgmPlaybackBorrowerRecord aggregate_snapshot;
    uint64_t aggregate_collection_version = 0;
    bool aggregate_snapshot_found = false;
    {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        aggregate_collection_version = g_bgm_playback_collection_version;
        for (const auto& record : g_bgm_playback_borrowers) {
            if (record.old_handle != retirement.request_handle) continue;
            if (!aggregate_snapshot_found
                || record.ordinal > aggregate_snapshot.ordinal) {
                aggregate_snapshot = record;
                aggregate_snapshot_found = true;
            }
        }
    }
    const SelectionSnapshot retained_selection = registry().selection_snapshot();
    AudioRouteState route;
    AudioRetirementRouteAuthorityFacts cleanup_route_authority;
    AudioRetirementOnMemoryRequestProjection onmemory_request_projection;
    bool journals_restored = false;
    bool immutable_identity_matches = false;
    AudioRetirementCleanupFailureFacts failure_facts;
    failure_facts.origin_valid = retirement.origin() == AudioStopRetirementOrigin::OrdinaryStop
        || retirement.origin() == AudioStopRetirementOrigin::ExactNaturalCompletion;
    failure_facts.retirement_quiescent =
        retirement.phase == AudioStopRetirementPhase::Quiescent;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route = g_audio_route_state;
        cleanup_route_authority =
            terminal_route_authority ? *terminal_route_authority
                : AudioRetirementRouteAuthorityFacts{
                    AudioRetirementRouteAuthority::DirectSuccessor,
                    false,
                    retirement.route_generation,
                    route.generation,
                    0,
                    0,
                     route.frozen_sound_patch.route_generation,
                     retirement.request_handle};
        onmemory_request_projection = project_audio_retirement_onmemory_request({
            retirement.request_handle,
            route.owned_request_handle,
            cleanup_route_authority,
        });
        journals_restored = g_active_patch_journal.empty()
            && g_pending_play_setup_patch.patches.empty()
            && g_failed_patch_journal.empty();
        immutable_identity_matches = g_frozen_profile_lease.active()
            && g_frozen_profile_lease.identity() == retirement.lease_identity
            && route.lease_identity == retirement.lease_identity
            && route.stop_retirement.phase == AudioStopRetirementPhase::Quiescent
            && route.stop_retirement.route_generation == retirement.route_generation
            && route.stop_retirement.lease_identity == retirement.lease_identity
            && route.stop_retirement.controller == retirement.controller
            && route.stop_retirement.slot == retirement.slot
            && route.stop_retirement.bgm == retirement.bgm
            && route.stop_retirement.custom_sound == retirement.custom_sound
            && route.stop_retirement.request_handle == retirement.request_handle
            && route.stop_retirement.origin() == retirement.origin()
            && route.stop_retirement_epoch == retirement_epoch
            && same_onmemory_bank_retired_backing_evidence(
                route.stop_retirement_backing, retired_backing_evidence)
            && audio_retirement_route_authority_proven(cleanup_route_authority)
            && onmemory_request_projection.accepted
            && route.controller == retirement.controller
            && audio_retirement_owned_route_identity_exact({
                retirement.slot, retirement.bgm, retirement.custom_sound,
                retirement.request_handle, route.owned_slot, route.owned_bgm,
                route.owned_sound, route.owned_request_handle})
            && route.frozen_sound_patch.lease_identity == retirement.lease_identity;
    }
    const bool terminal_request_domain = terminal_route_authority != nullptr;
    const bool root_request_valid =
        AudioBgmRequestHandle{retirement.request_handle}.valid_bgm_request();
    const bool owned_root_request_exact =
        audio_retirement_owned_request_identity_exact(
            retirement.request_handle, route.owned_request_handle);
    const bool leaf_request_valid = terminal_request_domain
        && AudioBgmRequestHandle{cleanup_route_authority.terminal_leaf_request_handle}
            .valid_bgm_request();
    log_audio_retirement_request_identity(
        terminal_request_domain,
        root_request_valid,
        owned_root_request_exact,
        leaf_request_valid,
        terminal_request_domain
            && cleanup_route_authority.terminal_leaf_request_handle
                == retirement.request_handle,
        terminal_request_domain
            && route.owned_request_handle
                == cleanup_route_authority.terminal_leaf_request_handle);
    failure_facts.immutable_identity_matches = immutable_identity_matches;
    failure_facts.live_route_successor =
        audio_retirement_route_authority_proven(cleanup_route_authority);
    failure_facts.journals_restored = journals_restored;

    void* retained_retired_backing = nullptr;
    const OnMemoryBankRetiredBackingRequirement backing_requirement =
        retirement.origin() == AudioStopRetirementOrigin::ExactNaturalCompletion
        ? OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion
        : OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence;
    const bool exact_request_retired = onmemory_bank_exact_request_retired(
        retired_backing_evidence,
        retirement.route_generation,
        retirement.lease_identity.generation,
        retirement_epoch,
        retirement.request_handle,
        route.frozen_sound_patch.sound_identity.live,
        retirement_read.observation.valid
            && !retirement_read.observation.request_handle_retired,
        retained_retired_backing,
        backing_requirement);
    failure_facts.backing_proven = exact_request_retired;
    if (!exact_request_retired) {
        log_audio_retirement_cleanup_failure(
            first_audio_retirement_cleanup_failure(failure_facts),
            retirement, route.generation, route.frozen_sound_patch.route_generation,
            retired_backing_evidence);
        return false;
    }

    const AudioNativeRouteObservation current_route = observe_native_route(
        retirement.controller,
        route.controller_identity,
        route.generation,
        retirement.lease_identity);
    const bool current_route_empty = current_route.valid
        && current_route.sound == nullptr
        && current_route.request_handle == 0
        && current_route.state == 0;
    const AudioDeferredNativeHandoffState& deferred = route.deferred_native_handoff;
    const bool current_native_identity_matches = !current_route_empty
        && (deferred.phase == AudioDeferredNativeHandoffPhase::RetainedFailure
            || deferred.phase == AudioDeferredNativeHandoffPhase::NativePlayForwarded)
        && deferred.route_generation == route.generation
        && deferred.lease_identity == retirement.lease_identity
        && deferred.controller == retirement.controller
        && deferred.slot == retirement.slot
        && deferred.bgm == retirement.bgm
        && deferred.retired_custom_request_handle == retirement.request_handle
        && deferred.requested_sound == current_route.sound
        && uobject_identity_matches(
            deferred.requested_sound, route.deferred_native_sound_identity);
    bool owner_field_observed = false;
    bool owner_field_empty = false;
    const bool custom_sound_fields_restored = frozen_sound_fields_restored_for_retirement(
        route.frozen_sound_patch, retirement, route.generation,
        &owner_field_observed, &owner_field_empty, terminal_route_authority);
    failure_facts.frozen_fields_restored = custom_sound_fields_restored;
    failure_facts.native_route_proven =
        current_route_empty || current_native_identity_matches;
    const AudioRetirementCleanupEvidence evidence{
        retirement,
        retirement_read.observation,
        current_route,
        immutable_identity_matches,
        journals_restored,
        custom_sound_fields_restored,
        current_native_identity_matches,
        cleanup_route_authority,
    };
    failure_facts.cleanup_policy_accepted = audio_retirement_cleanup_proven(evidence);
    if (!failure_facts.cleanup_policy_accepted) {
        BgmPlaybackAggregateRetirementSuccessorFacts successor_facts;
        successor_facts.direct_successor_cleanup_refused =
            first_audio_retirement_cleanup_failure(failure_facts)
                == AudioRetirementCleanupFailure::LiveRouteNotSuccessor;
        successor_facts.monitor_origin = static_cast<uint8_t>(retirement.origin());
        successor_facts.monitor_epoch = retirement_epoch;
        successor_facts.monitor_route_generation = retirement.route_generation;
        successor_facts.monitor_request_handle = retirement.request_handle;
        successor_facts.monitor_request_generation = retirement.request_handle >> 32;
        successor_facts.live_route_generation = route.generation;
        successor_facts.aggregate_snapshot_found = aggregate_snapshot_found;
        successor_facts.aggregate_ordinal = aggregate_snapshot.ordinal;
        successor_facts.aggregate_version = aggregate_snapshot.version;
        successor_facts.collection_version = aggregate_collection_version;
        successor_facts.aggregate_active = aggregate_snapshot.active;
        successor_facts.aggregate_failed = aggregate_snapshot.failed;
        successor_facts.terminal_state = aggregate_snapshot.terminal_state;
        successor_facts.aggregate_marker_closed = aggregate_snapshot.last_marker
            == BgmPlaybackAggregateMarker::AggregateClosed;
        successor_facts.anchor_exact = aggregate_snapshot.retirement_anchored
            && aggregate_snapshot.anchor_request_handle == retirement.request_handle
            && aggregate_snapshot.anchor_route_generation != UINT64_MAX
            && aggregate_snapshot.anchor_route_generation + 1
                == retirement.route_generation;
        successor_facts.controller_exact = aggregate_snapshot.controller
                == retirement.controller
            && controller_identity_proof_valid(aggregate_snapshot.controller_proof);
        successor_facts.lease_exact = aggregate_snapshot.lease
                == retirement.lease_identity
            && aggregate_snapshot.transition_lease == retirement.lease_identity;
        successor_facts.lifecycle_exact =
            aggregate_snapshot.anchor_state_epoch == retired_backing_evidence.lifecycle_epoch
            && aggregate_snapshot.lifecycle_state_epoch
                == retired_backing_evidence.lifecycle_epoch
            && aggregate_snapshot.anchor_cleanup_generation
                == retired_backing_evidence.cleanup_generation;
        successor_facts.tokens_exact =
            aggregate_snapshot.token_epoch == retired_backing_evidence.detached_ordinal
            && aggregate_snapshot.canonical
                == retired_backing_evidence.canonical.encode()
            && aggregate_snapshot.custom
                == retired_backing_evidence.custom.encode();
        successor_facts.sound_exact = aggregate_snapshot.sound
                == retirement.custom_sound
            && aggregate_snapshot.sound_identity.internal_index
                == route.frozen_sound_patch.sound_identity.live.internal_index
            && aggregate_snapshot.sound_identity.serial_number
                == route.frozen_sound_patch.sound_identity.live.serial_number;
        successor_facts.old_request_generation = aggregate_snapshot.old_handle >> 32;
        successor_facts.canonical_request_generation =
            aggregate_snapshot.canonical_intermediate_handle >> 32;
        successor_facts.latest_custom_request_generation =
            aggregate_snapshot.latest_custom_transition_handle >> 32;
        successor_facts.old_request_handle = aggregate_snapshot.old_handle;
        successor_facts.canonical_request_handle =
            aggregate_snapshot.canonical_intermediate_handle;
        successor_facts.latest_custom_request_handle =
            aggregate_snapshot.latest_custom_transition_handle;
        successor_facts.old_absent = aggregate_snapshot.old_absent;
        successor_facts.canonical_absent =
            aggregate_snapshot.canonical_intermediate_absent;
        successor_facts.latest_custom_absent =
            aggregate_snapshot.latest_custom_transition_absent;
        successor_facts.latest_custom_consumed =
            aggregate_snapshot.latest_custom_transition_consumed;
        successor_facts.anchor_route_generation =
            aggregate_snapshot.anchor_route_generation;
        successor_facts.canonical_input_route_generation =
            aggregate_snapshot.canonical_set_input_route_generation;
        successor_facts.canonical_route_generation =
            aggregate_snapshot.canonical_intermediate_route_generation;
        successor_facts.latest_custom_route_generation =
            aggregate_snapshot.latest_custom_transition_route_generation;
        successor_facts.operation_predecessor_nonce =
            aggregate_snapshot.operation_predecessor_nonce;
        successor_facts.transition_set_nonce = aggregate_snapshot.transition_set_nonce;
        successor_facts.transition_play_nonce = aggregate_snapshot.transition_play_nonce;
        successor_facts.operation_nonce_exact =
            aggregate_snapshot.operation_predecessor_nonce
                == aggregate_snapshot.transition_play_nonce
            && bgm_playback_operation_nonce_next(
                aggregate_snapshot.transition_set_nonce,
                aggregate_snapshot.transition_play_nonce);
        successor_facts.route_chain_exact = retirement.route_generation <= UINT64_MAX - 2
            && aggregate_snapshot.canonical_set_input_route_generation
                == retirement.route_generation
            && aggregate_snapshot.canonical_intermediate_route_generation
                == retirement.route_generation + 1
            && aggregate_snapshot.latest_custom_transition_route_generation
                == retirement.route_generation + 2
            && aggregate_snapshot.latest_custom_transition_route_generation
                == route.generation;
        successor_facts.journals_restored = journals_restored;
        successor_facts.frozen_fields_restored = custom_sound_fields_restored;
        successor_facts.owner_field_observed = owner_field_observed;
        successor_facts.owner_field_empty = owner_field_empty;
        successor_facts.native_route_empty = current_route_empty;
        successor_facts.native_route_identity_exact = current_native_identity_matches;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            if (aggregate_snapshot_found
                && g_bgm_playback_collection_version == aggregate_collection_version) {
                for (const auto& current : g_bgm_playback_borrowers) {
                    if (current.ordinal == aggregate_snapshot.ordinal) {
                        successor_facts.version_recheck_exact =
                            current.version == aggregate_snapshot.version
                            && current.last_marker == aggregate_snapshot.last_marker;
                        break;
                    }
                }
            }
        }
        log_bgm_aggregate_retirement_successor(successor_facts);
        log_audio_retirement_cleanup_failure(
            first_audio_retirement_cleanup_failure(failure_facts),
            retirement, route.generation, route.frozen_sound_patch.route_generation,
            retired_backing_evidence);
        core::log(core::LogLevel::Error,
            "[audio_sead] cleanup_after_retirement status=validation_failed cleanup=retained");
        return false;
    }

    AudioRouteCleanupResult result;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        AudioRouteState& live = g_audio_route_state;
        const bool exact_natural_lifecycle_current =
            retirement.origin() != AudioStopRetirementOrigin::ExactNaturalCompletion
            || exact_natural_completion_backing_matches_lifecycle(
                retired_backing_evidence, g_onmemory_bank_lifecycle);
        const bool terminal_authority_current = !terminal_route_authority
            || (live.terminal_handoff.state
                    == BgmPlaybackAggregateTerminalAuthorityState::InFlight
                && live.terminal_handoff.generation == route.generation
                && audio_retirement_route_authority_proven(
                    *terminal_route_authority));
        const AudioRetirementOnMemoryRequestProjection
            live_onmemory_request_projection =
                project_audio_retirement_onmemory_request({
                    retirement.request_handle,
                    live.owned_request_handle,
                    cleanup_route_authority,
                });
        if (!g_frozen_profile_lease.active()
            || !(g_frozen_profile_lease.identity() == retirement.lease_identity)
            || live.generation != route.generation
            || !(live.lease_identity == retirement.lease_identity)
            || live.controller != retirement.controller
            || !audio_retirement_owned_route_identity_exact({
                retirement.slot, retirement.bgm, retirement.custom_sound,
                retirement.request_handle, live.owned_slot, live.owned_bgm,
                live.owned_sound, live.owned_request_handle})
            || live.stop_retirement.phase != AudioStopRetirementPhase::Quiescent
            || live.stop_retirement.request_handle != retirement.request_handle
            || live.stop_retirement.origin() != retirement.origin()
            || live.stop_retirement_epoch != retirement_epoch
            || !same_onmemory_bank_retired_backing_evidence(
                live.stop_retirement_backing, retired_backing_evidence)
            || !exact_natural_lifecycle_current
            || !terminal_authority_current
            || !live_onmemory_request_projection.accepted
            || !g_active_patch_journal.empty()
            || !g_pending_play_setup_patch.patches.empty()
            || !g_failed_patch_journal.empty()) {
            core::log(core::LogLevel::Error,
                "[audio_sead] cleanup_after_retirement status=commit_race cleanup=retained");
            return false;
        }
        AudioCleanupEvidence cleanup;
        cleanup.operation = AudioCleanupOperation::ListReturn;
        cleanup.active_journal_empty = true;
        cleanup.pending_journal_empty = true;
        cleanup.failed_journal_empty = true;
        cleanup.auxiliary_journals_restored = true;
        cleanup.immutable_identity_matches = true;
        cleanup.native_clear_verified = true;
        cleanup.route_state_unchanged = true;
        cleanup.native_attempted = true;
        result = apply_audio_cleanup(
            g_frozen_profile_lease, retirement.lease_identity, cleanup);
        if (!result.clear_route_metadata) {
            core::log(core::LogLevel::Error,
                "[audio_sead] cleanup_after_retirement status=lease_transition_failed cleanup=retained");
            return false;
        }
        g_pending_play_setup_patch = {};
        g_failed_patch_journal.clear();
        g_piano_audio_request_profile = {};
        g_native_play_setup_profile = {};
        g_slot_setup_profile = {};
        AudioRouteTransitionRecorder route_transition_record(
            AudioRouteTransitionReason::RetirementCleanup,
            AudioRouteTransitionKind::RouteReset);
        g_audio_route_state = {};
    }
    if (result.thaw_profile) {
        registry().clear_frozen_profile();
    }
    retire_registry_cleanup(retirement.lease_identity);

    const PlaybackSnapshot current_playback = registry().playback_snapshot();
    const CleanupLease current_cleanup = registry().cleanup_lease();
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const OnMemoryBankDetachedRecord& detached = g_onmemory_bank_lifecycle.active();
        const OnMemoryBankSoundIdentity observed_sound{
            route.frozen_sound_patch.sound,
            route.frozen_sound_patch.sound_identity.live,
        };
        const bool structural_identity_matches = detached
            && observed_sound == detached.sound
            && observed_sound.object == retirement.custom_sound
            && uobject_identity_matches(
                observed_sound.object, route.frozen_sound_patch.sound_identity);
        uint64_t current_owner_token = 0;
        const bool current_owner_readable = structural_identity_matches
            && core::safe_read_field(
                observed_sound.object,
                runtime_layouts::SqexSeadSound::observed_field548,
                current_owner_token);

        OnMemoryBankRetirementFacts facts;
        facts.route_generation = retirement.route_generation;
        facts.cleanup_generation = retirement.lease_identity.generation;
        facts.retired_request_handle =
            onmemory_request_projection.retired_request_handle;
        facts.current_request_handle = current_route.request_handle;
        facts.current_backing = retirement_read.current_backing;
        facts.retired_backing = retained_retired_backing;
        facts.current_backing_observed = retirement_read.current_backing_observed;
        facts.retired_backing_observed = exact_request_retired;
        facts.current_owner = classify_onmemory_bank_retirement_owner(
            detached,
            observed_sound,
            structural_identity_matches,
            current_owner_readable,
            current_owner_token);
        facts.exact_request_retired = exact_request_retired;
        facts.route_released = g_audio_route_state.phase == AudioRoutePhase::Idle;
        facts.playback_released = !current_playback.token.valid();
        facts.cleanup_released = !current_cleanup.token.valid();
        facts.owner_restore_verified = custom_sound_fields_restored;
        facts.runtime_installed =
            g_audio_route_installed.load(std::memory_order_acquire);
        facts.lookup_signature_valid =
            g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire);
        facts.release_signature_valid =
            g_onmemory_bank_release_available.load(std::memory_order_acquire);
        facts.shutdown_or_disabled =
            !g_audio_route_installed.load(std::memory_order_acquire)
            || g_audio_route_disabled.load(std::memory_order_acquire);
        bool pause_hold = false;
        if (pause_hold_probe
            && g_pause_resume_bank.phase == PauseResumeBankPhase::RetirementCandidate
            && g_pause_resume_bank.detached.ordinal == detached.ordinal) {
            OnMemoryBankLifecycleState validation = g_onmemory_bank_lifecycle;
            OnMemoryBankReleaseAction validation_action;
            pause_hold = validation.claim_release(facts, validation_action);
            if (pause_hold) {
                pause_hold_probe->eligible = true;
                pause_hold_probe->facts = facts;
                pause_hold_probe->detached = detached;
                pause_hold_probe->lifecycle_state_epoch =
                    g_onmemory_bank_lifecycle.state_epoch();
                pause_hold_probe->selection = retained_selection;
                pause_hold_probe->lease_identity = retirement.lease_identity;
                pause_hold_probe->controller = route.controller;
                pause_hold_probe->controller_identity = route.controller_identity;
                pause_hold_probe->exit_requested =
                    g_pause_resume_bank.exit_requested;
            }
        }
        if (post_retirement_release && !pause_hold) {
            // Claim the generation-keyed release barrier before making either
            // deferred native observation eligible. Rejected retirement facts
            // therefore schedule neither diagnostics nor release work.
            const OnMemoryBankRetirementSchedule scheduled =
                coordinate_onmemory_bank_retirement(
                    g_onmemory_bank_lifecycle,
                    facts,
                    g_onmemory_bank_diagnostic_pair,
                    route.frozen_sound_patch.route_generation,
                    retirement.lease_identity.generation);
            *post_retirement_release = scheduled.release;
            if (post_retirement_diagnostic) {
                *post_retirement_diagnostic = scheduled.diagnostic;
            }
        }
    }

    std::ostringstream out;
    out << "[audio_sead] cleanup_committed_after_retirement"
        << " old_request_handle=0x" << std::hex << retirement.request_handle
        << " current_request_handle=0x" << current_route.request_handle
        << " controller=0x" << reinterpret_cast<uintptr_t>(retirement.controller)
        << " slot=0x" << reinterpret_cast<uintptr_t>(retirement.slot)
        << " bgm=0x" << reinterpret_cast<uintptr_t>(retirement.bgm)
        << std::dec
        << " stop_generation=" << retirement.route_generation
        << " route_generation=" << current_route.route_generation
        << " lease_generation=" << retirement.lease_identity.generation
        << " song_key=" << retirement.lease_identity.song_key
        << " route_state=" << static_cast<unsigned>(current_route.state)
        << " thawed=" << (result.thaw_profile ? 1 : 0);
    core::log(core::LogLevel::Info, out.str());
    return true;
}

void poll_audio_stop_retirement_monitor(
    OnMemoryBankDiagnosticPair* post_retirement_diagnostic,
    OnMemoryBankReleaseAction* post_retirement_release,
    PauseResumeBankHoldProbe* pause_hold_probe)
{
    AudioStopRetirementState monitor;
    uint64_t monitor_epoch = 0;
    OnMemoryBankRetiredBackingEvidence retained_backing;
    UObjectLiveHandle monitor_sound_identity;
    UObjectIdentity controller_identity;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (!g_audio_route_state.stop_retirement.active()) {
            return;
        }
        monitor = g_audio_route_state.stop_retirement;
        monitor_epoch = g_audio_route_state.stop_retirement_epoch;
        retained_backing = g_audio_route_state.stop_retirement_backing;
        monitor_sound_identity = g_audio_route_state.frozen_sound_patch.sound_identity.live;
        controller_identity = g_audio_route_state.controller_identity;
    }

    const StopRetirementRead current = read_stop_retirement(
        monitor.controller,
        controller_identity,
        monitor.route_generation,
        monitor.lease_identity,
        monitor.slot,
        monitor.bgm,
        monitor.custom_sound,
        monitor.request_handle);
    AudioStopRetirementState updated = monitor;
    OnMemoryBankRetiredBackingEvidence updated_backing = retained_backing;
    if (current.observation.request_handle_retired) {
        (void)record_onmemory_bank_retired_backing(
            updated_backing,
            {
                monitor.route_generation,
                monitor.lease_identity.generation,
                monitor_epoch,
                monitor.request_handle,
                monitor_sound_identity,
                true,
                current.retired_backing_observed,
                current.retired_backing,
            });
    }
    AudioStopRetirementObservation phase_observation = current.observation;
    const OnMemoryBankRetiredBackingRequirement backing_requirement =
        monitor.origin() == AudioStopRetirementOrigin::ExactNaturalCompletion
        ? OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion
        : OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence;
    if (onmemory_bank_retirement_presence_pending(updated_backing,
            phase_observation.valid,
            phase_observation.request_handle_retired,
            backing_requirement)) {
        // Until presence has supplied backing evidence, absence cannot prove
        // retirement; preserve the bounded monitor wait for async Stop publish.
        phase_observation.request_handle_retired = true;
    }
    const AudioStopRetirementPhase phase = advance_audio_stop_retirement_monitor(
        updated, phase_observation, 300);
    if (phase == AudioStopRetirementPhase::Invalid
        || phase == AudioStopRetirementPhase::TimedOut) {
        updated_backing = {};
    }
    bool committed = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        AudioStopRetirementState& live = g_audio_route_state.stop_retirement;
        if (live.phase == monitor.phase
            && live.route_generation == monitor.route_generation
            && live.lease_identity == monitor.lease_identity
            && live.controller == monitor.controller
            && live.slot == monitor.slot
            && live.bgm == monitor.bgm
            && live.custom_sound == monitor.custom_sound
            && live.request_handle == monitor.request_handle
            && live.poll_count == monitor.poll_count
            && live.origin() == monitor.origin()
            && g_audio_route_state.stop_retirement_epoch == monitor_epoch
            && same_onmemory_bank_retired_backing_evidence(
                g_audio_route_state.stop_retirement_backing, retained_backing)) {
            live = updated;
            g_audio_route_state.stop_retirement_backing = updated_backing;
            committed = true;
        }
    }
    if (!committed) {
        return;
    }
    if (phase != AudioStopRetirementPhase::Waiting
        || updated.poll_count == 1
        || updated.poll_count % 30 == 0) {
        log_stop_retirement("owner_tick", current, phase, updated.poll_count);
    }
    if (monitor.phase == AudioStopRetirementPhase::Waiting
        && phase == AudioStopRetirementPhase::Quiescent) {
        (void)commit_audio_cleanup_after_retirement(
            updated, current, monitor_epoch, updated_backing,
            post_retirement_diagnostic,
            post_retirement_release,
            pause_hold_probe);
    }
}

bool same_pause_resume_retirement_monitor(
    const AudioStopRetirementState& expected,
    const AudioStopRetirementState& current) noexcept
{
    return expected.phase == current.phase
        && expected.route_generation == current.route_generation
        && expected.lease_identity == current.lease_identity
        && expected.controller == current.controller
        && expected.slot == current.slot
        && expected.bgm == current.bgm
        && expected.custom_sound == current.custom_sound
        && expected.request_handle == current.request_handle
        && expected.poll_count == current.poll_count
        && expected.origin() == current.origin();
}

void poll_pause_resume_bank_retirement(
    OnMemoryBankDiagnosticPair* diagnostic,
    OnMemoryBankReleaseAction* release,
    PauseResumeBankMarker* marker,
    PauseResumeReleaseAuthorityProbe* authority_probe)
{
    PauseResumeBankSession snapshot;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (!pause_resume_retirement_poll_allowed(
                g_pause_resume_bank.phase,
                g_pause_resume_bank.retirement_monitor.active(),
                g_pause_resume_bank.pending_release_probe)) return;
        snapshot = g_pause_resume_bank;
    }
    StopRetirementRead current = read_stop_retirement(
        snapshot.controller, snapshot.controller_identity,
        snapshot.detached.route_generation + 1, snapshot.lease_identity,
        snapshot.slot, snapshot.bgm, snapshot.detached.sound.object,
        snapshot.request_handle);
    AudioStopRetirementState updated_monitor = snapshot.retirement_monitor;
    OnMemoryBankRetiredBackingEvidence updated_backing = snapshot.retired_backing;
    if (current.observation.request_handle_retired) {
        OnMemoryBankRetiredBackingObservation observed;
        observed.route_generation = snapshot.detached.route_generation + 1;
        observed.cleanup_generation = snapshot.detached.cleanup_generation;
        observed.monitor_epoch = snapshot.retirement_epoch;
        observed.request_handle = snapshot.request_handle;
        observed.sound_identity = snapshot.detached.sound.live;
        observed.exact_request_present = true;
        observed.backing_observed = current.retired_backing_observed;
        observed.backing = current.retired_backing;
        (void)record_onmemory_bank_retired_backing(updated_backing, observed);
    }
    AudioStopRetirementObservation observation = current.observation;
    if (onmemory_bank_retirement_presence_pending(updated_backing,
            observation.valid, observation.request_handle_retired)) {
        observation.request_handle_retired = true;
    }
    (void)advance_audio_stop_retirement_monitor(
        updated_monitor, observation, 300);
    if (updated_monitor.phase == AudioStopRetirementPhase::Invalid
        || updated_monitor.phase == AudioStopRetirementPhase::TimedOut) {
        updated_backing = {};
    }
    bool quiescent = false;
    void* retired_backing = nullptr;
    if (updated_monitor.phase == AudioStopRetirementPhase::Quiescent) {
        quiescent = onmemory_bank_exact_request_retired(
            updated_backing,
            snapshot.detached.route_generation + 1,
            snapshot.detached.cleanup_generation,
            snapshot.retirement_epoch,
            snapshot.request_handle,
            snapshot.detached.sound.live,
            current.observation.valid
                && !current.observation.request_handle_retired,
            retired_backing);
    }
    std::unique_lock<std::mutex> lock(g_audio_state_mutex);
    if (!pause_resume_epoch_matches(snapshot.session_epoch, snapshot.cycle_epoch,
            g_pause_resume_bank.session_epoch, g_pause_resume_bank.cycle_epoch)
        || !same_pause_resume_retirement_monitor(
            snapshot.retirement_monitor,
            g_pause_resume_bank.retirement_monitor)
        || !same_onmemory_bank_retired_backing_evidence(
            snapshot.retired_backing, g_pause_resume_bank.retired_backing)) return;
    g_pause_resume_bank.retired_backing = updated_backing;
    if (!quiescent) {
        g_pause_resume_bank.retirement_monitor = updated_monitor;
        if (updated_monitor.phase == AudioStopRetirementPhase::Quiescent
            && pause_resume_retirement_mismatch_admitted(
                g_pause_resume_bank.retirement_mismatch_recorded,
                PauseResumeRetirementMismatch::QuiescentProofMissing)) {
            g_pause_resume_bank.retirement_mismatch_recorded = true;
            if (marker) *marker = {
                PauseResumeBankMarkerStatus::RetirementMismatch,
                pause_resume_retirement_mismatch_name(
                    PauseResumeRetirementMismatch::QuiescentProofMissing),
                snapshot.session_epoch,
                snapshot.cycle_epoch,
                true};
        }
        if (updated_monitor.phase == AudioStopRetirementPhase::Invalid
            || updated_monitor.phase == AudioStopRetirementPhase::TimedOut) {
            g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
            if (marker) *marker = {PauseResumeBankMarkerStatus::Failed,
                "retirement_monitor", snapshot.session_epoch,
                snapshot.cycle_epoch, true};
        }
        return;
    }

    OnMemoryBankRetirementFacts facts = bind_onmemory_bank_retirement_facts(
        snapshot.retirement_facts, snapshot.detached,
        current.current_request_handle, current.current_backing,
        current.current_backing_observed, retired_backing,
        static_cast<bool>(updated_backing));
    const auto retirement_outcome = pause_resume_retirement_outcome(
        snapshot.phase, true, true, snapshot.exit_requested);
    if (retirement_outcome.advance_cycle) {
        const bool lifecycle_current = snapshot.lifecycle_state_epoch != 0
            && g_onmemory_bank_lifecycle.state_epoch()
                == snapshot.lifecycle_state_epoch;
        if (!lifecycle_current) {
            g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
            if (marker) *marker = {PauseResumeBankMarkerStatus::Failed,
                "cycle_authority", snapshot.session_epoch,
                snapshot.cycle_epoch, true};
            return;
        }
        g_pause_resume_bank.pending_release_probe = {};
        g_pause_resume_bank.pending_release_evidence = {};
        g_pause_resume_bank.retirement_monitor = {};
        g_pause_resume_bank.retired_backing = {};
        // The detached lifecycle record was rebound to this resumed request.
        // Preserve its exact completed-retirement evidence for a later
        // definitive list return instead of retaining the initial hold facts.
        g_pause_resume_bank.retirement_facts = facts;
        g_pause_resume_bank.phase = retirement_outcome.phase;
        g_pause_resume_bank.cycle_epoch = next_pause_resume_bank_epoch(
            g_pause_resume_bank.cycle_epoch);
        g_pause_resume_bank.retirement_mismatch_recorded = false;
        if (marker) *marker = {PauseResumeBankMarkerStatus::SuspendedReady,
            "cycle_retired", snapshot.session_epoch,
            g_pause_resume_bank.cycle_epoch, true};
        return;
    }

    const auto exposure = expose_pause_resume_release_probe(
        g_pause_resume_bank_release_probe_epoch,
        g_pause_resume_bank.pending_release_probe,
        snapshot.phase,
        snapshot.session_epoch,
        snapshot.cycle_epoch,
        snapshot.lifecycle_state_epoch,
        snapshot.retirement_epoch,
        snapshot.request_handle);
    if (!exposure.exposed) return;
    g_pause_resume_bank_release_probe_epoch = exposure.next_ordinal;
    g_pause_resume_bank.pending_release_probe = exposure.identity;
    g_pause_resume_bank.pending_release_evidence = updated_backing;
    if (authority_probe) {
        authority_probe->eligible = true;
        authority_probe->identity = g_pause_resume_bank.pending_release_probe;
        authority_probe->evidence = updated_backing;
        authority_probe->session = g_pause_resume_bank;
        authority_probe->facts = facts;
        // The native replacement route can point at a different sound after
        // Stop. Release authority belongs to the exact detached sound whose
        // canonical owner was restored, not to the currently playing BGM
        // sound.
        authority_probe->current_sound = snapshot.detached.sound.object;
    }
}

PauseResumeCoordinatorResult coordinate_pause_resume_release_probe(
    const PauseResumeReleaseAuthorityProbe& probe,
    OnMemoryBankDiagnosticPair* diagnostic,
    OnMemoryBankReleaseAction* release,
    PauseResumeBankMarker* marker)
{
    if (!probe.eligible) return {};
    PlaybackSnapshot playback_before;
    PlaybackSnapshot playback_after;
    CleanupLease cleanup_before;
    CleanupLease cleanup_after;
    uint64_t registry_generation_before = 0;
    uint64_t registry_generation_after = 0;
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity route_lease;
    uint64_t lifecycle_epoch = 0;
    OnMemoryBankDetachedRecord lifecycle;
    bool route_released = false;
    UObjectIdentity sound_identity;
    bool sound_observed = false;
    uint64_t owner_token = 0;
    bool owner_observed = false;
    OnMemoryBankSoundIdentity observed_sound;
    const auto current_probe_facts_locked = [&]() {
        const auto& active = g_onmemory_bank_lifecycle.active();
        const bool detached_matches = active
            && active.ordinal == probe.session.detached.ordinal
            && active.sound == probe.session.detached.sound
            && active.canonical.encode() == probe.session.detached.canonical.encode()
            && active.custom.encode() == probe.session.detached.custom.encode()
            && active.request_handle == probe.session.detached.request_handle
            && active.backing_observed == probe.session.detached.backing_observed
            && active.backing_identity == probe.session.detached.backing_identity;
        return PauseResumeReleaseProbeCurrentFacts{
            pause_resume_release_probe_matches(
                probe.identity, g_pause_resume_bank.pending_release_probe),
            same_onmemory_bank_retired_backing_evidence(
                probe.evidence, g_pause_resume_bank.pending_release_evidence),
            same_onmemory_bank_retired_backing_evidence(
                probe.evidence, g_pause_resume_bank.retired_backing),
            same_pause_resume_retirement_monitor(
                probe.session.retirement_monitor,
                g_pause_resume_bank.retirement_monitor)
                && g_pause_resume_bank.retirement_epoch
                    == probe.identity.retirement_epoch
                && g_pause_resume_bank.request_handle
                    == probe.identity.request_identity,
            pause_resume_epoch_matches(
                probe.identity.session_epoch, probe.identity.cycle_epoch,
                g_pause_resume_bank.session_epoch,
                g_pause_resume_bank.cycle_epoch)
                && g_pause_resume_bank.phase == probe.identity.source_phase,
            g_onmemory_bank_lifecycle.state_epoch()
                == probe.identity.lifecycle_state_epoch,
            detached_matches,
        };
    };
    return coordinate_pause_resume_release_authority(
        [&]() {
            playback_before = registry().playback_snapshot();
            cleanup_before = registry().cleanup_lease();
            registry_generation_before = registry().selection_snapshot().generation;
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const auto& active = g_onmemory_bank_lifecycle.active();
            if (!pause_resume_release_probe_current(
                    current_probe_facts_locked())) {
                return false;
            }
            route_generation = g_audio_route_state.generation;
            route_lease = g_audio_route_state.lease_identity;
            lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
            lifecycle = g_onmemory_bank_lifecycle.active();
            route_released = g_audio_route_state.phase == AudioRoutePhase::Idle
                && !g_audio_route_state.custom_resource_owned
                && !g_audio_route_state.set_play_handoff_pending
                && !g_audio_route_state.list_cleanup_pending
                && g_audio_route_state.frozen_sound_patch.patches.empty()
                && g_pending_play_setup_patch.patches.empty()
                && g_active_patch_journal.empty()
                && g_failed_patch_journal.empty()
                && !g_unpublished_audio_setup
                && !g_frozen_profile_lease.active();
            return true;
        },
        [&]() {
            sound_observed = probe.current_sound
                && read_uobject_identity(probe.current_sound, sound_identity);
            owner_observed = sound_observed
                && core::safe_read_field(probe.current_sound,
                    runtime_layouts::SqexSeadSound::observed_field548,
                    owner_token);
            observed_sound = {probe.current_sound,
                sound_observed ? sound_identity.live : UObjectLiveHandle{}};
            playback_after = registry().playback_snapshot();
            cleanup_after = registry().cleanup_lease();
            registry_generation_after = registry().selection_snapshot().generation;
            return true;
        },
        [&]() -> PauseResumeCoordinatorResult {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const auto& active = g_onmemory_bank_lifecycle.active();
            if (!pause_resume_release_probe_current(
                    current_probe_facts_locked())) {
                return {};
            }
            const bool exact_lifecycle = active && lifecycle
                && active.ordinal == probe.session.detached.ordinal
                && active.ordinal == lifecycle.ordinal
                && active.sound == probe.session.detached.sound
                && active.canonical.encode() == probe.session.detached.canonical.encode()
                && active.custom.encode() == probe.session.detached.custom.encode()
                && active.request_handle == probe.session.detached.request_handle
                && active.backing_observed == probe.session.detached.backing_observed
                && active.backing_identity == probe.session.detached.backing_identity;
            const bool route_released_now = g_audio_route_state.phase
                    == AudioRoutePhase::Idle
                && !g_audio_route_state.custom_resource_owned
                && !g_audio_route_state.set_play_handoff_pending
                && !g_audio_route_state.list_cleanup_pending
                && g_audio_route_state.frozen_sound_patch.patches.empty()
                && g_pending_play_setup_patch.patches.empty()
                && g_active_patch_journal.empty()
                && g_failed_patch_journal.empty()
                && !g_unpublished_audio_setup
                && !g_frozen_profile_lease.active();
            OnMemoryBankRetirementFacts facts = probe.facts;
            facts.current_owner = classify_onmemory_bank_retirement_owner(
                probe.session.detached, observed_sound,
                sound_observed && observed_sound == probe.session.detached.sound,
                owner_observed, owner_token);
            const PauseResumeReleaseAuthorityFacts authority{
                pause_resume_epoch_matches(probe.session.session_epoch,
                    probe.session.cycle_epoch, g_pause_resume_bank.session_epoch,
                    g_pause_resume_bank.cycle_epoch),
                route_generation == g_audio_route_state.generation
                    && route_lease == g_audio_route_state.lease_identity,
                route_released && route_released_now,
                !playback_before.song && !playback_before.token.valid()
                    && !playback_after.song && !playback_after.token.valid(),
                !cleanup_before.song && !cleanup_before.token.valid()
                    && !cleanup_after.song && !cleanup_after.token.valid()
                    && registry_generation_before == registry_generation_after,
                exact_lifecycle,
                lifecycle_epoch == probe.session.lifecycle_state_epoch
                    && g_onmemory_bank_lifecycle.state_epoch()
                        == probe.session.lifecycle_state_epoch,
                facts.current_owner.category
                        == OnMemoryBankRetirementOwnerCategory::Canonical
                    || facts.current_owner.category
                        == OnMemoryBankRetirementOwnerCategory::Zero,
                probe.session.retirement_facts.cleanup_generation
                        == probe.session.detached.cleanup_generation
                    && facts.cleanup_generation
                        == probe.session.detached.cleanup_generation,
                !g_onmemory_bank_lifecycle.release_in_flight()
                    && !g_onmemory_bank_lifecycle.failed()
                    && g_audio_route_installed.load(std::memory_order_acquire)
                    && !g_audio_route_disabled.load(std::memory_order_acquire),
            };
            if (!pause_resume_release_authority_valid(authority)) return {};
            facts.exact_request_retired = true;
            facts.route_released = authority.route_released;
            facts.playback_released = authority.playback_released;
            facts.cleanup_released = authority.cleanup_released;
            facts.owner_restore_verified = authority.owner_canonical_or_zero;
            facts.runtime_installed =
                g_audio_route_installed.load(std::memory_order_acquire);
            facts.lookup_signature_valid =
                g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire);
            facts.release_signature_valid =
                g_onmemory_bank_release_available.load(std::memory_order_acquire);
            facts.shutdown_or_disabled = !facts.runtime_installed
                || g_audio_route_disabled.load(std::memory_order_acquire);
            const auto outcome = pause_resume_retirement_outcome(
                probe.session.phase, true, true, probe.session.exit_requested);
            g_pause_resume_bank.retirement_facts = facts;
            g_pause_resume_bank.retirement_monitor = {};
            g_pause_resume_bank.retired_backing = {};
            if (outcome.advance_cycle) {
                g_pause_resume_bank.pending_release_probe = {};
                g_pause_resume_bank.pending_release_evidence = {};
                g_pause_resume_bank.phase = outcome.phase;
                g_pause_resume_bank.cycle_epoch = next_pause_resume_bank_epoch(
                    g_pause_resume_bank.cycle_epoch);
                g_pause_resume_bank.retirement_mismatch_recorded = false;
                if (marker) *marker = {PauseResumeBankMarkerStatus::SuspendedReady,
                    "cycle_retired", probe.session.session_epoch,
                    g_pause_resume_bank.cycle_epoch, true};
                return {true, false, false};
            }
            if (!outcome.release_ready) return {};
            // Consume the exact frozen authority before mutating lifecycle
            // release state. The audio-state lock makes consume+claim atomic
            // with respect to any copied probe callback.
            g_pause_resume_bank.pending_release_probe = {};
            g_pause_resume_bank.pending_release_evidence = {};
            g_pause_resume_bank.retirement_monitor = {};
            g_pause_resume_bank.retired_backing = {};
            const auto scheduled = coordinate_onmemory_bank_retirement(
                g_onmemory_bank_lifecycle, facts, g_onmemory_bank_diagnostic_pair,
                facts.route_generation, facts.cleanup_generation);
            if (!scheduled.claimed) {
                g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
                if (marker && !marker->eligible) *marker = {
                    PauseResumeBankMarkerStatus::Failed, "release_claim",
                    probe.session.session_epoch, probe.session.cycle_epoch, true};
                return {true, false, true};
            }
            if (diagnostic) *diagnostic = scheduled.diagnostic;
            if (release) *release = scheduled.release;
            g_pause_resume_bank.phase = PauseResumeBankPhase::ReleaseRequested;
            if (marker) *marker = {PauseResumeBankMarkerStatus::ReleaseRequested,
                "exit_quiescent", probe.session.session_epoch,
                probe.session.cycle_epoch, true};
            return {true, true, false};
        },
        [&]() {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const bool same_pending = pause_resume_release_probe_matches(
                    probe.identity, g_pause_resume_bank.pending_release_probe)
                && same_onmemory_bank_retired_backing_evidence(
                    probe.evidence, g_pause_resume_bank.pending_release_evidence);
            if (same_pending) {
                g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
                g_pause_resume_bank.pending_release_probe = {};
                g_pause_resume_bank.pending_release_evidence = {};
                if (marker && !marker->eligible) *marker = {
                    PauseResumeBankMarkerStatus::Failed, "release_authority",
                    probe.session.session_epoch, probe.session.cycle_epoch, true};
            }
        });
}
