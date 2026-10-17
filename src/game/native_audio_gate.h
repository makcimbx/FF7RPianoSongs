#pragma once

#include <mutex>
#include <shared_mutex>

namespace ff7r::piano::game::native_audio {

// The updater never waits for an entrant while holding an engine lock. Native
// registration tries once after the original updater returned; busy means pending.
// Covered foreign entries wait BEFORE calling their original. Nested same-thread
// native calls inherit the outer admission instead of recursively locking an SRW.
class NativeEntryGate final {
public:
    static bool inside_entry() noexcept { return depth_!=0; }
    class Reader final {
    public:
        explicit Reader(NativeEntryGate& gate) : gate_(gate) {
            if (!depth_) lock_ = std::shared_lock<std::shared_mutex>(gate_.mutex_);
            ++depth_;
        }
        ~Reader() { --depth_; }
        Reader(const Reader&) = delete;
        Reader& operator=(const Reader&) = delete;
    private:
        NativeEntryGate& gate_;
        std::shared_lock<std::shared_mutex> lock_;
    };
    class Writer final {
    public:
        explicit Writer(NativeEntryGate& gate) {
            if (!depth_) lock_ = std::unique_lock<std::shared_mutex>(gate.mutex_, std::try_to_lock);
            if (lock_.owns_lock()) ++depth_;
        }
        ~Writer() { if (lock_.owns_lock()) --depth_; }
        explicit operator bool() const noexcept { return lock_.owns_lock(); }
        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;
    private:
        std::unique_lock<std::shared_mutex> lock_;
    };
private:
    std::shared_mutex mutex_;
    // One process-wide manager gate; no other lock domain may share this depth.
    inline static thread_local unsigned depth_ = 0;
};

} // namespace ff7r::piano::game::native_audio
