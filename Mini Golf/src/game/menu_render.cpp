// Drawing a menu screen: the dimmed background and logo, the title with its underline, the
// visible rows at their slide positions — the selected one with the ball and rippling
// letters — and, on Select Hole, the scroll arrows.
#include "calling.h"
#include "draw.h"
#include "fixed.h"
#include "game_state.h"
#include "host_text.h"
#include "menu.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"
#include "strings.h"
#include "ui.h"

namespace minigolf::game {

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t TITLE_Y = 0x30, UNDERLINE_Y_BELOW_LINE = 0x2e, UNDERLINE_HEIGHT = 2;
constexpr uint32_t ROW_PITCH = 0x19, ROW_PITCH_DENSE = 0x14;  // the long menus pack rows closer
constexpr uint32_t SCRATCH_TEXT_2 = DIALOG_MESSAGE;           // the value of a heading row
constexpr uint32_t WHITE = 0x10000;                           // 16.16 colour component
// The scroll arrows at the bottom right of Select Hole; variant 2 is the greyed-out one.
constexpr uint32_t ARROW_UP_IMAGE = GAME_STATE + 0x84ac4, ARROW_DOWN_IMAGE = GAME_STATE + 0x84b00;
constexpr int32_t ARROWS_Y = 0xe3, ARROW_UP_X = 0x122, ARROW_DOWN_X = 0x120;
constexpr uint32_t ARROW_LIT = 1, ARROW_GREY = 2;

bool wide_text() {
    return static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
}

// The menus set in the large font: Options and the pause menu always, Statistics when it was
// opened from the pause menu. This port's Cheats screen is set like the Options screen it opens
// from, so that stepping into it does not change the look of the rows.
bool uses_large_font(uint32_t title) {
    return title == TITLE_OPTIONS || title == TITLE_PAUSE ||
           title == host_text_id(HostText::CheatsTitle) ||
           (title == TITLE_HELP && static_cast<uint32_t>(screen_state().byte_dcf) != 0);
}

bool uses_dense_rows(uint32_t title) {
    return title == TITLE_OPTIONS || title == TITLE_PAUSE || title == TITLE_SELECT_HOLE ||
           title == host_text_id(HostText::CheatsTitle);
}

}  // namespace

// 0x1800c1dc — draw the menu screen.
uint32_t menu_render() {
    const uint32_t text = GAME_STATE + game_state::TEXT;
    FontRecord& small_font = as_font(screen_state().font_object);
    const uint32_t large_font = screen_state().text_layout;
    PackRecord& pack = as_pack(game_state_block().pack_handle);
    const uint32_t title = menu_state().title_text;

    background_draw(Blend::Opaque);
    logo_draw(Blend::Alpha);

    if (title != 0xffff'ffffu) {
        menu_text_load(title, address_of(pack), SCRATCH_TEXT);
        text_draw(small_font, SCRATCH_TEXT, static_cast<int32_t>(SCREEN_CENTRE_X),
                  static_cast<int32_t>(TITLE_Y), Align::Centre);
        const int32_t width = static_cast<int32_t>(text_width(small_font, SCRATCH_TEXT)) + 2;
        const int32_t x = static_cast<int32_t>(SCREEN_CENTRE_X) - halve(width);
        const uint32_t y = small_font.line_height + UNDERLINE_Y_BELOW_LINE;
        rect_fill(to_fixed(x), to_fixed(y), to_fixed(width), to_fixed(UNDERLINE_HEIGHT), WHITE,
                  WHITE, WHITE, WHITE, Blend::KeyedAlt);
    }

    const int32_t first_row = static_cast<int32_t>(static_cast<uint32_t>(menu_state().first_row));
    if (menu_items() == nullptr) {
        assert_trap(0x1800c374u);
    }
    MenuItem* entry = menu_items() + first_row;
    int32_t y = to_whole(entry->y);
    const bool steady = static_cast<uint32_t>(screen_state().phase) == PHASE_STEADY;
    const int32_t last_row =
        first_row + static_cast<int32_t>(static_cast<uint32_t>(menu_state().visible_rows));
    for (int32_t row = first_row; row < last_row; ++row, ++entry) {
        const uint32_t style = entry->style;
        if (style == STYLE_HIDDEN) {
            continue;
        }
        // The course's own name is drawn large, out of that course's own pack.
        const bool course_name = style == STYLE_COURSE_NAME;
        FontRecord& font = course_name || uses_large_font(title) ? as_font(large_font) : small_font;
        uint32_t text_pack = address_of(pack);
        if (course_name) {
            const int32_t course = static_cast<int32_t>(static_cast<uint32_t>(menu_state().course));
            text_pack = game_state_block().pack_course[static_cast<uint32_t>(course)];
        }
        menu_text_load(entry->text_id, text_pack, SCRATCH_TEXT);
        if (style == STYLE_HEADING) {  // "MUSIC: " + its value
            guest<uint16_t>(SCRATCH_TEXT_2) = 0;
            const uint32_t value = heading_value_text(entry->kind);
            resource_load(pack, value, SCRATCH_TEXT_2, 0x800);
            if (wide_text()) {
                wide_string_append(SCRATCH_TEXT, SCRATCH_TEXT_2);
            } else {
                string_append(SCRATCH_TEXT, SCRATCH_TEXT_2);
            }
        }
        const int32_t x = to_whole(entry->x);
        if (static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor)) ==
            row) {  // selected: rippling
            highlighted_row_draw(font, x, y, steady);
        } else {
            text_draw(font, SCRATCH_TEXT, x, y, Align::Left);
        }
        y += static_cast<int32_t>(uses_dense_rows(title) ? ROW_PITCH_DENSE : ROW_PITCH);
    }

    if (title == TITLE_SELECT_HOLE && steady) {
        const int32_t item_count = static_cast<int32_t>(
            static_cast<uint32_t>(static_cast<int8_t>(menu_state().item_count)));
        image_draw(ARROW_UP_X, ARROWS_Y, as_image(ARROW_UP_IMAGE).width,
                   as_image(ARROW_UP_IMAGE).height, as_image(ARROW_UP_IMAGE), 0, 0,
                   first_row > 0 ? ARROW_LIT : ARROW_GREY, Blend::KeyedAlt);
        image_draw(ARROW_DOWN_X + static_cast<int32_t>(as_image(ARROW_UP_IMAGE).width), ARROWS_Y,
                   as_image(ARROW_DOWN_IMAGE).width, as_image(ARROW_DOWN_IMAGE).height,
                   as_image(ARROW_DOWN_IMAGE), 0, 0, last_row < item_count ? ARROW_LIT : ARROW_GREY,
                   Blend::KeyedAlt);
    }
    as_text(text).byte_72d = static_cast<uint8_t>(1);
    return 0;
}

}  // namespace minigolf::game
