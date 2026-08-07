#pragma once

namespace ff7r::piano::game::uobject_identity_lifecycle {

enum class Event {
    Initialize,
    Clear,
    ActivateChartConsumer,
    ReleaseChartConsumer,
};

struct State {
    bool initialized = false;
    bool chart_consumer_active = false;
    bool clear_requested = false;
};

struct Effects {
    bool initialize = false;
    bool clear_list_locator = false;
    bool clear_chart_locator = false;
};

struct Transition {
    State after{};
    Effects effects{};
};

// Pure characterization of the centralized identity service's existing lifecycle.
// Locator storage remains owned by uobject_identity.cpp; effects state exactly which
// side may be cleared by each transition.
Transition transition(State before, Event event) noexcept;

} // namespace ff7r::piano::game::uobject_identity_lifecycle
