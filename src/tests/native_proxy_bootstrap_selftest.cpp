#ifdef FF7RP_NATIVE_PROXY_UNAVAILABLE_SELFTEST
#include "game/native_proxy_bootstrap.cpp"
#include <cassert>
namespace na=ff7r::piano::game::native_audio;
static unsigned callbacks=0;
static void initialize() noexcept {++callbacks;}
int main(){
    assert(!na::try_owned_proxy_native_audio_startup(reinterpret_cast<HMODULE>(1),reinterpret_cast<HMODULE>(2),3,initialize));
    assert(!callbacks&&!na::native_proxy_detail::scope_present());
    assert(!na::native_proxy_detail::begin_publication(reinterpret_cast<HMODULE>(1)));
    assert(!na::native_proxy_detail::finish_publication(reinterpret_cast<HMODULE>(1),3));
    na::native_proxy_detail::abandon_publication(reinterpret_cast<HMODULE>(1));
    const auto state=na::native_proxy_bootstrap_status();assert(!state.callback_invoked&&!state.barriers_published);
}
#else
#define FF7RP_OWNED_PROXY
#define FF7RP_NATIVE_PROXY_ENTRY_BRIDGE_SELFTEST
#include "proxy/entry_bridge.cpp"
#define FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
#include "game/native_proxy_bootstrap.cpp"
#define FF7RP_NATIVE_AUDIO_RUNTIME_FIXTURE_ONLY
#include "native_audio_runtime_selftest.cpp"
#include <iostream>

// Data-only bridge gateways below never execute an entry thunk or a game image.
namespace ff7rp::proxy {
extern "C" void ff7rp_proxy_entry_thunk() {}
extern "C" void ff7rp_proxy_entry_dispatch_return() {}
}
namespace pb=ff7r::piano::game::native_audio::proxy_bootstrap_impl;
namespace bridge=ff7rp::proxy;
namespace core=ff7r::piano::core;
namespace {
struct ProxyFixture;
ProxyFixture* active=nullptr;
struct ProxyFixture : RuntimeFixture {
    static constexpr na::Address own=0x70000000;
    std::map<na::Address,std::vector<unsigned char>> disk_bytes;
    std::array<IMAGE_NT_HEADERS64,2> headers{};
    std::array<IMAGE_SECTION_HEADER,2> sections{};
    std::array<std::vector<unsigned char>,7> warm;
    unsigned callbacks=0,reads=0;DWORD owner_thread=GetCurrentThreadId();
    bool changed=false,bad_page=false,bad_frame=false,bad_hash=false,bad_path=false,result=false;
    bool enter_during=false,early_activity=false;int reject_hook=-1;
    std::wstring filename=L"d3d9.dll";
    void (*before)(ProxyFixture&)=nullptr;
    void (*during)(ProxyFixture&,unsigned)=nullptr;
    void (*after_apply)(ProxyFixture&)=nullptr;
    void (*before_helper)(ProxyFixture&)=nullptr;
    static constexpr na::Address spans[]={rva::NativeAudioInitialGuHeader,rva::BgmListRegistry,rva::ScoreInfoRegistry,
        rva::PianoScoreRegistry,rva::DataObjectNameMap,rva::PackageStoreOwnerGlobal,rva::PackageStoreGeneration};
    template<class T>void map(na::Address address,const T& value){memory[address].resize(sizeof(T));put(address,value);}
    ProxyFixture(){
        active=this;pb::attempted.clear();pb::failure=na::ProxyBootstrapFailure::None;pb::predicate=nullptr;
        pb::invoked=false;pb::published=false;pb::scope=nullptr;
        bridge::state.stage=bridge::EntryStage::Empty;bridge::state.error=bridge::EntryError::None;
        bridge::state.retained=false;bridge::state.stopped=false;bridge::state.scope=nullptr;
        bridge::clear_proof(bridge::state.proof);
        na::g_base=0;na::g_origin=false;na::g_entry_seen=false;na::g_hooks_ready=false;na::g_accepting=false;
        na::g_installed=0;na::g_failure=na::RuntimeFailure::None;
        for(unsigned i=0;i<7;++i){warm[i]=memory.at(i?base+spans[i]:array);memory[base+spans[i]]=std::vector<unsigned char>(i<4?48:8,0);}
        memory[base+rva::NativeAudioExeEntry]=find_hook_spec("native_audio_exe_entry")->expected_prologue;
        map(base+rva::NativeAudioCrtState,std::uint32_t{0});
        for(auto r:{rva::NativeBootstrapHinstance,rva::NativeBootstrapTaskGraph,rva::NativeBootstrapPool,rva::NativeBootstrapBackgroundPool,
            rva::NativeBootstrapCrashProcess,rva::NativeBootstrapCrashResult,rva::NativeBootstrapCrashWorker,rva::NativeBootstrapCrashRequest,
            rva::NativeBootstrapCrashEvent,rva::NativeBootstrapSrwGmalloc,rva::NativeBootstrapSrwReserve,rva::NativeBootstrapSrwLock,rva::NativeBootstrapSrwOutput})map(base+r,std::uint64_t{0});
        for(auto r:{rva::NativeBootstrapCrashStop,rva::NativeBootstrapCrashComplete,rva::NativeBootstrapGuSerial,rva::NativeBootstrapSrwGuard})map(base+r,std::uint32_t{0});
        map(base+rva::NativeBootstrapSrwPoolComplete,static_cast<unsigned char>(0));
        map(base+rva::NativeBootstrapCookieValue,std::uint64_t{0x123456789abc});
        if constexpr(rva::NativeBootstrapIoPool!=0)map(base+rva::NativeBootstrapIoPool,std::uint64_t{0});
        for(unsigned i=0;i<2;++i){const auto image=i?own:base;
            IMAGE_DOS_HEADER d{};d.e_magic=IMAGE_DOS_SIGNATURE;d.e_lfanew=128;map(image,d);
            auto& h=headers[i];h.Signature=IMAGE_NT_SIGNATURE;h.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
            h.FileHeader.Characteristics=IMAGE_FILE_EXECUTABLE_IMAGE|(i?IMAGE_FILE_DLL:0);
            h.FileHeader.NumberOfSections=1;h.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
            h.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;h.OptionalHeader.ImageBase=image;
            h.OptionalHeader.SizeOfImage=i?0x10000:core::generated::kSizeOfImage;h.OptionalHeader.SizeOfHeaders=0x1000;
            h.FileHeader.TimeDateStamp=i?1:core::generated::kExeTimestamp;h.OptionalHeader.CheckSum=i?2:core::generated::kPeChecksum;
            h.OptionalHeader.AddressOfEntryPoint=i?0x1000:static_cast<DWORD>(rva::NativeAudioExeEntry);
            h.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]={0x3000,24};map(image+128,h);
            auto& s=sections[i];s.VirtualAddress=0x1000;s.SizeOfRawData=h.OptionalHeader.SizeOfImage-0x1000;s.Misc.VirtualSize=s.SizeOfRawData;
            s.Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_EXECUTE;map(image+128+sizeof(h),s);
        }
        const RUNTIME_FUNCTION rows[]={{0x1000,0x1080,{0x4000}},{0x1200,0x1280,{0x4010}}};
        disk_bytes[own+0x3000]={reinterpret_cast<const unsigned char*>(rows),reinterpret_cast<const unsigned char*>(rows)+sizeof(rows)};
        for(const auto& r:rows){memory[own+r.BeginAddress]=std::vector<unsigned char>(0x80,0x90);disk_bytes[own+r.BeginAddress]=memory[own+r.BeginAddress];
            memory[own+r.UnwindData]={1,0,0,0};disk_bytes[own+r.UnwindData]=memory[own+r.UnwindData];}
        pb::test={};pb::test.read=[](auto p,void* out,std::size_t n){++active->reads;return Fixture::read(active,p,out,n);};
        pb::test.open=[](HMODULE m,pb::File& f){const bool self=reinterpret_cast<na::Address>(m)==own;const unsigned i=self?1:0;
            f.dos.e_magic=IMAGE_DOS_SIGNATURE;f.dos.e_lfanew=128;f.nt=active->headers[i];f.sections[0]=active->sections[i];
            f.id.nFileSizeLow=self?12345:static_cast<DWORD>(core::generated::kExeFileSize);
            f.path=self?std::wstring(active->bad_path?L"C:/other/":L"C:/game/")+active->filename:L"C:/game/ff7rebirth_.exe";
            if(!self){const auto hex=std::string_view(core::generated::kExeSha256);
                for(unsigned j=0;j<32;++j){const auto cv=[](char c){return c>='a'?c-'a'+10:c-'0';};f.sha[j]=static_cast<unsigned char>(cv(hex[j*2])*16+cv(hex[j*2+1]));}
                if(active->bad_hash)f.sha[0]^=1;}return true;};
        pb::test.disk=[](const pb::File&,DWORD r,void* out,DWORD n){auto it=active->disk_bytes.upper_bound(own+r);if(it==active->disk_bytes.begin())return false;
            const auto p=std::prev(it);const auto offset=own+r-p->first;if(offset>p->second.size()||n>p->second.size()-offset)return false;
            std::memcpy(out,p->second.data()+offset,n);return true;};
        pb::test.unchanged=[](const pb::File&){return !active->changed;};
        pb::test.module=[](auto p)->HMODULE{
            if(p==reinterpret_cast<na::Address>(&na::try_owned_proxy_native_audio_startup)||p==reinterpret_cast<na::Address>(&initialize))return reinterpret_cast<HMODULE>(own);
            if(p>=base&&p<base+core::generated::kSizeOfImage)return reinterpret_cast<HMODULE>(base);
            if(p>=own&&p<own+0x10000)return reinterpret_cast<HMODULE>(own);return nullptr;};
        pb::test.exe=[](){return reinterpret_cast<HMODULE>(base);};pb::test.thread=[](){return active->owner_thread;};
        pb::test.lookup=[](auto pc,pb::Frame& f){if(pc==reinterpret_cast<na::Address>(&initialize))pc=own+0x1005;
            const DWORD start=pc<own+0x1200?0x1000:0x1200;f={pc,own,{start,start+0x80,{start==0x1000?0x4000u:0x4010u}}};return true;};
        pb::test.capture=[](pb::Frame* f,unsigned){pb::test.lookup(own+0x1005,f[0]);pb::test.lookup(own+0x1205,f[1]);if(active->bad_frame)f[1].pc++;return 2u;};
        pb::test.page=[](auto,HMODULE){return !active->bad_page;};
        bridge::test_gateway=&gateway;
        na::test_install_hook=[](std::string_view id){const auto i=active->installed;if(active->reject_hook==static_cast<int>(i))return false;
            if(i==0)assert(id=="native_audio_exe_entry");
            ++active->installed;if(active->during)active->during(*active,i);
            if(active->enter_during&&i==1)na::ordinary_entry_hook();
            if(active->early_activity&&i==2)na::lookup_hook(reinterpret_cast<void*>(base+rva::BgmListRegistry),0,nullptr,nullptr);return true;};
        na::test_apply_hooks=[]{
            assert(++active->applies==1 && active->installed==12 && na::g_installed==12);
            assert(na::g_services.identity && na::g_services.scoreinfo_original && !na::g_accepting && !na::g_hooks_ready);
            const auto p=base+rva::NativeAudioExeEntry,relay=base+0x8000;
            const auto detour=reinterpret_cast<na::Address>(&na::ordinary_entry_hook);
            std::vector<unsigned char> data={0xff,0x25,0,0,0,0};data.resize(14);
            std::memcpy(data.data()+6,&detour,8);active->memory[relay]=data;
            auto patch=find_hook_spec("native_audio_exe_entry")->expected_prologue;patch[0]=0xe9;
            const auto displacement=static_cast<std::int32_t>(relay-p-5);
            std::memcpy(patch.data()+1,&displacement,4);active->memory[p]=patch;
            if(active->after_apply)active->after_apply(*active);return true;
        };
        na::test_cancel_hooks=nullptr;
        na::test_disable_hook=[](std::size_t){++active->disabled;return true;};
        na::test_remove_hook=[](std::size_t i){++active->removed;if(i==0)active->memory[base+rva::NativeAudioExeEntry]=find_hook_spec("native_audio_exe_entry")->expected_prologue;return true;};
    }
    ~ProxyFixture(){assert(!pb::scope);assert(na::shutdown_native_audio_runtime());pb::test={};bridge::test_gateway=nullptr;active=nullptr;}
    static void initialize() noexcept {
        na::NativeInstallDiagnostic snapshot;
        assert(!na::take_native_install_diagnostic(snapshot));
        ++active->callbacks;if(active->before)active->before(*active);
        active->result=na::install_native_audio_runtime(reinterpret_cast<HMODULE>(base),na::g_services);
        assert(!na::take_native_install_diagnostic(snapshot)); // Outer entry authority still active.
    }
    static bool entered(const bridge::EntryScope&,void*) noexcept {if(active->before_helper)active->before_helper(*active);
        return na::try_owned_proxy_native_audio_startup(reinterpret_cast<HMODULE>(base),reinterpret_cast<HMODULE>(own),own+0x1205,&initialize);}
    bool arm(bool process_start=true){bridge::EntryProfile p{};p.timestamp=core::generated::kExeTimestamp;p.image_size=core::generated::kSizeOfImage;
        p.checksum=core::generated::kPeChecksum;p.entry_rva=static_cast<DWORD>(rva::NativeAudioExeEntry);const auto& bytes=find_hook_spec("native_audio_exe_entry")->expected_prologue;
        p.signature_size=static_cast<std::uint8_t>(bytes.size());std::copy(bytes.begin(),bytes.end(),p.signature.begin());
        return bridge::arm_process_start_entry(reinterpret_cast<void*>(own),DLL_PROCESS_ATTACH,process_start?this:nullptr,p,&entered,nullptr).armed();}
    bool run(){assert(arm());return bridge::EntryBridgeAccess::dispatch()==base+rva::NativeAudioExeEntry&&pb::invoked.load();}
    void reject(){assert(!run()&&!callbacks&&!installed&&!na::g_origin&&!mounts&&!acquires&&!bridges&&!name_calls);}
    void resume(){for(unsigned i=0;i<7;++i)memory[base+spans[i]]=warm[i];}
    static bool write(na::Address p,const void* in,std::size_t n) noexcept {auto it=active->memory.upper_bound(p);if(it==active->memory.begin())return false;
        auto q=std::prev(it);const auto off=p-q->first;if(off>q->second.size()||n>q->second.size()-off)return false;std::memcpy(q->second.data()+off,in,n);return true;}
    static const bridge::Gateway gateway;
    DWORD protection=PAGE_EXECUTE_READ;
};
const bridge::Gateway ProxyFixture::gateway={
        []()noexcept->void*{return reinterpret_cast<void*>(base);},[]()noexcept{return active->owner_thread;},
        [](auto p,bridge::Region& out)noexcept{const bool thunk=p==reinterpret_cast<na::Address>(&bridge::ff7rp_proxy_entry_thunk);
            out={thunk?own:base,thunk?p:base,thunk?4096:core::generated::kSizeOfImage,MEM_COMMIT,MEM_IMAGE,active->protection};return true;},
        [](auto p,void* out,std::size_t n)noexcept{return Fixture::read(active,p,out,n);},write,
        [](auto,std::size_t,DWORD p,DWORD& old)noexcept{old=active->protection;active->protection=p;return true;},
        [](auto,std::size_t)noexcept{return true;}};
}
int main(){
    constexpr auto base=Fixture::base,own=ProxyFixture::own;
    {ProxyFixture f;assert(f.run()&&f.result&&f.callbacks==1&&f.installed==12&&!na::g_origin&&!na::g_captured);
        assert(na::native_proxy_bootstrap_status().barriers_published&&!f.mounts&&!f.acquires&&!f.name_calls);
        assert(na::ordinary_entry_hook()==17&&na::g_origin);f.resume();f.start(true);f.frame();assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready);
        assert(!na::try_owned_proxy_native_audio_startup(reinterpret_cast<HMODULE>(base),reinterpret_cast<HMODULE>(own),own+0x1205,&ProxyFixture::initialize));}
    for(const auto* name:{L"d3d9.dll",L"D3DCOMPILER_43.dll",L"winmm.dll"}){ProxyFixture f;f.filename=name;const auto ran=f.run();
        if(!ran||!f.result){const auto s=na::native_proxy_bootstrap_status();std::cerr<<"owned fixture: "<<(s.predicate?s.predicate:"none")<<" runtime="<<na::native_audio_failure_name(na::g_failure.load())<<"\n";}
        assert(ran&&f.result&&!na::g_origin&&!f.mounts);}
    for(const auto cookie:{0x2b992ddfa232ull,0x7ffe10203040ull}){ProxyFixture f;f.put(base+rva::NativeBootstrapCookieValue,cookie);
        assert(f.run()&&f.result&&!na::g_origin&&!f.mounts&&!f.acquires&&!f.name_calls);}
    {ProxyFixture f;f.memory[own+0x4010].resize(16);f.memory[own+0x4010][0]=0x21;
        const RUNTIME_FUNCTION parent{0x1000,0x1080,{0x4000}};std::memcpy(f.memory[own+0x4010].data()+4,&parent,sizeof(parent));f.disk_bytes[own+0x4010]=f.memory[own+0x4010];
        assert(f.run()&&f.result);}
    {ProxyFixture f;f.memory[own+0x4010].resize(16);f.memory[own+0x4010][0]=0x21;
        const RUNTIME_FUNCTION self{0x1200,0x1280,{0x4010}};std::memcpy(f.memory[own+0x4010].data()+4,&self,sizeof(self));f.disk_bytes[own+0x4010]=f.memory[own+0x4010];f.reject();}
    {ProxyFixture f;assert(!na::try_owned_proxy_native_audio_startup(reinterpret_cast<HMODULE>(base),reinterpret_cast<HMODULE>(own),own+0x1205,&ProxyFixture::initialize));assert(!f.callbacks);}
    {ProxyFixture f;assert(!f.arm(false)&&!bridge::active_entry_scope()&&!f.callbacks);}
    {ProxyFixture f;assert(f.arm());bridge::stop_entry_bridge();assert(bridge::EntryBridgeAccess::dispatch()==base+rva::NativeAudioExeEntry&&!f.callbacks);}
    for(unsigned bad=0;bad<23;++bad){ProxyFixture f;f.before_helper=[](ProxyFixture& x){
            switch(x.reject_hook){case 0:x.bad_hash=true;break;case 1:x.bad_path=true;break;case 2:x.bad_frame=true;break;case 3:x.bad_page=true;break;
            case 4:x.changed=true;break;case 5:++bridge::state.proof.attach_thread;break;case 6:++x.owner_thread;break;
            case 7:bridge::state.proof.proxy=0;break;case 8:bridge::state.proof.image=0;break;case 9:++bridge::state.proof.entry;break;
            case 10:bridge::state.proof.profile.signature[0]^=1;break;case 11:bridge::state.proof.original[0]^=1;break;
            case 12:x.memory[own+0x1000][0]^=1;break;case 13:x.memory[own+0x4010][0]^=1;break;
            case 14:++bridge::state.proof.profile.timestamp;break;case 15:++bridge::state.proof.profile.image_size;break;
            case 16:++bridge::state.proof.profile.checksum;break;case 17:--bridge::state.proof.profile.signature_size;break;
            case 18:++bridge::state.proof.entry_thread;break;case 19:bridge::state.proof.restored=false;break;
            case 20:bridge::state.proof.callback_invoked=false;break;case 21:bridge::state.proof.callback_succeeded=true;break;
            case 22:x.memory[own+128][0]^=1;break;}};
        f.reject_hook=static_cast<int>(bad);f.reject();}
    for(auto r:{rva::NativeAudioCrtState,rva::NativeAudioInitialGuHeader,rva::BgmListRegistry,rva::DataObjectNameMap,
        rva::PackageStoreOwnerGlobal,rva::PackageStoreGeneration,rva::NativeBootstrapCrashComplete,rva::NativeBootstrapTaskGraph}){
        ProxyFixture f;f.put(base+r,std::uint32_t{1});f.reject();}
    for(unsigned i=0;i<12;++i){ProxyFixture f;f.reject_hook=static_cast<int>(i);assert(f.run()&&!f.result&&!na::g_origin&&f.installed==i&&f.removed==i);}
    for(unsigned change=0;change<5;++change){ProxyFixture f;f.reject_hook=-2-static_cast<int>(change);f.after_apply=[](ProxyFixture& x){
        switch(-x.reject_hook-2){case 0:x.changed=true;break;case 1:x.owner_thread++;break;case 2:bridge::state.proof.original[0]^=1;break;
        case 3:x.put(base+rva::NativeAudioCrtState,std::uint32_t{1});break;case 4:x.memory[base+rva::NativeAudioExeEntry][0]=0x90;break;}};
        assert(f.run()&&!f.result&&!na::g_origin&&f.installed==12&&f.removed==12&&!na::native_proxy_bootstrap_status().barriers_published);}
    {ProxyFixture f;f.enter_during=true;assert(f.run()&&!f.result&&!na::g_origin&&f.removed==12);}
    {ProxyFixture f;f.early_activity=true;assert(f.run()&&!f.result&&!na::g_origin&&f.removed==12);}
    std::cout<<"native_proxy_bootstrap_selftest passed\n";
}
#endif
