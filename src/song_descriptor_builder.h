#pragma once

#include "game/song_registry.h"
#include "pipeline/song_types.h"

namespace ff7r::piano {

// Pure composition seam between validated offline song storage and the
// immutable descriptor consumed by runtime code. The returned descriptor owns
// every string and chart row copied from the source song.
game::SongDescriptor build_song_descriptor(
    const ff7rp::pipeline::LoadedSong& song, int visible_index);

} // namespace ff7r::piano
