#include "game/native_audio_bootstrap.h"
#include "game/native_bootstrap_baseline.h"
#include "game/hook_specs.h"
#include "game/rvas.h"
#include "core/generated/build_identity.generated.h"
#include "core/pe_image.h"
#include <bcrypt.h>
#include <psapi.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <string_view>

namespace ff7r::piano::game::native_audio::baseline_facts {
bool check(std::uintptr_t base,bool post,bool srw,Reader copy,const char** failure) noexcept {
    const auto read=[&](std::uintptr_t address,auto& value){return copy(address,&value,sizeof(value));};
    const auto failed=[&](bool value,const char* predicate) {
        if(value && failure && !*failure)*failure=predicate;
        return value;
    };
    std::uint32_t crt=~0u,serial=~0u,stop=~0u,generation=~0u;unsigned char complete=255;
    std::array<unsigned char,48> gu{},expected{};
    if(post){std::fill(expected.begin()+4,expected.begin()+8,static_cast<unsigned char>(0xff));expected[12]=1;}
    if(failed(!read(base+rva::NativeAudioCrtState,crt) || crt!=(post?2u:srw?1u:0u),"baseline.crt")
        || failed(!read(base+rva::NativeAudioInitialGuHeader,gu) || gu!=expected,"baseline.gu")
        || failed(!read(base+rva::NativeBootstrapGuSerial,serial) || serial!=(post?1000u:0u),"baseline.gu_serial")
        || failed(!read(base+rva::PackageStoreGeneration,generation) || generation,"baseline.generation")
        || failed(!read(base+rva::NativeBootstrapCrashStop,stop) || stop,"baseline.crash_stop")
        || failed(!read(base+rva::NativeBootstrapCrashComplete,complete) || complete!=(post?1:0),"baseline.crash_complete"))return false;
    for(auto r:{rva::NativeBootstrapHinstance,rva::NativeBootstrapTaskGraph,rva::NativeBootstrapPool,
        rva::NativeBootstrapBackgroundPool,rva::PackageStoreOwnerGlobal,rva::DataObjectNameMap,
        rva::NativeBootstrapCrashProcess,rva::NativeBootstrapCrashResult}) {
        std::uint64_t value=1;if(failed(!r || !read(base+r,value) || value,"baseline.absent_global"))return false;
    }
    if constexpr(rva::NativeBootstrapIoPool!=0) {
        std::uint64_t value=1;if(failed(!read(base+rva::NativeBootstrapIoPool,value) || value,"baseline.io_pool"))return false;
    }
    for(auto r:{rva::BgmListRegistry,rva::ScoreInfoRegistry,rva::PianoScoreRegistry}) {
        std::array<unsigned char,48> group{};
        if(failed(!read(base+r,group) || group!=std::array<unsigned char,48>{},"baseline.registry"))return false;
    }
    for(auto r:{rva::NativeBootstrapCrashWorker,rva::NativeBootstrapCrashRequest,rva::NativeBootstrapCrashEvent}) {
        std::uint64_t value=0;if(failed(!read(base+r,value) || (post?(!value || value==~0ull):value!=0),"baseline.crash_handle"))return false;
    }
    if(srw) {
        std::uint32_t guard=~0u;unsigned char done=255;
        if(failed(!read(base+rva::NativeBootstrapSrwGuard,guard) || guard,"baseline.srw_guard")
            || failed(!read(base+rva::NativeBootstrapSrwPoolComplete,done) || done,"baseline.srw_complete"))return false;
        for(auto r:{rva::NativeBootstrapSrwGmalloc,rva::NativeBootstrapSrwReserve,rva::NativeBootstrapSrwLock,rva::NativeBootstrapSrwOutput}) {
            std::uint64_t value=1;if(failed(!r || !read(base+r,value) || value,"baseline.srw_global"))return false;
        }
    } else if(!post){std::uint64_t cookie=0;if(failed(!read(base+rva::NativeBootstrapCookieValue,cookie) || cookie!=0x2b992ddfa232ull,"baseline.cookie"))return false;}
    return true;
}
}

namespace ff7r::piano::game::native_audio {
namespace bootstrap_impl {
using Address=std::uintptr_t;
constexpr std::size_t kFrames=64;
// External loader profile, NOT game RVAs. Exact release/source provenance:
// analysis/NativeAudioLoaderHandoff-20261016.json (UAL 9.7.2, ab722bef...).
constexpr char kLoaderSha[]="22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c";
// Exact external CRT fixture; no cross-Windows-version or CFG-override fallback.
constexpr char kCrtSha[]="5c52e3a303baaac0e0af8bd9b96134993da34bc9d834a31ef37e1d2cdc7fe192";
constexpr DWORD kCrtImageSize=0x14c000,kCrtChecksum=0x152ee3;
// Exact external SCP-NP profile: analysis/NativeAudioCrtRetpoline-20261017.json.
constexpr char kNtdllSha[]="a74f7482085eab125ccc09152ab7e0b5994bcb13e1a7b29880bdbb24179ecb8b";
constexpr char kScpSha[]="d34e6384deadab7a818e5d2472c64288a44669d23cb6deb2e78a5533160568c0";
constexpr DWORD kNtdllSize=0x267000,kNtdllChecksum=0x26a8ff;
constexpr DWORD kDvrtSection=0x14b000,kDvrtTable=0x14bb28;
constexpr DWORD kScpSource=0x16e000,kScpEnd=0x16e2b0,kScpEntry=0x16e0c0,kScpFunctionEnd=0x16e103,kScpUnwind=0x1b6914;
constexpr DWORD kScpExports=0x1746f0,kScpExportsEnd=0x1746f8,kSystemBlock=0x1ea450,kSystemPointer=0x1ea558;
constexpr ULONGLONG kScpPreferred=0x18016e000,kScpPreferredEnd=0x18016e2b0;
struct Window {Address rva;std::string_view hex;};
constexpr Window loader_windows[]={
    {0x2166a,"488d15df990600488bceff15361a06004885c07402ffd04183e7fd44897c2430"},
    {0x20ea0,"40555356574154415541564157488dac24d8feffff4881ec28020000"},
    {0x3308c,"33c0f0440fb1253d0b0800750ae88247ffffe8ddd6ffff488b8d500600004833cce8beab0100"},
    {0x30acc,"488d8d50020000e8c803ffff4c8d85b0010000"},
    {0x30b87,"e8440cffff"},{0x30c47,"e8840bffff"},{0x30d33,"e8980affff"},
    {0x21b5f,"e83cf3ffff"},{0x21c3f,"e85cf2ffff"},
    {0x1fa49,"48c74424280f000000488b4c2438488d0502bb0600488d5424204889442420e8d3140100488bcb4883c4305b48ff2554370600"},
    {0x1fac9,"48c744242817000000488b4c2438488d05e2bb0600488d5424204889442420e853140100488bcb4883c4305b48ff25e4360600"},
    {0x26fe7,"33c0b901000000f00fb10ddecb08000f855e030000e81f080000e87a970000e94f030000"},
    {0x8b050,"496e697469616c697a6541534900"},
    {0x1ee89,"48c744242817000000488b4c2438488d05bac80600488d5424204889442420e893200100488bcb4883c4305b48ff25c4420600"},
};
constexpr const char* game_windows[]={"native_audio_exe_entry","native_bootstrap_cookie","native_bootstrap_crt",
    "native_bootstrap_crt_complete","native_bootstrap_show","native_bootstrap_show_to_winmain",
    "native_bootstrap_winmain","native_bootstrap_hinstance_store","native_bootstrap_cookie_time",
    "native_bootstrap_task_publish","native_bootstrap_task_call","native_bootstrap_crash_entry"};
constexpr const char* srw_windows[]={"native_bootstrap_srw_header","native_bootstrap_srw_guard_call",
    "native_bootstrap_srw_malloc_secondary","native_bootstrap_srw_pool_secondary","native_bootstrap_srw_pool_call",
    "native_bootstrap_srw_block_call","native_bootstrap_srw_name_call","native_bootstrap_srw_first_name",
    "native_bootstrap_srw_initterm_call","native_bootstrap_srw_reserve_code","native_bootstrap_srw_malloc_code",
    "native_bootstrap_srw_name_destination","native_bootstrap_srw_initterm_thunk"};
constexpr Window crt_windows[]={
    {0x707a0,"483bca742b48895c2408574883ec20488bfa488bd9488b034885c075144883c308483bdf75ef488b5c24304883c4205fc3e83a780800ebe5"},
    {0xf8010,"e98b56ffff"},{0xed6a0,"ff2502050100"},{0xed6c0,"ffe0"}};
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity{};
    IMAGE_NT_HEADERS64 nt{};
    LONG nt_offset=0;
    std::array<IMAGE_SECTION_HEADER,96> sections{};
    std::array<unsigned char,32> sha{};
    std::wstring path;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
struct Frame {Address pc=0,image=0;RUNTIME_FUNCTION function{};Address rbx=0,rdi=0;};
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
struct TestAccess {
    bool (*read)(Address,void*,std::size_t)=nullptr;
    bool (*open)(HMODULE,File&)=nullptr;
    bool (*unchanged)(const File&)=nullptr;
    HMODULE (*module)(Address)=nullptr;
    Address (*own_export)(HMODULE)=nullptr;
    unsigned (*capture)(Frame*,unsigned)=nullptr;
    bool (*disk_function)(const File&,DWORD,RUNTIME_FUNCTION&)=nullptr;
    DWORD (*thread)()=nullptr;
    HMODULE (*exe)()=nullptr;
    bool (*system_module)(HMODULE)=nullptr;
    bool (*disk_read)(const File&,DWORD,void*,DWORD)=nullptr;
    bool (*lookup)(Address,Frame&)=nullptr;
    bool (*context)(CONTEXT&,Address&,Address&)=nullptr;
    bool (*unwind)(CONTEXT&,Address,RUNTIME_FUNCTION&)=nullptr;
    bool (*crt_path)(const File&)=nullptr;
    HMODULE (*ntdll)()=nullptr;
    bool (*ntdll_path)(const File&)=nullptr;
    bool (*query)(Address,MEMORY_BASIC_INFORMATION&)=nullptr;
    bool (*mapped_file)(Address,const File&)=nullptr;
};
TestAccess test{};
#endif
std::atomic_flag attempted=ATOMIC_FLAG_INIT;
std::atomic<BootstrapSource> diagnostic_source{BootstrapSource::None};
std::atomic<BootstrapFailure> diagnostic_failure{BootstrapFailure::None};
std::atomic<DWORD> diagnostic_thread{0};
std::atomic_uint diagnostic_frames{0};
std::atomic_bool diagnostic_invoked{false},diagnostic_published{false};
std::atomic_uint diagnostic_frame{0};
std::atomic<Address> diagnostic_observed{0},diagnostic_expected{0};
std::atomic_flag image_failure_claimed=ATOMIC_FLAG_INIT;
std::atomic_bool image_failure_ready{false};
BootstrapImageFailure image_failure{}; // Published once; read only after release/acquire.
std::atomic_flag frame_failure_claimed=ATOMIC_FLAG_INIT;
std::atomic_bool frame_failure_ready{false};
BootstrapFrameFailure frame_failure{};
BootstrapFunctionRecord record(const RUNTIME_FUNCTION& f) noexcept {return {f.BeginAddress,f.EndAddress,f.UnwindData};}
bool frame_reject(BootstrapFrameFailure facts,const char* predicate) noexcept {
    if(!frame_failure_claimed.test_and_set(std::memory_order_acq_rel)) {
        facts.predicate=predicate;frame_failure=facts;frame_failure_ready.store(true,std::memory_order_release);
    }
    return false;
}
bool image_reject(BootstrapImageModule module,BootstrapImageStage stage,const char* field,
    std::uint64_t expected,std::uint64_t observed,unsigned offset=0,unsigned index=0) noexcept {
    if(!image_failure_claimed.test_and_set(std::memory_order_acq_rel)) {
        image_failure={module,stage,field,offset,index,expected,observed};
        image_failure_ready.store(true,std::memory_order_release);
    }
    return false;
}
bool reject(BootstrapFailure failure) noexcept {
    auto expected=BootstrapFailure::None;
    diagnostic_failure.compare_exchange_strong(expected,failure,std::memory_order_acq_rel);return false;
}
DWORD thread() noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.thread)return test.thread();
#endif
    return GetCurrentThreadId();
}
bool copy(Address address,void* out,std::size_t size) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.read)return test.read(address,out,size);
#endif
    return size && size<=65536 && core::safe_copy_bytes(reinterpret_cast<void*>(address),out,size);
}
template<class T> bool read(Address address,T& out) noexcept {return copy(address,&out,sizeof(out));}
HMODULE module_at(Address address) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.module)return test.module(address);
#endif
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address),&module)?module:nullptr;
}
bool file_read(HANDLE file,std::uint64_t offset,void* out,DWORD size) noexcept {
    LARGE_INTEGER where{};where.QuadPart=offset;DWORD got=0;
    return size && size<=65536 && SetFilePointerEx(file,where,nullptr,FILE_BEGIN)
        && ReadFile(file,out,size,&got,nullptr) && got==size;
}
bool digest(HANDLE file,std::uint64_t size,std::array<unsigned char,32>& out) noexcept {
    if(!size || size>128ull*1024*1024)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    const bool created=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
        && BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    bool ok=created;std::array<unsigned char,65536> buffer{};
    for(std::uint64_t offset=0;ok && offset<size;) {
        const DWORD n=static_cast<DWORD>((size-offset)<buffer.size()?(size-offset):buffer.size());
        ok=file_read(file,offset,buffer.data(),n) && BCryptHashData(hash,buffer.data(),n,0)>=0;offset+=n;
    }
    ok=ok && BCryptFinishHash(hash,out.data(),static_cast<ULONG>(out.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
int nibble(char c) noexcept {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
bool hash_equal(const std::array<unsigned char,32>& hash,std::string_view hex) noexcept {
    if(hex.size()!=64)return false;
    for(std::size_t i=0;i<hash.size();++i)if(nibble(hex[i*2])<0 || nibble(hex[i*2+1])<0
        || hash[i]!=(nibble(hex[i*2])*16+nibble(hex[i*2+1])))return false;return true;
}
bool open_file(HMODULE module,File& file) {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.open)return test.open(module,file);
#endif
    std::array<wchar_t,32768> path{};const auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if(!n || n>=path.size())return false;file.path.assign(path.data(),n);
    file.handle=CreateFileW(file.path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file.handle==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.handle,&file.identity)
        || (file.identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    const auto size=(static_cast<std::uint64_t>(file.identity.nFileSizeHigh)<<32)|file.identity.nFileSizeLow;
    IMAGE_DOS_HEADER dos{};
    if(!file_read(file.handle,0,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0
        || static_cast<std::uint64_t>(dos.e_lfanew)+sizeof(file.nt)>size
        || !file_read(file.handle,dos.e_lfanew,&file.nt,sizeof(file.nt))
        || file.nt.Signature!=IMAGE_NT_SIGNATURE || file.nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
        || file.nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || file.nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64)
        || !file.nt.FileHeader.NumberOfSections || file.nt.FileHeader.NumberOfSections>file.sections.size())return false;
    if(!file_read(file.handle,dos.e_lfanew+sizeof(file.nt),file.sections.data(),
        file.nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)))return false;
    file.nt_offset=dos.e_lfanew;
    return digest(file.handle,size,file.sha);
}
bool unchanged(const File& file) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.unchanged)return test.unchanged(file);
#endif
    BY_HANDLE_FILE_INFORMATION current{};
    return GetFileInformationByHandle(file.handle,&current)
        && current.dwVolumeSerialNumber==file.identity.dwVolumeSerialNumber
        && current.nFileIndexHigh==file.identity.nFileIndexHigh && current.nFileIndexLow==file.identity.nFileIndexLow
        && current.nFileSizeHigh==file.identity.nFileSizeHigh && current.nFileSizeLow==file.identity.nFileSizeLow
        && current.dwFileAttributes==file.identity.dwFileAttributes
        && !std::memcmp(&current.ftLastWriteTime,&file.identity.ftLastWriteTime,sizeof(FILETIME));
}
bool disk_rva(const File& file,DWORD rva,void* out,DWORD size) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.disk_read)return test.disk_read(file,rva,out,size);
#endif
    for(unsigned i=0;i<file.nt.FileHeader.NumberOfSections;++i) {
        const auto& s=file.sections[i];
        if(rva>=s.VirtualAddress && static_cast<std::uint64_t>(rva)+size<=static_cast<std::uint64_t>(s.VirtualAddress)+s.SizeOfRawData)
            return file_read(file.handle,static_cast<std::uint64_t>(s.PointerToRawData)+rva-s.VirtualAddress,out,size);
    }
    return false;
}
bool disk_function(const File& file,DWORD pc,RUNTIME_FUNCTION& out) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.disk_function)return test.disk_function(file,pc,out);
#endif
    const auto& table=file.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if(!table.VirtualAddress || !table.Size || table.Size%sizeof(out) || table.Size>16*1024*1024)return false;
    DWORD lo=0,hi=table.Size/sizeof(out);
    while(lo<hi) {
        const DWORD mid=lo+(hi-lo)/2;RUNTIME_FUNCTION f{};
        if(!disk_rva(file,table.VirtualAddress+mid*sizeof(f),&f,sizeof(f)))return false;
        if(pc<f.BeginAddress)hi=mid;else if(pc>=f.EndAddress)lo=mid+1;else {out=f;return true;}
    }
    return false;
}
bool lookup(Address pc,Frame& frame) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.lookup)return test.lookup(pc,frame);
#endif
    __try {
        DWORD64 image=0;const auto* function=RtlLookupFunctionEntry(pc,&image,nullptr);
        if(!function || !read(reinterpret_cast<Address>(function),frame.function))return false;
        frame.pc=pc;frame.image=static_cast<Address>(image);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
unsigned capture(Frame* frames,unsigned capacity) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.capture)return test.capture(frames,capacity);
#endif
    std::array<void*,kFrames> pcs{};const unsigned n=RtlCaptureStackBackTrace(0,capacity,pcs.data(),nullptr);
    for(unsigned i=0;i<n;++i) {
        if(!lookup(reinterpret_cast<Address>(pcs[i]),frames[i]))return 0;
    }
    return n;
}
bool bytes(Address address,std::string_view hex) noexcept {
    if(hex.empty() || hex.size()%2 || hex.size()>256)return false;
    std::array<unsigned char,128> current{};if(!copy(address,current.data(),hex.size()/2))return false;
    for(std::size_t i=0;i<hex.size()/2;++i)if(current[i]!=(nibble(hex[i*2])*16+nibble(hex[i*2+1])))return false;return true;
}
bool image_matches(HMODULE module,const File& file,DWORD stamp,DWORD size,DWORD checksum,DWORD aep,
    BootstrapImageModule which) noexcept {
    using Stage=BootstrapImageStage;
    const auto fail=[&](Stage stage,const char* field,std::uint64_t expected,std::uint64_t observed,
        unsigned offset=0,unsigned index=0){return image_reject(which,stage,field,expected,observed,offset,index);};
    const auto& disk=file.nt;
#define DISK_CHECK(field,value) if(disk.field!=(value))return fail(Stage::DiskHeader,#field,value,disk.field)
    DISK_CHECK(Signature,IMAGE_NT_SIGNATURE);DISK_CHECK(FileHeader.Machine,IMAGE_FILE_MACHINE_AMD64);
    DISK_CHECK(OptionalHeader.Magic,IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    DISK_CHECK(FileHeader.TimeDateStamp,stamp);DISK_CHECK(OptionalHeader.SizeOfImage,size);
    DISK_CHECK(OptionalHeader.CheckSum,checksum);DISK_CHECK(OptionalHeader.AddressOfEntryPoint,aep);
    DISK_CHECK(FileHeader.SizeOfOptionalHeader,sizeof(IMAGE_OPTIONAL_HEADER64));
#undef DISK_CHECK
    if(!disk.FileHeader.NumberOfSections || disk.FileHeader.NumberOfSections>file.sections.size())
        return fail(Stage::DiskHeader,"FileHeader.NumberOfSections",file.sections.size(),disk.FileHeader.NumberOfSections);
    const auto base=reinterpret_cast<Address>(module);const auto preferred=disk.OptionalHeader.ImageBase;
    if(!base || (base&65535) || !preferred || (preferred&65535) || base>UINTPTR_MAX-size)
        return fail(Stage::NtHeader,"ModuleBase",preferred,base);
    // PE ImageBase denotes the preferred VA. Windows may publish the actual VA
    // there after relocation; no other NT header byte is normalized.
    const auto& reloc=disk.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if(base!=preferred && ((disk.FileHeader.Characteristics&IMAGE_FILE_RELOCS_STRIPPED)
        || disk.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_BASERELOC
        || !reloc.VirtualAddress || reloc.Size<sizeof(IMAGE_BASE_RELOCATION)
        || reloc.VirtualAddress>=size || reloc.Size>size-reloc.VirtualAddress))
        return fail(Stage::DiskHeader,"RelocatableImageBase",preferred,base);
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!read(base,dos))return fail(Stage::DosHeader,"Read",sizeof(dos),0);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE)return fail(Stage::DosHeader,"e_magic",IMAGE_DOS_SIGNATURE,dos.e_magic);
    if(dos.e_lfanew!=file.nt_offset || dos.e_lfanew<0 || dos.e_lfanew>=4096)
        return fail(Stage::DosHeader,"e_lfanew",file.nt_offset,static_cast<std::uint32_t>(dos.e_lfanew));
    if(!read(base+dos.e_lfanew,nt))return fail(Stage::NtHeader,"Read",sizeof(nt),0);
    if(nt.OptionalHeader.ImageBase!=preferred && nt.OptionalHeader.ImageBase!=base)
        return fail(Stage::NtHeader,"OptionalHeader.ImageBase",base,nt.OptionalHeader.ImageBase,
            offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+offsetof(IMAGE_OPTIONAL_HEADER64,ImageBase));
    nt.OptionalHeader.ImageBase=preferred;
    const auto* observed=reinterpret_cast<const unsigned char*>(&nt);
    const auto* expected=reinterpret_cast<const unsigned char*>(&disk);
    for(unsigned i=0;i<sizeof(nt);++i)if(observed[i]!=expected[i]) {
        const char* field="OptionalHeader.DataDirectory";unsigned field_offset=i,field_size=1,index=0;
#define NT_FIELD(name) if(i>=offsetof(IMAGE_NT_HEADERS64,name) && i<offsetof(IMAGE_NT_HEADERS64,name)+sizeof(disk.name)){field=#name;field_offset=offsetof(IMAGE_NT_HEADERS64,name);field_size=sizeof(disk.name);}
#define FH(name) NT_FIELD(FileHeader.name)
#define OH(name) NT_FIELD(OptionalHeader.name)
        NT_FIELD(Signature);FH(Machine);FH(NumberOfSections);FH(TimeDateStamp);FH(PointerToSymbolTable);
        FH(NumberOfSymbols);FH(SizeOfOptionalHeader);FH(Characteristics);
        OH(Magic);OH(MajorLinkerVersion);OH(MinorLinkerVersion);OH(SizeOfCode);OH(SizeOfInitializedData);
        OH(SizeOfUninitializedData);OH(AddressOfEntryPoint);OH(BaseOfCode);OH(SectionAlignment);OH(FileAlignment);
        OH(MajorOperatingSystemVersion);OH(MinorOperatingSystemVersion);OH(MajorImageVersion);OH(MinorImageVersion);
        OH(MajorSubsystemVersion);OH(MinorSubsystemVersion);OH(Win32VersionValue);OH(SizeOfImage);OH(SizeOfHeaders);
        OH(CheckSum);OH(Subsystem);OH(DllCharacteristics);OH(SizeOfStackReserve);OH(SizeOfStackCommit);
        OH(SizeOfHeapReserve);OH(SizeOfHeapCommit);OH(LoaderFlags);OH(NumberOfRvaAndSizes);
#undef OH
#undef FH
#undef NT_FIELD
        constexpr auto directories=offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+offsetof(IMAGE_OPTIONAL_HEADER64,DataDirectory);
        if(i>=directories){index=(i-directories)/sizeof(IMAGE_DATA_DIRECTORY);const bool length=(i-directories)%8>=4;
            field=length?"OptionalHeader.DataDirectory.Size":"OptionalHeader.DataDirectory.VirtualAddress";
            field_offset=directories+index*8+(length?4:0);field_size=4;}
        std::uint64_t a=0,b=0;std::memcpy(&a,expected+field_offset,field_size);std::memcpy(&b,observed+field_offset,field_size);
        return fail(Stage::NtHeader,field,a,b,field_offset,index);
    }
    std::array<IMAGE_SECTION_HEADER,96> sections{};
    const auto count=disk.FileHeader.NumberOfSections;const auto extent=count*sizeof(sections[0]);
    const auto section_offset=static_cast<std::uint64_t>(file.nt_offset)+sizeof(nt);
    if(section_offset+extent>disk.OptionalHeader.SizeOfHeaders || section_offset+extent>size)
        return fail(Stage::Sections,"SectionTableExtent",disk.OptionalHeader.SizeOfHeaders,section_offset+extent);
    if(!copy(base+section_offset,sections.data(),extent))return fail(Stage::Sections,"Read",extent,0);
    for(unsigned n=0;n<count;++n) {
        const auto* a=reinterpret_cast<const unsigned char*>(&sections[n]);
        const auto* b=reinterpret_cast<const unsigned char*>(&file.sections[n]);
        for(unsigned i=0;i<sizeof(sections[n]);++i)if(a[i]!=b[i]) {
            const char* field="SectionHeader";unsigned start=i,length=1;
#define SECTION_FIELD(name) if(i>=offsetof(IMAGE_SECTION_HEADER,name) && i<offsetof(IMAGE_SECTION_HEADER,name)+sizeof(sections[n].name)){field="SectionHeader." #name;start=offsetof(IMAGE_SECTION_HEADER,name);length=sizeof(sections[n].name);}
            SECTION_FIELD(Name);SECTION_FIELD(Misc.VirtualSize);SECTION_FIELD(VirtualAddress);SECTION_FIELD(SizeOfRawData);
            SECTION_FIELD(PointerToRawData);SECTION_FIELD(PointerToRelocations);SECTION_FIELD(PointerToLinenumbers);
            SECTION_FIELD(NumberOfRelocations);SECTION_FIELD(NumberOfLinenumbers);SECTION_FIELD(Characteristics);
#undef SECTION_FIELD
            std::uint64_t x=0,y=0;std::memcpy(&x,b+start,length);std::memcpy(&y,a+start,length);
            return fail(Stage::Sections,field,x,y,start,n);
        }
    }
    return true;
}
enum class Publication { Fresh, Installing, Published, Failed };
struct Scope {
    HMODULE exe=nullptr,asi=nullptr,loader=nullptr,crt=nullptr,ntdll=nullptr;
    Address caller=0,export_target=0;DWORD owner_thread=0;
    BootstrapSource source=BootstrapSource::None;Publication publication=Publication::Fresh;
    File executable,proxy,self,crt_file,ntdll_file;
};
thread_local Scope* scope=nullptr;
Address own_export(HMODULE asi) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.own_export)return test.own_export(asi);
#endif
    return reinterpret_cast<Address>(GetProcAddress(asi,"InitializeASI"));
}
bool system_module(HMODULE module) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.system_module)return test.system_module(module);
#endif
    const wchar_t* name=nullptr;
    if(module==GetModuleHandleW(L"ntdll.dll"))name=L"ntdll.dll";
    else if(module==GetModuleHandleW(L"kernel32.dll"))name=L"kernel32.dll";
    else return false;
    std::array<wchar_t,32768> path{},directory{};
    const auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    const auto d=GetSystemDirectoryW(directory.data(),static_cast<UINT>(directory.size()));
    if(!n || n>=path.size() || !d || d+32>=directory.size())return false;
    // Bounded system-frame suffix only; no unknown DLL frames are skipped.
    wcscat_s(directory.data(),directory.size(),L"\\");wcscat_s(directory.data(),directory.size(),name);
    return !_wcsicmp(path.data(),directory.data());
}
bool code(const Scope& s) noexcept {
    const auto exe=reinterpret_cast<Address>(s.exe),loader=reinterpret_cast<Address>(s.loader);
    for(auto id:game_windows) {
        const auto* spec=find_rva_signature(id);
        if(!spec || !spec->rva || spec->expected_prologue.empty() || spec->expected_prologue.size()>128)return false;
        std::array<unsigned char,128> current{};
        if(!copy(exe+spec->rva,current.data(),spec->expected_prologue.size())
            || std::memcmp(current.data(),spec->expected_prologue.data(),spec->expected_prologue.size()))return false;
    }
    for(const auto& w:loader_windows)if(!bytes(loader+w.rva,w.hex))return false;
    return true;
}
bool baseline(const Scope& s) noexcept {
    return baseline_facts::check(reinterpret_cast<Address>(s.exe),s.source==BootstrapSource::PreWinMain,
        s.source==BootstrapSource::FirstSrw,copy);
}
bool unwind_record(Address image,const File& file,const RUNTIME_FUNCTION& disk,
    BootstrapFrameFailure& facts,RUNTIME_FUNCTION& parent,bool& chained) noexcept {
    facts.expected=record(disk);
    if(disk.BeginAddress>=disk.EndAddress || disk.EndAddress>file.nt.OptionalHeader.SizeOfImage
        || disk.UnwindData>=file.nt.OptionalHeader.SizeOfImage-4 || (disk.UnwindData&3))
        return frame_reject(facts,"function_bounds");
    std::array<unsigned char,544> expected{},current{};
    if(!disk_rva(file,disk.UnwindData,expected.data(),4))return frame_reject(facts,"unwind_disk_header");
    const auto flags=expected[0]>>3,version=expected[0]&7;
    if((version!=1 && version!=2) || flags>7 || ((flags&4) && (flags&3))) {
        facts.observed_byte=expected[0];return frame_reject(facts,"unwind_format");
    }
    const DWORD tail=(4u+2u*expected[2]+3u)&~3u;
    const DWORD size=tail+((flags&4)?12u:(flags&3)?4u:0u);
    if(size>expected.size() || size>file.nt.OptionalHeader.SizeOfImage
        || disk.UnwindData>file.nt.OptionalHeader.SizeOfImage-size)return frame_reject(facts,"unwind_extent");
    if(!disk_rva(file,disk.UnwindData,expected.data(),size))return frame_reject(facts,"unwind_disk_bytes");
    if(!copy(image+disk.UnwindData,current.data(),size))return frame_reject(facts,"unwind_live_bytes");
    for(unsigned i=0;i<size;++i)if(expected[i]!=current[i]) {
        facts.byte_offset=i;facts.expected_byte=expected[i];facts.observed_byte=current[i];
        return frame_reject(facts,"unwind_byte_mismatch");
    }
    chained=(flags&UNW_FLAG_CHAININFO)!=0;
    if(chained)std::memcpy(&parent,expected.data()+tail,sizeof(parent));
    return true;
}
bool function(const Frame& f,const File& file,DWORD begin,DWORD end,DWORD unwind,
    BootstrapFrameFailure facts) noexcept {
    RUNTIME_FUNCTION disk{};facts.observed=record(f.function);
    if(f.pc<f.image || f.pc-f.image>MAXDWORD
        || !disk_function(file,static_cast<DWORD>(f.pc-f.image),disk))return frame_reject(facts,"function_disk_lookup");
    facts.expected=record(disk);
    if(std::memcmp(&disk,&f.function,sizeof(disk)))return frame_reject(facts,"function_lookup_record");
    if(disk.BeginAddress!=begin || disk.EndAddress!=end || (unwind && disk.UnwindData!=unwind))
        return frame_reject(facts,"function_profile_record");
    RUNTIME_FUNCTION unused{};bool chained=false;return unwind_record(f.image,file,disk,facts,unused,chained);
}
bool primary_function(const Frame& frame,const File& file,RUNTIME_FUNCTION& primary,BootstrapFrameFailure facts) noexcept {
    facts.observed=record(frame.function);RUNTIME_FUNCTION disk{};
    if(frame.pc<frame.image || frame.pc-frame.image>MAXDWORD
        || !disk_function(file,static_cast<DWORD>(frame.pc-frame.image),disk))return frame_reject(facts,"own_disk_lookup");
    facts.expected=record(disk);
    if(std::memcmp(&disk,&frame.function,sizeof(disk)))return frame_reject(facts,"own_lookup_record");
    std::array<RUNTIME_FUNCTION,16> visited{};
    for(unsigned depth=0;depth<visited.size();++depth) {
        facts.chain_depth=depth;facts.expected=record(disk);
        for(unsigned i=0;i<depth;++i)if(!std::memcmp(&visited[i],&disk,sizeof(disk)))return frame_reject(facts,"own_chain_cycle");
        visited[depth]=disk;RUNTIME_FUNCTION parent{};bool chained=false;
        if(!unwind_record(frame.image,file,disk,facts,parent,chained))return false;
        if(!chained){primary=disk;return true;}
        // CHAININFO names another exact pdata entry, not an arbitrary function address.
        RUNTIME_FUNCTION authoritative{};facts.observed=record(parent);
        if(!disk_function(file,parent.BeginAddress,authoritative))return frame_reject(facts,"own_chain_lookup");
        facts.expected=record(authoritative);
        if(std::memcmp(&parent,&authoritative,sizeof(parent)))return frame_reject(facts,"own_chain_record");
        disk=parent;
    }
    return frame_reject(facts,"own_chain_bound");
}
template<class T> bool disk_live(const File& file,Address base,DWORD rva,T& out) noexcept {
    T live{};
    return rva<file.nt.OptionalHeader.SizeOfImage && sizeof(T)<=file.nt.OptionalHeader.SizeOfImage-rva
        && disk_rva(file,rva,&out,sizeof(out)) && read(base+rva,live) && !std::memcmp(&out,&live,sizeof(out));
}
bool disk_text(const File& file,Address base,DWORD rva,std::string_view text) noexcept {
    std::array<char,128> expected{},live{};const auto n=text.size()+1;
    return n<=expected.size() && rva<file.nt.OptionalHeader.SizeOfImage && n<=file.nt.OptionalHeader.SizeOfImage-rva
        && disk_rva(file,rva,expected.data(),static_cast<DWORD>(n)) && copy(base+rva,live.data(),n)
        && !std::memcmp(expected.data(),live.data(),n) && !std::memcmp(expected.data(),text.data(),text.size())
        && !expected[text.size()];
}
bool crt_system_path(const File& file) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.crt_path)return test.crt_path(file);
#endif
    std::array<wchar_t,32768> path{};const auto n=GetSystemDirectoryW(path.data(),static_cast<UINT>(path.size()));
    return n && n+32<path.size() && !wcscat_s(path.data(),path.size(),L"\\ucrtbase.dll")
        && !_wcsicmp(file.path.c_str(),path.data());
}
bool mapped_file_is(Address address,const File& file) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.mapped_file)return test.mapped_file(address,file);
#endif
    std::array<wchar_t,32768> mapped{},pinned{};
    const auto a=K32GetMappedFileNameW(GetCurrentProcess(),reinterpret_cast<void*>(address),mapped.data(),static_cast<DWORD>(mapped.size()));
    const auto b=GetFinalPathNameByHandleW(file.handle,pinned.data(),static_cast<DWORD>(pinned.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_NT);
    return a && a<mapped.size() && b && b<pinned.size() && !_wcsicmp(mapped.data(),pinned.data()) && unchanged(file);
}
bool query_memory(Address address,MEMORY_BASIC_INFORMATION& out) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.query)return test.query(address,out);
#endif
    return VirtualQuery(reinterpret_cast<void*>(address),&out,sizeof(out))==sizeof(out);
}
bool exact_export(const File& file,Address base,std::string_view name,DWORD expected) noexcept {
    const auto& dir=file.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];IMAGE_EXPORT_DIRECTORY exports{};
    if(!dir.VirtualAddress || dir.Size<sizeof(exports) || !disk_live(file,base,dir.VirtualAddress,exports)
        || !exports.NumberOfNames || exports.NumberOfNames>65536 || !exports.NumberOfFunctions || exports.NumberOfFunctions>65536)return false;
    unsigned found=0;
    for(DWORD i=0;i<exports.NumberOfNames;++i){DWORD text=0;
        if(!disk_live(file,base,exports.AddressOfNames+i*4,text))return false;
        if(!disk_text(file,base,text,name))continue;
        WORD ordinal=0;DWORD value=0;
        if(!disk_live(file,base,exports.AddressOfNameOrdinals+i*2,ordinal) || ordinal>=exports.NumberOfFunctions
            || !disk_live(file,base,exports.AddressOfFunctions+ordinal*4,value) || value!=expected
            || (value>=dir.VirtualAddress && value-dir.VirtualAddress<dir.Size))return false;
        ++found;
    }return found==1;
}
// Authenticate the DIR64 pointer declarations and prove the complete SCP source
// has no base relocations. No arbitrary relocation type grants code authority.
bool scp_relocations(const File& file) noexcept {
    const auto& dir=file.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if(!dir.VirtualAddress || !dir.Size || dir.Size>4*1024*1024 || dir.VirtualAddress>=file.nt.OptionalHeader.SizeOfImage
        || dir.Size>file.nt.OptionalHeader.SizeOfImage-dir.VirtualAddress)return false;
    unsigned pointers=0;DWORD offset=0;std::array<WORD,8192> entries{};
    while(offset<dir.Size){IMAGE_BASE_RELOCATION block{};
        if(dir.Size-offset<sizeof(block) || !disk_rva(file,dir.VirtualAddress+offset,&block,sizeof(block))
            || block.SizeOfBlock<sizeof(block) || block.SizeOfBlock>dir.Size-offset || (block.SizeOfBlock&1))return false;
        const DWORD count=(block.SizeOfBlock-sizeof(block))/2;
        if(count>entries.size() || (count && !disk_rva(file,dir.VirtualAddress+offset+sizeof(block),entries.data(),count*2)))return false;
        for(DWORD i=0;i<count;++i){const auto type=entries[i]>>12;const DWORD slot=entries[i]&0xfff;
            if(!type)continue;if(block.VirtualAddress>MAXDWORD-slot)return false;const DWORD rva=block.VirtualAddress+slot;
            if(rva<kScpEnd && (rva>=kScpSource || kScpSource-rva<8))return false;
            if(rva==kScpExports || rva==kScpExportsEnd){if(type!=IMAGE_REL_BASED_DIR64)return false;const unsigned bit=rva==kScpExports?1:2;
                if(pointers&bit)return false;pointers|=bit;}
        }offset+=block.SizeOfBlock;
    }return offset==dir.Size && pointers==3;
}
bool scp_np_dispatch(Scope& s,Address crt,const File& file,const std::array<unsigned char,16>& stub,BootstrapFrameFailure facts) noexcept {
    auto fail=[&](const char* why){return frame_reject(facts,why);};
    static_assert(offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64,DynamicValueRelocTableOffset)==0xe0);
    static_assert(offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64,DynamicValueRelocTableSection)==0xe4);
    const auto& cfg=file.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    DWORD cfg_size=0,table_offset=0,flags=0;WORD section=0;ULONGLONG table_va=1;
    if(cfg.VirtualAddress!=0xfd4f0 || cfg.Size!=0x148 || !disk_live(file,crt,cfg.VirtualAddress,cfg_size) || cfg_size!=0x148
        || !disk_live(file,crt,cfg.VirtualAddress+offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64,DynamicValueRelocTable),table_va) || table_va
        || !disk_live(file,crt,cfg.VirtualAddress+0xe0,table_offset) || table_offset!=0xb28
        || !disk_live(file,crt,cfg.VirtualAddress+0xe4,section) || section!=8
        || !disk_live(file,crt,cfg.VirtualAddress+offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64,GuardFlags),flags) || flags!=0x10417500
        || file.nt.FileHeader.NumberOfSections<section)return fail("scp_dvrt_config");
    const auto& reloc=file.sections[section-1];
    if(std::memcmp(reloc.Name,".reloc\0\0",8) || reloc.VirtualAddress!=kDvrtSection || table_offset>reloc.Misc.VirtualSize
        || 0x398>reloc.Misc.VirtualSize-table_offset)return fail("scp_dvrt_section");
    std::array<unsigned char,0x398> table{},table_live{};
    if(!disk_rva(file,reloc.VirtualAddress+table_offset,table.data(),static_cast<DWORD>(table.size()))
        || !copy(crt+reloc.VirtualAddress+table_offset,table_live.data(),table_live.size()) || table!=table_live)return fail("scp_dvrt_bytes");
    DWORD version=0,size=0,payload=0,info=0;ULONGLONG symbol=0;
    std::memcpy(&version,table.data(),4);std::memcpy(&size,table.data()+4,4);std::memcpy(&symbol,table.data()+8,8);
    std::memcpy(&payload,table.data()+16,4);std::memcpy(&info,table.data()+20,4);
    constexpr std::array<unsigned char,28> selected={0xa0,0xd6,0x0e,0,0,0,0,0,4,0,0,0,0x0c,0,0,0,0xa0,0xd6,0x0e,0,0,0x80,0x0f,0,0x0c,0,0,0};
    // Selected function-override record includes the relocation block immediately
    // after its RVA array: exactly X64_REL32 f8011 and one padding word.
    constexpr std::array<unsigned char,4> words={0x11,0x10,0,0};
    if(version!=1 || size!=0x390 || symbol!=7 || payload!=0x384 || info!=0x1c0
        || std::memcmp(table.data()+24,selected.data(),selected.size()) || std::memcmp(table.data()+52,words.data(),words.size()))return fail("scp_dvrt_record");
    std::int32_t displacement=0;std::memcpy(&displacement,stub.data()+1,4);
    const Address next=crt+0xf8015;Address target=0;
    if(displacement>=0){if(next>UINTPTR_MAX-static_cast<unsigned>(displacement))return fail("scp_target_overflow");target=next+displacement;}
    else {const auto distance=static_cast<Address>(-static_cast<std::int64_t>(displacement));if(next<distance)return fail("scp_target_overflow");target=next-distance;}
    if(target<0xc0)return fail("scp_target_overflow");const Address clone=target-0xc0;
    HMODULE nt=nullptr;
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.ntdll)nt=test.ntdll();else
#endif
    nt=GetModuleHandleW(L"ntdll.dll");
    if(!nt || (s.ntdll && nt!=s.ntdll))return fail("scp_ntdll_module");
    if(!s.ntdll){try{if(!open_file(nt,s.ntdll_file))return fail("scp_ntdll_file");}catch(...){return fail("scp_ntdll_file");}s.ntdll=nt;}
    const auto& n=s.ntdll_file;const Address nb=reinterpret_cast<Address>(nt);
    bool path_ok=false;
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.ntdll_path)path_ok=test.ntdll_path(n);else
#endif
    {std::array<wchar_t,32768> path{};const auto count=GetSystemDirectoryW(path.data(),static_cast<UINT>(path.size()));
        path_ok=count && count+32<path.size() && !wcscat_s(path.data(),path.size(),L"\\ntdll.dll") && !_wcsicmp(path.data(),n.path.c_str());}
    if(!path_ok || n.identity.nFileSizeHigh || n.identity.nFileSizeLow!=2522080 || !hash_equal(n.sha,kNtdllSha) || !unchanged(n)
        || !image_matches(nt,n,0x6bdf03ca,kNtdllSize,kNtdllChecksum,n.nt.OptionalHeader.AddressOfEntryPoint,BootstrapImageModule::Ntdll))return fail("scp_ntdll_identity");
    if(!exact_export(n,nb,"RtlpScpCfgNtdllExports",kScpExports) || !exact_export(n,nb,"LdrSystemDllInitBlock",kSystemBlock))return fail("scp_exports");
    bool source_section=false;
    for(unsigned i=0;i<n.nt.FileHeader.NumberOfSections;++i){const auto& sec=n.sections[i];
        if(!std::memcmp(sec.Name,"SCPCFGNP",8))source_section=sec.VirtualAddress==kScpSource && sec.Misc.VirtualSize==688
            && sec.SizeOfRawData>=688 && (sec.Characteristics&IMAGE_SCN_MEM_EXECUTE) && (sec.Characteristics&IMAGE_SCN_MEM_READ) && !(sec.Characteristics&IMAGE_SCN_MEM_WRITE);}
    std::array<unsigned char,688> source{},source_live{},cloned{};
    if(!source_section || !scp_relocations(n) || !disk_rva(n,kScpSource,source.data(),static_cast<DWORD>(source.size()))
        || !copy(nb+kScpSource,source_live.data(),source_live.size()) || source_live!=source)return fail("scp_source");
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> sha{};
    const bool hashed=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
        && BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0 && BCryptHashData(hash,source.data(),static_cast<ULONG>(source.size()),0)>=0
        && BCryptFinishHash(hash,sha.data(),static_cast<ULONG>(sha.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    constexpr std::array<DWORD,6> header={0x40,0xc0,0x140,0x1c0,0x240,0x2a4};
    if(!hashed || !hash_equal(sha,kScpSha) || std::memcmp(source.data(),header.data(),sizeof(header))
        || std::memcmp(source.data()+0xc0,"\x48\xff\xe0",3))return fail("scp_template");
    std::array<ULONGLONG,2> disk_pointers{},pointers{};
    DWORD init_size=0;ULONGLONG published=0,disk_published=1;
    if(!disk_rva(n,kScpExports,disk_pointers.data(),sizeof(disk_pointers)) || disk_pointers!=std::array<ULONGLONG,2>{kScpPreferred,kScpPreferredEnd}
        || !copy(nb+kScpExports,pointers.data(),sizeof(pointers)) || pointers!=std::array<ULONGLONG,2>{nb+kScpSource,nb+kScpEnd}
        || !disk_live(n,nb,kSystemBlock,init_size) || init_size!=0x128 || !disk_rva(n,kSystemPointer,&disk_published,sizeof(disk_published)) || disk_published
        || !read(nb+kSystemPointer,published) || published!=target)return fail("scp_publication");
    Frame dispatch{};BootstrapFrameFailure source_facts=facts;
    if(!lookup(nb+kScpEntry,dispatch) || dispatch.image!=nb || !function(dispatch,n,kScpEntry,kScpFunctionEnd,kScpUnwind,source_facts)
        || !bytes(nb+kScpUnwind,"01000000"))return fail("scp_source_unwind");
    MEMORY_BASIC_INFORMATION region{},again{};
    auto mapping=[&](MEMORY_BASIC_INFORMATION& out){return clone>=nb+kNtdllSize && query_memory(clone,out)
        && out.BaseAddress==reinterpret_cast<void*>(clone) && out.AllocationBase==nt && out.State==MEM_COMMIT
        && out.Type==MEM_IMAGE && out.Protect==PAGE_EXECUTE_READ && out.RegionSize==0x1000
        && mapped_file_is(nb,n) && mapped_file_is(clone,n);};
    if(!mapping(region) || !copy(clone,cloned.data(),cloned.size()) || cloned!=source)return fail("scp_clone");
    // Recheck every mutable witness before returning; never rebase a proof after
    // a drift or infer identity from the JMP target's proximity to ntdll.
    std::array<unsigned char,688> reread{};std::array<unsigned char,16> stub_again{};
    std::array<unsigned char,0x398> table_again{};std::array<ULONGLONG,2> pointers_again{};ULONGLONG published_again=0,va_again=1;
    DWORD size_again=0,offset_again=0,flags_again=0,block_again=0;WORD section_again=0;
    if(!copy(clone,reread.data(),reread.size()) || reread!=source || !copy(nb+kScpSource,reread.data(),reread.size()) || reread!=source
        || !copy(crt+0xf8010,stub_again.data(),stub_again.size()) || stub_again!=stub
        || !copy(crt+kDvrtTable,table_again.data(),table_again.size()) || table_again!=table
        || !copy(nb+kScpExports,pointers_again.data(),sizeof(pointers_again)) || pointers_again!=pointers
        || !read(nb+kSystemPointer,published_again) || published_again!=published || !read(nb+kSystemBlock,block_again) || block_again!=init_size
        || !read(crt+cfg.VirtualAddress,size_again) || size_again!=cfg_size
        || !read(crt+cfg.VirtualAddress+offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64,DynamicValueRelocTable),va_again) || va_again!=table_va
        || !read(crt+cfg.VirtualAddress+0xe0,offset_again) || offset_again!=table_offset
        || !read(crt+cfg.VirtualAddress+0xe4,section_again) || section_again!=section
        || !read(crt+cfg.VirtualAddress+offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64,GuardFlags),flags_again) || flags_again!=flags || !mapping(again)
        || again.BaseAddress!=region.BaseAddress || again.RegionSize!=region.RegionSize || !unchanged(file) || !unchanged(n))return fail("scp_reread");
    return true;
}
bool crt_code_matches(Address base,const File& file,BootstrapFrameFailure facts,Scope* current=nullptr) noexcept {
    facts.expected_module=base;facts.observed_module=base;
    for(const auto& window:crt_windows) {
        const auto hex=window.rva==0xf8010?std::string_view("e98b56ffffcccccccccccccccccccccc"):window.hex;
        std::array<unsigned char,128> live{},disk{};const auto size=hex.size()/2;
        const auto fail=[&](const char* predicate,const char* input,unsigned offset,unsigned expected,unsigned observed,bool byte=true) {
            facts.byte_offset=offset;facts.expected_byte=byte?expected:0;facts.observed_byte=byte?observed:0;
            // CodeWindow offset is the window RVA; index is its first failing byte.
            // Both projections use these validation copies, never diagnostic rereads.
            image_reject(BootstrapImageModule::Crt,BootstrapImageStage::CodeWindow,input,
                expected,observed,static_cast<unsigned>(window.rva),offset);
            return frame_reject(facts,predicate);
        };
        if(!copy(base+window.rva,live.data(),size))return fail("srw_crt_live_read","live_read",0,static_cast<unsigned>(size),0,false);
        if(!disk_rva(file,static_cast<DWORD>(window.rva),disk.data(),static_cast<DWORD>(size)))
            return fail("srw_crt_disk_read","disk_read",0,static_cast<unsigned>(size),0,false);
        for(unsigned i=0;i<size;++i) {
            const auto expected=static_cast<unsigned>(nibble(hex[i*2])*16+nibble(hex[i*2+1]));
            if(disk[i]!=expected)return fail("srw_crt_disk_code","disk_byte",i,expected,disk[i]);
        }
        if(window.rva==0xf8010 && std::memcmp(live.data(),disk.data(),size)) {
            for(unsigned i=0;i<size;++i)if((i<1 || i>4) && live[i]!=disk[i])return fail("srw_crt_code","live_byte",i,disk[i],live[i]);
            std::array<unsigned char,16> stub{};std::copy_n(live.begin(),stub.size(),stub.begin());
            if(current){auto variant_facts=facts;unsigned first=1;while(first<5 && live[first]==disk[first])++first;
                variant_facts.byte_offset=first;variant_facts.expected_byte=disk[first];variant_facts.observed_byte=live[first];
                if(!scp_np_dispatch(*current,base,file,stub,variant_facts)) {
                    image_reject(BootstrapImageModule::Crt,BootstrapImageStage::CodeWindow,"live_byte",disk[first],live[first],static_cast<unsigned>(window.rva),first);
                    return false;
                }}
            else {for(unsigned i=0;i<size;++i)if(live[i]!=disk[i])return fail("srw_crt_code","live_byte",i,disk[i],live[i]);}
        } else for(unsigned i=0;i<size;++i)if(live[i]!=disk[i])return fail("srw_crt_code","live_byte",i,disk[i],live[i]);
    }
    return true;
}
bool srw_contract(Scope& s,BootstrapFrameFailure facts={}) noexcept {
    const auto exe=reinterpret_cast<Address>(s.exe);Address target=0;
    facts.expected_module=exe;
    auto fail=[&](const char* why){return frame_reject(facts,why);};
    if constexpr(rva::NativeBootstrapSrwHeader==0) return fail("srw_build");
    else {
    if(!read(exe+rva::NativeBootstrapSrwInitIat,target))return fail("srw_import_read");
    const auto crt=module_at(target);facts.observed_module=reinterpret_cast<Address>(crt);
    if(!crt || crt==s.exe || crt==s.asi || crt==s.loader || (s.crt && s.crt!=crt))return fail("srw_crt_module");
    if(!s.crt) {
        try{if(!open_file(crt,s.crt_file))return fail("srw_crt_file");}catch(...){return fail("srw_crt_file");}
        s.crt=crt;
    }
    const auto base=reinterpret_cast<Address>(crt);const auto& file=s.crt_file;
    const auto size=(static_cast<std::uint64_t>(file.identity.nFileSizeHigh)<<32)|file.identity.nFileSizeLow;
    if(size!=1377512 || !hash_equal(file.sha,kCrtSha) || !crt_system_path(file) || !unchanged(file))return fail("srw_crt_identity");
    if(!image_matches(crt,file,0x2bbefd73,kCrtImageSize,kCrtChecksum,file.nt.OptionalHeader.AddressOfEntryPoint,BootstrapImageModule::Crt))
        return fail("srw_crt_image");
    // Authenticate the actual API-set import/name and resolved non-forwarded export,
    // not a DLL basename, an IAT equality alone, or a guessed OS-wide function RVA.
    const auto imports=s.executable.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!imports.VirtualAddress || imports.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR) || imports.Size>65536)return fail("srw_import_directory");
    bool imported=false;
    for(unsigned i=0;i<128 && (i+1)*sizeof(IMAGE_IMPORT_DESCRIPTOR)<=imports.Size;++i) {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if(!disk_live(s.executable,exe,imports.VirtualAddress+i*sizeof(descriptor),descriptor))return fail("srw_import_descriptor");
        if(!descriptor.Name)break;
        if(!disk_text(s.executable,exe,descriptor.Name,"api-ms-win-crt-runtime-l1-1-0.dll"))continue;
        if(!descriptor.OriginalFirstThunk || rva::NativeBootstrapSrwInitIat<descriptor.FirstThunk
            || (rva::NativeBootstrapSrwInitIat-descriptor.FirstThunk)%8
            || (rva::NativeBootstrapSrwInitIat-descriptor.FirstThunk)/8>=8192)return fail("srw_import_thunk_bounds");
        std::uint64_t name=0;const auto offset=rva::NativeBootstrapSrwInitIat-descriptor.FirstThunk;
        if(!disk_live(s.executable,exe,descriptor.OriginalFirstThunk+static_cast<DWORD>(offset),name)
            || name!=rva::NativeBootstrapSrwInitName
            || !disk_text(s.executable,exe,static_cast<DWORD>(name)+2,"_initterm"))return fail("srw_import_name");
        if(imported)return fail("srw_import_duplicate");imported=true;
    }
    if(!imported)return fail("srw_import_missing");
    const auto exports=file.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];IMAGE_EXPORT_DIRECTORY directory{};
    if(!exports.VirtualAddress || !exports.Size || exports.VirtualAddress>file.nt.OptionalHeader.SizeOfImage
        || exports.Size>file.nt.OptionalHeader.SizeOfImage-exports.VirtualAddress
        || !disk_live(file,base,exports.VirtualAddress,directory) || !directory.NumberOfNames || directory.NumberOfNames>65536
        || !directory.NumberOfFunctions || directory.NumberOfFunctions>65536)return fail("srw_export_directory");
    DWORD exported=0;
    for(DWORD i=0;i<directory.NumberOfNames;++i) {
        DWORD name=0;if(!disk_live(file,base,directory.AddressOfNames+i*4,name))return fail("srw_export_name_table");
        if(!disk_text(file,base,name,"_initterm"))continue;
        WORD ordinal=0;DWORD value=0;
        if(!disk_live(file,base,directory.AddressOfNameOrdinals+i*2,ordinal) || ordinal>=directory.NumberOfFunctions
            || !disk_live(file,base,directory.AddressOfFunctions+ordinal*4,value) || exported
            || (value>=exports.VirtualAddress && value-exports.VirtualAddress<exports.Size))return fail("srw_export_forwarder_or_duplicate");
        exported=value;
    }
    facts.expected_register=base+0x707a0;facts.observed_register=target;
    if(exported!=0x707a0 || target!=base+exported)return fail("srw_export_target");
    if(!crt_code_matches(base,file,facts,&s))return false;
    for(const auto& row:std::array<RUNTIME_FUNCTION,2>{{{0xed6a0,0xed6a6,{0x1243b0}},{0xed6c0,0xed6c2,{0x124a18}}}}) {
        Frame f{};if(!lookup(base+row.BeginAddress,f) || f.image!=base
            || !function(f,file,row.BeginAddress,row.EndAddress,row.UnwindData,facts)
            || !bytes(base+row.UnwindData,"01000000"))return fail("srw_dispatch_unwind");
    }
    RUNTIME_FUNCTION no_frame{};if(disk_function(file,0xf8010,no_frame))return fail("srw_dispatch_extra_function");
    const auto config=file.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    Address cell=0,disk_cell=0,dispatch=0,original=0;
    if(config.Size<0x80 || !disk_rva(file,config.VirtualAddress+0x78,&disk_cell,8)
        || disk_cell!=file.nt.OptionalHeader.ImageBase+0xfdba8
        || !read(base+config.VirtualAddress+0x78,cell) || cell!=base+0xfdba8
        || !disk_rva(file,0xfdba8,&original,8) || original!=file.nt.OptionalHeader.ImageBase+0xed6c0
        || !read(base+0xfdba8,dispatch) || dispatch!=base+0xed6c0)return fail("srw_cfg_dispatch");
    for(auto id:srw_windows) {
        const auto* spec=find_rva_signature(id);if(!spec || !spec->rva || spec->expected_prologue.empty())return fail("srw_game_spec");
        std::array<unsigned char,128> live{};
        if(spec->expected_prologue.size()>live.size() || !copy(exe+spec->rva,live.data(),spec->expected_prologue.size())
            || std::memcmp(live.data(),spec->expected_prologue.data(),spec->expected_prologue.size()))return fail("srw_game_code");
    }
    const std::array<Address,4> prefix={0,exe+rva::NativeBootstrapSrwCrtSetup,exe+rva::NativeBootstrapSrwAtexit,exe+rva::NativeBootstrapSrwFirstName};
    std::array<Address,4> actual{};
    if(!read(exe+rva::NativeBootstrapSrwInitBegin,actual) || actual!=prefix)return fail("srw_callback_table");
    return true;
    }
}
// All inputs here are copied/authenticated before the OS unwinder can follow
// metadata. SEH contains malformed/unavailable current-stack reconstruction.
bool unwind_step(CONTEXT& context,Address image,RUNTIME_FUNCTION& function) noexcept {
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.unwind)return test.unwind(context,image,function);
#endif
    __try {void* handler=nullptr;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,&function,&context,&handler,&frame,nullptr);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool stack_bounds(Address& low,Address& high) noexcept {
    const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    low=reinterpret_cast<Address>(tib->StackLimit);high=reinterpret_cast<Address>(tib->StackBase);
    return true;
}
unsigned virtual_frames(Scope& s,std::array<Frame,kFrames>& out) noexcept {
    CONTEXT context{};Address low=0,high=0;
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
    if(test.context){if(!test.context(context,low,high))return 0;}else
#endif
    {if(!stack_bounds(low,high))return 0;RtlCaptureContext(&context);}
    constexpr DWORD required=CONTEXT_CONTROL|CONTEXT_INTEGER;
    if(!low || low>=high || (context.ContextFlags&required)!=required)return 0;
    for(unsigned i=0;i<out.size();++i) {
        auto& f=out[i];BootstrapFrameFailure facts{};facts.index=i;
        if(context.Rsp<low || context.Rsp>=high || (context.Rsp&7)) {
            frame_reject(facts,"srw_context_lookup_bounds");return 0;
        }
        const auto image=module_at(context.Rip);facts.observed_module=reinterpret_cast<Address>(image);
        const File* file=image==s.asi?&s.self:image==s.loader?&s.proxy:image==s.exe?&s.executable:image==s.crt?&s.crt_file:nullptr;
        if(!image || !file) {frame_reject(facts,"srw_context_module");return 0;}
        if(!lookup(context.Rip,f) || f.image!=reinterpret_cast<Address>(image)) {frame_reject(facts,"srw_context_lookup");return 0;}
        f.rbx=context.Rbx;f.rdi=context.Rdi;
        RUNTIME_FUNCTION primary{};
        if(!primary_function(f,*file,primary,facts))return 0;
        if(f.image==reinterpret_cast<Address>(s.exe) && f.pc-f.image==rva::NativeBootstrapSrwInittermCall+19)return i+1;
        const auto before=context.Rsp;
        if(!unwind_step(context,f.image,f.function) || context.Rsp<=before || context.Rsp>high || !context.Rip) {
            frame_reject(facts,"srw_virtual_unwind");return 0;
        }
    }
    return 0;
}
void project_following(BootstrapFrameFailure& facts,const std::array<Frame,kFrames>& frames,
    unsigned count,unsigned index,const Scope& s) noexcept {
    facts.following_count=0;facts.following={};
    count=(std::min)(count,static_cast<unsigned>(frames.size()));
    if(index>=count)return;
    for(unsigned j=0;j<facts.following.size() && j<count-index-1;++j) {
        const auto& frame=frames[index+1+j];auto& out=facts.following[j];
        if(frame.image && frame.image==reinterpret_cast<Address>(s.exe))out.module=BootstrapFrameModule::Executable;
        else if(frame.image && frame.image==reinterpret_cast<Address>(s.loader))out.module=BootstrapFrameModule::Loader;
        else if(frame.image && frame.image==reinterpret_cast<Address>(s.asi))out.module=BootstrapFrameModule::OwnAsi;
        out.return_rva=frame.image && frame.pc>=frame.image?frame.pc-frame.image:0;
        out.function=record(frame.function);++facts.following_count;
    }
}
bool frames(Scope& s) noexcept {
    std::array<Frame,kFrames> f{};const unsigned n=capture(f.data(),static_cast<unsigned>(f.size()));diagnostic_frames.store(n);
    if(!n || n>=f.size())return reject(BootstrapFailure::Frames);
    const auto asi=reinterpret_cast<Address>(s.asi),loader=reinterpret_cast<Address>(s.loader),exe=reinterpret_cast<Address>(s.exe);
    unsigned i=0,exports=0;Frame own{};RUNTIME_FUNCTION own_function{};BootstrapFrameFailure facts{};
    facts.supplied_caller_return_rva=s.caller>=loader?s.caller-loader:0;
    project_following(facts,f,n,0,s);
    auto failed_frame=[&](unsigned index,Address expected,const char* predicate="continuation"){
        diagnostic_frame.store(index);diagnostic_expected.store(expected);
        diagnostic_observed.store(index<n && f[index].pc>=f[index].image?f[index].pc-f[index].image:0);
        facts.index=index;facts.export_count=exports;
        project_following(facts,f,n,index,s);
        if(index<n)facts.observed_module=f[index].image;
        frame_reject(facts,predicate);
        return reject(BootstrapFailure::Frames);
    };
    facts.expected_module=asi;
    if(!lookup(s.export_target,own)){frame_reject(facts,"export_lookup");return reject(BootstrapFailure::Export);}
    facts.observed_module=own.image;facts.export_entry=record(own.function);
    if(own.image!=asi || s.export_target-asi<own.function.BeginAddress || s.export_target-asi>=own.function.EndAddress) {
        frame_reject(facts,"export_lookup_module_range");return reject(BootstrapFailure::Export);
    }
    if(!primary_function(own,s.self,own_function,facts))return reject(BootstrapFailure::Export);
    facts.export_primary=record(own_function);
    for(;i<n && f[i].image==asi;++i) {
        facts.index=i;facts.observed_module=f[i].image;facts.last_own=record(f[i].function);
        project_following(facts,f,n,i,s);
        if(module_at(f[i].pc)!=s.asi || f[i].pc-asi<f[i].function.BeginAddress || f[i].pc-asi>=f[i].function.EndAddress)
            {frame_reject(facts,"own_frame_module_range");return reject(BootstrapFailure::Unwind);}
        RUNTIME_FUNCTION primary{};
        if(!primary_function(f[i],s.self,primary,facts))return failed_frame(i,f[i].pc-asi,"own_primary");
        facts.last_own_primary=record(primary);
        if(!std::memcmp(&primary,&own_function,sizeof(own_function)))++exports;
    }
    facts.expected_module=loader;facts.index=i;facts.export_count=exports;
    project_following(facts,f,n,i,s);
    if(exports!=1)return failed_frame(i,0x21681,"export_cardinality");
    if(!i || i+5>=n)return failed_frame(i,0x21681,"loader_frame_count");
    if(f[i].pc!=s.caller)return failed_frame(i,0x21681,"export_caller");
    if(f[i].image!=loader || f[i].pc-loader!=0x21681 || module_at(f[i].pc)!=s.loader)
        return failed_frame(i,0x21681,"loader_module_return");
    facts.observed_module=f[i].image;
    if(!function(f[i],s.proxy,0x20ea0,0x217cb,0x9f794,facts))return failed_frame(i,0x21681,"loader_function");
    auto loader_frame=[&](Address pc,DWORD begin,DWORD end,DWORD unwind) {
        facts.index=i;facts.expected_module=loader;facts.observed_module=i<n?f[i].image:0;
        project_following(facts,f,n,i,s);
        if(i>=n || f[i].image!=loader || f[i].pc!=loader+pc || module_at(f[i].pc)!=s.loader
            || !function(f[i],s.proxy,begin,end,unwind,facts)){failed_frame(i,pc);return false;}++i;return true;
    };
    ++i;
    if(f[i].pc==loader+0x21b64 || f[i].pc==loader+0x21c44) {
        const auto pc=f[i].pc-loader;if(!loader_frame(pc,0x217d0,0x21c8b,0x9f848))return reject(BootstrapFailure::Frames);
        const auto parent=f[i].pc-loader;
        if(parent!=0x30b8c && parent!=0x30c4c && parent!=0x30d38)return failed_frame(i,0,"directory_parent");
        if(!loader_frame(parent,0x30780,0x30f3b,0x9f938))return reject(BootstrapFailure::Frames);
    } else if(!loader_frame(0x30ad8,0x30780,0x30f3b,0x9f938))return reject(BootstrapFailure::Frames);
    if(!loader_frame(0x330a3,0x30f40,0x330e6,0x9fa64))return reject(BootstrapFailure::Frames);
    BootstrapSource source=BootstrapSource::None;
    if(f[i].pc==loader+0x1eead) {
        if(!loader_frame(0x1eead,0x1ee80,0x1eebc,0x9c120))return reject(BootstrapFailure::Frames);
        facts.index=i;
        if(!srw_contract(s,facts))return failed_frame(i,0,"srw_contract");
        // A second, native-context unwind reconstructs nonvolatiles; a list of
        // return PCs alone cannot authenticate a CRT callback or static guard.
        std::array<Frame,kFrames> recovered{};const auto count=virtual_frames(s,recovered);
        unsigned first=0;while(first<count && recovered[first].image==asi)++first;
        unsigned original=0;while(original<n && f[original].image==asi)++original;
        if(!count || first==count)return failed_frame(i,0,"srw_context_unavailable");
        for(unsigned a=first,b=original;a<count;++a,++b) {
            if(b>=n || recovered[a].pc!=f[b].pc || recovered[a].image!=f[b].image
                || std::memcmp(&recovered[a].function,&f[b].function,sizeof(RUNTIME_FUNCTION)))
                return failed_frame(b,0,"srw_context_stack_mismatch");
        }
        const auto ri=first+i-original;
        if(i+9>n || ri+9!=count)return failed_frame(i,0,"srw_exact_frame_count");
        struct Expected {Address pc,begin;DWORD extent;};
        constexpr bool older=std::string_view(core::generated::kBuildId)=="ff7rebirth-steam-win64-68fd6fde";
        // Independent old imported-function/call contract, not a displacement of
        // the newer ranges: its FName inner call/extent and constructor call differ.
        const std::array<Expected,9> chain=older?std::array<Expected,9>{{
             {rva::NativeBootstrapSrwHeader+22,rva::NativeBootstrapSrwHeader,0x7b},
             {rva::NativeBootstrapSrwGuardCall+12,rva::NativeBootstrapSrwGuardCall-0x29,0x54},
             {rva::NativeBootstrapSrwMallocSecondary+5,rva::NativeBootstrapSrwMallocSecondary,0x30},
             {rva::NativeBootstrapSrwPoolCall+18,rva::NativeBootstrapSrwPoolCall-0x3d,0x298c},
             {rva::NativeBootstrapSrwPoolSecondary+12,rva::NativeBootstrapSrwPoolSecondary,0x18},
             {rva::NativeBootstrapSrwBlockCall+5,rva::NativeBootstrapSrwBlockCall-0x60,0x22b},
             {rva::NativeBootstrapSrwNameCall+26,rva::NativeBootstrapSrwNameDestination-0x35,0x220},
             {0x707d6,0x707a0,0x38},
             {rva::NativeBootstrapSrwInittermCall+19,rva::NativeBootstrapCrt,0x171}}}:
            std::array<Expected,9>{{{rva::NativeBootstrapSrwHeader+22,rva::NativeBootstrapSrwHeader,0x7b},
            {rva::NativeBootstrapSrwGuardCall+12,rva::NativeBootstrapSrwGuardCall-0x29,0x54},
            {rva::NativeBootstrapSrwMallocSecondary+5,rva::NativeBootstrapSrwMallocSecondary,0x30},
            {rva::NativeBootstrapSrwPoolCall+18,rva::NativeBootstrapSrwPoolCall-0x3d,0x298c},
            {rva::NativeBootstrapSrwPoolSecondary+12,rva::NativeBootstrapSrwPoolSecondary,0x18},
            {rva::NativeBootstrapSrwBlockCall+14,rva::NativeBootstrapSrwBlockCall-0x53,0x32b},
            {rva::NativeBootstrapSrwNameCall+14,rva::NativeBootstrapSrwNameDestination-0x35,0x220},
            {0x707d6,0x707a0,0x38},
            {rva::NativeBootstrapSrwInittermCall+19,rva::NativeBootstrapCrt,0x171}}};
        for(unsigned j=0;j<chain.size();++j) {
            const auto& expected=chain[j];const auto& actual=f[i+j];const bool crt=j==7;
            const auto image=crt?reinterpret_cast<Address>(s.crt):exe;const auto& file=crt?s.crt_file:s.executable;
            facts.index=i+j;facts.expected_module=image;facts.observed_module=actual.image;project_following(facts,f,n,i+j,s);
            if(actual.image!=image || actual.pc!=image+expected.pc || module_at(actual.pc)!=reinterpret_cast<HMODULE>(image)
                || !function(actual,file,static_cast<DWORD>(expected.begin),static_cast<DWORD>(expected.begin+expected.extent),crt?0x11bc3c:0,facts))
                return failed_frame(i+j,expected.pc,"srw_continuation");
            RUNTIME_FUNCTION primary{};
            if(!primary_function(actual,file,primary,facts))return failed_frame(i+j,expected.pc,"srw_chain");
            Address parent=expected.begin;DWORD extent=expected.extent;
            if(j==2){parent=rva::NativeBootstrapSrwMallocCode-0x18;extent=0xff;}
            if(j==4){const auto* call=find_rva_signature("native_bootstrap_srw_block_call");std::int32_t displacement=0;
                const std::size_t operand=older?1:10, return_offset=older?5:14;
                if(!call || call->expected_prologue.size()<operand+4)return failed_frame(i+j,expected.pc,"srw_parent_call");
                std::memcpy(&displacement,call->expected_prologue.data()+operand,4);
                parent=rva::NativeBootstrapSrwBlockCall+return_offset+displacement;extent=0x26;}
            if(primary.BeginAddress!=parent || primary.EndAddress!=parent+extent)return failed_frame(i+j,expected.pc,"srw_primary");
            if(crt && !bytes(image+0x11bc3c,"010f04000f3406000f320b70"))return failed_frame(i+j,expected.pc,"srw_crt_unwind");
            auto reg=[&](Address wanted,Address value,const char* label){facts.expected_register=wanted;facts.observed_register=value;
                if(value==wanted)return true;failed_frame(i+j,expected.pc,label);return false;};
            if(j==0 && !reg(exe+rva::NativeBootstrapSrwGuard,recovered[ri+j].rbx,"srw_guard_rbx"))return false;
            if(j==3 && !reg(exe+rva::NativeBootstrapSrwPool,recovered[ri+j].rdi,"srw_pool_rdi"))return false;
            if(j==6 && !reg(exe+rva::NativeBootstrapSrwOutput,recovered[ri+j].rbx,"srw_output_rbx"))return false;
            if(j==7 && (!reg(exe+rva::NativeBootstrapSrwInitBegin+24,recovered[ri+j].rbx,"srw_crt_cursor_rbx")
                || !reg(exe+rva::NativeBootstrapSrwInitEnd,recovered[ri+j].rdi,"srw_crt_end_rdi")))return false;
        }
        i+=9;source=BootstrapSource::FirstSrw;
    } else if(f[i].pc==loader+0x1fa6d) {
        if(!loader_frame(0x1fa6d,0x1fa40,0x1fa7c,0x9c120))return reject(BootstrapFailure::Frames);
        source=BootstrapSource::PreWinMain;
    } else {
        if(!loader_frame(0x1faed,0x1fac0,0x1fafc,0x9c120))return reject(BootstrapFailure::Frames);
        source=BootstrapSource::Cookie;
    }
    if(source!=BootstrapSource::FirstSrw) {
    if(i+2>n)return failed_frame(i,0,"source_frame_count");
    const Address current=source==BootstrapSource::Cookie?rva::NativeBootstrapCookieTime+6:rva::NativeBootstrapShow+0x22;
    const Address parent=source==BootstrapSource::Cookie?rva::NativeAudioExeEntry+9:rva::NativeBootstrapShowToWinmain+5;
    const DWORD begin=static_cast<DWORD>(source==BootstrapSource::Cookie?rva::NativeBootstrapCookie:rva::NativeBootstrapShow);
    const DWORD extent=source==BootstrapSource::Cookie?0xaf:0x3a;
    const DWORD outer=static_cast<DWORD>(source==BootstrapSource::Cookie?rva::NativeAudioExeEntry:rva::NativeBootstrapCrt);
    const DWORD outer_extent=source==BootstrapSource::Cookie?0x12:0x171;
    facts.index=i;facts.expected_module=exe;facts.observed_module=f[i].image;
    project_following(facts,f,n,i,s);
    if(f[i].image!=exe || f[i].pc!=exe+current || module_at(f[i].pc)!=s.exe
        || !function(f[i],s.executable,begin,begin+extent,0,facts))return failed_frame(i,current);
    facts.index=i+1;facts.observed_module=f[i+1].image;
    project_following(facts,f,n,i+1,s);
    if(f[i+1].image!=exe || f[i+1].pc!=exe+parent || module_at(f[i+1].pc)!=s.exe
        || !function(f[i+1],s.executable,outer,outer+outer_extent,0,facts))return failed_frame(i+1,parent);
    i+=2;
    }
    for(;i<n;++i)if(module_at(f[i].pc)!=reinterpret_cast<HMODULE>(f[i].image)
        || !system_module(reinterpret_cast<HMODULE>(f[i].image)))return failed_frame(i,0,"system_suffix");
    if(s.source!=BootstrapSource::None && s.source!=source)return failed_frame(i,0,"source_changed");
    s.source=source;diagnostic_source.store(source);return true;
}
bool validate(Scope& s) noexcept {
    if(diagnostic_failure.load(std::memory_order_acquire)!=BootstrapFailure::None)return false;
    if(thread()!=s.owner_thread)return reject(BootstrapFailure::Thread);
    if(own_export(s.asi)!=s.export_target || module_at(s.caller)!=s.loader || module_at(s.export_target)!=s.asi
        || module_at(reinterpret_cast<Address>(&try_synchronous_native_audio_startup))!=s.asi)return reject(BootstrapFailure::Image);
    if(!unchanged(s.executable) || !unchanged(s.proxy) || !unchanged(s.self) || (s.crt && !unchanged(s.crt_file)))return reject(BootstrapFailure::Files);
    if(!image_matches(s.exe,s.executable,core::generated::kExeTimestamp,core::generated::kSizeOfImage,
        core::generated::kPeChecksum,static_cast<DWORD>(rva::NativeAudioExeEntry),BootstrapImageModule::Executable)
        || !image_matches(s.loader,s.proxy,0x6a1ee839,0x12a000,0x37d79f,0x4e850,BootstrapImageModule::Loader)
        || !image_matches(s.asi,s.self,s.self.nt.FileHeader.TimeDateStamp,s.self.nt.OptionalHeader.SizeOfImage,
            s.self.nt.OptionalHeader.CheckSum,s.self.nt.OptionalHeader.AddressOfEntryPoint,BootstrapImageModule::OwnAsi))return reject(BootstrapFailure::Image);
    if(!code(s))return reject(BootstrapFailure::Code);
    if(!frames(s))return false;
    // UAL restores the EXE IAT before enumeration. The retained real wrapper
    // frame and original CALL bytes, not a still-patched IAT, prove this edge.
    if(!baseline(s) || (s.source==BootstrapSource::FirstSrw && !srw_contract(s)) || !baseline(s))return reject(BootstrapFailure::Baseline);
    return true;
}
}
BootstrapSnapshot native_audio_bootstrap_status() noexcept {
    using namespace bootstrap_impl;
    return {diagnostic_source.load(),diagnostic_failure.load(),diagnostic_thread.load(),diagnostic_frames.load(),
        diagnostic_invoked.load(),diagnostic_published.load(),diagnostic_frame.load(),diagnostic_observed.load(),diagnostic_expected.load(),
        image_failure_ready.load(std::memory_order_acquire)?image_failure:BootstrapImageFailure{},
        frame_failure_ready.load(std::memory_order_acquire)?frame_failure:BootstrapFrameFailure{}};
}
bool try_synchronous_native_audio_startup(HMODULE exe,HMODULE asi,std::uintptr_t caller,NativeStartupCallback initialize) noexcept {
    using namespace bootstrap_impl;
    if(attempted.test_and_set(std::memory_order_acq_rel))return reject(BootstrapFailure::Reused);
    if(scope || !exe || !asi || exe==asi || !caller || !initialize)return reject(BootstrapFailure::Arguments);
    if((std::string_view(core::generated::kBuildId)!="ff7rebirth-steam-win64-6a16ced2"
        && std::string_view(core::generated::kBuildId)!="ff7rebirth-steam-win64-68fd6fde") || !rva::NativeBootstrapShow)
        return reject(BootstrapFailure::Build);
    try {
        HMODULE actual_exe=nullptr;
#ifdef FF7RP_NATIVE_AUDIO_BOOTSTRAP_SELFTEST
        if(test.exe)actual_exe=test.exe();else
#endif
        actual_exe=GetModuleHandleW(nullptr);
        if(actual_exe!=exe)return reject(BootstrapFailure::Image);
        Scope current{};current.exe=exe;current.asi=asi;current.caller=caller;current.loader=module_at(caller);
        current.owner_thread=thread();diagnostic_thread.store(current.owner_thread);
        current.export_target=own_export(asi);
        if(!current.loader || current.loader==asi || current.loader==exe || !current.export_target)return reject(BootstrapFailure::Export);
        if(!open_file(exe,current.executable) || !open_file(current.loader,current.proxy) || !open_file(asi,current.self))return reject(BootstrapFailure::Files);
        const auto size=[](const File& f){return(static_cast<std::uint64_t>(f.identity.nFileSizeHigh)<<32)|f.identity.nFileSizeLow;};
        const auto slash=current.executable.path.find_last_of(L"\\/");
        const auto proxy_slash=current.proxy.path.find_last_of(L"\\/");
        // The caller module supplies its actual name; identity and handoff code authenticate the loader.
        if(slash==std::wstring::npos || proxy_slash==std::wstring::npos
            || _wcsicmp(current.proxy.path.substr(0,proxy_slash+1).c_str(),current.executable.path.substr(0,slash+1).c_str())
            || size(current.executable)!=core::generated::kExeFileSize || size(current.proxy)!=3615648
            || !hash_equal(current.executable.sha,core::generated::kExeSha256) || !hash_equal(current.proxy.sha,kLoaderSha))return reject(BootstrapFailure::Files);
        if(!validate(current))return false;
        scope=&current;struct Reset {~Reset(){scope=nullptr;}} reset;
        diagnostic_invoked.store(true,std::memory_order_release);initialize();return true;
    }catch(...){return reject(BootstrapFailure::Exception);}
}
namespace bootstrap_detail {
bool scope_present() noexcept {return bootstrap_impl::scope!=nullptr;}
bool begin_publication(HMODULE exe) noexcept {
    using namespace bootstrap_impl;auto* s=scope;
    if(!s || s->exe!=exe || s->publication!=Publication::Fresh)return reject(BootstrapFailure::Scope);
    if(!validate(*s)){s->publication=Publication::Failed;return false;}
    s->publication=Publication::Installing;return true;
}
bool finish_publication(HMODULE exe) noexcept {
    using namespace bootstrap_impl;auto* s=scope;
    if(!s || s->exe!=exe || s->publication!=Publication::Installing)return reject(BootstrapFailure::Scope);
    if(!validate(*s)){s->publication=Publication::Failed;return false;}
    s->publication=Publication::Published;diagnostic_published.store(true,std::memory_order_release);return true;
}
void abandon_publication(HMODULE exe) noexcept {
    using namespace bootstrap_impl;auto* s=scope;
    if(!s || s->exe!=exe || s->owner_thread!=thread())return;
    s->publication=Publication::Failed;diagnostic_published.store(false,std::memory_order_release);
    reject(BootstrapFailure::Publication);
}
}
}
