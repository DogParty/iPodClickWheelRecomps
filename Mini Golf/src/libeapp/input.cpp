// The InputEvents framework: the click wheel as the game polls it.
//
// The game polls once per frame; the poll writes one event word — bit 30 "event present" plus
// the wheel position code in the low byte — or 0 when nothing happened. Button presses do not
// travel this way for Mini Golf: they are flag bits the frame pump sets directly
// (runtime/main.cpp). Behaviour from the emulator's Stub::InputPoll { arg: 0, offset: 4 }.
#include "framework/controls.h"
#include "host_state.h"
#include "ipod_eapp.h"
#include "runtime/memory.h"

namespace minigolf::eapp {

namespace {

// The event word lands four bytes into the structure the game passes.
constexpr uint32_t EVENT_WORD_OFFSET = 4;

}  // namespace

}  // namespace minigolf::eapp

namespace minigolf::controls {

using namespace minigolf::eapp;  // NOLINT(google-build-using-namespace): one file, by design

// #0 poll(events, movement): *(events + 4) = the next event word, or 0.
void poll(GuestAddress events, GuestAddress movement) {
    log_call("InputEvents", 0, {events, movement});
    st32(events + EVENT_WORD_OFFSET, take_input_event());
}

// #1 the game is finished with an event node. The iPod freed it; the host owns nothing.
uint32_t release_event(GuestAddress node) {
    log_call("InputEvents", 1, {node});
    return 0;
}

}  // namespace minigolf::controls
