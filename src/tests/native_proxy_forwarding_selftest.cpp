// Includes the actual gateway with Win32 loader doubles; only fixture callbacks run.
#include <windows.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr,"%s\n",message); std::abort(); }
}
const auto self_module = reinterpret_cast<HMODULE>(0x10000000);
const auto system_module = reinterpret_cast<HMODULE>(0x20000000);
const auto foreign_module = reinterpret_cast<HMODULE>(0x30000000);
std::wstring self_path, loaded_path, target_path;
unsigned loads=0, frees=0, resolutions=0, calls=0;
bool system_failure=false, load_failure=false, redirected=false, self_load=false;
bool path_failure=false, bad_mapping=false, foreign_target=false, non_image=false;
bool reenter=false, mismatched_ordinal=false;
bool only_required=false, keep_missing_ordinal_present=false;
FARPROC invalid_target=nullptr;
FARPROC foreign_export=nullptr;
std::string missing_name;
unsigned missing_ordinal=0;
std::vector<unsigned> requested_ordinals;
std::vector<std::string> requested_names;
int recursive_status=-1;

std::uint64_t integer_fixture(std::uint64_t a,std::uint64_t b,std::uint64_t c,std::uint64_t d,
    std::uint64_t e,std::uint64_t f,std::uint64_t g,std::uint64_t h)
{
    ++calls;
    check(GetLastError()==0x1234,"Tail thunk changed incoming LastError");
    SetLastError(0x5678);
    return a+3*b+5*c+7*d+11*e+13*f+17*g+19*h;
}
double float_fixture(std::uint64_t a,double b,std::uint64_t c,double d,double e,std::uint64_t f)
{
    ++calls;
    return static_cast<double>(a)+b+static_cast<double>(c)+d+e+static_cast<double>(f);
}
struct Large { std::uint64_t a,b,c; };
Large aggregate_fixture(std::uint64_t a,double b,std::uint64_t c,std::uint64_t d,std::uint64_t e)
{
    ++calls;
    return {a+c,static_cast<std::uint64_t>(b)+d,e};
}
void generic_fixture() { ++calls; }
void mismatch_fixture() { ++calls; }
int perf_fixture(DWORD color,LPCWSTR text)
{
    ++calls;
    check(color==0x12345678 && text && std::wcscmp(text,L"fixture")==0,"Required d3d9 forwarding damaged arguments");
    return 37;
}
template<class T> FARPROC proc(T function) { return std::bit_cast<FARPROC>(function); }
template<std::size_t Index> void distinct_fixture() { calls+=static_cast<unsigned>(Index+1); }
template<std::size_t... Index> auto distinct_functions(std::index_sequence<Index...>)
{
    return std::array{proc(&distinct_fixture<Index>)...};
}
const auto distinct=distinct_functions(std::make_index_sequence<52>{});
std::vector<std::string> name_order;

FARPROC named(const char* name)
{
    if (std::strcmp(name,"D3DCompile")==0 || std::strcmp(name,"XInputGetState")==0)
        return proc(&integer_fixture);
    if (std::strcmp(name,"D3DDisassemble")==0) return proc(&float_fixture);
    if (std::strcmp(name,"D3DCreateBlob")==0) return proc(&aggregate_fixture);
    if (std::strcmp(name,"D3DPERF_BeginEvent")==0) return proc(&perf_fixture);
    const auto found=std::find(name_order.begin(),name_order.end(),name);
    check(found!=name_order.end(),"Unknown fixture export name");
    return distinct[static_cast<std::size_t>(found-name_order.begin())];
}

bool required_name(const char* name)
{
    // Independent EXE-contract expectation, not the production required mask.
    constexpr const char* required[]={"D3DDisassemble","D3DPERF_BeginEvent","D3DPERF_EndEvent",
        "D3D11CreateDevice","DirectSoundCreate8","DirectSoundCaptureEnumerateW","DirectSoundCaptureCreate8",
        "timeGetDevCaps","timeEndPeriod","timeGetTime","timeBeginPeriod","CreateDXGIFactory",
        "CreateDXGIFactory1","XInputGetState","XInputSetState"};
    return std::any_of(std::begin(required),std::end(required),[&](const char* item) { return std::strcmp(item,name)==0; });
}

FARPROC fixture_export(const char* name,bool by_ordinal=false)
{
    if ((only_required && !required_name(name)) || (missing_name==name && !(by_ordinal && keep_missing_ordinal_present)))
        return nullptr;
    return named(name);
}

DWORD WINAPI fake_filename(HMODULE module,LPWSTR buffer,DWORD capacity)
{
    const auto& value=module==self_module ? self_path : module==system_module ? loaded_path : target_path;
    if (path_failure || value.size()>=capacity) return capacity;
    std::wmemcpy(buffer,value.c_str(),value.size()+1); return static_cast<DWORD>(value.size());
}
UINT WINAPI fake_system_directory(LPWSTR buffer,UINT capacity)
{
    check(capacity>=32,"Unexpected System32 path bound");
    if (system_failure) return capacity;
    constexpr wchar_t directory[]=L"C:\\Windows\\System32";
    std::wmemcpy(buffer,directory,std::size(directory)); return 19;
}
HMODULE WINAPI fake_load(LPCWSTR path,HANDLE file,DWORD flags);
FARPROC WINAPI fake_get(HMODULE module,LPCSTR name);
BOOL WINAPI fake_free(HMODULE module)
{
    check(module==system_module || (self_load && module==self_module),"Released foreign reference");
    ++frees; return TRUE;
}
SIZE_T WINAPI fake_query(LPCVOID address,PMEMORY_BASIC_INFORMATION info,SIZE_T size)
{
    check(size==sizeof(*info),"Unexpected query size");
    *info={}; info->State=MEM_COMMIT; info->Type=non_image ? MEM_PRIVATE : MEM_IMAGE;
    info->Protect=(bad_mapping || (invalid_target && address==reinterpret_cast<LPCVOID>(invalid_target))) ?
        PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    info->AllocationBase=(foreign_target || (foreign_export && address==reinterpret_cast<LPCVOID>(foreign_export))) ?
        foreign_module : system_module;
    return sizeof(*info);
}
} // namespace

#define GetModuleFileNameW fake_filename
#define GetSystemDirectoryW fake_system_directory
#define LoadLibraryExW fake_load
#define GetProcAddress fake_get
#define FreeLibrary fake_free
#define VirtualQuery fake_query
#include "proxy/forwarding.cpp"
#undef GetModuleFileNameW
#undef GetSystemDirectoryW
#undef LoadLibraryExW
#undef GetProcAddress
#undef FreeLibrary
#undef VirtualQuery

namespace {
using namespace ff7r::piano::proxy;
HMODULE WINAPI fake_load(LPCWSTR path,HANDLE file,DWORD flags)
{
    ++loads;
    check(!file && flags==LOAD_LIBRARY_SEARCH_SYSTEM32,"Unsafe DLL search flags");
    check(std::wstring(path)==L"C:\\Windows\\System32\\"+self_path.substr(self_path.find_last_of(L'\\')+1),
        "Original resolved outside exact System32 basename");
    check(native_proxy_targets==nullptr,"Partial table published before load");
    if (reenter) recursive_status=static_cast<int>(initialize_forwarding(self_module));
    SetLastError(777);
    if (load_failure) return nullptr;
    loaded_path=redirected ? L"C:\\other\\original.dll" : path;
    return self_load ? self_module : system_module;
}
FARPROC WINAPI fake_get(HMODULE module,LPCSTR name)
{
    check(module==system_module,"Resolved wrong module"); ++resolutions;
    check(native_proxy_targets==nullptr,"Partial forwarding table visible during resolution");
    const auto value=reinterpret_cast<std::uintptr_t>(name);
    if (value<=65535) {
        const auto ordinal=static_cast<unsigned>(value); requested_ordinals.push_back(ordinal);
        if (ordinal==missing_ordinal) return nullptr;
        if (mismatched_ordinal) return proc(&mismatch_fixture);
        const auto base=self_path.substr(self_path.find_last_of(L'\\')+1);
        if (_wcsicmp(base.c_str(),L"D3DCOMPILER_43.dll")==0) {
            constexpr const char* order[]={"D3DAssemble","DebugSetMute","D3DCompile","D3DCompressShaders",
                "D3DCreateBlob","D3DDecompressShaders","D3DDisassemble","D3DDisassemble10Effect",
                "D3DGetBlobPart","D3DGetDebugInfo","D3DGetInputAndOutputSignatureBlob","D3DGetInputSignatureBlob",
                "D3DGetOutputSignatureBlob","D3DPreprocess","D3DReflect","D3DReturnFailure1","D3DStripShader"};
            check(ordinal>=1 && ordinal<=std::size(order),"Unknown compiler ordinal");
            return fixture_export(order[ordinal-1],true);
        }
        if (_wcsicmp(base.c_str(),L"d3d9.dll")==0) {
            constexpr const char* order[]={nullptr,nullptr,nullptr,nullptr,"Direct3DCreate9On12","Direct3DCreate9On12Ex",
                nullptr,nullptr,"Direct3DShaderValidatorCreate9","PSGPError","PSGPSampleTexture","D3DPERF_BeginEvent",
                "D3DPERF_EndEvent","D3DPERF_GetStatus","D3DPERF_QueryRepeatFrame","D3DPERF_SetMarker",
                "D3DPERF_SetOptions","D3DPERF_SetRegion","DebugSetLevel","DebugSetMute",
                "Direct3D9EnableMaximizedWindowedModeShim","Direct3DCreate9","Direct3DCreate9Ex"};
            check(ordinal>=16 && ordinal<=38,"Unknown d3d9 ordinal");
            return order[ordinal-16] ? fixture_export(order[ordinal-16],true) :
                only_required ? nullptr : proc(&generic_fixture);
        }
        if (_wcsicmp(base.c_str(),L"dsound.dll")==0) {
            check(ordinal==8 || ordinal==11 || ordinal==12,"Unknown Sound ordinal");
            return fixture_export(ordinal==8 ? "DirectSoundCaptureEnumerateW" : ordinal==11 ? "DirectSoundCreate8" : "DirectSoundCaptureCreate8",true);
        }
        check(_wcsicmp(base.c_str(),L"xinput1_3.dll")==0,"Unknown ordinal profile");
        if (ordinal==2 || ordinal==3) return fixture_export(ordinal==2 ? "XInputGetState" : "XInputSetState",true);
        check(ordinal>=100 && ordinal<=103,"Unknown XInput ordinal");
        return only_required ? nullptr : proc(&generic_fixture);
    }
    requested_names.emplace_back(name);
    return fixture_export(name);
}

void reset(const wchar_t* basename=L"D3DCOMPILER_43.dll")
{
    // Test-only reset: all module operations are doubles and no worker is running.
    native_proxy_targets=nullptr; state=0; proxy_module=nullptr; retained_original=nullptr;
    targets.fill(nullptr); self_path=L"D:\\fixture\\"+std::wstring(basename);
    name_order.clear(); for (const auto& entry:names) name_order.emplace_back(entry.name);
    loaded_path.clear(); target_path=L"C:\\Windows\\System32\\kernelbase.dll";
    loads=frees=resolutions=calls=0; requested_ordinals.clear(); requested_names.clear();
    system_failure=load_failure=redirected=self_load=path_failure=bad_mapping=foreign_target=non_image=false;
    reenter=mismatched_ordinal=false; missing_name.clear(); missing_ordinal=0; recursive_status=-1;
    only_required=keep_missing_ordinal_present=false; invalid_target=foreign_export=nullptr;
}

void expect_failure(ForwardingStatus expected)
{
    SetLastError(0x1234);
    check(initialize_forwarding(self_module)==expected,"Wrong failure reason");
    check(GetLastError()==0x1234,"Initialization changed LastError");
    check(!forwarding_ready() && native_proxy_targets==nullptr,"Failed/partial table published");
    const auto before=loads;
    check(initialize_forwarding(self_module)==ForwardingStatus::FailedPreviously && loads==before,
        "Failed initialization retried loader");
    check(!calls,"Initialization invoked an original export");
}

void profile_tests()
{
    constexpr const wchar_t* profiles[]={L"d3dcompiler_43.dll",L"d3d9.dll",L"d3d11.dll",L"dsound.dll",
        L"winmm.dll",L"dxgi.dll",L"xinput1_3.dll"};
    for (const auto* profile:profiles) {
        reset(profile); reenter=true; SetLastError(0x1234);
        check(initialize_forwarding(self_module)==ForwardingStatus::Ready,"Supported profile failed");
        check(GetLastError()==0x1234 && forwarding_ready(),"Ready publication/error preservation failed");
        check(recursive_status==static_cast<int>(ForwardingStatus::InProgress),"Reentrant initialization waited/retried");
        check(loads==1 && frees==0 && retained_original==system_module && calls==0,"Module lifetime/invocation mismatch");
        check(initialize_forwarding(self_module)==ForwardingStatus::Ready && loads==1,"Successful initialization repeated");
        check(initialize_forwarding(foreign_module)==ForwardingStatus::InvalidSelf,"Different proxy reused publication");
        auto* published=static_cast<FARPROC*>(native_proxy_targets);
        for (std::size_t i=0;i<slot_count;++i) check(published[i]!=nullptr,"Published empty slot");
        if (std::wcscmp(profile,L"xinput1_3.dll")==0) {
            check(published[1]==named("XInputGetState") && published[2]==named("XInputSetState"),"XInput ordinal collision");
            check(published[10]==&native_proxy_fail_fast,"Unsupported XInput ordinal aliases Sound");
        }
        if (std::wcscmp(profile,L"dsound.dll")==0) {
            check(published[7]==named("DirectSoundCaptureEnumerateW") && published[10]==named("DirectSoundCreate8") &&
                published[11]==named("DirectSoundCaptureCreate8"),"DSound required ordinal mismatch");
            check(published[1]==&native_proxy_fail_fast,"Unsupported Sound ordinal aliases XInput");
        }
        if (std::wcscmp(profile,L"d3dcompiler_43.dll")==0)
            check(published[1]==named("DebugSetMute") && published[10]==named("D3DGetInputAndOutputSignatureBlob"),"Compiler ordinal collision");
        if (std::wcscmp(profile,L"d3d9.dll")==0)
            check(published[26]==named("D3DPERF_BeginEvent") && published[27]==named("D3DPERF_EndEvent"),"D3d9 ordinal mapping");
        const auto configured=requested_names;
        for (const auto& name:configured) {
            reset(profile); missing_name=name;
            if (required_name(name.c_str())) {
                expect_failure(ForwardingStatus::MissingExport);
                check(frees==1,"Partial profile leaked backing reference");
            } else {
                check(initialize_forwarding(self_module)==ForwardingStatus::Ready,"Absent optional export rejected profile");
                const auto index=std::find(name_order.begin(),name_order.end(),name)-name_order.begin();
                check(targets[ordinal_slots+static_cast<std::size_t>(index)]==&native_proxy_fail_fast && frees==0 && calls==0,
                    "Absent optional export not fail-fast, invoked code or released reference");
            }
        }
        reset(profile); only_required=true;
        check(initialize_forwarding(self_module)==ForwardingStatus::Ready && calls==0,"Required-only profile rejected/invoked code");
    }
    reset(L"version.dll"); expect_failure(ForwardingStatus::UnsupportedBasename); check(loads==0,"Unsupported filename loaded");
    reset(); check(initialize_forwarding(nullptr)==ForwardingStatus::InvalidSelf,"Null self accepted");
    reset(); path_failure=true; expect_failure(ForwardingStatus::InvalidSelf);
    reset(); system_failure=true; expect_failure(ForwardingStatus::SystemDirectoryFailed);
    reset(); load_failure=true; expect_failure(ForwardingStatus::LoadFailed); check(frees==0,"Freed failed load");
    reset(); redirected=true; expect_failure(ForwardingStatus::WrongModule); check(frees==1,"Leaked rejected load");
    reset(); self_load=true; expect_failure(ForwardingStatus::WrongModule);
    reset(); missing_name="D3DDisassemble"; expect_failure(ForwardingStatus::MissingExport); check(frees==1,"Leaked partial resolution");
    for (const auto* profile:{L"dsound.dll",L"xinput1_3.dll"}) {
        const std::array<unsigned,3> required=std::wcscmp(profile,L"dsound.dll")==0 ?
            std::array<unsigned,3>{8,11,12} : std::array<unsigned,3>{2,3,2};
        for (const auto ordinal:required) { reset(profile); missing_ordinal=ordinal; expect_failure(ForwardingStatus::MissingExport); }
    }
    reset(); missing_ordinal=17;
    check(initialize_forwarding(self_module)==ForwardingStatus::Ready && targets[16]==&native_proxy_fail_fast,
        "Missing optional ordinal rejected profile or aliased another function");
    reset(L"d3d9.dll"); missing_name="Direct3DCreate9On12"; keep_missing_ordinal_present=true;
    expect_failure(ForwardingStatus::MissingExport);
    reset(L"d3d9.dll"); invalid_target=named("Direct3DCreate9On12"); expect_failure(ForwardingStatus::InvalidTarget);
    reset(L"d3d9.dll"); foreign_export=named("Direct3DCreate9On12"); target_path=L"D:\\fixture\\hook.dll";
    expect_failure(ForwardingStatus::InvalidTarget);
    reset(L"d3d9.dll"); invalid_target=proc(&generic_fixture); expect_failure(ForwardingStatus::InvalidTarget);
    reset(); mismatched_ordinal=true; expect_failure(ForwardingStatus::MissingExport);
    reset(); bad_mapping=true; expect_failure(ForwardingStatus::InvalidTarget);
    reset(); non_image=true; expect_failure(ForwardingStatus::InvalidTarget);
    reset(); foreign_target=true; target_path=L"D:\\fixture\\hook.dll"; expect_failure(ForwardingStatus::InvalidTarget);
    reset(); foreign_target=true; target_path=L"C:\\Windows\\System32\\kernelbase.dll";
    check(initialize_forwarding(self_module)==ForwardingStatus::Ready,"Genuine System32 OS forwarder rejected");
}

// Read the built fixture as data, never LoadLibrary it. This also checks the
// hand-maintained .def/assembly/C++ slot ordering against the actual linked PE.
void export_test(const char* file)
{
    std::ifstream stream(file,std::ios::binary|std::ios::ate);
    check(stream.good(),"Cannot read linked proxy fixture");
    const auto length=stream.tellg(); check(length>0 && length<=4*1024*1024,"Fixture PE size");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length)); stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    check(stream.good(),"Short fixture PE read");
    auto u32=[&](std::size_t at) {
        check(at+4<=bytes.size(),"PE DWORD bounds"); std::uint32_t v=0; std::memcpy(&v,bytes.data()+at,4); return v;
    };
    auto u16=[&](std::size_t at) {
        check(at+2<=bytes.size(),"PE WORD bounds"); std::uint16_t v=0; std::memcpy(&v,bytes.data()+at,2); return v;
    };
    const auto nt=u32(60); check(u16(0)==0x5a4d && u32(nt)==0x4550 && u16(nt+4)==0x8664 &&
        u16(nt+24)==0x20b,"Not x64 PE32+");
    const auto sections=nt+24+u16(nt+20); const auto section_count=u16(nt+6);
    auto offset=[&](std::uint32_t rva) -> std::size_t {
        for (unsigned i=0;i<section_count;++i) {
            const auto s=sections+i*40; const auto start=u32(s+12), size=u32(s+16);
            if (rva>=start && rva-start<size) {
                const auto pos=static_cast<std::size_t>(u32(s+20))+rva-start;
                check(pos<bytes.size(),"PE RVA file bounds"); return pos;
            }
        }
        check(false,"Unmapped PE RVA"); return 0;
    };
    const auto export_rva=u32(nt+24+112), export_size=u32(nt+24+116);
    const auto e=offset(export_rva);
    const auto base=u32(e+16), functions=u32(e+20), count=u32(e+24);
    check(base==1 && functions==307 && count==std::size(names),"Wrong union export range/count");
    const auto function_table=offset(u32(e+28)), name_table=offset(u32(e+32)), ordinal_table=offset(u32(e+36));
    auto thunk=[&](unsigned ordinal,std::size_t slot) {
        const auto rva=u32(function_table+(ordinal-base)*4);
        check(rva && !(rva>=export_rva && rva-export_rva<export_size),"Missing export or recursive PE forwarder");
        auto p=offset(rva);
        check(p+24<=bytes.size() && bytes[p]==0x4c && bytes[p+1]==0x8b && bytes[p+2]==0x1d &&
            bytes[p+7]==0x4d && bytes[p+8]==0x85 && bytes[p+9]==0xdb,"Thunk changed argument/register contract");
        p+=10;
        std::int64_t failure=0;
        if (bytes[p]==0x74) {
            failure=static_cast<std::int64_t>(p)+2+std::bit_cast<std::int8_t>(bytes[p+1]); p+=2;
        } else {
            check(bytes[p]==0x0f && bytes[p+1]==0x84,"Missing unpublished fail-fast branch");
            failure=static_cast<std::int64_t>(p)+6+std::bit_cast<std::int32_t>(u32(p+2)); p+=6;
        }
        const unsigned char fatal[]={0xb9,7,0,0,0,0xcd,0x29};
        check(failure>=0 && static_cast<std::uint64_t>(failure)+sizeof(fatal)<=bytes.size() &&
            std::memcmp(bytes.data()+failure,fatal,sizeof(fatal))==0,"Unpublished export does not directly fail-fast");
        check(bytes[p]==0x41 && bytes[p+1]==0xff,"Not leaf indirect jump");
        std::uint32_t displacement=0;
        if (bytes[p+2]==0x23) displacement=0;
        else if (bytes[p+2]==0x63) displacement=bytes[p+3];
        else { check(bytes[p+2]==0xa3,"Unexpected indirect dispatch addressing"); displacement=u32(p+3); }
        check(displacement==slot*8,".def/assembly/C++ dispatch slot mismatch");
    };
    for (unsigned ordinal=1;ordinal<=38;++ordinal) thunk(ordinal,ordinal-1);
    for (unsigned ordinal=100;ordinal<=103;++ordinal) thunk(ordinal,ordinal-100+38);
    for (unsigned ordinal=39;ordinal<=255;++ordinal)
        if (ordinal<100 || ordinal>103) check(u32(function_table+(ordinal-base)*4)==0,"Unexpected ordinal alias");
    std::array<bool,std::size(names)> seen{};
    for (unsigned i=0;i<count;++i) {
        const auto pos=offset(u32(name_table+i*4)); std::size_t n=0;
        while (n<96 && pos+n<bytes.size() && bytes[pos+n]) ++n;
        check(n<96 && pos+n<bytes.size(),"Unbounded export name");
        const std::string name(reinterpret_cast<const char*>(bytes.data()+pos),n);
        std::size_t index=0; while(index<std::size(names) && name!=names[index].name) ++index;
        check(index<std::size(names) && !seen[index],"Unexpected/duplicate named export"); seen[index]=true;
        const auto ordinal=base+u16(ordinal_table+i*2);
        check(ordinal==256+index,"Wrong named union ordinal"); thunk(ordinal,ordinal_slots+index);
    }
}
} // namespace

// Deliberately different signatures through actual MASM thunks, not C wrappers.
extern "C" std::uint64_t proxy_D3DCompile(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,
    std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
extern "C" double proxy_D3DDisassemble(std::uint64_t,double,std::uint64_t,double,double,std::uint64_t);
extern "C" Large proxy_D3DCreateBlob(std::uint64_t,double,std::uint64_t,std::uint64_t,std::uint64_t);
extern "C" std::uint64_t proxy_ord_3(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,
    std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
extern "C" std::uint64_t proxy_ord_2(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,
    std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
extern "C" int proxy_D3DPERF_BeginEvent(DWORD,LPCWSTR);

int main(int argc,char** argv)
{
    check(argc==1 || argc==2,"Usage: native_proxy_forwarding_selftest [linked-proxy-fixture.dll]");
    profile_tests(); reset();
    check(initialize_forwarding(self_module)==ForwardingStatus::Ready,"ABI fixture init failed");
    SetLastError(0x1234);
    check(proxy_D3DCompile(1,2,3,4,5,6,7,8)==454,"Integer/stack argument or RAX damaged");
    check(GetLastError()==0x5678 && calls==1,"LastError or exactly-once tail forwarding damaged");
    check(proxy_D3DDisassemble(1,2.25,3,4.5,5.75,6)==22.5,"Positional XMM/stack/XMM0 damaged");
    const auto result=proxy_D3DCreateBlob(11,12.0,13,14,15);
    check(result.a==24 && result.b==26 && result.c==15,"Hidden return buffer/shifted arguments damaged");
    SetLastError(0x1234);
    check(proxy_ord_3(1,2,3,4,5,6,7,8)==454,"Compiler ordinal thunk damaged ABI");
    reset(L"xinput1_3.dll"); check(initialize_forwarding(self_module)==ForwardingStatus::Ready,"Ordinal fixture init failed");
    SetLastError(0x1234);
    check(proxy_ord_2(1,2,3,4,5,6,7,8)==454 && calls==1,"Profile-dispatched ordinal damaged ABI");
    check(frees==0,"Published reference was released");
    reset(L"d3d9.dll"); missing_name="Direct3DCreate9On12";
    check(initialize_forwarding(self_module)==ForwardingStatus::Ready && calls==0,"Optional On12 prevented required d3d9 readiness");
    check(targets[59]==&native_proxy_fail_fast && targets[19]==&native_proxy_fail_fast,"Missing On12 name/ordinal not fail-fast");
    check(proxy_D3DPERF_BeginEvent(0x12345678,L"fixture")==37 && calls==1,"Required d3d9 export did not forward once");
    if (argc==2) export_test(argv[1]);
    std::puts("native_proxy_forwarding_selftest passed (loader doubles; actual MASM tail dispatch)");
}
