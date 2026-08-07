#include "game/audio_cleanup_policy.h"

#include <cstring>
#include <limits>

namespace ff7r::piano::game {

bool audio_journal_commit_ready(const AudioJournalCommitEvidence& evidence) noexcept
{
    return evidence.pending_journal_restored
        && evidence.failed_journal_restored
        && evidence.auxiliary_journals_restored
        && evidence.active_journal_empty
        && evidence.lease_active
        && evidence.lease_identity_matches;
}

AudioCleanupDecision evaluate_audio_cleanup(const AudioCleanupEvidence& evidence) noexcept
{
    const bool quiesced = evidence.operation == AudioCleanupOperation::ListReturn
        || (evidence.hooks_disabled && evidence.callbacks_drained);
    const bool journals_clear = evidence.active_journal_empty
        && evidence.pending_journal_empty
        && evidence.failed_journal_empty
        && evidence.auxiliary_journals_restored;
    if (!quiesced || !journals_clear || !evidence.immutable_identity_matches) {
        return AudioCleanupDecision::Retain;
    }
    if (evidence.native_clear_verified && evidence.route_state_unchanged) {
        return AudioCleanupDecision::CommitRelease;
    }
    if (!evidence.native_attempted && !evidence.ambiguous_route_state) {
        return AudioCleanupDecision::CommitVerifiedNoRoute;
    }
    return AudioCleanupDecision::Retain;
}

AudioPatchRestoreDecision evaluate_audio_patch_restore(
    uint64_t current, uint64_t original, uint64_t replacement,
    bool allow_native_changes) noexcept
{
    if (current == original) return AudioPatchRestoreDecision::AlreadyRestored;
    if (current == replacement) return AudioPatchRestoreDecision::RestoreOriginal;
    return allow_native_changes
        ? AudioPatchRestoreDecision::NativeOwned
        : AudioPatchRestoreDecision::Conflict;
}

bool rebase_audio_patch_original_after_exact_override(
    AudioPatchRestoreField& field,
    const AudioPatchRestoreExactOverride& exact) noexcept
{
    if (!exact.applied || !exact.enabled || !exact.fresh_native_owner_proof
        || !exact.lookup_signature_valid || !exact.release_signature_valid
        || !exact.canonical_kind2_qualified || !exact.custom_kind2_qualified
        || !field.object || !exact.object || field.object != exact.object
        || field.offset != exact.offset || field.size == 0
        || field.size != exact.size || !field.label || !exact.label
        || std::strcmp(field.label, exact.label) != 0
        || field.original != exact.expected_journal_original
        || (field.original != 0 && field.original != exact.restore_value)
        || field.replacement != 0 || exact.expected_current == 0
        || exact.restore_value == 0
        || exact.restore_value == exact.expected_current) {
        return false;
    }
    field.original = exact.restore_value;
    return true;
}

bool rebase_unique_audio_patch_original_after_exact_override(
    std::vector<AudioPatchRestoreField>& fields,
    const AudioPatchRestoreExactOverride& exact,
    uint32_t& matching_field_count) noexcept
{
    matching_field_count = 0;
    AudioPatchRestoreField* matching = nullptr;
    for (AudioPatchRestoreField& field : fields) {
        const bool exact_identity = field.object == exact.object
            && field.offset == exact.offset && field.size == exact.size
            && field.label && exact.label
            && std::strcmp(field.label, exact.label) == 0;
        if (!exact_identity) continue;
        if (matching_field_count != std::numeric_limits<uint32_t>::max()) {
            ++matching_field_count;
        }
        matching = &field;
    }
    return matching_field_count == 1 && matching
        && rebase_audio_patch_original_after_exact_override(*matching, exact);
}

bool audio_patch_original_rebase_route_successor_exact(
    const uint64_t frozen_route_generation,
    const uint64_t pre_restore_route_generation,
    const uint64_t live_route_generation) noexcept
{
    return pre_restore_route_generation != 0
        && pre_restore_route_generation != UINT64_MAX
        && frozen_route_generation == pre_restore_route_generation
        && live_route_generation == pre_restore_route_generation + 1;
}

bool audio_patch_original_rebase_post_restore_setup_exact(
    const AudioPatchOriginalRebasePostRestoreSetupFacts& facts) noexcept
{
    return facts.selection_song_exact && facts.selection_profile_valid
        && facts.desired_song_exact && facts.patched_song_cleared
        && facts.transient_sound_cleared
        && facts.pending_patch_journal_empty
        && facts.unpublished_setup_cleared;
}

bool audio_patch_original_rebase_context_exact(
    const AudioPatchOriginalRebaseContextFacts& facts) noexcept
{
    return facts.route_generation_exact && facts.lease_exact
        && facts.sound_pointer_exact && facts.sound_live_identity_exact
        && facts.controller_pointer_exact
        && facts.controller_live_identity_exact
        && facts.setup_exact && facts.frozen_snapshot_valid;
}

namespace {

struct AudioPatchRestorePlanEntry {
    const AudioPatchRestoreField* field = nullptr;
    uint64_t inspected_value = 0;
    uint64_t restore_value = 0;
    bool exact_override = false;
};

bool exact_override_matches(
    const AudioPatchRestoreExactOverride& exact,
    const AudioPatchRestoreField& field,
    const uint64_t current) noexcept
{
    return exact.enabled
        && exact.fresh_native_owner_proof
        && exact.lookup_signature_valid
        && exact.release_signature_valid
        && exact.canonical_kind2_qualified
        && exact.custom_kind2_qualified
        && exact.object == field.object
        && exact.offset == field.offset
        && exact.size == field.size
        && exact.label && field.label
        && std::strcmp(exact.label, field.label) == 0
        && exact.expected_journal_original == field.original
        && exact.expected_current == current
        && exact.restore_value != 0
        && exact.restore_value != current;
}

void copy_report_label(
    char (&destination)[kAudioPatchRestoreReportLabelCapacity],
    const char* source) noexcept
{
    const char* const bounded_source = source ? source : "?";
    size_t length = 0;
    while (length < kAudioPatchRestoreReportLabelCapacity - 1
        && bounded_source[length] != '\0') {
        ++length;
    }
    std::memcpy(destination, bounded_source, length);
    destination[length] = '\0';
}

void increment_bounded(uint32_t& value) noexcept
{
    if (value != std::numeric_limits<uint32_t>::max()) ++value;
}

AudioPatchRestoreFailureCategory restore_category(
    const AudioPatchRestoreDecision decision) noexcept
{
    switch (decision) {
    case AudioPatchRestoreDecision::AlreadyRestored:
        return AudioPatchRestoreFailureCategory::AlreadyRestored;
    case AudioPatchRestoreDecision::RestoreOriginal:
        return AudioPatchRestoreFailureCategory::RestoreOriginal;
    case AudioPatchRestoreDecision::NativeOwned:
        return AudioPatchRestoreFailureCategory::PreserveNativeValue;
    case AudioPatchRestoreDecision::Conflict:
        return AudioPatchRestoreFailureCategory::Conflict;
    }
    return AudioPatchRestoreFailureCategory::Unclassified;
}

void record_failure(
    AudioPatchRestoreFailureReport* report,
    const AudioPatchRestoreFailureStage stage,
    const AudioPatchRestoreFailureCategory category,
    const AudioPatchRestoreField& field,
    const bool current_read,
    const uint64_t current,
    const bool allow_native_changes,
    const size_t restore_plan_index) noexcept
{
    if (!report || report->failed()) return;
    report->stage = stage;
    report->category = category;
    copy_report_label(report->label, field.label);
    report->offset = field.offset;
    report->size = field.size;
    if (!field.redact_values_in_report) {
        report->current = current;
        report->original = field.original;
        report->replacement = field.replacement;
    }
    report->current_read = current_read;
    report->values_redacted = field.redact_values_in_report;
    report->allow_native_changes = allow_native_changes;
    report->restore_plan_index = restore_plan_index;
}

void record_rollback_failure(
    AudioPatchRestoreFailureReport* report,
    const AudioPatchRollbackFailureStage stage,
    const AudioPatchRestoreField& field,
    const bool current_read,
    const uint64_t current,
    const uint64_t inspected_value,
    const size_t restore_plan_index) noexcept
{
    if (!report || report->rollback_failed()) return;
    report->rollback_stage = stage;
    copy_report_label(report->rollback_label, field.label);
    report->rollback_offset = field.offset;
    report->rollback_size = field.size;
    if (!field.redact_values_in_report) {
        report->rollback_current = current;
        report->rollback_original = field.original;
        report->rollback_replacement = field.replacement;
        report->rollback_inspected_value = inspected_value;
    }
    report->rollback_current_read = current_read;
    report->rollback_values_redacted = field.redact_values_in_report;
    report->rollback_plan_index = restore_plan_index;
}

} // namespace

bool restore_audio_patches_reverse(
    const std::vector<AudioPatchRestoreField>& patches,
    const bool allow_native_changes,
    const AudioPatchRestoreAccess& access,
    AudioPatchRestoreFailureReport* report,
    AudioPatchRestoreExactOverride* exact_override)
{
    if (exact_override) exact_override->applied = false;
    if (report) {
        *report = {};
        report->allow_native_changes = allow_native_changes;
    }

    std::vector<AudioPatchRestorePlanEntry> restore_plan;
    restore_plan.reserve(patches.size());
    for (auto it = patches.rbegin(); it != patches.rend(); ++it) {
        uint64_t current = 0;
        if (!access.read || !access.read(*it, current, access.context)) {
            record_failure(report, AudioPatchRestoreFailureStage::FieldRead,
                AudioPatchRestoreFailureCategory::Unclassified, *it, false, 0,
                allow_native_changes, restore_plan.size());
            return false;
        }
        AudioPatchRestoreDecision decision = evaluate_audio_patch_restore(
            current, it->original, it->replacement, allow_native_changes);
        bool uses_exact_override = false;
        uint64_t restore_value = it->original;
        if (decision == AudioPatchRestoreDecision::NativeOwned
            && exact_override
            && exact_override_matches(*exact_override, *it, current)) {
            decision = AudioPatchRestoreDecision::RestoreOriginal;
            restore_value = exact_override->restore_value;
            uses_exact_override = true;
        }
        if (decision == AudioPatchRestoreDecision::AlreadyRestored) {
            if (report) increment_bounded(report->already_restored_count);
            continue;
        }
        if (decision != AudioPatchRestoreDecision::RestoreOriginal) {
            if (decision == AudioPatchRestoreDecision::Conflict) {
                record_failure(report,
                    AudioPatchRestoreFailureStage::ConflictDecision,
                    restore_category(decision), *it, true, current,
                    allow_native_changes, restore_plan.size());
                return false;
            }
            if (report) increment_bounded(report->preserve_native_value_count);
            if (access.preserve_native) {
                access.preserve_native(*it, current, access.context);
            }
            continue;
        }
        if (report) increment_bounded(report->restore_original_count);
        restore_plan.push_back({&*it, current, restore_value, uses_exact_override});
    }

    std::vector<size_t> restored;
    restored.reserve(restore_plan.size());
    for (size_t plan_index = 0; plan_index < restore_plan.size(); ++plan_index) {
        const AudioPatchRestorePlanEntry& entry = restore_plan[plan_index];
        const AudioPatchRestoreField& field = *entry.field;
        const bool wrote = access.write
            && access.write(field, entry.restore_value, access.context);
        uint64_t verify = 0;
        bool verify_read = false;
        bool verified = false;
        if (wrote) {
            verify_read = access.read
                && access.read(field, verify, access.context);
            verified = verify_read && verify == entry.restore_value;
        }
        if (wrote && verified) {
            restored.push_back(plan_index);
            continue;
        }

        record_failure(report,
            wrote ? AudioPatchRestoreFailureStage::PostWriteVerify
                  : AudioPatchRestoreFailureStage::RestoreWrite,
            AudioPatchRestoreFailureCategory::RestoreOriginal,
            field, wrote ? verify_read : true,
            wrote ? verify : entry.inspected_value,
            allow_native_changes, plan_index);

        // DURABLE: every write that may have changed memory is rolled back to
        // the value inspected before this transaction, not journal replacement.
        // The uncertain current write is handled first; all earlier writes are
        // then attempted in reverse even when a rollback step fails.
        const auto rollback_entry = [&](
            const AudioPatchRestorePlanEntry& rollback_plan_entry,
            const size_t rollback_plan_index) noexcept {
            const AudioPatchRestoreField& rollback_field =
                *rollback_plan_entry.field;
            const bool rollback_wrote = access.write
                && access.write(
                    rollback_field, rollback_plan_entry.inspected_value,
                    access.context);
            uint64_t rollback_verify = 0;
            const bool rollback_read = access.read
                && access.read(rollback_field, rollback_verify, access.context);
            if (!rollback_wrote) {
                record_rollback_failure(report,
                    AudioPatchRollbackFailureStage::RestoreWrite,
                    rollback_field, rollback_read, rollback_verify,
                    rollback_plan_entry.inspected_value, rollback_plan_index);
            } else if (!rollback_read
                || rollback_verify != rollback_plan_entry.inspected_value) {
                record_rollback_failure(report,
                    AudioPatchRollbackFailureStage::PostWriteVerify,
                    rollback_field, rollback_read, rollback_verify,
                    rollback_plan_entry.inspected_value, rollback_plan_index);
            }
        };
        rollback_entry(entry, plan_index);
        for (auto rollback = restored.rbegin(); rollback != restored.rend(); ++rollback) {
            rollback_entry(restore_plan[*rollback], *rollback);
        }
        return false;
    }
    if (exact_override) {
        for (const AudioPatchRestorePlanEntry& entry : restore_plan) {
            if (entry.exact_override) {
                exact_override->applied = true;
                break;
            }
        }
    }
    return true;
}

AudioNativeHandoffDecision evaluate_audio_native_handoff(
    const AudioNativeRouteObservation& owned,
    const AudioNativeRouteObservation& current,
    void* requested_sound,
    uint64_t expected_generation) noexcept
{
    const bool generation_matches = expected_generation == 0
        || (owned.route_generation != 0
            && owned.route_generation + 1 == expected_generation
            && current.route_generation == expected_generation
            && owned.lease_identity.valid()
            && current.lease_identity == owned.lease_identity);
    if (!owned.valid || !current.valid || !requested_sound
        || !owned.slot || !owned.bgm || !owned.sound
        || !AudioBgmRequestHandle{owned.request_handle}.valid_bgm_request()
        || !generation_matches
        || current.slot != owned.slot
        || current.bgm != owned.bgm
        || (current.state != 2 && current.state != 4)) {
        return AudioNativeHandoffDecision::AmbiguousRetainAndBlock;
    }
    if (current.sound == owned.sound
        && current.request_handle == owned.request_handle
        && current.state == 4) {
        return AudioNativeHandoffDecision::OwnedCustomRoute;
    }
    if (current.sound != requested_sound || current.sound == owned.sound
        || !AudioBgmRequestHandle{current.request_handle}.valid_bgm_request()) {
        return AudioNativeHandoffDecision::AmbiguousRetainAndBlock;
    }
    return current.request_handle == owned.request_handle
        ? AudioNativeHandoffDecision::ReleaseBeforeNativePlay
        : AudioNativeHandoffDecision::NativeReplacementProven;
}

bool native_route_replacement_proven(
    const AudioNativeRouteObservation& owned,
    const AudioNativeRouteObservation& current,
    void* requested_sound,
    uint64_t expected_generation) noexcept
{
    return evaluate_audio_native_handoff(
        owned, current, requested_sound, expected_generation)
        == AudioNativeHandoffDecision::NativeReplacementProven;
}

bool native_route_release_proven(
    const AudioNativeRouteObservation& owned,
    const AudioNativeRouteObservation& cleared,
    uint64_t expected_generation) noexcept
{
    return expected_generation != 0
        && owned.valid && cleared.valid
        && owned.slot && owned.bgm && owned.sound
        && AudioBgmRequestHandle{owned.request_handle}.valid_bgm_request()
        && owned.lease_identity.valid()
        && (owned.route_generation == expected_generation
            || owned.route_generation + 1 == expected_generation)
        && cleared.lease_identity == owned.lease_identity
        && cleared.route_generation == expected_generation
        && cleared.slot == owned.slot
        && cleared.bgm == owned.bgm
        && cleared.sound == nullptr
        && cleared.request_handle == 0
        && cleared.state == 0;
}

bool begin_deferred_native_handoff(
    AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& owned,
    void* controller,
    void* requested_sound,
    uint64_t expected_generation) noexcept
{
    handoff = {};
    if (!owned.valid || !owned.lease_identity.valid() || !controller || !requested_sound
        || !owned.slot || !owned.bgm || !owned.sound
        || !AudioBgmRequestHandle{owned.request_handle}.valid_bgm_request()
        || owned.state != 4 || owned.route_generation + 1 != expected_generation) {
        return false;
    }
    handoff.phase = AudioDeferredNativeHandoffPhase::SetCaptured;
    handoff.lease_identity = owned.lease_identity;
    handoff.route_generation = expected_generation;
    handoff.controller = controller;
    handoff.slot = owned.slot;
    handoff.bgm = owned.bgm;
    handoff.requested_sound = requested_sound;
    handoff.retired_custom_request_handle = owned.request_handle;
    return true;
}

bool record_deferred_native_clear(
    AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& owned,
    const AudioNativeRouteObservation& cleared) noexcept
{
    if (handoff.phase != AudioDeferredNativeHandoffPhase::SetCaptured
        || !native_route_release_proven(owned, cleared, handoff.route_generation)) {
        handoff.phase = AudioDeferredNativeHandoffPhase::RetainedFailure;
        return false;
    }
    handoff.phase = AudioDeferredNativeHandoffPhase::CustomCleared;
    return true;
}

bool record_deferred_native_set(
    AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& current) noexcept
{
    if (handoff.phase != AudioDeferredNativeHandoffPhase::CustomCleared
        || !current.valid
        || current.route_generation != handoff.route_generation
        || !(current.lease_identity == handoff.lease_identity)
        || current.slot != handoff.slot
        || current.bgm != handoff.bgm
        || current.sound != handoff.requested_sound
        || !AudioBgmRequestHandle{current.request_handle}.valid_bgm_request()
        || (current.state != 2 && current.state != 4)) {
        return false;
    }
    handoff.native_request_handle = current.request_handle;
    handoff.phase = AudioDeferredNativeHandoffPhase::NativeSetApplied;
    return true;
}

bool deferred_native_play_ready(
    const AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& current) noexcept
{
    return handoff.phase == AudioDeferredNativeHandoffPhase::NativeSetApplied
        && current.valid
        && current.route_generation == handoff.route_generation
        && current.lease_identity == handoff.lease_identity
        && current.slot == handoff.slot
        && current.bgm == handoff.bgm
        && current.sound == handoff.requested_sound
        && current.request_handle == handoff.native_request_handle
        && AudioBgmRequestHandle{current.request_handle}.valid_bgm_request()
        && (current.state == 2 || current.state == 4);
}

AudioDeferredNativeHandoffAction evaluate_deferred_native_handoff_action(
    const AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& current) noexcept
{
    if (!current.valid
        || current.route_generation != handoff.route_generation
        || !(current.lease_identity == handoff.lease_identity)
        || current.slot != handoff.slot
        || current.bgm != handoff.bgm) {
        return AudioDeferredNativeHandoffAction::Retain;
    }
    if (handoff.phase == AudioDeferredNativeHandoffPhase::CustomCleared
        && current.sound == nullptr
        && current.request_handle == 0
        && current.state == 0) {
        return AudioDeferredNativeHandoffAction::ApplyNativeSet;
    }
    if (deferred_native_play_ready(handoff, current)) {
        return AudioDeferredNativeHandoffAction::ForwardNativePlay;
    }
    if (handoff.phase == AudioDeferredNativeHandoffPhase::RetainedFailure
        && current.sound == handoff.requested_sound
        && AudioBgmRequestHandle{current.request_handle}.valid_bgm_request()) {
        return AudioDeferredNativeHandoffAction::ForwardNativePlayRetainingFailure;
    }
    return AudioDeferredNativeHandoffAction::Retain;
}

bool bypass_audio_native_detour(uint32_t native_replay_depth) noexcept
{
    return native_replay_depth != 0;
}

AudioNaturalCompletionRetirementFailure
classify_audio_natural_completion_retirement(
    const AudioNaturalCompletionRetirementFacts& facts) noexcept
{
    using Failure = AudioNaturalCompletionRetirementFailure;
    if (!facts.route_custom_owned) return Failure::RouteNotCustomOwned;
    if (!facts.route_lease_valid) return Failure::RouteLeaseInvalid;
    if (!facts.route_controller_exact) return Failure::RouteControllerMismatch;
    if (!facts.pre_stop_chain_read) return Failure::PreStopChainUnreadable;
    if (!facts.pre_stop_sound_null) return Failure::PreStopSoundNotNull;
    if (!facts.pre_stop_request_zero) return Failure::PreStopRequestNotZero;
    if (!facts.pre_stop_state_idle) return Failure::PreStopStateNotIdle;
    if (!facts.detached_present) return Failure::DetachedMissing;
    if (!facts.detached_restore_applied) return Failure::DetachedPhaseNotRestoreApplied;
    if (!facts.detached_route_predecessor) return Failure::DetachedRouteNotPredecessor;
    if (!facts.detached_cleanup_generation_exact) {
        return Failure::DetachedCleanupGenerationMismatch;
    }
    if (!facts.detached_sound_exact) return Failure::DetachedSoundMismatch;
    if (!facts.detached_request_available) return Failure::DetachedRequestUnavailable;
    if (!facts.detached_request_exact) return Failure::DetachedRequestMismatch;
    if (!facts.detached_tokens_valid) return Failure::DetachedTokensInvalid;
    if (!facts.detached_owner_restored) return Failure::DetachedOwnerNotRestored;
    if (!facts.frozen_patch_valid) return Failure::FrozenPatchInvalid;
    if (!facts.frozen_route_predecessor) return Failure::FrozenRouteNotPredecessor;
    if (!facts.frozen_lease_exact) return Failure::FrozenLeaseMismatch;
    if (!facts.frozen_sound_exact) return Failure::FrozenSoundMismatch;
    if (!facts.frozen_owner_patch_exact) return Failure::FrozenOwnerPatchMismatch;
    if (!facts.journals_restored) return Failure::JournalsNotRestored;
    if (!facts.pause_resume_clear) return Failure::PauseResumeConflict;
    if (!facts.setup_clear) return Failure::SetupConflict;
    if (!facts.registry_playback_exact) return Failure::RegistryPlaybackMismatch;
    if (!facts.registry_cleanup_clear_or_exact) return Failure::RegistryCleanupConflict;
    if (!facts.aggregate_snapshot_available) return Failure::AggregateSnapshotUnavailable;
    if (facts.active_aggregate_borrower) return Failure::ActiveAggregateBorrower;
    if (!facts.canonical_proof_snapshot_available) {
        return Failure::CanonicalProofSnapshotUnavailable;
    }
    if (facts.canonical_proof_active) return Failure::CanonicalProofActive;
    if (facts.existing_retirement_phase != AudioStopRetirementPhase::None) {
        return Failure::ExistingRetirement;
    }
    return Failure::None;
}

AudioNaturalCompletionRetirementStart
start_audio_natural_completion_retirement(
    const AudioNaturalCompletionRetirementFacts& facts,
    const AudioStopRetirementObservation& observation,
    const OnMemoryBankRetiredBackingEvidence& backing_evidence) noexcept
{
    AudioNaturalCompletionRetirementStart result;
    result.failure = classify_audio_natural_completion_retirement(facts);
    if (result.failure != AudioNaturalCompletionRetirementFailure::None) {
        return result;
    }
    if (!observation.valid
        || !onmemory_bank_retired_backing_satisfies(
            backing_evidence,
            OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion)
        || backing_evidence.route_generation != observation.route_generation
        || backing_evidence.cleanup_generation
            != observation.lease_identity.generation
        || backing_evidence.request_handle != observation.request_handle) {
        result.failure = AudioNaturalCompletionRetirementFailure::InvalidObservation;
        return result;
    }
    result.monitor_observation = observation;
    result.presence_pending = onmemory_bank_retirement_presence_pending(
        backing_evidence, observation.valid, observation.request_handle_retired,
        OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion);
    if (observation.route_generation == 0
        || !observation.lease_identity.valid()
        || !observation.controller || !observation.slot || !observation.bgm
        || !observation.custom_sound
        || !AudioBgmRequestHandle{observation.request_handle}.valid_bgm_request()
        || result.presence_pending) {
        result.failure =
            AudioNaturalCompletionRetirementFailure::MonitorStartFailed;
    } else {
        result.admitted = true;
        result.monitor.phase = AudioStopRetirementPhase::Waiting;
        result.monitor.route_generation = observation.route_generation;
        result.monitor.lease_identity = observation.lease_identity;
        result.monitor.controller = observation.controller;
        result.monitor.slot = observation.slot;
        result.monitor.bgm = observation.bgm;
        result.monitor.custom_sound = observation.custom_sound;
        result.monitor.request_handle = observation.request_handle;
        result.monitor.origin_ =
            AudioStopRetirementOrigin::ExactNaturalCompletion;
    }
    return result;
}

AudioNaturalCompletionRetirementFailure
classify_audio_natural_completion_retirement_commit(
    const AudioNaturalCompletionRetirementFacts& facts) noexcept
{
    const auto qualification =
        classify_audio_natural_completion_retirement(facts);
    if (qualification != AudioNaturalCompletionRetirementFailure::None) {
        return qualification;
    }
    if (!facts.route_commit_exact) {
        return AudioNaturalCompletionRetirementFailure::RouteCommitMismatch;
    }
    if (!facts.detached_commit_exact) {
        return AudioNaturalCompletionRetirementFailure::DetachedCommitMismatch;
    }
    if (!facts.frozen_commit_exact) {
        return AudioNaturalCompletionRetirementFailure::FrozenCommitMismatch;
    }
    if (!facts.ownership_commit_exact) {
        return AudioNaturalCompletionRetirementFailure::OwnershipCommitMismatch;
    }
    if (!facts.registry_commit_exact) {
        return AudioNaturalCompletionRetirementFailure::RegistryCommitMismatch;
    }
    return AudioNaturalCompletionRetirementFailure::None;
}

bool begin_audio_stop_retirement_monitor(
    AudioStopRetirementState& state,
    const AudioStopRetirementObservation& observation) noexcept
{
    state = {};
    if (!observation.valid
        || observation.route_generation == 0
        || !observation.lease_identity.valid()
        || !observation.controller
        || !observation.slot
        || !observation.bgm
        || !observation.custom_sound
        || !AudioBgmRequestHandle{observation.request_handle}.valid_bgm_request()
        || !observation.request_handle_retired) {
        return false;
    }
    state.phase = AudioStopRetirementPhase::Waiting;
    state.route_generation = observation.route_generation;
    state.lease_identity = observation.lease_identity;
    state.controller = observation.controller;
    state.slot = observation.slot;
    state.bgm = observation.bgm;
    state.custom_sound = observation.custom_sound;
    state.request_handle = observation.request_handle;
    state.origin_ = AudioStopRetirementOrigin::OrdinaryStop;
    return true;
}

bool audio_retirement_frozen_generation_proven(
    const uint64_t frozen_route_generation,
    const AudioStopRetirementState& retirement,
    const uint64_t live_route_generation) noexcept
{
    if (frozen_route_generation == 0
        || retirement.route_generation == 0
        || retirement.phase != AudioStopRetirementPhase::Quiescent
        || live_route_generation != retirement.route_generation + 1) {
        return false;
    }
    switch (retirement.origin()) {
    case AudioStopRetirementOrigin::OrdinaryStop:
        return frozen_route_generation + 1 == retirement.route_generation;
    case AudioStopRetirementOrigin::ExactNaturalCompletion:
        return frozen_route_generation + 1 == retirement.route_generation;
    case AudioStopRetirementOrigin::None:
        return false;
    }
    return false;
}

AudioRetirementCleanupFailure first_audio_retirement_cleanup_failure(
    const AudioRetirementCleanupFailureFacts& facts) noexcept
{
    using Failure = AudioRetirementCleanupFailure;
    if (!facts.origin_valid) return Failure::InvalidOrigin;
    if (!facts.retirement_quiescent) return Failure::RetirementNotQuiescent;
    if (!facts.live_route_successor) return Failure::LiveRouteNotSuccessor;
    if (!facts.immutable_identity_matches) return Failure::ImmutableIdentityMismatch;
    if (!facts.backing_proven) return Failure::BackingNotProven;
    if (!facts.journals_restored) return Failure::JournalsNotRestored;
    if (!facts.frozen_fields_restored) return Failure::FrozenFieldsNotRestored;
    if (!facts.native_route_proven) return Failure::NativeRouteNotProven;
    if (!facts.cleanup_policy_accepted) return Failure::CleanupPolicyRejected;
    return Failure::None;
}

AudioStopRetirementPhase advance_audio_stop_retirement_monitor(
    AudioStopRetirementState& state,
    const AudioStopRetirementObservation& observation,
    uint32_t max_polls) noexcept
{
    if (state.phase != AudioStopRetirementPhase::Waiting) {
        return state.phase;
    }
    if (!observation.valid
        || observation.route_generation != state.route_generation
        || !(observation.lease_identity == state.lease_identity)
        || observation.controller != state.controller
        || observation.slot != state.slot
        || observation.bgm != state.bgm
        || observation.custom_sound != state.custom_sound
        || observation.request_handle != state.request_handle) {
        state.phase = AudioStopRetirementPhase::Invalid;
        return state.phase;
    }
    if (!observation.request_handle_retired) {
        state.phase = AudioStopRetirementPhase::Quiescent;
        return state.phase;
    }
    ++state.poll_count;
    if (max_polls != 0 && state.poll_count >= max_polls) {
        state.phase = AudioStopRetirementPhase::TimedOut;
    }
    return state.phase;
}

bool audio_retirement_route_authority_proven(
    const AudioRetirementRouteAuthorityFacts& facts) noexcept
{
    if (facts.monitor_route_generation == 0
        || facts.live_route_generation == 0
        || facts.frozen_route_generation == 0) {
        return false;
    }
    switch (facts.authority) {
    case AudioRetirementRouteAuthority::DirectSuccessor:
        return facts.monitor_route_generation != UINT64_MAX
            && facts.live_route_generation == facts.monitor_route_generation + 1;
    case AudioRetirementRouteAuthority::TerminalAggregateHandoff:
        return facts.terminal_handoff_authenticated
            && facts.terminal_leaf_route_generation != 0
            && AudioBgmRequestHandle{facts.monitor_request_handle}
                .valid_bgm_request()
            && AudioBgmRequestHandle{facts.terminal_leaf_request_handle}
                .valid_bgm_request()
            && facts.terminal_leaf_request_handle != facts.monitor_request_handle
            && facts.live_route_generation == facts.terminal_leaf_route_generation
            && facts.frozen_route_generation != UINT64_MAX
            && facts.frozen_route_generation + 1 == facts.monitor_route_generation;
    }
    return false;
}

bool audio_retirement_owned_request_identity_exact(
    const uint64_t retirement_request_handle,
    const uint64_t route_owned_request_handle) noexcept
{
    return AudioBgmRequestHandle{retirement_request_handle}.valid_bgm_request()
        && route_owned_request_handle == retirement_request_handle;
}

bool audio_retirement_owned_route_identity_exact(
    const AudioRetirementOwnedRouteIdentityFacts& facts) noexcept
{
    return facts.expected_slot != nullptr
        && facts.expected_bgm != nullptr
        && facts.expected_sound != nullptr
        && facts.owned_slot == facts.expected_slot
        && facts.owned_bgm == facts.expected_bgm
        && facts.owned_sound == facts.expected_sound
        && audio_retirement_owned_request_identity_exact(
            facts.expected_request, facts.owned_request);
}

AudioRetirementOnMemoryRequestProjection
project_audio_retirement_onmemory_request(
    const AudioRetirementOnMemoryRequestProjectionFacts& facts) noexcept
{
    AudioRetirementOnMemoryRequestProjection result;
    if (!audio_retirement_owned_request_identity_exact(
            facts.retirement_request_handle,
            facts.route_owned_request_handle)
        || !audio_retirement_route_authority_proven(facts.route_authority)
        || facts.route_authority.monitor_request_handle
            != facts.retirement_request_handle) {
        return result;
    }
    result.accepted = true;
    result.retired_request_handle = facts.retirement_request_handle;
    return result;
}

bool audio_retirement_cleanup_proven(
    const AudioRetirementCleanupEvidence& evidence) noexcept
{
    const AudioStopRetirementState& retirement = evidence.retirement;
    const AudioStopRetirementObservation& observed = evidence.retirement_observation;
    const AudioNativeRouteObservation& current = evidence.current_route;
    AudioRetirementRouteAuthorityFacts route_authority = evidence.route_authority;
    if (route_authority.authority
            == AudioRetirementRouteAuthority::DirectSuccessor
        && route_authority.monitor_route_generation == 0
        && route_authority.live_route_generation == 0) {
        route_authority.monitor_route_generation = retirement.route_generation;
        route_authority.live_route_generation = current.route_generation;
        route_authority.frozen_route_generation = retirement.route_generation;
    }
    if ((retirement.origin() != AudioStopRetirementOrigin::OrdinaryStop
            && retirement.origin()
                != AudioStopRetirementOrigin::ExactNaturalCompletion)
        || retirement.phase != AudioStopRetirementPhase::Quiescent
        || !retirement.lease_identity.valid()
        || !retirement.controller || !retirement.slot || !retirement.bgm
        || !retirement.custom_sound
        || !AudioBgmRequestHandle{retirement.request_handle}.valid_bgm_request()
        || !observed.valid || observed.request_handle_retired
        || observed.route_generation != retirement.route_generation
        || !(observed.lease_identity == retirement.lease_identity)
        || observed.controller != retirement.controller
        || observed.slot != retirement.slot
        || observed.bgm != retirement.bgm
        || observed.custom_sound != retirement.custom_sound
        || observed.request_handle != retirement.request_handle
        || observed.current_sound != current.sound
        || observed.current_request_handle != current.request_handle
        || observed.current_state != current.state
        || !current.valid
        || !audio_retirement_route_authority_proven(route_authority)
        || !(current.lease_identity == retirement.lease_identity)
        || current.slot != retirement.slot
        || current.bgm != retirement.bgm
        || !evidence.immutable_identity_matches
        || !evidence.all_journals_restored
        || !evidence.custom_sound_fields_restored) {
        return false;
    }

    const bool empty_route = current.sound == nullptr
        && current.request_handle == 0
        && current.state == 0;
    const AudioBgmRequestHandle old_handle{retirement.request_handle};
    const AudioBgmRequestHandle new_handle{current.request_handle};
    const bool newer_native_route = evidence.current_native_identity_matches
        && current.sound
        && current.sound != retirement.custom_sound
        && new_handle.valid_bgm_request()
        && new_handle.value != old_handle.value
        && new_handle.generation() > old_handle.generation()
        && (current.state == 2 || current.state == 4);
    return empty_route || newer_native_route;
}

bool record_deferred_native_play(
    AudioDeferredNativeHandoffState& handoff,
    const AudioNativeRouteObservation& current) noexcept
{
    if (!deferred_native_play_ready(handoff, current) || current.state != 4) {
        return false;
    }
    handoff.phase = AudioDeferredNativeHandoffPhase::NativePlayForwarded;
    return true;
}

bool AudioPatchJournalState::begin(
    FrozenProfileLeaseState& lease, AudioRouteLeaseIdentity identity) noexcept
{
    if (!lease.mark_native_arm_attempt(identity)) return false;
    ++active_operations_;
    return true;
}

void AudioPatchJournalState::finish() noexcept
{
    if (active_operations_ > 0) --active_operations_;
}

AudioRouteCleanupResult apply_audio_cleanup(
    FrozenProfileLeaseState& lease,
    AudioRouteLeaseIdentity identity,
    const AudioCleanupEvidence& evidence) noexcept
{
    switch (evaluate_audio_cleanup(evidence)) {
    case AudioCleanupDecision::CommitVerifiedNoRoute:
        return lease.transition(AudioRouteCleanupEvent::VerifiedNoRoute, identity);
    case AudioCleanupDecision::CommitRelease:
        return lease.transition(
            evidence.operation == AudioCleanupOperation::Shutdown
                ? AudioRouteCleanupEvent::ShutdownReleaseVerified
                : AudioRouteCleanupEvent::NativeReleaseCommitted,
            identity);
    case AudioCleanupDecision::Retain:
        return lease.transition(AudioRouteCleanupEvent::NativeClearUnverified, identity);
    }
    return lease.transition(AudioRouteCleanupEvent::NativeClearUnverified, identity);
}

} // namespace ff7r::piano::game
