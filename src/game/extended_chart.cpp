#include "game/extended_chart.h"

#include "core/logging.h"
#include "core/hooks.h"
#include "core/pe_image.h"
#include "game/duration.h"
#include "game/audio_sead.h"
#include "game/extended_chart_runtime_specs.h"
#include "game/hook_specs.h"
#include "game/generated/rvas.generated.h"
#include "game/song_registry.h"
#include "pipeline/pipeline_limits.h"

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
constexpr std::size_t kEventCount = 512;
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
    const SongDescriptor* song = nullptr;
    const SongDifficultyProfile* profile = nullptr;
    std::uint64_t registry_generation = 0;
    std::uint64_t policy_generation = 0;
    std::uint64_t descriptor_hash = 0;
    std::uint64_t owner_generation = 0;
    void* owner = nullptr;
    ChartAudioExpandTlsSnapshot tls{};
};

struct Committed513 {
    bool active = false;
    const SongDescriptor* song = nullptr;
    const SongDifficultyProfile* profile = nullptr;
    std::uint64_t registry_generation = 0;
    std::uint64_t policy_generation = 0;
    std::uint64_t descriptor_hash = 0;
    std::uint64_t selection_generation = 0;
    std::uint64_t route_generation = 0;
    std::uint64_t lease_generation = 0;
    std::uint64_t song_key = 0;
    std::uint64_t owner_generation = 0;
    void* owner = nullptr;
    void* wrapper = nullptr;
    void* chart_row = nullptr;
    EventHeader* header = nullptr;
    void* allocation = nullptr;
    int32_t capacity = 0;
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
Committed513 g_committed;

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
        && note.camera_switch_timing == 0
        && std::all_of(note.ignore_sound_ids.begin(), note.ignore_sound_ids.end(),
            [](const std::string& id) { return id.empty(); });
}

bool eligible_profile(const SongDifficultyProfile& profile) {
    return profile.diagnostic_source_rows == ff7rp::pipeline::kPlayable513ChartRows
        && profile.diagnostic_native_prefix_rows == ff7rp::pipeline::kMaxChartRows
        && profile.diagnostic_tail_rows == 1u && profile.diagnostic_descriptor_hash != 0
        && profile.diagnostic_policy_generation == ff7rp::pipeline::chart_row_policy_generation()
        && profile.note_count == static_cast<int>(ff7rp::pipeline::kPlayable513ChartRows)
        && profile.chart_notes.size() == ff7rp::pipeline::kMaxChartRows
        && profile.diagnostic_tail_note && restricted_note(*profile.diagnostic_tail_note)
        && std::all_of(profile.chart_notes.begin(), profile.chart_notes.end(), restricted_note);
}

bool exact_identity(const Transaction& tx, void* wrapper, void* chart_row, uintptr_t caller_rva) {
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const RetainedChartOwnerObservation owner = retained_chart_owner_observation();
    return tx.active && tx.wrapper == wrapper && tx.chart_row == chart_row
        && tx.caller_rva == caller_rva && caller_rva == rva::PersistentChartExpandCaller
        && playback.song == tx.song && playback.profile == tx.profile
        && playback.generation == tx.registry_generation && tx.policy_generation == ff7rp::pipeline::chart_row_policy_generation()
        && playback.profile && playback.profile->diagnostic_descriptor_hash == tx.descriptor_hash
        && owner.owner_observed && owner.chart_read_succeeded && owner.owner == tx.owner
        && owner.chart == wrapper && owner.generation == tx.owner_generation
        && owner.registry_generation == tx.registry_generation;
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
    if (capacity < 513 || capacity > kMaximumEventCapacity
        || static_cast<std::size_t>(capacity) > std::numeric_limits<std::size_t>::max() / kEventSize) {
        return false;
    }
    bytes = static_cast<std::size_t>(capacity) * kEventSize;
    return true;
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
    return tx.expected_header && tx.reserved_data && tx.reserved_capacity >= 513
        && core::safe_copy_bytes(tx.expected_header, &header, sizeof(header))
        && header.data == tx.reserved_data && header.count == count
        && header.capacity == tx.reserved_capacity;
}

bool exact_unowned_tail(const Transaction& tx, void* wrapper, void* chart_row,
    const uintptr_t caller_rva, void* tail)
{
    EventHeader header{};
    return exact_identity(tx, wrapper, chart_row, caller_rva)
        && exact_header_state(tx, 512, header)
        && static_cast<uint8_t*>(header.data) + kEventCount * kEventSize == tail
        && memory_writable(tail, kEventSize);
}

void publish_committed_513(const Transaction& tx)
{
    std::lock_guard<std::mutex> lock(g_committed_mutex);
    g_committed = {true, tx.song, tx.profile, tx.registry_generation,
        tx.policy_generation, tx.descriptor_hash, tx.tls.selection_generation,
        tx.tls.route_generation, tx.tls.lease_generation, tx.tls.song_key,
        tx.owner_generation, tx.owner, tx.wrapper, tx.chart_row,
        tx.expected_header, tx.reserved_data, tx.reserved_capacity};
}

bool committed_matches_locked(const SelectionSnapshot& selection)
{
    return g_committed.active && g_admissions.load(std::memory_order_acquire)
        && selection.song == g_committed.song && selection.profile == g_committed.profile
        && selection.generation == g_committed.registry_generation
        && selection.generation == g_committed.selection_generation
        && selection.profile && eligible_profile(*selection.profile)
        && g_committed.policy_generation == ff7rp::pipeline::chart_row_policy_generation()
        && selection.profile->diagnostic_descriptor_hash == g_committed.descriptor_hash;
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
        g_original_reserve(header, 513);
        EventHeader reserved{};
        if (core::safe_copy_bytes(header, &reserved, sizeof(reserved))
            && reserved.data && reserved.count == 0 && reserved.capacity >= 513
            && reserved.capacity <= kMaximumEventCapacity) {
            tx.reserved_data = reserved.data;
            tx.reserved_capacity = reserved.capacity;
            core::log(core::LogLevel::Info, "[extended_chart] reserve_hit=1 substitution=513 reserve_validated=1");
        } else {
            tx.reserve_mismatch = true;
            core::log(core::LogLevel::Error,
                "[extended_chart] reserve_hit=1 substitution=513 reserve_validated=0 failure=reserve_result");
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

bool playable_513_profile(const SongDifficultyProfile& profile) noexcept {
    const PlaybackSnapshot playback = registry().playback_snapshot();
    return playback.profile == &profile && playable_513_playback(playback);
}

bool playable_513_selection(const SelectionSnapshot& selection) noexcept {
    std::lock_guard<std::mutex> lock(g_committed_mutex);
    const bool matches = committed_matches_locked(selection);
    if (g_committed.active && !matches) {
        core::log(core::LogLevel::Info,
            "[extended_chart] committed_513=invalidated reason=selection_or_generation_mismatch");
        g_committed = {};
    }
    return matches;
}

bool playable_513_playback(const PlaybackSnapshot& playback) noexcept {
    std::lock_guard<std::mutex> lock(g_committed_mutex);
    const bool matches = committed_matches_locked(playback)
        && playback.token.registry_generation == g_committed.registry_generation
        && playback.token.route_generation == g_committed.route_generation
        && playback.token.lease_generation == g_committed.lease_generation
        && playback.token.song_key == g_committed.song_key;
    if (g_committed.active && !matches) {
        core::log(core::LogLevel::Info,
            "[extended_chart] committed_513=invalidated reason=playback_context_mismatch");
        g_committed = {};
    }
    return matches;
}

void invalidate_extended_chart_commit(const char* reason) noexcept {
    std::lock_guard<std::mutex> lock(g_committed_mutex);
    if (g_committed.active) {
        std::ostringstream out;
        out << "[extended_chart] committed_513=invalidated reason="
            << (reason ? reason : "unspecified")
            << " owner_generation=" << g_committed.owner_generation
            << " registry_generation=" << g_committed.registry_generation
            << " policy_generation=" << g_committed.policy_generation
            << " selection_generation=" << g_committed.selection_generation
            << " route_generation=" << g_committed.route_generation
            << " lease_generation=" << g_committed.lease_generation;
        core::log(core::LogLevel::Info, out.str());
    }
    g_committed = {};
}

ExtendedChartSupport configure_extended_chart_experiment(HMODULE exe_module, bool requested)
{
    invalidate_extended_chart_commit("configuration_change");
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
    if (!g_module || exe_module != g_module) { error = "row-513 helper verification unavailable"; return true; }
    if (g_original_reserve) { error = "row-513 reserve hook already retained"; return true; }
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
    void* wrapper, void* chart_row, uintptr_t caller_rva) noexcept {
    if (g_transaction.active) {
        g_transaction.reserve_mismatch = true;
        core::log(core::LogLevel::Error,
            "[extended_chart] mutation_available=0 failure=nested_outer_transaction");
        return;
    }
    abort_extended_chart_transaction();
    if (!g_admissions.load(std::memory_order_acquire) || !transaction.active
        || transaction.depth != 1 || !transaction.original_inflight
        || caller_rva != rva::PersistentChartExpandCaller
        || !wrapper || !chart_row) return;
    const PlaybackSnapshot playback = registry().playback_snapshot();
    const RetainedChartOwnerObservation owner = retained_chart_owner_observation();
    if (!playback.song || !playback.profile || !eligible_profile(*playback.profile)
        || !owner.owner_observed || !owner.chart_read_succeeded || owner.chart != wrapper
        || owner.registry_generation != playback.generation
        || transaction.selection_generation != playback.generation
        || transaction.route_generation != playback.token.route_generation
        || transaction.lease_generation != playback.token.lease_generation
        || transaction.song_key != playback.token.song_key) return;
    bool expected = false;
    if (!g_global_claim.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    void* side = nullptr;
    if (!core::safe_read_field(chart_row, kSideOffset, side) || !side) {
        g_global_claim.store(false, std::memory_order_release); return;
    }
    g_transaction = {true, true, false, false, wrapper, chart_row, side, caller_rva,
        reinterpret_cast<EventHeader*>(static_cast<uint8_t*>(wrapper) + kHeaderOffset),
        nullptr, 0, playback.song, playback.profile, playback.generation,
        ff7rp::pipeline::chart_row_policy_generation(), playback.profile->diagnostic_descriptor_hash,
        owner.generation, owner.owner, transaction};
    std::ostringstream out;
    out << "[extended_chart] transaction=admitted owner_generation=" << owner.generation
        << " registry_generation=" << playback.generation
        << " policy_generation=" << g_transaction.policy_generation
        << " descriptor_hash=" << g_transaction.descriptor_hash;
    core::log(core::LogLevel::Info, out.str());
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
            << " owner_generation=" << tx.owner_generation
            << " registry_generation=" << tx.registry_generation
            << " policy_generation=" << tx.policy_generation;
        core::log(core::LogLevel::Error, out.str());
        return finish();
    };
    const auto reject = [&](const char* reason) {
        std::ostringstream out;
        out << "[extended_chart] mutation_available=1 reserve_hit=" << tx.reserve_hit
            << " substitution=" << (tx.reserve_hit && !tx.reserve_mismatch)
            << " count_commit=none rollback=none failure=" << reason
            << " owner_generation=" << tx.owner_generation
            << " registry_generation=" << tx.registry_generation
            << " policy_generation=" << tx.policy_generation;
        core::log(core::LogLevel::Error, out.str());
        return finish();
    };
    if (!tx.reserve_hit || tx.reserve_mismatch || !exact_identity(tx, wrapper, chart_row, caller_rva))
        return reject("reserve_or_identity");
    EventHeader header{};
    std::size_t event_bytes = 0;
    if (!core::safe_copy_bytes(tx.expected_header, &header, sizeof(header)) || header.data != tx.reserved_data
        || header.count != 512 || header.capacity < 513 || header.capacity != tx.reserved_capacity
        || !event_extent_bytes(header.capacity, event_bytes) || !memory_writable(header.data, event_bytes))
        return reject("prefix_header");
    float fps = 0, old_max = 0;
    if (!read_at(wrapper, kFrameRateOffset, fps) || !std::isfinite(fps) || fps <= 0
        || !read_at(wrapper, kMaxTimeOffset, old_max) || !std::isfinite(old_max))
        return reject("chart_time_state");
    float prefix_max = 0, previous = 0;
    // The evidence proves sentinel 8, not a general numeric range. Use only assignment
    // values already produced by this exact parser-built monotone prefix.
    std::array<bool, 256> prefix_assignments{};
    uint8_t prefix_lookup_path = 2;
    for (std::size_t i = 0; i < kEventCount; ++i) {
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
    const SongChartNote& note = *tx.profile->diagnostic_tail_note;
    float tail_time = 0; uint64_t tail_name = 0;
    if (!parse_time(note.time_str, fps, tail_time) || tail_time < previous || !resolve_name(note.monotone_id, tail_name))
        return reject("tail_descriptor");
    auto* tail = static_cast<uint8_t*>(header.data) + kEventCount * kEventSize;
    const auto cleanup_unowned_tail = [&]() {
        if (!exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail)) return false;
        g_api.destruct(tail);
        if (!exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail)) return false;
        std::memset(tail, 0, kEventSize);
        return true;
    };
    if (!exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail))
        return unresolved("tail_slot_identity_before_zero");
    std::memset(tail, 0, kEventSize);
    if (!exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail))
        return unresolved("tail_slot_identity_before_constructor");
    if (g_api.construct(tail, wrapper, tx.side, 1024, tail_time, 0.0f,
            static_cast<uint8_t>(note.note_type), static_cast<uint8_t>(note.dot_type), tail_name) != tail
        || !event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, false)) {
        if (!cleanup_unowned_tail()) return unresolved("tail_constructor_cleanup_identity");
        return reject("tail_constructor_validation");
    }
    if (!exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail)
        || !event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, false))
        return unresolved("tail_identity_before_callback");
    void* captured = wrapper;
    g_api.callback_build(tail + 0x50, &captured);
    uint8_t tail_assignment = 8, tail_lookup_path = 2;
    if (!event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, true)
        || !event_assignment(tail, tail_assignment) || !prefix_assignments[tail_assignment]
        || !event_lookup_path(tail, tail_lookup_path) || tail_lookup_path != prefix_lookup_path
        || !exact_identity(tx, wrapper, chart_row, caller_rva)) {
        if (!cleanup_unowned_tail()) return unresolved("tail_callback_cleanup_identity");
        return reject("tail_callback_validation");
    }
    const float new_max = std::max(old_max, tail_time);
    float current_max = 0;
    if (!exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail)
        || !event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, true)
        || !read_at(wrapper, kMaxTimeOffset, current_max) || current_max != old_max)
        return unresolved("max_time_prewrite_identity");
    write_at(wrapper, kMaxTimeOffset, new_max);
    float committed_max = 0;
    if (!read_at(wrapper, kMaxTimeOffset, committed_max) || committed_max != new_max
        || !exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail)
        || !event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, true))
        return unresolved("max_time_postwrite_identity");
    if (!exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail)
        || !event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, true)
        || !read_at(wrapper, kMaxTimeOffset, committed_max) || committed_max != new_max)
        return unresolved("count_commit_identity");
    tx.expected_header->count = 513; // Ownership publication is deliberately last.
    EventHeader committed{};
    if (core::safe_copy_bytes(tx.expected_header, &committed, sizeof(committed))
        && committed.data == header.data && committed.count == 513 && committed.capacity == header.capacity
        && read_at(wrapper, kMaxTimeOffset, committed_max) && committed_max == new_max
        && event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, true)
        && exact_identity(tx, wrapper, chart_row, caller_rva)) {
        publish_committed_513(tx);
        success = true;
        core::log(core::LogLevel::Info, "[extended_chart] prefix_validated=1 tail_constructed=1 callback_validated=1 count_commit=513 rollback=none");
        return finish();
    }
    EventHeader live{};
    float live_max = 0;
    if (core::safe_copy_bytes(tx.expected_header, &live, sizeof(live)) && live.data == header.data
        && live.count == 513 && live.capacity == header.capacity
        && read_at(wrapper, kMaxTimeOffset, live_max) && live_max == new_max
        && event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, true)
        && exact_identity(tx, wrapper, chart_row, caller_rva)) {
        tx.expected_header->count = 512;
        EventHeader restored{};
        const bool count_restored = core::safe_copy_bytes(tx.expected_header, &restored, sizeof(restored))
            && restored.data == header.data && restored.count == 512 && restored.capacity == header.capacity
            && exact_identity(tx, wrapper, chart_row, caller_rva);
        if (count_restored) {
            float rollback_max = 0;
            if (!exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail)
                || !event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, true)
                || !read_at(wrapper, kMaxTimeOffset, rollback_max) || rollback_max != new_max)
                return unresolved("post_count_max_restore_identity");
            write_at(wrapper, kMaxTimeOffset, old_max);
            float restored_max = 0;
            const bool max_restored = read_at(wrapper, kMaxTimeOffset, restored_max) && restored_max == old_max;
            if (!max_restored || !exact_unowned_tail(tx, wrapper, chart_row, caller_rva, tail)
                || !event_valid(tail, wrapper, tx.side, 1024, tail_time, note, tail_name, true))
                return unresolved("post_count_max_restore_verification");
            if (cleanup_unowned_tail()) {
                core::log(core::LogLevel::Error,
                    "[extended_chart] count_commit=513 rollback=completed count_restore=proved max_restore=proved failure=post_count_validation");
            } else {
                return unresolved("post_count_cleanup_identity");
            }
        } else {
            block_custom_audio_route_for_unresolved_chart_mutation();
            core::log(core::LogLevel::Error,
                "[extended_chart] count_commit=513 rollback=preserved count_restore=unresolved failure=post_count_validation");
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
    ff7rp::pipeline::configure_chart_row_limit(
        ff7rp::pipeline::experimental_extended_charts_requested(), false, false);
}

} // namespace ff7r::piano::game
