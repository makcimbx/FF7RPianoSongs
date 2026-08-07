#include "game/list_patch_selftest.h"
#include "game/module_hooks.h"
#include "game/runtime_layouts.h"
#include "game/hook_specs.h"
#include "game/audio_sead.h"
#include "game/progress.h"
#include "game/title.h"

#include <array>
#include <cstring>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace game = ff7r::piano::game;
namespace layouts = ff7r::piano::game::runtime_layouts;

namespace {
bool identity_valid = true;
bool capture_identity_valid = true;
void* expected_widget = nullptr;
void* expected_item_widget = nullptr;
bool menu_session_valid = true;
bool drift_session_on_revalidation = false;
int menu_session_capture_count = 0;
int preference_reads = 0;
int preferred_profile_index = 0;
std::array<uint8_t, 8> trace{};
size_t trace_count = 0;

bool identity(void* object, const game::UObjectLiveHandle&) noexcept
{
    return identity_valid
        && (object == expected_widget || object == expected_item_widget);
}
void event(uint8_t value) noexcept { trace[trace_count++] = value; }
bool require(bool value, const char* message)
{
    if (!value) std::cerr << "list_catalog_transaction_selftest: " << message << '\n';
    return value;
}

game::SongDescriptor song(int visible)
{
    game::SongDescriptor value;
    value.id = "list-" + std::to_string(visible);
    value.visible_index = visible;
    value.base_slot = 0;
    value.profiles.resize(2);
    value.profiles[0].difficulty = 1;
    value.profiles[1].difficulty = 7;
    return value;
}

template<class T> void field(std::vector<std::byte>& object, uintptr_t offset, T value)
{
    std::memcpy(object.data() + offset, &value, sizeof(value));
}
template<class T> T field(const std::vector<std::byte>& object, uintptr_t offset)
{
    T value{};
    std::memcpy(&value, object.data() + offset, sizeof(value));
    return value;
}
} // namespace

namespace ff7r::piano::game {
// The transaction test does not execute setup rendering or hook installation.
core::HookCallbackGate& non_audio_hook_gate() { static core::HookCallbackGate gate; return gate; }
ProfileListCallbacks make_profile_list_callbacks() { return {}; }
bool retire_scoreinfo_overlay_for_list_return() noexcept { return true; }
void begin_scoreinfo_list_item(const SelectionSnapshot&) {}
void end_scoreinfo_list_item() {}
bool menu_session_capture_for_callback(MenuSessionSnapshot&) noexcept { return false; }
bool menu_session_generation_matches(uint64_t) noexcept { return false; }
ProfileListCoordinator& profile_list_coordinator() { static ProfileListCoordinator value; return value; }
bool ProfileListCoordinator::run_list_return(ListReturnCallbacks&, ProfileListCallbacks&) { return false; }
bool custom_audio_route_idle_for_menu_input() noexcept { return true; }
bool validate_live_uobject_handle(void* object, const UObjectLiveHandle& identity) {
    return ::identity(object, identity);
}
bool capture_live_uobject_handle(void* object, UObjectLiveHandle& out) {
    if (!capture_identity_valid || !identity_valid
        || (object != expected_widget && object != expected_item_widget)) return false;
    out = {4, 40};
    return true;
}
MenuSessionSnapshot capture_menu_callback_session() noexcept {
    ++menu_session_capture_count;
    if (!menu_session_valid) return {};
    MenuSessionSnapshot session;
    session.generation = drift_session_on_revalidation
            && menu_session_capture_count > 1 ? 8 : 7;
    session.phase = MenuSessionPhase::Ready;
    session.widget = expected_widget;
    session.widget_identity = {4, 40};
    return session;
}
const HookSpec* find_hook_spec(std::string_view) { return nullptr; }
AudioRouteCleanupResult release_audio_route_on_piano_list_return(int) { return {}; }
std::wstring custom_scores_ini_path(HMODULE) { return {}; }
int load_last_played_profile_index(const SongDescriptor&, const std::wstring&) {
    ++preference_reads;
    return preferred_profile_index;
}
int load_last_played_profile_index(const SongDescriptor&) {
    ++preference_reads;
    return preferred_profile_index;
}
ProgressRecord load_progress(const SongDescriptor&, const std::wstring&,
    const SongDifficultyProfile*) { return {}; }
RankText compute_rank_text(const SongDescriptor&, const ProgressRecord&,
    const SongDifficultyProfile*) { return {}; }
void retire_scoreinfo_result_authority_for_list() noexcept {}
} // namespace ff7r::piano::game

int main()
{
    bool ok = true;
    ok &= require(!game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::Restored)
            && !game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::VerifiedOwnerless)
            && game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::NoOwnedPatch)
            && game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::SessionIdentityMismatch)
            && game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::LiveIdentityInvalid)
            && game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::CatalogIdentityMismatch)
            && game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::TupleUnreadable)
            && game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::TupleDrift)
            && game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::TransitionFailed)
            && game::exact_close_list_restore_is_terminal(
                game::ExactCloseListRestoreResult::RollbackUnverified),
        "exact-close failure disposition did not require terminal admission");
    std::vector<std::byte> widget(0x900);
    std::array<layouts::PianoListEntry, layouts::PianoMusicList::vanilla_count> vanilla{};
    expected_widget = widget.data();

    field(widget, layouts::PianoMusicList::entries, vanilla.data());
    field(widget, layouts::PianoMusicList::count,
        static_cast<int32_t>(vanilla.size()));
    field(widget, layouts::PianoMusicList::capacity,
        static_cast<int32_t>(vanilla.size()));
    const game::UObjectLiveHandle live{4, 40};

    game::configure_list_catalog_selftest(&identity, {}, &event);
    ok &= require(game::piano_list_catalog_owner_state(widget.data(), live)
                == game::PianoListOwnerState::None
            && !game::piano_list_catalog_terminal_failure(),
        "ownerless empty authoritative registry was not admitted as vanilla");
    const game::MenuSessionSnapshot empty_reclassified_close{1,
        game::MenuSessionPhase::Closing, nullptr, nullptr, widget.data(), live,
        game::MenuListOwnership::ReclassifyOnExactClose};
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                empty_reclassified_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::VerifiedOwnerless
            && !game::piano_list_catalog_terminal_failure(),
        "transient ownerless empty close was not verified nonterminal");
    std::mutex owner_contention_mutex;
    std::condition_variable owner_contention_changed;
    bool owner_lock_held = false;
    bool release_owner_lock = false;
    std::thread owner_lock_holder([&] {
        game::list_catalog_selftest_lock_owners();
        {
            std::lock_guard lock(owner_contention_mutex);
            owner_lock_held = true;
        }
        owner_contention_changed.notify_all();
        {
            std::unique_lock lock(owner_contention_mutex);
            owner_contention_changed.wait(lock, [&] { return release_owner_lock; });
        }
        game::list_catalog_selftest_unlock_owners();
    });
    {
        std::unique_lock lock(owner_contention_mutex);
        owner_contention_changed.wait(lock, [&] { return owner_lock_held; });
    }
    ok &= require(game::piano_list_catalog_owner_state(widget.data(), live)
                == game::PianoListOwnerState::Transient
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Transient
            && !game::piano_list_catalog_terminal_failure(),
        "contended list inspection did not remain a nonterminal native fallback");
    {
        std::lock_guard lock(owner_contention_mutex);
        release_owner_lock = true;
    }
    owner_contention_changed.notify_all();
    owner_lock_holder.join();
    auto base = game::registry().registry_snapshot();
    game::SongRegistryStorage replacement_songs;
    for (int visible = 5; visible <= 16; ++visible)
        replacement_songs.push_back(song(visible));
    auto replacement = std::make_shared<const game::SongRegistryStorage>(
        std::move(replacement_songs));

    // Preparation must try-lock ownership and return without touching the tuple.
    std::mutex owner_test_mutex;
    std::condition_variable owner_test_changed;
    bool owner_locked = false;
    bool owner_release = false;
    std::thread owner_holder([&] {
        game::list_catalog_selftest_lock_owners();
        {
            std::lock_guard lock(owner_test_mutex);
            owner_locked = true;
        }
        owner_test_changed.notify_all();
        std::unique_lock lock(owner_test_mutex);
        owner_test_changed.wait(lock, [&] { return owner_release; });
        game::list_catalog_selftest_unlock_owners();
    });
    {
        std::unique_lock lock(owner_test_mutex);
        owner_test_changed.wait(lock, [&] { return owner_locked; });
    }
    ok &= require(!game::prepare_piano_list_catalog(widget.data(), live, base, replacement)
            && field<layouts::PianoListEntry*>(widget, layouts::PianoMusicList::entries)
                == vanilla.data(),
        "contended owner preparation blocked or wrote the tuple");
    {
        std::lock_guard lock(owner_test_mutex);
        owner_release = true;
    }
    owner_test_changed.notify_all();
    owner_holder.join();

    identity_valid = false;
    ok &= require(!game::prepare_piano_list_catalog(widget.data(), live, base, replacement)
            && field<layouts::PianoListEntry*>(widget, layouts::PianoMusicList::entries) == vanilla.data(),
        "stale widget preparation wrote the tuple");
    identity_valid = true;

    expected_widget = reinterpret_cast<void*>(1);
    ok &= require(!game::prepare_piano_list_catalog(expected_widget, live, base, replacement),
        "unreadable widget preparation was accepted");
    expected_widget = widget.data();

    for (int write = 1; write <= 3; ++write) {
        game::configure_list_catalog_selftest(&identity, {write, 0}, &event);
        auto prepared = game::prepare_piano_list_catalog(widget.data(), live, base, replacement);
        ok &= require(prepared
                && game::commit_prepared_piano_list_catalog(*prepared)
                    == game::PianoListCatalogCommitResult::Rejected
                && field<layouts::PianoListEntry*>(widget, layouts::PianoMusicList::entries) == vanilla.data()
                && field<int32_t>(widget, layouts::PianoMusicList::count)
                    == static_cast<int32_t>(vanilla.size())
                && game::list_catalog_selftest_owner_count() == 0,
            "forward write fault did not exactly restore native tuple");
    }

    game::configure_list_catalog_selftest(&identity, {}, &event);
    auto prefix = game::prepare_piano_list_catalog(widget.data(), live, base, replacement);
    identity_valid = false;
    ok &= require(prefix && game::commit_prepared_piano_list_catalog(*prefix)
            == game::PianoListCatalogCommitResult::Rejected
            && game::list_catalog_selftest_owner_count() == 0,
        "commit-time replaced widget identity was accepted");
    identity_valid = true;
    prefix = game::prepare_piano_list_catalog(widget.data(), live, base, replacement);
    trace_count = 0;
    ok &= require(prefix && game::commit_prepared_piano_list_catalog(*prefix)
            == game::PianoListCatalogCommitResult::Committed
            && trace_count == 2 && trace[0] == 0 && trace[1] == 1,
        "actual list transaction commit order changed");
    auto registry_commit = game::registry().begin_catalog_commit(base, replacement);
    ok &= require(registry_commit != nullptr, "registry commit preparation failed");
    game::registry().commit_catalog(*registry_commit);
    game::finalize_prepared_piano_list_catalog(*prefix);
    registry_commit.reset();
    ok &= require(game::list_catalog_selftest_matches_registry(widget.data()),
        "setup coherence rejected committed owner and registry identity");

    std::vector<std::byte> item_widget(0x500);
    expected_item_widget = item_widget.data();
    game::SelectionSnapshot initialized;
    const auto setup_generation = game::registry().registry_snapshot().generation;
    preference_reads = 0;
    ok &= require(!game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 10, initialized)
            && preference_reads == 0
            && game::registry().registry_snapshot().generation == setup_generation,
        "closed hook admission read persistence or mutated the registry");
    game::non_audio_hook_gate().open();
    identity_valid = false;
    ok &= require(!game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 10, initialized)
            && preference_reads == 0
            && game::registry().registry_snapshot().generation == setup_generation,
        "catalog admission failure read persistence or mutated the registry");
    identity_valid = true;
    capture_identity_valid = false;
    ok &= require(!game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 10, initialized)
            && preference_reads == 0
            && game::registry().registry_snapshot().generation == setup_generation,
        "UObject admission failure read persistence or mutated the registry");
    capture_identity_valid = true;
    menu_session_valid = false;
    ok &= require(!game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 10, initialized)
            && preference_reads == 0
            && game::registry().registry_snapshot().generation == setup_generation,
        "missing menu-session admission read persistence or mutated the registry");
    menu_session_valid = true;
    preferred_profile_index = 1;
    menu_session_capture_count = 0;
    ok &= require(game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 10, initialized)
            && preference_reads == 1 && initialized.profile_index == 1
            && initialized.profile && initialized.profile->difficulty == 7,
        "admitted first setup did not initialize the persisted profile");
    const auto initialized_generation = game::registry().registry_snapshot().generation;
    {
        game::ScopedSongRenderContext render(initialized);
        const game::RenderSnapshot projected = game::registry().render_snapshot();
        ok &= require(projected.profile == initialized.profile
                && projected.profile_index == 1,
            "initialized profile did not reach row render projection");
    }
    preferred_profile_index = 0;
    menu_session_capture_count = 0;
    ok &= require(game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 10, initialized)
            && preference_reads == 1 && initialized.profile_index == 1
            && game::registry().registry_snapshot().generation
                == initialized_generation,
        "duplicate setup overwrote the initialized profile");

    preference_reads = 0;
    preferred_profile_index = 1;
    drift_session_on_revalidation = true;
    menu_session_capture_count = 0;
    const auto stale_generation = game::registry().registry_snapshot().generation;
    ok &= require(!game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 11, initialized)
            && preference_reads == 1
            && game::registry().registry_snapshot().generation == stale_generation
            && game::registry().snapshot_for_visible_index(11).profile_index == 0,
        "stale session revalidation mutated the registry");
    drift_session_on_revalidation = false;
    menu_session_capture_count = 0;

    game::registry().set_active_selection(11, 0);
    game::SelectionSnapshot playing = game::registry().selection_snapshot();
    game::CustomContextToken playing_token{};
    playing_token.registry_generation = playing.generation;
    playing_token.route_generation = 1;
    playing_token.lease_generation = 1;
    playing_token.song_key = 11;
    preference_reads = 0;
    preferred_profile_index = 1;
    game::SelectionSnapshot queried;
    const auto blocked_generation = game::registry().registry_snapshot().generation;
    ok &= require(game::registry().publish_playback(playing, playing_token)
            && game::list_profile_initialization_selftest(widget.data(),
                item_widget.data(), 11, initialized)
            && game::list_profile_initialization_selftest(widget.data(),
                item_widget.data(), 11, initialized)
            && initialized.profile_index == 0 && preference_reads == 0
            && game::registry().registry_snapshot().generation == blocked_generation
            && game::registry().initialized_profile_state(initialized, queried)
                == game::InitializedProfileState::Deferred,
        "ownership-blocked no-op setup did not render without sealing");
    {
        game::ScopedSongRenderContext render(initialized);
        const game::RenderSnapshot projected = game::registry().render_snapshot();
        ok &= require(projected.profile == initialized.profile
                && projected.profile_index == 0,
            "deferred setup did not preserve custom row/rank projection");
    }
    ok &= require(game::registry().retire_cleanup_lease(playing_token),
        "deferred list setup fixture did not retire playback ownership");
    menu_session_capture_count = 0;
    ok &= require(game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 11, initialized)
            && initialized.profile_index == 1 && preference_reads == 1
            && game::registry().registry_snapshot().generation
                == blocked_generation + 1
            && game::registry().initialized_profile_state(initialized, queried)
                == game::InitializedProfileState::Present,
        "ownership-free setup did not seal the persisted profile");
    menu_session_capture_count = 0;
    ok &= require(game::list_profile_initialization_selftest(widget.data(),
            item_widget.data(), 11, initialized)
            && initialized.profile_index == 1 && preference_reads == 1,
        "sealed deferred profile performed duplicate preference I/O");
    game::registry().clear_active_selection();

    const auto adopted = game::registry().registry_snapshot();
    game::registry().set_active_selection(10, 0);
    ok &= require(game::registry().cycle_active_profile(-1),
        "profile churn fixture did not advance state generation");
    game::registry().clear_active_selection();
    game::registry().set_active_selection(15, 0);
    game::registry().clear_active_selection();
    const auto churned = game::registry().registry_snapshot();
    bool churn_indices_exact = churned.storage == adopted.storage
        && churned.catalog_revision == adopted.catalog_revision
        && churned.generation > adopted.generation;
    for (int visible = 10; visible <= 15; ++visible)
        churn_indices_exact = churn_indices_exact
            && churned.by_visible_index(visible) != nullptr;
    ok &= require(churn_indices_exact
            && game::list_catalog_selftest_matches_registry(widget.data())
            && game::piano_list_catalog_owner_state(widget.data(), live)
                == game::PianoListOwnerState::Managed,
        "state-generation churn invalidated unchanged list catalog indices 10-15");

    base = game::registry().registry_snapshot();
    game::SongRegistryStorage newer_songs = *replacement;
    newer_songs.push_back(song(17));
    auto newer = std::make_shared<const game::SongRegistryStorage>(
        std::move(newer_songs));
    auto newer_prefix = game::prepare_piano_list_catalog(widget.data(), live, base, newer);
    ok &= require(newer_prefix && game::commit_prepared_piano_list_catalog(*newer_prefix)
            == game::PianoListCatalogCommitResult::Committed,
        "prefix-to-newer-prefix tuple commit failed");
    registry_commit = game::registry().begin_catalog_commit(base, newer);
    ok &= require(registry_commit != nullptr, "newer registry commit preparation failed");
    game::registry().commit_catalog(*registry_commit);
    game::finalize_prepared_piano_list_catalog(*newer_prefix);
    registry_commit.reset();
    ok &= require(game::list_catalog_selftest_owner_count() == 1
            && game::list_catalog_selftest_matches_registry(widget.data()),
        "newer prefix did not retain exactly the current owner");
    const auto authoritative_catalog = game::registry().registry_snapshot();
    const game::MenuSessionSnapshot exact_close{7,
        game::MenuSessionPhase::Closing, nullptr, nullptr, widget.data(), live,
        game::MenuListOwnership::Managed};
    trace_count = 0;
    game::configure_list_catalog_selftest(&identity, {}, &event);
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                exact_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::Restored
            && trace_count == 3 && trace[0] == 2 && trace[1] == 3
            && trace[2] == 4
            && game::list_catalog_selftest_owner_count() == 0
            && field<layouts::PianoListEntry*>(widget,
                layouts::PianoMusicList::entries) == vanilla.data()
            && field<int32_t>(widget, layouts::PianoMusicList::count)
                == static_cast<int32_t>(vanilla.size())
            && field<int32_t>(widget, layouts::PianoMusicList::capacity)
                == static_cast<int32_t>(vanilla.size())
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Pending
            && !game::piano_list_catalog_terminal_failure(),
        "first exact-close restoration did not retire storage and preserve reopen admission");

    ok &= require(game::piano_list_catalog_owner_state(widget.data(), live)
                == game::PianoListOwnerState::None
            && !game::piano_list_catalog_terminal_failure(),
        "exact restored candidate was not admitted as a nonterminal native fallback");
    const game::MenuSessionSnapshot candidate_reclassified_close{8,
        game::MenuSessionPhase::Closing, nullptr, nullptr, widget.data(), live,
        game::MenuListOwnership::ReclassifyOnExactClose};
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                candidate_reclassified_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::VerifiedOwnerless
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Pending
            && !game::piano_list_catalog_terminal_failure(),
        "transient exact-candidate ownerless close was not verified nonterminal");
    game::registry().set_active_selection(10, 0);
    game::registry().clear_active_selection();
    const auto churned_authoritative_catalog = game::registry().registry_snapshot();
    ok &= require(churned_authoritative_catalog.generation
                != authoritative_catalog.generation
            && churned_authoritative_catalog.catalog_revision
                == authoritative_catalog.catalog_revision
            && churned_authoritative_catalog.storage
                == authoritative_catalog.storage,
        "state-generation churn changed authoritative catalog identity");

    auto reopened_publication = game::prepare_piano_list_catalog_republish(
        widget.data(), live);
    ok &= require(reopened_publication
            && game::commit_prepared_piano_list_catalog(*reopened_publication)
                == game::PianoListCatalogCommitResult::Committed,
        "successful exact-close restoration did not permit same-catalog republish");
    game::finalize_prepared_piano_list_catalog(*reopened_publication);
    ok &= require(game::list_catalog_selftest_owner_count() == 1
            && game::list_catalog_selftest_matches_registry(widget.data())
            && game::piano_list_catalog_owner_state(widget.data(), live)
                == game::PianoListOwnerState::Managed
            && game::registry().is_current(churned_authoritative_catalog)
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::None,
        "reopen did not republish the unchanged authoritative catalog exactly");

    owner_lock_held = false;
    release_owner_lock = false;
    std::thread active_owner_lock_holder([&] {
        game::list_catalog_selftest_lock_owners();
        {
            std::lock_guard lock(owner_contention_mutex);
            owner_lock_held = true;
        }
        owner_contention_changed.notify_all();
        {
            std::unique_lock lock(owner_contention_mutex);
            owner_contention_changed.wait(lock, [&] { return release_owner_lock; });
        }
        game::list_catalog_selftest_unlock_owners();
    });
    {
        std::unique_lock lock(owner_contention_mutex);
        owner_contention_changed.wait(lock, [&] { return owner_lock_held; });
    }
    const auto contended_owner_state
        = game::piano_list_catalog_owner_state(widget.data(), live);
    const auto contended_session_ownership
        = game::menu_list_ownership_from_open_facts(
            contended_owner_state == game::PianoListOwnerState::Managed,
            contended_owner_state == game::PianoListOwnerState::Transient);
    {
        std::lock_guard lock(owner_contention_mutex);
        release_owner_lock = true;
    }
    owner_contention_changed.notify_all();
    active_owner_lock_holder.join();
    const game::MenuSessionSnapshot second_close{8,
        game::MenuSessionPhase::Closing, nullptr, nullptr, widget.data(), live,
        contended_session_ownership};
    trace_count = 0;
    game::configure_list_catalog_selftest(&identity, {}, &event);
    ok &= require(contended_owner_state == game::PianoListOwnerState::Transient
            && contended_session_ownership
                == game::MenuListOwnership::ReclassifyOnExactClose
            && game::restore_owned_list_after_exact_cancel_close(
                second_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::Restored
            && trace_count == 3 && trace[0] == 2 && trace[1] == 3
            && trace[2] == 4
            && game::list_catalog_selftest_owner_count() == 0
            && field<layouts::PianoListEntry*>(widget,
                layouts::PianoMusicList::entries) == vanilla.data()
            && field<int32_t>(widget, layouts::PianoMusicList::count)
                == static_cast<int32_t>(vanilla.size())
            && field<int32_t>(widget, layouts::PianoMusicList::capacity)
                == static_cast<int32_t>(vanilla.size())
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Pending
            && !game::piano_list_catalog_terminal_failure(),
        "second exact-close generation did not restore and retire exactly");

    const auto repeated_authoritative_catalog = game::registry().registry_snapshot();
    auto repeated_publication = game::prepare_piano_list_catalog_republish(
        widget.data(), live);
    ok &= require(repeated_publication
            && game::commit_prepared_piano_list_catalog(*repeated_publication)
                == game::PianoListCatalogCommitResult::Committed,
        "second same-catalog republish did not commit");
    game::finalize_prepared_piano_list_catalog(*repeated_publication);
    const game::MenuSessionSnapshot third_close{9,
        game::MenuSessionPhase::Closing, nullptr, nullptr, widget.data(), live,
        game::MenuListOwnership::Managed};
    ok &= require(game::piano_list_catalog_owner_state(widget.data(), live)
                == game::PianoListOwnerState::Managed
            && game::restore_owned_list_after_exact_cancel_close(
                third_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::Restored
            && game::list_catalog_selftest_owner_count() == 0
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Pending
            && game::registry().is_current(repeated_authoritative_catalog)
            && !game::piano_list_catalog_terminal_failure(),
        "repeated restore/republish generation did not remain exact and reusable");

    base = game::registry().registry_snapshot();
    auto failure_catalog = std::make_shared<const game::SongRegistryStorage>(
        game::SongRegistryStorage{song(5), song(6), song(7), song(8)});
    auto failure_publication = game::prepare_piano_list_catalog(
        widget.data(), live, base, failure_catalog);
    ok &= require(failure_publication
            && game::commit_prepared_piano_list_catalog(*failure_publication)
                == game::PianoListCatalogCommitResult::Committed,
        "exact-close failure fixture publication failed");
    registry_commit = game::registry().begin_catalog_commit(base, failure_catalog);
    ok &= require(registry_commit != nullptr,
        "exact-close failure fixture registry preparation failed");
    game::registry().commit_catalog(*registry_commit);
    game::finalize_prepared_piano_list_catalog(*failure_publication);
    registry_commit.reset();
    ok &= require(game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::None
            && game::piano_list_catalog_owner_state(widget.data(), live)
                == game::PianoListOwnerState::Managed
            && !game::piano_list_catalog_terminal_failure(),
        "newer committed catalog did not supersede the candidate with an exact managed owner");
    auto* const redirected = field<layouts::PianoListEntry*>(
        widget, layouts::PianoMusicList::entries);
    const int32_t redirected_count = field<int32_t>(
        widget, layouts::PianoMusicList::count);
    const int32_t redirected_capacity = field<int32_t>(
        widget, layouts::PianoMusicList::capacity);

    std::vector<std::byte> uncertain_widget(0x900);
    std::array<layouts::PianoListEntry, layouts::PianoMusicList::vanilla_count>
        uncertain_vanilla{};
    expected_widget = uncertain_widget.data();
    field(uncertain_widget, layouts::PianoMusicList::entries, uncertain_vanilla.data());
    field(uncertain_widget, layouts::PianoMusicList::count,
        static_cast<int32_t>(uncertain_vanilla.size()));
    field(uncertain_widget, layouts::PianoMusicList::capacity,
        static_cast<int32_t>(uncertain_vanilla.size()));
    const auto uncertain_base = game::registry().registry_snapshot();
    auto uncertain_catalog = std::make_shared<const game::SongRegistryStorage>(
        game::SongRegistryStorage{song(5), song(6), song(7)});
    game::configure_list_catalog_selftest(&identity, {2, 1}, &event);
    auto uncertain = game::prepare_piano_list_catalog(
        uncertain_widget.data(), live, uncertain_base, uncertain_catalog);
    const auto retained_before = game::list_catalog_selftest_owner_count();
    ok &= require(uncertain && game::commit_prepared_piano_list_catalog(*uncertain)
            == game::PianoListCatalogCommitResult::RollbackUnverified
            && game::piano_list_catalog_terminal_failure()
            && game::list_catalog_selftest_owner_count() == retained_before + 1,
        "publication rollback uncertainty did not retain backing and fail closed");

    expected_widget = widget.data();
    const auto retained_after_uncertainty
        = game::list_catalog_selftest_owner_count();
    const game::MenuSessionSnapshot failure_close{9,
        game::MenuSessionPhase::Closing, nullptr, nullptr, widget.data(), live,
        game::MenuListOwnership::Managed};
    field(widget, layouts::PianoMusicList::count, redirected_count - 1);
    game::configure_list_catalog_selftest(&identity, {}, &event);
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                failure_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::TupleDrift
            && game::piano_list_catalog_terminal_failure()
            && game::list_catalog_selftest_owner_count()
                == retained_after_uncertainty,
        "exact-close tuple drift did not retain ownership and fail closed");
    field(widget, layouts::PianoMusicList::count, redirected_count);

    game::UObjectLiveHandle drifted = live;
    ++drifted.serial_number;
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                failure_close, widget.data(), drifted)
                == game::ExactCloseListRestoreResult::SessionIdentityMismatch
            && game::piano_list_catalog_terminal_failure()
            && game::list_catalog_selftest_owner_count()
                == retained_after_uncertainty,
        "exact-close bound identity drift did not retain ownership and fail closed");

    identity_valid = false;
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                failure_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::LiveIdentityInvalid
            && game::piano_list_catalog_terminal_failure()
            && game::list_catalog_selftest_owner_count()
                == retained_after_uncertainty,
        "exact-close live identity drift did not retain ownership and fail closed");
    identity_valid = true;

    trace_count = 0;
    game::configure_list_catalog_selftest(&identity, {1, 0}, &event);
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                failure_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::TransitionFailed
            && game::piano_list_catalog_terminal_failure()
            && game::list_catalog_selftest_owner_count()
                == retained_after_uncertainty
            && field<layouts::PianoListEntry*>(widget,
                layouts::PianoMusicList::entries) == redirected
            && field<int32_t>(widget, layouts::PianoMusicList::count)
                == redirected_count
            && field<int32_t>(widget, layouts::PianoMusicList::capacity)
                == redirected_capacity,
        "exact-close write failure did not retain the verified redirected owner");

    trace_count = 0;
    game::configure_list_catalog_selftest(&identity, {1, 1}, &event);
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                failure_close, widget.data(), live)
                == game::ExactCloseListRestoreResult::RollbackUnverified
            && game::piano_list_catalog_terminal_failure()
            && game::list_catalog_selftest_owner_count()
                == retained_after_uncertainty
            && field<layouts::PianoListEntry*>(widget,
                layouts::PianoMusicList::entries) == redirected
            && field<int32_t>(widget, layouts::PianoMusicList::count)
                == redirected_count
            && field<int32_t>(widget, layouts::PianoMusicList::capacity)
                == redirected_capacity
            && !game::prepare_piano_list_catalog(
                widget.data(), live, game::registry().registry_snapshot(), failure_catalog)
            && !game::restore_owned_list_patches()
            && game::list_catalog_selftest_owner_count()
                == retained_after_uncertainty,
        "exact-close rollback uncertainty did not retain backing and terminal admission");

    std::vector<std::byte> ownerless_nonempty_widget(0x900);
    expected_widget = ownerless_nonempty_widget.data();
    const game::MenuSessionSnapshot missing_managed_close{10,
        game::MenuSessionPhase::Closing, nullptr, nullptr,
        ownerless_nonempty_widget.data(), live,
        game::MenuListOwnership::Managed};
    const game::MenuSessionSnapshot unexplained_reclassified_close{11,
        game::MenuSessionPhase::Closing, nullptr, nullptr,
        ownerless_nonempty_widget.data(), live,
        game::MenuListOwnership::ReclassifyOnExactClose};
    ok &= require(game::restore_owned_list_after_exact_cancel_close(
                missing_managed_close, ownerless_nonempty_widget.data(), live)
                == game::ExactCloseListRestoreResult::NoOwnedPatch
            && game::restore_owned_list_after_exact_cancel_close(
                unexplained_reclassified_close,
                ownerless_nonempty_widget.data(), live)
                == game::ExactCloseListRestoreResult::CatalogIdentityMismatch
            && game::piano_list_catalog_terminal_failure(),
        "missing managed or unexplained reclassified owner did not remain terminal");
    ok &= require(game::piano_list_catalog_owner_state(
                ownerless_nonempty_widget.data(), live)
                == game::PianoListOwnerState::Uncertain
            && game::piano_list_catalog_terminal_failure(),
        "ownerless non-empty authoritative registry did not fail closed");

    if (!ok) return 1;
    std::cout << "list_catalog_transaction_selftest: ok\n";
    return 0;
}
