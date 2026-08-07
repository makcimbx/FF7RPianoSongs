#pragma once

#include <windows.h>

#include <atomic>

namespace ff7r::piano::core {

using CreateThreadFunction = HANDLE(WINAPI*)(
    LPSECURITY_ATTRIBUTES,
    SIZE_T,
    LPTHREAD_START_ROUTINE,
    LPVOID,
    DWORD,
    LPDWORD);
using CloseHandleFunction = BOOL(WINAPI*)(HANDLE);

enum class StartupThreadResult {
    Started,
    AlreadyClaimed,
    CreateFailed,
};

// Preconditions: worker, create_thread, and close_handle are non-null. The
// production caller guarantees these inputs, so this seam does not recheck them.
StartupThreadResult start_thread_once(
    std::atomic<bool>& claimed,
    LPTHREAD_START_ROUTINE worker,
    LPVOID context,
    CreateThreadFunction create_thread,
    CloseHandleFunction close_handle) noexcept;

} // namespace ff7r::piano::core
