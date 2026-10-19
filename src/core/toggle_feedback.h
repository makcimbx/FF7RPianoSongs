#pragma once

namespace ff7r::piano::core {

// Call after a successful toggle, outside game locks. Playback is asynchronous.
bool play_toggle_feedback(bool enabled);
bool stop_toggle_feedback();

} // namespace ff7r::piano::core
