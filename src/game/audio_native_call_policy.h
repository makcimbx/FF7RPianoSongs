#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

namespace ff7r::piano::game {

struct BgmPlaybackPendingPlayTransactionFacts final {
    bool candidate_present = false;
    bool authority_exact = false;
    bool unresolved_latch_armed_before_native = false;
    uint32_t native_entry_count = 0;
    bool native_state4_observed = false;
    bool exact_play_published = false;
    bool known_request_attached_unresolved = false;
    bool unknown_publication_latch_armed = false;
    bool exact_request_resolved_or_absent = false;
};

struct BgmPlaybackPendingPlayAuthorityDecisionFacts final {
    bool native_available = false;
    bool authority_candidate_present = false;
    bool owner_patch_candidate_present = false;
    bool authority_exact = false;
};

enum class BgmPlaybackPendingPlayCandidateSource : uint8_t {
    None,
    Authority,
    OwnerPatch,
    Both,
};

constexpr BgmPlaybackPendingPlayCandidateSource
bgm_playback_pending_play_candidate_source(
    const BgmPlaybackPendingPlayAuthorityDecisionFacts& f) noexcept
{
    if (f.authority_candidate_present && f.owner_patch_candidate_present) {
        return BgmPlaybackPendingPlayCandidateSource::Both;
    }
    if (f.authority_candidate_present) {
        return BgmPlaybackPendingPlayCandidateSource::Authority;
    }
    if (f.owner_patch_candidate_present) {
        return BgmPlaybackPendingPlayCandidateSource::OwnerPatch;
    }
    return BgmPlaybackPendingPlayCandidateSource::None;
}

constexpr bool bgm_playback_pending_play_candidate_present(
    const BgmPlaybackPendingPlayAuthorityDecisionFacts& f) noexcept
{
    return bgm_playback_pending_play_candidate_source(f)
        != BgmPlaybackPendingPlayCandidateSource::None;
}

constexpr bool bgm_playback_pending_play_latch_required(
    const BgmPlaybackPendingPlayAuthorityDecisionFacts& f) noexcept
{
    return f.native_available
        && bgm_playback_pending_play_candidate_present(f);
}

constexpr bool bgm_playback_pending_play_normal_publication_allowed(
    const BgmPlaybackPendingPlayAuthorityDecisionFacts& f) noexcept
{
    return f.native_available && f.authority_candidate_present
        && f.authority_exact;
}

constexpr bool bgm_playback_pending_play_ownership_durable(
    const BgmPlaybackPendingPlayTransactionFacts& f) noexcept
{
    return f.candidate_present && f.native_entry_count == 1
        && f.unresolved_latch_armed_before_native
        && (f.exact_play_published || f.known_request_attached_unresolved
            || f.unknown_publication_latch_armed);
}

constexpr bool bgm_playback_pending_play_latch_discharge_allowed(
    const BgmPlaybackPendingPlayTransactionFacts& f) noexcept
{
    return bgm_playback_pending_play_ownership_durable(f)
        && !f.unknown_publication_latch_armed;
}

constexpr bool bgm_playback_pending_play_release_blocked(
    const BgmPlaybackPendingPlayTransactionFacts& f) noexcept
{
    return f.native_entry_count == 1
        && !f.exact_request_resolved_or_absent
        && (f.unresolved_latch_armed_before_native
            || f.known_request_attached_unresolved
            || f.unknown_publication_latch_armed)
        && !f.exact_play_published;
}

enum class BgmPlaybackProtectedNativeFault : uint8_t {
    None,
    NativeFailure,
    CppException,
    StructuredException,
};

struct BgmPlaybackProtectedNativeResult final {
    bool succeeded = false;
    BgmPlaybackProtectedNativeFault fault =
        BgmPlaybackProtectedNativeFault::NativeFailure;
};

enum class BgmPlaybackNativeSetTarget : uint8_t {
    None,
    Trampoline,
    DirectRva,
};

enum class BgmPlaybackNativeSetCallResult : uint8_t {
    NotCalled,
    CalledSucceeded,
    CalledFailed,
    CppException,
    StructuredException,
};

struct BgmPlaybackNativeSetCallOutcome final {
    BgmPlaybackNativeSetTarget target = BgmPlaybackNativeSetTarget::None;
    BgmPlaybackNativeSetCallResult result =
        BgmPlaybackNativeSetCallResult::NotCalled;
    uint8_t call_count = 0;
};

constexpr BgmPlaybackNativeSetTarget select_bgm_playback_native_set_target(
    bool trampoline_available, bool direct_rva_available) noexcept
{
    return trampoline_available ? BgmPlaybackNativeSetTarget::Trampoline
        : direct_rva_available ? BgmPlaybackNativeSetTarget::DirectRva
                               : BgmPlaybackNativeSetTarget::None;
}

constexpr bool bgm_playback_native_set_target_available(
    BgmPlaybackNativeSetTarget target) noexcept
{
    return target != BgmPlaybackNativeSetTarget::None;
}

constexpr bool bgm_playback_native_set_call_outcome_valid(
    const BgmPlaybackNativeSetCallOutcome& outcome) noexcept
{
    if (outcome.call_count > 1) return false;
    switch (outcome.result) {
    case BgmPlaybackNativeSetCallResult::NotCalled:
        return outcome.call_count == 0
            && outcome.target == BgmPlaybackNativeSetTarget::None;
    case BgmPlaybackNativeSetCallResult::CalledSucceeded:
    case BgmPlaybackNativeSetCallResult::CalledFailed:
    case BgmPlaybackNativeSetCallResult::CppException:
        return outcome.call_count == 1
            && bgm_playback_native_set_target_available(outcome.target);
    case BgmPlaybackNativeSetCallResult::StructuredException:
        return bgm_playback_native_set_target_available(outcome.target);
    }
    return false;
}

constexpr BgmPlaybackNativeSetCallOutcome
bgm_playback_native_set_structured_exception_outcome(
    BgmPlaybackNativeSetTarget target, bool call_started) noexcept
{
    return {target, BgmPlaybackNativeSetCallResult::StructuredException,
        static_cast<uint8_t>(call_started ? 1 : 0)};
}

template <typename Invoke, typename... Args>
BgmPlaybackNativeSetCallOutcome invoke_bgm_playback_native_set_exactly_once(
    BgmPlaybackNativeSetTarget target, Invoke&& invoke, Args&&... args) noexcept
{
    if (!bgm_playback_native_set_target_available(target)) return {};
    try {
        const bool succeeded = std::forward<Invoke>(invoke)(target,
            std::forward<Args>(args)...);
        return {target, succeeded
                ? BgmPlaybackNativeSetCallResult::CalledSucceeded
                : BgmPlaybackNativeSetCallResult::CalledFailed,
            1};
    } catch (...) {
        return {target, BgmPlaybackNativeSetCallResult::CppException, 1};
    }
}

constexpr BgmPlaybackProtectedNativeResult
bgm_playback_native_set_compatibility_result(
    const BgmPlaybackNativeSetCallOutcome& outcome) noexcept
{
    switch (outcome.result) {
    case BgmPlaybackNativeSetCallResult::NotCalled:
    case BgmPlaybackNativeSetCallResult::CalledSucceeded:
        return {true, BgmPlaybackProtectedNativeFault::None};
    case BgmPlaybackNativeSetCallResult::CalledFailed:
        return {false, BgmPlaybackProtectedNativeFault::NativeFailure};
    case BgmPlaybackNativeSetCallResult::CppException:
        return {false, BgmPlaybackProtectedNativeFault::CppException};
    case BgmPlaybackNativeSetCallResult::StructuredException:
        return {false, BgmPlaybackProtectedNativeFault::StructuredException};
    }
    return {false, BgmPlaybackProtectedNativeFault::NativeFailure};
}

enum class BgmPlaybackNativePlayTarget : uint8_t {
    None,
    Trampoline,
};

enum class BgmPlaybackNativePlayCallResult : uint8_t {
    NotCalled,
    CalledSucceeded,
    CalledFailed,
    CppException,
    StructuredException,
};

struct BgmPlaybackNativePlayCallOutcome final {
    BgmPlaybackNativePlayTarget target = BgmPlaybackNativePlayTarget::None;
    BgmPlaybackNativePlayCallResult result =
        BgmPlaybackNativePlayCallResult::NotCalled;
    uint8_t call_count = 0;
};

constexpr BgmPlaybackNativePlayTarget select_bgm_playback_native_play_target(
    const bool trampoline_available) noexcept
{
    return trampoline_available ? BgmPlaybackNativePlayTarget::Trampoline
                                : BgmPlaybackNativePlayTarget::None;
}

constexpr bool bgm_playback_native_play_target_available(
    const BgmPlaybackNativePlayTarget target) noexcept
{
    return target == BgmPlaybackNativePlayTarget::Trampoline;
}

constexpr bool bgm_playback_native_play_call_outcome_valid(
    const BgmPlaybackNativePlayCallOutcome& outcome) noexcept
{
    if (outcome.call_count > 1) return false;
    switch (outcome.result) {
    case BgmPlaybackNativePlayCallResult::NotCalled:
        return outcome.call_count == 0
            && outcome.target == BgmPlaybackNativePlayTarget::None;
    case BgmPlaybackNativePlayCallResult::CalledSucceeded:
    case BgmPlaybackNativePlayCallResult::CalledFailed:
    case BgmPlaybackNativePlayCallResult::CppException:
        return outcome.call_count == 1
            && bgm_playback_native_play_target_available(outcome.target);
    case BgmPlaybackNativePlayCallResult::StructuredException:
        return bgm_playback_native_play_target_available(outcome.target);
    }
    return false;
}

constexpr BgmPlaybackNativePlayCallOutcome
bgm_playback_native_play_structured_exception_outcome(
    const BgmPlaybackNativePlayTarget target,
    const bool call_started) noexcept
{
    return {target, BgmPlaybackNativePlayCallResult::StructuredException,
        static_cast<uint8_t>(call_started ? 1 : 0)};
}

template <typename Invoke, typename... Args>
BgmPlaybackNativePlayCallOutcome invoke_bgm_playback_native_play_exactly_once(
    const BgmPlaybackNativePlayTarget target, Invoke&& invoke,
    Args&&... args) noexcept
{
    if (!bgm_playback_native_play_target_available(target)) return {};
    try {
        const bool succeeded = std::forward<Invoke>(invoke)(target,
            std::forward<Args>(args)...);
        return {target, succeeded
                ? BgmPlaybackNativePlayCallResult::CalledSucceeded
                : BgmPlaybackNativePlayCallResult::CalledFailed,
            1};
    } catch (...) {
        return {target, BgmPlaybackNativePlayCallResult::CppException, 1};
    }
}

constexpr BgmPlaybackProtectedNativeResult
bgm_playback_native_play_compatibility_result(
    const BgmPlaybackNativePlayCallOutcome& outcome) noexcept
{
    switch (outcome.result) {
    case BgmPlaybackNativePlayCallResult::NotCalled:
    case BgmPlaybackNativePlayCallResult::CalledSucceeded:
        return {true, BgmPlaybackProtectedNativeFault::None};
    case BgmPlaybackNativePlayCallResult::CalledFailed:
        return {false, BgmPlaybackProtectedNativeFault::NativeFailure};
    case BgmPlaybackNativePlayCallResult::CppException:
        return {false, BgmPlaybackProtectedNativeFault::CppException};
    case BgmPlaybackNativePlayCallResult::StructuredException:
        return {false, BgmPlaybackProtectedNativeFault::StructuredException};
    }
    return {false, BgmPlaybackProtectedNativeFault::NativeFailure};
}

struct BgmPlaybackNativePlayOrchestrationFacts final {
    bool candidate_present = false;
    bool authority_exact = false;
    bool unresolved_latch_armed_before_native = false;
    bool native_available = false;
    bool observe_failure_without_native = false;
    bool retention_candidate_present = false;
};

struct BgmPlaybackNativePlayCaptureResult final {
    bool state4_observed = false;
};

struct BgmPlaybackNativePlayOrchestrationResult final {
    BgmPlaybackNativePlayCallOutcome native{};
    BgmPlaybackProtectedNativeResult compatibility{};
    bool capture_completed = false;
    bool cleanup_completed = false;
    bool observation_performed = false;
    bool retention_attempted = false;
    bool retained_publication = false;
    bool unknown_publication_latched = false;
    bool publication_durable = false;
    bool latch_discharge_requested = false;
};

template <typename Invoke, typename Capture, typename Cleanup,
    typename Observe, typename Retain, typename Discharge>
BgmPlaybackNativePlayOrchestrationResult
run_bgm_playback_native_play_orchestration(
    const BgmPlaybackNativePlayTarget target,
    const BgmPlaybackNativePlayOrchestrationFacts& facts,
    Invoke&& invoke, Capture&& capture, Cleanup&& cleanup,
    Observe&& observe, Retain&& retain, Discharge&& discharge)
{
    BgmPlaybackNativePlayOrchestrationResult out;
    try {
        out.native = std::forward<Invoke>(invoke)(target);
    } catch (...) {
        out.native = bgm_playback_native_play_target_available(target)
            ? BgmPlaybackNativePlayCallOutcome{target,
                BgmPlaybackNativePlayCallResult::CppException, 1}
            : BgmPlaybackNativePlayCallOutcome{};
    }

    const auto native_result =
        bgm_playback_native_play_compatibility_result(out.native);
    auto captured = std::forward<Capture>(capture)();
    out.capture_completed = true;
    std::forward<Cleanup>(cleanup)();
    out.cleanup_completed = true;

    const bool observation_allowed = facts.native_available
        || (facts.observe_failure_without_native && !native_result.succeeded);
    if (observation_allowed) {
        std::forward<Observe>(observe)(native_result, captured);
        out.observation_performed = true;
    }
    if (facts.native_available && facts.retention_candidate_present) {
        out.retained_publication =
            std::forward<Retain>(retain)(captured);
        out.retention_attempted = true;
    }

    out.unknown_publication_latched =
        facts.unresolved_latch_armed_before_native
        && !out.retained_publication;
    out.publication_durable = !facts.unresolved_latch_armed_before_native
        || bgm_playback_pending_play_latch_discharge_allowed({
            facts.candidate_present, facts.authority_exact,
            facts.unresolved_latch_armed_before_native,
            out.native.call_count, captured.state4_observed,
            false, out.retained_publication,
            out.unknown_publication_latched, false});
    out.latch_discharge_requested =
        facts.unresolved_latch_armed_before_native && out.publication_durable;
    std::forward<Discharge>(discharge)(out.latch_discharge_requested);
    out.compatibility = bgm_playback_native_play_compatibility_result(out.native);
    return out;
}

static_assert(std::is_trivially_copyable_v<BgmPlaybackNativePlayCallOutcome>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativePlayOrchestrationFacts>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativePlayCaptureResult>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativePlayOrchestrationResult>);

enum class BgmPlaybackNativeStopTarget : uint8_t {
    None,
    Trampoline,
};

enum class BgmPlaybackNativeStopCallResult : uint8_t {
    NotCalled,
    CalledReturned,
};

struct BgmPlaybackNativeStopCallOutcome final {
    BgmPlaybackNativeStopTarget target = BgmPlaybackNativeStopTarget::None;
    BgmPlaybackNativeStopCallResult result =
        BgmPlaybackNativeStopCallResult::NotCalled;
    uint8_t call_count = 0;
};

struct BgmPlaybackNativeStopObservationResult final {
    BgmPlaybackNativeStopCallOutcome native{};
    bool post_observation_ran = false;
    bool post_observation_succeeded = false;
    bool original_forwarded = false;
};

constexpr BgmPlaybackNativeStopTarget select_bgm_playback_native_stop_target(
    const bool trampoline_available) noexcept
{
    return trampoline_available ? BgmPlaybackNativeStopTarget::Trampoline
                                : BgmPlaybackNativeStopTarget::None;
}

constexpr bool bgm_playback_native_stop_target_available(
    const BgmPlaybackNativeStopTarget target) noexcept
{
    return target == BgmPlaybackNativeStopTarget::Trampoline;
}

constexpr bool bgm_playback_native_stop_call_outcome_valid(
    const BgmPlaybackNativeStopCallOutcome& outcome) noexcept
{
    if (outcome.call_count > 1) return false;
    switch (outcome.result) {
    case BgmPlaybackNativeStopCallResult::NotCalled:
        return outcome.target == BgmPlaybackNativeStopTarget::None
            && outcome.call_count == 0;
    case BgmPlaybackNativeStopCallResult::CalledReturned:
        return outcome.target == BgmPlaybackNativeStopTarget::Trampoline
            && outcome.call_count == 1;
    }
    return false;
}

constexpr bool bgm_playback_native_stop_original_forwarded(
    const BgmPlaybackNativeStopCallOutcome& outcome) noexcept
{
    // Compatibility marker: the admitted forwarding wrapper returned normally,
    // including the historical missing-trampoline no-op path.
    return bgm_playback_native_stop_call_outcome_valid(outcome);
}

template <typename Invoke, typename... Args>
BgmPlaybackNativeStopCallOutcome invoke_bgm_playback_native_stop_exactly_once(
    const BgmPlaybackNativeStopTarget target, Invoke&& invoke, Args&&... args)
{
    if (!bgm_playback_native_stop_target_available(target)) return {};
    std::forward<Invoke>(invoke)(target, std::forward<Args>(args)...);
    return {target, BgmPlaybackNativeStopCallResult::CalledReturned, 1};
}

template <typename Invoke, typename Observe, typename... Args>
BgmPlaybackNativeStopObservationResult
run_bgm_playback_native_stop_post_observation(
    const BgmPlaybackNativeStopTarget target, Invoke&& invoke,
    Observe&& observe, Args&&... args)
{
    BgmPlaybackNativeStopObservationResult out;
    out.native = invoke_bgm_playback_native_stop_exactly_once(target,
        std::forward<Invoke>(invoke), std::forward<Args>(args)...);
    out.original_forwarded =
        bgm_playback_native_stop_original_forwarded(out.native);
    out.post_observation_succeeded =
        std::forward<Observe>(observe)(out.native);
    out.post_observation_ran = true;
    return out;
}

static_assert(std::is_trivially_copyable_v<BgmPlaybackNativeStopCallOutcome>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativeStopObservationResult>);

enum class BgmPlaybackNativePlaySetupTarget : uint8_t {
    None,
    Trampoline,
};

enum class BgmPlaybackNativePlaySetupCallResult : uint8_t {
    NotCalled,
    CalledReturned,
};

struct BgmPlaybackNativePlaySetupCallOutcome final {
    BgmPlaybackNativePlaySetupTarget target =
        BgmPlaybackNativePlaySetupTarget::None;
    BgmPlaybackNativePlaySetupCallResult result =
        BgmPlaybackNativePlaySetupCallResult::NotCalled;
    uint8_t call_count = 0;
};

struct BgmPlaybackNativePlaySetupTlsSnapshot final {
    uint32_t original_depth = 0;
    uint64_t expected_generation = 0;
    bool play_claimed = false;
};

struct BgmPlaybackNativePlaySetupAttemptState final {
    BgmPlaybackNativePlaySetupTarget selected_target =
        BgmPlaybackNativePlaySetupTarget::None;
    uint8_t attempt_count = 0;
    bool invocation_started = false;
    bool native_return_captured = false;
};

struct BgmPlaybackNativePlaySetupReturnObservation final {
    BgmPlaybackNativePlaySetupAttemptState attempt{};
    BgmPlaybackNativePlaySetupCallOutcome native{};
    BgmPlaybackNativePlaySetupTlsSnapshot tls{};
    bool post_return_observation_ran = false;
    bool original_forwarded = false;
};

constexpr BgmPlaybackNativePlaySetupTarget
select_bgm_playback_native_play_setup_target(
    const bool trampoline_available) noexcept
{
    return trampoline_available ? BgmPlaybackNativePlaySetupTarget::Trampoline
                                : BgmPlaybackNativePlaySetupTarget::None;
}

constexpr bool bgm_playback_native_play_setup_target_available(
    const BgmPlaybackNativePlaySetupTarget target) noexcept
{
    return target == BgmPlaybackNativePlaySetupTarget::Trampoline;
}

constexpr bool bgm_playback_native_play_setup_call_outcome_valid(
    const BgmPlaybackNativePlaySetupCallOutcome& outcome) noexcept
{
    if (outcome.call_count > 1) return false;
    switch (outcome.result) {
    case BgmPlaybackNativePlaySetupCallResult::NotCalled:
        return outcome.target == BgmPlaybackNativePlaySetupTarget::None
            && outcome.call_count == 0;
    case BgmPlaybackNativePlaySetupCallResult::CalledReturned:
        return outcome.target == BgmPlaybackNativePlaySetupTarget::Trampoline
            && outcome.call_count == 1;
    }
    return false;
}

constexpr bool bgm_playback_native_play_setup_original_forwarded(
    const BgmPlaybackNativePlaySetupCallOutcome& outcome) noexcept
{
    // Compatibility marker: the admitted forwarding wrapper returned normally,
    // including the historical missing-trampoline no-op path.
    return bgm_playback_native_play_setup_call_outcome_valid(outcome);
}

constexpr bool bgm_playback_native_play_setup_return_tuple_authoritative(
    const BgmPlaybackNativePlaySetupAttemptState& attempt) noexcept
{
    return attempt.native_return_captured;
}

template <typename Invoke, typename... Args>
BgmPlaybackNativePlaySetupCallOutcome
invoke_bgm_playback_native_play_setup_exactly_once(
    const BgmPlaybackNativePlaySetupTarget target, Invoke&& invoke,
    Args&&... args)
{
    if (!bgm_playback_native_play_setup_target_available(target)) return {};
    std::forward<Invoke>(invoke)(target, std::forward<Args>(args)...);
    return {target, BgmPlaybackNativePlaySetupCallResult::CalledReturned, 1};
}

template <typename Invoke, typename Observe, typename PublishAttempt,
          typename... Args>
BgmPlaybackNativePlaySetupReturnObservation
run_bgm_playback_native_play_setup_return_observation(
    const BgmPlaybackNativePlaySetupTarget target, Invoke&& invoke,
    Observe&& observe, PublishAttempt&& publish_attempt, Args&&... args)
{
    BgmPlaybackNativePlaySetupReturnObservation out;
    out.attempt.selected_target = target;
    out.attempt.attempt_count = 1;
    out.attempt.invocation_started =
        bgm_playback_native_play_setup_target_available(target);
    std::forward<PublishAttempt>(publish_attempt)(out.attempt);
    out.native = invoke_bgm_playback_native_play_setup_exactly_once(target,
        std::forward<Invoke>(invoke), std::forward<Args>(args)...);
    out.tls = std::forward<Observe>(observe)(out.native);
    out.post_return_observation_ran = true;
    out.original_forwarded =
        bgm_playback_native_play_setup_original_forwarded(out.native);
    out.attempt.native_return_captured = true;
    std::forward<PublishAttempt>(publish_attempt)(out.attempt);
    return out;
}

static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativePlaySetupCallOutcome>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativePlaySetupTlsSnapshot>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativePlaySetupAttemptState>);
static_assert(std::is_trivially_copyable_v<
    BgmPlaybackNativePlaySetupReturnObservation>);

constexpr bool bgm_playback_set_outcome_publication_owed(
    bool aggregate_candidate_active, bool native_callback_entered) noexcept
{
    return aggregate_candidate_active && native_callback_entered;
}

constexpr bool bgm_playback_set_outcome_retention_required(
    bool publication_owed, bool native_succeeded,
    bool exact_success_provenance_published) noexcept
{
    return publication_owed
        && (!native_succeeded || !exact_success_provenance_published);
}

constexpr bool bgm_playback_faulted_set_request_exact(
    bool controller_chain_exact, bool sound_identity_exact,
    bool valid_type8_request, uint8_t state) noexcept
{
    return controller_chain_exact && sound_identity_exact
        && valid_type8_request && (state == 2 || state == 4);
}

template <typename Invoke, typename Cleanup>
BgmPlaybackProtectedNativeResult bgm_playback_protected_native_boundary(
    Invoke&& invoke, Cleanup&& cleanup) noexcept
{
    static_assert(noexcept(cleanup()),
        "protected native cleanup must be nonthrowing");
    BgmPlaybackProtectedNativeResult result;
    try {
        result = invoke();
    } catch (...) {
        result = {false, BgmPlaybackProtectedNativeFault::CppException};
    }
    cleanup();
    return result;
}

} // namespace ff7r::piano::game
