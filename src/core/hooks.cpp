#include "core/hooks.h"

#include "core/pe_image.h"

#include <MinHook.h>

#include <sstream>
#include <utility>

namespace ff7r::piano::core {
namespace {

bool g_minhook_initialized = false;

} // namespace

bool RawRvaHook::install(HMODULE module, uintptr_t rva, const std::vector<uint8_t>& expected_prologue, void* detour, void** original, std::string& error)
{
    if (!g_minhook_initialized) {
        error = "MinHook is not initialized";
        return false;
    }
    if (target_) {
        error = "hook already installed";
        return false;
    }
    if (original) {
        *original = nullptr;
    }
    auto* target = reinterpret_cast<uint8_t*>(module) + rva;
    if (!bytes_equal(target, expected_prologue)) {
        std::ostringstream out;
        out << "prologue mismatch at rva=0x" << std::hex << rva;
        error = out.str();
        return false;
    }
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        if (original) {
            *original = nullptr;
        }
        error = MH_StatusToString(status);
        return false;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        const MH_STATUS remove_status = MH_RemoveHook(target);
        if (remove_status == MH_OK || remove_status == MH_ERROR_NOT_CREATED) {
            if (original) {
                *original = nullptr;
            }
        } else {
            target_ = target;
        }
        std::ostringstream out;
        out << "enable failed: " << MH_StatusToString(status);
        if (remove_status != MH_OK && remove_status != MH_ERROR_NOT_CREATED) {
            out << "; cleanup retained: " << MH_StatusToString(remove_status);
        }
        error = out.str();
        return false;
    }
    target_ = target;
    return true;
}

bool RawRvaHook::disable()
{
    if (!target_) {
        return true;
    }
    const MH_STATUS status = MH_DisableHook(target_);
    return status == MH_OK || status == MH_ERROR_DISABLED;
}

bool RawRvaHook::remove()
{
    if (!target_) {
        return true;
    }
    if (!disable()) {
        return false;
    }
    const MH_STATUS status = MH_RemoveHook(target_);
    if (status != MH_OK && status != MH_ERROR_NOT_CREATED) {
        return false;
    }
    target_ = nullptr;
    return true;
}

HookCallbackGate::Lease::Lease(Lease&& other) noexcept
    : gate_(std::exchange(other.gate_, nullptr))
{
}

HookCallbackGate::Lease& HookCallbackGate::Lease::operator=(Lease&& other) noexcept
{
    if (this != &other) {
        release();
        gate_ = std::exchange(other.gate_, nullptr);
    }
    return *this;
}

HookCallbackGate::Lease::~Lease()
{
    release();
}

void HookCallbackGate::Lease::release()
{
    if (gate_) {
        std::exchange(gate_, nullptr)->leave();
    }
}

HookCallbackGate::Lease HookCallbackGate::try_enter()
{
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock || !accepting_) {
        return {};
    }
    ++callbacks_in_flight_;
    return Lease(this);
}

HookCallbackGate::ExclusiveLease HookCallbackGate::try_suspend_exclusive(
    Lease& caller) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock || !accepting_ || exclusive_ || caller.gate_ != this
        || callbacks_in_flight_ != 1) return {};
    accepting_ = false;
    reopen_after_exclusive_ = true;
    exclusive_ = true;
    callbacks_in_flight_ = 0;
    caller.gate_ = nullptr;
    return ExclusiveLease(this, std::move(lock));
}

HookCallbackGate::ExclusiveLease::ExclusiveLease(ExclusiveLease&& other) noexcept
    : gate_(std::exchange(other.gate_, nullptr)), lock_(std::move(other.lock_)) {}

HookCallbackGate::ExclusiveLease& HookCallbackGate::ExclusiveLease::operator=(
    ExclusiveLease&& other) noexcept
{
    if (this != &other) {
        (void)resume_as_lease();
        gate_ = std::exchange(other.gate_, nullptr);
        lock_ = std::move(other.lock_);
    }
    return *this;
}

HookCallbackGate::ExclusiveLease::~ExclusiveLease()
{
    // A discarded exclusive lease still becomes an ordinary protected
    // callback lease briefly, then releases it through Lease destruction.
    (void)resume_as_lease();
}

HookCallbackGate::Lease HookCallbackGate::ExclusiveLease::resume_as_lease() noexcept
{
    HookCallbackGate* gate = std::exchange(gate_, nullptr);
    if (!gate) return {};
    // try_suspend_exclusive retains this exact gate lock for the bounded
    // adoption transaction, so resume performs no game-thread acquisition.
    gate->exclusive_ = false;
    gate->accepting_ = gate->reopen_after_exclusive_;
    gate->reopen_after_exclusive_ = false;
    ++gate->callbacks_in_flight_;
    gate->drained_.notify_all();
    lock_.unlock();
    return Lease(gate);
}

void HookCallbackGate::open()
{
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = true;
}

void HookCallbackGate::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
    reopen_after_exclusive_ = false;
}

bool HookCallbackGate::drain(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return drained_.wait_for(lock, timeout, [this] {
        return callbacks_in_flight_ == 0 && !exclusive_;
    });
}

bool HookCallbackGate::accepting() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return accepting_;
}

uint32_t HookCallbackGate::callbacks_in_flight() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return callbacks_in_flight_;
}

void HookCallbackGate::leave()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (callbacks_in_flight_ > 0 && --callbacks_in_flight_ == 0) {
        drained_.notify_all();
    }
}

bool HookShutdownResult::ok() const
{
    return gate_closed && hooks_disabled && callbacks_drained && native_state_restored
        && hooks_removed && state_cleared;
}

HookShutdownResult shutdown_gated_hooks(
    HookCallbackGate& gate,
    const std::vector<HookTeardownOperation>& hooks,
    const std::function<bool()>& restore_native_state,
    const std::function<void()>& clear_state,
    std::chrono::milliseconds drain_timeout)
{
    HookShutdownResult result;
    gate.close();
    result.gate_closed = true;

    result.hooks_disabled = true;
    for (const HookTeardownOperation& hook : hooks) {
        result.hooks_disabled = hook.disable && hook.disable() && result.hooks_disabled;
    }
    if (!result.hooks_disabled) {
        return result;
    }

    result.callbacks_drained = gate.drain(drain_timeout);
    if (!result.callbacks_drained) {
        return result;
    }

    result.native_state_restored = !restore_native_state || restore_native_state();
    if (!result.native_state_restored) {
        return result;
    }

    result.hooks_removed = true;
    for (const HookTeardownOperation& hook : hooks) {
        result.hooks_removed = hook.remove && hook.remove() && result.hooks_removed;
    }
    if (!result.hooks_removed) {
        return result;
    }

    if (clear_state) {
        clear_state();
    }
    result.state_cleared = true;
    return result;
}

HookTeardownOperation teardown_operation(RawRvaHook& hook)
{
    return {
        [&hook] { return hook.disable(); },
        [&hook] { return hook.remove(); },
    };
}

bool restore_native_state_transactionally(const std::vector<NativeRestoreOperation>& operations)
{
    for (const auto& operation : operations) {
        if (!operation.validate_redirected || !operation.validate_redirected()) return false;
    }
    std::vector<const NativeRestoreOperation*> restored;
    for (const auto& operation : operations) {
        if (!operation.restore_original || !operation.restore_original()) {
            if (operation.restore_redirected) (void)operation.restore_redirected();
            for (auto it = restored.rbegin(); it != restored.rend(); ++it) {
                if ((*it)->restore_redirected) (void)(*it)->restore_redirected();
            }
            return false;
        }
        restored.push_back(&operation);
    }
    return true;
}

bool aggregate_shutdown_results(const std::vector<HookShutdownResult>& results)
{
    for (const auto& result : results) {
        if (!result.ok()) return false;
    }
    return true;
}

bool initialize_hooks(std::string& error)
{
    if (g_minhook_initialized) {
        return true;
    }
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        error = MH_StatusToString(status);
        return false;
    }
    g_minhook_initialized = true;
    return true;
}

bool shutdown_hooks()
{
    if (!g_minhook_initialized) {
        return true;
    }
    const MH_STATUS status = MH_Uninitialize();
    if (status != MH_OK && status != MH_ERROR_NOT_INITIALIZED) {
        return false;
    }
    g_minhook_initialized = false;
    return true;
}

} // namespace ff7r::piano::core
