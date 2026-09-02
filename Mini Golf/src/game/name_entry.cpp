// The name-entry screen: a wheel of glyphs, a backspace and a tick, and the name being typed.
//
// Screen 10. `name_entry_enter` (0x1800561c) loads the alphabet (resource 0x29) and the
// prompt (0x76), copies the saved name into the edit buffer, and installs the screen: the
// generic menu wrapper moves the cursor along the wheel, this screen's handler
// (0x180126e8) acts on Select and Menu, and its tick is empty. Text is UTF-16 for language
// 10 and 8-bit otherwise; the two paths mirror each other throughout.
#include "name_entry.h"

#include "calling.h"
#include "dialog.h"
#include "draw.h"
#include "fixed.h"
#include "game_state.h"
#include "hole_load.h"
#include "libc.h"
#include "menu.h"
#include "platform/text_entry.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"
#include "strings.h"
#include "text.h"
#include "ui.h"

#include <string>

namespace minigolf::game {

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t RESOURCE_ALPHABET = 0x29, RESOURCE_PROMPT = 0x76;
constexpr uint32_t NAME_MAX_LENGTH = 0x10;
constexpr uint32_t DIALOG_LEAVE_NAME_ENTRY = 1, DIALOG_NAME_CONFIRMED = 6;

bool wide_text() {
    return static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
}

uint32_t glyph_code(uint32_t index) {
    return static_cast<uint32_t>(
        static_cast<int32_t>(wide_text() ? guest_array<int16_t>(GLYPH_CODES)[index]
                                         : guest_array<int8_t>(GLYPH_CODES)[index]));
}

void edit_store(uint32_t index, uint32_t code) {
    if (wide_text()) {
        guest<uint16_t>(EDIT_BUFFER + 2 * index) = static_cast<uint16_t>(code);
    } else {
        guest<uint8_t>(EDIT_BUFFER + index) = static_cast<uint8_t>(code);
    }
}

// --- typing, where the machine has a keyboard ------------------------------------------------
//
// An addition, not something the original did: the iPod had no keyboard, so a name was spelled
// out on the wheel one letter at a time, and that still works. On a platform that offers typing
// (platform/text_entry.h) the letters can simply be typed instead. The rules are the wheel's own,
// so that neither way can do something the other cannot: the same store, the same length, the
// same limit, and Return finishes exactly as the tick glyph does.

// A typed character, if the game's fonts can show it. The alphabet the wheel offers is the
// authority: a character it does not contain is one the name cannot hold.
bool name_accepts(uint32_t code) {
    const uint32_t count = wide_text() ? wide_string_length(ALPHABET) : string_length(ALPHABET);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t letter = static_cast<uint32_t>(
            wide_text() ? guest_array<int16_t>(ALPHABET)[i] : guest_array<int8_t>(ALPHABET)[i]);
        if (letter == code) {
            return true;
        }
    }
    return false;
}

// One code point from UTF-8, and how many bytes it took. Typed characters arrive as UTF-8
// because a keyboard layout or an input method has already had its say by then.
uint32_t utf8_next(const std::string& text, size_t& at) {
    const auto byte = [&](size_t i) {
        return static_cast<uint32_t>(static_cast<uint8_t>(text[i]));
    };
    const uint32_t first = byte(at);
    if (first < 0x80) {
        at += 1;
        return first;
    }
    if ((first & 0xe0) == 0xc0 && at + 1 < text.size()) {
        at += 2;
        return ((first & 0x1f) << 6) | (byte(at - 1) & 0x3f);
    }
    if ((first & 0xf0) == 0xe0 && at + 2 < text.size()) {
        at += 3;
        return ((first & 0x0f) << 12) | ((byte(at - 2) & 0x3f) << 6) | (byte(at - 1) & 0x3f);
    }
    at += 1;  // something this game has no letter for; skipped by name_accepts anyway
    return 0;
}

// Hand over to the dialog screen with a reason (the dialog's type).
void leave_to_dialog(uint32_t dialog_type) {
    game_state_block().loaded[154] = static_cast<int8_t>(0);
    menu_state().dialog_type = static_cast<uint8_t>(dialog_type);
    dialog_enter();
}

// After the name changed: the confirm glyph appears once there is a letter and disappears
// when the name is empty again; a full name confirms itself.
void after_name_change(bool after_backspace) {
    const int32_t length = static_cast<int32_t>(menu_state().name_length);
    if (after_backspace) {
        if (static_cast<uint32_t>(menu_state().name_length) == 0) {
            menu_state().item_count =
                static_cast<uint8_t>(static_cast<uint32_t>(menu_state().item_count) - 1);
        }
        return;
    }
    if (length == 1) {
        menu_state().item_count =
            static_cast<uint8_t>(static_cast<uint32_t>(menu_state().item_count) + 1);
    } else if (length == 0x11) {
        leave_to_dialog(DIALOG_NAME_CONFIRMED);
    }
}

}  // namespace

// 0x180126e8 — Select picks the glyph under the cursor; Menu leaves for the dialog.
uint32_t name_entry_handle_event(uint32_t event) {
    if (event == EVENT_MENU) {
        leave_to_dialog(DIALOG_LEAVE_NAME_ENTRY);
        return 0;
    }
    if (event != EVENT_SELECT) {
        return 0;
    }
    const int32_t length = static_cast<int32_t>(menu_state().name_length);
    const uint32_t code = glyph_code(
        static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor))));
    if (code == GLYPH_BACKSPACE) {
        if (length == 0) {
            return 0;
        }
        menu_state().name_length = static_cast<int8_t>(
            static_cast<uint8_t>(static_cast<uint32_t>(static_cast<int8_t>(length - 1))));
        edit_store(static_cast<uint32_t>(length - 1), 0);
        after_name_change(true);
        return 0;
    }
    if (code == GLYPH_CONFIRM) {
        if (length > 0) {
            leave_to_dialog(DIALOG_NAME_CONFIRMED);
        }
        return 0;
    }
    edit_store(static_cast<uint32_t>(length), code);
    menu_state().name_length = static_cast<int8_t>(
        static_cast<uint8_t>(static_cast<uint32_t>(static_cast<int8_t>(length + 1))));
    edit_store(static_cast<uint32_t>(
                   static_cast<int32_t>(static_cast<uint32_t>(menu_state().name_length))),
               0);
    after_name_change(false);
    return 0;
}

// Take whatever the player typed since the last frame into the name, and answer whether that
// finished the name entry. Not part of the original: see the note beside `name_accepts`. Every
// change goes through the same steps the wheel's own handler uses, so a typed name and a
// spelled-out one are the same name, held to the same length.
bool name_entry_typing() {
    if (!platform::text_entry_supported()) {
        return false;
    }
    const platform::TypedText typed = platform::text_entry_take();
    if (typed.empty()) {
        return false;
    }

    for (unsigned i = 0; i < typed.backspaces; ++i) {
        const int32_t length = static_cast<int32_t>(menu_state().name_length);
        if (length == 0) {
            break;
        }
        menu_state().name_length = static_cast<int8_t>(length - 1);
        edit_store(static_cast<uint32_t>(length - 1), 0);
        after_name_change(true);
    }

    size_t at = 0;
    while (at < typed.characters.size()) {
        uint32_t code = utf8_next(typed.characters, at);
        if (code >= 'a' && code <= 'z') {
            code -= 'a' - 'A';  // the alphabet on the wheel is upper case
        }
        const int32_t length = static_cast<int32_t>(menu_state().name_length);
        if (length >= static_cast<int32_t>(NAME_MAX_LENGTH) || !name_accepts(code)) {
            continue;  // the name is full, or this is not a letter the game can show
        }
        edit_store(static_cast<uint32_t>(length), code);
        menu_state().name_length = static_cast<int8_t>(length + 1);
        edit_store(static_cast<uint32_t>(length + 1), 0);
        after_name_change(false);
    }

    if (typed.confirm && static_cast<int32_t>(menu_state().name_length) > 0) {
        leave_to_dialog(DIALOG_NAME_CONFIRMED);
        return true;
    }
    return false;
}

// 0x1800561c — open the name entry: the alphabet wheel, the saved name as the starting edit
// buffer, and the panel growing in over the course picture.
void name_entry_enter() {
    course_images_load();
    screen_install(name_entry_handle_event, tick_nothing, name_entry_render, nullptr);
    play_state().panel_growing = static_cast<uint8_t>(1);
    play_state().panel_scale = 0;
    play_state().panel_scale_step = PANEL_SCALE_STEP_VALUE;

    PackRecord& pack = as_pack(game_state_block().pack_handle);
    resource_load(pack, RESOURCE_ALPHABET, ALPHABET, 0x800);

    // The saved name becomes the edit buffer; its length is the item count's starting point.
    const uint32_t name = SCREEN_OBJECT + screen::PLAYER_NAME;
    uint32_t length;
    if (wide_text()) {
        menu_state().item_count = static_cast<uint8_t>(wide_string_length(ALPHABET));
        wide_string_copy(EDIT_BUFFER, name);
        length = wide_string_length(EDIT_BUFFER);
    } else {
        menu_state().item_count = static_cast<uint8_t>(string_length(ALPHABET));
        string_copy(EDIT_BUFFER, name);
        length = string_length(EDIT_BUFFER);
    }
    menu_state().name_length = static_cast<int8_t>(static_cast<uint8_t>(length));
    if (static_cast<uint32_t>(menu_state().name_length) == 0) {
        menu_state().item_count = static_cast<uint8_t>(
            static_cast<uint32_t>(menu_state().item_count) - 1);  // no confirm glyph yet
    } else if (static_cast<uint32_t>(menu_state().name_length) == 0x11) {
        menu_state().name_length = static_cast<uint8_t>(NAME_MAX_LENGTH);
        edit_store(NAME_MAX_LENGTH, 0);
    }
    menu_state().visible_rows = static_cast<int8_t>(0xb);
    menu_state().first_row = static_cast<int8_t>(0);
    menu_state().cursor = static_cast<int8_t>(0);

    resource_load(pack, RESOURCE_PROMPT, NAME_ENTRY_TEXT, 0x800);
    const uint32_t lines = text_layout(as_font(screen_state().text_layout), NAME_ENTRY_TEXT,
                                       TEXT_LAYOUT_OUT, TEXT_LAYOUT_LINES, TEXT_LAYOUT_WIDTH);
    if (lines == 0) {
        assert_trap(0x18005784u);
    }
    text_block().byte_729 = static_cast<uint8_t>(lines);
    screen_set(10);
    wheel_slots_clear();  // memclr
}
// --- render ------------------------------------------------------------------------------------

namespace {

constexpr uint32_t PANEL_X = 10, PANEL_WIDTH = 300, PANEL_BOTTOM = 0xe6, PANEL_TEXT_INSET = 10;
constexpr uint32_t LOGO_CLEARANCE = 0x3a;
constexpr int32_t NAME_X = 0x14, NAME_TOP = 0x2a;  // the typed name sits between logo and panel
constexpr uint32_t CARET_PERIOD = 0x1e, CARET_ON_FRAMES = 0xf, CARET_HEIGHT = 2;
constexpr int32_t WHEEL_BOTTOM = 0xdc;  // the glyph wheel is centred between the prompt and here
constexpr int32_t WHEEL_ARROW_LEFT_X = 0xf, WHEEL_ARROW_RIGHT_X = 0x125, WHEEL_ARROW_DY = 4;
constexpr int32_t BACKSPACE_ICON_DY = 8, CONFIRM_ICON_DY = 7;
constexpr uint32_t WHITE = 0x10000;
constexpr uint32_t LINE_STARTS = TEXT_LAYOUT_OUT, LINE_BUFFER = DIALOG_TEXT;
constexpr uint32_t CODE_BACKSPACE = 0x3a, CODE_CONFIRM = 0x3b;
// The wheel's images (draw.h image layout).
constexpr uint32_t ARROW_LEFT_IMAGE = GAME_STATE + 0x84bf0,
                   ARROW_RIGHT_IMAGE = GAME_STATE + 0x84c2c;
constexpr uint32_t CONFIRM_IMAGE = GAME_STATE + 0x84c68, BACKSPACE_IMAGE = GAME_STATE + 0x84ca4;
constexpr uint32_t GLYPH_WIDE_OFFSET = 0x1c,
                   GLYPH_OFFSET = 0x2c;  // the one-glyph strings in the frame

int32_t signed_width(ImageRecord& image) {
    return static_cast<int32_t>(image.width);
}

// The white box around the selected glyph: four lines.
void selection_box_draw(int32_t left, int32_t top, int32_t width, int32_t height) {
    const uint32_t x0 = to_fixed(left), x1 = static_cast<uint32_t>(left + width) << 16;
    const uint32_t y0 = to_fixed(top), y1 = static_cast<uint32_t>(top + height) << 16;
    line_draw(x0, y0, x1, y0, WHITE, WHITE, WHITE, WHITE, Blend::Keyed);
    line_draw(x1, y0, x1, y1, WHITE, WHITE, WHITE, WHITE, Blend::Keyed);
    line_draw(x1, y1, x0, y1, WHITE, WHITE, WHITE, WHITE, Blend::Keyed);
    line_draw(x0, y1, x0, y0, WHITE, WHITE, WHITE, WHITE, Blend::Keyed);
}

}  // namespace

// 0x18013848 — the name-entry screen: the name so far with a blinking caret, the prompt on
// its panel, and the glyph wheel below it with the selected glyph boxed. Answers 1.
uint32_t name_entry_render() {
    GuestScratch frame(4 * 9 + 0x6c);
    const uint32_t text = GAME_STATE + game_state::TEXT;
    FontRecord& small_font = as_font(screen_state().font_object);
    FontRecord& large_font = as_font(screen_state().text_layout);
    const int32_t small_line = static_cast<int32_t>(small_font.line_height);
    const int32_t large_line = static_cast<int32_t>(large_font.line_height);
    const int32_t panel_y = small_line + static_cast<int32_t>(LOGO_CLEARANCE);
    const int32_t panel_height = static_cast<int32_t>(PANEL_BOTTOM) - panel_y;

    background_draw(Blend::Alpha);
    logo_draw(Blend::Additive);
    if (panel_scale_step()) {
        panel_draw_scaled(PANEL_X, panel_y, PANEL_WIDTH, panel_height);
        return 1;
    }
    panel_draw(PANEL_X, panel_y, PANEL_WIDTH, panel_height);

    // The name, and the caret after it on the blink's on half.
    const int32_t name_y = NAME_TOP + halve(panel_y - large_line - NAME_TOP);
    text_draw(large_font, EDIT_BUFFER, NAME_X, name_y, Align::Left);
    const libc::Division division = libc::signed_divide(text_block().frame_count, CARET_PERIOD);
    if (division.remainder < CARET_ON_FRAMES) {
        int32_t name_width = 2;
        if (static_cast<uint32_t>(menu_state().name_length) != 0) {
            name_width = static_cast<int32_t>(text_width(large_font, EDIT_BUFFER)) + 2;
        }
        rect_fill(to_fixed(name_width + NAME_X), to_fixed(large_line + name_y - 2),
                  to_fixed(large_font.cell_width), to_fixed(CARET_HEIGHT), WHITE, WHITE, WHITE,
                  WHITE, Blend::KeyedAlt);
    }

    // The prompt, line by line as text layout broke it.
    int32_t y = panel_y + static_cast<int32_t>(PANEL_TEXT_INSET);
    const int32_t prompt_lines = static_cast<int32_t>(menu_state().byte_29);
    for (int32_t line = 0; line < prompt_lines; ++line) {
        const uint32_t start = guest_array<uint32_t>(LINE_STARTS)[line];
        const uint32_t length = guest_array<uint32_t>(LINE_STARTS)[line + 1] - start;
        if (wide_text()) {
            guest<uint16_t>(LINE_BUFFER) = static_cast<uint16_t>(0);
            wide_string_copy_n(LINE_BUFFER, NAME_ENTRY_TEXT + start * 2, length);
            guest<uint16_t>(LINE_BUFFER + length * 2) = static_cast<uint16_t>(0);
        } else {
            guest<uint8_t>(LINE_BUFFER) = static_cast<uint8_t>(0);
            string_copy_n(LINE_BUFFER, NAME_ENTRY_TEXT + start, length);
            guest<uint8_t>(LINE_BUFFER + length) = static_cast<uint8_t>(0);
        }
        text_draw(large_font, LINE_BUFFER, static_cast<int32_t>(SCREEN_CENTRE_X), y, Align::Centre);
        y += large_line;
    }

    // The wheel: `visible` glyphs in cells of the small font's width, the cursor kept in the
    // middle except near either end.
    const int32_t cell = static_cast<int32_t>(small_font.cell_width);
    const int32_t visible = static_cast<int32_t>(static_cast<uint32_t>(menu_state().visible_rows));
    const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    const int32_t count = static_cast<int32_t>(static_cast<int8_t>(menu_state().item_count));
    int32_t first = cursor - halve(visible);
    if (first < 0) {
        first = 0;
    } else if (count - visible <= first) {
        first = count - visible;
    }
    const int32_t wheel_top = y + static_cast<int32_t>(PANEL_TEXT_INSET);
    const int32_t wheel_y = wheel_top + halve(WHEEL_BOTTOM - small_line - wheel_top);
    const int32_t half_cell = halve(cell);
    const int32_t x0 = half_cell + halve(static_cast<int32_t>(SCREEN_WIDTH) - cell * visible);
    if (first > 0) {
        image_draw(WHEEL_ARROW_LEFT_X, wheel_y + WHEEL_ARROW_DY, as_image(ARROW_LEFT_IMAGE).width,
                   as_image(ARROW_LEFT_IMAGE).height, as_image(ARROW_LEFT_IMAGE), 0, 0, 1,
                   Blend::KeyedAlt);
    }
    if (count - visible > first) {
        image_draw(WHEEL_ARROW_RIGHT_X, wheel_y + WHEEL_ARROW_DY, as_image(ARROW_RIGHT_IMAGE).width,
                   as_image(ARROW_RIGHT_IMAGE).height, as_image(ARROW_RIGHT_IMAGE), 0, 0, 1,
                   Blend::KeyedAlt);
    }
    int32_t selected_x;
    if (cursor < halve(visible)) {
        selected_x = cell * cursor + x0;
    } else if (count - halve(visible) <= cursor) {
        selected_x = cell * (cursor - first) + x0;
    } else {
        selected_x = static_cast<int32_t>(SCREEN_CENTRE_X);
    }
    selection_box_draw(selected_x - half_cell, wheel_y - 1, cell + 1, small_line + 5);

    // The glyphs themselves, from the alphabet resource; two positions are icons.
    resource_load(as_pack(game_state_block().pack_handle), RESOURCE_ALPHABET, GLYPH_CODES, 0x800);
    int32_t x = x0;
    for (int32_t i = first; i < visible + first; ++i, x += cell) {
        const uint32_t index = static_cast<uint32_t>(i);
        uint32_t glyph;
        int32_t code;
        if (wide_text()) {
            glyph = frame.at(GLYPH_WIDE_OFFSET);
            guest<uint16_t>(glyph) = static_cast<uint16_t>(0);
            wide_string_copy_n(glyph, GLYPH_CODES + index * 2, 1);
            guest<uint16_t>(glyph + 2) = static_cast<uint16_t>(0);
            code = static_cast<int32_t>(guest_array<int16_t>(ALPHABET)[index]);
        } else {
            glyph = frame.at(GLYPH_OFFSET);
            guest<uint8_t>(glyph) = static_cast<uint8_t>(0);
            string_copy_n(glyph, GLYPH_CODES + index, 1);
            guest<uint8_t>(glyph + 1) = static_cast<uint8_t>(0);
            code = static_cast<int32_t>(guest_array<int8_t>(ALPHABET)[index]);
        }
        if (code == static_cast<int32_t>(CODE_BACKSPACE)) {
            image_draw(x - halve(signed_width(as_image(BACKSPACE_IMAGE))),
                       wheel_y + BACKSPACE_ICON_DY, as_image(BACKSPACE_IMAGE).width,
                       as_image(BACKSPACE_IMAGE).height, as_image(BACKSPACE_IMAGE), 0, 0, 1,
                       Blend::KeyedAlt);
        } else if (code == static_cast<int32_t>(CODE_CONFIRM)) {
            image_draw(x - halve(signed_width(as_image(CONFIRM_IMAGE))), wheel_y + CONFIRM_ICON_DY,
                       as_image(CONFIRM_IMAGE).width, as_image(CONFIRM_IMAGE).height,
                       as_image(CONFIRM_IMAGE), 0, 0, 1, Blend::KeyedAlt);
        } else {
            text_draw(small_font, glyph, x, wheel_y, Align::Centre);
        }
    }
    as_text(text).byte_72d = static_cast<uint8_t>(1);
    return 1;
}

}  // namespace minigolf::game
