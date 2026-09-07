#include "game/menu_session_authority.h"

#include <atomic>
#include <limits>

namespace ff7r::piano::game {
namespace {
MenuSessionAuthority g_authority;
std::atomic_uint64_t g_active_generation{0};
std::atomic_uint64_t g_ready_generation{0};
bool handles_equal(const UObjectLiveHandle& a, const UObjectLiveHandle& b) noexcept {
    return a.internal_index == b.internal_index && a.serial_number == b.serial_number;
}
}

bool same_menu_session(const MenuSessionSnapshot& a, const MenuSessionSnapshot& b,
    bool require_phase) noexcept {
    return a.generation && a.generation == b.generation
        && (!require_phase || a.phase == b.phase) && a.controller == b.controller
        && a.list == b.list && a.widget == b.widget
        && handles_equal(a.widget_identity, b.widget_identity)
        && a.list_ownership == b.list_ownership;
}

MenuSessionSnapshot MenuSessionAuthority::begin_open(void* controller, void* list,
    void* widget, UObjectLiveHandle identity, uint16_t word,
    MenuListOwnership list_ownership) noexcept {
    std::lock_guard lock(mutex_);
    if (disabled_ || !controller || !list || !widget || identity.internal_index < 0
        || identity.serial_number <= 0 || (word != 0 && word != 0x0100)
        || generation_ == std::numeric_limits<uint64_t>::max()) {
        if (generation_ == std::numeric_limits<uint64_t>::max()) {
            disabled_ = true; current_.phase = MenuSessionPhase::Failed;
        }
        return {};
    }
    if (current_) return {};
    current_ = {++generation_, MenuSessionPhase::Opening,
        controller, list, widget, identity, list_ownership};
    if (this == &g_authority) {
        g_active_generation.store(current_.generation, std::memory_order_release);
        g_ready_generation.store(0, std::memory_order_release);
    }
    return current_;
}

bool MenuSessionAuthority::publish_ready(const MenuSessionSnapshot& opening,
    uint16_t word, void* widget, UObjectLiveHandle identity) noexcept {
    std::lock_guard lock(mutex_);
    if (disabled_ || word != 1 || current_.phase != MenuSessionPhase::Opening
        || !same_menu_session(current_, opening, false) || widget != current_.widget
        || !handles_equal(identity, current_.widget_identity)) return false;
    current_.phase = MenuSessionPhase::Ready;
    if (this == &g_authority)
        g_ready_generation.store(current_.generation, std::memory_order_release);
    return true;
}

MenuSessionSnapshot MenuSessionAuthority::begin_close(void* list) noexcept {
    std::lock_guard lock(mutex_);
    if (disabled_ || current_.phase != MenuSessionPhase::Ready || current_.list != list) return {};
    current_.phase = MenuSessionPhase::Closing;
    if (this == &g_authority) g_ready_generation.store(0, std::memory_order_release);
    return current_;
}

bool MenuSessionAuthority::finish_close(const MenuSessionSnapshot& closing,
    uint16_t word) noexcept {
    std::lock_guard lock(mutex_);
    if (!same_menu_session(current_, closing, false)
        || current_.phase != MenuSessionPhase::Closing) return false;
    if (word == 0x0100) {
        current_ = {};
        if (this == &g_authority) g_active_generation.store(0, std::memory_order_release);
    } else if (word == 1) {
        current_.phase = MenuSessionPhase::Ready;
        if (this == &g_authority)
            g_ready_generation.store(current_.generation, std::memory_order_release);
    } else { current_.phase = MenuSessionPhase::Failed; return false; }
    return true;
}

MenuSessionSnapshot MenuSessionAuthority::begin_ending(void* controller) noexcept {
    std::lock_guard lock(mutex_);
    if (disabled_ || !current_ || current_.controller != controller
        || current_.phase == MenuSessionPhase::Ending) return {};
    current_.phase = MenuSessionPhase::Ending;
    if (this == &g_authority) g_ready_generation.store(0, std::memory_order_release);
    return current_;
}

bool MenuSessionAuthority::retire(uint64_t generation) noexcept {
    std::lock_guard lock(mutex_);
    if (!current_ || current_.generation != generation) return false;
    current_ = {};
    if (this == &g_authority) {
        g_active_generation.store(0, std::memory_order_release);
        g_ready_generation.store(0, std::memory_order_release);
    }
    return true;
}

MenuSessionSnapshot MenuSessionAuthority::capture(bool allow_opening) const noexcept {
    std::lock_guard lock(mutex_);
    if (disabled_ || (current_.phase != MenuSessionPhase::Ready
        && !(allow_opening && current_.phase == MenuSessionPhase::Opening))) return {};
    return current_;
}

bool MenuSessionAuthority::try_idle_for_catalog_adoption() const noexcept {
    std::unique_lock lock(mutex_, std::try_to_lock);
    return lock.owns_lock() && !disabled_ && !current_;
}

bool MenuSessionAuthority::matches(uint64_t generation, void* widget,
    const UObjectLiveHandle& identity, bool ready) const noexcept {
    std::lock_guard lock(mutex_);
    return !disabled_ && current_.generation == generation
        && (!ready || current_.phase == MenuSessionPhase::Ready)
        && current_.widget == widget && handles_equal(current_.widget_identity, identity);
}

void MenuSessionAuthority::shutdown() noexcept {
    std::lock_guard lock(mutex_); disabled_ = true; current_ = {};
    if (this == &g_authority) {
        g_active_generation.store(0, std::memory_order_release);
        g_ready_generation.store(0, std::memory_order_release);
    }
}

MenuSessionAuthority& menu_session_authority() noexcept { return g_authority; }
MenuSessionSnapshot capture_ready_menu_session() noexcept { return g_authority.capture(false); }
#ifndef FF7RP_LIST_CATALOG_SELFTEST
// The list harness supplies its synthetic widget/session ingress, while using
// the real authority methods above for Open/Close and generation transitions.
MenuSessionSnapshot capture_menu_callback_session() noexcept { return g_authority.capture(true); }
bool menu_session_matches(uint64_t generation, void* widget,
    const UObjectLiveHandle& identity, bool ready) noexcept {
    return g_authority.matches(generation, widget, identity, ready);
}
#endif
bool menu_session_generation_exact(uint64_t generation, bool ready) noexcept {
    return generation != 0 && (ready ? g_ready_generation : g_active_generation)
        .load(std::memory_order_acquire) == generation;
}
uint64_t active_menu_session_generation() noexcept {
    return g_active_generation.load(std::memory_order_acquire);
}
} // namespace ff7r::piano::game
