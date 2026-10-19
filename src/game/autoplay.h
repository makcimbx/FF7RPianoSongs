#pragma once
#include "game/autoplay_policy.h"
#include "core/hooks.h"
#include <Windows.h>

namespace ff7r::piano::game {
struct HookInstallContext;
bool install_autoplay_hooks(const HookInstallContext& context);
core::HookShutdownResult shutdown_autoplay();
// Drain the enclosing native tick before tearing down any services it can call.
bool quiesce_autoplay_controller_ticks();
void configure_autoplay_input(int function_key) noexcept;
int autoplay_function_key() noexcept;
std::uint64_t autoplay_seated_token() noexcept;
bool autoplay_toggle(std::uint64_t token, bool& enabled) noexcept;
bool autoplay_assisted() noexcept;
// Native callback only: refresh from the current synchronous controller-tick scope.
void autoplay_observe_native_phase() noexcept;
void autoplay_revoke_controller(void* controller) noexcept;

class AutoplayChartScope {
public:
    AutoplayChartScope(void* chart, void* left, void* right, bool admitted) noexcept;
    ~AutoplayChartScope();
private:
    void* previous_chart_ = nullptr;
    std::uint64_t previous_play_ = 0;
};
}
