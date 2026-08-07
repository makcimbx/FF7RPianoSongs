#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ff7r::piano::game::piano_page_selection_guard {

inline constexpr std::size_t kMarkerCapacity = 8;
inline constexpr std::size_t kSongIdCapacity = 96;

struct Marker final {
    std::uint64_t playback_generation = 0;
    std::uint64_t catalog_revision = 0;
    std::int32_t selected_index = -1;
    std::int32_t profile_index = -1;
    std::int32_t visible_index = -1;
    std::int32_t base_slot = -1;
    std::int32_t difficulty = 0;
    std::int32_t note_count = 0;
    bool playback_available = false;
    bool catalog_revision_exact = false;
    bool unexpected_negative = false;
    std::array<char, kSongIdCapacity> song_id{};
};

struct MarkerIdentity final {
    std::uint64_t playback_generation = 0;
    std::int32_t selected_index = -1;
    bool operator==(const MarkerIdentity&) const = default;
};

template <std::size_t Capacity = kMarkerCapacity>
class BoundedMarkerSet final {
public:
    bool insert(const MarkerIdentity& identity) noexcept
    {
        if (size_ == Capacity) return false;
        for (std::size_t index = 0; index < size_; ++index) {
            if (identities_[index] == identity) return false;
        }
        identities_[size_++] = identity;
        return true;
    }
    std::size_t size() const noexcept { return size_; }
private:
    std::array<MarkerIdentity, Capacity> identities_{};
    std::size_t size_ = 0;
};

using MarkerCapture = std::function<bool(Marker&)>;
using MarkerSink = std::function<void(const Marker&)>;
void capture_and_emit_marker(std::int32_t selected_index,
    const MarkerCapture& capture, const MarkerSink& sink) noexcept;

using ResolverAction = std::function<void()>;
using ContinuationAction = std::function<void()>;
void dispatch_selected_index(std::int32_t selected_index,
    const ResolverAction& resolver, const ContinuationAction& continuation) noexcept;

std::vector<std::uint8_t> make_guard_relay(const void* marker,
    const void* resolver, const void* continuation);
bool decode_rel32_target(const void* instruction,
    const std::array<std::uint8_t, 5>& bytes, const void*& target) noexcept;
bool make_rel32_call(const void* instruction, const void* target,
    std::array<std::uint8_t, 5>& bytes) noexcept;

struct PatchOperations final {
    std::function<bool(const void*, void*, std::size_t)> read;
    std::function<bool(void*, const void*, std::size_t)> write;
    std::function<bool(void*, std::size_t, unsigned long, unsigned long*)> protect;
    std::function<bool(const void*, std::size_t)> flush;
    std::function<bool(const void*, unsigned long&)> query_protection;
};

enum class Rel32PatchState { Original, Replacement, Unsafe };
enum class InstallResult { Installed, Unavailable, UnsafeMutation };

bool replace_rel32_call_transactionally(void* call_site,
    const std::array<std::uint8_t, 5>& original, const void* original_target,
    const void* replacement_target, const PatchOperations& operations,
    Rel32PatchState& state, std::string& error) noexcept;

InstallResult install(HMODULE exe_module) noexcept;

} // namespace ff7r::piano::game::piano_page_selection_guard
