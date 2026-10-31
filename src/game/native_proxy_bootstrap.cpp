#include "game/native_proxy_bootstrap.h"
#include "game/native_bootstrap_baseline.h"
#include "game/hook_specs.h"
#ifdef FF7RP_OWNED_PROXY
#include "proxy/call_bridge.h"
extern "C" void ff7rp_proxy_call_dispatch_return();
#endif
#ifdef FF7RP_OWNED_PROXY
#include "proxy/entry_bridge.h"
#endif
#include "core/pe_image.h"
#include "core/generated/build_identity.generated.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ff7r::piano::game::native_audio {
namespace proxy_bootstrap_impl {
using Address=std::uintptr_t;
constexpr std::uint64_t kMaxFile=256ull*1024*1024;
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION id{};
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    std::array<IMAGE_SECTION_HEADER,96> sections{};
    std::array<unsigned char,32> sha{};
    std::filesystem::path path;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
struct Frame {Address pc=0,image=0;RUNTIME_FUNCTION function{};};
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
struct TestAccess {
    bool (*read)(Address,void*,std::size_t)=nullptr;
    bool (*open)(HMODULE,File&)=nullptr;
    bool (*disk)(const File&,DWORD,void*,DWORD)=nullptr;
    bool (*unchanged)(const File&)=nullptr;
    HMODULE (*module)(Address)=nullptr;
    HMODULE (*exe)()=nullptr;
    DWORD (*thread)()=nullptr;
    bool (*lookup)(Address,Frame&)=nullptr;
    unsigned (*capture)(Frame*,unsigned)=nullptr;
    bool (*page)(Address,HMODULE)=nullptr;
} test;
#endif
std::atomic_flag attempted=ATOMIC_FLAG_INIT;
std::atomic<ProxyBootstrapFailure> failure{ProxyBootstrapFailure::None};
std::atomic<const char*> predicate{nullptr};
std::atomic<DWORD> observed_thread{0};
std::atomic_bool invoked{false},published{false};
std::atomic_bool observed_post_crt{false};
bool reject(ProxyBootstrapFailure value,const char* why) noexcept {
    auto expected=ProxyBootstrapFailure::None;
    if(failure.compare_exchange_strong(expected,value,std::memory_order_acq_rel))predicate.store(why,std::memory_order_release);
    return false;
}
DWORD thread() noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.thread)return test.thread();
#endif
    return GetCurrentThreadId();
}
bool copy(Address address,void* out,std::size_t count) noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.read)return test.read(address,out,count);
#endif
    return address && count && count<=65536 && core::safe_copy_bytes(reinterpret_cast<void*>(address),out,count);
}
template<class T> bool read(Address address,T& value) noexcept {return copy(address,&value,sizeof(value));}
HMODULE module_at(Address address) noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.module)return test.module(address);
#endif
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address),&module)?module:nullptr;
}
HMODULE executable() noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.exe)return test.exe();
#endif
    return GetModuleHandleW(nullptr);
}
bool file_read(HANDLE h,std::uint64_t offset,void* out,DWORD size) noexcept {
    LARGE_INTEGER where{};where.QuadPart=offset;DWORD got=0;
    return size && size<=65536 && SetFilePointerEx(h,where,nullptr,FILE_BEGIN)
        && ReadFile(h,out,size,&got,nullptr) && got==size;
}
bool digest(HANDLE h,std::uint64_t size,std::array<unsigned char,32>& sha) noexcept {
    if(!size || size>kMaxFile)return false;
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
        && BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    std::array<unsigned char,65536> block{};
    for(std::uint64_t pos=0;ok && pos<size;){const auto n=static_cast<DWORD>((std::min)(size-pos,std::uint64_t(block.size())));
        ok=file_read(h,pos,block.data(),n) && BCryptHashData(hash,block.data(),n,0)>=0;pos+=n;}
    ok=ok && BCryptFinishHash(hash,sha.data(),static_cast<ULONG>(sha.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
bool hash_equal(const std::array<unsigned char,32>& sha,std::string_view expected) noexcept {
    if(expected.size()!=64)return false;
    auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(unsigned i=0;i<32;++i){const auto a=hex(expected[i*2]),b=hex(expected[i*2+1]);
        if(a<0 || b<0 || sha[i]!=a*16+b)return false;}return true;
}
std::uint64_t size(const File& f) noexcept {return (std::uint64_t(f.id.nFileSizeHigh)<<32)|f.id.nFileSizeLow;}
bool open(HMODULE module,File& f) {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.open)return test.open(module,f);
#endif
    std::array<wchar_t,32768> name{};const auto n=GetModuleFileNameW(module,name.data(),static_cast<DWORD>(name.size()));
    if(!n || n>=name.size())return false;
    const auto raw=std::filesystem::path(std::wstring(name.data(),n));
    f.handle=CreateFileW(raw.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f.handle==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(f.handle,&f.id)
        || (f.id.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || size(f)>kMaxFile)return false;
    const auto final=GetFinalPathNameByHandleW(f.handle,name.data(),static_cast<DWORD>(name.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!final || final>=name.size())return false;f.path=std::wstring(name.data(),final);
    if(!file_read(f.handle,0,&f.dos,sizeof(f.dos)) || f.dos.e_magic!=IMAGE_DOS_SIGNATURE || f.dos.e_lfanew<sizeof(f.dos)
        || std::uint64_t(f.dos.e_lfanew)+sizeof(f.nt)>size(f) || !file_read(f.handle,f.dos.e_lfanew,&f.nt,sizeof(f.nt))
        || f.nt.Signature!=IMAGE_NT_SIGNATURE || f.nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
        || f.nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64) || f.nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || !f.nt.FileHeader.NumberOfSections || f.nt.FileHeader.NumberOfSections>f.sections.size())return false;
    return file_read(f.handle,f.dos.e_lfanew+sizeof(f.nt),f.sections.data(),f.nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER))
        && digest(f.handle,size(f),f.sha);
}
bool unchanged(const File& f) noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.unchanged)return test.unchanged(f);
#endif
    BY_HANDLE_FILE_INFORMATION id{};
    return GetFileInformationByHandle(f.handle,&id) && id.dwVolumeSerialNumber==f.id.dwVolumeSerialNumber
        && id.nFileIndexHigh==f.id.nFileIndexHigh && id.nFileIndexLow==f.id.nFileIndexLow
        && id.nFileSizeHigh==f.id.nFileSizeHigh && id.nFileSizeLow==f.id.nFileSizeLow
        && id.dwFileAttributes==f.id.dwFileAttributes && !std::memcmp(&id.ftLastWriteTime,&f.id.ftLastWriteTime,sizeof(FILETIME));
}
bool disk(const File& f,DWORD rva,void* out,DWORD count) noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.disk)return test.disk(f,rva,out,count);
#endif
    if(!count || count>65536 || rva>f.nt.OptionalHeader.SizeOfImage || count>f.nt.OptionalHeader.SizeOfImage-rva)return false;
    for(unsigned i=0;i<f.nt.FileHeader.NumberOfSections;++i){const auto& s=f.sections[i];
        if(rva>=s.VirtualAddress && std::uint64_t(rva)+count<=std::uint64_t(s.VirtualAddress)+s.SizeOfRawData)
            return file_read(f.handle,std::uint64_t(s.PointerToRawData)+rva-s.VirtualAddress,out,count);}
    return false;
}
bool image(HMODULE module,const File& f) noexcept {
    const auto base=reinterpret_cast<Address>(module);IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!base || (base&0xffff) || !read(base,dos) || dos.e_magic!=f.dos.e_magic || dos.e_lfanew!=f.dos.e_lfanew
        || !read(base+dos.e_lfanew,nt))return false;
    const auto preferred=f.nt.OptionalHeader.ImageBase;
    if(nt.OptionalHeader.ImageBase!=preferred && nt.OptionalHeader.ImageBase!=base)return false;
    if(base!=preferred){const auto reloc=f.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if((f.nt.FileHeader.Characteristics&IMAGE_FILE_RELOCS_STRIPPED) || !reloc.VirtualAddress || !reloc.Size
            || reloc.VirtualAddress>f.nt.OptionalHeader.SizeOfImage || reloc.Size>f.nt.OptionalHeader.SizeOfImage-reloc.VirtualAddress)return false;}
    nt.OptionalHeader.ImageBase=preferred;
    if(std::memcmp(&nt,&f.nt,sizeof(nt)) || !nt.OptionalHeader.SizeOfImage
        || base>UINTPTR_MAX-nt.OptionalHeader.SizeOfImage || !nt.FileHeader.NumberOfSections
        || nt.FileHeader.NumberOfSections>96 || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
        || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    std::array<IMAGE_SECTION_HEADER,96> sections{};
    const auto end=std::uint64_t(dos.e_lfanew)+sizeof(nt)+nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER);
    return end<=nt.OptionalHeader.SizeOfHeaders && end<=nt.OptionalHeader.SizeOfImage
        && copy(base+dos.e_lfanew+sizeof(nt),sections.data(),nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER))
        && !std::memcmp(sections.data(),f.sections.data(),nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER));
}
bool page(Address address,HMODULE owner) noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.page)return test.page(address,owner);
#endif
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))==sizeof(m) && m.AllocationBase==owner
        && m.State==MEM_COMMIT && m.Type==MEM_IMAGE && m.Protect==PAGE_EXECUTE_READ;
}
bool lookup(Address pc,Frame& frame) noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.lookup)return test.lookup(pc,frame);
#endif
    __try {DWORD64 base=0;const auto* row=RtlLookupFunctionEntry(pc,&base,nullptr);
        if(!row || !base || !read(reinterpret_cast<Address>(row),frame.function))return false;
        frame.pc=pc;frame.image=base;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool disk_function(const File& f,DWORD pc,RUNTIME_FUNCTION& out) noexcept {
    const auto dir=f.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if(!dir.VirtualAddress || !dir.Size || dir.Size%sizeof(out) || dir.Size>16*1024*1024)return false;
    DWORD lo=0,hi=dir.Size/sizeof(out);
    while(lo<hi){const auto mid=lo+(hi-lo)/2;RUNTIME_FUNCTION row{};
        if(!disk(f,dir.VirtualAddress+mid*sizeof(row),&row,sizeof(row)))return false;
        if(pc<row.BeginAddress)hi=mid;else if(pc>=row.EndAddress)lo=mid+1;else {out=row;return true;}}
    return false;
}
bool equal_bytes(const File& f,Address base,DWORD rva,DWORD count) noexcept {
    if(!count || count>65536)return false;
    std::array<unsigned char,1024> expected{},actual{};
    for(DWORD offset=0;offset<count;){const auto n=(std::min)(count-offset,DWORD(expected.size()));
        if(!disk(f,rva+offset,expected.data(),n) || !copy(base+rva+offset,actual.data(),n)
            || std::memcmp(expected.data(),actual.data(),n))return false;offset+=n;}return true;
}
bool function(const File& f,const Frame& frame,RUNTIME_FUNCTION& primary) noexcept {
    if(frame.pc<frame.image || frame.pc-frame.image>=f.nt.OptionalHeader.SizeOfImage)return false;
    auto row=frame.function;RUNTIME_FUNCTION disk_row{};std::array<DWORD,16> visited{};
    if(!disk_function(f,static_cast<DWORD>(frame.pc-frame.image),disk_row) || std::memcmp(&row,&disk_row,sizeof(row)))return false;
    for(unsigned depth=0;depth<visited.size();++depth){
        if(row.BeginAddress>=row.EndAddress || row.EndAddress>f.nt.OptionalHeader.SizeOfImage
            || std::find(visited.begin(),visited.begin()+depth,row.UnwindData)!=visited.begin()+depth
            || !equal_bytes(f,frame.image,row.BeginAddress,row.EndAddress-row.BeginAddress))return false;
        visited[depth]=row.UnwindData;std::array<unsigned char,4> h{};
        if(!disk(f,row.UnwindData,h.data(),4) || (h[0]&7)!=1)return false;
        const auto flags=h[0]>>3;if(flags>7 || ((flags&4)&&(flags&3)))return false;
        const DWORD codes=(4+2*h[2]+3)&~3u,extent=codes+((flags&4)?12:(flags&3)?4:0);
        if(!equal_bytes(f,frame.image,row.UnwindData,extent))return false;
        if(!(flags&4)){primary=row;return true;}
        RUNTIME_FUNCTION parent{};
        if(!disk(f,row.UnwindData+codes,&parent,sizeof(parent)) || !disk_function(f,parent.BeginAddress,disk_row)
            || std::memcmp(&parent,&disk_row,sizeof(parent)))return false;row=parent;
    }return false;
}
bool callable(const File& file,HMODULE owner,Address pc,RUNTIME_FUNCTION& primary) noexcept {
    Frame frame{};return module_at(pc)==owner && page(pc,owner) && lookup(pc,frame)
        && frame.image==reinterpret_cast<Address>(owner) && function(file,frame,primary);
}
unsigned capture(std::array<Frame,32>& frames) noexcept {
#ifdef FF7RP_NATIVE_PROXY_BOOTSTRAP_SELFTEST
    if(test.capture)return test.capture(frames.data(),static_cast<unsigned>(frames.size()));
#endif
    std::array<void*,32> pcs{};const auto count=RtlCaptureStackBackTrace(0,static_cast<DWORD>(pcs.size()),pcs.data(),nullptr);
    for(unsigned i=0;i<count;++i)if(!lookup(reinterpret_cast<Address>(pcs[i]),frames[i]))return 0;return count;
}
#ifdef FF7RP_OWNED_PROXY
struct Scope {
    HMODULE exe=nullptr,own=nullptr;Address caller=0,initialize=0;
    DWORD owner=0;File executable,self;
    const ff7rp::proxy::EntryScope* bridge=nullptr;
    const ff7rp::proxy::CallScope* call_bridge=nullptr;
    ff7rp::proxy::CallProof call_initial{};
    bool post=false;
    ff7rp::proxy::EntryProof initial{};
    enum class Stage {Validating,Installing,Published,Failed} stage=Stage::Validating;
};
thread_local Scope* scope=nullptr;
bool same_proof(const ff7rp::proxy::EntryProof& a,const ff7rp::proxy::EntryProof& b) noexcept {
    return a.image==b.image && a.proxy==b.proxy && a.entry==b.entry
        && a.attach_thread==b.attach_thread && a.entry_thread==b.entry_thread
        && a.original==b.original && a.restored==b.restored && a.callback_invoked==b.callback_invoked
        && a.callback_succeeded==b.callback_succeeded
        && a.profile.timestamp==b.profile.timestamp && a.profile.image_size==b.profile.image_size
        && a.profile.checksum==b.profile.checksum && a.profile.entry_rva==b.profile.entry_rva
        && a.profile.signature_size==b.profile.signature_size && a.profile.signature==b.profile.signature;
}
bool entry_code(Scope& s,Address detour) noexcept {
    const auto contract=native_proxy_detail::entry_contract();const auto entry=reinterpret_cast<Address>(s.exe)+contract.rva;
    if(!contract.rva || contract.original.size()!=18 || !page(entry,s.exe))return false;
    std::array<unsigned char,18> bytes{};
    if(!copy(entry,bytes.data(),bytes.size()))return false;
    if(!detour)return !std::memcmp(bytes.data(),contract.original.data(),bytes.size());
    if(bytes[0]!=0xe9 || std::memcmp(bytes.data()+5,contract.original.data()+5,bytes.size()-5))return false;
    std::int32_t displacement=0;std::memcpy(&displacement,bytes.data()+1,4);
    const auto target=entry+5+static_cast<std::intptr_t>(displacement);
    if(target==detour)return true;
    std::array<unsigned char,14> relay{};Address destination=0;
    if(!copy(target,relay.data(),relay.size()) || std::memcmp(relay.data(),"\xff\x25\0\0\0\0",6))return false;
    std::memcpy(&destination,relay.data()+6,8);return destination==detour;
}
bool validate(Scope& s,Address detour=0) {
    if(failure.load(std::memory_order_acquire)!=ProxyBootstrapFailure::None || thread()!=s.owner
        || executable()!=s.exe || s.own==s.exe
        || module_at(reinterpret_cast<Address>(&try_owned_proxy_native_audio_startup))!=s.own
        || module_at(s.caller)!=s.own)return reject(ProxyBootstrapFailure::Modules,"thread_or_modules");
    if(!unchanged(s.executable) || !unchanged(s.self))return reject(ProxyBootstrapFailure::Files,"file_changed");
    if(!image(s.exe,s.executable) || !image(s.own,s.self))return reject(ProxyBootstrapFailure::Image,"loaded_image");
    RUNTIME_FUNCTION callback{},caller{};
    if(!callable(s.self,s.own,s.initialize,callback) || !callable(s.self,s.own,s.caller,caller))
        return reject(ProxyBootstrapFailure::Code,"callback_or_caller_code_unwind");
    std::array<Frame,32> frames{};const auto n=capture(frames);unsigned hits=0;
    if(!n || n>frames.size())return reject(ProxyBootstrapFailure::Frames,"capture");
    if(s.post) {
        ff7rp::proxy::CallProof current{};
        const auto* held=ff7rp::proxy::active_call_scope();
        const auto* call=find_rva_signature("native_bootstrap_show_to_winmain");
        const auto* target=find_rva_signature("native_bootstrap_show");
        const auto entry=native_proxy_detail::entry_contract();
        const auto& root=s.call_initial.root;
        if(!core::generated::kOwnedProxyPreWinMain || !held || held!=s.call_bridge || !held->capture(current)
            || !same_proof(root,current.root) || current.return_pc!=s.call_initial.return_pc
            || current.profile.call_rva!=s.call_initial.profile.call_rva || current.profile.target_rva!=s.call_initial.profile.target_rva
            || current.profile.window_size!=s.call_initial.profile.window_size || current.profile.window!=s.call_initial.profile.window
            || root.image!=reinterpret_cast<Address>(s.exe) || root.proxy!=reinterpret_cast<Address>(s.own)
            || root.attach_thread!=s.owner || root.entry_thread!=s.owner || !root.restored || !root.callback_invoked
            || root.profile.timestamp!=core::generated::kExeTimestamp || root.profile.image_size!=core::generated::kSizeOfImage
            || root.profile.checksum!=core::generated::kPeChecksum || root.profile.entry_rva!=entry.rva
            || root.profile.signature_size!=entry.original.size() || entry.original.size()<root.original.size()
            || entry.original.size()>root.profile.signature.size()
            || std::memcmp(root.original.data(),entry.original.data(),root.original.size())
            || std::memcmp(root.profile.signature.data(),entry.original.data(),entry.original.size())
            || !call || !target || current.profile.call_rva!=call->rva || current.profile.target_rva!=target->rva
            || current.profile.window_size!=call->expected_prologue.size() || call->expected_prologue.size()<5
            || call->expected_prologue.size()>current.profile.window.size()
            || std::memcmp(current.profile.window.data(),call->expected_prologue.data(),call->expected_prologue.size())
            || current.return_pc!=reinterpret_cast<Address>(s.exe)+call->rva+5)
            return reject(ProxyBootstrapFailure::Context,"private_call_scope");
        bool thunk=false,source=false;
        for(unsigned i=0;i<n;++i) {
            RUNTIME_FUNCTION row{};
            if(thunk) {
                if(frames[i].image!=reinterpret_cast<Address>(s.exe) || frames[i].pc!=current.return_pc
                    || module_at(frames[i].pc)!=s.exe || !function(s.executable,frames[i],row)
                    || row.BeginAddress>call->rva || row.EndAddress<=call->rva+5)
                    return reject(ProxyBootstrapFailure::Frames,"post_crt_return_unwind");
                source=true;break;
            }
            if(frames[i].image!=reinterpret_cast<Address>(s.own) || module_at(frames[i].pc)!=s.own
                || !function(s.self,frames[i],row))return reject(ProxyBootstrapFailure::Frames,"post_own_frame");
            if(frames[i].pc==s.caller)++hits;
            if(frames[i].pc==reinterpret_cast<Address>(&ff7rp_proxy_call_dispatch_return))thunk=true;
        }
        if(hits!=1 || !source)return reject(ProxyBootstrapFailure::Frames,"post_current_chain");
        if(!page(reinterpret_cast<Address>(s.exe)+call->rva,s.exe)
            || !equal_bytes(s.executable,reinterpret_cast<Address>(s.exe),static_cast<DWORD>(call->rva),static_cast<DWORD>(call->expected_prologue.size()))
            || !page(reinterpret_cast<Address>(s.exe)+target->rva,s.exe)
            || !equal_bytes(s.executable,reinterpret_cast<Address>(s.exe),static_cast<DWORD>(target->rva),static_cast<DWORD>(target->expected_prologue.size())))
            return reject(ProxyBootstrapFailure::Code,"restored_show_window");
        const char* baseline_predicate=nullptr;
        if(!baseline_facts::check(reinterpret_cast<Address>(s.exe),true,false,copy,&baseline_predicate))
            return reject(ProxyBootstrapFailure::Cold,baseline_predicate);
        return true;
    }
    for(unsigned i=0;i<n;++i){
        RUNTIME_FUNCTION row{};
        if(frames[i].image!=reinterpret_cast<Address>(s.own) || module_at(frames[i].pc)!=s.own
            || !function(s.self,frames[i],row))return reject(ProxyBootstrapFailure::Frames,"own_frame");
        if(frames[i].pc==s.caller){++hits;break;}
    }
    if(hits!=1)return reject(ProxyBootstrapFailure::Frames,"internal_caller");
    ff7rp::proxy::EntryProof current{};
    const auto contract=native_proxy_detail::entry_contract();
    const auto* bridge=ff7rp::proxy::active_entry_scope();
    if(!bridge || bridge!=s.bridge || !bridge->capture(current)
        || current.proxy!=reinterpret_cast<Address>(s.own) || current.image!=reinterpret_cast<Address>(s.exe)
        || current.attach_thread!=s.owner || current.entry_thread!=s.owner
        || !current.restored || !current.callback_invoked || current.callback_succeeded
        || current.profile.timestamp!=core::generated::kExeTimestamp
        || current.profile.image_size!=core::generated::kSizeOfImage || current.profile.checksum!=core::generated::kPeChecksum
        || current.profile.entry_rva!=contract.rva || current.profile.signature_size!=contract.original.size()
        || contract.original.size()!=18
        || std::memcmp(current.profile.signature.data(),contract.original.data(),contract.original.size())
        || std::memcmp(current.original.data(),contract.original.data(),current.original.size())
        || current.entry!=reinterpret_cast<Address>(s.exe)+contract.rva
        || (s.initial.image && !same_proof(s.initial,current)))return reject(ProxyBootstrapFailure::Context,"private_entry_scope");
    if(!native_proxy_detail::cold_entry_ready(s.exe))return reject(ProxyBootstrapFailure::Cold,"cold_entry_ready");
    if(!entry_code(s,detour))return reject(ProxyBootstrapFailure::Cold,"entry_ownership");
    s.initial=current;return true;
}
#endif
}

ProxyBootstrapSnapshot native_proxy_bootstrap_status() noexcept {
    using namespace proxy_bootstrap_impl;
    return {failure.load(std::memory_order_acquire),predicate.load(std::memory_order_acquire),observed_thread.load(),invoked.load(),published.load(),observed_post_crt.load()};
}
bool try_owned_proxy_native_audio_startup(HMODULE exe,HMODULE own,std::uintptr_t caller,NativeStartupCallback initialize) noexcept {
#ifndef FF7RP_OWNED_PROXY
    (void)exe;(void)own;(void)caller;(void)initialize;
    return false;
#else
    using namespace proxy_bootstrap_impl;
    if(attempted.test_and_set(std::memory_order_acq_rel))return false;
    try {
        if(!exe || !own || !caller || !initialize || scope)return reject(ProxyBootstrapFailure::Attempt,"arguments_or_reentry");
        Scope current;current.exe=exe;current.own=own;current.caller=caller;current.initialize=reinterpret_cast<Address>(initialize);
        current.owner=thread();observed_thread.store(current.owner);
        current.call_bridge=ff7rp::proxy::active_call_scope();
        current.post=current.call_bridge!=nullptr;
        observed_post_crt.store(current.post);
        if(current.post && !current.call_bridge->capture(current.call_initial))
            return reject(ProxyBootstrapFailure::Context,"no_private_call_scope");
        current.bridge=ff7rp::proxy::active_entry_scope();
        if(!current.post && !current.bridge)return reject(ProxyBootstrapFailure::Context,"no_private_entry_scope");
        if(!open(exe,current.executable) || !open(own,current.self))return reject(ProxyBootstrapFailure::Files,"open");
        const auto& nt=current.executable.nt;
        if(size(current.executable)!=core::generated::kExeFileSize || !hash_equal(current.executable.sha,core::generated::kExeSha256)
            || nt.FileHeader.TimeDateStamp!=core::generated::kExeTimestamp || nt.OptionalHeader.SizeOfImage!=core::generated::kSizeOfImage
            || nt.OptionalHeader.CheckSum!=core::generated::kPeChecksum || nt.OptionalHeader.AddressOfEntryPoint!=native_proxy_detail::entry_contract().rva)
            return reject(ProxyBootstrapFailure::Files,"pinned_identity");
        const auto directory=current.executable.path.parent_path().native();
        if(_wcsicmp(directory.c_str(),current.self.path.parent_path().c_str()))
            return reject(ProxyBootstrapFailure::Files,"executable_directory");
        if(!validate(current))return false;
        struct Active {explicit Active(Scope& s){scope=&s;}~Active(){scope=nullptr;}} active(current);
        invoked.store(true,std::memory_order_release);initialize();return true;
    }catch(...){return reject(ProxyBootstrapFailure::Exception,"exception");}
#endif
}
namespace native_proxy_detail {
#ifdef FF7RP_OWNED_PROXY
bool scope_present() noexcept {return proxy_bootstrap_impl::scope!=nullptr;}
bool post_crt_scope_present() noexcept {auto* s=proxy_bootstrap_impl::scope;return s && s->post;}
bool begin_publication(HMODULE exe) noexcept {
    using namespace proxy_bootstrap_impl;auto* s=scope;
    try {if(!s || s->exe!=exe || s->stage!=Scope::Stage::Validating || !validate(*s))return false;
        s->stage=Scope::Stage::Installing;return true;}catch(...){return reject(ProxyBootstrapFailure::Exception,"begin_exception");}
}
bool finish_publication(HMODULE exe,std::uintptr_t detour) noexcept {
    using namespace proxy_bootstrap_impl;auto* s=scope;
    try {if(!s || s->exe!=exe || s->stage!=Scope::Stage::Installing || (!s->post && !detour) || !validate(*s,detour))return false;
        s->stage=Scope::Stage::Published;published.store(true,std::memory_order_release);return true;
    }catch(...){return reject(ProxyBootstrapFailure::Exception,"finish_exception");}
}
void abandon_publication(HMODULE exe) noexcept {
    using namespace proxy_bootstrap_impl;auto* s=scope;
    if(s && s->exe==exe && s->owner==thread()){s->stage=Scope::Stage::Failed;published.store(false);reject(ProxyBootstrapFailure::Publication,"rollback");}
}
#else
bool scope_present() noexcept {return false;}
bool post_crt_scope_present() noexcept {return false;}
bool begin_publication(HMODULE) noexcept {return false;}
bool finish_publication(HMODULE,std::uintptr_t) noexcept {return false;}
void abandon_publication(HMODULE) noexcept {}
#endif
}
}
