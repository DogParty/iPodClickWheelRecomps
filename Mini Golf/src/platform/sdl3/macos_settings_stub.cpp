// macos_settings.h on platforms without Cocoa: no window yet. The settings themselves work
// everywhere (platform/input_bindings.h, and the host's own frame-rate lock); what is missing
// here is only a way to show them to the player.
#include "platform/sdl3/macos_settings.h"

#ifndef __APPLE__
namespace minigolf::platform {

void macos_settings_install(const SettingsHooks&) {}
void macos_settings_open() {}
void macos_settings_set_frame_rate(unsigned) {}

}  // namespace minigolf::platform
#endif
