// Text as the fonts draw it: each glyph is a cell of the font's image, advanced by a per-glyph
// width. A font object (loaded by the resource code) carries its tables; strings are bytes,
// or halfwords when the font reaches past code 0xff.
#include "text.h"

#include "calling.h"
#include "draw.h"
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

constexpr uint32_t FIRST_CODE = 0x20, CELLS_PER_ROW = 64;

bool wide(FontRecord& font_at) {
    return static_cast<int32_t>(font_at.last_code) > 0xff;
}
// One of a font's per-code tables (widths or advances), which start at code 0x20.
int32_t table(uint32_t table_at, int32_t code) {
    return static_cast<int32_t>(
        guest_array<int8_t>(table_at)[code - static_cast<int32_t>(FIRST_CODE)]);
}
int32_t code_at(FontRecord& font_at, uint32_t text) {
    return wide(font_at) ? guest<int16_t>(text) : guest<int8_t>(text);
}
uint32_t step(FontRecord& font_at) {
    return wide(font_at) ? 2 : 1;
}

// One glyph's cell drawn at the pen; returns how far the pen moves. Wide fonts lay their cells
// out 64 to a row, byte fonts in one row.
int32_t glyph_put(uint32_t handle, FontRecord& font_at, int32_t code, int32_t x, int32_t y,
                  uint32_t blend) {
    if (code == ' ') {
        return table(font_at.advances, ' ');
    }
    const int32_t cell = code - 0x21;
    const int32_t cell_width = static_cast<int32_t>(font_at.cell_width);
    const int32_t cell_height = static_cast<int32_t>(font_at.cell_height);
    // Halfword fonts lay their cells out 64 to a row, and draw them one pixel short.
    int32_t u = cell * cell_width, v = 0, height = cell_height;
    if (wide(font_at)) {
        const int32_t row = cell / static_cast<int32_t>(CELLS_PER_ROW);
        u = (cell - row * static_cast<int32_t>(CELLS_PER_ROW)) * cell_width;
        v = row * cell_height;
        height = cell_height - 1;
    }
    image_draw_at(handle, x, y, table(font_at.widths, code), height, as_image(address_of(font_at)),
                  u, v, static_cast<uint32_t>(font_at.sheet.variant), blend);
    return table(font_at.advances, code);
}

}  // namespace

// 0x18007800 — where a line starts so that it is centred on x.
int32_t line_centre_x(FontRecord& font_at, uint32_t text, int32_t x) {
    const int32_t width = static_cast<int32_t>(text_width(font_at, text));
    return x - ((width + (width < 0 ? 1 : 0)) >> 1);
}

// 0x18007934 / 0x18007d8c — draw text at x, y with an alignment; newlines start a new line,
// re-aligned. The first draws with the text blend, the second with the blend it is given.
void text_draw_at(uint32_t handle, FontRecord& font_at, uint32_t text, int32_t x, int32_t y,
                  Align align, uint32_t blend) {
    int32_t pen = x;
    if (align != Align::Left) {
        const int32_t width = static_cast<int32_t>(text_width(font_at, text));
        pen = x - (align == Align::Centre ? (width + (width < 0 ? 1 : 0)) >> 1 : width);
    }
    for (int32_t code = code_at(font_at, text); code != 0; code = code_at(font_at, text)) {
        text += step(font_at);
        if (code == '\n') {
            if (code_at(font_at, text) != 0) {
                if (align == Align::Centre) {
                    pen = line_centre_x(font_at, text, x);
                } else if (align == Align::Right) {
                    pen = x - static_cast<int32_t>(text_width(font_at, text));
                }
            }
            if (align == Align::Left) {
                pen = x;
            }
            y += static_cast<int32_t>(font_at.line_height);
            continue;
        }
        pen += glyph_put(handle, font_at, code, pen, y, blend);
    }
}

// 0x18007be8 — draw text with no alignment and no newlines: the name entry's wheel glyphs.
void glyph_draw_at(uint32_t handle, FontRecord& font_at, uint32_t text, int32_t x, int32_t y) {
    for (int32_t code = code_at(font_at, text); code != 0; code = code_at(font_at, text)) {
        text += step(font_at);
        x += glyph_put(handle, font_at, code, x, y, static_cast<uint32_t>(Blend::KeyedAlt));
    }
}

// 0x18008040 — break text into lines no wider than `width` pixels: `out` receives the index
// each line starts at, then one past the end. Returns the number of lines, or 0 if they do not
// fit in `max_lines`. Byte text breaks after a space or punctuation; halfword text only after a
// space, and starts the next line at its first non-space — or at the overflowing character
// when the line holds no space at all.
uint32_t text_layout(FontRecord& font_at, uint32_t text, uint32_t out, uint32_t max_lines,
                     int32_t width) {
    libc::memory_clear(out, max_lines * 4);
    const bool halfwords = wide(font_at);
    const int32_t length =
        static_cast<int32_t>(halfwords ? wide_string_length(text) : string_length(text));
    const auto at = [&](int32_t i) {
        return halfwords ? guest<int16_t>(text + static_cast<uint32_t>(i) * 2)
                         : guest<int8_t>(text + static_cast<uint32_t>(i));
    };
    const auto breaks_after = [&](int32_t c) {
        if (halfwords) {
            return c == ' ';
        }
        return c == ' ' || c == '-' || c == ',' || c == '.' || c == ';' || c == ':' || c == '\n';
    };
    const auto line_out = [&](uint32_t line, int32_t start) {
        guest_array<uint32_t>(out)[line] = static_cast<uint32_t>(start);
    };

    uint32_t lines = 0;
    int32_t line_start = 0, run = 0;
    for (int32_t i = 0; i < length; ++i) {
        const int32_t c = at(i);
        if (c == '\n') {
            line_out(lines++, line_start);
            if (lines == max_lines) {
                return 0;
            }
            line_start = i + 1;
            run = 0;
            continue;
        }
        run += table(font_at.advances, c);
        if (run <= width) {
            continue;
        }
        line_out(lines++, line_start);
        if (lines == max_lines) {
            return 0;
        }
        int32_t j = i - 1;
        while (!breaks_after(at(j))) {
            --j;
        }
        if (halfwords && static_cast<int32_t>(guest_array<uint32_t>(out)[lines - 1]) > j) {
            j = i;  // no space on this line: break at the overflow
        } else {
            ++j;
        }
        while (at(j) == ' ' && j < length) {
            ++j;
        }
        line_start = j;
        i = j;  // the loop's increment skips the line's first character (as the original)
        run = 0;
    }
    line_out(lines, line_start);
    if (lines + 1 == max_lines) {
        return 0;
    }
    line_out(lines + 1, length + 1);
    return lines + 1;
}

}  // namespace minigolf::game
