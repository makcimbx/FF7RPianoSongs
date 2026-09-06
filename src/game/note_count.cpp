#include "game/note_count.h"

#include "core/hooks.h"
#include "core/pe_image.h"
#include "game/audio_sead.h"
#include "game/chord_voicing.h"
#include "game/completion_timing.h"
#include "game/completion_capture.h"
#include "game/duration.h"
#include "game/extended_chart.h"
#include "game/extended_chart_runtime_specs.h"
#include "game/module_hooks.h"
#include "game/hook_specs.h"
#include "game/rvas.h"
#include "game/runtime_layouts.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/extended_chart_eligibility.h"

#include "core/logging.h"

#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace ff7r::piano::game {
namespace {

std::atomic_int g_active_captured_note_count{0};
std::mutex g_completion_capture_mutex;
std::shared_ptr<const CompletionCapture> g_completion_capture;
bool g_completion_held = false;

using Mode48GenericHelperFn = uintptr_t(__fastcall*)(void* arg0, void* arg1, void* arg2, void* arg3);
using PianoScoreChartExpandFn = void(__fastcall*)(void* wrapper, void* chart_row, void* arg3, void* arg4);
using PianoChartUpdateFn = void(__fastcall*)(void* wrapper, float delta_seconds, void* arg3, void* arg4);

core::RawRvaHook g_note_count_hook;
core::RawRvaHook g_chart_expand_hook;
core::RawRvaHook g_chart_update_hook;
Mode48GenericHelperFn g_original_note_count = nullptr;
PianoScoreChartExpandFn g_original_chart_expand = nullptr;
PianoChartUpdateFn g_original_chart_update = nullptr;
uintptr_t g_module_base = 0;
std::atomic_uint64_t g_capture_id{0};
std::atomic_uint64_t g_chart_audio_expand_ordinal{0};
thread_local ChartAudioExpandTlsSnapshot g_chart_audio_expand_tls;

ChartAudioDiagnosticArraySnapshot chart_audio_array_snapshot(
    void* wrapper, const uintptr_t offset) noexcept
{
    ChartAudioDiagnosticArraySnapshot out;
    if (!wrapper) return out;
    void* data = nullptr;
    out.data_read = core::safe_read_field(wrapper, offset, data);
    out.count_read = core::safe_read_field(wrapper, offset + 0x8, out.count);
    out.capacity_read = core::safe_read_field(
        wrapper, offset + 0xc, out.capacity);
    out.data = reinterpret_cast<uintptr_t>(data);
    return out;
}

uint64_t next_chart_audio_expand_ordinal() noexcept
{
    const uint64_t current
        = g_chart_audio_expand_ordinal.load(std::memory_order_acquire);
    if (current == UINT64_MAX) return 0;
    const uint64_t next
        = g_chart_audio_expand_ordinal.fetch_add(1, std::memory_order_acq_rel) + 1;
    return next == 0 ? 0 : next;
}

class ChartAudioExpandTlsScope final {
public:
    ChartAudioExpandTlsScope(
        const ChartAudioDiagnosticTransaction& transaction,
        void* wrapper,
        ChartAudioExpandTlsSnapshot& completed) noexcept
        : previous_(g_chart_audio_expand_tls), wrapper_(wrapper), completed_(&completed)
    {
        if (!transaction.active || transaction.generation == 0) return;
        const uint64_t ordinal = next_chart_audio_expand_ordinal();
        if (ordinal == 0 || previous_.depth == UINT32_MAX) return;
        active_ = true;
        g_chart_audio_expand_tls = {};
        g_chart_audio_expand_tls.active = true;
        g_chart_audio_expand_tls.generation = transaction.generation;
        g_chart_audio_expand_tls.selection_generation
            = transaction.prewrite.selection_generation;
        g_chart_audio_expand_tls.route_generation
            = transaction.prewrite.route_generation;
        g_chart_audio_expand_tls.lease_generation
            = transaction.prewrite.lease_generation;
        g_chart_audio_expand_tls.song_key = transaction.prewrite.song_key;
        g_chart_audio_expand_tls.depth = previous_.depth + 1;
        g_chart_audio_expand_tls.enter_ordinal = ordinal;
        g_chart_audio_expand_tls.original_inflight = true;
        g_chart_audio_expand_tls.enter_time
            = chart_audio_array_snapshot(wrapper_, 0x70);
        g_chart_audio_expand_tls.enter_event
            = chart_audio_array_snapshot(wrapper_, 0x80);
    }

    ~ChartAudioExpandTlsScope() noexcept { finish(); }

    void finish() noexcept
    {
        if (!active_) return;
        g_chart_audio_expand_tls.exit_time
            = chart_audio_array_snapshot(wrapper_, 0x70);
        g_chart_audio_expand_tls.exit_event
            = chart_audio_array_snapshot(wrapper_, 0x80);
        g_chart_audio_expand_tls.exit_ordinal
            = next_chart_audio_expand_ordinal();
        g_chart_audio_expand_tls.original_inflight = false;
        if (completed_) *completed_ = g_chart_audio_expand_tls;
        g_chart_audio_expand_tls = previous_;
        active_ = false;
    }

private:
    ChartAudioExpandTlsSnapshot previous_{};
    void* wrapper_ = nullptr;
    ChartAudioExpandTlsSnapshot* completed_ = nullptr;
    bool active_ = false;
};

void log_chart_audio_expand_snapshot(
    const char* marker,
    const ChartAudioExpandTlsSnapshot& snapshot,
    const bool enter) noexcept
{
    try {
        if (!snapshot.active || snapshot.generation == 0) return;
        const auto& time = enter ? snapshot.enter_time : snapshot.exit_time;
        const auto& event = enter ? snapshot.enter_event : snapshot.exit_event;
        std::ostringstream out;
        out << "[chart_expand] chart_audio_transaction marker=" << marker
            << " generation=" << snapshot.generation
            << " selection_generation=" << snapshot.selection_generation
            << " route_generation=" << snapshot.route_generation
            << " lease_generation=" << snapshot.lease_generation
            << " song_key=" << snapshot.song_key
            << " depth=" << snapshot.depth
            << " enter_ordinal=" << snapshot.enter_ordinal
            << " exit_ordinal=" << snapshot.exit_ordinal
            << " original_inflight=" << snapshot.original_inflight
            << " time_data_read=" << time.data_read
            << " time_count_read=" << time.count_read
            << " time_capacity_read=" << time.capacity_read
            << " time_data=0x" << std::hex << time.data << std::dec
            << " time_count=" << time.count
            << " time_capacity=" << time.capacity
            << " event_data_read=" << event.data_read
            << " event_count_read=" << event.count_read
            << " event_capacity_read=" << event.capacity_read
            << " event_data=0x" << std::hex << event.data << std::dec
            << " event_count=" << event.count
            << " event_capacity=" << event.capacity;
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

bool valid_note_count(int note_count)
{
    return note_count > 0
        && note_count <= static_cast<int>(ff7rp::pipeline::effective_chart_row_limit());
}

bool install_named_hook(const HookInstallContext& context, const char* name, core::RawRvaHook& hook, void* detour, void** original)
{
    const HookSpec* spec = find_hook_spec(name);
    if (!spec) {
        std::ostringstream out;
        out << "[note_count] status=install_failed hook=" << name << " error=missing_hook_spec";
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    std::string error;
    if (!hook.install(context.exe_module, spec->rva, spec->expected_prologue, detour, original, error)) {
        std::ostringstream out;
        out << "[note_count] status=install_failed hook=" << name << " error=" << error;
        core::log(core::LogLevel::Error, out.str());
        return false;
    }

    std::ostringstream out;
    out << "[note_count] status=installed hook=" << name << " rva=0x" << std::hex << spec->rva;
    core::log(core::LogLevel::Info, out.str());
    return true;
}

struct InspectedEvent {
    float time = 0.0f;
    uint8_t state = 0;
    int64_t parent = -1;
    int64_t successor = -1;
    uintptr_t owned_data = 0;
    int32_t owned_count = 0;
    int32_t owned_capacity = 0;
    std::array<uint64_t, 8> referenced_state{};
    std::vector<uint64_t> owned_values;
};

bool checked_extent(const uintptr_t base, const std::size_t count, const std::size_t element_size,
    uintptr_t* end)
{
    if (!end || (count != 0 && element_size > std::numeric_limits<std::size_t>::max() / count)) return false;
    const std::size_t bytes = count * element_size;
    if (base > std::numeric_limits<uintptr_t>::max() - bytes) return false;
    *end = base + bytes;
    return true;
}

bool log_chart_event_plan(void* wrapper, int32_t expected_event_count,
    const SongDifficultyProfile* profile, std::uint64_t capture_id)
{
    constexpr uintptr_t kTimeArrayOffset = 0x70;
    constexpr uintptr_t kEventArrayOffset = 0x80;
    constexpr uintptr_t kEventParentOffset = 0x10;
    constexpr uintptr_t kEventSuccessorOffset = 0x18;
    constexpr uintptr_t kEventTimeOffset = 0x24;
    constexpr uintptr_t kEventOwnedDataOffset = 0x38;
    constexpr uintptr_t kEventOwnedCountOffset = 0x40;
    constexpr uintptr_t kEventOwnedCapacityOffset = 0x44;
    constexpr uintptr_t kEventStateOffset = 0x4a;
    constexpr uintptr_t kEventReferencedStateOffset = 0x50;
    constexpr uintptr_t kEventSize = 0x90;
    constexpr int32_t kMaximumEventCount =
        static_cast<int32_t>(ff7rp::pipeline::kMaximumExtendedChartRows);
    constexpr int32_t kMaximumOwnedCapacity = 64;
    constexpr int32_t kBoundaryEventCount = 16;
    constexpr int32_t kMaximumOwnedValues = 8;
    uintptr_t events = 0;
    int32_t event_count = 0;
    int32_t event_capacity = 0;
    uintptr_t times = 0;
    int32_t time_count = 0;
    int32_t time_capacity = 0;
    uint8_t active = 0;
    const bool event_pointer_read = wrapper && core::safe_read_field(wrapper, kEventArrayOffset, events);
    const bool event_count_read = wrapper && core::safe_read_field(wrapper, kEventArrayOffset + 0x8, event_count);
    const bool event_capacity_read = wrapper && core::safe_read_field(wrapper, kEventArrayOffset + 0xc, event_capacity);
    const bool time_pointer_read = wrapper && core::safe_read_field(wrapper, kTimeArrayOffset, times);
    const bool time_count_read = wrapper && core::safe_read_field(wrapper, kTimeArrayOffset + 0x8, time_count);
    const bool time_capacity_read = wrapper && core::safe_read_field(wrapper, kTimeArrayOffset + 0xc, time_capacity);
    const bool active_read = wrapper && core::safe_read_field(wrapper, 0xa0, active);
    uintptr_t events_end = 0;
    uintptr_t times_end = 0;
    const bool header_valid = event_pointer_read && event_count_read && event_capacity_read &&
        time_pointer_read && time_count_read && time_capacity_read && active_read &&
        event_count == expected_event_count && event_count > 0 && event_count <= event_capacity &&
        event_capacity <= kMaximumEventCount && time_count >= 0 && time_count <= time_capacity &&
        time_capacity <= kMaximumEventCount && events != 0 && (time_count == 0 || times != 0) &&
        checked_extent(events, static_cast<std::size_t>(event_capacity), kEventSize, &events_end) &&
        checked_extent(times, static_cast<std::size_t>(time_capacity), sizeof(uint64_t), &times_end);
    if (!header_valid) {
        std::ostringstream invalid;
        invalid << "[chart_event_plan] status=invalid capture_id=" << capture_id
            << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper) << std::dec
            << " event_pointer_read=" << event_pointer_read
            << " event_count_read=" << event_count_read
            << " event_capacity_read=" << event_capacity_read
            << " time_pointer_read=" << time_pointer_read
            << " time_count_read=" << time_count_read
            << " time_capacity_read=" << time_capacity_read
            << " active_read=" << active_read
            << " event_count=" << event_count << " expected_event_count=" << expected_event_count
            << " event_capacity=" << event_capacity << " time_count=" << time_count
            << " time_capacity=" << time_capacity;
        core::log(core::LogLevel::Error, invalid.str());
        return false;
    }

    if (time_count > 0) {
        uint64_t first_time = 0;
        uint64_t last_time = 0;
        if (!core::safe_read_field(reinterpret_cast<void*>(times), 0, first_time) ||
            !core::safe_read_field(reinterpret_cast<void*>(times),
                static_cast<uintptr_t>(time_count - 1) * sizeof(uint64_t), last_time)) {
            core::log(core::LogLevel::Error,
                "[chart_event_plan] status=invalid reason=time_allocation_read_failed capture_id=" +
                std::to_string(capture_id));
            return false;
        }
    }

    float maximum_time = -1.0f;
    int tail_events = 0;
    std::vector<InspectedEvent> inspected(static_cast<std::size_t>(event_count));
    std::ostringstream tail;
    std::ostringstream boundary;
    tail << std::fixed << std::setprecision(3);
    boundary << std::hex;
    const auto link_index = [&](uintptr_t link) {
        if (!link) {
            return int64_t{-1};
        }
        if (link < events || link >= events_end || ((link - events) % kEventSize) != 0) {
            return int64_t{-2};
        }
        return static_cast<int64_t>((link - events) / kEventSize);
    };
    for (int32_t index = 0; index < event_count; ++index) {
        const uintptr_t record = events + static_cast<uintptr_t>(index) * kEventSize;
        InspectedEvent& item = inspected[static_cast<std::size_t>(index)];
        uintptr_t parent = 0;
        uintptr_t successor = 0;
        const bool fields_read = core::safe_read_field(reinterpret_cast<void*>(record), kEventTimeOffset, item.time)
            && core::safe_read_field(reinterpret_cast<void*>(record), kEventStateOffset, item.state)
            && core::safe_read_field(reinterpret_cast<void*>(record), kEventParentOffset, parent)
            && core::safe_read_field(reinterpret_cast<void*>(record), kEventSuccessorOffset, successor);
        if (!fields_read || !std::isfinite(item.time) || item.time < 0.0f) {
            core::log(core::LogLevel::Error,
                "[chart_event_plan] status=invalid reason=event_field_read_failed capture_id=" +
                std::to_string(capture_id) + " event=" + std::to_string(index));
            return false;
        }
        item.parent = link_index(parent);
        item.successor = link_index(successor);
        if (item.parent == -2 || item.successor == -2) {
            core::log(core::LogLevel::Error,
                "[chart_event_plan] status=invalid reason=event_link_out_of_allocation capture_id=" +
                std::to_string(capture_id) + " event=" + std::to_string(index));
            return false;
        }
        if (!core::safe_read_field(reinterpret_cast<void*>(record), kEventOwnedDataOffset, item.owned_data) ||
            !core::safe_read_field(reinterpret_cast<void*>(record), kEventOwnedCountOffset, item.owned_count) ||
            !core::safe_read_field(reinterpret_cast<void*>(record), kEventOwnedCapacityOffset, item.owned_capacity) ||
            item.owned_count < 0 || item.owned_capacity < 0 || item.owned_count > item.owned_capacity ||
            item.owned_capacity > kMaximumOwnedCapacity || (item.owned_count > 0 && !item.owned_data) ||
            !core::safe_copy_bytes(reinterpret_cast<void*>(record + kEventReferencedStateOffset),
                item.referenced_state.data(), item.referenced_state.size() * sizeof(uint64_t))) {
            core::log(core::LogLevel::Error,
                "[chart_event_plan] status=invalid reason=event_owned_or_reference_read_failed capture_id=" +
                std::to_string(capture_id) + " event=" + std::to_string(index));
            return false;
        }
        uintptr_t owned_end = 0;
        if (!checked_extent(item.owned_data, static_cast<std::size_t>(item.owned_capacity),
                sizeof(uint64_t), &owned_end)) {
            core::log(core::LogLevel::Error,
                "[chart_event_plan] status=invalid reason=owned_extent_overflow capture_id=" +
                std::to_string(capture_id) + " event=" + std::to_string(index));
            return false;
        }
        item.owned_values.resize(static_cast<std::size_t>(item.owned_count));
        if (!item.owned_values.empty() && !core::safe_copy_bytes(reinterpret_cast<void*>(item.owned_data),
                item.owned_values.data(), item.owned_values.size() * sizeof(uint64_t))) {
            core::log(core::LogLevel::Error,
                "[chart_event_plan] status=invalid reason=owned_values_read_failed capture_id=" +
                std::to_string(capture_id) + " event=" + std::to_string(index));
            return false;
        }
        maximum_time = std::max(maximum_time, item.time);
        if (item.time >= 120.0f && tail_events < 96) {
            if (tail_events != 0) {
                tail << ',';
            }
            tail << index << ':' << item.time << ':' << static_cast<int>(item.state);
            ++tail_events;
        }
        if (ff7rp::pipeline::extended_chart_input_enabled()
            && index >= std::max(0, event_count - kBoundaryEventCount)) {
            boundary << " event=" << std::dec << index << std::hex
                     << " addr=0x" << record
                     << std::dec << " time=" << item.time << " state=" << static_cast<int>(item.state)
                     << " parent=" << item.parent << " successor=" << item.successor
                     << std::hex << " owned=0x" << item.owned_data << std::dec
                     << " owned_count=" << item.owned_count << " owned_capacity=" << item.owned_capacity
                     << " owned_values=";
            for (std::size_t owned_index = 0;
                owned_index < item.owned_values.size() && owned_index < kMaximumOwnedValues; ++owned_index) {
                    boundary << (owned_index == 0 ? "" : ",") << "0x" << std::hex
                        << item.owned_values[owned_index];
                }
            boundary << " ref_words=";
            for (std::size_t word = 0; word < item.referenced_state.size(); ++word) {
                boundary << (word == 0 ? "" : ",") << "0x" << std::hex << item.referenced_state[word];
            }
        }
    }

    std::ostringstream out;
    out << "[chart_event_plan] status=valid capture_id=" << capture_id
        << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper)
        << std::dec
        << " event_count=" << event_count
        << " event_capacity=" << event_capacity
        << " event_base=0x" << std::hex << events
        << " time_base=0x" << times << std::dec
        << " time_count=" << time_count
        << " time_capacity=" << time_capacity
        << " active=" << static_cast<int>(active)
        << " header_reads_succeeded=1 event_reads_succeeded=1 ownership_reads_succeeded=1"
        << " max_event_time=" << maximum_time
        << " profile_last_prompt=" << profile_last_prompt_seconds(profile)
        << " tail_events=" << tail_events
        << " tail=" << tail.str()
        << " boundary=" << boundary.str();
    core::log(core::LogLevel::Info, out.str());
    return true;
}

uintptr_t to_rva(void* caller_address)
{
    if (!g_module_base) {
        return 0;
    }
    const auto caller = reinterpret_cast<uintptr_t>(caller_address);
    return caller >= g_module_base ? caller - g_module_base : 0;
}

void __fastcall chart_expand_detour(
    void* wrapper, void* chart_row, void* arg3, void* arg4) noexcept
{
    // Any expansion may replace the native chart allocation. Revoke the prior
    // success token before admission and before any possible original call.
    invalidate_extended_chart_commit("chart_expansion_begin");
    registry().invalidate_chart_admission(wrapper);
    ChartExpandPreparationOutcome preparation
        = ChartExpandPreparationOutcome::NativePristine;
    ChartAudioDiagnosticTransaction chart_audio_transaction;
    ChartAudioExpandTlsSnapshot chart_audio_expand;
    SelectionAudioAdmissionAuthority activation_authority;
    try {
    auto callback = non_audio_hook_gate().try_enter();
    if (!callback) {
        if (g_original_chart_expand) g_original_chart_expand(wrapper, chart_row, arg3, arg4);
        return;
    }
    void* caller = _ReturnAddress();
    const uintptr_t caller_rva = to_rva(caller);
    preparation = prepare_active_chart_row_patch_before_expand(
        wrapper, chart_row, caller_rva, &chart_audio_transaction,
        &activation_authority);
    if (!chart_expand_original_allowed(preparation)) {
        static std::atomic_int s_unresolved_logs{0};
        if (s_unresolved_logs.fetch_add(1, std::memory_order_relaxed) < 32) {
            core::log(core::LogLevel::Error,
                "[chart_expand] status=blocked reason=mutation_unresolved original_calls=0 journal_retained=1");
        }
        finish_chart_audio_diagnostic_transaction(
            chart_audio_transaction,
            ChartAudioDiagnosticTerminalOutcome::MutationUnresolved);
        return;
    }
    if (g_original_chart_expand) {
        ChartAudioExpandTlsScope scope(
            chart_audio_transaction, wrapper, chart_audio_expand);
        begin_extended_chart_transaction(current_chart_audio_expand_tls(),
            activation_authority, wrapper, chart_row, caller_rva);
        g_original_chart_expand(wrapper, chart_row, arg3, arg4);
        scope.finish();
    }
    log_chart_audio_expand_snapshot(
        "expand_enter", chart_audio_expand, true);
    log_chart_audio_expand_snapshot(
        "expand_exit", chart_audio_expand, false);
    finish_active_chart_row_patch_after_expand(wrapper, caller_rva);
    const bool extended_committed =
        finish_extended_chart_transaction(wrapper, chart_row, caller_rva);
    const auto& selected_profile = activation_authority.selection.profile;
    finish_chord_voicing_expansion(wrapper,
        preparation == ChartExpandPreparationOutcome::CustomCommitted
        && selected_profile
        && (selected_profile->extended_chart_tail_notes.empty() || extended_committed));
    chart_audio_diagnostic_expand_completed(chart_audio_transaction);
    // Keep the exact committed transaction active after expand return. A
    // PlaySetup that is not nested in this call can then correlate solely by
    // selection/route/lease/song facts. Audio publication/failure, an exact
    // replacement, list exit, or an expand exception terminalizes it.

    const SelectionSnapshot& snapshot = activation_authority.selection;
    const SongDescriptor* song = snapshot.song;
    const SongDifficultyProfile* profile = snapshot.profile;
    const bool authority_exact = selection_audio_admission_authority_matches(
        activation_authority);
    if (!authority_exact || !song || !profile
        || caller_rva != rva::PersistentChartExpandCaller) {
        return;
    }

    int32_t note_count = 0;
    if (!core::safe_read_field(wrapper, runtime_layouts::PianoScoreWrapper::copied_row_count, note_count)) {
        return;
    }
    const bool playable_extended_count = extended_committed && profile
        && note_count == static_cast<int32_t>(profile->native_event_count) && authority_exact;
    if (!valid_note_count(note_count) && !playable_extended_count) {
        return;
    }
    float max_event_seconds = 0.0f;
    const bool max_event_valid = core::safe_read_field(wrapper, 0x30, max_event_seconds)
        && std::isfinite(max_event_seconds) && max_event_seconds >= 0.0f;

    const std::uint64_t capture_id = g_capture_id.fetch_add(1, std::memory_order_acq_rel) + 1;
    const bool diagnostic_profile = profile->diagnostic_source_rows != 0 ||
        profile->diagnostic_tail_rows != 0;
    const bool exact_520_diagnostic =
        profile->diagnostic_source_rows == 520u &&
        profile->diagnostic_native_prefix_rows == ff7rp::pipeline::kMaxChartRows &&
        profile->diagnostic_tail_rows == 8u && profile->diagnostic_descriptor_hash != 0 &&
        profile->chart_notes.size() == ff7rp::pipeline::kMaxChartRows && note_count == 512;
    const RetainedChartOwnerObservation owner = exact_520_diagnostic
        ? retained_chart_owner_observation() : RetainedChartOwnerObservation{};
    const bool registry_identity_stable = snapshot.storage
        && activation_authority.token.valid()
        && snapshot.song == song && snapshot.profile == profile;
    UObjectLiveHandle wrapper_identity{};
    const bool wrapper_identity_valid = exact_520_diagnostic
        && capture_live_uobject_handle(wrapper, wrapper_identity);
    const bool capture_identity_valid = registry_identity_stable
        && owner.owner_observed && owner.chart_read_succeeded && owner.chart == wrapper
        && owner.registry_generation == snapshot.generation;
    const bool exact_extended_playable = playable_extended_count
        && profile->source_row_count > ff7rp::pipeline::kMaxChartRows
        && profile->source_row_count <= ff7rp::pipeline::kMaximumExtendedChartRows
        && profile->native_prefix_event_count >= ff7rp::pipeline::kMaxChartRows
        && profile->native_prefix_event_count <= ff7rp::pipeline::kMaxChartRows * 2u
        && profile->native_event_count > profile->native_prefix_event_count
        && profile->native_event_count <= ff7rp::pipeline::kMaximumNativeChartEvents
        && profile->required_action_count <= profile->native_event_count
        && profile->note_count == static_cast<int32_t>(profile->required_action_count)
        && profile->physical_chart_digest != 0;
    const bool synchronous_extended_identity = exact_extended_playable && authority_exact;
    const bool identity_valid = !diagnostic_profile ||
        (registry_identity_stable && ff7rp::pipeline::extended_chart_input_enabled() &&
            profile->diagnostic_policy_generation == ff7rp::pipeline::chart_row_policy_generation() &&
            ((exact_520_diagnostic && capture_identity_valid) || synchronous_extended_identity));
    if (diagnostic_profile || ff7rp::pipeline::extended_chart_input_enabled()) {
        const uintptr_t owner_address = reinterpret_cast<uintptr_t>(owner.owner);
        const uintptr_t owner_chart_field = owner_address != 0 &&
            owner_address <= std::numeric_limits<uintptr_t>::max() - kPersistentChartOwnerOffset
            ? owner_address + kPersistentChartOwnerOffset : 0;
        std::ostringstream identity;
        identity << "[extended_chart_identity] capture_id=" << capture_id
            << " status=" << (!identity_valid ? "rejected"
                : diagnostic_profile ? "proven" : "not_applicable")
            << " capability_input=" << ff7rp::pipeline::extended_chart_input_enabled()
            << " playable_authority=" << ff7rp::pipeline::playable_extended_transport_available()
            << " accepted_input_limit=" << ff7rp::pipeline::chart_input_row_limit()
            << " policy_generation=" << ff7rp::pipeline::chart_row_policy_generation()
            << " descriptor_policy_generation=" << profile->diagnostic_policy_generation
            << " registry_generation=" << snapshot.generation
            << " registry_identity_stable=" << registry_identity_stable
            << " authority_source=" << (!diagnostic_profile ? "not_applicable"
                : synchronous_extended_identity
                    ? "selection_admission+synchronous_native" : "retained_completion_owner")
            << " owner_generation=" << owner.generation
            << " owner_registry_generation=" << owner.registry_generation
            << " cache_source=" << (profile->diagnostic_loaded_from_runtime_cache ? "runtime" : "generated")
            << " song_id=" << song->id << " profile_index=" << snapshot.profile_index
            << " difficulty=" << profile->difficulty
            << " descriptor_hash=0x" << std::hex << profile->diagnostic_descriptor_hash
            << " owner=0x" << owner_address
            << " owner_chart_field=0x" << owner_chart_field
            << " owner_chart=0x" << reinterpret_cast<uintptr_t>(owner.chart)
            << " wrapper=0x" << reinterpret_cast<uintptr_t>(wrapper)
            << " caller_rva=0x" << caller_rva << std::dec
            << " owner_observed=" << owner.owner_observed
            << " owner_chart_read=" << owner.chart_read_succeeded
            << " wrapper_equal=" << (owner.chart == wrapper)
            << " wrapper_uobject_identity=" << wrapper_identity_valid
            << " source_rows=" << profile->diagnostic_source_rows
            << " native_prefix_rows=" << profile->diagnostic_native_prefix_rows
            << " tail_rows=" << profile->diagnostic_tail_rows
            << " playable_rows=" << profile->chart_notes.size()
            << " source_rows=" << profile->source_row_count
            << " parser_events=" << profile->native_prefix_event_count
            << " native_event_count=" << note_count
            << " required_actions=" << profile->required_action_count;
        core::log(!identity_valid ? core::LogLevel::Error
                : diagnostic_profile ? core::LogLevel::Info : core::LogLevel::Debug,
            identity.str());
    }
    if (!identity_valid || !log_chart_event_plan(wrapper, note_count, profile, capture_id)) {
        return;
    }
    if (synchronous_extended_identity) {
        // Extended ownership proof is complete inside the synchronous TLS
        // transaction. Do not promote its native chart/controller pointers into
        // the asynchronous completion-capture path; late completion-owner
        // observations remain diagnostics and cannot revoke the immutable token.
        core::log(core::LogLevel::Info,
            "[extended_chart_identity] status=proven authority_source=selection_admission+synchronous_native completion_owner_required=0 native_pointer_retained=0");
        return;
    }
    const float target_seconds = completion_target_seconds(song, profile, max_event_seconds);
    if (!max_event_valid || !std::isfinite(target_seconds) || target_seconds <= 0.0f
        || !capture_identity_valid) {
        return;
    }
    auto capture = std::make_shared<const CompletionCapture>(CompletionCapture{
        wrapper,
        wrapper_identity,
        owner.owner,
        owner.owner_identity,
        owner.generation,
        snapshot.generation,
        capture_id,
        song->id,
        snapshot.profile_index,
        profile->difficulty,
        profile->diagnostic_descriptor_hash,
        song->duration_seconds,
        profile_last_prompt_seconds(profile),
        max_event_seconds,
        target_seconds,
        wrapper_identity_valid,
        activation_authority.token,
    });
    {
        std::lock_guard<std::mutex> lock(g_completion_capture_mutex);
        g_completion_capture = std::move(capture);
        g_completion_held = false;
    }
    capture_active_note_count(note_count);
    log_active_chart_memory(-1.0f);
    static std::atomic_int s_logs{0};
    const int log_index = s_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 32) {
        std::ostringstream out;
        out << "[note_count] capture status=chart_expand"
            << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper)
            << std::dec
            << " note_count=" << note_count
            << " max_event_valid=" << max_event_valid
            << " max_event_seconds=" << max_event_seconds
            << " song_id=" << song->id;
        core::log(core::LogLevel::Debug, out.str());
    }
    } catch (...) {
        // Never unwind C++ exceptions through the native chart-expand ABI.
        log_chart_audio_expand_snapshot(
            "expand_enter_exception", chart_audio_expand, true);
        log_chart_audio_expand_snapshot(
            "expand_exit_exception", chart_audio_expand, false);
        finish_chart_audio_diagnostic_transaction(
            chart_audio_transaction,
            ChartAudioDiagnosticTerminalOutcome::ExpandException);
        if (preparation == ChartExpandPreparationOutcome::CustomCommitted) {
            finish_chord_voicing_expansion(wrapper, false);
            block_custom_audio_route_for_unresolved_chart_mutation();
        }
        abort_extended_chart_transaction();
    }
}

uintptr_t __fastcall note_count_detour(void* arg0, void* arg1, void* arg2, void* arg3)
{
    auto callback = non_audio_hook_gate().try_enter();
    const uintptr_t original = g_original_note_count ? g_original_note_count(arg0, arg1, arg2, arg3) : 0;
    if (!callback) return original;
    const RenderSnapshot menu = registry().render_snapshot();
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const SongDescriptor* song = menu.song ? menu.song : playback.song;
    const SongDifficultyProfile* profile = menu.profile ? menu.profile : playback.profile;
    const int replacement = resolve_menu_or_playback_note_count(menu, playback);
    const bool extended_profile = profile
        && profile->source_row_count > ff7rp::pipeline::kMaxChartRows
        && profile->native_event_count > profile->native_prefix_event_count;
    const bool restricted_extended = extended_profile && replacement == profile->note_count
        && (menu.song ? playable_extended_presentation(menu, playback)
                      : playable_extended_playback(playback));
    if (!song || (extended_profile && !restricted_extended)
        || (!valid_note_count(replacement) && !restricted_extended)) {
        return original;
    }

    static std::atomic_int s_logs{0};
    const int log_index = s_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 32) {
        std::ostringstream out;
        out << "[note_count] status=overridden"
            << " original=" << original
            << " replacement=" << replacement
            << " captured=" << active_captured_note_count()
            << " source=" << (menu.song ? "menu" : "playback")
            << " song_id=" << song->id;
        core::log(core::LogLevel::Debug, out.str());
    }
    return static_cast<uintptr_t>(replacement);
}

} // namespace

ChartAudioExpandTlsSnapshot current_chart_audio_expand_tls() noexcept
{
    return g_chart_audio_expand_tls;
}

void capture_active_note_count(int note_count)
{
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const bool restricted_extended = playback.profile
        && note_count == playback.profile->note_count
        && playable_extended_playback(playback);
    g_active_captured_note_count.store(valid_note_count(note_count) || restricted_extended ? note_count : 0,
        std::memory_order_relaxed);
}

int active_captured_note_count()
{
    return g_active_captured_note_count.load(std::memory_order_relaxed);
}

void reset_active_note_count()
{
    g_active_captured_note_count.store(0, std::memory_order_relaxed);
    invalidate_extended_chart_commit("note_count_lifecycle_reset");
}

int resolve_note_count(const PlaybackSnapshot& playback)
{
    const bool extended = playback.profile
        && playback.profile->source_row_count > ff7rp::pipeline::kMaxChartRows
        && playback.profile->native_event_count > playback.profile->native_prefix_event_count;
    const bool active = extended && playable_extended_playback(playback);
    // Zero means "use native/original" to callers. Never reuse a captured
    // descriptor action count after generalized parser-group suppression.
    if (extended && !active) return 0;
    const int maximum = active ? playback.profile->note_count
        : static_cast<int>(ff7rp::pipeline::effective_chart_row_limit());
    return menu_or_playback_note_count_value({}, playback,
        active_captured_note_count(), maximum);
}

int resolve_menu_or_playback_note_count(
    const RenderSnapshot& menu, const PlaybackSnapshot& playback)
{
    const SongDifficultyProfile* selected = menu.song ? menu.profile : playback.profile;
    const bool extended = selected
        && selected->source_row_count > ff7rp::pipeline::kMaxChartRows
        && selected->native_event_count > selected->native_prefix_event_count;
    const bool active = extended
        && (menu.song ? playable_extended_presentation(menu, playback)
                      : playable_extended_playback(playback));
    if (extended && !active) return 0;
    const int maximum = active ? selected->note_count
        : static_cast<int>(ff7rp::pipeline::effective_chart_row_limit());
    return menu_or_playback_note_count_value(menu, playback,
        active_captured_note_count(), maximum);
}

void __fastcall chart_update_detour(void* wrapper, float delta_seconds, void* arg3, void* arg4)
{
    auto callback = non_audio_hook_gate().try_enter();
    if (!chord_voicing_chart_update_allowed(wrapper)) return;
    if (g_original_chart_update) {
        g_original_chart_update(wrapper, delta_seconds, arg3, arg4);
    }
    if (!callback) return;

    std::shared_ptr<const CompletionCapture> capture;
    {
        std::lock_guard<std::mutex> lock(g_completion_capture_mutex);
        capture = g_completion_capture;
    }
    if (!capture || wrapper != capture->wrapper) return;

    const PlaybackSnapshot snapshot = registry().playback_snapshot();
    const RetainedChartOwnerObservation owner = retained_chart_owner_observation();
    if (!snapshot.song || !snapshot.profile || !owner.owner_observed || !owner.chart_read_succeeded
        || owner.chart != wrapper) {
        return;
    }

    uint8_t active = 0;
    float elapsed_seconds = 0.0f;
    float native_chart_max_seconds = 0.0f;
    if (!core::safe_read_field(wrapper, 0xa0, active) || active != 0
        || !core::safe_read_field(wrapper, 0x34, elapsed_seconds)
        || !core::safe_read_field(wrapper, 0x30, native_chart_max_seconds)
        || !std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0f) {
        return;
    }

    if (native_chart_max_seconds != capture->native_chart_max_seconds) return;
    UObjectLiveHandle wrapper_identity{};
    const bool wrapper_identity_valid = capture_live_uobject_handle(wrapper, wrapper_identity);
    const float current_target_seconds = completion_target_seconds(
        snapshot.song, snapshot.profile, native_chart_max_seconds);
    if (!completion_capture_matches(*capture, {
            wrapper, wrapper_identity, owner.owner, owner.owner_identity, owner.generation,
            owner.registry_generation, snapshot.generation, snapshot.song->id,
            snapshot.profile_index, snapshot.profile->difficulty,
            snapshot.profile->diagnostic_descriptor_hash, current_target_seconds,
            wrapper_identity_valid, snapshot.token,
        })) {
        return;
    }
    const float target_seconds = capture->target_seconds;
    if (!std::isfinite(target_seconds) || target_seconds <= 0.0f) {
        return;
    }

    if (elapsed_seconds < target_seconds) {
        const uint8_t held_active = 1;
        if (!core::safe_write_field(wrapper, 0xa0, held_active)) {
            std::lock_guard<std::mutex> lock(g_completion_capture_mutex);
            if (g_completion_capture == capture && !g_completion_held) {
                g_completion_held = true;
                core::log(core::LogLevel::Error, "[completion_hold] status=write_failed");
            }
            return;
        }
        bool first_hold = false;
        {
            std::lock_guard<std::mutex> lock(g_completion_capture_mutex);
            if (g_completion_capture == capture && !g_completion_held) {
                g_completion_held = true;
                first_hold = true;
            }
        }
        if (first_hold) {
            std::ostringstream out;
            out << "[completion_hold] status=holding"
                << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper) << std::dec
                << " elapsed=" << elapsed_seconds
                << " target=" << target_seconds
                << " chart_max=" << native_chart_max_seconds
                << " song_id=" << capture->song_id
                << " capture_id=" << capture->capture_id;
            core::log(core::LogLevel::Info, out.str());
        }
        return;
    }

    bool released = false;
    {
        std::lock_guard<std::mutex> lock(g_completion_capture_mutex);
        if (g_completion_capture == capture && g_completion_held) {
            g_completion_held = false;
            released = true;
        }
    }
    if (released) {
        std::ostringstream out;
        out << "[completion_hold] status=released"
            << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper) << std::dec
            << " elapsed=" << elapsed_seconds
            << " target=" << target_seconds
            << " song_id=" << capture->song_id
            << " capture_id=" << capture->capture_id;
        core::log(core::LogLevel::Info, out.str());
    }
}

void log_active_chart_memory(float playback_seconds)
{
    constexpr std::size_t kChartObjectSize = 0x140;
    std::array<uint32_t, kChartObjectSize / sizeof(uint32_t)> words{};
    void* wrapper = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_completion_capture_mutex);
        if (g_completion_capture) wrapper = g_completion_capture->wrapper;
    }
    if (!wrapper || !core::safe_copy_bytes(wrapper, words.data(), kChartObjectSize)) {
        return;
    }

    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 64) {
        return;
    }
    std::ostringstream out;
    out << "[chart_mem] wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper)
        << std::dec << " playback_seconds=" << playback_seconds << " words=" << std::hex;
    for (const uint32_t word : words) {
        out << std::setw(8) << std::setfill('0') << word;
    }
    core::log(core::LogLevel::Info, out.str());
}

bool install_note_count_hooks(const HookInstallContext& context)
{
    g_module_base = reinterpret_cast<uintptr_t>(context.exe_module);
    std::string extended_error;
    const bool ok = install_named_hook(context, "note_count", g_note_count_hook,
                        reinterpret_cast<void*>(&note_count_detour), reinterpret_cast<void**>(&g_original_note_count))
        && install_named_hook(context, "piano_score_expand", g_chart_expand_hook,
            reinterpret_cast<void*>(&chart_expand_detour), reinterpret_cast<void**>(&g_original_chart_expand))
        && install_named_hook(context, "piano_chart_update", g_chart_update_hook,
            reinterpret_cast<void*>(&chart_update_detour), reinterpret_cast<void**>(&g_original_chart_update))
        && install_extended_chart_reserve_hook(context.exe_module, extended_error);

    std::ostringstream out;
    out << "[note_count] status=" << (ok ? "live_hooks_installed" : "install_failed")
        << " captured=" << active_captured_note_count()
        << " custom_songs=" << registry().custom_count();
    core::log(ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    return ok;
}

core::HookShutdownResult shutdown_note_count()
{
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {
        core::teardown_operation(g_chart_update_hook),
        core::teardown_operation(g_chart_expand_hook),
        extended_chart_reserve_teardown_operation(),
        core::teardown_operation(g_note_count_hook),
    }, restore_chart_patch_for_shutdown, [] {
        g_original_chart_update = nullptr;
        g_original_chart_expand = nullptr;
        g_original_note_count = nullptr;
        clear_extended_chart_runtime_state();
        g_module_base = 0;
        std::lock_guard<std::mutex> lock(g_completion_capture_mutex);
        g_completion_capture.reset();
        g_completion_held = false;
        reset_active_note_count();
    });
}

} // namespace ff7r::piano::game
