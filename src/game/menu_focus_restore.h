#pragma once

#include <cstdint>
#include "game/menu_session_authority.h"
#include "game/song_registry.h"

namespace ff7r::piano::game {

enum class MenuFocusRestoreResult { NotApplied, Applied, Failed };

// Synchronous Open-owned authority only, never a persistent focus registry.
struct MenuOpenFocusContext {
    MenuSessionSnapshot opening{};
    RegistrySnapshot catalog{};
    SelectionSnapshot target{};
    void* array_data = nullptr;
    int32_t array_count = 0;
    int32_t array_capacity = 0;
    uint64_t target_name = 0;
    bool consumed = false;
    bool original_entered = false;
    MenuFocusRestoreResult result = MenuFocusRestoreResult::NotApplied;
};

inline thread_local MenuOpenFocusContext* current_menu_open_focus = nullptr;

class MenuOpenFocusScope final {
public:
    explicit MenuOpenFocusScope(MenuOpenFocusContext* context) noexcept
        : previous_(current_menu_open_focus) { current_menu_open_focus = context; }
    ~MenuOpenFocusScope() { current_menu_open_focus = previous_; }
    MenuOpenFocusScope(const MenuOpenFocusScope&) = delete;
    MenuOpenFocusScope& operator=(const MenuOpenFocusScope&) = delete;
private:
    MenuOpenFocusContext* previous_;
};

template<class Original>
void enter_menu_focus_original(MenuOpenFocusContext& context, Original original)
{
    // Mark entry before native execution, including a throwing trampoline.
    context.original_entered = true;
    original();
}

// Project returns explicit mutation outcome; NotApplied alone permits stock
// fallback. Failed (even before original entry) never retries native execution.
template<class Original, class Project>
void intercept_menu_open_focus(void* widget, bool exact_caller,
    Original original, Project project) noexcept
{
    auto* context = current_menu_open_focus;
    if (!exact_caller || !context || !context->opening || context->consumed
        || widget != context->opening.widget) {
        try { original(); } catch (...) {}
        return;
    }
    context->consumed = true;
    try {
        context->result = project(*context);
        if (context->result == MenuFocusRestoreResult::NotApplied && !context->original_entered)
            enter_menu_focus_original(*context, original);
    } catch (...) { context->result = MenuFocusRestoreResult::Failed; }
}

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
