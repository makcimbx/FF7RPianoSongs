#pragma once

#include "game/song_registry.h"
#include "game/uobject_lifetime.h"

#include <cstdint>
#include <string>

namespace ff7r::piano::game {

struct CompletionCapture {
    void* wrapper = nullptr;
    UObjectLiveHandle wrapper_identity{};
    void* owner = nullptr;
    UObjectLiveHandle owner_identity{};
    uint64_t owner_generation = 0;
    uint64_t registry_generation = 0;
    uint64_t capture_id = 0;
    std::string song_id;
    int profile_index = -1;
    int difficulty = 0;
    uint64_t descriptor_hash = 0;
    float descriptor_seconds = 0.0f;
    float prompt_seconds = 0.0f;
    float native_chart_max_seconds = 0.0f;
    float target_seconds = 0.0f;
    bool wrapper_identity_valid = false;
    CustomContextToken playback_token{};
};

struct CompletionCaptureIdentity {
    void* wrapper = nullptr;
    UObjectLiveHandle wrapper_identity{};
    void* owner = nullptr;
    UObjectLiveHandle owner_identity{};
    uint64_t owner_generation = 0;
    uint64_t owner_registry_generation = 0;
    uint64_t registry_generation = 0;
    std::string song_id;
    int profile_index = -1;
    int difficulty = 0;
    uint64_t descriptor_hash = 0;
    float target_seconds = 0.0f;
    bool wrapper_identity_valid = false;
    CustomContextToken playback_token{};
};

inline bool completion_capture_matches(
    const CompletionCapture& capture,
    const CompletionCaptureIdentity& current) noexcept
{
    const bool wrapper_identity_matches = !capture.wrapper_identity_valid
        || (current.wrapper_identity_valid
            && current.wrapper_identity.internal_index == capture.wrapper_identity.internal_index
            && current.wrapper_identity.serial_number == capture.wrapper_identity.serial_number);
    return current.wrapper == capture.wrapper
        && wrapper_identity_matches
        && current.owner == capture.owner
        && current.owner_identity.internal_index == capture.owner_identity.internal_index
        && current.owner_identity.serial_number == capture.owner_identity.serial_number
        && current.owner_generation == capture.owner_generation
        && current.owner_registry_generation == capture.registry_generation
        && current.registry_generation == capture.registry_generation
        && current.song_id == capture.song_id
        && current.profile_index == capture.profile_index
        && current.difficulty == capture.difficulty
        && current.descriptor_hash == capture.descriptor_hash
        && current.target_seconds == capture.target_seconds
        && current.playback_token == capture.playback_token;
}

} // namespace ff7r::piano::game
