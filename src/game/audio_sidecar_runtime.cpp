#include "game/audio_sidecar_runtime.h"

#include "core/logging.h"
#include "game/mabf_sidecar_loader.h"
#include "game/runtime_layouts.h"
#include "game/song_registry.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <unordered_set>

namespace ff7r::piano::game::audio_sead_detail {

#ifdef FF7RP_AUDIO_SIDECAR_SELFTEST
std::size_t g_selftest_build_count = 0;
std::size_t g_selftest_allocation_count = 0;
std::size_t g_selftest_free_count = 0;
#endif

struct PreparedAudioPrefix::Node final {
    std::shared_ptr<const Node> prior;
    SidecarRuntimeState sidecar;
    std::size_t count = 0;
};

PreparedAudioPrefix::PreparedAudioPrefix(
    std::shared_ptr<const Node> tail, const std::size_t size) noexcept
    : tail_(std::move(tail)), size_(size)
{
}

struct ProgressiveAudioCatalogBuilder::Impl final {
    std::shared_ptr<const PreparedAudioPrefix> current;
    std::unordered_set<std::string> keys;
    std::size_t logged_count = 0;
};

ProgressiveAudioCatalogBuilder::ProgressiveAudioCatalogBuilder() noexcept
{
    try { impl_ = std::make_unique<Impl>(); } catch (...) {}
}

ProgressiveAudioCatalogBuilder::~ProgressiveAudioCatalogBuilder() = default;

std::string sidecar_key_for_song(const SongDescriptor& song)
{
    if (!song.id.empty()) {
        return song.id;
    }
    return core::narrow(song.sidecar_path);
}

void push_unique_path(std::vector<std::wstring>& paths, const std::filesystem::path& path)
{
    const std::wstring value = path.wstring();
    if (value.empty()) {
        return;
    }
    if (std::find(paths.begin(), paths.end(), value) == paths.end()) {
        paths.push_back(value);
    }
}

std::vector<std::wstring> sidecar_path_candidates(const SongDescriptor& song)
{
    std::vector<std::wstring> paths;
    if (!song.sidecar_path.empty()) {
        const std::filesystem::path descriptor_path(song.sidecar_path);
        push_unique_path(paths, descriptor_path.parent_path() / L"song.mabf.bin");
        push_unique_path(paths, descriptor_path);
    }
    if (!song.id.empty()) {
        push_unique_path(paths, std::filesystem::path(core::log_directory()) / L"Music" / core::widen(song.id) / L".cache" / L"song.mabf.bin");
    }
    return paths;
}

SidecarRuntimeState build_sidecar_state(const SongDescriptor& song)
{
#ifdef FF7RP_AUDIO_SIDECAR_SELFTEST
    ++g_selftest_build_count;
#endif
    SidecarRuntimeState state;
    state.key = sidecar_key_for_song(song);
    state.song_id = song.id;
    state.status = "missing";

    const std::vector<std::wstring> candidates = sidecar_path_candidates(song);
    if (candidates.empty()) {
        state.status = "no_path";
        return state;
    }

    for (const std::wstring& path : candidates) {
        SidecarLoadResult load = load_sidecar_bytes_file(path);
        state.resolved_path = path;
        state.status = load.status;
        if (!load.ok) {
            continue;
        }
        if (!state.allocation.allocate_from_mabf(load.bytes)) {
            state.status = "virtualalloc_failed";
            return state;
        }
#ifdef FF7RP_AUDIO_SIDECAR_SELFTEST
        ++g_selftest_allocation_count;
#endif
        state.status = "ready";
        return state;
    }

    return state;
}

#ifdef FF7RP_AUDIO_SIDECAR_SELFTEST
void reset_audio_sidecar_selftest_counts() noexcept
{
    g_selftest_build_count = 0;
    g_selftest_allocation_count = 0;
    g_selftest_free_count = 0;
}

std::size_t audio_sidecar_selftest_build_count() noexcept { return g_selftest_build_count; }
std::size_t audio_sidecar_selftest_allocation_count() noexcept
{
    return g_selftest_allocation_count;
}
void audio_sidecar_selftest_note_free() noexcept { ++g_selftest_free_count; }
std::size_t audio_sidecar_selftest_free_count() noexcept { return g_selftest_free_count; }
#endif

void log_sidecar_state(const SidecarRuntimeState& state)
{
    std::ostringstream out;
    out << "[audio_sead_sidecar] key=\"" << state.key << "\""
        << " id=\"" << state.song_id << "\""
        << " status=" << state.status
        << " allocation=0x" << reinterpret_cast<uintptr_t>(state.allocation.sead_header())
        << " mabf=0x" << reinterpret_cast<uintptr_t>(state.allocation.mabf_bytes())
        << " mabf_size=0x" << state.allocation.mabf_size()
        << std::dec
        << " path=\"" << core::narrow(state.resolved_path) << "\"";
    core::log(state.status == "ready" ? core::LogLevel::Info : core::LogLevel::Debug, out.str());
}

std::shared_ptr<const PreparedAudioPrefix>
ProgressiveAudioCatalogBuilder::append(const SongDescriptor& song) noexcept
{
    try {
        if (!impl_) return {};
        const std::string key = sidecar_key_for_song(song);
        if (key.empty() || impl_->keys.find(key) != impl_->keys.end()) return {};
        SidecarRuntimeState sidecar = build_sidecar_state(song);
        if (sidecar.status != "ready") return {};
        auto node = std::make_shared<PreparedAudioPrefix::Node>();
        node->prior = impl_->current ? impl_->current->tail_ : nullptr;
        node->sidecar = std::move(sidecar);
        node->count = impl_->current ? impl_->current->size_ + 1 : 1;
        auto prefix = std::shared_ptr<const PreparedAudioPrefix>(
            new PreparedAudioPrefix(node, node->count));
        const auto inserted = impl_->keys.insert(key);
        if (!inserted.second) return {};
        impl_->current = prefix;
        return prefix;
    } catch (...) {
        return {};
    }
}

std::shared_ptr<const PreparedAudioPrefix>
ProgressiveAudioCatalogBuilder::snapshot() const noexcept
{
    return impl_ ? impl_->current : nullptr;
}

void ProgressiveAudioCatalogBuilder::log_newly_accepted(
    const std::shared_ptr<const PreparedAudioPrefix>& prefix) noexcept
{
    if (!impl_ || !prefix || prefix->size_ <= impl_->logged_count) return;
    constexpr auto kMaximumCustomSongs =
        runtime_layouts::PianoMusicList::maximum_count -
        runtime_layouts::PianoMusicList::vanilla_count;
    std::array<const PreparedAudioPrefix::Node*, kMaximumCustomSongs> newly_accepted{};
    std::size_t count = 0;
    for (auto node = prefix->tail_; node && node->count > impl_->logged_count;
         node = node->prior) {
        if (count == newly_accepted.size()) return;
        newly_accepted[count++] = node.get();
    }
    while (count > 0) {
        try { log_sidecar_state(newly_accepted[--count]->sidecar); } catch (...) {}
    }
    impl_->logged_count = prefix->size_;
}

std::size_t prepared_audio_prefix_size(const PreparedAudioPrefix& prefix) noexcept
{
    return prefix.size_;
}

const SidecarRuntimeState* find_prepared_audio_sidecar(
    const PreparedAudioPrefix& prefix, const std::string_view key) noexcept
{
    for (auto node = prefix.tail_; node; node = node->prior) {
        if (node->sidecar.key == key) return &node->sidecar;
    }
    return nullptr;
}

bool prepared_audio_prefix_matches(const PreparedAudioPrefix& prefix,
    const std::vector<SongDescriptor>& storage) noexcept
{
    try {
        constexpr auto kMaximumCustomSongs =
            runtime_layouts::PianoMusicList::maximum_count -
            runtime_layouts::PianoMusicList::vanilla_count;
        if (storage.size() > kMaximumCustomSongs) return false;
        if (prefix.size_ != storage.size()) return false;
        auto node = prefix.tail_;
        for (std::size_t index = storage.size(); index != 0; --index) {
            if (!node || node->count != index
                || node->sidecar.key != sidecar_key_for_song(storage[index - 1])) return false;
            node = node->prior;
        }
        return !node;
    } catch (...) {
        return false;
    }
}

} // namespace ff7r::piano::game::audio_sead_detail
