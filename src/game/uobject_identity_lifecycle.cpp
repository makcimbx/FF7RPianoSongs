#include "game/uobject_identity_lifecycle.h"

namespace ff7r::piano::game::uobject_identity_lifecycle {

Transition transition(const State before, const Event event) noexcept
{
    Transition result{before, {}};
    switch (event) {
    case Event::Initialize:
        if (!before.initialized) {
            result.after = {true, false, false};
            result.effects.initialize = true;
        }
        break;
    case Event::Clear:
        result.effects.clear_list_locator = true;
        result.after.clear_requested = true;
        if (!before.chart_consumer_active) {
            result.effects.clear_chart_locator = true;
            result.after.clear_requested = false;
            result.after.initialized = false;
        }
        break;
    case Event::ActivateChartConsumer:
        result.after.chart_consumer_active = true;
        break;
    case Event::ReleaseChartConsumer:
        result.after.chart_consumer_active = false;
        if (before.clear_requested) {
            result.effects.clear_chart_locator = true;
            result.after.clear_requested = false;
            result.after.initialized = false;
        }
        break;
    }
    return result;
}

} // namespace ff7r::piano::game::uobject_identity_lifecycle
