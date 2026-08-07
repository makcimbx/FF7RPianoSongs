#pragma once

#include <cstdint>
#include <functional>

namespace ff7r::piano::game {

struct NativeArrayTuple {
    uintptr_t pointer = 0;
    int32_t count = 0;
    int32_t capacity = 0;
};

inline bool operator==(const NativeArrayTuple& left, const NativeArrayTuple& right) noexcept
{
    return left.pointer == right.pointer
        && left.count == right.count
        && left.capacity == right.capacity;
}

struct NativeArrayTupleAccess {
    std::function<bool(NativeArrayTuple&)> read;
    std::function<bool(uintptr_t)> write_pointer;
    std::function<bool(int32_t)> write_count;
    std::function<bool(int32_t)> write_capacity;
    std::function<void(const NativeArrayTuple&)> observe_verified_state;
};

struct NativeArrayTupleFaultInjection {
    // One-based write index in the direction being tested; zero disables injection.
    int fail_after_forward_write = 0;
    int fail_after_rollback_write = 0;
};

struct NativeArrayTupleTransitionResult {
    bool committed = false;
    bool pre_state_validated = false;
    bool rollback_attempted = false;
    bool rollback_verified = false;
    bool retain_redirected_storage = true;
};

NativeArrayTupleTransitionResult publish_native_array_tuple(
    const NativeArrayTuple& original,
    int32_t original_backing_capacity,
    const NativeArrayTuple& redirected,
    int32_t redirected_backing_capacity,
    const NativeArrayTupleAccess& access,
    const NativeArrayTupleFaultInjection& fault = {});

NativeArrayTupleTransitionResult restore_native_array_tuple(
    const NativeArrayTuple& original,
    int32_t original_backing_capacity,
    const NativeArrayTuple& redirected,
    int32_t redirected_backing_capacity,
    const NativeArrayTupleAccess& access,
    const NativeArrayTupleFaultInjection& fault = {});

} // namespace ff7r::piano::game
