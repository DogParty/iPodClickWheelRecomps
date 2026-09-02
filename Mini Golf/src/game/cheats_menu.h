// The Cheats screen (cheats_menu.cpp), reached from Options.
#pragma once

#include <cstdint>

namespace minigolf::game {

// The kind on the Options row that opens this screen. Options' own rows are kinds 0 to 5, in
// the item table the image carries at OPTIONS_ITEMS; this is the next one.
constexpr uint32_t KIND_CHEATS = 6;

void cheats_enter();

// The screen, in the shape every menu screen has: its enter routine, and the handler that acts
// on Select and Menu.
void cheats_screen_enter();
uint32_t cheats_handle_event(uint32_t event);

}  // namespace minigolf::game
