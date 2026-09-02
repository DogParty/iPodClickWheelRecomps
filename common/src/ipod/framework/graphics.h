// The graphics the game draws with: a fixed-function pipeline over a 320x240 screen.
//
// This is OpenGL ES 1.x as the iPod exposed it, reduced to the entries Lost uses — texture
// upload and binding, two vertex attribute arrays, the constant colour every quad is tinted by,
// one 16.16 matrix upload, the render server's own lifecycle, and glDrawArrays.
// `src/libeapp/gles.cpp` implements it as a software rasteriser.
//
// Vertex data, texture pixels and matrices are named by `GuestAddress`: the game builds them in
// its own memory and the platform reads them from there.
#pragma once

#include "ipod/framework/types.h"

#include <cstdint>

namespace ipod::gfx {

// --- what the game names when it draws -------------------------------------------------------
// The values are the GL enumerants the hardware took; each is here because the game passes it.

// The one texture unit the iPod exposed. (Not GL's own GL_TEXTURE_2D — the firmware used its
// own number, which is what the game passes.)
enum class TextureTarget : uint32_t { Texture2D = 0x84f5 };

// How vertices are grouped. The game's own name for 7 was "triangle fan"; the pipeline reads it
// as quads — four vertices each — which is how the game builds its arrays.
enum class Primitive : uint32_t { TriangleStrip = 5, Quads = 7 };

// The component type of a vertex attribute. The game builds everything in 16.16 fixed point.
enum class AttributeType : uint32_t { Fixed = 0x140c, Float = 0x1406 };

// The element type of a `glDrawElements` index array.
enum class IndexType : uint32_t { UnsignedByte = 0x1401, UnsignedShort = 0x1403, UnsignedInt = 0x1405 };

// Texture formats: the plain one the game uploads screen captures in, and the palettised one its
// own sprite sheets are compressed with.
enum class PixelFormat : uint32_t { Rgba = 0x1908, Palette8Rgba8 = 0x8b96 };
enum class PixelType : uint32_t { UnsignedByte = 0x1401 };

// Which buffer `clear` clears. Only the colour buffer exists.
enum class Buffer : uint32_t { Color = 0x4000 };

// Row alignment of pixels read back from the screen.
enum class PixelStore : uint32_t { PackAlignment = 0xd05 };

// A texture parameter the hardware accepted and ignored.
enum class TextureParameter : uint32_t { Priority = 0x8066 };

// The fixed-function pipelines the firmware offered, by what they draw.
enum class Pipeline : uint32_t { Flat = 3, Ground = 0x1e, Textured = 0x27, TexturedSecond = 0x28 };

// The screen the game believes it is drawing into: 320x240, which is what every coordinate it
// computes is in and what every texture it captures off the screen is measured in. Nothing the
// game says is ever in any other unit.
constexpr unsigned SCREEN_WIDTH = 320;
constexpr unsigned SCREEN_HEIGHT = 240;

// The screen the pipeline actually draws into: packed 24-bit RGB, top row first, and
// `render_scale()` times the size above in each direction. At scale 1 the two are the same
// picture and this project has no second one.
[[nodiscard]] const uint8_t* screen_pixels();

// Has the game put anything in the framebuffer yet — a clear, or a draw?
//
// It has not on the very first frame: the firmware calls the game once to say "you are now
// running" and it draws nothing in reply. The buffer at that moment is still the magenta it is
// filled with so that an *un-drawn region* is unmistakable in a screenshot, and handing that to
// the window makes the player's first sight of the game a magenta screen — held there for as long
// as the window takes to appear, which is what made it look like a deliberate splash. A frame
// pump asks this and does not present until the answer is yes. The magenta stays where it is
// useful, which is in the screenshots.
[[nodiscard]] bool anything_drawn();
[[nodiscard]] unsigned screen_width();
[[nodiscard]] unsigned screen_height();

// A value in the 16.16 fixed point the game computes geometry and colours in. It reaches the
// platform as bits for the same reason a `Float32Bits` does: it is a word in a register.
using Fixed16Bits = uint32_t;

// --- the render server -----------------------------------------------------------------------
//
// Lost does not draw straight into a fixed-function pipeline the way Mini Golf did. The iPod's
// GPU ran a small firmware — the game ships it as `rserver.bin` — which the game starts,
// hands its image to, and selects a built-in pipeline from before it issues a single draw.
// All three answer 1 for success, and 0 — what an unimplemented import returns — for failure.
// Being told "no renderer" every frame is what once left Lost in a present-only loop that never
// drew anything (reference/eapp-loader/lib.rs, `Stub::GlStartRenderServer`).

// Start the driver and reset the context. The two out-parameters receive the constants 1 and 2,
// which is what Apple's implementation wrote there. Returns 1 when the server started.
[[nodiscard]] uint32_t start_render_server(uint32_t reserved, GuestAddress out_first,
                                           GuestAddress out_second);

// Stop the driver. Returns 1.
[[nodiscard]] uint32_t stop_render_server(uint32_t reserved);

// Give the server its firmware image: `bytes` of it at `image`. Returns 1. The size comes from
// the file load that read `rserver.bin`, which is why an open must report its byte count and not
// its handle (see src/libeapp/async_file.cpp).
[[nodiscard]] uint32_t set_render_server_image(uint32_t slot, GuestAddress image, uint32_t bytes);

// --- state ---------------------------------------------------------------------------------

// Choose which of the three texture units later texture calls apply to; `unit` is named as
// GL_TEXTURE0 + n. Answers a GL error code, which is how the game finds out it named one that
// does not exist. Only unit 0 is sampled — see the note on `sampled_texture` in libeapp/gles.h.
[[nodiscard]] uint32_t set_active_texture(uint32_t unit);

// Make `texture` the one later draws sample, on `target`. Texture 0 is "no texture".
void bind_texture(TextureTarget target, uint32_t texture);

// Fill the screen with the clear colour (only the colour buffer exists).
void clear(Buffer buffers);
void set_clear_color(Float32Bits red, Float32Bits green, Float32Bits blue, Float32Bits alpha);

// Byte alignment of rows in uploaded and read-back pixels. Answers a GL error code, which the
// game checks against zero.
[[nodiscard]] uint32_t set_pixel_store(PixelStore parameter, uint32_t value);

// A texture parameter the iPod accepted and ignored (the game sets a priority per texture).
[[nodiscard]] uint32_t set_texture_parameter(TextureTarget target, TextureParameter parameter,
                                             Float32Bits value);

// The error since the last call. The game asks after almost every call it makes and never looks
// at the answer — a habit of the original's code — so this is deliberately not [[nodiscard]].
uint32_t error();

// --- textures ------------------------------------------------------------------------------

// Upload `pixels` as the bound texture's image.
void texture_image(TextureTarget target, uint32_t level, PixelFormat internal_format,
                   uint32_t width, uint32_t height, uint32_t border, PixelFormat format,
                   PixelType type, GuestAddress pixels);

// Upload a palettised image (the game's own sprite format) as the bound texture.
void compressed_texture_image(TextureTarget target, uint32_t level, PixelFormat format,
                              uint32_t width, uint32_t height, uint32_t border, uint32_t size,
                              GuestAddress data);

// Copy a rectangle of the screen into the bound texture — how the hole captures its ground.
// glTexSubImage2D(target, level, x, y, width, height, format, type, pixels): replace one
// rectangle of the bound texture. The patch is decoded exactly as a whole upload would be.
void texture_sub_image(TextureTarget target, uint32_t level, uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height, PixelFormat format, PixelType type,
                       GuestAddress pixels);

void copy_texture_image(TextureTarget target, uint32_t level, PixelFormat format, int32_t x,
                        int32_t y, uint32_t width, uint32_t height, uint32_t border);

// --- geometry ------------------------------------------------------------------------------

// Where attribute `index` reads from: `components` values of GL `type` per vertex, `stride`
// bytes apart (0 = tightly packed), starting at `data`.
void set_vertex_array(uint32_t index, uint32_t components, AttributeType type, uint32_t normalized,
                      uint32_t stride, GuestAddress data);
void enable_vertex_array(uint32_t index);
void disable_vertex_array(uint32_t index);

// Draw `count` vertices from the enabled arrays, starting at `first`.
void draw_arrays(Primitive primitive, uint32_t first, uint32_t count);

// Draw `count` vertices from the enabled arrays, chosen by the index array of `type` at `indices`.
void draw_elements(Primitive primitive, uint32_t count, IndexType type, GuestAddress indices);

// glDisable(capability). The hardware's driver accepted it and nothing here depends on any
// capability it names; the call is recorded for the oracle and otherwise ignored, as the
// emulator ignores it.
void disable(uint32_t capability);

// glGenTextures(count, names): hand out `count` texture names into the array at `names`,
// creating nothing. Names are issued in sequence from 1; an upload to a name creates the texture.
void gen_textures(uint32_t count, GuestAddress names);

// --- matrices ------------------------------------------------------------------------------
// Each takes the address of a 4x4 column-major float matrix the game owns.

void matrix_identity(GuestAddress matrix);
void matrix_ortho(GuestAddress matrix, Float32Bits left, Float32Bits right, Float32Bits bottom,
                  Float32Bits top, Float32Bits near_plane, Float32Bits far_plane);
void matrix_multiply(GuestAddress destination, GuestAddress a, GuestAddress b);
// `matrix = matrix x translate(x, y, z)` and `matrix = matrix x rotate(angle, axis)`, the
// driver's mat4 helpers (#169 and #173), with the arguments as floats in integer registers.
void matrix_translate(GuestAddress matrix, Float32Bits x, Float32Bits y, Float32Bits z);
// `matrix = matrix x scale(x, y, z)` (#171), the third of the helpers.
void matrix_scale(GuestAddress matrix, Float32Bits x, Float32Bits y, Float32Bits z);
void matrix_rotate(GuestAddress matrix, Float32Bits angle_degrees, Float32Bits x, Float32Bits y,
                   Float32Bits z);

// Hand the pipeline the matrix draws are transformed by. `location` 0 is the one the game uses.
void set_matrix_uniform(uint32_t location, uint32_t count, uint32_t transpose, GuestAddress matrix);

// The same, for a matrix in 16.16 fixed point. This is Lost's *only* matrix path: it has eleven
// call sites and never calls the float form above, nor any of the mat4 helpers.
void set_matrix_uniform_fixed(uint32_t location, uint32_t count, uint32_t transpose,
                              GuestAddress matrix);

// The constant colour every vertex is multiplied by, in 16.16 fixed point, one component per
// argument. Lost sets it once per block of draws; leaving it out paints every tinted quad white.
// `location` 4 is the colour — the same bank holds other constants the pipeline reads.
void set_constant_color(uint32_t location, Fixed16Bits red, Fixed16Bits green, Fixed16Bits blue,
                        Fixed16Bits alpha);

// The same constant, as glUniform4xvAPPLE(location, count, values) gives it: four 16.16
// components in guest memory at `values`. Texas Hold'em's form — it sets the colour this way
// before nearly every draw and never calls the scalar form.
void set_constant_color_vector(uint32_t location, uint32_t count, GuestAddress values);

// --- the frame -----------------------------------------------------------------------------

// The frame is complete. The frame pump presents the screen after the frame vector returns.
void swap_buffers();

// Decide what a draw reads the way the emulator does: from the attribute *enable* flags, and
// with no colour of its own when there is no texture. This project reads the attributes the draw
// actually pointed and paints an untextured draw in the constant colour (see `rasterise_arrays`
// in libeapp/gles.cpp) — the one place the two renderers deliberately differ. The picture oracle
// turns this on so it can still compare whole frames rather than being blinded to the third of
// every one of them where they disagree.
void set_emulator_graphics(bool as_the_emulator_does);

// How this title drives the vertex attribute arrays, which decides how a draw's attributes are
// recognised — and there is no reading that is right for every title.
//
// A game that *re-points* every attribute it wants immediately before every draw has told the
// driver, by doing so, which attributes that draw reads; the enable flags say only that something
// once used one, and are useless if the game never disables an array. Pass true for such a game.
//
// A game that points its attributes once and then draws many times has said no such thing, and
// its enable flags are the only information there is. That is the default, because it is the
// reading that cannot invent a missing texture: under the other one, every draw after the first
// reads as untextured and the screen goes blank.
void set_attributes_repointed_per_draw(bool repointed);

// --- what this renderer can do that the iPod's could not -------------------------------------
//
// Both of these are off by default and both are refused while `set_emulator_graphics` is on:
// the picture oracle compares whole 320x240 frames against the emulator, and a renderer drawing
// a different number of pixels has nothing to compare.

// Draw at `scale` times 320x240 and hand the platform the larger picture. The game is never told:
// it goes on computing in 320x240 and its coordinates are multiplied on the way to the raster,
// so nothing it draws moves. What changes is where the *edges* land — a triangle's boundary is
// resolved `scale` times as finely — which is worth having exactly where the game's geometry is
// transformed or scaled, and worth nothing at all where a sprite is blitted at 1:1. Those are
// still detected at 1:1 (in the game's own pixels) and still sampled nearest, so they enlarge as
// the same hard blocks and no sprite is softened by turning this up.
//
// 1 is the iPod's own resolution and the only scale the oracles run at. Anything outside 1..8 is
// clamped. Changing it resizes the framebuffer, which costs one allocation — 8 is 2560x1920, or
// 14 MB, and the ceiling is there because past it the picture stops repaying the memory rather
// than because anything breaks.
void set_render_scale(unsigned scale);
[[nodiscard]] unsigned render_scale();

// Resolve a run of glyphs at the raster's resolution rather than at the game's.
//
// The game's text is a bitmap font: one draw per line of dialogue, one quad per glyph, each quad
// 1:1 with its own cell of an anti-aliased sheet (`is_text_run` in libeapp/gles.cpp says how one
// is recognised). Enlarged, the coverage in that sheet is what decides where a glyph's edge is,
// and plain bilinear spreads that edge over as many pixels as the picture was enlarged by, which
// is a blurred letter. This reconstructs the edge instead: the coverage is filtered and then
// taken back to full contrast across one raster pixel, wherever that pixel falls.
//
// It has nothing to do at scale 1 — one texel is one pixel and there is no edge to resolve —
// so it is the render scale that decides how much this is worth.
void set_high_resolution_text(bool resolve_glyphs_at_raster_resolution);

// How many threads draw a frame. One per core unless told otherwise; 0 asks for that explicitly,
// and 1 draws on the calling thread and starts nothing. Answers what it settled on.
//
// **This is not one of the two settings above and is not a choice about the picture.** The raster
// is cut into horizontal stripes and a pixel belongs to exactly one of them, so the draws that
// touch it still arrive in the order the game issued them and every frame is bit-for-bit the
// frame one thread would have drawn — which is why it is on by default and why the picture oracle
// is unaffected. It is here so that a run can be pinned to one thread when something needs
// measuring, and so a platform with its own opinion about threads can say so.
unsigned set_render_threads(unsigned threads);
[[nodiscard]] unsigned render_threads();

// Two capability words the game reads at start-up and masks: which pipeline features exist, and
// whether selecting the built-in pipeline succeeded.
[[nodiscard]] uint32_t pipeline_capabilities(uint32_t query, uint32_t argument);
[[nodiscard]] uint32_t select_pipeline(Pipeline pipeline);

}  // namespace ipod::gfx
