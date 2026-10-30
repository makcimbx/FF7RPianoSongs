#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ff7r::piano::game {

namespace native_audio {
struct NativeAudioLayout {
    std::string_view build_id;
    std::size_t bgm_resource, scoreinfo_bgm;
    std::size_t append_slot, backing_slot, serialized_interface_slot;
};
} // namespace native_audio

struct ExtendedChartBuildSpec {
    std::string_view build_id;
    std::array<std::uint8_t, 5> persistent_expand_call;
    std::array<std::uint8_t, 32> fname_signature;
    std::array<std::uintptr_t, 4> callback_vtable_slots;
};

struct RuntimeBuildProfile {
    native_audio::NativeAudioLayout native_audio;
    ExtendedChartBuildSpec extended_chart;
};

namespace generated {
#include "game/generated/runtime_build_profiles.generated.inc"
} // namespace generated

// Inventory lookup only; consumers retain their exact executable/signature gates.
inline constexpr const RuntimeBuildProfile* find_runtime_build_profile(
    std::string_view build_id) noexcept
{
    for (const auto& profile : generated::kRuntimeBuildProfiles) {
        if (profile.native_audio.build_id == build_id) return &profile;
    }
    return nullptr;
}

} // namespace ff7r::piano::game
