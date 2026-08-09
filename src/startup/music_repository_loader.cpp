#include "startup/music_repository_loader.h"

#include "core/logging.h"
#include "song_descriptor_builder.h"
#include "startup/startup_cache_progress.h"

#include <filesystem>
#include <exception>
#include <sstream>
#include <utility>
#include <vector>

namespace ff7r::piano::startup {
namespace {

// Widest count the 128-row piano list could ever accept. Offline discovery
// cannot know the live vanilla list length, so this is only a coarse bound;
// the exact one is enforced at catalog adoption against the live first row.
constexpr std::size_t kMaximumCustomSongCount = 123;

MusicRepositoryLogEvent event(
    const MusicRepositoryLogLevel level, std::string text)
{
    return {level, std::move(text)};
}

MusicRepositoryLogEvent discovery_started_event(const std::size_t candidate_count)
{
    return event(MusicRepositoryLogLevel::Info,
        "[song] status=discovery_started candidates=" + std::to_string(candidate_count));
}

std::string loaded_log(const ff7rp::pipeline::LoadedSong& song)
{
    std::ostringstream out;
    out << "[song] status=loaded id=" << song.id
        << " title=\"" << song.config.title << "\""
        << " notes=" << song.chart.notes.size()
        << " logical_pcm_frames=" << song.audio.source_frame_count
        << " audio_source_seconds=" << song.audio.source_duration_seconds()
        << " resident_pcm_frames=" << song.audio.frame_count()
        << " hca_frames=" << song.mabf_metadata.hca_frame_count
        << " gain_applied=" << song.loudness_gain_applied
        << " limiter_engaged=" << song.loudness_limiter_engaged
        << " cache=0x" << std::hex << song.cache_key
        << " hca_status=\"" << song.hca_status.message << "\"";
    return out.str();
}

void fail_plan(MusicRepositoryPlan& plan, std::string message)
{
    plan.ready_to_publish = false;
    plan.descriptors.clear();
    plan.events.push_back(event(MusicRepositoryLogLevel::Error,
        "[song] status=discovery_failed error=\"" + std::move(message) + "\""));
}

bool is_fatal_discovery(const ff7rp::pipeline::SongDiscoveryCode code)
{
    using Code = ff7rp::pipeline::SongDiscoveryCode;
    return code != Code::Completed && code != Code::MissingRoot;
}

void emit(const MusicRepositoryLogEvent& item)
{
    core::log(item.level == MusicRepositoryLogLevel::Error
            ? core::LogLevel::Error : core::LogLevel::Info,
        item.text);
}

class ProgressiveSettlementPublisher final {
public:
    ProgressiveSettlementPublisher(ProgressiveRepositoryState& state,
        const ProgressiveRepositoryCallbacks& callbacks,
        StartupCacheProgress* progress,
        const std::function<void()>& publish_state) noexcept
        : state_(state), callbacks_(callbacks), progress_(progress),
          publish_state_(publish_state) {}

    void fail_terminal() noexcept
    {
        state_.phase = ProgressiveRepositoryPhase::Failed;
        if (progress_) progress_->failed();
        try {
            emit(event(MusicRepositoryLogLevel::Error,
                "[song] status=repository_failed valid_count="
                    + std::to_string(state_.valid_song_count)));
        } catch (...) {}
        publish_state_();
    }

    bool admit_descriptor(const game::SongDescriptor& descriptor) const noexcept
    {
        try {
            return !callbacks_.on_descriptor_admission
                || callbacks_.on_descriptor_admission(descriptor);
        } catch (...) { return false; }
    }

    void observe(const std::vector<game::SongDescriptor>& descriptors,
        const std::size_t settled_count,
        const bool fully_settled)
    {
        if (settled_count < state_.settled_candidate_count
            || (settled_count == state_.settled_candidate_count && !fully_settled
                && descriptors.size() <= last_offered_count_)) return;
        state_.settled_candidate_count = settled_count;
        if (descriptors.size() > kMaximumCustomSongCount) {
            fail_terminal();
            return;
        }
        const bool equivalent = state_.has_valid_catalog
            && descriptors.size() == accepted_count_;
        const bool increased = descriptors.size() > last_offered_count_;
        if (!fully_settled && (equivalent || !increased)) {
            publish_state_();
            return;
        }

        if (fully_settled && progress_) {
            progress_->publishing();
            publish_state_();
        }
        if (descriptors.empty()) {
            if (!fully_settled) {
                publish_state_();
                return;
            }
            state_.has_valid_catalog = false;
            state_.valid_song_count = 0;
            state_.phase = ProgressiveRepositoryPhase::Settled;
            if (progress_) progress_->ready(0);
            try {
                emit(event(MusicRepositoryLogLevel::Info,
                    "[song] status=repository_settled valid_count=0"));
            } catch (...) {}
            publish_state_();
            return;
        }

        // Every increased valid count is offered at admission time. If the
        // final observation is the same prefix after a recoverable publication
        // rejection, retry exactly once through the callback's retained
        // immutable candidate; descriptor/audio preparation is not repeated.
        if (fully_settled && !equivalent && !increased) {
            bool accepted = false;
            if (last_offer_rejected_ && !final_retry_attempted_) {
                final_retry_attempted_ = true;
                try {
                    accepted = callbacks_.on_final_catalog_retry
                        && callbacks_.on_final_catalog_retry();
                } catch (...) { accepted = false; }
                try {
                    emit(event(accepted ? MusicRepositoryLogLevel::Info
                                        : MusicRepositoryLogLevel::Error,
                        std::string("[song] status=pending_")
                            + (accepted ? "accepted" : "rejected")
                            + " count=" + std::to_string(descriptors.size())
                            + " fully_settled=true final_retry=true"));
                } catch (...) {}
            }
            if (accepted) {
                accepted_count_ = descriptors.size();
                state_.has_valid_catalog = true;
                state_.valid_song_count = accepted_count_;
                state_.phase = ProgressiveRepositoryPhase::Settled;
                if (progress_) {
                    progress_->available_on_reopen(accepted_count_);
                    progress_->ready(accepted_count_);
                }
            } else {
                state_.phase = ProgressiveRepositoryPhase::Failed;
                if (progress_) progress_->failed();
            }
            try {
                emit(event(accepted ? MusicRepositoryLogLevel::Info
                                    : MusicRepositoryLogLevel::Error,
                    std::string("[song] status=repository_")
                        + (accepted ? "settled" : "failed") + " valid_count="
                        + std::to_string(state_.valid_song_count)));
            } catch (...) {}
            publish_state_();
            return;
        }

        bool accepted = equivalent;
        if (!accepted) {
            MusicRepositoryPlan plan;
            plan.ready_to_publish = true;
            plan.descriptors = descriptors;
            last_offered_count_ = descriptors.size();
            try {
                accepted = callbacks_.on_catalog
                    && callbacks_.on_catalog(std::move(plan), fully_settled);
            } catch (...) { accepted = false; }
            last_offer_rejected_ = !accepted;
            try {
                emit(event(accepted ? MusicRepositoryLogLevel::Info : MusicRepositoryLogLevel::Error,
                    std::string("[song] status=pending_") + (accepted ? "accepted" : "rejected")
                        + " count=" + std::to_string(descriptors.size())
                        + " fully_settled=" + (fully_settled ? "true" : "false")));
            } catch (...) {}
        }
        if (accepted) {
            if (!equivalent) accepted_count_ = descriptors.size();
            state_.has_valid_catalog = true;
            state_.valid_song_count = accepted_count_;
            if (progress_) progress_->available_on_reopen(accepted_count_);
            state_.phase = fully_settled
                ? ProgressiveRepositoryPhase::Settled
                : ProgressiveRepositoryPhase::CatalogAvailable;
            if (fully_settled && progress_)
                progress_->ready(state_.valid_song_count);
        } else if (fully_settled) {
            state_.phase = ProgressiveRepositoryPhase::Failed;
            if (progress_) progress_->failed();
        }
        if (fully_settled) {
            try {
                emit(event(state_.phase == ProgressiveRepositoryPhase::Settled
                        ? MusicRepositoryLogLevel::Info : MusicRepositoryLogLevel::Error,
                    std::string("[song] status=repository_")
                        + (state_.phase == ProgressiveRepositoryPhase::Settled ? "settled" : "failed")
                        + " valid_count=" + std::to_string(state_.valid_song_count)));
            } catch (...) {}
        }
        publish_state_();
    }

private:
    ProgressiveRepositoryState& state_;
    const ProgressiveRepositoryCallbacks& callbacks_;
    StartupCacheProgress* progress_;
    std::function<void()> publish_state_;
    // Descriptor accumulation is append-only and accepted entries are
    // immutable, so count equality proves exact accepted-prefix identity.
    std::size_t accepted_count_ = 0;
    std::size_t last_offered_count_ = 0;
    bool last_offer_rejected_ = false;
    bool final_retry_attempted_ = false;
};

class ProgressiveDeltaAccumulator final {
public:
    explicit ProgressiveDeltaAccumulator(ProgressiveSettlementPublisher& publisher)
        : publisher_(publisher) {}

    void observe(const ff7rp::pipeline::SongRepositorySettlementDelta& delta)
    {
        std::size_t candidate_index = delta.first_candidate_index;
        for (const auto& candidate : delta.candidates) {
            emit(event(MusicRepositoryLogLevel::Info,
                "[song] status=loading directory=\"" + candidate.directory_name + "\""));
            for (const auto& stage : candidate.trace_stages) {
                emit(event(MusicRepositoryLogLevel::Info,
                    "[song_load] directory=\"" + candidate.directory_name + "\" stage=" + stage));
            }
            if (!candidate.status.ok()) {
                emit(event(MusicRepositoryLogLevel::Error,
                    "[song] status=skipped directory=\"" + candidate.directory_name
                        + "\" error=\"" + candidate.status.message + "\""));
            } else {
                if (descriptors_.size() >= kMaximumCustomSongCount) {
                    throw std::length_error("validated custom-song count exceeds the existing piano-list bound");
                }
                game::SongDescriptor descriptor = build_song_descriptor(
                    candidate.song, game::kUnresolvedVisibleIndex);
                if (publisher_.admit_descriptor(descriptor)) {
                    descriptors_.push_back(std::move(descriptor));
                    emit(event(MusicRepositoryLogLevel::Info, loaded_log(candidate.song)));
                } else {
                    emit(event(MusicRepositoryLogLevel::Error,
                        "[song] status=skipped directory=\"" + candidate.directory_name
                            + "\" error=\"audio sidecar preparation failed\""));
                }
            }
            ++candidate_index;
            settled_count_ = candidate_index;
            publisher_.observe(descriptors_, settled_count_, false);
        }
    }

    void complete_final() { publisher_.observe(descriptors_, settled_count_, true); }

private:
    ProgressiveSettlementPublisher& publisher_;
    std::vector<game::SongDescriptor> descriptors_;
    std::size_t settled_count_ = 0;
};

} // namespace

MusicRepositoryPlan compose_music_repository(ff7rp::pipeline::SongRepositoryResult discovery)
{
    MusicRepositoryPlan plan;
    if (discovery.enumeration_completed ||
        discovery.discovery_code == ff7rp::pipeline::SongDiscoveryCode::Completed ||
        discovery.discovery_code == ff7rp::pipeline::SongDiscoveryCode::MissingRoot) {
        const std::size_t candidate_count = discovery.enumeration_completed
            ? discovery.discovered_candidate_count : discovery.candidates.size();
        plan.events.push_back(discovery_started_event(candidate_count));
    }
    if (is_fatal_discovery(discovery.discovery_code)) {
        fail_plan(plan, discovery.discovery_status.message);
        return plan;
    }

    plan.descriptors.reserve(discovery.songs.size());

    for (const auto& candidate : discovery.candidates) {
        plan.events.push_back(event(MusicRepositoryLogLevel::Info,
            "[song] status=loading directory=\"" + candidate.directory_name + "\""));
        for (const std::string& stage : candidate.trace_stages) {
            plan.events.push_back(event(MusicRepositoryLogLevel::Info,
                "[song_load] directory=\"" + candidate.directory_name + "\" stage=" + stage));
        }
        if (!candidate.status.ok()) {
            plan.events.push_back(event(MusicRepositoryLogLevel::Error,
                "[song] status=skipped directory=\"" + candidate.directory_name +
                    "\" error=\"" + candidate.status.message + "\""));
            continue;
        }
        if (!candidate.loaded_song_index ||
            *candidate.loaded_song_index >= discovery.songs.size()) {
            fail_plan(plan, "song descriptor composition failed for directory " +
                candidate.directory_name + ": missing loaded song");
            return plan;
        }

        const auto& song = discovery.songs[*candidate.loaded_song_index];
        if (plan.descriptors.size() >= kMaximumCustomSongCount) {
            fail_plan(plan,
                "validated custom-song count exceeds the existing piano-list bound");
            return plan;
        }
        try {
            plan.descriptors.push_back(
                build_song_descriptor(song, game::kUnresolvedVisibleIndex));
        } catch (const std::exception& error) {
            fail_plan(plan, "song descriptor composition failed for directory " +
                candidate.directory_name + ": " + error.what());
            return plan;
        } catch (...) {
            fail_plan(plan, "song descriptor composition failed for directory " +
                candidate.directory_name + ": unknown exception");
            return plan;
        }
        plan.events.push_back(event(MusicRepositoryLogLevel::Info, loaded_log(song)));
    }

    plan.ready_to_publish = true;
    return plan;
}

bool publish_music_repository(MusicRepositoryPlan&& plan, game::SongRegistry& registry,
    StartupCacheProgress* progress)
{
    if (!plan.ready_to_publish) return false;
    const std::size_t count = plan.descriptors.size();
    if (progress) progress->publishing();
    const auto previous = registry.registry_snapshot();
    registry.replace(std::move(plan.descriptors));
    const auto published = registry.registry_snapshot();
    if (!published || published.generation == previous.generation) return false;
    if (progress) progress->ready(count);
    return true;
}

bool load_music_repository(const std::wstring& dll_dir, StartupCacheProgress* progress)
{
    try {
        const std::filesystem::path music_root = std::filesystem::path(dll_dir) / L"Music";
        bool discovery_started_emitted = false;
        ff7rp::pipeline::SongDiscoveryHooks hooks;
        hooks.after_enumeration = [&](const std::size_t candidate_count) {
            discovery_started_emitted = true;
            if (progress) progress->begin(candidate_count);
            emit(discovery_started_event(candidate_count));
        };
        hooks.on_progress = [progress](const ff7rp::pipeline::SongRepositoryProgress& event) {
            if (progress) progress->observe(event);
        };
        auto discovery = ff7rp::pipeline::discover_songs(music_root, hooks);
        if (progress && !discovery_started_emitted) {
            progress->begin(discovery.discovered_candidate_count);
        }
        MusicRepositoryPlan plan = compose_music_repository(std::move(discovery));
        std::size_t first_event = 0;
        if (discovery_started_emitted && !plan.events.empty()) first_event = 1;
        for (; first_event < plan.events.size(); ++first_event) emit(plan.events[first_event]);
        const std::size_t count = plan.descriptors.size();
        if (!publish_music_repository(std::move(plan), game::registry(), progress)) {
            if (progress) progress->failed();
            return false;
        }
        core::log(core::LogLevel::Info,
            "[song] status=registry_ready count=" + std::to_string(count));
        return true;
    } catch (const std::exception& error) {
        if (progress) progress->failed();
        core::log(core::LogLevel::Error,
            "[song] status=discovery_failed error=\"unexpected music repository coordinator failure: " +
                std::string(error.what()) + "\"");
    } catch (...) {
        if (progress) progress->failed();
        core::log(core::LogLevel::Error,
            "[song] status=discovery_failed error=\"unexpected music repository coordinator failure: unknown exception\"");
    }
    return false;
}

ProgressiveRepositoryState run_progressive_music_repository(
    const std::wstring& dll_dir, const ProgressiveRepositoryCallbacks& callbacks,
    StartupCacheProgress* progress)
{
    ProgressiveRepositoryState state;
    const auto publish_state = [&]() noexcept {
        try { if (callbacks.on_state) callbacks.on_state(state); } catch (...) {}
    };
    publish_state();
    try {
        const std::filesystem::path music_root = std::filesystem::path(dll_dir) / L"Music";
        state.phase = ProgressiveRepositoryPhase::Discovering;
        publish_state();
        ff7rp::pipeline::SongDiscoveryHooks hooks;
        ProgressiveSettlementPublisher publisher(state, callbacks, progress, publish_state);
        ProgressiveDeltaAccumulator accumulator(publisher);
        hooks.after_enumeration = [&](const std::size_t count) {
            state.candidate_count = count;
            if (progress) progress->begin(count);
            emit(discovery_started_event(count));
            publish_state();
        };
        hooks.on_progress = [progress](const ff7rp::pipeline::SongRepositoryProgress& event) {
            if (progress) progress->observe(event);
        };
        hooks.on_settled_delta = [&](const ff7rp::pipeline::SongRepositorySettlementDelta& delta) {
            accumulator.observe(delta);
        };
        const auto discovery = ff7rp::pipeline::discover_songs(music_root, hooks);
        if (!discovery.enumeration_completed) {
            state.candidate_count = discovery.discovered_candidate_count;
            state.phase = is_fatal_discovery(discovery.discovery_code)
                ? ProgressiveRepositoryPhase::Failed : ProgressiveRepositoryPhase::Settled;
            if (progress) {
                if (state.phase == ProgressiveRepositoryPhase::Failed) progress->failed();
                else { progress->publishing(); progress->ready(0); }
            }
            emit(event(state.phase == ProgressiveRepositoryPhase::Failed
                    ? MusicRepositoryLogLevel::Error : MusicRepositoryLogLevel::Info,
                std::string("[song] status=repository_")
                    + (state.phase == ProgressiveRepositoryPhase::Failed ? "failed" : "settled")
                    + " valid_count=0"));
            publish_state();
        } else if (is_fatal_discovery(discovery.discovery_code)
            || discovery.settlement_observer_failed) {
            // An earlier valid prefix remains authoritative. Terminal failure is
            // explicit and never withdraws or replaces that catalog.
            publisher.fail_terminal();
        } else if (discovery.candidates.empty()) {
            state.phase = ProgressiveRepositoryPhase::Settled;
            if (progress) { progress->publishing(); progress->ready(0); }
            emit(event(MusicRepositoryLogLevel::Info,
                "[song] status=repository_settled valid_count=0"));
            publish_state();
        } else {
            accumulator.complete_final();
        }
    } catch (...) {
        state.phase = ProgressiveRepositoryPhase::Failed;
        if (progress) progress->failed();
        try {
            emit(event(MusicRepositoryLogLevel::Error,
                "[song] status=repository_failed valid_count="
                    + std::to_string(state.valid_song_count)));
        } catch (...) {}
        publish_state();
    }
    return state;
}

#ifdef FF7RP_MUSIC_REPOSITORY_SELFTEST
ProgressiveRepositoryState run_progressive_repository_test_sequence(
    const std::size_t candidate_count,
    std::vector<ProgressiveRepositoryTestSettlement> settlements,
    const ProgressiveRepositoryCallbacks& callbacks,
    StartupCacheProgress* progress,
    const bool terminal_worker_failure_after_settlements)
{
    ProgressiveRepositoryState state;
    state.phase = ProgressiveRepositoryPhase::Discovering;
    state.candidate_count = candidate_count;
    if (progress) progress->begin(candidate_count);
    const auto publish_state = [&]() noexcept {
        try { if (callbacks.on_state) callbacks.on_state(state); } catch (...) {}
    };
    ProgressiveSettlementPublisher publisher(state, callbacks, progress, publish_state);
    std::vector<game::SongDescriptor> admitted;
    std::size_t observed_descriptors = 0;
    bool composition_failed = false;
    for (auto& settlement : settlements) {
        if (!settlement.plan.ready_to_publish) {
            if (settlement.fully_settled) publisher.fail_terminal();
            else publish_state();
            continue;
        }
        if (settlement.plan.descriptors.size() < observed_descriptors) {
            publisher.fail_terminal();
            continue;
        }
        for (std::size_t index = observed_descriptors;
             index < settlement.plan.descriptors.size(); ++index) {
            if (admitted.size() >= kMaximumCustomSongCount) {
                publisher.fail_terminal();
                composition_failed = true;
                break;
            }
            auto descriptor = settlement.plan.descriptors[index];
            descriptor.visible_index = game::kUnresolvedVisibleIndex;
            if (publisher.admit_descriptor(descriptor)) admitted.push_back(std::move(descriptor));
            publisher.observe(admitted, settlement.settled_candidate_count, false);
        }
        observed_descriptors = settlement.plan.descriptors.size();
        if (settlement.fully_settled && !composition_failed) {
            publisher.observe(admitted, settlement.settled_candidate_count, true);
        }
    }
    if (terminal_worker_failure_after_settlements) publisher.fail_terminal();
    return state;
}
#endif

} // namespace ff7r::piano::startup
