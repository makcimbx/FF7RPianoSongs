#include "game/menu_theme_runtime.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "core/version_gate.h"
#include "game/menu_theme_native.h"
#include "game/menu_theme_observation.h"
#include "game/menu_theme_plan.h"
#include "game/song_preview_overlay.h"
#include "game/menu_theme_palette.h"
#include "game/menu_theme_metrics.h"
#include "game/uobject_lifetime.h"
#include "game/runtime_layouts.h"
#include "game/module_hooks.h"
#include "game/rvas.h"

#include <algorithm>
#include <bit>
#include <array>
#include <atomic>
#include <deque>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <map>
#include <set>
#include <tuple>
#include <sstream>
#include <vector>

namespace ff7r::piano::game::menu_theme_runtime {
namespace {
// UI fairness bounds, not readiness, inventory, or CPU-utilization policy.
namespace native = menu_theme_native;
namespace observation = menu_theme_observation;
namespace plan = menu_theme_plan;
namespace palette = menu_theme_palette;
namespace metrics = menu_theme_metrics;
enum class Source { Ready, Close, PostedInput, Preparation, RowSetup, Selection };
enum class Transition { Choose, Delta, Close };
enum class Direction { Previous, Next };
enum class Cached { Pending, Ready, Failed };
struct Resource {
    core::ThemeImage pixels;
    native::OwnedImage image;
    Cached cached = Cached::Pending;
    native::Result result{};
};
struct Binding {
    std::string name;
    std::wstring leaf;
    native::ImageTarget target;
    native::OwnedImage original;
    bool available = false, resolved = false;
    native::Result result{};
    plan::Target selector{};
    native::Subject subject{};
    native::BrushSelection slot{};
    bool generic = false;
    void* widget = nullptr;
    native::SubjectKind kind = native::SubjectKind::Image;
    bool kind_known = false;
    std::size_t canonical = SIZE_MAX;
    std::size_t prototype = SIZE_MAX, row = SIZE_MAX, shown = SIZE_MAX;
};
struct Combination {
    std::size_t binding, resource;
    native::OwnedImage image;
    bool available = false;
    native::Result result{};
    bool tinted = false;
    std::size_t base = SIZE_MAX;
    std::array<float, 4> rgba{};
    std::size_t prototype = SIZE_MAX, row = SIZE_MAX;
    plan::State variant = plan::State::Any;
};
struct PreparationWork {
    std::uint64_t revision = 0;
    std::set<std::size_t> needed, bindings, combinations, palettes, resources;
    std::vector<std::size_t> rows;
    std::set<std::size_t> changed_rows;
    std::size_t expansion = 0, palette_expansion = 0, row = 0;
    std::size_t resolve_binding = 0, resolve_palette = 0, capture = 0, upload = 0, compose = 0, bind = 0;
    bool needs_rows = false, needs_speed = false;
    std::uint64_t visits = 0;
};
struct RowInventory {
    std::map<std::size_t, std::size_t> binding, combination, palette;
    std::vector<std::size_t> bindings, combinations, palettes;
};
template<class T>
std::size_t append_physical(std::deque<T>& records, std::map<std::size_t, std::size_t>& by_prototype,
    std::vector<std::size_t>& indices, std::size_t prototype, T record)
{
    const auto i = records.size();
    records.push_back(std::move(record));
    try { by_prototype.emplace(prototype, i); indices.push_back(i); }
    catch (...) { by_prototype.erase(prototype); records.pop_back(); throw; }
    return i;
}
struct IndexRange {
    std::size_t count;
    const std::vector<std::size_t>* indices = nullptr;
    struct Iterator {
        std::size_t position; const std::vector<std::size_t>* indices;
        std::size_t operator*() const noexcept { return indices ? (*indices)[position] : position; }
        Iterator& operator++() noexcept { ++position; return *this; }
        bool operator!=(const Iterator& other) const noexcept { return position != other.position; }
    };
    Iterator begin() const noexcept { return {0, indices}; }
    Iterator end() const noexcept { return {indices ? indices->size() : count, indices}; }
};
struct Prepared {
    std::string id;
    plan::Program program;
    // Program image i uses resource bank entry resources[i]; pixels have one owner.
    std::vector<std::size_t> resources, combinations;
    std::vector<std::size_t> palettes;
    std::vector<std::size_t> palette_colors;
    std::string rejection;
    bool backend = false, available = false;
    bool rejected = false, preparing = true;
    bool row_repair_only = false;
    bool song_rows = false;
    const char* admission_stage = nullptr;
    const char* progress_stage = nullptr;
    PreparationWork work;
};
struct PaletteBinding {
    plan::Target selector{};
    std::string name;
    std::wstring leaf;
    native::Subject target{};
    palette::Subject subject{};
    MenuSessionSnapshot session{};
    std::size_t canonical = SIZE_MAX;
    bool available = false, resolved = false;
    native::Result result{};
    std::size_t prototype = SIZE_MAX, row = SIZE_MAX;
    palette::Policy shown{};
    bool retirement_current = false;
    native::SubjectKind kind = native::SubjectKind::Image;
    bool kind_known = false;
};
struct SubjectLookup {
    std::wstring leaf;
    std::size_t row = SIZE_MAX;
    MenuSessionSnapshot session{};
    UObjectLiveHandle identity{};
    void* receiver = nullptr;
    std::uint64_t setup = 0, epoch = 0;
    native::SubjectKind kind{};
    native::Subject subject{};
    plan::Scope scope = plan::Scope::MusicList;
};
struct RowRecord {
    void* row = nullptr;
    UObjectLiveHandle identity{};
    MenuSessionSnapshot session{};
    std::int32_t index = -1;
    std::uint64_t association_setup = 0; // Admitted association, not each setup invocation counter.
    native::RowTicket ticket{};
    bool available = false;
    bool style_pending = false;
    bool failed = false;
    void* context = nullptr;
    UObjectLiveHandle context_identity{};
    native::RowKind kind = native::RowKind::Song;
    std::uint64_t membership_epoch = 0;
    bool detached_themed = false;
    native::Result binding_result{};
    std::uint64_t refresh_invocation = 0, refresh_elapsed_ms = 0;
    std::uint64_t diagnostic_pending_ns = 0; // Diagnostic only; never readiness or native authority.
    RowInventory inventory; // Stable physical indices only, never native authority.
    std::vector<SubjectLookup> subjects;
    PreparationWork work;
    std::uint64_t lookup_visits = 0;
    std::size_t prepared_choice = SIZE_MAX;
    bool refresh_palettes = false;
};
struct PreparationFacts {
    // Cumulative milliseconds, not exclusive CPU time: backend timings include
    // their validation; dispatch service includes runtime admission/application.
    std::uint64_t generation = 0;
    std::uint64_t dispatches = 0, service_ms = 0, queue_wait_ms = 0;
    std::uint64_t capture_calls = 0, capture_backend_ms = 0; // scalar + member API calls, not member count
    std::uint64_t capture_requested_members = 0, capture_batch_calls = 0;
    std::uint64_t batch_prepared_members = 0, batch_created_members = 0, batch_published_members = 0;
    std::uint64_t passive_bind_calls = 0, passive_bind_backend_ms = 0;
};
struct State {
    std::mutex mutex;
    HMODULE module = nullptr;
    std::vector<Prepared> themes; // original is index zero
    std::vector<Resource> resources;
    std::deque<Binding> bindings;
    std::deque<Combination> combinations;
    std::deque<PaletteBinding> palettes; // Published validator contexts never move during row expansion.
    std::size_t binding_templates = 0, combination_templates = 0, palette_templates = 0;
    std::size_t native_attempts = 0;
    std::uint64_t native_attempted_bytes = 0; // Known payload attempts only, not actual native storage or admission.
    bool work_pending = false, subjects_prepared = false;
    bool prepared_once = false;
    std::size_t preparation_cursor = 1;
    std::size_t desired = SIZE_MAX;
    bool desired_save = false;
    bool desired_gesture = false;
    Direction desired_direction = Direction::Next;
    std::uint64_t desired_epoch = 0;
    std::atomic_bool preparation_post_failed{false};
    std::atomic<const void*> row_rebind{nullptr};
    bool untracked_row = false;
    bool speed_ready = false;
    bool speed_dirty = false;
    native::Result speed_result{};
    std::uint64_t speed_epoch = 0;
    std::size_t retained_entries = 0;
    std::wstring ini;
    std::string selected = "original";
    int key = 0;
    std::atomic_bool held{false}, focused{false};
    std::atomic<std::uint64_t> focus_epoch{0}, cancelled_ticket{0};
    std::uint64_t sequence = 0;
    PreparationFacts preparation_facts{};
    std::uint64_t diagnostic_ready_ns = 0, diagnostic_ready_generation = 0;
    struct Request {
        std::uint64_t ticket = 0, focus_epoch = 0;
        MenuSessionSnapshot owner{};
        Direction direction = Direction::Next;
        bool preparation = false;
        std::uint64_t enqueued_ms = 0;
        std::uint64_t diagnostic_enqueued_ns = 0;
        bool continuation_posted = false;
    } pending;
    bool configured = false;
    MenuSessionSnapshot owner{};
    DWORD owner_thread = 0;
    std::size_t applied = 0;
    std::atomic_bool compromised{false};
    std::deque<RowRecord> rows;
    std::vector<SubjectLookup> subjects; // Copied descriptors, never callback authority or owning originals.
    std::set<std::size_t> pending_rows;
    std::uint64_t work_revision = 1;
    std::vector<std::size_t> changed, changed_colors;
    std::vector<palette::Policy> prior_policies;
    std::int32_t semantic_selected = -1;
    bool palette_configured = false, palette_installed = false;
    std::size_t palette_charged = 0, palette_current = 0;
};
State state; // Native roots are owned by the bridge, never destructed here.
void replace_row(RowRecord& row, RowRecord next = {}) noexcept
{
    auto inventory = std::move(row.inventory);
    row = std::move(next);
    row.inventory = std::move(inventory);
}
bool demanded(std::size_t choice) noexcept
{
    return choice && choice < state.themes.size() && (choice == state.applied || choice == state.desired);
}
bool original_has_palette_routes() noexcept
{
    return !state.applied && state.desired == SIZE_MAX
        && std::any_of(state.palettes.begin(), state.palettes.end(), [](const auto& p) { return p.available; });
}
void update_work_pending() noexcept
{
    // Passive/disabled routes still intercept animation. Drain them only on a
    // qualified callback, including after focus cancellation while unfocused.
    state.work_pending = original_has_palette_routes();
    state.work_pending |= state.applied && !state.pending_rows.empty();
    for (std::size_t i = 1; i < state.themes.size(); ++i)
        state.work_pending |= demanded(i) && state.themes[i].preparing && !state.themes[i].rejected;
    state.preparation_cursor = 1;
}
bool demanded_scope(plan::Scope target) noexcept
{
    for (std::size_t i = 1; i < state.themes.size(); ++i) if (demanded(i)) {
        for (const auto& image : state.themes[i].program.images) if (image.target.scope == target) return true;
        for (const auto& color : state.themes[i].program.colors) if (color.target.scope == target) return true;
    }
    return false;
}
bool awaiting_receivers(const char* stage) noexcept
{
    return stage && (std::string_view(stage) == "song_row_receivers_not_ready"
        || std::string_view(stage) == "speed_members_empty" || std::string_view(stage) == "speed_reconciliation_requires_preparation");
}
std::uint64_t diagnostic_now() noexcept { return metrics::enabled() ? metrics::now_ns() : 0; }
void cancel_row_diagnostic(RowRecord& row) noexcept
{
    if (!row.diagnostic_pending_ns) return;
    row.diagnostic_pending_ns = 0;
    metrics::count(metrics::Counter::RowsCancelled);
}
void pending_row_diagnostic(RowRecord& row) noexcept
{
    if (row.diagnostic_pending_ns) return; // Retries retain the original association's start.
    row.diagnostic_pending_ns = diagnostic_now();
    if (row.diagnostic_pending_ns) metrics::count(metrics::Counter::RowsPending);
}
void publish_row_diagnostic(RowRecord& row, std::uint64_t now) noexcept
{
    if (!row.diagnostic_pending_ns || !row.available || row.style_pending || !same_menu_session(row.session, state.owner)) return;
    metrics::annotate({row.session.generation, row.association_setup, row.identity.internal_index, row.index});
    if (now >= row.diagnostic_pending_ns) metrics::sample(metrics::Metric::RowPendingDelay, now - row.diagnostic_pending_ns);
    row.diagnostic_pending_ns = 0;
    metrics::count(metrics::Counter::RowsStyled);
}
void metrics_log(const char* line) noexcept
{
    try { core::log(core::LogLevel::Info, line); } catch (...) {} // Invoked only by flush outside runtime/native locks.
}
void add_fact(std::uint64_t& total, std::uint64_t value) noexcept
{
    total = value > UINT64_MAX - total ? UINT64_MAX : total + value;
}
template<class Call>
native::Result measured_backend(bool capture, Call&& call, std::size_t members = 1) noexcept
{
    metrics::Stamp measured;
    if (capture) metrics::begin(measured, metrics::Metric::Capture);
    else metrics::count(metrics::Counter::PassiveBindings);
    const auto generation = state.preparation_facts.generation;
    const auto started = GetTickCount64();
    const auto result = call();
    metrics::finish(measured, static_cast<bool>(result));
    const auto elapsed = GetTickCount64() - started;
    if (generation && state.preparation_facts.generation == generation) {
        auto& facts = state.preparation_facts;
        if (capture) {
            add_fact(facts.capture_calls, 1); add_fact(facts.capture_backend_ms, elapsed);
            add_fact(facts.capture_requested_members, members);
        }
        else { add_fact(facts.passive_bind_calls, 1); add_fact(facts.passive_bind_backend_ms, elapsed); }
    }
    return result;
}
thread_local std::uint64_t retired_palette_serial = 0;
thread_local std::uintptr_t scope_serial = 0;
void invalidate_preparation() noexcept
{
    // Leave work storage alive through reentrant native calls; replace it only
    // when a later qualified preparation frame observes the new revision.
    if (++state.work_revision == 0) {
        state.work_revision = 1;
        for (auto& theme : state.themes) theme.work.revision = 0;
    }
}
void observe_row_setup(const void*, void*, void*, std::int32_t, std::uint64_t, bool,
    const core::HookCallbackGate::Lease&, RowSetupSource = RowSetupSource::NativeSetup) noexcept;
void observe_selection(const void*, void*, std::int32_t, bool, const core::HookCallbackGate::Lease&) noexcept;
void observe_row_retired(const void*) noexcept;
bool refresh_composition(struct Scope&, struct Event&, std::vector<struct Event>* diagnostics = nullptr, std::size_t physical_row = SIZE_MAX, std::size_t requested = SIZE_MAX) noexcept;
bool retire_row(std::size_t, struct Scope&, struct Event&) noexcept;
bool reconcile_speed(struct Scope&, struct Event&, bool, std::size_t = SIZE_MAX) noexcept;
bool repair_applied_rows(struct Scope&, struct Event&, std::size_t physical_row = SIZE_MAX) noexcept;
bool current_song_association(std::size_t, struct Scope&) noexcept;
bool suspend_song_style(std::size_t, struct Scope&, struct Event&) noexcept;
bool finish_song_refresh(std::size_t, struct Scope&, struct Event&) noexcept;
bool match_palette_row(void*, const palette::Subject&) noexcept;
struct RetirementFilter { std::size_t row; bool current; };
bool match_palette_partition(void*, const palette::Subject&) noexcept;
bool apply_choice(struct Scope&, std::size_t, struct Event&, bool save = true, Transition mode = Transition::Choose) noexcept;
bool prepare_quantum(struct Scope&, struct Event&, std::vector<Event>*, std::size_t requested = SIZE_MAX, std::size_t physical_row = SIZE_MAX) noexcept;
struct Scope;
thread_local Scope* active_scope = nullptr;

bool executable_address(HMODULE module, std::uintptr_t address) noexcept
{
    metrics::Span mapping(metrics::Metric::QualifyMapping);
    return core::is_executable_image_address(module, address, false);
}

// Called exclusively by palette's matched setter body while its private TLS
// invocation AND real non-audio/module leases survive, never from a saved reader.
bool palette_phase_current(void* serial, const MenuSessionSnapshot& expected) noexcept
{
    palette::NativeInvocation invocation{};
    return non_audio_hook_gate().accepting() && !state.compromised.load(std::memory_order_acquire)
        && palette::current_invocation(invocation) && palette::invocation_matches(invocation)
        && invocation.thread == state.owner_thread && invocation.serial
        && invocation.serial == reinterpret_cast<std::uintptr_t>(serial) && invocation.serial > retired_palette_serial
        && same_menu_session(expected, state.owner) && same_menu_session(expected, capture_ready_menu_session());
}
bool qualify_palette_stack(void* serial, const MenuSessionSnapshot& expected) noexcept
{
    metrics::Span measured(metrics::Metric::QualifyCallback);
    metrics::Span palette_kind(metrics::Metric::QualifyPalette);
    if (!non_audio_hook_gate().accepting() || state.compromised.load()) return false;
    palette::NativeInvocation invocation{};
    if (!palette::current_invocation(invocation) || !palette::invocation_matches(invocation)
         || invocation.thread != GetCurrentThreadId() || !invocation.serial || !invocation.return_slot
         || invocation.serial != reinterpret_cast<std::uintptr_t>(serial)
         || invocation.serial <= retired_palette_serial
        || !executable_address(state.module, reinterpret_cast<std::uintptr_t>(invocation.caller))) return false;
    void* caller = nullptr;
    if (!core::safe_copy_bytes(invocation.return_slot, &caller, sizeof(caller)) || caller != invocation.caller
        || !same_menu_session(expected, capture_ready_menu_session())) return false;
    const auto facts = observation::capture_native_facts(state.module, FF7RP_TARGET_BUILD_ID, expected.generation);
    return observation::classify_native_facts(facts) == observation::Failure::None
        && facts.current_thread == invocation.thread && same_menu_session(facts.session, expected)
        && same_menu_session(expected, state.owner)
        && palette::invocation_matches(invocation) && palette_phase_current(serial, expected);
}
bool read_palette_stack(void*, const palette::NativeInvocation& invocation, palette::Authority& authority) noexcept
{
    if (!palette::invocation_matches(invocation) || !non_audio_hook_gate().accepting()) return false;
    const auto session = capture_ready_menu_session();
    // Candidate only: palette performs full admission before its private phase.
    auto* serial = reinterpret_cast<void*>(static_cast<std::uintptr_t>(invocation.serial));
    if (!palette_phase_current(serial, session)) return false;
    authority = {session, invocation.thread, serial, qualify_palette_stack, palette_phase_current};
    return palette::invocation_matches(invocation);
}

// Only this stack-local object can furnish the bridge's callback qualifier.
// Facts add rechecks to the owned frame; they do not create that frame's authority.
struct Scope {
    Source source;
    const void* frame;
    const core::HookCallbackGate::Lease& lease;
    MenuSessionSnapshot session;
    observation::Snapshot facts{};
    std::uintptr_t caller = 0;
    std::uint64_t ticket = 0;
    std::uint64_t focus_epoch = 0;
    DWORD thread = GetCurrentThreadId();
    std::uintptr_t serial = 0;
    bool retired = false;
    void* row = nullptr;
    std::int32_t index = -1;
    std::uint64_t setup = 0;
    bool after = true;
    std::size_t restricted_row = SIZE_MAX;
    bool refresh_palettes = false;
    std::size_t serviced_row = SIZE_MAX;
    bool step() noexcept {
        metrics::count(metrics::Counter::PreparationSteps);
        return !retired;
    }
    Scope(Source s, const void* f, const core::HookCallbackGate::Lease& l,
        const MenuSessionSnapshot& owner, std::uint64_t request = 0, std::uint64_t epoch = 0) noexcept
        : source(s), frame(f), lease(l), session(owner), ticket(request), focus_epoch(epoch)
    {
        if (active_scope) { active_scope->retired = true; retired = true; }
        if (scope_serial == std::numeric_limits<std::uintptr_t>::max()) retired = true;
        else serial = ++scope_serial;
        active_scope = this;
        if (source == Source::Ready || source == Source::Close) (void)core::safe_copy_bytes(frame, &caller, sizeof(caller));
        facts = observation::capture_native_facts(state.module, FF7RP_TARGET_BUILD_ID, session.generation);
    }
    ~Scope() { retired = true; active_scope = nullptr; }
    bool phase_current(const MenuSessionSnapshot& expected) noexcept
    {
        // Revocation metadata only; the adapter owns the uninterrupted proof window.
        return active_scope == this && serial && !retired && lease && thread == state.owner_thread
            && non_audio_hook_gate().accepting() && !state.compromised.load(std::memory_order_acquire)
            && same_menu_session(session, expected) && same_menu_session(session, state.owner)
            && same_menu_session(session, capture_ready_menu_session())
            && ((source != Source::PostedInput && source != Source::Preparation)
                || (!state.row_rebind.load() && (source == Source::Preparation
                    || (state.focused.load() && focus_epoch == state.focus_epoch.load()))));
    }
    bool qualify(const MenuSessionSnapshot& expected) noexcept
    {
        metrics::Span measured(metrics::Metric::QualifyCallback);
        metrics::Span scope_kind(metrics::Metric::QualifyScope);
        std::uintptr_t current_caller = 0;
        if (retired || active_scope != this || state.compromised.load(std::memory_order_acquire)
            || !lease || thread != GetCurrentThreadId() || !non_audio_hook_gate().accepting()
            || !same_menu_session(session, expected) || !same_menu_session(session, capture_ready_menu_session())) return false;
        if (source == Source::PostedInput || source == Source::Preparation) {
            if (state.row_rebind.load()) return false;
            if ((source == Source::PostedInput && (!state.focused.load() || focus_epoch != state.focus_epoch.load()))
                || !qualify_owned_dispatch(frame, ticket, session, source == Source::Preparation)) return false;
        } else if (source == Source::RowSetup) {
            if (!qualify_owned_row_setup(frame, session, row, index, setup, after)) return false;
        } else if (source == Source::Selection) {
            if (!qualify_owned_selection(frame, session, index, after)) return false;
        } else if (!core::safe_copy_bytes(frame, &current_caller, sizeof(current_caller))
            || !caller || current_caller != caller || !executable_address(state.module, caller)) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(state.module);
        if (source == Source::Ready && caller != base + rva::PianoMenuListOpenCall + 5) return false;
        if (source == Source::Close && caller != base + rva::PianoMenuListCancelCloseCall + 5) return false;
        const auto now = observation::capture_native_facts(state.module, FF7RP_TARGET_BUILD_ID, session.generation);
        return observation::classify_native_facts(facts) == observation::Failure::None
            && observation::classify_native_facts(now) == observation::Failure::None
            && same_menu_session(now.session, session) && now.current_thread == thread
            && now.native_thread == facts.native_thread && now.tls_vector == facts.tls_vector
            && now.tls_block == facts.tls_block && now.package == facts.package
            && now.package_class == facts.package_class && !retired
            && !state.compromised.load(std::memory_order_acquire) && non_audio_hook_gate().accepting()
            && (source != Source::RowSetup || qualify_owned_row_setup(frame, session, row, index, setup, after))
            && (source != Source::Selection || qualify_owned_selection(frame, session, index, after))
            && ((source != Source::PostedInput && source != Source::Preparation)
                || (qualify_owned_dispatch(frame, ticket, session, source == Source::Preparation)
                    && (source == Source::Preparation || (state.focused.load() && focus_epoch == state.focus_epoch.load()))))
            && !retired && !state.compromised.load(std::memory_order_acquire);
    }
    native::CallbackAuthority authority() noexcept
    {
        // A monotonic identity also rejects a saved authority if this stack slot is reused.
        return {session, thread, reinterpret_cast<void*>(serial), [](void* context, const MenuSessionSnapshot& expected) noexcept {
            return active_scope && active_scope->serial == reinterpret_cast<std::uintptr_t>(context)
                && active_scope->phase_current(expected) && active_scope->qualify(expected);
        }, [](void* context, const MenuSessionSnapshot& expected) noexcept {
            return active_scope && active_scope->serial == reinterpret_cast<std::uintptr_t>(context)
                && active_scope->phase_current(expected);
        }};
    }
};

struct Event {
    const char* stage = "none";
    const char* result = "unavailable";
    native::Result bridge{};
    std::uint64_t generation = 0;
    std::uint64_t ticket = 0;
    std::uint64_t setup = 0;
    std::int32_t logical_index = -1;
    std::size_t chosen = 0;
    std::size_t binding = SIZE_MAX;
    bool save = false;
    std::uint64_t elapsed_ms = 0;
    std::size_t cached_ready = 0, cached_known = 0;
    const char* detail = "none";
    const char* row_source = "none";
    std::int32_t previous_logical_index = -1;
    UObjectLiveHandle previous_row{}, current_row{};
    PreparationFacts preparation_facts{};
};
void report(const Event& e) noexcept
{
    // A nested optional callback must not format while its outer native call owns locks.
    if (active_scope || !e.stage || std::string_view(e.stage) == "none") return;
    try {
        std::ostringstream out;
        out << "[menu_theme_runtime] backend=typed_main_song_speed_cursor_palette stage=" << e.stage << " result=" << e.result
            << " generation=" << e.generation << " ticket=" << e.ticket << " setup=" << e.setup
            << " logical_index=" << e.logical_index << " bridge_stage=" << e.bridge.stage
             << " bridge_outcome=" << static_cast<unsigned>(e.bridge.outcome)
             << " mip_header_read=" << e.bridge.mip_header_read
             << " mip_count=" << e.bridge.mip_count << " mip_capacity=" << e.bridge.mip_capacity
             << " theme=" << (e.chosen < state.themes.size() ? state.themes[e.chosen].id : "unavailable")
              << " selector=" << (e.binding != SIZE_MAX && e.binding < state.bindings.size() ? state.bindings[e.binding].name : "none")
              << " detail=" << e.detail
              << " row_source=" << e.row_source << " previous_logical_index=" << e.previous_logical_index
              << " row_identity=" << e.previous_row.internal_index << ':' << e.previous_row.serial_number
              << "->" << e.current_row.internal_index << ':' << e.current_row.serial_number
            << " ownership=" << (state.compromised.load() ? "compromised" : "qualified")
              << " elapsed_ms=" << e.elapsed_ms;
        if (std::string_view(e.stage) == "preparation_progress")
            out << " cached_ready=" << e.cached_ready << " cached_known=" << e.cached_known;
        if (e.preparation_facts.generation) {
            const auto& f = e.preparation_facts;
            out << " preparation_generation=" << f.generation << " preparation_dispatches=" << f.dispatches
                << " preparation_service_ms=" << f.service_ms << " preparation_queue_wait_ms=" << f.queue_wait_ms
                << " capture_original_calls=" << f.capture_calls << " capture_original_backend_ms=" << f.capture_backend_ms
                << " capture_requested_members=" << f.capture_requested_members << " capture_batch_calls=" << f.capture_batch_calls
                << " capture_batch_prepared_members=" << f.batch_prepared_members << " capture_batch_created_members=" << f.batch_created_members
                << " capture_batch_published_members=" << f.batch_published_members
                << " passive_palette_bind_calls=" << f.passive_bind_calls << " passive_palette_bind_backend_ms=" << f.passive_bind_backend_ms;
        }
        out << " full_theme=false";
        core::log(core::LogLevel::Info, out.str());
    } catch (...) {}
}
std::string selector(const plan::Target& target)
{
    static constexpr const char* states[]{"Any", "Normal", "Selected"};
    static constexpr const char* brushes[]{"Primary", "Normal", "Hovered", "Pressed", "Disabled", "Indexed"};
    static constexpr const char* colors[]{"Primary", "Bottom", "Content", "Background", "Outline", "Shadow", "BrushTint"};
    const char* scope = "Unsupported";
    switch (target.scope) {
    case plan::Scope::MusicList: scope = "MusicList"; break;
    case plan::Scope::MusicListCell: scope = "MusicListCell"; break;
    case plan::Scope::SpeedIcon: scope = "SpeedIcon"; break;
    case plan::Scope::Cursor: scope = "Cursor"; break;
    }
    return std::string(scope) + "/" + target.widget + " state=" + states[static_cast<unsigned>(target.state)]
        + " brush=" + brushes[static_cast<unsigned>(target.brush)] + " index=" + std::to_string(target.index)
        + " color=" + colors[static_cast<unsigned>(target.color)];
}
bool non_row_scope(plan::Scope scope) noexcept
{
    return scope == plan::Scope::MusicList || scope == plan::Scope::Cursor;
}
bool matching_row_scope(plan::Scope scope, native::RowKind kind) noexcept
{
    return (scope == plan::Scope::MusicListCell && kind == native::RowKind::Song)
        || (scope == plan::Scope::SpeedIcon && kind == native::RowKind::Speed);
}
std::string backend_rejection(const plan::Program& program)
{
    const auto known = [](plan::Scope scope) { return non_row_scope(scope)
        || scope == plan::Scope::MusicListCell || scope == plan::Scope::SpeedIcon; };
    for (const auto& color : program.colors) if (!known(color.target.scope)) return "unsupported_scope:" + selector(color.target);
    for (const auto& image : program.images) {
        const auto& t = image.target;
        if (!known(t.scope)) return "unsupported_scope:" + selector(t);
        if (t.widget.empty() || t.widget.size() > 127 || !std::all_of(t.widget.begin(), t.widget.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        })) return "EndImage_leaf_requires_ASCII_alnum_underscore:" + selector(t);
    }
    return {};
}
bool same_pixels(const core::ThemeImage& a, const core::ThemeImage& b) noexcept
{
    return a.width == b.width && a.height == b.height && a.bgra == b.bgra;
}
// Worker only. Compilation owns parsing; these tables only plan backend admission
// and a conservative shared native ledger. Nothing is published on rejection.
void note_native_attempt(std::uint64_t bytes) noexcept
{
    if (state.native_attempts != SIZE_MAX) ++state.native_attempts;
    state.native_attempted_bytes += std::min(bytes, UINT64_MAX - state.native_attempted_bytes);
}
bool retain(core::ThemeDefinition&& definition, std::uint64_t& bytes, std::string& reason)
{
    Prepared prepared;
    if (!plan::compile(definition, prepared.program, reason)) return false;
    prepared.id = prepared.program.id;
    prepared.rejection = backend_rejection(prepared.program);
    prepared.backend = prepared.rejection.empty();
    const auto entries = 1 + prepared.program.images.size() + prepared.program.colors.size()
        + static_cast<std::size_t>(std::popcount(static_cast<unsigned>(prepared.program.preview.overrides)));
    if (entries > SIZE_MAX - state.retained_entries) { reason = "compiled_entry_overflow"; return false; }
    std::vector<std::size_t> first_pixels;
    std::vector<plan::Target> names;
    std::vector<Combination> staged;
    std::vector<plan::Target> palettes;
    std::uint64_t extra_bytes = 0;
    for (std::size_t i = 0; i < prepared.program.images.size(); ++i) {
        auto& image = prepared.program.images[i];
        std::size_t resource = 0;
        for (; resource < state.resources.size(); ++resource)
            if (same_pixels(image.pixels, state.resources[resource].pixels)) break;
        if (resource == state.resources.size()) {
            std::size_t j = 0;
            for (; j < first_pixels.size(); ++j)
                if (same_pixels(image.pixels, prepared.program.images[first_pixels[j]].pixels)) break;
            resource += j;
            if (j == first_pixels.size()) { first_pixels.push_back(i); extra_bytes += image.pixels.bgra.size(); }
        }
        prepared.resources.push_back(resource);
    }
    const auto member = [](plan::Target t) { t.state = plan::State::Any; t.color = plan::Color::Primary; return t; };
    const auto binding_for = [&](const plan::Target& target) {
        const auto key = member(target);
        std::size_t i = 0;
        for (; i < state.bindings.size(); ++i) if (state.bindings[i].selector == key) return i;
        const auto it = std::find(names.begin(), names.end(), key);
        i += static_cast<std::size_t>(it - names.begin());
        if (it == names.end()) names.push_back(key);
        return i;
    };
    const auto combination_for_plan = [&](Combination c) {
        const auto equal = [&](const Combination& old) { return old.binding == c.binding && old.resource == c.resource
            && old.tinted == c.tinted && old.base == c.base && old.rgba == c.rgba && old.variant == c.variant; };
        std::size_t i = 0;
        for (; i < state.combinations.size(); ++i) if (equal(state.combinations[i])) return i;
        const auto it = std::find_if(staged.begin(), staged.end(), equal);
        i += static_cast<std::size_t>(it - staged.begin());
        if (it == staged.end()) staged.push_back(c);
        return i;
    };
    std::vector<plan::Target> brush_members;
    if (prepared.backend) {
        const auto add_member = [&](const plan::Target& target) {
            const auto key = member(target);
            if (std::find(brush_members.begin(), brush_members.end(), key) == brush_members.end()) brush_members.push_back(key);
        };
        for (const auto& image : prepared.program.images) add_member(image.target);
        for (const auto& color : prepared.program.colors) if (color.target.color == plan::Color::BrushTint) add_member(color.target);
    }
    for (const auto& target : brush_members) {
        bool stateful = false;
        for (const auto& image : prepared.program.images)
            stateful |= member(image.target) == target && image.target.state != plan::State::Any;
        for (const auto& color : prepared.program.colors)
            stateful |= color.target.color == plan::Color::BrushTint && member(color.target) == target && color.target.state != plan::State::Any;
        for (const auto semantic : {plan::State::Normal, plan::State::Selected}) {
            const bool row = target.scope == plan::Scope::MusicListCell;
            if ((!row || !stateful) && semantic == plan::State::Selected) continue;
            std::size_t image_index = SIZE_MAX, tint_index = SIZE_MAX;
            for (std::size_t i = 0; i < prepared.program.images.size(); ++i) {
                const auto& t = prepared.program.images[i].target;
                if (member(t) != target || (t.state != plan::State::Any && t.state != semantic)) continue;
                if (image_index == SIZE_MAX || t.state != plan::State::Any) image_index = i;
            }
            for (std::size_t i = 0; i < prepared.program.colors.size(); ++i) {
                const auto& t = prepared.program.colors[i].target;
                if (t.color != plan::Color::BrushTint || member(t) != target || (t.state != plan::State::Any && t.state != semantic)) continue;
                if (tint_index == SIZE_MAX || t.state != plan::State::Any) tint_index = i;
            }
            if (image_index == SIZE_MAX && tint_index == SIZE_MAX) continue;
            Combination c; c.binding = binding_for(target); c.resource = SIZE_MAX;
            c.variant = row && stateful ? semantic : plan::State::Any;
            std::size_t combination = SIZE_MAX;
            if (image_index != SIZE_MAX) {
                c.resource = prepared.resources[image_index]; combination = combination_for_plan(c);
            }
            if (tint_index != SIZE_MAX) {
                c.resource = SIZE_MAX; c.base = combination; c.tinted = true;
                c.rgba = prepared.program.colors[tint_index].linear; combination = combination_for_plan(c);
            }
            prepared.combinations.push_back(combination);
        }
    }
    if (prepared.backend) for (std::size_t color_index = 0; color_index < prepared.program.colors.size(); ++color_index) {
        const auto& color = prepared.program.colors[color_index];
        if (color.target.color == plan::Color::BrushTint) continue;
        auto target = color.target; target.state = plan::State::Any;
        std::size_t i = 0;
        for (; i < state.palettes.size(); ++i) if (state.palettes[i].selector == target) break;
        if (i == state.palettes.size()) {
            auto it = std::find(palettes.begin(), palettes.end(), target);
            i += static_cast<std::size_t>(it - palettes.begin());
            if (it == palettes.end()) palettes.push_back(target);
        }
        prepared.palettes.push_back(i);
        prepared.palette_colors.push_back(color_index);
    }
    if (bytes > native::kRetainedByteLimit || extra_bytes > native::kRetainedByteLimit - bytes) {
        reason = "decoded_pixel_memory_budget"; return false;
    }
    const auto old_resources = state.resources.size(), old_bindings = state.bindings.size();
    const auto old_combinations = state.combinations.size(), old_palettes = state.palettes.size();
    try {
        for (auto i : first_pixels) state.resources.push_back({std::move(prepared.program.images[i].pixels)});
        for (auto& image : prepared.program.images) std::vector<std::uint8_t>{}.swap(image.pixels.bgra); // Bank owns pixels, not duplicate capacities.
        for (auto& target : names) {
            Binding binding;
            binding.name = selector(target); binding.leaf.assign(target.widget.begin(), target.widget.end());
            binding.selector = target; state.bindings.push_back(std::move(binding));
        }
        for (const auto& c : staged) state.combinations.push_back(c);
        for (const auto& target : palettes) {
            PaletteBinding binding; binding.selector = target; binding.name = selector(target);
            binding.leaf.assign(target.widget.begin(), target.widget.end()); state.palettes.push_back(std::move(binding));
        }
        if (!prepared.backend) core::log(core::LogLevel::Info, "[menu_theme_runtime] stage=prepare result=backend_pending id="
            + prepared.id + " detail=" + prepared.rejection);
        prepared.song_rows = std::any_of(prepared.program.images.begin(), prepared.program.images.end(), [](const auto& image) { return image.target.scope == plan::Scope::MusicListCell; })
            || std::any_of(prepared.program.colors.begin(), prepared.program.colors.end(), [](const auto& color) { return color.target.scope == plan::Scope::MusicListCell; });
        state.themes.push_back(std::move(prepared));
    } catch (...) {
        state.resources.resize(old_resources); state.bindings.resize(old_bindings);
        state.combinations.resize(old_combinations); state.palettes.resize(old_palettes);
        reason = "inventory_allocation_failed"; return false;
    }
    bytes += extra_bytes; state.retained_entries += entries;
    state.binding_templates = state.bindings.size(); state.combination_templates = state.combinations.size();
    state.palette_templates = state.palettes.size();
    return true;
}
int gesture_key(const core::Config& config) noexcept
{
    const int key = config.theme_function_key;
    if (key < 1 || key > 12 || key == config.autoplay_function_key
        || (config.preview_mode != core::PreviewMode::Off && key == config.preview_function_key)) return 0;
    return key;
}
void clear_owner() noexcept
{
    state.diagnostic_ready_ns = state.diagnostic_ready_generation = 0;
    for (auto& row : state.rows) cancel_row_diagnostic(row);
    retire_song_preview_palette(state.owner.generation);
    state.pending = {};
    state.pending_rows.clear();
    state.row_rebind.store(nullptr);
    state.untracked_row = false;
    state.speed_ready = false;
    state.speed_dirty = false;
    state.work_pending = false; state.subjects_prepared = false;
    state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
    // Foreign terminal callbacks revoke, not race a live setter's validator.
    // Keep contexts intact until their owner thread or the drained uninstall.
    if (state.owner_thread && state.owner_thread != GetCurrentThreadId()
        && (non_audio_hook_gate().accepting() || non_audio_hook_gate().callbacks_in_flight() != 0)) return;
    invalidate_preparation();
    state.subjects.clear();
    // No mutation permission: detach/orphan only. Qualified close retires first.
    if (state.palette_current && state.owner) (void)palette::retire(state.owner.widget, state.owner.generation, {});
    state.palette_current = 0;
    for (auto& p : state.palettes) { p.available = p.resolved = false; p.session = {}; p.subject = {}; p.target = {}; p.canonical = SIZE_MAX; p.shown = {}; }
    state.owner = {}; state.owner_thread = 0; state.applied = 0;
    for (auto& binding : state.bindings) { binding.available = binding.resolved = false; binding.target = {}; binding.subject = {}; binding.original = {}; binding.widget = nullptr; binding.canonical = SIZE_MAX; binding.shown = SIZE_MAX; }
    for (auto& row : state.rows) replace_row(row); // Physical indices survive; no native identity or work does.
    state.preparation_cursor = 1;
    for (auto& combination : state.combinations) { combination.available = false; combination.image = {}; }
    for (auto& theme : state.themes) { theme.available = false; theme.rejected = false; theme.preparing = true; theme.row_repair_only = false; theme.admission_stage = theme.progress_stage = nullptr; }
}
bool validate_palette_subject(void* context, const palette::Subject& subject, const MenuSessionSnapshot& session) noexcept
{
    const auto& p = *static_cast<const PaletteBinding*>(context);
    return !state.compromised.load() && (p.row == SIZE_MAX || state.rows[p.row].available)
        && same_menu_session(p.session, session) && same_menu_session(state.owner, session)
        && p.subject.widget == subject.widget && subject.owner == session.widget && subject.generation == session.generation
        && p.subject.property == subject.property && native::validate_subject(p.target, session);
}
bool palette_property(native::SubjectKind kind, plan::Color color, palette::Property& out) noexcept
{
    using K = native::SubjectKind; using C = plan::Color; using P = palette::Property;
    if (color == C::Background && kind == K::Button) { out = P::ButtonBackground; return true; }
    if (color == C::Bottom && kind == K::TextBlock) { out = P::TextBottom; return true; }
    if (color == C::Outline && kind == K::TextBlock) { out = P::TextOutline; return true; }
    if (color == C::Shadow && kind == K::TextBlock) { out = P::TextShadow; return true; }
    if (color == C::Content && kind == K::Button) { out = P::Button; return true; }
    if (color == C::Content && kind == K::CanvasPanel) { out = P::PanelContent; return true; }
    if (color != C::Primary) return false;
    switch (kind) {
    case K::Image: out = P::Image; break;
    case K::PieImage: out = P::PieImage; break;
    case K::TiledImage: out = P::TiledImage; break;
    case K::ImageSet: out = P::ImageSet; break;
    case K::Button: out = P::Button; break;
    case K::CanvasPanel: out = P::PanelContent; break;
    case K::TextBlock: out = P::Text; break;
    default: return false;
    }
    return true;
}
const native::RowTicket& row_ticket(std::size_t row) noexcept
{
    static const native::RowTicket main{};
    return row == SIZE_MAX ? main : state.rows[row].ticket;
}
template<class Compatible>
native::Result resolve_current_subject(std::wstring_view leaf, std::size_t row, plan::Scope target_scope, Scope& scope,
    Compatible compatible, bool& kind_known, native::SubjectKind& kind,
    native::Subject& out, void*& widget) noexcept
{
    metrics::Span measured(metrics::Metric::Resolve);
    try {
        const auto authority = scope.authority();
        const auto* association = row == SIZE_MAX ? nullptr : &state.rows[row];
        const auto receiver = association ? association->row : scope.session.widget;
        const auto identity = association ? association->identity : scope.session.widget_identity;
        const auto setup = association ? association->association_setup : 0;
        const auto epoch = association ? association->membership_epoch : 0;
        auto& subjects = row == SIZE_MAX ? state.subjects : state.rows[row].subjects;
        for (auto i = subjects.begin(); i != subjects.end();) {
            if (i->row != row || i->leaf != leaf || i->scope != target_scope) { ++i; continue; }
            if (!same_menu_session(i->session, scope.session) || i->receiver != receiver
                || i->identity.internal_index != identity.internal_index || i->identity.serial_number != identity.serial_number
                || i->setup != setup || i->epoch != epoch) { i = subjects.erase(i); continue; }
            const auto cached_kind = i->kind;
            out = i->subject; // Borrow no cache reference across a possibly reentrant backend call.
            const auto result = native::access_subject(out, authority, widget);
            if (!scope.qualify(scope.session)) return {native::Outcome::Authority, "shared_subject_retired"};
            if (!result) {
                subjects.erase(i);
                // A replacement is not permission to recapture a possibly styled
                // endpoint. A genuine rebind/new owner must establish its baseline.
                return result;
            }
            if (!compatible(cached_kind)) return {native::Outcome::Identity, "shared_subject_incompatible_property"};
            kind = cached_kind; kind_known = true;
            return result;
        }
        auto result = native::Result{native::Outcome::Identity, "no_compatible_current_subject"};
        const auto hinted_kind = kind;
        const bool hinted = kind_known && compatible(hinted_kind);
        const auto find = [&](native::SubjectKind candidate) {
            result = target_scope == plan::Scope::Cursor
                ? native::find_cursor_subject(leaf, authority, out, candidate)
                : native::find_subject(leaf, candidate, row_ticket(row), authority, out);
            if (!scope.qualify(scope.session)) { result = {native::Outcome::Authority, "subject_lookup_retired"}; return false; }
            if (result) {
                result = native::access_subject(out, authority, widget);
                if (!scope.qualify(scope.session)) result = {native::Outcome::Authority, "subject_access_retired"};
                if (result) {
                    kind = candidate; kind_known = true;
                    subjects.push_back({std::wstring(leaf), row, scope.session, identity, receiver, setup, epoch, candidate, out, target_scope});
                }
            }
            return static_cast<bool>(result);
        };
        if (hinted && find(hinted_kind)) return result;
        if (hinted && result.outcome != native::Outcome::Identity) return result;
        for (const auto candidate : {native::SubjectKind::Image, native::SubjectKind::PieImage, native::SubjectKind::TiledImage,
            native::SubjectKind::ImageSet, native::SubjectKind::Button, native::SubjectKind::TextBlock, native::SubjectKind::CanvasPanel}) {
            if (!compatible(candidate) || (hinted && candidate == hinted_kind)) continue;
            if (find(candidate) || result.outcome != native::Outcome::Identity) return result;
        }
        return result;
    } catch (...) { return {native::Outcome::Memory, "shared_subject_allocation"}; }
}
void evict_invalid_subject(std::wstring_view leaf, std::size_t row, plan::Scope target_scope, const native::Subject& subject, Scope& scope) noexcept
{
    if (!scope.qualify(scope.session) || native::validate_subject(subject, scope.session)) return;
    auto& subjects = row == SIZE_MAX ? state.subjects : state.rows[row].subjects;
    std::erase_if(subjects, [&](const SubjectLookup& cached) { return cached.row == row && cached.leaf == leaf && cached.scope == target_scope; });
    // Keep existing owning originals/source contexts unchanged. Only an actual
    // rebind/new generation may discard those and capture a replacement baseline.
}
native::Result prepare_palette(std::size_t index, Scope& scope, bool bind = true) noexcept
{
    auto& p = state.palettes[index];
    if (!state.palette_installed || !native::capabilities().subjects)
        return {native::Outcome::Unavailable, "palette_subject_backend"};
    const auto authority = scope.authority();
    if (p.resolved) {
        if (!bind) return {native::Outcome::Ready, "palette_subject_resolved"};
        p.canonical = index;
        p.subject.context = &p;
        for (const auto i : IndexRange{state.palettes.size(), p.row == SIZE_MAX ? nullptr : &state.rows[p.row].inventory.palettes}) if (i != index && state.palettes[i].available
            && state.palettes[i].row == p.row && state.palettes[i].selector.scope == p.selector.scope && state.palettes[i].subject.widget == p.subject.widget
            && state.palettes[i].subject.property == p.subject.property) {
            p.canonical = state.palettes[i].canonical; return state.palettes[i].result;
        }
        if (state.palette_charged != SIZE_MAX) ++state.palette_charged;
        const auto result = measured_backend(false, [&] { return palette::bind(p.subject, {}, authority); });
        if (result) ++state.palette_current;
        else evict_invalid_subject(p.leaf, p.row, p.selector.scope, p.target, scope);
        return result;
    }
    {
        palette::Property property{};
        void* widget = nullptr;
        if (!p.kind_known && p.prototype != SIZE_MAX && state.palettes[p.prototype].kind_known) {
            p.kind = state.palettes[p.prototype].kind; p.kind_known = true;
        }
        auto result = resolve_current_subject(p.leaf, p.row, p.selector.scope, scope,
            [&](native::SubjectKind candidate) { palette::Property value{}; return palette_property(candidate, p.selector.color, value); },
            p.kind_known, p.kind, p.target, widget);
        if (!result) return result;
        if (!palette_property(p.kind, p.selector.color, property)) return {native::Outcome::Identity, "incompatible_palette_property"};
        if (p.prototype != SIZE_MAX) { state.palettes[p.prototype].kind = p.kind; state.palettes[p.prototype].kind_known = true; }
        p.session = scope.session; p.subject = {widget, scope.session.widget, scope.session.generation,
            property, &p, validate_palette_subject};
        p.canonical = index;
        p.resolved = true;
        if (!bind) return {native::Outcome::Ready, "palette_subject_resolved"};
        for (const auto i : IndexRange{state.palettes.size(), p.row == SIZE_MAX ? nullptr : &state.rows[p.row].inventory.palettes}) if (i != index && state.palettes[i].available
            && state.palettes[i].row == p.row && state.palettes[i].selector.scope == p.selector.scope
            && state.palettes[i].subject.widget == widget && state.palettes[i].subject.property == property) {
            p.canonical = state.palettes[i].canonical; return state.palettes[i].result;
        }
        if (state.palette_charged != SIZE_MAX) ++state.palette_charged; // Attempt diagnostic, not backend admission.
        result = measured_backend(false, [&] { return palette::bind(p.subject, {}, authority); });
        if (result) ++state.palette_current;
        else evict_invalid_subject(p.leaf, p.row, p.selector.scope, p.target, scope);
        return result;
    }
}
native::Result capture_subject(Binding& binding, Scope& scope) noexcept
{
    const native::ResolveMember request{&binding.subject, binding.slot};
    native::ResolvedMember output{}; native::BatchProgress progress{};
    const auto result = native::resolve_members(std::span(&request, 1), scope.authority(), std::span(&output, 1), progress);
    if (!scope.qualify(scope.session)) return {native::Outcome::Authority, "capture_resolver_retired"};
    if (result && progress.completed != 1) return {native::Outcome::Input, "capture_resolver_incomplete"};
    if (result) binding.original = output.original;
    return result;
}
native::Result capture_binding(Binding& binding, Scope& scope, bool capture = true) noexcept
{
    const auto authority = scope.authority();
    if (binding.resolved) {
        if (!capture) return {native::Outcome::Ready, "brush_subject_resolved"};
        note_native_attempt(native::kOriginalCaptureByteCharge);
        const auto result = measured_backend(true, [&] { return binding.generic
            ? capture_subject(binding, scope)
            : native::capture_original(binding.target, authority, binding.original); });
        if (!result && binding.generic) evict_invalid_subject(binding.leaf, binding.row, binding.selector.scope, binding.subject, scope);
        return result;
    }
    binding.generic = native::capabilities().subjects;
    if (!binding.generic) {
        if (binding.selector.scope == plan::Scope::Cursor) return {native::Outcome::Unavailable, "cursor_subject_backend"};
        if (binding.row != SIZE_MAX) return {native::Outcome::Unavailable, "row_subject_backend"};
        if (binding.selector.brush != plan::Brush::Primary) return {native::Outcome::Unavailable, "generic_brush_backend"};
        auto result = [&] {
            metrics::Span resolve(metrics::Metric::Resolve);
            return native::find_image(binding.leaf, authority, binding.target);
        }();
        if (!result) return result;
        binding.resolved = true;
        if (!capture) return result;
        note_native_attempt(native::kOriginalCaptureByteCharge);
        return measured_backend(true, [&] { return native::capture_original(binding.target, authority, binding.original); });
    }
    binding.slot = {};
    if (binding.selector.brush == plan::Brush::Indexed) binding.slot.index = binding.selector.index;
    else if (binding.selector.brush != plan::Brush::Primary)
        binding.slot.button = static_cast<native::ButtonBrush>(static_cast<unsigned>(binding.selector.brush) - 1);
    const auto compatible = [&](native::SubjectKind kind) {
        const auto brush = binding.selector.brush;
        return kind != native::SubjectKind::TextBlock && kind != native::SubjectKind::CanvasPanel
            && !((brush == plan::Brush::Indexed && kind != native::SubjectKind::ImageSet)
            || (brush != plan::Brush::Indexed && kind == native::SubjectKind::ImageSet)
            || (brush != plan::Brush::Primary && brush != plan::Brush::Indexed && kind != native::SubjectKind::Button));
    };
    if (!binding.kind_known && binding.prototype != SIZE_MAX && state.bindings[binding.prototype].kind_known) {
        binding.kind = state.bindings[binding.prototype].kind; binding.kind_known = true;
    }
    const auto result = resolve_current_subject(binding.leaf, binding.row, binding.selector.scope, scope, compatible,
        binding.kind_known, binding.kind, binding.subject, binding.widget);
    if (!result) return result;
    binding.resolved = true;
    if (binding.prototype != SIZE_MAX) { state.bindings[binding.prototype].kind = binding.kind; state.bindings[binding.prototype].kind_known = true; }
    if (!capture) return result;
    note_native_attempt(native::kOriginalCaptureByteCharge);
    const auto captured = measured_backend(true, [&] { return capture_subject(binding, scope); });
    if (!captured) evict_invalid_subject(binding.leaf, binding.row, binding.selector.scope, binding.subject, scope);
    return captured;
}
bool same_capture_subject(const Binding& a, const Binding& b) noexcept
{
    // Current-owner resolution supplied the endpoint and actual row association;
    // the backend still revalidates opaque Subject/member affinity in this call.
    return a.generic && b.generic && a.resolved && b.resolved && a.widget == b.widget
        && a.kind == b.kind && a.row == b.row && a.selector.scope == b.selector.scope;
}
native::Result capture_members(const std::vector<std::size_t>& indices, Scope& scope) noexcept
{
    if (indices.empty()) return {native::Outcome::Input, "empty_capture_members"};
    try {
        std::vector<native::BrushSelection> members;
        std::vector<native::ResolvedMember> outputs(indices.size());
        std::vector<native::ResolveMember> requests;
        members.reserve(indices.size());
        const auto& subject = state.bindings[indices.front()];
        for (const auto i : indices) {
            const auto& b = state.bindings[i];
            if (!same_capture_subject(subject, b)) return {native::Outcome::Identity, "capture_subject_mismatch"};
            if (std::any_of(members.begin(), members.end(), [&](const auto& member) {
                return member.index == b.slot.index && member.button == b.slot.button;
            })) return {native::Outcome::Input, "duplicate_capture_member"};
            members.push_back(b.slot);
            requests.push_back({&subject.subject, b.slot});
        }
        native::BatchProgress progress{};
        const auto generation = state.preparation_facts.generation;
        for (std::size_t i = 0; i < members.size(); ++i) note_native_attempt(native::kOriginalCaptureByteCharge);
        const auto result = measured_backend(true, [&] {
            return native::resolve_members(requests, scope.authority(), outputs, progress);
        }, members.size());
        if (state.preparation_facts.generation == generation) {
            auto& facts = state.preparation_facts;
            add_fact(facts.capture_batch_calls, 1);
            add_fact(facts.batch_prepared_members, progress.completed);
            add_fact(facts.batch_created_members, progress.created);
            add_fact(facts.batch_published_members, result ? members.size() : 0);
        }
        // Progress is diagnostic only: no partial outputs, retry, or publication
        // after retirement, including a nominally successful reentrant backend.
        if (!scope.qualify(scope.session)) return {native::Outcome::Authority, "capture_members_retired"};
        if (!result) return result;
        if (progress.completed != members.size() || progress.created > progress.completed)
            return {native::Outcome::Input, "capture_members_incomplete"};
        for (std::size_t n = 0; n < indices.size(); ++n) {
            auto& b = state.bindings[indices[n]];
            b.original = outputs[n].original; b.canonical = indices[n]; b.shown = SIZE_MAX;
            b.result = result; b.available = true;
        }
        return result;
    } catch (...) { return {native::Outcome::Memory, "capture_members_allocation"}; }
}
struct SubjectChecks {
    struct Group {
        const native::Subject* subject = nullptr;
        std::vector<native::BrushAssignment> images;
    };
    std::vector<Group> groups;
    using ReceiverKey = std::tuple<std::uintptr_t, native::SubjectKind, std::size_t, plan::Scope>;
    std::map<ReceiverKey, std::size_t> receivers;
    static ReceiverKey key(const Binding& b) noexcept {
        return {reinterpret_cast<std::uintptr_t>(b.widget), b.kind, b.row, b.selector.scope};
    }
    bool brush(const Binding& b, const native::OwnedImage& image) noexcept {
        try {
            if (!receiver(b)) return false;
            groups[receivers.at(key(b))].images.push_back({b.slot, &image}); return true;
        } catch (...) { return false; }
    }
    bool property(const native::Subject& subject) noexcept {
        try { groups.push_back({&subject, {}}); return true; } catch (...) { return false; }
    }
    bool receiver(const Binding& binding) noexcept {
        try {
            if (!binding.generic || !binding.resolved) return false;
            const auto receiver_key = key(binding);
            if (!receivers.contains(receiver_key)) {
                groups.push_back({&binding.subject, {}});
                receivers.emplace(receiver_key, groups.size() - 1);
            }
            return true;
        } catch (...) { return false; }
    }
    native::Result run(Scope& scope) noexcept {
        try {
            std::vector<native::SubjectPreflight> requests;
            requests.reserve(std::min(groups.size(), native::kSubjectTraversalLimit));
            std::size_t images = 0;
            const auto flush = [&]() -> native::Result {
                if (requests.empty()) return {native::Outcome::Ready, "subjects_preflighted"};
                native::BatchProgress progress{};
                const auto result = native::preflight_subjects(requests, scope.authority(), progress);
                if (!scope.qualify(scope.session)) return {native::Outcome::Authority, "subject_preflight_retired"};
                if (!result) return result;
                if (progress.completed != requests.size()) return {native::Outcome::Input, "subject_preflight_incomplete"};
                requests.clear(); images = 0;
                return result;
            };
            for (const auto& group : groups) {
                std::size_t offset = 0;
                do {
                    if (requests.size() == native::kSubjectTraversalLimit || images == native::kSubjectTraversalLimit) {
                        const auto result = flush(); if (!result) return result;
                    }
                    const auto count = std::min(group.images.size() - offset, native::kSubjectTraversalLimit - images);
                    requests.push_back({group.subject, std::span(group.images).subspan(offset, count)});
                    offset += count; images += count;
                } while (offset < group.images.size());
            }
            return flush();
        } catch (...) { return {native::Outcome::Memory, "subject_preflight_allocation"}; }
    }
};
bool current_subject(const native::Subject& subject, Scope& scope) noexcept
{
    const native::SubjectPreflight request{&subject, {}}; native::BatchProgress progress{};
    const auto result = native::preflight_subjects(std::span(&request, 1), scope.authority(), progress);
    return result && progress.completed == 1 && scope.qualify(scope.session);
}
native::Result resolve_combinations(PreparationWork& work, std::size_t first, Scope& scope) noexcept
{
    try {
        const auto& prototype = state.combinations[first];
        const auto& receiver = state.bindings[prototype.binding];
        std::vector<std::size_t> indices;
        std::vector<native::ResolveMember> requests;
        for (const auto index : IndexRange{state.combination_templates,
            receiver.row == SIZE_MAX ? nullptr : &state.rows[receiver.row].inventory.combinations}) {
            ++work.visits;
            if (index < first || !work.combinations.contains(index)) continue;
            const auto& c = state.combinations[index]; const auto& b = state.bindings[c.binding];
            if (c.available || !same_capture_subject(receiver, b)) continue;
            const auto& original = state.bindings[b.canonical].original;
            auto resource = c.resource;
            if (c.tinted && c.base != SIZE_MAX) {
                const auto& base = state.combinations[c.base];
                if (base.tinted || base.binding != c.binding) return {native::Outcome::Input, "composition_resolver_base"};
                resource = base.resource;
            }
            if (c.tinted && !native::capabilities().brush_tint) return {native::Outcome::Unavailable, "brush_tint_backend"};
            requests.push_back({&receiver.subject, b.slot, &original,
                resource == SIZE_MAX ? nullptr : &state.resources[resource].image, c.tinted, c.rgba});
            indices.push_back(index);
            if (requests.size() == native::kSubjectTraversalLimit) break;
        }
        if (requests.empty()) return {native::Outcome::Input, "composition_dependencies_unavailable"};
        std::vector<native::ResolvedMember> outputs(requests.size());
        native::BatchProgress progress{};
        for (std::size_t n = 0; n < requests.size(); ++n) note_native_attempt(native::kOriginalCaptureByteCharge);
        // The backend measures the separate capture/compose/tint phases; this
        // mixed member group must not charge its entire duration to one phase.
        const auto result = native::resolve_members(requests, scope.authority(), outputs, progress);
        if (!scope.qualify(scope.session)) return {native::Outcome::Authority, "composition_resolver_retired"};
        if (!result) return result;
        if (progress.completed != requests.size()) return {native::Outcome::Input, "composition_resolver_incomplete"};
        for (std::size_t n = 0; n < indices.size(); ++n) {
            auto& c = state.combinations[indices[n]]; c.image = outputs[n].image; c.result = result; c.available = true;
        }
        return result;
    } catch (...) { return {native::Outcome::Memory, "composition_resolver_allocation"}; }
}
native::Result assign_binding(Binding& binding, const native::OwnedImage& image, const native::CallbackAuthority& authority) noexcept
{
    return binding.generic ? native::apply(binding.subject, binding.slot, image, authority)
        : native::apply(binding.target, image, authority);
}
void failed(Event& e, const char* stage, native::Result result = {}) noexcept
{
    state.compromised.store(true, std::memory_order_release);
    clear_owner(); // Revocation only: bridge roots/budgets are never cleaned up here.
    e.stage = stage; e.result = "unavailable"; e.bridge = result;
}
auto append_row() noexcept -> std::deque<RowRecord>::iterator
{
    try { state.rows.emplace_back(); return std::prev(state.rows.end()); }
    catch (...) { return state.rows.end(); }
}
bool invalidate_song_row(std::size_t r) noexcept
{
    try {
        auto& row = state.rows[r];
        row.work = {}; row.prepared_choice = SIZE_MAX;
        for (std::size_t i = 1; i < state.themes.size(); ++i) {
            auto& theme = state.themes[i];
            if (!theme.song_rows) continue;
            if (i == state.applied) theme.row_repair_only |= theme.available;
            theme.available = false; theme.preparing = true;
            // Retain unrelated expansion, uploads and owning work. Revisit only
            // this row's slots before a pending whole candidate may be admitted.
            auto& work = theme.work;
            if (work.revision && i != state.applied) {
                work.changed_rows.insert(r);
                if (!row.inventory.bindings.empty()) {
                    const auto first = row.inventory.bindings.front();
                    work.resolve_binding = std::min(work.resolve_binding, first);
                    work.capture = std::min(work.capture, first);
                }
                if (!row.inventory.combinations.empty()) work.compose = std::min(work.compose, row.inventory.combinations.front());
                if (!row.inventory.palettes.empty()) {
                    const auto first = row.inventory.palettes.front();
                    work.resolve_palette = std::min(work.resolve_palette, first);
                    work.bind = std::min(work.bind, first);
                }
            }
        }
        if (state.applied && state.themes[state.applied].song_rows) state.pending_rows.insert(r);
        update_work_pending();
        return true;
    } catch (...) { return false; }
}
void observe_row_setup(const void* frame, void* context, void* row, std::int32_t index,
    std::uint64_t setup, bool after, const core::HookCallbackGate::Lease& lease, RowSetupSource source) noexcept
{
    metrics::Span measured(after ? metrics::Metric::RowAfter : metrics::Metric::RowBefore);
    const auto started = GetTickCount64(); // This callback only; excludes the intervening native setup original.
    callback_entered();
    if (active_scope) return;
    if (!lease || !row || index < 0 || !setup || state.compromised.load()) return;
    const auto session = capture_menu_callback_session();
    metrics::annotate({session.generation});
    if (!session || !context || (session.widget && context != session.widget)
        || !qualify_owned_row_setup(frame, session, row, index, setup, after)) return;
    Event event; event.stage = "row_association"; event.generation = session.generation; event.setup = setup; event.logical_index = index;
    event.row_source = source == RowSetupSource::OwnedRefresh ? "owned_refresh" : "native_setup";
    std::vector<Event> diagnostics;
    std::unique_lock lock(state.mutex);
    auto record = std::find_if(state.rows.begin(), state.rows.end(), [&](const RowRecord& r) { return r.row == row && r.kind == native::RowKind::Song; });
    if (!after && record != state.rows.end()) metrics::annotate({session.generation, setup, record->identity.internal_index, index});
    if (record != state.rows.end()) { event.previous_logical_index = record->index; event.previous_row = record->identity; }
    if (!after) {
        if (session.phase == MenuSessionPhase::Ready && same_menu_session(session, state.owner)) {
            if (state.row_rebind.load() && state.row_rebind.load() != frame) failed(event, "nested_row_rebind");
            else {
                state.row_rebind.store(frame); event.ticket = state.pending.ticket; state.pending = {};
                Scope scope(Source::RowSetup, frame, lease, session);
                scope.row = row; scope.index = index; scope.setup = setup; scope.after = false;
                if (!scope.qualify(session)) failed(event, "row_retire_scope");
                else if (state.applied && record != state.rows.end() && !record->style_pending && record->index == index && current_song_association(static_cast<std::size_t>(record - state.rows.begin()), scope)) {
                    metrics::count(metrics::Counter::SameAssociationRefreshes);
                    const auto r = static_cast<std::size_t>(record - state.rows.begin());
                    if (!suspend_song_style(r, scope, event)) failed(event, "refresh_suspend_failed", event.bridge);
                    else {
                        record->refresh_invocation = setup; record->refresh_elapsed_ms = GetTickCount64() - started;
                        event.stage = "none"; // One paired refresh outcome, excluding native original time.
                    }
                }
                else if (!scope.qualify(session)) failed(event, "row_association_probe_retired");
                else if (record != state.rows.end()) (void)retire_row(static_cast<std::size_t>(record - state.rows.begin()), scope, event);
                else event.result = "new_row_rebind_started";
            }
        } else if (state.owner) failed(event, "row_rebind_without_retired_menu_owner");
        else if (record != state.rows.end()) { cancel_row_diagnostic(*record); replace_row(*record); }
        if (!state.compromised.load()) {
            if (record != state.rows.end()) record->refresh_elapsed_ms = GetTickCount64() - started;
            event.stage = "none"; // Successful before/after pair emits only its final outcome.
        }
        event.elapsed_ms = GetTickCount64() - started; lock.unlock(); report(event);
        return;
    }
    if (session.phase == MenuSessionPhase::Ready && state.row_rebind.load() != frame) return;
    if (record != state.rows.end() && record->refresh_invocation == setup) {
        metrics::annotate({session.generation, setup, record->identity.internal_index, index});
        const auto elapsed = record->refresh_elapsed_ms;
        record->refresh_invocation = 0;
        event.current_row = record->identity; event.chosen = state.applied;
        {
            Scope scope(Source::RowSetup, frame, lease, session);
            scope.row = row; scope.index = index; scope.setup = setup; scope.after = true;
            if (!scope.qualify(session) || record->index != index || !current_song_association(static_cast<std::size_t>(record - state.rows.begin()), scope))
                failed(event, "refresh_association_changed_after_original");
            else if (!finish_song_refresh(static_cast<std::size_t>(record - state.rows.begin()), scope, event))
                failed(event, "refresh_baseline_or_reapply_failed", event.bridge);
            else { event.stage = "row_association"; event.result = "same_association_refreshed"; event.detail = "refresh"; }
        }
        if (state.row_rebind.load() == frame) state.row_rebind.store(nullptr);
        event.elapsed_ms = elapsed + GetTickCount64() - started;
        lock.unlock(); report(event); return;
    }
    const auto before_elapsed = record != state.rows.end() ? record->refresh_elapsed_ms : 0;
    if (record == state.rows.end()) record = std::find_if(state.rows.begin(), state.rows.end(), [](const RowRecord& r) { return !r.row; });
    if (record == state.rows.end()) record = append_row();
    if (record == state.rows.end()) { event.result = "row_context_allocation_failed"; state.untracked_row = true; }
    else {
        cancel_row_diagnostic(*record);
        UObjectLiveHandle identity{}, context_identity{};
        if (!capture_live_uobject_handle(row, identity) || !capture_live_uobject_handle(context, context_identity))
            { replace_row(*record, {row, {}, session, index, setup}); record->failed = true; event.result = "positive_row_or_context_identity_unavailable"; }
        else {
            event.current_row = identity;
            replace_row(*record, {row, identity, session, index, setup});
            record->context = context; record->context_identity = context_identity;
            metrics::annotate({session.generation, setup, identity.internal_index, index});
            metrics::count(metrics::Counter::RowRebinds);
            pending_row_diagnostic(*record);
            event.result = "opening_association_no_mutation";
            if (session.phase == MenuSessionPhase::Ready && same_menu_session(session, state.owner)) {
                Scope scope(Source::RowSetup, frame, lease, session);
                scope.row = row; scope.index = index; scope.setup = setup; scope.after = true;
                if (!scope.qualify(session)) failed(event, "row_scope");
                else {
                    event.result = "active_song_row_preparation_scheduled";
                }
            }
        }
    }
    if (session.phase == MenuSessionPhase::Ready && same_menu_session(session, state.owner) && !state.compromised.load()) {
        Scope scope(Source::RowSetup, frame, lease, session);
        scope.row = row; scope.index = index; scope.setup = setup; scope.after = true;
        if (!scope.qualify(session)) failed(event, "row_publish_scope");
        else {
            if (record != state.rows.end()) record->style_pending = true;
            if (record != state.rows.end() && !invalidate_song_row(static_cast<std::size_t>(record - state.rows.begin()))) {
                failed(event, "row_work_allocation_failed");
            }
            if (state.desired == SIZE_MAX && state.applied && state.themes[state.applied].song_rows) {
                state.desired = state.applied; state.desired_save = false; state.desired_epoch = state.focus_epoch.load();
            }
            update_work_pending();
            // No whole-menu fallback on an ordinary rebind. Other physical rows/main
            // remain styled; only the restored native row waits for fresh affinity.
            const auto r = record == state.rows.end() ? SIZE_MAX : static_cast<std::size_t>(record - state.rows.begin());
            if (state.applied && r != SIZE_MAX && prepare_quantum(scope, event, &diagnostics, SIZE_MAX, r) && !state.compromised.load()) {
                if (!repair_applied_rows(scope, event, r)) {
                    if (!state.compromised.load() && !apply_choice(scope, 0, event, false)) failed(event, "row_repair_restore_failed");
                }
                const auto current = state.applied;
                if (state.compromised.load()) event.result = "row_repair_retired";
                else if (state.themes[current].available) {
                    if (current) event.result = "row_repaired_committed_program";
                }
                else if (!state.themes[current].preparing) {
                    if (!apply_choice(scope, 0, event, false)) failed(event, "row_rejected_original_restore_failed");
                    else { state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false; event.result = "row_rejected_original_restored"; }
                } else { event.stage = "row_association"; event.result = "row_delta_preparation_scheduled"; }
            } else if (state.applied && !state.compromised.load()) {
                event.stage = "row_association"; event.result = "row_delta_preparation_scheduled";
            }
        }
    }
    if (state.row_rebind.load() == frame) state.row_rebind.store(nullptr);
    event.elapsed_ms = before_elapsed + GetTickCount64() - started; event.detail = "rebind"; lock.unlock();
    for (const auto& diagnostic : diagnostics) report(diagnostic);
    report(event);
}
void observe_row_retired(const void* frame) noexcept
{
    if (state.row_rebind.load() == frame) retire(); // Actual owning frame unwound without a completed post-original bind.
}
void observe_selection(const void* frame, void* context, std::int32_t index, bool after,
    const core::HookCallbackGate::Lease& lease) noexcept
{
    metrics::Span measured(after ? metrics::Metric::SelectionAfter : metrics::Metric::SelectionBefore);
    callback_entered();
    if (active_scope) return;
    if (!lease || state.compromised.load()) return;
    const auto session = capture_ready_menu_session();
    if (context != session.widget || !qualify_owned_selection(frame, session, index, after)) return;
    metrics::annotate({session.generation, 0, -1, index});
    std::unique_lock lock(state.mutex);
    if (!same_menu_session(session, state.owner)) return;
    Event event; event.stage = "semantic_selection"; event.generation = session.generation; event.logical_index = index;
    {
        Scope scope(Source::Selection, frame, lease, session); scope.index = index; scope.after = after;
        if (scope.qualify(session)) {
            const bool changed = state.semantic_selected != index;
            // Worklists include both semantic variants. Selection changes the
            // chosen policy/member, not receiver membership or dependencies.
            state.semantic_selected = index;
            event.result = state.applied ? "selection_recorded" : "selection_recorded_original";
            if (!state.row_rebind.load()) {
                if (after) {
                    if (state.subjects_prepared) (void)reconcile_speed(scope, event, false);
                    if (state.speed_dirty) {
                        if (state.applied && !state.themes[state.applied].available && state.desired == SIZE_MAX) {
                            state.desired = state.applied; state.desired_save = state.desired_gesture = false;
                            state.desired_epoch = state.focus_epoch.load();
                        }
                        update_work_pending();
                    }
                }
                if (changed && state.applied && !state.compromised.load()
                    && !apply_choice(scope, state.applied, event, false, Transition::Delta) && !state.compromised.load()) {
                    if (!apply_choice(scope, 0, event, false)) failed(event, "selection_original_restore_failed");
                    else event.result = "selection_program_rejected_original_restored";
                }
                else if (!changed && after) event.stage = "none";
            }
            else event.result = "selection_recorded_during_row_rebind";
        } else failed(event, "semantic_selection_scope");
    }
    lock.unlock(); report(event);
}
// Callback-local projection: never publish a partial lookup or retain canonical
// membership across a native rebind, baseline refresh, rejection, or reopen.
struct ChoiceLookup {
    std::vector<std::array<std::size_t, 2>> brushes, colors;
    std::map<std::size_t, std::array<std::size_t, 2>> row_brushes, row_colors;
    bool restricted = false;
    std::uint64_t visits = 0;
    bool build(std::size_t choice, std::size_t physical_row = SIZE_MAX) noexcept {
        try {
            brushes.clear(); colors.clear(); row_brushes.clear(); row_colors.clear(); visits = 0;
            restricted = physical_row != SIZE_MAX;
            if (!choice) return true;
            if (!restricted) {
                brushes.assign(state.bindings.size(), {SIZE_MAX, SIZE_MAX});
                colors.assign(state.palettes.size(), {SIZE_MAX, SIZE_MAX});
                visits = state.bindings.size() + state.palettes.size();
            }
            const auto& theme = state.themes[choice];
            std::vector<std::size_t> brush_rank(restricted ? 0 : state.combination_templates, SIZE_MAX);
            std::vector<std::array<std::size_t, 2>> color_rank(restricted ? 0 : state.palette_templates, {SIZE_MAX, SIZE_MAX});
            std::map<std::size_t, std::size_t> row_brush_rank;
            std::map<std::size_t, std::array<std::size_t, 2>> row_color_rank;
            visits += brush_rank.size() + color_rank.size();
            for (std::size_t n = 0; n < theme.combinations.size(); ++n) {
                ++visits;
                if (restricted) row_brush_rank[theme.combinations[n]] = n;
                else brush_rank[theme.combinations[n]] = n;
            }
            const auto rank = [&](std::size_t i) {
                const auto prototype = i < state.combination_templates ? i : state.combinations[i].prototype;
                if (!restricted) return brush_rank[prototype];
                const auto found = row_brush_rank.find(prototype);
                return found == row_brush_rank.end() ? SIZE_MAX : found->second;
            };
            for (const auto i : IndexRange{state.combinations.size(), restricted ? &state.rows[physical_row].inventory.combinations : nullptr}) {
                ++visits;
                const auto& c = state.combinations[i];
                if (rank(i) == SIZE_MAX) continue;
                const auto canonical = state.bindings[c.binding].canonical;
                if (canonical == SIZE_MAX) continue;
                const auto row = state.bindings[canonical].row;
                if (row == SIZE_MAX ? !non_row_scope(state.bindings[c.binding].selector.scope) : c.row != row) continue;
                for (std::size_t s = 0; s < 2; ++s) {
                    const auto semantic = row != SIZE_MAX && state.rows[row].kind == native::RowKind::Speed ? plan::State::Any
                        : s ? plan::State::Selected : plan::State::Normal;
                    if (row != SIZE_MAX && c.variant != semantic && c.variant != plan::State::Any) continue;
                    auto& prior = restricted ? row_brushes.try_emplace(canonical, std::array<std::size_t, 2>{SIZE_MAX, SIZE_MAX}).first->second[s] : brushes[canonical][s];
                    if (prior == SIZE_MAX || rank(i) < rank(prior)) prior = i;
                }
            }
            const auto prefer_color = [&](std::size_t n, std::size_t prior) {
                if (prior == SIZE_MAX) return true;
                const auto semantic = theme.program.colors[theme.palette_colors[n]].target.state;
                const bool prior_any = theme.program.colors[theme.palette_colors[prior]].target.state == plan::State::Any;
                return semantic == plan::State::Any ? prior_any && n < prior : prior_any || n > prior;
            };
            for (std::size_t n = 0; n < theme.palettes.size(); ++n) {
                ++visits;
                const auto semantic = theme.program.colors[theme.palette_colors[n]].target.state;
                for (std::size_t s = 0; s < 2; ++s) {
                    if (semantic != plan::State::Any && semantic != (s ? plan::State::Selected : plan::State::Normal)) continue;
                    auto& prior = restricted ? row_color_rank.try_emplace(theme.palettes[n], std::array<std::size_t, 2>{SIZE_MAX, SIZE_MAX}).first->second[s] : color_rank[theme.palettes[n]][s];
                    if (prefer_color(n, prior)) prior = n;
                }
            }
            for (const auto i : IndexRange{state.palettes.size(), restricted ? &state.rows[physical_row].inventory.palettes : nullptr}) {
                ++visits;
                const auto& p = state.palettes[i];
                if (p.canonical == SIZE_MAX) continue;
                const auto row = state.palettes[p.canonical].row;
                if (row == SIZE_MAX ? !non_row_scope(p.selector.scope) : p.row != row) continue;
                for (std::size_t s = 0; s < 2; ++s) {
                    const auto prototype = i < state.palette_templates ? i : p.prototype;
                    const auto found = row_color_rank.find(prototype);
                    const auto n = restricted ? found == row_color_rank.end() ? SIZE_MAX : found->second[s] : color_rank[prototype][s];
                    if (n == SIZE_MAX) continue;
                    auto& prior = restricted ? row_colors.try_emplace(p.canonical, std::array<std::size_t, 2>{SIZE_MAX, SIZE_MAX}).first->second[s] : colors[p.canonical][s];
                    if (prefer_color(n, prior)) prior = n;
                }
            }
            return true;
        } catch (...) { return false; }
    }
    std::size_t combination(std::size_t binding) const noexcept {
        if (!restricted && brushes.empty()) return SIZE_MAX;
        const auto row = state.bindings[binding].row;
        const bool selected = row != SIZE_MAX && state.rows[row].kind == native::RowKind::Song
            && state.rows[row].index == state.semantic_selected;
        if (!restricted) return brushes[binding][selected];
        const auto found = row_brushes.find(binding);
        return found == row_brushes.end() ? SIZE_MAX : found->second[selected];
    }
    const native::OwnedImage& image(std::size_t binding) const noexcept {
        const auto i = combination(binding);
        return i == SIZE_MAX ? state.bindings[binding].original : state.combinations[i].image;
    }
    const plan::Palette* color(std::size_t choice, std::size_t canonical, bool selected = false) const noexcept {
        if (!restricted && colors.empty()) return nullptr;
        const auto found = row_colors.find(canonical);
        const auto n = restricted ? found == row_colors.end() ? SIZE_MAX : found->second[selected] : colors[canonical][selected];
        return n == SIZE_MAX ? nullptr : &state.themes[choice].program.colors[state.themes[choice].palette_colors[n]];
    }
};
palette::Policy choice_policy(const ChoiceLookup& lookup, std::size_t choice, std::size_t index) noexcept
{
    palette::Policy policy;
    if (const auto* color = lookup.color(choice, index)) { policy.normal.enabled = true; policy.normal.linear = color->linear; }
    if (state.palettes[index].row != SIZE_MAX && state.rows[state.palettes[index].row].kind == native::RowKind::Song) {
        if (const auto* color = lookup.color(choice, index, true)) { policy.selected.enabled = true; policy.selected.linear = color->linear; }
        policy.is_selected = state.rows[state.palettes[index].row].index == state.semantic_selected;
        if (policy.normal.enabled == policy.selected.enabled && policy.normal.linear == policy.selected.linear)
            policy.is_selected = false; // Uniform policies have no semantic transition to publish.
    }
    return policy;
}
bool same_policy(const palette::Policy& a, const palette::Policy& b) noexcept
{
    return a.is_selected == b.is_selected && a.normal.enabled == b.normal.enabled && a.selected.enabled == b.selected.enabled
        && a.normal.linear == b.normal.linear && a.selected.linear == b.selected.linear;
}
struct MemberGroup {
    std::vector<std::size_t> bindings;
    std::vector<native::BrushAssignment> next, prior;
};
bool same_member_subject(const Binding& a, const Binding& b) noexcept
{
    // Captured endpoint/class/row in this owner session, never a selector name.
    // apply_members revalidates the opaque Subject and every member's affinity.
    return a.generic && b.generic && a.widget && a.widget == b.widget && a.kind == b.kind && a.row == b.row && a.selector.scope == b.selector.scope
        && (a.kind == native::SubjectKind::Button || a.kind == native::SubjectKind::ImageSet);
}
bool make_member_groups(std::span<const std::size_t> indices, const ChoiceLookup* lookup, std::vector<MemberGroup>& groups) noexcept
{
    try {
        for (const auto i : indices) {
            auto group = std::find_if(groups.begin(), groups.end(), [&](const MemberGroup& g) {
                return same_member_subject(state.bindings[g.bindings.front()], state.bindings[i]);
            });
            if (group == groups.end()) { groups.emplace_back(); group = std::prev(groups.end()); }
            const auto& b = state.bindings[i];
            group->bindings.push_back(i);
            group->next.push_back({b.slot, lookup ? &lookup->image(i) : &b.original});
            group->prior.push_back({b.slot, b.shown == SIZE_MAX ? &b.original : &state.combinations[b.shown].image});
        }
        return true;
    } catch (...) { return false; }
}
native::Result write_member_group(const MemberGroup& group, bool prior, std::size_t count,
    const native::CallbackAuthority& authority, native::ApplyProgress& progress) noexcept
{
    const auto& b = state.bindings[group.bindings.front()];
    const auto& assignments = prior ? group.prior : group.next;
    if (b.generic && (b.kind == native::SubjectKind::Button || b.kind == native::SubjectKind::ImageSet))
        return native::apply_members(b.subject, std::span(assignments.data(), count), authority, progress);
    progress.attempted_members = 1;
    const auto result = assign_binding(state.bindings[group.bindings.front()], *assignments.front().image, authority);
    if (result) { progress.completed_members = 1; progress.refresh_complete = true; }
    return result;
}
native::Result assign_palette(const palette::Policy& policy, std::size_t index, const native::CallbackAuthority& authority) noexcept
{
    return palette::change(state.palettes[index].subject, policy, authority);
}
bool quiet_original_palettes(Scope& scope, Event& event) noexcept
{
    if (!original_has_palette_routes()) return true;
    const auto result = palette::retire(scope.session.widget, scope.session.generation, scope.authority());
    if (!result || !scope.qualify(scope.session)) {
        failed(event, "original_palette_retirement_failed", {native::Outcome::Authority, result.stage}); return false;
    }
    state.palette_charged -= std::min(state.palette_current, state.palette_charged);
    state.palette_current = 0;
    for (auto& p : state.palettes) { p.available = false; p.canonical = SIZE_MAX; p.shown = {}; }
    for (auto& theme : state.themes) if (!theme.palettes.empty()) {
        // Rebind on demand, without discarding owning brush baselines or uploads.
        theme.available = false; theme.preparing = true; theme.work.revision = 0;
    }
    update_work_pending();
    event.stage = "palette_retirement"; event.result = "original_palette_routes_retired";
    return true;
}
bool apply_choice(Scope& scope, std::size_t choice, Event& e, bool save, Transition mode) noexcept
{
    metrics::Span measured(metrics::Metric::Apply);
    if (mode == Transition::Delta && choice != state.applied) return false;
    e.chosen = choice;
    e.stage = "apply"; e.result = "program_unavailable";
    if (mode == Transition::Choose && !reconcile_speed(scope, e, false, choice)) return false;
    if (choice >= state.themes.size() || (!state.themes[choice].available && mode != Transition::Delta)) {
        if (choice && state.applied && !state.themes[state.applied].available && scope.qualify(scope.session)) {
            Event restore = e;
            if (!apply_choice(scope, 0, restore, false)) failed(e, "membership_original_restore_failed", restore.bridge);
            else { e.result = "membership_program_unavailable_original_restored"; e.save = false; }
        }
        return false;
    }
    ChoiceLookup lookup;
    if (!lookup.build(choice, scope.restricted_row)) {
        e.bridge = {native::Outcome::Memory, "choice_lookup_allocation"}; e.result = "preflight_lookup_allocation_failed_no_writes"; return false;
    }
    if (scope.restricted_row != SIZE_MAX) state.rows[scope.restricted_row].lookup_visits += lookup.visits;
    const auto authority = scope.authority();
    auto& changed = state.changed;
    SubjectChecks checks;
    std::size_t count = 0;
    auto& colors = state.changed_colors;
    auto& prior_policies = state.prior_policies;
    std::size_t color_count = 0;
    for (const auto i : IndexRange{state.palettes.size(), scope.restricted_row == SIZE_MAX ? nullptr : &state.rows[scope.restricted_row].inventory.palettes}) {
        const auto& p = state.palettes[i];
        if (mode == Transition::Delta && scope.restricted_row != SIZE_MAX && p.row != scope.restricted_row) continue;
        if (mode == Transition::Close) continue; // Qualified owner retirement restores each overridden source once.
        if (mode == Transition::Delta && p.row != SIZE_MAX && state.rows[p.row].style_pending) continue;
        if (!p.available || p.canonical != i) continue;
        const auto policy = choice_policy(lookup, choice, i);
        const bool unchanged = same_policy(p.shown, policy);
        const bool refresh = mode == Transition::Delta && scope.refresh_palettes && p.row == scope.restricted_row
            && p.subject.property != palette::Property::TextBottom && (policy.normal.enabled || policy.selected.enabled);
        if (unchanged && !refresh && (mode != Transition::Choose || choice == state.applied
            || (!policy.normal.enabled && !policy.selected.enabled))) {
            metrics::count(metrics::Counter::PolicyNoops);
            if (!policy.normal.enabled && !policy.selected.enabled) metrics::count(metrics::Counter::PassiveBindings);
            continue;
        }
        e.detail = p.name.c_str();
        if (!checks.property(p.target)) {
            e.bridge = {native::Outcome::Memory, "subject_preflight_allocation"}; e.result = "preflight_allocation_failed_no_writes"; return false;
        }
        if (!unchanged || refresh) { prior_policies[color_count] = p.shown; colors[color_count++] = i; }
    }
    // Group current Subjects and next/prior member affinities before any writes.
    for (const auto i : IndexRange{state.bindings.size(), scope.restricted_row == SIZE_MAX ? nullptr : &state.rows[scope.restricted_row].inventory.bindings}) {
        if (mode == Transition::Delta && state.bindings[i].row != SIZE_MAX && state.rows[state.bindings[i].row].style_pending) continue;
        if (mode == Transition::Delta && scope.restricted_row != SIZE_MAX && state.bindings[i].row != scope.restricted_row) continue;
        if (state.bindings[i].canonical != i) continue;
        const auto prior = state.bindings[i].shown, next = lookup.combination(i);
        if (!state.bindings[i].available) continue;
        if (prior == SIZE_MAX && next == SIZE_MAX) {
            metrics::count(metrics::Counter::PassiveBindings); metrics::count(metrics::Counter::BrushNoops); continue;
        }
        if (prior == next && (mode != Transition::Choose || choice == state.applied)) {
            metrics::count(metrics::Counter::BrushNoops); continue;
        }
        if (mode == Transition::Close) { changed[count++] = i; continue; }
        e.binding = i;
        const auto& b = state.bindings[i];
        native::Result result{native::Outcome::Ready, "subject_preflight_collected"};
        const auto& prior_image = prior == SIZE_MAX ? b.original : state.combinations[prior].image;
        if (b.generic) {
            if (!checks.brush(b, lookup.image(i)) || (prior != next && !checks.brush(b, prior_image)))
                result = {native::Outcome::Memory, "subject_preflight_allocation"};
        } else {
            const void* brush = nullptr;
            result = native::borrow_brush(lookup.image(i), authority, brush);
            brush = nullptr;
            if (result && prior != next) result = native::borrow_brush(prior_image, authority, brush);
        }
        e.bridge = result;
        if (!b.generic && !scope.qualify(scope.session)) { failed(e, "preflight_authority", result); return false; }
        if (!result) {
            e.bridge = result; e.result = "preflight_rejected_no_writes";
            if (choice) { state.themes[choice].available = false; state.themes[choice].rejected = true; }
            return false;
        }
        if (prior != next) changed[count++] = i;
    }
    if (mode != Transition::Close) {
        const auto result = checks.run(scope); e.bridge = result;
        if (!result) {
            if (!scope.qualify(scope.session)) failed(e, "preflight_authority", result);
            else { e.result = "preflight_rejected_no_writes"; if (choice) { state.themes[choice].available = false; state.themes[choice].rejected = true; } }
            return false;
        }
    }
    std::vector<MemberGroup> groups;
    if (!make_member_groups(std::span(changed.data(), count), &lookup, groups)) {
        e.bridge = {native::Outcome::Memory, "member_group_allocation"}; e.result = "preflight_group_allocation_failed_no_writes"; return false;
    }
    auto rollback_groups = [&](std::size_t last, std::size_t attempted_members) noexcept {
        metrics::Span rollback(metrics::Metric::Rollback);
        for (auto n = last + 1; n > 0; --n) {
            const auto& g = groups[n - 1];
            const auto members = n - 1 == last ? attempted_members : g.bindings.size();
            if (!members) continue;
            native::ApplyProgress progress{};
            const auto result = write_member_group(g, true, members, authority, progress);
            if (!result || !progress.refresh_complete || progress.completed_members != members || !scope.qualify(scope.session)) {
                failed(e, "group_rollback_unavailable", result); return false;
            }
        }
        return true;
    };
    for (std::size_t attempted = 0; attempted < groups.size(); ++attempted) {
        const auto& group = groups[attempted]; e.binding = group.bindings.front();
        native::ApplyProgress progress{};
        const auto result = write_member_group(group, false, group.bindings.size(), authority, progress);
        e.bridge = result;
        if (!scope.qualify(scope.session)) { failed(e, "transition_authority", result); return false; }
        if (!result || !progress.refresh_complete || progress.completed_members != group.bindings.size()) {
            e.bridge = result;
            if (!rollback_groups(attempted, std::min(progress.attempted_members, group.bindings.size()))) return false;
            e.binding = group.bindings.front();
            e.result = "transition_rejected_prior_restored";
            if (choice) { state.themes[choice].available = false; state.themes[choice].rejected = true; }
            return false;
        }
    }
    for (std::size_t attempted = 0; attempted < color_count; ++attempted) {
        e.detail = state.palettes[colors[attempted]].name.c_str();
        const auto result = assign_palette(choice_policy(lookup, choice, colors[attempted]), colors[attempted], authority); e.bridge = result;
        if (!scope.qualify(scope.session)) { failed(e, "palette_transition_retired", result); return false; }
        if (!result) {
            // First recover completed image writes. A stale native animation source
            // may forbid ALL palette replay; never paint its historical snapshot.
            if (!groups.empty() && !rollback_groups(groups.size() - 1, groups.back().bindings.size())) return false;
            for (std::size_t n = attempted + 1; n > 0; --n) {
                metrics::Span rollback_metric(metrics::Metric::Rollback);
                const auto rollback = assign_palette(prior_policies[n - 1], colors[n - 1], authority);
                if (!rollback || !scope.qualify(scope.session)) { failed(e, "palette_rollback_unavailable", rollback); return false; }
            }
            e.result = "palette_transition_rejected_prior_restored";
            if (choice) { state.themes[choice].available = false; state.themes[choice].rejected = true; }
            return false;
        }
    }
    if (!count && !color_count && !scope.qualify(scope.session)) {
        failed(e, "program_commit_retired", {native::Outcome::Authority, "program_commit_retired"}); return false;
    }
    for (std::size_t n = 0; n < count; ++n) state.bindings[changed[n]].shown = lookup.combination(changed[n]);
    for (std::size_t n = 0; n < color_count; ++n) state.palettes[colors[n]].shown = choice_policy(lookup, choice, colors[n]);
    if (mode != Transition::Delta) {
        state.applied = choice;
        state.pending_rows.clear();
        if (mode == Transition::Choose) publish_song_preview_palette(scope.session.generation, state.themes[choice].program.preview);
        if (choice) for (auto& row : state.rows) if (row.available) row.style_pending = false;
    }
    update_work_pending();
    if (!choice && state.desired < state.themes.size() && state.themes[state.desired].rejected) {
        state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
    }
    if (!choice && mode != Transition::Close && !quiet_original_palettes(scope, e)) return false;
    if (mode == Transition::Choose) metrics::count(metrics::Counter::ThemeCommits);
    if (choice && mode != Transition::Close && metrics::enabled()) {
        const auto now = diagnostic_now();
        if (mode == Transition::Choose && state.diagnostic_ready_ns && state.diagnostic_ready_generation == scope.session.generation) {
            if (now >= state.diagnostic_ready_ns) metrics::sample(metrics::Metric::ReadyToCommit, now - state.diagnostic_ready_ns);
            state.diagnostic_ready_ns = state.diagnostic_ready_generation = 0;
        }
        if (scope.restricted_row != SIZE_MAX) publish_row_diagnostic(state.rows[scope.restricted_row], now);
        else for (auto& row : state.rows) publish_row_diagnostic(row, now);
    }
    if (save) state.selected = state.themes[choice].id; // capacity reserved at worker-side configure
    e.stage = count || color_count || save ? "apply" : "none";
    e.result = mode == Transition::Delta ? "applied_subject_deltas" : "applied_complete_program";
    e.chosen = choice; e.save = save;
    return true;
}
bool repair_applied_rows(Scope& scope, Event& event, std::size_t physical_row) noexcept
{
    if (!state.applied || state.compromised.load()) return !state.compromised.load();
    if (scope.source == Source::Preparation && state.desired < state.themes.size()
        && state.desired != state.applied && state.themes[state.desired].available) return true;
    auto& theme = state.themes[state.applied];
    const auto r = physical_row == SIZE_MAX ? scope.serviced_row : physical_row;
    if (r != SIZE_MAX && state.pending_rows.contains(r)) {
        auto& row = state.rows[r];
        if (row.failed) return false;
        if (!row.available || !row.style_pending) return true;
        const auto target_scope = row.kind == native::RowKind::Song ? plan::Scope::MusicListCell : plan::Scope::SpeedIcon;
        bool ready = true;
        for (const auto prototype : theme.combinations) {
            if (state.bindings[state.combinations[prototype].binding].selector.scope != target_scope) continue;
            const auto found = row.inventory.combination.find(prototype);
            ready &= found != row.inventory.combination.end() && state.combinations[found->second].available;
        }
        for (const auto prototype : theme.palettes) {
            if (state.palettes[prototype].selector.scope != target_scope) continue;
            const auto found = row.inventory.palette.find(prototype);
            ready &= found != row.inventory.palette.end() && state.palettes[found->second].available;
        }
        if (!ready) return true;
        if (!scope.step()) return true;
        const auto restriction = scope.restricted_row;
        const auto refresh = scope.refresh_palettes;
        row.style_pending = false;
        scope.restricted_row = r; scope.refresh_palettes = row.refresh_palettes;
        const bool repaired = apply_choice(scope, state.applied, event, false, Transition::Delta);
        scope.restricted_row = restriction; scope.refresh_palettes = refresh;
        if (!repaired) { row.style_pending = true; return false; }
        row.refresh_palettes = false;
        state.pending_rows.erase(r);
    }
    // This repairs only the still-committed program; it never selects/persists a
    // pending user choice or claims that other recycled rows are complete.
    if (theme.row_repair_only && state.pending_rows.empty()) {
        theme.available = true; theme.preparing = false; theme.row_repair_only = false;
        if (state.desired == state.applied && !state.desired_save && !state.desired_gesture) state.desired = SIZE_MAX;
    }
    update_work_pending();
    return true;
}
bool reconcile_speed(Scope& scope, Event& event, bool prepare, std::size_t requested) noexcept
{
    metrics::Span measured(metrics::Metric::ReconcileSpeed);
    const auto uses_speed = [](const Prepared& theme) {
        for (const auto& image : theme.program.images) if (image.target.scope == plan::Scope::SpeedIcon) return true;
        for (const auto& color : theme.program.colors) if (color.target.scope == plan::Scope::SpeedIcon) return true;
        return false;
    };
    bool needed = false;
    for (std::size_t i = 1; i < state.themes.size(); ++i)
        if (demanded(i) || i == requested) needed |= uses_speed(state.themes[i]);
    if (!needed) return true;
    native::SpeedRowsSnapshot members;
    const auto authority = scope.authority();
    const auto result = native::read_speed_rows(authority, members);
    if (!scope.qualify(scope.session)) { failed(event, "speed_membership_retired", result); return false; }
    state.speed_ready = static_cast<bool>(result) && members.count != 0;
    state.speed_result = result;
    if (result && !members.count) state.speed_result = {native::Outcome::Unavailable, "speed_members_empty"};
    const auto revoke = [&](std::size_t row) {
        state.pending_rows.erase(row);
        invalidate_preparation();
        state.rows[row].subjects.clear();
        state.speed_dirty = true;
        auto& r = state.rows[row];
        cancel_row_diagnostic(r);
        std::vector<std::size_t> restores;
        std::vector<MemberGroup> groups;
        try {
            SubjectChecks checks;
            for (const auto binding : r.inventory.bindings) {
                const auto& b = state.bindings[binding];
                if (b.available && b.canonical == binding && b.shown != SIZE_MAX && !checks.receiver(b))
                    throw std::bad_alloc{};
            }
            for (const auto i : r.inventory.palettes) {
                const auto& p = state.palettes[i];
                if (p.available && p.canonical == i && !checks.property(p.target)) throw std::bad_alloc{};
            }
            const bool all_current = static_cast<bool>(checks.run(scope));
            if (!scope.qualify(scope.session)) { failed(event, "speed_retirement_preflight_authority"); return false; }
            for (const auto binding : r.inventory.bindings) {
                const auto& b = state.bindings[binding];
                if (!b.available || b.canonical != binding || b.shown == SIZE_MAX) continue;
                if (all_current || current_subject(b.subject, scope)) restores.push_back(binding);
                else r.detached_themed = true;
            }
            for (const auto i : r.inventory.palettes) {
                auto& p = state.palettes[i];
                if (p.available && p.canonical == i) p.retirement_current = all_current || current_subject(p.target, scope);
            }
        } catch (...) { failed(event, "speed_restore_group_allocation", {native::Outcome::Memory, "speed_restore_group_allocation"}); return false; }
        if (!make_member_groups(restores, 0, groups)) { failed(event, "speed_restore_group_allocation", {native::Outcome::Memory, "speed_restore_group_allocation"}); return false; }
        for (const auto& group : groups) {
            native::ApplyProgress progress{};
            const auto restored = write_member_group(group, false, group.bindings.size(), authority, progress);
            if (!restored || !progress.refresh_complete || progress.completed_members != group.bindings.size()
                || !scope.qualify(scope.session)) { failed(event, "speed_retirement_restore", restored); return false; }
        }
        for (std::size_t binding = 0; binding < state.bindings.size(); ++binding) {
            auto& b = state.bindings[binding];
            if (b.row != row || !b.available) continue;
            b.available = b.resolved = false; b.shown = SIZE_MAX;
        }
        std::size_t palette_count = 0, current_count = 0;
        for (const auto i : state.rows[row].inventory.palettes) {
            auto& p = state.palettes[i];
            if (p.row == row && p.available && p.canonical == i) {
                ++palette_count;
                if (p.retirement_current) ++current_count;
            }
        }
        const bool grouped = palette_count && (current_count == 0 || current_count == palette_count);
        if (grouped) {
            const auto retired = palette::retire_matching(scope.session.widget, scope.session.generation,
                match_palette_row, &row, current_count ? authority : native::CallbackAuthority{});
            if (!scope.qualify(scope.session)) { failed(event, "speed_palette_retirement_reentered", retired); return false; }
            if (!retired && current_count) { failed(event, "speed_palette_retirement", retired); return false; }
            if (retired) state.palette_charged -= std::min(palette_count, state.palette_charged);
            else for (const auto& p : state.palettes) if (p.row == row && p.available && (p.shown.normal.enabled || p.shown.selected.enabled)) r.detached_themed = true;
            state.palette_current -= std::min(palette_count, state.palette_current);
        }
        if (palette_count && !grouped) {
            for (const bool current : {false, true}) {
                RetirementFilter filter{row, current};
                const auto retired = palette::retire_matching(scope.session.widget, scope.session.generation,
                    match_palette_partition, &filter, current ? authority : native::CallbackAuthority{});
                if (!scope.qualify(scope.session)) { failed(event, "speed_palette_retirement_reentered", retired); return false; }
                if (!retired && current) { failed(event, "speed_palette_retirement", retired); return false; }
                const auto count = current ? current_count : palette_count - current_count;
                if (retired) state.palette_charged -= std::min(count, state.palette_charged);
                else for (const auto& p : state.palettes) if (p.row == row && !p.retirement_current && (p.shown.normal.enabled || p.shown.selected.enabled)) r.detached_themed = true;
            }
            state.palette_current -= std::min(palette_count, state.palette_current);
        }
        for (auto& p : state.palettes) if (p.row == row) {
            p.available = p.resolved = false; p.shown = {}; p.subject = {};
        }
        for (auto& c : state.combinations) if (c.row == row) { c.available = false; c.image = {}; }
        r.available = false;
        return true;
    };
    for (std::size_t i = 0; i < state.rows.size(); ++i) {
        auto& row = state.rows[i];
        if (row.kind != native::RowKind::Speed || !row.available) continue;
        const bool present = result && std::find(members.rows.begin(), members.rows.begin() + members.count, row.row) != members.rows.begin() + members.count;
        UObjectLiveHandle identity{};
        bool current = present && capture_live_uobject_handle(row.row, identity)
            && identity.internal_index == row.identity.internal_index && identity.serial_number == row.identity.serial_number;
        if (current) {
            SubjectChecks checks;
            for (const auto binding : row.inventory.bindings) {
                const auto& b = state.bindings[binding]; if (b.available && !checks.receiver(b)) current = false;
            }
            for (const auto color : row.inventory.palettes) {
                const auto& p = state.palettes[color]; if (p.available && !checks.property(p.target)) current = false;
            }
            if (current && !checks.run(scope)) current = false;
        }
        if (!current && !revoke(i)) return false;
    }
    if (result) for (std::size_t i = 0; i < members.count; ++i) {
        auto* pointer = members.rows[i]; UObjectLiveHandle identity{};
        if (!capture_live_uobject_handle(pointer, identity)) {
            state.speed_ready = false; state.speed_result = {native::Outcome::Identity, "speed_positive_row_unavailable"}; continue;
        }
        auto row = std::find_if(state.rows.begin(), state.rows.end(), [&](const RowRecord& r) { return r.row == pointer && r.kind == native::RowKind::Speed; });
        if (row != state.rows.end() && row->available) continue;
        if (row != state.rows.end() && row->failed && same_menu_session(row->session, scope.session)
            && identity.internal_index == row->identity.internal_index && identity.serial_number == row->identity.serial_number) {
            state.speed_ready = false; state.speed_result = row->binding_result; continue;
        }
        if (row != state.rows.end() && row->detached_themed && identity.internal_index == row->identity.internal_index
            && identity.serial_number == row->identity.serial_number) {
            failed(event, "speed_reappearance_without_qualified_original"); return false;
        }
        if (!prepare) { // Posted input never constructs or captures.
            if (!state.speed_dirty) invalidate_preparation();
            state.speed_ready = false; state.speed_dirty = true;
            state.work_pending = true; state.preparation_cursor = 1;
            state.speed_result = {native::Outcome::Unavailable, "speed_reconciliation_requires_preparation"}; continue;
        }
        if (row == state.rows.end()) row = std::find_if(state.rows.begin(), state.rows.end(), [](const RowRecord& r) { return !r.row; });
        if (row == state.rows.end()) row = append_row();
        if (row == state.rows.end() || state.speed_epoch == UINT64_MAX) {
            state.speed_ready = false; state.speed_result = {native::Outcome::Memory, "speed_context_allocation_or_epoch_overflow"}; continue;
        }
        if (!scope.step()) { state.speed_ready = false; return true; }
        replace_row(*row); row->row = pointer; row->identity = identity; row->session = scope.session;
        state.speed_dirty = true;
        row->kind = native::RowKind::Speed; row->membership_epoch = ++state.speed_epoch;
        const auto bound = [&] {
            metrics::Span resolve(metrics::Metric::Resolve);
            return native::bind_speed_row(pointer, row->membership_epoch, authority, row->ticket);
        }();
        row->binding_result = bound;
        if (!scope.qualify(scope.session)) { failed(event, "speed_bind_retired", bound); return false; }
        row->available = static_cast<bool>(bound); row->failed = !bound;
        invalidate_preparation();
        row->style_pending = true;
        try { state.pending_rows.insert(static_cast<std::size_t>(row - state.rows.begin())); }
        catch (...) { failed(event, "speed_work_allocation_failed"); return false; }
        if (bound) pending_row_diagnostic(*row);
        if (!bound) { state.speed_ready = false; state.speed_result = bound; }
    }
    if (state.speed_dirty) for (auto& theme : state.themes) if (uses_speed(theme)) {
        theme.preparing = true; theme.row_repair_only = false; state.preparation_cursor = 1;
        theme.available = false; // Admission covers the complete current membership, not just existing members.
    }
    update_work_pending();
    return true;
}
bool refresh_composition(Scope& scope, Event& event, std::vector<Event>* diagnostics, std::size_t physical_row, std::size_t requested) noexcept
{
    std::size_t preparing = SIZE_MAX;
    try {
        if (physical_row == SIZE_MAX && (!state.speed_ready || state.speed_dirty) && !reconcile_speed(scope, event, true)) return false;
        if (physical_row == SIZE_MAX) state.speed_dirty = false;
        const auto authority = scope.authority();
        const auto physical_binding = [&](std::size_t prototype, std::size_t row) {
            auto& inventory = state.rows[row].inventory;
            if (const auto found = inventory.binding.find(prototype); found != inventory.binding.end()) return found->second;
            Binding b; const auto& source = state.bindings[prototype];
            b.name = source.name; b.leaf = source.leaf; b.selector = source.selector; b.prototype = prototype; b.row = row;
            b.kind = source.kind; b.kind_known = source.kind_known;
            return append_physical(state.bindings, inventory.binding, inventory.bindings, prototype, std::move(b));
        };
        const auto physical_palette = [&](std::size_t prototype, std::size_t row) {
            auto& inventory = state.rows[row].inventory;
            if (const auto found = inventory.palette.find(prototype); found != inventory.palette.end()) return found->second;
            PaletteBinding p; const auto& source = state.palettes[prototype];
            p.name = source.name; p.leaf = source.leaf; p.selector = source.selector; p.prototype = prototype; p.row = row;
            p.kind = source.kind; p.kind_known = source.kind_known;
            return append_physical(state.palettes, inventory.palette, inventory.palettes, prototype, std::move(p));
        };
        std::size_t row_count = 0; bool failed_row = state.untracked_row;
        if (physical_row != SIZE_MAX) { row_count = state.rows[physical_row].available; failed_row = state.rows[physical_row].failed; }
        else for (const auto& r : state.rows) if (r.kind == native::RowKind::Song) { if (r.available) ++row_count; if (r.failed) failed_row = true; }
        const auto priority = requested < state.themes.size() ? requested : state.desired < state.themes.size() ? state.desired : state.applied;
        {
            const auto next = priority;
            const auto choice = physical_row != SIZE_MAX ? state.applied : priority;
            if (!choice || choice >= state.themes.size()) return true;
            auto& theme = state.themes[choice]; const bool previously_available = theme.available;
            if (physical_row == SIZE_MAX && !theme.preparing) { metrics::count(metrics::Counter::InactivePreparationSteps); return true; }
            preparing = choice;
            native::Result result{native::Outcome::Ready, "program_prepared"};
            Event d; d.stage = "program_admission"; d.generation = scope.session.generation; d.chosen = choice;
            d.preparation_facts = state.preparation_facts;
            d.detail = theme.rejection.empty() ? "none" : theme.rejection.c_str();
            const auto progress = [&](const char* stage) {
                // Report only phase changes, copied here and formatted after the owning scope/lock.
                if (theme.progress_stage && std::string_view(theme.progress_stage) == stage) return;
                theme.progress_stage = stage;
                if (diagnostics && physical_row == SIZE_MAX) {
                    auto p = d; p.stage = "preparation_progress"; p.result = stage;
                    p.cached_known = state.bindings.size() + state.combinations.size() + state.palettes.size() + state.resources.size();
                    for (const auto& b : state.bindings) p.cached_ready += b.available;
                    for (const auto& c : state.combinations) p.cached_ready += c.available;
                    for (const auto& color : state.palettes) p.cached_ready += color.available;
                    for (const auto& r : state.resources) p.cached_ready += r.cached == Cached::Ready;
                    diagnostics->push_back(p);
                }
            };
            auto& work = physical_row == SIZE_MAX ? theme.work : state.rows[physical_row].work;
            if (work.revision != state.work_revision) {
                if (work.revision) metrics::count(metrics::Counter::PreparationRestarts);
                work = {};
                for (auto index : theme.combinations) for (std::size_t depth = 0; index != SIZE_MAX; ++depth) {
                    if (index >= state.combination_templates || depth >= state.combination_templates) {
                        result = {native::Outcome::Input, "composition_dependency_cycle_or_index"}; break;
                    }
                    const auto& c = state.combinations[index];
                    const auto row_scope = physical_row != SIZE_MAX && state.rows[physical_row].kind == native::RowKind::Speed
                        ? plan::Scope::SpeedIcon : plan::Scope::MusicListCell;
                    if (physical_row == SIZE_MAX || state.bindings[c.binding].selector.scope == row_scope) {
                        work.needed.insert(index);
                        if (!c.tinted) work.resources.insert(c.resource);
                    }
                    index = c.base;
                }
                if (physical_row != SIZE_MAX) work.rows.push_back(physical_row);
                else for (std::size_t r = 0; r < state.rows.size(); ++r) if (state.rows[r].available) work.rows.push_back(r);
                if (result) work.revision = state.work_revision;
            }
            auto& needed = work.needed;
            auto& bindings = work.bindings; auto& combinations = work.combinations; auto& palettes = work.palettes;
            auto& needs_rows = work.needs_rows; auto& needs_speed = work.needs_speed;
            if (!theme.backend || theme.rejected) result = {native::Outcome::Unavailable, theme.rejected ? "transition_rejected" : "backend_pending"};
            else {
                do {
                for (auto t = needed.lower_bound(work.expansion); t != needed.end() && result; ++t) {
                    const auto prototype = *t; ++work.visits;
                    const auto& source = state.combinations[prototype];
                    if (non_row_scope(state.bindings[source.binding].selector.scope)) {
                        bindings.insert(source.binding); combinations.insert(prototype); work.expansion = prototype + 1; continue;
                    }
                    const bool speed = state.bindings[source.binding].selector.scope == plan::Scope::SpeedIcon;
                    needs_speed |= speed; needs_rows |= !speed;
                    for (auto& position = work.row; position < work.rows.size() && result; ++position) {
                        const auto r = work.rows[position]; ++work.visits;
                        if (state.rows[r].available
                        && matching_row_scope(state.bindings[source.binding].selector.scope, state.rows[r].kind)) {
                        auto& inventory = state.rows[r].inventory;
                        const auto found = inventory.combination.find(prototype);
                        const auto c = found == inventory.combination.end() ? state.combinations.size() : found->second;
                        if (found == inventory.combination.end()) {
                            progress("expand_current_rows");
                            if (!scope.step()) return true;
                            const auto b = physical_binding(source.binding, r);
                            auto copy = source; copy.prototype = prototype; copy.row = r; copy.binding = b; copy.image = {}; copy.available = false;
                            if (source.base != SIZE_MAX) {
                                copy.base = SIZE_MAX;
                                if (const auto base = inventory.combination.find(source.base); base != inventory.combination.end()) copy.base = base->second;
                                if (copy.base == SIZE_MAX) { result = {native::Outcome::Input, "row_composition_dependency"}; break; }
                            }
                            append_physical(state.combinations, inventory.combination, inventory.combinations, prototype, std::move(copy));
                        }
                        bindings.insert(state.combinations[c].binding);
                        combinations.insert(c);
                        }
                    }
                    work.row = 0; work.expansion = prototype + 1;
                }
                for (auto& n = work.palette_expansion; n < theme.palettes.size() && result; ++n) {
                    const auto t = theme.palettes[n];
                    ++work.visits;
                    if (physical_row != SIZE_MAX && !matching_row_scope(state.palettes[t].selector.scope, state.rows[physical_row].kind)) continue;
                    if (non_row_scope(state.palettes[t].selector.scope)) palettes.insert(t);
                    else {
                        const bool speed = state.palettes[t].selector.scope == plan::Scope::SpeedIcon;
                        needs_speed |= speed; needs_rows |= !speed;
                        for (auto& position = work.row; position < work.rows.size(); ++position) {
                            const auto r = work.rows[position]; ++work.visits;
                            if (state.rows[r].available
                            && matching_row_scope(state.palettes[t].selector.scope, state.rows[r].kind)) {
                            const bool exists = state.rows[r].inventory.palette.contains(t);
                            if (!exists) { progress("expand_current_rows"); if (!scope.step()) return true; }
                            const auto p = physical_palette(t, r);
                            palettes.insert(p);
                            }
                        }
                        work.row = 0;
                    }
                }
                if (result && !work.changed_rows.empty()) {
                    work.rows.assign(work.changed_rows.begin(), work.changed_rows.end());
                    work.changed_rows.clear();
                    work.expansion = work.palette_expansion = work.row = 0;
                    // Preserve unrelated cursors while completing this finite program's changed rows.
                    if (!scope.step()) return true;
                } else {
                    break;
                }
                } while (result);
                if (result && needs_rows && !row_count) result = {native::Outcome::Unavailable, "song_row_receivers_not_ready"};
                if (result && needs_rows && failed_row) result = {native::Outcome::Identity, "positive_song_row_association_unavailable"};
                if (result && needs_speed && !state.speed_ready) result = state.speed_result;
                // Resolve ALL current subjects before any owning capture or factory.
                for (auto next = bindings.lower_bound(work.resolve_binding); next != bindings.end() && result; ++next) {
                    const auto i = *next; ++work.visits;
                    if (!state.bindings[i].resolved) {
                    progress("resolve_brush_subjects"); if (!scope.step()) return true;
                    d.binding = i; result = capture_binding(state.bindings[i], scope, false); state.bindings[i].result = result;
                    }
                    work.resolve_binding = i + 1;
                }
                for (auto next = palettes.lower_bound(work.resolve_palette); next != palettes.end() && result; ++next) {
                    const auto i = *next; ++work.visits;
                    if (!state.palettes[i].resolved) {
                    progress("resolve_palette_subjects"); if (!scope.step()) return true;
                    d.detail = state.palettes[i].name.c_str(); result = prepare_palette(i, scope, false); state.palettes[i].result = result;
                    }
                    work.resolve_palette = i + 1;
                }
                const auto& resources = work.resources;
                if (result && (state.changed.size() < state.bindings.size() || state.changed_colors.size() < state.palettes.size()
                    || state.prior_policies.size() < state.palettes.size())) {
                    progress("transition_scratch"); if (!scope.step()) return true;
                    state.changed.resize(state.bindings.size()); state.changed_colors.resize(state.palettes.size());
                    state.prior_policies.resize(state.palettes.size());
                }
                for (auto next = bindings.lower_bound(work.capture); next != bindings.end() && result; ++next) {
                    const auto i = *next; ++work.visits;
                    if (!state.bindings[i].available) {
                    progress("capture_originals"); if (!scope.step()) return true;
                    auto& b = state.bindings[i]; b.canonical = i; b.shown = SIZE_MAX;
                    const auto peers = IndexRange{state.bindings.size(), b.row == SIZE_MAX ? nullptr : &state.rows[b.row].inventory.bindings};
                    if (b.generic) for (const auto j : peers) {
                        ++work.visits;
                        const auto& prior = state.bindings[j];
                        if (j != i && prior.available && prior.generic && prior.row == b.row && prior.selector.scope == b.selector.scope && prior.widget == b.widget && prior.kind == b.kind
                            && prior.slot.index == b.slot.index && prior.slot.button == b.slot.button) { b.canonical = prior.canonical; b.result = prior.result; b.available = true; break; }
                    }
                    if (!b.available) {
                        if (b.generic && (b.kind == native::SubjectKind::Button || b.kind == native::SubjectKind::ImageSet)) {
                            std::vector<std::size_t> group{i};
                            for (const auto j : peers) {
                                if (j <= i) continue;
                                ++work.visits;
                                const auto& candidate = state.bindings[j];
                                if (!bindings.contains(j) || candidate.available || !same_capture_subject(b, candidate)) continue;
                                if (std::none_of(group.begin(), group.end(), [&](std::size_t member) {
                                    const auto& slot = state.bindings[member].slot;
                                    return slot.index == candidate.slot.index && slot.button == candidate.slot.button;
                                })) group.push_back(j);
                            }
                            result = capture_members(group, scope);
                        } else result = capture_binding(b, scope);
                        if (!scope.qualify(scope.session)) { failed(event, "original_capture_retired", result); return false; }
                        b.result = result; b.available = static_cast<bool>(result);
                    }
                    }
                    work.capture = i + 1;
                }
                // Capture also validates renderer-specific member bounds before irreversible uploads.
                for (auto next = resources.lower_bound(work.upload); next != resources.end() && result; ++next) {
                    const auto i = *next; ++work.visits;
                    auto& resource = state.resources[i];
                    if (resource.cached == Cached::Pending) {
                        progress("upload_pixels"); if (!scope.step()) return true;
                        note_native_attempt(resource.pixels.bgra.size());
                        metrics::Span upload(metrics::Metric::Upload);
                        resource.result = native::create(resource.pixels, authority, resource.image);
                        resource.cached = resource.result ? Cached::Ready : Cached::Failed;
                    }
                    if (resource.cached != Cached::Ready) result = resource.result;
                    work.upload = i + 1;
                }
                for (auto next = combinations.lower_bound(work.compose); next != combinations.end() && result; ++next) {
                    const auto i = *next; ++work.visits;
                    if (!state.combinations[i].available) {
                    progress("compose_brushes"); if (!scope.step()) return true;
                    auto& c = state.combinations[i]; const auto& b = state.bindings[c.binding];
                    const auto& original = state.bindings[b.canonical].original;
                    if (c.tinted && !native::capabilities().brush_tint) result = {native::Outcome::Unavailable, "brush_tint_backend"};
                    else if (b.generic) result = resolve_combinations(work, i, scope);
                    else {
                        note_native_attempt(native::kOriginalCaptureByteCharge);
                        metrics::Span compose(c.tinted ? metrics::Metric::Tint : metrics::Metric::Compose);
                        result = c.tinted ? native::tint(c.base == SIZE_MAX ? original : state.combinations[c.base].image, c.rgba, authority, c.image)
                            : native::compose(original, state.resources[c.resource].image, authority, c.image);
                    }
                    c.result = result; c.available = static_cast<bool>(result);
                    }
                    work.compose = i + 1;
                }
                for (auto next = palettes.lower_bound(work.bind); next != palettes.end() && result; ++next) {
                    const auto i = *next; ++work.visits;
                    if (!state.palettes[i].available) {
                    progress("bind_palette_sources"); if (!scope.step()) return true;
                    auto& p = state.palettes[i]; result = prepare_palette(i, scope); p.result = result; p.available = static_cast<bool>(result); p.shown = {};
                    }
                    work.bind = i + 1;
                }
                // Physical aliases may differ in source syntax, but not in the same semantic state.
                std::map<std::size_t, std::array<bool, 3>> brush_states;
                for (const auto t : theme.combinations) for (const auto c : combinations) {
                    ++work.visits;
                    if (!result || (c != t && state.combinations[c].prototype != t)) continue;
                    const auto canonical = state.bindings[state.combinations[c].binding].canonical;
                    auto& used = brush_states[canonical][static_cast<unsigned>(state.combinations[c].variant)];
                    if (used) result = {native::Outcome::Input, "duplicate_physical_brush_slot"};
                    used = true;
                }
                std::map<std::size_t, std::array<bool, 3>> palette_states;
                for (std::size_t n = 0; n < theme.palettes.size() && result; ++n) {
                    const auto a = theme.palettes[n];
                    const auto semantic = static_cast<unsigned>(theme.program.colors[theme.palette_colors[n]].target.state);
                    for (const auto p : palettes) {
                        ++work.visits;
                        if (!result || (p != a && state.palettes[p].prototype != a)) continue;
                        auto& used = palette_states[state.palettes[p].canonical][semantic];
                        if (used) result = {native::Outcome::Input, "duplicate_physical_palette_property"};
                        used = true;
                    }
                }
            }
            // Publication is not native work. Do not yield a fully prepared program
            // back into the unresolved inventory after its final owning operation.
            if (!scope.qualify(scope.session)) { failed(event, "program_preparation_retired", result); return false; }
            if (physical_row != SIZE_MAX) {
                auto& row = state.rows[physical_row];
                // Keep complete-plan coverage for physical slots first discovered
                // by local repair, without rewinding another program's work.
                if (result && theme.work.revision == work.revision) {
                    theme.work.bindings.insert(work.bindings.begin(), work.bindings.end());
                    theme.work.combinations.insert(work.combinations.begin(), work.combinations.end());
                    theme.work.palettes.insert(work.palettes.begin(), work.palettes.end());
                }
                row.binding_result = result;
                row.prepared_choice = result ? choice : SIZE_MAX;
                row.failed = !result;
                if (!result) { theme.rejected = true; theme.preparing = false; theme.admission_stage = result.stage; }
                event.bridge = result;
                return true; // A repaired row is not whole-candidate admission.
            }
            theme.available = static_cast<bool>(result);
            theme.row_repair_only = false;
            if (!result && !awaiting_receivers(result.stage)) theme.rejected = true;
            state.prepared_once |= theme.available;
            theme.preparing = false;
            d.bridge = result; d.result = theme.available ? "ready" : result.stage;
            d.preparation_facts = state.preparation_facts;
            if (diagnostics && (scope.source == Source::Ready || previously_available != theme.available || theme.admission_stage != result.stage))
                diagnostics->push_back(d);
            theme.admission_stage = result.stage;
            if (choice == next) state.preparation_cursor = next + 1;
            update_work_pending();
            return true; // Publish at most one candidate per owned message, including rejected definitions.
        }
        state.speed_dirty = false;
        state.work_pending = false;
        return !state.compromised.load();
    } catch (...) {
        if (!scope.qualify(scope.session)) { failed(event, "preparation_allocation_authority_lost"); return false; }
        if (preparing < state.themes.size()) {
            auto& theme = state.themes[preparing]; theme.preparing = false; theme.available = false; theme.rejected = true;
            theme.admission_stage = "preparation_allocation_failed";
            if (state.preparation_cursor == preparing) ++state.preparation_cursor;
        }
        event.stage = "program_admission"; event.chosen = preparing; event.result = "preparation_allocation_failed";
        event.bridge = {native::Outcome::Memory, "preparation_allocation_failed"};
        if (physical_row != SIZE_MAX) state.rows[physical_row].failed = true;
        event.preparation_facts = state.preparation_facts;
        return true; // No writes occurred; retain the current complete program and Original.
    }
}
bool prepare_quantum(Scope& scope, Event& event, std::vector<Event>* diagnostics, std::size_t requested, std::size_t physical_row) noexcept
{
    metrics::Span measured(metrics::Metric::Prepare);
    if (!state.work_pending) metrics::count(metrics::Counter::InactivePreparationSteps);
    update_work_pending();
    if (!state.work_pending) return true;
    if (original_has_palette_routes()) return quiet_original_palettes(scope, event);
    // An explicit choice bypasses committed-row maintenance. A fallback dispatch
    // services only one pending row, or one demanded program when no row is pending.
    if (physical_row == SIZE_MAX && requested == SIZE_MAX && state.applied
        && (state.desired == SIZE_MAX || state.desired == state.applied) && !state.pending_rows.empty())
        physical_row = *state.pending_rows.begin();
    const auto choice = physical_row != SIZE_MAX ? state.applied
        : requested < state.themes.size() ? requested : state.desired < state.themes.size() ? state.desired : state.applied;
    if (!choice || choice >= state.themes.size()) return true;
    scope.serviced_row = physical_row;
    const auto& program = state.themes[choice];
    if (program.combinations.empty() && program.palettes.empty()) return refresh_composition(scope, event, diagnostics, physical_row, choice);
    if (!state.subjects_prepared) {
        if (!scope.step()) return true;
        if (native::capabilities().subjects) {
            metrics::Span resolve(metrics::Metric::Resolve);
            event.bridge = native::prepare_subjects(scope.authority());
        }
        if (!scope.qualify(scope.session)) { failed(event, "preparation_witnesses_retired", event.bridge); return false; }
        state.subjects_prepared = true;
    }
    // The actual row callback supplies its own index, never the queue head.
    if (physical_row != SIZE_MAX) {
        const auto r = physical_row;
        auto& row = state.rows[r];
        if (!row.style_pending) return true;
        if (row.prepared_choice == state.applied) return true;
        if (!row.available && !row.failed) {
            if (!scope.step()) return true;
            UObjectLiveHandle identity{};
            if (!same_menu_session(row.session, scope.session) || !capture_live_uobject_handle(row.row, identity)
                || identity.internal_index != row.identity.internal_index || identity.serial_number != row.identity.serial_number) {
                row.failed = true; cancel_row_diagnostic(row);
                row.binding_result = {native::Outcome::Identity, "positive_row_identity_changed"};
            } else {
                metrics::Span resolve(metrics::Metric::Resolve);
                row.binding_result = native::bind_row(row.kind, row.row, row.index, row.association_setup, scope.authority(), row.ticket);
                if (!scope.qualify(scope.session)) { failed(event, "preparation_row_retired", row.binding_result); return false; }
                row.available = static_cast<bool>(row.binding_result); row.failed = !row.available;
            }
        }
        if (row.failed) {
            auto& theme = state.themes[state.applied];
            theme.available = false; theme.preparing = false; theme.rejected = true;
            theme.admission_stage = row.binding_result.stage;
            return true;
        }
        return refresh_composition(scope, event, diagnostics, r, choice);
    }
    const bool needs_song_rows = program.song_rows;
    for (auto& row : state.rows) if (needs_song_rows && row.row && row.kind == native::RowKind::Song && !row.available && !row.failed) {
        if (!scope.step()) return true;
        UObjectLiveHandle identity{};
        if (!same_menu_session(row.session, scope.session) || !capture_live_uobject_handle(row.row, identity)
            || identity.internal_index != row.identity.internal_index || identity.serial_number != row.identity.serial_number) {
            row.failed = true; cancel_row_diagnostic(row); continue;
        }
        const auto bound = [&] {
            metrics::Span resolve(metrics::Metric::Resolve);
            return native::bind_row(native::RowKind::Song, row.row, row.index, row.association_setup, scope.authority(), row.ticket);
        }();
        if (!scope.qualify(scope.session)) { failed(event, "preparation_row_retired", bound); return false; }
        row.available = static_cast<bool>(bound); row.failed = !bound;
        if (!bound) cancel_row_diagnostic(row);
    }
    return refresh_composition(scope, event, diagnostics, SIZE_MAX, choice);
}
bool match_palette_row(void* context, const palette::Subject& subject) noexcept
{
    const auto* binding = static_cast<const PaletteBinding*>(subject.context);
    return binding && binding->row == *static_cast<const std::size_t*>(context);
}
bool match_palette_partition(void* context, const palette::Subject& subject) noexcept
{
    const auto* binding = static_cast<const PaletteBinding*>(subject.context);
    const auto& filter = *static_cast<const RetirementFilter*>(context);
    return binding && binding->row == filter.row && binding->retirement_current == filter.current;
}
bool current_song_association(std::size_t r, Scope& scope) noexcept
{
    const auto& row = state.rows[r];
    UObjectLiveHandle identity{}, context{};
    if (!row.available || row.kind != native::RowKind::Song || !same_menu_session(row.session, scope.session)
        || row.context != scope.session.widget || !capture_live_uobject_handle(row.row, identity)
        || !capture_live_uobject_handle(row.context, context)
        || identity.internal_index != row.identity.internal_index || identity.serial_number != row.identity.serial_number
        || context.internal_index != row.context_identity.internal_index || context.serial_number != row.context_identity.serial_number)
        return false;
    bool witnessed = false;
    SubjectChecks checks;
    const auto current = [&](const native::Subject& subject) {
        native::RowAssociation a{};
        const bool valid = native::subject_row_association(subject, a) && a.kind == native::RowKind::Song
            && a.logical_index == row.index && a.session_generation == row.session.generation
            && a.setup_generation == row.association_setup;
        witnessed |= valid;
        return valid;
    };
    for (const auto i : row.inventory.bindings) { const auto& b = state.bindings[i]; if (b.available) {
        if (!b.generic || !current(b.subject)) return false;
        if (!checks.brush(b, b.original)) return false;
    } }
    for (const auto i : row.inventory.palettes) { const auto& p = state.palettes[i]; if (p.available && (!current(p.target) || !checks.property(p.target))) return false; }
    return witnessed && static_cast<bool>(checks.run(scope));
}
bool suspend_song_style(std::size_t r, Scope& scope, Event& event) noexcept
{
    metrics::Span measured(metrics::Metric::SuspendRow);
    try {
        std::vector<std::size_t> changed;
        for (const auto i : state.rows[r].inventory.bindings) {
            const auto& b = state.bindings[i];
            if (b.row == r && b.available && b.canonical == i && b.shown != SIZE_MAX) changed.push_back(i);
        }
        std::vector<MemberGroup> groups;
        if (!make_member_groups(changed, 0, groups)) return false;
        for (const auto& group : groups) {
            native::ApplyProgress progress{};
            event.bridge = write_member_group(group, false, group.bindings.size(), scope.authority(), progress);
            if (!event.bridge || !progress.refresh_complete || progress.completed_members != group.bindings.size()
                || !scope.qualify(scope.session)) return false;
        }
        for (const auto i : changed) state.bindings[i].shown = SIZE_MAX;
        for (const auto i : state.rows[r].inventory.palettes) {
            auto& p = state.palettes[i];
            if (p.row != r || !p.available || p.canonical != i || same_policy(p.shown, {})) continue;
            // Projection leaves genuine UObject colors; Bottom still needs its exact attribute restored.
            if (p.subject.property != palette::Property::TextBottom) continue;
            event.bridge = palette::restore(p.subject, scope.authority());
            if (!event.bridge || !scope.qualify(scope.session)) return false;
            p.shown = {};
        }
        state.rows[r].style_pending = true;
        pending_row_diagnostic(state.rows[r]);
        return true;
    } catch (...) { event.bridge = {native::Outcome::Memory, "refresh_suspend_allocation"}; return false; }
}
bool finish_song_refresh(std::size_t r, Scope& scope, Event& event) noexcept
{
    metrics::Span measured(metrics::Metric::RefreshRow);
    try {
        // Original ran untinted. A live old handle does not prove that native
        // setup left its brush resource/geometry baseline unchanged.
        auto& row = state.rows[r];
        for (const auto i : row.inventory.bindings) {
            auto& b = state.bindings[i];
            b.available = false; b.original = {}; b.shown = SIZE_MAX;
        }
        for (const auto i : row.inventory.combinations) { state.combinations[i].available = false; state.combinations[i].image = {}; }
        row.refresh_palettes = true; // Force post-synchronization emission, including unchanged non-Bottom policies.
        if (!invalidate_song_row(r)) return false;
        if (!prepare_quantum(scope, event, nullptr, SIZE_MAX, r) || !repair_applied_rows(scope, event, r)) return false;
        return scope.qualify(scope.session);
    } catch (...) { event.bridge = {native::Outcome::Memory, "refresh_baseline_allocation"}; return false; }
}
bool retire_row(std::size_t row, Scope& scope, Event& event) noexcept
{
    metrics::Span measured(metrics::Metric::RetireRow);
    state.pending_rows.erase(row);
    cancel_row_diagnostic(state.rows[row]);
    state.rows[row].subjects.clear();
    const auto authority = scope.authority();
    std::vector<std::size_t> changed;
    std::vector<MemberGroup> groups;
    try {
        for (const auto i : state.rows[row].inventory.bindings) {
            const auto& b = state.bindings[i];
            if (b.row == row && b.available && b.canonical == i && b.shown != SIZE_MAX) changed.push_back(i);
        }
    } catch (...) { failed(event, "row_restore_group_allocation", {native::Outcome::Memory, "row_restore_group_allocation"}); return false; }
    if (!make_member_groups(changed, 0, groups)) { failed(event, "row_restore_group_allocation", {native::Outcome::Memory, "row_restore_group_allocation"}); return false; }
    for (const auto& group : groups) {
        native::ApplyProgress progress{};
        const auto result = write_member_group(group, false, group.bindings.size(), authority, progress);
        if (!result || !progress.refresh_complete || progress.completed_members != group.bindings.size()
            || !scope.qualify(scope.session)) { failed(event, "row_original_restore_unavailable", result); return false; }
    }
    std::size_t retired = 0;
    for (const auto i : state.rows[row].inventory.palettes) {
        const auto& p = state.palettes[i]; if (p.row == row && p.available && p.canonical == i) ++retired;
    }
    if (retired) {
        const auto result = palette::retire_matching(scope.session.widget, scope.session.generation, match_palette_row, &row, authority);
        if (!result || !scope.qualify(scope.session)) { failed(event, "row_palette_retire_unavailable", result); return false; }
        state.palette_current -= std::min(retired, state.palette_current);
        state.palette_charged -= std::min(retired, state.palette_charged);
    }
    for (const auto i : state.rows[row].inventory.bindings) { auto& b = state.bindings[i]; b.available = b.resolved = false; b.canonical = SIZE_MAX; b.shown = SIZE_MAX; b.original = {}; b.subject = {}; }
    for (const auto i : state.rows[row].inventory.combinations) { auto& c = state.combinations[i]; c.available = false; c.image = {}; }
    for (const auto i : state.rows[row].inventory.palettes) { auto& p = state.palettes[i]; p.available = p.resolved = false; p.canonical = SIZE_MAX; p.subject = {}; p.target = {}; p.shown = {}; }
    state.rows[row].available = false;
    event.result = "row_restored_and_revoked";
    return true;
}
void persist(const Event& e) noexcept
{
    if (!e.save) return;
    // No native lock or brush borrow survives here. Persistence never changes apply success.
    try {
        const auto& id = state.themes[e.chosen].id;
        const bool saved = core::save_selected_theme(state.ini, id);
        Event saved_event = e; saved_event.stage = "persistence";
        saved_event.result = saved ? "saved" : "write_failed_applied_program_retained";
        report(saved_event);
    } catch (...) {
        Event failure = e; failure.stage = "persistence"; failure.result = "exception_applied_program_retained";
        report(failure);
    }
}
} // namespace

void configure(HMODULE module, const std::filesystem::path& directory,
    const std::wstring& ini, const core::Config& config) noexcept
{
    try {
        std::lock_guard lock(state.mutex);
        if (state.configured || state.compromised.load()) return;
        state.configured = true; state.module = module; state.ini = ini;
        state.selected = config.selected_theme; state.key = gesture_key(config);
        state.selected.reserve(64);
        state.themes.push_back({"original"});
        std::uint64_t bytes = 0;
        std::size_t attempts = 0, loads = 0;
        std::error_code error;
        for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
            ++attempts;
            if (!it->is_directory(error)) continue;
            const auto id = it->path().filename().string();
            if (id == "original" || !core::valid_theme_id(id)) continue;
            core::ThemeDefinition definition; std::string reason;
            ++loads;
            bool accepted = false;
            try { accepted = core::load_theme(directory, id, definition, reason) && retain(std::move(definition), bytes, reason); }
            catch (...) { reason = "theme_allocation_failed"; }
            if (!accepted) {
                core::log(core::LogLevel::Info,
                    "[menu_theme_runtime] stage=prepare result=rejected id=" + id
                    + " detail=" + reason);
            }
        }
        std::sort(state.themes.begin() + 1, state.themes.end(), [](const Prepared& a, const Prepared& b) { return a.id < b.id; });
        const auto result = native::configure(module, FF7RP_TARGET_BUILD_ID); // authentication only
        state.palette_configured = static_cast<bool>(palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, read_palette_stack}));
        if (result && state.themes.size() > 1) {
            bind_owned_row_setup(observe_row_setup, observe_row_retired);
            bind_owned_selection(observe_selection);
        }
        if (!result) state.compromised.store(true);
        const bool preview = std::any_of(state.themes.begin(), state.themes.end(), [](const auto& theme) { return theme.program.preview.overrides != 0; });
        if (result && (!state.bindings.empty() || !state.palettes.empty() || preview))
            bind_owned_input(callback_entered, function_key, poll, focus_lost, posted_input);
        metrics::set_sink(metrics_log);
        metrics::enable(result && state.themes.size() > 1 && (!state.bindings.empty() || !state.palettes.empty() || preview));
        core::log(core::LogLevel::Info, "[menu_theme_runtime] stage=configure backend=typed_main_song_speed_cursor_palette prepared="
            + std::to_string(state.themes.size() - 1) + " retained_bytes=" + std::to_string(bytes)
            + " bindings=" + std::to_string(state.bindings.size()) + " combinations=" + std::to_string(state.combinations.size())
            + " candidates=" + std::to_string(attempts) + " loads=" + std::to_string(loads)
            + " gesture=" + (state.key ? "enabled" : config.theme_function_key ? "disabled_conflict_or_invalid" : "disabled")
            + " native=" + result.stage + " full_theme=false");
    } catch (...) {
        metrics::enable(false);
        state.compromised.store(true);
        core::log(core::LogLevel::Info, "[menu_theme_runtime] stage=configure result=optional_unavailable");
    }
}
void callback_entered() noexcept
{
    palette::NativeInvocation invocation{};
    if (palette::current_invocation(invocation)) retired_palette_serial = std::max(retired_palette_serial, invocation.serial);
    if (active_scope) {
        active_scope->retired = true;
        state.compromised.store(true, std::memory_order_release);
    }
}
void retire() noexcept
{
    callback_entered();
    if (active_scope) return; // Never lock a nonrecursive mutex owned by this stack.
    Event event; event.stage = "retire"; event.result = "owner_revoked_no_close_authority";
    std::unique_lock lock(state.mutex);
    if (!state.owner) return;
    state.compromised.store(true, std::memory_order_release);
    event.generation = state.owner.generation; event.ticket = state.pending.ticket;
    event.preparation_facts = state.preparation_facts;
    clear_owner(); lock.unlock();
    report(event);
}
void ready(void* list, std::uint64_t notified_generation, const void* frame,
    const core::HookCallbackGate::Lease& lease) noexcept
{
    metrics::Span measured(metrics::Metric::Ready);
    const auto diagnostic_started = diagnostic_now();
    if (active_scope) { callback_entered(); return; }
    Event event; event.stage = "ready";
    std::vector<Event> diagnostics;
    // The poller holds this only for bounded value copies, never posting or native work.
    // Do not lose a close/Ready transition merely because it sampled concurrently.
    std::unique_lock lock(state.mutex);
    if (!state.configured || state.themes.size() < 2 || state.compromised.load()) return;
    const auto session = capture_ready_menu_session(); event.generation = session.generation;
    metrics::annotate({session.generation});
    if (!session || session.list != list || session.generation != notified_generation) return;
    {
        Scope scope(Source::Ready, frame, lease, session);
        if (!scope.qualify(session)) failed(event, "ready_authority");
        else if (state.owner) {
            // Never adopt a possibly mutated menu as a new original after a missed close.
            if (!same_menu_session(state.owner, session)) failed(event, "unretired_owner");
            else event.result = "already_prepared";
        } else {
            invalidate_preparation(); state.subjects.clear();
            state.owner = session; state.owner_thread = scope.thread; state.applied = 0; state.themes[0].available = true;
            state.diagnostic_ready_ns = diagnostic_started; state.diagnostic_ready_generation = session.generation;
            publish_song_preview_palette(session.generation, {});
            state.preparation_facts = {}; state.preparation_facts.generation = session.generation;
            state.work_pending = false; state.subjects_prepared = false; state.preparation_cursor = 1;
            state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
            state.desired_epoch = state.focus_epoch.load();
            for (std::size_t i = 1; i < state.themes.size(); ++i) {
                state.themes[i].preparing = true;
                if (state.themes[i].id == state.selected) { state.desired = i; state.desired_save = true; }
            }
            update_work_pending();
            state.semantic_selected = -1;
            (void)core::safe_copy_bytes(static_cast<const std::uint8_t*>(session.widget)
                + runtime_layouts::PianoMusicList::selected_index, &state.semantic_selected, sizeof(state.semantic_selected));
            for (auto& row : state.rows) {
                if (state.compromised.load()) break;
                if (row.kind != native::RowKind::Song || !row.row || row.session.generation != session.generation || row.session.controller != session.controller
                    || row.session.list != session.list || row.session.list_ownership != session.list_ownership
                    || row.context != session.widget || row.context_identity.internal_index != session.widget_identity.internal_index
                    || row.context_identity.serial_number != session.widget_identity.serial_number) { cancel_row_diagnostic(row); continue; }
                UObjectLiveHandle identity{};
                if (!capture_live_uobject_handle(row.row, identity) || identity.internal_index != row.identity.internal_index
                    || identity.serial_number != row.identity.serial_number) { row.available = false; row.failed = true; cancel_row_diagnostic(row); continue; }
                row.session = session;
                row.available = false;
            }
            event.result = "original_ready_preparation_scheduled";
            if (state.desired < state.themes.size()) {
                // One finite requested program, under this genuine opening authority.
                if (prepare_quantum(scope, event, &diagnostics, state.desired) && state.themes[state.desired].available) {
                    const auto desired = state.desired;
                    state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                    (void)apply_choice(scope, desired, event, true);
                } else if (state.desired < state.themes.size() && !state.themes[state.desired].preparing
                    && !awaiting_receivers(state.themes[state.desired].admission_stage)) {
                    state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                }
                update_work_pending();
            }
        }
    }
    for (auto& diagnostic : diagnostics)
        if (std::string_view(diagnostic.stage) == "program_admission") diagnostic.preparation_facts = state.preparation_facts;
    if (std::string_view(event.stage) == "program_admission") event.preparation_facts = state.preparation_facts;
    lock.unlock();
    for (const auto& diagnostic : diagnostics) report(diagnostic);
    report(event); persist(event);
    wake_preparation_after_ready();
}
void close(void* list, const void* frame, const core::HookCallbackGate::Lease& lease) noexcept
{
    metrics::Span measured(metrics::Metric::Close);
    const auto started = GetTickCount64(); // Runtime restoration only, not the native close original.
    if (active_scope) { callback_entered(); return; }
    Event event; event.stage = "restore";
    std::unique_lock lock(state.mutex);
    if (!state.owner || state.owner.list != list) return;
    event.generation = state.owner.generation;
    metrics::annotate({event.generation});
    event.ticket = state.pending.ticket; // Correlates any queued request invalidated by this close.
    {
        Scope scope(Source::Close, frame, lease, state.owner);
        if (!scope.qualify(state.owner)) failed(event, "restore_authority");
        else if (state.applied) {
            if (!apply_choice(scope, 0, event, false, Transition::Close)) failed(event, "restore", event.bridge);
            else { event.stage = "restore"; event.result = "restored_original"; }
        } else event.result = "original_unchanged";
        if (state.palette_current && scope.qualify(scope.session)) {
            const auto current = state.palette_current;
            const auto result = palette::retire(scope.session.widget, scope.session.generation, scope.authority());
            event.bridge = result;
            if (result && scope.qualify(scope.session)) {
                state.palette_charged -= current; state.palette_current = 0;
            } else failed(event, "palette_retire_unavailable", result);
        }
        // Always retire, including when reentrant close revoked this stack.
        event.preparation_facts = state.preparation_facts;
        clear_owner();
    }
    event.elapsed_ms = GetTickCount64() - started; lock.unlock();
    report(event);
}
int function_key() noexcept { return state.key; } // Immutable after pre-service configuration.
void focus_lost() noexcept
{
    state.focused.store(false);
    state.focus_epoch.fetch_add(1); // Loss/regain can occur entirely between poll samples.
}
std::uint64_t reserve_preparation_continuation() noexcept
{
    if (active_scope || state.compromised.load() || state.row_rebind.load()) return 0;
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return 0;
    if (state.pending.ticket == state.cancelled_ticket.load()) state.pending = {};
    if (state.desired_gesture && state.desired_epoch != state.focus_epoch.load()) {
        state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
        update_work_pending();
    }
    const auto session = capture_ready_menu_session();
    if (!state.work_pending || state.pending.ticket || !state.owner
        || !same_menu_session(state.owner, session) || state.sequence == UINT64_MAX) return 0;
    const auto ticket = ++state.sequence;
    state.pending = {ticket, state.focus_epoch.load(), session, Direction::Next, true, GetTickCount64(), diagnostic_now()};
    return ticket;
}
void cancel_preparation_continuation(std::uint64_t ticket) noexcept
{
    auto cancelled = state.cancelled_ticket.load();
    while (cancelled < ticket && !state.cancelled_ticket.compare_exchange_weak(cancelled, ticket)) {}
}
bool post_preparation_continuation(std::uint64_t ticket) noexcept
{
    if (!ticket || state.compromised.load()) return false;
    {
        std::unique_lock lock(state.mutex, std::try_to_lock);
        if (!lock.owns_lock() || state.pending.ticket != ticket || !state.pending.preparation || state.pending.continuation_posted
            || state.cancelled_ticket.load() == ticket || state.row_rebind.load()
            || !same_menu_session(state.pending.owner, state.owner)
            || !same_menu_session(state.owner, capture_ready_menu_session())) return false;
        state.pending.continuation_posted = true;
    }
    // Cancellation can race submission; the actual dispatch still consumes and
    // checks the exact ticket, session and focus epoch before fresh admission.
    return post_input(ticket, true);
}
void poll(bool focused, bool keyboard, bool previous, bool next) noexcept
{
    metrics::flush(); // Also drains completed close/commit roots after the owner has gone away.
    keyboard = keyboard && state.key != 0;
    const bool held = keyboard || previous || next;
    const bool was_focused = state.focused.exchange(focused);
    if (was_focused && !focused) state.focus_epoch.fetch_add(1);
    const bool was_held = state.held.exchange(held);
    const bool rising = held && !was_held;
    // Always sample held, including ineligible/focus-loss periods: no held-entry trigger.
    const bool gesture = focused && rising && !(previous && (next || keyboard));
    if (state.compromised.load()) return;
    Event event; event.stage = "request"; event.result = "busy";
    std::uint64_t ticket = 0;
    bool preparation = false;
    {
        std::unique_lock lock(state.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            const auto session = capture_ready_menu_session(); event.generation = session.generation;
            if (state.pending.ticket == state.cancelled_ticket.load()) state.pending = {};
            if (state.desired_gesture && state.desired_epoch != state.focus_epoch.load()) {
                state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                update_work_pending();
            }
            if (!gesture && !state.work_pending) return;
            if (!gesture && preparation_dispatch_active()) return;
            if (gesture && state.desired_gesture
                && state.desired_direction == (previous ? Direction::Previous : Direction::Next)
                && std::none_of(state.themes.begin(), state.themes.end(), [](const Prepared& theme) {
                    return theme.available && &theme != &state.themes[state.applied];
                })) return;
            if (state.pending.ticket && !state.pending.preparation && state.pending.focus_epoch != state.focus_epoch.load()) state.pending = {};
            if (gesture && state.pending.preparation) {
                state.cancelled_ticket.store(state.pending.ticket); state.pending = {};
            }
            if (!state.owner || !same_menu_session(state.owner, session)) event.result = "no_ready_owner";
            else if (state.row_rebind.load()) event.result = "row_rebind_in_progress";
            else if (state.pending.ticket) event.result = "already_pending";
            else if (state.sequence == UINT64_MAX) event.result = "ticket_exhausted";
            else {
                ticket = ++state.sequence;
                state.pending = {ticket, state.focus_epoch.load(), session,
                    previous ? Direction::Previous : Direction::Next, !gesture, GetTickCount64(), diagnostic_now()};
                preparation = !gesture;
                event.ticket = ticket; event.result = "queued";
            }
        }
    }
    if (ticket && !post_input(ticket, preparation)) {
        state.cancelled_ticket.store(ticket); // Next edge/dispatch discards it, even if UI currently holds the mutex.
        event.result = "post_failed";
    }
    const bool failed_post = ticket && std::string_view(event.result) == "post_failed";
    if (gesture || (failed_post && !state.preparation_post_failed.exchange(true))) report(event);
    if (ticket && !failed_post) state.preparation_post_failed.store(false);
}
void posted_input(const void* frame, std::uint64_t ticket, const core::HookCallbackGate::Lease& lease) noexcept
{
    metrics::Span measured(metrics::Metric::Dispatch);
    const auto diagnostic_started = diagnostic_now();
    if (active_scope) { callback_entered(); return; }
    Event event; event.stage = "dispatch"; event.ticket = ticket;
    std::vector<Event> diagnostics;
    const auto started = GetTickCount64();
    std::unique_lock lock(state.mutex);
    const auto service_started = GetTickCount64();
    bool measured_dispatch = false;
    if (!ticket || state.pending.ticket != ticket) return;
    const auto request = state.pending; state.pending = {}; // Consume once BEFORE acquiring mutation authority.
    if (state.desired_gesture && state.desired_epoch != state.focus_epoch.load()) {
        state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
        update_work_pending();
    }
    const auto session = capture_ready_menu_session();
    event.generation = request.owner.generation;
    metrics::annotate({event.generation});
    if (state.row_rebind.load()) event.result = "row_rebind_in_progress";
    else if (state.compromised.load() || state.cancelled_ticket.load() == ticket
        || (request.direction != Direction::Previous && request.direction != Direction::Next)
        || (!request.preparation && (!state.focused.load() || request.focus_epoch != state.focus_epoch.load()))
        || !request.owner || !same_menu_session(request.owner, session) || !same_menu_session(session, state.owner))
        event.result = "stale_cancelled_or_unavailable";
    else {
        Scope scope(request.preparation ? Source::Preparation : Source::PostedInput, frame, lease, session, ticket, request.focus_epoch);
        if (!scope.qualify(session)) failed(event, "posted_input_authority");
        else if (request.preparation) {
            if (request.diagnostic_enqueued_ns && diagnostic_started >= request.diagnostic_enqueued_ns)
                metrics::sample(metrics::Metric::PreparationQueueWait, diagnostic_started - request.diagnostic_enqueued_ns);
            measured_dispatch = true;
            event.stage = "preparation";
            if (prepare_quantum(scope, event, &diagnostics)) {
                if (!scope.qualify(session)) failed(event, "preparation_retired");
                else if (!repair_applied_rows(scope, event)) {
                    if (!state.compromised.load() && !apply_choice(scope, 0, event, false)) failed(event, "preparation_row_repair_restore_failed");
                }
                else if (state.desired < state.themes.size() && state.themes[state.desired].available) {
                    const auto desired = state.desired; const bool save = state.desired_save;
                    state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                    if (!apply_choice(scope, desired, event, save) && !state.compromised.load()
                        && state.applied && !state.themes[state.applied].available) {
                        if (!apply_choice(scope, 0, event, false)) failed(event, "preparation_original_restore_failed");
                        else { event.chosen = desired; event.result = "requested_program_rejected_original_restored"; }
                    }
                } else if (state.desired < state.themes.size() && !state.themes[state.desired].preparing) {
                    const auto rejected = state.desired;
                    const auto* stage = state.themes[rejected].admission_stage;
                    const bool awaiting = awaiting_receivers(stage);
                    if (!awaiting) {
                        state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                        update_work_pending(); // Retire earlier passive binds even when Original was already applied.
                    }
                    if (state.applied && !state.themes[state.applied].available) {
                        if (!apply_choice(scope, 0, event, false)) failed(event, "preparation_original_restore_failed");
                        else event.result = "requested_program_rejected_original_restored";
                    } else if (awaiting) event.stage = "none"; // Admission already reported the missing receiver; wait for a real lifecycle event.
                    else event.result = "requested_program_rejected";
                    event.chosen = rejected;
                } else event.stage = "none";
            }
        }
        else {
            if (state.desired_gesture) {
                if (state.desired_direction != request.direction) {
                    state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                    update_work_pending();
                    if (quiet_original_palettes(scope, event)) event.result = "pending_gesture_cancelled_original";
                } else { event.chosen = state.desired; event.result = "gesture_already_pending"; }
            } else {
                auto choice = state.applied;
                for (std::size_t n = 0; n < state.themes.size(); ++n) {
                    choice = request.direction == Direction::Previous
                        ? (choice ? choice - 1 : state.themes.size() - 1) : (choice + 1) % state.themes.size();
                    if (!choice || (state.themes[choice].backend && !state.themes[choice].rejected)) break;
                }
                if (choice == state.applied) event.result = "no_other_admitted_program";
                else if (state.themes[choice].available) {
                    state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                    (void)apply_choice(scope, choice, event);
                } else {
                        state.desired = choice; state.desired_save = state.desired_gesture = true;
                        state.desired_direction = request.direction; state.desired_epoch = request.focus_epoch;
                        // Original deliberately stops observing inactive Speed receivers.
                        if (demanded_scope(plan::Scope::SpeedIcon)) state.speed_dirty = true;
                        if (!state.themes[choice].preparing) state.themes[choice].work.revision = 0;
                        state.themes[choice].preparing = true;
                        update_work_pending(); event.chosen = choice; event.result = "gesture_pending_whole_program";
                         // Service one finite requested program, never a pending peer row
                         // or inactive candidate. Real missing receivers defer without retry.
                        if (prepare_quantum(scope, event, &diagnostics, choice) && state.themes[choice].available) {
                            state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                            (void)apply_choice(scope, choice, event);
                        } else if (!state.compromised.load() && !state.themes[choice].preparing
                            && !awaiting_receivers(state.themes[choice].admission_stage)) {
                            state.desired = SIZE_MAX; state.desired_save = state.desired_gesture = false;
                            update_work_pending(); event.chosen = choice; event.result = "requested_program_rejected";
                        }
                }
            }
        }
    }
    if (measured_dispatch && state.preparation_facts.generation == request.owner.generation) {
        auto& facts = state.preparation_facts;
        add_fact(facts.dispatches, 1);
        // Only a consumed, qualified preparation message establishes delivery.
        // Queue wait includes PostMessage submission, but excludes this lock wait.
        add_fact(facts.queue_wait_ms, started - request.enqueued_ms);
        add_fact(facts.service_ms, GetTickCount64() - service_started);
    }
    for (auto& diagnostic : diagnostics)
        if (std::string_view(diagnostic.stage) == "program_admission") diagnostic.preparation_facts = state.preparation_facts;
    if (std::string_view(event.stage) == "program_admission") event.preparation_facts = state.preparation_facts;
    lock.unlock();
    event.elapsed_ms = GetTickCount64() - started;
    for (const auto& diagnostic : diagnostics) report(diagnostic);
    if (request.preparation && event.elapsed_ms >= 16 && std::string_view(event.stage) == "none") {
        event.stage = "preparation"; event.result = "slow_preparation_unit";
    }
    report(event); persist(event);
}
void install_palette_hooks() noexcept
{
    if (!state.palette_configured) return;
    Event event; event.stage = "palette_hooks"; event.bridge = palette::install();
    state.palette_installed = static_cast<bool>(event.bridge);
    event.result = event.bridge ? "installed" : "optional_unavailable"; report(event);
}
bool uninstall_palette_hooks() noexcept
{
    if (!state.palette_configured) return true;
    Event event; event.stage = "palette_hooks"; event.bridge = palette::uninstall();
    event.result = event.bridge ? "drained_uninstalled" : "retained_retry_required"; report(event);
    if (event.bridge) { state.palette_configured = false; state.palette_installed = false; }
    return static_cast<bool>(event.bridge);
}
void disable() noexcept
{
    state.compromised.store(true, std::memory_order_release);
    retire();
    native::disable(); // retained roots and irreversible budgets stay bridge-owned
}
} // namespace ff7r::piano::game::menu_theme_runtime
