#include "startup/native_audio_startup_latch.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using ff7r::piano::startup::NativeAudioStartupLatch;

void check(bool value)
{
    if (!value) std::abort();
}

int main()
{
    {
        NativeAudioStartupLatch latch;
        check(!latch.native_barriers_published());
        check(!latch.native_barriers_published());
        check(latch.native_initialized());
        check(!latch.native_initialized());
        check(!latch.native_barriers_published());
    }
    {
        NativeAudioStartupLatch latch;
        check(!latch.native_initialized());
        check(!latch.native_initialized());
        check(latch.native_barriers_published());
        check(!latch.native_initialized());
        check(!latch.native_barriers_published());
    }
    {
        NativeAudioStartupLatch latch;
        std::atomic<unsigned> starts{0};
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < 32; ++i) {
            threads.emplace_back([&, i] {
                const bool start = i % 2 ? latch.native_barriers_published() : latch.native_initialized();
                if (start) starts.fetch_add(1, std::memory_order_relaxed);
            });
        }
        for (auto& thread : threads) thread.join();
        check(starts.load() == 1);
        // A failed CreateThread is terminal for this readiness observation, not a new signal.
        check(!latch.native_initialized());
        check(!latch.native_barriers_published());
    }
    std::cout << "native_audio_startup_latch_selftest passed\n";
}
