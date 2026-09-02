// The render scale (src/framework/graphics.h, src/libeapp/gles.cpp): how large a picture the
// rasteriser draws, and who is allowed to change it.
//
// What this pins is the part with no picture in it — the size of the buffer, the range it will
// accept, and that `--emulator-graphics` takes the setting away rather than merely ignoring it.
// Whether the larger picture is *right* is not a question a size can answer, and is the picture
// oracle's (tests/frames.sh); whether it looks better is a question for eyes.
#include "framework/graphics.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_default_is_the_ipod() {
    check(cubis::gfx::render_scale() == 1, "the iPod's own resolution is where this starts");
    check(cubis::gfx::screen_width() == cubis::gfx::SCREEN_WIDTH, "and its own width");
    check(cubis::gfx::screen_height() == cubis::gfx::SCREEN_HEIGHT, "and its own height");
}

void test_the_buffer_grows() {
    cubis::gfx::set_render_scale(3);
    check(cubis::gfx::render_scale() == 3, "the scale is what was asked for");
    check(cubis::gfx::screen_width() == 3 * cubis::gfx::SCREEN_WIDTH,
          "the picture is three times as wide");
    check(cubis::gfx::screen_height() == 3 * cubis::gfx::SCREEN_HEIGHT,
          "and three times as tall");
    check(cubis::gfx::screen_pixels() != nullptr, "and there is a buffer behind it");
}

void test_out_of_range_is_clamped() {
    cubis::gfx::set_render_scale(99);
    check(cubis::gfx::render_scale() == 8,
          "a scale this rasteriser will not draw is clamped down");
    cubis::gfx::set_render_scale(0);
    check(cubis::gfx::render_scale() == 1, "and one below the iPod's is clamped up");
}

// The picture oracle compares whole frames against a renderer that draws 320x240. Asking for the
// emulator's graphics has to *take the scale away*, not merely be ignored by it: the two settings
// arrive from a file the player owns, and a recorded case must not depend on what is in it.
void test_the_oracle_owns_the_renderer() {
    cubis::gfx::set_render_scale(4);
    cubis::gfx::set_high_resolution_text(true);
    cubis::gfx::set_emulator_graphics(true);
    check(cubis::gfx::render_scale() == 1, "the emulator's graphics are 320x240 and nothing else");
    check(cubis::gfx::screen_width() == cubis::gfx::SCREEN_WIDTH, "so the buffer went back too");
    cubis::gfx::set_emulator_graphics(false);
}

}  // namespace

int main() {
    test_default_is_the_ipod();
    test_the_buffer_grows();
    test_out_of_range_is_clamped();
    test_the_oracle_owns_the_renderer();
    if (failures != 0) {
        std::fprintf(stderr, "render scale: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("render scale: ok");
    return 0;
}
