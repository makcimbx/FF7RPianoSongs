#include "game/catalog_adoption.h"

#include "game/audio_production_wiring.h"
#include "game/audio_sead.h"
#include "game/menu_session_authority.h"
#include "game/module_hooks.h"
#include "game/runtime_layouts.h"
#include "game/profile_list_coordinator.h"

#include "core/logging.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>

namespace ff7r::piano::game {
#ifdef FF7RP_CATALOG_ADOPTION_SELFTEST
void catalog_adoption_selftest_trace_registry_published() noexcept;
#endif
// `unresolved` is what the offline pipeline produced: the songs in discovery
// order, claiming no piano row.  `storage` and `audio` are that catalog
// resolved onto the rows appended after a live list of `first_custom_row`
// entries, and are the single identity the list, the registry and the audio
// catalog all commit together.
class PreparedPendingCatalog final {
public:
    std::shared_ptr<const SongRegistryStorage> storage;
    PreparedAudioCatalog audio;
    SongRegistryStorage unresolved;
    std::shared_ptr<const PreparedAudioPrefix> prefix;
    std::int32_t first_custom_row = -1;
};

namespace {

std::mutex g_pending_mutex;
std::shared_ptr<PreparedPendingCatalog> g_pending;
CatalogReadinessObserver g_readiness_observer;

void notify_readiness(const CatalogReadinessEvent event,
    const std::size_t song_count) noexcept
{
    const auto observer = g_readiness_observer;
    if (observer.callback) observer.callback(observer.context, event, song_count);
}

bool publish_pending(const std::shared_ptr<PreparedPendingCatalog>& pending) noexcept
{
    if (!pending) return false;
    const std::size_t song_count = pending->unresolved.size();
    {
        std::lock_guard lock(g_pending_mutex);
        if (piano_list_catalog_terminal_failure()) return false;
        g_pending = pending;
    }
    notify_readiness(CatalogReadinessEvent::Prepared, song_count);
    return true;
}

// A piano row is a live-list fact: the vanilla list grows with story progress
// and unlocked sheet music, so no offline stage can name it.  Resolution
// happens here, memoised per observed list length so a repeatedly blocked
// attempt re-uses the same storage instead of rebuilding it.
bool resolve_pending_catalog_rows(PreparedPendingCatalog& pending,
    const std::int32_t first_custom_row) noexcept
{
    if (pending.storage && pending.first_custom_row == first_custom_row) return true;
    if (first_custom_row < 0
        || static_cast<std::size_t>(first_custom_row) + pending.unresolved.size()
            > static_cast<std::size_t>(runtime_layouts::PianoMusicList::maximum_count)) {
        return false;
    }
    try {
        SongRegistryStorage resolved = pending.unresolved;
        if (!assign_custom_rows(resolved, first_custom_row)) return false;
        auto storage = std::make_shared<const SongRegistryStorage>(std::move(resolved));
        PreparedAudioCatalog audio;
        if (!prepare_audio_catalog_from_prefix(storage, pending.prefix, audio)) return false;
        pending.storage = std::move(storage);
        pending.audio = std::move(audio);
        pending.first_custom_row = first_custom_row;
        return true;
    } catch (...) {
        return false;
    }
}

const char* republish_state_name(const PianoListRepublishState state) noexcept
{
    switch (state) {
    case PianoListRepublishState::None: return "none";
    case PianoListRepublishState::Pending: return "pending";
    case PianoListRepublishState::Transient: return "transient";
    }
    return "unknown";
}

const char* list_commit_result_name(const PianoListCatalogCommitResult result) noexcept
{
    switch (result) {
    case PianoListCatalogCommitResult::Committed: return "committed";
    case PianoListCatalogCommitResult::Rejected: return "rejected";
    case PianoListCatalogCommitResult::RollbackUnverified: return "rollback_unverified";
    }
    return "unknown";
}

// One bounded event per adoption attempt.  `outcome` and `first_failure` default
// to "exception" and are overwritten only where a disposition is actually
// reached, so an unlabelled return added later reports itself instead of
// vanishing; silence is not a reachable state.  Each rung of the refusal ladder
// names the single predicate that is false rather than the conjunction that
// contained it.  The instance is declared ahead of every lock and lease the
// attempt takes, so this destructor formats and writes only after all of them
// have been released, per the runtime observability contract.
struct AdoptionReport final {
    const char* outcome = "exception";
    const char* first_failure = "exception";
    const char* branch = "none";
    const char* republish_state = "not_evaluated";
    const char* binding = "not_evaluated";
    const char* list_commit = "not_evaluated";
    core::LogLevel level = core::LogLevel::Info;
    bool pending_present = false;
    std::size_t pending_songs = 0;
    std::uint64_t registry_generation = 0;
    std::uint64_t catalog_revision = 0;
    std::int32_t first_custom_row = -1;

    ~AdoptionReport() noexcept
    {
        try {
            std::ostringstream out;
            out << "[catalog_adoption] attempt outcome=" << outcome
                << " first_failure=" << first_failure
                << " branch=" << branch
                << " pending=" << (pending_present ? 1 : 0)
                << " pending_songs=" << pending_songs
                << " republish_state=" << republish_state
                << " binding=" << binding
                << " list_commit=" << list_commit
                << " registry_gen=" << registry_generation
                << " catalog_revision=" << catalog_revision
                << " first_custom_row=" << first_custom_row;
            core::log(level, out.str());
        } catch (...) {}
    }
};

} // namespace

void configure_catalog_readiness_observer(const CatalogReadinessObserver observer) noexcept
{
    g_readiness_observer = observer;
}

void observe_catalog_adoption_result(const CatalogAdoptionResult result) noexcept
{
    if (result == CatalogAdoptionResult::Blocked)
        notify_readiness(CatalogReadinessEvent::AdoptionDeferred, 0);
}

std::shared_ptr<PreparedPendingCatalog> prepare_pending_catalog(
    std::vector<SongDescriptor> descriptors,
    std::shared_ptr<const PreparedAudioPrefix> prefix) noexcept
{
    if (descriptors.empty() || !prefix || piano_list_catalog_terminal_failure()) return {};
    // The native boundary is unavailable until adoption. Admit only the
    // absolute list bound here; resolution applies the exact live-boundary
    // bound before preparing or publishing any runtime catalog state.
    if (descriptors.size() > static_cast<std::size_t>(
            runtime_layouts::PianoMusicList::maximum_count)) return {};
    try {
        auto pending = std::make_shared<PreparedPendingCatalog>();
        pending->unresolved = std::move(descriptors);
        pending->prefix = std::move(prefix);
        return pending;
    } catch (...) {
        return {};
    }
}

bool offer_prepared_pending_catalog(
    const std::shared_ptr<PreparedPendingCatalog>& prepared) noexcept
{
    try { return publish_pending(prepared); } catch (...) { return false; }
}

void RejectedPendingCatalogRetry::begin_new_offer() noexcept
{
    candidate_.reset();
    prefix_.reset();
}

void RejectedPendingCatalogRetry::retain_rejected(
    std::shared_ptr<PreparedPendingCatalog> candidate,
    std::shared_ptr<const PreparedAudioPrefix> prefix) noexcept
{
    candidate_ = std::move(candidate);
    prefix_ = std::move(prefix);
}

bool RejectedPendingCatalogRetry::retry(
    std::shared_ptr<const PreparedAudioPrefix>& accepted_prefix) noexcept
{
    accepted_prefix.reset();
    if (!candidate_ || !prefix_) return false;
    if (!offer_prepared_pending_catalog(candidate_)) return false;
    accepted_prefix = std::move(prefix_);
    candidate_.reset();
    return true;
}

CatalogAdoptionResult try_adopt_pending_catalog_before_menu_open(
    void* embedded_list, void* widget, const UObjectLiveHandle& widget_identity,
    core::HookCallbackGate::Lease& callback) noexcept
{
    // Declared ahead of `pending_lock`, the exclusive callback suspension and the
    // audio callback scope, so the report is formatted and written only after
    // every one of them has been released.
    AdoptionReport report;
    const auto blocked = [&]() noexcept {
        report.outcome = "blocked";
        return CatalogAdoptionResult::Blocked;
    };
    const auto terminal_failure = [&]() noexcept {
        report.outcome = "terminal_failure";
        report.level = core::LogLevel::Error;
        return CatalogAdoptionResult::TerminalFailure;
    };
    if (piano_list_catalog_terminal_failure()) {
        report.first_failure = "terminal_failure_latched";
        return terminal_failure();
    }
    std::unique_lock pending_lock(g_pending_mutex, std::try_to_lock);
    if (!pending_lock.owns_lock()) {
        report.first_failure = "pending_lock_contended";
        return blocked();
    }
    std::shared_ptr<PreparedPendingCatalog> pending = g_pending;
    report.pending_present = static_cast<bool>(pending);
    report.pending_songs = pending ? pending->unresolved.size() : 0;
    PianoListRepublishState republish_state = PianoListRepublishState::None;
    if (!pending) republish_state = piano_list_catalog_republish_state();
    report.republish_state = republish_state_name(republish_state);
    if (republish_state == PianoListRepublishState::Transient) {
        report.first_failure = "republish_transient";
        return blocked();
    }
    const bool republish = republish_state == PianoListRepublishState::Pending;
    if (!pending && !republish) {
        report.outcome = "no_pending";
        report.first_failure = "none";
        return CatalogAdoptionResult::NoPending;
    }
    void* bound_widget = nullptr;
    UObjectLiveHandle bound_identity{};
    // The two branches below used one identical copy of this predicate each.
    // Terms, short-circuit order, and the single
    // resolve_piano_menu_widget_binding call per invocation are unchanged; the
    // conjunction is split only so a refusal can name the term that is false
    // instead of reporting the whole binding check.
    const auto binding_matches = [&]() noexcept {
        bound_widget = nullptr;
        bound_identity = {};
        if (!resolve_piano_menu_widget_binding(
                embedded_list, bound_widget, bound_identity)) {
            report.binding = "unresolved";
            return false;
        }
        if (bound_widget != widget) {
            report.binding = "widget_pointer";
            return false;
        }
        if (bound_identity.internal_index != widget_identity.internal_index) {
            report.binding = "widget_index";
            return false;
        }
        if (bound_identity.serial_number != widget_identity.serial_number) {
            report.binding = "widget_serial";
            return false;
        }
        report.binding = "matched";
        return true;
    };
    if (republish) {
        report.branch = "republish";
        if (!embedded_list) {
            report.first_failure = "embedded_list_null";
            return blocked();
        }
        if (!widget) {
            report.first_failure = "widget_null";
            return blocked();
        }
        if (!menu_session_authority().try_idle_for_catalog_adoption()) {
            report.first_failure = "menu_session_not_idle";
            return blocked();
        }
        auto callback_exclusive = non_audio_hook_gate().try_suspend_exclusive(callback);
        if (!callback_exclusive) {
            report.first_failure = "callback_gate_not_exclusive";
            return blocked();
        }
        if (!binding_matches()) {
            callback = callback_exclusive.resume_as_lease();
            report.first_failure = "widget_binding";
            return blocked();
        }
        if (!menu_session_authority().try_idle_for_catalog_adoption()) {
            callback = callback_exclusive.resume_as_lease();
            report.first_failure = "menu_session_not_idle_after_binding";
            return blocked();
        }
        auto list = prepare_piano_list_catalog_republish(widget, widget_identity);
        if (!list) {
            callback = callback_exclusive.resume_as_lease();
            report.first_failure = "list_republish_prepare";
            return blocked();
        }
        if (!binding_matches()) {
            callback = callback_exclusive.resume_as_lease();
            report.first_failure = "widget_binding_revalidate";
            return blocked();
        }
        if (!menu_session_authority().try_idle_for_catalog_adoption()) {
            callback = callback_exclusive.resume_as_lease();
            report.first_failure = "menu_session_not_idle_precommit";
            return blocked();
        }
        const auto list_result = commit_prepared_piano_list_catalog(*list);
        report.list_commit = list_commit_result_name(list_result);
        if (list_result != PianoListCatalogCommitResult::Committed) {
            callback = callback_exclusive.resume_as_lease();
            report.first_failure = "list_commit";
            return list_result == PianoListCatalogCommitResult::RollbackUnverified
                ? terminal_failure()
                : blocked();
        }
        finalize_prepared_piano_list_catalog(*list);
        list.reset();
        callback = callback_exclusive.resume_as_lease();
        pending_lock.unlock();
        report.outcome = "republished";
        report.first_failure = "none";
        return CatalogAdoptionResult::Republished;
    }
    report.branch = "adopt";
    if (!embedded_list) {
        report.first_failure = "embedded_list_null";
        return blocked();
    }
    if (!widget) {
        report.first_failure = "widget_null";
        return blocked();
    }
    if (!menu_session_authority().try_idle_for_catalog_adoption()) {
        report.first_failure = "menu_session_not_idle";
        return blocked();
    }
    if (!profile_list_coordinator().try_ready_for_catalog_adoption()) {
        report.first_failure = "profile_coordinator_not_ready";
        return blocked();
    }
    if (!try_selection_runtime_idle_for_catalog_adoption()) {
        report.first_failure = "selection_runtime_not_idle";
        return blocked();
    }
    std::int32_t first_custom_row = -1;
    if (!piano_list_first_custom_row(widget, widget_identity, first_custom_row)) {
        report.first_failure = "list_row_unreadable";
        return blocked();
    }
    report.first_custom_row = first_custom_row;
    if (!resolve_pending_catalog_rows(*pending, first_custom_row)) {
        report.first_failure = "list_row_resolve";
        return blocked();
    }

    auto callback_exclusive = non_audio_hook_gate().try_suspend_exclusive(callback);
    if (!callback_exclusive) {
        report.first_failure = "callback_gate_not_exclusive";
        return blocked();
    }
    AudioProductionCatalogScope audio_callbacks;
    if (!audio_callbacks) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "audio_callback_scope";
        return blocked();
    }
    if (!binding_matches()) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "widget_binding";
        return blocked();
    }
    RegistrySnapshot expected;
    if (!registry().try_registry_snapshot(expected)) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "registry_snapshot_contended";
        return blocked();
    }
    report.registry_generation = expected.generation;
    report.catalog_revision = expected.catalog_revision;
    auto list = prepare_piano_list_catalog(widget, widget_identity,
        expected, pending->storage);
    auto audio_commit = begin_prepared_audio_catalog_commit(
        pending->audio, expected.catalog_revision, audio_callbacks);
    auto registry_commit = registry().begin_catalog_commit(expected, pending->storage);
    if (!list) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "list_prepare";
        return blocked();
    }
    if (!registry_commit) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "registry_commit_begin";
        return blocked();
    }
    if (!audio_commit) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "audio_commit_begin";
        return blocked();
    }
    if (!menu_session_authority().try_idle_for_catalog_adoption()) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "menu_session_not_idle_prepared";
        return blocked();
    }
    if (!profile_list_coordinator().try_ready_for_catalog_adoption()) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "profile_coordinator_not_ready_prepared";
        return blocked();
    }
    if (!try_selection_runtime_idle_for_catalog_adoption()) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "selection_runtime_not_idle_prepared";
        return blocked();
    }

    // Re-resolve the exact embedded-list weak binding after all preparation and
    // immediately before the first native tuple write.
    if (!binding_matches()) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "widget_binding_precommit";
        return blocked();
    }

    // Lock order is pending publication, non-audio callback admission, audio
    // callback admission, audio state, registry state, then list ownership for
    // the bounded tuple commit. Preparation never retains the list lock.
    const auto list_result = commit_prepared_piano_list_catalog(*list);
    report.list_commit = list_commit_result_name(list_result);
    if (list_result != PianoListCatalogCommitResult::Committed) {
        callback = callback_exclusive.resume_as_lease();
        report.first_failure = "list_commit";
        return list_result == PianoListCatalogCommitResult::RollbackUnverified
            ? terminal_failure() : blocked();
    }
    // No fallible operation is permitted between native tuple publication and
    // the owning identity swaps.
    commit_prepared_audio_catalog(*audio_commit, expected.generation + 1,
        expected.catalog_revision + 1);
    registry().commit_catalog(*registry_commit);
#ifdef FF7RP_CATALOG_ADOPTION_SELFTEST
    catalog_adoption_selftest_trace_registry_published();
#endif
    finalize_prepared_piano_list_catalog(*list);
    finalize_prepared_audio_catalog_commit(*audio_commit);
    list.reset();
    audio_commit.reset();
    registry_commit.reset();
    callback = callback_exclusive.resume_as_lease();
    const std::size_t adopted_song_count = pending->storage->size();
    g_pending.reset();
    pending_lock.unlock();
    notify_readiness(CatalogReadinessEvent::Adopted, adopted_song_count);
    report.outcome = "adopted";
    report.first_failure = "none";
    return CatalogAdoptionResult::Adopted;
}

#ifdef FF7RP_CATALOG_ADOPTION_SELFTEST
void catalog_adoption_selftest_lock_pending() { g_pending_mutex.lock(); }
void catalog_adoption_selftest_unlock_pending() { g_pending_mutex.unlock(); }
#endif

} // namespace ff7r::piano::game
