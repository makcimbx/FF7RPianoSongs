#include "game/extended_chart.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "game/extended_chart_runtime_specs.h"
#include "game/hook_specs.h"
#include "pipeline/pipeline_limits.h"

#include <sstream>
#include <vector>

namespace ff7r::piano::game {
namespace {

bool validate_shipping_helpers(HMODULE exe_module, std::string& mismatch)
{
    if (!exe_module) {
        mismatch = "missing_executable_module";
        return false;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(exe_module);
    for (const ExtendedChartCanonicalSpec& canonical : kExtendedChartCanonicalSpecs) {
        const RvaSignatureSpec* spec = find_rva_signature(canonical.signature_id);
        if (!spec || spec->rva != canonical.rva || spec->expected_prologue.empty()
            || !core::bytes_equal(reinterpret_cast<const uint8_t*>(base + spec->rva),
                spec->expected_prologue)) {
            mismatch = std::string(canonical.signature_id);
            return false;
        }
    }
    return true;
}

} // namespace

ExtendedChartSupport configure_extended_chart_experiment(HMODULE exe_module, bool requested)
{
    ExtendedChartSupport support;
    support.requested = requested;
    if (!requested) {
        ff7rp::pipeline::configure_chart_row_limit(false, false);
        core::log(core::LogLevel::Info,
            "[extended_chart] status=disabled effective_row_limit=512");
        return support;
    }

    std::string mismatch;
    support.shipping_helpers_valid = validate_shipping_helpers(exe_module, mismatch);

    // The event constructor ABI is recovered, but generated tail field access,
    // exception-equivalent rollback, final link rebasing, and UI/completion
    // consumption are not proven. Publishing owned events remains unsafe.
    support.diagnostic_input_available = support.shipping_helpers_valid;
    support.mutation_available = false;
    ff7rp::pipeline::configure_chart_row_limit(true, support.diagnostic_input_available);
    support.policy_generation = ff7rp::pipeline::chart_row_policy_generation();

    std::ostringstream out;
    out << "[extended_chart] status=diagnostic_only"
        << " shipping_helpers_valid=" << support.shipping_helpers_valid
        << " diagnostic_input_available=" << support.diagnostic_input_available
        << " mutation_available=0 effective_row_limit="
        << ff7rp::pipeline::effective_chart_row_limit()
        << " accepted_input_limit=" << ff7rp::pipeline::chart_input_row_limit()
        << " policy_generation=" << support.policy_generation
        << " blocker=" << (support.shipping_helpers_valid
            ? "tail_mapping_rollback_links_ui_unproven" : "shipping_helper_prologue_mismatch");
    if (!mismatch.empty()) {
        out << " mismatch=" << mismatch;
    }
    core::log(support.shipping_helpers_valid ? core::LogLevel::Info : core::LogLevel::Error, out.str());
    return support;
}

} // namespace ff7r::piano::game
