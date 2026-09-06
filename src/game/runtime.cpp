#include "game/runtime.h"

#include "core/logging.h"
#include "game/hook_specs.h"
#include "game/module_hooks.h"
#include "game/chord_voicing.h"
#include "game/piano_page_selection_guard.h"
#include "game/uobject_identity.h"

#include <vector>

namespace ff7r::piano::game {
namespace {

core::HookCallbackGate g_non_audio_hook_gate;

bool log_shutdown_result(const char* module, const core::HookShutdownResult& result)
{
    std::string message = std::string("[hooks] shutdown module=") + module
        + " status=" + (result.ok() ? "ok" : "failed")
        + " gate_closed=" + std::to_string(result.gate_closed)
        + " hooks_disabled=" + std::to_string(result.hooks_disabled)
        + " callbacks_drained=" + std::to_string(result.callbacks_drained)
        + " native_restored=" + std::to_string(result.native_state_restored)
        + " hooks_removed=" + std::to_string(result.hooks_removed)
        + " state_cleared=" + std::to_string(result.state_cleared);
    core::log(result.ok() ? core::LogLevel::Debug : core::LogLevel::Error, message);
    return result.ok();
}

} // namespace

core::HookCallbackGate& non_audio_hook_gate()
{
    return g_non_audio_hook_gate;
}

bool install_release_hooks(HMODULE exe_module)
{
    g_non_audio_hook_gate.close();
    HookInstallContext context{exe_module};
    if (!validate_release_hook_specs(exe_module)) {
        return false;
    }
    initialize_uobject_identity(exe_module);
    const bool ok = install_menu_session_hooks(context)
        && install_list_patch_hooks(context)
        && install_selection_hooks(context)
        && install_scoreinfo_overlay_hooks(context)
        && install_title_hooks(context)
        && install_chart_patch_hooks(context)
        && install_note_count_hooks(context)
        && install_chord_voicing_hooks(context)
        && install_duration_hooks(context)
        && install_progress_hooks(context)
        && install_audio_sead_hooks(context)
        && install_piano_page_selection_guard(context);
    if (!ok) {
        const bool cleanup_ok = shutdown_release_hooks();
        if (!cleanup_ok) {
            core::log(core::LogLevel::Error, "[hooks] shutdown status=failed cleanup=retained");
        }
    } else {
        g_non_audio_hook_gate.open();
    }
    core::log(core::LogLevel::Info, ok ? "[hooks] status=installed_release_hooks" : "[hooks] status=failed");
    return ok;
}

bool shutdown_release_hooks()
{
    const bool audio_ok = shutdown_audio_sead();
    std::vector<core::HookShutdownResult> results;
    const auto collect = [&](const char* module, const core::HookShutdownResult& result) {
        (void)log_shutdown_result(module, result);
        results.push_back(result);
    };
    const auto progress_result = shutdown_progress();
    collect("progress", progress_result);
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
    const bool identity_consumers_torn_down = audio_ok
        && duration_result.ok()
        && note_count_result.ok()
        && selection_result.ok()
        && list_result.ok() && menu_result.ok();
    if (identity_consumers_torn_down) {
        clear_uobject_identity();
    }
    const bool ok = audio_ok && core::aggregate_shutdown_results(results);
    core::log(ok ? core::LogLevel::Debug : core::LogLevel::Error,
        ok ? "[hooks] shutdown status=ok" : "[hooks] shutdown status=failed cleanup=retained");
    return ok;
}

} // namespace ff7r::piano::game
