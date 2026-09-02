// The per-screen tick routines (screen_ticks.cpp): each is given the milliseconds since the
// last frame and answers 1 when the flow should suspend.
#pragma once

#include <cstdint>

namespace minigolf::game {

uint32_t page_tick(uint32_t milliseconds);           // 0x18006ce4
uint32_t dialog_tick(uint32_t milliseconds);         // 0x18004b4c
uint32_t name_entry_tick(uint32_t milliseconds);     // 0x180068fc
uint32_t course_select_tick(uint32_t milliseconds);  // 0x18006afc

}  // namespace minigolf::game
