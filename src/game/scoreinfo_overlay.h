#pragma once

#include "game/song_registry.h"
#include "game/scoreinfo_result_policy.h"
#include "game/ue_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace ff7r::piano::game {

class ScoreInfoPublicationEpoch {
public:
    std::mutex& mutex() noexcept { return mutex_; }
    std::uint64_t observe_locked() const noexcept { return epoch_; }
    void invalidate_locked() noexcept { ++epoch_; }
    bool current_locked(std::uint64_t observed,
        const CustomContextToken& expected, const PlaybackSnapshot& current) const noexcept
    {
        return observed == epoch_ && current.song && current.token == expected;
    }

private:
    std::mutex mutex_;
    std::uint64_t epoch_ = 0;
};

class ScoreInfoResolverRecursionScope {
public:
    explicit ScoreInfoResolverRecursionScope(unsigned& depth) noexcept
        : depth_(depth), outermost_(depth++ == 0) {}
    ~ScoreInfoResolverRecursionScope() { --depth_; }
    bool outermost() const noexcept { return outermost_; }

private:
    unsigned& depth_;
    bool outermost_ = false;
};

class ScoreInfoPianoDetailUpdateScope {
public:
    ScoreInfoPianoDetailUpdateScope() noexcept;
    ~ScoreInfoPianoDetailUpdateScope() noexcept;

    ScoreInfoPianoDetailUpdateScope(const ScoreInfoPianoDetailUpdateScope&) = delete;
    ScoreInfoPianoDetailUpdateScope& operator=(const ScoreInfoPianoDetailUpdateScope&) = delete;
};

inline bool scoreinfo_playback_eligible(const PlaybackSnapshot& playback) noexcept
{
    return playback.song && playback.profile && playback.token.valid();
}

inline constexpr uintptr_t kScoreInfoModeChangeOffset = 0x20;
inline constexpr uintptr_t kScoreInfoScoreArrayOffset = 0x30;
inline constexpr uintptr_t kScoreInfoMenuTextOffset = 0x40;
inline constexpr uintptr_t kScoreInfoBgmNameOffset = 0x50;
inline constexpr uintptr_t kScoreInfoCameraMinOffset = 0x58;
inline constexpr uintptr_t kScoreInfoCameraMaxOffset = 0x5c;
inline constexpr uintptr_t kScoreInfoCameraRateOffset = 0x60;
inline constexpr uintptr_t kScoreInfoBpmOffset = 0x64;
inline constexpr uintptr_t kScoreInfoStreamingFrameOffset = 0x68;
inline constexpr uintptr_t kScoreInfoDifficultyOffset = 0x6c;

inline FNameValue scoreinfo_source_bgm_name(const void* source_row, const size_t source_size) noexcept
{
    FNameValue value{};
    if (source_row && kScoreInfoBgmNameOffset + sizeof(value) <= source_size) {
        std::memcpy(&value, static_cast<const uint8_t*>(source_row) + kScoreInfoBgmNameOffset, sizeof(value));
    }
    return value;
}

constexpr int32_t native_scoreinfo_difficulty(const int difficulty)
{
    return difficulty < 1 ? 1 : (difficulty > 6 ? 6 : difficulty);
}

// List-only product policy. Never send a large public label to the allocating
// native repetition loop. The exact title/profile label and the separate
// result/scoring difficulty policy remain unchanged.
constexpr int32_t list_difficulty_icon_count(int label) noexcept
{
    return label < 0 ? 0 : (label > 6 ? 6 : label);
}

constexpr float custom_scoreinfo_bpm(const float vanilla_bpm)
{
    return vanilla_bpm;
}

inline std::array<int32_t, 4> scoreinfo_thresholds_for_descriptor(
    const SongDescriptor& song,
    const SongDifficultyProfile* profile) noexcept
{
    return profile ? profile->score_thresholds : song.score_thresholds;
}

struct ScoreInfoOverlayRow {
    static constexpr size_t kRowSize = 0x800;

    std::array<uint8_t, kRowSize> row{};
    std::array<int32_t, 2> mode_change_counts{};
    std::array<int32_t, 4> score_thresholds{};
    std::wstring title;
    float camera_min = 0.0f;
    float camera_max = 0.0f;
    float camera_rate = 1.0f;
    float bpm = 0.0f;
    uint32_t streaming_frame = 0;
    int32_t difficulty = 1;
    FNameValue bgm_name{};

    ScoreInfoOverlayRow() = default;
    ScoreInfoOverlayRow(const ScoreInfoOverlayRow& other);
    ScoreInfoOverlayRow& operator=(const ScoreInfoOverlayRow& other);
    ScoreInfoOverlayRow(ScoreInfoOverlayRow&& other) noexcept;
    ScoreInfoOverlayRow& operator=(ScoreInfoOverlayRow&& other) noexcept;

    uint8_t* data() { return row.data(); }
    const uint8_t* data() const { return row.data(); }
    size_t size() const { return row.size(); }
    void refresh_references();
};

inline bool scoreinfo_overlay_publishable(bool caller_known, const ScoreInfoOverlayRow& row) noexcept
{
    return caller_known && row.bgm_name.comparison_id != 0;
}

inline constexpr bool scoreinfo_menu_detail_overlay_candidate(
    const ScoreInfoResultCatalogRole role, const bool callback_accepted,
    const bool outermost, const bool wrapper_present,
    const bool scoped_descriptor_present) noexcept
{
    return (role == ScoreInfoResultCatalogRole::MenuDetail
            || role == ScoreInfoResultCatalogRole::ListItem)
        && callback_accepted && outermost && wrapper_present
        && scoped_descriptor_present;
}

ScoreInfoOverlayRow make_scoreinfo_overlay_row(
    const SongDescriptor& song, const SongDifficultyProfile* profile,
    const void* source_row = nullptr, size_t source_size = 0);
ScoreInfoOverlayRow make_scoreinfo_overlay_row(const SongDescriptor& song, const void* source_row = nullptr, size_t source_size = 0);
bool scoreinfo_overlay_row_matches_playback(const void* row, const PlaybackSnapshot& playback);
bool refresh_active_scoreinfo_overlay_profile();
bool invalidate_scoreinfo_playback(const CustomContextToken& token);
void retire_scoreinfo_result_authority_for_list();
inline bool scoreinfo_wrapper_restore_allowed(
    const void* current, const void* redirected, const void* original) noexcept
{
    return current == redirected || current == original;
}

} // namespace ff7r::piano::game
