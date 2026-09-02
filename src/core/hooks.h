#pragma once

#include <windows.h>

#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ff7r::piano::core {

class RawRvaHook {
public:
    RawRvaHook() = default;
    RawRvaHook(const RawRvaHook&) = delete;
    RawRvaHook& operator=(const RawRvaHook&) = delete;
    RawRvaHook(RawRvaHook&& other) = delete;
    RawRvaHook& operator=(RawRvaHook&& other) = delete;
    ~RawRvaHook() = default;

    bool install(HMODULE module, uintptr_t rva, const std::vector<uint8_t>& expected_prologue, void* detour, void** original, std::string& error);
    bool disable();
    bool remove();
    bool installed() const { return target_ != nullptr; }

private:
    void* target_ = nullptr;
};

class HookCallbackGate {
public:
    class ExclusiveLease;
    class Lease {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        ~Lease();

        explicit operator bool() const { return gate_ != nullptr; }

    private:
        friend class HookCallbackGate;
        friend class ExclusiveLease;
        explicit Lease(HookCallbackGate* gate) : gate_(gate) {}
        void release();
        HookCallbackGate* gate_ = nullptr;
    };

    // Temporarily closes admission and converts the caller's sole callback
    // lease into exclusive ownership without waiting.  Resume atomically
    // restores an in-flight lease before native code is called; an intervening
    // shutdown close keeps admission closed but cannot drain past that lease.
    class ExclusiveLease {
    public:
        ExclusiveLease() = default;
        ExclusiveLease(const ExclusiveLease&) = delete;
        ExclusiveLease& operator=(const ExclusiveLease&) = delete;
        ExclusiveLease(ExclusiveLease&& other) noexcept;
        ExclusiveLease& operator=(ExclusiveLease&& other) noexcept;
        ~ExclusiveLease();
        explicit operator bool() const { return gate_ != nullptr; }
        Lease resume_as_lease() noexcept;
    private:
        friend class HookCallbackGate;
        ExclusiveLease(HookCallbackGate* gate, std::unique_lock<std::mutex>&& lock)
            : gate_(gate), lock_(std::move(lock)) {}
        HookCallbackGate* gate_ = nullptr;
        std::unique_lock<std::mutex> lock_;
    };

    Lease try_enter();
    ExclusiveLease try_suspend_exclusive(Lease& caller) noexcept;
    void open();
    void close();
    bool drain(std::chrono::milliseconds timeout);
    bool accepting() const;
    uint32_t callbacks_in_flight() const;

private:
    void leave();

    mutable std::mutex mutex_;
    std::condition_variable drained_;
    bool accepting_ = false;
    bool exclusive_ = false;
    bool reopen_after_exclusive_ = false;
    uint32_t callbacks_in_flight_ = 0;
};

struct HookShutdownResult {
    bool gate_closed = false;
    bool hooks_disabled = false;
    bool callbacks_drained = false;
    bool native_state_restored = false;
    bool hooks_removed = false;
    bool state_cleared = false;

    bool ok() const;
};

struct HookTeardownOperation {
    std::function<bool()> disable;
    std::function<bool()> remove;
};

struct NativeRestoreOperation {
    std::function<bool()> validate_redirected;
    std::function<bool()> restore_original;
    std::function<bool()> restore_redirected;
};

HookShutdownResult shutdown_gated_hooks(
    HookCallbackGate& gate,
    const std::vector<HookTeardownOperation>& hooks,
    const std::function<bool()>& restore_native_state,
    const std::function<void()>& clear_state,
    std::chrono::milliseconds drain_timeout = std::chrono::milliseconds(2000));

HookTeardownOperation teardown_operation(RawRvaHook& hook);

// Shared helpers may receive unrelated callbacks. Disable the detour while its
// callback gate is still admitting/accounting calls, then close and drain it.
bool disable_then_close_and_drain(
    HookCallbackGate& gate,
    const std::function<bool()>& disable,
    std::chrono::milliseconds drain_timeout = std::chrono::milliseconds(2000));
bool restore_native_state_transactionally(const std::vector<NativeRestoreOperation>& operations);
bool aggregate_shutdown_results(const std::vector<HookShutdownResult>& results);

bool initialize_hooks(std::string& error);
bool shutdown_hooks();

} // namespace ff7r::piano::core
