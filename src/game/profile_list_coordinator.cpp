#include "game/profile_list_coordinator.h"

#include "game/song_registry.h"

#include <utility>

namespace ff7r::piano::game {
namespace {
ProfileListCoordinator g_coordinator;
}

ListSetupIdentity::operator bool() const noexcept
{
    return session_generation && context && widget && visible_index >= 0 && base_slot >= 0
        && setup_selection.storage;
}

bool ProfileListCoordinator::same_list_identity(
    const ListSetupIdentity& left, const ListSetupIdentity& right) noexcept
{
    return left.session_generation == right.session_generation
        && left.context == right.context && left.widget == right.widget
        && left.visible_index == right.visible_index
        && left.base_slot == right.base_slot
        && left.setup_selection.storage == right.setup_selection.storage
        && left.setup_selection.song == right.setup_selection.song
        && left.setup_selection.profile == right.setup_selection.profile
        && left.setup_selection.profile_index == right.setup_selection.profile_index
        && left.setup_selection.visible_index == right.setup_selection.visible_index
        && left.setup_selection.base_slot == right.setup_selection.base_slot
        && left.context_identity.internal_index == right.context_identity.internal_index
        && left.context_identity.serial_number == right.context_identity.serial_number
        && left.widget_identity.internal_index == right.widget_identity.internal_index
        && left.widget_identity.serial_number == right.widget_identity.serial_number;
}

bool ProfileListCoordinator::ui_matches_list_identity(
    const MenuUiRefreshSnapshot& ui, const ListSetupIdentity& identity) noexcept
{
    return ui.list_target.context == identity.context
        && ui.list_target.widget == identity.widget
        && ui.list_target.visible_index == identity.visible_index
        && ui.list_target.context_identity.internal_index
            == identity.context_identity.internal_index
        && ui.list_target.context_identity.serial_number
            == identity.context_identity.serial_number
        && ui.list_target.widget_identity.internal_index
            == identity.widget_identity.internal_index
        && ui.list_target.widget_identity.serial_number
            == identity.widget_identity.serial_number;
}

ProfileEdgeResult ProfileListCoordinator::submit_edge(
    const int delta, ProfileListCallbacks& callbacks)
{
    if (delta == 0) return ProfileEdgeResult::Rejected;
    const int direction = delta < 0 ? -1 : 1;
    {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ListEntering
            || state_ == ProfileListCoordinatorState::ListSetup
            || state_ == ProfileListCoordinatorState::ListWaitingReadiness) {
            if (deferred_.delta != 0) return ProfileEdgeResult::Rejected;
            deferred_.delta = direction;
            return ProfileEdgeResult::Deferred;
        }
        if (state_ != ProfileListCoordinatorState::Ready)
            return ProfileEdgeResult::Rejected;
        state_ = ProfileListCoordinatorState::ProfileChanging;
    }
    if (!callbacks.capture_selection || !callbacks.capture_ui || !callbacks.capture_session) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ProfileChanging)
            state_ = ProfileListCoordinatorState::Ready;
        return ProfileEdgeResult::Rejected;
    }
    MenuSessionSnapshot session;
    SelectionSnapshot expected;
    try { session = callbacks.capture_session(false); expected = callbacks.capture_selection(); }
    catch (...) {}
    MenuUiRefreshSnapshot ui;
    bool ui_captured = false;
    try { ui_captured = expected && callbacks.capture_ui(expected, ui); } catch (...) {}
    if (!session || !expected || !ui_captured) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ProfileChanging)
            state_ = ProfileListCoordinatorState::Ready;
        return ProfileEdgeResult::Rejected;
    }
    {
        std::lock_guard lock(mutex_);
        if (state_ != ProfileListCoordinatorState::ProfileChanging) return ProfileEdgeResult::Rejected;
        if (!session_generation_) session_generation_ = session.generation;
        if (session_generation_ != session.generation) {
            state_ = ProfileListCoordinatorState::Ready;
            return ProfileEdgeResult::Rejected;
        }
    }
    Intent intent{direction, expected, std::move(ui), session.generation};
    return execute(std::move(intent), callbacks);
}

ProfileEdgeResult ProfileListCoordinator::drain_deferred(
    ProfileListCallbacks& callbacks)
{
    Intent intent;
    ListSetupIdentity identity;
    {
        std::lock_guard lock(mutex_);
        if (state_ != ProfileListCoordinatorState::ListSetup
            || deferred_.delta == 0 || !list_identity_)
            return ProfileEdgeResult::Rejected;
        intent = deferred_;
        identity = list_identity_;
    }

    if (!callbacks.capture_selection || !callbacks.capture_ui || !callbacks.capture_session) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ListSetup)
            state_ = ProfileListCoordinatorState::ListWaitingReadiness;
        return ProfileEdgeResult::Deferred;
    }
    MenuSessionSnapshot session;
    SelectionSnapshot current;
    try { session = callbacks.capture_session(false); current = callbacks.capture_selection(); }
    catch (...) {}
    MenuUiRefreshSnapshot ui;
    bool ui_captured = false;
    try { ui_captured = current && callbacks.capture_ui(current, ui); } catch (...) {}
    bool session_exact = false;
    try { session_exact = callbacks.validate_session
        && callbacks.validate_session(identity.session_generation, true); } catch (...) {}
    if (!session || session.generation != identity.session_generation || !session_exact
        || !current || !selection_semantically_matches(identity.setup_selection, current)
        || !ui_captured || !ui_matches_list_identity(ui, identity)) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ListSetup)
            state_ = ProfileListCoordinatorState::ListWaitingReadiness;
        return ProfileEdgeResult::Deferred;
    }
    bool audio_idle = false;
    try { audio_idle = callbacks.audio_idle && callbacks.audio_idle(); } catch (...) {}
    if (!audio_idle) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ListSetup)
            state_ = ProfileListCoordinatorState::ListWaitingReadiness;
        return ProfileEdgeResult::Deferred;
    }
    intent.expected = std::move(current);
    intent.ui = std::move(ui);
    intent.session_generation = session.generation;
    {
        std::lock_guard lock(mutex_);
        if (state_ != ProfileListCoordinatorState::ListSetup
            || !same_list_identity(identity, list_identity_)
            || !same_list_identity(identity, deferred_.bound_identity)
            || deferred_.delta != intent.delta)
            return ProfileEdgeResult::Rejected;
        deferred_ = {};
        list_identity_ = {};
        notification_eligible_ = false;
        state_ = ProfileListCoordinatorState::ProfileChanging;
    }
    return execute(std::move(intent), callbacks, true);
}

ProfileEdgeResult ProfileListCoordinator::notify_session_ready(
    const uint64_t generation, ProfileListCallbacks& callbacks) noexcept
{
    try {
        {
            std::lock_guard lock(mutex_);
            if (!generation || ready_notification_consumed_ == generation
                || state_ != ProfileListCoordinatorState::ListWaitingReadiness
                || session_generation_ != generation
                || deferred_.delta == 0
                || deferred_.session_generation != generation
                || deferred_.bound_identity.session_generation != generation
                || !list_identity_
                || !same_list_identity(deferred_.bound_identity, list_identity_))
                return ProfileEdgeResult::Rejected;
            ready_notification_consumed_ = generation;
            if (!notification_eligible_) return ProfileEdgeResult::Deferred;
            state_ = ProfileListCoordinatorState::ListSetup;
        }
        return drain_deferred(callbacks);
    } catch (...) {
        return ProfileEdgeResult::Failed;
    }
}

ProfileEdgeResult ProfileListCoordinator::execute(
    Intent intent, ProfileListCallbacks& callbacks,
    const bool audio_readiness_prevalidated)
{
    try {
    {
        std::lock_guard lock(mutex_);
        if (state_ != ProfileListCoordinatorState::ProfileChanging
            || epoch_ == 0 || epoch_ > UINT32_MAX || sequence_ == UINT32_MAX) {
            state_ = ProfileListCoordinatorState::Failed;
            return ProfileEdgeResult::Failed;
        }
    }
    if ((!audio_readiness_prevalidated
            && (!callbacks.audio_idle || !callbacks.audio_idle()))
        || !callbacks.revoke_audio_activation || !callbacks.cycle_profile) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ProfileChanging)
            state_ = ProfileListCoordinatorState::Ready;
        return ProfileEdgeResult::Rejected;
    }
    if (!callbacks.validate_session
        || !callbacks.validate_session(intent.session_generation, true)
        || !callbacks.revoke_audio_activation(intent.expected, intent.session_generation)) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ProfileChanging)
            state_ = ProfileListCoordinatorState::Ready;
        return ProfileEdgeResult::Rejected;
    }
    SelectionSnapshot changed;
    if (!callbacks.cycle_profile(intent.expected, intent.delta, changed)) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ProfileChanging)
            state_ = ProfileListCoordinatorState::Ready;
        return ProfileEdgeResult::Rejected;
    }
    SelectionSnapshot validated = changed;
    if (!callbacks.revalidate
        || !callbacks.revalidate(changed, intent.ui, validated)) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ProfileChanging)
            state_ = ProfileListCoordinatorState::Failed;
        return ProfileEdgeResult::Failed;
    }
    changed = std::move(validated);
    if (callbacks.log_profile_change) callbacks.log_profile_change(changed);
    if (callbacks.observe_scoreinfo_overlay)
        (void)callbacks.observe_scoreinfo_overlay();

    intent.expected = changed;
    intent.ui.selection = changed;
    std::uint64_t ticket = 0;
    {
        std::lock_guard lock(mutex_);
        if (state_ != ProfileListCoordinatorState::ProfileChanging
            || session_generation_ != intent.session_generation
            || epoch_ == 0 || epoch_ > UINT32_MAX || sequence_ == UINT32_MAX) {
            state_ = ProfileListCoordinatorState::Failed;
            return ProfileEdgeResult::Failed;
        }
        ticket = (epoch_ << 32) | ++sequence_;
        ticket_ = ticket;
        refresh_ = std::move(intent);
        state_ = ProfileListCoordinatorState::RefreshPosted;
    }
    if (!callbacks.post_refresh || !callbacks.post_refresh(ticket)) {
        std::lock_guard lock(mutex_);
        if (ticket_ == ticket) {
            ticket_ = 0;
            refresh_ = {};
            state_ = ProfileListCoordinatorState::Failed;
        }
        return ProfileEdgeResult::Failed;
    }
    return ProfileEdgeResult::Changed;
    } catch (...) {
        std::lock_guard lock(mutex_);
        ticket_ = 0; refresh_ = {};
        if (state_ != ProfileListCoordinatorState::Shutdown)
            state_ = ProfileListCoordinatorState::Failed;
        return ProfileEdgeResult::Failed;
    }
}

bool ProfileListCoordinator::handle_refresh(
    const std::uint64_t ticket, ProfileListCallbacks& callbacks)
{
    try {
    Intent intent;
    {
        std::lock_guard lock(mutex_);
        if (state_ != ProfileListCoordinatorState::RefreshPosted
            || ticket == 0 || ticket != ticket_) return false;
        intent = refresh_;
        ticket_ = 0;
        refresh_ = {};
        state_ = ProfileListCoordinatorState::Refreshing;
    }
    if (!callbacks.validate_session
        || !callbacks.validate_session(intent.session_generation, true)) {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::Refreshing)
            state_ = ProfileListCoordinatorState::Ready;
        return false;
    }
    SelectionSnapshot adopted = intent.expected;
    bool ok = callbacks.refresh_list
        && callbacks.refresh_list(adopted, intent.ui.list_target);
    if (ok && callbacks.revalidate)
        ok = callbacks.revalidate(intent.expected, intent.ui, adopted);
    if (ok && callbacks.refresh_selection)
        ok = callbacks.refresh_selection(adopted, intent.ui.selection_target);
    if (ok && callbacks.revalidate)
        ok = callbacks.revalidate(intent.expected, intent.ui, adopted);
    {
        std::lock_guard lock(mutex_);
        if (state_ != ProfileListCoordinatorState::Refreshing) return false;
        state_ = ok ? ProfileListCoordinatorState::Ready
                    : ProfileListCoordinatorState::Failed;
    }
    return ok;
    } catch (...) {
        std::lock_guard lock(mutex_);
        ticket_ = 0; refresh_ = {};
        if (state_ != ProfileListCoordinatorState::Shutdown)
            state_ = ProfileListCoordinatorState::Failed;
        return false;
    }
}

bool ProfileListCoordinator::run_list_return(
    ListReturnCallbacks& list, ProfileListCallbacks& profile)
{
    MenuSessionSnapshot ingress_session{};
    try { if (profile.capture_session) ingress_session = profile.capture_session(true); }
    catch (...) { ingress_session = {}; }
    bool coordinated = false;
    bool readiness_ingress = false;
    {
        std::lock_guard lock(mutex_);
        if (ingress_session && !session_generation_)
            session_generation_ = ingress_session.generation;
        if (ingress_session && session_generation_ == ingress_session.generation
            && state_ == ProfileListCoordinatorState::Ready) {
            state_ = ProfileListCoordinatorState::ListEntering;
            notification_eligible_ = false;
            coordinated = true;
        } else if (ingress_session && session_generation_ == ingress_session.generation
            && state_ == ProfileListCoordinatorState::ListWaitingReadiness) {
            state_ = ProfileListCoordinatorState::ListEntering;
            notification_eligible_ = false;
            coordinated = true;
            readiness_ingress = true;
        }
    }
    try { if (list.retire_scoreinfo) list.retire_scoreinfo(); } catch (...) {}
    ListSetupView view{list.ingress_context, list.ingress_widget, list.ingress_index};
    const auto call_original = [&]() noexcept {
        view.ingress_context = list.ingress_context;
        view.ingress_widget = list.ingress_widget;
        view.ingress_index = list.ingress_index;
        try { if (list.original_setup) list.original_setup(view); } catch (...) {}
    };
    bool prepared = false;
    try { prepared = list.prepare && list.prepare(view) && view.identity; } catch (...) {}
    if (!prepared) {
        call_original();
        bool cleanup_released = false;
        try { cleanup_released = list.cleanup_audio && list.cleanup_audio(view); } catch (...) {}
        try { if (list.update_rank) list.update_rank(view); } catch (...) {}
        if (coordinated) {
            std::lock_guard lock(mutex_);
            if (state_ == ProfileListCoordinatorState::ListEntering) {
                if (deferred_.delta) state_ = ProfileListCoordinatorState::ListWaitingReadiness;
                else state_ = ProfileListCoordinatorState::Ready;
            }
        }
        return cleanup_released;
    }
    const bool bindable = view.identity.active_custom
        && view.identity.setup_selection.song
        && view.identity.session_generation == ingress_session.generation;
    if (!bindable) {
        call_original();
        bool cleanup_released = false;
        try { cleanup_released = list.cleanup_audio && list.cleanup_audio(view); } catch (...) {}
        try { if (list.update_rank) list.update_rank(view); } catch (...) {}
        if (coordinated) {
            std::lock_guard lock(mutex_);
            if (state_ == ProfileListCoordinatorState::ListEntering)
                state_ = deferred_.delta ? ProfileListCoordinatorState::ListWaitingReadiness
                                         : ProfileListCoordinatorState::Ready;
        }
        return cleanup_released;
    }
    {
        std::lock_guard lock(mutex_);
        if (coordinated && state_ == ProfileListCoordinatorState::ListEntering) {
            if (view.identity.session_generation != session_generation_) {
                deferred_ = {}; list_identity_ = {}; notification_eligible_ = false;
                state_ = ProfileListCoordinatorState::Ready;
            } else if (readiness_ingress && deferred_.session_generation
                && !same_list_identity(deferred_.bound_identity, view.identity)) {
                deferred_ = {};
                list_identity_ = {};
                notification_eligible_ = false;
            }
            list_identity_ = view.identity;
            if (deferred_.delta && !deferred_.session_generation) {
                deferred_.expected = view.identity.setup_selection;
                deferred_.session_generation = view.identity.session_generation;
                deferred_.bound_identity = view.identity;
            }
            state_ = ProfileListCoordinatorState::ListSetup;
        }
    }
    call_original();
    {
        std::lock_guard lock(mutex_);
        if (state_ == ProfileListCoordinatorState::ListSetup
            && deferred_.delta && !deferred_.session_generation) {
            deferred_.expected = view.identity.setup_selection;
            deferred_.session_generation = view.identity.session_generation;
            deferred_.bound_identity = view.identity;
        }
    }
    bool cleanup_released = false;
    try { cleanup_released = list.cleanup_audio && list.cleanup_audio(view); } catch (...) {}
    try { if (list.update_rank) list.update_rank(view); } catch (...) {}

    if (!coordinated) return false;

    {
        std::lock_guard lock(mutex_);
        if (state_ != ProfileListCoordinatorState::ListSetup
            || !same_list_identity(list_identity_, view.identity)) {
            deferred_ = {};
            list_identity_ = {};
            notification_eligible_ = false;
            state_ = ProfileListCoordinatorState::Ready;
            return false;
        }
        if (!cleanup_released) {
            if (deferred_.delta != 0) {
                state_ = ProfileListCoordinatorState::ListWaitingReadiness;
            } else {
                list_identity_ = {};
                state_ = ProfileListCoordinatorState::Ready;
            }
            return false;
        }
        if (deferred_.delta == 0) {
            list_identity_ = {};
            notification_eligible_ = false;
            state_ = ProfileListCoordinatorState::Ready;
            return true;
        }
        notification_eligible_ = true;
    }
    return drain_deferred(profile) == ProfileEdgeResult::Changed;
}

void ProfileListCoordinator::begin_session(uint64_t generation) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (!generation || state_ == ProfileListCoordinatorState::Shutdown) return;
        if (session_generation_ != generation) {
            if (epoch_ == UINT32_MAX) {
                state_ = ProfileListCoordinatorState::Failed;
                return;
            }
            ++epoch_;
            ticket_ = 0; refresh_ = {}; deferred_ = {}; list_identity_ = {};
            ready_notification_consumed_ = 0; notification_eligible_ = false;
            state_ = ProfileListCoordinatorState::Ready;
            session_generation_ = generation;
        }
    } catch (...) {}
}

void ProfileListCoordinator::retire_session(uint64_t generation) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (!generation || session_generation_ != generation) return;
        if (epoch_ == UINT32_MAX) {
            state_ = ProfileListCoordinatorState::Failed;
            return;
        }
        ++epoch_;
        ticket_ = 0; refresh_ = {}; deferred_ = {}; list_identity_ = {};
        ready_notification_consumed_ = 0; notification_eligible_ = false;
        session_generation_ = 0;
        if (state_ != ProfileListCoordinatorState::Shutdown)
            state_ = ProfileListCoordinatorState::Ready;
    } catch (...) {}
}

void ProfileListCoordinator::shutdown() noexcept
{
    try {
        std::lock_guard lock(mutex_);
        ++epoch_;
        if (epoch_ == 0) ++epoch_;
        ticket_ = 0;
        refresh_ = {};
        deferred_ = {};
        list_identity_ = {};
        ready_notification_consumed_ = 0;
        notification_eligible_ = false;
        state_ = ProfileListCoordinatorState::Shutdown;
        session_generation_ = 0;
    } catch (...) {
    }
}

ProfileListCoordinatorState ProfileListCoordinator::state() const noexcept
{
    try { std::lock_guard lock(mutex_); return state_; }
    catch (...) { return ProfileListCoordinatorState::Failed; }
}

bool ProfileListCoordinator::try_ready_for_catalog_adoption() const noexcept
{
    try {
        std::unique_lock lock(mutex_, std::try_to_lock);
        return lock.owns_lock() && state_ == ProfileListCoordinatorState::Ready;
    } catch (...) { return false; }
}

std::uint64_t ProfileListCoordinator::pending_ticket() const noexcept
{
    try { std::lock_guard lock(mutex_); return ticket_; }
    catch (...) { return 0; }
}

#ifdef FF7RP_COORDINATOR_SELFTEST
void ProfileListCoordinator::selftest_seed_ticket(
    const std::uint64_t epoch, const std::uint32_t sequence) noexcept {
    std::lock_guard lock(mutex_); epoch_ = epoch; sequence_ = sequence;
}
#endif

ProfileListCoordinator& profile_list_coordinator() { return g_coordinator; }

} // namespace ff7r::piano::game
