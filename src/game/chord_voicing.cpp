#include "game/chord_voicing.h"
#include "game/audio_sead.h"
#include "game/hook_specs.h"
#include "game/rvas.h"
#include "core/pe_image.h"
#include "core/logging.h"
#include "pipeline/chord_voicing.h"
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <limits>

extern "C" {
void* ff7rp_chord_copy_original = nullptr;
void ff7rp_chord_copy_bridge();
}

namespace ff7r::piano::game {
namespace {
using Callback = uint8_t(__fastcall*)(void*, float, void*);
Callback original_callback = nullptr;
core::RawRvaHook callback_hook, copy_hook;
core::HookCallbackGate chord_gate;
std::atomic_bool available{false};
uintptr_t callback_caller = 0, copy_caller = 0, chord_vtable = 0;
constexpr uintptr_t stride = 0x90;

template<class T> bool read(void* base, uintptr_t offset, T& out) noexcept
{ return core::safe_read_field(base, offset, out); }

bool controller_exact(const ChordVoicingBinding& binding) noexcept
{
    void *controller = nullptr, *chart = nullptr, *control = nullptr;
    return read(binding.wrapper, 0x118, controller) && controller == binding.controller
        && read(controller, 0xf48, chart) && chart == binding.wrapper
        && read(controller, 0xf50, control) && control == binding.control_block && control;
}

bool event_exact(const ChordVoicingBinding& binding, const NativeChartHeader& header,
    void* event, size_t& index) noexcept
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(event);
    if (!header.data || header.count <= 0 || header.capacity < header.count
        || static_cast<size_t>(header.count) != binding.plan.events.size()
        || header.data > UINTPTR_MAX - binding.plan.events.size() * stride
        || address < header.data || address >= header.data + binding.plan.events.size() * stride
        || (address - header.data) % stride) return false;
    index = (address - header.data) / stride;
    const auto& planned = binding.plan.events[index];
    void *chart = nullptr, *side = nullptr, *parent = nullptr, *successor = nullptr;
    uint32_t ordinal = 0;
    uint64_t name = 0;
    uint8_t selector = 0;
    const uintptr_t expected_parent = planned.parentless ? 0
        : header.data + planned.root_event_index * stride;
    const size_t next = binding.event_successors[index];
    const uintptr_t expected_next = next == ff7rp::pipeline::kNoChartEvent ? 0 : header.data + next * stride;
    const bool exact = read(event, 0, chart) && chart == binding.wrapper
        && read(event, 8, side)
        && side == (planned.hand == ff7rp::pipeline::ChartEventHand::Left ? binding.left : binding.right)
        && read(event, 0x10, parent) && reinterpret_cast<uintptr_t>(parent) == expected_parent
        && read(event, 0x18, successor) && reinterpret_cast<uintptr_t>(successor) == expected_next
        && read(event, 0x20, ordinal) && ordinal == planned.ordinal
        && read(event, 0x2c, name) && name == binding.event_names[index]
        && read(event, 0x49, selector) && selector == static_cast<uint8_t>(planned.kind);
    if (!exact || planned.parentless) return exact;
    size_t root_index = 0;
    return event_exact(binding, header, parent, root_index) && root_index == planned.root_event_index;
}

uintptr_t native_callback_entry = 0;

struct Frame {
    Frame* previous = nullptr;
    std::shared_ptr<const ChordVoicingBinding> binding;
    void* owner = nullptr;
    void* event = nullptr;
    const ResolvedChordVoicing* voicing = nullptr;
    ChordProjection projection;
};
thread_local Frame* current_frame = nullptr;
struct FrameScope {
    Frame& frame;
    explicit FrameScope(Frame& value) : frame(value) {
        frame.previous = current_frame; current_frame = &frame;
    }
    ~FrameScope() { current_frame = frame.previous; }
};

bool callback_exact(const ChartAdmissionSnapshot& chart, Frame& frame) noexcept
{
    NativeChartHeader fresh;
    size_t index = 0;
    const auto& binding = *frame.binding;
    uintptr_t table = 0, target = 0, entries = 0;
    int32_t count = 0, selected = -1;
    void* owner = nullptr;
    return read(binding.wrapper, 0x80, fresh) && fresh == chart.header
        && controller_exact(binding) && event_exact(binding, fresh, frame.event, index)
        && binding.plan.events[index].kind == ff7rp::pipeline::ChartEventKind::Chord
        && read(binding.left, 0, entries) && entries
        && read(binding.left, 8, count) && count > 0
        && read(binding.left, 0x1a0, selected) && selected >= 0 && selected < count
        && entries <= UINTPTR_MAX - static_cast<uintptr_t>(selected) * 16
        && read(reinterpret_cast<void*>(entries), static_cast<uintptr_t>(selected) * 16, owner)
        && owner == frame.owner && read(owner, 0, table) && table == chord_vtable
        && read(reinterpret_cast<void*>(table), 0x20, target) && target == native_callback_entry;
}

uint8_t invoke_chord_callback(void* owner, float tempo, void* event, bool charted)
{
    Frame frame;
    FrameScope scope(frame); // Even stock/nested-ineligible calls shadow outer authorization.
    if (charted) {
        const auto playback = registry().playback_snapshot();
        frame.binding = playback.chart_admission.binding;
        frame.owner = owner; frame.event = event;
        const auto& admitted = playback.chart_admission.header;
        const uintptr_t address = reinterpret_cast<uintptr_t>(event);
        // Classify only a member of the CURRENT sealed allocation, using its
        // immutable planned chord ID. Corrupted native IDs/backlinks/header must
        // not turn a known override event into an unrelated stock callback.
        if (frame.binding && admitted.data && address >= admitted.data
            && (address - admitted.data) % stride == 0) {
            const size_t index = (address - admitted.data) / stride;
            if (index < frame.binding->plan.events.size()
                && frame.binding->plan.events[index].kind == ff7rp::pipeline::ChartEventKind::Chord) {
                for (const auto& voicing : frame.binding->voicings) {
                    if (voicing.chord == frame.binding->event_names[index]) {
                        frame.voicing = &voicing;
                        break;
                    }
                }
            }
        }
        if (frame.voicing && !registry().commit_if_current_chart(frame.binding,
                +[](const ChartAdmissionSnapshot& chart, void* context) noexcept {
                    return callback_exact(chart, *static_cast<Frame*>(context));
                }, &frame)) {
            fail_chord_voicing_chart(frame.binding);
            return 0; // Known custom emission denied, never silently stock.
        }
    }
    return original_callback(owner, tempo, event);
}

uint8_t __fastcall chord_callback(void* owner, float tempo, void* event)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto lease = non_audio_hook_gate().try_enter();
    auto chord_lease = chord_gate.try_enter();
    return invoke_chord_callback(owner, tempo, event,
        lease && chord_lease && available.load(std::memory_order_acquire) && caller == callback_caller);
}

struct ProjectionCall { Frame* frame; void* source; };
bool commit_projection(const ChartAdmissionSnapshot& chart, void* opaque) noexcept
{
    auto& call = *static_cast<ProjectionCall*>(opaque);
    Frame& frame = *call.frame;
    if (!callback_exact(chart, frame)) return false;
    NativeChartHeader span;
    std::array<ChordVoice, 5> stock{};
    uint64_t source_id = 0;
    return read(call.source, 0, source_id) && source_id == frame.voicing->chord
        && read(call.source, 0x28, span) && span.data && span.count > 0 && span.count <= 5
        && span.capacity >= span.count
        && core::safe_copy_bytes(reinterpret_cast<void*>(span.data), stock.data(),
            static_cast<size_t>(span.count) * sizeof(ChordVoice))
        && project_chord_slots(*frame.voicing, stock.data(), span.count, frame.projection);
}
} // namespace

extern "C" void* ff7rp_chord_projection(void* dst, void* src, void* caller) noexcept
{
    Frame* frame = current_frame;
    if (!frame || !frame->voicing || reinterpret_cast<uintptr_t>(caller) != copy_caller
        || reinterpret_cast<uintptr_t>(dst) != reinterpret_cast<uintptr_t>(frame->owner) + 0xf8)
        return nullptr;
    ProjectionCall call{frame, src};
    if (registry().commit_if_current_chart(frame->binding, commit_projection, &call))
        return &frame->projection;
    // A violated native invariant is not faithful stock fallback for this chart.
    // Retire authority and use an empty local span for this synchronous emission.
    fail_chord_voicing_chart(frame->binding);
    frame->projection.data = frame->projection.voices.data();
    frame->projection.count = frame->projection.capacity = 0;
    return &frame->projection;
}

extern "C" void* ff7rp_chord_copy_dispatch(
    void* dst, void* src, void* caller, void** projection)
{
    // Account stock callers too: shared-helper teardown disables entry before
    // closing/draining this gate. Native calls never run under registry mutex.
    auto lease = chord_gate.try_enter();
    using Copier = void* (__fastcall*)(void*, void*);
    void* result = reinterpret_cast<Copier>(ff7rp_chord_copy_original)(dst, src);
    *projection = ff7rp_chord_projection(dst, src, caller);
    return result;
}

std::shared_ptr<ChordVoicingBinding> preflight_chord_voicing(
    const SelectionSnapshot& selection, void* wrapper, ChordNameResolver resolve)
{
    if (!available.load(std::memory_order_acquire) || !selection || !selection.storage
        || !resolve || !wrapper) return {};
    auto binding = std::make_shared<ChordVoicingBinding>();
    binding->selection = selection;
    binding->wrapper = wrapper;
    if (!read(wrapper, 0x118, binding->controller) || !binding->controller
        || !read(binding->controller, 0xf50, binding->control_block)
        || !controller_exact(*binding)) return {};
    binding->left = static_cast<uint8_t*>(binding->controller) + 0x380;
    binding->right = static_cast<uint8_t*>(binding->controller) + 0x538;
    std::vector<ff7rp::pipeline::ChartEventRow> rows;
    const auto& profile = *selection.profile;
    if (!profile.extended_chart_tail_notes.empty()
        && !ff7rp::pipeline::playable_extended_transport_available()) return {};
    rows.reserve(profile.chart_notes.size() + profile.extended_chart_tail_notes.size());
    for (const auto& row : profile.chart_notes)
        rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(row));
    for (const auto& row : profile.extended_chart_tail_notes)
        rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(row));
    binding->plan = ff7rp::pipeline::derive_chart_event_plan(rows);
    if (!binding->plan.valid()) return {};
    binding->event_successors.assign(binding->plan.events.size(), ff7rp::pipeline::kNoChartEvent);
    // Native link inserts each follower at its root's head (same canonical
    // ordering used by the existing extended-chart link journal).
    for (const auto& link : binding->plan.links) {
        binding->event_successors[link.child_event_index] = binding->event_successors[link.root_event_index];
        binding->event_successors[link.root_event_index] = link.child_event_index;
    }
    binding->event_names.reserve(binding->plan.events.size());
    for (const auto& event : binding->plan.events) {
        const auto& row = rows[event.source_row_index];
        uint64_t name = 0;
        if (!resolve(event.kind == ff7rp::pipeline::ChartEventKind::Chord
                ? row.chord_id : row.monotone_id, name)) return {};
        binding->event_names.push_back(name);
    }
    const auto assets = ff7rp::pipeline::selected_native_asset_capabilities();
    if (!assets.has_verified_authored_chord_voicing()) return {};
    binding->voicings.reserve(selection.song->chord_voicings.size());
    for (const auto& authored : selection.song->chord_voicings) {
        const auto* stock = ff7rp::pipeline::find_verified_native_chord(authored.chord_id, assets);
        if (!stock || authored.sound_ids.empty() || authored.sound_ids.size() > stock->sound_count
            || authored.sound_ids.size() > 5) return {};
        ResolvedChordVoicing value;
        value.count = static_cast<uint32_t>(authored.sound_ids.size());
        value.stock_width = stock->sound_count;
        if (!resolve(authored.chord_id, value.chord)) return {};
        for (size_t i = 0; i < value.count; ++i)
            if (!resolve(authored.sound_ids[i], value.sounds[i])) return {};
        binding->voicings.push_back(value);
    }
    return binding;
}

void finish_chord_voicing_expansion(void* wrapper, bool complete) noexcept
{
    const auto chart = registry().chart_admission_for_wrapper(wrapper);
    if (!chart.binding || chart.state != ChartAdmissionState::Pending) return;
    NativeChartHeader header;
    bool valid = complete && controller_exact(*chart.binding) && read(wrapper, 0x80, header)
        && header.count > 0 && header.capacity >= header.count
        && static_cast<size_t>(header.count) == chart.binding->plan.events.size();
    for (size_t i = 0; valid && i < chart.binding->plan.events.size(); ++i) {
        size_t observed = 0;
        valid = event_exact(*chart.binding, header,
            reinterpret_cast<void*>(header.data + i * stride), observed) && observed == i;
    }
    if (!valid || !registry().seal_chart_admission(chart.binding, header))
        fail_chord_voicing_chart(chart.binding);
}

bool chord_voicing_chart_update_allowed(void* wrapper) noexcept
{
    const auto decision = registry().chart_update_admission(wrapper);
    if (decision == ChartUpdateAdmission::RejectedFirst)
        core::log(core::LogLevel::Error,
            "[chord_voicing] status=chart_not_ready input=rejected original_calls=0");
    return decision == ChartUpdateAdmission::Stock || decision == ChartUpdateAdmission::Ready;
}

bool install_chord_voicing_hooks(const HookInstallContext& context)
{
    available.store(false, std::memory_order_release);
    const auto* outer = find_rva_signature("piano_chord_callback");
    const auto* copier = find_rva_signature("piano_chord_cached_copy");
    const auto* caller = find_rva_signature("piano_chord_copy_caller");
    const auto* terminal = find_rva_signature("piano_chord_terminal_caller");
    const auto* span = find_rva_signature("piano_chord_span_loads");
    if (!outer || !copier || !caller || !terminal || !span) return true; // Feature absent on 1.004.
    const uintptr_t base = reinterpret_cast<uintptr_t>(context.exe_module);
    const auto image = core::image_range(context.exe_module);
    for (const auto* spec : {outer, copier, caller, terminal, span}) {
        if (!spec->rva || spec->expected_prologue.empty() || spec->rva >= image.size
            || spec->expected_prologue.size() > image.size - spec->rva
            || !core::bytes_equal(reinterpret_cast<uint8_t*>(base + spec->rva), spec->expected_prologue))
            return true; // Stock songs remain usable; authored admission rejects.
    }
    if (!rva::PianoChordModeVtable || rva::PianoChordModeVtable + 0x28 > image.size) return true;
    chord_vtable = base + rva::PianoChordModeVtable;
    native_callback_entry = base + outer->rva;
    uintptr_t target = 0;
    if (!read(reinterpret_cast<void*>(chord_vtable), 0x20, target) || target != native_callback_entry)
        return true;
    callback_caller = base + terminal->rva + 0x18;
    copy_caller = base + caller->rva + 0x0f;
    std::string error;
    chord_gate.open();
    if (!callback_hook.install(context.exe_module, outer->rva, outer->expected_prologue,
            reinterpret_cast<void*>(&chord_callback), reinterpret_cast<void**>(&original_callback), error)
        || !copy_hook.install(context.exe_module, copier->rva, copier->expected_prologue,
            reinterpret_cast<void*>(&ff7rp_chord_copy_bridge), &ff7rp_chord_copy_original, error)) {
        return shutdown_chord_voicing().ok();
    }
    available.store(true, std::memory_order_release);
    return true;
}

core::HookShutdownResult shutdown_chord_voicing()
{
    available.store(false, std::memory_order_release);
    bool disabled = false;
    const bool drained = core::disable_then_close_and_drain(chord_gate, [&] {
        const bool copy_disabled = copy_hook.disable();
        const bool callback_disabled = callback_hook.disable();
        return disabled = copy_disabled && callback_disabled;
    });
    if (!drained) return {disabled, disabled, false, false, false, false};
    const bool copy_removed = copy_hook.remove();
    const bool callback_removed = callback_hook.remove();
    const bool removed = copy_removed && callback_removed;
    if (removed) { original_callback = nullptr; ff7rp_chord_copy_original = nullptr; }
    return {true, true, true, true, removed, removed};
}

#ifdef FF7RP_CHORD_VOICING_SELFTEST
bool chord_voicing_callback_selftest(void* owner, void* event, bool charted, bool expect_original)
{
    static int calls;
    calls = 0;
    const auto saved = original_callback;
    original_callback = +[](void*, float, void*) -> uint8_t { ++calls; return 0x5a; };
    const auto result = invoke_chord_callback(owner, 1.25f, event, charted);
    original_callback = saved;
    return calls == (expect_original ? 1 : 0) && result == (expect_original ? 0x5a : 0);
}

bool chord_voicing_scope_selftest()
{
    static int original_calls = 0;
    static bool original_args = false;
    original_calls = 0; original_args = false;
    const auto saved_original = original_callback;
    original_callback = +[](void* owner, float tempo, void* event) -> uint8_t {
        ++original_calls;
        original_args = owner == reinterpret_cast<void*>(1) && tempo == 1.25f
            && event == reinterpret_cast<void*>(2) && current_frame && !current_frame->voicing;
        return 0x5a;
    };
    Frame outer;
    ResolvedChordVoicing voicing;
    outer.voicing = &voicing;
    FrameScope first(outer);
    {
        Frame stock;
        FrameScope nested(stock);
        if (current_frame != &stock || current_frame->voicing) return false;
    }
    const auto result = chord_callback(reinterpret_cast<void*>(1), 1.25f, reinterpret_cast<void*>(2));
    original_callback = saved_original;
    return current_frame == &outer && current_frame->voicing == &voicing
        && original_calls == 1 && original_args && result == 0x5a;
}

uintptr_t chord_voicing_bridge_selftest_scope(const PlaybackSnapshot& playback, void* owner,
    void* event, uintptr_t caller, uintptr_t vtable, uintptr_t entry,
    void (*probe)(void*, void*, void*), void* source, void* result)
{
    copy_caller = caller; chord_vtable = vtable; native_callback_entry = entry;
    Frame frame;
    FrameScope scope(frame);
    frame.binding = playback.chart_admission.binding;
    if (!frame.binding || frame.binding->voicings.empty()
        || playback.chart_admission.state != ChartAdmissionState::Ready) return 0;
    frame.owner = owner; frame.event = event;
    frame.voicing = &frame.binding->voicings.front();
    probe(static_cast<uint8_t*>(owner) + 0xf8, source, result);
    // Identity comparison only; caller must never dereference the expired span.
    return reinterpret_cast<uintptr_t>(&frame.projection);
}
#endif
} // namespace ff7r::piano::game
