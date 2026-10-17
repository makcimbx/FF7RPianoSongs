#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

namespace ff7r::piano::game::native_audio {
using NativeStartupCallback = void(*)() noexcept;
enum class BootstrapSource : unsigned char { None, Cookie, PreWinMain, FirstSrw };
enum class BootstrapFailure : unsigned char {
    None, Reused, Arguments, Build, Files, Image, Export, Frames, Unwind,
    Code, Baseline, Thread, Scope, Publication, Exception,
};
enum class BootstrapImageModule : unsigned char { None, Executable, Loader, OwnAsi, Crt, Ntdll };
enum class BootstrapImageStage : unsigned char { None, DiskHeader, DosHeader, NtHeader, Sections, CodeWindow };
struct BootstrapImageFailure {
    BootstrapImageModule module = BootstrapImageModule::None;
    BootstrapImageStage stage = BootstrapImageStage::None;
    const char* field = "none"; // Static label; values below are copied diagnostics, never authority.
    unsigned offset = 0, index = 0; // CodeWindow: window RVA and first failing byte index.
    std::uint64_t expected = 0, observed = 0;
};
struct BootstrapFunctionRecord {
    std::uint32_t begin = 0, end = 0, unwind = 0;
};
enum class BootstrapFrameModule : unsigned char { Unknown, Executable, Loader, OwnAsi };
struct BootstrapFollowingFrame {
    BootstrapFrameModule module = BootstrapFrameModule::Unknown;
    std::uintptr_t return_rva = 0;
    BootstrapFunctionRecord function{};
};
struct BootstrapFrameFailure {
    // First copied failure only; static labels and numeric facts confer no authority.
    const char* predicate = "none";
    unsigned index = 0, export_count = 0, chain_depth = 0, byte_offset = 0;
    std::uintptr_t expected_module = 0, observed_module = 0;
    std::uintptr_t supplied_caller_return_rva = 0;
    BootstrapFunctionRecord expected{}, observed{}, export_entry{}, export_primary{}, last_own{}, last_own_primary{};
    unsigned expected_byte = 0, observed_byte = 0;
    unsigned following_count = 0;
    std::array<BootstrapFollowingFrame,2> following{};
    std::uintptr_t expected_register = 0, observed_register = 0;
};
struct BootstrapSnapshot {
    BootstrapSource source = BootstrapSource::None;
    BootstrapFailure failure = BootstrapFailure::None;
    DWORD thread = 0;
    unsigned frames = 0;
    bool callback_invoked = false;
    bool barriers_published = false;
    unsigned failed_frame = 0;
    std::uintptr_t observed_return_rva = 0, expected_return_rva = 0;
    BootstrapImageFailure image{};
    BootstrapFrameFailure frame{};
};
// Only the actual synchronous InitializeASI export may use this. No callback on
// rejection; no worker/late retry. Success means callback invocation, NOT audio
// readiness. The module and native owners remain process-lifetime retained.
bool try_synchronous_native_audio_startup(HMODULE exe_module, HMODULE asi_module,
    std::uintptr_t actual_export_caller, NativeStartupCallback initialize) noexcept;
BootstrapSnapshot native_audio_bootstrap_status() noexcept;

namespace bootstrap_detail {
// These cannot create authority: each requires the helper's private same-thread
// scope and re-unwinds/revalidates the live continuation. Runtime install only.
bool scope_present() noexcept;
bool begin_publication(HMODULE exe_module) noexcept;
bool finish_publication(HMODULE exe_module) noexcept;
void abandon_publication(HMODULE exe_module) noexcept;
}
}
