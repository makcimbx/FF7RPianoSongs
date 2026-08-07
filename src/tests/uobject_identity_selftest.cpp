#include "game/uobject_locator_core.h"
#include "game/uobject_identity_lifecycle.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace locator = ff7r::piano::game::uobject_locator_core;
namespace lifecycle = ff7r::piano::game::uobject_identity_lifecycle;

namespace {

using locator::Address;

[[noreturn]] void fail(const char* message)
{
    std::cerr << "uobject_identity_selftest: " << message << '\n';
    std::exit(1);
}

void require(const bool condition, const char* message)
{
    if (!condition) {
        fail(message);
    }
}

struct SyntheticMemory {
    std::unordered_map<Address, std::uint8_t> bytes;
    std::unordered_set<Address> unreadable;

    void fill(const Address address, const std::size_t size, const std::uint8_t value = 0)
    {
        for (std::size_t index = 0; index < size; ++index) {
            bytes[address + index] = value;
        }
    }

    template <typename T>
    void write(const Address address, const T& value)
    {
        const auto* source = reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            bytes[address + index] = source[index];
        }
    }

    template <typename T>
    T value(const Address address) const
    {
        T result{};
        auto* destination = reinterpret_cast<std::uint8_t*>(&result);
        for (std::size_t index = 0; index < sizeof(result); ++index) {
            const auto found = bytes.find(address + index);
            require(found != bytes.end(), "synthetic value read missing byte");
            destination[index] = found->second;
        }
        return result;
    }

    void deny(const Address address, const std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index) {
            unreadable.insert(address + index);
        }
    }

    void allow(const Address address, const std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index) {
            unreadable.erase(address + index);
        }
    }

    static bool read(
        void* context,
        const Address address,
        void* destination,
        const std::size_t size) noexcept
    {
        auto& memory = *static_cast<SyntheticMemory*>(context);
        auto* output = static_cast<std::uint8_t*>(destination);
        for (std::size_t index = 0; index < size; ++index) {
            const Address current = address + index;
            const auto found = memory.bytes.find(current);
            if (memory.unreadable.count(current) != 0 || found == memory.bytes.end()) {
                return false;
            }
            output[index] = found->second;
        }
        return true;
    }

    locator::MemoryReader reader() noexcept
    {
        return {this, &SyntheticMemory::read};
    }
};

constexpr int kSignature[]{0x03, -1, -1, -1, -1, 0x90};
constexpr Address kAdjustments[]{0x00, 0x04, 0x10, 0x20};

locator::LocatorFacts facts(
    const Address relative_displacement = 1,
    const Address instruction_size = 5,
    const Address* adjustments = kAdjustments,
    const std::size_t adjustment_count = sizeof(kAdjustments) / sizeof(kAdjustments[0]))
{
    return {
        kSignature,
        sizeof(kSignature) / sizeof(kSignature[0]),
        relative_displacement,
        instruction_size,
        adjustments,
        adjustment_count,
    };
}

void put_signature(
    SyntheticMemory& memory,
    const Address address,
    const std::int32_t displacement,
    const std::uint8_t wildcard_fill = 0x5a)
{
    memory.write(address, static_cast<std::uint8_t>(0x03));
    memory.fill(address + 1, 4, wildcard_fill);
    memory.write(address + 1, displacement);
    memory.write(address + 5, static_cast<std::uint8_t>(0x90));
}

struct PredicateState {
    std::vector<Address> accepted;
    std::vector<Address> calls;

    static bool test(void* context, const Address candidate) noexcept
    {
        auto& state = *static_cast<PredicateState*>(context);
        state.calls.push_back(candidate);
        return std::find(state.accepted.begin(), state.accepted.end(), candidate)
            != state.accepted.end();
    }

    locator::CandidatePredicate predicate() noexcept
    {
        return {this, &PredicateState::test};
    }
};

void test_pattern_semantics_and_malformed_reads()
{
    constexpr Address base = 0x100000;
    SyntheticMemory memory;
    memory.fill(base, 128, 0xcc);
    put_signature(memory, base + 12, 50 - (12 + 5), 0x11);
    put_signature(memory, base + 40, 90 - (40 + 5), 0x22);

    const Address adjustment[]{0};
    const auto locator_facts = facts(1, 5, adjustment, 1);
    PredicateState predicate{{base + 50}, {}};
    locator::LegacyLocator list(locator::LocatorPolicy::ListEager);
    const auto first = list.locate({base, 128, memory.reader()}, locator_facts, predicate.predicate());
    require(first.disposition == locator::LocateDisposition::Ready, "list first match should resolve");
    require(first.match_address == base + 12, "scanner must select first signature match");
    require(first.resolved == base + 50, "wildcard signature must preserve rel32 bytes");

    memory.write(base + 12, static_cast<std::uint8_t>(0x04));
    predicate.accepted = {base + 90};
    predicate.calls.clear();
    const auto fixed_mismatch = list.locate(
        {base, 128, memory.reader()}, locator_facts, predicate.predicate());
    require(fixed_mismatch.match_address == base + 40, "fixed mismatch must advance scanner");
    require(fixed_mismatch.resolved == base + 90, "later fixed match must resolve");

    memory.deny(base + 40, 1);
    const auto unreadable_fixed = list.locate(
        {base, 128, memory.reader()}, locator_facts, predicate.predicate());
    require(unreadable_fixed.disposition == locator::LocateDisposition::SignatureMissing,
        "unreadable fixed byte must reject that match");
    memory.allow(base + 40, 1);

    memory.write(base + 40, static_cast<std::uint8_t>(0x04));
    const auto missing_signature = list.locate(
        {base, 128, memory.reader()}, locator_facts, predicate.predicate());
    require(missing_signature.disposition == locator::LocateDisposition::SignatureMissing,
        "missing signature must fail closed");
    memory.write(base + 40, static_cast<std::uint8_t>(0x03));

    memory.deny(base + 42, 1);
    const int wildcard_signature[]{0x03, -1, -1, -1, -1, 0x90};
    const locator::LocatorFacts wildcard_facts{wildcard_signature, 6, 6, 1, adjustment, 1};
    memory.write(base + 46, static_cast<std::int32_t>(3));
    const auto wildcard_unreadable = list.locate(
        {base, 128, memory.reader()}, wildcard_facts, {});
    require(wildcard_unreadable.disposition == locator::LocateDisposition::Pending,
        "scanner must not read wildcard bytes");
    memory.allow(base + 42, 1);

    SyntheticMemory short_memory;
    short_memory.fill(base, 5, 0x03);
    const auto truncated_signature = list.locate(
        {base, 5, short_memory.reader()}, locator_facts, {});
    require(truncated_signature.disposition == locator::LocateDisposition::SignatureMissing,
        "truncated signature must fail closed");

    SyntheticMemory rel_memory;
    rel_memory.fill(base, 16, 0);
    put_signature(rel_memory, base + 2, 0);
    const auto distant_rel = facts(12, 5, adjustment, 1);
    rel_memory.deny(base + 14, 2);
    const auto truncated_rel = list.locate({base, 16, rel_memory.reader()}, distant_rel, {});
    require(truncated_rel.disposition == locator::LocateDisposition::RelativeDisplacementUnreadable,
        "truncated rel32 must be distinguished from missing signature");
}

void test_policy_arithmetic_and_valid_agreement()
{
    constexpr Address base = 0x200000;
    SyntheticMemory memory;
    memory.fill(base, 256, 0);
    put_signature(memory, base + 40, -16);

    const Address adjustments[]{0, 4};
    const auto locator_facts = facts(1, 6, adjustments, 2);
    const Address expected = base + 30;
    PredicateState list_predicate{{expected}, {}};
    PredicateState chart_predicate{{expected}, {}};
    locator::LegacyLocator list(locator::LocatorPolicy::ListEager);
    locator::LegacyLocator chart(locator::LocatorPolicy::ChartLazy);
    const auto list_result = list.locate(
        {base, 256, memory.reader()}, locator_facts, list_predicate.predicate());
    const auto chart_result = chart.locate(
        {base, 256, memory.reader()}, locator_facts, chart_predicate.predicate());
    Address chart_resolved = 0;
    require(chart.select(chart_predicate.predicate(), chart_resolved),
        "chart valid-domain selection should succeed");
    require(list_result.resolved == expected && chart_resolved == expected,
        "policies must agree for valid signed negative displacement");
    require(list.candidates() == chart.candidates(),
        "policies must agree on valid-domain candidate order");
    require(chart_result.disposition == locator::LocateDisposition::CandidatesReady,
        "chart policy must remain lazy");

    put_signature(memory, base + 40, 300);
    const auto list_out = list.locate({base, 256, memory.reader()}, locator_facts, {});
    const auto chart_out = chart.locate({base, 256, memory.reader()}, locator_facts, {});
    require(list_out.disposition == locator::LocateDisposition::TargetOutOfImage,
        "list policy must reject positive out-of-image target");
    require(chart_out.disposition == locator::LocateDisposition::CandidatesReady
            && chart_out.target == base + 346,
        "chart policy must retain positive out-of-image target");

    put_signature(memory, base + 2, -20);
    memory.write(base + 40, static_cast<std::uint8_t>(0));
    const auto list_underflow = list.locate({base, 256, memory.reader()}, locator_facts, {});
    const auto chart_underflow = chart.locate({base, 256, memory.reader()}, locator_facts, {});
    require(list_underflow.disposition == locator::LocateDisposition::TargetOutOfImage,
        "list negative target underflow must wrap then fail image bound");
    require(chart_underflow.target == base - 12,
        "chart signed target may point below the image");

    SyntheticMemory adjustment_memory;
    adjustment_memory.fill(base, 64, 0);
    put_signature(adjustment_memory, base + 4, 7);
    const Address underflow_adjustments[]{0x20};
    const auto adjustment_facts = facts(1, 5, underflow_adjustments, 1);
    const auto list_adjustment = list.locate(
        {base, 64, adjustment_memory.reader()}, adjustment_facts, {});
    const auto chart_adjustment = chart.locate(
        {base, 64, adjustment_memory.reader()}, adjustment_facts, {});
    require(list.candidates().at(0) == base + 16,
        "list adjustment underflow must fall back to unadjusted target");
    require(chart.candidates().at(0) == base - 16,
        "chart adjustment underflow must wrap across the image base");
    require(list_adjustment.disposition == locator::LocateDisposition::Pending
            && chart_adjustment.disposition == locator::LocateDisposition::CandidatesReady,
        "policy outcomes must expose eager pending versus lazy candidates");

    const Address wrapping_adjustments[]{std::numeric_limits<Address>::max()};
    const auto wrapping_facts = facts(1, 5, wrapping_adjustments, 1);
    list.locate({base, 64, adjustment_memory.reader()}, wrapping_facts, {});
    chart.locate({base, 64, adjustment_memory.reader()}, wrapping_facts, {});
    require(list.candidates().at(0) == base + 16,
        "list must suppress candidate-adjustment integer underflow");
    require(chart.candidates().at(0) == base + 17,
        "chart candidate subtraction must preserve unsigned wraparound");

    const Address high_base = std::numeric_limits<Address>::max() - 0x40;
    SyntheticMemory overflow_memory;
    overflow_memory.fill(high_base, 32, 0);
    put_signature(overflow_memory, high_base + 2, 8);
    const auto overflow_facts = facts(1, 0x80, underflow_adjustments, 1);
    const auto chart_overflow = chart.locate(
        {high_base, 32, overflow_memory.reader()}, overflow_facts, {});
    require(chart_overflow.disposition == locator::LocateDisposition::CandidatesReady
            && chart_overflow.target < high_base,
        "chart absolute target addition must preserve unsigned overflow");

    constexpr Address low_base = 0x8000;
    SyntheticMemory low_memory;
    low_memory.fill(low_base, 32, 0);
    put_signature(low_memory, low_base + 4, 4);
    const auto list_low = list.locate({low_base, 32, low_memory.reader()}, facts(), {});
    const auto chart_low = chart.locate({low_base, 32, low_memory.reader()}, facts(), {});
    require(list_low.disposition == locator::LocateDisposition::Pending,
        "list rel32 read must not impose chart's low-address floor");
    require(chart_low.disposition == locator::LocateDisposition::RelativeDisplacementUnreadable,
        "chart rel32 read must retain its 0x10000 address floor");
}

void test_eager_lazy_retry_and_invalidation()
{
    constexpr Address base = 0x300000;
    SyntheticMemory memory;
    memory.fill(base, 128, 0);
    put_signature(memory, base + 8, 64 - (8 + 5));
    const Address adjustments[]{0, 4, 16};
    const auto locator_facts = facts(1, 5, adjustments, 3);
    const Address first = base + 64;
    const Address second = base + 60;
    const Address third = base + 48;

    PredicateState list_predicate{{second}, {}};
    locator::LegacyLocator list(locator::LocatorPolicy::ListEager);
    const auto eager = list.locate(
        {base, 128, memory.reader()}, locator_facts, list_predicate.predicate());
    require(eager.resolved == second, "list eager policy must pick first valid candidate");
    require(list_predicate.calls == std::vector<Address>({first, second}),
        "list eager policy must validate in candidate order");

    PredicateState chart_predicate{{second}, {}};
    locator::LegacyLocator chart(locator::LocatorPolicy::ChartLazy);
    chart.locate({base, 128, memory.reader()}, locator_facts, chart_predicate.predicate());
    require(chart_predicate.calls.empty(), "chart locate must not validate candidates eagerly");
    Address selected = 0;
    require(chart.select(chart_predicate.predicate(), selected) && selected == second,
        "chart lazy selection must preserve candidate order");

    chart_predicate.accepted = {third};
    chart_predicate.calls.clear();
    require(chart.select(chart_predicate.predicate(), selected) && selected == third,
        "invalid cached candidate must fall back to later candidate");
    require(chart_predicate.calls == std::vector<Address>({second, first, second, third}),
        "fallback must retry cached address then the complete candidate sequence");

    PredicateState pending_predicate{{}, {}};
    const auto pending = list.locate(
        {base, 128, memory.reader()}, locator_facts, pending_predicate.predicate());
    require(pending.disposition == locator::LocateDisposition::Pending,
        "list locator must retain candidates when no header is initially valid");
    pending_predicate.accepted = {third};
    pending_predicate.calls.clear();
    require(list.select(pending_predicate.predicate(), selected) && selected == third,
        "list pending locator must allow later-valid retry");

    pending_predicate.accepted.clear();
    chart.locate({base, 128, memory.reader()}, locator_facts, pending_predicate.predicate());
    require(!chart.select(pending_predicate.predicate(), selected),
        "chart initial invalid candidates must remain unresolved");
    pending_predicate.accepted = {first};
    require(chart.select(pending_predicate.predicate(), selected) && selected == first,
        "chart must retry candidates that become valid later");
}

void write_header(
    SyntheticMemory& memory,
    const Address address,
    const locator::GUObjectArrayHeader& header,
    const locator::GUObjectArrayLayout& layout = {})
{
    memory.write(address + layout.chunks, header.chunks);
    memory.write(address + layout.max_elements, header.max_elements);
    memory.write(address + layout.num_elements, header.num_elements);
    memory.write(address + layout.max_chunks, header.max_chunks);
    memory.write(address + layout.num_chunks, header.num_chunks);
}

void test_header_predicate_domains_and_unreadability()
{
    const locator::GUObjectArrayHeader both{0x20000, 100, 50, 4, 2};
    const locator::GUObjectArrayHeader list_only{1, 1, 50, 1, 2};
    const locator::GUObjectArrayHeader chart_only{0x20000, 2'000'000, 2'000'000, 1024, 1024};
    require(locator::list_header_accepts(both) && locator::chart_header_accepts(0x10000, both),
        "shared valid header must satisfy both domains");
    require(locator::list_header_accepts(list_only)
            && !locator::chart_header_accepts(0x10000, list_only),
        "list domain must retain permissive maxima and pointer behavior");
    require(!locator::list_header_accepts(chart_only)
            && locator::chart_header_accepts(0x10000, chart_only),
        "chart domain must retain inclusive larger bounds");
    require(!locator::chart_header_accepts(0xffff, both),
        "chart domain must retain candidate-address floor");

    constexpr Address candidate = 0x410000;
    SyntheticMemory memory;
    write_header(memory, candidate, both);
    locator::HeaderCandidateContext context{memory.reader(), {}, locator::HeaderDomain::List};
    require(locator::header_candidate_accepts(&context, candidate),
        "readable valid header must be accepted");
    memory.deny(candidate + context.layout.num_elements, sizeof(std::int32_t));
    require(!locator::header_candidate_accepts(&context, candidate),
        "partially unreadable header must fail closed");
}

struct IdentityFixture {
    static constexpr Address array = 0x500000;
    static constexpr Address chunks = 0x510000;
    static constexpr Address chunk = 0x520000;
    static constexpr Address object = 0x530000;
    static constexpr std::int32_t index = 3;

    SyntheticMemory memory;
    locator::UObjectIdentityAccess access{};
    bool mutate_serial_after_root = false;

    IdentityFixture()
    {
        const locator::GUObjectArrayHeader header{chunks, 16, 8, 1, 1};
        write_header(memory, array, header);
        memory.write(chunks, chunk);
        memory.write(item_address(), object);
        memory.write(flags_address(), std::int32_t{0});
        memory.write(item_address() + 0x10, std::int32_t{7});
        memory.write(object + 0x08, std::uint32_t{0});
        memory.write(object + 0x0c, index);
        access.memory = memory.reader();
        access.atomic_flags = {this, &IdentityFixture::atomic_load, &IdentityFixture::atomic_compare_exchange};
        access.array_address = array;
    }

    static constexpr Address item_address()
    {
        return chunk + static_cast<Address>(index) * 0x18;
    }

    static constexpr Address flags_address()
    {
        return item_address() + 0x08;
    }

    static bool atomic_load(void* context, const Address address, std::int32_t& value) noexcept
    {
        auto& fixture = *static_cast<IdentityFixture*>(context);
        if (fixture.memory.unreadable.count(address) != 0) {
            return false;
        }
        const auto found = fixture.memory.bytes.find(address);
        if (found == fixture.memory.bytes.end()) {
            return false;
        }
        value = fixture.memory.value<std::int32_t>(address);
        return true;
    }

    static bool atomic_compare_exchange(
        void* context,
        const Address address,
        const std::int32_t expected,
        const std::int32_t desired,
        std::int32_t& observed) noexcept
    {
        auto& fixture = *static_cast<IdentityFixture*>(context);
        if (!atomic_load(context, address, observed)) {
            return false;
        }
        if (observed == expected) {
            fixture.memory.write(address, desired);
            if (fixture.mutate_serial_after_root
                && (desired & fixture.access.facts.root_set) != 0
                && (expected & fixture.access.facts.root_set) == 0) {
                fixture.memory.write(item_address() + 0x10, std::int32_t{8});
                fixture.mutate_serial_after_root = false;
            }
        }
        return true;
    }
};

void test_live_handle_capture_classifier()
{
    using Result = locator::UObjectLiveHandleCaptureResult;
    using Mutate = void (*)(IdentityFixture&);
    struct Case {
        Result expected;
        Mutate mutate;
    };
    const Case cases[]{
        {Result::UObjectFieldsUnavailable, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::object + 0x0c, sizeof(std::int32_t));
         }},
        {Result::IndexOutOfRange, [](IdentityFixture& fixture) {
             fixture.memory.write(IdentityFixture::object + 0x0c, std::int32_t{8});
         }},
        {Result::HeaderInvalid, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::array + 0x10, sizeof(Address));
         }},
        {Result::ChunkOutOfRange, [](IdentityFixture& fixture) {
             fixture.access.facts.elements_per_chunk = 2;
         }},
        {Result::ChunkPointerUnreadableOrNull, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::chunks, sizeof(Address));
         }},
        {Result::ChunkPointerUnreadableOrNull, [](IdentityFixture& fixture) {
             fixture.memory.write(IdentityFixture::chunks, Address{0});
         }},
        {Result::ItemAddressOrReadFailure, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::chunks, std::numeric_limits<Address>::max() - 0x10);
         }},
        {Result::ItemAddressOrReadFailure, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::item_address(), sizeof(Address));
         }},
        {Result::ObjectMismatch, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::item_address(), IdentityFixture::object + 0x100);
         }},
        {Result::SerialInvalid, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::item_address() + 0x10, std::int32_t{0});
         }},
        {Result::ItemFlagsDead, [](IdentityFixture& fixture) {
             fixture.memory.write(IdentityFixture::flags_address(), std::int32_t{1 << 28});
         }},
        {Result::UObjectDestroyed, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::object + 0x08, std::uint32_t{0x8000});
         }},
        {Result::Success, [](IdentityFixture&) {}},
    };

    require(locator::classify_live_handle_capture_access(false, Result::Success)
            == Result::ResolverOrViewUnavailable,
        "unavailable resolver/view must be the access classifier result");
    for (const Case& test : cases) {
        require(locator::classify_live_handle_capture_access(true, test.expected)
                == test.expected,
            "available resolver/view must preserve the first capture result");
        IdentityFixture fixture;
        test.mutate(fixture);
        locator::UObjectLiveHandle handle{91, 92};
        const Result actual = locator::capture_live_handle_diagnostic(
            fixture.access, IdentityFixture::object, handle);
        require(actual == test.expected, "live-handle capture classifier branch mismatch");
        if (actual == Result::Success) {
            require(handle.internal_index == IdentityFixture::index && handle.serial_number == 7,
                "successful diagnostic capture must publish the live handle");
        } else {
            require(handle.internal_index == 91 && handle.serial_number == 92,
                "failed diagnostic capture must not publish a partial handle");
        }
    }
}

void test_item_backed_zero_serial_capture_classifier()
{
    using Result = locator::UObjectItemBackedZeroSerialCaptureResult;
    using Mutate = void (*)(IdentityFixture&);
    struct Case {
        Result expected;
        Mutate mutate;
    };
    const Case cases[]{
        {Result::UObjectFieldsUnavailable, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::object + 0x0c, sizeof(std::int32_t));
         }},
        {Result::UObjectFieldsUnavailable, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::object + 0x08, sizeof(std::uint32_t));
         }},
        {Result::IndexOutOfRange, [](IdentityFixture& fixture) {
             fixture.memory.write(IdentityFixture::object + 0x0c, std::int32_t{-1});
         }},
        {Result::IndexOutOfRange, [](IdentityFixture& fixture) {
             fixture.memory.write(IdentityFixture::object + 0x0c, std::int32_t{8});
         }},
        {Result::HeaderInvalid, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::array + 0x10, sizeof(Address));
         }},
        {Result::HeaderInvalid, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::array + 0x24, std::int32_t{-1});
         }},
        {Result::ChunkOutOfRange, [](IdentityFixture& fixture) {
             fixture.access.facts.elements_per_chunk = 2;
         }},
        {Result::ChunkPointerUnreadableOrNull, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::chunks, sizeof(Address));
         }},
        {Result::ChunkPointerUnreadableOrNull, [](IdentityFixture& fixture) {
             fixture.memory.write(IdentityFixture::chunks, Address{0});
         }},
        {Result::ItemAddressOrReadFailure, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::chunks, std::numeric_limits<Address>::max() - 0x10);
         }},
        {Result::ItemAddressOrReadFailure, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::item_address(), sizeof(Address));
         }},
        {Result::ItemAddressOrReadFailure, [](IdentityFixture& fixture) {
             fixture.memory.deny(IdentityFixture::flags_address(), sizeof(std::int32_t));
         }},
        {Result::ObjectMismatch, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::item_address(), IdentityFixture::object + 0x100);
         }},
        {Result::SerialPositive, [](IdentityFixture&) {}},
        {Result::SerialNegative, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::item_address() + 0x10, std::int32_t{-1});
         }},
        {Result::ItemFlagsDead, [](IdentityFixture& fixture) {
             fixture.memory.write(IdentityFixture::flags_address(), std::int32_t{1 << 28});
             fixture.memory.write(
                 IdentityFixture::item_address() + 0x10, std::int32_t{0});
         }},
        {Result::ItemFlagsDead, [](IdentityFixture& fixture) {
             fixture.memory.write(IdentityFixture::flags_address(), std::int32_t{1 << 29});
             fixture.memory.write(
                 IdentityFixture::item_address() + 0x10, std::int32_t{0});
         }},
        {Result::UObjectDestroyed, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::object + 0x08, std::uint32_t{0x8000});
             fixture.memory.write(
                 IdentityFixture::item_address() + 0x10, std::int32_t{0});
         }},
        {Result::UObjectDestroyed, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::object + 0x08, std::uint32_t{0x10000});
             fixture.memory.write(
                 IdentityFixture::item_address() + 0x10, std::int32_t{0});
         }},
        {Result::Success, [](IdentityFixture& fixture) {
             fixture.memory.write(
                 IdentityFixture::item_address() + 0x10, std::int32_t{0});
         }},
    };

    require(locator::classify_item_backed_zero_serial_capture_access(
                false, Result::Success)
            == Result::ResolverOrViewUnavailable,
        "unavailable resolver/view must reject zero-serial item capture");
    std::size_t case_index = 0;
    for (const Case& test : cases) {
        IdentityFixture fixture;
        test.mutate(fixture);
        locator::UObjectItemBackedZeroSerialSnapshot snapshot{91, 92};
        const Result actual =
            locator::capture_item_backed_zero_serial_snapshot_diagnostic(
                fixture.access, IdentityFixture::object, snapshot);
        if (actual != test.expected) {
            std::cerr << "zero-serial classifier case=" << case_index
                      << " expected=" << static_cast<int>(test.expected)
                      << " actual=" << static_cast<int>(actual) << '\n';
        }
        require(actual == test.expected,
            "zero-serial item capture classifier branch mismatch");
        if (actual == Result::Success) {
            require(snapshot.internal_index == IdentityFixture::index
                    && snapshot.serial_number == 0,
                "successful zero-serial item capture must publish exact fields");
        } else {
            require(snapshot.internal_index == 91 && snapshot.serial_number == 92,
                "failed zero-serial item capture must not publish partial fields");
        }
        ++case_index;
    }

    IdentityFixture fixture;
    fixture.memory.write(
        IdentityFixture::item_address() + 0x10, std::int32_t{0});
    locator::UObjectItemBackedZeroSerialSnapshot expected{};
    require(locator::item_backed_zero_serial_capture_succeeded(
                locator::capture_item_backed_zero_serial_snapshot_diagnostic(
                    fixture.access, IdentityFixture::object, expected))
            && locator::validate_item_backed_zero_serial_snapshot(
                fixture.access, IdentityFixture::object, expected),
        "unchanged zero-serial item snapshot must freshly validate");

    constexpr std::int32_t drifted_index = IdentityFixture::index + 1;
    constexpr Address drifted_item = IdentityFixture::chunk
        + static_cast<Address>(drifted_index) * 0x18;
    fixture.memory.write(drifted_item, IdentityFixture::object);
    fixture.memory.write(drifted_item + 0x08, std::int32_t{0});
    fixture.memory.write(drifted_item + 0x10, std::int32_t{0});
    fixture.memory.write(
        IdentityFixture::object + 0x0c, drifted_index);
    require(!locator::validate_item_backed_zero_serial_snapshot(
                fixture.access, IdentityFixture::object, expected),
        "zero-serial item index drift must reject the retained snapshot");

    fixture.memory.write(
        IdentityFixture::object + 0x0c, IdentityFixture::index);
    fixture.memory.write(
        IdentityFixture::item_address() + 0x10, std::int32_t{8});
    require(!locator::validate_item_backed_zero_serial_snapshot(
                fixture.access, IdentityFixture::object, expected),
        "zero-serial item changing to positive serial must reject");
    fixture.memory.write(
        IdentityFixture::item_address() + 0x10, std::int32_t{-8});
    require(!locator::validate_item_backed_zero_serial_snapshot(
                fixture.access, IdentityFixture::object, expected),
        "zero-serial item changing to negative serial must reject");
}

void test_serial_identity_dead_flags_and_raw_pointer_gap()
{
    IdentityFixture fixture;
    locator::UObjectLiveHandle handle{};
    require(locator::capture_live_handle(fixture.access, IdentityFixture::object, handle),
        "live object identity must be captured");
    require(handle.internal_index == IdentityFixture::index && handle.serial_number == 7,
        "capture must preserve index and serial identity");
    require(locator::validate_live_handle(fixture.access, IdentityFixture::object, handle),
        "unchanged identity must validate");

    fixture.memory.write(IdentityFixture::item_address() + 0x10, std::int32_t{8});
    require(!locator::validate_live_handle(fixture.access, IdentityFixture::object, handle),
        "serial reuse must invalidate old handle");
    fixture.memory.write(IdentityFixture::item_address() + 0x10, std::int32_t{7});

    fixture.memory.write(IdentityFixture::flags_address(), std::int32_t{1 << 28});
    require(!locator::capture_live_handle(fixture.access, IdentityFixture::object, handle),
        "unreachable item must not produce a live handle");
    fixture.memory.write(IdentityFixture::flags_address(), std::int32_t{1 << 29});
    require(!locator::capture_live_handle(fixture.access, IdentityFixture::object, handle),
        "pending-kill item must not produce a live handle");
    fixture.memory.write(IdentityFixture::flags_address(), std::int32_t{0});
    fixture.memory.write(IdentityFixture::object + 0x08, std::uint32_t{0x8000});
    require(!locator::capture_live_handle(fixture.access, IdentityFixture::object, handle),
        "begin-destroyed UObject must not produce a live handle");
    fixture.memory.write(IdentityFixture::object + 0x08, std::uint32_t{0x10000});
    require(!locator::capture_live_handle(fixture.access, IdentityFixture::object, handle),
        "finish-destroyed UObject must not produce a live handle");
    fixture.memory.write(IdentityFixture::object + 0x08, std::uint32_t{0});

    Address raw_object = 0;
    fixture.memory.write(IdentityFixture::item_address() + 0x10, std::int32_t{0});
    fixture.memory.write(IdentityFixture::flags_address(), std::int32_t{1 << 28});
    require(locator::read_uobject_item_pointer(fixture.access, IdentityFixture::index, raw_object)
            && raw_object == IdentityFixture::object,
        "raw chart-style lookup must expose non-null item pointer without identity checks");
    require(!locator::capture_live_handle(fixture.access, raw_object, handle),
        "serial/dead validation must expose the chart raw-pointer identity gap");

    fixture.memory.deny(IdentityFixture::item_address(), sizeof(Address));
    require(!locator::read_uobject_item_pointer(fixture.access, IdentityFixture::index, raw_object),
        "unreadable item object pointer must fail closed");
}

void test_root_pin_success_and_race_rollback()
{
    IdentityFixture success;
    locator::UObjectLiveHandle handle{};
    require(locator::capture_live_handle(success.access, IdentityFixture::object, handle),
        "pin fixture must start live");
    require(locator::pin_live_handle(success.access, IdentityFixture::object, handle),
        "pin must atomically set root on unchanged identity");
    require((success.memory.value<std::int32_t>(IdentityFixture::flags_address())
                & success.access.facts.root_set) != 0,
        "successful pin must retain root flag");
    require(locator::is_live_rooted(success.access, IdentityFixture::object, handle),
        "rooted query must revalidate identity and root flag");

    IdentityFixture raced;
    locator::UObjectLiveHandle raced_handle{};
    require(locator::capture_live_handle(raced.access, IdentityFixture::object, raced_handle),
        "race fixture must start live");
    raced.mutate_serial_after_root = true;
    require(!locator::pin_live_handle(raced.access, IdentityFixture::object, raced_handle),
        "post-CAS serial race must fail pin verification");
    require((raced.memory.value<std::int32_t>(IdentityFixture::flags_address())
                & raced.access.facts.root_set) == 0,
        "failed post-CAS verification must roll back only the root bit set by this pin");
}

struct LocatorLifecycleFixture {
    bool list_state = true;
    bool chart_state = true;

    void apply(const lifecycle::Transition& transition)
    {
        if (transition.effects.clear_list_locator) {
            list_state = false;
        }
        if (transition.effects.clear_chart_locator) {
            chart_state = false;
        }
    }
};

void test_deferred_identity_clear_and_initialization_retry()
{
    lifecycle::State state{};
    auto transition = lifecycle::transition(state, lifecycle::Event::Initialize);
    require(transition.effects.initialize && transition.after.initialized,
        "first initialization must start");
    state = transition.after;

    transition = lifecycle::transition(state, lifecycle::Event::ActivateChartConsumer);
    require(transition.after.chart_consumer_active,
        "chart activation must mark only the chart consumer active");
    state = transition.after;

    LocatorLifecycleFixture locator_state;
    transition = lifecycle::transition(state, lifecycle::Event::Clear);
    locator_state.apply(transition);
    require(transition.effects.clear_list_locator && !transition.effects.clear_chart_locator,
        "clear with an active chart must clear only list locator state");
    require(!locator_state.list_state && locator_state.chart_state,
        "pending clear must retain raw chart locator state");
    require(transition.after.initialized && transition.after.chart_consumer_active
            && transition.after.clear_requested,
        "active chart clear must remain pending and initialized");
    state = transition.after;

    const auto blocked_retry = lifecycle::transition(state, lifecycle::Event::Initialize);
    require(!blocked_retry.effects.initialize
            && blocked_retry.after.initialized
            && blocked_retry.after.chart_consumer_active
            && blocked_retry.after.clear_requested,
        "initialization retry must not reset a pending clear");

    transition = lifecycle::transition(state, lifecycle::Event::ReleaseChartConsumer);
    locator_state.apply(transition);
    require(transition.effects.clear_chart_locator && !transition.effects.clear_list_locator,
        "chart release must complete only the deferred chart clear");
    require(!locator_state.list_state && !locator_state.chart_state,
        "completed deferred clear must leave both locator states cleared");
    require(!transition.after.initialized && !transition.after.chart_consumer_active
            && !transition.after.clear_requested,
        "chart release must complete deferred identity teardown");
    state = transition.after;

    const auto allowed_retry = lifecycle::transition(state, lifecycle::Event::Initialize);
    require(allowed_retry.effects.initialize && allowed_retry.after.initialized,
        "initialization retry must start after complete teardown");
}

void test_identity_lifecycle_policy_state_isolation_edges()
{
    auto state = lifecycle::transition({}, lifecycle::Event::Initialize).after;
    LocatorLifecycleFixture locator_state;

    auto transition = lifecycle::transition(state, lifecycle::Event::ReleaseChartConsumer);
    require(!transition.effects.clear_list_locator && !transition.effects.clear_chart_locator
            && transition.after.initialized && !transition.after.clear_requested,
        "chart release without pending clear must not disturb identity state");

    transition = lifecycle::transition(state, lifecycle::Event::ActivateChartConsumer);
    state = transition.after;
    const auto duplicate_initialize = lifecycle::transition(state, lifecycle::Event::Initialize);
    require(!duplicate_initialize.effects.initialize
            && duplicate_initialize.after.chart_consumer_active,
        "duplicate initialization must not reset active chart state");

    transition = lifecycle::transition(state, lifecycle::Event::ReleaseChartConsumer);
    locator_state.apply(transition);
    require(locator_state.list_state && locator_state.chart_state
            && transition.after.initialized,
        "chart activate/release without clear must preserve both locator states");
    state = transition.after;

    transition = lifecycle::transition(state, lifecycle::Event::Clear);
    locator_state.apply(transition);
    require(transition.effects.clear_list_locator && transition.effects.clear_chart_locator,
        "clear without an active chart must clear both locator states immediately");
    require(!transition.after.initialized && !transition.after.clear_requested,
        "immediate clear must complete teardown");

    state = lifecycle::transition({}, lifecycle::Event::Initialize).after;
    state = lifecycle::transition(state, lifecycle::Event::ActivateChartConsumer).after;
    transition = lifecycle::transition(state, lifecycle::Event::Clear);
    state = transition.after;
    transition = lifecycle::transition(state, lifecycle::Event::Clear);
    require(transition.effects.clear_list_locator && !transition.effects.clear_chart_locator
            && transition.after.chart_consumer_active && transition.after.clear_requested,
        "repeated pending clear must remain list-only until chart release");
}

} // namespace

int main()
{
    test_pattern_semantics_and_malformed_reads();
    test_policy_arithmetic_and_valid_agreement();
    test_eager_lazy_retry_and_invalidation();
    test_header_predicate_domains_and_unreadability();
    test_live_handle_capture_classifier();
    test_item_backed_zero_serial_capture_classifier();
    test_serial_identity_dead_flags_and_raw_pointer_gap();
    test_root_pin_success_and_race_rollback();
    test_deferred_identity_clear_and_initialization_retry();
    test_identity_lifecycle_policy_state_isolation_edges();
    return 0;
}
