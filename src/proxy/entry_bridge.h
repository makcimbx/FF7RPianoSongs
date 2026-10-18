#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ff7rp::proxy {

inline constexpr std::size_t kEntryJumpBytes=14;
inline constexpr std::size_t kMaxEntrySignature=32;

// Supplied from the supported executable catalog, not inferred from a nearby build.
struct EntryProfile {
    std::uint32_t timestamp=0, image_size=0, checksum=0, entry_rva=0;
    std::uint8_t signature_size=0;
    std::array<std::uint8_t,kMaxEntrySignature> signature{};
};
enum class EntryStage : std::uint32_t { Empty, Arming, Rejected, Armed, Restoring, Initializing, Completed, FailedRetained };
enum class EntryError : std::uint32_t {
    None, Attach, Duplicate, Module, Profile, Image, Code, Protection,
    Patch, Flush, Rollback, EntryOwnership, Restore, Reentry, Callback, Stopped
};
enum class EntryRetention : std::uint32_t { Unreferenced, ProcessLifetime };
struct EntryResult {
    EntryStage stage=EntryStage::Empty;
    EntryError error=EntryError::None;
    EntryRetention retention=EntryRetention::Unreferenced;
    bool armed() const noexcept { return stage==EntryStage::Armed; }
};
struct EntryProof {
    EntryProfile profile{};
    std::uintptr_t image=0, proxy=0, entry=0;
    std::uint32_t attach_thread=0, entry_thread=0;
    std::array<std::uint8_t,kEntryJumpBytes> original{};
    bool restored=false, callback_invoked=false, callback_succeeded=false;
};

class EntryScope final {
public:
    EntryScope(const EntryScope&)=delete;
    EntryScope& operator=(const EntryScope&)=delete;
    // Valid only synchronously in the initializer on this actual entry thread.
    bool capture(EntryProof& out) const noexcept;
private:
    EntryScope() noexcept=default;
    friend struct EntryBridgeAccess;
};
using EntryInitializer=bool (*)(const EntryScope&,void*) noexcept;
const EntryScope* active_entry_scope() noexcept;

// Initializer/context must remain valid until entry; the callback runs synchronously.
// Only DLL_PROCESS_ATTACH with the real nonnull process-start lpReserved qualifies.
// No game calls, DLL loading, allocation, new threads or waits occur here. The caller
// supplies the genuine OS attach arguments; cold pre-entry load ordering must be
// qualified separately. The 14-byte transaction is not a live/atomic patch API.
// Caller must retain this module for process lifetime whenever the result says so, even
// on failure; returning FALSE from DllMain then would be unsafe.
EntryResult arm_process_start_entry(void* proxy_module,std::uint32_t attach_reason,
    const void* loader_reserved,const EntryProfile&,EntryInitializer,void* context) noexcept;
EntryResult entry_bridge_status() noexcept;
// Never unpatch/unload a published thunk. Stop-before-entry skips initialization
// at the actual entry and still resumes restored original code; no waiting.
EntryResult stop_entry_bridge() noexcept;
// Copied evidence is not an active scope or native-audio readiness authority.
bool completed_entry_proof(EntryProof&) noexcept;

} // namespace ff7rp::proxy
