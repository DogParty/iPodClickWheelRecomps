#include "ui.h"

#include "calling.h"
#include "draw.h"
#include "fixed.h"
#include "game_state.h"
#include "libc.h"
#include "menu.h"
#include "records.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"
#include "strings.h"

namespace minigolf::game {

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t HALF = 0x8000;  // 16.16

// Images the pack loader filled in at start-up (draw.h image layout).
constexpr uint32_t BACKGROUND_IMAGE = GAME_STATE + game_state::BLOCK_84CE0;
constexpr uint32_t LOGO_IMAGE = GAME_STATE + 0x84a88;
constexpr uint32_t BALL_IMAGE = GAME_STATE + 0x84d1c;       // three frames of 24×24, side by side
constexpr uint32_t BALL_SLOT_IMAGE = GAME_STATE + 0x84d58;  // its size places the ball
constexpr uint32_t BALL_FRAME_SIZE = 24;
constexpr uint32_t BALL_GAP = 0x1d;  // pixels between the ball's slot and the text
constexpr uint32_t BALL_FRAMES = 3, BALL_FRAME_PERIOD = 5;

// The ripple: a table of 256 signed heights, stepped through eight entries per letter and
// eight more per frame, scaled down by 0x1333.
constexpr uint32_t RIPPLE_TABLE = GAME_STATE + 0x8531c;
constexpr uint32_t RIPPLE_PHASE = GAME_STATE + game_state::SETTINGS + 0x5b4;
constexpr uint32_t RIPPLE_STEP = 8, RIPPLE_SCALE = 0x1333;

constexpr uint32_t PANEL_SCALE_FULL = 0x10000;
// The panel's pieces: the corner images (12×11, the tinted shape and the white rim) and the
// course's tint from its record in the course information table.
constexpr uint32_t CORNER_FILL_IMAGE = GAME_STATE + 0x84dd0,
                   CORNER_RIM_IMAGE = GAME_STATE + 0x84b3c;
constexpr uint32_t CORNER_WIDTH = 0xc, CORNER_HEIGHT = 0xb;
constexpr uint32_t EDGE_WHITE = 0xff00;  // 255/256 in 16.16; the edges are fully opaque
constexpr uint32_t OPAQUE = 0x10000;

constexpr uint32_t FIRST_GLYPH = 0x20;  // advances are indexed from the space
constexpr uint32_t GLYPH_WIDE_OFFSET = 0x10, GLYPH_OFFSET = 0x14;  // in the renderer's frame

// The ball on the frame it is currently showing, advanced every fifth frame.
void ball_draw(FontRecord& font, int32_t x, int32_t row_y) {
    const int32_t ball_y =
        row_y - halve(static_cast<int32_t>(as_image(BALL_SLOT_IMAGE).height - font.line_height));
    const int32_t ball_x =
        x - static_cast<int32_t>(as_image(BALL_SLOT_IMAGE).width) - static_cast<int32_t>(BALL_GAP);
    const int32_t ball_frame = static_cast<int32_t>(menu_state().ball_frame);
    image_draw(ball_x, ball_y, BALL_FRAME_SIZE, BALL_FRAME_SIZE, as_image(BALL_IMAGE),
               static_cast<uint32_t>(ball_frame * static_cast<int32_t>(BALL_FRAME_SIZE)), 0,
               static_cast<uint32_t>(as_image(BALL_IMAGE).variant), Blend::KeyedAlt);
    const libc::Division division =
        libc::signed_divide(text_block().frame_count, BALL_FRAME_PERIOD);
    if (division.remainder == 0) {
        const int32_t next =
            static_cast<int8_t>(static_cast<uint32_t>(menu_state().ball_frame) + 1);
        menu_state().ball_frame = static_cast<int8_t>(static_cast<uint8_t>(
            next == static_cast<int32_t>(BALL_FRAMES) ? 0u : static_cast<uint32_t>(next)));
    }
}

}  // namespace

int32_t halve(int32_t value) {  // the original's signed division by two
    return (value + static_cast<int32_t>(static_cast<uint32_t>(value) >> 31)) >> 1;
}

void background_draw(Blend dim_blend) {
    image_draw(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, as_image(BACKGROUND_IMAGE), 0, 0, 0,
               Blend::Opaque);
    rect_fill(0, 0, to_fixed(SCREEN_WIDTH), to_fixed(SCREEN_HEIGHT), 0, 0, 0, HALF, dim_blend);
}

void logo_draw(Blend blend) {
    const uint32_t width = as_image(LOGO_IMAGE).width;
    image_draw(halve(static_cast<int32_t>(SCREEN_WIDTH - width)), 0, width,
               as_image(LOGO_IMAGE).height, as_image(LOGO_IMAGE), 0, 0,
               static_cast<uint32_t>(as_image(LOGO_IMAGE).variant), blend);
}

// 0x18004d0c — the panel: a body tinted with the course's colour at half alpha (its middle,
// then the strips beside it once the panel is taller than its corners), a white edge, and
// the four rounded corners, each drawn twice — the tinted shape, then the white rim over it.
void panel_draw(int32_t x, int32_t y, int32_t width, int32_t height) {
    const CourseInfo& info = course_info_at(menu_state().course);
    const uint32_t tint_r = info.tint_r << 8, tint_g = info.tint_g << 8, tint_b = info.tint_b << 8;
    const auto px = [](int32_t v) { return to_fixed(v); };
    const auto tinted = [&](int32_t bx, int32_t by, int32_t bw, int32_t bh) {
        rect_fill(px(bx), px(by), px(bw), px(bh), tint_r, tint_g, tint_b, HALF, Blend::Keyed);
    };
    const auto edge = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        line_draw(px(x0), px(y0), px(x1), px(y1), EDGE_WHITE, EDGE_WHITE, EDGE_WHITE, OPAQUE,
                  Blend::KeyedAlt);
    };
    const auto corner = [&](int32_t cx, int32_t cy, int32_t u, int32_t v) {
        image_draw(cx, cy, CORNER_WIDTH, CORNER_HEIGHT, as_image(CORNER_FILL_IMAGE),
                   static_cast<uint32_t>(u), static_cast<uint32_t>(v), 2, Blend::KeyedAlt);
        image_draw(cx, cy, CORNER_WIDTH, CORNER_HEIGHT, as_image(CORNER_RIM_IMAGE),
                   static_cast<uint32_t>(u), static_cast<uint32_t>(v), 1, Blend::Text);
    };
    const int32_t cw = static_cast<int32_t>(CORNER_WIDTH), ch = static_cast<int32_t>(CORNER_HEIGHT);
    tinted(x + cw, y + 1, width - 2 * cw, height - 2);
    if (height > 2 * ch) {
        tinted(x + 1, y + ch, cw - 1, height - 2 * ch);
        tinted(x + width - cw, y + ch, cw - 1, height - 2 * ch);
    }
    edge(x + cw, y, x + width - ch, y);
    edge(x + cw, y + height - 1, x + width - ch, y + height - 1);
    edge(x + 1, y + ch - 1, x + 1, y + height - ch);
    edge(x + width, y + ch - 1, x + width, y + height - ch);
    corner(x, y, 0, 0);
    corner(x + width - cw, y, cw, 0);
    corner(x, y + height - ch, 0, ch);
    corner(x + width - cw, y + height - ch, cw, ch);
}

// 0x18005168 — the panel scaled by the grow-in scale about its centre; nothing until it is
// at least as big as its corners.
void panel_draw_scaled(int32_t x, int32_t y, int32_t width, int32_t height) {
    const int64_t scale = static_cast<int32_t>(play_state().panel_scale);
    const int32_t scaled_width = static_cast<int32_t>((width * scale) >> 16);
    const int32_t scaled_height = static_cast<int32_t>((height * scale) >> 16);
    if (scaled_width < 2 * static_cast<int32_t>(CORNER_WIDTH) ||
        scaled_height < 2 * static_cast<int32_t>(CORNER_HEIGHT)) {
        return;
    }
    panel_draw(x + (width >> 1) - (scaled_width >> 1), y + (height >> 1) - (scaled_height >> 1),
               scaled_width, scaled_height);
}

bool panel_scale_step() {
    PlayState& play = play_state();
    if (play.panel_growing == 0) {
        return false;
    }
    const int32_t scale = static_cast<int32_t>(play.panel_scale + play.panel_scale_step);
    play.panel_scale = static_cast<uint32_t>(scale);
    if (scale >= static_cast<int32_t>(PANEL_SCALE_FULL)) {
        play.panel_scale = PANEL_SCALE_FULL;
        play.panel_growing = 0;
    } else if (scale < 0) {
        play.panel_scale = 0;
        play.panel_growing = 0;
    }
    return true;
}

void highlighted_row_draw(FontRecord& font, int32_t x, int32_t row_y, bool with_ball) {
    if (with_ball) {
        ball_draw(font, x, row_y);
    }
    // The letters, one at a time, each on its own point of the ripple.
    const bool wide = static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
    uint32_t length;
    if (wide) {
        length = wide_string_length(SCRATCH_TEXT);
    } else {
        length = string_length(SCRATCH_TEXT);
    }
    uint32_t phase = guest<uint32_t>(RIPPLE_PHASE) + RIPPLE_STEP;
    guest<uint32_t>(RIPPLE_PHASE) = phase;
    // A one-character string in the caller's frame (both renderers keep it at the same place).
    const uint32_t glyph = registers().r[SP] + (wide ? GLYPH_WIDE_OFFSET : GLYPH_OFFSET);
    for (uint32_t i = 0; i < length; ++i) {
        const libc::Division ripple_y = libc::signed_divide(
            static_cast<uint32_t>(guest_array<int16_t>(RIPPLE_TABLE)[phase & 0xff]), RIPPLE_SCALE);
        const int32_t y = static_cast<int32_t>(ripple_y.quotient) + row_y;
        phase += RIPPLE_STEP;
        int32_t advance;
        if (wide) {
            guest<uint16_t>(glyph) = static_cast<uint16_t>(guest<uint16_t>(SCRATCH_TEXT + i * 2));
            guest<uint16_t>(glyph + 2) = static_cast<uint16_t>(0);
            glyph_draw(font, glyph, x, y);
            advance = static_cast<int32_t>(text_width(font, glyph));
        } else {
            const uint32_t letter = guest<uint8_t>(SCRATCH_TEXT + i);
            guest<uint8_t>(glyph) = static_cast<uint8_t>(letter);
            guest<uint8_t>(glyph + 1) = static_cast<uint8_t>(0);
            glyph_draw(font, glyph, x, y);
            const int32_t index = static_cast<int8_t>(letter) - static_cast<int32_t>(FIRST_GLYPH);
            advance = static_cast<int32_t>(guest_array<int8_t>(font.advances)[index]);
        }
        x += advance;
    }
}

}  // namespace minigolf::game
