#include "entry_bridge.h"
#include "forwarding.h"
#include "native_proxy.h"
#include "proxy/generated/native_proxy_profile.generated.h"
#include "game/native_proxy_bootstrap.h"
#include "game/native_audio_runtime.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <cstdio>

namespace {
namespace proxy = ff7r::piano::proxy;
namespace bridge = ff7rp::proxy;
HMODULE self_module = nullptr;
wchar_t directory[32768]{};
std::size_t directory_length = 0;

struct FailureFileApi {
    decltype(&CreateFileW) create;
    decltype(&WriteFile) write;
    decltype(&FlushFileBuffers) flush;
    decltype(&CloseHandle) close;
};
const FailureFileApi failure_file_api{CreateFileW, WriteFile, FlushFileBuffers, CloseHandle};
#ifdef FF7RP_PROXY_STARTUP_DIAGNOSTIC_SELFTEST
const FailureFileApi* test_failure_file_api = nullptr;
#endif
const FailureFileApi& failure_files() noexcept
{
#ifdef FF7RP_PROXY_STARTUP_DIAGNOSTIC_SELFTEST
    if (test_failure_file_api) return *test_failure_file_api;
#endif
    return failure_file_api;
}

// Copied failure facts only; this does not revalidate or grant startup authority.
bool persist_startup_failure(const ff7r::piano::game::native_audio::ProxyBootstrapSnapshot& status,
    const bridge::EntryProof& proof, const bridge::EntryResult& state) noexcept
{
    HANDLE file = INVALID_HANDLE_VALUE;
    bool complete = false;
    try {
        constexpr wchar_t filename[] = L"FF7RPianoSongs.proxy-startup-failure.log";
        wchar_t path[std::size(directory)]{};
        if (!directory_length || directory_length > std::size(path) - std::size(filename) ||
            directory[directory_length - 1] != L'\\') return false;
        std::memcpy(path, directory, directory_length * sizeof(wchar_t));
        std::memcpy(path + directory_length, filename, sizeof(filename));

        char predicate[97]{};
        std::size_t used = 0;
        if (status.predicate) {
            while (used < sizeof(predicate) - 1 && status.predicate[used]) {
                const auto c = static_cast<unsigned char>(status.predicate[used]);
                predicate[used++] = c >= 32 && c <= 126 ? static_cast<char>(c) : '_';
            }
        } else std::memcpy(predicate, "none", 5);
        char record[1024]{};
        const int count = std::snprintf(record, sizeof(record),
            "[proxy_startup_failure] schema=1 outcome=owned_entry_startup_rejected readiness=unavailable\r\n"
            "failure=%u predicate=%s predicate_at_limit=%u thread=%lu callback_invoked=%u barriers_published=%u\r\n"
            "bridge_stage=%u bridge_error=%u bridge_retention=%u restored=%u callback_invoked=%u callback_succeeded=%u "
            "attach_thread=%lu entry_thread=%lu entry_rva=0x%llx profile_timestamp=0x%lx profile_image_size=0x%lx profile_checksum=0x%lx\r\n",
            static_cast<unsigned>(status.failure), predicate, used == sizeof(predicate) - 1 ? 1u : 0u,
            status.thread, status.callback_invoked ? 1u : 0u, status.barriers_published ? 1u : 0u,
            static_cast<unsigned>(state.stage), static_cast<unsigned>(state.error), static_cast<unsigned>(state.retention),
            proof.restored ? 1u : 0u, proof.callback_invoked ? 1u : 0u, proof.callback_succeeded ? 1u : 0u,
            static_cast<unsigned long>(proof.attach_thread), static_cast<unsigned long>(proof.entry_thread),
            static_cast<unsigned long long>(proof.profile.entry_rva), static_cast<unsigned long>(proof.profile.timestamp),
            static_cast<unsigned long>(proof.profile.image_size), static_cast<unsigned long>(proof.profile.checksum));
        if (count <= 0 || static_cast<std::size_t>(count) >= sizeof(record)) return false;

        const auto& api = failure_files();
        file = api.create(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        complete = api.write(file, record, static_cast<DWORD>(count), &written, nullptr) != FALSE &&
            written == static_cast<DWORD>(count) && api.flush(file) != FALSE;
    } catch (...) { complete = false; }
    // Even a short/failed record remains owned evidence; never truncate, delete or retry it.
    if (file != INVALID_HANDLE_VALUE) {
        try { if (!failure_files().close(file)) complete = false; }
        catch (...) { complete = false; }
    }
    return complete;
}

bool copy_bytes(const void* source, void* target, std::size_t bytes) noexcept
{
    __try { std::memcpy(target, source, bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool equal_ascii(const char* a, const wchar_t* b) noexcept
{
    for (std::size_t i = 0; i < 64; ++i) {
        auto x = static_cast<unsigned char>(a[i]);
        auto y = static_cast<unsigned>(b[i]);
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= L'A' && y <= L'Z') y += L'a' - L'A';
        if (x != y) return false;
        if (x == 0) return true;
    }
    return false;
}

// A runtime LoadLibrary or an arbitrary renamed file is not a static origin.
bool has_normal_import(HMODULE exe, const wchar_t* basename) noexcept
{
    const auto base = reinterpret_cast<std::uintptr_t>(exe);
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!base || !copy_bytes(exe, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 0x100000 ||
        !copy_bytes(reinterpret_cast<void*>(base + dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage != proxy::generated::kImageSize) return false;
    const auto range = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!range.VirtualAddress || range.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        range.Size > 4096 * sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        range.VirtualAddress > nt.OptionalHeader.SizeOfImage ||
        range.Size > nt.OptionalHeader.SizeOfImage - range.VirtualAddress) return false;
    bool found = false;
    for (std::size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= range.Size;
         offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        IMAGE_IMPORT_DESCRIPTOR item{};
        if (!copy_bytes(reinterpret_cast<void*>(base + range.VirtualAddress + offset), &item, sizeof(item))) return false;
        if (!item.Name && !item.FirstThunk && !item.OriginalFirstThunk) return found;
        if (!item.Name || item.Name >= nt.OptionalHeader.SizeOfImage) return false;
        char name[64]{};
        bool terminated = false;
        for (std::size_t i = 0; i < std::size(name); ++i) {
            if (i >= nt.OptionalHeader.SizeOfImage - item.Name ||
                !copy_bytes(reinterpret_cast<void*>(base + item.Name + i), &name[i], 1)) return false;
            if (!name[i]) { terminated = true; break; }
        }
        if (!terminated) return false;
        if (equal_ascii(name, basename)) {
            if (found) return false;
            found = true;
        }
    }
    return false;
}

bool prepare_location(HMODULE self, HMODULE exe) noexcept
{
    wchar_t executable_path[32768]{};
    const auto n = GetModuleFileNameW(self, directory, static_cast<DWORD>(std::size(directory)));
    const auto m = GetModuleFileNameW(exe, executable_path, static_cast<DWORD>(std::size(executable_path)));
    if (!n || n >= std::size(directory) || !m || m >= std::size(executable_path)) return false;
    std::size_t own_split = n, exe_split = m;
    while (own_split && directory[own_split - 1] != L'\\') --own_split;
    while (exe_split && executable_path[exe_split - 1] != L'\\') --exe_split;
    if (!own_split || own_split != exe_split ||
        CompareStringOrdinal(directory, static_cast<int>(own_split), executable_path,
            static_cast<int>(exe_split), TRUE) != CSTR_EQUAL) return false;
    const auto* basename = directory + own_split;
    bool supported = false;
    for (const auto* name : proxy::kProfileNames) {
        if (CompareStringOrdinal(basename, -1, name, -1, TRUE) == CSTR_EQUAL) supported = true;
    }
    if (!supported || !has_normal_import(exe, basename)) return false;
    directory_length = own_split;
    return true;
}

bool initialize_at_entry(const bridge::EntryScope& scope, void*) noexcept
{
    bridge::EntryProof proof{};
    if (!scope.capture(proof) || !proof.restored || proof.proxy != reinterpret_cast<std::uintptr_t>(self_module)) return false;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(self_module), &pinned) || pinned != self_module) {
        OutputDebugStringW(L"FF7RPianoSongs proxy: cannot retain entry module\n");
        RaiseFailFastException(nullptr, nullptr, 0);
        return false;
    }
    if (proxy::initialize_forwarding(self_module) != proxy::ForwardingStatus::Ready) {
        OutputDebugStringW(L"FF7RPianoSongs proxy: System32 forwarding initialization failed\n");
        RaiseFailFastException(nullptr, nullptr, 0);
        return false;
    }
    if (!proxy::initialize_owned_product(reinterpret_cast<HMODULE>(proof.image), self_module)) {
        const auto status = ff7r::piano::game::native_audio::native_proxy_bootstrap_status();
        const auto state = bridge::entry_bridge_status();
        if (!persist_startup_failure(status, proof, state))
            OutputDebugStringW(L"FF7RPianoSongs proxy: startup failure record unavailable or incomplete; existing evidence retained\n");
        OutputDebugStringW(L"FF7RPianoSongs proxy: mod rejected owned-entry startup; forwarding remains ready\n");
        return false;
    }
    return true;
}
} // namespace

BOOL ff7r::piano::proxy::attach_process_start(HMODULE module, DWORD reason, LPVOID reserved) noexcept
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    self_module = module;
    DisableThreadLibraryCalls(module);
    if (!reserved || !prepare_location(module, GetModuleHandleW(nullptr))) return FALSE;
    bridge::EntryProfile profile{};
    profile.timestamp = proxy::generated::kTimestamp;
    profile.image_size = proxy::generated::kImageSize;
    profile.checksum = proxy::generated::kChecksum;
    profile.entry_rva = static_cast<std::uint32_t>(proxy::generated::kEntryRva);
    profile.signature_size = static_cast<std::uint8_t>(proxy::generated::kEntryBytes.size());
    static_assert(proxy::generated::kEntryBytes.size() <= bridge::kMaxEntrySignature);
    for (std::size_t i = 0; i < proxy::generated::kEntryBytes.size(); ++i) profile.signature[i] = proxy::generated::kEntryBytes[i];
    const auto result = bridge::arm_process_start_entry(module, reason, reserved, profile, initialize_at_entry, nullptr,
        ff7r::piano::game::native_audio::emit_native_install_diagnostic);
    return result.armed() || result.retention == bridge::EntryRetention::ProcessLifetime ? TRUE : FALSE;
}
