#include "game/piano_page_selection_guard.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace guard = ff7r::piano::game::piano_page_selection_guard;

namespace {
int resolver_calls = 0;
int marker_calls = 0;
int marker_selected = 0;

void __fastcall marker_probe(const std::int32_t selected) noexcept
{
    ++marker_calls;
    marker_selected = selected;
}

void* __fastcall resolver_probe(void*) noexcept
{
    ++resolver_calls;
    return nullptr;
}

int fail(const std::string& message)
{
    std::cerr << "piano_page_selection_guard_selftest: " << message << '\n';
    return 1;
}

void append_u64(std::vector<std::uint8_t>& bytes, const std::uintptr_t value)
{
    for (unsigned shift = 0; shift != 64; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

struct Executable final {
    void* memory = nullptr;
    explicit Executable(const std::vector<std::uint8_t>& bytes)
    {
        memory = VirtualAlloc(nullptr, bytes.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!memory) return;
        std::memcpy(memory, bytes.data(), bytes.size());
        unsigned long old = 0;
        if (!VirtualProtect(memory, bytes.size(), PAGE_EXECUTE_READ, &old)
            || !FlushInstructionCache(GetCurrentProcess(), memory, bytes.size())) {
            VirtualFree(memory, 0, MEM_RELEASE);
            memory = nullptr;
        }
    }
    ~Executable() { if (memory) VirtualFree(memory, 0, MEM_RELEASE); }
    Executable(const Executable&) = delete;
    Executable& operator=(const Executable&) = delete;
};

int test_dispatch_and_marker()
{
    for (const int selected : {-1, -2}) {
        int resolver = 0;
        int continuation = 0;
        guard::dispatch_selected_index(selected, [&] { ++resolver; }, [&] { ++continuation; });
        if (resolver != 0 || continuation != 1) return fail("negative dispatch touched resolver");
    }
    for (const int selected : {0, 4}) {
        int resolver = 0;
        int continuation = 0;
        guard::dispatch_selected_index(selected, [&] { ++resolver; }, [&] { ++continuation; });
        if (resolver != 1 || continuation != 0) return fail("nonnegative dispatch changed native flow");
    }
    int unresolved = 0;
    guard::dispatch_selected_index(4, [&] { ++unresolved; }, [] {});
    if (unresolved != 1) return fail("nonnegative unresolved handle was guarded");

    guard::BoundedMarkerSet<> markers;
    for (std::size_t index = 0; index != guard::kMarkerCapacity; ++index) {
        if (!markers.insert({index, -1})) return fail("bounded marker rejected unique identity");
    }
    if (markers.insert({0, -1}) || markers.insert({99, -2})
        || markers.size() != guard::kMarkerCapacity) return fail("bounded marker cap or dedupe failed");
    int sinks = 0;
    guard::capture_and_emit_marker(-2, [](guard::Marker& marker) {
        marker.playback_generation = 7;
        marker.catalog_revision = 3;
        marker.catalog_revision_exact = true;
        marker.visible_index = 10;
        marker.base_slot = 0;
        return true;
    }, [&](const guard::Marker& marker) {
        ++sinks;
        if (!marker.unexpected_negative || marker.selected_index != -2
            || marker.playback_generation != 7 || !marker.catalog_revision_exact
            || marker.catalog_revision != 3 || marker.visible_index != 10
            || marker.base_slot != 0) throw std::runtime_error("marker mismatch");
    });
    guard::capture_and_emit_marker(-1, [](guard::Marker&) -> bool {
        throw std::runtime_error("capture");
    }, [&](const guard::Marker&) { ++sinks; });
    guard::capture_and_emit_marker(-1, [](guard::Marker&) { return true; },
        [](const guard::Marker&) { throw std::runtime_error("sink"); });
    if (sinks != 1) return fail("marker exception containment failed");
    return 0;
}

int test_relay_abi()
{
    // The negative continuation rejoins a shared cleanup label in the generated
    // caller, exactly as the native continuation eventually returns through its
    // owning function frame.
    std::vector<std::uint8_t> continuation{0xb8, 0x16, 0, 0, 0, 0x49, 0xbb};
    append_u64(continuation, 0); // patched after caller allocation
    continuation.insert(continuation.end(), {0x41, 0xff, 0xe3});
    Executable continuation_code(continuation);
    if (!continuation_code.memory) return fail("continuation allocation failed");

    const auto relay_bytes = guard::make_guard_relay(
        reinterpret_cast<const void*>(&marker_probe),
        reinterpret_cast<const void*>(&resolver_probe), continuation_code.memory);
    if (relay_bytes.size() != 52 || relay_bytes[0] != 0x45 || relay_bytes[3] != 0x79
        || relay_bytes[4] != 0x23) return fail("relay layout changed");
    Executable relay(relay_bytes);
    if (!relay.memory) return fail("relay allocation failed");

    std::vector<std::uint8_t> caller{
        0x41, 0x54,                         // push r12
        0x48, 0x83, 0xec, 0x20,             // sub rsp,20h
        0x41, 0x89, 0xcc,                   // mov r12d,ecx
        0x48, 0xb8                          // mov rax,relay
    };
    append_u64(caller, reinterpret_cast<std::uintptr_t>(relay.memory));
    caller.insert(caller.end(), {
        0xff, 0xd0,                         // call rax
        0xb8, 0x0b, 0, 0, 0,               // mov eax,11
        0x48, 0x83, 0xc4, 0x20,             // cleanup: add rsp,20h
        0x41, 0x5c,                         // pop r12
        0xc3                                // ret
    });
    Executable caller_code(caller);
    if (!caller_code.memory) return fail("caller allocation failed");
    const auto cleanup = reinterpret_cast<std::uintptr_t>(caller_code.memory) + caller.size() - 7;
    unsigned long old = 0;
    if (!VirtualProtect(continuation_code.memory, continuation.size(), PAGE_READWRITE, &old))
        return fail("continuation repatch protection failed");
    std::memcpy(static_cast<std::uint8_t*>(continuation_code.memory) + 7, &cleanup, sizeof(cleanup));
    if (!VirtualProtect(continuation_code.memory, continuation.size(), PAGE_EXECUTE_READ, &old)
        || !FlushInstructionCache(GetCurrentProcess(), continuation_code.memory, continuation.size()))
        return fail("continuation repatch failed");

    using Caller = int(__fastcall*)(int);
    const auto invoke = reinterpret_cast<Caller>(caller_code.memory);
    resolver_calls = marker_calls = 0;
    const int negative_one_result = invoke(-1);
    if (negative_one_result != 22 || marker_calls != 1 || marker_selected != -1 || resolver_calls != 0)
        return fail("-1 relay bypass failed result=" + std::to_string(negative_one_result)
            + " markers=" + std::to_string(marker_calls)
            + " selected=" + std::to_string(marker_selected)
            + " resolvers=" + std::to_string(resolver_calls));
    if (invoke(-2) != 22 || marker_calls != 2 || marker_selected != -2 || resolver_calls != 0)
        return fail("-2 relay bypass failed");
    if (invoke(0) != 11 || invoke(4) != 11 || resolver_calls != 2 || marker_calls != 2)
        return fail("nonnegative relay tail call failed");
    return 0;
}

struct PatchModel final {
    std::array<std::uint8_t, 5> bytes{};
    unsigned long protection = PAGE_EXECUTE_READ;
    bool write_ok = true;
    bool write_fail_after_partial = false;
    bool zero_write = false;
    bool partial_write = false;
    bool flush_ok = true;
    bool query_ok = true;
    bool protect_write_ok = true;
    bool restore_ok = true;
    bool drift = false;
};

guard::PatchOperations operations(PatchModel& model)
{
    return {
        [&](const void*, void* out, std::size_t size) { std::memcpy(out, model.bytes.data(), size); return true; },
        [&](void*, const void* in, std::size_t size) {
            if (model.write_fail_after_partial) {
                std::memcpy(model.bytes.data(), in, 2);
                model.write_fail_after_partial = false;
                return false;
            }
            if (!model.write_ok) return false;
            if (!model.zero_write) std::memcpy(model.bytes.data(), in, model.partial_write ? 2 : size);
            return true;
        },
        [&](void*, std::size_t, unsigned long requested, unsigned long* prior) {
            if (requested == PAGE_EXECUTE_READWRITE && !model.protect_write_ok) return false;
            if (requested != PAGE_EXECUTE_READWRITE && !model.restore_ok) return false;
            *prior = model.drift && requested == PAGE_EXECUTE_READWRITE
                ? PAGE_READONLY : model.protection;
            model.protection = requested;
            return true;
        },
        [&](const void*, std::size_t) { return model.flush_ok; },
        [&](const void*, unsigned long& value) { value = model.protection; return model.query_ok; },
    };
}

int test_patch_transaction()
{
    alignas(8) std::array<std::uint8_t, 16> storage{};
    void* site = storage.data();
    const void* original_target = storage.data() + 10;
    const void* replacement_target = reinterpret_cast<const void*>(
        reinterpret_cast<std::uintptr_t>(site) + 0x12345678);
    std::array<std::uint8_t, 5> original{};
    std::array<std::uint8_t, 5> replacement{};
    if (!guard::make_rel32_call(site, original_target, original)
        || !guard::make_rel32_call(site, replacement_target, replacement))
        return fail("rel32 setup failed");
    const auto run = [&](PatchModel& model, guard::Rel32PatchState expected,
                         const bool expected_result) {
        guard::Rel32PatchState state = guard::Rel32PatchState::Unsafe;
        std::string error;
        const bool result = guard::replace_rel32_call_transactionally(site, original,
            original_target, replacement_target, operations(model), state, error);
        return result == expected_result && state == expected;
    };
    PatchModel success{original};
    if (!run(success, guard::Rel32PatchState::Replacement, true)
        || success.bytes != replacement || success.protection != PAGE_EXECUTE_READ)
        return fail("successful transaction failed");
    PatchModel already{replacement};
    if (!run(already, guard::Rel32PatchState::Replacement, false))
        return fail("replacement classification failed");
    PatchModel torn{original}; torn.bytes[2] ^= 0x7f;
    if (!run(torn, guard::Rel32PatchState::Unsafe, false)) return fail("torn startup was not unsafe");
    PatchModel protect_fail{original}; protect_fail.protect_write_ok = false;
    if (!run(protect_fail, guard::Rel32PatchState::Original, false)) return fail("protect failure state failed");
    for (int mode = 0; mode != 6; ++mode) {
        PatchModel failed{original};
        if (mode == 0) failed.write_ok = false;
        if (mode == 1) failed.zero_write = true;
        if (mode == 2) failed.partial_write = true;
        if (mode == 3) failed.flush_ok = false;
        if (mode == 4) failed.restore_ok = false;
        if (mode == 5) failed.write_fail_after_partial = true;
        const auto expected = mode == 0 || mode == 3 || mode == 4
            ? guard::Rel32PatchState::Unsafe : guard::Rel32PatchState::Original;
        guard::Rel32PatchState observed = guard::Rel32PatchState::Unsafe;
        std::string error;
        const bool result = guard::replace_rel32_call_transactionally(site, original,
            original_target, replacement_target, operations(failed), observed, error);
        if (result || observed != expected) return fail("write/verify rollback matrix failed mode="
            + std::to_string(mode) + " state=" + std::to_string(static_cast<int>(observed))
            + " error=" + error);
    }
    PatchModel torn_flush_failure{original};
    torn_flush_failure.write_fail_after_partial = true;
    torn_flush_failure.flush_ok = false;
    if (!run(torn_flush_failure, guard::Rel32PatchState::Unsafe, false))
        return fail("partial failed write plus rollback flush failure was not unsafe");
    PatchModel drift{original}; drift.drift = true;
    if (!run(drift, guard::Rel32PatchState::Unsafe, false)) return fail("protection drift was not unsafe");
    PatchModel query{original}; query.query_ok = false;
    if (!run(query, guard::Rel32PatchState::Unsafe, false)) return fail("query failure was not fail-closed");
    return 0;
}
} // namespace

int main()
{
    if (const int result = test_dispatch_and_marker()) return result;
    if (const int result = test_relay_abi()) return result;
    if (const int result = test_patch_transaction()) return result;
    std::cout << "piano_page_selection_guard_selftest: ok\n";
    return 0;
}
