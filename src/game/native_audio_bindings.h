#pragma once

#include "game/uobject_locator_core.h"
#include "game/runtime_build_profile.h"
#include "pipeline/native_audio_artifacts.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

namespace ff7r::piano::game::native_audio {

using Address = uobject_locator_core::Address;
using MemoryReader = uobject_locator_core::MemoryReader;

// Covers the repository's 123 custom-song limit plus existing native entries.
// Shared queue/group budget; native array capacity/field-width guards stay separate.
inline constexpr std::size_t kNativeAudioEntryLimit = 256;

// Only the exact compiled executable profile can be selected.
const NativeAudioLayout* native_audio_layout(std::string_view build_id) noexcept;

enum class BindingFailure : unsigned char {
    None, UnsupportedBuild, Input, Read, Identity, Relationship, Owner,
    RegistryBounds, RegistryWeak, RegistryMembership, MapBounds, MapChain,
    MapBinding, HistoricalMapDrift, Backing, Drift, Budget, PartialMutation,
    IncompleteSnapshot, // Not success: only initial read-only capture may defer.
};
enum class BindingPhase : unsigned char { BeforeBridge, AfterOwnBridge, CurrentRegistered };

// Produced by one-time exact discovery/name construction. No handle-to-object
// decoding is implied: independent owning acquisition and object identity are
// separate witnesses. The backend must keep each native reference alive.
struct ObjectNames {
    std::uint64_t instance = 0, class_name = 0, metaclass = 0;
    std::uint64_t script_package = 0, package_class = 0, package = 0;
};
struct ObjectWitness {
    // instance, class, metaclass, script package, package class, package,
    // outer class (UPackage); same seven nodes as the retained authentication.
    // Metadata may have item-backed serial zero, pinned unchanged in every phase;
    // only this cohort's candidate instance may gain a serial at its own bridge.
    std::array<Address, 7> objects{};
};
struct RetainedAcquisition {
    Address handle = 0, control = 0;
    std::uint64_t requested_path = 0; // constructor-produced full object-path FName
};
struct BindingSeed {
    ObjectWitness candidate{}, stock{};
    ObjectNames candidate_names{}, stock_names{};
    RetainedAcquisition owner{};
    std::uint64_t association = 0, stock_association = 0;
};
struct NameWitness {
    std::string_view text;
    std::uint64_t packed = 0;
};
// Backend-owned immutable constructor outputs (find-existing mode), constructed
// before capture. No string parsing or native calls occur in this reader.
struct ResolvedNames {
    const NameWitness* data = nullptr;
    std::size_t count = 0;
};
enum class BindingSide : unsigned char { None, Candidate, Stock };
// Bounded copied failure facts, never handles, leases or new validation authority.
struct BindingDiagnostic {
    const char* stage = "none";
    const char* predicate = "none";
    BindingSide side = BindingSide::None;
    unsigned char node = 255;
    BindingPhase phase = BindingPhase::BeforeBridge;
    std::int32_t index = -1, serial = 0; // scan index or graph node's copied item index
    bool serial_observed = false, read_failed = false, zero_attempted = false;
    unsigned identity_result = 0, zero_result = 0, scanned = 0;
    std::array<unsigned, 3> matches{};
    bool before_observed = false, after_observed = false;
    uobject_locator_core::GUObjectArrayHeader before{}, after{};
};
struct BindingResult {
    BindingFailure failure = BindingFailure::None;
    unsigned char role = 0;
    BindingDiagnostic diagnostic{};
    explicit operator bool() const noexcept { return failure == BindingFailure::None; }
};

// Read-only evidence, NOT a gate, owning reference, game admission or readiness.
// Calls require the backend's external native reader/writer exclusion. Rechecking
// copied spans detects drift but cannot substitute for that exclusion.
// The initial bounded GU uniqueness scan is an observation under that authority,
// not global GC exclusion or a lease. Native acquisitions remain caller-owned.
class CohortBindings final {
public:
    CohortBindings() = default;
    BindingResult capture(std::string_view validated_build_id, Address image_base,
        const uobject_locator_core::UObjectIdentityAccess& identity,
        std::shared_ptr<const ff7rp::pipeline::NativeAudioArtifactSet> artifacts,
        const std::array<BindingSeed, 3>& seeds, ResolvedNames names) noexcept;
    BindingResult validate(const uobject_locator_core::UObjectIdentityAccess& identity,
        BindingPhase phase) const noexcept;
    // Call only immediately after THIS backend's class bridge returned normally,
    // within the same exclusion interval. Does not authorize or invoke the bridge.
    BindingResult latch_after_own_bridge(
        const uobject_locator_core::UObjectIdentityAccess& identity,
        unsigned char role) noexcept;
    bool captured() const noexcept { return captured_; }
    BindingResult terminal_failure() const noexcept { return terminal_failure_; }

private:
    struct Identity {
        std::array<unsigned char, 40> header{};
        std::int32_t index = -1, serial = 0;
    };
    struct Record {
        BindingSeed seed{};
        std::array<Identity, 7> candidate{}, stock{};
        Address map_root = 0;
        std::array<unsigned char, 80> map_header{};
        std::array<Address, 2> backing{};
        std::int32_t assigned_serial = 0;
        bool registered = false;
    };
    BindingResult read_record(const uobject_locator_core::UObjectIdentityAccess&,
        unsigned char role, Record&, BindingPhase, bool initial) const noexcept;
    std::shared_ptr<const ff7rp::pipeline::NativeAudioArtifactSet> artifacts_;
    std::array<Record, 3> records_{};
    Address image_base_ = 0;
    bool captured_ = false;
    BindingResult terminal_failure_{};
};

const char* binding_failure_name(BindingFailure) noexcept;

} // namespace ff7r::piano::game::native_audio
