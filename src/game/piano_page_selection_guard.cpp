#include "game/piano_page_selection_guard.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "game/hook_specs.h"
#include "game/module_hooks.h"
#include "game/rvas.h"
#include "game/song_registry.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>

namespace ff7r::piano::game::piano_page_selection_guard {
namespace {

void append_u64(std::vector<std::uint8_t>& bytes, const std::uintptr_t value)
{
    for (unsigned shift = 0; shift != 64; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

bool rel32_reachable(const void* instruction, const void* target) noexcept
{
    const auto source = reinterpret_cast<std::intptr_t>(instruction) + 5;
    const auto destination = reinterpret_cast<std::intptr_t>(target);
    const auto delta = destination - source;
    return delta >= std::numeric_limits<std::int32_t>::min()
        && delta <= std::numeric_limits<std::int32_t>::max();
}

Rel32PatchState classify_live_call(const void* call_site,
    const std::array<std::uint8_t, 5>& original, const void* original_target,
    const std::array<std::uint8_t, 5>& replacement, const void* replacement_target,
    const unsigned long original_protection, const PatchOperations& operations) noexcept
{
    try {
        std::array<std::uint8_t, 5> live{};
        unsigned long protection = 0;
        if (!operations.read(call_site, live.data(), live.size())
            || !operations.query_protection(call_site, protection)
            || protection != original_protection) return Rel32PatchState::Unsafe;
        const void* target = nullptr;
        if (!decode_rel32_target(call_site, live, target)) return Rel32PatchState::Unsafe;
        if (live == original && target == original_target) return Rel32PatchState::Original;
        if (live == replacement && target == replacement_target)
            return Rel32PatchState::Replacement;
    } catch (...) {}
    return Rel32PatchState::Unsafe;
}

bool restore_original(void* call_site, const std::array<std::uint8_t, 5>& original,
    const unsigned long original_protection, const PatchOperations& operations) noexcept
{
    try {
        unsigned long prior = 0;
        const bool writable = operations.protect(call_site, original.size(),
            PAGE_EXECUTE_READWRITE, &prior);
        const bool wrote = writable && operations.write(call_site, original.data(), original.size());
        const bool flushed = wrote && operations.flush(call_site, original.size());
        unsigned long ignored = 0;
        const bool restored = operations.protect(call_site, original.size(),
            original_protection, &ignored);
        std::array<std::uint8_t, 5> verified{};
        unsigned long final_protection = 0;
        return flushed && restored
            && operations.read(call_site, verified.data(), verified.size())
            && verified == original
            && operations.query_protection(call_site, final_protection)
            && final_protection == original_protection;
    } catch (...) { return false; }
}

#ifndef FF7RP_PIANO_PAGE_SELECTION_GUARD_SELFTEST

std::mutex g_marker_mutex;
BoundedMarkerSet<> g_markers;
void* g_relay = nullptr;
std::size_t g_relay_size = 0;

void publish_marker(const Marker& marker) noexcept
{
    try {
        bool admitted = false;
        {
            std::unique_lock lock(g_marker_mutex, std::try_to_lock);
            if (!lock) return;
            admitted = g_markers.insert(
                {marker.playback_generation, marker.selected_index});
        }
        if (!admitted) return;
        std::ostringstream out;
        out << "[piano_page_selection_guard] status="
            << (marker.unexpected_negative
                ? "unexpected_negative_bypassed" : "no_selection_bypassed")
            << " selected_index=" << marker.selected_index
             << " playback_generation=" << marker.playback_generation
             << " playback_available=" << marker.playback_available
             << " catalog_revision_exact=" << marker.catalog_revision_exact
             << " catalog_revision=" << marker.catalog_revision
             << " song_id=" << (marker.playback_available ? marker.song_id.data() : "unknown")
             << " profile_index=" << marker.profile_index
             << " visible_index=" << marker.visible_index
             << " base_slot=" << marker.base_slot
             << " difficulty=" << marker.difficulty
             << " note_count=" << marker.note_count;
        core::log(marker.unexpected_negative ? core::LogLevel::Error : core::LogLevel::Debug,
            out.str());
    } catch (...) {}
}

void __fastcall marker_entry(const std::int32_t selected_index) noexcept
{
    try {
        capture_and_emit_marker(selected_index, [](Marker& marker) {
            PlaybackSnapshot playback;
            if (!registry().try_playback_snapshot(playback)) return true;
            marker.playback_available = true;
            marker.playback_generation = playback.generation;
            marker.profile_index = playback.profile_index;
            marker.visible_index = playback.visible_index;
            marker.base_slot = playback.base_slot;
            if (playback.song) {
                const auto length = std::min(playback.song->id.size(), marker.song_id.size() - 1);
                std::memcpy(marker.song_id.data(), playback.song->id.data(), length);
                marker.song_id[length] = '\0';
            }
            if (playback.profile) {
                marker.difficulty = playback.profile->difficulty;
                marker.note_count = playback.profile->note_count;
            }
            RegistrySnapshot catalog;
            if (registry().try_registry_snapshot(catalog)
                && playback.storage && catalog.storage
                && playback.storage.get() == catalog.storage.get()) {
                marker.catalog_revision_exact = true;
                marker.catalog_revision = catalog.catalog_revision;
            }
            return true;
        }, [](const Marker& marker) { publish_marker(marker); });
    } catch (...) {}
}

PatchOperations native_patch_operations()
{
    return {
        [](const void* source, void* destination, const std::size_t size) {
            return core::safe_copy_bytes(source, destination, size);
        },
        [](void* destination, const void* source, const std::size_t size) {
            return core::safe_copy_bytes(source, destination, size);
        },
        [](void* address, const std::size_t size, const unsigned long protection,
            unsigned long* old_protection) {
            return VirtualProtect(address, size, protection, old_protection) != FALSE;
        },
        [](const void* address, const std::size_t size) {
            return FlushInstructionCache(GetCurrentProcess(), address, size) != FALSE;
        },
        [](const void* address, unsigned long& protection) {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return false;
            protection = info.Protect;
            return true;
        },
    };
}

void* allocate_near(const void* origin, const std::size_t size) noexcept
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto granularity = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    const auto center = reinterpret_cast<std::uintptr_t>(origin) & ~(granularity - 1);
    constexpr std::uintptr_t range = 0x7fff0000;
    for (std::uintptr_t distance = 0; distance <= range; distance += granularity) {
        const std::uintptr_t candidates[]{
            center >= distance ? center - distance : 0,
            center <= std::numeric_limits<std::uintptr_t>::max() - distance
                ? center + distance : 0,
        };
        for (const auto candidate : candidates) {
            if (!candidate) continue;
            void* result = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (result && rel32_reachable(origin, result)) return result;
            if (result) VirtualFree(result, 0, MEM_RELEASE);
        }
    }
    return nullptr;
}

bool signature_matches(HMODULE module, const char* id) noexcept
{
    const auto* spec = find_rva_signature(id);
    return spec && core::bytes_equal(reinterpret_cast<const std::uint8_t*>(module)
        + spec->rva, spec->expected_prologue);
}

#endif

} // namespace

void capture_and_emit_marker(const std::int32_t selected_index,
    const MarkerCapture& capture, const MarkerSink& sink) noexcept
{
    try {
        Marker marker;
        marker.selected_index = selected_index;
        marker.unexpected_negative = selected_index < -1;
        if (capture && capture(marker) && sink) sink(marker);
    } catch (...) {}
}

void dispatch_selected_index(const std::int32_t selected_index,
    const ResolverAction& resolver, const ContinuationAction& continuation) noexcept
{
    try {
        if (selected_index < 0) {
            if (continuation) continuation();
        } else if (resolver) {
            resolver();
        }
    } catch (...) {}
}

std::vector<std::uint8_t> make_guard_relay(const void* marker,
    const void* resolver, const void* continuation)
{
    // Negative: call the noexcept marker with aligned shadow space, discard the
    // patched CALL return, and jump to the native no-selection continuation.
    // Nonnegative: tail-jump the original resolver with the untouched return.
    std::vector<std::uint8_t> bytes{
        0x45, 0x85, 0xe4,                   // test r12d,r12d
        0x79, 0x23,                         // jns original
        0x48, 0x83, 0xec, 0x28,             // sub rsp,28h
        0x41, 0x8b, 0xcc,                   // mov ecx,r12d
        0x48, 0xb8                          // mov rax,marker
    };
    append_u64(bytes, reinterpret_cast<std::uintptr_t>(marker));
    bytes.insert(bytes.end(), {
        0xff, 0xd0,                         // call rax
        0x48, 0x83, 0xc4, 0x30,             // add rsp,30h (shadow + CALL return)
        0x48, 0xb8                          // mov rax,continuation
    });
    append_u64(bytes, reinterpret_cast<std::uintptr_t>(continuation));
    bytes.insert(bytes.end(), {
        0xff, 0xe0,                         // jmp rax
        0x48, 0xb8                          // original: mov rax,resolver
    });
    append_u64(bytes, reinterpret_cast<std::uintptr_t>(resolver));
    bytes.insert(bytes.end(), {0xff, 0xe0}); // jmp rax
    return bytes;
}

bool decode_rel32_target(const void* instruction,
    const std::array<std::uint8_t, 5>& bytes, const void*& target) noexcept
{
    target = nullptr;
    if (!instruction || bytes[0] != 0xe8) return false;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    const auto next = reinterpret_cast<std::intptr_t>(instruction) + 5;
    target = reinterpret_cast<const void*>(next + displacement);
    return true;
}

bool make_rel32_call(const void* instruction, const void* target,
    std::array<std::uint8_t, 5>& bytes) noexcept
{
    if (!instruction || !target || !rel32_reachable(instruction, target)) return false;
    bytes[0] = 0xe8;
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(target)
        - (reinterpret_cast<std::intptr_t>(instruction) + 5));
    std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    return true;
}

bool replace_rel32_call_transactionally(void* call_site,
    const std::array<std::uint8_t, 5>& original, const void* original_target,
    const void* replacement_target, const PatchOperations& operations,
    Rel32PatchState& state, std::string& error) noexcept
{
    state = Rel32PatchState::Unsafe;
    unsigned long original_protection = 0;
    bool mutation_started = false;
    std::array<std::uint8_t, 5> replacement{};
    try {
        if (!call_site || !operations.read || !operations.write || !operations.protect
            || !operations.flush || !operations.query_protection
            || !make_rel32_call(call_site, replacement_target, replacement)
            || !operations.query_protection(call_site, original_protection)) {
            error = "preflight_failed";
            return false;
        }
        std::array<std::uint8_t, 5> current{};
        const void* current_target = nullptr;
        if (!operations.read(call_site, current.data(), current.size())) {
            error = "call_read_failed";
            return false;
        }
        if (!decode_rel32_target(call_site, current, current_target)
            || current != original || current_target != original_target) {
            state = classify_live_call(call_site, original, original_target,
                replacement, replacement_target, original_protection, operations);
            error = state == Rel32PatchState::Replacement
                ? "replacement_already_live" : "unsafe_live_call";
            return false;
        }
        state = Rel32PatchState::Original;
        unsigned long captured = 0;
        if (!operations.protect(call_site, replacement.size(), PAGE_EXECUTE_READWRITE,
                &captured)) {
            error = "protect_write_failed";
            return false;
        }
        mutation_started = true;
        if (captured != original_protection) {
            unsigned long ignored = 0;
            const bool restored = operations.protect(
                call_site, replacement.size(), captured, &ignored);
            unsigned long verified_protection = 0;
            state = Rel32PatchState::Unsafe;
            error = restored
                    && operations.query_protection(call_site, verified_protection)
                    && verified_protection == captured
                ? "protection_drift" : "protection_drift_restore_failed";
            return false;
        }
        const bool wrote = operations.write(call_site, replacement.data(), replacement.size());
        const bool flushed = wrote && operations.flush(call_site, replacement.size());
        unsigned long ignored = 0;
        const bool protected_back = operations.protect(call_site, replacement.size(),
            original_protection, &ignored);
        std::array<std::uint8_t, 5> verified{};
        unsigned long final_protection = 0;
        const void* verified_target = nullptr;
        if (flushed && protected_back
            && operations.read(call_site, verified.data(), verified.size())
            && verified == replacement
            && decode_rel32_target(call_site, verified, verified_target)
            && verified_target == replacement_target
            && operations.query_protection(call_site, final_protection)
            && final_protection == original_protection) {
            state = Rel32PatchState::Replacement;
            return true;
        }
        // A failed write API does not prove that zero bytes changed. After any
        // attempted write, only a fully verified rollback can make the site
        // safe again. Rereading original-looking data bytes is insufficient
        // when rollback or its instruction-cache flush failed.
        const bool rollback_ok = restore_original(
            call_site, original, original_protection, operations);
        state = rollback_ok
            ? classify_live_call(call_site, original, original_target,
                replacement, replacement_target, original_protection, operations)
            : Rel32PatchState::Unsafe;
        error = !wrote ? "write_failed" : "write_or_verify_failed";
        return false;
    } catch (...) {
        if (mutation_started) {
            const bool rollback_ok = restore_original(
                call_site, original, original_protection, operations);
            state = rollback_ok
                ? classify_live_call(call_site, original, original_target,
                    replacement, replacement_target, original_protection, operations)
                : Rel32PatchState::Unsafe;
        }
        error = "exception";
        return false;
    }
}

#ifndef FF7RP_PIANO_PAGE_SELECTION_GUARD_SELFTEST
InstallResult install(HMODULE exe_module) noexcept
{
    static_assert(rva::PianoPageVisibilityCall == rva::PianoPageSelectedResolveCall + 13);
    static_assert(rva::PianoPageNoSelectionContinuation == rva::PianoPageVisibilityCall + 5);
    try {
        if (!exe_module
            || !signature_matches(exe_module, "piano_page_selected_resolve_call")
            || !signature_matches(exe_module, "piano_page_visibility_call")
            || !signature_matches(exe_module, "piano_page_no_selection_continuation")
            || !signature_matches(exe_module, "weak_object_resolver")
            || !signature_matches(exe_module, "widget_visibility_setter")) {
            core::log(core::LogLevel::Error,
                "[piano_page_selection_guard] status=install_failed stage=signature");
            return InstallResult::Unavailable;
        }
        auto* call_site = reinterpret_cast<std::uint8_t*>(exe_module)
            + rva::PianoPageSelectedResolveCall;
        std::array<std::uint8_t, 5> original{};
        if (!core::safe_copy_bytes(call_site, original.data(), original.size()))
            return InstallResult::Unavailable;
        const void* decoded_resolver = nullptr;
        if (!decode_rel32_target(call_site, original, decoded_resolver)
            || decoded_resolver != reinterpret_cast<std::uint8_t*>(exe_module)
                + rva::WeakObjectResolver) return InstallResult::Unavailable;
        std::array<std::uint8_t, 5> visibility{};
        auto* visibility_call = reinterpret_cast<std::uint8_t*>(exe_module)
            + rva::PianoPageVisibilityCall;
        const void* decoded_setter = nullptr;
        if (!core::safe_copy_bytes(visibility_call, visibility.data(), visibility.size())
            || !decode_rel32_target(visibility_call, visibility, decoded_setter)
            || decoded_setter != reinterpret_cast<std::uint8_t*>(exe_module)
                + rva::WidgetVisibilitySetter) return InstallResult::Unavailable;

        const auto relay_bytes = make_guard_relay(reinterpret_cast<const void*>(&marker_entry),
            decoded_resolver, reinterpret_cast<std::uint8_t*>(exe_module)
                + rva::PianoPageNoSelectionContinuation);
        g_relay_size = relay_bytes.size();
        g_relay = allocate_near(call_site, g_relay_size);
        if (!g_relay) return InstallResult::Unavailable;
        std::memcpy(g_relay, relay_bytes.data(), relay_bytes.size());
        unsigned long old_protection = 0;
        if (!VirtualProtect(g_relay, g_relay_size, PAGE_EXECUTE_READ, &old_protection)
            || !FlushInstructionCache(GetCurrentProcess(), g_relay, g_relay_size)) {
            VirtualFree(g_relay, 0, MEM_RELEASE);
            g_relay = nullptr;
            return InstallResult::Unavailable;
        }
        MEMORY_BASIC_INFORMATION relay_info{};
        if (VirtualQuery(g_relay, &relay_info, sizeof(relay_info)) != sizeof(relay_info)
            || relay_info.Protect != PAGE_EXECUTE_READ) {
            VirtualFree(g_relay, 0, MEM_RELEASE);
            g_relay = nullptr;
            return InstallResult::Unavailable;
        }

        Rel32PatchState state = Rel32PatchState::Unsafe;
        std::string error;
        if (!replace_rel32_call_transactionally(call_site, original, decoded_resolver,
                g_relay, native_patch_operations(), state, error)) {
            if (state == Rel32PatchState::Original) {
                VirtualFree(g_relay, 0, MEM_RELEASE);
                g_relay = nullptr;
            }
            core::log(core::LogLevel::Error,
                "[piano_page_selection_guard] status=install_failed stage=patch error=" + error);
            return state == Rel32PatchState::Original
                ? InstallResult::Unavailable : InstallResult::UnsafeMutation;
        }
        core::log(core::LogLevel::Info,
            "[piano_page_selection_guard] status=installed owner=piano_page_selection_guard");
        return InstallResult::Installed;
    } catch (...) {
        core::log(core::LogLevel::Error,
            "[piano_page_selection_guard] status=install_failed stage=exception");
        return g_relay ? InstallResult::UnsafeMutation : InstallResult::Unavailable;
    }
}
#else
InstallResult install(HMODULE) noexcept { return InstallResult::Unavailable; }
#endif

} // namespace ff7r::piano::game::piano_page_selection_guard

namespace ff7r::piano::game {
bool install_piano_page_selection_guard(const HookInstallContext& context)
{
    const auto result = piano_page_selection_guard::install(context.exe_module);
    if (result == piano_page_selection_guard::InstallResult::UnsafeMutation) {
        core::log(core::LogLevel::Error,
            "[piano_page_selection_guard] status=unsafe_mutation action=fail_fast");
        RaiseFailFastException(nullptr, nullptr, 0);
        return false;
    }
    return result == piano_page_selection_guard::InstallResult::Installed;
}
} // namespace ff7r::piano::game
