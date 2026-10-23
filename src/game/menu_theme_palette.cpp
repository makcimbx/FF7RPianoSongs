#include "game/menu_theme_palette.h"
#include "game/menu_theme_metrics.h"

#include "core/pe_image.h"
#include "core/version_gate.h"
#include "game/hook_specs.h"
#include "game/module_hooks.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <intrin.h>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ff7r::piano::game::menu_theme_palette {
namespace perf = menu_theme_metrics;
namespace {
using Linear = std::array<float, 4>;
struct alignas(8) NativeColor { std::array<unsigned char, 40> bytes{}; };
static_assert(sizeof(NativeColor) == 40);
struct alignas(8) NativeAttribute { std::array<unsigned char, 0x40> bytes{}; };
struct SlatePair { void* slate = nullptr; void* controller = nullptr; };
using Copy = void* (__fastcall*)(void*, const void*);
using Destroy = void (__fastcall*)(void*);
using Setter = void (__fastcall*)(void*, void*);
using Opacity = void (__fastcall*)(void*, float);
struct Contract { const char* id; Property property; std::size_t offset; bool owning; bool opacity; };
constexpr Contract contracts[] = {
    {"end_image_set_color", Property::Image, 0x1a0, false, false},
    {"end_pie_image_set_color", Property::PieImage, 0x1a0, false, false},
    {"end_tiled_image_set_color", Property::TiledImage, 0x1a8, false, false},
    {"end_image_set_set_color", Property::ImageSet, 0x118, false, false},
    {"end_button_set_color", Property::Button, 0x398, false, false},
    {"end_button_set_background_color", Property::ButtonBackground, 0x3a8, false, false},
    {"end_canvas_panel_set_content_color", Property::PanelContent, 0x120, false, false},
    {"end_text_block_set_color", Property::Text, 0x148, true, false},
    {"end_text_block_set_bottom_color", Property::TextBottom, 0x188, true, false},
    {"end_text_block_set_outline_color", Property::TextOutline, 0x1c0, false, false},
    {"end_text_block_set_shadow_color", Property::TextShadow, 0x1f0, false, false},
    {"end_image_set_opacity", Property::Image, 0x1a0, false, true},
    {"end_pie_image_set_opacity", Property::PieImage, 0x1a0, false, true},
    {"end_tiled_image_set_opacity", Property::TiledImage, 0x1a8, false, true},
    {"end_image_set_set_opacity", Property::ImageSet, 0x118, false, true},
    {"end_text_block_set_opacity", Property::Text, 0x148, true, true},
    {"end_text_block_set_bottom_opacity", Property::TextBottom, 0x188, true, true},
};
constexpr std::size_t kMethods = std::size(contracts);
constexpr std::size_t index(Property p) { return static_cast<std::size_t>(p); }
struct Source {
    Subject subject{};
    Policy policy{};
    NativeColor native{};
    bool occupied = false, active = false, initialized = false;
    bool overridden = false; // Only an applied override requires an explicit restoring setter.
    bool property_source_current = false; // Never recapture an uncertain temporary presentation.
    NativeAttribute bottom_attribute{};
    SlatePair bottom_slate{}; // Identity facts only; the validated widget retains this pair.
    bool attribute_initialized = false, attribute_current = false;
    // Routing is mutex-protected even for rejected foreign-thread callbacks.
    void* routed_widget = nullptr;
    Property routed_property = Property::Image;
    std::atomic<std::uint64_t> updates{0};
    std::atomic<std::uint64_t> forwarding{0};
    std::uint64_t captured = 0; // Owner-thread only; inequality means stale.
    std::uint64_t sequence = 0; // Qualified source changes, separate from forwarding revisions.
};
struct State {
    // Only publication/lookups use this lock. Never held across native calls.
    std::mutex mutex;
    core::HookCallbackGate callbacks;
    std::array<core::RawRvaHook, kMethods> hooks;
    std::array<const RvaSignatureSpec*, kMethods> specs{};
    std::array<void*, kMethods> originals{};
    // Heap nodes never move or erase; vector growth moves only their pointers.
    std::vector<std::unique_ptr<Source>> sources;
    Copy copy = nullptr, assign = nullptr;
    Destroy destroy = nullptr;
    Copy attribute_assign = nullptr;
    Destroy attribute_destroy = nullptr;
    Setter bottom_attribute_setter = nullptr; // Borrowed attribute, never consumed.
    HMODULE module = nullptr;
    CallbackReader reader{};
    std::atomic<DWORD> thread{0};
    bool configured = false, installed = false;
    std::atomic<bool> poisoned{false};
    Result failure{};
};
State state; // Byte storage only: NEVER native-owning static destructors.
thread_local bool entered = false;
struct Entry { bool previous = entered; Entry() { entered = true; } ~Entry() { entered = previous; } };
struct NestedOpacity { Source* source = nullptr; void* widget; Property property; Linear untinted{}; std::uint64_t captured; bool observed = false; };
thread_local NestedOpacity* opacity_scope = nullptr;
thread_local const NativeInvocation* invocation = nullptr;
thread_local std::uint64_t invocation_serial = 0;
thread_local std::uint64_t proof_boundary = 0;
void end_proof_phase() noexcept {
    if (proof_boundary != std::numeric_limits<std::uint64_t>::max()) ++proof_boundary;
}
struct ProofPhase;
thread_local ProofPhase* proof_phase = nullptr;
struct ProofPhase {
    ProofPhase* previous = proof_phase;
    const Authority* authority = nullptr;
    const Source* source = nullptr;
    Subject subject{};
    std::uint64_t boundary = 0;
    ProofPhase() noexcept { end_proof_phase(); proof_phase = this; }
    ~ProofPhase() { end_proof_phase(); proof_phase = previous; }
    bool current(const Authority& a) const noexcept {
        return authority == &a && boundary == proof_boundary
            && boundary != std::numeric_limits<std::uint64_t>::max();
    }
};
struct InvocationScope {
    NativeInvocation value;
    const NativeInvocation* previous = invocation;
    InvocationScope(void* slot, void* caller, std::size_t method, void* widget) noexcept
        : value{slot, caller, widget, contracts[method].property, contracts[method].opacity, GetCurrentThreadId()} {
        end_proof_phase();
        if (invocation_serial != std::numeric_limits<std::uint64_t>::max()) value.serial = ++invocation_serial;
        invocation = value.serial ? &value : nullptr;
    }
    ~InvocationScope() { end_proof_phase(); invocation = previous; }
};

Result fail(Outcome outcome, const char* stage) noexcept
{
    std::lock_guard lock(state.mutex);
    return state.failure = {outcome, stage};
}
bool admitted(const Authority& a) noexcept
{
    const auto cheap = [&]() noexcept { return !state.poisoned.load() && a.game_thread_id != 0
        && a.game_thread_id == GetCurrentThreadId()
        && a.session.generation && a.session.widget
        && (a.session.phase == MenuSessionPhase::Ready || a.session.phase == MenuSessionPhase::Closing)
        && a.qualify_current_callback
        && menu_session_matches(a.session.generation, a.session.widget, a.session.widget_identity,
            a.session.phase == MenuSessionPhase::Ready)
        && (!a.phase_current || a.phase_current(a.context, a.session)); };
    if (!cheap()) {
        if (proof_phase) { proof_phase->authority = nullptr; proof_phase->source = nullptr; }
        return false;
    }
    if (a.phase_current && proof_phase && proof_phase->current(a)) return true;
    const auto boundary = proof_boundary;
    if (!a.qualify_current_callback(a.context, a.session) || boundary != proof_boundary || !cheap()) {
        if (proof_phase) { proof_phase->authority = nullptr; proof_phase->source = nullptr; }
        return false;
    }
    if (a.phase_current && proof_phase && boundary == proof_boundary) {
        proof_phase->authority = &a; proof_phase->boundary = boundary; proof_phase->source = nullptr;
    }
    return true;
}
bool same(const Subject&, const Subject&) noexcept;
bool valid(const Source& s, const Authority& a) noexcept
{
    if (s.active && admitted(a) && s.active && s.subject.owner == a.session.widget
        && s.subject.generation == a.session.generation && s.subject.validate) {
        if (a.phase_current && proof_phase && proof_phase->current(a)
            && proof_phase->source == &s && same(proof_phase->subject, s.subject)) return true;
    } else { perf::count(perf::Counter::PaletteRejected); return false; }
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::ValidatePalette);
    const auto boundary = proof_boundary;
    const bool result = s.subject.validate(s.subject.context, s.subject, a.session) && s.active
        && boundary == proof_boundary && (!a.phase_current || a.phase_current(a.context, a.session));
    if (result && a.phase_current && proof_phase && proof_phase->current(a) && boundary == proof_boundary) {
        proof_phase->source = &s; proof_phase->subject = s.subject;
    }
    perf::finish(stamp, result);
    if (!result) perf::count(perf::Counter::PaletteRejected);
    return result;
}
bool same(const Subject& a, const Subject& b) noexcept
{
    return a.widget == b.widget && a.owner == b.owner && a.generation == b.generation
        && a.property == b.property && a.context == b.context && a.validate == b.validate;
}
Source* find(void* widget, Property p) noexcept
{
    std::lock_guard lock(state.mutex);
    for (auto& record : state.sources) {
        perf::count(perf::Counter::PaletteRouteVisits);
        auto& s = *record;
        if (s.routed_widget == widget && s.routed_property == p) return &s;
    }
    return nullptr;
}
struct Forward {
    Source* source = nullptr;
    Forward(void* widget, Property p) noexcept {
        perf::count(perf::Counter::PaletteForwarded);
        std::lock_guard lock(state.mutex);
        for (auto& record : state.sources) {
            perf::count(perf::Counter::PaletteRouteVisits);
            auto& s = *record;
            if (s.routed_widget == widget && s.routed_property == p) {
                source = &s;
                s.forwarding.fetch_add(1);
                s.updates.fetch_add(1);
                break;
            }
        }
    }
    ~Forward() {
        if (source) {
            // Completion must invalidate captures taken while Original was pending.
            source->updates.fetch_add(1);
            source->forwarding.fetch_sub(1);
        }
    }
};
void detach(Source& s) noexcept
{
    std::lock_guard lock(state.mutex);
    s.routed_widget = nullptr; s.active = false; s.subject = {};
}
bool fresh(const Source& s) noexcept {
    return s.captured == s.updates.load() && s.forwarding.load() == 0
        && s.captured == s.updates.load();
}
bool policy_valid(const Policy& p) noexcept
{
    for (const auto* c : {&p.normal, &p.selected})
        for (float f : c->linear) if (!std::isfinite(f) || f < 0 || f > 1) return false;
    return true;
}
Linear rgba(const NativeColor& c) noexcept
{
    Linear out; std::memcpy(out.data(), c.bytes.data(), sizeof(out)); return out;
}
bool source_rgba(const Source& s, Linear& out) noexcept
{
    // SlateColor's retained shared FLinearColor is also the branch used by the
    // observed text-opacity setter; its inline specified color may be stale.
    void* shared = nullptr;
    if (contracts[index(s.subject.property)].owning)
        std::memcpy(&shared, s.native.bytes.data() + 0x18, sizeof(shared));
    if (shared) return core::safe_copy_bytes(shared, &out, sizeof(out));
    out = rgba(s.native); return true;
}
NativeColor constant(const Linear& value) noexcept
{
    NativeColor out{}; // mode 0 = specified color; no shared controller.
    std::memcpy(out.bytes.data(), value.data(), sizeof(value)); return out;
}
Linear themed(const Source& s, Linear source) noexcept
{
    const auto& c = s.policy.is_selected ? s.policy.selected : s.policy.normal;
    if (c.enabled) {
        for (unsigned i = 0; i < 3; ++i) source[i] = c.linear[i];
        source[3] *= c.linear[3];
    }
    return source;
}
// SEH wrappers contain no C++ unwinding objects. Never retry an uncertain native call.
bool owning(perf::Metric metric, Copy f, void* dst, const void* src) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, metric);
    end_proof_phase();
    __try { f(dst, src); end_proof_phase(); perf::finish(stamp); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { end_proof_phase(); perf::finish(stamp, false); return false; }
}
bool destroy(void* p) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::NativeColorDestroy);
    end_proof_phase();
    __try { state.destroy(p); end_proof_phase(); perf::finish(stamp); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { end_proof_phase(); perf::finish(stamp, false); return false; }
}
bool destroy_attribute(void* p) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::NativeBottomDestroy);
    end_proof_phase();
    __try { state.attribute_destroy(p); end_proof_phase(); perf::finish(stamp); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { end_proof_phase(); perf::finish(stamp, false); return false; }
}
bool set_bottom_attribute(void* slate, void* attribute) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::NativeBottomSetter);
    end_proof_phase();
    __try { state.bottom_attribute_setter(slate, attribute); end_proof_phase(); perf::finish(stamp); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { end_proof_phase(); perf::finish(stamp, false); return false; }
}
bool call(std::size_t method, void* widget, void* input) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::NativeColorSetter);
    end_proof_phase();
    __try { reinterpret_cast<Setter>(state.originals[method])(widget, input); end_proof_phase(); perf::finish(stamp); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { end_proof_phase(); perf::finish(stamp, false); return false; }
}
bool call_opacity(std::size_t method, void* widget, float input) noexcept
{
    perf::Stamp stamp{}; perf::begin(stamp, perf::Metric::NativeOpacitySetter);
    end_proof_phase();
    __try { reinterpret_cast<Opacity>(state.originals[method])(widget, input); end_proof_phase(); perf::finish(stamp); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { end_proof_phase(); perf::finish(stamp, false); return false; }
}
void fault(const char* stage) noexcept
{
    state.poisoned.store(true); fail(Outcome::NativeFault, stage);
}
bool save(Source& s, const void* value) noexcept
{
    const auto update = s.updates.load();
    if (s.forwarding.load()) { fail(Outcome::Unavailable, "source_forwarding"); return false; }
    if (s.sequence == (std::numeric_limits<std::uint64_t>::max)()) { fail(Outcome::Budget, "source_sequence"); return false; }
    ++s.sequence;
    if (contracts[index(s.subject.property)].owning) {
        // The slot is reserved before construction; a fault retains uncertain ownership.
        const bool initialized = s.initialized;
        s.initialized = true;
        if (!owning(initialized ? perf::Metric::NativeColorAssign : perf::Metric::NativeColorCopy,
            initialized ? state.assign : state.copy, &s.native, value)) {
            fault("source_owning_copy"); return false;
        }
    } else if (!core::safe_copy_bytes(value, s.native.bytes.data(), sizeof(Linear))) {
        fail(Outcome::Memory, "source_read"); return false;
    } else s.initialized = true;
    if (s.forwarding.load() || s.updates.load() != update) {
        fail(Outcome::Unavailable, "source_capture_raced_forward"); return false;
    }
    s.captured = update;
    perf::count(perf::Counter::SourceCaptures);
    return true;
}
bool read_bottom_pair(const Source& s, const Authority& a, SlatePair& pair) noexcept
{
    if (!valid(s, a)) { fail(Outcome::Authority, "bottom_subject"); return false; }
    if (!core::safe_copy_bytes(static_cast<unsigned char*>(s.subject.widget) + 0x220, &pair, sizeof(pair))
        || !pair.slate || !pair.controller) { fail(Outcome::Identity, "bottom_slate_pair"); return false; }
    if (!valid(s, a)) { fail(Outcome::Authority, "bottom_post_read_authority"); return false; }
    return true;
}
bool bottom_pair_matches(const Source& s, const Authority& a, const SlatePair& expected) noexcept
{
    SlatePair current{};
    if (!read_bottom_pair(s, a, current)) return false;
    if (current.slate != expected.slate || current.controller != expected.controller) {
        fail(Outcome::Identity, "bottom_slate_drift"); return false;
    }
    if (!fresh(s)) { fail(Outcome::Unavailable, "bottom_source_stale"); return false; }
    return true;
}
bool capture_bottom_attribute(Source& s, const Authority& a, const SlatePair& pair, bool native_input) noexcept
{
    perf::Span metric(perf::Metric::NativeBottomCapture);
    s.attribute_current = false;
    if (!bottom_pair_matches(s, a, pair)) return false;
    // Zero attribute is initialized/unset. Native assignment retains the color and
    // clones delegates; uncertain ownership after a fault stays reserved.
    if (!s.attribute_initialized) s.bottom_attribute = NativeAttribute{}; // Reconstruct destroyed/unowned storage only.
    s.attribute_initialized = true;
    if (!owning(perf::Metric::NativeBottomAssign, state.attribute_assign, &s.bottom_attribute, static_cast<unsigned char*>(pair.slate) + 0x340)) {
        fault("bottom_attribute_assign"); return false;
    }
    if (!bottom_pair_matches(s, a, pair)) return false;
    // A real bottom setter established the latest native set/unbound state, but
    // received themed RGB. Replace only its owned color via native assignment.
    if (native_input && !owning(perf::Metric::NativeColorAssign, state.assign, &s.bottom_attribute, &s.native)) {
        fault("bottom_attribute_source_color"); return false;
    }
    if (!bottom_pair_matches(s, a, pair)) return false;
    s.bottom_slate = pair;
    s.attribute_current = true;
    return true;
}
bool restore_bottom(Source& s, const Authority& a) noexcept
{
    if (!s.attribute_current) { fail(Outcome::Unavailable, "bottom_attribute_stale"); return false; }
    if (!bottom_pair_matches(s, a, s.bottom_slate)) return false;
    // The UObject setter would force bIsSet=1 and erase an original unset/delegate.
    if (!owning(perf::Metric::NativeColorAssign, state.assign, static_cast<unsigned char*>(s.subject.widget) + 0x188, &s.native)) {
        fault("bottom_object_restore"); return false;
    }
    if (!bottom_pair_matches(s, a, s.bottom_slate)) return false;
    if (!set_bottom_attribute(s.bottom_slate.slate, &s.bottom_attribute)) {
        fault("bottom_attribute_restore"); return false;
    }
    if (!bottom_pair_matches(s, a, s.bottom_slate)) return false;
    s.overridden = false;
    return true;
}
bool projection_current(const Source& s, const Authority& a, std::uint64_t sequence) noexcept
{
    if (!valid(s, a)) { fail(Outcome::Authority, "projection_subject"); return false; }
    if (!fresh(s) || s.sequence != sequence) { fail(Outcome::Unavailable, "projection_superseded"); return false; }
    if (s.subject.property == Property::TextBottom && (!s.attribute_current || !bottom_pair_matches(s, a, s.bottom_slate))) {
        fail(Outcome::Identity, "projection_bottom_pair"); return false;
    }
    return true;
}
bool project_source(Source& s, const Authority& a, std::uint64_t sequence) noexcept
{
    perf::Span metric(perf::Metric::ProjectSource);
    if (!projection_current(s, a, sequence)) return false;
    const auto& c = contracts[index(s.subject.property)];
    auto* property = static_cast<unsigned char*>(s.subject.widget) + c.offset;
    // Slate owns the presentation. MovieScene must read the genuine property,
    // not feed our last presentation RGB back into its next evaluation.
    s.property_source_current = false;
    if (c.owning) {
        if (!owning(perf::Metric::NativeColorAssign, state.assign, property, &s.native)) { fault("projection_owning_assign"); return false; }
    } else if (!core::safe_write_field(property, 0, rgba(s.native))) {
        s.updates.fetch_add(1); fail(Outcome::Memory, "projection_write"); return false;
    }
    if (!projection_current(s, a, sequence)) return false;
    s.property_source_current = true;
    return true;
}
bool emit(Source& s, const Authority& a, bool tint) noexcept
{
    perf::Span metric(perf::Metric::PaletteEmit);
    if (!valid(s, a)) { fail(Outcome::Identity, "emit_subject"); return false; }
    if (!fresh(s)) { fail(Outcome::Unavailable, "source_stale"); return false; }
    const auto method = index(s.subject.property);
    NativeColor value{};
    Linear source{};
    const bool enabled = tint && (s.policy.is_selected ? s.policy.selected.enabled : s.policy.normal.enabled);
    if (!contracts[method].owning && !s.property_source_current) {
        fail(Outcome::Unavailable, "property_source_uncertain"); return false;
    }
    // Even unchanged native setters can enable effects (notably bottom-text gradients).
    if (!enabled && !s.overridden) { perf::count(perf::Counter::PolicyNoops); return true; }
    if (!contracts[method].owning) {
        // Native setup can write LinearColor directly, without an intercepted setter.
        const auto captured = s.captured;
        s.property_source_current = false;
        const bool saved = save(s, static_cast<unsigned char*>(s.subject.widget) + contracts[method].offset);
        s.captured = captured; // A property read cannot recover a forwarded source.
        if (!saved || !projection_current(s, a, s.sequence)) return false;
        perf::count(perf::Counter::SourceRecaptures);
        s.property_source_current = true;
    }
    const auto sequence = s.sequence;
    const bool bottom = s.subject.property == Property::TextBottom;
    if (bottom) {
        if (!enabled) return restore_bottom(s, a);
        if (!s.attribute_current) {
            if (s.overridden) { fail(Outcome::Unavailable, "bottom_attribute_stale"); return false; }
            SlatePair pair{};
            if (!read_bottom_pair(s, a, pair) || !capture_bottom_attribute(s, a, pair, false)) return false;
        } else if (!bottom_pair_matches(s, a, s.bottom_slate)) return false;
    }
    if ((enabled || !contracts[method].owning) && !source_rgba(s, source)) {
        fail(Outcome::Memory, "emit_shared_color"); return false;
    }
    if (contracts[method].owning) {
        if (enabled)
            value = constant(themed(s, source));
        else {
            if (!owning(perf::Metric::NativeColorCopy, state.copy, &value, &s.native)) { fault("emit_copy"); return false; }
            if (!projection_current(s, a, sequence)) return false; // Uncertain local owner is never destroyed after authority loss.
        }
        s.overridden = s.overridden || enabled;
        // Consuming setter owns value now, including on a fault. No second destroy.
        if (!call(method, s.subject.widget, &value)) { fault("emit_setter"); return false; }
    } else {
        Linear value16 = tint ? themed(s, source) : rgba(s.native);
        s.overridden = s.overridden || enabled;
        s.property_source_current = false;
        if (!call(method, s.subject.widget, &value16)) { fault("emit_setter"); return false; }
    }
    if (!valid(s, a)) { fail(Outcome::Authority, "emit_post_native_authority"); return false; }
    if (bottom && !bottom_pair_matches(s, a, s.bottom_slate)) return false;
    if (!fresh(s)) { fail(Outcome::Unavailable, "source_stale_after_native"); return false; }
    if (!enabled && !contracts[method].owning) {
        if (!projection_current(s, a, sequence)) return false;
        s.property_source_current = true;
    }
    if (enabled && !project_source(s, a, sequence)) return false;
    s.overridden = enabled; // Only verified completion can discharge an existing override.
    return true;
}
bool current(Authority& a) noexcept
{
    return invocation && state.reader.current && state.reader.current(state.reader.context, *invocation, a)
        && a.game_thread_id == state.thread && admitted(a);
}
void intercept(std::size_t method, void* widget, void* input) noexcept
{
    perf::Span metric(perf::Metric::PaletteColorHook);
    ProofPhase phase;
    auto counted = state.callbacks.try_enter();
    auto shared = non_audio_hook_gate().try_enter();
    Entry entry;
    Authority a{};
    Source* s = nullptr;
    const bool nested = entry.previous && opacity_scope && opacity_scope->widget == widget
        && opacity_scope->property == contracts[method].property && opacity_scope->source->active
        && !opacity_scope->observed && fresh(*opacity_scope->source)
        && opacity_scope->captured == opacity_scope->source->updates.load();
    if (!counted || !shared || (entry.previous && !nested)
        || !(s = find(widget, contracts[method].property)) || !current(a) || !valid(*s, a)) {
        Forward forward(widget, contracts[method].property);
        if (!call(method, widget, input)) fault("forward_setter");
        return;
    }
    perf::annotate({a.session.generation});
    NativeColor synthetic{};
    const bool bottom = contracts[method].property == Property::TextBottom;
    SlatePair bottom_pair{};
    if (bottom && !read_bottom_pair(*s, a, bottom_pair)) {
        Forward forward(widget, contracts[method].property);
        if (!call(method, widget, input)) fault("forward_bottom_pair");
        return;
    }
    const void* source = input;
    if (nested) {
        synthetic = constant(opacity_scope->untinted);
        source = &synthetic;
        opacity_scope->observed = true;
    }
    const bool saved = save(*s, source);
    if (nested) s->captured = opacity_scope->captured;
    if (!saved || !valid(*s, a) || !fresh(*s)) {
        Forward forward(widget, contracts[method].property);
        // Saving did not consume or alter the real native argument.
        if (!call(method, widget, input)) fault("forward_after_capture");
        return;
    }
    const auto& c = s->policy.is_selected ? s->policy.selected : s->policy.normal;
    const auto sequence = s->sequence;
    Linear resolved{};
    if (c.enabled && !source_rgba(*s, resolved)) {
        Forward forward(widget, contracts[method].property);
        fail(Outcome::Memory, "setter_shared_color");
        if (!call(method, widget, input)) fault("forward_shared_color");
        return;
    }
    if (bottom) s->attribute_current = false;
    bool called = false;
    const bool next_override = c.enabled || (nested && s->overridden);
    if (contracts[method].owning) {
        // A consuming native argument cannot be memcpy'd or abandoned. Assignment
        // releases its old owner once; the original then consumes it exactly once.
        if (c.enabled) {
            auto replacement = constant(themed(*s, resolved));
            if (!owning(perf::Metric::NativeColorAssign, state.assign, input, &replacement)) {
                fault("consuming_argument_assign"); return;
            }
            if (!valid(*s, a) || !fresh(*s) || s->sequence != sequence) {
                Forward forward(widget, contracts[method].property);
                if (!call(method, widget, input)) fault("forward_consuming_argument");
                return;
            }
        }
        s->overridden = s->overridden || next_override;
        called = call(method, widget, input);
        if (!called) fault("owning_setter");
    } else {
        auto replacement = themed(*s, c.enabled ? resolved : rgba(s->native));
        s->overridden = s->overridden || next_override;
        s->property_source_current = false;
        called = call(method, widget, &replacement);
        if (!called) fault("linear_setter");
    }
    if (called && (!valid(*s, a) || !fresh(*s) || s->sequence != sequence)) return;
    if (bottom && called && !capture_bottom_attribute(*s, a, bottom_pair, true)) { s->updates.fetch_add(1); return; }
    if (called && project_source(*s, a, sequence)) s->overridden = next_override;
}
void intercept_opacity(std::size_t method, void* widget, float alpha) noexcept
{
    perf::Span metric(perf::Metric::PaletteOpacityHook);
    ProofPhase phase;
    auto counted = state.callbacks.try_enter();
    auto shared = non_audio_hook_gate().try_enter();
    Entry entry;
    Authority a{};
    Source* s = nullptr;
    if (!counted || !shared || entry.previous
        || !(s = find(widget, contracts[method].property)) || !current(a) || !valid(*s, a) || !fresh(*s)) {
        Forward forward(widget, contracts[method].property);
        if (!call_opacity(method, widget, alpha)) fault("forward_opacity");
        return;
    }
    perf::annotate({a.session.generation});
    Linear untinted{};
    const auto captured = s->captured;
    if (!source_rgba(*s, untinted)) {
        Forward forward(widget, contracts[method].property);
        fail(Outcome::Memory, "opacity_shared_color");
        if (!call_opacity(method, widget, alpha)) fault("forward_shared_opacity");
        return;
    }
    untinted[3] = alpha;
    if (contracts[method].owning) {
        // Text opacity reads the already-themed widget and calls its color setter.
        // Substitute the saved UNTINTED RGB as that nested setter's source.
        NestedOpacity nested{s, widget, contracts[method].property, untinted, captured, false};
        auto* previous = opacity_scope;
        opacity_scope = &nested;
        const bool ok = call_opacity(method, widget, alpha);
        opacity_scope = previous;
        if (!ok) fault("text_opacity");
        else if (!nested.observed && s->active && fresh(*s)) fault("text_opacity_missing_nested_setter");
    } else {
        auto source = constant(untinted);
        const bool saved = save(*s, &source);
        s->captured = captured; // Alpha alone cannot recover RGB after a forwarded update.
        if (!saved || !valid(*s, a) || !fresh(*s)) {
            Forward forward(widget, contracts[method].property);
            if (!call_opacity(method, widget, alpha)) fault("forward_opacity_capture");
            return;
        }
        const auto sequence = s->sequence;
        const auto presentation = themed(*s, untinted);
        const bool enabled = (s->policy.is_selected ? s->policy.selected.enabled : s->policy.normal.enabled);
        if (!projection_current(*s, a, sequence)) {
            Forward forward(widget, contracts[method].property);
            if (!call_opacity(method, widget, alpha)) fault("forward_opacity_projection");
            return;
        }
        // These direct opacity methods refresh Slate from the complete property.
        // RGB projection lasts only through that real native call.
        s->property_source_current = false;
        if (enabled && !core::safe_write_field(widget, contracts[method].offset, presentation)) {
            s->updates.fetch_add(1); fail(Outcome::Memory, "opacity_projection_write");
            Forward forward(widget, contracts[method].property);
            if (!call_opacity(method, widget, alpha)) fault("forward_opacity_projection_write");
            return;
        }
        const float modulated = presentation[3];
        if ((s->policy.is_selected ? s->policy.selected.enabled : s->policy.normal.enabled)) s->overridden = true;
        if (!call_opacity(method, widget, modulated)) fault("image_opacity");
        else project_source(*s, a, sequence);
    }
}
template<std::size_t I> __declspec(noinline) void __fastcall color_detour(void* widget, void* input) {
    InvocationScope scope(_AddressOfReturnAddress(), _ReturnAddress(), I, widget);
    intercept(I, widget, input);
}
template<std::size_t I> __declspec(noinline) void __fastcall opacity_detour(void* widget, float alpha) {
    InvocationScope scope(_AddressOfReturnAddress(), _ReturnAddress(), I, widget);
    intercept_opacity(I, widget, alpha);
}
const std::array<void*, kMethods> detours{
    reinterpret_cast<void*>(&color_detour<0>), reinterpret_cast<void*>(&color_detour<1>),
    reinterpret_cast<void*>(&color_detour<2>), reinterpret_cast<void*>(&color_detour<3>),
    reinterpret_cast<void*>(&color_detour<4>), reinterpret_cast<void*>(&color_detour<5>),
    reinterpret_cast<void*>(&color_detour<6>), reinterpret_cast<void*>(&color_detour<7>),
    reinterpret_cast<void*>(&color_detour<8>), reinterpret_cast<void*>(&color_detour<9>),
    reinterpret_cast<void*>(&color_detour<10>), reinterpret_cast<void*>(&opacity_detour<11>),
    reinterpret_cast<void*>(&opacity_detour<12>), reinterpret_cast<void*>(&opacity_detour<13>),
    reinterpret_cast<void*>(&opacity_detour<14>), reinterpret_cast<void*>(&opacity_detour<15>),
    reinterpret_cast<void*>(&opacity_detour<16>),
};
const RvaSignatureSpec* authenticate(HMODULE module, const char* id) noexcept
{
    const auto* spec = find_rva_signature(id);
    const auto range = core::image_range(module);
    if (!spec || !spec->rva || spec->expected_prologue.empty() || !range.base
        || spec->rva >= range.size || spec->expected_prologue.size() > range.size - spec->rva)
        return nullptr;
    MEMORY_BASIC_INFORMATION memory{};
    const auto* p = range.base + spec->rva;
    if (!VirtualQuery(p, &memory, sizeof(memory)) || memory.State != MEM_COMMIT
        || memory.AllocationBase != module || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return nullptr;
    const auto protect = memory.Protect & 0xff;
    if (protect != PAGE_EXECUTE && protect != PAGE_EXECUTE_READ
        && protect != PAGE_EXECUTE_READWRITE && protect != PAGE_EXECUTE_WRITECOPY) return nullptr;
    return core::bytes_equal(p, spec->expected_prologue) ? spec : nullptr;
}
} // namespace

bool current_invocation(NativeInvocation& out) noexcept
{
    if (!invocation) { out = {}; return false; }
    out = *invocation; return true;
}
bool invocation_matches(const NativeInvocation& value) noexcept
{
    return invocation && value.return_slot == invocation->return_slot && value.caller == invocation->caller
        && value.widget == invocation->widget && value.property == invocation->property
        && value.opacity == invocation->opacity && value.thread == invocation->thread
        && value.serial == invocation->serial;
}

Result configure(HMODULE module, std::string_view profile, CallbackReader reader) noexcept
{
    if (entered || state.installed || state.callbacks.callbacks_in_flight()) return fail(Outcome::Busy, "configure_busy");
    for (const auto& hook : state.hooks) if (hook.installed()) return fail(Outcome::Busy, "configure_hook_retained");
    state.configured = false;
    if (state.poisoned.load()) return fail(Outcome::Unavailable, "native_fault_latched");
    if (!module || profile != FF7RP_TARGET_BUILD_ID || !core::is_supported_exe(module))
        return fail(Outcome::UnsupportedImage, "executable_identity");
    if (!reader.current) return fail(Outcome::Input, "callback_reader");
    std::array<const RvaSignatureSpec*, kMethods> specs{};
    for (std::size_t i = 0; i < kMethods; ++i) {
        specs[i] = authenticate(module, contracts[i].id);
        if (!specs[i]) return fail(Outcome::Signature, contracts[i].id);
        for (std::size_t j = 0; j < i; ++j)
            if (specs[i]->rva == specs[j]->rva) return fail(Outcome::Signature, "duplicate_setter_address");
    }
    const auto* copy = authenticate(module, "native_slate_color_copy_construct");
    const auto* assign = authenticate(module, "native_slate_color_assign");
    const auto* destroy_spec = authenticate(module, "native_slate_color_destroy");
    if (!copy || !assign || !destroy_spec) return fail(Outcome::Signature, "slate_color_ownership");
    const auto* attribute_assign = authenticate(module, "native_slate_color_attribute_assign");
    const auto* attribute_destroy = authenticate(module, "native_slate_color_attribute_destroy");
    const auto* bottom_setter = authenticate(module, "end_text_slate_set_bottom_attribute");
    if (!attribute_assign || !attribute_destroy || !bottom_setter)
        return fail(Outcome::Signature, "bottom_attribute_ownership");
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    state.copy = reinterpret_cast<Copy>(base + copy->rva);
    state.assign = reinterpret_cast<Copy>(base + assign->rva);
    state.destroy = reinterpret_cast<Destroy>(base + destroy_spec->rva);
    state.attribute_assign = reinterpret_cast<Copy>(base + attribute_assign->rva);
    state.attribute_destroy = reinterpret_cast<Destroy>(base + attribute_destroy->rva);
    state.bottom_attribute_setter = reinterpret_cast<Setter>(base + bottom_setter->rva);
    state.specs = specs; state.module = module; state.reader = reader;
    state.configured = true;
    return {Outcome::Ready, "configured"};
}
Result install() noexcept
{
    if (!state.configured || state.poisoned.load()) return fail(Outcome::Unavailable, "install_unconfigured");
    if (state.installed) return fail(Outcome::Busy, "already_installed");
    for (const auto& h : state.hooks) if (h.installed()) return fail(Outcome::Busy, "rollback_hook_retained");
    state.callbacks.open();
    for (std::size_t i = 0; i < kMethods; ++i) {
        std::string error;
        if (!state.hooks[i].install(state.module, state.specs[i]->rva,
            state.specs[i]->expected_prologue, detours[i], &state.originals[i], error)) {
            const auto rollback = uninstall();
            return fail(rollback ? Outcome::Unavailable : rollback.outcome,
                rollback ? contracts[i].id : rollback.stage);
        }
    }
    state.installed = true;
    return {Outcome::Ready, "installed"};
}
Result uninstall(std::chrono::milliseconds timeout) noexcept
{
    if (entered) return fail(Outcome::Busy, "uninstall_callback");
    const bool drained = core::disable_then_close_and_drain(state.callbacks, [] {
        bool ok = true;
        for (auto& hook : state.hooks) if (hook.installed()) ok = hook.disable() && ok;
        return ok;
    }, timeout);
    if (!drained) return fail(Outcome::Busy, "disable_drain");
    // Clear registered subjects before the adapter can release validator contexts.
    {
        std::lock_guard lock(state.mutex);
        for (auto& record : state.sources) {
            auto& s = *record;
            s.routed_widget = nullptr; s.active = false; s.subject = {};
        }
        state.reader = {};
    }
    bool removed = true;
    for (auto& hook : state.hooks) if (hook.installed()) removed = hook.remove() && removed;
    if (!removed) return fail(Outcome::Busy, "remove_hooks");
    state.installed = false; state.configured = false; state.thread = 0;
    return {Outcome::Ready, "uninstalled"};
}
Result bind(const Subject& subject, const Policy& policy, const Authority& a) noexcept
{
    perf::Span metric(perf::Metric::PaletteBind);
    ProofPhase phase;
    if (entered) return fail(Outcome::Busy, "bind_reentry");
    auto counted = state.callbacks.try_enter();
    if (!counted || !state.installed || !admitted(a)) return fail(Outcome::Authority, "bind_authority");
    if (!subject.widget || !subject.validate || index(subject.property) >= index(Property::Count)
        || !policy_valid(policy)) return fail(Outcome::Input, "bind_input");
    if (state.thread && state.thread != a.game_thread_id) return fail(Outcome::Authority, "bind_thread");
    Entry entry;
    if (find(subject.widget, subject.property)) return fail(Outcome::Busy, "already_bound");
    Source* slot = nullptr;
    end_proof_phase(); // Neither allocator may carry a proof through reentry.
    try {
        std::lock_guard lock(state.mutex);
        for (auto& record : state.sources) {
            auto& s = *record;
            if (!s.occupied && s.forwarding.load() == 0) { slot = &s; s.occupied = true; break; }
        }
        if (!slot && state.sources.size() != state.sources.max_size()) {
            // Either allocation may fail; the unpublished byte-only node unwinds safely.
            auto pending = std::make_unique<Source>();
            state.sources.push_back(std::move(pending));
            slot = state.sources.back().get();
            slot->occupied = true;
        }
    } catch (const std::bad_alloc&) {
        return fail(Outcome::Budget, "source_allocation");
    } catch (const std::length_error&) {
        return fail(Outcome::Budget, "source_storage_size");
    }
    if (!slot) return fail(Outcome::Budget, "source_storage_size");
    end_proof_phase();
    slot->subject = subject; slot->policy = policy; slot->active = true; slot->overridden = false;
    slot->property_source_current = false;
    slot->attribute_current = false; slot->bottom_slate = {};
    { std::lock_guard lock(state.mutex); slot->routed_property = subject.property; slot->routed_widget = subject.widget; }
    state.thread = a.game_thread_id;
    if (!valid(*slot, a)) {
        detach(*slot); slot->occupied = false;
        return fail(Outcome::Identity, "bind_subject");
    }
    const auto& c = contracts[index(subject.property)];
    const bool passive = !(policy.is_selected ? policy.selected.enabled : policy.normal.enabled);
    const auto revision = slot->updates.load();
    NativeColor read{};
    if (!core::safe_copy_bytes(static_cast<unsigned char*>(subject.widget) + c.offset,
        &read, c.owning ? sizeof(read) : sizeof(Linear)) || (!passive && !valid(*slot, a))) {
        detach(*slot); // uncertain owner remains reserved
        return fail(Outcome::Memory, "bind_capture");
    }
    // Passive capture validates the subject after its owning helper, rather than
    // before the helper and again through an emit that cannot change anything.
    if (passive && (!slot->active || !admitted(a) || !slot->active)) {
        detach(*slot); return fail(Outcome::Authority, "bind_capture_authority");
    }
    if (slot->forwarding.load() || slot->updates.load() != revision) {
        detach(*slot); return fail(Outcome::Unavailable, "bind_capture_forwarded");
    }
    if (!save(*slot, &read)) {
        detach(*slot);
        return last_failure();
    }
    if (passive && !valid(*slot, a)) {
        detach(*slot); return fail(Outcome::Authority, "bind_capture_subject");
    }
    if (!fresh(*slot) || slot->captured != revision) {
        detach(*slot); return fail(Outcome::Unavailable, "bind_capture_superseded");
    }
    slot->property_source_current = true;
    if (passive) { perf::count(perf::Counter::PassiveBindings); return {Outcome::Ready, "bound"}; }
    if (!emit(*slot, a, true)) return last_failure();
    return {Outcome::Ready, "bound"};
}
Result change(const Subject& subject, const Policy& policy, const Authority& a) noexcept
{
    perf::Span metric(perf::Metric::PaletteChange);
    ProofPhase phase;
    if (entered) return fail(Outcome::Busy, "change_reentry");
    auto counted = state.callbacks.try_enter();
    if (!counted || !admitted(a)) return fail(Outcome::Authority, "change_authority");
    if (!policy_valid(policy)) return fail(Outcome::Input, "change_policy");
    Entry entry;
    auto* s = find(subject.widget, subject.property);
    if (!s || !same(s->subject, subject) || !valid(*s, a)) return fail(Outcome::Identity, "change_subject");
    s->policy = policy;
    return emit(*s, a, true) ? Result{Outcome::Ready, "changed"} : last_failure();
}
Result restore(const Subject& subject, const Authority& a) noexcept
{
    perf::Span metric(perf::Metric::PaletteRestore);
    Policy original{};
    return change(subject, original, a);
}
namespace {
Result retire_bindings(void* owner, std::uint64_t generation, const Subject* only,
    RetirementMatch match, void* context, const Authority& a) noexcept
{
    perf::Span metric(perf::Metric::PaletteRetire);
    ProofPhase phase;
    const auto matches = [&](const Source& s) {
        return s.active && s.subject.owner == owner && s.subject.generation == generation
            && (!only || same(s.subject, *only)) && (!match || match(context, s.subject));
    };
    if (state.thread != GetCurrentThreadId()) return fail(Outcome::Authority, "retire_thread");
    if (entered) {
        for (auto& record : state.sources) if (matches(*record)) detach(*record);
        return fail(Outcome::Authority, "retired_reentrant_orphan");
    }
    auto counted = state.callbacks.try_enter();
    if (!counted) {
        for (auto& record : state.sources) if (matches(*record)) detach(*record);
        return fail(Outcome::Authority, "retired_gate_orphan");
    }
    // No registry access from a worker; even lost authority must be retired by
    // the established owner thread, or by drained uninstall.
    Entry entry;
    bool ok = true;
    for (auto& record : state.sources) {
        auto& s = *record;
        if (!matches(s)) continue;
        const bool owns = contracts[index(s.subject.property)].owning;
        if (!owns && !s.overridden && s.property_source_current
            && !s.attribute_initialized && !s.attribute_current && !s.forwarding.load()) {
            // No native owner or presentation to restore; metadata retirement needs no UObject proof.
            detach(s);
            s.initialized = false; s.occupied = false; s.property_source_current = false; s.bottom_slate = {};
            continue;
        }
        const bool restored = valid(s, a) && emit(s, a, false);
        const bool cleanup = restored && valid(s, a);
        detach(s); // retire before native destructor reentry
        bool cleared = cleanup;
        if (cleared && s.attribute_initialized) {
            if (!admitted(a)) cleared = false;
            else if (!destroy_attribute(&s.bottom_attribute)) { fault("retire_attribute_destroy"); cleared = false; }
            else s.attribute_initialized = false;
        }
        if (cleared && owns) {
            if (!admitted(a)) cleared = false;
            else if (!destroy(&s.native)) { fault("retire_destroy"); cleared = false; }
        }
        if (cleared && admitted(a)) {
            s.initialized = false; s.occupied = false; s.attribute_current = false; s.bottom_slate = {};
        } else ok = false;
    }
    return ok ? Result{Outcome::Ready, "retired"} : fail(Outcome::Authority, "retired_orphan_ownership");
}
}
Result retire(void* owner, std::uint64_t generation, const Authority& a) noexcept
{
    return retire_bindings(owner, generation, nullptr, nullptr, nullptr, a);
}
Result retire_subject(const Subject& subject, const Authority& a) noexcept
{
    const Subject copy = subject; // Native restoration may reenter the binding owner.
    return retire_bindings(copy.owner, copy.generation, &copy, nullptr, nullptr, a);
}
Result retire_matching(void* owner, std::uint64_t generation, RetirementMatch match,
    void* context, const Authority& a) noexcept
{
    if (!match) return fail(Outcome::Input, "retire_match");
    return retire_bindings(owner, generation, nullptr, match, context, a);
}
Result last_failure() noexcept { std::lock_guard lock(state.mutex); return state.failure; }
std::size_t source_storage_count() noexcept { std::lock_guard lock(state.mutex); return state.sources.size(); }
} // namespace ff7r::piano::game::menu_theme_palette
