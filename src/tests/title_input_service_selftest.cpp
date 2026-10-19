#include "game/title.h"
#include <hidsdi.h>
#include <hidpi.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <Xinput.h>

namespace {
bool deferred=false,fail_thread=false,fail_window=false,fail_raw=false,fail_restore=false,fail_names=false;
bool fail_hook=false,stop_on_name=false;
bool window_available=true;
unsigned posts=0,window_messages=0;
ULONG raw_hat=6;
unsigned threads=0,waits=0,closes=0,windows=0,raw_adds=0,raw_removes=0,names=0,original_keys=0,hooks=0;
LONG_PTR window_proc=1;
bool ctrl=false, function=false, foreground=true, fail_feedback_stop=false, accept_toggle=true;
int configured_key=8;
std::uint64_t seat_token=0;
unsigned toggles=0,feedback=0,feedback_stops=0;
bool preview_focus=false,observed_preview_held=false,preview_chord=false,fail_preview_stop=false;
unsigned preview_cancels=0,preview_shutdowns=0;
WORD controller_buttons[4]{};
SHORT WINAPI async_key(int key) {return ((key==VK_CONTROL && ctrl)
    || (key==VK_F1+configured_key-1 && configured_key && function))?SHORT(-32768):0;}
HWND WINAPI foreground_window(){return foreground?reinterpret_cast<HWND>(3):nullptr;}
DWORD WINAPI window_process(HWND,LPDWORD process){*process=GetCurrentProcessId();return 1;}
DWORD WINAPI xinput(DWORD user,XINPUT_STATE* state){state->Gamepad.wButtons=controller_buttons[user];return ERROR_SUCCESS;}
void require(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
HANDLE WINAPI create_thread(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD) {
    require(deferred,"early CreateThread");++threads;return fail_thread?nullptr:reinterpret_cast<HANDLE>(2);
}
DWORD WINAPI wait_thread(HANDLE,DWORD) {++waits;return WAIT_OBJECT_0;}
BOOL WINAPI close_thread(HANDLE) {++closes;return TRUE;}
BOOL WINAPI enum_windows(WNDENUMPROC,LPARAM output) {
    require(deferred,"early window setup");++windows;
    *reinterpret_cast<HWND*>(output)=window_available?reinterpret_cast<HWND>(3):nullptr;return TRUE;
}
BOOL WINAPI post_message(HWND,UINT,WPARAM,LPARAM){++posts;return TRUE;}
LRESULT WINAPI original_window(WNDPROC,HWND,UINT,WPARAM,LPARAM){++window_messages;return 77;}
UINT WINAPI raw_input(HRAWINPUT,UINT,LPVOID output,PUINT size,UINT) {
    *size=sizeof(RAWINPUT);if(!output)return 0;
    RAWINPUT raw{};raw.header.dwType=RIM_TYPEHID;raw.header.dwSize=sizeof(raw);
    raw.data.hid.dwCount=1;raw.data.hid.dwSizeHid=1;
    std::memcpy(output,&raw,sizeof(raw));return sizeof(raw);
}
UINT WINAPI raw_info(HANDLE,UINT,LPVOID output,PUINT size) {
    *size=16;if(output)std::memset(output,0,16);return output?16:0;
}
NTSTATUS NTAPI hat_value(HIDP_REPORT_TYPE,USAGE,USHORT,USAGE,PULONG value,PHIDP_PREPARSED_DATA,PCHAR,ULONG) {
    *value=raw_hat;return HIDP_STATUS_SUCCESS;
}
LONG_PTR WINAPI set_window(HWND,int,LONG_PTR value) {
    if((value!=1 && fail_window) || (value==1 && fail_restore)) {SetLastError(ERROR_ACCESS_DENIED);return 0;}
    const auto old=window_proc;window_proc=value;return old;
}
LONG_PTR WINAPI get_window(HWND,int) {return window_proc;}
BOOL WINAPI test_register_raw_devices(PCRAWINPUTDEVICE devices,UINT count,UINT) {
    require(deferred,"early raw input setup");require(count==2,"raw device count");
    if(devices[0].dwFlags&RIDEV_REMOVE){++raw_removes;return TRUE;}
    ++raw_adds;return !fail_raw;
}
}
#define CreateThread create_thread
#define WaitForSingleObject wait_thread
#define CloseHandle close_thread
#define EnumWindows enum_windows
#define SetWindowLongPtrW set_window
#define GetWindowLongPtrW get_window
#define RegisterRawInputDevices test_register_raw_devices
#define PostMessageW post_message
#define CallWindowProcW original_window
#define GetRawInputData raw_input
#define GetRawInputDeviceInfoW raw_info
#define HidP_GetUsageValue hat_value
#define FF7RP_TITLE_INPUT_SERVICE_SELFTEST
#define GetAsyncKeyState async_key
#define GetForegroundWindow foreground_window
#define GetWindowThreadProcessId window_process
#include "game/title.cpp"
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef CreateThread
#undef WaitForSingleObject
#undef CloseHandle
#undef EnumWindows
#undef SetWindowLongPtrW
#undef GetWindowLongPtrW
#undef RegisterRawInputDevices
#undef PostMessageW
#undef CallWindowProcW
#undef GetRawInputData
#undef GetRawInputDeviceInfoW
#undef HidP_GetUsageValue

using namespace ff7r::piano::game;
// Unrelated UI/audio paths must not be reached by this startup/entry test.
namespace ff7r::piano::game {
int preview_function_key() noexcept { return 7; }
void poll_song_preview(bool focus, bool held, bool chord) noexcept {
    preview_focus=focus;observed_preview_held=held;preview_chord=chord;
}
bool cancel_song_preview(const char*, bool) noexcept { ++preview_cancels;return true; }
bool shutdown_song_preview() noexcept { ++preview_shutdowns;return !fail_preview_stop; }
core::HookCallbackGate& non_audio_hook_gate() {static core::HookCallbackGate gate;return gate;}
int autoplay_function_key() noexcept {return configured_key;}
std::uint64_t autoplay_seated_token() noexcept {return seat_token;}
bool autoplay_toggle(std::uint64_t token,bool& enabled) noexcept {
    if(!accept_toggle || !token || token!=seat_token)return false;
    enabled=(++toggles%2)!=0;return true;
}
bool autoplay_assisted() noexcept {return true;}
void autoplay_observe_native_phase() noexcept {}
bool revoke_selection_audio_activation_for_menu_session(const SelectionSnapshot&,std::uint64_t) noexcept {std::abort();}
bool custom_audio_route_idle_for_menu_input() noexcept {std::abort();}
bool custom_audio_playback_clock(double&) noexcept {std::abort();}
bool chart_time_seconds(const std::string&,float&){std::abort();}
bool capture_active_selection_ui_target(const SelectionSnapshot&,SelectionUiRefreshTarget&) noexcept {std::abort();}
bool capture_active_list_item_ui_target(const SelectionSnapshot&,ListItemUiRefreshTarget&) noexcept {std::abort();}
bool validate_active_selection_ui_target(const SelectionSnapshot&,const SelectionUiRefreshTarget&) noexcept {std::abort();}
bool validate_active_list_item_ui_target(const SelectionSnapshot&,const ListItemUiRefreshTarget&) noexcept {std::abort();}
bool refresh_active_selection_ui(const SelectionSnapshot&,const SelectionUiRefreshTarget&){std::abort();}
bool refresh_active_list_item_ui(const SelectionSnapshot&,const ListItemUiRefreshTarget&){std::abort();}
MenuSessionSnapshot capture_ready_menu_session() noexcept {std::abort();}
MenuSessionSnapshot capture_menu_callback_session() noexcept {std::abort();}
bool menu_session_generation_exact(std::uint64_t,bool) noexcept {std::abort();}
ScoreInfoPianoDetailUpdateScope::ScoreInfoPianoDetailUpdateScope() noexcept {}
ScoreInfoPianoDetailUpdateScope::~ScoreInfoPianoDetailUpdateScope(){}
bool scoreinfo_overlay_row_matches_playback(const void*,const PlaybackSnapshot&){std::abort();}
bool refresh_active_scoreinfo_overlay_profile(){std::abort();}
}
namespace ff7r::piano::core {
bool play_toggle_feedback(bool enabled) {++feedback;require(enabled==(toggles%2!=0),"feedback direction");return true;}
bool stop_toggle_feedback(){++feedback_stops;require(!g_difficulty_input_thread,"sound stopped before poller");return !fail_feedback_stop;}
}
namespace {
bool __fastcall original_key(void*,const void*,int32_t,float,bool) {++original_keys;return true;}
void* __fastcall name_ctor(FNameValue* output,const wchar_t*,int32_t) {
    ++names;require(deferred,"early GAME FNameCtor");
    if(stop_on_name && names==1)(void)shutdown_title();
    *output={fail_names?0u:100u+names,0};return output;
}
bool install_hook(const char* name,void** original) {
    ++hooks;*original=std::string_view(name)=="player_input_input_key"
        ?reinterpret_cast<void*>(&original_key):reinterpret_cast<void*>(1);return !fail_hook;
}
HMODULE module() {return reinterpret_cast<HMODULE>(0x10000000);}
void reset() {
    deferred=fail_thread=fail_window=fail_raw=fail_restore=fail_names=false;
    fail_hook=stop_on_name=false;
    window_available=true;posts=window_messages=0;raw_hat=6;
    threads=waits=closes=windows=raw_adds=raw_removes=names=original_keys=hooks=0;window_proc=1;
    g_input_service_state.store(TitleInputServiceState::Uninstalled);g_exe_module=nullptr;
    g_difficulty_input_thread=nullptr;g_difficulty_input_thread_running=false;g_game_window=nullptr;
    g_previous_window_proc=nullptr;g_raw_input_registration_attempted=false;
    g_native_dpad_left_name=0;g_native_dpad_right_name=0;
    for(auto& value:g_native_piano_direction_names)value=0;
    g_test_install_title_hook=&install_hook;g_test_fname_ctor=&name_ctor;
    non_audio_hook_gate().open();
    ctrl=function=false;foreground=true;configured_key=8;seat_token=0;
    fail_feedback_stop=false;accept_toggle=true;toggles=feedback=feedback_stops=0;
    fail_preview_stop=false;
    std::memset(controller_buttons,0,sizeof(controller_buttons));
    g_autoplay_input_edge={};g_difficulty_input_state.reset();
}
void install() {
    require(install_title_hooks({module()}),"hook publication");
    require(hooks==4 && !names && !threads && !windows && !raw_adds,"early install performed service work");
}
void autoplay_input_tests() {
    reset();
    // Resolve real providers once, then replace with the deterministic same-controller fixture.
    std::call_once(g_xinput_resolve_once,resolve_xinput_get_state);
    g_xinput_get_states={&xinput};
    poll_dynamic_difficulty_input_impl();
    ctrl=function=true;poll_dynamic_difficulty_input_impl();
    require(!toggles && !feedback,"standing toggle");
    seat_token=1;poll_dynamic_difficulty_input_impl();require(!toggles,"held through seat entry");
    ctrl=false;poll_dynamic_difficulty_input_impl();ctrl=true;poll_dynamic_difficulty_input_impl();
    require(toggles==1 && feedback==1,"immediate seated keyboard");
    poll_dynamic_difficulty_input_impl();require(toggles==1,"held retrigger");
    function=false;poll_dynamic_difficulty_input_impl();function=true;accept_toggle=false;
    poll_dynamic_difficulty_input_impl();require(feedback==1,"rejected toggle sounded");
    accept_toggle=true;function=false;poll_dynamic_difficulty_input_impl();
    function=true;foreground=false;poll_dynamic_difficulty_input_impl();foreground=true;
    poll_dynamic_difficulty_input_impl();require(toggles==1,"held focus entry");
    configured_key=0;ctrl=function=false;
    controller_buttons[0]=XINPUT_GAMEPAD_LEFT_THUMB;controller_buttons[1]=XINPUT_GAMEPAD_RIGHT_THUMB;
    poll_dynamic_difficulty_input_impl();require(toggles==1,"cross-controller chord");
    controller_buttons[0]|=XINPUT_GAMEPAD_RIGHT_THUMB;poll_dynamic_difficulty_input_impl();
    require(toggles==2 && feedback==2,"same-controller deactivation feedback");
    controller_buttons[0]=controller_buttons[1]=0;poll_dynamic_difficulty_input_impl();
    configured_key=12;ctrl=function=true;poll_dynamic_difficulty_input_impl();
    require(toggles==3 && feedback==3,"configured F key");
    function=false;poll_dynamic_difficulty_input_impl();configured_key=0;function=true;
    poll_dynamic_difficulty_input_impl();require(toggles==3,"keyboard disabled");
    configured_key=13;poll_dynamic_difficulty_input_impl();require(toggles==3,"F13 unsupported");
}
void preview_input_tests() {
    reset();g_xinput_get_states={&xinput};
    controller_buttons[0]=XINPUT_GAMEPAD_RIGHT_THUMB;
    poll_dynamic_difficulty_input_impl();
    require(preview_focus && observed_preview_held && !preview_chord,"R3-only preview");
    controller_buttons[0]|=XINPUT_GAMEPAD_LEFT_THUMB;
    poll_dynamic_difficulty_input_impl();
    require(!observed_preview_held && preview_chord,"autoplay chord excluded from preview");
    controller_buttons[0]=0;configured_key=7;ctrl=function=true;
    poll_dynamic_difficulty_input_impl();require(observed_preview_held,"Ctrl+F7 preview");
    foreground=false;poll_dynamic_difficulty_input_impl();require(!preview_focus,"preview focus projection");
    const auto prior=preview_cancels;
    g_input_service_state=TitleInputServiceState::Active;
    g_previous_window_proc=reinterpret_cast<WNDPROC>(1);
    difficulty_window_proc(nullptr,WM_KILLFOCUS,0,0);
    difficulty_window_proc(nullptr,WM_ACTIVATEAPP,FALSE,0);
    require(preview_cancels==prior+2,"synchronous preview focus cancellation");
}
void __fastcall result_consumer(void*) {
    auto* scope=AutoplayResultScope::active();require(scope && scope->suppress(),"outer shadow absent on rejected gate");
    std::array<std::uint8_t,16> record{};
    require(scope->redirect(record.data())!=record.data(),"outer result shadow aliases record");
}
}
int main() {
    try {
        autoplay_input_tests();
        preview_input_tests();
        reset();non_audio_hook_gate().close();g_original_detail_update=result_consumer;
        detail_update_detour(nullptr);require(!AutoplayResultScope::active(),"outer shadow leaked");
        reset();install();
        FNameValue key{101,0};
        require(player_input_key_detour(nullptr,&key,0,1,true) && original_keys==1,"early input original");
        require(!g_difficulty_event_providers.snapshot().decrement.player_input_held,"early custom input");
        deferred=true;
        require(!start_title_input_service(reinterpret_cast<HMODULE>(4)),"wrong installation module");
        require(start_title_input_service(module()),"deferred startup");
        require(start_title_input_service(module()) && names==12 && threads==1 && windows==1 && raw_adds==1,"startup not once");
        require(player_input_key_detour(nullptr,&key,0,1,true),"active key original");
        require(g_difficulty_event_providers.snapshot().decrement.player_input_held,"resolved key lost");
        require(player_input_key_detour(nullptr,&key,1,0,true),"release original");
        require(!g_difficulty_event_providers.snapshot().decrement.player_input_held,"release lost");
        require(shutdown_title().ok() && waits==1 && closes==1 && raw_removes==1 && window_proc==1,"service stop");
        require(!start_title_input_service(module()),"stopped service resurrected");

        reset();install();deferred=true;window_available=false;
        require(start_title_input_service(module()) && !g_game_window,"no-window startup was fatal");
        require(start_title_input_service(module()) && names==12 && threads==1 && windows==1 && !raw_adds,"pending service restarted");
        auto refresh=make_profile_list_callbacks().post_refresh;
        require(!refresh(1) && !posts && windows==2,"pending refresh incorrectly posted");
        window_available=true;
        require(refresh(2) && posts==1 && raw_adds==1 && names==12 && threads==1,"late window attachment");
        require(refresh(3) && windows==3 && raw_adds==1,"window attached twice");
        require(difficulty_window_proc(g_game_window,WM_KEYDOWN,VK_GAMEPAD_DPAD_LEFT,0)==77
            && g_difficulty_event_providers.snapshot().decrement.window_key_held,"late window key lost");
        (void)difficulty_window_proc(g_game_window,WM_KEYUP,VK_GAMEPAD_DPAD_LEFT,0);
        require(!g_difficulty_event_providers.snapshot().decrement.window_key_held,"window release lost");
        (void)difficulty_window_proc(g_game_window,WM_INPUT,0,1);
        require(g_difficulty_event_providers.snapshot().decrement.raw_hid_held,"late raw input lost");
        raw_hat=8;(void)difficulty_window_proc(g_game_window,WM_INPUT,0,1);
        require(!g_difficulty_event_providers.snapshot().decrement.raw_hid_held && window_messages==4,"raw release/original forwarding");
        require(shutdown_title().ok() && waits==1 && closes==1 && raw_removes==1 && window_proc==1,"late window cleanup");
        require(!refresh(4) && windows==3 && !start_title_input_service(module()),"lazy attachment after stop");
        require(shutdown_title().ok() && waits==1 && closes==1 && raw_removes==1,"cleanup repeated");
        reset();install();deferred=true;window_available=false;
        require(start_title_input_service(module()),"pending stop setup");
        auto stopped_refresh=make_profile_list_callbacks().post_refresh;
        require(shutdown_title().ok() && !raw_removes,"pending service cleanup");
        window_available=true;
        require(!stopped_refresh(1) && windows==1 && names==12 && threads==1
            && !start_title_input_service(module()),"pending service revived after stop");
        for(int failure=0;failure<2;++failure) {
            reset();install();deferred=true;window_available=false;
            require(start_title_input_service(module()),"lazy failure setup");
            window_available=true;fail_window=failure==0;fail_raw=failure==1;
            auto failed_refresh=make_profile_list_callbacks().post_refresh;
            require(!failed_refresh(1) && !posts && !g_difficulty_input_thread_running,"late failure admitted");
            require(!failed_refresh(2) && windows==2 && names==12 && threads==1
                && !start_title_input_service(module()),"late failure retried");
            require(shutdown_title().ok() && waits==1 && closes==1 && window_proc==1,"late failure cleanup");
        }

        reset();install();require(shutdown_title().ok() && !waits && !names,"early shutdown waited/called GAME");
        deferred=true;require(!start_title_input_service(module()) && !threads,"stop before start resurrected");
        reset();fail_hook=true;require(!install_title_hooks({module()}),"failed hooks published");
        require(shutdown_title().ok() && !names && !threads && !waits && !windows,"early failure waited/started service");
        reset();install();deferred=true;stop_on_name=true;
        require(!start_title_input_service(module()) && names==1 && !windows && !threads && !waits,"stop during startup resurrected");
        require(shutdown_title().ok(),"stopped startup cleanup");
        for(int failure=0;failure<4;++failure) {
            reset();install();deferred=true;
            fail_names=failure==0;fail_window=failure==1;fail_raw=failure==2;fail_thread=failure==3;
            require(!start_title_input_service(module()),"failed startup admitted");
            const auto calls=names;
            require(!start_title_input_service(module()) && names==calls,"failure retried native names");
            require(!waits && window_proc==1 && !g_game_window && !g_difficulty_input_thread,"partial cleanup");
            if(failure>=2)require(raw_removes==1,"raw startup rollback missing");
            require(shutdown_title().ok(),"failed service shutdown");
        }
        reset();install();deferred=true;fail_thread=true;fail_restore=true;
        require(!start_title_input_service(module()) && g_game_window,"failed restoration ownership lost");
        fail_restore=false;require(shutdown_title().ok() && window_proc==1,"retained window cleanup");
        reset();install();deferred=true;require(start_title_input_service(module()),"sound-stop setup");
        fail_feedback_stop=true;
        require(!shutdown_title().ok() && g_original_detail_update && !g_difficulty_input_thread,
            "sound stop failure lost hook ownership");
        fail_feedback_stop=false;require(shutdown_title().ok(),"retained sound ownership cleanup");
        reset();install();deferred=true;require(start_title_input_service(module()),"preview-stop setup");
        fail_preview_stop=true;
        require(!shutdown_title().ok() && g_original_detail_update && !g_difficulty_input_thread,
            "preview stop failure lost hook ownership");
        fail_preview_stop=false;require(shutdown_title().ok(),"retained preview ownership cleanup");
        std::cout<<"title input service selftest passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
