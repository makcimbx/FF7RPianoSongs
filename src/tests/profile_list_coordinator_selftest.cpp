#include "game/profile_list_coordinator.h"

#include <iostream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace ff7r::piano::game;

namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }

struct Fixture {
    SongDescriptor song;
    std::shared_ptr<SongRegistryStorage> storage;
    SelectionSnapshot current;
    MenuUiRefreshSnapshot ui;
    std::vector<std::string> events;
    std::uint64_t ticket = 0;
    bool idle = true;
    bool cycle_ok = true;
    bool list_ok = true;
    bool selection_ok = true;
    bool drift = false;
    bool callback_active = false;
    int audio_idle_calls = 0;
    bool false_after_first_idle = false;
    std::uint64_t session_generation = 1;
    MenuSessionPhase session_phase = MenuSessionPhase::Ready;
    std::function<void()> lock_probe;

    Fixture() {
        song.id = "song"; song.visible_index = 6; song.base_slot = 1;
        song.profiles.resize(2);
        storage = std::make_shared<SongRegistryStorage>();
        storage->push_back(song);
        current = {1, storage, &(*storage)[0], &(*storage)[0].profiles[0], 0, 6, 1};
        ui.selection = current;
        ui.selection_target = {reinterpret_cast<void*>(1), {1, 1}, 6};
        ui.list_target = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3),
            {2, 2}, {3, 3}, 6};
    }

    ProfileListCallbacks callbacks() {
        ProfileListCallbacks cb;
        cb.capture_selection = [&] { events.push_back("capture"); return current; };
        cb.capture_session = [&] (bool allow_opening) {
            if (session_phase == MenuSessionPhase::Opening && !allow_opening)
                return MenuSessionSnapshot{};
            return MenuSessionSnapshot{session_generation, session_phase,
                reinterpret_cast<void*>(9), reinterpret_cast<void*>(8),
                reinterpret_cast<void*>(7), {7, 7}};
        };
        cb.validate_session = [&](uint64_t generation, bool require_ready) {
            return generation == session_generation
                && (!require_ready || session_phase == MenuSessionPhase::Ready);
        };
        cb.capture_ui = [&](const SelectionSnapshot&, MenuUiRefreshSnapshot& out) {
            events.push_back("verify_ui"); out = ui; return !drift;
        };
        cb.audio_idle = [&] {
            if (lock_probe) lock_probe();
            events.push_back("idle");
            ++audio_idle_calls;
            return false_after_first_idle ? audio_idle_calls == 1 : idle;
        };
        cb.revoke_audio_activation = [&](const SelectionSnapshot&, uint64_t generation) {
            if (lock_probe) lock_probe(); events.push_back("revoke"); return generation == session_generation;
        };
        cb.cycle_profile = [&](const SelectionSnapshot& expected, int, SelectionSnapshot& changed) {
            if (lock_probe) lock_probe(); events.push_back("cycle");
            if (!cycle_ok || !selection_semantically_matches(expected, current)) return false;
            current.generation++;
            current.profile_index = 1;
            current.profile = &(*storage)[0].profiles[1];
            changed = current;
            return true;
        };
        cb.log_profile_change = [&](const SelectionSnapshot&) { events.push_back("log"); };
        cb.observe_scoreinfo_overlay = [&] { events.push_back("scoreinfo"); return false; };
        cb.post_refresh = [&](std::uint64_t value) { if (lock_probe) lock_probe(); events.push_back("post"); ticket = value; return true; };
        cb.refresh_list = [&](const SelectionSnapshot&, const ListItemUiRefreshTarget&) {
            if (callback_active) events.push_back("lock_violation");
            events.push_back("list_refresh"); current.generation++; return list_ok;
        };
        cb.refresh_selection = [&](const SelectionSnapshot&, const SelectionUiRefreshTarget&) {
            if (callback_active) events.push_back("lock_violation");
            events.push_back("selection_refresh"); current.generation++; return selection_ok;
        };
        cb.revalidate = [&](const SelectionSnapshot& expected, const MenuUiRefreshSnapshot&, SelectionSnapshot& adopted) {
            events.push_back("revalidate");
            if (drift || !selection_semantically_matches(expected, current)) return false;
            adopted = current; return true;
        };
        return cb;
    }
};

bool contains(const std::vector<std::string>& events, const char* value) {
    for (const auto& event : events) if (event == value) return true;
    return false;
}

std::size_t count(const std::vector<std::string>& events, const char* value) {
    std::size_t result = 0;
    for (const auto& event : events) if (event == value) ++result;
    return result;
}

std::size_t position(const std::vector<std::string>& events, const char* value) {
    for (std::size_t index = 0; index < events.size(); ++index)
        if (events[index] == value) return index;
    return events.size();
}
}

int main()
{
    for (bool changed_target : {false, true}) {
        ProfileListCoordinator coordinator;
        Fixture f; f.session_phase = MenuSessionPhase::Opening;
        auto cb = f.callbacks();
        coordinator.begin_session(1);
        ListReturnCallbacks list;
        list.prepare = [&](ListSetupView& view) {
            view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3),
                6, 1, f.current, {2, 2}, {3, 3}, 1, true};
            return true;
        };
        list.original_setup = [&](const ListSetupView&) {
            (void)coordinator.submit_edge(1, cb);
        };
        list.cleanup_audio = [](const ListSetupView&) { return true; };
        (void)coordinator.run_list_return(list, cb);
        if (coordinator.state() != ProfileListCoordinatorState::ListWaitingReadiness)
            return fail("opening focus fixture did not retain early input");
        auto restored = f.current;
        if (changed_target) {
            restored.profile = &(*f.storage)[0].profiles[1]; restored.profile_index = 1;
        }
        if (!coordinator.reconcile_open_focus(1, restored)) return fail("opening focus reconciliation rejected");
        f.session_phase = MenuSessionPhase::Ready;
        const auto result = coordinator.notify_session_ready(1, cb);
        if (changed_target && (result != ProfileEdgeResult::Rejected || contains(f.events, "cycle")))
            return fail("restored focus silently retargeted already-bound input");
        if (!changed_target && (result != ProfileEdgeResult::Changed || count(f.events, "cycle") != 1))
            return fail("same focus failed to drain immediate input exactly once");
    }
    {
        ProfileListCoordinator coordinator;
        if (!coordinator.try_ready_for_catalog_adoption())
            return fail("ready coordinator rejected adoption try-check");
        coordinator.shutdown();
        if (coordinator.try_ready_for_catalog_adoption())
            return fail("non-ready coordinator passed adoption try-check");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        ListReturnCallbacks list;
        list.ingress_context = reinterpret_cast<void*>(0x1010);
        list.ingress_widget = reinterpret_cast<void*>(0x2020);
        list.ingress_index = 37;
        list.prepare = [](ListSetupView& view) -> bool {
            view.ingress_context = reinterpret_cast<void*>(0xaaaa);
            view.ingress_widget = reinterpret_cast<void*>(0xbbbb);
            view.ingress_index = -9;
            throw 1;
        };
        int original_calls = 0;
        int cleanup_calls = 0;
        int rank_calls = 0;
        list.original_setup = [&](const ListSetupView& view) {
            ++original_calls;
            if (view.ingress_context != list.ingress_context
                || view.ingress_widget != list.ingress_widget
                || view.ingress_index != list.ingress_index)
                original_calls = -100;
        };
        list.cleanup_audio = [&](const ListSetupView& view) {
            ++cleanup_calls;
            return view.ingress_context == list.ingress_context
                && view.ingress_widget == list.ingress_widget
                && view.ingress_index == list.ingress_index;
        };
        list.update_rank = [&](const ListSetupView&) { ++rank_calls; };
        if (!coordinator.run_list_return(list, cb) || original_calls != 1
            || cleanup_calls != 1 || rank_calls != 1)
            return fail("prepare failure did not forward exact ingress/cleanup/rank once");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        ListReturnCallbacks list;
        list.prepare = [&](ListSetupView& view) {
            view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3),
                6, 1, f.current, {2, 2}, {3, 3}, 1, true}; return true;
        };
        bool nested = false; int originals = 0, cleanups = 0, ranks = 0;
        list.original_setup = [&](const ListSetupView&) {
            ++originals;
            if (!std::exchange(nested, true)) (void)coordinator.run_list_return(list, cb);
        };
        list.cleanup_audio = [&](const ListSetupView&) { ++cleanups; return true; };
        list.update_rank = [&](const ListSetupView&) { ++ranks; };
        (void)coordinator.run_list_return(list, cb);
        if (originals != 2 || cleanups != 2 || ranks != 2
            || coordinator.state() != ProfileListCoordinatorState::Ready)
            return fail("reentrant setup changed forwarding counts or coordinator state");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        cb.capture_session = [](bool) -> MenuSessionSnapshot { throw 1; };
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Rejected
            || coordinator.state() != ProfileListCoordinatorState::Ready)
            return fail("capture exception escaped or poisoned coordinator");
    }
    for (const bool throwing : {false, true}) {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        cb.post_refresh = [&, throwing](std::uint64_t) {
            if (throwing) throw 1; return false;
        };
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Failed
            || coordinator.pending_ticket() != 0
            || coordinator.state() != ProfileListCoordinatorState::Failed)
            return fail("PostMessage failure/exception was not contained fail-closed");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        coordinator.selftest_seed_ticket(1, UINT32_MAX);
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Failed
            || contains(f.events, "revoke") || contains(f.events, "cycle"))
            return fail("ticket overflow mutated before failing closed");
    }
    {
        ProfileListCoordinator coordinator;
        coordinator.selftest_seed_ticket(UINT32_MAX, 0);
        coordinator.begin_session(1);
        if (coordinator.state() != ProfileListCoordinatorState::Failed)
            return fail("ticket epoch overflow did not fail closed");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        bool active = false;
        ListReturnCallbacks list;
        list.prepare = [&](ListSetupView& view) {
            view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3),
                active ? 6 : 0, 1, f.current, {2, 2}, {3, 3}, 1, active};
            return true;
        };
        list.original_setup = [&](const ListSetupView&) {
            if (!active) (void)coordinator.submit_edge(1, cb);
        };
        list.cleanup_audio = [](const ListSetupView&) { return true; };
        (void)coordinator.run_list_return(list, cb);
        if (coordinator.state() != ProfileListCoordinatorState::ListWaitingReadiness
            || contains(f.events, "cycle"))
            return fail("unrelated row dropped or applied early intent");
        active = true;
        if (!coordinator.run_list_return(list, cb) || count(f.events, "cycle") != 1)
            return fail("row0 to active custom ingress did not drain exactly once");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        MenuSessionAuthority authority;
        const auto opening = authority.begin_open(
            reinterpret_cast<void*>(9), reinterpret_cast<void*>(8),
            reinterpret_cast<void*>(3), {3, 3}, 0);
        cb.capture_session = [&](bool allow_opening) {
            return authority.capture(allow_opening);
        };
        cb.validate_session = [&](uint64_t generation, bool require_ready) {
            const auto session = authority.capture(!require_ready);
            return session && session.generation == generation;
        };
        coordinator.begin_session(opening.generation);
        ListReturnCallbacks list;
        list.prepare = [&](ListSetupView& view) { view.identity = {
            reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1,
            f.current, {2, 2}, {3, 3}, 1, true}; return true; };
        list.original_setup = [&](const ListSetupView&) {
            (void)coordinator.submit_edge(1, cb);
        };
        list.cleanup_audio = [](const ListSetupView&) { return true; };
        (void)coordinator.run_list_return(list, cb);
        if (coordinator.state() != ProfileListCoordinatorState::ListWaitingReadiness
            || contains(f.events, "cycle"))
            return fail("Opening list callback did not retain exact edge");
        if (!authority.publish_ready(opening, 1, reinterpret_cast<void*>(3), {3, 3}))
            return fail("production-shaped Ready publication failed");
        if (coordinator.notify_session_ready(2, cb) != ProfileEdgeResult::Rejected
            || coordinator.notify_session_ready(1, cb) != ProfileEdgeResult::Changed
            || count(f.events, "cycle") != 1 || f.audio_idle_calls != 1
            || coordinator.notify_session_ready(1, cb) != ProfileEdgeResult::Rejected)
            return fail("one-shot Opening-to-Ready notification did not drain exactly once");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; f.session_phase = MenuSessionPhase::Opening;
        auto cb = f.callbacks(); coordinator.begin_session(1);
        ListReturnCallbacks list;
        list.prepare = [&](ListSetupView& view) { view.identity = {
            reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1,
            f.current, {2, 2}, {3, 3}, 1, true}; return true; };
        list.original_setup = [&](const ListSetupView&) {
            (void)coordinator.submit_edge(1, cb);
        };
        list.cleanup_audio = [](const ListSetupView&) { return true; };
        (void)coordinator.run_list_return(list, cb);
        coordinator.retire_session(1); coordinator.begin_session(2);
        f.session_generation = 2; f.session_phase = MenuSessionPhase::Ready;
        if (coordinator.notify_session_ready(1, cb) != ProfileEdgeResult::Rejected
            || contains(f.events, "cycle"))
            return fail("closed/reopened session revived stale Ready notification");
    }
    for (int unavailable = 0; unavailable < 5; ++unavailable) {
        ProfileListCoordinator coordinator;
        Fixture f; f.session_phase = MenuSessionPhase::Opening;
        auto cb = f.callbacks(); coordinator.begin_session(1);
        bool submitted = false;
        ListReturnCallbacks list;
        list.prepare = [&](ListSetupView& view) { view.identity = {
            reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1,
            f.current, {2, 2}, {3, 3}, 1, true}; return true; };
        list.original_setup = [&](const ListSetupView&) {
            if (!std::exchange(submitted, true))
                (void)coordinator.submit_edge(1, cb);
        };
        bool cleanup_ready = unavailable != 0;
        list.cleanup_audio = [&](const ListSetupView&) { return cleanup_ready; };
        if (unavailable == 1) f.idle = false;
        if (unavailable == 2) f.drift = true;
        (void)coordinator.run_list_return(list, cb);
        f.session_phase = MenuSessionPhase::Ready;
        const auto ready_ui = cb.capture_ui;
        if (unavailable == 3) cb.capture_ui = [](
            const SelectionSnapshot&, MenuUiRefreshSnapshot&) -> bool { throw 1; };
        if (unavailable == 4) ++f.ui.list_target.widget_identity.serial_number;
        if (coordinator.notify_session_ready(1, cb) != ProfileEdgeResult::Deferred
            || coordinator.state() != ProfileListCoordinatorState::ListWaitingReadiness
            || contains(f.events, "cycle")
            || coordinator.notify_session_ready(1, cb) != ProfileEdgeResult::Rejected)
            return fail("unavailable Ready notification did not remain one-shot pending");
        cb.capture_ui = ready_ui;
        if (unavailable == 4) --f.ui.list_target.widget_identity.serial_number;
        cleanup_ready = true; f.idle = true; f.drift = false;
        (void)coordinator.run_list_return(list, cb);
        if (count(f.events, "cycle") != 1)
            return fail("existing exact readiness ingress did not drain pending notification");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Changed)
            return fail("session invalidation setup failed");
        const auto stale = f.ticket;
        coordinator.retire_session(1);
        f.session_generation = 2;
        coordinator.begin_session(2);
        if (coordinator.handle_refresh(stale, cb)
            || coordinator.state() != ProfileListCoordinatorState::Ready)
            return fail("reopen revived stale session ticket");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; f.lock_probe = [&] { (void)coordinator.state(); };
        auto cb = f.callbacks();
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Changed || !f.ticket)
            return fail("profile change did not publish a ticket");
        if (coordinator.handle_refresh(f.ticket + 1, cb)
            || !coordinator.handle_refresh(f.ticket, cb)
            || coordinator.handle_refresh(f.ticket, cb)
            || coordinator.state() != ProfileListCoordinatorState::Ready)
            return fail("ticket claim was not exact and one-shot");
        const std::vector<std::string> expected{"capture","verify_ui","idle","revoke","cycle","revalidate","log","scoreinfo","post","list_refresh","revalidate","selection_refresh","revalidate"};
        if (f.events != expected) return fail("profile event order changed");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        ListReturnCallbacks list;
        list.retire_scoreinfo = [&] { f.events.push_back("retire"); };
        list.prepare = [&](ListSetupView& view) {
            f.events.push_back("prepare");
            view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1, f.current, {2, 2}, {3, 3}, 1, true};
            return true;
        };
        list.original_setup = [&](const ListSetupView&) {
            f.events.push_back("setup");
            if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Deferred)
                f.events.push_back("defer_failed");
            if (coordinator.submit_edge(-1, cb) != ProfileEdgeResult::Rejected)
                f.events.push_back("opposite_accepted");
        };
        list.cleanup_audio = [&](const ListSetupView&) { (void)coordinator.state(); f.events.push_back("cleanup"); return true; };
        list.update_rank = [&](const ListSetupView&) { (void)coordinator.state(); f.events.push_back("rank"); };
        if (!coordinator.run_list_return(list, cb) || !f.ticket)
            return fail("deferred list transition did not drain exactly once");
        if (count(f.events, "cleanup") != 1 || count(f.events, "rank") != 1
            || position(f.events, "cleanup") >= position(f.events, "rank")
            || position(f.events, "rank") >= position(f.events, "idle")
            || contains(f.events, "defer_failed")
            || contains(f.events, "opposite_accepted"))
            return fail("list return/deferred ordering failed");
    }
    for (int injection = 0; injection != 2; ++injection) {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        ListReturnCallbacks list;
        list.retire_scoreinfo = [&] {
            f.events.push_back("retire");
            if (injection == 0 && coordinator.submit_edge(1, cb) != ProfileEdgeResult::Deferred)
                f.events.push_back("ingress_defer_failed");
        };
        list.prepare = [&](ListSetupView& view) {
            f.events.push_back("prepare");
            if (injection == 1 && coordinator.submit_edge(1, cb) != ProfileEdgeResult::Deferred)
                f.events.push_back("prepare_defer_failed");
            view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1, f.current, {2, 2}, {3, 3}, 1, true};
            return true;
        };
        list.original_setup = [&](const ListSetupView&) { f.events.push_back("setup"); };
        list.cleanup_audio = [&](const ListSetupView&) { f.events.push_back("cleanup"); return true; };
        list.update_rank = [&](const ListSetupView&) { f.events.push_back("rank"); };
        if (!coordinator.run_list_return(list, cb) || !f.ticket
            || contains(f.events, "ingress_defer_failed")
            || contains(f.events, "prepare_defer_failed")
            || count(f.events, "cycle") != 1
            || position(f.events, "rank") >= position(f.events, "capture"))
            return fail("pre-identity list edge was not bound and drained exactly once");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        ListReturnCallbacks list;
        list.retire_scoreinfo = [] {};
        list.prepare = [&](ListSetupView& view) { view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1, f.current, {2, 2}, {3, 3}, 1, true}; return true; };
        list.original_setup = [&](const ListSetupView&) { (void)coordinator.submit_edge(1, cb); };
        bool retained = true;
        list.cleanup_audio = [&](const ListSetupView&) { return !std::exchange(retained, false); };
        list.update_rank = [](const ListSetupView&) {};
        if (coordinator.run_list_return(list, cb) || f.ticket || contains(f.events, "cycle"))
            return fail("Retained cleanup allowed profile mutation");
        if (coordinator.state() != ProfileListCoordinatorState::ListWaitingReadiness
            || !coordinator.run_list_return(list, cb) || !f.ticket
            || count(f.events, "cycle") != 1)
            return fail("Retained intent did not drain on exact readiness ingress");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks(); f.cycle_ok = false;
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Rejected || f.ticket)
            return fail("rejected cycle published ticket");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; f.idle = false; auto cb = f.callbacks();
        ListReturnCallbacks list;
        list.retire_scoreinfo = [] {};
        list.prepare = [&](ListSetupView& view) { view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1, f.current, {2, 2}, {3, 3}, 1, true}; return true; };
        list.original_setup = [&](const ListSetupView&) { if (!contains(f.events, "edge")) { f.events.push_back("edge"); (void)coordinator.submit_edge(1, cb); } };
        list.cleanup_audio = [](const ListSetupView&) { return true; };
        list.update_rank = [](const ListSetupView&) {};
        if (coordinator.run_list_return(list, cb) || coordinator.state() != ProfileListCoordinatorState::ListWaitingReadiness
            || f.audio_idle_calls != 1 || contains(f.events, "revoke")
            || contains(f.events, "cycle") || f.ticket)
            return fail("false audio-idle allowed or dropped deferred mutation");
        f.idle = true;
        if (!coordinator.run_list_return(list, cb) || !f.ticket
            || f.audio_idle_calls != 2 || count(f.events, "cycle") != 1)
            return fail("audio-idle readiness ingress did not drain exact intent");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; f.false_after_first_idle = true; auto cb = f.callbacks();
        ListReturnCallbacks list;
        list.retire_scoreinfo = [] {};
        list.prepare = [&](ListSetupView& view) { view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1, f.current, {2, 2}, {3, 3}, 1, true}; return true; };
        list.original_setup = [&](const ListSetupView&) { (void)coordinator.submit_edge(1, cb); };
        list.cleanup_audio = [](const ListSetupView&) { return true; };
        list.update_rank = [](const ListSetupView&) {};
        if (!coordinator.run_list_return(list, cb) || !f.ticket
            || f.audio_idle_calls != 1 || count(f.events, "cycle") != 1
            || coordinator.state() != ProfileListCoordinatorState::RefreshPosted)
            return fail("deferred mutation observed audio readiness more than once");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Changed) return fail("setup failed");
        f.list_ok = false;
        if (coordinator.handle_refresh(f.ticket, cb)
            || coordinator.state() != ProfileListCoordinatorState::Failed
            || contains(f.events, "selection_refresh"))
            return fail("partial refresh did not fail closed");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        ListReturnCallbacks list;
        list.retire_scoreinfo = [] {};
        list.prepare = [&](ListSetupView& view) { view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1, f.current, {2, 2}, {3, 3}, 1, true}; return true; };
        list.original_setup = [](const ListSetupView& view) {
            const_cast<ListSetupView&>(view).identity.widget = reinterpret_cast<void*>(6);
        };
        list.cleanup_audio = [](const ListSetupView&) { return true; };
        list.update_rank = [](const ListSetupView&) {};
        if (coordinator.run_list_return(list, cb)
            || coordinator.state() != ProfileListCoordinatorState::Ready)
            return fail("list identity drift retargeted completion");
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Changed)
            return fail("list identity drift permanently poisoned coordinator");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks();
        bool first = true;
        ListReturnCallbacks list;
        list.retire_scoreinfo = [] {};
        list.prepare = [&](ListSetupView& view) {
            view.identity = {reinterpret_cast<void*>(4),
                reinterpret_cast<void*>(5), 6, 1, f.current, {}, {}, 1, true};
            view.identity.context_identity = {2, 2};
            view.identity.widget_identity = {3, first ? 3 : 4};
            return true;
        };
        list.original_setup = [&](const ListSetupView&) {
            if (first) (void)coordinator.submit_edge(1, cb);
        };
        list.cleanup_audio = [&](const ListSetupView&) { return !first; };
        list.update_rank = [](const ListSetupView&) {};
        if (coordinator.run_list_return(list, cb)
            || coordinator.state() != ProfileListCoordinatorState::ListWaitingReadiness)
            return fail("retained drift setup did not preserve pending intent");
        if (contains(f.events, "cycle")) return fail("retained cleanup mutated before readiness");
        first = false;
        (void)coordinator.run_list_return(list, cb);
        if (contains(f.events, "cycle") || f.ticket
            || coordinator.state() != ProfileListCoordinatorState::Ready)
            return fail("retained intent retargeted a different list identity");
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Changed)
            return fail("retained identity rejection permanently poisoned coordinator");
    }
    {
        ProfileListCoordinator coordinator;
        Fixture f; auto cb = f.callbacks(); f.drift = true;
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Rejected
            || contains(f.events, "cycle")) return fail("gameplay/identity drift input was queued");
        f.drift = false;
        if (coordinator.submit_edge(1, cb) != ProfileEdgeResult::Changed) return fail("shutdown setup failed");
        const auto stale = f.ticket; coordinator.shutdown();
        ListReturnCallbacks list;
        list.retire_scoreinfo = [] {};
        list.prepare = [&](ListSetupView& view) { view.identity = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), 6, 1, f.current, {2, 2}, {3, 3}, 1, true}; return true; };
        list.original_setup = [](const ListSetupView&) {};
        list.cleanup_audio = [](const ListSetupView&) { return true; };
        list.update_rank = [](const ListSetupView&) {};
        if (coordinator.handle_refresh(stale, cb)
            || coordinator.run_list_return(list, cb)
            || coordinator.submit_edge(1, cb) != ProfileEdgeResult::Rejected
            || coordinator.state() != ProfileListCoordinatorState::Shutdown)
            return fail("shutdown accepted stale message or re-entered");
    }
    std::cout << "profile_list_coordinator_selftest: ok\n";
}
