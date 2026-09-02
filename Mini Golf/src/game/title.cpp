// The title screen: the logo and three parallax layers slide in, bounce, pause, and slide out.
//
// Screen 0. `title_enter` (0x18005d0c) loads the four title resources, works out the layers'
// start positions from the screen geometry in 16.16 fixed point, and installs the screen.
// `title_tick` (0x18012a00) runs the three phases on the frame counter and, when the exit
// animation ends, decides where the game goes: name entry if no name has been given, back
// into a saved course, or the main menu.
#include "title.h"

#include "calling.h"
#include "course.h"
#include "draw.h"
#include "fixed.h"
#include "game_state.h"
#include "libc.h"
#include "menu.h"
#include "name_entry.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"
#include "strings.h"

namespace minigolf::game {

// Defined below; title_enter installs them.
void title_screen_tick(uint32_t /*milliseconds*/);
uint32_t title_render();
ScreenEnter title_tick();

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t RESOURCE_TITLE_LOGO = 0x140;  // + 1, 2, 3: the three layers
constexpr uint32_t LAYER_COUNT = 3;
constexpr uint32_t PHASE_BOUNCE = 2;  // PHASE_SLIDE_IN and PHASE_SLIDE_OUT come from menu.h
constexpr uint32_t BOUNCE_STEPS = 5;
constexpr uint32_t TITLE_LAYER_COUNT = 3;
constexpr uint32_t BACKGROUND_IMAGE = GAME_STATE + game_state::BLOCK_84CE0;  // the sky and course
constexpr uint32_t DROP_FRAMES_FIXED = to_fixed(30);  // the layers drop in over thirty frames

Slide& layer(uint32_t index) {
    return text_block().slides[index];
}

// A layer's speed: its height spread over the thirty frames of the drop, in 16.16 — the
// original's 64-bit division (value << 32) / (to_fixed(30)), truncated toward zero.
int32_t per_frame_of_drop(int32_t value) {
    const int64_t numerator = static_cast<int64_t>(value) << 32;
    return static_cast<int32_t>(libc::divide64(static_cast<uint32_t>(numerator),
                                               static_cast<uint32_t>(numerator >> 32),
                                               DROP_FRAMES_FIXED, 0));
}

}  // namespace

// 0x18005d0c — open the title screen: the logo's four layers from the title pack, their
// geometry, and the course backdrop behind them.
void title_enter() {
    if (game_state_block().pack_handle == 0) {
        assert_trap(0x18005d20u);
    }
    const int32_t course = static_cast<int32_t>(menu_state().course);
    const uint32_t pack = game_state_block().pack_course[static_cast<uint32_t>(course)];
    if (pack == 0) {
        assert_trap(0x18005d3cu);
    }
    if (course != 0 && course != 1 && course != 2) {
        assert_trap(0x18005d60u);
    }

    // The logo and the three layers, each applied to its block and advanced past it.
    const uint32_t blocks[4] = {GAME_STATE + game_state::BLOCK_84CE0, GAME_STATE + 0x84efc,
                                GAME_STATE + 0x84f38, GAME_STATE + 0x84f74};
    const uint32_t ids[4] = {RESOURCE_TITLE_LOGO, RESOURCE_TITLE_LOGO + 3, RESOURCE_TITLE_LOGO + 2,
                             RESOURCE_TITLE_LOGO + 1};
    for (unsigned i = 0; i < 4; ++i) {
        ImageRecord& block = as_image(blocks[i]);
        image_apply(block, i == 0 ? 0u : 1u, as_pack(pack), ids[i]);
        block.texture_index = block.texture_index + 2;
    }

    // Layer geometry, in 16.16. Each layer's width comes from its block (+0xf68, +0xfa4 ...).
    const uint32_t text = GAME_STATE + game_state::TEXT;
    layer(1).x = 0;
    layer(1).picture = blocks[2];
    const int32_t width_a = static_cast<int32_t>(as_image(blocks[2]).height);
    layer(1).y = to_fixed(-width_a);
    layer(1).speed = static_cast<uint32_t>(per_frame_of_drop(width_a));
    layer(2).y = 0xf00000;
    layer(2).picture = blocks[3];
    layer(2).x = 0;
    const int32_t width_b = static_cast<int32_t>(as_image(blocks[3]).height);
    layer(2).speed = static_cast<uint32_t>(-per_frame_of_drop(width_b));
    layer(0).picture = blocks[1];
    const int32_t width_c = static_cast<int32_t>(as_image(blocks[1]).width);
    layer(0).x = to_fixed((0x140 - width_c) / 2);
    const int32_t width_d = static_cast<int32_t>(as_image(blocks[1]).height);
    layer(0).y = to_fixed(-width_d);
    layer(0).speed = static_cast<uint32_t>(per_frame_of_drop(width_d));

    as_text(text).byte_72d = static_cast<uint8_t>(0);
    screen_install(nullptr, title_screen_tick, title_render, nullptr);
    screen_set(0);
    screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_IN);
    wheel_slots_clear();  // memclr: no input yet
}

// 0x18012a00 — returns the enter routine to hand over to (a tail call in the original), or 0.
ScreenEnter title_tick() {
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    const uint32_t frames = text_block().frame_count;

    switch (static_cast<uint32_t>(static_cast<int8_t>(screen_state().phase))) {
    case PHASE_SLIDE_IN:
        for (uint32_t i = 0; i < LAYER_COUNT; ++i) {
            layer(i).y = layer(i).y + layer(i).speed;
        }
        if (frames != TITLE_FADE_FRAMES) {
            return 0;
        }
        layer(0).word_c = BOUNCE_STEPS;
        screen_state().phase = static_cast<uint8_t>(PHASE_BOUNCE);
        text_block().frame_count = 0;
        return 0;

    case PHASE_BOUNCE: {
        const int32_t bounce = static_cast<int32_t>(layer(0).word_c);
        const uint32_t position = layer(0).y;
        if (bounce > 0) {  // the logo settles: two pixels per step
            layer(0).y = position - 0x20000;
            layer(0).word_c = static_cast<uint32_t>(bounce - 1);
        } else {
            const uint32_t next = position + 0x20000;
            layer(0).y = next;
            if (static_cast<int32_t>(next) > 0) {
                layer(0).y = 0;
                layer(0).word_c = 0;
            }
        }
        if (frames != TITLE_BOUNCE_FRAMES) {
            return 0;
        }
        screen_state().phase = static_cast<uint8_t>(PHASE_SLIDE_OUT);
        text_block().frame_count = 0;
        return 0;
    }

    case PHASE_SLIDE_OUT:
        break;
    default:
        return 0;
    }

    // Slide out, accelerating: each layer moves by its speed, and the speed grows.
    for (uint32_t i = 0; i < LAYER_COUNT; ++i) {
        const uint32_t dx = layer(i).speed;
        layer(i).y = layer(i).y - dx - dx;
    }
    if (static_cast<int32_t>(frames) > static_cast<int32_t>(TITLE_FADE_FRAMES)) {
        int32_t step = static_cast<int8_t>(static_cast<uint32_t>(menu_state().byte_2e) + 1);
        text.byte_72e = static_cast<uint8_t>(static_cast<uint32_t>(step));
        if (step > 1) {
            text.byte_72e = static_cast<uint8_t>(1);
        }
    }
    if (frames != TITLE_EXIT_FRAMES) {
        return 0;
    }

    // Where next. A name of length zero means the player has not been asked yet.
    const bool wide = static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
    const int32_t marker = static_cast<int32_t>(static_cast<uint32_t>(screen_state().byte_df4));
    const uint32_t name = SCREEN_OBJECT + screen::PLAYER_NAME;
    uint32_t length;
    if (wide) {
        if (marker != 10) {
            guest<uint16_t>(name) = static_cast<uint16_t>(0);
        }
        length = wide_string_length(name);
    } else {
        if (marker == 10) {
            guest<uint8_t>(name) = static_cast<uint8_t>(0);
        }
        length = string_length(name);
    }
    const bool no_name_yet = wide ? marker != 10 : marker == 10;
    if (length == 0 || no_name_yet) {
        return name_entry_enter;
    }
    if (static_cast<uint32_t>(game_state_block().save_data_byte_5) == 1 &&
        static_cast<uint32_t>(screen_state().byte_dd0) == 1) {
        return resume_saved_course;
    }
    return main_menu_screen_enter;
}

// 0x1800cf60 — the main menu's "resume" and the title's saved-course exit: back into the saved
// course if it belongs to the current course, otherwise abandon it.
// 0x18012a00 — the title's tick: run the phases, and hand over to the screen it chose.
void title_screen_tick(uint32_t /*milliseconds*/) {
    if (const ScreenEnter next = title_tick()) {
        next();
    }
}

// --- render ------------------------------------------------------------------------------------

// 0x1801289c — the title: the background, then the three layers at their current height. The
// middle layer (the course name) is drawn keyed on the course's second variant until a course
// is chosen.
uint32_t title_render() {
    const uint32_t text = GAME_STATE + game_state::TEXT;
    image_draw(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, as_image(BACKGROUND_IMAGE), 0, 0, 0,
               Blend::Opaque);
    for (uint32_t i = 0; i < TITLE_LAYER_COUNT; ++i) {
        ImageRecord& image = as_image(layer(i).picture);
        Blend blend = Blend::Keyed;
        if (i == 2 || (i == 1 && static_cast<uint32_t>(menu_state().course) == 0)) {
            blend = Blend::KeyedAlt;
        }
        image_draw(to_whole(layer(i).x), to_whole(layer(i).y), image.width, image.height, image, 0,
                   0, static_cast<uint32_t>(image.variant), blend);
    }
    as_text(text).byte_72d = static_cast<uint8_t>(1);
    return 0;
}

void resume_saved_course() {
    int32_t saved_course;
    bool same_course;
    {
        menu_state().game_mode = static_cast<uint8_t>(MODE_SINGLE_PLAYER);
        saved_course = static_cast<int32_t>(static_cast<uint32_t>(
            static_cast<int8_t>(screen_block_byte(game_state::SCREEN_BYTE_9A))));
        same_course = saved_course == static_cast<int32_t>(menu_state().course);
        if (!same_course) {
            text_block().byte_745 = static_cast<uint8_t>(static_cast<uint32_t>(saved_course));
        } else {
            course_resume();
        }
    }
    if (same_course) {
        course_start(1);
    } else {
        course_load_request(static_cast<uint32_t>(saved_course), 0);
    }
}

}  // namespace minigolf::game
