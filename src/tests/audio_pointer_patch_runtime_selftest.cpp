#include "game/audio_pointer_patch_runtime.h"

#include "core/logging.h"
#include "tests/test_support.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace ff7r::piano::game;
namespace detail = ff7r::piano::game::audio_sead_detail;

void require(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "audio_pointer_patch_runtime_selftest: " << message << '\n';
        std::exit(1);
    }
}

struct Fields {
    std::uint64_t wide = 0;
    std::uint32_t narrow = 0;
    std::uint32_t padding = 0;
};

void test_field_patch_adapters()
{
    Fields fields{0x1111222233334444ull, 0x55667788u, 0};
    std::uint64_t value = 0;
    require(detail::read_field_u64(&fields, offsetof(Fields, wide), value) &&
        value == fields.wide, "64-bit field read changed");
    require(detail::read_field_u32_as_u64(&fields, offsetof(Fields, narrow), value) &&
        value == fields.narrow, "32-bit field read changed");

    std::vector<detail::AudioFieldPatch> patches;
    require(detail::append_patch(patches, &fields, offsetof(Fields, wide),
        0xaaaabbbbccccddddull, sizeof(std::uint64_t), "wide", true),
        "64-bit patch capture failed");
    require(detail::append_patch(patches, &fields, offsetof(Fields, narrow),
        0x12345678u, sizeof(std::uint32_t), "narrow"),
        "32-bit patch capture failed");
    require(patches.size() == 2 && patches[0].original == fields.wide &&
        patches[0].redact_values_in_report && patches[1].original == fields.narrow,
        "patch capture facts changed");
    for (const auto& patch : patches) {
        require(detail::write_field_patch(patch), "patch write failed");
        require(detail::verify_field_value(patch, patch.replacement),
            "patch post-write verification failed");
    }
    require(fields.wide == patches[0].replacement &&
        fields.narrow == static_cast<std::uint32_t>(patches[1].replacement),
        "patch values were not written exactly");
    require(detail::restore_patches_reverse(patches), "reverse restore failed");
    require(fields.wide == patches[0].original && fields.narrow == patches[1].original,
        "reverse restore values changed");

    detail::AudioFieldPatch rejected{
        &fields, offsetof(Fields, wide), fields.wide, 1, 0, "zero_size", false};
    require(!detail::write_field_patch(rejected), "zero-size patch was not rejected");

    fields.wide = 0x9999;
    AudioPatchRestoreFailureReport conflict_report{};
    require(!detail::restore_patches_reverse({patches[0]}, false, &conflict_report) &&
        conflict_report.stage == AudioPatchRestoreFailureStage::ConflictDecision &&
        conflict_report.category == AudioPatchRestoreFailureCategory::Conflict &&
        fields.wide == 0x9999, "conflicting native value was not rejected");
    AudioPatchRestoreFailureReport native_report{};
    require(detail::restore_patches_reverse({patches[0]}, true, &native_report) &&
        native_report.preserve_native_value_count == 1 && fields.wide == 0x9999,
        "allowed native value was not preserved");
}

void test_reverse_restore_transaction_rollback()
{
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    void* page = VirtualAlloc(nullptr, system_info.dwPageSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    require(page != nullptr, "read-only rollback page allocation failed");
    auto* read_only = static_cast<std::uint64_t*>(page);
    *read_only = 0x2222;
    std::uint64_t writable = 0x4444;
    const detail::AudioFieldPatch read_only_patch{
        read_only, 0, 0x1111, 0x2222, sizeof(std::uint64_t), "read_only", false};
    const detail::AudioFieldPatch writable_patch{
        &writable, 0, 0x3333, 0x4444, sizeof(std::uint64_t), "writable", false};
    DWORD old_protection = 0;
    require(VirtualProtect(page, system_info.dwPageSize, PAGE_READONLY, &old_protection) != FALSE,
        "read-only rollback page protection failed");
    AudioPatchRestoreFailureReport report{};
    const bool restored = detail::restore_patches_reverse(
        {read_only_patch, writable_patch}, false, &report);
    require(!restored && report.stage == AudioPatchRestoreFailureStage::RestoreWrite &&
        std::string(report.label) == "read_only", "later restore failure projection changed");
    require(writable == 0x4444 && *read_only == 0x2222,
        "failed restore did not roll earlier writes back to inspected values");
    DWORD ignored = 0;
    require(VirtualProtect(page, system_info.dwPageSize, old_protection, &ignored) != FALSE,
        "rollback page protection restore failed");
    require(VirtualFree(page, 0, MEM_RELEASE) != FALSE, "rollback page release failed");
}

void test_pointer_rollback_holder(const std::filesystem::path& log_path)
{
    ff7r::piano::core::set_log_path(log_path.wstring());
    ff7r::piano::core::set_log_level(ff7r::piano::core::LogLevel::Debug);
    Fields fields{0x1010, 0, 0};
    detail::PointerPatchRollbackState state;
    require(!state.try_patch(&fields, offsetof(Fields, wide), 0),
        "rollback holder accepted invalid patch");
    require(state.try_patch(&fields, offsetof(Fields, wide), 0x2020) &&
        state.active() && fields.wide == 0x2020,
        "rollback holder did not install exact patch");
    require(!state.try_patch(&fields, offsetof(Fields, wide), 0x3030),
        "rollback holder accepted a second active patch");
    require(state.rollback("retained", true) && state.active() && fields.wide == 0x1010,
        "retained rollback changed ownership behavior");
    state.commit_verified_rollback();
    require(!state.active(), "verified rollback commit did not clear record");

    require(state.try_patch(&fields, offsetof(Fields, wide), 0x3030),
        "rollback overwrite setup failed");
    fields.wide = 0x4040;
    require(!state.rollback("native_overwrite") && state.active() && fields.wide == 0x4040,
        "native overwrite mismatch did not retain rollback record");
    fields.wide = 0x3030;
    require(state.rollback("retry") && !state.active() && fields.wide == 0x1010,
        "rollback retry did not restore and clear record");

    require(state.try_patch(&fields, offsetof(Fields, wide), 0x5050),
        "already-restored setup failed");
    fields.wide = 0x1010;
    require(state.rollback("already_restored") && !state.active() && fields.wide == 0x1010,
        "already-restored rollback behavior changed");

    std::ifstream log_file(log_path);
    const std::string log_text{
        std::istreambuf_iterator<char>(log_file), std::istreambuf_iterator<char>()};
    require(log_text.find("[audio_sead_rollback] reason=retained status=ok") != std::string::npos &&
        log_text.find("reason=native_overwrite status=failed") != std::string::npos &&
        log_text.find("reason=retry status=ok") != std::string::npos &&
        log_text.find("reason=already_restored status=ok") != std::string::npos,
        "rollback holder log projection changed");
}

} // namespace

int main()
{
    try {
        ff7rp::tests::TemporaryDirectory temporary("ff7rp-audio-pointer-patch-runtime");
        test_field_patch_adapters();
        test_reverse_restore_transaction_rollback();
        test_pointer_rollback_holder(temporary.path() / "pointer-patch.log");
    } catch (const std::exception& ex) {
        std::cerr << "audio_pointer_patch_runtime_selftest: " << ex.what() << '\n';
        return 1;
    }
    std::cout << "audio_pointer_patch_runtime_selftest: ok\n";
    return 0;
}
