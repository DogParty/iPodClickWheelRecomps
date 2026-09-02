// course_select.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

void course_select_enter();

// Screen 5 (0x18005980, 0x18013760, 0x18014b40, 0x18014734).
void course_select_screen_enter();
uint32_t course_select_handle_event(uint32_t event);
void carousel_slide_tick(uint32_t /*milliseconds*/);
uint32_t course_select_render();

}  // namespace minigolf::game
