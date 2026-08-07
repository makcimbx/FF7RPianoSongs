#include "runtime_lifecycle_test_support.h"

#include "core/hooks.h"
#include "game/audio_cleanup_policy.h"
#include "game/completion_capture.h"
#include "game/duration.h"
#include "game/frozen_profile_lifecycle.h"
#include "game/native_array_publication.h"
#include "game/onmemory_bank_diagnostic.h"
#include "game/onmemory_bank_lifecycle.h"
#include "game/progress.h"
#include "game/runtime_context_policy.h"
#include "game/scoreinfo_overlay.h"
#include "game/title.h"
#include "game/uobject_locator_core.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ff7r::piano::tests::runtime_lifecycle {

using ff7r::piano::core::HookCallbackGate;
using ff7r::piano::core::HookShutdownResult;
using ff7r::piano::core::HookTeardownOperation;
using ff7r::piano::core::NativeRestoreOperation;

HookShutdownResult successful_result()
{
    HookShutdownResult result;
    result.gate_closed = true;
    result.hooks_disabled = true;
    result.callbacks_drained = true;
    result.native_state_restored = true;
    result.hooks_removed = true;
    result.state_cleared = true;
    return result;
}

bool tuple_state_safe(const ff7r::piano::game::NativeArrayTuple& value)
{
    const int32_t backing_capacity = value.pointer == 0x1000 ? 5
        : (value.pointer == 0x2000 ? 8 : (value.pointer == 0x3000 ? 12 : -1));
    return value.count >= 0 && value.capacity >= value.count
        && value.capacity <= backing_capacity;
}

struct TupleMemory {
    ff7r::piano::game::NativeArrayTuple value;
    std::vector<ff7r::piano::game::NativeArrayTuple> observed;

    ff7r::piano::game::NativeArrayTupleAccess access()
    {
        return {
            [this](auto& out) { out = value; return true; },
            [this](uintptr_t field) { value.pointer = field; return true; },
            [this](int32_t field) { value.count = field; return true; },
            [this](int32_t field) { value.capacity = field; return true; },
            [this](const auto& state) { observed.push_back(state); },
        };
    }
};

void require_safe_states(const TupleMemory& memory, const char* message)
{
    for (const auto& state : memory.observed) require(tuple_state_safe(state), message);
    require(tuple_state_safe(memory.value), message);
}

void test_shutdown_order_and_failures()
{
    std::vector<std::string> events;
    HookCallbackGate gate;
    gate.open();
    const auto result = ff7r::piano::core::shutdown_gated_hooks(gate, {{
        [&] { events.push_back("disable"); return true; },
        [&] { events.push_back("remove"); return true; },
    }}, [&] { events.push_back("restore"); return true; }, [&] { events.push_back("clear"); });
    require(result.ok(), "successful shutdown rejected");
    require(events == std::vector<std::string>({"disable", "restore", "remove", "clear"}),
        "shutdown phases ran out of order");

    gate.open();
    events.clear();
    const auto disable_failure = ff7r::piano::core::shutdown_gated_hooks(gate, {{
        [&] { events.push_back("disable"); return false; },
        [&] { events.push_back("remove"); return true; },
    }}, [&] { events.push_back("restore"); return true; }, [&] { events.push_back("clear"); });
    require(!disable_failure.ok() && events == std::vector<std::string>({"disable"}),
        "disable failure did not retain native and trampoline state");

    gate.open();
    events.clear();
    const auto remove_failure = ff7r::piano::core::shutdown_gated_hooks(gate, {{
        [&] { events.push_back("disable"); return true; },
        [&] { events.push_back("remove"); return false; },
    }}, [&] { events.push_back("restore"); return true; }, [&] { events.push_back("clear"); });
    require(!remove_failure.ok(), "remove failure reported success");
    require(events == std::vector<std::string>({"disable", "restore", "remove"}),
        "remove failure cleared required state");

    gate.open();
    events.clear();
    const auto restore_failure = ff7r::piano::core::shutdown_gated_hooks(gate, {{
        [] { return true; }, [] { return true; },
    }}, [&] { events.push_back("restore"); return false; }, [&] { events.push_back("clear"); });
    require(!restore_failure.ok() && events == std::vector<std::string>({"restore"}),
        "native or thread restore failure removed hooks or cleared state");

    gate.open();
    events.clear();
    const auto selection_five_hook_shutdown
        = ff7r::piano::core::shutdown_gated_hooks(gate, {
            {[&] { events.push_back("disable_action"); return true; },
             [&] { events.push_back("remove_action"); return true; }},
            {[&] { events.push_back("disable_getter"); return true; },
             [&] { events.push_back("remove_getter"); return true; }},
            {[&] { events.push_back("disable_use"); return true; },
             [&] { events.push_back("remove_use"); return true; }},
            {[&] { events.push_back("disable_helper"); return true; },
             [&] { events.push_back("remove_helper"); return true; }},
            {[&] { events.push_back("disable_index"); return true; },
             [&] { events.push_back("remove_index"); return true; }},
        }, {}, [&] { events.push_back("clear_selection"); });
    require(selection_five_hook_shutdown.ok()
            && events == std::vector<std::string>({
                "disable_action", "disable_getter", "disable_use",
                "disable_helper", "disable_index", "remove_action",
                "remove_getter", "remove_use", "remove_helper",
                "remove_index", "clear_selection"}),
        "five-hook selection teardown did not disable/drain/remove transactionally");
}

void test_paused_callback_timeout()
{
    HookCallbackGate gate;
    gate.open();
    auto callback = gate.try_enter();
    require(static_cast<bool>(callback), "callback gate did not open");
    bool remove_called = false;
    bool clear_called = false;
    HookShutdownResult result;
    std::thread shutdown([&] {
        result = ff7r::piano::core::shutdown_gated_hooks(gate, {{
            [] { return true; }, [&] { remove_called = true; return true; },
        }}, [] { return true; }, [&] { clear_called = true; }, std::chrono::milliseconds(10));
    });
    shutdown.join();
    require(!result.callbacks_drained && !remove_called && !clear_called,
        "drain timeout failed to preserve trampoline and callback state");
}

void test_poller_callback_lease_scope_impl();

void test_difficulty_input_merge_state()
{
    using ff7r::piano::game::DiagnosticEmissionAdmission;
    using ff7r::piano::game::DiagnosticEmissionBudget;
    using ff7r::piano::game::DifficultyEventProviderState;
    using ff7r::piano::game::DifficultyInputMergeState;
    using ff7r::piano::game::DifficultyInputProviderFacts;
    using ff7r::piano::game::difficulty_provider_held_after_key_event;
    using ff7r::piano::game::kDifficultyInputDiagnosticLimit;
    using ff7r::piano::game::kNativePianoInputDiagnosticLimit;
    using ff7r::piano::game::NativePianoInputDiagnosticAdmission;
    using ff7r::piano::game::run_native_piano_input_diagnostic;

    NativePianoInputDiagnosticAdmission native_admission;
    int clock_probes = 0;
    int diagnostic_emissions = 0;
    int exhaustion_markers = 0;
    const auto native_event = [&](const std::size_t direction, const int32_t event_type,
                                  const bool clock_active) {
        run_native_piano_input_diagnostic(
            native_admission, direction, event_type,
            [&](double& elapsed) {
                ++clock_probes;
                elapsed = 1.25;
                return clock_active;
            },
            [&](const double elapsed) {
                require(elapsed == 1.25, "native diagnostic lost the admitted clock sample");
                ++diagnostic_emissions;
            },
            [&] { ++exhaustion_markers; });
    };
    native_event(0, 0, false);
    require(clock_probes == 1 && diagnostic_emissions == 0 && exhaustion_markers == 0,
        "inactive playback clock reached native diagnostic snapshot/format/log work");
    native_event(0, 0, true);
    native_event(0, 2, true);
    native_event(0, 0, true);
    require(clock_probes == 2 && diagnostic_emissions == 1,
        "inactive Press stayed latched or active held input duplicated diagnostic work");
    native_event(0, 1, true);
    native_event(0, 2, true);
    native_event(0, 0, true);
    require(clock_probes == 3 && diagnostic_emissions == 2,
        "Release did not rearm exactly one native Press edge");
    native_admission.reset_held();
    native_event(0, 0, true);
    require(clock_probes == 4 && diagnostic_emissions == 3,
        "lifecycle held-state reset did not admit a later active Press");
    native_event(1, 0, true);
    require(clock_probes == 5 && diagnostic_emissions == 4,
        "independent native directions shared held admission state");
    native_event(2, 9, true);
    require(clock_probes == 5 && diagnostic_emissions == 4,
        "unknown native key event reached clock or diagnostic work");

    NativePianoInputDiagnosticAdmission session_retirement_admission{2};
    uint32_t session_emissions = 0;
    uint32_t session_markers = 0;
    const auto session_press = [&] {
        run_native_piano_input_diagnostic(
            session_retirement_admission, 0, 0,
            [](double&) { return true; },
            [&](double) { ++session_emissions; },
            [&] { ++session_markers; });
    };
    session_press();
    session_retirement_admission.reset_held();
    session_press();
    session_retirement_admission.reset_held();
    session_press();
    require(session_emissions == 2 && session_markers == 1,
        "session retirement reset held input or reset the process diagnostic budget incorrectly");

    NativePianoInputDiagnosticAdmission bounded_native_admission;
    uint32_t bounded_native_emissions = 0;
    uint32_t bounded_native_markers = 0;
    for (uint32_t attempt = 0; attempt < kNativePianoInputDiagnosticLimit + 8; ++attempt) {
        run_native_piano_input_diagnostic(
            bounded_native_admission, 0, 0,
            [](double&) { return true; },
            [&](double) { ++bounded_native_emissions; },
            [&] { ++bounded_native_markers; });
        bounded_native_admission.reset_held();
    }
    require(bounded_native_emissions == kNativePianoInputDiagnosticLimit
            && bounded_native_markers == 1,
        "native input diagnostic helper exceeded its exact cap or repeated exhaustion marker");

    const auto require_concurrent_budget = [](const uint32_t limit, const char* message) {
        DiagnosticEmissionBudget budget(limit);
        std::atomic_uint32_t emissions{0};
        std::atomic_uint32_t markers{0};
        constexpr uint32_t kThreadCount = 8;
        std::vector<std::thread> contenders;
        contenders.reserve(kThreadCount);
        for (uint32_t thread = 0; thread < kThreadCount; ++thread) {
            contenders.emplace_back([&] {
                for (uint32_t attempt = 0; attempt < limit; ++attempt) {
                    switch (budget.claim()) {
                    case DiagnosticEmissionAdmission::Emit:
                        emissions.fetch_add(1, std::memory_order_relaxed);
                        break;
                    case DiagnosticEmissionAdmission::EmitExhaustionMarker:
                        markers.fetch_add(1, std::memory_order_relaxed);
                        break;
                    case DiagnosticEmissionAdmission::Rejected:
                        break;
                    }
                }
            });
        }
        for (auto& contender : contenders) contender.join();
        require(emissions.load(std::memory_order_relaxed) == limit
                && markers.load(std::memory_order_relaxed) == 1,
            message);
    };
    require_concurrent_budget(kNativePianoInputDiagnosticLimit,
        "native input diagnostic budget exceeded its exact cap or repeated exhaustion marker");
    require_concurrent_budget(kDifficultyInputDiagnosticLimit,
        "difficulty-edge diagnostic budget exceeded its exact cap or repeated exhaustion marker");

    bool player_input_held = difficulty_provider_held_after_key_event(false, 0);
    require(player_input_held && difficulty_provider_held_after_key_event(player_input_held, 0),
        "repeated PlayerInput keydown did not preserve held state");
    require(difficulty_provider_held_after_key_event(player_input_held, 2),
        "PlayerInput repeat incorrectly released held state");
    require(!difficulty_provider_held_after_key_event(player_input_held, 1),
        "PlayerInput keyup did not release held state");

    DifficultyEventProviderState event_providers;
    event_providers.update_player_input(true, 0);
    event_providers.update_player_input(true, 2);
    auto event_facts = event_providers.snapshot();
    require(event_facts.decrement.player_input_held,
        "production PlayerInput provider did not retain Press/Repeat held state");
    event_providers.update_player_input(true, 1);
    require(!event_providers.snapshot().decrement.player_input_held,
        "production PlayerInput provider did not apply Released state");

    const auto populate_all_event_providers = [&] {
        event_providers.update_player_input(true, 0);
        event_providers.update_window_key(false, true);
        event_providers.update_raw_hid(true, true);
    };
    const auto require_event_providers_clear = [&](const char* message) {
        const auto cleared = event_providers.snapshot();
        require(!cleared.decrement.player_input_held
                && !cleared.increment.window_key_held
                && !cleared.decrement.raw_hid_held
                && !cleared.increment.raw_hid_held,
            message);
    };
    populate_all_event_providers();
    event_providers.reset_for_focus_loss();
    require_event_providers_clear(
        "focus loss did not clear every event-backed provider");
    populate_all_event_providers();
    event_providers.reset_for_application_loss();
    require_event_providers_clear(
        "application loss did not clear every event-backed provider");
    populate_all_event_providers();
    event_providers.reset_for_device_removal();
    require_event_providers_clear(
        "device removal did not clear every event-backed provider");

    event_providers.update_window_key(true, true);
    event_providers.update_window_key(true, false);
    event_providers.update_raw_hid(false, true);
    event_facts = event_providers.snapshot();
    require(!event_facts.decrement.window_key_held
            && event_facts.increment.raw_hid_held,
        "production WindowProc release or RawInput/HID report state was not applied");
    event_providers.reset_for_device_removal();

    DifficultyInputMergeState merge;
    DifficultyInputProviderFacts facts;
    auto edges = merge.update(facts);
    require(!edges.decrement && !edges.increment,
        "idle input merge emitted an edge");

    facts.decrement.player_input_held = true;
    edges = merge.update(facts);
    require(edges.decrement && !edges.increment,
        "PlayerInput press did not emit one decrement edge");
    facts.decrement.xinput_held = true;
    edges = merge.update(facts);
    require(!edges.decrement,
        "adjacent PlayerInput and XInput reports duplicated one press");
    facts.decrement.player_input_held = false;
    require(!merge.update(facts).decrement,
        "provider release rearmed while the physical press remained held");
    facts.decrement.xinput_held = false;
    require(!merge.update(facts).decrement,
        "aggregate release emitted an edge");
    facts.decrement.xinput_held = true;
    require(merge.update(facts).decrement,
        "release followed by a second press did not rearm");

    merge.reset();
    event_providers.update_player_input(true, 0);
    facts = event_providers.snapshot();
    require(merge.update(facts).decrement,
        "production PlayerInput press did not reach aggregate merge");
    facts.decrement.xinput_held = true;
    require(!merge.update(facts).decrement,
        "sampled-provider overlap duplicated an event-backed press");
    event_providers.reset_for_device_removal();
    facts = event_providers.snapshot();
    facts.decrement.xinput_held = true;
    require(!merge.update(facts).decrement,
        "provider reset rearmed while sampled fallback remained held");
    facts.decrement.xinput_held = false;
    require(!merge.update(facts).decrement,
        "device/provider loss emitted an aggregate edge");
    event_providers.update_player_input(true, 0);
    facts = event_providers.snapshot();
    require(merge.update(facts).decrement,
        "focus/device loss followed by repress did not rearm");

    merge.reset();
    facts = {};
    facts.increment.raw_hid_held = true;
    require(merge.update(facts).increment,
        "RawInput/HID press did not emit one increment edge");
    facts.increment.xinput_held = true;
    require(!merge.update(facts).increment,
        "adjacent RawInput/HID and XInput reports duplicated one press");
    require(!merge.update(facts).increment,
        "repeated held reports duplicated an edge");
    facts.increment.raw_hid_held = false;
    require(!merge.update(facts).increment,
        "provider disappearance rearmed while XInput remained held");
    facts.increment.xinput_held = false;
    require(!merge.update(facts).increment,
        "provider disappearance release emitted an edge");

    facts.decrement.keyboard_held = true;
    facts.increment.player_input_held = true;
    edges = merge.update(facts);
    require(edges.decrement && edges.increment,
        "opposite directions were not represented independently");
    edges = merge.update(facts);
    require(!edges.decrement && !edges.increment,
        "repeated keyboard or gamepad held state emitted another edge");

    ff7r::piano::core::HookCallbackGate closed_gate;
    bool submitted = false;
    ff7r::piano::game::run_difficulty_input_poller_iteration(
        closed_gate, [&] { submitted = merge.update(facts).decrement; }, [] {});
    require(!submitted, "closed callback gate consumed merged input");
}

void test_callback_gate_exclusive_conversion()
{
    HookCallbackGate gate;
    gate.open();
    auto callback = gate.try_enter();
    auto exclusive = gate.try_suspend_exclusive(callback);
    require(exclusive && !callback && !gate.try_enter(),
        "callback admission was not exclusively suspended without waiting");
    callback = exclusive.resume_as_lease();
    require(callback && gate.callbacks_in_flight() == 1 && gate.try_enter(),
        "exclusive callback protection was not restored before native continuation");
    callback = {};
    require(gate.callbacks_in_flight() == 0 && gate.try_enter(),
        "resumed callback gate did not reopen after protected callback exit");
    test_poller_callback_lease_scope_impl();
}

void test_poller_callback_lease_scope_impl()
{
    HookCallbackGate gate;
    gate.open();
    std::mutex state_mutex;
    std::condition_variable state_changed;
    bool protected_body = false;
    bool release_body = false;
    bool idle_wait = false;
    bool continue_iteration = false;
    bool closed_body_rejected = false;

    std::thread worker([&] {
        ff7r::piano::game::run_difficulty_input_poller_iteration(
            gate,
            [&] {
                std::unique_lock lock(state_mutex);
                protected_body = true;
                state_changed.notify_all();
                state_changed.wait(lock, [&] { return release_body; });
            },
            [&] {
                std::unique_lock lock(state_mutex);
                idle_wait = true;
                state_changed.notify_all();
                state_changed.wait(lock, [&] { return continue_iteration; });
            });
        bool closed_body_entered = false;
        ff7r::piano::game::run_difficulty_input_poller_iteration(
            gate, [&] { closed_body_entered = true; }, [] {});
        {
            std::lock_guard lock(state_mutex);
            closed_body_rejected = !closed_body_entered;
        }
        state_changed.notify_all();
    });

    {
        std::unique_lock lock(state_mutex);
        state_changed.wait(lock, [&] { return protected_body; });
    }
    auto caller = gate.try_enter();
    auto blocked = gate.try_suspend_exclusive(caller);
    require(!blocked && caller,
        "protected poll body allowed exclusive conversion through real concurrency");
    caller = {};

    {
        std::lock_guard lock(state_mutex);
        release_body = true;
    }
    state_changed.notify_all();
    {
        std::unique_lock lock(state_mutex);
        state_changed.wait(lock, [&] { return idle_wait; });
    }
    caller = gate.try_enter();
    auto exclusive = gate.try_suspend_exclusive(caller);
    require(exclusive && !caller,
        "idle poll wait retained callback protection after the body ended");
    caller = exclusive.resume_as_lease();
    caller = {};

    gate.close();
    require(gate.drain(std::chrono::milliseconds(1000)),
        "closed gate did not drain while poller idled outside its lease");
    {
        std::lock_guard lock(state_mutex);
        continue_iteration = true;
    }
    state_changed.notify_all();
    {
        std::unique_lock lock(state_mutex);
        state_changed.wait(lock, [&] { return closed_body_rejected; });
    }
    worker.join();
    require(gate.callbacks_in_flight() == 0,
        "poller callback lease leaked across iterations");
    test_difficulty_input_merge_state();
}

void test_transactional_native_restore()
{
    int pointer_field = 20;
    int count_field = 30;
    bool storage_released = false;
    int restore_attempt = 0;
    std::vector<NativeRestoreOperation> operations{
        { [&] { return pointer_field == 20; }, [&] { pointer_field = 10; return true; }, [&] { pointer_field = 20; return true; } },
        { [&] { return count_field == 30; }, [&] { ++restore_attempt; return false; }, [&] { count_field = 30; return true; } },
    };
    require(!ff7r::piano::core::restore_native_state_transactionally(operations),
        "partial list restoration unexpectedly succeeded");
    require(pointer_field == 20 && count_field == 30 && !storage_released && restore_attempt == 1,
        "failed list restoration did not roll back redirected fields");

    operations[1].restore_original = [&] { count_field = 11; return true; };
    require(ff7r::piano::core::restore_native_state_transactionally(operations),
        "valid list restoration failed");
    storage_released = true;
    require(pointer_field == 10 && count_field == 11 && storage_released,
        "list storage released before all fields restored");
}

void test_native_array_publication_journal()
{
    using namespace ff7r::piano::game;
    const NativeArrayTuple original{0x1000, 3, 5};
    const NativeArrayTuple redirected{0x2000, 8, 8};

    TupleMemory publication{original};
    auto result = publish_native_array_tuple(original, 5, redirected, 8, publication.access());
    require(result.committed && result.retain_redirected_storage && publication.value == redirected,
        "production list publication did not commit redirected storage");
    require(publication.observed == std::vector<NativeArrayTuple>({
        {0x2000, 3, 5}, {0x2000, 3, 8}, {0x2000, 8, 8},
    }), "list publication ordering changed");
    require_safe_states(publication, "list publication exposed bounds beyond visible storage");

    TupleMemory restoration{redirected};
    result = restore_native_array_tuple(original, 5, redirected, 8, restoration.access());
    require(result.committed && !result.retain_redirected_storage && restoration.value == original,
        "production list restoration did not release redirected ownership");
    require(restoration.observed == std::vector<NativeArrayTuple>({
        {0x2000, 3, 8}, {0x2000, 3, 5}, {0x1000, 3, 5},
    }), "list restoration did not shrink bounds before restoring pointer");
    require_safe_states(restoration, "list restoration exposed bounds beyond visible storage");

    const NativeArrayTuple newer_prefix{0x3000, 10, 12};
    TupleMemory prefix_replacement{redirected};
    result = publish_native_array_tuple(redirected, 8, newer_prefix, 12,
        prefix_replacement.access());
    require(result.committed && prefix_replacement.value == newer_prefix,
        "old-prefix to newer-prefix publication failed");
    require(prefix_replacement.observed == std::vector<NativeArrayTuple>({
        {0x3000, 8, 8}, {0x3000, 8, 12}, {0x3000, 10, 12},
    }), "prefix replacement ordering changed");
    require_safe_states(prefix_replacement,
        "old-prefix to newer-prefix exposed an unsafe tuple");

    TupleMemory stale_source{{0x1000, 2, 5}};
    result = publish_native_array_tuple(original, 5, redirected, 8, stale_source.access());
    require(!result.committed && !result.pre_state_validated
            && stale_source.observed.empty() && stale_source.value == NativeArrayTuple{0x1000, 2, 5},
        "unexpected tuple performed native writes");

    for (int fail_after = 1; fail_after <= 3; ++fail_after) {
        TupleMemory memory{original};
        result = publish_native_array_tuple(original, 5, redirected, 8, memory.access(),
            {fail_after, 0});
        require(!result.committed && result.rollback_attempted && result.rollback_verified
                && !result.retain_redirected_storage && memory.value == original,
            "publication fault did not restore every written tuple field");
        require_safe_states(memory, "publication fault rollback exposed an unsafe tuple");
    }
    for (int fail_after = 1; fail_after <= 3; ++fail_after) {
        TupleMemory memory{redirected};
        result = restore_native_array_tuple(original, 5, redirected, 8, memory.access(),
            {fail_after, 0});
        require(!result.committed && result.rollback_attempted && result.rollback_verified
                && result.retain_redirected_storage && memory.value == redirected,
            "restoration fault did not republish every redirected tuple field");
        require_safe_states(memory, "restoration fault rollback exposed an unsafe tuple");
    }

    for (int rollback_fail_after = 1; rollback_fail_after <= 3; ++rollback_fail_after) {
        TupleMemory memory{original};
        result = publish_native_array_tuple(original, 5, redirected, 8, memory.access(),
            {3, rollback_fail_after});
        require(!result.committed && !result.rollback_verified && result.retain_redirected_storage,
            "publication rollback fault allowed redirected storage release");
        require_safe_states(memory, "publication rollback failure exposed an unsafe tuple");
    }
    for (int rollback_fail_after = 1; rollback_fail_after <= 3; ++rollback_fail_after) {
        TupleMemory memory{redirected};
        result = restore_native_array_tuple(original, 5, redirected, 8, memory.access(),
            {3, rollback_fail_after});
        require(!result.committed && !result.rollback_verified && result.retain_redirected_storage,
            "restoration rollback fault allowed redirected storage release");
        require_safe_states(memory, "restoration rollback failure exposed an unsafe tuple");
    }
}

void test_render_context_and_registry_generation()
{
    using namespace ff7r::piano::game;
    SongRegistry& registry_under_test = registry();
    registry_under_test.replace({make_song("real", 1), make_song("rendered", 2)});
    registry_under_test.set_active_selection(1, 7);
    const ActiveSongSnapshot old_snapshot = registry_under_test.active_snapshot();

    std::atomic_bool context_ready{false};
    std::atomic_bool selection_changed{false};
    std::thread selection([&] {
        while (!context_ready.load(std::memory_order_acquire)) std::this_thread::yield();
        registry_under_test.set_active_selection(1, 9);
        selection_changed.store(true, std::memory_order_release);
    });
    {
        ScopedSongRenderContext render(2, 8);
        context_ready.store(true, std::memory_order_release);
        while (!selection_changed.load(std::memory_order_acquire)) std::this_thread::yield();
        const RenderSnapshot rendered = registry_under_test.render_snapshot();
        require(rendered.song && rendered.song->id == "rendered"
                && rendered.visible_index == 2 && rendered.base_slot == 8,
            "thread-local render context did not retain its row identity");
        require(registry_under_test.active_visible_index() == 1
                && registry_under_test.active_base_slot() == 9,
            "thread-local render context contaminated concurrent real selection");
    }
    selection.join();
    require(registry_under_test.active_visible_index() == 1 && registry_under_test.active_base_slot() == 9,
        "concurrent real selection was overwritten");

    registry_under_test.replace({make_song("replacement", 1)});
    const ActiveSongSnapshot replacement = registry_under_test.active_snapshot();
    require(old_snapshot.storage && old_snapshot.song && old_snapshot.song->id == "real",
        "immutable registry storage did not retain old generation");
    require(replacement.generation != old_snapshot.generation && replacement.song == nullptr,
        "registry generation/address reuse was not rejected");
}

void test_runtime_context_isolation()
{
    using namespace ff7r::piano::game;
    SongDescriptor song = make_song("Lets", 7);
    song.title = L"Let's Play Piano";
    song.duration_seconds = 163.004f;
    song.profiles[0].title = L"Let's Play Piano [Custom]";
    SongDescriptor encore = make_song("Encore", 8);
    encore.title = L"Encore";
    encore.profiles[0].title = L"Encore [Custom]";
    SongRegistry& registry_under_test = registry();
    registry_under_test.replace({song, encore});
    registry_under_test.set_active_selection(7, 0);
    const SelectionSnapshot selection = registry_under_test.selection_snapshot();
    require(selection.song && selection.profile, "custom selection snapshot was not published");

    CustomContextToken armed;
    armed.registry_generation = selection.generation;
    armed.route_generation = 4;
    armed.lease_generation = 1;
    armed.song_key = 0x4c657473;
    armed.bgm = reinterpret_cast<void*>(0x9009); // Native bgm_piano_09 alias identity.
    armed.sound = reinterpret_cast<void*>(0x2000);
    require(registry_under_test.publish_playback(selection, armed),
        "exact armed playback token was rejected");
    const PlaybackSnapshot playback = registry_under_test.playback_snapshot();
    require(playback.song == selection.song
            && playback_duration_or_original(playback, 10000.0f) == 163.004f,
        "custom playback duration was not generation-bound");
    require(descriptor_title_text(*playback.song, playback.profile)
                == L"Let's Play Piano [Custom]",
        "native BGM alias replaced the custom playback title");

    {
        ScopedSongRenderContext render(7, 9);
        const RenderSnapshot rendered = registry_under_test.render_snapshot();
        require(rendered.song == selection.song && rendered.base_slot == 9,
            "list render scope did not publish its row metadata");
        require(registry_under_test.playback_snapshot().token == armed,
            "list render scope mutated runtime playback eligibility");
    }

    CustomContextToken playing = armed;
    playing.route_generation = 5;
    playing.controller = reinterpret_cast<void*>(0x1000);
    playing.slot = reinterpret_cast<void*>(0x1100);
    playing.request_handle = 0x400010008ull;
    require(registry_under_test.update_playback_token(armed, playing),
        "custom PlaySetup token did not advance transactionally");
    require(!registry_under_test.update_playback_token(armed, playing),
        "stale route generation updated a reused playback token");

    const PlaybackSnapshot playing_snapshot = registry_under_test.playback_snapshot();
    TitleResolverStack resolvers;
    const TitleResolverToken outer{playing_snapshot, reinterpret_cast<void*>(0x3000)};
    const TitleResolverToken inner{playing_snapshot, reinterpret_cast<void*>(0x3100)};
    resolvers.push(outer);
    resolvers.push(inner);
    require(resolvers.size() == 2 && resolvers.peek().row == inner.row
            && !resolvers.consume(outer) && resolvers.consume(inner)
            && resolvers.consume(outer) && resolvers.size() == 0,
        "nested title resolver tokens were not consumed in LIFO order");
    resolvers.push(outer);
    resolvers.invalidate(playing);
    require(resolvers.size() == 0,
        "stale title resolver generation survived playback invalidation");

    require(registry_under_test.revoke_playback(playing),
        "native handoff did not revoke custom playback");
    const PlaybackSnapshot native = registry_under_test.playback_snapshot();
    const CleanupLease cleanup = registry_under_test.cleanup_lease();
    require(!native.song && cleanup.song == selection.song
            && cleanup.profile == selection.profile && cleanup.token == playing,
        "frozen cleanup identity leaked through or was lost after native handoff");
    require(playback_duration_or_original(native, 10000.0f) == 10000.0f,
        "line-292 native duration was overridden after playback revocation");
    require(!title_resolver_token_matches(outer, native, outer.row),
        "stale ScoreInfo generation remained title-eligible after handoff");
    require(scoreinfo_wrapper_restore_allowed(outer.row, outer.row,
                reinterpret_cast<void*>(0x3200))
            && scoreinfo_wrapper_restore_allowed(reinterpret_cast<void*>(0x3200), outer.row,
                reinterpret_cast<void*>(0x3200))
            && !scoreinfo_wrapper_restore_allowed(reinterpret_cast<void*>(0x3300), outer.row,
                reinterpret_cast<void*>(0x3200)),
        "ScoreInfo wrapper restoration accepted a stale foreign publication");
    require(registry_under_test.selection_snapshot().song == selection.song,
        "cleanup retirement mutated the menu selection");
    require(registry_under_test.retire_cleanup_lease(playing)
            && !registry_under_test.cleanup_lease().song,
        "quiescent retirement did not release the exact cleanup lease");

    registry_under_test.set_active_selection(8, 1);
    const SelectionSnapshot next_selection = registry_under_test.selection_snapshot();
    CustomContextToken next = playing;
    next.registry_generation = next_selection.generation;
    next.route_generation = 6;
    next.lease_generation = 2;
    next.song_key = 0x456e636f7265ull;
    require(registry_under_test.publish_playback(next_selection, next)
            && registry_under_test.playback_snapshot().song == next_selection.song,
        "custom-to-custom playback did not publish the new exact identity");
    require(!registry_under_test.revoke_playback(playing)
            && registry_under_test.playback_snapshot().token == next,
        "stale repeated-custom identity revoked the new playback");
    require(registry_under_test.revoke_playback(next)
            && registry_under_test.retire_cleanup_lease(next),
        "repeated custom playback did not retire independently");
}

void test_private_controller_stop_diagnostics()
{
    using namespace ff7r::piano::game;
    constexpr DiagnosticFact accepted = DiagnosticFact::Accepted;
    constexpr DiagnosticFact rejected = DiagnosticFact::Rejected;
    constexpr DiagnosticFact not_evaluated = DiagnosticFact::NotEvaluated;
    PrivateControllerStopDiagnosticEvidence accepted_evidence{
        accepted, accepted, accepted, accepted, accepted,
        PrivateControllerStopObservationResult::Accepted,
    };
    accepted_evidence.identity_observation.prefilter_result =
        UObjectIdentityPrefilterResult::Passed;
    require(classify_private_controller_stop_diagnostic(accepted_evidence)
            == PrivateControllerStopDiagnostic::Accepted,
        "all-true private Stop diagnostic evidence was rejected");
    const PrivateControllerStopDiagnosticEvidence incomplete_chain_evidence{};
    require(incomplete_chain_evidence.selection_guard_acquired == not_evaluated
            && classify_private_controller_stop_diagnostic(incomplete_chain_evidence)
                == PrivateControllerStopDiagnostic::ControllerIdentityChainIncomplete,
        "not-evaluated selection guard was misreported as rejected");

    struct DiagnosticCase {
        PrivateControllerStopDiagnosticEvidence evidence;
        PrivateControllerStopDiagnostic expected;
        const char* message;
    };
    const DiagnosticCase diagnostic_cases[] = {
        {{rejected, accepted, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::Accepted},
            PrivateControllerStopDiagnostic::SelectionGuardRejected,
            "selection-guard diagnostic failure was misclassified"},
        {{accepted, rejected, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::Accepted},
            PrivateControllerStopDiagnostic::RouteGenerationChanged,
            "route-generation diagnostic failure was misclassified"},
        {{accepted, accepted, rejected, accepted, accepted,
             PrivateControllerStopObservationResult::Accepted},
            PrivateControllerStopDiagnostic::LeaseChanged,
            "lease diagnostic failure was misclassified"},
        {{accepted, accepted, accepted, rejected, accepted,
             PrivateControllerStopObservationResult::Accepted},
            PrivateControllerStopDiagnostic::PhaseNotArmed,
            "route-phase diagnostic failure was misclassified"},
        {{accepted, accepted, accepted, accepted, rejected,
             PrivateControllerStopObservationResult::Accepted},
            PrivateControllerStopDiagnostic::ControllerIdentityChanged,
            "controller-identity diagnostic failure was misclassified"},
        {{accepted, accepted, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::ContextInvalid},
            PrivateControllerStopDiagnostic::ObserveContextInvalid,
            "observe-controller-stop diagnostic failure was misclassified"},
        {{accepted, accepted, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::TokenChanged},
            PrivateControllerStopDiagnostic::ObserveTokenChanged,
            "observe-controller-stop token failure was misclassified"},
        {{accepted, accepted, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::StageChanged},
            PrivateControllerStopDiagnostic::ObserveStageChanged,
            "observe-controller-stop stage failure was misclassified"},
        {{accepted, accepted, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::RequiredContextMissing},
            PrivateControllerStopDiagnostic::ObserveRequiredContextMissing,
            "observe-controller-stop context failure was misclassified"},
        {{accepted, accepted, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::ControllerHandleInvalid},
            PrivateControllerStopDiagnostic::ObserveControllerHandleInvalid,
            "observe-controller-stop controller-handle failure was misclassified"},
        {{accepted, accepted, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::SoundHandleInvalid},
            PrivateControllerStopDiagnostic::ObserveSoundHandleInvalid,
            "observe-controller-stop sound-handle failure was misclassified"},
        {{accepted, accepted, accepted, accepted, accepted,
             PrivateControllerStopObservationResult::Rejected},
            PrivateControllerStopDiagnostic::ObserveRejected,
            "unexpected observe-controller-stop rejection was misclassified"},
        {{rejected, rejected, rejected, rejected, rejected,
             PrivateControllerStopObservationResult::ContextInvalid},
            PrivateControllerStopDiagnostic::SelectionGuardRejected,
            "private Stop diagnostic precedence did not select the first failed fact"},
        {{accepted, rejected, rejected, rejected, rejected,
             PrivateControllerStopObservationResult::ContextInvalid},
            PrivateControllerStopDiagnostic::RouteGenerationChanged,
            "private Stop diagnostic precedence changed after guard acceptance"},
    };

    SongRegistry publication_registry;
    SongDescriptor song = make_song("stop-diagnostic", 4);
    publication_registry.replace({song});
    publication_registry.set_active_selection(4, 0);
    const SelectionSnapshot selection = publication_registry.selection_snapshot();
    CustomContextToken token;
    token.registry_generation = selection.generation;
    token.route_generation = 70;
    token.lease_generation = 80;
    token.song_key = 90;
    UnpublishedAudioSetupContext successful_arm{selection, token};
    const UObjectLiveHandle successful_controller_handle{71, 710};
    const ControllerIdentityProof successful_controller_proof =
        make_controller_identity_proof(
            reinterpret_cast<void*>(0x7100), reinterpret_cast<void*>(0x7110),
            0x71, 2, reinterpret_cast<void*>(0x7120), true,
            successful_controller_handle.internal_index, true,
            successful_controller_handle);
    require(successful_arm.bind_route_controller(successful_controller_proof),
        "direct Stop diagnostic fixture did not establish a successful serial arm");

    PrivateControllerStopDiagnosticEvidence prefilter_rejection_evidence;
    prefilter_rejection_evidence.identity_observation = {
        UObjectIdentityPrefilterResult::ExpectedLiveHandleValidationFailed,
        true,
        uobject_locator_core::UObjectLiveHandleCaptureResult::SerialInvalid,
    };
    require(classify_private_controller_stop_diagnostic(prefilter_rejection_evidence)
            == PrivateControllerStopDiagnostic::ControllerIdentityPrefilterRejected,
        "expected-live-handle validation failure did not select the prefilter primary reason");
    require(prefilter_rejection_evidence.selection_guard_acquired == not_evaluated
            && prefilter_rejection_evidence.route_generation_unchanged == not_evaluated
            && prefilter_rejection_evidence.lease_unchanged == not_evaluated
            && prefilter_rejection_evidence.phase_armed == not_evaluated
            && prefilter_rejection_evidence.controller_identity_unchanged
                == not_evaluated
            && prefilter_rejection_evidence.observe_result
                == PrivateControllerStopObservationResult::NotEvaluated,
        "prefilter rejection record did not retain not-evaluated post-filter facts");
    require(prefilter_rejection_evidence.identity_observation
                .observed_live_capture_attempted
            && prefilter_rejection_evidence.identity_observation
                .observed_live_capture_result
                == uobject_locator_core::UObjectLiveHandleCaptureResult::SerialInvalid
            && successful_arm.controller_proof.mode
                == ControllerIdentityProofMode::SerialBacked,
        "prefilter rejection record lost the independent observed capture reason or arm proof");
    require(!publish_private_controller_claim_if_proven(
                publication_registry, successful_arm)
            && !publication_registry.playback_snapshot().song,
        "prefilter-rejected Stop evidence published playback");

    PrivateControllerStopDiagnosticEvidence prefilter_precedence_evidence{
        rejected, rejected, rejected, rejected, rejected,
        PrivateControllerStopObservationResult::ContextInvalid,
    };
    prefilter_precedence_evidence.identity_observation.prefilter_result =
        UObjectIdentityPrefilterResult::ExpectedLiveHandleValidationFailed;
    require(classify_private_controller_stop_diagnostic(prefilter_precedence_evidence)
            == PrivateControllerStopDiagnostic::ControllerIdentityPrefilterRejected,
        "post-prefilter evidence incorrectly outranked the prefilter rejection");

    for (const DiagnosticCase& diagnostic_case : diagnostic_cases) {
        require(classify_private_controller_stop_diagnostic(diagnostic_case.evidence)
                == diagnostic_case.expected,
            diagnostic_case.message);
        UnpublishedAudioSetupContext unchanged{selection, token};
        require(unchanged.controller_stage == PrivateControllerSetupStage::AwaitingStop
                && !unchanged.controller && !unchanged.expected_sound
                && unchanged.request_before_set == 0
                && !publish_private_controller_claim_if_proven(
                    publication_registry, unchanged)
                && !publication_registry.playback_snapshot().song,
            "diagnostic failure published or acquired private controller ownership");
    }

    const PrivateControllerStopObservationEvidence observation_accepted{
        accepted, accepted, accepted, accepted, accepted, accepted,
    };
    require(classify_private_controller_stop_observation(observation_accepted)
            == PrivateControllerStopObservationResult::Accepted,
        "all-true observe-controller-stop evidence was rejected");
    struct ObservationCase {
        PrivateControllerStopObservationEvidence evidence;
        PrivateControllerStopObservationResult expected;
        const char* message;
    };
    const ObservationCase observation_cases[] = {
        {{rejected, accepted, accepted, accepted, accepted, accepted},
            PrivateControllerStopObservationResult::ContextInvalid,
            "invalid Stop context was misclassified"},
        {{accepted, rejected, accepted, accepted, accepted, accepted},
            PrivateControllerStopObservationResult::TokenChanged,
            "Stop token drift was misclassified"},
        {{accepted, accepted, rejected, accepted, accepted, accepted},
            PrivateControllerStopObservationResult::StageChanged,
            "Stop stage drift was misclassified"},
        {{accepted, accepted, accepted, rejected, accepted, accepted},
            PrivateControllerStopObservationResult::RequiredContextMissing,
            "missing Stop context was misclassified"},
        {{accepted, accepted, accepted, accepted, rejected, accepted},
            PrivateControllerStopObservationResult::ControllerHandleInvalid,
            "invalid Stop controller handle was misclassified"},
        {{accepted, accepted, accepted, accepted, accepted, rejected},
            PrivateControllerStopObservationResult::SoundHandleInvalid,
            "invalid Stop sound handle was misclassified"},
        {{rejected, not_evaluated, not_evaluated, not_evaluated,
             not_evaluated, not_evaluated},
            PrivateControllerStopObservationResult::ContextInvalid,
            "observe-controller-stop precedence did not select context first"},
        {{accepted, rejected, not_evaluated, not_evaluated,
             not_evaluated, not_evaluated},
            PrivateControllerStopObservationResult::TokenChanged,
            "observe-controller-stop precedence changed after context acceptance"},
    };
    for (const ObservationCase& observation_case : observation_cases) {
        require(classify_private_controller_stop_observation(observation_case.evidence)
                == observation_case.expected,
            observation_case.message);
    }

    void* const controller = reinterpret_cast<void*>(0x7100);
    void* const slot = reinterpret_cast<void*>(0x7200);
    void* const bgm = reinterpret_cast<void*>(0x7300);
    void* const sound = reinterpret_cast<void*>(0x7400);
    const UObjectLiveHandle controller_handle{71, 710};
    const UObjectLiveHandle sound_handle{74, 740};
    const ControllerIdentityProof controller_proof = make_controller_identity_proof(
        controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
        reinterpret_cast<void*>(0x7120), true, controller_handle.internal_index,
        true, controller_handle);
    const ControllerIdentityProof structural_controller_proof = make_controller_identity_proof(
        controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
        reinterpret_cast<void*>(0x7120), true, -1, false, {-1, 0});
    const uobject_locator_core::UObjectItemBackedZeroSerialSnapshot
        zero_serial_item{controller_handle.internal_index, 0};
    const ControllerIdentityProof zero_serial_controller_proof =
        make_controller_identity_proof(
            controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
            reinterpret_cast<void*>(0x7120), true,
            controller_handle.internal_index, false, {-1, 0}, true,
            zero_serial_item);
    const ControllerIdentityProof positive_index_capture_failed =
        make_controller_identity_proof(
            controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
            reinterpret_cast<void*>(0x7120), true, controller_handle.internal_index,
            false, {-1, 0});
    const ControllerIdentityProof positive_index_capture_mismatched =
        make_controller_identity_proof(
            controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
            reinterpret_cast<void*>(0x7120), true, controller_handle.internal_index,
            true, {controller_handle.internal_index + 1, controller_handle.serial_number});
    const ControllerIdentityProof unreadable_index = make_controller_identity_proof(
        controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
        reinterpret_cast<void*>(0x7120), false, -1, false, {-1, 0});
    const ControllerIdentityProof malformed_negative_capture =
        make_controller_identity_proof(
            controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
            reinterpret_cast<void*>(0x7120), true, -1, true, controller_handle);
    const ControllerIdentityProof malformed_zero_serial = make_controller_identity_proof(
        controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
        reinterpret_cast<void*>(0x7120), true, controller_handle.internal_index,
        true, {controller_handle.internal_index, 0});
    const ControllerIdentityProof zero_serial_negative = make_controller_identity_proof(
        controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
        reinterpret_cast<void*>(0x7120), true, controller_handle.internal_index,
        false, {-1, 0}, true, {controller_handle.internal_index, -1});
    const ControllerIdentityProof zero_serial_positive = make_controller_identity_proof(
        controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
        reinterpret_cast<void*>(0x7120), true, controller_handle.internal_index,
        false, {-1, 0}, true, {controller_handle.internal_index, 1});
    const ControllerIdentityProof zero_serial_item_mismatch =
        make_controller_identity_proof(
            controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
            reinterpret_cast<void*>(0x7120), true,
            controller_handle.internal_index, false, {-1, 0}, true,
            {controller_handle.internal_index + 1, 0});
    require(controller_proof.mode == ControllerIdentityProofMode::SerialBacked
            && structural_controller_proof.mode == ControllerIdentityProofMode::Structural
            && zero_serial_controller_proof.mode
                == ControllerIdentityProofMode::ItemBackedZeroSerial
            && positive_index_capture_failed.mode == ControllerIdentityProofMode::Invalid
            && positive_index_capture_mismatched.mode == ControllerIdentityProofMode::Invalid
            && unreadable_index.mode == ControllerIdentityProofMode::Invalid
            && malformed_negative_capture.mode == ControllerIdentityProofMode::Invalid
            && malformed_zero_serial.mode == ControllerIdentityProofMode::Invalid
            && zero_serial_negative.mode == ControllerIdentityProofMode::Invalid
            && zero_serial_positive.mode == ControllerIdentityProofMode::Invalid
            && zero_serial_item_mismatch.mode == ControllerIdentityProofMode::Invalid,
        "controller raw-index/live-capture classification was not fail closed");
    using CaptureResult = uobject_locator_core::UObjectLiveHandleCaptureResult;
    const CaptureResult positive_index_capture_failures[]{
        CaptureResult::ResolverOrViewUnavailable,
        CaptureResult::UObjectFieldsUnavailable,
        CaptureResult::IndexOutOfRange,
        CaptureResult::HeaderInvalid,
        CaptureResult::ChunkOutOfRange,
        CaptureResult::ChunkPointerUnreadableOrNull,
        CaptureResult::ItemAddressOrReadFailure,
        CaptureResult::ObjectMismatch,
        CaptureResult::SerialInvalid,
        CaptureResult::ItemFlagsDead,
        CaptureResult::UObjectDestroyed,
    };
    for (const CaptureResult capture_failure : positive_index_capture_failures) {
        const ControllerIdentityProof failed_proof = make_controller_identity_proof(
            controller, reinterpret_cast<void*>(0x7110), 0x71, 2,
            reinterpret_cast<void*>(0x7120), true, controller_handle.internal_index,
            uobject_locator_core::live_handle_capture_succeeded(capture_failure),
            {-1, 0});
        UnpublishedAudioSetupContext unpublished{selection, token};
        require(failed_proof.mode == ControllerIdentityProofMode::Invalid
                && !unpublished.bind_route_controller(failed_proof)
                && !controller_identity_proof_valid(unpublished.controller_proof),
            "positive-index capture failure became valid or publishable");
    }
    require(controller_identity_proof_matches(controller_proof, controller_proof)
            && controller_identity_proof_matches(
                structural_controller_proof, structural_controller_proof)
            && controller_identity_proof_matches(
                zero_serial_controller_proof, zero_serial_controller_proof),
        "valid controller proof modes did not match themselves");
    ControllerIdentityProof pointer_drift = controller_proof;
    pointer_drift.controller = reinterpret_cast<void*>(0x7101);
    ControllerIdentityProof class_drift = controller_proof;
    class_drift.object_class = reinterpret_cast<void*>(0x7111);
    ControllerIdentityProof name_drift = controller_proof;
    ++name_drift.name_comparison_id;
    ControllerIdentityProof name_number_drift = controller_proof;
    ++name_number_drift.name_number;
    ControllerIdentityProof outer_drift = controller_proof;
    outer_drift.outer = reinterpret_cast<void*>(0x7121);
    ControllerIdentityProof index_drift = controller_proof;
    ++index_drift.raw_internal_index;
    ++index_drift.live.internal_index;
    ControllerIdentityProof serial_drift = controller_proof;
    ++serial_drift.live.serial_number;
    ControllerIdentityProof mixed_mode = structural_controller_proof;
    mixed_mode.mode = ControllerIdentityProofMode::SerialBacked;
    ControllerIdentityProof structural_index_drift = structural_controller_proof;
    --structural_index_drift.raw_internal_index;
    ControllerIdentityProof zero_item_index_drift = zero_serial_controller_proof;
    ++zero_item_index_drift.raw_internal_index;
    ++zero_item_index_drift.item_backed_zero_serial.internal_index;
    ControllerIdentityProof zero_serial_positive_drift = zero_serial_controller_proof;
    zero_serial_positive_drift.item_backed_zero_serial.serial_number = 1;
    ControllerIdentityProof zero_serial_negative_drift = zero_serial_controller_proof;
    zero_serial_negative_drift.item_backed_zero_serial.serial_number = -1;
    const ControllerIdentityProof missing_proof{};
    const ControllerIdentityProof rejected_proofs[] = {
        pointer_drift, class_drift, name_drift, name_number_drift,
        outer_drift, index_drift,
        serial_drift, mixed_mode, missing_proof,
        positive_index_capture_failed, positive_index_capture_mismatched,
        unreadable_index, malformed_negative_capture, malformed_zero_serial,
        zero_serial_negative, zero_serial_positive, zero_serial_item_mismatch,
    };
    for (const ControllerIdentityProof& rejected_proof : rejected_proofs) {
        require(!controller_identity_proof_matches(controller_proof, rejected_proof),
            "controller proof identity drift was accepted");
    }
    require(!controller_identity_proof_matches(
                controller_proof, structural_controller_proof)
            && !controller_identity_proof_matches(
                structural_controller_proof, controller_proof)
            && !controller_identity_proof_matches(
                structural_controller_proof, structural_index_drift),
        "mixed serial-backed/structural controller proof modes matched");
    require(!controller_identity_proof_matches(
                zero_serial_controller_proof, zero_item_index_drift)
            && !controller_identity_proof_matches(
                zero_serial_controller_proof, zero_serial_positive_drift)
            && !controller_identity_proof_matches(
                zero_serial_controller_proof, zero_serial_negative_drift)
            && !controller_identity_proof_matches(
                zero_serial_controller_proof, controller_proof)
            && !controller_identity_proof_matches(
                zero_serial_controller_proof, structural_controller_proof),
        "zero-serial item/index/serial/mode drift was accepted");

    struct ProofMismatchCase {
        ControllerIdentityProof expected;
        ControllerIdentityProof current;
        ControllerIdentityProofMismatch mismatch;
        const char* message;
    };
    const ProofMismatchCase mismatch_cases[] = {
        {controller_proof, controller_proof, ControllerIdentityProofMismatch::None,
            "matching serial proof was classified as drift"},
        {structural_controller_proof, structural_controller_proof,
            ControllerIdentityProofMismatch::None,
            "matching structural proof was classified as drift"},
        {structural_controller_proof, structural_index_drift,
            ControllerIdentityProofMismatch::RawIndex,
            "structural controller raw-index drift was misclassified"},
        {zero_serial_controller_proof, zero_serial_controller_proof,
            ControllerIdentityProofMismatch::None,
            "matching zero-serial item proof was classified as drift"},
        {missing_proof, controller_proof,
            ControllerIdentityProofMismatch::ExpectedInvalid,
            "invalid expected proof was not classified first"},
        {controller_proof, missing_proof,
            ControllerIdentityProofMismatch::CurrentInvalid,
            "invalid current proof was not classified first"},
        {controller_proof, structural_controller_proof,
            ControllerIdentityProofMismatch::Mode,
            "proof mode drift was misclassified"},
        {controller_proof, pointer_drift,
            ControllerIdentityProofMismatch::ObjectPointer,
            "controller pointer drift was misclassified"},
        {controller_proof, class_drift,
            ControllerIdentityProofMismatch::ObjectClass,
            "controller class drift was misclassified"},
        {controller_proof, name_drift,
            ControllerIdentityProofMismatch::NameComparisonIndex,
            "controller name comparison-index drift was misclassified"},
        {controller_proof, name_number_drift,
            ControllerIdentityProofMismatch::NameNumber,
            "controller name-number drift was misclassified"},
        {controller_proof, outer_drift,
            ControllerIdentityProofMismatch::Outer,
            "controller outer drift was misclassified"},
        {controller_proof, index_drift,
            ControllerIdentityProofMismatch::LiveIndex,
            "controller live-index drift was misclassified"},
        {controller_proof, serial_drift,
            ControllerIdentityProofMismatch::Serial,
            "controller serial drift was misclassified"},
        {zero_serial_controller_proof, zero_item_index_drift,
            ControllerIdentityProofMismatch::ItemIndex,
            "zero-serial controller item-index drift was misclassified"},
        {zero_serial_controller_proof, zero_serial_positive_drift,
            ControllerIdentityProofMismatch::CurrentInvalid,
            "zero-serial controller positive-serial drift was misclassified"},
        {zero_serial_controller_proof, zero_serial_negative_drift,
            ControllerIdentityProofMismatch::CurrentInvalid,
            "zero-serial controller negative-serial drift was misclassified"},
    };
    for (const ProofMismatchCase& mismatch_case : mismatch_cases) {
        const ControllerIdentityProofMismatchReport report =
            classify_controller_identity_proof_mismatch(
                mismatch_case.expected, mismatch_case.current);
        require(report.first_mismatch == mismatch_case.mismatch,
            mismatch_case.message);
        require((report.first_mismatch == ControllerIdentityProofMismatch::None)
                == controller_identity_proof_matches(
                    mismatch_case.expected, mismatch_case.current),
            "controller proof classifier disagreed with the production matcher");
    }
    require(classify_controller_identity_proof_mismatch(
                missing_proof, positive_index_capture_failed).first_mismatch
                == ControllerIdentityProofMismatch::ExpectedInvalid,
        "expected-invalid precedence changed when both proofs were invalid");
    ControllerIdentityProof mode_and_pointer_drift = structural_controller_proof;
    mode_and_pointer_drift.controller = pointer_drift.controller;
    require(classify_controller_identity_proof_mismatch(
                controller_proof, mode_and_pointer_drift).first_mismatch
                == ControllerIdentityProofMismatch::Mode,
        "mode mismatch did not precede pointer mismatch");
    ControllerIdentityProof pointer_and_class_drift = pointer_drift;
    pointer_and_class_drift.object_class = class_drift.object_class;
    require(classify_controller_identity_proof_mismatch(
                controller_proof, pointer_and_class_drift).first_mismatch
                == ControllerIdentityProofMismatch::ObjectPointer,
        "pointer mismatch did not precede class mismatch");
    ControllerIdentityProof class_and_name_drift = class_drift;
    class_and_name_drift.name_comparison_id = name_drift.name_comparison_id;
    require(classify_controller_identity_proof_mismatch(
                controller_proof, class_and_name_drift).first_mismatch
                == ControllerIdentityProofMismatch::ObjectClass,
        "class mismatch did not precede name mismatch");
    ControllerIdentityProof name_and_number_drift = name_drift;
    name_and_number_drift.name_number = name_number_drift.name_number;
    require(classify_controller_identity_proof_mismatch(
                controller_proof, name_and_number_drift).first_mismatch
                == ControllerIdentityProofMismatch::NameComparisonIndex,
        "name comparison-index mismatch did not precede name-number mismatch");
    ControllerIdentityProof number_and_outer_drift = name_number_drift;
    number_and_outer_drift.outer = outer_drift.outer;
    require(classify_controller_identity_proof_mismatch(
                controller_proof, number_and_outer_drift).first_mismatch
                == ControllerIdentityProofMismatch::NameNumber,
        "name-number mismatch did not precede outer mismatch");
    ControllerIdentityProof outer_and_index_drift = index_drift;
    outer_and_index_drift.outer = outer_drift.outer;
    require(classify_controller_identity_proof_mismatch(
                controller_proof, outer_and_index_drift).first_mismatch
                == ControllerIdentityProofMismatch::Outer,
        "outer mismatch did not precede live-index mismatch");
    ControllerIdentityProof index_and_serial_drift = index_drift;
    index_and_serial_drift.live.serial_number = serial_drift.live.serial_number;
    require(classify_controller_identity_proof_mismatch(
                controller_proof, index_and_serial_drift).first_mismatch
                == ControllerIdentityProofMismatch::LiveIndex,
        "live-index mismatch did not precede serial mismatch");
    UnpublishedAudioSetupContext arm_side_invalid{selection, token};
    const bool arm_side_bound =
        arm_side_invalid.bind_route_controller(positive_index_capture_failed);
    const ControllerIdentityProofMismatchReport attempted_arm_report =
        classify_controller_identity_proof_mismatch(
            positive_index_capture_failed, structural_controller_proof);
    require(!arm_side_bound
            && !controller_identity_proof_valid(arm_side_invalid.controller_proof)
            && attempted_arm_report.first_mismatch
                == ControllerIdentityProofMismatch::ExpectedInvalid
            && attempted_arm_report.expected.raw_internal_index_readable
            && attempted_arm_report.expected.raw_internal_index_state
                == ControllerIdentityRawIndexState::Nonnegative
            && !attempted_arm_report.expected.live_capture_succeeded,
        "attempted invalid arm proof facts were lost or became publishable");
    const ControllerIdentityProofMismatchReport structural_facts =
        classify_controller_identity_proof_mismatch(
            structural_controller_proof, structural_controller_proof);
    const ControllerIdentityProofMismatchReport serial_facts =
        classify_controller_identity_proof_mismatch(
            controller_proof, controller_proof);
    require(structural_facts.expected.valid
            && structural_facts.expected.mode == ControllerIdentityProofMode::Structural
            && structural_facts.expected.raw_internal_index_readable
            && structural_facts.expected.raw_internal_index_state
                == ControllerIdentityRawIndexState::Negative
            && !structural_facts.expected.live_capture_succeeded
            && !structural_facts.expected.live_internal_index_valid
            && !structural_facts.expected.live_serial_valid,
        "structural controller proof facts were not bounded and exact");
    require(serial_facts.expected.valid
            && serial_facts.expected.mode
                == ControllerIdentityProofMode::SerialBacked
            && serial_facts.expected.raw_internal_index_readable
            && serial_facts.expected.raw_internal_index_state
                == ControllerIdentityRawIndexState::Nonnegative
            && serial_facts.expected.raw_internal_index
                == controller_handle.internal_index
            && serial_facts.expected.live_capture_succeeded
            && serial_facts.expected.live_internal_index_valid
            && serial_facts.expected.live_serial_valid,
        "serial controller proof facts were not bounded and exact");

    const auto observation_evidence_equal = [](
        const PrivateControllerStopObservationEvidence& left,
        const PrivateControllerStopObservationEvidence& right) {
        return left.context_valid == right.context_valid
            && left.token_unchanged == right.token_unchanged
            && left.stage_awaiting_stop == right.stage_awaiting_stop
            && left.required_context_present == right.required_context_present
            && left.controller_handle_valid == right.controller_handle_valid
            && left.sound_handle_valid == right.sound_handle_valid;
    };
    const auto contexts_field_equivalent = [](
        const UnpublishedAudioSetupContext& left,
        const UnpublishedAudioSetupContext& right) {
        return left.selection.generation == right.selection.generation
            && left.selection.storage == right.selection.storage
            && left.selection.song == right.selection.song
            && left.selection.profile == right.selection.profile
            && left.selection.profile_index == right.selection.profile_index
            && left.selection.visible_index == right.selection.visible_index
            && left.selection.base_slot == right.selection.base_slot
            && left.token == right.token
            && left.controller_stage == right.controller_stage
            && left.controller == right.controller
            && left.slot == right.slot
            && left.bgm == right.bgm
            && left.expected_sound == right.expected_sound
            && left.sound == right.sound
            && left.controller_proof.mode == right.controller_proof.mode
            && left.controller_proof.controller == right.controller_proof.controller
            && left.controller_proof.object_class == right.controller_proof.object_class
            && left.controller_proof.name_comparison_id
                == right.controller_proof.name_comparison_id
            && left.controller_proof.name_number == right.controller_proof.name_number
            && left.controller_proof.outer == right.controller_proof.outer
            && left.controller_proof.raw_internal_index_readable
                == right.controller_proof.raw_internal_index_readable
            && left.controller_proof.raw_internal_index
                == right.controller_proof.raw_internal_index
            && left.controller_proof.live_capture_succeeded
                == right.controller_proof.live_capture_succeeded
            && left.controller_proof.live.internal_index
                == right.controller_proof.live.internal_index
            && left.controller_proof.live.serial_number
                == right.controller_proof.live.serial_number
            && left.controller_proof.item_backed_zero_serial_capture_succeeded
                == right.controller_proof.item_backed_zero_serial_capture_succeeded
            && left.controller_proof.item_backed_zero_serial.internal_index
                == right.controller_proof.item_backed_zero_serial.internal_index
            && left.controller_proof.item_backed_zero_serial.serial_number
                == right.controller_proof.item_backed_zero_serial.serial_number
            && left.expected_sound_handle.internal_index
                == right.expected_sound_handle.internal_index
            && left.expected_sound_handle.serial_number
                == right.expected_sound_handle.serial_number
            && left.request_before_set == right.request_before_set
            && left.request_after_set == right.request_after_set
            && left.state_after_set == right.state_after_set
            && left.custom_patch_applied == right.custom_patch_applied
            && left.custom_patch_restored == right.custom_patch_restored;
    };
    enum class DirectObservationFailure {
        Context,
        Token,
        Stage,
        RequiredContext,
        ControllerHandle,
        SoundHandle,
    };
    struct DirectObservationCase {
        DirectObservationFailure failure;
        PrivateControllerStopObservationDiagnostic expected;
        const char* message;
    };
    const DirectObservationCase direct_observation_cases[] = {
        {DirectObservationFailure::Context,
            {{rejected, not_evaluated, not_evaluated, not_evaluated,
                 not_evaluated, not_evaluated},
                PrivateControllerStopObservationResult::ContextInvalid},
            "production Stop observation did not short-circuit invalid context"},
        {DirectObservationFailure::Token,
            {{accepted, rejected, not_evaluated, not_evaluated,
                 not_evaluated, not_evaluated},
                PrivateControllerStopObservationResult::TokenChanged},
            "production Stop observation did not short-circuit token drift"},
        {DirectObservationFailure::Stage,
            {{accepted, accepted, rejected, not_evaluated,
                 not_evaluated, not_evaluated},
                PrivateControllerStopObservationResult::StageChanged},
            "production Stop observation did not short-circuit stage drift"},
        {DirectObservationFailure::RequiredContext,
            {{accepted, accepted, accepted, rejected,
                 not_evaluated, not_evaluated},
                PrivateControllerStopObservationResult::RequiredContextMissing},
            "production Stop observation did not short-circuit missing context"},
        {DirectObservationFailure::ControllerHandle,
            {{accepted, accepted, accepted, accepted, rejected, not_evaluated},
                PrivateControllerStopObservationResult::ControllerHandleInvalid},
            "production Stop observation did not short-circuit controller identity"},
        {DirectObservationFailure::SoundHandle,
            {{accepted, accepted, accepted, accepted, accepted, rejected},
                PrivateControllerStopObservationResult::SoundHandleInvalid},
            "production Stop observation did not reject sound identity"},
    };
    for (const DirectObservationCase& observation_case : direct_observation_cases) {
        UnpublishedAudioSetupContext setup{selection, token};
        require(setup.bind_route_controller(controller_proof),
            "direct Stop fixture did not bind the route controller proof");
        CustomContextToken expected_token = token;
        void* expected_controller = controller;
        ControllerIdentityProof current_controller_proof = controller_proof;
        UObjectLiveHandle expected_sound_handle = sound_handle;
        switch (observation_case.failure) {
        case DirectObservationFailure::Context:
            setup = {};
            break;
        case DirectObservationFailure::Token:
            ++expected_token.route_generation;
            break;
        case DirectObservationFailure::Stage:
            setup.controller_stage = PrivateControllerSetupStage::StopObserved;
            break;
        case DirectObservationFailure::RequiredContext:
            expected_controller = nullptr;
            break;
        case DirectObservationFailure::ControllerHandle:
            ++current_controller_proof.live.serial_number;
            break;
        case DirectObservationFailure::SoundHandle:
            expected_sound_handle.serial_number = 0;
            break;
        }
        const UnpublishedAudioSetupContext before = setup;
        PrivateControllerStopObservationDiagnostic diagnostic;
        require(!setup.observe_controller_stop(
                    expected_token, expected_controller, current_controller_proof,
                    slot, bgm, sound, expected_sound_handle, 0x1008, &diagnostic),
            observation_case.message);
        require(diagnostic.result == observation_case.expected.result
                && observation_evidence_equal(
                    diagnostic.evidence, observation_case.expected.evidence),
            "production Stop observation returned incorrect diagnostic facts");
        require(contexts_field_equivalent(setup, before),
            "rejected production Stop observation mutated unpublished context");
        require(!publish_private_controller_claim_if_proven(publication_registry, setup)
                && !publication_registry.playback_snapshot().song,
            "rejected production Stop observation published controller ownership");
    }

    UnpublishedAudioSetupContext production_seam{selection, token};
    require(production_seam.bind_route_controller(structural_controller_proof),
        "production Stop seam did not bind a structural controller proof");
    PrivateControllerStopObservationDiagnostic production_diagnostic;
    require(production_seam.observe_controller_stop(
                token, controller, structural_controller_proof,
                slot, bgm, sound, sound_handle,
                0x1008, &production_diagnostic)
            && production_diagnostic.result
                == PrivateControllerStopObservationResult::Accepted
            && observation_evidence_equal(
                production_diagnostic.evidence, observation_accepted),
        "production Stop diagnostic seam disagreed with all-true pure evidence");
}

void test_identity_prefilter_and_deferred_emission()
{
    using namespace ff7r::piano::game;

    int validations = 0;
    int reads = 0;
    int comparisons = 0;
    const UObjectIdentityPrefilterResult validation_failure =
        evaluate_uobject_identity_prefilter(
            true,
            [&] { ++validations; return false; },
            [&] { ++reads; return true; },
            [&] { ++comparisons; return UObjectIdentityPrefilterResult::Passed; });
    require(validation_failure
                == UObjectIdentityPrefilterResult::ExpectedLiveHandleValidationFailed
            && validations == 1 && reads == 0 && comparisons == 0,
        "identity prefilter retried or read after exact validation failure");

    validations = 0;
    reads = 0;
    comparisons = 0;
    const UObjectIdentityPrefilterResult read_failure =
        evaluate_uobject_identity_prefilter(
            true,
            [&] { ++validations; return true; },
            [&] { ++reads; return false; },
            [&] { ++comparisons; return UObjectIdentityPrefilterResult::Passed; });
    require(read_failure == UObjectIdentityPrefilterResult::CurrentIdentityReadFailed
            && validations == 1 && reads == 1 && comparisons == 0,
        "identity prefilter retried or compared after exact read failure");

    validations = 0;
    reads = 0;
    comparisons = 0;
    const UObjectIdentityPrefilterResult mismatch =
        evaluate_uobject_identity_prefilter(
            true,
            [&] { ++validations; return true; },
            [&] { ++reads; return true; },
            [&] {
                ++comparisons;
                return UObjectIdentityPrefilterResult::CurrentLiveHandleChanged;
            });
    require(mismatch == UObjectIdentityPrefilterResult::CurrentLiveHandleChanged
            && validations == 1 && reads == 1 && comparisons == 1,
        "identity prefilter did not preserve one validation/read attempt");

    struct OperationLockProbe {
        explicit OperationLockProbe(bool& held) : held_(held) { held_ = true; }
        ~OperationLockProbe() { held_ = false; }
        bool& held_;
    };
    bool operation_lock_held = false;
    bool emitted = false;
    bool emitted_under_operation_lock = true;
    const auto run_deferred = [&](const bool lifecycle_safe) {
        auto deferred = make_deferred_noexcept_action([&] {
            emitted = true;
            emitted_under_operation_lock = operation_lock_held;
        });
        OperationLockProbe operation_lock(operation_lock_held);
        if (!lifecycle_safe) return;
        deferred.make_eligible();
    };
    run_deferred(false);
    require(!emitted,
        "deferred diagnostic emitted before lifecycle reached its safe point");
    run_deferred(true);
    require(emitted && !emitted_under_operation_lock && !operation_lock_held,
        "deferred diagnostic emitted before route-operation lock release");
}

void test_shutdown_aggregation()
{
    HookShutdownResult failed = successful_result();
    failed.callbacks_drained = false;
    require(ff7r::piano::core::aggregate_shutdown_results({successful_result()}),
        "successful shutdown aggregation failed");
    require(!ff7r::piano::core::aggregate_shutdown_results({successful_result(), failed}),
        "failed module omitted from shutdown aggregation");
}

void test_immutable_publication_and_capture_identity()
{
    using namespace ff7r::piano::game;
    auto first_mutable = std::make_shared<ScoreInfoOverlayRow>();
    std::shared_ptr<const ScoreInfoOverlayRow> first = first_mutable;
    auto second_mutable = std::make_shared<ScoreInfoOverlayRow>();
    second_mutable->difficulty = 6;
    std::shared_ptr<const ScoreInfoOverlayRow> second = second_mutable;
    require(first->difficulty == 1 && second->difficulty == 6,
        "ScoreInfo generation mutated an already published row");
    require(!scoreinfo_overlay_publishable(false, *second),
        "unknown ScoreInfo caller was publishable");
    require(!scoreinfo_overlay_publishable(true, *second),
        "failed FName construction was publishable");
    require(title_converter_source_allowed(true, false)
            && title_converter_source_allowed(false, true)
            && !title_converter_source_allowed(false, false),
        "title converter source policy rejected a validated overlay/render scope");
    SongDescriptor activation_song;
    activation_song.id = "activation-title";
    activation_song.title = L"Activation";
    activation_song.visible_index = 6;
    activation_song.profiles.push_back({});
    activation_song.profiles[0].title = L"Activation Profile";
    const auto activation_storage = std::make_shared<int>(7);
    SelectionSnapshot activation_selection;
    activation_selection.generation = 41;
    activation_selection.storage = activation_storage;
    activation_selection.song = &activation_song;
    activation_selection.profile = &activation_song.profiles[0];
    activation_selection.profile_index = 0;
    activation_selection.visible_index = 6;
    activation_selection.base_slot = 2;
    const PlaybackSnapshot no_playback;
    const auto activation_title = activation_selection_title_facts(
        true, false, false, activation_selection, activation_selection,
        no_playback);
    require(first_activation_selection_title_failure(activation_title)
            == ActivationSelectionTitleFailure::None,
        "stable custom activation selection title was rejected");
    require(title_converter_activation_return_exact(0x03999ce1, 0x03999ce1)
            && !title_converter_activation_return_exact(0x03999ce0, 0x03999ce1),
        "activation title return-site filter accepted another caller");
    auto other_caller = activation_title;
    other_caller.caller_exact = false;
    require(first_activation_selection_title_failure(other_caller)
            == ActivationSelectionTitleFailure::CallerMismatch,
        "non-cataloged converter caller entered activation title mode");
    auto overlay_precedence = activation_title;
    overlay_precedence.overlay_absent = false;
    require(first_activation_selection_title_failure(overlay_precedence)
            == ActivationSelectionTitleFailure::OverlayPrecedence,
        "activation title bypassed overlay precedence");
    auto render_precedence = activation_title;
    render_precedence.render_context_absent = false;
    require(first_activation_selection_title_failure(render_precedence)
            == ActivationSelectionTitleFailure::RenderPrecedence,
        "activation title bypassed list/render precedence");
    SelectionSnapshot vanilla_selection;
    require(first_activation_selection_title_failure(
                activation_selection_title_facts(
                    true, false, false, vanilla_selection,
                    vanilla_selection, no_playback))
            == ActivationSelectionTitleFailure::SelectionSongMissing,
        "vanilla or absent selection entered activation title mode");
    auto missing_profile = activation_selection;
    missing_profile.profile = nullptr;
    require(first_activation_selection_title_failure(
                activation_selection_title_facts(
                    true, false, false, missing_profile,
                    missing_profile, no_playback))
            == ActivationSelectionTitleFailure::SelectionProfileMissing,
        "custom selection without a profile entered activation title mode");
    auto stale_generation = activation_selection;
    ++stale_generation.generation;
    require(first_activation_selection_title_failure(
                activation_selection_title_facts(
                    true, false, false, activation_selection,
                    stale_generation, no_playback))
            == ActivationSelectionTitleFailure::SelectionGenerationDrift,
        "stale activation selection generation was accepted");
    SongDescriptor other_song;
    other_song.profiles.push_back({});
    auto stale_song = activation_selection;
    stale_song.song = &other_song;
    require(first_activation_selection_title_failure(
                activation_selection_title_facts(
                    true, false, false, activation_selection,
                    stale_song, no_playback))
            == ActivationSelectionTitleFailure::SelectionSongDrift,
        "stale activation song identity was accepted");
    auto stale_profile = activation_selection;
    stale_profile.profile = &other_song.profiles[0];
    require(first_activation_selection_title_failure(
                activation_selection_title_facts(
                    true, false, false, activation_selection,
                    stale_profile, no_playback))
            == ActivationSelectionTitleFailure::SelectionProfileDrift,
        "stale activation profile identity was accepted");
    auto stale_storage = activation_selection;
    stale_storage.storage = std::make_shared<int>(8);
    require(first_activation_selection_title_failure(
                activation_selection_title_facts(
                    true, false, false, activation_selection,
                    stale_storage, no_playback))
            == ActivationSelectionTitleFailure::SelectionStorageDrift,
        "stale retained registry storage was accepted");
    PlaybackSnapshot conflicting_playback;
    static_cast<SelectionSnapshot&>(conflicting_playback) = stale_song;
    conflicting_playback.token = {41, 5, 7, 11,
        reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000),
        reinterpret_cast<void*>(0x3000), reinterpret_cast<void*>(0x4000), 8};
    require(first_activation_selection_title_failure(
                activation_selection_title_facts(
                    true, false, false, activation_selection,
                    activation_selection, conflicting_playback))
            == ActivationSelectionTitleFailure::PlaybackConflict,
        "conflicting playback identity did not block pre-play title mode");
    PlaybackSnapshot matching_playback;
    static_cast<SelectionSnapshot&>(matching_playback) = activation_selection;
    matching_playback.token = {41, 5, 7, 11,
        reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000),
        reinterpret_cast<void*>(0x3000), reinterpret_cast<void*>(0x4000), 8};
    require(first_activation_selection_title_failure(
                activation_selection_title_facts(
                    true, false, false, activation_selection,
                    activation_selection, matching_playback))
            == ActivationSelectionTitleFailure::PlaybackConflict,
        "published matching playback entered pre-publication title mode");
    TitleConverterOriginalCallState original_call_state;
    int original_calls = 0;
    const int original_result = call_title_converter_original_exact_once(
        original_call_state, [&] {
            ++original_calls;
            return 17;
        });
    require(original_result == 17 && original_calls == 1
            && original_call_state.call_count == 1,
        "title converter original was not called exactly once");
    SongRegistry activation_registry;
    activation_registry.replace({activation_song});
    activation_registry.set_active_selection(6, 2);
    const SelectionSnapshot registry_selection =
        activation_registry.selection_snapshot();
    SelectionSnapshot observed_selection;
    PlaybackSnapshot observed_playback;
    int selection_commits = 0;
    require(activation_registry.commit_if_selection_snapshot(
                registry_selection, observed_selection, observed_playback,
                [&] {
                    ++selection_commits;
                    return true;
                })
            && selection_commits == 1
            && observed_selection.generation == registry_selection.generation
            && observed_selection.storage == registry_selection.storage
            && observed_selection.song == registry_selection.song
            && observed_selection.profile == registry_selection.profile,
        "exact selection was not retained through title commit");
    auto stale_registry_selection = registry_selection;
    ++stale_registry_selection.generation;
    require(!activation_registry.commit_if_selection_snapshot(
                stale_registry_selection, observed_selection, observed_playback,
                [&] {
                    ++selection_commits;
                    return true;
                })
            && selection_commits == 1,
        "stale selection reached atomic title commit");

    RetainedChartOwnerObservation owner_observation;
    owner_observation.owner = reinterpret_cast<void*>(0x1000);
    owner_observation.owner_observed = true;
    owner_observation.owner_identity_valid = false;
    void* const first_chart = reinterpret_cast<void*>(0x2000);
    require(bind_retained_chart_owner_observation(owner_observation, first_chart, 7)
            && owner_observation.chart == first_chart
            && owner_observation.registry_generation == 7
            && owner_observation.chart_read_succeeded,
        "late-created owner chart did not bind to its retained observation");
    require(bind_retained_chart_owner_observation(owner_observation, first_chart, 8)
            && owner_observation.registry_generation == 7,
        "stable retained owner chart was rejected");
    require(!bind_retained_chart_owner_observation(
                owner_observation, reinterpret_cast<void*>(0x3000), 8)
            && !owner_observation.chart_read_succeeded,
        "retained owner observation accepted chart pointer reuse");

    std::array<uint8_t, ScoreInfoOverlayRow::kRowSize> source_row{};
    const FNameValue native_bgm{1234, 0};
    std::memcpy(source_row.data() + kScoreInfoBgmNameOffset, &native_bgm, sizeof(native_bgm));
    ScoreInfoOverlayRow fallback;
    fallback.bgm_name = scoreinfo_source_bgm_name(source_row.data(), source_row.size());
    require(fallback.bgm_name.comparison_id == native_bgm.comparison_id
            && scoreinfo_overlay_publishable(true, fallback),
        "valid source ScoreInfo BGM name was not retained when alias lookup failed");

    CompletionCapture capture;
    capture.wrapper = reinterpret_cast<void*>(0x1000);
    capture.wrapper_identity = {10, 20};
    capture.wrapper_identity_valid = true;
    capture.owner = reinterpret_cast<void*>(0x2000);
    capture.owner_identity = {12, 34};
    capture.owner_generation = 5;
    capture.registry_generation = 7;
    capture.song_id = "song";
    capture.profile_index = 2;
    capture.difficulty = 4;
    capture.descriptor_hash = 0x1234;
    capture.target_seconds = 42.0f;
    capture.playback_token = {7, 11, 13, 17,
        reinterpret_cast<void*>(0x3000), reinterpret_cast<void*>(0x3100),
        reinterpret_cast<void*>(0x3200), reinterpret_cast<void*>(0x3300), 0x400010008ull};
    CompletionCaptureIdentity current;
    current.wrapper = capture.wrapper;
    current.wrapper_identity = capture.wrapper_identity;
    current.wrapper_identity_valid = true;
    current.owner = capture.owner;
    current.owner_identity = capture.owner_identity;
    current.owner_generation = 5;
    current.owner_registry_generation = 7;
    current.registry_generation = 7;
    current.song_id = "song";
    current.profile_index = 2;
    current.difficulty = 4;
    current.descriptor_hash = capture.descriptor_hash;
    current.target_seconds = capture.target_seconds;
    current.playback_token = capture.playback_token;
    require(completion_capture_matches(capture, current), "exact completion capture was rejected");
    current.owner_registry_generation = 6;
    require(!completion_capture_matches(capture, current),
        "stale retained-owner registry generation was rebound to current capture");
    current.owner_registry_generation = 7;
    current.registry_generation = 8;
    require(!completion_capture_matches(capture, current),
        "wrapper address reuse across registry generations was accepted");
    current.registry_generation = 7;
    current.wrapper_identity.serial_number = 21;
    require(!completion_capture_matches(capture, current),
        "wrapper pointer reuse with a new UObject serial was accepted");
    current.wrapper_identity = capture.wrapper_identity;
    capture.wrapper_identity_valid = false;
    current.wrapper_identity_valid = false;
    require(completion_capture_matches(capture, current),
        "non-UObject chart wrapper was rejected despite exact generation-bound identity");
    capture.wrapper_identity_valid = true;
    current.wrapper_identity_valid = true;
    current.profile_index = 3;
    require(!completion_capture_matches(capture, current),
        "current-selection drift was accepted");
    current.profile_index = 2;
    current.descriptor_hash ^= 1;
    require(!completion_capture_matches(capture, current), "descriptor hash drift was accepted");
    current.descriptor_hash = capture.descriptor_hash;
    current.target_seconds += 1.0f;
    require(!completion_capture_matches(capture, current), "completion target drift was accepted");
    current.target_seconds = capture.target_seconds;
    ++current.playback_token.route_generation;
    require(!completion_capture_matches(capture, current),
        "completion capture accepted stale exact playback token");
}

} // namespace ff7r::piano::tests::runtime_lifecycle
