// flow.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

uint32_t language_code();

uint32_t flow_step(uint32_t milliseconds);  // 0x1800ecd8

}  // namespace minigolf::game
