#pragma once
#include <windows.h>
#include <cstdint>

namespace ff7r::piano::core {
inline std::int64_t hook_trace_ticks() noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) ? value.QuadPart : 0;
}
enum class HookInstallOutcome { NotAttempted, NotInitialized, AlreadyOwned, SignatureMismatch,
    CreateFailed, EnableFailed, Installed, Prepared, QueueFailed };
struct HookApiTrace {
    std::int64_t begin = 0, end = 0;
    int status = 0;
    bool returned = false;
};
struct HookInstallTrace {
    const char* id = nullptr;
    HookApiTrace create, enable, queue, cancel;
    HookInstallOutcome outcome = HookInstallOutcome::NotAttempted;
    bool cleanup_attempted = false, cleanup_retained = false;
    int cleanup_status = 0;
};
struct HookBatchTrace {
    HookApiTrace apply;
    const char* failed_queue_hook = nullptr;
    bool cancellation_attempted = false, cancellation_completed = false;
    bool process_lifetime_retained = false;
};
} // namespace ff7r::piano::core
