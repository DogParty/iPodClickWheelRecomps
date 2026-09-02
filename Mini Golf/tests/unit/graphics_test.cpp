// The matrix helpers every drawn vertex goes through (src/libeapp/gles.cpp).
//
// The game builds its projection with these three calls and hands the product to the pipeline,
// so an error here moves everything on screen at once. The expected values are the definitions
// of an orthographic projection and of a column-major product; the game's own screen
// (0..320 by 0..240, depth -100..100) is used because that is what it asks for at start-up.
#include "framework/graphics.h"
#include "runtime/memory.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using namespace minigolf;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

constexpr uint32_t SCRATCH = RAM_BASE + 0x12'0000;

Float32Bits bits(float value) {
    Float32Bits word;
    std::memcpy(&word, &value, sizeof word);
    return word;
}

float element(uint32_t matrix, unsigned index) {
    float value;
    const uint32_t word = ld32(matrix + index * 4);
    std::memcpy(&value, &word, sizeof value);
    return value;
}

bool near(float value, float expected) {
    return std::fabs(value - expected) < 1e-5f;
}

bool is_identity(uint32_t matrix) {
    for (unsigned i = 0; i < 16; ++i) {
        if (!near(element(matrix, i), i % 5 == 0 ? 1.0f : 0.0f)) {
            return false;
        }
    }
    return true;
}

void test_identity() {
    for (unsigned i = 0; i < 16; ++i) {
        st32(SCRATCH + i * 4, 0xdead'beef);
    }
    gfx::matrix_identity(SCRATCH);
    check(is_identity(SCRATCH), "matrix_identity writes ones down the diagonal and zeros around");

    // A null matrix is the game's "no matrix"; it must be left alone rather than written through.
    gfx::matrix_identity(0);  // must not fault
}

void test_ortho() {
    const uint32_t m = SCRATCH + 0x100;
    gfx::matrix_ortho(m, bits(0.0f), bits(320.0f), bits(0.0f), bits(240.0f), bits(-100.0f),
                      bits(100.0f));
    // Scale maps the range onto -1..1; the translation puts its centre at the origin.
    check(near(element(m, 0), 2.0f / 320.0f), "ortho scales x by 2/width");
    check(near(element(m, 5), 2.0f / 240.0f), "ortho scales y by 2/height");
    check(near(element(m, 10), -2.0f / 200.0f), "ortho scales z by -2/depth");
    check(near(element(m, 12), -1.0f), "ortho moves the left edge to -1");
    check(near(element(m, 13), -1.0f), "ortho moves the bottom edge to -1");
    check(near(element(m, 15), 1.0f), "ortho leaves w alone");

    // A degenerate range has no projection; the matrix must be untouched.
    gfx::matrix_identity(m);
    gfx::matrix_ortho(m, bits(1.0f), bits(1.0f), bits(0.0f), bits(240.0f), bits(-1.0f), bits(1.0f));
    check(is_identity(m), "ortho with an empty range writes nothing");
}

void test_multiply() {
    const uint32_t a = SCRATCH + 0x200, b = SCRATCH + 0x300, out = SCRATCH + 0x400;
    gfx::matrix_identity(a);
    gfx::matrix_ortho(b, bits(0.0f), bits(320.0f), bits(0.0f), bits(240.0f), bits(-100.0f),
                      bits(100.0f));
    gfx::matrix_multiply(out, a, b);
    for (unsigned i = 0; i < 16; ++i) {
        if (!near(element(out, i), element(b, i))) {
            check(false, "multiplying by the identity changes nothing");
            break;
        }
    }

    // A translation times a scale, checked by hand: column-major, so the translation is in the
    // last column and is scaled by the matrix on its left.
    gfx::matrix_identity(a);
    st32(a + 12 * 4, bits(5.0f));  // translate x by 5
    gfx::matrix_identity(b);
    st32(b + 0 * 4, bits(2.0f));  // scale x by 2
    gfx::matrix_multiply(out, b, a);
    check(near(element(out, 12), 10.0f), "scale x translation scales the translation");
    check(near(element(out, 0), 2.0f), "the scale survives the product");
}

}  // namespace

int main() {
    guest_memory_init();
    test_identity();
    test_ortho();
    test_multiply();
    if (failures != 0) {
        std::fprintf(stderr, "graphics: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("graphics: ok");
    return 0;
}
