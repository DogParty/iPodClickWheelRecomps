// course.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

void course_load_request(uint32_t course, uint32_t hole);

void course_start(uint32_t resuming);

void course_state_save(uint32_t resumable);

void course_unload();

void game_new();

uint32_t save_record_snapshot();

void hole_start();

void course_resume();

void score_card_open();

// Screen 11's entry (0x1800291c) and tick (0x18002c28): the hole, with any pending input
// forgotten as it opens.
void hole_screen_enter();
void hole_screen_tick(uint32_t milliseconds);
void hole_enter();

}  // namespace minigolf::game
