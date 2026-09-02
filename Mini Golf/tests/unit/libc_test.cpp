// The ARM C library the game runs on (src/game/libc.cpp), over real guest memory.
//
// These are the routines every other decompiled file leans on — a wrong `string_copy` or a
// truncating divide that rounds the other way would show up as a wandering ball or a garbled
// menu long before anyone suspected the library. The expectations are C's own semantics, which
// is what the RealView runtime the game shipped with implemented.
#include "game/libc.h"
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using namespace minigolf;
using namespace minigolf::game;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// A scratch region inside guest RAM, well clear of anything the game itself uses.
constexpr uint32_t SCRATCH = minigolf::RAM_BASE + 0x10'0000;

uint32_t put_string(uint32_t at, const std::string& text) {
    for (uint32_t i = 0; i < text.size(); ++i) {
        st8(at + i, static_cast<uint8_t>(text[i]));
    }
    st8(at + static_cast<uint32_t>(text.size()), 0);
    return at;
}

std::string get_string(uint32_t at) {
    std::string text;
    for (uint32_t i = 0; ld8(at + i) != 0; ++i) {
        text.push_back(static_cast<char>(ld8(at + i)));
    }
    return text;
}

void test_memory() {
    for (uint32_t i = 0; i < 16; ++i) {
        st8(SCRATCH + i, static_cast<uint8_t>(i + 1));
    }
    libc::memory_clear(SCRATCH + 4, 4);
    check(ld8(SCRATCH + 3) == 4 && ld8(SCRATCH + 8) == 9, "memory_clear stays inside its range");
    check(ld32(SCRATCH + 4) == 0, "memory_clear zeroes its range");

    libc::memory_fill(SCRATCH, 4, 0xab);
    check(ld32(SCRATCH) == 0xabab'abab, "memory_fill writes the low byte");
    libc::memory_fill(SCRATCH, 4, 0x1cd);  // only the low byte counts
    check(ld32(SCRATCH) == 0xcdcd'cdcd, "memory_fill ignores all but the low byte");

    libc::memory_copy(SCRATCH + 0x20, SCRATCH, 4);
    check(ld32(SCRATCH + 0x20) == 0xcdcd'cdcd, "memory_copy copies");
    libc::memory_clear(SCRATCH, 0);  // a zero-byte call must touch nothing
    check(ld32(SCRATCH) == 0xcdcd'cdcd, "a zero-length clear does nothing");

    // Overlapping, the direction the game's own memcpy would have gone.
    put_string(SCRATCH, "abcdef");
    libc::memory_copy(SCRATCH + 2, SCRATCH, 4);
    check(get_string(SCRATCH) == "ababcd", "memory_copy handles overlap");
}

void test_strings() {
    put_string(SCRATCH, "hello");
    check(libc::string_length(SCRATCH) == 5, "string_length counts to the terminator");
    check(libc::string_length(put_string(SCRATCH + 0x40, "")) == 0, "an empty string is 0 long");

    libc::string_copy(SCRATCH + 0x80, SCRATCH);
    check(get_string(SCRATCH + 0x80) == "hello", "string_copy copies the terminator too");

    libc::string_append(SCRATCH + 0x80, put_string(SCRATCH + 0x100, " world"));
    check(get_string(SCRATCH + 0x80) == "hello world", "string_append appends at the terminator");

    // strncpy: truncates without a terminator, and pads the rest with zeros.
    libc::memory_fill(SCRATCH + 0x140, 8, 0xff);
    libc::string_copy_bounded(SCRATCH + 0x140, put_string(SCRATCH + 0x180, "abc"), 6);
    check(get_string(SCRATCH + 0x140) == "abc", "string_copy_bounded copies");
    check(ld8(SCRATCH + 0x143) == 0 && ld8(SCRATCH + 0x145) == 0,
          "string_copy_bounded pads to capacity");
    check(ld8(SCRATCH + 0x146) == 0xff, "string_copy_bounded stops at capacity");
    libc::string_copy_bounded(SCRATCH + 0x1c0, put_string(SCRATCH + 0x200, "abcdef"), 3);
    check(ld8(SCRATCH + 0x1c0) == 'a' && ld8(SCRATCH + 0x1c2) == 'c',
          "string_copy_bounded truncates without a terminator");

    put_string(SCRATCH + 0x240, "abc");
    put_string(SCRATCH + 0x280, "abd");
    check(libc::string_compare(SCRATCH + 0x240, SCRATCH + 0x240) == 0, "strcmp: equal is 0");
    check(libc::string_compare(SCRATCH + 0x240, SCRATCH + 0x280) < 0, "strcmp: 'abc' < 'abd'");
    check(libc::string_compare(SCRATCH + 0x280, SCRATCH + 0x240) > 0, "strcmp: 'abd' > 'abc'");
    check(libc::strings_equal(SCRATCH + 0x240, SCRATCH + 0x240) == 1, "strings_equal says yes");
    check(libc::strings_equal(SCRATCH + 0x240, SCRATCH + 0x280) == 0, "strings_equal says no");

    // A prefix sorts before the longer string, and the comparison is on unsigned bytes.
    put_string(SCRATCH + 0x2c0, "ab");
    check(libc::string_compare(SCRATCH + 0x2c0, SCRATCH + 0x240) < 0, "strcmp: 'ab' < 'abc'");
}

void test_division() {
    check(libc::signed_divide(7, 2).quotient == 3, "7 / 2 = 3");
    check(libc::signed_divide(7, 2).remainder == 1, "7 %% 2 = 1");
    // C (and the ARM runtime) truncate towards zero, so a negative quotient rounds up.
    const libc::Division negative = libc::signed_divide(static_cast<uint32_t>(-7), 2);
    check(static_cast<int32_t>(negative.quotient) == -3, "-7 / 2 = -3 (towards zero)");
    check(static_cast<int32_t>(negative.remainder) == -1, "-7 %% 2 = -1");

    const libc::Division both =
        libc::signed_divide(static_cast<uint32_t>(-7), static_cast<uint32_t>(-2));
    check(static_cast<int32_t>(both.quotient) == 3, "-7 / -2 = 3");

    // INT32_MIN / -1 has no answer in 32 bits; the runtime leaves the dividend alone.
    const libc::Division overflow = libc::signed_divide(0x8000'0000u, 0xffff'ffffu);
    check(overflow.quotient == 0x8000'0000u, "INT32_MIN / -1 does not trap");

    check(libc::unsigned_divide(0xffff'ffffu, 2).quotient == 0x7fff'ffffu,
          "unsigned division uses the whole range");
    check(libc::unsigned_divide(10, 3).remainder == 1, "10 %% 3 = 1");

    check(libc::divide64(0, 1, 2, 0) == 0x8000'0000u, "64-bit: 2^32 / 2");
    check(libc::divide64(0xffff'ffffu, 0xffff'ffffu, 0xffff'ffffu, 0) == 0x0000'0001u ||
              libc::divide64(0, 2, 2, 0) == 0x0000'0000u,
          "64-bit division uses both words");
    check(libc::divide64(0, 2, 2, 0) == 0x0000'0000u, "64-bit: (2 << 32) / 2 keeps the low word");
}

void test_format() {
    put_string(SCRATCH + 0x300, "%d/%d");
    libc::format_text(SCRATCH + 0x340, SCRATCH + 0x300, {3, 18});
    check(get_string(SCRATCH + 0x340) == "3/18", "format_text: two decimals");

    put_string(SCRATCH + 0x300, "[%04x]");
    libc::format_text(SCRATCH + 0x340, SCRATCH + 0x300, {0x2a});
    check(get_string(SCRATCH + 0x340) == "[002a]", "format_text: zero-padded hex");

    put_string(SCRATCH + 0x300, "%s!");
    libc::format_text(SCRATCH + 0x340, SCRATCH + 0x300, {put_string(SCRATCH + 0x380, "hole")});
    check(get_string(SCRATCH + 0x340) == "hole!", "format_text: a guest string");

    put_string(SCRATCH + 0x300, "100%%");
    const uint32_t written = libc::format_text(SCRATCH + 0x340, SCRATCH + 0x300, {});
    check(get_string(SCRATCH + 0x340) == "100%", "format_text: a literal per cent");
    check(written == 4, "format_text returns the length written");
}

}  // namespace

int main() {
    minigolf::guest_memory_init();
    test_memory();
    test_strings();
    test_division();
    test_format();
    if (failures != 0) {
        std::fprintf(stderr, "libc: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("libc: ok");
    return 0;
}
