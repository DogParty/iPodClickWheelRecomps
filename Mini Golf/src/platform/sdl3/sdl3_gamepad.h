// Gamepads on the desktop, as one more set of inputs the bindings table can name.
//
// Nothing here is a new kind of control. A gamepad button performs one of the seven things a
// player can do (platform/input_bindings.h) exactly as a key does, is looked up in the same
// table, and is chosen from the same list in the same settings window. What it needs from this
// file is a code of its own: an `InputCode` means only what the platform that produced it says
// it means, and SDL's keycodes and its gamepad buttons are two separate numberings that would
// otherwise collide. SDL keycodes reach up to bit 30 (SDL_SCANCODE_MASK) and no further, so
// bit 31 marks a code as a gamepad button.
//
// The sticks are deliberately *not* bindable buttons. A stick is the click wheel, and that is
// the whole reason to want one: the wheel counts 120 detents to a turn and eight to a menu row,
// which is finer than a key can express. A key press is worth a whole row, every time; a stick
// held a little off centre is worth a detent now and then, which is exactly what lining up a
// putt wants. `wheel_detents` is where that conversion lives.
#pragma once

#include "platform/input_bindings.h"

#include <SDL3/SDL.h>

#include <vector>

namespace minigolf::platform {

// Set on an InputCode to mark it as a gamepad button rather than a key.
constexpr InputCode GAMEPAD_FLAG = 0x8000'0000u;

[[nodiscard]] constexpr InputCode gamepad_code(SDL_GamepadButton button) {
    return GAMEPAD_FLAG | static_cast<InputCode>(button);
}

// The buttons a player may bind, with the names the settings window shows for them. The array
// outlives the call; `count` receives its length.
[[nodiscard]] const InputChoice* gamepad_inputs(unsigned& count);

// Every gamepad the system has, opened as they arrive and closed as they go. One instance,
// owned by the platform; the pads are closed with it.
class Gamepads {
public:
    Gamepads() = default;
    ~Gamepads();
    Gamepads(const Gamepads&) = delete;
    Gamepads& operator=(const Gamepads&) = delete;

    // Open whatever is already plugged in. Called once, after SDL is up.
    void open_all();

    // A gamepad added or removed. Any other event is ignored.
    void handle_event(const SDL_Event& event);

    // What the sticks are worth this frame, in click-wheel detents: positive is the direction
    // the right and down arrow keys scroll. Zero when nothing is deflected past the dead zone,
    // and zero when there is no gamepad at all, so a machine without one pays nothing.
    //
    // The fraction of a detent left over is carried into the next frame rather than dropped,
    // which is what lets a stick barely off centre turn the wheel slowly instead of not at all.
    [[nodiscard]] int wheel_detents(unsigned frames_per_second);

private:
    std::vector<SDL_Gamepad*> pads_;
    float carry_ = 0.0f;
};

}  // namespace minigolf::platform
