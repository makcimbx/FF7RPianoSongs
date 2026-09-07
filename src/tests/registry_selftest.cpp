#include "game/song_registry.h"
#include "game/completion_timing.h"
#include "game/duration.h"
#include "game/note_count.h"
#include "game/progress.h"
#include "game/scoreinfo_overlay.h"
#include "game/title.h"

#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <stdexcept>
#include <vector>

namespace {

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

ff7r::piano::game::SongDescriptor registry_fixture(
    const std::string& id, const int visible_index, const int difficulty)
{
    ff7r::piano::game::SongDescriptor song;
    song.id = id;
    song.title = std::wstring(id.begin(), id.end());
    song.visible_index = visible_index;
    ff7r::piano::game::SongDifficultyProfile profile;
    profile.title = song.title;
    profile.difficulty = difficulty;
    song.profiles.push_back(std::move(profile));
    return song;
}

ff7r::piano::game::SongDescriptor profile_fixture(
    const std::string& id, const int visible_index,
    const std::initializer_list<int> difficulties)
{
    ff7r::piano::game::SongDescriptor song;
    song.id = id;
    song.visible_index = visible_index;
    for (const int difficulty : difficulties) {
        ff7r::piano::game::SongDifficultyProfile profile;
        profile.difficulty = difficulty;
        song.profiles.push_back(profile);
    }
    return song;
}

int characterize_required_extension_outcomes()
{
    using namespace ff7r::piano::game;
    using Outcome = ChartExpandPreparationOutcome;
    // This is the production expansion owner's admission decision, not a
    // source-text oracle. Model strict tail-only FNAME_Find rejection and
    // both complete and unresolved journal/audio cancellation.
    for (bool required : {false, true}) {
        for (bool admitted : {false, true}) {
            for (bool cancelled : {false, true}) {
                int cancellations = 0;
                const auto outcome = chart_extension_admission_outcome(
                    Outcome::CustomCommitted, required, admitted, [&] {
                        ++cancellations;
                        return cancelled ? Outcome::NativePristine : Outcome::MutationUnresolved;
                    });
                const bool rejected = required && !admitted;
                if (cancellations != (rejected ? 1 : 0)
                    || outcome != (rejected ? (cancelled ? Outcome::NativePristine
                        : Outcome::MutationUnresolved) : Outcome::CustomCommitted)
                    || chart_expand_original_allowed(outcome) != (!rejected || cancelled))
                    return fail("required extension admission lost rollback/original gating");
            }
        }
    }

    // Exercise the actual registry outcome owner with no voicing binding.
    // Publication before failure models nested/immediate PlaySetup; attempted
    // publication after failure models delayed PlaySetup. Neither may retain
    // a playable prefix or republish the failed lease.
    for (bool publish_before_failure : {false, true}) {
        SongRegistry tested;
        tested.replace({registry_fixture("ExtendedWithoutVoicings", 7, 1)});
        tested.set_active_selection(7, 0);
        const auto selection = tested.selection_snapshot();
        const CustomContextToken token{selection.generation, 2, 3, 4};
        int wrapper_storage = 0;
        void* wrapper = &wrapper_storage;
        if (!tested.acquire_selection_guard(selection, token))
            return fail("required extension fixture admission");
        if (publish_before_failure && !tested.publish_playback_from_selection_guard(selection, token))
            return fail("successful extension publication control");
        if (tested.chart_update_admission(wrapper) != ChartUpdateAdmission::Stock)
            return fail("ordinary/no-voicing success gained a chart denial");
        auto stale = token;
        ++stale.lease_generation;
        if (tested.fail_chart_expansion(selection, stale, wrapper))
            return fail("stale extension failure consumed another lease");
        if (!tested.fail_chart_expansion(selection, token, wrapper)
            || tested.playback_snapshot().song
            || tested.selection_guard_matches(selection, token)
            || !tested.cleanup_lease().token.same_lease(token)
            || tested.chart_update_admission(wrapper) != ChartUpdateAdmission::Rejected
            || tested.publish_playback_from_selection_guard(selection, token)
            || tested.publish_playback(selection, token)
            || tested.acquire_selection_guard(selection, token))
            return fail("failed extension retained/published a shortened prefix");
        tested.invalidate_chart_admission(wrapper);
        if (tested.chart_update_admission(wrapper) != ChartUpdateAdmission::Rejected
            || !tested.fail_chart_expansion(selection, token, wrapper))
            return fail("reparse/repeated withdrawal erased failed-retained outcome");
        if (tested.retire_cleanup_lease(stale)
            || tested.chart_update_admission(wrapper) != ChartUpdateAdmission::Rejected
            || !tested.retire_cleanup_lease(token)
            || tested.chart_update_admission(wrapper) != ChartUpdateAdmission::Stock)
            return fail("extension denial was not owned by exact cleanup lease");
    }
    return 0;
}

int characterize_last_played_profile_policy()
{
    using namespace ff7r::piano::game;
    const SongDescriptor sparse = profile_fixture("SparsePolicy", 5, {1, 3, 6});
    if (resolve_last_played_profile_index(sparse, std::nullopt) != 0
        || resolve_last_played_profile_index(sparse,
            parse_last_played_difficulty(L"bad")) != 0
        || resolve_last_played_profile_index(sparse,
            parse_last_played_difficulty(L"4")) != 0
        || resolve_last_played_profile_index(sparse,
            parse_last_played_difficulty(L"3")) != 1
        || resolve_last_played_profile_index(sparse,
            parse_last_played_difficulty(L"6")) != 2
        || parse_last_played_difficulty(L" 3")
        || parse_last_played_difficulty(L"3x")
        || parse_last_played_difficulty(L"99999999999999999999")) {
        return fail("last-played difficulty parsing or sparse exact mapping changed");
    }
    const SongDescriptor single = profile_fixture("SinglePolicy", 6, {9});
    if (resolve_last_played_profile_index(single,
            parse_last_played_difficulty(L"9")) != 0
        || resolve_last_played_profile_index(single,
            parse_last_played_difficulty(L"2")) != 0) {
        return fail("single-profile preference did not remain index zero");
    }
    SongDifficultyProfile detached = single.profiles.front();
    if (resolve_score_profile_index(single, nullptr) != 0
        || resolve_score_profile_index(single, &single.profiles.front()) != 0
        || resolve_score_profile_index(single, &detached) != -1
        || resolve_score_profile_index(sparse, nullptr) != -1) {
        return fail("null or exact score-profile identity policy changed");
    }

    std::vector<int> writes;
    const bool committed = persist_score_then_last_played_preference(
        true,
        [&] { writes.push_back(1); return true; },
        [&] { writes.push_back(2); return true; });
    if (!committed || writes != std::vector<int>{1, 2})
        return fail("score and last-played marker did not commit in order");
    writes.clear();
    if (persist_score_then_last_played_preference(
            true,
            [&] { writes.push_back(1); return false; },
            [&] { writes.push_back(2); return true; })
        || writes != std::vector<int>{1}) {
        return fail("failed score write published a last-played marker");
    }
    writes.clear();
    if (persist_score_then_last_played_preference(
            true,
            [&] { writes.push_back(1); return true; },
            [&] { writes.push_back(2); return false; })
        || writes != std::vector<int>{1, 2}) {
        return fail("failed last-played marker claimed committed persistence");
    }
    writes.clear();
    if (!persist_score_then_last_played_preference(
            false,
            [&] { writes.push_back(1); return true; },
            [&] { writes.push_back(2); return false; })
        || writes != std::vector<int>{1}) {
        return fail("disabled preference did not commit only the score write");
    }
    return 0;
}

int characterize_focus_bookmark()
{
    using namespace ff7r::piano::game;
    SongRegistry r;
    auto song = profile_fixture("focus", 7, {0, 3, 2147483647});
    r.replace({song});
    r.set_active_selection(7, 0);
    if (r.prepare_last_played_focus()) return fail("hover created a focus bookmark");
    if (!r.cycle_active_profile(1)) return fail("focus fixture could not select sparse label");
    auto selected = r.selection_snapshot();
    CustomContextToken token{selected.generation, 1, 2, 3};
    if (!r.acquire_selection_guard(selected, token)
        || !r.publish_playback_from_selection_guard(selected, token)) return fail("focus playback publication");
    if (r.prepare_last_played_focus()) return fail("focus preference mutated live playback");
    if (!r.revoke_playback(token)) return fail("focus fixture revoke");
    const auto retained_generation = r.selection_snapshot().generation;
    const auto retained_focus = r.prepare_last_played_focus();
    if (!retained_focus || retained_focus.profile->difficulty != 3
        || r.selection_snapshot().generation != retained_generation || !r.cleanup_lease())
        return fail("unchanged focus waited for cleanup or mutated its retained authority");
    if (!r.retire_cleanup_lease(token)) return fail("focus fixture cleanup");
    if (!r.cycle_active_profile(1)) return fail("focus fixture hover change");
    auto target = r.prepare_last_played_focus();
    if (!target || target.profile->difficulty != 3) return fail("hover replaced accepted playback bookmark");
    r.set_active_selection(-1, -1);
    const auto adopt = [&](std::vector<SongDescriptor> songs) {
        auto prepared = r.begin_catalog_commit(r.registry_snapshot(),
            std::make_shared<const SongRegistryStorage>(std::move(songs)));
        if (!prepared) return false;
        r.commit_catalog(*prepared);
        return true;
    };
    auto reordered = profile_fixture("focus", 9, {2147483647, 0, 3});
    if (!adopt({profile_fixture("new-first", 7, {1}), reordered})) return fail("focus catalog adoption");
    auto preferred = r.snapshot_for_visible_index(9);
    if (!preferred.profile || preferred.profile->difficulty != 3 || preferred.profile_index != 2)
        return fail("profile preference used obsolete catalog index");
    target = r.prepare_last_played_focus();
    if (!target || target.visible_index != 9 || target.profile_index != 2)
        return fail("focus bookmark did not resolve stable song/profile identity");
    auto removed = profile_fixture("focus", 8, {0, 6});
    removed.default_profile_index = 1;
    if (!adopt({removed})) return fail("focus removed-profile adoption");
    target = r.prepare_last_played_focus();
    if (!target || target.profile->difficulty != 6) return fail("removed profile did not use current default");
    r.clear_last_played_focus();
    if (r.prepare_last_played_focus()) return fail("stock activation did not clear bookmark");
    r.set_active_selection(8, 0);
    selected = r.selection_snapshot(); token = {selected.generation, 2, 4, 5};
    if (!r.publish_playback(selected, token) || !r.revoke_playback(token)
        || !r.retire_cleanup_lease(token)) return fail("direct publication bookmark fixture");
    r.set_active_selection(-1, -1);
    if (!adopt({profile_fixture("unrelated", 6, {1})}) || r.prepare_last_played_focus())
        return fail("removed song did not fall back to native focus");
    return 0;
}

int characterize_profile_initialization()
{
    using namespace ff7r::piano::game;
    SongRegistry tested;
    tested.replace({profile_fixture("A", 5, {1, 3, 6}),
        profile_fixture("B", 6, {2, 8})});

    SelectionSnapshot a = tested.snapshot_for_visible_index(5);
    const auto before = tested.registry_snapshot();
    SelectionSnapshot initialized;
    if (!tested.initialize_profile_if_absent(a, 2, initialized)
        || initialized.profile_index != 2 || initialized.profile->difficulty != 6
        || initialized.generation != before.generation + 1
        || initialized.storage != before.storage) {
        return fail("first profile initializer did not insert a fresh owning snapshot");
    }
    SelectionSnapshot queried;
    if (tested.initialized_profile_state(initialized, queried)
            != InitializedProfileState::Present
        || queried.profile_index != 2 || queried.storage != initialized.storage) {
        return fail("initialized-state query did not return exact owning state");
    }
    const auto after_first = tested.registry_snapshot();
    if (!tested.initialize_profile_if_absent(initialized, 0, initialized)
        || initialized.profile_index != 2
        || tested.registry_snapshot().generation != after_first.generation) {
        return fail("second profile initializer overwrote or churned the first value");
    }
    SelectionSnapshot b = tested.snapshot_for_visible_index(6);
    if (!tested.initialize_profile_if_absent(b, 0, b)
        || b.profile_index != 0 || b.profile->difficulty != 2) {
        return fail("explicit profile-zero fallback was not inserted");
    }
    const auto fallback_generation = tested.registry_snapshot().generation;
    if (!tested.initialize_profile_if_absent(b, 1, b)
        || b.profile_index != 0
        || tested.registry_snapshot().generation != fallback_generation
        || tested.snapshot_for_visible_index(5).profile_index != 2) {
        return fail("fallback sealing or independent-song selection changed");
    }

    SongRegistry manual;
    manual.replace({profile_fixture("Manual", 7, {1, 4, 9})});
    manual.set_active_selection(7, 0);
    const SelectionSnapshot before_manual = manual.selection_snapshot();
    if (!manual.cycle_active_profile(1))
        return fail("manual-before-init fixture did not advance");
    SelectionSnapshot manual_selection = manual.selection_snapshot();
    if (!manual.initialize_profile_if_absent(
            before_manual, 2, initialized)
        || initialized.profile_index != 1) {
        return fail("manual race after initialized-state query was not preserved");
    }
    if (!manual.cycle_active_profile(1)
        || manual.selection_snapshot().profile_index != 2) {
        return fail("manual selection after initialization did not win");
    }

    SongRegistry stale;
    stale.replace({profile_fixture("Stale", 8, {1, 5})});
    SelectionSnapshot stale_selection = stale.snapshot_for_visible_index(8);
    stale.replace({profile_fixture("Stale", 8, {1, 5})});
    if (stale.initialize_profile_if_absent(stale_selection, 1, initialized)
        || stale.initialize_profile_if_absent(
            stale.snapshot_for_visible_index(8), 2, initialized)) {
        return fail("stale storage/song or invalid profile index was accepted");
    }

    SongRegistry locked;
    locked.replace({profile_fixture("Locked", 9, {1, 5})});
    locked.set_active_selection(9, 0);
    if (!locked.freeze_active_profile()
        || locked.initialize_profile_if_absent(
            locked.selection_snapshot(), 1, initialized)) {
        return fail("profile lock allowed absent-profile initialization");
    }

    SongRegistry guarded;
    guarded.replace({profile_fixture("Guarded", 10, {1, 5})});
    guarded.set_active_selection(10, 0);
    SelectionSnapshot guarded_selection = guarded.selection_snapshot();
    CustomContextToken guarded_token{};
    guarded_token.registry_generation = guarded_selection.generation;
    guarded_token.route_generation = 1;
    guarded_token.lease_generation = 1;
    guarded_token.song_key = 1;
    if (!guarded.acquire_selection_guard(guarded_selection, guarded_token)
        || guarded.initialize_profile_if_absent(
            guarded_selection, 1, initialized)) {
        return fail("selection guard allowed absent-profile initialization");
    }

    SongRegistry playing;
    playing.replace({profile_fixture("Playing", 11, {1, 5})});
    playing.set_active_selection(11, 0);
    SelectionSnapshot playing_selection = playing.selection_snapshot();
    CustomContextToken playing_token{};
    playing_token.registry_generation = playing_selection.generation;
    playing_token.route_generation = 1;
    playing_token.lease_generation = 1;
    playing_token.song_key = 2;
    int persistence_calls = 0;
    if (!playing.publish_playback(playing_selection, playing_token)
        || playing.initialized_profile_state(playing_selection, queried)
            != InitializedProfileState::Deferred
        || queried.profile_index != 0
        || queried.generation != playing_selection.generation
        || playing.initialize_profile_if_absent(
            playing_selection, 0, initialized)
        || playing.initialize_profile_if_absent(
            playing_selection, 1, initialized)
        || commit_progress_if_playback_token(playing,
            CustomContextToken{}, [&] { ++persistence_calls; return true; })
        || persistence_calls != 0
        || !commit_progress_if_playback_token(playing, playing_token, [&] {
            ++persistence_calls;
            return persist_score_then_last_played_preference(
                true, [] { return true; }, [] { return true; });
        }) || persistence_calls != 1) {
        return fail("playback ownership or exact-token persistence policy changed");
    }
    if (!playing.retire_cleanup_lease(playing_token))
        return fail("deferred profile initialization fixture did not retire ownership");
    playing_selection = playing.selection_snapshot();
    if (playing.initialized_profile_state(playing_selection, queried)
            != InitializedProfileState::Absent
        || !playing.initialize_profile_if_absent(playing_selection, 0, initialized)
        || playing.initialized_profile_state(initialized, queried)
            != InitializedProfileState::Present) {
        return fail("deferred no-op profile initialization was not eventually sealed");
    }

    tested.replace({profile_fixture("A", 5, {1, 3, 6}),
        profile_fixture("B", 6, {2, 8})});
    a = tested.snapshot_for_visible_index(5);
    if (a.profile_index != 0
        || !tested.initialize_profile_if_absent(a, 1, initialized)
        || initialized.profile_index != 1) {
        return fail("catalog replacement did not reset profile initialization lifetime");
    }
    return 0;
}

int characterize_registry_lifetime()
{
    using namespace ff7r::piano::game;

    SongRegistry registry_under_test;
    const RegistrySnapshot empty = registry_under_test.registry_snapshot();
    if (!empty || !empty.songs().empty() || empty.generation == 0
        || empty.catalog_revision == 0
        || !registry_under_test.is_current(empty)) {
        return fail("initial registry publication was not an owned empty generation");
    }

    registry_under_test.replace({registry_fixture("First", 5, 2)});
    registry_under_test.set_active_selection(5, 1);
    RegistrySnapshot first = registry_under_test.registry_snapshot();
    SelectionSnapshot selected = registry_under_test.selection_snapshot();
    const SongDescriptor* first_song = first.by_id("First");
    RegistrySnapshot first_again = registry_under_test.registry_snapshot();
    std::weak_ptr<const SongRegistryStorage> first_generation = first.storage;
    if (!first_song || first.by_visible_index(5) != first_song || !selected
        || selected.storage != first.storage || selected.song != first_song
        || selected.profile != &first_song->profiles[0]
        || selected.generation != first.generation
        || first_again.generation != first.generation || first_again.storage != first.storage
        || !registry_under_test.is_current(first)) {
        return fail("published registry/song/profile/storage identity was not coherent");
    }
    const std::uint64_t first_catalog_revision = first.catalog_revision;
    registry_under_test.clear_active_selection();
    registry_under_test.set_active_selection(5, 1);
    RegistrySnapshot state_churned = registry_under_test.registry_snapshot();
    if (state_churned.generation <= first.generation
        || state_churned.catalog_revision != first_catalog_revision
        || state_churned.storage != first.storage) {
        return fail("selection state generation changed immutable catalog identity");
    }
    first = state_churned;
    first_again = state_churned;
    selected = registry_under_test.selection_snapshot();
    state_churned = {};
    if (!registry_snapshot_owns_selection(first, selected)) {
        return fail("owning catalog did not recognize its exact selection");
    }
    RegistrySnapshot wrong_catalog = first;
    wrong_catalog.storage = std::make_shared<const SongRegistryStorage>();
    SelectionSnapshot wrong_profile = selected;
    ++wrong_profile.profile_index;
    if (registry_snapshot_owns_selection(wrong_catalog, selected)
        || registry_snapshot_owns_selection(first, wrong_profile)) {
        return fail("catalog ownership accepted storage or profile identity drift");
    }

    registry_under_test.replace({registry_fixture("Second", 6, 4)});
    RegistrySnapshot second = registry_under_test.registry_snapshot();
    std::weak_ptr<const SongRegistryStorage> second_generation = second.storage;
    CustomContextToken stale_token{};
    stale_token.registry_generation = selected.generation;
    stale_token.route_generation = 1;
    stale_token.lease_generation = 1;
    stale_token.song_key = 1;
    if (registry_under_test.is_current(first) || !registry_under_test.is_current(second)
        || second.generation != first.generation + 1
        || second.catalog_revision != first.catalog_revision + 1
        || second.storage == first.storage
        || first.by_id("First") != first_song || first_song->profiles[0].difficulty != 2
        || !registry_snapshot_owns_selection(first, selected)
        || registry_snapshot_owns_selection(second, selected)
        || descriptor_title_text(*selected.song, selected.profile) != L"First"
        || selected.profile->note_count != 0
        || !selected.profile->chart_notes.empty()
        || registry_under_test.acquire_selection_guard(selected, stale_token)
        || registry_under_test.registry_snapshot().generation != second.generation
        || registry_under_test.selection_snapshot()) {
        return fail("replacement did not reject stale identity while retaining its storage");
    }
    if (first_generation.expired()) {
        return fail("owning snapshot did not retain its replaced generation");
    }
    first = {};
    first_again = {};
    wrong_profile = {};
    if (first_generation.expired()) {
        return fail("selection snapshot did not retain its replaced generation");
    }
    selected = {};
    if (!first_generation.expired()) {
        return fail("old generation was not reclaimed after its final owning snapshot");
    }

    registry_under_test.replace({});
    RegistrySnapshot cleared = registry_under_test.registry_snapshot();
    if (!cleared.songs().empty() || cleared.generation != second.generation + 1
        || registry_under_test.is_current(second) || second.by_id("Second") == nullptr
        || second_generation.expired()) {
        return fail("clear replacement did not switch exactly or preserve its owning snapshot");
    }
    second = {};
    if (!second_generation.expired()) {
        return fail("replaced second generation was not reclaimed after snapshot release");
    }
    cleared = {};

    SongRegistry state_overflow;
    state_overflow.selftest_seed_generations(UINT64_MAX, 7);
    const RegistrySnapshot state_overflow_before = state_overflow.registry_snapshot();
    state_overflow.replace({registry_fixture("RejectedStateOverflow", 30, 1)});
    if (!state_overflow.is_current(state_overflow_before)
        || state_overflow.begin_catalog_commit(state_overflow_before,
            std::make_shared<const SongRegistryStorage>())) {
        return fail("state-generation overflow did not reject catalog replacement");
    }
    SongRegistry catalog_overflow;
    catalog_overflow.selftest_seed_generations(7, UINT64_MAX);
    const RegistrySnapshot catalog_overflow_before = catalog_overflow.registry_snapshot();
    catalog_overflow.replace({registry_fixture("RejectedCatalogOverflow", 31, 1)});
    if (!catalog_overflow.is_current(catalog_overflow_before)
        || catalog_overflow.begin_catalog_commit(catalog_overflow_before,
            std::make_shared<const SongRegistryStorage>())) {
        return fail("catalog-revision overflow did not reject catalog replacement");
    }

    for (int generation = 0; generation < 32; ++generation) {
        RegistrySnapshot retained = registry_under_test.registry_snapshot();
        std::weak_ptr<const SongRegistryStorage> retired = retained.storage;
        const std::uint64_t previous_generation = retained.generation;
        registry_under_test.replace({registry_fixture(
            "Repeated" + std::to_string(generation), 20 + generation, generation + 1)});
        const RegistrySnapshot current = registry_under_test.registry_snapshot();
        if (current.generation != previous_generation + 1
            || current.storage == retained.storage || retired.expired()) {
            return fail("repeated replacement did not publish one exact new generation");
        }
        retained = {};
        if (!retired.expired()) {
            return fail("repeated replacement retained obsolete storage without an owner");
        }
    }

    struct Handoff {
        std::mutex mutex;
        std::condition_variable changed;
        int phase = 0;
        bool reader_ok = false;
    } handoff;
    SongRegistry concurrent_registry;
    concurrent_registry.replace({registry_fixture("Before", 7, 1)});

    std::thread reader([&] {
        const RegistrySnapshot before = concurrent_registry.registry_snapshot();
        {
            std::lock_guard<std::mutex> lock(handoff.mutex);
            handoff.phase = 1;
        }
        handoff.changed.notify_all();
        {
            std::unique_lock<std::mutex> lock(handoff.mutex);
            handoff.changed.wait(lock, [&] { return handoff.phase == 2; });
        }
        const RegistrySnapshot after = concurrent_registry.registry_snapshot();
        const SongDescriptor* retained = before.by_id("Before");
        handoff.reader_ok = retained && retained->profiles[0].difficulty == 1
            && before.storage != after.storage
            && before.generation < after.generation
            && after.by_id("After") != nullptr
            && !concurrent_registry.is_current(before)
            && concurrent_registry.is_current(after);
    });
    {
        std::unique_lock<std::mutex> lock(handoff.mutex);
        handoff.changed.wait(lock, [&] { return handoff.phase == 1; });
    }
    concurrent_registry.replace({registry_fixture("After", 8, 3)});
    {
        std::lock_guard<std::mutex> lock(handoff.mutex);
        handoff.phase = 2;
    }
    handoff.changed.notify_all();
    reader.join();
    if (!handoff.reader_ok) {
        return fail("concurrent snapshot reader did not retain one coherent generation");
    }
    return 0;
}

int characterize_progress_lookup_display_caller_policy()
{
    using namespace ff7r::piano::game;
    // A build that does not declare a display call site renders it as the
    // generated zero sentinel. The sentinel must not turn the allowlist into a
    // wildcard for callers the module range check could not resolve.
    constexpr std::uintptr_t with_sentinel[] = {0x03c12c9c, 0, 0x03c41677};
    if (progress_lookup_display_caller_allowed(0, with_sentinel)) {
        return fail("an unresolved caller matched the absent-entry sentinel");
    }
    if (!progress_lookup_display_caller_allowed(0x03c12c9c, with_sentinel)
        || !progress_lookup_display_caller_allowed(0x03c41677, with_sentinel)) {
        return fail("a declared display call site was rejected");
    }
    if (progress_lookup_display_caller_allowed(0x0398a151, with_sentinel)) {
        return fail("a call site absent from this build was accepted");
    }
    constexpr std::uintptr_t complete[] = {0x039670bc, 0x0398a151, 0x0399a393};
    if (progress_lookup_display_caller_allowed(0, complete)) {
        return fail("an unresolved caller matched a sentinel-free allowlist");
    }
    if (!progress_lookup_display_caller_allowed(0x0398a151, complete)) {
        return fail("a declared display call site was rejected");
    }
    static_assert(!progress_lookup_display_caller_allowed(
        0, std::span<const std::uintptr_t>{}));
    return 0;
}

int characterize_score_diagnostic_policy()
{
    using namespace ff7r::piano::game;
    ScoreCalculationDiagnosticFacts facts{};
    if (classify_score_calculation_diagnostic(facts)
        != ScoreCalculationDiagnosticClassification::CallerUnavailable) {
        return fail("score diagnostic classifier did not reject an unstable caller");
    }
    facts.caller_rva = 0x1234;
    if (classify_score_calculation_diagnostic(facts)
        != ScoreCalculationDiagnosticClassification::CountersUnavailable) {
        return fail("score diagnostic classifier did not require copied counters");
    }
    facts.counters_copied = true;
    if (classify_score_calculation_diagnostic(facts)
        != ScoreCalculationDiagnosticClassification::NativeResultInvalid) {
        return fail("score diagnostic classifier did not require a valid native result");
    }
    facts.native_result_valid = true;
    if (classify_score_calculation_diagnostic(facts)
        != ScoreCalculationDiagnosticClassification::PlaybackRelationMismatch) {
        return fail("score diagnostic classifier did not require exact playback relation");
    }
    facts.playback_relation_exact = true;
    if (classify_score_calculation_diagnostic(facts)
        != ScoreCalculationDiagnosticClassification::PersistenceNotCommitted) {
        return fail("score diagnostic classifier did not require committed persistence");
    }
    facts.persistence_committed = true;
    if (classify_score_calculation_diagnostic(facts)
        != ScoreCalculationDiagnosticClassification::Eligible) {
        return fail("complete score diagnostic facts were rejected");
    }

    CustomContextToken token{};
    token.registry_generation = 1;
    token.route_generation = 2;
    token.lease_generation = 3;
    token.song_key = 4;
    token.request_handle = 5;
    ScoreCalculationDiagnosticBudget budget;
    if (!budget.consume(token) || !budget.consume(token) || budget.consume(token)) {
        return fail("score diagnostic budget did not stop after two exact-token records");
    }
    auto next = token;
    ++next.route_generation;
    if (!budget.consume(next)) {
        return fail("score diagnostic budget did not open for a different full token");
    }
    if (budget.consume(token)) {
        return fail("score diagnostic budget reopened a retired exact token");
    }

    ScoreCalculationDiagnosticBudget ordered_budget;
    ScoreCalculationDiagnosticFacts rejected{};
    if (consume_score_calculation_diagnostic_if_eligible(
            rejected, token, ordered_budget)) {
        return fail("ineligible score observation consumed an emission slot");
    }
    rejected.caller_rva = 0x1234;
    if (consume_score_calculation_diagnostic_if_eligible(
            rejected, token, ordered_budget)) {
        return fail("partially eligible score observation consumed an emission slot");
    }
    ScoreCalculationDiagnosticFacts eligible{
        0x1234, true, true, true, true};
    if (!consume_score_calculation_diagnostic_if_eligible(
            eligible, token, ordered_budget)
        || !consume_score_calculation_diagnostic_if_eligible(
            eligible, token, ordered_budget)
        || consume_score_calculation_diagnostic_if_eligible(
            eligible, token, ordered_budget)) {
        return fail("eligible score records did not receive exactly two emission slots");
    }

    SongDescriptor song;
    SongDifficultyProfile profile;
    auto storage = std::make_shared<int>(1);
    PlaybackSnapshot before{};
    before.generation = 1;
    before.storage = storage;
    before.song = &song;
    before.profile = &profile;
    before.profile_index = 0;
    before.token = token;
    auto after = before;
    if (!score_calculation_playback_relation_exact(before, after)) {
        return fail("exact score diagnostic playback relation was rejected");
    }
    ++after.token.request_handle;
    if (score_calculation_playback_relation_exact(before, after)) {
        return fail("score diagnostic playback token drift was accepted");
    }
    return 0;
}

int characterize_try_playback_snapshot_contention()
{
    using namespace ff7r::piano::game;
    SongRegistry tested;
    std::mutex mutex;
    std::condition_variable condition;
    bool locked = false;
    bool release = false;
    std::thread holder([&] {
        tested.selftest_lock_state();
        {
            std::lock_guard guard(mutex);
            locked = true;
        }
        condition.notify_one();
        {
            std::unique_lock wait_lock(mutex);
            condition.wait(wait_lock, [&] { return release; });
        }
        tested.selftest_unlock_state();
    });
    {
        std::unique_lock wait_lock(mutex);
        condition.wait(wait_lock, [&] { return locked; });
    }
    PlaybackSnapshot snapshot;
    snapshot.generation = 99;
    const bool acquired = tested.try_playback_snapshot(snapshot);
    {
        std::lock_guard guard(mutex);
        release = true;
    }
    condition.notify_one();
    holder.join();
    if (acquired || snapshot.generation != 0 || snapshot.storage) {
        return fail("try playback snapshot retained stale output under contention");
    }
    if (!tested.try_playback_snapshot(snapshot)) {
        return fail("try playback snapshot rejected uncontended state");
    }
    return 0;
}

} // namespace

int main()
{
    if (const int result = characterize_required_extension_outcomes()) return result;
    if (const int result = characterize_focus_bookmark()) return result;
    {
        using namespace ff7r::piano::game;
        SongRegistry presentation_registry;
        auto song = profile_fixture("IconPolicy", 7, {0, 1, 3, 6, 7, 2147483647});
        song.difficulty = 2; // Root/default is not selected-profile authority.
        for (auto& profile : song.profiles) {
            profile.title = L"IconPolicy [Lv." + std::to_wstring(profile.difficulty) + L"]";
            profile.note_count = 17;
        }
        presentation_registry.replace({song});
        presentation_registry.set_active_selection(7, 0);
        for (int label : {0, 1, 3, 6, 7, 2147483647}) {
            const auto selected = presentation_registry.selection_snapshot();
            if (!selected || selected.profile->difficulty != label)
                return fail("icon policy selected-profile fixture");
            ScopedSongRenderContext scope(selected);
            const auto menu = presentation_registry.render_snapshot();
            if (list_difficulty_icon_count(menu.profile->difficulty) != (label > 6 ? 6 : label)
                || menu.profile->title != L"IconPolicy [Lv." + std::to_wstring(label) + L"]"
                || menu.profile->difficulty != label || menu.profile->note_count != 17
                || native_scoreinfo_difficulty(label) != (label == 0 ? 1 : (label > 6 ? 6 : label)))
                return fail("list icon cap changed exact title/profile or result policy");
            if (!scoreinfo_menu_detail_overlay_candidate(ScoreInfoResultCatalogRole::ListItem,
                    true, true, true, true)
                || scoreinfo_menu_detail_overlay_candidate(ScoreInfoResultCatalogRole::ListItem,
                    true, true, true, false)
                || scoreinfo_result_caller_for_catalog_role(ScoreInfoResultCatalogRole::ListItem, true)
                    != ScoreInfoResultCaller::Unavailable)
                return fail("list icon overlay lost private scope or gained result authority");
            if (label != 2147483647 && !presentation_registry.cycle_active_profile(1))
                return fail("icon policy profile refresh failed");
        }
    }
    using ff7r::piano::game::SongChartNote;
    using ff7r::piano::game::SongDescriptor;
    using ff7r::piano::game::SongDifficultyProfile;

    if (const int lifetime_result = characterize_registry_lifetime(); lifetime_result != 0) {
        return lifetime_result;
    }
    if (const int preference_result = characterize_last_played_profile_policy();
        preference_result != 0) {
        return preference_result;
    }
    if (const int initialization_result = characterize_profile_initialization();
        initialization_result != 0) {
        return initialization_result;
    }
    if (const int display_caller_result
            = characterize_progress_lookup_display_caller_policy();
        display_caller_result != 0) {
        return display_caller_result;
    }
    if (const int diagnostic_result = characterize_score_diagnostic_policy();
        diagnostic_result != 0) {
        return diagnostic_result;
    }
    if (const int try_result = characterize_try_playback_snapshot_contention();
        try_result != 0) {
        return try_result;
    }

    SongDescriptor timed_song;
    timed_song.duration_seconds = 163.004f;
    SongDifficultyProfile timed_profile;
    timed_profile.chart_notes.push_back({"154_50"});
    const float audio_target = ff7r::piano::game::completion_target_seconds(&timed_song, &timed_profile, 155.4f);
    if (std::fabs(audio_target - 163.254f) > 0.001f) {
        return fail("completion target did not preserve full audio duration");
    }
    timed_song.duration_seconds = 120.0f;
    const float chart_target = ff7r::piano::game::completion_target_seconds(&timed_song, &timed_profile, 155.4f);
    if (std::fabs(chart_target - 155.65f) > 0.001f) {
        return fail("completion target did not preserve native chart duration");
    }

    std::vector<SongDescriptor> songs;
    for (int i = 0; i < 3; ++i) {
        SongDescriptor song;
        song.id = "Song" + std::to_string(i);
        song.title = L"Song " + std::to_wstring(i);
        song.visible_index = 5 + i;
        song.base_slot = 0;
        song.note_count = 1;
        SongChartNote note;
        note.time_str = "0_00";
        note.monotone_id = i == 2 ? "" : "Cn4";
        note.chord_id = i == 2 ? "pca_C" : "";
        song.chart_notes.push_back(note);
        if (i == 2) {
            for (int difficulty = 1; difficulty <= 6; ++difficulty) {
                SongDifficultyProfile profile;
                profile.title = L"Song 2 [Lv." + std::to_wstring(difficulty) + L"]";
                profile.difficulty = difficulty;
                profile.note_count = difficulty;
                profile.chart_notes.assign(static_cast<size_t>(difficulty), note);
                song.profiles.push_back(std::move(profile));
            }
            song.default_profile_index = 0;
        }
        songs.push_back(song);
    }

    auto& song_registry = ff7r::piano::game::registry();
    song_registry.replace(std::move(songs));
    const auto current_selection = [&song_registry] {
        return song_registry.selection_snapshot();
    };
    const auto current_difficulty = [&current_selection] {
        const auto snapshot = current_selection();
        return snapshot.profile ? snapshot.profile->difficulty : -1;
    };
    const auto current_progress_section = [&current_selection] {
        const auto snapshot = current_selection();
        return snapshot ? ff7r::piano::game::progress_section_name(
            *snapshot.song, snapshot.profile) : std::wstring{};
    };

    const ff7r::piano::game::RegistrySnapshot song_catalog
        = song_registry.registry_snapshot();
    const SongDescriptor* third_song = song_catalog.by_visible_index(7);
    if (!third_song) {
        return fail("third custom song is missing");
    }
    if (third_song->chart_notes.size() != 1) {
        return fail("chart rows were not preserved");
    }
    if (third_song->chart_notes[0].chord_id != "pca_C") {
        return fail("chart chord was not preserved");
    }
    if (!third_song->chart_notes[0].monotone_id.empty()) {
        return fail("chord-only chart row gained a monotone");
    }

    song_registry.set_active_selection(7, 0);
    const auto initial_snapshot = current_selection();
    if (!initial_snapshot.song || initial_snapshot.song->id != "Song2") {
        return fail("active song handoff failed");
    }
    if (song_registry.active_visible_index() != 7 || song_registry.active_base_slot() != 0) {
        return fail("active selection state mismatch");
    }
    if (!initial_snapshot.profile || initial_snapshot.profile->difficulty != 1) {
        return fail("default difficulty profile mismatch");
    }
    if (!initial_snapshot.song || !initial_snapshot.profile ||
        initial_snapshot.song->id != "Song2" || initial_snapshot.profile->difficulty != 1 ||
        initial_snapshot.profile_index != 0 || initial_snapshot.generation == 0) {
        return fail("locked active snapshot did not preserve song/profile identity");
    }
    if (song_registry.cycle_active_profile(-1) || current_difficulty() != 1) {
        return fail("minimum difficulty profile exceeded its lower bound");
    }
    if (!song_registry.cycle_active_profile(1) || current_difficulty() != 2) {
        return fail("difficulty profile did not increase");
    }
    const auto cycled_snapshot = current_selection();
    if (cycled_snapshot.generation <= initial_snapshot.generation ||
        cycled_snapshot.song != initial_snapshot.song || !cycled_snapshot.profile ||
        cycled_snapshot.profile->difficulty != 2 || cycled_snapshot.profile_index != 1) {
        return fail("locked active snapshot generation did not track profile selection");
    }
    if (!song_registry.freeze_active_profile()) {
        return fail("difficulty profile did not freeze");
    }
    if (song_registry.cycle_active_profile(1) || current_difficulty() != 2) {
        return fail("frozen difficulty profile changed");
    }
    song_registry.clear_frozen_profile();
    if (!song_registry.cycle_active_profile(1) || current_difficulty() != 3) {
        return fail("difficulty profile did not change after thaw");
    }
    for (int difficulty = 4; difficulty <= 6; ++difficulty) {
        if (!song_registry.cycle_active_profile(1) ||
            current_difficulty() != difficulty) {
            return fail("arbitrary profile count did not reach Lv." + std::to_string(difficulty));
        }
    }
    if (song_registry.cycle_active_profile(1) || current_difficulty() != 6) {
        return fail("maximum generated profile exceeded its upper bound");
    }
    if (!song_registry.freeze_active_profile()) {
        return fail("Lv.6 selection did not freeze its title and chart");
    }
    const auto frozen_six = current_selection();
    if (!frozen_six.profile || frozen_six.profile->difficulty != 6 ||
        frozen_six.profile->title != L"Song 2 [Lv.6]" ||
        frozen_six.profile->chart_notes.size() != 6) {
        return fail("Lv.6 selection did not freeze its title and chart");
    }
    if (ff7r::piano::game::native_scoreinfo_difficulty(7) != 6 ||
        ff7r::piano::game::native_scoreinfo_difficulty(6) != 6 ||
        ff7r::piano::game::native_scoreinfo_difficulty(1) != 1) {
        return fail("native ScoreInfo difficulty isolation failed");
    }
    if (ff7r::piano::game::custom_scoreinfo_bpm(0.0f) != 0.0f ||
        ff7r::piano::game::custom_scoreinfo_bpm(87.5f) != 87.5f) {
        return fail("custom ScoreInfo did not preserve vanilla BPM");
    }
    song_registry.clear_frozen_profile();
    if (!song_registry.cycle_active_profile(-1) ||
        current_progress_section() != L"Song2.difficulty.5") {
        return fail("Lv.5 score section was not selected independently");
    }
    if (!song_registry.cycle_active_profile(1) ||
        current_progress_section() != L"Song2.difficulty.6") {
        return fail("Lv.6 score section was not separated from Lv.5");
    }
    const ff7r::piano::game::SelectionSnapshot exact_before = song_registry.selection_snapshot();
    ff7r::piano::game::SelectionSnapshot exact_changed;
    if (!song_registry.cycle_active_profile_exact(exact_before, -1, exact_changed)
        || exact_changed.generation == exact_before.generation
        || exact_changed.storage != exact_before.storage
        || song_registry.cycle_active_profile_exact(exact_before, -1, exact_changed)) {
        return fail("exact profile mutation did not own and validate its prestate");
    }
    const SongDescriptor* retained_song = exact_before.song;
    {
        ff7r::piano::game::ScopedSongRenderContext render(exact_before);
        SongDescriptor replacement = *exact_before.song;
        replacement.id = "Replacement";
        song_registry.replace({replacement});
        if (!ff7r::piano::game::song_render_context_matches(retained_song)
            || song_registry.selection_matches(exact_before)) {
            return fail("owning render context did not retain storage or stale selection stayed current");
        }
    }
    if (ff7r::piano::game::song_render_context_matches(retained_song))
        return fail("owning render context did not unwind");
    song_registry.clear_active_selection();
    if (current_selection().song) {
        return fail("active song was not cleared");
    }

    for (int profile_count = 1; profile_count <= 6; ++profile_count) {
        SongDescriptor song;
        song.id = "Variable" + std::to_string(profile_count);
        song.title = L"Variable";
        song.visible_index = 7;
        SongChartNote note;
        note.time_str = "0_00";
        note.monotone_id = "Cn4";
        for (int difficulty = 1; difficulty <= profile_count; ++difficulty) {
            SongDifficultyProfile profile;
            profile.title = L"Variable [Lv." + std::to_wstring(difficulty) + L"]";
            profile.difficulty = difficulty;
            profile.note_count = difficulty;
            profile.chart_notes.assign(static_cast<std::size_t>(difficulty), note);
            song.profiles.push_back(std::move(profile));
        }
        song.default_profile_index = 0;
        song_registry.replace({song});
        song_registry.set_active_selection(7, 0);
        if (current_difficulty() != 1 ||
            song_registry.cycle_active_profile(-1)) {
            return fail("variable profile fixture violated its lower bound at count " +
                std::to_string(profile_count));
        }
        for (int difficulty = 2; difficulty <= profile_count; ++difficulty) {
            if (!song_registry.cycle_active_profile(1) ||
                current_difficulty() != difficulty) {
                return fail("variable profile fixture did not reach Lv." + std::to_string(difficulty));
            }
        }
        const std::wstring expected_section = profile_count == 1 ?
            L"Variable" + std::to_wstring(profile_count) :
            L"Variable" + std::to_wstring(profile_count) + L".difficulty." +
                std::to_wstring(profile_count);
        if (song_registry.cycle_active_profile(1) || !song_registry.freeze_active_profile()) {
            return fail("variable profile fixture violated selection/freeze/chart/score behavior at count " +
                std::to_string(profile_count));
        }
        const auto variable_selection = current_selection();
        if (!variable_selection.profile ||
            variable_selection.profile->difficulty != profile_count ||
            variable_selection.profile->chart_notes.size() != static_cast<std::size_t>(profile_count) ||
            current_progress_section() != expected_section) {
            return fail("variable profile fixture violated selection/freeze/chart/score behavior at count " +
                std::to_string(profile_count));
        }
        song_registry.clear_frozen_profile();
        song_registry.clear_active_selection();
    }

    SongDescriptor sparse_song;
    sparse_song.id = "Sparse";
    sparse_song.title = L"Sparse";
    sparse_song.visible_index = 7;
    SongChartNote sparse_note;
    sparse_note.time_str = "0_00";
    sparse_note.monotone_id = "Cn4";
    for (const int difficulty : {1, 3, 6}) {
        SongDifficultyProfile profile;
        profile.title = L"Sparse [Lv." + std::to_wstring(difficulty) + L"]";
        profile.difficulty = difficulty;
        profile.note_count = difficulty;
        profile.chart_notes.assign(static_cast<std::size_t>(difficulty), sparse_note);
        sparse_song.profiles.push_back(std::move(profile));
    }
    song_registry.replace({sparse_song});
    song_registry.set_active_selection(7, 0);
    if (current_difficulty() != 1 || !song_registry.cycle_active_profile(1)) {
        return fail("non-contiguous Lv.1/Lv.3/Lv.6 selection did not preserve its actual label");
    }
    const auto sparse_three = current_selection();
    if (!sparse_three.profile || sparse_three.profile->difficulty != 3 ||
        sparse_three.profile->title != L"Sparse [Lv.3]" ||
        sparse_three.profile->chart_notes.size() != 3 ||
        current_progress_section() != L"Sparse.difficulty.3" ||
        !song_registry.freeze_active_profile() || song_registry.cycle_active_profile(1)) {
        return fail("non-contiguous Lv.1/Lv.3/Lv.6 selection did not preserve its actual label");
    }
    song_registry.clear_frozen_profile();
    if (!song_registry.cycle_active_profile(1)) {
        return fail("non-contiguous profile cycling compacted or relabeled Lv.6");
    }
    const auto sparse_six = current_selection();
    if (!sparse_six.profile || sparse_six.profile->difficulty != 6 ||
        sparse_six.profile->title != L"Sparse [Lv.6]" ||
        sparse_six.profile->chart_notes.size() != 6 ||
        current_progress_section() != L"Sparse.difficulty.6") {
        return fail("non-contiguous profile cycling compacted or relabeled Lv.6");
    }
    song_registry.clear_active_selection();

    SongDescriptor owned_song = registry_fixture("Owned", 9, 5);
    owned_song.profiles[0].title = L"Owned profile";
    owned_song.profiles[0].note_count = 23;
    owned_song.profiles[0].chart_notes.push_back({"1_25", "Cn4", "", 3, 0, 0, 0});
    song_registry.replace({owned_song});
    song_registry.set_active_selection(9, 0);
    const ff7r::piano::game::RegistrySnapshot owned_catalog
        = song_registry.registry_snapshot();
    const ff7r::piano::game::SelectionSnapshot owned_selection
        = song_registry.selection_snapshot();
    song_registry.replace({registry_fixture("New", 10, 1)});
    if (!registry_snapshot_owns_selection(owned_catalog, owned_selection)
        || descriptor_title_text(*owned_selection.song, owned_selection.profile)
            != L"Owned profile"
        || owned_selection.profile->note_count != 23
        || owned_selection.profile->chart_notes.size() != 1
        || owned_selection.profile->chart_notes[0].monotone_id != "Cn4") {
        return fail("owning chart/title/note catalog lifetime did not survive replacement");
    }

    SongDescriptor menu_song = registry_fixture("Menu", 12, 2);
    menu_song.duration_seconds = 111.0f;
    menu_song.profiles[0].note_count = 11;
    menu_song.profiles[0].score_thresholds = {0, 110, 220, 330};
    SongDifficultyProfile second_profile = menu_song.profiles[0];
    second_profile.difficulty = 5;
    second_profile.note_count = 22;
    second_profile.score_thresholds = {0, 220, 440, 660};
    menu_song.profiles.push_back(second_profile);
    SongDescriptor playback_song = registry_fixture("Playback", 13, 4);
    playback_song.duration_seconds = 222.0f;
    playback_song.profiles[0].note_count = 33;
    song_registry.replace({menu_song, playback_song});
    song_registry.set_active_selection(12, 0);

    ff7r::piano::game::PlaybackSnapshot playback_fallback;
    const auto playback_catalog = song_registry.registry_snapshot();
    playback_fallback.storage = playback_catalog.storage;
    playback_fallback.song = playback_catalog.by_visible_index(13);
    playback_fallback.profile = &playback_fallback.song->profiles[0];
    {
        ff7r::piano::game::ScopedSongRenderContext menu(
            song_registry.selection_snapshot());
        const auto scoped = song_registry.render_snapshot();
        if (ff7r::piano::game::menu_or_playback_duration_or_original(
                scoped, playback_fallback, 9.0f) != 111.0f
            || ff7r::piano::game::menu_or_playback_note_count_value(
                scoped, playback_fallback, 44, 512) != 11
            || ff7r::piano::game::scoreinfo_thresholds_for_descriptor(
                *scoped.song, scoped.profile) != std::array<int32_t, 4>{0, 110, 220, 330}) {
            return fail("scoped menu metadata did not override unrelated playback");
        }
    }
    if (!song_registry.cycle_active_profile(1)) {
        return fail("menu metadata profile fixture did not advance");
    }
    {
        ff7r::piano::game::ScopedSongRenderContext menu(
            song_registry.selection_snapshot());
        const auto scoped = song_registry.render_snapshot();
        if (ff7r::piano::game::menu_or_playback_note_count_value(
                scoped, playback_fallback, 44, 512) != 22
            || ff7r::piano::game::scoreinfo_thresholds_for_descriptor(
                *scoped.song, scoped.profile) != std::array<int32_t, 4>{0, 220, 440, 660}) {
            return fail("scoped menu metadata leaked the prior profile");
        }
    }
    const ff7r::piano::game::RenderSnapshot no_menu{};
    if (ff7r::piano::game::menu_or_playback_duration_or_original(
            no_menu, playback_fallback, 9.0f) != 222.0f
        || ff7r::piano::game::menu_or_playback_note_count_value(
            no_menu, playback_fallback, 44, 512) != 33
        || ff7r::piano::game::menu_or_playback_duration_or_original(
            no_menu, {}, 9.0f) != 9.0f
        || ff7r::piano::game::menu_or_playback_note_count_value(
            no_menu, {}, 0, 512) != 0) {
        return fail("no-scope metadata did not preserve playback/vanilla fallback");
    }

    // Full selected-profile menu metadata must not require a playback lease or
    // inherit the root profile, native prefix, physical events, or captured count.
    SongDescriptor extended_menu = menu_song;
    extended_menu.profiles[0].note_count = 513;
    extended_menu.profiles[0].required_action_count = 513;
    extended_menu.profiles[0].source_row_count = 513;
    extended_menu.profiles[0].native_prefix_event_count = 512;
    extended_menu.profiles[0].native_event_count = 513;
    extended_menu.profiles[1].note_count = 549;
    extended_menu.profiles[1].required_action_count = 549;
    extended_menu.profiles[1].source_row_count = 600;
    extended_menu.profiles[1].native_prefix_event_count = 560;
    extended_menu.profiles[1].native_event_count = 658;
    song_registry.replace({extended_menu});
    song_registry.set_active_selection(12, 0);
    {
        ff7r::piano::game::ScopedSongRenderContext menu(song_registry.selection_snapshot());
        if (ff7r::piano::game::menu_descriptor_note_count(song_registry.render_snapshot()) != 513)
            return fail("extended menu required playback publication or truncated the full profile");
    }
    if (!song_registry.cycle_active_profile(1))
        return fail("extended menu selected-profile fixture did not advance");
    {
        ff7r::piano::game::ScopedSongRenderContext menu(song_registry.selection_snapshot());
        if (ff7r::piano::game::menu_descriptor_note_count(song_registry.render_snapshot()) != 549)
            return fail("extended menu counted events/followers or used the default profile");
    }
    for (const auto count : {1006, 8192, 8193, 0, -1}) {
        extended_menu.profiles[0].note_count = count;
        song_registry.replace({extended_menu});
        song_registry.set_active_selection(12, 0);
        ff7r::piano::game::ScopedSongRenderContext menu(song_registry.selection_snapshot());
        const int expected = count == 1006 || count == 8192 ? count : 0;
        if (ff7r::piano::game::menu_descriptor_note_count(song_registry.render_snapshot()) != expected)
            return fail("complete menu action count lost its native hard bound");
    }
    if (ff7r::piano::game::menu_descriptor_note_count({}) != 0)
        return fail("unscoped/stock menu acquired descriptor count authority");

    std::weak_ptr<const int> exception_owner;
    try {
        ff7r::piano::game::ScopedSongRenderContext menu(
            song_registry.selection_snapshot());
        auto owner = std::make_shared<const int>(1);
        exception_owner = owner;
        if (!ff7r::piano::game::retain_song_render_context_owner(owner))
            return fail("menu scope rejected its owned row");
        owner.reset();
        throw std::runtime_error("scope unwind");
    } catch (const std::runtime_error&) {
    }
    if (!exception_owner.expired() || song_registry.render_snapshot().song)
        return fail("menu metadata owner survived exception unwind");
    std::weak_ptr<const int> early_owner;
    [&] {
        ff7r::piano::game::ScopedSongRenderContext menu(
            song_registry.selection_snapshot());
        auto owner = std::make_shared<const int>(2);
        early_owner = owner;
        (void)ff7r::piano::game::retain_song_render_context_owner(owner);
        return;
    }();
    if (!early_owner.expired() || song_registry.render_snapshot().song)
        return fail("menu metadata owner survived early return");
    if (!ff7r::piano::game::scoreinfo_menu_detail_overlay_candidate(
            ff7r::piano::game::ScoreInfoResultCatalogRole::MenuDetail,
            true, true, true, true)
        || ff7r::piano::game::scoreinfo_menu_detail_overlay_candidate(
            ff7r::piano::game::ScoreInfoResultCatalogRole::Thresholds,
            true, true, true, true)
        || ff7r::piano::game::scoreinfo_result_caller_for_catalog_role(
            ff7r::piano::game::ScoreInfoResultCatalogRole::MenuDetail, true)
            != ff7r::piano::game::ScoreInfoResultCaller::Unavailable) {
        return fail("menu detail escaped exact caller or entered result authority");
    }
    using namespace ff7r::piano::game;
    if (!scoreinfo_menu_detail_overlay_candidate(ScoreInfoResultCatalogRole::ListItem,
            true, true, true, true)
        || scoreinfo_menu_detail_overlay_candidate(ScoreInfoResultCatalogRole::ListItem,
            true, true, true, false)
        || scoreinfo_menu_detail_overlay_candidate(ScoreInfoResultCatalogRole::ListItem,
            true, false, true, true)
        || scoreinfo_result_caller_for_catalog_role(ScoreInfoResultCatalogRole::ListItem, true)
            != ScoreInfoResultCaller::Unavailable)
        return fail("list-item private row escaped owning scope or entered RESULT policy");

    std::cout << "registry_selftest ok\n";
    return 0;
}
