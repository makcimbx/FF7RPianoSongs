#pragma once

#include "pipeline/song_repository.h"

#include <filesystem>
#include <optional>
#include <string>

namespace ff7rp::pipeline::detail {

struct SongCandidateIdentity {
    std::string lexical_key;
    std::string directory_name;
    bool used_legacy_narrow_encoding = false;
};

// Pure production-used projection helpers. The optional narrow identity lets
// tests cover the deterministic fallback without depending on host locale
// conversion failures.
SongCandidateIdentity project_song_candidate_identity(
    const std::filesystem::path& directory,
    std::optional<std::string> legacy_narrow_identity);
SongCandidateIdentity song_candidate_identity(
    const std::filesystem::path& directory);
void project_song_discovery_setup_failure(
    SongRepositoryResult& result, bool unknown_exception);

} // namespace ff7rp::pipeline::detail
