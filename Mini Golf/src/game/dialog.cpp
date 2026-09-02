// The dialog screen: a message box with the player's name, a text resource, and choices.
//
// Screen 8. `dialog_enter` (0x180051d0) is reached from other screens with a dialog type in
// MENU + DIALOG_TYPE (1..6): it assembles the message — the player's name, two line breaks,
// and a text resource chosen by the type — lays it out, installs the dialog's choice table,
// and remembers which screen to enter afterwards. The screen that was showing keeps its
// render (RENDER_SAVED) so the dialog can draw over it.
#include "dialog.h"

#include "calling.h"
#include "course.h"
#include "course_select.h"
#include "draw.h"
#include "game_state.h"
#include "hole_load.h"
#include "libc.h"
#include "menu.h"
#include "name_entry.h"
#include "page.h"
#include "pause_menu.h"
#include "resources.h"
#include "round_history.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"
#include "strings.h"
#include "text.h"
#include "ui.h"

namespace minigolf::game {

void music_start(uint32_t in_game);

void save_reset(uint32_t reset_options, uint32_t even_in_progress, uint32_t forget_screen);

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t LINE_BREAK = 0x1800'5488;  // "\n", in both encodings
constexpr uint32_t SPACE = 0x1800'5494;       // " "

enum DialogType : uint32_t {
    DIALOG_WELCOME = 1,     // after the title: "hello <name>" — leads to the menu or name entry
    DIALOG_RESET_GAME = 2,  // "this will reset your save game data" — leads back to options
    DIALOG_NEW_GAME = 3,  // "starting a new game will erase your saved game" — on to course select
    DIALOG_EXIT_GAME =
        4,  // "save and exit?" / "quit this game?" by the game mode; to the pause menu
    DIALOG_QUIT_GAME = 5,  // "quit this game?"; single player adds "progress will not be saved"
    DIALOG_NAME_CONFIRMED = 6,  // "is <name> right?" — leads back to name entry
};

bool wide_text() {
    return static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
}

void message_copy(uint32_t source) {
    if (wide_text()) {
        wide_string_copy(DIALOG_MESSAGE, source);
    } else {
        string_copy(DIALOG_MESSAGE, source);
    }
}

void message_append(uint32_t source) {
    wide_text() ? (void)wide_string_append(DIALOG_MESSAGE, source)
                : (void)string_append(DIALOG_MESSAGE, source);
}

uint32_t name_length() {
    return wide_text() ? wide_string_length(SCREEN_OBJECT + screen::PLAYER_NAME)
                       : string_length(SCREEN_OBJECT + screen::PLAYER_NAME);
}

}  // namespace

// 0x180051d0 — open the dialog over whatever is on screen: the message its type names (with
// the player's name folded in for some), two choices, and the panel growing in. The screen it
// covers keeps its render, so the dialog is drawn on top of a still frame.
void dialog_enter() {
    const int32_t type = static_cast<int32_t>(static_cast<int8_t>(menu_state().dialog_type));
    if (type <= 0) {
        assert_trap(0x180051e4u);
    }
    if (type >= 7) {
        assert_trap(0x180051f0u);
    }
    current_screen().render_saved = current_screen().render;
    PackRecord& pack = as_pack(game_state_block().pack_handle);
    uint32_t text_id = 0x5e;
    bool with_name = false;

    switch (type) {
    case DIALOG_WELCOME:
        current_screen().next_enter =
            name_length() != 0 ? main_menu_screen_enter : name_entry_enter;
        break;
    case DIALOG_RESET_GAME:
        current_screen().next_enter = options_screen_enter;
        text_id = 0x52;
        break;
    case DIALOG_NEW_GAME:
        current_screen().next_enter = course_select_screen_enter;
        text_id = 0x68;
        break;
    case DIALOG_EXIT_GAME:
        current_screen().next_enter = pause_menu_screen_enter;
        text_id = static_cast<uint32_t>(menu_state().game_mode) == MODE_SINGLE_PLAYER ? 0x5f : 0x5e;
        break;
    case DIALOG_QUIT_GAME:
        current_screen().next_enter = pause_menu_screen_enter;
        text_id = 0x82;
        break;
    case DIALOG_NAME_CONFIRMED:
        current_screen().next_enter = name_entry_enter;
        text_id = 0x78;
        with_name = true;
        break;
    default:
        break;
    }
    if (with_name) {
        // "<name>\n\n<text>"
        resource_load(pack, text_id, DIALOG_TEXT, 0x800);
        message_copy(EDIT_BUFFER);
        message_append(LINE_BREAK);
        message_append(LINE_BREAK);
        message_append(DIALOG_TEXT);
    } else {
        resource_load(pack, text_id, DIALOG_MESSAGE, 0x800);
    }
    if (type == DIALOG_QUIT_GAME &&
        static_cast<uint32_t>(menu_state().game_mode) == MODE_SINGLE_PLAYER) {
        resource_load(pack, 0x83, DIALOG_TEXT, 0x800);
        message_append(SPACE);
        message_append(DIALOG_TEXT);
    }

    const uint32_t lines = text_layout(as_font(screen_state().text_layout), DIALOG_MESSAGE,
                                       TEXT_LAYOUT_OUT, TEXT_LAYOUT_LINES, TEXT_LAYOUT_WIDTH);
    if (lines == 0) {
        assert_trap(0x180053d8u);
    }
    menu_state().first_row = static_cast<int8_t>(0);
    menu_state().cursor = static_cast<int8_t>(0);
    menu_state().item_count = static_cast<uint8_t>(lines);
    const libc::Division division =
        libc::signed_divide(TEXT_LAYOUT_WIDTH, as_font(screen_state().text_layout).line_height);
    menu_state().visible_rows = static_cast<int8_t>(division.quotient);

    menu_items_load(menu_table(), DIALOG_ITEMS, DIALOG_ITEMS_SIZE);
    menu_items() = menu_table();
    text_block().byte_72d = static_cast<uint8_t>(0);
    current_screen().handler = dialog_handle_event;
    current_screen().tick = tick_nothing;
    play_state().panel_growing = static_cast<uint8_t>(1);
    play_state().panel_scale = 0;
    play_state().panel_scale_step = PANEL_SCALE_STEP_VALUE;
    current_screen().render = dialog_render;
    screen_set(8);
    wheel_slots_clear();  // memclr
}

// --- render ------------------------------------------------------------------------------------

namespace {

constexpr uint32_t DIALOG_SCREEN = 8;
constexpr uint32_t PANEL_X = 10, PANEL_WIDTH = 300, PANEL_PADDING = 0x14, PANEL_TEXT_INSET = 10;
constexpr uint32_t TEXT_LEFT_X = 0x14;
constexpr uint32_t LOGO_CLEARANCE = 0x3a;  // pages keep their panel below the logo
constexpr uint32_t CHOICE_ROW_PITCH = 0x19, CHOICE_ROWS = 2;
constexpr uint32_t LINE_STARTS = TEXT_LAYOUT_OUT;  // text layout's word offsets of each line
constexpr uint32_t LINE_BUFFER = DIALOG_TEXT;      // one line at a time, for drawing

// Pages whose text is centred rather than set flush left (menu::PAGE), besides every dialog.
bool page_is_centred(uint32_t page) {
    return page == 2 || page == 6 || page == 9 || page == 10 || page == 11;
}

// Pages that sit under the logo: statistics and the three help pages.
bool page_under_logo(int32_t page) {
    return page == 1 || page == 3 || page == 4 || page == 5;
}

}  // namespace

// 0x18012fa8 — the dialog and the in-game pages: the dimmed background and logo, the panel
// (growing in, if it still is), the laid-out lines of the message, and — for the dialog — its
// two choices with the ball on the selected one. Answers 1.
uint32_t dialog_render() {
    const uint32_t text = GAME_STATE + game_state::TEXT;
    const bool is_dialog = static_cast<uint32_t>(screen_state().id) == DIALOG_SCREEN;
    FontRecord& small_font = as_font(screen_state().font_object);
    FontRecord& font = is_dialog ? as_font(screen_state().text_layout) : small_font;

    // The panel's height follows the text: the lines shown, plus room for the choices.
    const int32_t item_count = static_cast<int32_t>(static_cast<int8_t>(menu_state().item_count));
    const int32_t visible_rows =
        static_cast<int32_t>(static_cast<uint32_t>(menu_state().visible_rows));
    const int32_t lines = item_count <= visible_rows ? item_count : visible_rows;
    int32_t panel_height =
        lines * static_cast<int32_t>(font.line_height) + static_cast<int32_t>(PANEL_PADDING);
    if (is_dialog) {
        panel_height +=
            2 * static_cast<int32_t>(small_font.line_height) + static_cast<int32_t>(PANEL_PADDING);
    }
    int32_t panel_y = halve(static_cast<int32_t>(SCREEN_HEIGHT) - panel_height);
    if (page_under_logo(static_cast<int32_t>(static_cast<uint32_t>(menu_state().page)))) {
        const int32_t below_logo = static_cast<int32_t>(small_font.line_height + LOGO_CLEARANCE);
        if (below_logo > panel_y) {
            panel_y = below_logo;
        }
    }

    background_draw(Blend::Alpha);
    logo_draw(Blend::Alpha);
    if (panel_scale_step()) {  // still growing: only the panel this frame
        panel_draw_scaled(PANEL_X, panel_y, PANEL_WIDTH, panel_height);
        return 1;
    }
    panel_draw(PANEL_X, panel_y, PANEL_WIDTH, panel_height);

    // The message, line by line as text layout broke it.
    int32_t x = static_cast<int32_t>(TEXT_LEFT_X);
    Align align = Align::Left;
    if (page_is_centred(static_cast<uint32_t>(menu_state().page)) ||
        static_cast<uint32_t>(menu_state().dialog_type) != 0) {
        x = static_cast<int32_t>(SCREEN_CENTRE_X);
        align = Align::Centre;
    }
    int32_t y = panel_y + static_cast<int32_t>(PANEL_TEXT_INSET);
    const int32_t line_count = item_count < visible_rows ? item_count : visible_rows;
    for (int32_t line = 0; line < line_count; ++line) {
        const uint32_t start = guest_array<uint32_t>(LINE_STARTS)[line];
        const uint32_t length = guest_array<uint32_t>(LINE_STARTS)[line + 1] - start;
        if (wide_text()) {
            guest<uint16_t>(LINE_BUFFER) = static_cast<uint16_t>(0);
            wide_string_copy_n(LINE_BUFFER, DIALOG_MESSAGE + start * 2, length);
            guest<uint16_t>(LINE_BUFFER + length * 2) = static_cast<uint16_t>(0);
        } else {
            guest<uint8_t>(LINE_BUFFER) = static_cast<uint8_t>(0);
            string_copy_n(LINE_BUFFER, DIALOG_MESSAGE + start, length);
            guest<uint8_t>(LINE_BUFFER + length) = static_cast<uint8_t>(0);
        }
        text_draw(font, LINE_BUFFER, x, y, align);
        y += static_cast<int32_t>(font.line_height);
    }

    if (is_dialog) {
        const MenuItem* items = menu_items();
        if (items == nullptr) {
            assert_trap(0x180132ccu);
        }
        int32_t row_y = y + static_cast<int32_t>(PANEL_TEXT_INSET);
        for (uint32_t row = 0; row < CHOICE_ROWS; ++row) {
            const MenuItem& item = items[row];
            if (item.style == STYLE_HIDDEN) {
                continue;
            }
            resource_load(as_pack(game_state_block().pack_handle), item.text_id, SCRATCH_TEXT,
                          0x800);
            if (static_cast<uint32_t>(menu_state().cursor) == row) {
                highlighted_row_draw(small_font, static_cast<int32_t>(SCREEN_CENTRE_X), row_y,
                                     true);
            } else {
                text_draw(small_font, SCRATCH_TEXT, static_cast<int32_t>(SCREEN_CENTRE_X), row_y,
                          Align::Left);
            }
            row_y += static_cast<int32_t>(CHOICE_ROW_PITCH);
        }
    }
    as_text(text).byte_72d = static_cast<uint8_t>(1);
    return 1;
}

// --- the dialog's answer ---------------------------------------------------------------------

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t PAGE_AFTER_EXIT = 6;  // what course play shows after "save and exit"
constexpr uint32_t PLAYER_NAME = SCREEN_OBJECT + screen::PLAYER_NAME;

// The name typed on the name-entry screen becomes the player's name.
void player_name_commit() {
    if (wide_text()) {
        wide_string_copy(PLAYER_NAME, EDIT_BUFFER);
    } else {
        string_copy(PLAYER_NAME, EDIT_BUFFER);
    }
}

void slide_to_next() {
    const ScreenEnter next = current_screen().next_enter;
    if (next == nullptr) {
        assert_trap(0x18010830u);
    }
    next();
}

}  // namespace

// 0x18010654 — the dialog's handler. Select means "yes" (for the welcome dialog the answer 1
// tells the flow to move on); Menu means "no" and goes back to wherever the dialog said.
uint32_t dialog_handle_event(uint32_t event) {
    const uint32_t type = static_cast<uint32_t>(static_cast<int8_t>(menu_state().dialog_type));
    // Only a single-player round is saved; the other modes just end.
    const bool single_player = static_cast<uint32_t>(menu_state().game_mode) == MODE_SINGLE_PLAYER;
    if (event == EVENT_SELECT) {
        switch (type) {
        case DIALOG_WELCOME:
            return 1;
        case DIALOG_RESET_GAME:
            guest<uint16_t>(PLAYER_NAME) = static_cast<uint16_t>(0);
            game_state_block().loaded[155] = static_cast<int8_t>(0);
            save_reset(1, 1, 1);
            // The one place a player deliberately erases their progress, so it is the one place
            // this port's own record of it goes too (round_history.h). The cheats are settings
            // rather than progress and are left alone.
            round_history_clear();
            save_round_history();
            course_load_request(0, 0);
            return 0;
        case DIALOG_NEW_GAME:
            game_new();
            course_select_enter();
            game_state_block().save_data_byte_5 = static_cast<uint8_t>(0);
            screen_state().byte_dd0 = static_cast<uint8_t>(0);
            return 0;
        case DIALOG_EXIT_GAME:
            if (single_player) {
                game_state_block().save_data_byte_5 = static_cast<uint8_t>(1);
                course_state_save(1);
            }
            course_unload();
            course_images_load();
            if (!single_player) {
                return 1;
            }
            menu_state().page = static_cast<int8_t>(PAGE_AFTER_EXIT);
            page_enter();
            wheel_slots_clear();
            return 0;
        case DIALOG_QUIT_GAME:
            text_block().byte_72b = static_cast<uint8_t>(0xff);
            music_start(0);
            if (single_player) {
                game_state_block().save_data_byte_5 = static_cast<uint8_t>(0);
            }
            screen_state().byte_dcf = static_cast<uint8_t>(0);
            course_unload();
            course_images_load();
            main_menu_enter();
            return 0;
        case DIALOG_NAME_CONFIRMED:
            player_name_commit();
            screen_state().byte_df4 =
                static_cast<int8_t>(static_cast<uint32_t>(menu_state().language));
            save_record_snapshot();
            if (static_cast<uint32_t>(static_cast<uint8_t>(game_state_block().loaded[155])) == 1) {
                options_enter();
            } else {
                main_menu_enter();
            }
            return 0;
        default:
            return 0;
        }
    }
    if (event != EVENT_MENU) {
        return 0;
    }
    switch (type) {
    case DIALOG_WELCOME:
        text_block().menu_return_row = static_cast<uint8_t>(
            static_cast<uint32_t>(static_cast<uint8_t>(game_state_block().loaded[154])));
        break;
    case DIALOG_RESET_GAME:
    case DIALOG_QUIT_GAME:
        text_block().menu_return_row = static_cast<uint8_t>(5);
        break;
    case DIALOG_NEW_GAME:
        text_block().menu_return_row = static_cast<uint8_t>(0);
        current_screen().next_enter = game_modes_screen_enter;
        break;
    case DIALOG_EXIT_GAME:
        text_block().menu_return_row =
            static_cast<uint8_t>(static_cast<uint32_t>(menu_state().byte_29));
        break;
    case DIALOG_NAME_CONFIRMED:
        player_name_commit();
        break;
    default:
        break;
    }
    slide_to_next();
    return 0;
}

}  // namespace minigolf::game
