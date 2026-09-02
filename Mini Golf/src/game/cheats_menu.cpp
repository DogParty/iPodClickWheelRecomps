// The Cheats screen: one toggle row per cheat, Select flips the row under the cursor, Menu
// goes back to Options.
//
// It is an ordinary menu screen — the same item table, tick and renderer every other menu uses
// (menu.cpp) — with two differences, both because these rows say things no resource pack has a
// word for. Their text comes from host_text.h rather than the course pack, and each row builds
// its whole line ("AIM GUIDE: ON") instead of using the game's heading-and-value rows, which
// take both halves from the pack.
//
// It runs under the Options screen's id. Screen ids are the flow's, not the screen's: the id
// selects which wrapper runs the tick and which enter routine the flow uses to rebuild a screen
// it comes back to (flow.cpp, `screen_step` and `screen_enter`), and for both of those the
// right answer here is "whatever Options does". Nothing else reads it. The alternative — a
// sixteenth id — would mean changing the bound `screen_set` asserts on, which is one of the
// original's own invariants.
#include "cheats_menu.h"

#include "cheats.h"
#include "game_state.h"
#include "host_text.h"
#include "menu.h"
#include "screens.h"
#include "state.h"

namespace minigolf::game {

namespace {

// Every row sits where the other sub-menus' rows do: the item tables in the image all carry
// x = MENU_SLIDE_FROM and y = 80 (dumped from OPTIONS_ITEMS and its neighbours), and the menu
// renderer takes the first row's y and steps by the pitch from there.
constexpr uint32_t ROW_Y = 0x50 << 16;

// The label each row shows, in the order cheats.h lists the cheats.
constexpr HostText ROW_TEXT[CHEAT_COUNT] = {
    HostText::UnlockCourses, HostText::NoStrokeLimit, HostText::NoOutOfBounds,
    HostText::AimGuide,      HostText::GhostTrail,
};

}  // namespace

void cheats_enter() {
    MenuItem* table = menu_table();
    for (uint32_t row = 0; row < CHEAT_COUNT; ++row) {
        table[row].text_id = host_text_id(ROW_TEXT[row]);
        table[row].kind = row;  // as the image's own tables number their rows
        table[row].x = MENU_SLIDE_FROM;
        table[row].y = ROW_Y;
        table[row].delay = 0;
        table[row].style = 0;  // a plain row: the label already carries its own value
    }
    menu_items() = table;
    screen_install(cheats_handle_event, menu_screen_tick, menu_render, nullptr);
    screen_set(3);
    menu_open(host_text_id(HostText::CheatsTitle), CHEAT_COUNT);
}

// Select flips the cheat under the cursor and stays on the screen, the way Options' own Music
// and Sound FX rows do. Menu returns to Options with the cursor back on the row that opened
// this screen.
uint32_t cheats_handle_event(uint32_t event) {
    if (event == EVENT_SELECT) {
        const int32_t row = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
        if (row < 0 || row >= static_cast<int32_t>(CHEAT_COUNT)) {
            return 0;
        }
        cheat_toggle(all_cheats()[row]);
        return 0;
    }
    if (event == EVENT_MENU) {
        // `menu_open` starts the cursor on the last row whose kind matches this, which is how
        // the game itself preselects a row. options_enter() puts it back to 0 afterwards.
        menu_state().byte_28 = static_cast<int8_t>(KIND_CHEATS);
        menu_leave_to(options_screen_enter);
    }
    return 0;
}

void cheats_screen_enter() {
    cheats_enter();
    wheel_slots_clear();  // as every other screen's enter does: a press that chose it cannot act
}

}  // namespace minigolf::game
