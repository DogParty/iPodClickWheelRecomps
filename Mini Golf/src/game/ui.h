// The pieces of interface every menu-like screen draws: the background and the dimmed
// overlay, the logo, the rounded panel, and the highlighted row — a spinning ball beside
// letters that ripple up and down.
#pragma once

#include "draw.h"
#include "state.h"

#include <cstdint>

namespace minigolf::game {

// The full-screen background image (the sky and the course), then half-transparent black over
// it, drawn with `dim_blend` (the menus and the dialog differ here).
// The original's signed division by two, which rounds towards zero rather than down: it appears
// wherever something is centred, and the difference shows on odd widths.
[[nodiscard]] int32_t halve(int32_t value);

void background_draw(Blend dim_blend);

// The "MINI GOLF" logo across the top (the screens differ in how they blend it).
void logo_draw(Blend blend);

// The rounded, course-tinted panel messages sit on (0x18004d0c), and the same panel scaled by
// the current panel scale about its centre while it grows in (0x18005168).
void panel_draw(int32_t x, int32_t y, int32_t width, int32_t height);
void panel_draw_scaled(int32_t x, int32_t y, int32_t width, int32_t height);

// Advance the panel's grow-in scale by its step, stopping at full size or nothing. Returns
// whether the panel is still growing (true) or settled (false).
bool panel_scale_step();

// The panel message over a dimmed screen (0x180109cc): a title line when `titled`, the small
// font when `small`.
void panel_message_draw(uint32_t titled, uint32_t small);

// The selected row: the ball (spinning once every five frames, three frames per turn) to the
// left of `x` when `with_ball`, then the letters of the text in SCRATCH_TEXT drawn one by one,
// each lifted by the ripple table at its own phase. `row_y` is the row's baseline.
// `returns` are the return addresses of the calls the original made, which differ between the
// two renderers that share this routine.
struct HighlightReturns {
    uint32_t ball, wide_length, length, glyph_wide, glyph;
};
void highlighted_row_draw(FontRecord& font, int32_t x, int32_t row_y, bool with_ball);

}  // namespace minigolf::game
