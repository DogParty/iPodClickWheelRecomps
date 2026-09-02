// The vertex batcher every 2-D draw goes through (the renderer object at 0x1801a5a4).
//
// A draw does not touch OpenGL: it appends a quad to a buffer chosen by blend mode and texture,
// and the frame's end (batcher_flush, 0x18008de4) draws the buffers in blend order — six blend
// modes, eleven texture slots each, plus the flat-colour quads — as triangle fans, then hands
// every buffer back. Twelve buffers of 150 quads are shared from a free list; a (blend,
// texture) pair that fills one takes another and chains it behind.
//
// Vertices are 16.16: positions (x, y, 0, 1) with y measured up from the bottom of the 240-pixel
// screen, colours RGBA, texture coordinates in texels.
#include "async_request.h"
#include "calling.h"
#include "draw.h"
#include "fixed.h"
#include "framework/graphics.h"
#include "game_state.h"
#include "gl.h"
#include "gl_state.h"
#include "init.h"
#include "libc.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t RENDERER = 0x1801'a5a4;  // the batcher's globals (BatcherGlobals)
constexpr uint32_t SCREEN_SETTING_SECOND = 10;

constexpr uint32_t BATCHER_SIZE = 0x466b4, BUFFER_SIZE = 0x5dcc, BUFFER_COUNT = 12;
constexpr uint32_t BLEND_COUNT = 6, TEXTURE_SLOTS = 11;
constexpr uint32_t QUADS_PER_BUFFER = 0x96, VERTICES_PER_BUFFER = 0x258;
constexpr uint32_t SCREEN_BOTTOM = to_fixed(240);
constexpr uint32_t NO_TEXTURE = 0xffff'ffffu;

// The batcher object.
// The batcher's three globals (0x1801a5a4): the batcher itself, made on first use; the half
// width a line grows by (16.16); the quad count above which a texture is urgent.
struct [[gnu::packed]] BatcherGlobals {
    uint32_t renderer;
    uint32_t line_half_width;
    uint32_t texture_priority_limit;
};
BatcherGlobals& globals() {
    return guest<BatcherGlobals>(RENDERER);
}

// A vertex buffer: 150 quads as triangle fans, with the texture they share.
struct [[gnu::packed]] VertexBuffer {
    uint32_t texture;                             // GL texture name, or NO_TEXTURE
    uint32_t positions[VERTICES_PER_BUFFER * 4];  // x, y, z, w in 16.16
    uint32_t texcoords[VERTICES_PER_BUFFER * 2];  // u, v
    uint32_t colors[VERTICES_PER_BUFFER * 4];     // r, g, b, a
    uint32_t count;                               // quads used
    uint32_t next;  // -> the older buffer of the same pair, or the next free
};
static_assert(offsetof(VertexBuffer, texcoords) == 0x2584);
static_assert(offsetof(VertexBuffer, colors) == 0x3844);
static_assert(offsetof(VertexBuffer, count) == 0x5dc4);
static_assert(sizeof(VertexBuffer) == BUFFER_SIZE);

// The batcher: the free list, the newest buffer of every (blend, texture) pair and of every
// flat-colour blend, and the twelve buffers. It lives on the guest heap because the GL
// framework reads the vertex arrays from there.
struct [[gnu::packed]] Batcher {
    uint32_t free;
    uint32_t textured[BLEND_COUNT][TEXTURE_SLOTS];
    uint32_t untextured[BLEND_COUNT];
    VertexBuffer buffers[BUFFER_COUNT];
};
static_assert(offsetof(Batcher, untextured) == 0x10c);
static_assert(offsetof(Batcher, buffers) == 0x124);
static_assert(sizeof(Batcher) == BATCHER_SIZE);
Batcher& batcher(uint32_t at) {
    return guest<Batcher>(at);
}
VertexBuffer& buffer(uint32_t at) {
    return guest<VertexBuffer>(at);
}

// A buffer.

// The two slot tables are reached by address rather than through the overlay: a reference to a
// field of a packed structure is not something every compiler will give (aarch64-none-elf-g++
// refuses), and the address is what the original works with anyway.
uint32_t& textured_slot(uint32_t batcher_at, uint32_t blend, uint32_t texture) {
    return guest<uint32_t>(batcher_at + static_cast<uint32_t>(offsetof(Batcher, textured)) +
                           (blend * TEXTURE_SLOTS + texture) * 4);
}
uint32_t& untextured_slot(uint32_t batcher_at, uint32_t blend) {
    return guest<uint32_t>(batcher_at + static_cast<uint32_t>(offsetof(Batcher, untextured)) +
                           blend * 4);
}

// Pop the free list: the buffer, or 0 when all twelve are in use.
uint32_t free_buffer_pop(uint32_t batcher_at) {
    const uint32_t taken = batcher(batcher_at).free;
    if (taken != 0) {
        batcher(batcher_at).free = buffer(taken).next;
        buffer(taken).next = 0;
    }
    return taken;
}

// The vertex for quad `index` of a buffer: where its attributes go, as guest addresses. Pointers
// into the buffer would be the obvious thing, but the buffer is a packed overlay and taking the
// address of a field of one is not portable; an address is also what the original passes to GL.
struct VertexSlot {
    uint32_t position, color, texcoord;
};
VertexSlot vertex(uint32_t buffer_at, uint32_t quad, uint32_t corner) {
    const uint32_t v = quad * 4 + corner;
    return {buffer_at + static_cast<uint32_t>(offsetof(VertexBuffer, positions)) + v * 4 * 4,
            buffer_at + static_cast<uint32_t>(offsetof(VertexBuffer, colors)) + v * 4 * 4,
            buffer_at + static_cast<uint32_t>(offsetof(VertexBuffer, texcoords)) + v * 2 * 4};
}

void vertex_place(const VertexSlot& slot, uint32_t x, uint32_t y_from_top) {
    guest<uint32_t>(slot.position) = x;
    guest<uint32_t>(slot.position + 4) = SCREEN_BOTTOM - y_from_top;
}
void vertex_color(const VertexSlot& slot, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    guest<uint32_t>(slot.color) = r;
    guest<uint32_t>(slot.color + 4) = g;
    guest<uint32_t>(slot.color + 8) = b;
    guest<uint32_t>(slot.color + 12) = a;
}

// Take the next quad of a buffer, or QUADS_PER_BUFFER when it is full.
uint32_t quad_claim(uint32_t buffer_at) {
    const uint32_t count = buffer(buffer_at).count;
    if (static_cast<int32_t>(count) >= static_cast<int32_t>(QUADS_PER_BUFFER)) {
        return QUADS_PER_BUFFER;
    }
    buffer(buffer_at).count = count + 1;
    return count;
}

}  // namespace

// 0x18016654 — a buffer as built: every vertex at z = 0, w = 1.
void buffer_construct(uint32_t buffer_at) {
    for (uint32_t v = 0; v < VERTICES_PER_BUFFER; ++v) {
        buffer(buffer_at).positions[v * 4 + 2] = 0;
        buffer(buffer_at).positions[v * 4 + 3] = 0x10000;
    }
}

// 0x180166dc — empty the batcher: every buffer free and untextured, every slot empty.
void batcher_reset(uint32_t batcher_at) {
    Batcher& b = batcher(batcher_at);
    const uint32_t buffers_at = batcher_at + static_cast<uint32_t>(offsetof(Batcher, buffers));
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) {
        b.buffers[i].texture = NO_TEXTURE;
        b.buffers[i].count = 0;
        b.buffers[i].next = i + 1 < BUFFER_COUNT ? buffers_at + (i + 1) * BUFFER_SIZE : 0;
    }
    b.free = buffers_at;
    for (uint32_t blend = 0; blend < BLEND_COUNT; ++blend) {
        for (uint32_t texture = 0; texture < TEXTURE_SLOTS; ++texture) {
            b.textured[blend][texture] = 0;
        }
        b.untextured[blend] = 0;
    }
}

// 0x18016684 — the batcher, made on first use: allocated, its buffers constructed, then reset.
uint32_t batcher_get() {
    if (globals().renderer == 0) {
        const uint32_t batcher_at = operator_new(BATCHER_SIZE);
        array_construct(batcher_at + static_cast<uint32_t>(offsetof(Batcher, buffers)), 0,
                        BUFFER_SIZE, 0, BUFFER_COUNT, 0, buffer_construct, 0);
        batcher_reset(batcher_at);
        globals().renderer = batcher_at;
    }
    return globals().renderer;
}

// 0x18016a18 — a fresh flat-colour buffer for a blend mode, chained in front of its current one.
uint32_t untextured_buffer_take(uint32_t batcher_at, uint32_t blend) {
    const uint32_t taken = free_buffer_pop(batcher_at);
    if (taken != 0) {
        buffer(taken).texture = NO_TEXTURE;
        buffer(taken).next = untextured_slot(batcher_at, blend);
        untextured_slot(batcher_at, blend) = taken;
    }
    return taken;
}

// 0x18016a60 — the same for a (blend, texture) pair; the buffer remembers its texture.
uint32_t textured_buffer_take(uint32_t batcher_at, uint32_t blend, uint32_t texture) {
    const uint32_t taken = free_buffer_pop(batcher_at);
    if (taken != 0) {
        buffer(taken).texture = texture;
        buffer(taken).next = textured_slot(batcher_at, blend, texture);
        textured_slot(batcher_at, blend, texture) = taken;
    }
    return taken;
}

// 0x180164c8 — a flat-colour rectangle (corners in 16.16 from the top-left) into a buffer.
// Returns 0 when the buffer is full.
uint32_t rect_add(uint32_t buffer_at, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                  uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    const uint32_t quad = quad_claim(buffer_at);
    if (quad == QUADS_PER_BUFFER) {
        return 0;
    }
    const uint32_t xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    for (uint32_t corner = 0; corner < 4; ++corner) {
        const VertexSlot slot = vertex(buffer_at, quad, corner);
        vertex_place(slot, xs[corner], ys[corner]);
        vertex_color(slot, r, g, b, a);
    }
    return 1;
}

// 0x1801631c — a line as a rectangle grown by LINE_HALF_WIDTH on every side.
uint32_t line_add(uint32_t buffer_at, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                  uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    const uint32_t quad = quad_claim(buffer_at);
    if (quad == QUADS_PER_BUFFER) {
        return 0;
    }
    const uint32_t half = globals().line_half_width;
    const uint32_t xs[4] = {x0 - half, x1 + half, x1 + half, x0 - half};
    const uint32_t ys[4] = {y0 + half, y0 + half, y1 - half, y1 - half};
    for (uint32_t corner = 0; corner < 4; ++corner) {
        const VertexSlot slot = vertex(buffer_at, quad, corner);
        vertex_place(slot, xs[corner], ys[corner]);
        vertex_color(slot, r, g, b, a);
    }
    return 1;
}

// 0x18015ea4 — a textured quad, white. The texture coordinates come as four (u, v) pairs in
// the original's argument order, which is the corners' order reversed: the last pair lands on
// the first corner. (A sprite passes a rectangle's corners; the between-hole wipe passes a
// rotated quad's, so the pairs are kept apart.)
uint32_t quad_add(uint32_t buffer_at, const uint32_t xs[4], const uint32_t ys[4],
                  const uint32_t us[4], const uint32_t vs[4]) {
    const uint32_t quad = quad_claim(buffer_at);
    if (quad == QUADS_PER_BUFFER) {
        return 0;
    }
    for (uint32_t corner = 0; corner < 4; ++corner) {
        const VertexSlot slot = vertex(buffer_at, quad, corner);
        vertex_place(slot, xs[corner], ys[corner]);
        vertex_color(slot, 0x10000, 0x10000, 0x10000, 0x10000);
        guest<uint32_t>(slot.texcoord) = us[3 - corner];
        guest<uint32_t>(slot.texcoord + 4) = vs[3 - corner];
    }
    return 1;
}

// 0x18016984 — a rectangle for a blend mode: into its current buffer, or a fresh one when that
// is full or there is none. Returns 0 only when the buffers have run out.
uint32_t rect_push(uint32_t batcher_at, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                   uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t blend) {
    uint32_t at = untextured_slot(batcher_at, blend);
    if (at == 0 && (at = untextured_buffer_take(batcher_at, blend)) == 0) {
        return 0;
    }
    if (rect_add(at, x0, y0, x1, y1, r, g, b, a) != 0) {
        return 1;
    }
    if ((at = untextured_buffer_take(batcher_at, blend)) == 0) {
        return 0;
    }
    return rect_add(at, x0, y0, x1, y1, r, g, b, a);
}

// 0x18016894 — a line, the same way.
uint32_t line_push(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t r, uint32_t g,
                   uint32_t b, uint32_t a, uint32_t blend) {
    const uint32_t batcher_at = batcher_get();
    uint32_t at = untextured_slot(batcher_at, blend);
    if (at == 0 && (at = untextured_buffer_take(batcher_at, blend)) == 0) {
        return 0;
    }
    if (line_add(at, x0, y0, x1, y1, r, g, b, a) != 0) {
        return 1;
    }
    if ((at = untextured_buffer_take(batcher_at, blend)) == 0) {
        return 0;
    }
    return line_add(at, x0, y0, x1, y1, r, g, b, a);
}

// 0x18016788 — a textured quad, the same way, into the (blend, texture) pair's buffers.
uint32_t quad_push(uint32_t batcher_at, const uint32_t xs[4], const uint32_t ys[4],
                   const uint32_t us[4], const uint32_t vs[4], uint32_t texture, uint32_t blend) {
    uint32_t at = textured_slot(batcher_at, blend, texture);
    if (at == 0 && (at = textured_buffer_take(batcher_at, blend, texture)) == 0) {
        return 0;
    }
    if (quad_add(at, xs, ys, us, vs) != 0) {
        return 1;
    }
    if ((at = textured_buffer_take(batcher_at, blend, texture)) == 0) {
        return 0;
    }
    return quad_add(at, xs, ys, us, vs);
}

// 0x180160dc — draw one buffer as triangle fans. Returns 1 if it had anything to draw.
uint32_t buffer_flush(uint32_t buffer_at) {
    const uint32_t count = buffer(buffer_at).count;
    if (static_cast<int32_t>(count) <= 0) {
        return 0;
    }
    const uint32_t texture = buffer(buffer_at).texture;
    const auto attributes = [&](uint32_t second_size, uint32_t second_at) {
        gfx::set_vertex_array(0, 4, gfx::AttributeType::Fixed, 0, 0,
                              buffer_at + static_cast<uint32_t>(offsetof(VertexBuffer, positions)));
        gfx::error();
        gfx::enable_vertex_array(0);
        gfx::error();
        gfx::set_vertex_array(1, second_size, gfx::AttributeType::Fixed, 0, 0, second_at);
        gfx::error();
        gfx::enable_vertex_array(1);
        gfx::error();
        gfx::draw_arrays(gfx::Primitive::Quads, 0, count * 4);
        gfx::error();
        gfx::disable_vertex_array(0);
        gfx::error();
        gfx::disable_vertex_array(1);
    };
    if (texture == NO_TEXTURE) {
        gl_mode(gfx::Pipeline::Flat);
        camera_matrix_load();
        attributes(4, buffer_at + static_cast<uint32_t>(offsetof(VertexBuffer, colors)));
        gfx::error();
        return 1;
    }
    // Texture 1 is the screen-sized one; it is drawn the other way on the second screen.
    gfx::Pipeline pipeline = gfx::Pipeline::Textured;
    if (texture == 1 && static_cast<uint32_t>(settings_language()) != SCREEN_SETTING_SECOND) {
        pipeline = gfx::Pipeline::TexturedSecond;
    }
    gl_mode(pipeline);
    camera_matrix_load();
    gfx::bind_texture(gfx::TextureTarget::Texture2D, texture);
    gfx::error();
    const uint32_t limit = globals().texture_priority_limit;
    const uint32_t priority =
        limit != 0 && static_cast<int32_t>(count) <= static_cast<int32_t>(limit) ? 0 : FLOAT_ONE;
    (void)gfx::set_texture_parameter(gfx::TextureTarget::Texture2D, gfx::TextureParameter::Priority,
                                     priority);
    gfx::error();
    attributes(2, buffer_at + static_cast<uint32_t>(offsetof(VertexBuffer, texcoords)));
    gfx::error();
    gfx::bind_texture(gfx::TextureTarget::Texture2D, 0);
    gfx::error();
    return 1;
}

// 0x18008de4 — the frame's drawing: every buffer in blend order. Within a blend mode the
// texture slots are walked from the one that last drew something, and its chain is drawn
// newest first. Everything is then returned to the free list.
void batcher_flush() {
    uint32_t start = 0;
    for (uint32_t blend = 0; blend < BLEND_COUNT; ++blend) {
        uint32_t slot = start;
        for (uint32_t i = 0; i < TEXTURE_SLOTS; ++i) {
            for (uint32_t at = textured_slot(batcher_get(), blend, slot); at != 0;
                 at = buffer(at).next) {
                if (buffer_flush(at) != 0) {
                    start = slot;
                }
            }
            slot = slot + 1 == TEXTURE_SLOTS ? 0 : slot + 1;
        }
        for (uint32_t at = untextured_slot(batcher_get(), blend); at != 0; at = buffer(at).next) {
            buffer_flush(at);
        }
    }
    batcher_reset(batcher_get());
}

// --- The draws the screens call ---------------------------------------------------------------

// 0x180071e4 — fill a rectangle: x, y, width, height in pixels, colour in 16.16.
void rect_fill_at(uint32_t handle, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t blend) {
    if (handle == 0 || static_cast<int32_t>(width) <= 0 || static_cast<int32_t>(height) <= 0) {
        assert_trap(0x18007200u);
    }
    for (const uint32_t channel : {r, g, b, a}) {
        if (static_cast<int32_t>(channel) < 0 || channel > 0x10000) {
            assert_trap(0x18007224u);
        }
    }
    if (blend >= BLEND_COUNT) {
        assert_trap(0x18007284u);
    }
    rect_push(batcher_get(), x, y, x + width, y + height, r, g, b, a, blend);
}

// 0x18007314 — a line between two points.
void line_draw_at(uint32_t handle, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t r,
                  uint32_t g, uint32_t b, uint32_t a, uint32_t blend) {
    if (handle == 0) {
        assert_trap(0x18007330u);
    }
    for (const uint32_t channel : {r, g, b, a}) {
        if (static_cast<int32_t>(channel) < 0 || channel > 0x10000) {
            assert_trap(0x1800733cu);
        }
    }
    if (blend >= BLEND_COUNT) {
        assert_trap(0x1800739cu);
    }
    batcher_get();
    line_push(x0, y0, x1, y1, r, g, b, a, blend);
}

// 0x1800740c — a textured quad: four corners in 16.16 and the texel rectangle they show.
void textured_quad_push(const uint32_t xs[4], const uint32_t ys[4], const uint32_t us[4],
                        const uint32_t vs[4], uint32_t texture, uint32_t blend) {
    if (blend >= BLEND_COUNT) {
        assert_trap(0x18007428u);
    }
    quad_push(batcher_get(), xs, ys, us, vs, texture, blend);
}

// 0x18008e9c — `width`×`height` pixels of an image, from `u`,`v` texels into it, at x, y. The
// image's third variant is drawn half-transparent. Texture coordinates count from the bottom of
// the texture, so the image's v is turned over by the texture's height.
void image_draw_at(uint32_t handle, int32_t x, int32_t y, int32_t width, int32_t height,
                   ImageRecord& image, int32_t u, int32_t v, uint32_t variant, uint32_t blend) {
    if (handle == 0 || variant >= 3) {
        assert_trap(0x18008eccu);
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    ImageRecord& texture = as_image(TITLE_IMAGE + image.texture_index * TEXTURE_TABLE_STRIDE);
    const int32_t texture_height = static_cast<int32_t>(texture.height);
    const int32_t left = static_cast<int32_t>(image.u) + u;
    const int32_t bottom = texture_height - static_cast<int32_t>(image.v) - height - v;
    const int32_t top = bottom + height;
    const uint32_t xs[4] = {to_fixed(x), to_fixed(x + width), to_fixed(x + width), to_fixed(x)};
    const uint32_t ys[4] = {to_fixed(y), to_fixed(y), to_fixed(y + height), to_fixed(y + height)};
    // The alpha (variant 2: half) rides along in the original's argument list but the quad
    // is stored white; it is kept here only as the call's shape.
    const int32_t right = left + width;
    const uint32_t us[4] = {to_fixed(left), to_fixed(right), to_fixed(right), to_fixed(left)};
    const uint32_t vs[4] = {to_fixed(bottom), to_fixed(bottom), to_fixed(top), to_fixed(top)};
    textured_quad_push(xs, ys, us, vs, texture.texture_name, blend);
}

// 0x18014f4c — an image clipped to the screen: the part off the edges is trimmed from both the
// rectangle and the texels, so a sprite can slide off without the batcher seeing it.
void image_draw_clipped_at(int32_t x, int32_t y, int32_t width, int32_t height, int32_t u,
                           int32_t v, ImageRecord& image, uint32_t blend) {
    if (x + width < 0 || y + height < 0 || x >= static_cast<int32_t>(SCREEN_WIDTH) ||
        y >= static_cast<int32_t>(SCREEN_HEIGHT)) {
        return;
    }
    const uint32_t handle = game_state_block().handle;
    const uint32_t variant = static_cast<uint32_t>(image.variant);
    if (x >= 0 && y >= 0 && x + width <= static_cast<int32_t>(SCREEN_WIDTH) &&
        y + height <= static_cast<int32_t>(SCREEN_HEIGHT)) {
        image_draw_at(handle, x, y, width, height, image, u, v, variant, blend);
        return;
    }
    const int32_t left = x < 0 ? -x : 0, top = y < 0 ? -y : 0;
    const int32_t right = x + width > static_cast<int32_t>(SCREEN_WIDTH)
                              ? x + width - static_cast<int32_t>(SCREEN_WIDTH)
                              : 0;
    const int32_t bottom = y + height > static_cast<int32_t>(SCREEN_HEIGHT)
                               ? y + height - static_cast<int32_t>(SCREEN_HEIGHT)
                               : 0;
    image_draw_at(handle, x + left, y + top, width - left - right, height - bottom - top, image,
                  u + left, v + top, variant, blend);
}

}  // namespace minigolf::game
