#include "game/native_audio_decoder.h"

#include "core/pe_image.h"
#include "core/version_gate.h"
#include "game/hook_specs.h"
#include "game/generated/rvas.generated.h"

#include <array>
#include <limits>

namespace ff7r::piano::game {
namespace {

struct DecoderCallback {
    const RvaSignatureSpec* signature;
    std::uintptr_t target_rva;
    std::uintptr_t slot_rva;
};

struct DecoderDefaults {
    std::array<DecoderCallback, 3> callbacks;
    std::array<std::uintptr_t, 2> profiling_slots;
    std::uintptr_t stack_threshold;
};

bool mapped_in_image(HMODULE executable, const void* address, std::size_t bytes, bool code)
{
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(address, &region, sizeof(region)) != sizeof(region)
        || region.State != MEM_COMMIT || region.Type != MEM_IMAGE || region.AllocationBase != executable
        || (code ? region.Protect != PAGE_EXECUTE_READ
            : (region.Protect != PAGE_READONLY && region.Protect != PAGE_READWRITE && region.Protect != PAGE_WRITECOPY)))
        return false;
    const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    const auto pointer = reinterpret_cast<std::uintptr_t>(address);
    return start <= pointer && bytes <= region.RegionSize && pointer - start <= region.RegionSize - bytes;
}

ff7rp::pipeline::Status prepare_decoder_from_profile(HMODULE executable,
    const RvaSignatureSpec* signature, std::uintptr_t decoder_rva,
    const DecoderDefaults& defaults,
    ff7rp::pipeline::OodleDecoder& decoder)
{
    using ff7rp::pipeline::Status;
    using ff7rp::pipeline::StatusCode;
    decoder = {};
    if (!executable || executable != GetModuleHandleW(nullptr))
        return Status::error(StatusCode::InvalidArgument, "Native decoder requires the main executable");
    const auto identity = core::pe_identity(executable);
    if (identity.timestamp != core::kExpectedExeTimestamp || identity.size_of_image != core::kExpectedExeSizeOfImage
        || identity.checksum != core::kExpectedExeChecksum)
        return Status::error(StatusCode::InvalidArgument, "Native decoder executable identity mismatch");
    if (!decoder_rva || !signature || signature->rva != decoder_rva
        || signature->expected_prologue.empty() || signature->expected_prologue.size() > 64)
        return Status::error(StatusCode::NotFound, "Native decoder is unavailable for this build");
    const auto image = core::image_range(executable);
    const auto base = reinterpret_cast<std::uintptr_t>(executable);
    if (image.base != reinterpret_cast<const std::uint8_t*>(executable)
        || image.size != identity.size_of_image
        || base > std::numeric_limits<std::uintptr_t>::max() - image.size)
        return Status::error(StatusCode::InvalidArgument, "Native decoder image bounds mismatch");
    const auto in_bounds = [&](std::uintptr_t offset, std::size_t bytes) {
        return offset && offset < image.size && bytes <= image.size - offset;
    };
    const auto code_matches = [&](const RvaSignatureSpec* spec, std::uintptr_t offset) {
        return spec && spec->rva == offset && !spec->expected_prologue.empty()
            && spec->expected_prologue.size() <= 64 && in_bounds(offset, spec->expected_prologue.size())
            && mapped_in_image(executable, image.base + offset, spec->expected_prologue.size(), true)
            && core::bytes_equal(image.base + offset, spec->expected_prologue);
    };
    const auto read_slot = [&](std::uintptr_t offset, void* output, std::size_t bytes) {
        return offset % bytes == 0 && in_bounds(offset, bytes)
            && mapped_in_image(executable, image.base + offset, bytes, false)
            && core::safe_copy_bytes(image.base + offset, output, bytes);
    };
    if (!code_matches(signature, decoder_rva))
        return Status::error(StatusCode::InvalidArgument, "Native decoder code or mapping mismatch");
    std::array<std::uintptr_t, 5> slots{};
    for (std::size_t i = 0; i < defaults.callbacks.size(); ++i) {
        const auto& callback = defaults.callbacks[i];
        if (!code_matches(callback.signature, callback.target_rva)
            || !read_slot(callback.slot_rva, &slots[i], sizeof(slots[i]))
            || slots[i] != base + callback.target_rva)
            return Status::error(StatusCode::InvalidArgument, "Native decoder default callback mismatch");
    }
    for (std::size_t i = 0; i < defaults.profiling_slots.size(); ++i) {
        if (!read_slot(defaults.profiling_slots[i], &slots[i + 3], sizeof(slots[i + 3])) || slots[i + 3])
            return Status::error(StatusCode::InvalidArgument, "Native decoder profiling callback is not null");
    }
    std::int32_t threshold{};
    if (!read_slot(defaults.stack_threshold, &threshold, sizeof(threshold)) || threshold != 32768)
        return Status::error(StatusCode::InvalidArgument, "Native decoder stack threshold mismatch");
    // Copied defaults must survive code validation; this is not a callback-lifetime lease.
    if (!code_matches(signature, decoder_rva))
        return Status::error(StatusCode::InvalidArgument, "Native decoder code changed during validation");
    for (const auto& callback : defaults.callbacks) {
        if (!code_matches(callback.signature, callback.target_rva))
            return Status::error(StatusCode::InvalidArgument, "Native decoder callback code changed during validation");
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto offset = i < 3 ? defaults.callbacks[i].slot_rva : defaults.profiling_slots[i - 3];
        std::uintptr_t current{};
        if (!read_slot(offset, &current, sizeof(current)) || current != slots[i])
            return Status::error(StatusCode::InvalidArgument, "Native decoder callback slot changed during validation");
    }
    std::int32_t current_threshold{};
    if (!read_slot(defaults.stack_threshold, &current_threshold, sizeof(current_threshold)) || current_threshold != threshold)
        return Status::error(StatusCode::InvalidArgument, "Native decoder stack threshold changed during validation");
    return ff7rp::pipeline::OodleDecoder::from_verified_function(
        reinterpret_cast<ff7rp::pipeline::detail::OodleDecompressFunction>(base + decoder_rva), &decoder);
}

} // namespace

ff7rp::pipeline::Status prepare_game_oodle_decoder(
    HMODULE executable, ff7rp::pipeline::OodleDecoder& decoder)
{
    const DecoderDefaults defaults{{{
        {find_rva_signature("native_oodle_default_allocate"), rva::NativeOodleDefaultAllocate, rva::NativeOodleAllocateSlot},
        {find_rva_signature("native_oodle_default_free"), rva::NativeOodleDefaultFree, rva::NativeOodleFreeSlot},
        {find_rva_signature("native_oodle_default_diagnostic"), rva::NativeOodleDefaultDiagnostic, rva::NativeOodleDiagnosticSlot}
    }}, {rva::NativeOodleProfileBeginSlot, rva::NativeOodleProfileEndSlot}, rva::NativeOodleStackThreshold};
    return prepare_decoder_from_profile(executable, find_rva_signature("native_oodle_decompress"),
        rva::NativeOodleDecompress, defaults, decoder);
}

} // namespace ff7r::piano::game
