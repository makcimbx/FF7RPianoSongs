#include "game/title.h"

#include "core/hooks.h"
#include "game/audio_sead.h"
#include "game/completion_timing.h"
#include "game/module_hooks.h"
#include "game/hook_specs.h"
#include "game/rvas.h"
#include "game/runtime_layouts.h"
#include "game/scoreinfo_overlay.h"
#include "game/ue_types.h"

#include "core/logging.h"
#include "core/pe_image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <sstream>
#include <vector>

#include <Windows.h>
#include <Xinput.h>
#include <hidsdi.h>

namespace ff7r::piano::game {
namespace {

thread_local TitleResolverStack g_title_resolver_tokens;

using XInputGetStateFn = DWORD(WINAPI*)(DWORD user_index, XINPUT_STATE* state);
using FNameCtorFn = void*(__fastcall*)(FNameValue* out_name, const wchar_t* text, int32_t find_type);
using PlayerInputInputKeyFn = bool(__fastcall*)(void* player_input, const void* key, int32_t event_type, float amount, bool gamepad);

std::once_flag g_xinput_resolve_once;
std::vector<XInputGetStateFn> g_xinput_get_states;
DifficultyInputMergeState g_difficulty_input_state;
DifficultyEventProviderState g_difficulty_event_providers;
bool g_difficulty_input_eligible = false;
std::atomic_bool g_difficulty_input_thread_running{false};
HANDLE g_difficulty_input_thread = nullptr;
std::atomic_bool g_raw_hid_logged{false};
std::atomic_uint32_t g_native_dpad_left_name{0};
std::atomic_uint32_t g_native_dpad_right_name{0};
std::atomic_bool g_native_dpad_logged{false};
constexpr std::array<const wchar_t*, 8> kNativePianoDirectionNames{
    L"Gamepad_LeftStick_Up", L"Gamepad_LeftStick_Down", L"Gamepad_LeftStick_Left", L"Gamepad_LeftStick_Right",
    L"Gamepad_RightStick_Up", L"Gamepad_RightStick_Down", L"Gamepad_RightStick_Left", L"Gamepad_RightStick_Right",
};
constexpr std::array<const char*, 8> kNativePianoDirectionLabels{
    "left_up", "left_down", "left_left", "left_right",
    "right_up", "right_down", "right_left", "right_right",
};
std::array<std::atomic_uint32_t, 8> g_native_piano_direction_names{};
NativePianoInputDiagnosticAdmission g_native_piano_input_diagnostic_admission;
DiagnosticEmissionBudget g_difficulty_input_diagnostic_budget{kDifficultyInputDiagnosticLimit};
constexpr UINT kDifficultyUiRefreshMessage = WM_APP + 0x4f7;
HWND g_game_window = nullptr;
WNDPROC g_previous_window_proc = nullptr;
std::atomic<void*> g_last_title_population_context{nullptr};

bool install_difficulty_window_hook();
void apply_detail_title(void* context, const char* source);

using PianoDetailUpdateFn = void(__fastcall*)(void* context);
using PianoTitlePopulationFn = uint8_t(__fastcall*)(void* context);
using TitleViewConverterFn = uint64_t*(__fastcall*)(void* wrapper, uint64_t* out_view, void* arg3);
using SetStringTextFn = uintptr_t(__fastcall*)(void* text_widget, void* text);

core::RawRvaHook g_detail_update_hook;
core::RawRvaHook g_title_population_hook;
core::RawRvaHook g_title_converter_hook;
core::RawRvaHook g_player_input_key_hook;
PianoDetailUpdateFn g_original_detail_update = nullptr;
PianoTitlePopulationFn g_original_title_population = nullptr;
TitleViewConverterFn g_original_title_converter = nullptr;
PlayerInputInputKeyFn g_original_player_input_key = nullptr;

std::mutex g_title_text_mutex;
OwnedTitleText g_live_title;
HMODULE g_exe_module = nullptr;

using FStringView = runtime_layouts::FString;

bool install_named_hook(const HookInstallContext& context, const char* name, core::RawRvaHook& hook, void* detour, void** original)
{
    const HookSpec* spec = find_hook_spec(name);
    if (!spec) {
        std::ostringstream out;
        out << "[title] status=install_failed hook=" << name << " error=missing_hook_spec";
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    std::string error;
    if (!hook.install(context.exe_module, spec->rva, spec->expected_prologue, detour, original, error)) {
        std::ostringstream out;
        out << "[title] status=install_failed hook=" << name << " error=" << error;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    std::ostringstream out;
    out << "[title] status=installed hook=" << name << " rva=0x" << std::hex << spec->rva;
    core::log(core::LogLevel::Info, out.str());
    return true;
}

bool construct_fname_id(const wchar_t* text, uint32_t& comparison_id)
{
    comparison_id = 0;
    if (!g_exe_module || !text) {
        return false;
    }
    FNameValue name{};
    const auto fname_ctor = reinterpret_cast<FNameCtorFn>(
        reinterpret_cast<uintptr_t>(g_exe_module) + rva::FNameCtor);
    __try {
        fname_ctor(&name, text, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    comparison_id = name.comparison_id;
    return comparison_id != 0;
}

bool resolve_native_dpad_names()
{
    if (g_native_dpad_left_name.load(std::memory_order_acquire)
        && g_native_dpad_right_name.load(std::memory_order_acquire)) {
        return true;
    }
    uint32_t left = 0;
    uint32_t right = 0;
    if (!construct_fname_id(L"Gamepad_DPad_Left", left)
        || !construct_fname_id(L"Gamepad_DPad_Right", right)) {
        return false;
    }
    g_native_dpad_left_name.store(left, std::memory_order_release);
    g_native_dpad_right_name.store(right, std::memory_order_release);
    for (size_t index = 0; index < kNativePianoDirectionNames.size(); ++index) {
        if (g_native_piano_direction_names[index].load(std::memory_order_acquire) != 0) {
            continue;
        }
        uint32_t direction = 0;
        if (construct_fname_id(kNativePianoDirectionNames[index], direction)) {
            g_native_piano_direction_names[index].store(direction, std::memory_order_release);
        }
    }
    return true;
}

void log_native_piano_press(
    size_t direction_index, float amount, bool gamepad, const double elapsed)
{
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const SongDescriptor* song = playback.song;
    const SongDifficultyProfile* profile = playback.profile;

    float nearest = -1.0f;
    float nearest_delta = std::numeric_limits<float>::infinity();
    float previous = -1.0f;
    float next = -1.0f;
    const SongChartNote* nearest_note = nullptr;
    if (profile) {
        for (const SongChartNote& note : profile->chart_notes) {
            float prompt = 0.0f;
            if (!chart_time_seconds(note.time_str, prompt)) {
                continue;
            }
            const float delta = std::fabs(prompt - static_cast<float>(elapsed));
            if (delta < nearest_delta) {
                nearest_delta = delta;
                nearest = prompt;
                nearest_note = &note;
            }
            if (prompt <= elapsed && prompt > previous) {
                previous = prompt;
            }
            if (prompt >= elapsed && (next < 0.0f || prompt < next)) {
                next = prompt;
            }
        }
    }

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << "[input_diag] action=press direction=" << kNativePianoDirectionLabels[direction_index]
        << " amount=" << amount
        << " gamepad=" << (gamepad ? 1 : 0)
        << " clock_valid=1";
    if (song) {
        out << " song_id=" << song->id;
    }
    if (profile) {
        out << " difficulty=" << profile->difficulty;
    }
    out << " playback_seconds=" << elapsed;
    if (nearest >= 0.0f) {
        out << " nearest_prompt_seconds=" << nearest
            << " nearest_delta_ms=" << (static_cast<double>(nearest) - elapsed) * 1000.0;
        if (nearest_note) {
            out << " nearest_monotone=" << (nearest_note->monotone_id.empty() ? "none" : nearest_note->monotone_id)
                << " nearest_chord=" << (nearest_note->chord_id.empty() ? "none" : nearest_note->chord_id);
        }
    }
    if (previous >= 0.0f) {
        out << " previous_prompt_seconds=" << previous;
    }
    if (next >= 0.0f) {
        out << " next_prompt_seconds=" << next;
    }
    core::log(core::LogLevel::Info, out.str());
}

bool __fastcall player_input_key_detour(void* player_input, const void* key, int32_t event_type, float amount, bool gamepad)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!callback) {
        return g_original_player_input_key
            ? g_original_player_input_key(player_input, key, event_type, amount, gamepad) : false;
    }
    if ((event_type == 0 || event_type == 1 || event_type == 2) && key) {
        FNameValue key_name{};
        if (core::safe_copy_bytes(key, &key_name, sizeof(key_name))) {
            const uint32_t comparison_id = key_name.comparison_id;
            if (comparison_id == g_native_dpad_left_name.load(std::memory_order_acquire)) {
                g_difficulty_event_providers.update_player_input(true, event_type);
            } else if (comparison_id == g_native_dpad_right_name.load(std::memory_order_acquire)) {
                g_difficulty_event_providers.update_player_input(false, event_type);
            }
            for (size_t index = 0; index < g_native_piano_direction_names.size(); ++index) {
                if (comparison_id == g_native_piano_direction_names[index].load(std::memory_order_acquire)) {
                    run_native_piano_input_diagnostic(
                        g_native_piano_input_diagnostic_admission, index, event_type,
                        [](double& elapsed) { return custom_audio_playback_clock(elapsed); },
                        [&](const double elapsed) {
                            log_native_piano_press(index, amount, gamepad, elapsed);
                        },
                        [] {
                            core::log(core::LogLevel::Info,
                                "[input_diag] status=budget_exhausted limit="
                                + std::to_string(kNativePianoInputDiagnosticLimit));
                        });
                    break;
                }
            }
            if ((comparison_id == g_native_dpad_left_name.load(std::memory_order_relaxed)
                    || comparison_id == g_native_dpad_right_name.load(std::memory_order_relaxed))
                && !g_native_dpad_logged.exchange(true, std::memory_order_acq_rel)) {
                core::log(core::LogLevel::Info,
                    "[title] difficulty_input status=native_dpad_ready source=player_input_input_key");
            }
        }
    }
    return g_original_player_input_key
        ? g_original_player_input_key(player_input, key, event_type, amount, gamepad)
        : false;
}

SetStringTextFn resolve_set_string_text_helper()
{
    if (!g_exe_module) {
        return nullptr;
    }
    const RvaSignatureSpec* spec = find_rva_signature("set_string_text");
    auto* address = reinterpret_cast<uint8_t*>(g_exe_module) + rva::SetStringText;
    if (!spec || spec->rva != rva::SetStringText
        || !core::bytes_equal(address, spec->expected_prologue)) {
        static std::atomic_bool s_logged{false};
        if (!s_logged.exchange(true, std::memory_order_relaxed)) {
            std::ostringstream out;
            out << "[title] detail_title_helper status=disabled reason="
                << (spec ? "prologue_mismatch" : "missing_hook_spec")
                << " rva=0x" << std::hex << rva::SetStringText;
            core::log(core::LogLevel::Error, out.str());
        }
        return nullptr;
    }
    return reinterpret_cast<SetStringTextFn>(address);
}

FStringView make_fstring_view(const OwnedTitleText& title)
{
    const int32_t num_with_null = static_cast<int32_t>(title.storage.size() + 1);
    return {title.storage.c_str(), num_with_null, num_with_null};
}

void resolve_xinput_get_state()
{
    constexpr const wchar_t* kModuleNames[]{
        L"xinput1_3.dll",
        L"xinput1_4.dll",
        L"xinput9_1_0.dll",
    };
    for (const wchar_t* module_name : kModuleNames) {
        HMODULE module = GetModuleHandleW(module_name);
        if (module) {
            auto get_state = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
            if (get_state && std::find(g_xinput_get_states.begin(), g_xinput_get_states.end(), get_state) == g_xinput_get_states.end()) {
                g_xinput_get_states.push_back(get_state);
            }
        }
    }
    if (!g_xinput_get_states.empty()) {
        std::ostringstream out;
        out << "[title] difficulty_input status=xinput_ready providers=" << g_xinput_get_states.size();
        core::log(core::LogLevel::Info, out.str());
        return;
    }
    core::log(core::LogLevel::Info, "[title] difficulty_input status=xinput_unavailable keyboard=enabled");
}

DifficultyInputProviderFacts sample_difficulty_input()
{
    DifficultyInputProviderFacts state = g_difficulty_event_providers.snapshot();
    state.decrement.keyboard_held = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    state.increment.keyboard_held = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
    state.decrement.async_gamepad_held =
        (GetAsyncKeyState(VK_GAMEPAD_DPAD_LEFT) & 0x8000) != 0;
    state.increment.async_gamepad_held =
        (GetAsyncKeyState(VK_GAMEPAD_DPAD_RIGHT) & 0x8000) != 0;

    std::call_once(g_xinput_resolve_once, resolve_xinput_get_state);
    for (const auto get_state : g_xinput_get_states) {
        for (DWORD user_index = 0; user_index < XUSER_MAX_COUNT; ++user_index) {
            XINPUT_STATE xinput_state{};
            if (get_state(user_index, &xinput_state) == ERROR_SUCCESS) {
                state.decrement.xinput_held = state.decrement.xinput_held
                    || (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
                state.increment.xinput_held = state.increment.xinput_held
                    || (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
            }
        }
    }
    return state;
}

void poll_dynamic_difficulty_input_impl()
{
    const DifficultyInputProviderFacts current = sample_difficulty_input();
    const DifficultyInputEdges edges = g_difficulty_input_state.update(current);
    const bool decrement_edge = edges.decrement;
    const bool increment_edge = edges.increment;
    const bool keyboard_edge = (decrement_edge && current.decrement.keyboard_held)
        || (increment_edge && current.increment.keyboard_held);
    const bool gamepad_edge = (decrement_edge && !current.decrement.keyboard_held)
        || (increment_edge && !current.increment.keyboard_held);

    if (decrement_edge || increment_edge) {
        switch (g_difficulty_input_diagnostic_budget.claim()) {
        case DiagnosticEmissionAdmission::Emit: {
            const SelectionSnapshot selection = registry().selection_snapshot();
            const SongDescriptor* song = selection.song;
            std::ostringstream out;
            out << "[title] difficulty_input status=edge"
                << " eligible=" << (selection ? 1 : 0)
                << " decrement=" << (decrement_edge ? 1 : 0)
                << " increment=" << (increment_edge ? 1 : 0)
                << " keyboard=" << (keyboard_edge ? 1 : 0)
                << " gamepad=" << (gamepad_edge ? 1 : 0)
                << " song_id=" << (song ? song->id : "");
            core::log(core::LogLevel::Info, out.str());
            break;
        }
        case DiagnosticEmissionAdmission::EmitExhaustionMarker:
            core::log(core::LogLevel::Info,
                "[title] difficulty_input status=diagnostic_budget_exhausted limit="
                + std::to_string(kDifficultyInputDiagnosticLimit));
            break;
        case DiagnosticEmissionAdmission::Rejected:
            break;
        }
    }
    if (decrement_edge != increment_edge) {
        ProfileListCallbacks callbacks = make_profile_list_callbacks();
        (void)profile_list_coordinator().submit_edge(
            decrement_edge ? -1 : 1, callbacks);
    }
}

DWORD WINAPI difficulty_input_thread_main(void*)
{
    core::log(core::LogLevel::Info, "[title] difficulty_input status=poller_started interval_ms=16");
    uint32_t window_retry_ticks = 0;
    uint32_t native_name_retry_ticks = 0;
    while (g_difficulty_input_thread_running.load(std::memory_order_acquire)) {
        run_difficulty_input_poller_iteration(
            non_audio_hook_gate(),
            [&] {
                if (!g_game_window && (window_retry_ticks++ % 60) == 0) {
                    (void)install_difficulty_window_hook();
                }
                if ((!g_native_dpad_left_name.load(std::memory_order_acquire)
                        || !g_native_dpad_right_name.load(std::memory_order_acquire))
                    && (native_name_retry_ticks++ % 60) == 0) {
                    (void)resolve_native_dpad_names();
                }
                poll_dynamic_difficulty_input_impl();
            },
            [] { Sleep(16); });
    }
    core::log(core::LogLevel::Info, "[title] difficulty_input status=poller_stopped");
    return 0;
}

bool start_difficulty_input_thread()
{
    if (g_difficulty_input_thread) {
        return true;
    }
    g_difficulty_input_thread_running.store(true, std::memory_order_release);
    g_difficulty_input_thread = CreateThread(nullptr, 0, difficulty_input_thread_main, nullptr, 0, nullptr);
    if (!g_difficulty_input_thread) {
        g_difficulty_input_thread_running.store(false, std::memory_order_release);
        core::log(core::LogLevel::Error, "[title] difficulty_input status=poller_start_failed");
        return false;
    }
    return true;
}

bool stop_difficulty_input_thread()
{
    g_difficulty_input_thread_running.store(false, std::memory_order_release);
    if (!g_difficulty_input_thread) {
        return true;
    }
    const DWORD wait = WaitForSingleObject(g_difficulty_input_thread, 2000);
    if (wait != WAIT_OBJECT_0) {
        core::log(core::LogLevel::Error, "[title] difficulty_input status=poller_stop_timeout handle_retained=1");
        return false;
    }
    CloseHandle(g_difficulty_input_thread);
    g_difficulty_input_thread = nullptr;
    return true;
}

BOOL CALLBACK find_game_window(HWND window, LPARAM parameter)
{
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == GetCurrentProcessId() && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }
    return TRUE;
}

void update_raw_hid_dpad(LPARAM lparam)
{
    UINT raw_size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, nullptr, &raw_size, sizeof(RAWINPUTHEADER)) != 0
        || raw_size < sizeof(RAWINPUTHEADER)) {
        return;
    }

    std::vector<uint8_t> raw_storage(raw_size);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, raw_storage.data(), &raw_size, sizeof(RAWINPUTHEADER)) != raw_size) {
        return;
    }
    const auto* raw = reinterpret_cast<const RAWINPUT*>(raw_storage.data());
    if (raw->header.dwType != RIM_TYPEHID || raw->data.hid.dwSizeHid == 0 || raw->data.hid.dwCount == 0) {
        return;
    }

    UINT preparsed_size = 0;
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, nullptr, &preparsed_size) == static_cast<UINT>(-1)
        || preparsed_size == 0) {
        return;
    }
    std::vector<uint8_t> preparsed_storage(preparsed_size);
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, preparsed_storage.data(), &preparsed_size) == static_cast<UINT>(-1)) {
        return;
    }
    auto* preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed_storage.data());

    bool found_hat = false;
    bool left = false;
    bool right = false;
    for (DWORD report_index = 0; report_index < raw->data.hid.dwCount; ++report_index) {
        auto* report = reinterpret_cast<PCHAR>(const_cast<BYTE*>(raw->data.hid.bRawData)
            + report_index * raw->data.hid.dwSizeHid);
        ULONG hat = 0;
        if (HidP_GetUsageValue(HidP_Input, 0x01, 0, 0x39, &hat, preparsed, report, raw->data.hid.dwSizeHid)
            != HIDP_STATUS_SUCCESS) {
            continue;
        }
        found_hat = true;
        // HID hats commonly use 0..7 or 1..8 clockwise from up.
        left = left || hat == 6 || hat == 7;
        right = right || hat == 2 || hat == 3;
    }
    if (!found_hat) {
        return;
    }
    if (!g_raw_hid_logged.exchange(true, std::memory_order_relaxed)) {
        core::log(core::LogLevel::Info, "[title] difficulty_input status=raw_hid_ready usage=hat_switch");
    }
    g_difficulty_event_providers.update_raw_hid(left, right);
}

LRESULT CALLBACK difficulty_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!callback) {
        return g_previous_window_proc ? CallWindowProcW(g_previous_window_proc, window, message, wparam, lparam)
                                      : DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == WM_INPUT) {
        update_raw_hid_dpad(lparam);
    } else if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        if (wparam == VK_GAMEPAD_DPAD_LEFT) {
            g_difficulty_event_providers.update_window_key(true, true);
        } else if (wparam == VK_GAMEPAD_DPAD_RIGHT) {
            g_difficulty_event_providers.update_window_key(false, true);
        }
    } else if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        if (wparam == VK_GAMEPAD_DPAD_LEFT)
            g_difficulty_event_providers.update_window_key(true, false);
        else if (wparam == VK_GAMEPAD_DPAD_RIGHT)
            g_difficulty_event_providers.update_window_key(false, false);
    } else if (message == WM_KILLFOCUS) {
        g_difficulty_event_providers.reset_for_focus_loss();
        reset_native_piano_input_diagnostic_held();
    } else if (message == WM_ACTIVATEAPP && wparam == FALSE) {
        g_difficulty_event_providers.reset_for_application_loss();
        reset_native_piano_input_diagnostic_held();
    } else if (message == WM_INPUT_DEVICE_CHANGE && wparam == GIDC_REMOVAL) {
        g_difficulty_event_providers.reset_for_device_removal();
        reset_native_piano_input_diagnostic_held();
    }
    if (message == kDifficultyUiRefreshMessage) {
        ProfileListCallbacks callbacks = make_profile_list_callbacks();
        const bool refreshed = profile_list_coordinator().handle_refresh(
            static_cast<std::uint64_t>(wparam), callbacks);
        core::log(refreshed ? core::LogLevel::Info : core::LogLevel::Error,
            refreshed ? "[title] difficulty_profile ui_refresh=ready"
                      : "[title] difficulty_profile ui_refresh=ignored_or_failed");
        return 0;
    }
    return CallWindowProcW(g_previous_window_proc, window, message, wparam, lparam);
}

bool install_difficulty_window_hook()
{
    HWND window = nullptr;
    EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&window));
    if (!window) {
        core::log(core::LogLevel::Debug,
            "[title] difficulty_profile ui_refresh=window_not_found status=pending");
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(difficulty_window_proc));
    if (!previous && GetLastError() != ERROR_SUCCESS) {
        core::log(core::LogLevel::Error, "[title] difficulty_profile ui_refresh=window_hook_failed");
        return false;
    }
    g_game_window = window;
    g_previous_window_proc = reinterpret_cast<WNDPROC>(previous);
    const RAWINPUTDEVICE raw_devices[] = {
        {0x01, 0x04, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window}, // Generic Desktop / Joystick
        {0x01, 0x05, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window}, // Generic Desktop / Gamepad
    };
    if (!RegisterRawInputDevices(raw_devices, static_cast<UINT>(std::size(raw_devices)), sizeof(RAWINPUTDEVICE))) {
        core::log(core::LogLevel::Error,
            "[title] difficulty_input status=raw_input_registration_failed error=" + std::to_string(GetLastError()));
    } else {
        core::log(core::LogLevel::Info,
            "[title] difficulty_input status=raw_input_registered usages=joystick,gamepad");
    }
    core::log(core::LogLevel::Info, "[title] difficulty_profile ui_refresh=window_hook_ready");
    return true;
}

bool uninstall_difficulty_window_hook()
{
    if (g_game_window && g_previous_window_proc) {
        if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_game_window, GWLP_WNDPROC)) != difficulty_window_proc) {
            return false;
        }
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(g_game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_previous_window_proc))
            && GetLastError() != ERROR_SUCCESS) {
            return false;
        }
    }
    g_game_window = nullptr;
    g_previous_window_proc = nullptr;
    g_difficulty_event_providers.reset_for_teardown();
    reset_native_piano_input_diagnostic_held();
    return true;
}

const OwnedTitleText* refresh_live_title_locked(
    const SongDescriptor* song, const SongDifficultyProfile* profile)
{
    if (!song || (profile ? profile->title.empty() : song->title.empty())) {
        return nullptr;
    }
    g_live_title.storage = profile ? profile->title : song->title;
    return &g_live_title;
}

PlaybackSnapshot playback_title_snapshot()
{
    return registry().playback_snapshot();
}

struct ActivationSelectionTitleDiagnostic final {
    ActivationSelectionTitleFailure failure =
        ActivationSelectionTitleFailure::None;
    uintptr_t caller_rva = 0;
    uintptr_t out_view = 0;
    uintptr_t selected_song = 0;
    uintptr_t selected_profile = 0;
    uintptr_t current_song = 0;
    uintptr_t current_profile = 0;
    uintptr_t playback_song = 0;
    uintptr_t playback_profile = 0;
    uint64_t selected_generation = 0;
    uint64_t current_generation = 0;
    uint64_t playback_generation = 0;
    int selected_profile_index = -1;
    int current_profile_index = -1;
    bool applied = false;
};

static_assert(std::is_trivially_copyable_v<ActivationSelectionTitleDiagnostic>);

const char* activation_selection_title_failure_name(
    const ActivationSelectionTitleFailure failure) noexcept
{
    switch (failure) {
    case ActivationSelectionTitleFailure::None: return "none";
    case ActivationSelectionTitleFailure::CallerMismatch: return "caller";
    case ActivationSelectionTitleFailure::OverlayPrecedence: return "overlay_precedence";
    case ActivationSelectionTitleFailure::RenderPrecedence: return "render_precedence";
    case ActivationSelectionTitleFailure::SelectionSongMissing: return "selection_song";
    case ActivationSelectionTitleFailure::SelectionProfileMissing: return "selection_profile";
    case ActivationSelectionTitleFailure::SelectionStorageMissing: return "selection_storage";
    case ActivationSelectionTitleFailure::SelectionGenerationMissing: return "selection_generation";
    case ActivationSelectionTitleFailure::PlaybackConflict: return "playback_conflict";
    case ActivationSelectionTitleFailure::SelectionGenerationDrift: return "selection_generation_drift";
    case ActivationSelectionTitleFailure::SelectionStorageDrift: return "selection_storage_drift";
    case ActivationSelectionTitleFailure::SelectionSongDrift: return "selection_song_drift";
    case ActivationSelectionTitleFailure::SelectionProfileDrift: return "selection_profile_drift";
    case ActivationSelectionTitleFailure::SelectionProfileIndexDrift: return "selection_profile_index_drift";
    case ActivationSelectionTitleFailure::SelectionVisibleIndexDrift: return "selection_visible_index_drift";
    case ActivationSelectionTitleFailure::SelectionBaseSlotDrift: return "selection_base_slot_drift";
    case ActivationSelectionTitleFailure::TitleMissing: return "title_missing";
    case ActivationSelectionTitleFailure::WriteFailed: return "write_failed";
    }
    return "unknown";
}

void log_activation_selection_title(
    const ActivationSelectionTitleDiagnostic& diagnostic) noexcept
{
    try {
        const uint64_t signature =
            (static_cast<uint64_t>(diagnostic.failure) << 56)
            ^ diagnostic.selected_generation
            ^ (static_cast<uint64_t>(diagnostic.selected_profile_index + 1) << 32);
        static std::atomic_uint64_t s_last_signature{UINT64_MAX};
        if (s_last_signature.exchange(signature, std::memory_order_relaxed)
            == signature) return;
        static std::atomic_uint32_t s_budget{0};
        if (s_budget.fetch_add(1, std::memory_order_relaxed) >= 16) return;
        std::ostringstream out;
        out << "[title] converter source=activation_selection status="
            << (diagnostic.applied ? "applied" : "rejected")
            << " first_failed_predicate="
            << activation_selection_title_failure_name(diagnostic.failure)
            << " caller_rva=0x" << std::hex << diagnostic.caller_rva
            << " out=0x" << diagnostic.out_view
            << " selected_song=0x" << diagnostic.selected_song
            << " selected_profile=0x" << diagnostic.selected_profile
            << " current_song=0x" << diagnostic.current_song
            << " current_profile=0x" << diagnostic.current_profile
            << " playback_song=0x" << diagnostic.playback_song
            << " playback_profile=0x" << diagnostic.playback_profile
            << std::dec
            << " selected_generation=" << diagnostic.selected_generation
            << " current_generation=" << diagnostic.current_generation
            << " playback_generation=" << diagnostic.playback_generation
            << " selected_profile_index=" << diagnostic.selected_profile_index
            << " current_profile_index=" << diagnostic.current_profile_index;
        core::log(diagnostic.applied ? core::LogLevel::Info : core::LogLevel::Debug,
            out.str());
    } catch (...) {
    }
}

SelectionSnapshot menu_title_snapshot()
{
    if (!custom_audio_route_idle_for_menu_input()) return {};
    return registry().selection_snapshot();
}

void apply_detail_title(void* context, const char* source)
{
    if (!context) {
        return;
    }
    auto* set_text = resolve_set_string_text_helper();
    if (!set_text) {
        return;
    }

    int32_t slot_count = 0;
    core::safe_read_field(context, 0x1b0, slot_count);
    if (slot_count <= 0 || slot_count > 8) {
        slot_count = 1;
    }

    int applied_count = 0;
    std::wstring title_snapshot;
    {
        std::lock_guard<std::mutex> lock(g_title_text_mutex);
        const PlaybackSnapshot playback = playback_title_snapshot();
        const SelectionSnapshot menu = playback.song ? SelectionSnapshot{} : menu_title_snapshot();
        const OwnedTitleText* title = refresh_live_title_locked(
            playback.song ? playback.song : menu.song,
            playback.song ? playback.profile : menu.profile);
        if (!title) {
            return;
        }
        title_snapshot = title->storage;
        FStringView view = make_fstring_view(*title);
        for (int32_t slot = 0; slot < slot_count; ++slot) {
            void* title_widget = nullptr;
            if (core::safe_read_field(reinterpret_cast<uint8_t*>(context) + 0x38 + static_cast<uintptr_t>(slot) * sizeof(void*), 0, title_widget)
                && title_widget) {
                set_text(title_widget, &view);
                ++applied_count;
            }
        }
    }

    static std::atomic_int s_detail_title_logs{0};
    const int log_index = s_detail_title_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 32) {
        std::ostringstream out;
        out << "[title] detail_title_helper status=" << (applied_count > 0 ? "applied" : "missing_title_widgets")
            << " source=" << (source ? source : "?")
            << " context=0x" << std::hex << reinterpret_cast<uintptr_t>(context)
            << std::dec
            << " slot_count=" << slot_count
            << " applied_count=" << applied_count
            << " title=\"" << core::narrow(title_snapshot) << "\"";
        core::log(core::LogLevel::Debug, out.str());
    }
}

void __fastcall detail_update_detour(void* context)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (g_original_detail_update) {
        ScoreInfoPianoDetailUpdateScope scoreinfo_result_scope;
        g_original_detail_update(context);
    }
    if (!callback) return;
    const PlaybackSnapshot playback = playback_title_snapshot();
    const SelectionSnapshot menu = playback.song ? SelectionSnapshot{} : menu_title_snapshot();
    if (playback.song || menu.song) {
        apply_detail_title(context, "detail_update");
    }
}

uint8_t __fastcall title_population_detour(void* context)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!callback) return g_original_title_population ? g_original_title_population(context) : 0;
    g_last_title_population_context.store(context, std::memory_order_release);
    const uint8_t result = g_original_title_population ? g_original_title_population(context) : 0;
    const PlaybackSnapshot playback = playback_title_snapshot();
    const SelectionSnapshot menu = playback.song ? SelectionSnapshot{} : menu_title_snapshot();
    if (playback.song || menu.song) {
        apply_detail_title(context, "title_population");
    }
    return result;
}

uint64_t* __fastcall title_converter_detour(void* wrapper, uint64_t* out_view, void* arg3)
{
    auto callback = non_audio_hook_gate().try_enter();
    uint64_t* result = out_view;
    TitleConverterOriginalCallState original_call;
    if (g_original_title_converter) {
        result = call_title_converter_original_exact_once(original_call, [&] {
            return g_original_title_converter(wrapper, out_view, arg3);
        });
    }

    if (!callback) return result;
    const uintptr_t return_address = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t module_base = reinterpret_cast<uintptr_t>(g_exe_module);
    const uintptr_t caller_rva = module_base && return_address >= module_base
        ? return_address - module_base : 0;
    const bool activation_return_exact =
        title_converter_activation_return_exact(
            caller_rva, rva::TitleViewConverterActivationReturn);
    if (!out_view && !activation_return_exact) return result;
    uint64_t row = 0;
    if (wrapper) (void)core::safe_read_field(wrapper, sizeof(uint64_t), row);
    const RenderSnapshot render = registry().render_snapshot();
    const TitleResolverToken resolver = peek_title_resolver_token();
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const bool source_is_active_overlay = title_resolver_token_matches(
        resolver, playback, reinterpret_cast<const void*>(row))
        && scoreinfo_overlay_row_matches_playback(
            reinterpret_cast<const void*>(row), playback);
    const bool source_is_scoped_render_context = render.song != nullptr;
    if (!source_is_active_overlay && !source_is_scoped_render_context
        && activation_return_exact) {
        const SelectionSnapshot selected = registry().selection_snapshot();
        ActivationSelectionTitleDiagnostic diagnostic;
        diagnostic.caller_rva = caller_rva;
        diagnostic.out_view = reinterpret_cast<uintptr_t>(out_view);
        diagnostic.selected_song = reinterpret_cast<uintptr_t>(selected.song);
        diagnostic.selected_profile = reinterpret_cast<uintptr_t>(selected.profile);
        diagnostic.selected_generation = selected.generation;
        diagnostic.selected_profile_index = selected.profile_index;
        {
            std::lock_guard<std::mutex> lock(g_title_text_mutex);
            SelectionSnapshot current;
            PlaybackSnapshot current_playback;
            const bool committed = registry().commit_if_selection_snapshot(
                selected, current, current_playback, [&] {
                    const ActivationSelectionTitleFacts facts =
                        activation_selection_title_facts(
                            true, false, false, selected, current,
                            current_playback);
                    diagnostic.failure =
                        first_activation_selection_title_failure(facts);
                    if (diagnostic.failure
                        != ActivationSelectionTitleFailure::None) return false;
                    const OwnedTitleText* title = refresh_live_title_locked(
                        selected.song, selected.profile);
                    if (!title) {
                        diagnostic.failure =
                            ActivationSelectionTitleFailure::TitleMissing;
                        return false;
                    }
                    if (!apply_title_text_view(out_view, *title)) {
                        diagnostic.failure =
                            ActivationSelectionTitleFailure::WriteFailed;
                        return false;
                    }
                    return true;
                });
            diagnostic.current_song = reinterpret_cast<uintptr_t>(current.song);
            diagnostic.current_profile = reinterpret_cast<uintptr_t>(current.profile);
            diagnostic.current_generation = current.generation;
            diagnostic.current_profile_index = current.profile_index;
            diagnostic.playback_song =
                reinterpret_cast<uintptr_t>(current_playback.song);
            diagnostic.playback_profile =
                reinterpret_cast<uintptr_t>(current_playback.profile);
            diagnostic.playback_generation = current_playback.generation;
            diagnostic.applied = committed;
            if (!committed
                && diagnostic.failure == ActivationSelectionTitleFailure::None) {
                const ActivationSelectionTitleFacts facts =
                    activation_selection_title_facts(
                        true, false, false, selected, current,
                        current_playback);
                diagnostic.failure =
                    first_activation_selection_title_failure(facts);
                if (diagnostic.failure == ActivationSelectionTitleFailure::None) {
                    diagnostic.failure =
                        ActivationSelectionTitleFailure::SelectionGenerationDrift;
                }
            }
        }
        log_activation_selection_title(diagnostic);
        return result;
    }
    const SongDescriptor* song = source_is_active_overlay ? playback.song : render.song;
    const SongDifficultyProfile* profile = source_is_active_overlay
        ? playback.profile : render.profile;
    if (!song) return result;
    if (!title_converter_source_allowed(source_is_active_overlay, source_is_scoped_render_context)) {
        static std::atomic_int s_unexpected_source_logs{0};
        const int log_index = s_unexpected_source_logs.fetch_add(1, std::memory_order_relaxed);
        if (log_index < 24) {
            std::ostringstream out;
            out << "[title] converter status=unexpected_source"
                << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper)
                << " row=0x" << row
                << " out=0x" << reinterpret_cast<uintptr_t>(out_view)
                << std::dec
                << " resolver_depth=" << pending_title_resolver_tokens()
                << " song_id=" << song->id;
            core::log(core::LogLevel::Debug, out.str());
        }
        return result;
    }

    bool applied = false;
    std::wstring title_snapshot;
    {
        std::lock_guard<std::mutex> lock(g_title_text_mutex);
        const OwnedTitleText* title = refresh_live_title_locked(song, profile);
        if (title) {
            title_snapshot = title->storage;
            applied = apply_title_text_view(out_view, *title);
        }
    }
    if (applied) {
        if (source_is_active_overlay) {
            (void)consume_title_resolver_token(resolver);
        }
    }

    static std::atomic_int s_converter_logs{0};
    const int log_index = s_converter_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 32) {
        std::ostringstream out;
        out << "[title] converter status=" << (applied ? "overridden" : "write_failed")
            << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper)
            << " row=0x" << row
            << " out=0x" << reinterpret_cast<uintptr_t>(out_view)
            << std::dec
            << " source_validated=" << (source_is_active_overlay ? 1 : 0)
            << " render_scope=" << (source_is_scoped_render_context ? 1 : 0)
            << " resolver_depth_after=" << pending_title_resolver_tokens()
            << " title=\"" << core::narrow(title_snapshot) << "\"";
        core::log(applied ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    }
    return result;
}

bool is_writable_range(void* address, size_t size)
{
    if (!address || size == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) {
        return false;
    }

    const DWORD protect = info.Protect & 0xff;
    const bool writable = protect == PAGE_READWRITE
        || protect == PAGE_WRITECOPY
        || protect == PAGE_EXECUTE_READWRITE
        || protect == PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<uintptr_t>(address);
    const auto region_begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const auto region_end = region_begin + info.RegionSize;
    return writable && (info.Protect & PAGE_GUARD) == 0 && begin >= region_begin && size <= region_end - begin;
}

} // namespace

ProfileListCallbacks make_profile_list_callbacks()
{
    ProfileListCallbacks callbacks;
    callbacks.capture_selection = [] { return registry().selection_snapshot(); };
    callbacks.capture_session = [](bool allow_opening) {
        return allow_opening ? capture_menu_callback_session()
                             : capture_ready_menu_session();
    };
    callbacks.validate_session = [](uint64_t generation, bool require_ready) {
        return menu_session_generation_exact(generation, require_ready);
    };
    callbacks.capture_ui = [](const SelectionSnapshot& expected, MenuUiRefreshSnapshot& out) {
        out = {}; out.selection = expected;
        return capture_active_list_item_ui_target(expected, out.list_target)
            && capture_active_selection_ui_target(expected, out.selection_target)
            && registry().selection_matches(expected);
    };
    callbacks.audio_idle = [] { return custom_audio_route_idle_for_menu_input(); };
    callbacks.revoke_audio_activation = [](const SelectionSnapshot& expected,
        uint64_t session_generation) {
        return revoke_selection_audio_activation_for_menu_session(
            expected, session_generation);
    };
    callbacks.cycle_profile = [](const SelectionSnapshot& expected, int delta,
        SelectionSnapshot& changed) {
        return registry().cycle_active_profile_exact(expected, delta, changed);
    };
    callbacks.log_profile_change = [](const SelectionSnapshot& changed) {
        std::ostringstream out;
        out << "[title] difficulty_profile status=changed song_id=" << changed.song->id
            << " difficulty=" << changed.profile->difficulty
            << " notes=" << changed.profile->note_count;
        core::log(core::LogLevel::Info, out.str());
    };
    callbacks.observe_scoreinfo_overlay = [] {
        const bool refreshed = refresh_active_scoreinfo_overlay_profile();
        core::log(core::LogLevel::Info, refreshed
            ? "[title] difficulty_profile ui_refresh=overlay_updated"
            : "[title] difficulty_profile ui_refresh=overlay_unavailable");
        return refreshed;
    };
    callbacks.post_refresh = [](std::uint64_t ticket) {
        if (!g_game_window) (void)install_difficulty_window_hook();
        const bool posted = g_game_window && PostMessageW(g_game_window,
            kDifficultyUiRefreshMessage, static_cast<WPARAM>(ticket), 0);
        if (!posted) core::log(core::LogLevel::Error,
            "[title] difficulty_profile ui_refresh=post_failed");
        return posted;
    };
    callbacks.refresh_list = [](const SelectionSnapshot& expected,
        const ListItemUiRefreshTarget& target) {
        return refresh_active_list_item_ui(expected, target);
    };
    callbacks.refresh_selection = [](const SelectionSnapshot& expected,
        const SelectionUiRefreshTarget& target) {
        return refresh_active_selection_ui(expected, target);
    };
    callbacks.revalidate = [](const SelectionSnapshot& expected,
        const MenuUiRefreshSnapshot& ui, SelectionSnapshot& adopted) {
        SelectionSnapshot current = registry().selection_snapshot();
        if (!selection_semantically_matches(expected, current)
            || !validate_active_list_item_ui_target(current, ui.list_target)
            || !validate_active_selection_ui_target(current, ui.selection_target)) return false;
        adopted = std::move(current);
        return true;
    };
    return callbacks;
}

TitleTextView OwnedTitleText::view() const
{
    const int32_t num_with_null = static_cast<int32_t>(storage.size() + 1);
    return {storage.c_str(), num_with_null, num_with_null};
}

OwnedTitleText format_descriptor_title(
    const SongDescriptor& song, const SongDifficultyProfile* profile)
{
    OwnedTitleText result;
    result.storage = descriptor_title_text(song, profile);
    return result;
}

bool apply_title_text_view(void* out_view, const OwnedTitleText& title)
{
    if (!out_view || title.storage.empty()) {
        return false;
    }

    if (!is_writable_range(out_view, sizeof(uint64_t) + sizeof(uint32_t))) {
        return false;
    }

    const TitleTextView view = title.view();
    const uint64_t pointer = reinterpret_cast<uint64_t>(view.data);
    const uint32_t length_without_null = static_cast<uint32_t>(title.storage.size());
    std::memcpy(out_view, &pointer, sizeof(pointer));
    std::memcpy(static_cast<uint8_t*>(out_view) + sizeof(pointer), &length_without_null, sizeof(length_without_null));
    return true;
}

void reset_native_piano_input_diagnostic_held() noexcept
{
    g_native_piano_input_diagnostic_admission.reset_held();
}

void push_title_resolver_token(const TitleResolverToken& token)
{
    g_title_resolver_tokens.push(token);
}

TitleResolverToken peek_title_resolver_token()
{
    return g_title_resolver_tokens.peek();
}

bool consume_title_resolver_token(const TitleResolverToken& expected)
{
    return g_title_resolver_tokens.consume(expected);
}

void invalidate_title_resolver_tokens(const CustomContextToken& token)
{
    g_title_resolver_tokens.invalidate(token);
}

int pending_title_resolver_tokens()
{
    return g_title_resolver_tokens.size();
}

bool install_title_hooks(const HookInstallContext& context)
{
    g_exe_module = context.exe_module;
    const bool ok = install_named_hook(context, "piano_detail_update", g_detail_update_hook,
                        reinterpret_cast<void*>(&detail_update_detour), reinterpret_cast<void**>(&g_original_detail_update))
        && install_named_hook(context, "piano_title_population", g_title_population_hook,
            reinterpret_cast<void*>(&title_population_detour), reinterpret_cast<void**>(&g_original_title_population));

    bool converter_ok = false;
    bool native_input_ok = false;
    if (ok) {
        converter_ok = install_named_hook(context, "title_view_converter", g_title_converter_hook,
            reinterpret_cast<void*>(&title_converter_detour), reinterpret_cast<void**>(&g_original_title_converter));
        if (!converter_ok) {
            core::log(core::LogLevel::Error, "[title] converter status=disabled reason=install_failed");
        }
        (void)resolve_native_dpad_names();
        native_input_ok = install_named_hook(context, "player_input_input_key", g_player_input_key_hook,
            reinterpret_cast<void*>(&player_input_key_detour), reinterpret_cast<void**>(&g_original_player_input_key));
        std::ostringstream input_out;
        input_out << "[title] difficulty_input status="
            << (native_input_ok ? "native_player_input_ready" : "native_player_input_disabled");
        if (native_input_ok) {
            input_out << " rva=0x" << std::hex << rva::PlayerInputInputKey;
        } else {
            input_out << " reason=install_failed";
        }
        core::log(native_input_ok ? core::LogLevel::Info : core::LogLevel::Error, input_out.str());
    }

    std::ostringstream out;
    out << "[title] status=" << (ok ? "live_hooks_installed" : "install_failed")
        << " seam=detail_title_helper title_view_converter=" << (converter_ok ? "enabled_source_validated" : "disabled")
        << " detail_title_helper=" << (resolve_set_string_text_helper() ? "available" : "disabled")
        << " custom_songs=" << registry().custom_count();
    core::log(ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    (void)install_difficulty_window_hook();
    return ok && start_difficulty_input_thread();
}

core::HookShutdownResult shutdown_title()
{
    profile_list_coordinator().shutdown();
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_player_input_key_hook),
        core::teardown_operation(g_title_converter_hook),
        core::teardown_operation(g_title_population_hook),
        core::teardown_operation(g_detail_update_hook),
    }, [] {
        if (!stop_difficulty_input_thread()) return false;
        return uninstall_difficulty_window_hook();
    }, [] {
        g_original_player_input_key = nullptr;
        g_native_dpad_left_name.store(0, std::memory_order_release);
        g_native_dpad_right_name.store(0, std::memory_order_release);
        g_native_dpad_logged.store(false, std::memory_order_release);
        for (auto& direction : g_native_piano_direction_names) direction.store(0, std::memory_order_release);
        g_original_title_converter = nullptr;
        g_original_title_population = nullptr;
        g_last_title_population_context.store(nullptr, std::memory_order_release);
        g_original_detail_update = nullptr;
        g_difficulty_input_state.reset();
        g_difficulty_input_eligible = false;
        g_exe_module = nullptr;
    });
}

} // namespace ff7r::piano::game
