#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ff7r::piano::game {

enum class RuntimeLocatorMatchPolicy : std::uint8_t {
    First,
};

enum class RuntimeLocatorDecodeKind : std::uint8_t {
    Rel32,
};

struct RuntimeLocatorPatternSpec {
    const std::uint8_t* bytes = nullptr;
    std::string_view mask;
    std::size_t size = 0;
};

struct RuntimeLocatorDecodeSpec {
    RuntimeLocatorDecodeKind kind = RuntimeLocatorDecodeKind::Rel32;
    std::size_t displacement_offset = 0;
    std::size_t instruction_size = 0;
};

struct RuntimeLocatorAdjustmentSpec {
    const std::uintptr_t* values = nullptr;
    std::size_t size = 0;
};

struct RuntimeLocatorSpec {
    std::string_view id;
    RuntimeLocatorPatternSpec pattern;
    RuntimeLocatorMatchPolicy match_policy = RuntimeLocatorMatchPolicy::First;
    RuntimeLocatorDecodeSpec decode;
    RuntimeLocatorAdjustmentSpec candidate_adjustments;
};

const RuntimeLocatorSpec* find_runtime_locator_spec(std::string_view id) noexcept;

} // namespace ff7r::piano::game
