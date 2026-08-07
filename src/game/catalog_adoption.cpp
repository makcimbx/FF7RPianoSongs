#include "game/catalog_adoption.h"

#include "game/audio_production_wiring.h"
#include "game/audio_sead.h"
#include "game/menu_session_authority.h"
#include "game/module_hooks.h"
#include "game/runtime_layouts.h"
#include "game/profile_list_coordinator.h"

#include <memory>
#include <mutex>

namespace ff7r::piano::game {
#ifdef FF7RP_CATALOG_ADOPTION_SELFTEST
void catalog_adoption_selftest_trace_registry_published() noexcept;
#endif
class PreparedPendingCatalog final {
public:
    std::shared_ptr<const SongRegistryStorage> storage;
    PreparedAudioCatalog audio;
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
    const std::size_t song_count = pending->storage ? pending->storage->size() : 0;
    {
        std::lock_guard lock(g_pending_mutex);
        if (piano_list_catalog_terminal_failure()) return false;
        g_pending = pending;
    }
    notify_readiness(CatalogReadinessEvent::Prepared, song_count);
    return true;
}

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
    if (descriptors.size() > static_cast<std::size_t>(
            runtime_layouts::PianoMusicList::maximum_count
            - runtime_layouts::PianoMusicList::vanilla_count)) return {};
    try {
        auto storage = std::make_shared<const SongRegistryStorage>(std::move(descriptors));
        auto pending = std::make_shared<PreparedPendingCatalog>();
        pending->storage = storage;
        PreparedAudioCatalog audio;
        if (!prepare_audio_catalog_from_prefix(storage, std::move(prefix), audio)) return {};
        pending->audio = std::move(audio);
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
    if (piano_list_catalog_terminal_failure())
        return CatalogAdoptionResult::TerminalFailure;
    std::unique_lock pending_lock(g_pending_mutex, std::try_to_lock);
    if (!pending_lock.owns_lock()) return CatalogAdoptionResult::Blocked;
    std::shared_ptr<PreparedPendingCatalog> pending = g_pending;
    PianoListRepublishState republish_state = PianoListRepublishState::None;
    if (!pending) republish_state = piano_list_catalog_republish_state();
    if (republish_state == PianoListRepublishState::Transient)
        return CatalogAdoptionResult::Blocked;
    const bool republish = republish_state == PianoListRepublishState::Pending;
    if (!pending && !republish) return CatalogAdoptionResult::NoPending;
    if (republish) {
        if (!embedded_list || !widget
            || !menu_session_authority().try_idle_for_catalog_adoption()) {
            return CatalogAdoptionResult::Blocked;
        }
        auto callback_exclusive = non_audio_hook_gate().try_suspend_exclusive(callback);
        if (!callback_exclusive) return CatalogAdoptionResult::Blocked;
        void* bound_widget = nullptr;
        UObjectLiveHandle bound_identity{};
        const auto binding_matches = [&]() noexcept {
            bound_widget = nullptr;
            bound_identity = {};
            return resolve_piano_menu_widget_binding(
                       embedded_list, bound_widget, bound_identity)
                && bound_widget == widget
                && bound_identity.internal_index == widget_identity.internal_index
                && bound_identity.serial_number == widget_identity.serial_number;
        };
        if (!binding_matches()
            || !menu_session_authority().try_idle_for_catalog_adoption()) {
            callback = callback_exclusive.resume_as_lease();
            return CatalogAdoptionResult::Blocked;
        }
        auto list = prepare_piano_list_catalog_republish(widget, widget_identity);
        if (!list || !binding_matches()
            || !menu_session_authority().try_idle_for_catalog_adoption()) {
            callback = callback_exclusive.resume_as_lease();
            return CatalogAdoptionResult::Blocked;
        }
        const auto list_result = commit_prepared_piano_list_catalog(*list);
        if (list_result != PianoListCatalogCommitResult::Committed) {
            callback = callback_exclusive.resume_as_lease();
            return list_result == PianoListCatalogCommitResult::RollbackUnverified
                ? CatalogAdoptionResult::TerminalFailure
                : CatalogAdoptionResult::Blocked;
        }
        finalize_prepared_piano_list_catalog(*list);
        list.reset();
        callback = callback_exclusive.resume_as_lease();
        pending_lock.unlock();
        return CatalogAdoptionResult::Republished;
    }
    if (!embedded_list || !widget
        || !menu_session_authority().try_idle_for_catalog_adoption()
        || !profile_list_coordinator().try_ready_for_catalog_adoption()
        || !try_selection_runtime_idle_for_catalog_adoption()) {
        return CatalogAdoptionResult::Blocked;
    }

    auto callback_exclusive = non_audio_hook_gate().try_suspend_exclusive(callback);
    if (!callback_exclusive) return CatalogAdoptionResult::Blocked;
    AudioProductionCatalogScope audio_callbacks;
    if (!audio_callbacks) {
        callback = callback_exclusive.resume_as_lease();
        return CatalogAdoptionResult::Blocked;
    }
    void* bound_widget = nullptr;
    UObjectLiveHandle bound_identity{};
    const auto binding_matches = [&]() noexcept {
        bound_widget = nullptr;
        bound_identity = {};
        return resolve_piano_menu_widget_binding(
                   embedded_list, bound_widget, bound_identity)
            && bound_widget == widget
            && bound_identity.internal_index == widget_identity.internal_index
            && bound_identity.serial_number == widget_identity.serial_number;
    };
    if (!binding_matches()) {
        callback = callback_exclusive.resume_as_lease();
        return CatalogAdoptionResult::Blocked;
    }
    RegistrySnapshot expected;
    if (!registry().try_registry_snapshot(expected)) {
        callback = callback_exclusive.resume_as_lease();
        return CatalogAdoptionResult::Blocked;
    }
    auto list = prepare_piano_list_catalog(widget, widget_identity,
        expected, pending->storage);
    auto audio_commit = begin_prepared_audio_catalog_commit(
        pending->audio, expected.catalog_revision, audio_callbacks);
    auto registry_commit = registry().begin_catalog_commit(expected, pending->storage);
    if (!list || !registry_commit || !audio_commit
        || !menu_session_authority().try_idle_for_catalog_adoption()
        || !profile_list_coordinator().try_ready_for_catalog_adoption()
        || !try_selection_runtime_idle_for_catalog_adoption()) {
        callback = callback_exclusive.resume_as_lease();
        return CatalogAdoptionResult::Blocked;
    }

    // Re-resolve the exact embedded-list weak binding after all preparation and
    // immediately before the first native tuple write.
    if (!binding_matches()) {
        callback = callback_exclusive.resume_as_lease();
        return CatalogAdoptionResult::Blocked;
    }

    // Lock order is pending publication, non-audio callback admission, audio
    // callback admission, audio state, registry state, then list ownership for
    // the bounded tuple commit. Preparation never retains the list lock.
    const auto list_result = commit_prepared_piano_list_catalog(*list);
    if (list_result != PianoListCatalogCommitResult::Committed) {
        callback = callback_exclusive.resume_as_lease();
        return list_result == PianoListCatalogCommitResult::RollbackUnverified
            ? CatalogAdoptionResult::TerminalFailure : CatalogAdoptionResult::Blocked;
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
    return CatalogAdoptionResult::Adopted;
}

#ifdef FF7RP_CATALOG_ADOPTION_SELFTEST
void catalog_adoption_selftest_lock_pending() { g_pending_mutex.lock(); }
void catalog_adoption_selftest_unlock_pending() { g_pending_mutex.unlock(); }
#endif

} // namespace ff7r::piano::game
