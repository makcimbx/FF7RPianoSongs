#include "core/config.h"
#include "core/fail_closed_boundary.h"
#include "core/hooks.h"
#include "core/logging.h"
#include "core/startup_thread.h"
#include "core/version_gate.h"
#include "game/extended_chart.h"
#include "game/catalog_adoption.h"
#include "game/runtime.h"
#include "game/startup_cache_overlay.h"
#include "startup/music_repository_loader.h"
#include "startup/startup_cache_progress.h"
#include "release_identity.generated.h"

#include <windows.h>

#include <atomic>
#include <sstream>
#include <string>

namespace {

namespace core = ff7r::piano::core;
namespace game = ff7r::piano::game;

HMODULE g_module = nullptr;
std::atomic<bool> g_started{false};
std::atomic<bool> g_repository_started{false};
ff7r::piano::startup::StartupCacheProgress g_startup_cache_progress;
std::wstring g_dll_dir;

void catalog_readiness_changed(void* context, const game::CatalogReadinessEvent event,
    const std::size_t song_count) noexcept
{
    auto& progress = *static_cast<ff7r::piano::startup::StartupCacheProgress*>(context);
    switch (event) {
    case game::CatalogReadinessEvent::Prepared:
        progress.catalog_prepared(song_count);
        break;
    case game::CatalogReadinessEvent::AdoptionDeferred:
        progress.catalog_adoption_deferred();
        break;
    case game::CatalogReadinessEvent::Adopted:
        progress.catalog_adopted(song_count);
        break;
    }
}

DWORD WINAPI repository_worker(LPVOID) noexcept
{
    game::ProgressiveAudioCatalogBuilder audio_builder;
    game::RejectedPendingCatalogRetry rejected;
    ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
    callbacks.on_descriptor_admission = [&audio_builder](const game::SongDescriptor& descriptor) {
        return static_cast<bool>(audio_builder.append(descriptor));
    };
    callbacks.on_catalog = [&](ff7r::piano::startup::MusicRepositoryPlan plan, bool) {
        rejected.begin_new_offer();
        auto prefix = audio_builder.snapshot();
        auto candidate = game::prepare_pending_catalog(
            std::move(plan.descriptors), prefix);
        if (!candidate) return false;
        const bool accepted = game::offer_prepared_pending_catalog(candidate);
        if (accepted) {
            audio_builder.log_newly_accepted(prefix);
        } else {
            rejected.retain_rejected(std::move(candidate), std::move(prefix));
        }
        return accepted;
    };
    callbacks.on_final_catalog_retry = [&] {
        std::shared_ptr<const game::PreparedAudioPrefix> accepted_prefix;
        const bool accepted = rejected.retry(accepted_prefix);
        if (accepted) audio_builder.log_newly_accepted(accepted_prefix);
        return accepted;
    };
    (void)ff7r::piano::startup::run_progressive_music_repository(
        g_dll_dir, callbacks, &g_startup_cache_progress);
    return 0;
}

DWORD startup_worker_body()
{
    using namespace ff7r::piano;

    const std::wstring dll_path = core::module_path(g_module);
    const std::wstring dll_dir = core::parent_dir(dll_path);
    g_dll_dir = dll_dir;
    core::set_log_path(dll_dir + L"\\FF7RPianoSongs.log");

    const std::wstring ini_path = dll_dir + L"\\FF7RPianoSongs.ini";
    const core::Config config = core::load_config(ini_path);
    core::set_log_level(config.log_level);

    core::log(core::LogLevel::Info,
        std::string("[startup] product=") + std::string(generated::kProduct) +
        " version=" + std::string(generated::kVersion) +
        " platform=" + std::string(generated::kPlatform));
    if (!config.enabled) {
        core::log(core::LogLevel::Info, "[startup] status=disabled_by_config");
        return 0;
    }

    HMODULE exe = GetModuleHandleW(nullptr);
    core::PeIdentity actual_identity{};
    if (!core::is_supported_exe(exe, &actual_identity)) {
        std::ostringstream out;
        out << "[startup] status=disabled_game_updated"
            << " expected_timestamp=0x" << std::hex << core::kExpectedExeTimestamp
            << " actual_timestamp=0x" << actual_identity.timestamp
            << " expected_image_size=0x" << core::kExpectedExeSizeOfImage
            << " actual_image_size=0x" << actual_identity.size_of_image
            << " expected_checksum=0x" << core::kExpectedExeChecksum
            << " actual_checksum=0x" << actual_identity.checksum;
        core::log(core::LogLevel::Error, out.str());
        return 0;
    }

    game::configure_extended_chart_experiment(exe, config.experimental_extended_charts);
    game::configure_catalog_readiness_observer(
        {&g_startup_cache_progress, catalog_readiness_changed});

    std::string error;
    bool minhook_ready = core::initialize_hooks(error);
    if (!minhook_ready) {
        core::log(core::LogLevel::Info,
            "[startup_overlay] status=disabled_minhook_unavailable error=" + error);
    } else {
        (void)game::install_startup_cache_overlay(exe, g_startup_cache_progress);
    }

    if (!minhook_ready) {
        error.clear();
        minhook_ready = core::initialize_hooks(error);
        if (!minhook_ready) {
            core::log(core::LogLevel::Error,
                "[startup] status=disabled_minhook_error error=" + error);
            return 0;
        }
    }

    if (!game::install_release_hooks(exe)) {
        core::log(core::LogLevel::Error, "[startup] status=disabled_hook_install_failed");
        return 0;
    }
    if (core::start_thread_once(g_repository_started, repository_worker,
            nullptr, CreateThread, CloseHandle) != core::StartupThreadResult::Started) {
        core::log(core::LogLevel::Error,
            "[startup] status=repository_service_start_failed initial_catalog=empty_recoverable");
    }

    core::log(core::LogLevel::Info, "[startup] status=initialized mode=core_skeleton");
    return 0;
}

DWORD WINAPI worker_thread(LPVOID) noexcept
{
    return core::invoke_fail_closed<DWORD>(
        [] { return startup_worker_body(); },
        [](const core::FailClosedException exception) {
            core::log(core::LogLevel::Error,
                exception == core::FailClosedException::Standard
                    ? "[startup] status=disabled_unexpected_exception"
                    : "[startup] status=disabled_unknown_exception");
        },
        0);
}

} // namespace

extern "C" __declspec(dllexport) void InitializeASI()
{
    core::start_thread_once(g_started, worker_thread, nullptr, CreateThread, CloseHandle);
}

extern "C" __declspec(dllexport) void ReloadedStart()
{
    InitializeASI();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        InitializeASI();
    }
    return TRUE;
}
