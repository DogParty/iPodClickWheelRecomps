#include "draw.h"

#include "game_state.h"
#include "runtime/memory.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t BLEND_TEXT = 5;  // how text_draw_at (0x18007934) blends glyphs

uint32_t handle() {
    return game_state_block().handle;
}

}  // namespace

void image_draw(int32_t x, int32_t y, uint32_t width, uint32_t height, ImageRecord& image,
                uint32_t u, uint32_t v, uint32_t variant, Blend blend) {
    image_draw_at(handle(), x, y, static_cast<int32_t>(width), static_cast<int32_t>(height), image,
                  static_cast<int32_t>(u), static_cast<int32_t>(v), variant,
                  static_cast<uint32_t>(blend));
}

void rect_fill(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t red,
               uint32_t green, uint32_t blue, uint32_t alpha, Blend blend) {
    rect_fill_at(handle(), x, y, width, height, red, green, blue, alpha,
                 static_cast<uint32_t>(blend));
}

void line_draw(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t red, uint32_t green,
               uint32_t blue, uint32_t alpha, Blend blend) {
    line_draw_at(handle(), x0, y0, x1, y1, red, green, blue, alpha, static_cast<uint32_t>(blend));
}

void textured_quad_draw(const TexturedQuad& quad) {
    // The quad's corners and the texel rectangle's corners in the batcher's pair order.
    const uint32_t xs[4] = {quad.x0, quad.x1, quad.x2, quad.x3},
                   ys[4] = {quad.y0, quad.y1, quad.y2, quad.y3};
    const uint32_t us[4] = {quad.u0, quad.u1, quad.u1, quad.u0},
                   vs[4] = {quad.v0, quad.v0, quad.v1, quad.v1};
    textured_quad_push(xs, ys, us, vs, quad.texture, static_cast<uint32_t>(quad.blend));
}

void text_draw(FontRecord& font, uint32_t text, int32_t x, int32_t y, Align align) {
    text_draw_at(handle(), font, text, x, y, align, BLEND_TEXT);
}

void glyph_draw(FontRecord& font, uint32_t glyph, int32_t x, int32_t y) {
    glyph_draw_at(handle(), font, glyph, x, y);
}

}  // namespace minigolf::game
