#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Offline synthetic model of the exact restricted 513-row reserve transaction.
// It does not prove native ABI compatibility or runtime safety.
namespace ff7r::piano::game::synthetic_model {

inline constexpr std::size_t kNativeRows = 512;
inline constexpr std::size_t kPlayableRows = 513;

struct SourceRow {
    float time = 0;
    bool monotone = true;
    bool chord = false;
    uint32_t group = 0;
    int camera_transition = 0;
    bool ignore_sound = false;
    float strength = 0;
    uint64_t fname = 1;
    uint8_t assignment = 0;
    uint8_t lookup_path = 1;
};

struct Event {
    std::size_t source_row = 0;
    uint32_t ordinal = 0;
    float time = 0;
    bool callback_valid = true;
};

struct Chart {
    std::vector<Event> storage;
    std::size_t count = 0;
    std::size_t capacity = 0;
    float max_time = 0;
    bool tail_constructor_entered = false;
    bool tail_destructed = false;
    bool ownership_preserved = false;
    std::size_t max_write_count = 0;
};

enum class FailurePoint {
    None,
    PrefixValidation,
    FNameFind,
    ConstructorReturnedNonTail,
    ConstructorValidation,
    CallbackBuild,
    CallbackValidation,
    CallbackCleanupIdentityFailure,
    PreWriteMaxReadFailure,
    PreWriteMaxDrift,
    PostWriteMaxReadFailure,
    PostWriteMaxMismatch,
    PostCountValidation,
    PostCountRestoreFailure,
    PostCountExternalDrift,
};

struct CommitIdentity {
    std::uint64_t song = 1;
    std::uint64_t profile = 1;
    std::uint64_t selection_generation = 1;
    std::uint64_t policy_generation = 1;
    std::uint64_t registry_generation = 1;
    std::uint64_t route_generation = 1;
    std::uint64_t lease_generation = 1;
    std::uint64_t song_key = 1;
    std::uint64_t descriptor_hash = 1;
    std::uint64_t owner_generation = 1;
    std::uint64_t owner = 1;
    std::uint64_t wrapper = 1;
    std::uint64_t chart = 1;
    std::uint64_t header = 1;
    std::uint64_t allocation = 1;
};

struct PublicationState {
    bool committed = false;
    CommitIdentity identity{};
};

struct BuildRequest {
    const std::vector<SourceRow>* rows = nullptr;
    bool experiment_enabled = true;
    bool verified_1005 = true;
    bool exact_caller = true;
    bool exact_header = true;
    bool exact_thread = true;
    bool depth_one = true;
    bool exact_generation = true;
    bool global_claim = true;
    bool second_reserve_hit = false;
    std::size_t reserve_capacity = kPlayableRows;
    FailurePoint failure = FailurePoint::None;
};

struct Result {
    bool substituted = false;
    bool forwarded = false;
    bool prefix_validated = false;
    bool tail_constructed = false;
    bool constructor_returned_tail = false;
    bool callback_validated = false;
    bool count_committed = false;
    bool rolled_back = false;
    bool route_blocked = false;
    bool count_restore_proved = false;
    bool max_restore_proved = false;
};

Result run(const BuildRequest& request, Chart& chart);
void model_next_parser_reset(Chart& chart);
void model_expansion_begin(PublicationState& state);
void model_publish_result(const Result& result, const CommitIdentity& identity,
    PublicationState& state);
std::size_t model_published_count(PublicationState& state,
    const CommitIdentity& current, bool profile_eligible);
std::size_t model_presentation_published_count(PublicationState& state,
    const CommitIdentity& menu, const CommitIdentity* playback, bool profile_eligible);
void model_shutdown(PublicationState& state);

} // namespace ff7r::piano::game::synthetic_model
