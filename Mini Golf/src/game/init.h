// Start-up of the game proper (init.cpp): what the first tick does before the title screen.
#pragma once

#include <cstdint>

namespace minigolf::game {

// 0x18012268 — reset the game state, read the device's audio and language settings, choose the
// font, load the title image. Called once, on the first tick.
void game_init();

// The language Settings #0 reports, or a negative error code (0x18018b54).
int32_t settings_language();

// The five resource slots (see SLOT_FLAGS): reset them all; take the first free one, or -1.
void slots_reset();
int32_t slot_allocate();

void load_title_and_defaults();  // 0x18006ecc

}  // namespace minigolf::game
