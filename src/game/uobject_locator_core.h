#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ff7r::piano::game::uobject_locator_core {

using Address = std::uintptr_t;

struct MemoryReader {
    void* context = nullptr;
    bool (*read)(void* context, Address address, void* destination, std::size_t size) noexcept = nullptr;
};

struct ImageView {
    Address base = 0;
    std::size_t size = 0;
    MemoryReader memory{};
};

struct LocatorFacts {
    const int* signature = nullptr;
    std::size_t signature_size = 0;
    Address relative_displacement = 0;
    Address instruction_size = 0;
    const Address* candidate_adjustments = nullptr;
    std::size_t candidate_adjustment_count = 0;
};

enum class LocatorPolicy {
    ListEager,
    ChartLazy,
};

struct CandidatePredicate {
    void* context = nullptr;
    bool (*accept)(void* context, Address candidate) noexcept = nullptr;
};

enum class LocateDisposition {
    SignatureMissing,
    RelativeDisplacementUnreadable,
    TargetOutOfImage,
    CandidatesReady,
    Ready,
    Pending,
};

struct LocateOutcome {
    LocateDisposition disposition = LocateDisposition::SignatureMissing;
    Address match_address = 0;
    Address target = 0;
    Address resolved = 0;
};

// Characterizes the two existing locator policies without owning executable facts.
// ListEager treats target/candidates as RVAs, bounds them to the image, suppresses
// adjustment underflow, and tries candidates during locate(). ChartLazy keeps
// wrapped absolute-address arithmetic and defers all candidate validation.
class LegacyLocator {
public:
    explicit LegacyLocator(LocatorPolicy policy) noexcept;

    LocateOutcome locate(
        const ImageView& image,
        const LocatorFacts& facts,
        CandidatePredicate predicate = {}) noexcept;
    bool select(CandidatePredicate predicate, Address& resolved) noexcept;

    LocatorPolicy policy() const noexcept { return policy_; }
    Address resolved() const noexcept { return resolved_; }
    const std::vector<Address>& candidates() const noexcept { return candidates_; }

private:
    LocatorPolicy policy_;
    Address resolved_ = 0;
    std::vector<Address> candidates_;
};

struct GUObjectArrayHeader {
    Address chunks = 0;
    std::int32_t max_elements = 0;
    std::int32_t num_elements = 0;
    std::int32_t max_chunks = 0;
    std::int32_t num_chunks = 0;
};

struct GUObjectArrayLayout {
    Address chunks = 0x10;
    Address max_elements = 0x20;
    Address num_elements = 0x24;
    Address max_chunks = 0x28;
    Address num_chunks = 0x2c;
};

enum class HeaderDomain {
    List,
    Chart,
};

bool read_guobject_array_header(
    MemoryReader memory,
    Address address,
    const GUObjectArrayLayout& layout,
    GUObjectArrayHeader& out) noexcept;
bool list_header_accepts(const GUObjectArrayHeader& header) noexcept;
bool chart_header_accepts(Address address, const GUObjectArrayHeader& header) noexcept;

struct HeaderCandidateContext {
    MemoryReader memory{};
    GUObjectArrayLayout layout{};
    HeaderDomain domain = HeaderDomain::List;
};

bool header_candidate_accepts(void* context, Address candidate) noexcept;

struct UObjectLayout {
    Address flags = 0x08;
    Address internal_index = 0x0c;
};

struct FUObjectItemLayout {
    Address object = 0x00;
    Address flags = 0x08;
    Address serial_number = 0x10;
    Address size = 0x18;
};

struct UObjectIdentityFacts {
    GUObjectArrayLayout array{};
    UObjectLayout object{};
    FUObjectItemLayout item{};
    std::int32_t elements_per_chunk = 65'536;
    std::int32_t item_unreachable_or_pending_kill = (1 << 28) | (1 << 29);
    std::uint32_t object_begin_or_finish_destroyed = 0x00008000u | 0x00010000u;
    std::int32_t root_set = 1 << 30;
};

struct AtomicInt32Access {
    void* context = nullptr;
    bool (*load)(void* context, Address address, std::int32_t& value) noexcept = nullptr;
    bool (*compare_exchange)(
        void* context,
        Address address,
        std::int32_t expected,
        std::int32_t desired,
        std::int32_t& observed) noexcept = nullptr;
};

struct UObjectIdentityAccess {
    MemoryReader memory{};
    AtomicInt32Access atomic_flags{};
    Address array_address = 0;
    UObjectIdentityFacts facts{};
};

struct UObjectLiveHandle {
    std::int32_t internal_index = -1;
    std::int32_t serial_number = 0;
};

// Controller-only identity for the observed GUObjectArray case where the item
// is live and item-backed but intentionally has no positive UObject serial.
// This is not a UObjectLiveHandle and must never be accepted by generic
// UObject lifetime callers.
struct UObjectItemBackedZeroSerialSnapshot {
    std::int32_t internal_index = -1;
    std::int32_t serial_number = 0;
};

enum class UObjectLiveHandleCaptureResult {
    ResolverOrViewUnavailable,
    UObjectFieldsUnavailable,
    IndexOutOfRange,
    HeaderInvalid,
    ChunkOutOfRange,
    ChunkPointerUnreadableOrNull,
    ItemAddressOrReadFailure,
    ObjectMismatch,
    SerialInvalid,
    ItemFlagsDead,
    UObjectDestroyed,
    Success,
};

enum class UObjectItemBackedZeroSerialCaptureResult {
    ResolverOrViewUnavailable,
    UObjectFieldsUnavailable,
    IndexOutOfRange,
    HeaderInvalid,
    ChunkOutOfRange,
    ChunkPointerUnreadableOrNull,
    ItemAddressOrReadFailure,
    ObjectMismatch,
    SerialPositive,
    SerialNegative,
    ItemFlagsDead,
    UObjectDestroyed,
    Success,
};

constexpr UObjectLiveHandleCaptureResult classify_live_handle_capture_access(
    const bool resolver_or_view_available,
    const UObjectLiveHandleCaptureResult capture_result) noexcept
{
    return resolver_or_view_available
        ? capture_result
        : UObjectLiveHandleCaptureResult::ResolverOrViewUnavailable;
}

constexpr bool live_handle_capture_succeeded(
    const UObjectLiveHandleCaptureResult result) noexcept
{
    return result == UObjectLiveHandleCaptureResult::Success;
}

constexpr UObjectItemBackedZeroSerialCaptureResult
classify_item_backed_zero_serial_capture_access(
    const bool resolver_or_view_available,
    const UObjectItemBackedZeroSerialCaptureResult capture_result) noexcept
{
    return resolver_or_view_available
        ? capture_result
        : UObjectItemBackedZeroSerialCaptureResult::ResolverOrViewUnavailable;
}

constexpr bool item_backed_zero_serial_capture_succeeded(
    const UObjectItemBackedZeroSerialCaptureResult result) noexcept
{
    return result == UObjectItemBackedZeroSerialCaptureResult::Success;
}

bool read_uobject_item_pointer(
    const UObjectIdentityAccess& access,
    std::int32_t index,
    Address& object) noexcept;
bool capture_live_handle(
    const UObjectIdentityAccess& access,
    Address object,
    UObjectLiveHandle& out) noexcept;
UObjectLiveHandleCaptureResult capture_live_handle_diagnostic(
    const UObjectIdentityAccess& access,
    Address object,
    UObjectLiveHandle& out) noexcept;
UObjectItemBackedZeroSerialCaptureResult
capture_item_backed_zero_serial_snapshot_diagnostic(
    const UObjectIdentityAccess& access,
    Address object,
    UObjectItemBackedZeroSerialSnapshot& out) noexcept;
bool validate_item_backed_zero_serial_snapshot(
    const UObjectIdentityAccess& access,
    Address object,
    const UObjectItemBackedZeroSerialSnapshot& expected) noexcept;
bool validate_live_handle(
    const UObjectIdentityAccess& access,
    Address object,
    const UObjectLiveHandle& handle) noexcept;
bool pin_live_handle(
    const UObjectIdentityAccess& access,
    Address object,
    const UObjectLiveHandle& handle) noexcept;
bool is_live_rooted(
    const UObjectIdentityAccess& access,
    Address object,
    const UObjectLiveHandle& handle) noexcept;

} // namespace ff7r::piano::game::uobject_locator_core
