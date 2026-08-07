#include "game/runtime.h"
#include "game/module_hooks.h"

#include <array>
#include <iostream>

namespace {
std::array<int, 11> installed{};
std::size_t installed_count = 0;

bool record(const int module) noexcept
{
    installed[installed_count++] = module;
    return true;
}

ff7r::piano::core::HookShutdownResult shutdown_ok() noexcept
{
    return {true, true, true, true, true, true};
}
}

namespace ff7r::piano::game {
bool validate_release_hook_specs(HMODULE) { return true; }
void initialize_uobject_identity(HMODULE) {}
void clear_uobject_identity() {}
bool install_menu_session_hooks(const HookInstallContext&) { return record(0); }
bool install_list_patch_hooks(const HookInstallContext&) { return record(1); }
bool install_selection_hooks(const HookInstallContext&) { return record(2); }
bool install_scoreinfo_overlay_hooks(const HookInstallContext&) { return record(3); }
bool install_title_hooks(const HookInstallContext&) { return record(4); }
bool install_chart_patch_hooks(const HookInstallContext&) { return record(5); }
bool install_note_count_hooks(const HookInstallContext&) { return record(6); }
bool install_duration_hooks(const HookInstallContext&) { return record(7); }
bool install_progress_hooks(const HookInstallContext&) { return record(8); }
bool install_audio_sead_hooks(const HookInstallContext&) { return record(9); }
bool install_piano_page_selection_guard(const HookInstallContext&) { return record(10); }
core::HookShutdownResult shutdown_menu_session() { return shutdown_ok(); }
core::HookShutdownResult shutdown_list_patch() { return shutdown_ok(); }
core::HookShutdownResult shutdown_selection() { return shutdown_ok(); }
core::HookShutdownResult shutdown_scoreinfo_overlay() { return shutdown_ok(); }
core::HookShutdownResult shutdown_title() { return shutdown_ok(); }
core::HookShutdownResult shutdown_chart_patch() { return shutdown_ok(); }
core::HookShutdownResult shutdown_note_count() { return shutdown_ok(); }
core::HookShutdownResult shutdown_duration() { return shutdown_ok(); }
core::HookShutdownResult shutdown_progress() { return shutdown_ok(); }
bool shutdown_audio_sead() { return true; }
}

int main()
{
    const std::array<int, 11> expected{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    if (!ff7r::piano::game::install_release_hooks(nullptr)
        || installed_count != expected.size() || installed != expected) {
        std::cerr << "release_hook_empty_catalog_selftest: empty-catalog install skipped a required module\n";
        return 1;
    }
    std::cout << "release_hook_empty_catalog_selftest: ok\n";
    return 0;
}
