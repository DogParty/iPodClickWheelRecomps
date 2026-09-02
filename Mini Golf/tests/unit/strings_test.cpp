// The game's own string helpers (src/game/strings.cpp), 8-bit and UTF-16.
//
// The game carries two string worlds: byte text for most languages, UTF-16 for the one that
// needs it, with a parallel routine for each. These tests pin the pair against each other — a
// wide routine that disagrees with its byte twin is the bug that shows up as one language
// rendering wrongly — and pin `number_to_string`, whose width argument means two different
// things depending on its sign.
#include "game/strings.h"
#include "runtime/memory.h"

#include <cstdio>
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

constexpr uint32_t SCRATCH = RAM_BASE + 0x11'0000;

uint32_t put_bytes(uint32_t at, const std::string& text) {
    for (uint32_t i = 0; i < text.size(); ++i) {
        st8(at + i, static_cast<uint8_t>(text[i]));
    }
    st8(at + static_cast<uint32_t>(text.size()), 0);
    return at;
}

uint32_t put_wide(uint32_t at, const std::string& text) {
    for (uint32_t i = 0; i < text.size(); ++i) {
        st16(at + i * 2, static_cast<uint8_t>(text[i]));
    }
    st16(at + static_cast<uint32_t>(text.size()) * 2, 0);
    return at;
}

std::string get_bytes(uint32_t at) {
    std::string text;
    for (uint32_t i = 0; ld8(at + i) != 0; ++i) {
        text.push_back(static_cast<char>(ld8(at + i)));
    }
    return text;
}

std::string get_wide(uint32_t at) {
    std::string text;
    for (uint32_t i = 0; ld16(at + i * 2) != 0; ++i) {
        text.push_back(static_cast<char>(ld16(at + i * 2)));
    }
    return text;
}

void test_length_and_copy() {
    check(string_length(put_bytes(SCRATCH, "putting")) == 7, "string_length counts characters");
    check(wide_string_length(put_wide(SCRATCH + 0x40, "putting")) == 7,
          "wide_string_length counts characters, not bytes");
    check(string_length(put_bytes(SCRATCH + 0x80, "")) == 0, "an empty string is 0 long");
    check(wide_string_length(put_wide(SCRATCH + 0xc0, "")) == 0, "an empty wide string is 0 long");

    string_copy(SCRATCH + 0x100, put_bytes(SCRATCH, "green"));
    check(get_bytes(SCRATCH + 0x100) == "green", "string_copy copies");
    wide_string_copy(SCRATCH + 0x140, put_wide(SCRATCH + 0x40, "green"));
    check(get_wide(SCRATCH + 0x140) == "green", "wide_string_copy copies");

    // copy_n copies exactly n characters and terminates after them.
    string_copy_n(SCRATCH + 0x200, put_bytes(SCRATCH, "birdie"), 4);
    check(get_bytes(SCRATCH + 0x200) == "bird", "string_copy_n stops at n");
    wide_string_copy_n(SCRATCH + 0x240, put_wide(SCRATCH + 0x40, "birdie"), 4);
    check(get_wide(SCRATCH + 0x240) == "bird", "wide_string_copy_n stops at n");
}

void test_append() {
    put_bytes(SCRATCH + 0x300, "par");
    const AppendResult result = string_append(SCRATCH + 0x300, put_bytes(SCRATCH, " 3"));
    check(get_bytes(SCRATCH + 0x300) == "par 3", "string_append appends");
    check(result.destination_length == 5, "string_append reports the new length");
    check(result.source_length == 2, "string_append reports what it took");

    put_wide(SCRATCH + 0x340, "par");
    const AppendResult wide = wide_string_append(SCRATCH + 0x340, put_wide(SCRATCH + 0x40, " 3"));
    check(get_wide(SCRATCH + 0x340) == "par 3", "wide_string_append appends");
    check(wide.destination_length == 5 && wide.source_length == 2,
          "wide_string_append reports the same counts as its byte twin");
}

void test_numbers() {
    number_to_string(SCRATCH + 0x400, 42, 0);
    check(get_bytes(SCRATCH + 0x400) == "42", "a number with no width");
    number_to_string(SCRATCH + 0x400, 0, 0);
    check(get_bytes(SCRATCH + 0x400) == "0", "zero");

    // A positive width pads on the left with zeros: the score card's "07".
    number_to_string(SCRATCH + 0x400, 7, 2);
    check(get_bytes(SCRATCH + 0x400) == "07", "a positive width zero-pads to that many digits");
    number_to_string(SCRATCH + 0x400, 123, 2);
    check(get_bytes(SCRATCH + 0x400) == "123", "a width narrower than the number keeps it whole");

    // A negative width keeps that many digits instead, padding on the right.
    number_to_string(SCRATCH + 0x400, 5, -2);
    check(get_bytes(SCRATCH + 0x400) == "50", "a negative width keeps digits from the left");

    wide_number_to_string(SCRATCH + 0x440, 7, 2);
    check(get_wide(SCRATCH + 0x440) == "07", "the wide formatter agrees with the byte one");
}

}  // namespace

int main() {
    guest_memory_init();
    test_length_and_copy();
    test_append();
    test_numbers();
    if (failures != 0) {
        std::fprintf(stderr, "strings: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("strings: ok");
    return 0;
}
