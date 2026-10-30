#include "core/config.h"
#include "game/menu_theme_runtime.h"
#include "game/autoplay.h"
#include "game/song_preview.h"
#include "core/fail_closed_boundary.h"
#include "core/hooks.h"
#include "core/logging.h"
#include "core/startup_thread.h"
#include "core/version_gate.h"
#include "game/extended_chart.h"
#include "game/catalog_adoption.h"
#include "game/runtime.h"
#include "game/uobject_identity.h"
#include "game/startup_cache_overlay.h"
#include "game/title.h"
#include "startup/music_repository_loader.h"
#include "startup/native_audio_preparation.h"
#include "startup/native_audio_startup_latch.h"
#include "game/native_audio_bootstrap.h"
#include "game/native_audio_decoder.h"
#include "game/native_proxy_bootstrap.h"
#include "proxy/native_proxy.h"
#include "startup/startup_cache_progress.h"
#include "transcription/muscriptor_transcriber.h"
#include "release_identity.generated.h"

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <intrin.h>
#include <sstream>
#include <string>

namespace {

namespace core = ff7r::piano::core;
namespace game = ff7r::piano::game;

HMODULE g_module = nullptr;
std::atomic<bool> g_started{false};
std::atomic<bool> g_proxy_bootstrap{false}; // Diagnostic only; native admission revalidates its private scope.
#ifdef FF7RP_OWNED_PROXY
std::atomic<bool> g_proxy_startup_returned{false};
#endif
std::atomic<bool> g_repository_started{false};
std::atomic<bool> g_completion_started{false};
ff7r::piano::startup::NativeAudioStartupLatch g_native_startup;
ff7r::piano::startup::StartupCacheProgress g_startup_cache_progress;
std::wstring g_dll_dir;
core::Config g_config;

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

DWORD repository_worker_body()
{
    ff7rp::pipeline::OodleDecoder native_decoder;
    ff7rp::pipeline::NativeAudioCacheSession native_cache_session;
    std::filesystem::path native_cache_root;
    const DWORD appdata_units = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (appdata_units && appdata_units <= 32768) {
        std::wstring appdata(appdata_units, L'\0');
        const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", appdata.data(), appdata_units);
        if (copied && copied < appdata_units) {
            appdata.resize(copied);
            native_cache_root = std::filesystem::path(appdata) / L"FF7RPianoSongs" / L"native_audio";
        }
    }
    auto decoder_status = game::prepare_game_oodle_decoder(GetModuleHandleW(nullptr), native_decoder);
    if (decoder_status.ok())
        core::log(core::LogLevel::Info, "[native_cache] status=decoder_ready source=verified_executable");
    else {
        core::log(core::LogLevel::Info, "[native_cache] status=game_decoder_unavailable build=" +
            std::string(core::generated::kBuildId) + " reason=" + decoder_status.message);
        decoder_status = ff7rp::pipeline::OodleDecoder::load(
            std::filesystem::path(g_dll_dir) / L"oo2core_9_win64.dll", &native_decoder);
        if (decoder_status.ok())
            core::log(core::LogLevel::Info, "[native_cache] status=decoder_ready source=authenticated_companion");
    }
    if (!decoder_status.ok())
        core::log(core::LogLevel::Info, "[native_cache] status=decoder_unavailable native_artifacts=unavailable error=" +
            decoder_status.message);
    const auto source_paks = std::filesystem::path(core::module_path(GetModuleHandleW(nullptr)))
        .parent_path().parent_path().parent_path() / L"Content" / L"Paks";
    game::ProgressiveAudioCatalogBuilder audio_builder;
    game::RejectedPendingCatalogRetry rejected;
    ff7r::piano::startup::ProgressiveRepositoryCallbacks callbacks;
    callbacks.on_descriptor_admission = [&](game::SongDescriptor& descriptor) {
        if (native_decoder.available()) {
            const auto status = ff7r::piano::startup::prepare_native_audio_descriptor(
                descriptor, core::generated::kBuildId, source_paks, native_decoder, native_cache_root,
                &native_cache_session);
            if (!status.ok())
                core::log(core::LogLevel::Error, "[native_cache] status=not_prepared song=" + descriptor.id +
                    " native_artifacts=unavailable error=" + status.message);
            else
                core::log(core::LogLevel::Info, "[native_cache] status=prepared id=\"" + descriptor.id + "\"");
        }
        return static_cast<bool>(audio_builder.append(descriptor));
    };
    callbacks.on_catalog = [&](ff7r::piano::startup::MusicRepositoryPlan plan, bool) {
        rejected.begin_new_offer();
        auto prefix = audio_builder.snapshot();
        if (!prefix || !game::audio_sead_detail::enqueue_prepared_audio_prefix(*prefix)) {
            core::log(core::LogLevel::Error, "[native_audio_catalog] status=enqueue_rejected publication=unchanged");
            return false;
        }
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

DWORD WINAPI repository_worker(LPVOID) noexcept
{
    return core::invoke_fail_closed<DWORD>(
        [] { return repository_worker_body(); },
        [](const core::FailClosedException) {
            g_startup_cache_progress.failed();
            core::log(core::LogLevel::Error,
                "[native_cache] status=repository_exception accepted_catalog=retained");
        },
        0);
}

DWORD WINAPI complete_startup(LPVOID) noexcept
{
    return core::invoke_fail_closed<DWORD>(
        [] {
            const auto exe = GetModuleHandleW(nullptr);
            const auto& config = g_config;
            game::initialize_uobject_identity(exe);
            game::configure_autoplay_input(config.autoplay_function_key);
            game::configure_song_preview(config.preview_mode, config.preview_function_key);
            game::menu_theme_runtime::configure(exe, std::filesystem::path(g_dll_dir) / L"Themes",
                g_dll_dir + L"\\FF7RPianoSongs.ini", config);
            game::configure_extended_chart_capability(exe);
            game::configure_catalog_readiness_observer(
                {&g_startup_cache_progress, catalog_readiness_changed});
            const bool transcription = ff7rp::transcription::register_muscriptor_transcriber(
                std::filesystem::path(g_dll_dir) / ff7rp::transcription::kMuscriptorModelFileName,
                config.transcription_threads, [](const std::string& line) { core::log(core::LogLevel::Info, line); });
            core::log(core::LogLevel::Info, std::string("[transcription] model=") + (transcription ? "found" : "missing") +
                " threads=" + std::to_string(config.transcription_threads));
            if (!game::install_feature_hooks(exe)) {
                g_startup_cache_progress.failed();
                core::log(core::LogLevel::Error, "[startup] status=disabled_feature_install_failed native_protection=retained");
                return DWORD{0};
            }
            (void)game::install_startup_cache_overlay(GetModuleHandleW(nullptr),
                g_startup_cache_progress);
            if (!game::start_title_input_service(GetModuleHandleW(nullptr))) {
                g_startup_cache_progress.failed();
                core::log(core::LogLevel::Error, "[startup] status=disabled_title_input_service_failed");
                return DWORD{0};
            }
            if (core::start_thread_once(g_repository_started, repository_worker,
                    nullptr, CreateThread, CloseHandle) != core::StartupThreadResult::Started) {
                g_startup_cache_progress.failed();
                core::log(core::LogLevel::Error,
                    "[startup] status=repository_service_start_failed initial_catalog=empty_recoverable");
                return DWORD{0};
            }
            core::log(core::LogLevel::Info, "[startup] status=initialized mode=native_audio");
            return DWORD{0};
        },
        [](core::FailClosedException) {
            g_startup_cache_progress.failed();
            core::log(core::LogLevel::Error, "[startup] status=disabled_completion_exception");
        },
        DWORD{0});
}

void start_completed_startup() noexcept
{
    if (core::start_thread_once(g_completion_started, complete_startup, nullptr,
            CreateThread, CloseHandle) == core::StartupThreadResult::CreateFailed) {
        g_startup_cache_progress.failed();
        try {
            core::log(core::LogLevel::Error, "[startup] status=completion_service_start_failed");
        } catch (...) {
            // Diagnostics must not terminate the original initializer on resource exhaustion.
        }
    }
}

void native_initialized() noexcept
{
    if (g_native_startup.native_initialized()) start_completed_startup();
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
    g_config = config; // Published to the completion worker by the startup latch/thread creation.
    core::set_log_level(config.log_level);

    core::log(core::LogLevel::Info,
        std::string("[startup] product=") + std::string(generated::kProduct) +
        " version=" + std::string(generated::kVersion) +
        " platform=" + std::string(generated::kPlatform));
    if (g_proxy_bootstrap.load(std::memory_order_acquire)) {
        core::log(core::LogLevel::Info,
            "[native_audio] status=bootstrap_context source=owned_proxy_entry callback_invoked=1 readiness=not_published");
    } else {
        const auto bootstrap = game::native_audio::native_audio_bootstrap_status();
        constexpr const char* failures[] = {"none", "reused", "arguments", "build", "files",
            "image", "export", "frames", "unwind", "code", "baseline", "thread", "scope",
            "publication", "exception"};
        const auto failure = static_cast<unsigned>(bootstrap.failure);
        std::ostringstream out;
        out << "[native_audio] status=bootstrap_context source="
            << (bootstrap.source == game::native_audio::BootstrapSource::Cookie ? "cookie" :
                bootstrap.source == game::native_audio::BootstrapSource::PreWinMain ? "pre_winmain" :
                bootstrap.source == game::native_audio::BootstrapSource::FirstSrw ? "first_srw" : "none")
            << " failure=" << (failure < sizeof(failures) / sizeof(*failures) ? failures[failure] : "unknown")
            << " thread=" << bootstrap.thread << " frames=" << bootstrap.frames
            << " callback_invoked=" << bootstrap.callback_invoked
            << " barriers_published=" << bootstrap.barriers_published
            << " failed_frame=" << bootstrap.failed_frame
            << " observed_return_rva=0x" << std::hex << bootstrap.observed_return_rva
            << " expected_return_rva=0x" << bootstrap.expected_return_rva
            << " image_module="
            << (bootstrap.image.module == game::native_audio::BootstrapImageModule::Executable ? "executable" :
                bootstrap.image.module == game::native_audio::BootstrapImageModule::Loader ? "loader" :
                bootstrap.image.module == game::native_audio::BootstrapImageModule::OwnAsi ? "asi" :
                bootstrap.image.module == game::native_audio::BootstrapImageModule::Crt ? "crt" : "none")
            << " image_stage=" << static_cast<unsigned>(bootstrap.image.stage)
            << " image_field=" << bootstrap.image.field
            << " image_offset=0x" << bootstrap.image.offset << " image_index=" << std::dec << bootstrap.image.index
            << " image_expected=0x" << std::hex << bootstrap.image.expected
            << " image_observed=0x" << bootstrap.image.observed;
        core::log(core::LogLevel::Info, out.str());
        if (std::string_view(bootstrap.frame.predicate) != "none") {
            const auto& frame = bootstrap.frame;
            std::ostringstream detail;
            detail << "[native_audio] status=bootstrap_frame predicate=" << frame.predicate
                << " index=" << frame.index << " export_count=" << frame.export_count
                << " chain_depth=" << frame.chain_depth << " byte_offset=" << frame.byte_offset
                << " expected_module=0x" << std::hex << frame.expected_module
                << " observed_module=0x" << frame.observed_module
                << " supplied_caller_return_rva=0x" << frame.supplied_caller_return_rva
                << " expected_byte=0x" << frame.expected_byte << " observed_byte=0x" << frame.observed_byte
                << " expected_register=0x" << frame.expected_register
                << " observed_register=0x" << frame.observed_register;
            const auto record = [&](const char* name, const game::native_audio::BootstrapFunctionRecord& value) {
                detail << ' ' << name << "_begin=0x" << value.begin
                    << ' ' << name << "_end=0x" << value.end << ' ' << name << "_unwind=0x" << value.unwind;
            };
            record("expected", frame.expected);
            record("observed", frame.observed);
            record("export_entry", frame.export_entry);
            record("export_primary", frame.export_primary);
            record("last_own", frame.last_own);
            record("last_own_primary", frame.last_own_primary);
            detail << " following_count=" << std::dec << frame.following_count;
            for (std::size_t i = 0; i < frame.following_count && i < frame.following.size(); ++i) {
                const auto& following = frame.following[i];
                const auto name = "following" + std::to_string(i);
                detail << ' ' << name << "_module="
                    << (following.module == game::native_audio::BootstrapFrameModule::Executable ? "executable" :
                        following.module == game::native_audio::BootstrapFrameModule::Loader ? "loader" :
                        following.module == game::native_audio::BootstrapFrameModule::OwnAsi ? "asi" : "unknown")
                    << ' ' << name << "_return_rva=0x" << std::hex << following.return_rva;
                record(name.c_str(), following.function);
            }
            core::log(core::LogLevel::Info, detail.str());
        }
    }
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

    if (!game::install_native_protection_hooks(exe, native_initialized)) {
        g_startup_cache_progress.failed();
        core::log(core::LogLevel::Error, "[startup] status=disabled_hook_install_failed");
        return 0;
    }
    core::log(core::LogLevel::Info, "[startup] status=awaiting_native_initializer workers=not_started");
    if (g_native_startup.native_barriers_published()) start_completed_startup();
    return 0;
}

DWORD WINAPI worker_thread(LPVOID) noexcept
{
    return core::invoke_fail_closed<DWORD>(
        [] { return startup_worker_body(); },
        [](const core::FailClosedException exception) {
            g_startup_cache_progress.failed();
            core::log(core::LogLevel::Error,
                exception == core::FailClosedException::Standard
                    ? "[startup] status=disabled_unexpected_exception"
                    : "[startup] status=disabled_unknown_exception");
        },
        0);
}

void initialize_inline() noexcept
{
    (void)core::run_startup_once(g_started, worker_thread, nullptr);
}

#if defined(FF7RP_OWNED_PROXY)
void initialize_proxy_inline() noexcept
{
    g_proxy_bootstrap.store(true, std::memory_order_release);
    initialize_inline();
    // Keep a real return frame: the owned-entry validator authenticates its unwind record.
    g_proxy_startup_returned.store(true, std::memory_order_release);
}

#endif

} // namespace

#if !defined(FF7RP_OWNED_PROXY)
extern "C" __declspec(dllexport) void InitializeASI()
{
    if (g_started.load(std::memory_order_acquire)) return;
    const bool invoked = game::native_audio::try_synchronous_native_audio_startup(
            GetModuleHandleW(nullptr), g_module, reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
            initialize_inline);
    game::native_audio::emit_native_install_diagnostic();
    if (invoked) return;
    core::start_thread_once(g_started, worker_thread, nullptr, CreateThread, CloseHandle);
}

extern "C" __declspec(dllexport) void ReloadedStart()
{
    InitializeASI();
}
#else
bool ff7r::piano::proxy::initialize_owned_product(HMODULE executable, HMODULE self) noexcept
{
    if (g_started.load(std::memory_order_acquire) || self != g_module) return false;
    const auto invoked = game::native_audio::try_owned_proxy_native_audio_startup(
        executable, self, reinterpret_cast<std::uintptr_t>(_ReturnAddress()), initialize_proxy_inline);
    return invoked && g_proxy_startup_returned.load(std::memory_order_acquire);
}
#endif

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, [[maybe_unused]] LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
#if defined(FF7RP_OWNED_PROXY)
        return ff7r::piano::proxy::attach_process_start(module, reason, reserved);
#endif
    }
    return TRUE;
}
