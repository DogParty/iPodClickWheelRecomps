// The hole's state machine: the states a hole passes through, and its per-frame step.
//
// A hole is one state machine from the ball being placed to the wipe into the next hole. The
// states live here rather than in hole_tick.cpp because three files read `play_state().state`:
// the tick that drives it, the renderer that draws whatever it says is happening, and the turn
// code that hands play to the next player.
#pragma once

#include <cstdint>

namespace minigolf::game {

enum HoleState : uint32_t {
    IDLE = 0,
    PLACING = 1,
    AIMING = 2,
    POWER = 3,
    STRUCK = 4,
    ROLLING = 5,
    HOLED = 6,
    HOLED_ALT = 7,
    SINKING = 8,
    FAST_FORWARD = 9,
    HINT_START = 10,
    HINT_1 = 11,
    HINT_2 = 12,
    HINT_3 = 13,
    LIMIT_MESSAGE = 14,
    LIMIT_WAIT = 15,
    OUT_MESSAGE = 16,
    OUT_WAIT = 17,
    TURN_MESSAGE = 18,
    TURN_WAIT = 19,
    HOLE_DONE = 20,
    CARD_IN = 21,
    CARD = 22,
    CARD_OUT = 23,
    COURSE_MESSAGE = 24,
    COURSE_MESSAGE_WAIT = 25,
    PICTURE_START = 26,
    PICTURE_IN = 27,
    PICTURE = 28,
    PICTURE_OUT = 29,
    NEXT_HOLE = 30,
    CONFIRM = 31,
    STATE_COUNT = 32,
};

// One frame of the hole, `frame_ms` milliseconds after the last.
void hole_tick(uint32_t frame_ms);

}  // namespace minigolf::game
