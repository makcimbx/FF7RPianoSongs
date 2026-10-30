#include "game/runtime.h"
#include "game/menu_theme_runtime.h"
#include "game/autoplay.h"

#include "core/logging.h"
#include "game/hook_specs.h"
#include "game/module_hooks.h"
#include "game/chord_voicing.h"
#include "game/piano_page_selection_guard.h"
#include "game/uobject_identity.h"
#include "game/native_audio_runtime.h"
#include "game/native_audio_bootstrap.h"
#include "game/native_proxy_bootstrap.h"
#include "game/scoreinfo_overlay.h"

#include <vector>

namespace ff7r::piano::game {
namespace {

core::HookCallbackGate g_non_audio_hook_gate;
bool g_native_protection_installed = false;
bool g_feature_startup_attempted = false;
bool shutdown_feature_hooks(bool release_identity);

bool log_shutdown_result(const char* module, const core::HookShutdownResult& result)
{
    std::string message = std::string("[hooks] shutdown module=") + module
        + " status=" + (result.ok() ? "ok" : "failed")
        + " gate_closed=" + std::to_string(result.gate_closed)
        + " hooks_disabled=" + std::to_string(result.hooks_disabled)
        + " callbacks_drained=" + std::to_string(result.callbacks_drained)
        + " native_restored=" + std::to_string(result.native_state_restored)
        + " hooks_removed=" + std::to_string(result.hooks_removed)
        + " state_cleared=" + std::to_string(result.state_cleared)
        + " process_lifetime_retained=" + std::to_string(result.process_lifetime_retained);
    core::log(result.ok() ? core::LogLevel::Debug : core::LogLevel::Error, message);
    return result.ok();
}

} // namespace

core::HookCallbackGate& non_audio_hook_gate()
{
    return g_non_audio_hook_gate;
}

bool install_native_protection_hooks(HMODULE exe_module, void (*native_initialized)() noexcept)
{
    g_non_audio_hook_gate.close();
    if (native_audio::bootstrap_detail::scope_present()
        == native_audio::native_proxy_detail::scope_present()) return false;
    std::string error;
    if (!core::initialize_hooks(error)) {
        core::log(core::LogLevel::Error, "[startup] status=disabled_minhook_error error=" + error);
        return false;
    }
    // Stable services are borrowed only by queued/ready work, never empty capture.
    const bool native_audio_ok = native_audio::install_native_audio_runtime(exe_module,
        {&capture_current_uobject_identity_access, &query_original_scoreinfo_accessor,
            native_initialized});
    if (!native_audio_ok) {
        core::log(core::LogLevel::Error,
            "[native_audio] status=unavailable custom_admission=denied stock_playback=unchanged");
    } else {
        core::log(core::LogLevel::Info,
            "[native_audio] status=barriers_installed readiness=not_published");
    }
    // This direct CALL patch requires the same authenticated entry-held exclusion
    // as native publication; never install or reclaim its relay after continuation.
    const bool early_ok = native_audio_ok
        && install_piano_page_selection_guard({exe_module});
    g_native_protection_installed = early_ok;
    if (!early_ok && native_audio_ok) core::log(core::LogLevel::Error,
        "[hooks] stage=early_guard status=failed native_protection=retained workers=not_started");
    return early_ok;
}

bool install_feature_hooks(HMODULE exe_module)
{
    if (!g_native_protection_installed || g_feature_startup_attempted) return false;
    g_feature_startup_attempted = true; // Failure is terminal, including uncertain rollback.
    g_non_audio_hook_gate.close();
    core::retain_hook_trampolines_for_process_lifetime();
    core::log(core::LogLevel::Info,
        "[hooks] stage=live_feature_install trampoline_lifetime=process removal=forbidden admission=closed");
    HookInstallContext context{exe_module};
    bool ok = false;
    try {
        ok = validate_release_hook_specs(exe_module)
            && install_menu_session_hooks(context)
            && install_list_patch_hooks(context)
            && install_selection_hooks(context)
            && install_scoreinfo_overlay_hooks(context)
            && install_title_hooks(context)
            && install_chart_patch_hooks(context)
            && install_note_count_hooks(context)
            && install_chord_voicing_hooks(context)
            && install_autoplay_hooks(context)
            && install_duration_hooks(context)
            && install_progress_hooks(context)
            && install_audio_sead_hooks(context);
        if (ok) {
            menu_theme_runtime::install_palette_hooks(); // Optional, internally transactional.
            g_non_audio_hook_gate.open();
        }
    } catch (...) {
        core::log(core::LogLevel::Error, "[hooks] status=feature_startup_exception");
        ok = false;
    }
    if (!ok) {
        // No repository/cohort was admitted. Keep native lifecycle protection and GU services.
        const bool cleanup_ok = quiesce_autoplay_controller_ticks()
            && shutdown_audio_sead() && shutdown_feature_hooks(false);
        if (!cleanup_ok) {
            core::log(core::LogLevel::Error, "[hooks] feature_rollback status=failed cleanup=retained native_protection=retained");
        }
    }
    core::log(core::LogLevel::Info, ok ? "[hooks] status=installed_release_hooks" : "[hooks] status=failed");
    return ok;
}

bool shutdown_release_hooks()
{
    if (!quiesce_autoplay_controller_ticks()) {
        core::log(core::LogLevel::Error, "[hooks] shutdown status=deferred controller_tick=retained");
        return false;
    }
    const bool audio_ok = shutdown_audio_sead();
    const bool native_audio_ok = native_audio::shutdown_native_audio_runtime();
    if (!audio_ok || !native_audio_ok) {
        // Native callbacks still borrow the original ScoreInfo/GU services.
        core::log(core::LogLevel::Error, "[hooks] shutdown status=deferred services=retained");
        return false;
    }
    return shutdown_feature_hooks(true);
}

namespace {
bool shutdown_feature_hooks(const bool release_identity)
{
    g_non_audio_hook_gate.close();
    if (!g_non_audio_hook_gate.drain(std::chrono::milliseconds(2000))) {
        core::log(core::LogLevel::Error, "[hooks] shutdown status=deferred palette_or_callbacks=retained");
        return false;
    }
    const bool palette_ok = menu_theme_runtime::uninstall_palette_hooks();
    // A retained optional palette must not skip disabling the other live detours.
    if (!palette_ok && !core::hook_trampolines_retained_for_process_lifetime()) return false;
    std::vector<core::HookShutdownResult> results;
    const auto collect = [&](const char* module, const core::HookShutdownResult& result) {
        (void)log_shutdown_result(module, result);
        results.push_back(result);
    };
    const auto progress_result = shutdown_progress();
    collect("progress", progress_result);
    collect("autoplay", shutdown_autoplay());
    const auto duration_result = shutdown_duration();
    collect("duration", duration_result);
    const auto note_count_result = shutdown_note_count();
    collect("note_count", note_count_result);
    collect("chord_voicing", shutdown_chord_voicing());
    const auto chart_result = shutdown_chart_patch();
    collect("chart_patch", chart_result);
    const auto title_result = shutdown_title();
    collect("title", title_result);
    const auto scoreinfo_result = shutdown_scoreinfo_overlay();
    collect("scoreinfo", scoreinfo_result);
    const auto selection_result = shutdown_selection();
    collect("selection", selection_result);
    const auto list_result = shutdown_list_patch();
    collect("list", list_result);
    const auto menu_result = shutdown_menu_session();
    collect("menu_session", menu_result);
    if (release_identity && duration_result.ok() && note_count_result.ok()
        && selection_result.ok() && list_result.ok() && menu_result.ok()) {
        clear_uobject_identity();
    }
    const bool ok = palette_ok && core::aggregate_shutdown_results(results);
    core::log(ok ? core::LogLevel::Debug : core::LogLevel::Error,
        ok ? "[hooks] shutdown status=ok" : "[hooks] shutdown status=failed cleanup=retained");
    return ok;
}
} // namespace

} // namespace ff7r::piano::game
