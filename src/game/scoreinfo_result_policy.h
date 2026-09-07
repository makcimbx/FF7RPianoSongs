#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ff7r::piano::game {

enum class ScoreInfoResultCaller : std::uint8_t {
    Unavailable = 0,
    StyleSetup,
    Detail,
    ProgressSource,
    RankText,
    Thresholds,
};

enum class ScoreInfoResultCatalogRole : std::uint8_t {
    Unavailable = 0,
    StyleSetup,
    Detail,
    ProgressSource,
    RankText,
    Thresholds,
    MenuDetail,
    ListItem,
};

enum class ScoreInfoResultPhase : std::uint8_t {
    Empty = 0,
    StylePublished,
    PlaybackRevoked,
    DetailPublished,
    ProgressObserved,
    RankPublished,
    Closed,
    Poisoned,
    Retired,
};

enum class ScoreInfoResultCleanupAuthority : std::uint8_t {
    Absent = 0,
    Exact,
    Unrelated,
};

enum class ScoreInfoResultFailure : std::uint8_t {
    None = 0,
    CallerUnavailable,
    OriginalUnavailable,
    ResultWrapperMismatch,
    WrapperUnavailable,
    WrapperDiscriminatorUnavailable,
    SourceRowUnavailable,
    SourceRowDrift,
    SourceKeyUnavailable,
    SourceKeyMismatch,
    SourceDiscriminatorMismatch,
    SourceBgmMismatch,
    PlaybackUnavailable,
    AuthorityUnavailable,
    StorageMismatch,
    RegistryGenerationMismatch,
    SongMismatch,
    ProfileMismatch,
    ProfileIndexMismatch,
    TokenMismatch,
    CleanupUnrelated,
    PlaybackNotRevoked,
    DetailScopeMismatch,
    ConsumerThreadUnavailable,
    ConsumerThreadMismatch,
    GenerationMismatch,
    SupersessionBeforeClose,
    NoActiveAuthority,
    OutOfOrder,
    DuplicateCaller,
    RowConstructionFailed,
    PublicationFailed,
    RevalidationFailed,
    ListRetired,
    Shutdown,
};

enum class ScoreInfoResultControlFlowReason : std::uint8_t {
    CallbackRejected = 0,
    CallerUnavailable,
    Arg0Null,
    NonOutermost,
    PolicyDispatched,
};

struct ScoreInfoResultSourceIdentity {
    std::uint64_t row_key = 0;
    std::uint64_t wrapper_discriminator = 0;
    std::uint32_t bgm_comparison_id = 0;
    std::uint32_t bgm_number = 0;

    constexpr bool valid() const noexcept
    {
        return row_key != 0 && wrapper_discriminator != 0 && bgm_comparison_id != 0;
    }
};

struct ScoreInfoResultAuthorityIdentity {
    std::uintptr_t storage_identity = 0;
    std::uintptr_t song_identity = 0;
    std::uintptr_t profile_identity = 0;
    std::uint64_t registry_generation = 0;
    std::int32_t profile_index = -1;
    std::uint64_t token_registry_generation = 0;
    std::uint64_t token_route_generation = 0;
    std::uint64_t token_lease_generation = 0;
    std::uint64_t token_song_key = 0;
    std::uintptr_t token_controller = 0;
    std::uintptr_t token_slot = 0;
    std::uintptr_t token_bgm = 0;
    std::uintptr_t token_sound = 0;
    std::uint64_t token_request_handle = 0;

    constexpr bool valid() const noexcept
    {
        return storage_identity != 0 && song_identity != 0 && profile_identity != 0
            && registry_generation != 0 && profile_index >= 0
            && token_registry_generation != 0 && token_route_generation != 0
            && token_lease_generation != 0 && token_song_key != 0;
    }
};

struct ScoreInfoResultInvocationFacts {
    ScoreInfoResultCaller caller = ScoreInfoResultCaller::Unavailable;
    std::uintptr_t caller_rva = 0;
    bool caller_in_image = false;
    bool original_called = false;
    bool result_wrapper_exact = false;
    bool wrapper_readable = false;
    bool wrapper_discriminator_readable = false;
    bool source_row_readable = false;
    bool source_row_revalidated = false;
    bool playback_present = false;
    bool detail_scope_exact = false;
    ScoreInfoResultCleanupAuthority cleanup = ScoreInfoResultCleanupAuthority::Absent;
    std::uint32_t consumer_thread = 0;
    std::uint64_t next_generation = 0;
    ScoreInfoResultSourceIdentity source{};
    ScoreInfoResultAuthorityIdentity authority{};
    std::array<std::int32_t, 4> source_thresholds{};
};

struct ScoreInfoResultPolicyState {
    ScoreInfoResultPhase phase = ScoreInfoResultPhase::Empty;
    std::uint64_t generation = 0;
    std::uint32_t consumer_thread = 0;
    std::uint8_t retained_row_count = 0;
    bool playback_released = false;
    ScoreInfoResultSourceIdentity source{};
    ScoreInfoResultAuthorityIdentity authority{};
};

struct ScoreInfoResultTransition {
    bool accepted = false;
    bool publish = false;
    ScoreInfoResultFailure failure = ScoreInfoResultFailure::None;
    ScoreInfoResultPolicyState state{};
};

struct ScoreInfoResultControlFlowFacts {
    bool callback_accepted = false;
    ScoreInfoResultCaller caller = ScoreInfoResultCaller::Unavailable;
    bool caller_in_image = false;
    bool arg0_present = false;
    bool outermost = false;
};

constexpr ScoreInfoResultCaller scoreinfo_result_caller_for_catalog_role(
    const ScoreInfoResultCatalogRole role,
    const bool piano_detail_update_active) noexcept
{
    switch (role) {
    case ScoreInfoResultCatalogRole::StyleSetup:
        return ScoreInfoResultCaller::StyleSetup;
    case ScoreInfoResultCatalogRole::Detail:
        return piano_detail_update_active
            ? ScoreInfoResultCaller::Detail : ScoreInfoResultCaller::Unavailable;
    case ScoreInfoResultCatalogRole::ProgressSource:
        return piano_detail_update_active
            ? ScoreInfoResultCaller::ProgressSource : ScoreInfoResultCaller::Unavailable;
    case ScoreInfoResultCatalogRole::RankText:
        return piano_detail_update_active
            ? ScoreInfoResultCaller::RankText : ScoreInfoResultCaller::Unavailable;
    case ScoreInfoResultCatalogRole::Thresholds:
        return ScoreInfoResultCaller::Thresholds;
    case ScoreInfoResultCatalogRole::Unavailable:
    case ScoreInfoResultCatalogRole::MenuDetail:
    case ScoreInfoResultCatalogRole::ListItem:
        return ScoreInfoResultCaller::Unavailable;
    }
    return ScoreInfoResultCaller::Unavailable;
}

constexpr ScoreInfoResultControlFlowReason scoreinfo_result_control_flow_reason(
    const ScoreInfoResultControlFlowFacts& facts) noexcept
{
    if (!facts.callback_accepted) return ScoreInfoResultControlFlowReason::CallbackRejected;
    if (facts.caller == ScoreInfoResultCaller::Unavailable) {
        return ScoreInfoResultControlFlowReason::CallerUnavailable;
    }
    if (!facts.arg0_present) return ScoreInfoResultControlFlowReason::Arg0Null;
    if (!facts.outermost) return ScoreInfoResultControlFlowReason::NonOutermost;
    return ScoreInfoResultControlFlowReason::PolicyDispatched;
}

constexpr bool scoreinfo_result_control_flow_diagnostic_allowed(
    const ScoreInfoResultPhase phase,
    const ScoreInfoResultControlFlowFacts& facts,
    const std::uint8_t emitted_count) noexcept
{
    return phase != ScoreInfoResultPhase::Empty
        && phase != ScoreInfoResultPhase::Retired
        && (facts.caller != ScoreInfoResultCaller::Unavailable || facts.caller_in_image)
        && emitted_count < 8;
}

constexpr bool scoreinfo_result_source_equal(
    const ScoreInfoResultSourceIdentity& expected,
    const ScoreInfoResultSourceIdentity& current) noexcept
{
    return expected.row_key == current.row_key
        && expected.wrapper_discriminator == current.wrapper_discriminator
        && expected.bgm_comparison_id == current.bgm_comparison_id
        && expected.bgm_number == current.bgm_number;
}

constexpr ScoreInfoResultFailure scoreinfo_result_authority_failure(
    const ScoreInfoResultAuthorityIdentity& expected,
    const ScoreInfoResultAuthorityIdentity& current) noexcept
{
    if (!current.valid()) return ScoreInfoResultFailure::AuthorityUnavailable;
    if (expected.storage_identity != current.storage_identity) return ScoreInfoResultFailure::StorageMismatch;
    if (expected.registry_generation != current.registry_generation) return ScoreInfoResultFailure::RegistryGenerationMismatch;
    if (expected.song_identity != current.song_identity) return ScoreInfoResultFailure::SongMismatch;
    if (expected.profile_identity != current.profile_identity) return ScoreInfoResultFailure::ProfileMismatch;
    if (expected.profile_index != current.profile_index) return ScoreInfoResultFailure::ProfileIndexMismatch;
    if (expected.token_registry_generation != current.token_registry_generation
        || expected.token_route_generation != current.token_route_generation
        || expected.token_lease_generation != current.token_lease_generation
        || expected.token_song_key != current.token_song_key
        || expected.token_controller != current.token_controller
        || expected.token_slot != current.token_slot
        || expected.token_bgm != current.token_bgm
        || expected.token_sound != current.token_sound
        || expected.token_request_handle != current.token_request_handle) {
        return ScoreInfoResultFailure::TokenMismatch;
    }
    return ScoreInfoResultFailure::None;
}

constexpr ScoreInfoResultFailure scoreinfo_result_common_failure(
    const ScoreInfoResultInvocationFacts& facts) noexcept
{
    if (facts.caller == ScoreInfoResultCaller::Unavailable) return ScoreInfoResultFailure::CallerUnavailable;
    if (!facts.original_called) return ScoreInfoResultFailure::OriginalUnavailable;
    if (!facts.result_wrapper_exact) return ScoreInfoResultFailure::ResultWrapperMismatch;
    if (!facts.wrapper_readable) return ScoreInfoResultFailure::WrapperUnavailable;
    if (!facts.wrapper_discriminator_readable) return ScoreInfoResultFailure::WrapperDiscriminatorUnavailable;
    if (!facts.source_row_readable) return ScoreInfoResultFailure::SourceRowUnavailable;
    if (!facts.source_row_revalidated) return ScoreInfoResultFailure::SourceRowDrift;
    if (facts.source.row_key == 0) return ScoreInfoResultFailure::SourceKeyUnavailable;
    if (!facts.source.valid()) return ScoreInfoResultFailure::SourceBgmMismatch;
    return ScoreInfoResultFailure::None;
}

constexpr ScoreInfoResultPolicyState scoreinfo_result_poisoned(
    const ScoreInfoResultPolicyState& current) noexcept
{
    ScoreInfoResultPolicyState next = current;
    if (current.phase != ScoreInfoResultPhase::Empty
        && current.phase != ScoreInfoResultPhase::Retired) {
        next.phase = ScoreInfoResultPhase::Poisoned;
    }
    return next;
}

constexpr ScoreInfoResultTransition begin_scoreinfo_result_authority(
    const ScoreInfoResultPolicyState& current,
    const ScoreInfoResultInvocationFacts& facts) noexcept
{
    ScoreInfoResultTransition result{};
    result.state = current;
    if (facts.caller != ScoreInfoResultCaller::StyleSetup) {
        result.failure = ScoreInfoResultFailure::OutOfOrder;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    if (const auto failure = scoreinfo_result_common_failure(facts);
        failure != ScoreInfoResultFailure::None) {
        result.failure = failure;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    if (current.phase != ScoreInfoResultPhase::Empty
        && current.phase != ScoreInfoResultPhase::Retired
        && current.phase != ScoreInfoResultPhase::Closed) {
        result.failure = ScoreInfoResultFailure::SupersessionBeforeClose;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    if (!facts.playback_present) {
        result.failure = ScoreInfoResultFailure::PlaybackUnavailable;
        return result;
    }
    if (!facts.authority.valid()) {
        result.failure = ScoreInfoResultFailure::AuthorityUnavailable;
        return result;
    }
    if (facts.next_generation == 0 || facts.next_generation != current.generation + 1) {
        result.failure = ScoreInfoResultFailure::GenerationMismatch;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    result.accepted = true;
    result.publish = true;
    result.state = {};
    result.state.phase = ScoreInfoResultPhase::StylePublished;
    result.state.generation = facts.next_generation;
    result.state.retained_row_count = 1;
    result.state.source = facts.source;
    result.state.authority = facts.authority;
    return result;
}

constexpr ScoreInfoResultTransition revoke_scoreinfo_result_playback(
    const ScoreInfoResultPolicyState& current,
    const ScoreInfoResultAuthorityIdentity& authority) noexcept
{
    ScoreInfoResultTransition result{};
    result.state = current;
    if (current.phase == ScoreInfoResultPhase::Empty
        || current.phase == ScoreInfoResultPhase::Retired) {
        result.accepted = true;
        return result;
    }
    if (current.playback_released) {
        result.failure = ScoreInfoResultFailure::DuplicateCaller;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    const bool release_may_follow = current.phase == ScoreInfoResultPhase::StylePublished
        || current.phase == ScoreInfoResultPhase::DetailPublished
        || current.phase == ScoreInfoResultPhase::ProgressObserved
        || current.phase == ScoreInfoResultPhase::RankPublished
        || current.phase == ScoreInfoResultPhase::Closed;
    if (!release_may_follow) {
        result.failure = ScoreInfoResultFailure::OutOfOrder;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    result.failure = scoreinfo_result_authority_failure(current.authority, authority);
    if (result.failure != ScoreInfoResultFailure::None) {
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    result.accepted = true;
    result.state.playback_released = true;
    if (current.phase == ScoreInfoResultPhase::StylePublished) {
        result.state.phase = ScoreInfoResultPhase::PlaybackRevoked;
    }
    return result;
}

constexpr ScoreInfoResultTransition consume_scoreinfo_result_authority(
    const ScoreInfoResultPolicyState& current,
    const ScoreInfoResultInvocationFacts& facts) noexcept
{
    ScoreInfoResultTransition result{};
    result.state = current;
    if (current.phase == ScoreInfoResultPhase::Empty
        || current.phase == ScoreInfoResultPhase::Retired) {
        result.failure = ScoreInfoResultFailure::NoActiveAuthority;
        return result;
    }
    if (const auto failure = scoreinfo_result_common_failure(facts);
        failure != ScoreInfoResultFailure::None) {
        result.failure = failure;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    if (facts.caller == ScoreInfoResultCaller::Detail && !facts.detail_scope_exact) {
        result.failure = ScoreInfoResultFailure::DetailScopeMismatch;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    if (facts.cleanup == ScoreInfoResultCleanupAuthority::Unrelated) {
        result.failure = ScoreInfoResultFailure::CleanupUnrelated;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    if (facts.playback_present) {
        if (current.playback_released
            || facts.cleanup != ScoreInfoResultCleanupAuthority::Absent) {
            result.failure = ScoreInfoResultFailure::PlaybackNotRevoked;
            result.state = scoreinfo_result_poisoned(current);
            return result;
        }
    } else if (!current.playback_released) {
        result.failure = ScoreInfoResultFailure::PlaybackUnavailable;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    if (const auto failure = scoreinfo_result_authority_failure(current.authority, facts.authority);
        failure != ScoreInfoResultFailure::None) {
        result.failure = failure;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    if (!scoreinfo_result_source_equal(current.source, facts.source)) {
        result.failure = current.source.row_key != facts.source.row_key
            ? ScoreInfoResultFailure::SourceKeyMismatch
            : (current.source.wrapper_discriminator != facts.source.wrapper_discriminator
                ? ScoreInfoResultFailure::SourceDiscriminatorMismatch
                : ScoreInfoResultFailure::SourceBgmMismatch);
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }

    ScoreInfoResultPhase expected = ScoreInfoResultPhase::Poisoned;
    ScoreInfoResultPhase next = ScoreInfoResultPhase::Poisoned;
    switch (facts.caller) {
    case ScoreInfoResultCaller::Detail:
        next = ScoreInfoResultPhase::DetailPublished;
        if (facts.consumer_thread == 0) {
            result.failure = ScoreInfoResultFailure::ConsumerThreadUnavailable;
            result.state = scoreinfo_result_poisoned(current);
            return result;
        }
        break;
    case ScoreInfoResultCaller::ProgressSource:
        expected = ScoreInfoResultPhase::DetailPublished;
        next = ScoreInfoResultPhase::ProgressObserved;
        break;
    case ScoreInfoResultCaller::RankText:
        expected = ScoreInfoResultPhase::ProgressObserved;
        next = ScoreInfoResultPhase::RankPublished;
        break;
    case ScoreInfoResultCaller::Thresholds:
        expected = ScoreInfoResultPhase::RankPublished;
        next = ScoreInfoResultPhase::Closed;
        break;
    default:
        result.failure = ScoreInfoResultFailure::OutOfOrder;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    const bool detail_phase_exact = facts.caller == ScoreInfoResultCaller::Detail
        && (current.phase == ScoreInfoResultPhase::StylePublished
            || current.phase == ScoreInfoResultPhase::PlaybackRevoked);
    if (!detail_phase_exact && current.phase != expected) {
        result.failure = current.phase == next
            ? ScoreInfoResultFailure::DuplicateCaller
            : ScoreInfoResultFailure::OutOfOrder;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }
    const std::uint32_t expected_thread = facts.caller == ScoreInfoResultCaller::Detail
        ? facts.consumer_thread : current.consumer_thread;
    if (expected_thread == 0 || (facts.caller != ScoreInfoResultCaller::Detail
        && facts.consumer_thread != expected_thread)) {
        result.failure = facts.consumer_thread == 0
            ? ScoreInfoResultFailure::ConsumerThreadUnavailable
            : ScoreInfoResultFailure::ConsumerThreadMismatch;
        result.state = scoreinfo_result_poisoned(current);
        return result;
    }

    result.accepted = true;
    result.publish = facts.caller != ScoreInfoResultCaller::ProgressSource;
    result.state.phase = next;
    result.state.consumer_thread = expected_thread;
    if (result.publish) ++result.state.retained_row_count;
    return result;
}

constexpr ScoreInfoResultTransition retire_scoreinfo_result_authority(
    const ScoreInfoResultPolicyState& current,
    const ScoreInfoResultFailure reason) noexcept
{
    ScoreInfoResultTransition result{};
    result.accepted = true;
    result.failure = reason;
    result.state = current;
    result.state.phase = ScoreInfoResultPhase::Retired;
    result.state.consumer_thread = 0;
    result.state.retained_row_count = 0;
    result.state.source = {};
    result.state.authority = {};
    return result;
}

static_assert(std::is_trivially_copyable_v<ScoreInfoResultPolicyState>);
static_assert(std::is_trivially_copyable_v<ScoreInfoResultInvocationFacts>);
static_assert(std::is_trivially_copyable_v<ScoreInfoResultControlFlowFacts>);

} // namespace ff7r::piano::game
