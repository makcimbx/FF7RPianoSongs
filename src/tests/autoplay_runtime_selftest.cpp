#include "game/autoplay.cpp"
#include <algorithm>
#include <barrier>
#include <stdexcept>
#include <thread>
#include <iostream>

namespace ff7r::piano::game {
core::HookCallbackGate& non_audio_hook_gate() { static core::HookCallbackGate gate; return gate; }
void begin_song_hud_tick() noexcept {}
void begin_song_hud_tick(void*) noexcept {}
void observe_song_hud_controller(void*,void*,const PlaybackSnapshot&,int,int) noexcept {}
SongRegistry& registry() { static SongRegistry value; return value; }
PlaybackSnapshot SongRegistry::playback_snapshot() const { return {}; }
}
using namespace ff7r::piano::game;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class T> void put(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<std::uint8_t*>(p) + offset, &value, sizeof(value));
}
bool terminal_called = false, release_event = false;
void __fastcall terminal(void* event) {
    std::uint8_t marker = 9;
    require(ff7r::piano::core::safe_read_field(event, 0x4f, marker) && marker == 0,
        "introduced marker must be restored before callback");
    terminal_called = true;
    if (release_event) require(VirtualFree(event, 0, MEM_RELEASE) != 0, "event release");
}
void __fastcall match(void* event, std::uint8_t sector) {
    std::uint8_t marker = 0;
    require(sector == 7 && ff7r::piano::core::safe_read_field(event, 0x4f, marker) && marker == 1,
        "matcher forwarding/temporary marker");
    if (release_event) terminalizer_detour(event);
}
void __fastcall advance(void* event) { terminalizer_detour(event); }

void policy_tests() {
    configure_autoplay_input(12); require(autoplay_function_key() == 12, "F12 supported");
    for (const int key : {-1, 0, 13, 24}) {
        configure_autoplay_input(key); require(autoplay_function_key() == 0, "keyboard range is disabled or F1-F12");
    }
    configure_autoplay_input(8);
    AutoplayPolicy p;
    require(!p.toggle(0), "unavailable");
    p.available = true; p.observe(100, 5, -1);
    const auto seat = p.seat;
    require(!p.toggle(seat + 1) && p.toggle(seat) && p.enabled && !p.assisted, "list toggle/token");
    p.observe(100, 5, 6); require(!p.toggle(seat), "pending launch rejected");
    p.observe(100, 8, 9); require(!p.toggle(seat), "countin rejected");
    for (int i = 0; i < 128; ++i) p.observe(100, 8, 9);
    require(p.play == 0 && p.enabled, "delayed readiness must not start a play");
    p.observe(100, 9, -1);
    const auto first_play = p.play;
    require(p.assisted && p.toggle(seat) && !p.enabled && p.assisted, "sticky off");
    // No observation during pause: same token, no timeout revocation.
    require(p.toggle(seat) && p.toggle(seat) && p.assisted, "immediate pause toggles");
    for (int i = 0; i < 512; ++i) require(p.seated && p.play == first_play, "silence must not expire authority");
    require(p.toggle(seat) && p.toggle(seat) && p.assisted, "delayed pause/resume toggles");
    p.observe(100, 9, 4); require(!p.toggle(seat) && p.assisted, "pending return rejects input");
    p.observe(100, 9, -1);
    p.observe(100, 11, 10); require(p.assisted && !p.toggle(seat), "first calculation");
    p.observe(100, 10, -1); require(p.assisted, "second calculation");
    p.observe(100, 5, -1); p.observe(100, 9, -1);
    require(p.play == first_play + 1 && !p.assisted, "reused chart/new play resets");
    require(p.toggle(seat) && p.assisted, "midplay taint");
    p.observe(100, 5, 13); require(!p.seated && !p.enabled && p.assisted, "pending leave revokes");
    p.observe(100, 5, -1); require(p.seat != seat && !p.toggle(seat), "new seat rejects stale token");
    p.observe(100, 0, -1); require(!p.seated, "false continuation state");
    AutoplayInputEdge edge;
    require(!edge.update(true, true), "initial held must release");
    require(!edge.update(false, true) && !edge.update(true, false)
        && !edge.update(true, true), "ineligible held must rearm");
    require(!edge.update(false, true) && edge.update(true, true)
        && !edge.update(true, true), "one accepted edge");
}
void install_rejection_test() {
    std::size_t image_size = 0;
    for (const auto& spec : release_hook_specs())
        image_size = (std::max)(image_size, spec.rva + spec.expected_prologue.size());
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    require(image != nullptr, "signature fixture allocation");
    for (const auto& spec : release_hook_specs()) if (spec.required_for_release_startup)
        std::memcpy(image + spec.rva, spec.expected_prologue.data(), spec.expected_prologue.size());
    // Required product hooks match; optional autoplay matcher intentionally does not.
    require(validate_release_hook_specs(reinterpret_cast<HMODULE>(image)), "optional mismatch blocked unrelated startup");
    std::string error;
    require(ff7r::piano::core::initialize_hooks(error), "initialize real hook infrastructure");
    g_policy = {};
    require(install_autoplay_hooks({reinterpret_cast<HMODULE>(image)}), "complete rollback must be nonfatal");
    g_policy.observe(100, 5, -1);
    bool enabled = false;
    require(!g_policy.available && !autoplay_seated_token() && !autoplay_toggle(g_policy.seat, enabled),
        "signature rejection published seated authority");
    require(!g_matcher.installed() && !g_advancer.installed() && !g_terminalizer.installed() && !g_tick.installed()
        && !g_original_matcher && !g_original_advancer && !g_original_terminalizer && !g_original_tick,
        "signature rejection leaked partial hooks or originals");
    require(ff7r::piano::core::shutdown_hooks(), "rejected hook infrastructure cleanup");
    require(VirtualFree(image, 0, MEM_RELEASE) != 0, "signature fixture cleanup");
}
bool expect_nested_exclusive = true;
std::barrier<>* outer_entered = nullptr;
std::barrier<>* outer_release = nullptr;
bool __fastcall nested_adoption_tick(void*, float) {
    require(g_tick_gate.callbacks_in_flight() == 1, "outer tick must remain lifetime-accounted");
    {
        auto nested = non_audio_hook_gate().try_enter();
        require(static_cast<bool>(nested), "nested list-open callback admission");
        auto exclusive = non_audio_hook_gate().try_suspend_exclusive(nested);
        require(static_cast<bool>(exclusive) == expect_nested_exclusive,
            "nested adoption exclusivity/concurrent exclusion");
        if (exclusive) nested = exclusive.resume_as_lease();
    }
    if (outer_entered) {
        outer_entered->arrive_and_wait();
        outer_release->arrive_and_wait();
    }
    return true;
}
void tick_accounting_tests() {
    non_audio_hook_gate().open(); g_tick_gate.open();
    g_original_tick = nested_adoption_tick;
    require(tick_detour(nullptr, 0.0f), "nested adoption under actual outer tick");
    std::barrier concurrent_ready(2), concurrent_release(2);
    std::thread concurrent([&] {
        auto callback = non_audio_hook_gate().try_enter();
        require(static_cast<bool>(callback), "concurrent callback admission");
        concurrent_ready.arrive_and_wait(); concurrent_release.arrive_and_wait();
    });
    concurrent_ready.arrive_and_wait();
    expect_nested_exclusive = false;
    require(tick_detour(nullptr, 0.0f), "concurrent callback must not block native forwarding");
    concurrent_release.arrive_and_wait(); concurrent.join();
    expect_nested_exclusive = true;
    std::barrier entered(2), release(2);
    outer_entered = &entered; outer_release = &release;
    std::thread tick([] { require(tick_detour(nullptr, 0.0f), "held outer native tick forwarding"); });
    entered.arrive_and_wait();
    require(non_audio_hook_gate().drain(std::chrono::milliseconds(0)), "nested shared lease must be released");
    require(!ff7r::piano::core::disable_then_close_and_drain(g_tick_gate, [] { return g_tick.disable(); }, std::chrono::milliseconds(0))
        && g_tick_gate.callbacks_in_flight() == 1 && g_original_tick == nested_adoption_tick,
        "shutdown must retain the outer tick after nested callbacks/exclusive resume");
    release.arrive_and_wait(); tick.join();
    outer_entered = outer_release = nullptr;
    require(quiesce_autoplay_controller_ticks() && shutdown_autoplay().ok(), "outer tick shutdown retry/drain");
    g_original_tick = nullptr;
    non_audio_hook_gate().open();
}
void shadow_tests() {
    std::array<std::uint8_t, 16> native{}; native[8] = 42;
    require(!AutoplayResultScope::active(), "initial TLS");
    {
        AutoplayResultScope outer(true);
        auto* a = static_cast<std::uint8_t*>(outer.redirect(native.data()));
        require(a != native.data() && a[8] == 42 && outer.redirect(nullptr) == nullptr, "seed/null");
        a[8] = 99;
        {
            AutoplayResultScope inner(true);
            auto* b = static_cast<std::uint8_t*>(inner.redirect(native.data()));
            require(b != a && b[8] == 42, "nested reserved shadow"); b[8] = 77;
        }
        require(AutoplayResultScope::active() == &outer && a[8] == 99 && native[8] == 42,
            "nested return/native unchanged");
    }
    std::barrier rendezvous(2);
    auto run = [&](std::uint8_t value) {
        AutoplayResultScope result(true);
        auto* record = static_cast<std::uint8_t*>(result.redirect(native.data()));
        record[8] = value;
        rendezvous.arrive_and_wait();
        require(record[8] == value && AutoplayResultScope::active() == &result, "concurrent shadow");
    };
    std::thread a(run, std::uint8_t{11}), b(run, std::uint8_t{22}); a.join(); b.join();
    require(!AutoplayResultScope::active() && native[8] == 42, "no escaping TLS/native write");
}
void marker_tests() {
    std::array<std::uint8_t, 0x1100> controller{};
    std::array<std::uint8_t, 14 * 0x20> table{};
    std::array<std::uint8_t, 0xb0> chart{};
    for (int i = 0; i < 14; ++i) put(table.data(), i * 0x20, i);
    g_base = reinterpret_cast<std::uintptr_t>(table.data()) - rva::PianoAutoplayStateTable;
    put(controller.data(), 0xfc0, 14);
    put(controller.data(), 0xfc8, table.data());
    put(controller.data(), 0xfd0, table.data() + 9 * 0x20);
    put(controller.data(), 0xf48, chart.data());
    g_policy = {}; g_policy.available = true;
    g_policy.observe(reinterpret_cast<std::uintptr_t>(controller.data()), 5, -1);
    require(g_policy.toggle(g_policy.seat), "enable fixture");
    g_controller = controller.data();
    auto* event = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    require(event != nullptr, "event allocation");
    put(chart.data(), 0x80, event); put(chart.data(), 0x88, 1);
    put(event, 0, chart.data());
    g_original_matcher = match; g_original_advancer = advance; g_original_terminalizer = terminal;
    {
        AutoplayChartScope scope(chart.data(), controller.data() + 0x380, controller.data() + 0x538, true);
        require(g_chart == chart.data() && g_policy.play == 1, "immediate state9 chart scope");
        matcher_detour(event, 7);
        require(event[0x4f] == 0, "normal-return restoration");
        advancer_detour(event);
        require(terminal_called && event[0x4f] == 0, "advancer restoration before callback");
        event[0x4f] = 1;
        { Journal genuine(event); require(!genuine.introduced, "genuine marker not journaled"); }
        require(event[0x4f] == 1, "genuine marker preserved"); event[0x4f] = 0;
        put(event, 0x10, event); { Journal follower(event); require(!follower.introduced, "followers untouched"); }
        put(event, 0x10, static_cast<void*>(nullptr));
        event[0x4e] = 1; { Journal done(event); require(!done.introduced, "finalized untouched"); } event[0x4e] = 0;
        event[0x4a] = 2; { Journal miss(event); require(!miss.introduced, "miss untouched"); } event[0x4a] = 0;
        // Native callback invalidates storage. Discharged guards must not read it on return.
        release_event = true; matcher_detour(event, 7);
    }
    require(!g_chart && !g_chart_play, "scope restored");
    { AutoplayChartScope rejected(chart.data(), controller.data() + 0x380, controller.data() + 0x538, false);
      require(!g_chart, "custom readiness rejection"); }
    { AutoplayChartScope wrong(chart.data(), nullptr, nullptr, true); require(!g_chart, "exact side ownership"); }
    autoplay_revoke_controller(controller.data()); require(!autoplay_seated_token(), "destructor revocation");
    int current = 0, pending = 0;
    put(controller.data(), 0xfd0, table.data() + 1);
    require(!read_state(controller.data(), current, pending), "misaligned dispatcher rejected");
    g_controller = nullptr;
}
}
int main() {
    try { policy_tests(); install_rejection_test(); tick_accounting_tests(); shadow_tests(); marker_tests(); std::cout << "autoplay runtime selftest passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
