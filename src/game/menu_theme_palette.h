#pragma once

#include "game/menu_theme_native.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <string_view>

namespace ff7r::piano::game::menu_theme_palette {

using Authority = menu_theme_native::CallbackAuthority;
using Result = menu_theme_native::Result;
using Outcome = menu_theme_native::Outcome;

// Only observed fixed class properties. BrushTint belongs to the image/style adapter.
enum class Property : unsigned char {
    Image, PieImage, TiledImage, ImageSet, Button, ButtonBackground,
    PanelContent, Text, TextBottom, TextOutline, TextShadow, Count
};
struct Policy {
    struct Color { bool enabled = false; std::array<float, 4> linear{1, 1, 1, 1}; };
    Color normal{}, selected{};
    bool is_selected = false; // Semantic owner policy, never inferred from native RGB.
};
struct Subject {
    void* widget = nullptr;
    void* owner = nullptr;
    std::uint64_t generation = 0;
    Property property = Property::Image;
    void* context = nullptr;
    // Read-only, independently checks CURRENT class, identity and strong ownership.
    // Context remains alive until retire returns (or uninstall has drained).
    bool (*validate)(void*, const Subject&, const MenuSessionSnapshot&) noexcept = nullptr;
};
struct NativeInvocation;
struct CallbackReader {
    void* context = nullptr;
    // Called on the ACTUAL native setter stack, not a saved mutation token.
    // Supplies a candidate whose full qualifier palette invokes before use.
    // The reader need not duplicate that expensive native-fact capture itself.
    // This context outlives successful uninstall (including disable/drain retries).
    bool (*current)(void*, const NativeInvocation&, Authority&) noexcept = nullptr;
};
// Value-only observation, NOT mutation authority. Only a real detour establishes
// its private TLS scope. Runtime independently checks live frame/caller/module,
// lease, engine/GC/session and retirement facts throughout its synchronous use.
struct NativeInvocation {
    void* return_slot = nullptr;
    void* caller = nullptr;
    void* widget = nullptr;
    Property property = Property::Image;
    bool opacity = false;
    DWORD thread = 0;
    std::uint64_t serial = 0; // Distinguishes later calls reusing the same frame.
};
bool current_invocation(NativeInvocation&) noexcept;
// Metadata-only comparison against the real detour's private TLS witness.
bool invocation_matches(const NativeInvocation&) noexcept;

// No game calls. Stable IDs are resolved/authenticated from the selected catalog.
Result configure(HMODULE, std::string_view exact_profile, CallbackReader) noexcept;
// After MinHook initialization; all setters install or the partial set unwinds.
Result install() noexcept;
// Disable, close, drain, then remove. Does NOT invoke native owning cleanup.
// Caller retires bindings with authority first, or leaves retained orphan owners.
Result uninstall(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) noexcept;

// Caller holds the existing non-audio callback lease throughout explicit operations.
// A binding captures the actual fixed property before the first application.
// Stable source storage grows as needed; no count quota. Allocation/size failure
// returns Budget before capture/application, without publishing this subject.
// Disabled active policy captures/registers only (owning copies retain modes).
// Disabled changes/restoration/retirement emit no setter unless an override was applied.
// A certainly genuine, nonoverridden nonowning/attribute-free binding retires by exact metadata detach;
// it needs no native authority or live validator context because no native owner exists.
// TextBottom separately owns the Slate attribute baseline (including unset/delegate
// state), captured before override and advanced by qualified native bottom updates.
// Restore requires the same current Slate pair; identity loss retains orphan owners.
// Successful themed calls retain presentation in Slate, then write only the genuine
// source back to the UObject property under current authority/revision/sequence.
// This prevents subsequent property readers feeding our RGB back into animation;
// it cannot recover already contaminated caches or guarantee transient native reentry.
// Direct image opacity briefly projects RGB for its native Slate refresh, then
// restores the genuine property; no additional setter or synchronization is used.
Result bind(const Subject&, const Policy&, const Authority&) noexcept;
Result change(const Subject&, const Policy&, const Authority&) noexcept;
// Forwarded/unqualified updates invalidate the source. Change/restore cannot
// replay it; only a qualified full-color input (not opacity/widget recapture)
// after all matched forwarded Originals complete can establish a fresh untinted
// source. Retirement then retains orphan owners.
Result restore(const Subject&, const Authority&) noexcept;
// Owner-thread retirement clears validator references before return, including
// reentry (which revokes immediately and retains orphan ownership). Other-thread
// calls fail without detaching: contexts must then survive owner-thread retirement
// or successful drained uninstall. No
// owning destructor is run after admission loss; orphan storage remains retained.
Result retire(void* owner, std::uint64_t generation, const Authority&) noexcept;
// Recycle one property without retiring other rows/properties of the owner.
// Same context-lifetime, owner-thread and orphan rules as owner-wide retirement.
Result retire_subject(const Subject&, const Authority&) noexcept;
// One storage pass. Predicate runs only for active bindings of this owner/generation.
// Constant-time metadata only: no native reads, validators, palette reentry or scans
// of requested subjects. Context must survive the entire synchronous call/reentry;
// subject contexts still follow the retirement lifetime rules above.
using RetirementMatch = bool (*)(void* context, const Subject&) noexcept;
Result retire_matching(void* owner, std::uint64_t generation, RetirementMatch,
    void* context, const Authority&) noexcept;
Result last_failure() noexcept; // Copied facts only; no native reads/logging.
// Thread-safe metadata only: allocated nodes (including reusable/orphan records),
// NOT a capacity/admission limit or a count of live bindings. No native reads.
std::size_t source_storage_count() noexcept;

// Setter/custom-accessor coverage is limited to the contracts in
// analysis/MenuThemeStyles-20261021.json. No shared animation assets are changed.
} // namespace ff7r::piano::game::menu_theme_palette
