// The ARM calling convention, for the pure recompilation only.
//
// Recompiled ARM code calls the frameworks the way the hardware did: the first four arguments in
// r0-r3, the rest on the stack, the result in r0. Each function here unpacks one ordinal's
// arguments and calls the typed entry point behind it (src/framework/). The names are the ones
// `src/libeapp/imports.json` gives each ordinal, because `tools/emit.py` generates calls to them.
//
// The hand-decompiled game does not go through this file — it calls the typed entry points
// directly — and CMake compiles it only for a pure-recompilation build. `framework_call` writes
// the call log in ARM form for these calls (every register, the stack words, the return
// address), which is what `tests/diff.py --exact` compares.
#include "framework/audio.h"
#include "framework/controls.h"
#include "framework/device.h"
#include "framework/graphics.h"
#include "framework/music_library.h"
#include "framework/storage.h"
#include "ipod_eapp.h"
#include "runtime/memory.h"

namespace vortex::eapp {

namespace {

// Arguments beyond the fourth, in order.
uint32_t stacked(const Cpu& cpu, unsigned index) {
    return ld32(cpu.r[SP] + 4 * index);
}

}  // namespace

// --- miscTBD and Settings --------------------------------------------------------------------

void misc_alloc(Cpu& cpu) {
    cpu.r[0] = device::allocate(cpu.r[0]);
}
void misc_free(Cpu& cpu) {
    device::release(cpu.r[0]);
    cpu.r[0] = 0;
}
void misc_realloc(Cpu& cpu) {
    cpu.r[0] = device::reallocate(cpu.r[0], cpu.r[1]);
}
void misc_device_level_set(Cpu& cpu) {
    device::set_brightness(cpu.r[0]);
    cpu.r[0] = 0;
}
void misc_device_level_get(Cpu& cpu) {
    cpu.r[0] = device::brightness();
}
void misc_clock(Cpu& cpu) {
    cpu.r[0] = device::clock_microseconds(cpu.r[0]);
}
void misc_thousand(Cpu& cpu) {
    cpu.r[0] = device::clock_rate();
}
void misc_host_time(Cpu& cpu) {
    // The answer matters: a game reads a zero here as "the clock is not available" and draws a
    // hard-coded time instead (device.h, `wall_clock`).
    cpu.r[0] = device::wall_clock(cpu.r[0]);
}
void misc_host_battery(Cpu& cpu) {
    cpu.r[0] = device::battery_level();
}
void misc_resolve_name(Cpu& cpu) {
    device::resolve_resource(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
    cpu.r[0] = 0;
}
// #7 (the idle notice) and #11 (a word read beside the clock rate) answer 0 and do nothing.
void misc_zero(Cpu& cpu) {
    cpu.r[0] = 0;
}
void settings_get(Cpu& cpu) {
    cpu.r[0] = device::setting(cpu.r[0], cpu.r[1], cpu.r[2]);
}

// --- InputEvents ------------------------------------------------------------------------------

void input_poll(Cpu& cpu) {
    controls::poll(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}

// --- AsyncFileIO ------------------------------------------------------------------------------

void afio_open(Cpu& cpu) {
    cpu.r[0] = storage::open(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
}
void afio_open_alt(Cpu& cpu) {
    cpu.r[0] = storage::open_for_read(cpu.r[0], cpu.r[1], cpu.r[2]);
}
void afio_close(Cpu& cpu) {
    cpu.r[0] = storage::close(cpu.r[0]);
}
// #4 carries the request in r2 and has the same effect as #1: complete it with nothing to do.
void afio_operation(Cpu& cpu) {
    cpu.r[0] = storage::close(cpu.r[2]);
}
void afio_read(Cpu& cpu) {
    cpu.r[0] = storage::perform(cpu.r[0]);
}
// The save store (#12, #14, #16). These used to answer 1 — "not ready" — which is why no
// recording in tests/expected/ contains a save being written; the store is real now and the pure
// recompilation reaches it the same way the decompiled game does.
void afio_store_open(Cpu& cpu) {
    cpu.r[0] = storage::store_open(cpu.r[0], cpu.r[1], cpu.r[2]);
}
void afio_store_close(Cpu& cpu) {
    cpu.r[0] = storage::store_close(cpu.r[0]);
}
void afio_store_write(Cpu& cpu) {
    cpu.r[0] = storage::store_write(cpu.r[0], cpu.r[1], cpu.r[2]);
}

// --- Audio ------------------------------------------------------------------------------------

void audio_sfx_create(Cpu& cpu) {
    cpu.r[0] = audio::create_sound(cpu.r[0], cpu.r[1]);
}
void audio_sfx_release(Cpu& cpu) {
    audio::release_sound(cpu.r[0]);
    cpu.r[0] = 0;
}
void audio_sfx_play(Cpu& cpu) {
    audio::play_sound(cpu.r[0]);
    cpu.r[0] = 0;
}
void audio_sfx_set_buffer(Cpu& cpu) {
    audio::set_sound_buffer(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_08(Cpu& cpu) {
    audio::set_sound_data_size(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_0c(Cpu& cpu) {
    audio::set_sound_reserved(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_10(Cpu& cpu) {
    audio::set_sound_sample_rate(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_14(Cpu& cpu) {
    audio::set_sound_channels(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_18(Cpu& cpu) {
    audio::set_sound_bits(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_1c(Cpu& cpu) {
    audio::set_sound_volume(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_20(Cpu& cpu) {
    audio::set_sound_rate(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_24(Cpu& cpu) {
    audio::set_sound_pan(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_state(Cpu& cpu) {
    audio::set_sound_ready(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_repeat(Cpu& cpu) {
    audio::set_sound_repeat(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_set_field_3e(Cpu& cpu) {
    audio::set_sound_state(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void audio_sfx_get_field_04(Cpu& cpu) {
    cpu.r[0] = audio::sound_data(cpu.r[0]);
}
void audio_stream_register(Cpu& cpu) {
    cpu.r[0] = audio::register_stream(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
}
void audio_stream_play(Cpu& cpu) {
    audio::play_stream(cpu.r[0]);
    cpu.r[0] = 0;
}
void audio_stream_repeat(Cpu& cpu) {
    audio::set_repeat_mode(cpu.r[0]);
    cpu.r[0] = 0;
}
// #47 is answered zero by `audio_zero`: the emulator's `Stub::Value(0)`, unexplained.
// #50 sets the player's shuffle mode — the setter beside #48's repeat mode (reference/eapp-loader/
// lib.rs, `AudioRepeat`). Nothing here shuffles a one-track playlist; the answer is the zero the
// hardware gave.
void audio_set_shuffle(Cpu& cpu) {
    cpu.r[0] = 0;
}
void audio_query_ff(Cpu& cpu) {
    cpu.r[0] = 0xff;
}
void audio_zero(Cpu& cpu) {
    cpu.r[0] = 0;
}

// --- Metadata ---------------------------------------------------------------------------------

void metadata_now_playing_playlist(Cpu& cpu) {
    cpu.r[0] = music::now_playing_playlist();
}
void metadata_playlist_track_count(Cpu& cpu) {
    cpu.r[0] = music::playlist_track_count();
}
void metadata_now_playing_index(Cpu& cpu) {
    cpu.r[0] = music::now_playing_index();
}

// --- OpenGLES ---------------------------------------------------------------------------------

void gl_bind_texture(Cpu& cpu) {
    gfx::bind_texture(static_cast<gfx::TextureTarget>(cpu.r[0]), cpu.r[1]);
    cpu.r[0] = 0;
}
void gl_clear(Cpu& cpu) {
    gfx::clear(static_cast<gfx::Buffer>(cpu.r[0]));
    cpu.r[0] = 0;
}
void gl_clear_color(Cpu& cpu) {
    gfx::set_clear_color(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
    cpu.r[0] = 0;
}
// #105 glTexSubImage2D: four arguments in registers, five on the stack.
void gl_tex_sub_image_2d(Cpu& cpu) {
    gfx::texture_sub_image(static_cast<gfx::TextureTarget>(cpu.r[0]), cpu.r[1], cpu.r[2], cpu.r[3],
                           stacked(cpu, 0), stacked(cpu, 1),
                           static_cast<gfx::PixelFormat>(stacked(cpu, 2)),
                           static_cast<gfx::PixelType>(stacked(cpu, 3)), stacked(cpu, 4));
    cpu.r[0] = 0;
}
void gl_compressed_tex_image_2d(Cpu& cpu) {
    gfx::compressed_texture_image(static_cast<gfx::TextureTarget>(cpu.r[0]), cpu.r[1],
                                  static_cast<gfx::PixelFormat>(cpu.r[2]), cpu.r[3],
                                  stacked(cpu, 0), stacked(cpu, 1), stacked(cpu, 2),
                                  stacked(cpu, 3));
    cpu.r[0] = 0;
}
void gl_copy_tex_image_2d(Cpu& cpu) {
    gfx::copy_texture_image(static_cast<gfx::TextureTarget>(cpu.r[0]), cpu.r[1],
                            static_cast<gfx::PixelFormat>(cpu.r[2]), static_cast<int32_t>(cpu.r[3]),
                            static_cast<int32_t>(stacked(cpu, 0)), stacked(cpu, 1), stacked(cpu, 2),
                            stacked(cpu, 3));
    cpu.r[0] = 0;
}
void gl_disable_vertex_attrib_array(Cpu& cpu) {
    gfx::disable_vertex_array(cpu.r[0]);
    cpu.r[0] = 0;
}
void gl_enable_vertex_attrib_array(Cpu& cpu) {
    gfx::enable_vertex_array(cpu.r[0]);
    cpu.r[0] = 0;
}
void gl_draw_arrays(Cpu& cpu) {
    gfx::draw_arrays(static_cast<gfx::Primitive>(cpu.r[0]), cpu.r[1], cpu.r[2]);
    cpu.r[0] = 0;
}
void gl_draw_elements(Cpu& cpu) {
    gfx::draw_elements(static_cast<gfx::Primitive>(cpu.r[0]), cpu.r[1],
                       static_cast<gfx::IndexType>(cpu.r[2]), cpu.r[3]);
    cpu.r[0] = 0;
}
void gl_pixel_store(Cpu& cpu) {
    cpu.r[0] = gfx::set_pixel_store(static_cast<gfx::PixelStore>(cpu.r[0]), cpu.r[1]);
}
void gl_tex_image_2d(Cpu& cpu) {
    gfx::texture_image(static_cast<gfx::TextureTarget>(cpu.r[0]), cpu.r[1],
                       static_cast<gfx::PixelFormat>(cpu.r[2]), cpu.r[3], stacked(cpu, 0),
                       stacked(cpu, 1), static_cast<gfx::PixelFormat>(stacked(cpu, 2)),
                       static_cast<gfx::PixelType>(stacked(cpu, 3)), stacked(cpu, 4));
    cpu.r[0] = 0;
}
void gl_active_texture(Cpu& cpu) {
    cpu.r[0] = gfx::set_active_texture(cpu.r[0]);
}
void gl_start_render_server(Cpu& cpu) {
    cpu.r[0] = gfx::start_render_server(cpu.r[0], cpu.r[1], cpu.r[2]);
}
void gl_stop_render_server(Cpu& cpu) {
    cpu.r[0] = gfx::stop_render_server(cpu.r[0]);
}
void gl_set_render_server_image(Cpu& cpu) {
    cpu.r[0] = gfx::set_render_server_image(cpu.r[0], cpu.r[1], cpu.r[2]);
}
// #147's fifth argument is the first stack word — the scalar form's only difference from #148.
void gl_disable(Cpu& cpu) {
    gfx::disable(cpu.r[0]);
    cpu.r[0] = 0;
}
void gl_gen_textures(Cpu& cpu) {
    gfx::gen_textures(cpu.r[0], cpu.r[1]);
    cpu.r[0] = 0;
}
void gl_uniform_4xv(Cpu& cpu) {
    gfx::set_constant_color_vector(cpu.r[0], cpu.r[1], cpu.r[2]);
    cpu.r[0] = 0;
}
void gl_uniform_4x_scalar(Cpu& cpu) {
    gfx::set_constant_color(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], stacked(cpu, 0));
    cpu.r[0] = 0;
}
void gl_uniform_matrix_4xv(Cpu& cpu) {
    gfx::set_matrix_uniform_fixed(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
    cpu.r[0] = 0;
}
void gl_uniform_matrix_4fv(Cpu& cpu) {
    gfx::set_matrix_uniform(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
    cpu.r[0] = 0;
}
void gl_vertex_attrib_pointer(Cpu& cpu) {
    gfx::set_vertex_array(cpu.r[0], cpu.r[1], static_cast<gfx::AttributeType>(cpu.r[2]), cpu.r[3],
                          stacked(cpu, 0), stacked(cpu, 1));
    cpu.r[0] = 0;
}
void gl_swap_buffers(Cpu& cpu) {
    gfx::swap_buffers();
    cpu.r[0] = 0;
}
void gl_load_identity(Cpu& cpu) {
    gfx::matrix_identity(cpu.r[0]);
    cpu.r[0] = 0;
}
void gl_ortho(Cpu& cpu) {
    gfx::matrix_ortho(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], stacked(cpu, 0), stacked(cpu, 1),
                      stacked(cpu, 2));
    cpu.r[0] = 0;
}
void gl_scale_matrix(Cpu& cpu) {
    gfx::matrix_scale(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
    cpu.r[0] = 0;
}
void gl_translate_matrix(Cpu& cpu) {
    gfx::matrix_translate(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
    cpu.r[0] = 0;
}
// #173 rotatef(m, angle, x, y, z): the axis's z is the fifth argument and travels on the stack.
void gl_rotate_matrix(Cpu& cpu) {
    gfx::matrix_rotate(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], stacked(cpu, 0));
    cpu.r[0] = 0;
}
void gl_mult_matrix(Cpu& cpu) {
    gfx::matrix_multiply(cpu.r[0], cpu.r[1], cpu.r[2]);
    cpu.r[0] = 0;
}
// #53 glGetError and #101 glTexParameterf, which the hardware accepted and ignored.
void gl_zero(Cpu& cpu) {
    cpu.r[0] = 0;
}
// #159 selecting the built-in pipeline succeeded.
void gl_one(Cpu& cpu) {
    cpu.r[0] = gfx::select_pipeline(static_cast<gfx::Pipeline>(cpu.r[0]));
}
// #158 the capability word the game masks.
void gl_query_0x3000(Cpu& cpu) {
    cpu.r[0] = 0x3000;
}

}  // namespace vortex::eapp
