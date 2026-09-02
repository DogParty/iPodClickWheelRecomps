// What the player can do, and which input does it.
//
// The iPod had one wheel and five buttons, and this is that set: the two ways of turning the
// wheel, the four sides of it a finger can rest on, and the five buttons — named as a player
// would name them. Which key, button or gesture performs one is the platform's business: a
// binding here is a plain `InputCode`, and only the platform that produced it knows what it
// means (an SDL keycode, an Android key code, a gamepad button).
//
// The table is portable so that every platform gets rebinding for free once it has a way to ask
// the player for an input: the defaults, the "one input does one thing" rule, and the saved file
// are all here, and the platform supplies only the codes and a settings window.
#pragma once

#include <cstdint>
#include <string>

namespace holdem::platform {

// The eleven things a player can do.
//
// `SwipeLeft`/`SwipeRight` *turn* the wheel, which is how a menu moves. The four `Touch` actions
// are the other gesture the wheel has: a finger resting on one of its sides, which is how this
// game walks — "TOUCH THE LOWER SIDE OF THE WHEEL TO MAKE JACK MOVE DOWNWARDS", as it says
// itself. They are not the same thing and cannot be built out of each other: a turn is a change
// of position and a touch is a position, so holding one side reports the same place every frame
// and moves no menu at all.
//
// The rest are the buttons around and inside the wheel.
enum class Action : uint32_t {
    SwipeLeft,
    SwipeRight,
    TouchUp,
    TouchRight,
    TouchBottom,
    TouchLeft,
    Select,
    PlayPause,
    Menu,
    Rewind,
    FastForward,
};

constexpr unsigned ACTION_COUNT = 11;

// Two inputs may perform each action, because one name for a thing is often not enough: the
// menus are a vertical list, so a player reaches for the up and down arrows, while the wheel they
// stand for turned left and right. Slot 0 is the one a settings window shows first.
constexpr unsigned BINDING_SLOTS = 2;

// Every action, in the order a settings window should list them.
[[nodiscard]] const Action* all_actions();

// The name used in the saved bindings — stable, never shown to a player.
[[nodiscard]] const char* action_key(Action action);

// What a player sees.
[[nodiscard]] const char* action_label(Action action);

// One input, as the platform identifies it. 0 means "nothing bound".
using InputCode = uint32_t;
constexpr InputCode NO_INPUT = 0;

// An input a settings window can offer, with the name to show for it. A platform registers the
// set it is willing to assign; offering a list rather than asking the player to press a key is
// what lets a settings window work without touching that platform's event handling at all.
struct InputChoice {
    InputCode code;
    const char* label;
};

// What this platform offers. The array outlives the call; `count` receives its length.
void set_assignable_inputs(const InputChoice* choices, unsigned count);
[[nodiscard]] const InputChoice* assignable_inputs(unsigned& count);

// The name to show for one code, from that list; "—" when nothing is bound and the code itself
// when the platform did not offer it.
[[nodiscard]] const char* input_label(InputCode code);

class InputBindings {
public:
    // Give `action` this input, in one of its two slots. An input does exactly one thing, so
    // wherever it was bound before is left unbound — the alternative is a key that quietly does
    // two things at once.
    void bind(Action action, InputCode code, unsigned slot = 0);
    void clear(Action action, unsigned slot = 0);

    [[nodiscard]] InputCode code(Action action, unsigned slot = 0) const;

    // The action `code` performs, if any.
    [[nodiscard]] bool action_for(InputCode code, Action& action) const;

    // Back to what the platform asked for at start-up.
    void restore_defaults();

    // The bindings as text, one `action code code` line each — what gets saved, and what a person
    // could reasonably edit by hand. A deliberately unbound action is written with code 0, so
    // that an action simply *missing* from the file — one this build has and the file's writer
    // did not — keeps its default rather than silently doing nothing.
    [[nodiscard]] std::string to_text() const;
    void from_text(const std::string& text);

private:
    InputCode codes_[ACTION_COUNT][BINDING_SLOTS] = {};
    InputCode defaults_[ACTION_COUNT][BINDING_SLOTS] = {};

    friend void set_default_bindings(const InputCode (&codes)[ACTION_COUNT][BINDING_SLOTS]);
};

// The bindings in use.
[[nodiscard]] InputBindings& input_bindings();

// The platform's own defaults, installed once at start-up before anything is loaded. They are
// also what "Restore Defaults" goes back to.
void set_default_bindings(const InputCode (&codes)[ACTION_COUNT][BINDING_SLOTS]);

// Read the player's bindings from the platform's store, and write them back after a change.
// Both are no-ops on a platform whose store keeps nothing.
void load_input_bindings();
void save_input_bindings();

}  // namespace holdem::platform
