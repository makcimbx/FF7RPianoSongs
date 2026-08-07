#pragma once
#include <cstddef>
#include <functional>
#include <span>
namespace ff7r::piano::game {
enum class MandatoryHookTransactionResult { Installed, RolledBack, Retained };
struct MandatoryHookOperation { std::function<bool()> install, disable, remove; };
inline MandatoryHookTransactionResult install_mandatory_hook_transaction(
    std::span<MandatoryHookOperation> operations,
    std::size_t* failed_step = nullptr) noexcept {
    if (failed_step) *failed_step = operations.size();
    std::size_t installed = 0, attempted = 0;
    try { for (; installed < operations.size(); ++installed) {
        attempted = installed + 1;
        if (!operations[installed].install()) break;
    } } catch (...) {}
    if (installed == operations.size()) return MandatoryHookTransactionResult::Installed;
    if (failed_step) *failed_step = attempted - 1;
    bool disabled = true;
    for (std::size_t i = attempted; i; --i) try {
        const bool result = operations[i - 1].disable(); disabled = result && disabled;
    } catch (...) { disabled = false; }
    if (!disabled) return MandatoryHookTransactionResult::Retained;
    bool removed = true;
    for (std::size_t i = attempted; i; --i) try {
        const bool result = operations[i - 1].remove(); removed = result && removed;
    } catch (...) { removed = false; }
    return removed ? MandatoryHookTransactionResult::RolledBack
                   : MandatoryHookTransactionResult::Retained;
}
}
