// dialog.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

void dialog_enter();

// Screen 8 (0x18010654, 0x18012fa8).
uint32_t dialog_handle_event(uint32_t event);
uint32_t dialog_render();

}  // namespace minigolf::game
