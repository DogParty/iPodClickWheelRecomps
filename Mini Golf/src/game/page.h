// page.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

void page_enter();  // 0x18005f54

// Screen 7 (0x1800e764, 0x18010c3c).
uint32_t page_handle_event(uint32_t event);
uint32_t page_render();

}  // namespace minigolf::game
