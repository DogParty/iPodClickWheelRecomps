// Typing, on the machines that have a keyboard.
//
// The iPod had none: a name was spelled out one letter at a time on a wheel, and that is what the
// game's name-entry screen does. Where there *is* a keyboard, typing the name is so much better
// that it is worth the small addition — so this is a platform capability, not something the
// original did. A platform that cannot offer it simply never reports any, and the wheel is still
// the way in.
//
// The characters are whatever the platform's text input produced, which is why they arrive as
// UTF-8 rather than key codes: a keyboard layout, a dead key or an input method has already had
// its say by the time they get here.
#pragma once

#include <string>

namespace ipod::platform {

// What the player typed since the last frame.
struct TypedText {
    std::string characters;   // UTF-8, in the order they were typed
    unsigned backspaces = 0;  // how many times they asked to delete what came before
    bool confirm = false;     // and whether they said they were finished (Return)

    [[nodiscard]] bool empty() const { return characters.empty() && backspaces == 0 && !confirm; }
};

// Whether this platform offers typing at all. The game asks before it treats the absence of
// typed characters as meaningful.
[[nodiscard]] bool text_entry_supported();
void set_text_entry_supported(bool supported);

// The frame pump hands over what the platform collected; whoever acts on it takes it, which
// clears it, so one keystroke cannot be acted on twice.
void text_entry_deliver(const TypedText& typed);
[[nodiscard]] TypedText text_entry_take();

}  // namespace ipod::platform
