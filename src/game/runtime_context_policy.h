#pragma once

#include "game/audio_cleanup_policy.h"
#include "game/song_registry.h"
#include "game/uobject_lifetime.h"
#include "game/uobject_locator_core.h"
#include "game/audio_memory_read_policy.h"
#include "game/audio_native_call_policy.h"
#include "game/pause_resume_policy.h"
#include "game/selection_audio_policy.h"
#include "game/canonical_substrate_policy.h"
#include "game/bgm_playback_aggregate_policy.h"

#include "game/audio_setup_publication_policy.h"
#include "game/audio_cleanup_projection_policy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <utility>
