// hole_render.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

uint32_t hole_render();  // 0x1800a080

void panel_message_show(uint32_t text_id);

}  // namespace minigolf::game
