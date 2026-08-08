#include "game/progress.h"

#include "core/hooks.h"
#include "game/module_hooks.h"
#include "game/hook_specs.h"
#include "game/rvas.h"

#include "core/logging.h"
#include "core/pe_image.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <mutex>
#include <sstream>
#include <vector>

namespace ff7r::piano::game {
namespace {

std::wstring score_text(int32_t score)
{
    return std::to_wstring(std::max(0, score));
}

using ScoreCalculateFn = int(__fastcall*)(void* context, void* counters);
using ProgressLookupFn = uintptr_t(__fastcall*)(void* arg0, void* arg1, void* arg2, void* arg3);

core::RawRvaHook g_score_calculate_hook;
core::RawRvaHook g_progress_lookup_hook;
ScoreCalculateFn g_original_score_calculate = nullptr;
ProgressLookupFn g_original_progress_lookup = nullptr;
std::wstring g_scores_ini_path;
uintptr_t g_module_base = 0;
std::size_t g_module_size = 0;
std::mutex g_progress_mutex;
std::mutex g_score_diagnostic_mutex;
alignas(16) std::array<uint8_t, 16> g_progress_state_overlay{};
ScoreCalculationDiagnosticBudget g_score_diagnostic_budget;

// A build that does not declare one of these call sites renders it as the
// generated zero sentinel; progress_lookup_display_caller_allowed treats such an
// element as unmatchable rather than as a wildcard.
constexpr std::array<uintptr_t, 3> kProgressLookupCallerRvas{{
    rva::ProgressLookupDisplayCaller0,
    rva::ProgressLookupDisplayCaller1,
    rva::ProgressLookupDisplayCaller2,
}};

constexpr wchar_t kLastPlayedDifficultyKey[] = L"LastPlayedDifficulty";

bool save_last_played_difficulty(const SongDescriptor& song,
    const SongDifficultyProfile& profile, const std::wstring& ini_path)
{
    const std::wstring section = progress_song_section_name(song);
    const std::wstring difficulty = std::to_wstring(profile.difficulty);
    return WritePrivateProfileStringW(section.c_str(), kLastPlayedDifficultyKey,
        difficulty.c_str(), ini_path.c_str()) != FALSE;
}

uintptr_t to_rva(void* caller_address)
{
    if (!g_module_base) {
        return 0;
    }
    const auto caller = reinterpret_cast<uintptr_t>(caller_address);
    return caller >= g_module_base && g_module_size != 0
        && caller - g_module_base < g_module_size
        ? caller - g_module_base : 0;
}

int __fastcall score_calculate_detour(void* context, void* counters)
{
    auto callback = non_audio_hook_gate().try_enter();
    const uintptr_t caller_rva = callback ? to_rva(_ReturnAddress()) : 0;
    const PlaybackSnapshot playback_before = callback
        ? registry().playback_snapshot() : PlaybackSnapshot{};
    std::array<std::uint16_t, 4> native_counters{};
    const bool counters_copied = callback && counters
        && core::safe_copy_bytes(
            counters, native_counters.data(), sizeof(native_counters));
    const int result = g_original_score_calculate ? g_original_score_calculate(context, counters) : 0;
    if (!callback) return result;
    const PlaybackSnapshot playback_after = registry().playback_snapshot();
    const bool playback_relation_exact
        = score_calculation_playback_relation_exact(
            playback_before, playback_after);
    const SongDescriptor* song = playback_after.song;
    if (!song || result < 0 || result > 10000000 || g_scores_ini_path.empty()) {
        return result;
    }

    ProgressRecord updated{};
    bool improved = false;
    bool persistence_committed = false;
    {
        std::lock_guard<std::mutex> lock(g_progress_mutex);
        improved = record_score(
            *song, result, g_scores_ini_path, &updated, playback_after.profile,
            &playback_after.token, &persistence_committed,
            playback_relation_exact);
    }

    const ScoreCalculationDiagnosticFacts diagnostic_facts{
        caller_rva,
        counters_copied,
        true,
        playback_relation_exact,
        persistence_committed,
    };
    bool emit_diagnostic = false;
    {
        std::lock_guard<std::mutex> lock(g_score_diagnostic_mutex);
        emit_diagnostic = consume_score_calculation_diagnostic_if_eligible(
            diagnostic_facts, playback_after.token, g_score_diagnostic_budget);
    }

    if (emit_diagnostic) {
        std::ostringstream out;
        out << "[progress] score_calculation status=observed"
            << " caller_rva=0x" << std::hex << caller_rva << std::dec
            << " native_counters=" << native_counters[0]
            << ',' << native_counters[1]
            << ',' << native_counters[2]
            << ',' << native_counters[3]
            << " native_result=" << result
            << " exact_song_profile_token=1"
            << " persistence_committed=1"
            << " improved=" << (improved ? 1 : 0)
            << " high_score=" << updated.high_score
            << " last_score=" << updated.last_score
            << " profile_index=" << playback_after.profile_index
            << " token_registry_generation=" << playback_after.token.registry_generation
            << " token_route_generation=" << playback_after.token.route_generation
            << " token_lease_generation=" << playback_after.token.lease_generation
            << " token_song_key=" << playback_after.token.song_key
            << " token_request_handle=" << playback_after.token.request_handle
            << " song_id=" << song->id;
        core::log(core::LogLevel::Info, out.str());
    }
    return result;
}

uintptr_t __fastcall progress_lookup_detour(void* arg0, void* arg1, void* arg2, void* arg3)
{
    auto callback = non_audio_hook_gate().try_enter();
    void* caller = _ReturnAddress();
    const uintptr_t original = g_original_progress_lookup ? g_original_progress_lookup(arg0, arg1, arg2, arg3) : 0;
    if (!callback) return original;
    const SelectionSnapshot selection = registry().selection_snapshot();
    const SongDescriptor* song = selection.song;
    const uintptr_t caller_rva = to_rva(caller);
    if (!song || g_scores_ini_path.empty()
        || !progress_lookup_display_caller_allowed(
            caller_rva, kProgressLookupCallerRvas)) {
        return original;
    }

    ProgressRecord progress{};
    uintptr_t overlay = 0;
    {
        std::lock_guard<std::mutex> lock(g_progress_mutex);
        progress = load_progress(*song, g_scores_ini_path, selection.profile);
        g_progress_state_overlay.fill(0);
        if (progress.played) {
            const uint16_t flags = 0xc000u;
            std::memcpy(g_progress_state_overlay.data(), &flags, sizeof(flags));
            std::memcpy(g_progress_state_overlay.data() + 8, &progress.high_score, sizeof(progress.high_score));
        }
        overlay = reinterpret_cast<uintptr_t>(g_progress_state_overlay.data());
    }

    static std::atomic_int s_logs{0};
    const int log_index = s_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 64) {
        std::ostringstream out;
        out << "[progress] display_overlay status=returned"
            << " caller_rva=0x" << std::hex << caller_rva
            << " original=0x" << original
            << " overlay=0x" << overlay
            << std::dec
            << " played=" << (progress.played ? 1 : 0)
            << " high_score=" << progress.high_score
            << " last_score=" << progress.last_score
            << " song_id=" << song->id;
        core::log(core::LogLevel::Debug, out.str());
    }
    return overlay;
}

} // namespace

std::wstring custom_scores_ini_path(HMODULE module)
{
    const std::wstring base = module ? core::parent_dir(core::module_path(module)) : core::log_directory();
    return (base.empty() ? L"." : base) + L"\\FF7RPianoSongs.custom_piano_scores.ini";
}

ProgressRecord load_progress(const SongDescriptor& song, const std::wstring& ini_path,
    const SongDifficultyProfile* profile)
{
    const std::wstring section = progress_section_name(song, profile);
    const int high_score = static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"HighScore", 0, ini_path.c_str()));
    const int last_score = static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"LastScore", high_score, ini_path.c_str()));
    const int played = static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"Played", high_score > 0 ? 1 : 0, ini_path.c_str()));
    return {played != 0, std::max(0, high_score), std::max(0, last_score)};
}

bool save_progress(const SongDescriptor& song, const ProgressRecord& progress, const std::wstring& ini_path,
    const SongDifficultyProfile* profile)
{
    const std::wstring section = progress_section_name(song, profile);
    const std::wstring high_score = score_text(progress.high_score);
    const std::wstring last_score = score_text(progress.last_score);
    const BOOL wrote_played = WritePrivateProfileStringW(section.c_str(), L"Played", progress.played ? L"1" : L"0", ini_path.c_str());
    const BOOL wrote_high = WritePrivateProfileStringW(section.c_str(), L"HighScore", high_score.c_str(), ini_path.c_str());
    const BOOL wrote_last = WritePrivateProfileStringW(section.c_str(), L"LastScore", last_score.c_str(), ini_path.c_str());
    return wrote_played != FALSE && wrote_high != FALSE && wrote_last != FALSE;
}

int load_last_played_profile_index(
    const SongDescriptor& song, const std::wstring& ini_path)
{
    std::array<wchar_t, 32> value{};
    const std::wstring section = progress_song_section_name(song);
    const DWORD length = GetPrivateProfileStringW(section.c_str(),
        kLastPlayedDifficultyKey, L"", value.data(),
        static_cast<DWORD>(value.size()), ini_path.c_str());
    const std::optional<int> difficulty = length > 0 && length < value.size() - 1
        ? parse_last_played_difficulty(std::wstring_view(value.data(), length))
        : std::nullopt;
    return resolve_last_played_profile_index(song, difficulty);
}

int load_last_played_profile_index(const SongDescriptor& song)
{
    std::lock_guard<std::mutex> lock(g_progress_mutex);
    return g_scores_ini_path.empty()
        ? 0 : load_last_played_profile_index(song, g_scores_ini_path);
}

bool record_score(const SongDescriptor& song, int32_t score, const std::wstring& ini_path,
    ProgressRecord* updated, const SongDifficultyProfile* profile,
    const CustomContextToken* expected_playback, bool* persistence_committed,
    const bool persist_last_played_preference)
{
    if (persistence_committed) *persistence_committed = false;
    if (score < 0 || score > 10000000) {
        return false;
    }

    const int profile_index = resolve_score_profile_index(song, profile);
    const SongDifficultyProfile* resolved_profile = profile_index >= 0
        ? &song.profiles[static_cast<std::size_t>(profile_index)] : profile;
    ProgressRecord progress = load_progress(song, ini_path, resolved_profile);
    const bool improved = !progress.played || score > progress.high_score;
    progress.played = true;
    progress.last_score = score;
    if (improved) {
        progress.high_score = score;
    }
    const auto persist = [&] {
        return profile_index >= 0
            && persist_score_then_last_played_preference(
                persist_last_played_preference,
                [&] { return save_progress(
                    song, progress, ini_path, resolved_profile); },
                [&] { return save_last_played_difficulty(
                    song, song.profiles[static_cast<std::size_t>(profile_index)],
                    ini_path); });
    };
    const bool saved = expected_playback
        ? commit_progress_if_playback_token(
            registry(), *expected_playback, persist)
        : persist();
    if (updated) {
        *updated = progress;
    }
    if (persistence_committed) *persistence_committed = saved;
    return saved && improved;
}

RankText compute_rank_text(const SongDescriptor& song, const ProgressRecord& progress,
    const SongDifficultyProfile* profile)
{
    const auto& thresholds = profile ? profile->score_thresholds : song.score_thresholds;
    if (!progress.played) {
        return {L"", 1, "not_played"};
    }
    if (progress.high_score >= thresholds[3]) {
        return {L"\x2605", 2, "star"};
    }
    if (progress.high_score >= thresholds[2]) {
        return {L"A", 2, "A"};
    }
    if (progress.high_score >= thresholds[1]) {
        return {L"B", 2, "B"};
    }
    return {L"C", 2, "C"};
}

bool install_progress_hooks(const HookInstallContext& context)
{
    g_module_base = reinterpret_cast<uintptr_t>(context.exe_module);
    const core::ImageRange image = core::image_range(context.exe_module);
    g_module_size = reinterpret_cast<std::uintptr_t>(image.base) == g_module_base
        ? image.size : 0;
    g_scores_ini_path = custom_scores_ini_path(context.exe_module);

    const HookSpec* score_spec = find_hook_spec("score_calculate");
    const HookSpec* progress_spec = find_hook_spec("progress_lookup");
    if (!score_spec || !progress_spec) {
        core::log(core::LogLevel::Error, "[progress] status=install_failed error=missing_hook_spec");
        return false;
    }

    std::string score_error;
    const bool score_ok = g_score_calculate_hook.install(context.exe_module, score_spec->rva, score_spec->expected_prologue,
        reinterpret_cast<void*>(&score_calculate_detour), reinterpret_cast<void**>(&g_original_score_calculate), score_error);
    std::string progress_error;
    const bool progress_ok = g_progress_lookup_hook.install(context.exe_module, progress_spec->rva, progress_spec->expected_prologue,
        reinterpret_cast<void*>(&progress_lookup_detour), reinterpret_cast<void**>(&g_original_progress_lookup), progress_error);
    const bool ok = score_ok && progress_ok;

    std::ostringstream out;
    out << "[progress] status=" << (ok ? "live_hook_installed" : "install_failed")
        << " ini=FF7RPianoSongs.custom_piano_scores.ini"
        << " score_calculate=" << (score_ok ? "ok" : score_error)
        << " progress_lookup=" << (progress_ok ? "ok" : progress_error)
        << " custom_songs=" << registry().custom_count();
    core::log(ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    return ok;
}

core::HookShutdownResult shutdown_progress()
{
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_progress_lookup_hook),
        core::teardown_operation(g_score_calculate_hook),
    }, {}, [] {
        g_original_progress_lookup = nullptr;
        g_original_score_calculate = nullptr;
        g_scores_ini_path.clear();
        g_module_base = 0;
        g_module_size = 0;
        {
            std::lock_guard<std::mutex> lock(g_score_diagnostic_mutex);
            g_score_diagnostic_budget.reset();
        }
    });
}

} // namespace ff7r::piano::game
