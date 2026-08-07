#pragma once

#include "core/hooks.h"
#include "game/song_registry.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ff7r::piano::game {

struct TitleTextView {
    const wchar_t* data = nullptr;
    int32_t num = 0;
    int32_t max = 0;
};

struct OwnedTitleText {
    std::wstring storage;

    TitleTextView view() const;
};

inline const std::wstring& descriptor_title_text(
    const SongDescriptor& song, const SongDifficultyProfile* profile) noexcept
{
    return profile && !profile->title.empty() ? profile->title : song.title;
}

OwnedTitleText format_descriptor_title(
    const SongDescriptor& song, const SongDifficultyProfile* profile);
bool apply_title_text_view(void* out_view, const OwnedTitleText& title);
void reset_native_piano_input_diagnostic_held() noexcept;

struct TitleResolverToken {
    PlaybackSnapshot playback;
    const void* row = nullptr;

    explicit operator bool() const noexcept
    {
        return playback.song && playback.token.valid() && row;
    }
};

class TitleResolverStack {
public:
    void push(const TitleResolverToken& token)
    {
        if (token) tokens_.push_back(token);
    }
    TitleResolverToken peek() const
    {
        return tokens_.empty() ? TitleResolverToken{} : tokens_.back();
    }
    bool consume(const TitleResolverToken& expected)
    {
        if (tokens_.empty()) return false;
        const TitleResolverToken& current = tokens_.back();
        if (current.row != expected.row
            || current.playback.token != expected.playback.token) return false;
        tokens_.pop_back();
        return true;
    }
    void invalidate(const CustomContextToken& token)
    {
        for (auto it = tokens_.begin(); it != tokens_.end();) {
            if (it->playback.token == token) it = tokens_.erase(it);
            else ++it;
        }
    }
    int size() const noexcept { return static_cast<int>(tokens_.size()); }

private:
    std::vector<TitleResolverToken> tokens_;
};

void push_title_resolver_token(const TitleResolverToken& token);
TitleResolverToken peek_title_resolver_token();
bool consume_title_resolver_token(const TitleResolverToken& expected);
void invalidate_title_resolver_tokens(const CustomContextToken& token);
int pending_title_resolver_tokens();

inline bool title_resolver_token_matches(
    const TitleResolverToken& resolver,
    const PlaybackSnapshot& playback,
    const void* row) noexcept
{
    return resolver && playback.song == resolver.playback.song
        && playback.profile_index == resolver.playback.profile_index
        && playback.token == resolver.playback.token
        && row == resolver.row;
}

constexpr bool title_converter_source_allowed(
    const bool source_is_active_overlay,
    const bool source_is_scoped_render_context) noexcept
{
    return source_is_active_overlay || source_is_scoped_render_context;
}

constexpr bool title_converter_activation_return_exact(
    const uintptr_t caller_rva, const uintptr_t expected_return_rva) noexcept
{
    return expected_return_rva != 0 && caller_rva == expected_return_rva;
}

enum class ActivationSelectionTitleFailure : uint8_t {
    None,
    CallerMismatch,
    OverlayPrecedence,
    RenderPrecedence,
    SelectionSongMissing,
    SelectionProfileMissing,
    SelectionStorageMissing,
    SelectionGenerationMissing,
    PlaybackConflict,
    SelectionGenerationDrift,
    SelectionStorageDrift,
    SelectionSongDrift,
    SelectionProfileDrift,
    SelectionProfileIndexDrift,
    SelectionVisibleIndexDrift,
    SelectionBaseSlotDrift,
    TitleMissing,
    WriteFailed,
};

struct ActivationSelectionTitleFacts final {
    bool caller_exact = false;
    bool overlay_absent = false;
    bool render_context_absent = false;
    bool selection_song_present = false;
    bool selection_profile_present = false;
    bool retained_storage_present = false;
    bool selection_generation_present = false;
    bool playback_absent = false;
    bool selection_generation_exact = false;
    bool retained_storage_exact = false;
    bool selection_song_exact = false;
    bool selection_profile_exact = false;
    bool selection_profile_index_exact = false;
    bool selection_visible_index_exact = false;
    bool selection_base_slot_exact = false;
};

constexpr ActivationSelectionTitleFailure
first_activation_selection_title_failure(
    const ActivationSelectionTitleFacts& facts) noexcept
{
    if (!facts.caller_exact) return ActivationSelectionTitleFailure::CallerMismatch;
    if (!facts.overlay_absent) return ActivationSelectionTitleFailure::OverlayPrecedence;
    if (!facts.render_context_absent) return ActivationSelectionTitleFailure::RenderPrecedence;
    if (!facts.selection_song_present) return ActivationSelectionTitleFailure::SelectionSongMissing;
    if (!facts.selection_profile_present) return ActivationSelectionTitleFailure::SelectionProfileMissing;
    if (!facts.retained_storage_present) return ActivationSelectionTitleFailure::SelectionStorageMissing;
    if (!facts.selection_generation_present) return ActivationSelectionTitleFailure::SelectionGenerationMissing;
    if (!facts.playback_absent) return ActivationSelectionTitleFailure::PlaybackConflict;
    if (!facts.selection_generation_exact) return ActivationSelectionTitleFailure::SelectionGenerationDrift;
    if (!facts.retained_storage_exact) return ActivationSelectionTitleFailure::SelectionStorageDrift;
    if (!facts.selection_song_exact) return ActivationSelectionTitleFailure::SelectionSongDrift;
    if (!facts.selection_profile_exact) return ActivationSelectionTitleFailure::SelectionProfileDrift;
    if (!facts.selection_profile_index_exact) return ActivationSelectionTitleFailure::SelectionProfileIndexDrift;
    if (!facts.selection_visible_index_exact) return ActivationSelectionTitleFailure::SelectionVisibleIndexDrift;
    if (!facts.selection_base_slot_exact) return ActivationSelectionTitleFailure::SelectionBaseSlotDrift;
    return ActivationSelectionTitleFailure::None;
}

inline bool activation_selection_playback_canonically_absent(
    const PlaybackSnapshot& playback) noexcept
{
    return !playback.song && !playback.profile && !playback.storage
        && playback.generation == 0 && playback.profile_index == -1
        && playback.token == CustomContextToken{};
}

inline ActivationSelectionTitleFacts activation_selection_title_facts(
    const bool caller_exact,
    const bool source_is_active_overlay,
    const bool source_is_scoped_render_context,
    const SelectionSnapshot& selected,
    const SelectionSnapshot& current,
    const PlaybackSnapshot& playback) noexcept
{
    return {
        caller_exact,
        !source_is_active_overlay,
        !source_is_scoped_render_context,
        selected.song != nullptr,
        selected.profile != nullptr,
        selected.storage != nullptr,
        selected.generation != 0,
        activation_selection_playback_canonically_absent(playback),
        current.generation == selected.generation,
        current.storage == selected.storage,
        current.song == selected.song,
        current.profile == selected.profile,
        current.profile_index == selected.profile_index,
        current.visible_index == selected.visible_index,
        current.base_slot == selected.base_slot,
    };
}

struct TitleConverterOriginalCallState final {
    uint32_t call_count = 0;
};

struct DifficultyDirectionProviderFacts final {
    bool keyboard_held = false;
    bool player_input_held = false;
    bool window_key_held = false;
    bool raw_hid_held = false;
    bool async_gamepad_held = false;
    bool xinput_held = false;

    constexpr bool physically_held() const noexcept
    {
        return keyboard_held || player_input_held || window_key_held
            || raw_hid_held || async_gamepad_held || xinput_held;
    }
};

struct DifficultyInputProviderFacts final {
    DifficultyDirectionProviderFacts decrement;
    DifficultyDirectionProviderFacts increment;
};

struct DifficultyInputEdges final {
    bool decrement = false;
    bool increment = false;
};

inline constexpr uint32_t kNativePianoInputDiagnosticLimit = 64;
inline constexpr uint32_t kDifficultyInputDiagnosticLimit = 64;
inline constexpr std::size_t kNativePianoDirectionCount = 8;

enum class DiagnosticEmissionAdmission : uint8_t {
    Rejected,
    Emit,
    EmitExhaustionMarker,
};

class DiagnosticEmissionBudget final {
public:
    explicit constexpr DiagnosticEmissionBudget(const uint32_t limit) noexcept
        : limit_(limit)
    {
    }

    DiagnosticEmissionAdmission claim() noexcept
    {
        uint32_t emitted = emitted_.load(std::memory_order_relaxed);
        while (emitted < limit_) {
            if (emitted_.compare_exchange_weak(
                    emitted, emitted + 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return DiagnosticEmissionAdmission::Emit;
            }
        }
        return exhaustion_marker_emitted_.exchange(true, std::memory_order_acq_rel)
            ? DiagnosticEmissionAdmission::Rejected
            : DiagnosticEmissionAdmission::EmitExhaustionMarker;
    }

private:
    const uint32_t limit_;
    std::atomic_uint32_t emitted_{0};
    std::atomic_bool exhaustion_marker_emitted_{false};
};

class NativePianoInputDiagnosticAdmission final {
public:
    explicit NativePianoInputDiagnosticAdmission(
        const uint32_t limit = kNativePianoInputDiagnosticLimit) noexcept
        : budget_(limit)
    {
    }

    bool admit_press_edge(const std::size_t direction_index, const int32_t event_type) noexcept
    {
        if (direction_index >= held_.size()) return false;
        if (event_type == 1) {
            rearm_direction(direction_index);
            return false;
        }
        if (event_type != 0) return false;
        return !held_[direction_index].exchange(true, std::memory_order_acq_rel);
    }

    void rearm_direction(const std::size_t direction_index) noexcept
    {
        if (direction_index < held_.size()) {
            held_[direction_index].store(false, std::memory_order_release);
        }
    }

    void reset_held() noexcept
    {
        for (std::atomic_bool& held : held_) {
            held.store(false, std::memory_order_release);
        }
    }

    DiagnosticEmissionAdmission claim_emission() noexcept
    {
        return budget_.claim();
    }

private:
    std::array<std::atomic_bool, kNativePianoDirectionCount> held_{};
    DiagnosticEmissionBudget budget_;
};

template <typename ActiveClock, typename EmitDiagnostic, typename EmitExhaustionMarker>
void run_native_piano_input_diagnostic(
    NativePianoInputDiagnosticAdmission& admission,
    const std::size_t direction_index,
    const int32_t event_type,
    ActiveClock&& active_clock,
    EmitDiagnostic&& emit_diagnostic,
    EmitExhaustionMarker&& emit_exhaustion_marker)
{
    if (!admission.admit_press_edge(direction_index, event_type)) return;
    double elapsed = 0.0;
    if (!std::forward<ActiveClock>(active_clock)(elapsed)) {
        admission.rearm_direction(direction_index);
        return;
    }
    switch (admission.claim_emission()) {
    case DiagnosticEmissionAdmission::Emit:
        std::forward<EmitDiagnostic>(emit_diagnostic)(elapsed);
        break;
    case DiagnosticEmissionAdmission::EmitExhaustionMarker:
        std::forward<EmitExhaustionMarker>(emit_exhaustion_marker)();
        break;
    case DiagnosticEmissionAdmission::Rejected:
        break;
    }
}

constexpr bool difficulty_provider_held_after_key_event(
    const bool currently_held, const int32_t event_type) noexcept
{
    if (event_type == 0) return true;
    if (event_type == 1) return false;
    return currently_held;
}

class DifficultyEventProviderState final {
public:
    void update_player_input(const bool decrement, const int32_t event_type) noexcept
    {
        std::atomic_bool& held = decrement ? player_input_left_ : player_input_right_;
        held.store(difficulty_provider_held_after_key_event(
            held.load(std::memory_order_acquire), event_type), std::memory_order_release);
    }

    void update_window_key(const bool decrement, const bool held) noexcept
    {
        (decrement ? window_key_left_ : window_key_right_)
            .store(held, std::memory_order_release);
    }

    void update_raw_hid(const bool left, const bool right) noexcept
    {
        raw_hid_left_.store(left, std::memory_order_release);
        raw_hid_right_.store(right, std::memory_order_release);
    }

    DifficultyInputProviderFacts snapshot() const noexcept
    {
        DifficultyInputProviderFacts facts;
        facts.decrement.player_input_held =
            player_input_left_.load(std::memory_order_acquire);
        facts.increment.player_input_held =
            player_input_right_.load(std::memory_order_acquire);
        facts.decrement.window_key_held =
            window_key_left_.load(std::memory_order_acquire);
        facts.increment.window_key_held =
            window_key_right_.load(std::memory_order_acquire);
        facts.decrement.raw_hid_held =
            raw_hid_left_.load(std::memory_order_acquire);
        facts.increment.raw_hid_held =
            raw_hid_right_.load(std::memory_order_acquire);
        return facts;
    }

    void reset_for_focus_loss() noexcept { reset(); }
    void reset_for_application_loss() noexcept { reset(); }
    void reset_for_device_removal() noexcept { reset(); }
    void reset_for_teardown() noexcept { reset(); }

private:
    void reset() noexcept
    {
        player_input_left_.store(false, std::memory_order_release);
        player_input_right_.store(false, std::memory_order_release);
        window_key_left_.store(false, std::memory_order_release);
        window_key_right_.store(false, std::memory_order_release);
        raw_hid_left_.store(false, std::memory_order_release);
        raw_hid_right_.store(false, std::memory_order_release);
    }

    std::atomic_bool player_input_left_{false};
    std::atomic_bool player_input_right_{false};
    std::atomic_bool window_key_left_{false};
    std::atomic_bool window_key_right_{false};
    std::atomic_bool raw_hid_left_{false};
    std::atomic_bool raw_hid_right_{false};
};

class DifficultyInputMergeState final {
public:
    constexpr DifficultyInputEdges update(
        const DifficultyInputProviderFacts& facts) noexcept
    {
        const bool decrement_held = facts.decrement.physically_held();
        const bool increment_held = facts.increment.physically_held();
        const DifficultyInputEdges edges{
            decrement_held && !decrement_held_,
            increment_held && !increment_held_,
        };
        decrement_held_ = decrement_held;
        increment_held_ = increment_held;
        return edges;
    }

    constexpr void reset() noexcept
    {
        decrement_held_ = false;
        increment_held_ = false;
    }

private:
    bool decrement_held_ = false;
    bool increment_held_ = false;
};

template <typename OriginalCall>
auto call_title_converter_original_exact_once(
    TitleConverterOriginalCallState& state,
    OriginalCall&& original_call) -> decltype(original_call())
{
    ++state.call_count;
    return std::forward<OriginalCall>(original_call)();
}

template <typename ProtectedWork, typename IdleWait>
void run_difficulty_input_poller_iteration(
    core::HookCallbackGate& gate,
    ProtectedWork&& protected_work,
    IdleWait&& idle_wait)
{
    {
        auto callback = gate.try_enter();
        if (callback) std::forward<ProtectedWork>(protected_work)();
    }
    std::forward<IdleWait>(idle_wait)();
}

static_assert(std::is_trivially_copyable_v<ActivationSelectionTitleFacts>);
static_assert(std::is_trivially_copyable_v<TitleConverterOriginalCallState>);
static_assert(std::is_trivially_copyable_v<DifficultyInputProviderFacts>);
static_assert(std::is_trivially_copyable_v<DifficultyInputEdges>);

} // namespace ff7r::piano::game
