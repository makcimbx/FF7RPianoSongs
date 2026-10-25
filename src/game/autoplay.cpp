#include "game/autoplay.h"
#include "game/song_hud_runtime.h"
#include "game/song_registry.h"
#include "game/module_hooks.h"
#include "game/hook_specs.h"
#include "game/mandatory_hook_transaction.h"
#include "game/rvas.h"
#include "core/logging.h"
#include "core/pe_image.h"
#include <atomic>
#include <array>
#include <mutex>
#include <sstream>
#include <intrin.h>

namespace ff7r::piano::game {
namespace {
using EventFn = void(__fastcall*)(void*);
using MatcherFn = void(__fastcall*)(void*, std::uint8_t);
using TickFn = bool(__fastcall*)(void*, float);
core::RawRvaHook g_matcher, g_advancer, g_terminalizer, g_tick;
// Encloses list-open adoption, whose shared gate lease must remain convertible.
core::HookCallbackGate g_tick_gate;
MatcherFn g_original_matcher = nullptr;
EventFn g_original_advancer = nullptr, g_original_terminalizer = nullptr;
TickFn g_original_tick = nullptr;
std::mutex g_mutex;
AutoplayPolicy g_policy;
std::atomic_int g_function_key{8};
std::uintptr_t g_base = 0;
thread_local void* g_controller = nullptr;
thread_local void* g_chart = nullptr;
thread_local std::uint64_t g_chart_play = 0;
std::atomic_uint64_t g_marked{0}, g_terminalized{0};

void observe_phase(void* controller, int current, int pending) noexcept {
    if (current!=9 || (pending!=-1 && pending!=9)) begin_song_hud_tick();
    AutoplayPolicy snapshot;
    bool changed = false;
    {
        std::lock_guard lock(g_mutex);
        const auto previous = g_policy;
        g_policy.observe(reinterpret_cast<std::uintptr_t>(controller), current, pending);
        snapshot = g_policy;
        changed = previous.state != snapshot.state || previous.seat != snapshot.seat
            || previous.play != snapshot.play || previous.seated != snapshot.seated;
    }
    if (changed) {
        std::ostringstream out;
        out << "[autoplay] stage=phase seat=" << snapshot.seat << " play=" << snapshot.play
            << " state=" << snapshot.state << " seated=" << snapshot.seated << " assisted=" << snapshot.assisted
            << " marked_total=" << g_marked.load(std::memory_order_relaxed)
            << " terminalized_total=" << g_terminalized.load(std::memory_order_relaxed);
        core::log(core::LogLevel::Info, out.str());
    }
}

bool read_state(void* controller, int& current, int& pending) noexcept {
    std::uintptr_t table = 0, entry = 0, next = 0;
    int count = 0;
    if (!controller || !rva::PianoAutoplayStateTable
        || !core::safe_read_field(controller, 0xfc0, count) || count != 14
        || !core::safe_read_field(controller, 0xfc8, table)
        || table != g_base + rva::PianoAutoplayStateTable
        || !core::safe_read_field(controller, 0xfd0, entry)
        || !core::safe_read_field(controller, 0xfd8, next)) return false;
    const auto decode = [table](std::uintptr_t member, int& id) {
        id = -1;
        if (!member) return true;
        return member >= table && member - table < 14 * 0x20
            && (member - table) % 0x20 == 0
            && core::safe_read_field(reinterpret_cast<void*>(member), 0, id)
            && id == static_cast<int>((member - table) / 0x20);
    };
    return decode(entry, current) && decode(next, pending);
}

void disable_authority() noexcept {
    begin_song_hud_tick();
    std::lock_guard lock(g_mutex);
    g_policy.available = false;
    g_policy.revoke();
}

bool __fastcall tick_detour(void* controller, float delta) {
    auto lifetime = g_tick_gate.try_enter();
    bool admitted = false;
    {
        auto callback = non_audio_hook_gate().try_enter();
        admitted = lifetime && callback;
        if (!admitted) begin_song_hud_tick();
        int current = -1, pending = -1;
        if (admitted) {
            if (g_controller || !read_state(controller, current, pending)) disable_authority();
            else {
                begin_song_hud_tick(controller);
                observe_phase(controller, current, pending);
            }
        }
    }
    void* previous = g_controller;
    g_controller = admitted && !previous ? controller : nullptr;
    const bool continuation = g_original_tick ? g_original_tick(controller, delta) : false;
    g_controller = previous;
    // No native pointer dereference after the original's callbacks.
    if (!continuation) autoplay_revoke_controller(controller);
    return continuation;
}

bool marker_write(void* event, std::uint8_t marker) noexcept {
    return core::safe_write_field(event, 0x4f, marker);
}
[[noreturn]] void restoration_failed() noexcept {
    core::log(core::LogLevel::Error, "[autoplay] stage=marker_restore status=fatal callback_not_forwarded=1");
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

struct Journal : AutoplayMarkerJournal {
    Journal* previous = nullptr;
    inline static thread_local Journal* top = nullptr;
    explicit Journal(void* candidate) noexcept {
        previous = top; top = this;
        if (!g_chart || !g_chart_play) return;
        std::lock_guard lock(g_mutex);
        if (!g_policy.available || !g_policy.seated || !g_policy.enabled
            || g_policy.play != g_chart_play || g_policy.state != 9) return;
        std::uintptr_t data = 0, owner = 0, parent = 0;
        int count = 0;
        const auto address = reinterpret_cast<std::uintptr_t>(candidate);
        std::uint8_t result = 0, finalized = 0, marker = 0;
        if (!core::safe_read_field(g_chart, 0x80, data)
            || !core::safe_read_field(g_chart, 0x88, count)
            || count <= 0 || !data || address < data
            || (address - data) % 0x90 != 0
            || (address - data) / 0x90 >= static_cast<std::uintptr_t>(count)
            || !core::safe_read_field(candidate, 0, owner)
            || owner != reinterpret_cast<std::uintptr_t>(g_chart)
            || !core::safe_read_field(candidate, 0x10, parent) || parent
            || !core::safe_read_field(candidate, 0x4a, result) || (result != 0 && result != 3)
            || !core::safe_read_field(candidate, 0x4e, finalized) || finalized
            || !core::safe_read_field(candidate, 0x4f, marker) || marker) return;
        event = candidate;
        introduced = marker_write(event, 1);
        if (!introduced) { g_policy.available = false; g_policy.revoke(); return; }
        // Taint before native judgment, including a same-frame toggle-off.
        g_policy.assisted = true;
        g_marked.fetch_add(1, std::memory_order_relaxed);
    }
    ~Journal() {
        if (!discharge(marker_write)) restoration_failed();
        top = previous;
    }
};
void __fastcall matcher_detour(void* event, std::uint8_t sector) {
    Journal journal(event);
    if (g_original_matcher) g_original_matcher(event, sector);
}
void __fastcall advancer_detour(void* event) {
    Journal journal(event);
    if (g_original_advancer) g_original_advancer(event);
}
void __fastcall terminalizer_detour(void* event) {
    for (Journal* journal = Journal::top; journal; journal = journal->previous) {
        if (journal->introduced && journal->event == event) {
            if (!journal->discharge(marker_write)) restoration_failed();
            g_terminalized.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (g_original_terminalizer) g_original_terminalizer(event);
}
}

void configure_autoplay_input(int key) noexcept { g_function_key.store(key >= 0 && key <= 12 ? key : 0); }
int autoplay_function_key() noexcept { return g_function_key.load(); }
std::uint64_t autoplay_seated_token() noexcept {
    std::lock_guard lock(g_mutex);
    return g_policy.available && g_policy.seated ? g_policy.seat : 0;
}
bool autoplay_toggle(std::uint64_t token, bool& enabled) noexcept {
    std::lock_guard lock(g_mutex);
    if (!g_policy.toggle(token)) return false;
    enabled = g_policy.enabled;
    return true;
}
bool autoplay_assisted() noexcept {
    std::lock_guard lock(g_mutex);
    return g_policy.assisted;
}
void autoplay_observe_native_phase() noexcept {
    if (!g_controller) return;
    int current = -1, pending = -1;
    if (!read_state(g_controller, current, pending)) { disable_authority(); return; }
    observe_phase(g_controller, current, pending);
}
void autoplay_revoke_controller(void* controller) noexcept {
    begin_song_hud_tick();
    std::lock_guard lock(g_mutex);
    if (g_policy.controller == reinterpret_cast<std::uintptr_t>(controller)) g_policy.revoke();
}

AutoplayChartScope::AutoplayChartScope(void* chart, void* left, void* right, bool admitted) noexcept
    : previous_chart_(g_chart), previous_play_(g_chart_play) {
    g_chart = nullptr; g_chart_play = 0;
    if (!admitted || !g_controller) return;
    int current = -1, pending = -1;
    void* owned_chart = nullptr;
    if (!read_state(g_controller, current, pending)
        || !core::safe_read_field(g_controller, 0xf48, owned_chart)) {
        disable_authority(); return;
    }
    const auto owner = reinterpret_cast<std::uintptr_t>(g_controller);
    observe_phase(g_controller, current, pending);
    if (current != 9 || (pending != -1 && pending != 9) || owned_chart != chart
        || left != reinterpret_cast<void*>(owner + 0x380)
        || right != reinterpret_cast<void*>(owner + 0x538)) return;
    observe_song_hud_controller(g_controller,chart,registry().playback_snapshot(),current,pending);
    std::lock_guard lock(g_mutex);
    if (!g_policy.available || !g_policy.seated || g_policy.controller != owner) return;
    g_chart = chart; g_chart_play = g_policy.play;
}
AutoplayChartScope::~AutoplayChartScope() { g_chart = previous_chart_; g_chart_play = previous_play_; }

bool install_autoplay_hooks(const HookInstallContext& context) {
    constexpr std::array names{"piano_autoplay_matcher", "piano_autoplay_advancer",
        "piano_autoplay_terminalizer", "piano_autoplay_controller_tick"};
    std::array<core::RawRvaHook*, 4> hooks{&g_matcher, &g_advancer, &g_terminalizer, &g_tick};
    std::array<void*, 4> detours{reinterpret_cast<void*>(&matcher_detour), reinterpret_cast<void*>(&advancer_detour),
        reinterpret_cast<void*>(&terminalizer_detour), reinterpret_cast<void*>(&tick_detour)};
    std::array<void**, 4> originals{reinterpret_cast<void**>(&g_original_matcher), reinterpret_cast<void**>(&g_original_advancer),
        reinterpret_cast<void**>(&g_original_terminalizer), reinterpret_cast<void**>(&g_original_tick)};
    for (auto name : names) if (!find_hook_spec(name)) {
        core::log(core::LogLevel::Info, "[autoplay] stage=install status=unavailable reason=unproved_build");
        return true;
    }
    g_base = reinterpret_cast<std::uintptr_t>(context.exe_module);
    g_tick_gate.open();
    std::array<MandatoryHookOperation, 4> operations;
    for (std::size_t i = 0; i < operations.size(); ++i) {
        operations[i] = {[&, i] {
            const auto* spec = find_hook_spec(names[i]);
            std::string error;
            const bool installed = hooks[i]->install(context.exe_module, spec->rva, spec->expected_prologue,
                detours[i], originals[i], error);
            if (!installed) core::log(core::LogLevel::Error,
                std::string("[autoplay] stage=hook_install hook=") + names[i] + " status=failed reason=" + error);
            return installed;
        }, [&, i] { return hooks[i]->disable(); }, [&, i] { return hooks[i]->remove(); }};
    }
    const auto result = install_mandatory_hook_transaction(operations);
    {
        std::lock_guard lock(g_mutex);
        g_policy.available = result == MandatoryHookTransactionResult::Installed;
    }
    core::log(core::LogLevel::Info, result == MandatoryHookTransactionResult::Installed
        ? "[autoplay] stage=install status=ready" : result == MandatoryHookTransactionResult::RolledBack
        ? "[autoplay] stage=install status=unavailable rollback=complete"
        : "[autoplay] stage=install status=failed rollback=retained");
    return result != MandatoryHookTransactionResult::Retained;
}
bool quiesce_autoplay_controller_ticks() {
    disable_authority();
    return core::disable_then_close_and_drain(g_tick_gate, [] { return g_tick.disable(); });
}
core::HookShutdownResult shutdown_autoplay() {
    if (!quiesce_autoplay_controller_ticks()) return {};
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_tick), core::teardown_operation(g_advancer),
        core::teardown_operation(g_matcher), core::teardown_operation(g_terminalizer)},
        [] { return true; }, [] {});
}
}
