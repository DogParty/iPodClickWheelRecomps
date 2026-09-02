// text.cpp, as other files call it.
#pragma once

#include "state.h"

#include <cstdint>

namespace minigolf::game {

uint32_t text_layout(FontRecord& font_at, uint32_t text, uint32_t out, uint32_t max_lines,
                     int32_t width);

}  // namespace minigolf::game
