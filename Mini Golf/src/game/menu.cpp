// The menu screens: a table of items that slides in, waits for the wrapper's cursor and
// selection handling (screens.cpp), and slides out into the next screen's enter routine.
//
// One generic tick (0x1800c8d0) serves every menu; what differs is the item table each enter
// routine installs and the handler that acts on Select and Menu. Decompiled so far: the main
// menu (enter 0x1800553c, handler 0x1800d058) and the "back" handler 0x1800cfa8 that several
// sub-menus share.
//
// Long because every menu in the game is this one screen with a different item table; the
// handlers are here so that what each row does is next to the table that lists it.
#include "menu.h"

#include "calling.h"
#include "cheats_menu.h"
#include "course.h"
#include "course_select.h"
#include "dialog.h"
#include "fixed.h"
#include "framework/audio.h"
#include "game_state.h"
#include "hole_load.h"
#include "host_text.h"
#include "libc.h"
#include "name_entry.h"
#include "page.h"
#include "pause_menu.h"
#include "random.h"
#include "records.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"
#include "strings.h"
#include "title.h"

namespace minigolf::game {

// Defined below; the enter routines name them when they install a screen.
uint32_t options_handle_event(uint32_t event);
uint32_t game_modes_handle_event(uint32_t event);
uint32_t hole_select_handle_event(uint32_t event);
uint32_t help_handle_event(uint32_t event);

void music_start(uint32_t in_game);

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t MAX_VISIBLE_ROWS = 7;
constexpr uint32_t SLIDE_DELAY_STEP = 5;  // frames between one item's start and the next
constexpr int32_t SLIDE_OUT_MARGIN = 0x1e;
constexpr uint32_t SCRATCH_TEXT_2 = DIALOG_MESSAGE;  // the slide-out's second scratch buffer
constexpr uint32_t FONT_WIDE_THRESHOLD = 0xff;

MenuItem& item(uint32_t index) {
    return menu_items()[index];
}
int32_t first_row() {
    return static_cast<int32_t>(static_cast<uint32_t>(menu_state().first_row));
}
int32_t visible_rows() {
    return static_cast<int32_t>(static_cast<uint32_t>(menu_state().visible_rows));
}
uint32_t save_flag() {
    return static_cast<uint32_t>(game_state_block().save_data_byte_5);
}

// One frame of an item's travel: wait out its delay, then move 30 pixels.
void item_advance(MenuItem& entry) {
    const uint32_t delay = entry.delay;
    if (delay != 0) {
        entry.delay = delay - 1;
    } else {
        entry.x = entry.x - MENU_SLIDE_STEP;
    }
}

// The main menu's entries, by kind. Returns the handler's answer.
uint32_t main_menu_select(uint32_t cursor) {
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    const uint32_t kind = item(cursor).kind;
    switch (kind) {
    case 0:  // resume the saved course
        if (save_flag() != 1) {
            assert_trap(0x1800d11cu);
        }
        current_screen().next_enter = resume_saved_course;
        break;
    case 1:
        text.menu_return_row = static_cast<uint8_t>(0xff);
        current_screen().next_enter = game_modes_screen_enter;
        break;
    case 2:
        menu_state().page = static_cast<int8_t>(PAGE_VOLUME);
        current_screen().next_enter = page_screen_enter;
        break;
    case 3:
        text.menu_return_row = static_cast<uint8_t>(0xff);
        current_screen().next_enter = options_screen_enter;
        break;
    case 4:
        text.menu_return_row = static_cast<uint8_t>(0xff);
        menu_state().page = static_cast<int8_t>(PAGE_STATISTICS);
        current_screen().next_enter = page_screen_enter;
        break;
    case 5:
        text.menu_return_row = static_cast<uint8_t>(0xff);
        current_screen().next_enter = help_screen_enter;
        break;
    case 6: {  // quit: remember the row and answer 1 (the flow suspends)
        const int32_t next_row = static_cast<int8_t>(static_cast<int32_t>(cursor) + 1);
        game_state_block().loaded[154] =
            static_cast<int8_t>(save_flag() == 1 ? cursor : static_cast<uint32_t>(next_row));
        menu_slide_out_begin();
        screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_OUT);
        text_block().frame_count = 0;
        return 1;
    }
    default:
        assert_trap(0x1800d19cu);
    }
    menu_slide_out_begin();
    screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_OUT);
    text_block().frame_count = 0;
    return 0;
}

}  // namespace

// The menu item tables. The original built them in the game state block; the game is their
// only reader, so they live here (state.h declares them).
MenuItem* menu_table() {
    static MenuItem table[MENU_TABLE_ROWS];
    return table;
}
MenuItem* menu_table_alt() {
    return menu_table() + 1;
}
MenuItem* card_items_table() {
    static MenuItem table[2];
    return table;
}
MenuItem*& menu_items() {
    static MenuItem* items = nullptr;
    return items;
}

// Copy one of the image's item tables into a live table.
void menu_items_load(MenuItem* into, uint32_t image_items, uint32_t bytes) {
    for (uint32_t row = 0; row * menu_item::SIZE < bytes; ++row) {
        into[row] = guest<MenuItem>(image_items + row * menu_item::SIZE);
    }
}

// Slide out towards another screen.
void menu_leave_to(ScreenEnter enter) {
    current_screen().next_enter = enter;
    menu_slide_out_begin();
    screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_OUT);
    text_block().frame_count = 0;
}

// The heading rows carry a second, state-dependent text (the setting's current value).
uint32_t heading_value_text(uint32_t kind) {
    switch (kind) {
    case 0: {
        const int32_t music = static_cast<int32_t>(static_cast<uint32_t>(options_state().music));
        return music == 0 ? TEXT_OFF : music == 1 ? TEXT_ON : TEXT_AUTO;
    }
    case 1:
        return static_cast<uint32_t>(options_state().sound_fx) == 0 ? TEXT_OFF : TEXT_ON;
    case 2:
        return static_cast<uint32_t>(options_state().clock_battery) == 1 ? TEXT_ON : TEXT_OFF;
    case 4: {
        const int32_t gender =
            static_cast<int32_t>(static_cast<uint32_t>(options_state().player_gender));
        if (gender < 0 || gender > 2) {
            assert_trap(0x1800cabcu);
        }
        return TEXT_GENDER_FIRST + static_cast<uint32_t>(gender);  // random, female, male
    }
    default:
        return 0;  // kind 3 has no value text
    }
}

void menu_open(uint32_t title_text, uint32_t visible) {
    const MenuItem* items = menu_items();
    if (items == nullptr) {
        assert_trap(0x18012eecu);
    }
    menu_state().first_row = static_cast<int8_t>(0);
    menu_state().item_count = static_cast<uint8_t>(visible);
    menu_state().visible_rows = static_cast<int8_t>(visible);
    const int32_t count = static_cast<int32_t>(static_cast<int8_t>(menu_state().item_count));
    if (count > static_cast<int32_t>(MAX_VISIBLE_ROWS)) {
        menu_state().visible_rows = static_cast<int8_t>(MAX_VISIBLE_ROWS);
    }
    menu_state().title_text = title_text;

    // The cursor starts on the last item of the preselected kind, else the first item.
    const int32_t wanted = static_cast<int32_t>(static_cast<uint32_t>(menu_state().byte_28));
    int32_t cursor = -1;
    for (int32_t i = 0; i < count; ++i) {
        if (static_cast<int32_t>(items[i].kind) == wanted) {
            cursor = static_cast<int8_t>(i);
        }
    }
    menu_state().cursor = static_cast<int8_t>(static_cast<uint8_t>(cursor == -1 ? 0 : cursor));

    const int32_t last = first_row() + visible_rows();
    for (int32_t i = 0; i < last; ++i) {
        MenuItem& entry = menu_items()[i];
        entry.x = MENU_START_X;
        entry.delay =
            static_cast<uint32_t>((i - first_row()) * static_cast<int32_t>(SLIDE_DELAY_STEP));
    }
    menu_state().dialog_type = static_cast<uint8_t>(0);
    screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_IN);
}

void menu_text_load(uint32_t text_id, uint32_t pack_handle, uint32_t destination) {
    if (is_host_text(text_id)) {
        host_text_load(text_id, destination);
        return;
    }
    resource_load(as_pack(pack_handle), text_id, destination, 0x800);
}

void menu_slide_out_begin() {
    const int32_t first = first_row();
    const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    for (int32_t i = first; i < first + visible_rows(); ++i) {
        item(static_cast<uint32_t>(i)).delay =
            i == cursor
                ? 0xffff'ffffu
                : static_cast<uint32_t>((i - first) * static_cast<int32_t>(SLIDE_DELAY_STEP));
    }
}

uint32_t text_width(FontRecord& font, uint32_t text) {
    const uint32_t trap_for[] = {0x18007850u, 0x18007860u, 0x18007870u, 0x18007880u, 0x18007890u};
    const uint32_t fields[] = {font.widths, font.cell_width, font.cell_height, font.last_code,
                               font.advances};
    for (unsigned i = 0; i < 5; ++i) {
        if (fields[i] == 0) {
            assert_trap(trap_for[i]);
        }
    }
    if (text == 0) {
        assert_trap(0x1800789cu);
    }
    const bool wide =
        static_cast<int32_t>(font.cell_width) > static_cast<int32_t>(FONT_WIDE_THRESHOLD);
    const uint32_t widths = font.advances;
    if ((wide ? wide_string_length(text) : string_length(text)) == 0) {
        return 0;
    }
    uint32_t width = 0;
    for (uint32_t i = 0;; ++i) {
        const int32_t c = wide ? guest<int16_t>(text + 2 * i) : guest<int8_t>(text + i);
        if (c == 0 || c == '\n') {
            break;
        }
        width += static_cast<uint32_t>(guest_array<int8_t>(widths)[c - 0x20]);
    }
    return width;
}

// 0x1800c8d0 — the generic menu tick. Returns true when the slide-out has finished and the
// next screen's enter routine is due (the caller makes that call once this frame is gone).
bool menu_tick() {
    const int32_t first = first_row();
    const int32_t last = first + visible_rows();
    const uint32_t phase = static_cast<uint32_t>(static_cast<int8_t>(screen_state().phase));

    if (phase == PHASE_SLIDE_IN) {
        int32_t settled = 0;
        for (int32_t i = first; i < last; ++i) {
            MenuItem& entry = item(static_cast<uint32_t>(i));
            item_advance(entry);
            if (static_cast<int32_t>(entry.x) <= static_cast<int32_t>(MENU_SLIDE_FROM)) {
                entry.x = MENU_SLIDE_FROM;
                entry.delay = 0xffff'ffffu;
                ++settled;
            }
        }
        if (settled == visible_rows()) {
            screen_state().phase = static_cast<uint8_t>(PHASE_STEADY);
        }
        return false;
    }
    if (phase != PHASE_SLIDE_OUT) {
        return false;
    }

    // Slide out: each row leaves once its text has cleared the screen, so the row's width is
    // measured from its (re-loaded) text.
    int32_t gone = 0;
    for (int32_t i = first; i < last; ++i) {
        MenuItem& entry = item(static_cast<uint32_t>(i));
        const uint32_t style = entry.style;
        uint32_t pack = game_state_block().pack_handle;
        if (style == STYLE_COURSE_NAME) {
            const int32_t course = static_cast<int32_t>(static_cast<uint32_t>(menu_state().course));
            pack = game_state_block().pack_course[static_cast<uint32_t>(course)];
        }
        menu_text_load(entry.text_id, pack, SCRATCH_TEXT);
        uint32_t width = text_width(as_font(screen_state().font_object), SCRATCH_TEXT) + 2;
        if (style == STYLE_HEADING) {
            guest<uint16_t>(SCRATCH_TEXT_2) = static_cast<uint16_t>(0);
            const uint32_t value_text = heading_value_text(entry.kind);
            if (value_text != 0) {
                resource_load(as_pack((game_state_block().pack_handle)), value_text, SCRATCH_TEXT_2,
                              0x800);
                width +=
                    text_width(as_font(ld32(SCREEN_OBJECT + screen::FONT_OBJECT)), SCRATCH_TEXT_2);
            }
        }
        item_advance(entry);
        const int32_t right_edge =
            static_cast<int32_t>(width) + (to_whole(entry.x)) + SLIDE_OUT_MARGIN;
        if (right_edge < 0) {
            entry.delay = 0xffff'ffffu;
            ++gone;
        } else if (static_cast<int32_t>(entry.delay) < 0) {
            ++gone;
        }
    }
    if (gone != visible_rows()) {
        return false;
    }
    if (current_screen().next_enter == nullptr) {
        assert_trap(0x1800cb58u);
    }
    return true;
}

// 0x1800553c — open the main menu: the course's own images loaded, the item table copied in
// (without its "resume" row when there is no saved course), and the rows set sliding.
void main_menu_enter() {
    if (game_state_block().pack_handle == 0) {
        assert_trap(0x18005550u);
    }
    const int32_t course = static_cast<int32_t>(menu_state().course);
    if (game_state_block().pack_course[static_cast<uint32_t>(course)] == 0) {
        assert_trap(0x1800556cu);
    }
    course_images_load();
    menu_items_load(menu_table(), MAIN_MENU_ITEMS, MAIN_MENU_ITEMS_SIZE);
    menu_items() = menu_table();
    uint32_t rows = 7;
    if (save_flag() == 0) {  // no saved course: the table without its "resume" row
        menu_items() = menu_table_alt();
        rows = 6;
    }
    screen_install(main_menu_handle_event, menu_screen_tick, menu_render, nullptr);
    screen_set(1);
    menu_open(0xffff'ffffu, rows);
    wheel_slots_clear();  // memclr
}

// 0x1800d058 — the main menu's handler.
uint32_t main_menu_handle_event(uint32_t event) {
    if (menu_items() == nullptr) {
        assert_trap(0x1800d06cu);
    }
    const uint32_t cursor =
        static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor)));
    if (event == EVENT_SELECT) {
        return main_menu_select(cursor);
    }
    if (event == EVENT_MENU) {
        const int32_t next_row = static_cast<int8_t>(static_cast<int32_t>(cursor) + 1);
        game_state_block().loaded[154] =
            static_cast<int8_t>(save_flag() == 1 ? cursor : static_cast<uint32_t>(next_row));
        menu_state().dialog_type = static_cast<uint8_t>(1);
        dialog_enter();
    }
    return 0;
}

// 0x1800cfa8 — Help: Select opens the chosen help page (pages 3, 4, 5: controls, how to play,
// about);
// Menu returns to whichever menu opened the screen.
uint32_t help_handle_event(uint32_t event) {
    const uint32_t text = GAME_STATE + game_state::TEXT;
    if (event == EVENT_SELECT) {
        const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
        if (cursor < 0 || cursor > 2) {
            assert_trap(0x1800d040u);
        }
        menu_state().page = static_cast<uint8_t>(
            static_cast<int8_t>(PAGE_HELP_FIRST + static_cast<uint32_t>(cursor)));
        current_screen().next_enter = page_screen_enter;
    } else if (event == EVENT_MENU) {
        const bool after_course = static_cast<uint32_t>(screen_state().byte_dcf) == 1;
        current_screen().next_enter =
            after_course ? pause_menu_screen_enter : main_menu_screen_enter;
        as_text(text).menu_return_row = static_cast<uint8_t>(after_course ? 4 : 5);
    } else {
        return 0;
    }
    menu_slide_out_begin();
    screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_OUT);
    text_block().frame_count = 0;
    return 0;
}

// --- the smaller menus: enter routines and handlers --------------------------------------------

namespace {

constexpr uint32_t OPTIONS_ITEMS = 0x1801'9ce4, OPTIONS_ITEMS_SIZE = 0x90;
constexpr uint32_t OPTIONS_ROWS = OPTIONS_ITEMS_SIZE / menu_item::SIZE;  // the image's six
constexpr uint32_t GAME_MODES_ITEMS = 0x1801'9c9c, GAME_MODES_ITEMS_SIZE = 0x48;
constexpr uint32_t HELP_ITEMS = 0x1801'9d74, HELP_ITEMS_SIZE = 0x48;
constexpr uint32_t HOLE_SELECT_ITEMS = 0x1801'9e7c, HOLE_SELECT_ITEMS_SIZE = 0x1b0;

// Still recompiled, named by their use here (inferred).

constexpr uint32_t DIALOG_RESET_GAME = 2, DIALOG_NEW_GAME_ERASES_SAVE = 3;  // dialog.cpp

// The shape every small menu's enter routine shares: copy the item table, install the
// functions, set the screen id, open the menu, forget any input.
void small_menu_enter(uint32_t items, uint32_t items_size, ScreenHandler handler,
                      uint32_t screen_id, uint32_t title_text, uint32_t rows) {
    menu_items_load(menu_table(), items, items_size);
    menu_items() = menu_table();
    screen_install(handler, menu_screen_tick, menu_render, nullptr);
    screen_set(screen_id);
    menu_open(title_text, rows);
}

}  // namespace

// 0x180057d4 — Options (screen 3): Music, Sound FX, Clock/Batt, Brightness, Player, Reset Game,
// and this port's Cheats. From the pause menu (BYTE_DCF = 1) only the first four rows are
// offered.
//
// Cheats goes last because a menu shows a contiguous window of its table — there is no way to
// offer a row the pause menu's four-row window skips past — and because it belongs with Player
// and Reset Game among the rows the pause menu already leaves out: things to set up a game
// with, not things to change in the middle of one.
void options_enter() {
    game_state_block().loaded[155] = static_cast<int8_t>(1);
    const bool from_pause_menu = static_cast<uint32_t>(screen_state().byte_dcf) == 1;
    const bool with_cheats = !from_pause_menu && !port_additions_hidden();
    const uint32_t rows = from_pause_menu ? 4 : OPTIONS_ROWS + (with_cheats ? 1 : 0);
    menu_items_load(menu_table(), OPTIONS_ITEMS, OPTIONS_ITEMS_SIZE);
    if (with_cheats) {
        MenuItem& cheats = menu_table()[OPTIONS_ROWS];
        cheats.text_id = host_text_id(HostText::CheatsRow);
        cheats.kind = KIND_CHEATS;
        cheats.x = MENU_SLIDE_FROM;
        cheats.y = menu_table()[0].y;  // every row of a table carries the same start position
        cheats.delay = 0;
        cheats.style = 0;  // a plain row: it opens a screen rather than showing a value
    }
    menu_items() = menu_table();
    screen_install(options_handle_event, menu_screen_tick, menu_render, nullptr);
    screen_set(3);
    menu_open(TITLE_OPTIONS, rows);
    // `menu_open` has placed the cursor. Nothing in the game ever writes this byte — it is zero
    // from start-up on — so putting it back to zero leaves every later menu exactly as it was,
    // while letting the Cheats screen ask to come back to its own row (cheats_menu.cpp).
    menu_state().byte_28 = static_cast<int8_t>(0);
}

// 0x18005bc0 — Game Modes (screen 2): Single Player, Pass 'n Play, Practice Hole.
void game_modes_enter() {
    small_menu_enter(GAME_MODES_ITEMS, GAME_MODES_ITEMS_SIZE, game_modes_handle_event, 2,
                     TITLE_GAME_MODES, 3);
}

// 0x180054bc — Help (screen 4): three pages to choose from; Menu is the only way out.
void help_enter() {
    small_menu_enter(HELP_ITEMS, HELP_ITEMS_SIZE, help_handle_event, 4, TITLE_HELP, 3);
}

// 0x18005c40 — Select Hole (screen 6), for Practice Hole: eighteen rows named after the
// chosen course's holes.
void hole_select_enter() {
    menu_items_load(menu_table(), HOLE_SELECT_ITEMS, HOLE_SELECT_ITEMS_SIZE);
    menu_items() = menu_table();
    const int32_t course = static_cast<int32_t>(menu_state().course);
    const uint32_t first_hole_text = course_info_at(course).first_hole_text;
    for (uint32_t hole = 0; hole < HOLES_PER_COURSE; ++hole) {
        menu_table()[hole].text_id = first_hole_text + hole;
    }
    screen_install(hole_select_handle_event, menu_screen_tick, menu_render, nullptr);
    screen_set(6);
    menu_open(TITLE_SELECT_HOLE, HOLES_PER_COURSE);
}

// 0x180118e4 — a hole picked: on to the course; Menu returns to the course carousel.
uint32_t hole_select_handle_event(uint32_t event) {
    if (event == EVENT_SELECT) {
        const int32_t hole = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
        text_block().hole = static_cast<uint8_t>(static_cast<uint32_t>(hole));
        if (hole < 0) {
            assert_trap(0x18011944u);
        }
        if (hole >= static_cast<int32_t>(HOLES_PER_COURSE)) {
            assert_trap(0x18011950u);
        }
        menu_leave_to(hole_enter);
    } else if (event == EVENT_MENU) {
        menu_state().cursor = static_cast<int8_t>(0xffu);
        text_block().menu_return_row =
            static_cast<uint8_t>(static_cast<uint32_t>(menu_state().course));
        menu_leave_to(course_select_screen_enter);
    }
    return 0;
}

// 0x1801181c — a game mode picked. Single Player with a game already saved asks first (the
// dialog then leads on to course select); the other modes start a fresh round at once.
uint32_t game_modes_handle_event(uint32_t event) {
    const uint32_t text = GAME_STATE + game_state::TEXT;
    if (event == EVENT_MENU) {
        text_block().menu_return_row = static_cast<uint8_t>(1);
        menu_leave_to(main_menu_screen_enter);
        return 0;
    }
    if (event != EVENT_SELECT) {
        return 0;
    }
    const int32_t row = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    if (row < 0 || row > 2) {
        assert_trap(0x180118d0u);
    }
    menu_state().game_mode = static_cast<uint8_t>(static_cast<uint32_t>(row));
    if (row == MODE_SINGLE_PLAYER && save_flag() == 1) {
        menu_state().dialog_type = static_cast<uint8_t>(DIALOG_NEW_GAME_ERASES_SAVE);
        dialog_enter();
        return 0;
    }
    game_new();
    if (row == MODE_PASS_N_PLAY) {
        as_text(text).multiplayer = static_cast<uint8_t>(1);
    }
    menu_leave_to(course_select_screen_enter);
    return 0;
}

// Cycles a three-valued option byte and reports whether it wrapped back to 0.
bool option_cycle(int8_t& option) {
    const int32_t next = static_cast<int8_t>(static_cast<int32_t>(option) + 1);
    option = static_cast<int8_t>(next == 3 ? 0 : next);
    return next == 3 || next == 0;
}

// 0x1800f7f0 — the Options screen. Select changes the row's value (and marks the options
// dirty); Menu saves them and returns to whichever menu opened the screen.
uint32_t options_handle_event(uint32_t event) {
    const bool from_pause_menu = static_cast<uint32_t>(screen_state().byte_dcf) == 1;
    if (event == EVENT_MENU) {
        game_state_block().loaded[155] = static_cast<int8_t>(0);
        current_screen().next_enter =
            from_pause_menu ? pause_menu_screen_enter : main_menu_screen_enter;
        text_block().menu_return_row = static_cast<uint8_t>(from_pause_menu ? 2 : 3);
        if (options_scratch().changed != 0) {
            options_scratch().changed = 0;  // 0x18004a5c: the save hook, which only answers 1
        }
        menu_slide_out_begin();
        screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_OUT);
        text_block().frame_count = 0;
        return 0;
    }
    if (event != EVENT_SELECT) {
        return 0;
    }
    const int32_t row = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    switch (row) {
    case 0: {  // Music: off, on, auto
        const bool off = option_cycle(options_state().music);
        if (off &&
            static_cast<uint32_t>(static_cast<uint8_t>(game_state_block().loaded[248])) == 0) {
            audio::stop_music();  // 0x18004a64: stop the music
            menu_state().byte_2b = static_cast<uint8_t>(0xff);
        } else {
            music_start(from_pause_menu ? 1u : 0u);
        }
        break;
    }
    case 1:  // Sound FX
        options_state().sound_fx =
            static_cast<uint8_t>(static_cast<uint32_t>(options_state().sound_fx) == 0 ? 1 : 0);
        break;
    case 2:  // Clock/Batt
        options_state().clock_battery =
            static_cast<uint8_t>(static_cast<uint32_t>(options_state().clock_battery) ^ 1);
        break;
    case 3:  // Brightness: a slider page
        menu_state().page = static_cast<int8_t>(PAGE_BRIGHTNESS);
        current_screen().next_enter = page_screen_enter;
        menu_slide_out_begin();
        screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_OUT);
        text_block().frame_count = 0;
        return 0;
    case 4: {  // Player: random, female, male — decided now for the current round
        if (option_cycle(options_state().player_gender)) {
            const uint32_t roll = random_next(game_state_block().object_24, 0x64);
            menu_state().player_gender = static_cast<uint16_t>(roll & 1);
        } else {
            menu_state().player_gender = static_cast<uint16_t>(
                static_cast<uint32_t>(options_state().player_gender) == 1 ? 0 : 1);
        }
        break;
    }
    case 5:  // Reset Game: asks first
        menu_state().dialog_type = static_cast<uint8_t>(DIALOG_RESET_GAME);
        dialog_enter();
        return 0;
    case 6:  // Cheats: this port's own screen (cheats_menu.cpp)
        menu_leave_to(cheats_screen_enter);
        return 0;
    default:
        assert_trap(0x1800f994u);
    }
    options_scratch().changed = 1;
    return 0;
}

// --- screen entry points ---------------------------------------------------------------------
// Each opens its screen and then forgets any input still pending, so a press that
// chose the screen cannot also act on it.

void options_screen_enter() {
    options_enter();
    wheel_slots_clear();
}

void game_modes_screen_enter() {
    game_modes_enter();
    wheel_slots_clear();
}

void help_screen_enter() {
    help_enter();
    wheel_slots_clear();
}

void hole_select_screen_enter() {
    hole_select_enter();
    wheel_slots_clear();
}

// 0x1800c8d0 — a menu's tick: the rows slide, and when they have all gone the screen the menu
// chose is entered (a tail call in the original).
void menu_screen_tick(uint32_t /*milliseconds*/) {
    if (menu_tick()) {
        current_screen().next_enter();
    }
}

void main_menu_screen_enter() {
    main_menu_enter();
    music_start(0);  // a tail call in the original
}

}  // namespace minigolf::game
