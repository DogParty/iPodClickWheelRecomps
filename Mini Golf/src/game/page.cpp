// The page screen (screen 7): a message on the panel — statistics, the three help pages, the
// brightness and volume sliders, "your save game data was reset", "game progress was saved" —
// with a menu::PAGE to say which. The text is assembled here; the page's tick and render
// (0x180113c4, 0x18010c3c) are still recompiled, except that page 6 borrows the dialog's render.
#include "page.h"

#include "calling.h"
#include "course.h"
#include "dialog.h"
#include "fixed.h"
#include "game_state.h"
#include "host_text.h"
#include "libc.h"
#include "menu.h"
#include "resources.h"
#include "round_history.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"
#include "strings.h"
#include "text.h"
#include "ui.h"

#include <cstdio>

namespace minigolf::game {

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t SCREEN_PAGE = 7;
constexpr uint32_t PAGE_COUNT = 14;
// Text resources (jdmg order): the pages' messages and the statistics page's labels.
constexpr uint32_t TEXT_DEFAULT = 0x77, TEXT_RESET_DONE = 0x53, TEXT_HELP_CONTROLS = 0x30,
                   TEXT_HELP_PLAY = 0x4a, TEXT_ABOUT = 0x4c, TEXT_SAVED = 0x69;
constexpr uint32_t TEXT_MODE_PAGE[] = {0x59, 0x57, 0x5a, 0x5b, 0x5c};  // pages 7..11
constexpr uint32_t TEXT_HOLES_PLAYED = 0x44, TEXT_HOLES_IN_ONE = 0x45, TEXT_BEST_ROUNDS = 0x46,
                   TEXT_COURSE_LABEL = 0x3a, TEXT_NO_ROUNDS = 0x47, TEXT_STATISTICS_NOTE = 0x48;
// Literal strings in the image: "\n" serves both encodings; the rest come in pairs.
constexpr uint32_t LITERAL_NEWLINE = 0x1800'6490, LITERAL_V = 0x1800'6494,
                   LITERAL_TWO_NEWLINES = 0x1800'6498, LITERAL_WIDE_COLON = 0x1800'64a0,
                   LITERAL_WIDE_SPACE = 0x1800'64a4, LITERAL_COLON_SPACE = 0x1800'6660;
constexpr uint32_t LAYOUT_WIDTH = 0x80, LAYOUT_HEIGHT_SMALL = 0x118, LAYOUT_HEIGHT_LARGE = 0x10e;
constexpr int32_t MENU_TITLE_Y = 0x30, LINES_X = 0xf, SCROLL_BAR_X = 0x12a;
constexpr int32_t PANEL_X = 0xa, PANEL_WIDTH = 0x12c;
constexpr uint32_t FULL = 0x10000;
// The titles the pages show on their panel (jdmg order).
constexpr uint32_t TEXT_TITLE_STATISTICS = 0x2b, TEXT_TITLE_HELP_CONTROLS = 0x2a,
                   TEXT_TITLE_HELP_PLAY = 0x49, TEXT_TITLE_ABOUT = 0x4b, TEXT_TITLE_VOLUME = 0x7b,
                   TEXT_TITLE_BRIGHTNESS = 0x7c;
constexpr uint32_t ARROW_UP_IMAGE = GAME_STATE + 0x84ac4, ARROW_DOWN_IMAGE = GAME_STATE + 0x84b00;
constexpr uint32_t PLAYER_NAME = SCREEN_OBJECT + screen::PLAYER_NAME;

bool wide_text() {
    return static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
}

// Appending to the message in the current encoding, with the original's return addresses.
struct Message {
    uint32_t pack;

    void append(uint32_t source) const {
        if (wide_text()) {
            wide_string_append(DIALOG_MESSAGE, source);
        } else {
            string_append(DIALOG_MESSAGE, source);
        }
    }
    // Load a text resource into the scratch buffer and append it.
    void append_text(uint32_t text_id) const {
        resource_load(as_pack(pack), text_id, DIALOG_TEXT, 0x800);
        append(DIALOG_TEXT);
    }
    void append_number(uint32_t buffer, uint32_t value, uint32_t format_return) const {
        (void)format_return;
        (wide_text() ? wide_number_to_string : number_to_string)(buffer,
                                                                 static_cast<int32_t>(value), 0);
        append(buffer);
    }
};

// "V1.0.0", two line breaks, then the page's text.
void about_message(const Message& message, uint32_t text_id, uint32_t glyph_buffer) {
    if (wide_text()) {
        const uint32_t length = string_length(VERSION_STRING);
        guest<uint16_t>(glyph_buffer + 2) = static_cast<uint16_t>(0);
        guest<uint16_t>(DIALOG_MESSAGE) = static_cast<uint16_t>(0);
        for (uint32_t i = 0; i < length; ++i) {  // the 8-bit version, a character at a time
            guest<uint16_t>(glyph_buffer) = static_cast<uint16_t>(
                static_cast<uint32_t>(guest_array<int8_t>(VERSION_STRING)[i]));
            message.append(glyph_buffer);
        }
        message.append(LITERAL_NEWLINE);
        message.append(LITERAL_NEWLINE);
        message.append_text(text_id);
    } else {
        string_copy(DIALOG_MESSAGE, LITERAL_V);  // string_copy
        message.append(VERSION_STRING);
        message.append(LITERAL_TWO_NEWLINES);
        message.append_text(text_id);
    }
}

// What this port remembers on top of the original's three numbers (round_history.h): how many
// rounds have been finished on each course and what they average, and how many of the 54 holes
// have a best score on file. The labels are in English on every language's page, for the reason
// host_text.h gives.
//
// It is appended after the game's own lines rather than mixed into them, so that what the iPod
// showed is still there, in the order it showed it, above what this port has added.
void round_history_message(const Message& message) {
    uint32_t rounds = 0;
    for (uint32_t course = 0; course < COURSE_COUNT; ++course) {
        rounds += course_rounds(course);
    }
    char line[48];
    message.append(LITERAL_NEWLINE);
    std::snprintf(line, sizeof line, "ROUNDS FINISHED: %u", rounds);
    host_text_write(DIALOG_TEXT, line);
    message.append(DIALOG_TEXT);
    message.append(LITERAL_NEWLINE);
    std::snprintf(line, sizeof line, "HOLES WITH A BEST: %u/%u", holes_with_a_best(),
                  COURSE_COUNT * HOLES_PER_COURSE);
    host_text_write(DIALOG_TEXT, line);
    message.append(DIALOG_TEXT);
    message.append(LITERAL_NEWLINE);
    for (uint32_t course = 0; course < COURSE_COUNT; ++course) {
        if (course_rounds(course) == 0) {
            continue;  // a course never finished has no average to report
        }
        std::snprintf(line, sizeof line, "AVERAGE ON COURSE %u: %u", course + 1,
                      course_average_round(course));
        host_text_write(DIALOG_TEXT, line);
        message.append(DIALOG_TEXT);
        message.append(LITERAL_NEWLINE);
    }
}

// The player's name, holes played, holes in one, and the best round on each course.
void statistics_message(const Message& message, uint32_t number_buffer) {
    const bool wide = wide_text();
    if (wide) {
        guest<uint16_t>(number_buffer) = static_cast<uint16_t>(0);
        wide_string_copy(DIALOG_TEXT, PLAYER_NAME);  // wide_string_copy
    } else {
        guest<uint8_t>(number_buffer) = static_cast<uint8_t>(0);
        string_copy(DIALOG_TEXT, PLAYER_NAME);  // string_copy
    }
    message.append(DIALOG_TEXT);
    message.append(LITERAL_NEWLINE);
    message.append(LITERAL_NEWLINE);
    message.append_text(TEXT_HOLES_PLAYED);
    message.append_number(number_buffer, screen_state().holes_played,
                          wide ? 0x1800620cu : 0x180063ccu);
    message.append_text(TEXT_HOLES_IN_ONE);
    message.append_number(number_buffer, screen_state().holes_in_one,
                          wide ? 0x18006248u : 0x18006408u);
    message.append(LITERAL_NEWLINE);
    message.append_text(TEXT_BEST_ROUNDS);
    message.append(LITERAL_NEWLINE);
    for (uint32_t course = 0; course < 3; ++course) {
        message.append_text(TEXT_COURSE_LABEL);
        message.append_number(number_buffer, course + 1, wide ? 0x180062c0u : 0x180064c4u);
        if (wide) {
            message.append(LITERAL_WIDE_COLON);
            message.append(LITERAL_WIDE_SPACE);
        } else {
            message.append(LITERAL_COLON_SPACE);
        }
        const uint32_t best = screen_state().best_round[course];
        if (best == NO_BEST_ROUND) {
            message.append_text(TEXT_NO_ROUNDS);
        } else {
            message.append_number(number_buffer, best, wide ? 0x18006320u : 0x18006518u);
        }
        message.append(LITERAL_NEWLINE);
    }
    if (!port_additions_hidden()) {
        round_history_message(message);
    }
    message.append_text(TEXT_STATISTICS_NOTE);
}

}  // namespace

// 0x18005f54 — assemble the page's message and show it.
void page_enter() {
    GuestScratch frame(4 * 9 + 0x1c);
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    const int32_t page = static_cast<int32_t>(static_cast<uint32_t>(menu_state().page));
    if (page <= 0 || page >= static_cast<int32_t>(PAGE_COUNT)) {
        assert_trap(page <= 0 ? 0x18005f6cu : 0x18005f78u);
    }
    const Message message{game_state_block().pack_handle};

    // Where the page leads afterwards, what it is titled, and its text. `next` is handed to
    // `screen_install` at the end rather than written into the screen here: installing the
    // screen writes all four of its fields, so anything put there first is overwritten — which
    // left every page with no way out, since leaving one means calling exactly this.
    ScreenEnter next = help_screen_enter;
    menu_state().title_text = 0xffff'ffffu;
    uint32_t text_id = TEXT_DEFAULT;
    switch (page) {
    case PAGE_STATISTICS:
        next = main_menu_screen_enter;
        break;
    case PAGE_RESET_DONE:
        text_id = TEXT_RESET_DONE;
        next = options_screen_enter;
        break;
    case PAGE_HELP_FIRST:
    case PAGE_HELP_FIRST + 1:
    case PAGE_HELP_FIRST + 2:
        text_id = page == PAGE_HELP_FIRST       ? TEXT_HELP_CONTROLS
                  : page == PAGE_HELP_FIRST + 1 ? TEXT_HELP_PLAY
                                                : TEXT_ABOUT;
        menu_state().title_text = TITLE_HELP;
        break;
    case PAGE_SAVED:
        text_id = TEXT_SAVED;
        break;
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:  // the game-mode descriptions
        text_id = TEXT_MODE_PAGE[page - 7];
        next = game_modes_screen_enter;
        break;
    case PAGE_VOLUME:
        if (static_cast<uint32_t>(screen_state().byte_dcf) == 1) {
            menu_state().title_text = TITLE_PAUSE;
            next = pause_menu_screen_enter;
        } else {
            next = main_menu_screen_enter;
        }
        break;
    case PAGE_BRIGHTNESS:
        menu_state().title_text = TITLE_OPTIONS;
        next = options_screen_enter;
        break;
    default:
        break;
    }
    if (text_id == TEXT_ABOUT) {
        about_message(message, text_id, frame.at(0x10));
    } else {
        resource_load(as_pack(message.pack), text_id, DIALOG_MESSAGE, 0x800);
    }
    if (page == PAGE_STATISTICS) {
        statistics_message(message, frame.at(wide_text() ? 0x4 : 0xc));
    }

    // Lay the message out; the reset page uses the small font and a taller box.
    const uint32_t small_font = screen_state().font_object;
    const uint32_t large_font = screen_state().text_layout;
    const uint32_t small_line = as_font(small_font).line_height;
    const bool small = page == PAGE_RESET_DONE;
    const uint32_t lines =
        text_layout(as_font(small ? small_font : large_font), DIALOG_MESSAGE, TEXT_LAYOUT_OUT,
                    LAYOUT_WIDTH, small ? LAYOUT_HEIGHT_SMALL : LAYOUT_HEIGHT_LARGE);
    if (lines == 0) {
        assert_trap(small ? 0x1800658cu : 0x180065d0u);
    }
    text.score_card_first = static_cast<uint8_t>(0);
    text.carousel_course = static_cast<int8_t>(0);
    text.carousel_count = static_cast<uint8_t>(static_cast<int8_t>(lines));
    // The rows that fit in the box the page uses.
    const uint32_t rows =
        small ? libc::signed_divide(SCREEN_HEIGHT - small_line - 0x58, small_line).quotient
              : libc::signed_divide(SCREEN_HEIGHT - 2 * small_line - 0x62,
                                    as_font(large_font).line_height)
                    .quotient;
    text.score_card_rows_shown = static_cast<uint8_t>(static_cast<int8_t>(rows));
    text.byte_72d = static_cast<uint8_t>(0);

    screen_install(page_handle_event, tick_nothing,
                   page == PAGE_SAVED ? dialog_render : page_render, next);
    play_state().panel_growing = 1;
    play_state().panel_scale = 0;
    play_state().panel_scale_step = PANEL_SCALE_STEP_VALUE;
    screen_set(SCREEN_PAGE);
}

// 0x1800e764 — a page's buttons: Select or Menu leaves it. A course page (7..11) unloads the
// course first; the "saved" page answers 1 when opened from the pause menu (the flow resumes
// the hole); the others note which main menu row to return to and run the screen's next
// enter routine.
uint32_t page_handle_event(uint32_t event) {
    if (event != EVENT_SELECT && event != EVENT_MENU) {
        return 0;
    }
    const int32_t page = static_cast<int32_t>(static_cast<uint32_t>(menu_state().page));
    if (page >= 7 && page <= 11) {
        course_unload();
    }
    if (page == PAGE_SAVED) {
        return static_cast<uint32_t>(screen_state().byte_dcf) == 1 ? 1 : 0;
    }
    uint32_t row = 0xff;
    switch (page) {
    case PAGE_STATISTICS:
        row = 4;
        break;
    case PAGE_RESET_DONE:
        row = 5;
        break;
    case PAGE_HELP_FIRST:
        row = 0;
        break;
    case PAGE_HELP_FIRST + 1:
        row = 1;
        break;
    case PAGE_HELP_FIRST + 2:
    case PAGE_VOLUME:
        row = 2;
        break;
    case PAGE_BRIGHTNESS:
        row = 3;
        break;
    default:
        break;
    }
    text_block().menu_return_row = static_cast<uint8_t>(row);
    if (const ScreenEnter next = current_screen().next_enter) {
        next();
    }
    return 0;
}

constexpr int32_t SLIDER_X = 0x3a, SLIDER_TRACK_WIDTH = 0xcc, SLIDER_TRACK_HEIGHT = 0xe,
                  SLIDER_WIDTH = 0xc8, SLIDER_HEIGHT = 0xa;

// 0x1800e2ec — a slider for a level of 0..100: a white track, and black over the part of it
// past the level.
void slider_draw(int32_t y, int32_t level) {
    rect_fill(to_fixed(SLIDER_X), to_fixed(y - 2), to_fixed(SLIDER_TRACK_WIDTH),
              to_fixed(SLIDER_TRACK_HEIGHT), FULL, FULL, FULL, FULL, Blend::Additive);
    if (level >= 100) {
        return;
    }
    rect_fill(to_fixed(SLIDER_X + 2 + 2 * level), to_fixed(y), to_fixed(SLIDER_WIDTH - 2 * level),
              to_fixed(SLIDER_HEIGHT), 0, 0, 0, FULL, Blend::Keyed);
}

// 0x18010c3c — draw the page: background, logo, the menu's title underlined, then the panel
// with the page's own title, its lines (centred in the small font on the "reset" page, left
// in the large font otherwise) or a slider for the volume and brightness pages, and — when
// the page has a title — a scroll bar whose thumb shows which lines are in view.
uint32_t page_render() {
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    const int32_t page = static_cast<int32_t>(static_cast<uint32_t>(menu_state().page));
    uint32_t title = 0xffffffffu;
    switch (page) {
    case PAGE_STATISTICS:
        title = TEXT_TITLE_STATISTICS;
        break;
    case PAGE_HELP_FIRST:
        title = TEXT_TITLE_HELP_CONTROLS;
        break;
    case PAGE_HELP_FIRST + 1:
        title = TEXT_TITLE_HELP_PLAY;
        break;
    case PAGE_HELP_FIRST + 2:
        title = TEXT_TITLE_ABOUT;
        break;
    case PAGE_VOLUME:
        title = TEXT_TITLE_VOLUME;
        break;
    case PAGE_BRIGHTNESS:
        title = TEXT_TITLE_BRIGHTNESS;
        break;
    default:
        break;
    }
    FontRecord& small_font = as_font(screen_state().font_object);
    FontRecord& large_font = as_font(screen_state().text_layout);
    const int32_t small_line = static_cast<int32_t>(small_font.line_height);
    const int32_t large_line = static_cast<int32_t>(large_font.line_height);
    const bool centred = page == PAGE_RESET_DONE;  // the one page set in the small font
    const int32_t line = centred ? small_line : large_line;
    const int32_t lines = static_cast<int32_t>(static_cast<uint32_t>(text.carousel_count));
    const int32_t shown = static_cast<int32_t>(static_cast<uint32_t>(text.score_card_rows_shown));
    const int32_t rows = lines <= shown ? lines : shown;
    int32_t height = rows * line + 0x14;
    if (title != 0xffffffffu) {
        height += small_line + 0xa;
    }
    int32_t top = (static_cast<int32_t>(SCREEN_HEIGHT) - height) / 2;
    if (small_line + 0x3a > top) {
        top = small_line + 0x3a;  // never over the logo and the menu title
    }

    background_draw(Blend::Opaque);
    logo_draw(Blend::Alpha);
    if (menu_state().title_text != 0xffffffffu) {
        resource_load(as_pack((game_state_block().pack_handle)), (menu_state().title_text),
                      SCRATCH_TEXT, 0x800);
        text_draw(small_font, SCRATCH_TEXT, SCREEN_CENTRE_X, MENU_TITLE_Y, Align::Centre);
        const int32_t width = static_cast<int32_t>(text_width(small_font, SCRATCH_TEXT)) + 2;
        rect_fill(to_fixed(SCREEN_CENTRE_X - width / 2), to_fixed(small_line + 0x2e),
                  to_fixed(width), to_fixed(2), FULL, FULL, FULL, FULL, Blend::KeyedAlt);
    }

    if (static_cast<uint32_t>(play_state().panel_growing) != 0) {
        panel_scale_step();
        panel_draw_scaled(PANEL_X, top, PANEL_WIDTH, height);
        return 1;
    }
    panel_draw(PANEL_X, top, PANEL_WIDTH, height);
    int32_t y = top + 0xa;
    if (title != 0xffffffffu) {
        resource_load(as_pack((game_state_block().pack_handle)), title, SCRATCH_TEXT, 0x800);
        text_draw(small_font, SCRATCH_TEXT, SCREEN_CENTRE_X, y, Align::Centre);
        y += small_line + 0xa;
        if (title == TEXT_TITLE_VOLUME || title == TEXT_TITLE_BRIGHTNESS) {
            const uint32_t level =
                title == TEXT_TITLE_VOLUME ? play_state().music_level : play_state().device_level;
            slider_draw(y, static_cast<int32_t>(level));
            return 1;
        }
    }

    const int32_t lines_top = y;
    const int32_t first = static_cast<int32_t>(static_cast<uint32_t>(text.carousel_course));
    for (int32_t i = first; i < first + rows; ++i) {
        const uint32_t start = guest_array<uint32_t>(TEXT_LAYOUT_OUT)[i];
        const uint32_t length = guest_array<uint32_t>(TEXT_LAYOUT_OUT)[i + 1] - start;
        if (wide_text()) {
            guest<uint16_t>(DIALOG_TEXT) = static_cast<uint16_t>(0);
            wide_string_copy_n(DIALOG_TEXT, DIALOG_MESSAGE + start * 2, length);
            guest<uint16_t>(DIALOG_TEXT + length * 2) = static_cast<uint16_t>(0);
        } else {
            guest<uint8_t>(DIALOG_TEXT) = static_cast<uint8_t>(0);
            string_copy_n(DIALOG_TEXT, DIALOG_MESSAGE + start, length);
            guest<uint8_t>(DIALOG_TEXT + length) = static_cast<uint8_t>(0);
        }
        if (centred) {
            text_draw(small_font, DIALOG_TEXT, SCREEN_CENTRE_X, y, Align::Centre);
        } else {
            text_draw(large_font, DIALOG_TEXT, LINES_X, y, Align::Left);
        }
        y += line;
    }

    if (title != 0xffffffffu) {  // the scroll bar: arrows, and a thumb the size of the view
        const int32_t up_height = static_cast<int32_t>(as_image(ARROW_UP_IMAGE).height);
        const int32_t down_height = static_cast<int32_t>(as_image(ARROW_DOWN_IMAGE).height);
        const int32_t up_width = static_cast<int32_t>(as_image(ARROW_UP_IMAGE).width);
        const int32_t bottom = y - down_height;
        image_draw(SCROLL_BAR_X, lines_top, static_cast<uint32_t>(up_width),
                   static_cast<uint32_t>(up_height), as_image(ARROW_UP_IMAGE), 0, 0,
                   static_cast<uint32_t>(as_image(ARROW_UP_IMAGE).variant), Blend::KeyedAlt);
        image_draw(SCROLL_BAR_X, bottom, as_image(ARROW_DOWN_IMAGE).width,
                   static_cast<uint32_t>(down_height), as_image(ARROW_DOWN_IMAGE), 0, 0,
                   static_cast<uint32_t>(as_image(ARROW_DOWN_IMAGE).variant), Blend::KeyedAlt);
        const int32_t track_top = lines_top + up_height;
        int32_t thumb_y = to_fixed_signed(track_top),
                thumb_height = to_fixed_signed(bottom - track_top);
        const int64_t span =
            static_cast<int64_t>((rows * large_line - up_height - down_height) << 16);
        if (shown != lines) {
            const int64_t ratio =
                (static_cast<int64_t>(shown) << 32) / (static_cast<int64_t>(lines) << 16);
            thumb_height = static_cast<int32_t>((ratio * span) >> 16);
            const int64_t per_line =
                ((span - thumb_height) << 16) / (static_cast<int64_t>(lines - shown) << 16);
            thumb_y +=
                static_cast<int32_t>((static_cast<int64_t>(to_fixed(first)) * per_line) >> 16);
        }
        rect_fill(to_fixed(SCROLL_BAR_X), static_cast<uint32_t>(thumb_y), to_fixed(up_width),
                  static_cast<uint32_t>(thumb_height), 0, 0, 0, FULL, Blend::KeyedAlt);
        rect_fill((SCROLL_BAR_X + 1) << 16, static_cast<uint32_t>(thumb_y + to_fixed_signed(1)),
                  to_fixed(up_width - 2), static_cast<uint32_t>(thumb_height - to_fixed_signed(2)),
                  FULL, FULL, FULL, FULL, Blend::KeyedAlt);
    }
    text.byte_72d = static_cast<uint8_t>(1);
    return 1;
}

// 0x18005f54 — the page screen, entered: any pending input is forgotten with it.
void page_screen_enter() {
    page_enter();
    wheel_slots_clear();
}

}  // namespace minigolf::game
