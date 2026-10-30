#pragma once

#include <atomic>

namespace ff7r::piano::startup {

// Neither early hook publication nor a native initializer alone may start a worker.
class NativeAudioStartupLatch {
public:
    bool native_barriers_published() noexcept { return observe(1); }
    bool native_initialized() noexcept { return observe(2); }

private:
    bool observe(unsigned signal) noexcept
    {
        const auto before = observed_.fetch_or(signal, std::memory_order_acq_rel);
        return before != 3 && (before | signal) == 3;
    }

    std::atomic<unsigned> observed_{0};
};

} // namespace ff7r::piano::startup
