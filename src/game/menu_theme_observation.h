#pragma once

#include "game/menu_session_authority.h"
#include "game/uobject_locator_core.h"

#include <cstdint>
#include <string_view>
#include <windows.h>

namespace ff7r::piano::game::menu_theme_observation {

enum class Failure : unsigned char {
    None, UnsupportedImage, MissingSpec, Signature, Mapping, RipReference, TlsContract,
    Session, Caller, InitializedRead, ThreadRead, GarbageRead, ExitRead,
    TlsVectorRead, TlsBlockRead, TlsRoleRead, IdentityAccess,
    PackageSlotRead, PackageNull, PackageFields, PackageMembership,
    PackageClassFields, PackageClassMembership, PackageReread, PackageDrift,
    ThreadUninitialized, WrongThread, WrongTlsRole, GarbageCollecting, ExitPurge,
    PackageNotRooted, PackageOuter, BetweenSnapshots, ObserverException, Incomplete, AuthenticationBusy,
};
enum class Status : unsigned char { Observed, Unavailable, Drift };

struct ObjectFacts {
    std::uintptr_t object = 0, klass = 0, outer = 0;
    std::uint32_t name_comparison = 0, name_number = 0, object_flags = 0;
    std::int32_t index = -1, serial = -1, item_flags = 0;
    uobject_locator_core::UObjectLiveHandleCaptureResult live_result{};
    uobject_locator_core::UObjectItemBackedZeroSerialCaptureResult zero_result{};
    bool fields = false, membership = false, diagnostic_zero_serial = false;
    bool operator==(const ObjectFacts&) const = default;
};

// Copied read-only facts, NEVER a construction/borrow token or lifetime authority.
// Raw name IDs do not verify Package, /Engine/Transient or any other string.
struct Snapshot {
    Failure failure = Failure::None;
    const char* failure_site = "none";
    bool authenticated = false, complete = false;
    MenuSessionSnapshot session{};
    std::uint64_t expected_generation = 0;
    std::uintptr_t caller = 0, expected_caller = 0, tls_vector = 0, tls_block = 0;
    std::uint32_t current_thread = 0, native_thread = 0, tls_role = 0, garbage_collecting = 0;
    std::uint8_t thread_initialized = 0, exit_purge = 0;
    ObjectFacts package{}, package_class{};
};
struct Classification {
    Status status = Status::Unavailable;
    Failure first_failure = Failure::None;
    bool stable = false;
};

Failure classify(const Snapshot&) noexcept;
// Copied native facts only: NO callback provenance, retention or allocation
// authority. Requires completed capture; the current owned stack must qualify itself.
Failure classify_native_facts(const Snapshot&) noexcept;
// Game-thread/GC/package facts only; deliberately grants no Ready-menu authority.
Failure classify_engine_facts(const Snapshot&) noexcept;
Snapshot capture_engine_facts(HMODULE, std::string_view exact_profile) noexcept;
// Immutable authentication is reused only for the process-lifetime main executable
// and exact compiled profile. All session/thread/TLS/GC/package reads remain fresh.
// The normal open observer still authenticates independently on every capture.
Snapshot capture_native_facts(HMODULE borrowed_module, std::string_view exact_profile,
    std::uint64_t ready_generation) noexcept;
Classification classify(const Snapshot& before, const Snapshot& after) noexcept;
Snapshot capture(HMODULE borrowed_module, std::string_view exact_profile,
    std::uint64_t opening_generation, std::uintptr_t exact_return_site) noexcept;
// Formatting uses copied facts only, after capture/session/identity reads finish.
void report(const Snapshot& before, const Snapshot& after) noexcept;

} // namespace ff7r::piano::game::menu_theme_observation
