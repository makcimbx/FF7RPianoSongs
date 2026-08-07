#include "core/startup_thread.h"

namespace ff7r::piano::core {

static_assert(std::atomic<bool>::is_always_lock_free,
    "The loader-lock startup gate must always use lock-free atomics");

StartupThreadResult start_thread_once(
    std::atomic<bool>& claimed,
    const LPTHREAD_START_ROUTINE worker,
    const LPVOID context,
    const CreateThreadFunction create_thread,
    const CloseHandleFunction close_handle) noexcept
{
    bool expected = false;
    if (!claimed.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return StartupThreadResult::AlreadyClaimed;
    }

    const HANDLE thread = create_thread(nullptr, 0, worker, context, 0, nullptr);
    if (!thread) {
        claimed.store(false, std::memory_order_release);
        return StartupThreadResult::CreateFailed;
    }

    close_handle(thread);
    return StartupThreadResult::Started;
}

} // namespace ff7r::piano::core
