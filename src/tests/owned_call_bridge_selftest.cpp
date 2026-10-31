#ifdef NDEBUG
#undef NDEBUG
#endif
#define FF7RP_NATIVE_PROXY_ENTRY_BRIDGE_SELFTEST
#define FF7RP_OWNED_CALL_SELFTEST
#include "../proxy/call_bridge.cpp"
#include "../proxy/entry_bridge.cpp"
#include <cassert>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <xmmintrin.h>

namespace p=ff7rp::proxy;
extern "C" unsigned ff7rp_test_call_abi(void*);
namespace {
struct Fixture;
Fixture* fixture=nullptr;
struct Fixture {
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x6000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    std::uintptr_t base=reinterpret_cast<std::uintptr_t>(bytes);
    p::EntryProfile entry{};p::CallProfile call{};
    RUNTIME_FUNCTION function{0x3000,0x300e,0x4000};
    unsigned callbacks=0,observers=0,originals=0,writes=0,protects=0,flushes=0,frees=0;
    unsigned fail_write=0,fail_protect=0,fail_flush=0;
    std::uintptr_t original_return=0;
    bool allocation_fail=false,free_fail=false,partial_write=false,arm_result=false,callback_result=true,root_worker=false,reenter=false;
    const p::CallScope* saved_scope=nullptr;
    p::CallProof saved{};
    p::call_detail::Gateway calls{};
    Fixture(unsigned result=42) {
        assert(bytes);fixture=this;
        p::state.~State();new(&p::state)p::State{};
        p::call_detail::state.~State();new(&p::call_detail::state)p::call_detail::State{};
        p::test_gateway=&root_api;p::test_dispatch_caller=&p::ff7rp_proxy_entry_dispatch_return;
        p::call_detail::test_private_image=true;
        entry={0x12345678,0x6000,0x87654321,0x2000,24,{}};entry.signature.fill(0x90);
        entry.signature[0]=0xb8;entry.signature[1]=42;entry.signature[2]=entry.signature[3]=entry.signature[4]=0;entry.signature[5]=0xc3;
        IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=128;put(0,dos);
        IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
        nt.FileHeader.Characteristics=IMAGE_FILE_EXECUTABLE_IMAGE;nt.FileHeader.NumberOfSections=1;
        nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);nt.FileHeader.TimeDateStamp=entry.timestamp;
        nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt.OptionalHeader.SizeOfImage=entry.image_size;
        nt.OptionalHeader.CheckSum=entry.checksum;nt.OptionalHeader.AddressOfEntryPoint=entry.entry_rva;
        nt.OptionalHeader.SizeOfHeaders=4096;put(128,nt);
        std::memcpy(bytes+0x2000,entry.signature.data(),entry.signature_size);
        const unsigned char caller[]{0x48,0x83,0xec,0x28,0xe8,0xf7,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
        std::memcpy(bytes+0x3000,caller,sizeof(caller));
        // Genuine unwind registration: version1, four-byte prologue, UWOP_ALLOC_SMALL(40).
        const unsigned char unwind[]{1,4,1,0,4,0x42,0,0};std::memcpy(bytes+0x4000,unwind,sizeof(unwind));
        unsigned offset=0x3100;
        const unsigned char head[]{0x49,0xbb};std::memcpy(bytes+offset,head,2);offset+=2;put(offset,reinterpret_cast<std::uintptr_t>(&originals));offset+=8;
        const unsigned char inc[]{0x41,0xff,0x03,0x49,0xba};std::memcpy(bytes+offset,inc,5);offset+=5;put(offset,reinterpret_cast<std::uintptr_t>(&original_return));offset+=8;
        const unsigned char store[]{0x4c,0x8b,0x1c,0x24,0x4d,0x89,0x1a,0xb8};std::memcpy(bytes+offset,store,8);offset+=8;put(offset,result);offset+=4;bytes[offset]=0xc3;
        call.call_rva=0x3004;call.target_rva=0x3100;call.window_size=10;std::memcpy(call.window.data(),bytes+0x3004,10);
        DWORD old=0;assert(VirtualProtect(bytes,0x6000,PAGE_EXECUTE_READ,&old));
        assert(FlushInstructionCache(GetCurrentProcess(),bytes,0x6000));
        assert(RtlAddFunctionTable(&function,1,base));
        calls.allocate=allocate;calls.free=free;calls.write=write;calls.protect=protect;calls.flush=flush;
        p::call_detail::test_gateway=&calls;
    }
    ~Fixture() {
        assert(RtlDeleteFunctionTable(&function));
        // Test isolation only: production never releases exposed transport storage.
        if(p::call_detail::state.relay)assert(VirtualFree(p::call_detail::state.relay,0,MEM_RELEASE));
        assert(VirtualFree(bytes,0,MEM_RELEASE));
        p::test_gateway=nullptr;p::test_dispatch_caller=nullptr;p::call_detail::test_gateway=nullptr;fixture=nullptr;
    }
    template<class T>void put(unsigned at,const T& value){std::memcpy(bytes+at,&value,sizeof(value));}
    void mutate(unsigned at,unsigned char value){DWORD old=0;assert(VirtualProtect(bytes,0x6000,PAGE_READWRITE,&old));bytes[at]=value;assert(VirtualProtect(bytes,0x6000,old,&old));}
    void arm() {
        const auto self=GetModuleHandleW(nullptr);
        assert(p::arm_process_start_entry(self,DLL_PROCESS_ATTACH,reinterpret_cast<void*>(1),entry,root_callback,this).armed());
        assert(p::ff7rp_proxy_entry_dispatch()==base+entry.entry_rva);
    }
    static bool root_callback(const p::EntryScope& scope,void*)noexcept {
        auto& f=*fixture;
        if(f.root_worker) {
            std::thread worker([&]{assert(!p::arm_owned_call(scope,f.call,callback,&f));});worker.join();
            assert(p::owned_call_status().stage==p::CallStage::Empty);
        }
        f.arm_result=p::arm_owned_call(scope,f.call,callback,&f,observer);
        assert(!p::arm_owned_call(scope,f.call,callback,&f));
        return true;
    }
    static bool callback(const p::CallScope& scope,void*)noexcept {
        auto& f=*fixture;++f.callbacks;f.saved_scope=&scope;
        assert(p::active_call_scope()==&scope && scope.capture(f.saved));
        if(f.reenter)(void)ff7rp_test_call_abi(reinterpret_cast<void*>(&p::ff7rp_proxy_call_thunk));
        assert(f.saved.return_pc==f.base+0x3009 && !f.originals);
        assert(!p::ff7rp_proxy_call_dispatch()); // Ordinary/reentrant dispatcher call is not the private thunk.
        assert(p::active_call_scope()==&scope);
        std::thread worker([&]{assert(!p::active_call_scope());p::CallProof copy{};assert(!scope.capture(copy));assert(!p::ff7rp_proxy_call_dispatch());});worker.join();
        _mm_setcsr(_mm_getcsr()^0x6000u);
        return f.callback_result;
    }
    static void observer()noexcept {
        auto& f=*fixture;++f.observers;p::CallProof copy{};
        assert(!p::active_call_scope() && !f.saved_scope->capture(copy) && !f.originals);
        assert(p::owned_call_status().stage==p::CallStage::Completed);
    }
    static void* main_image()noexcept{return fixture->bytes;}
    inline static const p::Gateway root_api{
        main_image,+[]()noexcept{return GetCurrentThreadId();},
        +[](std::uintptr_t at,p::Region& out)noexcept {
            MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m)))return false;
            out={reinterpret_cast<std::uintptr_t>(m.AllocationBase),reinterpret_cast<std::uintptr_t>(m.BaseAddress),m.RegionSize,m.State,MEM_IMAGE,m.Protect};return true;
        },
        +[](std::uintptr_t at,void* out,std::size_t n)noexcept {SIZE_T read=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,n,&read)&&read==n;},
        +[](std::uintptr_t at,const void* in,std::size_t n)noexcept {SIZE_T wrote=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),in,n,&wrote)&&wrote==n;},
        +[](std::uintptr_t at,std::size_t n,DWORD next,DWORD& old)noexcept {return VirtualProtect(reinterpret_cast<void*>(at),n,next,&old)!=FALSE;},
        +[](std::uintptr_t at,std::size_t n)noexcept {return FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),n)!=FALSE;}
    };
    static LPVOID WINAPI allocate(LPVOID at,SIZE_T n,DWORD type,DWORD prot){return fixture->allocation_fail?nullptr:VirtualAlloc(at,n,type,prot);}
    static BOOL WINAPI free(LPVOID at,SIZE_T n,DWORD type){++fixture->frees;return fixture->free_fail?FALSE:VirtualFree(at,n,type);}
    static BOOL WINAPI protect(LPVOID at,SIZE_T n,DWORD next,PDWORD old){if(++fixture->protects==fixture->fail_protect)return FALSE;return VirtualProtect(at,n,next,old);}
    static BOOL WINAPI write(HANDLE process,LPVOID at,LPCVOID data,SIZE_T n,SIZE_T* wrote){
        if(++fixture->writes==fixture->fail_write){
            if(wrote)*wrote=0;
            if(fixture->partial_write)assert(WriteProcessMemory(process,at,data,n/2,wrote));
            return FALSE;
        }
        return WriteProcessMemory(process,at,data,n,wrote);
    }
    static BOOL WINAPI flush(HANDLE process,LPCVOID at,SIZE_T n){if(++fixture->flushes==fixture->fail_flush)return FALSE;return FlushInstructionCache(process,at,n);}
};
}

int main(int argc,char** argv) {
    if(argc>1) {
        Fixture f;f.arm();assert(f.arm_result);
        const std::string mode=argv[1];
        if(mode=="restore_protect")f.fail_protect=f.protects+1;
        if(mode=="restore_write")f.fail_write=f.writes+1;
        if(mode=="restore_flush")f.fail_flush=f.flushes+1;
        if(mode=="restore_reprotect")f.fail_protect=f.protects+2;
        if(mode=="conflict")f.mutate(0x300d,0x90);
        if(mode=="missing_unwind")assert(RtlDeleteFunctionTable(&f.function));
        if(mode=="wrong_caller")(void)ff7rp_test_call_abi(reinterpret_cast<void*>(&p::ff7rp_proxy_call_thunk));
        if(mode=="wrong_thread"){std::thread worker([&]{(void)ff7rp_test_call_abi(f.bytes+0x3000);});worker.join();}
        if(mode=="reentrant")f.reenter=true;
        (void)ff7rp_test_call_abi(f.bytes+0x3000);
        return 99; // The actual shim must stop, never forward uncertain code.
    }
    for(unsigned word:{10u,0u,0xbeefu})for(bool accepted:{false,true}) {
        Fixture f(word);f.root_worker=true;f.callback_result=accepted;f.arm();assert(f.arm_result);
        const auto mxcsr=_mm_getcsr();
        assert(ff7rp_test_call_abi(f.bytes+0x3000)==word && _mm_getcsr()==mxcsr);
        assert(f.originals==1 && f.original_return==f.base+0x3009 && f.callbacks==1 && f.observers==1);
        const auto status=p::owned_call_status();assert(status.relay_retained && status.callback_succeeded==accepted);
        assert(!p::ff7rp_proxy_call_dispatch() && f.callbacks==1);
        assert(!p::call_detail::current_chain()); // Root record/raw copies cannot recreate the live chain.
        assert(reinterpret_cast<WORD(*)()>(f.bytes+0x3000)()==word && f.originals==2 && f.callbacks==1);
    }
    {Fixture f;f.allocation_fail=true;f.arm();assert(!f.arm_result && !p::owned_call_status().relay_retained && !f.frees);}
    {Fixture f;f.call.window[9]^=1;f.arm();assert(!f.arm_result && p::owned_call_status().error==p::CallError::Ownership && !f.writes);}
    {Fixture f;f.call.target_rva++;f.arm();assert(!f.arm_result && p::owned_call_status().error==p::CallError::Profile);}
    {Fixture f;f.fail_write=1;f.free_fail=true;f.arm();assert(!f.arm_result && p::owned_call_status().stage==p::CallStage::FailedRetained && p::owned_call_status().relay_retained && f.frees==1);}
    {Fixture f;f.fail_write=2;f.partial_write=true;f.arm();assert(!f.arm_result && p::owned_call_status().stage==p::CallStage::Rejected && p::owned_call_status().relay_retained && !f.frees && p::call_detail::window(false));}
    for(unsigned step:{1u,2u}) {
        {Fixture f;f.fail_write=step;f.arm();assert(!f.arm_result);assert(p::owned_call_status().relay_retained==(step==2));assert(f.frees==(step==1?1u:0u));}
        {Fixture f;f.fail_flush=step;f.arm();assert(!f.arm_result);assert(p::owned_call_status().relay_retained==(step==2));}
    }
    for(unsigned step:{1u,2u,3u}){Fixture f;f.fail_protect=step;f.arm();assert(!f.arm_result);assert(p::owned_call_status().relay_retained==(step==3));}
    wchar_t path[32768]{};assert(GetModuleFileNameW(nullptr,path,32768));
    for(const wchar_t* mode:{L"restore_protect",L"restore_write",L"restore_flush",L"restore_reprotect",L"conflict",
            L"missing_unwind",L"wrong_caller",L"wrong_thread",L"reentrant"}) {
        std::wstring command=L"\""+std::wstring(path)+L"\" "+mode;STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        assert(CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
        assert(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);DWORD result=0;assert(GetExitCodeProcess(process.hProcess,&result));
        assert(result==0xc0000409);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }
}
