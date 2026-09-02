// The menu screens: sliding item lists with a handler per menu (menu.cpp).
#pragma once

#include "game_state.h"
#include "screens.h"
#include "state.h"

#include <cstdint>

namespace minigolf::game {

// What the menu screens share (menu.cpp, pause_menu.cpp, menu_render.cpp).
// A menu screen's tick and render (0x1800c8d0, 0x1800c1dc), and the handlers of the menus.
void menu_screen_tick(uint32_t /*milliseconds*/);
uint32_t menu_render();
uint32_t main_menu_handle_event(uint32_t event);
uint32_t pause_menu_handle_event(uint32_t event);

// The enter routines the menus hand over to. Each is what the original's dispatch address did:
// the screen's own enter, and for most of them the pending input cleared after it.
// The pages the page screen (page.cpp) shows, by menu::PAGE.
constexpr uint32_t PAGE_STATISTICS = 1, PAGE_RESET_DONE = 2, PAGE_HELP_FIRST = 3, PAGE_SAVED = 6,
                   PAGE_VOLUME = 0xc, PAGE_BRIGHTNESS = 0xd;
// The screens a menu can lead to. ENTER_PAGE shows whichever page MENU + PAGE names.
void page_screen_enter();           // 0x18005f54
void game_modes_screen_enter();     // 0x18005bc0
void options_screen_enter();        // 0x180057d4
void help_screen_enter();           // 0x180054bc
void main_menu_screen_enter();      // 0x1800553c
void pause_menu_screen_enter();     // 0x18005874
void course_select_screen_enter();  // 0x18005980
void hole_select_screen_enter();    // 0x18005c40
constexpr uint32_t PHASE_SLIDE_IN = 1, PHASE_STEADY = 2, PHASE_SLIDE_OUT = 3;
constexpr uint32_t EVENT_SELECT = 5, EVENT_MENU = 6;
// Text resource ids of the option values (jdmg.en order).
constexpr uint32_t TEXT_ON = 0x70, TEXT_OFF = 0x71, TEXT_AUTO = 0x7d, TEXT_GENDER_FIRST = 0x4e;
constexpr uint32_t MENU_START_X = 0x1400000;  // 320 pixels out, 16.16
constexpr uint32_t STYLE_HEADING = 1, STYLE_COURSE_NAME = 2, STYLE_HIDDEN = 3;  // menu_item::STYLE
// The menus, by the title text each shows (menu::TITLE_TEXT); -1 means no title.
constexpr uint32_t TITLE_GAME_MODES = 0x2a, TITLE_HELP = 0x2d, TITLE_PAUSE = 0x3b,
                   TITLE_SELECT_HOLE = 0x56, TITLE_OPTIONS = 0x6a;

// Start a menu over the installed item table: `visible_rows` rows (at most 7), the cursor on
// the item whose kind matches MENU + 0x28, items stacked off-screen to slide in (0x18012ed4).
void menu_open(uint32_t title_text, uint32_t visible);

// A title or a row's text into `destination`: out of the pack at `pack_handle`, or out of this
// port's own labels when the id is a host one (host_text.h). Both the renderer and the
// slide-out's width measurement go through here, so a row this port added behaves exactly like
// a pack row.
void menu_text_load(uint32_t text_id, uint32_t pack_handle, uint32_t destination);

// Begin sliding out towards `enter`, the screen to run once the slide is done.
void menu_leave_to(ScreenEnter enter);

// The text resource showing an option's current value, by the heading row's kind.
uint32_t heading_value_text(uint32_t kind);

// Begin sliding the menu out, the selected item staying put (0x180104bc).
void menu_slide_out_begin();

// Width in pixels of a line of text in a font (0x18007830).
uint32_t text_width(FontRecord& font, uint32_t text);

void game_modes_enter();

void hole_select_enter();

void main_menu_enter();

void options_enter();

}  // namespace minigolf::game
