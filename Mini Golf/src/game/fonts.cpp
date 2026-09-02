// The fonts and the small images loaded once the main pack is in: each font is an object
// with its glyph widths and advances read from the pack, its cell size set by hand, and its
// glyph sheet's texture; then the sine and reciprocal tables and the arrows, corners, battery
// and wheel images.
#include "fonts.h"

#include "calling.h"
#include "draw.h"
#include "game_state.h"
#include "libc.h"
#include "records.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

namespace {

// A font object: an image record (+0..+0x3b) whose width table and advance table follow.
namespace font {
constexpr uint32_t OBJECT_SIZE = 0x5c;
}  // namespace font
constexpr uint32_t IMAGE_HEADERS = 0x1803'3ff4,
                   IMAGE_HEADER_SIZE = 0x24;  // built-in headers by texture
constexpr uint32_t GLYPH_SHEET_WIDTH = GAME_STATE + 0x84a60,
                   GLYPH_SHEET_HEIGHT = GAME_STATE + 0x84a64;
constexpr uint32_t WIDE_GLYPH_CODES = GAME_STATE + 0x8f81e, WIDE_GLYPH_CODES_SIZE = 0x2d6;
constexpr uint32_t LOGO_IMAGE = GAME_STATE + 0x84a88;
constexpr uint32_t RECIPROCALS = GAME_STATE + 0x8551c;
// Text resources (jdmg): the glyph code lists, width and advance tables, and the tables.
constexpr uint32_t TEXT_CODES_BYTE = 0x20, TEXT_CODES_WIDE = 0x21, TEXT_WIDTHS_LARGE = 0x24,
                   TEXT_ADVANCES_LARGE = 0x25, TEXT_WIDTHS_SMALL = 3, TEXT_ADVANCES_SMALL = 4,
                   TEXT_SINE = 0xb, TEXT_RECIPROCALS = 0xc, IMAGE_LOGO = 6, IMAGE_SMALL_SHEET = 2;
constexpr uint32_t GLYPHS_WIDE = 0x16b, GLYPHS_BYTE = 0x5b, GLYPHS_SMALL = 0x1c;

uint32_t pack() {
    return game_state_block().pack_handle;
}

}  // namespace

// 0x18007744 — a font object for `glyphs` glyphs: the record, then a width and an advance
// table of that many bytes, all from the tracked heap. 0 if any allocation fails.
uint32_t font_create(uint32_t glyphs) {
    if (glyphs == 0) {
        assert_trap(0x18007750u);
    }
    const uint32_t object = tracked_allocate(font::OBJECT_SIZE);
    libc::memory_clear(object, font::OBJECT_SIZE);
    FontRecord& at = as_font(object);
    at.widths = static_cast<uint32_t>(tracked_allocate(glyphs));
    at.widths_size = at.widths != 0 ? glyphs : 0;
    if (at.widths == 0) {
        assert_trap(0x18007794u);
    }
    at.advances = static_cast<uint32_t>(tracked_allocate(glyphs));
    at.advances_size = at.advances != 0 ? glyphs : 0;
    if (at.advances == 0) {
        assert_trap(0x180077c0u);
    }
    at.last_code = glyphs;
    return object;
}

// 0x180077c4 — a font object let go: its texture, its tables, itself.
void font_destroy(uint32_t at) {
    if (at == 0) {
        assert_trap(0x180077d0u);
    }
    FontRecord& font = as_font(at);
    texture_release(font.sheet);
    if (font.advances != 0) {
        tracked_free(font.advances);
    }
    if (font.widths != 0) {
        tracked_free(font.widths);
    }
    tracked_free(at);
}

// 0x1800fe8c — once the main pack is loaded: the large font (a wider one, with its own glyph
// code list, for the UTF-16 language), the logo, the small font, the sine and reciprocal
// tables, and the small images of the menus. Returns 1.
uint32_t fonts_load() {
    if (pack() == 0) {
        assert_trap(0x1800fea0u);
    }
    const bool wide = static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
    const uint32_t large_at = font_create(wide ? GLYPHS_WIDE : GLYPHS_BYTE);
    screen_state().text_layout = large_at;
    if (large_at == 0) {
        assert_trap(wide ? 0x1800fee4u : 0x1800ff38u);
    }
    FontRecord& large = as_font(large_at);
    large.cell_width = wide ? 0x10 : 0xe;
    large.cell_height = 0x11;
    large.line_height = 0xf;
    if (resource_load(as_pack(pack()), wide ? TEXT_CODES_WIDE : TEXT_CODES_BYTE, WIDE_GLYPH_CODES,
                      WIDE_GLYPH_CODES_SIZE) == 0) {
        assert_trap(wide ? 0x1800ff20u : 0x1800ff74u);
    }
    const uint32_t large_texture = wide ? 3 : 1;
    if (resource_load(as_pack(pack()), TEXT_WIDTHS_LARGE, static_cast<uint32_t>(large.widths),
                      large.widths_size) == 0) {
        assert_trap(0x1800ff98u);
    }
    if (resource_load(as_pack(pack()), TEXT_ADVANCES_LARGE, static_cast<uint32_t>(large.advances),
                      large.advances_size) == 0) {
        assert_trap(0x1800ffbcu);
    }
    libc::memory_copy(field_address(large, offsetof(FontRecord, sheet.texture_index)),
                      IMAGE_HEADERS + large_texture * IMAGE_HEADER_SIZE, IMAGE_HEADER_SIZE);
    large.sheet.width = guest<uint32_t>(GLYPH_SHEET_WIDTH);
    large.sheet.height = guest<uint32_t>(GLYPH_SHEET_HEIGHT);
    large.sheet.variant = static_cast<uint8_t>(1);
    large.sheet.texture_index = large.sheet.texture_index + 1;

    if (image_apply(as_image(LOGO_IMAGE), 1, as_pack(pack()), IMAGE_LOGO) == 0) {
        assert_trap(0x18010034u);
    }
    as_image(LOGO_IMAGE).texture_index = as_image(LOGO_IMAGE).texture_index + 2;

    const uint32_t small_at = font_create(GLYPHS_SMALL);
    screen_state().small_font = small_at;
    if (small_at == 0) {
        assert_trap(0x18010058u);
    }
    FontRecord& small = as_font(small_at);
    small.cell_width = 0xa;
    small.cell_height = 0xa;
    small.line_height = 0xa;
    if (resource_load(as_pack(pack()), TEXT_WIDTHS_SMALL, static_cast<uint32_t>(small.widths),
                      small.widths_size) == 0) {
        assert_trap(0x18010094u);
    }
    if (resource_load(as_pack(pack()), TEXT_ADVANCES_SMALL, static_cast<uint32_t>(small.advances),
                      small.advances_size) == 0) {
        assert_trap(0x180100b8u);
    }
    if (image_apply(small.sheet, 1, as_pack(pack()), IMAGE_SMALL_SHEET) == 0) {
        assert_trap(0x180100dcu);
    }
    small.sheet.texture_index = small.sheet.texture_index + 2;

    if (resource_load(as_pack(pack()), TEXT_SINE, SINE_TABLE, 0x200) == 0) {
        assert_trap(0x1801010cu);
    }
    if (resource_load(as_pack(pack()), TEXT_RECIPROCALS, RECIPROCALS, 0x200) == 0) {
        assert_trap(0x1801012cu);
    }
    // The arrows, the panel's rim, the battery and the wheel's four images, two textures in.
    const uint32_t images[8][2] = {{0x84ac4, 0x1d}, {0x84b00, 0x1e}, {0x84b3c, 0x1f},
                                   {0x84bb4, 0x5},  {0x84bf0, 0xd},  {0x84c2c, 0xe},
                                   {0x84c68, 0xf},  {0x84ca4, 0x10}};
    const uint32_t returns[8] = {0x18010148u, 0x1801016cu, 0x18010190u, 0x180101b4u,
                                 0x180101d8u, 0x180101fcu, 0x18010220u, 0x18010244u};
    for (uint32_t i = 0; i < 8; ++i) {
        ImageRecord& image = as_image(GAME_STATE + images[i][0]);
        (void)returns[i];
        image_apply(image, 1, as_pack(pack()), images[i][1]);
        image.texture_index = image.texture_index + 2;
    }
    return 1;
}

}  // namespace minigolf::game
