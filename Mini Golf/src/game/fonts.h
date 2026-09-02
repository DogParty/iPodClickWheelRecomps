// fonts.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

uint32_t font_create(uint32_t glyphs);

uint32_t fonts_load();

}  // namespace minigolf::game
