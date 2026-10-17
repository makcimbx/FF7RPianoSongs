#pragma once

#include "game/native_audio_runtime.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ff7r::piano::game {
struct SongDescriptor;
namespace audio_sead_detail {

enum class AudioCatalogReadiness { Pending, Ready, Failed };
struct PreparedNativeSong final {
    std::string key;
    std::shared_ptr<const ff7rp::pipeline::NativeAudioArtifactSet> artifacts;
    std::shared_ptr<native_audio::PreparedNativeAudio> token;
    std::uint64_t generation=0;
};

class PreparedAudioPrefix final {
public:
    ~PreparedAudioPrefix()=default;
    PreparedAudioPrefix(const PreparedAudioPrefix&)=delete;
    PreparedAudioPrefix& operator=(const PreparedAudioPrefix&)=delete;
private:
    struct Node;
    std::shared_ptr<const Node> tail_;
    std::size_t size_=0;
    PreparedAudioPrefix(std::shared_ptr<const Node>,std::size_t) noexcept;
    friend class ProgressiveAudioCatalogBuilder;
    friend const PreparedNativeSong* find_prepared_native_song(const PreparedAudioPrefix&,std::string_view) noexcept;
    friend std::size_t prepared_audio_prefix_size(const PreparedAudioPrefix&) noexcept;
    friend bool prepared_audio_prefix_matches(const PreparedAudioPrefix&,const std::vector<SongDescriptor>&) noexcept;
    friend bool enqueue_prepared_audio_prefix(const PreparedAudioPrefix&) noexcept;
    friend AudioCatalogReadiness prepared_audio_prefix_readiness(const PreparedAudioPrefix&) noexcept;
};

// Worker-only preparation; no MABF mapping, native calls or game-memory reads.
class ProgressiveAudioCatalogBuilder final {
public:
    ProgressiveAudioCatalogBuilder() noexcept;
    ~ProgressiveAudioCatalogBuilder();
    ProgressiveAudioCatalogBuilder(const ProgressiveAudioCatalogBuilder&)=delete;
    ProgressiveAudioCatalogBuilder& operator=(const ProgressiveAudioCatalogBuilder&)=delete;
    std::shared_ptr<const PreparedAudioPrefix> append(const SongDescriptor&) noexcept;
    std::shared_ptr<const PreparedAudioPrefix> snapshot() const noexcept;
    void log_newly_accepted(const std::shared_ptr<const PreparedAudioPrefix>&) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string sidecar_key_for_song(const SongDescriptor&);
std::size_t prepared_audio_prefix_size(const PreparedAudioPrefix&) noexcept;
const PreparedNativeSong* find_prepared_native_song(const PreparedAudioPrefix&,std::string_view) noexcept;
bool prepared_audio_prefix_matches(const PreparedAudioPrefix&,const std::vector<SongDescriptor>&) noexcept;
// Worker: complete enqueue before publishing the immutable prefix to readers.
bool enqueue_prepared_audio_prefix(const PreparedAudioPrefix&) noexcept;
AudioCatalogReadiness prepared_audio_prefix_readiness(const PreparedAudioPrefix&) noexcept;
} // namespace audio_sead_detail
} // namespace ff7r::piano::game
