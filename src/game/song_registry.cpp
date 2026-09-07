#include "game/song_registry.h"
#include "game/chord_voicing_binding.h"

#include <algorithm>
#include <utility>

namespace ff7r::piano::game {
namespace {

SongRegistry g_registry;

struct RenderContext {
    RenderSnapshot snapshot;
    int visible_index = -1;
    int base_slot = -1;
    std::vector<std::shared_ptr<const void>> retained_owners;
};

thread_local std::vector<RenderContext> g_render_contexts;

int selected_profile_index(
    const SongDescriptor& song,
    const std::unordered_map<std::string, int>& selected_profiles)
{
    if (song.profiles.empty()) return -1;
    if (const auto it = selected_profiles.find(song.id); it != selected_profiles.end()) {
        for (size_t i = 0; i < song.profiles.size(); ++i)
            if (song.profiles[i].difficulty == it->second) return static_cast<int>(i);
    }
    return song.default_profile_index;
}

} // namespace

const SongRegistryStorage& RegistrySnapshot::songs() const noexcept
{
    static const SongRegistryStorage empty;
    return storage ? *storage : empty;
}

const SongDescriptor* RegistrySnapshot::by_visible_index(const int visible_index) const noexcept
{
    for (const auto& song : songs()) {
        if (song.visible_index == visible_index) return &song;
    }
    return nullptr;
}

const SongDescriptor* RegistrySnapshot::by_id(const std::string& id) const noexcept
{
    for (const auto& song : songs()) {
        if (song.id == id) return &song;
    }
    return nullptr;
}

void SongRegistry::replace(std::vector<SongDescriptor> songs)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (selection_guard_.song || generation_ == UINT64_MAX
        || catalog_revision_ == UINT64_MAX) return;
    songs_ = std::make_shared<const std::vector<SongDescriptor>>(std::move(songs));
    active_visible_index_ = -1;
    active_base_slot_ = -1;
    selected_profiles_.clear();
    last_played_ = {};
    profile_lock_song_id_.clear();
    profile_lock_index_ = -1;
    profile_lock_reservation_generation_ = 0;
    playback_ = {};
    cleanup_ = {};
    selection_guard_ = {};
    selection_guard_token_ = {};
    ++generation_;
    selection_guard_chart_ = {};
    ++catalog_revision_;
}

RegistrySnapshot SongRegistry::registry_snapshot() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return RegistrySnapshot{generation_, catalog_revision_, songs_};
}

bool SongRegistry::try_registry_snapshot(RegistrySnapshot& snapshot) const noexcept
{
    snapshot = {};
    try {
        std::unique_lock lock(state_mutex_, std::try_to_lock);
        if (!lock) return false;
        snapshot = RegistrySnapshot{generation_, catalog_revision_, songs_};
        return true;
    } catch (...) { return false; }
}

#ifdef FF7RP_CATALOG_ADOPTION_SELFTEST
void SongRegistry::selftest_lock_catalog_state() { state_mutex_.lock(); }
void SongRegistry::selftest_unlock_catalog_state() { state_mutex_.unlock(); }
#endif

#ifdef FF7RP_SONG_REGISTRY_SELFTEST
void SongRegistry::selftest_seed_generations(
    const std::uint64_t generation, const std::uint64_t catalog_revision)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    generation_ = generation;
    catalog_revision_ = catalog_revision;
}
void SongRegistry::selftest_lock_state() { state_mutex_.lock(); }
void SongRegistry::selftest_unlock_state() { state_mutex_.unlock(); }
#endif

bool SongRegistry::is_current(const RegistrySnapshot& snapshot) const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return snapshot.generation == generation_ && snapshot.storage == songs_;
}

bool SongRegistry::idle_for_catalog_adoption_locked() const noexcept
{
    return active_visible_index_ < 0 && active_base_slot_ < 0
        && profile_lock_song_id_.empty()
        && profile_lock_index_ < 0 && profile_lock_reservation_generation_ == 0
        && !playback_.song && !cleanup_.song && !selection_guard_.song
        && !selection_guard_token_.valid();
}

class SongRegistry::PreparedCatalogCommit final {
public:
    std::unique_lock<std::mutex> lock;
    std::shared_ptr<const SongRegistryStorage> replacement;
};

std::shared_ptr<SongRegistry::PreparedCatalogCommit> SongRegistry::begin_catalog_commit(
    const RegistrySnapshot& expected,
    std::shared_ptr<const SongRegistryStorage> replacement) noexcept
{
    try {
        if (!replacement) return {};
        auto prepared = std::make_shared<PreparedCatalogCommit>();
        prepared->lock = std::unique_lock<std::mutex>(state_mutex_, std::try_to_lock);
        if (!prepared->lock || expected.generation != generation_
            || expected.catalog_revision != catalog_revision_
            || expected.storage != songs_ || generation_ == UINT64_MAX
            || catalog_revision_ == UINT64_MAX
            || !idle_for_catalog_adoption_locked()) return {};
        prepared->replacement = std::move(replacement);
        return prepared;
    } catch (...) { return {}; }
}

void SongRegistry::commit_catalog(PreparedCatalogCommit& prepared) noexcept
{
    songs_.swap(prepared.replacement);
    ++generation_;
    ++catalog_revision_;
}

bool SongRegistry::selection_matches(const SelectionSnapshot& snapshot) const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return selection_matches_locked(snapshot);
}

void SongRegistry::set_active_selection(int visible_index, int base_slot)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (selection_guard_.song) return;
    active_visible_index_ = visible_index;
    active_base_slot_ = base_slot;
    ++generation_;
}

bool SongRegistry::publish_activation_selection_handoff(
    const SelectionSnapshot& expected_selection,
    const int visible_index, const int base_slot,
    const uint64_t reservation_generation, const bool alias_state_exact,
    SelectionSnapshot& published_selection)
{
    published_selection = {};
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (reservation_generation == 0 || !alias_state_exact
        || generation_ == UINT64_MAX
        || selection_guard_.song
        || visible_index != active_visible_index_
        || base_slot != active_base_slot_
        || !selection_matches_locked(expected_selection)) {
        return false;
    }
    ++generation_;
    published_selection.generation = generation_;
    published_selection.storage = songs_;
    published_selection.visible_index = active_visible_index_;
    published_selection.base_slot = active_base_slot_;
    for (const auto& song : *songs_) {
        if (song.visible_index != active_visible_index_) continue;
        published_selection.song = &song;
        const int index = selected_profile_index(song, selected_profiles_);
        if (index >= 0 && index < static_cast<int>(song.profiles.size())) {
            published_selection.profile_index = index;
            published_selection.profile
                = &song.profiles[static_cast<size_t>(index)];
        }
        break;
    }
    return true;
}

bool SongRegistry::preserve_confirmed_activation_index_notification(
    const SelectionSnapshot& confirmed_selection,
    const int visible_index, const int base_slot,
    const bool exact_notification)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return exact_notification && !selection_guard_.song
        && visible_index == active_visible_index_
        && base_slot == active_base_slot_
        && selection_matches_locked(confirmed_selection);
}

bool SongRegistry::freeze_active_profile_for_activation(
    const SelectionSnapshot& confirmed_selection,
    const uint64_t reservation_generation,
    SelectionSnapshot& frozen_selection)
{
    frozen_selection = {};
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (reservation_generation == 0 || generation_ == UINT64_MAX
        || selection_guard_.song
        || !selection_matches_locked(confirmed_selection)
        || !confirmed_selection.song || !confirmed_selection.profile) {
        return false;
    }
    const SongDescriptor& song = *confirmed_selection.song;
    const int index = selected_profile_index(song, selected_profiles_);
    if (index != confirmed_selection.profile_index || index < 0
        || index >= static_cast<int>(song.profiles.size())
        || &song.profiles[static_cast<size_t>(index)]
            != confirmed_selection.profile) {
        return false;
    }
    profile_lock_song_id_ = song.id;
    profile_lock_index_ = index;
    profile_lock_reservation_generation_ = reservation_generation;
    ++generation_;
    frozen_selection.generation = generation_;
    frozen_selection.storage = songs_;
    frozen_selection.song = &song;
    frozen_selection.profile = &song.profiles[static_cast<size_t>(index)];
    frozen_selection.profile_index = index;
    frozen_selection.visible_index = active_visible_index_;
    frozen_selection.base_slot = active_base_slot_;
    return true;
}

bool SongRegistry::thaw_active_profile_for_activation(
    const SelectionSnapshot& frozen_selection,
    const uint64_t reservation_generation,
    SelectionSnapshot& thawed_selection)
{
    thawed_selection = {};
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (reservation_generation == 0 || generation_ == UINT64_MAX
        || selection_guard_.song
        || profile_lock_reservation_generation_ != reservation_generation
        || !selection_matches_locked(frozen_selection)
        || !frozen_selection.song || !frozen_selection.profile
        || profile_lock_song_id_ != frozen_selection.song->id
        || profile_lock_index_ != frozen_selection.profile_index) {
        return false;
    }
    profile_lock_song_id_.clear();
    profile_lock_index_ = -1;
    profile_lock_reservation_generation_ = 0;
    ++generation_;
    thawed_selection = selection_snapshot_locked();
    return static_cast<bool>(thawed_selection);
}

void SongRegistry::clear_active_selection()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (selection_guard_.song) return;
    active_visible_index_ = -1;
    active_base_slot_ = -1;
    ++generation_;
}

ActiveSongSnapshot SongRegistry::active_snapshot() const
{
    return selection_snapshot();
}

SelectionSnapshot SongRegistry::selection_snapshot() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return selection_snapshot_locked();
}

SelectionSnapshot SongRegistry::selection_snapshot_locked() const
{
    SelectionSnapshot snapshot;
    snapshot.generation = generation_;
    snapshot.storage = songs_;
    snapshot.visible_index = active_visible_index_;
    snapshot.base_slot = active_base_slot_;
    for (const auto& song : *songs_) {
        if (song.visible_index != active_visible_index_) continue;
        snapshot.song = &song;
        const int index = selected_profile_index(song, selected_profiles_);
        if (index >= 0 && index < static_cast<int>(song.profiles.size())) {
            snapshot.profile_index = index;
            snapshot.profile = &song.profiles[static_cast<size_t>(index)];
        }
        return snapshot;
    }
    return snapshot;
}

RenderSnapshot SongRegistry::render_snapshot() const
{
    return g_render_contexts.empty() ? RenderSnapshot{} : g_render_contexts.back().snapshot;
}

PlaybackSnapshot SongRegistry::playback_snapshot() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return playback_;
}

bool SongRegistry::try_playback_snapshot(PlaybackSnapshot& snapshot) const noexcept
{
    snapshot = {};
    try {
        std::unique_lock lock(state_mutex_, std::try_to_lock);
        if (!lock) return false;
        snapshot = playback_;
        return true;
    } catch (...) { return false; }
}

CleanupLease SongRegistry::cleanup_lease() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return cleanup_;
}

SelectionSnapshot SongRegistry::snapshot_for_visible_index(int visible_index) const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    SelectionSnapshot snapshot;
    snapshot.generation = generation_;
    snapshot.storage = songs_;
    snapshot.visible_index = visible_index;
    for (const auto& song : *songs_) {
        if (song.visible_index != visible_index) continue;
        snapshot.song = &song;
        snapshot.base_slot = song.base_slot;
        const int index = selected_profile_index(song, selected_profiles_);
        if (index >= 0 && index < static_cast<int>(song.profiles.size())) {
            snapshot.profile_index = index;
            snapshot.profile = &song.profiles[static_cast<size_t>(index)];
        }
        return snapshot;
    }
    return snapshot;
}

bool SongRegistry::initialize_profile_if_absent(
    const SelectionSnapshot& expected, const int profile_index,
    SelectionSnapshot& initialized)
{
    const SelectionSnapshot expected_copy = expected;
    initialized = {};
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!expected_copy.song || !expected_copy.profile
        || expected_copy.storage != songs_ || profile_index < 0
        || profile_index >= static_cast<int>(expected_copy.song->profiles.size())) {
        return false;
    }
    const SongDescriptor* current_song = nullptr;
    for (const SongDescriptor& song : *songs_) {
        if (&song == expected_copy.song) {
            current_song = &song;
            break;
        }
    }
    if (!current_song || expected_copy.profile_index < 0
        || expected_copy.profile_index >= static_cast<int>(current_song->profiles.size())
        || &current_song->profiles[static_cast<std::size_t>(expected_copy.profile_index)]
            != expected_copy.profile) {
        return false;
    }

    auto selected = selected_profiles_.find(current_song->id);
    if (expected_copy.generation != generation_
        && selected == selected_profiles_.end()) return false;
    if (selected == selected_profiles_.end()) {
        if (profile_initialization_mutation_blocked_locked()) return false;
        selected = selected_profiles_.emplace(
            current_song->id, current_song->profiles[static_cast<size_t>(profile_index)].difficulty).first;
        ++generation_;
    }
    const int selected_index = selected_profile_index(*current_song, selected_profiles_);
    if (selected_index < 0
        || selected_index >= static_cast<int>(current_song->profiles.size())) {
        return false;
    }
    initialized.generation = generation_;
    initialized.storage = songs_;
    initialized.song = current_song;
    initialized.profile_index = selected_index;
    initialized.profile = &current_song->profiles[static_cast<std::size_t>(selected_index)];
    initialized.visible_index = expected_copy.visible_index;
    initialized.base_slot = expected_copy.base_slot;
    return true;
}

InitializedProfileState SongRegistry::initialized_profile_state(
    const SelectionSnapshot& expected, SelectionSnapshot& initialized) const
{
    initialized = {};
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!expected.song || !expected.profile || expected.generation != generation_
        || expected.storage != songs_ || expected.profile_index < 0) {
        return InitializedProfileState::Invalid;
    }
    const SongDescriptor* current_song = nullptr;
    for (const SongDescriptor& song : *songs_) {
        if (&song == expected.song) {
            current_song = &song;
            break;
        }
    }
    if (!current_song
        || expected.profile_index >= static_cast<int>(current_song->profiles.size())
        || &current_song->profiles[static_cast<std::size_t>(expected.profile_index)]
            != expected.profile) {
        return InitializedProfileState::Invalid;
    }
    const auto selected = selected_profiles_.find(current_song->id);
    if (selected == selected_profiles_.end()) {
        if (!profile_initialization_mutation_blocked_locked())
            return InitializedProfileState::Absent;
        initialized = expected;
        return InitializedProfileState::Deferred;
    }
    const int selected_index = selected_profile_index(*current_song, selected_profiles_);
    if (selected_index < 0
        || selected_index >= static_cast<int>(current_song->profiles.size())) {
        return InitializedProfileState::Invalid;
    }
    initialized.generation = generation_;
    initialized.storage = songs_;
    initialized.song = current_song;
    initialized.profile_index = selected_index;
    initialized.profile
        = &current_song->profiles[static_cast<std::size_t>(selected_index)];
    initialized.visible_index = expected.visible_index;
    initialized.base_slot = expected.base_slot;
    return InitializedProfileState::Present;
}

bool SongRegistry::profile_initialization_mutation_blocked_locked() const noexcept
{
    return generation_ == UINT64_MAX || !profile_lock_song_id_.empty()
        || profile_lock_index_ >= 0
        || profile_lock_reservation_generation_ != 0
        || playback_.song || cleanup_.song || selection_guard_.song
        || selection_guard_token_.valid();
}

bool SongRegistry::publish_playback(const SelectionSnapshot& selection, const CustomContextToken& token)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (cleanup_.token.same_lease(token)
        && (cleanup_.failed_expansion_wrapper || (cleanup_.chart_admission.binding
            && cleanup_.chart_admission.state == ChartAdmissionState::Failed))) return false;
    if (selection_guard_.song || !selection_matches_locked(selection)
        || token.registry_generation != generation_
        || !token.valid()) return false;
    last_played_ = selection;
    playback_ = {};
    static_cast<SelectionSnapshot&>(playback_) = selection;
    playback_.token = token;
    cleanup_ = {};
    static_cast<SelectionSnapshot&>(cleanup_) = selection;
    cleanup_.token = token;
    return true;
}

bool SongRegistry::selection_matches_locked(const SelectionSnapshot& selection) const
{
    if (!selection.song || !selection.profile || selection.generation != generation_
        || selection.storage != songs_ || selection.visible_index != active_visible_index_
        || selection.base_slot != active_base_slot_) return false;
    for (const auto& song : *songs_) {
        if (&song != selection.song || song.visible_index != active_visible_index_) continue;
        const int index = selected_profile_index(song, selected_profiles_);
        return index == selection.profile_index
            && index >= 0 && index < static_cast<int>(song.profiles.size())
            && &song.profiles[static_cast<size_t>(index)] == selection.profile;
    }
    return false;
}

bool SongRegistry::acquire_selection_guard(
    const SelectionSnapshot& selection, const CustomContextToken& token)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (cleanup_.token.same_lease(token)
        && (cleanup_.failed_expansion_wrapper || (cleanup_.chart_admission.binding
            && cleanup_.chart_admission.state == ChartAdmissionState::Failed))) return false;
    if (!selection_matches_locked(selection) || !token.valid()
        || token.registry_generation != generation_) return false;
    if (selection_guard_.song) {
        return selection_guard_token_.same_lease(token)
            && selection_guard_.generation == selection.generation
            && selection_guard_.song == selection.song
            && selection_guard_.profile == selection.profile;
    }
    selection_guard_ = selection;
    selection_guard_chart_ = {};
    selection_guard_token_ = token;
    return true;
}

bool SongRegistry::selection_guard_matches(
    const SelectionSnapshot& selection, const CustomContextToken& token) const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return selection_guard_.song && selection_guard_token_.same_lease(token)
        && selection_guard_.generation == selection.generation
        && selection_guard_.storage == selection.storage
        && selection_guard_.song == selection.song
        && selection_guard_.profile == selection.profile
        && selection_matches_locked(selection);
}

bool SongRegistry::release_selection_guard(const CustomContextToken& token)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!selection_guard_.song) return true;
    if (!selection_guard_token_.same_lease(token)) return false;
    selection_guard_ = {};
    selection_guard_token_ = {};
    selection_guard_chart_ = {};
    return true;
}

bool SongRegistry::publish_playback_from_selection_guard(
    const SelectionSnapshot& selection, const CustomContextToken& token)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (selection_guard_chart_.binding
        && selection_guard_chart_.state == ChartAdmissionState::Failed) return false;
    if (!selection_guard_.song || !selection_guard_token_.same_lease(token)
        || !selection_matches_locked(selection) || token.registry_generation != generation_
        || !token.valid()) return false;
    last_played_ = selection;
    playback_ = {};
    static_cast<SelectionSnapshot&>(playback_) = selection;
    playback_.token = token;
    playback_.chart_admission = selection_guard_chart_;
    cleanup_ = {};
    static_cast<SelectionSnapshot&>(cleanup_) = selection;
    cleanup_.token = token;
    cleanup_.chart_admission = selection_guard_chart_;
    selection_guard_ = {};
    selection_guard_token_ = {};
    selection_guard_chart_ = {};
    return true;
}

bool SongRegistry::update_playback_token(
    const CustomContextToken& expected, const CustomContextToken& replacement)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!playback_.song || !playback_.token.same_lease(expected)
        || playback_.token.route_generation != expected.route_generation
        || !replacement.valid() || !expected.same_lease(replacement)
        || replacement.route_generation < expected.route_generation) return false;
    playback_.token = replacement;
    if (cleanup_.song && cleanup_.token.same_lease(expected)) cleanup_.token = replacement;
    return true;
}

bool SongRegistry::revoke_playback(const CustomContextToken& token)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!playback_.song) return cleanup_.song && cleanup_.token.same_lease(token);
    if (!playback_.token.same_lease(token)) return false;
    cleanup_ = {};
    static_cast<SelectionSnapshot&>(cleanup_) = static_cast<const SelectionSnapshot&>(playback_);
    cleanup_.token = playback_.token;
    cleanup_.chart_admission = playback_.chart_admission;
    if (cleanup_.chart_admission.binding)
        cleanup_.chart_admission.state = ChartAdmissionState::Failed;
    playback_ = {};
    return true;
}

bool SongRegistry::fail_chart_expansion(const SelectionSnapshot& selection,
    const CustomContextToken& token, void* wrapper)
{
    std::lock_guard lock(state_mutex_);
    if (!wrapper || !token.valid()) return false;
    const auto exact = [&](const SelectionSnapshot& current, const CustomContextToken& held) {
        return current.song && held.same_lease(token)
            && current.storage == selection.storage
            && selection_semantically_matches(current, selection);
    };
    if (exact(playback_, playback_.token)) {
        static_cast<SelectionSnapshot&>(cleanup_) = playback_;
        cleanup_.token = playback_.token;
        cleanup_.chart_admission = playback_.chart_admission;
        playback_ = {};
    } else if (exact(selection_guard_, selection_guard_token_)) {
        static_cast<SelectionSnapshot&>(cleanup_) = selection_guard_;
        cleanup_.token = selection_guard_token_;
        cleanup_.chart_admission = selection_guard_chart_;
        selection_guard_ = {};
        selection_guard_token_ = {};
        selection_guard_chart_ = {};
    } else if (!exact(cleanup_, cleanup_.token)) {
        return false;
    }
    cleanup_.failed_expansion_wrapper = wrapper;
    if (cleanup_.chart_admission.binding)
        cleanup_.chart_admission.state = ChartAdmissionState::Failed;
    return true;
}

bool SongRegistry::retire_cleanup_lease(const CustomContextToken& token)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (playback_.song && playback_.token.same_lease(token)) playback_ = {};
    if (!cleanup_.song) return true;
    if (!cleanup_.token.same_lease(token)) return false;
    cleanup_ = {};
    return true;
}

bool SongRegistry::commit_if_playback_token(
    const CustomContextToken& token, const std::function<bool()>& commit)
{
    if (!commit) return false;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!playback_.song || playback_.token != token) return false;
    return commit();
}

bool SongRegistry::attach_chart_admission(const SelectionSnapshot& selection,
    const CustomContextToken& token, std::shared_ptr<const ChordVoicingBinding> binding)
{
    std::lock_guard lock(state_mutex_);
    if (!binding || !token.valid() || !binding->lease.same_lease(token)
        || !selection_semantically_matches(binding->selection, selection)
        || binding->selection.storage != selection.storage) return false;
    if (selection_guard_.song && selection_guard_token_.same_lease(token)
        && selection_matches_locked(selection)) {
        selection_guard_chart_ = {std::move(binding)};
        return true;
    }
    if (playback_.song && playback_.token.same_lease(token)
        && selection_semantically_matches(playback_, selection)
        && playback_.storage == selection.storage) {
        playback_.chart_admission = {std::move(binding)};
        cleanup_.chart_admission = playback_.chart_admission;
        return true;
    }
    return false;
}

bool SongRegistry::seal_chart_admission(
    const std::shared_ptr<const ChordVoicingBinding>& binding, const NativeChartHeader& header)
{
    std::lock_guard lock(state_mutex_);
    if (!binding) return false;
    auto seal = [&](ChartAdmissionSnapshot& chart, const CustomContextToken& token) {
        if (chart.binding != binding || chart.state != ChartAdmissionState::Pending
            || !token.valid() || !token.same_lease(binding->lease)) return false;
        chart.header = header;
        chart.state = ChartAdmissionState::Ready;
        return true;
    };
    if (seal(selection_guard_chart_, selection_guard_token_)) return true;
    if (!seal(playback_.chart_admission, playback_.token)) return false;
    cleanup_.chart_admission = playback_.chart_admission;
    return true;
}

void SongRegistry::invalidate_chart_admission(void* wrapper)
{
    std::lock_guard lock(state_mutex_);
    for (auto* chart : {&selection_guard_chart_, &playback_.chart_admission,
             &cleanup_.chart_admission}) {
        if (chart->binding && chart->binding->wrapper == wrapper)
            chart->state = ChartAdmissionState::Failed;
    }
}

ChartAdmissionSnapshot SongRegistry::chart_admission_for_wrapper(void* wrapper) const
{
    std::lock_guard lock(state_mutex_);
    for (const auto* chart : {&selection_guard_chart_, &playback_.chart_admission,
             &cleanup_.chart_admission}) {
        if (chart->binding && chart->binding->wrapper == wrapper) return *chart;
    }
    return {};
}

bool SongRegistry::fail_chart_admission(const std::shared_ptr<const ChordVoicingBinding>& binding)
{
    std::lock_guard lock(state_mutex_);
    if (!binding) return false;
    bool matched = false;
    for (auto* chart : {&selection_guard_chart_, &playback_.chart_admission,
             &cleanup_.chart_admission}) {
        if (chart->binding == binding) {
            chart->state = ChartAdmissionState::Failed;
            matched = true;
        }
    }
    if (selection_guard_chart_.binding == binding && !playback_.song) {
        static_cast<SelectionSnapshot&>(cleanup_) = selection_guard_;
        cleanup_.token = selection_guard_token_;
        cleanup_.chart_admission = selection_guard_chart_;
        selection_guard_ = {};
        selection_guard_token_ = {};
        selection_guard_chart_ = {};
    }
    return matched;
}

ChartUpdateAdmission SongRegistry::chart_update_admission(void* wrapper)
{
    std::lock_guard lock(state_mutex_);
    if (wrapper && cleanup_.failed_expansion_wrapper == wrapper)
        return ChartUpdateAdmission::Rejected;
    for (auto* chart : {&selection_guard_chart_, &playback_.chart_admission,
             &cleanup_.chart_admission}) {
        if (!chart->binding || chart->binding->wrapper != wrapper) continue;
        if (chart == &playback_.chart_admission && playback_.token.valid()
            && playback_.token.same_lease(chart->binding->lease)
            && chart->state == ChartAdmissionState::Ready) return ChartUpdateAdmission::Ready;
        const bool first = !chart->rejection_reported;
        chart->rejection_reported = true;
        return first ? ChartUpdateAdmission::RejectedFirst : ChartUpdateAdmission::Rejected;
    }
    return ChartUpdateAdmission::Stock;
}

bool SongRegistry::commit_if_current_chart(
    const std::shared_ptr<const ChordVoicingBinding>& binding,
    bool (*commit)(const ChartAdmissionSnapshot&, void*) noexcept, void* context)
{
    std::lock_guard lock(state_mutex_);
    const auto& chart = playback_.chart_admission;
    return binding && chart.binding == binding && chart.state == ChartAdmissionState::Ready
        && playback_.token.valid() && playback_.token.same_lease(binding->lease)
        && commit && commit(chart, context);
}

bool SongRegistry::commit_if_selection_snapshot(
    const SelectionSnapshot& expected,
    SelectionSnapshot& observed_selection,
    PlaybackSnapshot& observed_playback,
    const std::function<bool()>& commit)
{
    if (!commit) return false;
    std::lock_guard<std::mutex> lock(state_mutex_);
    observed_selection = selection_snapshot_locked();
    observed_playback = playback_;
    if (!selection_matches_locked(expected)) return false;
    return commit();
}

bool SongRegistry::cycle_active_profile(int delta)
{
    if (delta == 0) return false;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (selection_guard_.song) return false;
    const SongDescriptor* song = nullptr;
    for (const auto& candidate : *songs_) {
        if (candidate.visible_index == active_visible_index_) {
            song = &candidate;
            break;
        }
    }
    if (!song || song->profiles.size() < 2 || profile_lock_song_id_ == song->id) return false;
    const int index = selected_profile_index(*song, selected_profiles_);
    if (index < 0 || index >= static_cast<int>(song->profiles.size())) return false;
    const int next = std::clamp(index + (delta < 0 ? -1 : 1), 0, static_cast<int>(song->profiles.size()) - 1);
    if (next == index) return false;
    selected_profiles_[song->id] = song->profiles[static_cast<size_t>(next)].difficulty;
    ++generation_;
    return true;
}

bool SongRegistry::cycle_active_profile_exact(
    const SelectionSnapshot& expected, const int delta,
    SelectionSnapshot& changed)
{
    changed = {};
    if (delta == 0) return false;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!selection_matches_locked(expected) || selection_guard_.song) return false;
    const SongDescriptor& song = *expected.song;
    if (song.profiles.size() < 2 || profile_lock_song_id_ == song.id) return false;
    const int index = selected_profile_index(song, selected_profiles_);
    if (index < 0 || index >= static_cast<int>(song.profiles.size()))
        return false;
    const int next = std::clamp(index + (delta < 0 ? -1 : 1), 0,
        static_cast<int>(song.profiles.size()) - 1);
    if (next == index) return false;
    selected_profiles_[song.id] = song.profiles[static_cast<size_t>(next)].difficulty;
    ++generation_;
    changed = selection_snapshot_locked();
    return static_cast<bool>(changed);
}

bool SongRegistry::freeze_active_profile()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (selection_guard_.song) return false;
    for (const auto& song : *songs_) {
        if (song.visible_index != active_visible_index_ || song.profiles.empty()) continue;
        const int index = selected_profile_index(song, selected_profiles_);
        if (index < 0 || index >= static_cast<int>(song.profiles.size())) return false;
        profile_lock_song_id_ = song.id;
        profile_lock_index_ = index;
        profile_lock_reservation_generation_ = 0;
        ++generation_;
        return true;
    }
    return false;
}

SelectionSnapshot SongRegistry::prepare_last_played_focus()
{
    std::lock_guard lock(state_mutex_);
    if (!last_played_ || generation_ == UINT64_MAX
        || playback_.song || selection_guard_.song || selection_guard_token_.valid()) return {};
    for (const auto& song : *songs_) {
        if (song.id != last_played_.song->id || song.profiles.empty()) continue;
        int index = song.default_profile_index;
        for (size_t i = 0; i < song.profiles.size(); ++i)
            if (song.profiles[i].difficulty == last_played_.profile->difficulty) index = static_cast<int>(i);
        if (index < 0 || index >= static_cast<int>(song.profiles.size())) return {};
        if (selected_profile_index(song, selected_profiles_) != index) {
            if (profile_initialization_mutation_blocked_locked()) return {};
            selected_profiles_[song.id] = song.profiles[static_cast<size_t>(index)].difficulty;
            ++generation_;
        }
        // An unchanged preference is usable immediately even while native audio
        // cleanup is retained. This neither thaws a profile nor mutates its lease.
        SelectionSnapshot result;
        result.storage = songs_; result.generation = generation_;
        result.song = &song; result.profile_index = index;
        result.profile = &song.profiles[static_cast<size_t>(index)];
        result.visible_index = song.visible_index; result.base_slot = song.base_slot;
        return result;
    }
    return {};
}

void SongRegistry::clear_last_played_focus()
{
    std::lock_guard lock(state_mutex_);
    last_played_ = {};
}

void SongRegistry::clear_frozen_profile()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (selection_guard_.song) return;
    profile_lock_song_id_.clear();
    profile_lock_index_ = -1;
    profile_lock_reservation_generation_ = 0;
    ++generation_;
}

int SongRegistry::active_visible_index() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return active_visible_index_;
}

int SongRegistry::active_base_slot() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return active_base_slot_;
}

size_t SongRegistry::custom_count() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return songs_->size();
}

SongRegistry& registry()
{
    return g_registry;
}

ScopedSongRenderContext::ScopedSongRenderContext(int visible_index, int base_slot)
{
    SelectionSnapshot selected = registry().snapshot_for_visible_index(visible_index);
    RenderSnapshot snapshot;
    static_cast<SelectionSnapshot&>(snapshot) = std::move(selected);
    snapshot.base_slot = base_slot;
    if (!snapshot.song) return;
    g_render_contexts.push_back({std::move(snapshot), visible_index, base_slot, {}});
    active_ = true;
}

ScopedSongRenderContext::ScopedSongRenderContext(SelectionSnapshot snapshot)
{
    if (!snapshot.song || !snapshot.storage) return;
    RenderSnapshot render;
    static_cast<SelectionSnapshot&>(render) = std::move(snapshot);
    g_render_contexts.push_back({std::move(render),
        g_render_contexts.empty() ? -1 : g_render_contexts.back().visible_index,
        g_render_contexts.empty() ? -1 : g_render_contexts.back().base_slot, {}});
    active_ = true;
}

ScopedSongRenderContext::~ScopedSongRenderContext()
{
    if (active_ && !g_render_contexts.empty()) g_render_contexts.pop_back();
}

bool song_render_context_matches(const SongDescriptor* song) noexcept
{
    return song && !g_render_contexts.empty() && g_render_contexts.back().snapshot.song == song;
}

bool retain_song_render_context_owner(std::shared_ptr<const void> owner) noexcept
{
    if (!owner || g_render_contexts.empty()) return false;
    try {
        g_render_contexts.back().retained_owners.push_back(std::move(owner));
        return true;
    } catch (...) {
        return false;
    }
}

bool selection_semantically_matches(
    const SelectionSnapshot& left, const SelectionSnapshot& right) noexcept
{
    return left && right && left.storage == right.storage
        && left.song == right.song && left.profile == right.profile
        && left.profile_index == right.profile_index
        && left.visible_index == right.visible_index
        && left.base_slot == right.base_slot;
}

bool registry_snapshot_owns_selection(
    const RegistrySnapshot& catalog, const SelectionSnapshot& selection) noexcept
{
    if (!catalog.storage || !selection || selection.storage != catalog.storage
        || selection.profile_index < 0) return false;
    for (const SongDescriptor& song : catalog.songs()) {
        if (&song != selection.song) continue;
        const auto index = static_cast<size_t>(selection.profile_index);
        return index < song.profiles.size()
            && &song.profiles[index] == selection.profile;
    }
    return false;
}

} // namespace ff7r::piano::game
