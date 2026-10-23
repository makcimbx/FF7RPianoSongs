// Same-TU control-flow test: copied facts and stateful fake native UI only.
#include "game/menu_theme_runtime.h"
#include "game/menu_theme_observation.h"
#include "game/menu_theme_native.h"
#include "core/pe_image.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
#include <new>
#include <thread>
#include <tuple>
#include <stdexcept>
#include <vector>

namespace {
HMODULE fake_module = nullptr;
DWORD mapping_protection=PAGE_EXECUTE_READ, mapping_type=MEM_IMAGE, mapping_state=MEM_COMMIT;
bool mapping_query_succeeds=true, mapping_foreign_owner=false, mapping_allow_execute_only=false;
unsigned mapping_queries=0, executable_mapping_calls=0, observed_return_reads=0;
const void* observed_return_slot=nullptr;
SIZE_T WINAPI mapped_query(LPCVOID, PMEMORY_BASIC_INFORMATION info, SIZE_T) {
    ++mapping_queries;
    if(!mapping_query_succeeds)return 0;
    *info = {}; info->AllocationBase = mapping_foreign_owner ? nullptr : fake_module; info->State = mapping_state;
    info->Type = mapping_type; info->Protect = mapping_protection; return sizeof(*info);
}
bool control = true;
ULONGLONG work_clock = 0;
LONGLONG performance_ticks = 0, performance_step = 64;
bool performance_valid=true;
unsigned palette_changes = 0;
unsigned palette_change_attempts = 0, fail_palette_change_at = 0;
bool reenter_palette_change = false;
bool slow_capture = false;
bool replace_label_after_capture=false;
bool replace_cursor_after_capture=false;
bool slow_palette_bind = false, fail_next_palette_bind = false;
unsigned palette_bind_attempts=0,fail_palette_bind_at=0;
unsigned subject_validations = 0;
unsigned batch_calls = 0, button_publications = 0, image_synchronizes = 0;
unsigned filtered_retire_calls = 0, retirement_visits = 0;
std::size_t fail_batch_member = 0;
bool fail_batch_refresh = false, matching_retirement = false;
bool native_poisoned = false;
ULONGLONG WINAPI preparation_clock() { return work_clock; }
BOOL WINAPI preparation_counter(LARGE_INTEGER* value) {
    if(!performance_valid)return FALSE;
    performance_ticks += performance_step;
    value->QuadPart = static_cast<LONGLONG>(work_clock) * 1000 + performance_ticks; return TRUE;
}
BOOL WINAPI preparation_frequency(LARGE_INTEGER* value) { value->QuadPart = 1000000; return performance_valid; }
}
// Keep the real value predicate, replacing only fact acquisition used by runtime.
#define capture_native_facts unused_production_capture_native_facts
#include "game/menu_theme_observation.cpp"
#undef capture_native_facts
#include "game/menu_theme_plan.cpp"
#include "game/song_preview_overlay.cpp"
#define VirtualQuery mapped_query
#define GetTickCount64 preparation_clock
#define QueryPerformanceCounter preparation_counter
#define QueryPerformanceFrequency preparation_frequency
#include "game/menu_theme_runtime.cpp"
#undef QueryPerformanceFrequency
#undef QueryPerformanceCounter
#undef GetTickCount64
#undef VirtualQuery

namespace runtime = ff7r::piano::game::menu_theme_runtime;
namespace native = ff7r::piano::game::menu_theme_native;
namespace obs = ff7r::piano::game::menu_theme_observation;
namespace game = ff7r::piano::game;
namespace core = ff7r::piano::core;
namespace {
game::MenuSessionSnapshot current;
core::HookCallbackGate gate;
unsigned creates = 0, lookups = 0, captures = 0, composes = 0, borrows = 0, applies = 0, saves = 0;
unsigned original_calls = 0;
unsigned loads = 0;
std::size_t retained_images = 0;
std::uint64_t retained_bytes = 0;
bool reject_loads = false, fail_post = false, polling = false;
bool ready_wake_enabled=true;
unsigned ready_wake_calls=0;
std::uint64_t queued_ticket = 0;
struct DispatchFrame { std::uint64_t ticket; game::MenuSessionSnapshot owner; const core::HookCallbackGate::Lease& lease; bool preparation; };
std::map<std::uint64_t, bool> posted_purposes;
DispatchFrame* dispatch_frame = nullptr;
bool fail_native = false, fail_save = false, nested_create = false, nested_apply = false;
bool nested_selection = false;
unsigned selection_originals = 0;
unsigned fail_apply_at = 0, nested_apply_at = 0;
bool fail_rollback = false, apply_has_failed = false;
std::string missing_leaf, fail_compose_leaf, fail_borrow_leaf;
enum class Kind { Prepared, Original, Composed };
struct Brush { Kind kind; std::string leaf; int value = 0, geometry = 0; std::uint64_t generation = 0, subject = 0; };
std::map<std::uint64_t, Brush> brushes;
std::uint64_t next_brush = 0;
std::uint64_t brush_token(const native::OwnedImage& image) {
    std::uint64_t token = 0; std::memcpy(&token, &image, sizeof(token)); return token;
}
void publish_brush(native::OwnedImage& image, const Brush& brush) {
    const auto token = ++next_brush;
    std::memcpy(&image, &token, sizeof(token)); brushes[token] = brush;
}
unsigned capture_batches = 0, capture_api_calls = 0;
unsigned resolve_batches=0, resolve_requests=0, resolve_max_members=0;
unsigned preflight_batches=0, preflight_requests=0, preflight_max_images=0;
unsigned resolve_compose_reuses=0;
unsigned unbatched_work_calls=0;
bool in_member_resolver=false;
std::map<std::pair<std::size_t,std::size_t>,native::OwnedImage> resolved_compositions;
native::CaptureProgress last_capture_progress{};
unsigned fail_capture_member = 0, retire_capture_member = 0;
bool fail_capture_fault = false;
bool in_capture_batch = false;
std::map<std::string, native::OwnedImage> original_cache;
std::map<const native::ImageTarget*, std::string> targets;
std::map<std::string, int> visuals, originals;
bool generic_enabled = false, fail_palette_install = false, fail_palette_uninstall = false, palette_hooks = false;
bool generic_tint_enabled = false;
unsigned tint_calls = 0;
std::map<const native::OwnedImage*, std::array<float, 4>> tinted_brushes;
unsigned palette_installs = 0, palette_uninstalls = 0, row_binds = 0;
unsigned subject_retires = 0, owner_retires = 0, row_originals_called = 0;
int native_selected = -1;
bool subject_names_ready = false, fail_subject_names = false, fail_speed_read = false, fail_speed_bind = false;
unsigned subject_prepares = 0, speed_reads = 0, speed_binds = 0, speed_bind_attempts = 0;
std::vector<void*> speed_members;
std::map<void*, std::uint64_t> row_trees;
std::map<std::wstring, native::SubjectKind> widget_kinds;
struct GenericSubject { std::wstring leaf; native::SubjectKind kind; void* widget; std::uint64_t generation; void* row = nullptr; std::uint64_t setup = 0;
    std::uint64_t epoch = 0, tree = 0; int serial = 0; std::uint64_t version = 0;
    bool cursor = false; std::uint64_t cursor_provider = 0; };
bool cursor_present = true;
std::uint64_t cursor_provider = 1;
unsigned cursor_lookups = 0;
std::string subject_leaf(const GenericSubject& s) {
    std::string leaf(s.leaf.begin(), s.leaf.end());
    if (s.cursor) leaf = "cursor/" + leaf;
    if (s.row) leaf = std::to_string(reinterpret_cast<std::uintptr_t>(s.row)) + "/" + leaf;
    return leaf;
}
struct RowAssociation { void* row; int index; std::uint64_t setup, generation; std::uint64_t epoch = 0; };
std::map<const native::RowTicket*, RowAssociation> row_tickets;
// Fake opaque value handles must survive Subject copies, like the real descriptor.
std::uint64_t next_subject = 0;
std::uint64_t subject_token(const native::Subject& subject) {
    std::uint64_t token = 0; std::memcpy(&token, &subject, sizeof(token)); return token;
}
std::map<std::uint64_t, GenericSubject> generic_subjects;
std::map<std::uint64_t, native::ImageTarget> generic_targets;
std::map<std::pair<std::wstring, native::SubjectKind>, unsigned> subject_probes;
std::map<std::wstring, std::uint64_t> widget_versions;
unsigned subject_accesses = 0;
unsigned reenter_access_at = 0;
bool nested_lookup = false;
std::map<void*, game::UObjectLiveHandle> row_handles;
std::map<void*, bool> active_rows;
runtime::RowSetupObserver row_observer = nullptr;
runtime::RowSetupRetired row_retired = nullptr;
runtime::SelectionObserver selection_observer = nullptr;
struct RowFrame { void* row; int index; std::uint64_t setup; bool after; bool retired = false, refresh = false, target_current = true; };
RowFrame* row_frame = nullptr;
struct SelectionFrame { int index; bool after = true; };
SelectionFrame* selection_frame = nullptr;
namespace palette = game::menu_theme_palette;
std::map<palette::Property, unsigned> palette_restores;
std::map<palette::Property, unsigned> palette_changes_by_property;
palette::CallbackReader palette_reader{};
struct ColorSource { palette::Subject subject; palette::Policy policy; std::array<float, 4> native, shown; bool stale = false; };
std::map<std::pair<void*, palette::Property>, ColorSource> color_sources;
std::map<std::pair<void*, palette::Property>, std::array<float, 4>> color_world;
palette::NativeInvocation* palette_invocation = nullptr;
bool gc = false, purge = false, wrong_tls = false, incomplete = false;
unsigned native_fact_captures = 0;
bool fail_palette_retirement = false;
const native::OwnedImage* displayed = nullptr;
const core::HookCallbackGate::Lease* current_lease = nullptr;
std::string saved;
std::vector<std::string> messages;
void (*bound_entry)() noexcept = nullptr;
void (*bound_input)(const void*, std::uint64_t, const core::HookCallbackGate::Lease&) noexcept = nullptr;
void require(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "menu_theme_runtime_selftest: " << what << '\n'
            << "calls lookup=" << lookups << " capture=" << captures << " create=" << creates
            << " compose=" << composes << " tint=" << tint_calls << " borrow=" << borrows
            << " apply=" << applies << " palette_change=" << palette_changes
            << " subject_validate=" << subject_validations << " save=" << saves << '\n'
            << "owner_generation=" << runtime::state.owner.generation << " applied=" << runtime::state.applied
            << " compromised=" << runtime::state.compromised.load() << " retained=" << retained_images
            << "/" << retained_bytes << " runtime_attempted=" << runtime::state.native_attempts
            << "/" << runtime::state.native_attempted_bytes << '\n';
        for (std::size_t i = 0; i < runtime::state.themes.size() && i < 8; ++i) {
            const auto& theme = runtime::state.themes[i];
            std::cerr << "program " << theme.id << " available=" << theme.available << " rejected=" << theme.rejected
                << " stage=" << (theme.admission_stage ? theme.admission_stage : "none") << '\n';
        }
        for (std::size_t i = messages.size() > 8 ? messages.size() - 8 : 0; i < messages.size(); ++i)
            std::cerr << messages[i] << '\n';
        std::exit(1);
    }
}
core::ThemeDefinition definition(const char* id) {
    core::ThemeDefinition d; d.id = id;
    d.images["MusicList/Bg_1"] = {{std::uint8_t(std::string_view(id) == "sun" ? 2 : 1), 2, 3, 4}, 1, 1}; return d;
}
core::ThemeDefinition sized_definition(const char* id, std::uint32_t width, std::uint32_t height) {
    auto d = definition(id);
    auto& pixels = d.images.begin()->second;
    pixels.width = width; pixels.height = height;
    pixels.bgra.resize(std::size_t(width) * height * 4);
    return d;
}
void empty_inventory() {
    auto& s = runtime::state;
    runtime::retired_palette_serial = 0;
    s.themes.clear(); s.resources.clear(); s.bindings.clear(); s.combinations.clear(); s.retained_entries = 0;
    s.themes.push_back({"original"});
    s.palettes.clear();
    s.binding_templates = s.combination_templates = s.palette_templates = 0;
}
core::ThemeDefinition images(const char* id, std::initializer_list<std::pair<const char*, std::uint8_t>> entries) {
    core::ThemeDefinition d; d.id = id;
    for (const auto& [selector, value] : entries) d.images[selector] = {{value, 2, 3, 4}, 1, 1};
    return d;
}
void reset() {
    mapping_protection=PAGE_EXECUTE_READ;mapping_type=MEM_IMAGE;mapping_state=MEM_COMMIT;
    mapping_query_succeeds=true;mapping_foreign_owner=false;mapping_allow_execute_only=false;
    mapping_queries=executable_mapping_calls=observed_return_reads=0;observed_return_slot=nullptr;
    game::publish_song_preview_palette(0, {});
    auto& s = runtime::state;
    s.palettes.clear(); s.binding_templates = s.combination_templates = s.palette_templates = 0;
    s.native_attempts = 0; s.native_attempted_bytes = 0; s.preparation_cursor = 1;
    s.changed.clear(); s.changed_colors.clear(); s.prior_policies.clear();
    s.work_pending = s.subjects_prepared = s.prepared_once = false; s.desired = SIZE_MAX; s.desired_save = s.desired_gesture = false;
    posted_purposes.clear();
    work_clock = 0; performance_ticks = 0; performance_step = 64;performance_valid=true;
    slow_capture = slow_palette_bind = fail_next_palette_bind = false; subject_validations = 0;
    replace_label_after_capture=false;
    replace_cursor_after_capture=false;
    palette_bind_attempts=fail_palette_bind_at=0;
    batch_calls = button_publications = image_synchronizes = filtered_retire_calls = retirement_visits = 0;
    fail_batch_member = 0; fail_batch_refresh = matching_retirement = false;
    native_poisoned = false; // reset models a fresh backend/process, not configure/disable
    palette_changes = palette_change_attempts = fail_palette_change_at = 0;
    reenter_palette_change = false; palette_restores.clear(); palette_changes_by_property.clear();
    s.desired_epoch = 0; s.preparation_post_failed = false;
    s.row_rebind.store(nullptr); s.untracked_row = false;
    s.themes.clear(); s.resources.clear(); s.bindings.clear(); s.combinations.clear(); s.retained_entries = 0;
    s.owner = {}; s.owner_thread = 0; s.applied = 0; runtime::retired_palette_serial = 0;
    s.ini = L"fake.ini"; s.selected = "original"; s.selected.reserve(64);
    s.key = 6; s.held = false; s.focused = false; s.focus_epoch = 0; s.cancelled_ticket = 0;
    s.pending = {}; s.sequence = 0; s.configured = true; s.compromised.store(false);
    s.preparation_facts = {};
    s.diagnostic_ready_ns = s.diagnostic_ready_generation = 0;
    fake_module = reinterpret_cast<HMODULE>(0x10000000);
    s.module = fake_module;
    s.themes.push_back({"original"});
    std::uint64_t bytes = 0;
    std::string reason;
    require(runtime::retain(definition("night"), bytes, reason) && runtime::retain(definition("sun"), bytes, reason), "prepared definitions");
    current = {7, game::MenuSessionPhase::Ready, reinterpret_cast<void*>(1), reinterpret_cast<void*>(2),
        reinterpret_cast<void*>(3), {3, 10}, game::MenuListOwnership::Managed};
    creates = lookups = captures = composes = borrows = applies = saves = original_calls = 0;
    loads = 0; retained_images = 0; retained_bytes = 0;
    queued_ticket = 0; dispatch_frame = nullptr; fail_post = polling = reject_loads = false;
    ready_wake_enabled=true;ready_wake_calls=0;
    bound_entry = nullptr; bound_input = nullptr;
    fail_native = fail_save = nested_create = nested_apply = gc = purge = wrong_tls = incomplete = false;
    native_fact_captures = 0;
    fail_palette_retirement = false;
    nested_selection = false; selection_originals = 0;
    fail_apply_at = nested_apply_at = 0; fail_rollback = apply_has_failed = false;
    missing_leaf.clear(); fail_compose_leaf.clear(); fail_borrow_leaf.clear();
    brushes.clear(); next_brush = 0; targets.clear(); visuals.clear(); originals.clear();
    capture_batches = capture_api_calls = fail_capture_member = retire_capture_member = 0;
    resolve_batches=resolve_requests=resolve_max_members=0;
    preflight_batches=preflight_requests=preflight_max_images=0;
    resolve_compose_reuses=0;resolved_compositions.clear();
    unbatched_work_calls=0;in_member_resolver=false;
    last_capture_progress = {}; fail_capture_fault = in_capture_batch = false; original_cache.clear();
    generic_enabled = fail_palette_install = fail_palette_uninstall = palette_hooks = false;
    generic_tint_enabled = false; tint_calls = 0; tinted_brushes.clear();
    palette_installs = palette_uninstalls = row_binds = 0; native_selected = -1;
    subject_retires = owner_retires = row_originals_called = 0;
    widget_kinds.clear(); generic_subjects.clear(); generic_targets.clear(); row_handles.clear(); active_rows.clear();
    cursor_present = true; cursor_provider = 1; cursor_lookups = 0;
    next_subject = 0; subject_probes.clear(); widget_versions.clear(); subject_accesses = reenter_access_at = 0; nested_lookup = false;
    s.subjects.clear(); s.pending_rows.clear(); s.work_revision = 1;
    row_observer = nullptr; row_retired = nullptr; row_tickets.clear(); selection_observer = nullptr; row_frame = nullptr; selection_frame = nullptr;
    subject_names_ready = fail_subject_names = fail_speed_read = fail_speed_bind = false;
    subject_prepares = speed_reads = speed_binds = speed_bind_attempts = 0;
    speed_members.clear(); row_trees.clear(); runtime::state.speed_ready = runtime::state.speed_dirty = false; runtime::state.speed_epoch = 0;
    palette_reader = {}; palette_invocation = nullptr; color_sources.clear(); color_world.clear();
    s.palettes.clear(); s.rows = {}; s.semantic_selected = -1;
    s.palette_configured = s.palette_installed = false; s.palette_charged = s.palette_current = 0;
    displayed = nullptr; saved.clear(); messages.clear(); control = true;
    gate.open();
}
std::uintptr_t caller(runtime::Source source) {
    return reinterpret_cast<std::uintptr_t>(fake_module) + (source == runtime::Source::Ready
        ? game::rva::PianoMenuListOpenCall + 5 : source == runtime::Source::Close
        ? game::rva::PianoMenuListCancelCloseCall + 5 : game::rva::FNameCtor);
}
void finish_preparation(const core::HookCallbackGate::Lease&);
void prepare_fixture_candidates(const core::HookCallbackGate::Lease&);
// Legacy admission/transaction fixtures explicitly request all candidates before
// choosing. Readiness/demand scenarios pass false and use the real input queue.
void open(const core::HookCallbackGate::Lease& lease, bool finish = true) {
    runtime::callback_entered();
    const auto frame = caller(runtime::Source::Ready);
    runtime::ready(current.list, current.generation, &frame, lease);
    if (finish) { finish_preparation(lease); prepare_fixture_candidates(lease); }
}
void close(const core::HookCallbackGate::Lease& lease) {
    runtime::callback_entered();
    const auto frame = caller(runtime::Source::Close);
    runtime::close(current.list, &frame, lease);
    current.phase = game::MenuSessionPhase::Closing;
    ++original_calls; // Mandatory original remains outside optional restoration.
}
void sample(bool focus, bool held, bool previous = false, bool next = false) {
    const auto native_calls = creates + lookups + captures + composes + borrows + applies + tint_calls;
    polling = true; runtime::poll(focus, held, previous, next); polling = false;
    require(native_calls == creates + lookups + captures + composes + borrows + applies + tint_calls, "poller never calls native bridge");
}
void deliver(const core::HookCallbackGate::Lease& lease, std::uint64_t ticket = 0) {
    if (!ticket) ticket = queued_ticket;
    if (queued_ticket == ticket) queued_ticket = 0;
    runtime::callback_entered();
    DispatchFrame frame{ticket, current, lease, posted_purposes[ticket]}; dispatch_frame = &frame;
    runtime::posted_input(&frame, ticket, lease); dispatch_frame = nullptr;
}
void finish_preparation(const core::HookCallbackGate::Lease& lease) {
    std::size_t bound = 1000000; // Test failure bound, never production readiness or resource admission.
    while (runtime::state.owner && runtime::state.work_pending && !runtime::state.compromised.load()) {
        require(bound-- != 0, "bounded preparation must finish or explicitly reject");
        sample(true, runtime::state.held.load());
        deliver(lease);
    }
}
void prepare_fixture_candidates(const core::HookCallbackGate::Lease& lease) {
    if (!runtime::state.owner || runtime::state.compromised.load()) return;
    const auto desired = runtime::state.desired;
    for (std::size_t i = 1; i < runtime::state.themes.size() && !runtime::state.compromised.load(); ++i) {
        auto& theme = runtime::state.themes[i];
        if (!theme.backend || theme.rejected || theme.available) continue;
        runtime::state.desired = i; theme.preparing = true; theme.work.revision = 0;
        runtime::update_work_pending();
        std::size_t bound = 1000000;
        while (theme.preparing && !runtime::state.compromised.load()) {
            require(bound-- != 0, "explicit fixture candidate request finishes or rejects");
            const auto ticket = ++runtime::state.sequence;
            DispatchFrame frame{ticket, current, lease, true}; dispatch_frame = &frame;
            std::vector<runtime::Event> diagnostics;
            {
                std::unique_lock lock(runtime::state.mutex);
                runtime::Scope scope(runtime::Source::Preparation, &frame, lease, current, ticket);
                runtime::Event event;
                require(scope.qualify(current), "fixture preparation retains the qualified dispatch frame");
                (void)runtime::prepare_quantum(scope, event, &diagnostics, i);
            }
            dispatch_frame = nullptr;
            for (const auto& event : diagnostics) runtime::report(event);
        }
    }
    runtime::state.desired = desired; runtime::update_work_pending();
}
void key(const core::HookCallbackGate::Lease& lease) {
    sample(true, false); sample(true, control); deliver(lease);
}
// Faithful fake ledger: the original brush charge is shared with the actual
// capture_original reserve code; prepared pixels and records use its same caps.
bool charge(std::uint64_t bytes) {
    if (retained_bytes > native::kRetainedByteLimit
        || bytes > native::kRetainedByteLimit - retained_bytes) return false;
    ++retained_images; retained_bytes += bytes; return true;
}
bool admitted(const native::CallbackAuthority& a) {
    return !native_poisoned && a.qualify_current_callback && a.qualify_current_callback(a.context, a.session);
}
native::Result latch_batch_result(native::Result result) {
    if (result.outcome == native::Outcome::NativeFault) native_poisoned = true;
    return result;
}
native::Result result(bool ok, const char* stage) {
    return {ok ? native::Outcome::Ready : native::Outcome::Authority, stage};
}
bool valid_brush(const native::OwnedImage& image) {
    const auto it = brushes.find(brush_token(image));
    if (it == brushes.end()) return false;
    if (it->second.kind == Kind::Prepared) return true;
    if (it->second.generation != current.generation || !originals.contains(it->second.leaf)) return false;
    if (it->second.subject) {
        native::Subject subject{};
        std::memcpy(&subject, &it->second.subject, sizeof(it->second.subject));
        // Retained storage alone is not a live member/provider affinity.
        return native::validate_subject(subject, current);
    }
    return true;
}
} // namespace

namespace ff7r::piano::core {
ImageRange image_range(HMODULE module) { return {reinterpret_cast<const std::uint8_t*>(module), 0x20000000}; }
// Separately compiled production helper cannot use this TU's fake OS reader.
// Preserve the original mapping predicate, including each caller's execute-only policy.
bool is_executable_image_address(HMODULE module, std::uintptr_t address, bool allow_execute_only) noexcept {
    ++executable_mapping_calls;mapping_allow_execute_only=allow_execute_only;
    const auto image=image_range(module);const auto base=reinterpret_cast<std::uintptr_t>(image.base);
    if(!module || address<base || address-base>=image.size)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!mapped_query(reinterpret_cast<void*>(address),&info,sizeof(info)) || info.AllocationBase!=module
        || info.Type!=MEM_IMAGE || info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto p=info.Protect&0xff;
    return (allow_execute_only && p==PAGE_EXECUTE) || p==PAGE_EXECUTE_READ
        || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}
bool safe_copy_bytes(const void* from, void* to, size_t n) {
    if (!from || !to) return false;
    if(from==observed_return_slot)++observed_return_reads;
    if (from == static_cast<const std::uint8_t*>(current.widget) + game::runtime_layouts::PianoMusicList::selected_index) {
        require(n == sizeof(native_selected), "exact selected scalar read"); std::memcpy(to, &native_selected, n); return true;
    }
    std::memcpy(to, from, n); return true;
}
bool safe_write_bytes(void* to, const void* from, size_t n) { return safe_copy_bytes(from, to, n); }
bool bytes_equal(const std::uint8_t* p, const std::vector<std::uint8_t>& v) { return p && !std::memcmp(p, v.data(), v.size()); }
// Runtime uses the explicit fake fact-acquisition seam. If an accidentally linked
// production acquisition path is ever executed, fail instead of granting facts.
bool is_supported_exe(HMODULE, PeIdentity*) { std::abort(); }
void log(LogLevel, const std::string& text) {
    require(!runtime::active_scope, "format/log after mutation scope"); messages.push_back(text);
}
bool save_selected_theme(const std::wstring&, const std::string& id) {
    const bool unlocked = runtime::state.mutex.try_lock();
    require(!runtime::active_scope && unlocked, "save outside runtime/bridge authority");
    runtime::state.mutex.unlock();
    ++saves; if (fail_save) return false; saved = id; return true;
}
bool valid_theme_id(std::string_view id) noexcept { return !id.empty() && id.size() <= 64; }
bool load_theme(const std::filesystem::path&, std::string_view id, ThemeDefinition& out, std::string& error) {
    require(!runtime::active_scope, "source preparation outside callbacks");
    ++loads;
    if (reject_loads && loads % 2) { error = "invalid_manifest"; return false; }
    if (id == "invalid") { error = "invalid_manifest"; return false; }
    out = definition(std::string(id).c_str());
    if (id == "preview-only") { out.images.clear(); out.colors["Preview/Accent"] = {20, 40, 80, 128}; }
    if (id == "unsupported" || reject_loads) out.colors["SpeedIcon/SpeedIcon.ColorAndOpacity"] = {};
    return true;
}
}
namespace ff7r::piano::game {
const RvaSignatureSpec* find_rva_signature(std::string_view) { std::abort(); }
bool capture_current_uobject_identity_access(uobject_locator_core::UObjectIdentityAccess&) noexcept { std::abort(); }
core::HookCallbackGate& non_audio_hook_gate() { return gate; }
MenuSessionSnapshot capture_ready_menu_session() noexcept {
    return current.phase == MenuSessionPhase::Ready ? current : MenuSessionSnapshot{};
}
bool same_menu_session(const MenuSessionSnapshot& a, const MenuSessionSnapshot& b, bool phase) noexcept {
    return a.generation == b.generation && (!phase || a.phase == b.phase) && a.list == b.list
        && a.controller == b.controller && a.widget == b.widget && a.list_ownership == b.list_ownership
        && a.widget_identity.internal_index == b.widget_identity.internal_index
        && a.widget_identity.serial_number == b.widget_identity.serial_number;
}
MenuSessionSnapshot capture_menu_callback_session() noexcept { return current; }
bool capture_live_uobject_handle(void* row, UObjectLiveHandle& out) {
    if (row && row == current.widget) { out = current.widget_identity; return out.internal_index >= 0 && out.serial_number > 0; }
    const auto it = row_handles.find(row); if (it == row_handles.end()) return false; out = it->second; return true;
}
namespace menu_theme_observation {
Snapshot capture_native_facts(HMODULE, std::string_view, std::uint64_t generation) noexcept {
    ++native_fact_captures;
    Snapshot s; s.authenticated = true; s.complete = !incomplete; s.session = capture_ready_menu_session();
    s.expected_generation = generation; s.thread_initialized = 1;
    s.current_thread = s.native_thread = GetCurrentThreadId(); s.tls_vector = 1; s.tls_block = 2;
    s.tls_role = wrong_tls ? 1 : 2; s.garbage_collecting = gc; s.exit_purge = purge;
    s.package.object = 10; s.package.fields = s.package.membership = true; s.package.item_flags = 1 << 30;
    s.package_class.object = 11; s.package_class.fields = s.package_class.membership = true;
    return s;
}
}
namespace menu_theme_runtime {
void bind_owned_row_setup(RowSetupObserver observer, RowSetupRetired retired) noexcept { row_observer = observer; row_retired = retired; }
void bind_owned_selection(SelectionObserver observer) noexcept { selection_observer = observer; }
bool qualify_owned_row_setup(const void* frame, const MenuSessionSnapshot& session, void* row, std::int32_t index,
    std::uint64_t setup, bool after) noexcept {
    return frame && frame == row_frame && !row_frame->retired && (!row_frame->refresh || row_frame->target_current)
        && row_frame->row == row && row_frame->index == index
        && row_frame->setup == setup && row_frame->after == after && same_menu_session(session, current, false);
}
bool qualify_owned_selection(const void* frame, const MenuSessionSnapshot& session, std::int32_t index, bool after) noexcept {
    return frame && frame == selection_frame && selection_frame->index == index && selection_frame->after == after && same_menu_session(session, current);
}
void bind_owned_input(void (*entry)() noexcept,
    int (*)() noexcept, void (*)(bool, bool, bool, bool) noexcept, void (*)() noexcept,
    void (*input)(const void*, std::uint64_t, const core::HookCallbackGate::Lease&) noexcept) noexcept {
    bound_entry = entry; bound_input = input;
}
bool post_input(std::uint64_t ticket, bool preparation) noexcept {
    require(!runtime::active_scope, "only metadata handoff posts a value ticket outside native authority");
    if (fail_post) return false;
    require(runtime::state.pending.ticket == ticket, "one current owner-bound ticket; prior canceled messages have no authority");
    posted_purposes[ticket] = preparation; queued_ticket = ticket; return true;
}
bool preparation_dispatch_active() noexcept { return dispatch_frame != nullptr; }
void wake_preparation_after_ready() noexcept {
    require(!runtime::active_scope && runtime::state.mutex.try_lock(),"Ready wakes only after Scope and runtime lock retire");
    runtime::state.mutex.unlock();++ready_wake_calls;
    if(!ready_wake_enabled)return;
    // Title tests enforce one-ticket metadata delivery without a successor chain. This seam
    // exposes the metadata ticket; only deliver() supplies actual native authority.
    const auto ticket=reserve_preparation_continuation();
    if(ticket && !post_preparation_continuation(ticket))cancel_preparation_continuation(ticket);
}
bool qualify_owned_dispatch(const void* frame, std::uint64_t ticket, const MenuSessionSnapshot& owner, bool preparation) noexcept {
    return dispatch_frame && frame == dispatch_frame && dispatch_frame->ticket == ticket
        && dispatch_frame->preparation == preparation && dispatch_frame->lease && same_menu_session(owner, dispatch_frame->owner);
}
}
namespace menu_theme_native {
bool preparing_scope() {
    const auto* scope = runtime::active_scope;
    return !polling && scope && (scope->source == runtime::Source::Ready || scope->source == runtime::Source::Preparation
        || scope->source == runtime::Source::PostedInput
        || (scope->source == runtime::Source::RowSetup && scope->after && runtime::state.row_rebind.load() == scope->frame));
}
Result configure(HMODULE, std::string_view) noexcept {
    return native_poisoned ? Result{Outcome::Unavailable, "fake_backend_poisoned"} : result(true, "fake_authenticated");
}
void disable() noexcept {}
Result find_image(std::wstring_view leaf, const CallbackAuthority& a, ImageTarget& target) noexcept {
    require(!polling, "no native lookup on poller");
    require(!leaf.empty() && preparing_scope(), "lookup only actual cooperative preparation stack, never input/poller/selection");
    ++lookups;
    if (!admitted(a)) return result(false, "fake_lookup_admission");
    const std::string name(leaf.begin(), leaf.end());
    if (name == missing_leaf || fail_native) return {Outcome::Identity, "fake_missing_image"};
    targets[&target] = name;
    if (!originals.contains(name)) originals[name] = 100 + static_cast<int>(originals.size());
    if (!visuals.contains(name)) visuals[name] = originals[name];
    return result(true, "fake_lookup");
}
Result capture_original(const ImageTarget& target, const CallbackAuthority& a, OwnedImage& image) noexcept {
    if(!in_member_resolver && !in_capture_batch)++unbatched_work_calls;
    if (!in_capture_batch) ++capture_api_calls;
    require(preparing_scope(), "original capture only in qualified cooperative preparation");
    const auto& leaf = targets.at(&target);
    require(visuals.at(leaf) == originals.at(leaf), "never recapture an already-mutated brush"); ++captures;
    if (slow_capture) work_clock += 100;
    if (!admitted(a) || fail_native) return result(false, "fake_capture");
    if (!charge(kOriginalCaptureByteCharge)) return {Outcome::Budget, "original_budget"};
    publish_brush(image, {Kind::Original, leaf, originals.at(leaf), originals.at(leaf), current.generation});
    return result(true, "fake_capture");
}
Result create(const core::ThemeImage& pixels, const CallbackAuthority& a, OwnedImage& image) noexcept {
    require(!polling, "no native creation on poller");
    require(preparing_scope()
        && pixels.width && pixels.height && pixels.width <= 4096 && pixels.height <= 4096
        && pixels.bgra.size() == std::uint64_t(pixels.width) * pixels.height * 4,
        "native creation only owned preparation message, consumes prepared pixels");
    if (!admitted(a)) return result(false, "fake_create_admission");
    if (!charge(pixels.bgra.size())) return {Outcome::Budget, "reserve_lifetime_budget"};
    ++creates;
    publish_brush(image, {Kind::Prepared, {}, pixels.bgra.front()});
    if (nested_create) { nested_create = false; ::close(*current_lease); }
    return result(admitted(a) && !fail_native, "fake_create_recheck");
}
Result compose(const OwnedImage& original, const OwnedImage& prepared, const CallbackAuthority& a, OwnedImage& image) noexcept {
    if(!in_member_resolver)++unbatched_work_calls;
    require(preparing_scope(), "composition only qualified cooperative preparation");
    if (!admitted(a) || !valid_brush(original) || !valid_brush(prepared)) return result(false, "fake_compose_admission");
    const auto shape = brushes.at(brush_token(original)), pixels = brushes.at(brush_token(prepared));
    require(shape.kind == Kind::Original && pixels.kind == Kind::Prepared, "actual compose contract consumes original plus immutable pixels");
    ++composes;
    if (!charge(kOriginalCaptureByteCharge)) return {Outcome::Budget, "compose_budget"};
    if (shape.leaf == fail_compose_leaf) return {Outcome::NativeFault, "fake_compose_failure"};
    publish_brush(image, {Kind::Composed, shape.leaf, pixels.value, shape.geometry, current.generation, shape.subject});
    return result(admitted(a), "fake_composed");
}
Result borrow_brush(const OwnedImage& image, const CallbackAuthority& a, const void*& brush) noexcept {
    require(!polling, "no native preflight on poller"); ++borrows; brush = nullptr;
    if (!admitted(a) || !valid_brush(image)) return result(false, "fake_borrow_admission");
    if (fail_native || brushes.at(brush_token(image)).leaf == fail_borrow_leaf) return {Outcome::Identity, "fake_borrow_failure"};
    brush = &image; return result(true, "fake_borrowed");
}
Result tint(const OwnedImage& source, const std::array<float, 4>& rgba, const CallbackAuthority& a, OwnedImage& image) noexcept {
    if(!in_member_resolver)++unbatched_work_calls;
    require(preparing_scope(),
        "tint preparation uses only owned preparation messages");
    if (!admitted(a) || !valid_brush(source) || !generic_tint_enabled) return result(false, "fake_tint_admission");
    require(brushes.at(brush_token(source)).kind != Kind::Prepared, "tint needs target/member affinity");
    ++tint_calls;
    if (!charge(kOriginalCaptureByteCharge)) return {Outcome::Budget, "tint_budget"};
    publish_brush(image, brushes.at(brush_token(source))); tinted_brushes[&image] = rgba;
    return result(admitted(a), "fake_tint");
}
Result apply(const ImageTarget& target, const OwnedImage& image, const CallbackAuthority& a) noexcept {
    require(!polling, "no apply on poller");
    if (!admitted(a) || !valid_brush(image)) return result(false, "fake_apply_admission");
    const auto value = brushes.at(brush_token(image)); const auto& leaf = targets.at(&target);
    require(value.kind != Kind::Prepared && value.leaf == leaf && value.geometry == originals.at(leaf),
        "assignment is target-affine composition preserving native geometry, never raw prepared brush");
    ++applies;
    if (apply_has_failed && fail_rollback) return {Outcome::NativeFault, "fake_rollback_failure"};
    visuals[leaf] = value.value;
    displayed = value.kind == Kind::Original ? nullptr : &image;
    if (applies == fail_apply_at) { apply_has_failed = true; return {Outcome::NativeFault, "fake_fault_after_assignment"}; }
    if (nested_selection) {
        nested_selection = false; SelectionFrame frame{17}; selection_frame = &frame;
        runtime::observe_selection(&frame, current.widget, 17, false, *current_lease);
        ++selection_originals;
        runtime::observe_selection(&frame, current.widget, 17, true, *current_lease); selection_frame = nullptr;
    }
    if (nested_apply || applies == nested_apply_at) { nested_apply = false; ::close(*current_lease); }
    return result(admitted(a), "fake_apply_recheck");
}
}
}

namespace ff7r::piano::game::menu_theme_native {
Capabilities capabilities() noexcept { return {generic_enabled, generic_enabled, generic_tint_enabled}; }
Result prepare_subjects(const CallbackAuthority& a) noexcept {
    require(preparing_scope(), "name witnesses prepared only on qualified cooperative preparation");
    ++subject_prepares; subject_names_ready = admitted(a) && !fail_subject_names;
    return result(subject_names_ready, "fake_name_witnesses");
}
Result read_speed_rows(const CallbackAuthority& a, SpeedRowsSnapshot& out) noexcept {
    require(!polling, "no speed membership reads on poller"); ++speed_reads; out = {};
    if (!admitted(a) || !subject_names_ready || fail_speed_read || speed_members.size() > out.rows.size()) return {Outcome::Identity, "fake_speed_membership"};
    for (auto* row : speed_members) if (!active_rows[row] || !row_handles.contains(row)) return {Outcome::Identity, "fake_speed_positive_member"};
    out.count = speed_members.size(); std::copy(speed_members.begin(), speed_members.end(), out.rows.begin());
    return result(true, "fake_speed_members");
}
Result bind_speed_row(void* row, std::uint64_t epoch, const CallbackAuthority& a, RowTicket& out) noexcept {
    ++speed_bind_attempts;
    if (fail_speed_bind) return {Outcome::Input, "fake_speed_bind_rejected"};
    require(preparing_scope(), "speed binding requires qualified sliced preparation, never the poller");
    if (!admitted(a) || !subject_names_ready || !epoch || !active_rows[row] || !row_handles.contains(row)
        || std::find(speed_members.begin(), speed_members.end(), row) == speed_members.end()) return {Outcome::Identity, "fake_speed_bind"};
    row_tickets[&out] = {row, -1, 0, current.generation, epoch}; ++speed_binds;
    return result(true, "fake_uniform_speed_bound");
}
Result bind_row(RowKind kind, void* row, std::int32_t index, std::uint64_t setup, const CallbackAuthority& a, RowTicket& out) noexcept {
    require(preparing_scope() && kind == RowKind::Song,
        "prepare genuine song association only in bounded owned message, no manufactured speed index");
    if (!admitted(a) || !active_rows.contains(row) || !active_rows.at(row) || !row_handles.contains(row) || index < 0 || !setup)
        return {Outcome::Identity, "fake_active_row"};
    row_tickets[&out] = {row, index, setup, current.generation};
    ++row_binds; return result(true, "fake_row_bound");
}
Result find_fixture_subject(std::wstring_view leaf, SubjectKind kind, const RowTicket* ticket, const CallbackAuthority& a, Subject& out, bool cursor) noexcept {
    ++lookups;
    ++subject_probes[{std::wstring(leaf), kind}];
    require(preparing_scope(),
        "typed lookup only in owned preparation messages");
    const auto it = widget_kinds.find(std::wstring(leaf));
    if (!generic_enabled || !subject_names_ready || !admitted(a) || (cursor && !cursor_present) || it == widget_kinds.end() || it->second != kind)
        return {Outcome::Identity, "fake_subject_class"};
    const auto ordinal = static_cast<std::uintptr_t>(std::distance(widget_kinds.begin(), it));
    const auto row = row_tickets.find(ticket);
    if (row != row_tickets.end() && (!active_rows.contains(row->second.row) || !active_rows.at(row->second.row)
        || row->second.generation != current.generation)) return {Outcome::Identity, "fake_inactive_row"};
    const auto offset = cursor ? 0x10000000 + cursor_provider * 0x10000
        : row == row_tickets.end() ? 0 : reinterpret_cast<std::uintptr_t>(row->second.row) * 0x10000;
    const auto token = ++next_subject; std::memcpy(&out, &token, sizeof(token));
    generic_subjects[token] = {std::wstring(leaf), kind, reinterpret_cast<void*>(0x71000000 + ordinal * 0x100 + offset
        + widget_versions[std::wstring(leaf)] * 0x1000), current.generation,
        row == row_tickets.end() ? nullptr : row->second.row, row == row_tickets.end() ? 0 : row->second.setup};
    if (row != row_tickets.end()) {
        auto& s = generic_subjects[token]; s.epoch = row->second.epoch; s.tree = row_trees[s.row]; s.serial = row_handles[s.row].serial_number;
        s.widget = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(s.widget) + s.tree * 0x1000);
    }
    generic_subjects[token].version = widget_versions[std::wstring(leaf)];
    generic_subjects[token].cursor = cursor; generic_subjects[token].cursor_provider = cursor_provider;
    if (nested_lookup) { nested_lookup = false; runtime::callback_entered(); }
    return result(true, "fake_subject_found");
}
Result find_subject(std::wstring_view leaf, SubjectKind kind, const RowTicket& ticket, const CallbackAuthority& a, Subject& out) noexcept {
    return find_fixture_subject(leaf, kind, &ticket, a, out, false);
}
Result find_cursor_subject(std::wstring_view leaf, const CallbackAuthority& a, Subject& out, SubjectKind kind) noexcept {
    ++cursor_lookups;
    return find_fixture_subject(leaf, kind, nullptr, a, out, true);
}
bool validate_subject(const Subject& subject, const MenuSessionSnapshot& session) noexcept {
    require(!matching_retirement, "retirement predicates perform metadata-only matching, not native validation");
    ++subject_validations;
    const auto it = generic_subjects.find(subject_token(subject));
    if (it == generic_subjects.end() || it->second.generation != session.generation || !same_menu_session(session, current)
        || !widget_kinds.contains(it->second.leaf) || widget_kinds.at(it->second.leaf) != it->second.kind
        || widget_versions[it->second.leaf] != it->second.version) return false;
    if (it->second.cursor && (!cursor_present || cursor_provider != it->second.cursor_provider)) return false;
    if (!it->second.row) return true;
    return active_rows.contains(it->second.row) && active_rows.at(it->second.row)
        && row_handles[it->second.row].serial_number == it->second.serial && row_trees[it->second.row] == it->second.tree
        && std::any_of(row_tickets.begin(), row_tickets.end(), [&](const auto& entry) {
            return entry.second.row == it->second.row && entry.second.setup == it->second.setup && entry.second.epoch == it->second.epoch
                && entry.second.generation == session.generation;
        });
}
bool subject_row_association(const Subject& subject, RowAssociation& out) noexcept {
    const auto it = generic_subjects.find(subject_token(subject));
    if (it == generic_subjects.end() || !it->second.row) return false;
    const auto& s = it->second;
    for (const auto& [ticket, association] : row_tickets) {
        (void)ticket;
        if (association.row != s.row || association.setup != s.setup || association.epoch != s.epoch || association.generation != s.generation) continue;
        out.kind = s.epoch ? RowKind::Speed : RowKind::Song;
        out.logical_index = association.index; out.session_generation = association.generation;
        out.setup_generation = association.setup; out.membership_epoch = association.epoch;
        return true;
    }
    return false;
}
Result access_subject(const Subject& subject, const CallbackAuthority& a, void*& widget) noexcept {
    ++subject_accesses;
    widget = nullptr;
    if (!admitted(a) || !validate_subject(subject, a.session)) return {Outcome::Identity, "fake_access_subject"};
    widget = generic_subjects.at(subject_token(subject)).widget;
    if (reenter_access_at == subject_accesses) runtime::callback_entered();
    return result(admitted(a), "fake_accessed");
}
Result capture_original(const Subject& subject, BrushSelection slot, const CallbackAuthority& a, OwnedImage& image) noexcept {
    if (!admitted(a) || !validate_subject(subject, a.session)) return {Outcome::Identity, "fake_capture_subject"};
    const auto& s = generic_subjects.at(subject_token(subject));
    std::string leaf = subject_leaf(s);
    if (s.kind == SubjectKind::Button) leaf += "#" + std::to_string(static_cast<unsigned>(slot.button));
    if (s.kind == SubjectKind::ImageSet) { if (slot.index < 0 || slot.index >= 3) return {Outcome::Input, "fake_indexed_slot"}; leaf += "#" + std::to_string(slot.index); }
    auto& target = generic_targets[subject_token(subject)]; targets[&target] = leaf;
    if (!originals.contains(leaf)) originals[leaf] = 100 + static_cast<int>(originals.size());
    if (!visuals.contains(leaf)) visuals[leaf] = originals[leaf];
    const auto captured = capture_original(target, a, image);
    if (captured) brushes.at(brush_token(image)).subject = subject_token(subject);
    if(captured && replace_label_after_capture){replace_label_after_capture=false;++widget_versions[L"Label"];}
    if(captured && replace_cursor_after_capture){replace_cursor_after_capture=false;++cursor_provider;}
    return captured;
}
Result capture_original_members(const Subject& subject, std::span<const BrushSelection> members,
    const CallbackAuthority& a, std::span<OwnedImage> outputs, CaptureProgress& progress) noexcept {
    if(!in_member_resolver)++unbatched_work_calls;
    ++capture_batches; ++capture_api_calls; progress = {};
    for (auto& output : outputs) output = {};
    const auto finish = [&](Result value) {
        last_capture_progress = progress; in_capture_batch = false; return value;
    };
    require(preparing_scope(), "member originals only under real cooperative or post-original refresh authority");
    if (native_poisoned) return finish({Outcome::Unavailable, "fake_backend_poisoned"});
    if (!admitted(a) || !validate_subject(subject, a.session)) return finish({Outcome::Authority, "fake_capture_batch_admission"});
    const auto s = generic_subjects.at(subject_token(subject));
    if (members.empty() || outputs.size() != members.size()
        || (s.kind != SubjectKind::Button && s.kind != SubjectKind::ImageSet)) return finish({Outcome::Input, "fake_capture_batch_input"});
    (void)generic_targets[subject_token(subject)]; // Warm immutable hits still have a fresh current target descriptor.
    std::vector<std::string> leaves;
    for (std::size_t n = 0; n < members.size(); ++n) {
        const auto slot = members[n];
        if (s.kind == SubjectKind::ImageSet && (slot.index < 0 || slot.index >= 3)) return finish({Outcome::Input, "fake_indexed_slot"});
        if (s.kind == SubjectKind::Button && (slot.index != 0 || static_cast<unsigned>(slot.button) > 3)) return finish({Outcome::Input, "fake_button_slot"});
        for (std::size_t j = 0; j < n; ++j) if (members[j].index == slot.index && members[j].button == slot.button)
            return finish({Outcome::Input, "fake_duplicate_capture_slot"});
        std::string leaf = subject_leaf(s);
        leaf += "#" + std::to_string(s.kind == SubjectKind::Button ? static_cast<unsigned>(slot.button) : slot.index);
        if (!originals.contains(leaf)) originals[leaf] = 100 + static_cast<int>(originals.size());
        if (!visuals.contains(leaf)) visuals[leaf] = originals[leaf];
        require(visuals.at(leaf) == originals.at(leaf), "batch must never capture a themed original");
        leaves.push_back(std::move(leaf));
    }
    std::vector<OwnedImage> staged(members.size());
    in_capture_batch = true;
    for (std::size_t n = 0; n < members.size(); ++n) {
        if (retire_capture_member == n + 1) runtime::callback_entered();
        if (!admitted(a) || !validate_subject(subject, a.session)) return finish({Outcome::Authority, "fake_capture_batch_retired"});
        if (fail_capture_member == n + 1) {
            if (fail_capture_fault) { native_poisoned = true; return finish({Outcome::NativeFault, "fake_capture_batch_fault"}); }
            return finish({Outcome::Budget, "fake_capture_batch_budget"});
        }
        const auto key = leaves[n] + ":" + std::to_string(originals.at(leaves[n]));
        if (const auto cached = original_cache.find(key); cached != original_cache.end()) {
            auto brush = brushes.at(brush_token(cached->second)); brush.generation = current.generation; brush.subject = subject_token(subject);
            publish_brush(staged[n], brush); ++captures; // fresh affinity, no new immutable holder
        } else {
            const auto captured = capture_original(subject, members[n], a, staged[n]);
            if (!captured) return finish(captured);
            original_cache[key] = staged[n]; ++progress.created_members;
        }
        ++progress.prepared_members;
    }
    if (!admitted(a) || !validate_subject(subject, a.session)) return finish({Outcome::Authority, "fake_capture_batch_publish_retired"});
    std::copy(staged.begin(), staged.end(), outputs.begin()); progress.published_members = members.size();
    return finish({Outcome::Ready, "fake_capture_batch"});
}
Result resolve_members(std::span<const ResolveMember> requests, const CallbackAuthority& a,
    std::span<ResolvedMember> outputs, BatchProgress& progress) noexcept {
    ++resolve_batches;resolve_requests+=static_cast<unsigned>(requests.size());
    resolve_max_members=std::max(resolve_max_members,static_cast<unsigned>(requests.size()));progress={};
    if(requests.empty() || requests.size()>kSubjectTraversalLimit || requests.size()!=outputs.size())return {Outcome::Input,"fake_resolver_shape"};
    if(!admitted(a))return {Outcome::Authority,"fake_resolver_admission"};
    struct ResolverScope {
        bool prior=in_member_resolver;
        ResolverScope(){in_member_resolver=true;}
        ~ResolverScope(){in_member_resolver=prior;}
    } resolver_scope;
    require(std::all_of(requests.begin(),requests.end(),[&](const auto& r){return r.subject==requests.front().subject;}),
        "owning resolver quantum stays within one current Subject, never an arbitrary whole row/program");
    std::vector<ResolvedMember> staged(requests.size());
    for(std::size_t i=0;i<requests.size();++i) {
        const auto& request=requests[i];
        if(!request.subject || !validate_subject(*request.subject,a.session)) {
            progress.failed_index=i;return {Outcome::Identity,"fake_resolver_subject"};
        }
        require(&outputs[i].original!=request.original && &outputs[i].image!=request.original
            && &outputs[i].original!=request.prepared && &outputs[i].image!=request.prepared,"resolver never aliases owning inputs");
        if((request.original && !valid_brush(*request.original)) || (request.prepared && !valid_brush(*request.prepared))) {
            progress.failed_index=i;return {Outcome::Identity,"fake_resolver_image"};
        }
        if(request.original)require(brushes.at(brush_token(*request.original)).kind==Kind::Original,"resolver original is a genuine baseline, not an already composed/tinted input");
        if(request.prepared)require(brushes.at(brush_token(*request.prepared)).kind==Kind::Prepared,"resolver texture was uploaded explicitly before materialization");
    }
    const auto& first=requests.front();
    const auto kind=generic_subjects.at(subject_token(*first.subject)).kind;
    const bool group_capture=(kind==SubjectKind::Button || kind==SubjectKind::ImageSet)
        && std::all_of(requests.begin(),requests.end(),[&](const auto& r){return r.subject==first.subject && !r.original && !r.prepared && !r.tint;});
    if(group_capture) {
        std::vector<BrushSelection> members;std::vector<OwnedImage> originals(requests.size());
        for(const auto& r:requests)members.push_back(r.member);
        CaptureProgress captured{};
        const auto result=capture_original_members(*first.subject,members,a,originals,captured);
        progress.completed=captured.prepared_members;progress.created=captured.created_members;
        if(!result){progress.failed_index=progress.completed;return result;}
        for(std::size_t i=0;i<requests.size();++i)staged[i]={originals[i],originals[i]};
    } else for(std::size_t i=0;i<requests.size();++i) {
        const auto& r=requests[i];Result result{Outcome::Ready,"fake_resolver_retained"};
        auto& out=staged[i];
        if(r.original)out.original=*r.original;
        else {result=capture_original(*r.subject,r.member,a,out.original);if(result)++progress.created;}
        if(result)out.image=out.original;
        if(result && r.prepared){
            const auto key=std::pair<std::size_t,std::size_t>{brush_token(out.original),brush_token(*r.prepared)};
            if(const auto found=resolved_compositions.find(key);found!=resolved_compositions.end()){
                out.image=found->second;++resolve_compose_reuses;
            }else{
                result=compose(out.original,*r.prepared,a,out.image);
                if(result){resolved_compositions[key]=out.image;++progress.created;}
            }
        }
        if(result && r.tint){OwnedImage tinted;result=tint(out.image,r.linear_rgba,a,tinted);if(result){out.image=tinted;++progress.created;}}
        if(!result){progress.failed_index=i;return result;}
        if(!admitted(a)){progress.failed_index=i;return {Outcome::Authority,"fake_resolver_retired"};}
        ++progress.completed;
    }
    if(!admitted(a))return {Outcome::Authority,"fake_resolver_publish_retired"};
    for(std::size_t i=0;i<requests.size();++i)if(!validate_subject(*requests[i].subject,a.session)){
        progress.failed_index=i;return {Outcome::Identity,"fake_resolver_publish_subject"};
    }
    std::copy(staged.begin(),staged.end(),outputs.begin());return {Outcome::Ready,"fake_resolved_members"};
}
Result preflight_subjects(std::span<const SubjectPreflight> requests,const CallbackAuthority& a,BatchProgress& progress) noexcept {
    ++preflight_batches;preflight_requests+=static_cast<unsigned>(requests.size());progress={};
    require(requests.size()<=kSubjectTraversalLimit,"read-only preflight uses backend traversal bound");
    std::size_t image_count=0;for(const auto& r:requests)image_count+=r.images.size();
    require(image_count<=kSubjectTraversalLimit,"preflight image affinities use backend traversal bound");
    for(std::size_t i=0;i<requests.size();++i) {
        const auto& r=requests[i];preflight_max_images=std::max(preflight_max_images,static_cast<unsigned>(r.images.size()));
        if(!r.subject || !admitted(a) || !validate_subject(*r.subject,a.session)) {
            progress.failed_index=i;return {Outcome::Identity,"fake_preflight_subject"};
        }
        const auto& subject=generic_subjects.at(subject_token(*r.subject));
        for(const auto& assignment:r.images) {
            if(!assignment.image){progress.failed_index=i;return {Outcome::Input,"fake_preflight_null_image"};}
            const void* borrowed=nullptr;const auto result=borrow_brush(*assignment.image,a,borrowed);
            if(!result){progress.failed_index=i;return result;}
            const auto& brush=brushes.at(brush_token(*assignment.image));
            auto leaf=subject_leaf(subject);
            if(subject.kind==SubjectKind::Button)leaf+="#"+std::to_string(static_cast<unsigned>(assignment.member.button));
            if(subject.kind==SubjectKind::ImageSet)leaf+="#"+std::to_string(assignment.member.index);
            if(brush.subject!=subject_token(*r.subject) || brush.leaf!=leaf) {
                progress.failed_index=i;return {Outcome::Identity,"fake_preflight_member_affinity"};
            }
        }
        ++progress.completed;
    }
    return result(admitted(a),"fake_subjects_preflighted");
}
Result apply(const Subject& subject, BrushSelection slot, const OwnedImage& image, const CallbackAuthority& a) noexcept {
    if (!admitted(a) || !validate_subject(subject, a.session)) return {Outcome::Identity, "fake_assign_subject"};
    const auto& s = generic_subjects.at(subject_token(subject)); std::string leaf = subject_leaf(s);
    if (s.kind == SubjectKind::Button) leaf += "#" + std::to_string(static_cast<unsigned>(slot.button));
    if (s.kind == SubjectKind::ImageSet) leaf += "#" + std::to_string(slot.index);
    require(brushes.at(brush_token(image)).leaf == leaf, "generic assignment retains exact class slot affinity");
    require(brushes.at(brush_token(image)).subject == subject_token(subject), "generic assignment requires fresh current-subject affinity, not just immutable storage reuse");
    // A batch uses one Subject with several explicit members; the Subject's last
    // captured member is not the member selected by this assignment.
    auto& target = generic_targets.at(subject_token(subject)); targets[&target] = leaf;
    return apply(target, image, a);
}
Result apply_members(const Subject& subject, std::span<const BrushAssignment> members,
    const CallbackAuthority& a, ApplyProgress& progress) noexcept {
    progress = {}; ++batch_calls;
    if (native_poisoned) return {Outcome::Unavailable, "fake_backend_poisoned"};
    if (!admitted(a) || !validate_subject(subject, a.session) || members.empty()) return {Outcome::Authority, "fake_batch_admission"};
    const auto kind = generic_subjects.at(subject_token(subject)).kind;
    require(kind == SubjectKind::Button || kind == SubjectKind::ImageSet, "only owning Button/ImageSet operations batch");
    for (std::size_t i = 0; i < members.size(); ++i) {
        require(members[i].image && valid_brush(*members[i].image), "every batch retains a valid cached brush");
        for (std::size_t j = 0; j < i; ++j) require(kind == SubjectKind::Button
            ? members[i].member.button != members[j].member.button : members[i].member.index != members[j].member.index,
            "batch members must be distinct");
    }
    if (kind == SubjectKind::Button) { ++button_publications; progress.attempted_members = members.size(); }
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (kind == SubjectKind::ImageSet) ++progress.attempted_members;
        const auto result = apply(subject, members[i].member, *members[i].image, a);
        if (!result) return latch_batch_result(result);
        if (kind == SubjectKind::ImageSet && fail_batch_member == i + 1) {
            fail_batch_member = 0; return latch_batch_result({Outcome::NativeFault, "fake_partial_image_member"});
        }
        if (kind == SubjectKind::ImageSet) ++progress.completed_members;
    }
    if (kind == SubjectKind::Button) progress.completed_members = members.size();
    else ++image_synchronizes;
    if (fail_batch_refresh) { fail_batch_refresh = false; return latch_batch_result({Outcome::NativeFault, "fake_batch_refresh"}); }
    if (!admitted(a)) return {Outcome::Authority, "fake_batch_retired"};
    progress.refresh_complete = true;
    return {Outcome::Ready, "fake_members_applied"};
}
}
namespace ff7r::piano::game::menu_theme_palette {
Result configure(HMODULE, std::string_view, CallbackReader reader) noexcept { palette_reader = reader; return {Outcome::Ready, "fake_palette_authenticated"}; }
Result install() noexcept { ++palette_installs; palette_hooks = !fail_palette_install; return {palette_hooks ? Outcome::Ready : Outcome::Signature, "fake_palette_install_transaction"}; }
Result uninstall(std::chrono::milliseconds) noexcept {
    ++palette_uninstalls;
    if (fail_palette_uninstall) return {Outcome::Busy, "fake_palette_drain_retained"};
    palette_hooks = false; color_sources.clear(); return {Outcome::Ready, "fake_palette_drained"};
}
bool current_invocation(NativeInvocation& out) noexcept { if (!palette_invocation) return false; out = *palette_invocation; return true; }
bool invocation_matches(const NativeInvocation& i) noexcept {
    return palette_invocation && i.return_slot == palette_invocation->return_slot && i.caller == palette_invocation->caller
        && i.serial == palette_invocation->serial && i.widget == palette_invocation->widget && i.thread == GetCurrentThreadId();
}
bool admitted_palette(const Subject& subject, const Authority& a) {
    require(!matching_retirement, "retirement predicates must not enter palette operations");
    return palette_hooks && !polling && a.qualify_current_callback && a.qualify_current_callback(a.context, a.session)
        && subject.validate && subject.validate(subject.context, subject, a.session);
}
Result bind(const Subject& subject, const Policy& policy, const Authority& a) noexcept {
    if (!admitted_palette(subject, a)) return {Outcome::Authority, "fake_palette_bind_authority"};
    if (++palette_bind_attempts==fail_palette_bind_at || fail_next_palette_bind) { fail_next_palette_bind = false; return {Outcome::Memory, "fake_palette_bind_rejected"}; }
    const auto key = std::pair{subject.widget, subject.property};
    if (color_sources.contains(key)) return {Outcome::Busy, "fake_palette_duplicate"};
    const auto existing = color_world.find(key);
    const std::array<float, 4> actual = existing == color_world.end() ? std::array<float, 4>{0.3f, 0.4f, 0.5f, 0.75f} : existing->second;
    color_sources[key] = {subject, policy, actual, actual};
    color_world[key] = color_sources[key].shown;
    if (slow_palette_bind) work_clock += 100;
    return {Outcome::Ready, "fake_palette_bound"};
}
Result change(const Subject& subject, const Policy& policy, const Authority& a) noexcept {
    auto it = color_sources.find({subject.widget, subject.property});
    if (!admitted_palette(subject, a) || it == color_sources.end()) return {Outcome::Identity, "fake_palette_change_subject"};
    auto& source = it->second;
    if (source.stale) return {Outcome::Unavailable, "fake_native_source_stale"};
    if (++palette_change_attempts == fail_palette_change_at) return {Outcome::Memory, "fake_palette_change_failure"};
    ++palette_changes;
    ++palette_changes_by_property[subject.property];
    source.policy = policy; source.shown = source.native;
    const auto& tint = policy.is_selected ? policy.selected : policy.normal;
    if (tint.enabled) { source.shown = tint.linear; source.shown[3] *= source.native[3]; }
    color_world[it->first] = source.shown;
    if (reenter_palette_change) { reenter_palette_change = false; runtime::callback_entered(); }
    return {Outcome::Ready, "fake_palette_changed"};
}
Result restore(const Subject& subject, const Authority& a) noexcept {
    ++palette_restores[subject.property]; return change(subject, {}, a);
}
Result retire_subject(const Subject& subject, const Authority& a) noexcept {
    const auto it = color_sources.find({subject.widget, subject.property});
    if (it == color_sources.end()) return {Outcome::Identity, "fake_missing_retired_subject"};
    if (!admitted_palette(subject, a) || it->second.stale) {
        it->second.subject.validate = nullptr;
        return {Outcome::Authority, "fake_subject_detached_orphan"};
    }
    color_world[it->first] = it->second.native; color_sources.erase(it);
    ++subject_retires;
    return {Outcome::Ready, "fake_subject_restored_retired"};
}
Result retire_matching(void* owner, std::uint64_t generation, RetirementMatch match, void* context, const Authority& a) noexcept {
    require(!matching_retirement, "retirement predicate must not reenter palette");
    ++filtered_retire_calls; bool restored = true;
    for (auto it = color_sources.begin(); it != color_sources.end();) {
        ++retirement_visits;
        if (!it->second.subject.validate || it->second.subject.owner != owner || it->second.subject.generation != generation) { ++it; continue; }
        matching_retirement = true;
        const bool selected = match && match(context, it->second.subject);
        matching_retirement = false;
        if (!selected) { ++it; continue; }
        if (!admitted_palette(it->second.subject, a) || it->second.stale) {
            restored = false; it->second.subject.validate = nullptr; ++it;
        } else { color_world[it->first] = it->second.native; it = color_sources.erase(it); ++subject_retires; }
    }
    return {restored ? Outcome::Ready : Outcome::Authority, "fake_filtered_retirement"};
}
Result retire(void* owner, std::uint64_t generation, const Authority& a) noexcept {
    ++owner_retires;
    if (fail_palette_retirement) { fail_palette_retirement = false; return {Outcome::Authority, "fake_retirement_ownership_retained"}; }
    bool restored = true;
    for (auto it = color_sources.begin(); it != color_sources.end();) {
        if (it->second.subject.owner != owner || it->second.subject.generation != generation) { ++it; continue; }
        if (!admitted_palette(it->second.subject, a) || it->second.stale) { restored = false; it->second.subject.validate = nullptr; ++it; }
        else { color_world[it->first] = it->second.native; it = color_sources.erase(it); }
    }
    return {restored ? Outcome::Ready : Outcome::Authority, "fake_palette_retire"};
}
Result last_failure() noexcept { return {Outcome::Unavailable, "fake_last_failure"}; }
}
namespace {
void add(core::ThemeDefinition d, std::uint64_t& bytes) {
    std::string reason;
    const bool retained = runtime::retain(std::move(d), bytes, reason);
    require(retained, reason.empty() ? "compiled program retained" : reason.c_str());
}
void multi_target_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); empty_inventory(); std::uint64_t bytes = 0;
    add(images("alpha", {{"MusicList/Bg_1", 1}, {"MusicList/Bg_2", 1}}), bytes);
    add(images("beta", {{"MusicList/Bg_3", 2}}), bytes);
    add(images("broken", {{"MusicList/Bg_4", 3}}), bytes);
    auto palette = images("palette", {{"MusicList/Bg_5", 4}});
    palette.colors["SpeedIcon/SpeedIcon.ColorAndOpacity"] = {128, 0, 0, 255};
    add(std::move(palette), bytes);
    add(images("row", {{"SpeedIcon/SpeedIcon.1", 1}}), bytes);
    missing_leaf = "Bg_4"; open(lease);
    require(lookups == 4 && creates == 2 && captures == 3 && composes == 3
        && runtime::state.bindings.size() == 6 && runtime::state.binding_templates == 6
        && runtime::state.themes[1].available && runtime::state.themes[2].available
        && !runtime::state.themes[3].available && !runtime::state.themes[4].available
        && !runtime::state.themes[5].available && !originals.contains("Bg_5"),
        "whole live admission retains all templates but prepares only complete programs with current receivers");
    require(runtime::state.themes[4].program.colors[0].linear[0] > 0.21f
        && runtime::state.themes[5].program.images[0].target.scope == game::menu_theme_plan::Scope::SpeedIcon
        && runtime::state.themes[5].resources[0] == runtime::state.themes[1].resources[0],
        "compiled pending palette and row definitions survive without subset admission");
    key(lease);
    require(saved == "alpha" && visuals["Bg_1"] == 1 && visuals["Bg_2"] == 1
        && visuals["Bg_3"] == originals["Bg_3"], "one pixel resource composes onto multiple exact original shapes");
    key(lease);
    require(saved == "beta" && visuals["Bg_1"] == originals["Bg_1"] && visuals["Bg_2"] == originals["Bg_2"]
        && visuals["Bg_3"] == 2, "disjoint override restores each missing binding to its own original");
    key(lease);
    require(saved == "original" && visuals == originals, "original restores master table, inadmissible programs are skipped");
    sample(true, false); sample(true, false, true, false); deliver(lease);
    require(saved == "beta", "previous wrap skips pending adapters and unavailable targets");
    sample(true, false); sample(true, false, true, false); deliver(lease);
    require(saved == "alpha", "previous follows sorted admitted programs");
    close(lease); require(visuals == originals, "exact close restores all touched bindings");
    ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(creates == 2 && composes == 6 && saved == "alpha" && visuals["Bg_1"] == 1,
        "new owner composes current affinities while immutable resources are reused");
    close(lease); ++current.generation; current.phase = game::MenuSessionPhase::Ready;
    runtime::state.selected = "palette"; const auto prior_applies = applies, prior_saves = saves; open(lease);
    require(applies == prior_applies && saves == prior_saves && visuals == originals,
        "loaded pending palette never applies its image subset or persists");

    auto transition_inventory = [&] {
        reset(); empty_inventory(); bytes = 0;
        add(images("alpha", {{"MusicList/Bg_1", 1}, {"MusicList/Bg_2", 1}}), bytes);
        add(images("beta", {{"MusicList/Bg_1", 2}, {"MusicList/Bg_3", 2}}), bytes);
        add(images("gamma", {{"MusicList/Bg_3", 4}}), bytes);
        open(lease); key(lease); require(saved == "alpha", "transition prior program installed");
    };
    transition_inventory();
    auto prior_writes = applies; const auto prior_saves_alpha = saves;
    fail_borrow_leaf = "Bg_2"; key(lease);
    require(applies == prior_writes && saves == prior_saves_alpha && saved == "alpha" && !runtime::state.compromised.load(),
        "transition preflights all choices before first write and preserves committed program");
    fail_borrow_leaf.clear(); key(lease);
    require(saved == "gamma", "preflight-rejected program does not disable working successor");
    transition_inventory(); prior_writes = applies; fail_apply_at = applies + 2; key(lease);
    require(applies == prior_writes + 4 && saved == "alpha" && saves == 1 && runtime::state.applied == 1
        && visuals["Bg_1"] == 1 && visuals["Bg_2"] == 1 && visuals["Bg_3"] == originals["Bg_3"]
        && !runtime::state.compromised.load(), "later assignment fault rolls back both attempted bindings without persistence");
    key(lease); require(saved == "gamma", "rollback preserves unrelated valid successor");
    transition_inventory(); prior_writes = applies; fail_apply_at = applies + 2; fail_rollback = true; key(lease);
    require(applies == prior_writes + 3 && saved == "alpha" && saves == 1 && runtime::state.compromised.load()
        && !runtime::state.owner, "rollback failure stops further writes and compromises owner");
    transition_inventory(); prior_writes = applies; nested_apply_at = applies + 2; key(lease);
    require(applies == prior_writes + 2 && saved == "alpha" && saves == 1 && original_calls == 1
        && runtime::state.compromised.load() && !runtime::state.owner,
        "retirement during later assignment stops without stale rollback or persistence");
    transition_inventory(); close(lease); ++current.generation; current.phase = game::MenuSessionPhase::Ready;
    fail_compose_leaf = "Bg_3"; runtime::state.selected = "beta"; const auto saved_before = saves; open(lease);
    require(saves == saved_before && visuals == originals && runtime::state.themes[1].available
        && !runtime::state.themes[2].available && !runtime::state.compromised.load(),
        "failed composition prevents whole program admission without disabling valid bindings");
    key(lease); require(saved == "alpha", "valid program survives other composition failure");
}
}

namespace {
void generic_palette_row_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); empty_inventory(); generic_enabled = true; std::uint64_t failed_bytes = 0;
    widget_kinds = {{L"Pie", native::SubjectKind::PieImage}, {L"Text", native::SubjectKind::TextBlock}};
    auto unavailable = images("bad", {{"MusicList/Pie", 4}}); unavailable.colors["MusicList/Text"] = {255, 0, 0, 255};
    add(std::move(unavailable), failed_bytes); add(images("good", {{"MusicList/Pie", 4}}), failed_bytes);
    runtime::state.palette_configured = true; fail_palette_install = true; runtime::install_palette_hooks(); open(lease); key(lease);
    require(!runtime::state.themes[1].available && runtime::state.themes[2].available && saved == "good"
        && visuals.at("Pie") == 4 && !runtime::state.compromised,
        "failed optional setter install excludes mixed program wholly without disabling valid image peer");
    close(lease); require(runtime::uninstall_palette_hooks(), "failed optional install still participates in teardown");
    reset(); empty_inventory(); generic_enabled = true; std::uint64_t bytes = 0;
    widget_kinds = {{L"Button", native::SubjectKind::Button}, {L"Panel", native::SubjectKind::CanvasPanel},
        {L"Pie", native::SubjectKind::PieImage}, {L"Set", native::SubjectKind::ImageSet},
        {L"Text", native::SubjectKind::TextBlock}, {L"Tiled", native::SubjectKind::TiledImage}};
    auto alpha = images("alpha", {{"MusicList/Pie", 4}, {"MusicList/Tiled", 4}, {"MusicList/Set.1", 5},
        {"MusicList/Button.Normal", 6}, {"MusicList/Button.Hovered", 7}});
    alpha.colors["MusicListCell/Text"] = {128, 0, 0, 128}; add(std::move(alpha), bytes);
    core::ThemeDefinition beta; beta.id = "beta";
    beta.colors["MusicList/Button.BackgroundColor"] = {0, 255, 0, 255};
    beta.colors["MusicList/Panel.ContentColorAndOpacity"] = {255, 0, 0, 255};
    beta.colors["MusicList/Text.ShadowColorAndOpacity"] = {0, 0, 255, 255}; add(std::move(beta), bytes);
    core::ThemeDefinition wrong; wrong.id = "gamma";
    wrong.colors["MusicList/Text.BackgroundColor"] = {255, 0, 255, 255}; add(std::move(wrong), bytes);
    add(images("zslot", {{"MusicList/Set.9", 9}}), bytes);
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "fake palette authentication");
    runtime::state.palette_configured = true; fail_palette_install = true; runtime::install_palette_hooks();
    require(!runtime::state.palette_installed && !palette_hooks, "optional failed install publishes no palette capability");
    fail_palette_install = false; runtime::install_palette_hooks();
    native_selected = 4;
    auto* row = reinterpret_cast<void*>(0x72001000); row_handles[row] = {91, 17}; active_rows[row] = true;
    auto* opening_widget = current.widget; row_handles[opening_widget] = current.widget_identity;
    current.widget = nullptr;
    current.phase = game::MenuSessionPhase::Opening;
    RowFrame setup{row, 17, 101, false}; row_frame = &setup;
    runtime::observe_row_setup(&setup, opening_widget, row, 17, 101, false, lease);
    unsigned row_originals = 0; ++row_originals; setup.after = true;
    runtime::observe_row_setup(&setup, opening_widget, row, 17, 101, true, lease); row_frame = nullptr;
    require(row_binds == 0 && runtime::state.rows[0].index == 17 && row_originals == 1,
        "Opening stores positive logical association without fabricated Ready mutation authority");
    current.widget = opening_widget; current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(runtime::state.semantic_selected == 4 && row_binds == 1 && runtime::state.rows[0].available
        && creates == 4 && color_sources.size() == 4 && !runtime::state.themes[3].available && !runtime::state.themes[4].available,
        "Ready uses real selected scalar and active row ticket, typed class slots, and class-compatible palettes only");
    require(runtime::state.themes[4].admission_stage
        && std::string_view(runtime::state.themes[4].admission_stage) == "fake_indexed_slot"
        && runtime::state.resources[runtime::state.themes[4].resources[0]].cached == runtime::Cached::Pending,
        "invalid renderer member rejects whole program before uploading its unique pixels");
    key(lease);
    require(saved == "alpha" && visuals.at("Button#0") == 6 && visuals.at("Button#1") == 7
        && visuals.at("Set#1") == 5 && visuals.at("Pie") == 4 && visuals.at("Tiled") == 4,
        "whole generic image/style and palette program commits together with shared pixel upload");
    auto text = std::find_if(color_sources.begin(), color_sources.end(), [](const auto& p) { return p.first.second == palette::Property::Text; });
    require(text != color_sources.end() && text->second.shown[0] > 0.21f && text->second.shown[3] < 0.38f,
        "actual compiled linear palette and native fade alpha compose");
    // Stateful native boundary: real runtime reader must qualify the private
    // invocation; the palette module's own detour/source tests remain separate.
    void* caller = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(runtime::state.module) + 0x900);
    palette::NativeInvocation invocation{&caller, caller, text->first.first, palette::Property::Text, false, GetCurrentThreadId(), 1};
    palette_invocation = &invocation; palette::Authority animation_authority{};
    require(palette_reader.current(nullptr, invocation, animation_authority)
        && animation_authority.qualify_current_callback(animation_authority.context, current), "real current native setter stack reader");
    gc = true; require(palette_reader.current(nullptr, invocation, animation_authority)
        && !animation_authority.qualify_current_callback(animation_authority.context, current),
        "candidate reader does not replace full admission rejecting unsafe engine state");
    text->second.stale = true; text->second.shown = {0.8f, 0.1f, 0.2f, 0.3f}; color_world[text->first] = text->second.shown;
    gc = false; require(palette_reader.current(nullptr, invocation, animation_authority), "new qualified full native source can recover");
    text->second.native = {0.9f, 0.2f, 0.1f, 0.4f}; text->second.stale = false;
    text->second.shown = text->second.policy.normal.linear; text->second.shown[3] *= 0.4f; color_world[text->first] = text->second.shown;
    runtime::callback_entered();
    require(!animation_authority.qualify_current_callback(animation_authority.context, current), "owned callback reentry retires parent native setter scope");
    ++invocation.serial;
    require(palette_reader.current(nullptr, invocation, animation_authority), "fresh setter establishes its own qualified invocation after retirement");
    palette_invocation = nullptr;
    require(!animation_authority.qualify_current_callback(animation_authority.context, current), "saved native facts never authorize outside actual private invocation");
    ++invocation.serial; palette_invocation = &invocation;
    require(!animation_authority.qualify_current_callback(animation_authority.context, current), "later private setter on same physical stack cannot revive prior invocation authority");
    palette_invocation = nullptr;
    key(lease);
    require(saved == "beta" && visuals == originals && text->second.shown == text->second.native,
        "missing image and palette overrides restore exact shapes and latest untinted animation, not first snapshot");
    SelectionFrame selection{17}; selection_frame = &selection;
    runtime::observe_selection(&selection, current.widget, 17, true, lease); selection_frame = nullptr;
    require(runtime::state.semantic_selected == 17 && native_selected == 4, "semantic event survives temporary aliased native selected scalar");
    setup = {row, 23, 102, false}; row_frame = &setup;
    runtime::observe_row_setup(&setup, current.widget, row, 23, 102, false, lease);
    require(!runtime::state.rows[0].available && runtime::state.row_rebind.load() == &setup, "physical row admission revoked before native rebind");
    ++row_originals; setup.after = true; active_rows.erase(row);
    runtime::observe_row_setup(&setup, current.widget, row, 23, 102, true, lease);
    setup.retired = true; runtime::observe_row_retired(&setup); row_frame = nullptr; finish_preparation(lease);
    require(!runtime::state.rows[0].available && row_binds == 1, "cached/pool-only row cannot acquire active strong membership");
    active_rows[row] = true; setup = {row, 23, 103, false}; row_frame = &setup;
    setup.after = false; runtime::observe_row_setup(&setup, current.widget, row, 23, 103, false, lease);
    ++row_originals; setup.after = true;
    runtime::observe_row_setup(&setup, current.widget, row, 23, 103, true, lease); row_frame = nullptr;
    setup.retired = true; runtime::observe_row_retired(&setup); finish_preparation(lease);
    sample(true, false); sample(true, false, true); deliver(lease); finish_preparation(lease);
    require(runtime::state.rows[0].available && runtime::state.rows[0].index == 23 && row_binds == 2 && row_originals == 3,
        "fresh setup generation rebinds recycled physical row to genuine logical index, never pool position");
    const auto before_close_saves = saves; close(lease);
    require(color_sources.empty() && runtime::state.palette_charged == 0 && visuals == originals && saves == before_close_saves,
        "qualified close restores and retires palette owners before context revocation without persisting original");
    ++current.generation; current.phase = game::MenuSessionPhase::Opening;
    setup = {row, 23, 104, false}; row_frame = &setup;
    runtime::observe_row_setup(&setup, current.widget, row, 23, 104, false, lease);
    setup.after = true; runtime::observe_row_setup(&setup, current.widget, row, 23, 104, true, lease); row_frame = nullptr;
    current.phase = game::MenuSessionPhase::Ready; open(lease);
    const auto captures_before_foreign_retire = captures + applies + composes + creates;
    const auto* stable_context = runtime::state.palettes[0].subject.context;
    std::thread foreign_retire([] { runtime::retire(); }); foreign_retire.join();
    require(runtime::state.compromised && runtime::state.owner && runtime::state.palette_current == 4
        && runtime::state.palettes[0].subject.context == stable_context
        && captures + applies + composes + creates == captures_before_foreign_retire,
        "foreign retirement atomically revokes but retains validator contexts without native calls until owner/drain");
    fail_palette_uninstall = true;
    require(!runtime::uninstall_palette_hooks() && runtime::state.palette_configured && palette_hooks,
        "failed drain retains capability and stable contexts for lifecycle retry");
    fail_palette_uninstall = false;
    require(runtime::uninstall_palette_hooks() && !palette_hooks && !runtime::state.palette_configured,
        "successful drained uninstall clears optional capability before identity services disappear");
}
}

template<class Original>
void setup_song_row(void* row, int index, std::uint64_t generation, const core::HookCallbackGate::Lease& lease,
    Original&& original, bool refresh = false, void* context = nullptr, bool complete = true, bool finish = true) {
    RowFrame frame{row, index, generation, false}; frame.refresh = refresh;
    auto* parent = row_frame; if (parent) parent->retired = true;
    row_frame = &frame;
    auto* receiver = context ? context : current.widget;
    const auto source = refresh ? runtime::RowSetupSource::OwnedRefresh : runtime::RowSetupSource::NativeSetup;
    runtime::observe_row_setup(&frame, receiver, row, index, generation, false, lease, source);
    ++row_originals_called; original(); // The real module forwards each setup/refresh branch once.
    frame.after = true;
    if (complete && !frame.retired) runtime::observe_row_setup(&frame, receiver, row, index, generation, true, lease, source);
    frame.retired = true; runtime::observe_row_retired(&frame); row_frame = parent;
    if (!parent && finish) finish_preparation(lease);
}
template<class Original>
void select_song(int index, const core::HookCallbackGate::Lease& lease, Original&& original) {
    SelectionFrame frame{index, false}; selection_frame = &frame;
    runtime::observe_selection(&frame, current.widget, index, false, lease);
    ++selection_originals; original(); frame.after = true;
    runtime::observe_selection(&frame, current.widget, index, true, lease); selection_frame = nullptr;
    finish_preparation(lease);
}
std::string row_leaf(void* row, const char* leaf) { return std::to_string(reinterpret_cast<std::uintptr_t>(row)) + "/" + leaf; }
void native_row_style(void* row) {
    const auto prefix = row_leaf(row, "");
    for (auto& [leaf, value] : originals) if (leaf.starts_with(prefix)) { value += 1000; visuals[leaf] = value; }
    for (auto& [key, value] : color_world)
        if (std::any_of(generic_subjects.begin(), generic_subjects.end(), [&](const auto& s) { return s.second.row == row && s.second.widget == key.first; }))
            value = {0.65f, 0.25f, 0.15f, 0.4f};
}
ColorSource& row_color(void* row, palette::Property property) {
    const auto found = std::find_if(color_sources.begin(), color_sources.end(), [&](const auto& entry) {
        const auto* context = static_cast<const runtime::PaletteBinding*>(entry.second.subject.context);
        return entry.first.second == property && context->row != SIZE_MAX && runtime::state.rows[context->row].row == row;
    });
    require(found != color_sources.end(), "current physical row palette source"); return found->second;
}
void qualified_native_color(ColorSource& source, std::array<float, 4> color) {
    void* caller = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(runtime::state.module) + 0x900);
    palette::NativeInvocation invocation{&caller, caller, source.subject.widget, source.subject.property, false, GetCurrentThreadId(), 100};
    palette_invocation = &invocation; palette::Authority a{};
    require(palette_reader.current(nullptr, invocation, a) && a.qualify_current_callback
        && a.qualify_current_callback(a.context, current) && source.subject.validate(source.subject.context, source.subject, current),
        "native animation requires real reader and current positive row association");
    source.native = color; source.stale = false;
    const auto& tint = source.policy.is_selected ? source.policy.selected : source.policy.normal;
    source.shown = tint.enabled ? tint.linear : color; if (tint.enabled) source.shown[3] *= color[3];
    color_world[{source.subject.widget, source.subject.property}] = source.shown; palette_invocation = nullptr;
}
void song_row_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); empty_inventory(); generic_enabled = generic_tint_enabled = true;
    widget_kinds = {{L"MainBg", native::SubjectKind::Image}, {L"CellBg", native::SubjectKind::Image},
        {L"Icon", native::SubjectKind::Image}, {L"CellButton", native::SubjectKind::Button}, {L"CellLabel", native::SubjectKind::TextBlock}};
    std::uint64_t bytes = 0;
    auto alpha = images("alpha", {{"MusicList/MainBg", 1}, {"MusicListCell/CellBg", 2}, {"MusicListCell/CellBg@selected", 9},
        {"MusicListCell/Icon@selected", 3}, {"MusicListCell/CellButton.Normal", 4}});
    alpha.colors["MusicListCell/CellButton.Normal@selected"] = {128, 0, 0, 255};
    alpha.colors["MusicListCell/CellLabel.ColorAndOpacity"] = {255, 0, 0, 255};
    alpha.colors["MusicListCell/CellLabel.ColorAndOpacity@normal"] = {0, 0, 255, 128};
    alpha.colors["MusicListCell/CellLabel.ColorAndOpacity@selected"] = {0, 255, 0, 192}; add(std::move(alpha), bytes);
    auto beta = images("beta", {{"MusicListCell/Icon@normal", 6}});
    beta.colors["MusicListCell/CellLabel.OutlineColorAndOpacity@selected"] = {128, 64, 0, 255}; add(std::move(beta), bytes);
    core::ThemeDefinition wrong; wrong.id = "gamma"; wrong.colors["MusicListCell/CellLabel.BackgroundColor"] = {255, 0, 0, 255}; add(std::move(wrong), bytes);
    add(images("speed", {{"SpeedIcon/SpeedIcon.1", 2}}), bytes);
    add(images("zmain", {{"MusicList/MainBg", 1}}), bytes);
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "palette reader configured");
    runtime::state.palette_configured = true; runtime::install_palette_hooks();
    auto* const a = reinterpret_cast<void*>(0x72001000); auto* const b = reinterpret_cast<void*>(0x72002000);
    row_handles[a] = {91, 17}; row_handles[b] = {92, 18}; active_rows[a] = active_rows[b] = true;
    auto* receiver = current.widget; row_handles[receiver] = current.widget_identity;
    current.phase = game::MenuSessionPhase::Opening; current.widget = nullptr;
    setup_song_row(a, 17, 101, lease, [] {}, false, receiver);
    setup_song_row(b, 23, 102, lease, [] {}, false, receiver);
    require(row_binds == 0 && creates == 0 && color_sources.empty() && row_originals_called == 2, "Opening associates logical rows without native mutation");
    current.widget = receiver; current.phase = game::MenuSessionPhase::Ready; native_selected = 17; open(lease);
    require(runtime::state.themes[1].available && runtime::state.themes[2].available && !runtime::state.themes[3].available
        && !runtime::state.themes[4].available && runtime::state.themes[5].available && row_binds == 2, "song rows admit independently of wrong-property and Speed peers");
    key(lease);
    require(saved == "alpha" && visuals.at(row_leaf(a, "CellBg")) == 9 && visuals.at(row_leaf(b, "CellBg")) == 2
        && visuals.at(row_leaf(a, "Icon")) == 3 && visuals.at(row_leaf(b, "Icon")) == originals.at(row_leaf(b, "Icon")),
        "selected logical index chooses cached resource variants and exact omitted originals");
    require(row_color(a, palette::Property::Text).policy.is_selected && !row_color(b, palette::Property::Text).policy.is_selected
        && row_color(a, palette::Property::Text).shown[1] == 1 && row_color(b, palette::Property::Text).shown[2] == 1,
        "semantic selected/normal palettes override Any independently");
    const auto factories = creates + captures + composes + tint_calls + lookups; const auto user_saves = saves;
    unsigned before_original_returns = 0;
    select_song(23, lease, [&] {
        require(runtime::state.semantic_selected == 23 && row_color(b, palette::Property::Text).policy.is_selected, "policy published before native setters");
        native_selected = 0; // A temporary stock alias is not the logical selected song.
        qualified_native_color(row_color(b, palette::Property::Text), {0.2f, 0.3f, 0.4f, 0.6f});
        before_original_returns = applies; // Native selection changes semantic/animation values, not owning brush storage.
    });
    require(visuals.at(row_leaf(a, "CellBg")) == 2 && visuals.at(row_leaf(b, "CellBg")) == 9
        && visuals.at(row_leaf(a, "Icon")) == originals.at(row_leaf(a, "Icon")) && visuals.at(row_leaf(b, "Icon")) == 3
        && row_color(b, palette::Property::Text).shown[3] > 0.45f && row_color(b, palette::Property::Text).shown[3] < 0.46f
        && factories == creates + captures + composes + tint_calls + lookups && saves == user_saves && applies == before_original_returns,
        "latest native fade survives selection without duplicate post-original brush writes or persistence");
    sample(true, false); sample(true, true); const auto stale_ticket = queued_ticket;
    const auto other_world = row_color(b, palette::Property::Text).shown; const auto uploads = creates; const auto owners = owner_retires;
    setup_song_row(a, 42, 103, lease, [&] {
        require(subject_retires == 2 && owner_retires == owners && color_sources.size() == 2
            && row_color(b, palette::Property::Text).shown == other_world, "recycling detaches only that row, not other rows or owner");
        require(visuals.at(row_leaf(a, "CellBg")) == originals.at(row_leaf(a, "CellBg")), "old row restores before native rebind");
        deliver(lease); require(saves == user_saves, "queued old gesture cannot enter rebind");
        sample(true, false); sample(true, true); require(!runtime::state.pending.ticket, "poller cannot queue delayed row work");
        native_row_style(a);
    }, true);
    require(stale_ticket && creates == uploads && row_originals_called == 3 && runtime::state.rows[0].index == 42
        && visuals.at(row_leaf(a, "CellBg")) == 2 && row_color(a, palette::Property::Text).native[3] == 0.4f
        && saves == user_saves && !runtime::state.row_rebind.load(), "module refresh uses new generation/source, no upload or save");
    select_song(42, lease, [] {});
    require(visuals.at(row_leaf(a, "CellBg")) == 9 && visuals.at(row_leaf(b, "CellBg")) == 2, "recycled row uses new logical index, not pool position");
    sample(true, false); key(lease); finish_preparation(lease);
    require(saved == "beta" && visuals.at("MainBg") == originals.at("MainBg")
        && visuals.at(row_leaf(a, "CellBg")) == originals.at(row_leaf(a, "CellBg"))
        && visuals.at(row_leaf(a, "Icon")) == originals.at(row_leaf(a, "Icon")) && visuals.at(row_leaf(b, "Icon")) == 6
        && row_color(a, palette::Property::Text).shown == row_color(a, palette::Property::Text).native,
        "disjoint theme restores missing main/row/resource/state/colors to current exact originals");
    sample(true, false); sample(true, false, true, false); deliver(lease); require(saved == "alpha", "previous gesture unchanged for row programs");
    const auto reopen_uploads = creates; close(lease);
    require(visuals == originals && color_sources.empty(), "close restores all main/row bindings and untinted sources");
    ++current.generation; current.phase = game::MenuSessionPhase::Opening;
    setup_song_row(a, 42, 201, lease, [] {}); setup_song_row(b, 23, 202, lease, [] {});
    current.phase = game::MenuSessionPhase::Ready; native_selected = 42; open(lease);
    require(creates == reopen_uploads && runtime::state.applied == 1, "reopen reuses pixels and recaptures current row affinities");
    // Rebinding has no generation/count quota while actual native bytes remain available.
    for (std::uint64_t i = 0; i < 64 && runtime::state.themes[1].available; ++i)
        setup_song_row(a, 43 + i, 300 + i, lease, [&] { native_row_style(a); }, true);
    require(runtime::state.themes[1].available && !runtime::state.compromised.load(), "recycling is admitted by actual bytes, not a reopen/rebind quota");
    retained_bytes = native::kRetainedByteLimit - native::kOriginalCaptureByteCharge;
    setup_song_row(a, 42, 500, lease, [&] { native_row_style(a); }, true);
    require(!runtime::state.compromised.load() && !runtime::state.themes[1].available && runtime::state.themes[5].available
        && retained_bytes <= native::kRetainedByteLimit
        && retained_bytes == native::kRetainedByteLimit && runtime::state.applied == 0
        && std::string_view(runtime::state.themes[1].admission_stage) == "original_budget",
        "actual native byte exhaustion rejects row candidate while independent main program remains usable");
    close(lease);
}
void song_failure_tests(const core::HookCallbackGate::Lease& lease) {
    auto* row = reinterpret_cast<void*>(0x72003000);
    const auto initialize = [&](bool opening_row, bool drift = false, bool semantic_brush = false) {
        reset(); empty_inventory(); generic_enabled = true;
        widget_kinds = {{L"CellBg", native::SubjectKind::Image}, {L"CellLabel", native::SubjectKind::TextBlock}, {L"MainBg", native::SubjectKind::Image}};
        std::uint64_t bytes = 0;
        auto alpha = images("alpha", {{"MusicListCell/CellBg", 2}});
        if (semantic_brush) alpha.images["MusicListCell/CellBg@normal"] = {{3, 0, 0, 255}, 1, 1};
        alpha.colors["MusicListCell/CellLabel.ColorAndOpacity"] = {0, 0, 255, 128}; add(std::move(alpha), bytes);
        add(images("zmain", {{"MusicList/MainBg", 1}}), bytes);
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "failure fixture palette authentication");
        runtime::state.palette_configured = true; runtime::install_palette_hooks(); runtime::state.selected = "alpha";
        row_handles[row] = {99, 19}; active_rows[row] = true;
        if (opening_row) {
            current.phase = game::MenuSessionPhase::Opening;
            setup_song_row(row, 17, 1, lease, [] {});
            if (drift) ++row_handles[row].serial_number;
        }
        current.phase = game::MenuSessionPhase::Ready; native_selected = 17; open(lease);
    };
    initialize(false);
    require(!runtime::state.themes[1].available && runtime::state.themes[2].available && runtime::state.applied == 0,
        "delayed song membership is explicit unavailable state, not partial theme success");
    const auto uploads = creates; const auto no_saves = saves;
    setup_song_row(row, 42, 2, lease, [] {});
    require(runtime::state.applied == 1 && creates == uploads + 1 && saves == no_saves + 1 && saved == "alpha"
        && visuals.at(row_leaf(row, "CellBg")) == 2,
        "deferred loaded selection prepares once and persists only after actual complete application");
    close(lease);

    initialize(true, true);
    require(!runtime::state.themes[1].available && row_binds == 0 && runtime::state.rows[0].failed,
        "recycled allocation between Opening and Ready cannot inherit old logical association");
    setup_song_row(row, 42, 3, lease, [] {});
    require(runtime::state.rows[0].index == 42 && runtime::state.applied == 1 && row_binds == 1,
        "only fresh actual setup repairs serial drift with genuine new logical index");
    close(lease);

    initialize(true);
    const auto prior_saves = saves;
    fail_apply_at = applies + 2; key(lease);
    require(!runtime::state.compromised.load() && runtime::state.applied == 1 && saves == prior_saves
        && visuals.at(row_leaf(row, "CellBg")) == 2 && visuals.at("MainBg") == originals.at("MainBg")
        && row_color(row, palette::Property::Text).shown[2] == 1,
        "failed second target restores both attempted writes and prior row policy, no false persistence");
    close(lease);

    initialize(true);
    const auto uniform_calls = applies + borrows, uniform_saves = saves;
    select_song(42, lease, [] {});
    require(runtime::state.semantic_selected == 42 && runtime::state.applied == 1
        && applies + borrows == uniform_calls && saves == uniform_saves,
        "uniform row overrides do not force cached brush reapplication on semantic events");
    close(lease);

    initialize(true, false, true);
    const auto semantic_saves = saves;
    fail_apply_at = applies + 1;
    select_song(42, lease, [] {});
    require(!runtime::state.compromised.load() && runtime::state.applied == 0 && runtime::state.themes[1].rejected
        && visuals.at(row_leaf(row, "CellBg")) == originals.at(row_leaf(row, "CellBg"))
        && color_sources.empty() && saves == semantic_saves,
        "failed semantic reapplication restores original rather than leaving wrong-row state or persisting fallback");
    close(lease);

    initialize(true);
    const auto rebind_saves = saves;
    setup_song_row(row, 42, 5, lease, [&] { native_row_style(row); fail_apply_at = applies + 1; });
    require(!runtime::state.compromised.load() && runtime::state.applied == 0 && runtime::state.themes[1].rejected
        && visuals.at(row_leaf(row, "CellBg")) == originals.at(row_leaf(row, "CellBg"))
        && color_sources.empty() && saves == rebind_saves,
        "failed fresh-row application restores complete original, never a partially rebound program");
    close(lease);

    initialize(true);
    const auto before_retirement = creates + captures + composes + tint_calls + lookups;
    const auto retirement_saves = saves;
    setup_song_row(row, 43, 4, lease, [&] { native_row_style(row); }, true, nullptr, false);
    require(runtime::state.compromised.load() && !runtime::state.owner && !runtime::state.row_rebind.load()
        && saves == retirement_saves && before_retirement == creates + captures + composes + tint_calls + lookups
        && row_originals_called == 2, "unwound refresh retires owner without stale capture/cleanup and forwards original once");

    initialize(true);
    const auto reentry_saves = saves; const auto calls = applies;
    nested_selection = true; key(lease);
    require(runtime::state.compromised.load() && selection_originals == 1 && applies == calls + 1 && saves == reentry_saves,
        "actual selection reentry retires cached row transition; never stale rollback or persistence");
}
void speed_program_tests(const core::HookCallbackGate::Lease& lease) {
    auto* const song = reinterpret_cast<void*>(0x73001000);
    auto* const a = reinterpret_cast<void*>(0x73002000);
    auto* const b = reinterpret_cast<void*>(0x73003000);
    auto* const c = reinterpret_cast<void*>(0x73004000);
    const auto initialize = [&](bool fixture_candidates = true) {
        reset(); empty_inventory(); generic_enabled = generic_tint_enabled = true;
        widget_kinds = {{L"MainBg", native::SubjectKind::Image}, {L"CellBg", native::SubjectKind::Image},
            {L"SpeedIcon", native::SubjectKind::ImageSet}, {L"SpeedLabel", native::SubjectKind::TextBlock}};
        std::uint64_t bytes = 0;
        auto alpha = images("alpha", {{"MusicList/MainBg", 1}, {"MusicListCell/CellBg", 2}, {"SpeedIcon/SpeedIcon.1", 2}});
        alpha.colors["SpeedIcon/SpeedIcon.1"] = {128, 64, 0, 255};
        alpha.colors["SpeedIcon/SpeedIcon.ColorAndOpacity"] = {0, 255, 0, 192}; add(std::move(alpha), bytes);
        auto beta = images("beta", {{"SpeedIcon/SpeedIcon.2", 3}});
        beta.colors["SpeedIcon/SpeedLabel.BottomColorAndOpacity"] = {255, 0, 0, 128}; add(std::move(beta), bytes);
        add(images("bad", {{"SpeedIcon/SpeedIcon.9", 2}}), bytes);
        add(images("zmain", {{"MusicList/MainBg", 1}}), bytes);
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "speed palette authentication");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
        row_handles[song] = {110, 11}; row_handles[a] = {111, 12}; row_handles[b] = {112, 13};
        active_rows[song] = active_rows[a] = active_rows[b] = true; speed_members = {a, b};
        current.phase = game::MenuSessionPhase::Opening; setup_song_row(song, 17, 1, lease, [] {});
        current.phase = game::MenuSessionPhase::Ready; native_selected = 17; open(lease, fixture_candidates);
    };
    initialize();
    require(subject_prepares == 1 && subject_names_ready && speed_binds == 2 && row_binds == 1
        && runtime::state.themes[1].available && runtime::state.themes[2].available && !runtime::state.themes[3].available,
        "Ready prepares witnesses before uniform current Speed members, bad indexed peer excluded whole");
    const auto epoch_a = std::find_if(runtime::state.rows.begin(), runtime::state.rows.end(), [&](const auto& r) { return r.row == a; })->membership_epoch;
    for (const auto& [ticket, association] : row_tickets) if (association.epoch)
        require(association.index == -1 && association.setup == 0 && ticket, "local speed epoch is never a song index/native setup");
    key(lease);
    require(saved == "alpha" && visuals.at("MainBg") == 1 && visuals.at(row_leaf(song, "CellBg")) == 2
        && visuals.at(row_leaf(a, "SpeedIcon#1")) == 2 && visuals.at(row_leaf(b, "SpeedIcon#1")) == 2
        && visuals.at(row_leaf(a, "SpeedIcon#2")) == originals.at(row_leaf(a, "SpeedIcon#2")),
        "one complete main/song/uniform-speed program shares resources and preserves omitted slots");
    require(row_color(a, palette::Property::ImageSet).policy.normal.enabled && !row_color(a, palette::Property::ImageSet).policy.is_selected,
        "uniform speed color is independent of logical song selection");
    const auto prepared = creates + captures + composes + tint_calls + lookups; const auto user_saves = saves;
    int selected_index = 2; const std::array<float, 3> transforms{0.5f, 1.0f, 2.0f};
    select_song(23, lease, [&] { selected_index = 0; qualified_native_color(row_color(a, palette::Property::ImageSet), {0.1f, 0.2f, 0.3f, 0.6f}); });
    require(selected_index == 0 && transforms == std::array<float, 3>{0.5f, 1.0f, 2.0f} && speed_binds == 2
        && prepared == creates + captures + composes + tint_calls + lookups && saves == user_saves
        && std::find_if(runtime::state.rows.begin(), runtime::state.rows.end(), [&](const auto& r) { return r.row == a; })->membership_epoch == epoch_a
        && row_color(a, palette::Property::ImageSet).shown[3] > 0.45f && row_color(a, palette::Property::ImageSet).shown[3] < 0.46f,
        "selection encloses native speed update without rebinding, rebuilding, index override or stale fade");
    key(lease);
    require(saved == "beta" && visuals.at("MainBg") == originals.at("MainBg")
        && visuals.at(row_leaf(song, "CellBg")) == originals.at(row_leaf(song, "CellBg"))
        && visuals.at(row_leaf(a, "SpeedIcon#1")) == originals.at(row_leaf(a, "SpeedIcon#1"))
        && visuals.at(row_leaf(a, "SpeedIcon#2")) == 3 && !row_color(a, palette::Property::ImageSet).policy.normal.enabled,
        "speed transition restores disjoint main/song/brush/palette overrides to actual originals");
    sample(true, false); sample(true, false, true, false); deliver(lease);
    require(saved == "alpha" && prepared == creates + captures + composes + tint_calls + lookups, "previous uses cached complete speed program");
    close(lease); require(visuals == originals && color_sources.empty(), "close restores every scope before retiring current bindings");
    const auto uploads = creates;
    ++current.generation; current.phase = game::MenuSessionPhase::Opening; setup_song_row(song, 17, 2, lease, [] {});
    current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(creates == uploads && saved == "alpha" && speed_binds == 4, "reopen reuses textures, freshly binds actual Speed membership");
    close(lease);

    initialize(false); key(lease); finish_preparation(lease);
    require(runtime::state.applied == 1 && runtime::state.themes[1].available, "pending Speed removal starts from committed Alpha");
    row_handles[c] = {113, 14}; active_rows[c] = true; speed_members.push_back(c);
    const auto partial_sources = color_sources.size();
    fail_palette_bind_at=palette_bind_attempts+2; key(lease);
    const auto pending_member = std::find_if(runtime::state.rows.begin(), runtime::state.rows.end(), [&](const auto& row) { return row.row == c; });
    require(pending_member != runtime::state.rows.end(), "request positively binds new Speed member before real source rejection");
    const auto pending_index = static_cast<std::size_t>(pending_member - runtime::state.rows.begin());
    require(runtime::state.applied == 1 && runtime::state.desired == SIZE_MAX && runtime::state.pending_rows.contains(pending_index)
        && color_sources.size() == partial_sources + 1, "rejected peer retains real partial source and pending Speed contribution, not a partial commit");
    runtime::focus_lost();
    select_song(23, lease, [&] { speed_members = {a, b}; active_rows[c] = false; });
    require(!runtime::state.compromised.load() && runtime::state.applied == 1 && runtime::state.themes[1].available
        && !runtime::state.work_pending && runtime::state.pending_rows.empty() && saves == 1,
        "cancelled partial Speed member removal settles idle and preserves complete committed admission without input");
    sample(true, false);
    require(!queued_ticket, "revoked pending Speed row cannot perpetually post maintenance");
    std::cout << "Speed partial-bind/rejection/removal pending=0 idle=1 committed=alpha\n";
    close(lease);

    initialize(); key(lease);
    const auto stable_visuals = visuals; const auto stable_colors = color_world;
    const auto maintenance_saves = saves, maintenance_applies = applies, maintenance_colors = palette_changes;
    const auto membership_revision = runtime::state.work_revision;
    const auto speed_probes = subject_probes[{L"SpeedIcon", native::SubjectKind::ImageSet}];
    SelectionFrame new_member_selection{23, false}; selection_frame = &new_member_selection;
    runtime::observe_selection(&new_member_selection, current.widget, 23, false, lease);
    ++selection_originals;
    row_handles[c] = {113, 14}; active_rows[c] = true; speed_members.push_back(c);
    new_member_selection.after = true;
    runtime::observe_selection(&new_member_selection, current.widget, 23, true, lease); selection_frame = nullptr;
    require(runtime::state.semantic_selected == 23 && runtime::state.applied == 1 && runtime::state.work_pending
        && runtime::state.desired == 1 && !runtime::state.desired_save && !runtime::state.desired_gesture
        && runtime::state.work_revision != membership_revision
        && saves == maintenance_saves && applies == maintenance_applies && palette_changes == maintenance_colors,
        "post-original Speed discovery retains only non-saving applied-program maintenance intent");
    finish_preparation(lease); // No further input or selection event may be needed to style the new member.
    require(runtime::state.applied == 1 && runtime::state.themes[1].available && runtime::state.desired == SIZE_MAX
        && visuals.at(row_leaf(c, "SpeedIcon#1")) == 2 && row_color(c, palette::Property::ImageSet).policy.normal.enabled
        && saves == maintenance_saves && applies == maintenance_applies + 1 && palette_changes == maintenance_colors + 1,
        "maintenance completes the applied program on the new Speed member without persistence or unrelated writes");
    require(runtime::state.themes[1].work.revision == runtime::state.work_revision
        && subject_probes[{L"SpeedIcon", native::SubjectKind::ImageSet}] == speed_probes + 1,
        "Speed membership rebuilds dependencies and freshly resolves only new receiver, sharing its brush/palette Subject");
    for (const auto& [target, value] : stable_visuals)
        require(visuals.at(target) == value, "Speed maintenance preserves every previously styled brush");
    for (const auto& [target, value] : stable_colors)
        require(color_world.at(target) == value, "Speed maintenance preserves every previously styled palette");
    close(lease);

    initialize(); key(lease);
    row_handles[c] = {113, 14}; active_rows[c] = true; speed_members.push_back(c);
    const auto membership_saves = saves, membership_factories = creates + captures + composes + tint_calls + lookups;
    key(lease);
    require(!runtime::state.compromised.load() && runtime::state.applied == 0 && visuals == originals
        && saves == membership_saves && saved == "alpha"
        && creates + captures + composes + tint_calls + lookups == membership_factories,
        "membership invalidating active program restores cached originals without factories or false persistence");

    initialize(); key(lease);
    sample(true, false); sample(true, false, true); deliver(lease); // Return to quiet Original before native recycling.
    const auto owners = owner_retires; const auto epoch = runtime::state.speed_epoch;
    ++row_trees[a]; native_row_style(a);
    select_song(23, lease, [] {});
    require(speed_binds == 2 && !runtime::state.work_pending,
        "quiet Original does not reconcile inactive Speed membership during selection");
    key(lease); finish_preparation(lease);
    require(!runtime::state.compromised.load() && runtime::state.speed_ready && speed_binds == 3
        && runtime::state.speed_epoch == epoch + 1 && owner_retires == owners, "explicit request reconciles only changed positive row after unthemed recycling");
    row_handles[c] = {113, 14}; active_rows[c] = true; speed_members.push_back(c);
    const auto no_dispatch_factories = creates + captures + composes + tint_calls + lookups; const auto no_dispatch_saves = saves;
    key(lease);
    finish_preparation(lease);
    require(creates + captures + composes + tint_calls + lookups > no_dispatch_factories && saves == no_dispatch_saves + 1
        && runtime::state.themes[2].available && runtime::state.applied == 2
        && visuals.at(row_leaf(c, "SpeedIcon#2")) == 3,
        "explicit request prepares new membership and publishes only the complete requested program");
    select_song(17, lease, [] {});
    sample(true, false); sample(true, false, true); deliver(lease); finish_preparation(lease);
    require(saved == "alpha", "explicit previous request persists Alpha only after admission");
    require(visuals.at(row_leaf(c, "SpeedIcon#1")) == 2, "qualified request styles new member before complete publication");
    speed_members = {a, c}; active_rows[b] = false;
    const auto missing_owners = owner_retires;
    select_song(23, lease, [] {});
    require(!runtime::state.compromised.load() && owner_retires == missing_owners && runtime::state.themes[1].available,
        "missing member revokes only its contexts, remaining actual members stay usable");
    speed_members.push_back(b); active_rows[b] = true;
    const auto before_reappearance = creates + captures + composes + tint_calls + lookups; const auto before_reappearance_saves = saves;
    select_song(17, lease, [] {});
    require(runtime::state.compromised.load() && before_reappearance == creates + captures + composes + tint_calls + lookups
        && saves == before_reappearance_saves, "previously themed detached allocation cannot be recaptured as original on unexplained reappearance");

    initialize(); fail_speed_read = true; key(lease);
    require(!runtime::state.compromised.load() && saves == 0 && !runtime::state.themes[1].available && runtime::state.themes[4].available,
        "unavailable speed capability does not claim partial program or disable valid main peer");
    close(lease);

    initialize(); fail_apply_at = applies + 3; key(lease);
    require(native_poisoned && runtime::state.compromised.load() && saves == 0 && visuals != originals && applies == 3,
        "faulted speed batch latches backend, forbids rollback of earlier main/song writes and never saves a partial ID");
    const auto faulted_visuals = visuals; const auto faulted_applies = applies;
    key(lease); close(lease);
    require(visuals == faulted_visuals && applies == faulted_applies && saves == 0,
        "native-fault compromise forbids switching to a peer or stale close restoration");

    reset(); empty_inventory(); generic_enabled = true;
    widget_kinds = {{L"SpeedIcon", native::SubjectKind::ImageSet}};
    core::ThemeDefinition colors; colors.id = "palette"; colors.colors["SpeedIcon/SpeedIcon.ColorAndOpacity"] = {0, 128, 255, 128};
    std::uint64_t color_bytes = 0; add(std::move(colors), color_bytes);
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "palette-only speed configure");
    runtime::state.palette_configured = true; runtime::install_palette_hooks();
    row_handles[a] = {111, 12}; active_rows[a] = true; speed_members = {a}; open(lease);
    require(subject_prepares == 1 && speed_binds == 1 && creates == 0 && captures == 0 && runtime::state.themes[1].available,
        "palette-only speed inventory prepares actual witnesses before first enumeration without image/row side effects");
    key(lease); require(saved == "palette" && row_color(a, palette::Property::ImageSet).shown[2] == 1, "palette-only program persists actual applied ID");
    close(lease); require(color_sources.empty(), "palette-only original retirement under close authority");
}
void shared_subject_work_tests(const core::HookCallbackGate::Lease& lease) {
    const auto enable_palette = [&] {
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
            {nullptr, runtime::read_palette_stack})), "shared-subject reader configures");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
    };
    reset(); empty_inventory(); std::uint64_t bytes = 0;
    auto alpha = images("alpha", {{"MusicList/Btn.Normal", 3}, {"MusicList/Btn.Hovered", 3},
        {"MusicList/Btn.Pressed", 3}, {"MusicList/Btn.Disabled", 3},
        {"MusicList/Set.0", 3}, {"MusicList/Set.1", 3}, {"MusicList/Set.2", 3}});
    alpha.colors["MusicList/Btn.ColorAndOpacity"] = {180, 80, 40, 255};
    alpha.colors["MusicList/Btn.BackgroundColor"] = {40, 80, 180, 255};
    alpha.colors["MusicList/Label.ColorAndOpacity"] = {180, 80, 40, 255};
    alpha.colors["MusicList/Label.OutlineColorAndOpacity"] = {40, 80, 180, 255};
    alpha.colors["MusicList/Label.ShadowColorAndOpacity"] = {80, 180, 40, 255};
    auto beta = alpha; beta.id = "beta";
    for (auto& [name, pixels] : beta.images) { (void)name; pixels.bgra[0] = 5; }
    add(std::move(alpha), bytes); add(std::move(beta), bytes);
    generic_enabled = true; widget_kinds[L"Btn"] = native::SubjectKind::Button;
    widget_kinds[L"Set"] = native::SubjectKind::ImageSet; widget_kinds[L"Label"] = native::SubjectKind::TextBlock;
    enable_palette(); slow_capture = true; open(lease, false);
    key(lease);
    auto& s = runtime::state;
    require(lookups == 8 && captures == 7 && capture_batches == 2 && capture_api_calls == 2
        && s.subjects.size() == 3 && applies==7 && saves==1,
        "four Button and three ImageSet members plus five properties resolve three current widgets, not each member/property");
    require(subject_probes[{L"Btn", native::SubjectKind::Button}] == 1
        && subject_probes[{L"Set", native::SubjectKind::ImageSet}] == 1
        && subject_probes[{L"Label", native::SubjectKind::TextBlock}] == 1 && subject_accesses > 3,
        "shared copies still use fresh access/validation; first primary Text classification probes six genuine classes");
    const auto revision = s.work_revision;
    auto capture_cursor = s.themes[1].work.capture;
    const auto resolved_cursor = s.themes[1].work.resolve_binding;
    SelectionFrame selection{42, false}; selection_frame = &selection;
    runtime::observe_selection(&selection, current.widget, 42, false, lease);
    selection.after = true; runtime::observe_selection(&selection, current.widget, 42, true, lease); selection_frame = nullptr;
    require(s.work_revision == revision, "semantic policy changes do not discard a worklist containing both variants");
    unsigned quanta = 0;
    while (!s.themes[1].available && !s.compromised.load()) {
        require(++quanta < 10000, "shared-subject candidate makes bounded durable progress");
        sample(true, false); deliver(lease);
        require(s.themes[1].work.revision == revision && s.themes[1].work.capture >= capture_cursor
            && s.themes[1].work.resolve_binding == resolved_cursor && lookups == 8,
            "yield resumes the same dependency/physical worklist without repeating resolved lookups or rewinding cursors");
        capture_cursor = s.themes[1].work.capture;
    }
    require(s.themes[1].available && s.applied == 1 && !s.themes[2].available && !s.themes[2].work.revision,
        "first complete requested candidate commits without preparing its peer");
    sample(true, false); sample(true, false, true); deliver(lease); // Alpha -> Original, not the unrequested Beta.
    require(s.applied == 0 && s.desired == SIZE_MAX && saved == "original", "latest explicit Original wins over unfinished peer work");
    slow_capture = false; finish_preparation(lease);
    require(lookups == 8 && !s.themes[2].available && s.applied == 0 && saved == "original" && !s.work_pending,
        "explicit Original stops preparation; inactive peer cannot override it");
    prepare_fixture_candidates(lease); // Explicit peer admission sets up the following warm-resource comparison.
    const auto uploads = creates; auto prior_lookups = lookups; const auto prior_captures = captures;
    close(lease); require(s.subjects.empty(), "owner retirement evicts all copied current-generation Subjects");
    ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(lookups == prior_lookups + 3 && captures == prior_captures + 7 && creates == uploads
        && std::all_of(s.subjects.begin(), s.subjects.end(), [&](const auto& entry) { return entry.session.generation == current.generation; }),
        "warm classification hints perform one fresh lookup per widget and capture new affinities, without reupload or old-generation Subjects");
    close(lease); prior_lookups = lookups;
    for (auto& p : s.palettes) if (p.leaf == L"Label" && p.selector.color == game::menu_theme_plan::Color::Primary) {
        p.kind = native::SubjectKind::CanvasPanel; p.kind_known = true;
    }
    ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(lookups == prior_lookups + 9 && s.themes[1].available && s.themes[2].available,
        "wrong compatible hint is tried through real class validation then falls back to actual Text; it cannot force classification");
    close(lease);

    // Replacement between resolution and owning work must reject, not recapture a possibly styled endpoint.
    reset(); empty_inventory(); bytes = 0;
    auto panel = images("alpha", {{"MusicList/Cell", 7}});
    panel.colors["MusicList/Label.ColorAndOpacity"] = {180, 80, 40, 255};
    auto peer = panel; peer.id = "beta"; add(std::move(panel), bytes); add(std::move(peer), bytes);
    generic_enabled = true; widget_kinds[L"Cell"] = native::SubjectKind::Image; widget_kinds[L"Label"] = native::SubjectKind::TextBlock;
    enable_palette(); slow_capture = true; replace_label_after_capture=true; open(lease, false); key(lease);
    require(captures == 1 && !color_sources.size(), "replacement occurs inside actual owning capture after descriptor resolution, before palette registration");
    const auto before_replacement = lookups;slow_capture = false; finish_preparation(lease);
    require(!s.themes[1].available && !s.themes[2].available && captures == 1 && !applies && !saves && !s.compromised.load()
        && lookups == before_replacement && std::none_of(s.subjects.begin(), s.subjects.end(), [](const auto& entry) { return entry.leaf == L"Label"; }),
        "replaced current Subject evicts cache and rejects dependent candidates without new baseline capture or partial selection");

    reset(); empty_inventory(); bytes = 0; add(images("alpha", {{"MusicList/Cell", 3}}), bytes);
    generic_enabled = true; widget_kinds[L"Cell"] = native::SubjectKind::Image; nested_lookup = true; open(lease);
    require(s.compromised.load() && s.subjects.empty() && !captures && !creates && !applies && !saves,
        "reentrant lookup retires actual preparation before publishing any shared Subject or native resource");

    reset(); empty_inventory(); bytes = 0;
    add(images("alpha", {{"MusicList/Btn.Normal", 3}, {"MusicList/Btn.Hovered", 3}}), bytes);
    generic_enabled = true; widget_kinds[L"Btn"] = native::SubjectKind::Button; reenter_access_at = 2; open(lease);
    require(lookups == 1 && s.compromised.load() && s.subjects.empty() && !captures && !creates && !applies && !saves,
        "reentrant access of a shared copied Subject retires preparation; no cache reference or owning work survives");

    reset(); empty_inventory(); bytes = 0;
    auto row_program = images("alpha", {{"MusicListCell/RowBtn.Normal", 3}, {"MusicListCell/RowBtn.Hovered", 3}});
    row_program.colors["MusicListCell/RowBtn.BackgroundColor"] = {180, 80, 40, 255};
    row_program.colors["MusicListCell/Label.ColorAndOpacity"] = {180, 80, 40, 255};
    add(std::move(row_program), bytes); generic_enabled = true;
    widget_kinds[L"RowBtn"] = native::SubjectKind::Button; widget_kinds[L"Label"] = native::SubjectKind::TextBlock; enable_palette();
    void* row = reinterpret_cast<void*>(0x7e000); row_handles[row] = {91, 191}; active_rows[row] = true;
    current.phase = game::MenuSessionPhase::Opening; setup_song_row(row, 1, 1, lease, [] {});
    current.phase = game::MenuSessionPhase::Ready; open(lease); key(lease);
    const auto old_revision = s.themes[1].work.revision; const auto old_lookups = lookups; const auto old_saves = saves;
    slow_capture = true; setup_song_row(row, 2, 2, lease, [&] { native_row_style(row); }, true, nullptr, true, false);
    require(s.work_revision == old_revision && s.rows[0].work.revision == s.work_revision
        && lookups == old_lookups + 2 && s.applied == 1,
        "real recycled setup preserves unrelated work and replaces its row association; hints still require fresh Button/Text lookup");
    slow_capture = false; finish_preparation(lease);
    require(s.themes[1].available && saves == old_saves && std::all_of(s.rows[0].subjects.begin(), s.rows[0].subjects.end(), [&](const auto& entry) {
        return entry.setup == 2;
    }), "row repair cannot reuse old setup Subjects or persist a maintenance-only delta");
    close(lease);
}
void same_association_refresh_tests(const core::HookCallbackGate::Lease& lease) {
    auto initialize = [&](bool bottom = false) {
        reset(); empty_inventory(); generic_enabled = true;
        widget_kinds = {{L"Cell", native::SubjectKind::Image}, {L"RowBtn", native::SubjectKind::Button}, {L"Label", native::SubjectKind::TextBlock}};
        auto definition = images("alpha", {{"MusicListCell/Cell", 5}, {"MusicListCell/RowBtn.Normal", 7}, {"MusicListCell/RowBtn.Hovered", 8}});
        definition.colors.emplace("MusicListCell/Label.ColorAndOpacity", std::array<std::uint8_t, 4>{70, 130, 210, 255});
        if (bottom) definition.colors.emplace("MusicListCell/Label.BottomColorAndOpacity", std::array<std::uint8_t, 4>{210, 130, 70, 255});
        std::uint64_t bytes = 0; std::string error;
        require(runtime::retain(std::move(definition), bytes, error), "refresh program compiles");
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "refresh reader configures");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
        auto* row = reinterpret_cast<void*>(0x9750); row_handles[row] = {71, 171}; active_rows[row] = true;
        current.phase = game::MenuSessionPhase::Opening;
        setup_song_row(row, 17, 1, lease, [] {});
        current.phase = game::MenuSessionPhase::Ready; open(lease); key(lease);
        require(runtime::state.applied == 1, "refresh fixture actually applies custom program");
        return row;
    };
    auto* row = initialize();
    const auto old_binds = row_binds, old_sources = subject_retires, old_owner_retires = owner_retires;
    const auto old_lookups = lookups, old_creates = creates, old_saves = saves, old_originals = row_originals_called;
    const auto old_capture_batches = capture_batches, old_capture_members = captures;
    const auto old_colors = palette_changes, old_restores = palette_restores[palette::Property::Text];
    auto* context = row_color(row, palette::Property::Text).subject.context;
    const std::array<float, 4> updated{0.2f, 0.4f, 0.6f, 0.35f};
    setup_song_row(row, 17, 2, lease, [&] {
        require(visuals.at(row_leaf(row, "Cell")) == originals.at(row_leaf(row, "Cell"))
            && visuals.at(row_leaf(row, "RowBtn#0")) == originals.at(row_leaf(row, "RowBtn#0")), "same-association original runs on untinted brushes");
        require(row_color(row, palette::Property::Text).policy.normal.enabled
            && palette_changes == old_colors && palette_restores[palette::Property::Text] == old_restores,
            "same-association original retains non-Bottom routing without a restore setter");
        native_row_style(row); // Native original really changes resource/geometry baseline.
        qualified_native_color(row_color(row, palette::Property::Text), updated);
    }, true, nullptr, true, false);
    require(!runtime::state.compromised && runtime::state.applied == 1 && runtime::state.themes[1].available
        && !runtime::state.rows[0].style_pending && runtime::state.rows[0].association_setup == 1
        && row_binds == old_binds && subject_retires == old_sources && owner_retires == old_owner_retires
        && lookups == old_lookups && creates == old_creates && saves == old_saves
        && capture_batches == old_capture_batches + 1 && captures == old_capture_members + 3
        && last_capture_progress.published_members == 2
        && row_originals_called == old_originals + 1
        && row_color(row, palette::Property::Text).subject.context == context
        && visuals.at(row_leaf(row, "Cell")) == 5 && row_color(row, palette::Property::Text).native == updated
        && row_color(row, palette::Property::Text).shown[3] == updated[3]
        && palette_changes == old_colors + 1 && palette_restores[palette::Property::Text] == old_restores,
        "same association repairs synchronously with fresh baseline, retained ticket/contexts and no queue/save/upload");
    std::cout << "same-association non-Bottom restore=" << palette_restores[palette::Property::Text] - old_restores
        << " post-emission=" << palette_changes - old_colors << '\n';
    require(std::any_of(messages.begin(), messages.end(), [](const std::string& m) {
        return m.find("same_association_refreshed") != std::string::npos && m.find("row_source=owned_refresh") != std::string::npos;
    }), "paired refresh outcome records actual source");
    const auto refreshed_target = row_color(row, palette::Property::Text).subject.widget;
    key(lease);
    require(runtime::state.applied == 0 && visuals == originals && color_sources.empty()
        && color_world.at({refreshed_target, palette::Property::Text}) == updated,
        "Original restores changed native baseline, never old live brush or themed snapshot");
    const auto passive_writes = applies, passive_colors = palette_changes;
    setup_song_row(row, 17, 3, lease, [] {}, true, nullptr, true, false);
    require(applies == passive_writes && palette_changes == passive_colors && runtime::state.applied == 0, "same-association Original remains passive");
    close(lease);

    row = initialize(true);
    const auto with_bottom_colors = palette_changes;
    const auto text_changes = palette_changes_by_property[palette::Property::Text];
    const auto bottom_changes = palette_changes_by_property[palette::Property::TextBottom];
    setup_song_row(row, 17, 2, lease, [&] {
        require(row_color(row, palette::Property::Text).policy.normal.enabled
            && !row_color(row, palette::Property::TextBottom).policy.normal.enabled
            && palette_restores[palette::Property::Text] == 0 && palette_restores[palette::Property::TextBottom] == 1,
            "same-association Bottom alone retains the restore-before-original protocol");
        // No setter update: post-refresh emission is still required if setup reset presentation.
        row_color(row, palette::Property::Text).shown = row_color(row, palette::Property::Text).native;
    }, true, nullptr, true, false);
    require(!runtime::state.compromised && palette_changes == with_bottom_colors + 3
        && palette_changes_by_property[palette::Property::Text] == text_changes + 1
        && palette_changes_by_property[palette::Property::TextBottom] == bottom_changes + 2
        && row_color(row, palette::Property::Text).shown != row_color(row, palette::Property::Text).native
        && row_color(row, palette::Property::TextBottom).policy.normal.enabled,
        "unchanged non-Bottom emits once after setup; Bottom still restores and reapplies");
    std::cout << "same-association Bottom setter-rounds=" << palette_changes_by_property[palette::Property::TextBottom] - bottom_changes
        << " restore=" << palette_restores[palette::Property::TextBottom] << '\n';
    close(lease);

    row = initialize();
    const auto before_failure = palette_change_attempts, before_failure_save = saves;
    const auto* failure_source = &row_color(row, palette::Property::Text);
    fail_palette_change_at = before_failure + 1;
    setup_song_row(row, 17, 2, lease, [] {}, true, nullptr, true, false);
    require(runtime::state.compromised && saves == before_failure_save && palette_change_attempts == before_failure + 2
        && failure_source->policy.normal.enabled,
        "forced unchanged-policy failure rolls back the truthful prior policy, never a fabricated disabled policy");
    std::cout << "same-association forced-emission failure attempts=" << palette_change_attempts - before_failure
        << " prior-policy-restored=" << failure_source->policy.normal.enabled << '\n';

    row = initialize();
    const auto before_reentry = palette_change_attempts, before_reentry_save = saves;
    setup_song_row(row, 17, 2, lease, [&] { reenter_palette_change = true; }, true, nullptr, true, false);
    require(runtime::state.compromised && saves == before_reentry_save && palette_change_attempts == before_reentry + 1,
        "reentry during forced emission retires authority without stale palette rollback or persistence");
    std::cout << "same-association reentry attempts=" << palette_change_attempts - before_reentry << " stale-rollback=0\n";

    row = initialize(); const auto prior_bind = row_binds;
    setup_song_row(row, 42, 2, lease, [&] { native_row_style(row); }, true);
    require(!runtime::state.compromised && row_binds == prior_bind + 1 && runtime::state.rows[0].association_setup == 2
        && runtime::state.rows[0].index == 42, "logical recycling uses genuine new association");
    close(lease);

    for (const int drift : {0, 1, 2, 3}) {
        row = initialize(); const auto before_save = saves;
        setup_song_row(row, 17, 2, lease, [&] {
            if (drift == 0) ++row_handles[row].serial_number;
            else if (drift == 1) ++row_trees[row];
            else if (drift == 2) widget_kinds[L"Cell"] = native::SubjectKind::TiledImage;
            else active_rows[row] = false;
        }, true, nullptr, true, false);
        require(runtime::state.compromised && saves == before_save && visuals == originals,
            "post-original identity/tree/class/membership drift stops without stale styling or persistence");
    }
    row = initialize(); const auto before_nested_save = saves;
    setup_song_row(row, 17, 2, lease, [&] { setup_song_row(row, 42, 3, lease, [] {}, true, nullptr, true, false); }, true, nullptr, true, false);
    require(runtime::state.compromised && saves == before_nested_save, "nested setup retires parent refresh without stale repair");
    row = initialize(); const auto before_unwind = saves;
    setup_song_row(row, 17, 2, lease, [] {}, true, nullptr, false, false);
    require(runtime::state.compromised && saves == before_unwind, "missing post-original callback revokes retained refresh");
}
void capture_member_runtime_tests(const core::HookCallbackGate::Lease& lease) {
    const auto initialize = [&] {
        reset(); empty_inventory(); generic_enabled = true;
        widget_kinds[L"Btn"] = native::SubjectKind::Button;
        widget_kinds[L"Set"] = native::SubjectKind::ImageSet;
        std::uint64_t bytes = 0;
        add(images("alpha", {{"MusicList/Btn.Normal", 3}, {"MusicList/Btn.Hovered", 4},
            {"MusicList/Set.0", 5}, {"MusicList/Set.1", 6}}), bytes);
        add(images("beta", {{"MusicList/Btn.Normal", 3}, {"MusicList/Btn.Pressed", 7}}), bytes);
    };
    initialize(); open(lease, false);
    require(!capture_api_calls && !captures && runtime::state.themes[0].available,
        "Original remains immediate before any grouped capture");
    key(lease); finish_preparation(lease);
    auto facts = runtime::state.preparation_facts;
    require(capture_batches == 2 && capture_api_calls == 2 && captures == 4
        && facts.capture_calls == 2 && facts.capture_batch_calls == 2 && facts.capture_requested_members == 4
        && facts.batch_prepared_members == 4 && facts.batch_created_members == 4 && facts.batch_published_members == 4
        && runtime::state.themes[1].available && !runtime::state.themes[2].available
        && saved == "alpha" && saves == 1,
        "one API call per same-target missing member group, no repeated capture of a shared member, whole deferred choice saved once");
    key(lease); finish_preparation(lease); key(lease); // Request Beta, then Original.
    const auto uploads = creates, batches = capture_batches;
    require(runtime::state.applied == 0 && visuals == originals, "batched originals restore exact member baselines");
    close(lease); ++current.generation; current.phase = game::MenuSessionPhase::Ready;
    open(lease); facts = runtime::state.preparation_facts;
    require(capture_batches == batches + 3 && creates == uploads && facts.capture_calls == 3
        && facts.batch_prepared_members == 5 && facts.batch_created_members == 0 && facts.batch_published_members == 5,
        "warm immutable hits still use fresh current-generation member capture affinities without new holders or uploads");
    close(lease);

    // One member is a warm hit, the other really changes under native Original.
    initialize(); open(lease); close(lease);
    originals["Btn#0"] += 20; visuals["Btn#0"] = originals["Btn#0"];
    ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease);
    facts = runtime::state.preparation_facts;
    require(facts.batch_prepared_members == 5 && facts.batch_created_members == 1 && facts.batch_published_members == 5
        && runtime::state.themes[1].available, "partial warm-cache miss publishes the complete set with one new baseline holder");
    key(lease); key(lease); key(lease);
    require(runtime::state.applied == 0 && visuals == originals && visuals.at("Btn#0") == originals.at("Btn#0"),
        "Original uses freshly captured changed member, never its merely-live old original");
    close(lease);

    for (const bool fault : {false, true}) {
        initialize(); fail_capture_member = 2; fail_capture_fault = fault;
        open(lease);
        facts = runtime::state.preparation_facts;
        require(!last_capture_progress.published_members && facts.batch_published_members == 0
            && facts.batch_prepared_members >= 1 && facts.batch_created_members >= 1
            && !runtime::state.themes[1].available && !applies && !saves && visuals == originals,
            "partial capture failure retains charged completed holders but no usable outputs, UI changes or partial persistence");
        for (const auto& b : runtime::state.bindings) require(!valid_brush(b.original), "failed capture outputs remain empty in runtime");
        require(fault ? native_poisoned : !runtime::state.compromised.load(),
            "capture NativeFault poisons backend; ordinary resource rejection leaves Original usable");
        close(lease);
    }
    initialize(); retire_capture_member = 2; open(lease);
    const auto stopped = capture_api_calls;
    require(runtime::state.compromised.load() && !last_capture_progress.published_members
        && !runtime::state.preparation_facts.batch_published_members && !creates && !applies && !saves,
        "retirement during miss rejects all outputs and publishes no original or composed resource");
    sample(true, false); sample(true, true); deliver(lease); close(lease);
    require(capture_api_calls == stopped && !applies && !saves, "lost capture authority never retries, publishes or restores stale state");
}
void batch_runtime_tests(const core::HookCallbackGate::Lease& lease) {
    void* a = reinterpret_cast<void*>(0x7a000); void* b = reinterpret_cast<void*>(0x7b000);
    const auto initialize = [&] {
        reset(); empty_inventory(); std::uint64_t bytes = 0;
        auto alpha = images("alpha", {{"MusicList/Btn.Normal", 1}, {"MusicList/Btn.Hovered", 2},
            {"MusicList/Set.0", 3}, {"MusicList/Set.1", 4},
            {"MusicListCell/RowBtn.Normal", 5}, {"MusicListCell/RowBtn.Hovered", 6}});
        alpha.colors["MusicListCell/Label.ColorAndOpacity"] = {200, 80, 40, 255};
        alpha.colors["MusicListCell/Panel.ContentColorAndOpacity"] = {40, 160, 200, 255};
        auto beta = alpha; beta.id = "beta";
        for (auto& [name, pixels] : beta.images) { (void)name; pixels.bgra[0] += 10; }
        add(std::move(alpha), bytes); add(std::move(beta), bytes);
        generic_enabled = true;
        widget_kinds[L"Btn"] = widget_kinds[L"RowBtn"] = native::SubjectKind::Button;
        widget_kinds[L"Set"] = native::SubjectKind::ImageSet;
        widget_kinds[L"Label"] = native::SubjectKind::TextBlock;
        widget_kinds[L"Panel"] = native::SubjectKind::CanvasPanel;
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
            {nullptr, runtime::read_palette_stack})), "batch reader configures");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
        row_handles[a] = {81, 181}; row_handles[b] = {82, 182}; active_rows[a] = active_rows[b] = true;
        current.phase = game::MenuSessionPhase::Opening;
        setup_song_row(a, 1, 1, lease, [] {}); setup_song_row(b, 2, 2, lease, [] {});
        current.phase = game::MenuSessionPhase::Ready; open(lease);
        originals["Btn#2"] = visuals["Btn#2"] = 91; originals["Set#2"] = visuals["Set#2"] = 92;
    };
    initialize(); key(lease);
    require(batch_calls == 4 && button_publications == 3 && image_synchronizes == 1 && saved == "alpha"
        && visuals.at("Btn#2") == 91 && visuals.at("Set#2") == 92,
        "distinct current subjects batch independently: one Button style or ImageSet sync, no unrelated member writes");
    const auto peer = row_color(b, palette::Property::Text).shown;
    const auto scans = filtered_retire_calls, visits = retirement_visits;
    const auto source_count = color_sources.size(); const auto batches = batch_calls;
    setup_song_row(a, 3, 3, lease, [&] { native_row_style(a); });
    require(filtered_retire_calls == scans + 1 && retirement_visits == visits + source_count
        && batch_calls >= batches + 2 && row_color(b, palette::Property::Text).shown == peer && saves == 1,
        "row restore/reapply batches its own members and retires both properties in one metadata-filtered storage pass");
    key(lease); finish_preparation(lease); require(saved == "beta", "complete grouped choice persists only its actual ID");
    key(lease); const auto original_batches = batch_calls; const auto original_colors = palette_changes;
    close(lease);
    require(visuals == originals && batch_calls == original_batches && palette_changes == original_colors,
        "Original restores every changed member and subsequent close performs no redundant UI writes");
    initialize(); key(lease); const auto close_batches = batch_calls; const auto close_colors = palette_changes;
    close(lease);
    require(batch_calls == close_batches + 4 && palette_changes == close_colors && owner_retires == 1
        && filtered_retire_calls == 0 && visuals == originals && color_sources.empty(),
        "custom close batches four current subjects and retires palette owner once without duplicate color restoration");
    for (const bool refresh : {false, true}) {
        initialize(); key(lease); const auto prior = visuals; const auto prior_colors = color_world;
        const auto writes = saves; const auto group_before = batch_calls;
        if (refresh) fail_batch_refresh = true; else fail_batch_member = 2;
        key(lease);
        require(native_poisoned && runtime::state.compromised.load() && visuals != prior
            && color_world == prior_colors && saves == writes
            && batch_calls == group_before + (refresh ? 2u : 3u),
            "native-fault latch rejects rollback: runtime compromises without palette writes or persistence");
        const auto stopped = visuals; const auto native_writes = applies; const auto colors = palette_changes;
        const auto stopped_batches = batch_calls;
        require(!native::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID) && native_poisoned,
            "configure cannot clear irreversible native-fault latch");
        key(lease);
        close(lease);
        require(visuals == stopped && applies == native_writes && palette_changes == colors
            && batch_calls == stopped_batches && saves == writes,
            "later input/close never attempts stale restoration or another mutation after native fault");
    }
    initialize(); key(lease); const auto group_before = batch_calls; const auto writes = saves;
    nested_apply = true; key(lease);
    require(runtime::state.compromised.load() && batch_calls == group_before + 1 && saves == writes,
        "retirement inside a grouped UI write stops immediately without stale grouped rollback or persistence");
}
void tint_program_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); empty_inventory(); generic_enabled = generic_tint_enabled = true;
    widget_kinds = {{L"Button", native::SubjectKind::Button}, {L"Text", native::SubjectKind::TextBlock}};
    std::uint64_t bytes = 0;
    auto alpha = images("alpha", {{"MusicList/Button.Normal", 4}});
    alpha.colors["MusicList/Button.Normal"] = {128, 0, 0, 255};
    alpha.colors["MusicList/Button.Hovered"] = {0, 255, 0, 128};
    alpha.colors["MusicList/Text"] = {0, 0, 128, 255}; add(std::move(alpha), bytes);
    add(images("beta", {{"MusicList/Button.Normal", 4}}), bytes);
    auto gamma = images("gamma", {{"MusicList/Button.Normal", 4}});
    gamma.colors["MusicList/Button.Normal"] = {128, 0, 0, 255}; add(std::move(gamma), bytes);
    add(images("delta", {{"MusicList/Button", 4}}), bytes);
    add(images("ambiguous", {{"MusicList/Button", 4}, {"MusicList/Button.Normal", 4}}), bytes);
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})),
        "tint test palette authentication");
    runtime::state.palette_configured = true; runtime::install_palette_hooks(); open(lease);
    require(creates == 1 && composes == 1 && resolve_compose_reuses == 2 && tint_calls == 2 && runtime::state.combinations.size() == 4,
        "resource-plus-tint and original-only tint share immutable base/cache accounting");
    key(lease); require(saved == "alpha" && color_sources.begin()->second.policy.normal.linear == game::menu_theme_plan::linear_color({0, 0, 128, 255})
        && color_sources.begin()->second.shown[3] == color_sources.begin()->second.native[3],
        "tint selectors do not misindex other palette colors and whole program persists");
    const auto charges = retained_bytes;
    const auto native_tints = tint_calls;
    key(lease); require(saved == "beta", "omitted tints restore exact originals or untinted resource composition");
    key(lease); require(saved == "gamma" && tint_calls == native_tints && retained_bytes == charges,
        "posted switching reuses tinted cache without owning construction/upload");
    key(lease); require(saved == "delta" && runtime::state.applied == 4 && !runtime::state.themes[5].available,
        "physical Primary/Normal aliases transition once; duplicate physical overrides reject whole peer");
    close(lease); require(runtime::uninstall_palette_hooks(), "tint/palette close retirement");
    reset(); empty_inventory(); generic_enabled = true; widget_kinds = {{L"Button", native::SubjectKind::Button}}; bytes = 0;
    auto unsupported = images("alpha", {{"MusicList/Button.Normal", 4}});
    unsupported.colors["MusicList/Button.Normal"] = {255, 0, 0, 255}; add(std::move(unsupported), bytes);
    add(images("beta", {{"MusicList/Button.Normal", 4}}), bytes); open(lease); key(lease);
    require(!tint_calls && !runtime::state.themes[1].available && runtime::state.themes[2].available && saved == "beta",
        "missing authenticated tint capability excludes whole candidate but preserves image peer");
    close(lease);
}
void check_choice_projection();
void pending_semantic_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); empty_inventory(); generic_enabled = true;
    widget_kinds = {{L"Cell", native::SubjectKind::Image}, {L"Label", native::SubjectKind::TextBlock}};
    auto theme = images("states", {{"MusicListCell/Cell", 4}});
    theme.colors["MusicListCell/Label.ColorAndOpacity@normal"] = {20, 40, 60, 255};
    theme.colors["MusicListCell/Label.ColorAndOpacity@selected"] = {200, 100, 80, 255};
    std::uint64_t bytes = 0; std::string error;
    require(runtime::retain(std::move(theme), bytes, error), "pending semantic program compiled");
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
        {nullptr, runtime::read_palette_stack})), "pending palette callback configured");
    runtime::state.palette_configured = true; runtime::install_palette_hooks();
    auto* a = reinterpret_cast<void*>(0xb100); auto* b = reinterpret_cast<void*>(0xb200);
    current.phase = game::MenuSessionPhase::Opening;
    row_handles[a] = {71, 9}; row_handles[b] = {72, 9}; active_rows[a] = active_rows[b] = true;
    setup_song_row(a, 17, 1, lease, [] {}); setup_song_row(b, 42, 2, lease, [] {});
    current.phase = game::MenuSessionPhase::Ready; native_selected = 17; open(lease, false);
    prepare_fixture_candidates(lease); // Genuine passive inventory, not a time-sliced normal request.
    require(captures==2 && !applies && !saves,"explicit fixture admission leaves Original visible until an actual gesture");
    SelectionFrame selection{42, false}; selection_frame = &selection;
    runtime::observe_selection(&selection, current.widget, 42, false, lease);
    ++selection_originals; // The actual owned original changes native animation before any palette binding.
    qualified_native_color(row_color(a,palette::Property::Text),{0.1f,0.2f,0.3f,0.8f});
    qualified_native_color(row_color(b,palette::Property::Text),{0.1f,0.2f,0.3f,0.35f});
    selection.after = true; runtime::observe_selection(&selection, current.widget, 42, true, lease); selection_frame = nullptr;
    require(runtime::state.semantic_selected == 42 && !applies && !saves,
        "Original records the actual selection event without assigning a preparing custom policy");
    key(lease);
    check_choice_projection();
    require(runtime::state.applied == 1 && row_color(b, palette::Property::Text).native[3] == 0.35f,
        "requested candidate binds latest genuine native animation before its first complete publication");
    require(runtime::state.applied == 1 && !row_color(a, palette::Property::Text).policy.is_selected
        && row_color(b, palette::Property::Text).policy.is_selected
        && row_color(b, palette::Property::Text).shown[3] == 0.35f && saves == 1,
        "first custom application uses latest semantic selection, not Ready scalar or captured RGB");
    const auto factories = captures + creates + composes + tint_calls + lookups;
    const auto old_changes = palette_changes; const auto old_brush_writes = applies; const auto old_borrows = borrows;
    select_song(17, lease, [&] { qualified_native_color(row_color(a, palette::Property::Text), {0.2f, 0.3f, 0.4f, 0.6f}); });
    require(row_color(a, palette::Property::Text).policy.is_selected
        && !row_color(b, palette::Property::Text).policy.is_selected
        && factories == captures + creates + composes + tint_calls + lookups && saves == 1
        && palette_changes == old_changes + 2 && applies == old_brush_writes && borrows == old_borrows,
        "active semantic animation changes policies without rebuilding copies or saving a theme");
    slow_capture = true;
    const auto old_b_visual = visuals.at(row_leaf(b, "Cell"));
    const auto timing_begin = messages.size();
    setup_song_row(a, 99, 3, lease, [&] { native_row_style(a); work_clock += 100; }, false, nullptr, true, false);
    for (auto i = timing_begin; i < messages.size(); ++i) if (messages[i].find("stage=row_association") != std::string::npos) {
        const auto field = messages[i].find("elapsed_ms=");
        require(field != std::string::npos && std::stoull(messages[i].substr(field + 11)) < 100,
            "row pre/post own-work timings exclude the simulated 100ms native setup original");
    }
    require(runtime::state.applied == 1 && runtime::state.desired == SIZE_MAX && !runtime::state.work_pending
        && row_color(b, palette::Property::Text).policy.normal.enabled
        && visuals.at(row_leaf(b, "Cell")) == old_b_visual && saves == 1,
        "the actual row completes before return without touching its styled peer or chosen ID");
    selection.index = 42; selection.after = false; selection_frame = &selection;
    runtime::observe_selection(&selection, current.widget, 42, false, lease); ++selection_originals;
    selection.after = true; runtime::observe_selection(&selection, current.widget, 42, true, lease); selection_frame = nullptr;
    slow_capture = false; runtime::focus_lost();
    std::size_t remaining = 10000;
    while (runtime::state.work_pending && !runtime::state.compromised.load()) {
        require(remaining-- != 0, "unfocused maintenance must terminate or explicitly reject");
        sample(false, false); deliver(lease);
    }
    require(runtime::state.applied == 1 && !row_color(a, palette::Property::Text).policy.is_selected
        && row_color(b, palette::Property::Text).policy.is_selected && saves == 1,
        "unfocused row maintenance preserves styling and applies the latest semantic delta without saving");
    slow_capture = true;
    // Queue a distinct already-qualified contribution explicitly in this orchestration fixture.
    // The callback for a must not drain b simply because b sorts first in a work queue.
    const auto b_index=static_cast<std::size_t>(std::find_if(runtime::state.rows.begin(),runtime::state.rows.end(),
        [&](const auto& r){return r.row==b;})-runtime::state.rows.begin());
    runtime::state.rows[b_index].style_pending=true;runtime::state.rows[b_index].prepared_choice=SIZE_MAX;
    require(runtime::invalidate_song_row(b_index),"pending peer fixture allocation");
    setup_song_row(a, 100, 4, lease, [&] { native_row_style(a); }, false, nullptr, true, false);
    require(!runtime::state.rows[0].style_pending && runtime::state.rows[b_index].style_pending,
        "actual row callback does not repair unrelated queued peer");
    setup_song_row(b, 101, 5, lease, [&] { native_row_style(b); }, false, nullptr, true, false);
    slow_capture = false; slow_palette_bind = true;
    const auto repaired = [&](void* pointer) {
        const auto row = std::find_if(runtime::state.rows.begin(), runtime::state.rows.end(),
            [&](const auto& r) { return r.row == pointer; });
        return row != runtime::state.rows.end() && row->available && !row->style_pending;
    };
    remaining = 10000;
    while (!repaired(a) && !runtime::state.compromised.load()) {
        require(remaining-- != 0, "interleaved row maintenance must make durable progress");
        sample(true, false); deliver(lease);
    }
    require(repaired(a) && repaired(b) && runtime::state.themes[1].available && runtime::state.applied == 1
        && visuals.at(row_leaf(a, "Cell")) == 4 && row_color(a, palette::Property::Text).policy.normal.enabled
        && saves == 1,
        "each real row callback repairs exactly its contribution; only the last restores complete committed admission");
    std::map<decltype(color_world)::key_type, std::array<float, 4>> genuine_world;
    for (const auto& [target, source] : color_sources) genuine_world[target] = source.native;
    key(lease);
    require(!runtime::state.applied && color_sources.empty() && saved == "original",
        "custom to Original restores the latest untinted native animation, not historical selection colors");
    slow_palette_bind = false; finish_preparation(lease);
    require(!runtime::state.applied && runtime::state.desired == SIZE_MAX && visuals == originals && saved == "original" && saves == 2,
        "Original selected during interleaved repair cancels older maintenance and cannot be overwritten by late preparation");
    for (const auto& [target, native] : genuine_world)
        require(color_world.at(target) == native,
            "Original leaves exact captured native row baselines, never a partially repaired custom snapshot");
    const auto passive_changes = palette_changes; const auto passive_brushes = applies; const auto passive_borrows = borrows;
    select_song(99, lease, [] {});
    require(palette_changes == passive_changes && applies == passive_brushes && borrows == passive_borrows,
        "Original selection is passive: no theme setters, borrowed brushes, or redundant restoration");
    close(lease);
    require(palette_changes == passive_changes && applies == passive_brushes,
        "Original close does not rewrite already-native subjects");
    reset(); open(lease); key(lease);
    const auto uploads = creates; const auto close_borrows = borrows; const auto close_applies = applies;
    close(lease);
    require(borrows == close_borrows && applies == close_applies + 1,
        "close restores the changed brush through its guarded owning apply without redundant all-source borrow preflights");
    ++current.generation; current.phase = game::MenuSessionPhase::Ready;
    const auto before_warm = applies; open(lease, false);
    require(runtime::state.applied == 1 && applies == before_warm + 1 && creates == uploads,
        "warm saved theme uses a qualified bounded Ready slice and immutable resources without waiting for a poll");
    const auto after_warm = applies; finish_preparation(lease);
    require(applies == after_warm && creates == uploads, "remaining inventory preparation cannot reapply the warm theme");
    close(lease);
    reset(); empty_inventory(); std::uint64_t palette_bytes = 0; std::string palette_error;
    core::ThemeDefinition first; first.id = "alpha"; first.colors["MusicList/Label.ColorAndOpacity"] = {200, 80, 40, 255};
    auto second = first; second.id = "beta";
    require(runtime::retain(std::move(first), palette_bytes, palette_error)
        && runtime::retain(std::move(second), palette_bytes, palette_error), "identical-policy transition fixture compiles");
    generic_enabled = true; widget_kinds[L"Label"] = native::SubjectKind::TextBlock;
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "palette transition reader configures");
    runtime::state.palette_configured = true; runtime::install_palette_hooks(); open(lease); key(lease);
    const auto valid_before = subject_validations; const auto writes_before = palette_changes; const auto saved_before = saves;
    widget_kinds[L"Label"] = native::SubjectKind::CanvasPanel;
    key(lease);
    require(subject_validations > valid_before && palette_changes == writes_before && saves == saved_before
        && runtime::state.applied == 1 && !runtime::state.themes[2].available,
        "an identical cached palette is still subject-qualified before a different program can claim complete application");
}
// Contract counterexample, not an Original-correctness qualification: a full-color
// setter carrying RGB read from the themed widget is indistinguishable here from
// a genuine native RGB edit. The palette boundary must supply untinted channels.
void panel_feedback_contract_tests(const core::HookCallbackGate::Lease& lease) {
    for (const bool feedback : {false, true}) {
        reset(); empty_inventory(); std::uint64_t bytes = 0; std::string error;
        core::ThemeDefinition theme; theme.id = "ocean";
        theme.colors["MusicListCell/Pnl_Light.ContentColorAndOpacity"] = {255, 214, 70, 255};
        require(runtime::retain(std::move(theme), bytes, error), "panel feedback fixture compiles");
        generic_enabled = true; widget_kinds[L"Pnl_Light"] = native::SubjectKind::CanvasPanel;
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
            {nullptr, runtime::read_palette_stack})), "panel feedback reader configures");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
        void* row = reinterpret_cast<void*>(0x6a000);
        row_handles[row] = {71, 171}; active_rows[row] = true;
        current.phase = game::MenuSessionPhase::Opening;
        setup_song_row(row, 7, 1, lease, [] {});
        current.phase = game::MenuSessionPhase::Ready; open(lease);
        auto& source = row_color(row, palette::Property::PanelContent);
        const std::array<float, 4> white{1, 1, 1, 0};
        qualified_native_color(source, white);
        key(lease);
        require(source.native == white && source.shown[1] != 1, "custom policy initially retains the white untinted panel source");
        auto input = feedback ? source.shown : white; input[3] = 1;
        const auto target = std::pair{source.subject.widget, source.subject.property};
        const auto owning_before = captures + creates + composes + tint_calls + lookups;
        qualified_native_color(source, input);
        key(lease);
        require(!runtime::state.applied && color_sources.empty()
            && owning_before == captures + creates + composes + tint_calls + lookups,
            "Original restores and retires panel routing without recapture or composition");
        require(color_world.at(target) == input,
            "Original delegates the latest native source; the full-setter seam, not runtime policy selection, determines its RGB");
        require(feedback ? color_world.at(target)[1] != white[1] : color_world.at(target)[1] == white[1],
            "counterexample isolates feedback contamination from white-RGB alpha animation without forcing a stock color");
        close(lease);
    }
}
void preview_program_tests(const core::HookCallbackGate::Lease& lease) {
    namespace plan = game::menu_theme_plan;
    const plan::PreviewPalette defaults;
    const auto published = [] { return game::capture_song_preview_palette(); };
    const auto next_gamepad = [&] {
        sample(true, false); sample(true, false, false, true); deliver(lease);
    };
    reset(); empty_inventory(); runtime::state.key = 0;
    core::ThemeDefinition alpha; alpha.id = "alpha"; alpha.colors["Preview/Background"] = {200, 10, 20, 128};
    core::ThemeDefinition beta; beta.id = "beta"; beta.colors["Preview/Accent"] = {1, 240, 3, 200}; beta.colors["Preview/Text"] = {3, 4, 250, 128};
    std::uint64_t bytes = 0; std::string error;
    require(runtime::retain(std::move(alpha), bytes, error) && runtime::retain(std::move(beta), bytes, error), "Preview-only compiled programs retained");
    runtime::state.selected = "alpha";
    require(!published().generation && published().palette == defaults, "loaded selection never publishes before real owner/commit");
    open(lease, false);
    require(published().active && published().generation == current.generation
        && published().palette == runtime::state.themes[1].program.preview && !applies && saves==1 && !queued_ticket,
        "Ready commits persisted Preview before return without a preparation message");
    require(runtime::state.applied == 1 && published().palette == runtime::state.themes[1].program.preview
        && saved == "alpha" && saves == 1 && runtime::state.desired == SIZE_MAX,
         "persisted Preview-only program completes immediately, without a readiness wait or native work");
    finish_preparation(lease);
    require(runtime::state.applied == 1 && published().palette == runtime::state.themes[1].program.preview
        && published().palette.linear[1] == defaults.linear[1] && saved == "alpha" && saves == 1
        && !subject_prepares && !row_binds && !speed_binds && !lookups && !captures && !creates && !applies && !palette_changes,
        "whole Preview-only choice commits/saves via genuine Ready authority without invented native subjects or Canvas mutation");
    const auto alpha_snapshot = published();
    runtime::focus_lost();
    require(published().palette == alpha_snapshot.palette, "focus loss cancels gestures, not committed Preview style");
    fail_save = true; next_gamepad();
    require(runtime::state.applied == 2 && published().palette == runtime::state.themes[2].program.preview
        && published().palette.linear[0] == defaults.linear[0] && saved == "alpha" && saves == 2,
        "next Preview palette resets missing roles independently; failed persistence does not misreport actual committed style");
    fail_save = false; next_gamepad();
    require(!runtime::state.applied && published().palette == defaults && saved == "original", "Original restores exact badge defaults");
    next_gamepad();
    sample(true, false); sample(true, false, false, true); const auto stale = queued_ticket;
    const auto old_generation = current.generation;
    close(lease);
    plan::PreviewPalette fading;
    require(!published().active && !game::preview_badge_palette(published(), old_generation, fading), "close suppresses old shown selection even during fade");
    ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease, false);
    const auto new_snapshot = published(); const auto before_saves = saves;
    deliver(lease, stale);
    require(published().generation == current.generation && published().palette == new_snapshot.palette && saves == before_saves
        && !game::preview_badge_palette(published(), old_generation, fading), "stale queued event cannot publish onto new menu/fading old selection");
    runtime::retire();
    require(!published().active, "terminal retirement revokes value publication without native cleanup");

    reset(); empty_inventory(); bytes = 0;
    core::ThemeDefinition only; only.id = "alpha"; only.colors["Preview/Border"] = {220, 30, 50, 255};
    auto mixed = images("beta", {{"MusicList/Bg_1", 4}}); mixed.colors["Preview/Background"] = {10, 20, 30, 255};
    require(runtime::retain(std::move(only), bytes, error) && runtime::retain(std::move(mixed), bytes, error), "mixed native+Preview inventory");
    open(lease); key(lease); const auto committed = published(); const auto saved_count = saves;
    fail_native = true; key(lease);
    require(runtime::state.applied == 1 && runtime::state.themes[2].rejected && published().palette == committed.palette
        && saves == saved_count, "failed native whole-program transition never publishes its Preview subset");
    fail_native = false; key(lease);
    require(!runtime::state.applied && published().palette == defaults, "Original remains available after mixed-program rejection");

    reset(); empty_inventory(); bytes = 0;
    auto missing = images("alpha", {{"MusicList/Bg_1", 4}}); missing.colors["Preview/Accent"] = {100, 20, 30, 255};
    require(runtime::retain(std::move(missing), bytes, error), "unavailable mixed Preview candidate compiled");
    runtime::state.selected = "alpha"; missing_leaf = "Bg_1"; open(lease);
    const auto original_snapshot = published();
    key(lease);
    require(!runtime::state.applied && !runtime::state.themes[1].available && !saves && saved.empty()
        && published().palette == original_snapshot.palette && published().palette == defaults
        && !applies && runtime::state.selected == "alpha" && runtime::state.desired == SIZE_MAX,
        "ready-only no-op Original with rejected saved candidate neither publishes custom subset nor rewrites saved selection");

    reset(); empty_inventory(); runtime::state.themes.clear(); runtime::state.configured = false;
    const auto root = std::filesystem::temp_directory_path() / ("ff7rp-preview-only-" + std::to_string(GetCurrentProcessId()));
    require(std::filesystem::create_directory(root), "create owned Preview-only test directory");
    try {
        std::filesystem::create_directory(root / "preview-only");
        core::Config config; config.theme_function_key = 0; config.selected_theme = "preview-only";
        runtime::configure(fake_module, root, L"fake.ini", config);
        require(bound_input && runtime::state.bindings.empty() && runtime::state.palettes.empty(),
            "worker binds existing optional input route for Preview-only inventory, even keyboard disabled");
        open(lease);
        require(runtime::state.applied == 1 && published().palette.overrides == 4 && saved == "preview-only",
            "worker-prepared Preview-only selection is usable and only actual successful apply publishes/saves");
        std::filesystem::remove_all(root);
    } catch (...) { std::filesystem::remove_all(root); throw; }
}

void cursor_program_tests(const core::HookCallbackGate::Lease& lease) {
    namespace plan = game::menu_theme_plan;
    const auto published = [] { return game::capture_song_preview_palette(); };
    const auto initialize = [&](bool mixed) {
        reset(); empty_inventory(); generic_enabled = generic_tint_enabled = true;
        widget_kinds = {{L"Cursor_Left", native::SubjectKind::Image}, {L"Img_BtnLight", native::SubjectKind::Image},
            {L"HoverBase", native::SubjectKind::Image}};
        auto alpha = images("alpha", {{"Cursor/Cursor_Left", 4}, {"Cursor/Img_BtnLight", 5}, {"Cursor/HoverBase", 6}});
        alpha.colors["Cursor/Img_BtnLight.ColorAndOpacity"] = {90, 130, 230, 180};
        alpha.colors["Cursor/HoverBase.BrushTint"] = {150, 30, 90, 255};
        auto beta = images("beta", {{"Cursor/Cursor_Left", 9}});
        if (mixed) {
            // Same leaf in different receivers must never share lookup/original/palette authority.
            alpha.images["MusicList/Cursor_Left"] = core::ThemeImage{{7, 0, 0, 255}, 1, 1};
            alpha.colors["MusicList/Img_BtnLight.ColorAndOpacity"] = {200, 30, 10, 255};
            alpha.colors["Preview/Accent"] = {50, 160, 230, 255};
            beta.colors["Preview/Text"] = {210, 120, 70, 255};
        }
        std::uint64_t bytes = 0; std::string error;
        require(runtime::retain(std::move(alpha), bytes, error) && runtime::retain(std::move(beta), bytes, error),
            "Cursor whole programs compile through real parser");
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
            {nullptr, runtime::read_palette_stack})), "Cursor palette actual reader configured");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
    };
    const auto glow = []() -> ColorSource& {
        const auto found = std::find_if(color_sources.begin(), color_sources.end(), [](const auto& entry) {
            const auto* binding = static_cast<const runtime::PaletteBinding*>(entry.second.subject.context);
            return binding && binding->selector.scope == plan::Scope::Cursor && binding->leaf == L"Img_BtnLight";
        });
        require(found != color_sources.end(), "current Cursor glow source exists"); return found->second;
    };
    for (const bool mixed : {false, true}) {
        initialize(mixed); open(lease, false);
        const auto defaults = plan::PreviewPalette{};
        require(!runtime::state.applied && published().palette == defaults && !applies && !saves,
            "Cursor intent never publishes before complete preparation");
        prepare_fixture_candidates(lease);
        require(runtime::state.themes[1].available && runtime::state.themes[2].available
            && cursor_lookups == 3 && !row_binds && !speed_binds && !speed_reads
            && runtime::state.bindings.size() == runtime::state.binding_templates
            && runtime::state.combinations.size() == runtime::state.combination_templates,
            "Cursor uses one current provider lookup per leaf, no fabricated row expansion");
        require(std::count_if(runtime::state.subjects.begin(), runtime::state.subjects.end(), [](const auto& s) {
            return s.scope == plan::Scope::Cursor;
        }) == 3, "Cursor and MusicList cache keys remain separate for identical leaves");
        const auto before_native = palette_changes;
        qualified_native_color(glow(), {0.8f, 0.7f, 0.6f, 0.25f});
        require(palette_changes == before_native && glow().shown[3] == 0.25f,
            "passive Cursor preparation retains actual animated native alpha");
        key(lease);
        const auto& alpha = runtime::state.themes[1].program;
        const auto color = std::find_if(alpha.colors.begin(), alpha.colors.end(), [](const auto& c) {
            return c.target.widget == "Img_BtnLight" && c.target.scope == plan::Scope::Cursor;
        });
        runtime::ChoiceLookup lookup; require(lookup.build(1), "Cursor choice projection");
        require(saved == "alpha" && runtime::state.applied == 1
            && visuals.at("cursor/Cursor_Left") == 4 && visuals.at("cursor/Img_BtnLight") == 5
            && brushes.at(brush_token(lookup.image(0))).generation == current.generation
            && color != alpha.colors.end() && glow().shown[0] == color->linear[0]
            && glow().shown[3] == color->linear[3] * 0.25f && published().palette == alpha.preview,
            "current Cursor images, tint, native fade and Preview commit as one whole program");
        if (mixed) require(visuals.at("Cursor_Left") == 7 && glow().subject.widget
            != std::find_if(color_sources.begin(), color_sources.end(), [](const auto& entry) {
                const auto* b = static_cast<const runtime::PaletteBinding*>(entry.second.subject.context);
                return b && b->selector.scope == plan::Scope::MusicList;
            })->second.subject.widget, "same-named main and cursor targets remain distinct physical subjects");
        const auto factories = creates + captures + composes + tint_calls + lookups;
        key(lease);
        require(saved == "beta" && visuals.at("cursor/Cursor_Left") == 9
            && visuals.at("cursor/Img_BtnLight") == originals.at("cursor/Img_BtnLight")
            && glow().shown == glow().native && !glow().policy.normal.enabled
            && published().palette == runtime::state.themes[2].program.preview
            && factories == creates + captures + composes + tint_calls + lookups,
            "omitted Cursor resources/colors restore exact baseline, cached next transition only");
        const auto glow_target = std::pair{glow().subject.widget, glow().subject.property};
        const auto native_glow = glow().native;
        key(lease);
        require(runtime::state.applied == 0 && visuals == originals && color_sources.empty() && color_world.at(glow_target) == native_glow
            && saved == "original" && published().palette == defaults,
            "Original restores all Cursor/main/Preview defaults without stale custom palette");
        const auto uploads = creates; const auto prior_cursor_lookups = cursor_lookups;
        close(lease); ++current.generation; current.phase = game::MenuSessionPhase::Ready;
        ++cursor_provider; open(lease);
        require(creates == uploads && cursor_lookups == prior_cursor_lookups + 3
            && runtime::state.themes[1].available && !runtime::state.compromised.load(),
            "new owner reconstructs real cursor affinities, not old provider authority; immutable pixels reused");
        key(lease); const auto retired_world = visuals; const auto retired_saves = saves;
        runtime::retire(); deliver(lease, ++runtime::state.sequence);
        require(visuals == retired_world && saves == retired_saves && !published().active,
            "retirement never restores a cursor through a stale saved pointer or publishes queued work");
    }
    initialize(true); slow_capture = true; replace_cursor_after_capture=true;open(lease, false);
    key(lease);
    require(captures == 1 && !applies && !saves, "provider replacement inside owning capture rejects before whole publication");
    finish_preparation(lease);
    require(!runtime::state.themes[1].available && !runtime::state.themes[2].available && captures == 1
        && !applies && !saves && published().palette == plan::PreviewPalette{},
        "provider replacement during preparation rejects whole candidates, no new themed-original capture or Preview subset");
    initialize(false); open(lease); key(lease);
    const auto world = visuals; const auto writes = applies; const auto saved_count = saves;
    ++cursor_provider; key(lease); close(lease);
    require(visuals == world && applies == writes && saves == saved_count && runtime::state.compromised.load(),
        "replaced live cursor invalidates owning apply/Original restore, no stale writes or false commit");
    initialize(false); nested_lookup = true; open(lease);
    require(runtime::state.compromised.load() && !captures && !creates && !applies && !saves
        && runtime::state.subjects.empty(), "reentrant Cursor lookup retires real callback, no publication or retry");
    initialize(true); open(lease); const auto defaults = published().palette;
    fail_borrow_leaf = "cursor/Cursor_Left"; key(lease);
    require(!runtime::state.applied && !applies && !saves && runtime::state.themes[1].rejected
        && published().palette == defaults && visuals == originals,
        "failed whole Cursor/native preflight cannot publish Preview or a partial custom theme");
    fail_borrow_leaf.clear(); key(lease);
    require(saved == "beta" && runtime::state.applied == 2 && published().palette == runtime::state.themes[2].program.preview,
        "independent valid Cursor peer survives rejected candidate");
    close(lease);
    reset(); empty_inventory(); generic_enabled = true;
    widget_kinds = {{L"Img_BtnLight", native::SubjectKind::Image}};
    core::ThemeDefinition glow_only; glow_only.id = "glow";
    glow_only.colors["Cursor/Img_BtnLight.ColorAndOpacity"] = {20, 90, 210, 255};
    std::uint64_t bytes = 0; std::string error;
    require(runtime::retain(std::move(glow_only), bytes, error), "Cursor palette-only program compiled");
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
        {nullptr, runtime::read_palette_stack})), "Cursor palette-only reader");
    runtime::state.palette_configured = true; runtime::install_palette_hooks(); open(lease); key(lease);
    require(runtime::state.applied == 1 && saved == "glow" && cursor_lookups == 1 && !captures && !creates
        && glow().policy.normal.enabled && runtime::state.rows.empty(),
        "palette-only Cursor has actual current provider and whole commit without image or row construction");
    qualified_native_color(glow(), {0.2f, 0.4f, 0.7f, 0.1f});
    const auto glow_target = std::pair{glow().subject.widget, glow().subject.property};
    const auto genuine_glow = glow().native; key(lease);
    require(!runtime::state.applied && color_sources.empty() && color_world.at(glow_target) == genuine_glow && saved == "original",
        "Original preserves latest native Cursor RGB/alpha rather than a constant or stale animation snapshot");
    close(lease);
    initialize(false);
    bytes = 0; for (const auto& resource : runtime::state.resources) bytes += resource.pixels.bgra.size();
    auto wrong_member = images("gamma", {{"Cursor/HoverBase.1", 11}});
    require(runtime::retain(std::move(wrong_member), bytes, error), "Cursor indexed candidate compiles before actual class admission");
    auto invalid_state = images("invalid", {{"Cursor/Cursor_Left@selected", 12}});
    require(!runtime::retain(std::move(invalid_state), bytes, error) && runtime::state.themes.size() == 4,
        "Cursor never inherits semantic Song row states");
    open(lease);
    require(runtime::state.themes[1].available && runtime::state.themes[2].available
        && !runtime::state.themes[3].available && !applies && !saves && !runtime::state.compromised.load(),
        "actual Image class rejects indexed Cursor override without disabling valid peers or publishing subset");
    initialize(false); cursor_present = false; open(lease);
    require(!runtime::state.themes[1].available && !runtime::state.themes[2].available && !captures && !applies
        && !saves && runtime::state.themes[0].available && !runtime::state.compromised.load(),
        "missing current provider is an honest candidate failure, not CachedCursor or MusicList fallback");
    initialize(false); generic_enabled = false; open(lease);
    require(!runtime::state.themes[1].available && !cursor_lookups && !captures && !applies && !saves
        && runtime::state.themes[0].available, "missing Cursor backend never falls back to MusicList legacy lookup");
}

void preparation_facts_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); slow_capture = true; open(lease, false);
    require(runtime::state.preparation_facts.generation == current.generation
        && !runtime::state.preparation_facts.dispatches && !captures, "fresh owner facts start without native work");
    work_clock = 100;
    fail_post = true; sample(true, true); const auto failed = runtime::state.pending.ticket;
    work_clock += 5; deliver(lease, failed);
    require(!runtime::state.preparation_facts.dispatches && !captures,
        "failed preparation post is neither delivered service nor backend work");
    fail_post = false; sample(true, false); sample(true, true); const auto cancelled = queued_ticket;
    sample(false, false); sample(true, false, true); const auto gesture = queued_ticket;
    work_clock += 9; deliver(lease, cancelled);
    require(!runtime::state.preparation_facts.dispatches && !captures,
        "superseded queued gesture contributes no dispatch or wait");
    performance_step = 2000; deliver(lease, gesture); performance_step = 64;
    require(!runtime::state.preparation_facts.dispatches, "gesture delivery is not preparation service");
    auto facts = runtime::state.preparation_facts;
    require(!facts.dispatches && !facts.queue_wait_ms && facts.capture_calls == 1
        && facts.capture_backend_ms == 100 && !facts.service_ms && !facts.passive_bind_calls,
        "complete gesture counts actual backend time without inventing posted-preparation service");
    finish_preparation(lease); facts = runtime::state.preparation_facts;
    require(facts.capture_calls == captures && facts.capture_calls == 1
        && facts.capture_backend_ms == 100,
        "shared original is counted once, not each program or validation");
    const auto stale = ++runtime::state.sequence;
    close(lease);
    require(std::any_of(messages.begin(), messages.end(), [&](const std::string& text) {
        return text.find("stage=restore") != std::string::npos
            && text.find("preparation_generation=7") != std::string::npos
            && text.find("capture_original_calls=1") != std::string::npos;
    }), "close logs copied owner facts after clearing live ownership");
    ++current.generation; current.phase = game::MenuSessionPhase::Ready;
    runtime::state.prepared_once = false; runtime::state.selected = "original";
    open(lease, false); deliver(lease, stale);
    require(runtime::state.preparation_facts.generation == current.generation
        && !runtime::state.preparation_facts.dispatches && !runtime::state.preparation_facts.capture_calls,
        "new owner resets totals and rejects foreign-generation delivery");
    runtime::retire();
    require(std::any_of(messages.begin(), messages.end(), [](const std::string& text) {
        return text.find("stage=retire") != std::string::npos && text.find("preparation_generation=8") != std::string::npos;
    }), "retire projects current owner facts before teardown");

    reset(); empty_inventory(); generic_enabled = true;
    widget_kinds = {{L"Label", native::SubjectKind::TextBlock}};
    core::ThemeDefinition theme; theme.id = "palette";
    theme.colors["MusicList/Label.ColorAndOpacity"] = {20, 40, 60, 255};
    theme.colors["MusicList/Label.OutlineColorAndOpacity"] = {40, 60, 80, 255};
    std::uint64_t bytes = 0; std::string error;
    require(runtime::retain(std::move(theme), bytes, error), "measured palette program compiled");
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
        {nullptr, runtime::read_palette_stack})), "measured palette reader configured");
    runtime::state.palette_configured = true; runtime::install_palette_hooks();
    slow_palette_bind = true; open(lease, false);
    performance_step=2000;key(lease);performance_step=64;
    require(color_sources.size()==2 && runtime::state.applied==1,"finite gesture binds and commits both properties before return");
    finish_preparation(lease);
    facts = runtime::state.preparation_facts;
    require(facts.passive_bind_calls == 2 && color_sources.size() == 2
        && facts.passive_bind_backend_ms == 200
        && !facts.service_ms && !facts.capture_calls && palette_changes == 2,
        "passive binds count actual validated calls and deterministic backend duration without extra setters");
    require(std::any_of(messages.begin(), messages.end(), [](const std::string& text) {
        return text.find("stage=program_admission") != std::string::npos
            && text.find("passive_palette_bind_calls=2") != std::string::npos;
    }), "actual readiness projects aggregate preparation facts, not per-call timing spam");
    close(lease);
}
void incremental_preparation_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); slow_capture = true; open(lease, false);
    require(runtime::state.themes[0].available && !runtime::state.work_pending && !runtime::state.applied
        && !creates && !captures && !lookups && !composes && !applies && !saves,
        "Ready publishes Original immediately without lookup, owning capture, upload or assignment");
    sample(true, false); const auto job = ++runtime::state.sequence;
    require(!queued_ticket && !creates, "Original poller posts no unrequested physical preparation");
    sample(true, true); const auto gesture = queued_ticket;
    require(gesture != job && !runtime::state.pending.preparation, "first explicit gesture owns the request, not inactive maintenance");
    deliver(lease, job); require(!creates && !applies, "canceled preparation message has no authority");
    deliver(lease, gesture);
    require(runtime::state.desired == SIZE_MAX && runtime::state.applied==1 && creates==1 && applies==1 && saves==1,
        "actual explicit request finishes its complete program despite expensive helper clocks");
    finish_preparation(lease);
    require(runtime::state.applied == 1 && saved == "night" && saves == 1,
        "one early edge applies and saves the whole ready program without hold or repress");
    reset(); open(lease, false); key(lease);
    require(captures == 1 && creates == 1 && composes == 1 && runtime::state.themes[1].available
        && applies == 1 && saves == 1,
        "a cheap owned callback advances multiple small tasks, not one metadata item per poll or a whole-menu blocking build");
    const auto missing=[&]{
        reset();empty_inventory();generic_enabled=true;widget_kinds={{L"Cell",native::SubjectKind::Image}};
        std::uint64_t bytes=0;add(images("night",{{"MusicListCell/Cell",4}}),bytes);open(lease,false);key(lease);
        require(runtime::state.desired==1 && !runtime::state.work_pending && !captures && !queued_ticket,
            "actual missing receiver defers intent without spin or successor message");
    };
    missing();sample(true, false); sample(true, false, true, false); deliver(lease);
    require(runtime::state.desired == SIZE_MAX && !runtime::state.desired_gesture && !applies && !saves,
        "opposite early direction explicitly cancels to Original");
    missing();
    require(runtime::state.desired == 1 && runtime::state.desired_gesture, "focus cancellation fixture has a genuine deferred input gesture");
    runtime::focus_lost(); sample(true, true); deliver(lease);
    finish_preparation(lease);
    require(runtime::state.applied == 0 && runtime::state.desired == SIZE_MAX && !applies && !saves,
        "focus notification cancels deferred choice, held return never selects it later");
    auto* ready_row=reinterpret_cast<void*>(0xa090);row_handles[ready_row]={490,20};active_rows[ready_row]=true;
    setup_song_row(ready_row,17,1,lease,[]{});key(lease);
    require(saved == "night" && saves == 1, "fresh release/repress selects only complete genuinely ready program");
    reset(); runtime::state.selected = "night"; open(lease, false); runtime::focus_lost();
    std::size_t maintenance_bound = 10000;
    while (runtime::state.work_pending && !runtime::state.compromised.load()) {
        require(maintenance_bound-- != 0, "unfocused remembered preparation terminates or rejects");
        sample(false, false); deliver(lease);
    }
    require(runtime::state.applied == 1 && saved == "night" && !runtime::state.focused.load(),
        "focus loss cancels input, not remembered theme maintenance or visible styling");
    missing();ready_row=reinterpret_cast<void*>(0xa091);row_handles[ready_row]={491,20};active_rows[ready_row]=true;
    setup_song_row(ready_row,18,1,lease,[] {},false,nullptr,true,false);fail_post=true;
    require(!captures && runtime::state.desired==1,"post-failure fixture has genuinely newly ready deferred work");
    sample(true, false); const auto first_failure_logs = messages.size(); sample(true, false);
    require(messages.size() == first_failure_logs && !creates && !applies, "repeated preparation post failure is bounded and quiet");
    fail_post = false; sample(true, false); const auto stale = queued_ticket; close(lease);
    deliver(lease, stale);
    require(!creates && !captures && !applies && !saves && original_calls == 1, "close revokes unfinished jobs and forwards original once");
    ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease, false); deliver(lease, stale);
    require(!creates && !captures && !applies, "old preparation cannot mutate new generation");
    finish_preparation(lease); close(lease);
    reset(); empty_inventory(); generic_enabled = true; fail_speed_bind = true;
    widget_kinds = {{L"MainBg", native::SubjectKind::Image}, {L"SpeedIcon", native::SubjectKind::ImageSet}};
    std::uint64_t bytes = 0; std::string error;
    require(runtime::retain(images("speed", {{"SpeedIcon/SpeedIcon.0", 1}}), bytes, error)
        && runtime::retain(images("main", {{"MusicList/MainBg", 2}}), bytes, error), "speed failure peers compiled");
    for (std::uintptr_t i = 0; i < 2; ++i) {
        auto* row = reinterpret_cast<void*>(0xa100 + i); row_handles[row] = {static_cast<int>(500 + i), 20};
        active_rows[row] = true; speed_members.push_back(row);
    }
    open(lease);
    require(speed_bind_attempts == 2 && !runtime::state.work_pending && !runtime::state.themes[1].available
        && runtime::state.themes[2].available && runtime::state.themes[0].available && !runtime::state.compromised.load(),
        "rejected speed members terminate preparation once each without blocking Original or main peer");
    close(lease);
}
core::ThemeDefinition full_fixture(const char* id, std::uint8_t base) {
    core::ThemeDefinition d; d.id = id;
    const char* images[] = {"MusicList/Bg_1", "MusicList/Bg_2", "MusicList/Bg_Pettern", "MusicList/ListBg",
        "MusicList/Arrow_Left", "MusicList/Arrow_Right", "MusicListCell/EndImage_172", "MusicListCell/EndImage",
        "MusicListCell/Icon_New", "MusicListCell/MainMenu_Btn.Normal", "MusicListCell/MainMenu_Btn.Hovered",
        "MusicListCell/MainMenu_Btn.Pressed", "MusicListCell/MainMenu_Btn.Disabled", "SpeedIcon/SpeedIcon.0",
        "SpeedIcon/SpeedIcon.1", "SpeedIcon/SpeedIcon.2"};
    for (std::size_t i = 0; i < std::size(images); ++i) d.images[images[i]] = {{static_cast<std::uint8_t>(base + i + 1), 2, 3, 255}, 1, 1};
    const char* colors[] = {"MusicList/Txt_Title_1.ColorAndOpacity", "MusicList/Txt_Time.ColorAndOpacity",
        "MusicList/Txt_Volume.ColorAndOpacity", "MusicList/Txt_HighScore.ColorAndOpacity", "MusicList/Txt_TargetScore.ColorAndOpacity",
        "MusicList/Pnl_Info.ContentColorAndOpacity", "MusicList/Deco.ColorAndOpacity", "MusicListCell/Txt_Title.ColorAndOpacity@normal",
        "MusicListCell/Txt_Title.ColorAndOpacity@selected", "MusicListCell/Txt_Title.BottomColorAndOpacity@selected",
        "MusicListCell/Txt_Rank.OutlineColorAndOpacity", "MusicListCell/Txt_Rank.ShadowColorAndOpacity",
        "MusicListCell/Pnl_Light.ContentColorAndOpacity", "MusicListCell/MainMenu_Btn.BackgroundColor",
        "MusicListCell/MainMenu_Btn.Hovered", "SpeedIcon/SpeedIcon.ColorAndOpacity"};
    for (const auto* selector : colors) d.colors[selector] = {static_cast<std::uint8_t>(base + 90), 120, 180, 255};
    return d;
}
void check_choice_projection() {
    auto& s = runtime::state;
    for (std::size_t choice = 0; choice < s.themes.size(); ++choice) {
        runtime::ChoiceLookup lookup; require(lookup.build(choice), "linear choice projection builds");
        const auto visits = lookup.visits;
        for (std::size_t b = 0; b < s.bindings.size(); ++b) if (s.bindings[b].canonical == b) {
            std::size_t expected = SIZE_MAX;
            if (choice) for (const auto t : s.themes[choice].combinations) {
                const auto& c = s.combinations[t];
                if (s.bindings[b].row == SIZE_MAX) {
                    if (runtime::non_row_scope(s.bindings[c.binding].selector.scope) && s.bindings[c.binding].canonical == b) { expected = t; break; }
                } else {
                    const auto row = s.bindings[b].row;
                    const auto semantic = s.rows[row].kind == native::RowKind::Speed ? game::menu_theme_plan::State::Any
                        : s.rows[row].index == s.semantic_selected ? game::menu_theme_plan::State::Selected : game::menu_theme_plan::State::Normal;
                    if (c.variant != semantic && c.variant != game::menu_theme_plan::State::Any) continue;
                    for (std::size_t i = s.combination_templates; i < s.combinations.size(); ++i)
                        if (s.combinations[i].prototype == t && s.combinations[i].row == row && s.bindings[s.combinations[i].binding].canonical == b) { expected = i; break; }
                    if (expected != SIZE_MAX) break;
                }
            }
            require(lookup.combination(b) == expected, "projected brush retains template order, semantic state and canonical aliases");
        }
        for (std::size_t p = 0; p < s.palettes.size(); ++p) if (s.palettes[p].canonical == p) for (bool selected : {false, true}) {
            const game::menu_theme_plan::Palette* expected = nullptr;
            if (choice) for (std::size_t n = 0; n < s.themes[choice].palettes.size(); ++n) {
                const auto t = s.themes[choice].palettes[n];
                const auto& color = s.themes[choice].program.colors[s.themes[choice].palette_colors[n]];
                bool matches = s.palettes[p].row == SIZE_MAX && runtime::non_row_scope(s.palettes[t].selector.scope) && s.palettes[t].canonical == p;
                if (s.palettes[p].row != SIZE_MAX) for (std::size_t i = s.palette_templates; i < s.palettes.size(); ++i)
                    if (s.palettes[i].prototype == t && s.palettes[i].row == s.palettes[p].row && s.palettes[i].canonical == p) { matches = true; break; }
                if (!matches || (color.target.state != game::menu_theme_plan::State::Any
                    && color.target.state != (selected ? game::menu_theme_plan::State::Selected : game::menu_theme_plan::State::Normal))) continue;
                if (!expected || color.target.state != game::menu_theme_plan::State::Any) expected = &color;
            }
            require(lookup.color(choice, p, selected) == expected, "projected palette retains Any/specific precedence and canonical aliases");
        }
        require(lookup.visits == visits, "direct queries perform no inventory scans");
    }
}
void projection_scale_tests(const core::HookCallbackGate::Lease& lease) {
    std::uint64_t previous = 0;
    for (const auto count : {64u, 128u}) {
        reset(); empty_inventory(); auto& s = runtime::state;
        s.themes.resize(2);
        for (std::size_t t = 0; t < 8; ++t) {
            runtime::Binding b; b.selector.scope = game::menu_theme_plan::Scope::MusicListCell; s.bindings.push_back(b);
            runtime::Combination c{}; c.binding = t; s.combinations.push_back(c); s.themes[1].combinations.push_back(t);
            runtime::PaletteBinding p; p.selector.scope = game::menu_theme_plan::Scope::MusicListCell; s.palettes.push_back(p);
            s.themes[1].palettes.push_back(t); s.themes[1].palette_colors.push_back(t); s.themes[1].program.colors.emplace_back();
        }
        s.binding_templates = s.combination_templates = s.palette_templates = 8;
        for (std::size_t r = 0; r < count; ++r) {
            runtime::RowRecord row; row.index = static_cast<int>(r); s.rows.push_back(row);
            for (std::size_t t = 0; t < 8; ++t) {
                auto b = s.bindings[t]; b.row = r; b.prototype = t; b.canonical = s.bindings.size(); s.bindings.push_back(b);
                auto c = s.combinations[t]; c.row = r; c.prototype = t; c.binding = b.canonical; s.combinations.push_back(c);
                auto p = s.palettes[t]; p.row = r; p.prototype = t; p.canonical = s.palettes.size(); s.palettes.push_back(p);
            }
        }
        runtime::ChoiceLookup lookup; require(lookup.build(1), "scale projection builds");
        const auto expected = s.bindings.size() + s.palettes.size() + s.combination_templates + s.palette_templates
            + s.themes[1].combinations.size() + s.themes[1].palettes.size() + s.combinations.size() + s.palettes.size();
        require(lookup.visits == expected && (!previous || lookup.visits < previous * 2), "doubling physical rows doubles projection work, never nested inventory work");
        std::cout << "choice projection rows=" << count << " visits=" << lookup.visits << '\n'; previous = lookup.visits;
        check_choice_projection();
    }
    reset();
    runtime::Scope scope(runtime::Source::Preparation, nullptr, lease, current);
    performance_step = 0;
    for (unsigned i = 0; i < 100; ++i) require(scope.step(), "fast preparation has no arbitrary item quota");
    performance_ticks += 2000;
    require(scope.step() && work_clock == 0, "elapsed clock changes cannot yield semantic preparation");
    performance_ticks -= 100000; require(scope.step(), "backward diagnostic clock cannot stop preparation");
    scope.retired=true;require(!scope.step(),"retirement, not time, revokes a semantic unit");
}
void realistic_inventory_tests(const core::HookCallbackGate::Lease& lease, bool probes = true) {
    reset(); empty_inventory(); generic_enabled = generic_tint_enabled = true;
    for (const auto* name : {L"Bg_1", L"Bg_2", L"ListBg", L"Arrow_Left", L"Arrow_Right", L"EndImage_172", L"EndImage", L"Icon_New"})
        widget_kinds[name] = native::SubjectKind::Image;
    widget_kinds[L"Bg_Pettern"] = native::SubjectKind::TiledImage;
    widget_kinds[L"MainMenu_Btn"] = native::SubjectKind::Button; widget_kinds[L"SpeedIcon"] = native::SubjectKind::ImageSet;
    for (const auto* name : {L"Txt_Title_1", L"Txt_Time", L"Txt_Volume", L"Txt_HighScore", L"Txt_TargetScore", L"Txt_Title", L"Txt_Rank"})
        widget_kinds[name] = native::SubjectKind::TextBlock;
    widget_kinds[L"Pnl_Info"] = widget_kinds[L"Pnl_Light"] = native::SubjectKind::CanvasPanel;
    widget_kinds[L"Deco"] = native::SubjectKind::PieImage;
    std::uint64_t bytes = 0; std::string error;
    require(runtime::retain(full_fixture("full-ocean", 0), bytes, error)
        && runtime::retain(full_fixture("full-orchid", 32), bytes, error), "both full fixture programs compile atomically");
    if (probes) require(runtime::retain(images("probe-blue", {{"MusicList/Bg_1", 80}}), bytes, error)
        && runtime::retain(images("probe-magenta", {{"MusicList/Bg_1", 81}}), bytes, error), "collected four-directory inventory compiles");
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "realistic palette authentication");
    runtime::state.palette_configured = true; runtime::install_palette_hooks();
    const auto associate = [&] {
        current.phase = game::MenuSessionPhase::Opening;
        for (int i = 0; i < 13; ++i) {
            auto* row = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x8100 + i));
            row_handles[row] = {100 + i, 10}; active_rows[row] = true;
            setup_song_row(row, 100 + i * 7, 200 + i, lease, [] {});
        }
        for (int i = 0; i < 5; ++i) {
            auto* row = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9100 + i));
            row_handles[row] = {200 + i, 20}; active_rows[row] = true;
            if (std::find(speed_members.begin(), speed_members.end(), row) == speed_members.end()) speed_members.push_back(row);
        }
        current.phase = game::MenuSessionPhase::Ready; native_selected = 107;
    };
    associate(); open(lease, false);
    require(!creates && !captures && !lookups && !applies && runtime::state.themes[0].available,
        "13-song/5-speed Ready remains constant native work: Original is immediately selectable");
    key(lease); require(runtime::state.applied==1 && runtime::state.desired==SIZE_MAX && saves==1 && !queued_ticket && !runtime::state.work_pending,
        "one explicit full-theme dispatch admits all real receivers and commits before return without a successor chain");
    sample(true, false); sample(true, false, true, false); deliver(lease);
    require(runtime::state.desired == SIZE_MAX && !runtime::state.applied && visuals==originals && saves==2,
        "Original remains immediately selectable after finite full-theme application");
    require(!runtime::state.work_pending, "cancelled full candidate stops preparation until a real request");
    key(lease);
    const auto receivers=1+runtime::state.rows.size();
    require(!runtime::state.work_pending && !queued_ticket && runtime::state.applied==1
        && runtime::state.bindings.size()<=runtime::state.binding_templates*receivers
        && runtime::state.combinations.size()<=runtime::state.combination_templates*receivers
        && runtime::state.palettes.size()<=runtime::state.palette_templates*receivers,
        "finite full-theme re-request uses unique physical template/receiver slots without posted continuations");
    sample(true, false); sample(true, false, true); deliver(lease); // Full Ocean -> Original.
    prepare_fixture_candidates(lease); // Explicitly admit peers for the legacy complete-cycle/cache fixture.
    check_choice_projection();
    while (runtime::state.applied) key(lease);
    const auto saves_before_full_cycle = saves;
    key(lease);
    require(!runtime::state.compromised.load() && saved == "full-ocean" && saves == saves_before_full_cycle + 1
        && std::all_of(runtime::state.themes.begin(), runtime::state.themes.end(), [](const auto& t) { return t.available; })
        && retained_bytes <= native::kRetainedByteLimit,
        "actual full row expansion admits every fixture candidate; only complete requested program persists");
    const auto factories = creates + captures + composes + tint_calls + lookups;
    key(lease); require(saved == "full-orchid" && factories == creates + captures + composes + tint_calls + lookups,
        "second complete full theme switches from immutable cache with no factories or lookups");
    for (std::size_t i = 0; i < runtime::state.themes.size() - 2; ++i) key(lease);
    require(runtime::state.applied == 0 && saved == "original" && visuals == originals,
        "Original restores all expanded brush slots and is remembered only after successful reset");
    const auto uploads = creates; close(lease); ++current.generation; associate(); open(lease);
    require(creates == uploads && runtime::state.themes[1].available && runtime::state.themes[2].available,
        "reopen reuses pixel resources, charges only real fresh original/composition attempts");
    check_choice_projection();
    close(lease);
}
void dynamic_cache_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); empty_inventory(); generic_enabled = true;
    std::uint64_t bytes = 0; std::string error;
    for (std::size_t group = 0; group < 5; ++group) {
        auto definition = images("bulk", {}); definition.id = "bulk_" + std::to_string(group);
        for (std::size_t n = 0; n < 44; ++n) {
            const auto leaf = "Image_" + std::to_string(group * 44 + n);
            definition.images["MusicListCell/" + leaf] = {{1, 2, 3, 255}, 1, 1};
            widget_kinds[std::wstring(leaf.begin(), leaf.end())] = native::SubjectKind::Image;
        }
        require(runtime::retain(std::move(definition), bytes, error), "each valid bounded manifest joins unbounded-count inventory");
    }
    auto* binding = &runtime::state.bindings.front(); auto* combination = &runtime::state.combinations.front();
    current.phase = game::MenuSessionPhase::Opening;
    for (int i = 0; i < 5; ++i) {
        auto* row = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xe100 + i));
        row_handles[row] = {1200 + i, 45}; active_rows[row] = true;
        setup_song_row(row, i, 800 + i, lease, [] {});
    }
    current.phase = game::MenuSessionPhase::Ready;
    open(lease, false);
    require(!creates && !captures && runtime::state.themes[0].available, "large inventory never batches native work in Ready");
    prepare_fixture_candidates(lease);
    require(runtime::state.bindings.size() == 1320 && runtime::state.combinations.size() == 1320
        && retained_images == 2201 && creates == 1 && captures == 1100 && composes == 1100
        && std::all_of(runtime::state.themes.begin(), runtime::state.themes.end(), [](const auto& t) { return t.available; })
        && binding == &runtime::state.bindings.front() && combination == &runtime::state.combinations.front()
        && !applies && !saves && !runtime::state.compromised.load(),
        "dynamic bindings/compositions/scratch exceed former ceilings without moving owning handles or claiming partial apply");
    close(lease);

    reset(); empty_inventory(); generic_enabled = true; bytes = 0;
    widget_kinds[L"Cell"] = native::SubjectKind::Image;
    auto many_rows = images("many_rows", {{"MusicListCell/Cell", 1}});
    require(runtime::retain(std::move(many_rows), bytes, error), "dynamic song row program compiled");
    current.phase = game::MenuSessionPhase::Opening;
    for (int i = 0; i < 70; ++i) {
        auto* row = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xd100 + i));
        row_handles[row] = {1000 + i, 44}; active_rows[row] = true;
        setup_song_row(row, i, 700 + i, lease, [] {});
    }
    current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(runtime::state.themes[1].available && captures == 70 && composes == 70 && creates == 1
        && std::count_if(runtime::state.rows.begin(), runtime::state.rows.end(), [](const auto& r) { return r.available; }) == 70
        && !runtime::state.untracked_row && !runtime::state.compromised.load(),
        "positive current rows grow beyond former 64-row quota with stable native tickets");
    close(lease);

    reset(); empty_inventory(); generic_enabled = true;
    widget_kinds[L"RootText"] = native::SubjectKind::TextBlock;
    auto main = images("main", {}); main.colors["MusicList/RootText"] = {100, 120, 150, 255};
    bytes = 0; require(runtime::retain(std::move(main), bytes, error), "stable palette main compiled");
    auto rows = images("rows", {});
    for (std::size_t n = 0; n < 100; ++n) {
        const auto leaf = "RowText_" + std::to_string(n);
        rows.colors["MusicListCell/" + leaf] = {10, 20, 30, 255};
        widget_kinds[std::wstring(leaf.begin(), leaf.end())] = native::SubjectKind::TextBlock;
    }
    require(runtime::retain(std::move(rows), bytes, error), "row palette contexts compiled without runtime source quota");
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "palette boundary configured");
    runtime::state.palette_configured = true; runtime::install_palette_hooks(); runtime::state.selected = "main";
    current.phase = game::MenuSessionPhase::Opening;
    for (int i = 0; i < 13; ++i) {
        auto* row = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xb100 + i));
        row_handles[row] = {700 + i, 20}; active_rows[row] = true;
        setup_song_row(row, i, 600 + i, lease, [] {});
    }
    current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(runtime::state.palettes.size() == 1401 && runtime::state.themes[1].available
        && runtime::state.themes[2].available && color_sources.size() == 1301 && runtime::state.applied == 1 && saved == "main"
        && !runtime::state.compromised.load(), "dynamic palette backend admits expanded peer while preserving valid current main theme");
    auto& source = color_sources.at({runtime::state.palettes[0].subject.widget, palette::Property::Text});
    require(source.subject.context == &runtime::state.palettes[0]
        && source.subject.validate(source.subject.context, source.subject, current),
        "published palette validator address remains stable after more than former context ceiling expands");
    qualified_native_color(source, {0.2f, 0.3f, 0.4f, 0.5f});
    require(source.native[3] == 0.5f && source.shown[3] == 0.5f, "retained main validator still consumes genuine latest native source");
    close(lease);
}
void runtime_metrics_tests(const core::HookCallbackGate::Lease& lease) {
    namespace metrics = game::menu_theme_metrics;
    const auto run = [&](bool enabled) {
        metrics::enable(false); metrics::set_sink(nullptr);
        metrics::Snapshot snapshot;
        while (metrics::take_snapshot(snapshot)) {}
        reset(); empty_inventory(); generic_enabled = true;
        widget_kinds = {{L"Cell", native::SubjectKind::Image}, {L"Label", native::SubjectKind::TextBlock}};
        auto program = images("alpha", {{"MusicListCell/Cell", 5}});
        program.colors["MusicListCell/Label.ColorAndOpacity"] = {70, 130, 210, 255};
        std::uint64_t bytes = 0; add(std::move(program), bytes);
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
            {nullptr, runtime::read_palette_stack})), "metrics fixture palette reader configures");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
        metrics::enable(enabled);
        std::array<std::uint64_t, metrics::metric_count> samples{};
        std::array<std::uint64_t, metrics::counter_count> counters{};
        const auto drain = [&] {
            while (metrics::take_snapshot(snapshot)) {
                for (std::size_t i = 0; i < samples.size(); ++i) samples[i] += snapshot.stats[i].count;
                for (std::size_t i = 0; i < counters.size(); ++i) counters[i] += snapshot.counters[i];
            }
        };
        const auto sampled = [&](metrics::Metric metric) { return samples[static_cast<std::size_t>(metric)]; };
        const auto counted = [&](metrics::Counter counter) { return counters[static_cast<std::size_t>(counter)]; };
        void* row = reinterpret_cast<void*>(0x9840); row_handles[row] = {72, 172}; active_rows[row] = true;
        current.phase = game::MenuSessionPhase::Opening; setup_song_row(row, 17, 1, lease, [] {});
        current.phase = game::MenuSessionPhase::Ready; slow_capture = true; open(lease, false);
        sample(true, true); deliver(lease); sample(true, false); finish_preparation(lease);
        require(runtime::state.applied == 1, "metrics scenario actually commits the deferred theme");
        select_song(17, lease, [] {}); drain();
        if (enabled) require(sampled(metrics::Metric::ReadyToCommit) == 1 && sampled(metrics::Metric::RowPendingDelay) == 1
            && !sampled(metrics::Metric::PreparationQueueWait) && counted(metrics::Counter::RowsStyled) == 1,
            "semantic request records one actual commit/publication without inventing maintenance queue service");
        setup_song_row(row, 42, 2, lease, [&] { native_row_style(row); }, true, nullptr, true, false);
        require(!runtime::state.rows[0].style_pending, "actual row callback publishes its semantic contribution before return");
        close(lease); drain();
        ++current.generation; current.phase = game::MenuSessionPhase::Opening;
        setup_song_row(row, 99, 3, lease, [] {});
        drain();
        if (enabled) require(sampled(metrics::Metric::RowPendingDelay) == 2 && !counted(metrics::Counter::RowsCancelled),
            "positive opening association remains pending without a Ready owner or false completion sample");
        ++current.generation;current.phase=game::MenuSessionPhase::Opening;
        setup_song_row(row,100,4,lease,[]{});
        drain();
        if (enabled) require(counted(metrics::Counter::RowsCancelled)==1,
            "genuine opening replacement cancels the superseded association before Ready");
        current.phase = game::MenuSessionPhase::Ready; slow_capture = false; open(lease, false); finish_preparation(lease);
        require(runtime::state.applied == 1, "metrics reopen scenario actually restores the remembered theme");
        select_song(99, lease, [] {}); close(lease); drain();
        if (enabled) require(sampled(metrics::Metric::ReadyToCommit) == 2 && sampled(metrics::Metric::RowPendingDelay) == 3
            && counted(metrics::Counter::RowsPending) == 4 && counted(metrics::Counter::RowsStyled) == 3
            && counted(metrics::Counter::RowsCancelled) == 1,
            "new generation gets fresh latency samples, with no duplicates after maintenance or cancellation");
        if (enabled) std::cout << "runtime metrics Ready commits=" << sampled(metrics::Metric::ReadyToCommit)
            << " row publications=" << sampled(metrics::Metric::RowPendingDelay)
            << " pending=" << counted(metrics::Counter::RowsPending) << " styled=" << counted(metrics::Counter::RowsStyled)
            << " cancelled=" << counted(metrics::Counter::RowsCancelled)
            << " queue deliveries=" << sampled(metrics::Metric::PreparationQueueWait) << '\n';
        metrics::enable(false);
        return std::array<std::uint64_t, 12>{creates, captures, composes, borrows, applies, palette_changes,
            lookups, row_binds, speed_binds, subject_validations, row_originals_called, saves};
    };
    const auto without = run(false), with = run(true);
    require(with == without, "runtime metrics do not add native reads, validation, mutation, preparation or persistence operations");
    std::cout << "runtime metrics native operation counters unchanged (12 categories)\n";
}
void executable_mapping_route_tests() {
    reset();
    const auto module=runtime::state.module;
    const auto address=reinterpret_cast<std::uintptr_t>(module)+0x2000;
    for(const auto protection:{PAGE_EXECUTE_READ,PAGE_EXECUTE_READWRITE,PAGE_EXECUTE_WRITECOPY}) {
        mapping_protection=protection;
        require(runtime::executable_address(module,address) && !mapping_allow_execute_only
            && core::is_executable_image_address(module,address,true),"shared mapping helper retains each executable/readable variant");
    }
    mapping_protection=PAGE_EXECUTE;
    require(!runtime::executable_address(module,address) && !mapping_allow_execute_only
        && core::is_executable_image_address(module,address,true),"runtime excludes execute-only while row/selection policy permits it");
    for(const auto protection:{PAGE_READONLY,PAGE_READWRITE,PAGE_NOACCESS,PAGE_EXECUTE_READ|PAGE_GUARD}) {
        mapping_protection=protection;
        require(!runtime::executable_address(module,address) && !core::is_executable_image_address(module,address,true),
            "nonexecutable, guarded and inaccessible mappings never qualify");
    }
    mapping_protection=PAGE_EXECUTE_READ;mapping_type=MEM_PRIVATE;
    require(!runtime::executable_address(module,address),"private allocation is not executable image authority");
    mapping_type=MEM_IMAGE;mapping_state=MEM_RESERVE;
    require(!runtime::executable_address(module,address),"uncommitted image mapping is rejected");
    mapping_state=MEM_COMMIT;mapping_foreign_owner=true;
    require(!runtime::executable_address(module,address),"foreign allocation ownership is rejected");
    mapping_foreign_owner=false;mapping_query_succeeds=false;
    require(!runtime::executable_address(module,address),"failed current mapping query is rejected");
    mapping_query_succeeds=true;
    const auto queries=mapping_queries;
    require(!runtime::executable_address(nullptr,address)
        && !runtime::executable_address(module,reinterpret_cast<std::uintptr_t>(module)-1)
        && !runtime::executable_address(module,reinterpret_cast<std::uintptr_t>(module)+0x20000000)
        && mapping_queries==queries,"module and exact image bounds fail before an OS query");
}
void runtime_phase_guard_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); open(lease, false);
    runtime::state.focused = true; runtime::state.focus_epoch = 9;
    DispatchFrame frame{++runtime::state.sequence, current, lease, false}; dispatch_frame = &frame;
    alignas(runtime::Scope) unsigned char storage[sizeof(runtime::Scope)];
    auto* scope = new (storage) runtime::Scope(runtime::Source::PostedInput, &frame, lease, current, frame.ticket, 9);
    auto saved = scope->authority();
    require(saved.phase_current && saved.phase_current(saved.context, current)
        && saved.qualify_current_callback(saved.context, current), "Scope phase guard supplements full owned-frame admission");
    const auto before = native_fact_captures;
    const auto mappings_before=executable_mapping_calls;
    for (unsigned i = 0; i < 100; ++i) require(saved.phase_current(saved.context, current), "uninterrupted Scope phase remains current");
    require(native_fact_captures == before && executable_mapping_calls==mappings_before,
        "metadata Scope guards never capture native facts or query executable mappings");
    gc = true;
    require(saved.phase_current(saved.context, current) && !saved.qualify_current_callback(saved.context, current),
        "cheap phase guard is not standalone GC authority");
    gc = false; wrong_tls = true;
    require(!saved.qualify_current_callback(saved.context, current), "full Scope admission still rejects changed TLS");
    wrong_tls = false;
    runtime::state.focus_epoch = 10;
    require(!saved.phase_current(saved.context, current), "input phase revokes changed focus epoch");
    runtime::state.focus_epoch = 9; runtime::state.row_rebind = current.widget;
    require(!saved.phase_current(saved.context, current), "input phase revokes row rebind");
    runtime::state.row_rebind = nullptr;
    const auto old_thread = scope->thread; ++scope->thread;
    require(!saved.phase_current(saved.context, current), "Scope phase checks established thread metadata");
    scope->thread = old_thread;
    ++current.generation;
    require(!saved.phase_current(saved.context, scope->session), "Scope phase revokes session change");
    --current.generation;
    runtime::callback_entered();
    require(!saved.phase_current(saved.context, current) && !saved.qualify_current_callback(saved.context, current),
        "reentry retires the parent Scope for both full and phase admission");
    scope->~Scope();
    runtime::state.compromised = false; // Separate fresh-admission fixture; never revive the retired Scope.
    scope = new (storage) runtime::Scope(runtime::Source::PostedInput, &frame, lease, current, frame.ticket, 9);
    auto fresh = scope->authority();
    require(fresh.phase_current(fresh.context, current) && fresh.qualify_current_callback(fresh.context, current)
        && !saved.phase_current(saved.context, current) && !saved.qualify_current_callback(saved.context, current),
        "same physical Scope stack storage cannot revive saved authority");
    scope->~Scope(); dispatch_frame = nullptr;
    require(!fresh.phase_current(fresh.context, current), "destroyed Scope authority has no surviving TLS owner");

    void* caller = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(runtime::state.module) + 0x2000);
    palette::NativeInvocation invocation{&caller, caller, current.widget, palette::Property::Text, false, GetCurrentThreadId(), 900};
    palette_invocation = &invocation; palette::Authority authority{};
    const auto candidate_before = native_fact_captures;
    const auto mapping_before=executable_mapping_calls;
    observed_return_slot=invocation.return_slot;observed_return_reads=0;
    require(runtime::read_palette_stack(nullptr, invocation, authority) && authority.phase_current
        && authority.phase_current(authority.context, current) && native_fact_captures == candidate_before
        && executable_mapping_calls==mapping_before && !observed_return_reads,
        "palette reader supplies a metadata candidate without duplicate full admission");
    require(authority.qualify_current_callback(authority.context, current) && native_fact_captures == candidate_before + 1
        && executable_mapping_calls==mapping_before+1 && !mapping_allow_execute_only && observed_return_reads==1,
        "palette first full admission still captures live engine facts");
    mapping_protection=PAGE_EXECUTE_READ|PAGE_GUARD;
    require(!authority.qualify_current_callback(authority.context,current)
        && executable_mapping_calls==mapping_before+2 && observed_return_reads==1
        && native_fact_captures==candidate_before+1,"fresh helper rejects protection drift before native facts or return-slot read");
    mapping_protection=PAGE_EXECUTE_READ;caller=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(invocation.caller)+1);
    require(!authority.qualify_current_callback(authority.context,current)
        && executable_mapping_calls==mapping_before+3 && observed_return_reads==2
        && native_fact_captures==candidate_before+1,"mapping proof never replaces live return-slot/caller equality");
    caller=invocation.caller;
    require(authority.qualify_current_callback(authority.context,current)
        && executable_mapping_calls==mapping_before+4 && observed_return_reads==3
        && native_fact_captures==candidate_before+2,"full admission reacquires helper, live caller and native facts without cached authority");
    std::cout<<"runtime executable mapping fresh-helper="<<executable_mapping_calls-mapping_before
        <<" return-slot-reads="<<observed_return_reads<<" guard/caller-drift=rejected execute-only-policy=distinct\n";
    gc = true;
    require(authority.phase_current(authority.context, current) && !authority.qualify_current_callback(authority.context, current),
        "palette phase metadata does not replace full GC proof");
    gc = false;
    runtime::callback_entered();
    require(!authority.phase_current(authority.context, current) && !authority.qualify_current_callback(authority.context, current),
        "palette reentry revokes the private parent invocation serial");
    ++invocation.serial;
    require(!authority.phase_current(authority.context, current) && !authority.qualify_current_callback(authority.context, current),
        "same private setter stack storage cannot revive prior serial");
    require(runtime::read_palette_stack(nullptr, invocation, authority)
        && authority.qualify_current_callback(authority.context, current), "fresh palette invocation performs its own full admission");
    palette_invocation = nullptr;
    require(!authority.phase_current(authority.context, current), "palette guard rejects a saved authority outside private TLS");
    observed_return_slot=nullptr;
    close(lease);
    std::cout << "runtime phase guards metadata-fact-captures=0 palette-candidate-fact-captures=0 first-full=1 saved-stack-reuse=rejected\n";
}
void original_palette_quiet_tests(const core::HookCallbackGate::Lease& lease) {
    reset(); empty_inventory(); generic_enabled = true;
    widget_kinds = {{L"ZBtn", native::SubjectKind::Button}, {L"Label", native::SubjectKind::TextBlock}};
    core::ThemeDefinition program; program.id = "quiet";
    program.colors["MusicList/ZBtn.BackgroundColor"] = {40, 80, 120, 255};
    program.colors["MusicList/Label.ColorAndOpacity"] = {80, 120, 160, 255};
    program.colors["MusicList/Label.BottomColorAndOpacity"] = {120, 160, 200, 255};
    std::uint64_t bytes = 0; add(std::move(program), bytes);
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})),
        "quiet Original palette reader configures");
    runtime::state.palette_configured = true; runtime::install_palette_hooks(); open(lease, false);
    key(lease); finish_preparation(lease);
    require(runtime::state.applied == 1 && color_sources.size() == 3, "requested owning, Bottom and nonowning palettes bind");
    std::map<decltype(color_world)::key_type, std::array<float, 4>> genuine;
    for (const auto& [target, source] : color_sources) genuine[target] = source.native;
    key(lease);
    require(!runtime::state.applied && color_sources.empty() && !runtime::state.work_pending,
        "custom to Original restores and retires all routing under live authority");
    for (const auto& [target, value] : genuine) require(color_world.at(target) == value, "retirement preserves exact genuine palette sources");
    const auto facts = native_fact_captures, validations = subject_validations;
    for (unsigned i = 0; i < 20; ++i) for (const auto& [target, value] : genuine) {
        auto input = value; input[3] = static_cast<float>(i) / 20;
        require(!color_sources.contains(target), "Original native animation has no runtime palette route");
        color_world[target] = input; // Genuine setter's no-route path; real palette seam is tested separately.
    }
    require(native_fact_captures == facts && subject_validations == validations,
        "Original-after-custom animation performs zero runtime qualification or Subject validation");
    key(lease); finish_preparation(lease);
    require(runtime::state.applied == 1 && color_sources.size() == 3 && !captures && !creates && !composes && !tint_calls,
        "later explicit request rebinds genuine sources without recreating brush baselines or resources");
    for (const auto& [target, source] : color_sources) require(source.native[3] == 0.95f, "rebind observes latest unhooked native alpha");
    key(lease);
    for(unsigned mode=0;mode<3;++mode){
        fail_palette_bind_at=palette_bind_attempts+2;key(lease);
        require(runtime::state.themes[1].rejected && !runtime::state.applied
            && runtime::state.desired==SIZE_MAX && color_sources.size()==1 && runtime::state.work_pending,
            "real later bind rejection leaves only the earlier passive source, never partial custom publication");
        if(mode==2){
            sample(true,false);sample(true,false,true);deliver(lease);
            require(!queued_ticket,"gesture never posts its own successor cleanup");
            sample(true,false);deliver(lease); // Rejected demand is already cancelled; poll hands off one cleanup unit.
        }
        else {if(mode==1)runtime::focus_lost();sample(mode==0,false);deliver(lease);}
        require(color_sources.empty() && !runtime::state.work_pending && !runtime::state.compromised.load(),
            "one qualified idle/focus/opposite cleanup retires rejected partial routes without a continuation chain");
        close(lease);++current.generation;current.phase=game::MenuSessionPhase::Ready;
        if(mode<2)open(lease,false);
    }
    std::cout << "bind rejection partial-routes=1 cleanup-routes=0 idle/focus/opposite=checked\n";
    std::cout << "Original-after-custom routes=0 animation-qualifications=0 cancelled-preparation-routes=0 focus-cancel-routes=0\n";
    slow_palette_bind = false;
    open(lease, false); key(lease); finish_preparation(lease);
    require(runtime::state.applied == 1 && saved == "quiet", "retirement failure fixture has a complete committed custom program");
    const auto saves_before_failure = saves;
    fail_palette_retirement = true; key(lease);
    require(runtime::state.compromised.load() && !runtime::state.owner && !color_sources.empty()
        && runtime::state.palette_charged && saved == "quiet" && saves == saves_before_failure,
        "failed owning retirement retains stable orphan contexts and revokes authority without false Original persistence");
}
void continuation_handoff_tests(const core::HookCallbackGate::Lease& lease) {
    const auto missing_then_ready=[&]{
        reset();empty_inventory();generic_enabled=true;widget_kinds={{L"Cell",native::SubjectKind::Image}};
        std::uint64_t bytes=0;add(images("alpha",{{"MusicListCell/Cell",5}}),bytes);
        open(lease,false);key(lease);
        require(runtime::state.desired==1 && !runtime::state.work_pending && !captures && !queued_ticket,
            "missing receiver defers intent without native retries or a successor-message chain");
        auto* row=reinterpret_cast<void*>(0x9860);row_handles[row]={73,173};active_rows[row]=true;
        setup_song_row(row,17,1,lease,[]{},false,nullptr,true,false);
    };
    missing_then_ready();
    auto& s=runtime::state;
    require(s.work_pending && s.desired==1 && !s.applied,"actual receiver setup enables one exceptional fallback unit");
    const auto native_before=std::array{captures,creates,composes,borrows,applies,subject_validations,native_fact_captures};
    const auto ticket=runtime::reserve_preparation_continuation();
    require(ticket && !runtime::reserve_preparation_continuation(),"one metadata reservation suppresses duplicate continuation");
    sample(true,false);require(!queued_ticket && s.pending.ticket==ticket,"poll cannot duplicate reserved preparation");
    require(runtime::post_preparation_continuation(ticket) && queued_ticket==ticket,"worker metadata handoff posts existing private dispatch");
    require(!runtime::post_preparation_continuation(ticket),"reservation submits at most one private message");
    require(native_before==std::array{captures,creates,composes,borrows,applies,subject_validations,native_fact_captures},
        "reservation, polling and metadata handoff perform no native work or fact capture");
    deliver(lease);finish_preparation(lease);
    require(s.applied==1 && saves==1 && !runtime::reserve_preparation_continuation(),"only real qualified dispatch completes desired program");
    close(lease);

    missing_then_ready();
    const auto stale=runtime::reserve_preparation_continuation();
    sample(true,false);sample(true,false,true); // Opposite input supersedes reserved maintenance.
    require(!runtime::post_preparation_continuation(stale),"opposite gesture invalidates delayed maintenance reservation");
    deliver(lease);require(!s.applied && !s.work_pending,"opposite cancels requested work without timer authority");
    missing_then_ready();
    const auto lost=runtime::reserve_preparation_continuation();runtime::focus_lost();
    require(lost,"focus cancellation exercises a delayed continuation, not an already complete request");
    require(runtime::post_preparation_continuation(lost),"unfocused maintenance can carry cancellation cleanup");
    deliver(lease);finish_preparation(lease);
    require(!s.applied && s.desired==SIZE_MAX && !s.work_pending,"focus epoch cancels gesture intent before maintenance mutation");
    close(lease);require(!runtime::post_preparation_continuation(lost),"closed owner rejects saved continuation ticket");
    std::cout<<"runtime continuation metadata-native-calls=0 duplicate/opposite/focus/close=checked\n";
}
void opening_wake_and_changed_rows_tests(const core::HookCallbackGate::Lease& lease) {
    std::uint64_t eventual_dispatches=0;
    const auto opening=[&](bool persisted,bool warm,LONGLONG clock_step=5000,bool clock_valid=true){
        reset();slow_capture=true;performance_step=clock_step;performance_valid=clock_valid;runtime::state.prepared_once=warm;
        if(persisted)runtime::state.selected="night";
        open(lease,false);
        if(persisted){
            require(ready_wake_calls==1 && runtime::state.applied==1 && saves==1
                && !queued_ticket && !runtime::state.work_pending && !runtime::state.preparation_facts.dispatches
                && !performance_ticks && work_clock>=100,
                "cold and warm persisted Ready commit before return, without time yields or posted preparation despite expensive helpers");
            require(!runtime::state.themes[2].available && !runtime::state.themes[2].work.revision,
                "synchronous opening does not prepare inactive programs");
        }else{
            require(!captures && !creates && !applies && !queued_ticket,"Original-only opening stays lazy");
            key(lease);
            require(runtime::state.applied==1 && runtime::state.desired==SIZE_MAX && captures==1 && creates==1 && !queued_ticket,
                "interactive request completes its finite program before dispatch returns despite expensive capture");
            finish_preparation(lease);
            eventual_dispatches=runtime::state.preparation_facts.dispatches;
            require(!eventual_dispatches,"explicit choice requires no successor maintenance dispatch");
        }
        const auto operations=std::tuple{creates,captures,composes,tint_calls,borrows,applies,palette_changes,lookups};
        close(lease);
        if(persisted){
            const auto uploads=creates,baselines=captures;++current.generation;current.phase=game::MenuSessionPhase::Ready;
            open(lease,false);
            require(runtime::state.applied==1 && !queued_ticket && !runtime::state.work_pending && creates==uploads && captures==baselines+1,
                "actual warm reopen commits synchronously with a fresh baseline and reused immutable upload");
            close(lease);
        }
        return operations;
    };
    const auto eventual=opening(false,false),cold=opening(true,false),warm=opening(true,true);
    require(eventual==cold && cold==warm && eventual==opening(false,false,0)
        && eventual==opening(false,false,-5000) && eventual==opening(false,false,5000,false),
        "opening and explicit admission/native totals are independent of frozen, backward, expensive or failing performance clocks");
    reset();runtime::state.selected="night";fail_compose_leaf="Bg_1";open(lease,false);
    require(!runtime::state.applied && !saves && runtime::state.themes[1].rejected && runtime::state.desired==SIZE_MAX,
        "synchronous opening rejection preserves Original without false persistence");close(lease);
    reset();runtime::state.selected="night";gc=true;open(lease,false);
    require(!captures && !applies && !saves && !runtime::state.owner,"unsafe Ready facts never admit opening work");
    reset();runtime::state.selected="night";nested_create=true;open(lease,false);
    require(runtime::state.compromised.load() && !runtime::state.applied && !applies && !saves,
        "opening helper reentry retires genuine authority without partial commit");

    reset();empty_inventory();generic_enabled=true;widget_kinds={{L"Cell",native::SubjectKind::Image}};
    std::uint64_t late_bytes=0;add(images("alpha",{{"MusicListCell/Cell",5}}),late_bytes);runtime::state.selected="alpha";
    open(lease,false);
    require(!runtime::state.applied && runtime::state.desired==1 && !captures && !creates && !queued_ticket,
        "absent receivers leave Original and deferred intent without an opening retry loop");
    void* late_row=reinterpret_cast<void*>(0x9880);row_handles[late_row]={75,175};active_rows[late_row]=true;
    setup_song_row(late_row,18,1,lease,[]{});finish_preparation(lease);
    require(runtime::state.applied==1 && saves==1 && !runtime::state.compromised.load(),
        "later genuine association completes deferred opening through existing bounded preparation");close(lease);

    reset();empty_inventory();generic_enabled=true;widget_kinds={{L"Cell",native::SubjectKind::Image}};
    std::uint64_t bytes=0;add(images("alpha",{{"MusicListCell/Cell",5}}),bytes);
    void* row=reinterpret_cast<void*>(0x9870);row_handles[row]={74,174};active_rows[row]=true;
    current.phase=game::MenuSessionPhase::Opening;setup_song_row(row,17,1,lease,[]{});
    current.phase=game::MenuSessionPhase::Ready;open(lease); // Explicit fixture admission leaves genuine baselines current.
    auto& s=runtime::state;auto& theme=s.themes[1];
    const auto operations=std::tuple{creates,captures,composes,tint_calls,borrows,applies,palette_changes,lookups,row_binds};
    const auto pending=[&]{theme.available=false;theme.preparing=true;theme.work.changed_rows.insert(0);s.desired=1;runtime::update_work_pending();};
    const auto quantum=[&](bool depleted){
        DispatchFrame frame{++s.sequence,s.owner,lease,true};dispatch_frame=&frame;
        {
            std::lock_guard lock(s.mutex);
            runtime::Scope scope(runtime::Source::Preparation,&frame,lease,s.owner,frame.ticket);
            if(depleted)performance_ticks+=2000000;
            runtime::Event event;require(runtime::prepare_quantum(scope,event,nullptr),"changed-row metadata remains valid");
        }
        dispatch_frame=nullptr;
    };
    performance_step=0;pending();
    quantum(false);require(theme.available,"changed-row expansion and admission complete in one semantic unit");
    pending();
    quantum(true);require(theme.available && theme.work.changed_rows.empty(),
        "elapsed clock changes cannot defer changed-row expansion or whole-candidate admission");
    require(operations==std::tuple{creates,captures,composes,tint_calls,borrows,applies,palette_changes,lookups,row_binds},
        "eliminating the metadata-only yield adds no owning, binding or rendering operations");
    performance_step=64;s.desired=SIZE_MAX;runtime::update_work_pending();close(lease);
    reset();empty_inventory();generic_enabled=true;widget_kinds={{L"Cell",native::SubjectKind::Image}};
    std::uint64_t requested_bytes=0;add(images("alpha",{{"MusicListCell/Cell",5}}),requested_bytes);
    add(images("beta",{{"MusicListCell/Cell",9}}),requested_bytes);
    current.phase=game::MenuSessionPhase::Opening;
    for(unsigned i=0;i<2;++i){
        auto* pointer=reinterpret_cast<void*>(0x98b0+i*0x10);row_handles[pointer]={static_cast<int>(77+i),177};active_rows[pointer]=true;
        setup_song_row(pointer,20+i,1,lease,[]{});
    }
    current.phase=game::MenuSessionPhase::Ready;open(lease,false);key(lease);
    // Projection-only queued maintenance; baseline holders came from qualified native helpers.
    runtime::state.rows[0].style_pending=true;require(runtime::invalidate_song_row(0),"pending committed-row fixture allocation");
    key(lease);
    require(runtime::state.applied==2 && saved=="beta" && saves==2 && runtime::state.themes[2].available
        && runtime::state.pending_rows.empty() && !queued_ticket && !runtime::state.work_pending
        && visuals.at(row_leaf(reinterpret_cast<void*>(0x98b0),"Cell"))==9
        && visuals.at(row_leaf(reinterpret_cast<void*>(0x98c0),"Cell"))==9,
        "explicit whole-program service bypasses committed row queue and commits every requested contribution before return");
    close(lease);
    std::cout<<"persisted opening cold/warm commits-before-return=1 posted-preparation=0 time-yields=0 eventual-native-operations=equal\n"
        <<"interactive eventual posted-preparation="<<eventual_dispatches<<" persisted Ready=0 native create/capture/compose/tint/borrow/apply/palette/find="
        <<std::get<0>(eventual)<<'/'<<std::get<1>(eventual)<<'/'<<std::get<2>(eventual)<<'/'<<std::get<3>(eventual)<<'/'
        <<std::get<4>(eventual)<<'/'<<std::get<5>(eventual)<<'/'<<std::get<6>(eventual)<<'/'<<std::get<7>(eventual)<<'\n'
        <<"explicit whole request bypasses pending-row queue=checked performance-clock frozen/backward/failed native-equivalence=checked\n"
        <<"changed-row metadata semantic-units=1 clock-dependent-yields=0 owning-operations=0\n";
}
void indexed_song_row_tests(const core::HookCallbackGate::Lease& lease) {
    const auto run = [&](unsigned rows, unsigned inactive) {
        reset(); empty_inventory(); generic_enabled = true;
        widget_kinds = {{L"Cell", native::SubjectKind::Image}, {L"Label", native::SubjectKind::TextBlock}};
        std::uint64_t bytes = 0;
        auto alpha = images("alpha", {{"MusicListCell/Cell", 5}});
        alpha.colors["MusicListCell/Label.ColorAndOpacity"] = {70, 130, 210, 255};
        add(std::move(alpha), bytes);
        for (unsigned i = 0; i < inactive; ++i) {
            auto peer = images(("peer_" + std::to_string(i)).c_str(), {{"MusicListCell/Cell", static_cast<std::uint8_t>(20 + i)}});
            add(std::move(peer), bytes);
        }
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "indexed row palette reader");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
        current.phase = game::MenuSessionPhase::Opening;
        for (unsigned i = 0; i < rows; ++i) {
            auto* row = reinterpret_cast<void*>(0x71000 + i * 0x100);
            row_handles[row] = {static_cast<int>(80 + i), static_cast<int>(180 + i)}; active_rows[row] = true;
            setup_song_row(row, 100 + i, 1, lease, [] {}, false);
        }
        current.phase = game::MenuSessionPhase::Ready; open(lease, false); key(lease); finish_preparation(lease);
        require(runtime::state.applied == 1, "indexed row fixture commits the complete requested theme");
        const std::array before{captures, composes, borrows, applies, palette_changes, lookups, row_binds, subject_validations};
        auto* row = reinterpret_cast<void*>(0x71000);
        setup_song_row(row, 700, 2, lease, [&] { native_row_style(row); }, true);
        require(runtime::state.applied == 1 && runtime::state.rows[0].association_setup == 2
            && runtime::state.rows[0].index == 700 && !runtime::state.rows[0].style_pending
            && visuals.at(row_leaf(row, "Cell")) == 5, "indexed repair uses the fresh positive ticket and baseline");
        const std::array after{captures, composes, borrows, applies, palette_changes, lookups, row_binds, subject_validations};
        std::array<unsigned, 8> delta{};
        for (std::size_t i = 0; i < delta.size(); ++i) delta[i] = after[i] - before[i];
        const auto visits = runtime::state.rows[0].work.visits;
        require(visits && runtime::state.rows[0].lookup_visits, "row reconciliation exposes actual preparation and projection visits");
        for (unsigned i = 1; i < rows; ++i)
            require(!runtime::state.rows[i].style_pending && visuals.at(row_leaf(reinterpret_cast<void*>(0x71000 + i * 0x100), "Cell")) == 5,
                "unaffected physical rows retain their committed presentation");
        const auto lookup_visits = runtime::state.rows[0].lookup_visits;
        close(lease);
        return std::tuple{delta, visits, lookup_visits};
    };
    const auto small = run(2, 0), large = run(32, 0), peers = run(32, 8);
    require(small == large && large == peers, "one-row preparation/lookup visits and native calls do not grow with unrelated rows or inactive themes");
    std::cout << "indexed Song repair rows=2/32 inactive=0/8 preparation-visits=" << std::get<1>(small)
        << " lookup-visits=" << std::get<2>(small) << " native-operation-deltas=identical\n";

    reset(); empty_inventory(); generic_enabled = true;
    widget_kinds = {{L"Cell", native::SubjectKind::Image}, {L"Extra", native::SubjectKind::Image}, {L"Label", native::SubjectKind::TextBlock}};
    std::uint64_t bytes = 0;
    add(images("alpha", {{"MusicListCell/Cell", 5}}), bytes);
    auto beta = images("beta", {{"MusicListCell/Cell", 7}, {"MusicListCell/Extra", 9}});
    beta.colors["MusicListCell/Label.ColorAndOpacity"] = {100, 150, 220, 255}; add(std::move(beta), bytes);
    require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID, {nullptr, runtime::read_palette_stack})), "superseded row palette reader");
    runtime::state.palette_configured = true; runtime::install_palette_hooks();
    current.phase = game::MenuSessionPhase::Opening;
    for (unsigned i = 0; i < 4; ++i) {
        auto* row = reinterpret_cast<void*>(0x75000 + i * 0x100);
        row_handles[row] = {static_cast<int>(110 + i), static_cast<int>(210 + i)}; active_rows[row] = true;
        setup_song_row(row, 100 + i, 1, lease, [] {}, false);
    }
    current.phase = game::MenuSessionPhase::Ready; open(lease, false); key(lease); finish_preparation(lease);
    auto& s = runtime::state;
    auto* exposed = reinterpret_cast<void*>(0x75400);
    row_handles[exposed] = {114, 214}; active_rows[exposed] = true;
    setup_song_row(exposed, 104, 1, lease, [] {});
    const auto exposed_binding = s.rows[4].inventory.bindings.front();
    require(s.themes[1].available && s.applied == 1 && s.themes[1].work.bindings.contains(exposed_binding)
        && s.themes[1].work.combinations.contains(s.rows[4].inventory.combinations.front()) && saves == 1,
        "newly exposed committed row joins complete-plan coverage without a whole-program reconstruction or save");
    prepare_fixture_candidates(lease); // Real passive candidate baselines, never fabricated native ownership.
    // Model admitted candidate progress invalidated by the following real row callbacks.
    s.desired=2;s.desired_save=true;s.desired_gesture=true;s.desired_direction=runtime::Direction::Next;s.desired_epoch=s.focus_epoch.load();
    s.themes[2].available=false;s.themes[2].preparing=true;
    require(s.applied == 1 && !s.themes[2].available, "row-unit fixture retains a separate requested whole program");
    const auto revision = s.work_revision;
    const auto peer_revision = s.themes[2].work.revision;
    auto prior = std::find_if(s.bindings.begin(), s.bindings.end(), [&](const auto& binding) {
        return binding.row == 0 && binding.leaf == L"Extra" && binding.available;
    });
    require(prior != s.bindings.end(), "pending peer has durable owning progress on an unaffected row");
    const auto* prior_binding = &*prior; // Stable deque record, not an iterator across physical expansion.
    const auto prior_original = brush_token(prior_binding->original);
    auto* recycled = reinterpret_cast<void*>(0x75100);
    setup_song_row(recycled, 700, 2, lease, [&] { native_row_style(recycled); }, true, nullptr, true, false);
    const auto stale_subject = subject_token(s.bindings[s.rows[1].inventory.bindings.front()].subject);
    setup_song_row(recycled, 701, 3, lease, [] {}, true, nullptr, true, false); // Same appearance, genuinely new logical association.
    require(s.work_revision == revision && s.themes[2].work.revision == peer_revision
        && brush_token(prior_binding->original) == prior_original && s.applied == 1 && s.desired == 2 && !s.themes[2].available,
        "rapid equal-looking row supersession retains unrelated candidate work without admitting it");
    require(s.rows[1].association_setup == 3 && s.rows[1].index==701 && subject_token(s.bindings[s.rows[1].inventory.bindings.front()].subject) != stale_subject,
        "equal-looking recycled rows use fresh setup Subjects, never old affinity");
    slow_capture = false; finish_preparation(lease);
    require(s.applied == 2 && s.themes[2].available && !s.rows[1].style_pending && saves == 2
        && brush_token(prior_binding->original) == prior_original && visuals.at(row_leaf(recycled, "Extra")) == 9,
        "whole candidate commits only after the latest row and unaffected requested work are complete");
    std::cout<<"actual row exact-contribution=checked unrelated-requested-progress=preserved whole-admission=deferred fresh-setup-affinity=checked\n";
    check_choice_projection(); key(lease); // Beta -> Original: retire all passive palette routing.
    require(!s.applied && color_sources.empty(), "indexed records survive exact Original cleanup without routes");
    key(lease); finish_preparation(lease); check_choice_projection();
    require(s.applied == 1 && !s.compromised.load(), "re-request after Original rebuilds valid canonical row indices");
    close(lease);
}
void demand_preparation_tests(const core::HookCallbackGate::Lease& lease) {
    const auto run = [&](unsigned inactive) {
        reset(); empty_inventory(); generic_enabled = true;
        widget_kinds = {{L"Cell", native::SubjectKind::Image}, {L"Label", native::SubjectKind::TextBlock}};
        auto program = images("alpha", {{"MusicListCell/Cell", 5}});
        program.colors["MusicListCell/Label.ColorAndOpacity"] = {70, 130, 210, 255};
        std::uint64_t bytes = 0; add(std::move(program), bytes);
        for (unsigned i = 0; i < inactive; ++i) {
            const auto leaf = "Inactive" + std::to_string(i);
            widget_kinds[std::wstring(leaf.begin(), leaf.end())] = native::SubjectKind::Image;
            const auto selector = (i ? "MusicListCell/" : "SpeedIcon/") + leaf;
            auto peer = images(("peer_" + std::to_string(i)).c_str(), {{selector.c_str(), static_cast<std::uint8_t>(20 + i)}});
            add(std::move(peer), bytes);
        }
        require(static_cast<bool>(palette::configure(runtime::state.module, FF7RP_TARGET_BUILD_ID,
            {nullptr, runtime::read_palette_stack})), "demand fixture palette reader configures");
        runtime::state.palette_configured = true; runtime::install_palette_hooks();
        void* row = reinterpret_cast<void*>(0x9860); row_handles[row] = {73, 173}; active_rows[row] = true;
        current.phase = game::MenuSessionPhase::Opening; setup_song_row(row, 17, 1, lease, [] {});
        current.phase = game::MenuSessionPhase::Ready; open(lease, false);
        for (unsigned i = 0; i < 4; ++i) {
            setup_song_row(row, 18 + i, 2 + i, lease, [&] { native_row_style(row); }, true, nullptr, true, false);
            sample(true, false); require(!queued_ticket, "Original scrolling never posts inactive preparation");
        }
        require(!runtime::state.work_pending && subject_prepares == 0 && row_binds == 0 && speed_reads == 0 && speed_binds == 0
            && lookups == 0 && capture_api_calls == 0 && creates == 0 && composes == 0 && tint_calls == 0 && palette_changes == 0
            && color_sources.empty() && runtime::state.bindings.size() == runtime::state.binding_templates
            && runtime::state.combinations.size() == runtime::state.combination_templates
            && runtime::state.palettes.size() == runtime::state.palette_templates,
            "Original-only open and recycling perform no native preparation, capture, upload, composition or passive source binding");
        slow_capture = true;
        sample(true, true); deliver(lease);
        require(capture_api_calls == 1 && runtime::state.applied == 1 && runtime::state.desired == SIZE_MAX && !queued_ticket,
            "finite explicit custom request completes before return despite expensive diagnostic clocks");
        require(runtime::state.applied == 1 && saved == "alpha", "explicit re-request completes and persists the whole chosen program");
        setup_song_row(row, 42, 6, lease, [&] { native_row_style(row); }, true, nullptr, true, false);
        finish_preparation(lease);
        require(runtime::state.applied == 1 && !runtime::state.rows[0].style_pending, "committed-row recycling repairs only demanded work");
        setup_song_row(row, 42, 7, lease, [&] {
            native_row_style(row); // Updates the fake native resource/geometry baseline.
            qualified_native_color(row_color(row, palette::Property::Text), {0.2f, 0.3f, 0.4f, 0.35f});
        }, true, nullptr, true, false);
        finish_preparation(lease);
        require(runtime::state.applied == 1 && !runtime::state.work_pending && speed_reads == 0 && speed_binds == 0,
            "same-association fresh-baseline reconstruction neither queues nor reconciles inactive peers");
        for (std::size_t i = 2; i < runtime::state.themes.size(); ++i)
            require(!runtime::state.themes[i].available && runtime::state.themes[i].work.revision == 0,
                "unrequested programs retain logical inventory without physical admission work");
        close(lease);
        std::cout << "demand preparation inactive=" << inactive << " Original materialize/bind/capture=0 captures=" << captures
            << " composes=" << composes << " row-binds=" << row_binds << "\n";
        return std::array<std::uint64_t, 14>{creates, captures, capture_api_calls, composes, tint_calls, borrows, applies,
            palette_changes, lookups, row_binds, speed_binds, speed_reads, subject_prepares, subject_validations};
    };
    const auto small = run(0), large = run(8);
    require(small == large, "adding eight inactive programs adds zero native work to requested theme preparation, recycling, refresh or close");
}
void native_batch_runtime_tests(const core::HookCallbackGate::Lease& lease) {
    reset();empty_inventory();generic_enabled=generic_tint_enabled=true;
    widget_kinds={{L"Btn",native::SubjectKind::Button},{L"Cell",native::SubjectKind::Image},{L"Label",native::SubjectKind::TextBlock}};
    std::uint64_t bytes=0;
    auto alpha=images("alpha",{{"MusicListCell/Btn.Normal",7},{"MusicListCell/Btn.Hovered",7},{"MusicListCell/Cell",4}});
    alpha.colors["MusicListCell/Btn.Normal"]={128,64,32,255};
    alpha.colors["MusicListCell/Label.ColorAndOpacity"]={180,90,40,255};
    add(std::move(alpha),bytes);add(images("beta",{{"MusicListCell/Btn.Normal",9}}),bytes);
    require(static_cast<bool>(palette::configure(runtime::state.module,FF7RP_TARGET_BUILD_ID,{nullptr,runtime::read_palette_stack})),"batch palette configured");
    runtime::state.palette_configured=true;runtime::install_palette_hooks();
    void* row=reinterpret_cast<void*>(0x76b00);row_handles[row]={121,221};active_rows[row]=true;
    current.phase=game::MenuSessionPhase::Opening;setup_song_row(row,17,1,lease,[]{});
    current.phase=game::MenuSessionPhase::Ready;open(lease,false);key(lease);finish_preparation(lease);
    require(runtime::state.applied==1 && captures==3 && capture_batches==1 && capture_api_calls==2
        && composes==3 && tint_calls==1 && applies==3 && button_publications==1 && palette_changes==1,
        "batched first admission preserves three genuine baselines, three compositions, one tint and exact publications");
    require(resolve_max_members>=3 && resolve_compose_reuses==1 && preflight_max_images>=4,
        "Button dependencies resolve together; next/prior affinities preflight as one Subject group");
    const auto before_capture=captures,before_compose=composes,before_tint=tint_calls,before_apply=applies;
    const auto before_lookup=lookups,before_bind=row_binds,before_palette=palette_changes;
    setup_song_row(row,17,2,lease,[&]{native_row_style(row);},true);
    require(runtime::state.applied==1 && captures==before_capture+3 && composes==before_compose+3
        && tint_calls==before_tint+1 && applies==before_apply+6 && palette_changes==before_palette+1
        && lookups==before_lookup && row_binds==before_bind,
        "same association still restores three members and rebuilds fresh resource/geometry without redundant lookup or palette restore");
    const auto switch_captures=captures,switch_composes=composes,switch_tints=tint_calls,switch_applies=applies;
    const auto switch_preflights=preflight_batches;
    key(lease);finish_preparation(lease);
    require(runtime::state.applied==2 && captures==switch_captures && composes==switch_composes+1
        && tint_calls==switch_tints && applies==switch_applies+3 && preflight_batches>switch_preflights
        && visuals.at(row_leaf(row,"Btn#0"))==9 && visuals.at(row_leaf(row,"Btn#1"))==originals.at(row_leaf(row,"Btn#1"))
        && visuals.at(row_leaf(row,"Cell"))==originals.at(row_leaf(row,"Cell")),
        "whole peer switch uses grouped preflight, existing baselines and exact omitted-member restoration");
    std::cout<<"runtime native batches resolve-max="<<resolve_max_members<<" preflight-max-images="<<preflight_max_images
        <<" first capture/compose/tint/write=3/3/1/3 refresh=3/3/1/6 peer=0/1/0/3\n";
    close(lease);
}
int main() {
    reset(); auto lease = gate.try_enter(); current_lease = &lease;
    core::Config config;
    require(runtime::gesture_key(config) == 6, "configured F6");
    config.theme_function_key = 0; require(!runtime::gesture_key(config), "disabled key");
    config.theme_function_key = 8; require(!runtime::gesture_key(config), "autoplay conflict");
    config.theme_function_key = 7; require(!runtime::gesture_key(config), "preview conflict");
    config.preview_mode = core::PreviewMode::Off; require(runtime::gesture_key(config) == 7, "inactive preview no conflict");
    runtime_metrics_tests(lease);
    native_batch_runtime_tests(lease);
    executable_mapping_route_tests();
    runtime_phase_guard_tests(lease);
    original_palette_quiet_tests(lease);
    continuation_handoff_tests(lease);
    opening_wake_and_changed_rows_tests(lease);
    indexed_song_row_tests(lease);
    demand_preparation_tests(lease);
    projection_scale_tests(lease);
    dynamic_cache_tests(lease);
    preview_program_tests(lease);
    cursor_program_tests(lease);
    preparation_facts_tests(lease);
    pending_semantic_tests(lease); panel_feedback_contract_tests(lease); incremental_preparation_tests(lease); realistic_inventory_tests(lease); realistic_inventory_tests(lease, false); reset();
    std::string reason;
    std::uint64_t total = 8;
    auto unsupported = definition("palette"); unsupported.colors["SpeedIcon/SpeedIcon.ColorAndOpacity"] = {};
    require(runtime::retain(std::move(unsupported), total, reason) && runtime::state.themes.back().backend
        && !runtime::state.themes.back().available && !runtime::state.themes.back().program.colors.empty(),
        "keep complete compiled speed palette pending actual live admission, never partially applied");
    unsupported = definition("state"); unsupported.images["MusicList/Bg_1@selected"] = {};
    const auto count = runtime::state.themes.size();
    require(!runtime::retain(std::move(unsupported), total, reason) && runtime::state.themes.size() == count,
        "real compiler rejects malformed selectors without changing valid inventory");
    reset(); empty_inventory(); total = 0;
    unsigned bindings = 0;
    for (; bindings < 20; ++bindings) {
        auto d = images("tiny", {}); d.id = "theme_" + std::to_string(bindings);
        d.images["MusicList/Bg_" + std::to_string(bindings)] = {{1, 2, 3, 4}, 1, 1};
        if (!runtime::retain(std::move(d), total, reason)) break;
    }
    require(bindings == 20 && runtime::state.resources.size() == 1,
        "CPU inventory does not reserve hypothetical future menu generations");
    open(lease);
    constexpr std::size_t generations = 27;
    for (std::size_t i = 1; i < generations; ++i) {
        close(lease); ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease);
    }
    require(!runtime::state.compromised.load() && retained_images == 1 + generations * 2 * bindings && retained_images > 1024
        && creates == 1 && captures == generations * bindings && composes == generations * bindings,
        "more than former retained-record/reopen ceiling is admitted by actual bytes, textures upload once");
    // Native's Budget outcome owns admission. This adapter models known payload charges;
    // native foundation tests separately exercise its carrier/ledger allocation accounting.
    reset();
    retained_bytes = native::kRetainedByteLimit - 2 * native::kOriginalCaptureByteCharge - 4;
    runtime::state.native_attempts = 100000; runtime::state.native_attempted_bytes = UINT64_MAX;
    open(lease);
    require(retained_bytes == native::kRetainedByteLimit
        && creates == 1 && captures == 1 && composes == 1 && runtime::state.themes[1].available
        && !runtime::state.themes[2].available && runtime::state.resources[1].result.outcome == native::Outcome::Budget
        && runtime::state.themes[0].available && !runtime::state.compromised.load() && !applies && !saves,
        "actual byte exhaustion rejects only candidate, with no hypothetical headroom or attempted-work quota");
    reset(); empty_inventory();
    std::uint64_t preflight_bytes = 0;
    require(runtime::retain(images("preflight", {{"MusicList/Bg_1", 1}, {"MusicList/Bg_2", 2}}), preflight_bytes, reason),
        "prepare complete preflight fixture");
    missing_leaf = "Bg_2"; open(lease);
    require(lookups == 2 && !captures && !creates && !composes && !applies && !saves
        && !runtime::state.themes[1].available && visuals == originals && runtime::state.themes[1].admission_stage
        && std::string_view(runtime::state.themes[1].admission_stage) == "fake_missing_image",
        "all-target resolution rejects missing peer before any owning capture or factory");
    reset(); key(lease); require(!applies && !creates && !original_calls, "early polling never invokes native or original input");
    open(lease); require(lookups == 1 && captures == 1 && creates == 2 && composes == 2 && !applies
        && visuals == originals && runtime::state.themes[1].available && runtime::state.themes[2].available,
        "queued preparation resolves once, native capture revalidates, and leaves actual originals unchanged");
    sample(true, true); require(!queued_ticket && !applies, "held through Ready never schedules a delayed gesture");
    control = false; key(lease); require(!applies, "Ctrl required"); control = true;
    key(lease); require(applies == 1 && saves == 1 && saved == "night" && creates == 2 && composes == 2
        && lookups == 1 && captures == 1, "cached switch and successful persistence without lookup/capture/factory");
    fail_save = true; key(lease); require(applies == 2 && saves == 2 && saved == "night"
        && runtime::state.applied == 2 && runtime::state.selected == "sun", "write failure honest, successful application retained");
    close(lease); require(!displayed && !runtime::state.owner && applies == 3, "qualified close restores original and retires");
    ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(creates == 2 && lookups == 2 && captures == 2 && composes == 4 && runtime::state.applied == 2,
        "immediate reopen cache reuse and remembered successful choice");
    ++current.generation; key(lease); require(applies == 4, "stale input cannot mutate");
    open(lease); require(runtime::state.compromised.load() && captures == 2, "missed close never captures mutated original");
    reset(); open(lease); sample(true, true); const auto old_ticket = queued_ticket;
    require(old_ticket && !applies && !saves, "poll only queues, no mutation or persistence");
    close(lease); ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease); deliver(lease, old_ticket);
    require(!applies && !saves && !runtime::state.pending.ticket, "queued old gesture cannot migrate across close/reopen");
    sample(true, true); require(!queued_ticket, "held across close/reopen never retriggers");
    key(lease); require(applies == 1 && saves == 1, "new owner accepts release/repress immediately");
    const auto consumed = runtime::state.sequence; deliver(lease, consumed);
    require(applies == 1 && saves == 1, "duplicate consumed ticket rejected");
    reset(); open(lease); sample(false, true); sample(true, true);
    require(!queued_ticket, "focus entry held chord suppressed");
    sample(true, false); sample(true, true); sample(false, true); sample(true, true); deliver(lease);
    require(!applies && !saves && !runtime::state.compromised.load(), "queued request invalidated by focus loss even after regain");
    key(lease); require(applies == 1, "focus-loss rejection permits a fresh edge");
    reset(); open(lease); sample(true, true);
    const auto epoch = runtime::state.focus_epoch.load();
    const auto calls_before_loss = creates + lookups + captures + composes + borrows + applies;
    {
        std::lock_guard lock(runtime::state.mutex);
        runtime::focus_lost(); // Neither a runtime lock nor native access is permitted here.
    }
    require(runtime::state.held.load() && !runtime::state.focused.load()
        && runtime::state.focus_epoch.load() == epoch + 1
        && calls_before_loss == creates + lookups + captures + composes + borrows + applies,
        "focus notification only atomically revokes authority, preserving held chord");
    // Window focus has already returned; no poll observed either notification.
    deliver(lease);
    require(!applies && !saves && !runtime::state.compromised.load(), "between-polls loss/regain rejects old queued message without saving");
    sample(true, true); require(!queued_ticket, "held return after notification never retriggers");
    key(lease); require(applies == 1 && saves == 1, "release/new edge works after notification rejection");
    reset(); open(lease); sample(true, true); runtime::focus_lost(); sample(true, true); deliver(lease);
    require(!applies && !saves, "regained-focus poll before delivery cannot renew old ticket epoch");
    reset(); open(lease); fail_post = true; sample(true, true);
    require(!queued_ticket && !applies, "post failure never mutates");
    fail_post = false; key(lease); require(applies == 1, "post failure permits release/repress retry");
    reset(); open(lease); const auto sequence_before_edge = runtime::state.sequence;
    sample(true, true); const auto coalesced_ticket = queued_ticket; sample(true, false); sample(true, true);
    require(runtime::state.sequence == sequence_before_edge + 1 && queued_ticket == coalesced_ticket
        && runtime::state.pending.ticket == coalesced_ticket,
        "one bounded pending request, further edges coalesce until dispatch");
    deliver(lease); require(applies == 1, "coalesced request applies once");
    reset(); open(lease); sample(true, true); gc = true; deliver(lease);
    require(!applies && !saves && runtime::state.compromised.load(), "posted scope rechecks authenticated GC facts");
    reset(); open(lease); fail_native = true; key(lease);
    require(!saves && !displayed && !runtime::state.compromised.load(), "failed preflight writes nothing and preserves unrelated programs");
    reset(); open(lease); sample(true, true); runtime::retire(); deliver(lease);
    require(!applies && !saves && !runtime::state.owner, "terminal retirement invalidates queued request without stale restore");
    reset(); open(lease); sample(true, true); runtime::disable(); deliver(lease);
    require(!applies && !saves && !runtime::state.owner, "shutdown invalidates already queued request");
    reset(); open(lease); sample(true, true);
    runtime::posted_input(reinterpret_cast<void*>(1), queued_ticket, lease);
    require(!applies && runtime::state.compromised.load(), "WndProc/thread snapshots cannot replace private current-stack identity");
    reset(); gc = true; open(lease); require(!lookups && runtime::state.compromised.load(), "GC facts do not authorize mutation");
    reset(); wrong_tls = true; open(lease); require(!lookups, "wrong TLS role");
    reset(); purge = true; open(lease); require(!lookups, "purge exclusion");
    reset(); incomplete = true; open(lease); require(!lookups, "incomplete facts");
    reset(); auto bad_frame = caller(runtime::Source::Ready) + 1;
    runtime::ready(current.list, current.generation, &bad_frame, lease); require(!lookups, "actual exact native caller required");
    reset(); const auto stale_frame = caller(runtime::Source::Ready);
    runtime::ready(current.list, current.generation - 1, &stale_frame, lease);
    require(!lookups && !captures, "notification belongs to its actual opening generation");
    reset(); fail_native = true; open(lease); require(!runtime::state.themes[1].available && !saves && !applies,
        "missing native bindings reject their programs without persistence or global poisoning");
    reset(); nested_create = true; open(lease);
    require(runtime::state.compromised.load() && !applies && original_calls == 1, "nested close during factory retires before subsequent calls");
    reset(); open(lease); nested_apply = true; key(lease);
    require(runtime::state.compromised.load() && !saves && original_calls == 1, "nested close during assignment no stale restore/save, close original once");
    reset(); open(lease); nested_selection = true; key(lease);
    require(runtime::state.compromised && !saves && selection_originals == 1 && !runtime::state.owner,
        "owned semantic selection entry retires mid-transition, forwards once, and forbids stale rollback or persistence");
    reset(); open(lease); key(lease); const auto previous_applies = applies;
    current.phase = game::MenuSessionPhase::Ending; close(lease);
    require(applies == previous_applies && runtime::state.compromised.load() && !runtime::state.owner,
        "lost close authority retires without stale restoration");
    reset(); open(lease); runtime::disable(); key(lease);
    require(!applies && runtime::state.compromised.load(), "disable revokes input, no native cleanup");
    reset(); current.phase = game::MenuSessionPhase::Opening;
    key(lease); key(lease); // Delayed readiness is a state, not a timed wait.
    require(!creates && !applies && !original_calls, "delayed Ready early gestures perform no native work");
    current.phase = game::MenuSessionPhase::Ready; open(lease); key(lease);
    require(creates == 2 && applies == 1 && saved == "night", "first gesture immediately after Ready works");
    close(lease); ++current.generation; current.phase = game::MenuSessionPhase::Opening;
    key(lease); require(!runtime::state.owner && creates == 2, "early reopen gesture no stale owner");
    current.phase = game::MenuSessionPhase::Ready; open(lease);
    require(creates == 2 && runtime::state.applied == 1, "delayed reopen reuses immutable cache and selection");
    reset(); runtime::state.key = 0; runtime::state.selected = "night";
    open(lease); key(lease);
    require(applies == 1 && saved == "night", "disabled gesture still loads a supported selection");
    reset(); runtime::state.key = 12; open(lease);
    require(runtime::function_key() == 12, "poller receives configured F12, not default F6");
    key(lease); require(applies == 1, "configured F12 sampled gesture");
    reset(); runtime::state.key = 0; open(lease);
    sample(true, false, true, false); deliver(lease);
    require(runtime::state.applied == 2 && saved == "sun" && saves == 1, "gamepad previous wraps original to last sorted theme with keyboard disabled");
    sample(true, false, true, false); deliver(lease);
    require(applies == 1 && saves == 1, "held gamepad does not repeat");
    sample(true, false); sample(true, false, true, false); deliver(lease);
    require(saved == "night" && runtime::state.applied == 1, "previous persists actual preceding ID");
    sample(true, false); sample(true, false, true, false); deliver(lease);
    require(saved == "original" && runtime::state.applied == 0, "previous includes original sentinel");
    for (const auto* id : {"night", "sun", "original"}) {
        sample(true, false); sample(true, false, false, true); deliver(lease);
        require(saved == id, "next follows sorted themes and wraps with actual successful persistence");
    }
    const auto conflict_applies = applies, conflict_saves = saves;
    sample(true, false); sample(true, false, true, true); deliver(lease);
    sample(true, false, false, true); deliver(lease);
    require(applies == conflict_applies && saves == conflict_saves && !queued_ticket,
        "both-trigger conflict blocks the whole hold, including releasing only one trigger");
    sample(true, false); sample(true, false, false, true); deliver(lease);
    require(saved == "night", "full trigger release permits new next edge while L3 remains held");
    sample(true, false); sample(true, false, true, false); runtime::focus_lost(); deliver(lease);
    require(saved == "night", "focus notification revokes queued previous without persisting");
    sample(true, false, true, false); require(!queued_ticket, "held gamepad focus return suppressed");
    sample(true, false); sample(true, false, true, false); deliver(lease);
    require(saved == "original", "fresh previous after focus loss works");
    sample(true, false); sample(true, false, false, true);
    const auto gamepad_ticket = queued_ticket;
    const auto saves_before_close = saves;
    close(lease); ++current.generation; current.phase = game::MenuSessionPhase::Ready; open(lease);
    deliver(lease, gamepad_ticket);
    require(saved == "original" && saves == saves_before_close && runtime::state.applied == 0,
        "queued gamepad direction cannot migrate to a reopened owner");
    reset(); runtime::state.selected = "not_prepared"; open(lease);
    require(!applies && !saves && !displayed, "unavailable loaded selection never claims partial application");
    reset(); empty_inventory(); runtime::state.configured = false; runtime::state.themes.clear();
    const auto missing = std::filesystem::temp_directory_path() / (L"ff7rp-theme-runtime-missing-" + std::to_wstring(GetCurrentProcessId()));
    config = {}; runtime::configure(fake_module, missing, L"fake.ini", config);
    require(runtime::state.themes.size() == 1 && !creates && !lookups && !captures,
        "missing Themes directory optional, configure never invokes game APIs");
    reset(); empty_inventory(); runtime::state.configured = false; runtime::state.themes.clear();
    const auto directory = std::filesystem::temp_directory_path() / (L"ff7rp-theme-runtime-"
        + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    for (const auto* id : {L"sun", L"night", L"unsupported", L"invalid"}) std::filesystem::create_directories(directory / id);
    config = {}; config.selected_theme = "night"; config.autoplay_function_key = 6;
    runtime::configure(fake_module, directory, L"fake.ini", config);
    require(runtime::state.themes.size() == 4 && runtime::state.themes[0].id == "original"
        && runtime::state.themes[1].id == "night" && runtime::state.themes[2].id == "sun"
        && runtime::state.themes[3].backend && !runtime::state.themes[3].available
        && !creates && !lookups && !captures && bound_entry && bound_input && !runtime::state.key,
        "worker prepares sorted supported inventory, rejects invalid/unsupported, disables only conflicting gesture");
    open(lease); key(lease);
    require(applies == 1 && saved == "night", "configured selection applies even with gesture conflict");
    std::filesystem::remove_all(directory);
    reset(); empty_inventory(); runtime::state.configured = false; runtime::state.themes.clear(); reject_loads = true;
    constexpr std::size_t rejected_candidates = 264;
    for (std::size_t i = 0; i < rejected_candidates; ++i)
        std::filesystem::create_directories(directory / ("rejected_" + std::to_string(i)));
    runtime::configure(fake_module, directory, L"fake.ini", {});
    require(loads == rejected_candidates && runtime::state.themes.size() > 1
        && !creates && !captures && !lookups && bound_input,
        "malformed peers do not truncate later candidate loading at a count quota");
    require(std::none_of(messages.begin(), messages.end(), [](const auto& message) {
        return message.find("enumeration_attempt_limit_truncated") != std::string::npos;
    }), "inventory is not silently truncated by count");
    std::filesystem::remove_all(directory);
    reset(); empty_inventory(); runtime::state.configured = false; runtime::state.themes.clear();
    constexpr std::size_t many_themes = 1100;
    for (std::size_t i = 0; i < many_themes; ++i) std::filesystem::create_directories(directory / ("theme_" + std::to_string(i)));
    runtime::configure(fake_module, directory, L"fake.ini", {});
    require(loads == many_themes && runtime::state.themes.size() == many_themes + 1 && runtime::state.resources.size() == 1
        && !creates && !captures && !lookups, "more than former theme/entry quotas load on worker with real shared pixels");
    std::filesystem::remove_all(directory);
    multi_target_tests(lease);
    generic_palette_row_tests(lease);
    song_row_tests(lease);
    song_failure_tests(lease);
    speed_program_tests(lease);
    tint_program_tests(lease);
    shared_subject_work_tests(lease);
    same_association_refresh_tests(lease);
    capture_member_runtime_tests(lease);
    batch_runtime_tests(lease);
    std::cout << "menu_theme_runtime_selftest passed (fake native UI only)\n";
}
