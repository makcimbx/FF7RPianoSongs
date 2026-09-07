#pragma once

#include "game/song_registry.h"
#include "game/uobject_identity.h"

#include <memory>

#include "game/profile_list_coordinator.h"

#include "core/hooks.h"

#include <windows.h>

namespace ff7r::piano::game {

struct HookInstallContext {
    HMODULE exe_module = nullptr;
};

core::HookCallbackGate& non_audio_hook_gate();

bool install_list_patch_hooks(const HookInstallContext& context);
bool install_menu_session_hooks(const HookInstallContext& context);
bool install_selection_hooks(const HookInstallContext& context);
bool install_scoreinfo_overlay_hooks(const HookInstallContext& context);
bool install_title_hooks(const HookInstallContext& context);
bool install_chart_patch_hooks(const HookInstallContext& context);
bool install_note_count_hooks(const HookInstallContext& context);
bool install_duration_hooks(const HookInstallContext& context);
bool install_progress_hooks(const HookInstallContext& context);
bool install_audio_sead_hooks(const HookInstallContext& context);
bool install_piano_page_selection_guard(const HookInstallContext& context);
bool try_selection_runtime_idle_for_catalog_adoption() noexcept;
class PreparedPianoListCatalog;
struct MenuSessionSnapshot;
enum class PianoListCatalogCommitResult {
    Committed,
    Rejected,
    RollbackUnverified,
};
enum class ExactCloseListRestoreResult {
    Restored,
    VerifiedOwnerless,
    NoOwnedPatch,
    SessionIdentityMismatch,
    LiveIdentityInvalid,
    CatalogIdentityMismatch,
    TupleUnreadable,
    TupleDrift,
    TransitionFailed,
    RollbackUnverified,
};
constexpr bool exact_close_list_restore_is_terminal(
    const ExactCloseListRestoreResult result) noexcept
{
    return result != ExactCloseListRestoreResult::Restored
        && result != ExactCloseListRestoreResult::VerifiedOwnerless;
}
enum class PianoListOwnerState {
    None,
    Managed,
    Transient,
    Uncertain,
};
enum class PianoListRepublishState {
    None,
    Pending,
    Transient,
};
std::shared_ptr<PreparedPianoListCatalog> prepare_piano_list_catalog(
    void* widget, const UObjectLiveHandle& widget_identity,
    const RegistrySnapshot& expected,
    std::shared_ptr<const SongRegistryStorage> replacement) noexcept;
std::shared_ptr<PreparedPianoListCatalog> prepare_piano_list_catalog_republish(
    void* widget, const UObjectLiveHandle& widget_identity) noexcept;
// The row the first custom song must occupy: the live count for an unowned
// native list, or the preserved original native count for a managed owner.
bool piano_list_first_custom_row(void* widget,
    const UObjectLiveHandle& widget_identity, int32_t& first_custom_row) noexcept;
PianoListCatalogCommitResult commit_prepared_piano_list_catalog(
    PreparedPianoListCatalog& prepared) noexcept;
void finalize_prepared_piano_list_catalog(
    PreparedPianoListCatalog& prepared) noexcept;
bool piano_list_catalog_terminal_failure() noexcept;
// False after any focus mutation failure: the caller must not publish Ready.
bool restore_last_played_menu_focus(const MenuSessionSnapshot& opening,
    void (*restore_selection)(void*)) noexcept;
PianoListRepublishState piano_list_catalog_republish_state() noexcept;
PianoListOwnerState piano_list_catalog_owner_state(
    void* widget, const UObjectLiveHandle& widget_identity) noexcept;
ExactCloseListRestoreResult restore_owned_list_after_exact_cancel_close(
    const MenuSessionSnapshot& closing, void* post_close_widget,
    const UObjectLiveHandle& post_close_identity) noexcept;
ProfileListCallbacks make_profile_list_callbacks();

core::HookShutdownResult shutdown_list_patch();
core::HookShutdownResult shutdown_menu_session();
core::HookShutdownResult shutdown_selection();
core::HookShutdownResult shutdown_scoreinfo_overlay();
core::HookShutdownResult shutdown_title();
core::HookShutdownResult shutdown_chart_patch();
core::HookShutdownResult shutdown_note_count();
core::HookShutdownResult shutdown_duration();
core::HookShutdownResult shutdown_progress();
bool shutdown_audio_sead();

} // namespace ff7r::piano::game
