// name_entry.cpp, as other files call it: screen 10, where the player types a name.
#pragma once

#include <cstdint>

namespace minigolf::game {

void name_entry_enter();  // 0x1800561c

// Take whatever the player typed since the last frame into the name. Does nothing where the
// platform offers no typing, which is every machine the game was written for; see name_entry.cpp.
[[nodiscard]] bool name_entry_typing();
uint32_t name_entry_handle_event(uint32_t event);  // 0x180126e8
uint32_t name_entry_render();                      // 0x18013848

}  // namespace minigolf::game
