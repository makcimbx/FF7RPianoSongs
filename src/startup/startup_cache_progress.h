#pragma once

#include "pipeline/song_repository.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

namespace ff7r::piano::startup {

enum class StartupCachePhase {
    Starting,
    Loading,
    Publishing,
    Ready,
    Failed,
};

struct StartupCacheProgressSnapshot {
    StartupCachePhase phase = StartupCachePhase::Starting;
    std::size_t candidate_count = 0;
    std::size_t completed_candidates = 0;
    std::size_t active_candidate_count = 0;
    std::size_t completed_stage_units = 0;
    std::size_t total_stage_units = 0;
    ff7rp::pipeline::SongLoadProgressStage latest_stage =
        ff7rp::pipeline::SongLoadProgressStage::Inspecting;
    std::size_t published_song_count = 0;
    std::size_t available_on_reopen_song_count = 0;
    std::size_t active_song_count = 0;
    bool catalog_update_pending = false;
    bool adoption_deferred = false;
    bool estimates_available = false;
    std::size_t estimated_per_song_seconds = 0;
    std::size_t estimated_total_remaining_seconds = 0;
};

using StartupCacheNow = std::function<std::chrono::steady_clock::time_point()>;

class StartupCacheProgress {
public:
    explicit StartupCacheProgress(StartupCacheNow now = {}) noexcept;

    void begin(std::size_t candidate_count) noexcept;
    void observe(const ff7rp::pipeline::SongRepositoryProgress& progress) noexcept;
    void publishing() noexcept;
    void available_on_reopen(std::size_t song_count) noexcept;
    void catalog_prepared(std::size_t song_count) noexcept;
    void catalog_adopted(std::size_t song_count) noexcept;
    void catalog_adoption_deferred() noexcept;
    void ready(std::size_t published_song_count) noexcept;
    void failed() noexcept;
    bool try_snapshot(StartupCacheProgressSnapshot& snapshot) const noexcept;
    StartupCacheProgressSnapshot snapshot() const noexcept;

private:
    mutable std::mutex mutex_;
    StartupCacheProgressSnapshot state_;
    std::vector<unsigned char> candidate_stage_units_;
    std::vector<bool> candidate_complete_;
    std::vector<bool> candidate_started_;
    std::vector<bool> candidate_rebuild_observed_;
    std::vector<std::chrono::steady_clock::time_point> candidate_started_at_;
    StartupCacheNow now_;
    std::size_t valid_duration_samples_ = 0;
    double ema_candidate_seconds_ = 0.0;
    void refresh_latest_stage_locked() noexcept;
    void refresh_estimates_locked() noexcept;
};

} // namespace ff7r::piano::startup
