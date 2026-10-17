#pragma once

#include <windows.h>

#include "pipeline/oodle_decoder.h"

namespace ff7r::piano::game {

// Repository worker only, after the authenticated native initializer notification.
// Requires the catalog-default EXE allocator/free/diagnostic callbacks, null profiling,
// and reviewed stack threshold. Rereads detect drift, not concurrent replacement or shutdown.
// The main executable remains mapped for process lifetime; this acquires no module lease.
ff7rp::pipeline::Status prepare_game_oodle_decoder(
    HMODULE executable, ff7rp::pipeline::OodleDecoder& decoder);

} // namespace ff7r::piano::game
