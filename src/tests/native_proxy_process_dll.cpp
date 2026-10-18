#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "proxy/entry_bridge.h"
#include "native_proxy_process_protocol.h"
#include "native_proxy_process_profile.generated.h"

namespace p=ff7rp::proxy;
namespace {
constinit NativeProxyProcessReport report{};
constinit bool in_dllmain=false;
constinit HMODULE self_module=nullptr;

bool initialize(const p::EntryScope& scope,void*) noexcept {
    ++report.callback_count;report.callback_thread=GetCurrentThreadId();
    p::EntryProof proof;
    if(!in_dllmain)report.callback_checks|=1;
    if(p::active_entry_scope()==&scope)report.callback_checks|=2;
    if(scope.capture(proof)) {
        if(proof.restored && proof.callback_invoked)report.callback_checks|=4;
        if(proof.attach_thread==report.attach_thread && proof.entry_thread==report.callback_thread
            && proof.attach_thread==proof.entry_thread)report.callback_checks|=8;
        if(proof.image==reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))
            && proof.proxy==reinterpret_cast<std::uintptr_t>(self_module)
            && proof.entry==proof.image+kHostProfile.entry_rva)report.callback_checks|=16;
        unsigned char code[p::kMaxEntrySignature];SIZE_T copied=0;
        bool same=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(proof.entry),code,sizeof(code),&copied)
            && copied==sizeof(code);
        for(std::size_t i=0;i<sizeof(code) && same;++i)same=code[i]==kHostProfile.signature[i];
        if(same)report.callback_checks|=32;
    }
    p::EntryProof completed;
    if(!p::completed_entry_proof(completed) && p::entry_bridge_status().stage==p::EntryStage::Initializing)
        report.callback_checks|=64;
    return report.callback_checks==kNativeProxyProcessCallbackChecks;
}
}

extern "C" int fixture_probe(NativeProxyProcessReport* out) noexcept {
    if(!out)return 0;
    const auto status=p::entry_bridge_status();
    report.stage=static_cast<std::uint32_t>(status.stage);report.error=static_cast<std::uint32_t>(status.error);
    report.retention=static_cast<std::uint32_t>(status.retention);
    report.probe_in_dllmain=in_dllmain;report.active_scope_at_probe=p::active_entry_scope()!=nullptr;
    p::EntryProof proof;
    if(p::completed_entry_proof(proof) && proof.restored && proof.callback_invoked && proof.callback_succeeded
        && proof.attach_thread==report.attach_thread && proof.entry_thread==report.callback_thread
        && proof.image==reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)))report.completed_proof_checks=1;
    *out=report;return 1;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved) {
    in_dllmain=true;
    if(reason==DLL_PROCESS_ATTACH) {
        self_module=module;++report.attach_count;report.attach_thread=GetCurrentThreadId();
        report.process_start_reserved=reserved!=nullptr;
        const auto result=p::arm_process_start_entry(module,reason,reserved,kHostProfile,&initialize,nullptr);
        report.stage=static_cast<std::uint32_t>(result.stage);report.error=static_cast<std::uint32_t>(result.error);
        report.retention=static_cast<std::uint32_t>(result.retention);
    }
    in_dllmain=false;
    // An armed or uncertain bridge must never be unloaded by a FALSE attach return.
    return TRUE;
}
