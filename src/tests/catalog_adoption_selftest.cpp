#include "game/catalog_adoption.h"
#include "game/audio_production_wiring.h"
#include "game/audio_sead.h"
#include "game/menu_session_authority.h"
#include "game/module_hooks.h"
#include "game/native_array_publication.h"
#include "startup/startup_cache_progress.h"

#include <array>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace game = ff7r::piano::game;
namespace core = ff7r::piano::core;

namespace {
enum class Event : uint8_t { Tuple, Owner, Audio, Registry };
struct RepublishCandidate {
    uint64_t catalog_revision = 0;
    std::shared_ptr<const game::SongRegistryStorage> storage;
    explicit operator bool() const noexcept { return static_cast<bool>(storage); }
};
struct Fixture {
    game::NativeArrayTuple tuple{0x1000, 5, 5};
    game::UObjectLiveHandle identity{7, 70};
    std::shared_ptr<std::vector<uint64_t>> owner;
    std::shared_ptr<const game::SongRegistryStorage> owner_catalog;
    uint64_t owner_registry_generation = 0;
    uint64_t owner_catalog_revision = 0;
    RepublishCandidate republish_candidate{};
    std::array<std::shared_ptr<std::vector<uint64_t>>, 8> retained{};
    size_t retained_count = 0;
    game::NativeArrayTupleFaultInjection fault{};
    bool stale_widget = false;
    bool binding_unreadable = false;
    bool replace_binding_on_revalidate = false;
    size_t binding_calls = 0;
    bool menu_blocked = false;
    bool menu_contended = false;
    bool coordinator_blocked = false;
    bool coordinator_contended = false;
    bool selection_blocked = false;
    bool selection_contended = false;
    bool list_contended = false;
    bool audio_idle = true;
    bool audio_base_stale = false;
    bool registry_base_stale = false;
    bool terminal = false;
    uint64_t audio_catalog_revision = 1;
    size_t prepared_catalogs = 0;
    std::shared_ptr<const game::SongRegistryStorage> audio_catalog =
        std::make_shared<const game::SongRegistryStorage>();
    std::shared_ptr<const game::PreparedAudioPrefix> audio_prefix;
    std::array<Event, 8> trace{};
    size_t trace_count = 0;

    void event(Event value) noexcept { trace[trace_count++] = value; }
} f;
std::mutex prepare_mutex;
std::condition_variable prepare_changed;

bool require(bool value, const char* message)
{
    if (!value) std::cerr << "catalog_adoption_selftest: " << message << '\n';
    return value;
}

// Songs arrive from the offline pipeline without a row: adoption resolves it
// against the live list, so the number only identifies the fixture.
game::SongDescriptor song(int number)
{
    game::SongDescriptor value;
    value.id = "catalog-" + std::to_string(number);
    value.base_slot = 0;
    value.profiles.emplace_back();
    value.default_profile_index = 0;
    return value;
}

game::CatalogAdoptionResult attempt(void* widget = &f)
{
    f.binding_calls = 0;
    auto callback = game::non_audio_hook_gate().try_enter();
    if (!callback) return game::CatalogAdoptionResult::Blocked;
    const auto result = game::try_adopt_pending_catalog_before_menu_open(
        &f, widget, f.identity, callback);
    game::observe_catalog_adoption_result(result);
    return result;
}

bool exact_restore_fixture()
{
    if (!f.owner) return false;
    f.tuple = {0x1000, 5, 5};
    f.owner.reset();
    f.owner_catalog.reset();
    f.owner_registry_generation = 0;
    f.owner_catalog_revision = 0;
    const auto authoritative = game::registry().registry_snapshot();
    f.republish_candidate = {
        authoritative.catalog_revision, authoritative.storage};
    return static_cast<bool>(f.republish_candidate);
}

void readiness_changed(void* context, const game::CatalogReadinessEvent event,
    const std::size_t song_count) noexcept
{
    auto& progress = *static_cast<ff7r::piano::startup::StartupCacheProgress*>(context);
    switch (event) {
    case game::CatalogReadinessEvent::Prepared: progress.catalog_prepared(song_count); break;
    case game::CatalogReadinessEvent::AdoptionDeferred:
        progress.catalog_adoption_deferred();
        break;
    case game::CatalogReadinessEvent::Adopted: progress.catalog_adopted(song_count); break;
    }
}

// Sidecar prefixes are keyed by song identity, never by row, so a prefix
// prepared before adoption still matches once rows are resolved.
struct PrefixToken {
    std::shared_ptr<const PrefixToken> prior;
    std::string id;
};

std::shared_ptr<const game::PreparedAudioPrefix> prefix_after(
    const std::shared_ptr<const game::PreparedAudioPrefix>& prior, int number)
{
    auto token = std::make_shared<PrefixToken>();
    if (prior) {
        token->prior = std::shared_ptr<const PrefixToken>(prior,
            reinterpret_cast<const PrefixToken*>(prior.get()));
    }
    token->id = "catalog-" + std::to_string(number);
    const auto* alias = reinterpret_cast<const game::PreparedAudioPrefix*>(token.get());
    return {std::move(token), alias};
}

const PrefixToken* prefix_token(
    const std::shared_ptr<const game::PreparedAudioPrefix>& prefix)
{
    return reinterpret_cast<const PrefixToken*>(prefix.get());
}

bool offer(std::vector<game::SongDescriptor> descriptors,
    const std::shared_ptr<const game::PreparedAudioPrefix>& prefix)
{
    return game::offer_prepared_pending_catalog(
        game::prepare_pending_catalog(std::move(descriptors), prefix));
}
} // namespace

namespace ff7r::piano::game {

class PreparedAudioCatalogCommit { public: PreparedAudioCatalog* prepared = nullptr; };
class PreparedPianoListCatalog {
public:
    NativeArrayTuple source{}, target{};
    NativeArrayTupleAccess access;
    NativeArrayTupleFaultInjection fault{};
    std::shared_ptr<std::vector<uint64_t>> replacement;
    std::shared_ptr<const SongRegistryStorage> catalog;
    uint64_t registry_generation = 0;
    uint64_t catalog_revision = 0;
    bool republish = false;
};

core::HookCallbackGate& non_audio_hook_gate()
{
    static core::HookCallbackGate gate;
    return gate;
}

std::shared_ptr<PreparedPianoListCatalog> prepare_piano_list_catalog_republish(
    void* widget, const UObjectLiveHandle& identity) noexcept
{
    if (f.list_contended || !f.republish_candidate) return {};
    const RegistrySnapshot candidate_catalog{
        0, f.republish_candidate.catalog_revision,
        f.republish_candidate.storage};
    auto out = prepare_piano_list_catalog(widget, identity,
        candidate_catalog, candidate_catalog.storage);
    if (!out) return {};
    out->replacement = std::make_shared<std::vector<uint64_t>>(
        static_cast<size_t>(out->source.count)
            + candidate_catalog.storage->size(), 0);
    out->target = {reinterpret_cast<uintptr_t>(out->replacement->data()),
        static_cast<int32_t>(out->replacement->size()),
        static_cast<int32_t>(out->replacement->capacity())};
    out->catalog_revision = candidate_catalog.catalog_revision;
    out->registry_generation = candidate_catalog.generation;
    out->republish = true;
    return out;
}

bool prepare_audio_catalog_from_prefix(
    std::shared_ptr<const SongRegistryStorage> storage,
    std::shared_ptr<const PreparedAudioPrefix> prefix,
    PreparedAudioCatalog& prepared) noexcept
{
    if (!storage || !prefix) return false;
    const PrefixToken* node = prefix_token(prefix);
    for (std::size_t count = storage->size(); count != 0; --count) {
        if (!node || node->id != (*storage)[count - 1].id) return false;
        node = node->prior.get();
    }
    if (node) return false;
    prepared.storage = std::move(storage);
    prepared.prefix = std::move(prefix);
    ++f.prepared_catalogs;
    return true;
}

std::shared_ptr<PreparedAudioCatalogCommit> begin_prepared_audio_catalog_commit(
    PreparedAudioCatalog& prepared, uint64_t expected,
    const AudioProductionCatalogScope& scope) noexcept
{
    if (!scope || !f.audio_idle || f.audio_base_stale
        || expected != f.audio_catalog_revision)
        return {};
    auto out = std::make_shared<PreparedAudioCatalogCommit>();
    out->prepared = &prepared;
    return out;
}

void commit_prepared_audio_catalog(PreparedAudioCatalogCommit& commit,
    uint64_t, uint64_t catalog_revision) noexcept
{
    f.audio_catalog = commit.prepared->storage;
    f.audio_prefix.swap(commit.prepared->prefix);
    f.audio_catalog_revision = catalog_revision;
    f.event(Event::Audio);
}
void finalize_prepared_audio_catalog_commit(PreparedAudioCatalogCommit& commit) noexcept
{
    commit.prepared->prefix.reset();
    commit.prepared->storage.reset();
}

bool piano_list_first_custom_row(void* widget, const UObjectLiveHandle& identity,
    int32_t& first_custom_row) noexcept
{
    if (f.stale_widget || widget != &f
        || identity.internal_index != f.identity.internal_index
        || identity.serial_number != f.identity.serial_number) return false;
    first_custom_row = f.owner ? 5 : f.tuple.count;
    return true;
}

std::shared_ptr<PreparedPianoListCatalog> prepare_piano_list_catalog(
    void* widget, const UObjectLiveHandle& identity, const RegistrySnapshot& expected,
    std::shared_ptr<const SongRegistryStorage> replacement) noexcept
{
    if (f.terminal || f.stale_widget || widget != &f
        || identity.internal_index != f.identity.internal_index
        || identity.serial_number != f.identity.serial_number) return {};
    auto out = std::make_shared<PreparedPianoListCatalog>();
    out->source = f.tuple;
    const int32_t native_count = f.owner ? 5 : f.tuple.count;
    out->replacement = std::make_shared<std::vector<uint64_t>>(
        static_cast<size_t>(native_count) + replacement->size(), 0);
    out->target = {reinterpret_cast<uintptr_t>(out->replacement->data()),
        static_cast<int32_t>(out->replacement->size()),
        static_cast<int32_t>(out->replacement->capacity())};
    out->catalog = std::move(replacement);
    out->registry_generation = expected.generation + 1;
    out->catalog_revision = expected.catalog_revision + 1;
    out->fault = f.fault;
    out->access = {
        [](NativeArrayTuple& value) { value = f.tuple; return true; },
        [](uintptr_t value) { f.tuple.pointer = value; return true; },
        [](int32_t value) { f.tuple.count = value; return true; },
        [](int32_t value) { f.tuple.capacity = value; return true; },
        {},
    };
    if (f.registry_base_stale) registry().replace({song(99)});
    return out;
}

PianoListCatalogCommitResult commit_prepared_piano_list_catalog(
    PreparedPianoListCatalog& prepared) noexcept
{
    const auto result = publish_native_array_tuple(prepared.source,
        prepared.source.capacity, prepared.target, prepared.target.capacity,
        prepared.access, prepared.fault);
    if (!result.committed) {
        if (!result.rollback_verified) {
            f.retained[f.retained_count++] = prepared.replacement;
            f.terminal = true;
            return PianoListCatalogCommitResult::RollbackUnverified;
        }
        return PianoListCatalogCommitResult::Rejected;
    }
    f.event(Event::Tuple);
    if (f.owner) f.retained[f.retained_count++] = f.owner;
    f.owner = prepared.replacement;
    f.owner_catalog = prepared.catalog;
    f.owner_registry_generation = prepared.registry_generation;
    f.owner_catalog_revision = prepared.catalog_revision;
    f.event(Event::Owner);
    return PianoListCatalogCommitResult::Committed;
}
void finalize_prepared_piano_list_catalog(PreparedPianoListCatalog&) noexcept
{
    f.republish_candidate = {};
}
bool piano_list_catalog_terminal_failure() noexcept { return f.terminal; }
PianoListRepublishState piano_list_catalog_republish_state() noexcept
{
    return f.republish_candidate
        ? PianoListRepublishState::Pending : PianoListRepublishState::None;
}
bool try_selection_runtime_idle_for_catalog_adoption() noexcept
{
    return !f.selection_blocked && !f.selection_contended;
}

bool resolve_piano_menu_widget_binding(void*, void*& widget,
    UObjectLiveHandle& identity) noexcept
{
    ++f.binding_calls;
    if (f.binding_unreadable) { widget = nullptr; identity = {}; return false; }
    widget = f.replace_binding_on_revalidate && f.binding_calls == 2
        ? reinterpret_cast<void*>(0x2000) : static_cast<void*>(&f);
    identity = f.identity;
    return true;
}

MenuSessionAuthority& menu_session_authority() noexcept
{
    static MenuSessionAuthority authority;
    return authority;
}
MenuSessionSnapshot MenuSessionAuthority::capture(bool) const noexcept
{
    return f.menu_blocked ? MenuSessionSnapshot{1, MenuSessionPhase::Ready} : MenuSessionSnapshot{};
}
bool MenuSessionAuthority::try_idle_for_catalog_adoption() const noexcept
{
    return !f.menu_blocked && !f.menu_contended;
}

ProfileListCoordinator& profile_list_coordinator()
{
    static ProfileListCoordinator coordinator;
    return coordinator;
}
ProfileListCoordinatorState ProfileListCoordinator::state() const noexcept
{
    return f.coordinator_blocked ? ProfileListCoordinatorState::ListSetup
                                 : ProfileListCoordinatorState::Ready;
}
bool ProfileListCoordinator::try_ready_for_catalog_adoption() const noexcept
{
    return !f.coordinator_blocked && !f.coordinator_contended;
}

void audio_production_outer_callback_entered() noexcept {}
void audio_production_outer_callback_exited(bool) noexcept {}
void catalog_adoption_selftest_trace_registry_published() noexcept
{
    f.event(Event::Registry);
}
} // namespace ff7r::piano::game

int main()
{
    bool ok = true;
    ff7r::piano::startup::StartupCacheProgress readiness;
    readiness.begin(3);
    game::configure_catalog_readiness_observer({&readiness, readiness_changed});
    const auto initial_readiness = readiness.snapshot();
    ok &= require(initial_readiness.active_song_count == 0
            && initial_readiness.available_on_reopen_song_count == 0
            && !initial_readiness.catalog_update_pending,
        "initial empty active catalog readiness was not explicit");
    game::non_audio_hook_gate().open();
    game::audio_production_open_callback_admission();

    const auto old_registry = game::registry().registry_snapshot();
    auto prefix1 = prefix_after({}, 5);
    auto prefix2 = prefix_after(prefix1, 6);
    auto prefix3 = prefix_after(prefix2, 7);
    std::weak_ptr<const PrefixToken> weak1(std::shared_ptr<const PrefixToken>(prefix1,
        reinterpret_cast<const PrefixToken*>(prefix1.get())));
    std::weak_ptr<const PrefixToken> weak2(std::shared_ptr<const PrefixToken>(prefix2,
        reinterpret_cast<const PrefixToken*>(prefix2.get())));
    std::vector<game::SongDescriptor> oversized;
    for (int number = 0; number <= 128; ++number) oversized.push_back(song(number));
    ok &= require(!offer(std::move(oversized), prefix3),
        "catalog exceeding the proven piano-list bound became pending");
    auto first_pending = game::prepare_pending_catalog({song(5)}, prefix1);
    ok &= require(first_pending
            && game::offer_prepared_pending_catalog(first_pending),
        "initial pending offer failed");
    const auto first_readiness = readiness.snapshot();
    ok &= require(first_readiness.available_on_reopen_song_count == 1
            && first_readiness.active_song_count == 0
            && first_readiness.catalog_update_pending,
        "first accepted pending count was not projected");
    auto superseding_pending = game::prepare_pending_catalog(
        {song(5), song(6)}, prefix2);
    ok &= require(superseding_pending
            && game::offer_prepared_pending_catalog(superseding_pending),
        "pending prefix supersession failed");
    ok &= require(readiness.snapshot().available_on_reopen_song_count == 2,
        "successive accepted pending count did not advance");
    first_pending.reset();
    ok &= require(!weak1.expired(),
        "pending supersession released a node retained by the newer prefix");
    first_pending = game::prepare_pending_catalog({song(5)}, prefix1);
    ok &= require(first_pending
            && game::offer_prepared_pending_catalog(first_pending),
        "initial pending prefix restoration failed");
    superseding_pending.reset();
    readiness.begin(3);
    ok &= require(game::offer_prepared_pending_catalog(first_pending)
            && readiness.snapshot().available_on_reopen_song_count == 1,
        "focused initial-prefix readiness reset failed");

    bool pending_locked = false;
    bool release_pending = false;
    std::thread pending_holder([&] {
        game::catalog_adoption_selftest_lock_pending();
        {
            std::lock_guard lock(prepare_mutex);
            pending_locked = true;
        }
        prepare_changed.notify_all();
        {
            std::unique_lock lock(prepare_mutex);
            prepare_changed.wait(lock, [&] { return release_pending; });
        }
        game::catalog_adoption_selftest_unlock_pending();
    });
    {
        std::unique_lock lock(prepare_mutex);
        prepare_changed.wait(lock, [&] { return pending_locked; });
    }
    const auto tuple_before_contention = f.tuple;
    const auto registry_before_contention = game::registry().registry_snapshot();
    const auto audio_before_contention = f.audio_catalog;
    int initial_empty_forwards = 0;
    for (int open = 0; open < 2; ++open) {
        const auto result = attempt();
        if (game::classify_menu_open_catalog_result(true, result)
            == game::MenuOpenCatalogDisposition::ForwardOriginal)
            ++initial_empty_forwards;
        ok &= require(result == game::CatalogAdoptionResult::Blocked,
            "pending publication contention did not remain recoverably blocked");
    }
    ok &= require(initial_empty_forwards == 2
            && registry_before_contention.storage
            && registry_before_contention.storage->empty()
            && game::registry().is_current(registry_before_contention)
            && f.audio_catalog == audio_before_contention
            && f.tuple == tuple_before_contention && !f.owner,
        "blocked initial-empty opens did not forward unchanged vanilla/empty state");
    const auto initial_blocked_readiness = readiness.snapshot();
    ok &= require(initial_blocked_readiness.catalog_update_pending
            && initial_blocked_readiness.adoption_deferred
            && initial_blocked_readiness.active_song_count == 0
            && initial_blocked_readiness.available_on_reopen_song_count == 1,
        "blocked usable opens did not retain pending readiness");
    {
        std::lock_guard lock(prepare_mutex);
        release_pending = true;
    }
    prepare_changed.notify_all();
    pending_holder.join();

    game::catalog_adoption_selftest_lock_pending();
    bool raced_offer_started = false;
    bool raced_offer_accepted = true;
    std::thread raced_offer([&] {
        {
            std::lock_guard lock(prepare_mutex);
            raced_offer_started = true;
        }
        prepare_changed.notify_all();
        raced_offer_accepted = offer({song(5), song(6)}, prefix2);
    });
    {
        std::unique_lock lock(prepare_mutex);
        prepare_changed.wait(lock, [&] { return raced_offer_started; });
    }
    f.terminal = true;
    game::catalog_adoption_selftest_unlock_pending();
    raced_offer.join();
    ok &= require(!raced_offer_accepted,
        "pending offer did not recheck terminal rollback under publication lock");
    f.terminal = false;
    f.stale_widget = true;
    ok &= require(attempt() == game::CatalogAdoptionResult::Blocked
            && f.tuple == game::NativeArrayTuple{0x1000, 5, 5}
            && game::registry().is_current(old_registry) && !f.owner,
        "stale widget mutated coordinator state");
    f.stale_widget = false;

    f.binding_unreadable = true;
    ok &= require(attempt() == game::CatalogAdoptionResult::Blocked
            && f.tuple == game::NativeArrayTuple{0x1000, 5, 5} && !f.owner,
        "unreadable embedded-list binding performed native writes");
    f.binding_unreadable = false;
    f.replace_binding_on_revalidate = true;
    ok &= require(attempt() == game::CatalogAdoptionResult::Blocked
            && f.binding_calls == 2
            && f.tuple == game::NativeArrayTuple{0x1000, 5, 5} && !f.owner,
        "replaced embedded-list binding performed native writes");
    f.replace_binding_on_revalidate = false;

    for (int write = 1; write <= 3; ++write) {
        f.fault = {write, 0};
        const auto registry_before = game::registry().registry_snapshot();
        const auto audio_before = f.audio_catalog;
        const auto tuple_before = f.tuple;
        ok &= require(attempt() == game::CatalogAdoptionResult::Blocked
                && f.tuple == tuple_before && game::registry().is_current(registry_before)
                && f.audio_catalog == audio_before && !f.owner,
            "verified tuple rollback changed catalog ownership");
    }
    f.fault = {};

    const std::array<bool*, 8> blockers{
        &f.menu_blocked, &f.coordinator_blocked, &f.selection_blocked,
        &f.audio_idle, &f.audio_base_stale, &f.menu_contended,
        &f.coordinator_contended, &f.selection_contended,
    };
    for (size_t index = 0; index < blockers.size(); ++index) {
        *blockers[index] = index == 3 ? false : true;
        const auto before = f.tuple;
        ok &= require(attempt() == game::CatalogAdoptionResult::Blocked && f.tuple == before,
            "coordinator blocker performed native writes");
        *blockers[index] = index == 3 ? true : false;
    }

    bool registry_locked = false;
    bool release_registry = false;
    std::thread registry_holder([&] {
        game::registry().selftest_lock_catalog_state();
        {
            std::lock_guard lock(prepare_mutex);
            registry_locked = true;
        }
        prepare_changed.notify_all();
        std::unique_lock lock(prepare_mutex);
        prepare_changed.wait(lock, [&] { return release_registry; });
        game::registry().selftest_unlock_catalog_state();
    });
    {
        std::unique_lock lock(prepare_mutex);
        prepare_changed.wait(lock, [&] { return registry_locked; });
    }
    const auto before_registry_contention = f.tuple;
    ok &= require(attempt() == game::CatalogAdoptionResult::Blocked
            && f.tuple == before_registry_contention && !f.owner,
        "registry snapshot contention performed native writes");
    {
        std::lock_guard lock(prepare_mutex);
        release_registry = true;
    }
    prepare_changed.notify_all();
    registry_holder.join();
    auto contention = game::non_audio_hook_gate().try_enter();
    ok &= require(attempt() == game::CatalogAdoptionResult::Blocked,
        "non-audio callback contention was admitted");
    contention = {};
    {
        game::AudioProductionCatalogScope audio_contention;
        ok &= require(audio_contention
                && attempt() == game::CatalogAdoptionResult::Blocked,
            "audio callback scope contention was admitted");
    }

    f.registry_base_stale = true;
    const auto tuple_before_stale_registry = f.tuple;
    ok &= require(attempt() == game::CatalogAdoptionResult::Blocked
            && f.tuple == tuple_before_stale_registry && !f.owner,
        "stale registry base performed native writes");
    f.registry_base_stale = false;
    const auto refreshed_base = game::registry().registry_snapshot();
    f.audio_catalog_revision = refreshed_base.catalog_revision;
    f.audio_catalog = refreshed_base.storage;

    f.trace_count = 0;
    ok &= require(attempt() == game::CatalogAdoptionResult::Adopted
            && f.trace_count == 4
            && f.trace == std::array<Event, 8>{Event::Tuple, Event::Owner,
                Event::Audio, Event::Registry},
        "successful coordinator commit order changed");
    ok &= require(f.owner && f.owner_catalog == game::registry().registry_snapshot().storage
            && f.owner_catalog_revision
                == game::registry().registry_snapshot().catalog_revision,
        "successful adoption did not publish one coherent owner");
    const auto first_adopted_readiness = readiness.snapshot();
    ok &= require(first_adopted_readiness.active_song_count == 1
            && !first_adopted_readiness.catalog_update_pending
            && !first_adopted_readiness.adoption_deferred,
        "successful adoption did not align active readiness");

    const auto first_registry = game::registry().registry_snapshot();
    const auto first_audio = f.audio_catalog;
    const auto first_audio_revision = f.audio_catalog_revision;
    const auto adopted_owner = f.owner;
    ok &= require(exact_restore_fixture()
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Pending
            && !f.owner,
        "exact restore fixture did not preserve immutable republish identity");
    game::registry().set_active_selection(5, 0);
    game::registry().clear_active_selection();
    const auto churned_registry = game::registry().registry_snapshot();
    ok &= require(churned_registry.generation != first_registry.generation
            && churned_registry.catalog_revision == first_registry.catalog_revision
            && churned_registry.storage == first_registry.storage,
        "state-generation churn changed immutable catalog identity");
    const auto native_fallback_intact = [&] {
        return f.tuple == game::NativeArrayTuple{0x1000, 5, 5}
            && !f.owner && !f.terminal
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Pending;
    };
    const auto recoverable_native_fallback = [&](const game::CatalogAdoptionResult result) {
        return result == game::CatalogAdoptionResult::Blocked
            && game::classify_menu_open_catalog_result(true, result)
                == game::MenuOpenCatalogDisposition::ForwardOriginal
            && native_fallback_intact();
    };

    bool republish_pending_locked = false;
    bool release_republish_pending = false;
    std::thread republish_pending_holder([&] {
        game::catalog_adoption_selftest_lock_pending();
        {
            std::lock_guard lock(prepare_mutex);
            republish_pending_locked = true;
        }
        prepare_changed.notify_all();
        std::unique_lock lock(prepare_mutex);
        prepare_changed.wait(lock, [&] { return release_republish_pending; });
        game::catalog_adoption_selftest_unlock_pending();
    });
    {
        std::unique_lock lock(prepare_mutex);
        prepare_changed.wait(lock, [&] { return republish_pending_locked; });
    }
    const auto pending_contended_reopen = attempt();
    {
        std::lock_guard lock(prepare_mutex);
        release_republish_pending = true;
    }
    prepare_changed.notify_all();
    republish_pending_holder.join();
    ok &= require(recoverable_native_fallback(pending_contended_reopen),
        "pending-lock contention did not retain the native fallback candidate");

    auto republish_callback_contention = game::non_audio_hook_gate().try_enter();
    const auto callback_contended_reopen = attempt();
    republish_callback_contention = {};
    ok &= require(recoverable_native_fallback(callback_contended_reopen),
        "non-audio contention did not retain the native fallback candidate");

    f.menu_blocked = true;
    const auto blocked_reopen = attempt();
    f.menu_blocked = false;
    ok &= require(recoverable_native_fallback(blocked_reopen)
            && game::registry().is_current(churned_registry),
        "blocked immediate reopen did not preserve a usable native fallback and republish candidate");

    f.binding_unreadable = true;
    const auto binding_blocked_reopen = attempt();
    f.binding_unreadable = false;
    ok &= require(recoverable_native_fallback(binding_blocked_reopen),
        "binding failure did not retain the native fallback candidate");

    f.list_contended = true;
    const auto list_contended_reopen = attempt();
    f.list_contended = false;
    ok &= require(recoverable_native_fallback(list_contended_reopen),
        "list contention did not retain the native fallback candidate");

    game::registry().set_active_selection(5, 0);
    const bool republish_profile_frozen = game::registry().freeze_active_profile();
    f.coordinator_blocked = true;
    f.selection_blocked = true;
    f.audio_idle = false;
    f.audio_base_stale = true;
    const auto republish_registry = game::registry().registry_snapshot();
    f.trace_count = 0;
    const auto republish_result = attempt();
    const bool republish_registry_unchanged
        = game::registry().is_current(republish_registry);
    f.coordinator_blocked = false;
    f.selection_blocked = false;
    f.audio_idle = true;
    f.audio_base_stale = false;
    game::registry().clear_frozen_profile();
    game::registry().clear_active_selection();
    ok &= require(republish_profile_frozen
            && republish_result == game::CatalogAdoptionResult::Republished
            && f.trace_count == 2
            && f.trace[0] == Event::Tuple && f.trace[1] == Event::Owner
            && f.owner && f.owner != adopted_owner
            && f.owner_registry_generation == 0
            && republish_registry_unchanged
            && f.audio_catalog == first_audio
            && f.audio_catalog_revision == first_audio_revision
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::None,
        "list-only republish inherited a full-catalog quiescence or mutation gate");
    ok &= require(exact_restore_fixture()
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Pending
            && !f.owner,
        "second exact close did not preserve the repeated-reopen candidate");

    const auto first_owner = adopted_owner;
    first_pending.reset();
    const auto preparations_before_retry = f.prepared_catalogs;
    auto second_pending = game::prepare_pending_catalog({song(5), song(6)}, prefix2);
    game::RejectedPendingCatalogRetry same_prefix_retry;
    same_prefix_retry.begin_new_offer();
    f.terminal = true;
    const bool same_prefix_first_offer =
        game::offer_prepared_pending_catalog(second_pending);
    f.terminal = false;
    same_prefix_retry.retain_rejected(second_pending, prefix2);
    std::shared_ptr<const game::PreparedAudioPrefix> retried_prefix;
    const bool same_prefix_final_retry = same_prefix_retry.retry(retried_prefix);
    ok &= require(second_pending && !same_prefix_first_offer && same_prefix_final_retry
            && f.prepared_catalogs == preparations_before_retry
            && retried_prefix == prefix2
            && !weak1.expired(),
        "same unresolved prefix did not reject then accept without eager reconstruction");

    game::registry().set_active_selection(5, 0);
    const auto active_registry_before_block = game::registry().registry_snapshot();
    const auto active_audio_before_block = f.audio_catalog;
    const auto active_tuple_before_block = f.tuple;
    const auto active_owner_before_block = f.owner;
    const auto* active_prefix_before_block = f.audio_prefix.get();
    int active_catalog_forwards = 0;
    for (int open = 0; open < 2; ++open) {
        const auto result = attempt();
        if (game::classify_menu_open_catalog_result(true, result)
            == game::MenuOpenCatalogDisposition::ForwardOriginal)
            ++active_catalog_forwards;
        ok &= require(result == game::CatalogAdoptionResult::Blocked,
            "active selection did not block pending adoption");
    }
    ok &= require(active_catalog_forwards == 2
            && active_registry_before_block.storage
            && active_registry_before_block.storage->size() == 1
            && game::registry().is_current(active_registry_before_block)
            && f.audio_catalog == active_audio_before_block
            && f.tuple == active_tuple_before_block
            && f.owner == active_owner_before_block
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::Pending
            && f.audio_prefix.get() == active_prefix_before_block,
        "blocked active-catalog opens changed list/audio/registry ownership or lifetime");
    const auto active_blocked_readiness = readiness.snapshot();
    ok &= require(active_blocked_readiness.active_song_count == 1
            && active_blocked_readiness.available_on_reopen_song_count == 2
            && active_blocked_readiness.catalog_update_pending
            && active_blocked_readiness.adoption_deferred,
        "blocked nonempty-catalog opens did not preserve deferred readiness");
    game::registry().clear_active_selection();

    game::registry().set_active_selection(5, 0);
    const bool profile_frozen = game::registry().freeze_active_profile();
    const auto frozen_registry_before_block = game::registry().registry_snapshot();
    const auto frozen_audio_before_block = f.audio_catalog;
    const auto frozen_tuple_before_block = f.tuple;
    const auto frozen_owner_before_block = f.owner;
    const auto* frozen_prefix_before_block = f.audio_prefix.get();
    ok &= require(profile_frozen
            && attempt() == game::CatalogAdoptionResult::Blocked
            && game::registry().is_current(frozen_registry_before_block)
            && f.audio_catalog == frozen_audio_before_block
            && f.tuple == frozen_tuple_before_block
            && f.owner == frozen_owner_before_block
            && f.audio_prefix.get() == frozen_prefix_before_block,
        "frozen-profile contention changed active catalog ownership or lifetime");
    game::registry().clear_frozen_profile();
    const auto selected = game::registry().selection_snapshot();
    const game::CustomContextToken token{
        selected.generation, 1, 1, 1, nullptr, nullptr, nullptr, nullptr, 0};
    ok &= require(game::registry().publish_playback(selected, token),
        "playback blocker setup failed");
    game::registry().clear_active_selection();
    const auto playback_registry_before_block = game::registry().registry_snapshot();
    const auto playback_audio_before_block = f.audio_catalog;
    const auto playback_tuple_before_block = f.tuple;
    const auto playback_owner_before_block = f.owner;
    const auto* playback_prefix_before_block = f.audio_prefix.get();
    ok &= require(attempt() == game::CatalogAdoptionResult::Blocked
            && game::registry().is_current(playback_registry_before_block)
            && f.audio_catalog == playback_audio_before_block
            && f.tuple == playback_tuple_before_block
            && f.owner == playback_owner_before_block
            && f.audio_prefix.get() == playback_prefix_before_block,
        "playback contention changed active list/audio/registry ownership or lifetime");
    const bool playback_revoked = game::registry().revoke_playback(token);
    const auto cleanup_registry_before_block = game::registry().registry_snapshot();
    const auto cleanup_audio_before_block = f.audio_catalog;
    const auto cleanup_tuple_before_block = f.tuple;
    const auto cleanup_owner_before_block = f.owner;
    const auto* cleanup_prefix_before_block = f.audio_prefix.get();
    ok &= require(playback_revoked
            && attempt() == game::CatalogAdoptionResult::Blocked
            && game::registry().is_current(cleanup_registry_before_block)
            && f.audio_catalog == cleanup_audio_before_block
            && f.tuple == cleanup_tuple_before_block
            && f.owner == cleanup_owner_before_block
            && f.audio_prefix.get() == cleanup_prefix_before_block,
        "cleanup contention changed active list/audio/registry ownership or lifetime");
    ok &= require(game::registry().retire_cleanup_lease(token),
        "cleanup blocker retirement failed");

    ok &= require(attempt() == game::CatalogAdoptionResult::Adopted
            && f.owner && f.owner != first_owner
            && game::piano_list_catalog_republish_state()
                == game::PianoListRepublishState::None,
        "restored vanilla tuple did not publish the newer authoritative prefix");
    ok &= require(readiness.snapshot().active_song_count == 2
            && !readiness.snapshot().catalog_update_pending,
        "newer successful adoption did not catch active count up");
    second_pending.reset();
    retried_prefix.reset();
    ok &= require(f.audio_prefix == prefix2
            && prefix_token(f.audio_prefix)->prior.get() == prefix_token(prefix1),
        "active catalog did not retain the production-shaped persistent prefix");

    auto third_pending = game::prepare_pending_catalog(
        {song(5), song(6), song(7)}, prefix3);
    bool interleaved_lock_held = false;
    bool release_interleaved_lock = false;
    std::thread interleaved_holder([&] {
        game::catalog_adoption_selftest_lock_pending();
        {
            std::lock_guard lock(prepare_mutex);
            interleaved_lock_held = true;
        }
        prepare_changed.notify_all();
        {
            std::unique_lock lock(prepare_mutex);
            prepare_changed.wait(lock, [&] { return release_interleaved_lock; });
        }
        game::catalog_adoption_selftest_unlock_pending();
    });
    {
        std::unique_lock lock(prepare_mutex);
        prepare_changed.wait(lock, [&] { return interleaved_lock_held; });
    }
    bool interleaved_publication_accepted = false;
    bool interleaved_publication_started = false;
    std::thread interleaved_publication([&] {
        {
            std::lock_guard lock(prepare_mutex);
            interleaved_publication_started = true;
        }
        prepare_changed.notify_all();
        interleaved_publication_accepted =
            game::offer_prepared_pending_catalog(third_pending);
    });
    {
        std::unique_lock lock(prepare_mutex);
        prepare_changed.wait(lock, [&] { return interleaved_publication_started; });
    }
    const auto interleaved_attempt = attempt();
    ok &= require(interleaved_attempt == game::CatalogAdoptionResult::Blocked
            && game::classify_menu_open_catalog_result(true, interleaved_attempt)
                == game::MenuOpenCatalogDisposition::ForwardOriginal,
        "publication/adoption lock interleaving did not forward the active catalog");
    {
        std::lock_guard lock(prepare_mutex);
        release_interleaved_lock = true;
    }
    prepare_changed.notify_all();
    interleaved_holder.join();
    interleaved_publication.join();
    ok &= require(interleaved_publication_accepted
            && attempt() == game::CatalogAdoptionResult::Adopted,
        "interleaved newest-prefix publication was not adopted on immediate retry");
    third_pending.reset();

    game::RejectedPendingCatalogRetry stale_retry;
    auto stale_prefix = prefix_after({}, 9);
    const auto preparations_before_stale = f.prepared_catalogs;
    stale_retry.begin_new_offer();
    auto stale_a = game::prepare_pending_catalog({song(9)}, stale_prefix);
    f.terminal = true;
    const bool stale_a_accepted = game::offer_prepared_pending_catalog(stale_a);
    f.terminal = false;
    stale_retry.retain_rejected(stale_a, stale_prefix);
    stale_retry.begin_new_offer();
    auto failed_larger_b = game::prepare_pending_catalog(
        {song(9), song(10)}, stale_prefix);
    std::shared_ptr<const game::PreparedAudioPrefix> stale_retry_prefix;
    ok &= require(stale_a && !stale_a_accepted && failed_larger_b
            && !stale_retry.retry(stale_retry_prefix) && !stale_retry_prefix
            && f.prepared_catalogs == preparations_before_stale
            && attempt() == game::CatalogAdoptionResult::NoPending,
        "new unresolved offer retained or published the older rejected prefix");

    prefix1.reset();
    prefix2.reset();
    prefix3.reset();
    ok &= require(!weak1.expired() && !weak2.expired(),
        "active prefix did not preserve prior persistent nodes");
    f.audio_prefix.reset();
    ok &= require(weak1.expired() && weak2.expired(),
        "persistent nodes survived after final prefix ownership disappeared");

    auto terminal1 = prefix_after({}, 5);
    auto terminal2 = prefix_after(terminal1, 6);
    auto terminal3 = prefix_after(terminal2, 7);
    ok &= require(offer({song(5), song(6), song(7)}, terminal3),
        "terminal pending offer failed");
    f.fault = {2, 1};
    const auto registry_before_terminal = game::registry().registry_snapshot();
    const auto audio_before_terminal = f.audio_catalog;
    ok &= require(attempt() == game::CatalogAdoptionResult::TerminalFailure
            && f.terminal && game::registry().is_current(registry_before_terminal)
            && f.audio_catalog == audio_before_terminal && f.retained_count >= 2
            && attempt() == game::CatalogAdoptionResult::TerminalFailure
            && f.binding_calls == 0
            && !offer({song(9)}, prefix_after({}, 9)),
        "unverified rollback did not enter retained terminal safety state");


    if (!ok) return 1;
    std::cout << "catalog_adoption_selftest: ok\n";
    return 0;
}
