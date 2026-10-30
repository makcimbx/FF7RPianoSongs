#define FF7RP_PROXY_STARTUP_DIAGNOSTIC_SELFTEST
#include "proxy/proxy_main.cpp"
#include <cassert>
#include <string>
#include <iostream>

namespace na = ff7r::piano::game::native_audio;
namespace {
struct Files {
    unsigned creates=0,writes=0,flushes=0,closes=0;
    bool exists=false,open_fail=false,write_fail=false,short_write=false,flush_fail=false,close_fail=false;
    unsigned throw_at=0;
    std::wstring path;
    std::string bytes;
} files;
HANDLE WINAPI create(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE template_file)
{
    ++files.creates;
    assert(access==GENERIC_WRITE && share==0 && !security && disposition==CREATE_NEW && !template_file);
    assert(flags==(FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH));
    files.path=path;
    if(files.throw_at==1)throw 1;
    if(files.exists||files.open_fail)return INVALID_HANDLE_VALUE;
    files.exists=true;return reinterpret_cast<HANDLE>(1);
}
BOOL WINAPI write(HANDLE handle,LPCVOID bytes,DWORD count,LPDWORD written,LPOVERLAPPED overlapped)
{
    ++files.writes;assert(handle==reinterpret_cast<HANDLE>(1)&&!overlapped&&count>0&&count<1024);
    if(files.throw_at==2)throw 2;
    *written=files.short_write?count-1:count;
    files.bytes.assign(static_cast<const char*>(bytes),files.write_fail?0:*written);
    return !files.write_fail;
}
BOOL WINAPI flush(HANDLE handle)
{
    ++files.flushes;assert(handle==reinterpret_cast<HANDLE>(1));
    if(files.throw_at==3)throw 3;return !files.flush_fail;
}
BOOL WINAPI close(HANDLE handle)
{
    ++files.closes;assert(handle==reinterpret_cast<HANDLE>(1));
    if(files.throw_at==4)throw 4;return !files.close_fail;
}
const FailureFileApi api{create,write,flush,close};
void reset()
{
    files={};test_failure_file_api=&api;
    std::memset(directory,0,sizeof(directory));
    constexpr wchar_t path[]=L"C:\\game\\D3DCOMPILER_43.dll";
    std::memcpy(directory,path,sizeof(path));directory_length=8;
}
na::ProxyBootstrapSnapshot status{na::ProxyBootstrapFailure::Code,"callback_or_caller_code_unwind",46032,false,false};
bridge::EntryProof proof()
{
    bridge::EntryProof p{};p.restored=true;p.callback_invoked=true;p.attach_thread=p.entry_thread=46032;
    p.profile.entry_rva=0x216c0a4;p.profile.timestamp=0x6a16ced2;p.profile.image_size=0x99d9000;p.profile.checksum=0x769ea6e;
    return p;
}
bridge::EntryResult state{bridge::EntryStage::Initializing,bridge::EntryError::None,bridge::EntryRetention::ProcessLifetime};
bool persist(){return persist_startup_failure(status,proof(),state);}
}

// Link doubles only: this test must never initialize forwarding, bridge or product.
namespace ff7r::piano::proxy {
ForwardingStatus initialize_forwarding(HMODULE) noexcept {assert(false);return ForwardingStatus::FailedPreviously;}
bool initialize_owned_product(HMODULE,HMODULE) noexcept {assert(false);return false;}
}
namespace ff7rp::proxy {
bool EntryScope::capture(EntryProof&) const noexcept {assert(false);return false;}
EntryResult entry_bridge_status() noexcept {assert(false);return {};}
EntryResult arm_process_start_entry(void*,std::uint32_t,const void*,const EntryProfile&,EntryInitializer,void*,EntryCompletionObserver) noexcept {assert(false);return {};}
}
namespace ff7r::piano::game::native_audio {
void emit_native_install_diagnostic() noexcept {assert(false);}
ProxyBootstrapSnapshot native_proxy_bootstrap_status() noexcept {assert(false);return {};}
}

int main()
{
    reset();assert(persist());
    assert(files.path==L"C:\\game\\FF7RPianoSongs.proxy-startup-failure.log");
    assert(files.creates==1&&files.writes==1&&files.flushes==1&&files.closes==1);
    assert(files.bytes.find("failure=7 predicate=callback_or_caller_code_unwind predicate_at_limit=0 thread=46032 callback_invoked=0 barriers_published=0")!=std::string::npos);
    assert(files.bytes.find("bridge_stage=5 bridge_error=0 bridge_retention=1 restored=1 callback_invoked=1 callback_succeeded=0")!=std::string::npos);
    assert(files.bytes.find("attach_thread=46032 entry_thread=46032 entry_rva=0x216c0a4")!=std::string::npos);
    assert(files.bytes.find("readiness=unavailable")!=std::string::npos);
    const auto first=files.bytes;assert(!persist());assert(files.bytes==first&&files.creates==2&&files.writes==1&&files.closes==1);

    reset();files.exists=true;files.bytes="prior evidence";assert(!persist());assert(files.bytes=="prior evidence"&&!files.writes&&!files.closes);
    reset();files.open_fail=true;assert(!persist());assert(!files.writes&&!files.flushes&&!files.closes);
    for(unsigned fault=0;fault<4;++fault){reset();files.write_fail=fault==0;files.short_write=fault==1;files.flush_fail=fault==2;files.close_fail=fault==3;
        assert(!persist());assert(files.exists&&files.writes==1&&files.closes==1);const auto partial=files.bytes;
        assert(!persist());assert(files.bytes==partial&&files.writes==1&&files.closes==1);}
    for(unsigned fault=1;fault<=4;++fault){reset();files.throw_at=fault;assert(!persist());assert(files.closes==(fault==1?0u:1u));}

    reset();const auto saved=status;status.predicate=nullptr;assert(persist());assert(files.bytes.find("predicate=none ")!=std::string::npos);status=saved;
    reset();std::string long_label(4096,'x');long_label[1]='\n';long_label[2]='\r';status.predicate=long_label.c_str();assert(persist());
    assert(files.bytes.find("predicate=x__")!=std::string::npos&&files.bytes.find("predicate_at_limit=1")!=std::string::npos&&files.bytes.size()<1024);status=saved;
    reset();status={};assert(persist());assert(files.bytes.find("failure=0 predicate=none predicate_at_limit=0 thread=0 callback_invoked=0 barriers_published=0")!=std::string::npos);status=saved;
    for(auto length:{std::size_t{0},std::size_t{32768},std::size_t{1},std::size_t(-1)}){reset();directory_length=length;assert(!persist());assert(!files.creates);}
    reset();directory_length=32768-std::size(L"FF7RPianoSongs.proxy-startup-failure.log");
    for(std::size_t i=0;i<directory_length;++i)directory[i]=L'x';
    directory[0]=L'C';directory[1]=L':';directory[2]=directory[directory_length-1]=L'\\';
    assert(persist());assert(files.path.size()==std::size(directory)-1);
    std::cout<<"native_proxy_startup_diagnostic_selftest passed\n";
}
