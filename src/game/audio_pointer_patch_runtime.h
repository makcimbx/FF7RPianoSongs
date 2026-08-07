#pragma once

#include "core/logging.h"
#include "core/pe_image.h"
#include "game/audio_cleanup_policy.h"

#include <cstdint>
#include <mutex>
#include <sstream>
#include <vector>

namespace ff7r::piano::game::audio_sead_detail {

using AudioFieldPatch = AudioPatchRestoreField;

class PointerPatchRollbackState {
public:
    bool try_patch(void* object, uintptr_t field_offset, uintptr_t replacement)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ || !object || replacement == 0) {
            return false;
        }

        uintptr_t original = 0;
        if (!core::safe_read_field(object, field_offset, original)) {
            return false;
        }
        if (!core::safe_write_field(object, field_offset, replacement)) {
            return false;
        }

        uintptr_t verify = 0;
        if (!core::safe_read_field(object, field_offset, verify) || verify != replacement) {
            (void)core::safe_write_field(object, field_offset, original);
            return false;
        }

        active_ = true;
        object_ = reinterpret_cast<uintptr_t>(object);
        field_address_ = object_ + field_offset;
        original_ = original;
        replacement_ = replacement;
        return true;
    }

    bool rollback(const char* reason, bool retain_record = false)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) {
            return true;
        }

        uintptr_t current = 0;
        const bool read_ok = core::safe_read_field(reinterpret_cast<void*>(field_address_), 0, current);
        bool write_ok = false;
        bool verify_ok = false;
        if (read_ok && current == replacement_) {
            write_ok = core::safe_write_field(reinterpret_cast<void*>(field_address_), 0, original_);
            uintptr_t verify = 0;
            verify_ok = write_ok && core::safe_read_field(reinterpret_cast<void*>(field_address_), 0, verify) && verify == original_;
        } else {
            verify_ok = read_ok && current == original_;
        }

        std::ostringstream out;
        out << "[audio_sead_rollback] reason=" << (reason ? reason : "?")
            << " status=" << (verify_ok ? "ok" : "failed")
            << " read_ok=" << (read_ok ? 1 : 0)
            << " write_ok=" << (write_ok ? 1 : 0)
            << " object=0x" << std::hex << object_
            << " field=0x" << field_address_
            << " current=0x" << current
            << " original=0x" << original_
            << " replacement=0x" << replacement_;
        core::log(verify_ok ? core::LogLevel::Info : core::LogLevel::Error, out.str());
        if (verify_ok && !retain_record) {
            active_ = false;
            object_ = 0;
            field_address_ = 0;
            original_ = 0;
            replacement_ = 0;
        }
        return verify_ok;
    }

    void commit_verified_rollback()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        object_ = 0;
        field_address_ = 0;
        original_ = 0;
        replacement_ = 0;
    }

    bool active() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

private:
    mutable std::mutex mutex_;
    bool active_ = false;
    uintptr_t object_ = 0;
    uintptr_t field_address_ = 0;
    uintptr_t original_ = 0;
    uintptr_t replacement_ = 0;
};

bool read_field_u64(void* object, uintptr_t offset, uint64_t& out) noexcept;
bool read_field_u32_as_u64(void* object, uintptr_t offset, uint64_t& out) noexcept;
bool write_field_patch(const AudioFieldPatch& patch) noexcept;
bool verify_field_value(const AudioFieldPatch& patch, uint64_t expected) noexcept;
bool append_patch(std::vector<AudioFieldPatch>& patches, void* object,
    uintptr_t offset, uint64_t replacement, size_t size, const char* label,
    bool redact_values_in_report = false);
bool read_field_patch_for_restore(
    const AudioPatchRestoreField& patch, uint64_t& current, void* context);
bool write_field_patch_for_restore(
    const AudioPatchRestoreField& patch, uint64_t value, void* context);
bool restore_patches_reverse(
    const std::vector<AudioFieldPatch>& patches,
    bool allow_native_changes = false,
    AudioPatchRestoreFailureReport* report = nullptr,
    AudioPatchRestoreExactOverride* exact_override = nullptr);

} // namespace ff7r::piano::game::audio_sead_detail
