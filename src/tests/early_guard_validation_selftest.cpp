#include "game/hook_specs.h"
#include "core/pe_image.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace ff7r::piano;
void require(bool value) { if (!value) std::abort(); }

int main()
{
    const auto* guard = game::find_hook_spec("piano_page_selected_resolve_call");
    require(guard && !guard->required_for_release_startup && guard->expected_prologue.size() >= 5);
    std::size_t extent = guard->rva + guard->expected_prologue.size();
    const game::HookSpec* required = nullptr;
    for (const auto& spec : game::release_hook_specs()) if (spec.required_for_release_startup) {
        extent = std::max(extent, spec.rva + spec.expected_prologue.size());
        required = &spec;
    }
    require(required != nullptr);
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, extent,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    require(image != nullptr);
    for (const auto& spec : game::release_hook_specs()) if (spec.required_for_release_startup)
        std::memcpy(image + spec.rva, spec.expected_prologue.data(), spec.expected_prologue.size());
    std::memcpy(image + guard->rva, guard->expected_prologue.data(), guard->expected_prologue.size());
    require(game::validate_release_hook_specs(reinterpret_cast<HMODULE>(image)));
    // Model only the owner's five-byte CALL publication, not an executable live patch.
    std::memset(image + guard->rva, 0xcc, 5);
    require(!core::bytes_equal(image + guard->rva, guard->expected_prologue));
    require(game::validate_release_hook_specs(reinterpret_cast<HMODULE>(image)));
    image[required->rva] ^= 0xff;
    require(!game::validate_release_hook_specs(reinterpret_cast<HMODULE>(image)));
    require(VirtualFree(image, 0, MEM_RELEASE) != 0);
    std::cout << "early_guard_validation_selftest passed\n";
}
