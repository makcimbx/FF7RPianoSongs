#pragma once

#include "game/song_registry.h"
#include "pipeline/song_repository.h"

#include <string>
#include <functional>
#include <vector>

namespace ff7r::piano::startup {

class StartupCacheProgress;

enum class MusicRepositoryLogLevel {
    Error,
    Info,
};

struct MusicRepositoryLogEvent {
    MusicRepositoryLogLevel level = MusicRepositoryLogLevel::Info;
    std::string text;
};

struct MusicRepositoryPlan {
    bool ready_to_publish = false;
    std::vector<MusicRepositoryLogEvent> events;
    std::vector<game::SongDescriptor> descriptors;
};

enum class ProgressiveRepositoryPhase {
    InitialEmpty,
    Discovering,
    CatalogAvailable,
    Settled,
    Failed,
};

struct ProgressiveRepositoryState {
    ProgressiveRepositoryPhase phase = ProgressiveRepositoryPhase::InitialEmpty;
    std::size_t candidate_count = 0;
    std::size_t settled_candidate_count = 0;
    std::size_t valid_song_count = 0;
    bool has_valid_catalog = false;
};

struct ProgressiveRepositoryCallbacks {
    std::function<void(const ProgressiveRepositoryState&)> on_state;
    // Synchronous loader-thread admission. The descriptor is appended only if
    // its independently owned immutable audio sidecar node was prepared.
    std::function<bool(const game::SongDescriptor&)> on_descriptor_admission;
    // Called on the loader worker with a fully composed immutable candidate.
    // Runtime code may prepare native-free sidecars here but must not touch UObjects.
    // Returns true only after the complete candidate has been accepted for
    // private runtime preparation/publication.
    std::function<bool(MusicRepositoryPlan, bool fully_settled)> on_catalog;
    // Exactly-once final retry for the last unchanged rejected candidate. The
    // callback must reuse its already prepared immutable descriptor/sidecar
    // snapshot and perform no source I/O or reconstruction.
    std::function<bool()> on_final_catalog_retry;
};

// Pure production-used composition stage. It projects the complete ordered
// discovery result before any registry mutation can occur.
MusicRepositoryPlan compose_music_repository(ff7rp::pipeline::SongRepositoryResult discovery);

// Concrete publication stage: a ready plan performs exactly one replacement.
// A failed plan never mutates the registry.
bool publish_music_repository(MusicRepositoryPlan&& plan, game::SongRegistry& registry,
    StartupCacheProgress* progress = nullptr);

// Production startup coordinator.
bool load_music_repository(const std::wstring& dll_dir, StartupCacheProgress* progress = nullptr);

// Synchronous worker body for the process-lifetime progressive service. The
// caller owns the thread. Publication callbacks never receive an empty catalog.
ProgressiveRepositoryState run_progressive_music_repository(
    const std::wstring& dll_dir, const ProgressiveRepositoryCallbacks& callbacks,
    StartupCacheProgress* progress = nullptr);

#ifdef FF7RP_MUSIC_REPOSITORY_SELFTEST
struct ProgressiveRepositoryTestSettlement {
    MusicRepositoryPlan plan;
    std::size_t settled_candidate_count = 0;
    bool fully_settled = false;
};
ProgressiveRepositoryState run_progressive_repository_test_sequence(
    std::size_t candidate_count,
    std::vector<ProgressiveRepositoryTestSettlement> settlements,
    const ProgressiveRepositoryCallbacks& callbacks,
    StartupCacheProgress* progress = nullptr,
    bool terminal_worker_failure_after_settlements = false);
#endif

} // namespace ff7r::piano::startup
