#pragma once

#include <filesystem>
#include <string>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline {

enum class SongDiscoveryCode {
    Completed,
    MissingRoot,
    InspectFailed,
    EnumerationFailed,
    WorkerStartFailed,
    WorkerFailed,
    SetupFailed,
};

struct SongCandidateResult {
    std::filesystem::path directory;
    std::string directory_name;
    Status status;
    std::optional<std::size_t> loaded_song_index;
    std::vector<std::string> trace_stages;
};

struct SongRepositoryResult {
    std::vector<LoadedSong> songs;
    std::vector<Status> errors;
    SongDiscoveryCode discovery_code = SongDiscoveryCode::Completed;
    Status discovery_status;
    bool enumeration_completed = false;
    std::size_t discovered_candidate_count = 0;
    std::vector<SongCandidateResult> candidates;
    bool settlement_observer_failed = false;
};

enum class SongLoadProgressStage : unsigned char {
    Inspecting,
    ValidatingCache,
    DecodingAudio,
    GeneratingChart,
    BuildingAudioCache,
    PublishingCache,
    Complete,
};

enum class SongLoadTerminalOutcome : unsigned char {
    InProgress,
    Succeeded,
    Failed,
};

struct SongRepositoryProgress {
    std::size_t candidate_index = 0;
    SongLoadProgressStage stage = SongLoadProgressStage::Inspecting;
    SongLoadTerminalOutcome outcome = SongLoadTerminalOutcome::InProgress;
};

// Stable worker-owned candidate state. A settlement callback receives a
// non-owning span over only the newly contiguous lexical delta. The span and
// every pointer/reference obtained from it are valid only until that callback
// returns and must not escape it.
struct SettledSongCandidate {
    std::filesystem::path directory;
    std::string directory_name;
    Status status;
    std::vector<std::string> trace_stages;
    LoadedSong song;
};

struct SongRepositorySettlementDelta {
    std::span<const SettledSongCandidate> candidates;
    std::size_t first_candidate_index = 0;
    std::size_t total_candidate_count = 0;
    bool fully_settled = false;
};

using SongLoadProgress = std::function<void(SongLoadProgressStage stage)>;

struct SongDiscoveryHooks {
    std::function<void()> before_setup;
    std::function<void(std::size_t)> after_enumeration;
    std::function<void(std::size_t)> before_worker_start;
    std::function<void(std::size_t)> before_candidate_load;
    std::function<void(std::size_t)> after_candidate_load;
    std::function<void(std::size_t)> after_worker_body;
    std::function<void(const SongRepositoryProgress&)> on_progress;
    std::function<void(const SongRepositorySettlementDelta&)> on_settled_delta;
};

struct ProfileActionComparison {
    std::size_t retained = 0;
    std::size_t replaced = 0;
    std::size_t removed = 0;
    std::size_t added = 0;
    double overlap = 1.0;
    bool nested = true;
};

ProfileActionComparison compare_profile_actions(const SongConfig& previous, const SongConfig& current);
std::string render_default_song_json(const std::filesystem::path& directory);
using SongLoadTrace = std::function<void(const char* stage)>;

Status load_song_directory(
    const std::string& song_directory,
    LoadedSong* out_song,
    SongLoadTrace trace = {},
    bool rebuild_invalid_cache = true,
    SongLoadProgress progress = {});
SongRepositoryResult discover_songs(const std::string& music_root);
SongRepositoryResult discover_songs(const std::string& music_root, const SongDiscoveryHooks& hooks);
SongRepositoryResult discover_songs(const std::filesystem::path& music_root);
SongRepositoryResult discover_songs(
    const std::filesystem::path& music_root, const SongDiscoveryHooks& hooks);

inline SongRepositoryResult discover_songs(const char* music_root) {
    return discover_songs(std::string(music_root));
}

inline SongRepositoryResult discover_songs(
    const char* music_root, const SongDiscoveryHooks& hooks) {
    return discover_songs(std::string(music_root), hooks);
}

} // namespace ff7rp::pipeline
