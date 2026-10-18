#include "proxy/entry_bridge.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <limits>

namespace ff7rp::proxy {
extern "C" void ff7rp_proxy_entry_thunk();
extern "C" void ff7rp_proxy_entry_dispatch_return();
extern "C" { std::uintptr_t ff7rp_proxy_entry_resume=0; }
struct EntryBridgeAccess { static std::uintptr_t dispatch() noexcept; };
namespace {
struct Region { std::uintptr_t allocation=0,begin=0; std::size_t size=0; DWORD state=0,type=0,protection=0; };
struct Gateway {
    void* (*main_image)() noexcept;
    DWORD (*thread)() noexcept;
    bool (*region)(std::uintptr_t,Region&) noexcept;
    bool (*read)(std::uintptr_t,void*,std::size_t) noexcept;
    bool (*write)(std::uintptr_t,const void*,std::size_t) noexcept;
    bool (*protect)(std::uintptr_t,std::size_t,DWORD,DWORD&) noexcept;
    bool (*flush)(std::uintptr_t,std::size_t) noexcept;
};
void* main_image() noexcept { return GetModuleHandleW(nullptr); }
DWORD thread() noexcept { return GetCurrentThreadId(); }
bool region(std::uintptr_t address,Region& out) noexcept {
    MEMORY_BASIC_INFORMATION m;
    if(VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))!=sizeof(m))return false;
    out={reinterpret_cast<std::uintptr_t>(m.AllocationBase),reinterpret_cast<std::uintptr_t>(m.BaseAddress),m.RegionSize,m.State,m.Type,m.Protect};return true;
}
bool read(std::uintptr_t address,void* out,std::size_t size) noexcept {
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&copied)!=FALSE && copied==size;
}
bool write(std::uintptr_t address,const void* in,std::size_t size) noexcept {
    SIZE_T copied=0;
    return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),in,size,&copied)!=FALSE && copied==size;
}
bool protect(std::uintptr_t address,std::size_t size,DWORD value,DWORD& old) noexcept {
    return VirtualProtect(reinterpret_cast<void*>(address),size,value,&old)!=FALSE;
}
bool flush(std::uintptr_t address,std::size_t size) noexcept {
    return FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),size)!=FALSE;
}
const Gateway native{main_image,thread,region,read,write,protect,flush};
#ifdef FF7RP_NATIVE_PROXY_ENTRY_BRIDGE_SELFTEST
const Gateway* test_gateway=nullptr;
const void* test_dispatch_caller=nullptr;
#endif
const Gateway& api() noexcept {
#ifdef FF7RP_NATIVE_PROXY_ENTRY_BRIDGE_SELFTEST
    if(test_gateway)return *test_gateway;
#endif
    return native;
}
static_assert(std::atomic<EntryStage>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
struct State {
    std::atomic<EntryStage> stage{EntryStage::Empty};
    std::atomic<EntryError> error{EntryError::None};
    std::atomic<bool> retained{false},stopped{false};
    EntryProof proof{};
    std::array<std::uint8_t,kEntryJumpBytes> jump{};
    DWORD original_protection=0;
    EntryInitializer initializer=nullptr;
    void* context=nullptr;
    const EntryScope* scope=nullptr;
};
constinit State state;
bool same(const void* lhs,const void* rhs,std::size_t n) noexcept {
    auto* a=static_cast<const volatile unsigned char*>(lhs);auto* b=static_cast<const volatile unsigned char*>(rhs);
    for(std::size_t i=0;i<n;++i)if(a[i]!=b[i])return false;return true;
}
bool matches(std::uintptr_t address,const void* expected,std::size_t n) noexcept {
    std::array<std::uint8_t,kMaxEntrySignature> bytes;
    return n<=bytes.size() && api().read(address,bytes.data(),n) && same(bytes.data(),expected,n);
}
bool published_entry_matches() noexcept {
    std::array<std::uint8_t,kMaxEntrySignature> bytes;
    const auto n=state.proof.profile.signature_size;
    if(!api().read(state.proof.entry,bytes.data(),n))return false;
    for(std::size_t i=0;i<n;++i)
        if(bytes[i]!=(i<kEntryJumpBytes?state.jump[i]:state.proof.profile.signature[i]))return false;
    return true;
}
bool executable(DWORD protection) noexcept {
    if(protection&(PAGE_GUARD|PAGE_NOACCESS))return false;
    const auto p=protection&0xff;
    return p==PAGE_EXECUTE || p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}
EntryResult reject(EntryError error,bool retained=false) noexcept {
    state.error.store(error,std::memory_order_relaxed);
    if(retained)state.retained.store(true,std::memory_order_release);
    state.stage.store(retained?EntryStage::FailedRetained:EntryStage::Rejected,std::memory_order_release);
    return entry_bridge_status();
}
bool validate_image(std::uintptr_t base,const EntryProfile& profile) noexcept {
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    if(!api().read(base,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<static_cast<LONG>(sizeof(dos))
        || dos.e_lfanew>4096 || static_cast<std::size_t>(dos.e_lfanew)+sizeof(nt)>profile.image_size
        || base>std::numeric_limits<std::uintptr_t>::max()-profile.image_size
        || !api().read(base+dos.e_lfanew,&nt,sizeof(nt)))return false;
    return nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64
        && (nt.FileHeader.Characteristics&IMAGE_FILE_EXECUTABLE_IMAGE) && !(nt.FileHeader.Characteristics&IMAGE_FILE_DLL)
        && nt.FileHeader.NumberOfSections && nt.FileHeader.NumberOfSections<=96
        && nt.FileHeader.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64)
        && nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC
        && nt.FileHeader.TimeDateStamp==profile.timestamp && nt.OptionalHeader.SizeOfImage==profile.image_size
        && nt.OptionalHeader.CheckSum==profile.checksum && nt.OptionalHeader.AddressOfEntryPoint==profile.entry_rva
        && nt.OptionalHeader.SizeOfHeaders>=static_cast<std::size_t>(dos.e_lfanew)+sizeof(nt)
        && nt.OptionalHeader.SizeOfHeaders<=profile.image_size;
}
void clear_proof(EntryProof& proof) noexcept {
    // Volatile byte operations keep this pre-CRT component independent of memset/ISA initialization.
    auto* bytes=reinterpret_cast<volatile unsigned char*>(&proof);
    for(std::size_t i=0;i<sizeof(proof);++i)bytes[i]=0;
}
bool rollback() noexcept {
    DWORD old=0,discard=0;
    // One bounded rollback attempt. Every failed/uncertain component retains us.
    if(!api().protect(state.proof.entry,kEntryJumpBytes,PAGE_EXECUTE_READWRITE,old))return false;
    const bool wrote=api().write(state.proof.entry,state.proof.original.data(),kEntryJumpBytes);
    const bool verified=matches(state.proof.entry,state.proof.profile.signature.data(),state.proof.profile.signature_size);
    const bool flushed=api().flush(state.proof.entry,kEntryJumpBytes);
    const bool protected_ok=api().protect(state.proof.entry,kEntryJumpBytes,state.original_protection,discard);
    return wrote && verified && flushed && protected_ok;
}
} // namespace

EntryResult entry_bridge_status() noexcept {
    const auto stage=state.stage.load(std::memory_order_acquire);
    return {stage,state.error.load(std::memory_order_acquire),
        stage==EntryStage::Arming || state.retained.load(std::memory_order_acquire)
            ?EntryRetention::ProcessLifetime:EntryRetention::Unreferenced};
}
EntryResult arm_process_start_entry(void* proxy,std::uint32_t reason,const void* reserved,
    const EntryProfile& profile,EntryInitializer initializer,void* context) noexcept {
    auto expected=EntryStage::Empty;
    if(!state.stage.compare_exchange_strong(expected,EntryStage::Arming,std::memory_order_acq_rel)) {
        auto out=entry_bridge_status();out.error=EntryError::Duplicate;return out;
    }
    if(state.stopped.load(std::memory_order_acquire))return reject(EntryError::Stopped);
    if(reason!=DLL_PROCESS_ATTACH || !reserved)return reject(EntryError::Attach);
    if(!proxy || !initializer)return reject(EntryError::Module);
    if(!profile.timestamp || !profile.checksum || profile.image_size<4096 || !profile.entry_rva
        || profile.signature_size<kEntryJumpBytes || profile.signature_size>kMaxEntrySignature
        || profile.entry_rva>profile.image_size-profile.signature_size)return reject(EntryError::Profile);
    const auto image=reinterpret_cast<std::uintptr_t>(api().main_image());Region own{},code{};
    if(!image || image==reinterpret_cast<std::uintptr_t>(proxy)
        || !api().region(reinterpret_cast<std::uintptr_t>(&ff7rp_proxy_entry_thunk),own)
        || own.allocation!=reinterpret_cast<std::uintptr_t>(proxy) || own.state!=MEM_COMMIT
        || own.type!=MEM_IMAGE || !executable(own.protection))return reject(EntryError::Module);
    if(!validate_image(image,profile))return reject(EntryError::Image);
    const auto entry=image+profile.entry_rva;
    if(!api().region(entry,code) || code.allocation!=image || code.state!=MEM_COMMIT || code.type!=MEM_IMAGE
        || !executable(code.protection) || entry<code.begin || entry-code.begin>code.size
        || profile.signature_size>code.size-(entry-code.begin)
        || !matches(entry,profile.signature.data(),profile.signature_size))return reject(EntryError::Code);
    state.proof.profile=profile;state.proof.image=image;state.proof.proxy=reinterpret_cast<std::uintptr_t>(proxy);
    state.proof.entry=entry;state.proof.attach_thread=api().thread();
    for(std::size_t i=0;i<kEntryJumpBytes;++i)state.proof.original[i]=profile.signature[i];
    state.initializer=initializer;state.context=context;state.original_protection=code.protection;
    state.jump[0]=0xff;state.jump[1]=0x25;
    const auto target=reinterpret_cast<std::uintptr_t>(&ff7rp_proxy_entry_thunk);
    for(std::size_t i=0;i<sizeof(target);++i)state.jump[6+i]=static_cast<std::uint8_t>(target>>(8*i));
    DWORD old=0,discard=0;
    if(!api().protect(entry,kEntryJumpBytes,PAGE_EXECUTE_READWRITE,old))return reject(EntryError::Protection);
    // Recheck after the protection transition, before publishing any thunk byte.
    if(old!=code.protection || !matches(entry,profile.signature.data(),profile.signature_size)) {
        const bool restored=api().protect(entry,kEntryJumpBytes,old,discard);
        return reject(EntryError::Code,!restored);
    }
    ff7rp_proxy_entry_resume=entry;
    state.retained.store(true,std::memory_order_release); // Before any thunk reference becomes visible.
    const bool wrote=api().write(entry,state.jump.data(),state.jump.size());
    const bool verified=published_entry_matches();
    const bool flushed=api().flush(entry,kEntryJumpBytes);
    const bool protected_ok=api().protect(entry,kEntryJumpBytes,old,discard);
    if(!wrote || !verified || !flushed || !protected_ok) {
        const auto error=!wrote || !verified?EntryError::Patch:!flushed?EntryError::Flush:EntryError::Protection;
        if(!rollback())return reject(EntryError::Rollback,true);
        state.retained.store(false,std::memory_order_release);
        return reject(error);
    }
    if(state.stage.load(std::memory_order_acquire)!=EntryStage::Arming
        || state.error.load(std::memory_order_acquire)!=EntryError::None)return reject(EntryError::EntryOwnership,true);
    state.stage.store(EntryStage::Armed,std::memory_order_release);
    return entry_bridge_status();
}

const EntryScope* active_entry_scope() noexcept {
    if(state.stage.load(std::memory_order_acquire)!=EntryStage::Initializing
        || api().thread()!=state.proof.entry_thread)return nullptr;
    return state.scope;
}
bool EntryScope::capture(EntryProof& out) const noexcept {
    clear_proof(out);if(active_entry_scope()!=this || !state.proof.restored)return false;
    out=state.proof;return true;
}
std::uintptr_t EntryBridgeAccess::dispatch() noexcept {
    auto expected=EntryStage::Armed;
    if(!state.stage.compare_exchange_strong(expected,EntryStage::Restoring,std::memory_order_acq_rel)) {
        state.error.store(EntryError::Reentry,std::memory_order_release);return 0;
    }
    state.proof.entry_thread=api().thread();
    if(state.proof.entry_thread!=state.proof.attach_thread || !published_entry_matches()) {
        reject(EntryError::EntryOwnership,true);return 0;
    }
    DWORD old=0,discard=0;
    if(!api().protect(state.proof.entry,kEntryJumpBytes,PAGE_EXECUTE_READWRITE,old)) {
        reject(EntryError::Protection,true);return 0;
    }
    if(old!=state.original_protection || !published_entry_matches()) {
        api().protect(state.proof.entry,kEntryJumpBytes,old,discard);
        reject(EntryError::EntryOwnership,true);return 0;
    }
    const bool wrote=api().write(state.proof.entry,state.proof.original.data(),kEntryJumpBytes);
    const bool verified=matches(state.proof.entry,state.proof.profile.signature.data(),state.proof.profile.signature_size);
    const bool flushed=api().flush(state.proof.entry,kEntryJumpBytes);
    const bool protected_ok=api().protect(state.proof.entry,kEntryJumpBytes,state.original_protection,discard);
    if(!wrote || !verified || !flushed || !protected_ok) { reject(EntryError::Restore,true);return 0; }
    state.proof.restored=true;
    EntryScope scope;
    if(!state.stopped.load(std::memory_order_acquire)) {
        state.scope=&scope;state.stage.store(EntryStage::Initializing,std::memory_order_release);
        state.proof.callback_invoked=true;
        state.proof.callback_succeeded=state.initializer(scope,state.context);
        state.scope=nullptr;
        if(state.stage.load(std::memory_order_acquire)!=EntryStage::Initializing
            || state.error.load(std::memory_order_acquire)!=EntryError::None) {
            reject(state.error.load(std::memory_order_acquire),true);return 0;
        }
        state.error.store(state.proof.callback_succeeded?EntryError::None:EntryError::Callback,std::memory_order_relaxed);
    } else state.error.store(EntryError::Stopped,std::memory_order_relaxed);
    state.stage.store(EntryStage::Completed,std::memory_order_release);
    return state.proof.entry;
}
extern "C" std::uintptr_t ff7rp_proxy_entry_dispatch() noexcept {
    const void* caller=_ReturnAddress();
#ifdef FF7RP_NATIVE_PROXY_ENTRY_BRIDGE_SELFTEST
    if(test_dispatch_caller)caller=test_dispatch_caller;
#endif
    if(caller!=reinterpret_cast<const void*>(&ff7rp_proxy_entry_dispatch_return)) {
        if(state.retained.load(std::memory_order_acquire))reject(EntryError::EntryOwnership,true);
        return 0;
    }
    return EntryBridgeAccess::dispatch();
}
EntryResult stop_entry_bridge() noexcept {
    state.stopped.store(true,std::memory_order_release);return entry_bridge_status();
}
bool completed_entry_proof(EntryProof& out) noexcept {
    clear_proof(out);if(state.stage.load(std::memory_order_acquire)!=EntryStage::Completed)return false;
    out=state.proof;return true;
}
} // namespace ff7rp::proxy
