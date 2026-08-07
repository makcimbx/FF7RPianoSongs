#pragma once

#include "game/song_registry.h"
#include "game/uobject_lifetime.h"

namespace ff7r::piano::game {

struct SelectionUiRefreshTarget final {
    void* context = nullptr;
    UObjectLiveHandle identity{};
    int visible_index = -1;
};

struct ListItemUiRefreshTarget final {
    void* context = nullptr;
    void* widget = nullptr;
    UObjectLiveHandle context_identity{};
    UObjectLiveHandle widget_identity{};
    int visible_index = -1;
};

struct MenuUiRefreshSnapshot final {
    SelectionSnapshot selection;
    SelectionUiRefreshTarget selection_target;
    ListItemUiRefreshTarget list_target;
};

bool capture_active_selection_ui_target(
    const SelectionSnapshot& expected, SelectionUiRefreshTarget& out) noexcept;
bool capture_active_list_item_ui_target(
    const SelectionSnapshot& expected, ListItemUiRefreshTarget& out) noexcept;
bool validate_active_selection_ui_target(
    const SelectionSnapshot& expected, const SelectionUiRefreshTarget& target) noexcept;
bool validate_active_list_item_ui_target(
    const SelectionSnapshot& expected, const ListItemUiRefreshTarget& target) noexcept;
bool refresh_active_selection_ui(
    const SelectionSnapshot& expected, const SelectionUiRefreshTarget& target);
bool refresh_active_list_item_ui(
    const SelectionSnapshot& expected, const ListItemUiRefreshTarget& target);

} // namespace ff7r::piano::game
