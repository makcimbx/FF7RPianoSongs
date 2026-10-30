// Same-TU actual palette reader/controller test; only native ABI and MinHook facts
// are fake. Link core/hooks.cpp + core/pe_image.cpp, NOT MinHook or version_gate.cpp.
#include "game/menu_theme_palette.h"
#include "game/hook_specs.h"
#include "core/pe_image.h"
#include <MinHook.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <new>
#include <vector>

namespace {
std::atomic<int> allocations_before_failure{-1};
}
// Exercise real container allocation failure, not a successful admission stub.
void* operator new(std::size_t bytes) {
    auto remaining = allocations_before_failure.load();
    while (remaining >= 0) {
        if (allocations_before_failure.compare_exchange_weak(remaining, remaining - 1)) {
            if (remaining == 0) throw std::bad_alloc();
            break;
        }
    }
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
HMODULE module = nullptr;
unsigned palette_patch_writes = 0;
bool fail_property_write = false;
SIZE_T WINAPI mapped_query(LPCVOID, PMEMORY_BASIC_INFORMATION info, SIZE_T) {
    *info = {}; info->AllocationBase = module; info->State = MEM_COMMIT;
    info->Protect = PAGE_EXECUTE_READ; return sizeof(*info);
}
}
namespace ff7r::piano::core {
template<class T> bool checked_palette_field_write(void* object, std::size_t offset, const T& value) {
    return !fail_property_write && safe_write_field(object, offset, value);
}
bool counted_palette_patch_write(void* destination, const void* source, size_t bytes) {
    ++palette_patch_writes;
    return safe_write_bytes(destination, source, bytes);
}
}
#define VirtualQuery mapped_query
#define safe_write_bytes counted_palette_patch_write
#define safe_write_field checked_palette_field_write
#include "game/menu_theme_palette.cpp"
#undef safe_write_field
#undef safe_write_bytes
#undef VirtualQuery

namespace palette = ff7r::piano::game::menu_theme_palette;
namespace game = ff7r::piano::game;
namespace core = ff7r::piano::core;
namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "menu_theme_palette_selftest: " << message << '\n'; std::exit(1); }
}
void ready(palette::Result r, const char* message) { require(bool(r), message); }
alignas(16) std::array<unsigned char, 8192> image{};
std::map<std::string, game::RvaSignatureSpec> specs;
core::HookCallbackGate shared_gate;
game::MenuSessionSnapshot session{};
bool image_supported = true, qualify = true, current_class = true, strong_member = true;
bool fresh_identity = true, reader_ready = true, fault_copy = false, fault_setter = false;
bool share_proofs = true, phase_eligible = true, reader_qualifies = false;
std::atomic<std::uint64_t> qualifier_calls{0};
enum class PhaseLoss { None, Gc, Session, Identity, Revoked };
PhaseLoss phase_loss = PhaseLoss::None;
enum class CopyAction { None, AuthorityLoss, IdentityLoss, Retire, Forward };
CopyAction copy_action = CopyAction::None;
palette::Property copy_property = palette::Property::Text;
palette::Authority authority();
palette::Subject subject(palette::Property);
void invoke_color(std::size_t, void*, void*);
void* fault_assign_destination = nullptr;
unsigned assigns_until_fault = 0;
bool fail_disable = false, fail_remove = false, fail_enable = false, lose_after_setter = false;
bool reenter = false;
enum class Reentry { None, Retire, RetireSubject, RetireMatching, Color, Opacity };
Reentry same_reentry = Reentry::None;
void* reentry_match_context = nullptr;
bool retirement_match(void*, const palette::Subject&) noexcept;
bool context_live = true;
palette::Source* revoked = nullptr;
palette::NativeInvocation saved_invocation{};
bool inspect_invocation = false;
std::atomic<std::uint64_t> reader_calls{0};
std::mutex forward_mutex;
std::condition_variable forward_changed;
bool pause_foreign = false, foreign_pending = false, release_foreign = false;
void before_native_write() {
    if (GetCurrentThreadId() == palette::state.thread) return;
    std::unique_lock lock(forward_mutex);
    if (!pause_foreign) return;
    foreign_pending = true;
    forward_changed.notify_all();
    require(forward_changed.wait_for(lock, std::chrono::seconds(5), [] { return release_foreign; }),
        "forward-overlap test release failure bound");
    pause_foreign = false;
}
unsigned install_count = 0, fail_install = 0, setter_calls = 0, consumes = 0;
unsigned copies = 0, assignments = 0, destroys = 0, validators = 0;
std::map<void*, std::size_t> installed;
alignas(8) std::array<unsigned char, 0x500> widget{}, held_widget{};
struct Controller {
    std::uint64_t unused = 0;
    int references = 1;
    palette::Linear value{};
};
Controller* controller(const void* p) {
    Controller* out = nullptr;
    std::memcpy(&out, static_cast<const unsigned char*>(p) + 0x20, sizeof(out)); return out;
}
void put_controller(palette::NativeColor& p, Controller* c) {
    c->value = palette::rgba(p);
    void* shared = &c->value;
    std::memcpy(p.bytes.data() + 0x18, &shared, sizeof(shared));
    std::memcpy(p.bytes.data() + 0x20, &c, sizeof(c));
    p.bytes[0x10] = 3; // Linked owning color, not specified/constant.
}
void __fastcall native_destroy(void* p) {
    ++destroys;
    if (auto* c = controller(p)) { require(c->references > 0, "never double release"); --c->references; }
    // The real destructor does not zero storage. Tests intentionally do not either.
}
void* __fastcall native_copy(void* dst, const void* src) {
    if (fault_copy) RaiseException(0xe0001234, 0, 0, nullptr);
    ++copies;
    std::memcpy(dst, src, 40);
    if (auto* c = controller(dst)) ++c->references;
    const auto action = std::exchange(copy_action, CopyAction::None);
    if (action == CopyAction::AuthorityLoss) qualify = false;
    else if (action == CopyAction::IdentityLoss) fresh_identity = false;
    else if (action == CopyAction::Retire) {
        revoked = palette::find(widget.data(), copy_property);
        const auto retired = palette::retire_subject(subject(copy_property), authority());
        require(!retired && retired.outcome == palette::Outcome::Authority
            && std::strcmp(retired.stage, "retired_reentrant_orphan") == 0
            && revoked && revoked->occupied && revoked->initialized && !revoked->active
            && !revoked->subject.validate && !palette::find(widget.data(), copy_property),
            "copy-helper reentry detaches immediately but reports retained ownership, not successful native cleanup");
        context_live = false;
    } else if (action == CopyAction::Forward) {
        auto input = palette::constant({0.3f, 0.5f, 0.7f, 0.6f});
        invoke_color(palette::index(copy_property), widget.data(), &input);
    }
    return dst;
}
void* __fastcall native_assign(void* dst, const void* src) {
    if (dst == fault_assign_destination) {
        if (!assigns_until_fault) RaiseException(0xe0001237, 0, 0, nullptr);
        --assigns_until_fault;
    }
    ++assignments;
    palette::NativeColor local{};
    native_copy(&local, src); // retain before release, including self assignment
    native_destroy(dst);
    std::memcpy(dst, &local, 40);
    return dst;
}
struct FakeDelegate { unsigned identity; Controller* owner; };
unsigned delegates_live = 0, attribute_assignments = 0, attribute_destroys = 0, bottom_attribute_calls = 0;
bool fault_attribute = false, drift_attribute = false, revoke_attribute = false, retire_attribute = false, revoke_attribute_destroy = false;
alignas(8) std::array<unsigned char, 0x380> slate{};
alignas(8) std::array<unsigned char, 0x380> replacement_slate{};
int slate_controller = 0, replacement_controller = 0;
palette::NativeAttribute* live_attribute() { return reinterpret_cast<palette::NativeAttribute*>(slate.data() + 0x340); }
FakeDelegate* attribute_delegate(const void* p) {
    FakeDelegate* result = nullptr;
    std::memcpy(&result, static_cast<const unsigned char*>(p) + 0x30, sizeof(result)); return result;
}
FakeDelegate* clone_delegate(const FakeDelegate& source) {
    auto* result = new FakeDelegate(source);
    if (result->owner) ++result->owner->references;
    ++delegates_live; return result;
}
void __fastcall native_attribute_destroy(void* p) {
    ++attribute_destroys;
    native_destroy(p);
    if (auto* delegate = attribute_delegate(p)) {
        if (delegate->owner) { require(delegate->owner->references > 0, "delegate captured owner releases once"); --delegate->owner->references; }
        delete delegate; --delegates_live;
    }
    if (revoke_attribute_destroy) { revoke_attribute_destroy = false; qualify = false; }
}
void* __fastcall native_attribute_assign(void* dst, const void* src) {
    if (fault_attribute) RaiseException(0xe0001236, 0, 0, nullptr);
    ++attribute_assignments;
    // Fake the actual native owning ABI: clone delegate and retain color before release.
    auto* delegate = attribute_delegate(src);
    auto* cloned = delegate ? clone_delegate(*delegate) : nullptr;
    palette::NativeColor color{}; native_copy(&color, src);
    native_attribute_destroy(dst);
    std::memcpy(dst, src, sizeof(palette::NativeAttribute));
    std::memcpy(dst, &color, sizeof(color));
    std::memcpy(static_cast<unsigned char*>(dst) + 0x30, &cloned, sizeof(cloned));
    return dst;
}
void __fastcall native_bottom_attribute_setter(void* target, void* attribute) {
    ++bottom_attribute_calls;
    const auto before = *static_cast<palette::NativeAttribute*>(attribute);
    const auto consumed = consumes;
    native_attribute_assign(static_cast<unsigned char*>(target) + 0x340, attribute);
    require(std::memcmp(&before, attribute, sizeof(before)) == 0 && consumes == consumed,
        "Slate attribute setter borrows input, never consumes or changes it");
    if (drift_attribute) {
        drift_attribute = false;
        palette::SlatePair pair{replacement_slate.data(), &replacement_controller};
        std::memcpy(widget.data() + 0x220, &pair, sizeof(pair));
    }
    if (retire_attribute) {
        retire_attribute = false;
        revoked = palette::find(widget.data(), palette::Property::TextBottom);
        require(!palette::retire_subject(subject(palette::Property::TextBottom), authority()), "attribute restoration reentrant retirement revokes immediately");
        context_live = false;
    }
    if (revoke_attribute) { revoke_attribute = false; qualify = false; }
}
bool qualifier(void*, const game::MenuSessionSnapshot& s) noexcept {
    qualifier_calls.fetch_add(1);
    return qualify && game::same_menu_session(s, session);
}
bool phase_guard(void* context, const game::MenuSessionSnapshot& s) noexcept {
    return phase_eligible && game::same_menu_session(s, session)
        && (!context || palette::invocation_matches(*static_cast<palette::NativeInvocation*>(context)));
}
palette::Authority authority() {
    return {session, GetCurrentThreadId(), nullptr, &qualifier, share_proofs ? &phase_guard : nullptr};
}
bool native_qualifier(void* context, const game::MenuSessionSnapshot& s) noexcept {
    return context && palette::invocation_matches(*static_cast<palette::NativeInvocation*>(context))
        && qualifier(nullptr, s);
}
bool reader(void*, const palette::NativeInvocation& invocation, palette::Authority& a) noexcept {
    reader_calls.fetch_add(1);
    require(palette::invocation_matches(invocation) && invocation.return_slot && invocation.caller,
        "reader sees actual detour frame, not invented open/input provenance");
    saved_invocation = invocation;
    if (std::exchange(inspect_invocation, false)) {
        auto forged = invocation;
        forged.thread ^= 1u;
        require(!palette::invocation_matches(forged), "a copied invocation cannot forge a different thread on the same stack/serial");
        forged = invocation; forged.serial ^= 1u;
        require(!palette::invocation_matches(forged), "same-stack invocation requires the exact private serial");
        std::thread foreign([saved = invocation] {
            palette::NativeInvocation current{};
            require(!palette::current_invocation(current) && !palette::invocation_matches(saved),
                "a saved invocation cannot cross threads without the actual private TLS scope");
            auto foreign_forged = saved; foreign_forged.thread = GetCurrentThreadId();
            require(!palette::invocation_matches(foreign_forged), "matching the foreign OS thread cannot manufacture private TLS authority");
        });
        foreign.join();
        require(palette::invocation_matches(invocation), "same-stack exact thread/serial witness survives unrelated foreign observation");
    }
    if (!reader_ready) return false;
    a = authority();
    // Borrow the real detour's scope, whose lifetime encloses all uses of a.
    a.context = const_cast<palette::NativeInvocation*>(&invocation);
    a.qualify_current_callback = &native_qualifier;
    return !reader_qualifies || native_qualifier(a.context, a.session);
}
bool validator(void* context, const palette::Subject& s, const game::MenuSessionSnapshot& current) noexcept {
    require(context_live, "revoked validator context is NEVER called after retirement returns");
    ++validators;
    return context == widget.data() && s.widget == widget.data() && s.owner == current.widget
        && s.generation == current.generation && current_class && strong_member && fresh_identity;
}
palette::Subject subject(palette::Property property) {
    return {widget.data(), session.widget, session.generation, property, widget.data(), &validator};
}
palette::Policy policy() {
    palette::Policy p;
    p.normal = {true, {0.1f, 0.2f, 0.3f, 0.5f}};
    p.selected = {true, {0.7f, 0.6f, 0.5f, 0.25f}};
    return p;
}
palette::Linear read_color(palette::Property p, const void* target = widget.data()) {
    palette::Linear out{};
    const auto offset = palette::contracts[palette::index(p)].offset;
    require(core::safe_copy_bytes(static_cast<const unsigned char*>(target) + offset, &out, sizeof(out)), "actual property read");
    return out;
}
// Fake Slate storage is separate from documented UObject fields. These test-only
// offsets fit every fixture widget; owning text presentation uses native assignment.
constexpr std::size_t presentation_text = 0x4b0;
void* presentation_address(palette::Property p, void* target) {
    if (p == palette::Property::Text) return static_cast<unsigned char*>(target) + presentation_text;
    if (p == palette::Property::TextBottom) {
        palette::SlatePair pair{}; std::memcpy(&pair, static_cast<unsigned char*>(target) + 0x220, sizeof(pair));
        return static_cast<unsigned char*>(pair.slate) + 0x340;
    }
    return static_cast<unsigned char*>(target) + 0x400 + palette::index(p) * sizeof(palette::Linear);
}
palette::Linear read_presentation(palette::Property p, void* target = widget.data()) {
    palette::Linear value{}; std::memcpy(&value, presentation_address(p, target), sizeof(value)); return value;
}
void destroy_text_widget(void* target) {
    native_destroy(static_cast<unsigned char*>(target) + 0x148);
    native_destroy(static_cast<unsigned char*>(target) + presentation_text);
    std::memset(static_cast<unsigned char*>(target) + presentation_text, 0, sizeof(palette::NativeColor));
}
void expect(palette::Property p, palette::Linear color, const char* message) {
    require(read_presentation(p) == color, message);
}
void invoke_color(std::size_t method, void* target, void* input) {
    reinterpret_cast<palette::Setter>(palette::detours[method])(target, input);
}
void invoke_opacity(std::size_t method, void* target, float alpha) {
    reinterpret_cast<palette::Opacity>(palette::detours[method])(target, alpha);
}
template<std::size_t I> void __fastcall native_setter(void* target, void* value) {
    before_native_write();
    ++setter_calls;
    if (fault_setter) RaiseException(0xe0001235, 0, 0, nullptr);
    const auto& c = palette::contracts[I];
    auto* dst = static_cast<unsigned char*>(target) + c.offset;
    if (c.owning) {
        native_assign(dst, value);
        if constexpr (I == 7) native_assign(presentation_address(c.property, target), value);
        if constexpr (I == 8) {
            palette::SlatePair pair{}; std::memcpy(&pair, static_cast<unsigned char*>(target) + 0x220, sizeof(pair));
            if (pair.slate) {
                palette::NativeAttribute attribute{};
                native_copy(&attribute, value);
                attribute.bytes[0x28] = 1; // Actual native bottom setter always establishes set/unbound state.
                native_bottom_attribute_setter(pair.slate, &attribute);
                native_attribute_destroy(&attribute);
            }
        }
        native_destroy(value); ++consumes;
    }
    else {
        std::memcpy(dst, value, 16);
        std::memcpy(presentation_address(c.property, target), value, 16);
    }
    const auto action = same_reentry; same_reentry = Reentry::None;
    if (action == Reentry::Retire || action == Reentry::RetireSubject) {
        revoked = palette::find(target, c.property);
        require(revoked != nullptr, "reentrant retire targets registered source");
        require(!(action == Reentry::RetireSubject
            ? palette::retire_subject(subject(c.property), authority())
            : palette::retire(session.widget, session.generation, authority())),
            "reentrant retirement reports orphan ownership");
        context_live = false; // Owner releases context immediately after return.
        require(!palette::bind(subject(c.property), policy(), authority()), "slot cannot be rebound under active callback");
    } else if (action == Reentry::RetireMatching) {
        revoked = palette::find(target, c.property);
        require(!palette::retire_matching(session.widget, session.generation, &retirement_match,
            reentry_match_context, authority()), "reentrant matching retirement revokes without native cleanup");
        context_live = false;
    } else if (action == Reentry::Color) {
        auto fresh = palette::constant({0.33f, 0.44f, 0.55f, 0.66f});
        invoke_color(I, target, &fresh);
    } else if (action == Reentry::Opacity) {
        invoke_opacity(c.owning ? (c.property == palette::Property::Text ? 15 : 16) : 11, target, 0.77f);
    }
    if (lose_after_setter) qualify = false;
    if (phase_loss == PhaseLoss::Gc) qualify = false;
    if (phase_loss == PhaseLoss::Session) ++session.generation;
    if (phase_loss == PhaseLoss::Identity) fresh_identity = false;
    if (phase_loss == PhaseLoss::Revoked) phase_eligible = false;
    if (reenter) {
        reenter = false;
        palette::Linear unrelated{0.9f, 0.8f, 0.7f, 0.6f};
        invoke_color(0, held_widget.data(), &unrelated);
    }
}
template<std::size_t I> void __fastcall native_opacity(void* target, float alpha) {
    before_native_write();
    ++setter_calls;
    const auto& c = palette::contracts[I];
    if (c.owning) {
        auto source = read_color(c.property, target);
        void* shared = nullptr;
        std::memcpy(&shared, static_cast<unsigned char*>(target) + c.offset + 0x18, sizeof(shared));
        if (shared) require(core::safe_copy_bytes(shared, &source, sizeof(source)), "native text opacity reads live shared color");
        auto value = palette::constant(source);
        auto rgba = palette::rgba(value); rgba[3] = alpha; value = palette::constant(rgba);
        invoke_color(palette::index(c.property), target, &value);
    } else {
        std::memcpy(static_cast<unsigned char*>(target) + c.offset + 12, &alpha, sizeof(alpha));
        std::memcpy(presentation_address(c.property, target), static_cast<unsigned char*>(target) + c.offset, sizeof(palette::Linear));
        if (lose_after_setter) qualify = false;
    }
}
const std::array<void*, palette::kMethods> natives{
    reinterpret_cast<void*>(&native_setter<0>), reinterpret_cast<void*>(&native_setter<1>),
    reinterpret_cast<void*>(&native_setter<2>), reinterpret_cast<void*>(&native_setter<3>),
    reinterpret_cast<void*>(&native_setter<4>), reinterpret_cast<void*>(&native_setter<5>),
    reinterpret_cast<void*>(&native_setter<6>), reinterpret_cast<void*>(&native_setter<7>),
    reinterpret_cast<void*>(&native_setter<8>), reinterpret_cast<void*>(&native_setter<9>),
    reinterpret_cast<void*>(&native_setter<10>), reinterpret_cast<void*>(&native_opacity<11>),
    reinterpret_cast<void*>(&native_opacity<12>), reinterpret_cast<void*>(&native_opacity<13>),
    reinterpret_cast<void*>(&native_opacity<14>), reinterpret_cast<void*>(&native_opacity<15>),
    reinterpret_cast<void*>(&native_opacity<16>),
};
void prepare_catalog() {
    module = reinterpret_cast<HMODULE>(image.data());
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 256;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image.data() + 256);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->OptionalHeader.SizeOfImage = unsigned(image.size());
    std::uintptr_t offset = 1024;
    const auto add = [&](const char* id) {
        image[offset] = 0xc3;
        specs.emplace(id, game::RvaSignatureSpec{id, offset, {0xc3}});
        offset += 16;
    };
    for (const auto& c : palette::contracts) add(c.id);
    add("native_slate_color_copy_construct"); add("native_slate_color_assign"); add("native_slate_color_destroy");
    add("end_text_slate_set_bottom_attribute"); add("native_slate_color_attribute_assign"); add("native_slate_color_attribute_destroy");
}
void fake_native_functions() {
    // Narrow native-call substitution AFTER production catalog authentication.
    palette::state.copy = &native_copy; palette::state.assign = &native_assign;
    palette::state.destroy = &native_destroy;
    palette::state.attribute_assign = &native_attribute_assign;
    palette::state.attribute_destroy = &native_attribute_destroy;
    palette::state.bottom_attribute_setter = &native_bottom_attribute_setter;
}
void setup() {
    inspect_invocation = false;
    share_proofs = phase_eligible = true; reader_qualifies = false;
    phase_loss = PhaseLoss::None;
    qualify = current_class = strong_member = fresh_identity = reader_ready = true;
    fault_copy = fault_setter = reenter = lose_after_setter = false;
    fail_property_write = false;
    same_reentry = Reentry::None; context_live = true; revoked = nullptr;
    reentry_match_context = nullptr;
    fault_assign_destination = nullptr; assigns_until_fault = 0;
    copy_action = CopyAction::None;
    fault_attribute = drift_attribute = revoke_attribute = retire_attribute = revoke_attribute_destroy = false;
    palette::SlatePair pair{slate.data(), &slate_controller};
    std::memcpy(widget.data() + 0x220, &pair, sizeof(pair));
    palette::state.poisoned.store(false); // Test fixture only, production never clears a native fault.
    session.generation++; session.phase = game::MenuSessionPhase::Ready;
    session.widget = held_widget.data(); session.widget_identity = {1, 9};
    shared_gate.open();
    ready(palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, &reader}), "configure authenticates all actual IDs");
    fake_native_functions(); install_count = fail_install = 0;
    ready(palette::install(), "transactional install");
}
void finish() {
    ready(palette::retire(session.widget, session.generation, authority()), "qualified retire");
    native_attribute_destroy(live_attribute());
    *live_attribute() = palette::NativeAttribute{}; // Fake widget teardown/reconstruction, not production mutation.
    native_destroy(widget.data() + presentation_text);
    std::memset(widget.data() + presentation_text, 0, sizeof(palette::NativeColor));
    ready(palette::uninstall(), "disable drain remove");
    require(installed.empty(), "no retained hooks");
}
void initialize(palette::Property p, palette::Linear value) {
    const auto& c = palette::contracts[palette::index(p)];
    if (c.owning) {
        const auto v = palette::constant(value);
        std::memcpy(widget.data() + c.offset, &v, sizeof(v));
    } else std::memcpy(widget.data() + c.offset, &value, sizeof(value));
}
struct DynamicContext { void* widget = nullptr; bool live = true; unsigned group = 0, comparisons = 0; };
struct RetirementFilter { unsigned group = 0, comparisons = 0; std::uint32_t properties = 0; };
bool retirement_match(void* context, const palette::Subject& subject) noexcept {
    auto& filter = *static_cast<RetirementFilter*>(context);
    ++filter.comparisons;
    if (subject.context == widget.data()) return (filter.properties & (1u << palette::index(subject.property))) != 0;
    auto& metadata = *static_cast<DynamicContext*>(subject.context);
    require(metadata.live, "retirement predicate never sees a detached/freed context");
    ++metadata.comparisons;
    return metadata.group == filter.group; // Direct O(1) metadata, no requested-subject scan.
}
struct DynamicWidget {
    alignas(8) std::array<unsigned char, 0x500> bytes{};
    Controller original{}, animated{};
    std::unique_ptr<DynamicContext> context = std::make_unique<DynamicContext>();
    DynamicWidget() {
        animated.references = 0;
        context->widget = bytes.data();
        auto color = palette::constant({0.8f, 0.4f, 0.2f, 0.8f});
        put_controller(color, &original);
        std::memcpy(bytes.data() + palette::contracts[palette::index(palette::Property::Text)].offset,
            &color, sizeof(color));
    }
};
bool dynamic_validator(void* context, const palette::Subject& s, const game::MenuSessionSnapshot& current) noexcept {
    auto* c = static_cast<DynamicContext*>(context);
    require(c && c->live, "dynamic validator context survives until retirement returns");
    ++validators;
    return c->widget == s.widget && s.owner == current.widget && s.generation == current.generation
        && current_class && strong_member && fresh_identity;
}
palette::Subject dynamic_subject(DynamicWidget& w) {
    return {w.bytes.data(), session.widget, session.generation, palette::Property::Text, w.context.get(), &dynamic_validator};
}
void dynamic_source_regression() {
    // Greater than the removed 1024-source ceiling; this is a test size, not admission policy.
    constexpr std::size_t count = 1033;
    static std::unique_ptr<DynamicWidget> retained_orphan; // Fake native controller remains alive with its retained ref.
    palette::Source* orphan_source = nullptr;
    std::vector<std::unique_ptr<DynamicWidget>> widgets;
    for (std::size_t i = 0; i <= count; ++i) widgets.push_back(std::make_unique<DynamicWidget>());
    setup();
    require(palette::source_storage_count() == 0, "dynamic storage test runs before first source allocation");
    auto first = dynamic_subject(*widgets.front());
    auto calls = setter_calls, native_copies = copies, native_destroys = destroys;
    allocations_before_failure = 0;
    auto failed = palette::bind(first, policy(), authority());
    require(!failed && failed.outcome == palette::Outcome::Budget && allocations_before_failure.load() == -1
        && std::strcmp(failed.stage, "source_allocation") == 0, "real first-node allocation failure reported");
    require(!palette::find(first.widget, first.property) && palette::source_storage_count() == 0
        && calls == setter_calls && native_copies == copies && native_destroys == destroys
        && widgets.front()->original.references == 1, "allocation failure publishes no context and touches no native ownership");
    allocations_before_failure = 1; // Source allocation succeeds; pointer-vector growth fails.
    failed = palette::bind(first, policy(), authority());
    require(!failed && failed.outcome == palette::Outcome::Budget && allocations_before_failure.load() == -1
        && palette::source_storage_count() == 0 && !palette::find(first.widget, first.property)
        && calls == setter_calls && native_copies == copies && native_destroys == destroys,
        "index allocation failure unwinds unpublished node without native ownership or context publication");
    ready(palette::bind(first, policy(), authority()), "bind after allocation recovery");
    auto* stable = palette::find(first.widget, first.property);
    require(stable && widgets.front()->original.references == 2, "source and untinted UObject retain original controller independently");
    auto incoming = palette::constant({0.22f, 0.33f, 0.44f, 0.55f});
    widgets.front()->animated.references = 1;
    put_controller(incoming, &widgets.front()->animated);
    {
        std::lock_guard lock(forward_mutex);
        pause_foreign = true; foreign_pending = release_foreign = false;
    }
    std::thread foreign([&] { invoke_color(palette::index(first.property), first.widget, &incoming); });
    {
        std::unique_lock lock(forward_mutex);
        require(forward_changed.wait_for(lock, std::chrono::seconds(5), [] { return foreign_pending; }),
            "foreign consuming Original paused before write during dynamic growth");
    }
    for (std::size_t i = 1; i < count; ++i) {
        ready(palette::bind(dynamic_subject(*widgets[i]), policy(), authority()), "distinct owning source beyond old count ceiling");
        require(widgets[i]->original.references == 2, "every dynamic source and UObject retain original independently");
        if (i == 1) {
            auto orphan = dynamic_subject(*widgets[i]);
            orphan_source = palette::find(orphan.widget, orphan.property);
            qualify = false;
            require(!palette::retire_subject(orphan, authority()), "lost authority retains owning dynamic orphan");
            qualify = true;
            widgets[i]->context->live = false; widgets[i]->context.reset();
            retained_orphan = std::move(widgets[i]);
        }
    }
    require(palette::source_storage_count() == count && palette::find(first.widget, first.property) == stable
        && stable->forwarding.load() == 1, "Source and outstanding Forward pointers survive dynamic growth");
    require(orphan_source && orphan_source->occupied && !orphan_source->active && !orphan_source->subject.validate
        && controller(&orphan_source->native) == &retained_orphan->original && retained_orphan->original.references == 2,
        "growth preserves orphan controller ownership without retaining freed validator context");
    calls = setter_calls; native_copies = copies; native_destroys = destroys;
    allocations_before_failure = 0;
    failed = palette::bind(dynamic_subject(*widgets.back()), policy(), authority());
    require(!failed && failed.outcome == palette::Outcome::Budget && allocations_before_failure.load() == -1
        && palette::source_storage_count() == count && !palette::find(widgets.back()->bytes.data(), first.property)
        && calls == setter_calls && native_copies == copies && native_destroys == destroys,
        "later allocation failure preserves existing nodes, active callbacks and native owners");
    auto premature = palette::constant({0.9f, 0.6f, 0.3f, 0.8f});
    const auto consumed = consumes;
    invoke_color(palette::index(first.property), first.widget, &premature);
    require(consumes == consumed + 1 && !palette::fresh(*stable) && stable->forwarding.load() == 1,
        "qualified consuming input during growth cannot recover outstanding forwarded source");
    require(!palette::change(first, policy(), authority()) && !palette::restore(first, authority()),
        "outstanding foreign source remains stale across growth and allocation failure");
    {
        std::lock_guard lock(forward_mutex); release_foreign = true;
    }
    forward_changed.notify_all(); foreign.join();
    require(consumes == consumed + 2 && widgets.front()->animated.references == 2
        && stable->forwarding.load() == 0 && !palette::fresh(*stable) && !palette::restore(first, authority()),
        "late consuming Original consumes once and completion cannot bless stale source");
    const palette::Linear fresh{0.7f, 0.5f, 0.3f, 0.9f};
    auto recovery = palette::constant(fresh);
    invoke_color(palette::index(first.property), first.widget, &recovery);
    ready(palette::restore(first, authority()), "post-completion qualified source recovers after growth");
    require(read_color(first.property, first.widget) == fresh && widgets.front()->original.references == 0
        && widgets.front()->animated.references == 0, "fresh snapshot releases superseded controllers exactly once");
    ready(palette::retire(session.widget, session.generation, authority()), "retire all dynamic owning sources");
    for (std::size_t i = 0; i <= count; ++i) {
        if (!widgets[i]) continue;
        auto& w = *widgets[i];
        require(!palette::find(w.bytes.data(), first.property), "dynamic subject detached before context release");
        const auto* object_owner = controller(w.bytes.data() + 0x148);
        const auto* slate_owner = controller(presentation_address(first.property, w.bytes.data()));
        if (i == 0) require(!object_owner && !slate_owner && w.original.references == 0,
            "recovered constant source restores both destinations and releases superseded owner");
        else if (i == count) require(object_owner == &w.original && !slate_owner && w.original.references == 1,
            "failed allocation never registered or emitted: only initial UObject owns original");
        else require(object_owner == &w.original && slate_owner == &w.original && w.original.references == 2,
            "retired owning source restores independent UObject and Slate owners exactly");
        w.context->live = false; w.context.reset();
    }
    const auto checks = validators;
    const auto reads = reader_calls.load();
    auto unbound = palette::constant(fresh);
    invoke_color(palette::index(first.property), first.widget, &unbound);
    auto orphan_unbound = palette::constant(fresh);
    invoke_color(palette::index(first.property), retained_orphan->bytes.data(), &orphan_unbound);
    require(validators == checks && reader_calls.load() == reads, "retired freed contexts never consulted by callbacks");
    ready(palette::uninstall(), "drain dynamic storage without native cleanup");
    require(palette::source_storage_count() == count, "drained uninstall retains stable nodes, no count-based eviction");
    require(orphan_source->occupied && !orphan_source->subject.validate && retained_orphan->original.references == 1
        && controller(&orphan_source->native) == &retained_orphan->original
        && !controller(retained_orphan->bytes.data() + 0x148)
        && !controller(presentation_address(first.property, retained_orphan->bytes.data())),
        "drained uninstall does not destroy dynamic orphan or resurrect its freed context");
    setup();
    ready(palette::bind(subject(palette::Property::Image), policy(), authority()), "safely cleaned source node can recycle");
    require(palette::source_storage_count() == count, "safe reuse needs no extra source allocation");
    finish();
    for (auto& w : widgets) {
        if (!w) continue;
        destroy_text_widget(w->bytes.data());
        require(w->original.references == 0 && w->animated.references == 0,
            "fake widget lifetime cleanup has no leaked or double-released native color owner");
    }
}
void matching_retirement_regression() {
    setup();
    std::vector<std::unique_ptr<DynamicWidget>> rows;
    for (unsigned i = 0; i != 6; ++i) {
        rows.push_back(std::make_unique<DynamicWidget>());
        rows.back()->context->group = i % 3;
        ready(palette::bind(dynamic_subject(*rows.back()), policy(), authority()), "batch row binding");
    }
    initialize(palette::Property::Text, {0.9f, 0.8f, 0.7f, 1});
    initialize(palette::Property::TextBottom, {0.6f, 0.5f, 0.4f, 0.8f});
    auto primary = subject(palette::Property::Text), bottom = subject(palette::Property::TextBottom);
    ready(palette::bind(primary, policy(), authority()), "unrelated primary property");
    ready(palette::bind(bottom, policy(), authority()), "batch bottom unset baseline");
    RetirementFilter filter{0, 0, 1u << palette::index(bottom.property)};
    ready(palette::retire_matching(session.widget, session.generation, &retirement_match, &filter, authority()), "retire first row group plus bottom in one pass");
    require(filter.comparisons == 8 && !palette::find(bottom.widget, bottom.property)
        && live_attribute()->bytes[0x28] == 0, "one predicate comparison per eligible binding; unset bottom restored exactly");
    for (unsigned i = 0; i != rows.size(); ++i) {
        auto& row = *rows[i];
        require(row.context->comparisons == 1 && row.original.references == 2, "one scan, genuine UObject and source/Slate owners retained independently");
        require(bool(palette::find(row.bytes.data(), palette::Property::Text)) == (i % 3 != 0), "only selected row group detached");
        if (i % 3 == 0) { row.context->live = false; row.context.reset(); }
    }
    ready(palette::bind(bottom, {}, authority()), "rebind safely recycled bottom source passively");
    ready(palette::change(bottom, policy(), authority()), "batch bottom false-to-true transition");
    auto incoming = palette::constant({0.31f, 0.41f, 0.51f, 0.61f});
    const auto latest = incoming.bytes;
    invoke_color(palette::index(bottom.property), bottom.widget, &incoming);
    filter = {1, 0, 1u << palette::index(bottom.property)};
    ready(palette::retire_matching(session.widget, session.generation, &retirement_match, &filter, authority()), "retire second group with latest bottom update");
    require(filter.comparisons == 6 && live_attribute()->bytes[0x28] == 1
        && std::memcmp(live_attribute(), latest.data(), latest.size()) == 0
        && std::memcmp(widget.data() + 0x188, latest.data(), latest.size()) == 0,
        "second single pass restores newest bottom attribute and source, not opening unset state");
    for (unsigned i : {1u, 4u}) {
        require(rows[i]->context->comparisons == 2 && rows[i]->original.references == 2, "second subset compared once per remaining row");
        rows[i]->context->live = false; rows[i]->context.reset();
    }
    expect(primary.property, {0.1f, 0.2f, 0.3f, 0.5f}, "unrelated property stays themed across both batches");
    const auto calls = setter_calls, destroyed = destroys;
    filter = {99, 0, 0};
    ready(palette::retire_matching(session.widget, session.generation, &retirement_match, &filter, authority()), "empty subset is passive");
    require(filter.comparisons == 3 && calls == setter_calls && destroyed == destroys, "empty subset performs metadata comparisons only");
    filter.comparisons = 0;
    ready(palette::retire_matching(session.widget, session.generation + 1, &retirement_match, &filter, authority()), "other generation excluded before predicate");
    require(filter.comparisons == 0, "generation filtering needs no caller metadata access");
    std::thread wrong_thread([&] {
        require(!palette::retire_matching(session.widget, session.generation, &retirement_match, &filter, authority()), "foreign batch cannot detach or clean native owners");
    });
    wrong_thread.join();
    require(filter.comparisons == 0 && calls == setter_calls && destroyed == destroys, "foreign-thread rejection precedes predicate/native calls");
    require(!palette::retire_matching(session.widget, session.generation, nullptr, nullptr, authority()), "null filter cannot accidentally retire the owner");
    finish();
    for (auto& row : rows) {
        row->context.reset();
        destroy_text_widget(row->bytes.data());
        require(row->original.references == 0, "batch and final owner cleanup balance actual linked-color refs");
    }

    setup();
    static Controller reentrant_owner{};
    auto opening = palette::constant({0.8f, 0.4f, 0.2f, 0.8f}); put_controller(opening, &reentrant_owner);
    std::memcpy(widget.data() + 0x148, &opening, sizeof(opening));
    initialize(bottom.property, {0.6f, 0.5f, 0.4f, 0.8f});
    primary = subject(primary.property); bottom = subject(bottom.property);
    ready(palette::bind(primary, policy(), authority()), "reentrant matching primary");
    ready(palette::bind(bottom, policy(), authority()), "reentrant matching bottom");
    auto unrelated = std::make_unique<DynamicWidget>(); unrelated->context->group = 1;
    auto other = dynamic_subject(*unrelated);
    ready(palette::bind(other, policy(), authority()), "unrelated row survives reentrant matching");
    filter = {0, 0, (1u << palette::index(primary.property)) | (1u << palette::index(bottom.property))};
    reentry_match_context = &filter; same_reentry = Reentry::RetireMatching;
    require(!palette::retire_matching(session.widget, session.generation, &retirement_match, &filter, authority()), "native Original reenters matching retirement");
    require(!context_live && !palette::find(primary.widget, primary.property) && !palette::find(bottom.widget, bottom.property)
        && revoked && revoked->occupied && !revoked->subject.validate && palette::find(other.widget, other.property),
        "reentrant subset detaches all selected contexts immediately, never subsequent validator calls");
    require(reentrant_owner.references == 3, "interrupted restore retains source plus actual restored UObject and Slate owners");
    finish();
    unrelated->context.reset(); destroy_text_widget(unrelated->bytes.data()); destroy_text_widget(widget.data());
    require(unrelated->original.references == 0 && reentrant_owner.references == 1, "unrelated cleanup succeeds while revoked source keeps orphan controller");

    static std::vector<std::unique_ptr<DynamicWidget>> orphans;
    for (bool forwarding : {false, true}) {
        setup(); rows.clear();
        for (unsigned i = 0; i != 4; ++i) {
            rows.push_back(std::make_unique<DynamicWidget>()); rows.back()->context->group = i % 2;
            ready(palette::bind(dynamic_subject(*rows.back()), policy(), authority()), "matching failure row");
        }
        auto* stable = palette::find(rows[0]->bytes.data(), palette::Property::Text);
        std::thread foreign;
        if (forwarding) {
            { std::lock_guard lock(forward_mutex); pause_foreign = true; foreign_pending = release_foreign = false; }
            foreign = std::thread([&] {
                auto value = palette::constant({0.2f, 0.3f, 0.4f, 0.5f});
                invoke_color(palette::index(palette::Property::Text), rows[0]->bytes.data(), &value);
            });
            std::unique_lock lock(forward_mutex);
            require(forward_changed.wait_for(lock, std::chrono::seconds(5), [] { return foreign_pending; }), "matching forward write barrier failure bound");
        } else lose_after_setter = true;
        filter = {0, 0, 0};
        const auto consumed = consumes;
        require(!palette::retire_matching(session.widget, session.generation, &retirement_match, &filter, authority()), "matched lost-authority or stale-forward owner remains orphaned");
        require(filter.comparisons == 4 && stable->occupied && !stable->active && !stable->subject.validate,
            "failed subset still has one comparison per active source and immediate context revocation");
        rows[0]->context->live = false; rows[0]->context.reset();
        rows[2]->context->live = false; rows[2]->context.reset();
        if (forwarding) {
            require(stable->forwarding.load() == 1 && rows[2]->original.references == 2,
                "stale selected source orphaned; fresh selected source restored/cleaned independently");
            { std::lock_guard lock(forward_mutex); release_foreign = true; } forward_changed.notify_all(); foreign.join();
            require(stable->forwarding.load() == 0 && !palette::fresh(*stable) && consumes == consumed + 2,
                "detached source survives completion accounting; fresh restore and foreign input each consumed once");
        }
        lose_after_setter = false; qualify = true;
        for (unsigned i : {0u, 2u}) {
            const auto readers = reader_calls.load();
            auto value = palette::constant({0.4f, 0.5f, 0.6f, 0.7f});
            invoke_color(palette::index(palette::Property::Text), rows[i]->bytes.data(), &value);
            require(reader_calls.load() == readers, "retired matching context is never authenticated after being freed");
        }
        require(palette::find(rows[1]->bytes.data(), palette::Property::Text) && palette::find(rows[3]->bytes.data(), palette::Property::Text),
            "unmatched groups remain usable after matched failure");
        finish();
        for (unsigned i = 0; i != rows.size(); ++i) {
            rows[i]->context.reset(); destroy_text_widget(rows[i]->bytes.data());
            if (i == 0 || (!forwarding && i == 2)) {
                require(rows[i]->original.references == 1, "failed matched source keeps actual orphan owner without unsafe cleanup");
                orphans.push_back(std::move(rows[i]));
            } else require(rows[i]->original.references == 0, "successfully retired/unmatched source cleanup balances refs");
        }
    }
}
void proof_phase_regression() {
    struct Counts { std::uint64_t qualifiers; unsigned validations, setters, copies, assignments, destroys; } measured[2]{};
    const palette::Linear incoming{0.3f, 0.4f, 0.5f, 0.6f};
    for (unsigned phased = 0; phased != 2; ++phased) {
        setup(); share_proofs = phased != 0; reader_qualifies = !phased;
        auto s = subject(palette::Property::Image);
        initialize(s.property, {0.8f, 0.6f, 0.4f, 0.9f});
        ready(palette::bind(s, policy(), authority()), "proof fixture starts from the same enabled policy");
        const Counts before{qualifier_calls.load(), validators, setter_calls, copies, assignments, destroys};
        inspect_invocation = phased != 0;
        auto input = incoming; invoke_color(0, widget.data(), &input);
        require(!inspect_invocation, "actual detour reader exercised the invocation matching probe");
        measured[phased] = {qualifier_calls.load() - before.qualifiers, validators - before.validations,
            setter_calls - before.setters, copies - before.copies, assignments - before.assignments, destroys - before.destroys};
        auto expected = policy().normal.linear; expected[3] *= incoming[3];
        expect(s.property, expected, "phase sharing preserves native presentation and genuine source alpha");
        require(read_color(s.property) == incoming && measured[phased].setters == 1
            && !measured[phased].copies && !measured[phased].assignments && !measured[phased].destroys,
            "proof optimization changes no Original/helper calls or genuine property projection");
        finish();
    }
    require(measured[0].qualifiers == 7 && measured[0].validations == 5
        && measured[1].qualifiers == 2 && measured[1].validations == 2,
        "ordinary Linear interception drops seven captures/five validations to two native-delimited proofs");
    std::puts("palette Linear interception: qualifier captures 7 -> 2; Subject validations 5 -> 2; Original calls 1 -> 1");

    for (auto loss : {PhaseLoss::Gc, PhaseLoss::Session, PhaseLoss::Identity, PhaseLoss::Revoked}) {
        setup(); auto s = subject(palette::Property::Image);
        initialize(s.property, {0.8f, 0.6f, 0.4f, 0.9f});
        ready(palette::bind(s, policy(), authority()), "native boundary loss fixture");
        const auto prior_session = session;
        const auto calls = setter_calls;
        auto input = incoming; phase_loss = loss; invoke_color(0, widget.data(), &input);
        auto* source = palette::find(s.widget, s.property);
        require(source && !source->property_source_current && setter_calls == calls + 1,
            "native setter revokes cached proof before any genuine property projection");
        phase_loss = PhaseLoss::None; qualify = fresh_identity = phase_eligible = true; session = prior_session;
        require(!palette::change(s, policy(), authority()), "regained admission cannot recover an uncertain themed property");
        invoke_color(0, widget.data(), &input);
        require(source->property_source_current && read_color(s.property) == incoming,
            "only a fresh qualified Original source recovers after native boundary loss");
        finish();
    }

    setup();
    auto image_subject = subject(palette::Property::Image), pie = subject(palette::Property::PieImage);
    initialize(image_subject.property, incoming); initialize(pie.property, incoming);
    ready(palette::bind(image_subject, {}, authority()), "metadata retirement first passive binding");
    ready(palette::bind(pie, {}, authority()), "metadata retirement unmatched passive binding");
    auto* source = palette::find(image_subject.widget, image_subject.property);
    const Counts before{qualifier_calls.load(), validators, setter_calls, copies, assignments, destroys};
    qualify = fresh_identity = context_live = phase_eligible = false;
    RetirementFilter filter{}; filter.properties = 1u << palette::index(image_subject.property);
    ready(palette::retire_matching(session.widget, session.generation, &retirement_match, &filter, authority()),
        "untouched nonowning retirement needs no live UObject or Subject context");
    require(!source->active && !source->occupied && !source->initialized && palette::find(pie.widget, pie.property)
        && qualifier_calls.load() == before.qualifiers && validators == before.validations
        && setter_calls == before.setters && copies == before.copies && assignments == before.assignments && destroys == before.destroys,
        "exact metadata filter detaches only its passive source with zero qualification/validation/native work");
    qualify = fresh_identity = context_live = phase_eligible = true;
    finish();
}
void failed_restoration_retirement_regression() {
    for (unsigned failure = 0; failure != 4; ++failure) {
        setup();
        auto s = subject(palette::Property::Image);
        initialize(s.property, {0.8f, 0.6f, 0.4f, 0.9f});
        ready(palette::bind(s, policy(), authority()), "failed restoration starts with an applied override");
        auto* source = palette::find(s.widget, s.property);
        const auto presentation = read_presentation(s.property);
        const auto calls = setter_calls;
        fault_setter = true;
        require(!palette::restore(s, authority()) && setter_calls == calls + 1
            && source->overridden && !source->property_source_current
            && read_presentation(s.property) == presentation,
            "failed restoring setter leaves the themed presentation and an outstanding uncertain override");
        fault_setter = false;
        if (failure == 1) qualify = false;
        if (failure >= 2) {
            palette::state.poisoned.store(false); // Test-only: isolate uncertainty from the production poison guard.
            require(palette::admitted(authority()), "uncertain restoration fixture has otherwise current authority");
        }
        if (failure == 3) source->overridden = false; // Prove the certainty check also rejects the former premature flag.
        const auto before_calls = setter_calls, before_copies = copies, before_assignments = assignments, before_destroys = destroys;
        require(!palette::retire_subject(s, authority()) && source->occupied && source->initialized
            && !source->active && !source->subject.validate && !source->property_source_current
            && setter_calls == before_calls && copies == before_copies && assignments == before_assignments && destroys == before_destroys
            && read_presentation(s.property) == presentation,
            "poisoned/revoked/current authority cannot recycle uncertain restoration or perform another native mutation");
        ready(palette::uninstall(), "uncertain restoration orphan teardown performs no native cleanup");
    }
}
void passive_registration_regression() {
    for (std::size_t i = 0; i < palette::index(palette::Property::Count); ++i) {
        setup();
        const auto p = static_cast<palette::Property>(i);
        const auto& contract = palette::contracts[i];
        auto s = subject(p);
        Controller original{}, animated{};
        const palette::Linear opening{0.8f, 0.6f, 0.4f, 0.9f};
        initialize(p, opening);
        if (contract.owning) {
            auto linked = palette::constant(opening); put_controller(linked, &original);
            std::memcpy(widget.data() + contract.offset, &linked, sizeof(linked));
        }
        const auto before = widget;
        auto calls = setter_calls;
        const auto before_validators = validators;
        namespace perf = game::menu_theme_metrics;
        perf::enable(true);
        ready(palette::bind(s, {}, authority()), "Original preparation passively registers every property");
        perf::enable(false);
        perf::Snapshot metrics{};
        require(perf::take_snapshot(metrics)
            && metrics.stats[static_cast<std::size_t>(perf::Metric::ValidatePalette)].count == validators - before_validators
            && !metrics.stats[static_cast<std::size_t>(perf::Metric::ValidatePalette)].failed
            && !metrics.stats[static_cast<std::size_t>(perf::Metric::NativeColorSetter)].count
            && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeColorCopy)].count == (contract.owning ? 1u : 0u)
            && metrics.counters[static_cast<std::size_t>(perf::Counter::PassiveBindings)] == 1,
            "passive metrics count actual validator/copy passes and zero Original calls");
        require(validators == before_validators + (contract.owning ? 2 : 1),
            "passive capture shares an uninterrupted proof but reacquires after an owning helper");
        ready(palette::change(s, {}, authority()), "disabled change is passive");
        ready(palette::restore(s, authority()), "untouched property restore is passive");
        auto inactive = policy(); inactive.normal.enabled = false; inactive.is_selected = false;
        ready(palette::change(s, inactive, authority()), "inactive selected policy is passive on normal row");
        require(setter_calls == calls && widget == before,
            "disabled preparation makes zero native setter calls and changes no widget bytes, including TextBottom");
        if (contract.owning) require(original.references == 2,
            "passive snapshot retains linked SlateColor without assigning or consuming widget mode");
        ready(palette::retire_subject(s, authority()), "passive prepared property retirement");
        require(setter_calls == calls && widget == before && (!contract.owning || original.references == 1),
            "never-themed retirement releases only snapshot, without invoking effectful setter");
        ready(palette::bind(s, {}, authority()), "passive registration safely recycles cleaned record");
        require(setter_calls == calls && widget == before, "passive rebind also preserves native appearance");

        const palette::Linear latest{0.3f, 0.5f, 0.7f, 0.6f};
        auto input = palette::constant(latest);
        if (contract.owning) put_controller(input, &animated);
        const auto consumed = consumes;
        perf::enable(true);
        invoke_color(i, widget.data(), &input);
        perf::enable(false);
        require(perf::take_snapshot(metrics)
            && metrics.stats[static_cast<std::size_t>(perf::Metric::PaletteColorHook)].count == 1
            && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeColorSetter)].count == 1
            && !metrics.stats[static_cast<std::size_t>(perf::Metric::NativeColorSetter)].failed,
            "animation metrics count the actual Original once, including passive native input");
        require(setter_calls == calls + 1 && consumes == consumed + (contract.owning ? 1u : 0u),
            "passive animation invokes actual Original once and consumes owning input once");
        const auto latest_bytes = widget;
        ready(palette::restore(s, authority()), "disabled tracking requires no artificial setter");
        require(setter_calls == calls + 1 && widget == latest_bytes,
            "tracking latest untinted native input does not create a restoring effect");
        if (contract.owning) require(original.references == 0 && animated.references == (p == palette::Property::TextBottom ? 4 : 3),
            "passive animation updates owning baseline while preserving shared widget mode");

        auto active = policy(); active.is_selected = true;
        ready(palette::change(s, active, authority()), "genuine later selected override applies");
        auto expected = active.selected.linear; expected[3] *= latest[3];
        expect(p, expected, "enabled policy applies selected linear RGB and native alpha");
        require(setter_calls == calls + 2, "first genuine override invokes setter once");
        ready(palette::change(s, {}, authority()), "disabling an applied override genuinely restores latest original");
        require(setter_calls == calls + (p == palette::Property::TextBottom ? 2 : 3) && widget == latest_bytes,
            "restoration preserves all owning mode/controller bytes, not just RGB");
        calls = setter_calls;
        ready(palette::change(s, {}, authority()), "repeated disabled change is passive");
        ready(palette::restore(s, authority()), "already-restored property is passive");
        ready(palette::retire(session.widget, session.generation, authority()), "already-restored owner retirement");
        require(setter_calls == calls && widget == latest_bytes,
            "already-restored retirement performs no redundant side-effectful setter");
        finish(); // Fake Slate teardown intentionally destroys/zeros its separate owning presentation.
        require(setter_calls == calls, "fake teardown also invokes no widget setters");
        if (contract.owning) {
            require(animated.references == 1, "retirement leaves only exact restored widget owner");
            native_destroy(widget.data() + contract.offset);
            require(animated.references == 0, "widget cleanup releases final owning reference once");
        }
    }
}
void initialize_bottom_attribute(const palette::NativeColor& color, bool set, const FakeDelegate* delegate = nullptr) {
    // Fake native fixture construction; production never patches attribute flags/delegates.
    palette::NativeAttribute attribute{};
    native_copy(&attribute, &color);
    attribute.bytes[0x28] = set ? 1 : 0;
    if (delegate) {
        auto* cloned = clone_delegate(*delegate);
        std::memcpy(attribute.bytes.data() + 0x30, &cloned, sizeof(cloned));
        const std::uint64_t count = 1;
        std::memcpy(attribute.bytes.data() + 0x38, &count, sizeof(count));
    }
    native_attribute_assign(live_attribute(), &attribute);
    native_attribute_destroy(&attribute);
}
void passive_capture_reentry_regression() {
    static std::array<Controller, 8> owners{}; // Reserved snapshots retain their actual native owners.
    std::size_t owner_index = 0;
    for (const auto p : {palette::Property::Text, palette::Property::TextBottom}) {
        for (const auto action : {CopyAction::AuthorityLoss, CopyAction::IdentityLoss, CopyAction::Retire, CopyAction::Forward}) {
            setup();
            auto& owner = owners[owner_index++];
            auto input = palette::constant({0.8f, 0.4f, 0.2f, 0.8f}); put_controller(input, &owner);
            std::memcpy(widget.data() + palette::contracts[palette::index(p)].offset, &input, sizeof(input));
            auto s = subject(p);
            const auto calls = setter_calls, consumed = consumes, destroyed = destroys, checks = validators;
            copy_property = p; copy_action = action;
            const auto result = palette::bind(s, {}, authority());
            require(!result && !palette::find(s.widget, p), "copy-helper changes cannot publish successful passive registration");
            if (action == CopyAction::Forward) {
                require(result.outcome == palette::Outcome::Unavailable && setter_calls == calls + 1
                    && consumes == consumed + 1 && owner.references == 1
                    && read_color(p) == palette::Linear{0.3f, 0.5f, 0.7f, 0.6f},
                    "forward during owning capture wins once; stale opening capture cannot become fresh");
            } else {
                require(result.outcome == palette::Outcome::Authority && setter_calls == calls && consumes == consumed
                    && destroys == destroyed && owner.references == 2
                    && std::memcmp(widget.data() + palette::contracts[palette::index(p)].offset, &input, sizeof(input)) == 0,
                    "post-helper loss rejects without setter, byte mutation or unsafe native-owner cleanup");
                if (action == CopyAction::Retire) require(revoked && !revoked->active && !revoked->subject.validate,
                    "helper retirement clears freed context before outer capture continuation");
            }
            require(validators == checks + (action == CopyAction::IdentityLoss ? 2 : 1),
                "failed admission or detached context never invokes stale validator; identity is rechecked after helper");
            ready(palette::uninstall(), "failed passive capture drains without native cleanup");
            if (p == palette::Property::Text) destroy_text_widget(widget.data());
            else {
                native_destroy(widget.data() + 0x188);
                native_attribute_destroy(live_attribute()); *live_attribute() = palette::NativeAttribute{};
            }
            require(owner.references == 1, "only reserved source owns controller after fake widget teardown");
        }
    }
}
void bottom_attribute_regression() {
    auto missing = specs.extract("native_slate_color_attribute_assign");
    require(!palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, &reader}) && !palette::state.configured,
        "missing canonical owning-attribute contract fails palette configuration closed");
    specs.insert(std::move(missing));
    for (bool delegated : {false, true}) {
        setup();
        const auto s = subject(palette::Property::TextBottom);
        Controller object_owner{}, attribute_owner{}, delegate_owner{};
        auto object_color = palette::constant({0.8f, 0.6f, 0.4f, 0.9f}); put_controller(object_color, &object_owner);
        auto attribute_color = palette::constant({0.2f, 0.4f, 0.6f, 0.8f}); put_controller(attribute_color, &attribute_owner);
        auto delegate_root = palette::constant({1, 1, 1, 1}); put_controller(delegate_root, &delegate_owner);
        FakeDelegate delegate{73, &delegate_owner};
        std::memcpy(widget.data() + 0x188, &object_color, sizeof(object_color)); // Transfer fixture owner.
        initialize_bottom_attribute(attribute_color, delegated, delegated ? &delegate : nullptr);
        native_destroy(&attribute_color); // Only the live Slate attribute owns this color now.
        const auto calls = setter_calls;
        ready(palette::bind(s, {}, authority()), "passive bottom binding does not enable original gradient");
        require(setter_calls == calls && live_attribute()->bytes[0x28] == (delegated ? 1 : 0), "passive unset/bound state unchanged");
        ready(palette::change(s, policy(), authority()), "first bottom override snapshots UObject AND Slate attribute");
        auto* source = palette::find(s.widget, s.property);
        require(source && source->attribute_initialized && source->attribute_current
            && live_attribute()->bytes[0x28] == 1 && !attribute_delegate(live_attribute()), "override uses native set/unbound behavior");
        require(object_owner.references == 2 && attribute_owner.references == 1,
            "independent UObject and Slate owning color baselines survive override");
        if (delegated) require(delegates_live == 1 && delegate_owner.references == 2,
            "snapshot clones bound delegate and retains captured owner after live delegate replaced");
        const auto original_calls = setter_calls, borrowed_calls = bottom_attribute_calls, consumed = consumes;
        ready(palette::restore(s, authority()), "restore original bottom attribute without bottom UObject setter");
        require(setter_calls == original_calls && bottom_attribute_calls == borrowed_calls + 1 && consumes == consumed,
            "restoration uses native owning UObject assign and borrowed Slate setter, never consuming bottom setter");
        require(std::memcmp(widget.data() + 0x188, &object_color, sizeof(object_color)) == 0
            && std::memcmp(live_attribute(), &attribute_color, sizeof(attribute_color)) == 0
            && live_attribute()->bytes[0x28] == (delegated ? 1 : 0), "exact independent owning modes and original unset flag restored");
        auto* restored = attribute_delegate(live_attribute());
        require(delegated ? (restored && restored->identity == 73 && restored->owner == &delegate_owner
            && restored != attribute_delegate(&source->bottom_attribute) && delegates_live == 2 && delegate_owner.references == 3)
            : (!restored && delegates_live == 0), "bound delegate restored via owning clone, not pointer copying");
        ready(palette::change(s, policy(), authority()), "second override preserves original baseline");
        ready(palette::restore(s, authority()), "second restore preserves original bottom state");
        auto genuine = palette::constant({0.17f, 0.27f, 0.37f, 0.47f});
        invoke_color(palette::index(s.property), widget.data(), &genuine);
        ready(palette::change(s, policy(), authority()), "override after actual native update replaces opening attribute baseline");
        ready(palette::restore(s, authority()), "restore newest native set/unbound state, never resurrect opening delegate");
        require(live_attribute()->bytes[0x28] == 1 && !attribute_delegate(live_attribute()) && delegates_live == 0
            && std::memcmp(live_attribute(), &genuine, sizeof(genuine)) == 0,
            "latest genuine native input supersedes both original unset and original bound attributes");
        finish();
        native_destroy(widget.data() + 0x188); native_destroy(&delegate_root);
        require(object_owner.references == 0 && attribute_owner.references == 0 && delegate_owner.references == 0 && delegates_live == 0,
            "all snapshot/widget/delegate owners cleaned exactly once under authority");
    }

    setup();
    auto s = subject(palette::Property::TextBottom);
    initialize(s.property, {0.8f, 0.6f, 0.4f, 0.9f});
    ready(palette::bind(s, policy(), authority()), "unset bottom overridden before native updates");
    Controller animated{};
    auto input = palette::constant({0.3f, 0.5f, 0.7f, 0.6f}); put_controller(input, &animated);
    // The hook assigns themed color INTO the consuming argument. Compare restoration
    // with non-owning expected bytes saved before that argument is transformed/consumed.
    const auto expected_input = input.bytes;
    const auto before_input = consumes;
    invoke_color(palette::index(s.property), widget.data(), &input);
    auto* native_source = palette::find(s.widget, s.property);
    require(native_source && native_source->native.bytes == expected_input
        && std::memcmp(&native_source->bottom_attribute, expected_input.data(), expected_input.size()) == 0
        && animated.references == 3 && consumes == before_input + 1 && !controller(&input)
        && input.bytes != expected_input,
        "both snapshots own untinted linked source while transformed native argument is consumed exactly once");
    expect(s.property, {0.1f, 0.2f, 0.3f, 0.3f}, "genuine bottom input remains themed before restoration");
    ready(palette::restore(s, authority()), "restore latest native bottom update instead of opening unset attribute");
    require(live_attribute()->bytes[0x28] == 1 && !attribute_delegate(live_attribute())
        && std::memcmp(live_attribute(), expected_input.data(), expected_input.size()) == 0
        && std::memcmp(widget.data() + 0x188, expected_input.data(), expected_input.size()) == 0
        && animated.references == 4,
        "genuine native update advances owning color and set/unbound baseline");
    ready(palette::change(s, policy(), authority()), "reapply before nested bottom opacity");
    animated.value = {0.31f, 0.51f, 0.71f, 0.6f};
    const auto consumed = consumes;
    invoke_opacity(16, widget.data(), 0.4f);
    ready(palette::restore(s, authority()), "restore nested bottom opacity's new baseline");
    expect(s.property, {0.31f, 0.51f, 0.71f, 0.4f}, "bottom opacity preserves untinted shared source RGB");
    require(live_attribute()->bytes[0x28] == 1 && !attribute_delegate(live_attribute()) && animated.references == 0
        && consumes == consumed + 1, "nested opacity advances native attribute baseline and consumes input once");
    ready(palette::change(s, policy(), authority()), "reapply before unqualified bottom update");
    qualify = false;
    auto unavailable = palette::constant({0.9f, 0.8f, 0.7f, 0.6f});
    invoke_color(palette::index(s.property), widget.data(), &unavailable);
    qualify = true;
    auto calls = bottom_attribute_calls;
    require(!palette::restore(s, authority()) && !palette::change(s, policy(), authority()) && bottom_attribute_calls == calls,
        "forwarded bottom native update forbids replaying old owning attribute");
    auto recovered = palette::constant({0.4f, 0.5f, 0.6f, 0.7f});
    const auto expected_recovered = recovered.bytes;
    invoke_color(palette::index(s.property), widget.data(), &recovered);
    require(palette::find(s.widget, s.property)->native.bytes == expected_recovered && recovered.bytes != expected_recovered,
        "qualified recovery captures untinted input before consuming argument transformation");
    ready(palette::change(s, policy(), authority()), "qualified input recovers stale bottom attribute");
    ready(palette::restore(s, authority()), "restored recovered attribute remains native-set, not opening-unset");
    require(live_attribute()->bytes[0x28] == 1
        && std::memcmp(live_attribute(), expected_recovered.data(), expected_recovered.size()) == 0
        && std::memcmp(widget.data() + 0x188, expected_recovered.data(), expected_recovered.size()) == 0,
        "only a genuinely qualified new source establishes latest bottom restoration");
    finish();

    for (int failure = 0; failure != 4; ++failure) {
        setup(); initialize(s.property, {0.8f, 0.6f, 0.4f, 0.9f}); s = subject(s.property);
        ready(palette::bind(s, policy(), authority()), "bottom restoration failure fixture");
        auto* source = palette::find(s.widget, s.property);
        calls = bottom_attribute_calls;
        if (failure == 0) { // Drift BEFORE restore must invoke neither mutator on stale Slate.
            palette::SlatePair changed{slate.data(), &replacement_controller};
            std::memcpy(widget.data() + 0x220, &changed, sizeof(changed));
            const auto before = widget;
            require(!palette::restore(s, authority()) && widget == before && bottom_attribute_calls == calls,
                "controller-only Slate pair drift fails closed before UObject or Slate restoration");
        } else {
            drift_attribute = failure == 1;
            retire_attribute = failure == 2;
            revoke_attribute = failure == 3;
            require(!palette::restore(s, authority()) && bottom_attribute_calls == calls + 1,
                "post-restore Slate drift, reentrant retirement or authority loss is detected");
        }
        const auto destroyed = attribute_destroys;
        require(!palette::retire(session.widget, session.generation, authority()) || failure == 2,
            "failed bottom restoration retains owning orphan without stale native cleanup");
        require(source->occupied && !source->active && !source->subject.validate && attribute_destroys == destroyed,
            "retirement clears validator context and preserves orphan attribute ownership");
        ready(palette::uninstall(), "drained uninstall retains bottom orphan bytes, no native destruction");
        native_attribute_destroy(live_attribute()); *live_attribute() = palette::NativeAttribute{};
        native_attribute_destroy(replacement_slate.data() + 0x340); replacement_slate.fill(0);
    }
    setup(); initialize(s.property, {0.8f, 0.6f, 0.4f, 0.9f}); s = subject(s.property);
    fault_attribute = true;
    require(!palette::bind(s, policy(), authority()) && palette::state.poisoned.load(), "attribute clone fault reserves uncertain ownership before mutation");
    const auto destroyed = attribute_destroys;
    fault_attribute = false;
    require(!palette::retire(session.widget, session.generation, authority()) && attribute_destroys == destroyed,
        "no owning attribute cleanup after native fault authority loss");
    ready(palette::uninstall(), "attribute fault retains orphan until process teardown");

    setup();
    static Controller retained_cleanup_owner{};
    auto retained_color = palette::constant({0.8f, 0.6f, 0.4f, 0.9f}); put_controller(retained_color, &retained_cleanup_owner);
    std::memcpy(widget.data() + 0x188, &retained_color, sizeof(retained_color));
    s = subject(s.property);
    ready(palette::bind(s, policy(), authority()), "attribute cleanup authority fixture");
    ready(palette::restore(s, authority()), "restore before cleanup authority loss");
    auto* retained_source = palette::find(s.widget, s.property);
    const auto before_cleanup = destroys;
    revoke_attribute_destroy = true;
    require(!palette::retire_subject(s, authority()) && destroys == before_cleanup + 1
        && retained_source->occupied && !retained_source->active && !retained_source->subject.validate
        && !retained_source->attribute_initialized && retained_source->initialized && retained_cleanup_owner.references == 2,
        "authority loss during attribute destruction forbids subsequent native color cleanup and clears contexts");
    ready(palette::uninstall(), "retain source color owner after partial authorized cleanup");
    native_attribute_destroy(live_attribute()); *live_attribute() = palette::NativeAttribute{};
    native_destroy(widget.data() + 0x188);
    require(retained_cleanup_owner.references == 1, "uncleaned source keeps real controller alive until process teardown");
}
void routed_filter_regression() {
    setup(); initialize(palette::Property::Image, {0.8f, 0.4f, 0.2f, 0.8f});
    auto s = subject(palette::Property::Image);
    ready(palette::bind(s, policy(), authority()), "instance filter initial bind");
    const auto reads = reader_calls.load();
    auto calls = setter_calls;
    palette::Linear native{0.7f, 0.5f, 0.3f, 0.9f};
    namespace perf = game::menu_theme_metrics;
    perf::enable(true);
    invoke_color(0, held_widget.data(), &native);
    require(setter_calls == ++calls && reader_calls.load() == reads,
        "unmatched widget color forwards exactly once without reader/context acquisition");
    require(read_color(s.property, held_widget.data()) == native, "unmatched widget color unchanged");
    invoke_opacity(11, held_widget.data(), 0.6f);
    require(setter_calls == ++calls && reader_calls.load() == reads,
        "unmatched widget opacity forwards exactly once without reader/context acquisition");
    require(read_color(s.property, held_widget.data()) == palette::Linear{0.7f, 0.5f, 0.3f, 0.6f},
        "unmatched widget opacity unchanged");
    invoke_color(3, widget.data(), &native); // ImageSet is not the registered Image property.
    require(setter_calls == ++calls && reader_calls.load() == reads,
        "unmatched property color forwards exactly once without reader/context acquisition");
    expect(palette::Property::ImageSet, native, "unmatched property color unchanged");
    invoke_opacity(14, widget.data(), 0.6f);
    require(setter_calls == ++calls && reader_calls.load() == reads,
        "unmatched property opacity forwards exactly once without reader/context acquisition");
    expect(palette::Property::ImageSet, {0.7f, 0.5f, 0.3f, 0.6f}, "unmatched property opacity unchanged");
    perf::enable(false);
    perf::Snapshot metrics{};
    require(perf::take_snapshot(metrics)
        && metrics.stats[static_cast<std::size_t>(perf::Metric::PaletteColorHook)].count == 2
        && metrics.stats[static_cast<std::size_t>(perf::Metric::PaletteOpacityHook)].count == 2
        && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeColorSetter)].count == 2
        && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeOpacitySetter)].count == 2
        && !metrics.stats[static_cast<std::size_t>(perf::Metric::ValidatePalette)].count
        && metrics.counters[static_cast<std::size_t>(perf::Counter::PaletteForwarded)] == 4,
        "forward metrics count each actual Original, without fabricating validation passes");
    invoke_color(0, widget.data(), &native);
    require(setter_calls == ++calls && reader_calls.load() == reads + 1,
        "matched color still acquires reader and calls Original exactly once");
    expect(s.property, {0.1f, 0.2f, 0.3f, 0.9f * 0.5f}, "matched color still themed");
    invoke_opacity(11, widget.data(), 0.6f);
    require(setter_calls == ++calls && reader_calls.load() == reads + 2,
        "matched opacity still acquires reader and calls Original exactly once");
    expect(s.property, {0.1f, 0.2f, 0.3f, 0.6f * 0.5f}, "matched opacity still themed");
    ready(palette::restore(s, authority()), "unmatched calls do not stale registered source");
    expect(s.property, {0.7f, 0.5f, 0.3f, 0.6f}, "matched untinted source remains restorable");
    finish();
}
void subject_retirement_regression() {
    for (int scenario = 0; scenario != 3; ++scenario) {
        setup();
        const auto first = subject(palette::Property::Text);
        const auto second = subject(palette::Property::TextBottom);
        const palette::Linear original{0.8f, 0.4f, 0.2f, 0.8f};
        initialize(first.property, original); initialize(second.property, original);
        ready(palette::bind(first, policy(), authority()), "first independently bound property");
        ready(palette::bind(second, policy(), authority()), "second independently bound property");
        const auto untouched = read_presentation(second.property);
        if (scenario == 0) {
            ready(palette::retire_subject(first, authority()), "retire only one property");
            expect(first.property, original, "one property restored before detach");
        } else if (scenario == 1) {
            qualify = false;
            require(!palette::retire_subject(first, authority()), "lost authority retains orphan ownership");
            qualify = true;
        } else {
            same_reentry = Reentry::RetireSubject;
            auto update = palette::constant(original);
            invoke_color(palette::index(first.property), widget.data(), &update);
            context_live = true;
        }
        require(!palette::find(first.widget, first.property), "retired property cannot validate freed context");
        require(palette::find(second.widget, second.property), "unrelated property stays registered");
        expect(second.property, untouched, "unrelated property not restored or changed");
        ready(palette::change(second, policy(), authority()), "unrelated source remains usable");
        finish();
    }
}
void supported_properties() {
    for (std::size_t i = 0; i < palette::index(palette::Property::Count); ++i) {
        setup();
        const auto p = static_cast<palette::Property>(i);
        initialize(p, {0.8f, 0.4f, 0.2f, 0.8f});
        auto s = subject(p); ready(palette::bind(s, policy(), authority()), "bind fixed property actual reader");
        expect(p, {0.1f, 0.2f, 0.3f, 0.4f}, "plan linear RGB and modulated authored alpha");
        palette::Linear animation{0.9f, 0.8f, 0.7f, 0.4f};
        auto native = palette::constant(animation);
        const auto before = setter_calls;
        invoke_color(i, widget.data(), palette::contracts[i].owning ? static_cast<void*>(&native) : &animation);
        require(setter_calls == before + 1, "native setter exactly once");
        expect(p, {0.1f, 0.2f, 0.3f, 0.2f}, "animation keeps palette, preserves fade");
        auto selected = policy(); selected.is_selected = true;
        ready(palette::change(s, selected, authority()), "semantic selected transition");
        expect(p, {0.7f, 0.6f, 0.5f, 0.1f}, "selected colors no stale normal row");
        selected.normal.linear = {0.4f, 0.3f, 0.2f, 1}; selected.is_selected = false;
        ready(palette::change(s, selected, authority()), "theme and selection transition");
        expect(p, {0.4f, 0.3f, 0.2f, 0.4f}, "transition starts from latest untinted original");
        ready(palette::restore(s, authority()), "original transition");
        expect(p, animation, "restore latest animated original, not opening snapshot");
        finish();
    }
}
void owning_and_nesting() {
    for (const auto p : {palette::Property::Text, palette::Property::TextBottom}) {
        setup();
        Controller original, animated;
        auto opening = palette::constant({0.8f, 0.4f, 0.2f, 0.8f}); put_controller(opening, &original);
        const auto offset = palette::contracts[palette::index(p)].offset;
        std::memcpy(widget.data() + offset, &opening, sizeof(opening)); // transfer fixture owner to widget
        auto s = subject(p);
        ready(palette::bind(s, policy(), authority()), "owning original capture");
        require(original.references == 2, "untinted UObject and snapshot retain original while Slate stays themed");
        auto animation = palette::constant({0.9f, 0.8f, 0.7f, 0.6f}); put_controller(animation, &animated);
        const auto before = consumes;
        invoke_color(palette::index(p), widget.data(), &animation);
        require(consumes == before + 1 && animated.references == (p == palette::Property::TextBottom ? 3 : 2) && original.references == 0,
            "incoming owner consumed once, bounded source owns latest controller");
        ready(palette::restore(s, authority()), "owning exact restoration");
        require(animated.references == (p == palette::Property::TextBottom ? 4 : 3), "restored UObject/Slate and snapshots own independent references");
        require(std::memcmp(widget.data() + offset, &palette::find(widget.data(), p)->native, 40) == 0,
            "exact 40-byte owning restoration including mode/shared/controller");
        ready(palette::change(s, policy(), authority()), "reapply theme");
        // Inline specified RGB is stale in a linked SlateColor. The native
        // opacity path selects its shared FLinearColor, not the themed widget.
        animated.value = {0.35f, 0.45f, 0.55f, 0.9f};
        const auto opacity_method = p == palette::Property::Text ? 15u : 16u;
        invoke_opacity(opacity_method, widget.data(), 0.4f);
        expect(p, {0.1f, 0.2f, 0.3f, 0.2f}, "nested opacity modulates once");
        require(animated.references == 0, "native opacity constant transition releases old source controller");
        ready(palette::restore(s, authority()), "nested source restoration");
        expect(p, {0.35f, 0.45f, 0.55f, 0.4f}, "nested opacity reads owned shared source, NEVER themed native RGB");
        finish();
    }
    for (std::size_t method = 11; method < 15; ++method) {
        setup(); const auto p = palette::contracts[method].property;
        initialize(p, {0.9f, 0.8f, 0.7f, 0.8f}); auto s = subject(p);
        ready(palette::bind(s, policy(), authority()), "image opacity binding");
        invoke_opacity(method, widget.data(), 0.0f);
        expect(p, {0.1f, 0.2f, 0.3f, 0}, "authored invisible alpha stays invisible");
        invoke_opacity(method, widget.data(), 0.6f);
        ready(palette::restore(s, authority()), "direct opacity restore");
        expect(p, {0.9f, 0.8f, 0.7f, 0.6f}, "latest direct opacity source"); finish();
    }
}
void filtering_reentry_cleanup() {
    setup(); auto s = subject(palette::Property::Image);
    initialize(s.property, {0.8f, 0.4f, 0.2f, 0.8f});
    palette::Linear early_native{0.4f, 0.5f, 0.6f, 0.7f};
    reader_ready = false;
    invoke_color(0, widget.data(), &early_native);
    expect(s.property, early_native, "input before readiness forwards immediately without waiting");
    reader_ready = true;
    ready(palette::bind(s, policy(), authority()), "filtered binding");
    ready(palette::restore(s, authority()), "delayed readiness restore");
    expect(s.property, early_native, "binding after readiness reads latest early native value");
    ready(palette::change(s, policy(), authority()), "immediate transition after delayed readiness");
    palette::Linear native{0.8f, 0.7f, 0.6f, 0.5f};
    for (bool* fact : {&reader_ready, &qualify, &current_class, &strong_member, &fresh_identity}) {
        *fact = false; const auto before = setter_calls;
        invoke_color(0, widget.data(), &native);
        require(setter_calls == before + 1, "unavailable/stale native input forwarded exactly once");
        expect(s.property, native, "stale subject never transformed"); *fact = true;
        const auto calls = setter_calls;
        require(!palette::restore(s, authority()) && !palette::change(s, policy(), authority()),
            "regained admission must not emit stale source");
        require(setter_calls == calls, "stale restore/change never calls Original");
    }
    auto stale = s; ++stale.generation;
    require(!palette::change(stale, policy(), authority()), "stale policy update rejected");
    ++session.generation; invoke_color(0, widget.data(), &native);
    expect(s.property, native, "old binding never transforms a new native menu generation");
    --session.generation;
    shared_gate.close(); invoke_color(0, widget.data(), &native);
    expect(s.property, native, "existing non-audio gate loss forwards unchanged"); shared_gate.open();
    invoke_color(0, held_widget.data(), &native);
    require(read_color(s.property, held_widget.data()) == native, "held unrelated controller unaffected");
    require(!palette::change(s, policy(), authority()), "valid authority alone cannot replay stale source");
    invoke_color(0, widget.data(), &native);
    ready(palette::change(s, policy(), authority()), "qualified fresh incoming source recovers");
    reenter = true; invoke_color(0, widget.data(), &native);
    require(read_color(s.property, held_widget.data()) == palette::Linear{0.9f, 0.8f, 0.7f, 0.6f},
        "reentrant unrelated original forwards without lock/deadlock");
    ready(palette::restore(s, authority()), "restore after reentry"); expect(s.property, native, "latest unthemed reentry baseline");
    finish();

    setup(); static Controller retained;
    auto color = palette::constant({0.8f, 0.4f, 0.2f, 0.8f}); put_controller(color, &retained);
    std::memcpy(widget.data() + 0x148, &color, sizeof(color));
    auto text = subject(palette::Property::Text);
    ready(palette::bind(text, policy(), authority()), "retained owner fixture");
    qualify = false; const auto destruction_count = destroys;
    require(!palette::retire(session.widget, session.generation, authority()), "lost authority reports orphan");
    require(!palette::find(widget.data(), text.property) && destroys == destruction_count && retained.references == 2,
        "retire clears validator context, retains owner, no cleanup after authority loss");
    ready(palette::uninstall(), "uninstall orphan owner without native calls");
    // Keep the orphan controller itself alive for the entire test process.
    // Its refcount was checked above; no production cleanup can touch it again.

    setup(); initialize(palette::Property::Text, {0.8f, 0.4f, 0.2f, 0.8f});
    text = subject(palette::Property::Text);
    ready(palette::bind(text, policy(), authority()), "post-call authority loss fixture");
    lose_after_setter = true; const auto prior_destroy = destroys;
    require(!palette::retire(session.widget, session.generation, authority()), "authority rechecked after restore call");
    require(destroys == prior_destroy + 3, "only UObject/Slate setter assignments and consuming argument destroy, no later projection or snapshot cleanup");
    require(!palette::find(widget.data(), text.property), "post-call loss clears validator context");
    lose_after_setter = false; ready(palette::uninstall(), "post-call authority loss teardown");
}
void property_feedback_regression() {
    for (std::size_t method = 0; method != palette::index(palette::Property::Count); ++method) {
        setup();
        const auto p = static_cast<palette::Property>(method);
        palette::Linear genuine{0.8f, 0.4f, 0.2f, 0.8f};
        initialize(p, genuine);
        auto s = subject(p);
        ready(palette::bind(s, {}, authority()), "feedback baseline starts as passive Original");
        auto chosen = policy();
        ready(palette::change(s, chosen, authority()), "enable presentation without changing animation-readable property");
        auto* source = palette::find(s.widget, p);
        require(read_color(p) == genuine && read_presentation(p) != genuine,
            "UObject property stays genuine while independently copied Slate presentation is themed");
        for (unsigned cycle = 0; cycle != 40; ++cycle) {
            if (cycle == 13) {
                genuine = {0.6f, 0.7f, 0.9f, 0.8f};
                auto input = palette::constant(genuine);
                invoke_color(method, s.widget, palette::contracts[method].owning ? static_cast<void*>(&input) : genuine.data());
            }
            const float alpha = static_cast<float>((cycle % 7) + 1) / 8.0f;
            if (palette::contracts[method].owning) {
                // The real text opacity ABI reads the current UObject SlateColor,
                // resolves shared color, then sends a constant to the consuming setter.
                invoke_opacity(p == palette::Property::Text ? 15 : 16, s.widget, alpha);
            } else if (method < 4 && cycle % 2) {
                invoke_opacity(11 + method, s.widget, alpha);
            } else {
                // Cached property resolution still reads current UObject RGBA on
                // each masked alpha evaluation; it does not cache untinted RGB.
                auto evaluated = read_color(p);
                require(evaluated[0] == genuine[0] && evaluated[1] == genuine[1] && evaluated[2] == genuine[2],
                    "native RMW alpha reader never sees previous palette RGB");
                evaluated[3] = alpha;
                invoke_color(method, s.widget, evaluated.data());
            }
            genuine[3] = alpha;
            require(read_color(p) == genuine && palette::rgba(source->native) == genuine,
                "genuine RGB and alpha advance without palette feedback across repeated native evaluations");
            auto presentation = chosen.normal.linear; presentation[3] *= alpha;
            expect(p, presentation, "Slate stays themed after property-only writeback");
            if (cycle % 5 == 0) {
                const auto sequence = source->sequence;
                chosen.normal.linear[0] = chosen.normal.linear[0] == 0.1f ? 0.15f : 0.1f;
                ready(palette::change(s, chosen, authority()), "explicit presentation emit does not rebase source");
                require(source->sequence == sequence + (palette::contracts[method].owning ? 0 : 1) && read_color(p) == genuine,
                    "own emit preserves source provenance and genuine animation-readable property");
            }
        }
        ready(palette::restore(s, authority()), "Original after many alpha cycles restores genuine RGB, not palette RGB");
        require(read_color(p) == genuine && read_presentation(p) == genuine,
            "Original preserves actual native RGB change and latest authored alpha");
        finish();
    }

    setup();
    static Controller fault_owner{};
    auto native = palette::constant({0.8f, 0.4f, 0.2f, 0.8f}); put_controller(native, &fault_owner);
    std::memcpy(widget.data() + 0x148, &native, sizeof(native));
    auto s = subject(palette::Property::Text);
    ready(palette::bind(s, {}, authority()), "owning writeback fault baseline");
    fault_assign_destination = widget.data() + 0x148; assigns_until_fault = 1; // Original succeeds; property-only assignment faults.
    const auto calls = setter_calls;
    require(!palette::change(s, policy(), authority()) && setter_calls == calls + 1 && palette::state.poisoned.load(),
        "owning property writeback fault is not retried or hidden by another setter");
    auto* orphan = palette::find(s.widget, s.property);
    require(orphan && fault_owner.references == 1 && read_color(s.property) == read_presentation(s.property)
        && read_color(s.property) != palette::rgba(orphan->native), "failed writeback does not pretend property restoration succeeded");
    fault_assign_destination = nullptr;
    const auto destroyed = destroys;
    require(!palette::retire_matching(session.widget, session.generation,
        +[](void*, const palette::Subject&) noexcept { return true; }, nullptr, authority()) && destroys == destroyed,
        "native fault forbids subsequent owning cleanup while detaching contexts");
    ready(palette::uninstall(), "writeback fault safely retains orphan ownership");
    require(fault_owner.references == 1 && !orphan->subject.validate, "orphan keeps actual controller, never stale validator context");
}
void direct_property_write_regression() {
    for (std::size_t method = 0; method != palette::index(palette::Property::Count); ++method) {
        if (palette::contracts[method].owning) continue;
        setup();
        const auto p = static_cast<palette::Property>(method);
        const palette::Linear opening{0.8f, 0.4f, 0.2f, 0};
        initialize(p, opening);
        auto s = subject(p);
        ready(palette::bind(s, {}, authority()), "direct-write baseline passively captures native zero alpha");
        auto* source = palette::find(s.widget, p);
        const palette::Linear latest{0.6f, 0.7f, 0.9f, 0.37f};
        initialize(p, latest); // Genuine UObject write, deliberately no intercepted setter.
        ready(palette::change(s, policy(), authority()), "apply captures newer direct UObject color");
        auto expected = policy().normal.linear; expected[3] *= latest[3];
        require(read_color(p) == latest && palette::rgba(source->native) == latest,
            "apply preserves direct native alpha 0.37 and RGB instead of replaying passive alpha zero");
        expect(p, expected, "direct native alpha is modulated, never forced");
        ready(palette::change(s, policy(), authority()), "repeated apply recaptures genuine property, not Slate RGB");
        require(palette::rgba(source->native) == latest, "themed presentation never becomes source");
        const palette::Linear restored{0.3f, 0.4f, 0.5f, 0.61f};
        initialize(p, restored);
        ready(palette::restore(s, authority()), "restore captures direct native update");
        require(read_color(p) == restored && read_presentation(p) == restored,
            "Original restores newest direct property, not last intercepted source");
        const palette::Linear refreshed{0.9f, 0.8f, 0.7f, 0.43f};
        initialize(p, refreshed); // Same-association-like native setup after restoration.
        ready(palette::change(s, policy(), authority()), "same-association restore/write/reapply preserves new baseline");
        require(read_color(p) == refreshed && palette::rgba(source->native) == refreshed,
            "same association does not retain its pre-setup source");
        const palette::Linear retiring{0.2f, 0.3f, 0.4f, 0.79f};
        initialize(p, retiring);
        ready(palette::retire_subject(s, authority()), "retirement captures final direct native update");
        require(read_color(p) == retiring && read_presentation(p) == retiring,
            "retirement does not overwrite newer genuine property");
        ready(palette::bind(s, {}, authority()), "direct native zero remains a valid source");
        initialize(p, opening);
        ready(palette::change(s, policy(), authority()), "apply respects direct native zero alpha");
        expected[3] = 0;
        expect(p, expected, "native zero alpha is preserved, not repaired to an invented alpha");
        finish();
    }
}
void uncertain_property_regression() {
    for (unsigned operation = 0; operation != 5; ++operation) {
        setup();
        const auto p = palette::Property::Image;
        const palette::Linear genuine{0.8f, 0.4f, 0.2f, 0.8f};
        initialize(p, genuine);
        auto s = subject(p);
        ready(palette::bind(s, policy(), authority()), "uncertain property starts with successful genuine projection");
        auto* source = palette::find(s.widget, p);
        if (operation == 3) fail_property_write = true;
        else if (operation == 4) fault_setter = true;
        else lose_after_setter = true;
        if (operation == 1) {
            auto incoming = palette::constant(genuine);
            invoke_color(0, s.widget, &incoming);
        } else if (operation == 2) invoke_opacity(11, s.widget, genuine[3]);
        else require(!palette::change(s, policy(), authority()), "failed emission reports uncertainty");
        lose_after_setter = fault_setter = fail_property_write = false;
        qualify = true;
        require(!source->property_source_current, "failed native/projection completion never blesses property as genuine");
        const auto prior = palette::rgba(source->native);
        const auto actual = read_color(p);
        const auto calls = setter_calls;
        require(!palette::change(s, policy(), authority()) && !palette::restore(s, authority())
            && setter_calls == calls && palette::rgba(source->native) == prior && read_color(p) == actual,
            "later emission cannot adopt uncertain themed property or replay over it");
        if (operation < 4) {
            const palette::Linear recovery{0.3f, 0.5f, 0.7f, 0.9f};
            auto incoming = palette::constant(recovery);
            invoke_color(0, s.widget, &incoming);
            require(source->property_source_current && read_color(p) == recovery,
                "qualified genuine setter plus successful projection reestablishes property source");
            ready(palette::restore(s, authority()), "genuine recovery enables safe later recapture");
            expect(p, recovery, "recovery never adopted temporary themed RGB");
            finish();
        } else {
            require(palette::state.poisoned.load() && !palette::retire(session.widget, session.generation, authority()),
                "native fault remains terminal and is not retried for recovery");
            ready(palette::uninstall(), "native fault retains uncertain source without mutation");
        }
    }
}
void faults_and_transaction() {
    setup(); auto s = subject(palette::Property::Image);
    initialize(s.property, {0.8f, 0.4f, 0.2f, 0.8f});
    ready(palette::bind(s, policy(), authority()), "fault fixture binding");
    fault_setter = true; palette::Linear native{1, 1, 1, 1};
    namespace perf = game::menu_theme_metrics;
    perf::enable(true);
    const auto before = setter_calls; invoke_color(0, widget.data(), &native);
    perf::enable(false);
    perf::Snapshot metrics{};
    require(perf::take_snapshot(metrics)
        && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeColorSetter)].count == 1
        && metrics.stats[static_cast<std::size_t>(perf::Metric::NativeColorSetter)].failed == 1
        && !metrics.counters[static_cast<std::size_t>(perf::Counter::MalformedScopes)],
        "setter SEH metrics record one failed Original and leave balanced scopes");
    require(setter_calls == before + 1 && palette::state.poisoned.load(), "fault latches, original never retried");
    require(palette::last_failure().outcome == palette::Outcome::NativeFault, "failure fact copied for reporting");
    fault_setter = false;
    require(!palette::retire(session.widget, session.generation, authority()), "poisoned binding retired without cleanup");
    ready(palette::uninstall(), "fault teardown");

    setup(); auto text = subject(palette::Property::Text);
    initialize(text.property, {1, 1, 1, 1}); fault_copy = true;
    require(!palette::bind(text, policy(), authority()) && palette::state.poisoned.load(), "owning copy fault retained, no invented cleanup");
    fault_copy = false; ready(palette::uninstall(), "copy fault teardown");
    palette::state.poisoned.store(false);
    for (std::size_t at = 1; at <= palette::kMethods; ++at) {
        ready(palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, &reader}), "rollback configure");
        fake_native_functions(); install_count = 0; fail_install = unsigned(at);
        require(!palette::install() && installed.empty(), "each partial installation unwinds every preceding hook");
        require(!palette::state.callbacks.accepting(), "rollback closes own callback gate");
    }
    fail_install = 0;
    ready(palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, &reader}), "enable failure configure");
    fake_native_functions(); install_count = 0; fail_enable = true;
    require(!palette::install() && installed.empty(), "enable failure removes created hook transactionally");
    fail_enable = false;
    setup(); fail_disable = true;
    require(!palette::uninstall() && !installed.empty(), "disable failure retains trampolines/state");
    fail_disable = false; fail_remove = true;
    require(!palette::uninstall() && !installed.empty(), "remove failure retains hooks for retry");
    fail_remove = false; ready(palette::uninstall(), "safe teardown retry");
    image_supported = false;
    require(!palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, &reader}), "unsupported executable rejected");
    image_supported = true;
    auto& missing = specs.at(palette::contracts[0].id); const auto saved = missing.rva; missing.rva = 0;
    require(!palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, &reader}), "absent catalog ID fails closed");
    missing.rva = saved;
    auto& duplicate = specs.at(palette::contracts[1].id); const auto old = duplicate.rva; duplicate.rva = saved;
    require(!palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, &reader}), "duplicate setter address rejected");
    duplicate.rva = old;
}
void retirement_and_stale_regressions() {
    static Controller retiring_owners[4], incoming_owner;
    for (int operation = 0; operation != 4; ++operation) {
        setup(); initialize(palette::Property::Text, {0.8f, 0.4f, 0.2f, 0.8f});
        auto linked = palette::constant({0.8f, 0.4f, 0.2f, 0.8f});
        put_controller(linked, &retiring_owners[operation]);
        std::memcpy(widget.data() + 0x148, &linked, sizeof(linked));
        const auto s = subject(palette::Property::Text);
        if (operation) ready(palette::bind(s, policy(), authority()), "retirement regression initial bind");
        const auto before = consumes; same_reentry = Reentry::Retire;
        if (operation == 0) require(!palette::bind(s, policy(), authority()), "bind notices immediate revocation");
        if (operation == 1) require(!palette::change(s, policy(), authority()), "change notices immediate revocation");
        if (operation == 2) require(!palette::restore(s, authority()), "restore notices immediate revocation");
        if (operation == 3) {
            auto input = palette::constant({0.3f, 0.4f, 0.5f, 0.6f});
            put_controller(input, &incoming_owner);
            invoke_color(7, widget.data(), &input);
        }
        require(consumes == before + 1, "reentrant retirement preserves exact single native argument consumption");
        require(revoked && revoked->occupied && !revoked->active && !revoked->subject.validate,
            "revoked slot stays reserved, context detached, no owning cleanup");
        require(operation == 3 ? incoming_owner.references == 1
            : retiring_owners[operation].references == (operation == 2 ? 3 : 1),
            "reentrant retirement never releases the orphan controller owner");
        require(!palette::find(widget.data(), s.property), "revocation removes routing before return");
        ready(palette::uninstall(), "revoked context teardown never validates freed context");
    }
    for (auto p : {palette::Property::Image, palette::Property::Text}) {
        for (auto action : {Reentry::Color, Reentry::Opacity}) {
            setup(); initialize(p, {0.8f, 0.4f, 0.2f, 0.8f}); auto s = subject(p);
            ready(palette::bind(s, policy(), authority()), "same-subject reentry initial bind");
            same_reentry = action;
            auto source = palette::constant({0.8f, 0.7f, 0.6f, 0.5f});
            invoke_color(palette::index(p), widget.data(), &source);
            const auto actual = read_color(p); const auto calls = setter_calls;
            if (!palette::contracts[palette::index(p)].owning)
                require(!palette::find(s.widget, p)->property_source_current,
                    "same-subject native reentry leaves property source uncertain");
            require(!palette::restore(s, authority()) && !palette::change(s, policy(), authority()),
                "same-subject forwarded color/opacity invalidates baseline");
            require(setter_calls == calls && read_color(p) == actual, "no replay over newer reentrant native value");
            invoke_opacity(p == palette::Property::Text ? 15 : 11, widget.data(), 0.42f);
            require(!palette::restore(s, authority()), "opacity alone never recovers stale RGB");
            auto recovery = palette::constant({0.9f, 0.6f, 0.3f, 0.8f});
            invoke_color(palette::index(p), widget.data(), &recovery);
            ready(palette::restore(s, authority()), "qualified full incoming source recovers stale snapshot");
            expect(p, {0.9f, 0.6f, 0.3f, 0.8f}, "recovery uses input, not possibly themed current widget");
            finish();
        }
    }
    setup(); initialize(palette::Property::Image, {0.8f, 0.4f, 0.2f, 0.8f});
    auto s = subject(palette::Property::Image); ready(palette::bind(s, policy(), authority()), "foreign-thread binding");
    palette::Linear native{0.2f, 0.3f, 0.4f, 0.5f};
    std::thread foreign([&] { invoke_color(0, widget.data(), &native); }); foreign.join();
    require(!palette::restore(s, authority()), "foreign-thread forwarded input atomically marks source stale");
    palette::NativeInvocation observation{};
    require(!palette::current_invocation(observation) && !palette::invocation_matches(saved_invocation),
        "no saved invocation qualifies after real detour unwinds");
    const auto before = setter_calls;
    palette::intercept(0, widget.data(), &native); // Honest helper-only negative seam: no scope.
    require(setter_calls == before + 1 && !palette::restore(s, authority()), "helper alone cannot manufacture acquisition");
    invoke_color(0, widget.data(), &native);
    ready(palette::restore(s, authority()), "real detour plus qualified source recovers foreign-thread update");
    finish();
}
void overlapping_forward_regression() {
    for (const auto property : {palette::Property::Image, palette::Property::TextBottom}) for (bool opacity : {false, true}) {
        setup(); initialize(property, {0.8f, 0.4f, 0.2f, 0.8f});
        auto s = subject(property);
        const auto method = palette::index(property);
        ready(palette::bind(s, policy(), authority()), "overlapping forward initial bind");
        auto* source = palette::find(widget.data(), s.property);
        {
            std::lock_guard lock(forward_mutex);
            pause_foreign = true; foreign_pending = release_foreign = false;
        }
        palette::Linear foreign_value{0.22f, 0.33f, 0.44f, 0.55f};
        auto foreign_color = palette::constant(foreign_value);
        std::thread foreign([&] {
            if (opacity) invoke_opacity(property == palette::Property::Image ? 11 : 16, widget.data(), 0.77f);
            else invoke_color(method, widget.data(), &foreign_color);
        });
        {
            std::unique_lock lock(forward_mutex);
            require(forward_changed.wait_for(lock, std::chrono::seconds(5), [] { return foreign_pending; }),
                "foreign Original reached pre-write barrier");
        }
        require(source && source->forwarding.load() == 1 && !palette::fresh(*source),
            "matched forwarded Original remains outstanding before its write");
        palette::Linear premature{0.9f, 0.6f, 0.3f, 0.8f};
        auto premature_color = palette::constant(premature);
        const auto calls = setter_calls;
        invoke_color(method, widget.data(), &premature_color);
        require(setter_calls == calls + 1 && source->forwarding.load() == 1 && !palette::fresh(*source),
            "qualified input during outstanding forward forwards once, cannot recover source");
        const auto after_input = setter_calls;
        const auto after_input_attributes = bottom_attribute_calls;
        require(!palette::change(s, policy(), authority()) && !palette::restore(s, authority())
            && setter_calls == after_input && bottom_attribute_calls == after_input_attributes,
            "pending forward blocks stale color AND owning attribute change/restore");
        const auto revision = source->updates.load();
        {
            std::lock_guard lock(forward_mutex); release_foreign = true;
        }
        forward_changed.notify_all(); foreign.join();
        require(source->forwarding.load() == 0 && source->updates.load() != revision && !palette::fresh(*source),
            "completion invalidates projection even after owner's intervening qualified input");
        expect(s.property, opacity ? palette::Linear{0.9f, 0.6f, 0.3f, 0.77f} : foreign_value,
            "foreign Original performs its actual late native write");
        const auto completed_calls = setter_calls;
        const auto completed_attributes = bottom_attribute_calls;
        require(!palette::restore(s, authority()) && !palette::change(s, policy(), authority())
            && setter_calls == completed_calls && bottom_attribute_calls == completed_attributes,
            "completion alone does not bless pre-completion color or attribute source");
        palette::Linear recovery{0.7f, 0.5f, 0.3f, 0.9f};
        auto recovery_color = palette::constant(recovery);
        invoke_color(method, widget.data(), &recovery_color);
        ready(palette::restore(s, authority()), "new qualified full input AFTER completion recovers source");
        expect(s.property, recovery, "restoration uses only post-completion untinted input");
        if (property == palette::Property::TextBottom) require(live_attribute()->bytes[0x28] == 1,
            "post-forward recovery restores newest native set attribute, never opening unset flag");
        finish();
    }
}
} // namespace

// Fake MinHook preserves the actual RawRvaHook install/disable/remove control flow.
MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_Uninitialize() { return MH_OK; }
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID) { std::abort(); }
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID) { std::abort(); }
MH_STATUS WINAPI MH_ApplyQueued() { std::abort(); }
MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID, LPVOID* original) {
    if (++install_count == fail_install) return MH_ERROR_UNSUPPORTED_FUNCTION;
    const auto offset = reinterpret_cast<std::uintptr_t>(target) - reinterpret_cast<std::uintptr_t>(module);
    for (std::size_t i = 0; i < palette::kMethods; ++i) if (specs.at(palette::contracts[i].id).rva == offset) {
        require(!installed.contains(target), "single owner per hooked address");
        installed.emplace(target, i); *original = natives[i]; return MH_OK;
    }
    return MH_ERROR_UNSUPPORTED_FUNCTION;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return fail_enable ? MH_ERROR_MEMORY_PROTECT : MH_OK; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { return fail_disable ? MH_ERROR_MEMORY_PROTECT : MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID target) {
    if (fail_remove) return MH_ERROR_MEMORY_PROTECT;
    installed.erase(target); return MH_OK;
}
const char* WINAPI MH_StatusToString(MH_STATUS) { return "fake MinHook status"; }
namespace ff7r::piano::core {
bool is_supported_exe(HMODULE candidate, PeIdentity*) { return image_supported && candidate == module; }
}
namespace ff7r::piano::game {
core::HookCallbackGate& non_audio_hook_gate() { return shared_gate; }
const RvaSignatureSpec* find_rva_signature(std::string_view id) {
    auto it = specs.find(std::string(id)); return it == specs.end() ? nullptr : &it->second;
}
bool same_menu_session(const MenuSessionSnapshot& a, const MenuSessionSnapshot& b, bool phase) noexcept {
    return a.widget == b.widget && a.generation == b.generation && (!phase || a.phase == b.phase);
}
bool menu_session_matches(std::uint64_t generation, void* owner, const UObjectLiveHandle&, bool ready) noexcept {
    return generation == session.generation && owner == session.widget
        && (!ready || session.phase == MenuSessionPhase::Ready);
}
}
int main() {
    prepare_catalog(); std::string error; require(core::initialize_hooks(error), "initialize fake MinHook");
    dynamic_source_regression(); matching_retirement_regression(); proof_phase_regression(); failed_restoration_retirement_regression(); passive_registration_regression(); passive_capture_reentry_regression(); bottom_attribute_regression(); routed_filter_regression(); subject_retirement_regression(); supported_properties(); owning_and_nesting(); filtering_reentry_cleanup(); retirement_and_stale_regressions(); overlapping_forward_regression(); property_feedback_regression(); direct_property_write_regression(); uncertain_property_regression(); faults_and_transaction();
    require(core::shutdown_hooks(), "shutdown fake MinHook");
    require(core::initialize_hooks(error), "initialize retained MinHook");
    ready(palette::configure(module, FF7RP_TARGET_BUILD_ID, {nullptr, &reader}), "configure retained palette");
    fake_native_functions(); install_count = fail_install = 0;
    core::retain_hook_trampolines_for_process_lifetime();
    fail_enable = true;
    require(!palette::install(), "enable failure retains locally published original");
    fail_enable = false;
    const auto retained_original = palette::state.originals[0];
    require(retained_original && palette::state.hooks[0].installed() && installed.size() == 1,
        "failed enable cannot remove owned trampoline or erase original");
    const auto retained_cleanup = palette::uninstall();
    require(!retained_cleanup && std::string_view(retained_cleanup.stage) == "process_lifetime_retained"
        && palette::state.reader.current == &reader && palette::state.configured,
        "retained palette keeps reader and configured module state");
    require(!palette::state.hooks[0].remove() && !core::shutdown_hooks()
        && palette::state.originals[0] == retained_original && installed.size() == 1,
        "explicit remove and uninitialize cannot free retained originals");
    require(!palette_patch_writes, "color data projection never changes page protection or flushes executable code");
    std::cout << "menu_theme_palette_selftest passed\n";
}
