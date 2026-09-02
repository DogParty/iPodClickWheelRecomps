// 16.16 fixed point: the conversions and the arithmetic the game's geometry depends on.
#include "game/fixed.h"

#include <cstdint>
#include <cstdio>

using minigolf::game::Fixed16;
using minigolf::game::fraction_of;
using minigolf::game::to_fixed;
using minigolf::game::to_whole;

namespace {

int failures = 0;

// Not `assert`: a build that defines NDEBUG would compile the whole test away and still say it
// passed. Every other test here counts failures the same way.
void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    // Conversions round-trip, and negative values truncate the way an arithmetic shift does.
    check(to_fixed(0) == 0u, "to_fixed(0) == 0u");
    check(to_fixed(1) == 0x10000u, "to_fixed(1) == 0x10000u");
    check(to_fixed(320) == 0x0140'0000u, "to_fixed(320) == 0x0140'0000u");
    check(to_whole(to_fixed(123)) == 123, "to_whole(to_fixed(123)) == 123");
    check(to_whole(to_fixed(-5)) == -5, "to_whole(to_fixed(-5)) == -5");
    check(to_whole(0x0001'8000u) == 1, "to_whole(0x0001'8000u) == 1");  // 1.5 truncates down
    check(to_whole(0xffff'8000u) == -1,
          "to_whole(0xffff'8000u) == -1");  // -0.5 truncates towards negative infinity
    check(fraction_of(0x0001'8000u) == 0x8000u, "fraction_of(0x0001'8000u) == 0x8000u");

    // The wrapper agrees with the raw words the game stores.
    check(Fixed16::from_whole(7).bits() == to_fixed(7),
          "Fixed16::from_whole(7).bits() == to_fixed(7)");
    check(Fixed16::from_bits(0x0001'8000u).whole() == 1,
          "Fixed16::from_bits(0x0001'8000u).whole() == 1");

    // Addition and subtraction are word arithmetic; the product keeps the scale.
    check((Fixed16::from_whole(2) + Fixed16::from_whole(3)) == Fixed16::from_whole(5),
          "(Fixed16::from_whole(2) + Fixed16::from_whole(3)) == Fixed16::from_whole(5)");
    check((Fixed16::from_whole(2) - Fixed16::from_whole(3)) == Fixed16::from_whole(-1),
          "(Fixed16::from_whole(2) - Fixed16::from_whole(3)) == Fixed16::from_whole(-1)");
    check((Fixed16::from_whole(3) * Fixed16::from_whole(4)) == Fixed16::from_whole(12),
          "(Fixed16::from_whole(3) * Fixed16::from_whole(4)) == Fixed16::from_whole(12)");
    check((Fixed16::from_bits(0x8000u) * Fixed16::from_whole(5)).bits() == 0x0002'8000u,
          "(Fixed16::from_bits(0x8000u) * Fixed16::from_whole(5)).bits() == 0x0002'8000u");
    check((-Fixed16::from_whole(2)) == Fixed16::from_whole(-2),
          "(-Fixed16::from_whole(2)) == Fixed16::from_whole(-2)");
    check((Fixed16::from_whole(3) * -2) == Fixed16::from_whole(-6),
          "(Fixed16::from_whole(3) * -2) == Fixed16::from_whole(-6)");

    // A half times a half is a quarter — the case a plain 32-bit multiply gets wrong.
    check((Fixed16::from_bits(0x8000u) * Fixed16::from_bits(0x8000u)).bits() == 0x4000u,
          "(Fixed16::from_bits(0x8000u) * Fixed16::from_bits(0x8000u)).bits() == 0x4000u");

    // Ordering follows the signed value, not the word.
    check(Fixed16::from_whole(-1) < Fixed16::from_whole(0),
          "Fixed16::from_whole(-1) < Fixed16::from_whole(0)");
    check(Fixed16::from_whole(2) >= Fixed16::from_whole(2),
          "Fixed16::from_whole(2) >= Fixed16::from_whole(2)");

    if (failures != 0) {
        std::fprintf(stderr, "fixed: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("fixed: ok");
    return 0;
}
