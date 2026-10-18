// Actual helper/runtime; only copied unwind/file/memory/native gateways doubled.
#define FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
#include "game/native_audio_bootstrap.cpp"
#define FF7RP_NATIVE_AUDIO_RUNTIME_FIXTURE_ONLY
#include "native_audio_runtime_selftest.cpp"
#include <psapi.h>
namespace bs=na::bootstrap_impl;
namespace core=ff7r::piano::core;
constexpr bool older_bootstrap=std::string_view(core::generated::kBuildId)=="ff7rebirth-steam-win64-68fd6fde";
namespace {
struct BootstrapFixture;
BootstrapFixture* boot=nullptr;
struct BootstrapFixture : RuntimeFixture {
    static constexpr na::Address loader=0x70000000,asi=0x80000000,system=0x90000000,crt=0xa0000000;
    bool post=true,directory=false,file_changed=false,wrong_hash=false,wrong_proxy_hash=false,wrong_size=false,wrong_export=false;
    bool disk_drift=false,no_frames=false,install_result=false,noop=false,recurse=false,wrong_path=false;
    std::wstring proxy_name=L"xinput1_3.dll";
    void (*before_install)(BootstrapFixture&)=nullptr;
    void (*during_install)(BootstrapFixture&,unsigned)=nullptr;
    na::Address import_slot=0;
    int mutate_install=-1,fail_at=-1;
    unsigned callbacks=0,captures=0,memory_reads=0;
    bool drift_crt_code=false;
    DWORD actual_thread=GetCurrentThreadId();
    std::vector<bs::Frame> stack;
    std::vector<bs::Frame> reconstructed,srw_rows;
    bool bad_crt_hash=false,bad_crt_path=false,unwind_unavailable=false;
    unsigned unwind_steps=0;
    unsigned source_mutation=0;
    RUNTIME_FUNCTION export_entry{0x1000,0x1100,{0x2000}};
    std::vector<RUNTIME_FUNCTION> own_rows{export_entry};
    DWORD export_offset=0x1000;
    const bs::File* actual_self=nullptr;
    const bs::File* actual_crt=nullptr;const bs::File* actual_nt=nullptr;
    static constexpr bs::Address ntbase=0xb0000000,clone=ntbase+bs::kNtdllSize;
    MEMORY_BASIC_INFORMATION mapping{};bool mapped_identity=true,nt_path=true,bad_nt_hash=false,scp_mapping_drift=false;bs::Address scp_drift=0;
    void first_scp();
    std::map<na::Address,std::vector<unsigned char>> disk_unwind;
    std::vector<std::pair<na::Address,std::vector<unsigned char>>> saved;
    IMAGE_NT_HEADERS64 exe_nt{},loader_nt{},asi_nt{},crt_nt{};
    IMAGE_SECTION_HEADER exe_section{},loader_section{},asi_section{},crt_section{};
    BootstrapFixture(bool initialized=true,bool subdirectory=false):post(initialized),directory(subdirectory) {
        artifacts->build_id=core::generated::kBuildId;
        boot=this;bs::attempted.clear();bs::scope=nullptr;
        bs::image_failure_claimed.clear();bs::image_failure_ready.store(false);
        bs::frame_failure_claimed.clear();bs::frame_failure_ready.store(false);
        bs::diagnostic_source.store(na::BootstrapSource::None);bs::diagnostic_failure.store(na::BootstrapFailure::None);
        bs::diagnostic_thread.store(0);bs::diagnostic_frames.store(0);bs::diagnostic_invoked.store(false);bs::diagnostic_published.store(false);
        bs::diagnostic_frame.store(0);bs::diagnostic_observed.store(0);bs::diagnostic_expected.store(0);
        na::g_base=0;na::g_origin.store(false);na::g_entry_seen.store(false);na::g_hooks_ready.store(false);na::g_accepting.store(false);
        const std::array managers={rva::BgmListRegistry,rva::ScoreInfoRegistry,rva::PianoScoreRegistry};
        for(auto r:managers){saved.emplace_back(base+r,memory[base+r]);memory[base+r].assign(48,0);}
        saved.emplace_back(base+rva::DataObjectNameMap,memory[base+rva::DataObjectNameMap]);memory[base+rva::DataObjectNameMap].assign(8,0);
        auto map=[&](auto address,auto value){memory[address].resize(sizeof(value));put(address,value);};
        map(base+rva::NativeAudioCrtState,post?2u:0u);
        std::array<unsigned char,48> gu_header{};if(post){std::fill(gu_header.begin()+4,gu_header.begin()+8,static_cast<unsigned char>(255));gu_header[12]=1;}
        map(base+rva::NativeAudioInitialGuHeader,gu_header);map(base+rva::NativeBootstrapGuSerial,post?1000u:0u);
        map(base+rva::NativeBootstrapCrashComplete,static_cast<unsigned char>(post));
        map(base+rva::NativeBootstrapCrashStop,0u);map(base+rva::PackageStoreGeneration,0u);
        for(auto r:{rva::NativeBootstrapHinstance,rva::NativeBootstrapTaskGraph,rva::NativeBootstrapPool,
            rva::NativeBootstrapBackgroundPool,rva::PackageStoreOwnerGlobal,rva::NativeBootstrapCrashProcess,rva::NativeBootstrapCrashResult})map(base+r,std::uint64_t{0});
        if constexpr(rva::NativeBootstrapIoPool!=0)map(base+rva::NativeBootstrapIoPool,std::uint64_t{0});
        for(auto r:{rva::NativeBootstrapCrashWorker,rva::NativeBootstrapCrashRequest,rva::NativeBootstrapCrashEvent})map(base+r,post?std::uint64_t{123}:0ull);
        map(base+rva::NativeBootstrapCookieValue,0x2b992ddfa232ull);
        for(auto id:bs::game_windows){const auto* spec=find_rva_signature(id);assert(spec);memory[base+spec->rva]=spec->expected_prologue;}
        for(const auto& w:bs::loader_windows){auto& bytes=memory[loader+w.rva];bytes.resize(w.hex.size()/2);
            for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(bs::nibble(w.hex[i*2])*16+bs::nibble(w.hex[i*2+1]));}
        const auto continuation=post?rva::NativeBootstrapShow+0x22:rva::NativeBootstrapCookieTime+6;
        std::array<unsigned char,6> call{};assert(Fixture::read(this,base+continuation-6,call.data(),call.size()));
        std::int32_t displacement=0;std::memcpy(&displacement,call.data()+2,4);
        // LoadPluginsAndRestoreIAT already restored this slot before the export.
        import_slot=base+continuation+displacement;map(import_slot,system+0x2000);
        exe_nt=pe(base,core::generated::kExeTimestamp,core::generated::kSizeOfImage,core::generated::kPeChecksum,static_cast<DWORD>(rva::NativeAudioExeEntry));
        loader_nt=pe(loader,0x6a1ee839,0x12a000,0x37d79f,0x4e850);
        asi_nt=pe(asi,1,0x100000,2,0x1000);build_stack();
        for(const auto& frame:stack)if(frame.image==base || frame.image==loader || frame.image==asi){
            memory[frame.image+frame.function.UnwindData]={1,0,0,0};disk_unwind[frame.image+frame.function.UnwindData]={1,0,0,0};}
        if constexpr(older_bootstrap)older_unwind();
        bs::test={
            [](auto address,void* out,std::size_t n){++boot->memory_reads;const bool copied=Fixture::read(boot,address,out,n);
                if(copied && boot->drift_crt_code && address==crt+bs::crt_windows[3].rva) {
                    boot->drift_crt_code=false;boot->memory[crt+bs::crt_windows[0].rva][0]^=1;
                }
                if(copied && address==clone && n==688 && boot->scp_drift){
                    auto it=boot->memory.upper_bound(boot->scp_drift);assert(it!=boot->memory.begin());--it;
                    it->second[boot->scp_drift-it->first]^=1;boot->scp_drift=0;
                }
                if(copied && address==clone && n==688 && boot->scp_mapping_drift){boot->scp_mapping_drift=false;boot->mapping.Protect=PAGE_EXECUTE_READWRITE;}
                return copied;},
            [](HMODULE module,bs::File& file){return boot->open(module,file);},
            [](const bs::File&){return !boot->file_changed;},
            [](auto address)->HMODULE {
                if(address==reinterpret_cast<na::Address>(&na::try_synchronous_native_audio_startup))return reinterpret_cast<HMODULE>(asi);
                if(address>=base && address<base+core::generated::kSizeOfImage)return reinterpret_cast<HMODULE>(base);
                if(address>=loader && address<loader+0x12a000)return reinterpret_cast<HMODULE>(loader);
                if(address>=asi && address<asi+0x100000)return reinterpret_cast<HMODULE>(asi);
                 if(address>=system && address<system+0x100000)return reinterpret_cast<HMODULE>(system);
                 if(address>=crt && address<crt+bs::kCrtImageSize)return reinterpret_cast<HMODULE>(crt);
                return nullptr;
            },
            [](HMODULE)->bs::Address{return asi+boot->export_offset+(boot->wrong_export?0x300:0);},
            [](bs::Frame* out,unsigned capacity){++boot->captures;if(boot->no_frames)return 0u;
                const auto n=static_cast<unsigned>(std::min<std::size_t>(capacity,boot->stack.size()));std::copy_n(boot->stack.data(),n,out);return n;},
              [](const bs::File& file,DWORD pc,RUNTIME_FUNCTION& out){const auto image=file.path.ends_with(L".exe")?base:file.path.ends_with(L".asi")?asi:file.path.ends_with(L"ucrtbase.dll")?crt:file.path.ends_with(L"ntdll.dll")?ntbase:loader;
                  if(image==ntbase){if(pc>=0x16e0c0 && pc<0x16e103){out={0x16e0c0,0x16e103,{0x1b6914}};return true;}return false;}
                 if(image==asi){for(const auto& row:boot->own_rows)if(pc>=row.BeginAddress && pc<row.EndAddress){out=row;return true;}return false;}
                 if(!boot->srw_rows.empty()){for(const auto& f:boot->srw_rows)if(f.image==image && pc>=f.function.BeginAddress && pc<f.function.EndAddress){out=f.function;return true;}return false;}
                for(const auto& frame:boot->stack)if(frame.image==image && frame.pc-image==pc){out=frame.function;if(boot->disk_drift)out.UnwindData^=8;return true;}return false;},
            []{return boot->actual_thread;},[]{return reinterpret_cast<HMODULE>(base);},
            [](HMODULE module){return module==reinterpret_cast<HMODULE>(system);},
            [](const bs::File& file,DWORD rva,void* out,DWORD n){
                  const auto image=file.path.ends_with(L".exe")?base:file.path.ends_with(L".asi")?asi:file.path.ends_with(L"ucrtbase.dll")?crt:file.path.ends_with(L"ntdll.dll")?ntbase:loader;const auto address=image+rva;
                auto it=boot->disk_unwind.upper_bound(address);if(it==boot->disk_unwind.begin())return false;--it;
                const auto offset=address-it->first;if(offset>it->second.size() || n>it->second.size()-offset)return false;
                std::memcpy(out,it->second.data()+offset,n);return true;
            },
              [](bs::Address pc,bs::Frame& out){if(pc==asi+boot->export_offset){out={pc,asi,boot->export_entry};return true;}
                  if(pc==ntbase+0x16e0c0){out=frame(ntbase,0x16e0c0,0x16e0c0,0x16e103,0x1b6914);return true;}
                 for(const auto& f:boot->srw_rows)if(pc>=f.image+f.function.BeginAddress && pc<f.image+f.function.EndAddress){out=f;out.pc=pc;return true;}return false;}
        };
        na::test_install_hook=[](std::string_view id){assert(id!="native_audio_exe_entry");const unsigned index=boot->installed++;
            assert(!na::g_origin.load() && bs::scope && bs::scope->publication==bs::Publication::Installing);
            if(static_cast<int>(index)==boot->mutate_install)boot->put(base+rva::NativeBootstrapTaskGraph,std::uint64_t{1});
            if(boot->during_install)boot->during_install(*boot,index);
            return static_cast<int>(index)!=boot->fail_at;};
        na::test_disable_hook=[](std::size_t){++current->disabled;return true;};
        na::test_remove_hook=[](std::size_t){++current->removed;return true;};
    }
    ~BootstrapFixture(){bs::test={};boot=nullptr;}
    IMAGE_NT_HEADERS64 pe(na::Address module,DWORD stamp,DWORD size,DWORD checksum,DWORD aep) {
        memory[module].resize(4096);IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=128;put(module,dos);
        IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
        nt.FileHeader.NumberOfSections=1;nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
        nt.FileHeader.TimeDateStamp=stamp;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt.OptionalHeader.ImageBase=module==base?0x140000000ull:0x180000000ull;
        nt.OptionalHeader.SizeOfHeaders=4096;nt.OptionalHeader.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        nt.OptionalHeader.DllCharacteristics=IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]={size-4096,128};
        nt.OptionalHeader.SizeOfImage=size;nt.OptionalHeader.CheckSum=checksum;nt.OptionalHeader.AddressOfEntryPoint=aep;put(module+128,nt);
        IMAGE_SECTION_HEADER section{};std::memcpy(section.Name,".text",5);section.VirtualAddress=4096;
        section.Misc.VirtualSize=size-4096;section.SizeOfRawData=1024;section.PointerToRawData=4096;
        section.Characteristics=IMAGE_SCN_CNT_CODE|IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;
        (module==base?exe_section:module==loader?loader_section:module==crt?crt_section:asi_section)=section;
        put(module+128+sizeof(nt),section);return nt;
    }
    static bs::Frame frame(na::Address module,DWORD pc,DWORD begin,DWORD end,DWORD unwind){return {module+pc,module,{begin,end,{unwind}}};}
    void unwind(DWORD rva,std::vector<unsigned char> bytes,na::Address image=asi) {
        disk_unwind[image+rva]=bytes;memory[image+rva]=std::move(bytes);
    }
    void older_unwind() {
        // Imported old records, not the newer fixture's invented no-op entries.
        // Handler RVAs are synthetic test data; no exception handler is executed.
        constexpr bs::Window old_unwind[]={
            {0x06d12688,"0104010004420000"}, {0x06d799c4,"010d04000d340a000d520650"},
            {0x06d7996c,"090a04000a3408000a520670"}, {0x06d1fd10,"0107020007011300"},
            {0x06d126d4,"0106020006320230"}, {0x06c84cf4,"010d01000d420000"},
            {0x06c8ed6c,"21000000bcf48300bbf58300784fc806"}, {0x06c84f78,"01180600186407001834060018320b70"},
            {0x06da93d4,"01220b00223422002201180010f00ee00cd00ac00870076006500000"},
            {0x06c93878,"2100000024b48b004ab48b00f44cc806"},
            {0x06c917ac,"013e0f003e78090035680a00223420002201160010f00ee00cd00ac00870076006500000"},
            {0x06c9175c,"192d09001b548e001b348d001b0188000ee00c700b600000"}};
        for(const auto& w:old_unwind){std::vector<unsigned char> data(w.hex.size()/2);
            for(unsigned i=0;i<data.size();++i)data[i]=static_cast<unsigned char>(bs::nibble(w.hex[i*2])*16+bs::nibble(w.hex[i*2+1]));
            if((data[0]>>3)&3)data.resize(((4+2*data[2]+3)&~3u)+4,0);
            unwind(static_cast<DWORD>(w.rva),std::move(data),base);}
    }
    void chained_export() {
        export_entry={0x1000,0x1010,{0x2000}};
        own_rows={export_entry,{0x1010,0x1044,{0x2100}},{0x1044,0x1072,{0x2200}}};
        // Actual MSVC shape from the installed export: primary allocation,
        // secondary save, then secondary zero-prologue continuation, both chained.
        unwind(0x2000,{0x01,4,1,0,4,0x62,0,0});
        for(unsigned i=1;i<own_rows.size();++i){std::vector<unsigned char> bytes=i==1
            ?std::vector<unsigned char>{0x21,5,2,0,5,0x34,6,0}:std::vector<unsigned char>{0x21,0,0,0};
            const auto old=bytes.size();bytes.resize(old+sizeof(export_entry));std::memcpy(bytes.data()+old,&export_entry,sizeof(export_entry));
            unwind(own_rows[i].UnwindData,std::move(bytes));}
        stack[0]={asi+0x1044,asi,own_rows[2]};
    }
    void rejected_wrapper(unsigned captured,const RUNTIME_FUNCTION* observed=nullptr) {
        assert(directory);const RUNTIME_FUNCTION helper{0x3000,0x3100,{0x4000}};
        own_rows.push_back(helper);unwind(0x4000,{1,0,0,0});
        stack.insert(stack.begin(),2,bs::Frame{asi+0x3001,asi,helper});stack[7].pc=loader+0x1eead;
        if(observed)stack[7].function=*observed;
        while(stack.size()<captured)stack.push_back(stack.back());stack.resize(captured);
    }
    void build_stack() {
        stack={frame(asi,0x1001,0x1000,0x1100,0x2000),frame(loader,0x21681,0x20ea0,0x217cb,0x9f794)};
        if(directory){stack.push_back(frame(loader,0x21b64,0x217d0,0x21c8b,0x9f848));stack.push_back(frame(loader,0x30b8c,0x30780,0x30f3b,0x9f938));}
        else stack.push_back(frame(loader,0x30ad8,0x30780,0x30f3b,0x9f938));
        stack.push_back(frame(loader,0x330a3,0x30f40,0x330e6,0x9fa64));
        stack.push_back(frame(loader,post?0x1fa6d:0x1faed,post?0x1fa40:0x1fac0,post?0x1fa7c:0x1fafc,0x9c120));
        const DWORD inner=static_cast<DWORD>(post?rva::NativeBootstrapShow:rva::NativeBootstrapCookie);
        const DWORD outer=static_cast<DWORD>(post?rva::NativeBootstrapCrt:rva::NativeAudioExeEntry);
        stack.push_back(frame(base,static_cast<DWORD>(post?rva::NativeBootstrapShow+0x22:rva::NativeBootstrapCookieTime+6),inner,inner+(post?0x3a:0xaf),
            post?(older_bootstrap?0x6d1fd10:0x6b8fe80):(older_bootstrap?0x6d799c4:0x6be9c8c)));
        stack.push_back(frame(base,static_cast<DWORD>(post?rva::NativeBootstrapShowToWinmain+5:rva::NativeAudioExeEntry+9),outer,outer+(post?0x171:0x12),
            post?(older_bootstrap?0x6d7996c:0x6be9c34):(older_bootstrap?0x6d12688:0x6b827b8)));
        stack.push_back(frame(system,0x1001,0x1000,0x2000,0x3000));
    }
    template<class T>void disk_value(na::Address image,DWORD rva,const T& value) {
        std::vector<unsigned char> data(sizeof(value));std::memcpy(data.data(),&value,sizeof(value));unwind(rva,std::move(data),image);
    }
    void text(na::Address image,DWORD rva,const char* value) {
        std::vector<unsigned char> data(strlen(value)+1);std::memcpy(data.data(),value,data.size());unwind(rva,std::move(data),image);
    }
    void first_srw() {
        post=false;put(base+rva::NativeAudioCrtState,1u);
        memory[base+rva::NativeAudioInitialGuHeader].assign(48,0);put(base+rva::NativeBootstrapGuSerial,0u);
        put(base+rva::NativeBootstrapCrashComplete,static_cast<unsigned char>(0));
        for(auto r:{rva::NativeBootstrapCrashWorker,rva::NativeBootstrapCrashRequest,rva::NativeBootstrapCrashEvent})put(base+r,std::uint64_t{0});
        memory[base+rva::NativeBootstrapSrwGuard].resize(4);put(base+rva::NativeBootstrapSrwGuard,0u);
        memory[base+rva::NativeBootstrapSrwPoolComplete]={0};
        for(auto r:{rva::NativeBootstrapSrwGmalloc,rva::NativeBootstrapSrwReserve,rva::NativeBootstrapSrwLock,rva::NativeBootstrapSrwOutput}) {
            memory[base+r].resize(8);put(base+r,std::uint64_t{0});}
        for(auto id:bs::srw_windows){const auto* spec=find_rva_signature(id);assert(spec);memory[base+spec->rva]=spec->expected_prologue;}
        disk_value(base,static_cast<DWORD>(rva::NativeBootstrapSrwInitBegin),std::array<na::Address,4>{0,base+rva::NativeBootstrapSrwCrtSetup,
            base+rva::NativeBootstrapSrwAtexit,base+rva::NativeBootstrapSrwFirstName});
        crt_nt=pe(crt,0x2bbefd73,bs::kCrtImageSize,bs::kCrtChecksum,0x1000);
        exe_nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]={0x06c8f000,40};put(base+128,exe_nt);
        IMAGE_IMPORT_DESCRIPTOR import{};import.OriginalFirstThunk=0x06c8e000;import.FirstThunk=static_cast<DWORD>(rva::NativeBootstrapSrwInitIat);import.Name=0x06c8f100;
        disk_value(base,0x06c8f000,import);disk_value(base,0x06c8f000+20,IMAGE_IMPORT_DESCRIPTOR{});
        text(base,0x06c8f100,"api-ms-win-crt-runtime-l1-1-0.dll");disk_value(base,0x06c8e000,std::uint64_t{rva::NativeBootstrapSrwInitName});
        std::array<char,12> name{};std::memcpy(name.data()+2,"_initterm",10);disk_value(base,static_cast<DWORD>(rva::NativeBootstrapSrwInitName),name);
        memory[base+rva::NativeBootstrapSrwInitIat].resize(8);put(base+rva::NativeBootstrapSrwInitIat,crt+0x707a0);
        crt_nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]={0x110000,0x200};
        crt_nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG]={0x110800,0x100};put(crt+128,crt_nt);
        IMAGE_EXPORT_DIRECTORY exports{};exports.NumberOfNames=exports.NumberOfFunctions=1;exports.AddressOfNames=0x110100;
        exports.AddressOfNameOrdinals=0x110110;exports.AddressOfFunctions=0x110120;
        disk_value(crt,0x110000,exports);disk_value(crt,0x110100,DWORD{0x110180});disk_value(crt,0x110110,WORD{0});
        disk_value(crt,0x110120,DWORD{0x707a0});text(crt,0x110180,"_initterm");
        disk_value(crt,0x110878,std::uint64_t{0x1800fdba8});put(crt+0x110878,crt+0xfdba8);
        disk_value(crt,0xfdba8,std::uint64_t{0x1800ed6c0});put(crt+0xfdba8,crt+0xed6c0);
        for(const auto& w:bs::crt_windows){std::vector<unsigned char> data(w.hex.size()/2);
            for(unsigned i=0;i<data.size();++i)data[i]=static_cast<unsigned char>(bs::nibble(w.hex[i*2])*16+bs::nibble(w.hex[i*2+1]));
            if(w.rva==0xf8010)data.resize(16,0xcc);
            unwind(static_cast<DWORD>(w.rva),std::move(data),crt);}
        const unsigned wrapper=directory?5:4;stack.resize(wrapper+1);stack[wrapper]=frame(loader,0x1eead,0x1ee80,0x1eebc,0x9c120);
        const std::array<bs::Frame,9> source=older_bootstrap?std::array<bs::Frame,9>{
            frame(base,0x020ae89e,0x020ae888,0x020ae903,0x06d126d4),
            frame(base,0x01921e89,0x01921e54,0x01921ea8,0x06c84cf4),frame(base,0x028987cf,0x028987ca,0x028987fa,0x06c8ed6c),
            frame(base,0x02454933,0x024548e4,0x02457270,0x06da93d4),frame(base,0x0289a736,0x0289a72a,0x0289a742,0x06c93878),
            frame(base,0x00882875,0x00882810,0x00882a3b,0x06c917ac),frame(base,0x008823e2,0x00882298,0x008824b8,0x06c9175c),
            frame(crt,0x707d6,0x707a0,0x707d8,0x11bc3c),frame(base,0x020af35c,0x020af2e0,0x020af451,0x06d7996c)}:
            std::array<bs::Frame,9>{frame(base,0x0216b4ee,0x0216b4d8,0x0216b553,0x06b82804),
            frame(base,0x019daa65,0x019daa30,0x019daa84,0x06aead94),frame(base,0x024bb505,0x024bb500,0x024bb530,0x06aed8e0),
            frame(base,0x02222f17,0x02222ec8,0x02225854,0x06b2355c),frame(base,0x024d20bc,0x024d20b0,0x024d20c8,0x06b124e4),
            frame(base,0x00b523c1,0x00b52360,0x00b5268b,0x06b10864),frame(base,0x00b5190e,0x00b517c4,0x00b519e4,0x06b107ec),
            frame(crt,0x707d6,0x707a0,0x707d8,0x11bc3c),frame(base,0x0216bfac,0x0216bf30,0x0216c0a1,0x06be9c34)};
        stack.insert(stack.end(),source.begin(),source.end());stack.push_back(frame(system,0x1001,0x1000,0x2000,0x3000));
        stack[wrapper+1].rbx=base+rva::NativeBootstrapSrwGuard;stack[wrapper+4].rdi=base+rva::NativeBootstrapSrwPool;
        stack[wrapper+7].rbx=base+rva::NativeBootstrapSrwOutput;stack[wrapper+8].rbx=base+rva::NativeBootstrapSrwInitBegin+24;
        stack[wrapper+8].rdi=base+rva::NativeBootstrapSrwInitEnd;
        srw_rows=stack;
        srw_rows.push_back(older_bootstrap?frame(base,0x83f4bc,0x83f4bc,0x83f5bb,0x6c84f78):frame(base,0x79fa64,0x79fa64,0x79fb63,0x6aead5c));
        srw_rows.push_back(older_bootstrap?frame(base,0x8bb424,0x8bb424,0x8bb44a,0x6c84cf4):frame(base,0xb7ba48,0xb7ba48,0xb7ba6e,0x6aead94));
        srw_rows.push_back(frame(crt,0xed6a0,0xed6a0,0xed6a6,0x1243b0));srw_rows.push_back(frame(crt,0xed6c0,0xed6c0,0xed6c2,0x124a18));
        for(const auto& f:srw_rows)if(f.image!=system)unwind(f.function.UnwindData,{1,0,0,0},f.image);
        for(const auto& [child,parent]:std::array<std::pair<unsigned,unsigned>,2>{{{wrapper+3,static_cast<unsigned>(srw_rows.size()-4)},
            {wrapper+5,static_cast<unsigned>(srw_rows.size()-3)}}}) {
            std::vector<unsigned char> data(16);data[0]=0x21;std::memcpy(data.data()+4,&srw_rows[parent].function,12);
            unwind(stack[child].function.UnwindData,std::move(data),base);}
        unwind(0x11bc3c,{1,15,4,0,15,0x34,6,0,15,0x32,11,0x70},crt);
        if constexpr(older_bootstrap)older_unwind();
        reconstructed=stack;
        bs::test.crt_path=[](const bs::File&){return !boot->bad_crt_path;};
        bs::test.context=[](CONTEXT& c,bs::Address& low,bs::Address& high){low=0x60000000;high=low+4096;
            c.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
            c.Rip=boot->reconstructed.front().pc;c.Rsp=low;c.Rbx=boot->reconstructed.front().rbx;c.Rdi=boot->reconstructed.front().rdi;return true;};
        bs::test.unwind=[](CONTEXT& c,bs::Address image,RUNTIME_FUNCTION& function){++boot->unwind_steps;if(boot->unwind_unavailable)return false;
            const auto index=(c.Rsp-0x60000000)/16;assert(index<boot->reconstructed.size());
            const auto& f=boot->reconstructed[index];assert(image==f.image && !std::memcmp(&function,&f.function,12));
            if(index+1==boot->reconstructed.size())return false;const auto& next=boot->reconstructed[index+1];
            c.Rsp+=16;c.Rip=next.pc;c.Rbx=next.rbx;c.Rdi=next.rdi;return true;};
    }
    bool open(HMODULE module,bs::File& file) {
        const auto* actual=module==reinterpret_cast<HMODULE>(ntbase)?actual_nt:module==reinterpret_cast<HMODULE>(crt)?actual_crt:nullptr;
        if(actual){file.path=actual->path;file.nt=actual->nt;file.nt_offset=actual->nt_offset;file.sections=actual->sections;
            file.identity=actual->identity;file.sha=actual->sha;if(actual==actual_nt && bad_nt_hash)file.sha[0]^=1;return true;}
        if(module==reinterpret_cast<HMODULE>(crt)) {
            file.path=L"C:\\Windows\\System32\\ucrtbase.dll";file.nt=crt_nt;file.nt_offset=128;file.sections[0]=crt_section;
            file.identity.nFileSizeLow=1377512;
            for(unsigned i=0;i<32;++i)file.sha[i]=static_cast<unsigned char>(bs::nibble(bs::kCrtSha[i*2])*16+bs::nibble(bs::kCrtSha[i*2+1]));
            if(bad_crt_hash)file.sha[0]^=1;return true;
        }
        const bool exe=module==reinterpret_cast<HMODULE>(base),self=module==reinterpret_cast<HMODULE>(asi);
        file.path=exe?L"C:\\Game\\ff7rebirth_.exe":self?L"C:\\Game\\FF7RPianoSongs.asi":(wrong_path?L"C:\\Other\\":L"C:\\Game\\")+proxy_name;
        if(self && actual_self){file.nt=actual_self->nt;file.nt_offset=actual_self->nt_offset;file.identity=actual_self->identity;
            file.sections=actual_self->sections;file.sha=actual_self->sha;return true;}
        file.nt=exe?exe_nt:self?asi_nt:loader_nt;file.nt_offset=128;file.identity.nFileSizeLow=static_cast<DWORD>(exe?core::generated::kExeFileSize:self?8192:3615648);
        file.sections[0]=exe?exe_section:self?asi_section:loader_section;
        if(wrong_size)++file.identity.nFileSizeLow;const std::string_view hex=exe?core::generated::kExeSha256:bs::kLoaderSha;
        for(unsigned i=0;i<32;++i)file.sha[i]=static_cast<unsigned char>(bs::nibble(hex[i*2])*16+bs::nibble(hex[i*2+1]));
        if(wrong_hash || (!exe && !self && wrong_proxy_hash))file.sha[0]^=1;return true;
    }
    static void initialize() noexcept {
        ++boot->callbacks;assert(bs::scope && bs::scope->owner_thread==GetCurrentThreadId());
        if(boot->before_install)boot->before_install(*boot);
        if(boot->recurse)assert(!na::try_synchronous_native_audio_startup(reinterpret_cast<HMODULE>(base),reinterpret_cast<HMODULE>(asi),loader+0x21681,initialize));
        if(!boot->noop)boot->install_result=na::install_native_audio_runtime(reinterpret_cast<HMODULE>(base),{identity,score});
    }
    bool run(){return na::try_synchronous_native_audio_startup(reinterpret_cast<HMODULE>(base),reinterpret_cast<HMODULE>(asi),loader+0x21681,initialize);}
    void rejected(){const bool result=run();if(result || callbacks || installed)std::cerr<<"unexpected bootstrap callback post="<<post<<" failure="<<static_cast<int>(na::native_audio_bootstrap_status().failure)<<"\n";
        assert(!result && !callbacks && !installed && !na::g_origin.load() && !mounts && !name_calls && !bs::scope);}
    void resume_stock(){for(const auto& [address,bytes]:saved)memory[address]=bytes;}
};
}
// Map only data into RW memory and register its real pdata; never LoadLibrary,
// execute a mapped byte, or run an ASI/UAL entrypoint.
struct MetadataImage {
    bs::File file;
    unsigned char* image=nullptr;
    PRUNTIME_FUNCTION table=nullptr;
    DWORD count=0;
    explicit MetadataImage(const std::filesystem::path& path) {
        file.handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        assert(file.handle!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(file.handle,&file.identity));
        file.path=path.wstring();IMAGE_DOS_HEADER dos{};
        assert(bs::file_read(file.handle,0,&dos,sizeof(dos)) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0);
        file.nt_offset=static_cast<DWORD>(dos.e_lfanew);
        assert(bs::file_read(file.handle,file.nt_offset,&file.nt,sizeof(file.nt)));
        assert(file.nt.Signature==IMAGE_NT_SIGNATURE && file.nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64);
        assert(file.nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && file.nt.FileHeader.NumberOfSections<=96);
        assert(file.nt.FileHeader.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64));
        const auto extent=(static_cast<std::uint64_t>(file.identity.nFileSizeHigh)<<32)|file.identity.nFileSizeLow;
        assert(extent && extent<=128ull*1024*1024 && bs::digest(file.handle,extent,file.sha));
        assert(bs::file_read(file.handle,file.nt_offset+24+sizeof(IMAGE_OPTIONAL_HEADER64),file.sections.data(),
            file.nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)));
        const auto size=file.nt.OptionalHeader.SizeOfImage;assert(size && size<=128u*1024*1024);
        image=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(image);
        assert(file.nt.OptionalHeader.SizeOfHeaders<=65536 && file.nt.OptionalHeader.SizeOfHeaders<=size);
        assert(bs::file_read(file.handle,0,image,file.nt.OptionalHeader.SizeOfHeaders));
        const auto& exception=file.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        assert(exception.VirtualAddress<size && exception.Size && exception.Size<=size-exception.VirtualAddress && exception.Size%12==0);
        // Only pdata/xdata are read by the Windows lookup and our bounded reader.
        for(unsigned s=0;s<file.nt.FileHeader.NumberOfSections;++s){const auto& section=file.sections[s];
            assert(section.VirtualAddress<size && section.SizeOfRawData<=size-section.VirtualAddress);
            assert(section.PointerToRawData<=extent && section.SizeOfRawData<=extent-section.PointerToRawData);
            for(DWORD at=0;at<section.SizeOfRawData;){const auto n=(std::min)(DWORD{65536},section.SizeOfRawData-at);
                assert(bs::file_read(file.handle,section.PointerToRawData+at,image+section.VirtualAddress+at,n));at+=n;}}
        table=reinterpret_cast<PRUNTIME_FUNCTION>(image+exception.VirtualAddress);count=exception.Size/12;
        assert(RtlAddFunctionTable(table,count,reinterpret_cast<DWORD64>(image)));
    }
    ~MetadataImage(){if(table)assert(RtlDeleteFunctionTable(table));if(image)VirtualFree(image,0,MEM_RELEASE);}
    bs::Frame lookup(DWORD rva) const {
        bs::Frame result{};assert(bs::lookup(reinterpret_cast<na::Address>(image)+rva,result));
        assert(result.image==reinterpret_cast<na::Address>(image));return result;
    }
    DWORD export_rva(const char* name) const {
        const auto& dir=file.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];IMAGE_EXPORT_DIRECTORY exports{};
        assert(bs::disk_rva(file,dir.VirtualAddress,&exports,sizeof(exports)) && exports.NumberOfNames<=65536);
        for(DWORD i=0;i<exports.NumberOfNames;++i){DWORD text=0;assert(bs::disk_rva(file,exports.AddressOfNames+4*i,&text,4));
            std::array<char,14> bytes{};assert(bs::disk_rva(file,text,bytes.data(),static_cast<DWORD>(bytes.size())));
            if(!std::memcmp(bytes.data(),name,bytes.size())){WORD ordinal=0;DWORD address=0;
                assert(bs::disk_rva(file,exports.AddressOfNameOrdinals+2*i,&ordinal,2) && ordinal<exports.NumberOfFunctions);
                assert(bs::disk_rva(file,exports.AddressOfFunctions+4*ordinal,&address,4));
                assert(address>=dir.VirtualAddress+dir.Size || address<dir.VirtualAddress);return address;}}
        assert(false);return 0;
    }
};
struct ScpDiskFixture {
    bs::File crt,nt;std::vector<unsigned char> crt_image,nt_image;
    static void image(const bs::File& file,std::vector<unsigned char>& out) {
        out.resize(file.nt.OptionalHeader.SizeOfImage);
        assert(bs::file_read(file.handle,0,out.data(),file.nt.OptionalHeader.SizeOfHeaders));
        for(unsigned i=0;i<file.nt.FileHeader.NumberOfSections;++i){const auto& s=file.sections[i];
            assert(s.VirtualAddress<=out.size() && s.SizeOfRawData<=out.size()-s.VirtualAddress);
            for(DWORD offset=0;offset<s.SizeOfRawData;){const DWORD count=std::min<DWORD>(65536,s.SizeOfRawData-offset);
                assert(bs::file_read(file.handle,s.PointerToRawData+offset,out.data()+s.VirtualAddress+offset,count));offset+=count;}}
    }
    ScpDiskFixture(){assert(bs::open_file(GetModuleHandleW(L"ucrtbase.dll"),crt) && bs::hash_equal(crt.sha,bs::kCrtSha));
        assert(bs::open_file(GetModuleHandleW(L"ntdll.dll"),nt) && bs::hash_equal(nt.sha,bs::kNtdllSha));image(crt,crt_image);image(nt,nt_image);}
};
void BootstrapFixture::first_scp() {
    first_srw();const auto access=bs::test;bs::test={};static ScpDiskFixture disk;bs::test=access;
    actual_crt=&disk.crt;actual_nt=&disk.nt;
    for(const auto region:{crt,ntbase}){
        memory.erase(memory.lower_bound(region),memory.lower_bound(region+0x300000));
        disk_unwind.erase(disk_unwind.lower_bound(region),disk_unwind.lower_bound(region+0x300000));}
    memory[crt]=disk.crt_image;disk_unwind[crt]=disk.crt_image;
    memory[ntbase]=disk.nt_image;disk_unwind[ntbase]=disk.nt_image;
    put(crt+0xfd568,crt+0xfdba8);put(crt+0xfdba8,crt+0xed6c0);
    put(ntbase+0x1746f0,ntbase+0x16e000);put(ntbase+0x1746f8,ntbase+0x16e2b0);
    put(ntbase+0x1ea558,clone+0xc0);
    memory[clone]=std::vector<unsigned char>(disk.nt_image.begin()+0x16e000,disk.nt_image.begin()+0x16e2b0);
    const auto displacement=static_cast<std::int32_t>((clone+0xc0)-(crt+0xf8015));put(crt+0xf8011,displacement);
    mapping.BaseAddress=reinterpret_cast<void*>(clone);mapping.AllocationBase=reinterpret_cast<void*>(ntbase);
    mapping.State=MEM_COMMIT;mapping.Type=MEM_IMAGE;mapping.Protect=PAGE_EXECUTE_READ;mapping.RegionSize=4096;
    bs::test.ntdll=[]{return reinterpret_cast<HMODULE>(ntbase);};bs::test.ntdll_path=[](const bs::File&){return boot->nt_path;};
    bs::test.mapped_file=[](bs::Address,const bs::File&){return boot->mapped_identity;};
    bs::test.query=[](bs::Address where,MEMORY_BASIC_INFORMATION& out){if(where!=reinterpret_cast<bs::Address>(boot->mapping.BaseAddress))return false;out=boot->mapping;return true;};
}
void actual_metadata_tests(const char* asi_path,const char* loader_path,const char* expected_sha) {
    bs::test={};bs::frame_failure_claimed.clear();bs::frame_failure_ready.store(false);
    MetadataImage asi{std::filesystem::path(asi_path)};
    assert(bs::hash_equal(asi.file.sha,expected_sha));
    const auto export_offset=asi.export_rva("InitializeASI");
    const auto entry=asi.lookup(export_offset);RUNTIME_FUNCTION primary{};na::BootstrapFrameFailure facts{};
    assert(bs::primary_function(entry,asi.file,primary,facts));unsigned secondary=0;
    for(DWORD i=0;i<asi.count;++i){const auto row=asi.table[i];std::array<unsigned char,4> header{};
        assert(bs::disk_rva(asi.file,row.UnwindData,header.data(),4));if(!(header[0]>>3&UNW_FLAG_CHAININFO))continue;
        const DWORD tail=(4+2*header[2]+3)&~3u;RUNTIME_FUNCTION parent{};
        assert(bs::disk_rva(asi.file,row.UnwindData+tail,&parent,sizeof(parent)));
        if(std::memcmp(&parent,&primary,sizeof(parent)))continue;
        const auto observed=asi.lookup(row.BeginAddress);assert(!std::memcmp(&observed.function,&row,sizeof(row)));
        RUNTIME_FUNCTION resolved{};assert(bs::primary_function(observed,asi.file,resolved,facts));
        assert(!std::memcmp(&resolved,&primary,sizeof(primary)) && std::memcmp(&observed.function,&entry.function,sizeof(primary)));
        std::cout<<"InitializeASI secondary begin=0x"<<std::hex<<row.BeginAddress<<" end=0x"<<row.EndAddress
            <<" unwind=0x"<<row.UnwindData<<" primary=0x"<<primary.BeginAddress<<std::dec<<"\n";++secondary;
        // Exact disk/Windows-produced records also traverse the complete source
        // and cardinality gate, not just the isolated chain resolver.
        for(bool post:{false,true}){BootstrapFixture f(post);f.actual_self=&asi.file;
            f.export_offset=export_offset;f.export_entry=entry.function;
            f.own_rows.assign(asi.table,asi.table+asi.count);
            for(auto it=f.memory.begin();it!=f.memory.end();)if(it->first>=f.asi && it->first<f.asi+asi.file.nt.OptionalHeader.SizeOfImage)
                it=f.memory.erase(it);else ++it;
            f.memory[f.asi]={asi.image,asi.image+asi.file.nt.OptionalHeader.SizeOfImage};
            for(auto it=f.disk_unwind.begin();it!=f.disk_unwind.end();)if(it->first>=f.asi && it->first<f.asi+asi.file.nt.OptionalHeader.SizeOfImage)
                it=f.disk_unwind.erase(it);else ++it;
            f.disk_unwind[f.asi]=f.memory[f.asi];f.stack[0]={f.asi+row.BeginAddress,f.asi,observed.function};
            const auto invoked=f.run();if(!invoked || !f.install_result){const auto status=na::native_audio_bootstrap_status();
                std::cerr<<"actual export source failure="<<unsigned(status.failure)<<" image="<<status.image.field
                    <<" frame="<<status.frame.predicate<<" index="<<status.failed_frame
                    <<" expected_unwind=0x"<<std::hex<<status.frame.expected.unwind<<std::dec<<"\n";}
            assert(invoked && f.install_result && f.callbacks==1 && f.installed==11 && na::g_origin.load());
            assert(!f.mounts && !f.acquires && !f.bridges && !f.name_calls);
        }
    }
    assert(secondary); // The exact failed candidate has split, chained export regions.
    MetadataImage loader{std::filesystem::path(loader_path)};assert(bs::hash_equal(loader.file.sha,"22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c"));
    const auto frame=loader.lookup(0x21681);facts.expected_module=frame.image;facts.observed_module=frame.image;
    assert(bs::function(frame,loader.file,0x20ea0,0x217cb,0x9f794,facts));
    assert(!bs::frame_failure_ready.load());
    const auto unsupported_wrapper=loader.lookup(0x1eead);
    {BootstrapFixture f(false,true);f.rejected_wrapper(19,&unsupported_wrapper.function);
        const auto first=f.stack[8],second=f.stack[9];f.rejected();const auto state=na::native_audio_bootstrap_status();
        assert(state.frame.predicate==std::string_view("srw_import_read"));
        assert(state.frame.following_count==2 && !state.callback_invoked && !state.barriers_published);
        (void)first;(void)second;}
    std::cout<<"authentic UAL FindFiles raw Windows lookup/unwind passed (no module initialization)\n";
}
void actual_crt_unwind_test() {
    bs::test={};bs::frame_failure_claimed.clear();bs::frame_failure_ready.store(false);
    std::array<wchar_t,32768> path{};const auto n=GetSystemDirectoryW(path.data(),static_cast<UINT>(path.size()));
    assert(n && n+32<path.size() && !wcscat_s(path.data(),path.size(),L"\\ucrtbase.dll"));
    // Read-only data mapping plus registered metadata; never LoadLibrary or execute
    // the CRT/game/UAL image. Exercise the OS unwinder on real authenticated pdata.
    MetadataImage crt{std::filesystem::path(path.data())};
    if(!bs::hash_equal(crt.file.sha,bs::kCrtSha)){std::cout<<"exact SRW CRT metadata fixture unavailable on this OS\n";return;}
    const auto loaded=GetModuleHandleW(L"ucrtbase.dll");bs::File loaded_file{};
    assert(loaded && bs::open_file(loaded,loaded_file) && loaded_file.sha==crt.file.sha);
    DWORD first_window=0;unsigned first_offset=0,first_expected=0,first_observed=0;
    for(const auto& window:bs::crt_windows) {
        std::array<unsigned char,128> disk{},live{};const auto size=window.hex.size()/2;
        assert(bs::disk_rva(loaded_file,static_cast<DWORD>(window.rva),disk.data(),static_cast<DWORD>(size))
            && bs::copy(reinterpret_cast<bs::Address>(loaded)+window.rva,live.data(),size));
        for(unsigned offset=0;offset<size;++offset)if(live[offset]!=disk[offset]) {
            if(!first_window){first_window=static_cast<DWORD>(window.rva);first_offset=offset;first_expected=disk[offset];first_observed=live[offset];}
            std::cout<<"own loaded CRT window=0x"<<std::hex<<window.rva<<" offset=0x"<<offset
                <<" disk=0x"<<unsigned(disk[offset])<<" live=0x"<<unsigned(live[offset])<<" bytes=";
            for(unsigned j=0;j<size;++j)std::cout<<std::hex<<unsigned(live[j])<<',';
            std::cout<<std::dec<<'\n';
            if(size==5 && live[0]==0xe9) {
                std::int32_t displacement=0;std::memcpy(&displacement,live.data()+1,4);
                const auto target=reinterpret_cast<bs::Address>(loaded)+window.rva+5+displacement;
                const auto owner=bs::module_at(target);std::array<wchar_t,32768> name{};MEMORY_BASIC_INFORMATION memory{};
                if(owner)GetModuleFileNameW(owner,name.data(),static_cast<DWORD>(name.size()));
                assert(VirtualQuery(reinterpret_cast<void*>(target),&memory,sizeof(memory))==sizeof(memory));
                std::cout<<"own CRT thunk target=0x"<<std::hex<<target<<" module=0x"<<reinterpret_cast<bs::Address>(owner)
                    <<" type=0x"<<memory.Type<<" protection=0x"<<memory.Protect<<std::dec<<'\n';
                std::wcout<<L"own CRT thunk owner="<<name.data()<<L'\n';
                name.fill(0);const auto mapped=K32GetMappedFileNameW(GetCurrentProcess(),reinterpret_cast<void*>(target),name.data(),static_cast<DWORD>(name.size()));
                std::wcout<<L"own CRT thunk mapped file="<<(mapped?name.data():L"unavailable")<<L'\n';
            }
            break;
        }
    }
    bs::image_failure_claimed.clear();bs::image_failure_ready.store(false);
    bs::frame_failure_claimed.clear();bs::frame_failure_ready.store(false);
    const bool accepted=bs::crt_code_matches(reinterpret_cast<bs::Address>(loaded),loaded_file,{});
    assert(accepted==!first_window);const auto observed=na::native_audio_bootstrap_status();
    assert(!observed.callback_invoked && !observed.barriers_published);
    if(first_window)assert(observed.image.module==na::BootstrapImageModule::Crt
        && observed.image.stage==na::BootstrapImageStage::CodeWindow && observed.image.field==std::string_view("live_byte")
        && observed.image.offset==first_window && observed.image.index==first_offset
        && observed.frame.expected_byte==first_expected && observed.frame.observed_byte==first_observed);
    bs::image_failure_claimed.clear();bs::image_failure_ready.store(false);
    bs::frame_failure_claimed.clear();bs::frame_failure_ready.store(false);
    bs::Scope dispatch_scope;
    const bool variant=bs::crt_code_matches(reinterpret_cast<bs::Address>(loaded),loaded_file,{},&dispatch_scope);
    if(!variant){const auto state=na::native_audio_bootstrap_status();std::cerr<<"loaded dispatch rejection="<<(state.frame.predicate?state.frame.predicate:"")
        <<" image="<<(state.image.field?state.image.field:"")<<" expected="<<state.image.expected<<" observed="<<state.image.observed<<'\n';}
    assert(variant && !na::native_audio_bootstrap_status().callback_invoked && !na::native_audio_bootstrap_status().barriers_published);
    std::cout<<"authenticated own loaded SCP-NP/original CRT dispatch passed (read-only)\n";
    const auto frame=crt.lookup(0x707d6);na::BootstrapFrameFailure facts{};
    assert(bs::function(frame,crt.file,0x707a0,0x707d8,0x11bc3c,facts));
    alignas(16) std::array<DWORD64,16> stack{};
    constexpr DWORD64 saved_rbx=0x123456789abcdef0,saved_rdi=0xfedcba9876543210;
    stack[6]=saved_rbx;stack[4]=saved_rdi;stack[5]=reinterpret_cast<DWORD64>(&actual_crt_unwind_test);
    CONTEXT context{};context.ContextFlags=CONTEXT_FULL;context.Rip=frame.pc;context.Rsp=reinterpret_cast<DWORD64>(stack.data());
    context.Rbx=1;context.Rdi=2;auto row=frame.function;
    assert(bs::unwind_step(context,frame.image,row) && context.Rbx==saved_rbx && context.Rdi==saved_rdi
        && context.Rip==stack[5] && context.Rsp==reinterpret_cast<DWORD64>(stack.data())+0x30);
    std::cout<<"exact CRT callback Windows virtual unwind/nonvolatile restoration passed (metadata only)\n";
}
int main(int argc,char** argv) {
    actual_crt_unwind_test();
    for(const auto* name:{L"xinput1_3.dll",L"dinput8.dll",L"dxgi.dll",L"version.dll"}) {
        for(unsigned source=0;source<3;++source) {
            BootstrapFixture f(source==1);f.proxy_name=name;if(source==2)f.first_scp();
            assert(f.run() && f.install_result && f.callbacks==1 && !f.mounts && !f.acquires && !f.bridges);
        }
        {BootstrapFixture f;f.proxy_name=name;f.wrong_proxy_hash=true;f.rejected();}
        {BootstrapFixture f;f.proxy_name=name;f.wrong_path=true;f.rejected();}
    }
    for(bool directory:{false,true}){BootstrapFixture f(false,directory);f.first_scp();
        const bool accepted=f.run();if(!accepted){const auto s=na::native_audio_bootstrap_status();std::cerr<<"SCP source rejection="<<(s.frame.predicate?s.frame.predicate:"")<<'\n';}
        assert(accepted && f.install_result && f.callbacks==1 && f.installed==11 && na::g_origin.load());
        assert(!f.mounts && !f.acquires && !f.bridges && !f.name_calls && na::native_audio_bootstrap_status().source==na::BootstrapSource::FirstSrw);}
    {BootstrapFixture f;f.first_scp();constexpr auto other=f.clone+0x20000;
        f.memory[other]=std::move(f.memory[f.clone]);f.memory.erase(f.clone);f.mapping.BaseAddress=reinterpret_cast<void*>(other);
        f.put(f.ntbase+bs::kSystemPointer,other+0xc0);f.put(f.crt+0xf8011,static_cast<std::int32_t>((other+0xc0)-(f.crt+0xf8015)));
        assert(f.run() && f.install_result && !f.mounts && !f.acquires && !f.name_calls);}
    for(unsigned offset=0;offset<56;++offset){BootstrapFixture f;f.first_scp();f.memory[f.crt][bs::kDvrtTable+offset]^=1;f.rejected();}
    for(unsigned offset=0;offset<56;++offset){BootstrapFixture f;f.first_scp();f.memory[f.crt][bs::kDvrtTable+offset]^=1;
        f.disk_unwind[f.crt][bs::kDvrtTable+offset]^=1;f.rejected();}
    {BootstrapFixture f;f.first_scp();auto& data=f.disk_unwind[f.ntbase];
        const auto block=f.actual_nt->nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
        const DWORD page=bs::kScpSource;const WORD entry=0xa000;std::memcpy(data.data()+block,&page,4);std::memcpy(data.data()+block+8,&entry,2);f.rejected();}
    for(DWORD expected:{bs::kScpExports,bs::kSystemBlock}){BootstrapFixture f;f.first_scp();
        IMAGE_EXPORT_DIRECTORY exports{};const auto directory=f.actual_nt->nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        std::memcpy(&exports,f.memory[f.ntbase].data()+directory,sizeof(exports));bool changed=false;
        for(DWORD index=0;index<exports.NumberOfFunctions;++index){DWORD value=0;
            std::memcpy(&value,f.memory[f.ntbase].data()+exports.AddressOfFunctions+index*4,4);
            if(value==expected){f.put(f.ntbase+exports.AddressOfFunctions+index*4,value+1);changed=true;break;}}
        assert(changed);f.rejected();}
    for(bool root:{false,true}){BootstrapFixture f;f.first_scp();
        bs::test.mapped_file=[](bs::Address where,const bs::File&){return where!=(boot->source_mutation?boot->ntbase:boot->clone);};
        f.source_mutation=root;f.rejected();}
    {BootstrapFixture f;f.first_scp();bs::test.query=[](bs::Address,MEMORY_BASIC_INFORMATION&){return false;};f.rejected();}
    for(unsigned failure=0;failure<28;++failure){BootstrapFixture f;f.first_scp();
        switch(failure){
        case 0:f.bad_nt_hash=true;break;
        case 1:f.nt_path=false;break;
        case 2:f.mapped_identity=false;break;
        case 3:f.mapping.Type=MEM_PRIVATE;break;
        case 4:f.mapping.Protect=PAGE_EXECUTE_READWRITE;break;
        case 5:f.mapping.Protect=PAGE_EXECUTE_WRITECOPY;break;
        case 6:f.mapping.Protect=PAGE_EXECUTE_READ|PAGE_GUARD;break;
        case 7:f.mapping.State=MEM_RESERVE;break;
        case 8:f.mapping.RegionSize=687;break;
        case 9:f.mapping.BaseAddress=reinterpret_cast<void*>(f.clone+16);break;
        case 10:f.mapping.AllocationBase=reinterpret_cast<void*>(f.ntbase+65536);break;
        case 11:f.put(f.ntbase+bs::kSystemPointer,f.clone+0xc1);break;
        case 12:f.put(f.ntbase+bs::kSystemBlock,DWORD{0x129});break;
        case 13:f.put(f.ntbase+bs::kScpExports,f.ntbase+bs::kScpSource+8);break;
        case 14:f.put(f.ntbase+bs::kScpExportsEnd,f.ntbase+bs::kScpEnd-8);break;
        case 15:f.memory[f.ntbase][f.actual_nt->nt_offset+offsetof(IMAGE_NT_HEADERS64,FileHeader.TimeDateStamp)]^=1;break;
        case 16:f.memory[f.ntbase][bs::kScpUnwind]^=1;break;
        case 17:f.memory[f.clone][0xc0]^=1;break;
        case 18:f.memory[f.clone][687]^=1;break;
        case 19:f.memory[f.ntbase][bs::kScpSource+687]^=1;break;
        case 20:f.memory[f.ntbase][bs::kScpSource+687]^=1;f.disk_unwind[f.ntbase][bs::kScpSource+687]^=1;f.memory[f.clone][687]^=1;break;
        case 21:f.put(f.crt+0xfdba8,f.crt+0xed6c1);break;
        case 22:f.put(f.crt+0xf8011,std::int32_t{0});break;
        case 23:f.disk_unwind[f.ntbase][bs::kSystemPointer]=1;break;
        case 24:f.memory[f.crt][0xfd4f0+0xe0]^=1;break;
        case 25:f.memory[f.crt][0xfd4f0+0xe4]^=1;break;
        case 26:f.memory[f.crt][0xfd4f0+offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64,GuardFlags)]^=1;break;
        case 27:f.memory[f.ntbase][f.actual_nt->nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress]^=1;break;
        }f.rejected();assert(!f.mounts && !f.acquires && !f.bridges && !f.name_calls);
        const auto s=na::native_audio_bootstrap_status();const auto reads=f.memory_reads;auto copy=s;copy.frame.byte_offset=99;
        assert(na::native_audio_bootstrap_status().frame.byte_offset==s.frame.byte_offset && f.memory_reads==reads);}
    for(unsigned padding=5;padding<16;++padding){BootstrapFixture f;f.first_scp();f.memory[f.crt][0xf8010+padding]^=1;f.rejected();}
    for(auto r:{bs::kScpSource,bs::kScpSource+0xc0,bs::kScpEnd-1}){BootstrapFixture f;f.first_scp();
        f.memory[f.ntbase][r]^=1;f.disk_unwind[f.ntbase][r]^=1;f.rejected();}
    for(auto address:{BootstrapFixture::clone+687,BootstrapFixture::ntbase+bs::kScpSource+2,
        BootstrapFixture::ntbase+bs::kSystemPointer,BootstrapFixture::ntbase+bs::kScpExports,
        BootstrapFixture::crt+0xf8011,BootstrapFixture::crt+bs::kDvrtTable+25,BootstrapFixture::crt+0xfd4f0+0xe0}) {
        BootstrapFixture f;f.first_scp();f.scp_drift=address;f.rejected();assert(na::native_audio_bootstrap_status().frame.predicate==std::string_view("scp_reread"));}
    {BootstrapFixture f;f.first_scp();f.scp_mapping_drift=true;f.rejected();assert(na::native_audio_bootstrap_status().frame.predicate==std::string_view("scp_reread"));}
    {BootstrapFixture f;f.first_scp();f.stack.insert(f.stack.begin()+12,BootstrapFixture::frame(f.ntbase,bs::kScpEntry,bs::kScpEntry,bs::kScpFunctionEnd,bs::kScpUnwind));
        f.reconstructed=f.stack;f.rejected();} // A tail JMP supplies no extra callback frame.
    for(bool during:{false,true}){BootstrapFixture f;f.first_scp();
        if(during)f.during_install=[](BootstrapFixture& x,unsigned count){if(count==10)x.put(x.ntbase+bs::kSystemPointer,x.clone+0xc1);};
        else f.before_install=[](BootstrapFixture& x){x.put(x.ntbase+bs::kSystemPointer,x.clone+0xc1);};
        assert(f.run() && !f.install_result && !na::g_origin.load() && f.installed==(during?11u:0u) && f.removed==f.installed
            && !na::native_audio_bootstrap_status().barriers_published && !f.mounts && !f.acquires && !f.bridges && !f.name_calls);}
    for(bool directory:{false,true}){BootstrapFixture f(false,directory);f.first_srw();
        const bool result=f.run();const auto snapshot=na::native_audio_bootstrap_status();
        if(!result || !f.install_result)std::cerr<<"SRW failure="<<static_cast<int>(snapshot.failure)<<" predicate="<<snapshot.frame.predicate
            <<" frame="<<snapshot.failed_frame<<" expected="<<std::hex<<snapshot.expected_return_rva<<" observed="<<snapshot.observed_return_rva<<std::dec<<"\n";
        assert(result && f.install_result && f.installed==11 && f.callbacks==1 && na::g_origin.load());
        assert(snapshot.source==na::BootstrapSource::FirstSrw && snapshot.barriers_published && f.unwind_steps);
        assert(!f.mounts && !f.acquires && !f.bridges && !f.name_calls && !na::g_captured.load());
        f.resume_stock();f.start(true);f.RuntimeFixture::frame();
        assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready);}
    // Every source continuation is exact, including exactly one imported CRT
    // callback frame. The two independent captures must also agree.
    if constexpr(older_bootstrap) {
        // An old image must never inherit the newer source's frame geometry.
        for(unsigned row:{2u,5u,6u}){BootstrapFixture f;f.first_srw();
            constexpr DWORD newer_returns[]={0x024bb505,0x00b523c1,0x00b5190e};
            f.stack[5+row].pc=f.base+newer_returns[row==2?0:row==5?1:2];f.reconstructed=f.stack;f.rejected();}
        for(unsigned source=0;source<3;++source){BootstrapFixture f(source==1);if(source==2)f.first_srw();
            f.put(f.base+rva::NativeBootstrapIoPool,std::uint64_t{1});f.rejected();
            assert(na::native_audio_bootstrap_status().failure==na::BootstrapFailure::Baseline);}
        for(bool during:{false,true}){BootstrapFixture f;f.first_srw();
            if(during)f.during_install=[](BootstrapFixture& x,unsigned i){if(i==10)x.put(x.base+rva::NativeBootstrapIoPool,std::uint64_t{1});};
            else f.before_install=[](BootstrapFixture& x){x.put(x.base+rva::NativeBootstrapIoPool,std::uint64_t{1});};
            assert(na::try_synchronous_native_audio_startup(reinterpret_cast<HMODULE>(f.base),reinterpret_cast<HMODULE>(f.asi),
                f.loader+0x21681,&BootstrapFixture::initialize));
            assert(!f.install_result && !na::g_origin && f.installed==(during?11u:0u)
                && f.removed==f.installed && f.disabled==f.installed
                && !na::native_audio_bootstrap_status().barriers_published);}
    }
    for(unsigned row=0;row<9;++row){BootstrapFixture f;f.first_srw();f.stack[5+row].pc+=1;f.reconstructed=f.stack;f.rejected();}
    for(unsigned row=0;row<9;++row){BootstrapFixture f;f.first_srw();f.stack.erase(f.stack.begin()+5+row);f.reconstructed=f.stack;f.rejected();}
    {BootstrapFixture f;f.first_srw();f.stack.insert(f.stack.begin()+12,f.stack[12]);f.reconstructed=f.stack;f.rejected();}
    {BootstrapFixture f;f.first_srw();f.stack.insert(f.stack.begin()+12,BootstrapFixture::frame(f.system,0x1001,0x1000,0x2000,0x3000));f.reconstructed=f.stack;f.rejected();}
    {BootstrapFixture f;f.first_srw();f.stack[5].pc++;f.rejected();assert(na::native_audio_bootstrap_status().frame.predicate==std::string_view("srw_context_stack_mismatch"));}
    for(unsigned witness=0;witness<5;++witness){BootstrapFixture f;f.first_srw();
        const std::array<unsigned,5> rows={5,8,11,12,12};auto& row=f.reconstructed[rows[witness]];
        auto& value=(witness==1 || witness==4)?row.rdi:row.rbx;value+=8;f.rejected();
        const auto state=na::native_audio_bootstrap_status();const auto reads=f.memory_reads;
        assert(state.frame.expected_register && state.frame.observed_register==state.frame.expected_register+8);
        auto detached=state;detached.frame.expected_register=0;assert(na::native_audio_bootstrap_status().frame.expected_register && f.memory_reads==reads);}
    for(unsigned failure=0;failure<5;++failure){BootstrapFixture f;f.first_srw();
        if(failure==0)f.unwind_unavailable=true;
        if(failure==1)bs::test.context=[](CONTEXT&,bs::Address&,bs::Address&){return false;};
        if(failure==2)bs::test.context=[](CONTEXT& c,bs::Address& low,bs::Address& high){low=high=1;c.ContextFlags=CONTEXT_FULL;return true;};
        if(failure==3)bs::test.context=[](CONTEXT& c,bs::Address& low,bs::Address& high){low=1;high=16;c.Rsp=8;c.Rip=boot->stack[0].pc;return true;};
        if(failure==4)bs::test.unwind=[](CONTEXT&,bs::Address,RUNTIME_FUNCTION&){return true;};
        f.rejected();}
    for(unsigned failure=0;failure<17;++failure){BootstrapFixture f;f.first_srw();
        switch(failure){
        case 0:f.bad_crt_hash=true;break;
        case 1:f.bad_crt_path=true;break;
        case 2:f.put(f.base+rva::NativeBootstrapSrwInitIat,f.crt+0x707a1);break;
        case 3:f.put(f.crt+0xfdba8,f.crt+0xed6c1);break;
        case 4:f.put(f.crt+0x110878,f.crt+0xfdba0);break;
        case 5:f.memory[f.crt+0x707a0][0]^=1;break;
        case 6:f.memory[f.crt+0xf8010][0]^=1;break;
        case 7:f.memory[f.crt+0xed6a0][0]^=1;break;
        case 8:f.memory[f.crt+0xed6c0][0]^=1;break;
        case 9:f.memory[f.base+rva::NativeBootstrapSrwInitBegin][24]^=1;break;
        case 10:f.memory[f.base+0x06c8f100][0]^=1;break;
        case 11:f.memory[f.base+rva::NativeBootstrapSrwInitName][2]^=1;break;
        case 12:f.disk_value(f.crt,0x110120,DWORD{0x110180});break; // forwarded export
        case 13:f.disk_value(f.crt,0x110120,DWORD{0x707a1});break;
        case 14:f.disk_value(f.base,0x06c8e000,std::uint64_t{0x8000000000000001});break;
        case 15:f.memory[f.crt+0x11bc3c][0]^=1;break;
        case 16:f.memory[f.crt+0x1243b0][0]^=1;break;
        }f.rejected();}
    for(auto id:bs::srw_windows){BootstrapFixture f;f.first_srw();const auto* spec=find_rva_signature(id);f.memory[f.base+spec->rva][0]^=1;f.rejected();}
    for(const auto& window:bs::crt_windows)for(unsigned offset=0;offset<window.hex.size()/2;++offset)for(bool disk:{false,true}) {
        BootstrapFixture f;f.first_srw();auto& bytes=(disk?f.disk_unwind:f.memory)[f.crt+window.rva];
        const auto expected=bytes[offset];bytes[offset]^=1;f.rejected();
        const auto reads=f.memory_reads;const auto state=na::native_audio_bootstrap_status();
        const auto predicate=disk?"srw_crt_disk_code":window.rva==0xf8010 && offset>=1 && offset<=4?"scp_dvrt_config":"srw_crt_code";
        assert(state.frame.predicate==std::string_view(predicate)
            && state.frame.byte_offset==offset && state.frame.expected_byte==expected && state.frame.observed_byte==(expected^1u)
            && state.image.module==na::BootstrapImageModule::Crt && state.image.stage==na::BootstrapImageStage::CodeWindow
            && state.image.field==std::string_view(disk?"disk_byte":"live_byte") && state.image.offset==window.rva
            && state.image.index==offset && state.image.expected==expected && state.image.observed==(expected^1u));
        auto detached=state;detached.image.offset=0;detached.frame.observed_byte=0;
        assert(na::native_audio_bootstrap_status().image.offset==window.rva && f.memory_reads==reads);
        assert(!bs::image_reject(na::BootstrapImageModule::Executable,na::BootstrapImageStage::NtHeader,"later",0,1));
        assert(na::native_audio_bootstrap_status().image.offset==window.rva && f.memory_reads==reads);
    }
    for(const auto& window:bs::crt_windows)for(bool disk:{false,true}) {
        BootstrapFixture f;f.first_srw();(disk?f.disk_unwind:f.memory)[f.crt+window.rva].resize(window.hex.size()/2-1);f.rejected();
        const auto state=na::native_audio_bootstrap_status();
        assert(state.frame.predicate==std::string_view(disk?"srw_crt_disk_read":"srw_crt_live_read")
            && state.image.field==std::string_view(disk?"disk_read":"live_read") && state.image.offset==window.rva
            && state.image.expected==(window.rva==0xf8010?16:window.hex.size()/2) && !state.image.observed && !state.frame.expected_byte && !state.frame.observed_byte);
    }
    {BootstrapFixture f;f.first_srw();f.drift_crt_code=true;f.rejected();
        assert(na::native_audio_bootstrap_status().image.offset==bs::crt_windows[0].rva);}
    // A relocated operand alone still grants nothing without its complete proof.
    {BootstrapFixture f;f.first_srw();auto& code=f.memory[f.crt+bs::crt_windows[1].rva];code={0xe9,0xab,0xf0,0x79,0x02};code.resize(16,0xcc);f.rejected();
        const auto state=na::native_audio_bootstrap_status();assert(state.image.offset==0xf8010 && state.image.index==1
            && state.frame.expected_byte==0x8b && state.frame.observed_byte==0xab);}
    for(const auto& window:bs::crt_windows)for(bool during:{false,true}) {
        BootstrapFixture f;f.first_srw();f.source_mutation=static_cast<unsigned>(window.rva);
        if(during)f.during_install=[](BootstrapFixture& fixture,unsigned index){if(index==10)fixture.memory[fixture.crt+fixture.source_mutation][0]^=1;};
        else f.before_install=[](BootstrapFixture& fixture){fixture.memory[fixture.crt+fixture.source_mutation][0]^=1;};
        assert(f.run() && !f.install_result && !na::g_origin.load());
        const auto state=na::native_audio_bootstrap_status();assert(!state.barriers_published && state.image.offset==window.rva
            && state.image.field==std::string_view("live_byte") && f.installed==(during?11u:0u) && f.removed==f.installed);
    }
    for(auto r:{rva::NativeBootstrapSrwGuard,rva::NativeBootstrapSrwGmalloc,rva::NativeBootstrapSrwPoolComplete,rva::NativeBootstrapSrwReserve,
        rva::NativeBootstrapSrwLock,rva::NativeBootstrapSrwOutput,rva::NativeAudioInitialGuHeader,rva::NativeBootstrapGuSerial,
        rva::NativeBootstrapCrashWorker,rva::NativeBootstrapCrashComplete,rva::NativeBootstrapHinstance}) {
        BootstrapFixture f;f.first_srw();f.memory[f.base+r][0]^=1;f.rejected();}
    for(unsigned state:{0u,2u,3u}){BootstrapFixture f;f.first_srw();f.put(f.base+rva::NativeAudioCrtState,state);f.rejected();}
    for(unsigned mutation=0;mutation<3;++mutation){BootstrapFixture f;f.first_srw();
        f.source_mutation=mutation;f.before_install=[](BootstrapFixture& f){if(f.source_mutation==0)f.reconstructed[5].rbx+=8;
            if(f.source_mutation==1)f.put(f.crt+0xfdba8,f.crt+0xed6c1);
            if(f.source_mutation==2)f.put(f.base+rva::NativeBootstrapSrwGuard,1u);};
        assert(f.run() && !f.install_result && !f.installed && !na::g_origin.load());}
    for(unsigned mutation=0;mutation<4;++mutation){BootstrapFixture f;f.first_srw();
        f.source_mutation=mutation;f.during_install=[](BootstrapFixture& f,unsigned count){if(count!=10)return;
            if(f.source_mutation==0)f.reconstructed[5].rbx+=8;
            if(f.source_mutation==1)f.put(f.crt+0xfdba8,f.crt+0xed6c1);
            if(f.source_mutation==2)f.put(f.base+rva::NativeBootstrapSrwGuard,1u);
            if(f.source_mutation==3)f.memory[f.crt+0x707a0][0]^=1;};
        assert(f.run() && !f.install_result && f.installed==11 && f.removed==11 && !na::g_origin.load());
        assert(!na::native_audio_bootstrap_status().barriers_published && !f.mounts && !f.acquires && !f.bridges);}
    // Current rejected wrapper shape: own frames, directory route, deferred
    // handoff, then 1eead at index 7. EXE successors are fixture facts, NOT a
    // claim about the as-yet-unidentified live EXE source.
    for(unsigned captured:{9u,10u,19u}){BootstrapFixture f(false,true);
        f.rejected_wrapper(captured);
        const auto next1=f.stack[8];const auto next2=captured>9?f.stack[9]:bs::Frame{};
        f.rejected();const auto reads=f.memory_reads;auto snapshot=na::native_audio_bootstrap_status();
        assert(snapshot.failed_frame==7 && snapshot.observed_return_rva==0x1eead && snapshot.expected_return_rva==0x1eead);
        assert(snapshot.frame.following_count==(captured==9?1u:2u));
        const auto same=[](const na::BootstrapFollowingFrame& copy,const bs::Frame& original){
            assert(copy.module==na::BootstrapFrameModule::Executable && copy.return_rva==original.pc-original.image);
            assert(copy.function.begin==original.function.BeginAddress && copy.function.end==original.function.EndAddress
                && copy.function.unwind==original.function.UnwindData);};
        same(snapshot.frame.following[0],next1);
        if(captured>9)same(snapshot.frame.following[1],next2);
        else assert(snapshot.frame.following[1].module==na::BootstrapFrameModule::Unknown
            && !snapshot.frame.following[1].return_rva && !snapshot.frame.following[1].function.begin
            && !snapshot.frame.following[1].function.end && !snapshot.frame.following[1].function.unwind);
        f.stack[8]={};snapshot.frame.following[0]={};
        bs::frame_reject({},"must_not_overwrite");
        same(na::native_audio_bootstrap_status().frame.following[0],next1);
        assert(f.memory_reads==reads && !snapshot.callback_invoked && !snapshot.barriers_published);}
    {BootstrapFixture f;f.stack.back().image=0xdead0000;f.rejected();
        const auto snapshot=na::native_audio_bootstrap_status();assert(snapshot.frame.predicate==std::string_view("system_suffix"));
        assert(!snapshot.frame.following_count);
        for(const auto& next:snapshot.frame.following)assert(next.module==na::BootstrapFrameModule::Unknown && !next.return_rva
            && !next.function.begin && !next.function.end && !next.function.unwind);}
    {BootstrapFixture f;bs::Scope s;s.exe=reinterpret_cast<HMODULE>(f.base);s.loader=reinterpret_cast<HMODULE>(f.loader);
        s.asi=reinterpret_cast<HMODULE>(f.asi);std::array<bs::Frame,bs::kFrames> frames{};
        for(unsigned i=0;i<frames.size();++i){const na::Address image=i%5==0?f.base:i%5==1?f.loader:i%5==2?f.asi:i%5==3?0xdead0000:0;
            frames[i]={image+0x100+i,image,{100+i,200+i,{300+i}}};}
        const auto reads=f.memory_reads;
        bs::test.read=[](na::Address,void*,std::size_t){assert(false);return false;};
        bs::test.module=[](na::Address)->HMODULE{assert(false);return nullptr;};
        bs::test.lookup=[](na::Address,bs::Frame&){assert(false);return false;};
        for(unsigned count:{0u,1u,2u,3u,64u,static_cast<unsigned>(MAXDWORD)})for(unsigned index:{0u,1u,2u,62u,63u,64u,static_cast<unsigned>(MAXDWORD)}){
            na::BootstrapFrameFailure facts;facts.following_count=2;for(auto& next:facts.following)next.return_rva=0xffff;
            bs::project_following(facts,frames,count,index,s);const auto bound=(std::min)(count,64u);
            const auto expected=index<bound?(std::min)(2u,bound-index-1):0u;assert(facts.following_count==expected);
            for(unsigned j=0;j<2;++j){const auto& copy=facts.following[j];
                if(j>=expected){assert(copy.module==na::BootstrapFrameModule::Unknown && !copy.return_rva
                    && !copy.function.begin && !copy.function.end && !copy.function.unwind);continue;}
                const auto& original=frames[index+1+j];const auto module=original.image==f.base?na::BootstrapFrameModule::Executable
                    :original.image==f.loader?na::BootstrapFrameModule::Loader:original.image==f.asi?na::BootstrapFrameModule::OwnAsi:na::BootstrapFrameModule::Unknown;
                assert(copy.module==module && copy.return_rva==(original.image?original.pc-original.image:0));
                assert(copy.function.begin==original.function.BeginAddress && copy.function.end==original.function.EndAddress
                    && copy.function.unwind==original.function.UnwindData);}}
        assert(f.memory_reads==reads);}
    for(bool post:{false,true})for(bool directory:{false,true}){BootstrapFixture f(post,directory);f.chained_export();
        assert(std::memcmp(&f.export_entry,&f.stack.front().function,sizeof(RUNTIME_FUNCTION)));
        assert(f.run() && f.install_result && f.callbacks==1 && f.installed==11);
        assert(na::native_audio_bootstrap_status().frame.predicate==std::string_view("none"));}
    for(unsigned fault=0;fault<10;++fault){BootstrapFixture f;f.chained_export();
        switch(fault){
        case 0:f.unwind(0x2200,{1,0,0,0});break; // Same-module unrelated function is not the export.
        case 1:f.memory[f.asi+0x2200][0]^=1;break;
        case 2:f.disk_unwind[f.asi+0x2200][0]=0x39;f.memory[f.asi+0x2200][0]=0x39;break; // CHAIN + EH invalid.
        case 3:{auto bytes=f.disk_unwind[f.asi+0x2200];bytes[4]^=4;f.unwind(0x2200,bytes);break;}
        case 4:{auto bytes=f.disk_unwind[f.asi+0x2200];std::memcpy(bytes.data()+4,&f.own_rows[2],12);f.unwind(0x2200,bytes);break;}
        case 5:f.stack.insert(f.stack.begin(),f.stack.front());break;
        case 6:f.stack[0].function.UnwindData^=4;break;
        case 7:f.export_entry.UnwindData^=4;break; // Lookup cannot invent a disk record.
        case 8:f.own_rows.erase(f.own_rows.begin());break;
        case 9:f.disk_unwind.erase(f.asi+0x2200);break;
        }f.rejected();const auto count=f.memory_reads;const auto snapshot=na::native_audio_bootstrap_status();
        assert(snapshot.frame.predicate!=std::string_view("none") && !snapshot.callback_invoked && !snapshot.barriers_published);
        assert(f.memory_reads==count);
        if(fault==0){assert(snapshot.frame.predicate==std::string_view("export_cardinality") && snapshot.frame.export_count==0);
            assert(snapshot.observed_return_rva==snapshot.expected_return_rva && snapshot.observed_return_rva==0x21681);
            assert(snapshot.frame.export_entry.begin==0x1000 && snapshot.frame.last_own.begin==0x1044);}
    }
    for(bool after:{false,true}){BootstrapFixture f;f.chained_export();
        if(after)f.during_install=[](BootstrapFixture& f,unsigned index){if(index==10)f.memory[f.asi+0x2200][0]^=1;};
        else f.before_install=[](BootstrapFixture& f){f.memory[f.asi+0x2200][0]^=1;};
        assert(f.run() && f.callbacks==1 && !f.install_result && !na::g_origin.load());
        const auto state=na::native_audio_bootstrap_status();
        assert(!state.barriers_published && state.frame.predicate==std::string_view("unwind_byte_mismatch"));
        assert(f.installed==(after?11u:0u) && f.removed==f.installed && f.disabled==f.installed);}
    for(unsigned links:{2u,15u,16u}){BootstrapFixture f;f.chained_export();f.own_rows.resize(1);
        auto previous=f.export_entry;
        for(unsigned i=0;i<links;++i){RUNTIME_FUNCTION next{0x3000+0x10*i,0x3010+0x10*i,{0x4000+0x20*i}};
            std::vector<unsigned char> bytes(16);bytes[0]=0x21;std::memcpy(bytes.data()+4,&previous,sizeof(previous));
            f.unwind(next.UnwindData,std::move(bytes));f.own_rows.push_back(next);previous=next;}
        f.stack[0]={f.asi+previous.BeginAddress,f.asi,previous};
        if(links<16)assert(f.run() && f.install_result);
        else {f.rejected();assert(na::native_audio_bootstrap_status().frame.predicate==std::string_view("own_chain_bound"));}}
    // Exact authenticated UAL FindFiles v1/EH+UH header, ten unwind codes and handler RVA.
    const std::vector<unsigned char> find_unwind={0x19,0x2d,0x0a,0,0x1c,1,0x45,0,0x0d,0xf0,0x0b,0xe0,
        9,0xd0,7,0xc0,5,0x70,4,0x60,3,0x30,2,0x50,0x10,0xe4,4,0};
    {BootstrapFixture f;f.unwind(0x9f794,find_unwind,f.loader);assert(f.run() && f.install_result);}
    for(unsigned i=0;i<find_unwind.size();++i){BootstrapFixture f;f.unwind(0x9f794,find_unwind,f.loader);
        f.memory[f.loader+0x9f794][i]^=1;f.rejected();const auto state=na::native_audio_bootstrap_status();
        assert(state.failed_frame==1 && state.observed_return_rva==0x21681 && state.expected_return_rva==0x21681);
        assert(state.frame.predicate==std::string_view("unwind_byte_mismatch") && state.frame.byte_offset==i);
        assert(state.frame.expected.begin==0x20ea0 && state.frame.expected.end==0x217cb && state.frame.expected.unwind==0x9f794);
        assert(state.frame.observed.begin==0x20ea0 && state.frame.observed.end==0x217cb);
        assert(state.frame.expected_byte!=state.frame.observed_byte);}
    // The OS can leave the preferred base or publish its exact relocated base.
    for(bool post:{false,true})for(unsigned mode=0;mode<3;++mode){BootstrapFixture f(post);
        for(const auto module:{f.base,f.loader}){auto nt=module==f.base?f.exe_nt:f.loader_nt;
            if(mode==1)nt.OptionalHeader.ImageBase=module;
            if(mode==2){auto& disk=module==f.base?f.exe_nt:f.loader_nt;disk.OptionalHeader.ImageBase=module;
                disk.FileHeader.Characteristics|=IMAGE_FILE_RELOCS_STRIPPED;
                disk.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]={};nt=disk;}
            f.put(module+128,nt);}
        assert(f.run() && f.install_result && f.callbacks==1 && f.installed==11);
        assert(!na::native_audio_bootstrap_status().image.expected);
    }
    // Every other byte remains immutable, including directories and reserved bytes.
    constexpr auto image_base_offset=offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+offsetof(IMAGE_OPTIONAL_HEADER64,ImageBase);
    for(bool loader:{false,true})for(unsigned i=0;i<sizeof(IMAGE_NT_HEADERS64);++i){
        if(i>=image_base_offset && i<image_base_offset+sizeof(ULONGLONG))continue;
        BootstrapFixture f;const auto module=loader?f.loader:f.base;auto nt=loader?f.loader_nt:f.exe_nt;
        reinterpret_cast<unsigned char*>(&nt)[i]^=1;f.put(module+128,nt);f.rejected();
        const auto reads=f.memory_reads;const auto state=na::native_audio_bootstrap_status();
        assert(state.failure==na::BootstrapFailure::Image && !state.callback_invoked && !state.barriers_published);
        assert(state.image.module==(loader?na::BootstrapImageModule::Loader:na::BootstrapImageModule::Executable));
        assert(state.image.stage==na::BootstrapImageStage::NtHeader && std::string_view(state.image.field)!="none");
        assert(state.image.expected!=state.image.observed && f.memory_reads==reads);
    }
    for(bool loader:{false,true})for(unsigned i=0;i<sizeof(IMAGE_SECTION_HEADER);++i){BootstrapFixture f;
        const auto module=loader?f.loader:f.base;auto section=loader?f.loader_section:f.exe_section;
        reinterpret_cast<unsigned char*>(&section)[i]^=1;f.put(module+128+sizeof(IMAGE_NT_HEADERS64),section);f.rejected();
        const auto state=na::native_audio_bootstrap_status();assert(state.image.stage==na::BootstrapImageStage::Sections);
        assert(state.image.module==(loader?na::BootstrapImageModule::Loader:na::BootstrapImageModule::Executable));
        assert(state.image.index==0 && state.image.expected!=state.image.observed);
    }
    for(bool loader:{false,true})for(unsigned fault=0;fault<10;++fault){BootstrapFixture f;
        const auto module=loader?f.loader:f.base;auto& disk=loader?f.loader_nt:f.exe_nt;auto nt=disk;
        switch(fault){
        case 0:nt.OptionalHeader.ImageBase=module+65536;break;
        case 1:nt.OptionalHeader.ImageBase=disk.OptionalHeader.ImageBase+65536;break;
        case 2:nt.OptionalHeader.ImageBase=0;break;
        case 3:disk.FileHeader.Characteristics|=IMAGE_FILE_RELOCS_STRIPPED;nt=disk;break;
        case 4:disk.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]={};nt=disk;break;
        case 5:disk.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size=1;nt=disk;break;
        case 6:disk.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress=disk.OptionalHeader.SizeOfImage;nt=disk;break;
        case 7:disk.OptionalHeader.NumberOfRvaAndSizes=IMAGE_DIRECTORY_ENTRY_BASERELOC;nt=disk;break;
        case 8:{IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=256;f.put(module,dos);break;}
        case 9:disk.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size=UINT32_MAX;nt=disk;break;
        }f.put(module+128,nt);f.rejected();assert(na::native_audio_bootstrap_status().image.module!=na::BootstrapImageModule::None);
    }
    for(bool after:{false,true}){BootstrapFixture f;
        if(after)f.during_install=[](BootstrapFixture& f,unsigned index){if(index==10){auto nt=f.exe_nt;nt.OptionalHeader.CheckSum^=1;f.put(f.base+128,nt);}};
        else f.before_install=[](BootstrapFixture& f){auto nt=f.loader_nt;nt.FileHeader.TimeDateStamp^=1;f.put(f.loader+128,nt);};
        assert(f.run() && !f.install_result && !na::g_origin.load());const auto state=na::native_audio_bootstrap_status();
        assert(state.failure==na::BootstrapFailure::Image && !state.barriers_published && state.callback_invoked);
        assert(state.image.module==(after?na::BootstrapImageModule::Executable:na::BootstrapImageModule::Loader));
        assert(f.installed==(after?11u:0u) && f.disabled==f.installed && f.removed==f.installed);
    }
    for(bool post:{false,true})for(bool directory:{false,true}) {
        BootstrapFixture f(post,directory);assert(f.run() && f.callbacks==1 && f.install_result);
        assert(!bs::scope && f.installed==11 && na::g_origin.load() && !na::g_entry_seen.load());
        const auto state=na::native_audio_bootstrap_status();assert(state.barriers_published && state.callback_invoked);
        assert(state.source==(post?na::BootstrapSource::PreWinMain:na::BootstrapSource::Cookie));
        assert(!f.mounts && !f.acquires && !f.bridges && !f.name_calls && !na::g_captured.load());
        assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::AwaitingInitializer);
        f.resume_stock();f.immediate=true;na::factory_hook();f.RuntimeFixture::frame();f.RuntimeFixture::frame();
        assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready && f.mounts==1 && f.acquires==3 && f.bridges==3);
        assert(!f.run() && f.callbacks==1);assert(!na::bootstrap_detail::finish_publication(reinterpret_cast<HMODULE>(f.base)));
    }
    for(bool post:{false,true})for(unsigned fault=0;fault<27;++fault) {
        BootstrapFixture f(post);
        switch(fault) {
        case 0:f.wrong_hash=true;break;case 1:f.wrong_size=true;break;case 2:f.wrong_export=true;break;
        case 3:f.no_frames=true;break;case 4:f.disk_drift=true;break;case 5:f.file_changed=true;break;
        case 6:f.stack[1].pc+=1;break;case 7:f.stack.erase(f.stack.begin()+2);break;
        case 8:std::swap(f.stack[2],f.stack[3]);break;
        case 9:f.stack[3]=BootstrapFixture::frame(f.loader,0x27006,0x261e0,0x2747b,0xa3398);break;
        case 10:f.stack[4].pc+=1;break;case 11:f.stack[5].pc+=1;break;case 12:f.stack[6].pc+=1;break;
        case 13:f.stack.insert(f.stack.begin()+2,f.stack[2]);break;
        case 14:f.stack.insert(f.stack.begin(),f.stack.front());break;
        case 15:f.stack.resize(64,f.stack.back());break;
        case 16:f.memory[f.loader+0x3308c][0]^=1;break;
        case 17:f.memory[f.base+rva::NativeBootstrapShow][0]^=1;break;
        case 18:f.put(f.base+rva::NativeAudioCrtState,1u);break;
        case 19:f.put(f.base+rva::NativeAudioInitialGuHeader+36,1);break;
        case 20:f.memory[f.base+f.stack[5].function.UnwindData][0]^=1;break;
        case 21:f.memory[f.loader+0x9fa64][0]^=1;break;
        case 22:f.loader_nt.OptionalHeader.CheckSum+=1;break;
        case 23:f.wrong_path=true;break;
        case 24:f.put(f.loader+128,IMAGE_NT_HEADERS64{});break;
        case 25:f.stack.back().image=f.asi;break;
        case 26:f.stack[1].function.EndAddress+=1;break;
        }f.rejected();
        if(fault==9){const auto state=na::native_audio_bootstrap_status();assert(state.failed_frame==3 && state.observed_return_rva==0x27006 && state.expected_return_rva==0x330a3);}
    }
    for(bool post:{false,true})for(auto r:{rva::NativeBootstrapGuSerial,rva::NativeBootstrapHinstance,rva::NativeBootstrapTaskGraph,
        rva::NativeBootstrapPool,rva::NativeBootstrapBackgroundPool,rva::PackageStoreOwnerGlobal,rva::PackageStoreGeneration,
        rva::DataObjectNameMap,rva::BgmListRegistry,rva::ScoreInfoRegistry,rva::PianoScoreRegistry,rva::NativeBootstrapCrashStop,
        rva::NativeBootstrapCrashProcess,rva::NativeBootstrapCrashResult,rva::NativeBootstrapCrashComplete,
        rva::NativeBootstrapCrashWorker,rva::NativeBootstrapCrashRequest,rva::NativeBootstrapCrashEvent}) {
        BootstrapFixture f(post);
        if(r==rva::NativeBootstrapCrashWorker || r==rva::NativeBootstrapCrashRequest || r==rva::NativeBootstrapCrashEvent)f.put(f.base+r,post?0ull:1ull);
        else f.memory[f.base+r][0]^=1;
        f.rejected();
    }
    for(unsigned fault=0;fault<11;++fault) {
        BootstrapFixture f;f.fail_at=static_cast<int>(fault);assert(f.run() && f.callbacks==1 && !f.install_result);
        assert(!na::g_origin.load() && f.disabled==fault && f.removed==fault && !na::g_installed);
        assert(!f.mounts && !f.acquires && !f.name_calls);
    }
    for(unsigned point=0;point<11;++point) {
        BootstrapFixture f;f.mutate_install=static_cast<int>(point);assert(f.run() && !f.install_result);
        assert(f.installed==11 && f.disabled==11 && f.removed==11 && !na::g_origin.load());
        assert(na::native_audio_bootstrap_status().failure==na::BootstrapFailure::Baseline);
    }
    for(unsigned fault=0;fault<7;++fault) {
        BootstrapFixture f;
        f.before_install=[](BootstrapFixture& f){
            switch(f.fail_at){
            case 0:++f.actual_thread;break;case 1:f.wrong_export=true;break;case 2:f.file_changed=true;break;
            case 3:f.memory[f.loader+0x3308c][0]^=1;break;case 4:f.stack[3].pc+=1;break;
            case 5:f.memory[f.base+rva::NativeBootstrapCookieTime][0]^=1;break;case 6:f.put(f.base+rva::NativeAudioCrtState,1u);break;
            }
        };f.fail_at=static_cast<int>(fault);
        assert(f.run() && f.callbacks==1 && !f.install_result && !f.installed && !na::g_origin.load());
    }
    for(unsigned fault=0;fault<6;++fault) {
        BootstrapFixture f;f.fail_at=static_cast<int>(fault+30);
        f.during_install=[](BootstrapFixture& f,unsigned index){if(index!=10)return;
            switch(f.fail_at-30){
            case 0:++f.actual_thread;break;case 1:f.file_changed=true;break;
            case 2:f.memory[f.loader+0x3308c][0]^=1;break;case 3:f.stack[3].pc+=1;break;
            case 4:f.memory[f.base+rva::NativeBootstrapCookieTime][0]^=1;break;case 5:f.wrong_export=true;break;
            }
        };
        assert(f.run() && !f.install_result && f.removed==11 && f.disabled==11 && !na::g_origin.load());
    }
    {BootstrapFixture f;f.noop=true;assert(!na::try_synchronous_native_audio_startup(reinterpret_cast<HMODULE>(f.base),reinterpret_cast<HMODULE>(f.asi),f.loader+0x21682,BootstrapFixture::initialize));assert(!f.callbacks);}
    {BootstrapFixture f;f.noop=true;assert(f.run() && !na::g_origin.load());na::test_install_hook=nullptr;
        assert(!na::install_native_audio_runtime(reinterpret_cast<HMODULE>(f.base),{RuntimeFixture::identity,RuntimeFixture::score}) && !f.installed);}
    {BootstrapFixture f;f.noop=true;assert(f.run() && !na::g_origin.load());assert(!na::bootstrap_detail::begin_publication(reinterpret_cast<HMODULE>(f.base)) && !f.installed);}
    {BootstrapFixture f;f.recurse=true;assert(f.run() && f.callbacks==1 && !f.install_result && !f.installed && !na::g_origin.load());}
    {BootstrapFixture f;f.before_install=[](BootstrapFixture&){std::thread other([]{
        assert(!na::bootstrap_detail::scope_present());assert(!na::bootstrap_detail::begin_publication(reinterpret_cast<HMODULE>(BootstrapFixture::base)));
     });other.join();};assert(f.run() && !f.install_result && !f.installed && !na::g_origin.load());}
    {BootstrapFixture f;bs::File file;file.handle=CreateFileW(f.artifacts->files[0].path.c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,0,nullptr);assert(file.handle!=INVALID_HANDLE_VALUE);
     std::array<unsigned char,32> hash{};assert(bs::digest(file.handle,7,hash));
     const auto expected=ff7rp::pipeline::native_sha256({reinterpret_cast<const std::uint8_t*>("fixture"),7});
     for(unsigned i=0;i<32;++i)assert(hash[i]==static_cast<unsigned char>(expected[i]));
     assert(!bs::digest(file.handle,8,hash) && !bs::digest(file.handle,128ull*1024*1024+1,hash));
     assert(GetFileInformationByHandle(file.handle,&file.identity));bs::test.unchanged=nullptr;assert(bs::unchanged(file));}
    // Observe only this test executable, never the game or UAL/plugin startup.
    {bs::test={};bs::image_failure_claimed.clear();bs::image_failure_ready.store(false);
     const auto self=GetModuleHandleW(nullptr);bs::File file;assert(bs::open_file(self,file));
     IMAGE_NT_HEADERS64 loaded{};assert(bs::read(reinterpret_cast<na::Address>(self)+file.nt_offset,loaded));
     assert(loaded.OptionalHeader.ImageBase==file.nt.OptionalHeader.ImageBase
         || loaded.OptionalHeader.ImageBase==reinterpret_cast<na::Address>(self));
     assert(bs::image_matches(self,file,file.nt.FileHeader.TimeDateStamp,file.nt.OptionalHeader.SizeOfImage,
          file.nt.OptionalHeader.CheckSum,file.nt.OptionalHeader.AddressOfEntryPoint,na::BootstrapImageModule::Executable));
     bs::Frame compiled{};RUNTIME_FUNCTION primary{};
     assert(bs::lookup(reinterpret_cast<na::Address>(&na::try_synchronous_native_audio_startup),compiled));
     assert(compiled.image==reinterpret_cast<na::Address>(self) && bs::primary_function(compiled,file,primary,{}));
     assert(!bs::image_failure_ready.load());
     std::cout<<"own executable preferred=0x"<<std::hex<<file.nt.OptionalHeader.ImageBase
         <<" actual=0x"<<reinterpret_cast<na::Address>(self)<<" header=0x"<<loaded.OptionalHeader.ImageBase<<std::dec<<"\n";}
    if(argc>1){assert(argc==4);actual_metadata_tests(argv[1],argv[2],argv[3]);}
    std::cout<<"native_audio_bootstrap_selftest passed\n";
}
