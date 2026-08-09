#include "startup/music_repository_loader.h"

#include "core/logging.h"
#include "pipeline/song_repository_detail.h"
#include "startup/startup_cache_progress.h"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {

using ff7r::piano::startup::MusicRepositoryPlan;
using ff7rp::pipeline::LoadedSong;
using ff7rp::pipeline::SongCandidateResult;
using ff7rp::pipeline::SongDiscoveryCode;
using ff7rp::pipeline::SongRepositoryResult;
using ff7rp::pipeline::Status;
using ff7rp::pipeline::StatusCode;

bool require(const bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "music_repository_loader_selftest: " << message << '\n';
    return false;
}

LoadedSong song(std::string id, std::string title)
{
    LoadedSong value;
    value.id = std::move(id);
    value.config.title = std::move(title);
    value.config.bpm = 120.0;
    value.cache_sidecar_path = "cache/runtime.mabf";
    value.hca_status = Status::ok_status();
    return value;
}

SongCandidateResult accepted(
    std::string name, const std::size_t song_index, std::vector<std::string> trace = {})
{
    SongCandidateResult result;
    result.directory = name;
    result.directory_name = std::move(name);
    result.status = Status::ok_status();
    result.loaded_song_index = song_index;
    result.trace_stages = std::move(trace);
    return result;
}

SongCandidateResult rejected(std::string name, std::string message)
{
    SongCandidateResult result;
    result.directory = name;
    result.directory_name = std::move(name);
    result.status = Status::error(StatusCode::InvalidJson, std::move(message));
    return result;
}

bool contains_event(const MusicRepositoryPlan& plan, const std::string& text)
{
    for (const auto& item : plan.events) {
        if (item.text == text) return true;
    }
    return false;
}

} // namespace

int main()
{
    bool ok = true;

    SongRepositoryResult ordered;
    ordered.songs.push_back(song("alpha", "Alpha"));
    ordered.songs.push_back(song("charlie", "Charlie"));
    ordered.candidates.push_back(accepted("AAlpha", 0, {"json_loaded", "chart_compiled"}));
    ordered.candidates.push_back(rejected("BRejected", "bad fixture"));
    ordered.candidates.push_back(accepted("CCharlie", 1));
MusicRepositoryPlan plan = ff7r::piano::startup::compose_music_repository(std::move(ordered));
    ok &= require(plan.ready_to_publish, "accepted and rejected discovery must be publishable");
    ok &= require(plan.descriptors.size() == 2, "only accepted songs must become descriptors");
    ok &= require(plan.descriptors[0].id == "alpha" && plan.descriptors[1].id == "charlie",
        "rejected candidates must not displace a later accepted song");
    ok &= require(plan.descriptors[0].visible_index
                == ff7r::piano::game::kUnresolvedVisibleIndex
            && plan.descriptors[1].visible_index
                == ff7r::piano::game::kUnresolvedVisibleIndex,
        "offline composition assigned a piano row it cannot know: the vanilla "
        "list length grows with story progress, so a first row fixed at "
        "discovery time silently drops every custom song once the game's own "
        "list is already that long");
    for (const int native_count : {5, 7}) {
        ff7r::piano::game::SongRegistryStorage resolved = plan.descriptors;
        ok &= require(ff7r::piano::game::assign_custom_rows(
                    resolved, native_count)
                && resolved[0].visible_index == native_count
                && resolved[1].visible_index == native_count + 1,
            "adoption must append custom rows contiguously after every live native length");
        ok &= require(!ff7r::piano::game::assign_custom_rows(
                    resolved, native_count)
                && !ff7r::piano::game::assign_custom_rows(
                    resolved, native_count - 1),
            "a catalog already resolved for one list length must not be re-based");
    }
    ok &= require(plan.events.size() == 9, "ordered event projection count changed");
    ok &= require(plan.events[1].text == "[song] status=loading directory=\"AAlpha\"" &&
        plan.events[2].text == "[song_load] directory=\"AAlpha\" stage=json_loaded" &&
        plan.events[5].text == "[song] status=loading directory=\"BRejected\"" &&
        plan.events[7].text == "[song] status=loading directory=\"CCharlie\"",
        "loading, trace, rejected, and loaded events must retain lexical candidate order");
    ok &= require(plan.events[4].text ==
        "[song] status=loaded id=alpha title=\"Alpha\" notes=0 logical_pcm_frames=0 "
        "audio_source_seconds=0 resident_pcm_frames=0 hca_frames=0 gain_applied=0 "
        "limiter_engaged=0 cache=0x0 hca_status=\"\"",
        "loaded event text changed");
    ok &= require(contains_event(plan,
        "[song] status=skipped directory=\"BRejected\" error=\"bad fixture\""),
        "rejected event text changed");

    ff7r::piano::game::SongRegistry registry;
    ff7r::piano::startup::StartupCacheProgress publication_progress;
    publication_progress.begin(3);
    const auto generation_before = registry.registry_snapshot().generation;
    ok &= require(ff7r::piano::startup::publish_music_repository(
            std::move(plan), registry, &publication_progress),
        "complete plan must publish");
    ok &= require(registry.registry_snapshot().generation == generation_before + 1 &&
        registry.custom_count() == 2, "complete plan must perform exactly one replacement");
    ok &= require(publication_progress.snapshot().phase ==
            ff7r::piano::startup::StartupCachePhase::Ready &&
        publication_progress.snapshot().published_song_count == 2,
        "Ready must be published only after the registry replacement succeeds");

    SongRepositoryResult descriptor_failure;
    descriptor_failure.songs.push_back(song("before", "Before"));
    descriptor_failure.candidates.push_back(accepted("ABefore", 0, {"json_loaded"}));
    descriptor_failure.candidates.push_back(rejected("BRejected", "late bad fixture"));
    descriptor_failure.candidates.push_back(accepted("CBrokenDescriptorInput", 4));
    MusicRepositoryPlan failed_descriptor =
        ff7r::piano::startup::compose_music_repository(std::move(descriptor_failure));
    const auto after_success = registry.registry_snapshot().generation;
    ok &= require(!failed_descriptor.ready_to_publish && failed_descriptor.descriptors.empty(),
        "descriptor composition failure must discard the complete draft");
    ok &= require(failed_descriptor.events.size() == 8 &&
        failed_descriptor.events[0].text == "[song] status=discovery_started candidates=3" &&
        failed_descriptor.events[1].text == "[song] status=loading directory=\"ABefore\"" &&
        failed_descriptor.events[2].text ==
            "[song_load] directory=\"ABefore\" stage=json_loaded" &&
        failed_descriptor.events[3].text.find("[song] status=loaded id=before") == 0 &&
        failed_descriptor.events[4].text == "[song] status=loading directory=\"BRejected\"" &&
        failed_descriptor.events[5].text ==
            "[song] status=skipped directory=\"BRejected\" error=\"late bad fixture\"" &&
        failed_descriptor.events[6].text ==
            "[song] status=loading directory=\"CBrokenDescriptorInput\"" &&
        failed_descriptor.events[7].text ==
            "[song] status=discovery_failed error=\"song descriptor composition failed for directory "
            "CBrokenDescriptorInput: missing loaded song\"",
        "late descriptor failure must retain the exact lexical event prefix then append failure");
    ok &= require(!ff7r::piano::startup::publish_music_repository(
        std::move(failed_descriptor), registry) &&
        registry.registry_snapshot().generation == after_success,
        "descriptor failure must not publish partial state");

    SongRepositoryResult fatal_setup;
    fatal_setup.discovery_code = SongDiscoveryCode::SetupFailed;
    fatal_setup.discovery_status = Status::error(StatusCode::IoError, "setup fixture");
    fatal_setup.songs.push_back(song("partial", "Partial"));
    fatal_setup.candidates.push_back(accepted("Partial", 0));
    MusicRepositoryPlan failed_setup =
        ff7r::piano::startup::compose_music_repository(std::move(fatal_setup));
    ok &= require(!failed_setup.ready_to_publish && failed_setup.descriptors.empty() &&
        failed_setup.events.size() == 1,
        "fatal setup must discard partial discovery and project one failure");
    ok &= require(!ff7r::piano::startup::publish_music_repository(
        std::move(failed_setup), registry) &&
        registry.registry_snapshot().generation == after_success,
        "fatal setup must not publish");

    for (const SongDiscoveryCode fatal_code : {
        SongDiscoveryCode::InspectFailed,
        SongDiscoveryCode::EnumerationFailed,
        SongDiscoveryCode::WorkerStartFailed,
        SongDiscoveryCode::WorkerFailed}) {
        SongRepositoryResult fatal;
        fatal.discovery_code = fatal_code;
        fatal.discovery_status = Status::error(StatusCode::IoError, "fatal fixture");
        MusicRepositoryPlan fatal_plan =
            ff7r::piano::startup::compose_music_repository(std::move(fatal));
        ok &= require(!ff7r::piano::startup::publish_music_repository(
            std::move(fatal_plan), registry) &&
            registry.registry_snapshot().generation == after_success,
            "fatal discovery and worker outcomes must not publish");
    }

    const auto progressive_plan = [](std::initializer_list<const char*> ids) {
        MusicRepositoryPlan value;
        value.ready_to_publish = true;
        int visible = 5;
        for (const char* id : ids) {
            ff7r::piano::game::SongDescriptor descriptor;
            descriptor.id = id;
            descriptor.visible_index = visible++;
            value.descriptors.push_back(std::move(descriptor));
        }
        return value;
    };
    using TestSettlement =
        ff7r::piano::startup::ProgressiveRepositoryTestSettlement;
    using ProgressivePhase = ff7r::piano::startup::ProgressiveRepositoryPhase;
    using CachePhase = ff7r::piano::startup::StartupCachePhase;

    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        std::size_t offers = 0;
        bool final_state_saw_publishing = false;
        std::vector<ProgressivePhase> phases;
        std::vector<CachePhase> cache_phases;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [&](MusicRepositoryPlan, bool) {
            ++offers;
            return true;
        };
        callbacks.on_state = [&](const auto& state) {
            phases.push_back(state.phase);
            const auto cache_phase = progressive_progress.snapshot().phase;
            cache_phases.push_back(cache_phase);
            final_state_saw_publishing = final_state_saw_publishing
                || cache_phase == CachePhase::Publishing;
        };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            2, {{progressive_plan({"alpha"}), 1, false},
                   {progressive_plan({"alpha"}), 2, true}},
            callbacks, &progressive_progress);
        const auto snapshot = progressive_progress.snapshot();
        ok &= require(offers == 1 && state.phase == ProgressivePhase::Settled
                && state.has_valid_catalog && state.valid_song_count == 1
                && snapshot.phase == CachePhase::Ready
                && snapshot.published_song_count == 1
                && snapshot.available_on_reopen_song_count == 1
                && final_state_saw_publishing
                && phases == std::vector<ProgressivePhase>{
                    ProgressivePhase::CatalogAvailable, ProgressivePhase::CatalogAvailable,
                    ProgressivePhase::Settled}
                && cache_phases == std::vector<CachePhase>{
                    CachePhase::Loading, CachePhase::Publishing, CachePhase::Ready},
            "invalid final tail must not re-offer an unchanged accepted catalog");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        std::size_t offers = 0;
        std::size_t final_retries = 0;
        std::size_t accepted_states = 0;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [&](MusicRepositoryPlan, bool) {
            ++offers;
            return false;
        };
        callbacks.on_final_catalog_retry = [&] {
            ++final_retries;
            return true;
        };
        callbacks.on_state = [&](const auto& state) {
            if (state.phase == ProgressivePhase::Settled
                && state.has_valid_catalog && state.valid_song_count == 1) {
                ++accepted_states;
            }
        };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            2, {{progressive_plan({"alpha"}), 1, false},
                   {progressive_plan({"alpha"}), 2, true}},
            callbacks, &progressive_progress);
        const auto snapshot = progressive_progress.snapshot();
        ok &= require(offers == 1 && final_retries == 1 && accepted_states == 1
                && state.phase == ProgressivePhase::Settled
                && state.has_valid_catalog && state.valid_song_count == 1
                && snapshot.phase == CachePhase::Ready
                && snapshot.available_on_reopen_song_count == 1,
            "unchanged rejected final prefix did not retry and publish exactly once");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        bool retained_a = false;
        std::size_t offers = 0;
        std::size_t retries = 0;
        std::size_t accepted_states = 0;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [&](MusicRepositoryPlan plan, bool) {
            retained_a = false; // production clears retry ownership before preparation.
            ++offers;
            if (plan.descriptors.size() == 1) retained_a = true;
            return false;
        };
        callbacks.on_final_catalog_retry = [&] {
            ++retries;
            return retained_a;
        };
        callbacks.on_state = [&](const auto& state) {
            if (state.phase == ProgressivePhase::Settled && state.has_valid_catalog)
                ++accepted_states;
        };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            2, {{progressive_plan({"alpha"}), 1, false},
                   {progressive_plan({"alpha", "beta"}), 2, false},
                   {progressive_plan({"alpha", "beta"}), 2, true}},
            callbacks, &progressive_progress);
        const auto snapshot = progressive_progress.snapshot();
        ok &= require(offers == 2 && retries == 1 && !retained_a
                && accepted_states == 0
                && state.phase == ProgressivePhase::Failed
                && !state.has_valid_catalog && state.valid_song_count == 0
                && snapshot.phase == CachePhase::Failed
                && snapshot.available_on_reopen_song_count == 0,
            "failed larger candidate retried stale prefix or reported larger Ready");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        std::size_t offers = 0;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [&](MusicRepositoryPlan, bool) {
            ++offers;
            return offers == 2;
        };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            2, {{progressive_plan({"alpha"}), 1, false},
                   {progressive_plan({"alpha", "bravo"}), 2, true}},
            callbacks, &progressive_progress);
        ok &= require(offers == 2
                && state.phase == ProgressivePhase::Settled
                && state.has_valid_catalog && state.valid_song_count == 2
                && progressive_progress.snapshot().phase == CachePhase::Ready
                && progressive_progress.snapshot().available_on_reopen_song_count == 2,
            "a larger final catalog must retry after an earlier rejection and complete Ready");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [](MusicRepositoryPlan, bool) -> bool {
            throw std::runtime_error("injected catalog callback failure");
        };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            1, {{progressive_plan({"alpha"}), 1, true}}, callbacks, &progressive_progress);
        ok &= require(state.phase == ProgressivePhase::Failed && !state.has_valid_catalog
                && state.valid_song_count == 0
                && progressive_progress.snapshot().phase == CachePhase::Failed
                && progressive_progress.snapshot().available_on_reopen_song_count == 0,
            "throwing final catalog callback must be contained and fail terminal progress");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        std::size_t offers = 0;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [&](MusicRepositoryPlan, bool) { return ++offers == 1; };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            2, {{progressive_plan({"alpha"}), 1, false},
                   {progressive_plan({"alpha", "bravo"}), 2, true}},
            callbacks, &progressive_progress);
        ok &= require(state.phase == ProgressivePhase::Failed && state.has_valid_catalog
                && state.valid_song_count == 1
                && progressive_progress.snapshot().phase == CachePhase::Failed
                && progressive_progress.snapshot().available_on_reopen_song_count == 1,
            "rejected final preparation must retain an earlier accepted valid catalog");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [](MusicRepositoryPlan, bool) { return true; };
        MusicRepositoryPlan failed_final;
        failed_final.ready_to_publish = false;
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            2, {{progressive_plan({"alpha"}), 1, false},
                   {std::move(failed_final), 2, true}},
            callbacks, &progressive_progress);
        ok &= require(state.phase == ProgressivePhase::Failed && state.has_valid_catalog
                && state.valid_song_count == 1
                && progressive_progress.snapshot().phase == CachePhase::Failed,
            "terminal composition failure must retain an earlier accepted catalog and fail overlay");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [](MusicRepositoryPlan, bool) { return true; };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            1, {{progressive_plan({"alpha"}), 1, false}}, callbacks,
            &progressive_progress, true);
        ok &= require(state.phase == ProgressivePhase::Failed && state.has_valid_catalog
                && state.valid_song_count == 1
                && progressive_progress.snapshot().phase == CachePhase::Failed,
            "worker-body failure after settled deltas did not retain the earlier accepted catalog");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        std::size_t offers = 0;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [&](MusicRepositoryPlan, bool) { ++offers; return true; };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            1, {{progressive_plan({}), 1, true}}, callbacks, &progressive_progress);
        const auto snapshot = progressive_progress.snapshot();
        ok &= require(offers == 0 && state.phase == ProgressivePhase::Settled
                && !state.has_valid_catalog && state.valid_song_count == 0
                && snapshot.phase == CachePhase::Ready && snapshot.published_song_count == 0,
            "all-invalid settlement must deterministically complete Ready(0)");
    }
    {
        std::vector<std::size_t> offered_counts;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [&](MusicRepositoryPlan offered, bool) {
            offered_counts.push_back(offered.descriptors.size());
            return true;
        };
        std::vector<TestSettlement> settlements;
        for (std::size_t count = 1; count <= 9; ++count) {
            MusicRepositoryPlan plan_value;
            plan_value.ready_to_publish = true;
            for (std::size_t index = 0; index < count; ++index) {
                ff7r::piano::game::SongDescriptor descriptor;
                descriptor.id = "song-" + std::to_string(index);
                descriptor.visible_index = 5 + static_cast<int>(index);
                plan_value.descriptors.push_back(std::move(descriptor));
            }
            settlements.push_back({std::move(plan_value), count, count == 9});
        }
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            9, std::move(settlements), callbacks);
        ok &= require(offered_counts == std::vector<std::size_t>{1, 2, 3, 4, 5, 6, 7, 8, 9}
                && state.phase == ProgressivePhase::Settled
                && state.valid_song_count == 9,
            "progressive offers must publish every newly ready valid-song prefix");
    }
    {
        std::vector<std::size_t> offered_counts;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [&](MusicRepositoryPlan offered, bool) {
            offered_counts.push_back(offered.descriptors.size());
            return true;
        };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            3, {{progressive_plan({"alpha", "bravo", "charlie"}), 3, true}}, callbacks);
        ok &= require(offered_counts == std::vector<std::size_t>{1, 2, 3}
                && state.phase == ProgressivePhase::Settled && state.valid_song_count == 3,
            "a multi-song settlement delta did not offer each admitted prefix immediately");
    }
    {
        std::vector<std::size_t> offered_counts;
        std::vector<std::string> admitted_ids;
        bool admitted_rows_unresolved = true;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_descriptor_admission = [&](const ff7r::piano::game::SongDescriptor& descriptor) {
            if (descriptor.id == "bravo") return false;
            admitted_rows_unresolved = admitted_rows_unresolved
                && descriptor.visible_index
                    == ff7r::piano::game::kUnresolvedVisibleIndex;
            admitted_ids.push_back(descriptor.id);
            return true;
        };
        callbacks.on_catalog = [&](MusicRepositoryPlan offered, bool) {
            offered_counts.push_back(offered.descriptors.size());
            return true;
        };
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            3, {{progressive_plan({"alpha", "bravo", "charlie"}), 3, true}}, callbacks);
        ok &= require(admitted_ids == std::vector<std::string>{"alpha", "charlie"}
                && admitted_rows_unresolved
                && offered_counts == std::vector<std::size_t>{1, 2}
                && state.valid_song_count == 2,
            "failed sidecar admission blocked a later valid song or resolved a row offline");
    }
    {
        ff7r::piano::startup::StartupCacheProgress progressive_progress;
        ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
        callbacks.on_catalog = [](MusicRepositoryPlan, bool) { return true; };
        std::size_t admission_count = 0;
        callbacks.on_descriptor_admission = [&](const ff7r::piano::game::SongDescriptor&) {
            ++admission_count;
            return true;
        };
        MusicRepositoryPlan oversized_plan;
        oversized_plan.ready_to_publish = true;
        for (std::size_t index = 0; index < 124; ++index) {
            ff7r::piano::game::SongDescriptor descriptor;
            descriptor.id = "song-" + std::to_string(index);
            descriptor.visible_index = 5 + static_cast<int>(index);
            oversized_plan.descriptors.push_back(std::move(descriptor));
        }
        const auto state = ff7r::piano::startup::run_progressive_repository_test_sequence(
            124, {{progressive_plan({"alpha"}), 1, false},
                     {std::move(oversized_plan), 124, true}},
            callbacks, &progressive_progress);
        ok &= require(state.phase == ProgressivePhase::Failed && state.has_valid_catalog
                && state.valid_song_count == 123
                && admission_count == 123
                && progressive_progress.snapshot().phase == CachePhase::Failed,
            "unadoptable descriptor count did not fail before allocation while retaining the latest accepted catalog");
    }

    const std::filesystem::path temp =
        std::filesystem::temp_directory_path() / "ff7rp_music_repository_loader_selftest";
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp / "ZSecond", ec);
    std::filesystem::create_directories(temp / "AFirst", ec);
    ok &= require(!ec, "failed to create discovery fixture");

    std::mutex mutex;
    std::condition_variable changed;
    bool second_completed = false;
    std::vector<std::size_t> completion_order;
    std::vector<std::size_t> settled_prefixes;
    std::array<int, 2> delta_visits{};
    bool delta_views_valid = true;
    std::vector<ff7rp::pipeline::SongLoadTerminalOutcome> terminal_outcomes;
    ff7rp::pipeline::SongDiscoveryHooks hooks;
    ff7r::piano::startup::StartupCacheProgress reversed_progress;
    hooks.after_enumeration = [&](const std::size_t count) {
        reversed_progress.begin(count);
    };
    hooks.on_progress = [&](const ff7rp::pipeline::SongRepositoryProgress& progress) {
        reversed_progress.observe(progress);
        if (progress.stage == ff7rp::pipeline::SongLoadProgressStage::Complete)
            terminal_outcomes.push_back(progress.outcome);
    };
    hooks.before_candidate_load = [&](const std::size_t index) {
        if (index != 0) return;
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return second_completed; });
    };
    hooks.after_candidate_load = [&](const std::size_t index) {
        {
            std::lock_guard lock(mutex);
            completion_order.push_back(index);
            if (index == 1) second_completed = true;
        }
        changed.notify_all();
    };
    hooks.on_settled_delta = [&](const ff7rp::pipeline::SongRepositorySettlementDelta& delta) {
        settled_prefixes.push_back(delta.first_candidate_index + delta.candidates.size());
        for (std::size_t offset = 0; offset < delta.candidates.size(); ++offset) {
            const std::size_t index = delta.first_candidate_index + offset;
            ++delta_visits[index];
            delta_views_valid = delta_views_valid
                && delta.candidates[offset].directory_name
                    == (index == 0 ? "AFirst" : "ZSecond");
        }
    };
    SongRepositoryResult reversed = ff7rp::pipeline::discover_songs(temp.string(), hooks);
    ok &= require(completion_order.size() == 2 && completion_order[0] == 1 && completion_order[1] == 0,
        "fixture must force reverse worker completion without timing assumptions");
    ok &= require(settled_prefixes.size() == 1 && settled_prefixes[0] == 2,
        "later completion must stay private until the longest lexical prefix settles");
    ok &= require(delta_visits == std::array<int, 2>{1, 1} && delta_views_valid,
        "settlement delta did not expose each stable candidate exactly once synchronously");
    const auto reversed_snapshot = reversed_progress.snapshot();
    ok &= require(reversed_snapshot.completed_candidates == 2 &&
        reversed_snapshot.completed_stage_units == reversed_snapshot.total_stage_units,
        "typed candidate progress must complete monotonically under reverse worker ordering");
    ok &= require(terminal_outcomes.size() == 2
            && std::all_of(terminal_outcomes.begin(), terminal_outcomes.end(), [](const auto outcome) {
                return outcome == ff7rp::pipeline::SongLoadTerminalOutcome::Failed;
            }),
        "invalid candidates must emit typed Failed terminal outcomes");
    ok &= require(reversed.candidates.size() == 2 &&
        reversed.candidates[0].directory_name == "AFirst" &&
        reversed.candidates[1].directory_name == "ZSecond",
        "joined discovery result must retain lexical order independent of completion");

    ff7rp::pipeline::SongDiscoveryHooks observer_failure_hooks;
    observer_failure_hooks.on_progress = [](const ff7rp::pipeline::SongRepositoryProgress&) {
        throw std::runtime_error("injected progress observer failure");
    };
    const SongRepositoryResult observer_failure_result =
        ff7rp::pipeline::discover_songs(temp.string(), observer_failure_hooks);
    ok &= require(observer_failure_result.discovery_code == SongDiscoveryCode::Completed &&
        observer_failure_result.candidates.size() == 2,
        "progress observer failure must not alter repository discovery results");

    ff7rp::pipeline::SongDiscoveryHooks settlement_failure_hooks;
    settlement_failure_hooks.on_settled_delta =
        [](const ff7rp::pipeline::SongRepositorySettlementDelta&) {
            throw std::runtime_error("injected settlement observer failure");
        };
    const SongRepositoryResult settlement_failure_result =
        ff7rp::pipeline::discover_songs(temp.string(), settlement_failure_hooks);
    ok &= require(settlement_failure_result.discovery_code == SongDiscoveryCode::Completed
            && settlement_failure_result.settlement_observer_failed
            && settlement_failure_result.candidates.size() == 2,
        "settlement observer failure must be contained and explicitly reported");

    SongRepositoryResult projected_setup_failure;
    projected_setup_failure.songs.push_back(song("partial", "Partial"));
    projected_setup_failure.errors.push_back(Status::error(StatusCode::InvalidJson, "partial"));
    projected_setup_failure.candidates.push_back(accepted("Partial", 0));
    ff7rp::pipeline::detail::project_song_discovery_setup_failure(
        projected_setup_failure, false);
    ok &= require(projected_setup_failure.discovery_code == SongDiscoveryCode::SetupFailed &&
        projected_setup_failure.discovery_status.code == StatusCode::IoError &&
        projected_setup_failure.discovery_status.message ==
            "unexpected song discovery setup failure" &&
        projected_setup_failure.songs.empty() &&
        projected_setup_failure.candidates.empty() &&
        projected_setup_failure.errors.size() == 1,
        "legacy string conversion failures must use the established contained setup projection");
    const SongRepositoryResult legacy_missing =
        ff7rp::pipeline::discover_songs((temp / "LegacyMissing").string());
    ok &= require(legacy_missing.discovery_code == SongDiscoveryCode::MissingRoot,
        "legacy no-hooks string overload must delegate through the protected overload");

    const std::filesystem::path ordering_root = temp / "Ordering";
    const std::vector<std::filesystem::path> ordering_directories{
        ordering_root / "ZAscii",
        ordering_root / "AAscii",
#ifdef _WIN32
        ordering_root / L"é-Narrow-Or-Fallback",
#else
        ordering_root / "é-Narrow-Or-Fallback",
#endif
    };
    std::filesystem::create_directories(ordering_root, ec);
    std::vector<ff7rp::pipeline::detail::SongCandidateIdentity> expected_identities;
    for (const auto& directory : ordering_directories) {
        std::filesystem::create_directories(directory, ec);
        const auto identity = ff7rp::pipeline::detail::song_candidate_identity(directory);
        try {
            const std::string legacy_identity = directory.filename().string();
            ok &= require(identity.used_legacy_narrow_encoding &&
                identity.lexical_key == legacy_identity &&
                identity.directory_name == legacy_identity,
                "representable candidate identity must exactly match legacy filename().string()");
        } catch (const std::system_error&) {
            ok &= require(!identity.used_legacy_narrow_encoding,
                "failed narrow conversion must use fallback identity");
        } catch (const std::range_error&) {
            ok &= require(!identity.used_legacy_narrow_encoding,
                "failed narrow conversion must use fallback identity");
        }
        expected_identities.push_back(identity);
    }
    std::sort(expected_identities.begin(), expected_identities.end(),
        [](const auto& a, const auto& b) { return a.lexical_key < b.lexical_key; });
    const SongRepositoryResult ordered_identity =
        ff7rp::pipeline::discover_songs(ordering_root);
    ok &= require(ordered_identity.candidates.size() == expected_identities.size(),
        "identity fixture candidate count changed");
    for (std::size_t index = 0;
         index < ordered_identity.candidates.size() && index < expected_identities.size();
         ++index) {
        ok &= require(ordered_identity.candidates[index].directory_name ==
                expected_identities[index].directory_name,
            "candidate ordering and projected directory name must use the exact legacy narrow identity");
    }
    const auto explicit_fallback =
        ff7rp::pipeline::detail::project_song_candidate_identity(
#ifdef _WIN32
            std::filesystem::path(L"音楽-fallback"),
#else
            std::filesystem::path("音楽-fallback"),
#endif
            std::nullopt);
    ok &= require(!explicit_fallback.used_legacy_narrow_encoding &&
        explicit_fallback.lexical_key == explicit_fallback.directory_name &&
        !explicit_fallback.directory_name.empty(),
        "unrepresentable narrow names must use one deterministic fail-closed UTF-8 fallback identity");

    std::size_t enumerated_count = 0;
    ff7rp::pipeline::SongDiscoveryHooks worker_start_failure_hooks;
    worker_start_failure_hooks.after_enumeration =
        [&](const std::size_t count) { enumerated_count = count; };
    worker_start_failure_hooks.before_worker_start =
        [](std::size_t) { throw std::runtime_error("worker start fixture"); };
    SongRepositoryResult worker_start_failure =
        ff7rp::pipeline::discover_songs(temp, worker_start_failure_hooks);
    MusicRepositoryPlan worker_start_plan =
        ff7r::piano::startup::compose_music_repository(std::move(worker_start_failure));
    ok &= require(enumerated_count == 3 && worker_start_plan.events.size() == 2 &&
        worker_start_plan.events[0].text == "[song] status=discovery_started candidates=3" &&
        worker_start_plan.events[1].text ==
            "[song] status=discovery_failed error=\"failed to start song discovery workers\"",
        "worker-start failure must retain the pre-worker discovery marker");
    ok &= require(!ff7r::piano::startup::publish_music_repository(
        std::move(worker_start_plan), registry) &&
        registry.registry_snapshot().generation == after_success,
        "worker-start failure must not publish");

    enumerated_count = 0;
    ff7rp::pipeline::SongDiscoveryHooks worker_body_failure_hooks;
    worker_body_failure_hooks.after_enumeration =
        [&](const std::size_t count) { enumerated_count = count; };
    worker_body_failure_hooks.after_worker_body = [](std::size_t) {
        throw std::runtime_error("worker body fixture");
    };
    std::size_t worker_body_settled_candidates = 0;
    worker_body_failure_hooks.on_settled_delta = [&](const auto& delta) {
        worker_body_settled_candidates += delta.candidates.size();
    };
    SongRepositoryResult worker_body_failure =
        ff7rp::pipeline::discover_songs(temp, worker_body_failure_hooks);
    MusicRepositoryPlan worker_body_plan =
        ff7r::piano::startup::compose_music_repository(std::move(worker_body_failure));
    ok &= require(enumerated_count == 3 && worker_body_settled_candidates == 3
        && worker_body_plan.events.size() == 2 &&
        worker_body_plan.events[0].text == "[song] status=discovery_started candidates=3" &&
        worker_body_plan.events[1].text ==
            "[song] status=discovery_failed error=\"song discovery worker failed outside a candidate load\"",
        "worker-body failure must retain the pre-worker discovery marker");
    ok &= require(!ff7r::piano::startup::publish_music_repository(
        std::move(worker_body_plan), registry) &&
        registry.registry_snapshot().generation == after_success,
        "worker-body failure must not publish");

    const std::filesystem::path native_missing =
#ifdef _WIN32
        temp / L"音楽-日本語" / L"Missing";
#else
        temp / "native-path" / "Missing";
#endif
    SongRepositoryResult missing = ff7rp::pipeline::discover_songs(native_missing);
    ok &= require(missing.discovery_code == SongDiscoveryCode::MissingRoot &&
        missing.candidates.empty(), "missing root identity changed");
    MusicRepositoryPlan empty = ff7r::piano::startup::compose_music_repository(std::move(missing));
    ok &= require(empty.ready_to_publish && empty.descriptors.empty() &&
        empty.events.size() == 1 &&
        empty.events[0].text == "[song] status=discovery_started candidates=0",
        "production missing-root behavior must remain an empty successful repository");
    ok &= require(ff7r::piano::startup::publish_music_repository(std::move(empty), registry) &&
        registry.registry_snapshot().generation == after_success + 1 && registry.custom_count() == 0,
        "missing root must publish exactly one empty registry");

#ifdef _WIN32
    const std::filesystem::path native_dll_dir = temp / L"起動-é";
    std::filesystem::create_directories(native_dll_dir, ec);
    const std::filesystem::path log_path = temp / "native_missing_root.log";
    ff7r::piano::core::set_log_path(log_path.wstring());
    ff7r::piano::core::set_log_level(ff7r::piano::core::LogLevel::Info);
    const auto singleton_generation =
        ff7r::piano::game::registry().registry_snapshot().generation;
    ok &= require(ff7r::piano::startup::load_music_repository(native_dll_dir.wstring()),
        "production coordinator must accept a non-ASCII native Windows path");
    ok &= require(ff7r::piano::game::registry().registry_snapshot().generation ==
            singleton_generation + 1 &&
        ff7r::piano::game::registry().custom_count() == 0,
        "production missing root must replace the singleton once with an empty registry");
    std::ifstream log(log_path, std::ios::binary);
    const std::string log_text{
        std::istreambuf_iterator<char>(log), std::istreambuf_iterator<char>()};
    const std::string marker = "[song] status=discovery_started candidates=0";
    const std::string ready = "[song] status=registry_ready count=0";
    const std::size_t marker_position = log_text.find(marker);
    const std::size_t ready_position = log_text.find(ready);
    ok &= require(marker_position != std::string::npos &&
        ready_position != std::string::npos && marker_position < ready_position,
        "production missing root must log the marker before exact registry_ready");
#endif

    std::filesystem::remove_all(temp, ec);
    if (!ok) return 1;
    std::cout << "music_repository_loader_selftest ok\n";
    return 0;
}
