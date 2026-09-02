// The matrix state the game keeps beside OpenGL: a projection and a modelview matrix (4×4
// words), whichever is current, and a flag that says it is still the identity.
#include "gl_state.h"

#include "calling.h"
#include "framework/graphics.h"
#include "game_state.h"
#include "gl.h"
#include "libc.h"
#include "records.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"

namespace minigolf::game {

namespace {

constexpr uint32_t RENDERER = 0x1803'b38c, RENDERER_SIZE = 0x3c;
constexpr uint32_t FLOAT_100 = 0x42c8'0000, FLOAT_MINUS_100 = 0xc2c8'0000, FLOAT_240 = 0x4370'0000,
                   FLOAT_320 = 0x43a0'0000;
// The draw items (0x1803b3c8) and the six doubly-linked lists (0x1803fba8) they are queued on.
constexpr uint32_t DRAW_ITEMS = 0x1803'b3c8, DRAW_ITEMS_SIZE = 0x47e0,
                   DRAW_ITEM_COUNT = 0x1801'a5b0;
constexpr uint32_t DRAW_LISTS = 0x1803'fba8, DRAW_LIST_COUNT = 6, DRAW_LIST_SIZE = 16;

constexpr uint32_t CURRENT_MATRIX = 0x1801'a514;  // -> the matrix in use
constexpr uint32_t PROJECTION = 0x1801'a518, MODELVIEW = 0x1801'a55c;
constexpr uint32_t GL_MODELVIEW = 0x1700, GL_PROJECTION = 0x1701;

}  // namespace

// 0x180113c8 — choose the current matrix by GL matrix mode.
uint32_t& current_matrix() {
    return guest<uint32_t>(CURRENT_MATRIX);
}
void matrix_mode_set(uint32_t mode) {
    if (mode == GL_MODELVIEW) {
        current_matrix() = MODELVIEW;
    } else if (mode == GL_PROJECTION) {
        current_matrix() = PROJECTION;
    }
}

// 0x18012bd0 — the current matrix back to the identity, unless it already is.
void matrix_load_identity() {
    const uint32_t matrix = current_matrix();
    if (as_matrix(matrix).is_identity != 0) {
        return;
    }
    gfx::matrix_identity(matrix);
    as_matrix(current_matrix()).is_identity = 0;
}

// 0x1801218c — multiply the current matrix by another (straight in when it is the identity,
// through a copy otherwise).
void matrix_multiply(uint32_t by) {
    GuestScratch frame(4 * 3 + 0x84);
    const uint32_t product = frame.at(0x44), copy = frame.at(4);
    gfx::matrix_identity(product);
    const uint32_t matrix = current_matrix();
    if (as_matrix(matrix).is_identity != 0) {
        gfx::matrix_multiply(matrix, by, product);
    } else {
        gfx::matrix_multiply(copy, matrix, by);
        gfx::matrix_multiply(current_matrix(), copy, product);
    }
    as_matrix(current_matrix()).is_identity = 0;
}

// 0x1800eaa0 — the projection times the modelview, handed to GL.
void camera_matrix_load() {
    GuestScratch frame(4 * 1 + 0x44);
    const uint32_t product = frame.at(4);
    gfx::matrix_multiply(product, PROJECTION, MODELVIEW);
    gfx::set_matrix_uniform(0, 1, 0, product);
}

// 0x1800dd80 — an orthographic projection multiplied into the current matrix.
void matrix_ortho(uint32_t left, uint32_t right, uint32_t bottom, uint32_t top, uint32_t near,
                  uint32_t far) {
    GuestScratch frame(4 * 1 + 0x4c);
    const uint32_t matrix = frame.at(0xc);
    gfx::matrix_ortho(matrix, left, right, bottom, top, near, far);
    matrix_multiply(matrix);
}

// 0x18008c78 — the renderer object (0x1803b38c): a 320×240 viewport with both of its
// rectangles set, a white clear colour, an orthographic projection of the screen with depth
// -100..100, a fresh modelview, and the six empty draw lists. Returns the renderer.
uint32_t renderer_create() {
    GuestScratch frame(4 * 4);
    libc::memory_clear(RENDERER, RENDERER_SIZE);
    as_rect_object(RENDERER).width = SCREEN_WIDTH;
    as_rect_object(RENDERER).height = SCREEN_HEIGHT;
    object_rect_a_set(as_rect_object(RENDERER), 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    object_rect_b_set(as_rect_object(RENDERER), 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gfx::set_clear_color(FLOAT_ONE, 0, FLOAT_ONE, FLOAT_ONE);
    gfx::error();
    matrix_mode_set(GL_PROJECTION);
    gfx::error();
    matrix_load_identity();
    gfx::error();
    matrix_ortho(0, FLOAT_320, 0, FLOAT_240, FLOAT_MINUS_100, FLOAT_100);
    gfx::error();
    matrix_mode_set(GL_MODELVIEW);
    gfx::error();
    matrix_load_identity();
    gfx::error();
    libc::memory_clear(DRAW_ITEMS, DRAW_ITEMS_SIZE);
    guest<uint32_t>(DRAW_ITEM_COUNT) = 0;
    for (uint32_t i = 0; i < DRAW_LIST_COUNT; ++i) {
        const uint32_t at = DRAW_LISTS + i * DRAW_LIST_SIZE;
        DrawList& list = guest<DrawList>(at);
        list.next = at;
        list.previous = at;
        list.renderer = RENDERER;
        list.index = i;
    }
    return RENDERER;
}

}  // namespace minigolf::game
