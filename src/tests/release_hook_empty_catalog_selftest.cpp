#include "game/runtime.h"
#include "game/autoplay.h"
#include "game/module_hooks.h"
#include "game/native_audio_runtime.h"
#include "game/mandatory_hook_transaction.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace {
std::array<int, 14> installed{};
std::size_t installed_count = 0;
std::size_t shutdown_count = 0;
std::size_t identity_clear_count = 0;
bool native_shutdown_ready = false;
bool ticks_quiesced = false;
unsigned audio_shutdown_calls = 0;
bool native_install_ready = true;
void (*initializer_notification)() noexcept = nullptr;
bool initializer_notified = false;
bool identity_initialized = false;
unsigned feature_validations = 0;
unsigned native_shutdown_calls = 0;
unsigned palette_install_calls = 0;
bool validation_ready = true;
int failed_module = -99;
bool throw_install = false;
bool rollback_ready = true;
bool live_retention_test = false;
bool early_scope = true;
ff7r::piano::core::RawRvaHook live_hook;
using StockFn = int(*)();
StockFn original_stock = nullptr;
HMODULE stock_module = nullptr;
std::mutex forwarding_mutex;
std::condition_variable forwarding_changed;
bool original_captured = false, resume_forwarding = false, state_cleared = false;
unsigned stock_calls = 0, custom_calls = 0;
int stock_result = 0;
std::thread forwarder;
ff7r::piano::game::MandatoryHookTransactionResult local_rollback;
ff7r::piano::core::HookShutdownResult global_cleanup;

void require(bool condition) { if (!condition) std::abort(); }

int stock_forwarding_detour()
{
    const auto original = original_stock;
    {
        std::unique_lock lock(forwarding_mutex);
        original_captured = true;
        forwarding_changed.notify_all();
        require(forwarding_changed.wait_for(lock, std::chrono::seconds(5), [] { return resume_forwarding; }));
    }
    auto lease = ff7r::piano::game::non_audio_hook_gate().try_enter();
    if (lease) ++custom_calls;
    ++stock_calls;
    return original();
}

void prepare_live_retention_test()
{
    using namespace ff7r::piano;
    // Executable target storage deliberately lives until process exit, like the game image.
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    require(code != nullptr);
    std::memset(code, 0x90, 4096);
    const unsigned char body[]{0xb8, 42, 0, 0, 0, 0xc3}; // mov eax,42; ret
    std::memcpy(code, body, sizeof(body));
    std::memcpy(code + 64, body, sizeof(body));
    require(FlushInstructionCache(GetCurrentProcess(), code, 4096) != 0);
    stock_module = reinterpret_cast<HMODULE>(code);
    std::string error;
    require(core::initialize_hooks(error));
    core::RawRvaHook early;
    StockFn early_original = nullptr;
    game::MandatoryHookOperation operations[]{
        {[&] { return early.install(stock_module, 64, {0xb8,42,0,0,0},
            reinterpret_cast<void*>(&stock_forwarding_detour), reinterpret_cast<void**>(&early_original), error); },
            [&] { return early.disable(); }, [&] { return early.remove(); }},
        {[] { return false; }, [] { return true; }, [] { return true; }}};
    require(game::install_mandatory_hook_transaction(operations) == game::MandatoryHookTransactionResult::RolledBack);
    require(!early.installed() && core::shutdown_hooks()); // Pre-boundary native semantics unchanged.
}

bool install_live_menu_failure()
{
    using namespace ff7r::piano;
    std::string error;
    game::MandatoryHookOperation operations[]{
        {[&] { return live_hook.install(stock_module, 0, {0xb8,42,0,0,0},
            reinterpret_cast<void*>(&stock_forwarding_detour), reinterpret_cast<void**>(&original_stock), error); },
            [] { return live_hook.disable(); }, [] { return live_hook.remove(); }},
        {[] {
            forwarder = std::thread([] { stock_result = reinterpret_cast<StockFn>(stock_module)(); });
            std::unique_lock lock(forwarding_mutex);
            require(forwarding_changed.wait_for(lock, std::chrono::seconds(5), [] { return original_captured; }));
            return false;
        }, [] { return true; }, [] { return true; }}};
    local_rollback = game::install_mandatory_hook_transaction(operations);
    require(local_rollback == game::MandatoryHookTransactionResult::Retained);
    return false;
}

void initializer_complete() noexcept
{
    if (installed_count != 2 || identity_initialized || feature_validations) std::abort();
    initializer_notified = true;
}

bool record(const int module)
{
    installed[installed_count++] = module;
    if (ff7r::piano::game::non_audio_hook_gate().accepting()) std::abort();
    if (module == 10) require(early_scope && installed_count == 2 && !identity_initialized && !feature_validations);
    else if (module != -1 && (!identity_initialized || feature_validations != 1 || early_scope)) std::abort();
    if (module == failed_module && throw_install) throw std::runtime_error("install failure");
    return module != failed_module;
}

ff7r::piano::core::HookShutdownResult shutdown_ok() noexcept
{
    ++shutdown_count;
    return {true, rollback_ready, rollback_ready, rollback_ready, rollback_ready, rollback_ready};
}
}

namespace ff7r::piano::game {
bool validate_release_hook_specs(HMODULE) { ++feature_validations; return identity_initialized && validation_ready; }
void initialize_uobject_identity(HMODULE) { identity_initialized = true; }
void clear_uobject_identity() { ++identity_clear_count; }
bool capture_current_uobject_identity_access(uobject_locator_core::UObjectIdentityAccess&) noexcept { return false; }
native_audio::NativeAccessor* query_original_scoreinfo_accessor(native_audio::NativeAccessor*, std::uint64_t) noexcept { return nullptr; }
namespace native_audio {
namespace bootstrap_detail { bool scope_present() noexcept { return early_scope; } }
namespace native_proxy_detail { bool scope_present() noexcept { return false; } }
bool install_native_audio_runtime(HMODULE, RuntimeServices services) noexcept {
    initializer_notification = services.initializer_complete;
    return services.identity && services.scoreinfo_original && record(-1) && native_install_ready;
}
bool shutdown_native_audio_runtime() noexcept { ++native_shutdown_calls; return native_shutdown_ready; }
}
bool install_menu_session_hooks(const HookInstallContext&) {
    if (!record(0)) return false;
    return live_retention_test ? install_live_menu_failure() : true;
}
bool install_list_patch_hooks(const HookInstallContext&) { return record(1); }
bool install_selection_hooks(const HookInstallContext&) { return record(2); }
bool install_scoreinfo_overlay_hooks(const HookInstallContext&) { return record(3); }
bool install_title_hooks(const HookInstallContext&) { return record(4); }
bool install_chart_patch_hooks(const HookInstallContext&) { return record(5); }
bool install_note_count_hooks(const HookInstallContext&) { return record(6); }
bool install_chord_voicing_hooks(const HookInstallContext&) { return record(11); }
bool install_autoplay_hooks(const HookInstallContext&) { return record(12); }
core::HookShutdownResult shutdown_autoplay() { return shutdown_ok(); }
bool quiesce_autoplay_controller_ticks() { return ticks_quiesced; }
core::HookShutdownResult shutdown_chord_voicing() { return shutdown_ok(); }
bool install_duration_hooks(const HookInstallContext&) { return record(7); }
bool install_progress_hooks(const HookInstallContext&) { return record(8); }
bool install_audio_sead_hooks(const HookInstallContext&) { return record(9); }
bool install_piano_page_selection_guard(const HookInstallContext&) { return record(10); }
core::HookShutdownResult shutdown_menu_session() {
    if (!live_retention_test) return shutdown_ok();
    global_cleanup = core::shutdown_gated_hooks(non_audio_hook_gate(),
        {core::teardown_operation(live_hook)}, [] { return true; }, [] {
            state_cleared = true; original_stock = nullptr;
        });
    return global_cleanup;
}
core::HookShutdownResult shutdown_list_patch() { return shutdown_ok(); }
core::HookShutdownResult shutdown_selection() { return shutdown_ok(); }
core::HookShutdownResult shutdown_scoreinfo_overlay() { return shutdown_ok(); }
core::HookShutdownResult shutdown_title() { return shutdown_ok(); }
core::HookShutdownResult shutdown_chart_patch() { return shutdown_ok(); }
core::HookShutdownResult shutdown_note_count() { return shutdown_ok(); }
core::HookShutdownResult shutdown_duration() { return shutdown_ok(); }
core::HookShutdownResult shutdown_progress() { return shutdown_ok(); }
bool shutdown_audio_sead() { ++audio_shutdown_calls; return true; }
namespace menu_theme_runtime {
void install_palette_hooks() noexcept {
    if (non_audio_hook_gate().accepting()) std::abort();
    ++palette_install_calls;
}
bool uninstall_palette_hooks() noexcept { return !live_retention_test; }
}
}

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "success";
    if (mode == "live_retention") {
        live_retention_test = true; ticks_quiesced = true;
        prepare_live_retention_test();
    }
    if (mode == "native_failure") native_install_ready = false;
    if (mode == "early_guard_failure") failed_module = 10;
    if (mode == "late_scope") early_scope = false;
    if (mode == "validation_failure") { validation_ready = false; ticks_quiesced = true; }
    if (mode == "feature_failure" || mode == "rollback_failure" || mode == "feature_exception") {
        failed_module = 4;
        ticks_quiesced = true;
        rollback_ready = mode != "rollback_failure";
        throw_install = mode == "feature_exception";
    }
    if (ff7r::piano::game::install_feature_hooks(nullptr)) return 1;
    const bool native_ok = ff7r::piano::game::install_native_protection_hooks(nullptr, initializer_complete);
    if (mode == "late_scope") {
        require(!native_ok && !installed_count && !ff7r::piano::game::install_feature_hooks(nullptr));
        return 0;
    }
    if (installed_count != (mode == "native_failure" ? 1u : 2u) || installed[0] != -1 || identity_initialized || feature_validations
        || ff7r::piano::game::non_audio_hook_gate().accepting()) return 1;
    early_scope = false; // Original entry may continue only after the early owner returns.
    if (mode == "early_guard_failure") {
        require(!native_ok && !ff7r::piano::game::install_feature_hooks(nullptr)
            && !feature_validations && !native_shutdown_calls && !initializer_notified);
        return 0;
    }
    if (mode == "native_failure") {
        if (native_ok || ff7r::piano::game::install_feature_hooks(nullptr) || installed_count != 1) return 1;
        return 0;
    }
    if (!native_ok || initializer_notification != initializer_complete || initializer_notified) return 1;
    initializer_notification();
    if (!initializer_notified || installed_count != 2) return 1;
    ff7r::piano::game::initialize_uobject_identity(nullptr);
    const bool features_ok = ff7r::piano::game::install_feature_hooks(nullptr);
    if (live_retention_test) {
        using namespace ff7r::piano;
        require(!features_ok && !game::non_audio_hook_gate().accepting());
        require(!native_shutdown_calls && !identity_clear_count && !palette_install_calls);
        require(installed_count == 3 && !custom_calls && !stock_calls);
        require(local_rollback == game::MandatoryHookTransactionResult::Retained);
        require(global_cleanup.hooks_disabled && global_cleanup.callbacks_drained
            && global_cleanup.process_lifetime_retained && !global_cleanup.ok()
            && !global_cleanup.hooks_removed && !global_cleanup.state_cleared && !state_cleared);
        const auto saved = original_stock;
        require(saved && live_hook.installed() && !live_hook.remove() && live_hook.installed());
        require(!core::shutdown_hooks() && original_stock == saved);
        std::string error;
        require(!live_hook.install(stock_module, 0, {0xb8,42,0,0,0},
            reinterpret_cast<void*>(&stock_forwarding_detour), reinterpret_cast<void**>(&original_stock), error));
        require(original_stock == saved); // Rejected reinstall cannot erase a retained original.
        {
            std::lock_guard lock(forwarding_mutex); resume_forwarding = true;
        }
        forwarding_changed.notify_all(); forwarder.join();
        require(stock_result == 42 && stock_calls == 1 && !custom_calls && !state_cleared);
        require(reinterpret_cast<StockFn>(stock_module)() == 42); // Disabled: stock target, no detour.
        require(stock_calls == 1 && !game::install_feature_hooks(nullptr));
        return 0;
    }
    if (mode != "success") {
        if (features_ok || ff7r::piano::game::non_audio_hook_gate().accepting()
            || native_shutdown_calls || identity_clear_count || !shutdown_count || !audio_shutdown_calls
            || installed_count != (mode == "validation_failure" ? 2u : 7u)
            || feature_validations != 1 || palette_install_calls) return 1;
        const auto before = shutdown_count;
        if (ff7r::piano::game::install_feature_hooks(nullptr) || shutdown_count != before
            || feature_validations != 1 || native_shutdown_calls) return 1;
        return 0;
    }
    const std::array<int, 14> expected{-1, 10, 0, 1, 2, 3, 4, 5, 6, 11, 12, 7, 8, 9};
    if (!features_ok || !ff7r::piano::game::non_audio_hook_gate().accepting()
        || palette_install_calls != 1
        || installed_count != expected.size() || installed != expected) {
        std::cerr << "release_hook_empty_catalog_selftest: empty-catalog install skipped a required module\n";
        return 1;
    }
    if (ff7r::piano::game::shutdown_release_hooks() || audio_shutdown_calls
        || shutdown_count || identity_clear_count) {
        std::cerr << "release_hook_empty_catalog_selftest: enclosing tick lost borrowed services\n";
        return 1;
    }
    ticks_quiesced = true;
    if (ff7r::piano::game::shutdown_release_hooks() || shutdown_count || identity_clear_count) {
        std::cerr << "release_hook_empty_catalog_selftest: pending native work lost borrowed services\n";
        return 1;
    }
    native_shutdown_ready = true;
    if (!ff7r::piano::game::shutdown_release_hooks() || !shutdown_count || identity_clear_count != 1) {
        std::cerr << "release_hook_empty_catalog_selftest: safe shutdown retry failed\n";
        return 1;
    }
    std::cout << "release_hook_empty_catalog_selftest: ok\n";
    return 0;
}
