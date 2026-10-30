#ifdef NDEBUG
#undef NDEBUG
#endif
#define FF7RP_NATIVE_PROXY_ENTRY_BRIDGE_SELFTEST
#include "../proxy/entry_bridge.cpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <xmmintrin.h>

namespace p=ff7rp::proxy;
namespace {
struct Fixture;
Fixture* current=nullptr;
struct Fixture {
    alignas(4096) std::array<unsigned char,0x5000> bytes{};
    std::uintptr_t base=reinterpret_cast<std::uintptr_t>(bytes.data());
    static constexpr std::uintptr_t proxy=0x60000000;
    p::EntryProfile profile{};
    unsigned protects=0,writes=0,flushes=0,callbacks=0,completions=0;
    unsigned fail_protect=0,fail_write=0,corrupt_write=0,fail_flush=0,fail_read=0,reads=0;
    DWORD protection=PAGE_EXECUTE_READ,tid=91;
    bool callback_ok=true,reenter=false,mutate_on_protect=false,old_protection_drift=false,execute=false,stop_during_write=false;
    p::EntryProof copied{};
    Fixture() {
        current=this;p::state.~State();new(&p::state)p::State{};
        p::ff7rp_proxy_entry_resume=0;p::test_gateway=&gateway;
        p::test_dispatch_caller=reinterpret_cast<const void*>(&p::ff7rp_proxy_entry_dispatch_return);
        profile.timestamp=0x12345678;profile.image_size=static_cast<std::uint32_t>(bytes.size());
        profile.checksum=0x87654321;profile.entry_rva=0x2000;profile.signature_size=24;
        for(unsigned i=0;i<profile.signature_size;++i)profile.signature[i]=0x90;
        // A synthetic, position-independent entry: return 42; no game image/code.
        profile.signature[0]=0xb8;profile.signature[1]=42;profile.signature[2]=0;
        profile.signature[3]=0;profile.signature[4]=0;profile.signature[5]=0xc3;
        initialize();
    }
    ~Fixture(){p::test_gateway=nullptr;p::test_dispatch_caller=nullptr;current=nullptr;}
    void initialize() {
        IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=128;put(0,dos);
        IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
        nt.FileHeader.Characteristics=IMAGE_FILE_EXECUTABLE_IMAGE;nt.FileHeader.NumberOfSections=1;
        nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);nt.FileHeader.TimeDateStamp=profile.timestamp;
        nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt.OptionalHeader.SizeOfImage=profile.image_size;
        nt.OptionalHeader.CheckSum=profile.checksum;nt.OptionalHeader.AddressOfEntryPoint=profile.entry_rva;
        nt.OptionalHeader.SizeOfHeaders=4096;put(128,nt);
        std::memcpy(reinterpret_cast<void*>(entry()),profile.signature.data(),profile.signature_size);
    }
    template<class T> void put(std::size_t offset,const T& value) {std::memcpy(reinterpret_cast<void*>(base+offset),&value,sizeof(value));}
    std::uintptr_t entry()const{return base+profile.entry_rva;}
    bool original()const{return std::memcmp(reinterpret_cast<void*>(entry()),profile.signature.data(),profile.signature_size)==0;}
    p::EntryResult arm(const void* reserved=reinterpret_cast<void*>(1),unsigned reason=DLL_PROCESS_ATTACH) {
        const auto result=p::arm_process_start_entry(reinterpret_cast<void*>(proxy),reason,reserved,profile,initialize_callback,this,completed_observer);
        assert(!callbacks && !p::active_entry_scope());return result;
    }
    static void* image()noexcept{return reinterpret_cast<void*>(current->base);}
    static DWORD thread()noexcept{return current->tid;}
    static bool region(std::uintptr_t address,p::Region& out)noexcept {
        auto& f=*current;
        if(address==reinterpret_cast<std::uintptr_t>(&p::ff7rp_proxy_entry_thunk)) {
            out={proxy,address,4096,MEM_COMMIT,MEM_IMAGE,PAGE_EXECUTE_READ};return true;
        }
        if(address<f.base || address>=f.base+f.bytes.size())return false;
        out={f.base,f.base,f.bytes.size(),MEM_COMMIT,MEM_IMAGE,f.protection};return true;
    }
    static bool read(std::uintptr_t address,void* out,std::size_t size)noexcept {
        auto& f=*current;if(++f.reads==f.fail_read)return false;
        if(address<f.base || address-f.base>f.bytes.size() || size>f.bytes.size()-(address-f.base))return false;
        std::memcpy(out,reinterpret_cast<void*>(address),size);return true;
    }
    static bool write(std::uintptr_t address,const void* data,std::size_t size)noexcept {
        auto& f=*current;++f.writes;
        assert(p::entry_bridge_status().retention==p::EntryRetention::ProcessLifetime);
        if(f.stop_during_write && f.writes==1)assert(p::stop_entry_bridge().retention==p::EntryRetention::ProcessLifetime);
        if(f.writes==f.fail_write){std::memcpy(reinterpret_cast<void*>(address),data,5);return false;}
        std::memcpy(reinterpret_cast<void*>(address),data,size);
        if(f.writes==f.corrupt_write)*reinterpret_cast<unsigned char*>(address)=0xcc;
        return true;
    }
    static bool protect(std::uintptr_t,std::size_t size,DWORD next,DWORD& old)noexcept {
        auto& f=*current;assert(size==p::kEntryJumpBytes);old=f.protection;
        if(++f.protects==f.fail_protect)return false;
        if(f.old_protection_drift && f.protects==1)old=PAGE_EXECUTE_WRITECOPY;
        f.protection=next;
        if(f.mutate_on_protect && f.protects==1)*reinterpret_cast<unsigned char*>(f.entry())=0xcc;
        return true;
    }
    static bool flush(std::uintptr_t address,std::size_t size)noexcept {
        auto& f=*current;if(++f.flushes==f.fail_flush)return false;
        return !f.execute || FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),size)!=FALSE;
    }
    static bool initialize_callback(const p::EntryScope& scope,void* context)noexcept {
        auto& f=*static_cast<Fixture*>(context);++f.callbacks;
        assert(f.original() && f.protection==PAGE_EXECUTE_READ);
        assert(p::active_entry_scope()==&scope && scope.capture(f.copied));
        assert(f.copied.restored && f.copied.entry_thread==f.tid && f.copied.entry==f.entry());
        p::EntryProof unfinished{};assert(!p::completed_entry_proof(unfinished));
        ++f.tid;assert(!p::active_entry_scope());assert(!scope.capture(unfinished));--f.tid;
        assert(p::active_entry_scope()==&scope);
        if(f.reenter)assert(!p::ff7rp_proxy_entry_dispatch());
        if(f.execute)_mm_setcsr(_mm_getcsr()^0x6000u);
        return f.callback_ok;
    }
    static void completed_observer()noexcept {
        assert(++current->completions==1);
        assert(!p::active_entry_scope() && p::entry_bridge_status().stage==p::EntryStage::Completed);
        p::EntryProof proof{};assert(p::completed_entry_proof(proof));
        assert(current->original()); // Restored code has not been called by this dispatch yet.
    }
    inline static const p::Gateway gateway{image,thread,region,read,write,protect,flush};
};

void unwind_test() {
    const auto address=reinterpret_cast<DWORD64>(&p::ff7rp_proxy_entry_thunk);
    DWORD64 image=0;auto* function=RtlLookupFunctionEntry(address,&image,nullptr);assert(function);
    const auto* info=reinterpret_cast<const unsigned char*>(image+function->UnwindData);
    assert((info[0]&7)==1 && (info[3]&15)==5 && (info[3]>>4)==0);
    bool xmm[16]{};unsigned saved=0;
    for(unsigned i=0;i<info[2];++i) {
        const unsigned op=info[4+2*i+1]&15,reg=info[4+2*i+1]>>4;
        if(op==8){assert(reg>=6);xmm[reg]=true;++i;}
        else if(op==4){++saved;++i;}
        else if(op==1)i+=reg?2:1;
        else assert(op==0 || op==3);
    }
    assert(saved==7);for(unsigned i=6;i<16;++i)assert(xmm[i]);
    alignas(16) std::array<unsigned char,2048> stack{};
    const auto frame=reinterpret_cast<DWORD64>(stack.data()+512);
    auto store=[&](unsigned offset,DWORD64 value){std::memcpy(reinterpret_cast<void*>(frame+offset),&value,8);};
    store(0x2a0,0xabcde);store(0x2a8,0x123456789);
    const unsigned slots[]{0x38,0x40,0x48,0x70,0x78,0x80,0x88};
    for(unsigned i=0;i<7;++i)store(slots[i],100+i);
    for(unsigned i=6;i<16;++i){store(0x140+16*i,200+i);store(0x148+16*i,300+i);}
    CONTEXT c{};c.ContextFlags=CONTEXT_ALL;c.Rbp=frame;c.Rsp=frame-8;
    // Simulate the temporary pushfq immediately after the recorded prologue.
    c.Rip=image+function->BeginAddress+info[1]+1;
    PVOID handler=nullptr;DWORD64 establisher=0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,c.Rip,function,&c,&handler,&establisher,nullptr);
    assert(c.Rsp==frame+0x2b0 && c.Rip==0x123456789 && c.Rbp==0xabcde);
    assert(c.Rbx==100 && c.Rsi==101 && c.Rdi==102 && c.R12==103 && c.R13==104 && c.R14==105 && c.R15==106);
    const M128A* registers=&c.Xmm0;for(unsigned i=6;i<16;++i)assert(registers[i].Low==200+i && registers[i].High==300+i);
    const unsigned char epilogue[]{0x48,0x8d,0xa5,0xa0,0x02,0,0,0x5d,0xff,0x25};
    DWORD64 epilogue_pc=0;
    for(DWORD i=function->BeginAddress;i+sizeof(epilogue)<=function->EndAddress;++i)
        if(!std::memcmp(reinterpret_cast<void*>(image+i),epilogue,sizeof(epilogue))){epilogue_pc=image+i;break;}
    assert(epilogue_pc);
    for(const unsigned offset:{0u,7u,8u}) {
        CONTEXT e=c;e.Rip=epilogue_pc+offset;
        e.Rsp=offset==0?frame:offset==7?frame+0x2a0:frame+0x2a8;
        e.Rbp=offset==8?0xabcde:frame;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,e.Rip,function,&e,&handler,&establisher,nullptr);
        assert(e.Rsp==frame+0x2b0 && e.Rip==0x123456789 && e.Rbp==0xabcde);
    }
}
} // namespace

int main() {
    // Exercise the production bounded gateway on self-owned stack memory only.
    {std::uint32_t source=0x12345678,copy=0,replacement=0x87654321;
     assert(p::native.read(reinterpret_cast<std::uintptr_t>(&source),&copy,sizeof(copy)) && copy==source);
     assert(p::native.write(reinterpret_cast<std::uintptr_t>(&source),&replacement,sizeof(replacement)) && source==replacement);
     assert(!p::native.read(1,&copy,sizeof(copy)) && !p::native.write(1,&replacement,sizeof(replacement)));}
    using S=p::EntryStage;using E=p::EntryError;using R=p::EntryRetention;
    {Fixture f;assert(f.arm().armed());assert(!f.original());
     assert(f.arm().error==E::Duplicate);assert(p::ff7rp_proxy_entry_dispatch()==f.entry());
     assert(f.original() && f.callbacks==1 && !p::active_entry_scope());
     p::EntryProof proof{};assert(p::completed_entry_proof(proof) && proof.callback_succeeded);
     assert(p::entry_bridge_status().retention==R::ProcessLifetime);
     assert(!p::ff7rp_proxy_entry_dispatch() && f.callbacks==1 && f.original());}
    for(unsigned reason:{0u,2u,3u}){Fixture f;assert(f.arm(reinterpret_cast<void*>(1),reason).error==E::Attach);assert(!f.writes);}
    {Fixture f;assert(f.arm(nullptr).error==E::Attach && !f.writes);}
    {Fixture f;f.profile.timestamp^=1;assert(f.arm().error==E::Image && !f.writes);}
    {Fixture f;f.put(0,std::uint16_t{0});assert(f.arm().error==E::Image && !f.writes);}
    {Fixture f;const auto r=p::arm_process_start_entry(reinterpret_cast<void*>(Fixture::proxy+1),1,reinterpret_cast<void*>(1),f.profile,Fixture::initialize_callback,&f);
     assert(r.error==E::Module && !f.writes);}
    {Fixture f;f.profile.entry_rva+=16;assert(f.arm().error==E::Image && !f.writes);}
    {Fixture f;f.profile.signature_size=13;assert(f.arm().error==E::Profile && !f.writes);}
    {Fixture f;f.profile.signature[2]^=1;assert(f.arm().error==E::Code && !f.writes);}
    {Fixture f;*reinterpret_cast<unsigned char*>(f.entry())=0xe9;assert(f.arm().error==E::Code && !f.writes);}
    {Fixture f;f.protection=PAGE_READWRITE;assert(f.arm().error==E::Code && !f.writes);}
    {Fixture f;f.mutate_on_protect=true;assert(f.arm().error==E::Code && !f.writes);}
    {Fixture f;f.old_protection_drift=true;assert(f.arm().error==E::Code && !f.writes && f.protection==PAGE_EXECUTE_WRITECOPY);}
    {Fixture f;f.fail_protect=1;assert(f.arm().error==E::Protection && f.original() && !f.writes);}
    for(unsigned defect=0;defect<4;++defect) {
        Fixture f;if(defect==0)f.fail_write=1;if(defect==1)f.corrupt_write=1;if(defect==2)f.fail_flush=1;if(defect==3)f.fail_protect=2;
        const auto r=f.arm();assert(r.stage==S::Rejected && r.retention==R::Unreferenced && f.original() && !f.callbacks);
    }
    {Fixture f;f.fail_write=1;f.fail_protect=3;const auto r=f.arm();assert(r.stage==S::FailedRetained && r.retention==R::ProcessLifetime);}
    {Fixture f;f.fail_write=1;f.fail_flush=2;assert(f.arm().stage==S::FailedRetained);}
    {Fixture f;f.corrupt_write=1;f.fail_write=2;assert(f.arm().stage==S::FailedRetained);}
    {Fixture f;assert(f.arm().armed());p::stop_entry_bridge();assert(p::ff7rp_proxy_entry_dispatch()==f.entry());
     assert(!f.callbacks && f.original() && p::entry_bridge_status().error==E::Stopped);}
    {Fixture f;f.stop_during_write=true;assert(f.arm().armed());assert(p::ff7rp_proxy_entry_dispatch()==f.entry());
     assert(!f.callbacks && f.original() && p::entry_bridge_status().error==E::Stopped);}
    {Fixture f;p::stop_entry_bridge();assert(f.arm().error==E::Stopped && !f.writes);}
    {Fixture f;f.callback_ok=false;assert(f.arm().armed());assert(p::ff7rp_proxy_entry_dispatch()==f.entry());
     assert(f.callbacks==1 && f.original() && p::entry_bridge_status().error==E::Callback);
     assert(!p::ff7rp_proxy_entry_dispatch() && f.callbacks==1);}
    {Fixture f;f.reenter=true;assert(f.arm().armed());assert(!p::ff7rp_proxy_entry_dispatch());
     assert(f.callbacks==1 && f.original() && p::entry_bridge_status().stage==S::FailedRetained);}
    {Fixture f;assert(f.arm().armed());p::test_dispatch_caller=nullptr;
     assert(!p::ff7rp_proxy_entry_dispatch() && !f.callbacks && p::entry_bridge_status().error==E::EntryOwnership);}
    for(unsigned defect=0;defect<8;++defect) {
        Fixture f;assert(f.arm().armed());
        if(defect==0)f.fail_protect=3;if(defect==1)f.fail_write=2;if(defect==2)f.fail_flush=2;if(defect==3)f.fail_protect=4;
        if(defect==4)*reinterpret_cast<unsigned char*>(f.entry())=0xcc;
        if(defect==5)++f.tid;
        if(defect==6)*reinterpret_cast<unsigned char*>(f.entry()+20)=0xcc;
        if(defect==7)f.fail_read=f.reads+2; // Ownership reread after making writable.
        assert(!p::ff7rp_proxy_entry_dispatch() && !f.callbacks);
        assert(p::entry_bridge_status().stage==S::FailedRetained && p::stop_entry_bridge().retention==R::ProcessLifetime);
    }
    unwind_test();
    {Fixture f;auto* executable=VirtualAlloc(nullptr,f.bytes.size(),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(executable);
     f.base=reinterpret_cast<std::uintptr_t>(executable);f.execute=true;f.initialize();
     p::test_dispatch_caller=nullptr; // Real assembled call-site provenance, not the mapped test override.
     auto recorder=[&](std::uintptr_t code,std::uintptr_t output,bool tail) {
         std::size_t pos=0;auto byte=[&](unsigned char v){reinterpret_cast<unsigned char*>(code)[pos++]=v;};
         auto relative=[&](std::uintptr_t target){const auto d=static_cast<std::int32_t>(target-(code+pos+4));
             std::memcpy(reinterpret_cast<void*>(code+pos),&d,4);pos+=4;};
         // RIP-relative stores record every GPR without borrowing/clobbering one.
         for(unsigned reg=0;reg<16;++reg){byte(reg>=8?0x4c:0x48);byte(0x89);byte(static_cast<unsigned char>(5+8*(reg&7)));relative(output+8*reg);}
         byte(0x9c);byte(0x8f);byte(0x05);relative(output+128); // RFLAGS.
         byte(0x0f);byte(0xae);byte(0x05);relative(output+0x100); // Aligned FXSAVE.
         if(tail){byte(0xe9);relative(f.entry());}
         else {byte(0xb8);byte(42);byte(0);byte(0);byte(0);byte(0xc3);}
     };
     recorder(f.base+0x1000,f.base+0x3000,true);recorder(f.entry(),f.base+0x3800,false);
     f.profile.signature_size=32;std::memcpy(f.profile.signature.data(),reinterpret_cast<void*>(f.entry()),32);
     assert(FlushInstructionCache(GetCurrentProcess(),executable,f.bytes.size()));
     const auto mxcsr=_mm_getcsr();assert(f.arm().armed());
     const auto entry=reinterpret_cast<int(*)()>(f.base+0x1000);assert(entry()==42);
     assert(f.callbacks==1 && f.original() && _mm_getcsr()==mxcsr);
     assert(!std::memcmp(reinterpret_cast<void*>(f.base+0x3000),reinterpret_cast<void*>(f.base+0x3800),136));
     assert(!std::memcmp(reinterpret_cast<void*>(f.base+0x3100),reinterpret_cast<void*>(f.base+0x3900),2)); // x87 control.
     assert(!std::memcmp(reinterpret_cast<void*>(f.base+0x3118),reinterpret_cast<void*>(f.base+0x3918),8)); // MXCSR/mask.
     assert(!std::memcmp(reinterpret_cast<void*>(f.base+0x31a0),reinterpret_cast<void*>(f.base+0x39a0),256)); // XMM0..15.
      assert(p::entry_bridge_status().stage==S::Completed && f.completions==1);assert(VirtualFree(executable,0,MEM_RELEASE));}
    std::puts("native proxy entry bridge selftest passed (synthetic image only)");
    return 0;
}
