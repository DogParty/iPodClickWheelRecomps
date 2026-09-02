// title.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

void title_enter();

// 0x1800cf60 — the main menu's "resume" and the title's saved-course exit: back into the saved
// course when it is the current one, otherwise load it.
void resume_saved_course();

}  // namespace minigolf::game
