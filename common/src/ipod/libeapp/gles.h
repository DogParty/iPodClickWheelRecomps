// The OpenGLES framework: a software rasteriser for the iPod's 320×240 screen.
//
// The framework has 179 entries; 26 are implemented here and a silent boot of the game reaches
// 16 of those — texture upload and binding, vertex attribute arrays, the constant colour every
// quad is tinted by, one 16.16 matrix upload, the render server's own lifecycle, and
// glDrawArrays. (Ten of the 26 are the float matrix path Mini Golf used and this title does not
// call; they are kept because they cost nothing and the next title may.)
// This file holds the state those entries share; gles.cpp implements them. The
// pipeline is a port of the emulator's (reference/eapp-loader/lib.rs, `draw_indexed`,
// `fill_triangle`, `project`, `attr`, `upload_*`), which was built up draw call by draw call
// against the real titles — its quirks (the colour-key threshold of 8, the Y flip decided by the
// projection matrix, the half-texel sampling offset) are measured behaviour, not choices.
//
// Public entry points are declared in include/ipod_eapp.h; this header is for the GL state the
// implementation file and tests share.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace ipod::eapp::gles {

struct Texture {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, top row first as uploaded
    // An `GL_ALPHA` upload has no colour of its own — its RGB is zero by construction. GL's
    // texture environment leaves `Cv = Cp` for a one-component alpha texture, so such a texture
    // contributes *coverage* and the fragment keeps the colour it arrived with. Sampling its RGB
    // instead paints black.
    bool alpha_only = false;
    // Wider: the texture carries no COLOUR of its own — `GL_ALPHA`, `GL_LUMINANCE` or
    // `GL_LUMINANCE_ALPHA`, whose RGB is either nothing at all or a single grey ramp. Whatever
    // colour such a draw ends up in has to come from the vertex colour array or, when there is
    // none, from the constant colour register, and this is what decides that (`fill_triangle`).
    //
    // Cubis 2 is the measurement: every cube on its board is one `GL_LUMINANCE_ALPHA` sheet of
    // grey cube shapes drawn with the cube's colour in the register, and a rule that stopped at
    // `GL_ALPHA` rendered the whole board grey. In the same frames it draws a `GL_RGBA` logo
    // with a stale `(0,1,0,1)` left in the register, so widening to the *colour* formats too
    // would turn that green — which is the failure that made the register default-off in the
    // first place (LOST's monochrome UI, The Sims Bowling's black alley). The format is the
    // line. See `../../../Cubis2/PLAN.md` difference 6 and `reference/eapp-loader/lib.rs`
    // (`Texture::colourless`), which this mirrors exactly, because the picture oracle compares
    // the two renderers pixel for pixel.
    bool colourless = false;
};

// One vertex attribute array as set by glVertexAttribPointer: `components` per vertex of GL
// `type`, `stride` bytes apart (0 = tightly packed), starting at guest address `pointer`.
struct VertexArray {
    unsigned components = 0;
    uint32_t type = 0;
    unsigned stride = 0;
    uint32_t pointer = 0;
};

using Matrix = std::array<float, 16>;  // column-major, as OpenGL and the game store it

constexpr unsigned ATTRIBUTE_COUNT = 8;

struct State {
    std::map<uint32_t, Texture> textures;
    // Two bindings, because the iPod exposed three texture units and Lost uses two of them.
    // `bound_texture` is simply the last one bound, on whichever unit, and is what an upload
    // fills. `sampled_texture` is unit 0's, and is the one a draw reads: this pipeline has a
    // single sampler, so a bind to unit 1 must not quietly become the texture the next draw
    // uses. Getting that wrong is a rendering fault with no symptom in the call log.
    uint32_t bound_texture = 0;
    // The last name `gen_textures` handed out. The driver's counter starts at 1, so 0 is never
    // issued and stays meaningful as "unbound" (reference/eapp-loader/lib.rs, GlGenTextures).
    uint32_t next_texture_name = 0;
    uint32_t sampled_texture = 0;
    uint32_t active_texture_unit = 0;

    std::array<std::optional<VertexArray>, ATTRIBUTE_COUNT> arrays;
    std::array<bool, ATTRIBUTE_COUNT> attribute_enabled{};
    // Which attributes were pointed at an array *since the last draw*, which is what says they
    // belong to this one. The enable flags cannot: this game never disables an attribute array —
    // it does not even import `glDisableVertexAttribArray` — so once an attribute is on it stays
    // on for the rest of the run. See `rasterise_arrays`.
    std::array<bool, ATTRIBUTE_COUNT> attribute_pointed{};

    std::array<float, 4> clear_color{};
    std::optional<Matrix> model_view_projection;  // set through #125 when the location is 0
    bool projection_flips_y = false;              // ortho with a negative Y scale was seen
    std::array<float, 4> modulate{1.0f, 1.0f, 1.0f, 1.0f};
    // Whether the game has ever put anything in the constant colour register. A title that never
    // calls `#147` has no constant colour for an untextured draw to be painted in, and its flat
    // draws carry their colour in the vertex array instead — see `fill_triangle`.
    bool constant_colour_set = false;

    uint32_t pipeline = 0;              // the built-in pipeline #159 last selected
    bool emulator_graphics = false;     // see gfx::set_emulator_graphics
    // Whether this title re-points every vertex attribute before every draw, which decides
    // how a draw's attributes are recognised. See `rasterise_arrays`.
    bool attributes_repointed_per_draw = false;
    bool high_resolution_text = false;  // see gfx::set_high_resolution_text
    // How opaque the ink of the run of text being drawn is, 0..255 — the largest alpha in any of
    // its cells. Set per draw, read by `Filter::Glyph`'s edge reconstruction, which measures a
    // filtered texel against *this* rather than against 255 so that a face too small to be
    // opaque anywhere is still reconstructed. See `cell_coverage_peak` in gles.cpp.
    uint8_t glyph_ink = 255;
    unsigned draws = 0;                 // numbers the lines IPOD_VERTEX_HASH prints
    // Whether the game has put anything in the framebuffer yet — a clear or a draw. See
    // gfx::anything_drawn.
    bool anything_drawn = false;
};

State& state();

}  // namespace ipod::eapp::gles
