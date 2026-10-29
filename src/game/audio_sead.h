#pragma once

#include "game/audio_sidecar_runtime.h"
#include "game/song_registry.h"

#include <memory>

#include "game/chart_patch.h"
#include "game/frozen_profile_lifecycle.h"
#include "game/selection_audio_policy.h"

#include <cstdint>
#include <string>

namespace ff7r::piano::game {

class AudioProductionCatalogScope;
class PreparedAudioCatalogCommit;
using PreparedAudioPrefix = audio_sead_detail::PreparedAudioPrefix;
using ProgressiveAudioCatalogBuilder = audio_sead_detail::ProgressiveAudioCatalogBuilder;

class PreparedAudioCatalog final {
public:
    std::shared_ptr<const SongRegistryStorage> storage;
    std::shared_ptr<const PreparedAudioPrefix> prefix;
};
bool prepare_audio_catalog_from_prefix(
    std::shared_ptr<const SongRegistryStorage> storage,
    std::shared_ptr<const PreparedAudioPrefix> prefix,
    PreparedAudioCatalog& prepared) noexcept;
using AudioCatalogReadiness = audio_sead_detail::AudioCatalogReadiness;
AudioCatalogReadiness prepared_audio_catalog_readiness(const PreparedAudioCatalog&) noexcept;
// Checks the adopted immutable cohort and its fresh native identity, not merely
// symbol presence. Used before reservation and before each selected-name return.
bool selected_native_audio_name(const SelectionSnapshot&, std::uint64_t&) noexcept;
std::shared_ptr<PreparedAudioCatalogCommit> begin_prepared_audio_catalog_commit(
    PreparedAudioCatalog& prepared, uint64_t expected_catalog_revision,
    const AudioProductionCatalogScope& callback_scope) noexcept;
void commit_prepared_audio_catalog(PreparedAudioCatalogCommit& prepared,
    uint64_t generation, uint64_t catalog_revision) noexcept;
void finalize_prepared_audio_catalog_commit(
    PreparedAudioCatalogCommit& prepared) noexcept;

struct MenuSessionSnapshot;

AudioRouteArmLease arm_audio_route_for_song_id(const std::string& song_id);
struct SelectionActivationListContext final {
    void* object = nullptr;
    int32_t internal_index = 0;
    int32_t serial_number = 0;

    explicit operator bool() const noexcept
    {
        return object != nullptr && internal_index > 0 && serial_number > 0;
    }
};

enum class SelectionActivationHandoffResult : uint8_t {
    Prepared,
    Confirmed,
    NoPendingReservation,
    AlreadyConfirmed,
    ReservationGenerationDrift,
    ContextDrift,
    PriorSelectionDrift,
    GenerationWrap,
    SameGeneration,
    SkippedGeneration,
    StorageDrift,
    SongDrift,
    ProfileDrift,
    VisibleIndexDrift,
    BaseSlotDrift,
};

enum class SelectionActivationNotificationResult : uint8_t {
    ConfirmedSameSelection,
    NoPendingReservation,
    DuplicateNotification,
    ContextDrift,
    SelectionDrift,
};

struct SelectionActivationHandoff final {
    uint64_t reservation_generation = 0;
    uint64_t prior_selection_generation = 0;
    SelectionActivationHandoffResult result
        = SelectionActivationHandoffResult::NoPendingReservation;

    explicit operator bool() const noexcept
    {
        return reservation_generation != 0
            && result == SelectionActivationHandoffResult::Prepared;
    }
};

enum class SelectionActivationReservationFailure {
    None, NativeSelection, ListContextSnapshot, ListContext, ActiveAdmission, MenuNotReady,
    MenuContext, MenuGeneration, NativeListBinding, ReservationDrift, ReservationState
};
struct SelectionActivationReservationDiagnostic {
    SelectionActivationReservationFailure failure=SelectionActivationReservationFailure::ListContextSnapshot;
    std::uint64_t selection_generation=0,menu_generation=0,reservation_generation=0;
    SelectionActivationReservationState state=SelectionActivationReservationState::Empty;
};
const char* selection_activation_reservation_failure_name(SelectionActivationReservationFailure) noexcept;
bool reserve_selection_audio_activation(
    const SelectionSnapshot& selection,
    const SelectionActivationListContext& list_context,
    SelectionActivationReservationDiagnostic* diagnostic=nullptr) noexcept;
SelectionActivationHandoff prepare_selection_audio_activation_handoff(
    const SelectionSnapshot& selection,
    const SelectionActivationListContext& list_context) noexcept;
SelectionActivationHandoffResult confirm_selection_audio_activation_handoff(
    const SelectionActivationHandoff& handoff,
    const SelectionSnapshot& published_selection,
    const SelectionActivationListContext& list_context) noexcept;
SelectionActivationHandoffResult publish_selection_audio_activation_handoff(
    const SelectionActivationHandoff& handoff,
    const SelectionActivationListContext& list_context,
    int32_t visible_index, int32_t base_slot, bool alias_state_exact,
    SelectionSnapshot& published_selection) noexcept;
SelectionActivationNotificationResult
observe_selection_audio_activation_index_notification(
    const SelectionSnapshot& selection,
    const SelectionActivationListContext& list_context,
    int32_t visible_index, int32_t base_slot) noexcept;
bool revoke_selection_audio_activation_if_pending() noexcept;
void revoke_selection_audio_activation() noexcept;
bool revoke_selection_audio_activation_for_menu_session(
    const SelectionSnapshot& expected_selection, uint64_t menu_session_generation) noexcept;
struct SelectionActivationTerminalResult final {
    SelectionActivationTerminalDecision decision
        = SelectionActivationTerminalDecision::Rejected;
    SelectionActivationTerminalFailure first_failure
        = SelectionActivationTerminalFailure::ReservationState;
    SelectionActivationReservationState reservation_state
        = SelectionActivationReservationState::Empty;
    SelectionActivationTransferState transfer_before
        = SelectionActivationTransferState::Pending;
    SelectionActivationTransferState transfer_after
        = SelectionActivationTransferState::Pending;
    uint64_t reservation_generation = 0;
    bool handoff_confirmed = false;
    bool identity_exact = false;
};
SelectionActivationTerminalResult terminate_selection_audio_activation_for_menu_session(
    const MenuSessionSnapshot& session, const SelectionSnapshot& expected_selection,
    SelectionActivationTerminalReason reason) noexcept;
bool reclaim_selection_audio_activation_for_controller(
    void* controller, SelectionActivationTerminalReason reason) noexcept;
uint64_t selection_audio_activation_revocation_epoch() noexcept;

class SelectionAudioAdmission;
class SelectionActivationClaim final {
public:
    struct Impl;

    SelectionActivationClaim() noexcept;
    SelectionActivationClaim(SelectionActivationClaim&&) noexcept;
    SelectionActivationClaim& operator=(SelectionActivationClaim&&) noexcept;
    SelectionActivationClaim(const SelectionActivationClaim&) = delete;
    SelectionActivationClaim& operator=(const SelectionActivationClaim&) = delete;
    ~SelectionActivationClaim() noexcept;

    explicit operator bool() const noexcept;

private:
    std::unique_ptr<Impl> impl_;

    friend SelectionActivationClaim claim_selection_audio_activation(
        void* wrapper, uintptr_t caller_rva) noexcept;
    friend SelectionSnapshot selection_audio_activation_claim_selection(
        const SelectionActivationClaim& claim) noexcept;
    friend SelectionAudioAdmission begin_selection_audio_admission(
        SelectionActivationClaim claim, bool chart_plan_complete) noexcept;
};

SelectionActivationClaim claim_selection_audio_activation(
    void* wrapper, uintptr_t caller_rva) noexcept;
SelectionSnapshot selection_audio_activation_claim_selection(
    const SelectionActivationClaim& claim) noexcept;

struct SelectionAudioAdmissionAuthority;
class SelectionAudioAdmission final {
public:
    struct Impl;

    SelectionAudioAdmission() noexcept;
    SelectionAudioAdmission(SelectionAudioAdmission&&) noexcept;
    SelectionAudioAdmission& operator=(SelectionAudioAdmission&&) noexcept;
    SelectionAudioAdmission(const SelectionAudioAdmission&) = delete;
    SelectionAudioAdmission& operator=(const SelectionAudioAdmission&) = delete;
    ~SelectionAudioAdmission() noexcept;

    explicit operator bool() const noexcept;

private:
    std::unique_ptr<Impl> impl_;

    friend SelectionAudioAdmission begin_selection_audio_admission(
        SelectionActivationClaim claim, bool chart_plan_complete) noexcept;
    friend AudioRouteArmLease commit_selection_audio_admission(
        SelectionAudioAdmission& admission) noexcept;
    friend AudioRouteLeaseIdentity selection_audio_admission_lease(
        const SelectionAudioAdmission& admission) noexcept;
    friend bool cancel_selection_audio_admission(
        SelectionAudioAdmission& admission) noexcept;
    friend bool capture_selection_audio_admission_diagnostic(
        const SelectionAudioAdmission& admission,
        ChartAudioDiagnosticPrewriteSnapshot& out) noexcept;
    friend bool capture_selection_audio_admission_authority(
        const SelectionAudioAdmission& admission,
        SelectionAudioAdmissionAuthority& out) noexcept;
};

struct SelectionAudioAdmissionAuthority final {
    bool exact = false;
    SelectionSnapshot selection{};
    CustomContextToken token{};
    std::uint64_t route_lifecycle_epoch = 0;
    // Selection activation claim, checked by the matcher. The two fields below
    // are chart transaction numbering and may be rewritten by chart_patch.
    std::uint64_t selection_activation = 0;
    std::uint64_t activation_generation = 0;
    std::uint64_t preparation_ordinal = 0;
};

SelectionAudioAdmission begin_selection_audio_admission(
    SelectionActivationClaim claim, bool chart_plan_complete) noexcept;
AudioRouteArmLease commit_selection_audio_admission(
    SelectionAudioAdmission& admission) noexcept;
AudioRouteLeaseIdentity selection_audio_admission_lease(
    const SelectionAudioAdmission& admission) noexcept;
bool cancel_selection_audio_admission(SelectionAudioAdmission& admission) noexcept;
bool capture_selection_audio_admission_diagnostic(
    const SelectionAudioAdmission& admission,
    ChartAudioDiagnosticPrewriteSnapshot& out) noexcept;
bool capture_selection_audio_admission_authority(
    const SelectionAudioAdmission& admission,
    SelectionAudioAdmissionAuthority& out) noexcept;
enum class SelectionAudioAuthorityFailure : std::uint8_t {
    None, NotExact, Disabled, ActiveToken, ActiveSelection, Activation,
    RouteEpoch, SelectionGuard, CurrentSelection,
    PlaybackSelection, PlaybackToken, PlaybackChartFailed
};
struct SelectionAudioAuthorityDiagnostic final {
    SelectionAudioAuthorityFailure failure = SelectionAudioAuthorityFailure::None;
    bool committed = false;
    std::uint64_t expected_selection = 0, observed_selection = 0;
    std::uint64_t expected_activation = 0, observed_activation = 0;
    std::uint64_t expected_route_epoch = 0, expected_preparation = 0;
    CustomContextToken expected_token{}, observed_token{};
};
const char* selection_audio_authority_failure_name(SelectionAudioAuthorityFailure) noexcept;
// The exact owner transfers from selection guard to playback at commit;
// cleanup-only authority is never sufficient for native expansion.
bool selection_audio_admission_authority_matches(
    const SelectionAudioAdmissionAuthority& authority,
    SelectionAudioAuthorityDiagnostic* diagnostic = nullptr) noexcept;
bool selection_audio_admission_cancellation_complete(
    const SelectionSnapshot& selection,
    const AudioRouteLeaseIdentity& lease) noexcept;
void block_custom_audio_route_for_unresolved_chart_mutation() noexcept;
void fail_required_chart_expansion(
    const SelectionAudioAdmissionAuthority& authority, void* wrapper) noexcept;
// Withdrawal only: native voices/resources remain owned until real cleanup.
void fail_chord_voicing_chart(std::shared_ptr<const ChordVoicingBinding>) noexcept;
AudioRouteCleanupResult release_audio_route_on_piano_list_return(int32_t item_index);
AudioRouteShutdownResult shutdown_audio_sead_with_result();
bool clear_frozen_profile_if_audio_unowned();
bool custom_audio_route_idle_for_menu_input() noexcept;
bool custom_audio_playback_clock(double& elapsed_seconds) noexcept;

}
