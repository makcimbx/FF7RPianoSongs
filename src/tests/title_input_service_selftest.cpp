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
bool fail_trigger_names=false;
bool window_available=true,fail_post=false,wrong_window_thread=false;
UINT posted_message=0; WPARAM posted_wparam=0; LPARAM posted_lparam=0; HWND posted_window=nullptr;
unsigned posts=0,window_messages=0;
void require(bool,const char*);
ULONG raw_hat=6;
unsigned threads=0,waits=0,closes=0,windows=0,raw_adds=0,raw_removes=0,names=0,original_keys=0,hooks=0;
LONG_PTR window_proc=1;
bool ctrl=false, function=false, foreground=true, fail_feedback_stop=false, accept_toggle=true;
int configured_key=8;
int theme_function=6; bool theme_function_held=false;
std::uint64_t seat_token=0;
unsigned toggles=0,feedback=0,feedback_stops=0;
bool preview_focus=false,observed_preview_held=false,preview_chord=false,fail_preview_stop=false;
unsigned preview_cancels=0,preview_shutdowns=0;
WORD controller_buttons[4]{};
BYTE controller_left_triggers[4]{},controller_right_triggers[4]{};
SHORT WINAPI async_key(int key) {return ((key==VK_CONTROL && ctrl)
    || (key==VK_F1+configured_key-1 && configured_key && function)
    || (theme_function && key==VK_F1+theme_function-1 && theme_function_held))?SHORT(-32768):0;}
HWND WINAPI foreground_window(){return foreground?reinterpret_cast<HWND>(3):nullptr;}
DWORD WINAPI window_process(HWND,LPDWORD process){*process=GetCurrentProcessId();return GetCurrentThreadId()+(wrong_window_thread?1:0);}
DWORD WINAPI xinput(DWORD user,XINPUT_STATE* state){
    state->Gamepad.wButtons=controller_buttons[user];state->Gamepad.bLeftTrigger=controller_left_triggers[user];
    state->Gamepad.bRightTrigger=controller_right_triggers[user];return ERROR_SUCCESS;
}
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
BOOL WINAPI post_message(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    ++posts;if(fail_post)return FALSE;
    posted_window=window;posted_message=message;posted_wparam=wparam;posted_lparam=lparam;return TRUE;
}
LRESULT WINAPI original_window(WNDPROC,HWND,UINT,WPARAM,LPARAM){++window_messages;return 77;}
LRESULT WINAPI default_window(HWND,UINT,WPARAM,LPARAM){++window_messages;return 88;}
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
#define DefWindowProcW default_window
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
#undef DefWindowProcW
#undef GetRawInputData
#undef GetRawInputDeviceInfoW
#undef HidP_GetUsageValue

using namespace ff7r::piano::game;
namespace {
MenuSessionSnapshot theme_owner{},requested_owner{};
bool theme_held=false,reenter_dispatch=false;
bool observed_theme_previous=false,observed_theme_next=false,requested_previous=false;
unsigned theme_dispatches=0,theme_rejections=0,theme_samples=0;
unsigned theme_focus_losses=0;
std::uint64_t theme_sequence=0,theme_pending=0;
bool theme_preparation=false;
bool continuation_work=false;
}
// Unrelated UI/audio paths must not be reached by this startup/entry test.
namespace ff7r::piano::game {
namespace menu_theme_runtime {
std::uint64_t reserve_preparation_continuation() noexcept {
    require(!g_theme_dispatch_frame,"reservation only after actual private frame retirement");
    if(!continuation_work || theme_pending || !theme_owner)return 0;
    requested_owner=theme_owner;theme_preparation=true;return theme_pending=++theme_sequence;
}
void cancel_preparation_continuation(std::uint64_t ticket) noexcept {if(ticket==theme_pending)theme_pending=0;}
bool post_preparation_continuation(std::uint64_t ticket) noexcept {
    return ticket && ticket==theme_pending && same_menu_session(requested_owner,theme_owner) && post_input(ticket,true);
}
}
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
MenuSessionSnapshot capture_ready_menu_session() noexcept {
    if(!g_theme_dispatch)std::abort();return theme_owner;
}
bool same_menu_session(const MenuSessionSnapshot& a,const MenuSessionSnapshot& b,bool phase) noexcept {
    return a.generation==b.generation && (!phase || a.phase==b.phase) && a.controller==b.controller
        && a.list==b.list && a.widget==b.widget && a.widget_identity.internal_index==b.widget_identity.internal_index
        && a.widget_identity.serial_number==b.widget_identity.serial_number && a.list_ownership==b.list_ownership;
}
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
void* __fastcall name_ctor(FNameValue* output,const wchar_t* text,int32_t find) {
    ++names;require(deferred,"early GAME FNameCtor");
    require(find==0,"native key names use canonical FName_Find0");
    if(stop_on_name && names==1)(void)shutdown_title();
    const bool trigger = std::wstring_view(text)==L"Gamepad_LeftTrigger" || std::wstring_view(text)==L"Gamepad_RightTrigger";
    *output={(fail_names || (fail_trigger_names && trigger))?0u:100u+names,0};return output;
}
bool install_hook(const char* name,void** original) {
    ++hooks;*original=std::string_view(name)=="player_input_input_key"
        ?reinterpret_cast<void*>(&original_key):reinterpret_cast<void*>(1);return !fail_hook;
}
HMODULE module() {return reinterpret_cast<HMODULE>(0x10000000);}
void reset() {
    continuation_work=false;
    deferred=fail_thread=fail_window=fail_raw=fail_restore=fail_names=false;
    fail_hook=stop_on_name=false;
    fail_trigger_names=false;g_native_left_trigger_name=0;g_native_right_trigger_name=0;
    observed_theme_previous=observed_theme_next=requested_previous=false;
    std::memset(controller_left_triggers,0,sizeof(controller_left_triggers));
    std::memset(controller_right_triggers,0,sizeof(controller_right_triggers));
    window_available=true;posts=window_messages=0;raw_hat=6;
    threads=waits=closes=windows=raw_adds=raw_removes=names=original_keys=hooks=0;window_proc=1;
    g_input_service_state.store(TitleInputServiceState::Uninstalled);g_exe_module=nullptr;
    g_difficulty_input_thread=nullptr;g_difficulty_input_thread_running=false;g_game_window=nullptr;
    g_previous_window_proc=nullptr;g_raw_input_registration_attempted=false;
    g_native_dpad_left_name=0;g_native_dpad_right_name=0;
    g_native_left_thumb_name=0;g_native_right_thumb_name=0;
    g_difficulty_event_providers.reset_for_teardown();
    for(auto& value:g_native_piano_direction_names)value=0;
    g_test_install_title_hook=&install_hook;g_test_fname_ctor=&name_ctor;
    g_theme_entry=nullptr;g_theme_key=nullptr;g_theme_poll=nullptr;g_theme_dispatch=nullptr;g_theme_dispatch_frame=nullptr;
    g_theme_dispatch_active=false;
    g_theme_focus_lost=nullptr;theme_focus_losses=0;
    g_theme_window.store(nullptr);
    theme_owner={};requested_owner={};theme_held=reenter_dispatch=false;
    theme_dispatches=theme_rejections=theme_samples=0;theme_sequence=theme_pending=0;theme_preparation=false;
    fail_post=wrong_window_thread=theme_function_held=false;theme_function=6;
    posted_window=nullptr;posted_message=0;posted_wparam=0;posted_lparam=0;
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
int theme_key() noexcept {return theme_function;}
void theme_focus_lost() noexcept {++theme_focus_losses;theme_pending=0;}
void theme_poll(bool focused,bool keyboard,bool previous,bool next) noexcept {
    observed_theme_previous=previous;observed_theme_next=next;
    const bool held=keyboard||previous||next;
    ++theme_samples;const bool rising=held&&!theme_held;theme_held=held;
    if(!focused || !rising || (previous && (keyboard||next)) || theme_pending)return;
    requested_previous=previous;
    requested_owner=theme_owner;theme_pending=++theme_sequence;
    if(!menu_theme_runtime::post_input(theme_pending))theme_pending=0;
}
void theme_dispatch(const void* frame,std::uint64_t ticket,const ff7r::piano::core::HookCallbackGate::Lease&) noexcept {
    if(!theme_pending || ticket!=theme_pending)return;
    theme_pending=0;
    if(!menu_theme_runtime::qualify_owned_dispatch(frame,ticket,requested_owner,theme_preparation)){++theme_rejections;return;}
    if(reenter_dispatch) {
        (void)difficulty_window_proc(g_game_window,WM_NULL,0,0);
        require(!menu_theme_runtime::qualify_owned_dispatch(frame,ticket,requested_owner,theme_preparation),"reentrant normal message retires private frame");
        ++theme_rejections;return;
    }
    ++theme_dispatches;
}
void theme_input_tests() {
    reset();install();deferred=true;require(start_title_input_service(module()),"theme poll service");
    theme_owner={7,MenuSessionPhase::Ready,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),
        reinterpret_cast<void*>(3),{3,10},MenuListOwnership::Managed};
    menu_theme_runtime::bind_owned_input(nullptr,theme_key,theme_poll,theme_focus_lost,theme_dispatch);
    theme_function_held=true;poll_dynamic_difficulty_input_impl();require(!posts,"configured Fn alone does not post");
    ctrl=true;poll_dynamic_difficulty_input_impl();
    require(posts==1 && !theme_dispatches && posted_message==kThemeInputMessage,"actual poll queues private message, never dispatches inline");
    poll_dynamic_difficulty_input_impl();require(posts==1,"held chord posts once");
    require(difficulty_window_proc(posted_window,posted_message,posted_wparam,posted_lparam)==0
        && theme_dispatches==1 && !window_messages,"owned private message stack qualifies and is consumed");
    const auto* expired=g_theme_dispatch_frame;require(!expired
        && !menu_theme_runtime::qualify_owned_dispatch(expired,posted_wparam,requested_owner),"no retained title frame authority");
    FNameValue key{606,0};const auto old_posts=posts;
    require(player_input_key_detour(nullptr,&key,0,1,false) && original_keys==1 && posts==old_posts,
        "PlayerInput forwards once without a duplicate theme route");
    require(difficulty_window_proc(g_game_window,WM_APP+0x555,0,0)==77 && window_messages==1,"unknown messages forward exactly once");
    theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;poll_dynamic_difficulty_input_impl();
    ++theme_owner.generation;
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);
    require(theme_dispatches==1 && theme_rejections==1,"posted title frame cannot adopt a different generation");
    theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;fail_post=true;
    poll_dynamic_difficulty_input_impl();require(!theme_pending,"post failure does not retain a live request");fail_post=false;
    theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;poll_dynamic_difficulty_input_impl();
    wrong_window_thread=true;(void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);wrong_window_thread=false;
    require(theme_rejections==2,"actual window thread must match dispatch thread");
    theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;poll_dynamic_difficulty_input_impl();
    reenter_dispatch=true;(void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);reenter_dispatch=false;
    require(theme_rejections==3 && window_messages==2,"nested message forwards and retires outer dispatch");
    theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;foreground=false;
    const auto unfocused_posts=posts;poll_dynamic_difficulty_input_impl();foreground=true;poll_dynamic_difficulty_input_impl();
    require(posts==unfocused_posts,"actual poll suppresses a chord held while focus returns");
    theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;poll_dynamic_difficulty_input_impl();
    window_proc=1;(void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);
    window_proc=reinterpret_cast<LONG_PTR>(difficulty_window_proc);require(theme_rejections==4,"dispatch rejects a replaced live subclass");
    theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;poll_dynamic_difficulty_input_impl();
    (void)difficulty_window_proc(reinterpret_cast<HWND>(9),posted_message,posted_wparam,0);
    require(theme_rejections==5,"private message on the wrong HWND is not owning-stack authority");
    theme_function=12;theme_function_held=false;poll_dynamic_difficulty_input_impl();
    const auto before=posts;theme_function_held=true;poll_dynamic_difficulty_input_impl();
    require(posts==before+1,"actual poll uses configured F12");
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);
    theme_function=0;theme_function_held=false;poll_dynamic_difficulty_input_impl();
    theme_function_held=true;poll_dynamic_difficulty_input_impl();require(posts==before+1,"disabled theme key posts nothing");
    theme_function=6;
    for (const UINT loss : {UINT(WM_KILLFOCUS),UINT(WM_ACTIVATEAPP)}) {
        theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;poll_dynamic_difficulty_input_impl();
        const auto prior_posts=posts,prior_dispatches=theme_dispatches,prior_messages=window_messages;
        const auto prior_losses=theme_focus_losses,prior_cancels=preview_cancels;
        require(difficulty_window_proc(g_game_window,loss,FALSE,0)==77,"focus loss forwards original once");
        require(difficulty_window_proc(g_game_window,loss==WM_KILLFOCUS?WM_SETFOCUS:WM_ACTIVATEAPP,TRUE,0)==77,
            "focus regain forwards original once");
        require(window_messages==prior_messages+2 && theme_focus_losses==prior_losses+1
            && preview_cancels==prior_cancels+1,"both loss notifications revoke theme input, retain preview cancellation, regain does not revoke");
        (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);
        require(theme_dispatches==prior_dispatches,"loss/regain without polling invalidates queued dispatch");
        poll_dynamic_difficulty_input_impl();require(posts==prior_posts,"held return after notification posts nothing");
        theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;poll_dynamic_difficulty_input_impl();
        (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);
        require(theme_dispatches==prior_dispatches+1,"release/new edge dispatches after focus notifications");
    }
    foreground=false;theme_preparation=true;requested_owner=theme_owner;theme_pending=++theme_sequence;
    const auto before_maintenance=theme_dispatches;
    require(menu_theme_runtime::post_input(theme_pending,true) && posted_lparam==1,"maintenance purpose captured by actual private message");
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,posted_lparam);
    require(theme_dispatches==before_maintenance+1,"owned maintenance stack remains qualified without keyboard focus");
    theme_preparation=false;theme_pending=++theme_sequence;
    require(menu_theme_runtime::post_input(theme_pending,true),"purpose mismatch fixture posted");
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,posted_lparam);
    require(theme_dispatches==before_maintenance+1,"maintenance message cannot promote a gesture scope");
    theme_pending=++theme_sequence;require(menu_theme_runtime::post_input(theme_pending),"unfocused gesture fixture posted");
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);
    require(theme_dispatches==before_maintenance+1,"gesture source still requires foreground focus");
    foreground=true;theme_function_held=false;poll_dynamic_difficulty_input_impl();theme_function_held=true;poll_dynamic_difficulty_input_impl();
    const auto dispatched=theme_dispatches;
    require(shutdown_title().ok(),"theme input shutdown");
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);
    require(theme_dispatches==dispatched,"queued message after shutdown never dispatches");
    require(!menu_theme_runtime::post_input(99),"stopped service cannot queue");
}
void continuation_scheduler_tests() {
    auto start=[] {
        reset();install();deferred=true;
        require(start_title_input_service(module()),"continuation service");
        theme_owner={7,MenuSessionPhase::Ready,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),
            reinterpret_cast<void*>(3),{3,10},MenuListOwnership::Managed};
        menu_theme_runtime::bind_owned_input(nullptr,theme_key,theme_poll,theme_focus_lost,theme_dispatch);
        continuation_work=true;
    };
    start();menu_theme_runtime::wake_preparation_after_ready();
    require(posts==1 && theme_pending && !theme_samples && !theme_dispatches,
        "Ready leftover work posts metadata only, without polling or native dispatch");
    menu_theme_runtime::wake_preparation_after_ready();
    require(posts==1,"Ready first wake cannot duplicate an outstanding ticket");
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,posted_lparam);
    require(theme_dispatches==1 && posts==1 && !theme_pending,
        "a completed dispatch never posts its own successor, even with leftover maintenance");
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,posted_lparam);
    require(theme_dispatches==1 && posts==1,"consumed message cannot dispatch twice or replenish itself");
    require(shutdown_title().ok(),"cold Ready shutdown");
    start();
    {
        auto lease=non_audio_hook_gate().try_enter();
        ThemeDispatchFrame frame{g_game_window,1,theme_owner,lease};g_theme_dispatch_frame=&frame;
        menu_theme_runtime::wake_preparation_after_ready();
        require(!theme_pending && !posts,"no metadata wake inside actual dispatch frame");
        frame.retired=true;g_theme_dispatch_frame=nullptr;
    }
    menu_theme_runtime::wake_preparation_after_ready();const auto first=theme_pending;
    require(first && posts==1 && !theme_dispatches,"one exceptional wake has no timer or native work");
    const auto before=posts;
    require(difficulty_window_proc(g_game_window,WM_PAINT,0,0)==77
        && difficulty_window_proc(g_game_window,WM_KEYUP,VK_LEFT,0)==77 && posts==before,
        "paint and ordinary input forward without continuation draining");
    menu_theme_runtime::cancel_preparation_continuation(first);
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,posted_lparam);
    require(!theme_dispatches && posts==before,"cancelled ticket cannot execute or schedule work");
    menu_theme_runtime::wake_preparation_after_ready();const auto replacement=theme_pending;
    require(replacement!=first,"later real wake owns a fresh reservation");
    ++theme_owner.generation;
    (void)difficulty_window_proc(posted_window,posted_message,posted_wparam,posted_lparam);
    require(!theme_dispatches && theme_rejections,"reopened generation cannot revive old work");
    require(shutdown_title().ok(),"no asynchronous timer callback remains to drain at shutdown");
    start();fail_post=true;menu_theme_runtime::wake_preparation_after_ready();fail_post=false;
    require(!theme_pending,"post failure releases ticket for polling");
    theme_poll(true,true,false,false);require(theme_pending && posts==2,"post failure fallback accepts fresh ticket");
    require(shutdown_title().ok(),"post failure shutdown");

    const auto stopped_posts=posts;menu_theme_runtime::wake_preparation_after_ready();
    require(posts==stopped_posts,"stopped service cannot reserve or post maintenance");
    std::cout<<"theme metadata fallback one-ticket=1 automatic-successors=0 native-on-worker=0 stale/cancel/post/stop=checked\n";
}
void controller_theme_input_tests() {
    reset();install();deferred=true;theme_function=0;
    menu_theme_runtime::bind_owned_input(nullptr,theme_key,theme_poll,theme_focus_lost,theme_dispatch);
    require(start_title_input_service(module()) && names==14,"optional native trigger names resolve at startup with Find0");
    g_xinput_get_states={&xinput};
    theme_owner={7,MenuSessionPhase::Ready,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),
        reinterpret_cast<void*>(3),{3,10},MenuListOwnership::Managed};
    auto poll=[] {poll_dynamic_difficulty_input_impl();};
    auto dispatch=[] {(void)difficulty_window_proc(posted_window,posted_message,posted_wparam,0);};
    auto clear_xinput=[] {
        std::memset(controller_buttons,0,sizeof(controller_buttons));
        std::memset(controller_left_triggers,0,sizeof(controller_left_triggers));
        std::memset(controller_right_triggers,0,sizeof(controller_right_triggers));
    };
    auto native_key=[](std::uint32_t id,int event) {
        require(id!=0,"native key comes from actual startup resolution");
        const auto before=original_keys;FNameValue key{id,0};
        require(player_input_key_detour(nullptr,&key,event,1,true) && original_keys==before+1,
            "native controller input forwards original exactly once");
    };
    controller_buttons[0]=XINPUT_GAMEPAD_LEFT_THUMB;controller_left_triggers[1]=255;poll();
    require(!observed_theme_previous && !observed_theme_next && !theme_pending,"no cross-controller L3/trigger chord");
    controller_left_triggers[0]=XINPUT_GAMEPAD_TRIGGER_THRESHOLD;poll();require(!theme_pending,"SDK trigger threshold excludes noise");
    controller_left_triggers[0]=XINPUT_GAMEPAD_TRIGGER_THRESHOLD+1;poll();
    require(theme_pending && requested_previous && observed_theme_previous,"same-controller XInput previous with keyboard disabled");
    dispatch();const auto first=theme_dispatches;poll();require(!theme_pending && theme_dispatches==first,"held trigger no repeat");
    clear_xinput();poll();controller_buttons[0]=XINPUT_GAMEPAD_LEFT_THUMB;
    controller_left_triggers[0]=controller_right_triggers[0]=255;poll();
    require(observed_theme_previous && observed_theme_next && !theme_pending,"both XInput triggers conflict");
    controller_left_triggers[0]=0;poll();require(!theme_pending,"releasing one conflicting trigger cannot delay-switch");
    controller_right_triggers[0]=0;poll();controller_right_triggers[0]=255;poll();
    require(theme_pending && !requested_previous,"R2 repress is next while holding L3");dispatch();
    clear_xinput();poll();
    const auto left=g_native_left_trigger_name.load(),right=g_native_right_trigger_name.load();
    const auto l3=g_native_left_thumb_name.load();
    native_key(l3,0);controller_right_triggers[0]=255;poll();
    require(!theme_pending && !observed_theme_next,"native L3 cannot borrow XInput trigger");
    native_key(l3,1);controller_right_triggers[0]=0;controller_buttons[0]=XINPUT_GAMEPAD_LEFT_THUMB;
    native_key(left,0);poll();require(!theme_pending && !observed_theme_previous,"XInput L3 cannot borrow native trigger");
    clear_xinput();native_key(left,1);poll();native_key(l3,0);native_key(left,0);poll();
    require(theme_pending && requested_previous,"non-XInput native provider composes L3/L2");dispatch();
    native_key(left,2);poll();require(!theme_pending && g_difficulty_event_providers.left_trigger_held(),"native trigger Repeat retains held without repeat switch");
    native_key(left,1);poll();require(!observed_theme_previous,"native trigger Release clears chord");
    native_key(right,0);poll();require(theme_pending && !requested_previous,"native R2 press is next");dispatch();
    native_key(right,1);poll();native_key(left,0);native_key(right,0);poll();
    require(!theme_pending && observed_theme_previous && observed_theme_next,"both native triggers conflict");
    native_key(left,1);poll();require(!theme_pending,"native conflict release-one suppressed");
    native_key(right,1);poll();native_key(right,0);poll();require(theme_pending,"native release/repress works");dispatch();
    for (const auto message : {UINT(WM_KILLFOCUS),UINT(WM_ACTIVATEAPP),UINT(WM_INPUT_DEVICE_CHANGE)}) {
        native_key(l3,0);native_key(left,0);native_key(right,0);
        (void)difficulty_window_proc(g_game_window,message,message==WM_INPUT_DEVICE_CHANGE?GIDC_REMOVAL:FALSE,0);
        require(!g_difficulty_event_providers.left_trigger_held() && !g_difficulty_event_providers.right_trigger_held()
            && !g_difficulty_event_providers.left_thumbstick_held(),"focus/application/device paths reset native chord scalars together");
        poll();
    }
    native_key(left,0);require(shutdown_title().ok() && !g_difficulty_event_providers.left_trigger_held()
        && !g_native_left_trigger_name.load() && !g_native_right_trigger_name.load(),"teardown clears trigger state and resolved IDs");
    reset();install();deferred=true;fail_trigger_names=true;theme_function=0;
    menu_theme_runtime::bind_owned_input(nullptr,theme_key,theme_poll,theme_focus_lost,theme_dispatch);
    require(start_title_input_service(module()) && !g_native_left_trigger_name.load() && !g_native_right_trigger_name.load(),
        "missing native names disable only optional native trigger provider");
    g_xinput_get_states={&xinput};theme_owner={7,MenuSessionPhase::Ready,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),
        reinterpret_cast<void*>(3),{3,10},MenuListOwnership::Managed};
    controller_buttons[0]=XINPUT_GAMEPAD_LEFT_THUMB;controller_right_triggers[0]=255;poll();
    require(theme_pending && !requested_previous,"XInput remains available when optional native lookup fails");dispatch();
    require(shutdown_title().ok(),"optional trigger failure shutdown");
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
        theme_input_tests();
        continuation_scheduler_tests();
        controller_theme_input_tests();
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
