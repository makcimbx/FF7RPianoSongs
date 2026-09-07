#pragma once

#include "game/menu_ui_refresh.h"
#include "game/menu_session_authority.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace ff7r::piano::game {

enum class ProfileListCoordinatorState {
    Ready,
    ListEntering,
    ListSetup,
    ListWaitingReadiness,
    ProfileChanging,
    RefreshPosted,
    Refreshing,
    Failed,
    Shutdown,
};

enum class ProfileEdgeResult {
    Changed,
    Deferred,
    Rejected,
    Failed,
};

struct ProfileListCallbacks {
    std::function<SelectionSnapshot()> capture_selection;
    std::function<MenuSessionSnapshot(bool allow_opening)> capture_session;
    std::function<bool(uint64_t generation, bool require_ready)> validate_session;
    std::function<bool(const SelectionSnapshot&, MenuUiRefreshSnapshot&)> capture_ui;
    std::function<bool()> audio_idle;
    std::function<bool(const SelectionSnapshot&, uint64_t)> revoke_audio_activation;
    std::function<bool(const SelectionSnapshot&, int, SelectionSnapshot&)> cycle_profile;
    std::function<void(const SelectionSnapshot&)> log_profile_change;
    std::function<bool()> observe_scoreinfo_overlay;
    std::function<bool(std::uint64_t)> post_refresh;
    std::function<bool(const SelectionSnapshot&, const ListItemUiRefreshTarget&)> refresh_list;
    std::function<bool(const SelectionSnapshot&, const SelectionUiRefreshTarget&)> refresh_selection;
    std::function<bool(const SelectionSnapshot&, const MenuUiRefreshSnapshot&, SelectionSnapshot&)> revalidate;
};

struct ListSetupIdentity final {
    void* context = nullptr;
    void* widget = nullptr;
    int visible_index = -1;
    int base_slot = -1;
    SelectionSnapshot setup_selection;
    UObjectLiveHandle context_identity;
    UObjectLiveHandle widget_identity;
    uint64_t session_generation = 0;
    bool active_custom = false;

    explicit operator bool() const noexcept;
};

struct ListSetupView final {
    void* ingress_context = nullptr;
    void* ingress_widget = nullptr;
    int ingress_index = -1;
    ListSetupIdentity identity;
    std::shared_ptr<void> callback_authority;
};

struct ListReturnCallbacks {
    void* ingress_context = nullptr;
    void* ingress_widget = nullptr;
    int ingress_index = -1;
    std::function<void()> retire_scoreinfo;
    std::function<bool(ListSetupView&)> prepare;
    std::function<void(const ListSetupView&)> original_setup;
    std::function<bool(const ListSetupView&)> cleanup_audio;
    std::function<void(const ListSetupView&)> update_rank;
};

class ProfileListCoordinator final {
public:
    ProfileEdgeResult submit_edge(int delta, ProfileListCallbacks& callbacks);
    bool handle_refresh(std::uint64_t ticket, ProfileListCallbacks& callbacks);
    bool run_list_return(ListReturnCallbacks& list, ProfileListCallbacks& profile);
    ProfileEdgeResult notify_session_ready(
        uint64_t generation, ProfileListCallbacks& callbacks) noexcept;
    void begin_session(uint64_t generation) noexcept;
    // An Opening restore must not retarget input already bound to another row.
    bool reconcile_open_focus(uint64_t generation, const SelectionSnapshot& target) noexcept;
    void retire_session(uint64_t generation) noexcept;
    void shutdown() noexcept;
    ProfileListCoordinatorState state() const noexcept;
    bool try_ready_for_catalog_adoption() const noexcept;
    std::uint64_t pending_ticket() const noexcept;
#ifdef FF7RP_COORDINATOR_SELFTEST
    void selftest_seed_ticket(std::uint64_t epoch, std::uint32_t sequence) noexcept;
#endif

private:
    struct Intent {
        int delta = 0;
        SelectionSnapshot expected;
        MenuUiRefreshSnapshot ui;
        uint64_t session_generation = 0;
        ListSetupIdentity bound_identity;
    };

    ProfileEdgeResult execute(Intent intent, ProfileListCallbacks& callbacks,
        bool audio_readiness_prevalidated = false);
    ProfileEdgeResult drain_deferred(ProfileListCallbacks& callbacks);
    static bool same_list_identity(
        const ListSetupIdentity& left, const ListSetupIdentity& right) noexcept;
    static bool ui_matches_list_identity(
        const MenuUiRefreshSnapshot& ui, const ListSetupIdentity& identity) noexcept;

    mutable std::mutex mutex_;
    ProfileListCoordinatorState state_ = ProfileListCoordinatorState::Ready;
    ListSetupIdentity list_identity_;
    Intent deferred_;
    Intent refresh_;
    std::uint64_t epoch_ = 1;
    std::uint64_t sequence_ = 0;
    std::uint64_t ticket_ = 0;
    uint64_t session_generation_ = 0;
    uint64_t ready_notification_consumed_ = 0;
    bool notification_eligible_ = false;
};

ProfileListCoordinator& profile_list_coordinator();

} // namespace ff7r::piano::game
