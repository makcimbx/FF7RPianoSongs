#include "game/module_hooks.h"

#include "game/audio_sead.h"
#include "core/logging.h"
#include "core/pe_image.h"
#include "game/completion_timing.h"
#include "game/extended_chart.h"
#include "pipeline/chart_event_plan.h"
#include "game/note_count.h"
#include "game/rvas.h"
#include "game/runtime_layouts.h"
#include "game/runtime_context_policy.h"
#include "game/song_registry.h"
#include "game/uobject_identity.h"
#include "game/ue_types.h"
#include "pipeline/pipeline_limits.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ff7r::piano::game {
namespace {

constexpr bool kInstallLiveChartPatchHook = false;
constexpr uintptr_t kPianoScoreRowHeaderBase = 0x480;
constexpr uintptr_t kObservedPianoScoreChartField = 0x38;
constexpr std::array<int32_t, 11> kObservedPianoScoreRowCounts{143, 0, 143, 143, 132, 0, 141, 136, 0, 143, 57};
constexpr size_t kTimeStrHeaderIndex = 0;
constexpr size_t kIgnoreSoundHeaderIndex = 1;
constexpr size_t kMonotoneIdHeaderIndex = 2;
constexpr size_t kChordIdHeaderIndex = 3;
constexpr size_t kChordNoteTypeHeaderIndex = 4;
constexpr size_t kChordDotTypeHeaderIndex = 5;
constexpr size_t kMonotoneNoteTypeHeaderIndex = 6;
constexpr size_t kMonotoneDotTypeHeaderIndex = 7;
constexpr size_t kStrengthHeaderIndex = 8;
constexpr size_t kCameraSwitchTimingHeaderIndex = 9;
constexpr size_t kGroupIndexHeaderIndex = 10;
constexpr std::array<const wchar_t*, 3> kObservedPianoScorePrefixNotes{L"An4", L"Gn4", L"Fs4"};
constexpr int32_t kMaxPatchedChartRows = static_cast<int32_t>(ff7rp::pipeline::kMaxChartRows);
constexpr size_t kNativeStableLinkedEventCapacity = 512u;
constexpr size_t kMaxChartScanSize = 2u * 1024u * 1024u;
constexpr uintptr_t kChartRowNoteTypeOffset = 0x00;
constexpr uintptr_t kChartRowDotTypeOffset = 0x04;
constexpr uintptr_t kChartRowCameraSwitchTimingOffset = 0x08;
constexpr uintptr_t kChartRowGroupIndexOffset = 0x0c;
constexpr uintptr_t kChartRowTimeStrOffset = 0x10;
constexpr uintptr_t kChartRowMonotoneIdOffset = 0x20;

using FNameCtorFn = void*(__fastcall*)(FNameValue* out_name, const wchar_t* text, int32_t find_type);

struct DescriptorChartRow {
    std::string time_str;
    std::string monotone_id;
    std::string chord_id;
    int32_t note_type = 3;
    int32_t dot_type = 0;
    int32_t camera_switch_timing = 0;
    int32_t group_index = 0;
    std::array<std::string, 3> ignore_sound_ids{};
};

struct ChartMemoryLayout {
    uintptr_t chart_base = 0;
    size_t chart_size = 0;
    uintptr_t time_array_base = 0;
    uintptr_t monotone_id_array_base = 0;
    size_t time_header_index = kObservedPianoScoreRowCounts.size();
    size_t monotone_id_header_index = kObservedPianoScoreRowCounts.size();
    uintptr_t row_header_base = kPianoScoreRowHeaderBase;
    uintptr_t score_field_offset = 0;
};

struct FrozenStringEntry {
    uint64_t data = 0;
    int32_t num = 0;
    int32_t max = 0;
};

static_assert(sizeof(FrozenStringEntry) == 16);
static_assert(sizeof(wchar_t) == 2);

struct ChartNameResolver {
    bool (*resolve_monotone_id)(const std::string& monotone_id, uint64_t& packed_name) = nullptr;
    bool (*resolve_chord_id)(const std::string& chord_id, uint64_t& packed_name) = nullptr;
    bool (*resolve_ignore_sound_id)(const std::string& ignore_sound_id, uint64_t& packed_name) = nullptr;
};

struct JournalEntry {
    uintptr_t address = 0;
    std::vector<uint8_t> original;
    std::vector<uint8_t> patched;
    std::string label;
    int32_t index = -1;
};

struct OwnedDescriptorChartArrays {
    std::vector<std::array<wchar_t, 8>> time_texts;
    std::vector<FrozenStringEntry> time_entries;
    std::vector<uint64_t> ignore_sound_ids;
    std::vector<uint64_t> monotone_ids;
    std::vector<uint64_t> chord_ids;
    std::vector<uint8_t> chord_note_types;
    std::vector<uint8_t> chord_dot_types;
    std::vector<uint8_t> monotone_note_types;
    std::vector<uint8_t> monotone_dot_types;
    std::vector<uint8_t> camera_switch_timings;
    std::vector<uint8_t> group_indices;
};

struct PlannedDescriptorChartPatch {
    std::shared_ptr<OwnedDescriptorChartArrays> arrays;
    std::vector<JournalEntry> entries;
};

struct ChartPatchJournal {
    bool active = false;
    bool mutation_unresolved = false;
    bool audio_committed = false;
    bool chart_restored = false;
    bool audio_cancellation_required = false;
    uintptr_t score_object = 0;
    uintptr_t chart_base = 0;
    size_t attempted_entries = 0;
    SelectionSnapshot selection{};
    AudioRouteLeaseIdentity admission_lease{};
    std::shared_ptr<OwnedDescriptorChartArrays> arrays;
    std::vector<JournalEntry> entries;

    void clear()
    {
        active = false;
        mutation_unresolved = false;
        audio_committed = false;
        chart_restored = false;
        audio_cancellation_required = false;
        score_object = 0;
        chart_base = 0;
        attempted_entries = 0;
        selection = {};
        admission_lease = {};
        arrays.reset();
        entries.clear();
    }
};

using GUObjectArrayView = runtime_layouts::GUObjectArrayView;

std::mutex g_chart_patch_mutex;
ChartPatchJournal g_chart_patch_journal;
ChartAudioDiagnosticTransaction g_chart_audio_diagnostic_transaction;
std::atomic_uint64_t g_chart_audio_diagnostic_generation{0};
std::atomic_uint64_t g_chart_audio_diagnostic_ordinal{0};
FNameCtorFn g_chart_fname_ctor = nullptr;
std::atomic_uintptr_t g_row_patch_wrapper{0};
std::atomic_int g_row_patch_next_index{0};
std::atomic_uintptr_t g_cached_pianoscore_object{0};
bool next_chart_audio_diagnostic_value(
    std::atomic_uint64_t& counter, uint64_t& out) noexcept
{
    uint64_t current = counter.load(std::memory_order_acquire);
    while (current != UINT64_MAX) {
        const uint64_t next = current + 1;
        if (next == 0) return false;
        if (counter.compare_exchange_weak(current, next,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            out = next;
            return true;
        }
    }
    return false;
}

const char* chart_audio_terminal_name(
    const ChartAudioDiagnosticTerminalOutcome outcome) noexcept
{
    switch (outcome) {
    case ChartAudioDiagnosticTerminalOutcome::None: return "none";
    case ChartAudioDiagnosticTerminalOutcome::NativePristine: return "native_pristine";
    case ChartAudioDiagnosticTerminalOutcome::MutationUnresolved: return "mutation_unresolved";
    case ChartAudioDiagnosticTerminalOutcome::CustomPrepared: return "custom_prepared";
    case ChartAudioDiagnosticTerminalOutcome::ExpandFinished: return "expand_finished";
    case ChartAudioDiagnosticTerminalOutcome::ExpandException: return "expand_exception";
    case ChartAudioDiagnosticTerminalOutcome::AudioPublished: return "audio_published";
    case ChartAudioDiagnosticTerminalOutcome::AudioFailed: return "audio_failed";
    case ChartAudioDiagnosticTerminalOutcome::StopFailed: return "stop_failed";
    case ChartAudioDiagnosticTerminalOutcome::Superseded: return "superseded";
    case ChartAudioDiagnosticTerminalOutcome::ListExit: return "list_exit";
    }
    return "unknown";
}

void log_chart_audio_diagnostic_transaction(
    const char* marker,
    const ChartAudioDiagnosticTransaction& diagnostic) noexcept
{
    try {
        if (diagnostic.generation == 0) return;
        std::ostringstream out;
        out << "[chart_patch] chart_audio_transaction marker=" << marker
            << " generation=" << diagnostic.generation
            << " preparation_ordinal=" << diagnostic.preparation_ordinal
            << " active=" << diagnostic.active
            << " preparation_outcome="
            << static_cast<unsigned>(diagnostic.preparation_outcome)
            << " journal_active=" << diagnostic.journal_active
            << " audio_committed=" << diagnostic.audio_committed
            << " terminal="
            << chart_audio_terminal_name(diagnostic.terminal_outcome)
            << " wrapper=0x" << std::hex << diagnostic.wrapper
            << " chart_row=0x" << diagnostic.chart_row << std::dec
            << " selection_generation="
            << diagnostic.prewrite.selection_generation
            << " route_generation=" << diagnostic.prewrite.route_generation
            << " lease_generation=" << diagnostic.prewrite.lease_generation
             << " song_key=" << diagnostic.prewrite.song_key
             << " prewrite_attempted=" << diagnostic.prewrite.attempted
             << " exact_admission=" << diagnostic.prewrite.exact_admission
             << " startup_mode="
            << static_cast<unsigned>(diagnostic.prewrite.startup_mode)
            << " chain_read=" << diagnostic.prewrite.chain_read
            << " sound_read=" << diagnostic.prewrite.sound_read
            << " request_read=" << diagnostic.prewrite.request_read
            << " state_read=" << diagnostic.prewrite.state_read
            << " controller=0x" << std::hex
            << diagnostic.prewrite.controller
            << " slot=0x" << diagnostic.prewrite.slot
            << " bgm=0x" << diagnostic.prewrite.bgm
            << " sound=0x" << diagnostic.prewrite.sound
            << " request=0x" << diagnostic.prewrite.request
            << std::dec << " state="
            << static_cast<unsigned>(diagnostic.prewrite.state)
             << " controller_proof_valid="
             << diagnostic.prewrite.controller_proof.valid
             << " controller_proof_mode="
             << static_cast<unsigned>(diagnostic.prewrite.controller_proof.mode)
             << " controller_raw_read="
             << diagnostic.prewrite.controller_proof.raw_index_readable
             << " controller_raw_index="
             << diagnostic.prewrite.controller_proof.raw_index
             << " controller_live_capture="
             << diagnostic.prewrite.controller_proof.live_capture_succeeded
             << " controller_live_index="
             << diagnostic.prewrite.controller_proof.live_index
             << " controller_live_serial="
             << diagnostic.prewrite.controller_proof.live_serial
             << " controller_item_capture="
             << diagnostic.prewrite.controller_proof.item_capture_succeeded
             << " controller_item_index="
             << diagnostic.prewrite.controller_proof.item_index
             << " controller_item_serial="
             << diagnostic.prewrite.controller_proof.item_serial;
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {
    }
}

bool construct_fname_find(FNameCtorFn ctor, const std::wstring& text, FNameValue& out)
{
    if (!ctor || text.empty()) {
        return false;
    }
    __try {
        out = {};
        ctor(&out, text.c_str(), 0); // UE4 EFindName::FNAME_Find; fail closed instead of adding names.
        return out.comparison_id != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
        return false;
    }
}

bool construct_fname_find(FNameCtorFn ctor, const wchar_t* text, FNameValue& out)
{
    return text ? construct_fname_find(ctor, std::wstring(text), out) : false;
}

uint64_t pack_fname(const FNameValue& value)
{
    return (static_cast<uint64_t>(value.number) << 32) | value.comparison_id;
}

bool resolve_monotone_id_fname(FNameCtorFn ctor, const std::string& monotone_id, uint64_t& packed_name)
{
    if (monotone_id.empty()) {
        return false;
    }
    FNameValue value{};
    if (!construct_fname_find(ctor, core::widen(monotone_id), value)) {
        return false;
    }
    packed_name = pack_fname(value);
    return packed_name != 0;
}

bool query_readable_region(uintptr_t address, size_t& available)
{
    available = 0;
    if (address < 0x10000) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) {
        return false;
    }
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0 || (mbi.Protect & readable) == 0) {
        return false;
    }
    const uintptr_t region_base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t region_end = region_base + mbi.RegionSize;
    if (address < region_base || address >= region_end) {
        return false;
    }
    available = static_cast<size_t>(region_end - address);
    return available > 0;
}

template <typename T, typename = void>
struct has_chart_rows_member : std::false_type {};

template <typename T>
struct has_chart_rows_member<T, std::void_t<decltype(std::declval<const T&>().chart_rows)>> : std::true_type {};

template <typename T, typename = void>
struct has_chart_notes_member : std::false_type {};

template <typename T>
struct has_chart_notes_member<T, std::void_t<decltype(std::declval<const T&>().chart_notes)>> : std::true_type {};

template <typename T, typename = void>
struct has_notes_member : std::false_type {};

template <typename T>
struct has_notes_member<T, std::void_t<decltype(std::declval<const T&>().notes)>> : std::true_type {};

template <typename T, typename = void>
struct has_nested_chart_notes_member : std::false_type {};

template <typename T>
struct has_nested_chart_notes_member<T, std::void_t<decltype(std::declval<const T&>().chart.notes)>> : std::true_type {};

template <typename T, typename = void>
struct has_time_str_member : std::false_type {};

template <typename T>
struct has_time_str_member<T, std::void_t<decltype(std::declval<const T&>().time_str)>> : std::true_type {};

template <typename T, typename = void>
struct has_monotone_id_member : std::false_type {};

template <typename T>
struct has_monotone_id_member<T, std::void_t<decltype(std::declval<const T&>().monotone_id)>> : std::true_type {};

template <typename T, typename = void>
struct has_chord_id_member : std::false_type {};

template <typename T>
struct has_chord_id_member<T, std::void_t<decltype(std::declval<const T&>().chord_id)>> : std::true_type {};

template <typename T, typename = void>
struct has_note_type_member : std::false_type {};

template <typename T>
struct has_note_type_member<T, std::void_t<decltype(std::declval<const T&>().note_type)>> : std::true_type {};

template <typename T, typename = void>
struct has_dot_type_member : std::false_type {};

template <typename T>
struct has_dot_type_member<T, std::void_t<decltype(std::declval<const T&>().dot_type)>> : std::true_type {};

template <typename T, typename = void>
struct has_camera_switch_timing_member : std::false_type {};

template <typename T>
struct has_camera_switch_timing_member<T, std::void_t<decltype(std::declval<const T&>().camera_switch_timing)>> : std::true_type {};

template <typename T, typename = void>
struct has_group_index_member : std::false_type {};

template <typename T>
struct has_group_index_member<T, std::void_t<decltype(std::declval<const T&>().group_index)>> : std::true_type {};

template <typename T, typename = void>
struct has_ignore_sound_ids_member : std::false_type {};

template <typename T>
struct has_ignore_sound_ids_member<T, std::void_t<decltype(std::declval<const T&>().ignore_sound_ids)>> : std::true_type {};

template <typename Note>
bool chart_row_from_note(const Note& note, DescriptorChartRow& out)
{
    if constexpr (has_time_str_member<Note>::value && has_monotone_id_member<Note>::value) {
        out.time_str = note.time_str;
        out.monotone_id = note.monotone_id;
        if constexpr (has_chord_id_member<Note>::value) {
            out.chord_id = note.chord_id;
        }
        if constexpr (has_note_type_member<Note>::value) {
            out.note_type = static_cast<int32_t>(note.note_type);
        }
        if constexpr (has_dot_type_member<Note>::value) {
            out.dot_type = static_cast<int32_t>(note.dot_type);
        }
        if constexpr (has_camera_switch_timing_member<Note>::value) {
            out.camera_switch_timing = static_cast<int32_t>(note.camera_switch_timing);
        }
        if constexpr (has_group_index_member<Note>::value) {
            out.group_index = static_cast<int32_t>(note.group_index);
        }
        if constexpr (has_ignore_sound_ids_member<Note>::value) {
            out.ignore_sound_ids = note.ignore_sound_ids;
        }
        return !out.time_str.empty() && (!out.monotone_id.empty() || !out.chord_id.empty());
    } else {
        (void)note;
        return false;
    }
}

template <typename Notes>
bool append_chart_rows_from_notes(const Notes& notes, std::vector<DescriptorChartRow>& out)
{
    for (const auto& note : notes) {
        DescriptorChartRow row{};
        if (!chart_row_from_note(note, row)) {
            return false;
        }
        out.push_back(std::move(row));
    }
    return !out.empty();
}

template <typename Descriptor>
bool descriptor_chart_fields_available()
{
    return has_chart_rows_member<Descriptor>::value
        || has_chart_notes_member<Descriptor>::value
        || has_notes_member<Descriptor>::value
        || has_nested_chart_notes_member<Descriptor>::value;
}

template <typename Descriptor>
bool build_descriptor_chart_rows(const Descriptor& descriptor, std::vector<DescriptorChartRow>& out)
{
    out.clear();
    if constexpr (has_chart_rows_member<Descriptor>::value) {
        return append_chart_rows_from_notes(descriptor.chart_rows, out);
    } else if constexpr (has_chart_notes_member<Descriptor>::value) {
        return append_chart_rows_from_notes(descriptor.chart_notes, out);
    } else if constexpr (has_nested_chart_notes_member<Descriptor>::value) {
        return append_chart_rows_from_notes(descriptor.chart.notes, out);
    } else if constexpr (has_notes_member<Descriptor>::value) {
        return append_chart_rows_from_notes(descriptor.notes, out);
    } else {
        (void)descriptor;
        return false;
    }
}

struct NativeEventPlanSummary {
    size_t event_count = 0;
    bool grouping_present = false;
};

NativeEventPlanSummary summarize_native_events(
    const std::vector<DescriptorChartRow>& rows) noexcept
{
    NativeEventPlanSummary summary{};
    for (const DescriptorChartRow& row : rows) {
        summary.event_count += static_cast<size_t>(!row.monotone_id.empty());
        summary.event_count += static_cast<size_t>(!row.chord_id.empty());
        summary.grouping_present = summary.grouping_present || row.group_index != 0;
    }
    return summary;
}

std::vector<uint8_t> bytes_from_value(const void* value, size_t size)
{
    std::vector<uint8_t> bytes(size);
    if (value && size > 0) {
        std::memcpy(bytes.data(), value, size);
    }
    return bytes;
}

template <typename T>
std::vector<uint8_t> bytes_from_value(const T& value)
{
    return bytes_from_value(&value, sizeof(T));
}

std::vector<uint8_t> utf16_ascii_bytes(const std::string& text)
{
    std::vector<uint8_t> bytes;
    bytes.reserve((text.size() + 1) * sizeof(uint16_t));
    for (const char ch : text) {
        bytes.push_back(static_cast<uint8_t>(ch));
        bytes.push_back(0);
    }
    bytes.push_back(0);
    bytes.push_back(0);
    return bytes;
}

enum class ChartWriteResult : uint8_t {
    Applied,
    NotWritten,
    WriteUncertain,
};

static_assert(std::is_nothrow_copy_assignable_v<SelectionSnapshot>);
static_assert(std::is_nothrow_move_assignable_v<std::vector<JournalEntry>>);

bool safe_chart_copy_noexcept(
    const void* source, void* destination, const size_t size) noexcept
{
    try {
        return core::safe_copy_bytes(source, destination, size);
    } catch (...) {
        return false;
    }
}

bool safe_chart_write_noexcept(
    void* destination, const void* source, const size_t size) noexcept
{
    try {
        return core::safe_write_bytes(destination, source, size);
    } catch (...) {
        return false;
    }
}

bool chart_entry_bytes_equal_noexcept(
    const JournalEntry& entry, const std::vector<uint8_t>& expected) noexcept
{
    if (entry.address < 0x10000 || expected.empty()) return false;
    constexpr size_t kChunkSize = 64;
    std::array<uint8_t, kChunkSize> current{};
    for (size_t offset = 0; offset < expected.size(); offset += kChunkSize) {
        const size_t count = std::min(kChunkSize, expected.size() - offset);
        if (!safe_chart_copy_noexcept(
                reinterpret_cast<const void*>(entry.address + offset),
                current.data(), count)
            || std::memcmp(current.data(), expected.data() + offset, count) != 0) {
            return false;
        }
    }
    return true;
}

ChartWriteResult write_checked_bytes(const JournalEntry& entry) noexcept
{
    if (entry.address < 0x10000 || entry.original.empty() || entry.original.size() != entry.patched.size()) {
        return ChartWriteResult::NotWritten;
    }

    if (!chart_entry_bytes_equal_noexcept(entry, entry.original)) {
        return ChartWriteResult::NotWritten;
    }
    if (!safe_chart_write_noexcept(reinterpret_cast<void*>(entry.address),
            entry.patched.data(), entry.patched.size())) {
        return ChartWriteResult::WriteUncertain;
    }
    return chart_entry_bytes_equal_noexcept(entry, entry.patched)
        ? ChartWriteResult::Applied : ChartWriteResult::WriteUncertain;
}

bool restore_entry(
    const JournalEntry& entry, const bool allow_partial_write = false) noexcept
{
    if (entry.address < 0x10000 || entry.original.empty()
        || entry.original.size() != entry.patched.size()) {
        return false;
    }
    const auto read_chunk = [&](const size_t offset, uint8_t* destination,
                                const size_t count) noexcept {
        return safe_chart_copy_noexcept(
            reinterpret_cast<const void*>(entry.address + offset),
            destination, count);
    };
    const auto write_all = [&](const uint8_t* source, const size_t count) noexcept {
        return safe_chart_write_noexcept(
            reinterpret_cast<void*>(entry.address), source, count);
    };
    return chart_chunked_restore_exact(
        entry.original.data(), entry.patched.data(), entry.original.size(),
        allow_partial_write, read_chunk, write_all);
}

bool append_byte_patch(
    uintptr_t address,
    std::vector<uint8_t> original,
    std::vector<uint8_t> patched,
    const char* label,
    int32_t index,
    std::vector<JournalEntry>& out)
{
    if (address < 0x10000 || original.empty() || original.size() != patched.size()) {
        return false;
    }
    JournalEntry entry{};
    entry.address = address;
    entry.original = std::move(original);
    entry.patched = std::move(patched);
    entry.label = label ? label : "?";
    entry.index = index;
    out.push_back(std::move(entry));
    return true;
}

template <typename T>
bool append_field_patch(uintptr_t address, const T& patched, const char* label, int32_t index, std::vector<JournalEntry>& out)
{
    T original{};
    if (address < 0x10000 || !core::safe_read_field(reinterpret_cast<void*>(address), 0, original)) {
        return false;
    }
    return append_byte_patch(address, bytes_from_value(original), bytes_from_value(patched), label, index, out);
}

bool decode_time_string(
    uintptr_t chart_base,
    size_t chart_size,
    uintptr_t entry_offset,
    std::string& text,
    uintptr_t& payload_offset,
    int32_t& char_count)
{
    uint64_t packed_payload = 0;
    int32_t num = 0;
    int32_t max = 0;
    if (!chart_base
        || entry_offset + 16 > chart_size
        || !core::safe_read_field(reinterpret_cast<void*>(chart_base + entry_offset), 0, packed_payload)
        || !core::safe_read_field(reinterpret_cast<void*>(chart_base + entry_offset + 8), 0, num)
        || !core::safe_read_field(reinterpret_cast<void*>(chart_base + entry_offset + 12), 0, max)
        || num < 0
        || max != num
        || max > 32) {
        return false;
    }

    if (num == 0) {
        text.clear();
        payload_offset = 0;
        char_count = 0;
        return true;
    }

    const uintptr_t target = entry_offset + static_cast<uintptr_t>(packed_payload >> 1);
    if (target + static_cast<uintptr_t>(num) * sizeof(uint16_t) > chart_size) {
        return false;
    }

    std::string value;
    for (int32_t index = 0; index < num; ++index) {
        uint16_t ch = 0;
        if (!core::safe_read_field(reinterpret_cast<void*>(chart_base + target + static_cast<uintptr_t>(index) * sizeof(uint16_t)), 0, ch)) {
            return false;
        }
        if (ch == 0) {
            break;
        }
        if (ch > 0x7f) {
            return false;
        }
        value.push_back(static_cast<char>(ch));
    }

    text = std::move(value);
    payload_offset = target;
    char_count = num;
    return true;
}

bool valid_chart_layout(const ChartMemoryLayout& layout, int32_t row_count)
{
    if (!layout.chart_base || layout.chart_size == 0 || row_count <= 0 || row_count > kMaxPatchedChartRows) {
        return false;
    }
    const uintptr_t row_count_u = static_cast<uintptr_t>(row_count);
    return layout.time_array_base + row_count_u * 16u <= layout.chart_size
        && layout.monotone_id_array_base + row_count_u * sizeof(uint64_t) <= layout.chart_size
        && layout.row_header_base + kObservedPianoScoreRowCounts.size() * 16u <= layout.chart_size;
}

bool read_block_header(uintptr_t chart_base, size_t chart_size, uintptr_t header_offset, int32_t& num, int32_t& max)
{
    if (!chart_base
        || header_offset + 16 > chart_size
        || !core::safe_read_field(reinterpret_cast<void*>(chart_base + header_offset + 8), 0, num)
        || !core::safe_read_field(reinterpret_cast<void*>(chart_base + header_offset + 12), 0, max)) {
        return false;
    }
    return true;
}

bool validate_pianoscore_row_headers(uintptr_t chart_base, size_t chart_size, int& matched)
{
    matched = 0;
    for (size_t index = 0; index < kObservedPianoScoreRowCounts.size(); ++index) {
        int32_t num = -1;
        int32_t max = -1;
        if (!read_block_header(chart_base, chart_size, kPianoScoreRowHeaderBase + index * 16u, num, max)) {
            return false;
        }
        if (num == kObservedPianoScoreRowCounts[index] && max == kObservedPianoScoreRowCounts[index]) {
            ++matched;
        }
    }
    return matched == static_cast<int>(kObservedPianoScoreRowCounts.size());
}

size_t find_array_header_index(const ChartMemoryLayout& layout, uintptr_t array_base)
{
    const uintptr_t expected = layout.chart_base + array_base;
    for (size_t index = 0; index < kObservedPianoScoreRowCounts.size(); ++index) {
        const uintptr_t header = layout.chart_base + layout.row_header_base + index * 16u;
        uint64_t packed_data = 0;
        if (!core::safe_read_field(reinterpret_cast<void*>(header), 0, packed_data)) {
            continue;
        }

        uintptr_t data = static_cast<uintptr_t>(packed_data);
        if ((packed_data & 1u) != 0) {
            data = static_cast<uintptr_t>(static_cast<intptr_t>(header) + (static_cast<int64_t>(packed_data) >> 1));
        }
        if (data == expected) {
            return index;
        }
    }
    return kObservedPianoScoreRowCounts.size();
}

bool is_time_like_text(const std::string& text)
{
    return text.size() == 5
        && text[0] >= '0' && text[0] <= '9'
        && text[1] >= '0' && text[1] <= '9'
        && text[2] == '_'
        && text[3] >= '0' && text[3] <= '9'
        && text[4] >= '0' && text[4] <= '9';
}

uintptr_t find_time_array_base(uintptr_t chart_base, size_t chart_size)
{
    const size_t scan_limit = std::min<size_t>(chart_size, 0x20000u);
    for (uintptr_t base = 0; base + 16u * kObservedPianoScoreRowCounts[0] <= scan_limit; base += sizeof(uintptr_t)) {
        int time_like = 0;
        std::string index_zero;
        for (int32_t index = 0; index < kObservedPianoScoreRowCounts[0]; ++index) {
            std::string text;
            uintptr_t payload = 0;
            int32_t chars = 0;
            if (!decode_time_string(chart_base, chart_size, base + static_cast<uintptr_t>(index) * 16u, text, payload, chars)) {
                continue;
            }
            if (index == 0) {
                index_zero = text;
            }
            if (is_time_like_text(text)) {
                ++time_like;
            }
        }
        if (index_zero == "06_56" && time_like >= 100) {
            return base;
        }
    }
    return 0;
}

float native_chart_last_prompt_seconds(const ChartMemoryLayout& layout)
{
    float last = -1.0f;
    for (int32_t index = 0; index < kObservedPianoScoreRowCounts[0]; ++index) {
        std::string text;
        uintptr_t payload = 0;
        int32_t chars = 0;
        float seconds = 0.0f;
        if (decode_time_string(layout.chart_base, layout.chart_size,
                layout.time_array_base + static_cast<uintptr_t>(index) * 16u, text, payload, chars)
            && chart_time_seconds(text, seconds)) {
            last = std::max(last, seconds);
        }
    }
    return last;
}

bool build_observed_prefix_note_names(FNameCtorFn fname_ctor, std::array<uint64_t, kObservedPianoScorePrefixNotes.size()>& out)
{
    for (size_t index = 0; index < kObservedPianoScorePrefixNotes.size(); ++index) {
        FNameValue value{};
        if (!construct_fname_find(fname_ctor, kObservedPianoScorePrefixNotes[index], value)) {
            return false;
        }
        out[index] = pack_fname(value);
    }
    return true;
}

uintptr_t find_monotone_id_array_base(uintptr_t chart_base, size_t chart_size, const std::array<uint64_t, kObservedPianoScorePrefixNotes.size()>& prefix)
{
    const size_t scan_limit = std::min<size_t>(chart_size, 0x20000u);
    for (uintptr_t base = 0; base + sizeof(uint64_t) * kObservedPianoScoreRowCounts[0] <= scan_limit; base += sizeof(uintptr_t)) {
        bool prefix_match = true;
        for (size_t index = 0; index < prefix.size(); ++index) {
            uint64_t packed = 0;
            if (!core::safe_read_field(reinterpret_cast<void*>(chart_base + base + index * sizeof(uint64_t)), 0, packed)
                || packed != prefix[index]) {
                prefix_match = false;
                break;
            }
        }
        if (prefix_match) {
            return base;
        }
    }
    return 0;
}

[[maybe_unused]] bool resolve_chart_layout_from_score_object(void* score_object, FNameCtorFn fname_ctor, ChartMemoryLayout& out, std::string& reason)
{
    out = {};
    reason.clear();
    if (!score_object) {
        reason = "missing_score_object";
        return false;
    }
    if (!fname_ctor) {
        reason = "missing_fname_ctor";
        return false;
    }

    std::array<uint64_t, kObservedPianoScorePrefixNotes.size()> prefix{};
    if (!build_observed_prefix_note_names(fname_ctor, prefix)) {
        reason = "missing_observed_prefix_fnames";
        return false;
    }

    for (uintptr_t field_offset = 0x28; field_offset <= 0xc0; field_offset += sizeof(uintptr_t)) {
        uintptr_t candidate = 0;
        size_t available = 0;
        if (!core::safe_read_field(score_object, field_offset, candidate)
            || candidate < 0x10000
            || !query_readable_region(candidate, available)) {
            continue;
        }

        ChartMemoryLayout layout{};
        layout.chart_base = candidate;
        layout.chart_size = std::min(available, kMaxChartScanSize);
        layout.row_header_base = kPianoScoreRowHeaderBase;
        layout.score_field_offset = field_offset;

        int matched_headers = 0;
        if (!validate_pianoscore_row_headers(layout.chart_base, layout.chart_size, matched_headers)) {
            continue;
        }
        layout.time_array_base = find_time_array_base(layout.chart_base, layout.chart_size);
        layout.monotone_id_array_base = find_monotone_id_array_base(layout.chart_base, layout.chart_size, prefix);
        if (!layout.time_array_base || !layout.monotone_id_array_base || !valid_chart_layout(layout, kObservedPianoScoreRowCounts[0])) {
            continue;
        }
        layout.time_header_index = find_array_header_index(layout, layout.time_array_base);
        layout.monotone_id_header_index = find_array_header_index(layout, layout.monotone_id_array_base);
        if (layout.time_header_index != kTimeStrHeaderIndex
            || layout.monotone_id_header_index != kMonotoneIdHeaderIndex) {
            continue;
        }

        out = layout;
        reason = field_offset == kObservedPianoScoreChartField ? "ok_primary_field" : "ok_fallback_field";
        return true;
    }

    reason = "missing_valid_chart_layout";
    return false;
}

bool plan_descriptor_chart_patch(
    const SongDifficultyProfile& profile,
    const std::string& song_id,
    const ChartMemoryLayout& layout,
    const ChartNameResolver& resolver,
    PlannedDescriptorChartPatch& out,
    std::string* fail_reason = nullptr)
{
    out = {};
    std::vector<DescriptorChartRow> rows;
    if (!build_descriptor_chart_rows(profile, rows)) {
        if (fail_reason) {
            *fail_reason = "descriptor_rows_unavailable";
        }
        return false;
    }
    bool generalized_extended = false;
    try {
        const auto policy = ff7rp::pipeline::chart_row_policy_snapshot();
        std::vector<ff7rp::pipeline::ChartEventRow> complete_rows;
        complete_rows.reserve(profile.chart_notes.size() + profile.extended_chart_tail_notes.size());
        for (const SongChartNote& row : profile.chart_notes)
            complete_rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(row));
        for (const SongChartNote& row : profile.extended_chart_tail_notes)
            complete_rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(row));
        const ff7rp::pipeline::ChartEventPlan plan =
            ff7rp::pipeline::derive_chart_event_plan(complete_rows);
        generalized_extended = policy.playable_extended_available
            && plan.valid()
            && plan.source_row_count > ff7rp::pipeline::kMaxChartRows
            && plan.native_event_count > plan.native_prefix_event_count
            && plan.source_row_count == profile.source_row_count
            && plan.native_prefix_event_count == profile.native_prefix_event_count
            && plan.native_event_count == profile.native_event_count
            && plan.required_action_count == profile.required_action_count
            && plan.physical_digest == profile.physical_chart_digest
            && profile.note_count == static_cast<int32_t>(plan.required_action_count)
            && profile.diagnostic_descriptor_hash != 0
            && profile.physical_chart_digest != 0
            && profile.diagnostic_policy_generation == policy.generation;
    } catch (...) {
        generalized_extended = false;
    }
    if (rows.size() > static_cast<size_t>(kMaxPatchedChartRows)
        && ff7rp::pipeline::experimental_extended_charts_requested()) {
        rows.resize(static_cast<size_t>(kMaxPatchedChartRows));
    }
    const NativeEventPlanSummary native_events = summarize_native_events(rows);
    if (!generalized_extended && native_events.grouping_present
        && native_events.event_count > kNativeStableLinkedEventCapacity) {
        if (fail_reason) {
            *fail_reason = "native_event_link_stability_guard row_count="
                + std::to_string(rows.size())
                + " event_count=" + std::to_string(native_events.event_count)
                + " native_stable_link_capacity="
                + std::to_string(kNativeStableLinkedEventCapacity)
                + " grouping_present=1";
        }
        return false;
    }
    if (!resolver.resolve_monotone_id
        || !resolver.resolve_chord_id
        || !resolver.resolve_ignore_sound_id
        || rows.empty()
        || rows.size() > static_cast<size_t>(kMaxPatchedChartRows)
        || layout.time_header_index >= kObservedPianoScoreRowCounts.size()
        || layout.monotone_id_header_index >= kObservedPianoScoreRowCounts.size()) {
        if (fail_reason) {
            *fail_reason = (!resolver.resolve_monotone_id || !resolver.resolve_chord_id
                    || !resolver.resolve_ignore_sound_id)
                ? "missing_resolver" : "invalid_chart_layout";
        }
        return false;
    }

    constexpr size_t kIgnoreSoundSlotsPerRow = 3u;
    if (rows.size() > std::numeric_limits<size_t>::max() / kIgnoreSoundSlotsPerRow) {
        if (fail_reason) *fail_reason = "ignore_sound_count_overflow";
        return false;
    }
    const size_t ignore_sound_count = rows.size() * kIgnoreSoundSlotsPerRow;
    if (ignore_sound_count > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        if (fail_reason) *fail_reason = "ignore_sound_count_out_of_range";
        return false;
    }

    auto arrays = std::make_shared<OwnedDescriptorChartArrays>();
    std::vector<JournalEntry> entries;
    entries.reserve(kObservedPianoScoreRowCounts.size() * 3u);
    arrays->time_texts.resize(rows.size());
    arrays->time_entries.resize(rows.size());
    arrays->ignore_sound_ids.resize(ignore_sound_count);
    arrays->monotone_ids.resize(rows.size());
    arrays->chord_ids.resize(rows.size());
    arrays->chord_note_types.resize(rows.size());
    arrays->chord_dot_types.resize(rows.size());
    arrays->monotone_note_types.resize(rows.size());
    arrays->monotone_dot_types.resize(rows.size());
    arrays->camera_switch_timings.resize(rows.size());
    arrays->group_indices.resize(rows.size());

    for (size_t index = 0; index < rows.size(); ++index) {
        const DescriptorChartRow& row = rows[index];
        if (row.time_str.empty() || row.time_str.size() + 1 > arrays->time_texts[index].size()) {
            if (fail_reason) {
                *fail_reason = "invalid_time_string index=" + std::to_string(index);
            }
            return false;
        }
        auto& text = arrays->time_texts[index];
        for (size_t char_index = 0; char_index < row.time_str.size(); ++char_index) {
            text[char_index] = static_cast<wchar_t>(static_cast<unsigned char>(row.time_str[char_index]));
        }
        text[row.time_str.size()] = L'\0';
        arrays->time_entries[index].data = reinterpret_cast<uint64_t>(text.data());
        arrays->time_entries[index].num = static_cast<int32_t>(row.time_str.size() + 1);
        arrays->time_entries[index].max = arrays->time_entries[index].num;

        if (!row.monotone_id.empty() && !resolver.resolve_monotone_id(row.monotone_id, arrays->monotone_ids[index])) {
            if (fail_reason) {
                *fail_reason = "note_resolve_failed index=" + std::to_string(index) + " monotone=" + row.monotone_id;
            }
            return false;
        }
        if (!row.chord_id.empty() && !resolver.resolve_chord_id(row.chord_id, arrays->chord_ids[index])) {
            if (fail_reason) {
                *fail_reason = "chord_resolve_failed index=" + std::to_string(index) + " chord=" + row.chord_id;
            }
            return false;
        }
        for (size_t slot = 0; slot < kIgnoreSoundSlotsPerRow; ++slot) {
            const std::string& ignore_sound_id = row.ignore_sound_ids[slot];
            uint64_t& packed_name = arrays->ignore_sound_ids[index * kIgnoreSoundSlotsPerRow + slot];
            if (!ignore_sound_id.empty()
                && !resolver.resolve_ignore_sound_id(ignore_sound_id, packed_name)) {
                if (fail_reason) {
                    *fail_reason = "ignore_sound_resolve_failed index=" + std::to_string(index)
                        + " slot=" + std::to_string(slot) + " name=" + ignore_sound_id;
                }
                return false;
            }
        }
        arrays->chord_note_types[index] = row.chord_id.empty() ? 0 : static_cast<uint8_t>(std::clamp(row.note_type, 0, 255));
        arrays->chord_dot_types[index] = row.chord_id.empty() ? 0 : static_cast<uint8_t>(std::clamp(row.dot_type, 0, 255));
        arrays->monotone_note_types[index] = row.monotone_id.empty() ? 0 : static_cast<uint8_t>(std::clamp(row.note_type, 0, 255));
        arrays->monotone_dot_types[index] = row.monotone_id.empty() ? 0 : static_cast<uint8_t>(std::clamp(row.dot_type, 0, 255));
        arrays->camera_switch_timings[index] = static_cast<uint8_t>(std::clamp(row.camera_switch_timing, 0, 255));
        // The generalized transaction relinks only after the final allocation is
        // stable. Suppress parser links for its exact first-512 source prefix.
        arrays->group_indices[index] = generalized_extended
            ? 0 : static_cast<uint8_t>(std::clamp(row.group_index, 0, 255));
    }

    const int32_t patched_count = static_cast<int32_t>(rows.size());
    const auto append_owned_array = [&](size_t header_index, uint64_t data, int32_t count, const char* label) {
        const uintptr_t header = layout.chart_base + layout.row_header_base + header_index * 16u;
        return append_field_patch(header, data, label, static_cast<int32_t>(header_index), entries)
            && append_field_patch(header + 8u, count, label, static_cast<int32_t>(header_index), entries)
            && append_field_patch(header + 12u, count, label, static_cast<int32_t>(header_index), entries);
    };
    const int32_t ignore_sound_patched_count = static_cast<int32_t>(ignore_sound_count);
    if (!append_owned_array(layout.time_header_index, reinterpret_cast<uint64_t>(arrays->time_entries.data()), patched_count, "PianoScore.TimeStr_Array")
        || !append_owned_array(kIgnoreSoundHeaderIndex, reinterpret_cast<uint64_t>(arrays->ignore_sound_ids.data()), ignore_sound_patched_count, "PianoScore.IgnoreSound_Array")
        || !append_owned_array(layout.monotone_id_header_index, reinterpret_cast<uint64_t>(arrays->monotone_ids.data()), patched_count, "PianoScore.MonotoneID_Array")
        || !append_owned_array(kChordIdHeaderIndex, reinterpret_cast<uint64_t>(arrays->chord_ids.data()), patched_count, "PianoScore.ChordID_Array")
        || !append_owned_array(kChordNoteTypeHeaderIndex, reinterpret_cast<uint64_t>(arrays->chord_note_types.data()), patched_count, "PianoScore.ChordNoteType_Array")
        || !append_owned_array(kChordDotTypeHeaderIndex, reinterpret_cast<uint64_t>(arrays->chord_dot_types.data()), patched_count, "PianoScore.ChordDotType_Array")
        || !append_owned_array(kMonotoneNoteTypeHeaderIndex, reinterpret_cast<uint64_t>(arrays->monotone_note_types.data()), patched_count, "PianoScore.MonotoneNoteType_Array")
        || !append_owned_array(kMonotoneDotTypeHeaderIndex, reinterpret_cast<uint64_t>(arrays->monotone_dot_types.data()), patched_count, "PianoScore.MonotoneDotType_Array")
        || !append_owned_array(kStrengthHeaderIndex, 0, 0, "PianoScore.Strength_Array")
        || !append_owned_array(kCameraSwitchTimingHeaderIndex, reinterpret_cast<uint64_t>(arrays->camera_switch_timings.data()), patched_count, "PianoScore.CameraSwitchTiming_Array")
        || !append_owned_array(kGroupIndexHeaderIndex, reinterpret_cast<uint64_t>(arrays->group_indices.data()), patched_count, "PianoScore.GroupIndex_Array")) {
        if (fail_reason) {
            *fail_reason = "owned_array_patch_plan_failed";
        }
        return false;
    }
    static std::atomic_int s_plan_logs{0};
    const int plan_log_index = s_plan_logs.fetch_add(1, std::memory_order_relaxed);
    if (plan_log_index < 16) {
        std::ostringstream out_log;
        out_log << "[chart_patch] plan status=ok song_id=" << song_id
            << " difficulty=" << profile.difficulty
            << " note_rows=" << rows.size()
            << " native_events=" << native_events.event_count
            << " grouping_present=" << native_events.grouping_present
            << " native_stable_link_capacity=" << kNativeStableLinkedEventCapacity
            << " ignore_sound_entries=" << ignore_sound_count
            << " time_rows=" << rows.size()
            << " skipped_time_rows=0"
            << " time_header=" << layout.time_header_index
            << " monotone_header=" << layout.monotone_id_header_index
            << " entries=" << entries.size();
        core::log(core::LogLevel::Info, out_log.str());
    }
    if (entries.empty()) return false;
    out.arrays = std::move(arrays);
    out.entries = std::move(entries);
    return true;
}

bool try_plan_descriptor_chart_patch(
    const SongDifficultyProfile& profile,
    const std::string& song_id,
    const ChartMemoryLayout& layout,
    const ChartNameResolver& resolver,
    PlannedDescriptorChartPatch& out,
    std::string* fail_reason = nullptr) noexcept
{
    try {
        return plan_descriptor_chart_patch(
            profile, song_id, layout, resolver, out, fail_reason);
    } catch (...) {
        out = {};
        if (fail_reason) {
            try {
                *fail_reason = "owned_array_allocation_or_plan_exception";
            } catch (...) {
            }
        }
        return false;
    }
}

bool should_cap_wrapper_count_after_expand(
    const int32_t copied_count, const int32_t published_source_rows,
    const bool preserve_generalized_parser_count) noexcept
{
    return !preserve_generalized_parser_count
        && copied_count > published_source_rows;
}

#ifndef FF7RP_CHART_PATCH_SELFTEST
bool find_live_pianoscore_object(void*& object, ChartMemoryLayout& layout, std::string& reason)
{
    object = nullptr;
    layout = {};
    reason.clear();
    if (!g_chart_fname_ctor) {
        reason = "missing_fname_ctor";
        return false;
    }

    const uintptr_t cached = g_cached_pianoscore_object.load(std::memory_order_relaxed);
    if (cached) {
        void* cached_object = reinterpret_cast<void*>(cached);
        if (resolve_chart_layout_from_score_object(cached_object, g_chart_fname_ctor, layout, reason)) {
            object = cached_object;
            return true;
        }
        g_cached_pianoscore_object.store(0, std::memory_order_relaxed);
    }

    GUObjectArrayView view{};
    if (!read_chart_guobject_array(view)) {
        reason = "guobjectarray_unavailable";
        return false;
    }

    FNameValue piano_score_name{};
    if (!construct_fname_find(g_chart_fname_ctor, L"PianoScore", piano_score_name)) {
        reason = "pianoscore_fname_missing";
        return false;
    }

    int name_matches = 0;
    int layout_matches = 0;
    std::string last_layout_reason;
    for (int32_t index = 0; index < view.num_elements; ++index) {
        void* candidate = nullptr;
        if (!read_chart_uobject_item_object(view, index, candidate)) {
            continue;
        }
        FNameValue object_name{};
        if (!core::safe_read_field(candidate, runtime_layouts::UObject::name, object_name)
            || object_name.comparison_id != piano_score_name.comparison_id) {
            continue;
        }
        ++name_matches;
        ChartMemoryLayout candidate_layout{};
        std::string candidate_reason;
        if (resolve_chart_layout_from_score_object(candidate, g_chart_fname_ctor, candidate_layout, candidate_reason)) {
            ++layout_matches;
            object = candidate;
            layout = candidate_layout;
            g_cached_pianoscore_object.store(reinterpret_cast<uintptr_t>(candidate), std::memory_order_relaxed);
            static std::atomic_int s_logs{0};
            const int log_index = s_logs.fetch_add(1, std::memory_order_relaxed);
            if (log_index < 8) {
                std::ostringstream out;
                out << "[chart_patch] pianoscore_object status=found"
                    << " object=0x" << std::hex << reinterpret_cast<uintptr_t>(candidate)
                    << " chart_base=0x" << candidate_layout.chart_base
                    << " field=0x" << candidate_layout.score_field_offset
                    << std::dec
                    << " object_index=" << index
                    << " name_matches=" << name_matches
                    << " layout_reason=" << candidate_reason;
                core::log(core::LogLevel::Info, out.str());
            }
            return true;
        }
        last_layout_reason = candidate_reason;
    }

    std::ostringstream out_reason;
    out_reason << "pianoscore_layout_missing name_matches=" << name_matches
        << " layout_matches=" << layout_matches
        << " last=" << last_layout_reason;
    reason = out_reason.str();
    return false;
}

bool selection_chart_rows(
    const SelectionSnapshot& selection, std::vector<DescriptorChartRow>& rows)
{
    if (!selection.song || !selection.profile
        || !build_descriptor_chart_rows(*selection.profile, rows) || rows.empty()
        || rows.size() > ff7rp::pipeline::chart_input_row_limit()) {
        rows.clear();
        return false;
    }
    return true;
}

int32_t resolve_expand_row_index(void* wrapper, size_t row_count)
{
    int32_t wrapper_count = -1;
    if (core::safe_read_field(wrapper, runtime_layouts::PianoScoreWrapper::copied_row_count, wrapper_count)
        && wrapper_count >= 0
        && wrapper_count < static_cast<int32_t>(row_count)) {
        return wrapper_count;
    }

    const uintptr_t wrapper_value = reinterpret_cast<uintptr_t>(wrapper);
    const uintptr_t previous_wrapper = g_row_patch_wrapper.load(std::memory_order_relaxed);
    if (previous_wrapper != wrapper_value) {
        g_row_patch_wrapper.store(wrapper_value, std::memory_order_relaxed);
        g_row_patch_next_index.store(0, std::memory_order_relaxed);
    }

    const int32_t fallback_index = g_row_patch_next_index.fetch_add(1, std::memory_order_relaxed);
    return fallback_index >= 0 && fallback_index < static_cast<int32_t>(row_count) ? fallback_index : -1;
}

bool append_chart_row_patch_plan(
    void* chart_row,
    int32_t row_index,
    const DescriptorChartRow& row,
    std::vector<JournalEntry>& out,
    std::string* fail_reason = nullptr)
{
    if (!chart_row || row_index < 0 || !g_chart_fname_ctor) {
        if (fail_reason) {
            *fail_reason = !chart_row ? "missing_chart_row" : (row_index < 0 ? "invalid_row_index" : "missing_fname_ctor");
        }
        return false;
    }

    size_t row_available = 0;
    size_t available = 0;
    const uintptr_t row_base = reinterpret_cast<uintptr_t>(chart_row);
    if (!query_readable_region(row_base, available)) {
        if (fail_reason) {
            *fail_reason = "row_unreadable";
        }
        return false;
    }
    row_available = std::min<size_t>(available, 0x1000u);

    std::string old_time;
    uintptr_t payload_offset = 0;
    int32_t chars = 0;
    if (!decode_time_string(row_base, row_available, kChartRowTimeStrOffset, old_time, payload_offset, chars)) {
        if (fail_reason) {
            *fail_reason = "time_decode_failed";
        }
        return false;
    }
    const std::vector<uint8_t> old_time_bytes = utf16_ascii_bytes(old_time);
    const std::vector<uint8_t> new_time_bytes = utf16_ascii_bytes(row.time_str);
    if (old_time.empty() || old_time_bytes.size() != new_time_bytes.size()) {
        if (fail_reason) {
            std::ostringstream out_reason;
            out_reason << "time_length_mismatch old=\"" << old_time << "\" new=\"" << row.time_str << "\" old_bytes=" << old_time_bytes.size() << " new_bytes=" << new_time_bytes.size();
            *fail_reason = out_reason.str();
        }
        return false;
    }
    if (!append_byte_patch(row_base + payload_offset, old_time_bytes, new_time_bytes, "PianoScore.Row.TimeStr", row_index, out)) {
        if (fail_reason) {
            *fail_reason = "time_patch_plan_failed";
        }
        return false;
    }

    uint64_t old_note = 0;
    uint64_t new_note = 0;
    const uintptr_t note_address = row_base + kChartRowMonotoneIdOffset;
    if (!core::safe_read_field(reinterpret_cast<void*>(note_address), 0, old_note)) {
        if (fail_reason) {
            *fail_reason = "old_note_read_failed";
        }
        return false;
    }
    if (!resolve_monotone_id_fname(g_chart_fname_ctor, row.monotone_id, new_note)) {
        if (fail_reason) {
            *fail_reason = "monotone_fname_resolve_failed:" + row.monotone_id;
        }
        return false;
    }
    if (!append_byte_patch(note_address, bytes_from_value(old_note), bytes_from_value(new_note), "PianoScore.Row.MonotoneID", row_index, out)) {
        if (fail_reason) {
            *fail_reason = "note_patch_plan_failed";
        }
        return false;
    }

    const uint8_t note_type = static_cast<uint8_t>(std::clamp(row.note_type, 0, 255));
    if (!append_field_patch(row_base + kChartRowNoteTypeOffset, note_type, "PianoScore.Row.NoteType", row_index, out)
        || !append_field_patch(row_base + kChartRowDotTypeOffset, row.dot_type, "PianoScore.Row.DotType", row_index, out)
        || !append_field_patch(row_base + kChartRowCameraSwitchTimingOffset, row.camera_switch_timing, "PianoScore.Row.CameraSwitchTiming", row_index, out)
        || !append_field_patch(row_base + kChartRowGroupIndexOffset, row.group_index, "PianoScore.Row.GroupIndex", row_index, out)) {
        if (fail_reason) {
            *fail_reason = "field_patch_plan_failed";
        }
        return false;
    }

    return true;
}
#endif

enum class PlannedChartWriteResult : uint8_t {
    Applied,
    RolledBack,
    Unresolved,
};

PlannedChartWriteResult apply_planned_descriptor_chart_patch(
    uintptr_t score_object, uintptr_t chart_base,
    const SelectionSnapshot& selection,
    const AudioRouteLeaseIdentity admission_lease,
    PlannedDescriptorChartPatch planned)
{
    std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
    if (!planned.arrays || planned.entries.empty()
        || g_chart_patch_journal.active || !g_chart_patch_journal.entries.empty()) {
        return PlannedChartWriteResult::Unresolved;
    }
    g_chart_patch_journal.active = true;
    g_chart_patch_journal.score_object = score_object;
    g_chart_patch_journal.chart_base = chart_base;
    g_chart_patch_journal.selection = selection;
    g_chart_patch_journal.admission_lease = admission_lease;
    g_chart_patch_journal.arrays = std::move(planned.arrays);
    g_chart_patch_journal.entries = std::move(planned.entries);

    for (const JournalEntry& entry : g_chart_patch_journal.entries) {
        const ChartWriteResult write = write_checked_bytes(entry);
        if (write != ChartWriteResult::NotWritten) {
            ++g_chart_patch_journal.attempted_entries;
        }
        if (write != ChartWriteResult::Applied) {
            bool restored = true;
            for (size_t index = g_chart_patch_journal.attempted_entries;
                 index != 0; --index) {
                restored = restore_entry(
                    g_chart_patch_journal.entries[index - 1], true) && restored;
            }
            if (restored) {
                g_chart_patch_journal.clear();
                return PlannedChartWriteResult::RolledBack;
            }
            g_chart_patch_journal.mutation_unresolved = true;
            return PlannedChartWriteResult::Unresolved;
        }
    }
    return PlannedChartWriteResult::Applied;
}

struct ChartRestoreDiagnostic {
    const char* reason = nullptr;
    size_t entries = 0;
    size_t restored_bytes = 0;
    uintptr_t score_object = 0;
    uintptr_t chart_base = 0;
};

void log_chart_restore_best_effort(
    const ChartRestoreDiagnostic& diagnostic) noexcept
{
    chart_diagnostic_best_effort([&]() {
        std::ostringstream out;
        out << "[chart_patch] restore reason="
            << (diagnostic.reason ? diagnostic.reason : "?")
            << " status=ok"
            << " entries=" << diagnostic.entries
            << " restored_bytes=" << diagnostic.restored_bytes
            << " score_object=0x" << std::hex << diagnostic.score_object
            << " chart_base=0x" << diagnostic.chart_base
            << std::dec;
        core::log(core::LogLevel::Debug, out.str());
    });
}

void log_chart_transaction_best_effort(
    const core::LogLevel level, const char* message) noexcept
{
    chart_diagnostic_best_effort([&]() {
        core::log(level, message ? message : "[chart_patch] transaction status=unknown");
    });
}

bool restore_chart_patch_state(
    const char* reason, const bool clear_after_restore,
    SelectionSnapshot* const restored_selection = nullptr) noexcept
{
    ChartRestoreDiagnostic diagnostic{};
    try {
        {
            std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
            if (!g_chart_patch_journal.active
                && g_chart_patch_journal.entries.empty()) {
                g_chart_patch_journal.clear();
                return true;
            }

            ChartPatchJournal& journal = g_chart_patch_journal;
            if (!journal.chart_restored) {
                for (size_t index = journal.attempted_entries; index != 0; --index) {
                    const JournalEntry& entry = journal.entries[index - 1];
                    if (!restore_entry(entry)) {
                        journal.mutation_unresolved = true;
                        return false;
                    }
                    diagnostic.restored_bytes += entry.original.size();
                }
                journal.chart_restored = true;
            }
            const bool cancellation_committed
                = !journal.audio_cancellation_required
#ifndef FF7RP_CHART_PATCH_SELFTEST
                || selection_audio_admission_cancellation_complete(
                    journal.selection, journal.admission_lease)
#endif
                ;
            if (clear_after_restore
                && !chart_transaction_journal_may_clear(
                    journal.chart_restored,
                    journal.audio_cancellation_required,
                    cancellation_committed)) {
                journal.mutation_unresolved = true;
                return false;
            }
            diagnostic.reason = reason;
            diagnostic.entries = journal.entries.size();
            diagnostic.score_object = journal.score_object;
            diagnostic.chart_base = journal.chart_base;
            if (clear_after_restore && restored_selection) {
                *restored_selection = journal.selection;
            }
            if (clear_after_restore) journal.clear();
        }
        log_chart_restore_best_effort(diagnostic);
        return true;
    } catch (...) {
        return false;
    }
}

bool restore_and_reset_chart_patch_state(
    const char* reason, SelectionSnapshot* restored_selection = nullptr) noexcept
{
    return restore_chart_patch_state(reason, true, restored_selection);
}

#ifndef FF7RP_CHART_PATCH_SELFTEST
bool restore_chart_and_cancel_audio(
    SelectionAudioAdmission& admission, const char* reason) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
        if (!g_chart_patch_journal.active) return false;
        g_chart_patch_journal.audio_cancellation_required = true;
    } catch (...) {
        return false;
    }
    if (!restore_chart_patch_state(reason, false)) return false;
    if (!cancel_selection_audio_admission(admission)) return false;
    try {
        std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
        if (!g_chart_patch_journal.active
            || !chart_transaction_journal_may_clear(
                g_chart_patch_journal.chart_restored,
                g_chart_patch_journal.audio_cancellation_required, true)) {
            return false;
        }
        g_chart_patch_journal.clear();
        return true;
    } catch (...) {
        return false;
    }
}

[[maybe_unused]] int count_descriptors_with_chart_rows()
{
    int count = 0;
    std::vector<DescriptorChartRow> rows;
    const RegistrySnapshot registry_view = registry().registry_snapshot();
    for (const SongDescriptor& song : registry_view.songs()) {
        for (const SongDifficultyProfile& profile : song.profiles) {
            if (build_descriptor_chart_rows(profile, rows)) {
                ++count;
            }
        }
    }
    return count;
}

struct ChartDescriptorReadiness {
    int descriptors_with_rows = 0;
    int descriptors_resolvable = 0;
    int rows = 0;
    int unresolved_rows = 0;
};

ChartDescriptorReadiness descriptor_readiness(FNameCtorFn fname_ctor)
{
    ChartDescriptorReadiness readiness{};
    std::vector<DescriptorChartRow> rows;
    const RegistrySnapshot registry_view = registry().registry_snapshot();
    for (const SongDescriptor& song : registry_view.songs()) {
        for (const SongDifficultyProfile& profile : song.profiles) {
            if (!build_descriptor_chart_rows(profile, rows)) {
                continue;
            }
            ++readiness.descriptors_with_rows;
            readiness.rows += static_cast<int>(rows.size());
            bool descriptor_ok = true;
            for (const DescriptorChartRow& row : rows) {
                uint64_t packed = 0;
                if (!row.monotone_id.empty() && !resolve_monotone_id_fname(fname_ctor, row.monotone_id, packed)) {
                    descriptor_ok = false;
                    ++readiness.unresolved_rows;
                }
                if (!row.chord_id.empty() && !resolve_monotone_id_fname(fname_ctor, row.chord_id, packed)) {
                    descriptor_ok = false;
                    ++readiness.unresolved_rows;
                }
                for (const std::string& ignore_sound_id : row.ignore_sound_ids) {
                    if (!ignore_sound_id.empty()
                        && !resolve_monotone_id_fname(fname_ctor, ignore_sound_id, packed)) {
                        descriptor_ok = false;
                        ++readiness.unresolved_rows;
                    }
                }
            }
            if (descriptor_ok) {
                ++readiness.descriptors_resolvable;
            }
        }
    }
    return readiness;
}

void log_extended_chart_source_boundary(const std::vector<DescriptorChartRow>& rows)
{
    if (!ff7rp::pipeline::experimental_extended_charts_requested()
        || rows.size() <= ff7rp::pipeline::kMaxChartRows) {
        return;
    }
    static std::atomic_int s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 4) {
        return;
    }
    const size_t first = ff7rp::pipeline::kMaxChartRows >= 4u
        ? ff7rp::pipeline::kMaxChartRows - 4u : 0u;
    const size_t end = std::min(rows.size(), ff7rp::pipeline::kMaxChartRows + 8u);
    std::ostringstream out;
    out << "[extended_chart_diag] source_rows=" << rows.size() << " boundary=";
    for (size_t index = first; index < end; ++index) {
        const DescriptorChartRow& row = rows[index];
        out << (index == first ? "" : ";")
            << index << ':' << row.time_str
            << ":m=" << row.monotone_id
            << ":c=" << row.chord_id
            << ":n=" << row.note_type
            << ":d=" << row.dot_type
            << ":cam=" << row.camera_switch_timing
            << ":g=" << row.group_index;
    }
    core::log(core::LogLevel::Info, out.str());
}
#endif

} // namespace

#ifdef FF7RP_CHART_PATCH_SELFTEST
bool chart_patch_ignore_sound_selftest()
{
    struct NativeArrayHeader {
        uint64_t data = 0;
        int32_t num = 0;
        int32_t max = 0;
    };
    static_assert(sizeof(NativeArrayHeader) == 16);

    (void)restore_and_reset_chart_patch_state("selftest_begin");
    std::vector<uint8_t> chart_bytes(
        kObservedPianoScoreRowCounts.size() * sizeof(NativeArrayHeader));
    for (size_t index = 0; index < kObservedPianoScoreRowCounts.size(); ++index) {
        NativeArrayHeader header{
            0x10000000u + static_cast<uint64_t>(index) * 0x100u,
            kObservedPianoScoreRowCounts[index],
            kObservedPianoScoreRowCounts[index],
        };
        std::memcpy(chart_bytes.data() + index * sizeof(header), &header, sizeof(header));
    }
    const std::vector<uint8_t> original_chart = chart_bytes;

    ChartMemoryLayout layout{};
    layout.chart_base = reinterpret_cast<uintptr_t>(chart_bytes.data());
    layout.chart_size = chart_bytes.size();
    layout.row_header_base = 0;
    layout.time_header_index = kTimeStrHeaderIndex;
    layout.monotone_id_header_index = kMonotoneIdHeaderIndex;

    ChartNameResolver resolver{};
    const auto resolve = [](const std::string& name, uint64_t& packed) {
        if (name == "Mono") packed = 0x1001u;
        else if (name == "Chord") packed = 0x1002u;
        else if (name == "A") packed = 0x2001u;
        else if (name == "B") packed = 0x2002u;
        else if (name == "C") packed = 0x2003u;
        else return false;
        return true;
    };
    resolver.resolve_monotone_id = resolve;
    resolver.resolve_chord_id = resolve;
    resolver.resolve_ignore_sound_id = resolve;

    const auto make_profile = [](std::array<std::string, 3> first,
                                 std::array<std::string, 3> second) {
        SongDifficultyProfile profile;
        profile.difficulty = 4;
        profile.chart_notes.push_back(
            {"00_00", "Mono", "Chord", 3, 0, 0, 7, std::move(first)});
        profile.chart_notes.push_back(
            {"00_25", "Mono", "Chord", 3, 0, 0, 8, std::move(second)});
        return profile;
    };
    const auto fail = [&]() {
        (void)restore_and_reset_chart_patch_state("selftest_failure");
        return false;
    };

    const auto make_event_profile = [](const size_t grouped_monotone_rows,
                                       const size_t dual_hand_rows,
                                       const bool grouped,
                                       const int difficulty) {
        SongDifficultyProfile profile;
        profile.difficulty = difficulty;
        const size_t row_count = grouped_monotone_rows + dual_hand_rows;
        profile.chart_notes.reserve(row_count);
        for (size_t index = 0; index < row_count; ++index) {
            const bool dual_hand = index >= grouped_monotone_rows;
            profile.chart_notes.push_back({
                "00_00", "Mono", dual_hand ? "Chord" : "",
                3, 0, 0, grouped && !dual_hand ? 1 : 0, {},
            });
        }
        return profile;
    };

    SongDescriptor boundary_song;
    boundary_song.id = "selftest-event-boundary";
    boundary_song.profiles.push_back(make_event_profile(2u, 255u, true, 1));
    boundary_song.profiles.push_back(make_event_profile(3u, 255u, true, 2));

    PlannedDescriptorChartPatch boundary_plan;
    std::string boundary_reason;
    if (!try_plan_descriptor_chart_patch(
            boundary_song.profiles[0], boundary_song.id, layout, resolver,
            boundary_plan, &boundary_reason)
        || !boundary_plan.arrays || boundary_plan.entries.empty()
        || chart_bytes != original_chart
        || g_chart_patch_journal.active || !g_chart_patch_journal.entries.empty()) {
        return fail();
    }

    boundary_plan = {};
    ChartNameResolver rejecting_resolver{};
    const auto reject_name = [](const std::string&, uint64_t&) { return false; };
    rejecting_resolver.resolve_monotone_id = reject_name;
    rejecting_resolver.resolve_chord_id = reject_name;
    rejecting_resolver.resolve_ignore_sound_id = reject_name;
    if (try_plan_descriptor_chart_patch(
            boundary_song.profiles[1], boundary_song.id, layout, rejecting_resolver,
            boundary_plan, &boundary_reason)
        || boundary_plan.arrays || !boundary_plan.entries.empty()
        || boundary_reason != "native_event_link_stability_guard row_count=258 event_count=513 native_stable_link_capacity=512 grouping_present=1"
        || chart_bytes != original_chart
        || g_chart_patch_journal.active || g_chart_patch_journal.attempted_entries != 0
        || g_chart_patch_journal.arrays || !g_chart_patch_journal.entries.empty()) {
        return fail();
    }

    const SongDifficultyProfile ungrouped_513
        = make_event_profile(1u, 256u, false, 3);
    if (!try_plan_descriptor_chart_patch(
            ungrouped_513, "selftest-ungrouped-513", layout, resolver,
            boundary_plan, &boundary_reason)
        || !boundary_plan.arrays || boundary_plan.entries.empty()
        || chart_bytes != original_chart
        || g_chart_patch_journal.active || !g_chart_patch_journal.entries.empty()) {
        return fail();
    }

    SongDifficultyProfile generalized;
    generalized.difficulty = 4;
    generalized.chart_notes.assign(512,
        SongChartNote{"00_00", "Mono", "", 3, 0, 0, 0, {}});
    generalized.extended_chart_tail_notes.push_back(
        SongChartNote{"00_00", "Mono", "", 3, 0, 0, 1, {}});
    generalized.chart_notes[511].group_index = 1;
    std::vector<ff7rp::pipeline::ChartEventRow> generalized_rows;
    generalized_rows.reserve(513);
    for (const auto& row : generalized.chart_notes)
        generalized_rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(row));
    generalized_rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(
        generalized.extended_chart_tail_notes.front()));
    const auto generalized_events = ff7rp::pipeline::derive_chart_event_plan(generalized_rows);
    ff7rp::pipeline::configure_chart_row_limit(true, true, true);
    generalized.source_row_count = generalized_events.source_row_count;
    generalized.native_prefix_event_count = generalized_events.native_prefix_event_count;
    generalized.native_event_count = generalized_events.native_event_count;
    generalized.required_action_count = generalized_events.required_action_count;
    generalized.note_count = static_cast<int32_t>(generalized_events.required_action_count);
    generalized.physical_chart_digest = generalized_events.physical_digest;
    generalized.diagnostic_descriptor_hash = 1;
    generalized.diagnostic_policy_generation = ff7rp::pipeline::chart_row_policy_generation();
    boundary_plan = {};
    const bool suppression_ok = try_plan_descriptor_chart_patch(
            generalized, "selftest-generalized-groups", layout, resolver,
            boundary_plan, &boundary_reason)
        && boundary_plan.arrays
        && std::all_of(boundary_plan.arrays->group_indices.begin(),
            boundary_plan.arrays->group_indices.end(), [](const uint8_t value) { return value == 0; });
    ff7rp::pipeline::configure_chart_row_limit(false, false, false);
    if (!suppression_ok) return fail();

    // Generalized cleanup preserves exact compact P only with synchronous
    // authority. Missing authority and legacy diagnostics retain the cap;
    // ordinary equal counts never produce a duplicate native write.
    if (should_cap_wrapper_count_after_expand(1022, 512, true)
        || !should_cap_wrapper_count_after_expand(1022, 512, false)
        || should_cap_wrapper_count_after_expand(512, 512, false)
        || !should_cap_wrapper_count_after_expand(520, 512, false)) {
        return fail();
    }

    PlannedDescriptorChartPatch planned;
    std::string reason;
    const SongDifficultyProfile root_profile = make_profile({"A", "", "C"}, {"", "B", ""});
    if (!try_plan_descriptor_chart_patch(
            root_profile, "selftest-root", layout, resolver, planned, &reason)
        || !planned.arrays
        || planned.arrays->ignore_sound_ids
            != std::vector<uint64_t>({0x2001u, 0u, 0x2003u, 0u, 0x2002u, 0u})) {
        return fail();
    }
    OwnedDescriptorChartArrays* const first_owner = planned.arrays.get();
    const PlannedChartWriteResult first_write = apply_planned_descriptor_chart_patch(
        0, layout.chart_base, {}, {}, std::move(planned));
    NativeArrayHeader published{};
    std::memcpy(&published,
        chart_bytes.data() + kIgnoreSoundHeaderIndex * sizeof(published), sizeof(published));
    if (first_write != PlannedChartWriteResult::Applied
        || published.data != reinterpret_cast<uint64_t>(first_owner->ignore_sound_ids.data())
        || published.num != 6 || published.max != 6
        || g_chart_patch_journal.arrays.get() != first_owner) {
        return fail();
    }

    SongDifficultyProfile missing_profile = root_profile;
    missing_profile.chart_notes[0].ignore_sound_ids[1] = "Missing";
    PlannedDescriptorChartPatch rejected;
    if (try_plan_descriptor_chart_patch(
            missing_profile, "selftest-missing", layout, resolver, rejected, &reason)
        || rejected.arrays || !rejected.entries.empty()
        || reason.find("ignore_sound_resolve_failed index=0 slot=1") != 0
        || g_chart_patch_journal.arrays.get() != first_owner
        || first_owner->ignore_sound_ids[4] != 0x2002u) {
        return fail();
    }
    if (!restore_and_reset_chart_patch_state("selftest_root_restore")
        || chart_bytes != original_chart) {
        return fail();
    }

    const SongDifficultyProfile switched_profile = make_profile({"C", "A", ""}, {"", "", "B"});
    if (!try_plan_descriptor_chart_patch(
            switched_profile, "selftest-profile", layout, resolver, planned, &reason)
        || planned.arrays->ignore_sound_ids
            != std::vector<uint64_t>({0x2003u, 0x2001u, 0u, 0u, 0u, 0x2002u})
        || apply_planned_descriptor_chart_patch(0, layout.chart_base, {}, {}, std::move(planned))
            != PlannedChartWriteResult::Applied
        || !restore_and_reset_chart_patch_state("selftest_profile_restore")
        || chart_bytes != original_chart) {
        return fail();
    }

    if (!try_plan_descriptor_chart_patch(
            root_profile, "selftest-rollback", layout, resolver, planned, &reason)
        || planned.entries.size() < 5u) {
        return fail();
    }
    const uintptr_t drift_address = planned.entries[4].address;
    uint8_t drift = 0;
    if (!core::safe_read_field(reinterpret_cast<void*>(drift_address), 0, drift)) {
        return fail();
    }
    ++drift;
    if (!core::safe_write_bytes(reinterpret_cast<void*>(drift_address), &drift, sizeof(drift))) {
        return fail();
    }
    const std::vector<uint8_t> drifted_chart = chart_bytes;
    if (apply_planned_descriptor_chart_patch(0, layout.chart_base, {}, {}, std::move(planned))
            != PlannedChartWriteResult::RolledBack
        || chart_bytes != drifted_chart
        || g_chart_patch_journal.active || !g_chart_patch_journal.entries.empty()
        || g_chart_patch_journal.arrays) {
        return fail();
    }
    return true;
}
#endif

#ifndef FF7RP_CHART_PATCH_SELFTEST
bool chart_audio_diagnostic_transaction_exact(
    const uint64_t selection_generation,
    const uint64_t route_generation,
    const uint64_t lease_generation,
    const uint64_t song_key,
    ChartAudioDiagnosticTransaction& out) noexcept
{
    out = {};
    try {
        std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
        const auto& current = g_chart_audio_diagnostic_transaction;
        if (!current.active || current.generation == 0
            || current.prewrite.selection_generation != selection_generation
            || current.prewrite.route_generation != route_generation
            || current.prewrite.lease_generation != lease_generation
            || current.prewrite.song_key != song_key) {
            return false;
        }
        out = current;
        return true;
    } catch (...) {
        return false;
    }
}

bool chart_audio_diagnostic_generation_exact(
    const uint64_t generation,
    ChartAudioDiagnosticTransaction& out) noexcept
{
    out = {};
    try {
        std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
        const auto& current = g_chart_audio_diagnostic_transaction;
        if (generation == 0 || !current.active
            || current.generation != generation) {
            return false;
        }
        out = current;
        return true;
    } catch (...) {
        return false;
    }
}

void chart_audio_diagnostic_expand_completed(
    const ChartAudioDiagnosticTransaction& transaction) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
        if (!transaction.active || transaction.generation == 0
            || !g_chart_audio_diagnostic_transaction.active
            || g_chart_audio_diagnostic_transaction.generation
                != transaction.generation) {
            return;
        }
        // These native addresses are callback-lifetime observations only.
        // Retain the immutable generation/state association, but never the
        // wrapper or row pointers after chart expansion returns.
        g_chart_audio_diagnostic_transaction.wrapper = 0;
        g_chart_audio_diagnostic_transaction.chart_row = 0;
    } catch (...) {
    }
}

void finish_chart_audio_diagnostic_transaction(
    const ChartAudioDiagnosticTransaction& transaction,
    const ChartAudioDiagnosticTerminalOutcome outcome,
    const uint64_t successful_lifecycle_epoch) noexcept
{
    ChartAudioDiagnosticTransaction terminal;
    try {
        {
            std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
            if (!transaction.active || transaction.generation == 0
                || !g_chart_audio_diagnostic_transaction.active
                || g_chart_audio_diagnostic_transaction.generation
                    != transaction.generation) {
                return;
            }
            terminal = g_chart_audio_diagnostic_transaction;
            terminal.active = false;
            terminal.terminal_outcome = outcome;
            g_chart_audio_diagnostic_transaction = {};
        }
        log_chart_audio_diagnostic_transaction("terminal", terminal);
        extended_chart_activation_terminal(
            terminal.generation, terminal.route_lifecycle_epoch, outcome,
            successful_lifecycle_epoch);
    } catch (...) {
    }
}

ChartExpandPreparationOutcome prepare_active_chart_row_patch_impl(
    void* wrapper, void* chart_row, uintptr_t caller_rva,
    SelectionAudioAdmission& admission,
    ChartAudioDiagnosticTransaction* diagnostic,
    SelectionAudioAdmissionAuthority* authority)
{
    if (caller_rva != rva::PersistentChartExpandCaller || !wrapper || !chart_row) {
        return ChartExpandPreparationOutcome::NativePristine;
    }

    SelectionActivationClaim activation_claim
        = claim_selection_audio_activation(wrapper, caller_rva);
    if (!activation_claim) {
        return ChartExpandPreparationOutcome::NativePristine;
    }
    const SelectionSnapshot selection
        = selection_audio_activation_claim_selection(activation_claim);
    std::vector<DescriptorChartRow> rows;
    if (!selection_chart_rows(selection, rows)) {
        return ChartExpandPreparationOutcome::NativePristine;
    }
    const SongDescriptor* const song = selection.song;
    const SongDifficultyProfile* const profile = selection.profile;
    log_extended_chart_source_boundary(rows);

    void* score_object = nullptr;
    ChartMemoryLayout layout{};
    std::string layout_reason;
    const bool layout_ok = find_live_pianoscore_object(score_object, layout, layout_reason);
    ChartNameResolver resolver{};
    resolver.resolve_monotone_id = [](const std::string& monotone_id, uint64_t& packed_name) {
        return resolve_monotone_id_fname(g_chart_fname_ctor, monotone_id, packed_name);
    };
    resolver.resolve_chord_id = [](const std::string& chord_id, uint64_t& packed_name) {
        return resolve_monotone_id_fname(g_chart_fname_ctor, chord_id, packed_name);
    };
    resolver.resolve_ignore_sound_id = [](const std::string& ignore_sound_id, uint64_t& packed_name) {
        return resolve_monotone_id_fname(g_chart_fname_ctor, ignore_sound_id, packed_name);
    };
    PlannedDescriptorChartPatch planned;
    std::string plan_fail_reason;
    const bool planned_ok = layout_ok && song && profile
        && try_plan_descriptor_chart_patch(
            *profile, song->id, layout, resolver, planned, &plan_fail_reason);
    if (!planned_ok) {
        static std::atomic_int s_plan_failure_logs{0};
        if (s_plan_failure_logs.fetch_add(1, std::memory_order_relaxed) < 32) {
            chart_diagnostic_best_effort([&]() {
                const std::string reason = !layout_ok
                    ? (layout_reason.empty() ? "invalid_live_chart_layout" : layout_reason)
                    : !song ? "missing_song_descriptor"
                    : !profile ? "missing_difficulty_profile"
                    : plan_fail_reason.empty() ? "descriptor_chart_plan_failed"
                                               : plan_fail_reason;
                std::ostringstream out;
                out << "[chart_patch] plan status=skipped reason=" << reason
                    << " song_id=" << (song ? song->id : "<none>")
                    << " difficulty=" << (profile ? profile->difficulty : 0)
                    << " layout_ok=" << (layout_ok ? 1 : 0);
                core::log(core::LogLevel::Error, out.str());
            });
        }
        return ChartExpandPreparationOutcome::NativePristine;
    }

    admission = begin_selection_audio_admission(
        std::move(activation_claim), planned_ok);
    ChartAudioAdmissionCoordinator admission_coordinator;
    if (!admission) {
        static std::atomic_int s_admission_logs{0};
        if (s_admission_logs.fetch_add(1, std::memory_order_relaxed) < 32) {
            std::ostringstream out;
            out << "[chart_patch] admission status=activation_audio_not_ready"
                << " reason=admission_rejected"
                << " song_id=" << song->id
                << " selection_generation=" << selection.generation
                << " chart_apply_attempted=0 native_chart_preserved=1";
            core::log(core::LogLevel::Info, out.str());
        }
        admission = {};
        return ChartExpandPreparationOutcome::NativePristine;
    }
    if (!admission_coordinator.reserve({
            true, true, true, true, true, true, true, true, true,
    })) {
        admission = {};
        return ChartExpandPreparationOutcome::NativePristine;
    }

    const AudioRouteLeaseIdentity admission_lease
        = selection_audio_admission_lease(admission);
    SelectionAudioAdmissionAuthority captured_authority;
    (void)capture_selection_audio_admission_authority(admission, captured_authority);
    if (diagnostic) {
        diagnostic->route_lifecycle_epoch = captured_authority.route_lifecycle_epoch;
        diagnostic->wrapper = reinterpret_cast<uintptr_t>(wrapper);
        diagnostic->chart_row = reinterpret_cast<uintptr_t>(chart_row);
        (void)capture_selection_audio_admission_diagnostic(
            admission, diagnostic->prewrite);
        uint64_t generation = 0;
        uint64_t ordinal = 0;
        if (next_chart_audio_diagnostic_value(
                g_chart_audio_diagnostic_generation, generation)
            && next_chart_audio_diagnostic_value(
                g_chart_audio_diagnostic_ordinal, ordinal)) {
            diagnostic->generation = generation;
            diagnostic->preparation_ordinal = ordinal;
        }
    }
    const PlannedChartWriteResult write_result
        = apply_planned_descriptor_chart_patch(
        reinterpret_cast<uintptr_t>(score_object), layout.chart_base,
        selection, admission_lease, std::move(planned));
    bool applied = write_result == PlannedChartWriteResult::Applied;
    (void)admission_coordinator.finish_chart_write(applied);
    if (write_result == PlannedChartWriteResult::RolledBack) {
        admission = {};
        return ChartExpandPreparationOutcome::NativePristine;
    }
    if (write_result == PlannedChartWriteResult::Unresolved) {
        admission = {};
        block_custom_audio_route_for_unresolved_chart_mutation();
        log_chart_transaction_best_effort(core::LogLevel::Error,
            "[chart_patch] transaction status=mutation_unresolved stage=chart_write original_expand=blocked journal_retained=1");
        return ChartExpandPreparationOutcome::MutationUnresolved;
    }
    AudioRouteArmLease audio_lease{};
    if (applied && song) {
        audio_lease = commit_selection_audio_admission(admission);
    }
    if (audio_lease.status == AudioRouteArmStatus::Armed) {
        try {
            std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
            g_chart_patch_journal.audio_committed = true;
            g_chart_patch_journal.admission_lease = audio_lease.identity;
        } catch (...) {
            if (!restore_chart_and_cancel_audio(admission,
                    "selection_admission_commit_publication_failed")) {
                block_custom_audio_route_for_unresolved_chart_mutation();
                return ChartExpandPreparationOutcome::MutationUnresolved;
            }
            return ChartExpandPreparationOutcome::NativePristine;
        }
    }
    const bool expand_safe = chart_audio_expand_custom_data_allowed(
        applied,
        audio_lease.status == AudioRouteArmStatus::Armed,
        audio_lease.identity.valid()
            && audio_lease.identity.song_key != 0
            && song && selection.song == song);
    const bool transaction_committed = admission_coordinator.finish_audio_commit(
        expand_safe);
    if (applied && !transaction_committed) {
        applied = false;
        const bool transaction_cancelled = restore_chart_and_cancel_audio(
            admission, "selection_admission_commit_failed");
        admission_coordinator.cancel();
        if (!transaction_cancelled) {
            block_custom_audio_route_for_unresolved_chart_mutation();
            log_chart_transaction_best_effort(core::LogLevel::Error,
                "[chart_patch] admission status=cancel_failed reason=mutation_unresolved journal_retained=1");
            return ChartExpandPreparationOutcome::MutationUnresolved;
        }
        log_chart_transaction_best_effort(core::LogLevel::Info,
            "[chart_patch] admission status=activation_audio_not_ready reason=arm_commit_failed chart_restored=1 native_chart_preserved=1");
    }
    if (applied && !admission_coordinator.original_may_consume_custom_chart()) {
        applied = false;
        const bool transaction_cancelled = restore_chart_and_cancel_audio(
            admission, "selection_admission_invariant_failed");
        admission_coordinator.cancel();
        if (!transaction_cancelled) {
            block_custom_audio_route_for_unresolved_chart_mutation();
            return ChartExpandPreparationOutcome::MutationUnresolved;
        }
    }
    if (!applied || audio_lease.status != AudioRouteArmStatus::Armed) {
        admission = {};
    }

    const auto outcome = applied ? ChartExpandPreparationOutcome::CustomCommitted
                                 : ChartExpandPreparationOutcome::NativePristine;
    if (diagnostic) {
        diagnostic->preparation_outcome = static_cast<uint8_t>(outcome);
        diagnostic->journal_active = applied;
        diagnostic->audio_committed = applied
            && audio_lease.status == AudioRouteArmStatus::Armed;
        diagnostic->terminal_outcome = applied
            ? ChartAudioDiagnosticTerminalOutcome::CustomPrepared
            : ChartAudioDiagnosticTerminalOutcome::NativePristine;
        diagnostic->active = applied && diagnostic->generation != 0;
        if (diagnostic->active) {
            ChartAudioDiagnosticTransaction superseded;
            {
                std::lock_guard<std::mutex> lock(g_chart_patch_mutex);
                if (g_chart_audio_diagnostic_transaction.active) {
                    superseded = g_chart_audio_diagnostic_transaction;
                    superseded.active = false;
                    superseded.terminal_outcome
                        = ChartAudioDiagnosticTerminalOutcome::Superseded;
                }
                g_chart_audio_diagnostic_transaction = *diagnostic;
            }
            if (superseded.generation != 0) {
                log_chart_audio_diagnostic_transaction("terminal", superseded);
                extended_chart_activation_terminal(superseded.generation,
                    superseded.route_lifecycle_epoch,
                    ChartAudioDiagnosticTerminalOutcome::Superseded);
            }
        }
    }
    if (authority && applied && captured_authority.exact && diagnostic
        && diagnostic->active) {
        captured_authority.activation_generation = diagnostic->generation;
        captured_authority.preparation_ordinal = diagnostic->preparation_ordinal;
        *authority = std::move(captured_authority);
    }
    return outcome;
}

void log_chart_prepare_outcome_best_effort(
    const ChartExpandPreparationOutcome outcome,
    void* wrapper, void* chart_row) noexcept
{
    try {
        static std::atomic_int s_logs{0};
        if (s_logs.fetch_add(1, std::memory_order_relaxed) >= 64) return;
        std::ostringstream out;
        out << "[chart_patch] row_prepare status="
            << (outcome == ChartExpandPreparationOutcome::CustomCommitted
                    ? "applied" : "skipped")
            << " reason="
            << (outcome == ChartExpandPreparationOutcome::CustomCommitted
                    ? "audio_committed" : "transaction_cancelled")
            << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper)
            << " chart_row=0x" << reinterpret_cast<uintptr_t>(chart_row)
            << std::dec;
        core::log(outcome == ChartExpandPreparationOutcome::CustomCommitted
                ? core::LogLevel::Debug : core::LogLevel::Error,
            out.str());
    } catch (...) {
    }
}

ChartExpandPreparationOutcome prepare_active_chart_row_patch_before_expand(
    void* wrapper, void* chart_row, uintptr_t caller_rva,
    ChartAudioDiagnosticTransaction* diagnostic,
    SelectionAudioAdmissionAuthority* authority) noexcept
{
    SelectionAudioAdmission admission{};
    if (diagnostic) *diagnostic = {};
    if (authority) *authority = {};
    try {
        const ChartExpandPreparationOutcome outcome
            = prepare_active_chart_row_patch_impl(
            wrapper, chart_row, caller_rva, admission, diagnostic, authority);
        log_chart_prepare_outcome_best_effort(outcome, wrapper, chart_row);
        if (diagnostic && diagnostic->generation != 0) {
            log_chart_audio_diagnostic_transaction("pre_write", *diagnostic);
        }
        return outcome;
    } catch (...) {
        const bool transaction_cancelled = restore_chart_and_cancel_audio(
            admission, "prepare_exception");
        const auto outcome = transaction_cancelled
            ? ChartExpandPreparationOutcome::NativePristine
            : ChartExpandPreparationOutcome::MutationUnresolved;
        if (outcome == ChartExpandPreparationOutcome::MutationUnresolved) {
            block_custom_audio_route_for_unresolved_chart_mutation();
        }
        if (diagnostic) {
            diagnostic->preparation_outcome = static_cast<uint8_t>(outcome);
            diagnostic->terminal_outcome = outcome
                == ChartExpandPreparationOutcome::MutationUnresolved
                ? ChartAudioDiagnosticTerminalOutcome::MutationUnresolved
                : ChartAudioDiagnosticTerminalOutcome::NativePristine;
            diagnostic->active = false;
            log_chart_audio_diagnostic_transaction("prepare_exception", *diagnostic);
        }
        return outcome;
    }
}

void finish_active_chart_row_patch_after_expand(
    void* wrapper, uintptr_t caller_rva) noexcept
{
    try {
    if (caller_rva != rva::PersistentChartExpandCaller || !wrapper) {
        return;
    }

    SelectionSnapshot selection;
    if (!restore_and_reset_chart_patch_state("expand_after_original", &selection)) {
        block_custom_audio_route_for_unresolved_chart_mutation();
        return;
    }

    std::vector<DescriptorChartRow> rows;
    if (!selection_chart_rows(selection, rows)) {
        return;
    }
    const SongDescriptor* const song = selection.song;

    const int32_t patched_count = static_cast<int32_t>(rows.size());
    int32_t copied_count = -1;
    if (!core::safe_read_field(wrapper,
            runtime_layouts::PianoScoreWrapper::copied_row_count, copied_count)) {
        return;
    }

#ifndef FF7RP_CHART_PATCH_SELFTEST
    const bool preserve_generalized_parser_count
        = extended_chart_parser_count_preservation_exact(wrapper, caller_rva);
#else
    const bool preserve_generalized_parser_count = false;
#endif
    if (!should_cap_wrapper_count_after_expand(
            copied_count, patched_count, preserve_generalized_parser_count)) return;

    const bool wrote = core::safe_write_field(
        wrapper, runtime_layouts::PianoScoreWrapper::copied_row_count, patched_count);
    if (!wrote) {
        return;
    }

    static std::atomic_int s_logs{0};
    const int log_index = s_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 32) {
        std::ostringstream out;
        out << "[chart_patch] wrapper_count_cap status=ok"
            << " wrapper=0x" << std::hex << reinterpret_cast<uintptr_t>(wrapper)
            << std::dec
            << " old_count=" << copied_count
            << " new_count=" << patched_count
            << " song_id=" << (song ? song->id : "?");
        core::log(core::LogLevel::Debug, out.str());
    }
    } catch (...) {
        // Original expansion already returned. The persistent chart journal is
        // restored before all allocation-capable finish diagnostics above.
    }
}

bool install_chart_patch_hooks(const HookInstallContext& context)
{
    if (!restore_and_reset_chart_patch_state("install_reset")) {
        core::log(core::LogLevel::Error,
            "[chart_patch] status=install_failed reason=retained_chart_mutation");
        return false;
    }
    activate_uobject_identity_chart_consumer();

    const uintptr_t exe_base = reinterpret_cast<uintptr_t>(context.exe_module);
    const uintptr_t chart_expand = exe_base && rva::PianoScoreExpand ? exe_base + rva::PianoScoreExpand : 0;
    const uintptr_t selected_entry_getter = exe_base && rva::SelectedEntryGetter ? exe_base + rva::SelectedEntryGetter : 0;
    const uintptr_t fname_ctor = exe_base && rva::FNameCtor ? exe_base + rva::FNameCtor : 0;
    const FNameCtorFn fname_ctor_fn = reinterpret_cast<FNameCtorFn>(fname_ctor);
    g_chart_fname_ctor = reinterpret_cast<FNameCtorFn>(fname_ctor);
    const ChartDescriptorReadiness readiness = descriptor_readiness(fname_ctor_fn);

    std::ostringstream out;
    out << "[chart_patch] status=pending_live_detour hooks=" << (kInstallLiveChartPatchHook ? "enabled" : "disabled")
        << " seam=descriptor_chart_rows"
        << " rollback=journal_ready"
        << " descriptor_chart_fields=" << (descriptor_chart_fields_available<SongDescriptor>() ? "available" : "missing")
        << " descriptors_with_chart_rows=" << readiness.descriptors_with_rows
        << " descriptors_resolvable=" << readiness.descriptors_resolvable
        << " chart_rows=" << readiness.rows
        << " unresolved_chart_rows=" << readiness.unresolved_rows
        << " custom_songs=" << registry().custom_count()
        << " chart_expand=0x" << std::hex << chart_expand
        << " selected_entry_getter=0x" << selected_entry_getter
        << " fname_ctor=0x" << fname_ctor
        << std::dec;
    core::log(core::LogLevel::Debug, out.str());

    core::log(core::LogLevel::Info,
        "[chart_patch] status=shared_expand_callback_live hooks=owned_by_note_count active_descriptor_handoff=prepared row_mutation=transient rollback=journal_ready");
    return true;
}

bool restore_chart_patch_for_shutdown()
{
    return restore_and_reset_chart_patch_state("shutdown");
}

core::HookShutdownResult shutdown_chart_patch()
{
    return core::shutdown_gated_hooks(non_audio_hook_gate(), {}, restore_chart_patch_for_shutdown, [] {
        g_chart_fname_ctor = nullptr;
        g_row_patch_wrapper.store(0, std::memory_order_relaxed);
        g_row_patch_next_index.store(0, std::memory_order_relaxed);
        g_cached_pianoscore_object.store(0, std::memory_order_relaxed);
        release_uobject_identity_chart_consumer();
    });
}
#endif

} // namespace ff7r::piano::game
