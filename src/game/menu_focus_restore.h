#pragma once

#include <cstdint>

namespace ff7r::piano::game {

enum class MenuFocusRestoreResult { NotApplied, Applied, Failed };

// Only the saved FName is temporary. The native helper owns the intentional
// selected-index change and its internal control/delegate synchronization.
// Accessors are the existing live-widget/managed-array boundary in production.
template<class Validate, class Read, class Write, class Restore>
MenuFocusRestoreResult restore_menu_focus_fields(int32_t index,
    Validate validate, Read read, Write write, Restore restore) noexcept
{
    uint64_t saved = 0;
    if (!validate() || !read(0x478, saved)) return MenuFocusRestoreResult::NotApplied;
    if (!write(0x478, uint64_t{0})) return MenuFocusRestoreResult::Failed;
    bool applied = false;
    try {
        if (write(0x480, index)) {
            restore();
            int32_t selected = -1;
            applied = validate() && read(0x480, selected) && selected == index;
        }
    } catch (...) {}
    uint64_t current = 1;
    if (!validate() || !read(0x478, current) || current != 0
        || !write(0x478, saved) || !read(0x478, current) || current != saved)
        return MenuFocusRestoreResult::Failed;
    return applied ? MenuFocusRestoreResult::Applied : MenuFocusRestoreResult::Failed;
}

} // namespace ff7r::piano::game
