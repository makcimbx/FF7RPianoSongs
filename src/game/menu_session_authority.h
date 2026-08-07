#pragma once

#include "core/hooks.h"
#include "game/song_registry.h"
#include "game/catalog_adoption.h"
#include "game/uobject_lifetime.h"

#include <cstdint>
#include <mutex>

namespace ff7r::piano::game {

enum class MenuOpenAdmissionDisposition {
    ForwardOriginal,
    SuppressTerminalFailure,
    Protected,
};

constexpr MenuOpenAdmissionDisposition classify_menu_open_admission(
    const bool exact_call_site, const bool terminal_failure,
    const bool callback_admitted) noexcept
{
    if (!exact_call_site) return MenuOpenAdmissionDisposition::ForwardOriginal;
    if (terminal_failure)
        return MenuOpenAdmissionDisposition::SuppressTerminalFailure;
    if (!callback_admitted)
        return MenuOpenAdmissionDisposition::ForwardOriginal;
    return MenuOpenAdmissionDisposition::Protected;
}

enum class MenuOpenCatalogDisposition {
    ForwardOriginal,
    SuppressTerminalFailure,
};

constexpr MenuOpenCatalogDisposition classify_menu_open_catalog_result(
    const bool exact_call_site, const CatalogAdoptionResult result) noexcept
{
    if (!exact_call_site) return MenuOpenCatalogDisposition::ForwardOriginal;
    if (result == CatalogAdoptionResult::TerminalFailure)
        return MenuOpenCatalogDisposition::SuppressTerminalFailure;
    return MenuOpenCatalogDisposition::ForwardOriginal;
}

enum class MenuListOwnership : uint8_t {
    Unmanaged,
    Managed,
    ReclassifyOnExactClose,
};

constexpr MenuListOwnership menu_list_ownership_from_open_facts(
    const bool managed, const bool transient) noexcept
{
    return managed ? MenuListOwnership::Managed
        : transient ? MenuListOwnership::ReclassifyOnExactClose
                    : MenuListOwnership::Unmanaged;
}

constexpr MenuListOwnership menu_open_ownership_for_final_binding(
    void* classified_widget, const UObjectLiveHandle& classified_identity,
    void* final_widget, const UObjectLiveHandle& final_identity,
    const MenuListOwnership classified) noexcept
{
    return classified_widget == final_widget
        && classified_identity.internal_index == final_identity.internal_index
        && classified_identity.serial_number == final_identity.serial_number
        ? classified : MenuListOwnership::ReclassifyOnExactClose;
}

enum class MenuSessionPhase : uint8_t { None, Opening, Ready, Closing, Ending, Failed };

constexpr bool should_restore_list_after_cancel_close(
    const bool original_returned, const bool exact_session_finished,
    const bool active_word_read, const uint16_t active_word,
    const MenuListOwnership ownership) noexcept
{
    return original_returned && exact_session_finished && active_word_read
        && active_word == 0x0100 && ownership != MenuListOwnership::Unmanaged;
}

struct MenuSessionSnapshot final {
    uint64_t generation = 0;
    MenuSessionPhase phase = MenuSessionPhase::None;
    void* controller = nullptr;
    void* list = nullptr;
    void* widget = nullptr;
    UObjectLiveHandle widget_identity{};
    MenuListOwnership list_ownership = MenuListOwnership::Unmanaged;
    explicit operator bool() const noexcept { return generation != 0; }
};

bool same_menu_session(const MenuSessionSnapshot& left,
    const MenuSessionSnapshot& right, bool require_phase = true) noexcept;

class MenuSessionAuthority final {
public:
    explicit MenuSessionAuthority(uint64_t initial_generation = 0) noexcept
        : generation_(initial_generation) {}
    MenuSessionSnapshot begin_open(void* controller, void* list, void* widget,
        UObjectLiveHandle identity, uint16_t native_word,
        MenuListOwnership list_ownership = MenuListOwnership::Unmanaged) noexcept;
    bool publish_ready(const MenuSessionSnapshot& opening, uint16_t native_word,
        void* widget, UObjectLiveHandle identity) noexcept;
    MenuSessionSnapshot begin_close(void* list) noexcept;
    bool finish_close(const MenuSessionSnapshot& closing, uint16_t native_word) noexcept;
    MenuSessionSnapshot begin_ending(void* controller) noexcept;
    bool retire(uint64_t generation) noexcept;
    MenuSessionSnapshot capture(bool allow_opening = false) const noexcept;
    bool try_idle_for_catalog_adoption() const noexcept;
    bool matches(uint64_t generation, void* widget,
        const UObjectLiveHandle& identity, bool require_ready = true) const noexcept;
    void shutdown() noexcept;

private:
    mutable std::mutex mutex_;
    uint64_t generation_ = 0;
    bool disabled_ = false;
    MenuSessionSnapshot current_{};
};

MenuSessionAuthority& menu_session_authority() noexcept;
MenuSessionSnapshot capture_ready_menu_session() noexcept;
MenuSessionSnapshot capture_menu_callback_session() noexcept;
bool menu_session_matches(uint64_t generation, void* widget,
    const UObjectLiveHandle& identity, bool require_ready = true) noexcept;
bool menu_session_generation_exact(uint64_t generation, bool require_ready) noexcept;
uint64_t active_menu_session_generation() noexcept;
// Resolves the widget bound to the embedded piano list through its established
// weak-handle layout and validates the resulting live UObject identity.
bool resolve_piano_menu_widget_binding(void* embedded_list, void*& widget,
    UObjectLiveHandle& identity) noexcept;

} // namespace ff7r::piano::game
