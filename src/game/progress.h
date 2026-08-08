#pragma once

#include "game/song_registry.h"

#include <windows.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ff7r::piano::game {

struct ProgressRecord {
    bool played = false;
    int32_t high_score = 0;
    int32_t last_score = 0;
};

struct RankText {
    const wchar_t* text = L"";
    int32_t num = 1;
    const char* kind = "not_played";
};

// Zero is never a valid caller RVA: it is the PE header, the value the module
// range check yields for a caller outside the executable, and the generated
// catalog's absence sentinel for an address a build does not declare. Rejecting
// it first keeps an unknown caller unmatchable even when the allowlist itself
// carries a sentinel, which happens on any build where a display call site does
// not exist.
constexpr bool progress_lookup_display_caller_allowed(
    const std::uintptr_t caller_rva,
    const std::span<const std::uintptr_t> allowlist) noexcept
{
    if (caller_rva == 0) return false;
    for (const std::uintptr_t allowed : allowlist) {
        if (caller_rva == allowed) return true;
    }
    return false;
}

enum class ScoreCalculationDiagnosticClassification : std::uint8_t {
    Eligible = 0,
    CallerUnavailable,
    CountersUnavailable,
    NativeResultInvalid,
    PlaybackRelationMismatch,
    PersistenceNotCommitted,
};

struct ScoreCalculationDiagnosticFacts {
    std::uintptr_t caller_rva = 0;
    bool counters_copied = false;
    bool native_result_valid = false;
    bool playback_relation_exact = false;
    bool persistence_committed = false;
};

constexpr ScoreCalculationDiagnosticClassification
classify_score_calculation_diagnostic(
    const ScoreCalculationDiagnosticFacts& facts) noexcept
{
    if (facts.caller_rva == 0) {
        return ScoreCalculationDiagnosticClassification::CallerUnavailable;
    }
    if (!facts.counters_copied) {
        return ScoreCalculationDiagnosticClassification::CountersUnavailable;
    }
    if (!facts.native_result_valid) {
        return ScoreCalculationDiagnosticClassification::NativeResultInvalid;
    }
    if (!facts.playback_relation_exact) {
        return ScoreCalculationDiagnosticClassification::PlaybackRelationMismatch;
    }
    if (!facts.persistence_committed) {
        return ScoreCalculationDiagnosticClassification::PersistenceNotCommitted;
    }
    return ScoreCalculationDiagnosticClassification::Eligible;
}

inline bool score_calculation_playback_relation_exact(
    const PlaybackSnapshot& before, const PlaybackSnapshot& after) noexcept
{
    return before.song && before.profile && before.token.valid()
        && before.generation == after.generation
        && before.storage == after.storage
        && before.song == after.song
        && before.profile == after.profile
        && before.profile_index == after.profile_index
        && before.token == after.token;
}

class ScoreCalculationDiagnosticBudget {
public:
    bool consume(const CustomContextToken& token) noexcept
    {
        if (!token.valid()) return false;
        if (token != token_) {
            if (token_.valid() && !newer_than_current(token)) return false;
            token_ = token;
            emitted_ = 0;
        }
        if (emitted_ >= 2) return false;
        ++emitted_;
        return true;
    }

    void reset() noexcept
    {
        token_ = {};
        emitted_ = 0;
    }

private:
    bool newer_than_current(const CustomContextToken& token) const noexcept
    {
        if (token.registry_generation != token_.registry_generation) {
            return token.registry_generation > token_.registry_generation;
        }
        if (token.lease_generation != token_.lease_generation) {
            return token.lease_generation > token_.lease_generation;
        }
        return token.route_generation > token_.route_generation;
    }

    CustomContextToken token_{};
    std::uint8_t emitted_ = 0;
};

inline bool consume_score_calculation_diagnostic_if_eligible(
    const ScoreCalculationDiagnosticFacts& facts,
    const CustomContextToken& token,
    ScoreCalculationDiagnosticBudget& budget) noexcept
{
    return classify_score_calculation_diagnostic(facts)
            == ScoreCalculationDiagnosticClassification::Eligible
        && budget.consume(token);
}

inline bool commit_progress_if_playback_token(
    SongRegistry& song_registry, const CustomContextToken& token,
    const std::function<bool()>& commit)
{
    return song_registry.commit_if_playback_token(token, commit);
}

inline std::wstring progress_section_name(
    const SongDescriptor& song, const SongDifficultyProfile* profile = nullptr)
{
    const int difficulty = profile ? profile->difficulty : song.difficulty;
    std::wstring section(song.id.begin(), song.id.end());
    if (song.profiles.size() > 1) {
        section += L".difficulty." + std::to_wstring(difficulty);
    }
    return section;
}

inline std::wstring progress_song_section_name(const SongDescriptor& song)
{
    return std::wstring(song.id.begin(), song.id.end());
}

inline std::optional<int> parse_last_played_difficulty(
    const std::wstring_view text) noexcept
{
    if (text.empty()) return std::nullopt;
    int value = 0;
    for (const wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return std::nullopt;
        const int digit = ch - L'0';
        if (value > (std::numeric_limits<int>::max() - digit) / 10)
            return std::nullopt;
        value = value * 10 + digit;
    }
    return value;
}

inline int resolve_last_played_profile_index(
    const SongDescriptor& song, const std::optional<int> difficulty) noexcept
{
    if (difficulty) {
        for (std::size_t index = 0; index < song.profiles.size(); ++index) {
            if (song.profiles[index].difficulty == *difficulty)
                return static_cast<int>(index);
        }
    }
    return 0;
}

inline int resolve_score_profile_index(const SongDescriptor& song,
    const SongDifficultyProfile* profile) noexcept
{
    if (!profile && song.profiles.size() == 1) return 0;
    if (!profile) return -1;
    for (std::size_t index = 0; index < song.profiles.size(); ++index) {
        if (&song.profiles[index] == profile) return static_cast<int>(index);
    }
    return -1;
}

template<class ScoreWrite, class PreferenceWrite>
bool persist_score_then_last_played_preference(
    const bool preference_enabled, ScoreWrite&& score_write,
    PreferenceWrite&& preference_write)
{
    if (!score_write()) return false;
    return !preference_enabled || preference_write();
}

std::wstring custom_scores_ini_path(HMODULE module);
ProgressRecord load_progress(const SongDescriptor& song, const std::wstring& ini_path,
    const SongDifficultyProfile* profile = nullptr);
bool save_progress(const SongDescriptor& song, const ProgressRecord& progress, const std::wstring& ini_path,
    const SongDifficultyProfile* profile = nullptr);
int load_last_played_profile_index(
    const SongDescriptor& song, const std::wstring& ini_path);
int load_last_played_profile_index(const SongDescriptor& song);
bool record_score(const SongDescriptor& song, int32_t score, const std::wstring& ini_path,
    ProgressRecord* updated = nullptr, const SongDifficultyProfile* profile = nullptr,
    const CustomContextToken* expected_playback = nullptr,
    bool* persistence_committed = nullptr,
    bool persist_last_played_preference = true);
RankText compute_rank_text(const SongDescriptor& song, const ProgressRecord& progress,
    const SongDifficultyProfile* profile = nullptr);

} // namespace ff7r::piano::game
