#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ff7r::piano::game {
struct SongDescriptor;

namespace audio_sead_detail {

constexpr size_t kSeadPayloadHeaderSize = 0x10;

#ifdef FF7RP_AUDIO_SIDECAR_SELFTEST
void audio_sidecar_selftest_note_free() noexcept;
#endif

class MabfVirtualAllocation {
public:
    MabfVirtualAllocation() = default;
    MabfVirtualAllocation(const MabfVirtualAllocation&) = delete;
    MabfVirtualAllocation& operator=(const MabfVirtualAllocation&) = delete;

    MabfVirtualAllocation(MabfVirtualAllocation&& other) noexcept
        : allocation_(other.allocation_)
        , allocation_size_(other.allocation_size_)
        , mabf_size_(other.mabf_size_)
    {
        other.allocation_ = nullptr;
        other.allocation_size_ = 0;
        other.mabf_size_ = 0;
    }

    MabfVirtualAllocation& operator=(MabfVirtualAllocation&& other) noexcept
    {
        if (this != &other) {
            reset();
            allocation_ = other.allocation_;
            allocation_size_ = other.allocation_size_;
            mabf_size_ = other.mabf_size_;
            other.allocation_ = nullptr;
            other.allocation_size_ = 0;
            other.mabf_size_ = 0;
        }
        return *this;
    }

    ~MabfVirtualAllocation()
    {
        reset();
    }

    bool allocate_from_mabf(const std::vector<uint8_t>& bytes)
    {
        reset();
        if (bytes.empty() || bytes.size() > UINT32_MAX) {
            return false;
        }

        const size_t allocation_size = bytes.size() + kSeadPayloadHeaderSize;
        void* allocation = VirtualAlloc(nullptr, allocation_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!allocation) {
            return false;
        }

        const uint64_t size32 = static_cast<uint32_t>(bytes.size());
        auto* header = reinterpret_cast<uint64_t*>(allocation);
        header[0] = 0x21;
        header[1] = (size32 << 32) | size32;
        std::memcpy(reinterpret_cast<uint8_t*>(allocation) + kSeadPayloadHeaderSize, bytes.data(), bytes.size());

        allocation_ = allocation;
        allocation_size_ = allocation_size;
        mabf_size_ = bytes.size();
        return true;
    }

    void reset()
    {
        if (allocation_) {
            VirtualFree(allocation_, 0, MEM_RELEASE);
#ifdef FF7RP_AUDIO_SIDECAR_SELFTEST
            audio_sidecar_selftest_note_free();
#endif
            allocation_ = nullptr;
            allocation_size_ = 0;
            mabf_size_ = 0;
        }
    }

    void* sead_header() const { return allocation_; }
    void* mabf_bytes() const { return allocation_ ? reinterpret_cast<uint8_t*>(allocation_) + kSeadPayloadHeaderSize : nullptr; }
    size_t allocation_size() const { return allocation_size_; }
    size_t mabf_size() const { return mabf_size_; }

private:
    void* allocation_ = nullptr;
    size_t allocation_size_ = 0;
    size_t mabf_size_ = 0;
};

struct SidecarRuntimeState {
    std::string key;
    std::string song_id;
    std::wstring resolved_path;
    std::string status;
    MabfVirtualAllocation allocation;
};

class PreparedAudioPrefix final {
public:
    ~PreparedAudioPrefix() = default;
    PreparedAudioPrefix(const PreparedAudioPrefix&) = delete;
    PreparedAudioPrefix& operator=(const PreparedAudioPrefix&) = delete;

private:
    struct Node;
    std::shared_ptr<const Node> tail_;
    std::size_t size_ = 0;

    PreparedAudioPrefix(std::shared_ptr<const Node> tail, std::size_t size) noexcept;
    friend class ProgressiveAudioCatalogBuilder;
    friend const SidecarRuntimeState* find_prepared_audio_sidecar(
        const PreparedAudioPrefix&, std::string_view) noexcept;
    friend std::size_t prepared_audio_prefix_size(
        const PreparedAudioPrefix&) noexcept;
    friend bool prepared_audio_prefix_matches(
        const PreparedAudioPrefix&, const std::vector<SongDescriptor>&) noexcept;
};

// Process-scoped loader-thread builder. Append is failure-atomic: a failed
// sidecar load, validation, allocation, or duplicate key leaves the prefix
// unchanged. Snapshots retain immutable separately allocated nodes in O(1).
class ProgressiveAudioCatalogBuilder final {
public:
    ProgressiveAudioCatalogBuilder() noexcept;
    ~ProgressiveAudioCatalogBuilder();
    ProgressiveAudioCatalogBuilder(const ProgressiveAudioCatalogBuilder&) = delete;
    ProgressiveAudioCatalogBuilder& operator=(const ProgressiveAudioCatalogBuilder&) = delete;

    std::shared_ptr<const PreparedAudioPrefix> append(const SongDescriptor& song) noexcept;
    std::shared_ptr<const PreparedAudioPrefix> snapshot() const noexcept;
    void log_newly_accepted(
        const std::shared_ptr<const PreparedAudioPrefix>& prefix) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::size_t prepared_audio_prefix_size(const PreparedAudioPrefix& prefix) noexcept;
const SidecarRuntimeState* find_prepared_audio_sidecar(
    const PreparedAudioPrefix& prefix, std::string_view key) noexcept;
bool prepared_audio_prefix_matches(
    const PreparedAudioPrefix& prefix, const std::vector<SongDescriptor>& storage) noexcept;

std::string sidecar_key_for_song(const SongDescriptor& song);
std::vector<std::wstring> sidecar_path_candidates(const SongDescriptor& song);
SidecarRuntimeState build_sidecar_state(const SongDescriptor& song);
void log_sidecar_state(const SidecarRuntimeState& state);

#ifdef FF7RP_AUDIO_SIDECAR_SELFTEST
void reset_audio_sidecar_selftest_counts() noexcept;
std::size_t audio_sidecar_selftest_build_count() noexcept;
std::size_t audio_sidecar_selftest_allocation_count() noexcept;
std::size_t audio_sidecar_selftest_free_count() noexcept;
#endif

} // namespace audio_sead_detail
} // namespace ff7r::piano::game
