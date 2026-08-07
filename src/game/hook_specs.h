#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <windows.h>

namespace ff7r::piano::game {

struct HookSpec {
    std::string_view name;
    uintptr_t rva = 0;
    std::vector<uint8_t> expected_prologue;
    bool required_for_release_startup = true;
};

struct RvaSignatureSpec {
    std::string_view id;
    uintptr_t rva = 0;
    std::vector<uint8_t> expected_prologue;
};

const HookSpec* find_hook_spec(std::string_view name);
const RvaSignatureSpec* find_rva_signature(std::string_view id);
const std::vector<HookSpec>& release_hook_specs();
bool validate_release_hook_specs(HMODULE exe_module);

} // namespace ff7r::piano::game
