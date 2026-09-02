// The click wheel and the buttons.
//
// The game polls once a frame for one wheel event; button presses are flag bits the frame pump
// sets directly (src/runtime/main.cpp), not events. `src/libeapp/input.cpp` implements the poll.
#pragma once

#include "framework/types.h"

#include <cstdint>

namespace cubis::controls {

// Take the next wheel event into the record at `events` (its second word receives the event, or
// zero when nothing happened). `movement` is the record the game also passes and never reads.
void poll(GuestAddress events, GuestAddress movement);

// Tell the event system the game is finished with an event node. The iPod freed it here; the
// host owns nothing, so the answer is zero.
[[nodiscard]] uint32_t release_event(GuestAddress node);

}  // namespace cubis::controls
