#include <windows.h>

#include "core/pe_image.h"
#include "core/version_gate.h"
#include "game/hook_specs.h"
#include "pipeline/oodle_decoder.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

namespace {
using namespace ff7r::piano;
std::vector<std::uint8_t> memory(0x5000);
HMODULE fixture_executable = reinterpret_cast<HMODULE>(memory.data());
core::PeIdentity fixture_identity{};
core::ImageRange fixture_image{};
game::RvaSignatureSpec fixture_signature{};
std::array<game::RvaSignatureSpec, 3> callback_signatures{};
constexpr std::array<std::uintptr_t, 6> default_data_slots{0x3000, 0x3008, 0x3018, 0x3030, 0x3038, 0x3040};
std::array<std::uintptr_t, 6> data_slots = default_data_slots;
MEMORY_BASIC_INFORMATION mapping{};
MEMORY_BASIC_INFORMATION data_mapping{};
MEMORY_BASIC_INFORMATION mapping_override{};
std::uintptr_t mapping_override_at = 0;
SIZE_T query_bytes = sizeof(mapping);
bool query_ok = true;
unsigned code_reads = 0;
unsigned data_reads = 0;
std::array<unsigned, 4> code_passes{};
std::array<unsigned, 6> data_passes{};
std::function<void(std::uintptr_t, unsigned)> on_code;
std::function<void(std::uintptr_t, unsigned)> on_copy;
std::uintptr_t copy_failure = 0;
unsigned copy_failure_pass = 0;
unsigned decoder_calls = 0;

void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}

HMODULE WINAPI fixture_module(LPCWSTR name)
{
    check(name == nullptr, "Decoder searched for another module");
    return fixture_executable;
}

SIZE_T WINAPI fixture_query(LPCVOID address, PMEMORY_BASIC_INFORMATION output, SIZE_T size)
{
    check(size == sizeof(mapping), "Unexpected mapping query size");
    if (!query_ok) return 0;
    const auto offset = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(memory.data());
    const bool data = std::find(data_slots.begin(), data_slots.end(), offset) != data_slots.end();
    *output = offset == mapping_override_at ? mapping_override : (data ? data_mapping : mapping);
    return query_bytes;
}

std::intptr_t __stdcall unused_decoder(const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t,
    std::uint32_t, std::uint32_t, std::uint32_t, std::uint64_t, std::size_t,
    std::uint64_t, std::uint64_t, std::uint8_t*, std::size_t, std::uint32_t)
{
    ++decoder_calls;
    return 0;
}

void reset()
{
    std::fill(memory.begin(), memory.end(), std::uint8_t{0});
    data_slots = default_data_slots;
    fixture_executable = reinterpret_cast<HMODULE>(memory.data());
    fixture_identity = {core::kExpectedExeTimestamp, core::kExpectedExeSizeOfImage, core::kExpectedExeChecksum};
    fixture_image = {memory.data(), fixture_identity.size_of_image};
    fixture_signature = {"native_oodle_decompress", 0x2000, {0x3e,0x40,0x55,0x53,0x56,0x57,
        0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xf8,0,0,0,
        0x48,0x8d,0x6c,0x24,0x60}};
    std::copy(fixture_signature.expected_prologue.begin(), fixture_signature.expected_prologue.end(), memory.begin() + 0x2000);
    callback_signatures = {{{"native_oodle_default_allocate", 0x2100, {0x48, 0x89, 0x5c, 0x24, 0x08}},
        {"native_oodle_default_free", 0x2200, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20}},
        {"native_oodle_default_diagnostic", 0x2300, {0x83, 0xf9, 0x02, 0x7d, 0x67}}}};
    for (std::size_t i = 0; i < callback_signatures.size(); ++i) {
        const auto& signature = callback_signatures[i];
        std::copy(signature.expected_prologue.begin(), signature.expected_prologue.end(), memory.begin() + signature.rva);
        const auto target = reinterpret_cast<std::uintptr_t>(memory.data()) + signature.rva;
        std::memcpy(memory.data() + data_slots[i], &target, sizeof(target));
    }
    const std::int32_t threshold = 32768;
    std::memcpy(memory.data() + data_slots[5], &threshold, sizeof(threshold));
    mapping = {};
    mapping.BaseAddress = memory.data(); mapping.AllocationBase = fixture_executable;
    mapping.RegionSize = memory.size(); mapping.State = MEM_COMMIT;
    mapping.Type = MEM_IMAGE; mapping.Protect = PAGE_EXECUTE_READ;
    data_mapping = mapping; data_mapping.BaseAddress = memory.data() + 0x3000;
    data_mapping.RegionSize = 0x1000; data_mapping.Protect = PAGE_READWRITE;
    mapping_override = {}; mapping_override_at = 0; query_bytes = sizeof(mapping);
    query_ok = true; code_reads = 0; data_reads = 0; code_passes = {}; data_passes = {};
    on_code = {}; on_copy = {}; copy_failure = 0; copy_failure_pass = 0;
}
} // namespace

#define GetModuleHandleW fixture_module
#define VirtualQuery fixture_query
#include "../game/native_audio_decoder.cpp"
#undef VirtualQuery
#undef GetModuleHandleW

namespace {
game::DecoderDefaults fixture_defaults()
{
    return {{{{&callback_signatures[0], 0x2100, data_slots[0]},
        {&callback_signatures[1], 0x2200, data_slots[1]},
        {&callback_signatures[2], 0x2300, data_slots[2]}}}, {data_slots[3], data_slots[4]}, data_slots[5]};
}

void put_pointer(std::uintptr_t offset, std::uintptr_t value)
{
    std::memcpy(memory.data() + offset, &value, sizeof(value));
}
} // namespace

namespace ff7r::piano::core {
PeIdentity pe_identity(HMODULE) { return fixture_identity; }
ImageRange image_range(HMODULE) { return fixture_image; }
bool bytes_equal(const std::uint8_t* address, const std::vector<std::uint8_t>& expected)
{
    ++code_reads;
    check(address >= memory.data() && address + expected.size() <= memory.data() + memory.size(),
        "Unbounded decoder code read");
    const auto offset = static_cast<std::uintptr_t>(address - memory.data());
    std::size_t index = 0;
    if (offset != fixture_signature.rva) {
        const auto found = std::find_if(callback_signatures.begin(), callback_signatures.end(),
            [offset](const auto& signature) { return signature.rva == offset; });
        check(found != callback_signatures.end(), "Unexpected code target");
        index = static_cast<std::size_t>(found - callback_signatures.begin()) + 1;
    }
    const auto pass = ++code_passes[index];
    if (on_code) on_code(offset, pass);
    return std::memcmp(address, expected.data(), expected.size()) == 0;
}
bool safe_copy_bytes(const void* source, void* destination, std::size_t bytes)
{
    const auto offset = reinterpret_cast<std::uintptr_t>(source) - reinterpret_cast<std::uintptr_t>(memory.data());
    const auto found = std::find(data_slots.begin(), data_slots.end(), offset);
    check(found != data_slots.end() && destination != nullptr, "Unexpected data read");
    const auto index = static_cast<std::size_t>(found - data_slots.begin());
    check(bytes == (index == 5 ? 4u : 8u), "Unbounded data-slot read");
    ++data_reads;
    const auto pass = ++data_passes[index];
    if (on_copy) on_copy(offset, pass);
    if (offset == copy_failure && (!copy_failure_pass || pass == copy_failure_pass)) return false;
    std::memcpy(destination, source, bytes);
    return true;
}
} // namespace ff7r::piano::core

namespace ff7r::piano::game {
const RvaSignatureSpec* find_rva_signature(std::string_view id)
{
    if (id == fixture_signature.id) return &fixture_signature;
    for (const auto& signature : callback_signatures) if (signature.id == id) return &signature;
    return nullptr;
}
} // namespace ff7r::piano::game

int main()
{
    using namespace ff7r::piano;
    namespace rva = game::rva;
    using ff7rp::pipeline::OodleDecoder;
    reset();
    OodleDecoder decoder;
    check(game::prepare_decoder_from_profile(fixture_executable, &fixture_signature, fixture_signature.rva,
        fixture_defaults(), decoder).ok() && decoder.available() && code_reads == 8 && data_reads == 12
        && !decoder_calls, "Qualified default game decoder was not bound");
    auto retained = decoder;
    const auto reject = [&](const char* message, const game::DecoderDefaults* profile = nullptr) {
        check(OodleDecoder::from_verified_function(unused_decoder, &decoder).ok(), "Stale decoder setup failed");
        check(!game::prepare_decoder_from_profile(fixture_executable, &fixture_signature, 0x2000,
            profile ? *profile : fixture_defaults(), decoder).ok()
            && !decoder.available() && retained.available() && !decoder_calls, message);
    };
    for (unsigned field = 0; field != 3; ++field) {
        reset();
        if (field == 0) ++fixture_identity.timestamp;
        if (field == 1) ++fixture_identity.size_of_image;
        if (field == 2) ++fixture_identity.checksum;
        reject("Changed executable identity accepted");
        check(!code_reads && !data_reads, "Read before image authentication");
    }
    reset(); fixture_signature.rva = 0; reject("Absent decoder contract accepted");
    reset(); fixture_signature.expected_prologue.clear(); reject("Empty signature accepted");
    reset(); fixture_signature.expected_prologue.resize(65); reject("Oversized signature accepted");
    reset(); fixture_image.size = 0x2000; reject("Out-of-image decoder accepted");
    reset(); fixture_image.base = memory.data() + 1; reject("Wrong image base accepted");
    reset(); query_ok = false; reject("Unreadable mapping accepted");
    reset(); --query_bytes; reject("Partial mapping query accepted");
    for (const DWORD protection : std::array<DWORD, 4>{PAGE_READONLY, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY,
            PAGE_EXECUTE_READ | PAGE_GUARD}) {
        reset(); mapping.Protect = protection; reject("Unsupported code protection accepted");
    }
    reset(); mapping.Type = MEM_PRIVATE; reject("Private decoder code accepted");
    reset(); mapping.State = MEM_RESERVE; reject("Uncommitted code accepted");
    reset(); mapping.AllocationBase = nullptr; reject("Wrong code allocation accepted");
    reset(); mapping.BaseAddress = memory.data() + 0x2001; reject("Mapping begins after code");
    reset(); mapping.RegionSize = 0x2001; reject("Code crosses mapped range");
    reset(); memory[0x2000] ^= 1; reject("Changed decoder code accepted");
    reset();
    check(!game::prepare_decoder_from_profile(nullptr, &fixture_signature, 0x2000, fixture_defaults(), decoder).ok()
        && !decoder.available(), "Null executable accepted");
    check(!game::prepare_decoder_from_profile(reinterpret_cast<HMODULE>(memory.data() + 1),
        &fixture_signature, 0x2000, fixture_defaults(), decoder).ok() && !decoder.available(), "Non-main executable accepted");
    check(!game::prepare_decoder_from_profile(fixture_executable, nullptr, 0x2000, fixture_defaults(), decoder).ok(),
        "Missing catalog signature accepted");
    check(!game::prepare_decoder_from_profile(fixture_executable, &fixture_signature, 0, fixture_defaults(), decoder).ok(),
        "Zero function RVA accepted");
    check(OodleDecoder::from_verified_function(unused_decoder, &decoder).ok(), "Trusted factory fixture failed");
    reset(); fixture_signature.rva = 0x1ff0;
    reject("Mismatched catalog function accepted");
    for (std::size_t i = 0; i < callback_signatures.size(); ++i) {
        reset(); auto missing = fixture_defaults(); missing.callbacks[i].signature = nullptr;
        reject("Missing callback signature accepted", &missing);
        reset(); auto absent = fixture_defaults(); absent.callbacks[i].target_rva = 0;
        reject("Missing callback RVA accepted", &absent);
        reset(); ++callback_signatures[i].rva; reject("Mismatched callback signature RVA accepted");
        reset(); callback_signatures[i].expected_prologue.clear(); reject("Empty callback signature accepted");
        reset(); callback_signatures[i].expected_prologue.resize(65); reject("Oversized callback signature accepted");
        reset(); memory[callback_signatures[i].rva] ^= 1; reject("Changed callback code accepted");
        reset(); put_pointer(data_slots[i], 0); reject("Null default callback accepted");
        reset(); put_pointer(data_slots[i], reinterpret_cast<std::uintptr_t>(unused_decoder));
        reject("Custom callback accepted");
        for (unsigned defect = 0; defect < 8; ++defect) {
            reset(); mapping_override_at = callback_signatures[i].rva; mapping_override = mapping;
            if (defect == 0) mapping_override.Protect = PAGE_EXECUTE_READWRITE;
            if (defect == 1) mapping_override.Protect = PAGE_READONLY;
            if (defect == 2) mapping_override.Type = MEM_PRIVATE;
            if (defect == 3) mapping_override.State = MEM_RESERVE;
            if (defect == 4) mapping_override.AllocationBase = nullptr;
            if (defect == 5) mapping_override.BaseAddress = memory.data() + mapping_override_at + 1;
            if (defect == 6) mapping_override.RegionSize = mapping_override_at + 1;
            if (defect == 7) mapping_override.Protect |= PAGE_GUARD;
            reject("Callback mapping mismatch accepted");
        }
        reset(); on_code = [i](std::uintptr_t offset, unsigned pass) {
            if (offset == callback_signatures[i].rva && pass == 2) memory[offset] ^= 1;
        };
        reject("Callback code reread drift accepted");
    }
    const auto set_slot = [](game::DecoderDefaults& profile, std::size_t index, std::uintptr_t value) {
        if (index < 3) profile.callbacks[index].slot_rva = value;
        else if (index < 5) profile.profiling_slots[index - 3] = value;
        else profile.stack_threshold = value;
    };
    for (std::size_t i = 0; i < data_slots.size(); ++i) {
        for (const auto offset : {std::uintptr_t{0}, data_slots[i] + 1,
                std::uintptr_t{core::kExpectedExeSizeOfImage}, std::numeric_limits<std::uintptr_t>::max()}) {
            reset(); auto profile = fixture_defaults(); set_slot(profile, i, offset);
            reject("Missing/unaligned/out-of-image data slot accepted", &profile);
        }
        if (i < 5) {
            reset(); auto profile = fixture_defaults(); set_slot(profile, i, core::kExpectedExeSizeOfImage - 4);
            reject("Data slot crossing image accepted", &profile);
        }
        for (unsigned defect = 0; defect < 9; ++defect) {
            reset(); mapping_override_at = data_slots[i]; mapping_override = data_mapping;
            if (defect == 0) mapping_override.Protect = PAGE_NOACCESS;
            if (defect == 1) mapping_override.Protect = PAGE_EXECUTE_READ;
            if (defect == 2) mapping_override.Type = MEM_PRIVATE;
            if (defect == 3) mapping_override.State = MEM_RESERVE;
            if (defect == 4) mapping_override.AllocationBase = nullptr;
            if (defect == 5) mapping_override.BaseAddress = memory.data() + data_slots[i] + 1;
            if (defect == 6) mapping_override.RegionSize = data_slots[i] - 0x3000 + 1;
            if (defect == 7) mapping_override.Protect |= PAGE_GUARD;
            if (defect == 8) mapping_override.Protect |= PAGE_NOCACHE;
            reject("Data-slot mapping mismatch accepted");
        }
        for (const unsigned pass : {1u, 2u}) {
            reset(); copy_failure = data_slots[i]; copy_failure_pass = pass;
            reject("Unreadable data slot accepted");
        }
        reset(); on_copy = [i](std::uintptr_t offset, unsigned pass) {
            if (offset == data_slots[i] && pass == 2) memory[offset] ^= 1;
        };
        reject("Data-slot reread drift accepted");
    }
    for (const auto index : {3u, 4u}) {
        reset(); put_pointer(data_slots[index], reinterpret_cast<std::uintptr_t>(unused_decoder));
        reject("Non-null profiling callback accepted");
    }
    for (const std::int32_t threshold : {std::int32_t{-1}, std::int32_t{0}, std::int32_t{32767},
            std::int32_t{32769}, std::numeric_limits<std::int32_t>::max()}) {
        reset(); std::memcpy(memory.data() + data_slots[5], &threshold, sizeof(threshold));
        reject("Unreviewed stack threshold accepted");
    }
    reset(); on_copy = [](std::uintptr_t offset, unsigned pass) {
        if (offset == data_slots[5] && pass == 1) memory[0x2000] ^= 1;
    };
    reject("Decoder code changed during slot reads accepted");
    for (const DWORD protection : {PAGE_READONLY, PAGE_READWRITE, PAGE_WRITECOPY}) {
        reset(); data_mapping.Protect = protection;
        check(game::prepare_decoder_from_profile(fixture_executable, &fixture_signature, 0x2000,
            fixture_defaults(), decoder).ok() && decoder.available() && !decoder_calls,
            "Readable default image slots rejected");
    }
    reset();
    check(!game::prepare_game_oodle_decoder(fixture_executable, decoder).ok() && !decoder.available(),
        "Public adapter accepted a signature for a different generated RVA");
    retained = {}; decoder = {};
    // Exercise the production entry point with this build's actual generated contracts,
    // copied into inert storage. None of these bytes are executed.
    const std::vector<game::RvaSignatureSpec> catalog_signatures = {
#include "game/generated/rva_signatures.generated.inc"
    };
    const auto signature_for = [&](const char* id) {
        const auto found = std::find_if(catalog_signatures.begin(), catalog_signatures.end(),
            [id](const auto& signature) { return signature.id == id; });
        check(found != catalog_signatures.end(), "Generated default signature missing");
        return *found;
    };
    fixture_signature = signature_for("native_oodle_decompress");
    callback_signatures = {signature_for("native_oodle_default_allocate"),
        signature_for("native_oodle_default_free"), signature_for("native_oodle_default_diagnostic")};
    data_slots = {game::rva::NativeOodleAllocateSlot, game::rva::NativeOodleFreeSlot,
        game::rva::NativeOodleDiagnosticSlot, game::rva::NativeOodleProfileBeginSlot,
        game::rva::NativeOodleProfileEndSlot, game::rva::NativeOodleStackThreshold};
    memory.resize(core::kExpectedExeSizeOfImage);
    fixture_executable = reinterpret_cast<HMODULE>(memory.data());
    fixture_image = {memory.data(), memory.size()};
    mapping.BaseAddress = memory.data(); mapping.AllocationBase = fixture_executable; mapping.RegionSize = memory.size();
    data_mapping = mapping; data_mapping.Protect = PAGE_READWRITE;
    std::copy(fixture_signature.expected_prologue.begin(), fixture_signature.expected_prologue.end(), memory.begin() + fixture_signature.rva);
    for (std::size_t i = 0; i < callback_signatures.size(); ++i) {
        const auto& signature = callback_signatures[i];
        std::copy(signature.expected_prologue.begin(), signature.expected_prologue.end(), memory.begin() + signature.rva);
        put_pointer(data_slots[i], reinterpret_cast<std::uintptr_t>(memory.data()) + signature.rva);
    }
    const std::int32_t threshold = 32768;
    std::memcpy(memory.data() + data_slots[5], &threshold, sizeof(threshold));
    code_reads = data_reads = 0; code_passes = {}; data_passes = {};
    check(game::prepare_game_oodle_decoder(fixture_executable, decoder).ok() && decoder.available()
        && code_reads == 8 && data_reads == 12 && !decoder_calls, "Generated producer-shaped defaults rejected");
    retained = decoder;
    for (std::size_t i = 0; i < data_slots.size(); ++i) {
        memory[data_slots[i]] ^= 1;
        check(!game::prepare_game_oodle_decoder(fixture_executable, decoder).ok() && !decoder.available()
            && retained.available() && !decoder_calls, "Generated dependency mismatch accepted");
        memory[data_slots[i]] ^= 1;
    }
    std::puts("native_audio_decoder_selftest passed (no game function invoked)");
}
