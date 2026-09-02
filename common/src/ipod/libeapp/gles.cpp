// OpenGLES framework implementation. See gles.h for the design and the source of every rule.
#include "ipod/libeapp/gles.h"

#include "ipod/framework/graphics.h"
#include "ipod/libeapp/call_log.h"
#include "ipod/runtime/memory.h"
#include "ipod/runtime/fatal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace ipod::eapp {

namespace gles {

State& state() {
    static State instance;
    return instance;
}

}  // namespace gles

namespace {

using gles::Matrix;
using gles::state;
using gles::Texture;
using gles::VertexArray;

// The game's own screen. Every coordinate it computes is in these units and stays in them: the
// raster below may be larger, and the multiplication happens in `project` and nowhere else.
constexpr unsigned GAME_WIDTH = gfx::SCREEN_WIDTH;
constexpr unsigned GAME_HEIGHT = gfx::SCREEN_HEIGHT;

constexpr unsigned MIN_RENDER_SCALE = 1;
constexpr unsigned MAX_RENDER_SCALE = 8;

// GL enumerants the game uses.
constexpr uint32_t GL_COLOR_BUFFER_BIT = 0x4000;
constexpr uint32_t GL_TRIANGLE_STRIP = 5;
constexpr uint32_t GL_QUADS = 7;
constexpr uint32_t GL_BYTE = 0x1400;
constexpr uint32_t GL_UNSIGNED_BYTE = 0x1401;
constexpr uint32_t GL_SHORT = 0x1402;
constexpr uint32_t GL_UNSIGNED_SHORT = 0x1403;
constexpr uint32_t GL_UNSIGNED_INT = 0x1405;
constexpr uint32_t GL_FLOAT = 0x1406;
constexpr uint32_t GL_ALPHA = 0x1906;
constexpr uint32_t GL_RGB = 0x1907;
constexpr uint32_t GL_RGBA = 0x1908;
constexpr uint32_t GL_LUMINANCE = 0x1909;
constexpr uint32_t GL_LUMINANCE_ALPHA = 0x190a;
// The OES paletted texture formats. `PALETTE4_*` index four bits per texel and `PALETTE8_*`
// eight; the suffix is the palette's own entry format.
constexpr uint32_t GL_PALETTE4_RGB8 = 0x8b90;
constexpr uint32_t GL_PALETTE4_RGBA8 = 0x8b91;
constexpr uint32_t GL_PALETTE4_R5_G6_B5 = 0x8b92;
constexpr uint32_t GL_PALETTE4_RGBA4 = 0x8b93;
constexpr uint32_t GL_PALETTE4_RGB5_A1 = 0x8b94;
constexpr uint32_t GL_PALETTE8_RGB8 = 0x8b95;
constexpr uint32_t GL_PALETTE8_RGBA8 = 0x8b96;
constexpr uint32_t GL_PALETTE8_R5_G6_B5 = 0x8b97;
constexpr uint32_t GL_PALETTE8_RGBA4 = 0x8b98;
constexpr uint32_t GL_PALETTE8_RGB5_A1 = 0x8b99;
constexpr uint32_t GL_UNSIGNED_SHORT_4_4_4_4 = 0x8033;
constexpr uint32_t GL_UNSIGNED_SHORT_5_5_5_1 = 0x8034;
constexpr uint32_t GL_UNSIGNED_SHORT_5_6_5 = 0x8363;
constexpr uint32_t GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr uint32_t GL_PACK_ALIGNMENT = 0x0D05;
constexpr uint32_t GL_INVALID_ENUM = 0x0500;
constexpr uint32_t GL_INVALID_VALUE = 0x0501;

// Texels (and modulated fragments) with alpha below this are colour-keyed away rather than
// blended — the threshold the emulator settled on against the real art.
constexpr uint8_t COLOUR_KEY_ALPHA = 8;
// How hard a glyph's edge may be taken back towards full contrast, whatever the render scale.
// See `Filter::Glyph` in `sample_texture`: the reconstruction sharpens over one texel, which at
// scale 4 or 8 is a very hard edge indeed — hard enough to erode an ornate face whose alpha ramp
// *is* the design rather than an antialiased hard edge (Cubis 2's `rockart-11` HUD, whose
// `SCORE` came out `SCVRK`). Two levels of sharpening is enough to resolve an edge and gentle
// enough to leave a drawn ramp recognisable.
constexpr float GLYPH_MAX_CONTRAST = 2.0f;

constexpr float MINIFIED_RATIO = 1.5f;  // texels per pixel above which to box-filter the footprint

constexpr unsigned MAX_TEXTURE_SIDE = 2048;
constexpr unsigned MAX_VERTICES = 4096;

// How many raster pixels the game gets for each of its own, in each direction. One at the iPod's
// resolution; see gfx::set_render_scale.
unsigned& render_scale_value() {
    static unsigned scale = 1;
    return scale;
}

// The raster's size, which is the game's screen times that scale.
unsigned raster_width() {
    return GAME_WIDTH * render_scale_value();
}

unsigned raster_height() {
    return GAME_HEIGHT * render_scale_value();
}

// The framebuffer starts magenta so an un-drawn region is unmistakable in a screenshot, and is
// repainted magenta whenever it is resized for the same reason.
std::vector<uint8_t>& framebuffer_storage() {
    static std::vector<uint8_t> pixels = [] {
        std::vector<uint8_t> p(static_cast<size_t>(GAME_WIDTH) * GAME_HEIGHT * 3);
        for (size_t i = 0; i < p.size(); i += 3) {
            p[i] = 255;
            p[i + 1] = 0;
            p[i + 2] = 255;
        }
        return p;
    }();
    return pixels;
}

uint8_t* framebuffer() {
    return framebuffer_storage().data();
}

float float_from_bits(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

uint32_t bits_from_float(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

float guest_float(uint32_t address) {
    return float_from_bits(ld32(address));
}

Matrix read_matrix(uint32_t address) {
    Matrix m;
    for (unsigned i = 0; i < 16; ++i) {
        m[i] = guest_float(address + 4 * i);
    }
    return m;
}

void write_matrix(uint32_t address, const Matrix& m) {
    for (unsigned i = 0; i < 16; ++i) {
        st32(address + 4 * i, bits_from_float(m[i]));
    }
}

Matrix identity() {
    Matrix m{};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

// Column-major product a × b.
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (unsigned column = 0; column < 4; ++column) {
        for (unsigned row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (unsigned k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[column * 4 + k];
            }
            out[column * 4 + row] = sum;
        }
    }
    return out;
}

uint8_t to_byte(float unit) {
    return static_cast<uint8_t>(std::clamp(unit, 0.0f, 1.0f) * 255.0f);
}

// ---------------------------------------------------------------------------------------------
// Texture upload
// ---------------------------------------------------------------------------------------------

bool plausible_size(unsigned width, unsigned height) {
    return width != 0 && height != 0 && width <= MAX_TEXTURE_SIDE && height <= MAX_TEXTURE_SIDE;
}

// IPOD_TEX_LOG=1 prints one line per upload: the texture name, its size, and three sample
// texels. The emulator prints the same three under EAPP_TEX_FMT_LOG=1, so a texture that decodes
// differently on the two sides can be found without dumping either. A wrong decode is invisible
// in the call log and looks like a rendering bug, which is why this exists.
void store_texture(unsigned width, unsigned height, std::vector<uint8_t> rgba,
                   bool alpha_only = false, bool colourless = false) {
    static const bool log_uploads = std::getenv("IPOD_TEX_LOG") != nullptr;
    if (log_uploads && !rgba.empty()) {
        const auto probe = [&](unsigned x, unsigned y) {
            const size_t o =
                (static_cast<size_t>(std::min(y, height - 1)) * width + std::min(x, width - 1)) * 4;
            std::fprintf(stderr, " %02x%02x%02x%02x", rgba[o], rgba[o + 1], rgba[o + 2],
                         rgba[o + 3]);
        };
        std::fprintf(stderr, "tex#%u %ux%u%s", state().bound_texture, width, height,
                     alpha_only ? " alpha-only" : "");
        probe(width / 8, height / 2);
        probe(width / 4, height / 4);
        probe(width / 2, height / 2);
        std::fprintf(stderr, "\n");
    }
    state().textures[state().bound_texture] =
        Texture{width, height, std::move(rgba), alpha_only, colourless};
}

// Expand a 16-bit packed texel to 8-bit channels with rounding, as the emulator does. The same
// three decodes serve both an uncompressed upload and a paletted one's palette entries.
using Texel = std::array<uint8_t, 4>;

Texel rgb565(uint32_t p) {
    const uint32_t r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
    return {static_cast<uint8_t>((r * 255 + 15) / 31), static_cast<uint8_t>((g * 255 + 31) / 63),
            static_cast<uint8_t>((b * 255 + 15) / 31), 0xff};
}

Texel rgba5551(uint32_t p) {
    const uint32_t r = (p >> 11) & 0x1f, g = (p >> 6) & 0x1f, b = (p >> 1) & 0x1f;
    return {static_cast<uint8_t>((r * 255 + 15) / 31), static_cast<uint8_t>((g * 255 + 15) / 31),
            static_cast<uint8_t>((b * 255 + 15) / 31), static_cast<uint8_t>((p & 1) ? 0xff : 0)};
}

Texel rgba4444(uint32_t p) {
    return {static_cast<uint8_t>(((p >> 12) & 0xf) * 17),
            static_cast<uint8_t>(((p >> 8) & 0xf) * 17),
            static_cast<uint8_t>(((p >> 4) & 0xf) * 17), static_cast<uint8_t>((p & 0xf) * 17)};
}

void push(std::vector<uint8_t>& out, const Texel& texel) {
    out.insert(out.end(), texel.begin(), texel.end());
}

// glTexImage2D data in any of the formats the titles use, converted to RGBA8.
void upload_plain(unsigned width, unsigned height, uint32_t format, uint32_t type, uint32_t data) {
    if (!plausible_size(width, height) || data == 0) {
        return;
    }
    const size_t texels = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba;
    rgba.reserve(texels * 4);
    bool coverage_only = false;  // set by the GL_ALPHA case below; see Texture::alpha_only
    // ...and the three formats that carry no colour of their own; see Texture::colourless.
    const bool colourless = type == GL_UNSIGNED_BYTE &&
                            (format == GL_ALPHA || format == GL_LUMINANCE ||
                             format == GL_LUMINANCE_ALPHA);
    for (size_t i = 0; i < texels; ++i) {
        const uint32_t index = static_cast<uint32_t>(i);
        if (type == GL_UNSIGNED_BYTE) {
            switch (format) {
            case GL_RGB: {
                const uint32_t at = data + index * 3;
                rgba.insert(rgba.end(),
                            {static_cast<uint8_t>(ld8(at)), static_cast<uint8_t>(ld8(at + 1)),
                             static_cast<uint8_t>(ld8(at + 2)), 0xff});
                break;
            }
            case GL_RGBA: {
                const uint32_t at = data + index * 4;
                rgba.insert(rgba.end(),
                            {static_cast<uint8_t>(ld8(at)), static_cast<uint8_t>(ld8(at + 1)),
                             static_cast<uint8_t>(ld8(at + 2)), static_cast<uint8_t>(ld8(at + 3))});
                break;
            }
            case GL_LUMINANCE_ALPHA: {
                const uint8_t l = static_cast<uint8_t>(ld8(data + index * 2));
                const uint8_t a = static_cast<uint8_t>(ld8(data + index * 2 + 1));
                rgba.insert(rgba.end(), {l, l, l, a});
                break;
            }
            case GL_LUMINANCE: {
                const uint8_t l = static_cast<uint8_t>(ld8(data + index));
                rgba.insert(rgba.end(), {l, l, l, 0xff});
                break;
            }
            case GL_ALPHA:
                rgba.insert(rgba.end(), {0, 0, 0, static_cast<uint8_t>(ld8(data + index))});
                coverage_only = true;
                break;
            default:
                return;  // unhandled format: leave the texture as it was
            }
        } else if (type == GL_UNSIGNED_SHORT_5_6_5) {
            push(rgba, rgb565(ld16(data + index * 2)));
        } else if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
            push(rgba, rgba5551(ld16(data + index * 2)));
        } else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
            push(rgba, rgba4444(ld16(data + index * 2)));
        } else {
            return;
        }
    }
    store_texture(width, height, std::move(rgba), coverage_only, colourless);
}

// glCompressedTexImage2D as the titles use it: a 256-entry RGBA palette followed by one index
// byte per texel.
// How many bytes one entry of an OES paletted format's palette takes.
//
// This is the whole reason the format argument reaches here. The formats differ in their palette
// *entry size*, which sets two things at once: how a colour decodes, and where the index array
// begins — 256 entries later. Decoding a 16-bit palette as RGBA8 therefore reads every colour
// through the wrong lens *and* starts the indices 512 bytes late, which does not look like a
// wrong colour. It looks like noise, and it is what Lost's jungle rendered as.
unsigned palette_entry_bytes(uint32_t format) {
    switch (format) {
    case GL_PALETTE4_RGB8:
    case GL_PALETTE8_RGB8:
        return 3;
    case GL_PALETTE4_R5_G6_B5:
    case GL_PALETTE4_RGBA4:
    case GL_PALETTE4_RGB5_A1:
    case GL_PALETTE8_R5_G6_B5:
    case GL_PALETTE8_RGBA4:
    case GL_PALETTE8_RGB5_A1:
        return 2;
    case GL_PALETTE4_RGBA8:
    case GL_PALETTE8_RGBA8:
    default:
        return 4;  // the RGBA8 palettes, and anything not recognised
    }
}

Texel palette_entry(uint32_t format, uint32_t at) {
    switch (palette_entry_bytes(format)) {
    case 3:
        return {static_cast<uint8_t>(ld8(at)), static_cast<uint8_t>(ld8(at + 1)),
                static_cast<uint8_t>(ld8(at + 2)), 0xff};
    case 2:
        switch (format) {
        case GL_PALETTE4_R5_G6_B5:
        case GL_PALETTE8_R5_G6_B5:
            return rgb565(ld16(at));
        case GL_PALETTE4_RGBA4:
        case GL_PALETTE8_RGBA4:
            return rgba4444(ld16(at));
        default:
            return rgba5551(ld16(at));
        }
    default:
        return {static_cast<uint8_t>(ld8(at)), static_cast<uint8_t>(ld8(at + 1)),
                static_cast<uint8_t>(ld8(at + 2)), static_cast<uint8_t>(ld8(at + 3))};
    }
}

// glCompressedTexImage2D data: a 256-entry palette followed by one byte of index per texel.
//
// Only the eight-bit-index formats are decoded as such. A four-bit-index format would pack two
// texels to the byte, and no title seen so far uploads one; if one does, its texture comes out
// twice as wide as it should and half of it repeated, which is visible rather than silent.
void upload_paletted(unsigned width, unsigned height, uint32_t data, uint32_t format) {
    if (!plausible_size(width, height)) {
        return;
    }
    const unsigned entry = palette_entry_bytes(format);
    std::array<Texel, 256> palette;
    for (uint32_t i = 0; i < palette.size(); ++i) {
        palette[i] = palette_entry(format, data + i * entry);
    }
    const uint32_t indices = data + static_cast<uint32_t>(palette.size()) * entry;
    const size_t texels = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba;
    rgba.reserve(texels * 4);
    for (size_t i = 0; i < texels; ++i) {
        push(rgba, palette[ld8(indices + static_cast<uint32_t>(i))]);
    }
    store_texture(width, height, std::move(rgba));
}

// glCopyTexImage2D: the framebuffer is stored top row first, GL addresses it bottom-up.
//
// The rectangle and the texture it becomes are both named in the game's own pixels, so above
// scale 1 each of them covers `scale`x`scale` of the raster and the texture is the average of
// that block. Handing back the larger picture instead would give the game a texture of a size it
// did not ask for and coordinates it did not compute — this is a capture the game goes on to
// draw with its own numbers, so it has to come back in its own units.
void copy_framebuffer_to_texture(int64_t x, int64_t y, unsigned width, unsigned height) {
    if (!plausible_size(width, height)) {
        return;
    }
    const unsigned scale = render_scale_value();
    const unsigned block = scale * scale;
    std::vector<uint8_t> rgba;
    rgba.reserve(static_cast<size_t>(width) * height * 4);
    const uint8_t* pixels = framebuffer();
    for (unsigned row = 0; row < height; ++row) {
        const int64_t source_y = static_cast<int64_t>(GAME_HEIGHT) - 1 - (y + row);
        for (unsigned column = 0; column < width; ++column) {
            const int64_t source_x = x + column;
            if (source_x < 0 || source_y < 0 || source_x >= GAME_WIDTH || source_y >= GAME_HEIGHT) {
                rgba.insert(rgba.end(), {0, 0, 0, 0xff});
                continue;
            }
            std::array<unsigned, 3> sum{};
            for (unsigned dy = 0; dy < scale; ++dy) {
                const size_t raster_row = static_cast<size_t>(source_y) * scale + dy;
                for (unsigned dx = 0; dx < scale; ++dx) {
                    const size_t offset =
                        (raster_row * raster_width() + static_cast<size_t>(source_x) * scale + dx) *
                        3;
                    for (size_t channel = 0; channel < 3; ++channel) {
                        sum[channel] += pixels[offset + channel];
                    }
                }
            }
            rgba.insert(rgba.end(), {static_cast<uint8_t>((sum[0] + block / 2) / block),
                                     static_cast<uint8_t>((sum[1] + block / 2) / block),
                                     static_cast<uint8_t>((sum[2] + block / 2) / block), 0xff});
        }
    }
    store_texture(width, height, std::move(rgba));
}

// ---------------------------------------------------------------------------------------------
// Vertex fetch and projection
// ---------------------------------------------------------------------------------------------

bool attribute_active(unsigned index) {
    return state().arrays[index].has_value() && state().attribute_enabled[index];
}

// Component `component` of vertex `vertex` from attribute array `index`, as a float. Fixed-point
// 16.16 is the type the game actually uses; the others are here because the emulator met them.
float attribute(unsigned index, uint32_t vertex, unsigned component) {
    if (!attribute_active(index)) {
        return 0.0f;
    }
    const VertexArray& array = *state().arrays[index];
    if (component >= array.components) {
        return 0.0f;
    }
    unsigned width = 4;
    if (array.type == GL_BYTE || array.type == GL_UNSIGNED_BYTE) {
        width = 1;
    } else if (array.type == GL_SHORT || array.type == GL_UNSIGNED_SHORT) {
        width = 2;
    }
    const unsigned stride = array.stride == 0 ? array.components * width : array.stride;
    const uint32_t address = array.pointer + vertex * stride + component * width;
    switch (array.type) {
    case GL_BYTE:
        return static_cast<float>(static_cast<int8_t>(ld8(address)));
    case GL_UNSIGNED_BYTE:
        return static_cast<float>(ld8(address));
    case GL_SHORT:
        return static_cast<float>(static_cast<int16_t>(ld16(address)));
    case GL_UNSIGNED_SHORT:
        return static_cast<float>(ld16(address));
    case GL_FLOAT:
        return guest_float(address);
    default:
        return static_cast<float>(static_cast<int32_t>(ld32(address))) / 65536.0f;
    }
}

bool transforming() {
    return state().model_view_projection.has_value() &&
           *state().model_view_projection != identity();
}

struct ScreenPoint {
    float x;
    float y;
};

// Positions are already in screen pixels unless a non-identity MVP was supplied through #125.
//
// **This is the only place the render scale is applied.** Everything upstream is the game's own
// arithmetic in the game's own 320x240, and everything downstream is the raster; putting the
// multiplication anywhere else would mean two units in one expression somewhere.
ScreenPoint project(float x, float y, float z, float w) {
    const float scale = static_cast<float>(render_scale_value());
    if (!transforming()) {
        return {x * scale, y * scale};
    }
    const Matrix& m = *state().model_view_projection;
    const float in[4] = {x, y, z, w};
    float out[4];
    for (unsigned row = 0; row < 4; ++row) {
        float sum = 0.0f;
        for (unsigned column = 0; column < 4; ++column) {
            sum += m[column * 4 + row] * in[column];
        }
        out[row] = sum;
    }
    const float inverse_w = std::fabs(out[3]) > 1e-6f ? 1.0f / out[3] : 1.0f;
    return {(out[0] * inverse_w + 1.0f) * 0.5f * static_cast<float>(raster_width()),
            (out[1] * inverse_w + 1.0f) * 0.5f * static_cast<float>(raster_height())};
}

// ---------------------------------------------------------------------------------------------
// Rasterisation
// ---------------------------------------------------------------------------------------------

struct Vertex {
    float x, y;                // raster position
    float u, v;                // texture coordinates in texels
    std::array<float, 3> rgb;  // per-vertex colour
    float alpha;
};

// How a textured fragment reads its texel. The first two are the framework's own two filters as
// the emulator arrived at them; the third is this project's, and is only ever chosen for a run
// of glyphs with gfx::set_high_resolution_text on.
enum class Filter { Bilinear, Nearest, Glyph };

// The texels a draw is entitled to read: the region of the atlas its own coordinates name.
//
// Sprites are packed into a sheet, and a bilinear tap half a texel outside the sprite reads its
// neighbour. It shows most where a symmetrical thing is drawn as one half twice: the tutorial's
// click wheel is a 45x90 half-disc drawn at x 114.8..159.8 with u 0..45 and then again, mirrored,
// at x 204.7..159.8 with the same u — so the two innermost columns each reached past u=45 into
// whatever was packed beside it and came back dark. The result is a seam down the middle of the
// wheel, of Jack, and of anything else drawn as two halves.
//
// GL would call this clamping to the edge; the edge here is the sprite's, not the sheet's.
struct TexelBounds {
    unsigned left, right, top, bottom;  // inclusive texel indices
};

// Box-filtered sample over an n×n texel footprint, for textures drawn smaller than they are:
// the ground is dithered art shown at two to three texels per pixel, and sampling one or two
// texels out of each footprint turns the dither into moiré bands (the hardware's mipmaps
// average it away; this does the same).
std::array<uint8_t, 4> sample_texture_box(const Texture& t, float u, float v, unsigned n,
                                          const TexelBounds& within) {
    const int half = static_cast<int>(n) / 2;
    const int cx = static_cast<int>(std::floor(u)), cy = static_cast<int>(std::floor(v));
    std::array<unsigned, 4> sum{};
    for (int dy = -half; dy < static_cast<int>(n) - half; ++dy) {
        const int y =
            std::clamp(cy + dy, static_cast<int>(within.top), static_cast<int>(within.bottom));
        for (int dx = -half; dx < static_cast<int>(n) - half; ++dx) {
            const int x =
                std::clamp(cx + dx, static_cast<int>(within.left), static_cast<int>(within.right));
            const size_t p = (static_cast<size_t>(y) * t.width + static_cast<size_t>(x)) * 4;
            for (size_t c = 0; c < 4; ++c) {
                sum[c] += t.rgba[p + c];
            }
        }
    }
    std::array<uint8_t, 4> out;
    for (size_t c = 0; c < 4; ++c) {
        out[c] = static_cast<uint8_t>((sum[c] + n * n / 2) / (n * n));
    }
    return out;
}

// Sample the bound texture at (u, v), with the half-texel offset and edge clamping.
//
// Two rules here are measured rather than chosen, and both come from the emulator's rasteriser
// (reference/eapp-loader/lib.rs):
//
// * **The colour is filtered weighted by alpha** — premultiplied, in effect. Straight bilinear
//   averages the RGB of all four texels whether or not they are transparent, and these titles
//   key transparency with magenta at alpha 0, so every keyed edge blended real colour with
//   bright magenta and came out fringed in pink. Weighting each texel's colour by its own alpha
//   drops the transparent ones out of the colour average entirely, which is what the hardware
//   effectively did. Alpha itself filters normally, so edges keep their soft falloff.
//
// * **`Nearest` takes the nearest texel outright.** It is chosen for a draw that is a 1:1 blit
//   (see `is_one_to_one`), where bilinear buys nothing and costs sharpness: text laid out at
//   fractional positions samples between texels and softens, and while it moves that offset
//   changes continuously, so the text visibly smears.
//
// `Glyph` is this project's own and is neither: see `Filter` and gfx::set_high_resolution_text.
// Which way a texture coordinate runs against the pixel it is sampled at: +1 when it grows with
// the pixel index, -1 when it shrinks, and **0 for the emulator's rule** — round the tie up,
// whichever way the coordinate runs — which is what `--emulator-graphics` and therefore the
// picture oracle wants. Used only to break a tie; see `sample_texture`.
struct TieBias {
    float u = 0.0f;
    float v = 0.0f;
};

std::array<uint8_t, 4> sample_texture(const Texture& t, float u, float v, Filter filter,
                                      float pixels_per_texel, const TexelBounds& within,
                                      TieBias tie = {}) {
    const float sx = std::clamp(u - 0.5f, 0.0f, static_cast<float>(t.width - 1));
    const float sy = std::clamp(v - 0.5f, 0.0f, static_cast<float>(t.height - 1));
    const unsigned x0 = std::clamp(static_cast<unsigned>(sx), within.left, within.right);
    const unsigned y0 = std::clamp(static_cast<unsigned>(sy), within.top, within.bottom);
    const unsigned x1 = std::min(x0 + 1, within.right), y1 = std::min(y0 + 1, within.bottom);
    float dx = sx - static_cast<float>(x0), dy = sy - static_cast<float>(y0);
    const auto at = [&](unsigned x, unsigned y) {
        return (static_cast<size_t>(y) * t.width + x) * 4;
    };

    // `Nearest` taken as one texel rather than as a bilinear tap with three weights of zero.
    //
    // This is not a shortcut with a rounding cost — it is the same answer, and the four lines
    // below say why. Snapping dx and dy leaves exactly one weight at 1.0 and three at 0.0, so
    // `alpha_sum` is that texel's own alpha exactly, and each channel's sum is `alpha * colour`,
    // a product of two integers under 256 and therefore exact in a float; dividing it by that
    // same alpha gives the colour back unchanged. The general path below computes precisely this,
    // through twelve multiplications and three divisions.
    //
    // It matters because **this is the common path**: every sprite in this game is a 1:1 blit,
    // and at a render scale above 1 the whole scene is one (`is_one_to_one_over` asks in the
    // game's own pixels, so raising the scale does not turn these into bilinear taps).
    if (filter == Filter::Nearest) {
        // The tie, and which side of it the artwork is on.
        //
        // At a 1:1 blit the sample lands *exactly* between two texels for every pixel of the
        // draw, because these games place their quads on a half-texel offset — a full-screen
        // backdrop at `uv [0.5..320.5]`, a glyph cell at `uv [133.5..142.5]`. Neither texel is
        // nearer, so "nearest" does not answer the question; the quad does. Its edges map the
        // same number of pixels onto the same number of texels, so pixel *k* of the quad is
        // texel *k* of the quad — which means the tie must resolve toward the edge the
        // coordinate came from: down where the coordinate grows with the pixel index, up where
        // it shrinks (a `v` that runs backwards through the projection's flip).
        //
        // Get it wrong and every glyph in the game is drawn one texel to the right: its own
        // leftmost column is dropped and a sliver of the *next* cell in the atlas appears at its
        // right edge. That is the row of stray ticks and doubled strokes over Cubis 2's menus,
        // and it is a whole pixel out of a nine-pixel letter.
        //
        // The 1e-3 is what makes it a tie test at all rather than a coin toss: the interpolated
        // coordinate carries ~1e-6 of arithmetic noise, so a bare comparison against 0.5 lands
        // on either side per pixel. Away from the tie this is ordinary nearest sampling.
        constexpr float TIE = 1e-3f;
        const auto beyond = [](float fraction, float direction) {
            if (direction == 0.0f) {
                return fraction >= 0.5f - TIE;  // the emulator's rule; see `TieBias`
            }
            return direction > 0.0f ? fraction >= 0.5f + TIE : fraction > 0.5f - TIE;
        };
        const bool take_x1 = beyond(dx, tie.u);
        const bool take_y1 = beyond(dy, tie.v);
        const size_t p = at(take_x1 ? x1 : x0, take_y1 ? y1 : y0);
        if (t.rgba[p + 3] == 0) {
            return {};  // fully transparent: the general path answers all zeros here too
        }
        return {t.rgba[p], t.rgba[p + 1], t.rgba[p + 2], t.rgba[p + 3]};
    }
    const size_t p[4] = {at(x0, y0), at(x1, y0), at(x0, y1), at(x1, y1)};
    const float weight[4] = {(1.0f - dx) * (1.0f - dy), dx * (1.0f - dy), (1.0f - dx) * dy,
                             dx * dy};

    float alpha_sum = 0.0f;
    for (size_t corner = 0; corner < 4; ++corner) {
        alpha_sum += weight[corner] * static_cast<float>(t.rgba[p[corner] + 3]);
    }
    std::array<uint8_t, 4> out{};
    // The coverage a glyph's edge is reconstructed from. Bilinear spreads the sheet's own edge
    // over one *texel*, which is `pixels_per_texel` pixels once the picture is enlarged; taking
    // the filtered coverage back to full contrast over that distance puts the edge back where
    // the sheet says it is and keeps it one pixel wide. The colour average below is deliberately
    // still weighted by the *filtered* alpha, not this one: what is being sharpened is where the
    // letter ends, not which texels contributed its colour.
    float coverage = alpha_sum;
    if (filter == Filter::Glyph && pixels_per_texel > 1.0f) {
        // Against this face's own ink, not against 255. A 7-pixel face is antialiased into
        // permanent translucency — `maiandra-7` peaks around two thirds — so measuring its
        // coverage on a 0..255 scale puts every texel of every letter below the curve's midpoint
        // and the letter disappears. Measured against its peak, its solid middle reads as 1 and
        // its edge as the ramp it is, and the reconstruction puts the edge back where the sheet
        // says it is *at the weight the artist drew it*. With `ink` at 255 this is the same
        // expression it always was, so the big faces are unchanged. See `cell_coverage_peak`.
        const float ink = static_cast<float>(state().glyph_ink);
        const float unit = std::clamp(alpha_sum / ink, 0.0f, 1.0f);
        const float contrast = std::min(pixels_per_texel, GLYPH_MAX_CONTRAST);
        const float sharpened = ink * std::clamp(0.5f + (unit - 0.5f) * contrast, 0.0f, 1.0f);
        // The faithful sample this pixel would have had without the feature: at 1:1 — which every
        // quad of a run is, `is_text_run` requires it — that is the texel the quad's own edges
        // put here, and it is *exact*. It is the floor below, because a feature that is off by
        // default must never render a glyph with less ink than leaving it off would.
        const size_t nearest = at(dx >= 0.5f ? x1 : x0, dy >= 0.5f ? y1 : y0);
        const float faithful = static_cast<float>(t.rgba[nearest + 3]);
        // Sharpen, but never erase ink the sheet actually has.
        //
        // The curve above is a *hard-edge* model: it assumes a texel below half coverage is
        // outside the letter and antialiasing put it there. That holds for a big clean face and
        // holds for neither of the small ones here — measured cell by cell, only 15 % of
        // `maiandra-7`'s ink and 27 % of `rockart-11`'s is at full weight, so for both of them
        // most of the letter *is* ramp and the ramp is the design. Taking the model literally
        // erased three quarters of every stroke: Cubis 2's `SCORE` came out `SCVRK` at any
        // render scale above 1, and had done since before the ink normalisation above existed.
        //
        // Flooring at the faithful sample leaves the sharpening free to firm up an edge and
        // unable to delete a thin stroke. Where the model does fit — a face whose interior is
        // solid — the sharpened value is the larger one everywhere that matters, so those faces
        // are unchanged.
        coverage = std::max(sharpened, faithful);
    }
    out[3] = static_cast<uint8_t>(std::clamp(std::round(coverage), 0.0f, 255.0f));
    if (alpha_sum <= 0.0f) {
        return out;  // fully transparent: there is no colour to average
    }
    for (size_t channel = 0; channel < 3; ++channel) {
        float sum = 0.0f;
        for (size_t corner = 0; corner < 4; ++corner) {
            sum += weight[corner] * static_cast<float>(t.rgba[p[corner] + 3]) *
                   static_cast<float>(t.rgba[p[corner] + channel]);
        }
        out[channel] = static_cast<uint8_t>(std::clamp(std::round(sum / alpha_sum), 0.0f, 255.0f));
    }
    return out;
}

uint8_t modulated(uint8_t value, float factor) {
    return static_cast<uint8_t>(
        std::clamp(std::round(static_cast<float>(value) * factor), 0.0f, 255.0f));
}

// `band_first`/`band_last` are the rows of the raster this call may write, inclusive. They are
// the whole picture on one thread and one stripe of it on several (`rasterise_triangles`), and
// they narrow *which pixels are visited* and nothing else: every pixel that is drawn is drawn by
// exactly one call, from the same arithmetic, in the same order relative to the other draws that
// touch it.
void fill_triangle(const Vertex& a, const Vertex& b, const Vertex& c, bool textured,
                   const Texture* texture, Filter filter, unsigned band_first, unsigned band_last) {
    const unsigned width = raster_width(), height = raster_height();
    const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (std::fabs(area) < 1e-6f) {
        return;  // degenerate
    }
    // The rectangle a primitive covers is HALF-OPEN: a pixel belongs to it when its centre is
    // inside, and the far edge is not inside.
    //
    // These games place their quads on half-pixel boundaries — a glyph cell at `x [83.5..88.5]`,
    // five pixels wide — so the far edge lands exactly on a pixel centre and an inclusive box
    // covers six. That sixth pixel is the next glyph's, and drawing it grafts a column of one
    // letter onto the end of the previous one: `Arcade` came out `Ahcadc`, and every string in
    // the game was a pixel wider than the font says. Two abutting quads paint the shared column
    // twice for the same reason.
    //
    // `ceil(edge - 0.5)` is the first pixel whose centre is at or past `edge`, so the last pixel
    // inside is one before the far edge's. Off under `--emulator-graphics`, which reproduces the
    // emulator's inclusive box for the picture oracle.
    const bool half_open = !state().emulator_graphics;
    const float raw_max_x = std::max({a.x, b.x, c.x}), raw_max_y = std::max({a.y, b.y, c.y});
    const float raw_min_x = std::min({a.x, b.x, c.x}), raw_min_y = std::min({a.y, b.y, c.y});
    const float max_x =
        std::min(half_open ? std::ceil(raw_max_x - 0.5f) - 1.0f : std::ceil(raw_max_x),
                 static_cast<float>(width - 1));
    const float max_y =
        std::min(half_open ? std::ceil(raw_max_y - 0.5f) - 1.0f : std::ceil(raw_max_y),
                 static_cast<float>(height - 1));
    if (max_x < 0.0f || max_y < 0.0f) {
        return;
    }
    const unsigned min_x = static_cast<unsigned>(
        std::max(half_open ? std::ceil(raw_min_x - 0.5f) : std::floor(raw_min_x), 0.0f));
    unsigned min_y = static_cast<unsigned>(
        std::max(half_open ? std::ceil(raw_min_y - 0.5f) : std::floor(raw_min_y), 0.0f));
    unsigned last_y = static_cast<unsigned>(max_y);
    // The rows this triangle wants, narrowed to the rows this call owns. A stripe that the
    // triangle misses entirely costs one comparison.
    min_y = std::max(min_y, band_first);
    last_y = std::min(last_y, band_last);
    if (min_y > last_y) {
        return;
    }
    // Texels per pixel over the triangle: a minified texture is sampled nearest, like the
    // framework's default filter, anything else bilinearly.
    //
    // Under `--emulator-graphics` the box filter stays off: the emulator samples a minified
    // texture nearest, and the picture oracle compares against it pixel for pixel. Texas
    // Hold'em draws its card backs at half size, and the filter alone moved 2.5% of that frame —
    // all of it on the cards' edges and the logo inside them.
    const float texel_area = std::fabs((b.u - a.u) * (c.v - a.v) - (c.u - a.u) * (b.v - a.v));
    const float texels_per_pixel = texture != nullptr && std::fabs(area) > 0.0f
                                       ? std::sqrt(texel_area / std::fabs(area))
                                       : 1.0f;
    const unsigned footprint = texels_per_pixel > MINIFIED_RATIO && !state().emulator_graphics
                                   ? static_cast<unsigned>(std::lround(texels_per_pixel))
                                   : 1;
    // The other way round, which is what a glyph's edge is reconstructed over: how many raster
    // pixels one texel of the sheet covers. It is the render scale for a glyph drawn 1:1 with a
    // game pixel, which is what a run of text is, and it is 1 when there is nothing to resolve.
    const float pixels_per_texel = texels_per_pixel > 0.0f ? 1.0f / texels_per_pixel : 1.0f;
    // Which way each texture coordinate runs against the pixel index, for the tie in
    // `sample_texture`. A 1:1 blit is axis-aligned, so `u` is a function of `x` alone and `v` of
    // `y`: take the sign from whichever pair of vertices differ in that axis. A triangle with no
    // extent in an axis contributes no pixels there, so the fallback never decides anything.
    //
    // Under `--emulator-graphics` both stay +1, which is the emulator's own rule and what the
    // picture oracle compares against.
    const auto direction = [](float first, float second, float from, float to) {
        if (std::fabs(to - from) < 1e-6f) {
            return 1.0f;
        }
        return (second - first) / (to - from) < 0.0f ? -1.0f : 1.0f;
    };
    TieBias tie;
    if (!state().emulator_graphics) {
        const float ux = std::fabs(b.x - a.x) > std::fabs(c.x - a.x)
                             ? direction(a.u, b.u, a.x, b.x)
                             : direction(a.u, c.u, a.x, c.x);
        const float vy = std::fabs(b.y - a.y) > std::fabs(c.y - a.y)
                             ? direction(a.v, b.v, a.y, b.y)
                             : direction(a.v, c.v, a.y, c.y);
        tie = TieBias{ux, vy};
    }
    // The corner of the sheet this draw may read. Its own coordinates say where that is: the
    // three vertices of a triangle span the whole of its quad's texture rectangle, because a
    // quad is split along a diagonal whose ends are opposite corners.
    TexelBounds within{0, texture == nullptr ? 0 : texture->width - 1, 0,
                       texture == nullptr ? 0 : texture->height - 1};
    if (texture != nullptr) {
        const float u_low = std::min({a.u, b.u, c.u}), u_high = std::max({a.u, b.u, c.u});
        const float v_low = std::min({a.v, b.v, c.v}), v_high = std::max({a.v, b.v, c.v});
        const auto texel = [](float edge, unsigned last) {
            return static_cast<unsigned>(std::clamp(edge, 0.0f, static_cast<float>(last)));
        };
        // The rectangle is HALF-OPEN at its far edge, and that is the whole of this.
        //
        // A glyph cell of `uv [6.5 .. 11.5]` is five texels wide — 6, 7, 8, 9, 10 — and 11.5 is
        // where it stops, not a sixth texel. But the quad's far edge lands exactly on a pixel
        // centre, so the rasteriser covers six pixels, and the sixth samples texel 11: the first
        // column of the *next* glyph in the atlas. Every string in the game was drawn with a
        // sliver of the following letter grafted onto each character — `Arcade` came out
        // `Ahcadc` — and the same at the far edge of any 1:1 blit.
        //
        // Taking the span from the rectangle's own width rather than from `ceil` of its far edge
        // is exact for a 1:1 blit and right for any other: `left + span - 1` is the last texel
        // the rectangle contains, whatever the scale.
        const auto span = [](float low, float high) {
            return std::max(1, static_cast<int>(std::lround(high - low)));
        };
        within.left = texel(std::floor(u_low), texture->width - 1);
        within.right = std::min(texture->width - 1,
                                within.left + static_cast<unsigned>(span(u_low, u_high)) - 1);
        within.top = texel(std::floor(v_low), texture->height - 1);
        within.bottom = std::min(texture->height - 1,
                                 within.top + static_cast<unsigned>(span(v_low, v_high)) - 1);
        if (state().emulator_graphics) {
            within = TexelBounds{0, texture->width - 1, 0, texture->height - 1};
        }
    }

    const bool flip_y = !(state().projection_flips_y && !transforming());
    const std::array<float, 4>& modulate = state().modulate;
    // Where the pipeline's constant colour reaches the fragment. It is *not* a general tint:
    // this driver's combiner leaves an ordinary texture's own colours alone, and multiplying
    // them by the register instead casts the whole picture in whatever the game last set — which
    // for Lost is a green. The case where the register *is* the colour is a texture that has
    // none of its own (Texture::colourless), and then only when no colour array is supplying a
    // primary colour instead (GL ES 1.1 §2.7: an enabled colour array replaces the current
    // colour outright). Both halves are measured behaviour — see reference/eapp-loader/lib.rs,
    // which arrived at them through SAT Prep's black question text on a white panel, Hold'em's
    // gold name-entry panel and Cubis 2's grey board.
    const bool colour_supplied_by_array = attribute_active(2) && state().arrays[2].has_value();
    const bool tint_applies = modulate != std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f} &&
                              texture != nullptr && texture->colourless &&
                              !colour_supplied_by_array;
    // A draw with no texture takes its colour from the constant register — but only in a game
    // that has put something there.
    //
    // This is the second thing the two titles disagree about, and like the first it is a fact
    // about the game rather than about the hardware. Lost sets the register (`#147`) before each
    // block of draws and its letterbox bars and dialogue panels are painted from it. Mini Golf
    // never sets it — it does not import the ordinal at all — and carries the colour of an
    // untextured draw in the vertex array, as GL would by default. Painting its flat draws from
    // an unset register meant opaque white, and whole screens came out blank.
    //
    // So the register is used when there *is* one, which needs no title to declare anything: the
    // state says whether the game ever wrote it.
    const bool flat = !textured && !state().emulator_graphics && state().constant_colour_set;
    const std::array<uint8_t, 3> flat_rgb = {to_byte(modulate[0]), to_byte(modulate[1]),
                                             to_byte(modulate[2])};
    const uint8_t flat_alpha = to_byte(modulate[3]);
    uint8_t* pixels = framebuffer();

    // How far along a row this triangle can possibly reach.
    //
    // Each of the three edge functions is *affine in fx* at a fixed fy — expanding the cross
    // product above leaves `numerator = K + fx * slope` — so the pixels a row can contribute are
    // one interval, and the two thirds of a bounding box that a pair of triangles does not cover
    // need never be visited at all. That is where the time went: a quad is drawn as two triangles
    // and each one's bounding box is twice its area, so half of every fragment's worth of edge
    // arithmetic was being spent deciding not to draw it.
    //
    // The interval is deliberately widened by a pixel at each end and **the original per-pixel
    // test below is kept**. This arithmetic decides only which pixels are *looked at*; which are
    // *drawn* is still decided exactly as it was, so no edge pixel can fall on a different side
    // of the test because of how its row was bounded.
    const float inverse_area = 1.0f / area;
    const float sign = area > 0.0f ? 1.0f : -1.0f;
    const float slope0 = (b.y - c.y) * sign;
    const float slope1 = (c.y - a.y) * sign;
    const float slope2 = (a.y - b.y) * sign;

    for (unsigned py = min_y; py <= last_y; ++py) {
        const float fy = static_cast<float>(py) + 0.5f;
        // Each numerator at fx = 0, in the orientation where "inside" means "not negative".
        const float base0 = (b.x * (c.y - fy) - c.x * (b.y - fy)) * sign;
        const float base1 = (c.x * (a.y - fy) - a.x * (c.y - fy)) * sign;
        const float base2 = area * sign - base0 - base1;
        float low = static_cast<float>(min_x), high = static_cast<float>(max_x);
        bool empty = false;
        const auto narrow = [&](float base, float slope) {
            if (slope > 0.0f) {
                low = std::max(low, (-base / slope) - 1.5f);
            } else if (slope < 0.0f) {
                high = std::min(high, (-base / slope) + 0.5f);
            } else if (base < 0.0f) {
                empty = true;  // this row is on the wrong side of an edge parallel to it
            }
        };
        narrow(base0, slope0);
        narrow(base1, slope1);
        narrow(base2, slope2);
        if (empty || high < low) {
            continue;
        }
        const unsigned row_first = std::max(min_x, static_cast<unsigned>(std::max(low, 0.0f)));
        // `max_x` is already clamped to `width - 1` and `py` to `height - 1` above, so the
        // per-pixel bounds test the loop used to carry could never fire; the clamp is here.
        const unsigned row_last = std::min(static_cast<unsigned>(max_x),
                                           static_cast<unsigned>(std::max(std::ceil(high), 0.0f)));

        for (unsigned px = row_first; px <= row_last; ++px) {
            const float fx = static_cast<float>(px) + 0.5f;
            // `* inverse_area`, not `/ area`: one reciprocal a triangle instead of two divisions
            // a *pixel*, which on this machine is about a third of the whole inner loop. The two
            // differ by at most an ulp, and the one place a rasteriser is sensitive to an ulp —
            // the half-texel tie of a 1:1 blit — is decided with a tolerance a thousand times
            // wider than that, for exactly this reason (see `sample_texture`).
            const float w0 = ((b.x - fx) * (c.y - fy) - (c.x - fx) * (b.y - fy)) * inverse_area;
            const float w1 = ((c.x - fx) * (a.y - fy) - (a.x - fx) * (c.y - fy)) * inverse_area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                continue;
            }
            std::array<uint8_t, 3> rgb;
            for (size_t i = 0; i < 3; ++i) {
                rgb[i] = to_byte(w0 * a.rgb[i] + w1 * b.rgb[i] + w2 * c.rgb[i]);
            }
            uint8_t alpha = textured ? 255 : to_byte(w0 * a.alpha + w1 * b.alpha + w2 * c.alpha);
            if (flat) {
                rgb = flat_rgb;
                alpha = flat_alpha;
            }
            if (alpha == 0) {
                continue;
            }
            const std::array<uint8_t, 4> texel =
                texture == nullptr || flat
                    ? std::array<uint8_t, 4>{}
                    : (footprint > 1
                           ? sample_texture_box(*texture, w0 * a.u + w1 * b.u + w2 * c.u,
                                                w0 * a.v + w1 * b.v + w2 * c.v, footprint, within)
                           : sample_texture(*texture, w0 * a.u + w1 * b.u + w2 * c.u,
                                            w0 * a.v + w1 * b.v + w2 * c.v, filter,
                                            pixels_per_texel, within, tie));
            if (!flat && texture != nullptr) {
                if (texel[3] < COLOUR_KEY_ALPHA) {
                    continue;
                }
                alpha = texel[3];
                // An alpha-only texture is coverage: the fragment keeps the colour it arrived
                // with (see Texture::alpha_only). Otherwise the texture *modulates* the
                // fragment's colour rather than replacing it — `Cv = Cp * Cs`. The two work out
                // the same whenever the primary colour is white, which is nearly every draw, and
                // differ exactly where a title puts its ink colour in the vertex array and draws
                // a white glyph through it.
                if (!texture->alpha_only) {
                    for (size_t i = 0; i < 3; ++i) {
                        rgb[i] = static_cast<uint8_t>(
                            (static_cast<uint32_t>(texel[i]) * rgb[i] + 127u) / 255u);
                    }
                }
            }
            if (tint_applies) {
                for (size_t i = 0; i < 3; ++i) {
                    rgb[i] = modulated(rgb[i], modulate[i]);
                }
                alpha = modulated(alpha, modulate[3]);
                if (alpha < COLOUR_KEY_ALPHA) {
                    continue;
                }
            }
            const unsigned row = flip_y ? height - 1 - py : py;
            const size_t offset = (static_cast<size_t>(row) * width + px) * 3;
            if (alpha < 255) {
                const uint32_t coverage = alpha, inverse = 255 - coverage;
                for (size_t k = 0; k < 3; ++k) {
                    rgb[k] = static_cast<uint8_t>(
                        (rgb[k] * coverage + pixels[offset + k] * inverse + 127) / 255);
                }
            }
            std::copy(rgb.begin(), rgb.end(), pixels + offset);
        }
    }
}

// glDrawArrays: attribute 0 is position, attribute 1 is either texture coordinates or — when it
// has four components — the colour; attribute 2 is a colour when attribute 1 is not.
// Is this draw a 1:1 blit — one texel per pixel, in both directions? Bilinear filtering exists
// for the titles that scale; at 1:1 it buys nothing and costs sharpness, so a draw that answers
// yes here samples the nearest texel instead (see `sample_texture`). The tolerance and the
// half-pixel floor are the emulator's (reference/eapp-loader/lib.rs).
float extent_low(const std::vector<Vertex>& vertices, float Vertex::* field) {
    float low = vertices[0].*field;
    for (const Vertex& vertex : vertices) {
        low = std::min(low, vertex.*field);
    }
    return low;
}

float extent_high(const std::vector<Vertex>& vertices, float Vertex::* field) {
    float high = vertices[0].*field;
    for (const Vertex& vertex : vertices) {
        high = std::max(high, vertex.*field);
    }
    return high;
}

// The lowest, the highest, and the span of `field` over vertices [from, to).
float extent_low_over(const std::vector<Vertex>& vertices, size_t from, size_t to,
                      float Vertex::* field) {
    float low = vertices[from].*field;
    for (size_t i = from; i < to; ++i) {
        low = std::min(low, vertices[i].*field);
    }
    return low;
}

float extent_high_over(const std::vector<Vertex>& vertices, size_t from, size_t to,
                       float Vertex::* field) {
    float high = vertices[from].*field;
    for (size_t i = from; i < to; ++i) {
        high = std::max(high, vertices[i].*field);
    }
    return high;
}

float extent_over(const std::vector<Vertex>& vertices, size_t from, size_t to,
                  float Vertex::* field) {
    return extent_high_over(vertices, from, to, field) - extent_low_over(vertices, from, to, field);
}

// Is [from, to) one texel per *game* pixel, in both directions?
//
// The game's pixels, not the raster's: at a render scale above 1 a 1:1 blit covers that many
// raster pixels and its texture is no bigger, and asking the question in raster pixels would
// answer no for every one of them — which would quietly turn the sharpness this test exists to
// keep into a bilinear blur the moment anyone raised the scale.
bool is_one_to_one_over(const std::vector<Vertex>& vertices, size_t from, size_t to) {
    const float scale = static_cast<float>(render_scale_value());
    const float width = extent_over(vertices, from, to, &Vertex::x) / scale;
    const float height = extent_over(vertices, from, to, &Vertex::y) / scale;
    const float texels_x = extent_over(vertices, from, to, &Vertex::u);
    const float texels_y = extent_over(vertices, from, to, &Vertex::v);
    constexpr float TOLERANCE = 0.02f;
    return width > 0.5f && height > 0.5f && std::fabs(texels_x / width - 1.0f) < TOLERANCE &&
           std::fabs(texels_y / height - 1.0f) < TOLERANCE;
}

bool is_one_to_one(const std::vector<Vertex>& vertices) {
    return is_one_to_one_over(vertices, 0, vertices.size());
}

// Is *every quad* in this draw a 1:1 blit, even though the draw as a whole is not?
//
// A run of text is exactly that shape and is the reason this exists. The glyphs of a string sit
// at unrelated places in the font atlas, so the draw's total texture extent has nothing to do
// with its total width on screen and `is_one_to_one` above answers no — while each individual
// glyph is a perfect 1:1 blit. Taking the draw's answer meant every string in these games was
// sampled bilinearly: soft letters, and a column of the *neighbouring* glyph's cell bled into
// each one, which is the row of stray ticks and doubled strokes Cubis 2's menus were full of.
//
// It has to be all of them. A draw that mixes 1:1 quads with scaled ones has no single right
// filter, and the scaled ones are the ones that would suffer, so the mixed case keeps bilinear.
bool every_quad_is_one_to_one(const std::vector<Vertex>& vertices, uint32_t mode) {
    if (mode != GL_QUADS || vertices.size() < 8 || vertices.size() % 4 != 0) {
        return false;  // one quad is `is_one_to_one`'s own case; anything else is not a run
    }
    for (size_t quad = 0; quad < vertices.size(); quad += 4) {
        if (!is_one_to_one_over(vertices, quad, quad + 4)) {
            return false;
        }
    }
    return true;
}

// The largest a glyph is allowed to be, in the game's own pixels. The dialogue font draws at
// about 11x13; the ceiling is only here to keep a screen-sized 1:1 blit out of `is_text_run`.
constexpr float MAX_GLYPH_SIDE = 48.0f;
// Four, not two. A two-quad draw of small 1:1 rectangles is far more often a sprite drawn as one
// half and its mirror — which is how this game draws a character's drop shadow, and its click
// wheel, and Jack himself — than it is a word. Text this short falls back to `Nearest`, which is
// what it got at every render scale before this feature existed.
constexpr size_t MIN_GLYPHS_IN_A_RUN = 4;
// The least ink a cell must reach before it counts as a letter rather than a smudge. See
// `cell_coverage_peak`: the *inside* of a letter is that cell's own peak, not 255.
constexpr uint8_t GLYPH_MINIMUM_INK = 64;

// Is this draw a run of text — one quad per glyph, each 1:1 with its own cell of a sheet?
//
// The game draws a whole line of dialogue as a single `glDrawArrays` of quads: 121 of them for
// the Chapter 1 card, each about 11x13 pixels, each pointing at its own small rectangle of the
// font sheet, and each landing at a fractional position because the line is laid out in the
// game's own sub-pixel arithmetic. That shape is what is recognised here, and it is a shape no
// other draw in this game has: a sprite is one quad, the scene is a handful of large ones, and
// nothing else puts dozens of small independently-mapped 1:1 rectangles in one call.
//
// It has to be asked per quad, and that is the whole reason this exists next to
// `is_one_to_one`: over the *draw* the glyphs span the width of the screen while their texture
// coordinates wander over the sheet, so the draw-wide test says "not 1:1" and the run has been
// sampled bilinearly all along.
// ---------------------------------------------------------------------------------------------
// Drawing a triangle list, on one thread or several
// ---------------------------------------------------------------------------------------------

// Every triangle of a draw, over the rows [first, last].
void rasterise_triangles(const std::vector<Vertex>& vertices, uint32_t mode, bool textured,
                         const Texture* texture, Filter filter, unsigned first, unsigned last) {
    const size_t n = vertices.size();
    if (mode == GL_TRIANGLE_STRIP) {
        for (size_t i = 0; i + 2 < n; ++i) {
            fill_triangle(vertices[i], vertices[i + 1], vertices[i + 2], textured, texture, filter,
                          first, last);
        }
    } else if (mode == GL_QUADS && n >= 4 && n % 4 == 0) {
        for (size_t q = 0; q < n; q += 4) {
            fill_triangle(vertices[q], vertices[q + 1], vertices[q + 2], textured, texture, filter,
                          first, last);
            fill_triangle(vertices[q], vertices[q + 2], vertices[q + 3], textured, texture, filter,
                          first, last);
        }
    } else {  // triangle fan, which also covers GL_TRIANGLES for the single quads the game draws
        for (size_t i = 1; i + 1 < n; ++i) {
            fill_triangle(vertices[0], vertices[i], vertices[i + 1], textured, texture, filter,
                          first, last);
        }
    }
}

// --- the workers -----------------------------------------------------------------------------
//
// The picture is cut into horizontal stripes and each is drawn by whichever thread takes it next.
// **This changes nothing about what is drawn.** A pixel belongs to exactly one stripe, so the
// draws that touch it still reach it in the order the game issued them, through the same
// arithmetic, and every frame is bit-for-bit the frame one thread would have produced. That is
// worth being exact about, because it is what lets the picture oracle keep its job.
//
// Stripes are taken rather than dealt out: this machine has eight fast cores and two slow ones,
// and an even share would leave the fast eight waiting on the slow two. Several stripes per
// worker, claimed with one atomic increment, cost nothing and let the fast cores take more.
constexpr unsigned STRIPES_PER_WORKER = 4;
constexpr unsigned MIN_STRIPE_ROWS = 16;
// Below this many raster pixels a draw is not worth waking anybody for.
//
// Measured, on the level this game's picture oracle draws, at render scales 1, 2 and 4: 2 048 is
// the best of 1 024 / 2 048 / 4 096 / 8 192 / 16 384 / 96 * 1024 at every one of them, and the
// old value — which asked about the *framebuffer* rather than the draw — was the worst.
constexpr uint64_t MIN_PIXELS_TO_SHARE = 2048;

struct Job {
    const std::vector<Vertex>* vertices;
    uint32_t mode;
    bool textured;
    const Texture* texture;
    Filter filter;
    unsigned first_row;    // the first raster row this draw can reach
    unsigned rows;         // how many rows from there it can reach
    unsigned stripe_rows;  // how tall each stripe is
    unsigned stripes;
};

class RasterWorkers {
public:
    ~RasterWorkers() { stop(); }

    // How many threads to draw with; 0 asks for one per core. Answers what it settled on.
    unsigned set_count(unsigned wanted) {
        const unsigned cores = std::thread::hardware_concurrency();
        const unsigned n = wanted != 0 ? wanted : (cores != 0 ? cores : 1);
        if (n == count_) {
            return count_;
        }
        stop();
        count_ = n;
        return count_;
    }

    // 0 means "not chosen yet", which resolves to one per core the first time anybody asks.
    [[nodiscard]] unsigned count() {
        if (count_ == 0) {
            set_count(0);
        }
        return count_;
    }

    void run(const Job& job) {
        start();
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            job_ = &job;
            next_stripe_.store(0, std::memory_order_relaxed);
            outstanding_ = helpers_;
            ++generation_;
        }
        work_ready_.notify_all();
        take_stripes(job);  // this thread is one of the workers, not a spectator
        std::unique_lock<std::mutex> lock(mutex_);
        work_done_.wait(lock, [this] { return outstanding_ == 0; });
        job_ = nullptr;
    }

private:
    void start() {
        if (!threads_.empty() || count_ <= 1) {
            return;
        }
        helpers_ = count_ - 1;  // the calling thread draws too
        stopping_ = false;
        threads_.reserve(helpers_);
        for (unsigned i = 0; i < helpers_; ++i) {
            threads_.emplace_back([this] { serve(); });
        }
    }

    void stop() {
        if (threads_.empty()) {
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        work_ready_.notify_all();
        for (std::thread& thread : threads_) {
            thread.join();
        }
        threads_.clear();
        helpers_ = 0;
    }

    void serve() {
        uint64_t seen = 0;
        for (;;) {
            const Job* job = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_ready_.wait(lock, [this, seen] { return stopping_ || generation_ != seen; });
                if (stopping_) {
                    return;
                }
                seen = generation_;
                job = job_;
            }
            if (job != nullptr) {
                take_stripes(*job);
            }
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                --outstanding_;
            }
            work_done_.notify_one();
        }
    }

    void take_stripes(const Job& job) {
        for (;;) {
            const unsigned stripe = next_stripe_.fetch_add(1, std::memory_order_relaxed);
            if (stripe >= job.stripes) {
                return;
            }
            const unsigned first = job.first_row + stripe * job.stripe_rows;
            const unsigned last =
                job.first_row + std::min((stripe + 1) * job.stripe_rows, job.rows) - 1;
            rasterise_triangles(*job.vertices, job.mode, job.textured, job.texture, job.filter,
                                first, last);
        }
    }

    unsigned count_ = 0;
    unsigned helpers_ = 0;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable work_done_;
    std::atomic<unsigned> next_stripe_{0};
    const Job* job_ = nullptr;
    uint64_t generation_ = 0;
    unsigned outstanding_ = 0;
    bool stopping_ = false;
};

RasterWorkers& workers() {
    static RasterWorkers instance;
    return instance;
}

// The whole draw: on this thread alone when there is little to do or only one worker, and shared
// out by stripes otherwise.
//
// **How big the draw is, not how big the screen is.** This used to ask whether the *framebuffer*
// was worth sharing, which at any render scale above 1 it always was — so every draw went to the
// pool, including a 5x9 glyph. These games issue draws in the thousands per frame: a level frame
// here is over a hundred, a run of text is one per letter, and waking nine threads to paint
// forty-five pixels costs orders of magnitude more than painting them. Measured with `sample` on
// an M1 Max at render scale 4: **45 % of all CPU across ten threads was `__psynch_cvwait`** —
// threads waiting for work or for each other — against 53 % actually inside `fill_triangle`.
//
// The draw's own bounding box is the right question, and its rows are also the only stripes
// worth handing out: a worker given a stripe the draw does not reach walks three edge functions
// per row to decide it has nothing to do.
void rasterise_triangles(const std::vector<Vertex>& vertices, uint32_t mode, bool textured,
                         const Texture* texture, Filter filter) {
    const unsigned rows = raster_height();
    if (vertices.empty()) {
        return;
    }
    float low_y = vertices[0].y, high_y = vertices[0].y;
    float low_x = vertices[0].x, high_x = vertices[0].x;
    for (const Vertex& vertex : vertices) {
        low_y = std::min(low_y, vertex.y);
        high_y = std::max(high_y, vertex.y);
        low_x = std::min(low_x, vertex.x);
        high_x = std::max(high_x, vertex.x);
    }
    const unsigned first_row =
        static_cast<unsigned>(std::clamp(std::floor(low_y), 0.0f, static_cast<float>(rows - 1)));
    const unsigned last_row =
        static_cast<unsigned>(std::clamp(std::ceil(high_y), 0.0f, static_cast<float>(rows - 1)));
    const unsigned covered_rows = last_row - first_row + 1;
    const uint64_t covered = static_cast<uint64_t>(std::max(0.0f, high_x - low_x)) * covered_rows;
    if (workers().count() <= 1 || covered < MIN_PIXELS_TO_SHARE ||
        covered_rows < MIN_STRIPE_ROWS * 2) {
        rasterise_triangles(vertices, mode, textured, texture, filter, first_row, last_row);
        return;
    }
    Job job;
    job.vertices = &vertices;
    job.mode = mode;
    job.textured = textured;
    job.texture = texture;
    job.filter = filter;
    job.first_row = first_row;
    job.rows = covered_rows;
    job.stripe_rows =
        std::max(MIN_STRIPE_ROWS, (covered_rows + workers().count() * STRIPES_PER_WORKER - 1) /
                                      (workers().count() * STRIPES_PER_WORKER));
    job.stripes = (covered_rows + job.stripe_rows - 1) / job.stripe_rows;
    workers().run(job);
}

// Does this quad's cell of the sheet hold a coverage *edge* — texels that are all but opaque and
// texels that are all but clear?
//
// This is the question the whole feature rests on, and it was missing. Reconstructing a glyph
// means treating the alpha channel as coverage: a letter is opaque inside, clear outside, and the
// ramp between them is the edge that gets taken back to full contrast. Run that on a sprite whose
// alpha is *translucency* rather than coverage — uniformly half-there, opaque nowhere — and the
// contrast curve does not sharpen an edge. It decides the whole sprite is outside the letter, and
// removes it.
//
// That is not hypothetical. A character's drop shadow is exactly such a sprite and is exactly the
// shape this test was looking for: two mirrored 11x13 quads, each 1:1 with its own cell. With the
// setting on, every drop shadow in the game vanished at any render scale above 1, and this is what
// had been missing.
// How opaque the inside of this cell's letter is — 0 when it holds no letter.
//
// **Not 255.** That was the assumption, and it cost this feature the smallest face in the
// collection — the one that needs it most.
//
// `fonts/maiandra-7.raw`, the 7-pixel face Cubis 2 draws its level header in, holds exactly
// *two* texels at or above 250 in the whole 84x126 sheet (`insignia-16` holds 3 041, `tribeca-11`
// 768). At that size every stroke is thinner than a pixel, so the face is antialiased into
// permanent translucency. Measured on its own `Arcade` run, cell by cell:
//
//     cell u  6-10 v 17-25  peak 170      cell u 14-17 v 35-43  peak 187
//     cell u  9-11 v 44-52  peak 153      cell u 18-21 v 35-43  peak 153
//     cell u 11-13 v 35-43  peak 136      cell u  3- 6 v 35-43  peak 153
//
// — every cell has a clear texel, none comes within sixty levels of opaque, and one failing cell
// rejected the whole run. So asking for an opaque texel asked "is this a big font".
//
// What a cell has to hold is an *edge*: somewhere at full ink for that face, and somewhere clear.
// Returning the peak rather than a yes/no is what lets the reconstruction below put the contrast
// curve's midpoint where this face's midpoint actually is (`sample_texture`, `Filter::Glyph`).
// A cell that is uniformly translucent — a drop shadow, which is what this test was written to
// protect — has no clear texel and still answers 0; and if one ever did get through, its filtered
// alpha divided by its own peak is 1 everywhere, so the curve returns it unchanged rather than
// erasing it.
uint8_t cell_coverage_peak(const Texture& texture, const std::vector<Vertex>& vertices,
                           size_t quad) {
    const auto texel = [](float edge, unsigned last) {
        return static_cast<unsigned>(std::clamp(edge, 0.0f, static_cast<float>(last)));
    };
    const unsigned left =
        texel(std::floor(extent_low_over(vertices, quad, quad + 4, &Vertex::u)), texture.width - 1);
    const unsigned right = std::max(
        left, texel(std::ceil(extent_high_over(vertices, quad, quad + 4, &Vertex::u)) - 1.0f,
                    texture.width - 1));
    const unsigned top = texel(std::floor(extent_low_over(vertices, quad, quad + 4, &Vertex::v)),
                               texture.height - 1);
    const unsigned bottom = std::max(
        top, texel(std::ceil(extent_high_over(vertices, quad, quad + 4, &Vertex::v)) - 1.0f,
                   texture.height - 1));

    uint8_t peak = 0;
    bool clear = false;
    for (unsigned y = top; y <= bottom; ++y) {
        for (unsigned x = left; x <= right; ++x) {
            const uint8_t alpha =
                texture.rgba[(static_cast<size_t>(y) * texture.width + x) * 4 + 3];
            peak = std::max(peak, alpha);
            clear = clear || alpha < COLOUR_KEY_ALPHA;
        }
    }
    return clear && peak >= GLYPH_MINIMUM_INK ? peak : 0;
}

// Is this draw a run of text, and if so how opaque is the ink? 0 = not a run.
//
// The run's peak is the largest of its cells' — one number for the draw, because a line of text
// is one face at one size and a letter that happens to be thinner than its neighbours should not
// be reconstructed against a lower midpoint than the rest of the word.
uint8_t text_run_ink(const std::vector<Vertex>& vertices, uint32_t mode, const Texture* texture) {
    if (texture == nullptr || mode != GL_QUADS || vertices.size() % 4 != 0 ||
        vertices.size() < MIN_GLYPHS_IN_A_RUN * 4) {
        return 0;
    }
    const float scale = static_cast<float>(render_scale_value());
    uint8_t ink = 0;
    for (size_t quad = 0; quad < vertices.size(); quad += 4) {
        if (!is_one_to_one_over(vertices, quad, quad + 4)) {
            return 0;
        }
        if (extent_over(vertices, quad, quad + 4, &Vertex::x) / scale > MAX_GLYPH_SIDE ||
            extent_over(vertices, quad, quad + 4, &Vertex::y) / scale > MAX_GLYPH_SIDE) {
            return 0;
        }
        const uint8_t peak = cell_coverage_peak(*texture, vertices, quad);
        if (peak == 0) {
            return 0;
        }
        ink = std::max(ink, peak);
    }
    return ink;
}

// Draw the vertices `indices` names, in that order, from the enabled arrays. `glDrawArrays`
// names them `first .. first + count`; `glDrawElements` reads them from an index array. The
// emulator's `draw_indexed` is the same split (reference/eapp-loader/lib.rs).
void rasterise_indexed(uint32_t mode, const std::vector<uint32_t>& indices) {
    const uint32_t count = static_cast<uint32_t>(indices.size());
    if (count < 3) {
        return;
    }
    const bool attribute1_is_colour =
        state().arrays[1].has_value() && state().arrays[1]->components == 4;
    const bool has_colour = attribute_active(2);
    const unsigned position_components = state().arrays[0] ? state().arrays[0]->components : 4;

    std::vector<Vertex> vertices;
    vertices.reserve(count);
    for (const uint32_t n : indices) {
        Vertex vertex{};
        if (attribute1_is_colour) {
            vertex.rgb = {attribute(1, n, 0), attribute(1, n, 1), attribute(1, n, 2)};
            vertex.alpha = attribute(1, n, 3);
        } else if (has_colour) {
            vertex.rgb = {attribute(2, n, 0), attribute(2, n, 1), attribute(2, n, 2)};
            vertex.alpha = 1.0f;
        } else {
            vertex.rgb = {1.0f, 1.0f, 1.0f};
            vertex.alpha = 1.0f;
        }
        const float w = position_components >= 4 ? attribute(0, n, 3) : 1.0f;
        const ScreenPoint p =
            project(attribute(0, n, 0), attribute(0, n, 1), attribute(0, n, 2), w);
        vertex.x = p.x;
        vertex.y = p.y;
        vertex.u = attribute(1, n, 0);
        vertex.v = attribute(1, n, 1);
        vertices.push_back(vertex);
    }

    // Whether this draw has texture coordinates of its own.
    //
    // Not "is attribute 1 enabled": this game never turns an attribute array off — it does not
    // import `glDisableVertexAttribArray` at all — so the flag says only that something, at some
    // point, used one. What it does instead is *re-point* every attribute it wants immediately
    // before every draw, which the disassembly is plain about. The textured paths
    // (0x18007e20/0x18007e48, and two more like them) point attribute 0 and then attribute 1;
    // the path that draws the letterbox bars and the dialogue panels (0x1803b714) points
    // attribute 0 and stops there. On the hardware the pipeline's own program decides what it
    // reads and a stale pointer is simply not read; here, believing the enable flag meant
    // sampling a texture through *the previous draw's* coordinates, smeared across the whole
    // quad. That is what made a flat dark bar look like a blurred copy of the scene behind it.
    // `--emulator-graphics` puts the enable flag back in its place, so the picture oracle can
    // still compare whole frames against a renderer that reads it (framework/graphics.h).
    //
    // **Which of the two readings applies is the title's to declare**, and that was learned the
    // hard way. The premise above is not "the game never disables an attribute array" — neither
    // game imports `glDisableVertexAttribArray` — it is the stronger claim that the game
    // *re-points* every attribute it wants before every draw, so that "pointed since the last
    // draw" carries the information the enable flag has lost. Lost does exactly that. Mini Golf
    // does not: it points its attributes once and then draws many times, so under this rule every
    // draw after the first read as untextured and whole screens came out blank. The enable flag
    // is the conservative reading and is the default; a title says so when it has earned the
    // other one (gfx::set_attributes_repointed_per_draw).
    const bool has_coordinates =
        state().emulator_graphics || !state().attributes_repointed_per_draw
            ? attribute_active(1)
            : state().attribute_pointed[1];
    const bool textured = has_coordinates && !attribute1_is_colour &&
                          state().textures.count(state().sampled_texture) != 0;

    // IPOD_VERTEX_HASH in the environment: one line per draw with a hash of what reached
    // the rasteriser, so two builds can be compared below the level of the call log.
    static const bool hash_draws = std::getenv("IPOD_VERTEX_HASH") != nullptr;
    if (hash_draws) {
        uint32_t hash = 2166136261u;
        auto mix = [&hash](float value) {
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            for (unsigned i = 0; i < 4; ++i) {
                hash = (hash ^ ((bits >> (8 * i)) & 0xffu)) * 16777619u;
            }
        };
        for (const Vertex& vertex : vertices) {
            for (const float value : {vertex.x, vertex.y, vertex.u, vertex.v, vertex.rgb[0],
                                      vertex.rgb[1], vertex.rgb[2], vertex.alpha}) {
                mix(value);
            }
        }
        const std::array<float, 4>& tint = state().modulate;
        std::fprintf(stderr,
                     "draw %u mode %u count %u texture %u %s pipe %u mod [%.2f %.2f %.2f %.2f] "
                     "x %.1f..%.1f y %.1f..%.1f hash %08x\n",
                     state().draws, mode, count, state().sampled_texture,
                     textured ? "textured" : "flat", state().pipeline, tint[0], tint[1], tint[2],
                     tint[3], extent_low(vertices, &Vertex::x), extent_high(vertices, &Vertex::x),
                     extent_low(vertices, &Vertex::y), extent_high(vertices, &Vertex::y), hash);
        static const char* dump_draw = std::getenv("IPOD_VERTEX_DUMP");
        if (dump_draw != nullptr && std::strtoul(dump_draw, nullptr, 10) == state().draws) {
            for (const Vertex& vertex : vertices) {
                std::fprintf(stderr, "  xy %.3f %.3f uv %.3f %.3f rgba %.3f %.3f %.3f %.3f\n",
                             vertex.x, vertex.y, vertex.u, vertex.v, vertex.rgb[0], vertex.rgb[1],
                             vertex.rgb[2], vertex.alpha);
            }
        }
    }

    // A draw whose constant colour register is all zeros contributes nothing under any reading
    // of the combiner — modulate, add or replace — so it is skipped whole rather than guessed at.
    if (state().modulate == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}) {
        ++state().draws;
        return;
    }

    // Which filter this draw's fragments read their texels with. A run of glyphs is asked about
    // first, because every quad in one is also a 1:1 blit and `Nearest` would otherwise take it:
    // that is the right answer at the game's own resolution and the wrong one above it, where a
    // glyph's edge is worth resolving rather than replicating (gfx::set_high_resolution_text).
    // The texture, looked up once for the draw rather than once per triangle. A line of dialogue
    // is 242 triangles and this is a `std::map` lookup; it also means the fatal below is raised
    // on this thread, before any worker has been handed anything. It has to come before the
    // filter, because deciding whether this is a run of glyphs means looking at the sheet.
    //
    // `.at()` would throw, and the project builds with -fno-exceptions; a draw with a texture that
    // was never uploaded is a bug in the caller, so say so rather than terminate silently.
    const Texture* texture = nullptr;
    if (textured) {
        const auto found = state().textures.find(state().sampled_texture);
        if (found == state().textures.end()) {
            fatal("draw with texture %u, which was never uploaded", state().sampled_texture);
        }
        texture = &found->second;
    }
    Filter filter = Filter::Bilinear;
    if (textured) {
        const uint8_t ink = state().high_resolution_text && !state().emulator_graphics
                                ? text_run_ink(vertices, mode, texture)
                                : 0;
        if (ink != 0) {
            state().glyph_ink = ink;
            filter = Filter::Glyph;
        } else if (is_one_to_one(vertices)) {
            filter = Filter::Nearest;
        } else if (!state().emulator_graphics && every_quad_is_one_to_one(vertices, mode)) {
            // A run of glyphs: 1:1 quad by quad, though not as a draw. See the function.
            //
            // Behind `!emulator_graphics` because the emulator asks the question of the whole
            // draw and the picture oracle compares against it pixel for pixel; this is the same
            // kind of deliberate improvement as the minified-texture box filter beside it, and
            // is off wherever the oracle is the standard.
            filter = Filter::Nearest;
        }
    }
    rasterise_triangles(vertices, mode, textured, texture, filter);
    state().anything_drawn = true;
    ++state().draws;
    // The attributes belong to the draw that pointed them, and no further.
    state().attribute_pointed.fill(false);
}

void rasterise_arrays(uint32_t mode, uint32_t first, uint32_t count) {
    if (count < 3 || count > MAX_VERTICES) {
        return;
    }
    std::vector<uint32_t> indices(count);
    for (uint32_t i = 0; i < count; ++i) {
        indices[i] = first + i;
    }
    rasterise_indexed(mode, indices);
}

// The index array of a `glDrawElements`: `count` indices of GL `type` at `pointer`. Shorts are
// the common case and the reading for a type this does not know — a wrong width would read
// neighbouring indices as garbage vertices, as the emulator's `draw_elements` notes. An index
// count above 65 536 or a null pointer is refused as the emulator refuses it.
constexpr uint32_t MAX_INDICES = 65536;

void rasterise_elements(uint32_t mode, uint32_t count, uint32_t type, uint32_t pointer) {
    if (count < 3 || count > MAX_INDICES || pointer == 0) {
        return;
    }
    std::vector<uint32_t> indices(count);
    for (uint32_t i = 0; i < count; ++i) {
        switch (type) {
        case GL_UNSIGNED_BYTE:
            indices[i] = ld8(pointer + i);
            break;
        case GL_UNSIGNED_INT:
            indices[i] = ld32(pointer + 4 * i);
            break;
        default:
            indices[i] = ld16(pointer + 2 * i);
            break;
        }
    }
    rasterise_indexed(mode, indices);
}

}  // namespace

// ---------------------------------------------------------------------------------------------

}  // namespace ipod::eapp

// ---------------------------------------------------------------------------------------------
// The graphics interface (src/framework/graphics.h). Each entry point records the framework
// ordinal the hardware knew it by — imports.json lists them — and then does the work.
// ---------------------------------------------------------------------------------------------

namespace ipod::gfx {

// The implementation lives in the file above: the rasteriser, the texture uploads and the GL
// state, all in `ipod::eapp`.
using namespace ipod::eapp;  // NOLINT(google-build-using-namespace): one file, by design

namespace {

// 16.16 fixed point, as the game computes colours and matrices in.
float from_fixed(uint32_t bits) {
    return static_cast<float>(static_cast<int32_t>(bits)) / 65536.0f;
}

// glActiveTexture names a unit as GL_TEXTURE0 + n, and the iPod had three.
constexpr uint32_t GL_TEXTURE0 = 0x84c0;
constexpr uint32_t MAX_TEXTURE_UNIT = 2;

// Which slot of the pipeline's constant bank holds the colour every vertex is multiplied by.
// Measured: Lost writes the tint to location 4 and other constants to other slots
// (reference/eapp-loader/lib.rs, `Stub::GlUniform4xScalar`).
constexpr uint32_t CONSTANT_COLOR_LOCATION = 4;

}  // namespace

const uint8_t* screen_pixels() {
    return framebuffer();
}

bool anything_drawn() {
    return state().anything_drawn;
}

unsigned screen_width() {
    return raster_width();
}

unsigned screen_height() {
    return raster_height();
}

// #4 glBindTexture(target, texture)
// #0 glActiveTexture(unit): choose which of the three units later texture calls apply to. The
// argument is GL_TEXTURE0 + n; the driver answers GL_INVALID_ENUM for anything else, which is
// what the game checks for (its own driver: `sub r0,#0x84c0 / cmp r0,#2 / strls ...`).
uint32_t set_active_texture(uint32_t unit) {
    log_call("OpenGLES", 0, {unit});
    const uint32_t index = unit - GL_TEXTURE0;
    if (index > MAX_TEXTURE_UNIT) {
        return GL_INVALID_ENUM;
    }
    state().active_texture_unit = index;
    return 0;
}

void bind_texture(TextureTarget target, uint32_t texture) {
    log_call("OpenGLES", 4, {static_cast<uint32_t>(target), texture});
    state().bound_texture = texture;
    if (state().active_texture_unit == 0) {
        state().sampled_texture = texture;
    }
}

// #35 glDisable(capability): accepted and ignored, as the hardware's driver did for everything a
// game here turns off.
void disable(uint32_t capability) {
    log_call("OpenGLES", 35, {capability});
}

// #45 glGenTextures(count, names): names are handed out in sequence from 1 and nothing is
// created until something is uploaded to one. The emulator caps a request at 256 names, which
// no game approaches; kept so the two agree if one ever does.
void gen_textures(uint32_t count, GuestAddress names) {
    log_call("OpenGLES", 45, {count, names});
    constexpr uint32_t MOST_NAMES_PER_CALL = 256;
    for (uint32_t i = 0; i < std::min(count, MOST_NAMES_PER_CALL); ++i) {
        ++state().next_texture_name;
        if (names != 0) {
            st32(names + 4 * i, state().next_texture_name);
        }
    }
}

// #12 glClear(mask): only the colour buffer exists.
void clear(Buffer buffers) {
    log_call("OpenGLES", 12, {static_cast<uint32_t>(buffers)});
    if ((static_cast<uint32_t>(buffers) & GL_COLOR_BUFFER_BIT) == 0) {
        return;
    }
    const std::array<uint8_t, 3> colour = {to_byte(state().clear_color[0]),
                                           to_byte(state().clear_color[1]),
                                           to_byte(state().clear_color[2])};
    state().anything_drawn = true;
    std::vector<uint8_t>& pixels = framebuffer_storage();
    for (size_t i = 0; i < pixels.size(); i += 3) {
        std::copy(colour.begin(), colour.end(), pixels.data() + i);
    }
}

// #13 glClearColor(r, g, b, a), each a float in an integer register.
void set_clear_color(Float32Bits red, Float32Bits green, Float32Bits blue, Float32Bits alpha) {
    log_call("OpenGLES", 13, {red, green, blue, alpha});
    const Float32Bits components[4] = {red, green, blue, alpha};
    for (unsigned i = 0; i < 4; ++i) {
        state().clear_color[i] = float_from_bits(components[i]);
    }
}

// #84 glPixelStorei(pname, param): only the alignments, only the legal values.
// The alignment is validated and then ignored, as the emulator's implementation does: both of
// this file's upload paths read tightly packed rows, which is all the game ever supplies.
uint32_t set_pixel_store(PixelStore parameter, uint32_t value) {
    const uint32_t name = static_cast<uint32_t>(parameter);
    log_call("OpenGLES", 84, {name, value});
    if (value != 1 && value != 2 && value != 4 && value != 8) {
        return GL_INVALID_VALUE;
    }
    return name == GL_UNPACK_ALIGNMENT || name == GL_PACK_ALIGNMENT ? 0 : GL_INVALID_ENUM;
}

// #101 glTexParameterf: the iPod accepted the call and ignored it.
uint32_t set_texture_parameter(TextureTarget target, TextureParameter parameter,
                               Float32Bits value) {
    log_call("OpenGLES", 101,
             {static_cast<uint32_t>(target), static_cast<uint32_t>(parameter), value});
    return 0;
}

// #53 glGetError: nothing here ever fails.
uint32_t error() {
    log_call("OpenGLES", 53, {});
    return 0;
}

// #99 glTexImage2D(target, level, internalformat, width, height, border, format, type, data)
void texture_image(TextureTarget target, uint32_t level, PixelFormat internal_format,
                   uint32_t width, uint32_t height, uint32_t border, PixelFormat format,
                   PixelType type, GuestAddress pixels) {
    log_call("OpenGLES", 99,
             {static_cast<uint32_t>(target), level, static_cast<uint32_t>(internal_format), width,
              height, border, static_cast<uint32_t>(format), static_cast<uint32_t>(type)});
    upload_plain(width, height, static_cast<uint32_t>(format), static_cast<uint32_t>(type), pixels);
}

// #19 glCompressedTexImage2D(target, level, format, width, height, border, size, data)
void compressed_texture_image(TextureTarget target, uint32_t level, PixelFormat format,
                              uint32_t width, uint32_t height, uint32_t border, uint32_t size,
                              GuestAddress data) {
    log_call("OpenGLES", 19,
             {static_cast<uint32_t>(target), level, static_cast<uint32_t>(format), width, height,
              border, size, data});
    upload_paletted(width, height, data, static_cast<uint32_t>(format));
}

// #105 glTexSubImage2D(target, level, x, y, width, height, format, type, pixels).
//
// The patch is decoded by the whole-texture upload into a scratch name and then copied into the
// bound texture, so every pixel format works here without a second decoder — the emulator does
// the same (reference/eapp-loader/lib.rs, `upload_sub`). Texas Hold'em uses it once, to put the
// table's felt into a full-screen texture.
void texture_sub_image(TextureTarget target, uint32_t level, uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height, PixelFormat format, PixelType type,
                       GuestAddress pixels) {
    log_call("OpenGLES", 105,
             {static_cast<uint32_t>(target), level, x, y, width, height,
              static_cast<uint32_t>(format), static_cast<uint32_t>(type)});
    const auto destination = state().textures.find(state().bound_texture);
    if (destination == state().textures.end() || width == 0 || height == 0) {
        return;
    }
    constexpr uint32_t SCRATCH_NAME = 0xffffffffu;
    const uint32_t bound = state().bound_texture;
    state().bound_texture = SCRATCH_NAME;
    upload_plain(width, height, static_cast<uint32_t>(format), static_cast<uint32_t>(type), pixels);
    state().bound_texture = bound;
    const auto patch = state().textures.find(SCRATCH_NAME);
    if (patch == state().textures.end()) {
        return;
    }
    Texture& into = destination->second;
    for (unsigned row = 0; row < height && y + row < into.height; ++row) {
        for (unsigned column = 0; column < width && x + column < into.width; ++column) {
            const size_t from = (static_cast<size_t>(row) * width + column) * 4;
            const size_t to = (static_cast<size_t>(y + row) * into.width + (x + column)) * 4;
            std::copy_n(patch->second.rgba.begin() + static_cast<std::ptrdiff_t>(from), 4,
                        into.rgba.begin() + static_cast<std::ptrdiff_t>(to));
        }
    }
    state().textures.erase(patch);
}

// #21 glCopyTexImage2D(target, level, format, x, y, width, height, border)
void copy_texture_image(TextureTarget target, uint32_t level, PixelFormat format, int32_t x,
                        int32_t y, uint32_t width, uint32_t height, uint32_t border) {
    log_call("OpenGLES", 21,
             {static_cast<uint32_t>(target), level, static_cast<uint32_t>(format),
              static_cast<uint32_t>(x), static_cast<uint32_t>(y), width, height, border});
    copy_framebuffer_to_texture(x, y, width, height);
}

// #137 glVertexAttribPointer(index, size, type, normalized, stride, pointer)
void set_vertex_array(uint32_t index, uint32_t components, AttributeType type, uint32_t normalized,
                      uint32_t stride, GuestAddress data) {
    log_call("OpenGLES", 137,
             {index, components, static_cast<uint32_t>(type), normalized, stride, data});
    if (index < gles::ATTRIBUTE_COUNT) {
        state().arrays[index] = VertexArray{components, static_cast<uint32_t>(type), stride, data};
        // Pointing an attribute is how this game says the next draw wants it (see the note in
        // `rasterise_arrays`).
        //
        // Pointing attribute *0* additionally starts a new draw's set. Every path in this game
        // points the position first and then whatever else that draw needs — the three textured
        // paths point 0 and then 1, the flat path points 0 alone — so anything still marked when
        // a position arrives was left by an earlier setup, and a setup that did not end in a
        // draw is the only way that happens.
        //
        // Clearing the rest here is what stops that leftover being read as part of this draw. It
        // was not clearing it that put texture coordinates on the letterbox bar: with a stale
        // attribute 1 the bar sampled the atlas through whatever quad had been prepared and
        // abandoned before it, so the bar came out smeared with the scene, or keyed away to
        // nothing at all. Measured from a saved game at the beach camp, it happened on 385 of
        // 923 of that bar's draws — which is why it read as random, and why it changed as the
        // player walked: what is prepared and abandoned depends on what is on screen.
        if (index == 0) {
            state().attribute_pointed.fill(false);
        }
        state().attribute_pointed[index] = true;
    }
}

// #40 glEnableVertexAttribArray(index) / #36 glDisableVertexAttribArray(index)
void enable_vertex_array(uint32_t index) {
    log_call("OpenGLES", 40, {index});
    if (index < gles::ATTRIBUTE_COUNT) {
        state().attribute_enabled[index] = true;
    }
}

void disable_vertex_array(uint32_t index) {
    log_call("OpenGLES", 36, {index});
    if (index < gles::ATTRIBUTE_COUNT) {
        state().attribute_enabled[index] = false;
    }
}

// #37 glDrawArrays(mode, first, count)
void draw_arrays(Primitive primitive, uint32_t first, uint32_t count) {
    log_call("OpenGLES", 37, {static_cast<uint32_t>(primitive), first, count});
    rasterise_arrays(static_cast<uint32_t>(primitive), first, count);
}

// #38 glDrawElements(mode, count, type, indices): the same draw, the vertices chosen by an index
// array. The Sims Bowling draws four of the five quads in a menu frame this way (its PLAN.md,
// difference 4); the emulator implements it as `Stub::GlDrawElements` (reference/eapp-loader/
// lib.rs, `draw_elements`), and the reversing table names the ordinal.
void draw_elements(Primitive primitive, uint32_t count, IndexType type, GuestAddress indices) {
    log_call("OpenGLES", 38,
             {static_cast<uint32_t>(primitive), count, static_cast<uint32_t>(type), indices});
    rasterise_elements(static_cast<uint32_t>(primitive), count, static_cast<uint32_t>(type),
                       indices);
}

// #165 load the identity into the matrix at `matrix`.
void matrix_identity(GuestAddress matrix) {
    log_call("OpenGLES", 165, {matrix});
    if (matrix != 0) {
        write_matrix(matrix, identity());
    }
}

// #167 glOrtho into the matrix at `matrix`.
void matrix_ortho(GuestAddress matrix, Float32Bits left, Float32Bits right, Float32Bits bottom,
                  Float32Bits top, Float32Bits near_plane, Float32Bits far_plane) {
    log_call("OpenGLES", 167, {matrix, left, right, bottom, top, near_plane, far_plane});
    const float l = float_from_bits(left), r = float_from_bits(right);
    const float b = float_from_bits(bottom), t = float_from_bits(top);
    const float n = float_from_bits(near_plane), f = float_from_bits(far_plane);
    if (matrix == 0 || r == l || t == b || f == n) {
        return;
    }
    Matrix o{};
    o[0] = 2.0f / (r - l);
    o[5] = 2.0f / (t - b);
    o[10] = -2.0f / (f - n);
    o[12] = -(r + l) / (r - l);
    o[13] = -(t + b) / (t - b);
    o[14] = -(f + n) / (f - n);
    o[15] = 1.0f;
    write_matrix(matrix, o);
    if (o[5] < 0.0f) {
        state().projection_flips_y = true;
    }
}

// #175 destination = a x b, all float matrices the game owns.
void matrix_multiply(GuestAddress destination, GuestAddress a, GuestAddress b) {
    log_call("OpenGLES", 175, {destination, a, b});
    if (destination != 0 && a != 0 && b != 0) {
        write_matrix(destination, multiply(read_matrix(a), read_matrix(b)));
    }
}

// #169 matrix = matrix x translate(x, y, z), in place, column-major floats.
void matrix_translate(GuestAddress matrix, Float32Bits x, Float32Bits y, Float32Bits z) {
    log_call("OpenGLES", 169, {matrix, x, y, z});
    if (matrix == 0) {
        return;
    }
    Matrix t = identity();
    t[12] = float_from_bits(x);
    t[13] = float_from_bits(y);
    t[14] = float_from_bits(z);
    write_matrix(matrix, multiply(read_matrix(matrix), t));
}

// #171 matrix = matrix x scale(x, y, z), in place.
void matrix_scale(GuestAddress matrix, Float32Bits x, Float32Bits y, Float32Bits z) {
    log_call("OpenGLES", 171, {matrix, x, y, z});
    if (matrix == 0) {
        return;
    }
    Matrix s = identity();
    s[0] = float_from_bits(x);
    s[5] = float_from_bits(y);
    s[10] = float_from_bits(z);
    write_matrix(matrix, multiply(read_matrix(matrix), s));
}

// #173 matrix = matrix x rotate(angle, axis), the axis normalised first; degrees, as glRotatef
// takes them. The rotation matrix is the one the OpenGL specification gives for glRotate.
void matrix_rotate(GuestAddress matrix, Float32Bits angle_degrees, Float32Bits x, Float32Bits y,
                   Float32Bits z) {
    log_call("OpenGLES", 173, {matrix, angle_degrees, x, y, z});
    if (matrix == 0) {
        return;
    }
    constexpr float DEGREES_TO_RADIANS = 3.14159265358979323846f / 180.0f;
    const float angle = float_from_bits(angle_degrees) * DEGREES_TO_RADIANS;
    float ax = float_from_bits(x), ay = float_from_bits(y), az = float_from_bits(z);
    const float length = std::sqrt(ax * ax + ay * ay + az * az);
    if (length > 0.0f) {
        ax /= length;
        ay /= length;
        az /= length;
    }
    const float s = std::sin(angle), c = std::cos(angle), ic = 1.0f - c;
    Matrix r = identity();
    r[0] = ax * ax * ic + c;
    r[1] = ay * ax * ic + az * s;
    r[2] = ax * az * ic - ay * s;
    r[4] = ax * ay * ic - az * s;
    r[5] = ay * ay * ic + c;
    r[6] = ay * az * ic + ax * s;
    r[8] = ax * az * ic + ay * s;
    r[9] = ay * az * ic - ax * s;
    r[10] = az * az * ic + c;
    write_matrix(matrix, multiply(read_matrix(matrix), r));
}

// #125 glUniformMatrix4fv(location, count, transpose, value): location 0 is the matrix the game
// draws with. A negative Y scale in any matrix supplied marks the projection as Y-flipping.
void set_matrix_uniform(uint32_t location, uint32_t count, uint32_t transpose,
                        GuestAddress matrix) {
    log_call("OpenGLES", 125, {location, count, transpose, matrix});
    if (matrix == 0) {
        return;
    }
    if (guest_float(matrix + 5 * 4) < 0.0f) {
        state().projection_flips_y = true;
    }
    if (location == 0) {
        state().model_view_projection = read_matrix(matrix);
    }
}

// #149 glUniformMatrix4xvAPPLE(location, count, transpose, value): the 16.16 twin of #125, and
// the only matrix Lost ever uploads. Same rule for the Y direction — a negative Y scale in the
// projection marks the screen as upside down for the rasteriser.
void set_matrix_uniform_fixed(uint32_t location, uint32_t count, uint32_t transpose,
                              GuestAddress matrix) {
    log_call("OpenGLES", 149, {location, count, transpose, matrix});
    if (matrix == 0 || location != 0) {
        return;
    }
    gles::Matrix values{};
    for (unsigned i = 0; i < values.size(); ++i) {
        values[i] = from_fixed(ld32(matrix + 4 * i));
    }
    if (values[5] < 0.0f) {
        state().projection_flips_y = true;
    }
    state().model_view_projection = values;
}

// #147 glUniform4xAPPLE(location, x, y, z, w): the scalar form of #148, four 16.16 components in
// separate arguments — the fifth of which travels on the stack. Location 4 is the constant
// colour every drawn vertex is multiplied by; the same bank holds other pipeline constants,
// which are accepted and ignored, as the hardware's own driver did with the ones it did not use.
void set_constant_color(uint32_t location, Fixed16Bits red, Fixed16Bits green, Fixed16Bits blue,
                        Fixed16Bits alpha) {
    state().constant_colour_set = true;
    log_call("OpenGLES", 147, {location, red, green, blue, alpha});
    if (location == CONSTANT_COLOR_LOCATION) {
        state().modulate = {from_fixed(red), from_fixed(green), from_fixed(blue),
                            from_fixed(alpha)};
    }
}

// #148 glUniform4xvAPPLE(location, count, values): the vector form of #147 — four 16.16
// components in guest memory. `location` -1 is a documented no-op, and a count of zero or a null
// pointer sets nothing; the register the colour lands in is the same one.
void set_constant_color_vector(uint32_t location, uint32_t count, GuestAddress values) {
    log_call("OpenGLES", 148, {location, count, values});
    if (static_cast<int32_t>(location) < 0 || values == 0 || count == 0) {
        return;
    }
    state().constant_colour_set = true;
    if (location == CONSTANT_COLOR_LOCATION) {
        state().modulate = {from_fixed(ld32(values)), from_fixed(ld32(values + 4)),
                            from_fixed(ld32(values + 8)), from_fixed(ld32(values + 12))};
    }
}

// --- the render server ------------------------------------------------------------------------
//
// Nothing here models the GPU firmware: the rasteriser below *is* the renderer, and what the
// game needs from these three is to be told that starting one worked. See the note in
// framework/graphics.h for what happens when they are not answered.

uint32_t start_render_server(uint32_t reserved, GuestAddress out_first, GuestAddress out_second) {
    log_call("OpenGLES", 152, {reserved, out_first, out_second});
    // Apple's implementation writes these two constants; the game keeps them and hands them back.
    if (out_first != 0) {
        st32(out_first, 1);
    }
    if (out_second != 0) {
        st32(out_second, 2);
    }
    return 1;
}

uint32_t stop_render_server(uint32_t reserved) {
    log_call("OpenGLES", 153, {reserved});
    return 1;
}

uint32_t set_render_server_image(uint32_t slot, GuestAddress image, uint32_t bytes) {
    log_call("OpenGLES", 164, {slot, image, bytes});
    return 1;
}

// #157 the frame is complete. The frame pump presents the screen after the frame vector
// returns, so nothing is copied here.
void swap_buffers() {
    log_call("OpenGLES", 157, {0, 0});
}

// #158 a capability word the game masks, and #159 selecting the built-in pipeline, which the
// hardware reported as succeeding.
uint32_t pipeline_capabilities(uint32_t query, uint32_t argument) {
    log_call("OpenGLES", 158, {query, argument});
    return 0x3000;
}

void set_attributes_repointed_per_draw(bool repointed) {
    state().attributes_repointed_per_draw = repointed;
}

void set_emulator_graphics(bool as_the_emulator_does) {
    state().emulator_graphics = as_the_emulator_does;
    if (as_the_emulator_does) {
        // The picture oracle compares whole frames against a renderer that draws 320x240 and
        // reads a 1:1 blit as a 1:1 blit. Neither of this project's own two answers has anything
        // to compare against there, so asking for the emulator's graphics takes both away.
        set_render_scale(1);
        set_high_resolution_text(false);
    }
}

void set_render_scale(unsigned scale) {
    const unsigned wanted = std::clamp(scale, MIN_RENDER_SCALE, MAX_RENDER_SCALE);
    if (wanted == render_scale_value()) {
        return;
    }
    // What was on screen survives the resize, resampled to the new size.
    //
    // This used to paint the buffer magenta, on the reasoning that the frame after a resize
    // has drawn nothing yet and an uncovered region should say so. That reasoning assumes a
    // game that redraws its whole screen every frame, and The Sims Bowling does not: it clears
    // once at boot (`clears=1` in every recording) and each frame redraws only what animates —
    // five draws at its main menu, all in the highlight row — so a resize turned everything
    // else magenta and it stayed that way until the next screen change. A nearest-neighbour
    // copy of the old picture is what a display scaler would show: soft at the new size until
    // the game redraws each part, and the game does redraw as the player moves on. At start-up
    // the old picture is the magenta the buffer began with, so an undrawn region still says so.
    const unsigned old_width = raster_width(), old_height = raster_height();
    const std::vector<uint8_t> old_pixels = framebuffer_storage();
    render_scale_value() = wanted;
    std::vector<uint8_t>& pixels = framebuffer_storage();
    const unsigned width = raster_width(), height = raster_height();
    pixels.assign(static_cast<size_t>(width) * height * 3, 0);
    for (unsigned y = 0; y < height; ++y) {
        const size_t source_row = static_cast<size_t>(y) * old_height / height * old_width;
        uint8_t* row = pixels.data() + static_cast<size_t>(y) * width * 3;
        for (unsigned x = 0; x < width; ++x) {
            const size_t source = (source_row + static_cast<size_t>(x) * old_width / width) * 3;
            row[x * 3] = old_pixels[source];
            row[x * 3 + 1] = old_pixels[source + 1];
            row[x * 3 + 2] = old_pixels[source + 2];
        }
    }
}

unsigned render_scale() {
    return render_scale_value();
}

void set_high_resolution_text(bool resolve_glyphs_at_raster_resolution) {
    state().high_resolution_text = resolve_glyphs_at_raster_resolution;
}

unsigned set_render_threads(unsigned threads) {
    return workers().set_count(threads);
}

unsigned render_threads() {
    return workers().count();
}

uint32_t select_pipeline(Pipeline pipeline) {
    log_call("OpenGLES", 159, {static_cast<uint32_t>(pipeline)});
    // Recorded for the draw log rather than acted on: which of the fifty built-in pipelines is
    // selected decides what the fragment combiner does with the constant colour, and that
    // program lives in the render server's own firmware (`rserver.bin`), not here.
    state().pipeline = static_cast<uint32_t>(pipeline);
    return 1;
}

}  // namespace ipod::gfx
