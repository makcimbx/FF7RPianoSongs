#pragma once

#include <cstdint>

namespace ff7r::piano::game {

struct FNameValue {
    uint32_t comparison_id = 0;
    uint32_t number = 0;
};

template <typename T>
struct TArrayView {
    T* data = nullptr;
    int32_t num = 0;
    int32_t max = 0;
};

struct CompactWideTextRef {
    void* data = nullptr;
    int32_t length = 0;
};

} // namespace ff7r::piano::game
