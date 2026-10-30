#include "core/hooks.h"
#include <MinHook.h>
#include <array>
#include <cstdlib>
#include <iostream>

namespace core=ff7r::piano::core;
namespace {
void check(bool value){if(!value)std::abort();}
core::HookInstallTrace* active=nullptr;
MH_STATUS create_status=MH_OK,enable_status=MH_OK,remove_status=MH_OK;
unsigned creates=0,enables=0,removes=0;
}
MH_STATUS WINAPI MH_Initialize(){return MH_OK;}
MH_STATUS WINAPI MH_Uninitialize(){return MH_OK;}
const char* WINAPI MH_StatusToString(MH_STATUS){return "injected";}
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID* original){
    ++creates;check(active && active->create.begin>0 && !active->create.returned && !active->enable.begin);
    if(create_status==MH_OK)*original=reinterpret_cast<void*>(42);
    return create_status;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID){
    ++enables;check(active->create.returned && active->create.status==MH_OK && active->enable.begin>=active->create.end && !active->enable.returned);
    return enable_status;
}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){++removes;return remove_status;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_ApplyQueued(){std::abort();}

int main(){
    std::string error;check(core::initialize_hooks(error));
    std::array<unsigned char,16> bytes{0x90};
    const auto module=reinterpret_cast<HMODULE>(bytes.data());
    for(unsigned mode=0;mode<4;++mode){
        core::RawRvaHook hook;core::HookInstallTrace trace{};trace.id="stable_hook";active=&trace;
        create_status=mode==1?MH_ERROR_MEMORY_ALLOC:MH_OK;
        enable_status=mode>=2?MH_ERROR_MEMORY_PROTECT:MH_OK;
        remove_status=mode==3?MH_ERROR_MEMORY_PROTECT:MH_OK;
        void* original=nullptr;
        check(hook.install(module,0,{0x90},reinterpret_cast<void*>(1),&original,error,&trace)==(mode==0));
        check(trace.create.returned && trace.create.status==create_status && trace.create.end>=trace.create.begin);
        check(trace.enable.returned==(mode!=1));
        if(mode!=1)check(trace.enable.status==enable_status && trace.enable.end>=trace.enable.begin);
        check(trace.outcome==(mode==0?core::HookInstallOutcome::Installed:mode==1?core::HookInstallOutcome::CreateFailed:core::HookInstallOutcome::EnableFailed));
        if(mode>=2)check(trace.cleanup_attempted && trace.cleanup_status==remove_status && trace.cleanup_retained==(mode==3));
        if(mode==3)check(hook.installed() && original==reinterpret_cast<void*>(42));
        remove_status=MH_OK;check(hook.remove());
    }
    check(creates==4 && enables==3 && removes==4);
    check(core::shutdown_hooks());
    std::cout<<"hook_install_trace_selftest: passed\n";
}
