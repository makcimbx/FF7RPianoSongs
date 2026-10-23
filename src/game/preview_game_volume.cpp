#include "game/preview_game_volume.h"

#include "core/pe_image.h"
#include "core/version_gate.h"
#include "game/generated/rvas.generated.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace ff7r::piano::game {
namespace {
using namespace preview_game_volume_detail;
using Address = std::uintptr_t;
template<class T> bool read(Memory m, Address base, Address offset, T& out) noexcept
{
    constexpr auto max = std::numeric_limits<Address>::max();
    return base && offset <= max - base && sizeof(T) - 1 <= max - (base + offset)
        && m.read && m.read(m.context, base + offset, &out, sizeof(T));
}
struct State {
    Address array = 0;
    std::int32_t count = 0, shutdown = 0;
    std::uint8_t initialized = 0;
    bool operator==(const State&) const = default;
};
bool state(Memory m, Address base, State& s) noexcept
{
    return read(m, base, rva::AudioCategoryArray, s.array)
        && read(m, base, rva::AudioCategoryCount, s.count)
        && read(m, base, rva::AudioShutdown, s.shutdown)
        && read(m, base, rva::AudioInitialized, s.initialized);
}
struct Category {
    Address envelopes = 0, name = 0;
    std::int32_t id = -1, layers = 0;
    bool operator==(const Category&) const = default;
};
bool category(Memory m, Address p, Category& c) noexcept
{
    return read(m, p, 0x38, c.envelopes) && read(m, p, 0x60, c.id)
        && read(m, p, 0x78, c.name) && read(m, p, 0x80, c.layers);
}
bool bgm(Memory m, const Category& c) noexcept
{
    std::array<char, 4> name{};
    return read(m, c.name, 0, name) && name == std::array<char, 4>{'b','g','m',0};
}
}

PreviewGameVolume preview_game_volume_detail::project(Memory m, Address base, std::size_t size,
    std::string_view profile, bool supported) noexcept
{
    if (!supported || profile != FF7RP_TARGET_BUILD_ID || !base
        || size > std::numeric_limits<Address>::max() - base) return {};
    for (const auto r : {rva::AudioCategoryArray, rva::AudioCategoryCount,
             rva::AudioShutdown, rva::AudioInitialized}) {
        if (!r || r >= size || sizeof(Address) > size - r) return {};
    }
    State before{};
    if (!state(m, base, before)) return {};
    if (before.initialized == 0 || before.shutdown != 0 || !before.array || before.count == 0)
        return {PreviewGameVolumeState::NotReady};
    if (before.initialized != 1 || before.count < 0 || before.count > 256) return {};
    std::array<Address, 256> slots{}, after_slots{};
    const auto bytes = static_cast<std::size_t>(before.count) * sizeof(Address);
    if (bytes - 1 > std::numeric_limits<Address>::max() - before.array
        || !m.read || !m.read(m.context, before.array, slots.data(), bytes)) return {};
    Address selected = 0;
    Category selected_facts{};
    for (int i = 0; i < before.count; ++i) {
        const auto p = slots[i];
        if (!p) continue;
        for (int j = 0; j < i; ++j) if (slots[j] == p) return {};
        std::int32_t id = -1;
        if (!read(m, p, 0x60, id)) return {};
        if (id != 31) continue;
        if (selected || !category(m, p, selected_facts) || selected_facts.id != 31
            || !bgm(m, selected_facts)) return {};
        selected = p;
    }
    if (!selected || selected_facts.layers < 4 || selected_facts.layers > 32) return {};
    // Config=2 is record3 (stride 0x30); target already includes master.
    float target = 0, after_target = 0;
    if (!read(m, selected_facts.envelopes, 0x9c, target)
        || !std::isfinite(target) || target < 0 || target > 1) return {};
    Category after_category{};
    State after{};
    if (!category(m, selected, after_category) || after_category != selected_facts
        || !bgm(m, after_category) || !read(m, after_category.envelopes, 0x9c, after_target)
        || after_target != target || !m.read(m.context, before.array, after_slots.data(), bytes)
        || std::memcmp(slots.data(), after_slots.data(), bytes)
        || !state(m, base, after) || after != before) return {};
    return {PreviewGameVolumeState::Ready, target};
}

PreviewGameVolume capture_preview_game_volume() noexcept
{
    const auto module = GetModuleHandleW(nullptr);
    if (!module || !core::is_supported_exe(module)) return {};
    const auto image = core::image_range(module);
    const preview_game_volume_detail::Memory memory{nullptr,
        [](void*, Address p, void* out, std::size_t n) noexcept {
            SIZE_T copied = 0;
            return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(p),
                out, n, &copied) && copied == n;
        }};
    // OS copies tolerate teardown; repeated identities reject detected drift, not ABA.
    // Only a bounded settings scalar escapes, never a borrowed pointer or native call.
    return preview_game_volume_detail::project(memory, reinterpret_cast<Address>(image.base),
        image.size, FF7RP_TARGET_BUILD_ID, true);
}
} // namespace ff7r::piano::game
