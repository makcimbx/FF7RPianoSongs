#include "game/module_hooks.h"
#include "game/menu_session_authority.h"

#include "game/audio_sead.h"
#include "core/hooks.h"
#include "core/logging.h"
#include "core/pe_image.h"
#include "game/audio_cleanup_policy.h"
#include "game/audio_production_wiring.h"
#include "game/audio_play_setup_diagnostics.h"
#include "game/audio_pointer_patch_runtime.h"
#include "game/audio_sidecar_runtime.h"
#include "game/duration.h"
#include "game/hook_specs.h"
#include "game/mabf_sidecar_loader.h"
#include "game/note_count.h"
#include "game/onmemory_bank_diagnostic.h"
#include "game/onmemory_bank_lifecycle.h"
#include "game/rvas.h"
#include "game/runtime_layouts.h"
#include "game/runtime_context_policy.h"
#include "game/scoreinfo_overlay.h"
#include "game/song_registry.h"
#include "game/title.h"
#include "game/ue_types.h"
#include "game/uobject_identity.h"
#include "game/uobject_lifetime.h"
#include "pipeline/mabf_builder.h"
#include "pipeline/pipeline_limits.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ff7r::piano::game {
void observe_cleanup_only_onmemory_bank_noexcept() noexcept;
bool try_clear_custom_activation_quarantine(
    CustomActivationQuarantineClearReason reason) noexcept;
namespace {

using audio_sead_detail::AudioFieldPatch;
using audio_sead_detail::ChartAudioPlaySetupDecision;
using audio_sead_detail::ChartAudioPlaySetupDiagnostic;
using audio_sead_detail::DeferredPlaySetupRestoreFailureEmitter;
using audio_sead_detail::PointerPatchRollbackState;
using audio_sead_detail::SidecarRuntimeState;
using audio_sead_detail::append_patch;
using audio_sead_detail::build_sidecar_state;
using audio_sead_detail::kSeadPayloadHeaderSize;
using audio_sead_detail::log_chart_audio_play_setup_diagnostic;
using audio_sead_detail::log_sidecar_state;
using audio_sead_detail::read_field_u32_as_u64;
using audio_sead_detail::read_field_u64;
using audio_sead_detail::restore_patches_reverse;
using audio_sead_detail::sidecar_key_for_song;
using audio_sead_detail::verify_field_value;
using audio_sead_detail::write_field_patch;

constexpr bool kEnableLiveSqexSeadDetours = true;
constexpr uintptr_t kPianoAudioOwnerActiveKeyOffset = 0x0c;

std::atomic<uint32_t> g_native_mabf_capture_failures{0};
std::mutex g_native_mabf_capture_mutex;
std::vector<const void*> g_native_mabf_capture_headers;

uint64_t native_mabf_capture_hash(const std::vector<uint8_t>& bytes)
{
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

void capture_native_mabf_once(void* music)
{
    if (!music || !ff7rp::pipeline::playable_extended_transport_available()) return;

    void* sead_header = nullptr;
    uint64_t allocation_tag = 0;
    uint64_t packed_size = 0;
    const auto fail = [&](const char* reason) {
        if (g_native_mabf_capture_failures.fetch_add(1, std::memory_order_relaxed) < 8) {
            core::log(core::LogLevel::Error,
                std::string("[audio_sead] native_mabf_capture status=failed reason=") + reason);
        }
    };
    if (!core::safe_read_field(music, runtime_layouts::SqexSeadSound::mabf_source, sead_header)
        || !sead_header ||
        !core::safe_read_field(sead_header, 0, allocation_tag) ||
        !core::safe_read_field(sead_header, 8, packed_size)) {
        fail("header_read");
        return;
    }
    const uint32_t low_size = static_cast<uint32_t>(packed_size);
    const uint32_t high_size = static_cast<uint32_t>(packed_size >> 32u);
    if (allocation_tag != 0x21 || low_size != high_size || low_size < ff7rp::pipeline::kMabfHeaderSize ||
        low_size > ff7rp::pipeline::kMaxMabfBytes) {
        fail("header_contract");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_native_mabf_capture_mutex);
        if (std::find(g_native_mabf_capture_headers.begin(), g_native_mabf_capture_headers.end(),
                sead_header) != g_native_mabf_capture_headers.end()) {
            return;
        }
        if (g_native_mabf_capture_headers.size() >= 16) return;
        g_native_mabf_capture_headers.push_back(sead_header);
    }
    std::vector<uint8_t> bytes(low_size);
    if (!core::safe_copy_bytes(static_cast<const uint8_t*>(sead_header) + kSeadPayloadHeaderSize,
            bytes.data(), bytes.size())) {
        fail("payload_read");
        return;
    }
    ff7rp::pipeline::MabfArtifactMetadata metadata;
    const auto validation = ff7rp::pipeline::validate_structural_mabf(bytes, &metadata);
    const bool structurally_valid = validation.ok();

    wchar_t local_app_data[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data,
        static_cast<DWORD>(std::size(local_app_data)));
    if (length == 0 || length >= std::size(local_app_data)) {
        fail("local_app_data");
        return;
    }
    const std::filesystem::path directory = std::filesystem::path(local_app_data) /
        L"FF7RPianoSongs" / L"captures";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        fail("create_directory");
        return;
    }
    const uint64_t content_hash = native_mabf_capture_hash(bytes);
    std::wostringstream filename;
    filename << L"native_" << std::hex << content_hash
             << (structurally_valid ? L".mabf" : L".unvalidated.mabf");
    const std::filesystem::path output = directory / filename.str();
    HANDLE file = CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS) {
            core::log(core::LogLevel::Info,
                "[audio_sead] native_mabf_capture status=already_exists path=" + output.generic_string());
            return;
        }
        fail("create_file");
        return;
    }
    bool write_ok = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20u));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written != chunk) {
            write_ok = false;
            break;
        }
        offset += written;
    }
    if (write_ok) write_ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!write_ok) {
        DeleteFileW(output.c_str());
        fail("write_file");
        return;
    }
    std::ostringstream out;
    out << "[audio_sead] native_mabf_capture status=saved path=" << output.generic_string()
        << " bytes=" << bytes.size()
        << " structural_validation=" << (structurally_valid ? "valid" : "rejected")
        << " validation_message=" << (structurally_valid ? "ok" : validation.message);
    if (structurally_valid) out << " frames=" << metadata.logical_source_frames;
    core::log(core::LogLevel::Info, out.str());
}

using BgmPrepareFn = void(__fastcall*)(void* bgm);
using SeadPlaySetupFn = ProductionPlaySetupFn;
using BgmSlotStopFn = ProductionBgmSlotStopFn;
using BgmPlaybackTransferFn = void(__fastcall*)(void* destination, void* source);
using BgmSlotSetFn = ProductionBgmSlotSetFn;
using BgmSlotPlayFn = ProductionBgmSlotPlayFn;
using BgmControllerLookupFn = void*(__fastcall*)(uint64_t slot_key);
using BgmManagerPauseFn = void(__fastcall*)(void* manager, float fade_seconds);
using BgmSlotTransitionFn = void(__fastcall*)(void* slot);
using BgmSlotSetupFn = void(__fastcall*)(void* slot, uint8_t flag, uint64_t context);
using PianoAudioStateTickFn = void(__fastcall*)(void* owner, float delta_seconds);
using PianoAdaptiveJudgmentFn = void(__fastcall*)(void* adaptive_state, void* event);
using PianoAudioRequestFn = void(__fastcall*)(uint8_t request_index, uint64_t packed_key, float scalar1, float scalar2, uint8_t flag);
using DescriptorBuildFn = uint64_t*(__fastcall*)(uint64_t* out_descriptor, void* sound);
using CreatePackageFn = void*(__fastcall*)(const wchar_t* package_name);
using StaticConstructObjectFn = void*(__fastcall*)(void* params);
using FNameCtorFn = void*(__fastcall*)(FNameValue* out_name, const wchar_t* text, int32_t find_type);
using SeadOnMemoryBankKindLookupFn = uint32_t(__fastcall*)(const uint64_t* token);
using SeadOnMemoryBankReleaseFn = uint64_t(__fastcall*)(const uint64_t* token, uint8_t asynchronous);

struct FStaticConstructObjectParametersLocal {
    void* object_class = nullptr;
    void* outer = nullptr;
    FNameValue name{};
    uint32_t set_flags = 0;
    uint32_t internal_set_flags = 0;
    bool copy_transients_from_class_defaults = false;
    bool assume_template_is_archetype = false;
    uint8_t padding[6]{};
    void* object_template = nullptr;
    void* instance_graph = nullptr;
    void* external_package = nullptr;
};

static_assert(sizeof(FStaticConstructObjectParametersLocal) == 0x40, "StaticConstructObject params layout must match prototype");

std::mutex g_audio_state_mutex;
struct AudioProductionOperationCoordinatorView {
    std::recursive_mutex& mutex() noexcept {
        return audio_production_operation_mutex();
    }
} g_audio_route_operations;
RegistrySnapshot g_sidecar_catalog;
std::shared_ptr<const audio_sead_detail::PreparedAudioPrefix> g_sidecar_prefix;
PointerPatchRollbackState g_sound_patch_rollback;
PointerPatchRollbackState g_controller_patch_rollback;
core::RawRvaHook g_bgm_prepare_hook;
BgmPrepareFn g_original_bgm_prepare = nullptr;
core::RawRvaHook g_play_setup_hook;
SeadPlaySetupFn& g_original_play_setup = production_play_setup_trampoline();
core::RawRvaHook g_bgm_slot_stop_hook;
BgmSlotStopFn& g_original_bgm_slot_stop = production_bgm_slot_stop_trampoline();
core::RawRvaHook g_bgm_playback_transfer_hook;
BgmPlaybackTransferFn g_original_bgm_playback_transfer = nullptr;
core::RawRvaHook g_bgm_slot_set_hook;
BgmSlotSetFn& g_original_bgm_slot_set = production_bgm_slot_set_trampoline();
core::RawRvaHook g_bgm_slot_play_hook;
BgmSlotPlayFn& g_original_bgm_slot_play = production_bgm_slot_play_trampoline();
core::RawRvaHook g_bgm_manager_pause_hook;
BgmManagerPauseFn g_original_bgm_manager_pause = nullptr;
core::RawRvaHook g_bgm_slot_pause_transition_hook;
BgmSlotTransitionFn g_original_bgm_slot_pause_transition = nullptr;
core::RawRvaHook g_bgm_slot_resume_transition_hook;
BgmSlotTransitionFn g_original_bgm_slot_resume_transition = nullptr;
core::RawRvaHook g_piano_audio_state_tick_hook;
PianoAudioStateTickFn g_original_piano_audio_state_tick = nullptr;
core::RawRvaHook g_piano_adaptive_judgment_hook;
PianoAdaptiveJudgmentFn g_original_piano_adaptive_judgment = nullptr;
HMODULE g_exe_module = nullptr;
std::atomic_bool g_audio_route_installed{false};
std::atomic_bool g_audio_route_disabled{false};
std::atomic_bool g_controller_lookup_available{false};
std::atomic_bool g_controller_rebuild_available{false};
std::atomic_bool g_slot_setup_available{false};
std::atomic_bool g_piano_audio_request_available{false};
std::atomic_bool g_onmemory_bank_kind_lookup_available{false};
std::atomic_uintptr_t g_onmemory_bank_kind_lookup{0};
std::atomic_bool g_onmemory_bank_release_available{false};
std::atomic_uintptr_t g_onmemory_bank_release{0};
std::atomic_uintptr_t g_alias_music{0};
OnMemoryBankDiagnosticPairState g_onmemory_bank_diagnostic_pair;
OnMemoryBankLifecycleState g_onmemory_bank_lifecycle;
OnMemoryBankCleanupOnlyState g_onmemory_bank_cleanup_only;
uint64_t g_onmemory_bank_cleanup_only_generation = 0;
CleanupOnlyObservationFingerprint g_cleanup_only_observation_fingerprint;
uint32_t g_cleanup_only_observation_fact_logs = 0;
CustomActivationQuarantineRecord g_custom_activation_quarantine;
uint64_t g_custom_activation_quarantine_generation = 0;
std::atomic_uint64_t g_selection_activation_revocation_epoch{1};

struct PendingPlaySetupPatch {
    void* sound = nullptr;
    std::string song_id;
    std::vector<AudioFieldPatch> patches;
    bool restore_safe = true;
};

enum class AudioRoutePhase {
    Idle,
    Armed,
    Rebuilding,
    PatchedPlaySetup,
    Playing,
    CanonicalRelinquishmentPending,
    CanonicalRelinquished,
};

struct UObjectIdentity {
    void* object_class = nullptr;
    FNameValue name{};
    void* outer = nullptr;
    bool raw_internal_index_readable = false;
    int32_t raw_internal_index = -1;
    bool live_capture_attempted = false;
    bool live_capture_succeeded = false;
    UObjectLiveHandleCaptureResult live_capture_result =
        UObjectLiveHandleCaptureResult::ResolverOrViewUnavailable;
    UObjectLiveHandle live{};
    bool item_backed_zero_serial_capture_attempted = false;
    bool item_backed_zero_serial_capture_succeeded = false;
    UObjectItemBackedZeroSerialCaptureResult
        item_backed_zero_serial_capture_result =
            UObjectItemBackedZeroSerialCaptureResult::ResolverOrViewUnavailable;
    UObjectItemBackedZeroSerialSnapshot item_backed_zero_serial{};
};

struct FrozenSoundPatchSnapshot {
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease_identity{};
    void* sound = nullptr;
    UObjectIdentity sound_identity{};
    std::vector<AudioFieldPatch> patches;

    bool valid() const noexcept
    {
        return route_generation != 0 && lease_identity.valid() && sound
            && patches.size() == 5;
    }
};

bool same_frozen_sound_patch_snapshot(
    const FrozenSoundPatchSnapshot& left,
    const FrozenSoundPatchSnapshot& right) noexcept
{
    if (left.route_generation != right.route_generation
        || !(left.lease_identity == right.lease_identity)
        || left.sound != right.sound
        || left.sound_identity.live.internal_index
            != right.sound_identity.live.internal_index
        || left.sound_identity.live.serial_number
            != right.sound_identity.live.serial_number
        || left.patches.size() != right.patches.size()) {
        return false;
    }
    for (size_t index = 0; index < left.patches.size(); ++index) {
        const auto& a = left.patches[index];
        const auto& b = right.patches[index];
        if (a.object != b.object || a.offset != b.offset
            || a.original != b.original || a.replacement != b.replacement
            || a.size != b.size
            || a.redact_values_in_report != b.redact_values_in_report
            || ((!a.label || !b.label) ? a.label != b.label
                                       : std::strcmp(a.label, b.label) != 0)) {
            return false;
        }
    }
    return true;
}

bool same_onmemory_bank_detached_record(
    const OnMemoryBankDetachedRecord& left,
    const OnMemoryBankDetachedRecord& right) noexcept
{
    return left.phase == right.phase && left.sound.object == right.sound.object
        && left.sound.live.internal_index == right.sound.live.internal_index
        && left.sound.live.serial_number == right.sound.live.serial_number
        && left.canonical.encode() == right.canonical.encode()
        && left.custom.encode() == right.custom.encode()
        && left.ordinal == right.ordinal
        && left.route_generation == right.route_generation
        && left.cleanup_generation == right.cleanup_generation
        && left.request_handle == right.request_handle
        && left.backing_identity == right.backing_identity
        && left.backing_observed == right.backing_observed
        && left.observation_count == right.observation_count
        && left.owner_restore_verified == right.owner_restore_verified
        && left.release_attempted == right.release_attempted;
}

bool exact_natural_completion_backing_matches_lifecycle(
    const OnMemoryBankRetiredBackingEvidence& evidence,
    const OnMemoryBankLifecycleState& lifecycle) noexcept
{
    return onmemory_bank_exact_natural_detached_backing_current(
        evidence, lifecycle.state_epoch(), lifecycle.active());
}

struct SlotSetupProfile {
    bool ready = false;
    void* controller = nullptr;
    UObjectIdentity controller_identity{};
    void* slot = nullptr;
    uint32_t field34 = 0;
    uint32_t field48 = 0;
    uint8_t field5e = 0;
    std::array<uint8_t, 0x3c> fields60_to_9b{};
    uint8_t field9c = 0;
    uint8_t setup_flag = 0;
};

struct NativePlaySetupProfile {
    bool ready = false;
    std::string song_id;
    void* controller = nullptr;
    UObjectIdentity controller_identity{};
    void* slot = nullptr;
    void* sound = nullptr;
    UObjectIdentity sound_identity{};
    float arg1 = 0.0f;
    float arg2 = 0.0f;
    uint64_t arg3 = 0;
    uint64_t arg4 = 0;
    uint8_t flag = 0;
    std::array<uint8_t, 12> arg6{};
};

struct PianoAudioRequestProfile {
    bool ready = false;
    std::string captured_song_id;
    int32_t base_slot = -1;
    void* owner = nullptr;
    uint8_t request_index = 0;
    uint64_t packed_key = 0;
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease_identity{};
    void* controller = nullptr;
    UObjectIdentity controller_identity{};
    void* slot = nullptr;
    void* bgm = nullptr;
    uint64_t request_handle = 0;
};

struct AudioRouteState {
    AudioRoutePhase phase = AudioRoutePhase::Idle;
    uint64_t generation = 0;
    uint64_t canonical_relinquishment_generation = 0;
    AudioRouteLeaseIdentity lease_identity{};
    std::string desired_song_id;
    std::string patched_song_id;
    void* sound = nullptr;
    UObjectIdentity private_setup_sound_identity{};
    void* controller = nullptr;
    UObjectIdentity controller_identity{};
    bool controller_arm_proof_attempted = false;
    ControllerIdentityProof controller_arm_attempted_proof{};
    bool controller_arm_bind_succeeded = false;
    bool stop_observed = false;
    uint64_t stop_authorized_generation = 0;
    bool set_play_handoff_pending = false;
    void* handoff_slot = nullptr;
    void* handoff_bgm = nullptr;
    void* handoff_sound = nullptr;
    UObjectIdentity handoff_sound_identity{};
    uint64_t handoff_request_handle = 0;
    bool custom_resource_owned = false;
    bool aggregate_awaiting_transition = false;
    void* owned_slot = nullptr;
    void* owned_bgm = nullptr;
    void* owned_sound = nullptr;
    UObjectIdentity owned_sound_identity{};
    uint64_t owned_request_handle = 0;
    void* reusable_sound = nullptr;
    UObjectIdentity reusable_sound_identity{};
    void* reusable_slot = nullptr;
    void* reusable_bgm = nullptr;
    bool list_cleanup_pending = false;
    bool native_clear_verified = false;
    AudioDeferredNativeHandoffState deferred_native_handoff{};
    UObjectIdentity deferred_native_sound_identity{};
    FrozenSoundPatchSnapshot frozen_sound_patch{};
    AudioStopRetirementState stop_retirement{};
    uint64_t stop_retirement_epoch = 0;
    OnMemoryBankRetiredBackingEvidence stop_retirement_backing{};
    BgmPlaybackAggregateTerminalAuthority terminal_handoff{};
};

enum class PendingSoundRoute {
    None,
    RequiresPlaySetup,
    SameSongReuseBlocked,
    SongMismatch,
    UnsafeStoppedPatch,
};

PendingPlaySetupPatch g_pending_play_setup_patch;
std::vector<AudioFieldPatch> g_failed_patch_journal;
AudioRouteState g_audio_route_state;
UnpublishedAudioSetupContext g_unpublished_audio_setup;
FrozenProfileLeaseState g_frozen_profile_lease;
uint64_t g_next_audio_route_generation = 0;
uint64_t g_next_stop_retirement_epoch = 0;
AudioPatchJournalState g_active_patch_journal;

enum class AudioRouteTransitionReason : uint8_t {
    AbandonUnmodifiedArm,
    AggregateSetForward,
    AggregateExitComplete,
    RetirementCleanup,
    ControllerRebuildRouteChanged,
    ControllerRebuildFailedRelease,
    PrivateControllerFailure,
    PrivateSetCaptureRejected,
    SlotSetInvalidation,
    SlotSetHandoffFailure,
    SlotPlayHandoffFailure,
    AdmissionArmPublished,
    AdmissionArmBindFailure,
    AdmissionCancel,
    ArmRoutePublished,
    ArmSetupInvalidated,
    ArmOwnerRequestFailure,
    ArmRebuildFailure,
    ListReturnEarly,
    ListReturnVerified,
    ListReturnCanonicalRelinquish,
    ListReturnRouteRestoreRelinquish,
    ListReturnNativeClear,
    ShutdownReset,
    SetupProofRejected,
    SetupRouteMismatch,
    SetupPublicationFailed,
    SetupPublished,
    SetupPlaybackRevoked,
    PlaySetupRejected,
    FeatureDisabled,
};

enum class AudioRouteTransitionKind : uint8_t {
    RouteReset,
    SetupInvalidation,
    Combined,
};

struct AudioRouteTransitionProjection final {
    bool route_enabled = false;
    AudioRoutePhase route_phase = AudioRoutePhase::Idle;
    uint64_t route_generation = 0;
    uint64_t route_lease_generation = 0;
    uint64_t route_lease_song_key = 0;
    uint64_t route_song_key = 0;
    bool route_owned = false;
    bool cleanup_pending = false;
    bool setup_valid = false;
    PrivateControllerSetupStage setup_stage = PrivateControllerSetupStage::AwaitingStop;
    uint64_t setup_registry_generation = 0;
    uint64_t setup_route_generation = 0;
    uint64_t setup_lease_generation = 0;
    uint64_t setup_song_key = 0;
    uintptr_t setup_controller = 0;
    uintptr_t setup_slot = 0;
    uintptr_t setup_bgm = 0;
    uintptr_t setup_expected_sound = 0;
    int32_t setup_expected_sound_index = -1;
    int32_t setup_expected_sound_serial = 0;
    CanonicalSubstrateBridgePhase setup_bridge_phase =
        CanonicalSubstrateBridgePhase::None;
    uint64_t setup_bridge_generation = 0;
    uint64_t setup_bridge_canonical_token = 0;
    uintptr_t setup_bridge_expected_sound = 0;
    bool frozen_profile_active = false;
    bool frozen_profile_native_arm_attempted = false;
    bool route_native_arm_attempted = false;
};

struct PlaySetupQualificationDiagnostic final {
    bool valid = false;
    uintptr_t callback_sound = 0;
    int32_t callback_sound_index = -1;
    int32_t callback_sound_serial = 0;
    bool callback_sound_identity_established = false;
    bool sound_identity_read_attempted = false;
    bool sound_identity_read_succeeded = false;
    bool sound_live_capture_succeeded = false;
    bool sound_index_valid = false;
    bool sound_serial_valid = false;
    bool owner_read_attempted = false;
    bool owner_read_succeeded = false;
    uint64_t owner_token = 0;
    bool route_matches = false;
    bool setup_matches = false;
    bool sidecar_matches = false;
    bool supported_build = false;
    bool lookup_signature_valid = false;
    bool release_signature_valid = false;
    PlaySetupQualificationEntryFailure entry_first_failure =
        PlaySetupQualificationEntryFailure::None;
    bool preflight_reached = false;
    OnMemoryBankPlaySetupPreflight preflight{};
    bool bank_lookup_reached = false;
    uint32_t queried_canonical_kind = UINT32_MAX;
    bool revalidation_reached = false;
    PlaySetupQualificationRevalidation revalidation{};
    PlaySetupQualificationRevalidationFailure revalidation_first_failure =
        PlaySetupQualificationRevalidationFailure::None;
    bool commit_reached = false;
    OnMemoryBankPlaySetupCommitResult commit{};
};

static_assert(std::is_trivially_copyable_v<PlaySetupQualificationDiagnostic>);

struct AudioRouteTransitionRecord final {
    AudioRouteTransitionReason reason = AudioRouteTransitionReason::FeatureDisabled;
    AudioRouteTransitionKind kind = AudioRouteTransitionKind::RouteReset;
    AudioRouteCallbackKind callback_kind = AudioRouteCallbackKind::None;
    uint64_t sequence = 0;
    uint32_t thread_id = 0;
    uint32_t callback_depth = 0;
    uint32_t play_setup_depth = 0;
    uint32_t replay_depth = 0;
    AudioRouteTransitionProjection old_state{};
    AudioRouteTransitionProjection new_state{};
    PlaySetupQualificationDiagnostic play_setup_qualification{};
};
static_assert(std::is_trivially_copyable_v<AudioRouteTransitionRecord>);

constexpr size_t kAudioRouteTransitionCapacity = 128;
std::array<AudioRouteTransitionRecord, kAudioRouteTransitionCapacity>
    g_audio_route_transition_records{};
size_t g_audio_route_transition_start = 0;
size_t g_audio_route_transition_count = 0;
uint64_t g_audio_route_transition_overwritten = 0;
uint64_t g_audio_route_transition_sequence = 0;

thread_local uint32_t g_native_play_setup_replay_depth = 0;

const char* onmemory_bank_presence_name(const OnMemoryBankPresence presence) noexcept
{
    switch (presence) {
    case OnMemoryBankPresence::PresentSabf: return "present_sabf";
    case OnMemoryBankPresence::Absent: return "absent";
    case OnMemoryBankPresence::PresentSupportedBank: return "present_supported_bank";
    case OnMemoryBankPresence::Unexpected: return "unexpected";
    }
    return "unexpected";
}

uint32_t lookup_onmemory_bank_kind_noexcept(const uint64_t token) noexcept
{
    const auto address = g_onmemory_bank_kind_lookup.load(std::memory_order_acquire);
    if (!address) return UINT32_MAX;
    auto* const lookup = reinterpret_cast<SeadOnMemoryBankKindLookupFn>(address);
    __try {
        return lookup(&token);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return UINT32_MAX;
    }
}

OnMemoryBankNativeReleaseResult release_onmemory_bank_async_noexcept(
    const uint64_t* const copied_custom_token,
    const uint8_t asynchronous) noexcept
{
    OnMemoryBankNativeReleaseResult result;
    const auto address = g_onmemory_bank_release.load(std::memory_order_acquire);
    if (!address || !copied_custom_token || asynchronous != 1) return result;
    auto* const release = reinterpret_cast<SeadOnMemoryBankReleaseFn>(address);
    __try {
        result.ignored_native_return = release(copied_custom_token, asynchronous);
        result.completed = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = {};
    }
    return result;
}

void log_onmemory_lifecycle(
    const char* const status,
    const OnMemoryBankLifecyclePhase phase,
    const uint64_t route_generation,
    const uint64_t cleanup_generation,
    const uint64_t ordinal,
    const char* const reason,
    const uint32_t canonical_kind = UINT32_MAX,
    const uint32_t custom_kind = UINT32_MAX,
    const uint64_t canonical_token = 0,
    const uint64_t custom_token = 0) noexcept
{
    try {
        static std::atomic_uint32_t s_logs{0};
        if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 128) return;
        core::log(phase == OnMemoryBankLifecyclePhase::Failed
                ? core::LogLevel::Error : core::LogLevel::Info,
            format_onmemory_bank_lifecycle_log(
                status, phase, route_generation, cleanup_generation,
                ordinal, reason, canonical_kind, custom_kind,
                canonical_token, custom_token));
    } catch (...) {
    }
}

// Retirement classification is now the single fact that decides which
// retirement path runs, so it is always reported: the outcome, both token
// values, and -- when rejected -- the first predicate that failed.
void log_onmemory_retain_outcome(
    const OnMemoryBankRetainResult& result) noexcept
{
    log_onmemory_lifecycle(
        onmemory_bank_retain_outcome_name(result.outcome),
        result.outcome == OnMemoryBankRetainOutcome::Detached
            ? OnMemoryBankLifecyclePhase::RestoreApplied
            : OnMemoryBankLifecyclePhase::CanonicalQualified,
        result.route_generation, result.cleanup_generation, result.ordinal,
        onmemory_bank_retain_failure_name(result.first_failure),
        UINT32_MAX, UINT32_MAX,
        result.canonical_token, result.custom_token);
}

void log_onmemory_bank_pair(
    const char* stage, const OnMemoryBankDiagnosticPair& pair) noexcept
{
    try {
        static std::atomic_uint32_t s_logs{0};
        observe_onmemory_bank_pair(
            g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
            [](const uint64_t token) noexcept {
                return lookup_onmemory_bank_kind_noexcept(token);
            },
            pair,
            [&](const OnMemoryBankDiagnosticRole diagnostic_role,
                const DecodedOnMemoryBankToken& token, const uint32_t bank_kind,
                const OnMemoryBankPresence presence) {
                if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 32) return;
                const char* const role = diagnostic_role == OnMemoryBankDiagnosticRole::Original
                    ? "original" : "custom";
                std::ostringstream out;
                out << "[audio_sead] onmemory_bank_residency"
                    << " stage=" << stage
                    << " role=" << role
                    << " route_generation=" << pair.route_generation
                    << " cleanup_generation=" << pair.cleanup_generation
                    << " token=" << token.encode()
                    << " original_token=" << pair.original.encode()
                    << " custom_token=" << pair.custom.encode()
                    << " tokens_shared="
                    << (pair.original.encode() == pair.custom.encode() ? 1 : 0)
                    << " bank_kind=" << bank_kind
                    << " classification=" << onmemory_bank_presence_name(presence);
                core::log(core::LogLevel::Info, out.str());
            });
    } catch (...) {
    }
}

uint64_t audio_route_song_key(const std::string& song_id)
{
    uint64_t value = 1469598103934665603ull;
    for (const unsigned char character : song_id) {
        value ^= character;
        value *= 1099511628211ull;
    }
    return value == 0 ? 1 : value;
}

bool native_audio_route_owned_locked();

AudioRouteTransitionProjection capture_audio_route_transition_projection_locked() noexcept
{
    AudioRouteTransitionProjection out;
    const auto& route = g_audio_route_state;
    const auto& setup = g_unpublished_audio_setup;
    out.route_enabled = !g_audio_route_disabled.load(std::memory_order_acquire);
    out.route_phase = route.phase;
    out.route_generation = route.generation;
    out.route_lease_generation = route.lease_identity.generation;
    out.route_lease_song_key = route.lease_identity.song_key;
    out.route_song_key = route.desired_song_id.empty()
        ? 0 : audio_route_song_key(route.desired_song_id);
    out.route_owned = native_audio_route_owned_locked();
    out.cleanup_pending = route.list_cleanup_pending;
    out.setup_valid = static_cast<bool>(setup);
    if (setup) {
        out.setup_stage = setup.controller_stage;
        out.setup_registry_generation = setup.token.registry_generation;
        out.setup_route_generation = setup.token.route_generation;
        out.setup_lease_generation = setup.token.lease_generation;
        out.setup_song_key = setup.token.song_key;
        out.setup_controller = reinterpret_cast<uintptr_t>(setup.controller);
        out.setup_slot = reinterpret_cast<uintptr_t>(setup.slot);
        out.setup_bgm = reinterpret_cast<uintptr_t>(setup.bgm);
        out.setup_expected_sound = reinterpret_cast<uintptr_t>(setup.expected_sound);
        out.setup_expected_sound_index = setup.expected_sound_handle.internal_index;
        out.setup_expected_sound_serial = setup.expected_sound_handle.serial_number;
        out.setup_bridge_phase = setup.substrate_bridge.phase;
        out.setup_bridge_generation = setup.substrate_bridge.generation;
        out.setup_bridge_canonical_token = setup.substrate_bridge.canonical_token;
        out.setup_bridge_expected_sound = reinterpret_cast<uintptr_t>(
            setup.substrate_bridge.expected_callback_sound);
    }
    out.frozen_profile_active = g_frozen_profile_lease.active();
    out.frozen_profile_native_arm_attempted = out.frozen_profile_active
        && g_frozen_profile_lease.native_arm_attempted();
    out.route_native_arm_attempted = route.controller_arm_proof_attempted;
    return out;
}

uint64_t next_audio_route_transition_sequence_locked() noexcept
{
    ++g_audio_route_transition_sequence;
    if (g_audio_route_transition_sequence == 0) ++g_audio_route_transition_sequence;
    return g_audio_route_transition_sequence;
}

void enqueue_audio_route_transition_locked(
    const AudioRouteTransitionReason reason,
    const AudioRouteTransitionKind kind,
    const AudioRouteTransitionProjection& old_state,
    const PlaySetupQualificationDiagnostic* const play_setup_qualification = nullptr) noexcept
{
    AudioRouteTransitionRecord record;
    record.reason = reason;
    record.kind = kind;
    record.callback_kind = audio_production_callback_kind();
    record.sequence = next_audio_route_transition_sequence_locked();
    record.thread_id = GetCurrentThreadId();
    record.callback_depth = audio_production_callback_depth();
    record.play_setup_depth = audio_production_play_setup_tls().original_depth;
    record.replay_depth = g_native_play_setup_replay_depth;
    record.old_state = old_state;
    record.new_state = capture_audio_route_transition_projection_locked();
    if (play_setup_qualification) {
        record.play_setup_qualification = *play_setup_qualification;
    }
    if (g_audio_route_transition_count == kAudioRouteTransitionCapacity) {
        g_audio_route_transition_start =
            (g_audio_route_transition_start + 1) % kAudioRouteTransitionCapacity;
        --g_audio_route_transition_count;
        ++g_audio_route_transition_overwritten;
    }
    const size_t index = (g_audio_route_transition_start
        + g_audio_route_transition_count) % kAudioRouteTransitionCapacity;
    g_audio_route_transition_records[index] = record;
    ++g_audio_route_transition_count;
}

class AudioRouteTransitionRecorder final {
public:
    AudioRouteTransitionRecorder(
        const AudioRouteTransitionReason reason,
        const AudioRouteTransitionKind kind,
        const PlaySetupQualificationDiagnostic* const play_setup_qualification = nullptr) noexcept
        : reason_(reason), kind_(kind), old_(capture_audio_route_transition_projection_locked()),
          play_setup_qualification_(play_setup_qualification)
    {
    }

    ~AudioRouteTransitionRecorder() noexcept
    {
        enqueue_audio_route_transition_locked(
            reason_, kind_, old_, play_setup_qualification_);
    }

private:
    AudioRouteTransitionReason reason_;
    AudioRouteTransitionKind kind_;
    AudioRouteTransitionProjection old_;
    const PlaySetupQualificationDiagnostic* play_setup_qualification_ = nullptr;
};

const char* audio_deferred_native_handoff_phase_name(
    AudioDeferredNativeHandoffPhase phase) noexcept
{
    switch (phase) {
    case AudioDeferredNativeHandoffPhase::None: return "none";
    case AudioDeferredNativeHandoffPhase::SetCaptured: return "set_captured";
    case AudioDeferredNativeHandoffPhase::CustomCleared: return "custom_cleared";
    case AudioDeferredNativeHandoffPhase::NativeSetApplied: return "native_set_applied";
    case AudioDeferredNativeHandoffPhase::NativePlayForwarded: return "native_play_forwarded";
    case AudioDeferredNativeHandoffPhase::RetainedFailure: return "retained_failure";
    }
    return "unknown";
}

const char* audio_route_transition_reason_name(AudioRouteTransitionReason reason) noexcept
{
    switch (reason) {
    case AudioRouteTransitionReason::AbandonUnmodifiedArm: return "abandon_unmodified_arm";
    case AudioRouteTransitionReason::AggregateSetForward: return "aggregate_set_forward";
    case AudioRouteTransitionReason::AggregateExitComplete: return "aggregate_exit_complete";
    case AudioRouteTransitionReason::RetirementCleanup: return "retirement_cleanup";
    case AudioRouteTransitionReason::ControllerRebuildRouteChanged: return "controller_rebuild_route_changed";
    case AudioRouteTransitionReason::ControllerRebuildFailedRelease: return "controller_rebuild_failed_release";
    case AudioRouteTransitionReason::PrivateControllerFailure: return "private_controller_failure";
    case AudioRouteTransitionReason::PrivateSetCaptureRejected: return "private_set_capture_rejected";
    case AudioRouteTransitionReason::SlotSetInvalidation: return "slot_set_invalidation";
    case AudioRouteTransitionReason::SlotSetHandoffFailure: return "slot_set_handoff_failure";
    case AudioRouteTransitionReason::SlotPlayHandoffFailure: return "slot_play_handoff_failure";
    case AudioRouteTransitionReason::AdmissionArmPublished: return "admission_arm_published";
    case AudioRouteTransitionReason::AdmissionArmBindFailure: return "admission_arm_bind_failure";
    case AudioRouteTransitionReason::AdmissionCancel: return "admission_cancel";
    case AudioRouteTransitionReason::ArmRoutePublished: return "arm_route_published";
    case AudioRouteTransitionReason::ArmSetupInvalidated: return "arm_setup_invalidated";
    case AudioRouteTransitionReason::ArmOwnerRequestFailure: return "arm_owner_request_failure";
    case AudioRouteTransitionReason::ArmRebuildFailure: return "arm_rebuild_failure";
    case AudioRouteTransitionReason::ListReturnEarly: return "list_return_early";
    case AudioRouteTransitionReason::ListReturnVerified: return "list_return_verified";
    case AudioRouteTransitionReason::ListReturnCanonicalRelinquish: return "list_return_canonical_relinquish";
    case AudioRouteTransitionReason::ListReturnRouteRestoreRelinquish: return "list_return_route_restore_relinquish";
    case AudioRouteTransitionReason::ListReturnNativeClear: return "list_return_native_clear";
    case AudioRouteTransitionReason::ShutdownReset: return "shutdown_reset";
    case AudioRouteTransitionReason::SetupProofRejected: return "setup_proof_rejected";
    case AudioRouteTransitionReason::SetupRouteMismatch: return "setup_route_mismatch";
    case AudioRouteTransitionReason::SetupPublicationFailed: return "setup_publication_failed";
    case AudioRouteTransitionReason::SetupPublished: return "setup_published";
    case AudioRouteTransitionReason::SetupPlaybackRevoked: return "setup_playback_revoked";
    case AudioRouteTransitionReason::PlaySetupRejected: return "playsetup_rejected";
    case AudioRouteTransitionReason::FeatureDisabled: return "feature_disabled";
    }
    return "unknown";
}

void drain_audio_route_transition_diagnostics_noexcept() noexcept
{
    std::array<AudioRouteTransitionRecord, kAudioRouteTransitionCapacity> records{};
    size_t count = 0;
    uint64_t overwritten = 0;
    try {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            count = g_audio_route_transition_count;
            overwritten = g_audio_route_transition_overwritten;
            for (size_t i = 0; i < count; ++i) {
                records[i] = g_audio_route_transition_records[
                    (g_audio_route_transition_start + i)
                    % kAudioRouteTransitionCapacity];
            }
            g_audio_route_transition_start = 0;
            g_audio_route_transition_count = 0;
            g_audio_route_transition_overwritten = 0;
        }
        for (size_t i = 0; i < count; ++i) {
            const auto& record = records[i];
            const auto append = [](std::ostringstream& out, const char* prefix,
                                    const AudioRouteTransitionProjection& state) {
                out << ' ' << prefix << "_enabled=" << (state.route_enabled ? 1 : 0)
                    << ' ' << prefix << "_phase=" << static_cast<unsigned>(state.route_phase)
                    << ' ' << prefix << "_generation=" << state.route_generation
                    << ' ' << prefix << "_lease=" << state.route_lease_generation
                    << ' ' << prefix << "_lease_song=" << state.route_lease_song_key
                    << ' ' << prefix << "_song=" << state.route_song_key
                    << ' ' << prefix << "_owned=" << (state.route_owned ? 1 : 0)
                    << ' ' << prefix << "_cleanup=" << (state.cleanup_pending ? 1 : 0)
                    << ' ' << prefix << "_setup_valid=" << (state.setup_valid ? 1 : 0)
                    << ' ' << prefix << "_setup_stage=" << static_cast<unsigned>(state.setup_stage)
                    << ' ' << prefix << "_setup_registry=" << state.setup_registry_generation
                    << ' ' << prefix << "_setup_route=" << state.setup_route_generation
                    << ' ' << prefix << "_setup_lease=" << state.setup_lease_generation
                    << ' ' << prefix << "_setup_song=" << state.setup_song_key
                    << ' ' << prefix << "_setup_controller=0x" << std::hex << state.setup_controller
                    << ' ' << prefix << "_setup_slot=0x" << state.setup_slot
                    << ' ' << prefix << "_setup_bgm=0x" << state.setup_bgm
                    << ' ' << prefix << "_setup_expected_sound=0x" << state.setup_expected_sound
                    << std::dec
                    << ' ' << prefix << "_setup_expected_index=" << state.setup_expected_sound_index
                    << ' ' << prefix << "_setup_expected_serial=" << state.setup_expected_sound_serial
                    << ' ' << prefix << "_bridge_phase="
                    << static_cast<unsigned>(state.setup_bridge_phase)
                    << ' ' << prefix << "_bridge_generation="
                    << state.setup_bridge_generation
                    << ' ' << prefix << "_bridge_canonical=0x" << std::hex
                    << state.setup_bridge_canonical_token << std::dec
                    << ' ' << prefix << "_bridge_expected_sound=0x" << std::hex
                    << state.setup_bridge_expected_sound << std::dec
                    << ' ' << prefix << "_frozen=" << (state.frozen_profile_active ? 1 : 0)
                    << ' ' << prefix << "_native_arm="
                    << (state.frozen_profile_native_arm_attempted ? 1 : 0)
                    << ' ' << prefix << "_route_arm="
                    << (state.route_native_arm_attempted ? 1 : 0);
            };
            const auto rebase_mask = [](const OnMemoryBankCanonicalRebaseFacts& facts) {
                return (facts.lifecycle_healthy ? 1u << 0 : 0u)
                    | (facts.no_release_in_flight ? 1u << 1 : 0u)
                    | (facts.no_active_pending ? 1u << 2 : 0u)
                    | (facts.canonical_ready ? 1u << 3 : 0u)
                    | (facts.same_sound_identity ? 1u << 4 : 0u)
                    | (facts.candidate_type1 ? 1u << 5 : 0u)
                    | (facts.candidate_differs_stale_canonical ? 1u << 6 : 0u)
                    | (facts.completed_custom_absence ? 1u << 7 : 0u)
                    | (facts.completed_sound_matches ? 1u << 8 : 0u)
                    | (facts.completed_canonical_matches ? 1u << 9 : 0u)
                    | (facts.prior_custom_type1 ? 1u << 10 : 0u)
                    | (facts.candidate_differs_prior_custom ? 1u << 11 : 0u);
            };
            std::ostringstream out;
            out << "[audio_sead] audio_route_transition"
                << " reason=" << audio_route_transition_reason_name(record.reason)
                << " sequence=" << record.sequence
                << " kind=" << static_cast<unsigned>(record.kind)
                << " thread=" << record.thread_id
                << " callback_kind=" << static_cast<unsigned>(record.callback_kind)
                << " callback_depth=" << record.callback_depth
                << " playsetup_depth=" << record.play_setup_depth
                << " replay_depth=" << record.replay_depth
                << " overwritten_before_drain=" << (i == 0 ? overwritten : 0);
            append(out, "old", record.old_state);
            append(out, "new", record.new_state);
            const auto& diagnostic = record.play_setup_qualification;
            if (diagnostic.valid) {
                const auto& preflight = diagnostic.preflight;
                const auto& commit = diagnostic.commit;
                out << " psq_valid=1"
                    << " psq_sound=0x" << std::hex << diagnostic.callback_sound
                    << std::dec
                    << " psq_index=" << diagnostic.callback_sound_index
                    << " psq_serial=" << diagnostic.callback_sound_serial
                    << " psq_identity=" << (diagnostic.callback_sound_identity_established ? 1 : 0)
                    << " psq_identity_attempted=" << (diagnostic.sound_identity_read_attempted ? 1 : 0)
                    << " psq_identity_read=" << (diagnostic.sound_identity_read_succeeded ? 1 : 0)
                    << " psq_live=" << (diagnostic.sound_live_capture_succeeded ? 1 : 0)
                    << " psq_index_valid=" << (diagnostic.sound_index_valid ? 1 : 0)
                    << " psq_serial_valid=" << (diagnostic.sound_serial_valid ? 1 : 0)
                    << " psq_owner_attempted=" << (diagnostic.owner_read_attempted ? 1 : 0)
                    << " psq_owner_read=" << (diagnostic.owner_read_succeeded ? 1 : 0)
                    << " psq_owner=0x" << std::hex << diagnostic.owner_token
                    << std::dec
                    << " psq_route=" << (diagnostic.route_matches ? 1 : 0)
                    << " psq_setup=" << (diagnostic.setup_matches ? 1 : 0)
                    << " psq_sidecar=" << (diagnostic.sidecar_matches ? 1 : 0)
                    << " psq_build=" << (diagnostic.supported_build ? 1 : 0)
                    << " psq_lookup_sig=" << (diagnostic.lookup_signature_valid ? 1 : 0)
                    << " psq_release_sig=" << (diagnostic.release_signature_valid ? 1 : 0)
                    << " psq_entry_failure=" << static_cast<unsigned>(diagnostic.entry_first_failure)
                    << " psq_preflight_reached=" << (diagnostic.preflight_reached ? 1 : 0)
                    << " psq_preflight_decision=" << static_cast<unsigned>(preflight.decision)
                    << " psq_preflight_failure=" << static_cast<unsigned>(preflight.first_failure)
                    << " psq_owner_nonzero=" << (preflight.owner_token_nonzero ? 1 : 0)
                    << " psq_owner_decode_attempted=" << (preflight.owner_token_decode_attempted ? 1 : 0)
                    << " psq_owner_decoded=" << (preflight.owner_token_decoded ? 1 : 0)
                    << " psq_owner_type1=" << (preflight.owner_token_type1 ? 1 : 0)
                    << " psq_preflight_rebase=" << (preflight.canonical_rebase_attempted ? 1 : 0)
                    << " psq_preflight_rebase_failure="
                    << static_cast<unsigned>(preflight.canonical_rebase_first_failure)
                    << " psq_preflight_rebase_mask=0x" << std::hex
                    << rebase_mask(preflight.canonical_rebase_facts)
                    << " psq_stale=0x" << preflight.stale_canonical.encode()
                    << " psq_candidate=0x" << preflight.candidate_canonical.encode()
                    << " psq_prior_custom=0x" << preflight.prior_custom.encode()
                    << std::dec
                    << " psq_lookup_reached=" << (diagnostic.bank_lookup_reached ? 1 : 0)
                    << " psq_kind=" << diagnostic.queried_canonical_kind
                    << " psq_revalidation_reached=" << (diagnostic.revalidation_reached ? 1 : 0)
                    << " psq_revalidation_failure="
                    << static_cast<unsigned>(diagnostic.revalidation_first_failure)
                    << " psq_revalidation_mask=0x" << std::hex
                    << ((diagnostic.revalidation.sound_identity_unchanged ? 1u << 0 : 0u)
                        | (diagnostic.revalidation.owner_unchanged ? 1u << 1 : 0u)
                        | (diagnostic.revalidation.registry_generation_unchanged ? 1u << 2 : 0u)
                        | (diagnostic.revalidation.route_generation_unchanged ? 1u << 3 : 0u)
                        | (diagnostic.revalidation.lease_unchanged ? 1u << 4 : 0u)
                        | (diagnostic.revalidation.song_unchanged ? 1u << 5 : 0u)
                        | (diagnostic.revalidation.controller_unchanged ? 1u << 6 : 0u)
                        | (diagnostic.revalidation.phase_armed ? 1u << 7 : 0u)
                        | (diagnostic.revalidation.setup_token_unchanged ? 1u << 8 : 0u)
                        | (diagnostic.revalidation.sidecar_unchanged ? 1u << 9 : 0u))
                    << std::dec
                    << " psq_commit_reached=" << (diagnostic.commit_reached ? 1 : 0)
                    << " psq_commit_decision=" << static_cast<unsigned>(commit.decision)
                    << " psq_commit_failure=" << static_cast<unsigned>(commit.first_failure)
                    << " psq_commit_rebase=" << (commit.canonical_rebase_checked ? 1 : 0)
                    << " psq_commit_rebase_failure="
                    << static_cast<unsigned>(commit.canonical_rebase_first_failure)
                    << " psq_commit_rebase_mask=0x" << std::hex
                    << rebase_mask(commit.canonical_rebase_facts) << std::dec;
            }
            core::log(core::LogLevel::Info, out.str());
        }
    } catch (...) {
    }
}

CustomContextToken custom_context_token(
    const uint64_t registry_generation, const AudioRouteState& route)
{
    CustomContextToken token;
    token.registry_generation = registry_generation;
    token.route_generation = route.generation;
    token.lease_generation = route.lease_identity.generation;
    token.song_key = route.lease_identity.song_key;
    token.controller = route.controller;
    token.slot = route.owned_slot;
    token.bgm = route.owned_bgm;
    token.sound = route.owned_sound ? route.owned_sound : route.sound;
    token.request_handle = route.owned_request_handle;
    return token;
}

bool token_matches_route(const CustomContextToken& token, const AudioRouteState& route)
{
    return token.valid()
        && token.route_generation == route.generation
        && token.lease_generation == route.lease_identity.generation
        && token.song_key == route.lease_identity.song_key;
}

void invalidate_unpublished_audio_setup_locked(
    AudioRouteTransitionReason reason, bool record_transition = true,
    const PlaySetupQualificationDiagnostic* const play_setup_qualification = nullptr)
{
    const AudioRouteTransitionProjection old_state = record_transition
        ? capture_audio_route_transition_projection_locked()
        : AudioRouteTransitionProjection{};
    if (g_unpublished_audio_setup) {
        (void)registry().release_selection_guard(g_unpublished_audio_setup.token);
    }
    g_unpublished_audio_setup.invalidate();
    g_audio_route_state.private_setup_sound_identity = {};
    if (record_transition) {
        enqueue_audio_route_transition_locked(
            reason, AudioRouteTransitionKind::SetupInvalidation, old_state,
            play_setup_qualification);
    }
}

void clear_unpublished_audio_setup_locked(
    const AudioRouteLeaseIdentity& identity, AudioRouteTransitionReason reason,
    bool record_transition = true,
    const PlaySetupQualificationDiagnostic* const play_setup_qualification = nullptr)
{
    if (g_unpublished_audio_setup
        && g_unpublished_audio_setup.token.lease_generation == identity.generation
        && g_unpublished_audio_setup.token.song_key == identity.song_key) {
        invalidate_unpublished_audio_setup_locked(
            reason, record_transition, play_setup_qualification);
    }
}

void clear_unpublished_audio_setup(
    const AudioRouteLeaseIdentity& identity, AudioRouteTransitionReason reason,
    const PlaySetupQualificationDiagnostic* const play_setup_qualification = nullptr)
{
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    clear_unpublished_audio_setup_locked(
        identity, reason, true, play_setup_qualification);
}

void clear_any_unpublished_audio_setup(AudioRouteTransitionReason reason)
{
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    invalidate_unpublished_audio_setup_locked(reason);
}

bool promote_unpublished_audio_setup(
    const AudioRouteLeaseIdentity& identity, const AudioArmPublicationProof proof,
    const GuardedPlaySetupClaimObservation* guarded_observation = nullptr)
{
    if (!audio_arm_proof_allows_playback(proof)) {
        clear_unpublished_audio_setup(
            identity, AudioRouteTransitionReason::SetupProofRejected);
        return false;
    }
    UnpublishedAudioSetupContext setup;
    CustomContextToken current_route_token;
    UObjectLiveHandle current_route_sound_handle;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        current_route_token = custom_context_token(
            g_unpublished_audio_setup.selection.generation, g_audio_route_state);
        const bool guarded_play_setup_claim =
            proof == AudioArmPublicationProof::PlaySetupClaimed
            && g_unpublished_audio_setup.controller_stage
                == PrivateControllerSetupStage::StopObserved;
        const bool controller_claim_stage_valid = proof != AudioArmPublicationProof::ControllerRebuildClaimed
            || g_unpublished_audio_setup.controller_stage == PrivateControllerSetupStage::Claimed
            || g_unpublished_audio_setup.controller_stage == PrivateControllerSetupStage::AwaitingStop;
        if (!g_unpublished_audio_setup
            || g_unpublished_audio_setup.token.lease_generation != identity.generation
            || g_unpublished_audio_setup.token.song_key != identity.song_key
            || !g_unpublished_audio_setup.token.same_lease(current_route_token)
            || g_audio_route_state.phase == AudioRoutePhase::Idle
            || g_audio_route_state.desired_song_id != g_unpublished_audio_setup.selection.song->id
            || !controller_claim_stage_valid
            || (guarded_play_setup_claim
                && g_unpublished_audio_setup.token != current_route_token)
            || g_audio_route_disabled.load(std::memory_order_acquire)) {
            clear_unpublished_audio_setup_locked(
                identity, AudioRouteTransitionReason::SetupRouteMismatch);
            return false;
        }
        g_unpublished_audio_setup.token = current_route_token;
        setup = g_unpublished_audio_setup;
        current_route_sound_handle = g_audio_route_state.owned_sound_identity.live;
    }
    const AudioArmPublicationPath publication_path = classify_audio_arm_publication(
        proof, setup.controller_stage);
    const bool published = publication_path == AudioArmPublicationPath::PrivateControllerClaim
        ? publish_private_controller_claim_if_proven(registry(), setup)
        : publication_path == AudioArmPublicationPath::GuardedPlaySetupClaim
            ? guarded_observation
                && publish_guarded_play_setup_claim_if_proven(
                    registry(), setup, current_route_token, proof,
                    *guarded_observation, current_route_sound_handle)
        : publication_path == AudioArmPublicationPath::Generic
            && publish_audio_arm_if_proven(registry(), setup.selection, setup.token, proof);
    if (!published) {
        clear_unpublished_audio_setup(
            identity, AudioRouteTransitionReason::SetupPublicationFailed);
        return false;
    }
    clear_unpublished_audio_setup(
        identity, AudioRouteTransitionReason::SetupPublished);
    return true;
}

bool revoke_playback_snapshot(const PlaybackSnapshot& playback)
{
    if (!playback.song) return false;
    const bool revoked = registry().revoke_playback(playback.token);
    if (!invalidate_scoreinfo_playback(playback.token)) {
        core::log(core::LogLevel::Error,
            "[audio_sead] playback_revoke scoreinfo_restore=failed");
    }
    return revoked;
}

bool revoke_registry_playback(const AudioRouteState& route)
{
    clear_unpublished_audio_setup(
        route.lease_identity, AudioRouteTransitionReason::SetupPlaybackRevoked);
    const PlaybackSnapshot playback = registry().playback_snapshot();
    return playback.song && token_matches_route(playback.token, route)
        && revoke_playback_snapshot(playback);
}

void retire_registry_cleanup(const AudioRouteLeaseIdentity& identity)
{
    const CleanupLease cleanup = registry().cleanup_lease();
    if (cleanup.song && cleanup.token.lease_generation == identity.generation
        && cleanup.token.song_key == identity.song_key) {
        (void)registry().retire_cleanup_lease(cleanup.token);
    }
}

bool abandon_unmodified_custom_audio_arm(
    const PlaybackSnapshot& playback,
    const UnpublishedAudioSetupContext& setup,
    const bool publicly_exposed,
    const PlaySetupQualificationDiagnostic& play_setup_qualification)
{
    const AudioRouteLeaseIdentity lease_identity{
        setup.token.lease_generation, setup.token.song_key};
    const bool registry_released = publicly_exposed
        ? revoke_playback_snapshot(playback)
        : (clear_unpublished_audio_setup(
               lease_identity, AudioRouteTransitionReason::AbandonUnmodifiedArm,
               &play_setup_qualification),
            true);
    if (!registry_released) return false;

    AudioRouteCleanupResult cleanup;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_audio_route_state.phase != AudioRoutePhase::Armed
            || g_audio_route_state.generation != setup.token.route_generation
            || !(g_audio_route_state.lease_identity == lease_identity)
            || g_audio_route_state.custom_resource_owned
            || g_audio_route_state.owned_request_handle != 0
            || !g_pending_play_setup_patch.patches.empty()) {
            return false;
        }
        cleanup = g_frozen_profile_lease.transition(
            AudioRouteCleanupEvent::VerifiedNoRoute,
            lease_identity);
        if (!cleanup.clear_route_metadata) return false;
        AudioRouteTransitionRecorder route_transition_record(
            AudioRouteTransitionReason::AbandonUnmodifiedArm,
            AudioRouteTransitionKind::Combined,
            &play_setup_qualification);
        g_onmemory_bank_lifecycle.cancel_qualification(
            setup.token.route_generation);
        g_audio_route_state = {};
        g_unpublished_audio_setup = {};
    }
    if (cleanup.thaw_profile) registry().clear_frozen_profile();
    retire_registry_cleanup(lease_identity);
    return cleanup.released();
}

class ActiveNativePatchScope {
public:
    ActiveNativePatchScope()
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (!g_frozen_profile_lease.active()) {
            const std::string& song_id = g_audio_route_state.desired_song_id;
            identity_ = {++g_next_audio_route_generation,
                audio_route_song_key(song_id.empty() ? "retained-native-patch" : song_id)};
            g_audio_route_state.lease_identity = identity_;
            g_frozen_profile_lease.acquire(identity_);
        } else {
            identity_ = g_frozen_profile_lease.identity();
            g_audio_route_state.lease_identity = identity_;
        }
        g_audio_route_state.list_cleanup_pending = true;
        (void)g_active_patch_journal.begin(g_frozen_profile_lease, identity_);
    }

    ~ActiveNativePatchScope()
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_active_patch_journal.finish();
    }

private:
    AudioRouteLeaseIdentity identity_{};
};

bool native_audio_route_owned_locked()
{
    return g_audio_route_state.custom_resource_owned
        && g_audio_route_state.controller
        && g_audio_route_state.owned_slot
        && g_audio_route_state.owned_bgm
        && g_audio_route_state.owned_sound
        && AudioBgmRequestHandle{
            g_audio_route_state.owned_request_handle}.valid_bgm_request();
}

void clear_owned_audio_route_locked()
{
    g_audio_route_state.custom_resource_owned = false;
    g_audio_route_state.aggregate_awaiting_transition = false;
    g_audio_route_state.owned_slot = nullptr;
    g_audio_route_state.owned_bgm = nullptr;
    g_audio_route_state.owned_sound = nullptr;
    g_audio_route_state.owned_sound_identity = {};
    g_audio_route_state.owned_request_handle = 0;
    g_audio_route_state.reusable_sound = nullptr;
    g_audio_route_state.reusable_sound_identity = {};
    g_audio_route_state.reusable_slot = nullptr;
    g_audio_route_state.reusable_bgm = nullptr;
}
SlotSetupProfile g_slot_setup_profile;
NativePlaySetupProfile g_native_play_setup_profile;
PianoAudioRequestProfile g_piano_audio_request_profile;
struct PianoAudioOwnerSnapshot {
    uint8_t field08 = 0;
    uint64_t field0c = 0;
    uint64_t field14 = 0;
    uint32_t field4c = 0;
    uint64_t field50 = 0;
    uint32_t field58 = 0;
    uint32_t field5c = 0;
    uint8_t field60 = 0;
    uint32_t field64 = 0;
    uint8_t field68 = 0;
    uint64_t field74 = 0;
    std::array<uint8_t, 8> fields7c_to_83{};
    uint8_t field788 = 0;
    uint32_t field78c = 0;
};
struct MabfModeObservation {
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint8_t slot_state = 0;
    uint32_t requested_mode = 0;
    uint64_t mode_key = 0;
    bool valid = false;
};
void* g_piano_audio_owner = nullptr;
PianoAudioOwnerSnapshot g_piano_audio_owner_snapshot;
bool g_piano_audio_owner_snapshot_valid = false;
MabfModeObservation g_mabf_mode_observation;
std::atomic_bool g_diagnostic_playback_clock_active{false};
std::atomic_uint64_t g_diagnostic_playback_elapsed_us{0};
std::atomic_uint32_t g_diagnostic_playback_log_bucket{UINT32_MAX};
std::atomic_uint32_t g_completion_memory_log_bucket{UINT32_MAX};
thread_local void* g_piano_audio_owner_tick = nullptr;
thread_local uint64_t g_piano_audio_owner_tick_nonce_counter = 0;
thread_local uint64_t g_piano_audio_owner_tick_nonce = 0;
thread_local bool g_piano_audio_owner_custom_playsetup = false;

struct PauseResumeBankSession {
    PauseResumeBankPhase phase = PauseResumeBankPhase::Idle;
    uint64_t session_epoch = 0;
    uint64_t cycle_epoch = 0;
    OnMemoryBankDetachedRecord detached;
    uint64_t lifecycle_state_epoch = 0;
    SelectionSnapshot selection;
    OnMemoryBankRetirementFacts retirement_facts;
    AudioRouteLeaseIdentity lease_identity;
    void* owner_tick = nullptr;
    uint64_t owner_tick_nonce = 0;
    void* controller = nullptr;
    UObjectIdentity controller_identity;
    void* slot = nullptr;
    void* bgm = nullptr;
    uint64_t request_handle = 0;
    void* backing = nullptr;
    bool backing_observed = false;
    AudioFieldPatch owner_patch;
    AudioStopRetirementState retirement_monitor;
    uint64_t retirement_epoch = 0;
    OnMemoryBankRetiredBackingEvidence retired_backing;
    PauseResumeReleaseProbeIdentity pending_release_probe;
    OnMemoryBankRetiredBackingEvidence pending_release_evidence;
    bool exit_requested = false;
    bool retirement_mismatch_recorded = false;
};
PauseResumeBankSession g_pause_resume_bank;

// Fixed-capacity, observation-only playback borrower history.  Records survive
// route invalidation so list-return and shutdown ordering can be correlated.
#include "game/audio_borrower_state.inc"

static_assert(std::is_trivially_copyable_v<AudioFieldPatch>);
static_assert(std::is_nothrow_copy_assignable_v<AudioFieldPatch>);

enum class AggregateRollbackSlotState : uint8_t {
    Free,
    Writing,
    Armed,
    Retained,
    Flushing,
};

enum class AggregateRollbackAuthorityKind : uint8_t {
    None,
    CustomSet,
    CustomPlay,
};

enum class AggregateRollbackRequestProof : uint8_t {
    None,
    PreSet,
    ExactPostSet,
};

struct AggregateRollbackAuthority final {
    AggregateRollbackAuthorityKind kind = AggregateRollbackAuthorityKind::None;
    void* field_object = nullptr;
    void* sound = nullptr;
    UObjectLiveHandle field_identity{};
    void* lifecycle_sound = nullptr;
    UObjectLiveHandle lifecycle_sound_identity{};
    void* controller = nullptr;
    ControllerIdentityProof controller_proof{};
    void* slot = nullptr;
    void* bgm = nullptr;
    uint64_t ordinal = 0;
    uint64_t record_version = 0;
    uint64_t collection_version = 0;
    uint64_t operation_nonce = 0;
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease{};
    uint64_t lifecycle_state_epoch = 0;
    uint64_t token_ordinal = 0;
    uint64_t canonical_token = 0;
    uint64_t custom_token = 0;
    void* owner = nullptr;
    uint32_t owner_thread = 0;
    uint64_t owner_command = 0;
    uint64_t request = 0;
    AggregateRollbackRequestProof request_proof =
        AggregateRollbackRequestProof::None;
    uint8_t request_state = 0;
    uint64_t expected_value = 0;
    uint64_t candidate_value = 0;
};
static_assert(std::is_trivially_copyable_v<AggregateRollbackAuthority>);
static_assert(std::is_nothrow_copy_assignable_v<AggregateRollbackAuthority>);

struct AggregateRollbackSlot final {
    std::atomic<AggregateRollbackSlotState> state{
        AggregateRollbackSlotState::Free};
    AudioFieldPatch patch{};
    AggregateRollbackAuthority authority{};
    std::atomic_uint64_t generation{0};
};

std::array<AggregateRollbackSlot, 8> g_bgm_aggregate_rollback_slots{};

struct AggregatePatchRollback final {
    AudioFieldPatch patch{};
    AggregateRollbackAuthority authority{};
    int32_t slot = -1;
    uint64_t generation = 0;
    bool active = false;
};

struct AggregateDurableRollback final {
    AudioFieldPatch patch{};
    AggregateRollbackAuthority authority{};
    uint64_t generation = 0;
};
static_assert(std::is_trivially_copyable_v<AggregateDurableRollback>);

std::mutex g_bgm_aggregate_durable_rollback_mutex;
std::vector<AggregateDurableRollback> g_bgm_aggregate_durable_rollbacks;
std::atomic_bool g_bgm_aggregate_retained_rollback{false};

enum class AggregatePatchCleanupResult : uint8_t {
    None,
    Restored,
    NativeOverwrite,
    Retained,
};

struct BgmPlaybackSetObservationCandidate final {
    bool active = false;
    uint64_t ordinal = 0;
    uint64_t version = 0;
    uint64_t collection_version = 0;
    uint64_t route_generation = 0;
    uint64_t token_epoch = 0;
    uint64_t lifecycle_state_epoch = 0;
    uint64_t boundary_nonce = 0;
    uint64_t operation_predecessor_nonce = 0;
    uint64_t owner_command = 0;
    uint64_t canonical_token = 0;
    uint64_t custom_token = 0;
    AudioRouteLeaseIdentity lease{};
    void* owner = nullptr;
    bool from_canonical_stop_boundary = false;
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    void* requested_sound = nullptr;
    UObjectLiveHandle requested_sound_identity{};
};
thread_local BgmPlaybackSetObservationCandidate
    g_bgm_playback_set_observation_candidate{};
thread_local BgmPlaybackPreparationLogProposal
    g_bgm_playback_set_preparation_log{};
thread_local bool g_bgm_aggregate_set_owner_rebound = false;
thread_local bool g_bgm_aggregate_play_owner_rebound = false;
uint64_t g_pause_resume_bank_session_epoch = 0;
uint64_t g_pause_resume_bank_retirement_epoch = 0;
uint64_t g_pause_resume_bank_release_probe_epoch = 0;
std::atomic_uint32_t g_pause_resume_bank_logs{0};

enum class PauseResumeBankMarkerStatus : uint8_t {
    SuspendedReady,
    SetRebound,
    PlayResumed,
    CanonicalRestored,
    ExitPending,
    ReleaseRequested,
    ReleaseComplete,
    RetirementMismatch,
    Failed,
};

struct PauseResumeBankMarker {
    PauseResumeBankMarkerStatus status = PauseResumeBankMarkerStatus::Failed;
    const char* reason = "none";
    uint64_t session_ordinal = 0;
    uint64_t cycle_ordinal = 0;
    bool eligible = false;
};

struct PauseResumeBankMarkerBatch {
    PauseResumeBankMarker records[8]{};
    size_t count = 0;
};

thread_local PauseResumeBankMarkerBatch* g_pause_resume_bank_marker_batch = nullptr;

struct PauseResumeBankHoldProbe {
    bool eligible = false;
    OnMemoryBankRetirementFacts facts;
    OnMemoryBankDetachedRecord detached;
    uint64_t lifecycle_state_epoch = 0;
    SelectionSnapshot selection;
    AudioRouteLeaseIdentity lease_identity;
    void* controller = nullptr;
    UObjectIdentity controller_identity;
    bool exit_requested = false;
};

struct PauseResumeReleaseAuthorityProbe {
    bool eligible = false;
    PauseResumeReleaseProbeIdentity identity;
    OnMemoryBankRetiredBackingEvidence evidence;
    PauseResumeBankSession session;
    OnMemoryBankRetirementFacts facts;
    void* current_sound = nullptr;
};

using AudioCallbackScope = AudioProductionCallbackScope;
using AudioShutdownScope = AudioProductionShutdownScope;

void retain_failed_patch_journal(const std::vector<AudioFieldPatch>& patches)
{
    if (patches.empty()) return;
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    g_failed_patch_journal.insert(g_failed_patch_journal.end(), patches.begin(), patches.end());
    g_audio_route_state.list_cleanup_pending = true;
    (void)g_frozen_profile_lease.transition(
        AudioRouteCleanupEvent::NativeClearUnverified,
        g_audio_route_state.lease_identity);
}

using PlaySetupOriginalScope = AudioProductionPlaySetupScope;

class NativePlaySetupReplayScope {
public:
    NativePlaySetupReplayScope() { ++g_native_play_setup_replay_depth; }
    ~NativePlaySetupReplayScope() { --g_native_play_setup_replay_depth; }
};

const char* audio_route_phase_name(AudioRoutePhase phase)
{
    switch (phase) {
    case AudioRoutePhase::Idle: return "idle";
    case AudioRoutePhase::Armed: return "armed";
    case AudioRoutePhase::Rebuilding: return "rebuilding";
    case AudioRoutePhase::PatchedPlaySetup: return "patched_playsetup";
    case AudioRoutePhase::Playing: return "playing";
    case AudioRoutePhase::CanonicalRelinquishmentPending:
        return "canonical_relinquishment_pending";
    case AudioRoutePhase::CanonicalRelinquished:
        return "canonical_relinquished";
    }
    return "unknown";
}

bool signature_matches(HMODULE exe_module, const RvaSignatureSpec* spec)
{
    if (!exe_module || !spec || spec->rva == 0 || spec->expected_prologue.empty()) {
        return false;
    }
    std::vector<uint8_t> actual(spec->expected_prologue.size());
    const auto* target = reinterpret_cast<const uint8_t*>(exe_module) + spec->rva;
    return core::safe_copy_bytes(target, actual.data(), actual.size())
        && actual == spec->expected_prologue;
}

void append_signature_status(std::ostringstream& out, const char* name,
    HMODULE exe_module, std::string_view spec_name)
{
    const RvaSignatureSpec* spec = find_rva_signature(spec_name);
    if (!spec) {
        out << ' ' << name << "_spec=missing " << name << "_sig=mismatch";
        return;
    }
    out << ' ' << name << "_rva=0x" << std::hex << spec->rva << std::dec
        << ' ' << name << "_sig=" << (signature_matches(exe_module, spec) ? "ok" : "mismatch");
}

bool construct_fname(FNameCtorFn ctor, const wchar_t* text, int32_t find_type, FNameValue& out)
{
    if (!ctor || !text || !*text) {
        return false;
    }
    __try {
        out = {};
        ctor(&out, text, find_type);
        return out.comparison_id != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
        return false;
    }
}

bool descriptor_build_seh(DescriptorBuildFn fn, void* sound, uint64_t (&descriptor)[3])
{
    if (!fn || !sound) {
        return false;
    }
    __try {
        descriptor[0] = descriptor[1] = descriptor[2] = 0;
        fn(descriptor, sound);
        return descriptor[0] != 0 || descriptor[1] != 0 || descriptor[2] != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        descriptor[0] = descriptor[1] = descriptor[2] = 0;
        return false;
    }
}

void* create_package_seh(CreatePackageFn fn, const wchar_t* package_name)
{
    if (!fn || !package_name) {
        return nullptr;
    }
    __try {
        return fn(package_name);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* static_construct_object_seh(StaticConstructObjectFn fn, FStaticConstructObjectParametersLocal& params)
{
    if (!fn) {
        return nullptr;
    }
    __try {
        return fn(&params);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool call_bgm_prepare_original(void* bgm)
{
    if (!g_original_bgm_prepare) {
        return false;
    }
    g_original_bgm_prepare(bgm);
    return true;
}

void observe_bgm_playback_set_result(
    void* controller, void* requested_sound, bool native_succeeded,
    BgmPlaybackDeferredLogProposal* proposal) noexcept;
class AggregateMutationReleaseWitness;
void invalidate_bgm_playback_operation(
    void* controller, bool set_operation, const char* reason,
    BgmPlaybackDeferredLogProposal* proposal = nullptr) noexcept;
void observe_bgm_playback_play_failed(void* controller,
    BgmPlaybackDeferredLogProposal* proposal = nullptr) noexcept;
void emit_bgm_playback_result_log(
    const BgmPlaybackDeferredLogProposal& proposal,
    const AggregateMutationReleaseWitness& witness) noexcept;
void emit_bgm_playback_preparation_log(
    const BgmPlaybackPreparationLogProposal& proposal,
    BgmPlaybackPreparationEmissionPhase phase,
    const AggregateMutationReleaseWitness& witness) noexcept;
void emit_bgm_aggregate_owner_patch_log(
    const BgmAggregateOwnerPatchLogProposal& proposal,
    const AggregateMutationReleaseWitness& witness) noexcept;
bool prepare_bgm_aggregate_owner_patch(
    void* controller, void* sound, bool play,
    AggregatePatchRollback& rollback,
    bool* owner_patch_candidate_present = nullptr,
    BgmAggregateOwnerPatchLogProposal* log_proposal = nullptr) noexcept;
void log_bgm_aggregate_mutation(
    const char* status, const char* stage, uint64_t ordinal,
    uint64_t version, uint64_t predecessor) noexcept;
void log_bgm_exit_request(const char* status, uint64_t ordinal,
    uint64_t request, uint64_t generation,
    const char* reason = nullptr) noexcept;
bool read_uobject_identity(void* object, UObjectIdentity& out);
bool read_controller_bgm_chain(void* controller, void*& slot, void*& bgm);
bool read_controller_audio_chain(void* controller, void*& slot, void*& bgm,
    void*& sound, uint64_t& request_handle, uint8_t& state);
bool controller_identity_proof_matches_live(
    const ControllerIdentityProof& expected, void* controller);
bool aggregate_rollback_ownership_clear() noexcept;
AggregatePatchCleanupResult cleanup_aggregate_patch_noexcept(
    AggregatePatchRollback& rollback, uint64_t& current_out) noexcept;
bool strengthen_custom_set_rollback_request_noexcept(
    AggregatePatchRollback& rollback) noexcept;
void flush_aggregate_rollback_slots_best_effort() noexcept;
struct AggregateNativeCleanup final {
    AggregatePatchRollback* owner = nullptr;
    AggregatePatchCleanupResult owner_result =
        AggregatePatchCleanupResult::None;
    uint64_t owner_current = 0;

    void operator()() noexcept
    {
        if (owner) owner_result = cleanup_aggregate_patch_noexcept(
            *owner, owner_current);
    }
};
static_assert(noexcept(std::declval<AggregateNativeCleanup&>()()));

struct BgmPlaybackNativeSetOutcome final {
    bool native_succeeded = false;
    bool native_callback_entered = false;
    bool exact = false;
    BgmPlaybackProtectedNativeFault fault =
        BgmPlaybackProtectedNativeFault::NativeFailure;
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    UObjectLiveHandle sound_identity{};
    uint64_t request = 0;
    uint8_t state = 0;
};
static_assert(std::is_trivially_copyable_v<BgmPlaybackNativeSetOutcome>);

struct BgmPlaybackPendingPlayAuthority final {
    bool candidate_present = false;
    bool authority_exact = false;
    bool known_request_sound_exact = false;
    bool custom = false;
    uint64_t ordinal = 0;
    uint64_t record_version = 0;
    uint64_t collection_version = 0;
    uint64_t set_nonce = 0;
    uint64_t expected_play_nonce = 0;
    void* controller = nullptr;
    ControllerIdentityProof controller_proof{};
    void* slot = nullptr;
    void* bgm = nullptr;
    uint64_t request = 0;
    void* sound = nullptr;
    UObjectLiveHandle sound_identity{};
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease{};
    uint64_t lifecycle_state_epoch = 0;
    uint64_t token_ordinal = 0;
    uint64_t canonical_token = 0;
    uint64_t custom_token = 0;
    void* owner = nullptr;
    uint32_t owner_thread = 0;
    uint64_t intended_command = 0;
    uint64_t canonical_command = 0;
    uint64_t song_identity = 0;
};
static_assert(std::is_trivially_copyable_v<BgmPlaybackPendingPlayAuthority>);
static_assert(std::is_nothrow_copy_assignable_v<BgmPlaybackPendingPlayAuthority>);

struct BgmPlaybackPendingPlayOwnershipDiagnostic final {
    bool proposed = false;
    bool unknown_publication_latched = false;
    BgmPlaybackPendingPlayCandidateSource source =
        BgmPlaybackPendingPlayCandidateSource::None;
    uint64_t ordinal = 0;
    uint64_t request = 0;
};
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackPendingPlayOwnershipDiagnostic>);

struct BgmPlaybackUnpublishedPlayDiagnostic final {
    bool proposed = false;
    bool authority_candidate_present = false;
    bool authority_exact = false;
    bool publication_authority_exact = false;
    BgmPlaybackPendingPlayCandidateSource source =
        BgmPlaybackPendingPlayCandidateSource::None;
    BgmPlaybackPlayFailure first_failure = BgmPlaybackPlayFailure::PendingSet;
    uint64_t ordinal = 0;
    uint64_t expected_record_version = 0;
    uint64_t current_record_version = 0;
    uint64_t expected_collection_version = 0;
    uint64_t current_collection_version = 0;
    uint64_t expected_route = 0;
    uint64_t current_route = 0;
    uint64_t expected_lifecycle = 0;
    uint64_t current_lifecycle = 0;
    uint64_t expected_request = 0;
    uint64_t current_request = 0;
    uint64_t expected_command = 0;
    uint64_t captured_owner_command = 0;
    bool captured_owner_state_read = false;
    uint8_t captured_owner_state = 0xff;
    uint64_t expected_nonce = 0;
    uint64_t current_nonce = 0;
    bool pending_set_exact = false;
    bool controller_chain_exact = false;
    bool owner_thread_exact = false;
    bool command_exact = false;
    bool route_exact = false;
    bool lease_exact = false;
    bool lifecycle_exact = false;
    bool token_ordinal_exact = false;
    bool token_values_exact = false;
    bool request_sound_identity_exact = false;
    bool request_handle_exact = false;
    bool state4_exact = false;
    bool nonce_exact = false;
};
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackUnpublishedPlayDiagnostic>);

enum class ListReturnClearAuthorityBlocker : uint8_t {
    None,
    CurrentController,
    ControllerIdentity,
    ControllerChain,
    SoundRead,
    RequestRead,
    StateRead,
    SoundMissing,
    RequestMissing,
    StateNotPlaying,
    SoundIdentity,
    CustomResourceUnowned,
    SlotMismatch,
    BgmMismatch,
    SoundMismatch,
    RequestMismatch,
    OwnedSoundIdentity,
    AggregateAuthority,
    AggregateVersionDrift,
    PostClearValidation,
};

enum class ListReturnClearAuthoritySource : uint8_t {
    None,
    LegacyRoute,
    AggregateCanonical,
    Both,
    RouteRestore,
};

struct ListReturnClearAuthorityDiagnostic final {
    bool proposed = false;
    bool clear_attempted = false;
    bool route_owned = false;
    bool aggregate_candidate_present = false;
    bool aggregate_canonical_exit_exact = false;
    bool aggregate_pre_call_revalidated = false;
    bool aggregate_clear_committed = false;
    bool canonical_substrate_relinquished = false;
    bool canonical_substrate_pending_published = false;
    bool canonical_substrate_route_committed = false;
    bool canonical_substrate_borrower_finalized = false;
    bool canonical_substrate_partial = false;
    bool post_clear_exact = false;
    bool synchronous_absence_observer_ran = false;
    ListReturnClearAuthoritySource source =
        ListReturnClearAuthoritySource::None;
    ListReturnClearAuthorityBlocker blocker =
        ListReturnClearAuthorityBlocker::None;
    BgmPlaybackCanonicalSubstrateRelinquishmentState
        canonical_substrate_state =
            BgmPlaybackCanonicalSubstrateRelinquishmentState::None;
    uint64_t canonical_substrate_transaction_generation = 0;

    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity route_lease{};
    void* route_controller = nullptr;
    bool route_controller_proof_valid = false;
    void* route_slot = nullptr;
    void* route_bgm = nullptr;
    void* route_sound = nullptr;
    UObjectLiveHandle route_sound_identity{};
    uint64_t route_request = 0;
    bool route_custom_resource_owned = false;
    bool route_list_cleanup_pending = false;
    bool route_native_clear_verified = false;

    void* live_controller = nullptr;
    void* live_slot = nullptr;
    void* live_bgm = nullptr;
    void* live_sound = nullptr;
    UObjectLiveHandle live_sound_identity{};
    uint64_t live_request = 0;
    uint8_t live_state = 0xff;
    bool live_controller_exact = false;
    bool live_controller_identity_exact = false;
    bool live_chain_read = false;
    bool live_sound_read = false;
    bool live_request_read = false;
    bool live_state_read = false;
    bool live_sound_identity_read = false;
    bool live_slot_exact = false;
    bool live_bgm_exact = false;
    bool live_sound_exact = false;
    bool live_request_exact = false;
    bool live_sound_identity_exact = false;

    uint64_t aggregate_ordinal = 0;
    uint64_t aggregate_record_version = 0;
    uint64_t aggregate_collection_version = 0;
    uint64_t aggregate_list_exit_epoch = 0;
    void* aggregate_controller = nullptr;
    bool aggregate_controller_proof_matches_route = false;
    void* aggregate_slot = nullptr;
    void* aggregate_bgm = nullptr;
    void* aggregate_canonical_sound = nullptr;
    UObjectLiveHandle aggregate_canonical_sound_identity{};
    uint64_t aggregate_new_handle = 0;
    uint64_t aggregate_canonical_handle = 0;
    uint64_t aggregate_route_generation = 0;
    AudioRouteLeaseIdentity aggregate_lease{};
    uint64_t aggregate_lifecycle_state_epoch = 0;
    uint64_t aggregate_token_epoch = 0;
    uint64_t aggregate_canonical_token = 0;
    uint64_t aggregate_custom_token = 0;
    bool aggregate_list_exit = false;
    bool aggregate_current_active = false;
    bool aggregate_failed = false;
    bool aggregate_destination_aba = false;
    bool aggregate_old_identity_conflict = false;
    bool aggregate_lineage_identity_conflict = false;
    bool aggregate_continuity_exact = false;
    bool aggregate_transition_set_pending = false;
    bool aggregate_boundary_pending = false;
    bool aggregate_unresolved_request = false;
};
static_assert(std::is_trivially_copyable_v<
    ListReturnClearAuthorityDiagnostic>);

struct ListReturnAggregateClearAuthority final {
    bool candidate_present = false;
    uint64_t collection_version = 0;
    BgmPlaybackBorrowerRecord record{};
};
static_assert(std::is_trivially_copyable_v<
    ListReturnAggregateClearAuthority>);

struct BgmPlaybackPendingPlayOutcome final {
    bool active = false;
    bool publication_authority_exact = false;
    bool chain_read = false;
    bool sound_identity_read = false;
    bool owner_read = false;
    bool owner_state_read = false;
    bool known_request_sound_exact = false;
    uint64_t ordinal = 0;
    uint64_t record_version = 0;
    uint64_t collection_version = 0;
    uint64_t set_nonce = 0;
    uint64_t request = 0;
    bool custom = false;
    void* sound = nullptr;
    UObjectLiveHandle sound_identity{};
    uint64_t authorized_request = 0;
    void* authorized_sound = nullptr;
    UObjectLiveHandle authorized_sound_identity{};
    void* controller = nullptr;
    ControllerIdentityProof controller_proof{};
    void* slot = nullptr;
    void* bgm = nullptr;
    uint8_t state = 0xff;
    void* owner = nullptr;
    uint32_t owner_thread = 0;
    uint8_t owner_state = 0xff;
    uint64_t owner_command = 0;
    uint64_t route_generation = 0;
    AudioRouteLeaseIdentity lease{};
    uint64_t lifecycle_state_epoch = 0;
    uint64_t token_ordinal = 0;
    uint64_t canonical_token = 0;
    uint64_t custom_token = 0;
    uint64_t play_nonce = 0;
};
static_assert(std::is_trivially_copyable_v<BgmPlaybackPendingPlayOutcome>);

ControllerIdentityProof controller_identity_proof(
    void* controller, const UObjectIdentity& identity);
bool lookup_current_piano_audio_owner(void*& owner);
BgmPlaybackNativeSetOutcome capture_bgm_native_set_outcome_noexcept(
    void* controller, void* requested_sound, bool native_callback_entered,
    const BgmPlaybackProtectedNativeResult& native) noexcept;
BgmPlaybackPendingPlayAuthority prepare_bgm_pending_play_authority_noexcept(
    void* controller, const AggregateRollbackAuthority& rollback) noexcept;
BgmPlaybackPendingPlayOutcome capture_bgm_pending_play_outcome_noexcept(
    const BgmPlaybackPendingPlayAuthority& authority) noexcept;
BgmPlaybackPendingPlayOutcome capture_bgm_pending_play_outcome_seh(
    const BgmPlaybackPendingPlayAuthority& authority) noexcept;
bool retain_unpublished_bgm_set_outcome_noexcept(
    const BgmPlaybackSetObservationCandidate& candidate,
    const BgmPlaybackNativeSetOutcome& outcome) noexcept;
bool retain_unpublished_bgm_play_outcome_noexcept(
    void* controller, const BgmPlaybackPendingPlayOutcome& outcome) noexcept;

BgmPlaybackNativeSetCallOutcome call_bgm_slot_set_native_protected(
    BgmPlaybackNativeSetTarget target, void* controller, void* sound);
BgmPlaybackNativePlayCallOutcome call_bgm_slot_play_native_protected(
    BgmPlaybackNativePlayTarget target, void* controller);
class AggregateMutationReleaseWitness final {
public:
    AggregateMutationReleaseWitness(const AggregateMutationReleaseWitness&) = delete;
    AggregateMutationReleaseWitness& operator=(
        const AggregateMutationReleaseWitness&) = delete;
    AggregateMutationReleaseWitness(
        AggregateMutationReleaseWitness&& other) noexcept
        : released_(std::exchange(other.released_, false))
    {
    }
    AggregateMutationReleaseWitness& operator=(
        AggregateMutationReleaseWitness&& other) noexcept
    {
        if (this != &other) {
            released_ = std::exchange(other.released_, false);
        }
        return *this;
    }

    [[nodiscard]] bool released() const noexcept { return released_; }

private:
    friend struct AggregateMutationLease;
    explicit AggregateMutationReleaseWitness(const bool released) noexcept
        : released_(released) {}
    bool released_ = false;
};

void publish_bgm_aggregate_native_cleanup(
    AggregatePatchCleanupResult result,
    const AggregateMutationReleaseWitness& witness) noexcept;

struct AggregateMutationLease final {
    using Policy = BgmPlaybackAggregateMutationLease<
        BgmPlaybackAggregateMutationGate>;

    AggregateMutationLease() noexcept
        : policy(g_bgm_aggregate_mutation_gate, []() noexcept {
              return g_bgm_aggregate_exit_requested.load(
                         std::memory_order_acquire)
                  || g_bgm_aggregate_exit_pending.load(
                      std::memory_order_acquire);
          }),
          active(policy.active)
    {}

    AggregateMutationLease(const AggregateMutationLease&) = delete;
    AggregateMutationLease& operator=(const AggregateMutationLease&) = delete;

    [[nodiscard]] AggregateMutationReleaseWitness release() noexcept
    {
        return AggregateMutationReleaseWitness(policy.release());
    }

    Policy policy;
    bool& active;
};

BgmPlaybackAggregateExitOwnershipFacts snapshot_bgm_aggregate_exit_ownership(
    bool mutation_gate_drained, bool clear_empty_exit) noexcept
{
    BgmPlaybackAggregateExitOwnershipFacts facts;
    try {
        facts.unresolved_publication =
            g_bgm_aggregate_unresolved_publications.load(
                std::memory_order_acquire) != 0;
        facts.rollback_owned = !aggregate_rollback_ownership_clear();
        facts.observer_lease_in_flight = !mutation_gate_drained
            && !g_bgm_aggregate_mutation_gate.drained();
        {
            std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
            const auto state = g_bgm_aggregate_release_claim.state;
            facts.release_claim_owned =
                state == AggregateReleaseClaimState::Pending
                || state == AggregateReleaseClaimState::InFlight
                || state == AggregateReleaseClaimState::Retained;
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            facts.route_or_cleanup_owned =
                g_audio_route_state.custom_resource_owned
                || g_audio_route_state.list_cleanup_pending
                || g_frozen_profile_lease.active()
                || static_cast<bool>(g_onmemory_bank_lifecycle.active());
        }
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            for (const auto& record : g_bgm_playback_borrowers) {
                if (record.active) {
                    facts.active_or_retained_borrower = true;
                    break;
                }
            }
            if (clear_empty_exit
                && !bgm_playback_aggregate_exit_ownership_present(facts)) {
                g_bgm_aggregate_exit_requested.store(
                    false, std::memory_order_release);
                g_bgm_aggregate_exit_pending.store(
                    false, std::memory_order_release);
            }
        }
    } catch (...) {
        // An unreadable ownership snapshot is retained fail-closed.
        facts.active_or_retained_borrower = true;
    }
    return facts;
}

bool complete_empty_bgm_aggregate_exit_after_drain() noexcept
{
    bool empty = false;
    const bool reopened = g_bgm_aggregate_mutation_gate.reopen_after_drain_if(
        [&]() noexcept {
            const auto facts = snapshot_bgm_aggregate_exit_ownership(
                true, true);
            empty = !bgm_playback_aggregate_exit_ownership_present(facts);
            return empty;
        });
    return reopened && empty;
}

void log_bgm_aggregate_empty_exit() noexcept
{
    if (g_bgm_aggregate_mutation_logs.fetch_add(
            1, std::memory_order_acq_rel) >= 96) return;
    try {
        core::log(core::LogLevel::Info,
            "[audio_sead] aggregate_resume status=exit_empty_complete "
            "native_forwarding=exact_once mutation_authorized=0");
    } catch (...) {
    }
}

bool claim_bgm_aggregate_exit_diagnostic(
    uint64_t ordinal, const char* reason) noexcept
{
    if (!ordinal || !reason) return true;
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char* p =
             reinterpret_cast<const unsigned char*>(reason);
         *p; ++p) {
        hash = (hash ^ *p) * 1099511628211ull;
    }
    if (!hash) hash = 1;
    try {
        std::lock_guard lock(g_bgm_aggregate_exit_diagnostic_mutex);
        BgmAggregateExitDiagnosticKey* free_key = nullptr;
        for (auto& key : g_bgm_aggregate_exit_diagnostic_keys) {
            if (key.ordinal == ordinal && key.reason_hash == hash) {
                return false;
            }
            if (!free_key && key.ordinal == 0) free_key = &key;
        }
        if (!free_key) return false;
        free_key->ordinal = ordinal;
        free_key->reason_hash = hash;
        return true;
    } catch (...) {
        return false;
    }
}

void log_bgm_aggregate_exit_stage(const char* status, const char* reason,
    uint64_t ordinal, uint64_t old_handle, uint64_t new_handle,
    bool old_absent, bool new_absent, bool current_active) noexcept
{
    if (std::strcmp(status, "exit_release_blocked") == 0
        && !claim_bgm_aggregate_exit_diagnostic(ordinal, reason)) return;
    if (g_bgm_aggregate_exit_logs.fetch_add(
            1, std::memory_order_relaxed) >= 64) return;
    try {
        std::ostringstream out;
        out << "[audio_sead] aggregate_exit status=" << status
            << " reason=" << reason
            << " ordinal=" << ordinal
            << " old=0x" << std::hex << old_handle
            << " new=0x" << new_handle << std::dec
            << " old_absent=" << (old_absent ? 1 : 0)
            << " new_absent=" << (new_absent ? 1 : 0)
            << " current_active=" << (current_active ? 1 : 0)
            << " mutation_authorized=0";
        core::log(std::string_view(status) == "exit_pending_published"
                ? core::LogLevel::Info : core::LogLevel::Error,
            out.str());
    } catch (...) {
    }
}

void publish_bgm_aggregate_exit(bool list_exit, bool shutdown) noexcept
{
    if (!list_exit && !shutdown) return;
    // The first decision is a read-only ownership snapshot. Even an empty
    // exit is then closed/drained and rechecked so a callback cannot enter in
    // the decision-to-reopen window.
    const auto initial_ownership = snapshot_bgm_aggregate_exit_ownership(
        false, false);
    if (!bgm_playback_aggregate_exit_ownership_present(initial_ownership)
        && g_bgm_aggregate_mutation_gate.closed()
        && complete_empty_bgm_aggregate_exit_after_drain()) {
        log_bgm_aggregate_empty_exit();
        return;
    }
    g_bgm_aggregate_mutation_gate.close_and_drain([]() noexcept {
        g_bgm_aggregate_exit_requested.store(true, std::memory_order_release);
    });
    if (complete_empty_bgm_aggregate_exit_after_drain()) {
        log_bgm_aggregate_empty_exit();
        return;
    }
    struct PreservedRequestMarker {
        uint64_t ordinal = 0;
        uint64_t request = 0;
        uint64_t generation = 0;
        bool unresolved = false;
    };
    std::array<PreservedRequestMarker, 32> markers{};
    size_t marker_count = 0;
    bool applicable_record = false;
    BgmPlaybackBorrowerRecord newest_exited_record;
    bool have_newest_exited_record = false;
    {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        bool changed = false;
        for (auto& record : g_bgm_playback_borrowers) {
            if (!record.active) continue;
            applicable_record = true;
            if (!have_newest_exited_record
                || record.ordinal > newest_exited_record.ordinal) {
                newest_exited_record = record;
                have_newest_exited_record = true;
            }
            const bool pending_set = record.transition_set_pending;
            const uint64_t pending_handle = record.transition_set_handle;
            const bool pending_exact = pending_set
                && AudioBgmRequestHandle{pending_handle}.valid_bgm_request()
                && record.transition_set_sound
                && record.transition_set_sound_identity.serial_number > 0;
            if (pending_exact) {
                if (record.new_handle == 0 || record.new_handle == pending_handle) {
                    record.new_handle = pending_handle;
                    record.expected_new_sound = record.transition_set_sound;
                    record.expected_new_sound_identity =
                        record.transition_set_sound_identity;
                    record.set_observed = true;
                    record.new_absent = false;
                } else {
                    record.exit_unresolved_native_request = true;
                    record.failed = true;
                }
                if (marker_count < markers.size()) markers[marker_count++] = {
                    record.ordinal, pending_handle, pending_handle >> 32,
                    record.exit_unresolved_native_request};
            } else if (pending_set) {
                record.exit_unresolved_native_request = true;
                record.exit_unresolved_request_handle = pending_handle;
                record.exit_unresolved_request_generation = pending_handle >> 32;
                record.exit_unresolved_request_sound = record.transition_set_sound;
                record.exit_unresolved_request_sound_identity =
                    record.transition_set_sound_identity;
                if (marker_count < markers.size()) markers[marker_count++] = {
                    record.ordinal, pending_handle, pending_handle >> 32, true};
            }
            const bool record_changed = (list_exit && !record.list_exit)
                || (shutdown && !record.shutdown)
                || record.canonical_stop_boundary_active
                || !record.canonical_stop_boundary_consumed
                || record.canonical_stop_boundary_nonce != 0
                || pending_set
                || record.transition_set_nonce != 0
                || record.transition_set_handle != 0
                || record.transition_set_sound != nullptr
                || record.transition_set_sound_identity.serial_number != 0
                || record.transition_set_owner_rebound
                || record.transition_play_owner_rebound
                || !record.latest_custom_transition_consumed;
            record.list_exit = record.list_exit || list_exit;
            record.shutdown = record.shutdown || shutdown;
            record.canonical_stop_boundary_active = false;
            record.canonical_stop_boundary_consumed = true;
            record.canonical_stop_boundary_nonce = 0;
            record.transition_set_pending = false;
            record.transition_set_nonce = 0;
            record.transition_set_handle = 0;
            record.transition_set_sound = nullptr;
            record.transition_set_sound_identity = {};
            record.transition_set_owner_rebound = false;
            record.transition_play_owner_rebound = false;
            record.latest_custom_transition_consumed = true;
            if (record_changed) {
                ++record.version;
                changed = true;
            }
        }
        if (changed) ++g_bgm_playback_collection_version;
        g_bgm_aggregate_exit_pending.store(true, std::memory_order_release);
    }
    if (have_newest_exited_record) {
        log_bgm_aggregate_exit_stage("exit_pending_published",
            list_exit ? "list_return" : "shutdown",
            newest_exited_record.ordinal, newest_exited_record.old_handle,
            newest_exited_record.new_handle, newest_exited_record.old_absent,
            newest_exited_record.new_absent,
            newest_exited_record.current_active);
    }
    if (!applicable_record) {
        log_bgm_aggregate_mutation(
            "exit_orphan_ownership_retained",
            initial_ownership.rollback_owned ? "rollback"
                : initial_ownership.release_claim_owned ? "release_claim"
                : initial_ownership.unresolved_publication ? "unresolved_request"
                : "route_or_cleanup",
            0, 0, 0);
    }
    for (size_t i = 0; i < marker_count; ++i) {
        const auto& marker = markers[i];
        log_bgm_exit_request(marker.unresolved
                ? "exit_unresolved_request_retained"
                : "exit_pending_set_preserved",
            marker.ordinal, marker.request, marker.generation);
        if (!marker.unresolved) log_bgm_exit_request(
            "exit_request_resolved", marker.ordinal,
            marker.request, marker.generation);
    }
}

bool bgm_aggregate_lineage_active(void* controller) noexcept
{
    if (!controller || g_bgm_aggregate_exit_requested.load(
            std::memory_order_acquire)) return false;
    try {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        for (const auto& record : g_bgm_playback_borrowers) {
            if (bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)
                && !record.list_exit
                && !record.shutdown && record.retirement_anchored
                && record.controller == controller && record.continuity_exact) {
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

bool bgm_aggregate_lineage_exit_pending(void* controller) noexcept
{
    if (!controller || (!g_bgm_aggregate_exit_requested.load(
            std::memory_order_acquire)
        && !g_bgm_aggregate_exit_pending.load(
            std::memory_order_acquire))) return false;
    try {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        for (const auto& record : g_bgm_playback_borrowers) {
            if (record.active && (record.list_exit || record.shutdown)
                && record.controller == controller) return true;
        }
    } catch (...) {
    }
    return false;
}

BgmAggregateSetForwardResult begin_bgm_aggregate_set_forward(
    void* controller) noexcept
{
    BgmAggregateSetForwardResult result{};
    try {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        auto& route = g_audio_route_state;
        const auto& candidate = g_bgm_playback_set_observation_candidate;
        const bool exact = controller && route.controller == controller
            && route.custom_resource_owned && route.list_cleanup_pending
            && route.lease_identity.valid()
            && (!candidate.active
                || (candidate.controller == controller
                    && candidate.route_generation == route.generation
                    && candidate.lease == route.lease_identity));
        if (!exact || route.generation == UINT64_MAX) return result;
        const uint64_t route_generation_before = route.generation;
        AudioRouteTransitionRecorder route_transition_record(
            AudioRouteTransitionReason::AggregateSetForward,
            AudioRouteTransitionKind::RouteReset);
        ++route.generation;
        route.aggregate_awaiting_transition = true;
        route.phase = AudioRoutePhase::Idle;
        route.desired_song_id.clear();
        route.patched_song_id.clear();
        // Aggregate ownership deliberately preserves the custom resource,
        // frozen profile, cleanup lease, and exact controller/slot/BGM chain.
        result.route_advanced = true;
        result.log = {true, true, candidate.ordinal, candidate.version,
            candidate.operation_predecessor_nonce, route_generation_before,
            route.generation, route.lease_identity.generation,
            route.lease_identity.song_key};
        return result;
    } catch (...) {
        return result;
    }
}

void emit_bgm_aggregate_set_forward_log(
    const BgmAggregateSetForwardResult& result) noexcept
{
    if (!bgm_aggregate_set_forward_log_may_emit(result)) return;
    const auto& proposal = result.log;
    log_bgm_aggregate_mutation("aggregate_awaiting", "set_forward",
        proposal.ordinal, proposal.version, proposal.predecessor);
}

void log_bgm_exit_request(const char* status, uint64_t ordinal,
    uint64_t request, uint64_t generation, const char* reason) noexcept
{
    try {
        if (g_bgm_aggregate_mutation_logs.fetch_add(
                1, std::memory_order_relaxed) >= 96) return;
        std::ostringstream out;
        out << "[audio_sead] aggregate_resume status=" << status
            << " ordinal=" << ordinal
            << " request=0x" << std::hex << request << std::dec
            << " request_generation=" << generation
            << (reason ? " reason=" : "") << (reason ? reason : "")
            << " mutation_authorized=0";
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

BgmPlaybackNativeSetOutcome capture_bgm_native_set_outcome_noexcept(
    void* controller, void* requested_sound, bool native_callback_entered,
    const BgmPlaybackProtectedNativeResult& native) noexcept
{
    BgmPlaybackNativeSetOutcome out;
    out.native_succeeded = native.succeeded;
    out.native_callback_entered = native_callback_entered;
    out.fault = native.fault;
    out.controller = controller;
    if (!native_callback_entered || !controller || !requested_sound) return out;
    try {
        UObjectIdentity identity;
        const bool chain_exact = read_controller_audio_chain(
                controller, out.slot, out.bgm, out.sound, out.request, out.state)
            && out.sound == requested_sound;
        const bool identity_exact = chain_exact
            && read_uobject_identity(out.sound, identity)
            && identity.live.serial_number > 0
            && validate_live_uobject_handle(out.sound, identity.live);
        out.exact = bgm_playback_faulted_set_request_exact(chain_exact,
            identity_exact,
            AudioBgmRequestHandle{out.request}.valid_bgm_request(), out.state);
        if (out.exact) out.sound_identity = identity.live;
    } catch (...) {
        out.exact = false;
    }
    return out;
}

BgmPlaybackPendingPlayAuthority prepare_bgm_pending_play_authority_noexcept(
    void* controller, const AggregateRollbackAuthority& rollback) noexcept
{
    BgmPlaybackPendingPlayAuthority authority;
    if (!controller) return authority;
    try {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        const BgmPlaybackBorrowerRecord* target = nullptr;
        for (const auto& record : g_bgm_playback_borrowers) {
            if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                    record.failed, record.terminal_callback_retired)
                || record.controller != controller
                || !record.transition_set_pending) continue;
            if (!target || record.ordinal > target->ordinal) target = &record;
        }
        if (target) {
            authority = {
                true, false, false, target->transition_set_custom, target->ordinal,
                target->version, g_bgm_playback_collection_version,
                target->transition_set_nonce,
                target->transition_set_nonce + 1, target->controller,
                target->controller_proof, target->slot, target->bgm,
                target->transition_set_handle, target->transition_set_sound,
                target->transition_set_sound_identity,
                target->transition_route_generation, target->transition_lease,
                target->transition_lifecycle_state_epoch,
                target->transition_token_epoch,
                target->transition_canonical_token,
                target->transition_custom_token, target->owner,
                target->owner_thread_id, target->transition_command_key,
                target->desired_key, target->transition_lease.song_key};
            authority.known_request_sound_exact =
                AudioBgmRequestHandle{authority.request}.valid_bgm_request()
                && authority.sound
                && authority.sound_identity.serial_number > 0;
            authority.authority_exact = authority.set_nonce != 0
                && authority.set_nonce == g_bgm_playback_operation_nonce
                && g_bgm_playback_operation_nonce != UINT64_MAX
                && authority.known_request_sound_exact
                && authority.route_generation != 0 && authority.lease.valid()
                && authority.lifecycle_state_epoch != 0
                && authority.token_ordinal != 0
                && authority.canonical_token != 0
                && authority.custom_token != 0
                && authority.owner && authority.owner_thread != 0
                && authority.intended_command != 0
                && (!authority.custom
                    || bgm_playback_custom_play_intended_command_exact(
                        true, true, authority.intended_command,
                        authority.canonical_command,
                        target->anchored_custom_command));
            if (authority.custom) {
                authority.authority_exact = authority.authority_exact
                    && rollback.kind
                        == AggregateRollbackAuthorityKind::CustomPlay
                    && rollback.ordinal == authority.ordinal
                    && rollback.record_version == authority.record_version
                    && rollback.collection_version == authority.collection_version
                    && rollback.controller == authority.controller
                    && controller_identity_proof_matches(
                        rollback.controller_proof,
                        authority.controller_proof)
                    && rollback.slot == authority.slot
                    && rollback.bgm == authority.bgm
                    && rollback.request == authority.request
                    && rollback.sound == authority.sound
                    && rollback.field_identity.internal_index
                        == authority.sound_identity.internal_index
                    && rollback.field_identity.serial_number
                        == authority.sound_identity.serial_number
                    && rollback.route_generation == authority.route_generation
                    && rollback.lease == authority.lease
                    && rollback.lifecycle_state_epoch
                        == authority.lifecycle_state_epoch
                    && rollback.token_ordinal == authority.token_ordinal
                    && rollback.canonical_token == authority.canonical_token
                    && rollback.custom_token == authority.custom_token
                    && rollback.owner == authority.owner
                    && rollback.owner_thread == authority.owner_thread
                    && rollback.owner_command == authority.intended_command
                    && rollback.operation_nonce == authority.set_nonce
                    && rollback.request_proof
                        == AggregateRollbackRequestProof::ExactPostSet;
            }
        }
    } catch (...) {
        authority.authority_exact = false;
    }
    return authority;
}

BgmPlaybackPendingPlayOutcome play_outcome_from_authority(
    const BgmPlaybackPendingPlayAuthority& authority) noexcept
{
    BgmPlaybackPendingPlayOutcome out;
    out.active = authority.candidate_present;
    out.known_request_sound_exact = authority.known_request_sound_exact;
    out.ordinal = authority.ordinal;
    out.record_version = authority.record_version;
    out.collection_version = authority.collection_version;
    out.set_nonce = authority.set_nonce;
    out.request = authority.request;
    out.custom = authority.custom;
    out.sound = authority.sound;
    out.sound_identity = authority.sound_identity;
    out.authorized_request = authority.request;
    out.authorized_sound = authority.sound;
    out.authorized_sound_identity = authority.sound_identity;
    out.controller = authority.controller;
    out.controller_proof = authority.controller_proof;
    out.slot = authority.slot;
    out.bgm = authority.bgm;
    out.route_generation = authority.route_generation;
    out.lease = authority.lease;
    out.lifecycle_state_epoch = authority.lifecycle_state_epoch;
    out.token_ordinal = authority.token_ordinal;
    out.canonical_token = authority.canonical_token;
    out.custom_token = authority.custom_token;
    out.owner = authority.owner;
    out.owner_thread = authority.owner_thread;
    out.play_nonce = authority.expected_play_nonce;
    return out;
}

BgmPlaybackPendingPlayOutcome capture_bgm_pending_play_outcome_noexcept(
    const BgmPlaybackPendingPlayAuthority& authority) noexcept
{
    auto out = play_outcome_from_authority(authority);
    if (!authority.candidate_present || !authority.authority_exact) return out;
    try {
        void* controller = authority.controller;
        UObjectIdentity sound_identity;
        UObjectIdentity controller_identity;
        out.chain_read = read_controller_audio_chain(controller, out.slot,
            out.bgm, out.sound, out.request, out.state);
        out.sound_identity_read = out.chain_read && out.sound
            && read_uobject_identity(out.sound, sound_identity)
            && sound_identity.live.serial_number > 0
            && validate_live_uobject_handle(out.sound, sound_identity.live);
        if (out.sound_identity_read) out.sound_identity = sound_identity.live;
        if (read_uobject_identity(controller, controller_identity)) {
            out.controller_proof = controller_identity_proof(
                controller, controller_identity);
        }
        out.owner_read = lookup_current_piano_audio_owner(out.owner)
            && out.owner
            && core::safe_read_field(out.owner,
                runtime_layouts::PianoAudioOwner::packed_key,
                out.owner_command)
            && out.owner_command != 0;
        out.owner_state_read = out.owner_read
            && core::safe_read_field(out.owner,
                runtime_layouts::PianoAudioOwner::state, out.owner_state);
        out.owner_thread = GetCurrentThreadId();
        {
            std::lock_guard<std::mutex> state_lock(g_audio_state_mutex);
            out.route_generation = g_audio_route_state.generation;
            out.lease = g_audio_route_state.lease_identity;
            const auto lifecycle = g_onmemory_bank_lifecycle.active();
            out.lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
            if (lifecycle) {
                out.token_ordinal = lifecycle.ordinal;
                out.canonical_token = lifecycle.canonical.encode();
                out.custom_token = lifecycle.custom.encode();
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            const BgmPlaybackBorrowerRecord* record = nullptr;
            for (const auto& candidate : g_bgm_playback_borrowers) {
                if (candidate.active && candidate.ordinal == authority.ordinal) {
                    record = &candidate;
                    break;
                }
            }
            out.publication_authority_exact = authority.authority_exact && record
                && record->version == authority.record_version
                && g_bgm_playback_collection_version
                    == authority.collection_version
                && record->active && !record->failed
                && record->controller == controller
                && record->transition_set_pending
                && record->transition_set_nonce == authority.set_nonce
                && record->transition_set_custom == authority.custom;
        }
    } catch (...) {
        out = play_outcome_from_authority(authority);
    }
    return out;
}

BgmPlaybackPendingPlayOutcome capture_bgm_pending_play_outcome_seh(
    const BgmPlaybackPendingPlayAuthority& authority) noexcept
{
    auto out = play_outcome_from_authority(authority);
    __try {
        out = capture_bgm_pending_play_outcome_noexcept(authority);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = play_outcome_from_authority(authority);
    }
    return out;
}

bool retain_unpublished_bgm_set_outcome_noexcept(
    const BgmPlaybackSetObservationCandidate& candidate,
    const BgmPlaybackNativeSetOutcome& outcome) noexcept
{
    if (!candidate.active || !outcome.native_callback_entered) return true;
    uint64_t ordinal = candidate.ordinal;
    uint64_t request = outcome.exact ? outcome.request : 0;
    bool retained = false;
    try {
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            for (auto& record : g_bgm_playback_borrowers) {
                if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                        record.failed, record.terminal_callback_retired)
                    || record.ordinal != candidate.ordinal
                    || record.controller != candidate.controller
                    || !(record.lease == candidate.lease)
                    || record.canonical != candidate.canonical_token
                    || record.custom != candidate.custom_token) continue;
                const bool published_exact = outcome.native_succeeded
                    && outcome.exact
                    && record.transition_set_pending
                    && bgm_playback_exact_successor(
                        candidate.version, record.version)
                    && bgm_playback_exact_successor(
                        candidate.collection_version,
                        g_bgm_playback_collection_version)
                    && record.controller == outcome.controller
                    && record.slot == outcome.slot && record.bgm == outcome.bgm
                    && record.transition_set_handle == outcome.request
                    && record.transition_set_sound == outcome.sound
                    && record.transition_set_sound_identity.internal_index
                        == outcome.sound_identity.internal_index
                    && record.transition_set_sound_identity.serial_number
                        == outcome.sound_identity.serial_number
                    && record.transition_lease == candidate.lease
                    && bgm_playback_exact_successor(
                        candidate.route_generation,
                        record.transition_route_generation)
                    && record.transition_token_epoch == candidate.token_epoch
                    && record.transition_lifecycle_state_epoch
                        == candidate.lifecycle_state_epoch
                    && record.transition_canonical_token
                        == candidate.canonical_token
                    && record.transition_custom_token == candidate.custom_token;
                const bool nonce_exact = bgm_playback_operation_nonce_next(
                    candidate.operation_predecessor_nonce,
                    record.transition_set_nonce);
                if (published_exact && nonce_exact) return true;
                record.exit_unresolved_native_request = true;
                record.exit_unresolved_native_request_faulted =
                    !outcome.native_succeeded;
                record.exit_unresolved_request_handle = request;
                record.exit_unresolved_request_generation = request >> 32;
                record.exit_unresolved_request_sound = outcome.exact
                    ? outcome.sound : candidate.requested_sound;
                record.exit_unresolved_request_sound_identity = outcome.exact
                    ? outcome.sound_identity : candidate.requested_sound_identity;
                record.expected_new_sound = outcome.exact
                    ? outcome.sound : candidate.requested_sound;
                record.expected_new_sound_identity = outcome.exact
                    ? outcome.sound_identity : candidate.requested_sound_identity;
                record.set_observed = true;
                if (outcome.exact) {
                    if (record.new_handle == 0 || record.new_handle == request) {
                        record.new_handle = request;
                        record.expected_new_sound = outcome.sound;
                        record.expected_new_sound_identity = outcome.sound_identity;
                        record.set_observed = true;
                        record.new_ready = record.new_ready || outcome.state == 4;
                        record.new_absent = false;
                    } else {
                        record.failed = true;
                    }
                }
                ++record.version;
                ++g_bgm_playback_collection_version;
                retained = true;
                break;
            }
        }
    } catch (...) {
    }
    const char* reason = "postcall_unreadable";
    if (outcome.fault == BgmPlaybackProtectedNativeFault::CppException
        || outcome.fault
            == BgmPlaybackProtectedNativeFault::StructuredException) {
        reason = "native_fault";
    } else if (!outcome.native_succeeded) {
        reason = "native_failure";
    }
    log_bgm_exit_request("exit_unresolved_request_retained",
        ordinal, request, request >> 32, reason);
    return retained;
}

bool retain_unpublished_bgm_play_outcome_noexcept(
    void* controller, const BgmPlaybackPendingPlayOutcome& outcome) noexcept
{
    if (!controller || !outcome.active) return true;
    if (!outcome.known_request_sound_exact
        || !AudioBgmRequestHandle{
            outcome.authorized_request}.valid_bgm_request()
        || !outcome.authorized_sound
        || outcome.authorized_sound_identity.serial_number <= 0) return false;
    bool retained = false;
    try {
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            for (auto& record : g_bgm_playback_borrowers) {
                if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                        record.failed, record.terminal_callback_retired)
                    || record.ordinal != outcome.ordinal
                    || record.controller != controller) continue;
                const bool exact_play_publication = record.transition_play_epoch != 0
                    && bgm_playback_exact_successor(
                        outcome.record_version, record.version)
                    && bgm_playback_exact_successor(
                        outcome.collection_version,
                        g_bgm_playback_collection_version)
                    && bgm_playback_operation_nonce_next(
                        outcome.set_nonce, record.transition_play_nonce);
                const bool represented = (exact_play_publication
                        && record.canonical_intermediate_handle
                            == outcome.authorized_request)
                    || (exact_play_publication
                        && record.latest_custom_transition_handle
                            == outcome.authorized_request)
                    || (record.exit_unresolved_native_request
                        && record.exit_unresolved_request_handle
                            == outcome.authorized_request
                        && record.exit_unresolved_request_sound
                            == outcome.authorized_sound
                        && record.exit_unresolved_request_sound_identity.internal_index
                            == outcome.authorized_sound_identity.internal_index
                        && record.exit_unresolved_request_sound_identity.serial_number
                            == outcome.authorized_sound_identity.serial_number);
                if (represented) return true;
                const bool pending_authority_exact = record.transition_set_pending
                    && record.transition_set_nonce == outcome.set_nonce
                    && record.transition_set_handle == outcome.authorized_request
                    && record.transition_set_sound == outcome.authorized_sound
                    && record.transition_set_sound_identity.internal_index
                        == outcome.authorized_sound_identity.internal_index
                    && record.transition_set_sound_identity.serial_number
                        == outcome.authorized_sound_identity.serial_number;
                if (!pending_authority_exact) continue;
                record.exit_unresolved_native_request = true;
                record.exit_unresolved_native_request_faulted = false;
                record.exit_unresolved_request_handle = outcome.authorized_request;
                record.exit_unresolved_request_generation =
                    outcome.authorized_request >> 32;
                record.exit_unresolved_request_sound = outcome.authorized_sound;
                record.exit_unresolved_request_sound_identity =
                    outcome.authorized_sound_identity;
                record.transition_set_pending = false;
                record.transition_set_nonce = 0;
                record.transition_set_handle = 0;
                record.transition_set_custom = false;
                record.transition_set_sound = nullptr;
                record.transition_set_sound_identity = {};
                record.transition_command_key = 0;
                if (record.new_handle == 0
                    || record.new_handle == outcome.authorized_request) {
                    record.new_handle = outcome.authorized_request;
                    record.expected_new_sound = outcome.authorized_sound;
                    record.expected_new_sound_identity =
                        outcome.authorized_sound_identity;
                    record.set_observed = true;
                    record.new_absent = false;
                } else {
                    record.failed = true;
                }
                ++record.version;
                ++g_bgm_playback_collection_version;
                retained = true;
                break;
            }
        }
    } catch (...) {
    }
    (void)bgm_playback_observe_best_effort(true, [&] {
        log_bgm_exit_request(retained
                ? "exit_unresolved_request_retained"
                : "exit_unresolved_publication_latched",
            outcome.ordinal, outcome.authorized_request,
            outcome.authorized_request >> 32);
    });
    return retained;
}

void observe_bgm_playback_set_result_best_effort(
    void* controller, void* sound, bool native_succeeded,
    BgmPlaybackDeferredLogProposal* proposal) noexcept
{
    (void)ff7r::piano::game::bgm_playback_observe_best_effort(
        native_succeeded, [controller, sound, proposal]() {
            observe_bgm_playback_set_result(controller, sound, true, proposal);
        });
    if (!native_succeeded) {
        try {
            observe_bgm_playback_set_result(controller, sound, false, proposal);
        } catch (...) {
        }
    }
}

void observe_bgm_playback_set_result_best_effort_seh(
    void* controller, void* sound, bool native_succeeded,
    BgmPlaybackDeferredLogProposal* proposal) noexcept
{
    __try {
        observe_bgm_playback_set_result_best_effort(
            controller, sound, native_succeeded, proposal);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        invalidate_bgm_playback_operation(
            controller, true, "observer_structured_fault", proposal);
    }
}

void call_bgm_slot_set_original(void* controller, void* sound)
{
    AggregateMutationLease mutation_lease;
    AggregatePatchRollback aggregate_patch;
    BgmAggregateOwnerPatchLogProposal owner_patch_log;
    g_bgm_aggregate_set_owner_rebound = mutation_lease.active
        && prepare_bgm_aggregate_owner_patch(
            controller, sound, false, aggregate_patch, nullptr,
            &owner_patch_log);
    const bool native_available = g_original_bgm_slot_set || g_exe_module;
    AggregateNativeCleanup cleanup{&aggregate_patch};
    bool request_strengthened = true;
    BgmPlaybackNativeSetCallOutcome native_call;
    const auto native = bgm_playback_protected_native_boundary(
        [&] {
            const auto native_target = select_bgm_playback_native_set_target(
                g_original_bgm_slot_set != nullptr, g_exe_module != nullptr);
            native_call = call_bgm_slot_set_native_protected(
                native_target, controller, sound);
            const auto result =
                bgm_playback_native_set_compatibility_result(native_call);
            if (result.succeeded) {
                request_strengthened =
                    strengthen_custom_set_rollback_request_noexcept(
                        aggregate_patch);
            }
            return result;
        },
        [&cleanup]() noexcept { cleanup(); });
    const auto set_candidate = g_bgm_playback_set_observation_candidate;
    const auto set_outcome = capture_bgm_native_set_outcome_noexcept(
        controller, sound, native_available, native);
    const bool publication_owed = bgm_playback_set_outcome_publication_owed(
        set_candidate.active, native_available);
    if (publication_owed) g_bgm_aggregate_unresolved_publications.fetch_add(
        1, std::memory_order_acq_rel);
    flush_aggregate_rollback_slots_best_effort();
    g_bgm_aggregate_set_owner_rebound =
        g_bgm_aggregate_set_owner_rebound
        && cleanup.owner_result == AggregatePatchCleanupResult::Restored;
    BgmPlaybackDeferredLogProposal set_log;
    observe_bgm_playback_set_result_best_effort_seh(
        controller, sound, native.succeeded && native_available, &set_log);
    const bool publication_durable =
        retain_unpublished_bgm_set_outcome_noexcept(set_candidate, set_outcome);
    if (publication_owed && publication_durable) {
        g_bgm_aggregate_unresolved_publications.fetch_sub(
            1, std::memory_order_acq_rel);
    }
    auto release_witness = mutation_lease.release();
    emit_bgm_playback_preparation_log(g_bgm_playback_set_preparation_log,
        BgmPlaybackPreparationEmissionPhase::BorrowerAuthorityReleased,
        release_witness);
    emit_bgm_aggregate_owner_patch_log(owner_patch_log, release_witness);
    publish_bgm_aggregate_native_cleanup(cleanup.owner_result, release_witness);
    if (release_witness.released()
        && native.succeeded && !request_strengthened) {
        log_bgm_aggregate_mutation("set_request_strengthening_failed",
            "native_callback", aggregate_patch.authority.ordinal,
            aggregate_patch.authority.record_version,
            aggregate_patch.authority.operation_nonce);
    }
    emit_bgm_playback_result_log(set_log, release_witness);
}

void observe_bgm_playback_play_transition(void* controller,
    const BgmPlaybackPendingPlayOutcome& outcome,
    BgmPlaybackUnpublishedPlayDiagnostic* unpublished,
    BgmPlaybackDeferredLogProposal* proposal) noexcept;
const char* play_failure_name(BgmPlaybackPlayFailure failure) noexcept;

void observe_bgm_playback_play_best_effort_seh(
    void* controller, bool native_succeeded,
    const BgmPlaybackPendingPlayOutcome& outcome,
    BgmPlaybackUnpublishedPlayDiagnostic* unpublished,
    BgmPlaybackDeferredLogProposal* proposal) noexcept
{
    __try {
        (void)ff7r::piano::game::bgm_playback_observe_best_effort(
            native_succeeded, [controller, outcome, unpublished, proposal]() {
                observe_bgm_playback_play_transition(
                    controller, outcome, unpublished, proposal);
            });
        if (!native_succeeded && !outcome.active) {
            observe_bgm_playback_play_failed(controller, proposal);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!outcome.active) {
            invalidate_bgm_playback_operation(
                controller, false, "observer_structured_fault", proposal);
        }
    }
}

const char* pending_play_candidate_source_name(
    const BgmPlaybackPendingPlayCandidateSource source) noexcept
{
    switch (source) {
    case BgmPlaybackPendingPlayCandidateSource::Authority: return "authority";
    case BgmPlaybackPendingPlayCandidateSource::OwnerPatch: return "owner_patch";
    case BgmPlaybackPendingPlayCandidateSource::Both: return "both";
    case BgmPlaybackPendingPlayCandidateSource::None: return "none";
    }
    return "none";
}

void publish_bgm_pending_play_ownership_diagnostic(
    const BgmPlaybackPendingPlayOwnershipDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed) return;
    const char* source = pending_play_candidate_source_name(diagnostic.source);
    (void)bgm_playback_observe_best_effort(true, [&] {
        log_bgm_exit_request(diagnostic.unknown_publication_latched
                ? "play_publication_unknown_latched"
                : "play_publication_owned", diagnostic.ordinal,
            diagnostic.request, diagnostic.request >> 32, source);
    });
}

void publish_bgm_unpublished_play_diagnostic(
    const BgmPlaybackUnpublishedPlayDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed) return;
    (void)bgm_playback_observe_best_effort(true, [&] {
        std::ostringstream out;
        out << "[audio_sead] playback_aggregate"
            << " status=transition_play_unpublished"
            << " first_failed_predicate="
            << play_failure_name(diagnostic.first_failure)
            << " candidate_source="
            << pending_play_candidate_source_name(diagnostic.source)
            << " authority_candidate_present="
            << diagnostic.authority_candidate_present
            << " authority_exact=" << diagnostic.authority_exact
            << " publication_authority_exact="
            << diagnostic.publication_authority_exact
            << " ordinal=" << diagnostic.ordinal
            << " expected_record_version="
            << diagnostic.expected_record_version
            << " current_record_version="
            << diagnostic.current_record_version
            << " expected_collection_version="
            << diagnostic.expected_collection_version
            << " current_collection_version="
            << diagnostic.current_collection_version
            << " expected_route=" << diagnostic.expected_route
            << " current_route=" << diagnostic.current_route
            << " expected_lifecycle=" << diagnostic.expected_lifecycle
            << " current_lifecycle=" << diagnostic.current_lifecycle
            << " expected_request=0x" << std::hex
            << diagnostic.expected_request
            << " current_request=0x" << diagnostic.current_request
            << " expected_command=0x" << diagnostic.expected_command
            << " captured_owner_command=0x"
            << diagnostic.captured_owner_command
            << std::dec
            << " captured_owner_state_read="
            << diagnostic.captured_owner_state_read
            << " captured_owner_state="
            << static_cast<unsigned>(diagnostic.captured_owner_state)
            << " expected_nonce=" << diagnostic.expected_nonce
            << " current_nonce=" << diagnostic.current_nonce
            << " pending_set_exact=" << diagnostic.pending_set_exact
            << " controller_chain_exact="
            << diagnostic.controller_chain_exact
            << " owner_thread_exact=" << diagnostic.owner_thread_exact
            << " command_exact=" << diagnostic.command_exact
            << " route_exact=" << diagnostic.route_exact
            << " lease_exact=" << diagnostic.lease_exact
            << " lifecycle_exact=" << diagnostic.lifecycle_exact
            << " token_ordinal_exact=" << diagnostic.token_ordinal_exact
            << " token_values_exact=" << diagnostic.token_values_exact
            << " request_sound_identity_exact="
            << diagnostic.request_sound_identity_exact
            << " request_handle_exact=" << diagnostic.request_handle_exact
            << " state4_exact=" << diagnostic.state4_exact
            << " nonce_exact=" << diagnostic.nonce_exact
            << " mutation_authorized=0";
        core::log(core::LogLevel::Info, out.str());
    });
}

const char* list_return_clear_authority_blocker_name(
    const ListReturnClearAuthorityBlocker blocker) noexcept
{
    switch (blocker) {
    case ListReturnClearAuthorityBlocker::None: return "none";
    case ListReturnClearAuthorityBlocker::CurrentController: return "current_controller";
    case ListReturnClearAuthorityBlocker::ControllerIdentity: return "controller_identity";
    case ListReturnClearAuthorityBlocker::ControllerChain: return "controller_chain";
    case ListReturnClearAuthorityBlocker::SoundRead: return "sound_read";
    case ListReturnClearAuthorityBlocker::RequestRead: return "request_read";
    case ListReturnClearAuthorityBlocker::StateRead: return "state_read";
    case ListReturnClearAuthorityBlocker::SoundMissing: return "sound_missing";
    case ListReturnClearAuthorityBlocker::RequestMissing: return "request_missing";
    case ListReturnClearAuthorityBlocker::StateNotPlaying: return "state_not_playing";
    case ListReturnClearAuthorityBlocker::SoundIdentity: return "sound_identity";
    case ListReturnClearAuthorityBlocker::CustomResourceUnowned: return "custom_resource_unowned";
    case ListReturnClearAuthorityBlocker::SlotMismatch: return "slot_mismatch";
    case ListReturnClearAuthorityBlocker::BgmMismatch: return "bgm_mismatch";
    case ListReturnClearAuthorityBlocker::SoundMismatch: return "sound_mismatch";
    case ListReturnClearAuthorityBlocker::RequestMismatch: return "request_mismatch";
    case ListReturnClearAuthorityBlocker::OwnedSoundIdentity: return "owned_sound_identity";
    case ListReturnClearAuthorityBlocker::AggregateAuthority: return "aggregate_authority";
    case ListReturnClearAuthorityBlocker::AggregateVersionDrift: return "aggregate_version_drift";
    case ListReturnClearAuthorityBlocker::PostClearValidation: return "post_clear_validation";
    }
    return "unknown";
}

const char* list_return_clear_authority_source_name(
    const ListReturnClearAuthoritySource source) noexcept
{
    switch (source) {
    case ListReturnClearAuthoritySource::None: return "none";
    case ListReturnClearAuthoritySource::LegacyRoute: return "legacy_route";
    case ListReturnClearAuthoritySource::AggregateCanonical:
        return "aggregate_canonical";
    case ListReturnClearAuthoritySource::Both: return "both";
    case ListReturnClearAuthoritySource::RouteRestore: return "route_restore";
    }
    return "unknown";
}

void publish_list_return_clear_authority_diagnostic(
    const ListReturnClearAuthorityDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed) return;
    (void)bgm_playback_observe_best_effort(true, [&] {
        std::ostringstream out;
        out << "[audio_sead] list_return_clear_authority"
            << " blocker="
            << list_return_clear_authority_blocker_name(diagnostic.blocker)
            << " source="
            << list_return_clear_authority_source_name(diagnostic.source)
            << " route_owned=" << diagnostic.route_owned
            << " aggregate_candidate_present="
            << diagnostic.aggregate_candidate_present
            << " aggregate_canonical_exit_exact="
            << diagnostic.aggregate_canonical_exit_exact
            << " aggregate_pre_call_revalidated="
            << diagnostic.aggregate_pre_call_revalidated
            << " aggregate_clear_committed="
            << diagnostic.aggregate_clear_committed
            << " canonical_substrate_relinquished="
            << diagnostic.canonical_substrate_relinquished
            << " canonical_substrate_pending_published="
            << diagnostic.canonical_substrate_pending_published
            << " canonical_substrate_route_committed="
            << diagnostic.canonical_substrate_route_committed
            << " canonical_substrate_borrower_finalized="
            << diagnostic.canonical_substrate_borrower_finalized
            << " canonical_substrate_partial="
            << diagnostic.canonical_substrate_partial
            << " canonical_substrate_state="
            << static_cast<unsigned>(diagnostic.canonical_substrate_state)
            << " canonical_substrate_transaction_generation="
            << diagnostic.canonical_substrate_transaction_generation
            << " post_clear_exact=" << diagnostic.post_clear_exact
            << " synchronous_absence_observer_ran="
            << diagnostic.synchronous_absence_observer_ran
            << " clear_attempted=" << diagnostic.clear_attempted
            << " route_generation=" << diagnostic.route_generation
            << " route_lease_generation=" << diagnostic.route_lease.generation
            << " route_lease_song=0x" << std::hex
            << diagnostic.route_lease.song_key
            << " route_controller=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.route_controller)
            << " route_slot=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.route_slot)
            << " route_bgm=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.route_bgm)
            << " route_sound=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.route_sound)
            << " route_sound_index=" << std::dec
            << diagnostic.route_sound_identity.internal_index
            << " route_sound_serial="
            << diagnostic.route_sound_identity.serial_number
            << " route_request=0x" << std::hex << diagnostic.route_request
            << std::dec
            << " route_controller_proof_valid="
            << diagnostic.route_controller_proof_valid
            << " route_custom_resource_owned="
            << diagnostic.route_custom_resource_owned
            << " route_list_cleanup_pending="
            << diagnostic.route_list_cleanup_pending
            << " route_native_clear_verified="
            << diagnostic.route_native_clear_verified
            << " live_controller=0x" << std::hex
            << reinterpret_cast<uintptr_t>(diagnostic.live_controller)
            << " live_slot=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.live_slot)
            << " live_bgm=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.live_bgm)
            << " live_sound=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.live_sound)
            << " live_sound_index=" << std::dec
            << diagnostic.live_sound_identity.internal_index
            << " live_sound_serial="
            << diagnostic.live_sound_identity.serial_number
            << " live_request=0x" << std::hex << diagnostic.live_request
            << std::dec << " live_state="
            << static_cast<unsigned>(diagnostic.live_state)
            << " live_controller_exact=" << diagnostic.live_controller_exact
            << " live_controller_identity_exact="
            << diagnostic.live_controller_identity_exact
            << " live_chain_read=" << diagnostic.live_chain_read
            << " live_sound_read=" << diagnostic.live_sound_read
            << " live_request_read=" << diagnostic.live_request_read
            << " live_state_read=" << diagnostic.live_state_read
            << " live_sound_identity_read="
            << diagnostic.live_sound_identity_read
            << " live_slot_exact=" << diagnostic.live_slot_exact
            << " live_bgm_exact=" << diagnostic.live_bgm_exact
            << " live_sound_exact=" << diagnostic.live_sound_exact
            << " live_request_exact=" << diagnostic.live_request_exact
            << " live_sound_identity_exact="
            << diagnostic.live_sound_identity_exact
            << " aggregate_ordinal=" << diagnostic.aggregate_ordinal
            << " aggregate_record_version="
            << diagnostic.aggregate_record_version
            << " aggregate_collection_version="
            << diagnostic.aggregate_collection_version
            << " aggregate_list_exit_epoch="
            << diagnostic.aggregate_list_exit_epoch
            << " aggregate_controller=0x" << std::hex
            << reinterpret_cast<uintptr_t>(diagnostic.aggregate_controller)
            << " aggregate_slot=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.aggregate_slot)
            << " aggregate_bgm=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.aggregate_bgm)
            << " aggregate_canonical_sound=0x"
            << reinterpret_cast<uintptr_t>(diagnostic.aggregate_canonical_sound)
            << " aggregate_sound_index=" << std::dec
            << diagnostic.aggregate_canonical_sound_identity.internal_index
            << " aggregate_sound_serial="
            << diagnostic.aggregate_canonical_sound_identity.serial_number
            << " aggregate_new=0x" << std::hex
            << diagnostic.aggregate_new_handle
            << " aggregate_canonical=0x"
            << diagnostic.aggregate_canonical_handle
            << std::dec << " aggregate_route="
            << diagnostic.aggregate_route_generation
            << " aggregate_lease_generation="
            << diagnostic.aggregate_lease.generation
            << " aggregate_lease_song=0x" << std::hex
            << diagnostic.aggregate_lease.song_key << std::dec
            << " aggregate_lifecycle="
            << diagnostic.aggregate_lifecycle_state_epoch
            << " aggregate_token_epoch=" << diagnostic.aggregate_token_epoch
            << " aggregate_canonical_token=0x" << std::hex
            << diagnostic.aggregate_canonical_token
            << " aggregate_custom_token=0x"
            << diagnostic.aggregate_custom_token << std::dec
            << " aggregate_controller_proof_matches_route="
            << diagnostic.aggregate_controller_proof_matches_route
            << " aggregate_list_exit=" << diagnostic.aggregate_list_exit
            << " aggregate_current_active="
            << diagnostic.aggregate_current_active
            << " aggregate_failed=" << diagnostic.aggregate_failed
            << " aggregate_destination_aba="
            << diagnostic.aggregate_destination_aba
            << " aggregate_old_identity_conflict="
            << diagnostic.aggregate_old_identity_conflict
            << " aggregate_lineage_identity_conflict="
            << diagnostic.aggregate_lineage_identity_conflict
            << " aggregate_continuity_exact="
            << diagnostic.aggregate_continuity_exact
            << " aggregate_set_pending="
            << diagnostic.aggregate_transition_set_pending
            << " aggregate_boundary_pending="
            << diagnostic.aggregate_boundary_pending
            << " aggregate_unresolved="
            << diagnostic.aggregate_unresolved_request;
        core::log(core::LogLevel::Info, out.str());
    });
}

void call_bgm_slot_play_original(void* controller)
{
    AggregateMutationLease mutation_lease;
    AggregatePatchRollback aggregate_patch;
    BgmAggregateOwnerPatchLogProposal owner_patch_log;
    bool owner_patch_candidate_present = false;
    g_bgm_aggregate_play_owner_rebound = mutation_lease.active
        && prepare_bgm_aggregate_owner_patch(
            controller, nullptr, true, aggregate_patch,
            &owner_patch_candidate_present, &owner_patch_log);
    const bool native_available = g_original_bgm_slot_play != nullptr;
    const auto play_authority = prepare_bgm_pending_play_authority_noexcept(
        controller, aggregate_patch.authority);
    const BgmPlaybackPendingPlayAuthorityDecisionFacts play_decision{
        native_available, play_authority.candidate_present,
        owner_patch_candidate_present, play_authority.authority_exact};
    const bool candidate_present =
        bgm_playback_pending_play_candidate_present(play_decision);
    BgmPlaybackPendingPlayOwnershipDiagnostic ownership_diagnostic{
        candidate_present, false,
        bgm_playback_pending_play_candidate_source(play_decision),
        play_authority.ordinal, play_authority.request};
    const bool publication_owed =
        bgm_playback_pending_play_latch_required(play_decision);
    if (publication_owed) g_bgm_aggregate_unresolved_publications.fetch_add(
        1, std::memory_order_acq_rel);
    AggregateNativeCleanup cleanup{&aggregate_patch};
    const auto native_target = select_bgm_playback_native_play_target(
        native_available);
    BgmPlaybackPendingPlayOutcome play_outcome;
    BgmPlaybackUnpublishedPlayDiagnostic unpublished_diagnostic;
    BgmPlaybackDeferredLogProposal play_log;
    const auto orchestration = run_bgm_playback_native_play_orchestration(
        native_target,
        BgmPlaybackNativePlayOrchestrationFacts{
            candidate_present, play_authority.authority_exact,
            publication_owed, native_available, false,
            play_authority.candidate_present},
        [&](BgmPlaybackNativePlayTarget target) {
            return call_bgm_slot_play_native_protected(target, controller);
        },
        [&] {
            play_outcome = capture_bgm_pending_play_outcome_seh(play_authority);
            return BgmPlaybackNativePlayCaptureResult{
                play_outcome.state == 4};
        },
        [&] {
            cleanup();
            flush_aggregate_rollback_slots_best_effort();
            g_bgm_aggregate_play_owner_rebound =
                g_bgm_aggregate_play_owner_rebound
                && cleanup.owner_result
                    == AggregatePatchCleanupResult::Restored;
        },
        [&](const BgmPlaybackProtectedNativeResult& native,
            const BgmPlaybackNativePlayCaptureResult&) {
            observe_bgm_playback_play_best_effort_seh(
                controller, native.succeeded, play_outcome,
                &unpublished_diagnostic, &play_log);
        },
        [&](const BgmPlaybackNativePlayCaptureResult&) {
            return retain_unpublished_bgm_play_outcome_noexcept(
                controller, play_outcome);
        },
        [&](const bool discharge) {
            if (discharge) {
                g_bgm_aggregate_unresolved_publications.fetch_sub(
                    1, std::memory_order_acq_rel);
            }
        });
    const auto native = orchestration.compatibility;
    ownership_diagnostic.unknown_publication_latched =
        orchestration.unknown_publication_latched;
    if (unpublished_diagnostic.proposed) {
        unpublished_diagnostic.authority_candidate_present =
            play_authority.candidate_present;
        unpublished_diagnostic.authority_exact = play_authority.authority_exact;
        unpublished_diagnostic.source =
            bgm_playback_pending_play_candidate_source(play_decision);
    }
    auto release_witness = mutation_lease.release();
    emit_bgm_aggregate_owner_patch_log(owner_patch_log, release_witness);
    emit_bgm_playback_result_log(play_log, release_witness);
    publish_bgm_aggregate_native_cleanup(cleanup.owner_result, release_witness);
    if (release_witness.released()) {
        publish_bgm_pending_play_ownership_diagnostic(ownership_diagnostic);
    }
    publish_bgm_unpublished_play_diagnostic(unpublished_diagnostic);
}

BgmPlaybackNativeSetCallOutcome call_bgm_slot_set_native_protected(
    BgmPlaybackNativeSetTarget target, void* controller, void* sound)
{
    (void)target;
    return invoke_audio_production_set(controller, sound);
}

bool call_bgm_slot_set_original_seh(void* controller, void* sound)
{
    const bool native_available = g_original_bgm_slot_set || g_exe_module;
    AggregateMutationLease mutation_lease;
    AggregatePatchRollback aggregate_patch;
    BgmAggregateOwnerPatchLogProposal owner_patch_log;
    g_bgm_aggregate_set_owner_rebound = mutation_lease.active
        && prepare_bgm_aggregate_owner_patch(
            controller, sound, false, aggregate_patch, nullptr,
            &owner_patch_log);
    AggregateNativeCleanup cleanup{&aggregate_patch};
    bool request_strengthened = true;
    BgmPlaybackNativeSetCallOutcome native_call;
    const auto native = bgm_playback_protected_native_boundary(
        [&] {
            const auto native_target = select_bgm_playback_native_set_target(
                g_original_bgm_slot_set != nullptr, g_exe_module != nullptr);
            native_call = call_bgm_slot_set_native_protected(
                native_target, controller, sound);
            const auto result =
                bgm_playback_native_set_compatibility_result(native_call);
            if (result.succeeded) {
                request_strengthened =
                    strengthen_custom_set_rollback_request_noexcept(
                        aggregate_patch);
            }
            return result;
        },
        [&cleanup]() noexcept { cleanup(); });
    const auto set_candidate = g_bgm_playback_set_observation_candidate;
    const auto set_outcome = capture_bgm_native_set_outcome_noexcept(
        controller, sound, native_available, native);
    const bool publication_owed = bgm_playback_set_outcome_publication_owed(
        set_candidate.active, native_available);
    if (publication_owed) g_bgm_aggregate_unresolved_publications.fetch_add(
        1, std::memory_order_acq_rel);
    flush_aggregate_rollback_slots_best_effort();
    g_bgm_aggregate_set_owner_rebound =
        g_bgm_aggregate_set_owner_rebound
        && cleanup.owner_result == AggregatePatchCleanupResult::Restored;
    BgmPlaybackDeferredLogProposal set_log;
    observe_bgm_playback_set_result_best_effort_seh(
        controller, sound, native.succeeded && native_available, &set_log);
    const bool publication_durable =
        retain_unpublished_bgm_set_outcome_noexcept(set_candidate, set_outcome);
    if (publication_owed && publication_durable) {
        g_bgm_aggregate_unresolved_publications.fetch_sub(
            1, std::memory_order_acq_rel);
    }
    auto release_witness = mutation_lease.release();
    emit_bgm_playback_preparation_log(g_bgm_playback_set_preparation_log,
        BgmPlaybackPreparationEmissionPhase::BorrowerAuthorityReleased,
        release_witness);
    emit_bgm_aggregate_owner_patch_log(owner_patch_log, release_witness);
    publish_bgm_aggregate_native_cleanup(cleanup.owner_result, release_witness);
    if (release_witness.released()
        && native.succeeded && !request_strengthened) {
        log_bgm_aggregate_mutation("set_request_strengthening_failed",
            "native_callback", aggregate_patch.authority.ordinal,
            aggregate_patch.authority.record_version,
            aggregate_patch.authority.operation_nonce);
    }
    emit_bgm_playback_result_log(set_log, release_witness);
    return native.succeeded;
}

BgmPlaybackNativePlayCallOutcome call_bgm_slot_play_native_protected(
    const BgmPlaybackNativePlayTarget target, void* controller)
{
    (void)target;
    return invoke_audio_production_play(controller);
}

bool call_bgm_slot_play_original_seh(void* controller)
{
    const bool native_available = g_original_bgm_slot_play != nullptr;
    AggregateMutationLease mutation_lease;
    AggregatePatchRollback aggregate_patch;
    BgmAggregateOwnerPatchLogProposal owner_patch_log;
    bool owner_patch_candidate_present = false;
    g_bgm_aggregate_play_owner_rebound = mutation_lease.active
        && prepare_bgm_aggregate_owner_patch(
            controller, nullptr, true, aggregate_patch,
            &owner_patch_candidate_present, &owner_patch_log);
    const auto play_authority = prepare_bgm_pending_play_authority_noexcept(
        controller, aggregate_patch.authority);
    const BgmPlaybackPendingPlayAuthorityDecisionFacts play_decision{
        native_available, play_authority.candidate_present,
        owner_patch_candidate_present, play_authority.authority_exact};
    const bool candidate_present =
        bgm_playback_pending_play_candidate_present(play_decision);
    BgmPlaybackPendingPlayOwnershipDiagnostic ownership_diagnostic{
        candidate_present, false,
        bgm_playback_pending_play_candidate_source(play_decision),
        play_authority.ordinal, play_authority.request};
    const bool publication_owed =
        bgm_playback_pending_play_latch_required(play_decision);
    if (publication_owed) g_bgm_aggregate_unresolved_publications.fetch_add(
        1, std::memory_order_acq_rel);
    AggregateNativeCleanup cleanup{&aggregate_patch};
    const auto native_target = select_bgm_playback_native_play_target(
        native_available);
    BgmPlaybackPendingPlayOutcome play_outcome;
    BgmPlaybackUnpublishedPlayDiagnostic unpublished_diagnostic;
    BgmPlaybackDeferredLogProposal play_log;
    const auto orchestration = run_bgm_playback_native_play_orchestration(
        native_target,
        BgmPlaybackNativePlayOrchestrationFacts{
            candidate_present, play_authority.authority_exact,
            publication_owed, native_available, true,
            play_authority.candidate_present},
        [&](BgmPlaybackNativePlayTarget target) {
            return call_bgm_slot_play_native_protected(target, controller);
        },
        [&] {
            play_outcome = capture_bgm_pending_play_outcome_seh(play_authority);
            return BgmPlaybackNativePlayCaptureResult{
                play_outcome.state == 4};
        },
        [&] {
            cleanup();
            flush_aggregate_rollback_slots_best_effort();
            g_bgm_aggregate_play_owner_rebound =
                g_bgm_aggregate_play_owner_rebound
                && cleanup.owner_result
                    == AggregatePatchCleanupResult::Restored;
        },
        [&](const BgmPlaybackProtectedNativeResult& native,
            const BgmPlaybackNativePlayCaptureResult&) {
            observe_bgm_playback_play_best_effort_seh(
                controller, native.succeeded, play_outcome,
                &unpublished_diagnostic, &play_log);
        },
        [&](const BgmPlaybackNativePlayCaptureResult&) {
            return retain_unpublished_bgm_play_outcome_noexcept(
                controller, play_outcome);
        },
        [&](const bool discharge) {
            if (discharge) {
                g_bgm_aggregate_unresolved_publications.fetch_sub(
                    1, std::memory_order_acq_rel);
            }
        });
    const auto native = orchestration.compatibility;
    ownership_diagnostic.unknown_publication_latched =
        orchestration.unknown_publication_latched;
    if (unpublished_diagnostic.proposed) {
        unpublished_diagnostic.authority_candidate_present =
            play_authority.candidate_present;
        unpublished_diagnostic.authority_exact = play_authority.authority_exact;
        unpublished_diagnostic.source =
            bgm_playback_pending_play_candidate_source(play_decision);
    }
    auto release_witness = mutation_lease.release();
    emit_bgm_aggregate_owner_patch_log(owner_patch_log, release_witness);
    emit_bgm_playback_result_log(play_log, release_witness);
    publish_bgm_aggregate_native_cleanup(cleanup.owner_result, release_witness);
    if (release_witness.released()) {
        publish_bgm_pending_play_ownership_diagnostic(ownership_diagnostic);
    }
    publish_bgm_unpublished_play_diagnostic(unpublished_diagnostic);
    return native.succeeded;
}

const SidecarRuntimeState* find_ready_sidecar_locked(const SongDescriptor& song)
{
    return g_sidecar_prefix
        ? find_prepared_audio_sidecar(*g_sidecar_prefix, sidecar_key_for_song(song))
        : nullptr;
}

void* ensure_alias_music(void* source_sound)
{
    const uintptr_t existing = g_alias_music.load(std::memory_order_acquire);
    if (existing) {
        return reinterpret_cast<void*>(existing);
    }
    if (!source_sound || !g_exe_module) {
        return nullptr;
    }

    void* object_class = nullptr;
    void* template_outer = nullptr;
    if (!core::safe_read_field(source_sound, runtime_layouts::UObject::object_class, object_class)
        || !object_class) {
        return nullptr;
    }
    (void)core::safe_read_field(source_sound, runtime_layouts::UObject::outer, template_outer);

    auto* const exe_base = reinterpret_cast<uint8_t*>(g_exe_module);
    auto* const fname_ctor = reinterpret_cast<FNameCtorFn>(exe_base + rva::FNameCtor);
    auto* const create_package = rva::CreatePackage
        ? reinterpret_cast<CreatePackageFn>(exe_base + rva::CreatePackage)
        : nullptr;
    auto* const construct_object = rva::StaticConstructObject
        ? reinterpret_cast<StaticConstructObjectFn>(exe_base + rva::StaticConstructObject)
        : nullptr;

    FNameValue alias_name{};
    if (!construct_fname(fname_ctor, L"bgm_piano_09", 0, alias_name)
        && !construct_fname(fname_ctor, L"bgm_piano_09", 1, alias_name)) {
        core::log(core::LogLevel::Error, "[audio_sead] alias_construct status=fname_failed");
        return nullptr;
    }

    void* package = create_package_seh(create_package, L"/Game/Sound/BGM/bgm_piano_09");
    const char* package_source = "create_package";
    if (!package) {
        package = template_outer;
        package_source = "template_outer";
    }
    if (!package) {
        core::log(core::LogLevel::Error, "[audio_sead] alias_construct status=package_failed");
        return nullptr;
    }

    FStaticConstructObjectParametersLocal params{};
    params.object_class = object_class;
    params.outer = package;
    params.name = alias_name;
    params.set_flags = 0x00000003;
    params.object_template = source_sound;

    void* constructed = static_construct_object_seh(construct_object, params);
    std::ostringstream out;
    out << "[audio_sead] alias_construct status=" << (constructed ? "ok" : "failed")
        << " source_sound=0x" << std::hex << reinterpret_cast<uintptr_t>(source_sound)
        << " class=0x" << reinterpret_cast<uintptr_t>(object_class)
        << " package=0x" << reinterpret_cast<uintptr_t>(package)
        << " object=0x" << reinterpret_cast<uintptr_t>(constructed)
        << std::dec
        << " package_source=" << package_source;
    core::log(constructed ? core::LogLevel::Info : core::LogLevel::Error, out.str());

    if (constructed) {
        uintptr_t expected = 0;
        if (!g_alias_music.compare_exchange_strong(expected, reinterpret_cast<uintptr_t>(constructed), std::memory_order_acq_rel)) {
            constructed = reinterpret_cast<void*>(expected);
        }
    }
    return constructed;
}

bool arm_aggregate_rollback_slot(
    const AudioFieldPatch& patch, const AggregateRollbackAuthority& authority,
    int32_t& slot_out, uint64_t& generation_out) noexcept
{
    slot_out = -1;
    generation_out = 0;
    for (size_t i = 0; i < g_bgm_aggregate_rollback_slots.size(); ++i) {
        auto& slot = g_bgm_aggregate_rollback_slots[i];
        auto expected = AggregateRollbackSlotState::Free;
        if (!slot.state.compare_exchange_strong(expected,
                AggregateRollbackSlotState::Writing,
                std::memory_order_acq_rel)) continue;
        slot.patch = patch;
        slot.authority = authority;
        const uint64_t prior = slot.generation.load(std::memory_order_relaxed);
        if (!bgm_playback_emergency_slot_arm_allowed(true, prior)) {
            slot.patch = {};
            slot.authority = {};
            slot.state.store(AggregateRollbackSlotState::Free,
                std::memory_order_release);
            return false;
        }
        generation_out = prior + 1;
        slot.generation.store(generation_out, std::memory_order_relaxed);
        slot.state.store(
            AggregateRollbackSlotState::Armed, std::memory_order_release);
        slot_out = static_cast<int32_t>(i);
        return true;
    }
    return false;
}

bool aggregate_rollback_slot_matches(
    const int32_t index, const uint64_t generation) noexcept
{
    return index >= 0
        && static_cast<size_t>(index) < g_bgm_aggregate_rollback_slots.size()
        && generation != 0
        && g_bgm_aggregate_rollback_slots[static_cast<size_t>(index)]
               .generation.load(std::memory_order_acquire) == generation;
}

void release_aggregate_rollback_slot(
    const int32_t index, const uint64_t generation) noexcept
{
    if (!aggregate_rollback_slot_matches(index, generation)) return;
    auto& slot = g_bgm_aggregate_rollback_slots[static_cast<size_t>(index)];
    slot.patch = {};
    slot.authority = {};
    slot.state.store(
        AggregateRollbackSlotState::Free, std::memory_order_release);
}

void retain_aggregate_rollback_slot(
    const int32_t index, const uint64_t generation) noexcept
{
    if (!aggregate_rollback_slot_matches(index, generation)) return;
    g_audio_route_disabled.store(true, std::memory_order_release);
    g_bgm_aggregate_rollback_slots[static_cast<size_t>(index)].state.store(
        AggregateRollbackSlotState::Retained, std::memory_order_release);
    g_bgm_aggregate_retained_rollback.store(true, std::memory_order_release);
}

bool validate_aggregate_rollback_authority_noexcept(
    const AggregateRollbackAuthority& authority) noexcept
{
    if (authority.kind == AggregateRollbackAuthorityKind::None
        || !authority.field_object || !authority.sound
        || !authority.controller || !authority.slot || !authority.bgm
        || authority.ordinal == 0 || authority.record_version == 0
        || authority.operation_nonce == 0 || authority.route_generation == 0
        || authority.lifecycle_state_epoch == 0
        || authority.token_ordinal == 0 || authority.canonical_token == 0
        || authority.custom_token == 0
        || authority.canonical_token == authority.custom_token
        || authority.expected_value == authority.candidate_value) return false;
    try {
        void* route_controller = nullptr;
        uint64_t route_generation = 0;
        AudioRouteLeaseIdentity route_lease{};
        OnMemoryBankDetachedRecord lifecycle;
        uint64_t lifecycle_epoch = 0;
        bool lifecycle_failed = true;
        bool release_in_flight = true;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            route_controller = g_audio_route_state.controller;
            route_generation = g_audio_route_state.generation;
            route_lease = g_audio_route_state.lease_identity;
            lifecycle = g_onmemory_bank_lifecycle.active();
            lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
            lifecycle_failed = g_onmemory_bank_lifecycle.failed();
            release_in_flight = g_onmemory_bank_lifecycle.release_in_flight();
        }
        if (route_controller != authority.controller
            || route_generation != authority.route_generation
            || !(route_lease == authority.lease)
            || !lifecycle
            || lifecycle.phase != OnMemoryBankLifecyclePhase::RestoreApplied
            || lifecycle_failed || release_in_flight
            || lifecycle_epoch != authority.lifecycle_state_epoch
            || lifecycle.ordinal != authority.token_ordinal
            || lifecycle.canonical.encode() != authority.canonical_token
            || lifecycle.custom.encode() != authority.custom_token
            || lifecycle.sound.object != authority.lifecycle_sound
            || lifecycle.sound.live.internal_index
                != authority.lifecycle_sound_identity.internal_index
            || lifecycle.sound.live.serial_number
                != authority.lifecycle_sound_identity.serial_number) return false;

        bool record_exact = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            if (g_bgm_playback_collection_version
                != authority.collection_version
                || g_bgm_playback_operation_nonce != authority.operation_nonce) {
                return false;
            }
            for (const auto& record : g_bgm_playback_borrowers) {
                if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                        record.failed, record.terminal_callback_retired)
                    || record.ordinal != authority.ordinal
                    || record.version != authority.record_version
                    || record.controller != authority.controller
                    || record.slot != authority.slot || record.bgm != authority.bgm
                    || record.owner != authority.owner
                    || record.owner_thread_id != authority.owner_thread) continue;
                if (authority.kind == AggregateRollbackAuthorityKind::CustomSet) {
                    record_exact = record.canonical_stop_boundary_active
                        && !record.canonical_stop_boundary_consumed;
                } else if (authority.kind
                    == AggregateRollbackAuthorityKind::CustomPlay) {
                    record_exact = record.transition_set_pending
                        && record.transition_set_custom
                        && record.transition_set_nonce == authority.operation_nonce
                        && record.transition_set_handle == authority.request;
                } else {
                    record_exact = record.transition_set_pending
                        && !record.transition_set_custom
                        && record.transition_set_nonce == authority.operation_nonce
                        && record.transition_set_handle == authority.request;
                }
                break;
            }
        }
        if (!record_exact || authority.owner != g_piano_audio_owner
            || authority.owner_thread != GetCurrentThreadId()) return false;
        uint64_t command = 0;
        if (!core::safe_read_field(authority.owner,
                runtime_layouts::PianoAudioOwner::packed_key, command)
            || command != authority.owner_command
            || !controller_identity_proof_matches_live(
                authority.controller_proof, authority.controller)) return false;
        void* slot = nullptr;
        void* bgm = nullptr;
        if (!read_controller_bgm_chain(authority.controller, slot, bgm)
            || slot != authority.slot || bgm != authority.bgm) return false;
        UObjectIdentity identity;
        if (!read_uobject_identity(authority.sound, identity)
            || identity.live.internal_index != authority.field_identity.internal_index
            || identity.live.serial_number != authority.field_identity.serial_number) {
            return false;
        }
        if (authority.kind == AggregateRollbackAuthorityKind::CustomSet
            && authority.request_proof == AggregateRollbackRequestProof::PreSet
            && authority.request == 0 && authority.request_state == 0) {
            return bgm_playback_rollback_authority_exact({
                true, true, true, true, true, true,
                true, true, true, true, true});
        }
        if (authority.request_proof
            != AggregateRollbackRequestProof::ExactPostSet
            || authority.request == 0
            || !AudioBgmRequestHandle{authority.request}.valid_bgm_request()) {
            return false;
        }
        void* sound = nullptr;
        uint64_t request = 0;
        uint8_t state = 0;
        if (!core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, sound)
            || !core::safe_read_field(bgm,
                runtime_layouts::SqexSeadBgm::request_handle, request)
            || sound != authority.sound
            || request != authority.request
            || (authority.kind == AggregateRollbackAuthorityKind::CustomSet
                && (!core::safe_read_field(slot,
                        runtime_layouts::SqexSeadSlot::state, state)
                    || state != authority.request_state
                    || state != 2))) {
            return false;
        }
        return bgm_playback_rollback_authority_exact({
            true, true, true, true, true, true, true, true, true, true, true});
    } catch (...) {
        return false;
    }
}

bool strengthen_custom_set_rollback_request_noexcept(
    AggregatePatchRollback& rollback) noexcept
{
    if (!rollback.active) return true;
    if (rollback.authority.kind != AggregateRollbackAuthorityKind::CustomSet
        || rollback.authority.request_proof
            != AggregateRollbackRequestProof::PreSet
        || !aggregate_rollback_slot_matches(
            rollback.slot, rollback.generation)) return false;
    const auto invalidate = [&]() noexcept {
        rollback.authority.request_proof = AggregateRollbackRequestProof::None;
        if (!aggregate_rollback_slot_matches(
                rollback.slot, rollback.generation)) return;
        auto& emergency = g_bgm_aggregate_rollback_slots[
            static_cast<size_t>(rollback.slot)];
        emergency.authority.request_proof = AggregateRollbackRequestProof::None;
    };
    try {
        void* slot = nullptr;
        void* bgm = nullptr;
        void* sound = nullptr;
        uint64_t request = 0;
        uint8_t state = 0;
        UObjectIdentity identity;
        if (!read_controller_bgm_chain(
                rollback.authority.controller, slot, bgm)
            || slot != rollback.authority.slot
            || bgm != rollback.authority.bgm
            || !core::safe_read_field(
                slot, runtime_layouts::SqexSeadSlot::state, state)
            || state != 2
            || !core::safe_read_field(
                bgm, runtime_layouts::SqexSeadBgm::sound, sound)
            || sound != rollback.authority.sound
            || !core::safe_read_field(bgm,
                runtime_layouts::SqexSeadBgm::request_handle, request)
            || !read_uobject_identity(sound, identity)
            || identity.live.internal_index
                != rollback.authority.field_identity.internal_index
            || identity.live.serial_number
                != rollback.authority.field_identity.serial_number
            || !bgm_playback_custom_set_post_request_exact(
                true, request != 0,
                AudioBgmRequestHandle{request}.valid_bgm_request(),
                state == 2, true)) {
            invalidate();
            return false;
        }
        auto strengthened = rollback.authority;
        strengthened.request = request;
        strengthened.request_proof =
            AggregateRollbackRequestProof::ExactPostSet;
        strengthened.request_state = state;
        auto& emergency = g_bgm_aggregate_rollback_slots[
            static_cast<size_t>(rollback.slot)];
        if (emergency.state.load(std::memory_order_acquire)
                != AggregateRollbackSlotState::Armed
            || emergency.generation.load(std::memory_order_acquire)
                != rollback.generation) {
            invalidate();
            return false;
        }
        emergency.authority = strengthened;
        rollback.authority = strengthened;
        return true;
    } catch (...) {
        invalidate();
        return false;
    }
}

AggregatePatchCleanupResult cleanup_aggregate_patch_noexcept(
    AggregatePatchRollback& rollback, uint64_t& current_out) noexcept
{
    current_out = 0;
    if (!rollback.active) return AggregatePatchCleanupResult::None;
    const auto& patch = rollback.patch;
    if (rollback.authority.field_object != patch.object
        || rollback.authority.expected_value != patch.original
        || rollback.authority.candidate_value != patch.replacement
        || !aggregate_rollback_slot_matches(
            rollback.slot, rollback.generation)) {
        retain_aggregate_rollback_slot(rollback.slot, rollback.generation);
        rollback.active = false;
        return AggregatePatchCleanupResult::Retained;
    }
    const auto result = bgm_playback_protected_rollback_cleanup(
        patch.original, patch.replacement,
        [&](uint64_t& current) noexcept {
            return patch.size == sizeof(uint32_t)
                ? read_field_u32_as_u64(patch.object, patch.offset, current)
                : read_field_u64(patch.object, patch.offset, current);
        },
        [&]() noexcept {
            return validate_aggregate_rollback_authority_noexcept(
                rollback.authority);
        },
        [&](const uint64_t expected) noexcept {
            AudioFieldPatch restore = patch;
            restore.replacement = expected;
            return write_field_patch(restore)
                && verify_field_value(patch, expected);
        }, current_out);
    if (result == BgmPlaybackProtectedRollbackResult::NativeOverwrite) {
        release_aggregate_rollback_slot(rollback.slot, rollback.generation);
        rollback.active = false;
        return AggregatePatchCleanupResult::NativeOverwrite;
    }
    if (result == BgmPlaybackProtectedRollbackResult::Restored) {
        release_aggregate_rollback_slot(rollback.slot, rollback.generation);
        rollback.active = false;
        return AggregatePatchCleanupResult::Restored;
    }
    retain_aggregate_rollback_slot(rollback.slot, rollback.generation);
    rollback.active = false;
    return AggregatePatchCleanupResult::Retained;
}

void flush_aggregate_rollback_slots_best_effort() noexcept
{
    for (auto& slot : g_bgm_aggregate_rollback_slots) {
        auto expected = AggregateRollbackSlotState::Retained;
        if (!slot.state.compare_exchange_strong(expected,
                AggregateRollbackSlotState::Flushing,
                std::memory_order_acq_rel)) continue;
        try {
            const AggregateDurableRollback retained{
                slot.patch, slot.authority,
                slot.generation.load(std::memory_order_acquire)};
            {
                std::lock_guard<std::mutex> lock(
                    g_bgm_aggregate_durable_rollback_mutex);
                g_bgm_aggregate_durable_rollbacks.push_back(retained);
            }
            {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                g_audio_route_state.list_cleanup_pending = true;
                (void)g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::NativeClearUnverified,
                    g_audio_route_state.lease_identity);
            }
            slot.patch = {};
            slot.authority = {};
            slot.state.store(
                AggregateRollbackSlotState::Free, std::memory_order_release);
        } catch (...) {
            slot.state.store(
                AggregateRollbackSlotState::Retained,
                std::memory_order_release);
        }
    }
}

bool aggregate_rollback_ownership_clear() noexcept
{
    bool all_slots_free = true;
    for (const auto& slot : g_bgm_aggregate_rollback_slots) {
        if (slot.state.load(std::memory_order_acquire)
            != AggregateRollbackSlotState::Free) {
            all_slots_free = false;
            break;
        }
    }
    try {
        std::lock_guard<std::mutex> lock(
            g_bgm_aggregate_durable_rollback_mutex);
        return bgm_playback_emergency_init_allowed(
            all_slots_free, g_bgm_aggregate_durable_rollbacks.empty());
    } catch (...) {
        return false;
    }
}

bool process_aggregate_rollbacks_after_drain() noexcept
{
    for (size_t i = 0; i < g_bgm_aggregate_rollback_slots.size(); ++i) {
        auto& slot = g_bgm_aggregate_rollback_slots[i];
        auto state = slot.state.load(std::memory_order_acquire);
        if (state != AggregateRollbackSlotState::Armed
            && state != AggregateRollbackSlotState::Retained) continue;
        if (!slot.state.compare_exchange_strong(state,
                AggregateRollbackSlotState::Flushing,
                std::memory_order_acq_rel)) continue;
        AggregatePatchRollback rollback{slot.patch, slot.authority,
            static_cast<int32_t>(i),
            slot.generation.load(std::memory_order_acquire), true};
        uint64_t current = 0;
        (void)cleanup_aggregate_patch_noexcept(rollback, current);
    }
    flush_aggregate_rollback_slots_best_effort();

    for (;;) {
        AggregateDurableRollback retained;
        bool found = false;
        try {
            std::lock_guard<std::mutex> lock(
                g_bgm_aggregate_durable_rollback_mutex);
            if (!g_bgm_aggregate_durable_rollbacks.empty()) {
                retained = g_bgm_aggregate_durable_rollbacks.back();
                found = true;
            }
        } catch (...) {
            break;
        }
        if (!found) break;
        uint64_t current = 0;
        const bool read = retained.patch.size == sizeof(uint32_t)
            ? read_field_u32_as_u64(retained.patch.object,
                retained.patch.offset, current)
            : read_field_u64(retained.patch.object,
                retained.patch.offset, current);
        bool resolved = read && current != retained.patch.replacement;
        if (read && current == retained.patch.replacement
            && validate_aggregate_rollback_authority_noexcept(
                retained.authority)) {
            AudioFieldPatch restore = retained.patch;
            restore.replacement = retained.patch.original;
            resolved = write_field_patch(restore)
                && verify_field_value(retained.patch, retained.patch.original);
        }
        if (!resolved) break;
        try {
            std::lock_guard<std::mutex> lock(
                g_bgm_aggregate_durable_rollback_mutex);
            if (!g_bgm_aggregate_durable_rollbacks.empty()
                && g_bgm_aggregate_durable_rollbacks.back().generation
                    == retained.generation
                && g_bgm_aggregate_durable_rollbacks.back().patch.object
                    == retained.patch.object) {
                g_bgm_aggregate_durable_rollbacks.pop_back();
            }
        } catch (...) {
            break;
        }
    }
    const bool clear = aggregate_rollback_ownership_clear();
    g_bgm_aggregate_retained_rollback.store(!clear,
        std::memory_order_release);
    return clear;
}

void log_pause_resume_bank_marker(const PauseResumeBankMarker& marker) noexcept
{
    if (!marker.eligible
        || !pause_resume_marker_log_admitted(
            g_pause_resume_bank_logs.fetch_add(1, std::memory_order_relaxed))) return;
    try {
        const char* status = "failed";
        switch (marker.status) {
        case PauseResumeBankMarkerStatus::SuspendedReady: status = "suspended_ready"; break;
        case PauseResumeBankMarkerStatus::SetRebound: status = "set_rebound"; break;
        case PauseResumeBankMarkerStatus::PlayResumed: status = "play_resumed"; break;
        case PauseResumeBankMarkerStatus::CanonicalRestored: status = "canonical_restored"; break;
        case PauseResumeBankMarkerStatus::ExitPending: status = "exit_pending"; break;
        case PauseResumeBankMarkerStatus::ReleaseRequested: status = "release_requested"; break;
        case PauseResumeBankMarkerStatus::ReleaseComplete: status = "release_complete"; break;
        case PauseResumeBankMarkerStatus::RetirementMismatch: status = "retirement_mismatch"; break;
        case PauseResumeBankMarkerStatus::Failed: break;
        }
        std::ostringstream out;
        out << "[audio_sead] pause_resume_bank status=" << status
            << " reason=" << marker.reason
            << " session=" << marker.session_ordinal
            << " cycle=" << marker.cycle_ordinal;
        core::log(marker.status == PauseResumeBankMarkerStatus::Failed
            ? core::LogLevel::Error : core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

void emit_or_batch_pause_resume_bank_marker(
    const PauseResumeBankMarker& marker) noexcept
{
    if (!marker.eligible) return;
    if (g_pause_resume_bank_marker_batch) {
        if (g_pause_resume_bank_marker_batch->count
            < std::size(g_pause_resume_bank_marker_batch->records)) {
            g_pause_resume_bank_marker_batch->records[
                g_pause_resume_bank_marker_batch->count++] = marker;
        }
        return;
    }
    log_pause_resume_bank_marker(marker);
}

PauseResumeRestorationProof restore_pause_resume_owner_patch(
    const std::vector<AudioFieldPatch>& patches) noexcept
{
    PauseResumeRestorationProof proof;
    if (patches.empty()) return proof;
    proof.performed = true;
    try {
        proof.canonical_verified = restore_patches_reverse(patches);
        if (!proof.canonical_verified) {
            g_audio_route_disabled.store(true, std::memory_order_release);
            retain_failed_patch_journal(patches);
        }
    } catch (...) {
        g_audio_route_disabled.store(true, std::memory_order_release);
        try { retain_failed_patch_journal(patches); } catch (...) {}
    }
    return proof;
}

bool read_uobject_identity(void* object, UObjectIdentity& out);

void log_bgm_aggregate_mutation(
    const char* status, const char* stage, uint64_t ordinal,
    uint64_t version, uint64_t predecessor) noexcept
{
    if (g_bgm_aggregate_mutation_logs.fetch_add(
            1, std::memory_order_relaxed) >= 96) return;
    try {
        const std::string_view status_view(status);
        const bool mutation_authorized = status_view == "owner_rebound"
            || status_view == "canonical_restored"
            || status_view == "custom_play_confirmed"
            || status_view == "release_inflight"
            || status_view == "release_committed"
            || status_view == "release_failed_retained"
            || status_view == "exit_release_claimed"
            || status_view == "exit_cleanup_complete"
            || status_view == "aggregate_awaiting"
            || status_view == "patch_failed"
            || status_view == "restore_failed"
            || status_view == "set_request_strengthening_failed";
        std::ostringstream out;
        out << "[audio_sead] aggregate_resume status=" << status
            << " stage=" << stage
            << " ordinal=" << ordinal
            << " version=" << version
            << " predecessor_nonce=" << predecessor
            << " native_forwarding=exact_once"
            << " mutation_authorized=" << (mutation_authorized ? 1 : 0);
        core::log(status_view == "owner_rebound"
                || status_view == "canonical_restored"
                || status_view == "custom_play_confirmed"
                || status_view == "release_inflight"
                || status_view == "exit_release_claimed"
                || status_view == "exit_cleanup_complete"
            ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    } catch (...) {
    }
}

bool prepare_bgm_aggregate_owner_patch(
    void* controller, void* sound, bool play,
    AggregatePatchRollback& rollback,
    bool* owner_patch_candidate_present,
    BgmAggregateOwnerPatchLogProposal* log_proposal) noexcept
{
    rollback = {};
    if (log_proposal) *log_proposal = {};
    if (owner_patch_candidate_present) {
        *owner_patch_candidate_present = false;
    }
    try {
        OnMemoryBankDetachedRecord lifecycle;
        AudioRouteState route_snapshot;
        uint64_t lifecycle_state_epoch = 0;
        bool lifecycle_available = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            lifecycle = g_onmemory_bank_lifecycle.active();
            route_snapshot = g_audio_route_state;
            lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
            lifecycle_available = lifecycle
                && lifecycle.phase == OnMemoryBankLifecyclePhase::RestoreApplied
                && !g_onmemory_bank_lifecycle.failed()
                && !g_onmemory_bank_lifecycle.release_in_flight();
        }

        uint64_t ordinal = 0;
        uint64_t version = 0;
        uint64_t predecessor = 0;
        uint64_t canonical = 0;
        uint64_t custom = 0;
        uint64_t expected_lifecycle_epoch = 0;
        uint64_t expected_route_generation = 0;
        AudioRouteLeaseIdentity expected_lease;
        void* expected_owner = nullptr;
        uint32_t expected_thread = 0;
        uint64_t expected_command = 0;
        void* expected_sound = sound;
        UObjectLiveHandle expected_identity{};
        ControllerIdentityProof expected_controller_proof{};
        void* expected_slot = nullptr;
        void* expected_bgm = nullptr;
        uint64_t expected_collection_version = 0;
        uint64_t expected_operation_nonce = 0;
        uint64_t expected_token_ordinal = 0;
        uint64_t expected_request = 0;
        bool exact_record = false;
        bool intended_command_exact = !play;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            if (!play) {
                const auto& candidate = g_bgm_playback_set_observation_candidate;
                exact_record = candidate.active
                    && candidate.from_canonical_stop_boundary
                    && candidate.controller == controller
                    && candidate.requested_sound == sound
                    && sound != nullptr;
                if (exact_record) {
                    BgmPlaybackBorrowerRecord* record = nullptr;
                    for (auto& current : g_bgm_playback_borrowers) {
                        if (current.active && !current.failed
                            && !current.list_exit && !current.shutdown
                            && current.ordinal == candidate.ordinal) {
                            record = &current;
                            break;
                        }
                    }
                    exact_record = record
                        && record->version == candidate.version
                        && record->canonical_stop_boundary_active
                        && !record->canonical_stop_boundary_consumed
                        && record->canonical_stop_boundary_nonce
                            == candidate.boundary_nonce
                        && g_bgm_playback_operation_nonce != UINT64_MAX
                        && bgm_playback_operation_nonce_next(
                            candidate.operation_predecessor_nonce,
                            g_bgm_playback_operation_nonce + 1);
                    if (record) {
                        expected_thread = record->owner_thread_id;
                        expected_controller_proof = record->controller_proof;
                    }
                    ordinal = candidate.ordinal;
                    version = candidate.version;
                    predecessor = candidate.operation_predecessor_nonce;
                    canonical = candidate.canonical_token;
                    custom = candidate.custom_token;
                    expected_lifecycle_epoch = candidate.lifecycle_state_epoch;
                    expected_route_generation =
                        candidate.route_generation == UINT64_MAX
                        ? 0 : candidate.route_generation + 1;
                    expected_lease = candidate.lease;
                    expected_identity = candidate.requested_sound_identity;
                    expected_owner = candidate.owner;
                    expected_command = candidate.owner_command;
                    expected_slot = candidate.slot;
                    expected_bgm = candidate.bgm;
                    expected_collection_version = candidate.collection_version;
                    expected_operation_nonce = g_bgm_playback_operation_nonce;
                    expected_token_ordinal = candidate.token_epoch;
                }
            } else {
                BgmPlaybackBorrowerRecord* target = nullptr;
                for (auto& record : g_bgm_playback_borrowers) {
                    if (!bgm_playback_aggregate_callback_match_eligible(record.active,
                            record.failed, record.terminal_callback_retired)
                        || record.list_exit
                        || record.shutdown
                        || record.controller != controller
                        || !record.transition_set_pending
                        || !record.transition_set_custom
                        || !record.transition_set_owner_rebound) continue;
                    if (!target || record.ordinal > target->ordinal) target = &record;
                }
                if (target && owner_patch_candidate_present) {
                    *owner_patch_candidate_present = true;
                }
                if (target) {
                    exact_record = target->transition_set_nonce != 0
                        && target->transition_set_nonce
                            == g_bgm_playback_operation_nonce
                        && bgm_playback_custom_play_rollback_request_exact(
                            target->transition_set_handle,
                            target->transition_set_handle,
                            target->transition_set_nonce)
                        && g_bgm_playback_operation_nonce != UINT64_MAX;
                    intended_command_exact =
                        bgm_playback_custom_play_intended_command_exact(
                            target->transition_set_pending,
                            target->transition_set_custom,
                            target->transition_command_key,
                            target->desired_key,
                            target->anchored_custom_command);
                    ordinal = target->ordinal;
                    version = target->version;
                    predecessor = target->transition_set_nonce;
                    canonical = target->transition_canonical_token;
                    custom = target->transition_custom_token;
                    expected_lifecycle_epoch =
                        target->transition_lifecycle_state_epoch;
                    expected_route_generation = target->transition_route_generation;
                    expected_lease = target->transition_lease;
                    expected_sound = target->transition_set_sound;
                    expected_identity = target->transition_set_sound_identity;
                    expected_owner = target->owner;
                    expected_thread = target->owner_thread_id;
                    expected_command = target->transition_command_key;
                    expected_controller_proof = target->controller_proof;
                    expected_slot = target->slot;
                    expected_bgm = target->bgm;
                    expected_collection_version = g_bgm_playback_collection_version;
                    expected_operation_nonce = g_bgm_playback_operation_nonce;
                    expected_token_ordinal = target->transition_token_epoch;
                    expected_request = target->transition_set_handle;
                }
            }
        }

        UObjectIdentity live_identity;
        uint64_t owner_token = 0;
        void* live_owner = g_piano_audio_owner;
        uint64_t live_command = 0;
        const bool owner_binding_exact = live_owner == expected_owner
            && expected_thread == GetCurrentThreadId() && live_owner;
        const bool owner_command_exact = owner_binding_exact
            && (play ? intended_command_exact
                     : (core::safe_read_field(live_owner,
                            runtime_layouts::PianoAudioOwner::packed_key,
                            live_command)
                         && live_command == expected_command));
        const bool exact = bgm_playback_aggregate_mutation_exact({
                exact_record,
                exact_record,
                exact_record,
                route_snapshot.controller == controller
                    && route_snapshot.aggregate_awaiting_transition
                    && route_snapshot.generation == expected_route_generation
                    && route_snapshot.lease_identity == expected_lease,
                lifecycle_available
                    && lifecycle_state_epoch == expected_lifecycle_epoch,
                lifecycle_available
                    && lifecycle.canonical.encode() == canonical
                    && lifecycle.custom.encode() == custom,
                expected_sound != nullptr,
                owner_command_exact,
                true,
                true,
                exact_record})
            && expected_sound != nullptr
            && read_uobject_identity(expected_sound, live_identity)
            && live_identity.live.internal_index == expected_identity.internal_index
            && live_identity.live.serial_number == expected_identity.serial_number
            && lifecycle.sound.object == expected_sound
            && lifecycle.sound.live.internal_index == expected_identity.internal_index
            && lifecycle.sound.live.serial_number == expected_identity.serial_number
            && lifecycle.canonical.encode() == canonical
            && lifecycle.custom.encode() == custom
            && lifecycle_state_epoch == expected_lifecycle_epoch
            && core::safe_read_field(expected_sound,
                runtime_layouts::SqexSeadSound::observed_field548, owner_token)
            && owner_token == canonical && canonical != 0 && custom != 0
            && canonical != custom;
        const auto capture_log = [&](const BgmAggregateOwnerPatchLogStatus status,
                                     const bool patch_planned,
                                     const bool rollback_armed,
                                     const bool gate_open,
                                     const bool write_exact,
                                     const bool rollback_cleanup_attempted) {
            if (!log_proposal) return;
            log_proposal->status = status;
            log_proposal->play = play;
            log_proposal->ordinal = ordinal;
            log_proposal->version = version;
            log_proposal->predecessor = predecessor;
            log_proposal->route_generation = expected_route_generation;
            log_proposal->lifecycle_epoch = expected_lifecycle_epoch;
            log_proposal->sound_index = expected_identity.internal_index;
            log_proposal->sound_serial = expected_identity.serial_number;
            log_proposal->exact_record = exact_record;
            log_proposal->lifecycle_available = lifecycle_available;
            log_proposal->owner_binding_exact = owner_binding_exact;
            log_proposal->owner_command_exact = owner_command_exact;
            log_proposal->patch_planned = patch_planned;
            log_proposal->rollback_armed = rollback_armed;
            log_proposal->gate_open = gate_open;
            log_proposal->write_exact = write_exact;
            log_proposal->rollback_cleanup_attempted =
                rollback_cleanup_attempted;
            log_proposal->mutation_complete = true;
        };
        if (!exact) {
            if (exact_record) capture_log(
                BgmAggregateOwnerPatchLogStatus::AuthorizationBlocked,
                false, false, false, false, false);
            return false;
        }

        std::vector<AudioFieldPatch> requested;
        if (!append_patch(requested, expected_sound,
                runtime_layouts::SqexSeadSound::observed_field548,
                custom, sizeof(uint64_t), "aggregate_borrower_owner", true)
            || requested.size() != 1 || requested.front().original != canonical
            ) {
            capture_log(BgmAggregateOwnerPatchLogStatus::PatchFailed,
                false, false, false, false, false);
            return false;
        }
        rollback.patch = requested.front();
        rollback.authority = {
            play ? AggregateRollbackAuthorityKind::CustomPlay
                 : AggregateRollbackAuthorityKind::CustomSet,
            expected_sound,
            expected_sound,
            expected_identity,
            expected_sound,
            expected_identity,
            controller,
            expected_controller_proof,
            expected_slot,
            expected_bgm,
            ordinal,
            version,
            expected_collection_version,
            expected_operation_nonce,
            expected_route_generation,
            expected_lease,
            expected_lifecycle_epoch,
            expected_token_ordinal,
            canonical,
            custom,
            expected_owner,
            expected_thread,
            expected_command,
            play ? expected_request : 0,
            play ? AggregateRollbackRequestProof::ExactPostSet
                 : AggregateRollbackRequestProof::PreSet,
            play ? uint8_t{2} : uint8_t{0},
            canonical,
            custom};
        if (!arm_aggregate_rollback_slot(rollback.patch, rollback.authority,
                rollback.slot, rollback.generation)) {
            capture_log(BgmAggregateOwnerPatchLogStatus::PatchFailed,
                true, false, false, false, false);
            return false;
        }
        rollback.active = true;
        bool write_exact = false;
        const bool gate_open = g_bgm_aggregate_mutation_gate.while_open([&] {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            BgmPlaybackBorrowerRecord* current_record = nullptr;
            for (auto& current : g_bgm_playback_borrowers) {
                if (current.active && current.ordinal == ordinal) {
                    current_record = &current;
                    break;
                }
            }
            const bool final_authority = current_record
                && !g_bgm_aggregate_exit_requested.load(
                    std::memory_order_acquire)
                && !g_bgm_aggregate_exit_pending.load(
                    std::memory_order_acquire)
                && !current_record->list_exit && !current_record->shutdown
                && !current_record->failed
                && current_record->version == version
                && g_bgm_playback_collection_version
                    == expected_collection_version
                && g_bgm_playback_operation_nonce == expected_operation_nonce;
            write_exact = final_authority
                && write_field_patch(rollback.patch)
                && verify_field_value(rollback.patch, custom);
        });
        if (!gate_open || !write_exact) {
            uint64_t current = 0;
            (void)cleanup_aggregate_patch_noexcept(rollback, current);
            flush_aggregate_rollback_slots_best_effort();
            capture_log(BgmAggregateOwnerPatchLogStatus::PatchFailed,
                true, true, gate_open, write_exact, true);
            return false;
        }
        return true;
    } catch (...) {
        uint64_t current = 0;
        (void)cleanup_aggregate_patch_noexcept(rollback, current);
        flush_aggregate_rollback_slots_best_effort();
        return false;
    }
}

void publish_bgm_aggregate_native_cleanup(
    const AggregatePatchCleanupResult result,
    const AggregateMutationReleaseWitness& witness) noexcept
{
    if (!witness.released()) return;
    if (result != AggregatePatchCleanupResult::None) {
        log_bgm_aggregate_mutation(
            "owner_rebound", "native_callback", 0, 0, 0);
        const char* status = "restore_failed";
        if (result == AggregatePatchCleanupResult::Restored) {
            status = "canonical_restored";
        } else if (result
            == AggregatePatchCleanupResult::NativeOverwrite) {
            status = "native_overwrite";
        }
        log_bgm_aggregate_mutation(status, "native_callback", 0, 0, 0);
    }
}

bool restore_pause_resume_owner(
    PauseResumeBankMarker* marker = nullptr,
    bool preserve_owner_rebound_phase = false) noexcept
{
    AudioFieldPatch patch;
    uint64_t session = 0;
    uint64_t cycle = 0;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_pause_resume_bank.phase != PauseResumeBankPhase::OwnerRebound
            || !g_pause_resume_bank.owner_patch.object) return true;
        patch = g_pause_resume_bank.owner_patch;
        session = g_pause_resume_bank.session_epoch;
        cycle = g_pause_resume_bank.cycle_epoch;
    }
    const bool restored = static_cast<bool>(
        restore_pause_resume_owner_patch(std::vector<AudioFieldPatch>{patch}));
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_pause_resume_bank.phase == PauseResumeBankPhase::OwnerRebound) {
            g_pause_resume_bank.phase = restored && preserve_owner_rebound_phase
                ? PauseResumeBankPhase::OwnerRebound
                : restored ? PauseResumeBankPhase::ResumedActive
                           : PauseResumeBankPhase::Failed;
            g_pause_resume_bank.owner_patch = {};
        }
    }
    if (marker) {
        *marker = {restored ? PauseResumeBankMarkerStatus::CanonicalRestored
                            : PauseResumeBankMarkerStatus::Failed,
            restored ? "exact_owner" : "restore_failed", session, cycle, true};
    }
    return restored;
}

bool apply_patches(const std::vector<AudioFieldPatch>& patches, std::vector<AudioFieldPatch>& applied, const char*& failed_label)
{
    applied.clear();
    if (patches.empty()) return true;
    ActiveNativePatchScope patch_scope;
    for (const AudioFieldPatch& patch : patches) {
        if (!write_field_patch(patch)) {
            failed_label = patch.label ? patch.label : "?";
            if (restore_patches_reverse(applied)) {
                applied.clear();
            } else {
                g_audio_route_disabled.store(true, std::memory_order_release);
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                g_failed_patch_journal.insert(g_failed_patch_journal.end(), applied.begin(), applied.end());
                applied.clear();
            }
            return false;
        }
        applied.push_back(patch);
        if (!verify_field_value(patch, patch.replacement)) {
            failed_label = patch.label ? patch.label : "?";
            if (restore_patches_reverse(applied)) {
                applied.clear();
            } else {
                g_audio_route_disabled.store(true, std::memory_order_release);
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                g_failed_patch_journal.insert(g_failed_patch_journal.end(), applied.begin(), applied.end());
                applied.clear();
            }
            return false;
        }
    }
    return true;
}

bool route_bgm_prepare_once(void* bgm, void* expected_bgm, void* expected_sound,
    const SongDescriptor& song, const SidecarRuntimeState& sidecar)
{
    void* source_sound = nullptr;
    if (!core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, source_sound)
        || !bgm_prepare_identity_matches(expected_bgm, expected_sound, bgm, source_sound)) {
        return false;
    }
    // The constructed alias object can produce an incomplete descriptor on this build
    // (desc8/desc10 are zero). Patch the live source sound for this prepare call only,
    // then restore it immediately after the original BGMPrepare returns.
    void* alias_music = source_sound;
    capture_native_mabf_once(alias_music);

    auto* const descriptor_build = reinterpret_cast<DescriptorBuildFn>(reinterpret_cast<uint8_t*>(g_exe_module) + rva::SeadDescriptorBuild);
    uint64_t descriptor[3]{};

    std::vector<AudioFieldPatch> patches;
    patches.reserve(14);
    if (!append_patch(patches, alias_music, runtime_layouts::SqexSeadSound::mabf_source, reinterpret_cast<uintptr_t>(sidecar.allocation.sead_header()), sizeof(uint64_t), "alias+0x38", true)
        || !append_patch(patches, alias_music, runtime_layouts::SqexSeadSound::observed_field398, 0, sizeof(uint64_t), "alias+0x398", true)
        || !append_patch(patches, alias_music, runtime_layouts::SqexSeadSound::observed_field41c, 1, sizeof(uint32_t), "alias+0x41c")
        || !append_patch(patches, alias_music, runtime_layouts::SqexSeadSound::observed_field420, 0, sizeof(uint64_t), "alias+0x420", true)
        || !append_patch(patches, alias_music, runtime_layouts::SqexSeadSound::observed_field548, 0, sizeof(uint64_t), "alias+0x548", true)) {
        core::log(core::LogLevel::Error, "[audio_sead] route status=plan_failed phase=source_sound_fields");
        return false;
    }

    std::vector<AudioFieldPatch> applied_alias;
    const char* failed_label = nullptr;
    source_sound = nullptr;
    if (!core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, source_sound)
        || !bgm_prepare_identity_matches(expected_bgm, expected_sound, bgm, source_sound)) {
        return false;
    }
    if (!apply_patches(patches, applied_alias, failed_label)) {
        std::ostringstream out;
        out << "[audio_sead] route status=patch_failed phase=source_sound label=" << (failed_label ? failed_label : "?")
            << " song_id=" << song.id;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    const bool descriptor_ok = descriptor_build_seh(descriptor_build, alias_music, descriptor);
    if (!descriptor_ok) {
        const bool restored = restore_patches_reverse(applied_alias);
        if (!restored) retain_failed_patch_journal(applied_alias);
        std::ostringstream out;
        out << "[audio_sead] route status=descriptor_failed song_id=" << song.id
            << " restored=" << (restored ? 1 : 0);
        core::log(core::LogLevel::Error, out.str());
        if (!restored) {
            g_audio_route_disabled.store(true, std::memory_order_relaxed);
        }
        return false;
    }

    std::vector<AudioFieldPatch> bgm_patches;
    bgm_patches.reserve(9);
    source_sound = nullptr;
    if (!core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, source_sound)
        || !bgm_prepare_identity_matches(expected_bgm, expected_sound, bgm, source_sound)) {
        const bool restored = restore_patches_reverse(applied_alias);
        if (!restored) retain_failed_patch_journal(applied_alias);
        if (!restored) g_audio_route_disabled.store(true, std::memory_order_release);
        return false;
    }
    if (!append_patch(bgm_patches, bgm, runtime_layouts::SqexSeadBgm::sound, reinterpret_cast<uintptr_t>(alias_music), sizeof(uint64_t), "bgm+0x28", true)
        || !append_patch(bgm_patches, bgm, 0x30, descriptor[0], sizeof(uint64_t), "bgm+0x30")
        || !append_patch(bgm_patches, bgm, 0x38, descriptor[1], sizeof(uint64_t), "bgm+0x38")
        || !append_patch(bgm_patches, bgm, 0x40, descriptor[2], sizeof(uint64_t), "bgm+0x40")
        || !append_patch(bgm_patches, bgm, runtime_layouts::SqexSeadBgm::request_handle, 0, sizeof(uint64_t), "bgm+0x48")
        || !append_patch(bgm_patches, bgm, 0x58, 0, sizeof(uint64_t), "bgm+0x58")
        || !append_patch(bgm_patches, bgm, runtime_layouts::SqexSeadBgm::mode_key, 0, sizeof(uint64_t), "bgm+0x60")
        || !append_patch(bgm_patches, bgm, 0x68, 0x3f800000, sizeof(uint64_t), "bgm+0x68")
        || !append_patch(bgm_patches, bgm, runtime_layouts::SqexSeadBgm::backing_resource, 0, sizeof(uint64_t), "bgm+0x70", true)) {
        const bool restored = restore_patches_reverse(applied_alias);
        if (!restored) retain_failed_patch_journal(applied_alias);
        core::log(core::LogLevel::Error, restored ? "[audio_sead] route status=plan_failed phase=bgm_fields" : "[audio_sead] route status=plan_failed phase=bgm_fields alias_restore_failed");
        if (!restored) {
            g_audio_route_disabled.store(true, std::memory_order_relaxed);
        }
        return false;
    }

    std::vector<AudioFieldPatch> applied_bgm;
    if (!apply_patches(bgm_patches, applied_bgm, failed_label)) {
        const bool alias_restored = restore_patches_reverse(applied_alias);
        if (!alias_restored) retain_failed_patch_journal(applied_alias);
        std::ostringstream out;
        out << "[audio_sead] route status=patch_failed phase=bgm label=" << (failed_label ? failed_label : "?")
            << " song_id=" << song.id
            << " alias_restored=" << (alias_restored ? 1 : 0);
        core::log(core::LogLevel::Error, out.str());
        if (!alias_restored) {
            g_audio_route_disabled.store(true, std::memory_order_relaxed);
        }
        return false;
    }

    const bool original_ok = call_bgm_prepare_original(bgm);
    const bool bgm_restored = restore_patches_reverse(applied_bgm);
    const bool alias_restored = restore_patches_reverse(applied_alias);
    if (!bgm_restored) retain_failed_patch_journal(applied_bgm);
    if (!alias_restored) retain_failed_patch_journal(applied_alias);
    const bool ok = original_ok && bgm_restored && alias_restored;
    if (!bgm_restored || !alias_restored) {
        g_audio_route_disabled.store(true, std::memory_order_relaxed);
    }

    static std::atomic_int s_route_logs{0};
    const int log_index = s_route_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 64 || !ok) {
        std::ostringstream out;
        out << "[audio_sead] route status=" << (ok ? "prepared" : "failed")
            << " song_id=" << song.id
            << " bgm=0x" << std::hex << reinterpret_cast<uintptr_t>(bgm)
            << " source_sound=0x" << reinterpret_cast<uintptr_t>(source_sound)
            << " route_sound=0x" << reinterpret_cast<uintptr_t>(alias_music)
            << " sidecar=0x" << reinterpret_cast<uintptr_t>(sidecar.allocation.sead_header())
            << " desc0=0x" << descriptor[0]
            << " desc8=0x" << descriptor[1]
            << " desc10=0x" << descriptor[2]
            << std::dec
            << " original_ok=" << (original_ok ? 1 : 0)
            << " bgm_restored=" << (bgm_restored ? 1 : 0)
            << " source_restored=" << (alias_restored ? 1 : 0);
        core::log(ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    }
    return true;
}

bool patch_sound_for_sidecar_call(void* sound, const SongDescriptor& song, const SidecarRuntimeState& sidecar, const char* phase, std::vector<AudioFieldPatch>& applied)
{
    std::vector<AudioFieldPatch> patches;
    patches.reserve(5);
    if (!append_patch(patches, sound, runtime_layouts::SqexSeadSound::mabf_source, reinterpret_cast<uintptr_t>(sidecar.allocation.sead_header()), sizeof(uint64_t), "sound+0x38", true)
        || !append_patch(patches, sound, runtime_layouts::SqexSeadSound::observed_field398, 0, sizeof(uint64_t), "sound+0x398", true)
        || !append_patch(patches, sound, runtime_layouts::SqexSeadSound::observed_field41c, 1, sizeof(uint32_t), "sound+0x41c")
        || !append_patch(patches, sound, runtime_layouts::SqexSeadSound::observed_field420, 0, sizeof(uint64_t), "sound+0x420", true)
        || !append_patch(patches, sound, runtime_layouts::SqexSeadSound::observed_field548, 0, sizeof(uint64_t), "sound+0x548", true)) {
        std::ostringstream out;
        out << "[audio_sead] " << phase << " status=plan_failed song_id=" << song.id;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }
    const char* failed_label = nullptr;
    if (!apply_patches(patches, applied, failed_label)) {
        std::ostringstream out;
        out << "[audio_sead] " << phase << " status=patch_failed label=" << (failed_label ? failed_label : "?")
            << " song_id=" << song.id;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }
    return true;
}

bool restore_pending_play_setup_patch(
    const char* reason, bool preserve_playing_route = false,
    bool retain_journal = false,
    AudioPatchRestoreFailureReport* failure_report = nullptr,
    AudioPatchRestoreExactOverride* exact_override = nullptr)
{
    PendingPlaySetupPatch pending;
    bool restored = true;
    bool restore_safe = true;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        pending = g_pending_play_setup_patch;
        if (!pending.patches.empty()) {
            restore_safe = pending.restore_safe;
            restored = restore_safe && restore_patches_reverse(
                pending.patches, preserve_playing_route, failure_report,
                exact_override);
            if (restored) {
                if (!retain_journal) g_pending_play_setup_patch = {};
                ++g_audio_route_state.generation;
                if (g_audio_route_state.patched_song_id == pending.song_id
                    && g_audio_route_state.sound == pending.sound) {
                    g_audio_route_state.patched_song_id.clear();
                    g_audio_route_state.sound = nullptr;
                    if (!preserve_playing_route || !g_audio_route_state.controller) {
                        g_audio_route_state.controller = nullptr;
                        g_audio_route_state.controller_identity = {};
                        g_audio_route_state.phase = g_audio_route_state.desired_song_id.empty()
                            ? AudioRoutePhase::Idle
                        : AudioRoutePhase::Armed;
                    }
                }
            } else {
                g_pending_play_setup_patch.restore_safe = false;
            }
        }
    }
    if (pending.patches.empty()) {
        return true;
    }
    std::ostringstream out;
    out << "[audio_sead] play_setup_restore status="
        << (restored ? "ok" : (restore_safe ? "failed" : "skipped_lifetime_unverified"))
        << " reason=" << (reason ? reason : "?")
        << " song_id=" << pending.song_id
        << " sound=0x" << std::hex << reinterpret_cast<uintptr_t>(pending.sound)
        << std::dec
        << " patch_count=" << pending.patches.size();
    core::log(restored ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    if (!restored) {
        g_audio_route_disabled.store(true, std::memory_order_release);
    }
    return restored;
}

bool restore_failed_patch_journal(bool retain_journal = false)
{
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    if (g_failed_patch_journal.empty()) {
        return true;
    }
    const bool restored = restore_patches_reverse(g_failed_patch_journal);
    if (restored) {
        if (!retain_journal) g_failed_patch_journal.clear();
    } else {
        g_audio_route_state.list_cleanup_pending = true;
        (void)g_frozen_profile_lease.transition(
            AudioRouteCleanupEvent::NativeClearUnverified,
            g_audio_route_state.lease_identity);
        g_audio_route_disabled.store(true, std::memory_order_release);
    }
    core::log(restored ? core::LogLevel::Info : core::LogLevel::Error,
        restored
            ? "[audio_sead] failed_patch_restore status=ok"
            : "[audio_sead] failed_patch_restore status=failed cleanup=retained");
    return restored;
}

AudioCleanupEvidence audio_cleanup_evidence_locked(
    AudioCleanupOperation operation, bool hooks_disabled, bool callbacks_drained)
{
    AudioCleanupEvidence evidence;
    evidence.operation = operation;
    evidence.hooks_disabled = hooks_disabled;
    evidence.callbacks_drained = callbacks_drained;
    evidence.active_journal_empty = g_active_patch_journal.empty();
    evidence.pending_journal_empty = g_pending_play_setup_patch.patches.empty();
    evidence.failed_journal_empty = g_failed_patch_journal.empty();
    evidence.immutable_identity_matches = g_frozen_profile_lease.active()
        && g_audio_route_state.lease_identity == g_frozen_profile_lease.identity();
    const bool deferred_native_forwarded =
        g_audio_route_state.deferred_native_handoff.native_play_obligation_met();
    const auto stop_phase = g_audio_route_state.stop_retirement.phase;
    const bool stop_retirement_quiescent = stop_phase == AudioStopRetirementPhase::None
        || stop_phase == AudioStopRetirementPhase::Quiescent;
    evidence.native_clear_verified = g_audio_route_state.native_clear_verified
        && deferred_native_forwarded
        && stop_retirement_quiescent;
    evidence.route_state_unchanged = evidence.immutable_identity_matches;
    evidence.native_attempted = g_frozen_profile_lease.native_arm_attempted();
    evidence.ambiguous_route_state = g_audio_route_state.list_cleanup_pending
        || g_audio_route_state.custom_resource_owned
        || !deferred_native_forwarded
        || !stop_retirement_quiescent
        || !evidence.active_journal_empty
        || !evidence.pending_journal_empty
        || !evidence.failed_journal_empty;
    return evidence;
}

bool read_uobject_identity(void* object, UObjectIdentity& out)
{
    out = {};
    out.raw_internal_index_readable = object
        && core::safe_read_field(
            object, runtime_layouts::UObject::internal_index,
            out.raw_internal_index);
    const bool identity_read = object
        && core::safe_read_field(object, runtime_layouts::UObject::object_class, out.object_class)
        && out.object_class
        && core::safe_read_field(object, runtime_layouts::UObject::name, out.name)
        && core::safe_read_field(object, runtime_layouts::UObject::outer, out.outer);
    if (identity_read && out.raw_internal_index_readable
        && out.raw_internal_index >= 0) {
        // Some SQEXSEAD controller objects are not serial-backed GUObjectArray entries.
        // Retained source sounds require a live handle explicitly at their use sites.
        out.live_capture_attempted = true;
        out.live_capture_succeeded = capture_live_uobject_handle(
            object, out.live, out.live_capture_result);
        if (!out.live_capture_succeeded
            && out.live_capture_result
                == UObjectLiveHandleCaptureResult::SerialInvalid) {
            out.item_backed_zero_serial_capture_attempted = true;
            out.item_backed_zero_serial_capture_succeeded =
                capture_item_backed_zero_serial_uobject_snapshot(
                    object, out.item_backed_zero_serial,
                    out.item_backed_zero_serial_capture_result);
        }
    }
    return identity_read;
}

ControllerIdentityProof controller_identity_proof(
    void* controller, const UObjectIdentity& identity)
{
    return make_controller_identity_proof(
        controller, identity.object_class,
        identity.name.comparison_id, identity.name.number,
        identity.outer, identity.raw_internal_index_readable,
        identity.raw_internal_index, identity.live_capture_succeeded,
        identity.live,
        identity.item_backed_zero_serial_capture_succeeded,
        identity.item_backed_zero_serial);
}

bool read_controller_identity_proof(void* controller, ControllerIdentityProof& out)
{
    UObjectIdentity identity;
    if (!read_uobject_identity(controller, identity)) {
        out = {};
        return false;
    }
    out = controller_identity_proof(controller, identity);
    return controller_identity_proof_valid(out);
}

bool controller_identity_proof_matches_live(
    const ControllerIdentityProof& expected, void* controller)
{
    ControllerIdentityProof current;
    return read_controller_identity_proof(controller, current)
        && controller_identity_proof_matches(expected, current);
}

bool uobject_identity_matches(
    void* object,
    const UObjectIdentity& expected,
    UObjectIdentityPrefilterResult& result)
{
    UObjectIdentity current;
    const bool requires_live_handle = expected.live.internal_index >= 0
        && expected.live.serial_number > 0;
    result = evaluate_uobject_identity_prefilter(
        requires_live_handle,
        [&] { return validate_live_uobject_handle(object, expected.live); },
        [&] { return read_uobject_identity(object, current); },
        [&] {
            if (requires_live_handle
                && (current.live.internal_index != expected.live.internal_index
                    || current.live.serial_number != expected.live.serial_number)) {
                return UObjectIdentityPrefilterResult::CurrentLiveHandleChanged;
            }
            if (current.object_class != expected.object_class) {
                return UObjectIdentityPrefilterResult::ObjectClassChanged;
            }
            if (current.name.comparison_id != expected.name.comparison_id) {
                return UObjectIdentityPrefilterResult::NameComparisonIndexChanged;
            }
            if (current.name.number != expected.name.number) {
                return UObjectIdentityPrefilterResult::NameNumberChanged;
            }
            if (current.outer != expected.outer) {
                return UObjectIdentityPrefilterResult::OuterChanged;
            }
            return UObjectIdentityPrefilterResult::Passed;
        });
    return uobject_identity_prefilter_passed(result);
}

bool uobject_identity_matches(void* object, const UObjectIdentity& expected)
{
    UObjectIdentityPrefilterResult result{};
    return uobject_identity_matches(object, expected, result);
}

bool read_controller_bgm_chain(void* controller, void*& slot, void*& bgm)
{
    ControllerAudioChainReadValues values{slot, bgm};
    const ControllerAudioChainReadFailure failure =
        read_controller_bgm_chain_ordered(
            controller, values,
            [](void* object, void*& value) {
                return core::safe_read_field(object,
                    runtime_layouts::SqexSeadController::slot, value);
            },
            [](void* object, void*& value) {
                return core::safe_read_field(object,
                    runtime_layouts::SqexSeadSlot::bgm, value);
            });
    slot = values.slot;
    bgm = values.bgm;
    return failure == ControllerAudioChainReadFailure::None;
}

bool read_controller_audio_chain(
    void* controller, void*& slot, void*& bgm, void*& sound,
    uint64_t& request_handle, uint8_t& state)
{
    ControllerAudioChainReadValues values{
        slot, bgm, sound, request_handle, state};
    const ControllerAudioChainReadFailure failure =
        read_controller_audio_chain_ordered(
            controller, values,
            [](void* object, void*& value) {
                return core::safe_read_field(object,
                    runtime_layouts::SqexSeadController::slot, value);
            },
            [](void* object, void*& value) {
                return core::safe_read_field(object,
                    runtime_layouts::SqexSeadSlot::bgm, value);
            },
            [](void* object, void*& value) {
                return core::safe_read_field(object,
                    runtime_layouts::SqexSeadBgm::sound, value);
            },
            [](void* object, uint64_t& value) {
                return core::safe_read_field(object,
                    runtime_layouts::SqexSeadBgm::request_handle, value);
            },
            [](void* object, uint8_t& value) {
                return core::safe_read_field(object,
                    runtime_layouts::SqexSeadSlot::state, value);
            });
    slot = values.slot;
    bgm = values.bgm;
    sound = values.sound;
    request_handle = values.request_handle;
    state = values.state;
    return failure == ControllerAudioChainReadFailure::None;
}

#include "game/audio_borrower_lineage.inc"

void* lookup_current_bgm_controller();

GuardedPlaySetupClaimReadResult read_guarded_play_setup_claim_observation_result(
    void* bound_controller)
{
    return read_guarded_play_setup_claim_ordered(
        bound_controller,
        []() { return lookup_current_bgm_controller(); },
        [](void* controller, ControllerIdentityProof& proof) {
            return read_controller_identity_proof(controller, proof);
        },
        [](void* controller, ControllerAudioChainReadValues& values) {
            return read_controller_audio_chain_ordered(
                controller, values,
                [](void* object, void*& value) {
                    return core::safe_read_field(object,
                        runtime_layouts::SqexSeadController::slot, value);
                },
                [](void* object, void*& value) {
                    return core::safe_read_field(object,
                        runtime_layouts::SqexSeadSlot::bgm, value);
                },
                [](void* object, void*& value) {
                    return core::safe_read_field(object,
                        runtime_layouts::SqexSeadBgm::sound, value);
                },
                [](void* object, uint64_t& value) {
                    return core::safe_read_field(object,
                        runtime_layouts::SqexSeadBgm::request_handle, value);
                },
                [](void* object, uint8_t& value) {
                    return core::safe_read_field(object,
                        runtime_layouts::SqexSeadSlot::state, value);
                });
        },
        [](void* sound, UObjectLiveHandle& handle) {
            UObjectIdentity identity;
            if (!read_uobject_identity(sound, identity)) return false;
            handle = identity.live;
            return true;
        });
}

bool read_guarded_play_setup_claim_observation(
    void* bound_controller, GuardedPlaySetupClaimObservation& observation)
{
    const GuardedPlaySetupClaimReadResult result =
        read_guarded_play_setup_claim_observation_result(bound_controller);
    observation = result.observation;
    return static_cast<bool>(result);
}

AudioNativeRouteObservation observe_native_route(
    void* controller,
    const UObjectIdentity& controller_identity,
    uint64_t generation,
    const AudioRouteLeaseIdentity& lease_identity)
{
    AudioNativeRouteObservation observation;
    observation.route_generation = generation;
    observation.lease_identity = lease_identity;
    observation.valid = lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, controller_identity)
        && read_controller_bgm_chain(controller, observation.slot, observation.bgm)
        && core::safe_read_field(observation.bgm, runtime_layouts::SqexSeadBgm::sound, observation.sound)
        && core::safe_read_field(observation.bgm, runtime_layouts::SqexSeadBgm::request_handle, observation.request_handle)
        && core::safe_read_field(observation.slot, runtime_layouts::SqexSeadSlot::state, observation.state);
    return observation;
}

#include "game/audio_retirement_cleanup.inc"

void log_deferred_route_diagnostics(
    const char* stage,
    void* controller,
    const UObjectIdentity& controller_identity,
    const AudioNativeRouteObservation& current,
    const AudioDeferredNativeHandoffState& expected)
{
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint64_t request_handle = 0;
    uint8_t state = 0xff;
    const bool current_controller_matches = lookup_current_bgm_controller() == controller;
    const bool controller_identity_matches = uobject_identity_matches(
        controller, controller_identity);
    const bool chain_read = read_controller_bgm_chain(controller, slot, bgm);
    const bool sound_read = chain_read
        && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, sound);
    const bool request_handle_read = chain_read
        && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::request_handle, request_handle);
    const bool state_read = chain_read
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, state);
    std::ostringstream out;
    out << "[audio_sead] deferred_route_observation stage=" << stage
        << " valid=" << (current.valid ? 1 : 0)
        << " current_controller_matches=" << (current_controller_matches ? 1 : 0)
        << " controller_identity_matches=" << (controller_identity_matches ? 1 : 0)
        << " chain_read=" << (chain_read ? 1 : 0)
        << " sound_read=" << (sound_read ? 1 : 0)
        << " request_handle_read=" << (request_handle_read ? 1 : 0)
        << " state_read=" << (state_read ? 1 : 0)
        << " slot_matches=" << (slot == expected.slot ? 1 : 0)
        << " bgm_matches=" << (bgm == expected.bgm ? 1 : 0)
        << " requested_sound_matches=" << (sound == expected.requested_sound ? 1 : 0)
        << " request_handle_zero=" << (request_handle == 0 ? 1 : 0)
        << " state=" << static_cast<unsigned>(state)
        << " route_generation=" << current.route_generation
        << " expected_generation=" << expected.route_generation
        << " lease_generation=" << current.lease_identity.generation
        << " expected_lease_generation=" << expected.lease_identity.generation
        << " slot=0x" << std::hex << reinterpret_cast<uintptr_t>(slot)
        << " bgm=0x" << reinterpret_cast<uintptr_t>(bgm)
        << " sound=0x" << reinterpret_cast<uintptr_t>(sound)
        << " request_handle=0x" << request_handle
        << " requested_sound=0x" << reinterpret_cast<uintptr_t>(expected.requested_sound)
        << " retired_custom_request_handle=0x"
        << expected.retired_custom_request_handle
        << std::dec;
    core::log(current.valid ? core::LogLevel::Info : core::LogLevel::Error, out.str());
}

void log_frozen_sound_patch_snapshot(
    const char* stage, const FrozenSoundPatchSnapshot& snapshot)
{
    if (!snapshot.valid()) {
        return;
    }
    const bool identity_matches = uobject_identity_matches(
        snapshot.sound, snapshot.sound_identity);
    std::ostringstream out;
    out << "[audio_sead] frozen_sound_patch stage=" << stage
        << " identity_matches=" << (identity_matches ? 1 : 0)
        << " route_generation=" << snapshot.route_generation
        << " lease_generation=" << snapshot.lease_identity.generation
        << " sound=0x" << std::hex << reinterpret_cast<uintptr_t>(snapshot.sound);
    for (const AudioFieldPatch& patch : snapshot.patches) {
        uint64_t current = 0;
        const bool read_ok = identity_matches
            && (patch.size == sizeof(uint32_t)
                    ? read_field_u32_as_u64(snapshot.sound, patch.offset, current)
                    : read_field_u64(snapshot.sound, patch.offset, current));
        const char* classification = !read_ok ? "unreadable"
            : (current == patch.original ? "original"
                : (current == patch.replacement ? "replacement" : "changed"));
        out << " field_" << patch.label
            << "_read=" << std::dec << (read_ok ? 1 : 0)
            << "_class=" << classification
            << "_original=0x" << std::hex << patch.original
            << "_replacement=0x" << patch.replacement
            << "_current=0x" << current;
    }
    core::log(identity_matches ? core::LogLevel::Info : core::LogLevel::Error, out.str());
}

void log_sound_route_fields(
    const char* stage, void* sound, const UObjectIdentity& sound_identity)
{
    if (!sound) {
        return;
    }
    const bool identity_matches = uobject_identity_matches(sound, sound_identity);
    uint64_t mabf_source = 0;
    uint32_t field398 = 0;
    uint32_t field41c = 0;
    uint32_t field420 = 0;
    uint64_t field548 = 0;
    const bool read_ok = identity_matches
        && core::safe_read_field(sound, runtime_layouts::SqexSeadSound::mabf_source, mabf_source)
        && core::safe_read_field(sound, runtime_layouts::SqexSeadSound::observed_field398, field398)
        && core::safe_read_field(sound, runtime_layouts::SqexSeadSound::observed_field41c, field41c)
        && core::safe_read_field(sound, runtime_layouts::SqexSeadSound::observed_field420, field420)
        && core::safe_read_field(sound, runtime_layouts::SqexSeadSound::observed_field548, field548);
    std::ostringstream out;
    out << "[audio_sead] sound_route_fields stage=" << stage
        << " identity_matches=" << (identity_matches ? 1 : 0)
        << " read_ok=" << (read_ok ? 1 : 0)
        << " sound=0x" << std::hex << reinterpret_cast<uintptr_t>(sound)
        << " mabf_source_38=0x" << mabf_source
        << " field_398=0x" << field398
        << " field_41c=0x" << field41c
        << " field_420=0x" << field420
        << " field_548=0x" << field548 << std::dec;
    core::log(read_ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
}

void log_slot_setup_snapshot(const char* phase, void* controller)
{
    void* slot = nullptr;
    void* bgm = nullptr;
    uint8_t state = 0;
    uint8_t mode = 0;
    uint8_t flag5c = 0;
    uint8_t flag5d = 0;
    uint32_t value34 = 0;
    uint32_t value48 = 0;
    uint32_t value60 = 0;
    uint64_t value64 = 0;
    uint32_t value6c = 0;
    uint32_t value70 = 0;
    uint32_t value74 = 0;
    uint32_t value78 = 0;
    uint32_t value7c = 0;
    uint32_t value80 = 0;
    uint32_t value84 = 0;
    uint32_t value88 = 0;
    uint64_t value8c = 0;
    uint32_t value94 = 0;
    uint32_t value98 = 0;
    const bool ok = read_controller_bgm_chain(controller, slot, bgm)
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, state)
        && core::safe_read_field(slot, 0x30, mode)
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field34, value34)
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field48, value48)
        && core::safe_read_field(slot, 0x5c, flag5c)
        && core::safe_read_field(slot, 0x5d, flag5d)
        && core::safe_read_field(slot, 0x60, value60)
        && core::safe_read_field(slot, 0x64, value64)
        && core::safe_read_field(slot, 0x6c, value6c)
        && core::safe_read_field(slot, 0x70, value70)
        && core::safe_read_field(slot, 0x74, value74)
        && core::safe_read_field(slot, 0x78, value78)
        && core::safe_read_field(slot, 0x7c, value7c)
        && core::safe_read_field(slot, 0x80, value80)
        && core::safe_read_field(slot, 0x84, value84)
        && core::safe_read_field(slot, 0x88, value88)
        && core::safe_read_field(slot, 0x8c, value8c)
        && core::safe_read_field(slot, 0x94, value94)
        && core::safe_read_field(slot, 0x98, value98);
    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 16) {
        return;
    }
    std::ostringstream out;
    out << "[audio_sead] slot_setup_snapshot phase=" << (phase ? phase : "?")
        << " status=" << (ok ? "ok" : "read_failed")
        << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
        << " slot=0x" << reinterpret_cast<uintptr_t>(slot)
        << " bgm=0x" << reinterpret_cast<uintptr_t>(bgm)
        << " state=0x" << static_cast<unsigned>(state)
        << " mode=0x" << static_cast<unsigned>(mode)
        << " f34=0x" << value34
        << " f48=0x" << value48
        << " f5c=0x" << static_cast<unsigned>(flag5c)
        << " f5d=0x" << static_cast<unsigned>(flag5d)
        << " f60=0x" << value60
        << " f64=0x" << value64
        << " f6c=0x" << value6c
        << " f70=0x" << value70
        << " f74=0x" << value74
        << " f78=0x" << value78
        << " f7c=0x" << value7c
        << " f80=0x" << value80
        << " f84=0x" << value84
        << " f88=0x" << value88
        << " f8c=0x" << value8c
        << " f94=0x" << value94
        << " f98=0x" << value98
        << std::dec;
    core::log(ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
}

bool lookup_current_bgm_controller_seh(BgmControllerLookupFn lookup, uint64_t slot_key, void*& controller)
{
    controller = nullptr;
    __try {
        controller = lookup(slot_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* lookup_current_bgm_controller()
{
    if (!g_controller_lookup_available.load(std::memory_order_acquire) || !g_exe_module) {
        return nullptr;
    }
    uint64_t slot_key = 0;
    auto* const key_address = reinterpret_cast<uint8_t*>(g_exe_module) + rva::BgmControllerKeyGlobal;
    if (!core::safe_read_field(key_address, 0, slot_key) || slot_key == 0) {
        return nullptr;
    }
    auto* const lookup = reinterpret_cast<BgmControllerLookupFn>(
        reinterpret_cast<uint8_t*>(g_exe_module) + rva::BgmControllerLookup);
    void* controller = nullptr;
    if (!lookup_current_bgm_controller_seh(lookup, slot_key, controller)) {
        g_controller_lookup_available.store(false, std::memory_order_release);
        core::log(core::LogLevel::Error, "[audio_sead] controller_lookup status=exception disabled=1");
        return nullptr;
    }
    return controller;
}

bool lookup_current_piano_audio_owner(void*& owner)
{
    owner = nullptr;
    if (!g_exe_module) {
        return false;
    }
    void* global = nullptr;
    return core::safe_read_field(g_exe_module, rva::PianoAudioGlobal, global)
        && global
        && core::safe_read_field(global, runtime_layouts::PianoAudioGlobal::owner, owner)
        && owner;
}

bool call_piano_audio_request_seh(PianoAudioRequestFn request, uint8_t request_index, uint64_t packed_key)
{
    __try {
        request(request_index, packed_key, 1.0f, 1.0f, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool capture_piano_audio_request_profile(const std::string& song_id, int32_t base_slot, void* owner)
{
    AudioRouteState route_snapshot;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_piano_audio_request_profile = {};
        route_snapshot = g_audio_route_state;
    }
    void* current_owner = nullptr;
    void* current_slot = nullptr;
    void* current_bgm = nullptr;
    void* current_sound = nullptr;
    uint64_t current_request_handle = 0;
    uint8_t current_state = 0;
    uint64_t packed_key = 0;
    uint8_t request_index = 0;
    const bool current_owner_valid = lookup_current_piano_audio_owner(current_owner);
    const bool packed_key_valid = owner
        && core::safe_read_field(owner, runtime_layouts::PianoAudioOwner::packed_key, packed_key);
    const bool request_index_valid = owner
        && core::safe_read_field(owner, runtime_layouts::PianoAudioOwner::request_index, request_index);
    const bool route_valid = route_snapshot.phase == AudioRoutePhase::Playing
        && route_snapshot.desired_song_id == song_id
        && route_snapshot.lease_identity.valid()
        && route_snapshot.custom_resource_owned
        && lookup_current_bgm_controller() == route_snapshot.controller
        && uobject_identity_matches(route_snapshot.controller, route_snapshot.controller_identity)
        && uobject_identity_matches(route_snapshot.owned_sound, route_snapshot.owned_sound_identity)
        && read_controller_bgm_chain(route_snapshot.controller, current_slot, current_bgm)
        && core::safe_read_field(current_bgm, runtime_layouts::SqexSeadBgm::sound, current_sound)
        && core::safe_read_field(current_bgm, runtime_layouts::SqexSeadBgm::request_handle, current_request_handle)
        && core::safe_read_field(current_slot, runtime_layouts::SqexSeadSlot::state, current_state)
        && current_slot == route_snapshot.owned_slot
        && current_bgm == route_snapshot.owned_bgm
        && current_sound == route_snapshot.owned_sound
        && current_request_handle == route_snapshot.owned_request_handle
        && current_request_handle != 0
        && current_state == 4;
    if (song_id.empty() || base_slot < 0 || !owner || !current_owner_valid
        || current_owner != owner || !packed_key_valid || !request_index_valid
        || packed_key == 0 || !route_valid) {
        std::ostringstream out;
        out << "[audio_sead] native_owner_request_profile status=capture_failed"
            << " song_id=" << (song_id.empty() ? "<none>" : song_id)
            << " base_slot=" << base_slot
            << " owner=0x" << std::hex << reinterpret_cast<uintptr_t>(owner)
            << " current_owner=0x" << reinterpret_cast<uintptr_t>(current_owner)
            << " packed_key=0x" << packed_key
            << std::dec
            << " current_owner_valid=" << current_owner_valid
            << " owner_matches=" << (current_owner == owner)
            << " packed_key_valid=" << packed_key_valid
            << " request_index_valid=" << request_index_valid
            << " route_valid=" << route_valid
            << " request_index=" << static_cast<unsigned>(request_index);
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    PianoAudioRequestProfile profile;
    profile.ready = true;
    profile.captured_song_id = song_id;
    profile.base_slot = base_slot;
    profile.owner = owner;
    profile.request_index = request_index;
    profile.packed_key = packed_key;
    profile.route_generation = route_snapshot.generation;
    profile.lease_identity = route_snapshot.lease_identity;
    profile.controller = route_snapshot.controller;
    profile.controller_identity = route_snapshot.controller_identity;
    profile.slot = route_snapshot.owned_slot;
    profile.bgm = route_snapshot.owned_bgm;
    profile.request_handle = route_snapshot.owned_request_handle;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_piano_audio_request_profile = std::move(profile);
    }
    std::ostringstream out;
    out << "[audio_sead] native_owner_request_profile status=captured"
        << " song_id=" << song_id
        << " base_slot=" << base_slot
        << " owner=0x" << std::hex << reinterpret_cast<uintptr_t>(owner)
        << " request_index=0x" << static_cast<unsigned>(request_index)
        << " packed_key=0x" << packed_key
        << std::dec
        << " route_generation=" << route_snapshot.generation
        << " lease_generation=" << route_snapshot.lease_identity.generation;
    core::log(core::LogLevel::Info, out.str());
    return true;
}

bool capture_slot_setup_profile(
    void* controller,
    const UObjectIdentity& expected_controller_identity,
    void* expected_slot,
    uint8_t setup_flag)
{
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_slot_setup_profile = {};
    }
    UObjectIdentity controller_identity;
    void* slot = nullptr;
    void* bgm = nullptr;
    SlotSetupProfile profile;
    profile.ready = lookup_current_bgm_controller() == controller
        && read_uobject_identity(controller, controller_identity)
        && uobject_identity_matches(controller, expected_controller_identity)
        && read_controller_bgm_chain(controller, slot, bgm)
        && slot == expected_slot
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field34, profile.field34)
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field48, profile.field48)
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field5e, profile.field5e)
        && core::safe_copy_bytes(reinterpret_cast<uint8_t*>(slot) + 0x60,
            profile.fields60_to_9b.data(), profile.fields60_to_9b.size())
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field9c, profile.field9c);
    if (!profile.ready) {
        core::log(core::LogLevel::Error, "[audio_sead] slot_setup_profile status=capture_failed");
        return false;
    }
    profile.controller = controller;
    profile.controller_identity = controller_identity;
    profile.slot = slot;
    profile.setup_flag = setup_flag;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_slot_setup_profile = profile;
    }
    core::log(core::LogLevel::Info, "[audio_sead] slot_setup_profile status=captured");
    return true;
}

bool capture_native_play_setup_profile(
    const std::string& song_id,
    void* controller,
    const UObjectIdentity& expected_controller_identity,
    void* expected_slot,
    void* sound,
    const UObjectIdentity& expected_sound_identity,
    float arg1,
    float arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint8_t flag,
    const std::array<uint8_t, 12>& arg6_snapshot,
    bool arg6_snapshot_valid)
{
    NativePlaySetupProfile profile;
    void* slot = nullptr;
    void* bgm = nullptr;
    profile.ready = arg6_snapshot_valid
        && lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, expected_controller_identity)
        && read_controller_bgm_chain(controller, slot, bgm)
        && slot == expected_slot
        && sound
        && uobject_identity_matches(sound, expected_sound_identity)
        && expected_sound_identity.live.internal_index >= 0
        && expected_sound_identity.live.serial_number > 0
        && pin_live_uobject_handle(sound, expected_sound_identity.live)
        && is_live_uobject_rooted(sound, expected_sound_identity.live);
    if (!profile.ready) {
        core::log(core::LogLevel::Error, "[audio_sead] native_play_setup_profile status=capture_failed");
        return false;
    }
    profile.controller = controller;
    profile.song_id = song_id;
    profile.controller_identity = expected_controller_identity;
    profile.slot = slot;
    profile.sound = sound;
    profile.sound_identity = expected_sound_identity;
    profile.arg1 = arg1;
    profile.arg2 = arg2;
    profile.arg3 = arg3;
    profile.arg4 = arg4;
    profile.flag = flag;
    profile.arg6 = arg6_snapshot;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_native_play_setup_profile = profile;
    }
    core::log(core::LogLevel::Info, "[audio_sead] native_play_setup_profile status=captured");
    return true;
}

bool get_native_play_setup_profile(
    const std::string& song_id,
    void* controller,
    const UObjectIdentity& controller_identity,
    void* slot,
    NativePlaySetupProfile& profile)
{
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    const bool matches = g_native_play_setup_profile.ready
        && g_native_play_setup_profile.song_id == song_id
        && g_native_play_setup_profile.controller == controller
        && g_native_play_setup_profile.slot == slot
        && uobject_identity_matches(controller, controller_identity)
        && uobject_identity_matches(controller, g_native_play_setup_profile.controller_identity)
        && g_native_play_setup_profile.sound
        && uobject_identity_matches(
            g_native_play_setup_profile.sound,
            g_native_play_setup_profile.sound_identity)
        && is_live_uobject_rooted(
            g_native_play_setup_profile.sound,
            g_native_play_setup_profile.sound_identity.live);
    if (matches) {
        profile = g_native_play_setup_profile;
    }
    return matches;
}

bool replay_play_setup_seh(
    SeadPlaySetupFn play_setup,
    void* sound,
    float arg1,
    float arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint8_t flag,
    void* arg6)
{
    __try {
        play_setup(sound, arg1, arg2, arg3, arg4, flag, arg6);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool slot_setup_profile_matches(void* controller, const UObjectIdentity& controller_identity, void* slot)
{
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    return g_slot_setup_profile.ready
        && g_slot_setup_profile.controller == controller
        && g_slot_setup_profile.slot == slot
        && g_slot_setup_available.load(std::memory_order_acquire)
        && uobject_identity_matches(controller, controller_identity)
        && uobject_identity_matches(controller, g_slot_setup_profile.controller_identity);
}

bool call_bgm_slot_setup_seh(BgmSlotSetupFn setup, void* slot, uint8_t flag)
{
    __try {
        setup(slot, flag, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool apply_slot_setup_profile(void* controller, const UObjectIdentity& controller_identity, void* slot)
{
    SlotSetupProfile profile;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        profile = g_slot_setup_profile;
    }
    if (!profile.ready
        || profile.controller != controller
        || profile.slot != slot
        || lookup_current_bgm_controller() != controller
        || !uobject_identity_matches(controller, controller_identity)
        || !uobject_identity_matches(controller, profile.controller_identity)) {
        return false;
    }
    void* current_slot = nullptr;
    void* current_bgm = nullptr;
    if (!read_controller_bgm_chain(controller, current_slot, current_bgm)
        || current_slot != slot) {
        return false;
    }
    uint32_t old34 = 0;
    uint32_t old48 = 0;
    uint8_t old5e = 0;
    uint8_t old9c = 0;
    std::array<uint8_t, 0x3c> old_fields{};
    if (!core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field34, old34)
        || !core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field48, old48)
        || !core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field5e, old5e)
        || !core::safe_copy_bytes(reinterpret_cast<uint8_t*>(slot) + 0x60,
            old_fields.data(), old_fields.size())
        || !core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field9c, old9c)
        || lookup_current_bgm_controller() != controller
        || !uobject_identity_matches(controller, controller_identity)
        || !read_controller_bgm_chain(controller, current_slot, current_bgm)
        || current_slot != slot) {
        return false;
    }
    const auto rollback_setup_fields = [&]() {
        const bool rollback_safe = lookup_current_bgm_controller() == controller
            && uobject_identity_matches(controller, controller_identity)
            && read_controller_bgm_chain(controller, current_slot, current_bgm)
            && current_slot == slot;
        const bool rollback34 = rollback_safe
            && core::safe_write_field(slot, runtime_layouts::SqexSeadSlot::observed_field34, old34);
        const bool rollback48 = rollback_safe
            && core::safe_write_field(slot, runtime_layouts::SqexSeadSlot::observed_field48, old48);
        const bool rollback5e = rollback_safe
            && core::safe_write_field(slot, runtime_layouts::SqexSeadSlot::observed_field5e, old5e);
        const bool rollback_fields = rollback_safe
            && core::safe_write_bytes(reinterpret_cast<uint8_t*>(slot) + 0x60,
                old_fields.data(), old_fields.size());
        const bool rollback9c = rollback_safe
            && core::safe_write_field(slot, runtime_layouts::SqexSeadSlot::observed_field9c, old9c);
        uint32_t verify34 = 0;
        uint32_t verify48 = 0;
        uint8_t verify5e = 0;
        uint8_t verify9c = 0;
        std::array<uint8_t, 0x3c> verify_fields{};
        return rollback34 && rollback48 && rollback5e && rollback_fields && rollback9c
            && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field34, verify34)
            && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field48, verify48)
            && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field5e, verify5e)
            && core::safe_copy_bytes(reinterpret_cast<uint8_t*>(slot) + 0x60,
                verify_fields.data(), verify_fields.size())
            && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field9c, verify9c)
            && verify34 == old34
            && verify48 == old48
            && verify5e == old5e
            && verify_fields == old_fields
            && verify9c == old9c
            && lookup_current_bgm_controller() == controller
            && uobject_identity_matches(controller, controller_identity)
            && read_controller_bgm_chain(controller, current_slot, current_bgm)
            && current_slot == slot;
    };
    auto* const slot_setup = reinterpret_cast<BgmSlotSetupFn>(
        reinterpret_cast<uint8_t*>(g_exe_module) + rva::BgmSlotSetup);
    if (!g_slot_setup_available.load(std::memory_order_acquire)) {
        return false;
    }
    const bool native_setup_ok = call_bgm_slot_setup_seh(slot_setup, slot, profile.setup_flag);
    const bool native_chain_ok = lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, controller_identity)
        && read_controller_bgm_chain(controller, current_slot, current_bgm)
        && current_slot == slot;
    if (!native_setup_ok || !native_chain_ok) {
        const bool rolled_back = rollback_setup_fields();
        if (!native_setup_ok) {
            g_slot_setup_available.store(false, std::memory_order_release);
        }
        if (!rolled_back) {
            g_audio_route_disabled.store(true, std::memory_order_release);
        }
        core::log(core::LogLevel::Error,
            rolled_back
                ? "[audio_sead] slot_setup_profile status=native_setup_failed rolled_back=1"
                : "[audio_sead] slot_setup_profile status=native_setup_failed rolled_back=0 route=disabled");
        return false;
    }
    const bool wrote34 = core::safe_write_field(
        slot, runtime_layouts::SqexSeadSlot::observed_field34, profile.field34);
    const bool wrote48 = wrote34 && core::safe_write_field(
        slot, runtime_layouts::SqexSeadSlot::observed_field48, profile.field48);
    const bool wrote_fields = wrote48
        && core::safe_write_bytes(reinterpret_cast<uint8_t*>(slot) + 0x60,
            profile.fields60_to_9b.data(), profile.fields60_to_9b.size());
    uint32_t verify34 = 0;
    uint32_t verify48 = 0;
    uint8_t verify5e = 0;
    uint8_t verify9c = 0;
    std::array<uint8_t, 0x3c> verify_fields{};
    const bool verified = wrote_fields
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field34, verify34)
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field48, verify48)
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field5e, verify5e)
        && core::safe_copy_bytes(reinterpret_cast<uint8_t*>(slot) + 0x60,
            verify_fields.data(), verify_fields.size())
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::observed_field9c, verify9c)
        && verify34 == profile.field34
        && verify48 == profile.field48
        && verify5e == profile.field5e
        && verify_fields == profile.fields60_to_9b
        && verify9c == profile.field9c
        && lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, controller_identity)
        && read_controller_bgm_chain(controller, current_slot, current_bgm)
        && current_slot == slot;
    if (verified) {
        core::log(core::LogLevel::Info, "[audio_sead] slot_setup_profile status=applied");
        return true;
    }
    const bool rolled_back = rollback_setup_fields();
    if (!rolled_back) {
        g_audio_route_disabled.store(true, std::memory_order_release);
    }
    core::log(core::LogLevel::Error,
        rolled_back
            ? "[audio_sead] slot_setup_profile status=apply_failed rolled_back=1"
            : "[audio_sead] slot_setup_profile status=apply_failed rolled_back=0 route=disabled");
    return false;
}

bool rebuild_armed_controller_route(
    void* controller, const UObjectIdentity& controller_identity,
    const SongDescriptor& song, const SidecarRuntimeState& sidecar,
    bool* waiting_for_fresh_play_setup = nullptr)
{
    if (waiting_for_fresh_play_setup) {
        *waiting_for_fresh_play_setup = false;
    }
    void* current_controller = lookup_current_bgm_controller();
    if (current_controller != controller
        || !uobject_identity_matches(controller, controller_identity)) {
        core::log(core::LogLevel::Error, "[audio_sead] controller_rebuild status=validation_failed reason=current_controller_identity");
        return false;
    }
    bool stopped_controller = false;
    uint64_t route_generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_audio_route_state.phase != AudioRoutePhase::Armed
            || g_audio_route_state.desired_song_id != song.id
            || g_audio_route_state.controller != controller) {
            return false;
        }
        route_generation = g_audio_route_state.generation;
        stopped_controller = g_audio_route_state.stop_observed
            && g_audio_route_state.stop_authorized_generation == route_generation;
    }

    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint64_t old_request_handle = 0;
    uint8_t slot_state = 0;
    UObjectIdentity sound_identity;
    const bool chain_read = read_controller_bgm_chain(controller, slot, bgm)
        && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, sound)
        && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::request_handle, old_request_handle)
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, slot_state);
    void* const bgm_sound_before_rebuild = sound;
    if (!chain_read) {
        core::log(core::LogLevel::Error, "[audio_sead] controller_rebuild status=validation_failed reason=controller_slot_bgm_chain");
        return false;
    }
    NativePlaySetupProfile replay_profile;
    if (!get_native_play_setup_profile(song.id, controller, controller_identity, slot, replay_profile)) {
        if (waiting_for_fresh_play_setup) {
            *waiting_for_fresh_play_setup = true;
        }
        core::log(core::LogLevel::Info, "[audio_sead] controller_rebuild status=waiting_for_fresh_playsetup");
        return false;
    }
    sound = replay_profile.sound;
    sound_identity = replay_profile.sound_identity;
    if (old_request_handle && !stopped_controller) {
        std::ostringstream out;
        out << "[audio_sead] controller_rebuild status=blocked_resource_busy"
            << " song_id=" << song.id
            << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
            << " slot=0x" << reinterpret_cast<uintptr_t>(slot)
            << " bgm=0x" << reinterpret_cast<uintptr_t>(bgm)
            << " request_handle=0x" << old_request_handle
            << std::dec
            << " slot_state=" << static_cast<unsigned>(slot_state);
        core::log(core::LogLevel::Error, out.str());
        return false;
    }
    if (old_request_handle) {
        std::ostringstream out;
        out << "[audio_sead] controller_rebuild status=clearing_stopped_resource"
            << " song_id=" << song.id
            << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
            << " request_handle=0x" << old_request_handle
            << std::dec
            << " slot_state=" << static_cast<unsigned>(slot_state);
        core::log(core::LogLevel::Info, out.str());
    }

    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const bool route_still_matches = g_audio_route_state.phase == AudioRoutePhase::Armed
            && g_audio_route_state.generation == route_generation
            && g_audio_route_state.desired_song_id == song.id
            && g_audio_route_state.controller == controller;
        const bool stopped_resource_authorized = !old_request_handle
            || (g_audio_route_state.stop_observed
                && g_audio_route_state.stop_authorized_generation == route_generation);
        if (!route_still_matches || !stopped_resource_authorized) {
            if (route_still_matches) {
                AudioRouteTransitionRecorder route_transition_record(
                    AudioRouteTransitionReason::ControllerRebuildRouteChanged,
                    AudioRouteTransitionKind::RouteReset);
                ++g_audio_route_state.generation;
                g_audio_route_state.phase = g_audio_route_state.custom_resource_owned
                    ? AudioRoutePhase::Playing
                    : AudioRoutePhase::Idle;
                g_audio_route_state.desired_song_id.clear();
                g_audio_route_state.stop_observed = false;
                g_audio_route_state.stop_authorized_generation = 0;
                g_audio_route_state.set_play_handoff_pending = false;
            }
            core::log(core::LogLevel::Error, "[audio_sead] controller_rebuild status=validation_failed reason=route_changed_before_clear");
            return false;
        }
        // Stop evidence is a one-shot authorization for this exact route generation.
        g_audio_route_state.stop_observed = false;
        g_audio_route_state.stop_authorized_generation = 0;
        g_audio_route_state.set_play_handoff_pending = false;
        g_audio_route_state.phase = AudioRoutePhase::Rebuilding;
    }

    void* pre_clear_slot = nullptr;
    void* pre_clear_bgm = nullptr;
    void* pre_clear_sound = nullptr;
    uint64_t pre_clear_request_handle = 0;
    uint8_t pre_clear_state = 0;
    if (lookup_current_bgm_controller() != controller
        || !uobject_identity_matches(controller, controller_identity)
        || !uobject_identity_matches(sound, sound_identity)
        || !read_controller_bgm_chain(controller, pre_clear_slot, pre_clear_bgm)
        || !core::safe_read_field(pre_clear_bgm, runtime_layouts::SqexSeadBgm::sound, pre_clear_sound)
        || !core::safe_read_field(pre_clear_bgm, runtime_layouts::SqexSeadBgm::request_handle, pre_clear_request_handle)
        || !core::safe_read_field(pre_clear_slot, runtime_layouts::SqexSeadSlot::state, pre_clear_state)
        || pre_clear_slot != slot
        || pre_clear_bgm != bgm
        || pre_clear_sound != bgm_sound_before_rebuild
        || pre_clear_request_handle != old_request_handle
        || pre_clear_state != slot_state) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.phase == AudioRoutePhase::Rebuilding
                && g_audio_route_state.generation == route_generation
                && g_audio_route_state.controller == controller) {
                g_audio_route_state.phase = AudioRoutePhase::Armed;
                g_audio_route_state.stop_observed = stopped_controller;
                g_audio_route_state.stop_authorized_generation = stopped_controller ? route_generation : 0;
                g_audio_route_state.set_play_handoff_pending = false;
            }
        }
        core::log(core::LogLevel::Error, "[audio_sead] controller_rebuild status=validation_failed reason=pre_clear_lifetime_or_controller");
        return false;
    }
    call_bgm_slot_set_original(controller, nullptr);
    void* cleared_slot = nullptr;
    void* cleared_bgm = nullptr;
    void* cleared_sound = sound;
    uint64_t cleared_request_handle = old_request_handle;
    uint8_t cleared_slot_state = slot_state;
    const bool cleared = lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, controller_identity)
        && uobject_identity_matches(sound, sound_identity)
        && read_controller_bgm_chain(controller, cleared_slot, cleared_bgm)
        && core::safe_read_field(cleared_bgm, runtime_layouts::SqexSeadBgm::sound, cleared_sound)
        && core::safe_read_field(cleared_bgm, runtime_layouts::SqexSeadBgm::request_handle, cleared_request_handle)
        && core::safe_read_field(cleared_slot, runtime_layouts::SqexSeadSlot::state, cleared_slot_state)
        && cleared_slot == slot
        && cleared_bgm == bgm
        && cleared_sound == nullptr
        && cleared_request_handle == 0
        && cleared_slot_state == 0;
    if (!cleared) {
        std::ostringstream out;
        out << "[audio_sead] controller_rebuild status=clear_failed"
            << " song_id=" << song.id
            << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
            << " slot=0x" << reinterpret_cast<uintptr_t>(cleared_slot)
            << " bgm=0x" << reinterpret_cast<uintptr_t>(cleared_bgm)
            << " sound=0x" << reinterpret_cast<uintptr_t>(cleared_sound)
            << " request_handle=0x" << cleared_request_handle
            << std::dec
            << " slot_state=" << static_cast<unsigned>(cleared_slot_state);
        core::log(core::LogLevel::Error, out.str());
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::NativeClearUnverified,
                g_audio_route_state.lease_identity);
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
        return false;
    }

    std::vector<AudioFieldPatch> applied;
    if (!patch_sound_for_sidecar_call(sound, song, sidecar, "controller_rebuild", applied)) {
        bool route_still_matches = false;
        void* recovery_slot = nullptr;
        void* recovery_bgm = nullptr;
        void* recovery_sound = sound;
        uint64_t recovery_request_handle = 0;
        uint8_t recovery_state = 0;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            route_still_matches = g_audio_route_state.phase == AudioRoutePhase::Rebuilding
                && g_audio_route_state.generation == route_generation
                && g_audio_route_state.desired_song_id == song.id
                && g_audio_route_state.controller == controller;
        }
        bool recovery_rebound = false;
        void* rebound_recovery_slot = nullptr;
        void* rebound_recovery_bgm = nullptr;
        void* rebound_recovery_sound = nullptr;
        uint64_t rebound_recovery_request_handle = 0;
        uint8_t rebound_recovery_state = 0;
        if (route_still_matches
            && !g_audio_route_disabled.load(std::memory_order_acquire)
            && lookup_current_bgm_controller() == controller
            && uobject_identity_matches(controller, controller_identity)
            && uobject_identity_matches(sound, sound_identity)
            && read_controller_bgm_chain(controller, recovery_slot, recovery_bgm)
            && core::safe_read_field(recovery_bgm, runtime_layouts::SqexSeadBgm::sound, recovery_sound)
            && core::safe_read_field(recovery_bgm, runtime_layouts::SqexSeadBgm::request_handle, recovery_request_handle)
            && core::safe_read_field(recovery_slot, runtime_layouts::SqexSeadSlot::state, recovery_state)
            && recovery_slot == cleared_slot
            && recovery_bgm == cleared_bgm
            && recovery_sound == nullptr
            && recovery_request_handle == 0
            && recovery_state == 0) {
            call_bgm_slot_set_original(controller, sound);
            recovery_rebound = lookup_current_bgm_controller() == controller
                && uobject_identity_matches(controller, controller_identity)
                && uobject_identity_matches(sound, sound_identity)
                && read_controller_bgm_chain(controller, rebound_recovery_slot, rebound_recovery_bgm)
                && core::safe_read_field(rebound_recovery_bgm, runtime_layouts::SqexSeadBgm::sound, rebound_recovery_sound)
                && core::safe_read_field(
                    rebound_recovery_bgm, 0x48, rebound_recovery_request_handle)
                && core::safe_read_field(rebound_recovery_slot, runtime_layouts::SqexSeadSlot::state, rebound_recovery_state)
                && rebound_recovery_slot == recovery_slot
                && rebound_recovery_bgm == recovery_bgm
                && rebound_recovery_sound == sound
                && rebound_recovery_request_handle != 0
                && (rebound_recovery_state == 2 || rebound_recovery_state == 4);
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_audio_route_state.list_cleanup_pending = true;
            if (recovery_rebound) {
                g_audio_route_state.custom_resource_owned = true;
                g_audio_route_state.owned_slot = rebound_recovery_slot;
                g_audio_route_state.owned_bgm = rebound_recovery_bgm;
                g_audio_route_state.owned_sound = sound;
                g_audio_route_state.owned_sound_identity = sound_identity;
                g_audio_route_state.owned_request_handle = rebound_recovery_request_handle;
            }
            (void)g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::NativeClearUnverified,
                g_audio_route_state.lease_identity);
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
        core::log(recovery_rebound ? core::LogLevel::Info : core::LogLevel::Error,
            recovery_rebound
                ? "[audio_sead] controller_rebuild recovery=rebound route=disabled"
                : "[audio_sead] controller_rebuild recovery=failed route=disabled");
        return false;
    }
    bool route_matches = false;
    uint64_t playback_generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route_matches = g_audio_route_state.phase == AudioRoutePhase::Rebuilding
            && g_audio_route_state.generation == route_generation
            && g_audio_route_state.desired_song_id == song.id
            && g_audio_route_state.controller == controller;
        if (route_matches) {
            g_pending_play_setup_patch.sound = sound;
            g_pending_play_setup_patch.song_id = song.id;
            g_pending_play_setup_patch.patches = applied;
            g_pending_play_setup_patch.restore_safe = true;
            playback_generation = ++g_audio_route_state.generation;
            g_audio_route_state.phase = AudioRoutePhase::PatchedPlaySetup;
            g_audio_route_state.patched_song_id = song.id;
            g_audio_route_state.sound = sound;
        }
    }
    if (!route_matches) {
        const bool restored = restore_patches_reverse(applied);
        if (!restored) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_failed_patch_journal.insert(g_failed_patch_journal.end(), applied.begin(), applied.end());
            g_audio_route_disabled.store(true, std::memory_order_release);
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::NativeClearUnverified,
                g_audio_route_state.lease_identity);
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
        return false;
    }

    void* pre_set_slot = nullptr;
    void* pre_set_bgm = nullptr;
    void* pre_set_sound = sound;
    uint64_t pre_set_request_handle = old_request_handle;
    uint8_t pre_set_state = slot_state;
    if (lookup_current_bgm_controller() != controller
        || !uobject_identity_matches(controller, controller_identity)
        || !uobject_identity_matches(sound, sound_identity)
        || !read_controller_bgm_chain(controller, pre_set_slot, pre_set_bgm)
        || !core::safe_read_field(pre_set_bgm, runtime_layouts::SqexSeadBgm::sound, pre_set_sound)
        || !core::safe_read_field(pre_set_bgm, runtime_layouts::SqexSeadBgm::request_handle, pre_set_request_handle)
        || !core::safe_read_field(pre_set_slot, runtime_layouts::SqexSeadSlot::state, pre_set_state)
        || pre_set_slot != cleared_slot
        || pre_set_bgm != cleared_bgm
        || pre_set_sound != nullptr
        || pre_set_request_handle != 0
        || pre_set_state != 0) {
        (void)restore_pending_play_setup_patch("controller_rebuild_pre_set_validation", false);
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::RouteValidationFailed,
                g_audio_route_state.lease_identity);
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
        core::log(core::LogLevel::Error, "[audio_sead] controller_rebuild status=validation_failed reason=pre_set_lifetime_or_controller");
        return false;
    }
    std::array<uint8_t, 12> replay_arg6 = replay_profile.arg6;
    bool native_replay_ok = false;
    if (replay_profile.ready
        && replay_profile.controller == controller
        && replay_profile.slot == pre_set_slot
        && g_original_play_setup) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.mark_native_arm_attempt(
                g_audio_route_state.lease_identity);
        }
        NativePlaySetupReplayScope replay_scope;
        native_replay_ok = replay_play_setup_seh(
            g_original_play_setup,
            sound,
            replay_profile.arg1,
            replay_profile.arg2,
            replay_profile.arg3,
            replay_profile.arg4,
            replay_profile.flag,
            replay_arg6.data());
    }
    core::log(native_replay_ok ? core::LogLevel::Info : core::LogLevel::Error,
        native_replay_ok
            ? "[audio_sead] native_play_setup_replay status=called"
            : "[audio_sead] native_play_setup_replay status=failed");
    void* rebound_slot = nullptr;
    void* rebound_bgm = nullptr;
    void* rebound_sound = nullptr;
    uint64_t new_request_handle = 0;
    uint8_t rebound_slot_state = 0;
    bool route_safe = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route_safe = g_pending_play_setup_patch.restore_safe
            && g_audio_route_state.phase == AudioRoutePhase::PatchedPlaySetup
            && g_audio_route_state.generation == playback_generation
            && g_audio_route_state.patched_song_id == song.id
            && g_audio_route_state.sound == sound
            && g_audio_route_state.controller == controller;
    }
    const bool replay_chain_valid = lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, controller_identity)
        && uobject_identity_matches(sound, sound_identity)
        && read_controller_bgm_chain(controller, rebound_slot, rebound_bgm)
        && core::safe_read_field(rebound_bgm, runtime_layouts::SqexSeadBgm::sound, rebound_sound)
        && core::safe_read_field(rebound_bgm, runtime_layouts::SqexSeadBgm::request_handle, new_request_handle)
        && core::safe_read_field(rebound_slot, runtime_layouts::SqexSeadSlot::state, rebound_slot_state)
        && rebound_slot == pre_set_slot
        && rebound_bgm == pre_set_bgm
        && rebound_sound == sound
        && new_request_handle != 0
        && rebound_slot_state == 4;
    if (replay_chain_valid) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_audio_route_state.custom_resource_owned = true;
        g_audio_route_state.owned_slot = rebound_slot;
        g_audio_route_state.owned_bgm = rebound_bgm;
        g_audio_route_state.owned_sound = sound;
        g_audio_route_state.owned_sound_identity = sound_identity;
        g_audio_route_state.owned_request_handle = new_request_handle;
        g_audio_route_state.list_cleanup_pending = true;
    }
    const bool prepared = native_replay_ok
        && route_safe
        && replay_chain_valid;
    bool played = false;
    if (prepared) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_audio_route_state.phase = AudioRoutePhase::Playing;
            g_audio_route_state.stop_observed = false;
            g_audio_route_state.set_play_handoff_pending = false;
        }
        log_slot_setup_snapshot("controller_rebuild_after_native_playsetup", controller);
        void* played_slot = nullptr;
        void* played_bgm = nullptr;
        void* played_sound = nullptr;
        uint64_t played_request_handle = 0;
        uint8_t played_slot_state = 0;
        bool play_route_safe = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            play_route_safe = g_pending_play_setup_patch.restore_safe
                && g_audio_route_state.phase == AudioRoutePhase::Playing
                && g_audio_route_state.generation == playback_generation
                && g_audio_route_state.patched_song_id == song.id
                && g_audio_route_state.sound == sound
                && g_audio_route_state.controller == controller;
        }
        played = play_route_safe
            && lookup_current_bgm_controller() == controller
            && uobject_identity_matches(controller, controller_identity)
            && uobject_identity_matches(sound, sound_identity)
            && read_controller_bgm_chain(controller, played_slot, played_bgm)
            && core::safe_read_field(played_bgm, runtime_layouts::SqexSeadBgm::sound, played_sound)
            && core::safe_read_field(played_bgm, runtime_layouts::SqexSeadBgm::request_handle, played_request_handle)
            && core::safe_read_field(played_slot, runtime_layouts::SqexSeadSlot::state, played_slot_state)
            && played_slot == rebound_slot
            && played_bgm == rebound_bgm
            && played_sound == sound
            && played_request_handle == new_request_handle
            && played_request_handle != 0
            && played_slot_state == 4;
        if (played) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.phase == AudioRoutePhase::Playing
                && g_audio_route_state.generation == playback_generation
                && g_audio_route_state.patched_song_id == song.id
                && g_audio_route_state.sound == sound
                && g_audio_route_state.controller == controller) {
                g_audio_route_state.custom_resource_owned = true;
                g_audio_route_state.owned_slot = played_slot;
                g_audio_route_state.owned_bgm = played_bgm;
                g_audio_route_state.owned_sound = sound;
                g_audio_route_state.owned_sound_identity = sound_identity;
                g_audio_route_state.owned_request_handle = played_request_handle;
                g_audio_route_state.reusable_sound = sound;
                g_audio_route_state.reusable_sound_identity = sound_identity;
                g_audio_route_state.reusable_slot = played_slot;
                g_audio_route_state.reusable_bgm = played_bgm;
                g_audio_route_state.list_cleanup_pending = true;
            }
        }
    }
    bool restore_attempted = false;
    bool restored = false;
    if (played) {
        restore_attempted = true;
        restored = restore_pending_play_setup_patch("controller_rebuild_return", true);
        if (!restored) {
            played = false;
        }
    }
    bool failed_route_cleanup_verified = false;
    if (!played) {
        void* cleanup_pre_slot = nullptr;
        void* cleanup_pre_bgm = nullptr;
        void* cleanup_pre_sound = nullptr;
        uint64_t cleanup_pre_request_handle = 0;
        uint8_t cleanup_pre_state = 0;
        const bool cleanup_identity_safe = lookup_current_bgm_controller() == controller
            && uobject_identity_matches(controller, controller_identity)
            && uobject_identity_matches(sound, sound_identity)
            && read_controller_bgm_chain(controller, cleanup_pre_slot, cleanup_pre_bgm)
            && core::safe_read_field(cleanup_pre_bgm, runtime_layouts::SqexSeadBgm::sound, cleanup_pre_sound)
            && core::safe_read_field(cleanup_pre_bgm, runtime_layouts::SqexSeadBgm::request_handle, cleanup_pre_request_handle)
            && core::safe_read_field(cleanup_pre_slot, runtime_layouts::SqexSeadSlot::state, cleanup_pre_state)
            && cleanup_pre_slot == rebound_slot
            && cleanup_pre_bgm == rebound_bgm
            && cleanup_pre_sound == sound
            && cleanup_pre_request_handle == new_request_handle
            && cleanup_pre_request_handle != 0
            && (cleanup_pre_state == 2 || cleanup_pre_state == 4);
        if (cleanup_identity_safe) {
            call_bgm_slot_set_original(controller, nullptr);
        }
        void* cleanup_slot = nullptr;
        void* cleanup_bgm = nullptr;
        void* cleanup_sound = sound;
        uint64_t cleanup_request_handle = new_request_handle;
        uint8_t cleanup_state = rebound_slot_state;
        const bool cleanup_ok = cleanup_identity_safe
            && lookup_current_bgm_controller() == controller
            && uobject_identity_matches(controller, controller_identity)
            && uobject_identity_matches(sound, sound_identity)
            && read_controller_bgm_chain(controller, cleanup_slot, cleanup_bgm)
            && core::safe_read_field(cleanup_bgm, runtime_layouts::SqexSeadBgm::sound, cleanup_sound)
            && core::safe_read_field(cleanup_bgm, runtime_layouts::SqexSeadBgm::request_handle, cleanup_request_handle)
            && core::safe_read_field(cleanup_slot, runtime_layouts::SqexSeadSlot::state, cleanup_state)
            && cleanup_slot == cleanup_pre_slot
            && cleanup_bgm == cleanup_pre_bgm
            && cleanup_sound == nullptr
            && cleanup_request_handle == 0
            && cleanup_state == 0;
        failed_route_cleanup_verified = cleanup_ok;
        if (!cleanup_ok) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_pending_play_setup_patch.restore_safe = false;
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::NativeClearUnverified,
                g_audio_route_state.lease_identity);
            g_audio_route_disabled.store(true, std::memory_order_release);
        }
    }
    if (!restore_attempted) {
        restored = restore_pending_play_setup_patch("controller_rebuild_return", false);
    }
    if (!played) {
        AudioRouteCleanupResult cleanup_result;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (failed_route_cleanup_verified && restored) {
                cleanup_result = g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::NativeReleaseCommitted,
                    g_audio_route_state.lease_identity);
                if (cleanup_result.clear_route_metadata) {
                    AudioRouteTransitionRecorder route_transition_record(
                        AudioRouteTransitionReason::ControllerRebuildFailedRelease,
                        AudioRouteTransitionKind::RouteReset);
                    g_audio_route_state = {};
                }
            } else {
                g_audio_route_state.list_cleanup_pending = true;
                cleanup_result = g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::NativeClearUnverified,
                    g_audio_route_state.lease_identity);
            }
        }
        if (cleanup_result.thaw_profile) registry().clear_frozen_profile();
        g_audio_route_disabled.store(true, std::memory_order_release);
    }

    std::ostringstream out;
    out << "[audio_sead] controller_rebuild status=" << (played && restored ? "played" : "failed")
        << " song_id=" << song.id
        << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
        << " slot=0x" << reinterpret_cast<uintptr_t>(rebound_slot)
        << " bgm=0x" << reinterpret_cast<uintptr_t>(rebound_bgm)
        << " sound=0x" << reinterpret_cast<uintptr_t>(sound)
        << " request_handle=0x" << new_request_handle
        << std::dec
        << " slot_state_before=" << static_cast<unsigned>(slot_state)
        << " slot_state_after=" << static_cast<unsigned>(rebound_slot_state)
        << " route_safe=" << (route_safe ? 1 : 0)
        << " rebound=" << (prepared ? 1 : 0)
        << " played=" << (played ? 1 : 0)
        << " restored=" << (restored ? 1 : 0);
    core::log(played && restored ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    return played && restored;
}

bool has_audio_route_arm(const std::string& song_id)
{
    if (song_id.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    if (g_audio_route_state.phase != AudioRoutePhase::Armed
        || g_audio_route_state.desired_song_id != song_id) {
        return false;
    }
    return true;
}

PendingSoundRoute consume_audio_route_arm_for_pending_sound(void* controller, std::string& desired_song_id, std::string& patched_song_id, void*& sound)
{
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    if (g_audio_route_state.phase != AudioRoutePhase::Armed
        || g_audio_route_state.desired_song_id.empty()
        || !controller
        || g_audio_route_state.controller != controller) {
        return PendingSoundRoute::None;
    }
    desired_song_id = g_audio_route_state.desired_song_id;
    if (g_pending_play_setup_patch.patches.empty()) {
        return PendingSoundRoute::RequiresPlaySetup;
    }
    patched_song_id = g_pending_play_setup_patch.song_id;
    sound = g_pending_play_setup_patch.sound;
    if (!g_pending_play_setup_patch.restore_safe) {
        return PendingSoundRoute::UnsafeStoppedPatch;
    }
    if (desired_song_id != patched_song_id || !sound) {
        return PendingSoundRoute::SongMismatch;
    }
    return PendingSoundRoute::SameSongReuseBlocked;
}

BgmPlaybackNativePlaySetupReturnObservation call_play_setup_original(
    void* sound, float arg1, float arg2, uint64_t arg3, uint64_t arg4,
    uint8_t flag, void* arg6,
    BgmPlaybackNativePlaySetupAttemptState& attempt)
{
    return invoke_audio_production_play_setup(
        sound, arg1, arg2, arg3, arg4, flag, arg6, attempt);
}

const char* custom_activation_quarantine_failure_name(
    const CustomActivationQuarantineFailure failure) noexcept
{
    switch (failure) {
    case CustomActivationQuarantineFailure::None: return "none";
    case CustomActivationQuarantineFailure::ArmMissing: return "arm_missing";
    case CustomActivationQuarantineFailure::SidecarMissing: return "sidecar_missing";
    case CustomActivationQuarantineFailure::SoundIdentityInvalid: return "sound_identity_invalid";
    case CustomActivationQuarantineFailure::BankQualificationFailed: return "bank_qualification";
    case CustomActivationQuarantineFailure::PendingRestoreFailed: return "pending_restore";
    case CustomActivationQuarantineFailure::PostRestoreAuthorityFailed: return "post_restore_authority";
    case CustomActivationQuarantineFailure::PatchFailed: return "patch";
    case CustomActivationQuarantineFailure::RouteGenerationFailed: return "route_generation";
    case CustomActivationQuarantineFailure::SetupTokenAdvanceFailed: return "setup_token";
    case CustomActivationQuarantineFailure::DuplicateCallback: return "duplicate_callback";
    }
    return "unknown";
}

CustomActivationQuarantineRecord quarantine_custom_activation(
    const CustomActivationQuarantineFailure failure,
    const uint32_t nested_failure,
    const SelectionSnapshot& selection,
    const CustomContextToken& token,
    const CanonicalSubstrateBridgeAuthority& bridge,
    void* const callback_sound) noexcept
{
    CustomActivationQuarantineRecord record;
    int32_t callback_sound_index =
        bridge.expected_callback_sound_identity.internal_index;
    uint32_t callback_sound_serial =
        bridge.expected_callback_sound_identity.serial_number;
    if ((callback_sound_index < 0 || callback_sound_serial == 0)
        && callback_sound) {
        UObjectIdentity callback_identity;
        if (read_uobject_identity(callback_sound, callback_identity)) {
            callback_sound_index = callback_identity.live.internal_index;
            callback_sound_serial = callback_identity.live.serial_number;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_custom_activation_quarantine) {
            record = g_custom_activation_quarantine;
        } else {
            ++g_custom_activation_quarantine_generation;
            if (g_custom_activation_quarantine_generation == 0) {
                ++g_custom_activation_quarantine_generation;
            }
            record.active = true;
            record.generation = g_custom_activation_quarantine_generation;
            record.failure = failure;
            record.nested_failure = nested_failure;
            record.selection_generation = selection.generation;
            record.route_generation = token.route_generation;
            record.lease_generation = token.lease_generation;
            record.song_key = token.song_key;
            record.callback_sound = reinterpret_cast<uintptr_t>(
                bridge.expected_callback_sound
                    ? bridge.expected_callback_sound : callback_sound);
            record.callback_sound_index = callback_sound_index;
            record.callback_sound_serial = callback_sound_serial;
            record.original_forwarded = false;
            record.token_state_none = true;
            g_custom_activation_quarantine = record;
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
    }
    return record;
}

struct DeferredCustomActivationQuarantineMarker final {
    bool proposed = false;
    CustomActivationQuarantineRecord record{};
    CustomActivationQuarantineFailure observed_failure =
        CustomActivationQuarantineFailure::None;
    uint32_t nested_failure = 0;

    ~DeferredCustomActivationQuarantineMarker() noexcept
    {
        if (!proposed || !record) return;
        try {
            core::log(core::LogLevel::Error,
                std::string("[audio_sead] custom_activation_quarantined stage=pre_original first_failed_predicate=")
                    + custom_activation_quarantine_failure_name(observed_failure)
                    + " nested_failure=" + std::to_string(nested_failure)
                    + " generation=" + std::to_string(record.generation)
                    + " selection=" + std::to_string(record.selection_generation)
                    + " route=" + std::to_string(record.route_generation)
                    + " lease=" + std::to_string(record.lease_generation)
                    + " song=" + std::to_string(record.song_key)
                    + " callback=" + std::to_string(record.callback_sound)
                    + " callback_index=" + std::to_string(record.callback_sound_index)
                    + " callback_serial=" + std::to_string(record.callback_sound_serial)
                    + " original_forwarded=0 token_state=none");
        } catch (...) {
        }
    }

    void propose(const CustomActivationQuarantineRecord& value,
                 const CustomActivationQuarantineFailure failure,
                 const uint32_t nested) noexcept
    {
        proposed = true;
        record = value;
        observed_failure = failure;
        nested_failure = nested;
    }
};

enum class OwnerPatchMetadataNormalizationFailure : uint8_t {
    None,
    RestoreNotApplied,
    RouteDrift,
    LeaseDrift,
    SoundDrift,
    ControllerDrift,
    SetupDrift,
    FrozenSnapshotInvalid,
    OwnerPatchNotUnique,
    ExactOverrideRejected,
};

struct OwnerPatchMetadataNormalizationDiagnostic final {
    bool attempted = false;
    bool restored = false;
    bool exact_override_applied = false;
    uint64_t frozen_route_generation = 0;
    uint64_t pre_restore_route_generation = 0;
    uint64_t expected_live_route_generation = 0;
    uint64_t live_route_generation = 0;
    bool route_successor_exact = false;
    bool route_exact = false;
    bool lease_exact = false;
    bool sound_exact = false;
    bool controller_exact = false;
    bool setup_exact = false;
    bool setup_selection_song_exact = false;
    bool setup_selection_profile_valid = false;
    bool setup_desired_song_exact = false;
    bool setup_patched_song_cleared = false;
    bool setup_transient_sound_cleared = false;
    bool setup_pending_patch_journal_empty = false;
    bool setup_unpublished_cleared = false;
    bool frozen_snapshot_valid = false;
    uint32_t matching_owner_patches = 0;
    bool normalized = false;
    OwnerPatchMetadataNormalizationFailure first_failure =
        OwnerPatchMetadataNormalizationFailure::None;
};

static_assert(std::is_trivially_copyable_v<
    OwnerPatchMetadataNormalizationDiagnostic>);

const char* owner_patch_metadata_normalization_failure_name(
    const OwnerPatchMetadataNormalizationFailure failure) noexcept
{
    switch (failure) {
    case OwnerPatchMetadataNormalizationFailure::None: return "none";
    case OwnerPatchMetadataNormalizationFailure::RestoreNotApplied: return "restore_not_applied";
    case OwnerPatchMetadataNormalizationFailure::RouteDrift: return "route_drift";
    case OwnerPatchMetadataNormalizationFailure::LeaseDrift: return "lease_drift";
    case OwnerPatchMetadataNormalizationFailure::SoundDrift: return "sound_drift";
    case OwnerPatchMetadataNormalizationFailure::ControllerDrift: return "controller_drift";
    case OwnerPatchMetadataNormalizationFailure::SetupDrift: return "setup_drift";
    case OwnerPatchMetadataNormalizationFailure::FrozenSnapshotInvalid: return "frozen_snapshot_invalid";
    case OwnerPatchMetadataNormalizationFailure::OwnerPatchNotUnique: return "owner_patch_not_unique";
    case OwnerPatchMetadataNormalizationFailure::ExactOverrideRejected: return "exact_override_rejected";
    }
    return "unknown";
}

void log_owner_patch_metadata_normalization(
    const OwnerPatchMetadataNormalizationDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.attempted) return;
    static std::atomic_uint32_t s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 32) return;
    try {
        std::ostringstream stream;
        stream << "[audio_sead_owner_patch_normalization] failure="
               << owner_patch_metadata_normalization_failure_name(
                      diagnostic.first_failure)
               << " restored=" << (diagnostic.restored ? 1 : 0)
               << " override_applied="
               << (diagnostic.exact_override_applied ? 1 : 0)
               << " frozen_route_generation="
               << diagnostic.frozen_route_generation
               << " pre_restore_route_generation="
               << diagnostic.pre_restore_route_generation
               << " expected_live_route_generation="
               << diagnostic.expected_live_route_generation
               << " live_route_generation="
               << diagnostic.live_route_generation
               << " route_successor_exact="
               << (diagnostic.route_successor_exact ? 1 : 0)
               << " route_exact=" << (diagnostic.route_exact ? 1 : 0)
               << " lease_exact=" << (diagnostic.lease_exact ? 1 : 0)
               << " sound_exact=" << (diagnostic.sound_exact ? 1 : 0)
               << " controller_exact=" << (diagnostic.controller_exact ? 1 : 0)
               << " setup_exact=" << (diagnostic.setup_exact ? 1 : 0)
               << " setup_selection_song_exact="
               << (diagnostic.setup_selection_song_exact ? 1 : 0)
               << " setup_selection_profile_valid="
               << (diagnostic.setup_selection_profile_valid ? 1 : 0)
               << " setup_desired_song_exact="
               << (diagnostic.setup_desired_song_exact ? 1 : 0)
               << " setup_patched_song_cleared="
               << (diagnostic.setup_patched_song_cleared ? 1 : 0)
               << " setup_transient_sound_cleared="
               << (diagnostic.setup_transient_sound_cleared ? 1 : 0)
               << " setup_pending_patch_journal_empty="
               << (diagnostic.setup_pending_patch_journal_empty ? 1 : 0)
               << " setup_unpublished_cleared="
               << (diagnostic.setup_unpublished_cleared ? 1 : 0)
               << " frozen_valid="
               << (diagnostic.frozen_snapshot_valid ? 1 : 0)
               << " owner_patch_count=" << diagnostic.matching_owner_patches
               << " normalized=" << (diagnostic.normalized ? 1 : 0);
        core::log(diagnostic.normalized ? core::LogLevel::Info
                                        : core::LogLevel::Error,
                  stream.str());
    } catch (...) {
    }
}

void __fastcall play_setup_detour(void* sound, float arg1, float arg2, uint64_t arg3, uint64_t arg4, uint8_t flag, void* arg6)
{
    // Reverse destruction releases the operation lock before the deferred bank
    // diagnostic. The residency query runs while callback lifetime is still held;
    // the existing restore-failure emitter remains last, after callback release.
    DeferredPlaySetupRestoreFailureEmitter deferred_restore_failure;
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::PlaySetup);
    OnMemoryBankDiagnosticPair post_playsetup_diagnostic;
    auto deferred_onmemory_diagnostic = make_deferred_noexcept_action([&]() noexcept {
        log_onmemory_bank_pair("post_custom_playsetup", post_playsetup_diagnostic);
    });
    ChartAudioPlaySetupDiagnostic chart_diagnostic;
    chart_diagnostic.entry_captured = true;
    chart_diagnostic.tls = current_chart_audio_expand_tls();
    ChartAudioDiagnosticTransaction tls_transaction;
    if (chart_diagnostic.tls.active
        && chart_audio_diagnostic_generation_exact(
            chart_diagnostic.tls.generation, tls_transaction)) {
        chart_diagnostic.proposed = true;
        chart_diagnostic.associated_by_tls = true;
        chart_diagnostic.generation =
            chart_diagnostic.tls.generation;
    }
    auto deferred_chart_diagnostic = make_deferred_noexcept_action([&]() noexcept {
        log_chart_audio_play_setup_diagnostic(chart_diagnostic);
        if (chart_diagnostic.proposed && chart_diagnostic.generation != 0) {
            ChartAudioDiagnosticTransaction transaction;
            transaction.active = true;
            transaction.generation = chart_diagnostic.generation;
            finish_chart_audio_diagnostic_transaction(
                transaction,
                chart_diagnostic.publication_succeeded
                    ? ChartAudioDiagnosticTerminalOutcome::AudioPublished
                    : ChartAudioDiagnosticTerminalOutcome::AudioFailed,
                chart_diagnostic.publication_succeeded
                    && chart_diagnostic.lifecycle_transition_exact
                    ? chart_diagnostic.lifecycle_epoch_after : 0);
        }
    });
    if (chart_diagnostic.proposed) {
        deferred_chart_diagnostic.make_eligible();
    }
    OwnerPatchMetadataNormalizationDiagnostic owner_patch_normalization;
    auto deferred_owner_patch_normalization = make_deferred_noexcept_action([&]() noexcept {
        log_owner_patch_metadata_normalization(owner_patch_normalization);
    });
    DeferredCustomActivationQuarantineMarker quarantine_marker;
    const auto forward_unmodified = [&](const ChartAudioPlaySetupDecision decision,
                                        const bool committed_custom_activation,
                                        const bool quarantine_active) {
        if (!committed_custom_play_setup_may_forward_unmodified(
                committed_custom_activation, quarantine_active)) {
            return;
        }
        chart_diagnostic.decision = decision;
        chart_diagnostic.original_forwarded = true;
        chart_diagnostic.original_forwarded_unmodified = true;
        chart_diagnostic.native_return = call_play_setup_original(
            sound, arg1, arg2, arg3, arg4, flag, arg6,
            chart_diagnostic.native_attempt);
    };
    std::unique_lock<std::recursive_mutex> operation_lock(g_audio_route_operations.mutex());
    CustomActivationQuarantineRecord active_quarantine;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        active_quarantine = g_custom_activation_quarantine;
    }
    if (active_quarantine) {
        const bool pointer_exact = sound
            && active_quarantine.callback_sound
                == reinterpret_cast<uintptr_t>(sound);
        const bool expected_identity_available =
            active_quarantine.callback_sound_index >= 0
            && active_quarantine.callback_sound_serial != 0;
        UObjectIdentity callback_identity;
        const bool identity_read_succeeded = pointer_exact
            && expected_identity_available
            && read_uobject_identity(sound, callback_identity);
        const bool identity_exact = identity_read_succeeded
            && callback_identity.live.internal_index
                == active_quarantine.callback_sound_index
            && static_cast<uint32_t>(callback_identity.live.serial_number)
                == active_quarantine.callback_sound_serial;
        if (custom_activation_quarantine_callback_suppressed(
                true, pointer_exact, expected_identity_available,
                identity_read_succeeded, identity_exact)) {
            quarantine_marker.propose(active_quarantine,
                CustomActivationQuarantineFailure::DuplicateCallback, 0);
            return;
        }
    }
    if (g_audio_route_disabled.load(std::memory_order_relaxed) || !sound) {
        if (g_audio_route_disabled.load(std::memory_order_acquire)) {
            clear_any_unpublished_audio_setup(
                AudioRouteTransitionReason::FeatureDisabled);
        }
        forward_unmodified(
            ChartAudioPlaySetupDecision::DisabledOrNull, false, false);
        return;
    }
    const PlaybackSnapshot playback = registry().playback_snapshot();
    SelectionSnapshot setup_selection;
    CustomContextToken setup_token;
    bool publicly_exposed = false;
    AudioRouteState armed_route;
    PrivateControllerSetupStage setup_stage =
        PrivateControllerSetupStage::AwaitingStop;
    CanonicalSubstrateBridgeAuthority substrate_bridge{};
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        armed_route = g_audio_route_state;
        if (playback.song && token_matches_route(playback.token, armed_route)) {
            static_cast<SelectionSnapshot&>(setup_selection) =
                static_cast<const SelectionSnapshot&>(playback);
            setup_token = playback.token;
            publicly_exposed = true;
        } else if (g_unpublished_audio_setup
            && token_matches_route(g_unpublished_audio_setup.token, armed_route)) {
            setup_selection = g_unpublished_audio_setup.selection;
            setup_token = g_unpublished_audio_setup.token;
            setup_stage = g_unpublished_audio_setup.controller_stage;
            substrate_bridge = g_unpublished_audio_setup.substrate_bridge;
        }
    }
    const SongDescriptor* song = setup_selection.song;
    if (!song || armed_route.phase != AudioRoutePhase::Armed
        || armed_route.desired_song_id != song->id
        || !token_matches_route(setup_token, armed_route)) {
        forward_unmodified(ChartAudioPlaySetupDecision::NoExactArm, false, false);
        return;
    }
    if (!has_audio_route_arm(song->id)) {
        chart_diagnostic.decision = ChartAudioPlaySetupDecision::ArmMissing;
        quarantine_marker.propose(quarantine_custom_activation(
                CustomActivationQuarantineFailure::ArmMissing, 0,
                setup_selection, setup_token, substrate_bridge, sound),
            CustomActivationQuarantineFailure::ArmMissing, 0);
        return;
    }
    chart_diagnostic.selection_generation = setup_selection.generation;
    chart_diagnostic.route_generation = armed_route.generation;
    chart_diagnostic.lease_generation = armed_route.lease_identity.generation;
    chart_diagnostic.song_key = audio_route_song_key(song->id);
    chart_diagnostic.setup_stage = static_cast<uint8_t>(setup_stage);
    if (!chart_diagnostic.proposed) {
        ChartAudioDiagnosticTransaction transaction;
        if (chart_audio_diagnostic_transaction_exact(
                chart_diagnostic.selection_generation,
                chart_diagnostic.route_generation,
                chart_diagnostic.lease_generation,
                chart_diagnostic.song_key,
                transaction)) {
            chart_diagnostic.proposed = true;
            chart_diagnostic.associated_by_exact_state = true;
            chart_diagnostic.generation = transaction.generation;
            deferred_chart_diagnostic.make_eligible();
        }
    }
    const SidecarRuntimeState* sidecar = nullptr;
    uint64_t route_generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        sidecar = find_ready_sidecar_locked(*song);
    }
    if (!sidecar) {
        chart_diagnostic.decision = ChartAudioPlaySetupDecision::SidecarMissing;
        quarantine_marker.propose(quarantine_custom_activation(
                CustomActivationQuarantineFailure::SidecarMissing, 0,
                setup_selection, setup_token, substrate_bridge, sound),
            CustomActivationQuarantineFailure::SidecarMissing, 0);
        core::log(core::LogLevel::Error, "[audio_sead] play_setup status=blocked_sidecar_not_ready arm=retained");
        return;
    }

    UObjectIdentity qualified_sound_identity;
    uint64_t qualified_owner_token = 0;
    PlaySetupQualificationDiagnostic bank_diagnostic;
    bank_diagnostic.valid = true;
    bank_diagnostic.callback_sound = reinterpret_cast<uintptr_t>(sound);
    bank_diagnostic.sound_identity_read_attempted = true;
    bank_diagnostic.sound_identity_read_succeeded = read_uobject_identity(
        sound, qualified_sound_identity);
    bank_diagnostic.sound_live_capture_succeeded =
        bank_diagnostic.sound_identity_read_succeeded
        && qualified_sound_identity.live_capture_succeeded;
    bank_diagnostic.sound_index_valid = bank_diagnostic.sound_live_capture_succeeded
        && qualified_sound_identity.live.internal_index >= 0;
    bank_diagnostic.sound_serial_valid = bank_diagnostic.sound_index_valid
        && qualified_sound_identity.live.serial_number > 0;
    bank_diagnostic.owner_read_attempted = bank_diagnostic.sound_serial_valid;
    bank_diagnostic.owner_read_succeeded = bank_diagnostic.owner_read_attempted
        && core::safe_read_field(sound,
            runtime_layouts::SqexSeadSound::observed_field548,
            qualified_owner_token);
    bank_diagnostic.owner_token = qualified_owner_token;
    bank_diagnostic.callback_sound_index =
        bank_diagnostic.sound_live_capture_succeeded
        ? qualified_sound_identity.live.internal_index : -1;
    bank_diagnostic.callback_sound_serial =
        bank_diagnostic.sound_live_capture_succeeded
        ? qualified_sound_identity.live.serial_number : 0;
    bank_diagnostic.callback_sound_identity_established =
        bank_diagnostic.sound_serial_valid;
    const bool sound_identity_qualified = bank_diagnostic.owner_read_succeeded;
    OnMemoryBankPlaySetupSnapshot bank_snapshot;
    OnMemoryBankPlaySetupPreflight bank_preflight;
    OnMemoryBankRouteDecision bank_preflight_decision =
        OnMemoryBankRouteDecision::SoundIdentityInvalid;
    const PlaybackSnapshot pre_query_playback = publicly_exposed
        ? registry().playback_snapshot() : PlaybackSnapshot{};
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const bool supported_build =
            g_audio_route_installed.load(std::memory_order_acquire);
        const bool lookup_signature_valid =
            g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire);
        const bool release_signature_valid =
            g_onmemory_bank_release_available.load(std::memory_order_acquire);
        bank_diagnostic.supported_build = supported_build;
        bank_diagnostic.lookup_signature_valid = lookup_signature_valid;
        bank_diagnostic.release_signature_valid = release_signature_valid;
        const AudioRouteState& current_route = g_audio_route_state;
        const bool route_matches = current_route.phase == AudioRoutePhase::Armed
            && current_route.generation == armed_route.generation
            && current_route.lease_identity == armed_route.lease_identity
            && current_route.desired_song_id == song->id
            && current_route.controller == armed_route.controller
            && token_matches_route(setup_token, current_route);
        const bool setup_matches = publicly_exposed
            ? pre_query_playback.song == setup_selection.song
                && pre_query_playback.generation == setup_selection.generation
                && pre_query_playback.token == setup_token
            : g_unpublished_audio_setup
                && g_unpublished_audio_setup.selection.song == setup_selection.song
                && g_unpublished_audio_setup.selection.generation
                    == setup_selection.generation
                && g_unpublished_audio_setup.token == setup_token
                && g_unpublished_audio_setup.controller == current_route.controller;
        const bool sidecar_matches = find_ready_sidecar_locked(*song) == sidecar
            && sidecar->allocation.sead_header()
            && sidecar->allocation.allocation_size() != 0
            && sidecar->allocation.mabf_size() != 0;
        bank_diagnostic.route_matches = route_matches;
        bank_diagnostic.setup_matches = setup_matches;
        bank_diagnostic.sidecar_matches = sidecar_matches;
        const bool bridge_candidate = !publicly_exposed;
        const CanonicalSubstrateBridgePlaySetupFacts bridge_facts{
                bank_diagnostic.owner_read_succeeded
                    && qualified_owner_token == 0,
                substrate_bridge.phase
                    == CanonicalSubstrateBridgePhase::StopObserved,
                substrate_bridge.generation != 0,
                substrate_bridge.transaction_generation != 0,
                substrate_bridge.release_completion_epoch != 0,
                substrate_bridge.revocation_epoch
                    == selection_audio_activation_revocation_epoch(),
                substrate_bridge.selection_generation
                    == setup_selection.generation,
                substrate_bridge.route_generation == current_route.generation,
                substrate_bridge.lease == current_route.lease_identity,
                substrate_bridge.song_key
                    == current_route.lease_identity.song_key,
                substrate_bridge.controller == current_route.controller,
                controller_identity_proof_matches(
                    substrate_bridge.controller_proof,
                    g_unpublished_audio_setup.controller_proof),
                substrate_bridge.slot == g_unpublished_audio_setup.slot
                    && substrate_bridge.bgm == g_unpublished_audio_setup.bgm,
                substrate_bridge.old_canonical_sound
                        == g_unpublished_audio_setup.expected_sound
                    && private_object_handle_matches(
                        substrate_bridge.old_canonical_sound_identity,
                        g_unpublished_audio_setup.expected_sound_handle),
                substrate_bridge.old_canonical_request
                    == g_unpublished_audio_setup.request_before_set,
                substrate_bridge.expected_callback_sound == sound,
                private_object_handle_matches(
                    substrate_bridge.expected_callback_sound_identity,
                    qualified_sound_identity.live),
                substrate_bridge.canonical_token != 0,
            };
        const auto bridge_first_failure = bridge_candidate
            ? first_canonical_substrate_bridge_play_setup_failure(bridge_facts)
            : CanonicalSubstrateBridgePlaySetupFailure::None;
        const bool bridge_exact = bridge_candidate
            && bridge_first_failure
                == CanonicalSubstrateBridgePlaySetupFailure::None;
        if (substrate_bridge.phase != CanonicalSubstrateBridgePhase::None) {
            chart_diagnostic.substrate_bridge = true;
            chart_diagnostic.substrate_bridge_generation =
                substrate_bridge.generation;
            chart_diagnostic.substrate_bridge_phase = bridge_exact
                ? CanonicalSubstrateBridgePhase::StopObserved
                : CanonicalSubstrateBridgePhase::Failed;
            chart_diagnostic.substrate_bridge_canonical =
                substrate_bridge.canonical_token;
            chart_diagnostic.substrate_bridge_first_failure =
                bridge_first_failure;
        }
        bank_diagnostic.entry_first_failure =
            first_play_setup_qualification_entry_failure({
                bank_diagnostic.sound_identity_read_succeeded,
                bank_diagnostic.sound_live_capture_succeeded,
                bank_diagnostic.sound_index_valid,
                bank_diagnostic.sound_serial_valid,
                bank_diagnostic.owner_read_succeeded,
                route_matches,
                setup_matches,
                sidecar_matches,
            });
        if (sound_identity_qualified && route_matches && setup_matches
            && sidecar_matches) {
            bank_preflight = g_onmemory_bank_lifecycle.snapshot_play_setup(
                true,
                supported_build,
                lookup_signature_valid,
                release_signature_valid,
                {sound, qualified_sound_identity.live},
                qualified_owner_token,
                armed_route.generation,
                bridge_exact ? substrate_bridge.canonical_token : 0);
            bank_diagnostic.preflight_reached = true;
            bank_preflight_decision = bank_preflight.decision;
            bank_snapshot = bank_preflight.snapshot;
            bank_diagnostic.preflight = bank_preflight;
        }
    }
    static std::atomic_int s_canonical_rebase_logs{0};
    const int canonical_rebase_log_index =
        bank_preflight.canonical_rebase_attempted
        ? s_canonical_rebase_logs.fetch_add(1, std::memory_order_relaxed)
        : -1;
    const bool log_canonical_rebase = canonical_rebase_log_index >= 0
        && canonical_rebase_log_index < 32;
    const auto log_canonical_rebase_status = [&](const char* status,
                                                  const char* stage,
                                                  uint32_t kind) {
        if (!log_canonical_rebase) return;
        const OnMemoryBankCanonicalRebaseFacts& facts =
            bank_preflight.canonical_rebase_facts;
        std::ostringstream out;
        out << "[audio_sead] onmemory_canonical_rebase status=" << status
            << " stage=" << stage
            << " decision=" << static_cast<unsigned>(bank_preflight_decision)
            << " kind=" << kind
            << " old=0x" << std::hex
            << bank_preflight.stale_canonical.encode()
            << " candidate=0x" << bank_preflight.candidate_canonical.encode()
            << " prior_custom=0x" << bank_preflight.prior_custom.encode()
            << std::dec
            << " facts=healthy:" << (facts.lifecycle_healthy ? 1 : 0)
            << ",no_release:" << (facts.no_release_in_flight ? 1 : 0)
            << ",no_pending:" << (facts.no_active_pending ? 1 : 0)
            << ",canonical:" << (facts.canonical_ready ? 1 : 0)
            << ",sound:" << (facts.same_sound_identity ? 1 : 0)
            << ",candidate_type1:" << (facts.candidate_type1 ? 1 : 0)
            << ",candidate_new:"
            << (facts.candidate_differs_stale_canonical ? 1 : 0)
            << ",custom_absent:"
            << (facts.completed_custom_absence ? 1 : 0)
            << ",completed_sound:"
            << (facts.completed_sound_matches ? 1 : 0)
            << ",completed_canonical:"
            << (facts.completed_canonical_matches ? 1 : 0)
            << ",prior_custom_type1:"
            << (facts.prior_custom_type1 ? 1 : 0)
            << ",not_prior_custom:"
            << (facts.candidate_differs_prior_custom ? 1 : 0);
        core::log(status && std::strcmp(status, "blocked") == 0
                ? core::LogLevel::Error : core::LogLevel::Info,
            out.str());
    };
    if (bank_preflight.canonical_rebase_attempted) {
        log_canonical_rebase_status(
            bank_preflight_decision == OnMemoryBankRouteDecision::Allowed
                ? "admitted" : "blocked",
            "preflight", UINT32_MAX);
    }
    uint32_t queried_canonical_kind = 0;
    if (bank_preflight_decision == OnMemoryBankRouteDecision::Allowed) {
        operation_lock.unlock();
        queried_canonical_kind = lookup_onmemory_bank_kind_noexcept(
            bank_snapshot.query_token.encode());
        bank_diagnostic.bank_lookup_reached = true;
        bank_diagnostic.queried_canonical_kind = queried_canonical_kind;
        operation_lock.lock();

        UObjectIdentity revalidated_sound_identity;
        uint64_t revalidated_owner_token = 0;
        const bool sound_unchanged = read_uobject_identity(
                sound, revalidated_sound_identity)
            && revalidated_sound_identity.live_capture_succeeded
            && revalidated_sound_identity.live.internal_index
                == qualified_sound_identity.live.internal_index
            && revalidated_sound_identity.live.serial_number
                == qualified_sound_identity.live.serial_number
            && uobject_identity_matches(sound, qualified_sound_identity)
            && core::safe_read_field(sound,
                runtime_layouts::SqexSeadSound::observed_field548,
                revalidated_owner_token)
            && revalidated_owner_token == qualified_owner_token;
        const bool controller_unchanged = !armed_route.controller
            || uobject_identity_matches(
                armed_route.controller, armed_route.controller_identity);
        const PlaybackSnapshot post_query_playback = publicly_exposed
            ? registry().playback_snapshot() : PlaybackSnapshot{};
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const AudioRouteState& current_route = g_audio_route_state;
        const bool registry_generation_unchanged =
            setup_token.registry_generation == setup_selection.generation
            && (!publicly_exposed
                || post_query_playback.generation == pre_query_playback.generation);
        const bool route_generation_unchanged =
            current_route.generation == armed_route.generation
            && setup_token.route_generation == current_route.generation;
        const bool lease_unchanged =
            current_route.lease_identity == armed_route.lease_identity
            && setup_token.lease_generation
                == current_route.lease_identity.generation;
        const bool song_unchanged = current_route.desired_song_id == song->id
            && setup_token.song_key == audio_route_song_key(song->id);
        const bool route_controller_unchanged = controller_unchanged
            && current_route.controller == armed_route.controller;
        const bool setup_matches = publicly_exposed
            ? post_query_playback.song == setup_selection.song
                && post_query_playback.generation == setup_selection.generation
                && post_query_playback.token == setup_token
            : g_unpublished_audio_setup
                && g_unpublished_audio_setup.selection.song == setup_selection.song
                && g_unpublished_audio_setup.selection.generation
                    == setup_selection.generation
                && g_unpublished_audio_setup.token == setup_token
                && g_unpublished_audio_setup.controller == current_route.controller;
        const bool sidecar_matches = find_ready_sidecar_locked(*song) == sidecar
            && sidecar->allocation.sead_header()
            && sidecar->allocation.allocation_size() != 0
            && sidecar->allocation.mabf_size() != 0;
        const bool bridge_revalidated =
            !bank_snapshot.deferred_canonical_establishment
            || (!publicly_exposed
                && substrate_bridge.phase
                    == CanonicalSubstrateBridgePhase::StopObserved
                && substrate_bridge.revocation_epoch
                    == selection_audio_activation_revocation_epoch()
                && g_unpublished_audio_setup.substrate_bridge.generation
                    == substrate_bridge.generation
                && g_unpublished_audio_setup.substrate_bridge.phase
                    == CanonicalSubstrateBridgePhase::StopObserved
                && substrate_bridge.expected_callback_sound == sound
                && private_object_handle_matches(
                    substrate_bridge.expected_callback_sound_identity,
                    revalidated_sound_identity.live)
                && substrate_bridge.controller == current_route.controller
                && substrate_bridge.route_generation == current_route.generation
                && substrate_bridge.lease == current_route.lease_identity
                && substrate_bridge.selection_generation
                    == setup_selection.generation);
        const PlaySetupQualificationRevalidation revalidation{
            sound_unchanged,
            revalidated_owner_token == qualified_owner_token,
            registry_generation_unchanged,
            route_generation_unchanged,
            lease_unchanged,
            song_unchanged,
            route_controller_unchanged,
            current_route.phase == AudioRoutePhase::Armed,
            setup_matches && token_matches_route(setup_token, current_route),
            sidecar_matches && bridge_revalidated,
        };
        bank_diagnostic.revalidation_reached = true;
        bank_diagnostic.revalidation = revalidation;
        bank_diagnostic.revalidation_first_failure =
            first_play_setup_qualification_revalidation_failure(revalidation);
        if (bank_diagnostic.revalidation_first_failure
            == PlaySetupQualificationRevalidationFailure::None) {
            bank_diagnostic.commit_reached = true;
            bank_diagnostic.commit =
                g_onmemory_bank_lifecycle.commit_play_setup_qualification(
                bank_snapshot,
                {sound, revalidated_sound_identity.live},
                revalidated_owner_token,
                armed_route.generation,
                queried_canonical_kind);
            bank_preflight_decision = bank_diagnostic.commit.decision;
            chart_diagnostic.lifecycle_epoch_before =
                bank_diagnostic.commit.prior_state_epoch;
            chart_diagnostic.lifecycle_epoch_after =
                bank_diagnostic.commit.committed_state_epoch;
            chart_diagnostic.lifecycle_transition_exact =
                bank_preflight_decision == OnMemoryBankRouteDecision::Allowed
                && chart_diagnostic.lifecycle_epoch_before != 0
                && chart_diagnostic.lifecycle_epoch_before != UINT64_MAX
                && chart_diagnostic.lifecycle_epoch_after
                    == chart_diagnostic.lifecycle_epoch_before + 1;
            if (bank_preflight_decision == OnMemoryBankRouteDecision::Allowed
                && bank_snapshot.deferred_canonical_establishment) {
                g_unpublished_audio_setup.substrate_bridge.phase =
                    CanonicalSubstrateBridgePhase::OwnerZeroQualified;
                chart_diagnostic.substrate_bridge_phase =
                    CanonicalSubstrateBridgePhase::OwnerZeroQualified;
                chart_diagnostic.substrate_bridge_canonical_kind =
                    queried_canonical_kind;
            }
        } else {
            bank_preflight_decision = OnMemoryBankRouteDecision::SnapshotDrift;
        }
    }
    if (bank_snapshot.rebasing_canonical) {
        log_canonical_rebase_status(
            bank_preflight_decision == OnMemoryBankRouteDecision::Allowed
                ? "committed" : "blocked",
            "commit", queried_canonical_kind);
    }
    if (bank_preflight_decision != OnMemoryBankRouteDecision::Allowed) {
        UnpublishedAudioSetupContext setup_context;
        setup_context.selection = setup_selection;
        setup_context.token = setup_token;
        if (!abandon_unmodified_custom_audio_arm(
                playback, setup_context, publicly_exposed, bank_diagnostic)) {
            g_audio_route_disabled.store(true, std::memory_order_release);
        }
        const auto decision = sound_identity_qualified
            ? ChartAudioPlaySetupDecision::BankQualificationFailed
            : ChartAudioPlaySetupDecision::SoundIdentityInvalid;
        chart_diagnostic.decision = decision;
        const auto quarantine_failure = sound_identity_qualified
            ? CustomActivationQuarantineFailure::BankQualificationFailed
            : CustomActivationQuarantineFailure::SoundIdentityInvalid;
        quarantine_marker.propose(quarantine_custom_activation(
                quarantine_failure,
                static_cast<uint32_t>(bank_preflight_decision),
                setup_selection, setup_token, substrate_bridge, sound),
            quarantine_failure, static_cast<uint32_t>(bank_preflight_decision));
        return;
    }

    if (bank_snapshot.deferred_canonical_establishment) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        chart_diagnostic.substrate_bridge_route_generation_before_restore =
            g_audio_route_state.generation;
    }
    chart_diagnostic.substrate_bridge_restore_attempted =
        bank_snapshot.deferred_canonical_establishment;
    const bool pending_restore_succeeded =
        restore_pending_play_setup_patch("replace_pending");
    chart_diagnostic.substrate_bridge_restore_succeeded =
        bank_snapshot.deferred_canonical_establishment && pending_restore_succeeded;
    chart_diagnostic.substrate_bridge_restore_failed =
        bank_snapshot.deferred_canonical_establishment && !pending_restore_succeeded;
    if (!pending_restore_succeeded) {
        if (bank_snapshot.deferred_canonical_establishment) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            auto& bridge = g_unpublished_audio_setup.substrate_bridge;
            chart_diagnostic.substrate_bridge_route_generation_after_restore =
                g_audio_route_state.generation;
            chart_diagnostic.substrate_bridge_restore_prepatch_failure =
                CanonicalSubstrateBridgeRestorePrepatchFailure::RestoreFailed;
            if (bridge.generation == substrate_bridge.generation) {
                bridge.phase = CanonicalSubstrateBridgePhase::Failed;
                chart_diagnostic.substrate_bridge_phase = bridge.phase;
            }
            g_onmemory_bank_lifecycle.cancel_qualification(
                bank_snapshot.route_epoch);
            g_audio_route_disabled.store(true, std::memory_order_release);
        }
        chart_diagnostic.decision =
            ChartAudioPlaySetupDecision::PendingRestoreFailed;
        const uint32_t nested_failure = static_cast<uint32_t>(
            chart_diagnostic.substrate_bridge_restore_prepatch_failure);
        quarantine_marker.propose(quarantine_custom_activation(
                CustomActivationQuarantineFailure::PendingRestoreFailed,
                nested_failure,
                setup_selection, setup_token, substrate_bridge, sound),
            CustomActivationQuarantineFailure::PendingRestoreFailed,
            nested_failure);
        core::log(core::LogLevel::Error, "[audio_sead] play_setup status=blocked_pending_restore_failed");
        return;
    }

    if (bank_snapshot.deferred_canonical_establishment) {
        bool post_restore_exact = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            auto& bridge = g_unpublished_audio_setup.substrate_bridge;
            const uint64_t old_setup_generation =
                g_unpublished_audio_setup.token.route_generation;
            const uint64_t route_generation_after_restore =
                g_audio_route_state.generation;
            const uint64_t prospective_generation =
                route_generation_after_restore != UINT64_MAX
                    ? route_generation_after_restore + 1 : 0;
            const auto prepatch = canonical_substrate_bridge_restore_prepatch(
                true,
                bridge.phase,
                bridge.generation != 0
                    && bridge.generation == substrate_bridge.generation,
                bridge.route_generation,
                old_setup_generation,
                route_generation_after_restore,
                prospective_generation);
            chart_diagnostic.substrate_bridge_route_generation_after_restore =
                route_generation_after_restore;
            chart_diagnostic.substrate_bridge_old_route_generation =
                bridge.route_generation;
            chart_diagnostic.substrate_bridge_old_setup_generation =
                old_setup_generation;
            chart_diagnostic.substrate_bridge_patched_generation =
                prospective_generation;
            chart_diagnostic.substrate_bridge_restore_prepatch_failure =
                prepatch.failure;
            chart_diagnostic.substrate_bridge_patch_failure =
                prepatch.transition.failure;
            chart_diagnostic.substrate_bridge_post_restore_check_attempted =
                prepatch.post_restore_check_attempted;
            chart_diagnostic.substrate_bridge_post_restore_exact =
                prepatch.post_restore_exact;
            chart_diagnostic.substrate_bridge_successor_exact =
                prepatch.post_restore_exact;
            post_restore_exact = prepatch.new_patch_allowed;
            if (!post_restore_exact
                && bridge.generation == substrate_bridge.generation) {
                bridge.phase = CanonicalSubstrateBridgePhase::Failed;
                chart_diagnostic.substrate_bridge_phase = bridge.phase;
            }
            if (!post_restore_exact) {
                g_onmemory_bank_lifecycle.cancel_qualification(
                    bank_snapshot.route_epoch);
                g_audio_route_disabled.store(true, std::memory_order_release);
            }
        }
        if (!post_restore_exact) {
            chart_diagnostic.decision = ChartAudioPlaySetupDecision::RouteChanged;
            const uint32_t nested_failure = static_cast<uint32_t>(
                chart_diagnostic.substrate_bridge_patch_failure);
            quarantine_marker.propose(quarantine_custom_activation(
                    CustomActivationQuarantineFailure::PostRestoreAuthorityFailed,
                    nested_failure,
                    setup_selection, setup_token, substrate_bridge, sound),
                CustomActivationQuarantineFailure::PostRestoreAuthorityFailed,
                nested_failure);
            return;
        }
    }

    std::vector<AudioFieldPatch> applied;
    chart_diagnostic.substrate_bridge_new_patch_invoked =
        bank_snapshot.deferred_canonical_establishment;
    if (!patch_sound_for_sidecar_call(sound, *song, *sidecar, "play_setup", applied)) {
        chart_diagnostic.decision = ChartAudioPlaySetupDecision::PatchFailed;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_onmemory_bank_lifecycle.cancel_qualification(bank_snapshot.route_epoch);
        }
        quarantine_marker.propose(quarantine_custom_activation(
                CustomActivationQuarantineFailure::PatchFailed, 0,
                setup_selection, setup_token, substrate_bridge, sound),
            CustomActivationQuarantineFailure::PatchFailed, 0);
        core::log(core::LogLevel::Error, "[audio_sead] play_setup status=blocked_patch_failed arm=retained");
        return;
    }
    bool route_still_armed = false;
    AudioRouteState patched_route;
    CustomContextToken patched_token;
    bool token_updated = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route_still_armed = g_audio_route_state.phase == AudioRoutePhase::Armed
            && g_audio_route_state.desired_song_id == song->id;
        if (route_still_armed && bank_snapshot.deferred_canonical_establishment) {
            const auto& bridge = g_unpublished_audio_setup.substrate_bridge;
            const uint64_t old_setup_generation =
                g_unpublished_audio_setup.token.route_generation;
            const uint64_t prospective_generation =
                g_audio_route_state.generation != UINT64_MAX
                    ? g_audio_route_state.generation + 1 : 0;
            const auto transition = canonical_substrate_bridge_patch_transition(
                bridge.phase,
                bridge.generation != 0
                    && bridge.generation == substrate_bridge.generation,
                bridge.route_generation,
                old_setup_generation,
                g_audio_route_state.generation == setup_token.route_generation
                    ? g_audio_route_state.generation : 0,
                prospective_generation);
            chart_diagnostic.substrate_bridge_patch_failure = transition.failure;
            route_still_armed = chart_diagnostic.substrate_bridge_patch_failure
                == CanonicalSubstrateBridgePatchTransitionFailure::None;
        }
        if (route_still_armed) {
            g_pending_play_setup_patch.sound = sound;
            g_pending_play_setup_patch.song_id = song->id;
            g_pending_play_setup_patch.patches = applied;
            g_pending_play_setup_patch.restore_safe = true;
            g_audio_route_state.phase = AudioRoutePhase::PatchedPlaySetup;
            g_audio_route_state.patched_song_id = song->id;
            g_audio_route_state.sound = sound;
            g_audio_route_state.controller = nullptr;
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.mark_native_arm_attempt(
                g_audio_route_state.lease_identity);
            route_generation = ++g_audio_route_state.generation;
            patched_route = g_audio_route_state;
            patched_token = custom_context_token(
                setup_selection.generation, patched_route);
            patched_token.sound = sound;
            if (bank_snapshot.deferred_canonical_establishment) {
                auto& bridge = g_unpublished_audio_setup.substrate_bridge;
                token_updated = g_unpublished_audio_setup.advance(
                    setup_token, patched_token);
                if (token_updated) {
                    const auto transition =
                        canonical_substrate_bridge_patch_transition(
                            bridge.phase,
                            bridge.generation != 0
                                && bridge.generation == substrate_bridge.generation,
                            bridge.route_generation,
                            setup_token.route_generation,
                            setup_token.route_generation,
                            patched_token.route_generation);
                    bridge.route_generation = transition.route_generation;
                    bridge.phase = transition.phase;
                    token_updated = transition.failure
                        == CanonicalSubstrateBridgePatchTransitionFailure::None;
                    chart_diagnostic.substrate_bridge_patch_failure =
                        transition.failure;
                    chart_diagnostic.substrate_bridge_patched_generation =
                        patched_token.route_generation;
                    chart_diagnostic.substrate_bridge_successor_exact =
                        canonical_substrate_bridge_route_generation_successor(
                            setup_token.route_generation,
                            patched_token.route_generation);
                    chart_diagnostic.substrate_bridge_generation_advanced =
                        transition.generation_advanced;
                    chart_diagnostic.substrate_bridge_phase = bridge.phase;
                } else {
                    bridge.phase = CanonicalSubstrateBridgePhase::Failed;
                    chart_diagnostic.substrate_bridge_phase = bridge.phase;
                }
            }
        } else if (bank_snapshot.deferred_canonical_establishment) {
            auto& bridge = g_unpublished_audio_setup.substrate_bridge;
            if (bridge.generation == substrate_bridge.generation) {
                bridge.phase = CanonicalSubstrateBridgePhase::Failed;
                chart_diagnostic.substrate_bridge_phase = bridge.phase;
            }
        }
    }
    if (!route_still_armed) {
        chart_diagnostic.decision = ChartAudioPlaySetupDecision::RouteChanged;
        if (!restore_patches_reverse(applied)) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_failed_patch_journal.insert(g_failed_patch_journal.end(), applied.begin(), applied.end());
            g_audio_route_disabled.store(true, std::memory_order_release);
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_onmemory_bank_lifecycle.cancel_qualification(bank_snapshot.route_epoch);
        }
        core::log(core::LogLevel::Error, "[audio_sead] play_setup status=blocked_route_changed_before_original");
        const uint32_t nested_failure = static_cast<uint32_t>(
            chart_diagnostic.substrate_bridge_patch_failure);
        quarantine_marker.propose(quarantine_custom_activation(
                CustomActivationQuarantineFailure::RouteGenerationFailed,
                nested_failure,
                setup_selection, setup_token, substrate_bridge, sound),
            CustomActivationQuarantineFailure::RouteGenerationFailed,
            nested_failure);
        return;
    }
    if (publicly_exposed) {
        token_updated = registry().update_playback_token(setup_token, patched_token);
    } else if (!bank_snapshot.deferred_canonical_establishment) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        token_updated = g_unpublished_audio_setup.advance(setup_token, patched_token);
    }
    if (!token_updated) {
        chart_diagnostic.decision = ChartAudioPlaySetupDecision::TokenUpdateFailed;
        (void)restore_pending_play_setup_patch("playback_token_update_failed");
        if (publicly_exposed) (void)revoke_playback_snapshot(playback);
        else clear_unpublished_audio_setup(
            armed_route.lease_identity,
            AudioRouteTransitionReason::PlaySetupRejected);
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_onmemory_bank_lifecycle.cancel_qualification(bank_snapshot.route_epoch);
        }
        core::log(core::LogLevel::Error,
            "[audio_sead] play_setup status=blocked_playback_token_update_failed");
        quarantine_marker.propose(quarantine_custom_activation(
                CustomActivationQuarantineFailure::SetupTokenAdvanceFailed, 0,
                setup_selection, setup_token, substrate_bridge, sound),
            CustomActivationQuarantineFailure::SetupTokenAdvanceFailed, 0);
        return;
    }
    std::array<uint8_t, 12> arg6_snapshot{};
    const bool arg6_snapshot_valid = arg6
        && core::safe_copy_bytes(arg6, arg6_snapshot.data(), arg6_snapshot.size());
    static std::atomic_int s_logs{0};
    const int log_index = s_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 64) {
        uint64_t arg6_head = 0;
        uint32_t arg6_tail = 0;
        uint32_t arg1_bits = 0;
        uint32_t arg2_bits = 0;
        std::memcpy(&arg1_bits, &arg1, sizeof(arg1_bits));
        std::memcpy(&arg2_bits, &arg2, sizeof(arg2_bits));
        const bool arg6_read = arg6
            && core::safe_read_field(arg6, 0, arg6_head)
            && core::safe_read_field(arg6, sizeof(arg6_head), arg6_tail);
        std::ostringstream out;
        out << "[audio_sead] play_setup status=patched_for_original_call"
            << " song_id=" << song->id
            << " sound=0x" << std::hex << reinterpret_cast<uintptr_t>(sound)
            << " sidecar=0x" << reinterpret_cast<uintptr_t>(sidecar->allocation.sead_header())
            << " arg1_bits=0x" << arg1_bits
            << " arg2_bits=0x" << arg2_bits
            << " arg3=0x" << arg3
            << " arg4=0x" << arg4
            << " flag=0x" << static_cast<unsigned>(flag)
            << " arg6=0x" << reinterpret_cast<uintptr_t>(arg6)
            << " arg6_read=" << (arg6_read ? 1 : 0)
            << " arg6_head=0x" << arg6_head
            << " arg6_tail=0x" << arg6_tail
            << std::dec
            << " patch_count=" << applied.size();
        core::log(core::LogLevel::Info, out.str());
    }
    PlaySetupOriginalScope original_scope(route_generation);
    chart_diagnostic.decision = ChartAudioPlaySetupDecision::PatchedForOriginal;
    chart_diagnostic.patch_applied = true;
    chart_diagnostic.original_forwarded = true;
    chart_diagnostic.original_forwarded_patched = true;
    chart_diagnostic.substrate_bridge_original_invoked =
        bank_snapshot.deferred_canonical_establishment;
    chart_diagnostic.native_return = call_play_setup_original(
        sound, arg1, arg2, arg3, arg4, flag, arg6,
        chart_diagnostic.native_attempt);
    uint64_t custom_onmemory_token = 0;
    const bool custom_onmemory_token_copied = core::safe_read_field(
        sound, runtime_layouts::SqexSeadSound::observed_field548,
        custom_onmemory_token);
    DecodedOnMemoryBankToken canonical_onmemory;
    DecodedOnMemoryBankToken custom_onmemory;
    bool bank_tokens_observed = false;
    bool bank_tokens_decoded = false;
    bool deferred_bridge_original_entered = false;
    bool cleanup_only_bridge_authority = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const auto& bridge = g_unpublished_audio_setup.substrate_bridge;
        deferred_bridge_original_entered = bank_snapshot.deferred_canonical_establishment
            && chart_diagnostic.substrate_bridge_original_invoked
            && bridge.generation == substrate_bridge.generation;
        cleanup_only_bridge_authority = deferred_bridge_original_entered
            && (bridge.phase == CanonicalSubstrateBridgePhase::SetBound
                || bridge.phase == CanonicalSubstrateBridgePhase::Failed);
    }
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const OnMemoryBankCanonicalRecord& canonical =
            g_onmemory_bank_lifecycle.canonical();
        const bool bridge_set_bound =
            !bank_snapshot.deferred_canonical_establishment
            || (g_unpublished_audio_setup.substrate_bridge.generation
                    == substrate_bridge.generation
                && g_unpublished_audio_setup.substrate_bridge.phase
                    == CanonicalSubstrateBridgePhase::SetBound);
        if (bank_snapshot.deferred_canonical_establishment) {
            chart_diagnostic.substrate_bridge_set_bound = bridge_set_bound;
        }
        // Both roles' tokens are decoded whether or not they differ.  Whether
        // the custom role observes a *separate* bank is what distinguishes
        // detached ownership from a shared, already-resident bank, and that
        // distinction belongs to the lifecycle's retirement classification --
        // not to token decoding.  Folding it in here left the shared case with
        // no decoded custom token and no bank kinds at all, which made the
        // shared outcome undetectable downstream.
        bank_tokens_observed = canonical
            && canonical.sound
                == OnMemoryBankSoundIdentity{sound, qualified_sound_identity.live}
            && g_onmemory_bank_lifecycle.qualification_matches(
                {sound, qualified_sound_identity.live},
                armed_route.generation,
                qualified_owner_token)
            && custom_onmemory_token_copied
            && decode_onmemory_bank_token(custom_onmemory_token, custom_onmemory)
            && exact_type1_onmemory_token(custom_onmemory)
            && (bridge_set_bound || cleanup_only_bridge_authority);
        bank_tokens_decoded = bank_tokens_observed
            && exact_distinct_type1_onmemory_tokens(
                canonical.token, custom_onmemory);
        if (bank_tokens_observed) canonical_onmemory = canonical.token;
    }
    uint32_t canonical_kind = 0;
    uint32_t custom_kind = 0;
    if (bank_tokens_observed) {
        operation_lock.unlock();
        canonical_kind = lookup_onmemory_bank_kind_noexcept(
            canonical_onmemory.encode());
        custom_kind = lookup_onmemory_bank_kind_noexcept(
            custom_onmemory.encode());
        operation_lock.lock();
        if (bank_snapshot.deferred_canonical_establishment) {
            chart_diagnostic.substrate_bridge_custom = custom_onmemory.encode();
            chart_diagnostic.substrate_bridge_canonical_kind = canonical_kind;
            chart_diagnostic.substrate_bridge_custom_kind = custom_kind;
        }
    }
    UObjectIdentity post_query_sound_identity;
    uint64_t post_query_owner_token = 0;
    bool bank_lifecycle_revalidated = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const OnMemoryBankCanonicalRecord& canonical =
            g_onmemory_bank_lifecycle.canonical();
        bank_lifecycle_revalidated = canonical
            && canonical.token.encode() == canonical_onmemory.encode()
            && g_onmemory_bank_lifecycle.qualification_matches(
                {sound, qualified_sound_identity.live},
                armed_route.generation,
                qualified_owner_token);
    }
    // Evidence common to both retirement outcomes.  The native reads below are
    // performed once, for the shared case as well as the detached one; the
    // token relationship is applied afterwards so the retirement classifier is
    // reachable either way.
    const bool bank_post_query_exact = bank_tokens_observed
        && bank_lifecycle_revalidated
        && canonical_kind == 2 && custom_kind == 2
        && read_uobject_identity(sound, post_query_sound_identity)
        && post_query_sound_identity.live_capture_succeeded
        && post_query_sound_identity.live.internal_index
            == qualified_sound_identity.live.internal_index
        && post_query_sound_identity.live.serial_number
            == qualified_sound_identity.live.serial_number
        && uobject_identity_matches(sound, qualified_sound_identity)
        && core::safe_read_field(sound,
            runtime_layouts::SqexSeadSound::observed_field548,
            post_query_owner_token)
        && post_query_owner_token == custom_onmemory_token;
    const bool bank_kind_qualified = onmemory_bank_detached_retirement_admitted(
        bank_post_query_exact, bank_tokens_decoded);
    const bool bank_shared_qualified = onmemory_bank_shared_retirement_admitted(
        bank_post_query_exact, bank_tokens_decoded);
    // A shared bank leaves the owner field already holding the canonical token,
    // so no restore write is needed and the restore machinery cannot report one
    // as applied.  This is the proof that stands in its place; the post-query
    // read above has already established the value.  It is false whenever the
    // shared admission is false, so the detached path is untouched.
    const bool shared_owner_field_exact = bank_shared_qualified
        && post_query_owner_token == canonical_onmemory.encode();
    void* const setup_controller = lookup_current_bgm_controller();
    const GuardedPlaySetupClaimReadResult pre_restore_claim_read =
        read_guarded_play_setup_claim_observation_result(setup_controller);
    chart_diagnostic.pre_restore_claim_read_attempted = true;
    chart_diagnostic.pre_restore_claim_read_failure =
        pre_restore_claim_read.failure;
    chart_diagnostic.pre_restore_claim_audio_chain_failure =
        pre_restore_claim_read.audio_chain_failure;
    const GuardedPlaySetupClaimObservation pre_restore_observation =
        pre_restore_claim_read.observation;
    const bool pre_restore_observed = static_cast<bool>(pre_restore_claim_read);
    log_slot_setup_snapshot("native_playsetup_after_original", setup_controller);
    const auto play_setup_tls = audio_production_play_setup_tls();
    bool native_play_claimed = play_setup_tls.play_claimed
        && play_setup_tls.expected_generation == route_generation;
    void* profile_controller = nullptr;
    void* profile_slot = nullptr;
    void* profile_bgm = nullptr;
    void* profile_sound = nullptr;
    uint64_t custom_request_handle = 0;
    void* custom_backing = nullptr;
    UObjectIdentity profile_controller_identity;
    UObjectIdentity profile_sound_identity;
    bool guarded_stop_observed = false;
    CustomContextToken pre_restore_playing_token;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        native_play_claimed = native_play_claimed
            && g_audio_route_state.generation == route_generation
            && g_audio_route_state.phase == AudioRoutePhase::Playing
            && g_audio_route_state.patched_song_id == song->id
            && g_audio_route_state.sound == sound;
        if (native_play_claimed) {
            profile_controller = g_audio_route_state.controller;
            profile_controller_identity = g_audio_route_state.controller_identity;
            profile_slot = g_audio_route_state.owned_slot;
            profile_bgm = g_audio_route_state.owned_bgm;
            profile_sound = g_audio_route_state.owned_sound;
            profile_sound_identity = g_audio_route_state.owned_sound_identity;
            custom_request_handle = g_audio_route_state.owned_request_handle;
            native_play_claimed = profile_controller != nullptr
                && profile_slot != nullptr
                && profile_sound == sound
                && setup_controller == profile_controller;
            guarded_stop_observed = !publicly_exposed
                && g_unpublished_audio_setup.controller_stage
                    == PrivateControllerSetupStage::StopObserved;
            if (native_play_claimed && guarded_stop_observed) {
                pre_restore_playing_token = custom_context_token(
                    setup_selection.generation, g_audio_route_state);
                const GuardedPlaySetupNativeOwnerFacts owner_facts{
                    native_play_claimed,
                    g_audio_route_state.custom_resource_owned,
                    route_generation,
                    g_audio_route_state.generation,
                    g_audio_route_state.phase == AudioRoutePhase::Playing,
                    g_audio_route_state.patched_song_id == song->id,
                    sound,
                    g_audio_route_state.controller,
                    g_audio_route_state.owned_slot,
                    g_audio_route_state.owned_bgm,
                    g_audio_route_state.owned_sound,
                    profile_sound_identity.live,
                    g_audio_route_state.owned_request_handle,
                    pre_restore_playing_token,
                    pre_restore_observation,
                };
                native_play_claimed = pre_restore_observed
                    && guarded_play_setup_native_changes_allowed(
                        g_unpublished_audio_setup, owner_facts);
            }
            if (native_play_claimed && applied.size() == 5) {
                g_audio_route_state.frozen_sound_patch = {
                    route_generation,
                    g_audio_route_state.lease_identity,
                    sound,
                    profile_sound_identity,
                    applied,
                };
            }
        }
    }
    const bool custom_backing_observed = native_play_claimed
        && custom_request_handle != 0 && profile_bgm
        && core::safe_read_field(profile_bgm,
            runtime_layouts::SqexSeadBgm::backing_resource,
            custom_backing);
    native_play_claimed = native_play_claimed && custom_backing_observed;
    if (native_play_claimed) {
        AudioRouteState playing_route;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            playing_route = g_audio_route_state;
        }
        const CustomContextToken playing_token = custom_context_token(
            setup_selection.generation, playing_route);
        bool playing_token_updated = custom_route_claim_is_complete(playing_token);
        if (playing_token_updated && guarded_stop_observed) {
            playing_token_updated = playing_token == pre_restore_playing_token;
        }
        if (playing_token_updated && publicly_exposed) {
            playing_token_updated = registry().update_playback_token(
                patched_token, playing_token);
        } else if (playing_token_updated) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            playing_token_updated = g_unpublished_audio_setup.advance_or_confirm_claimed(
                patched_token, playing_token);
        }
        if (!playing_token_updated) {
            native_play_claimed = false;
            core::log(core::LogLevel::Error,
                "[audio_sead] play_setup status=playback_identity_publication_failed");
        }
    }
    if (native_play_claimed) {
        if (g_piano_audio_owner_tick) {
            g_piano_audio_owner_custom_playsetup = true;
            (void)capture_piano_audio_request_profile(song->id, song->base_slot, g_piano_audio_owner_tick);
        }
        (void)capture_slot_setup_profile(
            profile_controller, profile_controller_identity, profile_slot, flag);
        (void)capture_native_play_setup_profile(
            song->id,
            profile_controller,
            profile_controller_identity,
            profile_slot,
            profile_sound,
            profile_sound_identity,
            arg1,
            arg2,
            arg3,
            arg4,
            flag,
            arg6_snapshot,
            arg6_snapshot_valid);
    }

    // A shared bank never claims the native play -- nothing downstream of the
    // retirement classifier changes for it -- but it still has to reach that
    // classifier.  Capture its admission from the same refined evidence before
    // native_play_claimed is narrowed to the detached case.
    const bool shared_bank_play_qualified = native_play_claimed
        && bank_shared_qualified;
    native_play_claimed = native_play_claimed && bank_kind_qualified;
    const bool bank_retirement_qualified = native_play_claimed
        || shared_bank_play_qualified;
    AudioPatchRestoreExactOverride owner_restore;
    const AudioFieldPatch* owner_patch = nullptr;
    for (const AudioFieldPatch& patch : applied) {
        if (patch.object == sound
            && patch.offset == runtime_layouts::SqexSeadSound::observed_field548
            && patch.size == sizeof(uint64_t)
            && patch.label
            && std::strcmp(patch.label, "sound+0x548") == 0) {
            owner_patch = &patch;
            break;
        }
    }
    const bool cleanup_only_owner_restore = cleanup_only_bridge_authority
        && bank_kind_qualified;
    // owner_restore_verified is a hard predicate of both retirement outcomes,
    // so the restore has to be enabled for the shared case too.  With equal
    // tokens the restore writes the canonical token back over itself, which is
    // exactly the no-op it should be.
    owner_restore.enabled = ((bank_retirement_qualified && custom_backing_observed)
            || cleanup_only_owner_restore)
        && owner_patch && post_query_owner_token == custom_onmemory_token;
    owner_restore.fresh_native_owner_proof = bank_retirement_qualified
        || cleanup_only_owner_restore;
    owner_restore.lookup_signature_valid =
        g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire);
    owner_restore.release_signature_valid =
        g_onmemory_bank_release_available.load(std::memory_order_acquire);
    owner_restore.canonical_kind2_qualified = canonical_kind == 2;
    owner_restore.custom_kind2_qualified = custom_kind == 2;
    if (owner_patch) {
        owner_restore.object = owner_patch->object;
        owner_restore.offset = owner_patch->offset;
        owner_restore.size = owner_patch->size;
        owner_restore.label = owner_patch->label;
        owner_restore.expected_journal_original = owner_patch->original;
    }
    owner_restore.expected_current = custom_onmemory_token;
    owner_restore.restore_value = canonical_onmemory.encode();

    AudioPatchRestoreFailureReport* const restore_report =
        deferred_restore_failure.report();
    // The second argument is `preserve_playing_route`, and widening it to cover
    // the shared retirement is deliberate, not incidental.  A shared session is
    // a playing route in exactly the sense a detached one is: the play setup
    // claimed a native play, and the only difference is that the bank it needs
    // was already resident.  Two consequences follow, both intended:
    //
    //   * The flag reaches evaluate_audio_patch_restore as allow_native_changes,
    //     so a patched field the native has since rewritten to a third value is
    //     preserved as NativeOwned instead of failing the whole restore as
    //     Conflict and latching g_audio_route_disabled.  That is the correct
    //     reading on a live route -- the native owns those fields while it is
    //     playing -- and it is the same tolerance a detached play already gets.
    //   * Inside restore_pending_play_setup_patch it also stops the route's
    //     controller and phase from being cleared, so a shared session leaves
    //     play setup with a live controller and phase Playing.  Clearing them
    //     would misreport a route that is genuinely playing, and would strand
    //     the list-return shared authority, which compares the live controller
    //     against the route snapshot.
    const bool restored = restore_pending_play_setup_patch(
        "play_setup_return", bank_retirement_qualified || cleanup_only_owner_restore,
        false, restore_report,
        &owner_restore);
    GuardedPlaySetupClaimObservation guarded_observation;
    if (native_play_claimed && restored && guarded_stop_observed) {
        const GuardedPlaySetupClaimReadResult post_restore_claim_read =
            read_guarded_play_setup_claim_observation_result(profile_controller);
        chart_diagnostic.post_restore_claim_read_attempted = true;
        chart_diagnostic.post_restore_claim_read_failure =
            post_restore_claim_read.failure;
        chart_diagnostic.post_restore_claim_audio_chain_failure =
            post_restore_claim_read.audio_chain_failure;
        guarded_observation = post_restore_claim_read.observation;
        const bool post_restore_observed =
            static_cast<bool>(post_restore_claim_read);
        bool post_restore_token_updated = false;
        if (post_restore_observed) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const CustomContextToken post_restore_token = custom_context_token(
                setup_selection.generation, g_audio_route_state);
            post_restore_token_updated = advance_guarded_play_setup_after_restore(
                g_unpublished_audio_setup, pre_restore_playing_token,
                post_restore_token);
        }
        native_play_claimed = post_restore_observed && post_restore_token_updated;
    }
    bool registry_publication_succeeded = false;
    if (native_play_claimed && !publicly_exposed) {
        bool bridge_publication_authority = true;
        if (bank_snapshot.deferred_canonical_establishment) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            auto& bridge = g_unpublished_audio_setup.substrate_bridge;
            bridge_publication_authority =
                bridge.generation == substrate_bridge.generation
                && bridge.phase == CanonicalSubstrateBridgePhase::SetBound;
            if (!bridge_publication_authority
                && bridge.generation == substrate_bridge.generation) {
                bridge.phase = CanonicalSubstrateBridgePhase::Failed;
            }
        }
        native_play_claimed = native_play_claimed && restored
            && bridge_publication_authority;
        if (native_play_claimed) {
            registry_publication_succeeded = promote_unpublished_audio_setup(
                armed_route.lease_identity, AudioArmPublicationProof::PlaySetupClaimed,
                guarded_stop_observed ? &guarded_observation : nullptr);
            native_play_claimed = registry_publication_succeeded;
        }
        if (bank_snapshot.deferred_canonical_establishment) {
            chart_diagnostic.substrate_bridge_phase = native_play_claimed
                ? CanonicalSubstrateBridgePhase::Published
                : CanonicalSubstrateBridgePhase::Failed;
            chart_diagnostic.substrate_bridge_published = native_play_claimed;
        }
        if (!native_play_claimed) {
            core::log(core::LogLevel::Error,
                "[audio_sead] play_setup status=claim_publication_failed");
        }
    } else if (native_play_claimed && !restored) {
        native_play_claimed = false;
    }
    if (native_play_claimed && restored && owner_restore.applied) {
        owner_patch_normalization.attempted = true;
        deferred_owner_patch_normalization.make_eligible();
        owner_patch_normalization.restored = restored;
        owner_patch_normalization.exact_override_applied = owner_restore.applied;
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        auto& live = g_audio_route_state;
        auto& frozen = live.frozen_sound_patch;
        owner_patch_normalization.frozen_route_generation =
            frozen.route_generation;
        owner_patch_normalization.pre_restore_route_generation =
            route_generation;
        owner_patch_normalization.expected_live_route_generation =
            route_generation != UINT64_MAX ? route_generation + 1 : 0;
        owner_patch_normalization.live_route_generation = live.generation;
        owner_patch_normalization.route_successor_exact =
            audio_patch_original_rebase_route_successor_exact(
                frozen.route_generation, route_generation, live.generation);
        AudioPatchOriginalRebaseContextFacts normalization_context;
        normalization_context.route_generation_exact =
            live.phase == AudioRoutePhase::Playing
            && owner_patch_normalization.route_successor_exact
            && live.custom_resource_owned
            && live.owned_sound == sound;
        normalization_context.lease_exact =
            live.lease_identity == armed_route.lease_identity
            && frozen.lease_identity == armed_route.lease_identity;
        normalization_context.sound_pointer_exact =
            frozen.sound == sound && live.owned_sound == sound;
        normalization_context.sound_live_identity_exact =
            frozen.sound_identity.live.internal_index
                == qualified_sound_identity.live.internal_index
            && frozen.sound_identity.live.serial_number
                == qualified_sound_identity.live.serial_number
            && live.owned_sound_identity.live.internal_index
                == qualified_sound_identity.live.internal_index
            && live.owned_sound_identity.live.serial_number
                == qualified_sound_identity.live.serial_number;
        normalization_context.controller_pointer_exact =
            live.controller == profile_controller
            && profile_controller == setup_controller;
        normalization_context.controller_live_identity_exact =
            live.controller_identity.live.internal_index
                == profile_controller_identity.live.internal_index
            && live.controller_identity.live.serial_number
                == profile_controller_identity.live.serial_number;
        AudioPatchOriginalRebasePostRestoreSetupFacts post_restore_setup;
        post_restore_setup.selection_song_exact = setup_selection.song == song;
        post_restore_setup.selection_profile_valid = setup_selection.profile != nullptr;
        post_restore_setup.desired_song_exact = live.desired_song_id == song->id;
        post_restore_setup.patched_song_cleared = live.patched_song_id.empty();
        post_restore_setup.transient_sound_cleared = live.sound == nullptr;
        post_restore_setup.pending_patch_journal_empty =
            g_pending_play_setup_patch.patches.empty();
        post_restore_setup.unpublished_setup_cleared =
            !g_unpublished_audio_setup;
        normalization_context.setup_exact =
            audio_patch_original_rebase_post_restore_setup_exact(
                post_restore_setup);
        normalization_context.frozen_snapshot_valid = frozen.valid();
        owner_patch_normalization.route_exact =
            normalization_context.route_generation_exact;
        owner_patch_normalization.lease_exact = normalization_context.lease_exact;
        owner_patch_normalization.sound_exact =
            normalization_context.sound_pointer_exact
            && normalization_context.sound_live_identity_exact;
        owner_patch_normalization.controller_exact =
            normalization_context.controller_pointer_exact
            && normalization_context.controller_live_identity_exact;
        owner_patch_normalization.setup_exact = normalization_context.setup_exact;
        owner_patch_normalization.setup_selection_song_exact =
            post_restore_setup.selection_song_exact;
        owner_patch_normalization.setup_selection_profile_valid =
            post_restore_setup.selection_profile_valid;
        owner_patch_normalization.setup_desired_song_exact =
            post_restore_setup.desired_song_exact;
        owner_patch_normalization.setup_patched_song_cleared =
            post_restore_setup.patched_song_cleared;
        owner_patch_normalization.setup_transient_sound_cleared =
            post_restore_setup.transient_sound_cleared;
        owner_patch_normalization.setup_pending_patch_journal_empty =
            post_restore_setup.pending_patch_journal_empty;
        owner_patch_normalization.setup_unpublished_cleared =
            post_restore_setup.unpublished_setup_cleared;
        owner_patch_normalization.frozen_snapshot_valid =
            normalization_context.frozen_snapshot_valid;
        if (!owner_patch_normalization.route_exact) {
            owner_patch_normalization.first_failure =
                OwnerPatchMetadataNormalizationFailure::RouteDrift;
        } else if (!owner_patch_normalization.lease_exact) {
            owner_patch_normalization.first_failure =
                OwnerPatchMetadataNormalizationFailure::LeaseDrift;
        } else if (!owner_patch_normalization.sound_exact) {
            owner_patch_normalization.first_failure =
                OwnerPatchMetadataNormalizationFailure::SoundDrift;
        } else if (!owner_patch_normalization.controller_exact) {
            owner_patch_normalization.first_failure =
                OwnerPatchMetadataNormalizationFailure::ControllerDrift;
        } else if (!owner_patch_normalization.setup_exact) {
            owner_patch_normalization.first_failure =
                OwnerPatchMetadataNormalizationFailure::SetupDrift;
        } else if (!owner_patch_normalization.frozen_snapshot_valid) {
            owner_patch_normalization.first_failure =
                OwnerPatchMetadataNormalizationFailure::FrozenSnapshotInvalid;
        } else if (audio_patch_original_rebase_context_exact(
                       normalization_context)) {
            owner_patch_normalization.normalized =
                rebase_unique_audio_patch_original_after_exact_override(
                    frozen.patches, owner_restore,
                    owner_patch_normalization.matching_owner_patches);
            if (!owner_patch_normalization.normalized) {
                owner_patch_normalization.first_failure =
                    owner_patch_normalization.matching_owner_patches == 1
                    ? OwnerPatchMetadataNormalizationFailure::ExactOverrideRejected
                    : OwnerPatchMetadataNormalizationFailure::OwnerPatchNotUnique;
            }
        } else {
            owner_patch_normalization.first_failure =
                OwnerPatchMetadataNormalizationFailure::ExactOverrideRejected;
        }
        native_play_claimed = native_play_claimed
            && owner_patch_normalization.normalized;
    }
    OnMemoryBankRetainResult bank_retain{};
    const bool detached_retirement_admitted = native_play_claimed;
    OnMemoryBankRetirementGuardFacts retirement_guard;
    retirement_guard.detached_retirement_admitted = detached_retirement_admitted;
    retirement_guard.shared_retirement_admitted = shared_bank_play_qualified;
    retirement_guard.pending_patch_restored = restored;
    retirement_guard.owner_restore_applied = owner_restore.applied;
    retirement_guard.shared_owner_field_exact = shared_owner_field_exact;
    const bool owner_restore_proven = onmemory_bank_owner_restore_proven(
        owner_restore.applied, shared_owner_field_exact);
    if (onmemory_bank_retirement_guard_admits(retirement_guard)) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            bank_retain = g_onmemory_bank_lifecycle.retain_detached(
                {sound, qualified_sound_identity.live},
                custom_onmemory,
                armed_route.generation,
                route_generation,
                armed_route.lease_identity.generation,
                custom_request_handle,
                custom_backing,
                custom_backing_observed,
                true,
                owner_restore_proven,
                true,
                canonical_kind,
                custom_kind);
        }
        log_onmemory_retain_outcome(bank_retain);
        // Only a detached bank is owned by the mod, so only a detached bank
        // claims the native play.  The shared track leaves native_play_claimed
        // false, so every decision after this point -- including the playback
        // snapshot revocation below -- behaves exactly as it did before the
        // shared outcome existed.
        native_play_claimed = detached_retirement_admitted
            && bank_retain.detached();
        if (!detached_retirement_admitted && bank_retain.detached()) {
            // The canonical record was rebased between the admission read and
            // the retirement read.  A detached record now exists that the play
            // setup never claimed, so it will not be released on this route.
            core::log(core::LogLevel::Error,
                "[audio_sead] play_setup status=shared_retirement_diverged");
        }
    } else if (native_play_claimed) {
        native_play_claimed = false;
    }
    if (bank_snapshot.deferred_canonical_establishment && !native_play_claimed) {
        chart_diagnostic.substrate_bridge_phase =
            CanonicalSubstrateBridgePhase::Failed;
        chart_diagnostic.substrate_bridge_published = false;
    }
    bool playback_authority_absent = !registry_publication_succeeded;
    if (registry_publication_succeeded && !native_play_claimed) {
        const PlaybackSnapshot published_playback = registry().playback_snapshot();
        if (published_playback.song && published_playback.song->id == song->id) {
            playback_authority_absent = revoke_playback_snapshot(published_playback);
        }
    }

    uint64_t restored_owner = 0;
    const bool cleanup_owner_restored = cleanup_only_owner_restore && restored
        && owner_restore.applied
        && core::safe_read_field(sound,
            runtime_layouts::SqexSeadSound::observed_field548, restored_owner)
        && restored_owner == substrate_bridge.canonical_token;
    const CleanupOnlyPostOriginalOwnership cleanup_ownership =
        classify_cleanup_only_post_original_ownership({
            deferred_bridge_original_entered,
            native_play_claimed,
            playback_authority_absent,
            bank_kind_qualified,
            bank_tokens_decoded,
            bank_tokens_decoded && canonical_kind == 2 && custom_kind == 2,
            cleanup_owner_restored,
        });
    if (cleanup_ownership == CleanupOnlyPostOriginalOwnership::CleanupOnly
        || cleanup_ownership == CleanupOnlyPostOriginalOwnership::FailedRetained) {
        OnMemoryBankCleanupOnlyRecord cleanup{};
        cleanup.phase = OnMemoryBankCleanupOnlyPhase::FailedRetained;
        cleanup.first_failure = OnMemoryBankCleanupOnlyFailure::SalvageAuthorityInvalid;
        cleanup.bridge_generation = substrate_bridge.generation;
        cleanup.sound = {sound, substrate_bridge.expected_callback_sound_identity};
        cleanup.raw_custom_token = custom_onmemory_token;
        cleanup.selection_generation = setup_selection.generation;
        cleanup.route_generation = patched_token.route_generation;
        cleanup.song_key = substrate_bridge.song_key;
        cleanup.list_exit_epoch = substrate_bridge.list_exit_epoch;
        cleanup.release_completion_epoch = substrate_bridge.release_completion_epoch;
        cleanup.revocation_epoch = substrate_bridge.revocation_epoch;
        cleanup.controller = substrate_bridge.controller;
        cleanup.slot = substrate_bridge.slot;
        cleanup.bgm = substrate_bridge.bgm;
        (void)decode_onmemory_bank_token(
            substrate_bridge.canonical_token, cleanup.canonical);
        if (bank_tokens_decoded) cleanup.custom = custom_onmemory;
        cleanup.owner_restore_verified = cleanup_owner_restored;
        const bool salvage_exact = cleanup_ownership
            == CleanupOnlyPostOriginalOwnership::CleanupOnly;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            ++g_onmemory_bank_cleanup_only_generation;
            if (g_onmemory_bank_cleanup_only_generation == 0) {
                ++g_onmemory_bank_cleanup_only_generation;
            }
            cleanup.generation = g_onmemory_bank_cleanup_only_generation;
            cleanup.cleanup_generation = cleanup.generation;
            if (salvage_exact) {
                cleanup.phase =
                    OnMemoryBankCleanupOnlyPhase::CleanupOnlyRestoreApplied;
                cleanup.first_failure = OnMemoryBankCleanupOnlyFailure::None;
                if (!g_onmemory_bank_cleanup_only.retain(cleanup)) {
                    cleanup.phase = OnMemoryBankCleanupOnlyPhase::FailedRetained;
                    cleanup.first_failure =
                        OnMemoryBankCleanupOnlyFailure::RetainConflict;
                    (void)g_onmemory_bank_cleanup_only.retain_failed(
                        cleanup, cleanup.first_failure);
                }
            } else {
                cleanup.first_failure = !playback_authority_absent
                    ? OnMemoryBankCleanupOnlyFailure::RouteOrSetupActive
                    : !bank_kind_qualified
                        ? OnMemoryBankCleanupOnlyFailure::SoundIdentityInvalid
                    : !bank_tokens_decoded
                        ? OnMemoryBankCleanupOnlyFailure::CustomTokenInvalid
                        : canonical_kind != 2
                            ? OnMemoryBankCleanupOnlyFailure::CanonicalKindInvalid
                            : custom_kind != 2
                                ? OnMemoryBankCleanupOnlyFailure::CustomKindInvalid
                                : OnMemoryBankCleanupOnlyFailure::OwnerRestoreFailed;
                (void)g_onmemory_bank_cleanup_only.retain_failed(
                    cleanup, cleanup.first_failure);
            }
            g_onmemory_bank_lifecycle.cancel_qualification(armed_route.generation);
            g_unpublished_audio_setup = {};
            g_audio_route_state.custom_resource_owned = false;
            g_audio_route_state.owned_request_handle = 0;
            g_audio_route_state.owned_slot = nullptr;
            g_audio_route_state.owned_bgm = nullptr;
            g_audio_route_state.owned_sound = nullptr;
            g_audio_route_state.owned_sound_identity = {};
            g_audio_route_disabled.store(true, std::memory_order_release);
        }
        native_play_claimed = false;
        std::ostringstream cleanup_log;
        cleanup_log << "[audio_sead] cleanup_only_salvage generation="
            << cleanup.generation << " bridge_generation="
            << cleanup.bridge_generation << " exact=" << (salvage_exact ? 1 : 0)
            << " restored=" << (cleanup.owner_restore_verified ? 1 : 0)
            << " raw_custom=0x" << std::hex << cleanup.raw_custom_token
            << " canonical=0x" << cleanup.canonical.encode()
            << " custom=0x" << cleanup.custom.encode() << std::dec
            << " ownership=" << static_cast<unsigned>(cleanup_ownership)
            << " failure=" << static_cast<unsigned>(cleanup.first_failure);
        core::log(salvage_exact ? core::LogLevel::Info : core::LogLevel::Error,
            cleanup_log.str());
    }
    FrozenSoundPatchSnapshot restored_snapshot;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        restored_snapshot = g_audio_route_state.frozen_sound_patch;
    }
    if (native_play_claimed && restored && owner_restore.applied) {
        OnMemoryBankDiagnosticPair candidate{
            restored_snapshot.route_generation,
            restored_snapshot.lease_identity.generation,
            canonical_onmemory,
            custom_onmemory,
        };
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.frozen_sound_patch.route_generation
                    == candidate.route_generation
                && g_audio_route_state.lease_identity.generation
                    == candidate.cleanup_generation) {
                g_onmemory_bank_diagnostic_pair.retain(candidate);
                post_playsetup_diagnostic = candidate;
            }
        }
        if (post_playsetup_diagnostic) {
            deferred_onmemory_diagnostic.make_eligible();
        }
    }
    log_frozen_sound_patch_snapshot("after_play_setup_restore", restored_snapshot);
    if (!restored) {
        core::log(core::LogLevel::Error, "[audio_sead] play_setup status=blocked_return_restore_failed");
    }
    if (!native_play_claimed) {
        chart_diagnostic.decision =
            ChartAudioPlaySetupDecision::RoutedPublicationFailed;
        if (publicly_exposed) (void)revoke_playback_snapshot(registry().playback_snapshot());
        else clear_unpublished_audio_setup(
            armed_route.lease_identity,
            AudioRouteTransitionReason::PlaySetupRejected);
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::NativeClearUnverified,
                g_audio_route_state.lease_identity);
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
        core::log(core::LogLevel::Error, "[audio_sead] play_setup status=no_routed_play_after_original route=disabled");
    } else {
        chart_diagnostic.decision =
            ChartAudioPlaySetupDecision::RoutedPublicationSucceeded;
    }
    chart_diagnostic.routed_play_claimed = native_play_claimed;
    chart_diagnostic.publication_succeeded = native_play_claimed;
}

void log_private_controller_setup(
    std::string_view stage, const CustomContextToken& token, void* sound,
    uint64_t request_handle, uint8_t state, std::string_view failure_predicate = {})
{
    std::ostringstream out;
    out << "[audio_sead] private_controller_setup stage=" << stage
        << " registry_generation=" << token.registry_generation
        << " route_generation=" << token.route_generation
        << " lease_generation=" << token.lease_generation
        << " song_key=" << token.song_key
        << " sound=0x" << std::hex << reinterpret_cast<uintptr_t>(sound)
        << " request=0x" << request_handle
        << std::dec << " state=" << static_cast<unsigned>(state);
    if (!failure_predicate.empty()) {
        out << " failure_predicate=" << failure_predicate;
    }
    core::log(failure_predicate.empty() ? core::LogLevel::Info : core::LogLevel::Error, out.str());
}

const char* diagnostic_fact_name(const DiagnosticFact fact) noexcept
{
    switch (fact) {
    case DiagnosticFact::NotEvaluated: return "not_evaluated";
    case DiagnosticFact::Rejected: return "false";
    case DiagnosticFact::Accepted: return "true";
    }
    return "unknown";
}

const char* private_controller_stop_observation_name(
    const PrivateControllerStopObservationResult result) noexcept
{
    switch (result) {
    case PrivateControllerStopObservationResult::NotEvaluated: return "not_evaluated";
    case PrivateControllerStopObservationResult::ContextInvalid: return "context_invalid";
    case PrivateControllerStopObservationResult::TokenChanged: return "token_changed";
    case PrivateControllerStopObservationResult::StageChanged: return "stage_changed";
    case PrivateControllerStopObservationResult::RequiredContextMissing:
        return "required_context_missing";
    case PrivateControllerStopObservationResult::ControllerHandleInvalid:
        return "controller_handle_invalid";
    case PrivateControllerStopObservationResult::SoundHandleInvalid:
        return "sound_handle_invalid";
    case PrivateControllerStopObservationResult::Rejected: return "rejected";
    case PrivateControllerStopObservationResult::Accepted: return "accepted";
    }
    return "unknown";
}

const char* private_controller_stop_diagnostic_name(
    const PrivateControllerStopDiagnostic result) noexcept
{
    switch (result) {
    case PrivateControllerStopDiagnostic::Accepted: return "accepted";
    case PrivateControllerStopDiagnostic::ControllerIdentityPrefilterRejected:
        return "controller_identity_prefilter_rejected";
    case PrivateControllerStopDiagnostic::ControllerIdentityChainIncomplete:
        return "controller_identity_chain_incomplete";
    case PrivateControllerStopDiagnostic::SelectionGuardRejected:
        return "selection_guard_rejected";
    case PrivateControllerStopDiagnostic::RouteGenerationChanged:
        return "route_generation_changed";
    case PrivateControllerStopDiagnostic::LeaseChanged: return "lease_changed";
    case PrivateControllerStopDiagnostic::PhaseNotArmed: return "phase_not_armed";
    case PrivateControllerStopDiagnostic::ControllerIdentityChanged:
        return "controller_identity_changed";
    case PrivateControllerStopDiagnostic::ObserveContextInvalid:
        return "observe_context_invalid";
    case PrivateControllerStopDiagnostic::ObserveTokenChanged:
        return "observe_token_changed";
    case PrivateControllerStopDiagnostic::ObserveStageChanged:
        return "observe_stage_changed";
    case PrivateControllerStopDiagnostic::ObserveRequiredContextMissing:
        return "observe_required_context_missing";
    case PrivateControllerStopDiagnostic::ObserveControllerHandleInvalid:
        return "observe_controller_handle_invalid";
    case PrivateControllerStopDiagnostic::ObserveSoundHandleInvalid:
        return "observe_sound_handle_invalid";
    case PrivateControllerStopDiagnostic::ObserveRejected: return "observe_rejected";
    }
    return "unknown";
}

const char* controller_identity_proof_mode_name(
    const ControllerIdentityProofMode mode) noexcept
{
    switch (mode) {
    case ControllerIdentityProofMode::Invalid: return "invalid";
    case ControllerIdentityProofMode::SerialBacked: return "serial_backed";
    case ControllerIdentityProofMode::ItemBackedZeroSerial:
        return "item_backed_zero_serial";
    case ControllerIdentityProofMode::Structural: return "structural";
    }
    return "unknown";
}

const char* controller_identity_raw_index_state_name(
    const ControllerIdentityRawIndexState state) noexcept
{
    switch (state) {
    case ControllerIdentityRawIndexState::Unreadable: return "unreadable";
    case ControllerIdentityRawIndexState::Negative: return "negative";
    case ControllerIdentityRawIndexState::Nonnegative: return "nonnegative";
    }
    return "unknown";
}

const char* controller_identity_proof_mismatch_name(
    const ControllerIdentityProofMismatch mismatch) noexcept
{
    switch (mismatch) {
    case ControllerIdentityProofMismatch::None: return "none";
    case ControllerIdentityProofMismatch::ExpectedInvalid: return "expected_invalid";
    case ControllerIdentityProofMismatch::CurrentInvalid: return "current_invalid";
    case ControllerIdentityProofMismatch::Mode: return "mode";
    case ControllerIdentityProofMismatch::ObjectPointer: return "object_pointer";
    case ControllerIdentityProofMismatch::ObjectClass: return "class";
    case ControllerIdentityProofMismatch::NameComparisonIndex:
        return "name_comparison_index";
    case ControllerIdentityProofMismatch::NameNumber: return "name_number";
    case ControllerIdentityProofMismatch::Outer: return "outer";
    case ControllerIdentityProofMismatch::RawIndex: return "raw_index";
    case ControllerIdentityProofMismatch::LiveIndex: return "live_index";
    case ControllerIdentityProofMismatch::ItemIndex: return "item_index";
    case ControllerIdentityProofMismatch::Serial: return "serial";
    }
    return "unknown";
}

const char* uobject_identity_prefilter_result_name(
    const UObjectIdentityPrefilterResult result) noexcept
{
    switch (result) {
    case UObjectIdentityPrefilterResult::NotEvaluated: return "not_evaluated";
    case UObjectIdentityPrefilterResult::Passed: return "passed";
    case UObjectIdentityPrefilterResult::ExpectedLiveHandleValidationFailed:
        return "expected_live_handle_validation_failed";
    case UObjectIdentityPrefilterResult::CurrentIdentityReadFailed:
        return "current_identity_read_failed";
    case UObjectIdentityPrefilterResult::CurrentLiveHandleChanged:
        return "current_live_handle_changed";
    case UObjectIdentityPrefilterResult::ObjectClassChanged: return "class_changed";
    case UObjectIdentityPrefilterResult::NameComparisonIndexChanged:
        return "name_comparison_index_changed";
    case UObjectIdentityPrefilterResult::NameNumberChanged:
        return "name_number_changed";
    case UObjectIdentityPrefilterResult::OuterChanged: return "outer_changed";
    }
    return "unknown";
}

struct PrivateControllerStopDiagnosticRecord {
    bool pending = false;
    PrivateControllerStopDiagnosticEvidence evidence;
    uint64_t registry_generation = 0;
    uint64_t route_generation = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
    uintptr_t controller = 0;
    uintptr_t sound = 0;
    bool arm_proof_attempted = false;
    bool arm_bind_succeeded = false;
    ControllerIdentityProof arm_attempted_proof{};
    bool controller_proof_report_available = false;
    ControllerIdentityProofMismatchReport controller_proof_report{};
};

struct BgmPlaybackNativeStopReturnDiagnostic {
    bool proposed = false;
    uintptr_t controller = 0;
    BgmPlaybackNativeStopTarget target = BgmPlaybackNativeStopTarget::None;
    BgmPlaybackNativeStopCallResult result =
        BgmPlaybackNativeStopCallResult::NotCalled;
    uint8_t call_count = 0;
    bool post_observation_ran = false;
    bool post_observation_succeeded = false;
    bool original_forwarded = false;
};

struct ArmedStopControllerProofDiagnostic {
    bool valid = false;
    ControllerIdentityProofMode mode = ControllerIdentityProofMode::Invalid;
    bool raw_index_readable = false;
    int32_t raw_index = -1;
    bool live_capture_succeeded = false;
    int32_t live_index = -1;
    int32_t live_serial = 0;
    bool item_capture_succeeded = false;
    int32_t item_index = -1;
    int32_t item_serial = 0;
};

struct ArmedStopReadinessDiagnostic {
    bool proposed = false;
    bool natural_completion_evaluated = false;
    AudioNaturalCompletionRetirementFailure natural_completion_failure =
        AudioNaturalCompletionRetirementFailure::None;
    bool natural_completion_route_predecessor = false;
    bool natural_completion_detached_route_predecessor = false;
    bool natural_completion_frozen_route_predecessor = false;
    bool natural_completion_presence_pending = false;
    bool natural_completion_monitor_started = false;
    bool natural_completion_commit_attempted = false;
    bool natural_completion_committed = false;
    bool playback_absent = false;
    bool cleanup_absent = false;
    bool route_unowned = false;
    bool retirement_absent = false;
    bool selection_matches = false;
    bool route_matches = false;
    bool stage_awaiting_stop = false;
    bool private_stop_candidate = false;
    uintptr_t controller = 0;
    uintptr_t slot = 0;
    uintptr_t bgm = 0;
    uintptr_t sound = 0;
    uint64_t request = 0;
    uint8_t state = 0xff;
    bool chain_read = false;
    bool sound_read = false;
    bool request_read = false;
    bool state_read = false;
    bool idle_null_exact = false;
    bool active_sound_exact = false;
    bool arm_proof_attempted = false;
    bool arm_bind_succeeded = false;
    ArmedStopControllerProofDiagnostic attempted_arm_proof{};
    ArmedStopControllerProofDiagnostic bound_arm_proof{};
    bool current_controller_identity_attempted = false;
    bool current_controller_identity_read = false;
    ArmedStopControllerProofDiagnostic current_controller_proof{};
    bool current_controller_proof_exact = false;
    ControllerIdentityProofMismatch current_controller_proof_mismatch =
        ControllerIdentityProofMismatch::None;
    uint64_t selection_generation = 0;
    uint64_t route_generation = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
    uint64_t desired_song_key = 0;
    uint64_t patched_song_key = 0;
    bool desired_song_present = false;
    bool patched_song_present = false;
    PrivateControllerSetupStage setup_stage =
        PrivateControllerSetupStage::AwaitingStop;
    bool frozen_profile_active = false;
    uint64_t frozen_profile_generation = 0;
    uint64_t frozen_profile_song_key = 0;
    bool frozen_profile_native_arm_attempted = false;
    bool lifecycle_available = false;
    uint64_t lifecycle_state_epoch = 0;
    uint64_t lifecycle_ordinal = 0;
    uint64_t lifecycle_canonical_token = 0;
    uint64_t lifecycle_custom_token = 0;
    bool chart_transaction_associated = false;
    bool chart_transaction_associated_by_tls = false;
    uint64_t chart_transaction_generation = 0;
    ChartAudioExpandTlsSnapshot chart_expand_tls{};
    bool private_setup_staged = false;
    bool private_setup_failed = false;
    bool original_forwarded = false;
    bool postcondition_evaluated = false;
    bool postcondition_exact = false;
    bool postcondition_failed = false;
    AudioRoutePhase post_route_phase = AudioRoutePhase::Idle;
    uint64_t post_route_generation = 0;
    bool post_setup_valid = false;
    PrivateControllerSetupStage post_setup_stage =
        PrivateControllerSetupStage::AwaitingStop;
    uint64_t post_setup_registry_generation = 0;
    uint64_t post_setup_route_generation = 0;
    uint64_t post_setup_lease_generation = 0;
    uint64_t post_setup_song_key = 0;
};

static_assert(std::is_trivially_copyable_v<ArmedStopControllerProofDiagnostic>);
static_assert(std::is_trivially_copyable_v<ArmedStopReadinessDiagnostic>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativeStopReturnDiagnostic>);

const char* bgm_playback_native_stop_target_name(
    const BgmPlaybackNativeStopTarget target) noexcept
{
    switch (target) {
    case BgmPlaybackNativeStopTarget::None: return "none";
    case BgmPlaybackNativeStopTarget::Trampoline: return "trampoline";
    }
    return "unknown";
}

const char* bgm_playback_native_stop_result_name(
    const BgmPlaybackNativeStopCallResult result) noexcept
{
    switch (result) {
    case BgmPlaybackNativeStopCallResult::NotCalled: return "not_called";
    case BgmPlaybackNativeStopCallResult::CalledReturned:
        return "called_returned";
    }
    return "unknown";
}

const char* audio_natural_completion_retirement_failure_name(
    const AudioNaturalCompletionRetirementFailure failure) noexcept
{
    switch (failure) {
#define FF7RP_NATURAL_COMPLETION_FAILURE_NAME(value, text) \
    case AudioNaturalCompletionRetirementFailure::value: return text
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(None, "none");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(RouteNotCustomOwned, "route_not_custom_owned");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(RouteLeaseInvalid, "route_lease_invalid");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(RouteControllerMismatch, "route_controller_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(PreStopChainUnreadable, "pre_stop_chain_unreadable");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(PreStopSoundNotNull, "pre_stop_sound_not_null");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(PreStopRequestNotZero, "pre_stop_request_not_zero");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(PreStopStateNotIdle, "pre_stop_state_not_idle");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedMissing, "detached_missing");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedPhaseNotRestoreApplied, "detached_phase_not_restore_applied");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedRouteNotPredecessor, "detached_route_not_predecessor");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedCleanupGenerationMismatch, "detached_cleanup_generation_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedSoundMismatch, "detached_sound_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedRequestUnavailable, "detached_request_unavailable");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedRequestMismatch, "detached_request_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedTokensInvalid, "detached_tokens_invalid");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedOwnerNotRestored, "detached_owner_not_restored");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(FrozenPatchInvalid, "frozen_patch_invalid");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(FrozenRouteNotPredecessor, "frozen_route_not_predecessor");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(FrozenLeaseMismatch, "frozen_lease_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(FrozenSoundMismatch, "frozen_sound_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(FrozenOwnerPatchMismatch, "frozen_owner_patch_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(JournalsNotRestored, "journals_not_restored");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(PauseResumeConflict, "pause_resume_conflict");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(SetupConflict, "setup_conflict");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(RegistryPlaybackMismatch, "registry_playback_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(RegistryCleanupConflict, "registry_cleanup_conflict");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(AggregateSnapshotUnavailable, "aggregate_snapshot_unavailable");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(ActiveAggregateBorrower, "active_aggregate_borrower");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(CanonicalProofSnapshotUnavailable, "canonical_proof_snapshot_unavailable");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(CanonicalProofActive, "canonical_proof_active");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(ExistingRetirement, "existing_retirement");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(InvalidObservation, "invalid_observation");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(MonitorStartFailed, "monitor_start_failed");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(RouteCommitMismatch, "route_commit_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(DetachedCommitMismatch, "detached_commit_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(FrozenCommitMismatch, "frozen_commit_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(OwnershipCommitMismatch, "ownership_commit_mismatch");
    FF7RP_NATURAL_COMPLETION_FAILURE_NAME(RegistryCommitMismatch, "registry_commit_mismatch");
#undef FF7RP_NATURAL_COMPLETION_FAILURE_NAME
    }
    return "unknown";
}

void log_bgm_playback_native_stop_return(
    const BgmPlaybackNativeStopReturnDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed) return;
    static std::atomic_uint32_t budget{0};
    if (budget.fetch_add(1, std::memory_order_relaxed) >= 64) return;
    (void)bgm_playback_observe_best_effort(true, [&] {
        std::ostringstream out;
        out << "[audio_sead] native_stop_return"
            << " target="
            << bgm_playback_native_stop_target_name(diagnostic.target)
            << " result="
            << bgm_playback_native_stop_result_name(diagnostic.result)
            << " call_count=" << static_cast<unsigned>(diagnostic.call_count)
            << " post_observation_ran=" << diagnostic.post_observation_ran
            << " post_observation_succeeded="
            << diagnostic.post_observation_succeeded
            << " original_forwarded=" << diagnostic.original_forwarded
            << " controller=0x" << std::hex << diagnostic.controller;
        core::log(core::LogLevel::Info, out.str());
    });
}

ArmedStopControllerProofDiagnostic armed_stop_controller_proof_diagnostic(
    const ControllerIdentityProof& proof) noexcept
{
    ArmedStopControllerProofDiagnostic diagnostic;
    diagnostic.valid = controller_identity_proof_valid(proof);
    diagnostic.mode = proof.mode;
    diagnostic.raw_index_readable = proof.raw_internal_index_readable;
    diagnostic.raw_index = proof.raw_internal_index;
    diagnostic.live_capture_succeeded = proof.live_capture_succeeded;
    diagnostic.live_index = proof.live.internal_index;
    diagnostic.live_serial = proof.live.serial_number;
    diagnostic.item_capture_succeeded =
        proof.item_backed_zero_serial_capture_succeeded;
    diagnostic.item_index = proof.item_backed_zero_serial.internal_index;
    diagnostic.item_serial = proof.item_backed_zero_serial.serial_number;
    return diagnostic;
}

void log_armed_stop_readiness_diagnostic(
    const ArmedStopReadinessDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed) return;
    static std::atomic_uint32_t s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 64) return;
    bgm_playback_observe_best_effort(true, [&]() {
        const auto append_proof = [](std::ostringstream& out,
                                      const char* prefix,
                                      const ArmedStopControllerProofDiagnostic& proof) {
            out << ' ' << prefix << "_valid=" << proof.valid
                << ' ' << prefix << "_mode="
                << controller_identity_proof_mode_name(proof.mode)
                << ' ' << prefix << "_raw_index_readable="
                << proof.raw_index_readable
                << ' ' << prefix << "_raw_index=" << proof.raw_index
                << ' ' << prefix << "_live_capture_succeeded="
                << proof.live_capture_succeeded
                << ' ' << prefix << "_live_index=" << proof.live_index
                << ' ' << prefix << "_live_serial=" << proof.live_serial
                << ' ' << prefix << "_item_capture_succeeded="
                << proof.item_capture_succeeded
                << ' ' << prefix << "_item_index=" << proof.item_index
                << ' ' << prefix << "_item_serial=" << proof.item_serial;
        };
        std::ostringstream out;
        out << "[audio_sead] armed_stop_readiness"
            << " natural_completion_evaluated="
            << diagnostic.natural_completion_evaluated
            << " natural_completion_failure="
            << audio_natural_completion_retirement_failure_name(
                diagnostic.natural_completion_failure)
            << " natural_completion_route_predecessor="
            << diagnostic.natural_completion_route_predecessor
            << " natural_completion_detached_route_predecessor="
            << diagnostic.natural_completion_detached_route_predecessor
            << " natural_completion_frozen_route_predecessor="
            << diagnostic.natural_completion_frozen_route_predecessor
            << " natural_completion_presence_pending="
            << diagnostic.natural_completion_presence_pending
            << " natural_completion_monitor_started="
            << diagnostic.natural_completion_monitor_started
            << " natural_completion_commit_attempted="
            << diagnostic.natural_completion_commit_attempted
            << " natural_completion_committed="
            << diagnostic.natural_completion_committed
            << " playback_absent=" << diagnostic.playback_absent
            << " cleanup_absent=" << diagnostic.cleanup_absent
            << " route_unowned=" << diagnostic.route_unowned
            << " retirement_absent=" << diagnostic.retirement_absent
            << " selection_matches=" << diagnostic.selection_matches
            << " route_matches=" << diagnostic.route_matches
            << " stage_awaiting_stop=" << diagnostic.stage_awaiting_stop
            << " private_stop_candidate=" << diagnostic.private_stop_candidate
            << " controller=0x" << std::hex << diagnostic.controller
            << " slot=0x" << diagnostic.slot
            << " bgm=0x" << diagnostic.bgm
            << " sound=0x" << diagnostic.sound
            << " request=0x" << diagnostic.request << std::dec
            << " state=" << static_cast<unsigned>(diagnostic.state)
            << " chain_read=" << diagnostic.chain_read
            << " sound_read=" << diagnostic.sound_read
            << " request_read=" << diagnostic.request_read
            << " state_read=" << diagnostic.state_read
            << " idle_null_exact=" << diagnostic.idle_null_exact
            << " active_sound_exact=" << diagnostic.active_sound_exact
            << " arm_proof_attempted=" << diagnostic.arm_proof_attempted
            << " arm_bind_succeeded=" << diagnostic.arm_bind_succeeded
            << " current_controller_identity_attempted="
            << diagnostic.current_controller_identity_attempted
            << " current_controller_identity_read="
            << diagnostic.current_controller_identity_read
            << " current_controller_proof_exact="
            << diagnostic.current_controller_proof_exact
            << " current_controller_proof_mismatch="
            << controller_identity_proof_mismatch_name(
                diagnostic.current_controller_proof_mismatch)
            << " selection_generation=" << diagnostic.selection_generation
            << " route_generation=" << diagnostic.route_generation
            << " lease_generation=" << diagnostic.lease_generation
            << " song_key=" << diagnostic.song_key
            << " desired_song_present=" << diagnostic.desired_song_present
            << " desired_song_key=" << diagnostic.desired_song_key
            << " patched_song_present=" << diagnostic.patched_song_present
            << " patched_song_key=" << diagnostic.patched_song_key
            << " setup_stage=" << static_cast<unsigned>(diagnostic.setup_stage)
            << " frozen_profile_active=" << diagnostic.frozen_profile_active
            << " frozen_profile_generation="
            << diagnostic.frozen_profile_generation
            << " frozen_profile_song_key=" << diagnostic.frozen_profile_song_key
            << " frozen_profile_native_arm_attempted="
            << diagnostic.frozen_profile_native_arm_attempted
            << " lifecycle_available=" << diagnostic.lifecycle_available
            << " lifecycle_state_epoch=" << diagnostic.lifecycle_state_epoch
            << " lifecycle_ordinal=" << diagnostic.lifecycle_ordinal
            << " lifecycle_canonical_token=0x" << std::hex
            << diagnostic.lifecycle_canonical_token
            << " lifecycle_custom_token=0x"
            << diagnostic.lifecycle_custom_token << std::dec
            << " chart_transaction_associated="
            << diagnostic.chart_transaction_associated
            << " chart_transaction_associated_by_tls="
            << diagnostic.chart_transaction_associated_by_tls
            << " chart_transaction_generation="
            << diagnostic.chart_transaction_generation
            << " chart_expand_depth=" << diagnostic.chart_expand_tls.depth
            << " chart_expand_enter_ordinal="
            << diagnostic.chart_expand_tls.enter_ordinal
            << " chart_expand_exit_ordinal="
            << diagnostic.chart_expand_tls.exit_ordinal
            << " chart_expand_original_inflight="
            << diagnostic.chart_expand_tls.original_inflight
            << " expand_enter_time_count_read="
            << diagnostic.chart_expand_tls.enter_time.count_read
            << " expand_enter_time_count="
            << diagnostic.chart_expand_tls.enter_time.count
            << " expand_enter_event_count_read="
            << diagnostic.chart_expand_tls.enter_event.count_read
            << " expand_enter_event_count="
            << diagnostic.chart_expand_tls.enter_event.count
            << " expand_exit_time_count_read="
            << diagnostic.chart_expand_tls.exit_time.count_read
            << " expand_exit_time_count="
            << diagnostic.chart_expand_tls.exit_time.count
            << " expand_exit_event_count_read="
            << diagnostic.chart_expand_tls.exit_event.count_read
            << " expand_exit_event_count="
            << diagnostic.chart_expand_tls.exit_event.count
            << " private_setup_staged=" << diagnostic.private_setup_staged
            << " private_setup_failed=" << diagnostic.private_setup_failed
            << " original_forwarded=" << diagnostic.original_forwarded
            << " postcondition_evaluated=" << diagnostic.postcondition_evaluated
            << " postcondition_exact=" << diagnostic.postcondition_exact
            << " postcondition_failed=" << diagnostic.postcondition_failed
            << " post_route_phase=" << audio_route_phase_name(diagnostic.post_route_phase)
            << " post_route_generation=" << diagnostic.post_route_generation
            << " post_setup_valid=" << diagnostic.post_setup_valid
            << " post_setup_stage=" << static_cast<unsigned>(diagnostic.post_setup_stage)
            << " post_setup_registry_generation="
            << diagnostic.post_setup_registry_generation
            << " post_setup_route_generation="
            << diagnostic.post_setup_route_generation
            << " post_setup_lease_generation="
            << diagnostic.post_setup_lease_generation
            << " post_setup_song_key=" << diagnostic.post_setup_song_key;
        append_proof(out, "attempted_arm", diagnostic.attempted_arm_proof);
        append_proof(out, "bound_arm", diagnostic.bound_arm_proof);
        append_proof(out, "current", diagnostic.current_controller_proof);
        core::log(core::LogLevel::Info, out.str());
    });
}

void log_private_controller_stop_diagnostic(
    const PrivateControllerStopDiagnosticRecord& record) noexcept
{
    if (!record.pending) return;
    static std::atomic_uint32_t s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 64) return;

    try {
        const PrivateControllerStopDiagnosticEvidence& evidence = record.evidence;
        const PrivateControllerStopDiagnostic result =
            classify_private_controller_stop_diagnostic(evidence);
        std::ostringstream out;
        out << "[audio_sead] private_controller_stop_diagnostic status=rejected"
            << " reason=" << private_controller_stop_diagnostic_name(result)
            << " selection_guard_acquired="
            << diagnostic_fact_name(evidence.selection_guard_acquired)
            << " route_generation_unchanged="
            << diagnostic_fact_name(evidence.route_generation_unchanged)
            << " lease_unchanged=" << diagnostic_fact_name(evidence.lease_unchanged)
            << " phase_armed=" << diagnostic_fact_name(evidence.phase_armed)
            << " controller_identity_unchanged="
            << diagnostic_fact_name(evidence.controller_identity_unchanged)
            << " observe_accepted="
            << (evidence.observe_result == PrivateControllerStopObservationResult::NotEvaluated
                    ? "not_evaluated"
                    : evidence.observe_result == PrivateControllerStopObservationResult::Accepted
                        ? "true" : "false")
            << " observe_reason="
            << private_controller_stop_observation_name(evidence.observe_result)
            << " observe_context_valid="
            << diagnostic_fact_name(evidence.observe_evidence.context_valid)
            << " observe_token_unchanged="
            << diagnostic_fact_name(evidence.observe_evidence.token_unchanged)
            << " observe_stage_awaiting_stop="
            << diagnostic_fact_name(evidence.observe_evidence.stage_awaiting_stop)
            << " observe_required_context_present="
            << diagnostic_fact_name(evidence.observe_evidence.required_context_present)
            << " observe_controller_handle_valid="
            << diagnostic_fact_name(evidence.observe_evidence.controller_handle_valid)
            << " observe_sound_handle_valid="
            << diagnostic_fact_name(evidence.observe_evidence.sound_handle_valid)
            << " registry_generation=" << record.registry_generation
            << " route_generation=" << record.route_generation
            << " lease_generation=" << record.lease_generation
            << " song_key=" << record.song_key
            << " controller=0x" << std::hex << record.controller
            << " sound=0x" << record.sound
            << std::dec
            << " arm_proof_attempted=" << (record.arm_proof_attempted ? 1 : 0)
            << " arm_bind_succeeded=" << (record.arm_bind_succeeded ? 1 : 0)
            << " controller_prefilter_passed="
            << (evidence.identity_observation.prefilter_result
                        == UObjectIdentityPrefilterResult::NotEvaluated
                    ? "not_evaluated"
                    : uobject_identity_prefilter_passed(
                          evidence.identity_observation.prefilter_result)
                        ? "true" : "false")
            << " controller_prefilter_result="
            << uobject_identity_prefilter_result_name(
                evidence.identity_observation.prefilter_result)
            << " observed_live_capture_reason="
            << (evidence.identity_observation.observed_live_capture_attempted
                    ? uobject_live_handle_capture_result_name(
                        evidence.identity_observation.observed_live_capture_result)
                    : "not_attempted");
        if (record.arm_proof_attempted) {
            const ControllerIdentityProofFacts arm_facts =
                controller_identity_proof_facts(record.arm_attempted_proof);
            out << " arm_proof_valid=" << (arm_facts.valid ? 1 : 0)
                << " arm_proof_mode="
                << controller_identity_proof_mode_name(arm_facts.mode)
                << " arm_raw_index_readable="
                << (arm_facts.raw_internal_index_readable ? 1 : 0)
                << " arm_raw_index_state="
                << controller_identity_raw_index_state_name(
                    arm_facts.raw_internal_index_state)
                << " arm_raw_index=" << arm_facts.raw_internal_index
                << " arm_live_capture_succeeded="
                << (arm_facts.live_capture_succeeded ? 1 : 0)
                << " arm_live_index=" << arm_facts.live_internal_index
                << " arm_live_index_valid="
                << (arm_facts.live_internal_index_valid ? 1 : 0)
                << " arm_live_serial_valid="
                << (arm_facts.live_serial_valid ? 1 : 0);
        }
        if (result == PrivateControllerStopDiagnostic::ControllerIdentityChanged
            && record.controller_proof_report_available) {
            const auto append_proof_facts = [&out](
                const char* prefix, const ControllerIdentityProofFacts& facts) {
                out << std::dec
                    << ' ' << prefix << "_valid=" << (facts.valid ? 1 : 0)
                    << ' ' << prefix << "_mode="
                    << controller_identity_proof_mode_name(facts.mode)
                    << ' ' << prefix << "_raw_index_readable="
                    << (facts.raw_internal_index_readable ? 1 : 0)
                    << ' ' << prefix << "_raw_index_state="
                    << controller_identity_raw_index_state_name(
                        facts.raw_internal_index_state)
                    << ' ' << prefix << "_raw_index=" << facts.raw_internal_index
                    << ' ' << prefix << "_live_capture_succeeded="
                    << (facts.live_capture_succeeded ? 1 : 0)
                    << ' ' << prefix << "_live_index=" << facts.live_internal_index
                    << ' ' << prefix << "_live_index_valid="
                    << (facts.live_internal_index_valid ? 1 : 0)
                    << ' ' << prefix << "_live_serial_valid="
                    << (facts.live_serial_valid ? 1 : 0);
            };
            out << std::dec << " controller_proof_mismatch="
                << controller_identity_proof_mismatch_name(
                    record.controller_proof_report.first_mismatch);
            append_proof_facts(
                "proof_expected", record.controller_proof_report.expected);
            append_proof_facts(
                "proof_current", record.controller_proof_report.current);
        }
        core::log(core::LogLevel::Error, out.str());
    } catch (...) {
    }
}

enum class PrivateControllerSetupResult {
    NotEligible,
    Forwarded,
    Claimed,
    Failed,
};

const char* private_controller_setup_result_name(
    PrivateControllerSetupResult result) noexcept;

enum class PrivateControllerSetReadinessBlocker {
    None,
    PlaybackPresent,
    CleanupPresent,
    ControllerMissing,
    RequestedSoundMissing,
    ChainUnreadable,
    ControllerIdentityUnreadable,
    RequestedSoundIdentityUnreadable,
    ControllerProofInvalid,
    RouteOwnedOrCleanupPending,
    RetirementActive,
    SelectionMismatch,
    RouteTokenMismatch,
    RoutePhaseMismatch,
    RouteControllerMismatch,
    SetupControllerMismatch,
    SetupSlotMismatch,
    SetupBgmMismatch,
    ExpectedSoundPointerMismatch,
    ExpectedSoundHandleMismatch,
    SetupStageMismatch,
    SelectionGuardMismatch,
    RouteControllerIdentityMismatch,
    SetupSoundIdentityMismatch,
    SetCaptureRejected,
};

struct PrivateControllerSetReadinessDiagnostic {
    bool proposed = false;
    PrivateControllerSetReadinessBlocker first_failed =
        PrivateControllerSetReadinessBlocker::None;
    PrivateControllerSetupResult result = PrivateControllerSetupResult::NotEligible;
    bool original_forwarded = false;
    uint32_t original_forward_count = 0;
    bool playback_absent = false;
    bool cleanup_absent = false;
    bool controller_present = false;
    bool requested_sound_present = false;
    bool chain_read = false;
    uintptr_t controller = 0;
    uintptr_t live_slot = 0;
    uintptr_t live_bgm = 0;
    uintptr_t live_sound = 0;
    uint64_t live_request = 0;
    uint8_t live_state = 0xff;
    bool controller_identity_read = false;
    bool requested_sound_identity_read = false;
    int32_t requested_sound_index = -1;
    int32_t requested_sound_serial = 0;
    ArmedStopControllerProofDiagnostic route_controller_proof{};
    ArmedStopControllerProofDiagnostic setup_controller_proof{};
    ArmedStopControllerProofDiagnostic current_controller_proof{};
    bool route_current_controller_proof_exact = false;
    bool setup_current_controller_proof_exact = false;
    uint64_t selection_generation = 0;
    bool selection_matches = false;
    bool selection_guard_exact = false;
    AudioRoutePhase route_phase = AudioRoutePhase::Idle;
    uint64_t route_generation = 0;
    uint64_t lease_generation = 0;
    uint64_t song_key = 0;
    uintptr_t route_controller = 0;
    bool route_unowned = false;
    bool retirement_absent = false;
    bool route_token_exact = false;
    bool route_phase_exact = false;
    bool route_controller_exact = false;
    bool route_controller_identity_exact = false;
    PrivateControllerSetupStage setup_stage =
        PrivateControllerSetupStage::AwaitingStop;
    uint64_t setup_token_route_generation = 0;
    uint64_t setup_token_lease_generation = 0;
    uint64_t setup_token_song_key = 0;
    uintptr_t setup_controller = 0;
    uintptr_t setup_slot = 0;
    uintptr_t setup_bgm = 0;
    uintptr_t setup_expected_sound = 0;
    int32_t setup_expected_sound_index = -1;
    int32_t setup_expected_sound_serial = 0;
    bool setup_controller_exact = false;
    bool setup_slot_exact = false;
    bool setup_bgm_exact = false;
    bool expected_sound_pointer_exact = false;
    bool expected_sound_handle_exact = false;
    bool setup_sound_identity_exact = false;
    bool setup_stage_exact = false;
    bool eligibility_exact = false;
    bool capture_controller_set_succeeded = false;
    bool substrate_proof_available = false;
    uint64_t substrate_transaction_generation = 0;
};

static_assert(std::is_trivially_copyable_v<PrivateControllerSetReadinessDiagnostic>);

enum class PrivateControllerSetDispatchDecision : uint8_t {
    None,
    ReplayBypass,
    AggregateExit,
    AggregateActive,
    PauseResumeHandled,
    DeferredHandoffPending,
    RouteDisabled,
    PrivateHandled,
    PrivateNotEligible,
    FallbackDirect,
    FallbackDeferred,
};

struct PrivateControllerSetForwardDiagnostic {
    uintptr_t controller = 0;
    uintptr_t sound = 0;
};

struct PrivateControllerSetDispatchDiagnostic {
    bool proposed = false;
    PrivateControllerSetDispatchDecision decision =
        PrivateControllerSetDispatchDecision::None;
    uintptr_t controller = 0;
    uintptr_t requested_sound = 0;
    uint32_t replay_depth = 0;
    bool entry_route_enabled = false;
    AudioRoutePhase entry_route_phase = AudioRoutePhase::Idle;
    uint64_t entry_route_generation = 0;
    uint64_t entry_lease_generation = 0;
    uint64_t entry_song_key = 0;
    bool entry_route_owned = false;
    bool entry_cleanup_pending = false;
    bool entry_setup_valid = false;
    PrivateControllerSetupStage entry_setup_stage =
        PrivateControllerSetupStage::AwaitingStop;
    uint64_t entry_setup_registry_generation = 0;
    uint64_t entry_setup_route_generation = 0;
    uint64_t entry_setup_lease_generation = 0;
    uint64_t entry_setup_song_key = 0;
    uintptr_t entry_setup_controller = 0;
    uintptr_t entry_setup_slot = 0;
    uintptr_t entry_setup_bgm = 0;
    uintptr_t entry_setup_expected_sound = 0;
    int32_t entry_setup_expected_sound_index = -1;
    int32_t entry_setup_expected_sound_serial = 0;
    bool bridge_set_candidate = false;
    uint64_t bridge_generation = 0;
    CanonicalSubstrateBridgeSetFailure bridge_first_failure =
        CanonicalSubstrateBridgeSetFailure::None;
    bool bridge_identity_read_attempted = false;
    bool bridge_identity_read_succeeded = false;
    int32_t bridge_identity_index = -1;
    int32_t bridge_identity_serial = 0;
    bool bridge_identity_exact = false;
    CanonicalSubstrateBridgePhase bridge_result_phase =
        CanonicalSubstrateBridgePhase::None;
    bool aggregate_observation_created = false;
    bool aggregate_exit_evaluated = false;
    bool aggregate_exit_matched = false;
    bool aggregate_active_evaluated = false;
    bool aggregate_active_matched = false;
    bool aggregate_route_advanced = false;
    bool pause_resume_evaluated = false;
    bool pause_resume_handled = false;
    bool deferred_handoff_evaluated = false;
    bool deferred_handoff_pending = false;
    bool route_disabled_evaluated = false;
    bool route_disabled_matched = false;
    bool private_invoked = false;
    PrivateControllerSetupResult private_result =
        PrivateControllerSetupResult::NotEligible;
    uint32_t native_forward_count = 0;
    bool native_forward_overflow = false;
    std::array<PrivateControllerSetForwardDiagnostic, 2> native_forwards{};
};

static_assert(std::is_trivially_copyable_v<PrivateControllerSetForwardDiagnostic>);
static_assert(std::is_trivially_copyable_v<PrivateControllerSetDispatchDiagnostic>);

const char* private_controller_set_dispatch_decision_name(
    const PrivateControllerSetDispatchDecision decision) noexcept
{
    switch (decision) {
    case PrivateControllerSetDispatchDecision::None: return "none";
    case PrivateControllerSetDispatchDecision::ReplayBypass: return "replay_bypass";
    case PrivateControllerSetDispatchDecision::AggregateExit: return "aggregate_exit";
    case PrivateControllerSetDispatchDecision::AggregateActive: return "aggregate_active";
    case PrivateControllerSetDispatchDecision::PauseResumeHandled:
        return "pause_resume_handled";
    case PrivateControllerSetDispatchDecision::DeferredHandoffPending:
        return "deferred_handoff_pending";
    case PrivateControllerSetDispatchDecision::RouteDisabled: return "route_disabled";
    case PrivateControllerSetDispatchDecision::PrivateHandled: return "private_handled";
    case PrivateControllerSetDispatchDecision::PrivateNotEligible:
        return "private_not_eligible";
    case PrivateControllerSetDispatchDecision::FallbackDirect: return "fallback_direct";
    case PrivateControllerSetDispatchDecision::FallbackDeferred:
        return "fallback_deferred";
    }
    return "unknown";
}

void log_private_controller_set_dispatch_diagnostic(
    const PrivateControllerSetDispatchDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed) return;
    bgm_playback_observe_best_effort(true, [&]() {
        std::ostringstream out;
        out << "[audio_sead] private_controller_set_dispatch"
            << " decision="
            << private_controller_set_dispatch_decision_name(diagnostic.decision)
            << " controller=0x" << std::hex << diagnostic.controller
            << " requested_sound=0x" << diagnostic.requested_sound << std::dec
            << " replay_depth=" << diagnostic.replay_depth
            << " entry_route_enabled=" << diagnostic.entry_route_enabled
            << " entry_route_phase=" << audio_route_phase_name(diagnostic.entry_route_phase)
            << " entry_route_generation=" << diagnostic.entry_route_generation
            << " entry_lease_generation=" << diagnostic.entry_lease_generation
            << " entry_song_key=" << diagnostic.entry_song_key
            << " entry_route_owned=" << diagnostic.entry_route_owned
            << " entry_cleanup_pending=" << diagnostic.entry_cleanup_pending
            << " entry_setup_valid=" << diagnostic.entry_setup_valid
            << " entry_setup_stage="
            << static_cast<unsigned>(diagnostic.entry_setup_stage)
            << " entry_setup_registry_generation="
            << diagnostic.entry_setup_registry_generation
            << " entry_setup_route_generation="
            << diagnostic.entry_setup_route_generation
            << " entry_setup_lease_generation="
            << diagnostic.entry_setup_lease_generation
            << " entry_setup_song_key=" << diagnostic.entry_setup_song_key
            << " entry_setup_controller=0x" << std::hex
            << diagnostic.entry_setup_controller
            << " entry_setup_slot=0x" << diagnostic.entry_setup_slot
            << " entry_setup_bgm=0x" << diagnostic.entry_setup_bgm
            << " entry_setup_expected_sound=0x"
            << diagnostic.entry_setup_expected_sound << std::dec
            << " entry_setup_expected_sound_index="
            << diagnostic.entry_setup_expected_sound_index
            << " entry_setup_expected_sound_serial="
            << diagnostic.entry_setup_expected_sound_serial
            << " bridge_set_candidate=" << diagnostic.bridge_set_candidate
            << " bridge_generation=" << diagnostic.bridge_generation
            << " bridge_first_failure="
            << static_cast<unsigned>(diagnostic.bridge_first_failure)
            << " bridge_identity_read_attempted="
            << diagnostic.bridge_identity_read_attempted
            << " bridge_identity_read_succeeded="
            << diagnostic.bridge_identity_read_succeeded
            << " bridge_identity_index=" << diagnostic.bridge_identity_index
            << " bridge_identity_serial=" << diagnostic.bridge_identity_serial
            << " bridge_identity_exact=" << diagnostic.bridge_identity_exact
            << " bridge_result_phase="
            << static_cast<unsigned>(diagnostic.bridge_result_phase)
            << " aggregate_observation_created="
            << diagnostic.aggregate_observation_created
            << " aggregate_exit_evaluated=" << diagnostic.aggregate_exit_evaluated
            << " aggregate_exit_matched=" << diagnostic.aggregate_exit_matched
            << " aggregate_active_evaluated="
            << diagnostic.aggregate_active_evaluated
            << " aggregate_active_matched=" << diagnostic.aggregate_active_matched
            << " aggregate_route_advanced=" << diagnostic.aggregate_route_advanced
            << " pause_resume_evaluated=" << diagnostic.pause_resume_evaluated
            << " pause_resume_handled=" << diagnostic.pause_resume_handled
            << " deferred_handoff_evaluated="
            << diagnostic.deferred_handoff_evaluated
            << " deferred_handoff_pending="
            << diagnostic.deferred_handoff_pending
            << " route_disabled_evaluated=" << diagnostic.route_disabled_evaluated
            << " route_disabled_matched=" << diagnostic.route_disabled_matched
            << " private_invoked=" << diagnostic.private_invoked
            << " private_result="
            << private_controller_setup_result_name(diagnostic.private_result)
            << " native_forward_count=" << diagnostic.native_forward_count
            << " native_forward_overflow=" << diagnostic.native_forward_overflow;
        for (size_t index = 0; index < diagnostic.native_forwards.size(); ++index) {
            out << " forward" << index << "_controller=0x" << std::hex
                << diagnostic.native_forwards[index].controller
                << " forward" << index << "_sound=0x"
                << diagnostic.native_forwards[index].sound << std::dec;
        }
        core::log(core::LogLevel::Info, out.str());
    });
}

const char* private_controller_set_readiness_blocker_name(
    const PrivateControllerSetReadinessBlocker blocker) noexcept
{
    switch (blocker) {
    case PrivateControllerSetReadinessBlocker::None: return "none";
    case PrivateControllerSetReadinessBlocker::PlaybackPresent: return "playback_absent";
    case PrivateControllerSetReadinessBlocker::CleanupPresent: return "cleanup_absent";
    case PrivateControllerSetReadinessBlocker::ControllerMissing: return "controller_present";
    case PrivateControllerSetReadinessBlocker::RequestedSoundMissing:
        return "requested_sound_present";
    case PrivateControllerSetReadinessBlocker::ChainUnreadable: return "chain_read";
    case PrivateControllerSetReadinessBlocker::ControllerIdentityUnreadable:
        return "controller_identity_read";
    case PrivateControllerSetReadinessBlocker::RequestedSoundIdentityUnreadable:
        return "requested_sound_identity_read";
    case PrivateControllerSetReadinessBlocker::ControllerProofInvalid:
        return "controller_proof_valid";
    case PrivateControllerSetReadinessBlocker::RouteOwnedOrCleanupPending:
        return "route_unowned";
    case PrivateControllerSetReadinessBlocker::RetirementActive:
        return "retirement_absent";
    case PrivateControllerSetReadinessBlocker::SelectionMismatch:
        return "selection_matches";
    case PrivateControllerSetReadinessBlocker::RouteTokenMismatch:
        return "route_token_exact";
    case PrivateControllerSetReadinessBlocker::RoutePhaseMismatch:
        return "route_phase_armed";
    case PrivateControllerSetReadinessBlocker::RouteControllerMismatch:
        return "route_controller_exact";
    case PrivateControllerSetReadinessBlocker::SetupControllerMismatch:
        return "setup_controller_exact";
    case PrivateControllerSetReadinessBlocker::SetupSlotMismatch:
        return "setup_slot_exact";
    case PrivateControllerSetReadinessBlocker::SetupBgmMismatch:
        return "setup_bgm_exact";
    case PrivateControllerSetReadinessBlocker::ExpectedSoundPointerMismatch:
        return "expected_sound_pointer_exact";
    case PrivateControllerSetReadinessBlocker::ExpectedSoundHandleMismatch:
        return "expected_sound_handle_exact";
    case PrivateControllerSetReadinessBlocker::SetupStageMismatch:
        return "setup_stage_stop_observed";
    case PrivateControllerSetReadinessBlocker::SelectionGuardMismatch:
        return "selection_guard_exact";
    case PrivateControllerSetReadinessBlocker::RouteControllerIdentityMismatch:
        return "route_controller_identity_exact";
    case PrivateControllerSetReadinessBlocker::SetupSoundIdentityMismatch:
        return "setup_sound_identity_exact";
    case PrivateControllerSetReadinessBlocker::SetCaptureRejected:
        return "set_capture_succeeded";
    }
    return "unknown";
}

const char* private_controller_setup_result_name(
    const PrivateControllerSetupResult result) noexcept
{
    switch (result) {
    case PrivateControllerSetupResult::NotEligible: return "not_eligible";
    case PrivateControllerSetupResult::Forwarded: return "forwarded";
    case PrivateControllerSetupResult::Claimed: return "claimed";
    case PrivateControllerSetupResult::Failed: return "failed";
    }
    return "unknown";
}

void log_private_controller_set_readiness_diagnostic(
    const PrivateControllerSetReadinessDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.proposed) return;
    bgm_playback_observe_best_effort(true, [&]() {
        std::ostringstream out;
        out << "[audio_sead] private_controller_set_readiness"
            << " first_failed_predicate="
            << private_controller_set_readiness_blocker_name(diagnostic.first_failed)
            << " result=" << private_controller_setup_result_name(diagnostic.result)
            << " original_forwarded=" << (diagnostic.original_forwarded ? 1 : 0)
            << " original_forward_count=" << diagnostic.original_forward_count
            << " playback_absent=" << (diagnostic.playback_absent ? 1 : 0)
            << " cleanup_absent=" << (diagnostic.cleanup_absent ? 1 : 0)
            << " controller_present=" << (diagnostic.controller_present ? 1 : 0)
            << " requested_sound_present="
            << (diagnostic.requested_sound_present ? 1 : 0)
            << " chain_read=" << (diagnostic.chain_read ? 1 : 0)
            << " controller=0x" << std::hex << diagnostic.controller
            << " live_slot=0x" << diagnostic.live_slot
            << " live_bgm=0x" << diagnostic.live_bgm
            << " live_sound=0x" << diagnostic.live_sound
            << " live_request=0x" << diagnostic.live_request
            << std::dec << " live_state=" << static_cast<unsigned>(diagnostic.live_state)
            << " controller_identity_read="
            << (diagnostic.controller_identity_read ? 1 : 0)
            << " requested_sound_identity_read="
            << (diagnostic.requested_sound_identity_read ? 1 : 0)
            << " requested_sound_index=" << diagnostic.requested_sound_index
            << " requested_sound_serial=" << diagnostic.requested_sound_serial
            << " route_proof_mode="
            << controller_identity_proof_mode_name(diagnostic.route_controller_proof.mode)
            << " setup_proof_mode="
            << controller_identity_proof_mode_name(diagnostic.setup_controller_proof.mode)
            << " current_proof_mode="
            << controller_identity_proof_mode_name(diagnostic.current_controller_proof.mode)
            << " route_current_proof_exact="
            << (diagnostic.route_current_controller_proof_exact ? 1 : 0)
            << " setup_current_proof_exact="
            << (diagnostic.setup_current_controller_proof_exact ? 1 : 0)
            << " selection_generation=" << diagnostic.selection_generation
            << " selection_matches=" << (diagnostic.selection_matches ? 1 : 0)
            << " selection_guard_exact=" << (diagnostic.selection_guard_exact ? 1 : 0)
            << " route_phase=" << audio_route_phase_name(diagnostic.route_phase)
            << " route_generation=" << diagnostic.route_generation
            << " lease_generation=" << diagnostic.lease_generation
            << " song_key=" << diagnostic.song_key
            << " route_controller=0x" << std::hex << diagnostic.route_controller
            << std::dec << " route_unowned=" << (diagnostic.route_unowned ? 1 : 0)
            << " retirement_absent=" << (diagnostic.retirement_absent ? 1 : 0)
            << " route_token_exact=" << (diagnostic.route_token_exact ? 1 : 0)
            << " route_phase_exact=" << (diagnostic.route_phase_exact ? 1 : 0)
            << " route_controller_exact="
            << (diagnostic.route_controller_exact ? 1 : 0)
            << " route_controller_identity_exact="
            << (diagnostic.route_controller_identity_exact ? 1 : 0)
            << " setup_stage=" << static_cast<unsigned>(diagnostic.setup_stage)
            << " setup_token_route_generation="
            << diagnostic.setup_token_route_generation
            << " setup_token_lease_generation="
            << diagnostic.setup_token_lease_generation
            << " setup_token_song_key=" << diagnostic.setup_token_song_key
            << " setup_controller=0x" << std::hex << diagnostic.setup_controller
            << " setup_slot=0x" << diagnostic.setup_slot
            << " setup_bgm=0x" << diagnostic.setup_bgm
            << " setup_expected_sound=0x" << diagnostic.setup_expected_sound
            << std::dec << " setup_expected_sound_index="
            << diagnostic.setup_expected_sound_index
            << " setup_expected_sound_serial="
            << diagnostic.setup_expected_sound_serial
            << " setup_controller_exact="
            << (diagnostic.setup_controller_exact ? 1 : 0)
            << " setup_slot_exact=" << (diagnostic.setup_slot_exact ? 1 : 0)
            << " setup_bgm_exact=" << (diagnostic.setup_bgm_exact ? 1 : 0)
            << " expected_sound_pointer_exact="
            << (diagnostic.expected_sound_pointer_exact ? 1 : 0)
            << " expected_sound_handle_exact="
            << (diagnostic.expected_sound_handle_exact ? 1 : 0)
            << " setup_sound_identity_exact="
            << (diagnostic.setup_sound_identity_exact ? 1 : 0)
            << " setup_stage_exact=" << (diagnostic.setup_stage_exact ? 1 : 0)
            << " eligibility_exact=" << (diagnostic.eligibility_exact ? 1 : 0)
            << " set_capture_succeeded="
            << (diagnostic.capture_controller_set_succeeded ? 1 : 0)
            << " substrate_proof_available="
            << (diagnostic.substrate_proof_available ? 1 : 0)
            << " substrate_transaction_generation="
            << diagnostic.substrate_transaction_generation;
        core::log(core::LogLevel::Info, out.str());
    });
}

void fail_private_controller_setup(
    const AudioRouteLeaseIdentity& identity, const CustomContextToken& token,
    void* sound, uint64_t request_handle, uint8_t state,
    std::string_view predicate, bool custom_ownership_possible)
{
    if (custom_ownership_possible) {
        g_audio_route_disabled.store(true, std::memory_order_release);
    }
    bool thaw_profile = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        clear_unpublished_audio_setup_locked(
            identity, AudioRouteTransitionReason::PrivateControllerFailure);
        if (g_audio_route_state.lease_identity == identity) {
            g_audio_route_state.list_cleanup_pending = custom_ownership_possible;
            const auto transition = g_frozen_profile_lease.transition(
                custom_ownership_possible
                    ? AudioRouteCleanupEvent::NativeClearUnverified
                    : AudioRouteCleanupEvent::VerifiedNoRoute,
                identity);
            thaw_profile = transition.thaw_profile;
            if (transition.clear_route_metadata) {
                AudioRouteTransitionRecorder route_transition_record(
                    AudioRouteTransitionReason::PrivateControllerFailure,
                    AudioRouteTransitionKind::RouteReset);
                g_audio_route_state = {};
            }
        }
    }
    if (thaw_profile) {
        registry().clear_frozen_profile();
    }
    log_private_controller_setup("failed", token, sound, request_handle, state, predicate);
}

PrivateControllerSetupResult route_private_controller_set(
    void* controller, void* sound, const PlaybackSnapshot& playback,
    const CleanupLease& cleanup, const SelectionSnapshot& selection,
    PrivateControllerSetReadinessDiagnostic* diagnostic)
{
    if (diagnostic) {
        *diagnostic = {};
        diagnostic->playback_absent = !playback.song;
        diagnostic->cleanup_absent = !cleanup.song;
        diagnostic->controller_present = controller != nullptr;
        diagnostic->requested_sound_present = sound != nullptr;
        diagnostic->controller = reinterpret_cast<uintptr_t>(controller);
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const AudioRouteState& route = g_audio_route_state;
        const auto& setup = g_unpublished_audio_setup;
        diagnostic->proposed = setup
            && route.phase == AudioRoutePhase::Armed
            && setup.controller_stage == PrivateControllerSetupStage::StopObserved;
        if (diagnostic->proposed) {
            diagnostic->selection_generation = setup.selection.generation;
            diagnostic->route_phase = route.phase;
            diagnostic->route_generation = route.generation;
            diagnostic->lease_generation = route.lease_identity.generation;
            diagnostic->song_key = route.lease_identity.song_key;
            diagnostic->route_controller = reinterpret_cast<uintptr_t>(route.controller);
            diagnostic->route_unowned =
                !route.custom_resource_owned && !route.list_cleanup_pending;
            diagnostic->retirement_absent =
                route.stop_retirement.phase == AudioStopRetirementPhase::None;
            diagnostic->route_controller_proof =
                armed_stop_controller_proof_diagnostic(
                    route.controller_arm_attempted_proof);
            diagnostic->setup_controller_proof =
                armed_stop_controller_proof_diagnostic(setup.controller_proof);
            diagnostic->setup_stage = setup.controller_stage;
            diagnostic->setup_token_route_generation = setup.token.route_generation;
            diagnostic->setup_token_lease_generation = setup.token.lease_generation;
            diagnostic->setup_token_song_key = setup.token.song_key;
            diagnostic->setup_controller = reinterpret_cast<uintptr_t>(setup.controller);
            diagnostic->setup_slot = reinterpret_cast<uintptr_t>(setup.slot);
            diagnostic->setup_bgm = reinterpret_cast<uintptr_t>(setup.bgm);
            diagnostic->setup_expected_sound =
                reinterpret_cast<uintptr_t>(setup.expected_sound);
            diagnostic->setup_expected_sound_index =
                setup.expected_sound_handle.internal_index;
            diagnostic->setup_expected_sound_serial =
                setup.expected_sound_handle.serial_number;
            diagnostic->setup_stage_exact = true;
        }
    }
    const auto reject = [&](const PrivateControllerSetReadinessBlocker blocker) {
        if (diagnostic && diagnostic->proposed) {
            if (diagnostic->first_failed == PrivateControllerSetReadinessBlocker::None) {
                diagnostic->first_failed = blocker;
            }
            diagnostic->result = PrivateControllerSetupResult::NotEligible;
        }
        return PrivateControllerSetupResult::NotEligible;
    };
    const auto mark_forwarded = [&]() noexcept {
        if (diagnostic && diagnostic->proposed) {
            diagnostic->original_forwarded = true;
            ++diagnostic->original_forward_count;
        }
    };
    const auto forward_original = [&]() {
        mark_forwarded();
        call_bgm_slot_set_original(controller, sound);
    };
    const auto forward_original_seh = [&]() {
        mark_forwarded();
        return call_bgm_slot_set_original_seh(controller, sound);
    };
    if (playback.song) return reject(PrivateControllerSetReadinessBlocker::PlaybackPresent);
    if (cleanup.song) return reject(PrivateControllerSetReadinessBlocker::CleanupPresent);
    if (!controller) return reject(PrivateControllerSetReadinessBlocker::ControllerMissing);
    if (!sound) return reject(PrivateControllerSetReadinessBlocker::RequestedSoundMissing);

    void* current_slot = nullptr;
    void* current_bgm = nullptr;
    void* current_sound = nullptr;
    uint64_t current_request = 0;
    uint8_t current_state = 0xff;
    const bool chain_read = read_controller_audio_chain(
            controller, current_slot, current_bgm, current_sound,
            current_request, current_state);
    if (diagnostic && diagnostic->proposed) {
        diagnostic->chain_read = chain_read;
        diagnostic->live_slot = reinterpret_cast<uintptr_t>(current_slot);
        diagnostic->live_bgm = reinterpret_cast<uintptr_t>(current_bgm);
        diagnostic->live_sound = reinterpret_cast<uintptr_t>(current_sound);
        diagnostic->live_request = current_request;
        diagnostic->live_state = current_state;
    }
    if (!chain_read) return reject(PrivateControllerSetReadinessBlocker::ChainUnreadable);
    UObjectIdentity requested_controller_identity;
    UObjectIdentity requested_sound_identity;
    const bool controller_identity_read =
        read_uobject_identity(controller, requested_controller_identity);
    if (diagnostic && diagnostic->proposed) {
        diagnostic->controller_identity_read = controller_identity_read;
    }
    if (!controller_identity_read) {
        return reject(PrivateControllerSetReadinessBlocker::ControllerIdentityUnreadable);
    }
    const bool requested_sound_identity_read =
        read_uobject_identity(sound, requested_sound_identity);
    if (diagnostic && diagnostic->proposed) {
        diagnostic->requested_sound_identity_read = requested_sound_identity_read;
        if (requested_sound_identity_read) {
            diagnostic->requested_sound_index =
                requested_sound_identity.live.internal_index;
            diagnostic->requested_sound_serial =
                requested_sound_identity.live.serial_number;
        }
    }
    if (!requested_sound_identity_read) {
        return reject(
            PrivateControllerSetReadinessBlocker::RequestedSoundIdentityUnreadable);
    }
    const ControllerIdentityProof requested_controller_proof =
        controller_identity_proof(controller, requested_controller_identity);
    if (diagnostic && diagnostic->proposed) {
        diagnostic->current_controller_proof =
            armed_stop_controller_proof_diagnostic(requested_controller_proof);
    }
    if (!controller_identity_proof_valid(requested_controller_proof)) {
        return reject(PrivateControllerSetReadinessBlocker::ControllerProofInvalid);
    }

    SelectionSnapshot setup_selection;
    CustomContextToken captured_token;
    CustomContextToken set_token;
    AudioRouteLeaseIdentity lease_identity;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const AudioRouteState& route = g_audio_route_state;
        const bool setup_exists = static_cast<bool>(g_unpublished_audio_setup);
        const bool route_unowned =
            !route.custom_resource_owned && !route.list_cleanup_pending;
        const bool retirement_absent =
            route.stop_retirement.phase == AudioStopRetirementPhase::None;
        const bool selection_exact = setup_exists
            && selection.generation == g_unpublished_audio_setup.selection.generation
            && selection.song == g_unpublished_audio_setup.selection.song;
        const bool route_token_exact = setup_exists
            && token_matches_route(g_unpublished_audio_setup.token, route);
        const bool route_phase_exact = route.phase == AudioRoutePhase::Armed;
        const bool route_controller_exact = route.controller == controller;
        const bool setup_controller_exact = setup_exists
            && g_unpublished_audio_setup.controller == controller;
        const bool setup_slot_exact = setup_exists
            && g_unpublished_audio_setup.slot == current_slot;
        const bool setup_bgm_exact = setup_exists
            && g_unpublished_audio_setup.bgm == current_bgm;
        const bool expected_sound_pointer_exact = setup_exists
            && g_unpublished_audio_setup.expected_sound == sound;
        const bool expected_sound_handle_exact = setup_exists
            && private_object_handle_matches(
                g_unpublished_audio_setup.expected_sound_handle,
                requested_sound_identity.live);
        const bool setup_stage_exact = setup_exists
            && g_unpublished_audio_setup.controller_stage
                == PrivateControllerSetupStage::StopObserved;
        const PrivateControllerSetupEligibility eligibility{
            true,
            true,
            route_unowned,
            retirement_absent,
            selection_exact,
            route_token_exact && route_phase_exact && route_controller_exact
                && setup_controller_exact && setup_slot_exact && setup_bgm_exact
                && expected_sound_pointer_exact && expected_sound_handle_exact,
            setup_stage_exact,
        };
        if (diagnostic && diagnostic->proposed) {
            diagnostic->route_unowned = route_unowned;
            diagnostic->retirement_absent = retirement_absent;
            diagnostic->selection_matches = selection_exact;
            diagnostic->route_token_exact = route_token_exact;
            diagnostic->route_phase_exact = route_phase_exact;
            diagnostic->route_controller_exact = route_controller_exact;
            diagnostic->setup_controller_exact = setup_controller_exact;
            diagnostic->setup_slot_exact = setup_slot_exact;
            diagnostic->setup_bgm_exact = setup_bgm_exact;
            diagnostic->expected_sound_pointer_exact = expected_sound_pointer_exact;
            diagnostic->expected_sound_handle_exact = expected_sound_handle_exact;
            diagnostic->setup_stage_exact = setup_stage_exact;
            diagnostic->eligibility_exact =
                private_controller_setup_eligible(eligibility);
            diagnostic->route_current_controller_proof_exact =
                controller_identity_proof_matches(
                    route.controller_arm_attempted_proof,
                    requested_controller_proof);
            diagnostic->setup_current_controller_proof_exact = setup_exists
                && controller_identity_proof_matches(
                    g_unpublished_audio_setup.controller_proof,
                    requested_controller_proof);
        }
        if (!private_controller_setup_eligible(eligibility)) {
            if (!route_unowned) {
                return reject(
                    PrivateControllerSetReadinessBlocker::RouteOwnedOrCleanupPending);
            }
            if (!retirement_absent) {
                return reject(PrivateControllerSetReadinessBlocker::RetirementActive);
            }
            if (!selection_exact) {
                return reject(PrivateControllerSetReadinessBlocker::SelectionMismatch);
            }
            if (!route_token_exact) {
                return reject(PrivateControllerSetReadinessBlocker::RouteTokenMismatch);
            }
            if (!route_phase_exact) {
                return reject(PrivateControllerSetReadinessBlocker::RoutePhaseMismatch);
            }
            if (!route_controller_exact) {
                return reject(PrivateControllerSetReadinessBlocker::RouteControllerMismatch);
            }
            if (!setup_controller_exact) {
                return reject(PrivateControllerSetReadinessBlocker::SetupControllerMismatch);
            }
            if (!setup_slot_exact) {
                return reject(PrivateControllerSetReadinessBlocker::SetupSlotMismatch);
            }
            if (!setup_bgm_exact) {
                return reject(PrivateControllerSetReadinessBlocker::SetupBgmMismatch);
            }
            if (!expected_sound_pointer_exact) {
                return reject(
                    PrivateControllerSetReadinessBlocker::ExpectedSoundPointerMismatch);
            }
            if (!expected_sound_handle_exact) {
                return reject(
                    PrivateControllerSetReadinessBlocker::ExpectedSoundHandleMismatch);
            }
            return reject(PrivateControllerSetReadinessBlocker::SetupStageMismatch);
        }
        const bool selection_guard_exact = registry().selection_guard_matches(
            g_unpublished_audio_setup.selection, g_unpublished_audio_setup.token);
        const bool route_controller_identity_exact = selection_guard_exact
            && uobject_identity_matches(controller, route.controller_identity);
        const bool setup_sound_identity_exact = route_controller_identity_exact
            && uobject_identity_matches(sound, route.private_setup_sound_identity);
        if (diagnostic && diagnostic->proposed) {
            diagnostic->selection_guard_exact = selection_guard_exact;
            diagnostic->route_controller_identity_exact =
                route_controller_identity_exact;
            diagnostic->setup_sound_identity_exact = setup_sound_identity_exact;
        }
        if (!selection_guard_exact) {
            return reject(PrivateControllerSetReadinessBlocker::SelectionGuardMismatch);
        }
        if (!route_controller_identity_exact) {
            return reject(
                PrivateControllerSetReadinessBlocker::RouteControllerIdentityMismatch);
        }
        if (!setup_sound_identity_exact) {
            return reject(
                PrivateControllerSetReadinessBlocker::SetupSoundIdentityMismatch);
        }
        setup_selection = g_unpublished_audio_setup.selection;
        captured_token = g_unpublished_audio_setup.token;
        lease_identity = route.lease_identity;
        AudioRouteState& mutable_route = g_audio_route_state;
        mutable_route.phase = AudioRoutePhase::Rebuilding;
        mutable_route.patched_song_id = mutable_route.desired_song_id;
        mutable_route.sound = sound;
        ++mutable_route.generation;
        set_token = custom_context_token(captured_token.registry_generation, mutable_route);
        set_token.slot = current_slot;
        set_token.bgm = current_bgm;
        set_token.sound = sound;
        set_token.request_handle = current_request;
        if (!g_unpublished_audio_setup.capture_controller_set(
                captured_token, set_token, controller, requested_controller_proof,
                current_slot, current_bgm,
                sound, requested_sound_identity.live)) {
            AudioRouteTransitionRecorder route_transition_record(
                AudioRouteTransitionReason::PrivateSetCaptureRejected,
                AudioRouteTransitionKind::RouteReset);
            mutable_route.phase = AudioRoutePhase::Idle;
            return reject(PrivateControllerSetReadinessBlocker::SetCaptureRejected);
        }
        if (diagnostic && diagnostic->proposed) {
            diagnostic->capture_controller_set_succeeded = true;
        }
        mutable_route.list_cleanup_pending = true;
        (void)g_frozen_profile_lease.mark_native_arm_attempt(lease_identity);
    }
    log_private_controller_setup("set_captured", set_token, sound, current_request, current_state);

    const SidecarRuntimeState* sidecar = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        sidecar = find_ready_sidecar_locked(*setup_selection.song);
    }
    if (!sidecar) {
        fail_private_controller_setup(
            lease_identity, set_token, sound, current_request, current_state,
            "sidecar_not_ready", false);
        forward_original();
        if (diagnostic && diagnostic->proposed) {
            diagnostic->result = PrivateControllerSetupResult::Failed;
        }
        return PrivateControllerSetupResult::Failed;
    }

    void* prepatch_slot = nullptr;
    void* prepatch_bgm = nullptr;
    void* prepatch_sound = nullptr;
    uint64_t prepatch_request = 0;
    uint8_t prepatch_state = 0xff;
    bool prepatch_exact = registry().selection_guard_matches(setup_selection, set_token)
        && read_controller_audio_chain(
            controller, prepatch_slot, prepatch_bgm, prepatch_sound,
            prepatch_request, prepatch_state)
        && prepatch_slot == current_slot && prepatch_bgm == current_bgm;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        prepatch_exact = prepatch_exact
            && g_audio_route_state.lease_identity == lease_identity
            && g_audio_route_state.generation == set_token.route_generation
            && g_audio_route_state.phase == AudioRoutePhase::Rebuilding
            && g_unpublished_audio_setup.token == set_token
            && controller_identity_proof_matches_live(
                g_unpublished_audio_setup.controller_proof, controller)
            && uobject_identity_matches(controller, g_audio_route_state.controller_identity)
            && uobject_identity_matches(sound, g_audio_route_state.private_setup_sound_identity);
    }
    if (!prepatch_exact) {
        fail_private_controller_setup(
            lease_identity, set_token, sound, current_request, current_state,
            "set_prepatch_identity_or_selection_drift", false);
        forward_original();
        if (diagnostic && diagnostic->proposed) {
            diagnostic->result = PrivateControllerSetupResult::Failed;
        }
        return PrivateControllerSetupResult::Failed;
    }

    std::vector<AudioFieldPatch> applied;
    if (!patch_sound_for_sidecar_call(
            sound, *setup_selection.song, *sidecar, "private_controller_set", applied)) {
        const bool unsafe = g_audio_route_disabled.load(std::memory_order_acquire);
        fail_private_controller_setup(
            lease_identity, set_token, sound, current_request, current_state,
            unsafe ? "partial_patch_restore_failed" : "patch_apply_failed", unsafe);
        if (!unsafe) {
            forward_original();
        }
        if (diagnostic && diagnostic->proposed) {
            diagnostic->result = PrivateControllerSetupResult::Failed;
        }
        return PrivateControllerSetupResult::Failed;
    }
    bool patch_marked = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        patch_marked = g_unpublished_audio_setup.mark_controller_set_patched(set_token);
    }
    if (!patch_marked) {
        const bool setup_restore = restore_patches_reverse(applied);
        if (!setup_restore) retain_failed_patch_journal(applied);
        fail_private_controller_setup(
            lease_identity, set_token, sound, current_request, current_state,
            "setup_changed_before_patch_commit", !setup_restore);
        if (setup_restore) {
            forward_original();
        }
        if (diagnostic && diagnostic->proposed) {
            diagnostic->result = PrivateControllerSetupResult::Failed;
        }
        return PrivateControllerSetupResult::Failed;
    }
    log_private_controller_setup("patched", set_token, sound, current_request, current_state);

    if (!registry().selection_guard_matches(setup_selection, set_token)) {
        const bool setup_restore = restore_patches_reverse(applied);
        if (!setup_restore) retain_failed_patch_journal(applied);
        fail_private_controller_setup(
            lease_identity, set_token, sound, current_request, current_state,
            "set_selection_drift_before_forward", !setup_restore);
        if (setup_restore) forward_original();
        if (diagnostic && diagnostic->proposed) {
            diagnostic->result = PrivateControllerSetupResult::Failed;
        }
        return PrivateControllerSetupResult::Failed;
    }

    const bool forwarded = forward_original_seh();
    const bool restored = restore_patches_reverse(applied);
    if (!restored) {
        retain_failed_patch_journal(applied);
        g_audio_route_disabled.store(true, std::memory_order_release);
    }

    void* set_slot = nullptr;
    void* set_bgm = nullptr;
    void* set_sound = nullptr;
    uint64_t set_request = 0;
    uint8_t set_state = 0xff;
    const bool observed = read_controller_audio_chain(
        controller, set_slot, set_bgm, set_sound, set_request, set_state);
    ControllerIdentityProof set_controller_proof;
    const bool set_controller_proof_valid = observed
        && read_controller_identity_proof(controller, set_controller_proof);
    bool exact = forwarded && restored && observed
        && set_controller_proof_valid
        && registry().selection_guard_matches(setup_selection, set_token)
        && set_slot == current_slot && set_bgm == current_bgm && set_sound == sound
        && uobject_identity_matches(controller, requested_controller_identity)
        && uobject_identity_matches(sound, requested_sound_identity);
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        exact = exact
            && g_audio_route_state.lease_identity == lease_identity
            && g_audio_route_state.phase == AudioRoutePhase::Rebuilding
            && g_audio_route_state.generation == set_token.route_generation
            && uobject_identity_matches(
                sound, g_audio_route_state.private_setup_sound_identity)
            && g_unpublished_audio_setup.mark_controller_set_forwarded(
                set_token, set_controller_proof, set_request, set_state, restored);
    }
    if (!exact) {
        fail_private_controller_setup(
            lease_identity, set_token, sound, set_request, set_state,
            !forwarded ? "set_exception" : !restored ? "set_restore_failed"
                : !AudioBgmRequestHandle{set_request}.valid_bgm_request()
                    ? "set_request_not_type8"
                : set_request == current_request ? "set_request_not_fresh"
                : set_state != 2 ? "set_state_not_prepared"
                : "set_postcondition_mismatch",
            true);
        if (diagnostic && diagnostic->proposed) {
            diagnostic->result = PrivateControllerSetupResult::Failed;
        }
        return PrivateControllerSetupResult::Failed;
    }
    if (diagnostic && diagnostic->proposed) {
        diagnostic->result = PrivateControllerSetupResult::Forwarded;
    }
    return PrivateControllerSetupResult::Forwarded;
}

void __fastcall bgm_prepare_detour(void* bgm)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::Prepare);
    std::lock_guard<std::recursive_mutex> operation_lock(g_audio_route_operations.mutex());
    if (!callback_scope || g_audio_route_disabled.load(std::memory_order_acquire)) {
        if (g_audio_route_disabled.load(std::memory_order_acquire)) {
            clear_any_unpublished_audio_setup(
                AudioRouteTransitionReason::FeatureDisabled);
        }
        (void)call_bgm_prepare_original(bgm);
        return;
    }
    const PlaybackSnapshot playback = registry().playback_snapshot();
    SelectionSnapshot setup_selection;
    CustomContextToken setup_token;
    AudioRouteState route;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route = g_audio_route_state;
        if (playback.song && token_matches_route(playback.token, route)) {
            static_cast<SelectionSnapshot&>(setup_selection) =
                static_cast<const SelectionSnapshot&>(playback);
            setup_token = playback.token;
        } else if (g_unpublished_audio_setup
            && token_matches_route(g_unpublished_audio_setup.token, route)) {
            setup_selection = g_unpublished_audio_setup.selection;
            setup_token = g_unpublished_audio_setup.token;
        }
    }
    const SongDescriptor* song = setup_selection.song;
    if (!song || !bgm || route.desired_song_id != song->id
        || !token_matches_route(setup_token, route)
        || setup_token.bgm != bgm || !setup_token.sound) {
        (void)call_bgm_prepare_original(bgm);
        return;
    }

    const SidecarRuntimeState* sidecar = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        sidecar = find_ready_sidecar_locked(*song);
    }
    if (!sidecar) {
        static std::atomic_int s_missing_logs{0};
        if (s_missing_logs.fetch_add(1, std::memory_order_relaxed) < 16) {
            std::ostringstream out;
            out << "[audio_sead] route status=missing_sidecar song_id=" << song->id;
            core::log(core::LogLevel::Error, out.str());
        }
        (void)call_bgm_prepare_original(bgm);
        return;
    }

    void* current_sound = nullptr;
    const PlaybackSnapshot current_playback = registry().playback_snapshot();
    AudioRouteState current_route;
    bool exact_context = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        current_route = g_audio_route_state;
        const bool public_match = current_playback.song
            && current_playback.token == setup_token
            && token_matches_route(current_playback.token, current_route);
        const bool setup_match = g_unpublished_audio_setup
            && g_unpublished_audio_setup.token == setup_token
            && token_matches_route(setup_token, current_route);
        exact_context = !g_audio_route_disabled.load(std::memory_order_acquire)
            && (public_match || setup_match)
            && current_route.desired_song_id == song->id
            && setup_token.bgm == bgm && setup_token.sound;
    }
    if (!exact_context
        || !core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, current_sound)
        || !bgm_prepare_identity_matches(
            setup_token.bgm, setup_token.sound, bgm, current_sound)
        || !route_bgm_prepare_once(
            bgm, setup_token.bgm, setup_token.sound, *song, *sidecar)) {
        (void)call_bgm_prepare_original(bgm);
    }
}

PauseResumeDetourResult try_pause_resume_bank_set(
    void* controller, void* sound, PauseResumeBankMarker& marker)
{
    PauseResumeBankSession session;
    OnMemoryBankDetachedRecord active;
    bool sidecar_ready = false;
    bool lifecycle_failed = false;
    bool release_pending = false;
    uint64_t lifecycle_state_epoch = 0;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_pause_resume_bank.phase != PauseResumeBankPhase::ResumeStopObserved) {
            return {};
        }
        session = g_pause_resume_bank;
        active = g_onmemory_bank_lifecycle.active();
        lifecycle_failed = g_onmemory_bank_lifecycle.failed();
        release_pending = g_onmemory_bank_lifecycle.release_in_flight();
        lifecycle_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
        sidecar_ready = session.selection.song
            && find_ready_sidecar_locked(*session.selection.song);
    }
    const SelectionSnapshot selection = registry().selection_snapshot();
    const auto continuity = classify_pause_resume_selection_continuity(
        session.selection, selection);
    UObjectIdentity controller_identity;
    UObjectIdentity sound_identity;
    uint64_t owner_token = 0;
    const bool controller_observed = controller
        && read_uobject_identity(controller, controller_identity);
    const bool sound_observed = sound
        && read_uobject_identity(sound, sound_identity);
    const bool owner_observed = sound_observed
        && core::safe_read_field(
            sound, runtime_layouts::SqexSeadSound::observed_field548, owner_token);
    const bool exact_lifecycle = active
        && active.phase == OnMemoryBankLifecyclePhase::RestoreApplied
        && active.ordinal == session.detached.ordinal
        && active.sound == session.detached.sound
        && active.canonical.encode() == session.detached.canonical.encode()
        && active.custom.encode() == session.detached.custom.encode()
        && active.request_handle == session.detached.request_handle
        && active.backing_observed == session.detached.backing_observed
        && active.backing_identity == session.detached.backing_identity
        && lifecycle_state_epoch == session.lifecycle_state_epoch;
    const PauseResumeBankSetFacts facts{
        true,
        g_piano_audio_owner_tick
            && g_piano_audio_owner_tick == session.owner_tick,
        g_piano_audio_owner_tick_nonce != 0
            && g_piano_audio_owner_tick_nonce == session.owner_tick_nonce,
        audio_production_play_setup_tls().original_depth == 0,
        session.phase == PauseResumeBankPhase::ResumeStopObserved,
        continuity.semantics == PauseResumeSelectionSemantics::Match,
        controller_observed && controller == session.controller
            && controller_identity_proof_matches_live(
                controller_identity_proof(controller, controller_identity), controller),
        sound_observed && sound == session.detached.sound.object
            && sound_identity.live.internal_index
                == session.detached.sound.live.internal_index
            && sound_identity.live.serial_number
                == session.detached.sound.live.serial_number,
        owner_observed && owner_token == session.detached.canonical.encode(),
        exact_lifecycle,
        !lifecycle_failed && !release_pending,
        sidecar_ready,
        g_audio_route_installed.load(std::memory_order_acquire)
            && !g_audio_route_disabled.load(std::memory_order_acquire)
            && g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire)
            && g_onmemory_bank_release_available.load(std::memory_order_acquire),
    };
    if (!pause_resume_bank_set_eligible(facts)) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (pause_resume_epoch_matches(session.session_epoch, session.cycle_epoch,
                    g_pause_resume_bank.session_epoch, g_pause_resume_bank.cycle_epoch)) {
                g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
            }
        }
        marker = {PauseResumeBankMarkerStatus::Failed, "set_prefilter",
            session.session_epoch, session.cycle_epoch, true};
        return {facts.exact_canonical_owner
                ? PauseResumeDetourDisposition::ForwardAfterVerifiedCanonical
                : PauseResumeDetourDisposition::SuppressFailClosed,
            0, facts.exact_canonical_owner};
    }

    std::vector<AudioFieldPatch> requested;
    std::vector<AudioFieldPatch> applied;
    if (!append_patch(requested, sound,
            runtime_layouts::SqexSeadSound::observed_field548,
            session.detached.custom.encode(), sizeof(uint64_t),
            "pause_resume_bank_owner", true)
        || requested.size() != 1
        || requested.front().original != session.detached.canonical.encode()) {
        marker = {PauseResumeBankMarkerStatus::Failed, "set_patch_prepare",
            session.session_epoch, session.cycle_epoch, true};
        return {PauseResumeDetourDisposition::SuppressFailClosed, 0, false};
    }
    const AudioFieldPatch patch = requested.front();
    const auto apply_outcome = apply_pause_resume_patch_transaction(
        [&]() {
            if (!write_field_patch(patch)) return false;
            applied.push_back(patch);
            return true;
        },
        [&]() { return verify_field_value(patch, patch.replacement); },
        [&]() { return verify_field_value(patch, patch.original); },
        [&]() { return restore_pause_resume_owner_patch(applied); });
    if (apply_outcome != PauseResumePatchApplyOutcome::AppliedVerified
        || applied.size() != 1 || applied.front().offset
            != runtime_layouts::SqexSeadSound::observed_field548) {
        const bool canonical = apply_outcome
            == PauseResumePatchApplyOutcome::ApplyFailedCanonicalRestored;
        if (!canonical) {
            g_audio_route_disabled.store(true, std::memory_order_release);
            if (!applied.empty()) retain_failed_patch_journal(applied);
        }
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
        marker = {PauseResumeBankMarkerStatus::Failed,
            canonical ? "set_apply_restored" : "set_owner_uncertain",
            session.session_epoch, session.cycle_epoch, true};
        return {canonical
                ? PauseResumeDetourDisposition::ForwardAfterVerifiedCanonical
                : PauseResumeDetourDisposition::SuppressFailClosed,
            0, canonical};
    }

    bool native_forwarded = false;
    void* post_slot = nullptr;
    void* post_bgm = nullptr;
    void* post_sound = nullptr;
    uint64_t post_request = 0;
    uint8_t post_state = 0;
    const auto forward_result = coordinate_pause_resume_native_forward(
        [&]() {
            native_forwarded = true;
            return call_bgm_slot_set_original_seh(controller, sound);
        },
        [&]() {
            uint64_t post_owner = 0;
            UObjectIdentity post_sound_identity;
            return read_controller_audio_chain(controller, post_slot, post_bgm,
                    post_sound, post_request, post_state)
                && post_sound == sound
                && AudioBgmRequestHandle{post_request}.valid_bgm_request()
                && post_state == 2
                && read_uobject_identity(post_sound, post_sound_identity)
                && post_sound_identity.live.internal_index
                    == session.detached.sound.live.internal_index
                && post_sound_identity.live.serial_number
                    == session.detached.sound.live.serial_number
                && core::safe_read_field(post_sound,
                    runtime_layouts::SqexSeadSound::observed_field548, post_owner)
                && post_owner == session.detached.custom.encode();
        },
        [&]() { return static_cast<bool>(restore_pause_resume_owner_patch(applied)); },
        false);
    const auto disposition = pause_resume_forward_disposition(
        forward_result, native_forwarded ? 1 : 0, false);
    if (forward_result == PauseResumeNativeForwardResult::Succeeded) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (pause_resume_epoch_matches(session.session_epoch, session.cycle_epoch,
                g_pause_resume_bank.session_epoch, g_pause_resume_bank.cycle_epoch)
            && g_pause_resume_bank.phase == PauseResumeBankPhase::ResumeStopObserved) {
            g_pause_resume_bank.phase = PauseResumeBankPhase::OwnerRebound;
            g_pause_resume_bank.owner_patch = patch;
            g_pause_resume_bank.slot = post_slot;
            g_pause_resume_bank.bgm = post_bgm;
            g_pause_resume_bank.request_handle = post_request;
            marker = {PauseResumeBankMarkerStatus::SetRebound, "exact_set",
                session.session_epoch, session.cycle_epoch, true};
            return disposition;
        }
        (void)restore_pause_resume_owner_patch(applied);
    }
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
    }
    marker = {PauseResumeBankMarkerStatus::Failed,
        forward_result == PauseResumeNativeForwardResult::NativeFailedRestored
            ? "set_native_fault" : "set_postcondition_or_restore",
        session.session_epoch, session.cycle_epoch, true};
    return disposition;
}

void __fastcall bgm_slot_set_detour(void* controller, void* sound)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::Set);
    PauseResumeBankMarker pause_marker;
    auto deferred_pause_marker = make_deferred_noexcept_action([&]() noexcept {
        emit_or_batch_pause_resume_bank_marker(pause_marker);
    });
    PrivateControllerSetReadinessDiagnostic private_set_diagnostic;
    auto deferred_private_set_diagnostic = make_deferred_noexcept_action([&]() noexcept {
        log_private_controller_set_readiness_diagnostic(private_set_diagnostic);
    });
    PrivateControllerSetDispatchDiagnostic set_dispatch_diagnostic;
    auto deferred_set_dispatch_diagnostic = make_deferred_noexcept_action([&]() noexcept {
        log_private_controller_set_dispatch_diagnostic(set_dispatch_diagnostic);
    });
    std::lock_guard<std::recursive_mutex> operation_lock(g_audio_route_operations.mutex());
    set_dispatch_diagnostic.proposed = true;
    set_dispatch_diagnostic.controller = reinterpret_cast<uintptr_t>(controller);
    set_dispatch_diagnostic.requested_sound = reinterpret_cast<uintptr_t>(sound);
    set_dispatch_diagnostic.replay_depth = g_native_play_setup_replay_depth;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const auto& route = g_audio_route_state;
        const auto& setup = g_unpublished_audio_setup;
        set_dispatch_diagnostic.entry_route_enabled =
            !g_audio_route_disabled.load(std::memory_order_acquire);
        set_dispatch_diagnostic.entry_route_phase = route.phase;
        set_dispatch_diagnostic.entry_route_generation = route.generation;
        set_dispatch_diagnostic.entry_lease_generation = route.lease_identity.generation;
        set_dispatch_diagnostic.entry_song_key = route.lease_identity.song_key;
        set_dispatch_diagnostic.entry_route_owned = native_audio_route_owned_locked();
        set_dispatch_diagnostic.entry_cleanup_pending = route.list_cleanup_pending;
        set_dispatch_diagnostic.entry_setup_valid = static_cast<bool>(setup);
        if (setup) {
            set_dispatch_diagnostic.entry_setup_stage = setup.controller_stage;
            set_dispatch_diagnostic.entry_setup_registry_generation =
                setup.token.registry_generation;
            set_dispatch_diagnostic.entry_setup_route_generation =
                setup.token.route_generation;
            set_dispatch_diagnostic.entry_setup_lease_generation =
                setup.token.lease_generation;
            set_dispatch_diagnostic.entry_setup_song_key = setup.token.song_key;
            set_dispatch_diagnostic.entry_setup_controller =
                reinterpret_cast<uintptr_t>(setup.controller);
            set_dispatch_diagnostic.entry_setup_slot =
                reinterpret_cast<uintptr_t>(setup.slot);
            set_dispatch_diagnostic.entry_setup_bgm =
                reinterpret_cast<uintptr_t>(setup.bgm);
            set_dispatch_diagnostic.entry_setup_expected_sound =
                reinterpret_cast<uintptr_t>(setup.expected_sound);
            set_dispatch_diagnostic.entry_setup_expected_sound_index =
                setup.expected_sound_handle.internal_index;
            set_dispatch_diagnostic.entry_setup_expected_sound_serial =
                setup.expected_sound_handle.serial_number;
        }
    }
    CanonicalSubstrateBridgeAuthority set_bridge;
    bool read_bridge_set_identity = false;
    const auto bridge_set_candidate_facts_locked = [&]() noexcept {
        const auto& route = g_audio_route_state;
        const auto& setup = g_unpublished_audio_setup;
        const auto& bridge = setup.substrate_bridge;
        return CanonicalSubstrateBridgeSetCandidateFacts{
            bridge.phase
                == CanonicalSubstrateBridgePhase::PatchedOriginalInFlight,
            bridge.generation != 0,
            audio_production_play_setup_tls().original_depth != 0,
            route.phase == AudioRoutePhase::PatchedPlaySetup,
            bridge.route_generation != 0
                && bridge.route_generation == route.generation,
            bridge.lease == route.lease_identity,
            bridge.song_key != 0
                && bridge.song_key == route.lease_identity.song_key,
            setup && setup.token.route_generation == route.generation
                && setup.token.lease_generation == route.lease_identity.generation
                && setup.token.song_key == route.lease_identity.song_key,
            setup && bridge.selection_generation == setup.selection.generation
                && setup.selection.song
                && bridge.song_key
                    == audio_route_song_key(setup.selection.song->id),
            bridge.revocation_epoch
                == selection_audio_activation_revocation_epoch(),
            setup.controller == controller && bridge.controller == controller,
            sound != nullptr,
            bridge.expected_callback_sound == sound,
        };
    };
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        auto& bridge = g_unpublished_audio_setup.substrate_bridge;
        if (bridge.phase
                == CanonicalSubstrateBridgePhase::PatchedOriginalInFlight) {
            set_dispatch_diagnostic.bridge_set_candidate = true;
            set_dispatch_diagnostic.bridge_generation = bridge.generation;
            const auto candidate_facts = bridge_set_candidate_facts_locked();
            set_dispatch_diagnostic.bridge_first_failure =
                first_canonical_substrate_bridge_set_candidate_failure(
                    candidate_facts);
            if (canonical_substrate_bridge_set_identity_read_required(
                    candidate_facts)) {
                set_bridge = bridge;
                read_bridge_set_identity = true;
            } else {
                bridge.phase = canonical_substrate_bridge_set_result_phase(
                    bridge.phase,
                    set_dispatch_diagnostic.bridge_first_failure);
                set_dispatch_diagnostic.bridge_result_phase = bridge.phase;
            }
        } else if (bridge.phase == CanonicalSubstrateBridgePhase::SetBound
            && audio_production_play_setup_tls().original_depth != 0) {
            set_dispatch_diagnostic.bridge_set_candidate = true;
            set_dispatch_diagnostic.bridge_generation = bridge.generation;
            set_dispatch_diagnostic.bridge_first_failure =
                CanonicalSubstrateBridgeSetFailure::DuplicateSet;
            bridge.phase = canonical_substrate_bridge_set_result_phase(
                bridge.phase, CanonicalSubstrateBridgeSetFailure::DuplicateSet);
            set_dispatch_diagnostic.bridge_result_phase = bridge.phase;
        }
    }
    UObjectIdentity bridge_set_sound_identity;
    if (read_bridge_set_identity) {
        set_dispatch_diagnostic.bridge_identity_read_attempted = true;
        set_dispatch_diagnostic.bridge_identity_read_succeeded =
            read_uobject_identity(sound, bridge_set_sound_identity)
            && bridge_set_sound_identity.live_capture_succeeded;
        if (set_dispatch_diagnostic.bridge_identity_read_succeeded) {
            set_dispatch_diagnostic.bridge_identity_index =
                bridge_set_sound_identity.live.internal_index;
            set_dispatch_diagnostic.bridge_identity_serial =
                bridge_set_sound_identity.live.serial_number;
        }
        const CanonicalSubstrateBridgeSetIdentityFacts identity_facts{
            set_dispatch_diagnostic.bridge_identity_read_succeeded,
            set_dispatch_diagnostic.bridge_identity_read_succeeded
                && bridge_set_sound_identity.live.internal_index
                    == set_bridge.expected_callback_sound_identity.internal_index,
            set_dispatch_diagnostic.bridge_identity_read_succeeded
                && bridge_set_sound_identity.live.serial_number
                    == set_bridge.expected_callback_sound_identity.serial_number,
        };
        set_dispatch_diagnostic.bridge_first_failure =
            first_canonical_substrate_bridge_set_identity_failure(identity_facts);
        set_dispatch_diagnostic.bridge_identity_exact =
            set_dispatch_diagnostic.bridge_first_failure
                == CanonicalSubstrateBridgeSetFailure::None;
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        auto& bridge = g_unpublished_audio_setup.substrate_bridge;
        if (bridge.generation == set_bridge.generation
            && bridge.phase
                == CanonicalSubstrateBridgePhase::PatchedOriginalInFlight) {
            if (set_dispatch_diagnostic.bridge_identity_exact) {
                const auto revalidated =
                    first_canonical_substrate_bridge_set_candidate_failure(
                        bridge_set_candidate_facts_locked());
                if (revalidated == CanonicalSubstrateBridgeSetFailure::None) {
                    bridge.phase = canonical_substrate_bridge_set_result_phase(
                        bridge.phase, revalidated);
                } else {
                    set_dispatch_diagnostic.bridge_first_failure = revalidated;
                    bridge.phase = canonical_substrate_bridge_set_result_phase(
                        bridge.phase, revalidated);
                }
            } else {
                bridge.phase = canonical_substrate_bridge_set_result_phase(
                    bridge.phase,
                    set_dispatch_diagnostic.bridge_first_failure);
            }
            set_dispatch_diagnostic.bridge_result_phase = bridge.phase;
        } else {
            set_dispatch_diagnostic.bridge_first_failure =
                CanonicalSubstrateBridgeSetFailure::GenerationInvalid;
            if (bridge.generation == set_bridge.generation) {
                bridge.phase = canonical_substrate_bridge_set_result_phase(
                    bridge.phase,
                    set_dispatch_diagnostic.bridge_first_failure);
                set_dispatch_diagnostic.bridge_result_phase = bridge.phase;
            }
        }
    }
    deferred_set_dispatch_diagnostic.make_eligible();
    const auto record_native_forward = [&](void* forward_controller, void* forward_sound) {
        const uint32_t index = set_dispatch_diagnostic.native_forward_count++;
        if (index < set_dispatch_diagnostic.native_forwards.size()) {
            set_dispatch_diagnostic.native_forwards[index] = {
                reinterpret_cast<uintptr_t>(forward_controller),
                reinterpret_cast<uintptr_t>(forward_sound)};
        } else {
            set_dispatch_diagnostic.native_forward_overflow = true;
        }
    };
    const auto forward_original = [&](void* forward_sound) {
        record_native_forward(controller, forward_sound);
        call_bgm_slot_set_original(controller, forward_sound);
    };
    const auto forward_original_seh = [&](void* forward_sound) {
        record_native_forward(controller, forward_sound);
        return call_bgm_slot_set_original_seh(controller, forward_sound);
    };
    if (bypass_audio_native_detour(g_native_play_setup_replay_depth)) {
        set_dispatch_diagnostic.decision =
            PrivateControllerSetDispatchDecision::ReplayBypass;
        forward_original(sound);
        return;
    }
    BgmPlaybackSetObservationScope playback_set_observation(controller, sound);
    set_dispatch_diagnostic.aggregate_observation_created = true;
    set_dispatch_diagnostic.aggregate_exit_evaluated = true;
    const bool aggregate_exit = bgm_aggregate_lineage_exit_pending(controller);
    set_dispatch_diagnostic.aggregate_exit_matched = aggregate_exit;
    if (aggregate_exit) {
        set_dispatch_diagnostic.decision =
            PrivateControllerSetDispatchDecision::AggregateExit;
        log_bgm_aggregate_mutation("exit_native_forward", "set", 0, 0, 0);
        forward_original(sound);
        return;
    }
    set_dispatch_diagnostic.aggregate_active_evaluated = true;
    const bool aggregate_active = bgm_aggregate_lineage_active(controller);
    set_dispatch_diagnostic.aggregate_active_matched = aggregate_active;
    if (aggregate_active) {
        const auto aggregate_set_forward =
            begin_bgm_aggregate_set_forward(controller);
        emit_bgm_aggregate_set_forward_log(aggregate_set_forward);
        const bool route_advanced = aggregate_set_forward.route_advanced;
        set_dispatch_diagnostic.aggregate_route_advanced = route_advanced;
        set_dispatch_diagnostic.decision =
            PrivateControllerSetDispatchDecision::AggregateActive;
        if (!route_advanced) {
            log_bgm_aggregate_mutation("authorization_blocked",
                "set_route_transition", 0, 0, 0);
        }
        // Aggregate lineage never suppresses, clears, or replays native Set.
        forward_original(sound);
        return;
    }
    if (callback_scope) {
        set_dispatch_diagnostic.pause_resume_evaluated = true;
        const auto pause_result = try_pause_resume_bank_set(
            controller, sound, pause_marker);
        if (pause_marker.eligible) deferred_pause_marker.make_eligible();
        const bool pause_handled = dispatch_pause_resume_detour(
            pause_result, [&]() { return forward_original_seh(sound); });
        set_dispatch_diagnostic.pause_resume_handled = pause_handled;
        if (pause_handled) {
            set_dispatch_diagnostic.decision =
                PrivateControllerSetDispatchDecision::PauseResumeHandled;
            return;
        }
    }
    bool tracked_custom_ownership = false;
    set_dispatch_diagnostic.deferred_handoff_evaluated = true;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        auto& deferred = g_audio_route_state.deferred_native_handoff;
        if (controller && deferred.controller == controller && deferred.active()
            && deferred.phase != AudioDeferredNativeHandoffPhase::NativePlayForwarded) {
            if (deferred.phase == AudioDeferredNativeHandoffPhase::RetainedFailure) {
                deferred = {};
                g_audio_route_state.deferred_native_sound_identity = {};
                core::log(core::LogLevel::Info,
                    "[audio_sead] slot_set status=deferred_native_failure_retrying");
            } else {
                const bool cleared_intent_superseded = g_audio_route_state.native_clear_verified
                && (deferred.phase == AudioDeferredNativeHandoffPhase::CustomCleared
                    || deferred.phase == AudioDeferredNativeHandoffPhase::NativeSetApplied)
                && deferred.requested_sound != sound;
                if (cleared_intent_superseded) {
                    deferred = {};
                    g_audio_route_state.deferred_native_sound_identity = {};
                    core::log(core::LogLevel::Info,
                        "[audio_sead] slot_set status=deferred_native_intent_superseded_after_clear");
                } else {
                    set_dispatch_diagnostic.deferred_handoff_pending = true;
                    set_dispatch_diagnostic.decision =
                        PrivateControllerSetDispatchDecision::DeferredHandoffPending;
                    core::log(core::LogLevel::Info,
                        "[audio_sead] slot_set status=deferred_native_intent_already_pending");
                    return;
                }
            }
        }
        if (controller && deferred.controller == controller
            && deferred.phase == AudioDeferredNativeHandoffPhase::NativePlayForwarded) {
            deferred = {};
            g_audio_route_state.deferred_native_sound_identity = {};
        }
        tracked_custom_ownership = controller
            && g_audio_route_state.controller == controller
            && g_audio_route_state.custom_resource_owned;
    }
    set_dispatch_diagnostic.route_disabled_evaluated = true;
    const bool route_disabled = g_audio_route_disabled.load(std::memory_order_acquire)
        && !tracked_custom_ownership;
    set_dispatch_diagnostic.route_disabled_matched = route_disabled;
    if (route_disabled
        && !tracked_custom_ownership) {
        set_dispatch_diagnostic.decision =
            PrivateControllerSetDispatchDecision::RouteDisabled;
        clear_any_unpublished_audio_setup(
            AudioRouteTransitionReason::FeatureDisabled);
        const PlaybackSnapshot playback = registry().playback_snapshot();
        if (playback.song) (void)revoke_playback_snapshot(playback);
        forward_original(sound);
        return;
    }
    set_dispatch_diagnostic.private_invoked = true;
    const PrivateControllerSetupResult private_setup_result = route_private_controller_set(
        controller, sound, registry().playback_snapshot(),
        registry().cleanup_lease(), registry().selection_snapshot(),
        &private_set_diagnostic);
    set_dispatch_diagnostic.private_result = private_setup_result;
    for (uint32_t index = 0; index < private_set_diagnostic.original_forward_count; ++index) {
        record_native_forward(controller, sound);
    }
    if (private_set_diagnostic.proposed) {
        deferred_private_set_diagnostic.make_eligible();
    }
    if (private_setup_result != PrivateControllerSetupResult::NotEligible) {
        set_dispatch_diagnostic.decision =
            PrivateControllerSetDispatchDecision::PrivateHandled;
        return;
    }
    set_dispatch_diagnostic.decision =
        PrivateControllerSetDispatchDecision::PrivateNotEligible;
    const auto forward_after_private = [&](void* requested_sound) {
        if (private_set_diagnostic.proposed) {
            private_set_diagnostic.original_forwarded = true;
            ++private_set_diagnostic.original_forward_count;
        }
        forward_original(requested_sound);
    };
    bool invalidated_route = false;
    bool transfer_stop_authorization = false;
    uint64_t set_generation = 0;
    UObjectIdentity controller_identity;
    AudioRouteState prior_route;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (controller && g_audio_route_state.controller == controller) {
            AudioRouteTransitionRecorder route_transition_record(
                AudioRouteTransitionReason::SlotSetInvalidation,
                AudioRouteTransitionKind::RouteReset);
            prior_route = g_audio_route_state;
            transfer_stop_authorization = sound
                && audio_production_play_setup_tls().original_depth != 0
                && g_audio_route_state.phase == AudioRoutePhase::Playing
                && g_audio_route_state.stop_observed
                && g_audio_route_state.stop_authorized_generation == g_audio_route_state.generation + 1;
            controller_identity = g_audio_route_state.controller_identity;
            if (!g_pending_play_setup_patch.patches.empty()) {
                g_pending_play_setup_patch.restore_safe = false;
            }
            ++g_audio_route_state.generation;
            set_generation = g_audio_route_state.generation;
            g_audio_route_state.phase = AudioRoutePhase::Idle;
            g_audio_route_state.desired_song_id.clear();
            g_audio_route_state.patched_song_id.clear();
            g_audio_route_state.sound = nullptr;
            g_audio_route_state.stop_observed = false;
            g_audio_route_state.stop_authorized_generation = 0;
            g_audio_route_state.set_play_handoff_pending = false;
            g_audio_route_state.reusable_sound = nullptr;
            g_audio_route_state.reusable_sound_identity = {};
            g_audio_route_state.reusable_slot = nullptr;
            g_audio_route_state.reusable_bgm = nullptr;
            g_piano_audio_request_profile = {};
            g_native_play_setup_profile = {};
            g_slot_setup_profile = {};
            invalidated_route = true;
        }
    }
    if (invalidated_route) {
        (void)revoke_registry_playback(prior_route);
    }
    void* previous_slot = nullptr;
    void* previous_bgm = nullptr;
    void* previous_sound = nullptr;
    uint64_t previous_request_handle = 0;
    uint8_t previous_state = 0;
    const bool sound_transition_expected = transfer_stop_authorization
        && prior_route.custom_resource_owned
        && lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, controller_identity)
        && uobject_identity_matches(prior_route.owned_sound, prior_route.owned_sound_identity)
        && read_controller_bgm_chain(controller, previous_slot, previous_bgm)
        && core::safe_read_field(previous_bgm, runtime_layouts::SqexSeadBgm::sound, previous_sound)
        && core::safe_read_field(previous_bgm, runtime_layouts::SqexSeadBgm::request_handle, previous_request_handle)
        && core::safe_read_field(previous_slot, runtime_layouts::SqexSeadSlot::state, previous_state)
        && previous_slot == prior_route.owned_slot
        && previous_bgm == prior_route.owned_bgm
        && previous_sound == prior_route.owned_sound
        && previous_request_handle == prior_route.owned_request_handle
        && previous_sound != sound
        && previous_request_handle != 0
        && previous_state == 4;
    const bool defer_native_handoff = invalidated_route
        && !transfer_stop_authorization
        && prior_route.custom_resource_owned
        && sound;
    if (!defer_native_handoff) {
        set_dispatch_diagnostic.decision =
            PrivateControllerSetDispatchDecision::FallbackDirect;
        forward_after_private(sound);
    }
    bool authorization_transferred = false;
    if (transfer_stop_authorization
        && lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, controller_identity)) {
        void* slot = nullptr;
        void* bgm = nullptr;
        void* applied_sound = nullptr;
        uint64_t request_handle = 0;
        uint8_t slot_state = 0;
        const bool native_set_applied = sound_transition_expected
            && read_controller_bgm_chain(controller, slot, bgm)
            && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, applied_sound)
            && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::request_handle, request_handle)
            && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, slot_state)
            && slot == previous_slot
            && bgm == previous_bgm
            && applied_sound == sound
            && request_handle != 0
            && (slot_state == 2 || slot_state == 4);
        if (native_set_applied) {
            UObjectIdentity sound_identity;
            const bool sound_identity_valid = read_uobject_identity(sound, sound_identity);
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.controller == controller
                && g_audio_route_state.generation == set_generation
                && g_audio_route_state.phase == AudioRoutePhase::Idle
                && sound_identity_valid) {
                // A native Set immediately following the observed Stop owns the new
                // request handle. Authorize one clear on the next arm generation.
                g_audio_route_state.stop_observed = true;
                g_audio_route_state.stop_authorized_generation = set_generation + 1;
                g_audio_route_state.set_play_handoff_pending = true;
                g_audio_route_state.handoff_slot = slot;
                g_audio_route_state.handoff_bgm = bgm;
                g_audio_route_state.handoff_sound = sound;
                g_audio_route_state.handoff_sound_identity = sound_identity;
                g_audio_route_state.handoff_request_handle = request_handle;
                g_audio_route_state.custom_resource_owned = true;
                g_audio_route_state.owned_slot = slot;
                g_audio_route_state.owned_bgm = bgm;
                g_audio_route_state.owned_sound = sound;
                g_audio_route_state.owned_sound_identity = sound_identity;
                g_audio_route_state.owned_request_handle = request_handle;
                g_audio_route_state.reusable_sound = sound;
                g_audio_route_state.reusable_sound_identity = sound_identity;
                g_audio_route_state.reusable_slot = slot;
                g_audio_route_state.reusable_bgm = bgm;
                g_audio_route_state.list_cleanup_pending = true;
                authorization_transferred = true;
            }
        }
    }
    if (defer_native_handoff && !authorization_transferred) {
        set_dispatch_diagnostic.decision =
            PrivateControllerSetDispatchDecision::FallbackDeferred;
        log_frozen_sound_patch_snapshot(
            "before_native_handoff", prior_route.frozen_sound_patch);
        const AudioNativeRouteObservation owned_observation{
            prior_route.owned_slot, prior_route.owned_bgm, prior_route.owned_sound,
            prior_route.owned_request_handle, 4, prior_route.custom_resource_owned,
            prior_route.generation, prior_route.lease_identity,
        };
        const AudioNativeRouteObservation live_owned = observe_native_route(
            controller, controller_identity, prior_route.generation, prior_route.lease_identity);
        UObjectIdentity requested_sound_identity;
        const bool exact_owned_route = live_owned.valid
            && live_owned.slot == owned_observation.slot
            && live_owned.bgm == owned_observation.bgm
            && live_owned.sound == owned_observation.sound
            && live_owned.request_handle == owned_observation.request_handle
            && live_owned.state == 4
            && uobject_identity_matches(prior_route.owned_sound, prior_route.owned_sound_identity);
        const bool requested_identity_valid = read_uobject_identity(
            sound, requested_sound_identity);
        if (requested_identity_valid) {
            log_sound_route_fields(
                "requested_before_native_handoff", sound, requested_sound_identity);
        }
        AudioDeferredNativeHandoffState deferred;
        const bool intent_captured = exact_owned_route && requested_identity_valid
            && begin_deferred_native_handoff(
                deferred, owned_observation, controller, sound, set_generation);
        bool clear_verified = false;
        bool native_set_applied = false;
        bool native_set_forwarded = false;
        if (intent_captured) {
            forward_after_private(nullptr);
            AudioNativeRouteObservation cleared = observe_native_route(
                controller, controller_identity, set_generation, prior_route.lease_identity);
            log_deferred_route_diagnostics(
                "after_clear", controller, controller_identity, cleared, deferred);
            log_frozen_sound_patch_snapshot(
                "after_native_clear", prior_route.frozen_sound_patch);
            clear_verified = record_deferred_native_clear(
                deferred, owned_observation, cleared);
            forward_after_private(sound);
            native_set_forwarded = true;
            const AudioNativeRouteObservation rebound = observe_native_route(
                controller, controller_identity, set_generation, prior_route.lease_identity);
            log_deferred_route_diagnostics(
                "after_native_set", controller, controller_identity, rebound, deferred);
            log_frozen_sound_patch_snapshot(
                "after_native_set", prior_route.frozen_sound_patch);
            log_sound_route_fields(
                "requested_after_native_set", sound, requested_sound_identity);
            if (clear_verified) {
                native_set_applied = uobject_identity_matches(sound, requested_sound_identity)
                    && record_deferred_native_set(deferred, rebound);
            }
            if (!native_set_applied) {
                deferred.phase = AudioDeferredNativeHandoffPhase::RetainedFailure;
            }
        }
        if (!intent_captured) {
            forward_after_private(sound);
            native_set_forwarded = true;
            deferred.phase = AudioDeferredNativeHandoffPhase::RetainedFailure;
            deferred.lease_identity = prior_route.lease_identity;
            deferred.route_generation = set_generation;
            deferred.controller = controller;
            deferred.slot = prior_route.owned_slot;
            deferred.bgm = prior_route.owned_bgm;
            deferred.requested_sound = sound;
            deferred.retired_custom_request_handle = prior_route.owned_request_handle;
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.controller == controller
                && g_audio_route_state.generation == set_generation
                && g_audio_route_state.lease_identity == prior_route.lease_identity) {
                g_audio_route_state.deferred_native_handoff = deferred;
                g_audio_route_state.deferred_native_sound_identity = requested_sound_identity;
                g_audio_route_state.list_cleanup_pending = true;
                g_audio_route_state.native_clear_verified = clear_verified;
                if (clear_verified) {
                    clear_owned_audio_route_locked();
                }
            }
        }
        core::log(native_set_applied ? core::LogLevel::Info : core::LogLevel::Error,
            native_set_applied
                ? "[audio_sead] slot_set status=deferred_native_set_applied"
                : (clear_verified
                    ? "[audio_sead] slot_set status=deferred_native_set_pending_retry"
                    : (native_set_forwarded
                        ? "[audio_sead] slot_set status=deferred_native_clear_failed native_set_forwarded=1 cleanup_retained=1"
                        : "[audio_sead] slot_set status=deferred_native_clear_failed retained=1")));
    }
    if (invalidated_route && !transfer_stop_authorization
        && prior_route.custom_resource_owned && !sound) {
        const AudioNativeRouteObservation owned_observation{
            prior_route.owned_slot, prior_route.owned_bgm, prior_route.owned_sound,
            prior_route.owned_request_handle, 4, true,
            prior_route.generation, prior_route.lease_identity,
        };
        const AudioNativeRouteObservation cleared = observe_native_route(
            controller, controller_identity, set_generation, prior_route.lease_identity);
        const bool clear_verified = native_route_release_proven(
            owned_observation, cleared, set_generation);
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.controller == controller
                && g_audio_route_state.generation == set_generation
                && g_audio_route_state.lease_identity == prior_route.lease_identity) {
                g_audio_route_state.list_cleanup_pending = true;
                g_audio_route_state.native_clear_verified = clear_verified;
                if (clear_verified) {
                    clear_owned_audio_route_locked();
                }
            }
        }
        core::log(clear_verified ? core::LogLevel::Info : core::LogLevel::Error,
            clear_verified
                ? "[audio_sead] slot_set status=native_null_release_verified"
                : "[audio_sead] slot_set status=native_null_release_failed retained=1");
    }
    if (transfer_stop_authorization && !authorization_transferred) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_frozen_profile_lease.active()) {
                g_audio_route_state.list_cleanup_pending = true;
                (void)g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::NativeClearUnverified,
                    g_audio_route_state.lease_identity);
            } else {
                AudioRouteTransitionRecorder route_transition_record(
                    AudioRouteTransitionReason::SlotSetHandoffFailure,
                    AudioRouteTransitionKind::RouteReset);
                g_audio_route_state = {};
            }
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
        core::log(core::LogLevel::Error, "[audio_sead] slot_set status=postcondition_failed route=disabled");
    }
    if (invalidated_route) {
        static std::atomic_int s_logs{0};
        if (s_logs.fetch_add(1, std::memory_order_relaxed) < 64) {
            std::ostringstream out;
            out << "[audio_sead] slot_set status=route_invalidated"
                << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
                << " sound=0x" << reinterpret_cast<uintptr_t>(sound)
                << std::dec
                << " stop_authorization_transferred=" << (authorization_transferred ? 1 : 0);
            core::log(core::LogLevel::Info, out.str());
        }
    }
}

PrivateControllerSetupResult route_private_controller_play(
    void* controller, const PlaybackSnapshot& playback,
    const CleanupLease& cleanup, const SelectionSnapshot& selection)
{
    if (playback.song || cleanup.song || !controller) {
        return PrivateControllerSetupResult::NotEligible;
    }

    void* pre_slot = nullptr;
    void* pre_bgm = nullptr;
    void* pre_sound = nullptr;
    uint64_t pre_request = 0;
    uint8_t pre_state = 0xff;
    if (!read_controller_audio_chain(
            controller, pre_slot, pre_bgm, pre_sound, pre_request, pre_state)) {
        return PrivateControllerSetupResult::NotEligible;
    }
    UObjectIdentity pre_controller_identity;
    UObjectIdentity pre_sound_identity;
    const bool pre_identities_valid = read_uobject_identity(controller, pre_controller_identity)
        && pre_sound && read_uobject_identity(pre_sound, pre_sound_identity);
    const ControllerIdentityProof pre_controller_proof = pre_identities_valid
        ? controller_identity_proof(controller, pre_controller_identity)
        : ControllerIdentityProof{};

    UnpublishedAudioSetupContext setup;
    AudioRouteState route;
    bool private_candidate = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route = g_audio_route_state;
        private_candidate = g_unpublished_audio_setup
            && g_unpublished_audio_setup.controller_stage
                == PrivateControllerSetupStage::SetForwarded
            && token_matches_route(g_unpublished_audio_setup.token, route)
            && route.phase == AudioRoutePhase::Rebuilding
            && route.controller == controller;
        if (!private_candidate) return PrivateControllerSetupResult::NotEligible;
        setup = g_unpublished_audio_setup;
        const PrivateControllerSetupEligibility eligibility{
            true,
            true,
            !route.custom_resource_owned,
            route.stop_retirement.phase == AudioStopRetirementPhase::None,
            g_unpublished_audio_setup
                && selection.generation == g_unpublished_audio_setup.selection.generation
                && selection.song == g_unpublished_audio_setup.selection.song,
            g_unpublished_audio_setup
                && token_matches_route(g_unpublished_audio_setup.token, route)
                && route.phase == AudioRoutePhase::Rebuilding
                && route.controller == controller
                && g_unpublished_audio_setup.controller == controller
                && g_unpublished_audio_setup.slot == pre_slot
                && g_unpublished_audio_setup.bgm == pre_bgm
                && g_unpublished_audio_setup.sound == pre_sound,
            g_unpublished_audio_setup.controller_stage
                == PrivateControllerSetupStage::SetForwarded,
        };
        if (!private_controller_setup_eligible(eligibility)
            || !pre_identities_valid
            || !uobject_identity_matches(controller, route.controller_identity)
            || !uobject_identity_matches(pre_sound, route.private_setup_sound_identity)
            || !private_controller_play_forwardable(
                registry(), setup, setup.token, controller, pre_controller_proof,
                pre_slot, pre_bgm, pre_sound, pre_sound_identity.live,
                pre_request, pre_state)) {
            // Set may already own a custom native resource; do not forward an unproven Play.
        } else {
            private_candidate = false;
        }
    }
    if (private_candidate) {
        fail_private_controller_setup(
            route.lease_identity, setup.token, setup.sound, pre_request, pre_state,
            !pre_identities_valid ? "play_pre_identity_unreadable"
                : pre_request != setup.request_after_set ? "play_pre_request_mismatch"
                : pre_state != 2 ? "play_pre_state_not_prepared"
                : "play_precondition_or_selection_drift",
            true);
        return PrivateControllerSetupResult::Failed;
    }

    const bool forwarded = call_bgm_slot_play_original_seh(controller);
    log_private_controller_setup(
        "play_forwarded", setup.token, setup.sound, pre_request, pre_state,
        forwarded ? std::string_view{} : std::string_view{"play_exception"});

    void* post_slot = nullptr;
    void* post_bgm = nullptr;
    void* post_sound = nullptr;
    uint64_t post_request = 0;
    uint8_t post_state = 0xff;
    const bool observed = read_controller_audio_chain(
        controller, post_slot, post_bgm, post_sound, post_request, post_state);
    UObjectIdentity post_controller_identity;
    UObjectIdentity post_sound_identity;
    const bool post_identities_valid = observed
        && read_uobject_identity(controller, post_controller_identity)
        && post_sound && read_uobject_identity(post_sound, post_sound_identity);
    const ControllerIdentityProof post_controller_proof = post_identities_valid
        ? controller_identity_proof(controller, post_controller_identity)
        : ControllerIdentityProof{};

    bool claimed = false;
    CustomContextToken claim_token;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        AudioRouteState& current = g_audio_route_state;
        const bool exact_route = forwarded && observed && post_identities_valid
            && current.lease_identity == route.lease_identity
            && current.generation == setup.token.route_generation
            && current.phase == AudioRoutePhase::Rebuilding
            && current.desired_song_id == setup.selection.song->id
            && uobject_identity_matches(controller, current.controller_identity)
            && uobject_identity_matches(post_sound, current.private_setup_sound_identity)
            && controller_identity_proof_matches(
                setup.controller_proof, post_controller_proof)
            && private_object_handle_matches(
                setup.expected_sound_handle, post_sound_identity.live)
            && registry().selection_guard_matches(setup.selection, setup.token)
            && g_unpublished_audio_setup.controller_play_claimable(
                setup.token, controller, post_controller_proof,
                post_slot, post_bgm, post_sound, post_sound_identity.live,
                post_request, post_state);
        if (exact_route) {
            current.custom_resource_owned = true;
            current.phase = AudioRoutePhase::Playing;
            current.controller = controller;
            current.owned_slot = post_slot;
            current.owned_bgm = post_bgm;
            current.owned_sound = post_sound;
            current.owned_sound_identity = current.private_setup_sound_identity;
            current.owned_request_handle = post_request;
            current.sound = post_sound;
            current.list_cleanup_pending = true;
            ++current.generation;
            claim_token = custom_context_token(setup.token.registry_generation, current);
            claimed = g_unpublished_audio_setup.mark_controller_claimed(
                setup.token, claim_token);
        } else if (forwarded && observed
            && post_slot == setup.slot && post_bgm == setup.bgm
            && post_sound == setup.sound && post_request != 0) {
            current.custom_resource_owned = true;
            current.owned_slot = post_slot;
            current.owned_bgm = post_bgm;
            current.owned_sound = post_sound;
            (void)read_uobject_identity(post_sound, current.owned_sound_identity);
            current.owned_request_handle = post_request;
            current.list_cleanup_pending = true;
        }
    }

    if (!claimed || !promote_unpublished_audio_setup(
            route.lease_identity, AudioArmPublicationProof::ControllerRebuildClaimed)) {
        fail_private_controller_setup(
            route.lease_identity, setup.token, setup.sound, post_request, post_state,
            !forwarded ? "play_exception" : !observed ? "play_observation_failed"
                : !post_identities_valid ? "play_post_identity_unreadable"
                : post_request != setup.request_after_set ? "play_request_handle_changed"
                : post_state != 4 ? "slot_state_not_playing"
                : "play_claim_mismatch",
            true);
        return PrivateControllerSetupResult::Failed;
    }

    const bool overlay_refreshed = refresh_active_scoreinfo_overlay_profile();
    log_private_controller_setup("claimed", claim_token, post_sound, post_request, post_state);
    core::log(core::LogLevel::Info,
        std::string("[audio_sead] private_controller_setup metadata_refresh scoreinfo=")
            + (overlay_refreshed ? "refreshed" : "unavailable")
            + " duration=playback_snapshot_committed title=resolver_requires_native_refresh");
    return PrivateControllerSetupResult::Claimed;
}

PauseResumeDetourResult try_pause_resume_bank_play(
    void* controller, PauseResumeBankMarker& play_marker,
    PauseResumeBankMarker& restore_marker)
{
    PauseResumeBankSession session;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_pause_resume_bank.phase != PauseResumeBankPhase::OwnerRebound) {
            return {};
        }
        session = g_pause_resume_bank;
    }
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint64_t request = 0;
    uint8_t state = 0;
    uint64_t owner = 0;
    UObjectIdentity sound_identity;
    const bool chain = read_controller_audio_chain(
        controller, slot, bgm, sound, request, state);
    const bool identity = chain && sound
        && read_uobject_identity(sound, sound_identity);
    const PauseResumeBankPlayFacts facts{
        g_piano_audio_owner_tick
            && g_piano_audio_owner_tick == session.owner_tick,
        g_piano_audio_owner_tick_nonce != 0
            && g_piano_audio_owner_tick_nonce == session.owner_tick_nonce,
        audio_production_play_setup_tls().original_depth == 0,
        controller == session.controller
            && uobject_identity_matches(controller, session.controller_identity),
        identity && sound == session.detached.sound.object
            && sound_identity.live.internal_index
                == session.detached.sound.live.internal_index
            && sound_identity.live.serial_number
                == session.detached.sound.live.serial_number,
        chain && request == session.request_handle,
        chain && state == 2,
        identity && core::safe_read_field(sound,
            runtime_layouts::SqexSeadSound::observed_field548, owner)
            && owner == session.detached.custom.encode(),
    };
    if (!pause_resume_bank_play_eligible(facts)) {
        const bool restored = restore_pause_resume_owner(&restore_marker);
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
        }
        play_marker = {PauseResumeBankMarkerStatus::Failed, "play_prefilter",
            session.session_epoch, session.cycle_epoch, true};
        return {restored
                ? PauseResumeDetourDisposition::ForwardAfterVerifiedCanonical
                : PauseResumeDetourDisposition::SuppressFailClosed,
            0, restored};
    }

    bool native_forwarded = false;
    void* post_slot = nullptr;
    void* post_bgm = nullptr;
    void* post_sound = nullptr;
    uint64_t post_request = 0;
    uint8_t post_state = 0;
    void* post_backing = nullptr;
    bool backing_observed = false;
    const auto result = coordinate_pause_resume_native_forward(
        [&]() {
            native_forwarded = true;
            return call_bgm_slot_play_original_seh(controller);
        },
        [&]() {
            UObjectIdentity post_identity;
            const bool post_chain = read_controller_audio_chain(controller,
                post_slot, post_bgm, post_sound, post_request, post_state);
            backing_observed = post_chain && core::safe_read_field(post_bgm,
                runtime_layouts::SqexSeadBgm::backing_resource, post_backing);
            return post_chain && post_slot == session.slot
                && post_bgm == session.bgm
                && post_sound == session.detached.sound.object
                && post_request == session.request_handle
                && post_state == 4 && backing_observed
                && read_uobject_identity(post_sound, post_identity)
                && post_identity.live.internal_index
                    == session.detached.sound.live.internal_index
                && post_identity.live.serial_number
                    == session.detached.sound.live.serial_number;
        },
        [&]() { return restore_pause_resume_owner(&restore_marker); },
        true);
    const auto disposition = pause_resume_forward_disposition(
        result, native_forwarded ? 1 : 0, true);
    if (result == PauseResumeNativeForwardResult::Succeeded) {
        bool rebound = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (pause_resume_epoch_matches(session.session_epoch, session.cycle_epoch,
                    g_pause_resume_bank.session_epoch,
                    g_pause_resume_bank.cycle_epoch)
                && g_pause_resume_bank.phase == PauseResumeBankPhase::ResumedActive
                && g_onmemory_bank_lifecycle.rebind_detached_request(
                    session.detached, session.lifecycle_state_epoch,
                    post_request, post_backing, backing_observed)) {
                g_pause_resume_bank.detached = g_onmemory_bank_lifecycle.active();
                g_pause_resume_bank.lifecycle_state_epoch =
                    g_onmemory_bank_lifecycle.state_epoch();
                g_pause_resume_bank.slot = post_slot;
                g_pause_resume_bank.bgm = post_bgm;
                g_pause_resume_bank.request_handle = post_request;
                g_pause_resume_bank.backing = post_backing;
                g_pause_resume_bank.backing_observed = backing_observed;
                rebound = true;
            } else {
                g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
            }
        }
        if (rebound) {
            play_marker = {PauseResumeBankMarkerStatus::PlayResumed,
                "exact_play", session.session_epoch, session.cycle_epoch, true};
            return disposition;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
    }
    play_marker = {PauseResumeBankMarkerStatus::Failed,
        result == PauseResumeNativeForwardResult::NativeFailedRestored
            ? "play_native_fault" : "play_postcondition_or_restore",
        session.session_epoch, session.cycle_epoch, true};
    return disposition;
}

struct CanonicalSubstrateRequestRebaseMarker final {
    bool eligible = false;
    uint64_t old_request = 0;
    uint64_t new_request = 0;
    uint64_t transaction_generation = 0;
    uint64_t source_ordinal = 0;
    UObjectLiveHandle sound_identity{};
    unsigned reset_lineage_phase = 0;
    bool custom_release_required = false;
    unsigned lifecycle_phase = 0;
};

bool try_rebase_ready_canonical_substrate_request(
    void* controller, CanonicalSubstrateRequestRebaseMarker& marker) noexcept;

void __fastcall bgm_slot_play_detour(void* controller)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::Play);
    PauseResumeBankMarker pause_play_marker;
    PauseResumeBankMarker pause_restore_marker;
    CanonicalSubstrateRequestRebaseMarker substrate_rebase_marker;
    auto deferred_pause_markers = make_deferred_noexcept_action([&]() noexcept {
        emit_or_batch_pause_resume_bank_marker(pause_play_marker);
        emit_or_batch_pause_resume_bank_marker(pause_restore_marker);
        if (substrate_rebase_marker.eligible) {
            static std::atomic_uint32_t s_rebase_logs{0};
            if (s_rebase_logs.fetch_add(1, std::memory_order_relaxed) < 32) {
                std::ostringstream marker;
                marker
                    << "[audio_sead] canonical_substrate_request_rebase"
                    << " status=committed"
                    << " old_request=0x" << std::hex
                    << substrate_rebase_marker.old_request
                    << " new_request=0x"
                    << substrate_rebase_marker.new_request << std::dec
                    << " transaction_generation="
                    << substrate_rebase_marker.transaction_generation
                    << " source_ordinal="
                    << substrate_rebase_marker.source_ordinal
                    << " sound_index="
                    << substrate_rebase_marker.sound_identity.internal_index
                    << " sound_serial="
                    << substrate_rebase_marker.sound_identity.serial_number
                    << " state=4 mutation_authorized=1 custom_token=0"
                    << " canonical_token_preserved=1"
                    // Separates the two accounts of a stale proof: a commit
                    // taken while a custom release is still outstanding shows
                    // the proof's subject was already invalidated, while a
                    // clean commit shows the lineage fields advanced with no
                    // authority established behind them.
                    << " reset_lineage_phase="
                    << substrate_rebase_marker.reset_lineage_phase
                    << " custom_release_required="
                    << (substrate_rebase_marker.custom_release_required ? 1 : 0)
                    << " lifecycle_phase="
                    << substrate_rebase_marker.lifecycle_phase;
                core::log(core::LogLevel::Info, marker.str());
            }
        }
    });
    std::lock_guard<std::recursive_mutex> operation_lock(g_audio_route_operations.mutex());
    if (bypass_audio_native_detour(g_native_play_setup_replay_depth)) {
        call_bgm_slot_play_original(controller);
        return;
    }
    bool cleanup_only_native_play_passthrough = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const auto& setup = g_unpublished_audio_setup;
        const auto& bridge = setup.substrate_bridge;
        cleanup_only_native_play_passthrough = audio_production_play_setup_tls().original_depth != 0
            && setup
            && bridge.generation != 0
            && bridge.phase == CanonicalSubstrateBridgePhase::Failed
            && g_audio_route_state.phase == AudioRoutePhase::PatchedPlaySetup
            && bridge.route_generation != 0
            && bridge.route_generation == setup.token.route_generation
            && bridge.route_generation == g_audio_route_state.generation
            && bridge.lease.generation == setup.token.lease_generation
            && bridge.lease.song_key == setup.token.song_key
            && bridge.song_key == setup.token.song_key;
    }
    if (bgm_aggregate_lineage_exit_pending(controller)) {
        log_bgm_aggregate_mutation("exit_native_forward", "play", 0, 0, 0);
        call_bgm_slot_play_original(controller);
        return;
    }
    if (bgm_aggregate_lineage_active(controller)) {
        // Aggregate lineage never suppresses or replays native Play.
        call_bgm_slot_play_original(controller);
        return;
    }
    if (callback_scope) {
        const auto pause_result = try_pause_resume_bank_play(
            controller, pause_play_marker, pause_restore_marker);
        if (pause_play_marker.eligible || pause_restore_marker.eligible) {
            deferred_pause_markers.make_eligible();
        }
        if (dispatch_pause_resume_detour(pause_result, [&]() {
                return call_bgm_slot_play_original_seh(controller);
            })) {
            return;
        }
    }
    AudioRouteState deferred_route;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (controller
            && g_audio_route_state.deferred_native_handoff.controller == controller
            && g_audio_route_state.deferred_native_handoff.active()) {
            deferred_route = g_audio_route_state;
        }
    }
    if (deferred_route.deferred_native_handoff.active()) {
        AudioDeferredNativeHandoffState deferred = deferred_route.deferred_native_handoff;
        const auto observe_deferred = [&] {
            return observe_native_route(
                controller,
                deferred_route.controller_identity,
                deferred.route_generation,
                deferred.lease_identity);
        };
        bool set_replayed = false;
        bool play_forwarded = false;
        bool recovery_play_forwarded = false;
        bool postcondition_verified = false;
        if (deferred.phase == AudioDeferredNativeHandoffPhase::CustomCleared) {
            const AudioNativeRouteObservation cleared = observe_deferred();
            if (evaluate_deferred_native_handoff_action(deferred, cleared)
                    == AudioDeferredNativeHandoffAction::ApplyNativeSet
                && uobject_identity_matches(
                    deferred.requested_sound,
                    deferred_route.deferred_native_sound_identity)) {
                call_bgm_slot_set_original(controller, deferred.requested_sound);
                set_replayed = record_deferred_native_set(deferred, observe_deferred());
            }
        }
        if (deferred.phase == AudioDeferredNativeHandoffPhase::NativeSetApplied) {
            const AudioNativeRouteObservation before_play = observe_deferred();
            log_deferred_route_diagnostics(
                "before_native_play", controller, deferred_route.controller_identity,
                before_play, deferred);
            log_frozen_sound_patch_snapshot(
                "before_native_play", deferred_route.frozen_sound_patch);
            if (uobject_identity_matches(
                    deferred.requested_sound,
                    deferred_route.deferred_native_sound_identity)
                && evaluate_deferred_native_handoff_action(deferred, before_play)
                    == AudioDeferredNativeHandoffAction::ForwardNativePlay) {
                log_sound_route_fields(
                    "requested_before_native_play", deferred.requested_sound,
                    deferred_route.deferred_native_sound_identity);
                call_bgm_slot_play_original(controller);
                play_forwarded = true;
                const AudioNativeRouteObservation after_play = observe_deferred();
                log_deferred_route_diagnostics(
                    "after_native_play", controller, deferred_route.controller_identity,
                    after_play, deferred);
                log_frozen_sound_patch_snapshot(
                    "after_native_play", deferred_route.frozen_sound_patch);
                log_sound_route_fields(
                    "requested_after_native_play", deferred.requested_sound,
                    deferred_route.deferred_native_sound_identity);
                postcondition_verified = record_deferred_native_play(deferred, after_play);
                if (!postcondition_verified) {
                    deferred.phase = AudioDeferredNativeHandoffPhase::RetainedFailure;
                }
            }
        } else if (deferred.phase == AudioDeferredNativeHandoffPhase::RetainedFailure) {
            const AudioNativeRouteObservation before_play = observe_deferred();
            log_deferred_route_diagnostics(
                "before_recovery_play", controller, deferred_route.controller_identity,
                before_play, deferred);
            log_frozen_sound_patch_snapshot(
                "before_recovery_play", deferred_route.frozen_sound_patch);
            if (uobject_identity_matches(
                    deferred.requested_sound,
                    deferred_route.deferred_native_sound_identity)
                && evaluate_deferred_native_handoff_action(deferred, before_play)
                    == AudioDeferredNativeHandoffAction::ForwardNativePlayRetainingFailure) {
                log_sound_route_fields(
                    "requested_before_recovery_play", deferred.requested_sound,
                    deferred_route.deferred_native_sound_identity);
                call_bgm_slot_play_original(controller);
                play_forwarded = true;
                recovery_play_forwarded = true;
                const AudioNativeRouteObservation after_play = observe_deferred();
                log_deferred_route_diagnostics(
                    "after_recovery_play", controller, deferred_route.controller_identity,
                    after_play, deferred);
                log_frozen_sound_patch_snapshot(
                    "after_recovery_play", deferred_route.frozen_sound_patch);
                log_sound_route_fields(
                    "requested_after_recovery_play", deferred.requested_sound,
                    deferred_route.deferred_native_sound_identity);
            }
        } else if (deferred.phase
            == AudioDeferredNativeHandoffPhase::NativePlayForwarded) {
            const AudioNativeRouteObservation current = observe_deferred();
            const bool exact_native_route = current.valid
                && current.slot == deferred.slot
                && current.bgm == deferred.bgm
                && current.sound == deferred.requested_sound
                && current.request_handle == deferred.native_request_handle
                && current.request_handle
                && current.state == 4;
            if (exact_native_route) {
                call_bgm_slot_play_original(controller);
                play_forwarded = true;
                postcondition_verified = true;
            }
        }
        // Record the forwarding on every branch that reached the native Play,
        // not only the one that could also verify a postcondition.  The
        // retained-failure recovery forwards the native's own Play and then has
        // no later phase to advance into, so without this the record would stay
        // in `RetainedFailure` for the rest of the session and misreport a
        // discharged obligation as permanently outstanding.
        if (play_forwarded) {
            record_deferred_native_play_forwarded(deferred);
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.controller == controller
                && g_audio_route_state.generation == deferred.route_generation
                && g_audio_route_state.lease_identity == deferred.lease_identity
                && g_audio_route_state.deferred_native_handoff.requested_sound
                    == deferred.requested_sound) {
                g_audio_route_state.deferred_native_handoff = deferred;
                if (postcondition_verified) {
                    g_audio_route_state.native_clear_verified = true;
                }
            }
        }
        const char* status = recovery_play_forwarded
            ? "deferred_native_recovery_play_forwarded_cleanup_retained"
            : (postcondition_verified
            ? "deferred_native_play_forwarded"
            : (play_forwarded
                ? "deferred_native_play_forwarded_postcondition_unverified"
                : (set_replayed
                    ? "deferred_native_set_replayed_play_not_ready"
                    : "deferred_native_handoff_retained")));
        core::log(postcondition_verified ? core::LogLevel::Info : core::LogLevel::Error,
            std::string("[audio_sead] slot_play status=") + status);
        return;
    }
    bool unsequenced_owned_route = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        unsequenced_owned_route = controller
            && g_audio_route_state.controller == controller
            && g_audio_route_state.custom_resource_owned
            && audio_production_play_setup_tls().original_depth == 0
            && !g_audio_route_state.set_play_handoff_pending;
    }
    if (unsequenced_owned_route) {
        core::log(core::LogLevel::Error,
            "[audio_sead] slot_play status=custom_route_requires_native_set");
        return;
    }
    if (cleanup_only_native_play_passthrough
        || g_audio_route_disabled.load(std::memory_order_acquire)) {
        if (!cleanup_only_native_play_passthrough) {
            clear_any_unpublished_audio_setup(
                AudioRouteTransitionReason::FeatureDisabled);
            const PlaybackSnapshot playback = registry().playback_snapshot();
            if (playback.song) (void)revoke_playback_snapshot(playback);
        }
        // A failed owner-zero bridge may already have caused native PlaySetup
        // to materialize a new custom token. Preserve that exact setup for
        // post-original cleanup-only adoption, but never claim playback.
        call_bgm_slot_play_original(controller);
        return;
    }
    const PrivateControllerSetupResult private_setup_result = route_private_controller_play(
        controller, registry().playback_snapshot(),
        registry().cleanup_lease(), registry().selection_snapshot());
    if (private_setup_result != PrivateControllerSetupResult::NotEligible) {
        return;
    }
    std::string desired_song;
    std::string patched_song;
    void* patched_sound = nullptr;
    PendingSoundRoute pending_route = PendingSoundRoute::None;
    pending_route = consume_audio_route_arm_for_pending_sound(controller, desired_song, patched_song, patched_sound);
    if (pending_route != PendingSoundRoute::None) {
        static std::atomic_int s_mismatch_logs{0};
        if (s_mismatch_logs.fetch_add(1, std::memory_order_relaxed) < 64) {
            std::ostringstream out;
            const char* status = pending_route == PendingSoundRoute::RequiresPlaySetup
                ? "blocked_requires_playsetup"
                : (pending_route == PendingSoundRoute::SongMismatch
                ? "blocked_cross_song_pending_sound"
                : (pending_route == PendingSoundRoute::UnsafeStoppedPatch
                    ? "blocked_stopped_pending_sound"
                    : "blocked_same_song_requires_playsetup"));
            out << "[audio_sead] slot_play status=" << status
                << " desired_song_id=" << desired_song
                << " patched_song_id=" << patched_song
                << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
                << " sound=0x" << reinterpret_cast<uintptr_t>(patched_sound)
                << std::dec;
            core::log(core::LogLevel::Error, out.str());
        }
        return;
    }
    bool routed_play = false;
    void* routed_sound = nullptr;
    UObjectIdentity controller_identity;
    const bool controller_identity_valid = read_uobject_identity(controller, controller_identity);
    if (audio_production_play_setup_tls().original_depth != 0
        && !audio_production_play_setup_tls().play_claimed) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        routed_play = g_audio_route_state.generation
                == audio_production_play_setup_tls().expected_generation
            && g_audio_route_state.phase == AudioRoutePhase::PatchedPlaySetup
            && !g_pending_play_setup_patch.patches.empty()
            && g_audio_route_state.sound == g_pending_play_setup_patch.sound;
        if (routed_play) {
            g_audio_route_state.phase = AudioRoutePhase::Playing;
            g_audio_route_state.desired_song_id = g_pending_play_setup_patch.song_id;
            g_audio_route_state.patched_song_id = g_pending_play_setup_patch.song_id;
            g_audio_route_state.sound = g_pending_play_setup_patch.sound;
            routed_sound = g_pending_play_setup_patch.sound;
            g_audio_route_state.controller = controller_identity_valid ? controller : nullptr;
            g_audio_route_state.controller_identity = controller_identity_valid ? controller_identity : UObjectIdentity{};
            g_audio_route_state.stop_observed = false;
            g_audio_route_state.stop_authorized_generation = 0;
            g_audio_route_state.set_play_handoff_pending = false;
        }
    }
    bool ordinary_route_invalidated = false;
    AudioRouteState play_prior_route;
    bool transfer_set_authorization = false;
    bool had_stop_authorization = false;
    uint64_t generation_before_play = 0;
    uint64_t generation_after_play = 0;
    uint64_t authorized_generation = 0;
    UObjectIdentity handoff_controller_identity;
    void* expected_handoff_slot = nullptr;
    void* expected_handoff_bgm = nullptr;
    void* expected_handoff_sound = nullptr;
    UObjectIdentity expected_handoff_sound_identity;
    uint64_t expected_handoff_request_handle = 0;
    if (!routed_play && controller) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_audio_route_state.controller == controller) {
            play_prior_route = g_audio_route_state;
            transfer_set_authorization = g_audio_route_state.phase == AudioRoutePhase::Idle
                && g_audio_route_state.stop_observed
                && g_audio_route_state.set_play_handoff_pending
                && g_audio_route_state.stop_authorized_generation == g_audio_route_state.generation + 1;
            handoff_controller_identity = g_audio_route_state.controller_identity;
            expected_handoff_slot = g_audio_route_state.handoff_slot;
            expected_handoff_bgm = g_audio_route_state.handoff_bgm;
            expected_handoff_sound = g_audio_route_state.handoff_sound;
            expected_handoff_sound_identity = g_audio_route_state.handoff_sound_identity;
            expected_handoff_request_handle = g_audio_route_state.handoff_request_handle;
            ordinary_route_invalidated = true;
            generation_before_play = g_audio_route_state.generation;
            authorized_generation = g_audio_route_state.stop_authorized_generation;
            had_stop_authorization = g_audio_route_state.stop_observed;
            ++g_audio_route_state.generation;
            generation_after_play = g_audio_route_state.generation;
            g_audio_route_state.stop_observed = false;
            g_audio_route_state.stop_authorized_generation = 0;
            g_audio_route_state.set_play_handoff_pending = false;
        }
    }
    if (ordinary_route_invalidated) {
        (void)revoke_registry_playback(play_prior_route);
    }
    void* routed_pre_slot = nullptr;
    void* routed_pre_bgm = nullptr;
    void* routed_pre_sound = nullptr;
    uint64_t routed_pre_request_handle = 0;
    uint8_t routed_pre_state = 0;
    UObjectIdentity routed_pre_sound_identity;
    ControllerIdentityProof routed_pre_controller_proof;
    if (routed_play && controller_identity_valid) {
        (void)read_controller_identity_proof(controller, routed_pre_controller_proof);
    }
    if (routed_play) {
        const bool routed_pre_valid = controller_identity_valid
            && lookup_current_bgm_controller() == controller
            && read_uobject_identity(routed_sound, routed_pre_sound_identity)
            && read_controller_bgm_chain(controller, routed_pre_slot, routed_pre_bgm)
            && core::safe_read_field(routed_pre_bgm, runtime_layouts::SqexSeadBgm::sound, routed_pre_sound)
            && core::safe_read_field(routed_pre_bgm, runtime_layouts::SqexSeadBgm::request_handle, routed_pre_request_handle)
            && core::safe_read_field(routed_pre_slot, runtime_layouts::SqexSeadSlot::state, routed_pre_state)
            && routed_pre_sound == routed_sound
            && routed_pre_request_handle != 0
            && (routed_pre_state == 2 || routed_pre_state == 4);
        if (!routed_pre_valid) {
            core::log(core::LogLevel::Error, "[audio_sead] slot_play status=blocked_routed_precondition");
            return;
        }
    }
    const bool handoff_transfer_expected = transfer_set_authorization;
    if (transfer_set_authorization) {
        void* handoff_sound = nullptr;
        uint64_t handoff_request_handle = 0;
        uint8_t handoff_state = 0;
        const bool handoff_pre_valid = lookup_current_bgm_controller() == controller
            && uobject_identity_matches(controller, handoff_controller_identity)
            && uobject_identity_matches(expected_handoff_sound, expected_handoff_sound_identity)
            && read_controller_bgm_chain(controller, routed_pre_slot, routed_pre_bgm)
            && core::safe_read_field(routed_pre_bgm, runtime_layouts::SqexSeadBgm::sound, handoff_sound)
            && core::safe_read_field(routed_pre_bgm, runtime_layouts::SqexSeadBgm::request_handle, handoff_request_handle)
            && core::safe_read_field(routed_pre_slot, runtime_layouts::SqexSeadSlot::state, handoff_state)
            && routed_pre_slot == expected_handoff_slot
            && routed_pre_bgm == expected_handoff_bgm
            && handoff_sound == expected_handoff_sound
            && handoff_request_handle == expected_handoff_request_handle
            && handoff_request_handle != 0
            && (handoff_state == 2 || handoff_state == 4);
        if (!handoff_pre_valid) {
            transfer_set_authorization = false;
        }
    }
    call_bgm_slot_play_original(controller);
    if (try_rebase_ready_canonical_substrate_request(
            controller, substrate_rebase_marker)) {
        deferred_pause_markers.make_eligible();
    }
    bool routed_ownership_committed = false;
    if (routed_play && controller_identity_valid && routed_sound) {
        void* routed_slot = nullptr;
        void* routed_bgm = nullptr;
        void* current_sound = nullptr;
        uint64_t routed_request_handle = 0;
        uint8_t routed_slot_state = 0;
        UObjectIdentity routed_sound_identity;
        ControllerIdentityProof routed_post_controller_proof;
        (void)read_controller_identity_proof(controller, routed_post_controller_proof);
        const bool owned_request_handle_valid = lookup_current_bgm_controller() == controller
            && uobject_identity_matches(controller, controller_identity)
            && read_controller_bgm_chain(controller, routed_slot, routed_bgm)
            && core::safe_read_field(routed_bgm, runtime_layouts::SqexSeadBgm::sound, current_sound)
            && core::safe_read_field(routed_bgm, runtime_layouts::SqexSeadBgm::request_handle, routed_request_handle)
            && core::safe_read_field(routed_slot, runtime_layouts::SqexSeadSlot::state, routed_slot_state)
            && routed_slot == routed_pre_slot
            && routed_bgm == routed_pre_bgm
            && current_sound == routed_sound
            && routed_request_handle != 0
            && routed_slot_state == 4
            && read_uobject_identity(routed_sound, routed_sound_identity);
        if (owned_request_handle_valid) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.generation
                    == audio_production_play_setup_tls().expected_generation
                && g_audio_route_state.phase == AudioRoutePhase::Playing
                && g_audio_route_state.controller == controller) {
                g_audio_route_state.custom_resource_owned = true;
                g_audio_route_state.owned_slot = routed_slot;
                g_audio_route_state.owned_bgm = routed_bgm;
                g_audio_route_state.owned_sound = routed_sound;
                g_audio_route_state.owned_sound_identity = routed_sound_identity;
                g_audio_route_state.owned_request_handle = routed_request_handle;
                g_audio_route_state.reusable_sound = routed_sound;
                g_audio_route_state.reusable_sound_identity = routed_sound_identity;
                g_audio_route_state.reusable_slot = routed_slot;
                g_audio_route_state.reusable_bgm = routed_bgm;
                g_audio_route_state.list_cleanup_pending = true;
                const CustomContextToken private_expected =
                    g_unpublished_audio_setup.token;
                const bool private_route_exact = g_unpublished_audio_setup
                    && g_unpublished_audio_setup.controller_stage
                        == PrivateControllerSetupStage::SetForwarded
                    && token_matches_route(private_expected, g_audio_route_state)
                    && g_audio_route_state.desired_song_id
                        == g_unpublished_audio_setup.selection.song->id
                    && g_unpublished_audio_setup.controller == controller
                    && g_unpublished_audio_setup.slot == routed_pre_slot
                    && g_unpublished_audio_setup.bgm == routed_pre_bgm
                    && g_unpublished_audio_setup.sound == routed_sound;
                if (private_route_exact) {
                    const CustomContextToken private_claim = custom_context_token(
                        private_expected.registry_generation, g_audio_route_state);
                    (void)claim_private_controller_play_after_forward(
                        registry(), g_unpublished_audio_setup, private_expected,
                        controller, routed_pre_controller_proof,
                        routed_pre_slot, routed_pre_bgm, routed_pre_sound,
                        routed_pre_sound_identity.live,
                        routed_pre_request_handle, routed_pre_state,
                        routed_post_controller_proof,
                        routed_slot, routed_bgm, routed_sound,
                        routed_sound_identity.live,
                        routed_request_handle, routed_slot_state,
                        private_claim);
                }
                audio_production_claim_play_setup_play();
                routed_ownership_committed = true;
            }
        }
    }
    bool play_authorization_transferred = false;
    if (transfer_set_authorization
        && lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, handoff_controller_identity)
        && uobject_identity_matches(expected_handoff_sound, expected_handoff_sound_identity)) {
        void* slot = nullptr;
        void* bgm = nullptr;
        void* sound = nullptr;
        uint64_t request_handle = 0;
        uint8_t slot_state = 0;
        const bool native_play_applied = read_controller_bgm_chain(controller, slot, bgm)
            && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, sound)
            && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::request_handle, request_handle)
            && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, slot_state)
            && slot == expected_handoff_slot
            && bgm == expected_handoff_bgm
            && sound == expected_handoff_sound
            && request_handle == expected_handoff_request_handle
            && request_handle != 0
            && slot_state == 4;
        if (native_play_applied) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_audio_route_state.controller == controller
                && g_audio_route_state.generation == generation_after_play
                && g_audio_route_state.phase == AudioRoutePhase::Idle) {
                // Complete the single observed Stop -> Set -> Play transition.
                // Any further Set/Play invalidates this next-arm authorization.
                g_audio_route_state.stop_observed = true;
                g_audio_route_state.stop_authorized_generation = generation_after_play + 1;
                g_audio_route_state.set_play_handoff_pending = false;
                play_authorization_transferred = true;
            }
        }
    }
    if (routed_play && !routed_ownership_committed) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (!g_pending_play_setup_patch.patches.empty()) {
                g_pending_play_setup_patch.restore_safe = false;
            }
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::NativeClearUnverified,
                g_audio_route_state.lease_identity);
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
        core::log(core::LogLevel::Error, "[audio_sead] slot_play status=routed_postcondition_failed route=disabled");
    } else if (handoff_transfer_expected && !play_authorization_transferred) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_frozen_profile_lease.active()) {
                g_audio_route_state.list_cleanup_pending = true;
                (void)g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::NativeClearUnverified,
                    g_audio_route_state.lease_identity);
            } else {
                AudioRouteTransitionRecorder route_transition_record(
                    AudioRouteTransitionReason::SlotPlayHandoffFailure,
                    AudioRouteTransitionKind::RouteReset);
                g_audio_route_state = {};
            }
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
        core::log(core::LogLevel::Error, "[audio_sead] slot_play status=handoff_postcondition_failed route=disabled");
    }
    if (ordinary_route_invalidated) {
        static std::atomic_int s_invalidation_logs{0};
        if (s_invalidation_logs.fetch_add(1, std::memory_order_relaxed) < 64) {
            std::ostringstream out;
            out << "[audio_sead] slot_play status=route_invalidated"
                << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
                << std::dec
                << " generation_before=" << generation_before_play
                << " generation_after=" << generation_after_play
                << " stop_observed=" << (had_stop_authorization ? 1 : 0)
                << " authorized_generation=" << authorized_generation
                << " stop_authorization_transferred=" << (play_authorization_transferred ? 1 : 0);
            core::log(core::LogLevel::Info, out.str());
        }
    }
    PendingPlaySetupPatch pending_snapshot;
    AudioRoutePhase route_phase = AudioRoutePhase::Idle;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        pending_snapshot.sound = g_pending_play_setup_patch.sound;
        pending_snapshot.song_id = g_pending_play_setup_patch.song_id;
        pending_snapshot.patches = g_pending_play_setup_patch.patches;
        if (routed_play
            && g_audio_route_state.generation
                == audio_production_play_setup_tls().expected_generation
            && !pending_snapshot.patches.empty()) {
            // State was associated before original Play so a reentrant Stop can guard its lifetime.
        } else if (routed_play) {
            routed_play = false;
        }
        route_phase = g_audio_route_state.phase;
    }
    if (routed_play && !pending_snapshot.patches.empty()) {
        static std::atomic_int s_logs{0};
        if (s_logs.fetch_add(1, std::memory_order_relaxed) < 64) {
            std::ostringstream out;
            out << "[audio_sead] routed_play status=original_called"
                << " restore=on_playsetup_return"
                << " song_id=" << pending_snapshot.song_id
                << " sound=0x" << std::hex << reinterpret_cast<uintptr_t>(pending_snapshot.sound)
                << std::dec
                << " route_phase=" << audio_route_phase_name(route_phase)
                << " patch_count=" << pending_snapshot.patches.size();
            core::log(core::LogLevel::Info, out.str());
        }
    }
}

void __fastcall bgm_slot_stop_detour(void* controller)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::Stop);
    if (!callback_scope) {
        (void)invoke_audio_production_stop(controller);
        return;
    }
    PrivateControllerStopDiagnosticRecord private_stop_diagnostic_record;
    ArmedStopReadinessDiagnostic armed_stop_readiness_diagnostic;
    BgmPlaybackNativeStopReturnDiagnostic native_stop_return_diagnostic;
    BgmPlaybackAggregateTerminalStopDiagnostic terminal_handoff_stop;
    auto deferred_private_stop_diagnostic = make_deferred_noexcept_action([&]() noexcept {
        log_private_controller_stop_diagnostic(private_stop_diagnostic_record);
        log_armed_stop_readiness_diagnostic(armed_stop_readiness_diagnostic);
        if (armed_stop_readiness_diagnostic.chart_transaction_associated
            && armed_stop_readiness_diagnostic.chart_transaction_generation != 0
            && armed_stop_readiness_diagnostic.private_setup_failed) {
            ChartAudioDiagnosticTransaction transaction;
            transaction.active = true;
            transaction.generation
                = armed_stop_readiness_diagnostic.chart_transaction_generation;
            finish_chart_audio_diagnostic_transaction(
                transaction, ChartAudioDiagnosticTerminalOutcome::StopFailed);
        }
    });
    auto deferred_native_stop_return = make_deferred_noexcept_action([&]() noexcept {
        log_bgm_playback_native_stop_return(native_stop_return_diagnostic);
    });
    PauseResumeBankMarker retirement_mismatch_marker;
    auto deferred_retirement_mismatch = make_deferred_noexcept_action([&]() noexcept {
        emit_or_batch_pause_resume_bank_marker(retirement_mismatch_marker);
    });
    auto deferred_terminal_handoff_stop = make_deferred_noexcept_action([&]() noexcept {
        if (!terminal_handoff_stop.active) return;
        log_bgm_aggregate_terminal_handoff(
            "[audio_aggregate_terminal_handoff_stop]",
            terminal_handoff_stop.facts,
            terminal_handoff_stop.pre_classification);
        if (terminal_handoff_stop.retirement_diagnostic) {
            log_onmemory_bank_pair(
                "post_request_retirement",
                terminal_handoff_stop.retirement_diagnostic);
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            (void)g_onmemory_bank_diagnostic_pair.erase_exact(
                terminal_handoff_stop.retirement_diagnostic);
        }
        const OnMemoryBankReleaseAction release_action =
            terminal_handoff_stop.retirement_release;
        if (!release_action) return;
        if (bgm_aggregate_release_has_borrowers(release_action)) {
            observe_bgm_playback_release(release_action, false);
            defer_bgm_aggregate_release(release_action);
            observe_bgm_playback_aggregate(false, false);
            return;
        }
        observe_bgm_playback_release(release_action, false);
        observe_bgm_playback_aggregate(false, false);
        const auto execution = execute_onmemory_bank_release(
            g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
            g_onmemory_bank_release_available.load(std::memory_order_acquire),
            !g_audio_route_installed.load(std::memory_order_acquire)
                || g_audio_route_disabled.load(std::memory_order_acquire),
            release_action,
            [](const uint64_t token) noexcept {
                return lookup_onmemory_bank_kind_noexcept(token);
            },
            [](const uint64_t* token, const uint8_t asynchronous) noexcept {
                return release_onmemory_bank_async_noexcept(token, asynchronous);
            });
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        (void)g_onmemory_bank_lifecycle.finish_release(
            release_action, execution.outcome);
    });
    std::lock_guard<std::recursive_mutex> operation_lock(g_audio_route_operations.mutex());
    AudioRouteState snapshot;
    size_t patch_count = 0;
    bool controller_matches_route = false;
    bool pending_lifetime_guarded = false;
    bool stop_evidence_committed = false;
    bool exact_owned_chain_before_stop = false;
    AudioNaturalCompletionRetirementFacts natural_completion_facts;
    OnMemoryBankDetachedRecord natural_completion_detached;
    uint64_t natural_completion_lifecycle_epoch = 0;
    uint64_t natural_completion_counter_snapshot = 0;
    uint64_t natural_completion_monitor_epoch = 0;
    UnpublishedAudioSetupContext private_setup_before_stop;
    ControllerIdentityProof armed_stop_bound_proof;
    bool private_stop_candidate = false;
    const PlaybackSnapshot playback_before_stop = registry().playback_snapshot();
    const CleanupLease cleanup_before_stop = registry().cleanup_lease();
    const SelectionSnapshot selection_before_stop = registry().selection_snapshot();
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        snapshot = g_audio_route_state;
        patch_count = g_pending_play_setup_patch.patches.size();
        controller_matches_route = controller && snapshot.controller == controller;
        natural_completion_facts.route_custom_owned = snapshot.custom_resource_owned;
        if (natural_completion_facts.route_custom_owned) {
            armed_stop_readiness_diagnostic.proposed = true;
        }
        natural_completion_facts.route_lease_valid = snapshot.lease_identity.valid();
        natural_completion_facts.route_controller_exact = controller_matches_route;
        natural_completion_facts.existing_retirement_phase =
            snapshot.stop_retirement.phase;
        const auto& detached = g_onmemory_bank_lifecycle.active();
        natural_completion_lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
        natural_completion_counter_snapshot = g_next_stop_retirement_epoch;
        natural_completion_monitor_epoch = natural_completion_counter_snapshot + 1;
        if (natural_completion_monitor_epoch == 0) {
            ++natural_completion_monitor_epoch;
        }
        natural_completion_facts.detached_present = static_cast<bool>(detached);
        if (detached) {
            natural_completion_detached = detached;
            natural_completion_facts.detached_restore_applied =
                detached.phase == OnMemoryBankLifecyclePhase::RestoreApplied;
            natural_completion_facts.detached_route_predecessor =
                detached.route_generation + 1 == snapshot.generation;
            natural_completion_facts.detached_cleanup_generation_exact =
                detached.cleanup_generation == snapshot.lease_identity.generation;
            natural_completion_facts.detached_sound_exact =
                detached.sound.object == snapshot.owned_sound
                && detached.sound.live.internal_index
                    == snapshot.owned_sound_identity.live.internal_index
                && detached.sound.live.serial_number
                    == snapshot.owned_sound_identity.live.serial_number;
            natural_completion_facts.detached_request_available =
                AudioBgmRequestHandle{detached.request_handle}.valid_bgm_request();
            natural_completion_facts.detached_request_exact =
                detached.request_handle == snapshot.owned_request_handle;
            natural_completion_facts.detached_tokens_valid =
                static_cast<bool>(detached);
            natural_completion_facts.detached_owner_restored =
                detached.owner_restore_verified && !detached.release_attempted;
        }
        natural_completion_facts.frozen_patch_valid =
            snapshot.frozen_sound_patch.valid();
        natural_completion_facts.frozen_route_predecessor =
            snapshot.frozen_sound_patch.route_generation + 1 == snapshot.generation;
        natural_completion_facts.frozen_lease_exact =
            snapshot.frozen_sound_patch.lease_identity == snapshot.lease_identity;
        natural_completion_facts.frozen_sound_exact =
            snapshot.frozen_sound_patch.sound == snapshot.owned_sound
            && snapshot.frozen_sound_patch.sound_identity.live.internal_index
                == snapshot.owned_sound_identity.live.internal_index
            && snapshot.frozen_sound_patch.sound_identity.live.serial_number
                == snapshot.owned_sound_identity.live.serial_number;
        for (const auto& patch : snapshot.frozen_sound_patch.patches) {
            if (patch.object == snapshot.owned_sound
                && patch.label && std::strcmp(patch.label, "sound+0x548") == 0
                && natural_completion_facts.detached_present
                && patch.original == natural_completion_detached.canonical.encode()
                && patch.replacement == 0) {
                natural_completion_facts.frozen_owner_patch_exact = true;
                break;
            }
        }
        natural_completion_facts.journals_restored =
            g_active_patch_journal.empty()
            && g_pending_play_setup_patch.patches.empty()
            && g_failed_patch_journal.empty();
        natural_completion_facts.pause_resume_clear =
            g_pause_resume_bank.phase == PauseResumeBankPhase::Idle
            || g_pause_resume_bank.phase == PauseResumeBankPhase::Complete;
        natural_completion_facts.setup_clear = !g_unpublished_audio_setup;
        natural_completion_facts.registry_playback_exact =
            playback_before_stop.song
            && token_matches_route(playback_before_stop.token, snapshot);
        natural_completion_facts.registry_cleanup_clear_or_exact =
            !cleanup_before_stop.song
            || token_matches_route(cleanup_before_stop.token, snapshot);
        armed_stop_readiness_diagnostic.natural_completion_route_predecessor =
            natural_completion_facts.detached_route_predecessor
            && natural_completion_facts.frozen_route_predecessor;
        armed_stop_readiness_diagnostic.natural_completion_detached_route_predecessor =
            natural_completion_facts.detached_route_predecessor;
        armed_stop_readiness_diagnostic.natural_completion_frozen_route_predecessor =
            natural_completion_facts.frozen_route_predecessor;
        const PrivateControllerSetupEligibility private_eligibility{
            !playback_before_stop.song,
            !cleanup_before_stop.song,
            !snapshot.custom_resource_owned && !snapshot.list_cleanup_pending,
            snapshot.stop_retirement.phase == AudioStopRetirementPhase::None,
            g_unpublished_audio_setup
                && selection_before_stop.generation == g_unpublished_audio_setup.selection.generation
                && selection_before_stop.song == g_unpublished_audio_setup.selection.song,
            g_unpublished_audio_setup
                && token_matches_route(g_unpublished_audio_setup.token, snapshot)
                && snapshot.phase == AudioRoutePhase::Armed
                && snapshot.controller == controller,
            g_unpublished_audio_setup.controller_stage
                == PrivateControllerSetupStage::AwaitingStop,
        };
        if (g_unpublished_audio_setup
            && snapshot.phase == AudioRoutePhase::Armed) {
            armed_stop_readiness_diagnostic.proposed = true;
            armed_stop_readiness_diagnostic.playback_absent =
                private_eligibility.playback_absent;
            armed_stop_readiness_diagnostic.cleanup_absent =
                private_eligibility.cleanup_absent;
            armed_stop_readiness_diagnostic.route_unowned =
                private_eligibility.route_unowned;
            armed_stop_readiness_diagnostic.retirement_absent =
                private_eligibility.retirement_absent;
            armed_stop_readiness_diagnostic.selection_matches =
                private_eligibility.selection_matches;
            armed_stop_readiness_diagnostic.route_matches =
                private_eligibility.route_matches;
            armed_stop_readiness_diagnostic.stage_awaiting_stop =
                private_eligibility.operation_expected;
            armed_stop_readiness_diagnostic.controller =
                reinterpret_cast<uintptr_t>(controller);
            armed_stop_readiness_diagnostic.selection_generation =
                g_unpublished_audio_setup.selection.generation;
            armed_stop_readiness_diagnostic.route_generation = snapshot.generation;
            armed_stop_readiness_diagnostic.lease_generation =
                snapshot.lease_identity.generation;
            armed_stop_readiness_diagnostic.song_key =
                snapshot.lease_identity.song_key;
            armed_stop_readiness_diagnostic.desired_song_present =
                !snapshot.desired_song_id.empty();
            armed_stop_readiness_diagnostic.desired_song_key =
                snapshot.desired_song_id.empty()
                ? 0 : audio_route_song_key(snapshot.desired_song_id);
            armed_stop_readiness_diagnostic.patched_song_present =
                !snapshot.patched_song_id.empty();
            armed_stop_readiness_diagnostic.patched_song_key =
                snapshot.patched_song_id.empty()
                ? 0 : audio_route_song_key(snapshot.patched_song_id);
            armed_stop_readiness_diagnostic.setup_stage =
                g_unpublished_audio_setup.controller_stage;
            armed_stop_readiness_diagnostic.arm_proof_attempted =
                snapshot.controller_arm_proof_attempted;
            armed_stop_readiness_diagnostic.arm_bind_succeeded =
                snapshot.controller_arm_bind_succeeded;
            armed_stop_readiness_diagnostic.attempted_arm_proof =
                armed_stop_controller_proof_diagnostic(
                    snapshot.controller_arm_attempted_proof);
            armed_stop_bound_proof = g_unpublished_audio_setup.controller_proof;
            armed_stop_readiness_diagnostic.bound_arm_proof =
                armed_stop_controller_proof_diagnostic(armed_stop_bound_proof);
            armed_stop_readiness_diagnostic.frozen_profile_active =
                g_frozen_profile_lease.active();
            if (armed_stop_readiness_diagnostic.frozen_profile_active) {
                const AudioRouteLeaseIdentity frozen_identity =
                    g_frozen_profile_lease.identity();
                armed_stop_readiness_diagnostic.frozen_profile_generation =
                    frozen_identity.generation;
                armed_stop_readiness_diagnostic.frozen_profile_song_key =
                    frozen_identity.song_key;
                armed_stop_readiness_diagnostic.frozen_profile_native_arm_attempted =
                    g_frozen_profile_lease.native_arm_attempted();
            }
            const auto& lifecycle = g_onmemory_bank_lifecycle.active();
            armed_stop_readiness_diagnostic.lifecycle_available =
                static_cast<bool>(lifecycle);
            armed_stop_readiness_diagnostic.lifecycle_state_epoch =
                g_onmemory_bank_lifecycle.state_epoch();
            if (lifecycle) {
                armed_stop_readiness_diagnostic.lifecycle_ordinal = lifecycle.ordinal;
                armed_stop_readiness_diagnostic.lifecycle_canonical_token =
                    lifecycle.canonical.encode();
                armed_stop_readiness_diagnostic.lifecycle_custom_token =
                    lifecycle.custom.encode();
            }
        }
        if (private_controller_setup_eligible(private_eligibility)) {
            private_setup_before_stop = g_unpublished_audio_setup;
            private_stop_candidate = true;
        }
        pending_lifetime_guarded = patch_count != 0 && (controller_matches_route || snapshot.controller == nullptr);
        if (pending_lifetime_guarded) {
            g_pending_play_setup_patch.restore_safe = false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        natural_completion_facts.aggregate_snapshot_available = true;
        natural_completion_facts.active_aggregate_borrower =
            std::any_of(g_bgm_playback_borrowers.begin(),
                g_bgm_playback_borrowers.end(),
                [](const BgmPlaybackBorrowerRecord& record) {
                    return record.active;
                });
        natural_completion_facts.canonical_proof_snapshot_available = true;
        natural_completion_facts.canonical_proof_active =
            g_bgm_canonical_substrate_proof.active;
    }
    if (armed_stop_readiness_diagnostic.proposed) {
        armed_stop_readiness_diagnostic.private_stop_candidate =
            private_stop_candidate;
        void* diagnostic_slot = nullptr;
        void* diagnostic_bgm = nullptr;
        void* diagnostic_sound = nullptr;
        uint64_t diagnostic_request = 0;
        uint8_t diagnostic_state = 0xff;
        armed_stop_readiness_diagnostic.chain_read =
            read_controller_bgm_chain(controller, diagnostic_slot, diagnostic_bgm);
        if (armed_stop_readiness_diagnostic.chain_read) {
            armed_stop_readiness_diagnostic.sound_read = core::safe_read_field(
                diagnostic_bgm, runtime_layouts::SqexSeadBgm::sound,
                diagnostic_sound);
            if (armed_stop_readiness_diagnostic.sound_read) {
                armed_stop_readiness_diagnostic.request_read = core::safe_read_field(
                    diagnostic_bgm, runtime_layouts::SqexSeadBgm::request_handle,
                    diagnostic_request);
                if (armed_stop_readiness_diagnostic.request_read) {
                    armed_stop_readiness_diagnostic.state_read = core::safe_read_field(
                        diagnostic_slot, runtime_layouts::SqexSeadSlot::state,
                        diagnostic_state);
                }
            }
        }
        armed_stop_readiness_diagnostic.slot =
            reinterpret_cast<uintptr_t>(diagnostic_slot);
        armed_stop_readiness_diagnostic.bgm =
            reinterpret_cast<uintptr_t>(diagnostic_bgm);
        armed_stop_readiness_diagnostic.sound =
            reinterpret_cast<uintptr_t>(diagnostic_sound);
        armed_stop_readiness_diagnostic.request = diagnostic_request;
        armed_stop_readiness_diagnostic.state = diagnostic_state;
        const bool tuple_read = armed_stop_readiness_diagnostic.chain_read
            && armed_stop_readiness_diagnostic.sound_read
            && armed_stop_readiness_diagnostic.request_read
            && armed_stop_readiness_diagnostic.state_read;
        armed_stop_readiness_diagnostic.idle_null_exact = tuple_read
            && diagnostic_sound == nullptr && diagnostic_request == 0
            && diagnostic_state == 0;
        armed_stop_readiness_diagnostic.active_sound_exact = tuple_read
            && diagnostic_sound != nullptr && diagnostic_request != 0
            && diagnostic_state == 4;

        UObjectIdentity current_controller_identity;
        armed_stop_readiness_diagnostic.current_controller_identity_attempted =
            controller != nullptr;
        armed_stop_readiness_diagnostic.current_controller_identity_read =
            controller
            && read_uobject_identity(controller, current_controller_identity);
        if (armed_stop_readiness_diagnostic.current_controller_identity_read) {
            const ControllerIdentityProof current_proof =
                controller_identity_proof(controller, current_controller_identity);
            armed_stop_readiness_diagnostic.current_controller_proof =
                armed_stop_controller_proof_diagnostic(current_proof);
            const ControllerIdentityProofMismatchReport proof_report =
                classify_controller_identity_proof_mismatch(
                    armed_stop_bound_proof, current_proof);
            armed_stop_readiness_diagnostic.current_controller_proof_exact =
                proof_report.first_mismatch == ControllerIdentityProofMismatch::None;
            armed_stop_readiness_diagnostic.current_controller_proof_mismatch =
                proof_report.first_mismatch;
        }
        armed_stop_readiness_diagnostic.chart_expand_tls =
            current_chart_audio_expand_tls();
        ChartAudioDiagnosticTransaction tls_transaction;
        if (armed_stop_readiness_diagnostic.chart_expand_tls.active
            && chart_audio_diagnostic_generation_exact(
                armed_stop_readiness_diagnostic.chart_expand_tls.generation,
                tls_transaction)) {
            armed_stop_readiness_diagnostic.chart_transaction_associated = true;
            armed_stop_readiness_diagnostic.chart_transaction_associated_by_tls = true;
            armed_stop_readiness_diagnostic.chart_transaction_generation =
                armed_stop_readiness_diagnostic.chart_expand_tls.generation;
        } else {
            ChartAudioDiagnosticTransaction transaction;
            if (chart_audio_diagnostic_transaction_exact(
                    armed_stop_readiness_diagnostic.selection_generation,
                    armed_stop_readiness_diagnostic.route_generation,
                    armed_stop_readiness_diagnostic.lease_generation,
                    armed_stop_readiness_diagnostic.song_key,
                    transaction)) {
                armed_stop_readiness_diagnostic.chart_transaction_associated = true;
                armed_stop_readiness_diagnostic.chart_transaction_generation =
                    transaction.generation;
            }
        }
    }
    void* stop_slot = nullptr;
    void* stop_bgm = nullptr;
    void* stop_sound = nullptr;
    uint64_t stop_request_handle = 0;
    uint8_t stop_state = 0;
    exact_owned_chain_before_stop = controller_matches_route
        && snapshot.custom_resource_owned
        && lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, snapshot.controller_identity)
        && uobject_identity_matches(snapshot.owned_sound, snapshot.owned_sound_identity)
        && read_controller_bgm_chain(controller, stop_slot, stop_bgm)
        && core::safe_read_field(stop_bgm, runtime_layouts::SqexSeadBgm::sound, stop_sound)
        && core::safe_read_field(stop_bgm, runtime_layouts::SqexSeadBgm::request_handle, stop_request_handle)
        && core::safe_read_field(stop_slot, runtime_layouts::SqexSeadSlot::state, stop_state)
        && stop_slot == snapshot.owned_slot
        && stop_bgm == snapshot.owned_bgm
        && stop_sound == snapshot.owned_sound
        && stop_request_handle == snapshot.owned_request_handle
        && stop_request_handle != 0
        && stop_state == 4;
    UObjectIdentity private_controller_identity;
    UObjectIdentity private_sound_identity;
    const bool private_controller_identity_observed = private_stop_candidate
        && read_controller_bgm_chain(controller, stop_slot, stop_bgm)
        && core::safe_read_field(stop_bgm, runtime_layouts::SqexSeadBgm::sound, stop_sound)
        && core::safe_read_field(stop_bgm, runtime_layouts::SqexSeadBgm::request_handle, stop_request_handle)
        && core::safe_read_field(stop_slot, runtime_layouts::SqexSeadSlot::state, stop_state)
        && stop_sound
        && read_uobject_identity(controller, private_controller_identity);
    const bool private_sound_identity_observed = private_controller_identity_observed
        && read_uobject_identity(stop_sound, private_sound_identity);
    UObjectIdentityPrefilterResult private_controller_prefilter_result =
        UObjectIdentityPrefilterResult::NotEvaluated;
    const bool private_controller_prefilter_passed = private_sound_identity_observed
        && uobject_identity_matches(
            controller, snapshot.controller_identity,
            private_controller_prefilter_result);
    const bool private_chain_read = private_sound_identity_observed
        && private_controller_prefilter_passed;
    const ControllerIdentityProof private_controller_proof =
        private_controller_identity_observed
        ? controller_identity_proof(controller, private_controller_identity)
        : ControllerIdentityProof{};
    bool private_stop_staged = false;
    PrivateControllerStopDiagnosticEvidence private_stop_diagnostic;
    const bool private_controller_proof_report_available =
        private_controller_identity_observed
        && snapshot.controller_arm_proof_attempted;
    const ControllerIdentityProofMismatchReport private_controller_proof_report =
        private_controller_proof_report_available
        ? classify_controller_identity_proof_mismatch(
            snapshot.controller_arm_attempted_proof,
            private_controller_proof)
        : ControllerIdentityProofMismatchReport{};
    if (private_chain_read) {
        const bool selection_guard_acquired = registry().acquire_selection_guard(
            private_setup_before_stop.selection, private_setup_before_stop.token);
        private_stop_diagnostic.selection_guard_acquired =
            diagnostic_fact(selection_guard_acquired);
        if (selection_guard_acquired) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const bool route_generation_unchanged =
                g_audio_route_state.generation == snapshot.generation;
            private_stop_diagnostic.route_generation_unchanged =
                diagnostic_fact(route_generation_unchanged);
            if (route_generation_unchanged) {
                const bool lease_unchanged =
                    g_audio_route_state.lease_identity == snapshot.lease_identity;
                private_stop_diagnostic.lease_unchanged = diagnostic_fact(lease_unchanged);
                if (lease_unchanged) {
                    const bool phase_armed =
                        g_audio_route_state.phase == AudioRoutePhase::Armed;
                    private_stop_diagnostic.phase_armed = diagnostic_fact(phase_armed);
                    if (phase_armed) {
                        const bool controller_identity_unchanged =
                            controller_identity_proof_matches(
                                g_unpublished_audio_setup.controller_proof,
                                private_controller_proof)
                            && uobject_identity_matches(
                                controller, g_audio_route_state.controller_identity);
                        private_stop_diagnostic.controller_identity_unchanged =
                            diagnostic_fact(controller_identity_unchanged);
                        if (controller_identity_unchanged) {
                            g_audio_route_state.private_setup_sound_identity =
                                private_sound_identity;
                            PrivateControllerStopObservationDiagnostic observation_diagnostic;
                            private_stop_staged =
                                g_unpublished_audio_setup.observe_controller_stop(
                                    private_setup_before_stop.token, controller,
                                    private_controller_proof, stop_slot, stop_bgm,
                                    stop_sound, private_sound_identity.live,
                                    stop_request_handle,
                                    &observation_diagnostic);
                            private_stop_diagnostic.observe_result =
                                observation_diagnostic.result;
                            private_stop_diagnostic.observe_evidence =
                                observation_diagnostic.evidence;
                            if (private_stop_staged) {
                                private_setup_before_stop = g_unpublished_audio_setup;
                            }
                        }
                    }
                }
            }
        }
    }
    if (private_stop_candidate && !private_stop_staged) {
        armed_stop_readiness_diagnostic.private_setup_failed = true;
        private_stop_diagnostic_record.pending = true;
        private_stop_diagnostic_record.evidence = private_stop_diagnostic;
        private_stop_diagnostic_record.registry_generation =
            private_setup_before_stop.token.registry_generation;
        private_stop_diagnostic_record.route_generation =
            private_setup_before_stop.token.route_generation;
        private_stop_diagnostic_record.lease_generation =
            private_setup_before_stop.token.lease_generation;
        private_stop_diagnostic_record.song_key =
            private_setup_before_stop.token.song_key;
        private_stop_diagnostic_record.controller =
            reinterpret_cast<uintptr_t>(controller);
        private_stop_diagnostic_record.sound =
            reinterpret_cast<uintptr_t>(stop_sound);
        private_stop_diagnostic_record.arm_proof_attempted =
            snapshot.controller_arm_proof_attempted;
        private_stop_diagnostic_record.arm_bind_succeeded =
            snapshot.controller_arm_bind_succeeded;
        private_stop_diagnostic_record.arm_attempted_proof =
            snapshot.controller_arm_attempted_proof;
        private_stop_diagnostic_record.evidence.identity_observation.prefilter_result =
            private_controller_prefilter_result;
        private_stop_diagnostic_record.evidence.identity_observation
            .observed_live_capture_attempted =
            private_controller_identity_observed
            && private_controller_identity.live_capture_attempted;
        private_stop_diagnostic_record.evidence.identity_observation
            .observed_live_capture_result =
            private_controller_identity.live_capture_result;
        private_stop_diagnostic_record.controller_proof_report_available =
            private_controller_proof_report_available;
        private_stop_diagnostic_record.controller_proof_report =
            private_controller_proof_report;
        fail_private_controller_setup(
            snapshot.lease_identity, private_setup_before_stop.token,
            stop_sound, stop_request_handle, stop_state,
            !private_chain_read ? "stop_expected_sound_unreadable"
                : "stop_identity_or_selection_guard_failed",
            false);
        private_setup_before_stop.invalidate();
    }
    armed_stop_readiness_diagnostic.private_setup_staged = private_stop_staged;
    void* pause_pre_slot = nullptr;
    void* pause_pre_bgm = nullptr;
    void* pause_pre_sound = nullptr;
    uint64_t pause_pre_request = 0;
    uint8_t pause_pre_state = 0;
    const bool pause_pre_read = read_controller_audio_chain(controller,
        pause_pre_slot, pause_pre_bgm, pause_pre_sound,
        pause_pre_request, pause_pre_state);
    if (pause_pre_read && pause_pre_sound == nullptr
        && pause_pre_request == 0 && pause_pre_state == 0) {
        terminal_handoff_stop = observe_bgm_aggregate_terminal_handoff_at_stop(controller,
            pause_pre_read, pause_pre_slot, pause_pre_bgm, pause_pre_sound,
            pause_pre_request, pause_pre_state);
    }
    natural_completion_facts.pre_stop_chain_read = pause_pre_read;
    natural_completion_facts.pre_stop_sound_null =
        pause_pre_read && pause_pre_sound == nullptr;
    natural_completion_facts.pre_stop_request_zero =
        pause_pre_read && pause_pre_request == 0;
    natural_completion_facts.pre_stop_state_idle =
        pause_pre_read && pause_pre_state == 0;
    armed_stop_readiness_diagnostic.natural_completion_evaluated = true;
    armed_stop_readiness_diagnostic.natural_completion_failure =
        classify_audio_natural_completion_retirement(natural_completion_facts);
    if (armed_stop_readiness_diagnostic.natural_completion_failure
        == AudioNaturalCompletionRetirementFailure::None) {
        armed_stop_readiness_diagnostic.proposed = true;
    }
    void* pause_post_slot = nullptr;
    void* pause_post_bgm = nullptr;
    void* pause_post_sound = nullptr;
    uint64_t pause_post_request = 0;
    uint8_t pause_post_state = 0;
    BgmPlaybackNativeStopObservationResult native_stop_result;
    native_stop_result.native = invoke_audio_production_stop(controller);
    armed_stop_readiness_diagnostic.original_forwarded =
        bgm_playback_native_stop_original_forwarded(native_stop_result.native);
    deferred_private_stop_diagnostic.make_eligible();
    native_stop_result.post_observation_succeeded = read_controller_audio_chain(
        controller, pause_post_slot, pause_post_bgm, pause_post_sound,
        pause_post_request, pause_post_state);
    native_stop_result.post_observation_ran = true;
    native_stop_result.original_forwarded =
        bgm_playback_native_stop_original_forwarded(native_stop_result.native);
    const bool pause_post_read =
        native_stop_result.post_observation_succeeded;
    finish_bgm_aggregate_terminal_handoff_after_stop(
        terminal_handoff_stop,
        native_stop_result.native.call_count,
        native_stop_result.post_observation_ran,
        native_stop_result.post_observation_succeeded,
        controller, pause_post_slot, pause_post_bgm, pause_post_sound,
        pause_post_request, pause_post_state);
    if (terminal_handoff_stop.active) {
        deferred_terminal_handoff_stop.make_eligible();
    }
    native_stop_return_diagnostic = {
        true,
        reinterpret_cast<uintptr_t>(controller),
        native_stop_result.native.target,
        native_stop_result.native.result,
        native_stop_result.native.call_count,
        native_stop_result.post_observation_ran,
        native_stop_result.post_observation_succeeded,
        native_stop_result.original_forwarded,
    };
    deferred_native_stop_return.make_eligible();
    PauseResumeBankSession pause_session_snapshot;
    bool start_resumed_retirement = false;
    PauseResumeBankSession retirement_candidate_snapshot;
    bool anchor_retirement_candidate = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const auto& active = g_onmemory_bank_lifecycle.active();
        if ((g_pause_resume_bank.phase == PauseResumeBankPhase::Idle
                || g_pause_resume_bank.phase == PauseResumeBankPhase::Complete)
            && g_piano_audio_owner_tick && g_piano_audio_owner_tick_nonce != 0
            && audio_production_play_setup_tls().original_depth == 0
            && playback_before_stop.song
            && exact_owned_chain_before_stop && active
            && active.phase == OnMemoryBankLifecyclePhase::RestoreApplied
            && active.sound.object == snapshot.owned_sound) {
            g_pause_resume_bank = {};
            g_pause_resume_bank.phase = PauseResumeBankPhase::RetirementCandidate;
            g_pause_resume_bank.detached = active;
            g_pause_resume_bank.selection = selection_before_stop;
            g_pause_resume_bank.controller = controller;
            g_pause_resume_bank.controller_identity = snapshot.controller_identity;
            retirement_candidate_snapshot = g_pause_resume_bank;
            retirement_candidate_snapshot.lifecycle_state_epoch =
                g_onmemory_bank_lifecycle.state_epoch();
            anchor_retirement_candidate = true;
        } else if (g_pause_resume_bank.phase
                == PauseResumeBankPhase::AwaitingResumeOrListReturn
            && g_piano_audio_owner_tick && g_piano_audio_owner_tick_nonce != 0
            && audio_production_play_setup_tls().original_depth == 0) {
            const bool exact_transition = pause_pre_read && pause_post_read
                && controller == g_pause_resume_bank.controller
                && uobject_identity_matches(
                    controller, g_pause_resume_bank.controller_identity)
                && pause_pre_sound
                && pause_pre_sound != g_pause_resume_bank.detached.sound.object
                && pause_post_sound == nullptr;
            const auto outcome = pause_resume_stop_outcome(
                g_pause_resume_bank.phase, true, exact_transition);
            g_pause_resume_bank.phase = outcome.phase;
            if (exact_transition) {
                g_pause_resume_bank.owner_tick = g_piano_audio_owner_tick;
                g_pause_resume_bank.owner_tick_nonce =
                    g_piano_audio_owner_tick_nonce;
            }
        } else if (g_pause_resume_bank.phase == PauseResumeBankPhase::ResumedActive
            || g_pause_resume_bank.phase == PauseResumeBankPhase::ExitPending) {
            const PauseResumeRetirementAdmissionFacts admission{
                true,
                pause_pre_read,
                controller == g_pause_resume_bank.controller,
                pause_pre_sound == g_pause_resume_bank.detached.sound.object,
                pause_pre_request == g_pause_resume_bank.request_handle,
                pause_pre_state == 4,
                true,
                true,
                true,
            };
            const auto mismatch = classify_pause_resume_retirement_mismatch(admission);
            if (mismatch == PauseResumeRetirementMismatch::None) {
                pause_session_snapshot = g_pause_resume_bank;
                start_resumed_retirement = true;
            } else if (pause_resume_retirement_mismatch_admitted(
                    g_pause_resume_bank.retirement_mismatch_recorded, mismatch)) {
                g_pause_resume_bank.retirement_mismatch_recorded = true;
                retirement_mismatch_marker = {
                    PauseResumeBankMarkerStatus::RetirementMismatch,
                    pause_resume_retirement_mismatch_name(mismatch),
                    g_pause_resume_bank.session_epoch,
                    g_pause_resume_bank.cycle_epoch,
                    true};
                deferred_retirement_mismatch.make_eligible();
            }
        }
    }
    if (anchor_retirement_candidate) {
        anchor_bgm_playback_retirement_candidate(retirement_candidate_snapshot);
    }
    if (start_resumed_retirement) {
        StopRetirementRead retirement_read = read_stop_retirement(
            pause_session_snapshot.controller,
            pause_session_snapshot.controller_identity,
            pause_session_snapshot.detached.route_generation + 1,
            pause_session_snapshot.lease_identity,
            pause_session_snapshot.slot,
            pause_session_snapshot.bgm,
            pause_session_snapshot.detached.sound.object,
            pause_session_snapshot.request_handle);
        AudioStopRetirementObservation monitor_start = retirement_read.observation;
        if (monitor_start.valid && !monitor_start.request_handle_retired) {
            monitor_start.request_handle_retired = true;
        }
        AudioStopRetirementState monitor;
        const bool monitor_started = begin_audio_stop_retirement_monitor(
            monitor, monitor_start);
        OnMemoryBankRetiredBackingEvidence backing_evidence;
        const uint64_t monitor_epoch =
            g_pause_resume_bank_retirement_epoch = next_pause_resume_bank_epoch(
                g_pause_resume_bank_retirement_epoch);
        if (retirement_read.observation.request_handle_retired) {
            OnMemoryBankRetiredBackingObservation backing_observation;
            backing_observation.route_generation =
                pause_session_snapshot.detached.route_generation + 1;
            backing_observation.cleanup_generation =
                pause_session_snapshot.detached.cleanup_generation;
            backing_observation.monitor_epoch = monitor_epoch;
            backing_observation.request_handle =
                pause_session_snapshot.request_handle;
            backing_observation.sound_identity =
                pause_session_snapshot.detached.sound.live;
            backing_observation.exact_request_present = true;
            backing_observation.backing_observed =
                retirement_read.retired_backing_observed;
            backing_observation.backing = retirement_read.retired_backing;
            (void)record_onmemory_bank_retired_backing(
                backing_evidence, backing_observation);
        }
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const bool commit_current = pause_resume_epoch_matches(
                pause_session_snapshot.session_epoch,
                pause_session_snapshot.cycle_epoch,
                g_pause_resume_bank.session_epoch,
                g_pause_resume_bank.cycle_epoch)
            && (g_pause_resume_bank.phase == PauseResumeBankPhase::ResumedActive
                || g_pause_resume_bank.phase == PauseResumeBankPhase::ExitPending)
            && monitor_started && monitor.active();
        if (commit_current) {
            g_pause_resume_bank.retirement_monitor = monitor;
            g_pause_resume_bank.retirement_epoch = monitor_epoch;
            g_pause_resume_bank.retired_backing = backing_evidence;
            g_pause_resume_bank.pending_release_probe = {};
            g_pause_resume_bank.pending_release_evidence = {};
            g_pause_resume_bank.phase = g_pause_resume_bank.exit_requested
                ? PauseResumeBankPhase::ExitPending
                : PauseResumeBankPhase::RetirementWaiting;
        } else {
            const bool same_cycle = pause_resume_epoch_matches(
                pause_session_snapshot.session_epoch,
                pause_session_snapshot.cycle_epoch,
                g_pause_resume_bank.session_epoch,
                g_pause_resume_bank.cycle_epoch);
            if (!g_pause_resume_bank.retirement_mismatch_recorded) {
                const PauseResumeRetirementAdmissionFacts admission{
                    true, true, true, true, true, true,
                    retirement_read.observation.valid,
                    monitor_started && monitor.active(),
                    commit_current,
                };
                const auto mismatch = classify_pause_resume_retirement_mismatch(admission);
                if (same_cycle && pause_resume_retirement_mismatch_admitted(
                        g_pause_resume_bank.retirement_mismatch_recorded, mismatch)) {
                    g_pause_resume_bank.retirement_mismatch_recorded = true;
                    retirement_mismatch_marker = {
                        PauseResumeBankMarkerStatus::RetirementMismatch,
                        pause_resume_retirement_mismatch_name(mismatch),
                        g_pause_resume_bank.session_epoch,
                        g_pause_resume_bank.cycle_epoch,
                        true};
                    deferred_retirement_mismatch.make_eligible();
                }
            }
            if (same_cycle
                && (g_pause_resume_bank.phase == PauseResumeBankPhase::ResumedActive
                    || g_pause_resume_bank.phase == PauseResumeBankPhase::ExitPending)) {
                g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
            }
        }
    }

    if (private_stop_staged) {
        bool observed = false;
        armed_stop_readiness_diagnostic.postcondition_evaluated = true;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            armed_stop_readiness_diagnostic.post_route_phase =
                g_audio_route_state.phase;
            armed_stop_readiness_diagnostic.post_route_generation =
                g_audio_route_state.generation;
            armed_stop_readiness_diagnostic.post_setup_valid =
                static_cast<bool>(g_unpublished_audio_setup);
            if (g_unpublished_audio_setup) {
                armed_stop_readiness_diagnostic.post_setup_stage =
                    g_unpublished_audio_setup.controller_stage;
                armed_stop_readiness_diagnostic.post_setup_registry_generation =
                    g_unpublished_audio_setup.token.registry_generation;
                armed_stop_readiness_diagnostic.post_setup_route_generation =
                    g_unpublished_audio_setup.token.route_generation;
                armed_stop_readiness_diagnostic.post_setup_lease_generation =
                    g_unpublished_audio_setup.token.lease_generation;
                armed_stop_readiness_diagnostic.post_setup_song_key =
                    g_unpublished_audio_setup.token.song_key;
            }
            observed = g_audio_route_state.generation == snapshot.generation
                && g_audio_route_state.lease_identity == snapshot.lease_identity
                && g_audio_route_state.phase == AudioRoutePhase::Armed
                && g_unpublished_audio_setup.controller_stage
                    == PrivateControllerSetupStage::StopObserved
                && g_unpublished_audio_setup.token == private_setup_before_stop.token
                && controller_identity_proof_matches_live(
                    g_unpublished_audio_setup.controller_proof, controller)
                && uobject_identity_matches(controller, g_audio_route_state.controller_identity)
                && uobject_identity_matches(
                    stop_sound, g_audio_route_state.private_setup_sound_identity);
        }
        armed_stop_readiness_diagnostic.postcondition_exact = observed;
        armed_stop_readiness_diagnostic.postcondition_failed = !observed;
        if (!observed) {
            fail_private_controller_setup(
                snapshot.lease_identity, private_setup_before_stop.token,
                stop_sound, stop_request_handle, stop_state,
                "stop_postcondition_mismatch", false);
        }
    }

    void* post_stop_slot = nullptr;
    void* post_stop_bgm = nullptr;
    void* post_stop_sound = nullptr;
    uint64_t post_stop_request_handle = 0;
    uint8_t post_stop_state = 0;
    const bool exact_owned_chain_after_stop = exact_owned_chain_before_stop
        && lookup_current_bgm_controller() == controller
        && uobject_identity_matches(controller, snapshot.controller_identity)
        && uobject_identity_matches(snapshot.owned_sound, snapshot.owned_sound_identity)
        && read_controller_bgm_chain(controller, post_stop_slot, post_stop_bgm)
        && core::safe_read_field(post_stop_bgm, runtime_layouts::SqexSeadBgm::sound, post_stop_sound)
        && core::safe_read_field(post_stop_bgm, runtime_layouts::SqexSeadBgm::request_handle, post_stop_request_handle)
        && core::safe_read_field(post_stop_slot, runtime_layouts::SqexSeadSlot::state, post_stop_state)
        && post_stop_slot == snapshot.owned_slot
        && post_stop_bgm == snapshot.owned_bgm
        && post_stop_sound == snapshot.owned_sound
        && post_stop_request_handle == snapshot.owned_request_handle
        && post_stop_request_handle != 0
        && post_stop_state == 4;
    const bool natural_completion_candidate =
        armed_stop_readiness_diagnostic.natural_completion_failure
        == AudioNaturalCompletionRetirementFailure::None;
    const StopRetirementRead retirement_after_stop =
        exact_owned_chain_before_stop || natural_completion_candidate
        ? read_stop_retirement(
            controller,
            snapshot.controller_identity,
            snapshot.generation,
            snapshot.lease_identity,
            snapshot.owned_slot,
            snapshot.owned_bgm,
            natural_completion_candidate
                ? natural_completion_detached.sound.object
                : snapshot.owned_sound,
            natural_completion_candidate
                ? natural_completion_detached.request_handle
                : snapshot.owned_request_handle)
        : StopRetirementRead{};
    AudioNaturalCompletionRetirementStart natural_completion_start;
    AudioStopRetirementState natural_completion_monitor;
    OnMemoryBankRetiredBackingEvidence natural_completion_backing;
    bool natural_completion_monitor_started = false;
    if (natural_completion_candidate) {
        const OnMemoryBankDetachedBackingSeedFacts seed_facts{
            natural_completion_lifecycle_epoch,
            natural_completion_detached.ordinal,
            snapshot.generation,
            natural_completion_detached.cleanup_generation,
            natural_completion_monitor_epoch,
            natural_completion_detached.request_handle,
            natural_completion_detached.sound.live,
            natural_completion_detached.canonical,
            natural_completion_detached.custom,
            natural_completion_detached.backing_observed,
            natural_completion_detached.backing_identity,
        };
        const bool backing_seeded = seed_onmemory_bank_retired_backing_from_detached(
            natural_completion_backing, natural_completion_detached, seed_facts);
        if (backing_seeded
            && retirement_after_stop.observation.request_handle_retired) {
            (void)record_onmemory_bank_retired_backing(
                natural_completion_backing,
                {
                    snapshot.generation,
                    natural_completion_detached.cleanup_generation,
                    natural_completion_monitor_epoch,
                    natural_completion_detached.request_handle,
                    natural_completion_detached.sound.live,
                    true,
                    retirement_after_stop.retired_backing_observed,
                    retirement_after_stop.retired_backing,
                });
        }
        natural_completion_start = start_audio_natural_completion_retirement(
            natural_completion_facts, retirement_after_stop.observation,
            natural_completion_backing);
        armed_stop_readiness_diagnostic.natural_completion_failure =
            natural_completion_start.failure;
        armed_stop_readiness_diagnostic.natural_completion_presence_pending =
            natural_completion_start.presence_pending;
        natural_completion_monitor_started = natural_completion_start.admitted;
        natural_completion_monitor = natural_completion_start.monitor;
        armed_stop_readiness_diagnostic.natural_completion_monitor_started =
            natural_completion_monitor_started;
    }

    if (natural_completion_monitor_started) {
        const PlaybackSnapshot playback_commit = registry().playback_snapshot();
        const CleanupLease cleanup_commit = registry().cleanup_lease();
        bool ownership_commit_exact = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            ownership_commit_exact = !g_bgm_canonical_substrate_proof.active
                && std::none_of(g_bgm_playback_borrowers.begin(),
                    g_bgm_playback_borrowers.end(),
                    [](const BgmPlaybackBorrowerRecord& record) {
                        return record.active;
                    });
        }
        armed_stop_readiness_diagnostic.natural_completion_commit_attempted = true;
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        auto& live = g_audio_route_state;
        const auto& live_detached = g_onmemory_bank_lifecycle.active();
        natural_completion_facts.route_commit_exact =
            live.generation == snapshot.generation
            && live.controller == snapshot.controller
            && live.lease_identity == snapshot.lease_identity
            && live.custom_resource_owned == snapshot.custom_resource_owned
            && live.owned_slot == snapshot.owned_slot
            && live.owned_bgm == snapshot.owned_bgm
            && live.owned_sound == snapshot.owned_sound
            && live.owned_request_handle == snapshot.owned_request_handle
            && live.stop_retirement.phase == AudioStopRetirementPhase::None
            && g_next_stop_retirement_epoch
                == natural_completion_counter_snapshot;
        natural_completion_facts.detached_commit_exact = live_detached
            && same_onmemory_bank_detached_record(
                live_detached, natural_completion_detached)
            && exact_natural_completion_backing_matches_lifecycle(
                natural_completion_backing, g_onmemory_bank_lifecycle);
        natural_completion_facts.frozen_commit_exact =
            same_frozen_sound_patch_snapshot(
                live.frozen_sound_patch, snapshot.frozen_sound_patch);
        natural_completion_facts.ownership_commit_exact = ownership_commit_exact
            && (g_pause_resume_bank.phase == PauseResumeBankPhase::Idle
                || g_pause_resume_bank.phase == PauseResumeBankPhase::Complete)
            && !g_unpublished_audio_setup
            && g_active_patch_journal.empty()
            && g_pending_play_setup_patch.patches.empty()
            && g_failed_patch_journal.empty();
        natural_completion_facts.registry_commit_exact =
            playback_commit.song == playback_before_stop.song
            && playback_commit.token == playback_before_stop.token
            && cleanup_commit.song == cleanup_before_stop.song
            && cleanup_commit.token == cleanup_before_stop.token;
        const auto commit_failure =
            classify_audio_natural_completion_retirement_commit(
                natural_completion_facts);
        armed_stop_readiness_diagnostic.natural_completion_failure =
            commit_failure;
        if (commit_failure == AudioNaturalCompletionRetirementFailure::None) {
            g_next_stop_retirement_epoch = natural_completion_monitor_epoch;
            live.stop_retirement = natural_completion_monitor;
            live.stop_retirement_epoch = natural_completion_monitor_epoch;
            live.stop_retirement_backing = natural_completion_backing;
            armed_stop_readiness_diagnostic.natural_completion_committed = true;
        }
    }
    bool retirement_monitor_started = false;
    if (controller_matches_route) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const bool route_unchanged = g_audio_route_state.controller == controller
            && g_audio_route_state.generation == snapshot.generation
            && g_audio_route_state.phase == snapshot.phase
            && g_audio_route_state.desired_song_id == snapshot.desired_song_id
            && g_audio_route_state.patched_song_id == snapshot.patched_song_id
            && g_audio_route_state.sound == snapshot.sound;
        if (route_unchanged
            && exact_owned_chain_after_stop
            && (snapshot.phase == AudioRoutePhase::Playing || snapshot.phase == AudioRoutePhase::Armed)) {
            g_audio_route_state.stop_observed = true;
            g_audio_route_state.stop_authorized_generation = snapshot.phase == AudioRoutePhase::Armed
                ? snapshot.generation
                : snapshot.generation + 1;
            g_audio_route_state.set_play_handoff_pending = false;
            stop_evidence_committed = true;
        }
        if (route_unchanged && exact_owned_chain_before_stop) {
            g_audio_route_state.stop_retirement_epoch = 0;
            g_audio_route_state.stop_retirement_backing = {};
            AudioStopRetirementObservation monitor_start =
                retirement_after_stop.observation;
            if (onmemory_bank_retirement_presence_pending(
                    g_audio_route_state.stop_retirement_backing,
                    monitor_start.valid,
                    monitor_start.request_handle_retired)) {
                // Initial absence is expected when native Stop publishes the
                // retired entry asynchronously. It cannot prove retirement.
                monitor_start.request_handle_retired = true;
            }
            retirement_monitor_started = begin_audio_stop_retirement_monitor(
                g_audio_route_state.stop_retirement, monitor_start);
            if (retirement_monitor_started) {
                ++g_next_stop_retirement_epoch;
                if (g_next_stop_retirement_epoch == 0) {
                    ++g_next_stop_retirement_epoch;
                }
                g_audio_route_state.stop_retirement_epoch =
                    g_next_stop_retirement_epoch;
                if (retirement_after_stop.observation.request_handle_retired) {
                    (void)record_onmemory_bank_retired_backing(
                        g_audio_route_state.stop_retirement_backing,
                        {
                            snapshot.generation,
                            snapshot.lease_identity.generation,
                            g_audio_route_state.stop_retirement_epoch,
                            snapshot.owned_request_handle,
                            snapshot.owned_sound_identity.live,
                            true,
                            retirement_after_stop.retired_backing_observed,
                            retirement_after_stop.retired_backing,
                        });
                }
            }
        }
    }

    if (exact_owned_chain_before_stop) {
        log_stop_retirement(
            "after_native_stop",
            retirement_after_stop,
            retirement_monitor_started
                ? AudioStopRetirementPhase::Waiting
                : AudioStopRetirementPhase::None,
            0);
    }

    if (g_audio_route_disabled.load(std::memory_order_acquire)) {
        return;
    }

    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) < 64) {
        std::ostringstream out;
        out << "[audio_sead] slot_stop status=observed_after_original"
            << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
            << " route_controller=0x" << reinterpret_cast<uintptr_t>(snapshot.controller)
            << " sound=0x" << reinterpret_cast<uintptr_t>(snapshot.sound)
            << std::dec
            << " controller_matches_route=" << (controller_matches_route ? 1 : 0)
            << " stop_observed=" << (stop_evidence_committed ? 1 : 0)
            << " route_phase=" << audio_route_phase_name(snapshot.phase)
            << " desired_song_id=" << snapshot.desired_song_id
            << " patched_song_id=" << snapshot.patched_song_id
            << " patch_count=" << patch_count
            << " restore=" << (pending_lifetime_guarded ? "lifetime_unverified" : "not_pending");
        core::log(core::LogLevel::Info, out.str());
    }
    const bool retry_rebuild = controller_matches_route
        && patch_count == 0
        && snapshot.phase == AudioRoutePhase::Armed
        && !snapshot.desired_song_id.empty()
        && g_controller_rebuild_available.load(std::memory_order_acquire);
    if (!retry_rebuild || !uobject_identity_matches(controller, snapshot.controller_identity)) {
        return;
    }
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const SongDescriptor* song = playback.song;
    if (!song || song->id != snapshot.desired_song_id
        || !token_matches_route(playback.token, snapshot)) {
        return;
    }
    const SidecarRuntimeState* sidecar = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        sidecar = find_ready_sidecar_locked(*song);
    }
    if (sidecar) {
        (void)rebuild_armed_controller_route(controller, snapshot.controller_identity, *song, *sidecar);
    }
}

void __fastcall bgm_manager_pause_detour(void* manager, float fade_seconds)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::ManagerPause);
    if (!callback_scope) {
        if (g_original_bgm_manager_pause) g_original_bgm_manager_pause(manager, fade_seconds);
        return;
    }
    AudioRouteState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        snapshot = g_audio_route_state;
    }
    void* manager_slot_before = nullptr;
    void* route_slot = nullptr;
    void* route_bgm = nullptr;
    uint8_t state_before = 0xff;
    int32_t pause_count_before = -1;
    (void)core::safe_read_field(
        manager, runtime_layouts::SqexSeadManager::current_slot, manager_slot_before);
    (void)core::safe_read_field(
        manager, runtime_layouts::SqexSeadManager::pause_count, pause_count_before);
    if (manager_slot_before) {
        (void)core::safe_read_field(
            manager_slot_before, runtime_layouts::SqexSeadSlot::state, state_before);
    }
    (void)read_controller_bgm_chain(snapshot.controller, route_slot, route_bgm);

    if (g_original_bgm_manager_pause) {
        g_original_bgm_manager_pause(manager, fade_seconds);
    }

    void* manager_slot_after = nullptr;
    uint8_t state_after = 0xff;
    int32_t pause_count_after = -1;
    (void)core::safe_read_field(
        manager, runtime_layouts::SqexSeadManager::current_slot, manager_slot_after);
    (void)core::safe_read_field(
        manager, runtime_layouts::SqexSeadManager::pause_count, pause_count_after);
    if (manager_slot_after) {
        (void)core::safe_read_field(
            manager_slot_after, runtime_layouts::SqexSeadSlot::state, state_after);
    }
    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) < 64) {
        std::ostringstream out;
        out << "[audio_sead] manager_pause status=observed_after_original"
            << " manager=0x" << std::hex << reinterpret_cast<uintptr_t>(manager)
            << " manager_slot_before=0x" << reinterpret_cast<uintptr_t>(manager_slot_before)
            << " manager_slot_after=0x" << reinterpret_cast<uintptr_t>(manager_slot_after)
            << " route_controller=0x" << reinterpret_cast<uintptr_t>(snapshot.controller)
            << " route_slot=0x" << reinterpret_cast<uintptr_t>(route_slot)
            << " route_bgm=0x" << reinterpret_cast<uintptr_t>(route_bgm)
            << std::dec
            << " route_slot_matches=" << (route_slot && route_slot == manager_slot_before ? 1 : 0)
            << " state_before=" << static_cast<int>(state_before)
            << " state_after=" << static_cast<int>(state_after)
            << " pause_count_before=" << pause_count_before
            << " pause_count_after=" << pause_count_after
            << " fade_seconds=" << fade_seconds
            << " route_phase=" << audio_route_phase_name(snapshot.phase);
        core::log(core::LogLevel::Info, out.str());
    }
}

void log_slot_transition(const char* transition, void* slot, BgmSlotTransitionFn original)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::SlotTransition);
    if (!callback_scope) {
        if (original) original(slot);
        return;
    }
    AudioRouteState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        snapshot = g_audio_route_state;
    }
    void* bgm_before = nullptr;
    uint64_t request_handle_before = 0;
    uint8_t state_before = 0xff;
    uint8_t flag5c_before = 0xff;
    uint8_t flag5d_before = 0xff;
    (void)core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::bgm, bgm_before);
    (void)core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, state_before);
    (void)core::safe_read_field(slot, 0x5c, flag5c_before);
    (void)core::safe_read_field(slot, 0x5d, flag5d_before);
    if (bgm_before) {
        (void)core::safe_read_field(
            bgm_before, runtime_layouts::SqexSeadBgm::request_handle, request_handle_before);
    }

    if (original) {
        original(slot);
    }

    void* bgm_after = nullptr;
    uint64_t request_handle_after = 0;
    uint8_t state_after = 0xff;
    uint8_t flag5c_after = 0xff;
    uint8_t flag5d_after = 0xff;
    (void)core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::bgm, bgm_after);
    (void)core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, state_after);
    (void)core::safe_read_field(slot, 0x5c, flag5c_after);
    (void)core::safe_read_field(slot, 0x5d, flag5d_after);
    if (bgm_after) {
        (void)core::safe_read_field(
            bgm_after, runtime_layouts::SqexSeadBgm::request_handle, request_handle_after);
    }

    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) < 128) {
        std::ostringstream out;
        out << "[audio_sead] slot_transition status=observed_after_original"
            << " transition=" << (transition ? transition : "?")
            << " slot=0x" << std::hex << reinterpret_cast<uintptr_t>(slot)
            << " bgm_before=0x" << reinterpret_cast<uintptr_t>(bgm_before)
            << " bgm_after=0x" << reinterpret_cast<uintptr_t>(bgm_after)
            << " request_handle_before=0x" << request_handle_before
            << " request_handle_after=0x" << request_handle_after
            << std::dec
            << " tracked_slot=" << (slot && (slot == snapshot.owned_slot || slot == snapshot.reusable_slot) ? 1 : 0)
            << " state_before=" << static_cast<int>(state_before)
            << " state_after=" << static_cast<int>(state_after)
            << " flag5c_before=" << static_cast<int>(flag5c_before)
            << " flag5c_after=" << static_cast<int>(flag5c_after)
            << " flag5d_before=" << static_cast<int>(flag5d_before)
            << " flag5d_after=" << static_cast<int>(flag5d_after)
            << " route_phase=" << audio_route_phase_name(snapshot.phase);
        core::log(core::LogLevel::Info, out.str());
    }
}

void __fastcall bgm_slot_pause_transition_detour(void* slot)
{
    log_slot_transition("pause", slot, g_original_bgm_slot_pause_transition);
}

void __fastcall bgm_slot_resume_transition_detour(void* slot)
{
    log_slot_transition("resume", slot, g_original_bgm_slot_resume_transition);
}

bool read_piano_audio_owner_snapshot(void* owner, PianoAudioOwnerSnapshot& snapshot)
{
    return owner
        && core::safe_read_field(owner, runtime_layouts::PianoAudioOwner::state, snapshot.field08)
        && core::safe_read_field(owner, 0x0c, snapshot.field0c)
        && core::safe_read_field(owner, 0x14, snapshot.field14)
        && core::safe_read_field(owner, 0x4c, snapshot.field4c)
        && core::safe_read_field(owner, runtime_layouts::PianoAudioOwner::packed_key, snapshot.field50)
        && core::safe_read_field(owner, 0x58, snapshot.field58)
        && core::safe_read_field(owner, 0x5c, snapshot.field5c)
        && core::safe_read_field(owner, 0x60, snapshot.field60)
        && core::safe_read_field(owner, 0x64, snapshot.field64)
        && core::safe_read_field(owner, 0x68, snapshot.field68)
        && core::safe_read_field(owner, 0x74, snapshot.field74)
        && core::safe_copy_bytes(reinterpret_cast<uint8_t*>(owner) + 0x7c,
            snapshot.fields7c_to_83.data(), snapshot.fields7c_to_83.size())
        && core::safe_read_field(owner, 0x788, snapshot.field788)
        && core::safe_read_field(owner, 0x78c, snapshot.field78c);
}

bool piano_audio_owner_snapshots_equal(const PianoAudioOwnerSnapshot& left, const PianoAudioOwnerSnapshot& right)
{
    return left.field08 == right.field08
        && left.field0c == right.field0c
        && left.field14 == right.field14
        && left.field4c == right.field4c
        && left.field50 == right.field50
        && left.field58 == right.field58
        && left.field5c == right.field5c
        && left.field60 == right.field60
        && left.field64 == right.field64
        && left.field68 == right.field68
        && left.field74 == right.field74
        && left.fields7c_to_83 == right.fields7c_to_83
        && left.field788 == right.field788;
}

void log_piano_audio_owner_snapshot(const char* status, void* owner, float delta_seconds,
    const PianoAudioOwnerSnapshot& snapshot, AudioRoutePhase route_phase)
{
    uint32_t delta_bits = 0;
    uint64_t fields7c = 0;
    void* predicate_global = nullptr;
    void* predicate_nested = nullptr;
    uint8_t predicate_mode = 0xff;
    uint8_t predicate_state = 0xff;
    const bool predicate_valid = g_exe_module
        && core::safe_read_field(g_exe_module, rva::PianoAudioGlobal, predicate_global)
        && predicate_global
        && core::safe_read_field(predicate_global, 0x70, predicate_mode)
        && core::safe_read_field(predicate_global, 0x280, predicate_nested)
        && predicate_nested
        && core::safe_read_field(predicate_nested, 0x7c0, predicate_state);
    std::memcpy(&delta_bits, &delta_seconds, sizeof(delta_bits));
    std::memcpy(&fields7c, snapshot.fields7c_to_83.data(), sizeof(fields7c));
    std::ostringstream out;
    out << "[audio_sead] piano_audio_owner status=" << (status ? status : "?")
        << " owner=0x" << std::hex << reinterpret_cast<uintptr_t>(owner)
        << " delta_bits=0x" << delta_bits
        << " field08=0x" << static_cast<unsigned>(snapshot.field08)
        << " field0c=0x" << snapshot.field0c
        << " field14=0x" << snapshot.field14
        << " field4c=0x" << snapshot.field4c
        << " field50=0x" << snapshot.field50
        << " field58=0x" << snapshot.field58
        << " field5c=0x" << snapshot.field5c
        << " field60=0x" << static_cast<unsigned>(snapshot.field60)
        << " field64=0x" << snapshot.field64
        << " field68=0x" << static_cast<unsigned>(snapshot.field68)
        << " field74=0x" << snapshot.field74
        << " fields7c=0x" << fields7c
        << " field788=0x" << static_cast<unsigned>(snapshot.field788)
        << " field78c=0x" << snapshot.field78c
        << " predicate_mode=0x" << static_cast<unsigned>(predicate_mode)
        << " predicate_state=0x" << static_cast<unsigned>(predicate_state)
        << std::dec << " route_phase=" << audio_route_phase_name(route_phase);
    if (predicate_valid) {
        out << " predicate_ready=" << ((predicate_mode > 9 && predicate_state <= 1) ? 1 : 0);
    } else {
        out << " predicate_ready=?";
    }
    core::log(core::LogLevel::Info, out.str());
}

void observe_mabf_slot_mode(void* controller, const std::string& song_id)
{
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint8_t slot_state = 0;
    uint32_t requested_mode = 0;
    uint64_t mode_key = 0;
    if (!read_controller_bgm_chain(controller, slot, bgm) ||
        !core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, slot_state) ||
        !core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, sound) ||
        !core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::requested_mode, requested_mode) ||
        !core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::mode_key, mode_key)) {
        return;
    }
    capture_native_mabf_once(sound);

    MabfModeObservation previous;
    bool emit = false;
    bool initial = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        previous = g_mabf_mode_observation;
        initial = !previous.valid;
        emit = initial || previous.controller != controller || previous.slot != slot
            || previous.bgm != bgm || previous.sound != sound
            || previous.requested_mode != requested_mode || previous.mode_key != mode_key
            || previous.slot_state != slot_state;
        g_mabf_mode_observation = {
            controller, slot, bgm, sound, slot_state, requested_mode, mode_key, true,
        };
    }
    if (!emit) {
        return;
    }

    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 256) {
        return;
    }
    const double elapsed_seconds = static_cast<double>(
        g_diagnostic_playback_elapsed_us.load(std::memory_order_acquire)) / 1000000.0;
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << "[audio_sead] mabf_mode_observation status=" << (initial ? "initial" : "changed")
        << " song_id=" << (song_id.empty() ? "unknown" : song_id)
        << " elapsed_seconds=" << elapsed_seconds
        << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(controller)
        << " slot=0x" << reinterpret_cast<uintptr_t>(slot)
        << " bgm=0x" << reinterpret_cast<uintptr_t>(bgm)
        << " sound=0x" << reinterpret_cast<uintptr_t>(sound)
        << " previous_bgm=0x" << reinterpret_cast<uintptr_t>(previous.bgm)
        << " previous_sound=0x" << reinterpret_cast<uintptr_t>(previous.sound)
        << " previous_state=0x" << static_cast<unsigned>(previous.slot_state)
        << " state=0x" << static_cast<unsigned>(slot_state)
        << " previous_requested_mode=0x" << previous.requested_mode
        << " requested_mode=0x" << requested_mode
        << " previous_mode_key=0x" << previous.mode_key
        << " mode_key=0x" << mode_key
        << std::dec << " semantic_mapping=mode0_click_mode1_clean_mode2_clean";
    core::log(core::LogLevel::Info, out.str());
}

void __fastcall piano_adaptive_judgment_detour(void* adaptive_state, void* event)
{
    AudioCallbackScope callback(AudioRouteCallbackKind::AdaptiveJudgment);
    int32_t tier_before = 0;
    int32_t total_before = 0;
    int16_t streak_before = 0;
    uint8_t judgment = 0;
    const bool before_valid = adaptive_state
        && core::safe_read_field(adaptive_state, 0x5c, tier_before)
        && core::safe_read_field(adaptive_state, 0x58, total_before)
        && core::safe_read_field(adaptive_state, 0x60, streak_before);
    const bool judgment_valid = event && core::safe_read_field(event, 0x4a, judgment);

    if (g_original_piano_adaptive_judgment) {
        g_original_piano_adaptive_judgment(adaptive_state, event);
    }
    if (!callback) return;

    int32_t tier_after = 0;
    int32_t total_after = 0;
    int16_t streak_after = 0;
    uint16_t peak_streak = 0;
    const bool after_valid = adaptive_state
        && core::safe_read_field(adaptive_state, 0x5c, tier_after)
        && core::safe_read_field(adaptive_state, 0x58, total_after)
        && core::safe_read_field(adaptive_state, 0x60, streak_after)
        && core::safe_read_field(adaptive_state, 0x62, peak_streak);

    static std::atomic_uint32_t s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 512) return;
    const PlaybackSnapshot snapshot = registry().playback_snapshot();
    std::ostringstream out;
    out << "[audio_sead] adaptive_judgment status=observed"
        << " song_id=" << (snapshot.song ? snapshot.song->id : "<none>")
        << " difficulty=" << (snapshot.profile ? snapshot.profile->difficulty : 0)
        << " judgment_valid=" << judgment_valid
        << " judgment=" << static_cast<unsigned>(judgment)
        << " before_valid=" << before_valid
        << " tier_before=" << tier_before
        << " total_before=" << total_before
        << " streak_before=" << streak_before
        << " after_valid=" << after_valid
        << " tier_after=" << tier_after
        << " total_after=" << total_after
        << " streak_after=" << streak_after
        << " peak_streak=" << peak_streak
        << " candidate_mode=" << (tier_after < 0 ? "Mode0" : tier_after == 0 ? "Mode1" : "Mode2");
    core::log(core::LogLevel::Info, out.str());
}

void __fastcall piano_audio_state_tick_detour(void* owner, float delta_seconds)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::PianoAudioTick);
    if (!callback_scope) {
        if (g_original_piano_audio_state_tick) g_original_piano_audio_state_tick(owner, delta_seconds);
        return;
    }
    // Declared after callback_scope and before the route lock: reverse
    // destruction runs this after route unlock but while callback lifetime is held.
    OnMemoryBankDiagnosticPair post_retirement_diagnostic;
    OnMemoryBankReleaseAction post_retirement_release;
    PauseResumeBankHoldProbe pause_hold_probe;
    PauseResumeReleaseAuthorityProbe pause_release_probe;
    PauseResumeBankMarker pause_marker;
    PauseResumeBankMarkerBatch pause_marker_batch;
    bool installed_pause_marker_batch = false;
    OnMemoryBankPendingObservation pending_bank_observation;
    bool has_pending_bank_observation = false;
    auto deferred_onmemory_diagnostic = make_deferred_noexcept_action([&]() noexcept {
        if (pause_hold_probe.eligible) {
            const uint32_t canonical_kind = lookup_onmemory_bank_kind_noexcept(
                pause_hold_probe.detached.canonical.encode());
            const uint32_t custom_kind = lookup_onmemory_bank_kind_noexcept(
                pause_hold_probe.detached.custom.encode());
            const auto exact_probe_locked = [&]() {
                const auto& active = g_onmemory_bank_lifecycle.active();
                return active
                    && active.phase == OnMemoryBankLifecyclePhase::RestoreApplied
                    && active.ordinal == pause_hold_probe.detached.ordinal
                    && active.sound == pause_hold_probe.detached.sound
                    && active.canonical.encode()
                        == pause_hold_probe.detached.canonical.encode()
                    && active.custom.encode()
                        == pause_hold_probe.detached.custom.encode()
                    && g_onmemory_bank_lifecycle.state_epoch()
                        == pause_hold_probe.lifecycle_state_epoch
                    && g_pause_resume_bank.phase
                        == PauseResumeBankPhase::RetirementCandidate
                    && g_pause_resume_bank.detached.ordinal
                        == pause_hold_probe.detached.ordinal
                    && g_pause_resume_bank.detached.request_handle
                        == pause_hold_probe.detached.request_handle;
            };
            const auto hold = coordinate_pause_resume_initial_hold(
                canonical_kind == 2, custom_kind == 2,
                [&](bool& exit_requested) {
                    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                    exit_requested = g_pause_resume_bank.exit_requested;
                    return exact_probe_locked();
                },
                [&](const PauseResumeInitialHoldOutcome& hold_outcome) {
                    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                    if (!exact_probe_locked()
                        || hold_outcome.release_ready
                            != g_pause_resume_bank.exit_requested) return false;
                    const auto& active = g_onmemory_bank_lifecycle.active();
                    g_pause_resume_bank.session_epoch =
                        g_pause_resume_bank_session_epoch =
                            next_pause_resume_bank_epoch(
                                g_pause_resume_bank_session_epoch);
                    g_pause_resume_bank.cycle_epoch = 1;
                    g_pause_resume_bank.detached = active;
                    g_pause_resume_bank.lifecycle_state_epoch =
                        g_onmemory_bank_lifecycle.state_epoch();
                    g_pause_resume_bank.selection = pause_hold_probe.selection;
                    g_pause_resume_bank.retirement_facts = pause_hold_probe.facts;
                    g_pause_resume_bank.lease_identity = pause_hold_probe.lease_identity;
                    g_pause_resume_bank.controller = pause_hold_probe.controller;
                    g_pause_resume_bank.controller_identity =
                        pause_hold_probe.controller_identity;
                    if (hold_outcome.release_ready) {
                        const auto scheduled = coordinate_onmemory_bank_retirement(
                            g_onmemory_bank_lifecycle,
                            pause_hold_probe.facts,
                            g_onmemory_bank_diagnostic_pair,
                            pause_hold_probe.facts.route_generation,
                            pause_hold_probe.facts.cleanup_generation);
                        post_retirement_diagnostic = scheduled.diagnostic;
                        post_retirement_release = scheduled.release;
                        g_pause_resume_bank.phase = scheduled.claimed
                            ? PauseResumeBankPhase::ReleaseRequested
                            : PauseResumeBankPhase::Failed;
                        pause_marker = {scheduled.claimed
                                ? PauseResumeBankMarkerStatus::ReleaseRequested
                                : PauseResumeBankMarkerStatus::Failed,
                            scheduled.claimed ? "initial_exit" : "initial_exit_gate",
                            g_pause_resume_bank.session_epoch,
                            g_pause_resume_bank.cycle_epoch, true};
                        return scheduled.claimed;
                    } else {
                        g_pause_resume_bank.phase = hold_outcome.phase;
                        pause_marker = {PauseResumeBankMarkerStatus::SuspendedReady,
                            "initial_retirement", g_pause_resume_bank.session_epoch,
                            g_pause_resume_bank.cycle_epoch, true};
                        return true;
                    }
                },
                [&]() {
                    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                    g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
                    const auto scheduled = coordinate_onmemory_bank_retirement(
                        g_onmemory_bank_lifecycle,
                        pause_hold_probe.facts,
                        g_onmemory_bank_diagnostic_pair,
                        pause_hold_probe.facts.route_generation,
                        pause_hold_probe.facts.cleanup_generation);
                    post_retirement_diagnostic = scheduled.diagnostic;
                    post_retirement_release = scheduled.release;
                    pause_marker = {PauseResumeBankMarkerStatus::Failed,
                        "initial_hold_proof", 0, 0, true};
                });
            (void)hold;
        }
        if (pause_release_probe.eligible) {
            (void)coordinate_pause_resume_release_probe(
                pause_release_probe, &post_retirement_diagnostic,
                &post_retirement_release, &pause_marker);
        }
        log_onmemory_bank_pair(
            "post_request_retirement", post_retirement_diagnostic);
        if (post_retirement_diagnostic) {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            (void)g_onmemory_bank_diagnostic_pair.erase_exact(
                post_retirement_diagnostic);
        }
        if (post_retirement_release
            && bgm_aggregate_release_has_borrowers(post_retirement_release)) {
            observe_bgm_playback_release(post_retirement_release, false);
            defer_bgm_aggregate_release(post_retirement_release);
            observe_bgm_playback_aggregate(false, false);
            post_retirement_release = {};
        }
        if (post_retirement_release) {
            observe_bgm_playback_release(post_retirement_release, false);
            observe_bgm_playback_aggregate(false, false);
            OnMemoryBankReleaseExecution execution;
            const bool release_executed =
                execute_pause_resume_deferred_action_once(true, [&]() {
                execution = execute_onmemory_bank_release(
                    g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
                    g_onmemory_bank_release_available.load(std::memory_order_acquire),
                    !g_audio_route_installed.load(std::memory_order_acquire)
                        || g_audio_route_disabled.load(std::memory_order_acquire),
                    post_retirement_release,
                    [](const uint64_t token) noexcept {
                        return lookup_onmemory_bank_kind_noexcept(token);
                    },
                    [](const uint64_t* token, const uint8_t asynchronous) noexcept {
                        return release_onmemory_bank_async_noexcept(token, asynchronous);
                    });
                return true;
            });
            if (!release_executed) return;
            OnMemoryBankDetachedRecord record;
            bool finished = false;
            {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                finished = g_onmemory_bank_lifecycle.finish_release(
                    post_retirement_release, execution.outcome);
                record = g_onmemory_bank_lifecycle.active();
                if (finished
                    && g_pause_resume_bank.phase
                        == PauseResumeBankPhase::ReleaseRequested
                    && record.ordinal == g_pause_resume_bank.detached.ordinal) {
                    g_pause_resume_bank.phase =
                        execution.outcome == OnMemoryBankReleaseOutcome::AlreadyAbsent
                        ? PauseResumeBankPhase::Complete
                        : execution.outcome
                            == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                            ? PauseResumeBankPhase::ReleasePending
                            : PauseResumeBankPhase::Failed;
                    pause_marker = {
                        g_pause_resume_bank.phase == PauseResumeBankPhase::Complete
                            ? PauseResumeBankMarkerStatus::ReleaseComplete
                            : execution.outcome
                                == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                                ? PauseResumeBankMarkerStatus::ReleaseRequested
                                : PauseResumeBankMarkerStatus::Failed,
                        execution.outcome == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                            ? "async_release" : "release_finished",
                        g_pause_resume_bank.session_epoch,
                        g_pause_resume_bank.cycle_epoch,
                        true};
                }
            }
            if (finished && execution.outcome != OnMemoryBankReleaseOutcome::Failed) {
                observe_bgm_playback_release(post_retirement_release, true);
                observe_bgm_playback_aggregate(false, false);
            }
            log_onmemory_lifecycle(
                finished ? "release_finished" : "release_stale",
                record.phase,
                post_retirement_release.route_generation,
                post_retirement_release.cleanup_generation,
                post_retirement_release.ordinal,
                execution.outcome == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                    ? "async_requested"
                    : execution.outcome == OnMemoryBankReleaseOutcome::AlreadyAbsent
                        ? "already_absent" : "failed");
        }
        if (has_pending_bank_observation) {
            const uint32_t canonical_kind = lookup_onmemory_bank_kind_noexcept(
                pending_bank_observation.action.canonical.encode());
            const uint32_t custom_kind = lookup_onmemory_bank_kind_noexcept(
                pending_bank_observation.action.custom.encode());
            OnMemoryBankDetachedRecord record;
            bool observed = false;
            {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                observed = g_onmemory_bank_lifecycle.observe(
                    pending_bank_observation, canonical_kind, custom_kind);
                record = g_onmemory_bank_lifecycle.active();
                if (observed
                    && g_pause_resume_bank.phase
                        == PauseResumeBankPhase::ReleasePending
                    && record.ordinal == g_pause_resume_bank.detached.ordinal
                    && record.phase == OnMemoryBankLifecyclePhase::Complete) {
                    g_pause_resume_bank.phase = PauseResumeBankPhase::Complete;
                    pause_marker = {PauseResumeBankMarkerStatus::ReleaseComplete,
                        "custom_absent", g_pause_resume_bank.session_epoch,
                        g_pause_resume_bank.cycle_epoch, true};
                }
            }
            if (observed && (record.phase == OnMemoryBankLifecyclePhase::Complete
                    || record.phase == OnMemoryBankLifecyclePhase::Failed)) {
                log_onmemory_lifecycle(
                    "residency_observed",
                    record.phase,
                    record.route_generation,
                    record.cleanup_generation,
                    record.ordinal,
                    record.phase == OnMemoryBankLifecyclePhase::Complete
                        ? "custom_absent" : "observation_failed",
                    canonical_kind,
                    custom_kind);
            }
            if (observed
                && record.phase == OnMemoryBankLifecyclePhase::Complete) {
                observe_bgm_playback_release(
                    pending_bank_observation.action, true);
                complete_bgm_aggregate_exit_cleanup(record);
            }
        }
        if (installed_pause_marker_batch) {
            drain_pause_resume_marker_batch(
                static_cast<uint32_t>(pause_marker_batch.count),
                [&](const uint32_t index) {
                    log_pause_resume_bank_marker(pause_marker_batch.records[index]);
                });
        }
        log_pause_resume_bank_marker(pause_marker);
    });
    std::lock_guard<std::recursive_mutex> route_operation_lock(g_audio_route_operations.mutex());
    void* const previous_owner = g_piano_audio_owner_tick;
    const uint64_t previous_tick_nonce = g_piano_audio_owner_tick_nonce;
    if (previous_tick_nonce == 0) {
        g_piano_audio_owner_tick_nonce_counter =
            next_pause_resume_bank_epoch(g_piano_audio_owner_tick_nonce_counter);
        g_piano_audio_owner_tick_nonce = g_piano_audio_owner_tick_nonce_counter;
    }
    const bool previous_custom = g_piano_audio_owner_custom_playsetup;
    PauseResumeBankMarkerBatch* const previous_marker_batch =
        g_pause_resume_bank_marker_batch;
    if (!previous_marker_batch) {
        g_pause_resume_bank_marker_batch = &pause_marker_batch;
        installed_pause_marker_batch = true;
    }
    g_piano_audio_owner_tick = owner;
    g_piano_audio_owner_custom_playsetup = false;
    if (g_original_piano_audio_state_tick) {
        g_original_piano_audio_state_tick(owner, delta_seconds);
    }
    const bool custom_playsetup = g_piano_audio_owner_custom_playsetup;
    bool missing_play_restore = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_pause_resume_bank.phase == PauseResumeBankPhase::ResumeStopObserved
            && g_pause_resume_bank.owner_tick_nonce
                == g_piano_audio_owner_tick_nonce) {
            g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
            pause_marker = {PauseResumeBankMarkerStatus::Failed,
                "owner_tick_missing_set", g_pause_resume_bank.session_epoch,
                g_pause_resume_bank.cycle_epoch, true};
        } else if (g_pause_resume_bank.phase == PauseResumeBankPhase::OwnerRebound
            && g_pause_resume_bank.owner_tick_nonce
                == g_piano_audio_owner_tick_nonce) {
            missing_play_restore = true;
        }
    }
    if (missing_play_restore) {
        const bool restored = restore_pause_resume_owner(&pause_marker);
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
        if (restored) {
            pause_marker = {PauseResumeBankMarkerStatus::Failed,
                "owner_tick_missing_play", g_pause_resume_bank.session_epoch,
                g_pause_resume_bank.cycle_epoch, true};
        }
    }
    g_piano_audio_owner_tick = previous_owner;
    g_piano_audio_owner_tick_nonce = previous_tick_nonce;
    g_piano_audio_owner_custom_playsetup = previous_custom;
    g_pause_resume_bank_marker_batch = previous_marker_batch;

    AudioRoutePhase route_phase = AudioRoutePhase::Idle;
    bool tracked_owner = false;
    std::string clock_song_id;
    void* route_controller = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route_phase = g_audio_route_state.phase;
        if (route_phase == AudioRoutePhase::Playing) {
            clock_song_id = g_audio_route_state.desired_song_id;
            route_controller = g_audio_route_state.controller;
        }
        if (owner && (custom_playsetup || !g_piano_audio_owner)) {
            if (owner != g_piano_audio_owner) {
                g_piano_audio_owner = owner;
                g_piano_audio_owner_snapshot = {};
                g_piano_audio_owner_snapshot_valid = false;
            }
        }
        tracked_owner = owner && owner == g_piano_audio_owner;
    }
    const bool clock_active = tracked_owner && route_phase == AudioRoutePhase::Playing;
    const bool clock_was_active = g_diagnostic_playback_clock_active.exchange(clock_active, std::memory_order_acq_rel);
    if (clock_was_active && !clock_active) {
        reset_native_piano_input_diagnostic_held();
    }
    if (clock_active && !clock_was_active) {
        g_diagnostic_playback_elapsed_us.store(0, std::memory_order_release);
        g_diagnostic_playback_log_bucket.store(UINT32_MAX, std::memory_order_release);
        g_completion_memory_log_bucket.store(UINT32_MAX, std::memory_order_release);
    }
    if (clock_active && std::isfinite(delta_seconds) && delta_seconds > 0.0f && delta_seconds <= 0.5f) {
        const auto delta_us = static_cast<uint64_t>(std::llround(static_cast<double>(delta_seconds) * 1000000.0));
        const uint64_t elapsed_us = g_diagnostic_playback_elapsed_us.fetch_add(delta_us, std::memory_order_acq_rel) + delta_us;
        const uint32_t log_bucket = static_cast<uint32_t>(elapsed_us / 5000000ULL);
        if (g_diagnostic_playback_log_bucket.exchange(log_bucket, std::memory_order_acq_rel) != log_bucket) {
            std::ostringstream out;
            out.setf(std::ios::fixed);
            out.precision(3);
            out << "[playback_diag] song_id=" << (clock_song_id.empty() ? "unknown" : clock_song_id)
                << " elapsed_seconds=" << static_cast<double>(elapsed_us) / 1000000.0;
            core::log(core::LogLevel::Info, out.str());
        }
        const uint32_t memory_bucket = elapsed_us < 120000000ULL
            ? static_cast<uint32_t>(elapsed_us / 5000000ULL)
            : 24U + static_cast<uint32_t>((elapsed_us - 120000000ULL) / 1000000ULL);
        if (g_completion_memory_log_bucket.exchange(memory_bucket, std::memory_order_acq_rel) != memory_bucket) {
            log_completion_owner_memory(static_cast<float>(static_cast<double>(elapsed_us) / 1000000.0));
            log_active_chart_memory(static_cast<float>(static_cast<double>(elapsed_us) / 1000000.0));
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        has_pending_bank_observation =
            g_onmemory_bank_lifecycle.copy_pending_observation(
                pending_bank_observation);
    }
    poll_audio_stop_retirement_monitor(
        &post_retirement_diagnostic, &post_retirement_release, &pause_hold_probe);
    poll_pause_resume_bank_retirement(
        &post_retirement_diagnostic, &post_retirement_release, &pause_marker,
        &pause_release_probe);
    observe_bgm_playback_aggregate(false, false);
    if (pause_resume_owner_tick_deferred_admitted(
            static_cast<bool>(post_retirement_diagnostic),
            static_cast<bool>(post_retirement_release),
            has_pending_bank_observation,
            pause_hold_probe.eligible,
            pause_release_probe.eligible,
            pause_marker.eligible,
            static_cast<uint32_t>(pause_marker_batch.count))) {
        deferred_onmemory_diagnostic.make_eligible();
    }
    if (!tracked_owner) {
        return;
    }
    void* observation_controller = route_controller ? route_controller : lookup_current_bgm_controller();
    if (observation_controller) {
        if (clock_song_id.empty()) {
            const PlaybackSnapshot snapshot = registry().playback_snapshot();
            if (snapshot.song) clock_song_id = snapshot.song->id;
        }
        observe_mabf_slot_mode(observation_controller, clock_song_id);
    }

    PianoAudioOwnerSnapshot snapshot;
    if (!read_piano_audio_owner_snapshot(owner, snapshot)) {
        if (custom_playsetup) {
            core::log(core::LogLevel::Error, "[audio_sead] piano_audio_owner status=capture_failed");
        }
        return;
    }
    bool changed = false;
    bool first_snapshot = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (owner != g_piano_audio_owner) {
            return;
        }
        first_snapshot = !g_piano_audio_owner_snapshot_valid;
        changed = g_piano_audio_owner_snapshot_valid
            && !piano_audio_owner_snapshots_equal(g_piano_audio_owner_snapshot, snapshot);
        g_piano_audio_owner_snapshot = snapshot;
        g_piano_audio_owner_snapshot_valid = true;
    }
    if (custom_playsetup || first_snapshot || changed) {
        static std::atomic_int s_logs{0};
        if (s_logs.fetch_add(1, std::memory_order_relaxed) < 128) {
            const char* status = custom_playsetup ? "captured_after_playsetup"
                : (first_snapshot ? "captured_initial" : "changed");
            log_piano_audio_owner_snapshot(status,
                owner, delta_seconds, snapshot, route_phase);
        }
    }
}

void log_pending_audio_hooks(HMODULE exe_module, size_t sidecars_ready)
{
    std::ostringstream create_package;
    create_package << "[audio_sead_hook] seam=CreatePackage status=fail_closed";
    append_signature_status(create_package, "create_package", exe_module, "create_package");
    create_package
        << " reason=construct_detour_not_complete";
    core::log(core::LogLevel::Info, create_package.str());

    std::ostringstream construct_object;
    construct_object << "[audio_sead_hook] seam=StaticConstructObject status=fail_closed";
    append_signature_status(construct_object, "static_construct_object", exe_module, "static_construct_object");
    construct_object
        << " reason=object_lifetime_and_rollback_not_proven";
    core::log(core::LogLevel::Info, construct_object.str());

    std::ostringstream controller;
    controller << "[audio_sead_hook] seam=live_controller status=fail_closed";
    append_signature_status(controller, "stop", exe_module, "bgm_slot_stop");
    append_signature_status(controller, "playback_transfer", exe_module,
        "bgm_playback_transfer");
    append_signature_status(controller, "set", exe_module, "bgm_slot_set");
    append_signature_status(controller, "play", exe_module, "bgm_slot_play");
    append_signature_status(controller, "prepare", exe_module, "bgm_prepare");
    append_signature_status(controller, "controller_lookup", exe_module, "bgm_controller_lookup");
    append_signature_status(controller, "manager_pause", exe_module, "bgm_manager_pause");
    append_signature_status(controller, "slot_pause_transition", exe_module, "bgm_slot_pause_transition");
    append_signature_status(controller, "slot_resume_transition", exe_module, "bgm_slot_resume_transition");
    append_signature_status(controller, "slot_setup", exe_module, "bgm_slot_setup");
    append_signature_status(controller, "piano_audio_state_tick", exe_module, "piano_audio_state_tick");
    append_signature_status(controller, "piano_audio_request", exe_module, "piano_audio_request");
    controller
        << " sidecars_ready=" << sidecars_ready
        << " sound_patch_rollback=" << (g_sound_patch_rollback.active() ? "active" : "ready")
        << " controller_patch_rollback=" << (g_controller_patch_rollback.active() ? "active" : "ready")
        << " live_detours=" << (kEnableLiveSqexSeadDetours ? "enabled" : "disabled")
        << " controller_rebuild=" << (g_controller_rebuild_available.load(std::memory_order_acquire) ? "enabled" : "disabled")
        << " controller_lookup=" << (g_controller_lookup_available.load(std::memory_order_acquire) ? "enabled" : "disabled")
        << " slot_setup=" << (g_slot_setup_available.load(std::memory_order_acquire) ? "enabled" : "disabled")
        << " piano_audio_request=" << (g_piano_audio_request_available.load(std::memory_order_acquire) ? "enabled" : "disabled");
    core::log(core::LogLevel::Info, controller.str());
}

} // namespace

bool prepare_audio_catalog_from_prefix(
    std::shared_ptr<const SongRegistryStorage> storage,
    std::shared_ptr<const PreparedAudioPrefix> prefix,
    PreparedAudioCatalog& prepared) noexcept
{
    if (!storage || !prefix || !prepared_audio_prefix_matches(*prefix, *storage)) return false;
    prepared.storage = std::move(storage);
    prepared.prefix = std::move(prefix);
    return true;
}

class PreparedAudioCatalogCommit final {
public:
    PreparedAudioCatalog* prepared = nullptr;
    std::unique_lock<std::mutex> lock;
};

bool try_audio_catalog_aggregate_ready(const bool audio_state_lock_held) noexcept
{
    BgmPlaybackAggregateExitOwnershipFacts facts;
    try {
        facts.unresolved_publication =
            g_bgm_aggregate_unresolved_publications.load(std::memory_order_acquire) != 0;
        bool rollback_slots_free = true;
        for (const auto& slot : g_bgm_aggregate_rollback_slots) {
            if (slot.state.load(std::memory_order_acquire)
                != AggregateRollbackSlotState::Free) {
                rollback_slots_free = false;
                break;
            }
        }
        std::unique_lock durable(g_bgm_aggregate_durable_rollback_mutex,
            std::try_to_lock);
        if (!durable) return false;
        facts.rollback_owned = !bgm_playback_emergency_init_allowed(
            rollback_slots_free, g_bgm_aggregate_durable_rollbacks.empty());
        if (!g_bgm_aggregate_mutation_gate.try_drained()) return false;

        std::unique_lock release(g_bgm_aggregate_release_mutex, std::try_to_lock);
        if (!release) return false;
        const auto release_state = g_bgm_aggregate_release_claim.state;
        facts.release_claim_owned = release_state == AggregateReleaseClaimState::Pending
            || release_state == AggregateReleaseClaimState::InFlight
            || release_state == AggregateReleaseClaimState::Retained;

        std::unique_lock<std::mutex> audio;
        if (!audio_state_lock_held) {
            audio = std::unique_lock<std::mutex>(g_audio_state_mutex, std::try_to_lock);
            if (!audio) return false;
        }
        facts.route_or_cleanup_owned = g_audio_route_state.custom_resource_owned
            || g_audio_route_state.list_cleanup_pending
            || g_frozen_profile_lease.active()
            || static_cast<bool>(g_onmemory_bank_lifecycle.active());

        std::unique_lock borrowers(g_bgm_playback_borrower_mutex, std::try_to_lock);
        if (!borrowers) return false;
        for (const auto& record : g_bgm_playback_borrowers) {
            if (record.active) {
                facts.active_or_retained_borrower = true;
                break;
            }
        }
        return bgm_playback_aggregate_menu_ready(facts,
            g_bgm_aggregate_exit_requested.load(std::memory_order_acquire),
            g_bgm_aggregate_exit_pending.load(std::memory_order_acquire));
    } catch (...) { return false; }
}

std::shared_ptr<PreparedAudioCatalogCommit> begin_prepared_audio_catalog_commit(
    PreparedAudioCatalog& prepared, const uint64_t expected_catalog_revision,
    const AudioProductionCatalogScope& callback_scope) noexcept
{
    if (!callback_scope || !prepared.storage
        || !g_audio_route_installed.load(std::memory_order_acquire)
        || g_audio_route_disabled.load(std::memory_order_acquire)
        || !try_audio_catalog_aggregate_ready(false)) return {};
    try {
        auto commit = std::make_shared<PreparedAudioCatalogCommit>();
        commit->lock = std::unique_lock<std::mutex>(g_audio_state_mutex, std::try_to_lock);
        if (!commit->lock
            || !g_audio_route_installed.load(std::memory_order_acquire)
            || g_audio_route_disabled.load(std::memory_order_acquire)
            || g_audio_route_state.phase != AudioRoutePhase::Idle
            || g_sidecar_catalog.catalog_revision != expected_catalog_revision
            || !try_audio_catalog_aggregate_ready(true)) return {};
        commit->prepared = &prepared;
        return commit;
    } catch (...) {
        return {};
    }
}

void commit_prepared_audio_catalog(PreparedAudioCatalogCommit& commit,
    const uint64_t generation, const uint64_t catalog_revision) noexcept
{
    PreparedAudioCatalog& prepared = *commit.prepared;
    g_sidecar_catalog = {generation, catalog_revision, prepared.storage};
    g_sidecar_prefix.swap(prepared.prefix);
}

void finalize_prepared_audio_catalog_commit(
    PreparedAudioCatalogCommit& commit) noexcept
{
    commit.prepared->prefix.reset();
    commit.prepared->storage.reset();
    if (commit.lock) commit.lock.unlock();
}

void audio_production_outer_callback_entered() noexcept
{
    drain_audio_route_transition_diagnostics_noexcept();
}

void audio_production_outer_callback_exited(const bool entered) noexcept
{
    if (cleanup_only_post_callback_observation_required(entered, 0)) {
        observe_cleanup_only_onmemory_bank_noexcept();
    }
}

#include "game/audio_selection_activation.inc" // catalog consumer: "persistent_chart_expand_caller"

void block_custom_audio_route_for_unresolved_chart_mutation() noexcept
{
    g_audio_route_disabled.store(true, std::memory_order_release);
    try {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_audio_route_state.list_cleanup_pending = true;
    } catch (...) {
    }
}

AudioRouteArmLease arm_audio_route_for_song_id(const std::string& song_id)
{
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::Arm);
    const auto aggregate_ownership = snapshot_bgm_aggregate_exit_ownership(
        false, false);
    if (!bgm_playback_aggregate_menu_ready(aggregate_ownership,
            g_bgm_aggregate_exit_requested.load(std::memory_order_acquire),
            g_bgm_aggregate_exit_pending.load(std::memory_order_acquire))) {
        log_bgm_aggregate_mutation(
            "new_song_blocked", "exit_cleanup_pending", 0, 0, 0);
        return {AudioRouteArmStatus::Failed, {}};
    }
    if (!callback_scope) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        invalidate_unpublished_audio_setup_locked(
            AudioRouteTransitionReason::AdmissionCancel);
        return g_frozen_profile_lease.active()
            ? AudioRouteArmLease{AudioRouteArmStatus::Retained, g_frozen_profile_lease.identity()}
            : AudioRouteArmLease{AudioRouteArmStatus::Failed, {}};
    }
    std::unique_lock<std::recursive_mutex> operation_lock(g_audio_route_operations.mutex());
    if (song_id.empty()) {
        return {AudioRouteArmStatus::Failed, {}};
    }
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_frozen_profile_lease.active()) {
            const AudioRouteLeaseIdentity retained_identity = g_frozen_profile_lease.identity();
            invalidate_unpublished_audio_setup_locked(
                AudioRouteTransitionReason::ArmSetupInvalidated);
            if (g_audio_route_disabled.load(std::memory_order_acquire)
                || !g_audio_route_installed.load(std::memory_order_acquire)) {
                (void)g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::FeatureDisabled, retained_identity);
            }
            return {AudioRouteArmStatus::Retained, retained_identity};
        }
        if (native_audio_route_owned_locked() || g_audio_route_state.list_cleanup_pending) {
            invalidate_unpublished_audio_setup_locked(
                AudioRouteTransitionReason::ArmSetupInvalidated);
            const AudioRouteLeaseIdentity retained_identity{
                ++g_next_audio_route_generation,
                audio_route_song_key(g_audio_route_state.desired_song_id.empty()
                    ? song_id : g_audio_route_state.desired_song_id),
            };
            g_audio_route_state.lease_identity = retained_identity;
            g_frozen_profile_lease.acquire(retained_identity);
            (void)g_frozen_profile_lease.mark_native_arm_attempt(retained_identity);
            return {AudioRouteArmStatus::Retained, retained_identity};
        }
    }
    if (!g_audio_route_installed.load(std::memory_order_acquire)) {
        return {AudioRouteArmStatus::NoRoute, {}};
    }
    if (g_audio_route_disabled.load(std::memory_order_acquire)) {
        return {AudioRouteArmStatus::Disabled, {}};
    }
    const SelectionSnapshot selection = registry().selection_snapshot();
    const SongDescriptor* song = selection.song;
    if (!song || song->id != song_id) {
        core::log(core::LogLevel::Error, "[audio_sead] arm status=failed reason=active_song_mismatch");
        return {AudioRouteArmStatus::Failed, {}};
    }
    const SidecarRuntimeState* sidecar = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        sidecar = find_ready_sidecar_locked(*song);
    }
    if (!sidecar) {
        core::log(core::LogLevel::Error, "[audio_sead] arm status=failed reason=sidecar_not_ready");
        return {AudioRouteArmStatus::Failed, {}};
    }
    // Arm only proves route-wide lifecycle capacity and callable availability.
    // Canonical ownership belongs to the actual sound supplied to PlaySetup.
    const bool sidecar_capacity_ready = sidecar->allocation.sead_header()
        && sidecar->allocation.allocation_size() != 0
        && sidecar->allocation.mabf_size() != 0;
    OnMemoryBankRouteDecision bank_arm_decision;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        bank_arm_decision = (g_onmemory_bank_cleanup_only.blocks_custom_routes()
                                || g_custom_activation_quarantine)
            ? OnMemoryBankRouteDecision::ReleasePending
            : g_onmemory_bank_lifecycle.preflight_arm(
                true,
                g_audio_route_installed.load(std::memory_order_acquire),
                g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
                g_onmemory_bank_release_available.load(std::memory_order_acquire));
    }
    if (!sidecar_capacity_ready
        || bank_arm_decision != OnMemoryBankRouteDecision::Allowed) {
        return {AudioRouteArmStatus::NoRoute, {}};
    }

    void* lookup_controller = lookup_current_bgm_controller();
    UObjectIdentity lookup_controller_identity;
    const bool lookup_controller_valid = lookup_controller
        && read_uobject_identity(lookup_controller, lookup_controller_identity);
    void* rebuild_controller = nullptr;
    UObjectIdentity rebuild_controller_identity;
    AudioRouteState route_before_arm;
    uint64_t arm_generation = 0;
    AudioRouteLeaseIdentity lease_identity{};
    bool stop_observed = false;
    uint64_t authorized_generation = 0;
    bool arm_proof_attempted = false;
    ControllerIdentityProof arm_attempted_proof{};
    bool arm_bind_succeeded = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        route_before_arm = g_audio_route_state;
        const auto old_transition_state =
            capture_audio_route_transition_projection_locked();
        if (lookup_controller_valid) {
            const bool same_retained_controller = g_audio_route_state.controller == lookup_controller
                && uobject_identity_matches(lookup_controller, g_audio_route_state.controller_identity);
            if (g_audio_route_state.controller && !same_retained_controller) {
                g_audio_route_state.stop_observed = false;
                g_audio_route_state.stop_authorized_generation = 0;
                g_audio_route_state.set_play_handoff_pending = false;
                g_audio_route_state.reusable_sound = nullptr;
                g_audio_route_state.reusable_sound_identity = {};
            }
            g_audio_route_state.controller = lookup_controller;
            g_audio_route_state.controller_identity = lookup_controller_identity;
        } else {
            g_audio_route_state.controller = nullptr;
            g_audio_route_state.controller_identity = {};
            g_audio_route_state.stop_observed = false;
            g_audio_route_state.stop_authorized_generation = 0;
            g_audio_route_state.set_play_handoff_pending = false;
            g_audio_route_state.reusable_sound = nullptr;
            g_audio_route_state.reusable_sound_identity = {};
            g_audio_route_state.reusable_slot = nullptr;
            g_audio_route_state.reusable_bgm = nullptr;
            g_audio_route_state.list_cleanup_pending = false;
            g_audio_route_state.reusable_slot = nullptr;
            g_audio_route_state.reusable_bgm = nullptr;
        }
        rebuild_controller = g_audio_route_state.controller;
        rebuild_controller_identity = g_audio_route_state.controller_identity;
        ++g_audio_route_state.generation;
        lease_identity = {
            ++g_next_audio_route_generation,
            audio_route_song_key(song_id),
        };
        g_audio_route_state.lease_identity = lease_identity;
        g_audio_route_state.frozen_sound_patch = {};
        g_audio_route_state.stop_retirement = {};
        g_audio_route_state.stop_retirement_epoch = 0;
        g_audio_route_state.stop_retirement_backing = {};
        g_audio_route_state.phase = AudioRoutePhase::Armed;
        g_audio_route_state.desired_song_id = song_id;
        arm_generation = g_audio_route_state.generation;
        g_frozen_profile_lease.acquire(lease_identity);
        stop_observed = g_audio_route_state.stop_observed;
        authorized_generation = g_audio_route_state.stop_authorized_generation;
        g_unpublished_audio_setup = {
            selection,
            custom_context_token(selection.generation, g_audio_route_state),
        };
        g_audio_route_state.controller_arm_proof_attempted = false;
        g_audio_route_state.controller_arm_attempted_proof = {};
        g_audio_route_state.controller_arm_bind_succeeded = false;
        if (rebuild_controller) {
            arm_proof_attempted = true;
            arm_attempted_proof =
                controller_identity_proof(
                    rebuild_controller, rebuild_controller_identity);
            arm_bind_succeeded =
                g_unpublished_audio_setup.bind_route_controller(
                    arm_attempted_proof);
            g_audio_route_state.controller_arm_proof_attempted = arm_proof_attempted;
            g_audio_route_state.controller_arm_attempted_proof = arm_attempted_proof;
            g_audio_route_state.controller_arm_bind_succeeded = arm_bind_succeeded;
        }
        enqueue_audio_route_transition_locked(
            AudioRouteTransitionReason::ArmRoutePublished,
            AudioRouteTransitionKind::Combined, old_transition_state);
    }
    static std::atomic_int s_logs{0};
    const int log_index = s_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 64) {
        std::ostringstream out;
        out << "[audio_sead] arm status=" << (rebuild_controller ? "armed_for_controller_rebuild" : "armed_waiting_for_playsetup")
            << " song_id=" << song_id
            << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(rebuild_controller) << std::dec
            << " lookup_controller=0x" << std::hex << reinterpret_cast<uintptr_t>(lookup_controller) << std::dec
            << " lookup_valid=" << (lookup_controller_valid ? 1 : 0)
            << " arm_proof_attempted="
            << (arm_proof_attempted ? 1 : 0)
            << " arm_bind_succeeded=" << (arm_bind_succeeded ? 1 : 0)
            << " arm_capture_reason="
            << (rebuild_controller_identity.live_capture_attempted
                    ? uobject_live_handle_capture_result_name(
                        rebuild_controller_identity.live_capture_result)
                    : "not_attempted")
            << " arm_zero_serial_capture_reason="
            << (rebuild_controller_identity
                        .item_backed_zero_serial_capture_attempted
                    ? uobject_item_backed_zero_serial_capture_result_name(
                        rebuild_controller_identity
                            .item_backed_zero_serial_capture_result)
                    : "not_attempted")
            << " generation=" << arm_generation
            << " stop_observed=" << (stop_observed ? 1 : 0)
            << " authorized_generation=" << authorized_generation;
        if (arm_proof_attempted) {
            const ControllerIdentityProofFacts facts =
                controller_identity_proof_facts(arm_attempted_proof);
            out << " arm_proof_valid=" << (facts.valid ? 1 : 0)
                << " arm_proof_mode="
                << controller_identity_proof_mode_name(facts.mode)
                << " arm_raw_index_readable="
                << (facts.raw_internal_index_readable ? 1 : 0)
                << " arm_raw_index_state="
                << controller_identity_raw_index_state_name(
                    facts.raw_internal_index_state)
                << " arm_raw_index=" << facts.raw_internal_index
                << " arm_live_capture_succeeded="
                << (facts.live_capture_succeeded ? 1 : 0)
                << " arm_live_index=" << facts.live_internal_index
                << " arm_live_index_valid="
                << (facts.live_internal_index_valid ? 1 : 0)
                << " arm_live_serial_valid="
                << (facts.live_serial_valid ? 1 : 0);
        }
        core::log(core::LogLevel::Info, out.str());
    }
    PianoAudioRequestProfile request_profile;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_piano_audio_request_profile.ready
            && g_piano_audio_request_profile.base_slot == song->base_slot) {
            request_profile = g_piano_audio_request_profile;
        }
    }
    if (request_profile.ready) {
        void* current_owner = nullptr;
        void* current_slot = nullptr;
        void* current_bgm = nullptr;
        void* current_sound = nullptr;
        uint64_t current_request_handle = 0;
        uint8_t current_slot_state = 0xff;
        uint8_t owner_state = 0xff;
        uint8_t live_request_index = 0;
        uint64_t live_packed_key = 0;
        const bool request_safe = g_piano_audio_request_available.load(std::memory_order_acquire)
            && request_profile.captured_song_id == song_id
            && lookup_current_piano_audio_owner(current_owner)
            && current_owner == request_profile.owner
            && core::safe_read_field(current_owner, runtime_layouts::PianoAudioOwner::state, owner_state)
            && owner_state == 0
            && core::safe_read_field(
                current_owner, runtime_layouts::PianoAudioOwner::request_index, live_request_index)
            && live_request_index == request_profile.request_index
            && core::safe_read_field(
                current_owner, runtime_layouts::PianoAudioOwner::packed_key, live_packed_key)
            && live_packed_key == request_profile.packed_key
            && request_profile.packed_key != 0
            && request_profile.route_generation == route_before_arm.generation
            && request_profile.lease_identity == route_before_arm.lease_identity
            && request_profile.controller == route_before_arm.controller
            && request_profile.slot == route_before_arm.owned_slot
            && request_profile.bgm == route_before_arm.owned_bgm
            && request_profile.request_handle == route_before_arm.owned_request_handle
            && route_before_arm.custom_resource_owned
            && route_before_arm.desired_song_id == song_id
            && uobject_identity_matches(
                request_profile.controller, request_profile.controller_identity)
            && read_controller_bgm_chain(request_profile.controller, current_slot, current_bgm)
            && core::safe_read_field(current_bgm, runtime_layouts::SqexSeadBgm::sound, current_sound)
            && core::safe_read_field(current_bgm, runtime_layouts::SqexSeadBgm::request_handle, current_request_handle)
            && core::safe_read_field(current_slot, runtime_layouts::SqexSeadSlot::state, current_slot_state)
            && current_slot == request_profile.slot
            && current_bgm == request_profile.bgm
            && current_sound == route_before_arm.owned_sound
            && current_request_handle == request_profile.request_handle
            && current_request_handle != 0
            && current_slot_state == 4;
        if (!request_safe) {
            clear_unpublished_audio_setup(
                lease_identity, AudioRouteTransitionReason::ArmOwnerRequestFailure);
            std::ostringstream request_out;
            request_out << "[audio_sead] native_owner_request status=blocked_validation"
                << " song_id=" << song_id
                << " base_slot=" << song->base_slot
                << " captured_song_id=" << request_profile.captured_song_id
                << " owner=0x" << std::hex << reinterpret_cast<uintptr_t>(current_owner)
                << " expected_owner=0x" << reinterpret_cast<uintptr_t>(request_profile.owner)
                << " owner_state=0x" << static_cast<unsigned>(owner_state)
                << " live_request_index=0x" << static_cast<unsigned>(live_request_index)
                << " expected_request_index=0x" << static_cast<unsigned>(request_profile.request_index)
                << " live_packed_key=0x" << live_packed_key
                << " expected_packed_key=0x" << request_profile.packed_key
                << std::dec
                << " captured_route_generation=" << request_profile.route_generation
                << " current_route_generation=" << route_before_arm.generation
                << " slot_state=" << static_cast<unsigned>(current_slot_state);
            core::log(core::LogLevel::Error, request_out.str());
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const bool retained = native_audio_route_owned_locked();
            if (retained) {
                (void)g_frozen_profile_lease.mark_native_arm_attempt(lease_identity);
                return {AudioRouteArmStatus::Retained, lease_identity};
            }
            const auto result = g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::VerifiedNoRoute, lease_identity);
            if (result.clear_route_metadata) {
                AudioRouteTransitionRecorder route_transition_record(
                    AudioRouteTransitionReason::ArmOwnerRequestFailure,
                    AudioRouteTransitionKind::RouteReset);
                g_audio_route_state = {};
            }
            return {AudioRouteArmStatus::Failed, {}};
        }
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            g_audio_route_state.list_cleanup_pending = true;
            (void)g_frozen_profile_lease.mark_native_arm_attempt(lease_identity);
        }
        auto* const request = reinterpret_cast<PianoAudioRequestFn>(
            reinterpret_cast<uint8_t*>(g_exe_module) + rva::PianoAudioRequest);
        const bool requested = call_piano_audio_request_seh(
            request, request_profile.request_index, request_profile.packed_key);
        std::ostringstream request_out;
        request_out << "[audio_sead] native_owner_request status=" << (requested ? "called" : "exception")
            << " song_id=" << song_id
            << " base_slot=" << song->base_slot
            << " captured_song_id=" << request_profile.captured_song_id
            << " owner=0x" << std::hex << reinterpret_cast<uintptr_t>(current_owner)
            << " request_index=0x" << static_cast<unsigned>(request_profile.request_index)
            << " packed_key=0x" << request_profile.packed_key
            << std::dec;
        core::log(requested ? core::LogLevel::Info : core::LogLevel::Error, request_out.str());
        if (!requested) {
            clear_unpublished_audio_setup(
                lease_identity, AudioRouteTransitionReason::ArmOwnerRequestFailure);
            g_audio_route_disabled.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            (void)g_frozen_profile_lease.transition(
                AudioRouteCleanupEvent::RequestException, lease_identity);
            return {AudioRouteArmStatus::Retained, lease_identity};
        }
        const PlaybackSnapshot claimed = registry().playback_snapshot();
        if (claimed.song && custom_route_claim_is_complete(claimed.token)
            && claimed.token.registry_generation == selection.generation
            && claimed.token.lease_generation == lease_identity.generation
            && claimed.token.song_key == lease_identity.song_key) {
            return {AudioRouteArmStatus::Armed, lease_identity};
        }
        clear_unpublished_audio_setup(
            lease_identity, AudioRouteTransitionReason::ArmOwnerRequestFailure);
        core::log(core::LogLevel::Info,
            "[audio_sead] arm status=retained reason=request_returned_without_custom_claim");
        return {AudioRouteArmStatus::Retained, lease_identity};
    }

    if (!lookup_controller_valid) {
        core::log(core::LogLevel::Info,
            "[audio_sead] arm status=armed_private reason=waiting_for_exact_playsetup_claim");
        return {AudioRouteArmStatus::Armed, lease_identity};
    }
    if (!rebuild_controller || rebuild_controller != lookup_controller
        || !uobject_identity_matches(rebuild_controller, rebuild_controller_identity)
        || !g_controller_rebuild_available.load(std::memory_order_acquire)) {
        clear_unpublished_audio_setup(
            lease_identity, AudioRouteTransitionReason::ArmOwnerRequestFailure);
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        (void)g_frozen_profile_lease.mark_native_arm_attempt(lease_identity);
        return {AudioRouteArmStatus::Retained, lease_identity};
    }
    bool waiting_for_fresh_play_setup = false;
    const bool rebuilt = rebuild_armed_controller_route(
        rebuild_controller, rebuild_controller_identity, *song, *sidecar,
        &waiting_for_fresh_play_setup);
    if (rebuilt) {
        if (promote_unpublished_audio_setup(
                lease_identity, AudioArmPublicationProof::ControllerRebuildClaimed)) {
            return {AudioRouteArmStatus::Armed, lease_identity};
        }
        clear_unpublished_audio_setup(
            lease_identity, AudioRouteTransitionReason::ArmOwnerRequestFailure);
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        (void)g_frozen_profile_lease.mark_native_arm_attempt(lease_identity);
        return {AudioRouteArmStatus::Retained, lease_identity};
    }
    if (waiting_for_fresh_play_setup) {
        core::log(core::LogLevel::Info,
            "[audio_sead] arm status=armed_private reason=waiting_for_controller_stop_set_play");
        return {AudioRouteArmStatus::Armed, lease_identity};
    }
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        const bool route_retained = g_audio_route_state.generation == arm_generation
            && g_audio_route_state.desired_song_id == song_id
            && g_audio_route_state.phase != AudioRoutePhase::Idle
            && !g_audio_route_disabled.load(std::memory_order_acquire);
        if (route_retained) {
            clear_unpublished_audio_setup_locked(
                lease_identity, AudioRouteTransitionReason::ArmRebuildFailure);
            (void)g_frozen_profile_lease.mark_native_arm_attempt(lease_identity);
            return {AudioRouteArmStatus::Retained, lease_identity};
        }
        const bool live_owned_route = native_audio_route_owned_locked();
        if (live_owned_route) {
            clear_unpublished_audio_setup_locked(
                lease_identity, AudioRouteTransitionReason::ArmRebuildFailure);
            (void)g_frozen_profile_lease.mark_native_arm_attempt(lease_identity);
            return {AudioRouteArmStatus::Retained, lease_identity};
        }
        const auto result = g_frozen_profile_lease.transition(
            AudioRouteCleanupEvent::VerifiedNoRoute, lease_identity);
        if (result.clear_route_metadata) {
            AudioRouteTransitionRecorder route_transition_record(
                AudioRouteTransitionReason::ArmRebuildFailure,
                AudioRouteTransitionKind::RouteReset);
            g_audio_route_state = {};
        }
        if (result.status == AudioRouteCleanupStatus::Retained) {
            clear_unpublished_audio_setup_locked(
                lease_identity, AudioRouteTransitionReason::ArmRebuildFailure);
            return {AudioRouteArmStatus::Retained, result.identity};
        }
    }
    clear_unpublished_audio_setup(
        lease_identity, AudioRouteTransitionReason::ArmRebuildFailure);
    return {AudioRouteArmStatus::Failed, {}};
}

// The mod's only mutation of the game's sound object is the mabf_source
// pointer captured in the frozen patch set.  Returns true when every captured
// field currently reads back its original value on the still-live sound.
//
// The owner-token field (sound+0x548) is excluded on purpose: the native
// rewrites it on its own Set, so it reports on the game's bank, not on whether
// the mod's mutation was reverted.  Mirrors the exclusion in
// audio_retirement_cleanup's frozen_non_owner_fields_restored.
bool frozen_sound_patch_fields_restored(
    const FrozenSoundPatchSnapshot& frozen) noexcept
{
    if (!frozen.valid() || !frozen.sound
        || !uobject_identity_matches(frozen.sound, frozen.sound_identity)) {
        return false;
    }
    size_t evaluated = 0;
    for (const AudioFieldPatch& patch : frozen.patches) {
        // valid() only checks the patch count, so the object each patch names
        // still has to be confirmed before its offset is read through
        // frozen.sound.  Parity with audio_retirement_cleanup's restore scan.
        if (patch.object != frozen.sound) return false;
        if (patch.label && std::strcmp(patch.label, "sound+0x548") == 0) {
            continue;
        }
        uint64_t current = 0;
        const bool read_ok = patch.size == sizeof(uint32_t)
            ? read_field_u32_as_u64(frozen.sound, patch.offset, current)
            : read_field_u64(frozen.sound, patch.offset, current);
        if (!read_ok || current != patch.original) return false;
        ++evaluated;
    }
    return evaluated != 0;
}

AudioRouteCleanupResult release_audio_route_on_piano_list_return_impl(
    AudioCleanupOperation operation, bool hooks_disabled, bool callbacks_drained,
    bool auxiliary_journals_restored = true,
    ListReturnClearAuthorityDiagnostic* diagnostic = nullptr)
{
    if (diagnostic) *diagnostic = {};
    struct ResetDispositionMarker final {
        bool eligible = false;
        CanonicalSubstrateListReturnResetDisposition disposition =
            CanonicalSubstrateListReturnResetDisposition::Rejected;
        uint64_t pre_route_generation = 0;
        uint64_t post_route_generation = 0;
        uint64_t pre_observation = 0;
        uint64_t post_observation = 0;
        CanonicalSubstrateResetLineagePhase authority_phase =
            CanonicalSubstrateResetLineagePhase::None;
        uint64_t authority_generation = 0;
        uint64_t revocation_epoch = 0;
    } reset_disposition_marker;
    auto deferred_reset_disposition_marker = make_deferred_noexcept_action(
        [&]() {
            if (!reset_disposition_marker.eligible) return;
            static std::atomic_uint32_t s_reset_disposition_logs{0};
            if (s_reset_disposition_logs.fetch_add(
                    1, std::memory_order_relaxed) >= 32) return;
            const char* disposition = "rejected";
            if (reset_disposition_marker.disposition
                == CanonicalSubstrateListReturnResetDisposition::Performed) {
                disposition = "performed";
            } else if (reset_disposition_marker.disposition
                == CanonicalSubstrateListReturnResetDisposition::AlreadyReady) {
                disposition = "already_ready";
            }
            std::ostringstream out;
            out << "[audio_sead] canonical_substrate_list_return_reset"
                << " disposition=" << disposition
                << " pre_route_generation="
                << reset_disposition_marker.pre_route_generation
                << " post_route_generation="
                << reset_disposition_marker.post_route_generation
                << " pre_observation="
                << reset_disposition_marker.pre_observation
                << " post_observation="
                << reset_disposition_marker.post_observation
                << " authority_phase="
                << static_cast<unsigned>(reset_disposition_marker.authority_phase)
                << " authority_generation="
                << reset_disposition_marker.authority_generation
                << " revocation_epoch="
                << reset_disposition_marker.revocation_epoch
                << " native_call_invoked=0 bank_release_invoked=0";
            core::log(core::LogLevel::Info, out.str());
        });
    deferred_reset_disposition_marker.make_eligible();
    CanonicalSubstrateResetLineageMarker reset_lineage_marker;
    auto deferred_reset_lineage_marker = make_deferred_noexcept_action(
        [&]() noexcept {
            if (!reset_lineage_marker.eligible) return;
            static std::atomic_uint32_t s_reset_lineage_logs{0};
            if (s_reset_lineage_logs.fetch_add(1, std::memory_order_relaxed) >= 32) {
                return;
            }
            std::ostringstream out;
            out << "[audio_sead] canonical_substrate_reset_lineage"
                << " status=committed"
                << " authority_generation="
                << reset_lineage_marker.authority_generation
                << " reset_observation_generation="
                << reset_lineage_marker.reset_observation_generation
                << " reset_route_generation="
                << reset_lineage_marker.reset_route_generation
                << " proof_route_generation="
                << reset_lineage_marker.proof_route_generation
                << " request=0x" << std::hex << reset_lineage_marker.request
                << std::dec << " transaction_generation="
                << reset_lineage_marker.transaction_generation
                << " rearm_edge=1 canonical_token_preserved=1 custom_token=0";
            core::log(core::LogLevel::Info, out.str());
        });
    std::lock_guard<std::recursive_mutex> operation_lock(g_audio_route_operations.mutex());
    clear_any_unpublished_audio_setup(
        AudioRouteTransitionReason::ListReturnEarly);
    const PlaybackSnapshot playback = registry().playback_snapshot();
    if (playback.song) {
        (void)revoke_playback_snapshot(playback);
    }
    const bool pending_restored = restore_pending_play_setup_patch(
        operation == AudioCleanupOperation::Shutdown ? "shutdown" : "list_return",
        false, true);
    const bool failed_restored = restore_failed_patch_journal(true);
    const auto apply_policy_locked = [&](AudioRouteLeaseIdentity identity,
                                         bool native_clear_verified = false,
                                         bool route_state_unchanged = false) {
        const bool lease_active = g_frozen_profile_lease.active();
        const bool can_stage_journal_commit = audio_journal_commit_ready({
            pending_restored,
            failed_restored,
            auxiliary_journals_restored,
            g_active_patch_journal.empty(),
            lease_active,
            lease_active && identity == g_frozen_profile_lease.identity(),
        });
        PendingPlaySetupPatch pending_backup;
        std::vector<AudioFieldPatch> failed_backup;
        if (can_stage_journal_commit) {
            pending_backup = g_pending_play_setup_patch;
            failed_backup = g_failed_patch_journal;
            g_pending_play_setup_patch = {};
            g_failed_patch_journal.clear();
        }
        AudioCleanupEvidence cleanup_evidence = audio_cleanup_evidence_locked(
            operation, hooks_disabled, callbacks_drained);
        cleanup_evidence.auxiliary_journals_restored = auxiliary_journals_restored;
        cleanup_evidence.native_clear_verified = native_clear_verified
            || cleanup_evidence.native_clear_verified;
        cleanup_evidence.route_state_unchanged = route_state_unchanged
            || cleanup_evidence.route_state_unchanged;
        AudioRouteCleanupResult result = apply_audio_cleanup(
            g_frozen_profile_lease, identity, cleanup_evidence);
        if (!result.released() && can_stage_journal_commit) {
            g_pending_play_setup_patch = std::move(pending_backup);
            g_failed_patch_journal = std::move(failed_backup);
        }
        return result;
    };

    AudioRouteState snapshot;
    bool return_without_cleanup = false;
    bool early_route_reset = false;
    bool early_reset_native_unowned = false;
    uint64_t early_reset_observation_generation = 0;
    AudioRouteState early_reset_route;
    AudioRouteCleanupResult early_result;
    BgmCanonicalSubstrateProof reset_proof_snapshot;
    uint64_t reset_authority_generation_snapshot = 0;
    uint64_t reset_revocation_epoch_snapshot = 0;
    {
        // Route-operation authority is already held and excludes rebase and
        // reset-qualification epoch writers. Reservation ownership excludes
        // reservation/revocation writers while the proof/epoch pair is captured.
        // Native reads run without the borrower lock because they can re-enter
        // playback callbacks. The commit then follows the established operation
        // -> reservation -> borrower -> audio order and holds borrower ownership
        // stable through the pure zero/Idle readiness classification.
        std::lock_guard<std::mutex> reservation_lock(
            g_selection_activation_reservation_mutex);
        {
            std::lock_guard<std::mutex> borrower_lock(
                g_bgm_playback_borrower_mutex);
            reset_proof_snapshot = g_bgm_canonical_substrate_proof;
            reset_authority_generation_snapshot =
                g_canonical_substrate_reset_lineage_generation;
        }
        reset_revocation_epoch_snapshot =
            g_selection_activation_revocation_epoch.load(
                std::memory_order_acquire);
        const auto& reset_authority_snapshot =
            reset_proof_snapshot.reset_lineage;
        void* const live_controller = lookup_current_bgm_controller();
        UObjectIdentity live_controller_identity{};
        const bool live_controller_identity_read = live_controller
            && read_uobject_identity(live_controller, live_controller_identity);
        const ControllerIdentityProof live_controller_proof =
            live_controller_identity_read
            ? controller_identity_proof(live_controller, live_controller_identity)
            : ControllerIdentityProof{};
        const bool live_substrate_exact = live_controller_identity_read
            && canonical_substrate_active_sound_live_exact(
                reset_proof_snapshot, live_controller, live_controller_proof);
        std::unique_lock<std::mutex> borrower_lock(
            g_bgm_playback_borrower_mutex);
        const bool reset_borrower_ownership_absent =
            std::none_of(g_bgm_playback_borrowers.begin(),
                g_bgm_playback_borrowers.end(),
                [](const BgmPlaybackBorrowerRecord& record) {
                    return record.active;
                });
        std::lock_guard<std::mutex> audio_lock(g_audio_state_mutex);
        if (!g_audio_route_state.custom_resource_owned && !g_audio_route_state.list_cleanup_pending) {
            return_without_cleanup = true;
            early_result = apply_policy_locked(g_audio_route_state.lease_identity);
            if (early_result.clear_route_metadata || !g_frozen_profile_lease.active()) {
                early_reset_route = g_audio_route_state;
                early_reset_native_unowned = !native_audio_route_owned_locked();
                const bool custom_ownership_absent =
                    !early_reset_route.custom_resource_owned
                    && !early_reset_route.aggregate_awaiting_transition
                    && !early_reset_route.owned_slot
                    && !early_reset_route.owned_bgm
                    && !early_reset_route.owned_sound
                    && early_reset_route.owned_request_handle == 0
                    && !early_reset_route.reusable_sound
                    && !early_reset_route.reusable_slot
                    && !early_reset_route.reusable_bgm;
                const bool route_metadata_empty =
                    early_reset_route.canonical_relinquishment_generation == 0
                    && !early_reset_route.sound && !early_reset_route.controller
                    && !early_reset_route.controller_arm_proof_attempted
                    && !early_reset_route.controller_arm_bind_succeeded
                    && !early_reset_route.stop_observed
                    && early_reset_route.stop_authorized_generation == 0
                    && !early_reset_route.set_play_handoff_pending
                    && !early_reset_route.handoff_slot
                    && !early_reset_route.handoff_bgm
                    && !early_reset_route.handoff_sound
                    && early_reset_route.handoff_request_handle == 0
                    && !early_reset_route.native_clear_verified
                    && !early_reset_route.deferred_native_handoff.active()
                    && !early_reset_route.frozen_sound_patch.valid()
                    && early_reset_route.stop_retirement.phase
                        == AudioStopRetirementPhase::None
                    && early_reset_route.stop_retirement_epoch == 0;
                const bool reset_authority_generation_exact =
                    reset_authority_snapshot.generation != 0
                    && reset_authority_snapshot.generation
                        == reset_authority_generation_snapshot;
                const bool reset_observation_exact =
                    reset_authority_snapshot.reset_observation_generation != 0
                    && reset_authority_snapshot.reset_observation_generation
                        == g_canonical_substrate_reset_observation_generation;
                const bool revocation_epoch_covers_rearm =
                    canonical_substrate_revocation_epoch_covers_rearm(
                        reset_authority_snapshot.rearm_epoch,
                        reset_revocation_epoch_snapshot);
                const auto reservation_state =
                    g_selection_activation_reservation_machine.state();
                const bool activation_reservation_absent =
                    reservation_state != SelectionActivationReservationState::Reserved
                    && reservation_state
                        != SelectionActivationReservationState::Consumed;
                const bool activation_route_predecessor_exact =
                    canonical_substrate_route_predecessor_exact(
                        canonical_substrate_route_use_facts(
                            reset_proof_snapshot, 0, {},
                            g_onmemory_bank_lifecycle.state_epoch(),
                            g_canonical_substrate_reset_observation_generation,
                            reset_authority_generation_snapshot,
                            CanonicalSubstrateResetLineagePhase::Qualified,
                            CanonicalSubstrateBridgePhase::Available));
                const CanonicalSubstrateAlreadyReadyFacts already_ready{
                    early_result.status == AudioRouteCleanupStatus::NoAction,
                    early_reset_route.phase == AudioRoutePhase::Idle,
                    early_reset_route.generation == 0,
                    early_reset_route.lease_identity == AudioRouteLeaseIdentity{},
                    early_reset_route.desired_song_id.empty()
                        && early_reset_route.patched_song_id.empty(),
                    custom_ownership_absent,
                    !early_reset_route.list_cleanup_pending,
                    route_metadata_empty,
                    g_active_patch_journal.empty()
                        && g_pending_play_setup_patch.patches.empty()
                        && g_failed_patch_journal.empty(),
                    !g_unpublished_audio_setup,
                    !g_frozen_profile_lease.active(),
                    early_reset_native_unowned,
                    !g_onmemory_bank_cleanup_only.blocks_custom_routes(),
                    !g_custom_activation_quarantine,
                    reset_authority_snapshot.phase
                        == CanonicalSubstrateResetLineagePhase::Qualified,
                    reset_authority_generation_exact,
                    reset_observation_exact,
                    revocation_epoch_covers_rearm,
                    activation_reservation_absent,
                    reset_borrower_ownership_absent,
                    live_substrate_exact,
                    activation_route_predecessor_exact,
                };
                const auto disposition =
                    classify_canonical_substrate_list_return_reset(
                        early_reset_route.generation != 0, already_ready);
                borrower_lock.unlock();
                reset_disposition_marker = {true, disposition,
                    early_reset_route.generation, early_reset_route.generation,
                    g_canonical_substrate_reset_observation_generation,
                    g_canonical_substrate_reset_observation_generation,
                    reset_authority_snapshot.phase,
                    reset_authority_snapshot.generation,
                    reset_revocation_epoch_snapshot};
                if (disposition
                    == CanonicalSubstrateListReturnResetDisposition::AlreadyReady) {
                    early_reset_observation_generation =
                        g_canonical_substrate_reset_observation_generation;
                } else {
                    if (g_canonical_substrate_reset_observation_generation
                        != UINT64_MAX) {
                        early_reset_observation_generation =
                            ++g_canonical_substrate_reset_observation_generation;
                    }
                    early_route_reset = true;
                    AudioRouteTransitionRecorder route_transition_record(
                        AudioRouteTransitionReason::ListReturnEarly,
                        AudioRouteTransitionKind::RouteReset);
                    g_audio_route_state = {};
                    reset_disposition_marker.post_route_generation =
                        g_audio_route_state.generation;
                    reset_disposition_marker.post_observation =
                        g_canonical_substrate_reset_observation_generation;
                }
            }
        } else {
            snapshot = g_audio_route_state;
        }
    }
    if (early_route_reset
        && early_result.status == AudioRouteCleanupStatus::NoAction) {
        (void)try_qualify_canonical_substrate_reset_lineage(
            early_reset_route, true, early_reset_native_unowned,
            early_reset_observation_generation, reset_lineage_marker);
    }
    if (reset_disposition_marker.eligible) {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        reset_disposition_marker.authority_phase =
            g_bgm_canonical_substrate_proof.reset_lineage.phase;
        reset_disposition_marker.authority_generation =
            g_bgm_canonical_substrate_proof.reset_lineage.generation;
        reset_disposition_marker.revocation_epoch =
            g_selection_activation_revocation_epoch.load(
                std::memory_order_acquire);
    }
    if (early_result.thaw_profile) registry().clear_frozen_profile();
    if (early_result.released()) retire_registry_cleanup(early_result.identity);
    if (return_without_cleanup) {
        if (early_result.status == AudioRouteCleanupStatus::NoAction) {
            core::log(core::LogLevel::Info,
                "[audio_sead] list_return_release status=no_action cleanup=verified");
        }
        return early_result;
    }

    const bool deferred_native_forwarded =
        snapshot.deferred_native_handoff.native_play_obligation_met();
    const bool stop_retirement_quiescent = snapshot.stop_retirement.phase
            == AudioStopRetirementPhase::None
        || snapshot.stop_retirement.phase == AudioStopRetirementPhase::Quiescent;
    if (snapshot.native_clear_verified
        && deferred_native_forwarded
        && stop_retirement_quiescent) {
        AudioRouteCleanupResult result;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            result = apply_policy_locked(snapshot.lease_identity, true, true);
            if (result.clear_route_metadata) {
                AudioRouteTransitionRecorder route_transition_record(
                    AudioRouteTransitionReason::ListReturnVerified,
                    AudioRouteTransitionKind::RouteReset);
                g_pending_play_setup_patch = {};
                g_failed_patch_journal.clear();
                g_audio_route_state = {};
                g_piano_audio_request_profile = {};
            }
        }
        if (result.thaw_profile) registry().clear_frozen_profile();
        if (result.released()) retire_registry_cleanup(result.identity);
        return result;
    }

    void* current_controller = lookup_current_bgm_controller();
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint64_t request_handle = 0;
    uint8_t slot_state = 0;
    UObjectIdentity release_sound_identity;
    const bool current_controller_exact = current_controller == snapshot.controller;
    const bool current_controller_identity_exact = current_controller_exact
        && uobject_identity_matches(snapshot.controller, snapshot.controller_identity);
    const bool current_chain_read = current_controller_identity_exact
        && read_controller_bgm_chain(snapshot.controller, slot, bgm);
    const bool current_sound_read = current_chain_read
        && core::safe_read_field(bgm, runtime_layouts::SqexSeadBgm::sound, sound);
    const bool current_request_read = current_sound_read
        && core::safe_read_field(
            bgm, runtime_layouts::SqexSeadBgm::request_handle, request_handle);
    const bool current_state_read = current_request_read
        && core::safe_read_field(slot, runtime_layouts::SqexSeadSlot::state, slot_state);
    const bool current_sound_identity_read = current_state_read && sound
        && request_handle && slot_state == 4
        && read_uobject_identity(sound, release_sound_identity);
    const bool current_chain_valid = current_sound_identity_read;
    const bool owned_sound_identity_exact = current_chain_valid
        && snapshot.custom_resource_owned
        && slot == snapshot.owned_slot
        && bgm == snapshot.owned_bgm
        && sound == snapshot.owned_sound
        && request_handle == snapshot.owned_request_handle
        && uobject_identity_matches(
            snapshot.owned_sound, snapshot.owned_sound_identity);
    const bool route_owned = current_chain_valid
        && snapshot.custom_resource_owned
        && slot == snapshot.owned_slot
        && bgm == snapshot.owned_bgm
        && sound == snapshot.owned_sound
        && request_handle == snapshot.owned_request_handle
        && owned_sound_identity_exact;
    ListReturnAggregateClearAuthority aggregate_authority;
    if (operation == AudioCleanupOperation::ListReturn) {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        aggregate_authority.collection_version =
            g_bgm_playback_collection_version;
        for (const auto& record : g_bgm_playback_borrowers) {
            if (!record.active || !record.list_exit
                || (aggregate_authority.candidate_present
                    && record.ordinal <= aggregate_authority.record.ordinal)) {
                continue;
            }
            aggregate_authority.record = record;
            aggregate_authority.candidate_present = true;
        }
    }
    const auto& aggregate_candidate = aggregate_authority.record;
    const ControllerIdentityProof route_controller_proof =
        controller_identity_proof(
            snapshot.controller, snapshot.controller_identity);
    const bool aggregate_controller_proof_exact =
        aggregate_authority.candidate_present
        && controller_identity_proof_matches(
            route_controller_proof, aggregate_candidate.controller_proof)
        && current_controller_identity_exact;
    OnMemoryBankDetachedRecord aggregate_lifecycle;
    uint64_t aggregate_lifecycle_epoch = 0;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        aggregate_lifecycle = g_onmemory_bank_lifecycle.active();
        aggregate_lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
    }
    const bool aggregate_lifecycle_tokens_exact =
        aggregate_authority.candidate_present && aggregate_lifecycle
        && aggregate_candidate.lifecycle_state_epoch != 0
        && aggregate_candidate.lifecycle_state_epoch == aggregate_lifecycle_epoch
        && aggregate_candidate.token_epoch != 0
        && aggregate_candidate.canonical != 0
        && aggregate_candidate.custom != 0
        && aggregate_candidate.canonical == aggregate_lifecycle.canonical.encode()
        && aggregate_candidate.custom == aggregate_lifecycle.custom.encode();
    const BgmPlaybackAggregateCanonicalExitClearFacts aggregate_clear_facts{
        g_bgm_aggregate_exit_requested.load(std::memory_order_acquire)
            && g_bgm_aggregate_exit_pending.load(std::memory_order_acquire),
        g_bgm_aggregate_mutation_gate.closed()
            && g_bgm_aggregate_mutation_gate.drained(),
        aggregate_authority.candidate_present,
        aggregate_authority.candidate_present
            && aggregate_candidate.version != 0,
        aggregate_authority.collection_version != 0,
        aggregate_authority.candidate_present && aggregate_candidate.list_exit
            && aggregate_candidate.list_exit_epoch != 0,
        aggregate_authority.candidate_present
            && aggregate_candidate.current_active,
        aggregate_authority.candidate_present && !aggregate_candidate.failed,
        aggregate_authority.candidate_present
            && !aggregate_candidate.destination_aba,
        aggregate_authority.candidate_present
            && !aggregate_candidate.old_identity_conflict
            && !aggregate_candidate.lineage_identity_conflict,
        aggregate_authority.candidate_present
            && aggregate_candidate.continuity_exact,
        current_chain_valid && aggregate_controller_proof_exact
            && aggregate_candidate.controller == current_controller,
        current_chain_valid && aggregate_candidate.slot == slot
            && aggregate_candidate.bgm == bgm,
        current_chain_valid && aggregate_candidate.expected_new_sound == sound,
        current_sound_identity_read
            && aggregate_candidate.expected_new_sound_identity.internal_index
                == release_sound_identity.live.internal_index
            && aggregate_candidate.expected_new_sound_identity.serial_number
                == release_sound_identity.live.serial_number,
        request_handle != 0 && aggregate_candidate.new_handle == request_handle,
        request_handle != 0
            && aggregate_candidate.canonical_intermediate_handle
                == request_handle,
        current_state_read && slot_state == 4,
        aggregate_authority.candidate_present
            && !aggregate_candidate.transition_set_pending,
        aggregate_authority.candidate_present
            && !aggregate_candidate.canonical_stop_boundary_active,
        aggregate_authority.candidate_present
            && !aggregate_candidate.exit_unresolved_native_request,
        aggregate_authority.candidate_present
            && aggregate_candidate.latest_custom_transition_handle == 0
            && aggregate_candidate.latest_custom_transition_sound == nullptr
            && aggregate_candidate.latest_custom_transition_child_ordinal == 0,
        aggregate_candidate.canonical_intermediate_route_generation != 0
            && aggregate_candidate.canonical_intermediate_route_generation
                == snapshot.generation,
        aggregate_candidate.lease.valid()
            && aggregate_candidate.lease == snapshot.lease_identity,
        aggregate_authority.candidate_present
            && aggregate_candidate.lifecycle_state_epoch != 0
            && aggregate_candidate.lifecycle_state_epoch == aggregate_lifecycle_epoch,
        aggregate_authority.candidate_present && aggregate_lifecycle_tokens_exact,
        snapshot.custom_resource_owned && snapshot.owned_sound
            && snapshot.owned_sound_identity.live.internal_index >= 0
            && snapshot.owned_sound_identity.live.serial_number > 0
            && snapshot.owned_request_handle != 0,
    };
    const bool aggregate_canonical_exit_exact =
        bgm_playback_aggregate_canonical_exit_clear_exact(
            aggregate_clear_facts);
    if (diagnostic) {
        diagnostic->proposed = true;
        diagnostic->route_owned = route_owned;
        diagnostic->route_generation = snapshot.generation;
        diagnostic->route_lease = snapshot.lease_identity;
        diagnostic->route_controller = snapshot.controller;
        diagnostic->route_controller_proof_valid = controller_identity_proof_valid(
            controller_identity_proof(snapshot.controller, snapshot.controller_identity));
        diagnostic->route_slot = snapshot.owned_slot;
        diagnostic->route_bgm = snapshot.owned_bgm;
        diagnostic->route_sound = snapshot.owned_sound;
        diagnostic->route_sound_identity = snapshot.owned_sound_identity.live;
        diagnostic->route_request = snapshot.owned_request_handle;
        diagnostic->route_custom_resource_owned = snapshot.custom_resource_owned;
        diagnostic->route_list_cleanup_pending = snapshot.list_cleanup_pending;
        diagnostic->route_native_clear_verified = snapshot.native_clear_verified;
        diagnostic->live_controller = current_controller;
        diagnostic->live_slot = slot;
        diagnostic->live_bgm = bgm;
        diagnostic->live_sound = sound;
        diagnostic->live_sound_identity = release_sound_identity.live;
        diagnostic->live_request = request_handle;
        diagnostic->live_state = current_state_read ? slot_state : 0xff;
        diagnostic->live_controller_exact = current_controller_exact;
        diagnostic->live_controller_identity_exact =
            current_controller_identity_exact;
        diagnostic->live_chain_read = current_chain_read;
        diagnostic->live_sound_read = current_sound_read;
        diagnostic->live_request_read = current_request_read;
        diagnostic->live_state_read = current_state_read;
        diagnostic->live_sound_identity_read = current_sound_identity_read;
        diagnostic->live_slot_exact = current_chain_read
            && slot == snapshot.owned_slot;
        diagnostic->live_bgm_exact = current_chain_read
            && bgm == snapshot.owned_bgm;
        diagnostic->live_sound_exact = current_sound_read
            && sound == snapshot.owned_sound;
        diagnostic->live_request_exact = current_request_read
            && request_handle == snapshot.owned_request_handle;
        diagnostic->live_sound_identity_exact = owned_sound_identity_exact;
        if (!current_controller_exact) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::CurrentController;
        } else if (!current_controller_identity_exact) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::ControllerIdentity;
        } else if (!current_chain_read) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::ControllerChain;
        } else if (!current_sound_read) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::SoundRead;
        } else if (!current_request_read) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::RequestRead;
        } else if (!current_state_read) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::StateRead;
        } else if (!sound) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::SoundMissing;
        } else if (!request_handle) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::RequestMissing;
        } else if (slot_state != 4) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::StateNotPlaying;
        } else if (!current_sound_identity_read) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::SoundIdentity;
        } else if (!snapshot.custom_resource_owned) {
            diagnostic->blocker =
                ListReturnClearAuthorityBlocker::CustomResourceUnowned;
        } else if (slot != snapshot.owned_slot) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::SlotMismatch;
        } else if (bgm != snapshot.owned_bgm) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::BgmMismatch;
        } else if (sound != snapshot.owned_sound) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::SoundMismatch;
        } else if (request_handle != snapshot.owned_request_handle) {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::RequestMismatch;
        } else if (!owned_sound_identity_exact) {
            diagnostic->blocker =
                ListReturnClearAuthorityBlocker::OwnedSoundIdentity;
        }

        diagnostic->aggregate_collection_version =
            aggregate_authority.collection_version;
        diagnostic->aggregate_candidate_present =
            aggregate_authority.candidate_present;
        if (aggregate_authority.candidate_present) {
            diagnostic->aggregate_ordinal = aggregate_candidate.ordinal;
            diagnostic->aggregate_record_version = aggregate_candidate.version;
            diagnostic->aggregate_list_exit_epoch =
                aggregate_candidate.list_exit_epoch;
            diagnostic->aggregate_controller = aggregate_candidate.controller;
            diagnostic->aggregate_controller_proof_matches_route =
                controller_identity_proof_matches(route_controller_proof,
                    aggregate_candidate.controller_proof);
            diagnostic->aggregate_slot = aggregate_candidate.slot;
            diagnostic->aggregate_bgm = aggregate_candidate.bgm;
            diagnostic->aggregate_canonical_sound =
                aggregate_candidate.expected_new_sound;
            diagnostic->aggregate_canonical_sound_identity =
                aggregate_candidate.expected_new_sound_identity;
            diagnostic->aggregate_new_handle = aggregate_candidate.new_handle;
            diagnostic->aggregate_canonical_handle =
                aggregate_candidate.canonical_intermediate_handle;
            diagnostic->aggregate_route_generation =
                aggregate_candidate.canonical_intermediate_route_generation;
            diagnostic->aggregate_lease = aggregate_candidate.lease;
            diagnostic->aggregate_lifecycle_state_epoch =
                aggregate_candidate.lifecycle_state_epoch;
            diagnostic->aggregate_token_epoch = aggregate_candidate.token_epoch;
            diagnostic->aggregate_canonical_token = aggregate_candidate.canonical;
            diagnostic->aggregate_custom_token = aggregate_candidate.custom;
            diagnostic->aggregate_list_exit = aggregate_candidate.list_exit;
            diagnostic->aggregate_current_active = aggregate_candidate.current_active;
            diagnostic->aggregate_failed = aggregate_candidate.failed;
            diagnostic->aggregate_destination_aba = aggregate_candidate.destination_aba;
            diagnostic->aggregate_old_identity_conflict =
                aggregate_candidate.old_identity_conflict;
            diagnostic->aggregate_lineage_identity_conflict =
                aggregate_candidate.lineage_identity_conflict;
            diagnostic->aggregate_continuity_exact =
                aggregate_candidate.continuity_exact;
            diagnostic->aggregate_transition_set_pending =
                aggregate_candidate.transition_set_pending;
            diagnostic->aggregate_boundary_pending =
                aggregate_candidate.canonical_stop_boundary_active;
            diagnostic->aggregate_unresolved_request =
                aggregate_candidate.exit_unresolved_native_request;
            diagnostic->aggregate_canonical_exit_exact =
                aggregate_canonical_exit_exact;
        }
    }
    // Direct evidence that the native has retired the mod's request handle: a
    // live read of the slot's retired vector.  This is deliberately not the
    // stop monitor's quiescent phase.  The monitor is advanced only from
    // piano_audio_state_tick, so on a build whose catalog lacks that locator it
    // never polls at all and its phase stays Waiting with poll_count 0 -- while
    // the handle it was waiting on is already retired.  Retirement is the fact
    // the retirement and the release actually depend on; quiescence is a
    // property of the poller.
    const StopRetirementRead list_return_retirement =
        (snapshot.controller && snapshot.owned_slot && snapshot.owned_bgm
            && snapshot.owned_sound && snapshot.owned_request_handle != 0)
        ? read_stop_retirement(snapshot.controller,
            snapshot.controller_identity, snapshot.generation,
            snapshot.lease_identity, snapshot.owned_slot, snapshot.owned_bgm,
            snapshot.owned_sound, snapshot.owned_request_handle)
        : StopRetirementRead{};
    const bool custom_request_retired = list_return_retirement.observation.valid
        && list_return_retirement.observation.request_handle_retired;

    // Third authority: proof that the mod's route bookkeeping may be retired.
    // It covers both arm-time lineages.  A shared lineage means the game's
    // OnMemory bank was already resident at custom play-setup, so the native
    // allocator was never reached and the mod never held a bank to release.  A
    // detached lineage means the mod did cause the load and still owes a
    // release -- but that release is proven by the arm-time record below, not
    // by anything on this path.  Neither of the two authorities above can fire
    // for either lineage once the native re-Set the canonical BGM, which used
    // to leave list_cleanup_pending latched for the life of the process and
    // deny every later activation.
    OnMemoryBankSharedRecord shared_bank{};
    OnMemoryBankDetachedRecord detached_bank{};
    uint64_t lineage_state_epoch = 0;
    bool shared_lineage = false;
    bool detached_lineage = false;
    BgmPlaybackRouteRestoreRetirementFacts retirement_facts{};
    {
        const OnMemoryBankSoundIdentity retired_sound{
            snapshot.owned_sound, snapshot.owned_sound_identity.live};
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        shared_bank = g_onmemory_bank_lifecycle.shared();
        detached_bank = g_onmemory_bank_lifecycle.active();
        lineage_state_epoch = g_onmemory_bank_lifecycle.state_epoch();
        shared_lineage = static_cast<bool>(shared_bank);
        // Only a record still holding its arm-time restore state describes a
        // route this path may retire; a claimed or already-completed release
        // does not.
        detached_lineage = static_cast<bool>(detached_bank)
            && detached_bank.phase == OnMemoryBankLifecyclePhase::RestoreApplied;
        // Exactly one lineage.  This is what makes "does the mod owe a bank
        // release?" a question answered by immutable arm-time proof and never
        // by ambient live state.
        retirement_facts.lineage_present = shared_lineage != detached_lineage;
        retirement_facts.lineage_sound_exact = shared_lineage
            ? shared_bank.sound == retired_sound
            : detached_lineage && detached_bank.sound == retired_sound;
        retirement_facts.lineage_route_exact = shared_lineage
            ? g_onmemory_bank_lifecycle.shared_matches(retired_sound,
                snapshot.owned_request_handle, snapshot.generation)
            : detached_lineage && snapshot.owned_request_handle != 0
                && detached_bank.request_handle == snapshot.owned_request_handle
                && detached_bank.route_generation <= snapshot.generation;
        retirement_facts.lineage_owner_restore_verified = shared_lineage
            ? shared_bank.owner_restore_verified
            : detached_lineage && detached_bank.owner_restore_verified;
        retirement_facts.lifecycle_failure_clear =
            !g_onmemory_bank_lifecycle.failed()
            && !g_onmemory_bank_lifecycle.release_in_flight();
        retirement_facts.no_custom_publication_pending =
            !g_unpublished_audio_setup
            && g_pending_play_setup_patch.patches.empty()
            && !snapshot.set_play_handoff_pending;
        retirement_facts.patch_journals_restored = pending_restored
            && failed_restored && auxiliary_journals_restored
            && g_failed_patch_journal.empty()
            && g_active_patch_journal.empty();
    }
    retirement_facts.owner_patch_restored =
        frozen_sound_patch_fields_restored(snapshot.frozen_sound_patch);
    retirement_facts.retired_sound_identity_exact = uobject_identity_matches(
        snapshot.owned_sound, snapshot.owned_sound_identity);
    retirement_facts.route_cleanup_pending = snapshot.custom_resource_owned
        && snapshot.list_cleanup_pending;
    retirement_facts.controller_exact = current_controller_exact
        && current_controller_identity_exact;
    retirement_facts.slot_bgm_exact = current_chain_read
        && slot == snapshot.owned_slot && bgm == snapshot.owned_bgm;
    // Note what this does *not* require: that the live sound and request are
    // still the mod's.  A native stop followed by an immediate native re-Set of
    // the canonical BGM legitimately drives both away from the mod's values.
    retirement_facts.live_chain_exact = current_sound_identity_read;
    retirement_facts.deferred_handoff_forwarded = deferred_native_forwarded;
    retirement_facts.custom_request_retired = custom_request_retired;
    const bool route_restore_retirement_proven =
        bgm_playback_route_restore_retirement_proven(retirement_facts);
    // Report the lineage evidence whenever any lineage exists, including when
    // it fails: otherwise a session that does not qualify is indistinguishable
    // from one that never had a lineage record at all.
    if (shared_lineage || detached_lineage) {
        std::ostringstream lineage_facts_out;
        lineage_facts_out << "[audio_sead] route_restore_authority proven="
            << (route_restore_retirement_proven ? 1 : 0)
            << " lineage=" << (shared_lineage && detached_lineage ? "ambiguous"
                : shared_lineage ? "shared" : "detached")
            << " shared_token=" << shared_bank.token.encode()
            << " shared_ordinal=" << shared_bank.ordinal
            << " detached_canonical=" << detached_bank.canonical.encode()
            << " detached_custom=" << detached_bank.custom.encode()
            << " detached_ordinal=" << detached_bank.ordinal
            << " lineage_present="
            << (retirement_facts.lineage_present ? 1 : 0)
            << " sound_exact=" << (retirement_facts.lineage_sound_exact ? 1 : 0)
            << " route_exact=" << (retirement_facts.lineage_route_exact ? 1 : 0)
            << " owner_restore_verified="
            << (retirement_facts.lineage_owner_restore_verified ? 1 : 0)
            << " failure_clear="
            << (retirement_facts.lifecycle_failure_clear ? 1 : 0)
            << " owner_patch_restored="
            << (retirement_facts.owner_patch_restored ? 1 : 0)
            << " retired_sound_identity_exact="
            << (retirement_facts.retired_sound_identity_exact ? 1 : 0)
            << " route_cleanup_pending="
            << (retirement_facts.route_cleanup_pending ? 1 : 0)
            << " controller_exact="
            << (retirement_facts.controller_exact ? 1 : 0)
            << " slot_bgm_exact=" << (retirement_facts.slot_bgm_exact ? 1 : 0)
            << " live_chain_exact="
            << (retirement_facts.live_chain_exact ? 1 : 0)
            << " no_custom_publication="
            << (retirement_facts.no_custom_publication_pending ? 1 : 0)
            << " deferred_handoff_forwarded="
            << (retirement_facts.deferred_handoff_forwarded ? 1 : 0)
            // Without the phase, a false `deferred_handoff_forwarded` cannot be
            // told apart from a handoff that legitimately never forwarded.
            << " deferred_handoff_phase="
            << audio_deferred_native_handoff_phase_name(
                snapshot.deferred_native_handoff.phase)
            << " custom_request_retired="
            << (retirement_facts.custom_request_retired ? 1 : 0)
            << " retired_count="
            << list_return_retirement.observation.retired_count
            << " stop_retirement_quiescent="
            << (stop_retirement_quiescent ? 1 : 0)
            << " patch_journals_restored="
            << (retirement_facts.patch_journals_restored ? 1 : 0);
        core::log(core::LogLevel::Info, lineage_facts_out.str());
    }

    const bool clear_authorized = bgm_playback_list_return_clear_authorized(
        route_owned, aggregate_canonical_exit_exact,
        route_restore_retirement_proven);
    const bool route_restore_relinquishment_path =
        bgm_playback_route_restore_relinquishment_path(
            route_owned, aggregate_canonical_exit_exact,
            route_restore_retirement_proven);
    if (diagnostic) {
        diagnostic->source = route_owned && aggregate_canonical_exit_exact
            ? ListReturnClearAuthoritySource::Both
            : route_owned ? ListReturnClearAuthoritySource::LegacyRoute
            : aggregate_canonical_exit_exact
                ? ListReturnClearAuthoritySource::AggregateCanonical
            : route_restore_retirement_proven
                ? ListReturnClearAuthoritySource::RouteRestore
                : ListReturnClearAuthoritySource::None;
        if (!clear_authorized && diagnostic->blocker
                == ListReturnClearAuthorityBlocker::None) {
            diagnostic->blocker =
                ListReturnClearAuthorityBlocker::AggregateAuthority;
        }
    }
    if (!clear_authorized) {
        AudioRouteCleanupResult result;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            result = apply_policy_locked(snapshot.lease_identity);
        }
        core::log(core::LogLevel::Error, "[audio_sead] list_return_release status=validation_failed cleanup=retained");
        return result;
    }

    // Restore retirement.  There is nothing to clear here: the native owns the
    // slot and has already re-Set the canonical BGM on it.  Retire the mod's
    // own route bookkeeping and return before the pin/pre-clear/native-clear
    // code below -- that is what keeps call_bgm_slot_set_original(controller,
    // nullptr) unreachable for this authority.  Reaching it would stop the
    // vanilla BGM the game had just started.
    //
    // Freeing the bank is a separate obligation, attempted after this
    // transaction commits and only for a detached lineage.  A shared lineage
    // leaves active_ empty, so claim_release() is structurally unreachable for
    // it and a bank the game owns can never be freed from here.
    if (route_restore_relinquishment_path) {
        AudioRouteCleanupResult result;
        bool relinquished = false;
        uint64_t relinquished_state_epoch = 0;
        {
            const OnMemoryBankSoundIdentity retired_sound{
                snapshot.owned_sound, snapshot.owned_sound_identity.live};
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const bool route_unchanged =
                g_audio_route_state.generation == snapshot.generation
                && g_audio_route_state.lease_identity == snapshot.lease_identity
                && g_audio_route_state.controller == snapshot.controller
                && g_audio_route_state.owned_slot == snapshot.owned_slot
                && g_audio_route_state.owned_bgm == snapshot.owned_bgm
                && g_audio_route_state.owned_sound == snapshot.owned_sound
                && g_audio_route_state.owned_request_handle
                    == snapshot.owned_request_handle
                && g_audio_route_state.custom_resource_owned
                && g_audio_route_state.list_cleanup_pending;
            // The lineage that authorized this branch must still be the one on
            // record.  The epoch covers every lifecycle mutation, so a record
            // that was consumed, claimed or re-armed between the evidence read
            // and this commit cannot be retired against stale proof.
            const bool lineage_unchanged =
                g_onmemory_bank_lifecycle.state_epoch() == lineage_state_epoch
                && (shared_lineage
                    ? g_onmemory_bank_lifecycle.shared_matches(retired_sound,
                        snapshot.owned_request_handle, snapshot.generation)
                    : static_cast<bool>(g_onmemory_bank_lifecycle.active()));
            if (route_unchanged && lineage_unchanged) {
                result = g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::CanonicalSubstrateRelinquished,
                    snapshot.lease_identity);
                // The shared observation is consumed here because nothing else
                // will ever act on it.  The detached record is left intact:
                // it is the release authority, and the release runs after this
                // transaction has committed the route retirement.
                const bool lineage_retired = result.clear_route_metadata
                    && (!shared_lineage
                        || g_onmemory_bank_lifecycle.consume_shared(retired_sound,
                            snapshot.owned_request_handle, snapshot.generation));
                if (lineage_retired) {
                    AudioRouteTransitionRecorder route_transition_record(
                        AudioRouteTransitionReason::ListReturnRouteRestoreRelinquish,
                        AudioRouteTransitionKind::RouteReset);
                    g_pending_play_setup_patch = {};
                    g_failed_patch_journal.clear();
                    g_piano_audio_request_profile = {};
                    ++g_audio_route_state.generation;
                    g_audio_route_state.phase = AudioRoutePhase::Idle;
                    g_audio_route_state.lease_identity = {};
                    g_audio_route_state.desired_song_id.clear();
                    g_audio_route_state.patched_song_id.clear();
                    g_audio_route_state.sound = nullptr;
                    g_audio_route_state.stop_observed = false;
                    g_audio_route_state.stop_authorized_generation = 0;
                    g_audio_route_state.set_play_handoff_pending = false;
                    g_audio_route_state.custom_resource_owned = false;
                    g_audio_route_state.aggregate_awaiting_transition = false;
                    g_audio_route_state.owned_slot = nullptr;
                    g_audio_route_state.owned_bgm = nullptr;
                    g_audio_route_state.owned_sound = nullptr;
                    g_audio_route_state.owned_sound_identity = {};
                    g_audio_route_state.owned_request_handle = 0;
                    g_audio_route_state.reusable_sound = sound;
                    g_audio_route_state.reusable_sound_identity =
                        release_sound_identity;
                    g_audio_route_state.reusable_slot = slot;
                    g_audio_route_state.reusable_bgm = bgm;
                    g_audio_route_state.list_cleanup_pending = false;
                    g_audio_route_state.native_clear_verified = false;
                    relinquished = true;
                    relinquished_state_epoch =
                        g_onmemory_bank_lifecycle.state_epoch();
                }
            } else {
                result = apply_policy_locked(snapshot.lease_identity);
            }
        }
        if (result.thaw_profile) registry().clear_frozen_profile();
        if (relinquished) retire_registry_cleanup(snapshot.lease_identity);
        if (diagnostic) diagnostic->clear_attempted = false;

        // Second obligation: free the bank the mod caused to load.  Reached
        // only for a detached lineage, and proven by the arm-time record plus
        // the retired request handle -- never by live-sound identity.  The
        // route retirement above has already committed on its own evidence, so
        // a rejected claim leaves the bank retained without re-latching the
        // list.
        const char* release_status = detached_lineage ? "claim_rejected" : "none";
        if (relinquished && detached_lineage) {
            uint64_t owner_token = 0;
            const bool owner_readable = core::safe_read_field(
                snapshot.owned_sound,
                runtime_layouts::SqexSeadSound::observed_field548, owner_token);
            const OnMemoryBankSoundIdentity retired_sound{
                snapshot.owned_sound, snapshot.owned_sound_identity.live};
            OnMemoryBankRetirementFacts release_facts;
            release_facts.route_generation = detached_bank.route_generation + 1;
            release_facts.cleanup_generation = detached_bank.cleanup_generation;
            release_facts.retired_request_handle = detached_bank.request_handle;
            release_facts.current_request_handle =
                list_return_retirement.current_request_handle;
            release_facts.current_backing = list_return_retirement.current_backing;
            release_facts.current_backing_observed =
                list_return_retirement.current_backing_observed;
            release_facts.retired_backing = list_return_retirement.retired_backing;
            release_facts.retired_backing_observed =
                list_return_retirement.retired_backing_observed;
            release_facts.current_owner = classify_onmemory_bank_retirement_owner(
                detached_bank, retired_sound,
                retirement_facts.retired_sound_identity_exact, owner_readable,
                owner_token);
            release_facts.exact_request_retired = custom_request_retired;
            release_facts.route_released = true;
            release_facts.playback_released = true;
            release_facts.cleanup_released = true;
            release_facts.owner_restore_verified = owner_readable
                && (owner_token == 0
                    || owner_token == detached_bank.canonical.encode());
            release_facts.runtime_installed =
                g_audio_route_installed.load(std::memory_order_acquire);
            release_facts.lookup_signature_valid =
                g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire);
            release_facts.release_signature_valid =
                g_onmemory_bank_release_available.load(std::memory_order_acquire);
            release_facts.shutdown_or_disabled = !release_facts.runtime_installed
                || g_audio_route_disabled.load(std::memory_order_acquire);
            OnMemoryBankReleaseAction release_action;
            bool release_claimed = false;
            {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                release_claimed = g_onmemory_bank_lifecycle.state_epoch()
                        == relinquished_state_epoch
                    && g_onmemory_bank_lifecycle.claim_release(
                        release_facts, release_action);
            }
            if (release_claimed) {
                observe_bgm_playback_release(release_action, false);
                if (bgm_aggregate_release_has_borrowers(release_action)) {
                    // Another aggregate borrower still references the bank; the
                    // deferred path owns the native call once it drains.
                    defer_bgm_aggregate_release(release_action);
                    release_status = "deferred";
                } else {
                    const auto execution = execute_onmemory_bank_release(
                        release_facts.lookup_signature_valid,
                        release_facts.release_signature_valid,
                        release_facts.shutdown_or_disabled,
                        release_action,
                        [](const uint64_t token) noexcept {
                            return lookup_onmemory_bank_kind_noexcept(token);
                        },
                        [](const uint64_t* token,
                            const uint8_t asynchronous) noexcept {
                            return release_onmemory_bank_async_noexcept(
                                token, asynchronous);
                        });
                    {
                        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                        (void)g_onmemory_bank_lifecycle.finish_release(
                            release_action, execution.outcome);
                    }
                    if (execution.outcome != OnMemoryBankReleaseOutcome::Failed) {
                        observe_bgm_playback_release(release_action, true);
                    }
                    release_status = execution.outcome
                            == OnMemoryBankReleaseOutcome::AlreadyAbsent
                        ? "already_absent"
                        : execution.outcome
                                == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                            ? "async_requested" : "failed";
                }
                observe_bgm_playback_aggregate(true, false);
            }
        }

        std::ostringstream restore_out;
        restore_out << "[audio_sead] list_return_release status="
            << (relinquished ? "released" : "route_restore_retain_failed")
            << " authority=route_restore"
            << " lineage=" << (shared_lineage ? "shared" : "detached")
            << " cleanup=" << (relinquished ? "verified" : "retained")
            << " bank_release=" << release_status
            << " shared_token=" << shared_bank.token.encode()
            << " detached_custom=" << detached_bank.custom.encode()
            << " route_generation=" << snapshot.generation;
        core::log(relinquished ? core::LogLevel::Info : core::LogLevel::Error,
            restore_out.str());
        return result;
    }

    const bool route_resource_pinned = route_owned
        || (uobject_identity_matches(
                snapshot.owned_sound, snapshot.owned_sound_identity)
            && pin_live_uobject_handle(snapshot.owned_sound,
                snapshot.owned_sound_identity.live));
    if (!route_resource_pinned
        || !pin_live_uobject_handle(sound, release_sound_identity.live)) {
        AudioRouteCleanupResult result;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            result = apply_policy_locked(snapshot.lease_identity);
        }
        core::log(core::LogLevel::Error, "[audio_sead] list_return_release status=pin_failed cleanup=retained");
        return result;
    }

    void* pre_clear_slot = nullptr;
    void* pre_clear_bgm = nullptr;
    void* pre_clear_sound = nullptr;
    uint64_t pre_clear_request_handle = 0;
    uint8_t pre_clear_state = 0;
    const bool pre_clear_valid = lookup_current_bgm_controller() == snapshot.controller
        && uobject_identity_matches(snapshot.controller, snapshot.controller_identity)
        && uobject_identity_matches(sound, release_sound_identity)
        && read_controller_bgm_chain(snapshot.controller, pre_clear_slot, pre_clear_bgm)
        && core::safe_read_field(pre_clear_bgm, runtime_layouts::SqexSeadBgm::sound, pre_clear_sound)
        && core::safe_read_field(pre_clear_bgm, runtime_layouts::SqexSeadBgm::request_handle, pre_clear_request_handle)
        && core::safe_read_field(pre_clear_slot, runtime_layouts::SqexSeadSlot::state, pre_clear_state)
        && pre_clear_slot == slot
        && pre_clear_bgm == bgm
        && pre_clear_sound == sound
        && pre_clear_request_handle == request_handle
        && pre_clear_state == 4;
    bool aggregate_pre_call_revalidated = !aggregate_canonical_exit_exact;
    if (aggregate_canonical_exit_exact) {
        bool lifecycle_exact = false;
        bool route_snapshot_exact = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            const auto lifecycle = g_onmemory_bank_lifecycle.active();
            lifecycle_exact = lifecycle
                && g_onmemory_bank_lifecycle.state_epoch()
                    == aggregate_candidate.lifecycle_state_epoch
                && lifecycle.canonical.encode() == aggregate_candidate.canonical
                && lifecycle.custom.encode() == aggregate_candidate.custom;
            route_snapshot_exact = g_audio_route_state.generation
                    == snapshot.generation
                && g_audio_route_state.lease_identity
                    == snapshot.lease_identity
                && g_audio_route_state.controller == snapshot.controller
                && g_audio_route_state.custom_resource_owned
                && g_audio_route_state.owned_slot == snapshot.owned_slot
                && g_audio_route_state.owned_bgm == snapshot.owned_bgm
                && g_audio_route_state.owned_sound == snapshot.owned_sound
                && g_audio_route_state.owned_sound_identity.live.internal_index
                    == snapshot.owned_sound_identity.live.internal_index
                && g_audio_route_state.owned_sound_identity.live.serial_number
                    == snapshot.owned_sound_identity.live.serial_number
                && g_audio_route_state.owned_request_handle
                    == snapshot.owned_request_handle;
        }
        bool borrower_exact = false;
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            const BgmPlaybackBorrowerRecord* current_record = nullptr;
            uint64_t latest_exit_ordinal = 0;
            for (const auto& record : g_bgm_playback_borrowers) {
                if (record.active && record.list_exit) {
                    latest_exit_ordinal = std::max(
                        latest_exit_ordinal, record.ordinal);
                }
                if (record.active
                    && record.ordinal == aggregate_candidate.ordinal) {
                    current_record = &record;
                }
            }
            borrower_exact = current_record
                && g_bgm_playback_collection_version
                    == aggregate_authority.collection_version
                && latest_exit_ordinal == aggregate_candidate.ordinal
                && current_record->version == aggregate_candidate.version
                && current_record->list_exit
                && current_record->current_active
                && !current_record->failed && !current_record->destination_aba
                && !current_record->old_identity_conflict
                && !current_record->lineage_identity_conflict
                && current_record->continuity_exact
                && !current_record->transition_set_pending
                && !current_record->canonical_stop_boundary_active
                && !current_record->exit_unresolved_native_request
                && current_record->latest_custom_transition_handle == 0
                && current_record->latest_custom_transition_sound == nullptr
                && current_record->latest_custom_transition_child_ordinal == 0
                && current_record->controller == aggregate_candidate.controller
                && controller_identity_proof_matches(
                    current_record->controller_proof,
                    aggregate_candidate.controller_proof)
                && current_record->slot == aggregate_candidate.slot
                && current_record->bgm == aggregate_candidate.bgm
                && current_record->new_handle == aggregate_candidate.new_handle
                && current_record->canonical_intermediate_handle
                    == aggregate_candidate.canonical_intermediate_handle
                && current_record->expected_new_sound
                    == aggregate_candidate.expected_new_sound
                && current_record->expected_new_sound_identity.internal_index
                    == aggregate_candidate.expected_new_sound_identity.internal_index
                && current_record->expected_new_sound_identity.serial_number
                    == aggregate_candidate.expected_new_sound_identity.serial_number
                && current_record->canonical_intermediate_route_generation
                    == aggregate_candidate.canonical_intermediate_route_generation
                && current_record->lease == aggregate_candidate.lease
                && current_record->lifecycle_state_epoch
                    == aggregate_candidate.lifecycle_state_epoch
                && current_record->token_epoch == aggregate_candidate.token_epoch
                && current_record->canonical == aggregate_candidate.canonical
                && current_record->custom == aggregate_candidate.custom;
        }
        aggregate_pre_call_revalidated = lifecycle_exact
            && route_snapshot_exact && borrower_exact
            && g_bgm_aggregate_exit_requested.load(std::memory_order_acquire)
            && g_bgm_aggregate_exit_pending.load(std::memory_order_acquire)
            && g_bgm_aggregate_mutation_gate.closed()
            && g_bgm_aggregate_mutation_gate.drained();
        if (diagnostic) {
            diagnostic->aggregate_pre_call_revalidated =
                aggregate_pre_call_revalidated;
        }
    }
    if (!pre_clear_valid || !aggregate_pre_call_revalidated) {
        if (diagnostic && !aggregate_pre_call_revalidated) {
            diagnostic->blocker =
                ListReturnClearAuthorityBlocker::AggregateVersionDrift;
        }
        AudioRouteCleanupResult result;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            result = apply_policy_locked(snapshot.lease_identity);
        }
        core::log(core::LogLevel::Error, "[audio_sead] list_return_release status=pre_clear_validation_failed cleanup=retained");
        return result;
    }

    const bool aggregate_canonical_substrate_path =
        !route_owned && aggregate_canonical_exit_exact;
    if (aggregate_canonical_substrate_path) {
        const bool relinquishment_authorized =
            bgm_playback_canonical_substrate_relinquishment_exact({
                true,
                aggregate_canonical_exit_exact,
                aggregate_pre_call_revalidated,
                aggregate_candidate.version != 0
                    && aggregate_authority.collection_version != 0
                    && aggregate_candidate.list_exit_epoch != 0,
            });
        const bool native_clear_required =
            bgm_playback_list_return_native_clear_required(
                route_owned, relinquishment_authorized);
        AudioRouteCleanupResult substrate_result{
            AudioRouteCleanupStatus::Retained,
            snapshot.lease_identity, false, false};
        bool route_relinquished = false;
        bool borrower_finalized = false;
        bool thaw_profile = false;
        uint64_t transaction_generation = 0;
        uint64_t pending_record_version = 0;
        uint64_t pending_collection_version = 0;
        uint64_t route_committed_record_version = 0;
        uint64_t route_committed_collection_version = 0;
        if (relinquishment_authorized && !native_clear_required) {
            // Phase 1: publish an immutable borrower proposal without holding
            // route/audio-state authority.
            {
                std::lock_guard<std::mutex> lock(
                    g_bgm_playback_borrower_mutex);
                BgmPlaybackBorrowerRecord* record = nullptr;
                uint64_t latest_exit_ordinal = 0;
                for (auto& candidate : g_bgm_playback_borrowers) {
                    if (candidate.active && candidate.list_exit) {
                        latest_exit_ordinal = std::max(
                            latest_exit_ordinal, candidate.ordinal);
                    }
                    if (candidate.active
                        && candidate.ordinal == aggregate_candidate.ordinal) {
                        record = &candidate;
                    }
                }
                const bool proposal_exact = record
                    && latest_exit_ordinal == aggregate_candidate.ordinal
                    && g_bgm_playback_collection_version
                        == aggregate_authority.collection_version
                    && record->version == aggregate_candidate.version
                    && record->list_exit_epoch
                        == aggregate_candidate.list_exit_epoch
                    && record->current_active && !record->failed
                    && !record->destination_aba
                    && !record->old_identity_conflict
                    && !record->lineage_identity_conflict
                    && record->continuity_exact
                    && !record->transition_set_pending
                    && !record->canonical_stop_boundary_active
                    && !record->exit_unresolved_native_request
                    && record->latest_custom_transition_handle == 0
                    && record->new_handle == request_handle
                    && record->canonical_intermediate_handle == request_handle
                    && record->expected_new_sound == sound
                    && record->expected_new_sound_identity.internal_index
                        == release_sound_identity.live.internal_index
                    && record->expected_new_sound_identity.serial_number
                        == release_sound_identity.live.serial_number
                    && record->canonical_substrate_relinquishment_state
                        == BgmPlaybackCanonicalSubstrateRelinquishmentState::None
                    && g_bgm_canonical_substrate_transaction_generation
                        != UINT64_MAX;
                if (proposal_exact) {
                    transaction_generation =
                        ++g_bgm_canonical_substrate_transaction_generation;
                    record->canonical_substrate_relinquishment_state =
                        BgmPlaybackCanonicalSubstrateRelinquishmentState::Pending;
                    record->canonical_substrate_transaction_generation =
                        transaction_generation;
                    record->canonical_substrate_handle = request_handle;
                    record->canonical_substrate_sound = sound;
                    record->canonical_substrate_sound_identity =
                        release_sound_identity.live;
                    record->canonical_substrate_route_generation =
                        aggregate_candidate.canonical_intermediate_route_generation;
                    record->canonical_substrate_lease = aggregate_candidate.lease;
                    ++record->version;
                    ++g_bgm_playback_collection_version;
                    pending_record_version = record->version;
                    pending_collection_version =
                        g_bgm_playback_collection_version;
                }
            }
            if (transaction_generation != 0 && diagnostic) {
                diagnostic->canonical_substrate_pending_published = true;
                diagnostic->canonical_substrate_state =
                    BgmPlaybackCanonicalSubstrateRelinquishmentState::Pending;
                diagnostic->canonical_substrate_transaction_generation =
                    transaction_generation;
            }

            // Revalidate the immutable proposal before entering the route-local
            // commit. No borrower lock is held while audio state is mutated.
            bool pending_exact = false;
            {
                std::lock_guard<std::mutex> lock(
                    g_bgm_playback_borrower_mutex);
                for (const auto& record : g_bgm_playback_borrowers) {
                    if (record.active
                        && record.ordinal == aggregate_candidate.ordinal) {
                        pending_exact = record.version == pending_record_version
                            && g_bgm_playback_collection_version
                                == pending_collection_version
                            && record.list_exit_epoch
                                == aggregate_candidate.list_exit_epoch
                            && record.canonical_substrate_transaction_generation
                                == transaction_generation
                            && record.canonical_substrate_relinquishment_state
                                == BgmPlaybackCanonicalSubstrateRelinquishmentState::Pending
                            && bgm_playback_canonical_substrate_phase_exact({
                                transaction_generation != 0,
                                true,
                                record.ordinal == aggregate_candidate.ordinal,
                                record.version == pending_record_version,
                                g_bgm_playback_collection_version
                                    == pending_collection_version,
                                record.list_exit_epoch
                                    == aggregate_candidate.list_exit_epoch,
                                record.canonical_substrate_route_generation
                                    == aggregate_candidate
                                        .canonical_intermediate_route_generation,
                                record.lifecycle_state_epoch
                                    == aggregate_candidate.lifecycle_state_epoch,
                                record.controller == aggregate_candidate.controller
                                    && controller_identity_proof_matches(
                                        record.controller_proof,
                                        aggregate_candidate.controller_proof),
                                record.canonical_substrate_handle == request_handle
                                    && record.canonical_substrate_sound == sound
                                    && record.canonical_substrate_sound_identity.internal_index
                                        == release_sound_identity.live.internal_index
                                    && record.canonical_substrate_sound_identity.serial_number
                                        == release_sound_identity.live.serial_number,
                                record.canonical == aggregate_candidate.canonical
                                    && record.custom == aggregate_candidate.custom,
                            });
                        break;
                    }
                }
            }

            // Phase 2: relinquish route metadata locally. The native canonical
            // request is neither cleared nor otherwise mutated.
            if (pending_exact) {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                const auto lifecycle = g_onmemory_bank_lifecycle.active();
                const bool route_exact = lifecycle
                    && g_onmemory_bank_lifecycle.state_epoch()
                        == aggregate_candidate.lifecycle_state_epoch
                    && lifecycle.canonical.encode() == aggregate_candidate.canonical
                    && lifecycle.custom.encode() == aggregate_candidate.custom
                    && g_audio_route_state.generation == snapshot.generation
                    && g_audio_route_state.lease_identity == snapshot.lease_identity
                    && g_audio_route_state.controller == snapshot.controller
                    && g_audio_route_state.custom_resource_owned
                    && g_audio_route_state.owned_slot == snapshot.owned_slot
                    && g_audio_route_state.owned_bgm == snapshot.owned_bgm
                    && g_audio_route_state.owned_sound == snapshot.owned_sound
                    && g_audio_route_state.owned_request_handle
                        == snapshot.owned_request_handle
                    && bgm_playback_canonical_substrate_phase_exact({
                        transaction_generation != 0,
                        pending_exact,
                        aggregate_candidate.ordinal != 0,
                        pending_record_version != 0,
                        pending_collection_version != 0,
                        aggregate_candidate.list_exit_epoch != 0,
                        g_audio_route_state.generation == snapshot.generation,
                        g_onmemory_bank_lifecycle.state_epoch()
                            == aggregate_candidate.lifecycle_state_epoch,
                        g_audio_route_state.controller == snapshot.controller,
                        request_handle != 0 && sound != nullptr,
                        lifecycle.canonical.encode() == aggregate_candidate.canonical
                            && lifecycle.custom.encode() == aggregate_candidate.custom,
                    });
                if (route_exact) {
                    g_audio_route_state.phase =
                        AudioRoutePhase::CanonicalRelinquishmentPending;
                    g_audio_route_state.canonical_relinquishment_generation =
                        transaction_generation;
                    substrate_result = g_frozen_profile_lease.transition(
                        AudioRouteCleanupEvent::CanonicalSubstrateRelinquished,
                        snapshot.lease_identity);
                    if (substrate_result.clear_route_metadata) {
                        AudioRouteTransitionRecorder route_transition_record(
                            AudioRouteTransitionReason::ListReturnCanonicalRelinquish,
                            AudioRouteTransitionKind::RouteReset);
                        thaw_profile = substrate_result.thaw_profile;
                        g_pending_play_setup_patch = {};
                        g_failed_patch_journal.clear();
                        g_piano_audio_request_profile = {};
                        ++g_audio_route_state.generation;
                        g_audio_route_state.phase =
                            AudioRoutePhase::CanonicalRelinquished;
                        g_audio_route_state.lease_identity = {};
                        g_audio_route_state.desired_song_id.clear();
                        g_audio_route_state.patched_song_id.clear();
                        g_audio_route_state.sound = nullptr;
                        g_audio_route_state.stop_observed = false;
                        g_audio_route_state.stop_authorized_generation = 0;
                        g_audio_route_state.set_play_handoff_pending = false;
                        g_audio_route_state.custom_resource_owned = false;
                        g_audio_route_state.aggregate_awaiting_transition = false;
                        g_audio_route_state.owned_slot = nullptr;
                        g_audio_route_state.owned_bgm = nullptr;
                        g_audio_route_state.owned_sound = nullptr;
                        g_audio_route_state.owned_sound_identity = {};
                        g_audio_route_state.owned_request_handle = 0;
                        g_audio_route_state.reusable_sound = sound;
                        g_audio_route_state.reusable_sound_identity =
                            release_sound_identity;
                        g_audio_route_state.reusable_slot = slot;
                        g_audio_route_state.reusable_bgm = bgm;
                        g_audio_route_state.list_cleanup_pending = false;
                        g_audio_route_state.native_clear_verified = false;
                        route_relinquished = true;
                    }
                }
            }
            if (route_relinquished && diagnostic) {
                diagnostic->canonical_substrate_route_committed = true;
                diagnostic->canonical_substrate_state =
                    BgmPlaybackCanonicalSubstrateRelinquishmentState::RouteCommitted;
            }

            // Publish the completed route-local phase without holding audio
            // state. An interruption here remains explicitly release-blocking.
            if (route_relinquished) {
                bool route_commit_current = false;
                {
                    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                    route_commit_current =
                        g_audio_route_state.phase
                            == AudioRoutePhase::CanonicalRelinquished
                        && g_audio_route_state.canonical_relinquishment_generation
                            == transaction_generation
                        && g_audio_route_state.generation
                            == snapshot.generation + 1
                        && !g_audio_route_state.custom_resource_owned
                        && g_audio_route_state.owned_request_handle == 0;
                }
                if (!route_commit_current) {
                    if (diagnostic) diagnostic->canonical_substrate_partial = true;
                } else {
                    std::lock_guard<std::mutex> lock(
                        g_bgm_playback_borrower_mutex);
                    for (auto& record : g_bgm_playback_borrowers) {
                        if (!record.active
                            || record.ordinal != aggregate_candidate.ordinal) continue;
                        const bool route_phase_exact =
                            bgm_playback_canonical_substrate_phase_exact({
                            transaction_generation != 0,
                            record.canonical_substrate_relinquishment_state
                                == BgmPlaybackCanonicalSubstrateRelinquishmentState::Pending,
                            record.ordinal == aggregate_candidate.ordinal,
                            record.version == pending_record_version,
                            g_bgm_playback_collection_version
                                == pending_collection_version,
                            record.list_exit_epoch
                                == aggregate_candidate.list_exit_epoch,
                            record.canonical_substrate_route_generation
                                == aggregate_candidate
                                    .canonical_intermediate_route_generation,
                            record.lifecycle_state_epoch
                                == aggregate_candidate.lifecycle_state_epoch,
                            record.controller == aggregate_candidate.controller
                                && controller_identity_proof_matches(
                                    record.controller_proof,
                                    aggregate_candidate.controller_proof),
                            record.canonical_substrate_handle == request_handle
                                && record.canonical_substrate_sound == sound
                                && record.canonical_substrate_sound_identity.internal_index
                                    == release_sound_identity.live.internal_index
                                && record.canonical_substrate_sound_identity.serial_number
                                    == release_sound_identity.live.serial_number,
                            record.canonical == aggregate_candidate.canonical
                                && record.custom == aggregate_candidate.custom,
                            });
                        if (!route_phase_exact) break;
                        record.canonical_substrate_relinquishment_state =
                            BgmPlaybackCanonicalSubstrateRelinquishmentState::RouteCommitted;
                        ++record.version;
                        ++g_bgm_playback_collection_version;
                        route_committed_record_version = record.version;
                        route_committed_collection_version =
                            g_bgm_playback_collection_version;
                        break;
                    }
                }
            }

            // Phase 3: finalize borrower ownership only if the proposal remains
            // byte-for-byte current. A failed finalization leaves a blocked,
            // non-clearing partial transaction for shutdown diagnostics.
            if (route_relinquished
                && route_committed_record_version != 0
                && route_committed_collection_version != 0) {
                std::lock_guard<std::mutex> lock(
                    g_bgm_playback_borrower_mutex);
                for (auto& record : g_bgm_playback_borrowers) {
                    if (!record.active
                        || record.ordinal != aggregate_candidate.ordinal) continue;
                    const bool finalize_exact = record.version
                            == route_committed_record_version
                        && g_bgm_playback_collection_version
                            == route_committed_collection_version
                        && record.list_exit_epoch
                            == aggregate_candidate.list_exit_epoch
                        && record.canonical_substrate_transaction_generation
                            == transaction_generation
                        && record.canonical_substrate_relinquishment_state
                            == BgmPlaybackCanonicalSubstrateRelinquishmentState::RouteCommitted
                        && record.new_handle == request_handle
                        && record.canonical_intermediate_handle == request_handle
                        && record.expected_new_sound == sound
                        && record.expected_new_sound_identity.internal_index
                            == release_sound_identity.live.internal_index
                        && record.expected_new_sound_identity.serial_number
                            == release_sound_identity.live.serial_number
                        && !record.failed && !record.destination_aba
                        && !record.old_identity_conflict
                        && !record.lineage_identity_conflict
                        && record.continuity_exact
                        && !record.transition_set_pending
                        && !record.canonical_stop_boundary_active
                        && !record.exit_unresolved_native_request
                        && bgm_playback_canonical_substrate_phase_exact({
                            transaction_generation != 0,
                            true,
                            record.ordinal == aggregate_candidate.ordinal,
                            record.version == route_committed_record_version,
                            g_bgm_playback_collection_version
                                == route_committed_collection_version,
                            record.list_exit_epoch
                                == aggregate_candidate.list_exit_epoch,
                            record.canonical_substrate_route_generation
                                == aggregate_candidate
                                    .canonical_intermediate_route_generation,
                            record.lifecycle_state_epoch
                                == aggregate_candidate.lifecycle_state_epoch,
                            record.controller == aggregate_candidate.controller
                                && controller_identity_proof_matches(
                                    record.controller_proof,
                                    aggregate_candidate.controller_proof),
                            record.canonical_substrate_handle == request_handle
                                && record.canonical_substrate_sound == sound
                                && record.canonical_substrate_sound_identity.internal_index
                                    == release_sound_identity.live.internal_index
                                && record.canonical_substrate_sound_identity.serial_number
                                    == release_sound_identity.live.serial_number,
                            record.canonical == aggregate_candidate.canonical
                                && record.custom == aggregate_candidate.custom,
                        });
                    if (!finalize_exact) break;
                    const uint64_t epoch = ++g_bgm_playback_observation_epoch;
                    record.canonical_substrate_relinquished = true;
                    record.canonical_substrate_current_exact = true;
                    record.canonical_substrate_relinquishment_state =
                        BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished;
                    record.canonical_substrate_relinquishment_epoch = epoch;
                    record.current_active = false;
                    ++record.version;
                    ++g_bgm_playback_collection_version;
                    g_bgm_canonical_substrate_proof = {
                        true,
                        BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished,
                        transaction_generation,
                        record.ordinal, record.version,
                        g_bgm_playback_collection_version,
                        record.list_exit_epoch, record.controller,
                        record.controller_proof, record.slot, record.bgm,
                        sound, release_sound_identity.live, request_handle,
                        0, {}, 0,
                        record.canonical_intermediate_route_generation,
                        record.lease, record.lifecycle_state_epoch,
                        record.canonical, record.custom,
                    };
                    borrower_finalized = true;
                    break;
                }
            }
        }
        if (route_relinquished && borrower_finalized) {
            if (thaw_profile) registry().clear_frozen_profile();
            retire_registry_cleanup(snapshot.lease_identity);
            if (diagnostic) {
                diagnostic->clear_attempted = false;
                diagnostic->aggregate_clear_committed = false;
                diagnostic->canonical_substrate_relinquished = true;
                diagnostic->canonical_substrate_borrower_finalized = true;
                diagnostic->canonical_substrate_state =
                    BgmPlaybackCanonicalSubstrateRelinquishmentState::CanonicalRelinquished;
                diagnostic->post_clear_exact = false;
                diagnostic->blocker = ListReturnClearAuthorityBlocker::None;
            }
            return substrate_result;
        }
        if (diagnostic) diagnostic->canonical_substrate_partial = true;
        return substrate_result;
    }

    if (diagnostic) diagnostic->clear_attempted = true;
    call_bgm_slot_set_original(snapshot.controller, nullptr);
    void* cleared_slot = nullptr;
    void* cleared_bgm = nullptr;
    void* cleared_sound = sound;
    uint64_t cleared_request_handle = request_handle;
    uint8_t cleared_state = slot_state;
    void* current_controller_after = lookup_current_bgm_controller();
    const bool cleared_chain_valid = current_controller_after == snapshot.controller
        && uobject_identity_matches(snapshot.controller, snapshot.controller_identity)
        && uobject_identity_matches(sound, release_sound_identity)
        && read_controller_bgm_chain(snapshot.controller, cleared_slot, cleared_bgm)
        && core::safe_read_field(cleared_bgm, runtime_layouts::SqexSeadBgm::sound, cleared_sound)
        && core::safe_read_field(cleared_bgm, runtime_layouts::SqexSeadBgm::request_handle, cleared_request_handle)
        && core::safe_read_field(cleared_slot, runtime_layouts::SqexSeadSlot::state, cleared_state)
        && cleared_slot == slot
        && cleared_bgm == bgm
        && cleared_sound == nullptr
        && cleared_request_handle == 0
        && cleared_state == 0;
    const AudioNativeRouteObservation owned_observation{
        slot, bgm, route_owned ? snapshot.owned_sound : sound,
        route_owned ? snapshot.owned_request_handle : request_handle,
        4, true,
        snapshot.generation, snapshot.lease_identity,
    };
    const AudioNativeRouteObservation cleared_observation{
        cleared_slot, cleared_bgm, cleared_sound, cleared_request_handle,
        cleared_state, cleared_chain_valid, snapshot.generation, snapshot.lease_identity,
    };
    const bool cleared = cleared_chain_valid && native_route_release_proven(
        owned_observation, cleared_observation, snapshot.generation);
    bool state_committed = false;
    AudioRouteCleanupResult release_result;
    if (cleared) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_audio_route_state.generation == snapshot.generation
            && g_audio_route_state.lease_identity == snapshot.lease_identity
            && g_audio_route_state.controller == snapshot.controller
            && ((snapshot.custom_resource_owned
                    && g_audio_route_state.owned_request_handle == snapshot.owned_request_handle)
                || (!snapshot.custom_resource_owned && g_audio_route_state.list_cleanup_pending))) {
            g_audio_route_state.native_clear_verified = true;
            release_result = apply_policy_locked(snapshot.lease_identity, true, true);
            if (!release_result.clear_route_metadata) {
                core::log(core::LogLevel::Error,
                    "[audio_sead] list_return_release status=lease_identity_mismatch cleanup=retained");
            } else {
                AudioRouteTransitionRecorder route_transition_record(
                    AudioRouteTransitionReason::ListReturnNativeClear,
                    AudioRouteTransitionKind::RouteReset);
                g_pending_play_setup_patch = {};
                g_failed_patch_journal.clear();
                g_piano_audio_request_profile = {};
                ++g_audio_route_state.generation;
                g_audio_route_state.phase = AudioRoutePhase::Idle;
                g_audio_route_state.lease_identity = {};
                g_audio_route_state.desired_song_id.clear();
                g_audio_route_state.patched_song_id.clear();
                g_audio_route_state.sound = nullptr;
                g_audio_route_state.stop_observed = false;
                g_audio_route_state.stop_authorized_generation = 0;
                g_audio_route_state.set_play_handoff_pending = false;
                g_audio_route_state.custom_resource_owned = false;
                g_audio_route_state.aggregate_awaiting_transition = false;
                g_audio_route_state.owned_slot = nullptr;
                g_audio_route_state.owned_bgm = nullptr;
                g_audio_route_state.owned_sound = nullptr;
                g_audio_route_state.owned_sound_identity = {};
                g_audio_route_state.owned_request_handle = 0;
                g_audio_route_state.reusable_sound = route_owned
                    ? sound : snapshot.owned_sound;
                g_audio_route_state.reusable_sound_identity = route_owned
                    ? release_sound_identity : snapshot.owned_sound_identity;
                g_audio_route_state.reusable_slot = slot;
                g_audio_route_state.reusable_bgm = bgm;
                g_audio_route_state.list_cleanup_pending = false;
                g_audio_route_state.native_clear_verified = false;
                state_committed = true;
            }
        }
    }
    const bool released = cleared && state_committed;
    const bool post_clear_exact = bgm_playback_list_return_post_clear_exact(
        BgmPlaybackListReturnPostClearFacts{
            diagnostic ? diagnostic->clear_attempted : true,
            cleared_chain_valid,
            cleared_sound == nullptr,
            cleared_request_handle == 0,
            cleared_state == 0,
            state_committed,
        });
    if (diagnostic) {
        diagnostic->post_clear_exact = post_clear_exact;
        diagnostic->aggregate_clear_committed = released
            && bgm_playback_list_return_synchronous_absence_allowed(
                aggregate_canonical_exit_exact,
                aggregate_pre_call_revalidated,
                post_clear_exact);
        if (!post_clear_exact) {
            diagnostic->blocker =
                ListReturnClearAuthorityBlocker::PostClearValidation;
        } else {
            diagnostic->blocker = ListReturnClearAuthorityBlocker::None;
        }
    }
    // The registry may thaw only after native clear validation and route-state commit. This keeps
    // profile identity frozen for every callback that can still observe the owned audio route.
    if (released && release_result.thaw_profile) {
        registry().clear_frozen_profile();
    }
    if (released) retire_registry_cleanup(snapshot.lease_identity);
    if (!released) {
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            release_result = apply_policy_locked(snapshot.lease_identity);
        }
        g_audio_route_disabled.store(true, std::memory_order_release);
    }

    std::ostringstream out;
    out << "[audio_sead] list_return_release status=" << (released ? "released" : (cleared ? "state_changed" : "clear_failed"))
        << " controller=0x" << std::hex << reinterpret_cast<uintptr_t>(snapshot.controller)
        << " slot=0x" << reinterpret_cast<uintptr_t>(slot)
        << " bgm=0x" << reinterpret_cast<uintptr_t>(bgm)
        << " request_handle=0x" << request_handle
        << std::dec
        << " slot_state_before=" << static_cast<unsigned>(slot_state)
        << " slot_state_after=" << static_cast<unsigned>(cleared_state);
    core::log(released ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    return release_result;
}

AudioRouteCleanupResult release_audio_route_on_piano_list_return(int32_t item_index)
{
    if (item_index != 0) return {};
    revoke_selection_audio_activation();
    AudioCallbackScope callback_scope(AudioRouteCallbackKind::ListReturn);
    if (!callback_scope) {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        return g_frozen_profile_lease.active()
            ? AudioRouteCleanupResult{AudioRouteCleanupStatus::Retained,
                g_frozen_profile_lease.identity(), false, false}
            : AudioRouteCleanupResult{};
    }
    uint64_t diagnostic_selection_generation = 0;
    uint64_t diagnostic_route_generation = 0;
    uint64_t diagnostic_lease_generation = 0;
    uint64_t diagnostic_song_key = 0;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_unpublished_audio_setup
            && g_audio_route_state.phase == AudioRoutePhase::Armed
            && !g_audio_route_state.desired_song_id.empty()) {
            diagnostic_selection_generation
                = g_unpublished_audio_setup.selection.generation;
            diagnostic_route_generation = g_audio_route_state.generation;
            diagnostic_lease_generation
                = g_audio_route_state.lease_identity.generation;
            diagnostic_song_key
                = audio_route_song_key(g_audio_route_state.desired_song_id);
        }
    }
    if (diagnostic_selection_generation != 0
        && diagnostic_route_generation != 0
        && diagnostic_lease_generation != 0
        && diagnostic_song_key != 0) {
        ChartAudioDiagnosticTransaction transaction;
        if (chart_audio_diagnostic_transaction_exact(
                diagnostic_selection_generation,
                diagnostic_route_generation,
                diagnostic_lease_generation,
                diagnostic_song_key,
                transaction)) {
            finish_chart_audio_diagnostic_transaction(
                transaction, ChartAudioDiagnosticTerminalOutcome::ListExit);
        }
    }
    observe_bgm_playback_aggregate(true, false);
    PauseResumeBankMarker marker;
    OnMemoryBankDiagnosticPair diagnostic;
    ListReturnClearAuthorityDiagnostic clear_authority_diagnostic;
    OnMemoryBankReleaseAction release;
    AudioRouteCleanupResult result;
    bool safe_to_continue = true;
    {
        std::lock_guard<std::recursive_mutex> operation_lock(
            g_audio_route_operations.mutex());
        PauseResumeBankSession snapshot;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            snapshot = g_pause_resume_bank;
        }
        const auto coordinated = coordinate_pause_resume_list_return(
            snapshot.phase,
            [&]() {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                if (g_pause_resume_bank.phase != snapshot.phase
                    || g_pause_resume_bank.session_epoch != snapshot.session_epoch
                    || g_pause_resume_bank.cycle_epoch != snapshot.cycle_epoch) {
                    return false;
                }
                g_pause_resume_bank.exit_requested = true;
                // List return changes the source authority for any deferred
                // quiescence probe. Leave the monitor Waiting so a later tick
                // can expose one fresh ExitPending-bound probe.
                g_pause_resume_bank.pending_release_probe = {};
                g_pause_resume_bank.pending_release_evidence = {};
                return true;
            },
            [&]() { return restore_pause_resume_owner(&marker, true); },
            [&](PauseResumeBankPhase expected, PauseResumeBankPhase next) {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                if (g_pause_resume_bank.phase != expected
                    || g_pause_resume_bank.session_epoch != snapshot.session_epoch
                    || g_pause_resume_bank.cycle_epoch != snapshot.cycle_epoch) {
                    return false;
                }
                g_pause_resume_bank.phase = next;
                return true;
            },
            [&](bool release_ready) {
                if (release_ready) {
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
                    uint64_t owner_token = 0;
                    bool identity_observed = false;
                    bool owner_observed = false;
                    OnMemoryBankSoundIdentity observed_sound;
                    const auto fresh_release = coordinate_pause_resume_release_authority(
                        [&]() {
                            playback_before = registry().playback_snapshot();
                            cleanup_before = registry().cleanup_lease();
                            registry_generation_before =
                                registry().selection_snapshot().generation;
                            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                            if (!pause_resume_epoch_matches(snapshot.session_epoch,
                                    snapshot.cycle_epoch,
                                    g_pause_resume_bank.session_epoch,
                                    g_pause_resume_bank.cycle_epoch)
                                || g_pause_resume_bank.phase
                                    != PauseResumeBankPhase::ExitPending) {
                                return false;
                            }
                            route_generation = g_audio_route_state.generation;
                            route_lease = g_audio_route_state.lease_identity;
                            lifecycle_epoch = g_onmemory_bank_lifecycle.state_epoch();
                            lifecycle = g_onmemory_bank_lifecycle.active();
                            route_released = g_audio_route_state.phase
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
                            return true;
                        },
                        [&]() {
                            identity_observed = snapshot.detached.sound.object
                                && read_uobject_identity(
                                    snapshot.detached.sound.object, sound_identity);
                            owner_observed = identity_observed
                                && core::safe_read_field(snapshot.detached.sound.object,
                                    runtime_layouts::SqexSeadSound::observed_field548,
                                    owner_token);
                            observed_sound = {snapshot.detached.sound.object,
                                identity_observed ? sound_identity.live
                                                  : UObjectLiveHandle{}};
                            playback_after = registry().playback_snapshot();
                            cleanup_after = registry().cleanup_lease();
                            registry_generation_after =
                                registry().selection_snapshot().generation;
                            return true;
                        },
                        [&]() -> PauseResumeCoordinatorResult {
                            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                            const auto& active = g_onmemory_bank_lifecycle.active();
                            const bool exact_lifecycle = active && lifecycle
                                && active.ordinal == snapshot.detached.ordinal
                                && active.ordinal == lifecycle.ordinal
                                && active.sound == snapshot.detached.sound
                                && active.canonical.encode()
                                    == snapshot.detached.canonical.encode()
                                && active.custom.encode()
                                    == snapshot.detached.custom.encode()
                                && active.request_handle
                                    == snapshot.detached.request_handle
                                && active.backing_observed
                                    == snapshot.detached.backing_observed
                                && active.backing_identity
                                    == snapshot.detached.backing_identity;
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
                            OnMemoryBankRetirementFacts facts =
                                g_pause_resume_bank.retirement_facts;
                            facts.current_owner = classify_onmemory_bank_retirement_owner(
                                snapshot.detached, observed_sound,
                                identity_observed
                                    && observed_sound == snapshot.detached.sound,
                                owner_observed, owner_token);
                            const PauseResumeReleaseAuthorityFacts authority{
                                pause_resume_epoch_matches(snapshot.session_epoch,
                                    snapshot.cycle_epoch,
                                    g_pause_resume_bank.session_epoch,
                                    g_pause_resume_bank.cycle_epoch)
                                    && g_pause_resume_bank.phase
                                        == PauseResumeBankPhase::ExitPending,
                                route_generation == g_audio_route_state.generation
                                    && route_lease
                                        == g_audio_route_state.lease_identity,
                                route_released && route_released_now,
                                !playback_before.song
                                    && !playback_before.token.valid()
                                    && !playback_after.song
                                    && !playback_after.token.valid(),
                                !cleanup_before.song
                                    && !cleanup_before.token.valid()
                                    && !cleanup_after.song
                                    && !cleanup_after.token.valid()
                                    && registry_generation_before
                                        == registry_generation_after,
                                exact_lifecycle,
                                lifecycle_epoch == snapshot.lifecycle_state_epoch
                                    && g_onmemory_bank_lifecycle.state_epoch()
                                        == snapshot.lifecycle_state_epoch,
                                facts.current_owner.category
                                        == OnMemoryBankRetirementOwnerCategory::Canonical
                                    || facts.current_owner.category
                                        == OnMemoryBankRetirementOwnerCategory::Zero,
                                facts.cleanup_generation
                                    == snapshot.detached.cleanup_generation,
                                !g_onmemory_bank_lifecycle.release_in_flight()
                                    && !g_onmemory_bank_lifecycle.failed()
                                    && g_audio_route_installed.load(
                                        std::memory_order_acquire)
                                    && !g_audio_route_disabled.load(
                                        std::memory_order_acquire),
                            };
                            if (!pause_resume_release_authority_valid(authority)) {
                                return {};
                            }
                            facts.route_released = authority.route_released;
                            facts.playback_released = authority.playback_released;
                            facts.cleanup_released = authority.cleanup_released;
                            facts.owner_restore_verified =
                                authority.owner_canonical_or_zero;
                            facts.runtime_installed = g_audio_route_installed.load(
                                std::memory_order_acquire);
                            facts.lookup_signature_valid =
                                g_onmemory_bank_kind_lookup_available.load(
                                    std::memory_order_acquire);
                            facts.release_signature_valid =
                                g_onmemory_bank_release_available.load(
                                    std::memory_order_acquire);
                            facts.shutdown_or_disabled = !facts.runtime_installed
                                || g_audio_route_disabled.load(
                                    std::memory_order_acquire);
                            const auto scheduled = coordinate_onmemory_bank_retirement(
                                g_onmemory_bank_lifecycle, facts,
                                g_onmemory_bank_diagnostic_pair,
                                facts.route_generation, facts.cleanup_generation);
                            if (!scheduled.claimed) return {};
                            diagnostic = scheduled.diagnostic;
                            release = scheduled.release;
                            g_pause_resume_bank.retirement_facts = facts;
                            g_pause_resume_bank.phase =
                                PauseResumeBankPhase::ReleaseRequested;
                            marker = {PauseResumeBankMarkerStatus::ReleaseRequested,
                                "list_return", snapshot.session_epoch,
                                snapshot.cycle_epoch, true};
                            return {true, true, false};
                        },
                        []() {});
                    if (!fresh_release.action_exposed) return false;
                } else if (snapshot.phase == PauseResumeBankPhase::RetirementCandidate) {
                    marker = {PauseResumeBankMarkerStatus::ExitPending,
                        "list_return_probe_pending", 0, 0, true};
                } else if (pause_resume_list_return_outcome(snapshot.phase).phase
                    == PauseResumeBankPhase::ExitPending) {
                    marker = {PauseResumeBankMarkerStatus::ExitPending,
                        "list_return", snapshot.session_epoch,
                        snapshot.cycle_epoch, true};
                }
                result = release_audio_route_on_piano_list_return_impl(
                    AudioCleanupOperation::ListReturn, true, true, true,
                    &clear_authority_diagnostic);
                return true;
            },
            [&]() {
                std::lock_guard<std::mutex> lock(g_audio_state_mutex);
                g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
                g_pause_resume_bank.pending_release_probe = {};
                g_pause_resume_bank.pending_release_evidence = {};
                if (!marker.eligible) marker = {
                    PauseResumeBankMarkerStatus::Failed,
                    "list_return_validation", g_pause_resume_bank.session_epoch,
                    g_pause_resume_bank.cycle_epoch, true};
            });
        safe_to_continue = coordinated.committed;
        if (!safe_to_continue) result = {
            AudioRouteCleanupStatus::Retained, {}, false, false};
    }
    if ((clear_authority_diagnostic.aggregate_clear_committed
            && clear_authority_diagnostic.post_clear_exact)
        || clear_authority_diagnostic.canonical_substrate_relinquished) {
        observe_bgm_playback_aggregate(true, false);
        clear_authority_diagnostic.synchronous_absence_observer_ran = true;
    }
    publish_list_return_clear_authority_diagnostic(
        clear_authority_diagnostic);
    log_pause_resume_bank_marker(marker);
    log_onmemory_bank_pair("pause_resume_list_return", diagnostic);
    if (release) {
        observe_bgm_playback_release(release, false);
        if (bgm_aggregate_release_has_borrowers(release)) {
            defer_bgm_aggregate_release(release);
            release = {};
        }
        observe_bgm_playback_aggregate(true, false);
    }
    if (release) {
        const auto execution = execute_onmemory_bank_release(
            g_onmemory_bank_kind_lookup_available.load(std::memory_order_acquire),
            g_onmemory_bank_release_available.load(std::memory_order_acquire),
            !g_audio_route_installed.load(std::memory_order_acquire)
                || g_audio_route_disabled.load(std::memory_order_acquire),
            release,
            [](const uint64_t token) noexcept {
                return lookup_onmemory_bank_kind_noexcept(token);
            },
            [](const uint64_t* token, const uint8_t asynchronous) noexcept {
                return release_onmemory_bank_async_noexcept(token, asynchronous);
            });
        PauseResumeBankMarker release_marker;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            if (g_onmemory_bank_lifecycle.finish_release(release, execution.outcome)) {
                g_pause_resume_bank.phase =
                    execution.outcome == OnMemoryBankReleaseOutcome::AlreadyAbsent
                    ? PauseResumeBankPhase::Complete
                    : execution.outcome == OnMemoryBankReleaseOutcome::AsyncReleaseRequested
                        ? PauseResumeBankPhase::ReleasePending
                        : PauseResumeBankPhase::Failed;
                release_marker = {
                    g_pause_resume_bank.phase == PauseResumeBankPhase::Complete
                        ? PauseResumeBankMarkerStatus::ReleaseComplete
                        : g_pause_resume_bank.phase == PauseResumeBankPhase::ReleasePending
                            ? PauseResumeBankMarkerStatus::ReleaseRequested
                            : PauseResumeBankMarkerStatus::Failed,
                    g_pause_resume_bank.phase == PauseResumeBankPhase::Complete
                        ? "already_absent" : "release_execution",
                    g_pause_resume_bank.session_epoch,
                    g_pause_resume_bank.cycle_epoch, true};
            }
        }
        if (execution.outcome != OnMemoryBankReleaseOutcome::Failed) {
            observe_bgm_playback_release(release, true);
        }
        observe_bgm_playback_aggregate(true, false);
        log_pause_resume_bank_marker(release_marker);
    }
    (void)try_clear_custom_activation_quarantine(
        CustomActivationQuarantineClearReason::ListReturn);
    return result;
}

bool clear_frozen_profile_if_audio_unowned()
{
    std::lock_guard<std::mutex> lock(g_audio_state_mutex);
    if (g_frozen_profile_lease.active()) return false;
    registry().clear_frozen_profile();
    return true;
}

bool custom_audio_route_idle_for_menu_input() noexcept
{
    if (!g_audio_route_installed.load(std::memory_order_acquire)
        || g_audio_route_disabled.load(std::memory_order_acquire)) {
        return false;
    }
    const auto ownership_before = snapshot_bgm_aggregate_exit_ownership(
        false, false);
    if (!bgm_playback_aggregate_menu_ready(ownership_before,
            g_bgm_aggregate_exit_requested.load(std::memory_order_acquire),
            g_bgm_aggregate_exit_pending.load(std::memory_order_acquire))) {
        return false;
    }
    bool idle = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        idle = g_audio_route_installed.load(std::memory_order_acquire)
            && !g_audio_route_disabled.load(std::memory_order_acquire)
            && g_audio_route_state.phase == AudioRoutePhase::Idle;
    }
    if (!idle) return false;
    const auto ownership_after = snapshot_bgm_aggregate_exit_ownership(
        false, false);
    return bgm_playback_aggregate_menu_ready(ownership_after,
        g_bgm_aggregate_exit_requested.load(std::memory_order_acquire),
        g_bgm_aggregate_exit_pending.load(std::memory_order_acquire));
}

bool custom_audio_playback_clock(double& elapsed_seconds) noexcept
{
    elapsed_seconds = 0.0;
    if (!g_diagnostic_playback_clock_active.load(std::memory_order_acquire)) {
        return false;
    }
    elapsed_seconds = static_cast<double>(
        g_diagnostic_playback_elapsed_us.load(std::memory_order_acquire)) / 1000000.0;
    return std::isfinite(elapsed_seconds) && elapsed_seconds >= 0.0;
}

bool install_audio_sead_hooks(const HookInstallContext& context)
{
    const bool install_started = bgm_playback_protected_install_begin(
        []() noexcept { return aggregate_rollback_ownership_clear(); },
        []() noexcept { audio_production_open_callback_admission(); });
    if (!install_started) {
        core::log(core::LogLevel::Error,
            "[audio_sead] status=install_failed reason=aggregate_rollback_retained mutation_authorized=0");
        return false;
    }
    g_diagnostic_playback_clock_active.store(false, std::memory_order_release);
    g_diagnostic_playback_elapsed_us.store(0, std::memory_order_release);
    g_diagnostic_playback_log_bucket.store(UINT32_MAX, std::memory_order_release);
    g_completion_memory_log_bucket.store(UINT32_MAX, std::memory_order_release);
    g_audio_route_installed.store(false, std::memory_order_release);
    g_exe_module = context.exe_module;
    bind_audio_production_executable(context.exe_module);
    g_audio_route_disabled.store(false, std::memory_order_relaxed);
    // install_release_hooks has already passed exact executable identity and the
    // generated release catalog. This optional callable gets its own exact
    // signature gate and remains unavailable on any mismatch.
    const bool onmemory_lookup_valid = context.exe_module
        && signature_matches(context.exe_module,
            find_rva_signature("sead_onmemory_bank_kind_lookup"));
    g_onmemory_bank_kind_lookup.store(
        onmemory_lookup_valid
            ? reinterpret_cast<uintptr_t>(context.exe_module) + rva::SeadOnMemoryBankKindLookup
            : 0,
        std::memory_order_release);
    g_onmemory_bank_kind_lookup_available.store(
        onmemory_lookup_valid, std::memory_order_release);
    const bool onmemory_release_valid = context.exe_module
        && signature_matches(context.exe_module,
            find_rva_signature("sead_onmemory_bank_release"));
    g_onmemory_bank_release.store(
        onmemory_release_valid
            ? reinterpret_cast<uintptr_t>(context.exe_module)
                + rva::SeadOnMemoryBankRelease
            : 0,
        std::memory_order_release);
    g_onmemory_bank_release_available.store(
        onmemory_release_valid, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_slot_setup_profile = {};
        g_native_play_setup_profile = {};
        g_piano_audio_request_profile = {};
        g_piano_audio_owner = nullptr;
        g_piano_audio_owner_snapshot = {};
        g_piano_audio_owner_snapshot_valid = false;
        g_mabf_mode_observation = {};
        g_onmemory_bank_diagnostic_pair = {};
        g_onmemory_bank_lifecycle.reset();
        g_onmemory_bank_cleanup_only.reset_complete();
        g_pause_resume_bank = {};
    }
    g_controller_rebuild_available.store(
        signature_matches(context.exe_module, find_rva_signature("bgm_slot_set"))
            && signature_matches(context.exe_module, find_rva_signature("bgm_prepare")),
        std::memory_order_release);
    g_controller_lookup_available.store(
        signature_matches(context.exe_module, find_rva_signature("bgm_controller_lookup")),
        std::memory_order_release);
    g_slot_setup_available.store(
        signature_matches(context.exe_module, find_rva_signature("bgm_slot_setup")),
        std::memory_order_release);
    g_piano_audio_request_available.store(
        signature_matches(context.exe_module, find_rva_signature("piano_audio_request")),
        std::memory_order_release);
    size_t ready = 0;
    size_t missing_or_invalid = 0;
    const RegistrySnapshot sidecar_catalog = registry().registry_snapshot();
    audio_sead_detail::ProgressiveAudioCatalogBuilder sidecars;
    for (const SongDescriptor& song : sidecar_catalog.songs()) {
        if (sidecars.append(song)) {
            ++ready;
        } else {
            ++missing_or_invalid;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        g_sidecar_catalog = sidecar_catalog;
        g_sidecar_prefix = sidecars.snapshot();
    }

    std::ostringstream out;
    out << "[audio_sead] status=prepared_playsetup_route"
        << " custom_songs=" << sidecar_catalog.songs().size()
        << " sidecars_ready=" << ready
        << " sidecars_missing_or_invalid=" << missing_or_invalid
        << std::dec
        << " live_hooks=" << (kEnableLiveSqexSeadDetours ? "playsetup_scoped_restore" : "disabled");
    core::log(core::LogLevel::Info, out.str());
    log_pending_audio_hooks(context.exe_module, ready);
    if (kEnableLiveSqexSeadDetours) {
        {
            std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
            g_bgm_playback_borrowers = {};
            g_bgm_canonical_substrate_proof = {};
            g_bgm_canonical_substrate_transaction_generation = 0;
            g_bgm_playback_borrower_ordinal = 0;
            g_bgm_playback_collection_version = 0;
            g_bgm_playback_capacity_overflow = false;
            g_bgm_playback_capacity_failure_epoch = 0;
            g_bgm_playback_observation_epoch.store(0, std::memory_order_release);
            g_bgm_playback_observation_logs.store(0, std::memory_order_release);
            g_bgm_playback_set_logs.store(0, std::memory_order_release);
            g_bgm_playback_play_logs.store(0, std::memory_order_release);
            g_bgm_playback_blocked_logs.store(0, std::memory_order_release);
            g_bgm_playback_boundary_logs.store(0, std::memory_order_release);
            g_bgm_aggregate_mutation_logs.store(0, std::memory_order_release);
            g_bgm_aggregate_exit_logs.store(0, std::memory_order_release);
            g_bgm_aggregate_exit_pending.store(false, std::memory_order_release);
            g_bgm_aggregate_exit_requested.store(false, std::memory_order_release);
            g_bgm_aggregate_unresolved_publications.store(
                0, std::memory_order_release);
        }
        {
            std::lock_guard<std::mutex> lock(
                g_bgm_aggregate_exit_diagnostic_mutex);
            g_bgm_aggregate_exit_diagnostic_keys = {};
        }
        {
            std::lock_guard<std::mutex> lock(g_bgm_aggregate_release_mutex);
            g_bgm_aggregate_release_claim.action = {};
            g_bgm_aggregate_release_claim.state =
                AggregateReleaseClaimState::Complete;
            g_bgm_aggregate_release_claim.generation = 0;
            g_bgm_aggregate_release_claim.claim_id = 0;
            g_bgm_aggregate_release_generation = 0;
            g_bgm_aggregate_release_claim_id = 0;
        }
        g_bgm_aggregate_mutation_gate.reopen();
        std::vector<core::RawRvaHook*> mandatory_hooks;
        auto install_mandatory = [&](core::RawRvaHook& candidate,
                                     std::function<bool()> install) {
            std::vector<AudioProductionInstallStep> steps;
            steps.reserve(mandatory_hooks.size() + 1);
            for (auto* hook : mandatory_hooks) {
                steps.push_back({[] { return true; },
                    [hook] { return hook->disable(); },
                    [hook] { return hook->remove(); }});
            }
            steps.push_back({std::move(install),
                [&candidate] { return candidate.disable(); },
                [&candidate] { return candidate.remove(); }});
            const auto result = execute_audio_production_install(steps);
            if (result.status != AudioProductionInstallStatus::Installed) {
                mandatory_hooks.clear();
                return false;
            }
            mandatory_hooks.push_back(&candidate);
            return true;
        };
        const HookSpec* stop_spec = find_hook_spec("bgm_slot_stop");
        if (!stop_spec) {
            core::log(core::LogLevel::Error, "[audio_sead] status=install_failed hook=bgm_slot_stop error=missing_hook_spec");
            return false;
        }
        std::string error;
        if (const HookSpec* transfer_spec = find_hook_spec(
                "bgm_playback_transfer")) {
            if (g_bgm_playback_transfer_hook.install(
                    context.exe_module, transfer_spec->rva,
                    transfer_spec->expected_prologue,
                    reinterpret_cast<void*>(&bgm_playback_transfer_detour),
                    reinterpret_cast<void**>(&g_original_bgm_playback_transfer),
                    error)) {
                core::log(core::LogLevel::Info,
                    "[audio_sead] status=live_hook_installed hook=bgm_playback_transfer mode=aggregate_observer mutation_authorized=0");
            } else {
                core::log(core::LogLevel::Info,
                    "[audio_sead] status=optional_hook_disabled hook=bgm_playback_transfer error=" + error);
            }
        } else {
            core::log(core::LogLevel::Info,
                "[audio_sead] status=optional_hook_disabled hook=bgm_playback_transfer error=missing_hook_spec");
        }
        error.clear();
        if (!install_mandatory(g_bgm_slot_stop_hook, [&] {
                return g_bgm_slot_stop_hook.install(context.exe_module,
                    stop_spec->rva, stop_spec->expected_prologue,
                    reinterpret_cast<void*>(&bgm_slot_stop_detour),
                    reinterpret_cast<void**>(&g_original_bgm_slot_stop), error);
            })) {
            std::ostringstream stop_out;
            stop_out << "[audio_sead] status=install_failed hook=bgm_slot_stop error=" << error;
            core::log(core::LogLevel::Error, stop_out.str());
            shutdown_audio_sead();
            return false;
        }
        std::ostringstream stop_out;
        stop_out << "[audio_sead] status=live_hook_installed hook=bgm_slot_stop rva=0x" << std::hex << stop_spec->rva
            << std::dec << " mode=lifetime_guard sidecars_ready=" << ready;
        core::log(core::LogLevel::Info, stop_out.str());

        if (const HookSpec* pause_spec = find_hook_spec("bgm_manager_pause")) {
            error.clear();
            if (g_bgm_manager_pause_hook.install(context.exe_module, pause_spec->rva, pause_spec->expected_prologue,
                    reinterpret_cast<void*>(&bgm_manager_pause_detour), reinterpret_cast<void**>(&g_original_bgm_manager_pause), error)) {
                std::ostringstream pause_out;
                pause_out << "[audio_sead] status=live_hook_installed hook=bgm_manager_pause rva=0x" << std::hex << pause_spec->rva
                    << std::dec << " mode=lifecycle_observer sidecars_ready=" << ready;
                core::log(core::LogLevel::Info, pause_out.str());
            } else {
                std::ostringstream pause_out;
                pause_out << "[audio_sead] status=optional_hook_disabled hook=bgm_manager_pause error=" << error;
                core::log(core::LogLevel::Info, pause_out.str());
            }
        } else {
            std::ostringstream pause_out;
            pause_out << "[audio_sead] status=optional_hook_disabled hook=bgm_manager_pause error=missing_hook_spec";
            core::log(core::LogLevel::Info, pause_out.str());
        }

        if (const HookSpec* transition_spec = find_hook_spec("bgm_slot_pause_transition")) {
            error.clear();
            if (g_bgm_slot_pause_transition_hook.install(context.exe_module, transition_spec->rva, transition_spec->expected_prologue,
                    reinterpret_cast<void*>(&bgm_slot_pause_transition_detour), reinterpret_cast<void**>(&g_original_bgm_slot_pause_transition), error)) {
                std::ostringstream transition_out;
                transition_out << "[audio_sead] status=live_hook_installed hook=bgm_slot_pause_transition rva=0x" << std::hex << transition_spec->rva
                    << std::dec << " mode=lifecycle_observer sidecars_ready=" << ready;
                core::log(core::LogLevel::Info, transition_out.str());
            } else {
                core::log(core::LogLevel::Info, "[audio_sead] status=optional_hook_disabled hook=bgm_slot_pause_transition error=" + error);
            }
        }

        if (const HookSpec* transition_spec = find_hook_spec("bgm_slot_resume_transition")) {
            error.clear();
            if (g_bgm_slot_resume_transition_hook.install(context.exe_module, transition_spec->rva, transition_spec->expected_prologue,
                    reinterpret_cast<void*>(&bgm_slot_resume_transition_detour), reinterpret_cast<void**>(&g_original_bgm_slot_resume_transition), error)) {
                std::ostringstream transition_out;
                transition_out << "[audio_sead] status=live_hook_installed hook=bgm_slot_resume_transition rva=0x" << std::hex << transition_spec->rva
                    << std::dec << " mode=lifecycle_observer sidecars_ready=" << ready;
                core::log(core::LogLevel::Info, transition_out.str());
            } else {
                core::log(core::LogLevel::Info, "[audio_sead] status=optional_hook_disabled hook=bgm_slot_resume_transition error=" + error);
            }
        }

        if (const HookSpec* owner_spec = find_hook_spec("piano_audio_state_tick")) {
            error.clear();
            if (g_piano_audio_state_tick_hook.install(context.exe_module, owner_spec->rva, owner_spec->expected_prologue,
                    reinterpret_cast<void*>(&piano_audio_state_tick_detour), reinterpret_cast<void**>(&g_original_piano_audio_state_tick), error)) {
                std::ostringstream owner_out;
                owner_out << "[audio_sead] status=live_hook_installed hook=piano_audio_state_tick rva=0x" << std::hex << owner_spec->rva
                    << std::dec << " mode=lifecycle_observer sidecars_ready=" << ready;
                core::log(core::LogLevel::Info, owner_out.str());
            } else {
                core::log(core::LogLevel::Info, "[audio_sead] status=optional_hook_disabled hook=piano_audio_state_tick error=" + error);
            }
        }

        if (const HookSpec* adaptive_spec = find_hook_spec("piano_adaptive_judgment")) {
            error.clear();
            if (g_piano_adaptive_judgment_hook.install(context.exe_module, adaptive_spec->rva,
                    adaptive_spec->expected_prologue,
                    reinterpret_cast<void*>(&piano_adaptive_judgment_detour),
                    reinterpret_cast<void**>(&g_original_piano_adaptive_judgment), error)) {
                std::ostringstream adaptive_out;
                adaptive_out << "[audio_sead] status=live_hook_installed hook=piano_adaptive_judgment rva=0x"
                    << std::hex << adaptive_spec->rva << std::dec << " mode=read_only_observer";
                core::log(core::LogLevel::Info, adaptive_out.str());
            } else {
                core::log(core::LogLevel::Info,
                    "[audio_sead] status=optional_hook_disabled hook=piano_adaptive_judgment error=" + error);
            }
        }

        const HookSpec* set_spec = find_hook_spec("bgm_slot_set");
        if (!set_spec) {
            core::log(core::LogLevel::Error, "[audio_sead] status=install_failed hook=bgm_slot_set error=missing_hook_spec");
            shutdown_audio_sead();
            return false;
        }
        error.clear();
        if (!install_mandatory(g_bgm_slot_set_hook, [&] {
                return g_bgm_slot_set_hook.install(context.exe_module,
                    set_spec->rva, set_spec->expected_prologue,
                    reinterpret_cast<void*>(&bgm_slot_set_detour),
                    reinterpret_cast<void**>(&g_original_bgm_slot_set), error);
            })) {
            std::ostringstream set_out;
            set_out << "[audio_sead] status=install_failed hook=bgm_slot_set error=" << error;
            core::log(core::LogLevel::Error, set_out.str());
            shutdown_audio_sead();
            return false;
        }
        std::ostringstream set_out;
        set_out << "[audio_sead] status=live_hook_installed hook=bgm_slot_set rva=0x" << std::hex << set_spec->rva
            << std::dec << " mode=lifecycle_observer sidecars_ready=" << ready;
        core::log(core::LogLevel::Info, set_out.str());

        const HookSpec* play_spec = find_hook_spec("bgm_slot_play");
        if (!play_spec) {
            core::log(core::LogLevel::Error, "[audio_sead] status=install_failed hook=bgm_slot_play error=missing_hook_spec");
            shutdown_audio_sead();
            return false;
        }
        error.clear();
        if (!install_mandatory(g_bgm_slot_play_hook, [&] {
                return g_bgm_slot_play_hook.install(context.exe_module,
                    play_spec->rva, play_spec->expected_prologue,
                    reinterpret_cast<void*>(&bgm_slot_play_detour),
                    reinterpret_cast<void**>(&g_original_bgm_slot_play), error);
            })) {
            std::ostringstream play_out;
            play_out << "[audio_sead] status=install_failed hook=bgm_slot_play error=" << error;
            core::log(core::LogLevel::Error, play_out.str());
            shutdown_audio_sead();
            return false;
        }
        std::ostringstream play_out;
        play_out << "[audio_sead] status=live_hook_installed hook=bgm_slot_play rva=0x" << std::hex << play_spec->rva
            << std::dec << " sidecars_ready=" << ready;
        core::log(core::LogLevel::Info, play_out.str());

        const HookSpec* spec = find_hook_spec("sqexsead_play_setup");
        if (!spec) {
            core::log(core::LogLevel::Error, "[audio_sead] status=install_failed hook=sqexsead_play_setup error=missing_hook_spec");
            shutdown_audio_sead();
            return false;
        }
        error.clear();
        if (!install_mandatory(g_play_setup_hook, [&] {
                return g_play_setup_hook.install(context.exe_module,
                    spec->rva, spec->expected_prologue,
                    reinterpret_cast<void*>(&play_setup_detour),
                    reinterpret_cast<void**>(&g_original_play_setup), error);
            })) {
            std::ostringstream hook_out;
            hook_out << "[audio_sead] status=install_failed hook=sqexsead_play_setup error=" << error;
            core::log(core::LogLevel::Error, hook_out.str());
            shutdown_audio_sead();
            return false;
        }
        std::ostringstream hook_out;
        hook_out << "[audio_sead] status=live_hook_installed hook=sqexsead_play_setup rva=0x" << std::hex << spec->rva
            << std::dec << " restore=scoped_to_original sidecars_ready=" << ready;
        core::log(core::LogLevel::Info, hook_out.str());
    }
    g_audio_route_installed.store(true, std::memory_order_release);
    return true;
}

AudioRouteShutdownResult shutdown_audio_sead_with_result()
{
    AudioRouteCleanupResult route_release{
        AudioRouteCleanupStatus::Retained, {}, false, false};
    bool hooks_disabled = false;
    bool callbacks_drained = false;
    AudioProductionShutdownPlan plan;
    plan.cleanup_ready = [] {
        observe_cleanup_only_onmemory_bank_noexcept();
        (void)try_clear_custom_activation_quarantine(
            CustomActivationQuarantineClearReason::Shutdown);
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        return !g_onmemory_bank_cleanup_only.blocks_custom_routes()
            && !g_custom_activation_quarantine;
    };
    plan.disable_hooks = [&] {
        revoke_selection_audio_activation();
        observe_bgm_playback_aggregate(false, true);
        g_audio_route_installed.store(false, std::memory_order_release);
        g_diagnostic_playback_clock_active.store(false, std::memory_order_release);
        g_diagnostic_playback_elapsed_us.store(0, std::memory_order_release);
        g_diagnostic_playback_log_bucket.store(UINT32_MAX, std::memory_order_release);
        g_completion_memory_log_bucket.store(UINT32_MAX, std::memory_order_release);
        g_audio_route_disabled.store(true, std::memory_order_release);
        hooks_disabled = true;
        hooks_disabled = g_play_setup_hook.disable() && hooks_disabled;
        hooks_disabled = g_bgm_slot_play_hook.disable() && hooks_disabled;
        hooks_disabled = g_bgm_slot_set_hook.disable() && hooks_disabled;
        hooks_disabled = g_piano_audio_state_tick_hook.disable() && hooks_disabled;
        hooks_disabled = g_piano_adaptive_judgment_hook.disable() && hooks_disabled;
        hooks_disabled = g_bgm_slot_resume_transition_hook.disable() && hooks_disabled;
        hooks_disabled = g_bgm_slot_pause_transition_hook.disable() && hooks_disabled;
        hooks_disabled = g_bgm_manager_pause_hook.disable() && hooks_disabled;
        hooks_disabled = g_bgm_playback_transfer_hook.disable() && hooks_disabled;
        hooks_disabled = g_bgm_slot_stop_hook.disable() && hooks_disabled;
        hooks_disabled = g_bgm_prepare_hook.disable() && hooks_disabled;
        return hooks_disabled;
    };
    plan.aggregate_rollback = [&] {
        callbacks_drained = true;
        return process_aggregate_rollbacks_after_drain();
    };
    plan.restore_and_release = [&] {
    PauseResumeBankMarker pause_shutdown_marker;
    {
        std::lock_guard<std::recursive_mutex> operation_lock(
            g_audio_route_operations.mutex());
        bool restore = false;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            restore = g_pause_resume_bank.phase == PauseResumeBankPhase::OwnerRebound;
        }
        const bool restored = !restore
            || restore_pause_resume_owner(&pause_shutdown_marker);
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        if (g_pause_resume_bank.phase != PauseResumeBankPhase::Idle
            && g_pause_resume_bank.phase != PauseResumeBankPhase::Complete) {
            g_pause_resume_bank.phase = PauseResumeBankPhase::Failed;
            g_pause_resume_bank.pending_release_probe = {};
            g_pause_resume_bank.pending_release_evidence = {};
            g_pause_resume_bank.retirement_monitor = {};
            g_pause_resume_bank.retired_backing = {};
            if (restored && !pause_shutdown_marker.eligible) {
                pause_shutdown_marker = {PauseResumeBankMarkerStatus::Failed,
                    "shutdown", g_pause_resume_bank.session_epoch,
                    g_pause_resume_bank.cycle_epoch, true};
            }
        }
    }
    log_pause_resume_bank_marker(pause_shutdown_marker);

    AudioShutdownScope shutdown_scope;
    const bool controller_restored = g_controller_patch_rollback.rollback("shutdown", true);
    const bool sound_restored = g_sound_patch_rollback.rollback("shutdown", true);
    route_release = release_audio_route_on_piano_list_return_impl(
        AudioCleanupOperation::Shutdown, hooks_disabled, callbacks_drained,
        controller_restored && sound_restored);
    observe_bgm_playback_aggregate(false, true);

    if (!controller_restored || !sound_restored || !route_release.released()) {
        return false;
    }
    return true;
    };
    plan.publish_clear = [&] {
    g_controller_patch_rollback.commit_verified_rollback();
    g_sound_patch_rollback.commit_verified_rollback();
    {
        std::lock_guard<std::mutex> lock(g_audio_state_mutex);
        AudioRouteTransitionRecorder route_transition_record(
            AudioRouteTransitionReason::ShutdownReset,
            AudioRouteTransitionKind::RouteReset);
        g_sidecar_prefix.reset();
        g_sidecar_catalog = {};
        g_audio_route_state = {};
        g_slot_setup_profile = {};
        g_native_play_setup_profile = {};
        g_piano_audio_request_profile = {};
        g_piano_audio_owner = nullptr;
        g_piano_audio_owner_snapshot = {};
        g_piano_audio_owner_snapshot_valid = false;
        g_mabf_mode_observation = {};
        g_onmemory_bank_diagnostic_pair = {};
        g_onmemory_bank_lifecycle.reset();
        g_pause_resume_bank = {};
    }
    g_alias_music.store(0, std::memory_order_relaxed);
    g_controller_rebuild_available.store(false, std::memory_order_release);
    g_controller_lookup_available.store(false, std::memory_order_release);
    g_slot_setup_available.store(false, std::memory_order_release);
    g_piano_audio_request_available.store(false, std::memory_order_release);
    g_onmemory_bank_kind_lookup_available.store(false, std::memory_order_release);
    g_onmemory_bank_kind_lookup.store(0, std::memory_order_release);
    g_onmemory_bank_release_available.store(false, std::memory_order_release);
    g_onmemory_bank_release.store(0, std::memory_order_release);
    g_exe_module = nullptr;
    clear_audio_production_native_bindings();
    {
        std::lock_guard<std::mutex> lock(g_bgm_playback_borrower_mutex);
        g_bgm_playback_borrowers = {};
        g_bgm_canonical_substrate_proof = {};
        g_bgm_canonical_substrate_transaction_generation = 0;
        g_bgm_playback_collection_version = 0;
        g_bgm_playback_capacity_overflow = false;
        g_bgm_playback_capacity_failure_epoch = 0;
    }
    };
    const auto execution = execute_audio_production_shutdown(plan);
    if (!execution.succeeded()) {
        AudioRouteLeaseIdentity lease;
        {
            std::lock_guard<std::mutex> lock(g_audio_state_mutex);
            lease = g_audio_route_state.lease_identity;
            if (execution.failed_phase != AudioProductionShutdownPhase::CallbackOwned
                && execution.failed_phase != AudioProductionShutdownPhase::CleanupReadiness) {
                (void)g_frozen_profile_lease.transition(
                    AudioRouteCleanupEvent::ShutdownUnverified, lease);
            }
        }
        const bool quiesce = execution.failed_phase
                == AudioProductionShutdownPhase::DisableHooks
            || execution.failed_phase == AudioProductionShutdownPhase::CallbackDrain;
        std::ostringstream out;
        out << "[audio_sead] shutdown status="
            << (quiesce ? "quiesce_failed" : "retained")
            << " phase=" << static_cast<unsigned>(execution.failed_phase)
            << " cleanup=retained admission_reopened="
            << (execution.admission_reopened ? "true" : "false");
        core::log(core::LogLevel::Error, out.str());
        if (execution.failed_phase != AudioProductionShutdownPhase::CallbackOwned) {
            route_release.identity = lease;
        }
        return {quiesce ? AudioRouteShutdownStatus::QuiesceFailed
                        : AudioRouteShutdownStatus::Retained, route_release};
    }
    core::log(core::LogLevel::Debug, "[audio_sead] shutdown status=ok hooks=disabled_retained");
    return {AudioRouteShutdownStatus::Succeeded, route_release};
}

bool shutdown_audio_sead()
{
    return shutdown_audio_sead_with_result().succeeded();
}

} // namespace ff7r::piano::game
