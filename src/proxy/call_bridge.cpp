#include "proxy/call_bridge.h"
#include <windows.h>
#include <atomic>
#include <intrin.h>

namespace ff7rp::proxy {
extern "C" void ff7rp_proxy_call_thunk();
extern "C" unsigned char ff7rp_proxy_call_dispatch_return;
extern "C" { std::uintptr_t ff7rp_proxy_call_resume=0; }
namespace call_detail {
struct State {
    std::atomic<CallStage> stage{CallStage::Empty};
    std::atomic<CallError> error{CallError::None};
    std::atomic<bool> retained{false},invoked{false},succeeded{false};
    CallProof proof{};
    std::array<std::uint8_t,5> patch{};
    DWORD protection=0;
    void* relay=nullptr;
    CallInitializer callback=nullptr;
    void* context=nullptr;
    EntryCompletionObserver observer=nullptr;
    const CallScope* scope=nullptr;
};
constinit State state;
struct Gateway {
    decltype(&VirtualAlloc) allocate=VirtualAlloc;
    decltype(&VirtualFree) free=VirtualFree;
    decltype(&VirtualProtect) protect=VirtualProtect;
    decltype(&WriteProcessMemory) write=WriteProcessMemory;
    decltype(&FlushInstructionCache) flush=FlushInstructionCache;
};
const Gateway native{};
#ifdef FF7RP_OWNED_CALL_SELFTEST
const Gateway* test_gateway=nullptr;
bool test_private_image=false;
#endif
const Gateway& api() noexcept {
#ifdef FF7RP_OWNED_CALL_SELFTEST
    if(test_gateway)return *test_gateway;
#endif
    return native;
}
bool copy(std::uintptr_t from,void* to,std::size_t size) noexcept {
    SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(from),to,size,&n) && n==size;
}
bool write(std::uintptr_t to,const void* from,std::size_t size) noexcept {
    SIZE_T n=0;return api().write(GetCurrentProcess(),reinterpret_cast<void*>(to),from,size,&n) && n==size;
}
bool same(const std::uint8_t* a,const std::uint8_t* b,std::size_t n) noexcept {
    for(std::size_t i=0;i<n;++i)if(a[i]!=b[i])return false;return true;
}
bool code(std::uintptr_t address,std::uintptr_t owner) noexcept {
    MEMORY_BASIC_INFORMATION m{};
    if(VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))!=sizeof(m)
        || reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=owner || m.State!=MEM_COMMIT
        || m.Protect!=PAGE_EXECUTE_READ)return false;
#ifdef FF7RP_OWNED_CALL_SELFTEST
    if(test_private_image && owner==state.proof.root.image)return m.Type==MEM_PRIVATE;
#endif
    return m.Type==MEM_IMAGE;
}
bool window(bool patched) noexcept {
    const auto& p=state.proof.profile;
    std::array<std::uint8_t,64> bytes{};
    if(!copy(state.proof.root.image+p.call_rva,bytes.data(),p.window_size))return false;
    for(unsigned i=0;i<p.window_size;++i)
        if(bytes[i]!=(patched && i<5?state.patch[i]:p.window[i]))return false;
    return true;
}
bool protect(std::uintptr_t at,std::size_t size,DWORD next,DWORD& old) noexcept {
    return api().protect(reinterpret_cast<void*>(at),size,next,&old)!=FALSE;
}
bool flush(std::uintptr_t at,std::size_t size) noexcept {
    return api().flush(GetCurrentProcess(),reinterpret_cast<void*>(at),size)!=FALSE;
}
bool restore() noexcept {
    const auto site=state.proof.return_pc-5;DWORD old=0,ignored=0;
    if(!protect(site,5,PAGE_EXECUTE_READWRITE,old))return false;
    const bool wrote=write(site,state.proof.profile.window.data(),5);
    const bool verified=window(false);
    const bool flushed=flush(site,5);
    const bool protected_ok=protect(site,5,state.protection,ignored);
    return wrote && verified && flushed && protected_ok && code(site,state.proof.root.image);
}
bool reject(CallError error,bool uncertain=false) noexcept {
    if(uncertain && state.relay)state.retained.store(true,std::memory_order_release);
    state.error.store(error,std::memory_order_relaxed);
    state.stage.store(uncertain?CallStage::FailedRetained:CallStage::Rejected,std::memory_order_release);
    return false;
}
bool extent(std::uintptr_t image,std::uint32_t& size) noexcept {
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!copy(image,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<64 || dos.e_lfanew>0x100000
        || !copy(image+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE
        || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    size=nt.OptionalHeader.SizeOfImage;return size>=4096;
}
// CURRENT context only: no supplied PCs, stack words, cached proof or test-success seam.
__declspec(noinline) bool current_chain() noexcept {
    __try {
        CONTEXT c{};RtlCaptureContext(&c);
        bool thunk=false;std::uint32_t own_size=0;
        if(!extent(state.proof.root.proxy,own_size))return false;
        for(unsigned depth=0;depth<24;++depth) {
            DWORD64 image=0;const auto* raw=RtlLookupFunctionEntry(c.Rip,&image,nullptr);
            RUNTIME_FUNCTION row{};
            if(!raw || !copy(reinterpret_cast<std::uintptr_t>(raw),&row,sizeof(row)))return false;
            const auto limit=image==state.proof.root.proxy?own_size:
                image==state.proof.root.image?state.proof.root.profile.image_size:0;
            if(limit<4 || row.BeginAddress>=row.EndAddress || row.EndAddress>limit || row.UnwindData>limit-4
                || c.Rip<image+row.BeginAddress || c.Rip>=image+row.EndAddress || !code(c.Rip,image))return false;
            if(thunk) {
                const auto site=state.proof.return_pc-5;
                return image==state.proof.root.image && c.Rip==state.proof.return_pc
                    && image+row.BeginAddress<=site && state.proof.return_pc<image+row.EndAddress;
            }
            if(image!=state.proof.root.proxy)return false;
            if(c.Rip==reinterpret_cast<DWORD64>(&ff7rp_proxy_call_dispatch_return)) {
                const auto entry=reinterpret_cast<DWORD64>(&ff7rp_proxy_call_thunk);
                if(entry<image+row.BeginAddress || entry>=image+row.EndAddress)return false;
                thunk=true;
            }
            const auto old_rsp=c.Rsp;PVOID handler=nullptr;DWORD64 establisher=0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,c.Rip,&row,&c,&handler,&establisher,nullptr);
            if(c.Rsp<=old_rsp || c.Rsp-old_rsp>0x10000)return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
} // namespace call_detail

CallStatus owned_call_status() noexcept {
    using namespace call_detail;
    const auto stage=state.stage.load(std::memory_order_acquire);
    return {stage,state.error.load(),state.retained.load(),state.invoked.load(),state.succeeded.load()};
}
const CallScope* active_call_scope() noexcept {
    using namespace call_detail;
    if(state.stage.load(std::memory_order_acquire)!=CallStage::Consuming
        || GetCurrentThreadId()!=state.proof.root.entry_thread)return nullptr;
    return state.scope;
}
bool CallScope::capture(CallProof& proof) const noexcept {
    if(active_call_scope()!=this)return false;
    proof=call_detail::state.proof;return true;
}
bool arm_owned_call(const EntryScope& root,const CallProfile& profile,CallInitializer callback,
    void* context,EntryCompletionObserver observer) noexcept {
    using namespace call_detail;
    // Reject copied/worker/reentrant roots before consuming the one-shot attempt.
    EntryProof proof{};
    if(active_entry_scope()!=&root || !root.capture(proof) || !proof.restored
        || proof.entry_thread!=GetCurrentThreadId())return false;
    auto expected=CallStage::Empty;
    if(!state.stage.compare_exchange_strong(expected,CallStage::Arming,std::memory_order_acq_rel))return false;
    if(!callback || profile.window_size<5 || profile.window_size>64 || profile.window[0]!=0xe8
        || profile.call_rva<0x1000 || profile.target_rva<0x1000
        || profile.window_size>proof.profile.image_size
        || profile.call_rva>proof.profile.image_size-profile.window_size || profile.target_rva>=proof.profile.image_size)
        return reject(CallError::Profile);
    std::int32_t delta=0;
    for(unsigned i=0;i<4;++i)reinterpret_cast<std::uint8_t*>(&delta)[i]=profile.window[i+1];
    if(static_cast<std::int64_t>(profile.call_rva)+5+delta!=profile.target_rva)return reject(CallError::Profile);
    state.proof={proof,profile,proof.image+profile.call_rva+5};
    if(!code(state.proof.return_pc-5,proof.image) || !code(proof.image+profile.call_rva+profile.window_size-1,proof.image)
        || !code(proof.image+profile.target_rva,proof.image)
        || !code(reinterpret_cast<std::uintptr_t>(&ff7rp_proxy_call_thunk),proof.proxy) || !window(false))return reject(CallError::Ownership);
    state.callback=callback;state.context=context;state.observer=observer;
    state.protection=PAGE_EXECUTE_READ;
    const auto site=state.proof.return_pc-5,aligned=site & ~std::uintptr_t(0xffff);
    for(std::uintptr_t distance=0;distance<0x7fff0000 && !state.relay;distance+=0x10000) {
        for(unsigned side=0;side<2 && !state.relay;++side) {
            if((side && aligned<distance) || (!side && aligned>UINTPTR_MAX-distance))continue;
            const auto address=side?aligned-distance:aligned+distance;
            state.relay=api().allocate(reinterpret_cast<void*>(address),4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        }
    }
    if(!state.relay)return reject(CallError::Allocation);
    const auto relay=reinterpret_cast<std::uintptr_t>(state.relay);
    const auto relative=static_cast<std::int64_t>(relay)-static_cast<std::int64_t>(site+5);
    std::array<std::uint8_t,14> jump{0xff,0x25,0,0,0,0};
    const auto target=reinterpret_cast<std::uintptr_t>(&ff7rp_proxy_call_thunk);
    for(unsigned i=0;i<8;++i)jump[6+i]=static_cast<std::uint8_t>(target>>(i*8));
    DWORD old=0,ignored=0;
    if(relative<INT32_MIN || relative>INT32_MAX || !write(relay,jump.data(),jump.size())
        || !protect(relay,4096,PAGE_EXECUTE_READ,old) || !flush(relay,jump.size())) {
        if(!api().free(state.relay,0,MEM_RELEASE))return reject(CallError::Preparation,true);
        state.relay=nullptr;return reject(CallError::Preparation);
    }
    const auto rel32=static_cast<std::int32_t>(relative);state.patch[0]=0xe8;
    for(unsigned i=0;i<4;++i)state.patch[i+1]=reinterpret_cast<const std::uint8_t*>(&rel32)[i];
    ff7rp_proxy_call_resume=proof.image+profile.target_rva;
    if(!protect(site,5,PAGE_EXECUTE_READWRITE,old)) {
        if(!api().free(state.relay,0,MEM_RELEASE))return reject(CallError::Patch,true);
        state.relay=nullptr;return reject(CallError::Patch);
    }
    if(old!=state.protection || !window(false)) {
        if(!protect(site,5,old,ignored))return reject(CallError::Ownership,true);
        if(!api().free(state.relay,0,MEM_RELEASE))return reject(CallError::Ownership,true);
        state.relay=nullptr;return reject(CallError::Ownership);
    }
    state.retained.store(true,std::memory_order_release); // Before even a partial CALL write.
    const bool wrote=write(site,state.patch.data(),5),verified=window(true),flushed=flush(site,5);
    const bool protected_ok=protect(site,5,state.protection,ignored);
    if(!wrote || !verified || !flushed || !protected_ok) {
        const bool restored=restore();return reject(CallError::Patch,!restored);
    }
    state.stage.store(CallStage::Armed,std::memory_order_release);return true;
}
struct CallBridgeAccess {
    static std::uintptr_t dispatch(const void* caller) noexcept {
        using namespace call_detail;
        if(caller!=&ff7rp_proxy_call_dispatch_return)return 0;
        auto expected=CallStage::Armed;
        if(!state.stage.compare_exchange_strong(expected,CallStage::Consuming,std::memory_order_acq_rel))return 0;
        EntryProof completed{};
        if(GetCurrentThreadId()!=state.proof.root.entry_thread || !completed_entry_proof(completed)
            || completed.image!=state.proof.root.image || completed.proxy!=state.proof.root.proxy
            || completed.entry!=state.proof.root.entry || completed.entry_thread!=state.proof.root.entry_thread
            || !current_chain() || !window(true)) {reject(CallError::Unwind,true);return 0;}
        if(!restore()) {reject(CallError::Restore,true);return 0;}
        CallScope scope;state.scope=&scope;state.invoked.store(true);
        state.succeeded.store(state.callback(scope,state.context));state.scope=nullptr;
        if(!state.succeeded.load())state.error.store(CallError::Callback);
        state.stage.store(CallStage::Completed,std::memory_order_release);
        if(state.observer)state.observer();
        return ff7rp_proxy_call_resume;
    }
};
extern "C" __declspec(noinline) std::uintptr_t ff7rp_proxy_call_dispatch() noexcept {
    return CallBridgeAccess::dispatch(_ReturnAddress());
}
} // namespace ff7rp::proxy
