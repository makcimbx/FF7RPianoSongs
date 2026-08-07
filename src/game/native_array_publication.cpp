#include "game/native_array_publication.h"

#include <array>
#include <cstddef>

namespace ff7r::piano::game {
namespace {

enum class TupleField {
    Pointer,
    Count,
    Capacity,
};

bool tuple_is_safe(
    const NativeArrayTuple& value,
    const NativeArrayTuple& original,
    int32_t original_backing_capacity,
    const NativeArrayTuple& redirected,
    int32_t redirected_backing_capacity)
{
    int32_t backing_capacity = -1;
    if (value.pointer == original.pointer) {
        backing_capacity = original_backing_capacity;
    } else if (value.pointer == redirected.pointer) {
        backing_capacity = redirected_backing_capacity;
    } else {
        return false;
    }
    return value.pointer != 0 && value.count >= 0 && value.capacity >= value.count
        && value.capacity <= backing_capacity;
}

bool write_field(
    TupleField field,
    const NativeArrayTuple& value,
    const NativeArrayTupleAccess& access)
{
    switch (field) {
    case TupleField::Pointer:
        return access.write_pointer && access.write_pointer(value.pointer);
    case TupleField::Count:
        return access.write_count && access.write_count(value.count);
    case TupleField::Capacity:
        return access.write_capacity && access.write_capacity(value.capacity);
    }
    return false;
}

void assign_field(TupleField field, NativeArrayTuple& destination, const NativeArrayTuple& source)
{
    switch (field) {
    case TupleField::Pointer:
        destination.pointer = source.pointer;
        break;
    case TupleField::Count:
        destination.count = source.count;
        break;
    case TupleField::Capacity:
        destination.capacity = source.capacity;
        break;
    }
}

NativeArrayTupleTransitionResult transition_native_array_tuple(
    const NativeArrayTuple& source,
    int32_t source_backing_capacity,
    const NativeArrayTuple& target,
    int32_t target_backing_capacity,
    const std::array<TupleField, 3>& order,
    bool publishing,
    const NativeArrayTupleAccess& access,
    const NativeArrayTupleFaultInjection& fault)
{
    NativeArrayTupleTransitionResult result;
    if (!access.read) return result;

    NativeArrayTuple current{};
    if (!access.read(current)) return result;
    if (current == target
        && tuple_is_safe(current, source, source_backing_capacity, target, target_backing_capacity)) {
        result.committed = true;
        result.pre_state_validated = true;
        result.rollback_verified = true;
        result.retain_redirected_storage = publishing;
        return result;
    }
    if (!(current == source)
        || !tuple_is_safe(current, source, source_backing_capacity, target, target_backing_capacity)) {
        return result;
    }
    result.pre_state_validated = true;

    int written = 0;
    bool forward_ok = true;
    for (TupleField field : order) {
        if (!write_field(field, target, access)) {
            forward_ok = false;
            break;
        }
        ++written;
        assign_field(field, current, target);
        NativeArrayTuple verified{};
        if (!access.read(verified) || !(verified == current)
            || !tuple_is_safe(verified, source, source_backing_capacity, target, target_backing_capacity)) {
            forward_ok = false;
            break;
        }
        if (access.observe_verified_state) access.observe_verified_state(verified);
        if (fault.fail_after_forward_write == written) {
            forward_ok = false;
            break;
        }
    }

    if (forward_ok) {
        // Every field write was immediately read back, including the final target tuple.
        result.committed = true;
        result.rollback_verified = true;
        result.retain_redirected_storage = publishing;
        return result;
    }

    result.rollback_attempted = written > 0;
    int rollback_writes = 0;
    for (int index = written - 1; index >= 0; --index) {
        const TupleField field = order[static_cast<size_t>(index)];
        if (!write_field(field, source, access)) return result;
        ++rollback_writes;
        assign_field(field, current, source);
        NativeArrayTuple verified{};
        if (!access.read(verified) || !(verified == current)
            || !tuple_is_safe(verified, source, source_backing_capacity, target, target_backing_capacity)) {
            return result;
        }
        if (access.observe_verified_state) access.observe_verified_state(verified);
        if (fault.fail_after_rollback_write == rollback_writes) return result;
    }

    NativeArrayTuple verified{};
    result.rollback_verified = access.read(verified) && verified == source
        && tuple_is_safe(verified, source, source_backing_capacity, target, target_backing_capacity);
    result.retain_redirected_storage = publishing ? !result.rollback_verified : true;
    return result;
}

} // namespace

NativeArrayTupleTransitionResult publish_native_array_tuple(
    const NativeArrayTuple& original,
    int32_t original_backing_capacity,
    const NativeArrayTuple& redirected,
    int32_t redirected_backing_capacity,
    const NativeArrayTupleAccess& access,
    const NativeArrayTupleFaultInjection& fault)
{
    // The redirected allocation is published first. It must back the still-visible original
    // capacity; only then may capacity grow, followed by count. Every visible tuple therefore
    // has count <= capacity <= the allocation backing its currently visible pointer.
    return transition_native_array_tuple(original, original_backing_capacity,
        redirected, redirected_backing_capacity,
        {TupleField::Pointer, TupleField::Capacity, TupleField::Count}, true, access, fault);
}

NativeArrayTupleTransitionResult restore_native_array_tuple(
    const NativeArrayTuple& original,
    int32_t original_backing_capacity,
    const NativeArrayTuple& redirected,
    int32_t redirected_backing_capacity,
    const NativeArrayTupleAccess& access,
    const NativeArrayTupleFaultInjection& fault)
{
    // Shrink count and capacity while the larger redirected allocation is still visible. The
    // smaller original pointer is restored last, so no reader can pair it with redirected bounds.
    return transition_native_array_tuple(redirected, redirected_backing_capacity,
        original, original_backing_capacity,
        {TupleField::Count, TupleField::Capacity, TupleField::Pointer}, false, access, fault);
}

} // namespace ff7r::piano::game
