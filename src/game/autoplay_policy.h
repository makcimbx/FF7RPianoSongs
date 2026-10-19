#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace ff7r::piano::game {

// Values only: the input thread never dereferences native storage.
struct AutoplayPolicy {
    std::uint64_t seat = 0, play = 0;
    std::uintptr_t controller = 0;
    int state = -1;
    bool available = false, seated = false, enabled = false, assisted = false, toggle_allowed = false;

    void revoke() noexcept { seated = enabled = toggle_allowed = false; controller = 0; state = -1; }
    void observe(std::uintptr_t owner, int current, int pending) noexcept {
        if (!available || current == 0 || current == 13 || pending == 13 || pending == 0) {
            revoke(); return;
        }
        if (controller && controller != owner) revoke();
        if (!seated && current == 5) { ++seat; seated = true; controller = owner; }
        if (!seated || controller != owner) return;
        if (current == 9 && state != 9) { ++play; assisted = enabled; }
        state = current;
        toggle_allowed = (current == 5 || current == 9) && (pending == -1 || pending == current);
    }
    bool toggle(std::uint64_t token) noexcept {
        if (!available || !seated || seat != token || !toggle_allowed) return false;
        enabled = !enabled;
        if (enabled && state == 9) assisted = true;
        return true;
    }
};

struct AutoplayInputEdge {
    bool armed = false;
    bool update(bool held, bool eligible) noexcept {
        if (!held) { armed = true; return false; }
        const bool edge = armed;
        armed = false;
        return eligible && edge;
    }
};

// Reserved by the complete synchronous result consumer, not by the lookup.
class AutoplayResultScope {
public:
    explicit AutoplayResultScope(bool assisted) noexcept
        : previous_(current), suppress_(assisted) { current = this; }
    ~AutoplayResultScope() { current = previous_; }
    AutoplayResultScope(const AutoplayResultScope&) = delete;
    static AutoplayResultScope* active() noexcept { return current; }
    bool suppress() const noexcept { return suppress_; }
    void* redirect(const void* source) noexcept {
        if (!source) return nullptr;
        if (!seeded_) { std::memcpy(record_.data(), source, record_.size()); seeded_ = true; }
        return record_.data();
    }
private:
    inline static thread_local AutoplayResultScope* current = nullptr;
    AutoplayResultScope* previous_;
    bool suppress_, seeded_ = false;
    alignas(16) std::array<std::uint8_t, 16> record_{};
};

struct AutoplayMarkerJournal {
    void* event = nullptr;
    bool introduced = false;
    template<class Write> bool discharge(Write&& write) noexcept {
        if (!introduced) return true;
        if (!write(event, std::uint8_t{0})) return false;
        introduced = false;
        event = nullptr;
        return true;
    }
};
}
