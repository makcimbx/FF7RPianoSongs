#include "game/audio_sidecar_runtime.h"
#include "game/song_descriptor.h"
#include "game/runtime_layouts.h"
#include "core/logging.h"
#include <mutex>
#include <unordered_set>

namespace ff7r::piano::game::audio_sead_detail {
namespace { std::mutex preparation_mutex;std::uint64_t preparation_generation=0; }
struct PreparedAudioPrefix::Node final {
    std::shared_ptr<const Node> prior;
    PreparedNativeSong song;
    std::size_t count=0;
};
PreparedAudioPrefix::PreparedAudioPrefix(std::shared_ptr<const Node> tail,std::size_t size) noexcept
    :tail_(std::move(tail)),size_(size) {}
struct ProgressiveAudioCatalogBuilder::Impl final {
    std::shared_ptr<const PreparedAudioPrefix> current;
    std::unordered_set<std::string> keys;
    std::size_t logged_count=0;
};
ProgressiveAudioCatalogBuilder::ProgressiveAudioCatalogBuilder() noexcept {
    try {impl_=std::make_unique<Impl>();}catch(...){}
}
ProgressiveAudioCatalogBuilder::~ProgressiveAudioCatalogBuilder()=default;
std::string sidecar_key_for_song(const SongDescriptor& song) {return song.id;}
std::shared_ptr<const PreparedAudioPrefix> ProgressiveAudioCatalogBuilder::append(const SongDescriptor& song) noexcept {
    try {
        if(!impl_ || song.id.empty() || !song.native_audio || impl_->keys.contains(song.id))return {};
        const auto count=impl_->current?impl_->current->size_:0;
        if(count>=static_cast<std::size_t>(runtime_layouts::PianoMusicList::maximum_count
            -runtime_layouts::PianoMusicList::vanilla_count))return {};
        std::uint64_t generation=0;
        {std::lock_guard lock(preparation_mutex);if(preparation_generation==UINT64_MAX)return {};
         generation=++preparation_generation;}
        auto token=native_audio::prepare_native_audio(song.native_audio,generation);if(!token)return {};
        auto node=std::make_shared<PreparedAudioPrefix::Node>();
        node->prior=impl_->current?impl_->current->tail_:nullptr;node->count=count+1;
        node->song={song.id,song.native_audio,std::move(token),generation};
        auto prefix=std::shared_ptr<const PreparedAudioPrefix>(new PreparedAudioPrefix(node,node->count));
        if(!impl_->keys.insert(song.id).second)return {};
        impl_->current=prefix;return prefix;
    }catch(...){return {};}
}
std::shared_ptr<const PreparedAudioPrefix> ProgressiveAudioCatalogBuilder::snapshot() const noexcept {
    return impl_?impl_->current:nullptr;
}
void ProgressiveAudioCatalogBuilder::log_newly_accepted(const std::shared_ptr<const PreparedAudioPrefix>& prefix) noexcept {
    if(!impl_ || !prefix || prefix->size_<=impl_->logged_count)return;
    try {core::log(core::LogLevel::Info,"[native_audio_catalog] prepared="+std::to_string(prefix->size_)
        +" readiness=pending native_ownership=none");}catch(...){}
    impl_->logged_count=prefix->size_;
}
std::size_t prepared_audio_prefix_size(const PreparedAudioPrefix& prefix) noexcept {return prefix.size_;}
const PreparedNativeSong* find_prepared_native_song(const PreparedAudioPrefix& prefix,std::string_view key) noexcept {
    for(auto node=prefix.tail_;node;node=node->prior)if(node->song.key==key)return &node->song;
    return nullptr;
}
bool prepared_audio_prefix_matches(const PreparedAudioPrefix& prefix,const std::vector<SongDescriptor>& storage) noexcept {
    if(prefix.size_!=storage.size())return false;
    auto node=prefix.tail_;
    for(auto index=storage.size();index;--index) {
        const auto& song=storage[index-1];
        if(!node || node->count!=index || node->song.key!=song.id || node->song.artifacts!=song.native_audio)return false;
        node=node->prior;
    }
    return !node;
}
bool enqueue_prepared_audio_prefix(const PreparedAudioPrefix& prefix) noexcept {
    for(auto node=prefix.tail_;node;node=node->prior)if(!native_audio::enqueue_native_audio(node->song.token))return false;
    return true;
}
AudioCatalogReadiness prepared_audio_prefix_readiness(const PreparedAudioPrefix& prefix) noexcept {
    bool pending=false;
    for(auto node=prefix.tail_;node;node=node->prior) {
        const auto status=native_audio::native_audio_status(*node->song.token);
        if(status.stage==native_audio::RuntimeStage::Failed || status.stage==native_audio::RuntimeStage::Stopped
            || status.stage==native_audio::RuntimeStage::Unavailable)return AudioCatalogReadiness::Failed;
        pending|=status.stage!=native_audio::RuntimeStage::Ready;
    }
    return pending?AudioCatalogReadiness::Pending:AudioCatalogReadiness::Ready;
}
} // namespace ff7r::piano::game::audio_sead_detail
