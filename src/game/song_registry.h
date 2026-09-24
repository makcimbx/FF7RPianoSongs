#pragma once

#include "game/song_descriptor.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ff7r::piano::game {

using SongRegistryStorage = std::vector<SongDescriptor>;

struct RegistrySnapshot {
    std::uint64_t generation = 0;
    std::uint64_t catalog_revision = 0;
    std::shared_ptr<const SongRegistryStorage> storage;

    const SongRegistryStorage& songs() const noexcept;
    const SongDescriptor* by_visible_index(int visible_index) const noexcept;
    const SongDescriptor* by_id(const std::string& id) const noexcept;
    explicit operator bool() const noexcept { return storage != nullptr; }
};

struct SelectionSnapshot {
    std::uint64_t generation = 0;
    std::shared_ptr<const void> storage;
    const SongDescriptor* song = nullptr;
    const SongDifficultyProfile* profile = nullptr;
    int profile_index = -1;
    int visible_index = -1;
    int base_slot = -1;

    explicit operator bool() const noexcept { return song && profile; }
};

struct RenderSnapshot : SelectionSnapshot {};
enum class InitializedProfileState : std::uint8_t {
    Invalid, Absent, Deferred, Present
};

struct CustomContextToken {
    std::uint64_t registry_generation = 0;
    std::uint64_t route_generation = 0;
    std::uint64_t lease_generation = 0;
    std::uint64_t song_key = 0;
    void* controller = nullptr;
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    std::uint64_t request_handle = 0;

    bool valid() const noexcept
    {
        return registry_generation != 0 && route_generation != 0
            && lease_generation != 0 && song_key != 0;
    }
    bool same_lease(const CustomContextToken& other) const noexcept
    {
        return registry_generation == other.registry_generation
            && lease_generation == other.lease_generation
            && song_key == other.song_key;
    }
    bool operator==(const CustomContextToken& other) const noexcept
    {
        return registry_generation == other.registry_generation
            && route_generation == other.route_generation
            && lease_generation == other.lease_generation
            && song_key == other.song_key
            && controller == other.controller && slot == other.slot
            && bgm == other.bgm && sound == other.sound
            && request_handle == other.request_handle;
    }
    bool operator!=(const CustomContextToken& other) const noexcept
    {
        return !(*this == other);
    }
};

struct ChordVoicingBinding;
enum class ChartAdmissionState : uint8_t { Pending, Ready, Failed };
struct NativeChartHeader {
    uintptr_t data = 0;
    int32_t count = 0;
    int32_t capacity = 0;
    bool operator==(const NativeChartHeader&) const = default;
};
struct ChartAdmissionSnapshot {
    std::shared_ptr<const ChordVoicingBinding> binding;
    NativeChartHeader header{};
    ChartAdmissionState state = ChartAdmissionState::Pending;
    bool rejection_reported = false;
};
enum class ChartUpdateAdmission : uint8_t { Stock, Ready, Rejected, RejectedFirst };

struct PlaybackSnapshot : SelectionSnapshot {
    CustomContextToken token{};
    ChartAdmissionSnapshot chart_admission{};
};

struct CleanupLease : SelectionSnapshot {
    CustomContextToken token{};
    ChartAdmissionSnapshot chart_admission{};
    // Denial only, never native dereference/destruction authority. Cleared by
    // real lease retirement. A failed prefix is not stock after reparse either.
    void* failed_expansion_wrapper = nullptr;
};

using ActiveSongSnapshot = SelectionSnapshot;

// Resolves an unresolved catalog onto the rows appended after a live list of
// first_custom_row entries, in storage order and without gaps. Refuses a
// catalog that already claims rows, so a catalog resolved against one list
// length can never be silently re-based against another.
inline bool assign_custom_rows(
    SongRegistryStorage& songs, const int first_custom_row) noexcept
{
    if (songs.empty() || first_custom_row < 0) return false;
    for (const SongDescriptor& song : songs) {
        if (song.visible_index != kUnresolvedVisibleIndex) return false;
    }
    int row = first_custom_row;
    for (SongDescriptor& song : songs) song.visible_index = row++;
    return true;
}

class SongRegistry {
public:
    class PreparedCatalogCommit;
    void replace(std::vector<SongDescriptor> songs);
    RegistrySnapshot registry_snapshot() const;
    bool try_registry_snapshot(RegistrySnapshot& snapshot) const noexcept;
    bool is_current(const RegistrySnapshot& snapshot) const;
    std::shared_ptr<PreparedCatalogCommit> begin_catalog_commit(
        const RegistrySnapshot& expected,
        std::shared_ptr<const SongRegistryStorage> replacement) noexcept;
    void commit_catalog(PreparedCatalogCommit& prepared) noexcept;
    bool selection_matches(const SelectionSnapshot& snapshot) const;
    void set_active_selection(int visible_index, int base_slot);
    bool publish_activation_selection_handoff(
        const SelectionSnapshot& expected_selection,
        int visible_index, int base_slot,
        uint64_t reservation_generation, bool alias_state_exact,
        SelectionSnapshot& published_selection);
    bool preserve_confirmed_activation_index_notification(
        const SelectionSnapshot& confirmed_selection,
        int visible_index, int base_slot, bool exact_notification);
    bool freeze_active_profile_for_activation(
        const SelectionSnapshot& confirmed_selection,
        uint64_t reservation_generation,
        SelectionSnapshot& frozen_selection);
    bool thaw_active_profile_for_activation(
        const SelectionSnapshot& frozen_selection,
        uint64_t reservation_generation,
        SelectionSnapshot& thawed_selection);
    void clear_active_selection();
    ActiveSongSnapshot active_snapshot() const;
    SelectionSnapshot selection_snapshot() const;
    RenderSnapshot render_snapshot() const;
    PlaybackSnapshot playback_snapshot() const;
    bool try_playback_snapshot(PlaybackSnapshot& snapshot) const noexcept;
    CleanupLease cleanup_lease() const;
    SelectionSnapshot snapshot_for_visible_index(int visible_index) const;
    InitializedProfileState initialized_profile_state(
        const SelectionSnapshot& expected, SelectionSnapshot& initialized) const;
    bool initialize_profile_if_absent(const SelectionSnapshot& expected,
        int profile_index, SelectionSnapshot& initialized);
    bool publish_playback(const SelectionSnapshot& selection, const CustomContextToken& token);
    bool acquire_selection_guard(
        const SelectionSnapshot& selection, const CustomContextToken& token);
    bool selection_guard_matches(
        const SelectionSnapshot& selection, const CustomContextToken& token) const;
    bool release_selection_guard(const CustomContextToken& token);
    bool publish_playback_from_selection_guard(
        const SelectionSnapshot& selection, const CustomContextToken& token);
    bool update_playback_token(const CustomContextToken& expected, const CustomContextToken& replacement);
    bool attach_chart_admission(const SelectionSnapshot&, const CustomContextToken&,
        std::shared_ptr<const ChordVoicingBinding>);
    bool seal_chart_admission(const std::shared_ptr<const ChordVoicingBinding>&,
        const NativeChartHeader&);
    void invalidate_chart_admission(void* wrapper);
    bool fail_chart_admission(const std::shared_ptr<const ChordVoicingBinding>&);
    ChartAdmissionSnapshot chart_admission_for_wrapper(void* wrapper) const;
    ChartUpdateAdmission chart_update_admission(void* wrapper);
    bool commit_if_current_chart(const std::shared_ptr<const ChordVoicingBinding>&,
        bool (*commit)(const ChartAdmissionSnapshot&, void*) noexcept, void* context);
    bool revoke_playback(const CustomContextToken& token);
    bool fail_chart_expansion(const SelectionSnapshot&, const CustomContextToken&,
        void* wrapper);
    bool retire_cleanup_lease(const CustomContextToken& token);
    bool commit_if_playback_token(
        const CustomContextToken& token, const std::function<bool()>& commit);
    bool commit_if_selection_snapshot(
        const SelectionSnapshot& expected,
        SelectionSnapshot& observed_selection,
        PlaybackSnapshot& observed_playback,
        const std::function<bool()>& commit);
    bool cycle_active_profile(int delta);
    bool cycle_active_profile_exact(const SelectionSnapshot& expected,
        int delta, SelectionSnapshot& changed);
    bool freeze_active_profile();
    void clear_frozen_profile();
    int active_visible_index() const;
    // Presentation preference only: accepted playback records stable identity;
    // cleanup retains neither a lease nor a native pointer for menu focus.
    SelectionSnapshot prepare_last_played_focus();
    void clear_last_played_focus();
    int active_base_slot() const;
    size_t custom_count() const;
#ifdef FF7RP_CATALOG_ADOPTION_SELFTEST
    void selftest_lock_catalog_state();
    void selftest_unlock_catalog_state();
#endif
#ifdef FF7RP_SONG_REGISTRY_SELFTEST
    void selftest_seed_generations(
        std::uint64_t generation, std::uint64_t catalog_revision);
    void selftest_lock_state();
    void selftest_unlock_state();
#endif

private:
    bool idle_for_catalog_adoption_locked() const noexcept;
    bool profile_initialization_mutation_blocked_locked() const noexcept;
    SelectionSnapshot selection_snapshot_locked() const;
    bool selection_matches_locked(const SelectionSnapshot& selection) const;

    std::shared_ptr<const SongRegistryStorage> songs_ =
        std::make_shared<const SongRegistryStorage>();
    mutable std::mutex state_mutex_;
    int active_visible_index_ = -1;
    int active_base_slot_ = -1;
    std::unordered_map<std::string, int> selected_profiles_; // difficulty labels, not indices
    // Pins immutable ID/label without allocating in accepted audio publication.
    // Only those values are used after catalog adoption, never stored indices.
    SelectionSnapshot last_played_;
    std::string profile_lock_song_id_;
    int profile_lock_index_ = -1;
    uint64_t profile_lock_reservation_generation_ = 0;
    PlaybackSnapshot playback_;
    CleanupLease cleanup_;
    SelectionSnapshot selection_guard_;
    CustomContextToken selection_guard_token_{};
    ChartAdmissionSnapshot selection_guard_chart_{};
    std::uint64_t generation_ = 1;
    std::uint64_t catalog_revision_ = 1;
};

class ScopedSongRenderContext {
public:
    ScopedSongRenderContext(int visible_index, int base_slot);
    explicit ScopedSongRenderContext(SelectionSnapshot snapshot);
    ScopedSongRenderContext(const ScopedSongRenderContext&) = delete;
    ScopedSongRenderContext& operator=(const ScopedSongRenderContext&) = delete;
    ~ScopedSongRenderContext();

private:
    bool active_ = false;
};

bool song_render_context_matches(const SongDescriptor* song) noexcept;
bool retain_song_render_context_owner(std::shared_ptr<const void> owner) noexcept;
bool selection_semantically_matches(
    const SelectionSnapshot& left, const SelectionSnapshot& right) noexcept;
bool registry_snapshot_owns_selection(
    const RegistrySnapshot& catalog, const SelectionSnapshot& selection) noexcept;
SongRegistry& registry();

} // namespace ff7r::piano::game
