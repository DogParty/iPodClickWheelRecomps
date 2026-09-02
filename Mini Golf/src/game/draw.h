// The 2-D drawing calls the screens are built from. The bodies behind them — a vertex batcher
// over OpenGL ES (0x1800740c, 0x18016984 and the renderer object at 0x1801a5a4) — are still
// recompiled; these wrappers give their arguments names and carry the ones that travel on the
// stack, so a screen's render routine reads as drawing, not as register shuffling.
//
// Coordinates are screen pixels (320×240, origin top-left) unless a parameter says 16.16.
#pragma once

#include "state.h"

#include <cstdint>

namespace minigolf::game {

// An image the pack loader filled in (course_pack_apply): where it sits in its texture and how
// big it is. The course pictures, the background and the sprite sheets all have this shape.
namespace image {
constexpr uint32_t TEXTURE_INDEX = 0x08;  // into the 60-byte texture table after TITLE_IMAGE
constexpr uint32_t U = 0x0c, V = 0x10;    // texel offset of the image inside the texture
constexpr uint32_t WIDTH = 0x2c, HEIGHT = 0x30;
constexpr uint32_t VARIANT = 0x38;  // byte 0..2: which of the image's variants to draw
constexpr uint32_t SIZE = 0x3c;
}  // namespace image

// How a draw combines with what is already on screen (the last argument of every draw).
enum class Blend : uint32_t {
    Opaque = 0,
    Alpha = 1,
    Additive = 2,
    Keyed = 3,     // the colour-key transparency the sprites use
    KeyedAlt = 4,  // the same with the image's second variant (inferred from use)
    Text = 5,      // the glyphs' blend, also the panel's white rim
};

// Draw `width`×`height` pixels of `image`, starting `u`,`v` texels into it, at x,y (0x18008e9c).
// `variant` picks one of the image's up-to-three variants.
void image_draw(int32_t x, int32_t y, uint32_t width, uint32_t height, ImageRecord& image,
                uint32_t u, uint32_t v, uint32_t variant, Blend blend);

// Fill a rectangle with a colour; position, size and colour are all 16.16 (0x180071e4).
void rect_fill(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t red,
               uint32_t green, uint32_t blue, uint32_t alpha, Blend blend);

// A one-pixel line from (x0, y0) to (x1, y1), all 16.16, in a colour (0x18007314).
void line_draw(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t red, uint32_t green,
               uint32_t blue, uint32_t alpha, Blend blend);

// The four corners of a textured quad and the texel rectangle mapped onto it, all 16.16.
struct TexturedQuad {
    uint32_t u0, v0, u1, v1;  // texel rectangle: left, top, right, bottom
    uint32_t x0, y0, x1, y1, x2, y2, x3,
        y3;            // corners: top-left, top-right, bottom-right, bottom-left
    uint32_t alpha;    // 16.16
    uint32_t texture;  // the GL texture name
    Blend blend;
};

// Draw a textured quad straight from a texture, bypassing the image table (0x1800740c). Unlike
// the other primitives it takes no handle.
void textured_quad_draw(const TexturedQuad& quad);

enum class Align : uint32_t { Centre = 0, Left = 1, Right = 2 };

// Draw a string with a font at x (the anchor `align` names), baseline y (0x18007934).
void text_draw(FontRecord& font, uint32_t text, int32_t x, int32_t y, Align align);

// Draw one glyph (a one-character string) of a font at x, y (0x18007be8).
void glyph_draw(FontRecord& font, uint32_t glyph, int32_t x, int32_t y);

// The batcher's and the text renderer's entry points (batcher.cpp, text.cpp), which the
// wrappers above call with the game's render handle.
void image_draw_at(uint32_t handle, int32_t x, int32_t y, int32_t width, int32_t height,
                   ImageRecord& image, int32_t u, int32_t v, uint32_t variant,
                   uint32_t blend);  // 0x18008e9c
void rect_fill_at(uint32_t handle, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t r, uint32_t g, uint32_t b, uint32_t a,
                  uint32_t blend);  // 0x180071e4
void line_draw_at(uint32_t handle, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t r,
                  uint32_t g, uint32_t b, uint32_t a, uint32_t blend);  // 0x18007314
void textured_quad_push(const uint32_t xs[4], const uint32_t ys[4], const uint32_t us[4],
                        const uint32_t vs[4], uint32_t texture, uint32_t blend);  // 0x1800740c
void text_draw_at(uint32_t handle, FontRecord& font_at, uint32_t text, int32_t x, int32_t y,
                  Align align, uint32_t blend);  // 0x18007934
void image_draw_clipped_at(int32_t x, int32_t y, int32_t width, int32_t height, int32_t u,
                           int32_t v, ImageRecord& image, uint32_t blend);  // 0x18014f4c
void glyph_draw_at(uint32_t handle, FontRecord& font_at, uint32_t text, int32_t x,
                   int32_t y);  // 0x18007be8

}  // namespace minigolf::game
