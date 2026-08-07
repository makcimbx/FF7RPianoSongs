#include "game/runtime_locator_specs.h"

#include <array>

namespace ff7r::piano::game {
namespace {

#include "game/generated/runtime_locator_specs.generated.inc"

} // namespace

const RuntimeLocatorSpec* find_runtime_locator_spec(std::string_view id) noexcept
{
    for (const auto& spec : kRuntimeLocatorSpecs) {
        if (spec.id == id) {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace ff7r::piano::game
