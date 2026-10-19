#include "game/native_audio_runtime.h"
#include "game/native_audio_bootstrap.h"
#include "game/native_proxy_bootstrap.h"
#include "game/native_audio_gate.h"
#include "game/native_audio_mount.h"
#include "game/hook_specs.h"
#include "game/rvas.h"
#include "core/generated/build_identity.generated.h"
#include "core/hooks.h"
#include "core/pe_image.h"
#include "core/logging.h"
#include "pipeline/cache.h"
#include <bcrypt.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace ff7r::piano::game::native_audio {
namespace {
namespace gu = uobject_locator_core;
using Artifacts = ff7rp::pipeline::NativeAudioArtifactSet;
// Qualified known-present PackageStore key, not a song selection or candidate.
constexpr std::uint64_t kStockPackageWitness = 0xe1817fba8041bd80ull;
struct Name {
    std::string text;
    std::wstring wide;
    std::uint64_t packed = 0;
    bool existing = false;
};
struct alignas(8) SoftPath { std::uint64_t name = 0, suffix = 0; std::int32_t count = 0, capacity = 0; };
struct alignas(8) Acquisition {
    std::array<Address,2> pair{};
    SoftPath path{};
    std::array<std::uint64_t,2> delegate{}, debug{};
};
struct AdmissionTimings {
    // -1 means not attempted; retryable authentication reports its latest attempt.
    std::int64_t mount_us=-1,request_us=-1,discover_us=-1,bindings_us=-1,register_us=-1;
};
template<class Fn> auto timed_stage(std::int64_t& duration,Fn&& fn) {
    struct Timer {
        std::int64_t& duration;
        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
        ~Timer(){duration=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();}
    } timer{duration};
    return fn();
}
static_assert(sizeof(SoftPath) == 24 && sizeof(NativeAccessor) == 32);
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
MemoryReader test_memory{};
Address (*test_entry)(std::string_view)=nullptr;
bool (*test_signature)(std::string_view)=nullptr;
bool (*test_install_hook)(std::string_view)=nullptr;
bool (*test_disable_hook)(std::size_t)=nullptr;
bool (*test_remove_hook)(std::size_t)=nullptr;
Address test_updater_caller=0;
void (*test_invalidation_entered)()=nullptr;
#endif
bool memory_read(void*, Address address, void* out, std::size_t size) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
    if(test_memory.read && test_memory.read(test_memory.context,address,out,size))return true;
#endif
    return core::safe_copy_bytes(reinterpret_cast<const void*>(address),out,size);
}
MemoryReader memory() noexcept { return {nullptr,&memory_read}; }
template<class T> bool read(Address address,T& out) noexcept { return memory_read(nullptr,address,&out,sizeof(out)); }
bool ascii(std::string_view s) noexcept {
    if (s.empty() || s.size() > 240) return false;
    for (const auto c:s) if (static_cast<unsigned char>(c)<32 || static_cast<unsigned char>(c)>126) return false;
    return true;
}
std::wstring widen(std::string_view s) { return {s.begin(),s.end()}; }
bool compact_file_hash(HANDLE file,std::uint64_t extent,std::uint64_t& compact) noexcept {
    // Producer bounds its source/MABF inputs to 64 MiB; leave room for Zen and
    // container metadata, but never hash an unbounded receipt-supplied extent.
    if(!extent || extent>512ull*1024*1024)return false;
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE value=nullptr;
        ~Hash(){if(value)BCryptDestroyHash(value);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    } hash;
    if(BCryptOpenAlgorithmProvider(&hash.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0
        || BCryptCreateHash(hash.algorithm,&hash.value,nullptr,0,nullptr,0,0)<0)return false;
    std::array<unsigned char,65536> bytes{};
    while(extent) {
        const DWORD request=static_cast<DWORD>((std::min)(extent,static_cast<std::uint64_t>(bytes.size())));
        DWORD got=0;
        if(!ReadFile(file,bytes.data(),request,&got,nullptr) || got!=request
            || BCryptHashData(hash.value,bytes.data(),got,0)<0)return false;
        extent-=got;
    }
    std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(hash.value,digest.data(),static_cast<ULONG>(digest.size()),0)<0)return false;
    compact=0;for(unsigned i=0;i<8;++i)compact|=static_cast<std::uint64_t>(digest[i])<<(i*8);
    return true;
}
}

class PreparedNativeAudio final {
public:
    ~PreparedNativeAudio() { for (auto h:files) if (h!=INVALID_HANDLE_VALUE) CloseHandle(h); }
    std::shared_ptr<const Artifacts> artifacts;
    std::shared_ptr<PreparedNativeAudio> resident; // Same immutable cohort, new descriptor generation.
    std::uint64_t generation = 0;
    std::array<HANDLE,3> files{INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE};
    std::wstring prefix;
    std::array<Name,32> names{};
    std::size_t name_count = 0;
    std::array<NameWitness,32> resolved{};
    std::array<Acquisition,3> acquisitions{};
    std::array<BindingSeed,3> seeds{};
    CohortBindings bindings;
    std::array<NativeAccessor,3> stock_rows{}, candidate_rows{};
    std::array<std::uint64_t,3> stock_keys{};
    std::uint64_t stock_bgm=0, stock_resource=0;
    std::uint64_t selected_name = 0;
    std::atomic<RuntimeStage> stage{RuntimeStage::Queued};
    std::atomic<RuntimeFailure> failure{RuntimeFailure::None};
    BindingResult binding_failure{}; // published before failure/stage release
    unsigned char role = 0;
    bool mounted = false;
    RuntimeStage reported_stage=RuntimeStage::Unavailable;
    AdmissionTimings timings{};
    std::array<std::uint64_t,3> environment{};
    std::array<unsigned char,260> mount_status{};
    std::array<std::uint64_t,2> encryption_id{};
    std::array<std::uint64_t,4> encryption_key{};

    bool add_name(std::string text,bool existing) {
        if (!ascii(text)) return false;
        for (std::size_t i=0;i<name_count;++i) if(names[i].text==text) {
            names[i].existing |= existing; return true;
        }
        if(name_count==names.size()) return false;
        auto& n=names[name_count++]; n.wide=widen(text); n.text=std::move(text); n.existing=existing; return true;
    }
    std::uint64_t name(std::string_view text) const noexcept {
        for(std::size_t i=0;i<name_count;++i) if(names[i].text==text) return names[i].packed;
        return 0;
    }
};

namespace {
Address g_base=0;
RuntimeServices g_services{};
core::HookCallbackGate g_callbacks;
NativeEntryGate g_managers;
std::shared_mutex g_lifetime;
// Admission is independent of the lifetime fence: shutdown never waits while
// holding a fence needed by an original callback to finish.
std::shared_mutex g_admission;
thread_local unsigned g_work_depth=0;
std::atomic_bool g_origin{false},g_entry_seen{false},g_hooks_ready{false};
constexpr unsigned kEntryForwardingLimit=1024;
std::atomic_uint g_entry_forwarding{0};
std::atomic_bool g_entry_forwarding_uncertain{false};
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
void (*test_entry_handoff)()=nullptr;
#endif
thread_local unsigned g_invalidator_depth=0, g_updater_depth=0;
std::atomic_bool g_accepting{false},g_captured{false},g_initializer_seen{false};
std::atomic_bool g_initializer_notified{false};
std::atomic_bool g_stock_admitted{false};
std::atomic<RuntimeFailure> g_failure{RuntimeFailure::None};
std::atomic_bool g_failure_reported{false},g_capture_reported{false};
std::atomic<DWORD> g_updater_thread{0};
std::atomic<std::uint64_t> g_stock_selection{0};
std::atomic_flag g_working=ATOMIC_FLAG_INIT;
MountIdentity g_mount{};
std::mutex g_queue_mutex;
// Never destruct admitted backing storage on ASI teardown/unload: native rows
// and independently owning acquisitions have process-exit retention semantics.
auto& g_queue=*new std::array<std::shared_ptr<PreparedNativeAudio>,kNativeAudioEntryLimit>;
std::size_t g_count=0,g_next=0;
using VoidFn=void(*)();
using EntryFn=int(*)();
EntryFn original_entry=nullptr;
using ReceiverFn=void(*)(void*);
using LookupFn=void*(*)(void*,std::uint64_t,std::uint64_t*,unsigned char*);
using AppendFn=void(*)(void*,unsigned char,std::uint64_t,void*);
using NameHelperFn=std::uint64_t(*)(void*,std::uint64_t,void*);
using AccessorFn=NativeAccessor*(*)(NativeAccessor*,std::uint64_t);
using BridgeFn=void(*)(void*,unsigned char,std::uint64_t);
using AcquireFn=void*(*)(void*,void*,void*,void*,std::int32_t,void*,void*,void*);
using MountFn=void*(*)(void*,void*,void*,void*,void*);
using NameFn=void(*)(std::uint64_t*,const wchar_t*,std::int32_t);
VoidFn original_factory=nullptr, original_shutdown=nullptr;
ReceiverFn original_destructor=nullptr,original_initialize=nullptr,original_updater=nullptr;
LookupFn original_lookup=nullptr;
AppendFn original_append=nullptr,original_remove=nullptr;
ReceiverFn original_reset=nullptr,original_reserve=nullptr;
NameHelperFn original_name_helper=nullptr;
std::array<core::RawRvaHook,12> g_hooks;
std::size_t g_installed=0;

void invalidate(RuntimeFailure reason) noexcept {
    RuntimeFailure expected=RuntimeFailure::None;
    g_failure.compare_exchange_strong(expected,reason,std::memory_order_acq_rel);
}
bool cold_baseline(Address base=g_base) noexcept {
    if constexpr(!rva::NativeAudioCrtState || !rva::NativeAudioInitialGuHeader || !rva::DataObjectNameMap)return false;
    else {
    std::uint32_t crt=1; Address map=1,owner=1;
    std::array<unsigned char,48> bytes{};
    if(!read(base+rva::NativeAudioCrtState,crt) || crt
        || !read(base+rva::DataObjectNameMap,map) || map
        || !read(base+rva::PackageStoreOwnerGlobal,owner) || owner)return false;
    for(auto rva:{rva::NativeAudioInitialGuHeader,rva::BgmListRegistry,rva::ScoreInfoRegistry,rva::PianoScoreRegistry}) {
        if(!read(base+rva,bytes) || std::any_of(bytes.begin(),bytes.end(),[](auto b){return b!=0;}))return false;
    }
    return true;
    }
}
void covered_entry() noexcept {
    if(!g_origin.load(std::memory_order_acquire))invalidate(RuntimeFailure::MissedInitializer);
}
int ordinary_entry_hook() {
    // The short observer lease hands off to durable forwarding accounting BEFORE
    // release. The original may run until process exit; its trampoline must live.
    struct Forwarding {
        bool counted=false;
        void enter() noexcept {
            auto count=g_entry_forwarding.load(std::memory_order_acquire);
            while(count<kEntryForwardingLimit) {
                if(g_entry_forwarding.compare_exchange_weak(count,count+1,std::memory_order_acq_rel)) {counted=true;return;}
            }
            g_entry_forwarding_uncertain.store(true,std::memory_order_release);
            invalidate(RuntimeFailure::MissedInitializer);
        }
        ~Forwarding(){if(counted)g_entry_forwarding.fetch_sub(1,std::memory_order_acq_rel);}
    } forwarding;
    {
        auto callback=g_callbacks.try_enter();
        forwarding.enter();
        if(!callback || g_entry_seen.exchange(true,std::memory_order_acq_rel)
            || !g_hooks_ready.load(std::memory_order_acquire) || !cold_baseline()
            || g_callbacks.callbacks_in_flight()!=1
            || g_failure.load(std::memory_order_acquire)!=RuntimeFailure::None)
            invalidate(RuntimeFailure::MissedInitializer);
        else {
            g_updater_thread.store(GetCurrentThreadId(),std::memory_order_release);
            g_origin.store(true,std::memory_order_release);
        }
    }
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
    if(test_entry_handoff)test_entry_handoff();
#endif
    return original_entry();
}
void fail(PreparedNativeAudio& work,RuntimeFailure reason,unsigned char role=0,BindingResult binding={}) noexcept {
    work.role=role; work.binding_failure=binding;
    work.failure.store(reason,std::memory_order_release);
    work.stage.store(RuntimeStage::Failed,std::memory_order_release);
}
void report_failure() noexcept {
    const auto reason=g_failure.load(std::memory_order_acquire);
    if(reason==RuntimeFailure::None || g_failure_reported.exchange(true,std::memory_order_acq_rel))return;
    try {core::log(core::LogLevel::Error,std::string("native_audio terminal failure=")+native_audio_failure_name(reason)
        +" retention=process_exit rollback=none");}catch(...){}
}
bool provenance() noexcept {
    if(g_failure.load(std::memory_order_acquire)!=RuntimeFailure::None || !g_captured.load(std::memory_order_acquire)) return false;
    MountIdentity fresh{};
    if(!read_mount_identity(memory(),g_base,g_mount.owner,fresh) || fresh!=g_mount) {
        invalidate(RuntimeFailure::Provenance); return false;
    }
    return true;
}
bool signature(std::string_view id) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
    if(test_signature)return test_signature(id);
#endif
    const auto* s=find_rva_signature(id);
    return s && s->rva && core::bytes_equal(reinterpret_cast<const std::uint8_t*>(g_base+s->rva),s->expected_prologue);
}
template<class Fn> Fn entry(std::string_view id) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
    if(test_entry)return reinterpret_cast<Fn>(test_entry(id));
#endif
    const auto* s=find_rva_signature(id);
    // The existing FName constructor is a canonical build-identity-validated
    // callable, deliberately without a signature spec. No other fallback exists.
    const auto address=s?s->rva:(id=="fname_ctor"?rva::FNameCtor:0);
    return address?reinterpret_cast<Fn>(g_base+address):nullptr;
}
bool known_manager(void* manager) noexcept {
    const Address p=reinterpret_cast<Address>(manager);
    return p==g_base+rva::BgmListRegistry || p==g_base+rva::ScoreInfoRegistry || p==g_base+rva::PianoScoreRegistry;
}

// The lifetime writer waits before entering owner replacement/teardown. Nested
// factory -> initializer/destructor calls inherit the same fence on that thread.
struct InvalidationFence {
    std::unique_lock<std::shared_mutex> lock;
    explicit InvalidationFence(RuntimeFailure reason,void* owner=nullptr) {
        // Publish sticky rejection before waiting for an already admitted reader.
        // Check again after acquisition: the first initializer may finish while
        // a replacement entrant waits behind its factory's writer fence.
        if(g_captured.load(std::memory_order_acquire)
            && (!owner || reinterpret_cast<Address>(owner)==g_mount.owner)) invalidate(reason);
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
        if(test_invalidation_entered)test_invalidation_entered();
#endif
        if(!g_invalidator_depth) lock=std::unique_lock<std::shared_mutex>(g_lifetime);
        ++g_invalidator_depth;
        if(g_captured.load(std::memory_order_acquire)
            && (!owner || reinterpret_cast<Address>(owner)==g_mount.owner)) invalidate(reason);
    }
    ~InvalidationFence(){--g_invalidator_depth;}
};
void notify_initializer_complete(const core::HookCallbackGate::Lease& callback) noexcept {
    // An initializer nested in the factory inherits its writer fence. Only the
    // outermost original's normal return may deliver the deferred notification.
    if(!callback || g_invalidator_depth || g_work_depth || NativeEntryGate::inside_entry()
        || !g_accepting.load(std::memory_order_acquire) || !g_hooks_ready.load(std::memory_order_acquire)
        || !g_origin.load(std::memory_order_acquire) || !g_initializer_seen.load(std::memory_order_acquire)
        || !provenance())return;
    const auto notify=g_services.initializer_complete;
    if(!notify || g_initializer_notified.exchange(true,std::memory_order_acq_rel))return;
    // Keep the caller's callback lease through the entire host invocation. In
    // particular, callback-triggered shutdown must retain all hooks/module code.
    notify();
}
void factory_hook() {
    auto callback=g_callbacks.try_enter();
    covered_entry();
    {InvalidationFence fence{RuntimeFailure::Replaced};
        try {original_factory();}catch(...){invalidate(RuntimeFailure::Replaced);throw;}}
    notify_initializer_complete(callback);
}
void shutdown_hook() {
    auto callback=g_callbacks.try_enter();
    covered_entry();
    InvalidationFence fence{RuntimeFailure::Shutdown}; original_shutdown();
}
void destructor_hook(void* owner) {
    auto callback=g_callbacks.try_enter();
    covered_entry();
    InvalidationFence fence{RuntimeFailure::Shutdown,owner}; original_destructor(owner);
}
void initialize_hook(void* owner) {
    auto callback=g_callbacks.try_enter();
    covered_entry();
    {
        std::shared_lock<std::shared_mutex> fence;
        if(!g_invalidator_depth) fence=std::shared_lock<std::shared_mutex>(g_lifetime);
        try {original_initialize(owner);}catch(...){invalidate(RuntimeFailure::Subscription);throw;}
        if(!callback || !g_accepting.load(std::memory_order_acquire) || !g_origin.load(std::memory_order_acquire)
            || g_failure.load(std::memory_order_acquire)!=RuntimeFailure::None)return;
        if(g_initializer_seen.exchange(true,std::memory_order_acq_rel)) {invalidate(RuntimeFailure::Replaced);return;}
        MountIdentity captured{}; PackageObservation witness{};
        if(!read_mount_identity(memory(),g_base,reinterpret_cast<Address>(owner),captured)
            || !read_package_store(memory(),captured.owner+680,kStockPackageWitness,witness) || !witness.value) {
            invalidate(RuntimeFailure::Subscription);return;
        }
        g_mount=captured; g_captured.store(true,std::memory_order_release);
    }
    notify_initializer_complete(callback);
}
void* lookup_hook(void* manager,std::uint64_t key,std::uint64_t* association,unsigned char* mode) {
    covered_entry();
    auto callback=g_callbacks.try_enter(); NativeEntryGate::Reader gate{g_managers};
    return original_lookup(manager,key,association,mode);
}
void append_hook(void* manager,unsigned char group,std::uint64_t association,void* object) {
    covered_entry();
    auto callback=g_callbacks.try_enter(); NativeEntryGate::Reader gate{g_managers};
    original_append(manager,group,association,object);
}
void remove_hook(void* manager,unsigned char group,std::uint64_t opaque,void* object) {
    covered_entry();
    auto callback=g_callbacks.try_enter(); NativeEntryGate::Reader gate{g_managers};
    if(known_manager(manager) && g_stock_admitted.load(std::memory_order_acquire)) invalidate(RuntimeFailure::Bindings);
    original_remove(manager,group,opaque,object);
}
void reset_hook(void* manager) {
    covered_entry();
    auto callback=g_callbacks.try_enter(); NativeEntryGate::Reader gate{g_managers};
    if(known_manager(manager) && g_stock_admitted.load(std::memory_order_acquire)) invalidate(RuntimeFailure::Bindings);
    original_reset(manager);
}
std::uint64_t name_helper_hook(void* map,std::uint64_t name,void* manager) {
    covered_entry();
    auto callback=g_callbacks.try_enter(); NativeEntryGate::Reader gate{g_managers};
    return original_name_helper(map,name,manager);
}
void reserve_hook(void* header) {
    covered_entry();
    auto callback=g_callbacks.try_enter(); NativeEntryGate::Reader gate{g_managers}; original_reserve(header);
}

bool construct_names(PreparedNativeAudio& work) {
    auto ctor=entry<NameFn>("fname_ctor");
    if(!ctor)return false;
    for(std::size_t i=0;i<work.name_count;++i) {
        auto& n=work.names[i]; ctor(&n.packed,n.wide.c_str(),n.existing?0:1);
        if(!static_cast<std::uint32_t>(n.packed)) return false;
        work.resolved[i]={n.text,n.packed};
    }
    work.selected_name=work.name(work.artifacts->score_name); return work.selected_name!=0;
}
bool mount(PreparedNativeAudio& work) {
    if(!signature("io_dispatcher_mount") || !provenance()) return false;
    PackageObservation stock{},after{};
    if(!read_package_store(memory(),g_mount.owner+680,kStockPackageWitness,stock) || !stock.value) return false;
    for(const auto& p:work.artifacts->packages) {
        PackageObservation missing{};
        if(p.package_id==kStockPackageWitness || !read_package_store(memory(),g_mount.owner+680,p.package_id,missing) || missing.value) return false;
    }
    work.environment={reinterpret_cast<Address>(work.prefix.c_str()),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(work.prefix.size()+1)) | (static_cast<std::uint64_t>(work.prefix.size()+1)<<32),0};
    // All backing buffers are retained with the cohort even after a failed call.
    work.mounted=true;
    auto call=entry<MountFn>("io_dispatcher_mount");
    void* result=call(reinterpret_cast<void*>(g_mount.dispatcher),work.mount_status.data(),
        work.environment.data(),work.encryption_id.data(),work.encryption_key.data());
    std::uint32_t status=~0u;std::memcpy(&status,work.mount_status.data(),sizeof(status));
    if(result!=work.mount_status.data() || status || !provenance()
        || !read_package_store(memory(),g_mount.owner+680,kStockPackageWitness,after) || stock!=after) return false;
    for(const auto& p:work.artifacts->packages) {
        PackageObservation present{};
        if(!read_package_store(memory(),g_mount.owner+680,p.package_id,present) || !present.value) return false;
    }
    return true;
}
bool acquire(PreparedNativeAudio& work) {
    if(!signature("single_soft_path_request") || !construct_names(work))return false;
    Address service=0,vtable=0;
    if(!read(g_base+rva::StreamableServiceGlobal,service) || !service
        || !read(service+88,vtable) || vtable!=g_base+rva::StreamableManagerVtable)return false;
    auto call=entry<AcquireFn>("single_soft_path_request");
    for(std::size_t i=0;i<3;++i) {
        if(!provenance())return false;
        auto& a=work.acquisitions[i]; a.path.name=work.name(work.artifacts->packages[i+1].object_path);
        if(!a.path.name || call(reinterpret_cast<void*>(service+88),a.pair.data(),&a.path,
            a.delegate.data(),0,nullptr,nullptr,a.debug.data())!=a.pair.data() || !a.pair[0] || !a.pair[1])return false;
    }
    return true;
}
// 0 pending, 1 completed, -1 terminal. Completion alone never admits an object.
int completed(const PreparedNativeAudio& work) noexcept {
    bool pending=false;
    for(const auto& a:work.acquisitions) {
        std::array<unsigned char,3> flags{};std::array<std::int32_t,2> counts{};
        if(!read(a.pair[0]+16,flags) || flags[0]>1 || flags[1] || flags[2]
            || !read(a.pair[1]+8,counts) || counts[0]<=0 || counts[1]<=0)return -1;
        pending |= flags[0]!=1;
    }
    return pending?0:1;
}
bool witness(const gu::UObjectIdentityAccess& identity,Address object,ObjectWitness& out) noexcept {
    auto& o=out.objects; o[0]=object;
    return read(object+16,o[1]) && read(o[1]+16,o[2]) && read(o[1]+32,o[3])
        && read(o[3]+16,o[4]) && read(object+32,o[5]) && read(o[5]+16,o[6]) && identity.array_address;
}
bool discover(PreparedNativeAudio& work,const gu::UObjectIdentityAccess& identity) noexcept {
    gu::GUObjectArrayHeader header{};
    if(!gu::read_guobject_array_header(identity.memory,identity.array_address,{},header)
        || !gu::list_header_accepts(header))return false;
    constexpr const char* classes[]={"BGMList","PianoScoreInfo","PianoScore"};
    std::array<unsigned,6> matches{};
    for(int index=0;index<header.num_elements;++index) {
        Address object=0;
        if(!gu::read_uobject_item_pointer(identity,index,object))continue;
        std::uint64_t name=0,package_name=0;Address outer=0;
        if(!read(object+24,name))return false;
        for(unsigned char role=0;role<3;++role) {
            if(name!=work.name(classes[role]))continue;
            if(!read(object+32,outer) || !outer || !read(outer+24,package_name))return false;
            auto& seed=work.seeds[role];
            if(package_name==seed.candidate_names.package) {
                if(++matches[role*2]!=1 || !witness(identity,object,seed.candidate))return false;
            } else if(package_name==seed.stock_names.package) {
                if(++matches[role*2+1]!=1 || !witness(identity,object,seed.stock))return false;
            }
        }
    }
    for(auto n:matches)if(n!=1)return false;
    return true;
}
void seed_names(PreparedNativeAudio& work) noexcept {
    constexpr const char* classes[]={"BGMList","PianoScoreInfo","PianoScore"};
    for(unsigned char i=0;i<3;++i) {
        auto& s=work.seeds[i]; const auto& package=work.artifacts->packages[i+1];
        const auto dot=package.object_path.rfind('.');
        auto& names=s.candidate_names;
        constexpr const char* class_names[]={"EndDataObjectBGMList","EndDataObjectPianoScoreInfo","EndDataObjectPianoScore"};
        names={work.name(classes[i]),work.name(class_names[i]),work.name("Class"),work.name("/Script/EndDataObject"),
            work.name("Package"),work.name(std::string_view(package.object_path).substr(0,dot))};
        s.stock_names=names;
        // Prebuilt stock package witness is adjacent to the candidate package in names.
        for(std::size_t n=0;n<work.name_count;++n) {
            const auto& text=work.names[n].text;
            if(text.starts_with("/Game/DataObject/Resident/") && text.ends_with(classes[i]))s.stock_names.package=work.names[n].packed;
        }
        s.association=work.names[work.name_count-1].packed; s.stock_association=work.name("Resident");
        s.owner={work.acquisitions[i].pair[0],work.acquisitions[i].pair[1],work.acquisitions[i].path.name};
    }
}
AccessorFn accessor(unsigned char role) noexcept {
    if(role==1)return g_services.scoreinfo_original;
    return entry<AccessorFn>(role==0?"bgmlist_accessor":"pianoscore_accessor");
}
bool query(PreparedNativeAudio& work,bool after) {
    const auto* layout=native_audio_layout(work.artifacts->build_id);
    if(!layout)return false;
    const std::array<std::uint64_t,3> candidate={work.name(work.artifacts->bgm_key),work.selected_name,work.selected_name};
    // Stock keys are captured from ordinary provider rows by a preconstructed
    // existing key witness, never a substituted native request or shared row.
    for(unsigned char i=0;i<3;++i) {
        auto& row=work.candidate_rows[i]; row={};
        // A before query of the NEW keys must miss. The unchanged stock registry
        // and all stock identities/backings are checked by CohortBindings.
        if(accessor(i)(&row,candidate[i])!=&row || (after?!row.words[1]:row.words[1]!=0)
            || (after && row.words[2]!=work.seeds[i].association))return false;
    }
    if(!after) {
        work.stock_keys[1]=work.stock_keys[2]=work.name(work.artifacts->base_score_name);
        if(!work.stock_keys[1])return false;
        auto& score=work.stock_rows[1];score={};
        if(accessor(1)(&score,work.stock_keys[1])!=&score || !score.words[1]
            || score.words[2]!=work.name("Resident") || !read(score.words[1]+layout->scoreinfo_bgm,work.stock_bgm))return false;
        work.stock_keys[0]=work.stock_bgm;
    }
    for(unsigned char i=0;i<3;++i) {
        NativeAccessor current{};
        if(accessor(i)(&current,work.stock_keys[i])!=&current || !current.words[1]
            || current.words[2]!=work.name("Resident"))return false;
        if(after && current.words!=work.stock_rows[i].words)return false;
        if(!after)work.stock_rows[i]=current;
    }
    std::uint64_t stock_resource=0,stock_bgm=0;
    if(!read(work.stock_rows[0].words[1]+layout->bgm_resource,stock_resource)
        || !read(work.stock_rows[1].words[1]+layout->scoreinfo_bgm,stock_bgm))return false;
    if(stock_bgm!=work.stock_bgm || (after && stock_resource!=work.stock_resource))return false;
    if(!after)work.stock_resource=stock_resource;
    if(after) {
        std::uint64_t resource=0,bgm=0;
        return read(work.candidate_rows[0].words[1]+layout->bgm_resource,resource) && resource==work.name(work.artifacts->music_name)
            && read(work.candidate_rows[1].words[1]+layout->scoreinfo_bgm,bgm) && bgm==candidate[0];
    }
    return true;
}
bool register_cohort(PreparedNativeAudio& work,const gu::UObjectIdentityAccess& identity) {
    auto checked=work.bindings.validate(identity,BindingPhase::BeforeBridge);
    if(!checked){fail(work,RuntimeFailure::Bindings,checked.role,checked);return false;}
    if(!query(work,false)){fail(work,RuntimeFailure::Query);return false;}
    const std::array<Address,3> managers={g_base+rva::BgmListRegistry,g_base+rva::ScoreInfoRegistry,g_base+rva::PianoScoreRegistry};
    constexpr const char* bridges[]={"bgmlist_append_bridge","scoreinfo_append_bridge","pianoscore_append_bridge"};
    // Reserve all groups before publishing ANY weak entry.
    for(unsigned char i=0;i<3;++i) {
        struct Header {Address data;std::int32_t count,capacity;} before{},after{};
        if(!read(managers[i]+32,before) || before.count<0
            || before.count>=static_cast<std::int32_t>(kNativeAudioEntryLimit)){fail(work,RuntimeFailure::Capacity,i);return false;}
        if(before.count==before.capacity)original_reserve(reinterpret_cast<void*>(managers[i]+32));
        std::array<unsigned char,16> spare{};
        if(!read(managers[i]+32,after) || after.count!=before.count || after.capacity<=after.count
            || after.capacity>(i?100000:4096) || !after.data || after.data%8
            || !read(after.data+static_cast<Address>(after.count)*16,spare)){fail(work,RuntimeFailure::Capacity,i);return false;}
    }
    for(unsigned char i=0;i<3;++i) {
        checked=work.bindings.validate(identity,BindingPhase::BeforeBridge);
        if(!checked || !provenance()){fail(work,RuntimeFailure::Bindings,i,checked);return false;}
        entry<BridgeFn>(bridges[i])(reinterpret_cast<void*>(work.seeds[i].candidate.objects[0]),2,work.seeds[i].association);
        checked=work.bindings.latch_after_own_bridge(identity,i);
        if(!checked){fail(work,RuntimeFailure::Bindings,checked.role,checked);return false;}
    }
    checked=work.bindings.validate(identity,BindingPhase::CurrentRegistered);
    if(!checked || !query(work,true) || !provenance()){fail(work,RuntimeFailure::Query,checked.role,checked);return false;}
    return true;
}
// Caller holds manager writer exclusion. Only a valid empty group is Pending;
// malformed or drifting stock observations fail without consuming a native call.
BindingResult stock_baseline(bool& ready) noexcept {
    struct Header {Address data;std::int32_t count,capacity;};
    struct Group {Header header{};std::array<std::array<unsigned char,16>,kNativeAudioEntryLimit> entries{};};
    const std::array<Address,3> managers={g_base+rva::BgmListRegistry,g_base+rva::ScoreInfoRegistry,g_base+rva::PianoScoreRegistry};
    const std::array<Address,3> vtables={g_base+rva::BgmListObjectVtable,g_base+rva::ScoreInfoObjectVtable,g_base+rva::PianoScoreObjectVtable};
    std::array<Group,3> groups{};ready=true;
    gu::UObjectIdentityAccess identity{};bool have_identity=false;
    for(unsigned char role=0;role<3;++role) {
        auto& group=groups[role];auto& h=group.header;
        if(!read(managers[role],h))return {BindingFailure::Read,role};
        if(h.count<0 || h.count>static_cast<std::int32_t>(kNativeAudioEntryLimit)
            || h.capacity<h.count || h.capacity>(role?100000:4096)
            || (h.capacity && !h.data) || (h.data && (h.data<0x10000 || h.data%8))
            || h.data>std::numeric_limits<Address>::max()-static_cast<Address>(h.count)*16)
            return {BindingFailure::RegistryBounds,role};
        if(!h.count)ready=false;
        for(int i=0;i<h.count;++i) {
            auto& entry=group.entries[i];Address object=0,vtable=0;gu::UObjectLiveHandle weak{};
            if(!read(h.data+static_cast<Address>(i)*16,entry))return {BindingFailure::Read,role};
            std::memcpy(&weak.internal_index,entry.data(),4);std::memcpy(&weak.serial_number,entry.data()+4,4);
            if(!have_identity){if(!g_services.identity(identity))return {BindingFailure::Identity,role};have_identity=true;}
            if(weak.internal_index<0 || weak.serial_number<=0
                || !gu::read_uobject_item_pointer(identity,weak.internal_index,object)
                || !gu::validate_live_handle(identity,object,weak))return {BindingFailure::RegistryWeak,role};
            if(!read(object,vtable))return {BindingFailure::Read,role};
            if(vtable!=vtables[role])return {BindingFailure::Identity,role};
        }
    }
    for(unsigned char role=0;role<3;++role) {
        const auto& group=groups[role];Header current{};
        if(!read(managers[role],current))return {BindingFailure::Read,role};
        if(std::memcmp(&current,&group.header,sizeof(current)))return {BindingFailure::Drift,role};
        for(int i=0;i<current.count;++i) {
            std::array<unsigned char,16> entry{};
            if(!read(current.data+static_cast<Address>(i)*16,entry))return {BindingFailure::Read,role};
            if(entry!=group.entries[i])return {BindingFailure::Drift,role};
            gu::UObjectLiveHandle weak{};Address object=0,vtable=0;
            std::memcpy(&weak.internal_index,entry.data(),4);std::memcpy(&weak.serial_number,entry.data()+4,4);
            if(!gu::read_uobject_item_pointer(identity,weak.internal_index,object)
                || !gu::validate_live_handle(identity,object,weak))return {BindingFailure::RegistryWeak,role};
            if(!read(object,vtable))return {BindingFailure::Read,role};
            if(vtable!=vtables[role])return {BindingFailure::Identity,role};
        }
    }
    return {};
}
void advance(PreparedNativeAudio& work,BindingResult& deferred) {
    if(!provenance()){fail(work,RuntimeFailure::Provenance);return;}
    if(work.resident) {
        if(work.resident->stage.load(std::memory_order_acquire)==RuntimeStage::Ready)
            work.stage.store(RuntimeStage::Ready,std::memory_order_release);
        return; // Never remount/reacquire/re-register a retained identical cohort.
    }
    const auto stage=work.stage.load(std::memory_order_acquire);
    if(stage==RuntimeStage::Queued || stage==RuntimeStage::AwaitingStock) {
        if(!g_stock_admitted.load(std::memory_order_acquire)) {
            work.stage.store(RuntimeStage::AwaitingStock,std::memory_order_release);
            {
                NativeEntryGate::Writer exclusion{g_managers};if(!exclusion)return;
                bool ready=false;const auto checked=stock_baseline(ready);
                if(!checked){fail(work,RuntimeFailure::Bindings,checked.role,checked);return;}
                if(!ready)return;
                g_stock_admitted.store(true,std::memory_order_release);
            } // Loader originals may need manager readers: never mount under writer exclusion.
        }
        work.stage.store(RuntimeStage::Mounting,std::memory_order_release);
        if(!timed_stage(work.timings.mount_us,[&]{return mount(work);})){fail(work,RuntimeFailure::Mount);return;}
        if(!timed_stage(work.timings.request_us,[&]{return acquire(work);})){fail(work,RuntimeFailure::Acquisition);return;}
        work.stage.store(RuntimeStage::Acquiring,std::memory_order_release);return;
    }
    if(stage!=RuntimeStage::Acquiring && stage!=RuntimeStage::Authenticating)return;
    const int done=completed(work);
    if(done<0){fail(work,RuntimeFailure::Acquisition);return;} if(!done)return;
    NativeEntryGate::Writer exclusion{g_managers}; if(!exclusion)return;
    gu::UObjectIdentityAccess identity{};
    if(!g_services.identity(identity))return;
    work.stage.store(RuntimeStage::Authenticating,std::memory_order_release);
    seed_names(work);
    if(!timed_stage(work.timings.discover_us,[&]{return discover(work,identity);})){fail(work,RuntimeFailure::Discovery);return;}
    const auto checked=timed_stage(work.timings.bindings_us,[&]{return work.bindings.capture(core::generated::kBuildId,g_base,identity,work.artifacts,work.seeds,
        {work.resolved.data(),work.name_count});});
    if(checked.failure==BindingFailure::IncompleteSnapshot) {
        deferred=checked;return; // Retain acquisitions; the next updater retries only read-only authentication.
    }
    if(!checked){fail(work,RuntimeFailure::Bindings,checked.role,checked);return;}
    work.stage.store(RuntimeStage::Registering,std::memory_order_release);
    if(timed_stage(work.timings.register_us,[&]{return register_cohort(work,identity);}))work.stage.store(RuntimeStage::Ready,std::memory_order_release);
}
void updater_hook(void* receiver) {
    covered_entry();
    Address caller=reinterpret_cast<Address>(_ReturnAddress());
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
    if(test_updater_caller)caller=test_updater_caller;
#endif
    auto callback=g_callbacks.try_enter();
    ++g_updater_depth; original_updater(receiver); --g_updater_depth;
    if(!callback || g_updater_depth || !g_accepting.load(std::memory_order_acquire)
        || caller!=g_base+rva::NativeAudioUpdaterReturn || NativeEntryGate::inside_entry())return;
    std::shared_lock admission(g_admission,std::try_to_lock);
    if(!admission.owns_lock() || !g_accepting.load(std::memory_order_acquire)
        || !g_origin.load(std::memory_order_acquire) || g_failure.load(std::memory_order_acquire)!=RuntimeFailure::None)return;
    struct WorkDepth {WorkDepth(){++g_work_depth;} ~WorkDepth(){--g_work_depth;}} depth;
    if(!g_captured.load(std::memory_order_acquire)){report_failure();return;}
    DWORD expected=0; const DWORD thread=GetCurrentThreadId();
    g_updater_thread.compare_exchange_strong(expected,thread,std::memory_order_acq_rel);
    if(g_updater_thread.load(std::memory_order_acquire)!=thread)return;
    if(g_working.test_and_set(std::memory_order_acquire))return;
    std::shared_ptr<PreparedNativeAudio> observed;
    RuntimeSnapshot snapshot{};
    BindingResult deferred{};
    AdmissionTimings timings{};
    {
        std::shared_lock<std::shared_mutex> lifetime(g_lifetime,std::try_to_lock);
        if(lifetime.owns_lock()) {
            std::shared_ptr<PreparedNativeAudio> work;
            {std::lock_guard lock(g_queue_mutex);if(g_next<g_count)work=g_queue[g_next];}
            if(work) {
                advance(*work,deferred);
                const auto stage=work->stage.load(std::memory_order_acquire);
                if(stage==RuntimeStage::Ready) {std::lock_guard lock(g_queue_mutex);++g_next;}
                else if(stage==RuntimeStage::Failed)invalidate(work->failure.load(std::memory_order_acquire));
                observed=work;snapshot=native_audio_status(*work);
                timings=work->timings;
                if(deferred.failure==BindingFailure::IncompleteSnapshot) {
                    snapshot.binding=deferred;snapshot.role=deferred.role;
                }
            }
        }
    }
    // No ASI mutation/gate/queue/lifetime lock is held during formatting/logging.
    try {
        if(!g_capture_reported.exchange(true,std::memory_order_acq_rel))
            core::log(core::LogLevel::Info,"native_audio initializer_captured generation="+std::to_string(g_mount.generation)
                +" subscription="+std::to_string(g_mount.subscription));
        if(observed && observed->reported_stage!=snapshot.stage) {
            observed->reported_stage=snapshot.stage;
            const auto& d=snapshot.binding.diagnostic;
            const auto header_counts=[](const auto& h) {return std::to_string(h.num_elements)+","
                +std::to_string(h.max_elements)+","+std::to_string(h.num_chunks)+","+std::to_string(h.max_chunks);};
            const auto details=snapshot.binding.failure==BindingFailure::None?std::string{}:
                " binding_stage="+std::string(d.stage)+" binding_predicate="+d.predicate
                +" binding_side="+(d.side==BindingSide::Candidate?"candidate":d.side==BindingSide::Stock?"stock":"none")
                +" binding_node="+std::to_string(d.node)+" binding_phase="+std::to_string(static_cast<unsigned>(d.phase))
                +" binding_index="+std::to_string(d.index)+" binding_serial="+std::to_string(d.serial)
                +" serial_observed="+std::to_string(d.serial_observed)+" identity_result="+std::to_string(d.identity_result)
                +" zero_attempted="+std::to_string(d.zero_attempted)+" zero_result="+std::to_string(d.zero_result)
                +" scan_read_failed="+std::to_string(d.read_failed)+" scanned="+std::to_string(d.scanned)
                +" matches="+std::to_string(d.matches[0])+","+std::to_string(d.matches[1])+","+std::to_string(d.matches[2])
                +" gu_headers_observed="+std::to_string(d.before_observed)+","+std::to_string(d.after_observed)
                +" gu_counts="+header_counts(d.before)+"->"+header_counts(d.after)
                +" gu_chunks_equal="+std::to_string(d.before.chunks==d.after.chunks);
            core::log(snapshot.failure==RuntimeFailure::None?core::LogLevel::Info:core::LogLevel::Error,
                "native_audio cohort="+observed->artifacts->identity+" generation="+std::to_string(snapshot.generation)
                +" stage="+native_audio_stage_name(snapshot.stage)+" failure="+native_audio_failure_name(snapshot.failure)
                +" binding="+binding_failure_name(snapshot.binding.failure)+" role="+std::to_string(snapshot.role)
                +details+" mount_us="+std::to_string(timings.mount_us)+" request_us="+std::to_string(timings.request_us)
                +" discover_us="+std::to_string(timings.discover_us)+" bindings_us="+std::to_string(timings.bindings_us)
                +" register_us="+std::to_string(timings.register_us)+" retention=process_exit rollback=none");
        }
    }catch(...){}
    report_failure();
    g_working.clear(std::memory_order_release);
}
}

std::shared_ptr<PreparedNativeAudio> prepare_native_audio(std::shared_ptr<const Artifacts> artifacts,std::uint64_t generation) noexcept {
    try {
        if(!artifacts || !native_audio_layout(artifacts->build_id) || !generation
            || !ff7rp::pipeline::valid_native_audio_identity(artifacts->identity) || !ascii(artifacts->music_name) || !ascii(artifacts->bgm_key)
            || !ascii(artifacts->score_name) || !ascii(artifacts->base_score_name)
            || !artifacts->container_id || !artifacts->container_prefix.is_absolute())return {};
        auto work=std::make_shared<PreparedNativeAudio>();work->artifacts=std::move(artifacts);work->generation=generation;
        work->prefix=work->artifacts->container_prefix.wstring();
        if(work->prefix.empty() || work->prefix.size()+1>256 || work->prefix.find(L'\0')!=std::wstring::npos
            || work->artifacts->container_prefix.lexically_normal()!=work->artifacts->container_prefix
            || work->artifacts->container_prefix.has_extension())return {};
        for(const auto& part:work->artifacts->container_prefix.relative_path())
            if(part==L".." || part==L"." || part.native().find(L':')!=std::wstring::npos)return {};
        constexpr const char* extensions[]={".utoc",".ucas",".pak"};
        std::array<bool,3> file_seen{};
        for(std::size_t i=0;i<3;++i) {
            const auto& f=work->artifacts->files[i];
            bool matched=false;
            for(std::size_t j=0;j<3;++j) {
                auto expected=work->artifacts->container_prefix;expected+=extensions[j];
                if(f.path==expected && !file_seen[j]){file_seen[j]=true;matched=true;break;}
            }
            if(!matched || !f.bytes || !f.content_hash)return {};
            auto h=CreateFileW(f.path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(h==INVALID_HANDLE_VALUE)return {};work->files[i]=h;
            LARGE_INTEGER size{};if(!GetFileSizeEx(h,&size) || size.QuadPart<0 || static_cast<std::uint64_t>(size.QuadPart)!=f.bytes)return {};
            std::uint64_t hash=0;
            if(!compact_file_hash(h,f.bytes,hash) || hash!=f.content_hash)return {};
        }
        constexpr const char* classes[]={"BGMList","PianoScoreInfo","PianoScore"};
        for(std::size_t i=0;i<work->artifacts->packages.size();++i) {
            const auto& p=work->artifacts->packages[i];
            if(p.role!=static_cast<ff7rp::pipeline::NativeAudioPackageRole>(i) || !p.package_id
                || !p.public_export_hash || !ascii(p.object_path) || !ascii(p.class_path))return {};
            for(std::size_t j=0;j<i;++j) {
                const auto& old=work->artifacts->packages[j];
                if(p.package_id==old.package_id || p.object_path==old.object_path)return {};
            }
        }
        if(!work->add_name("Class",true) || !work->add_name("Package",true)
            || !work->add_name("/Script/EndDataObject",true) || !work->add_name("Resident",true))return {};
        for(unsigned char i=0;i<3;++i) {
            const auto& p=work->artifacts->packages[i+1];
            const auto dot=p.object_path.rfind('.');
            if(p.role!=static_cast<ff7rp::pipeline::NativeAudioPackageRole>(i+1) || !p.package_id || !p.public_export_hash
                || p.class_path!=std::string("/Script/EndDataObject.EndDataObject")+classes[i]
                || dot==std::string::npos || p.object_path.substr(dot+1)!=classes[i]
                || !p.object_path.starts_with("/Game/") || !work->add_name(classes[i],true)
                || !work->add_name(std::string("EndDataObject")+classes[i],true)
                || !work->add_name(std::string("/Game/DataObject/Resident/")+classes[i],true)
                || !work->add_name(p.object_path.substr(0,dot),false) || !work->add_name(p.object_path,false))return {};
        }
        if(!work->add_name(work->artifacts->base_score_name,true)
            || !work->add_name(work->artifacts->bgm_key,false) || !work->add_name(work->artifacts->score_name,false)
            || !work->add_name(work->artifacts->music_name,false)
            || !work->add_name("FF7RPianoSongs_"+work->artifacts->score_name,false))return {};
        return work;
    }catch(...){return {};}
}

namespace native_proxy_detail {
EntryContract entry_contract() noexcept {
    const auto* spec=find_hook_spec("native_audio_exe_entry");
    return spec?EntryContract{spec->rva,spec->expected_prologue}:EntryContract{};
}
bool cold_entry_ready(HMODULE module) noexcept {
    const auto base=reinterpret_cast<Address>(module);
    if(!base || !cold_baseline(base))return false;
    // Source-specific pre-CRT witnesses, not reconstructed post-initialization state.
    for(auto offset:{rva::PackageStoreGeneration,rva::NativeBootstrapHinstance,rva::NativeBootstrapTaskGraph,
        rva::NativeBootstrapPool,rva::NativeBootstrapBackgroundPool,
        rva::NativeBootstrapCrashProcess,rva::NativeBootstrapCrashResult,
        rva::NativeBootstrapCrashWorker,rva::NativeBootstrapCrashRequest,rva::NativeBootstrapCrashEvent,
        rva::NativeBootstrapSrwGmalloc,rva::NativeBootstrapSrwReserve,rva::NativeBootstrapSrwLock,rva::NativeBootstrapSrwOutput}) {
        Address value=1;if(!offset || !read(base+offset,value) || value)return false;
    }
    std::uint32_t serial=1,guard=1;unsigned char pool=1;
    for(auto offset:{rva::NativeBootstrapCrashStop,rva::NativeBootstrapCrashComplete}){
        std::uint32_t value=1;if(!offset || !read(base+offset,value) || value)return false;}
    if(!rva::NativeBootstrapGuSerial || !read(base+rva::NativeBootstrapGuSerial,serial) || serial
        || !rva::NativeBootstrapSrwGuard || !read(base+rva::NativeBootstrapSrwGuard,guard) || guard
        || !rva::NativeBootstrapSrwPoolComplete || !read(base+rva::NativeBootstrapSrwPoolComplete,pool) || pool)return false;
    if constexpr(rva::NativeBootstrapIoPool!=0){Address value=1;if(!read(base+rva::NativeBootstrapIoPool,value) || value)return false;}
    return true;
}
}
bool install_native_audio_runtime(HMODULE module,RuntimeServices services) noexcept {
    const bool synchronous=bootstrap_detail::scope_present();
    const bool proxy_entry=native_proxy_detail::scope_present();
    // A worker with CRT=0 could already be inside the cookie API: patching its
    // suspended AEP return is unsafe even if a later entry event never arrives.
    // The literal pre-AEP transaction remains exercised by its injected early
    // source harness, not offered as an unqualified production-worker fallback.
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
    if((synchronous && proxy_entry) || (!synchronous && !proxy_entry && !test_install_hook))return false;
#else
    if(synchronous==proxy_entry)return false;
#endif
    if(g_base || !module || !services.identity || !services.scoreinfo_original
        || !native_audio_layout(core::generated::kBuildId))return false;
    core::PeIdentity identity{};
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
    if(test_install_hook)identity={core::generated::kExeTimestamp,core::generated::kSizeOfImage,core::generated::kPeChecksum};else
#endif
    identity=core::pe_identity(module);
    if(identity.timestamp!=core::generated::kExeTimestamp || identity.size_of_image!=core::generated::kSizeOfImage
        || identity.checksum!=core::generated::kPeChecksum)return false;
    g_base=reinterpret_cast<Address>(module);g_services=services;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if(!read(g_base,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0
        || static_cast<std::uint32_t>(dos.e_lfanew)>identity.size_of_image-sizeof(nt)
        || !read(g_base+dos.e_lfanew,nt) || nt.Signature!=IMAGE_NT_SIGNATURE
        || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || !rva::NativeAudioExeEntry || nt.OptionalHeader.AddressOfEntryPoint!=rva::NativeAudioExeEntry) {
        g_base=0;return false;
    }
    struct Hook {const char* id;void* detour;void** original;};
#define HOOK(id,fn,original) Hook{id,reinterpret_cast<void*>(&fn),reinterpret_cast<void**>(&original)}
    const std::array hooks={
        HOOK("native_audio_exe_entry",ordinary_entry_hook,original_entry),
        HOOK("package_store_factory",factory_hook,original_factory),HOOK("package_store_shutdown",shutdown_hook,original_shutdown),
        HOOK("package_store_destructor",destructor_hook,original_destructor),HOOK("package_store_full_initialize",initialize_hook,original_initialize),
        HOOK("dataobject_manager_lookup",lookup_hook,original_lookup),HOOK("dataobject_manager_append",append_hook,original_append),
        HOOK("dataobject_manager_remove",remove_hook,original_remove),HOOK("dataobject_manager_reset",reset_hook,original_reset),
        HOOK("dataobject_name_helper",name_helper_hook,original_name_helper),HOOK("dataobject_group_reserve",reserve_hook,original_reserve),
        HOOK("native_audio_updater",updater_hook,original_updater)};
#undef HOOK
    if(!rva::FNameCtor){g_base=0;return false;}
    constexpr const char* calls[]={"io_dispatcher_mount","single_soft_path_request","bgmlist_append_bridge",
        "scoreinfo_append_bridge","pianoscore_append_bridge","bgmlist_accessor","pianoscore_accessor","package_store_mount_callback","native_audio_updater_return"};
    for(auto id:calls)if(!signature(id)){g_base=0;return false;}
    for(const auto& h:hooks)if(!find_hook_spec(h.id) || !signature(h.id)){g_base=0;return false;}
    if(synchronous?!bootstrap_detail::begin_publication(module):proxy_entry?!native_proxy_detail::begin_publication(module):!cold_baseline()) {
        invalidate(RuntimeFailure::MissedInitializer);return false;
    }
    g_callbacks.open();
    std::string error;
    for(const auto& h:hooks) {
        // The authenticated API continuation is already inside the AEP/CRT.
        // Never patch its suspended prologue/return PC. The alternate source
        // publishes origin only after every selected-operation barrier is live.
        if(synchronous && h.detour==reinterpret_cast<void*>(&ordinary_entry_hook))continue;
        const auto* s=find_hook_spec(h.id);
        bool installed=false;
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
        if(test_install_hook)installed=test_install_hook(h.id);else
#endif
        installed=g_hooks[g_installed].install(module,s->rva,s->expected_prologue,h.detour,h.original,error);
        if(!installed) {
            if(synchronous)bootstrap_detail::abandon_publication(module);
            if(proxy_entry)native_proxy_detail::abandon_publication(module);
            invalidate(RuntimeFailure::Install);return shutdown_native_audio_runtime() && false;
        }
        ++g_installed;
    }
    // If initializer returned during installation, its callback did not accept
    // capture. Never infer initialization from a later pointer observation.
    if((synchronous?!bootstrap_detail::finish_publication(module):proxy_entry?
            !native_proxy_detail::finish_publication(module,reinterpret_cast<Address>(&ordinary_entry_hook)):!cold_baseline())
        || g_initializer_seen.load(std::memory_order_acquire) || g_callbacks.callbacks_in_flight()
        || g_entry_seen.load(std::memory_order_acquire) || g_failure.load(std::memory_order_acquire)!=RuntimeFailure::None) {
        if(synchronous)bootstrap_detail::abandon_publication(module);
        if(proxy_entry)native_proxy_detail::abandon_publication(module);
        invalidate(RuntimeFailure::MissedInitializer);return shutdown_native_audio_runtime() && false;
    }
    g_accepting.store(true,std::memory_order_release);
    g_hooks_ready.store(true,std::memory_order_release);
    if(synchronous) {
        g_updater_thread.store(GetCurrentThreadId(),std::memory_order_release);
        g_origin.store(true,std::memory_order_release);
    }
    return true;
}
bool enqueue_native_audio(std::shared_ptr<PreparedNativeAudio> work) noexcept {
    if(!work || !g_accepting.load(std::memory_order_acquire) || g_failure.load(std::memory_order_acquire)!=RuntimeFailure::None)return false;
    std::lock_guard lock(g_queue_mutex);
    for(std::size_t i=0;i<g_count;++i)if(g_queue[i].get()==work.get())return true;
    if(g_count==g_queue.size())return false;
    for(std::size_t i=0;i<g_count;++i) {
        if(g_queue[i]==work)return true;
        if(g_queue[i]->artifacts->identity==work->artifacts->identity) {
            const auto& a=*g_queue[i]->artifacts;const auto& b=*work->artifacts;
            if(a.build_id!=b.build_id || a.container_prefix!=b.container_prefix || a.container_id!=b.container_id
                || a.base_score_name!=b.base_score_name || a.music_name!=b.music_name || a.bgm_key!=b.bgm_key || a.score_name!=b.score_name)return false;
            for(std::size_t role=0;role<a.packages.size();++role) {
                const auto& x=a.packages[role];const auto& y=b.packages[role];
                if(x.role!=y.role || x.object_path!=y.object_path || x.class_path!=y.class_path
                    || x.package_id!=y.package_id || x.public_export_hash!=y.public_export_hash)return false;
            }
            for(const auto& file:a.files) {
                bool found=false;
                for(const auto& other:b.files)if(file.path==other.path && file.bytes==other.bytes && file.content_hash==other.content_hash)found=true;
                if(!found)return false;
            }
            work->resident=g_queue[i]->resident?g_queue[i]->resident:g_queue[i];break;
        }
    }
    g_queue[g_count++]=std::move(work);return true;
}
void observe_native_stock_selected_name(std::uint64_t name) noexcept {
    if(!static_cast<std::uint32_t>(name))return;
    std::uint64_t expected=0;
    g_stock_selection.compare_exchange_strong(expected,name,std::memory_order_acq_rel);
}
RuntimeSnapshot native_audio_status(const PreparedNativeAudio& work) noexcept {
    RuntimeSnapshot result{}; result.generation=work.generation;
    const auto& owner=work.resident?*work.resident:work;
    result.stage=owner.stage.load(std::memory_order_acquire); result.failure=owner.failure.load(std::memory_order_acquire);
    if(result.stage==RuntimeStage::Failed){result.role=owner.role;result.binding=owner.binding_failure;}
    const auto failure=g_failure.load(std::memory_order_acquire);
    if(failure!=RuntimeFailure::None){result.stage=failure==RuntimeFailure::Stopped?RuntimeStage::Stopped:RuntimeStage::Failed;result.failure=failure;}
    else if(!g_accepting.load(std::memory_order_acquire))result.stage=RuntimeStage::Stopped;
    else if(!g_captured.load(std::memory_order_acquire))result.stage=RuntimeStage::AwaitingInitializer;
    return result;
}
const char* native_audio_stage_name(RuntimeStage value) noexcept {
    constexpr const char* names[]={"unavailable","awaiting_initializer","awaiting_artifacts","queued","mounting",
        "acquiring","authenticating","registering","ready","failed","stopped","awaiting_stock"};
    const auto index=static_cast<unsigned char>(value);return index<std::size(names)?names[index]:"invalid";
}
const char* native_audio_failure_name(RuntimeFailure value) noexcept {
    constexpr const char* names[]={"none","unsupported_build","install","missed_initializer","replaced","shutdown",
        "provenance","subscription","artifacts","package_store","mount","acquisition","names","discovery",
        "bindings","query","capacity","stopped"};
    const auto index=static_cast<unsigned char>(value);return index<std::size(names)?names[index]:"invalid";
}
static bool validated_native_binding(const PreparedNativeAudio& work,std::uint64_t generation,std::uint64_t& name,
    NativePianoScoreRow* source=nullptr,NativePianoScoreCaptureFacts* facts=nullptr) noexcept {
    name=0;
    if(facts){*facts={};facts->generation=generation;facts->predicate="work_depth";}
    if(g_work_depth)return false;
    std::shared_lock admission(g_admission,std::try_to_lock);
    if(facts)facts->predicate="admission_contention";
    if(!admission.owns_lock())return false;
    if(facts)facts->predicate="admission_closed";
    if(!g_accepting.load(std::memory_order_acquire))return false;
    struct WorkDepth {WorkDepth(){++g_work_depth;} ~WorkDepth(){--g_work_depth;}} depth;
    if(facts)facts->predicate="generation";
    if(generation!=work.generation)return false;
    const auto status=native_audio_status(work);
    if(facts){facts->predicate="readiness";facts->status_observed=true;facts->stage=status.stage;facts->failure=status.failure;}
    if(status.stage!=RuntimeStage::Ready)return false;
    BindingResult checked{};
    {
        std::shared_lock lifetime(g_lifetime,std::try_to_lock);
        if(facts)facts->predicate="lifetime_contention";
        if(!lifetime.owns_lock())return false;
        if(facts)facts->predicate="lifetime_provenance";
        if(!provenance())return false;
        NativeEntryGate::Writer exclusion{g_managers};
        if(facts)facts->predicate="manager_exclusion";
        if(!exclusion)return false;
        gu::UObjectIdentityAccess identity{};
        if(facts)facts->predicate="identity_service";
        if(!g_services.identity(identity))return false;
        const auto& owner=work.resident?*work.resident:work;
        checked=owner.bindings.validate(identity,BindingPhase::CurrentRegistered);
        if(facts){facts->predicate="binding_validation";facts->binding_observed=true;facts->binding_failure=checked.failure;facts->binding_role=checked.role;}
        if(checked) {
            if(facts)facts->predicate="final_admission_closed";
            if(!g_accepting.load(std::memory_order_acquire))return false;
            if(facts)facts->predicate="final_provenance";
            if(!provenance())return false;
            name=owner.selected_name;
            if(facts){facts->name_observed=true;facts->observed_name=name;}
            if(source) {
                const auto row=owner.candidate_rows[2].words[1];
                if(facts)facts->predicate="row_missing";
                if(!row)return false;
                if(facts)facts->predicate="row_is_stock";
                if(row==owner.stock_rows[2].words[1])return false;
                *source={owner.seeds[2].candidate.objects[0],static_cast<std::uintptr_t>(row),name,generation};
            }
            if(facts)facts->predicate=name?"accepted":"selected_name_missing";
            return name!=0;
        }
        invalidate(RuntimeFailure::Bindings);
    }
    // Only copied first-failure facts are formatted after native exclusion ends.
    try {core::log(core::LogLevel::Error,"native_audio selection_rejected generation="+std::to_string(generation)
        +" binding="+binding_failure_name(checked.failure)+" role="+std::to_string(checked.role));}catch(...){}
    return false;
}
bool native_audio_selected_name(const PreparedNativeAudio& work,std::uint64_t generation,std::uint64_t& name) noexcept {
    return validated_native_binding(work,generation,name);
}
bool capture_native_pianoscore_row(const ff7rp::pipeline::NativeAudioArtifactSet& artifacts,
    std::uint64_t selected_name,NativePianoScoreRow& out,NativePianoScoreCaptureFacts* facts) noexcept {
    out={};std::shared_ptr<PreparedNativeAudio> work;
    if(facts){*facts={};facts->predicate="artifact_not_retained";facts->requested_name=selected_name;}
    {
        std::lock_guard lock(g_queue_mutex);
        for(std::size_t i=0;i<g_count;++i) {
            if(g_queue[i]->artifacts.get()==&artifacts){work=g_queue[i];break;}
        }
    }
    NativePianoScoreRow source;std::uint64_t name=0;
    if(!work)return false;
    if(facts)facts->predicate="requested_name_missing";
    if(!selected_name)return false;
    const bool captured=validated_native_binding(*work,work->generation,name,&source,facts);
    if(facts)facts->requested_name=selected_name;
    if(!captured)return false;
    if(facts)facts->predicate="selected_name_mismatch";
    if(name!=selected_name)return false;
    if(facts)facts->predicate="accepted";
    out=source;return true;
}
bool shutdown_native_audio_runtime() noexcept {
    g_accepting.store(false,std::memory_order_release);invalidate(RuntimeFailure::Stopped);
    if(g_work_depth || g_updater_depth || g_invalidator_depth || NativeEntryGate::inside_entry())return false;
    std::unique_lock admission(g_admission,std::try_to_lock);
    if(!admission.owns_lock())return false; // All protective hooks remain installed.
    if(g_callbacks.callbacks_in_flight()!=0)return false;
    // Check AFTER observer leases: a released lease has already published its
    // forwarding guard. Incomplete requires retaining the ASI and all trampolines.
    if(g_entry_forwarding.load(std::memory_order_acquire)!=0
        || g_entry_forwarding_uncertain.load(std::memory_order_acquire))return false;
    bool disabled=true;
    for(std::size_t i=g_installed;i>0;--i) {
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
        if(test_disable_hook){disabled=test_disable_hook(i-1)&&disabled;continue;}
#endif
        disabled=g_hooks[i-1].disable() && disabled;
    }
    g_callbacks.close();
    if(!disabled || g_callbacks.callbacks_in_flight()!=0
        || g_entry_forwarding.load(std::memory_order_acquire)!=0
        || g_entry_forwarding_uncertain.load(std::memory_order_acquire))return false;
    bool removed=true;
    for(std::size_t i=g_installed;i>0;--i) {
#ifdef FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
        if(test_remove_hook){removed=test_remove_hook(i-1)&&removed;continue;}
#endif
        removed=g_hooks[i-1].remove() && removed;
    }
    if(removed)g_installed=0;
    // Intentional retention, including terminal partial cohorts: no native
    // release/removal/unmount contract exists. This storage must outlive hooks.
    return removed;
}
} // namespace ff7r::piano::game::native_audio
