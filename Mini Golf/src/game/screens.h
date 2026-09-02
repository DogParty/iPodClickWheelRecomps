// The screen object and what every screen shares (screens.cpp).
#pragma once

#include <cstdint>

namespace minigolf::game {

// Make `id` the running screen: remembers the previous one and restarts the frame count
// (0x18004068). Ids are 0..14.
void screen_set(uint32_t id);

// What a screen provides. The original kept these as code addresses in the screen block and
// reached them with `blx`; nothing outside the game ever read them, so they are ordinary
// function pointers now — which also means a screen cannot name a function that does not exist.
using ScreenHandler = uint32_t (*)(uint32_t event);  // 5 = Select, 6 = Menu
using ScreenTick = void (*)(uint32_t milliseconds);
using ScreenStep = uint32_t (*)(uint32_t milliseconds);  // what the flow runs
using ScreenRender = uint32_t (*)();
using ScreenEnter = void (*)();

// The running screen: what handles its events, ticks it, draws it, and what it leads to.
// `render_saved` is the render a dialog or card put aside while it is up.
struct Screen {
    ScreenHandler handler;
    ScreenTick tick;
    ScreenRender render;
    ScreenRender render_saved;
    ScreenEnter next_enter;
};
Screen& current_screen();

// Install a screen's functions.
void screen_install(ScreenHandler handler, ScreenTick tick, ScreenRender render,
                    ScreenEnter next_enter);

// Play one of the ten menu sounds if the settings enable it (0x180047cc).
void menu_sound_play(uint32_t sound);

// Call the running screen's handler with a button event (5 = Select, 6 = Menu).
uint32_t screen_handle_event(uint32_t event);

// The two step routines the flow runs a screen with: the plain one just ticks it; the menu one
// also acts on the selection once the menu has settled (0x18006cb4 and 0x18006688).
uint32_t plain_screen_step(uint32_t milliseconds);

// The tick of a screen that has nothing to do between frames (0x180113c4).
void tick_nothing(uint32_t /*milliseconds*/);
uint32_t menu_screen_step(uint32_t milliseconds);

}  // namespace minigolf::game
