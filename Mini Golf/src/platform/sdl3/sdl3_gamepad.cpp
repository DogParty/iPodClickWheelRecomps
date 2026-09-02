// See sdl3_gamepad.h.
#include "platform/sdl3/sdl3_gamepad.h"

#include <algorithm>
#include <cmath>

namespace minigolf::platform {

namespace {

// How far a stick must be pushed before it counts as pushed at all. Sticks rest a little off
// centre and wear worse with age; a quarter of the throw is the usual allowance.
constexpr float DEAD_ZONE = 0.25f;

// What a stick held all the way over is worth, in detents a second. Eight detents is one menu
// row, so this is six rows a second at full throw — brisk without overshooting — and the square
// curve below means half throw is a quarter of that, which is the fine end a putt is aimed with.
constexpr float DETENTS_PER_SECOND = 48.0f;

// The axis reading, -1 to 1, with the dead zone taken out and the rest stretched back over the
// full range so the first movement past the zone is a small one rather than a jump.
float axis_fraction(int16_t raw) {
    const float value = static_cast<float>(raw) / 32767.0f;
    const float magnitude = std::fabs(value);
    if (magnitude <= DEAD_ZONE) {
        return 0.0f;
    }
    const float scaled = (magnitude - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    return std::copysign(scaled * scaled, value);  // squared: fine near the centre
}

// The buttons offered for binding, in the order a settings window should list them: the four
// face buttons as they sit on the pad, then the shoulders, then the D-pad, then the rest.
// The labels are what a player sees; the codes are what is written to their bindings file.
struct GamepadButtonName {
    SDL_GamepadButton button;
    const char* label;
};

constexpr GamepadButtonName BUTTONS[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, "Pad A (south)"},
    {SDL_GAMEPAD_BUTTON_EAST, "Pad B (east)"},
    {SDL_GAMEPAD_BUTTON_WEST, "Pad X (west)"},
    {SDL_GAMEPAD_BUTTON_NORTH, "Pad Y (north)"},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "Pad L"},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "Pad R"},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, "D-pad Up"},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "D-pad Down"},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "D-pad Left"},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "D-pad Right"},
    {SDL_GAMEPAD_BUTTON_START, "Pad Start"},
    {SDL_GAMEPAD_BUTTON_BACK, "Pad Back"},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, "Left stick click"},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "Right stick click"},
};

constexpr unsigned BUTTON_COUNT = sizeof BUTTONS / sizeof BUTTONS[0];

}  // namespace

const InputChoice* gamepad_inputs(unsigned& count) {
    static InputChoice choices[BUTTON_COUNT];
    static bool built = false;
    if (!built) {
        for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
            choices[i].code = gamepad_code(BUTTONS[i].button);
            choices[i].label = BUTTONS[i].label;
        }
        built = true;
    }
    count = BUTTON_COUNT;
    return choices;
}

Gamepads::~Gamepads() {
    for (SDL_Gamepad* pad : pads_) {
        SDL_CloseGamepad(pad);
    }
}

void Gamepads::open_all() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids == nullptr) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[i])) {
            pads_.push_back(pad);
        }
    }
    SDL_free(ids);
}

void Gamepads::handle_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which)) {
            pads_.push_back(pad);
        }
        return;
    }
    if (event.type != SDL_EVENT_GAMEPAD_REMOVED) {
        return;
    }
    SDL_Gamepad* gone = SDL_GetGamepadFromID(event.gdevice.which);
    const auto at = std::find(pads_.begin(), pads_.end(), gone);
    if (at != pads_.end()) {
        SDL_CloseGamepad(*at);
        pads_.erase(at);
    }
}

int Gamepads::wheel_detents(unsigned frames_per_second) {
    // The strongest deflection any pad is showing, on whichever of the two axes is pushed
    // further. Both axes turn the wheel because the menus are a vertical list and the wheel
    // they stand for turned sideways — the same reason the arrow keys come in two pairs.
    // Down and right are the same direction, as ↓ and → are.
    float strongest = 0.0f;
    for (SDL_Gamepad* pad : pads_) {
        const float x = axis_fraction(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
        const float y = axis_fraction(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
        const float pushed = std::fabs(x) >= std::fabs(y) ? x : y;
        if (std::fabs(pushed) > std::fabs(strongest)) {
            strongest = pushed;
        }
    }
    if (strongest == 0.0f) {
        carry_ = 0.0f;  // nothing held: a leftover fraction should not fire later
        return 0;
    }
    // An unlocked frame rate has no fixed step to divide by; the game's own timebase is what
    // the rest of the program assumes when it needs a number (runtime/main.cpp).
    const float rate = frames_per_second != 0 ? static_cast<float>(frames_per_second) : 60.0f;
    carry_ += strongest * DETENTS_PER_SECOND / rate;
    const float whole = std::trunc(carry_);
    carry_ -= whole;
    return static_cast<int>(whole);
}

}  // namespace minigolf::platform
