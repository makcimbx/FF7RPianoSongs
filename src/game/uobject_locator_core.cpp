#include "game/uobject_locator_core.h"

#include <cstring>
#include <limits>

namespace ff7r::piano::game::uobject_locator_core {
namespace {

template <typename T>
bool read_value(const MemoryReader memory, const Address address, T& out) noexcept
{
    return memory.read && memory.read(memory.context, address, &out, sizeof(out));
}

Address add_signed_wrapped(const Address base, const std::int32_t displacement) noexcept
{
    return base + static_cast<Address>(static_cast<std::intptr_t>(displacement));
}

bool image_address(const ImageView& image, const std::size_t offset, Address& out) noexcept
{
    if (offset >= image.size || image.base > std::numeric_limits<Address>::max() - offset) {
        return false;
    }
    out = image.base + offset;
    return true;
}

bool pattern_matches(
    const ImageView& image,
    const LocatorFacts& facts,
    const std::size_t offset) noexcept
{
    for (std::size_t index = 0; index < facts.signature_size; ++index) {
        const int expected = facts.signature[index];
        if (expected < 0) {
            continue;
        }
        Address address = 0;
        std::uint8_t actual = 0;
        if (!image_address(image, offset + index, address)
            || !read_value(image.memory, address, actual)
            || static_cast<int>(actual) != expected) {
            return false;
        }
    }
    return true;
}

bool find_first_pattern(
    const ImageView& image,
    const LocatorFacts& facts,
    std::size_t& match_offset) noexcept
{
    if (!image.base || !image.memory.read || !facts.signature || facts.signature_size == 0
        || image.size < facts.signature_size) {
        return false;
    }
    const std::size_t last = image.size - facts.signature_size;
    for (std::size_t offset = 0; offset <= last; ++offset) {
        if (pattern_matches(image, facts, offset)) {
            match_offset = offset;
            return true;
        }
    }
    return false;
}

bool predicate_accepts(const CandidatePredicate predicate, const Address candidate) noexcept
{
    return candidate && predicate.accept && predicate.accept(predicate.context, candidate);
}

bool read_header_for_domain(
    const HeaderCandidateContext& context,
    const Address candidate,
    GUObjectArrayHeader& header) noexcept
{
    if (context.domain == HeaderDomain::Chart && candidate < 0x10000) {
        return false;
    }
    if (!read_guobject_array_header(context.memory, candidate, context.layout, header)) {
        return false;
    }
    return context.domain == HeaderDomain::List
        ? list_header_accepts(header)
        : chart_header_accepts(candidate, header);
}

struct ItemFields {
    Address item = 0;
    Address object = 0;
    std::int32_t flags = 0;
    std::int32_t serial = 0;
};

bool add_checked(const Address base, const Address offset, Address& out) noexcept
{
    if (base > std::numeric_limits<Address>::max() - offset) {
        return false;
    }
    out = base + offset;
    return true;
}

bool multiply_checked(const Address left, const Address right, Address& out) noexcept
{
    if (left != 0 && right > std::numeric_limits<Address>::max() / left) {
        return false;
    }
    out = left * right;
    return true;
}

UObjectLiveHandleCaptureResult read_item_diagnostic(
    const UObjectIdentityAccess& access,
    const std::int32_t index,
    ItemFields& out) noexcept
{
    out = {};
    GUObjectArrayHeader header{};
    if (index < 0) {
        return UObjectLiveHandleCaptureResult::IndexOutOfRange;
    }
    if (access.facts.elements_per_chunk <= 0) {
        return UObjectLiveHandleCaptureResult::ChunkOutOfRange;
    }
    if (!read_guobject_array_header(
            access.memory, access.array_address, access.facts.array, header)
        || !list_header_accepts(header)) {
        return UObjectLiveHandleCaptureResult::HeaderInvalid;
    }
    if (index >= header.num_elements) {
        return UObjectLiveHandleCaptureResult::IndexOutOfRange;
    }

    const std::int32_t chunk_index = index / access.facts.elements_per_chunk;
    const std::int32_t within_chunk = index % access.facts.elements_per_chunk;
    if (chunk_index < 0 || chunk_index >= header.num_chunks) {
        return UObjectLiveHandleCaptureResult::ChunkOutOfRange;
    }

    Address chunk_pointer_offset = 0;
    Address chunk_pointer_address = 0;
    Address chunk = 0;
    Address item_offset = 0;
    if (!multiply_checked(static_cast<Address>(chunk_index), sizeof(Address), chunk_pointer_offset)
        || !add_checked(header.chunks, chunk_pointer_offset, chunk_pointer_address)
        || !read_value(access.memory, chunk_pointer_address, chunk)
        || !chunk) {
        return UObjectLiveHandleCaptureResult::ChunkPointerUnreadableOrNull;
    }
    if (!multiply_checked(static_cast<Address>(within_chunk), access.facts.item.size, item_offset)
        || !add_checked(chunk, item_offset, out.item)) {
        return UObjectLiveHandleCaptureResult::ItemAddressOrReadFailure;
    }

    Address object_address = 0;
    Address flags_address = 0;
    Address serial_address = 0;
    if (!add_checked(out.item, access.facts.item.object, object_address)
        || !add_checked(out.item, access.facts.item.flags, flags_address)
        || !add_checked(out.item, access.facts.item.serial_number, serial_address)
        || !read_value(access.memory, object_address, out.object)
        || !read_value(access.memory, flags_address, out.flags)
        || !read_value(access.memory, serial_address, out.serial)) {
        return UObjectLiveHandleCaptureResult::ItemAddressOrReadFailure;
    }
    return UObjectLiveHandleCaptureResult::Success;
}

bool read_item(
    const UObjectIdentityAccess& access,
    const std::int32_t index,
    ItemFields& out) noexcept
{
    return read_item_diagnostic(access, index, out)
        == UObjectLiveHandleCaptureResult::Success;
}

bool load_atomic_flags(
    const AtomicInt32Access& access,
    const Address address,
    std::int32_t& value) noexcept
{
    return access.load && access.load(access.context, address, value);
}

bool compare_exchange_flags(
    const AtomicInt32Access& access,
    const Address address,
    const std::int32_t expected,
    const std::int32_t desired,
    std::int32_t& observed) noexcept
{
    return access.compare_exchange
        && access.compare_exchange(access.context, address, expected, desired, observed);
}

} // namespace

LegacyLocator::LegacyLocator(const LocatorPolicy policy) noexcept
    : policy_(policy)
{
}

LocateOutcome LegacyLocator::locate(
    const ImageView& image,
    const LocatorFacts& facts,
    const CandidatePredicate predicate) noexcept
{
    candidates_.clear();
    resolved_ = 0;

    LocateOutcome outcome{};
    std::size_t match_offset = 0;
    if (!find_first_pattern(image, facts, match_offset)) {
        outcome.disposition = LocateDisposition::SignatureMissing;
        return outcome;
    }
    if (!image_address(image, match_offset, outcome.match_address)) {
        outcome.disposition = LocateDisposition::SignatureMissing;
        return outcome;
    }

    Address rel_address = 0;
    std::int32_t rel = 0;
    if (!add_checked(outcome.match_address, facts.relative_displacement, rel_address)
        || (policy_ == LocatorPolicy::ChartLazy && rel_address < 0x10000)
        || !read_value(image.memory, rel_address, rel)) {
        outcome.disposition = LocateDisposition::RelativeDisplacementUnreadable;
        return outcome;
    }

    candidates_.reserve(facts.candidate_adjustment_count);
    if (policy_ == LocatorPolicy::ListEager) {
        const Address target_rva = add_signed_wrapped(
            static_cast<Address>(match_offset) + facts.instruction_size, rel);
        outcome.target = target_rva;
        if (target_rva >= image.size) {
            outcome.disposition = LocateDisposition::TargetOutOfImage;
            return outcome;
        }
        for (std::size_t index = 0; index < facts.candidate_adjustment_count; ++index) {
            const Address adjustment = facts.candidate_adjustments[index];
            const Address candidate_rva = target_rva >= adjustment
                ? target_rva - adjustment
                : target_rva;
            Address candidate = 0;
            if (candidate_rva < image.size
                && image.base <= std::numeric_limits<Address>::max() - candidate_rva) {
                candidate = image.base + candidate_rva;
            }
            candidates_.push_back(candidate);
        }
        outcome.target = image.base + target_rva;
        Address resolved = 0;
        if (select(predicate, resolved)) {
            outcome.disposition = LocateDisposition::Ready;
            outcome.resolved = resolved;
        } else {
            outcome.disposition = LocateDisposition::Pending;
        }
        return outcome;
    }

    outcome.target = add_signed_wrapped(outcome.match_address + facts.instruction_size, rel);
    for (std::size_t index = 0; index < facts.candidate_adjustment_count; ++index) {
        candidates_.push_back(outcome.target - facts.candidate_adjustments[index]);
    }
    outcome.disposition = LocateDisposition::CandidatesReady;
    return outcome;
}

bool LegacyLocator::select(const CandidatePredicate predicate, Address& resolved) noexcept
{
    resolved = 0;
    if (predicate_accepts(predicate, resolved_)) {
        resolved = resolved_;
        return true;
    }
    for (const Address candidate : candidates_) {
        if (predicate_accepts(predicate, candidate)) {
            resolved_ = candidate;
            resolved = candidate;
            return true;
        }
    }
    return false;
}

bool read_guobject_array_header(
    const MemoryReader memory,
    const Address address,
    const GUObjectArrayLayout& layout,
    GUObjectArrayHeader& out) noexcept
{
    out = {};
    if (!address) {
        return false;
    }
    GUObjectArrayHeader header{};
    Address field = 0;
    const bool readable = add_checked(address, layout.chunks, field)
        && read_value(memory, field, header.chunks)
        && add_checked(address, layout.max_elements, field)
        && read_value(memory, field, header.max_elements)
        && add_checked(address, layout.num_elements, field)
        && read_value(memory, field, header.num_elements)
        && add_checked(address, layout.max_chunks, field)
        && read_value(memory, field, header.max_chunks)
        && add_checked(address, layout.num_chunks, field)
        && read_value(memory, field, header.num_chunks);
    if (readable) {
        out = header;
    }
    return readable;
}

bool list_header_accepts(const GUObjectArrayHeader& header) noexcept
{
    return header.chunks != 0
        && header.num_elements > 0
        && header.num_elements < 2'000'000
        && header.num_chunks > 0
        && header.num_chunks < 1024;
}

bool chart_header_accepts(const Address address, const GUObjectArrayHeader& header) noexcept
{
    return address >= 0x10000
        && header.chunks >= 0x10000
        && header.num_elements > 0
        && header.num_elements <= header.max_elements
        && header.max_elements <= 10'000'000
        && header.num_chunks > 0
        && header.num_chunks <= header.max_chunks
        && header.max_chunks <= 4096;
}

bool header_candidate_accepts(void* context, const Address candidate) noexcept
{
    if (!context) {
        return false;
    }
    const auto& header_context = *static_cast<const HeaderCandidateContext*>(context);
    GUObjectArrayHeader header{};
    return read_header_for_domain(header_context, candidate, header);
}

bool read_uobject_item_pointer(
    const UObjectIdentityAccess& access,
    const std::int32_t index,
    Address& object) noexcept
{
    ItemFields item{};
    if (!read_item(access, index, item)) {
        object = 0;
        return false;
    }
    object = item.object;
    return object != 0;
}

bool capture_live_handle(
    const UObjectIdentityAccess& access,
    const Address object,
    UObjectLiveHandle& out) noexcept
{
    return live_handle_capture_succeeded(
        capture_live_handle_diagnostic(access, object, out));
}

UObjectItemBackedZeroSerialCaptureResult map_item_read_result(
    const UObjectLiveHandleCaptureResult result) noexcept
{
    switch (result) {
    case UObjectLiveHandleCaptureResult::IndexOutOfRange:
        return UObjectItemBackedZeroSerialCaptureResult::IndexOutOfRange;
    case UObjectLiveHandleCaptureResult::HeaderInvalid:
        return UObjectItemBackedZeroSerialCaptureResult::HeaderInvalid;
    case UObjectLiveHandleCaptureResult::ChunkOutOfRange:
        return UObjectItemBackedZeroSerialCaptureResult::ChunkOutOfRange;
    case UObjectLiveHandleCaptureResult::ChunkPointerUnreadableOrNull:
        return UObjectItemBackedZeroSerialCaptureResult::ChunkPointerUnreadableOrNull;
    case UObjectLiveHandleCaptureResult::ItemAddressOrReadFailure:
        return UObjectItemBackedZeroSerialCaptureResult::ItemAddressOrReadFailure;
    case UObjectLiveHandleCaptureResult::Success:
        return UObjectItemBackedZeroSerialCaptureResult::Success;
    case UObjectLiveHandleCaptureResult::ResolverOrViewUnavailable:
    case UObjectLiveHandleCaptureResult::UObjectFieldsUnavailable:
    case UObjectLiveHandleCaptureResult::ObjectMismatch:
    case UObjectLiveHandleCaptureResult::SerialInvalid:
    case UObjectLiveHandleCaptureResult::ItemFlagsDead:
    case UObjectLiveHandleCaptureResult::UObjectDestroyed:
        return UObjectItemBackedZeroSerialCaptureResult::ItemAddressOrReadFailure;
    }
    return UObjectItemBackedZeroSerialCaptureResult::ItemAddressOrReadFailure;
}

UObjectLiveHandleCaptureResult capture_live_handle_diagnostic(
    const UObjectIdentityAccess& access,
    const Address object,
    UObjectLiveHandle& out) noexcept
{
    std::int32_t internal_index = -1;
    std::uint32_t object_flags = 0;
    Address field = 0;
    ItemFields item{};
    if (!object
        || !add_checked(object, access.facts.object.internal_index, field)
        || !read_value(access.memory, field, internal_index)
        || !add_checked(object, access.facts.object.flags, field)
        || !read_value(access.memory, field, object_flags)) {
        return UObjectLiveHandleCaptureResult::UObjectFieldsUnavailable;
    }
    const UObjectLiveHandleCaptureResult item_result =
        read_item_diagnostic(access, internal_index, item);
    if (item_result != UObjectLiveHandleCaptureResult::Success) {
        return item_result;
    }
    if (item.object != object) {
        return UObjectLiveHandleCaptureResult::ObjectMismatch;
    }
    if (item.serial <= 0) {
        return UObjectLiveHandleCaptureResult::SerialInvalid;
    }
    if ((item.flags & access.facts.item_unreachable_or_pending_kill) != 0) {
        return UObjectLiveHandleCaptureResult::ItemFlagsDead;
    }
    if ((object_flags & access.facts.object_begin_or_finish_destroyed) != 0) {
        return UObjectLiveHandleCaptureResult::UObjectDestroyed;
    }
    out.internal_index = internal_index;
    out.serial_number = item.serial;
    return UObjectLiveHandleCaptureResult::Success;
}

UObjectItemBackedZeroSerialCaptureResult
capture_item_backed_zero_serial_snapshot_diagnostic(
    const UObjectIdentityAccess& access,
    const Address object,
    UObjectItemBackedZeroSerialSnapshot& out) noexcept
{
    std::int32_t internal_index = -1;
    std::uint32_t object_flags = 0;
    Address field = 0;
    ItemFields item{};
    if (!object
        || !add_checked(object, access.facts.object.internal_index, field)
        || !read_value(access.memory, field, internal_index)
        || !add_checked(object, access.facts.object.flags, field)
        || !read_value(access.memory, field, object_flags)) {
        return UObjectItemBackedZeroSerialCaptureResult::UObjectFieldsUnavailable;
    }
    const UObjectLiveHandleCaptureResult item_result =
        read_item_diagnostic(access, internal_index, item);
    if (item_result != UObjectLiveHandleCaptureResult::Success) {
        return map_item_read_result(item_result);
    }
    if (item.object != object) {
        return UObjectItemBackedZeroSerialCaptureResult::ObjectMismatch;
    }
    if (item.serial > 0) {
        return UObjectItemBackedZeroSerialCaptureResult::SerialPositive;
    }
    if (item.serial < 0) {
        return UObjectItemBackedZeroSerialCaptureResult::SerialNegative;
    }
    if ((item.flags & access.facts.item_unreachable_or_pending_kill) != 0) {
        return UObjectItemBackedZeroSerialCaptureResult::ItemFlagsDead;
    }
    if ((object_flags & access.facts.object_begin_or_finish_destroyed) != 0) {
        return UObjectItemBackedZeroSerialCaptureResult::UObjectDestroyed;
    }
    out.internal_index = internal_index;
    out.serial_number = item.serial;
    return UObjectItemBackedZeroSerialCaptureResult::Success;
}

bool validate_item_backed_zero_serial_snapshot(
    const UObjectIdentityAccess& access,
    const Address object,
    const UObjectItemBackedZeroSerialSnapshot& expected) noexcept
{
    UObjectItemBackedZeroSerialSnapshot current{};
    return expected.internal_index >= 0 && expected.serial_number == 0
        && item_backed_zero_serial_capture_succeeded(
            capture_item_backed_zero_serial_snapshot_diagnostic(
                access, object, current))
        && current.internal_index == expected.internal_index
        && current.serial_number == expected.serial_number;
}

bool validate_live_handle(
    const UObjectIdentityAccess& access,
    const Address object,
    const UObjectLiveHandle& handle) noexcept
{
    UObjectLiveHandle current{};
    return capture_live_handle(access, object, current)
        && current.internal_index == handle.internal_index
        && current.serial_number == handle.serial_number;
}

bool pin_live_handle(
    const UObjectIdentityAccess& access,
    const Address object,
    const UObjectLiveHandle& handle) noexcept
{
    if (!validate_live_handle(access, object, handle)) {
        return false;
    }
    ItemFields item{};
    if (!read_item(access, handle.internal_index, item)
        || item.object != object
        || item.serial != handle.serial_number) {
        return false;
    }
    Address flags_address = 0;
    std::int32_t current = 0;
    if (!add_checked(item.item, access.facts.item.flags, flags_address)
        || !load_atomic_flags(access.atomic_flags, flags_address, current)) {
        return false;
    }

    const std::int32_t dead_flags = access.facts.item_unreachable_or_pending_kill;
    const std::int32_t root_set = access.facts.root_set;
    bool root_set_by_us = false;
    while ((current & root_set) == 0) {
        if ((current & dead_flags) != 0) {
            return false;
        }
        std::int32_t observed = 0;
        if (!compare_exchange_flags(
                access.atomic_flags, flags_address, current, current | root_set, observed)) {
            return false;
        }
        if (observed == current) {
            root_set_by_us = true;
            break;
        }
        current = observed;
    }

    std::int32_t final_flags = 0;
    const bool pinned = validate_live_handle(access, object, handle)
        && load_atomic_flags(access.atomic_flags, flags_address, final_flags)
        && (final_flags & root_set) != 0
        && (final_flags & dead_flags) == 0;
    if (!pinned && root_set_by_us
        && load_atomic_flags(access.atomic_flags, flags_address, current)) {
        while ((current & root_set) != 0) {
            std::int32_t observed = 0;
            if (!compare_exchange_flags(
                    access.atomic_flags, flags_address, current, current & ~root_set, observed)) {
                break;
            }
            if (observed == current) {
                break;
            }
            current = observed;
        }
    }
    return pinned;
}

bool is_live_rooted(
    const UObjectIdentityAccess& access,
    const Address object,
    const UObjectLiveHandle& handle) noexcept
{
    ItemFields item{};
    return validate_live_handle(access, object, handle)
        && read_item(access, handle.internal_index, item)
        && item.object == object
        && item.serial == handle.serial_number
        && (item.flags & access.facts.root_set) != 0;
}

} // namespace ff7r::piano::game::uobject_locator_core
