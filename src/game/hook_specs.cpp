#include "game/hook_specs.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "game/rvas.h"

#include <algorithm>
#include <sstream>

namespace ff7r::piano::game {
namespace {

const std::vector<HookSpec> kSpecs = {
#include "game/generated/hook_specs.generated.inc"
};

const std::vector<RvaSignatureSpec> kRvaSignatures = {
#include "game/generated/rva_signatures.generated.inc"
};

} // namespace

const HookSpec* find_hook_spec(std::string_view name)
{
    const auto it = std::find_if(kSpecs.begin(), kSpecs.end(), [name](const HookSpec& spec) {
        return spec.name == name;
    });
    return it == kSpecs.end() ? nullptr : &*it;
}

const RvaSignatureSpec* find_rva_signature(std::string_view id)
{
    const auto it = std::find_if(kRvaSignatures.begin(), kRvaSignatures.end(), [id](const RvaSignatureSpec& spec) {
        return spec.id == id;
    });
    return it == kRvaSignatures.end() ? nullptr : &*it;
}

const std::vector<HookSpec>& release_hook_specs()
{
    return kSpecs;
}

bool validate_release_hook_specs(HMODULE exe_module)
{
    bool ok = true;
    size_t checked = 0;
    size_t skipped = 0;
    for (const HookSpec& spec : kSpecs) {
        if (!spec.required_for_release_startup) {
            ++skipped;
            continue;
        }
        ++checked;
        const auto* address = reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(exe_module) + spec.rva);
        if (!core::bytes_equal(address, spec.expected_prologue)) {
            std::ostringstream out;
            out << "[hook_specs] status=prologue_mismatch name=" << spec.name << " rva=0x" << std::hex << spec.rva;
            core::log(core::LogLevel::Error, out.str());
            ok = false;
        }
    }
    std::ostringstream out;
    out << "[hook_specs] status=" << (ok ? "validated" : "validation_failed")
        << " required=" << checked
        << " skipped_probe_only=" << skipped
        << " total=" << kSpecs.size();
    core::log(core::LogLevel::Info, out.str());
    return ok;
}

} // namespace ff7r::piano::game
