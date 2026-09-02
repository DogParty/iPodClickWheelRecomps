// The course carousel (screen 5): the three courses as a vertical filmstrip of pictures, with
// the locked ones shown as placeholders.
//
// Reached from Game Modes (and from the dialog that asks before a new game erases the saved
// one). The pictures are course-pack resources 7, 8 and 9; a course that is not unlocked yet
// gets a built-in 222×146 placeholder instead. The strip scrolls so the remembered course is
// in view (CAROUSEL_SCROLL, 166 pixels per course).
#include "course_select.h"

#include "calling.h"
#include "cheats.h"
#include "course.h"
#include "draw.h"
#include "fixed.h"
#include "game_state.h"
#include "libc.h"
#include "menu.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"
#include "strings.h"
#include "ui.h"

namespace minigolf::game {

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t TITLE_TEXTURE_TARGET = 0x1801'be6c;
constexpr uint32_t FIRST_PICTURE_RESOURCE = 7;
constexpr uint32_t PLACEHOLDER_HEADERS[] = {0x1803'3fac, 0x1803'3fd0};  // for courses 1 and 2
constexpr uint32_t PLACEHOLDER_HEADER_SIZE = 0x24;
constexpr uint32_t PLACEHOLDER_WIDTH = 0xde, PLACEHOLDER_HEIGHT = 0x92;
constexpr uint32_t SLIDE_X = 0x31 << 16;        // 16.16: every slide at x = 49
constexpr uint32_t SLIDE_FIRST_Y = 0x2f << 16;  // 16.16: the first slide at y = 47
constexpr uint32_t SLIDE_PITCH = 0xa6 << 16;    // 166 pixels between slides
constexpr uint32_t SLIDE_SPEED = 0xb1111;       // 16.16, how fast the strip scrolls
constexpr uint32_t CAROUSEL_SLIDE_FRAMES = 15;
constexpr uint32_t SOUND_LOCKED = 5;
constexpr uint32_t TEXT_COURSE_NAME_FIRST = 0x26, TEXT_LOCKED_NAME = 0x6b, TEXT_BEST_ROUND = 0x46;
constexpr uint32_t FILM_HOLE_IMAGE = GAME_STATE + 0x84ec0;   // the sprocket hole down each side
constexpr uint32_t FILM_HOLE_COLUMNS = 6;                    // holes drawn per side
constexpr uint32_t SLIDE_WIDTH = 0xde, SLIDE_HEIGHT = 0x92;  // every slide is drawn this size
constexpr uint32_t BEST_ROUND_Y = 0xb4;

uint32_t picture(uint32_t course) {
    return COURSE_PICTURES + course * course_picture::SIZE;
}

Slide& slide(uint32_t course) {
    return text_block().slides[course];
}

// Load a course's picture from the pack, or give it the placeholder when it is still locked.
void picture_load(uint32_t course, uint32_t unlocked) {
    const uint32_t target = picture(course);
    if (course < unlocked) {
        image_apply(as_image(target), 0, as_pack(game_state_block().pack_handle),
                    FIRST_PICTURE_RESOURCE + course);
        as_course_picture(target).use_count = as_course_picture(target).use_count + 2;
        return;
    }
    libc::memory_copy(target + course_picture::USE_COUNT, PLACEHOLDER_HEADERS[course - 1],
                      PLACEHOLDER_HEADER_SIZE);
    as_course_picture(target).width = PLACEHOLDER_WIDTH;
    as_course_picture(target).height = PLACEHOLDER_HEIGHT;
    as_course_picture(target).locked = static_cast<uint8_t>(1);
    as_course_picture(target).use_count = 0;
}

}  // namespace

// 0x18005980 — build the filmstrip and show it at the remembered course.
void course_select_enter() {
    GuestScratch frame(4 * 9 + 12);
    menu_items() = nullptr;
    const int32_t unlocked = static_cast<int32_t>(courses_available());
    if (unlocked <= 0) {
        assert_trap(0x180059a0u);
    }
    if (unlocked > static_cast<int32_t>(COURSE_COUNT)) {
        assert_trap(0x180059acu);
    }

    texture_from_pixels(as_image(TITLE_IMAGE), 1, TITLE_IMAGE_WIDTH, TITLE_IMAGE_HEIGHT,
                        TITLE_TEXTURE_TARGET, 0);

    const uint32_t courses_unlocked = static_cast<uint32_t>(unlocked);
    picture_load(0, courses_unlocked);
    picture_load(1, courses_unlocked);
    picture_load(2, courses_unlocked);

    for (uint32_t course = 0; course < COURSE_COUNT; ++course) {
        slide(course).picture = picture(course);
        slide(course).x = SLIDE_X;
        slide(course).y = SLIDE_FIRST_Y + course * SLIDE_PITCH;
        slide(course).speed = SLIDE_SPEED;
    }
    text_block().byte_72d = static_cast<uint8_t>(0);
    screen_install(course_select_handle_event, carousel_slide_tick, course_select_render, nullptr);

    const int32_t course = static_cast<int32_t>(menu_state().course);
    text_block().carousel_course = static_cast<uint8_t>(static_cast<uint32_t>(course));
    text_block().carousel_count = static_cast<uint8_t>(COURSE_COUNT);
    if (course < 0 || course >= static_cast<int32_t>(COURSE_COUNT)) {
        assert_trap(0x18005b80u);
    }
    game_state_block().carousel_scroll = static_cast<uint32_t>(course) * SLIDE_PITCH;
    screen_set(5);
    screen_state().phase = static_cast<uint8_t>(PHASE_STEADY);
}

// 0x18013760 — a course picked, or Menu back to Game Modes. Picking a locked course only
// plays the refusal sound. Picking a different course than the one loaded asks the flow to
// load it; picking the loaded one goes straight on — to Select Hole in practice, else hole 1.
uint32_t course_select_handle_event(uint32_t event) {
    const uint32_t text = GAME_STATE + game_state::TEXT;
    if (event == EVENT_MENU) {
        const int32_t mode = static_cast<int32_t>(
            static_cast<uint32_t>(static_cast<int8_t>(menu_state().game_mode)));
        if (mode >= 0 && mode <= static_cast<int32_t>(MODE_PRACTICE_HOLE)) {
            text_block().menu_return_row = static_cast<uint8_t>(static_cast<uint32_t>(mode));
        }
        texture_release(as_image(TITLE_IMAGE));
        game_modes_enter();
        return 0;
    }
    if (event != EVENT_SELECT) {
        return 0;
    }
    const int32_t chosen = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    if (chosen >= static_cast<int32_t>(courses_available())) {
        menu_sound_play(SOUND_LOCKED);
        return 0;
    }
    texture_release(as_image(TITLE_IMAGE));
    if (chosen != static_cast<int32_t>(menu_state().course)) {
        course_load_request(static_cast<uint32_t>(chosen), 1);
        return 0;
    }
    as_text(text).hole = static_cast<uint8_t>(0);
    if (static_cast<uint32_t>(menu_state().game_mode) == MODE_PRACTICE_HOLE) {
        text_block().menu_return_row = static_cast<uint8_t>(0xff);
        hole_select_enter();
    } else {
        course_start(0);
    }
    return 0;
}

// --- render ------------------------------------------------------------------------------------

namespace {

bool wide_text() {
    return static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
}

// One slide of the filmstrip: the course's picture squeezed into the slide's frame (a locked
// course's placeholder is already that size), at the slide's position less the scroll.
void slide_draw(uint32_t slot, uint32_t unlocked) {
    ImageRecord& picture = as_image(slide(slot).picture);
    ImageRecord& texture = as_image(TITLE_IMAGE + picture.texture_index * TEXTURE_TABLE_STRIDE);
    uint32_t width = SCREEN_WIDTH, height = SCREEN_HEIGHT;
    if (slot >= unlocked) {
        width = PLACEHOLDER_WIDTH;
        height = PLACEHOLDER_HEIGHT;
    }
    const uint32_t u = picture.u;
    const uint32_t v0 = texture.height - picture.v - height;
    const uint32_t x = slide(slot).x >> 16;
    const uint32_t y = (slide(slot).y - game_state_block().carousel_scroll) >> 16;
    TexturedQuad quad;
    quad.u0 = to_fixed(u);
    quad.v0 = to_fixed(v0);
    quad.u1 = (u + width) << 16;
    quad.v1 = (v0 + height) << 16;
    quad.x0 = to_fixed(x);
    quad.y0 = to_fixed(y);
    quad.x1 = (x + SLIDE_WIDTH) << 16;
    quad.y1 = to_fixed(y);
    quad.x2 = (x + SLIDE_WIDTH) << 16;
    quad.y2 = (y + SLIDE_HEIGHT) << 16;
    quad.x3 = to_fixed(x);
    quad.y3 = (y + SLIDE_HEIGHT) << 16;
    quad.alpha = 0x10000;
    quad.texture = texture.texture_name;
    quad.blend = Blend::Additive;
    textured_quad_draw(quad);
}

}  // namespace

// 0x18014734 — draw the carousel: black, the three slides (the one in view and its neighbours,
// by slot), the sprocket holes scrolling with them, and — once the strip has settled — the
// course's name and its best round.
uint32_t course_select_render() {
    const uint32_t text = GAME_STATE + game_state::TEXT;
    rect_fill(0, 0, to_fixed(SCREEN_WIDTH), to_fixed(SCREEN_HEIGHT), 0, 0, 0, 0x10000,
              Blend::Opaque);

    const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    const int32_t unlocked = static_cast<int32_t>(courses_available());
    for (int32_t i = cursor; i < cursor + static_cast<int32_t>(COURSE_COUNT); ++i) {
        const libc::Division division = libc::signed_divide(static_cast<uint32_t>(i), COURSE_COUNT);
        slide_draw(division.remainder, static_cast<uint32_t>(unlocked));
    }

    const uint32_t hole_height = as_image(FILM_HOLE_IMAGE).height;
    const uint32_t hole_width = as_image(FILM_HOLE_IMAGE).width;
    const int32_t scroll = to_whole(game_state_block().carousel_scroll);
    const libc::Division wrapped = libc::signed_divide(static_cast<uint32_t>(-scroll), hole_height);
    int32_t y =
        static_cast<int32_t>(wrapped.remainder);  // the remainder: the holes wrap with the scroll
    for (uint32_t i = 0; i < FILM_HOLE_COLUMNS; ++i, y += static_cast<int32_t>(hole_height)) {
        image_draw(0, y, hole_width, hole_height, as_image(FILM_HOLE_IMAGE), 0, 0, 0,
                   Blend::KeyedAlt);
        image_draw(static_cast<int32_t>(SCREEN_WIDTH - hole_width), y, hole_width, hole_height,
                   as_image(FILM_HOLE_IMAGE), 0, 0, 0, Blend::KeyedAlt);
    }

    if (static_cast<uint32_t>(screen_state().phase) != PHASE_STEADY) {
        return 0;
    }
    PackRecord& pack = as_pack(game_state_block().pack_handle);
    const uint32_t best_round = screen_state().best_round[static_cast<uint32_t>(cursor)];
    bool show_best = false;
    if (cursor >= unlocked) {
        resource_load(pack, TEXT_LOCKED_NAME, SCRATCH_TEXT, 0x800);
    } else {
        resource_load(pack, TEXT_COURSE_NAME_FIRST + static_cast<uint32_t>(cursor), SCRATCH_TEXT,
                      0x800);
        show_best = best_round != NO_BEST_ROUND;
        if (show_best) {  // "BEST ROUND: " + the number
            if (wide_text()) {
                guest<uint16_t>(DIALOG_TEXT) = static_cast<uint16_t>(0);
                resource_load(pack, TEXT_BEST_ROUND, DIALOG_MESSAGE, 0x800);
                wide_number_to_string(DIALOG_TEXT, static_cast<int32_t>(best_round),
                                      static_cast<int32_t>(0));
                wide_string_append(DIALOG_MESSAGE, DIALOG_TEXT);  // wide_string_append
            } else {
                guest<uint8_t>(DIALOG_TEXT) = static_cast<uint8_t>(0);
                resource_load(pack, TEXT_BEST_ROUND, DIALOG_MESSAGE, 0x800);
                number_to_string(DIALOG_TEXT, static_cast<int32_t>(best_round),
                                 static_cast<int32_t>(0));
                string_append(DIALOG_MESSAGE, DIALOG_TEXT);  // string_append
            }
        }
    }
    const uint32_t name_font =
        (wide_text() ? screen_state().text_layout : screen_state().font_object);
    const int32_t name_y = halve(static_cast<int32_t>(SCREEN_HEIGHT) -
                                 2 * static_cast<int32_t>(as_font(name_font).line_height));
    text_draw(as_font(name_font), SCRATCH_TEXT, static_cast<int32_t>(SCREEN_CENTRE_X), name_y,
              Align::Centre);
    if (show_best) {
        text_draw(as_font(screen_state().text_layout), DIALOG_MESSAGE,
                  static_cast<int32_t>(SCREEN_CENTRE_X), static_cast<int32_t>(BEST_ROUND_Y),
                  Align::Centre);
    }
    as_text(text).byte_72d = static_cast<uint8_t>(1);
    return 0;
}

// 0x18014b40 — a frame of the course carousel sliding in or out: the scroll moves by the
// title layer's speed, and after fifteen frames the screen is steady.
void carousel_slide() {
    const int32_t phase = static_cast<int32_t>(static_cast<int8_t>(screen_state().phase));
    const uint32_t speed = game_state_block().text[119];
    uint32_t scroll = game_state_block().carousel_scroll;
    if (phase == static_cast<int32_t>(PHASE_SLIDE_IN)) {
        scroll -= speed;
    } else if (phase == static_cast<int32_t>(PHASE_SLIDE_OUT)) {
        scroll += speed;
    } else {
        return;
    }
    game_state_block().carousel_scroll = scroll;
    if (text_block().frame_count == CAROUSEL_SLIDE_FRAMES) {
        screen_state().phase = static_cast<uint8_t>(PHASE_STEADY);
        text_block().frame_count = 0;
    }
}

// --- screen entry points ---------------------------------------------------------------------
// Each opens its screen and then forgets any input still pending, so a press that
// chose the screen cannot also act on it.

void course_select_screen_enter() {
    course_select_enter();
    wheel_slots_clear();
}

void carousel_slide_tick(uint32_t /*milliseconds*/) {
    carousel_slide();
}

}  // namespace minigolf::game
