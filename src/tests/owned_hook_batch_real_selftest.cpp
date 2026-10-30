#include "core/hooks.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
namespace core=ff7r::piano::core;
namespace {
void check(bool ok){if(!ok)std::abort();}
using Fn=int(*)();
std::array<Fn,2> originals{};
int first(){return originals[0]()+1;}
int second(){return originals[1]()+2;}
int unrelated(){return 99;}
}
int main(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));check(image);
    constexpr std::array<unsigned char,6> code{0xb8,42,0,0,0,0xc3};
    for(unsigned i=0;i<3;++i)std::memcpy(image+i*64,code.data(),code.size());
    check(FlushInstructionCache(GetCurrentProcess(),image,4096));
    auto module=reinterpret_cast<HMODULE>(image);std::string error;
    check(core::initialize_hooks(error));
    core::RawRvaHook outside;void* outside_original=nullptr;
    check(outside.install(module,128,{code.begin(),code.end()},reinterpret_cast<void*>(&unrelated),&outside_original,error));
    std::array<core::RawRvaHook,2> hooks;
    std::array<core::HookInstallTrace,2> traces{};core::HookBatchTrace trace;
    {
        core::OwnedHookBatch batch;check(batch.valid());
        check(batch.prepare(hooks[0],module,0,{code.begin(),code.end()},reinterpret_cast<void*>(&first),reinterpret_cast<void**>(&originals[0]),&traces[0]));
        check(batch.prepare(hooks[1],module,64,{code.begin(),code.end()},reinterpret_cast<void*>(&second),reinterpret_cast<void**>(&originals[1]),&traces[1]));
        check(originals[0]()==42 && originals[1]()==42);
        check(reinterpret_cast<Fn>(image)()==42 && reinterpret_cast<Fn>(image+64)()==42);
        check(batch.apply(&trace));
    }
    check(trace.apply.returned && trace.apply.status==0);
    for(const auto& t:traces)check(t.create.returned && t.queue.returned && !t.enable.returned && t.outcome==core::HookInstallOutcome::Installed);
    check(reinterpret_cast<Fn>(image)()==43 && reinterpret_cast<Fn>(image+64)()==44 && reinterpret_cast<Fn>(image+128)()==99);
    for(auto& hook:hooks)check(hook.remove());
    check(reinterpret_cast<Fn>(image+128)()==99 && outside.remove() && core::shutdown_hooks());
    check(VirtualFree(image,0,MEM_RELEASE));
    std::cout<<"owned_hook_batch_real_selftest: passed\n";
}
