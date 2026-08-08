#pragma once

#include "game/onmemory_bank_diagnostic.h"
#include "game/uobject_lifetime.h"

#include <cstdint>
#include <string>
#include <type_traits>

namespace ff7r::piano::game {

enum class OnMemoryBankLifecyclePhase : uint8_t {
    Idle,
    CanonicalQualified,
    RestoreApplied,
    ReleaseClaimed,
    ResidencyPending,
    Complete,
    Failed,
};

enum class OnMemoryBankRouteDecision : uint8_t {
    Allowed,
    VanillaUnaffected,
    UnsupportedBuild,
    LookupSignatureUnavailable,
    ReleaseSignatureUnavailable,
    ReleasePending,
    LifecycleFailed,
    SoundIdentityInvalid,
    OwnerTokenInvalid,
    OwnerTokenConflict,
    SnapshotDrift,
    CanonicalKindInvalid,
};

inline const char* onmemory_bank_lifecycle_phase_name(
    OnMemoryBankLifecyclePhase phase) noexcept
{
    switch (phase) {
    case OnMemoryBankLifecyclePhase::Idle: return "idle";
    case OnMemoryBankLifecyclePhase::CanonicalQualified: return "canonical_qualified";
    case OnMemoryBankLifecyclePhase::RestoreApplied: return "restore_applied";
    case OnMemoryBankLifecyclePhase::ReleaseClaimed: return "release_claimed";
    case OnMemoryBankLifecyclePhase::ResidencyPending: return "residency_pending";
    case OnMemoryBankLifecyclePhase::Complete: return "complete";
    case OnMemoryBankLifecyclePhase::Failed: return "failed";
    }
    return "failed";
}

inline std::string format_onmemory_bank_lifecycle_log(
    const char* status,
    OnMemoryBankLifecyclePhase phase,
    uint64_t route_generation,
    uint64_t cleanup_generation,
    uint64_t ordinal,
    const char* reason,
    uint32_t canonical_kind = UINT32_MAX,
    uint32_t custom_kind = UINT32_MAX,
    uint64_t canonical_token = 0,
    uint64_t custom_token = 0)
{
    std::string text = "[audio_sead] onmemory_bank_lifecycle status=";
    text += status ? status : "unknown";
    text += " phase=";
    text += onmemory_bank_lifecycle_phase_name(phase);
    text += " route_generation=" + std::to_string(route_generation);
    text += " cleanup_generation=" + std::to_string(cleanup_generation);
    text += " ordinal=" + std::to_string(ordinal);
    text += " reason=";
    text += reason ? reason : "none";
    if (canonical_kind != UINT32_MAX) {
        text += " canonical_kind=" + std::to_string(canonical_kind);
    }
    if (custom_kind != UINT32_MAX) {
        text += " custom_kind=" + std::to_string(custom_kind);
    }
    // Both tokens are always emitted together when either is known: whether
    // they are equal is the single fact that selects the retirement path.
    if (canonical_token != 0 || custom_token != 0) {
        text += " canonical_token=" + std::to_string(canonical_token);
        text += " custom_token=" + std::to_string(custom_token);
        text += " tokens_shared=";
        text += (canonical_token == custom_token) ? "1" : "0";
    }
    return text;
}

struct OnMemoryBankSoundIdentity {
    void* object = nullptr;
    UObjectLiveHandle live{};

    constexpr explicit operator bool() const noexcept
    {
        return object && live.internal_index >= 0 && live.serial_number > 0;
    }
};

constexpr bool operator==(
    const OnMemoryBankSoundIdentity& left,
    const OnMemoryBankSoundIdentity& right) noexcept
{
    return left.object == right.object
        && left.live.internal_index == right.live.internal_index
        && left.live.serial_number == right.live.serial_number;
}

constexpr bool operator!=(
    const OnMemoryBankSoundIdentity& left,
    const OnMemoryBankSoundIdentity& right) noexcept
{
    return !(left == right);
}

constexpr bool exact_type1_onmemory_token(
    const DecodedOnMemoryBankToken& token) noexcept
{
    return token.type == 1 && token.encode() != 0;
}

constexpr bool exact_distinct_type1_onmemory_tokens(
    const DecodedOnMemoryBankToken& canonical,
    const DecodedOnMemoryBankToken& custom) noexcept
{
    return exact_type1_onmemory_token(canonical)
        && exact_type1_onmemory_token(custom)
        && canonical.encode() != custom.encode();
}

struct OnMemoryBankPlaySetupSnapshot {
    OnMemoryBankSoundIdentity sound{};
    uint64_t observed_owner_token = 0;
    uint64_t route_epoch = 0;
    uint64_t state_epoch = 0;
    uint64_t canonical_epoch = 0;
    DecodedOnMemoryBankToken query_token{};
    bool establishing_canonical = false;
    bool deferred_canonical_establishment = false;
    bool rebasing_canonical = false;
    DecodedOnMemoryBankToken stale_canonical{};
    DecodedOnMemoryBankToken prior_custom{};
    uint64_t completed_ordinal = 0;
    bool completed_owner_zero_rearm = false;
    uint64_t rearm_generation = 0;
    uint64_t rearm_source_ordinal = 0;
    uint64_t rearm_aba_ordinal = 0;

    constexpr explicit operator bool() const noexcept
    {
        return sound && route_epoch != 0 && state_epoch != 0
            && exact_type1_onmemory_token(query_token);
    }
};

struct OnMemoryBankCanonicalRebaseFacts {
    bool lifecycle_healthy = false;
    bool no_release_in_flight = false;
    bool no_active_pending = false;
    bool canonical_ready = false;
    bool same_sound_identity = false;
    bool candidate_type1 = false;
    bool candidate_differs_stale_canonical = false;
    bool completed_custom_absence = false;
    bool completed_sound_matches = false;
    bool completed_canonical_matches = false;
    bool prior_custom_type1 = false;
    bool candidate_differs_prior_custom = false;
};

enum class OnMemoryBankCanonicalRebaseFailure : uint8_t {
    None,
    LifecycleUnhealthy,
    ReleaseInFlight,
    ActivePending,
    CanonicalNotReady,
    SoundIdentityMismatch,
    CandidateNotType1,
    CandidateMatchesStaleCanonical,
    CompletedCustomNotAbsent,
    CompletedSoundMismatch,
    CompletedCanonicalMismatch,
    PriorCustomNotType1,
    CandidateMatchesPriorCustom,
};

constexpr OnMemoryBankCanonicalRebaseFailure
first_onmemory_bank_canonical_rebase_failure(
    const OnMemoryBankCanonicalRebaseFacts& facts) noexcept
{
    if (!facts.lifecycle_healthy) return OnMemoryBankCanonicalRebaseFailure::LifecycleUnhealthy;
    if (!facts.no_release_in_flight) return OnMemoryBankCanonicalRebaseFailure::ReleaseInFlight;
    if (!facts.no_active_pending) return OnMemoryBankCanonicalRebaseFailure::ActivePending;
    if (!facts.canonical_ready) return OnMemoryBankCanonicalRebaseFailure::CanonicalNotReady;
    if (!facts.same_sound_identity) return OnMemoryBankCanonicalRebaseFailure::SoundIdentityMismatch;
    if (!facts.candidate_type1) return OnMemoryBankCanonicalRebaseFailure::CandidateNotType1;
    if (!facts.candidate_differs_stale_canonical) return OnMemoryBankCanonicalRebaseFailure::CandidateMatchesStaleCanonical;
    if (!facts.completed_custom_absence) return OnMemoryBankCanonicalRebaseFailure::CompletedCustomNotAbsent;
    if (!facts.completed_sound_matches) return OnMemoryBankCanonicalRebaseFailure::CompletedSoundMismatch;
    if (!facts.completed_canonical_matches) return OnMemoryBankCanonicalRebaseFailure::CompletedCanonicalMismatch;
    if (!facts.prior_custom_type1) return OnMemoryBankCanonicalRebaseFailure::PriorCustomNotType1;
    if (!facts.candidate_differs_prior_custom) return OnMemoryBankCanonicalRebaseFailure::CandidateMatchesPriorCustom;
    return OnMemoryBankCanonicalRebaseFailure::None;
}

constexpr bool onmemory_bank_canonical_rebase_allowed(
    const OnMemoryBankCanonicalRebaseFacts& facts) noexcept
{
    return first_onmemory_bank_canonical_rebase_failure(facts)
        == OnMemoryBankCanonicalRebaseFailure::None;
}

enum class OnMemoryBankPlaySetupPreflightFailure : uint8_t {
    None,
    VanillaRoute,
    UnsupportedBuild,
    LookupSignatureUnavailable,
    ReleaseSignatureUnavailable,
    LifecycleFailed,
    ReleaseInFlight,
    ActivePending,
    SoundIdentityInvalid,
    RouteEpochInvalid,
    OwnerTokenZero,
    OwnerTokenDecodeInvalid,
    OwnerTokenNotType1,
    CanonicalRebaseRejected,
    CompletedRearmConflict,
};

struct OnMemoryBankPlaySetupPreflight {
    OnMemoryBankRouteDecision decision = OnMemoryBankRouteDecision::LifecycleFailed;
    OnMemoryBankPlaySetupSnapshot snapshot{};
    bool canonical_rebase_attempted = false;
    OnMemoryBankCanonicalRebaseFacts canonical_rebase_facts{};
    DecodedOnMemoryBankToken stale_canonical{};
    DecodedOnMemoryBankToken candidate_canonical{};
    DecodedOnMemoryBankToken prior_custom{};
    OnMemoryBankPlaySetupPreflightFailure first_failure =
        OnMemoryBankPlaySetupPreflightFailure::None;
    OnMemoryBankCanonicalRebaseFailure canonical_rebase_first_failure =
        OnMemoryBankCanonicalRebaseFailure::None;
    bool owner_token_nonzero = false;
    bool owner_token_decode_attempted = false;
    bool owner_token_decoded = false;
    bool owner_token_type1 = false;

    constexpr bool allowed() const noexcept
    {
        return decision == OnMemoryBankRouteDecision::Allowed;
    }
};

enum class OnMemoryBankPlaySetupCommitFailure : uint8_t {
    None,
    SnapshotSoundInvalid,
    SnapshotRouteEpochInvalid,
    SnapshotStateEpochInvalid,
    SnapshotQueryTokenInvalid,
    StateEpochDrift,
    SoundIdentityDrift,
    RouteEpochDrift,
    OwnerTokenDrift,
    CanonicalKindInvalid,
    CanonicalRebaseRejected,
    CanonicalMissing,
    CanonicalTokenDrift,
    CanonicalEpochDrift,
    ExistingCanonicalSoundDrift,
    CompletedRearmMissing,
    CompletedRearmDrift,
};

struct OnMemoryBankPlaySetupCommitResult {
    OnMemoryBankRouteDecision decision = OnMemoryBankRouteDecision::SnapshotDrift;
    OnMemoryBankPlaySetupCommitFailure first_failure =
        OnMemoryBankPlaySetupCommitFailure::SnapshotSoundInvalid;
    bool canonical_rebase_checked = false;
    OnMemoryBankCanonicalRebaseFacts canonical_rebase_facts{};
    OnMemoryBankCanonicalRebaseFailure canonical_rebase_first_failure =
        OnMemoryBankCanonicalRebaseFailure::None;

    constexpr operator OnMemoryBankRouteDecision() const noexcept { return decision; }
};

static_assert(std::is_trivially_copyable_v<OnMemoryBankPlaySetupPreflight>);
static_assert(std::is_trivially_copyable_v<OnMemoryBankPlaySetupCommitResult>);

struct OnMemoryBankCanonicalRecord {
    OnMemoryBankSoundIdentity sound{};
    DecodedOnMemoryBankToken token{};
    uint64_t validation_epoch = 0;
    bool kind2_qualified = false;

    constexpr explicit operator bool() const noexcept
    {
        return sound && validation_epoch != 0 && kind2_qualified
            && exact_type1_onmemory_token(token);
    }
};

struct OnMemoryBankRouteQualification {
    OnMemoryBankSoundIdentity sound{};
    DecodedOnMemoryBankToken canonical{};
    uint64_t route_epoch = 0;
    uint64_t canonical_epoch = 0;

    constexpr explicit operator bool() const noexcept
    {
        return sound && route_epoch != 0 && canonical_epoch != 0
            && exact_type1_onmemory_token(canonical);
    }
};

struct OnMemoryBankDetachedRecord {
    OnMemoryBankLifecyclePhase phase = OnMemoryBankLifecyclePhase::Idle;
    OnMemoryBankSoundIdentity sound{};
    DecodedOnMemoryBankToken canonical{};
    DecodedOnMemoryBankToken custom{};
    uint64_t ordinal = 0;
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    uint64_t request_handle = 0;
    void* backing_identity = nullptr;
    bool backing_observed = false;
    uint32_t observation_count = 0;
    bool owner_restore_verified = false;
    bool release_attempted = false;

    constexpr explicit operator bool() const noexcept
    {
        return phase != OnMemoryBankLifecyclePhase::Idle
            && sound && ordinal != 0 && route_generation != 0
            && cleanup_generation != 0 && request_handle != 0
            && backing_observed
            && exact_distinct_type1_onmemory_tokens(canonical, custom);
    }
};

// The game's OnMemory bank allocator mints a fresh token on every allocation
// and has no reuse path, so an unchanged owner token proves the allocator was
// never reached: the canonical bank was already resident and the native
// play-setup handed the *same* bank back for the custom role.  That is a legal
// and common state, but it is not detached ownership -- there is exactly one
// bank and the game owns it.  Releasing it would free audio data the game is
// still using, so a shared bank is recorded here and never in the detached
// record.  Only the detached record feeds claim_release(), which is the sole
// path to sead_onmemory_bank_release; keeping the shared observation in a
// separate member is what makes the release structurally unreachable.
struct OnMemoryBankSharedRecord {
    OnMemoryBankSoundIdentity sound{};
    DecodedOnMemoryBankToken token{};
    uint64_t ordinal = 0;
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    uint64_t request_handle = 0;
    bool owner_restore_verified = false;

    constexpr explicit operator bool() const noexcept
    {
        return sound && ordinal != 0 && route_generation != 0
            && cleanup_generation != 0 && request_handle != 0
            && owner_restore_verified && exact_type1_onmemory_token(token);
    }
};

enum class OnMemoryBankRetainOutcome : uint8_t {
    Rejected,
    Detached,
    Shared,
};

enum class OnMemoryBankRetainFailure : uint8_t {
    None,
    QualificationMissing,
    CanonicalMissing,
    LifecycleFailed,
    ReleaseInFlight,
    ActivePending,
    CanonicalSoundDrift,
    QualificationSoundDrift,
    QualificationRouteEpochDrift,
    CanonicalEpochDrift,
    FreshOwnerProofMissing,
    OwnerRestoreUnverified,
    PublicationFailed,
    CanonicalKindInvalid,
    CustomKindInvalid,
    RouteEpochZero,
    RouteGenerationZero,
    CleanupGenerationZero,
    RequestHandleZero,
    BackingUnobserved,
    CustomTokenInvalid,
};

inline const char* onmemory_bank_retain_outcome_name(
    OnMemoryBankRetainOutcome outcome) noexcept
{
    switch (outcome) {
    case OnMemoryBankRetainOutcome::Rejected: return "rejected";
    case OnMemoryBankRetainOutcome::Detached: return "detached";
    case OnMemoryBankRetainOutcome::Shared: return "shared";
    }
    return "rejected";
}

inline const char* onmemory_bank_retain_failure_name(
    OnMemoryBankRetainFailure failure) noexcept
{
    switch (failure) {
    case OnMemoryBankRetainFailure::None: return "none";
    case OnMemoryBankRetainFailure::QualificationMissing: return "qualification_missing";
    case OnMemoryBankRetainFailure::CanonicalMissing: return "canonical_missing";
    case OnMemoryBankRetainFailure::LifecycleFailed: return "lifecycle_failed";
    case OnMemoryBankRetainFailure::ReleaseInFlight: return "release_in_flight";
    case OnMemoryBankRetainFailure::ActivePending: return "active_pending";
    case OnMemoryBankRetainFailure::CanonicalSoundDrift: return "canonical_sound_drift";
    case OnMemoryBankRetainFailure::QualificationSoundDrift: return "qualification_sound_drift";
    case OnMemoryBankRetainFailure::QualificationRouteEpochDrift: return "qualification_route_epoch_drift";
    case OnMemoryBankRetainFailure::CanonicalEpochDrift: return "canonical_epoch_drift";
    case OnMemoryBankRetainFailure::FreshOwnerProofMissing: return "fresh_owner_proof_missing";
    case OnMemoryBankRetainFailure::OwnerRestoreUnverified: return "owner_restore_unverified";
    case OnMemoryBankRetainFailure::PublicationFailed: return "publication_failed";
    case OnMemoryBankRetainFailure::CanonicalKindInvalid: return "canonical_kind_invalid";
    case OnMemoryBankRetainFailure::CustomKindInvalid: return "custom_kind_invalid";
    case OnMemoryBankRetainFailure::RouteEpochZero: return "route_epoch_zero";
    case OnMemoryBankRetainFailure::RouteGenerationZero: return "route_generation_zero";
    case OnMemoryBankRetainFailure::CleanupGenerationZero: return "cleanup_generation_zero";
    case OnMemoryBankRetainFailure::RequestHandleZero: return "request_handle_zero";
    case OnMemoryBankRetainFailure::BackingUnobserved: return "backing_unobserved";
    case OnMemoryBankRetainFailure::CustomTokenInvalid: return "custom_token_invalid";
    }
    return "none";
}

// Play-setup admission split.  Both retirement outcomes need exactly the same
// post-query evidence; they differ only in whether the custom role observed a
// separate bank.  Folding the distinctness test into the shared admission is
// what previously made the shared outcome unreachable in production: the
// retirement classifier was never entered at all, so the tri-state could only
// ever be exercised from fixtures.
constexpr bool onmemory_bank_detached_retirement_admitted(
    const bool post_query_exact, const bool tokens_distinct) noexcept
{
    return post_query_exact && tokens_distinct;
}

constexpr bool onmemory_bank_shared_retirement_admitted(
    const bool post_query_exact, const bool tokens_distinct) noexcept
{
    return post_query_exact && !tokens_distinct;
}

// The two retirement outcomes prove the owner field is correct by different
// means, because the mod's write to sound+0x548 differs in kind between them.
//
// Detached: the mod wrote the custom token over the canonical one, so the
// restore has real work to do and `applied` records that it was carried out.
//
// Shared: the field already held the canonical token, because the game's own
// bank is the one both roles observe, so there is nothing to write back.  The
// restore machinery rejects that case rather than reporting success -- an
// exact override requires `restore_value != current` (exact_override_matches)
// and a field whose current value already equals its original short-circuits
// to AlreadyRestored before the override branch is consulted at all.  So
// `applied` is structurally false for a shared bank, and treating its absence
// as missing proof would make the shared outcome unreachable.  The proof that
// actually holds is that the field was *observed* to carry the canonical
// token, which the post-query read has already established.
constexpr bool onmemory_bank_owner_restore_proven(
    const bool owner_restore_applied, const bool shared_owner_field_exact) noexcept
{
    return owner_restore_applied || shared_owner_field_exact;
}

// The production guard that decides whether a play setup reaches the
// retirement classifier at all.  It is named here, rather than left inline at
// the call site, so a test can assert that a shared session is admitted --
// the admission predicates above are necessary but not sufficient, and a
// conjunct that is structurally false for a shared bank silently strands the
// whole shared path while every direct-call fixture still passes.
struct OnMemoryBankRetirementGuardFacts final {
    bool detached_retirement_admitted = false;
    bool shared_retirement_admitted = false;
    bool pending_patch_restored = false;
    bool owner_restore_applied = false;
    bool shared_owner_field_exact = false;
};

constexpr bool onmemory_bank_retirement_guard_admits(
    const OnMemoryBankRetirementGuardFacts& facts) noexcept
{
    return (facts.detached_retirement_admitted || facts.shared_retirement_admitted)
        && facts.pending_patch_restored
        && onmemory_bank_owner_restore_proven(
            facts.owner_restore_applied, facts.shared_owner_field_exact);
}

struct OnMemoryBankRetainResult {
    OnMemoryBankRetainOutcome outcome = OnMemoryBankRetainOutcome::Rejected;
    OnMemoryBankRetainFailure first_failure = OnMemoryBankRetainFailure::None;
    uint64_t canonical_token = 0;
    uint64_t custom_token = 0;
    uint64_t ordinal = 0;
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;

    constexpr bool detached() const noexcept
    {
        return outcome == OnMemoryBankRetainOutcome::Detached;
    }

    constexpr bool shared() const noexcept
    {
        return outcome == OnMemoryBankRetainOutcome::Shared;
    }
};

struct OnMemoryBankCompletedOwnerZeroRearmAuthority {
    uint64_t generation = 0;
    uint64_t state_epoch = 0;
    uint64_t source_ordinal = 0;
    uint64_t aba_ordinal = 0;
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    OnMemoryBankSoundIdentity sound{};
    DecodedOnMemoryBankToken canonical{};
    DecodedOnMemoryBankToken released_custom{};
    uint32_t canonical_kind = 0;
    uint32_t released_custom_kind = UINT32_MAX;
    bool owner_restore_verified = false;
    bool release_attempted = false;

    constexpr bool present() const noexcept
    {
        return generation != 0 || state_epoch != 0 || source_ordinal != 0
            || aba_ordinal != 0 || route_generation != 0
            || cleanup_generation != 0 || sound
            || canonical.encode() != 0 || released_custom.encode() != 0
            || canonical_kind != 0 || released_custom_kind != UINT32_MAX
            || owner_restore_verified || release_attempted;
    }

    constexpr explicit operator bool() const noexcept
    {
        return generation != 0 && state_epoch != 0 && source_ordinal != 0
            && aba_ordinal != 0 && route_generation != 0
            && cleanup_generation != 0 && sound
            && exact_distinct_type1_onmemory_tokens(canonical, released_custom)
            && canonical_kind == 2 && released_custom_kind == 0
            && owner_restore_verified && release_attempted;
    }
};

constexpr bool same_onmemory_bank_completed_owner_zero_rearm_authority(
    const OnMemoryBankCompletedOwnerZeroRearmAuthority& left,
    const OnMemoryBankCompletedOwnerZeroRearmAuthority& right) noexcept
{
    return left.generation == right.generation
        && left.state_epoch == right.state_epoch
        && left.source_ordinal == right.source_ordinal
        && left.aba_ordinal == right.aba_ordinal
        && left.route_generation == right.route_generation
        && left.cleanup_generation == right.cleanup_generation
        && left.sound == right.sound
        && left.canonical.encode() == right.canonical.encode()
        && left.released_custom.encode() == right.released_custom.encode()
        && left.canonical_kind == right.canonical_kind
        && left.released_custom_kind == right.released_custom_kind
        && left.owner_restore_verified == right.owner_restore_verified
        && left.release_attempted == right.release_attempted;
}

static_assert(std::is_trivially_copyable_v<
    OnMemoryBankCompletedOwnerZeroRearmAuthority>);

enum class OnMemoryBankRetirementOwnerCategory : uint8_t {
    Unreadable,
    IdentityMismatch,
    Zero,
    Canonical,
    DetachedCustom,
    ForeignNonzero,
};

struct OnMemoryBankRetirementOwnerFact {
    OnMemoryBankSoundIdentity sound{};
    OnMemoryBankRetirementOwnerCategory category =
        OnMemoryBankRetirementOwnerCategory::Unreadable;
    bool structural_identity_matches = false;
};

inline OnMemoryBankRetirementOwnerFact classify_onmemory_bank_retirement_owner(
    const OnMemoryBankDetachedRecord& detached,
    const OnMemoryBankSoundIdentity& observed_sound,
    bool structural_identity_matches,
    bool owner_readable,
    uint64_t owner_token) noexcept
{
    OnMemoryBankRetirementOwnerFact result;
    result.sound = observed_sound;
    result.structural_identity_matches = structural_identity_matches;
    if (!detached || !owner_readable) {
        result.category = OnMemoryBankRetirementOwnerCategory::Unreadable;
    } else if (!observed_sound || observed_sound != detached.sound
        || !structural_identity_matches) {
        result.category = OnMemoryBankRetirementOwnerCategory::IdentityMismatch;
    } else if (owner_token == 0) {
        result.category = OnMemoryBankRetirementOwnerCategory::Zero;
    } else if (owner_token == detached.canonical.encode()) {
        result.category = OnMemoryBankRetirementOwnerCategory::Canonical;
    } else if (owner_token == detached.custom.encode()) {
        result.category = OnMemoryBankRetirementOwnerCategory::DetachedCustom;
    } else {
        result.category = OnMemoryBankRetirementOwnerCategory::ForeignNonzero;
    }
    return result;
}

struct OnMemoryBankRetirementFacts {
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    uint64_t retired_request_handle = 0;
    uint64_t current_request_handle = 0;
    void* current_backing = nullptr;
    void* retired_backing = nullptr;
    bool current_backing_observed = false;
    bool retired_backing_observed = false;
    OnMemoryBankRetirementOwnerFact current_owner{};
    bool exact_request_retired = false;
    bool route_released = false;
    bool playback_released = false;
    bool cleanup_released = false;
    bool owner_restore_verified = false;
    bool runtime_installed = false;
    bool lookup_signature_valid = false;
    bool release_signature_valid = false;
    bool shutdown_or_disabled = false;
};

constexpr bool onmemory_bank_current_backing_detached(
    void* retained_backing,
    bool retained_backing_observed,
    void* current_backing,
    bool current_backing_observed) noexcept
{
    return retained_backing_observed && current_backing_observed
        && (!retained_backing || current_backing != retained_backing);
}

inline OnMemoryBankRetirementFacts bind_onmemory_bank_retirement_facts(
    const OnMemoryBankRetirementFacts& validated_baseline,
    const OnMemoryBankDetachedRecord& detached,
    uint64_t current_request_handle,
    void* current_backing,
    bool current_backing_observed,
    void* retired_backing,
    bool retired_backing_observed) noexcept
{
    OnMemoryBankRetirementFacts facts = validated_baseline;
    facts.route_generation = detached.route_generation + 1;
    facts.cleanup_generation = detached.cleanup_generation;
    facts.retired_request_handle = detached.request_handle;
    facts.current_request_handle = current_request_handle;
    facts.current_backing = current_backing;
    facts.current_backing_observed = current_backing_observed;
    facts.retired_backing = retired_backing;
    facts.retired_backing_observed = retired_backing_observed;
    return facts;
}

// Evidence from the observation where the exact request still exists in the
// native retired-request array. The native BGM object is deliberately not
// retained; the value is bound to immutable route/request/sound identities.
enum class OnMemoryBankRetiredBackingProvenance : uint8_t {
    None,
    RetiredVectorPresence,
    ExactNaturalCompletionDetached,
    DetachedAndRetiredVector,
};

enum class OnMemoryBankRetiredBackingRequirement : uint8_t {
    RetiredVectorPresence,
    ExactNaturalCompletion,
};

struct OnMemoryBankRetiredBackingEvidence {
    bool observed = false;
    bool failed = false;
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    uint64_t monitor_epoch = 0;
    uint64_t request_handle = 0;
    UObjectLiveHandle sound_identity{};
    void* value = nullptr;
    OnMemoryBankRetiredBackingProvenance provenance =
        OnMemoryBankRetiredBackingProvenance::None;
    uint64_t lifecycle_epoch = 0;
    uint64_t detached_ordinal = 0;
    DecodedOnMemoryBankToken canonical{};
    DecodedOnMemoryBankToken custom{};
    bool backing_observed = false;
    bool owner_restore_verified = false;
    bool release_attempted = false;

    explicit operator bool() const noexcept
    {
        if (!observed || failed
            || provenance == OnMemoryBankRetiredBackingProvenance::None
            || route_generation == 0 || cleanup_generation == 0
            || monitor_epoch == 0 || request_handle == 0
            || sound_identity.internal_index < 0
            || sound_identity.serial_number <= 0) {
            return false;
        }
        if (provenance == OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence) {
            return true;
        }
        return lifecycle_epoch != 0 && detached_ordinal != 0
            && backing_observed && owner_restore_verified && !release_attempted
            && exact_distinct_type1_onmemory_tokens(canonical, custom);
    }
};

struct OnMemoryBankDetachedBackingSeedFacts {
    uint64_t lifecycle_epoch = 0;
    uint64_t ordinal = 0;
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    uint64_t monitor_epoch = 0;
    uint64_t request_handle = 0;
    UObjectLiveHandle sound_identity{};
    DecodedOnMemoryBankToken canonical{};
    DecodedOnMemoryBankToken custom{};
    bool backing_observed = false;
    void* backing = nullptr;
};

struct OnMemoryBankRetiredBackingObservation {
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    uint64_t monitor_epoch = 0;
    uint64_t request_handle = 0;
    UObjectLiveHandle sound_identity{};
    bool exact_request_present = false;
    bool backing_observed = false;
    void* backing = nullptr;
};

// One native retired-array scan must produce exactly one fully readable entry
// for the expected request. Duplicate exact handles are rejected even when the
// readable fields happen to compare equal; array uniqueness is authority.
struct OnMemoryBankRetiredEntryEvidence {
    bool observed = false;
    bool failed = false;
    uint64_t request_handle = 0;
    void* sound = nullptr;
    void* backing = nullptr;
};

inline bool record_onmemory_bank_retired_entry(
    OnMemoryBankRetiredEntryEvidence& evidence,
    uint64_t observed_request_handle,
    uint64_t expected_request_handle,
    bool sound_observed,
    void* observed_sound,
    void* expected_sound,
    bool backing_observed,
    void* observed_backing) noexcept
{
    if (evidence.observed || evidence.failed
        || observed_request_handle == 0
        || observed_request_handle != expected_request_handle
        || !sound_observed || observed_sound != expected_sound
        || !backing_observed) {
        evidence.failed = true;
        return false;
    }
    evidence.observed = true;
    evidence.request_handle = observed_request_handle;
    evidence.sound = observed_sound;
    evidence.backing = observed_backing;
    return true;
}

inline bool onmemory_bank_retired_backing_satisfies(
    const OnMemoryBankRetiredBackingEvidence& evidence,
    OnMemoryBankRetiredBackingRequirement requirement =
        OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence) noexcept
{
    return evidence && (
        requirement == OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence
            ? evidence.provenance == OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence
                || evidence.provenance
                    == OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector
            : evidence.provenance
                    == OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached
                || evidence.provenance
                    == OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector);
}

inline bool onmemory_bank_retirement_presence_pending(
    const OnMemoryBankRetiredBackingEvidence& evidence,
    bool observation_valid,
    bool exact_request_present,
    OnMemoryBankRetiredBackingRequirement requirement =
        OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence) noexcept
{
    return observation_valid && !exact_request_present
        && !onmemory_bank_retired_backing_satisfies(evidence, requirement)
        && !evidence.failed;
}

inline bool same_onmemory_bank_retired_backing_evidence(
    const OnMemoryBankRetiredBackingEvidence& left,
    const OnMemoryBankRetiredBackingEvidence& right) noexcept
{
    return left.observed == right.observed && left.failed == right.failed
        && left.route_generation == right.route_generation
        && left.cleanup_generation == right.cleanup_generation
        && left.monitor_epoch == right.monitor_epoch
        && left.request_handle == right.request_handle
        && left.sound_identity.internal_index == right.sound_identity.internal_index
        && left.sound_identity.serial_number == right.sound_identity.serial_number
        && left.value == right.value
        && left.provenance == right.provenance
        && left.lifecycle_epoch == right.lifecycle_epoch
        && left.detached_ordinal == right.detached_ordinal
        && left.canonical.encode() == right.canonical.encode()
        && left.custom.encode() == right.custom.encode()
        && left.backing_observed == right.backing_observed
        && left.owner_restore_verified == right.owner_restore_verified
        && left.release_attempted == right.release_attempted;
}

inline bool seed_onmemory_bank_retired_backing_from_detached(
    OnMemoryBankRetiredBackingEvidence& retained,
    const OnMemoryBankDetachedRecord& detached,
    const OnMemoryBankDetachedBackingSeedFacts& expected) noexcept
{
    const OnMemoryBankRetiredBackingEvidence empty{};
    if (!same_onmemory_bank_retired_backing_evidence(retained, empty)
        || !detached
        || detached.phase != OnMemoryBankLifecyclePhase::RestoreApplied
        || expected.lifecycle_epoch == 0
        || detached.ordinal != expected.ordinal
        || detached.route_generation == UINT64_MAX
        || expected.route_generation != detached.route_generation + 1
        || detached.cleanup_generation != expected.cleanup_generation
        || expected.monitor_epoch == 0
        || detached.request_handle != expected.request_handle
        || detached.sound.live.internal_index != expected.sound_identity.internal_index
        || detached.sound.live.serial_number != expected.sound_identity.serial_number
        || detached.canonical.encode() != expected.canonical.encode()
        || detached.custom.encode() != expected.custom.encode()
        || detached.backing_observed != expected.backing_observed
        || !expected.backing_observed
        || detached.backing_identity != expected.backing
        || !detached.owner_restore_verified
        || detached.release_attempted) {
        return false;
    }
    retained.observed = true;
    retained.route_generation = expected.route_generation;
    retained.cleanup_generation = detached.cleanup_generation;
    retained.monitor_epoch = expected.monitor_epoch;
    retained.request_handle = detached.request_handle;
    retained.sound_identity = detached.sound.live;
    retained.value = detached.backing_identity;
    retained.provenance =
        OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached;
    retained.lifecycle_epoch = expected.lifecycle_epoch;
    retained.detached_ordinal = detached.ordinal;
    retained.canonical = detached.canonical;
    retained.custom = detached.custom;
    retained.backing_observed = detached.backing_observed;
    retained.owner_restore_verified = detached.owner_restore_verified;
    retained.release_attempted = detached.release_attempted;
    return true;
}

inline bool onmemory_bank_exact_natural_detached_backing_current(
    const OnMemoryBankRetiredBackingEvidence& evidence,
    uint64_t lifecycle_epoch,
    const OnMemoryBankDetachedRecord& detached) noexcept
{
    return onmemory_bank_retired_backing_satisfies(
               evidence,
               OnMemoryBankRetiredBackingRequirement::ExactNaturalCompletion)
        && lifecycle_epoch == evidence.lifecycle_epoch
        && detached
        && detached.phase == OnMemoryBankLifecyclePhase::RestoreApplied
        && detached.ordinal == evidence.detached_ordinal
        && detached.route_generation != UINT64_MAX
        && detached.route_generation + 1 == evidence.route_generation
        && detached.cleanup_generation == evidence.cleanup_generation
        && detached.request_handle == evidence.request_handle
        && detached.sound.live.internal_index == evidence.sound_identity.internal_index
        && detached.sound.live.serial_number == evidence.sound_identity.serial_number
        && detached.canonical.encode() == evidence.canonical.encode()
        && detached.custom.encode() == evidence.custom.encode()
        && detached.backing_observed == evidence.backing_observed
        && detached.backing_identity == evidence.value
        && detached.owner_restore_verified == evidence.owner_restore_verified
        && detached.release_attempted == evidence.release_attempted
        && detached.owner_restore_verified && !detached.release_attempted;
}

inline bool record_onmemory_bank_retired_backing(
    OnMemoryBankRetiredBackingEvidence& retained,
    const OnMemoryBankRetiredBackingObservation& observed) noexcept
{
    // Absence is not evidence of malformed retirement: native Stop may publish
    // the retired request asynchronously. Only a present request can add or
    // poison retained backing evidence.
    if (!observed.exact_request_present) {
        return false;
    }
    const bool identity_valid = observed.route_generation != 0
        && observed.cleanup_generation != 0 && observed.monitor_epoch != 0
        && observed.request_handle != 0
        && observed.sound_identity.internal_index >= 0
        && observed.sound_identity.serial_number > 0;
    if (!identity_valid || !observed.backing_observed) {
        retained.failed = true;
        return false;
    }
    if (!retained.observed) {
        if (retained.failed) return false;
        retained.observed = true;
        retained.route_generation = observed.route_generation;
        retained.cleanup_generation = observed.cleanup_generation;
        retained.monitor_epoch = observed.monitor_epoch;
        retained.request_handle = observed.request_handle;
        retained.sound_identity = observed.sound_identity;
        retained.value = observed.backing;
        retained.provenance =
            OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence;
        return true;
    }
    if (retained.route_generation != observed.route_generation
        || retained.cleanup_generation != observed.cleanup_generation
        || retained.monitor_epoch != observed.monitor_epoch
        || retained.request_handle != observed.request_handle
        || retained.sound_identity.internal_index
            != observed.sound_identity.internal_index
        || retained.sound_identity.serial_number
            != observed.sound_identity.serial_number
        || (retained.value != observed.backing
            && !(retained.value == nullptr && observed.backing != nullptr
                && retained.provenance
                    == OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached))) {
        retained.failed = true;
        return false;
    }
    if (retained.value == nullptr && observed.backing != nullptr
        && retained.provenance
            == OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached) {
        retained.value = observed.backing;
    }
    if (retained.provenance
        == OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached) {
        retained.provenance =
            OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector;
    } else if (retained.provenance
        != OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence
        && retained.provenance
            != OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector) {
        retained.failed = true;
        return false;
    }
    return !retained.failed;
}

inline bool onmemory_bank_exact_request_retired(
    const OnMemoryBankRetiredBackingEvidence& retained,
    uint64_t route_generation,
    uint64_t cleanup_generation,
    uint64_t monitor_epoch,
    uint64_t request_handle,
    const UObjectLiveHandle& sound_identity,
    bool exact_request_absent,
    void*& retired_backing,
    OnMemoryBankRetiredBackingRequirement requirement =
        OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence) noexcept
{
    retired_backing = nullptr;
    const bool provenance_exact = requirement
            == OnMemoryBankRetiredBackingRequirement::RetiredVectorPresence
        ? retained.provenance == OnMemoryBankRetiredBackingProvenance::RetiredVectorPresence
            || retained.provenance
                == OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector
        : retained.provenance
                == OnMemoryBankRetiredBackingProvenance::ExactNaturalCompletionDetached
            || retained.provenance
                == OnMemoryBankRetiredBackingProvenance::DetachedAndRetiredVector;
    if (!retained || !provenance_exact || !exact_request_absent
        || retained.route_generation != route_generation
        || retained.cleanup_generation != cleanup_generation
        || retained.monitor_epoch != monitor_epoch
        || retained.request_handle != request_handle
        || retained.sound_identity.internal_index != sound_identity.internal_index
        || retained.sound_identity.serial_number != sound_identity.serial_number) {
        return false;
    }
    retired_backing = retained.value;
    return true;
}

struct OnMemoryBankReleaseAction {
    uint64_t ordinal = 0;
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    DecodedOnMemoryBankToken canonical{};
    DecodedOnMemoryBankToken custom{};

    constexpr explicit operator bool() const noexcept
    {
        return ordinal != 0 && route_generation != 0 && cleanup_generation != 0
            && exact_distinct_type1_onmemory_tokens(canonical, custom);
    }
};

static_assert(std::is_trivially_copyable_v<OnMemoryBankReleaseAction>);

enum class OnMemoryBankReleaseOutcome : uint8_t {
    Failed,
    AlreadyAbsent,
    AsyncReleaseRequested,
};

struct OnMemoryBankNativeReleaseResult {
    bool completed = false;
    uint64_t ignored_native_return = 0;
};

struct OnMemoryBankReleaseExecution {
    OnMemoryBankReleaseOutcome outcome = OnMemoryBankReleaseOutcome::Failed;
    uint32_t canonical_kind = 0;
    uint32_t custom_kind = 0;
};

template <typename Lookup, typename Release>
OnMemoryBankReleaseExecution execute_onmemory_bank_release(
    const bool lookup_signature_valid,
    const bool release_signature_valid,
    const bool shutdown_or_disabled,
    const OnMemoryBankReleaseAction& action,
    Lookup&& lookup,
    Release&& release)
{
    OnMemoryBankReleaseExecution result;
    if (!lookup_signature_valid || !release_signature_valid
        || shutdown_or_disabled || !action) {
        return result;
    }
    const uint64_t canonical_token = action.canonical.encode();
    const uint64_t custom_token = action.custom.encode();
    result.canonical_kind = lookup(canonical_token);
    if (result.canonical_kind != 2) return result;
    result.custom_kind = lookup(custom_token);
    if (result.custom_kind == 0) {
        result.outcome = OnMemoryBankReleaseOutcome::AlreadyAbsent;
        return result;
    }
    if (result.custom_kind != 2) return result;
    const uint64_t copied_custom_token = custom_token;
    const OnMemoryBankNativeReleaseResult native_result = release(
        &copied_custom_token, static_cast<uint8_t>(1));
    if (native_result.completed) {
        // The native uint64 return is not a success predicate for this ABI.
        result.outcome = OnMemoryBankReleaseOutcome::AsyncReleaseRequested;
    }
    return result;
}

struct OnMemoryBankPendingObservation {
    OnMemoryBankReleaseAction action{};
    uint32_t observation_count = 0;

    constexpr explicit operator bool() const noexcept { return static_cast<bool>(action); }
};

enum class OnMemoryBankCleanupOnlyPhase : uint8_t {
    None,
    CleanupOnlyRestoreApplied,
    ReleaseInFlight,
    Complete,
    FailedRetained,
};

enum class OnMemoryBankCleanupOnlyFailure : uint8_t {
    None,
    SalvageAuthorityInvalid,
    SoundIdentityInvalid,
    CanonicalTokenInvalid,
    CustomTokenInvalid,
    TokenAlias,
    CanonicalKindInvalid,
    CustomKindInvalid,
    OwnerRestoreFailed,
    RetainConflict,
    ProvenanceDrift,
    ControllerChainDrift,
    OwnerDrift,
    RequestActive,
    BackingActive,
    SoundActive,
    RouteOrSetupActive,
    ReleaseSignaturesUnavailable,
    ReleaseExecutionFailed,
    CompletionKindInvalid,
};

struct OnMemoryBankCleanupOnlyRecord {
    OnMemoryBankCleanupOnlyPhase phase = OnMemoryBankCleanupOnlyPhase::None;
    OnMemoryBankCleanupOnlyFailure first_failure =
        OnMemoryBankCleanupOnlyFailure::None;
    uint64_t generation = 0;
    uint64_t bridge_generation = 0;
    OnMemoryBankSoundIdentity sound{};
    DecodedOnMemoryBankToken canonical{};
    DecodedOnMemoryBankToken custom{};
    uint64_t raw_custom_token = 0;
    uint64_t selection_generation = 0;
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    uint64_t song_key = 0;
    uint64_t list_exit_epoch = 0;
    uint64_t release_completion_epoch = 0;
    uint64_t revocation_epoch = 0;
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    bool owner_restore_verified = false;
    uint32_t release_call_count = 0;

    constexpr explicit operator bool() const noexcept
    {
        const bool identity = phase != OnMemoryBankCleanupOnlyPhase::None
            && generation != 0 && bridge_generation != 0 && sound
            && route_generation != 0 && cleanup_generation != 0;
        return identity && (phase == OnMemoryBankCleanupOnlyPhase::FailedRetained
            || (raw_custom_token == custom.encode()
                && exact_distinct_type1_onmemory_tokens(canonical, custom)));
    }
};

struct OnMemoryBankCleanupOnlyReleaseFacts {
    uint64_t generation = 0;
    bool provenance_exact = false;
    bool identity_exact = false;
    bool owner_canonical = false;
    bool callback_quiescence_exact = false;
    bool route_and_setup_absent = false;
    bool route_quiescence_exact = false;
    bool aggregate_and_pause_absent = false;
    bool request_zero = false;
    bool backing_read_succeeded = false;
    bool backing_null = false;
    bool backing_detached = false;
    bool sound_inactive = false;
    bool lookup_signature_valid = false;
    bool release_signature_valid = false;
    bool runtime_enabled = false;
    uint32_t canonical_kind = 0;
    uint32_t custom_kind = 0;
};

class OnMemoryBankCleanupOnlyState {
public:
    bool retain(const OnMemoryBankCleanupOnlyRecord& candidate) noexcept
    {
        if (blocks_custom_routes() || !candidate
            || candidate.phase
                != OnMemoryBankCleanupOnlyPhase::CleanupOnlyRestoreApplied
            || !candidate.owner_restore_verified
            || candidate.release_call_count != 0) {
            return false;
        }
        record_ = candidate;
        return true;
    }

    bool retain_failed(const OnMemoryBankCleanupOnlyRecord& candidate,
        OnMemoryBankCleanupOnlyFailure failure) noexcept
    {
        if (blocks_custom_routes() || !candidate || failure == OnMemoryBankCleanupOnlyFailure::None) {
            return false;
        }
        record_ = candidate;
        record_.phase = OnMemoryBankCleanupOnlyPhase::FailedRetained;
        record_.first_failure = failure;
        return true;
    }

    bool claim_release(const OnMemoryBankCleanupOnlyReleaseFacts& facts,
        OnMemoryBankReleaseAction& action) noexcept
    {
        action = {};
        if (record_.phase
                != OnMemoryBankCleanupOnlyPhase::CleanupOnlyRestoreApplied
            || facts.generation != record_.generation
            || !facts.provenance_exact || !facts.identity_exact
            || !facts.owner_canonical || !facts.callback_quiescence_exact
            || !facts.route_and_setup_absent
            || !facts.route_quiescence_exact
            || !facts.aggregate_and_pause_absent || !facts.request_zero
            || !facts.backing_read_succeeded || !facts.backing_null
            || !facts.backing_detached || !facts.sound_inactive
            || !facts.lookup_signature_valid || !facts.release_signature_valid
            || !facts.runtime_enabled || facts.canonical_kind != 2) {
            return false;
        }
        if (facts.custom_kind == 0) {
            record_.phase = OnMemoryBankCleanupOnlyPhase::Complete;
            return true;
        }
        if (facts.custom_kind != 2 || record_.release_call_count != 0) return false;
        action = {record_.generation, record_.route_generation,
            record_.cleanup_generation, record_.canonical, record_.custom};
        record_.phase = OnMemoryBankCleanupOnlyPhase::ReleaseInFlight;
        record_.release_call_count = 1;
        return true;
    }

    bool finish_release(const OnMemoryBankReleaseAction& action,
        OnMemoryBankReleaseOutcome outcome) noexcept
    {
        if (record_.phase != OnMemoryBankCleanupOnlyPhase::ReleaseInFlight
            || !action || action.ordinal != record_.generation
            || action.route_generation != record_.route_generation
            || action.cleanup_generation != record_.cleanup_generation
            || action.canonical.encode() != record_.canonical.encode()
            || action.custom.encode() != record_.custom.encode()) return false;
        if (outcome == OnMemoryBankReleaseOutcome::AlreadyAbsent) {
            record_.phase = OnMemoryBankCleanupOnlyPhase::Complete;
        } else if (outcome != OnMemoryBankReleaseOutcome::AsyncReleaseRequested) {
            record_.phase = OnMemoryBankCleanupOnlyPhase::FailedRetained;
            record_.first_failure =
                OnMemoryBankCleanupOnlyFailure::ReleaseExecutionFailed;
        }
        return true;
    }

    bool observe(uint64_t generation, uint32_t canonical_kind,
        uint32_t custom_kind) noexcept
    {
        if (record_.phase != OnMemoryBankCleanupOnlyPhase::ReleaseInFlight
            || generation != record_.generation) return false;
        if (canonical_kind == 2 && custom_kind == 0) {
            record_.phase = OnMemoryBankCleanupOnlyPhase::Complete;
        } else if (canonical_kind != 2 || custom_kind != 2) {
            record_.phase = OnMemoryBankCleanupOnlyPhase::FailedRetained;
            record_.first_failure =
                OnMemoryBankCleanupOnlyFailure::CompletionKindInvalid;
        }
        return true;
    }

    void fail(uint64_t generation, OnMemoryBankCleanupOnlyFailure failure) noexcept
    {
        if (generation == record_.generation
            && record_.phase != OnMemoryBankCleanupOnlyPhase::None
            && record_.phase != OnMemoryBankCleanupOnlyPhase::Complete) {
            record_.phase = OnMemoryBankCleanupOnlyPhase::FailedRetained;
            if (record_.first_failure == OnMemoryBankCleanupOnlyFailure::None) {
                record_.first_failure = failure;
            }
        }
    }

    bool blocks_custom_routes() const noexcept
    {
        return record_.phase
                == OnMemoryBankCleanupOnlyPhase::CleanupOnlyRestoreApplied
            || record_.phase == OnMemoryBankCleanupOnlyPhase::ReleaseInFlight
            || record_.phase == OnMemoryBankCleanupOnlyPhase::FailedRetained;
    }

    const OnMemoryBankCleanupOnlyRecord& record() const noexcept { return record_; }
    void reset_complete() noexcept
    {
        if (record_.phase == OnMemoryBankCleanupOnlyPhase::Complete) record_ = {};
    }

private:
    OnMemoryBankCleanupOnlyRecord record_{};
};

static_assert(std::is_trivially_copyable_v<OnMemoryBankCleanupOnlyRecord>);
static_assert(std::is_trivially_copyable_v<OnMemoryBankCleanupOnlyReleaseFacts>);

class OnMemoryBankLifecycleState {
public:
    OnMemoryBankRouteDecision preflight_arm(
        bool custom_route,
        bool supported_build,
        bool lookup_signature_valid,
        bool release_signature_valid) const noexcept
    {
        if (!custom_route) return OnMemoryBankRouteDecision::VanillaUnaffected;
        if (!supported_build) return OnMemoryBankRouteDecision::UnsupportedBuild;
        if (!lookup_signature_valid) {
            return OnMemoryBankRouteDecision::LookupSignatureUnavailable;
        }
        if (!release_signature_valid) {
            return OnMemoryBankRouteDecision::ReleaseSignatureUnavailable;
        }
        if (failed_) return OnMemoryBankRouteDecision::LifecycleFailed;
        if (release_in_flight_ || active_pending()) {
            return OnMemoryBankRouteDecision::ReleasePending;
        }
        return OnMemoryBankRouteDecision::Allowed;
    }

    OnMemoryBankPlaySetupPreflight snapshot_play_setup(
        bool custom_route,
        bool supported_build,
        bool lookup_signature_valid,
        bool release_signature_valid,
        const OnMemoryBankSoundIdentity& sound,
        uint64_t observed_owner_token,
        uint64_t route_epoch,
        uint64_t deferred_canonical_token = 0) const noexcept
    {
        OnMemoryBankPlaySetupPreflight result;
        if (!custom_route) {
            result.decision = OnMemoryBankRouteDecision::VanillaUnaffected;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::VanillaRoute;
            return result;
        }
        if (!supported_build) {
            result.decision = OnMemoryBankRouteDecision::UnsupportedBuild;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::UnsupportedBuild;
            return result;
        }
        if (!lookup_signature_valid) {
            result.decision = OnMemoryBankRouteDecision::LookupSignatureUnavailable;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::LookupSignatureUnavailable;
            return result;
        }
        if (!release_signature_valid) {
            result.decision = OnMemoryBankRouteDecision::ReleaseSignatureUnavailable;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::ReleaseSignatureUnavailable;
            return result;
        }
        if (failed_) {
            result.decision = OnMemoryBankRouteDecision::LifecycleFailed;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::LifecycleFailed;
            return result;
        }
        if (release_in_flight_) {
            result.decision = OnMemoryBankRouteDecision::ReleasePending;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::ReleaseInFlight;
            return result;
        }
        if (active_pending()) {
            result.decision = OnMemoryBankRouteDecision::ReleasePending;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::ActivePending;
            return result;
        }
        if (!sound) {
            result.decision = OnMemoryBankRouteDecision::SoundIdentityInvalid;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::SoundIdentityInvalid;
            return result;
        }
        if (route_epoch == 0) {
            result.decision = OnMemoryBankRouteDecision::SoundIdentityInvalid;
            result.first_failure = OnMemoryBankPlaySetupPreflightFailure::RouteEpochInvalid;
            return result;
        }

        DecodedOnMemoryBankToken observed;
        result.owner_token_nonzero = observed_owner_token != 0;
        result.owner_token_decode_attempted = result.owner_token_nonzero;
        result.owner_token_decoded = result.owner_token_decode_attempted
            && decode_onmemory_bank_token(observed_owner_token, observed);
        result.owner_token_type1 = result.owner_token_decoded
            && exact_type1_onmemory_token(observed);
        const bool owner_decoded = result.owner_token_type1;
        OnMemoryBankPlaySetupSnapshot snapshot;
        snapshot.sound = sound;
        snapshot.observed_owner_token = observed_owner_token;
        snapshot.route_epoch = route_epoch;
        snapshot.state_epoch = state_epoch_;
        snapshot.canonical_epoch = canonical_.validation_epoch;
        if (canonical_ && canonical_.sound == sound) {
            if (observed_owner_token != 0
                && observed_owner_token != canonical_.token.encode()) {
                const OnMemoryBankCanonicalRebaseFacts rebase_facts{
                    !failed_,
                    !release_in_flight_,
                    !active_pending(),
                    static_cast<bool>(canonical_),
                    canonical_.sound == sound,
                    owner_decoded,
                    owner_decoded
                        && observed.encode() != canonical_.token.encode(),
                    active_
                        && active_.phase == OnMemoryBankLifecyclePhase::Complete
                        && active_.release_attempted,
                    active_ && active_.sound == sound,
                    active_
                        && active_.canonical.encode()
                            == canonical_.token.encode(),
                    active_ && exact_type1_onmemory_token(active_.custom),
                    owner_decoded && active_
                        && observed.encode() != active_.custom.encode(),
                };
                result.canonical_rebase_attempted = true;
                result.canonical_rebase_facts = rebase_facts;
                result.stale_canonical = canonical_.token;
                result.candidate_canonical = observed;
                result.prior_custom = active_.custom;
                result.canonical_rebase_first_failure =
                    first_onmemory_bank_canonical_rebase_failure(rebase_facts);
                if (result.canonical_rebase_first_failure
                    != OnMemoryBankCanonicalRebaseFailure::None) {
                    result.decision =
                        OnMemoryBankRouteDecision::OwnerTokenConflict;
                    result.first_failure =
                        OnMemoryBankPlaySetupPreflightFailure::CanonicalRebaseRejected;
                    return result;
                }
                snapshot.query_token = observed;
                snapshot.rebasing_canonical = true;
                snapshot.stale_canonical = canonical_.token;
                snapshot.prior_custom = active_.custom;
                snapshot.completed_ordinal = active_.ordinal;
            } else {
                snapshot.query_token = canonical_.token;
            }
        } else {
            if (active_pending()) {
                result.decision = OnMemoryBankRouteDecision::ReleasePending;
                return result;
            }
            DecodedOnMemoryBankToken deferred_canonical;
            const bool deferred_canonical_decoded = observed_owner_token == 0
                && deferred_canonical_token != 0
                && decode_onmemory_bank_token(
                    deferred_canonical_token, deferred_canonical)
                && exact_type1_onmemory_token(deferred_canonical);
            const bool completed_rearm_exact = observed_owner_token == 0
                && rearm_authority_
                && rearm_authority_.state_epoch == state_epoch_
                && rearm_authority_.sound == sound
                && !failed_ && !release_in_flight_ && !active_pending();
            if (completed_rearm_exact && deferred_canonical_decoded
                && deferred_canonical.encode()
                    != rearm_authority_.canonical.encode()) {
                result.decision = OnMemoryBankRouteDecision::OwnerTokenConflict;
                result.first_failure =
                    OnMemoryBankPlaySetupPreflightFailure::CompletedRearmConflict;
                return result;
            }
            if (!owner_decoded && !deferred_canonical_decoded
                && !completed_rearm_exact) {
                result.decision = OnMemoryBankRouteDecision::OwnerTokenInvalid;
                result.first_failure = !result.owner_token_nonzero
                    ? OnMemoryBankPlaySetupPreflightFailure::OwnerTokenZero
                    : !result.owner_token_decoded
                        ? OnMemoryBankPlaySetupPreflightFailure::OwnerTokenDecodeInvalid
                        : OnMemoryBankPlaySetupPreflightFailure::OwnerTokenNotType1;
                return result;
            }
            snapshot.query_token = completed_rearm_exact
                ? rearm_authority_.canonical
                : deferred_canonical_decoded ? deferred_canonical : observed;
            snapshot.establishing_canonical = true;
            snapshot.deferred_canonical_establishment =
                deferred_canonical_decoded && !completed_rearm_exact;
            snapshot.completed_owner_zero_rearm = completed_rearm_exact;
            if (completed_rearm_exact) {
                snapshot.rearm_generation = rearm_authority_.generation;
                snapshot.rearm_source_ordinal = rearm_authority_.source_ordinal;
                snapshot.rearm_aba_ordinal = rearm_authority_.aba_ordinal;
            }
        }
        result.decision = OnMemoryBankRouteDecision::Allowed;
        result.snapshot = snapshot;
        return result;
    }

    OnMemoryBankPlaySetupCommitResult commit_play_setup_qualification(
        const OnMemoryBankPlaySetupSnapshot& snapshot,
        const OnMemoryBankSoundIdentity& revalidated_sound,
        uint64_t revalidated_owner_token,
        uint64_t revalidated_route_epoch,
        uint32_t canonical_kind) noexcept
    {
        OnMemoryBankPlaySetupCommitResult result;
        if (!snapshot.sound) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::SnapshotSoundInvalid;
            return result;
        }
        if (snapshot.route_epoch == 0) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::SnapshotRouteEpochInvalid;
            return result;
        }
        if (snapshot.state_epoch == 0) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::SnapshotStateEpochInvalid;
            return result;
        }
        if (!exact_type1_onmemory_token(snapshot.query_token)) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::SnapshotQueryTokenInvalid;
            return result;
        }
        if (snapshot.state_epoch != state_epoch_) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::StateEpochDrift;
            return result;
        }
        if (revalidated_sound != snapshot.sound) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::SoundIdentityDrift;
            return result;
        }
        if (revalidated_route_epoch != snapshot.route_epoch) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::RouteEpochDrift;
            return result;
        }
        if (revalidated_owner_token != snapshot.observed_owner_token) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::OwnerTokenDrift;
            return result;
        }
        if (canonical_kind != 2) {
            result.decision = OnMemoryBankRouteDecision::CanonicalKindInvalid;
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::CanonicalKindInvalid;
            return result;
        }
        if (snapshot.completed_owner_zero_rearm) {
            if (!rearm_authority_) {
                result.first_failure =
                    OnMemoryBankPlaySetupCommitFailure::CompletedRearmMissing;
                return result;
            }
            if (canonical_ || failed_ || release_in_flight_ || active_pending()
                || revalidated_owner_token != 0
                || rearm_authority_.state_epoch != state_epoch_
                || rearm_authority_.generation != snapshot.rearm_generation
                || rearm_authority_.source_ordinal
                    != snapshot.rearm_source_ordinal
                || rearm_authority_.aba_ordinal != snapshot.rearm_aba_ordinal
                || rearm_authority_.sound != snapshot.sound
                || rearm_authority_.canonical.encode()
                    != snapshot.query_token.encode()) {
                result.first_failure =
                    OnMemoryBankPlaySetupCommitFailure::CompletedRearmDrift;
                return result;
            }
        }
        if (snapshot.rebasing_canonical) {
            const OnMemoryBankCanonicalRebaseFacts rebase_facts{
                !failed_,
                !release_in_flight_,
                !active_pending(),
                static_cast<bool>(canonical_),
                canonical_.sound == snapshot.sound,
                exact_type1_onmemory_token(snapshot.query_token),
                snapshot.query_token.encode()
                    != snapshot.stale_canonical.encode(),
                active_
                    && active_.phase == OnMemoryBankLifecyclePhase::Complete
                    && active_.release_attempted
                    && active_.ordinal == snapshot.completed_ordinal,
                active_ && active_.sound == snapshot.sound,
                active_ && active_.canonical.encode()
                    == snapshot.stale_canonical.encode(),
                active_ && exact_type1_onmemory_token(active_.custom)
                    && active_.custom.encode() == snapshot.prior_custom.encode(),
                active_ && snapshot.query_token.encode()
                    != active_.custom.encode(),
            };
            result.canonical_rebase_checked = true;
            result.canonical_rebase_facts = rebase_facts;
            result.canonical_rebase_first_failure =
                first_onmemory_bank_canonical_rebase_failure(rebase_facts);
            if (result.canonical_rebase_first_failure
                != OnMemoryBankCanonicalRebaseFailure::None) {
                result.first_failure = OnMemoryBankPlaySetupCommitFailure::CanonicalRebaseRejected;
                return result;
            }
            if (!canonical_) {
                result.first_failure = OnMemoryBankPlaySetupCommitFailure::CanonicalMissing;
                return result;
            }
            if (canonical_.token.encode() != snapshot.stale_canonical.encode()) {
                result.first_failure = OnMemoryBankPlaySetupCommitFailure::CanonicalTokenDrift;
                return result;
            }
            if (canonical_.validation_epoch != snapshot.canonical_epoch) {
                result.first_failure = OnMemoryBankPlaySetupCommitFailure::CanonicalEpochDrift;
                return result;
            }
            canonical_.sound = snapshot.sound;
            canonical_.token = snapshot.query_token;
            canonical_.validation_epoch = ++canonical_epoch_;
            canonical_.kind2_qualified = true;
        } else if (snapshot.establishing_canonical) {
            canonical_.sound = snapshot.sound;
            canonical_.token = snapshot.query_token;
            canonical_.validation_epoch = ++canonical_epoch_;
            canonical_.kind2_qualified = true;
        } else if (!canonical_) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::CanonicalMissing;
            return result;
        } else if (canonical_.sound != snapshot.sound) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::ExistingCanonicalSoundDrift;
            return result;
        } else if (canonical_.token.encode() != snapshot.query_token.encode()) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::CanonicalTokenDrift;
            return result;
        } else if (canonical_.validation_epoch != snapshot.canonical_epoch) {
            result.first_failure = OnMemoryBankPlaySetupCommitFailure::CanonicalEpochDrift;
            return result;
        } else {
            canonical_.validation_epoch = ++canonical_epoch_;
            canonical_.kind2_qualified = true;
        }
        qualification_ = {
            canonical_.sound,
            canonical_.token,
            snapshot.route_epoch,
            canonical_.validation_epoch,
        };
        rearm_authority_ = {};
        ++state_epoch_;
        result.decision = OnMemoryBankRouteDecision::Allowed;
        result.first_failure = OnMemoryBankPlaySetupCommitFailure::None;
        return result;
    }

    bool qualification_matches(
        const OnMemoryBankSoundIdentity& sound,
        uint64_t armed_route_epoch,
        uint64_t owner_token) const noexcept
    {
        return qualification_
            && canonical_
            && qualification_.sound == sound
            && qualification_.route_epoch == armed_route_epoch
            && qualification_.canonical_epoch == canonical_.validation_epoch
            && qualification_.canonical.encode() == canonical_.token.encode()
            && (owner_token == 0 || owner_token == canonical_.token.encode())
            && !failed_ && !release_in_flight_ && !active_pending();
    }

    void cancel_qualification(uint64_t route_epoch) noexcept
    {
        if (qualification_ && qualification_.route_epoch == route_epoch
            && !active_pending() && !release_in_flight_) {
            qualification_ = {};
            ++state_epoch_;
        }
    }

    const OnMemoryBankCanonicalRecord& canonical() const noexcept
    {
        return canonical_;
    }

    // Tri-state retirement classification.
    //
    //   Detached - the custom role observes a *different* type-1 bank than the
    //              canonical role, so the native allocator ran and minted a
    //              bank the mod owns.  Only this outcome populates active_,
    //              and only active_ can reach claim_release()/native release.
    //   Shared   - every other predicate holds but both roles observe the same
    //              valid type-1 bank, i.e. the canonical bank was already
    //              resident and the allocator was never reached.  There is one
    //              bank and the game owns it; it is recorded in shared_ and is
    //              never releasable.
    //   Rejected - some predicate that guards either outcome failed.
    //
    // The two outcomes are distinguished only by the token relationship, which
    // is evaluated once, here.  No downstream flag can promote a shared bank
    // into the detached record.
    OnMemoryBankRetainResult retain_detached(
        const OnMemoryBankSoundIdentity& sound,
        const DecodedOnMemoryBankToken& custom,
        uint64_t qualification_route_epoch,
        uint64_t route_generation,
        uint64_t cleanup_generation,
        uint64_t request_handle,
        void* backing_identity,
        bool backing_observed,
        bool fresh_native_owner_proof,
        bool owner_restore_verified,
        bool publication_succeeded,
        uint32_t canonical_kind,
        uint32_t custom_kind) noexcept
    {
        OnMemoryBankRetainResult result;
        result.canonical_token = canonical_.token.encode();
        result.custom_token = custom.encode();
        result.route_generation = route_generation;
        result.cleanup_generation = cleanup_generation;
        result.first_failure = first_retain_failure(
            sound, custom, qualification_route_epoch, route_generation,
            cleanup_generation, request_handle, backing_observed,
            fresh_native_owner_proof, owner_restore_verified,
            publication_succeeded, canonical_kind, custom_kind);
        if (result.first_failure != OnMemoryBankRetainFailure::None) {
            return result;
        }
        if (!exact_distinct_type1_onmemory_tokens(canonical_.token, custom)) {
            // Same bank in both roles: record the borrow, never ownership.
            //
            // Retire any previous detached record as well.  first_retain_failure
            // has already established !active_pending() && !failed_, so a record
            // still present here is a fully retired Complete one from an earlier
            // cycle.  OnMemoryBankDetachedRecord::operator bool stays true for
            // Complete, and leaving it would make the shared authority read a
            // live detached record and refuse -- which is the ordinary
            // "first song allocated a bank, second song found it resident"
            // session, not an edge case.  Clearing it here also preserves the
            // structural argument: the shared branch owns no detached record at
            // all, so nothing it leaves behind can reach claim_release.
            //
            // Second consumer of that record's survival: completed_reset_matches
            // requires active_ to still hold the Complete record alongside a
            // matching rearm authority, so clearing it here makes that
            // verification fail for a session that still had an outstanding
            // completed-owner-zero rearm authority.  That is fail-closed, and
            // it is superseded in practice by the fresh qualification that must
            // precede any retain, so no compensating change is needed -- but it
            // is a real interaction, recorded so it is not rediscovered as a bug.
            active_ = {};
            shared_ = {};
            shared_.sound = sound;
            shared_.token = canonical_.token;
            shared_.ordinal = next_ordinal_++;
            shared_.route_generation = route_generation;
            shared_.cleanup_generation = cleanup_generation;
            shared_.request_handle = request_handle;
            shared_.owner_restore_verified = true;
            qualification_ = {};
            ++state_epoch_;
            result.outcome = OnMemoryBankRetainOutcome::Shared;
            result.ordinal = shared_.ordinal;
            return result;
        }
        active_ = {};
        active_.phase = OnMemoryBankLifecyclePhase::RestoreApplied;
        active_.sound = sound;
        active_.canonical = canonical_.token;
        active_.custom = custom;
        active_.ordinal = next_ordinal_++;
        active_.route_generation = route_generation;
        active_.cleanup_generation = cleanup_generation;
        active_.request_handle = request_handle;
        active_.backing_identity = backing_identity;
        active_.backing_observed = backing_observed;
        active_.owner_restore_verified = true;
        qualification_ = {};
        ++state_epoch_;
        result.outcome = OnMemoryBankRetainOutcome::Detached;
        result.ordinal = active_.ordinal;
        return result;
    }

    const OnMemoryBankSharedRecord& shared() const noexcept
    {
        return shared_;
    }

    // Match the shared observation against the playback the route recorded as
    // owned.  The native request handle is unique per request, so together
    // with the live sound identity it names the exact playback the record was
    // made for; the route generation bound additionally rejects a record made
    // for a later route.
    bool shared_matches(
        const OnMemoryBankSoundIdentity& sound,
        uint64_t request_handle,
        uint64_t route_generation_bound) const noexcept
    {
        return shared_ && !failed_ && shared_.sound == sound
            && shared_.request_handle == request_handle
            && request_handle != 0
            && shared_.route_generation <= route_generation_bound;
    }

    // Consume the shared observation exactly once, under the caller's audio
    // state lock, as part of the route retirement transaction.
    bool consume_shared(
        const OnMemoryBankSoundIdentity& sound,
        uint64_t request_handle,
        uint64_t route_generation_bound) noexcept
    {
        if (!shared_matches(sound, request_handle, route_generation_bound)) {
            return false;
        }
        shared_ = {};
        ++state_epoch_;
        return true;
    }

    // A resumed native Set/Play creates a new request for the same exact
    // detached bank.  Keep this lifecycle record authoritative instead of
    // creating a parallel ownership record in the pause coordinator.
    bool rebind_detached_request(
        const OnMemoryBankDetachedRecord& expected,
        uint64_t expected_state_epoch,
        uint64_t request_handle,
        void* backing_identity,
        bool backing_observed) noexcept
    {
        if (!active_ || active_.phase != OnMemoryBankLifecyclePhase::RestoreApplied
            || expected_state_epoch == 0 || state_epoch_ != expected_state_epoch
            || release_in_flight_ || failed_ || !backing_observed
            || request_handle == 0
            || active_.ordinal != expected.ordinal
            || active_.route_generation != expected.route_generation
            || active_.cleanup_generation != expected.cleanup_generation
            || active_.sound != expected.sound
            || active_.canonical.encode() != expected.canonical.encode()
            || active_.custom.encode() != expected.custom.encode()
            || active_.request_handle != expected.request_handle
            || active_.backing_observed != expected.backing_observed
            || active_.backing_identity != expected.backing_identity) {
            return false;
        }
        active_.request_handle = request_handle;
        active_.backing_identity = backing_identity;
        active_.backing_observed = true;
        ++state_epoch_;
        return true;
    }

    bool claim_release(
        const OnMemoryBankRetirementFacts& facts,
        OnMemoryBankReleaseAction& out) noexcept
    {
        out = {};
        if (!active_ || active_.phase != OnMemoryBankLifecyclePhase::RestoreApplied
            || release_in_flight_ || failed_
            || facts.route_generation != active_.route_generation + 1
            || facts.cleanup_generation != active_.cleanup_generation
            || facts.retired_request_handle != active_.request_handle
            || facts.current_request_handle == active_.request_handle
            || !onmemory_bank_current_backing_detached(
                active_.backing_identity,
                active_.backing_observed,
                facts.current_backing,
                facts.current_backing_observed)
            || !facts.retired_backing_observed
            || (active_.backing_identity
                && facts.retired_backing != active_.backing_identity)
            || facts.current_owner.sound != active_.sound
            || !facts.current_owner.structural_identity_matches
            || (facts.current_owner.category
                    != OnMemoryBankRetirementOwnerCategory::Canonical
                && facts.current_owner.category
                    != OnMemoryBankRetirementOwnerCategory::Zero)
            || !facts.exact_request_retired || !facts.route_released
            || !facts.playback_released || !facts.cleanup_released
            || !facts.owner_restore_verified || !active_.owner_restore_verified
            || !facts.runtime_installed || !facts.lookup_signature_valid
            || !facts.release_signature_valid || facts.shutdown_or_disabled) {
            return false;
        }
        out = {
            active_.ordinal,
            active_.route_generation,
            active_.cleanup_generation,
            active_.canonical,
            active_.custom,
        };
        active_.phase = OnMemoryBankLifecyclePhase::ReleaseClaimed;
        release_in_flight_ = out;
        ++state_epoch_;
        return true;
    }

    bool finish_release(
        const OnMemoryBankReleaseAction& action,
        OnMemoryBankReleaseOutcome outcome) noexcept
    {
        if (!matches_action(release_in_flight_, action)
            || !matches_active(action)
            || active_.phase != OnMemoryBankLifecyclePhase::ReleaseClaimed
            || active_.release_attempted) {
            return false;
        }
        active_.release_attempted = true;
        release_in_flight_ = {};
        switch (outcome) {
        case OnMemoryBankReleaseOutcome::AlreadyAbsent:
            active_.phase = OnMemoryBankLifecyclePhase::Complete;
            break;
        case OnMemoryBankReleaseOutcome::AsyncReleaseRequested:
            active_.phase = OnMemoryBankLifecyclePhase::ResidencyPending;
            break;
        case OnMemoryBankReleaseOutcome::Failed:
            fail();
            break;
        }
        ++state_epoch_;
        return true;
    }

    bool copy_pending_observation(OnMemoryBankPendingObservation& out) const noexcept
    {
        out = {};
        if (!active_ || active_.phase != OnMemoryBankLifecyclePhase::ResidencyPending
            || release_in_flight_) {
            return false;
        }
        out.action = {
            active_.ordinal,
            active_.route_generation,
            active_.cleanup_generation,
            active_.canonical,
            active_.custom,
        };
        out.observation_count = active_.observation_count;
        return true;
    }

    bool observe(
        const OnMemoryBankPendingObservation& observation,
        uint32_t canonical_kind,
        uint32_t custom_kind,
        uint32_t budget = 300) noexcept
    {
        if (!matches_active(observation.action)
            || active_.phase != OnMemoryBankLifecyclePhase::ResidencyPending
            || observation.observation_count != active_.observation_count) {
            return false;
        }
        if (canonical_kind != 2 || (custom_kind != 2 && custom_kind != 0)) {
            fail();
            ++state_epoch_;
            return true;
        }
        if (custom_kind == 0) {
            if (!active_.sound
                || !exact_distinct_type1_onmemory_tokens(
                    active_.canonical, active_.custom)
                || !active_.owner_restore_verified || !active_.release_attempted
                || active_.ordinal == 0 || active_.route_generation == 0
                || active_.cleanup_generation == 0
                || state_epoch_ == 0 || state_epoch_ == UINT64_MAX
                || next_rearm_generation_ == 0
                || next_rearm_generation_ == UINT64_MAX
                || next_rearm_aba_ordinal_ == 0
                || next_rearm_aba_ordinal_ == UINT64_MAX) {
                fail();
                ++state_epoch_;
                return true;
            }
            active_.phase = OnMemoryBankLifecyclePhase::Complete;
            ++state_epoch_;
            rearm_authority_ = {
                next_rearm_generation_++,
                state_epoch_,
                active_.ordinal,
                next_rearm_aba_ordinal_++,
                active_.route_generation,
                active_.cleanup_generation,
                active_.sound,
                active_.canonical,
                active_.custom,
                canonical_kind,
                custom_kind,
                active_.owner_restore_verified,
                active_.release_attempted,
            };
            return true;
        }
        ++active_.observation_count;
        if (active_.observation_count >= budget) fail();
        ++state_epoch_;
        return true;
    }

    bool custom_route_blocked() const noexcept
    {
        return failed_ || release_in_flight_ || active_pending();
    }

    bool failed() const noexcept { return failed_; }
    bool release_in_flight() const noexcept { return static_cast<bool>(release_in_flight_); }
    const OnMemoryBankDetachedRecord& active() const noexcept { return active_; }
    const OnMemoryBankCompletedOwnerZeroRearmAuthority&
    completed_owner_zero_rearm_authority() const noexcept
    {
        return rearm_authority_;
    }
    uint64_t state_epoch() const noexcept { return state_epoch_; }

    bool reset_completed_for_next_activation(
        const OnMemoryBankDetachedRecord& expected,
        const OnMemoryBankCompletedOwnerZeroRearmAuthority& expected_authority) noexcept
    {
        if (!completed_reset_matches(expected, expected_authority)
            || state_epoch_ == 0 || state_epoch_ == UINT64_MAX
            || next_rearm_generation_ == 0
            || next_rearm_generation_ == UINT64_MAX
            || next_rearm_aba_ordinal_ == 0
            || next_rearm_aba_ordinal_ == UINT64_MAX) {
            return false;
        }
        auto preserved = rearm_authority_;
        preserved.generation = next_rearm_generation_++;
        preserved.aba_ordinal = next_rearm_aba_ordinal_++;
        canonical_ = {};
        qualification_ = {};
        active_ = {};
        shared_ = {};
        release_in_flight_ = {};
        failed_ = false;
        ++state_epoch_;
        preserved.state_epoch = state_epoch_;
        rearm_authority_ = preserved;
        return true;
    }

    bool completed_reset_matches(
        const OnMemoryBankDetachedRecord& expected,
        const OnMemoryBankCompletedOwnerZeroRearmAuthority& expected_authority) const noexcept
    {
        if (!exact_detached_record(active_, expected)
            || active_.phase != OnMemoryBankLifecyclePhase::Complete) {
            return false;
        }
        return rearm_authority_
            && !failed_ && !release_in_flight_ && !qualification_
            && canonical_ && canonical_.sound == active_.sound
            && canonical_.token.encode() == active_.canonical.encode()
            && same_onmemory_bank_completed_owner_zero_rearm_authority(
                rearm_authority_, expected_authority)
            && rearm_authority_.state_epoch == state_epoch_
            && rearm_authority_.source_ordinal == active_.ordinal
            && rearm_authority_.sound == active_.sound
            && rearm_authority_.canonical.encode() == active_.canonical.encode()
            && rearm_authority_.released_custom.encode() == active_.custom.encode()
            && active_.owner_restore_verified && active_.release_attempted
            && state_epoch_ != 0 && state_epoch_ != UINT64_MAX
            && next_rearm_generation_ != 0
            && next_rearm_generation_ != UINT64_MAX
            && next_rearm_aba_ordinal_ != 0
            && next_rearm_aba_ordinal_ != UINT64_MAX;
    }

    void reset() noexcept
    {
        *this = {};
    }

private:
    bool active_pending() const noexcept
    {
        return active_.phase == OnMemoryBankLifecyclePhase::RestoreApplied
            || active_.phase == OnMemoryBankLifecyclePhase::ReleaseClaimed
            || active_.phase == OnMemoryBankLifecyclePhase::ResidencyPending;
    }

    static bool exact_detached_record(
        const OnMemoryBankDetachedRecord& left,
        const OnMemoryBankDetachedRecord& right) noexcept
    {
        return left.phase == right.phase && left.sound == right.sound
            && left.canonical.encode() == right.canonical.encode()
            && left.custom.encode() == right.custom.encode()
            && left.ordinal == right.ordinal
            && left.route_generation == right.route_generation
            && left.cleanup_generation == right.cleanup_generation
            && left.request_handle == right.request_handle
            && left.backing_identity == right.backing_identity
            && left.backing_observed == right.backing_observed
            && left.observation_count == right.observation_count
            && left.owner_restore_verified == right.owner_restore_verified
            && left.release_attempted == right.release_attempted;
    }

    static bool matches_action(
        const OnMemoryBankReleaseAction& left,
        const OnMemoryBankReleaseAction& right) noexcept
    {
        return left && right
            && left.ordinal == right.ordinal
            && left.route_generation == right.route_generation
            && left.cleanup_generation == right.cleanup_generation
            && left.canonical.encode() == right.canonical.encode()
            && left.custom.encode() == right.custom.encode();
    }

    bool matches_active(const OnMemoryBankReleaseAction& action) const noexcept
    {
        return active_ && action
            && active_.ordinal == action.ordinal
            && active_.route_generation == action.route_generation
            && active_.cleanup_generation == action.cleanup_generation
            && active_.canonical.encode() == action.canonical.encode()
            && active_.custom.encode() == action.custom.encode();
    }

    void fail() noexcept
    {
        failed_ = true;
        active_.phase = OnMemoryBankLifecyclePhase::Failed;
        release_in_flight_ = {};
        qualification_ = {};
        rearm_authority_ = {};
        shared_ = {};
    }

    // Ordered evaluation of every predicate that guards *both* retirement
    // outcomes.  The token relationship is deliberately absent: it selects
    // between Detached and Shared rather than rejecting the retirement.
    OnMemoryBankRetainFailure first_retain_failure(
        const OnMemoryBankSoundIdentity& sound,
        const DecodedOnMemoryBankToken& custom,
        uint64_t qualification_route_epoch,
        uint64_t route_generation,
        uint64_t cleanup_generation,
        uint64_t request_handle,
        bool backing_observed,
        bool fresh_native_owner_proof,
        bool owner_restore_verified,
        bool publication_succeeded,
        uint32_t canonical_kind,
        uint32_t custom_kind) const noexcept
    {
        using Failure = OnMemoryBankRetainFailure;
        if (!qualification_) return Failure::QualificationMissing;
        if (!canonical_) return Failure::CanonicalMissing;
        if (failed_) return Failure::LifecycleFailed;
        if (release_in_flight_) return Failure::ReleaseInFlight;
        if (active_pending()) return Failure::ActivePending;
        if (sound != canonical_.sound) return Failure::CanonicalSoundDrift;
        if (qualification_.sound != sound) return Failure::QualificationSoundDrift;
        if (qualification_.route_epoch != qualification_route_epoch) {
            return Failure::QualificationRouteEpochDrift;
        }
        if (qualification_.canonical_epoch != canonical_.validation_epoch) {
            return Failure::CanonicalEpochDrift;
        }
        if (!fresh_native_owner_proof) return Failure::FreshOwnerProofMissing;
        if (!owner_restore_verified) return Failure::OwnerRestoreUnverified;
        if (!publication_succeeded) return Failure::PublicationFailed;
        if (canonical_kind != 2) return Failure::CanonicalKindInvalid;
        if (custom_kind != 2) return Failure::CustomKindInvalid;
        if (qualification_route_epoch == 0) return Failure::RouteEpochZero;
        if (route_generation == 0) return Failure::RouteGenerationZero;
        if (cleanup_generation == 0) return Failure::CleanupGenerationZero;
        if (request_handle == 0) return Failure::RequestHandleZero;
        if (!backing_observed) return Failure::BackingUnobserved;
        if (!exact_type1_onmemory_token(custom)) return Failure::CustomTokenInvalid;
        return Failure::None;
    }

    OnMemoryBankCanonicalRecord canonical_{};
    OnMemoryBankRouteQualification qualification_{};
    OnMemoryBankDetachedRecord active_{};
    OnMemoryBankSharedRecord shared_{};
    OnMemoryBankReleaseAction release_in_flight_{};
    OnMemoryBankCompletedOwnerZeroRearmAuthority rearm_authority_{};
    uint64_t state_epoch_ = 1;
    uint64_t canonical_epoch_ = 0;
    uint64_t next_ordinal_ = 1;
    uint64_t next_rearm_generation_ = 1;
    uint64_t next_rearm_aba_ordinal_ = 1;
    bool failed_ = false;
};

struct OnMemoryBankRetirementSchedule {
    bool claimed = false;
    OnMemoryBankDiagnosticPair diagnostic{};
    OnMemoryBankReleaseAction release{};
};

inline OnMemoryBankRetirementSchedule coordinate_onmemory_bank_retirement(
    OnMemoryBankLifecycleState& lifecycle,
    const OnMemoryBankRetirementFacts& facts,
    const OnMemoryBankDiagnosticPairState& diagnostics,
    uint64_t diagnostic_route_generation,
    uint64_t diagnostic_cleanup_generation) noexcept
{
    OnMemoryBankRetirementSchedule scheduled;
    if (!lifecycle.claim_release(facts, scheduled.release)) return scheduled;
    scheduled.claimed = true;
    (void)diagnostics.copy_exact(
        diagnostic_route_generation,
        diagnostic_cleanup_generation,
        scheduled.diagnostic);
    return scheduled;
}

} // namespace ff7r::piano::game
