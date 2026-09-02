// OpenGL ES as the game reaches it: libeapp's ordinals under their GL names, the constants the
// game passes them, and the batcher's one piece of cached GL state.
#pragma once

#include "calling.h"
#include "framework/graphics.h"
#include "guest.h"
#include "runtime/memory.h"

#include <cstdint>
#include <initializer_list>

namespace minigolf::game {

constexpr uint32_t GL_MODE = 0x1801'a5a0;  // the pipeline last selected
// Float bit patterns the game hands the pipeline (the ABI carries floats in integer
// registers, so what travels is the word).
constexpr Float32Bits FLOAT_ONE = 0x3f80'0000;

// Switch the batcher's pipeline unless it is already the one asked for.
inline void gl_mode(gfx::Pipeline pipeline) {
    const uint32_t wanted = static_cast<uint32_t>(pipeline);
    if (guest<uint32_t>(GL_MODE) != wanted) {
        (void)gfx::select_pipeline(pipeline);  // the answer is always "selected"
        guest<uint32_t>(GL_MODE) = wanted;
    }
}

}  // namespace minigolf::game
