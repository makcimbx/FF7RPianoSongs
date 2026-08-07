#pragma once

#include <cstdint>
#include <type_traits>

namespace ff7r::piano::game {

struct DecodedOnMemoryBankToken {
    uint8_t type = 0;
    uint16_t index = 0;
    uint32_t generation = 0;

    constexpr uint64_t encode() const noexcept
    {
        return static_cast<uint64_t>(type)
            | (static_cast<uint64_t>(index) << 16u)
            | (static_cast<uint64_t>(generation) << 32u);
    }
};

constexpr bool decode_onmemory_bank_token(
    const uint64_t token, DecodedOnMemoryBankToken& decoded) noexcept
{
    // Byte 1 is reserved in the supported token layout. Refuse to normalize an
    // unknown layout because the retained decoded token must reproduce the exact key.
    if ((token & 0x000000000000ff00ULL) != 0) return false;
    decoded.type = static_cast<uint8_t>(token);
    decoded.index = static_cast<uint16_t>(token >> 16u);
    decoded.generation = static_cast<uint32_t>(token >> 32u);
    return decoded.encode() == token;
}

enum class OnMemoryBankPresence {
    Absent,
    PresentSabf,
    PresentSupportedBank,
    Unexpected,
};

enum class OnMemoryBankDiagnosticRole {
    Original,
    Custom,
};

constexpr OnMemoryBankPresence classify_onmemory_bank_kind(
    const uint32_t bank_kind) noexcept
{
    switch (bank_kind) {
    case 0: return OnMemoryBankPresence::Absent;
    case 1: return OnMemoryBankPresence::PresentSabf;
    case 2: return OnMemoryBankPresence::PresentSupportedBank;
    default: return OnMemoryBankPresence::Unexpected;
    }
}

struct OnMemoryBankDiagnosticPair {
    uint64_t route_generation = 0;
    uint64_t cleanup_generation = 0;
    DecodedOnMemoryBankToken original;
    DecodedOnMemoryBankToken custom;

    constexpr explicit operator bool() const noexcept
    {
        return route_generation != 0 && cleanup_generation != 0;
    }
};

static_assert(std::is_trivially_copyable_v<DecodedOnMemoryBankToken>);
static_assert(std::is_trivially_copyable_v<OnMemoryBankDiagnosticPair>);

struct OnMemoryBankDiagnosticPairState {
    OnMemoryBankDiagnosticPair pair;

    void retain(const OnMemoryBankDiagnosticPair& candidate) noexcept
    {
        if (candidate) pair = candidate;
    }

    constexpr bool matches(
        const uint64_t route_generation,
        const uint64_t cleanup_generation) const noexcept
    {
        return pair
            && pair.route_generation == route_generation
            && pair.cleanup_generation == cleanup_generation;
    }

    bool copy_exact(
        const uint64_t route_generation,
        const uint64_t cleanup_generation,
        OnMemoryBankDiagnosticPair& out) const noexcept
    {
        if (!matches(route_generation, cleanup_generation)) return false;
        out = pair;
        return true;
    }

    bool erase_exact(const OnMemoryBankDiagnosticPair& emitted) noexcept
    {
        if (!matches(emitted.route_generation, emitted.cleanup_generation)) return false;
        pair = {};
        return true;
    }
};

template <typename Lookup, typename Observe>
void observe_onmemory_bank_pair(
    const bool validated_lookup_available,
    Lookup&& lookup,
    const OnMemoryBankDiagnosticPair& pair,
    Observe&& observe)
{
    if (!validated_lookup_available || !pair) return;
    const uint64_t original_token = pair.original.encode();
    const uint32_t original_kind = lookup(original_token);
    observe(OnMemoryBankDiagnosticRole::Original, pair.original, original_kind,
        classify_onmemory_bank_kind(original_kind));
    const uint64_t custom_token = pair.custom.encode();
    const uint32_t custom_kind = lookup(custom_token);
    observe(OnMemoryBankDiagnosticRole::Custom, pair.custom, custom_kind,
        classify_onmemory_bank_kind(custom_kind));
}

} // namespace ff7r::piano::game
