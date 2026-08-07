#include "game/scoreinfo_overlay.h"
#include "game/scoreinfo_result_policy.h"

#include "core/hooks.h"
#include "game/hook_specs.h"
#include "game/module_hooks.h"
#include "game/rvas.h"
#include "game/title.h"
#include "game/ue_types.h"

#include "core/logging.h"
#include "core/pe_image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <mutex>
#include <memory>
#include <sstream>
#include <thread>
#include <Windows.h>

namespace ff7r::piano::game {
namespace {

template <typename T>
void write_row_field(std::array<uint8_t, ScoreInfoOverlayRow::kRowSize>& row, uintptr_t offset, const T& value)
{
    if (offset + sizeof(T) <= row.size()) {
        std::memcpy(row.data() + offset, &value, sizeof(T));
    }
}

template <typename T>
T read_row_field(const std::array<uint8_t, ScoreInfoOverlayRow::kRowSize>& row, uintptr_t offset, T fallback = {})
{
    if (offset + sizeof(T) <= row.size()) {
        std::memcpy(&fallback, row.data() + offset, sizeof(T));
    }
    return fallback;
}

using Mode48GenericHelperFn = uintptr_t(__fastcall*)(void* arg0, void* arg1, void* arg2, void* arg3);
using FNameCtorFn = void*(__fastcall*)(FNameValue* out_name, const wchar_t* text, int32_t find_type);

core::RawRvaHook g_scoreinfo_hook;
Mode48GenericHelperFn g_original_scoreinfo_resolver = nullptr;
uintptr_t g_module_base = 0;
std::size_t g_module_size = 0;
ScoreInfoPublicationEpoch g_overlay_epoch;
thread_local unsigned g_scoreinfo_resolver_depth = 0;
thread_local unsigned g_scoreinfo_piano_detail_update_depth = 0;
struct ScoreInfoResultRuntimeState {
    ScoreInfoResultPolicyState policy{};
    std::shared_ptr<const SongRegistryStorage> storage;
    const SongDescriptor* song = nullptr;
    const SongDifficultyProfile* profile = nullptr;
    CustomContextToken token{};
    std::array<std::shared_ptr<const ScoreInfoOverlayRow>, 4> rows{};
    std::uint8_t row_count = 0;
    std::uint8_t diagnostic_count = 0;
};
ScoreInfoResultRuntimeState g_scoreinfo_result;

constexpr ScoreInfoResultCatalogRole scoreinfo_result_catalog_role_from_rva(
    const std::uintptr_t caller_rva) noexcept
{
    switch (caller_rva) {
    case rva::ScoreInfoStyleSetupReturn: return ScoreInfoResultCatalogRole::StyleSetup;
    case rva::ScoreInfoDetailReturn: return ScoreInfoResultCatalogRole::Detail;
    case rva::ScoreInfoProgressSourceReturn: return ScoreInfoResultCatalogRole::ProgressSource;
    case rva::ScoreInfoRankTextReturn: return ScoreInfoResultCatalogRole::RankText;
    case rva::ScoreInfoThresholdsReturn: return ScoreInfoResultCatalogRole::Thresholds;
    case rva::ScoreInfoMenuDetailReturn: return ScoreInfoResultCatalogRole::MenuDetail;
    default: return ScoreInfoResultCatalogRole::Unavailable;
    }
}

uintptr_t to_rva(void* caller_address)
{
    if (!g_module_base) {
        return 0;
    }
    const auto caller = reinterpret_cast<uintptr_t>(caller_address);
    return g_module_size != 0 && caller >= g_module_base
        && caller - g_module_base < g_module_size
        ? caller - g_module_base : 0;
}

bool construct_fname_find(const wchar_t* text, FNameValue& out)
{
    if (!g_module_base || !text || !*text) {
        return false;
    }
    auto* ctor = reinterpret_cast<FNameCtorFn>(g_module_base + rva::FNameCtor);
    __try {
        out = {};
        ctor(&out, text, 0);
        return out.comparison_id != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
        return false;
    }
}

struct ScoreInfoResultDiagnostic {
    bool emit = false;
    ScoreInfoResultCaller caller = ScoreInfoResultCaller::Unavailable;
    ScoreInfoResultPhase before = ScoreInfoResultPhase::Empty;
    ScoreInfoResultPhase after = ScoreInfoResultPhase::Empty;
    ScoreInfoResultFailure failure = ScoreInfoResultFailure::None;
    ScoreInfoResultControlFlowReason control_flow = ScoreInfoResultControlFlowReason::PolicyDispatched;
    std::uint64_t generation = 0;
    std::uintptr_t caller_rva = 0;
    bool caller_in_image = false;
    bool result_wrapper_exact = false;
    bool wrapper_readable = false;
    bool discriminator_readable = false;
    bool source_readable = false;
    bool source_revalidated = false;
    bool authority_exact = false;
    bool source_exact = false;
    bool thread_exact = false;
    bool published = false;
    std::array<std::int32_t, 4> source_thresholds{};
    std::array<std::int32_t, 4> published_thresholds{};
};

const char* scoreinfo_result_control_flow_name(
    const ScoreInfoResultControlFlowReason reason) noexcept
{
    switch (reason) {
    case ScoreInfoResultControlFlowReason::CallbackRejected: return "callback_rejected";
    case ScoreInfoResultControlFlowReason::CallerUnavailable: return "caller_unavailable";
    case ScoreInfoResultControlFlowReason::Arg0Null: return "arg0_null";
    case ScoreInfoResultControlFlowReason::NonOutermost: return "non_outermost";
    case ScoreInfoResultControlFlowReason::PolicyDispatched: return "policy_dispatched";
    }
    return "unknown";
}

const char* scoreinfo_result_caller_name(const ScoreInfoResultCaller caller) noexcept
{
    switch (caller) {
    case ScoreInfoResultCaller::StyleSetup: return "style_setup";
    case ScoreInfoResultCaller::Detail: return "detail";
    case ScoreInfoResultCaller::ProgressSource: return "progress_source";
    case ScoreInfoResultCaller::RankText: return "rank_text";
    case ScoreInfoResultCaller::Thresholds: return "thresholds";
    case ScoreInfoResultCaller::Unavailable: return "unavailable";
    }
    return "unknown";
}

const char* scoreinfo_result_phase_name(const ScoreInfoResultPhase phase) noexcept
{
    switch (phase) {
    case ScoreInfoResultPhase::Empty: return "empty";
    case ScoreInfoResultPhase::StylePublished: return "style_published";
    case ScoreInfoResultPhase::PlaybackRevoked: return "playback_revoked";
    case ScoreInfoResultPhase::DetailPublished: return "detail_published";
    case ScoreInfoResultPhase::ProgressObserved: return "progress_observed";
    case ScoreInfoResultPhase::RankPublished: return "rank_published";
    case ScoreInfoResultPhase::Closed: return "closed";
    case ScoreInfoResultPhase::Poisoned: return "poisoned";
    case ScoreInfoResultPhase::Retired: return "retired";
    }
    return "unknown";
}

const char* scoreinfo_result_failure_name(const ScoreInfoResultFailure failure) noexcept
{
    switch (failure) {
    case ScoreInfoResultFailure::None: return "none";
    case ScoreInfoResultFailure::CallerUnavailable: return "caller_unavailable";
    case ScoreInfoResultFailure::OriginalUnavailable: return "original_unavailable";
    case ScoreInfoResultFailure::ResultWrapperMismatch: return "result_wrapper_mismatch";
    case ScoreInfoResultFailure::WrapperUnavailable: return "wrapper_unavailable";
    case ScoreInfoResultFailure::WrapperDiscriminatorUnavailable: return "wrapper_discriminator_unavailable";
    case ScoreInfoResultFailure::SourceRowUnavailable: return "source_row_unavailable";
    case ScoreInfoResultFailure::SourceRowDrift: return "source_row_drift";
    case ScoreInfoResultFailure::SourceKeyUnavailable: return "source_key_unavailable";
    case ScoreInfoResultFailure::SourceKeyMismatch: return "source_key_mismatch";
    case ScoreInfoResultFailure::SourceDiscriminatorMismatch: return "source_discriminator_mismatch";
    case ScoreInfoResultFailure::SourceBgmMismatch: return "source_bgm_mismatch";
    case ScoreInfoResultFailure::PlaybackUnavailable: return "playback_unavailable";
    case ScoreInfoResultFailure::AuthorityUnavailable: return "authority_unavailable";
    case ScoreInfoResultFailure::StorageMismatch: return "storage_mismatch";
    case ScoreInfoResultFailure::RegistryGenerationMismatch: return "registry_generation_mismatch";
    case ScoreInfoResultFailure::SongMismatch: return "song_mismatch";
    case ScoreInfoResultFailure::ProfileMismatch: return "profile_mismatch";
    case ScoreInfoResultFailure::ProfileIndexMismatch: return "profile_index_mismatch";
    case ScoreInfoResultFailure::TokenMismatch: return "token_mismatch";
    case ScoreInfoResultFailure::CleanupUnrelated: return "cleanup_unrelated";
    case ScoreInfoResultFailure::PlaybackNotRevoked: return "playback_not_revoked";
    case ScoreInfoResultFailure::DetailScopeMismatch: return "detail_scope_mismatch";
    case ScoreInfoResultFailure::ConsumerThreadUnavailable: return "consumer_thread_unavailable";
    case ScoreInfoResultFailure::ConsumerThreadMismatch: return "consumer_thread_mismatch";
    case ScoreInfoResultFailure::GenerationMismatch: return "generation_mismatch";
    case ScoreInfoResultFailure::SupersessionBeforeClose: return "supersession_before_close";
    case ScoreInfoResultFailure::NoActiveAuthority: return "no_active_authority";
    case ScoreInfoResultFailure::OutOfOrder: return "out_of_order";
    case ScoreInfoResultFailure::DuplicateCaller: return "duplicate_caller";
    case ScoreInfoResultFailure::RowConstructionFailed: return "row_construction_failed";
    case ScoreInfoResultFailure::PublicationFailed: return "publication_failed";
    case ScoreInfoResultFailure::RevalidationFailed: return "revalidation_failed";
    case ScoreInfoResultFailure::ListRetired: return "list_retired";
    case ScoreInfoResultFailure::Shutdown: return "shutdown";
    }
    return "unknown";
}

void log_scoreinfo_result_diagnostic(const ScoreInfoResultDiagnostic& diagnostic) noexcept
{
    if (!diagnostic.emit) return;
    try {
        std::ostringstream out;
        out << "[scoreinfo_result] generation=" << diagnostic.generation
            << " caller=" << scoreinfo_result_caller_name(diagnostic.caller)
            << " control_flow=" << scoreinfo_result_control_flow_name(diagnostic.control_flow)
            << " caller_in_image=" << (diagnostic.caller_in_image ? 1 : 0)
            << " caller_rva=0x" << std::hex << diagnostic.caller_rva << std::dec
            << " before=" << scoreinfo_result_phase_name(diagnostic.before)
            << " after=" << scoreinfo_result_phase_name(diagnostic.after)
            << " rejection=" << scoreinfo_result_failure_name(diagnostic.failure)
            << " result_wrapper_exact=" << (diagnostic.result_wrapper_exact ? 1 : 0)
            << " wrapper_readable=" << (diagnostic.wrapper_readable ? 1 : 0)
            << " discriminator_readable=" << (diagnostic.discriminator_readable ? 1 : 0)
            << " source_readable=" << (diagnostic.source_readable ? 1 : 0)
            << " source_revalidated=" << (diagnostic.source_revalidated ? 1 : 0)
            << " authority_exact=" << (diagnostic.authority_exact ? 1 : 0)
            << " source_exact=" << (diagnostic.source_exact ? 1 : 0)
            << " thread_exact=" << (diagnostic.thread_exact ? 1 : 0)
            << " published=" << (diagnostic.published ? 1 : 0)
            << " source_thresholds=" << diagnostic.source_thresholds[0]
            << ',' << diagnostic.source_thresholds[1]
            << ',' << diagnostic.source_thresholds[2]
            << ',' << diagnostic.source_thresholds[3]
            << " published_thresholds=" << diagnostic.published_thresholds[0]
            << ',' << diagnostic.published_thresholds[1]
            << ',' << diagnostic.published_thresholds[2]
            << ',' << diagnostic.published_thresholds[3];
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

bool scoreinfo_policy_state_equal(
    const ScoreInfoResultPolicyState& left,
    const ScoreInfoResultPolicyState& right) noexcept
{
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

ScoreInfoResultAuthorityIdentity scoreinfo_result_authority_identity(
    const RegistrySnapshot& registry_snapshot,
    const SelectionSnapshot& selection,
    const CustomContextToken& token) noexcept
{
    ScoreInfoResultAuthorityIdentity identity{};
    if (!registry_snapshot || !selection.song || !selection.profile || !token.valid()
        || selection.generation != registry_snapshot.generation
        || selection.storage.get() != registry_snapshot.storage.get()
        || token.registry_generation != registry_snapshot.generation
        || selection.profile_index < 0
        || static_cast<std::size_t>(selection.profile_index) >= selection.song->profiles.size()
        || &selection.song->profiles[static_cast<std::size_t>(selection.profile_index)] != selection.profile
        || registry_snapshot.by_id(selection.song->id) != selection.song) {
        return identity;
    }
    identity.storage_identity = reinterpret_cast<std::uintptr_t>(registry_snapshot.storage.get());
    identity.song_identity = reinterpret_cast<std::uintptr_t>(selection.song);
    identity.profile_identity = reinterpret_cast<std::uintptr_t>(selection.profile);
    identity.registry_generation = registry_snapshot.generation;
    identity.profile_index = selection.profile_index;
    identity.token_registry_generation = token.registry_generation;
    identity.token_route_generation = token.route_generation;
    identity.token_lease_generation = token.lease_generation;
    identity.token_song_key = token.song_key;
    identity.token_controller = reinterpret_cast<std::uintptr_t>(token.controller);
    identity.token_slot = reinterpret_cast<std::uintptr_t>(token.slot);
    identity.token_bgm = reinterpret_cast<std::uintptr_t>(token.bgm);
    identity.token_sound = reinterpret_cast<std::uintptr_t>(token.sound);
    identity.token_request_handle = token.request_handle;
    return identity;
}

bool read_scoreinfo_result_source(
    void* wrapper,
    void* row_key,
    ScoreInfoResultInvocationFacts& facts,
    void*& source_row) noexcept
{
    facts.wrapper_readable = wrapper != nullptr;
    facts.result_wrapper_exact = wrapper != nullptr;
    std::uint64_t discriminator = 0;
    facts.wrapper_discriminator_readable = wrapper
        && core::safe_read_field(wrapper, 0, discriminator) && discriminator != 0;
    facts.source_row_readable = wrapper
        && core::safe_read_field(wrapper, sizeof(std::uint64_t), source_row) && source_row;
    facts.source.row_key = reinterpret_cast<std::uintptr_t>(row_key);
    facts.source.wrapper_discriminator = discriminator;
    if (facts.source_row_readable) {
        FNameValue bgm{};
        if (core::safe_read_field(source_row, kScoreInfoBgmNameOffset, bgm)) {
            facts.source.bgm_comparison_id = bgm.comparison_id;
            facts.source.bgm_number = bgm.number;
        }
        TArrayView<std::int32_t> thresholds{};
        if (core::safe_read_field(source_row, kScoreInfoScoreArrayOffset, thresholds)
            && thresholds.data && thresholds.num >= 4 && thresholds.max >= thresholds.num) {
            bool readable = true;
            for (std::size_t index = 0; index < facts.source_thresholds.size(); ++index) {
                if (!core::safe_read_field(
                        thresholds.data, index * sizeof(std::int32_t), facts.source_thresholds[index])) {
                    readable = false;
                    break;
                }
            }
            if (!readable) facts.source_thresholds = {};
        }
    }
    facts.source_row_revalidated = false;
    return facts.source.valid();
}

bool revalidate_scoreinfo_result_source(
    void* wrapper,
    void* expected_row,
    const ScoreInfoResultSourceIdentity& expected) noexcept
{
    void* current_row = nullptr;
    std::uint64_t current_discriminator = 0;
    FNameValue current_bgm{};
    return wrapper
        && core::safe_read_field(wrapper, 0, current_discriminator)
        && core::safe_read_field(wrapper, sizeof(std::uint64_t), current_row)
        && current_row == expected_row
        && core::safe_read_field(current_row, kScoreInfoBgmNameOffset, current_bgm)
        && current_discriminator == expected.wrapper_discriminator
        && current_bgm.comparison_id == expected.bgm_comparison_id
        && current_bgm.number == expected.bgm_number;
}

void prepare_scoreinfo_result_diagnostic_locked(
    ScoreInfoResultDiagnostic& diagnostic,
    const ScoreInfoResultPolicyState& before,
    const ScoreInfoResultTransition& transition,
    const ScoreInfoResultInvocationFacts& facts,
    const ScoreInfoOverlayRow* row) noexcept
{
    diagnostic.caller = facts.caller;
    diagnostic.caller_rva = facts.caller_rva;
    diagnostic.caller_in_image = facts.caller_in_image;
    diagnostic.before = before.phase;
    diagnostic.after = transition.state.phase;
    diagnostic.failure = transition.failure;
    diagnostic.generation = transition.state.generation;
    diagnostic.result_wrapper_exact = facts.result_wrapper_exact;
    diagnostic.wrapper_readable = facts.wrapper_readable;
    diagnostic.discriminator_readable = facts.wrapper_discriminator_readable;
    diagnostic.source_readable = facts.source_row_readable;
    diagnostic.source_revalidated = facts.source_row_revalidated;
    diagnostic.authority_exact = scoreinfo_result_authority_failure(
        transition.state.authority, facts.authority) == ScoreInfoResultFailure::None;
    diagnostic.source_exact = transition.state.source.valid()
        && scoreinfo_result_source_equal(transition.state.source, facts.source);
    diagnostic.thread_exact = transition.state.consumer_thread == 0
        || transition.state.consumer_thread == facts.consumer_thread;
    diagnostic.published = transition.accepted && transition.publish;
    diagnostic.source_thresholds = facts.source_thresholds;
    if (row) diagnostic.published_thresholds = row->score_thresholds;
    if (g_scoreinfo_result.diagnostic_count < 8) {
        ++g_scoreinfo_result.diagnostic_count;
        diagnostic.emit = true;
    }
}

void prepare_scoreinfo_control_flow_diagnostic(
    ScoreInfoResultDiagnostic& diagnostic,
    const ScoreInfoResultControlFlowFacts& facts,
    const ScoreInfoResultControlFlowReason reason,
    const std::uintptr_t caller_rva) noexcept
{
    std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
    if (!scoreinfo_result_control_flow_diagnostic_allowed(
            g_scoreinfo_result.policy.phase, facts, g_scoreinfo_result.diagnostic_count)) {
        return;
    }
    ++g_scoreinfo_result.diagnostic_count;
    diagnostic.emit = true;
    diagnostic.caller = facts.caller;
    diagnostic.before = g_scoreinfo_result.policy.phase;
    diagnostic.after = g_scoreinfo_result.policy.phase;
    diagnostic.control_flow = reason;
    diagnostic.generation = g_scoreinfo_result.policy.generation;
    diagnostic.caller_rva = caller_rva;
    diagnostic.caller_in_image = facts.caller_in_image;
}

uintptr_t __fastcall scoreinfo_resolver_detour(void* arg0, void* arg1, void* arg2, void* arg3)
{
    auto callback = non_audio_hook_gate().try_enter();
    ScoreInfoResolverRecursionScope recursion(g_scoreinfo_resolver_depth);
    void* caller = _ReturnAddress();
    const uintptr_t call_rva = to_rva(caller);
    const ScoreInfoResultCatalogRole catalog_role =
        scoreinfo_result_catalog_role_from_rva(call_rva);
    const ScoreInfoResultCaller result_caller = scoreinfo_result_caller_for_catalog_role(
        catalog_role, g_scoreinfo_piano_detail_update_depth != 0);
    const uintptr_t result = g_original_scoreinfo_resolver ? g_original_scoreinfo_resolver(arg0, arg1, arg2, arg3) : 0;
    const RenderSnapshot menu_before = registry().render_snapshot();
    if (scoreinfo_menu_detail_overlay_candidate(catalog_role,
            static_cast<bool>(callback), recursion.outermost(), arg0 != nullptr,
            menu_before.song != nullptr && menu_before.profile != nullptr)) {
        try {
            ScoreInfoResultInvocationFacts menu_facts{};
            const bool result_wrapper_exact
                = result != 0 && result == reinterpret_cast<std::uintptr_t>(arg0);
            void* source_row = nullptr;
            (void)read_scoreinfo_result_source(
                arg0, arg1, menu_facts, source_row);
            if (!result_wrapper_exact
                || !menu_facts.wrapper_discriminator_readable
                || !menu_facts.source_row_readable) {
                return result;
            }

            ScoreInfoOverlayRow built = make_scoreinfo_overlay_row(
                *menu_before.song, menu_before.profile,
                source_row, ScoreInfoOverlayRow::kRowSize);
            if (!scoreinfo_overlay_publishable(true, built)) return result;
            auto row = std::make_shared<const ScoreInfoOverlayRow>(std::move(built));

            const RenderSnapshot menu_after = registry().render_snapshot();
            if (!selection_semantically_matches(menu_before, menu_after)
                || !revalidate_scoreinfo_result_source(
                    arg0, source_row, menu_facts.source)
                || !retain_song_render_context_owner(row)) {
                return result;
            }
            void* overlay_ptr = const_cast<std::uint8_t*>(row->data());
            (void)core::safe_write_field(
                arg0, sizeof(std::uint64_t), overlay_ptr);
        } catch (...) {
            // Never unwind C++ exceptions through the native resolver ABI.
        }
        return result;
    }
    const ScoreInfoResultControlFlowFacts control_facts{
        static_cast<bool>(callback), result_caller, call_rva != 0, arg0 != nullptr, recursion.outermost()};
    const ScoreInfoResultControlFlowReason control_reason =
        scoreinfo_result_control_flow_reason(control_facts);
    if (control_reason != ScoreInfoResultControlFlowReason::PolicyDispatched) {
        ScoreInfoResultDiagnostic diagnostic{};
        prepare_scoreinfo_control_flow_diagnostic(
            diagnostic, control_facts, control_reason, call_rva);
        log_scoreinfo_result_diagnostic(diagnostic);
        return result;
    }

    ScoreInfoResultInvocationFacts facts{};
    facts.caller = result_caller;
    facts.caller_rva = call_rva;
    facts.caller_in_image = call_rva != 0;
    facts.original_called = g_original_scoreinfo_resolver != nullptr;
    facts.result_wrapper_exact = result == reinterpret_cast<std::uintptr_t>(arg0);
    facts.consumer_thread = GetCurrentThreadId();
    facts.detail_scope_exact = g_scoreinfo_piano_detail_update_depth == 1;
    void* source_row = nullptr;
    (void)read_scoreinfo_result_source(arg0, arg1, facts, source_row);
    facts.result_wrapper_exact = facts.result_wrapper_exact && result != 0;
    facts.source_row_revalidated = revalidate_scoreinfo_result_source(
        arg0, source_row, facts.source);

    if (result_caller == ScoreInfoResultCaller::StyleSetup) {
        const RegistrySnapshot registry_before = registry().registry_snapshot();
        const PlaybackSnapshot playback_before = registry().playback_snapshot();
        facts.playback_present = scoreinfo_playback_eligible(playback_before);
        facts.authority = scoreinfo_result_authority_identity(
            registry_before, playback_before, playback_before.token);

        if (!facts.result_wrapper_exact
            || scoreinfo_result_common_failure(facts) != ScoreInfoResultFailure::None
            || !facts.playback_present || !facts.authority.valid()) {
            ScoreInfoResultDiagnostic diagnostic{};
            {
                std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
                const ScoreInfoResultPolicyState before = g_scoreinfo_result.policy;
                facts.next_generation = before.generation + 1;
                const auto transition = begin_scoreinfo_result_authority(before, facts);
                g_scoreinfo_result.policy = transition.state;
                prepare_scoreinfo_result_diagnostic_locked(diagnostic, before, transition, facts, nullptr);
            }
            log_scoreinfo_result_diagnostic(diagnostic);
            return result;
        }

        {
            std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
            if (g_scoreinfo_result.policy.phase == ScoreInfoResultPhase::Closed) {
                g_scoreinfo_result.policy = retire_scoreinfo_result_authority(
                    g_scoreinfo_result.policy, ScoreInfoResultFailure::None).state;
                g_scoreinfo_result.storage.reset();
                g_scoreinfo_result.song = nullptr;
                g_scoreinfo_result.profile = nullptr;
                g_scoreinfo_result.token = {};
                g_scoreinfo_result.rows = {};
                g_scoreinfo_result.row_count = 0;
            }
        }

        ScoreInfoOverlayRow built = make_scoreinfo_overlay_row(
            *playback_before.song, playback_before.profile,
            source_row, ScoreInfoOverlayRow::kRowSize);
        if (!scoreinfo_overlay_publishable(true, built)) {
            ScoreInfoResultDiagnostic diagnostic{};
            {
                std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
                const ScoreInfoResultPolicyState before = g_scoreinfo_result.policy;
                auto transition = ScoreInfoResultTransition{};
                transition.state = scoreinfo_result_poisoned(before);
                transition.failure = ScoreInfoResultFailure::RowConstructionFailed;
                g_scoreinfo_result.policy = transition.state;
                prepare_scoreinfo_result_diagnostic_locked(diagnostic, before, transition, facts, &built);
            }
            log_scoreinfo_result_diagnostic(diagnostic);
            return result;
        }
        auto row = std::make_shared<const ScoreInfoOverlayRow>(std::move(built));

        const RegistrySnapshot registry_after = registry().registry_snapshot();
        const PlaybackSnapshot playback_after = registry().playback_snapshot();
        const ScoreInfoResultAuthorityIdentity authority_after = scoreinfo_result_authority_identity(
            registry_after, playback_after, playback_after.token);
        facts.source_row_revalidated = false;
        const bool authority_revalidated = scoreinfo_result_authority_failure(
            facts.authority, authority_after) == ScoreInfoResultFailure::None;

        ScoreInfoResultDiagnostic diagnostic{};
        bool published = false;
        {
            std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
            const ScoreInfoResultPolicyState before = g_scoreinfo_result.policy;
            facts.source_row_revalidated = revalidate_scoreinfo_result_source(
                arg0, source_row, facts.source);
            facts.next_generation = before.generation + 1;
            auto transition = begin_scoreinfo_result_authority(before, facts);
            if (transition.accepted && !authority_revalidated) {
                transition.accepted = false;
                transition.publish = false;
                transition.failure = ScoreInfoResultFailure::RevalidationFailed;
                transition.state = scoreinfo_result_poisoned(before);
            }
            if (transition.accepted && transition.publish) {
                void* overlay_ptr = const_cast<std::uint8_t*>(row->data());
                published = core::safe_write_field(arg0, sizeof(std::uint64_t), overlay_ptr);
                if (!published) {
                    transition.accepted = false;
                    transition.publish = false;
                    transition.failure = ScoreInfoResultFailure::PublicationFailed;
                    transition.state = scoreinfo_result_poisoned(before);
                }
            }
            g_scoreinfo_result.policy = transition.state;
            if (published) {
                g_scoreinfo_result.storage = registry_before.storage;
                g_scoreinfo_result.song = playback_before.song;
                g_scoreinfo_result.profile = playback_before.profile;
                g_scoreinfo_result.token = playback_before.token;
                g_scoreinfo_result.rows = {};
                g_scoreinfo_result.rows[0] = row;
                g_scoreinfo_result.row_count = 1;
                g_scoreinfo_result.diagnostic_count = 0;
            }
            prepare_scoreinfo_result_diagnostic_locked(
                diagnostic, before, transition, facts, row.get());
        }
        if (published) push_title_resolver_token({playback_before, row->data()});
        log_scoreinfo_result_diagnostic(diagnostic);
        return result;
    }

    ScoreInfoResultRuntimeState snapshot{};
    {
        std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
        snapshot = g_scoreinfo_result;
    }
    if (snapshot.policy.phase == ScoreInfoResultPhase::Empty
        || snapshot.policy.phase == ScoreInfoResultPhase::Retired) {
        return result;
    }

    const RegistrySnapshot registry_current = registry().registry_snapshot();
    const PlaybackSnapshot playback_current = registry().playback_snapshot();
    const CleanupLease cleanup_current = registry().cleanup_lease();
    facts.playback_present = scoreinfo_playback_eligible(playback_current);
    if (cleanup_current.song) {
        facts.cleanup = cleanup_current.token == snapshot.token
            ? ScoreInfoResultCleanupAuthority::Exact
            : ScoreInfoResultCleanupAuthority::Unrelated;
    }
    if (facts.playback_present) {
        facts.authority = scoreinfo_result_authority_identity(
            registry_current, playback_current, playback_current.token);
    } else if (cleanup_current.song) {
        facts.authority = scoreinfo_result_authority_identity(
            registry_current, cleanup_current, cleanup_current.token);
    } else if (snapshot.policy.playback_released) {
        facts.authority = snapshot.policy.authority;
    }

    const bool caller_publishes = result_caller == ScoreInfoResultCaller::Detail
        || result_caller == ScoreInfoResultCaller::RankText
        || result_caller == ScoreInfoResultCaller::Thresholds;
    ScoreInfoOverlayRow built{};
    std::shared_ptr<const ScoreInfoOverlayRow> row;
    if (caller_publishes && snapshot.song && snapshot.profile && facts.source_row_readable) {
        built = make_scoreinfo_overlay_row(
            *snapshot.song, snapshot.profile, source_row, ScoreInfoOverlayRow::kRowSize);
        if (scoreinfo_overlay_publishable(true, built)) {
            row = std::make_shared<const ScoreInfoOverlayRow>(std::move(built));
        }
    }
    facts.source_row_revalidated = false;

    ScoreInfoResultDiagnostic diagnostic{};
    bool published = false;
    {
        std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
        const ScoreInfoResultPolicyState before = g_scoreinfo_result.policy;
        facts.source_row_revalidated = revalidate_scoreinfo_result_source(
            arg0, source_row, facts.source);
        if (!scoreinfo_policy_state_equal(before, snapshot.policy)
            || g_scoreinfo_result.storage != snapshot.storage
            || g_scoreinfo_result.song != snapshot.song
            || g_scoreinfo_result.profile != snapshot.profile
            || g_scoreinfo_result.token != snapshot.token) {
            auto transition = ScoreInfoResultTransition{};
            transition.state = scoreinfo_result_poisoned(before);
            transition.failure = ScoreInfoResultFailure::RevalidationFailed;
            g_scoreinfo_result.policy = transition.state;
            prepare_scoreinfo_result_diagnostic_locked(diagnostic, before, transition, facts, row.get());
        } else {
            auto transition = consume_scoreinfo_result_authority(before, facts);
            if (transition.accepted && transition.publish && !row) {
                transition.accepted = false;
                transition.publish = false;
                transition.failure = ScoreInfoResultFailure::RowConstructionFailed;
                transition.state = scoreinfo_result_poisoned(before);
            }
            if (transition.accepted && transition.publish) {
                void* overlay_ptr = const_cast<std::uint8_t*>(row->data());
                published = core::safe_write_field(arg0, sizeof(std::uint64_t), overlay_ptr);
                if (!published) {
                    transition.accepted = false;
                    transition.publish = false;
                    transition.failure = ScoreInfoResultFailure::PublicationFailed;
                    transition.state = scoreinfo_result_poisoned(before);
                }
            }
            g_scoreinfo_result.policy = transition.state;
            if (published && transition.accepted && g_scoreinfo_result.row_count < g_scoreinfo_result.rows.size()) {
                g_scoreinfo_result.rows[g_scoreinfo_result.row_count++] = row;
            }
            prepare_scoreinfo_result_diagnostic_locked(diagnostic, before, transition, facts, row.get());
        }
    }
    log_scoreinfo_result_diagnostic(diagnostic);
    return result;
}

void copy_owned_fields(ScoreInfoOverlayRow& target, const ScoreInfoOverlayRow& source)
{
    target.row = source.row;
    target.mode_change_counts = source.mode_change_counts;
    target.score_thresholds = source.score_thresholds;
    target.title = source.title;
    target.camera_min = source.camera_min;
    target.camera_max = source.camera_max;
    target.camera_rate = source.camera_rate;
    target.bpm = source.bpm;
    target.streaming_frame = source.streaming_frame;
    target.difficulty = source.difficulty;
    target.bgm_name = source.bgm_name;
    target.refresh_references();
}

} // namespace

ScoreInfoPianoDetailUpdateScope::ScoreInfoPianoDetailUpdateScope() noexcept
{
    ++g_scoreinfo_piano_detail_update_depth;
}

ScoreInfoPianoDetailUpdateScope::~ScoreInfoPianoDetailUpdateScope() noexcept
{
    --g_scoreinfo_piano_detail_update_depth;
}

ScoreInfoOverlayRow::ScoreInfoOverlayRow(const ScoreInfoOverlayRow& other)
{
    copy_owned_fields(*this, other);
}

ScoreInfoOverlayRow& ScoreInfoOverlayRow::operator=(const ScoreInfoOverlayRow& other)
{
    if (this != &other) {
        copy_owned_fields(*this, other);
    }
    return *this;
}

ScoreInfoOverlayRow::ScoreInfoOverlayRow(ScoreInfoOverlayRow&& other) noexcept
{
    *this = std::move(other);
}

ScoreInfoOverlayRow& ScoreInfoOverlayRow::operator=(ScoreInfoOverlayRow&& other) noexcept
{
    if (this != &other) {
        row = other.row;
        mode_change_counts = other.mode_change_counts;
        score_thresholds = other.score_thresholds;
        title = std::move(other.title);
        camera_min = other.camera_min;
        camera_max = other.camera_max;
        camera_rate = other.camera_rate;
        bpm = other.bpm;
        streaming_frame = other.streaming_frame;
        difficulty = other.difficulty;
        bgm_name = other.bgm_name;
        refresh_references();
    }
    return *this;
}

void ScoreInfoOverlayRow::refresh_references()
{
    TArrayView<int32_t> mode_changes{mode_change_counts.data(), static_cast<int32_t>(mode_change_counts.size()), static_cast<int32_t>(mode_change_counts.size())};
    TArrayView<int32_t> scores{score_thresholds.data(), static_cast<int32_t>(score_thresholds.size()), static_cast<int32_t>(score_thresholds.size())};
    CompactWideTextRef menu_text{const_cast<wchar_t*>(title.c_str()), static_cast<int32_t>(title.size())};

    write_row_field(row, kScoreInfoModeChangeOffset, mode_changes);
    write_row_field(row, kScoreInfoScoreArrayOffset, scores);
    write_row_field(row, kScoreInfoMenuTextOffset, menu_text);
    write_row_field(row, kScoreInfoBgmNameOffset, bgm_name);
    write_row_field(row, kScoreInfoCameraMinOffset, camera_min);
    write_row_field(row, kScoreInfoCameraMaxOffset, camera_max);
    write_row_field(row, kScoreInfoCameraRateOffset, camera_rate);
    write_row_field(row, kScoreInfoBpmOffset, bpm);
    write_row_field(row, kScoreInfoStreamingFrameOffset, streaming_frame);
    write_row_field(row, kScoreInfoDifficultyOffset, difficulty);
}

ScoreInfoOverlayRow make_scoreinfo_overlay_row(
    const SongDescriptor& song, const SongDifficultyProfile* profile,
    const void* source_row, size_t source_size)
{
    ScoreInfoOverlayRow overlay;
    if (source_row && source_size > 0) {
        core::safe_copy_bytes(source_row, overlay.row.data(), std::min(overlay.row.size(), source_size));
        overlay.camera_min = read_row_field<float>(overlay.row, kScoreInfoCameraMinOffset, overlay.camera_min);
        overlay.camera_max = read_row_field<float>(overlay.row, kScoreInfoCameraMaxOffset, overlay.camera_max);
        overlay.camera_rate = read_row_field<float>(overlay.row, kScoreInfoCameraRateOffset, overlay.camera_rate);
        overlay.bpm = custom_scoreinfo_bpm(
            read_row_field<float>(overlay.row, kScoreInfoBpmOffset, overlay.bpm));
        overlay.streaming_frame = read_row_field<uint32_t>(overlay.row, kScoreInfoStreamingFrameOffset, overlay.streaming_frame);
        overlay.difficulty = read_row_field<int32_t>(overlay.row, kScoreInfoDifficultyOffset, overlay.difficulty);
        overlay.bgm_name = scoreinfo_source_bgm_name(source_row, source_size);
    }
    overlay.mode_change_counts = profile ? profile->mode_change_combo_counts : song.mode_change_combo_counts;
    overlay.score_thresholds = scoreinfo_thresholds_for_descriptor(song, profile);
    overlay.title = profile ? profile->title : song.title;
    overlay.difficulty = native_scoreinfo_difficulty(profile ? profile->difficulty : song.difficulty);
    FNameValue bgm09{};
    if (construct_fname_find(L"bgm_piano_09", bgm09)) {
        overlay.bgm_name = bgm09;
    }
    overlay.refresh_references();
    return overlay;
}

ScoreInfoOverlayRow make_scoreinfo_overlay_row(
    const SongDescriptor& song, const void* source_row, size_t source_size)
{
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const SelectionSnapshot selection = registry().selection_snapshot();
    const SongDifficultyProfile* profile = playback.song == &song ? playback.profile
        : (selection.song == &song ? selection.profile : nullptr);
    return make_scoreinfo_overlay_row(song, profile, source_row, source_size);
}

bool scoreinfo_overlay_row_matches_playback(
    const void* row, const PlaybackSnapshot& playback)
{
    if (!row || !playback.song || !playback.profile || !playback.token.valid()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
    if (g_scoreinfo_result.song != playback.song
        || g_scoreinfo_result.profile != playback.profile
        || g_scoreinfo_result.policy.authority.profile_index != playback.profile_index
        || g_scoreinfo_result.token != playback.token) {
        return false;
    }
    for (std::size_t index = 0; index < g_scoreinfo_result.row_count; ++index) {
        if (g_scoreinfo_result.rows[index]
            && row == g_scoreinfo_result.rows[index]->data()) return true;
    }
    return false;
}

bool refresh_active_scoreinfo_overlay_profile()
{
    const PlaybackSnapshot snapshot = registry().playback_snapshot();
    const SongDescriptor* song = snapshot.song;
    const SongDifficultyProfile* profile = snapshot.profile;
    if (!song || !profile) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
    return g_scoreinfo_result.policy.phase == ScoreInfoResultPhase::StylePublished
        && g_scoreinfo_result.song == song
        && g_scoreinfo_result.profile == profile
        && g_scoreinfo_result.policy.authority.profile_index == snapshot.profile_index
        && g_scoreinfo_result.token == snapshot.token;
}

bool install_scoreinfo_overlay_hooks(const HookInstallContext& context)
{
    g_module_base = reinterpret_cast<uintptr_t>(context.exe_module);
    const core::ImageRange image = core::image_range(context.exe_module);
    g_module_size = reinterpret_cast<std::uintptr_t>(image.base) == g_module_base
        ? image.size : 0;
    const HookSpec* spec = find_hook_spec("scoreinfo_resolver");
    if (!spec) {
        core::log(core::LogLevel::Error, "[scoreinfo_overlay] status=install_failed error=missing_hook_spec");
        return false;
    }

    std::string error;
    const bool ok = g_scoreinfo_hook.install(context.exe_module, spec->rva, spec->expected_prologue,
        reinterpret_cast<void*>(&scoreinfo_resolver_detour), reinterpret_cast<void**>(&g_original_scoreinfo_resolver), error);

    std::ostringstream out;
    out << "[scoreinfo_overlay] status=" << (ok ? "live_hook_installed" : "install_failed")
        << (ok ? "" : " error=") << (ok ? "" : error)
        << " custom_songs=" << registry().custom_count()
        << " row_size=" << ScoreInfoOverlayRow::kRowSize
        << " offsets=0x20,0x30,0x40,0x50,0x58,0x5c,0x60,0x64,0x68,0x6c";
    core::log(ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    return ok;
}

bool restore_scoreinfo_wrappers()
{
    CustomContextToken token{};
    {
        std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
        g_overlay_epoch.invalidate_locked();
        token = g_scoreinfo_result.token;
        g_scoreinfo_result.policy = retire_scoreinfo_result_authority(
            g_scoreinfo_result.policy, ScoreInfoResultFailure::Shutdown).state;
        g_scoreinfo_result.storage.reset();
        g_scoreinfo_result.song = nullptr;
        g_scoreinfo_result.profile = nullptr;
        g_scoreinfo_result.token = {};
        g_scoreinfo_result.rows = {};
        g_scoreinfo_result.row_count = 0;
    }
    if (token.valid()) invalidate_title_resolver_tokens(token);
    return true;
}

bool invalidate_scoreinfo_playback(const CustomContextToken& token)
{
    ScoreInfoResultRuntimeState snapshot{};
    {
        std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
        snapshot = g_scoreinfo_result;
    }
    if (snapshot.policy.phase == ScoreInfoResultPhase::Empty
        || snapshot.policy.phase == ScoreInfoResultPhase::Retired) return true;

    const RegistrySnapshot registry_current = registry().registry_snapshot();
    const PlaybackSnapshot playback_current = registry().playback_snapshot();
    const CleanupLease cleanup_current = registry().cleanup_lease();
    ScoreInfoResultAuthorityIdentity authority{};
    if (cleanup_current.song) {
        authority = scoreinfo_result_authority_identity(
            registry_current, cleanup_current, cleanup_current.token);
    } else if (playback_current.song) {
        authority = scoreinfo_result_authority_identity(
            registry_current, playback_current, playback_current.token);
    } else if (token == snapshot.token) {
        authority = snapshot.policy.authority;
    }

    bool accepted = false;
    ScoreInfoResultDiagnostic diagnostic{};
    {
        std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
        if (!scoreinfo_policy_state_equal(g_scoreinfo_result.policy, snapshot.policy)
            || g_scoreinfo_result.storage != snapshot.storage
            || g_scoreinfo_result.token != snapshot.token) return false;
        g_overlay_epoch.invalidate_locked();
        const ScoreInfoResultPolicyState before = g_scoreinfo_result.policy;
        const auto transition = revoke_scoreinfo_result_playback(
            g_scoreinfo_result.policy, authority);
        g_scoreinfo_result.policy = transition.state;
        accepted = transition.accepted;
        ScoreInfoResultInvocationFacts facts{};
        facts.authority = authority;
        prepare_scoreinfo_result_diagnostic_locked(
            diagnostic, before, transition, facts, nullptr);
    }
    log_scoreinfo_result_diagnostic(diagnostic);
    if (accepted) invalidate_title_resolver_tokens(token);
    return accepted;
}

void retire_scoreinfo_result_authority_for_list()
{
    ScoreInfoResultDiagnostic diagnostic{};
    CustomContextToken token{};
    {
        std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
        g_overlay_epoch.invalidate_locked();
        const ScoreInfoResultPolicyState before = g_scoreinfo_result.policy;
        const auto transition = retire_scoreinfo_result_authority(
            before, ScoreInfoResultFailure::ListRetired);
        token = g_scoreinfo_result.token;
        g_scoreinfo_result.policy = transition.state;
        g_scoreinfo_result.storage.reset();
        g_scoreinfo_result.song = nullptr;
        g_scoreinfo_result.profile = nullptr;
        g_scoreinfo_result.token = {};
        g_scoreinfo_result.rows = {};
        g_scoreinfo_result.row_count = 0;
        prepare_scoreinfo_result_diagnostic_locked(
            diagnostic, before, transition, {}, nullptr);
    }
    if (token.valid()) invalidate_title_resolver_tokens(token);
    log_scoreinfo_result_diagnostic(diagnostic);
}

core::HookShutdownResult shutdown_scoreinfo_overlay()
{
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_scoreinfo_hook),
    }, restore_scoreinfo_wrappers, [] {
        g_original_scoreinfo_resolver = nullptr;
        g_module_base = 0;
        g_module_size = 0;
        std::lock_guard<std::mutex> lock(g_overlay_epoch.mutex());
        g_scoreinfo_result = {};
    });
}

} // namespace ff7r::piano::game
