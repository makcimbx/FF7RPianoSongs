#include "game/menu_session_authority.h"
#include "game/mandatory_hook_transaction.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace ff7r::piano::game;

namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}

int main() {
    if (should_restore_list_after_cancel_close(false, true, true, 0x0100,
            MenuListOwnership::Managed)
        || should_restore_list_after_cancel_close(true, false, true, 0x0100,
            MenuListOwnership::Managed)
        || should_restore_list_after_cancel_close(true, true, false, 0x0100,
            MenuListOwnership::Managed)
        || should_restore_list_after_cancel_close(true, true, true, 1,
            MenuListOwnership::Managed)
        || should_restore_list_after_cancel_close(true, true, true, 0x0100,
            MenuListOwnership::Unmanaged)
        || !should_restore_list_after_cancel_close(true, true, true, 0x0100,
            MenuListOwnership::Managed)
        || !should_restore_list_after_cancel_close(true, true, true, 0x0100,
            MenuListOwnership::ReclassifyOnExactClose)) {
        return fail("exact-close restoration admission accepted a failed/non-cancel close");
    }
    void* first_binding = reinterpret_cast<void*>(0x2000);
    void* replaced_binding = reinterpret_cast<void*>(0x3000);
    const UObjectLiveHandle first_identity{7, 11};
    const UObjectLiveHandle replaced_identity{8, 12};
    if (menu_list_ownership_from_open_facts(true, false)
            != MenuListOwnership::Managed
        || menu_list_ownership_from_open_facts(false, true)
            != MenuListOwnership::ReclassifyOnExactClose
        || menu_list_ownership_from_open_facts(false, false)
            != MenuListOwnership::Unmanaged
        || menu_open_ownership_for_final_binding(first_binding,
            first_identity, first_binding, first_identity,
            MenuListOwnership::Managed) != MenuListOwnership::Managed
        || menu_open_ownership_for_final_binding(first_binding,
            first_identity, replaced_binding, first_identity,
            MenuListOwnership::Managed) != MenuListOwnership::ReclassifyOnExactClose
        || menu_open_ownership_for_final_binding(first_binding,
            first_identity, first_binding, replaced_identity,
            MenuListOwnership::Managed) != MenuListOwnership::ReclassifyOnExactClose
        || menu_open_ownership_for_final_binding(first_binding,
            first_identity, first_binding, first_identity,
            MenuListOwnership::Unmanaged) != MenuListOwnership::Unmanaged) {
        return fail("transient or replaced final binding lost exact-close reclassification");
    }
    if (classify_menu_open_admission(false, true, false)
            != MenuOpenAdmissionDisposition::ForwardOriginal
        || classify_menu_open_admission(true, false, false)
            != MenuOpenAdmissionDisposition::ForwardOriginal
        || classify_menu_open_admission(true, true, true)
            != MenuOpenAdmissionDisposition::SuppressTerminalFailure
        || classify_menu_open_admission(true, true, false)
            != MenuOpenAdmissionDisposition::SuppressTerminalFailure
        || classify_menu_open_admission(true, false, true)
            != MenuOpenAdmissionDisposition::Protected) {
        return fail("menu-open admission policy did not forward recoverable contention or suppress terminal failure");
    }
    {
        int originals = 0;
        int active_custom_rows = 2;
        bool pending = true;
        int list_mutations = 0;
        int audio_mutations = 0;
        int registry_mutations = 0;
        int lifetime_mutations = 0;
        const auto dispatch = [&](bool exact, CatalogAdoptionResult result) {
            if (classify_menu_open_catalog_result(exact, result)
                == MenuOpenCatalogDisposition::ForwardOriginal) ++originals;
        };
        dispatch(true, CatalogAdoptionResult::Blocked);
        dispatch(true, CatalogAdoptionResult::Blocked);
        if (originals != 2 || active_custom_rows != 2 || !pending
            || list_mutations != 0 || audio_mutations != 0
            || registry_mutations != 0 || lifetime_mutations != 0)
            return fail("blocked opens did not forward the unchanged active catalog exactly once each");
        dispatch(true, CatalogAdoptionResult::Adopted);
        active_custom_rows = 3;
        pending = false;
        dispatch(true, CatalogAdoptionResult::NoPending);
        dispatch(false, CatalogAdoptionResult::Blocked);
        if (originals != 5 || active_custom_rows != 3 || pending)
            return fail("later adoption, no-pending, or unrelated open did not forward exactly once");
        dispatch(true, CatalogAdoptionResult::TerminalFailure);
        if (originals != 5)
            return fail("terminal catalog failure forwarded the original open");

        active_custom_rows = 0;
        pending = true;
        dispatch(true, CatalogAdoptionResult::Blocked);
        if (originals != 6 || active_custom_rows != 0 || !pending)
            return fail("blocked initial-empty catalog did not forward vanilla/empty state");
    }
    {
        std::string order;
        std::vector<MandatoryHookOperation> operations;
        for (int i = 0; i < 4; ++i) operations.push_back({
            [&, i] { order += "i" + std::to_string(i); return i != 2; },
            [&, i] { order += "d" + std::to_string(i); return true; },
            [&, i] { order += "r" + std::to_string(i); return true; }});
        if (install_mandatory_hook_transaction(operations)
                != MandatoryHookTransactionResult::RolledBack
            || order != "i0i1i2d2d1d0r2r1r0")
            return fail("partial hook transaction rollback order failed");
    }
    {
        int disables = 0, removes = 0;
        std::vector<MandatoryHookOperation> operations;
        for (int i = 0; i < 3; ++i) operations.push_back({
            [i] { return i != 2; },
            [&, i] { ++disables; return i != 1; },
            [&] { ++removes; return true; }});
        if (install_mandatory_hook_transaction(operations)
                != MandatoryHookTransactionResult::Retained
            || disables != 3 || removes != 0)
            return fail("failed rollback did not retain ownership after full disable pass");
    }
    void* controller = reinterpret_cast<void*>(0x1000);
    void* list = reinterpret_cast<void*>(0x16f0);
    void* widget = reinterpret_cast<void*>(0x2000);
    UObjectLiveHandle identity{7, 11};
    MenuSessionAuthority authority;
    if (!authority.try_idle_for_catalog_adoption())
        return fail("idle menu authority rejected adoption try-check");

    const auto opening = authority.begin_open(controller, list, widget, identity, 0,
        MenuListOwnership::ReclassifyOnExactClose);
    if (authority.try_idle_for_catalog_adoption())
        return fail("active menu authority passed adoption try-check");
    if (!opening || opening.generation != 1 || opening.phase != MenuSessionPhase::Opening
        || opening.list_ownership != MenuListOwnership::ReclassifyOnExactClose
        || authority.capture(false) || authority.capture(true).generation != 1)
        return fail("opening publication/timing failed");
    if (authority.begin_open(controller, list, widget, identity, 0))
        return fail("duplicate open accepted");
    if (authority.publish_ready(opening, 1, widget, {7, 12})
        || !authority.publish_ready(opening, 1, widget, identity)
        || authority.capture(false).phase != MenuSessionPhase::Ready)
        return fail("ready validation/timing failed");

    const auto rejected_close = authority.begin_close(list);
    if (!rejected_close || !authority.finish_close(rejected_close, 1)
        || authority.capture(false).generation != 1)
        return fail("rejected cancel did not restore exact ready session");
    const auto closing = authority.begin_close(list);
    if (!closing
        || closing.list_ownership != MenuListOwnership::ReclassifyOnExactClose
        || authority.begin_open(controller, list, widget, identity, 0x0100)
        || !authority.finish_close(closing, 0x0100) || authority.capture(true))
        return fail("accepted cancel did not retire session");

    const auto reopened = authority.begin_open(controller, list, widget, identity, 0x0100);
    if (!reopened || reopened.generation != 2
        || reopened.list_ownership != MenuListOwnership::Unmanaged
        || !authority.publish_ready(reopened, 1, widget, identity))
        return fail("ownerless vanilla reopen did not allocate a clean generation");
    if (authority.finish_close(closing, 0x0100)
        || authority.capture(false).generation != reopened.generation)
        return fail("stale generation affected reopened session");
    if (authority.matches(reopened.generation, widget, {7, 12}, true))
        return fail("pointer reuse bypassed UObject serial identity");
    const auto second_closing = authority.begin_close(list);
    if (!second_closing || second_closing.generation != reopened.generation
        || !authority.finish_close(second_closing, 0x0100)
        || authority.capture(true))
        return fail("second exact cancel did not retire the reopened generation");
    const auto third_open = authority.begin_open(
        controller, list, widget, identity, 0x0100, MenuListOwnership::Managed);
    if (!third_open || third_open.generation != 3
        || third_open.list_ownership != MenuListOwnership::Managed
        || !authority.publish_ready(third_open, 1, widget, identity))
        return fail("second exact cancel did not permit a later legitimate open");
    if (authority.begin_ending(reinterpret_cast<void*>(0x9999)))
        return fail("wrong controller ended session");
    const auto ending = authority.begin_ending(controller);
    if (!ending || ending.phase != MenuSessionPhase::Ending
        || !authority.retire(ending.generation) || authority.retire(ending.generation))
        return fail("exit/destructor retirement was not exact/idempotent");

    MenuSessionAuthority overflow(std::numeric_limits<uint64_t>::max());
    if (overflow.begin_open(controller, list, widget, identity, 0)
        || overflow.capture(true))
        return fail("generation wrap did not fail closed");
    authority.shutdown();
    if (authority.begin_open(controller, list, widget, identity, 0))
        return fail("shutdown allowed re-entry");
    std::cout << "menu_session_authority_selftest: ok\n";
    return 0;
}
