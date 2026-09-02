// The pause menu (screen 12): what a round can do besides continue — help, options, the score
// card, statistics, saving, leaving. Which rows appear depends on the game mode: only a
// single-player round can be saved, so the others lose those rows and call "save and exit"
// plain "exit".
#include "pause_menu.h"

#include "calling.h"
#include "course.h"
#include "draw.h"
#include "game_state.h"
#include "libc.h"
#include "menu.h"
#include "page.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"
#include "text.h"
#include "ui.h"

namespace minigolf::game {

void course_state_save(uint32_t resumable);
void music_start(uint32_t in_game);
void course_unload();
void course_images_load();
void main_menu_enter();
void dialog_enter();
void game_save_enter();

namespace {

constexpr uint32_t CARD_ITEMS = 0x1801'9bc4, CARD_ITEMS_SIZE = 0x30;
constexpr uint32_t PAUSE_MENU_ITEMS = 0x1801'9dbc, PAUSE_MENU_ITEMS_SIZE = 0xc0,
                   PAUSE_MENU_ROWS = 8;
constexpr uint32_t TEXT_EXIT = 0x81;  // replaces "save and exit" outside single player
constexpr uint32_t DIALOG_EXIT_GAME = 4, DIALOG_QUIT_GAME = 5;  // dialog.cpp
constexpr uint32_t LAST_ROW_DELAY = 0x23;

// The save-game card (card_render): its panel, and where its two answers sit in it.
constexpr int32_t CARD_PANEL_X = 0xa, CARD_PANEL_WIDTH = 0x12c, PANEL_EXTRA = 0x1e;
constexpr int32_t CARD_ROW_X = 0x46, CARD_FIRST_ROW_Y = 0x14, CARD_ROW_STEP = 0x19;
constexpr int32_t CARD_ROWS = 2;

enum PauseRow : int32_t {
    PAUSE_RESUME,
    PAUSE_VOLUME,
    PAUSE_OPTIONS,
    PAUSE_SCORE_CARD,
    PAUSE_HELP,
    PAUSE_SAVE_GAME,  // "quit game" outside single player
    PAUSE_SAVE_AND_EXIT,
    PAUSE_ABANDON,
};

MenuItem& pause_item(uint32_t row) {
    return menu_table()[row];
}

}  // namespace

// 0x18005874 — the pause menu (screen 12). Eight rows; the modes that cannot save hide the rows
// that would, and call "save and exit" plain "exit".
void pause_menu_enter() {
    screen_state().byte_dcf = static_cast<uint8_t>(1);
    if (play_state().word_638 == 0xffff'ffffu) {
        play_state().word_638 = play_state().state;
    }
    menu_items_load(menu_table(), PAUSE_MENU_ITEMS, PAUSE_MENU_ITEMS_SIZE);
    menu_items() = menu_table();
    switch (static_cast<uint32_t>(static_cast<int8_t>(menu_state().game_mode))) {
    case MODE_SINGLE_PLAYER:
        pause_item(PAUSE_ABANDON).style = STYLE_HIDDEN;
        break;
    case MODE_PASS_N_PLAY:
        pause_item(PAUSE_SAVE_AND_EXIT).style = STYLE_HIDDEN;
        pause_item(PAUSE_SAVE_GAME).text_id = TEXT_EXIT;
        break;
    case MODE_PRACTICE_HOLE:
        pause_item(PAUSE_SCORE_CARD).style = STYLE_HIDDEN;
        pause_item(PAUSE_SAVE_AND_EXIT).style = STYLE_HIDDEN;
        pause_item(PAUSE_SAVE_GAME).text_id = TEXT_EXIT;
        break;
    default:
        break;
    }
    screen_install(pause_menu_handle_event, menu_screen_tick, menu_render, nullptr);
    screen_set(12);
    menu_open(TITLE_PAUSE, PAUSE_MENU_ROWS);
    text_block().score_card_rows_shown = static_cast<uint8_t>(PAUSE_MENU_ROWS);
    pause_item(PAUSE_ABANDON).x = MENU_START_X;
    pause_item(PAUSE_ABANDON).delay = LAST_ROW_DELAY;
}

// 0x1800e160 — the pause menu's rows. Answers 1 when the course is over (abandoned, or exited
// in a mode that does not save) so the flow can leave it.
uint32_t pause_menu_handle_event(uint32_t event) {
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    if (event == EVENT_MENU) {
        text.byte_729 = static_cast<uint8_t>(static_cast<uint32_t>(cursor));
        menu_state().dialog_type = static_cast<uint8_t>(DIALOG_EXIT_GAME);
        dialog_enter();
        return 0;
    }
    if (event != EVENT_SELECT) {
        return 0;
    }
    const bool single_player =
        static_cast<uint32_t>(static_cast<int8_t>(menu_state().game_mode)) == MODE_SINGLE_PLAYER;
    switch (cursor) {
    case PAUSE_RESUME:
        play_state().state = play_state().word_638;
        play_state().word_638 = 0xffff'ffffu;
        current_screen().next_enter = hole_screen_enter;
        screen_state().byte_dcf = static_cast<uint8_t>(0);
        break;
    case PAUSE_VOLUME:
        menu_state().page = static_cast<int8_t>(PAGE_VOLUME);
        current_screen().next_enter = page_screen_enter;
        break;
    case PAUSE_OPTIONS:
        text_block().menu_return_row = static_cast<uint8_t>(0xff);
        current_screen().next_enter = options_screen_enter;
        break;
    case PAUSE_SCORE_CARD:
        text.score_card_flag = static_cast<uint8_t>(1);
        current_screen().next_enter = score_card_open;
        break;
    case PAUSE_HELP:
        text_block().menu_return_row = static_cast<uint8_t>(0xff);
        current_screen().next_enter = help_screen_enter;
        break;
    case PAUSE_SAVE_GAME:
        if (single_player) {
            text_block().menu_return_row = static_cast<uint8_t>(0xff);
            game_save_enter();
        } else {  // "quit game": asks first
            play_state().byte_79a = static_cast<uint8_t>(0);
            menu_state().dialog_type = static_cast<uint8_t>(DIALOG_QUIT_GAME);
            dialog_enter();
        }
        return 0;
    case PAUSE_SAVE_AND_EXIT:
        if (single_player) {
            game_state_block().save_data_byte_5 = static_cast<uint8_t>(1);
            course_state_save(1);
        }
        course_unload();
        course_images_load();
        if (!single_player) {
            return 1;
        }
        menu_state().page = static_cast<int8_t>(PAGE_SAVED);
        page_enter();
        wheel_slots_clear();
        return 0;
    case PAUSE_ABANDON:
        course_unload();
        course_images_load();
        return 1;
    default:
        assert_trap(0x1800e2ccu);
    }
    menu_slide_out_begin();
    screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_OUT);
    text_block().frame_count = 0;
    return 0;
}

// 0x180136bc — the "save and quit?" card's handler: Select on the first answer or Menu saves
// the round as resumable, stops the music and leaves the course for the main menu; Select on
// the second opens the "quit without saving" dialog.
uint32_t card_handle_event(uint32_t event) {
    const bool leave = event == EVENT_MENU ||
                       (event == EVENT_SELECT && static_cast<uint32_t>(menu_state().cursor) == 0);
    if (event == EVENT_SELECT && static_cast<uint32_t>(menu_state().cursor) == 1) {
        play_state().byte_79a = static_cast<uint8_t>(0);
        menu_state().dialog_type = static_cast<uint8_t>(5);
        dialog_enter();
        return 0;
    }
    if (!leave) {
        return 0;
    }
    screen_state().byte_d85 = static_cast<uint8_t>(1);
    course_state_save(0);
    text_block().byte_72b = static_cast<uint8_t>(0xff);
    music_start(0);
    screen_state().byte_dcf = static_cast<uint8_t>(0);
    course_unload();
    course_images_load();
    main_menu_enter();
    return 0;
}

// 0x18014d10 — open the "save game?" card over the hole: the hole's render kept to draw
// behind it, the pause menu to come back to, the card's two items, the panel growing in.
void game_save_enter() {
    current_screen().render_saved = current_screen().render;
    current_screen().next_enter = pause_menu_screen_enter;
    menu_items_load(card_items_table(), CARD_ITEMS, CARD_ITEMS_SIZE);
    menu_items() = card_items_table();
    text_block().byte_72d = static_cast<uint8_t>(0);
    current_screen().handler = card_handle_event;
    current_screen().tick = tick_nothing;
    play_state().panel_growing = static_cast<uint8_t>(1);
    play_state().panel_scale = 0;
    play_state().panel_scale_step = PANEL_SCALE_STEP_VALUE;
    current_screen().render = card_render;
    screen_set(9);
    menu_open(0xffffffffu, 2);
    screen_state().phase = static_cast<uint8_t>(2);
    wheel_slots_clear();  // a tail call: memclr
}

// --- screen entry points ---------------------------------------------------------------------
// Each opens its screen and then forgets any input still pending, so a press that
// chose the screen cannot also act on it.

void pause_menu_screen_enter() {
    pause_menu_enter();
    wheel_slots_clear();
}

// 0x18014300 — the save-game card's render: the background and the logo, a panel sized to its
// two answers, and the answers themselves — the highlighted one with the ball beside it and the
// ripple through its letters, the other plain. Answers 1, as every render does.
//
// The card carries no message of its own: `game_save_enter` loads the two items and the question
// is the panel's first row. That is why this is shorter than `dialog_render`, which has a message
// to lay out above its choices.
uint32_t card_render() {
    FontRecord& font = as_font(screen_state().font_object);
    const int32_t rows = static_cast<int32_t>(static_cast<int8_t>(menu_state().item_count));
    const int32_t panel_height =
        rows * static_cast<int32_t>(font.line_height) + static_cast<int32_t>(PANEL_EXTRA);
    const int32_t panel_y = halve(static_cast<int32_t>(SCREEN_HEIGHT) - panel_height);

    background_draw(Blend::Alpha);
    logo_draw(Blend::Alpha);
    if (panel_scale_step()) {  // still growing or shrinking: only the panel this frame
        panel_draw_scaled(CARD_PANEL_X, panel_y, CARD_PANEL_WIDTH, panel_height);
        return 1;
    }
    panel_draw(CARD_PANEL_X, panel_y, CARD_PANEL_WIDTH, panel_height);

    const MenuItem* items = menu_items();
    if (items == nullptr) {
        assert_trap(0x18014490u);
    }
    const int32_t cursor = static_cast<int32_t>(static_cast<int8_t>(menu_state().cursor));
    int32_t row_y = panel_y + static_cast<int32_t>(CARD_FIRST_ROW_Y);
    for (int32_t row = 0; row < static_cast<int32_t>(CARD_ROWS); ++row) {
        resource_load(as_pack(game_state_block().pack_handle), items[row].text_id, SCRATCH_TEXT,
                      0x800);
        if (cursor == row) {
            highlighted_row_draw(font, CARD_ROW_X, row_y, true);
        } else {
            text_draw(font, SCRATCH_TEXT, CARD_ROW_X, row_y, Align::Left);
        }
        row_y += static_cast<int32_t>(CARD_ROW_STEP);
    }
    text_block().byte_72d = static_cast<uint8_t>(1);
    return 1;
}

}  // namespace minigolf::game
