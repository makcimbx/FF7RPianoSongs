#include "game/uobject_identity.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "game/uobject_identity_lifecycle.h"
#include "game/runtime_locator_specs.h"
#include "game/uobject_locator_core.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <sstream>
#include <vector>

namespace ff7r::piano::game {
namespace {

namespace locator = uobject_locator_core;
namespace lifecycle = uobject_identity_lifecycle;
using GUObjectArrayView = runtime_layouts::GUObjectArrayView;

std::atomic_uintptr_t g_guobjectarray_address{0};
std::unique_ptr<std::atomic_uintptr_t[]> g_guobjectarray_candidates;
std::size_t g_guobjectarray_candidate_count = 0;
std::atomic_uintptr_t g_chart_guobjectarray_address{0};
std::unique_ptr<std::atomic_uintptr_t[]> g_chart_guobjectarray_candidates;
std::size_t g_chart_guobjectarray_candidate_count = 0;
std::atomic_bool g_list_scan_ready{false};
std::atomic_bool g_chart_consumer_active{false};
std::atomic_bool g_clear_requested{false};
std::atomic_bool g_initialized{false};

bool read_memory(
    void*,
    const locator::Address address,
    void* destination,
    const std::size_t size) noexcept
{
    return address && destination
        && core::safe_copy_bytes(reinterpret_cast<const void*>(address), destination, size);
}

locator::MemoryReader process_memory() noexcept
{
    return {nullptr, &read_memory};
}

bool read_guobject_array_view(uint8_t* address, GUObjectArrayView& out)
{
    return address
        && core::safe_read_field(address, runtime_layouts::GUObjectArray::chunks, out.chunks)
        && core::safe_read_field(address, runtime_layouts::GUObjectArray::max_elements, out.max_elements)
        && core::safe_read_field(address, runtime_layouts::GUObjectArray::num_elements, out.num_elements)
        && core::safe_read_field(address, runtime_layouts::GUObjectArray::max_chunks, out.max_chunks)
        && core::safe_read_field(address, runtime_layouts::GUObjectArray::num_chunks, out.num_chunks)
        && out.chunks
        && out.num_elements > 0
        && out.num_elements < 2'000'000
        && out.num_chunks > 0
        && out.num_chunks < 1024;
}

bool read_chart_guobject_array_view(const uintptr_t address, GUObjectArrayView& out)
{
    out = {};
    uintptr_t chunks = 0;
    if (address < 0x10000
        || !core::safe_read_field(
            reinterpret_cast<void*>(address + runtime_layouts::GUObjectArray::chunks), 0, chunks)
        || !core::safe_read_field(
            reinterpret_cast<void*>(address + runtime_layouts::GUObjectArray::max_elements),
            0,
            out.max_elements)
        || !core::safe_read_field(
            reinterpret_cast<void*>(address + runtime_layouts::GUObjectArray::num_elements),
            0,
            out.num_elements)
        || !core::safe_read_field(
            reinterpret_cast<void*>(address + runtime_layouts::GUObjectArray::max_chunks),
            0,
            out.max_chunks)
        || !core::safe_read_field(
            reinterpret_cast<void*>(address + runtime_layouts::GUObjectArray::num_chunks),
            0,
            out.num_chunks)) {
        return false;
    }
    if (chunks < 0x10000
        || out.num_elements <= 0
        || out.num_elements > out.max_elements
        || out.max_elements > 10'000'000
        || out.num_chunks <= 0
        || out.num_chunks > out.max_chunks
        || out.max_chunks > 4096) {
        out = {};
        return false;
    }
    out.chunks = reinterpret_cast<uint8_t**>(chunks);
    return true;
}

bool read_candidate_guobjectarray_view(GUObjectArrayView& out, uintptr_t* selected_address = nullptr)
{
    const uintptr_t resolved = g_guobjectarray_address.load(std::memory_order_acquire);
    if (read_guobject_array_view(reinterpret_cast<uint8_t*>(resolved), out)) {
        if (selected_address) {
            *selected_address = resolved;
        }
        return true;
    }

    for (std::size_t index = 0; index < g_guobjectarray_candidate_count; ++index) {
        const uintptr_t candidate = g_guobjectarray_candidates[index].load(std::memory_order_acquire);
        if (read_guobject_array_view(reinterpret_cast<uint8_t*>(candidate), out)) {
            g_guobjectarray_address.store(candidate, std::memory_order_release);
            if (selected_address) {
                *selected_address = candidate;
            }
            return true;
        }
    }
    return false;
}

bool read_chart_candidate_guobjectarray_view(GUObjectArrayView& out)
{
    const uintptr_t resolved = g_chart_guobjectarray_address.load(std::memory_order_relaxed);
    if (resolved && read_chart_guobject_array_view(resolved, out)) {
        return true;
    }

    for (std::size_t index = 0; index < g_chart_guobjectarray_candidate_count; ++index) {
        const uintptr_t candidate =
            g_chart_guobjectarray_candidates[index].load(std::memory_order_relaxed);
        if (candidate && read_chart_guobject_array_view(candidate, out)) {
            g_chart_guobjectarray_address.store(candidate, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

void clear_list_locator_state()
{
    g_list_scan_ready.store(false, std::memory_order_release);
    g_guobjectarray_address.store(0, std::memory_order_release);
    for (std::size_t index = 0; index < g_guobjectarray_candidate_count; ++index) {
        g_guobjectarray_candidates[index].store(0, std::memory_order_release);
    }
    g_guobjectarray_candidate_count = 0;
    g_guobjectarray_candidates.reset();
}

void clear_chart_locator_state()
{
    g_chart_guobjectarray_address.store(0, std::memory_order_relaxed);
    for (std::size_t index = 0; index < g_chart_guobjectarray_candidate_count; ++index) {
        g_chart_guobjectarray_candidates[index].store(0, std::memory_order_relaxed);
    }
    g_chart_guobjectarray_candidate_count = 0;
    g_chart_guobjectarray_candidates.reset();
}

void complete_uobject_identity_clear(const lifecycle::Transition& transition)
{
    if (!transition.effects.clear_chart_locator) {
        return;
    }
    clear_chart_locator_state();
    g_clear_requested.store(transition.after.clear_requested, std::memory_order_release);
    g_initialized.store(transition.after.initialized, std::memory_order_release);
}

struct EagerCandidateContext {
    GUObjectArrayView accepted_view{};
};

bool accept_list_candidate(void* context, const locator::Address candidate) noexcept
{
    if (!context) {
        return false;
    }
    auto& candidate_context = *static_cast<EagerCandidateContext*>(context);
    return read_guobject_array_view(
        reinterpret_cast<uint8_t*>(candidate), candidate_context.accepted_view);
}

bool build_locator_facts(
    const RuntimeLocatorSpec& spec,
    std::vector<int>& signature,
    locator::LocatorFacts& facts)
{
    if (spec.match_policy != RuntimeLocatorMatchPolicy::First
        || spec.decode.kind != RuntimeLocatorDecodeKind::Rel32
        || !spec.pattern.bytes
        || spec.pattern.size == 0
        || spec.pattern.mask.size() != spec.pattern.size
        || !spec.candidate_adjustments.values
        || spec.candidate_adjustments.size == 0) {
        return false;
    }

    signature.reserve(spec.pattern.size);
    for (std::size_t index = 0; index < spec.pattern.size; ++index) {
        const char mask = spec.pattern.mask[index];
        if (mask == 'x') {
            signature.push_back(spec.pattern.bytes[index]);
        } else if (mask == '?') {
            signature.push_back(-1);
        } else {
            return false;
        }
    }

    facts.signature = signature.data();
    facts.signature_size = signature.size();
    facts.relative_displacement = spec.decode.displacement_offset;
    facts.instruction_size = spec.decode.instruction_size;
    facts.candidate_adjustments = spec.candidate_adjustments.values;
    facts.candidate_adjustment_count = spec.candidate_adjustments.size;
    return true;
}

locator::UObjectIdentityFacts identity_facts() noexcept
{
    locator::UObjectIdentityFacts facts{};
    facts.array = {
        runtime_layouts::GUObjectArray::chunks,
        runtime_layouts::GUObjectArray::max_elements,
        runtime_layouts::GUObjectArray::num_elements,
        runtime_layouts::GUObjectArray::max_chunks,
        runtime_layouts::GUObjectArray::num_chunks,
    };
    facts.object = {
        runtime_layouts::UObject::flags,
        runtime_layouts::UObject::internal_index,
    };
    facts.item = {
        runtime_layouts::FUObjectItem::object,
        runtime_layouts::FUObjectItem::flags,
        runtime_layouts::FUObjectItem::serial_number,
        runtime_layouts::FUObjectItem::observed_size,
    };
    facts.elements_per_chunk = runtime_layouts::GUObjectArray::elements_per_chunk;
    return facts;
}

bool load_atomic_flags(
    void*,
    const locator::Address address,
    int32_t& value) noexcept
{
    LONG current = 0;
    if (!core::safe_read_field(reinterpret_cast<void*>(address), 0, current)) {
        return false;
    }
    value = current;
    return true;
}

bool compare_exchange_atomic_flags(
    void*,
    const locator::Address address,
    const int32_t expected,
    const int32_t desired,
    int32_t& observed) noexcept
{
    if (!address) {
        return false;
    }
    __try {
        observed = InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(address), desired, expected);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool identity_access(locator::UObjectIdentityAccess& access)
{
    GUObjectArrayView view{};
    uintptr_t address = 0;
    if (!read_candidate_guobjectarray_view(view, &address)) {
        return false;
    }
    access.memory = process_memory();
    access.atomic_flags = {nullptr, &load_atomic_flags, &compare_exchange_atomic_flags};
    access.array_address = address;
    access.facts = identity_facts();
    return true;
}

locator::UObjectLiveHandle core_handle(const UObjectLiveHandle& handle) noexcept
{
    return {handle.internal_index, handle.serial_number};
}

} // namespace

void initialize_uobject_identity(HMODULE exe_module)
{
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
    }
    const auto transition = lifecycle::transition({}, lifecycle::Event::Initialize);
    g_chart_consumer_active.store(
        transition.after.chart_consumer_active, std::memory_order_release);
    g_clear_requested.store(transition.after.clear_requested, std::memory_order_release);

    const RuntimeLocatorSpec* spec = find_runtime_locator_spec("guobject_array");
    std::vector<int> signature;
    locator::LocatorFacts facts{};
    if (!exe_module || !spec || !build_locator_facts(*spec, signature, facts)) {
        core::log(core::LogLevel::Error, "[list_patch] status=guobjectarray_signature_not_found");
        if (exe_module) {
            core::log(core::LogLevel::Error, "[chart_patch] guobjectarray status=signature_missing");
        }
        return;
    }

    const auto exe = core::image_range(exe_module);
    const locator::ImageView image{
        reinterpret_cast<locator::Address>(exe.base),
        exe.size,
        process_memory(),
    };
    const auto initialize_list_locator = [&] {
        EagerCandidateContext candidate_context{};
        locator::LegacyLocator list_locator(locator::LocatorPolicy::ListEager);
        const locator::LocateOutcome outcome = list_locator.locate(
            image, facts, {&candidate_context, &accept_list_candidate});

        const auto& candidates = list_locator.candidates();
        auto candidate_state = std::make_unique<std::atomic_uintptr_t[]>(candidates.size());
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            candidate_state[index].store(candidates[index], std::memory_order_relaxed);
        }
        g_guobjectarray_candidates = std::move(candidate_state);
        g_guobjectarray_candidate_count = candidates.size();

        if (outcome.disposition == locator::LocateDisposition::SignatureMissing) {
            core::log(core::LogLevel::Error, "[list_patch] status=guobjectarray_signature_not_found");
            return;
        }
        if (outcome.disposition == locator::LocateDisposition::RelativeDisplacementUnreadable) {
            core::log(core::LogLevel::Error, "[list_patch] status=guobjectarray_rel_read_failed");
            return;
        }
        if (outcome.disposition == locator::LocateDisposition::TargetOutOfImage) {
            core::log(core::LogLevel::Error, "[list_patch] status=guobjectarray_target_out_of_range");
            return;
        }

        const uintptr_t target_rva = outcome.target - image.base;
        if (outcome.disposition == locator::LocateDisposition::Pending) {
            std::ostringstream out;
            out << "[list_patch] status=guobjectarray_layout_pending target_rva=0x"
                << std::hex << target_rva;
            core::log(core::LogLevel::Info, out.str());
            g_list_scan_ready.store(true, std::memory_order_release);
            return;
        }

        if (outcome.disposition != locator::LocateDisposition::Ready || !outcome.resolved) {
            core::log(core::LogLevel::Error, "[list_patch] status=guobjectarray_signature_not_found");
            return;
        }

        g_guobjectarray_address.store(outcome.resolved, std::memory_order_release);
        const uintptr_t resolved_rva = outcome.resolved - image.base;
        std::ostringstream out;
        out << "[list_patch] status=guobjectarray_ready rva=0x" << std::hex << resolved_rva
            << " target_rva=0x" << target_rva
            << std::dec << " num_elements=" << candidate_context.accepted_view.num_elements
            << " num_chunks=" << candidate_context.accepted_view.num_chunks;
        core::log(core::LogLevel::Info, out.str());
        g_list_scan_ready.store(true, std::memory_order_release);
    };
    initialize_list_locator();

    locator::LegacyLocator chart_locator(locator::LocatorPolicy::ChartLazy);
    const locator::LocateOutcome chart_outcome = chart_locator.locate(image, facts);
    const auto& chart_candidates = chart_locator.candidates();
    auto chart_candidate_state =
        std::make_unique<std::atomic_uintptr_t[]>(chart_candidates.size());
    for (std::size_t index = 0; index < chart_candidates.size(); ++index) {
        chart_candidate_state[index].store(chart_candidates[index], std::memory_order_relaxed);
    }
    g_chart_guobjectarray_candidates = std::move(chart_candidate_state);
    g_chart_guobjectarray_candidate_count = chart_candidates.size();
    if (image.base && image.size != 0
        && chart_outcome.disposition == locator::LocateDisposition::SignatureMissing) {
        core::log(core::LogLevel::Error, "[chart_patch] guobjectarray status=signature_missing");
    } else if (chart_outcome.disposition
        == locator::LocateDisposition::RelativeDisplacementUnreadable) {
        core::log(core::LogLevel::Error, "[chart_patch] guobjectarray status=rel_read_failed");
    }
}

void clear_uobject_identity()
{
    clear_list_locator_state();
    g_clear_requested.store(true, std::memory_order_release);
    const auto transition = lifecycle::transition(
        {true, g_chart_consumer_active.load(std::memory_order_acquire), true},
        lifecycle::Event::Clear);
    complete_uobject_identity_clear(transition);
}

void activate_uobject_identity_chart_consumer()
{
    const auto transition = lifecycle::transition(
        {true, false, false}, lifecycle::Event::ActivateChartConsumer);
    g_chart_consumer_active.store(
        transition.after.chart_consumer_active, std::memory_order_release);
}

void release_uobject_identity_chart_consumer()
{
    g_chart_consumer_active.store(false, std::memory_order_release);
    const auto transition = lifecycle::transition(
        {true, false, g_clear_requested.load(std::memory_order_acquire)},
        lifecycle::Event::ReleaseChartConsumer);
    complete_uobject_identity_clear(transition);
}

bool uobject_identity_list_scan_ready()
{
    return g_list_scan_ready.load(std::memory_order_acquire);
}

bool read_list_guobject_array(GUObjectArrayView& out)
{
    return read_candidate_guobjectarray_view(out);
}

bool read_list_uobject_item_object(
    const GUObjectArrayView& view,
    const int32_t index,
    void*& object)
{
    if (index < 0 || index >= view.num_elements) {
        return false;
    }
    const int32_t chunk_index = index / runtime_layouts::GUObjectArray::elements_per_chunk;
    const int32_t within_chunk = index % runtime_layouts::GUObjectArray::elements_per_chunk;
    if (chunk_index < 0 || chunk_index >= view.num_chunks) {
        return false;
    }

    uint8_t* chunk = nullptr;
    return core::safe_read_field(
               view.chunks, static_cast<uintptr_t>(chunk_index) * sizeof(uint8_t*), chunk)
        && chunk
        && core::safe_read_field(
            chunk,
            static_cast<uintptr_t>(within_chunk) * runtime_layouts::FUObjectItem::observed_size
                + runtime_layouts::FUObjectItem::object,
            object)
        && object;
}

bool read_chart_guobject_array(GUObjectArrayView& out)
{
    return read_chart_candidate_guobjectarray_view(out);
}

bool read_chart_uobject_item_object(
    const GUObjectArrayView& view,
    const int32_t index,
    void*& object)
{
    object = nullptr;
    if (!view.chunks || index < 0 || index >= view.num_elements) {
        return false;
    }
    const int32_t chunk_index = index / runtime_layouts::GUObjectArray::elements_per_chunk;
    const int32_t within_chunk = index % runtime_layouts::GUObjectArray::elements_per_chunk;
    if (chunk_index < 0 || chunk_index >= view.num_chunks) {
        return false;
    }
    uint8_t* chunk = nullptr;
    uintptr_t object_value = 0;
    if (!core::safe_read_field(view.chunks + chunk_index, 0, chunk)
        || !chunk
        || !core::safe_read_field(
            chunk + static_cast<uintptr_t>(within_chunk)
                    * runtime_layouts::FUObjectItem::observed_size,
            runtime_layouts::FUObjectItem::object,
            object_value)) {
        return false;
    }
    object = reinterpret_cast<void*>(object_value);
    return object != nullptr;
}

bool capture_live_uobject_handle(void* object, UObjectLiveHandle& out)
{
    UObjectLiveHandleCaptureResult result{};
    return capture_live_uobject_handle(object, out, result);
}

bool capture_live_uobject_handle(
    void* object,
    UObjectLiveHandle& out,
    UObjectLiveHandleCaptureResult& result)
{
    locator::UObjectIdentityAccess access{};
    locator::UObjectLiveHandle captured{};
    if (!identity_access(access)) {
        result = locator::classify_live_handle_capture_access(
            false, UObjectLiveHandleCaptureResult::Success);
        return false;
    }
    result = locator::classify_live_handle_capture_access(
        true,
        locator::capture_live_handle_diagnostic(
            access, reinterpret_cast<locator::Address>(object), captured));
    if (!locator::live_handle_capture_succeeded(result)) {
        return false;
    }
    out.internal_index = captured.internal_index;
    out.serial_number = captured.serial_number;
    return true;
}

const char* uobject_live_handle_capture_result_name(
    const UObjectLiveHandleCaptureResult result) noexcept
{
    switch (result) {
    case UObjectLiveHandleCaptureResult::ResolverOrViewUnavailable:
        return "resolver_or_view_unavailable";
    case UObjectLiveHandleCaptureResult::UObjectFieldsUnavailable:
        return "uobject_fields_unavailable";
    case UObjectLiveHandleCaptureResult::IndexOutOfRange:
        return "index_out_of_range";
    case UObjectLiveHandleCaptureResult::HeaderInvalid:
        return "header_invalid";
    case UObjectLiveHandleCaptureResult::ChunkOutOfRange:
        return "chunk_out_of_range";
    case UObjectLiveHandleCaptureResult::ChunkPointerUnreadableOrNull:
        return "chunk_pointer_unreadable_or_null";
    case UObjectLiveHandleCaptureResult::ItemAddressOrReadFailure:
        return "item_address_or_read_failure";
    case UObjectLiveHandleCaptureResult::ObjectMismatch:
        return "object_mismatch";
    case UObjectLiveHandleCaptureResult::SerialInvalid:
        return "serial_invalid";
    case UObjectLiveHandleCaptureResult::ItemFlagsDead:
        return "item_flags_dead";
    case UObjectLiveHandleCaptureResult::UObjectDestroyed:
        return "uobject_destroyed";
    case UObjectLiveHandleCaptureResult::Success:
        return "success";
    }
    return "unknown";
}

bool capture_item_backed_zero_serial_uobject_snapshot(
    void* object,
    UObjectItemBackedZeroSerialSnapshot& out,
    UObjectItemBackedZeroSerialCaptureResult& result)
{
    locator::UObjectIdentityAccess access{};
    locator::UObjectItemBackedZeroSerialSnapshot captured{};
    if (!identity_access(access)) {
        result = locator::classify_item_backed_zero_serial_capture_access(
            false, UObjectItemBackedZeroSerialCaptureResult::Success);
        return false;
    }
    result = locator::classify_item_backed_zero_serial_capture_access(
        true,
        locator::capture_item_backed_zero_serial_snapshot_diagnostic(
            access, reinterpret_cast<locator::Address>(object), captured));
    if (!locator::item_backed_zero_serial_capture_succeeded(result)) {
        return false;
    }
    out = captured;
    return true;
}

const char* uobject_item_backed_zero_serial_capture_result_name(
    const UObjectItemBackedZeroSerialCaptureResult result) noexcept
{
    switch (result) {
    case UObjectItemBackedZeroSerialCaptureResult::ResolverOrViewUnavailable:
        return "resolver_or_view_unavailable";
    case UObjectItemBackedZeroSerialCaptureResult::UObjectFieldsUnavailable:
        return "uobject_fields_unavailable";
    case UObjectItemBackedZeroSerialCaptureResult::IndexOutOfRange:
        return "index_out_of_range";
    case UObjectItemBackedZeroSerialCaptureResult::HeaderInvalid:
        return "header_invalid";
    case UObjectItemBackedZeroSerialCaptureResult::ChunkOutOfRange:
        return "chunk_out_of_range";
    case UObjectItemBackedZeroSerialCaptureResult::ChunkPointerUnreadableOrNull:
        return "chunk_pointer_unreadable_or_null";
    case UObjectItemBackedZeroSerialCaptureResult::ItemAddressOrReadFailure:
        return "item_address_or_read_failure";
    case UObjectItemBackedZeroSerialCaptureResult::ObjectMismatch:
        return "object_mismatch";
    case UObjectItemBackedZeroSerialCaptureResult::SerialPositive:
        return "serial_positive";
    case UObjectItemBackedZeroSerialCaptureResult::SerialNegative:
        return "serial_negative";
    case UObjectItemBackedZeroSerialCaptureResult::ItemFlagsDead:
        return "item_flags_dead";
    case UObjectItemBackedZeroSerialCaptureResult::UObjectDestroyed:
        return "uobject_destroyed";
    case UObjectItemBackedZeroSerialCaptureResult::Success:
        return "success_zero_serial";
    }
    return "unknown";
}

bool validate_live_uobject_handle(void* object, const UObjectLiveHandle& handle)
{
    locator::UObjectIdentityAccess access{};
    return identity_access(access)
        && locator::validate_live_handle(
            access, reinterpret_cast<locator::Address>(object), core_handle(handle));
}

bool pin_live_uobject_handle(void* object, const UObjectLiveHandle& handle)
{
    locator::UObjectIdentityAccess access{};
    return identity_access(access)
        && locator::pin_live_handle(
            access, reinterpret_cast<locator::Address>(object), core_handle(handle));
}

bool is_live_uobject_rooted(void* object, const UObjectLiveHandle& handle)
{
    locator::UObjectIdentityAccess access{};
    return identity_access(access)
        && locator::is_live_rooted(
            access, reinterpret_cast<locator::Address>(object), core_handle(handle));
}

} // namespace ff7r::piano::game
