#pragma once

#include "game/native_audio_bindings.h"

namespace ff7r::piano::game::native_audio {

// Captured ONLY on the actual initializer's normal return. No saved stack or
// synthesized initializer receiver is retained. Invalidation is caller-owned.
struct MountIdentity {
    Address owner = 0, dispatcher = 0, implementation = 0, closure = 0;
    std::uint64_t subscription = 0;
    std::uint32_t generation = 0;
    bool operator==(const MountIdentity&) const = default;
};
bool read_mount_identity(MemoryReader, Address image_base, Address actual_owner,
    MountIdentity&) noexcept;

struct PackageObservation {
    Address value = 0;
    std::array<unsigned char, 32> metadata{};
    bool operator==(const PackageObservation&) const = default;
};
// False means unreadable, malformed or drifting. Success with value==0 is a
// complete bounded absence observation, not permission to mount by itself.
bool read_package_store(MemoryReader, Address embedded_store, std::uint64_t package_id,
    PackageObservation&) noexcept;

} // namespace ff7r::piano::game::native_audio
