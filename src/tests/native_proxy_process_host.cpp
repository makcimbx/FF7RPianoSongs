#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "proxy/entry_bridge.h"
#include "native_proxy_process_protocol.h"

extern "C" __declspec(dllimport) int fixture_probe(NativeProxyProcessReport*) noexcept;
extern "C" __declspec(dllimport) int fixture_add(int,int) noexcept;

namespace {
template<std::size_t N> void output(const char (&text)[N]) noexcept {
    DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,N-1,&written,nullptr);
}
[[noreturn]] void fail(UINT code) noexcept { output("native proxy process fixture failed\n");ExitProcess(code); }
bool entry_bytes(unsigned char (&bytes)[ff7rp::proxy::kMaxEntrySignature]) noexcept {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    SIZE_T copied=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+nt->OptionalHeader.AddressOfEntryPoint),
        bytes,sizeof(bytes),&copied) && copied==sizeof(bytes);
}
}

extern "C" [[noreturn]] void fixture_host_entry() noexcept {
    namespace p=ff7rp::proxy;
    NativeProxyProcessReport first,second;
    if(!fixture_probe(&first))fail(10);
    if(first.attach_count!=1 || !first.process_start_reserved || first.callback_count!=1
        || first.callback_checks!=kNativeProxyProcessCallbackChecks || first.completed_proof_checks!=1
        || first.probe_in_dllmain || first.active_scope_at_probe
        || first.stage!=static_cast<unsigned>(p::EntryStage::Completed) || first.error!=static_cast<unsigned>(p::EntryError::None)
        || first.retention!=static_cast<unsigned>(p::EntryRetention::ProcessLifetime)
        || first.callback_thread!=GetCurrentThreadId() || first.attach_thread!=first.callback_thread)fail(11);
    if(fixture_add(42,8)!=65)fail(12);
    const auto fixture=GetModuleHandleW(L"native_proxy_process_fixture.dll");
    const auto target=GetModuleHandleW(L"native_proxy_process_target.dll");
    const auto forwarded=GetProcAddress(fixture,"fixture_add");MEMORY_BASIC_INFORMATION region;
    if(!target || !forwarded || VirtualQuery(reinterpret_cast<void*>(forwarded),&region,sizeof(region))!=sizeof(region)
        || region.AllocationBase!=target || reinterpret_cast<int(*)(int,int)>(forwarded)(42,8)!=65)fail(13);
    if(!fixture_probe(&second) || second.callback_count!=1 || second.stage!=first.stage || second.error!=first.error)fail(14);

    unsigned char before[p::kMaxEntrySignature],after[p::kMaxEntrySignature];
    if(!entry_bytes(before))fail(20);
    // A different owned DLL copy exercises genuine lpReserved==NULL after AEP.
    const auto late=LoadLibraryW(L"native_proxy_process_late.dll");if(!late)fail(21);
    const auto probe=reinterpret_cast<NativeProxyProcessProbe>(GetProcAddress(late,"fixture_probe"));
    NativeProxyProcessReport rejected;
    if(!probe || !probe(&rejected))fail(22);
    if(rejected.attach_count!=1 || rejected.process_start_reserved || rejected.callback_count || rejected.completed_proof_checks || rejected.probe_in_dllmain
        || rejected.active_scope_at_probe || rejected.stage!=static_cast<unsigned>(p::EntryStage::Rejected)
        || rejected.error!=static_cast<unsigned>(p::EntryError::Attach)
        || rejected.retention!=static_cast<unsigned>(p::EntryRetention::Unreferenced))fail(23);
    if(!FreeLibrary(late) || !entry_bytes(after))fail(24);
    for(std::size_t i=0;i<sizeof(before);++i)if(before[i]!=after[i])fail(25);
    if(!fixture_probe(&second) || second.callback_count!=1)fail(26);
    output("{\"static_reserved\":true,\"callback_before_host\":true,\"same_thread\":true,\"outside_dllmain\":true,"
        "\"restored_entry\":true,\"callback_count\":1,\"forwarded_result\":65,\"late_attach_rejected\":true,\"late_entry_unchanged\":true}\n");
    ExitProcess(0);
}
