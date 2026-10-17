#include "game/title.h"
#include <hidsdi.h>
#include <hidpi.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
bool deferred=false,fail_thread=false,fail_window=false,fail_raw=false,fail_restore=false,fail_names=false;
bool fail_hook=false,stop_on_name=false;
bool window_available=true;
unsigned posts=0,window_messages=0;
ULONG raw_hat=6;
unsigned threads=0,waits=0,closes=0,windows=0,raw_adds=0,raw_removes=0,names=0,original_keys=0,hooks=0;
LONG_PTR window_proc=1;
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
#include "game/title.cpp"
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
core::HookCallbackGate& non_audio_hook_gate() {static core::HookCallbackGate gate;return gate;}
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
ScoreInfoPianoDetailUpdateScope::ScoreInfoPianoDetailUpdateScope() noexcept {std::abort();}
ScoreInfoPianoDetailUpdateScope::~ScoreInfoPianoDetailUpdateScope(){std::abort();}
bool scoreinfo_overlay_row_matches_playback(const void*,const PlaybackSnapshot&){std::abort();}
bool refresh_active_scoreinfo_overlay_profile(){std::abort();}
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
}
void install() {
    require(install_title_hooks({module()}),"hook publication");
    require(hooks==4 && !names && !threads && !windows && !raw_adds,"early install performed service work");
}
}
int main() {
    try {
        reset();install();
        FNameValue key{101,0};
        require(player_input_key_detour(nullptr,&key,0,1,true) && original_keys==1,"early input original");
        require(!g_difficulty_event_providers.snapshot().decrement.player_input_held,"early custom input");
        deferred=true;
        require(!start_title_input_service(reinterpret_cast<HMODULE>(4)),"wrong installation module");
        require(start_title_input_service(module()),"deferred startup");
        require(start_title_input_service(module()) && names==10 && threads==1 && windows==1 && raw_adds==1,"startup not once");
        require(player_input_key_detour(nullptr,&key,0,1,true),"active key original");
        require(g_difficulty_event_providers.snapshot().decrement.player_input_held,"resolved key lost");
        require(player_input_key_detour(nullptr,&key,1,0,true),"release original");
        require(!g_difficulty_event_providers.snapshot().decrement.player_input_held,"release lost");
        require(shutdown_title().ok() && waits==1 && closes==1 && raw_removes==1 && window_proc==1,"service stop");
        require(!start_title_input_service(module()),"stopped service resurrected");

        reset();install();deferred=true;window_available=false;
        require(start_title_input_service(module()) && !g_game_window,"no-window startup was fatal");
        require(start_title_input_service(module()) && names==10 && threads==1 && windows==1 && !raw_adds,"pending service restarted");
        auto refresh=make_profile_list_callbacks().post_refresh;
        require(!refresh(1) && !posts && windows==2,"pending refresh incorrectly posted");
        window_available=true;
        require(refresh(2) && posts==1 && raw_adds==1 && names==10 && threads==1,"late window attachment");
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
        require(!stopped_refresh(1) && windows==1 && names==10 && threads==1
            && !start_title_input_service(module()),"pending service revived after stop");
        for(int failure=0;failure<2;++failure) {
            reset();install();deferred=true;window_available=false;
            require(start_title_input_service(module()),"lazy failure setup");
            window_available=true;fail_window=failure==0;fail_raw=failure==1;
            auto failed_refresh=make_profile_list_callbacks().post_refresh;
            require(!failed_refresh(1) && !posts && !g_difficulty_input_thread_running,"late failure admitted");
            require(!failed_refresh(2) && windows==2 && names==10 && threads==1
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
        std::cout<<"title input service selftest passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
