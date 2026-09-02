// The Switch's settings screen: the controls, and what each one is bound to.
//
// It is drawn as text on libnx's console rather than in the game's own picture, for the same
// reason the macOS one is a Cocoa window: these are settings of the program, not part of the
// game, and the game's 320×240 screen has no room for them. The console and the framebuffer are
// the same display, so the caller puts the display into console mode first and takes it back
// afterwards (`SwitchPlatform::settings_screen`).
//
// Everything it edits is the portable table in platform/input_bindings.h, which is what makes a
// screen like this fifty lines instead of a platform's worth of work.
#pragma once

#include <switch.h>

namespace minigolf::platform {

// Draw and run the screen until the player leaves it. `pad` is the caller's, already updated
// once this frame; this takes it over until it returns.
void switch_settings_screen(PadState& pad);

}  // namespace minigolf::platform
