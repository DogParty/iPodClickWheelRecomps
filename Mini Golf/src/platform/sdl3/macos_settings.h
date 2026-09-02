// The macOS settings window: three tabs — General, Input, Graphics.
//
// The window is Cocoa rather than drawn in the game's own 320x240 picture, because these are
// settings of the program and not part of the game. Input reads and writes
// `platform::input_bindings()` directly and asks the host to save them when something changes;
// General holds the frame rate — 30, 60 or unlocked — and whether the window title shows the rate
// it is actually managing; Graphics holds how the 320x240 picture is enlarged to fill a window
// many times its size. The frame rate is the host's setting, not the window's: the L key changes
// it too, so the window asks the host to change it and is told what it became.
//
// Other platforms get an empty stub. Everything that is not the window itself — the actions,
// the defaults, the "one input does one thing" rule, the file — is in platform/input_bindings.h,
// so a second platform's settings UI only has to name keys and offer them.
#pragma once

#include "platform/platform.h"

namespace minigolf::platform {

// What the window needs from the host. It keeps no settings of its own: it shows what the host
// has and tells it when the player wants it changed.
struct SettingsHooks {
    void (*on_bindings_changed)(void* context) = nullptr;  // save them; they are already applied
    // The frames a second to pace to, or 0 to run as fast as the machine allows.
    void (*on_frame_rate_chosen)(void* context, unsigned frames_per_second) = nullptr;
    void (*on_show_frame_rate_changed)(void* context, bool show) = nullptr;
    void (*on_scaling_chosen)(void* context, Scaling scaling) = nullptr;
    void (*on_pixel_perfect_changed)(void* context, bool pixel_perfect) = nullptr;
    void (*on_render_scale_chosen)(void* context, unsigned scale) = nullptr;
    unsigned frame_rate = 0;  // all four as they stand at start-up
    bool show_frame_rate = true;
    Scaling scaling = Scaling::Sharp;
    bool pixel_perfect = false;
    unsigned render_scale = 1;
    void* context = nullptr;
};

// Add "Settings…" (⌘,) to the application menu.
void macos_settings_install(const SettingsHooks& hooks);

// Show the window (also what the menu item does), bringing it to the front if it is already up.
void macos_settings_open();

// Keep General in step when the frame rate is changed elsewhere — the L key. The window is not
// the owner of that setting and must not look like it.
void macos_settings_set_frame_rate(unsigned frames_per_second);

}  // namespace minigolf::platform
