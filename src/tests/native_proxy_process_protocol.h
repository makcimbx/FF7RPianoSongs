#pragma once
#include <cstdint>

// Test-only POD ABI shared by the independently linked host and dependency DLL.
struct NativeProxyProcessReport {
    std::uint32_t attach_count, attach_thread, callback_thread, callback_count;
    std::uint32_t stage, error, retention, callback_checks;
    std::uint32_t process_start_reserved, probe_in_dllmain, active_scope_at_probe;
    std::uint32_t completed_proof_checks;
};
inline constexpr std::uint32_t kNativeProxyProcessCallbackChecks=0x7f;
using NativeProxyProcessProbe=int (*)(NativeProxyProcessReport*);
