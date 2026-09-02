// The device's music library: what the iPod was playing when the game was launched.
//
// The Metadata framework is the iPod's own music database, and it is the one framework Lost
// imports that Mini Golf never touched — 152 entries of it, of which the game reaches two. What
// they are for is the game's soundtrack: it registers its own tracks as streams through
// `Audio #40` and asks the library how many the now-playing playlist holds, either side of each
// registration, to learn the index it was given.
//
// Everything else about the library is answered as an *empty* one, which is a real device state
// (a freshly restored iPod) rather than a placeholder — see reference/eapp-loader/lib.rs, which
// establishes that reading and the two entries below.
#pragma once

#include "framework/types.h"

#include <cstdint>

namespace lost::music {

// Reserve the block `now_playing_playlist` hands out. Called once by the frame pump before the
// game runs, because *when* it is allocated decides where every later allocation lands, and the
// oracle compares addresses (see src/runtime/main.cpp).
void reserve_playlist();

// `Metadata #62` — the playlist that is playing now. The game never dereferences it; it hands
// it straight back to the library's other calls. What matters is that it is not null, which is
// the library's failure value.
[[nodiscard]] GuestAddress now_playing_playlist();

// `Metadata #134` — how many tracks that playlist holds.
//
// This has to *grow by one for each stream the game registers*. Lost samples it either side of
// an `Audio #40` and takes `count - 1` as the index it was given, so a constant answers -1 —
// the game's own failure value — for every track it owns, forever.
[[nodiscard]] uint32_t playlist_track_count();

}  // namespace lost::music
