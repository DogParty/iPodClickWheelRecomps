// pause_menu.cpp, as other files call it: screen 12 and the save-game card over it.
#pragma once

#include <cstdint>

namespace minigolf::game {

void pause_menu_enter();                     // 0x18005874
uint32_t card_handle_event(uint32_t event);  // 0x180136bc
uint32_t card_render();                      // 0x18014300

}  // namespace minigolf::game
