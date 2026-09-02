// The InputEvents framework: the click wheel as the game polls it.
//
// The game polls once per frame; the poll writes one event word — bit 30 "event present" plus
// the wheel position code in the low byte — into the *second* of the two structures it is
// handed, or 0 when nothing happened. Bit 30 is not decoration: some titles test it before
// looking at the low byte at all, so an event word without it is discarded whole.
//
// Buttons do not travel this way for this title. It has no button flags word; presses arrive as
// nodes on the event list at `ctx+0x30`, which the frame pump publishes (runtime/main.cpp).
// Behaviour from the emulator's `Stub::InputPoll { arg: 1, offset: 0 }`.
#include "framework/controls.h"
#include "host_state.h"
#include "ipod_eapp.h"
#include "runtime/memory.h"

namespace holdem::eapp {

namespace {

// Where in the second structure the event word lands.
constexpr uint32_t EVENT_WORD_OFFSET = 0;

}  // namespace

}  // namespace holdem::eapp

namespace holdem::controls {

using namespace holdem::eapp;  // NOLINT(google-build-using-namespace): one file, by design

// #0 poll(events, movement): *movement = the next event word, or 0.
void poll(GuestAddress events, GuestAddress movement) {
    log_call("InputEvents", 0, {events, movement});
    st32(movement + EVENT_WORD_OFFSET, take_input_event());
}

// #1 the game is finished with an event node. The iPod freed it; the host owns nothing.
uint32_t release_event(GuestAddress node) {
    log_call("InputEvents", 1, {node});
    return 0;
}

}  // namespace holdem::controls
