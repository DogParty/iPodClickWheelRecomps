// settings_window.h on platforms with no window of their own: nothing to show. The settings
// themselves work everywhere (platform/input_bindings.h, and the host's own frame-rate lock);
// what is missing here is only a way to show them to the player.
#include "platform/sdl3/settings_window.h"

#if !defined(__APPLE__) && !defined(_WIN32)
namespace lost::platform {

void settings_window_install(const SettingsHooks&) {}
void settings_window_open() {}
void settings_window_set_frame_rate(unsigned) {}

}  // namespace lost::platform
#endif
