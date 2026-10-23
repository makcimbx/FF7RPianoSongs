#pragma once

#include "core/theme.h"
#include "game/menu_session_authority.h"
#include "game/uobject_locator_core.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <limits>
#include <string_view>
#include <windows.h>

namespace ff7r::piano::game::menu_theme_native {

// Corruption-defense bound for a single current UI traversal, NOT a resource quota.
inline constexpr std::size_t kSubjectTraversalLimit = 256;
inline constexpr std::size_t kInvalidImageSlot = std::numeric_limits<std::size_t>::max();
inline constexpr std::uint64_t kRetainedByteLimit = 128ull * 1024 * 1024;
// Native brush storage size; admission also charges its carrier and actual C++ ledger allocations.
inline constexpr std::size_t kOriginalCaptureByteCharge = 0x88;

enum class Outcome : unsigned char {
    Ready, Unavailable, UnsupportedImage, Signature, Busy, Authority,
    Input, Budget, Package, Identity, Holder, FreshLayout, Memory, NativeFault,
};
struct Result {
    Outcome outcome = Outcome::Unavailable;
    const char* stage = "unconfigured";
    // Copied during the production read, never fetched again for diagnostics.
    bool mip_header_read = false;
    std::int32_t mip_count = 0, mip_capacity = 0;
    explicit operator bool() const noexcept { return outcome == Outcome::Ready; }
};

// NOT an admission token by itself. The ordinary menu callback owner must
// qualify its CURRENT stack, established game thread, live engine/outside-GC and
// shutdown exclusion for the ENTIRE synchronous call (including local cleanup).
// A worker/WndProc, saved snapshot, boolean or thread ID alone cannot qualify.
// The runtime composition supplies the live qualifier; this value never owns it.
struct CallbackAuthority {
    MenuSessionSnapshot session{};
    DWORD game_thread_id = 0;
    void* context = nullptr;
    bool (*qualify_current_callback)(void*, const MenuSessionSnapshot&) noexcept = nullptr;
    // Optional metadata-only revocation guard for an adapter-private uninterrupted
    // phase. Checks actual invocation/retirement/lease/session, not native facts.
    // Null preserves full qualification per use; never standalone authority.
    bool (*phase_current)(void*, const MenuSessionSnapshot&) noexcept = nullptr;
};
enum class SubjectKind : unsigned char { Image, PieImage, TiledImage, ImageSet, Button, TextBlock, CanvasPanel };
enum class RowKind : unsigned char { Song, Speed };
enum class ButtonBrush : unsigned char { Normal, Hovered, Pressed, Disabled };
struct BrushSelection { std::int32_t index = 0; ButtonBrush button = ButtonBrush::Normal; };
struct RowAssociation {
    RowKind kind = RowKind::Song;
    std::int32_t logical_index = -1;
    std::uint64_t session_generation = 0, setup_generation = 0;
    // Local SpeedIcon reconciliation epoch, NEVER native setup provenance.
    std::uint64_t membership_epoch = 0;
};
class RowTicket final {
public:
    RowTicket() = default;
private:
    std::uint64_t id_ = 0;
    friend Result bind_row(RowKind, void*, std::int32_t, std::uint64_t, const CallbackAuthority&, RowTicket&) noexcept;
    friend Result bind_speed_row(void*, std::uint64_t, const CallbackAuthority&, RowTicket&) noexcept;
    friend Result find_subject(std::wstring_view, SubjectKind, const RowTicket&, const CallbackAuthority&, class Subject&) noexcept;
};

namespace detail {
struct ObjectSnapshot {
    void* pointer = nullptr;
    uobject_locator_core::UObjectLiveHandle live{};
    uobject_locator_core::UObjectItemBackedZeroSerialSnapshot zero{};
    void* klass = nullptr;
    void* outer = nullptr;
    std::uint64_t name = 0;
};
struct TargetState {
    ObjectSnapshot receiver{}, tree{}, root{}, tree_class{}, target{}, target_class{};
    ObjectSnapshot cursor_main{}, cursor_list{};
    bool cursor_owner = false; // current SelectList.CursorWidget provider, NOT a row ticket
    std::uint64_t generation = 0;
    // Name witnesses only, never a saved containment path or ancillary weak lease.
    std::uint64_t panel_name = 0, slot_name = 0, umg_name = 0;
    std::uint64_t class_name = 0, package_name = 0, core_package_name = 0;
    std::uint32_t target_flags = 0;
    std::int32_t target_item_flags = 0;
    bool generic = false, brush_selected = true;
    SubjectKind kind = SubjectKind::Image;
    std::uint64_t row_id = 0;
    std::uintptr_t brush_offset = 0x108;
    std::uintptr_t images = 0;
    std::int32_t image_count = 0, image_capacity = 0, image_index = 0;
};
}
struct BrushAssignment;
struct ApplyProgress;
struct CaptureProgress;
struct ResolveMember;
struct ResolvedMember;
struct SubjectPreflight;
struct BatchProgress;
class Subject final {
public:
    Subject() = default;
private:
    detail::TargetState facts_{};
    friend Result find_subject(std::wstring_view, SubjectKind, const RowTicket&, const CallbackAuthority&, Subject&) noexcept;
    friend Result find_cursor_subject(std::wstring_view, const CallbackAuthority&, Subject&, SubjectKind) noexcept;
    friend bool validate_subject(const Subject&, const MenuSessionSnapshot&) noexcept;
    friend Result access_subject(const Subject&, const CallbackAuthority&, void*&) noexcept;
    friend Result capture_original(const Subject&, BrushSelection, const CallbackAuthority&, class OwnedImage&) noexcept;
    friend Result apply(const Subject&, BrushSelection, const class OwnedImage&, const CallbackAuthority&) noexcept;
    friend Result apply_members(const Subject&, std::span<const BrushAssignment>, const CallbackAuthority&, ApplyProgress&) noexcept;
    friend Result capture_original_members(const Subject&, std::span<const BrushSelection>, const CallbackAuthority&, std::span<class OwnedImage>, CaptureProgress&) noexcept;
    friend bool subject_row_association(const Subject&, RowAssociation&) noexcept;
    friend Result resolve_members(std::span<const ResolveMember>, const CallbackAuthority&, std::span<ResolvedMember>, BatchProgress&) noexcept;
    friend Result preflight_subjects(std::span<const SubjectPreflight>, const CallbackAuthority&, BatchProgress&) noexcept;
};
class OwnedImage;
class ImageTarget final {
public:
    ImageTarget() = default;
private:
    detail::TargetState facts_{};
    friend Result find_image(std::wstring_view, const CallbackAuthority&, ImageTarget&) noexcept;
    friend Result capture_original(const ImageTarget&, const CallbackAuthority&, class OwnedImage&) noexcept;
    friend Result apply(const ImageTarget&, const class OwnedImage&, const CallbackAuthority&) noexcept;
    friend Result capture_original(const Subject&, BrushSelection, const CallbackAuthority&, class OwnedImage&) noexcept;
    friend Result apply(const Subject&, BrushSelection, const class OwnedImage&, const CallbackAuthority&) noexcept;
};

class OwnedImage final {
public:
    OwnedImage() = default;
private:
    std::size_t slot_ = kInvalidImageSlot;
    // Per-capture affinity is NOT shared with immutable deduplicated storage.
    detail::TargetState affinity_{};
    bool target_bound_ = false;
    friend Result create(const core::ThemeImage&, const CallbackAuthority&, OwnedImage&) noexcept;
    friend Result borrow_brush(const OwnedImage&, const CallbackAuthority&, const void*&) noexcept;
    friend Result capture_original(const ImageTarget&, const CallbackAuthority&, OwnedImage&) noexcept;
    friend Result apply(const ImageTarget&, const OwnedImage&, const CallbackAuthority&) noexcept;
    friend Result apply(const Subject&, BrushSelection, const OwnedImage&, const CallbackAuthority&) noexcept;
    friend Result apply_members(const Subject&, std::span<const BrushAssignment>, const CallbackAuthority&, ApplyProgress&) noexcept;
    friend Result capture_original_members(const Subject&, std::span<const BrushSelection>, const CallbackAuthority&, std::span<OwnedImage>, CaptureProgress&) noexcept;
    friend Result compose(const OwnedImage&, const OwnedImage&, const CallbackAuthority&, OwnedImage&) noexcept;
    friend Result tint(const OwnedImage&, const std::array<float, 4>&, const CallbackAuthority&, OwnedImage&) noexcept;
    friend Result resolve_members(std::span<const ResolveMember>, const CallbackAuthority&, std::span<ResolvedMember>, BatchProgress&) noexcept;
    friend Result preflight_subjects(std::span<const SubjectPreflight>, const CallbackAuthority&, BatchProgress&) noexcept;
};

// Borrowed module: no LoadLibrary, native invocation/allocation at configure.
// All callable policies are authenticated transactionally before publication.
// Reconfiguration/shutdown never clears the process-lifetime allocation budget.
Result configure(HMODULE borrowed_module, std::string_view exact_profile) noexcept;
void disable() noexcept; // composition must exclude callbacks first; no native cleanup
Result create(const core::ThemeImage&, const CallbackAuthority&, OwnedImage&) noexcept;
// Borrow only inside the same qualified callback; never save it across callbacks.
// Ready means upload dispatched, NOT renderer readiness or a theme applied to UI.
Result borrow_brush(const OwnedImage&, const CallbackAuthority&, const void*& brush) noexcept;

// Mutating native cache lookup, not observation. 1..127 ASCII alnum/_ leaf;
// no path syntax or receiver fallback.
// Each check rebuilds bounded panel-slot containment to the current RootWidget.
// The caller still owns the real synchronous lifetime/GC exclusion; no saved
// path, ancillary zero-serial weak lease, visibility or production qualifier.
Result find_image(std::wstring_view leaf, const CallbackAuthority&, ImageTarget&) noexcept;
// Native owning full-brush snapshot, including empty/untextured originals.
// Identical immutable storage may be reused, never its target/owner affinity.
// Zero endpoints require a freshly checked strong current path, not a weak lease
// or historical ABA certificate; the caller excludes destruction in its scope.
Result capture_original(const ImageTarget&, const CallbackAuthority&, OwnedImage&) noexcept;
// Prepare/cache a standalone-texture brush preserving the captured native shape.
// No upload; owning copy + UV invalidation + native cache/resource reset happen
// BEFORE apply. The returned brush retains the original's exact target affinity.
Result compose(const OwnedImage& original, const OwnedImage& prepared,
    const CallbackAuthority&, OwnedImage&) noexcept;
// Immutable cached native Brush.TintColor replacement (specified linear RGBA).
// Source must already have exact target/member affinity. No texture upload or
// resource/cache/geometry changes; apply the result through the same adapter.
Result tint(const OwnedImage& source, const std::array<float, 4>& linear_rgba,
    const CallbackAuthority&, OwnedImage&) noexcept;
// No borrowed pointer escapes. Ready means native assignment completed, not paint.
// Lost admission never triggers an automatic restore or deferred native cleanup.
Result apply(const ImageTarget&, const OwnedImage&, const CallbackAuthority&) noexcept;
struct BrushAssignment { BrushSelection member{}; const OwnedImage* image = nullptr; };
struct ApplyProgress {
    std::size_t attempted_members = 0; // native UI writes attempted, possibly uncertain on fault
    std::size_t completed_members = 0; // returned AND passed fresh post-native checks
    bool refresh_complete = false;
};
// Button/ImageSet only, distinct members of ONE current Subject. Native owning
// writes are not atomic: ImageSet can be partially assigned; Button publishes one
// complete style. Progress is factual, not rollback authority. Failure never
// restores automatically; lost admission/uncertain ownership forbids stale cleanup.
// No witness/authority survives this synchronous call; no switch-time upload.
Result apply_members(const Subject&, std::span<const BrushAssignment>,
    const CallbackAuthority&, ApplyProgress&) noexcept;
struct CaptureProgress {
    std::size_t prepared_members = 0; // validated cache hits or completed native captures
    std::size_t created_members = 0; // new immutable native holders completed
    std::size_t published_members = 0; // outputs: either zero or the entire requested set
};
// Distinct members of one current subject; outputs.size() must equal members.size().
// Warm hits share only an uninterrupted private read phase (two containment
// checks), never a caller token. Misses use the ordinary owning capture/reentry
// guards. Outputs publish together; completed immutable cache records and failed
// allocations remain retained/charged even if a later member fails. Progress is
// not a partial usable output or lifetime authority. No native copy on warm hits.
Result capture_original_members(const Subject&, std::span<const BrushSelection>,
    const CallbackAuthority&, std::span<OwnedImage> outputs, CaptureProgress&) noexcept;

struct BatchProgress {
    std::size_t completed = 0; // completed requests, not partial usable outputs
    std::size_t created = 0; // completed new immutable carriers, including intermediates
    std::size_t failed_index = kInvalidImageSlot;
};
struct ResolveMember {
    const Subject* subject = nullptr;
    BrushSelection member{};
    const OwnedImage* original = nullptr; // null captures the current genuine baseline
    const OwnedImage* prepared = nullptr; // null preserves the baseline resource
    bool tint = false;
    std::array<float, 4> linear_rgba{};
};
struct ResolvedMember { OwnedImage original{}, image{}; };
// Capture-only or capture/retained Original -> compose -> optional tint. Never
// uploads or calls create: global type/collision/upload admission stays upstream.
// All inputs are checked before construction; outputs publish together on Ready.
// Outputs must match request count and must not alias input owning values.
// Completed carriers/failed allocations stay charged on failure. No proof escapes;
// allocation/native/reentry ends the private shared owner/Subject/resource phase.
Result resolve_members(std::span<const ResolveMember>, const CallbackAuthority&,
    std::span<ResolvedMember> outputs, BatchProgress&) noexcept;
struct SubjectPreflight {
    const Subject* subject = nullptr;
    // Empty checks a property-only Subject. Multiple images may refer to the same
    // member (e.g. next/prior); every image must have exact member affinity.
    std::span<const BrushAssignment> images{};
};
// Read-only grouped association/retirement/Apply preflight; no borrowed pointers,
// native helpers, allocation or caller-owned authority tokens. Bounds each call
// to kSubjectTraversalLimit requests; guard-null retains ordinary fresh proofs.
Result preflight_subjects(std::span<const SubjectPreflight>, const CallbackAuthority&,
    BatchProgress&) noexcept;

struct Capabilities { bool subjects = false, extended_brushes = false, brush_tint = false; };
Capabilities capabilities() noexcept;
// Prepare existing FNAME_Find0 class/package witnesses in a qualified callback;
// no UI lookup/cache mutation, native construction or texture allocation.
Result prepare_subjects(const CallbackAuthority&) noexcept;
// Call after the ACTUAL native setup/rebind, with its logical index and setup
// generation. Every bind invalidates the previous ticket for that pooled row.
// A pool/array position is never converted to a logical index here.
// Legacy Speed callers must supply actual native ordinal/setup provenance;
// uniform SpeedIcon callers use bind_speed_row instead, never fabricated setup.
// Associations reuse dynamically grown, byte-accounted storage; traversal bounds
// do not limit their total. Retired tickets never become valid through reuse.
Result bind_row(RowKind, void* row, std::int32_t logical_index, std::uint64_t setup_generation,
    const CallbackAuthority&, RowTicket&) noexcept;
// Uniform SpeedIcon membership only: logical_index=-1, setup_generation=0.
// Reconcile at qualified boundaries; SelectedIndex changes do not retire tickets.
// Each bind retires the preceding ticket for that row, including failed rebinds.
Result bind_speed_row(void* row, std::uint64_t membership_epoch,
    const CallbackAuthority&, RowTicket&) noexcept;
struct SpeedRowsSnapshot {
    std::array<void*, kSubjectTraversalLimit> rows{};
    std::size_t count = 0;
};
// Copied current strong members, NOT saved authority or ordinal/setup provenance.
// No native calls; prepare_subjects must first prepare generic name witnesses.
// Feed rows to bind_speed_row within the qualified reconciliation boundary.
Result read_speed_rows(const CallbackAuthority&, SpeedRowsSnapshot&) noexcept;
// Empty RowTicket selects the Ready main receiver. Traversal reads typed strong
// Slots/Content edges only; it never invokes the native FName widget cache.
Result find_subject(std::wstring_view leaf, SubjectKind, const RowTicket&,
    const CallbackAuthority&, Subject&) noexcept;
// Current positive Ready MusicList -> positive SelectList weak -> strong
// CursorWidget -> its OWN WidgetTree/root/typed containment. Never CachedCursor,
// a fabricated row ticket, or the MusicList tree. Subject validation/access and
// owning brush APIs reread this provider after reentry. No named-effect support.
Result find_cursor_subject(std::wstring_view leaf, const CallbackAuthority&, Subject&,
    SubjectKind = SubjectKind::Image) noexcept;
// Copied-fact/read-only validation for a palette's supplied source validator.
// True grants NO mutation/lifetime/thread/GC authority and invokes no native code.
bool validate_subject(const Subject&, const MenuSessionSnapshot&) noexcept;
bool subject_row_association(const Subject&, RowAssociation&) noexcept;
// Borrowed object access ONLY during the caller-qualified callback.
Result access_subject(const Subject&, const CallbackAuthority&, void*&) noexcept;
Result capture_original(const Subject&, BrushSelection, const CallbackAuthority&, OwnedImage&) noexcept;
Result apply(const Subject&, BrushSelection, const OwnedImage&, const CallbackAuthority&) noexcept;

namespace detail {
// Small value-only seams also used by the production preflight/layout checks.
Result preflight(std::uint32_t width, std::uint32_t height, std::size_t bytes,
    std::uint64_t reserved_bytes) noexcept;
struct FreshMip {
    std::int32_t width = 0, height = 0, format = 0, count = 0, capacity = 0;
    std::int32_t mip_width = 0, mip_height = 0;
    std::uint64_t bytes = 0;
    std::uint32_t flags = 0;
    std::uint8_t lock = 0;
    std::uintptr_t platform = 0, array = 0, mip = 0, data = 0;
};
const char* fresh_mip_failure(const FreshMip&, std::uint32_t width,
    std::uint32_t height, std::size_t bytes) noexcept;
bool fresh_mip_matches(const FreshMip&, std::uint32_t width,
    std::uint32_t height, std::size_t bytes) noexcept;
} // namespace detail
} // namespace ff7r::piano::game::menu_theme_native
