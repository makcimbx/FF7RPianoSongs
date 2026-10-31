#pragma once

#include "game/native_audio_bootstrap.h"
#include <span>

namespace ff7r::piano::game::native_audio {

enum class ProxyBootstrapFailure : unsigned char {
    None, Attempt, Modules, Files, Image, Exports, Frames, Code, Context,
    Cold, Publication, Exception
};
struct ProxyBootstrapSnapshot {
    ProxyBootstrapFailure failure=ProxyBootstrapFailure::None;
    const char* predicate=nullptr;
    DWORD thread=0;
    bool callback_invoked=false,barriers_published=false;
    bool post_crt=false;
};
// Only the linked owned proxy's actual private entry or admitted CALL scope.
// Success is synchronous callback invocation, NOT origin or audio readiness.
// Without FF7RP_OWNED_PROXY this API and its scope seams are unavailable.
// Retain this single module and all published hooks until process exit or safe teardown.
bool try_owned_proxy_native_audio_startup(HMODULE exe,HMODULE own,
    std::uintptr_t actual_caller,NativeStartupCallback initialize) noexcept;
ProxyBootstrapSnapshot native_proxy_bootstrap_status() noexcept;

namespace native_proxy_detail {
struct EntryContract {std::uintptr_t rva=0;std::span<const std::uint8_t> original{};};
// These private implementation seams cannot create a startup scope.
EntryContract entry_contract() noexcept;
bool cold_entry_ready(HMODULE exe) noexcept;
bool scope_present() noexcept;
bool post_crt_scope_present() noexcept;
bool begin_publication(HMODULE exe) noexcept;
bool finish_publication(HMODULE exe,std::uintptr_t owned_entry_detour) noexcept;
void abandon_publication(HMODULE exe) noexcept;
}
}
