#include "core/hooks.h"

#include "core/pe_image.h"

#include <MinHook.h>

#include <sstream>
#include <atomic>
#include <utility>

namespace ff7r::piano::core {
namespace {

bool g_minhook_initialized = false;
std::recursive_mutex g_minhook_operations;
bool g_queue_uncertain = false;
bool g_batch_active = false; // Accessed only under g_minhook_operations.
std::size_t g_aborted_batch_hooks = 0;
std::atomic<bool> g_process_lifetime_retention{false};

} // namespace

bool RawRvaHook::install(HMODULE module, uintptr_t rva, const std::vector<uint8_t>& expected_prologue, void* detour, void** original, std::string& error,
    HookInstallTrace* trace)
{
    std::unique_lock lock(g_minhook_operations, std::try_to_lock);
    if (!lock.owns_lock() || g_batch_active) { error = "hook operation busy"; return false; }
    if (!prepare(module, rva, expected_prologue, detour, original, trace, &error)) return false;
    auto* target = target_;
    if (trace) trace->enable.begin = hook_trace_ticks();
    const MH_STATUS status = MH_EnableHook(target);
    if (trace) { trace->enable.end = hook_trace_ticks(); trace->enable.status = status; trace->enable.returned = true; }
    if (status != MH_OK) {
        if (trace) trace->outcome = HookInstallOutcome::EnableFailed;
        if (hook_trampolines_retained_for_process_lifetime()) {
            if (trace) trace->cleanup_retained = true;
            const bool disabled = disable();
            error = std::string("enable failed: ") + MH_StatusToString(status)
                + "; cleanup=process_lifetime_retained detour_disabled=" + (disabled ? "1" : "0");
            return false;
        }
        const MH_STATUS remove_status = MH_RemoveHook(target);
        if (trace) {
            trace->cleanup_attempted = true; trace->cleanup_status = remove_status;
            trace->cleanup_retained = remove_status != MH_OK && remove_status != MH_ERROR_NOT_CREATED;
        }
        if (remove_status == MH_OK || remove_status == MH_ERROR_NOT_CREATED) {
            target_ = nullptr;
            if (original) *original = nullptr;
        }
        std::ostringstream out;
        out << "enable failed: " << MH_StatusToString(status);
        if (target_) out << "; cleanup retained: " << MH_StatusToString(remove_status);
        error = out.str();
        return false;
    }
    if (trace) trace->outcome = HookInstallOutcome::Installed;
    return true;
}

bool RawRvaHook::prepare(HMODULE module, uintptr_t rva, const std::vector<uint8_t>& expected_prologue,
    void* detour, void** original, HookInstallTrace* trace, std::string* error)
{
    if (!g_minhook_initialized) {
        if (trace) trace->outcome = HookInstallOutcome::NotInitialized;
        if (error) *error = "MinHook is not initialized";
        return false;
    }
    if (target_) {
        if (trace) trace->outcome = HookInstallOutcome::AlreadyOwned;
        if (error) *error = "hook already installed";
        return false;
    }
    if (original) {
        *original = nullptr;
    }
    auto* target = reinterpret_cast<uint8_t*>(module) + rva;
    if (!bytes_equal(target, expected_prologue)) {
        if (trace) trace->outcome = HookInstallOutcome::SignatureMismatch;
        if (error) {
            std::ostringstream out;
            out << "prologue mismatch at rva=0x" << std::hex << rva;
            *error = out.str();
        }
        return false;
    }
    if (trace) trace->create.begin = hook_trace_ticks();
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (trace) { trace->create.end = hook_trace_ticks(); trace->create.status = status; trace->create.returned = true; }
    if (status != MH_OK) {
        if (trace) trace->outcome = HookInstallOutcome::CreateFailed;
        if (original) {
            *original = nullptr;
        }
        if (error) *error = MH_StatusToString(status);
        return false;
    }
    target_ = target;
    if (trace) trace->outcome = HookInstallOutcome::Prepared;
    return true;
}

bool RawRvaHook::disable()
{
    std::unique_lock lock(g_minhook_operations, std::try_to_lock);
    if (!lock.owns_lock() || g_batch_active) return false;
    if (!target_) {
        return true;
    }
    const MH_STATUS status = MH_DisableHook(target_);
    return status == MH_OK || status == MH_ERROR_DISABLED;
}

bool RawRvaHook::remove()
{
    std::unique_lock lock(g_minhook_operations, std::try_to_lock);
    if (!lock.owns_lock() || g_batch_active) return false;
    if (!target_) {
        return true;
    }
    if (!disable()) {
        return false;
    }
    if (queue_uncertain_ || hook_trampolines_retained_for_process_lifetime()) return false;
    const MH_STATUS status = MH_RemoveHook(target_);
    if (status != MH_OK && status != MH_ERROR_NOT_CREATED) {
        return false;
    }
    target_ = nullptr;
    if (aborted_batch_) { aborted_batch_ = false; --g_aborted_batch_hooks; }
    return true;
}

OwnedHookBatch::OwnedHookBatch() : lock_(g_minhook_operations, std::try_to_lock) {
    if (lock_.owns_lock()) {
        if (g_batch_active) lock_.unlock();
        else g_batch_active = true;
    }
}
OwnedHookBatch::~OwnedHookBatch() {
    if (count_ && !cancellation_processed_) (void)cancel();
    if (lock_.owns_lock()) g_batch_active = false;
}
bool OwnedHookBatch::valid() const { return lock_.owns_lock() && g_minhook_initialized && !g_queue_uncertain && !g_aborted_batch_hooks; }
bool OwnedHookBatch::prepare(RawRvaHook& hook, HMODULE module, uintptr_t rva,
    const std::vector<uint8_t>& expected, void* detour, void** original, HookInstallTrace* trace)
{
    if (!valid() || pending_ || cancellation_processed_ || prepare_failed_ || count_ == entries_.size()) return false;
    if (!hook.prepare(module, rva, expected, detour, original, trace, nullptr)) { prepare_failed_ = true; return false; }
    entries_[count_++] = {&hook, trace}; // Own every created target before another API can fail.
    return true;
}
bool OwnedHookBatch::apply(HookBatchTrace* trace)
{
    if (!valid() || !count_ || pending_ || cancellation_processed_ || prepare_failed_) return false;
    pending_ = true;
    for (std::size_t i = 0; i < count_; ++i) {
        const auto& entry = entries_[i];
        if (entry.trace) entry.trace->queue.begin = hook_trace_ticks();
        const auto status = MH_QueueEnableHook(entry.hook->target_);
        if (entry.trace) {
            entry.trace->queue.end = hook_trace_ticks(); entry.trace->queue.returned = true;
            entry.trace->queue.status = status;
            if (status != MH_OK) entry.trace->outcome = HookInstallOutcome::QueueFailed;
        }
        if (status != MH_OK) {
            if (trace && entry.trace) trace->failed_queue_hook = entry.trace->id;
            return false;
        }
    }
    if (trace) trace->apply.begin = hook_trace_ticks();
    const auto status = MH_ApplyQueued(); // May have enabled a prefix even on failure.
    if (trace) { trace->apply.end = hook_trace_ticks(); trace->apply.returned = true; trace->apply.status = status; }
    if (status != MH_OK) {
        // Freeze may relocate IPs into disabled suffix trampolines before any patch fails.
        retain_hook_trampolines_for_process_lifetime();
        if (trace) trace->process_lifetime_retained = true;
        return false;
    }
    pending_ = false;
    cancellation_processed_ = true; // Successful queued state must not be cancelled on destruction.
    for (std::size_t i = 0; i < count_; ++i)
        if (entries_[i].trace) entries_[i].trace->outcome = HookInstallOutcome::Installed;
    return true;
}
bool OwnedHookBatch::cancel(HookBatchTrace* trace)
{
    if (!lock_.owns_lock() || cancellation_processed_) return false;
    cancellation_processed_ = true;
    for (std::size_t i = 0; i < count_; ++i) {
        entries_[i].hook->aborted_batch_ = true;
        ++g_aborted_batch_hooks;
    }
    if (!pending_) return true;
    if (trace) trace->cancellation_attempted = true;
    bool ok = true;
    for (std::size_t i = 0; i < count_; ++i) {
        const auto& entry = entries_[i];
        if (entry.trace) entry.trace->cancel.begin = hook_trace_ticks();
        const auto status = MH_QueueDisableHook(entry.hook->target_);
        if (entry.trace) {
            entry.trace->cancel.end = hook_trace_ticks(); entry.trace->cancel.returned = true; entry.trace->cancel.status = status;
        }
        if (status != MH_OK) { entry.hook->queue_uncertain_ = true; g_queue_uncertain = true; ok = false; }
    }
    pending_ = false;
    if (trace) trace->cancellation_completed = ok;
    return ok;
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
        && hooks_removed && state_cleared && !process_lifetime_retained;
}

HookShutdownResult shutdown_gated_hooks(
    HookCallbackGate& gate,
    const std::vector<HookTeardownOperation>& hooks,
    const std::function<bool()>& restore_native_state,
    const std::function<void()>& clear_state,
    std::chrono::milliseconds drain_timeout)
{
    HookShutdownResult result;
    result.process_lifetime_retained = hook_trampolines_retained_for_process_lifetime();
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

    // Gate drain excludes stock forwarders paused before admission. Do not
    // restore/clear module state or free originals they may still reference.
    if (result.process_lifetime_retained) return result;

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

bool disable_then_close_and_drain(HookCallbackGate& gate,
    const std::function<bool()>& disable, const std::chrono::milliseconds drain_timeout)
{
    const bool disabled = disable && disable();
    // If the detour may still be installed, admission must remain continuously
    // open so every forwarding callback receives a lease for a later retry.
    if (!disabled) return false;
    gate.close();
    return gate.drain(drain_timeout);
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
    std::unique_lock lock(g_minhook_operations, std::try_to_lock);
    if (!lock.owns_lock() || g_batch_active) { error = "hook operation busy"; return false; }
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
    std::unique_lock lock(g_minhook_operations, std::try_to_lock);
    if (!lock.owns_lock() || g_batch_active || g_queue_uncertain || g_aborted_batch_hooks) return false;
    if (hook_trampolines_retained_for_process_lifetime()) return false;
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

void retain_hook_trampolines_for_process_lifetime() noexcept
{
    g_process_lifetime_retention.store(true, std::memory_order_release);
}

bool hook_trampolines_retained_for_process_lifetime() noexcept
{
    return g_process_lifetime_retention.load(std::memory_order_acquire);
}

} // namespace ff7r::piano::core
