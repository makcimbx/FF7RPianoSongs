#pragma once

#include "game/song_registry.h"
#include "game/audio_sead.h"
#include "game/uobject_identity.h"
#include "core/hooks.h"

#include <vector>

namespace ff7r::piano::game {

enum class CatalogAdoptionResult {
    NoPending,
    Blocked,
    Adopted,
    Republished,
    TerminalFailure,
};

enum class CatalogReadinessEvent {
    Prepared,
    AdoptionDeferred,
    Adopted,
};

using CatalogReadinessCallback =
    void(*)(void* context, CatalogReadinessEvent event, std::size_t song_count) noexcept;

struct CatalogReadinessObserver {
    void* context = nullptr;
    CatalogReadinessCallback callback = nullptr;
};

// Startup-owned, process-lifetime observer. Configure before release hooks and
// repository publication begin; callbacks receive only already-computed counts.
void configure_catalog_readiness_observer(CatalogReadinessObserver observer) noexcept;
void observe_catalog_adoption_result(CatalogAdoptionResult result) noexcept;

class PreparedPendingCatalog;

// Loader-thread entry point. All descriptor and sidecar work is completed
// before this function makes the immutable catalog pending.
std::shared_ptr<PreparedPendingCatalog> prepare_pending_catalog(
    std::vector<SongDescriptor> descriptors,
    std::shared_ptr<const PreparedAudioPrefix> prefix) noexcept;
bool offer_prepared_pending_catalog(
    const std::shared_ptr<PreparedPendingCatalog>& prepared) noexcept;

class RejectedPendingCatalogRetry final {
public:
    void begin_new_offer() noexcept;
    void retain_rejected(std::shared_ptr<PreparedPendingCatalog> candidate,
        std::shared_ptr<const PreparedAudioPrefix> prefix) noexcept;
    bool retry(std::shared_ptr<const PreparedAudioPrefix>& accepted_prefix) noexcept;

private:
    std::shared_ptr<PreparedPendingCatalog> candidate_;
    std::shared_ptr<const PreparedAudioPrefix> prefix_;
};

// Exact menu-open callback entry point. Never waits. Failure leaves the old
// catalog authoritative and the newest pending candidate available for retry.
CatalogAdoptionResult try_adopt_pending_catalog_before_menu_open(
    void* embedded_list, void* widget, const UObjectLiveHandle& widget_identity,
    core::HookCallbackGate::Lease& callback) noexcept;

#ifdef FF7RP_CATALOG_ADOPTION_SELFTEST
void catalog_adoption_selftest_lock_pending();
void catalog_adoption_selftest_unlock_pending();
#endif

} // namespace ff7r::piano::game
