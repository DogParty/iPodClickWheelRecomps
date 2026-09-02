// The graphics the game draws with — the shared core's, brought into this title's
// namespace.
//
// The iPod's GL ES driver was one piece of firmware and every title called the same one,
// so there is one reimplementation of it: `recomps/common/src/ipod/libeapp/gles.cpp`.
// A title that never makes a given call simply never reaches that entry point.
//
// See ../../../common/README.md.
#pragma once

// The title's own `types.h` too: the shared graphics header includes the *shared* types,
// which declare into `ipod`, and callers here spell them unqualified from their own
// namespace. This is what used to reach them transitively.
#include "framework/types.h"
#include "ipod/framework/graphics.h"

namespace lost::gfx {
using ::ipod::gfx::AttributeType;
using ::ipod::gfx::Buffer;
using ::ipod::gfx::Fixed16Bits;
using ::ipod::gfx::Pipeline;
using ::ipod::gfx::PixelFormat;
using ::ipod::gfx::PixelStore;
using ::ipod::gfx::PixelType;
using ::ipod::gfx::Primitive;
using ::ipod::gfx::SCREEN_HEIGHT;
using ::ipod::gfx::SCREEN_WIDTH;
using ::ipod::gfx::TextureParameter;
using ::ipod::gfx::TextureTarget;
using ::ipod::gfx::bind_texture;
using ::ipod::gfx::clear;
using ::ipod::gfx::compressed_texture_image;
using ::ipod::gfx::copy_texture_image;
using ::ipod::gfx::disable_vertex_array;
using ::ipod::gfx::draw_arrays;
using ::ipod::gfx::enable_vertex_array;
using ::ipod::gfx::error;
using ::ipod::gfx::matrix_identity;
using ::ipod::gfx::matrix_multiply;
using ::ipod::gfx::matrix_ortho;
using ::ipod::gfx::pipeline_capabilities;
using ::ipod::gfx::render_scale;
using ::ipod::gfx::render_threads;
using ::ipod::gfx::screen_height;
using ::ipod::gfx::anything_drawn;
using ::ipod::gfx::screen_pixels;
using ::ipod::gfx::screen_width;
using ::ipod::gfx::select_pipeline;
using ::ipod::gfx::set_active_texture;
using ::ipod::gfx::set_clear_color;
using ::ipod::gfx::set_constant_color;
using ::ipod::gfx::set_attributes_repointed_per_draw;
using ::ipod::gfx::set_emulator_graphics;
using ::ipod::gfx::set_high_resolution_text;
using ::ipod::gfx::set_matrix_uniform;
using ::ipod::gfx::set_matrix_uniform_fixed;
using ::ipod::gfx::set_pixel_store;
using ::ipod::gfx::set_render_scale;
using ::ipod::gfx::set_render_server_image;
using ::ipod::gfx::set_render_threads;
using ::ipod::gfx::set_texture_parameter;
using ::ipod::gfx::set_vertex_array;
using ::ipod::gfx::start_render_server;
using ::ipod::gfx::stop_render_server;
using ::ipod::gfx::swap_buffers;
using ::ipod::gfx::texture_image;
}  // namespace lost::gfx
