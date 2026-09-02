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
constexpr std::size_t kSideOffset = 0x18;
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

struct RuntimeApi {
    FNameFn fname = nullptr;
    ConstructFn construct = nullptr;
    DestructFn destruct = nullptr;
    CallbackBuildFn callback_build = nullptr;
};

struct EventPlan {
    const SongChartNote* note = nullptr;
    float time = 0.0f; // Set exactly once from the parser-published post-original FPS.
    uint64_t fname = 0;
    uint32_t ordinal = 0;
};

struct Transaction {
    bool active = false;
    bool claimed = false;
    bool reserve_hit = false;
    bool reserve_mismatch = false;
    void* wrapper = nullptr;
    void* chart_row = nullptr;
    void* side = nullptr;
    uintptr_t caller_rva = 0;
    EventHeader* expected_header = nullptr;
    void* reserved_data = nullptr;
    int32_t reserved_capacity = 0;
    int32_t target_count = 0;
    int32_t expected_capacity = 0;
    std::size_t constructed_tail_count = 0;
    bool large_allocation_regime = false;
    std::vector<EventPlan> plans;
    const SongDescriptor* song = nullptr;
    const SongDifficultyProfile* profile = nullptr;
    std::uint64_t registry_generation = 0;
    std::uint64_t policy_generation = 0;
    std::uint64_t descriptor_hash = 0;
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
    int32_t target_count = 0;
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

bool restricted_note(const SongChartNote& note) {
    return !note.monotone_id.empty() && note.chord_id.empty() && note.group_index == 0
        && note.camera_switch_timing == 0 && note.dot_type == 0
        && (note.note_type == 2 || note.note_type == 3)
        && std::all_of(note.ignore_sound_ids.begin(), note.ignore_sound_ids.end(),
            [](const std::string& id) { return id.empty(); });
}

bool eligible_profile(const SongDifficultyProfile& profile) {
    const std::size_t target = profile.note_count > 0
        ? static_cast<std::size_t>(profile.note_count) : 0u;
    const auto policy = ff7rp::pipeline::chart_row_policy_snapshot();
    return ff7rp::pipeline::extended_chart_row_count_in_range(target)
        && policy.playable_extended_available
        && profile.diagnostic_source_rows == target
        && profile.diagnostic_native_prefix_rows == ff7rp::pipeline::kMaxChartRows
        && profile.diagnostic_tail_rows == target - ff7rp::pipeline::kMaxChartRows
        && profile.extended_chart_tail_notes.size() == profile.diagnostic_tail_rows
        && profile.diagnostic_descriptor_hash != 0
        && profile.diagnostic_policy_generation == policy.generation
        && profile.chart_notes.size() == ff7rp::pipeline::kMaxChartRows
        && std::all_of(profile.extended_chart_tail_notes.begin(),
            profile.extended_chart_tail_notes.end(), restricted_note)
        && std::all_of(profile.chart_notes.begin(), profile.chart_notes.end(), restricted_note);
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
    void* side = nullptr;
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
        && core::safe_read_field(chart_row, kSideOffset, side) && side == tx.side
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

bool event_valid(const void* event, void* chart, void* side, uint32_t ordinal,
    float time, const SongChartNote& note, uint64_t fname, bool require_callback) {
    void* event_chart = nullptr; void* event_side = nullptr; uintptr_t parent = 1, successor = 1;
    uint32_t actual_ordinal = 0; float actual_time = -1.0f, strength = -1.0f;
    uint64_t actual_fname = 0, owned = 1; int32_t owned_count = -1, owned_capacity = -1;
    uint8_t assignment = 8, lookup_path = 2, state = 1, note_type = 0, dot_type = 0; uint8_t trailing[3]{};
    return read_at(event, 0x00, event_chart) && event_chart == chart
        && read_at(event, 0x08, event_side) && event_side == side
        && read_at(event, 0x10, parent) && parent == 0 && read_at(event, 0x18, successor) && successor == 0
        && read_at(event, 0x20, actual_ordinal) && actual_ordinal == ordinal
        && read_at(event, 0x24, actual_time) && std::isfinite(actual_time) && actual_time == time
        && read_at(event, 0x28, strength) && strength == 0.0f
        && read_at(event, 0x2c, actual_fname) && actual_fname == fname
        && read_at(event, 0x38, owned) && owned == 0 && read_at(event, 0x40, owned_count) && owned_count == 0
        && read_at(event, 0x44, owned_capacity) && owned_capacity == 0
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
        && tx.reserved_capacity >= tx.target_count
        && core::safe_copy_bytes(tx.expected_header, &header, sizeof(header))
        && header.data == tx.reserved_data && header.count == count
        && header.capacity == tx.reserved_capacity;
}

bool exact_private_tail_slot(const Transaction& tx, void* wrapper, void* chart_row,
    const uintptr_t caller_rva, const std::size_t tail_index, void*& tail)
{
    EventHeader header{};
    tail = nullptr;
    const std::size_t tail_count = static_cast<std::size_t>(tx.target_count)
        - kNativeEventCount;
    if (tail_index >= tail_count) return false;
    const std::size_t row = kNativeEventCount + tail_index;
    if (row > std::numeric_limits<std::size_t>::max() / kEventSize) return false;
    const std::size_t offset = row * kEventSize;
    if (offset > std::numeric_limits<std::size_t>::max() - kEventSize) return false;
    if (!exact_identity(tx, wrapper, chart_row, caller_rva)
        || !exact_header_state(tx, 512, header)) return false;
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
        core::log(core::LogLevel::Error,
            lifecycle_handoff_invalid
                ? "[extended_chart] committed_extended=rejected reason=invalid_lifecycle_transition rollback=synchronous"
                : "[extended_chart] committed_extended=rejected reason=failed_activation_terminal rollback=synchronous");
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
    g_committed.target_count = tx.target_count;
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
        && selection.profile->note_count == g_committed.target_count
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
        g_original_reserve(header, tx.target_count);
        EventHeader reserved{};
        if (core::safe_copy_bytes(header, &reserved, sizeof(reserved))
            && reserved.data && reserved.count == 0
            && reserved.capacity == tx.expected_capacity
            && reserved.capacity >= tx.target_count
            && reserved.capacity <= kMaximumEventCapacity) {
            tx.reserved_data = reserved.data;
            tx.reserved_capacity = reserved.capacity;
            try {
                std::ostringstream out;
                out << "[extended_chart] phase=reserve reserve_hit=1 reserve_validated=1"
                    << " requested=" << tx.target_count
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
        || runtime_layouts::PianoChartController::chart != 0xf48
        || runtime_layouts::PianoChartController::chart_control_block != 0xf50
        || !core::bytes_equal(reinterpret_cast<const uint8_t*>(
                base + rva::PersistentChartExpandCaller - kExactExpandCall.size()),
            std::vector<uint8_t>(kExactExpandCall.begin(), kExactExpandCall.end()))) {
        mismatch = "persistent_chart_capture_layout_exact_1005";
        return false;
    }
    constexpr const char* ids[] = {"piano_event_vector_reserve", "piano_event_vector_reserve_call",
        "piano_event_callback_build", "piano_event_result_callback"};
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
    diagnostic_target_count = selection.profile->note_count;
    if (!eligible_profile(*selection.profile)) return reject("profile_ineligible");
    if (transaction.generation != authority.activation_generation
        || transaction.selection_generation != selection.generation
        || transaction.route_generation != authority.token.route_generation
        || transaction.lease_generation != authority.token.lease_generation
        || transaction.song_key != authority.token.song_key)
        return reject("activation_identity_mismatch");
    const int32_t target_count = selection.profile->note_count;
    int32_t expected_capacity = 0;
    bool large_regime = false;
    std::vector<EventPlan> plans;
    try {
        if (!expected_event_capacity(target_count, expected_capacity, large_regime))
            return reject("target_capacity_invalid");
        const std::size_t tail_count = static_cast<std::size_t>(target_count)
            - kNativeEventCount;
        plans.reserve(tail_count);
        for (std::size_t i = 0; i < tail_count; ++i) {
            const std::size_t row = kNativeEventCount + i;
            if (row > std::numeric_limits<uint32_t>::max() / 2u)
                return reject("tail_ordinal_overflow", i);
            const SongChartNote& note = selection.profile->extended_chart_tail_notes[i];
            EventPlan plan{&note, 0.0f, 0, static_cast<uint32_t>(row * 2u)};
            if (!restricted_note(note)) return reject("tail_shape_invalid", i);
            if (!resolve_name(note.monotone_id, plan.fname))
                return reject("tail_fname_find_failed", i);
            plans.push_back(plan);
        }
    } catch (...) {
        return reject("preflight_exception");
    }
    bool expected = false;
    if (!g_global_claim.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return reject("global_claim_busy");
    void* side = nullptr;
    void* controller = nullptr;
    void* control_block = nullptr;
    const uintptr_t chart_address = reinterpret_cast<uintptr_t>(wrapper);
    if (!core::safe_read_field(chart_row, kSideOffset, side) || !side
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
    g_transaction.side = side;
    g_transaction.caller_rva = caller_rva;
    g_transaction.expected_header = reinterpret_cast<EventHeader*>(chart_address + kHeaderOffset);
    g_transaction.song = selection.song;
    g_transaction.profile = selection.profile;
    g_transaction.registry_generation = selection.generation;
    g_transaction.policy_generation = ff7rp::pipeline::chart_row_policy_generation();
    g_transaction.descriptor_hash = selection.profile->diagnostic_descriptor_hash;
    g_transaction.controller = controller;
    g_transaction.controller_control_block = control_block;
    g_transaction.tls = transaction;
    g_transaction.authority = authority;
    g_transaction.target_count = target_count;
    g_transaction.expected_capacity = expected_capacity;
    g_transaction.large_allocation_regime = large_regime;
    g_transaction.plans = std::move(plans);
    try {
        std::ostringstream out;
        out << "[extended_chart] transaction=admitted authority_source=selection_admission+synchronous_native"
            << " controller_capture=exact reciprocal=exact header_relation=exact"
            << " registry_generation=" << selection.generation
            << " policy_generation=" << g_transaction.policy_generation
            << " descriptor_hash=" << g_transaction.descriptor_hash
            << " target_count=" << target_count
            << " tail_count=" << g_transaction.plans.size()
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
        std::ostringstream out;
        out << "[extended_chart] mutation_available=1 count_commit=unknown rollback=preserved"
            << " custom_route=blocked failure=" << reason
            << " registry_generation=" << tx.registry_generation
            << " policy_generation=" << tx.policy_generation;
        core::log(core::LogLevel::Error, out.str());
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
    core::log(core::LogLevel::Info,
        "[extended_chart] phase=post_original authority_source=selection_admission+synchronous_native controller_capture_stable=1 reciprocal=exact header_relation=exact");
    EventHeader header{};
    std::size_t event_bytes = 0;
    if (!core::safe_copy_bytes(tx.expected_header, &header, sizeof(header)) || header.data != tx.reserved_data
        || header.count != 512 || header.capacity < ff7rp::pipeline::kMinimumExtendedChartRows
        || header.capacity != tx.reserved_capacity
        || !event_extent_bytes(header.capacity, event_bytes) || !memory_writable(header.data, event_bytes))
        return reject("prefix_header");
    float fps = 0, old_max = 0;
    if (!read_at(wrapper, kFrameRateOffset, fps)) return reject("post_fps_read_failed");
    if (!std::isfinite(fps) || fps <= 0) return reject("post_fps_invalid");
    if (!read_at(wrapper, kMaxTimeOffset, old_max) || !std::isfinite(old_max))
        return reject("post_max_time_invalid");
    float prefix_max = 0, previous = 0;
    // The evidence proves sentinel 8, not a general numeric range. Use only assignment
    // values already produced by this exact parser-built monotone prefix.
    std::array<bool, 256> prefix_assignments{};
    uint8_t prefix_lookup_path = 2;
    for (std::size_t i = 0; i < kNativeEventCount; ++i) {
        float time = 0; uint64_t name = 0;
        uint8_t assignment = 8, lookup_path = 2;
        if (!parse_time(tx.profile->chart_notes[i].time_str, fps, time) || (i && time < previous)
            || !resolve_name(tx.profile->chart_notes[i].monotone_id, name)
            || !event_valid(static_cast<uint8_t*>(header.data) + i * kEventSize, wrapper, tx.side,
                static_cast<uint32_t>(2u * i), time, tx.profile->chart_notes[i], name, true)
            || !event_assignment(static_cast<uint8_t*>(header.data) + i * kEventSize, assignment)
            || !event_lookup_path(static_cast<uint8_t*>(header.data) + i * kEventSize, lookup_path)
            || (i && lookup_path != prefix_lookup_path))
            return reject("prefix_event");
        if (!i) prefix_lookup_path = lookup_path;
        prefix_assignments[assignment] = true;
        previous = time; prefix_max = std::max(prefix_max, time);
    }
    if (old_max != prefix_max) return reject("prefix_max_time");
    // The parser writes chart FPS only after its reserve call. Decode every tail
    // from that post-original value before constructing the first tail event.
    for (std::size_t i = 0; i < tx.plans.size(); ++i) {
        EventPlan& plan = tx.plans[i];
        if (!parse_time(plan.note->time_str, fps, plan.time))
            return reject("tail_time_parse_failed", i);
        if (plan.time < previous) return reject("tail_time_not_monotonic", i);
        previous = plan.time;
    }
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
    for (std::size_t i = 0; i < tx.plans.size(); ++i) {
        const EventPlan& plan = tx.plans[i];
        const SongChartNote& note = *plan.note;
        void* slot_void = nullptr;
        if (!exact_private_tail_slot(tx, wrapper, chart_row, caller_rva, i, slot_void))
            return unresolved("tail_slot_identity_before_zero");
        auto* tail = static_cast<uint8_t*>(slot_void);
        std::memset(tail, 0, kEventSize);
        void* rechecked = nullptr;
        if (!exact_private_tail_slot(tx, wrapper, chart_row, caller_rva, i, rechecked)
            || rechecked != tail) return unresolved("tail_slot_identity_before_constructor");
        // Native allocator/constructor faults remain outside the recoverable contract.
        void* const constructed = g_api.construct(tail, wrapper, tx.side, plan.ordinal,
            plan.time, 0.0f, static_cast<uint8_t>(note.note_type),
            static_cast<uint8_t>(note.dot_type), plan.fname);
        if (constructed != tail) return unresolved("tail_constructor_ownership_unproved");
        if (!event_valid(tail, wrapper, tx.side, plan.ordinal, plan.time,
                note, plan.fname, false)) {
            if (!cleanup_slot(i) || !cleanup_constructed())
                return unresolved("tail_constructor_cleanup_identity");
            return reject("tail_constructor_validation");
        }
        if (!exact_private_tail_slot(tx, wrapper, chart_row, caller_rva, i, rechecked)
            || rechecked != tail
            || !event_valid(tail, wrapper, tx.side, plan.ordinal, plan.time,
                note, plan.fname, false)) return unresolved("tail_identity_before_callback");
        void* captured = wrapper;
        g_api.callback_build(tail + 0x50, &captured);
        uint8_t assignment = 8, lookup_path = 2;
        if (!event_valid(tail, wrapper, tx.side, plan.ordinal, plan.time,
                note, plan.fname, true)
            || !event_assignment(tail, assignment) || !prefix_assignments[assignment]
            || !event_lookup_path(tail, lookup_path) || lookup_path != prefix_lookup_path
            || !exact_identity(tx, wrapper, chart_row, caller_rva)) {
            if (!cleanup_slot(i) || !cleanup_constructed())
                return unresolved("tail_callback_cleanup_identity");
            return reject("tail_callback_validation");
        }
        ++tx.constructed_tail_count;
    }
    const auto tails_valid = [&](const int32_t expected_count) {
        if (tx.constructed_tail_count != tx.plans.size()) return false;
        EventHeader current{};
        if (!exact_identity(tx, wrapper, chart_row, caller_rva)
            || !exact_header_state(tx, expected_count, current)) return false;
        for (std::size_t i = 0; i < tx.plans.size(); ++i) {
            const std::size_t row = kNativeEventCount + i;
            if (row > (std::numeric_limits<std::size_t>::max)() / kEventSize)
                return false;
            const std::size_t offset = row * kEventSize;
            const uintptr_t base = reinterpret_cast<uintptr_t>(current.data);
            if (base > (std::numeric_limits<uintptr_t>::max)() - offset)
                return false;
            void* slot = reinterpret_cast<void*>(base + offset);
            const EventPlan& plan = tx.plans[i];
            uint8_t assignment = 8;
            uint8_t lookup_path = 2;
            if (!memory_writable(slot, kEventSize)
                || !event_valid(slot, wrapper, tx.side, plan.ordinal, plan.time,
                    *plan.note, plan.fname, true)
                || !event_assignment(slot, assignment) || !prefix_assignments[assignment]
                || !event_lookup_path(slot, lookup_path)
                || lookup_path != prefix_lookup_path) return false;
        }
        return true;
    };
    float current_max = 0;
    if (!tails_valid(512)
        || !read_at(wrapper, kMaxTimeOffset, current_max) || current_max != old_max)
        return unresolved("max_time_prewrite_identity");
    write_at(wrapper, kMaxTimeOffset, new_max);
    float committed_max = 0;
    if (!read_at(wrapper, kMaxTimeOffset, committed_max) || committed_max != new_max
        || !tails_valid(512))
        return unresolved("max_time_postwrite_identity");
    if (!tails_valid(512)
        || !read_at(wrapper, kMaxTimeOffset, committed_max) || committed_max != new_max)
        return unresolved("count_commit_identity");
    tx.expected_header->count = tx.target_count; // Ownership publication is deliberately last.
    EventHeader committed{};
    if (core::safe_copy_bytes(tx.expected_header, &committed, sizeof(committed))
        && committed.data == header.data && committed.count == tx.target_count
        && committed.capacity == header.capacity
        && read_at(wrapper, kMaxTimeOffset, committed_max) && committed_max == new_max
        && tails_valid(tx.target_count)
        && exact_identity(tx, wrapper, chart_row, caller_rva)) {
        if (publish_committed_extended(tx)) {
            success = true;
            std::ostringstream out;
            out << "[extended_chart] prefix_validated=1 tails_constructed="
                << tx.constructed_tail_count << " first_ordinal=" << tx.plans.front().ordinal
                << " last_ordinal=" << tx.plans.back().ordinal
                << " callback_validated=1 count_commit=" << tx.target_count
                << " committed_extended=pending rollback=none authority_source=selection_admission+synchronous_native";
            core::log(core::LogLevel::Info, out.str());
            return finish();
        }
        core::log(core::LogLevel::Error,
            "[extended_chart] count_commit=extended publication=rejected reason=failed_terminal rollback=required");
    }
    EventHeader live{};
    float live_max = 0;
    if (core::safe_copy_bytes(tx.expected_header, &live, sizeof(live)) && live.data == header.data
        && live.count == tx.target_count && live.capacity == header.capacity
        && read_at(wrapper, kMaxTimeOffset, live_max) && live_max == new_max
        && tails_valid(tx.target_count)) {
        tx.expected_header->count = 512;
        EventHeader restored{};
        const bool count_restored = core::safe_copy_bytes(tx.expected_header, &restored, sizeof(restored))
            && restored.data == header.data && restored.count == 512 && restored.capacity == header.capacity
            && exact_identity(tx, wrapper, chart_row, caller_rva);
        if (count_restored) {
            float rollback_max = 0;
            if (!tails_valid(512)
                || !read_at(wrapper, kMaxTimeOffset, rollback_max) || rollback_max != new_max)
                return unresolved("post_count_max_restore_identity");
            write_at(wrapper, kMaxTimeOffset, old_max);
            float restored_max = 0;
            const bool max_restored = read_at(wrapper, kMaxTimeOffset, restored_max) && restored_max == old_max;
            if (!max_restored || !tails_valid(512))
                return unresolved("post_count_max_restore_verification");
            if (cleanup_constructed()) {
                core::log(core::LogLevel::Error,
                    "[extended_chart] rollback=completed count_restore=proved max_restore=proved tails_reverse_cleaned=1 failure=post_count_validation");
            } else {
                return unresolved("post_count_cleanup_identity");
            }
        } else {
            block_custom_audio_route_for_unresolved_chart_mutation();
            core::log(core::LogLevel::Error,
                    "[extended_chart] count_commit=extended rollback=preserved count_restore=unresolved failure=post_count_validation");
        }
    } else {
        block_custom_audio_route_for_unresolved_chart_mutation();
        core::log(core::LogLevel::Error, "[extended_chart] count_commit=unknown rollback=preserved failure=external_drift");
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
