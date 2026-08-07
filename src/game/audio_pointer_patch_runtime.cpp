#include "game/audio_pointer_patch_runtime.h"

namespace ff7r::piano::game::audio_sead_detail {

bool read_field_u64(void* object, uintptr_t offset, uint64_t& out) noexcept
{
    return core::safe_read_field(object, offset, out);
}

bool read_field_u32_as_u64(void* object, uintptr_t offset, uint64_t& out) noexcept
{
    uint32_t value = 0;
    if (!core::safe_read_field(object, offset, value)) {
        return false;
    }
    out = value;
    return true;
}

bool write_field_patch(const AudioFieldPatch& patch) noexcept
{
    if (!patch.object || patch.size == 0) {
        return false;
    }
    if (patch.size == sizeof(uint32_t)) {
        const auto value = static_cast<uint32_t>(patch.replacement);
        return core::safe_write_field(patch.object, patch.offset, value);
    }
    return core::safe_write_field(patch.object, patch.offset, patch.replacement);
}

bool verify_field_value(const AudioFieldPatch& patch, uint64_t expected) noexcept
{
    uint64_t current = 0;
    const bool read_ok = patch.size == sizeof(uint32_t)
        ? read_field_u32_as_u64(patch.object, patch.offset, current)
        : read_field_u64(patch.object, patch.offset, current);
    return read_ok && current == expected;
}

bool append_patch(std::vector<AudioFieldPatch>& patches, void* object,
    uintptr_t offset, uint64_t replacement, size_t size, const char* label,
    bool redact_values_in_report)
{
    uint64_t original = 0;
    const bool read_ok = size == sizeof(uint32_t)
        ? read_field_u32_as_u64(object, offset, original)
        : read_field_u64(object, offset, original);
    if (!read_ok) {
        return false;
    }
    patches.push_back(AudioFieldPatch{object, offset, original, replacement,
        size, label, redact_values_in_report});
    return true;
}

bool read_field_patch_for_restore(
    const AudioPatchRestoreField& patch, uint64_t& current, void*)
{
    return patch.size == sizeof(uint32_t)
        ? read_field_u32_as_u64(patch.object, patch.offset, current)
        : read_field_u64(patch.object, patch.offset, current);
}

bool write_field_patch_for_restore(
    const AudioPatchRestoreField& patch, const uint64_t value, void*)
{
    AudioFieldPatch write = patch;
    write.replacement = value;
    return write_field_patch(write);
}

bool restore_patches_reverse(
    const std::vector<AudioFieldPatch>& patches,
    bool allow_native_changes,
    AudioPatchRestoreFailureReport* report,
    AudioPatchRestoreExactOverride* exact_override)
{
    const AudioPatchRestoreAccess access{
        read_field_patch_for_restore,
        write_field_patch_for_restore,
        nullptr,
        nullptr,
    };
    return restore_audio_patches_reverse(
        patches, allow_native_changes, access, report, exact_override);
}

} // namespace ff7r::piano::game::audio_sead_detail
