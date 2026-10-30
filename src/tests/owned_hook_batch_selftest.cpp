#include "core/hooks.h"
#include <MinHook.h>
#include <array>
#include <cstdlib>
#include <string>
#include <iostream>
#include <string_view>
#include <thread>

namespace core=ff7r::piano::core;
namespace {
void check(bool ok){if(!ok)std::abort();}
struct Entry {void* target=nullptr;bool enabled=false,queued=false;};
constexpr unsigned kHooks=12,kLast=3*kHooks+1;
std::array<Entry,kHooks> entries;
std::array<void*,kHooks> originals{};
unsigned creates=0,queues=0,applies=0,cancels=0,enables=0,uninitializes=0;
unsigned stock_calls=0;
using Stock=int(*)();
Stock exposed_suffix=nullptr;
int stock(){check(entries.back().target!=nullptr);++stock_calls;return 42;}
int create_failure=-1,queue_failure=-1,apply_failure=-1,cancel_failure=-1,remove_failure=-1;
Entry& find(void* target){for(auto& e:entries)if(e.target==target)return e;std::abort();}
unsigned index(Entry& entry){return static_cast<unsigned>(&entry-entries.data());}
void all_prepared(){check(creates==kHooks);for(auto original:originals)check(original);}
}
MH_STATUS WINAPI MH_Initialize(){return MH_OK;}
MH_STATUS WINAPI MH_Uninitialize(){++uninitializes;return MH_OK;}
const char* WINAPI MH_StatusToString(MH_STATUS){return "injected";}
MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID,LPVOID* original){
    const auto i=creates++;if(static_cast<int>(i)==create_failure)return MH_ERROR_MEMORY_ALLOC;
    check(i<entries.size());entries[i]={target,false,false};*original=reinterpret_cast<void*>(&stock);return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID){++enables;std::abort();}
MH_STATUS WINAPI MH_DisableHook(LPVOID target){auto& e=find(target);e.enabled=e.queued=false;return MH_OK;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID target){auto& e=find(target);if(static_cast<int>(index(e))==remove_failure)return MH_ERROR_MEMORY_PROTECT;e={};return MH_OK;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID target){
    all_prepared();auto& e=find(target);const auto i=queues++;check(i==index(e));
    if(static_cast<int>(i)==queue_failure)return MH_ERROR_NOT_CREATED;
    e.queued=true;return MH_OK;
}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID target){
    ++cancels;auto& e=find(target);
    if(static_cast<int>(index(e))==cancel_failure)return MH_ERROR_NOT_CREATED;
    e.queued=false;return MH_OK;
}
MH_STATUS WINAPI MH_ApplyQueued(){
    ++applies;all_prepared();check(queues==kHooks);
    // Model Freeze relocating an IP into the last queued trampoline before patching.
    if(apply_failure>=0)exposed_suffix=reinterpret_cast<Stock>(originals.back());
    for(unsigned i=0;i<entries.size();++i){check(entries[i].queued);if(static_cast<int>(i)==apply_failure)return MH_ERROR_MEMORY_PROTECT;entries[i].enabled=true;}
    return MH_OK;
}
int main(int argc,char** argv){
    const bool uncertain=argc>1 && std::string_view(argv[1])=="cancel_failure";
    const bool failed_apply=argc>1 && std::string_view(argv[1])=="apply_failure";
    const bool failed_remove=argc>1 && std::string_view(argv[1])=="remove_failure";
    const unsigned first=uncertain?kLast:failed_apply?2*kHooks+1+std::stoul(argv[2]):failed_remove?2*kHooks:0;
    const unsigned last=argc>1?first:2*kHooks;
    std::string error;check(core::initialize_hooks(error));
    // Success, every prepare/queue boundary, every apply prefix, and remove retention.
    for(unsigned trial=first;trial<=last;++trial){
        creates=queues=applies=cancels=enables=0;entries={};originals={};
        create_failure=trial>=1 && trial<=kHooks?static_cast<int>(trial-1):-1;
        queue_failure=trial>kHooks && trial<=2*kHooks?static_cast<int>(trial-kHooks-1):-1;
        apply_failure=trial>2*kHooks?trial==kLast?1:static_cast<int>(trial-2*kHooks-1):-1;
        cancel_failure=uncertain?1:-1;remove_failure=failed_remove?1:-1;
        std::array<core::RawRvaHook,kHooks> hooks;
        std::array<core::HookInstallTrace,kHooks> traces{};
        core::HookBatchTrace trace;
        std::array<unsigned char,kHooks> bytes;bytes.fill(0x90);auto module=reinterpret_cast<HMODULE>(bytes.data());
        bool prepared=true,activated=false;
        {
            core::OwnedHookBatch batch;check(batch.valid());
            core::OwnedHookBatch nested;check(!nested.valid());
            std::thread contender([&]{core::RawRvaHook other;void* original=nullptr;std::string busy;
                check(!other.install(module,0,{0x90},reinterpret_cast<void*>(1),&original,busy));
                check(!core::shutdown_hooks() && !core::initialize_hooks(busy));});contender.join();
            for(unsigned i=0;i<hooks.size();++i){traces[i].id="owned";
                if(!batch.prepare(hooks[i],module,i,{0x90},reinterpret_cast<void*>(1),&originals[i],&traces[i])){prepared=false;break;}}
            if(prepared)activated=batch.apply(&trace);else check(!batch.apply(&trace));
            if(!activated){check(batch.cancel(&trace)==!uncertain);check(!batch.cancel(&trace));}
        }
        check(!enables && activated==(trial==0));
        check(applies==(trial==0 || trial>2*kHooks?1u:0u));
        check(queues==(create_failure>=0?0u:queue_failure>=0?static_cast<unsigned>(queue_failure+1):kHooks));
        check(cancels==(trial>kHooks?kHooks:0u));
        check(trace.apply.returned==(applies!=0));
        if(applies)check(trace.apply.status==(trial==0?MH_OK:MH_ERROR_MEMORY_PROTECT) && trace.apply.end>=trace.apply.begin);
        if(trial>kHooks){check(trace.cancellation_attempted && trace.cancellation_completed==!uncertain);for(unsigned i=0;i<kHooks;++i)check(traces[i].cancel.returned);}
        for(unsigned i=0;i<kHooks;++i){
            check(!traces[i].enable.returned);
            if(trial>kHooks)check(entries[i].queued==(uncertain && i==1));
            if(applies && !activated)check(entries[i].enabled==(static_cast<int>(i)<apply_failure));
            const bool retained=applies && !activated || (remove_failure>=0 && i==1);
            check(hooks[i].remove()==!retained);
            if(retained)check(hooks[i].installed() && originals[i]);
        }
        if(queue_failure>=0)check(trace.failed_queue_hook && traces[queue_failure].queue.status==MH_ERROR_NOT_CREATED);
        if(applies && !activated){
            check(trace.process_lifetime_retained && core::hook_trampolines_retained_for_process_lifetime());
            check(!entries.back().enabled && exposed_suffix && !stock_calls);
            core::HookCallbackGate gate;
            bool cleared=false;std::vector<core::HookTeardownOperation> teardown;
            for(auto& hook:hooks)teardown.push_back(core::teardown_operation(hook));
            const auto cleanup=core::shutdown_gated_hooks(gate,teardown,{},[&]{cleared=true;originals={};});
            check(!gate.accepting() && cleanup.hooks_disabled && cleanup.callbacks_drained
                && cleanup.process_lifetime_retained && !cleanup.ok() && !cleanup.hooks_removed
                && !cleanup.state_cleared && !cleared && originals.back());
            check(!core::shutdown_hooks() && uninitializes==0);
            core::OwnedHookBatch blocked;check(!blocked.valid());
            check(!blocked.apply() && !hooks.back().remove());
            check(exposed_suffix()==42 && stock_calls==1); // Resume after attempted cleanup.
            return 0;
        }
        check(!trace.process_lifetime_retained && !core::hook_trampolines_retained_for_process_lifetime());
        if(remove_failure>=0){
            check(!core::shutdown_hooks());core::OwnedHookBatch blocked;check(!blocked.valid());
        }
        remove_failure=-1;if(hooks[1].installed())check(hooks[1].remove());
    }
    if(!uncertain)check(core::shutdown_hooks());
    check(uninitializes==(uncertain?0u:1u));
    std::cout<<"owned_hook_batch_selftest: passed\n";
}
