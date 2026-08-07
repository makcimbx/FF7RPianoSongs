#include "startup/startup_cache_progress.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ff7r::piano::startup {
namespace {

constexpr std::size_t stage_units(const ff7rp::pipeline::SongLoadProgressStage stage) noexcept
{
    return static_cast<std::size_t>(stage) + 1;
}

constexpr std::size_t kCandidateStageUnits =
    stage_units(ff7rp::pipeline::SongLoadProgressStage::Complete);
constexpr double kEmaAlpha = 0.25;
constexpr double kMaximumEtaSeconds = 24.0 * 60.0 * 60.0;
constexpr std::size_t kMinimumEstimateSamples = 1;
constexpr std::size_t kMaximumRebuildWorkers = 2;

constexpr bool is_rebuild_stage(const ff7rp::pipeline::SongLoadProgressStage stage) noexcept
{
    using Stage = ff7rp::pipeline::SongLoadProgressStage;
    switch (stage) {
    case Stage::DecodingAudio:
    case Stage::GeneratingChart:
    case Stage::BuildingAudioCache:
    case Stage::PublishingCache:
        return true;
    default:
        return false;
    }
}

std::size_t coarse_eta_seconds(const double seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0;
    const double bounded = std::min(seconds, kMaximumEtaSeconds);
    const double quantum = bounded < 60.0 ? 5.0 : (bounded < 3600.0 ? 60.0 : 900.0);
    const double rounded = std::ceil(bounded / quantum) * quantum;
    return static_cast<std::size_t>(std::min(rounded, kMaximumEtaSeconds));
}

} // namespace

StartupCacheProgress::StartupCacheProgress(StartupCacheNow now) noexcept
    : now_(std::move(now))
{
}

void StartupCacheProgress::begin(const std::size_t candidate_count) noexcept
{
    try {
        std::vector<unsigned char> candidate_stage_units(candidate_count, 0);
        std::vector<bool> candidate_complete(candidate_count, false);
        std::vector<bool> candidate_started(candidate_count, false);
        std::vector<bool> candidate_rebuild_observed(candidate_count, false);
        std::vector<std::chrono::steady_clock::time_point> candidate_started_at(candidate_count);
        std::lock_guard lock(mutex_);
        state_ = {};
        state_.phase = StartupCachePhase::Loading;
        state_.candidate_count = candidate_count;
        state_.total_stage_units = candidate_count * kCandidateStageUnits;
        candidate_stage_units_ = std::move(candidate_stage_units);
        candidate_complete_ = std::move(candidate_complete);
        candidate_started_ = std::move(candidate_started);
        candidate_rebuild_observed_ = std::move(candidate_rebuild_observed);
        candidate_started_at_ = std::move(candidate_started_at);
        valid_duration_samples_ = 0;
        ema_candidate_seconds_ = 0.0;
    } catch (...) {
    }
}

void StartupCacheProgress::observe(
    const ff7rp::pipeline::SongRepositoryProgress& progress) noexcept
{
    try {
        const auto now = now_ ? now_() : std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        if (state_.phase != StartupCachePhase::Loading ||
            progress.candidate_index >= candidate_stage_units_.size()) return;

        const auto candidate_index = progress.candidate_index;
        auto& retained_units = candidate_stage_units_[candidate_index];
        const bool was_started = candidate_started_[candidate_index];
        if (!was_started) {
            candidate_started_[candidate_index] = true;
            candidate_started_at_[candidate_index] = now;
            ++state_.active_candidate_count;
        }
        if (is_rebuild_stage(progress.stage)) {
            candidate_rebuild_observed_[candidate_index] = true;
        }
        const auto incoming_units = static_cast<unsigned char>(stage_units(progress.stage));
        if (incoming_units > retained_units) {
            state_.completed_stage_units += incoming_units - retained_units;
            retained_units = incoming_units;
        }
        if (progress.stage == ff7rp::pipeline::SongLoadProgressStage::Complete &&
            !candidate_complete_[candidate_index]) {
            if (progress.outcome == ff7rp::pipeline::SongLoadTerminalOutcome::Succeeded
                && candidate_rebuild_observed_[candidate_index] && was_started &&
                now > candidate_started_at_[candidate_index]) {
                const double sample = std::chrono::duration<double>(
                    now - candidate_started_at_[candidate_index]).count();
                if (std::isfinite(sample) && sample > 0.0) {
                    const double bounded_sample = std::min(sample, kMaximumEtaSeconds);
                    ema_candidate_seconds_ = valid_duration_samples_ != 0
                        ? kEmaAlpha * bounded_sample + (1.0 - kEmaAlpha) * ema_candidate_seconds_
                        : bounded_sample;
                    ++valid_duration_samples_;
                }
            }
            candidate_complete_[candidate_index] = true;
            if (state_.active_candidate_count != 0) --state_.active_candidate_count;
            ++state_.completed_candidates;
        }
        refresh_latest_stage_locked();
        refresh_estimates_locked();
    } catch (...) {
    }
}

void StartupCacheProgress::refresh_estimates_locked() noexcept
{
    state_.estimates_available = false;
    state_.estimated_per_song_seconds = 0;
    state_.estimated_total_remaining_seconds = 0;
    if (state_.phase != StartupCachePhase::Loading ||
        valid_duration_samples_ < kMinimumEstimateSamples ||
        !std::isfinite(ema_candidate_seconds_) || ema_candidate_seconds_ <= 0.0) return;

    const std::size_t remaining_candidates =
        state_.candidate_count > state_.completed_candidates
        ? state_.candidate_count - state_.completed_candidates : 0;
    if (remaining_candidates == 0) return;

    const double workers = static_cast<double>(
        std::min(kMaximumRebuildWorkers, remaining_candidates));
    const double total_estimate = ema_candidate_seconds_ *
        static_cast<double>(remaining_candidates) / workers;
    state_.estimated_per_song_seconds = coarse_eta_seconds(ema_candidate_seconds_);
    state_.estimated_total_remaining_seconds = coarse_eta_seconds(total_estimate);
    state_.estimates_available = state_.estimated_per_song_seconds != 0;
}

void StartupCacheProgress::refresh_latest_stage_locked() noexcept
{
    std::size_t highest_units = 0;
    for (std::size_t index = 0; index < candidate_stage_units_.size(); ++index) {
        if (!candidate_complete_[index]) {
            highest_units = std::max<std::size_t>(highest_units, candidate_stage_units_[index]);
        }
    }
    state_.latest_stage = highest_units == 0
        ? ff7rp::pipeline::SongLoadProgressStage::Inspecting
        : static_cast<ff7rp::pipeline::SongLoadProgressStage>(highest_units - 1);
}

void StartupCacheProgress::publishing() noexcept
{
    try {
        std::lock_guard lock(mutex_);
        if (state_.phase == StartupCachePhase::Loading) {
            state_.phase = StartupCachePhase::Publishing;
            state_.active_candidate_count = 0;
            state_.completed_stage_units = state_.total_stage_units;
            state_.completed_candidates = state_.candidate_count;
            state_.estimates_available = false;
            state_.estimated_per_song_seconds = 0;
            state_.estimated_total_remaining_seconds = 0;
        }
    } catch (...) {
    }
}

void StartupCacheProgress::ready(const std::size_t published_song_count) noexcept
{
    try {
        std::lock_guard lock(mutex_);
        if (state_.phase != StartupCachePhase::Failed) {
            state_.phase = StartupCachePhase::Ready;
            state_.active_candidate_count = 0;
            state_.published_song_count = published_song_count;
            state_.available_on_reopen_song_count = std::max(
                state_.available_on_reopen_song_count, published_song_count);
            state_.catalog_update_pending =
                state_.available_on_reopen_song_count > state_.active_song_count;
            if (!state_.catalog_update_pending) state_.adoption_deferred = false;
            state_.estimates_available = false;
            state_.estimated_per_song_seconds = 0;
            state_.estimated_total_remaining_seconds = 0;
        }
    } catch (...) {
    }
}

void StartupCacheProgress::available_on_reopen(const std::size_t song_count) noexcept
{
    catalog_prepared(song_count);
}

void StartupCacheProgress::catalog_prepared(const std::size_t song_count) noexcept
{
    try {
        std::lock_guard lock(mutex_);
        state_.available_on_reopen_song_count = std::max(
            state_.available_on_reopen_song_count, song_count);
        state_.catalog_update_pending =
            state_.available_on_reopen_song_count > state_.active_song_count;
        if (!state_.catalog_update_pending) state_.adoption_deferred = false;
    } catch (...) {
    }
}

void StartupCacheProgress::catalog_adopted(const std::size_t song_count) noexcept
{
    try {
        std::lock_guard lock(mutex_);
        state_.active_song_count = song_count;
        state_.catalog_update_pending =
            state_.available_on_reopen_song_count > state_.active_song_count;
        state_.adoption_deferred = false;
    } catch (...) {
    }
}

void StartupCacheProgress::catalog_adoption_deferred() noexcept
{
    try {
        std::lock_guard lock(mutex_);
        if (state_.available_on_reopen_song_count > state_.active_song_count) {
            state_.catalog_update_pending = true;
            state_.adoption_deferred = true;
        }
    } catch (...) {
    }
}

void StartupCacheProgress::failed() noexcept
{
    try {
        std::lock_guard lock(mutex_);
        if (state_.phase != StartupCachePhase::Ready) {
            state_.phase = StartupCachePhase::Failed;
            state_.active_candidate_count = 0;
            state_.estimates_available = false;
            state_.estimated_per_song_seconds = 0;
            state_.estimated_total_remaining_seconds = 0;
        }
    } catch (...) {
    }
}

StartupCacheProgressSnapshot StartupCacheProgress::snapshot() const noexcept
{
    try {
        std::lock_guard lock(mutex_);
        return state_;
    } catch (...) {
        return {};
    }
}

bool StartupCacheProgress::try_snapshot(StartupCacheProgressSnapshot& snapshot) const noexcept
{
    try {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        snapshot = state_;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace ff7r::piano::startup
