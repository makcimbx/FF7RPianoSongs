#include "game/extended_chart.h"

#include "core/logging.h"
#include "core/hooks.h"
#include "core/pe_image.h"
#include "core/generated/build_identity.generated.h"
#include "game/audio_sead.h"
#include "game/extended_chart_runtime_specs.h"
#include "game/hook_specs.h"
#include "game/generated/rvas.generated.h"
#include "game/song_registry.h"
#include "game/runtime_layouts.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/extended_chart_eligibility.h"
#include "pipeline/chart_event_plan.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace ff7r::piano::game {
namespace {

constexpr std::size_t kEventSize = 0x90;
constexpr std::size_t kNativeEventCount = ff7rp::pipeline::kMaxChartRows;
// The experiment never accepts storage beyond the canonical diagnostic-input bound.
constexpr int32_t kMaximumEventCapacity =
    static_cast<int32_t>(ff7rp::pipeline::kExperimentalMaxChartRows);
constexpr std::size_t kHeaderOffset = 0x80;
constexpr std::size_t kMaxTimeOffset = 0x30;
constexpr std::size_t kFrameRateOffset = 0x48;
constexpr std::size_t kRightSideOffset = 0x18;
constexpr std::size_t kLeftSideOffset = 0x10;
constexpr std::size_t kFinalGroupOffset = runtime_layouts::PianoScoreWrapper::final_group_index;
constexpr int32_t kFNameFind = 0;

struct EventHeader { void* data; int32_t count; int32_t capacity; };
struct FNameValue { int32_t index; int32_t number; };
static_assert(sizeof(EventHeader) == 16);
static_assert(sizeof(FNameValue) == 8);

using ReserveFn = void(__fastcall*)(EventHeader*, int32_t);
using FNameFn = void*(__fastcall*)(FNameValue*, const wchar_t*, int32_t);
using ConstructFn = void*(__fastcall*)(void*, void*, void*, uint32_t, float, float, uint8_t, uint8_t, uint64_t);
using DestructFn = void(__fastcall*)(void*);
using CallbackBuildFn = void*(__fastcall*)(void*, void**);
using IgnoreSoundInsertFn = uint32_t(__fastcall*)(void*, const uint64_t*);
using EventLinkFn = void(__fastcall*)(void*, void*);

struct RuntimeApi {
    FNameFn fname = nullptr;
    ConstructFn construct = nullptr;
    DestructFn destruct = nullptr;
    CallbackBuildFn callback_build = nullptr;
    IgnoreSoundInsertFn ignore_sound_insert = nullptr;
    EventLinkFn event_link = nullptr;
};

struct EventPlan {
    const SongChartNote* note = nullptr;
    ff7rp::pipeline::ChartEventEntry entry{};
    float time = 0.0f; // Set exactly once from the parser-published post-original FPS.
    uint64_t fname = 0;
    std::array<uint64_t, 3> ignore_sounds{};
    std::size_t ignore_sound_count = 0;
};

struct LinkJournalEntry {
    std::size_t root_index = 0;
    std::size_t child_index = 0;
    void* root_head = nullptr;
    void* child_parent = nullptr;
    void* child_next = nullptr;
};

struct Transaction {
    bool active = false;
    bool claimed = false;
    bool reserve_hit = false;
    bool reserve_mismatch = false;
    void* wrapper = nullptr;
    void* chart_row = nullptr;
    void* right_side = nullptr;
    void* left_side = nullptr;
    uintptr_t caller_rva = 0;
    EventHeader* expected_header = nullptr;
    void* reserved_data = nullptr;
    int32_t reserved_capacity = 0;
    int32_t source_row_count = 0;
    int32_t prefix_event_count = 0;
    int32_t native_event_count = 0;
    int32_t required_action_count = 0;
    int32_t expected_capacity = 0;
    std::size_t constructed_tail_count = 0;
    bool large_allocation_regime = false;
    std::vector<EventPlan> plans;
    ff7rp::pipeline::ChartEventPlan physical_plan;
    std::vector<int32_t> expected_parent;
    std::vector<int32_t> expected_next;
    std::vector<int32_t> expected_head;
    std::vector<LinkJournalEntry> link_journal;
    uint8_t original_group_byte = 0;
    bool group_byte_captured = false;
    bool links_applied = false;
    const SongDescriptor* song = nullptr;
    const SongDifficultyProfile* profile = nullptr;
    std::uint64_t registry_generation = 0;
    std::uint64_t policy_generation = 0;
    std::uint64_t descriptor_hash = 0;
    std::uint64_t physical_digest = 0;
    void* controller = nullptr;
    void* controller_control_block = nullptr;
    ChartAudioExpandTlsSnapshot tls{};
    SelectionAudioAdmissionAuthority authority{};
};

struct CommittedExtended {
    bool pending = false;
    bool active = false;
    std::shared_ptr<const void> storage;
    const SongDescriptor* song = nullptr;
    const SongDifficultyProfile* profile = nullptr;
    std::uint64_t registry_generation = 0;
    std::uint64_t policy_generation = 0;
    std::uint64_t descriptor_hash = 0;
    std::uint64_t selection_generation = 0;
    std::uint64_t route_generation = 0;
    std::uint64_t lease_generation = 0;
    std::uint64_t song_key = 0;
    std::uint64_t activation_generation = 0;
    std::uint64_t preparation_ordinal = 0;
    std::uint64_t route_lifecycle_epoch = 0;
    int32_t source_row_count = 0;
    int32_t prefix_event_count = 0;
    int32_t native_event_count = 0;
    int32_t required_action_count = 0;
    std::uint64_t physical_digest = 0;
};

struct ActivationSerialState {
    bool valid = false;
    std::uint64_t generation = 0;
    std::uint64_t route_lifecycle_epoch = 0;
    std::uint64_t preparation_ordinal = 0;
    bool failed = false;
};

// Diagnostic activation generations are process-monotonic. Keep failure
// evidence independently of transaction/token publication so a terminal that
// wins before begin cannot be forgotten. Lifecycle qualification prevents an
// old route lifetime from blocking a distinct lifetime.
struct FailedActivationWatermark {
    bool valid = false;
    std::uint64_t generation = 0;
    std::uint64_t route_lifecycle_epoch = 0;
};

struct SuccessfulLifecycleTransition {
    bool delivered = false;
    bool exact = false;
    std::uint64_t generation = 0;
    std::uint64_t lifecycle_epoch_before = 0;
    std::uint64_t lifecycle_epoch_after = 0;
};

thread_local Transaction g_transaction;
std::atomic_bool g_global_claim{false};
std::atomic_bool g_admissions{false};
HMODULE g_module = nullptr;
bool g_research_valid = false;
RuntimeApi g_api;
ReserveFn g_original_reserve = nullptr;
core::RawRvaHook g_reserve_hook;
core::HookCallbackGate g_reserve_gate;
std::mutex g_committed_mutex;
CommittedExtended g_committed;
ActivationSerialState g_activation_serial;
FailedActivationWatermark g_failed_activation_watermark;
SuccessfulLifecycleTransition g_successful_lifecycle_transition;

template <typename T>
bool read_at(const void* base, std::size_t offset, T& value) {
    return core::safe_copy_bytes(static_cast<const uint8_t*>(base) + offset, &value, sizeof(value));
}

template <typename T>
void write_at(void* base, std::size_t offset, const T& value) {
    std::memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(value));
}

bool stored_profile_shape_valid(const SongDifficultyProfile& profile) {
    const auto policy = ff7rp::pipeline::chart_row_policy_snapshot();
    const std::size_t rows = profile.source_row_count;
    return policy.playable_extended_available
        && rows > ff7rp::pipeline::kMaxChartRows
        && rows <= ff7rp::pipeline::kMaximumExtendedChartRows
        && profile.chart_notes.size() == ff7rp::pipeline::kMaxChartRows
        && profile.extended_chart_tail_notes.size() == rows - ff7rp::pipeline::kMaxChartRows
        && profile.native_prefix_event_count >= ff7rp::pipeline::kMaxChartRows
        && profile.native_prefix_event_count <= ff7rp::pipeline::kMaxChartRows * 2u
        && profile.native_event_count > profile.native_prefix_event_count
        && profile.native_event_count <= ff7rp::pipeline::kMaximumNativeChartEvents
        && profile.required_action_count > 0
        && profile.required_action_count <= profile.native_event_count
        && profile.note_count == static_cast<int32_t>(profile.required_action_count)
        && profile.physical_chart_digest != 0
        && profile.diagnostic_descriptor_hash != 0
        && profile.diagnostic_policy_generation == policy.generation;
}

const SongChartNote* profile_row(const SongDifficultyProfile& profile, const std::size_t index) {
    if (index < profile.chart_notes.size()) return &profile.chart_notes[index];
    const std::size_t tail = index - profile.chart_notes.size();
    return tail < profile.extended_chart_tail_notes.size()
        ? &profile.extended_chart_tail_notes[tail] : nullptr;
}

bool derive_runtime_plan(const SongDifficultyProfile& profile,
    ff7rp::pipeline::ChartEventPlan& plan) {
    if (!stored_profile_shape_valid(profile)) return false;
    std::vector<ff7rp::pipeline::ChartEventRow> rows;
    rows.reserve(profile.source_row_count);
    for (const SongChartNote& row : profile.chart_notes)
        rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(row));
    for (const SongChartNote& row : profile.extended_chart_tail_notes)
        rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(row));
    plan = ff7rp::pipeline::derive_chart_event_plan(rows);
    return plan.valid()
        && plan.source_row_count == profile.source_row_count
        && plan.native_prefix_event_count == profile.native_prefix_event_count
        && plan.native_event_count == profile.native_event_count
        && plan.required_action_count == profile.required_action_count
        && plan.physical_digest == profile.physical_chart_digest
        && plan.events.size() == plan.native_event_count
        && plan.required_action_count == plan.native_event_count - plan.links.size();
}

bool eligible_profile(const SongDifficultyProfile& profile) {
    try {
        ff7rp::pipeline::ChartEventPlan plan;
        return derive_runtime_plan(profile, plan);
    } catch (...) { return false; }
}

bool read_controller_binding(void* chart, void*& controller, void*& control_block)
{
    controller = nullptr;
    control_block = nullptr;
    void* reciprocal_chart = nullptr;
    return chart
        && core::safe_read_field(chart,
            runtime_layouts::PianoScoreWrapper::controller_capture, controller)
        && controller
        && core::safe_read_field(controller,
            runtime_layouts::PianoChartController::chart, reciprocal_chart)
        && reciprocal_chart == chart
        && core::safe_read_field(controller,
            runtime_layouts::PianoChartController::chart_control_block, control_block)
        && control_block;
}

bool exact_identity(const Transaction& tx, void* wrapper, void* chart_row, uintptr_t caller_rva) {
    void* controller = nullptr;
    void* control_block = nullptr;
    void* right_side = nullptr;
    void* left_side = nullptr;
    const uintptr_t chart_address = reinterpret_cast<uintptr_t>(wrapper);
    const bool header_exact = chart_address != 0
        && chart_address <= std::numeric_limits<uintptr_t>::max() - kHeaderOffset
        && tx.expected_header == reinterpret_cast<EventHeader*>(chart_address + kHeaderOffset);
    return tx.active && tx.claimed && g_global_claim.load(std::memory_order_acquire)
        && tx.wrapper == wrapper && tx.chart_row == chart_row
        && tx.caller_rva == caller_rva && caller_rva == rva::PersistentChartExpandCaller
        && selection_audio_admission_authority_matches(tx.authority)
        && tx.authority.selection.song == tx.song && tx.authority.selection.profile == tx.profile
        && tx.authority.selection.generation == tx.registry_generation
        && tx.policy_generation == ff7rp::pipeline::chart_row_policy_generation()
        && tx.profile && tx.profile->diagnostic_descriptor_hash == tx.descriptor_hash
        && tx.profile->physical_chart_digest == tx.physical_digest
        && core::safe_read_field(chart_row, kRightSideOffset, right_side)
        && right_side == tx.right_side
        && core::safe_read_field(chart_row, kLeftSideOffset, left_side)
        && left_side == tx.left_side
        && header_exact && read_controller_binding(wrapper, controller, control_block)
        && controller == tx.controller
        && control_block == tx.controller_control_block;
}

bool parse_time(const std::string& text, float fps, float& value) {
    const std::size_t split = text.find('_');
    if (split == std::string::npos || split == 0 || split + 1 == text.size()) return false;
    uint32_t seconds = 0, frames = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == split) continue;
        if (text[i] < '0' || text[i] > '9') return false;
        uint32_t& part = i < split ? seconds : frames;
        if (part > (std::numeric_limits<uint32_t>::max() - 9u) / 10u) return false;
        part = part * 10u + static_cast<uint32_t>(text[i] - '0');
    }
    value = static_cast<float>(seconds) + static_cast<float>(frames) / fps;
    return std::isfinite(value) && value >= 0.0f;
}

bool resolve_name(const std::string& text, uint64_t& packed) {
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), wide.data(), needed) != needed) return false;
    FNameValue value{};
    g_api.fname(&value, wide.c_str(), kFNameFind);
    std::memcpy(&packed, &value, sizeof(packed));
    return packed != 0;
}

bool callback_valid(const void* event) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_module);
    uintptr_t callback = 0, zero = 1, vtable = 0, chart = 0;
    return read_at(event, 0x50, callback) && read_at(event, 0x60, zero)
        && read_at(event, 0x70, vtable) && read_at(event, 0x78, chart)
        && callback == base + rva::PianoEventResultCallback && zero == 0
        && vtable == base + rva::PianoEventCallbackVtable;
}

bool callback_empty(const void* event) {
    std::array<uint8_t, 0x40> state{};
    std::array<uint8_t, 0x40> zero{};
    return core::safe_copy_bytes(static_cast<const uint8_t*>(event) + 0x50,
        state.data(), state.size()) && state == zero;
}

bool ignore_sounds_valid(const void* event, const EventPlan& plan) {
    uintptr_t data = 0;
    int32_t count = -1, capacity = -1;
    if (!read_at(event, 0x38, data) || !read_at(event, 0x40, count)
        || !read_at(event, 0x44, capacity)
        || count != static_cast<int32_t>(plan.ignore_sound_count) || capacity < count)
        return false;
    if (!count) return data == 0 && capacity == 0;
    if (!data) return false;
    std::array<uint64_t, 3> values{};
    return core::safe_copy_bytes(reinterpret_cast<void*>(data), values.data(),
        static_cast<std::size_t>(count) * sizeof(uint64_t))
        && std::equal(values.begin(), values.begin() + count, plan.ignore_sounds.begin());
}

bool ignore_sounds_empty(const void* event) {
    uintptr_t data = 1; int32_t count = -1, capacity = -1;
    return read_at(event, 0x38, data) && data == 0
        && read_at(event, 0x40, count) && count == 0
        && read_at(event, 0x44, capacity) && capacity == 0;
}

bool event_valid(const void* event, void* chart, void* side,
    const EventPlan& plan, bool require_callback, bool require_null_links = true,
    bool require_complete_ignore_sounds = true) {
    const SongChartNote& note = *plan.note;
    void* event_chart = nullptr; void* event_side = nullptr; uintptr_t parent = 1, successor = 1;
    uint32_t actual_ordinal = 0; float actual_time = -1.0f, strength = -1.0f;
    uint64_t actual_fname = 0;
    uint8_t assignment = 8, lookup_path = 2, state = 1, note_type = 0, dot_type = 0; uint8_t trailing[3]{};
    return read_at(event, 0x00, event_chart) && event_chart == chart
        && read_at(event, 0x08, event_side) && event_side == side
        && read_at(event, 0x10, parent) && read_at(event, 0x18, successor)
        && (!require_null_links || (parent == 0 && successor == 0))
        && read_at(event, 0x20, actual_ordinal) && actual_ordinal == plan.entry.ordinal
        && read_at(event, 0x24, actual_time) && std::isfinite(actual_time) && actual_time == plan.time
        && read_at(event, 0x28, strength) && strength == 0.0f
        && read_at(event, 0x2c, actual_fname) && actual_fname == plan.fname
        && (require_complete_ignore_sounds
            ? ignore_sounds_valid(event, plan) : ignore_sounds_empty(event))
        && read_at(event, 0x48, assignment) && assignment != 8
        && read_at(event, 0x49, lookup_path) && lookup_path <= 1
        && read_at(event, 0x4a, state) && state == 0
        && read_at(event, 0x4b, note_type) && note_type == static_cast<uint8_t>(note.note_type)
        && read_at(event, 0x4c, dot_type) && dot_type == static_cast<uint8_t>(note.dot_type)
        && read_at(event, 0x4d, trailing) && trailing[0] == 0 && trailing[1] == 0 && trailing[2] == 0
        && (require_callback
            ? (callback_valid(event) && [&] { uintptr_t captured = 0; return read_at(event, 0x78, captured) && captured == reinterpret_cast<uintptr_t>(chart); }())
            : callback_empty(event));
}

bool memory_writable(void* address, std::size_t bytes) {
    uintptr_t cursor = reinterpret_cast<uintptr_t>(address);
    const uintptr_t end = cursor + bytes;
    if (end < cursor) return false;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info))
            || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            || !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}

bool event_extent_bytes(const int32_t capacity, std::size_t& bytes)
{
    if (capacity < ff7rp::pipeline::kMinimumExtendedChartRows || capacity > kMaximumEventCapacity
        || static_cast<std::size_t>(capacity) > std::numeric_limits<std::size_t>::max() / kEventSize) {
        return false;
    }
    bytes = static_cast<std::size_t>(capacity) * kEventSize;
    return true;
}

bool expected_event_capacity(const int32_t target, int32_t& capacity,
    bool& large_regime)
{
    if (target < static_cast<int32_t>(ff7rp::pipeline::kMinimumExtendedChartRows)
        || target > kMaximumEventCapacity) return false;
    constexpr uint64_t kSmallMaximum = 0x20000;
    constexpr uint64_t kSmallQuantum = 0x1000;
    constexpr uint64_t kLargeQuantum = 0x10000;
    const uint64_t raw = static_cast<uint64_t>(target) * kEventSize;
    large_regime = raw > kSmallMaximum;
    const uint64_t quantum = large_regime ? kLargeQuantum : kSmallQuantum;
    if (raw > std::numeric_limits<uint64_t>::max() - (quantum - 1)) return false;
    const uint64_t quantized = (raw + quantum - 1) & ~(quantum - 1);
    const uint64_t result = quantized / kEventSize;
    if (result < static_cast<uint64_t>(target)
        || result > static_cast<uint64_t>(kMaximumEventCapacity)) return false;
    capacity = static_cast<int32_t>(result);
    return target != kMaximumEventCapacity || capacity == kMaximumEventCapacity;
}

bool event_assignment(const void* event, uint8_t& assignment)
{
    return read_at(event, 0x48, assignment) && assignment != 8;
}

bool event_lookup_path(const void* event, uint8_t& lookup_path)
{
    return read_at(event, 0x49, lookup_path) && lookup_path <= 1;
}

bool exact_header_state(const Transaction& tx, const int32_t count, EventHeader& header)
{
    return tx.expected_header && tx.reserved_data
        && tx.reserved_capacity == tx.expected_capacity
        && tx.reserved_capacity >= tx.native_event_count
        && core::safe_copy_bytes(tx.expected_header, &header, sizeof(header))
        && header.data == tx.reserved_data && header.count == count
        && header.capacity == tx.reserved_capacity;
}

bool exact_private_tail_slot(const Transaction& tx, void* wrapper, void* chart_row,
    const uintptr_t caller_rva, const std::size_t tail_index, void*& tail)
{
    EventHeader header{};
    tail = nullptr;
    const std::size_t tail_count = static_cast<std::size_t>(tx.native_event_count)
        - static_cast<std::size_t>(tx.prefix_event_count);
    if (tail_index >= tail_count) return false;
    const std::size_t row = static_cast<std::size_t>(tx.prefix_event_count) + tail_index;
    if (row > std::numeric_limits<std::size_t>::max() / kEventSize) return false;
    const std::size_t offset = row * kEventSize;
    if (offset > std::numeric_limits<std::size_t>::max() - kEventSize) return false;
    if (!exact_identity(tx, wrapper, chart_row, caller_rva)
        || !exact_header_state(tx, tx.prefix_event_count, header)) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(header.data);
    if (base > (std::numeric_limits<uintptr_t>::max)() - offset) return false;
    tail = reinterpret_cast<void*>(base + offset);
    return memory_writable(tail, kEventSize);
}

bool publish_committed_extended(const Transaction& tx)
{
    std::lock_guard<std::mutex> lock(g_committed_mutex);
    const bool terminalized = g_failed_activation_watermark.valid
        && g_failed_activation_watermark.route_lifecycle_epoch
            == tx.authority.route_lifecycle_epoch
        && tx.authority.activation_generation
            <= g_failed_activation_watermark.generation;
    const bool lifecycle_handoff_invalid =
        g_successful_lifecycle_transition.delivered
        && g_successful_lifecycle_transition.generation
            >= tx.authority.activation_generation
        && (g_successful_lifecycle_transition.generation
                != tx.authority.activation_generation
            || !g_successful_lifecycle_transition.exact
            || tx.authority.route_lifecycle_epoch == UINT64_MAX
            || g_successful_lifecycle_transition.lifecycle_epoch_before
                != tx.authority.route_lifecycle_epoch
            || g_successful_lifecycle_transition.lifecycle_epoch_after
                != tx.authority.route_lifecycle_epoch + 1);
    if (terminalized || lifecycle_handoff_invalid || !g_activation_serial.valid
        || g_activation_serial.generation != tx.authority.activation_generation
        || g_activation_serial.route_lifecycle_epoch != tx.authority.route_lifecycle_epoch
        || g_activation_serial.preparation_ordinal != tx.authority.preparation_ordinal
        || g_activation_serial.failed) {
        try {
            core::log(core::LogLevel::Error,
                lifecycle_handoff_invalid
                    ? "[extended_chart] committed_extended=rejected reason=invalid_lifecycle_transition rollback=synchronous"
                    : "[extended_chart] committed_extended=rejected reason=failed_activation_terminal rollback=synchronous");
        } catch (...) {}
        return false;
    }
    g_committed = {};
    g_committed.pending = true;
    g_committed.storage = tx.authority.selection.storage;
    g_committed.song = tx.song;
    g_committed.profile = tx.profile;
    g_committed.registry_generation = tx.registry_generation;
    g_committed.policy_generation = tx.policy_generation;
    g_committed.descriptor_hash = tx.descriptor_hash;
    g_committed.selection_generation = tx.tls.selection_generation;
    g_committed.route_generation = tx.tls.route_generation;
    g_committed.lease_generation = tx.tls.lease_generation;
    g_committed.song_key = tx.tls.song_key;
    g_committed.activation_generation = tx.authority.activation_generation;
    g_committed.preparation_ordinal = tx.authority.preparation_ordinal;
    g_committed.route_lifecycle_epoch = tx.authority.route_lifecycle_epoch;
    g_committed.source_row_count = tx.source_row_count;
    g_committed.prefix_event_count = tx.prefix_event_count;
    g_committed.native_event_count = tx.native_event_count;
    g_committed.required_action_count = tx.required_action_count;
    g_committed.physical_digest = tx.physical_digest;
    return true;
}

bool committed_selection_matches_locked(const SelectionSnapshot& selection)
{
    return (g_committed.pending || g_committed.active)
        && g_admissions.load(std::memory_order_acquire) && g_committed.storage
        && selection.song == g_committed.song && selection.profile == g_committed.profile
        && selection.generation == g_committed.registry_generation
        && selection.generation == g_committed.selection_generation
        && selection.profile && eligible_profile(*selection.profile)
        && selection.profile->source_row_count == static_cast<std::size_t>(g_committed.source_row_count)
        && selection.profile->native_prefix_event_count == static_cast<std::size_t>(g_committed.prefix_event_count)
        && selection.profile->native_event_count == static_cast<std::size_t>(g_committed.native_event_count)
        && selection.profile->required_action_count == static_cast<std::size_t>(g_committed.required_action_count)
        && selection.profile->note_count == g_committed.required_action_count
        && selection.profile->physical_chart_digest == g_committed.physical_digest
        && g_committed.policy_generation == ff7rp::pipeline::chart_row_policy_generation()
        && selection.profile->diagnostic_descriptor_hash == g_committed.descriptor_hash;
}

bool committed_playback_identity_matches_locked(const PlaybackSnapshot& playback)
{
    return playback.token.valid() && committed_selection_matches_locked(playback)
        && playback.token.registry_generation == g_committed.registry_generation
        && playback.token.route_generation == g_committed.route_generation
        && playback.token.lease_generation == g_committed.lease_generation
        && playback.token.song_key == g_committed.song_key
        && g_activation_serial.valid && !g_activation_serial.failed
        && g_activation_serial.generation == g_committed.activation_generation
        && g_activation_serial.route_lifecycle_epoch
            == g_committed.route_lifecycle_epoch
        && g_activation_serial.preparation_ordinal
            == g_committed.preparation_ordinal;
}

enum class LifecycleTransitionMatch { Awaiting, Exact, Invalid };

LifecycleTransitionMatch committed_lifecycle_transition_locked()
{
    if (!g_successful_lifecycle_transition.delivered
        || g_successful_lifecycle_transition.generation
            < g_committed.activation_generation) {
        return LifecycleTransitionMatch::Awaiting;
    }
    if (g_successful_lifecycle_transition.generation
            != g_committed.activation_generation
        || !g_successful_lifecycle_transition.exact
        || g_committed.route_lifecycle_epoch == UINT64_MAX
        || g_successful_lifecycle_transition.lifecycle_epoch_before
            != g_committed.route_lifecycle_epoch
        || g_successful_lifecycle_transition.lifecycle_epoch_after
            != g_committed.route_lifecycle_epoch + 1) {
        return LifecycleTransitionMatch::Invalid;
    }
    return LifecycleTransitionMatch::Exact;
}

bool playback_absent(const PlaybackSnapshot& playback)
{
    return !playback.storage && !playback.song && !playback.profile
        && !playback.token.valid();
}

void release_transaction() {
    if (g_transaction.claimed) g_global_claim.store(false, std::memory_order_release);
    g_transaction = {};
}

void __fastcall reserve_detour(EventHeader* header, int32_t requested) noexcept {
    auto lease = g_reserve_gate.try_enter();
    Transaction& tx = g_transaction;
    const uintptr_t return_rva = reinterpret_cast<uintptr_t>(_ReturnAddress()) - reinterpret_cast<uintptr_t>(g_module);
    EventHeader observed{};
    const ChartAudioExpandTlsSnapshot live = current_chart_audio_expand_tls();
    const bool tls_exact = live.active && live.original_inflight && live.depth == 1
        && live.generation == tx.tls.generation && live.selection_generation == tx.tls.selection_generation
        && live.route_generation == tx.tls.route_generation && live.lease_generation == tx.tls.lease_generation
        && live.song_key == tx.tls.song_key && live.enter_ordinal == tx.tls.enter_ordinal;
    const bool candidate = lease && tx.active && tx.claimed
        && g_global_claim.load(std::memory_order_acquire)
        && !tx.reserve_hit && !tx.reserve_mismatch && tls_exact
        && requested == 512 && header == tx.expected_header
        && return_rva == rva::PianoEventVectorReserveCall + 5u
        && header && core::safe_copy_bytes(header, &observed, sizeof(observed))
        && observed.data == nullptr && observed.count == 0 && observed.capacity == 0
        && exact_identity(tx, tx.wrapper, tx.chart_row, tx.caller_rva);
    if (candidate) {
        tx.reserve_hit = true;
        g_original_reserve(header, tx.native_event_count);
        EventHeader reserved{};
        if (core::safe_copy_bytes(header, &reserved, sizeof(reserved))
            && reserved.data && reserved.count == 0
            && reserved.capacity == tx.expected_capacity
            && reserved.capacity >= tx.native_event_count
            && reserved.capacity <= kMaximumEventCapacity) {
            tx.reserved_data = reserved.data;
            tx.reserved_capacity = reserved.capacity;
            try {
                std::ostringstream out;
                out << "[extended_chart] phase=reserve reserve_hit=1 reserve_validated=1"
                    << " requested=" << tx.native_event_count
                    << " capacity=" << reserved.capacity
                    << " allocation_regime=" << (tx.large_allocation_regime ? "large" : "small")
                    << " controller_reciprocal=exact header_relation=exact"
                    << " authority_source=selection_admission+synchronous_native";
                core::log(core::LogLevel::Info, out.str());
            } catch (...) {
                // Logging is non-authoritative after native reserve returns.
            }
        } else {
            tx.reserve_mismatch = true;
            try {
                core::log(core::LogLevel::Error,
                    "[extended_chart] reserve_hit=1 reserve_validated=0 failure=reserve_result");
            } catch (...) {
            }
        }
        return;
    }
    if (lease && tx.active && requested == 512) tx.reserve_mismatch = true;
    g_original_reserve(header, requested);
}

bool validate_shipping_helpers(HMODULE exe_module, std::string& mismatch)
{
    if (!exe_module) {
        mismatch = "missing_executable_module";
        return false;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(exe_module);
    for (const ExtendedChartCanonicalSpec& canonical : kExtendedChartCanonicalSpecs) {
        const RvaSignatureSpec* spec = find_rva_signature(canonical.signature_id);
        if (!spec || spec->rva != canonical.rva || spec->expected_prologue.empty()
            || !core::bytes_equal(reinterpret_cast<const uint8_t*>(base + spec->rva),
                spec->expected_prologue)) {
            mismatch = std::string(canonical.signature_id);
            return false;
        }
    }
    return true;
}

bool validate_research_helpers(HMODULE module, std::string& mismatch) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    constexpr std::string_view kExactBuild = "ff7rebirth-steam-win64-6a16ced2";
    constexpr std::array<uint8_t, 5> kExactExpandCall{0xe8, 0xb1, 0xf1, 0x01, 0x00};
    if (core::generated::kBuildId != kExactBuild
        || !rva::PersistentChartExpandCaller
        || runtime_layouts::PianoScoreWrapper::controller_capture != 0x118
        || runtime_layouts::PianoScoreWrapper::final_group_index != 0xa3
        || runtime_layouts::PianoChartController::chart != 0xf48
        || runtime_layouts::PianoChartController::chart_control_block != 0xf50
        || !core::bytes_equal(reinterpret_cast<const uint8_t*>(
                base + rva::PersistentChartExpandCaller - kExactExpandCall.size()),
            std::vector<uint8_t>(kExactExpandCall.begin(), kExactExpandCall.end()))) {
        mismatch = "persistent_chart_capture_layout_exact_1005";
        return false;
    }
    constexpr const char* ids[] = {"piano_event_vector_reserve", "piano_event_vector_reserve_call",
        "piano_event_callback_build", "piano_event_result_callback",
        "piano_event_ignore_sound_insert", "piano_event_ignore_sound_insert_call",
        "piano_event_link"};
    for (const char* id : ids) {
        const RvaSignatureSpec* spec = find_rva_signature(id);
        if (!spec || !spec->rva || spec->expected_prologue.empty()
            || !core::bytes_equal(reinterpret_cast<const uint8_t*>(base + spec->rva), spec->expected_prologue)) {
            mismatch = id; return false;
        }
    }
    constexpr uint8_t fname_signature[] = {
        0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0xb8,0x40,
        0x04,0x00,0x00,0xe8,0xa4,0xa3,0x61,0x01,0x48,0x2b,0xe0,0x48,0x8b,0x05,0x1a,0xb0};
    if (!rva::FNameCtor || !core::bytes_equal(reinterpret_cast<const uint8_t*>(base + rva::FNameCtor),
        std::vector<uint8_t>(std::begin(fname_signature), std::end(fname_signature)))) {
        mismatch = "fname_ctor_exact_1005"; return false;
    }
    uintptr_t slots[4]{};
    if (!rva::PianoEventCallbackVtable || !core::safe_copy_bytes(
        reinterpret_cast<void*>(base + rva::PianoEventCallbackVtable), slots, sizeof(slots))) {
        mismatch = "piano_event_callback_vtable"; return false;
    }
    constexpr uintptr_t expected[] = {0x02805da4, 0x01958660, 0x007a5680, 0x027fcc44};
    for (std::size_t i = 0; i < 4; ++i) if (slots[i] != base + expected[i]) {
        mismatch = "piano_event_callback_vtable_slots"; return false;
    }
    return true;
}

} // namespace

bool playable_extended_profile(const SongDifficultyProfile& profile) noexcept {
    const PlaybackSnapshot playback = registry().playback_snapshot();
    return playback.profile == &profile && playable_extended_playback(playback);
}

bool playable_extended_playback(const PlaybackSnapshot& playback) noexcept {
    std::lock_guard<std::mutex> lock(g_committed_mutex);
    if (g_committed.pending && playback_absent(playback)) {
        core::log(core::LogLevel::Debug,
            "[extended_chart] committed_extended=pending playback=absent publication=512");
        return false;
    }
    const bool identity_matches = committed_playback_identity_matches_locked(playback);
    const LifecycleTransitionMatch transition = identity_matches
        ? committed_lifecycle_transition_locked() : LifecycleTransitionMatch::Invalid;
    if (g_committed.pending && identity_matches
        && transition == LifecycleTransitionMatch::Awaiting) {
        core::log(core::LogLevel::Debug,
            "[extended_chart] committed_extended=pending playback_identity=exact lifecycle_transition=awaiting publication=512");
        return false;
    }
    const bool matches = identity_matches
        && transition == LifecycleTransitionMatch::Exact;
    if (g_committed.pending && matches) {
        g_committed.pending = false;
        g_committed.active = true;
        core::log(core::LogLevel::Info,
            "[extended_chart] committed_extended=active playback_identity=exact lifecycle_transition=exact");
    } else if ((g_committed.pending || g_committed.active) && !matches) {
        core::log(core::LogLevel::Info,
            "[extended_chart] committed_extended=invalidated reason=playback_publication_mismatch");
        g_committed = {};
    }
    return matches && g_committed.active;
}


bool playable_extended_presentation(
    const RenderSnapshot& menu, const PlaybackSnapshot& playback) noexcept
{
    std::lock_guard<std::mutex> lock(g_committed_mutex);
    // The two immutable snapshots are accepted as one coherent bundle only
    // when their shared selection generation and descriptor identities agree.
    // No retry or retained native-pointer dereference is used here.
    const bool menu_matches = committed_selection_matches_locked(menu);
    if (g_committed.pending && menu_matches && playback_absent(playback)) {
        core::log(core::LogLevel::Debug,
            "[extended_chart] committed_extended=pending presentation_playback=absent publication=512");
        return false;
    }
    const bool playback_identity_matches = menu_matches
        && committed_playback_identity_matches_locked(playback)
        && menu.storage && playback.storage
        && menu.song == playback.song && menu.profile == playback.profile
        && menu.generation == playback.generation;
    const LifecycleTransitionMatch transition = playback_identity_matches
        ? committed_lifecycle_transition_locked() : LifecycleTransitionMatch::Invalid;
    if (g_committed.pending && playback_identity_matches
        && transition == LifecycleTransitionMatch::Awaiting) {
        core::log(core::LogLevel::Debug,
            "[extended_chart] committed_extended=pending presentation_identity=exact lifecycle_transition=awaiting publication=512");
        return false;
    }
    const bool matches = playback_identity_matches
        && transition == LifecycleTransitionMatch::Exact;
    if (g_committed.pending && matches) {
        g_committed.pending = false;
        g_committed.active = true;
        core::log(core::LogLevel::Info,
            "[extended_chart] committed_extended=active playback_identity=exact lifecycle_transition=exact source=presentation");
    } else if ((g_committed.pending || g_committed.active) && !matches) {
        core::log(core::LogLevel::Info,
            "[extended_chart] committed_extended=invalidated reason=presentation_context_mismatch");
        g_committed = {};
    }
    return matches && g_committed.active;
}

void invalidate_extended_chart_commit(const char* reason) noexcept {
    std::lock_guard<std::mutex> lock(g_committed_mutex);
    if (g_committed.pending || g_committed.active) {
        std::ostringstream out;
        out << "[extended_chart] committed_extended=invalidated reason="
            << (reason ? reason : "unspecified")
            << " registry_generation=" << g_committed.registry_generation
            << " policy_generation=" << g_committed.policy_generation
            << " selection_generation=" << g_committed.selection_generation
            << " route_generation=" << g_committed.route_generation
            << " lease_generation=" << g_committed.lease_generation
            << " activation_generation=" << g_committed.activation_generation
            << " route_lifecycle_epoch=" << g_committed.route_lifecycle_epoch;
        core::log(core::LogLevel::Info, out.str());
    }
    g_committed = {};
}

void extended_chart_activation_terminal(const std::uint64_t activation_generation,
    const std::uint64_t route_lifecycle_epoch,
    const ChartAudioDiagnosticTerminalOutcome outcome,
    const std::uint64_t successful_lifecycle_epoch) noexcept
{
    try {
        if (outcome == ChartAudioDiagnosticTerminalOutcome::AudioPublished
            || outcome == ChartAudioDiagnosticTerminalOutcome::ExpandFinished) {
            if (outcome == ChartAudioDiagnosticTerminalOutcome::AudioPublished) {
                const bool exact = route_lifecycle_epoch != 0
                    && route_lifecycle_epoch != UINT64_MAX
                    && successful_lifecycle_epoch == route_lifecycle_epoch + 1;
                std::lock_guard<std::mutex> lock(g_committed_mutex);
                if (!g_successful_lifecycle_transition.delivered
                    || activation_generation
                        > g_successful_lifecycle_transition.generation) {
                    g_successful_lifecycle_transition = {true, exact,
                        activation_generation, route_lifecycle_epoch,
                        successful_lifecycle_epoch};
                }
                core::log(exact ? core::LogLevel::Info : core::LogLevel::Error,
                    exact
                        ? "[extended_chart] activation_terminal=audio_published lifecycle_transition=exact"
                        : "[extended_chart] activation_terminal=audio_published lifecycle_transition=invalid");
            }
            return;
        }
        std::lock_guard<std::mutex> lock(g_committed_mutex);
        const bool recorded = !g_failed_activation_watermark.valid
            || activation_generation > g_failed_activation_watermark.generation;
        if (recorded) {
            g_failed_activation_watermark = {
                true, activation_generation, route_lifecycle_epoch};
        }
        const bool exact_serial = g_activation_serial.valid
            && g_activation_serial.generation == activation_generation
            && g_activation_serial.route_lifecycle_epoch == route_lifecycle_epoch;
        if (exact_serial) g_activation_serial.failed = true;
        if (g_successful_lifecycle_transition.delivered
            && g_successful_lifecycle_transition.generation == activation_generation
            && g_successful_lifecycle_transition.lifecycle_epoch_before
                == route_lifecycle_epoch) {
            g_successful_lifecycle_transition = {};
        }
        const bool invalidated_after_publication =
            (g_committed.pending || g_committed.active)
            && g_committed.activation_generation == activation_generation
            && g_committed.route_lifecycle_epoch == route_lifecycle_epoch;
        if (invalidated_after_publication) g_committed = {};
        std::ostringstream out;
        out << "[extended_chart] activation_terminal=failed_recorded"
            << " activation_generation=" << activation_generation
            << " route_lifecycle_epoch=" << route_lifecycle_epoch
            << " outcome=" << static_cast<unsigned>(outcome)
            << " watermark_recorded=" << recorded
            << " exact_current=" << exact_serial;
        core::log(core::LogLevel::Info, out.str());
        if (invalidated_after_publication) {
            core::log(core::LogLevel::Info,
                "[extended_chart] committed_extended=invalidated reason=activation_terminal terminal_order=after_publication");
        }
    } catch (...) {
        // Terminal diagnostics must not unwind through native audio callbacks.
    }
}

ExtendedChartSupport configure_extended_chart_experiment(HMODULE exe_module, bool requested)
{
    invalidate_extended_chart_commit("configuration_change");
    {
        std::lock_guard<std::mutex> lock(g_committed_mutex);
        g_activation_serial = {};
        g_failed_activation_watermark = {};
        g_successful_lifecycle_transition = {};
    }
    ExtendedChartSupport support;
    support.requested = requested;
    if (!requested) {
        ff7rp::pipeline::configure_chart_row_limit(false, false, false);
        g_admissions.store(false, std::memory_order_release);
        core::log(core::LogLevel::Info,
            "[extended_chart] status=disabled effective_row_limit=512");
        return support;
    }

    std::string mismatch;
    support.shipping_helpers_valid = validate_shipping_helpers(exe_module, mismatch);
    std::string research_mismatch;
    g_research_valid = support.shipping_helpers_valid
        && validate_research_helpers(exe_module, research_mismatch);
    support.diagnostic_input_available = support.shipping_helpers_valid;
    support.mutation_available = false;
    ff7rp::pipeline::configure_chart_row_limit(true, support.diagnostic_input_available, false);
    g_module = g_research_valid ? exe_module : nullptr;
    if (g_module) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_module);
        g_api = {
            reinterpret_cast<FNameFn>(base + rva::FNameCtor),
            reinterpret_cast<ConstructFn>(base + rva::PianoEventConstruct),
            reinterpret_cast<DestructFn>(base + rva::PianoEventDestruct),
            reinterpret_cast<CallbackBuildFn>(base + rva::PianoEventCallbackBuild),
            reinterpret_cast<IgnoreSoundInsertFn>(base + rva::PianoEventIgnoreSoundInsert),
            reinterpret_cast<EventLinkFn>(base + rva::PianoEventLink),
        };
    } else {
        g_api = {};
    }
    support.policy_generation = ff7rp::pipeline::chart_row_policy_generation();

    std::ostringstream out;
    out << "[extended_chart] status=diagnostic_only"
        << " shipping_helpers_valid=" << support.shipping_helpers_valid
        << " diagnostic_input_available=" << support.diagnostic_input_available
        << " mutation_available=0 effective_row_limit="
        << ff7rp::pipeline::effective_chart_row_limit()
        << " accepted_input_limit=" << ff7rp::pipeline::chart_input_row_limit()
        << " policy_generation=" << support.policy_generation
        << " blocker=" << (g_research_valid
            ? "reserve_hook_not_installed" : "research_helper_or_build_mismatch");
    if (!mismatch.empty()) {
        out << " mismatch=" << mismatch;
    }
    if (!research_mismatch.empty()) out << " research_mismatch=" << research_mismatch;
    core::log(support.shipping_helpers_valid ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    return support;
}

bool install_extended_chart_reserve_hook(HMODULE exe_module, std::string& error) {
    if (!ff7rp::pipeline::experimental_extended_charts_requested()) return true;
    if (!g_module || exe_module != g_module) { error = "extended-chart helper verification unavailable"; return true; }
    if (g_original_reserve) { error = "extended-chart reserve hook already retained"; return true; }
    const HookSpec* spec = find_hook_spec("piano_event_vector_reserve");
    if (!spec || !spec->rva || spec->expected_prologue.empty()
        || !g_reserve_hook.install(exe_module, spec->rva, spec->expected_prologue,
            reinterpret_cast<void*>(&reserve_detour), reinterpret_cast<void**>(&g_original_reserve), error)) {
        // RawRvaHook retains the target/original only when failed-install cleanup could
        // not remove it. Keep forward-only callbacks accounted until shutdown retries.
        if (g_original_reserve) g_reserve_gate.open();
        ff7rp::pipeline::configure_chart_row_limit(true, true, false);
        core::log(core::LogLevel::Error, g_original_reserve
            ? "[extended_chart] requested=1 verified=1 mutation_available=0 forward_accounting=1 failure=reserve_hook_install_cleanup_retained"
            : "[extended_chart] requested=1 verified=1 mutation_available=0 forward_accounting=0 failure=reserve_hook_install");
        return true; // Optional experiment fails closed; base plugin remains functional.
    }
    g_reserve_gate.open();
    g_admissions.store(true, std::memory_order_release);
    ff7rp::pipeline::configure_chart_row_limit(true, true, true);
    core::log(core::LogLevel::Info, "[extended_chart] requested=1 verified=1 mutation_available=1");
    return true;
}

void begin_extended_chart_transaction(const ChartAudioExpandTlsSnapshot& transaction,
    const SelectionAudioAdmissionAuthority& authority,
    void* wrapper, void* chart_row, uintptr_t caller_rva) noexcept {
    int32_t diagnostic_target_count = 0;
    const auto reject = [&](const char* reason,
        const std::size_t tail_index = (std::numeric_limits<std::size_t>::max)()) noexcept {
        try {
            std::ostringstream out;
            out << "[extended_chart] transaction=rejected phase=begin reason=" << reason
                << " target_count=" << diagnostic_target_count;
            if (tail_index != (std::numeric_limits<std::size_t>::max)())
                out << " tail_index=" << tail_index;
            core::log(core::LogLevel::Error, out.str());
        } catch (...) {
            // Diagnostics are best-effort and never cross the native detour boundary.
        }
    };
    if (g_transaction.active) {
        g_transaction.reserve_mismatch = true;
        reject("nested_outer_transaction");
        return;
    }
    abort_extended_chart_transaction();
    if (!g_admissions.load(std::memory_order_acquire)) return reject("admission_closed");
    if (!transaction.active || transaction.depth != 1 || !transaction.original_inflight)
        return reject("tls_scope_mismatch");
    if (caller_rva != rva::PersistentChartExpandCaller) return reject("caller_mismatch");
    if (!wrapper || !chart_row) return reject("native_argument_missing");
    const SelectionSnapshot& selection = authority.selection;
    if (!selection_audio_admission_authority_matches(authority))
        return reject("selection_admission_mismatch");
    if (!selection.song || !selection.profile) return reject("selection_missing");
    diagnostic_target_count = static_cast<int32_t>(selection.profile->native_event_count);
    ff7rp::pipeline::ChartEventPlan physical_plan;
    try {
        if (!derive_runtime_plan(*selection.profile, physical_plan))
            return reject("profile_plan_mismatch");
    } catch (...) { return reject("profile_plan_exception"); }
    if (transaction.generation != authority.activation_generation
        || transaction.selection_generation != selection.generation
        || transaction.route_generation != authority.token.route_generation
        || transaction.lease_generation != authority.token.lease_generation
        || transaction.song_key != authority.token.song_key)
        return reject("activation_identity_mismatch");
    const int32_t source_rows = static_cast<int32_t>(physical_plan.source_row_count);
    const int32_t prefix_events = static_cast<int32_t>(physical_plan.native_prefix_event_count);
    const int32_t native_events = static_cast<int32_t>(physical_plan.native_event_count);
    const int32_t actions = static_cast<int32_t>(physical_plan.required_action_count);
    int32_t expected_capacity = 0;
    bool large_regime = false;
    std::vector<EventPlan> plans;
    std::vector<int32_t> expected_parent;
    std::vector<int32_t> expected_next;
    std::vector<int32_t> expected_head;
    std::vector<LinkJournalEntry> link_journal;
    try {
        if (!expected_event_capacity(native_events, expected_capacity, large_regime))
            return reject("target_capacity_invalid");
        plans.reserve(physical_plan.events.size());
        expected_parent.assign(physical_plan.events.size(), -1);
        expected_next.assign(physical_plan.events.size(), -1);
        expected_head.assign(physical_plan.events.size(), -1);
        link_journal.reserve(physical_plan.links.size());
        for (std::size_t i = 0; i < physical_plan.events.size(); ++i) {
            const auto& entry = physical_plan.events[i];
            const SongChartNote* note = profile_row(*selection.profile, entry.source_row_index);
            if (!note || entry.compact_event_index != i)
                return reject("event_plan_index_mismatch", i);
            EventPlan plan{};
            plan.note = note;
            plan.entry = entry;
            const std::string& event_name = entry.kind == ff7rp::pipeline::ChartEventKind::Monotone
                ? note->monotone_id : note->chord_id;
            if (!resolve_name(event_name, plan.fname))
                return reject("event_fname_find_failed", i);
            if (entry.kind == ff7rp::pipeline::ChartEventKind::Chord) {
                for (const std::string& id : note->ignore_sound_ids) {
                    if (id.empty()) continue;
                    uint64_t packed = 0;
                    if (!resolve_name(id, packed)) return reject("ignore_fname_find_failed", i);
                    bool duplicate = false;
                    for (std::size_t j = 0; j < plan.ignore_sound_count; ++j)
                        duplicate = duplicate || plan.ignore_sounds[j] == packed;
                    if (!duplicate) plan.ignore_sounds[plan.ignore_sound_count++] = packed;
                }
            }
            plans.push_back(plan);
        }
        for (const auto& link : physical_plan.links) {
            if (link.root_event_index >= plans.size() || link.child_event_index >= plans.size()
                || link.root_event_index == link.child_event_index
                || expected_parent[link.child_event_index] != -1
                || expected_parent[link.root_event_index] != -1)
                return reject("link_plan_invalid");
            expected_parent[link.child_event_index] = static_cast<int32_t>(link.root_event_index);
            expected_next[link.child_event_index] = expected_head[link.root_event_index];
            expected_head[link.root_event_index] = static_cast<int32_t>(link.child_event_index);
        }
        if (physical_plan.required_action_count
            != physical_plan.native_event_count - physical_plan.links.size())
            return reject("action_link_count_mismatch");
    } catch (...) {
        return reject("preflight_exception");
    }
    bool expected = false;
    if (!g_global_claim.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return reject("global_claim_busy");
    void* right_side = nullptr;
    void* left_side = nullptr;
    void* controller = nullptr;
    void* control_block = nullptr;
    const uintptr_t chart_address = reinterpret_cast<uintptr_t>(wrapper);
    if (!core::safe_read_field(chart_row, kRightSideOffset, right_side) || !right_side
        || !core::safe_read_field(chart_row, kLeftSideOffset, left_side) || !left_side
        || !read_controller_binding(wrapper, controller, control_block)
        || chart_address > std::numeric_limits<uintptr_t>::max() - kHeaderOffset) {
        g_global_claim.store(false, std::memory_order_release);
        return reject("synchronous_native_authority_failed");
    }
    {
        std::lock_guard<std::mutex> lock(g_committed_mutex);
        const bool terminalized = g_failed_activation_watermark.valid
            && g_failed_activation_watermark.route_lifecycle_epoch
                == authority.route_lifecycle_epoch
            && authority.activation_generation
                <= g_failed_activation_watermark.generation;
        if (terminalized || (g_activation_serial.valid
            && authority.activation_generation <= g_activation_serial.generation)) {
            g_global_claim.store(false, std::memory_order_release);
            return reject(terminalized
                ? "activation_already_terminalized" : "activation_generation_stale");
        }
        g_activation_serial = {true, authority.activation_generation,
            authority.route_lifecycle_epoch, authority.preparation_ordinal, false};
    }
    g_transaction = {};
    g_transaction.active = true;
    g_transaction.claimed = true;
    g_transaction.wrapper = wrapper;
    g_transaction.chart_row = chart_row;
    g_transaction.right_side = right_side;
    g_transaction.left_side = left_side;
    g_transaction.caller_rva = caller_rva;
    g_transaction.expected_header = reinterpret_cast<EventHeader*>(chart_address + kHeaderOffset);
    g_transaction.song = selection.song;
    g_transaction.profile = selection.profile;
    g_transaction.registry_generation = selection.generation;
    g_transaction.policy_generation = ff7rp::pipeline::chart_row_policy_generation();
    g_transaction.descriptor_hash = selection.profile->diagnostic_descriptor_hash;
    g_transaction.physical_digest = physical_plan.physical_digest;
    g_transaction.controller = controller;
    g_transaction.controller_control_block = control_block;
    g_transaction.tls = transaction;
    g_transaction.authority = authority;
    g_transaction.source_row_count = source_rows;
    g_transaction.prefix_event_count = prefix_events;
    g_transaction.native_event_count = native_events;
    g_transaction.required_action_count = actions;
    g_transaction.expected_capacity = expected_capacity;
    g_transaction.large_allocation_regime = large_regime;
    g_transaction.plans = std::move(plans);
    g_transaction.physical_plan = std::move(physical_plan);
    g_transaction.expected_parent = std::move(expected_parent);
    g_transaction.expected_next = std::move(expected_next);
    g_transaction.expected_head = std::move(expected_head);
    g_transaction.link_journal = std::move(link_journal);
    try {
        std::ostringstream out;
        out << "[extended_chart] transaction=admitted authority_source=selection_admission+synchronous_native"
            << " controller_capture=exact reciprocal=exact header_relation=exact"
            << " registry_generation=" << selection.generation
            << " policy_generation=" << g_transaction.policy_generation
            << " descriptor_hash=" << g_transaction.descriptor_hash
            << " R=" << source_rows << " P=" << prefix_events
            << " E=" << native_events << " A=" << actions
            << " tail_count=" << (native_events - prefix_events)
            << " allocation_regime=" << (large_regime ? "large" : "small")
            << " time_authority=post_original_pending";
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
        // Diagnostics are best-effort and never cross the native detour boundary.
    }
}

bool finish_extended_chart_transaction(void* wrapper, void* chart_row, uintptr_t caller_rva) noexcept {
    Transaction& tx = g_transaction;
    if (!tx.active) return false;
    bool success = false;
    const auto finish = [&] {
        if (!success) invalidate_extended_chart_commit("transaction_failed");
        release_transaction();
        return success;
    };
    const auto unresolved = [&](const char* reason) {
        block_custom_audio_route_for_unresolved_chart_mutation();
        try {
            std::ostringstream out;
            out << "[extended_chart] mutation_available=1 count_commit=unknown rollback=preserved"
                << " custom_route=blocked failure=" << reason
                << " R=" << tx.source_row_count << " P=" << tx.prefix_event_count
                << " E=" << tx.native_event_count << " A=" << tx.required_action_count;
            core::log(core::LogLevel::Error, out.str());
        } catch (...) {}
        return finish();
    };
    const auto reject = [&](const char* reason,
        const std::size_t tail_index = (std::numeric_limits<std::size_t>::max)()) {
        try {
            std::ostringstream out;
            out << "[extended_chart] mutation_available=1 reserve_hit=" << tx.reserve_hit
                << " substitution=" << (tx.reserve_hit && !tx.reserve_mismatch)
                << " count_commit=none rollback=none failure=" << reason
                << " registry_generation=" << tx.registry_generation
                << " policy_generation=" << tx.policy_generation;
            if (tail_index != (std::numeric_limits<std::size_t>::max)())
                out << " tail_index=" << tail_index;
            core::log(core::LogLevel::Error, out.str());
        } catch (...) {
            // Diagnostics are best-effort and cannot change rejection semantics.
        }
        return finish();
    };
    if (!tx.reserve_hit || tx.reserve_mismatch || !exact_identity(tx, wrapper, chart_row, caller_rva))
        return reject("reserve_or_identity");
    try {
        core::log(core::LogLevel::Info,
            "[extended_chart] phase=post_original authority_source=selection_admission+synchronous_native controller_capture_stable=1 reciprocal=exact header_relation=exact");
    } catch (...) {}
    EventHeader header{};
    std::size_t event_bytes = 0;
    if (!core::safe_copy_bytes(tx.expected_header, &header, sizeof(header)) || header.data != tx.reserved_data
        || header.count != tx.prefix_event_count || header.capacity < tx.native_event_count
        || header.capacity != tx.reserved_capacity
        || !event_extent_bytes(header.capacity, event_bytes) || !memory_writable(header.data, event_bytes))
        return reject("prefix_header");
    float fps = 0, old_max = 0;
    if (!read_at(wrapper, kFrameRateOffset, fps)) return reject("post_fps_read_failed");
    if (!std::isfinite(fps) || fps <= 0) return reject("post_fps_invalid");
    if (!read_at(wrapper, kMaxTimeOffset, old_max) || !std::isfinite(old_max))
        return reject("post_max_time_invalid");
    float prefix_max = 0, previous = 0;
    for (std::size_t i = 0; i < tx.plans.size(); ++i) {
        EventPlan& plan = tx.plans[i];
        if (!parse_time(plan.note->time_str, fps, plan.time) || (i && plan.time < previous))
            return reject("event_time_preflight", i);
        previous = plan.time;
        if (i < static_cast<std::size_t>(tx.prefix_event_count))
            prefix_max = std::max(prefix_max, plan.time);
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(tx.prefix_event_count); ++i) {
        const EventPlan& plan = tx.plans[i];
        void* const side = plan.entry.hand == ff7rp::pipeline::ChartEventHand::Right
            ? tx.right_side : tx.left_side;
        if (!event_valid(static_cast<uint8_t*>(header.data) + i * kEventSize,
                wrapper, side, plan, true, true))
            return reject("prefix_event", i);
    }
    if (old_max != prefix_max) return reject("prefix_max_time");
    const float new_max = std::max(old_max, previous);
    const auto cleanup_slot = [&](const std::size_t index) {
        void* slot = nullptr;
        if (!exact_private_tail_slot(tx, wrapper, chart_row, caller_rva, index, slot)) return false;
        g_api.destruct(slot);
        void* rechecked = nullptr;
        if (!exact_private_tail_slot(tx, wrapper, chart_row, caller_rva, index, rechecked)
            || rechecked != slot) return false;
        std::memset(slot, 0, kEventSize);
        return true;
    };
    const auto cleanup_constructed = [&]() {
        while (tx.constructed_tail_count) {
            const std::size_t index = tx.constructed_tail_count - 1;
            if (!cleanup_slot(index)) return false;
            --tx.constructed_tail_count;
        }
        return true;
    };
    const std::size_t prefix = static_cast<std::size_t>(tx.prefix_event_count);
    for (std::size_t compact = prefix; compact < tx.plans.size(); ++compact) {
        const std::size_t i = compact - prefix;
        const EventPlan& plan = tx.plans[compact];
        const SongChartNote& note = *plan.note;
        void* const side = plan.entry.hand == ff7rp::pipeline::ChartEventHand::Right
            ? tx.right_side : tx.left_side;
        void* slot_void = nullptr;
        if (!exact_private_tail_slot(tx, wrapper, chart_row, caller_rva, i, slot_void))
            return unresolved("tail_slot_identity_before_zero");
        auto* tail = static_cast<uint8_t*>(slot_void);
        std::memset(tail, 0, kEventSize);
        void* rechecked = nullptr;
        if (!exact_private_tail_slot(tx, wrapper, chart_row, caller_rva, i, rechecked)
            || rechecked != tail) return unresolved("tail_slot_identity_before_constructor");
        // Native allocator/constructor faults remain outside the recoverable contract.
        void* const constructed = g_api.construct(tail, wrapper, side, plan.entry.ordinal,
            plan.time, 0.0f, static_cast<uint8_t>(note.note_type),
            static_cast<uint8_t>(note.dot_type), plan.fname);
        if (constructed != tail) return unresolved("tail_constructor_ownership_unproved");
        if (!event_valid(tail, wrapper, side, plan, false, true, false)) {
            if (!cleanup_slot(i) || !cleanup_constructed())
                return unresolved("tail_constructor_cleanup_identity");
            return reject("tail_constructor_validation");
        }
        for (std::size_t ignored = 0; ignored < plan.ignore_sound_count; ++ignored) {
            const uint32_t inserted = g_api.ignore_sound_insert(tail + 0x38,
                &plan.ignore_sounds[ignored]);
            if (inserted != ignored) {
                if (!cleanup_slot(i) || !cleanup_constructed())
                    return unresolved("ignore_sound_cleanup_identity");
                return reject("ignore_sound_insert_validation", compact);
            }
        }
        if (!exact_private_tail_slot(tx, wrapper, chart_row, caller_rva, i, rechecked)
            || rechecked != tail)
            return unresolved("tail_identity_before_callback");
        if (!event_valid(tail, wrapper, side, plan, false, true, true)) {
            if (!cleanup_slot(i) || !cleanup_constructed())
                return unresolved("ignore_sound_validation_cleanup_identity");
            return reject("ignore_sound_vector_validation", compact);
        }
        void* captured = wrapper;
        g_api.callback_build(tail + 0x50, &captured);
        uint8_t assignment = 8, lookup_path = 2;
        if (!event_valid(tail, wrapper, side, plan, true, true, true)
            || !event_assignment(tail, assignment)
            || !event_lookup_path(tail, lookup_path)
            || !exact_identity(tx, wrapper, chart_row, caller_rva)) {
            if (!cleanup_slot(i) || !cleanup_constructed())
                return unresolved("tail_callback_cleanup_identity");
            return reject("tail_callback_validation");
        }
        ++tx.constructed_tail_count;
    }
    const auto tails_valid = [&](const int32_t expected_count) {
        if (tx.constructed_tail_count != tx.plans.size() - prefix) return false;
        EventHeader current{};
        if (!exact_identity(tx, wrapper, chart_row, caller_rva)
            || !exact_header_state(tx, expected_count, current)) return false;
        for (std::size_t i = 0; i < tx.constructed_tail_count; ++i) {
            const std::size_t row = prefix + i;
            if (row > (std::numeric_limits<std::size_t>::max)() / kEventSize)
                return false;
            const std::size_t offset = row * kEventSize;
            const uintptr_t base = reinterpret_cast<uintptr_t>(current.data);
            if (base > (std::numeric_limits<uintptr_t>::max)() - offset)
                return false;
            void* slot = reinterpret_cast<void*>(base + offset);
            const EventPlan& plan = tx.plans[row];
            void* const side = plan.entry.hand == ff7rp::pipeline::ChartEventHand::Right
                ? tx.right_side : tx.left_side;
            if (!memory_writable(slot, kEventSize)
                || !event_valid(slot, wrapper, side, plan, true, !tx.links_applied, true))
                return false;
        }
        return true;
    };

    const auto event_pointer = [&](const std::size_t index, const int32_t expected_count,
        void*& result) {
        result = nullptr;
        EventHeader current{};
        if (index >= tx.plans.size() || !exact_identity(tx, wrapper, chart_row, caller_rva)
            || !exact_header_state(tx, expected_count, current)
            || index > (std::numeric_limits<std::size_t>::max)() / kEventSize) return false;
        const std::size_t offset = index * kEventSize;
        const uintptr_t base = reinterpret_cast<uintptr_t>(current.data);
        if (base > (std::numeric_limits<uintptr_t>::max)() - offset) return false;
        result = reinterpret_cast<void*>(base + offset);
        return memory_writable(result, kEventSize);
    };
    const auto rollback_links = [&](const int32_t expected_count) {
        while (!tx.link_journal.empty()) {
            const LinkJournalEntry entry = tx.link_journal.back();
            void* root = nullptr; void* child = nullptr;
            if (!event_pointer(entry.root_index, expected_count, root)
                || !event_pointer(entry.child_index, expected_count, child)) return false;
            uintptr_t current_head = 0, current_parent = 0, current_next = 0;
            if (!read_at(root, 0x18, current_head)
                || current_head != reinterpret_cast<uintptr_t>(child)
                || !read_at(child, 0x10, current_parent)
                || current_parent != reinterpret_cast<uintptr_t>(root)
                || !read_at(child, 0x18, current_next)
                || current_next != reinterpret_cast<uintptr_t>(entry.root_head)) return false;
            write_at(root, 0x18, entry.root_head);
            write_at(child, 0x10, entry.child_parent);
            write_at(child, 0x18, entry.child_next);
            uintptr_t root_head = 1, child_parent = 1, child_next = 1;
            if (!read_at(root, 0x18, root_head) || root_head != reinterpret_cast<uintptr_t>(entry.root_head)
                || !read_at(child, 0x10, child_parent) || child_parent != reinterpret_cast<uintptr_t>(entry.child_parent)
                || !read_at(child, 0x18, child_next) || child_next != reinterpret_cast<uintptr_t>(entry.child_next))
                return false;
            tx.link_journal.pop_back();
        }
        if (tx.group_byte_captured) {
            write_at(wrapper, kFinalGroupOffset, tx.original_group_byte);
            uint8_t restored = 0xff;
            if (!read_at(wrapper, kFinalGroupOffset, restored) || restored != tx.original_group_byte)
                return false;
        }
        tx.links_applied = false;
        return true;
    };
    const auto links_valid = [&](const int32_t expected_count) {
        uint8_t group = 0;
        if (!read_at(wrapper, kFinalGroupOffset, group)
            || group != tx.physical_plan.final_group_index) return false;
        for (std::size_t i = 0; i < tx.plans.size(); ++i) {
            void* event = nullptr; void* parent_ptr = nullptr; void* successor_ptr = nullptr;
            if (!event_pointer(i, expected_count, event)) return false;
            const EventPlan& plan = tx.plans[i];
            void* const side = plan.entry.hand == ff7rp::pipeline::ChartEventHand::Right
                ? tx.right_side : tx.left_side;
            if (!event_valid(event, wrapper, side, plan, true, false, true)) return false;
            const int32_t parent_index = tx.expected_parent[i];
            const int32_t successor_index = tx.expected_head[i] >= 0
                ? tx.expected_head[i] : tx.expected_next[i];
            if (parent_index >= 0 && !event_pointer(parent_index, expected_count, parent_ptr)) return false;
            if (successor_index >= 0 && !event_pointer(successor_index, expected_count, successor_ptr)) return false;
            uintptr_t parent = 1, successor = 1;
            if (!read_at(event, 0x10, parent) || parent != reinterpret_cast<uintptr_t>(parent_ptr)
                || !read_at(event, 0x18, successor)
                || successor != reinterpret_cast<uintptr_t>(successor_ptr)) return false;
        }
        return true;
    };

    if (!read_at(wrapper, kFinalGroupOffset, tx.original_group_byte))
        return unresolved("group_state_read_before_links");
    if (tx.original_group_byte != 0) {
        if (!cleanup_constructed()) return unresolved("group_state_cleanup_identity");
        return reject("group_state_not_suppressed");
    }
    tx.group_byte_captured = true;
    for (const auto& link : tx.physical_plan.links) {
        void* root = nullptr; void* child = nullptr;
        if (!event_pointer(link.root_event_index, tx.prefix_event_count, root)
            || !event_pointer(link.child_event_index, tx.prefix_event_count, child))
            return unresolved("link_slot_identity");
        LinkJournalEntry journal{link.root_event_index, link.child_event_index};
        if (!read_at(root, 0x18, journal.root_head)
            || !read_at(child, 0x10, journal.child_parent)
            || !read_at(child, 0x18, journal.child_next))
            return unresolved("link_prestate");
        void* prior_head = nullptr;
        const int32_t prior_head_index = tx.expected_next[link.child_event_index];
        if (prior_head_index >= 0
            && !event_pointer(prior_head_index, tx.prefix_event_count, prior_head))
            return unresolved("link_prior_head_identity");
        if (journal.root_head != prior_head || journal.child_parent || journal.child_next) {
            if (!rollback_links(tx.prefix_event_count) || !cleanup_constructed())
                return unresolved("link_prestate_rollback_identity");
            return reject("link_prestate_mismatch");
        }
        tx.link_journal.push_back(journal);
        g_api.event_link(root, child);
    }
    EventHeader link_header{};
    if (!exact_identity(tx, wrapper, chart_row, caller_rva)
        || !exact_header_state(tx, tx.prefix_event_count, link_header))
        return unresolved("group_write_identity");
    write_at(wrapper, kFinalGroupOffset, tx.physical_plan.final_group_index);
    tx.links_applied = true;
    if (!links_valid(tx.prefix_event_count)) {
        if (!rollback_links(tx.prefix_event_count) || !cleanup_constructed())
            return unresolved("link_rollback_identity");
        return reject("link_validation");
    }
    float current_max = 0;
    if (!tails_valid(tx.prefix_event_count) || !links_valid(tx.prefix_event_count)
        || !read_at(wrapper, kMaxTimeOffset, current_max) || current_max != old_max)
        return unresolved("max_time_prewrite_identity");
    write_at(wrapper, kMaxTimeOffset, new_max);
    float committed_max = 0;
    if (!read_at(wrapper, kMaxTimeOffset, committed_max) || committed_max != new_max
        || !tails_valid(tx.prefix_event_count) || !links_valid(tx.prefix_event_count))
        return unresolved("max_time_postwrite_identity");
    if (!tails_valid(tx.prefix_event_count) || !links_valid(tx.prefix_event_count)
        || !read_at(wrapper, kMaxTimeOffset, committed_max) || committed_max != new_max)
        return unresolved("count_commit_identity");
    tx.expected_header->count = tx.native_event_count; // Ownership publication is deliberately last.
    EventHeader committed{};
    if (core::safe_copy_bytes(tx.expected_header, &committed, sizeof(committed))
        && committed.data == header.data && committed.count == tx.native_event_count
        && committed.capacity == header.capacity
        && read_at(wrapper, kMaxTimeOffset, committed_max) && committed_max == new_max
        && tails_valid(tx.native_event_count) && links_valid(tx.native_event_count)
        && exact_identity(tx, wrapper, chart_row, caller_rva)) {
        if (publish_committed_extended(tx)) {
            success = true;
            try {
                std::ostringstream out;
                out << "[extended_chart] prefix_validated=1 tails_constructed="
                    << tx.constructed_tail_count << " first_ordinal=" << tx.plans[prefix].entry.ordinal
                    << " last_ordinal=" << tx.plans.back().entry.ordinal
                    << " links=" << tx.physical_plan.links.size()
                    << " ignore_values=validated count_commit=" << tx.native_event_count
                    << " required_actions=" << tx.required_action_count
                    << " committed_extended=pending rollback=none authority_source=selection_admission+synchronous_native";
                core::log(core::LogLevel::Info, out.str());
            } catch (...) {}
            return finish();
        }
        try { core::log(core::LogLevel::Error,
            "[extended_chart] count_commit=extended publication=rejected reason=failed_terminal rollback=required"); }
        catch (...) {}
    }
    EventHeader live{};
    float live_max = 0;
    if (core::safe_copy_bytes(tx.expected_header, &live, sizeof(live)) && live.data == header.data
        && live.count == tx.native_event_count && live.capacity == header.capacity
        && read_at(wrapper, kMaxTimeOffset, live_max) && live_max == new_max
        && tails_valid(tx.native_event_count) && links_valid(tx.native_event_count)) {
        tx.expected_header->count = tx.prefix_event_count;
        EventHeader restored{};
        const bool count_restored = core::safe_copy_bytes(tx.expected_header, &restored, sizeof(restored))
            && restored.data == header.data && restored.count == tx.prefix_event_count && restored.capacity == header.capacity
            && exact_identity(tx, wrapper, chart_row, caller_rva);
        if (count_restored) {
            float rollback_max = 0;
            if (!tails_valid(tx.prefix_event_count) || !links_valid(tx.prefix_event_count)
                || !read_at(wrapper, kMaxTimeOffset, rollback_max) || rollback_max != new_max)
                return unresolved("post_count_max_restore_identity");
            write_at(wrapper, kMaxTimeOffset, old_max);
            float restored_max = 0;
            const bool max_restored = read_at(wrapper, kMaxTimeOffset, restored_max) && restored_max == old_max;
            if (!max_restored || !tails_valid(tx.prefix_event_count)
                || !links_valid(tx.prefix_event_count))
                return unresolved("post_count_max_restore_verification");
            if (rollback_links(tx.prefix_event_count) && cleanup_constructed()) {
                try { core::log(core::LogLevel::Error,
                    "[extended_chart] rollback=completed count_restore=proved max_restore=proved links_reverse_replayed=1 tails_reverse_cleaned=1 failure=post_count_validation"); }
                catch (...) {}
            } else {
                return unresolved("post_count_cleanup_identity");
            }
        } else {
            block_custom_audio_route_for_unresolved_chart_mutation();
                try { core::log(core::LogLevel::Error,
                    "[extended_chart] count_commit=extended rollback=preserved count_restore=unresolved failure=post_count_validation"); }
                catch (...) {}
        }
    } else {
        block_custom_audio_route_for_unresolved_chart_mutation();
        try { core::log(core::LogLevel::Error,
            "[extended_chart] count_commit=unknown rollback=preserved failure=external_drift"); }
        catch (...) {}
    }
    return finish();
}

void abort_extended_chart_transaction() noexcept {
    invalidate_extended_chart_commit("transaction_aborted");
    release_transaction();
}

core::HookTeardownOperation extended_chart_reserve_teardown_operation() {
    g_admissions.store(false, std::memory_order_release);
    return {
        [] { return core::disable_then_close_and_drain(g_reserve_gate,
            [] { return g_reserve_hook.disable(); }); },
        [] { return g_reserve_hook.remove(); },
    };
}

void clear_extended_chart_runtime_state() noexcept {
    abort_extended_chart_transaction();
    invalidate_extended_chart_commit("shutdown");
    g_admissions.store(false, std::memory_order_release);
    g_reserve_gate.close();
    g_original_reserve = nullptr; g_api = {}; g_module = nullptr; g_research_valid = false;
    {
        std::lock_guard<std::mutex> lock(g_committed_mutex);
        g_activation_serial = {};
        g_failed_activation_watermark = {};
        g_successful_lifecycle_transition = {};
    }
    ff7rp::pipeline::configure_chart_row_limit(
        ff7rp::pipeline::experimental_extended_charts_requested(), false, false);
}

} // namespace ff7r::piano::game
