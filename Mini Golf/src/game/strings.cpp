// The game's own string helpers: append with null-pointer asserts, in 8-bit and UTF-16 flavours.
//
// These are the two most-called functions in the game (96 call sites between them) and the first
// to be decompiled by hand. Each has two parts: the real function, with real types, and a thin
// shim with the recompiled signature that moves arguments out of r0–r1 and calls it. The shim
// also leaves r1–r3 exactly as the original ARM code did on exit — the verification oracle logs
// r0–r3 at every framework call, and the game makes framework calls with those leftovers still in
// place, so a replacement that merely computed the right string would break the diff.
#include "strings.h"

#include "game_state.h"
#include "runtime/cpu.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"

namespace minigolf::game {

// 0x180092bc: strcat(destination, source) over guest memory. Both pointers must be non-null —
// the original trapped otherwise (`b .` at 0x180092c4 and 0x180092d8).
AppendResult string_append(uint32_t destination, uint32_t source) {
    uint32_t end = 0;
    while (ld8(destination + end) != 0) {
        ++end;
    }
    uint32_t copied = 0;
    for (uint32_t c = ld8(source); c != 0; c = ld8(source + ++copied)) {
        st8(destination + end++, c);
    }
    st8(destination + end, 0);
    return {end, copied};
}

// 0x180094a0: the same over 16-bit characters (the game keeps its UI text as UTF-16).
AppendResult wide_string_append(uint32_t destination, uint32_t source) {
    uint32_t end = 0;
    while (ld16(destination + 2 * end) != 0) {
        ++end;
    }
    uint32_t copied = 0;
    for (uint32_t c = ld16(source); c != 0; c = ld16(source + 2 * ++copied)) {
        st16(destination + 2 * end++, c);
    }
    st16(destination + 2 * end, 0);
    return {end, copied};
}

// 0x18009358 / 0x18009560: strlen for 8-bit and UTF-16 strings, asserting a non-null pointer.
uint32_t string_length(uint32_t text) {
    uint32_t length = 0;
    while (ld8(text + length) != 0) {
        ++length;
    }
    return length;
}

uint32_t wide_string_length(uint32_t text) {
    uint32_t length = 0;
    while (ld16(text + 2 * length) != 0) {
        ++length;
    }
    return length;
}

// 0x18009314 / 0x1800950c: strcpy for 8-bit and UTF-16 strings, asserting both pointers.
void string_copy(uint32_t destination, uint32_t source) {
    uint32_t i = 0;
    for (uint32_t c = ld8(source); c != 0; c = ld8(source + ++i)) {
        st8(destination + i, c);
    }
    st8(destination + i, 0);
}

void wide_string_copy(uint32_t destination, uint32_t source) {
    uint32_t i = 0;
    for (uint32_t c = ld16(source); c != 0; c = ld16(source + 2 * ++i)) {
        st16(destination + 2 * i, c);
    }
    st16(destination + 2 * i, 0);
}

// 0x18014f30 / 0x18009a7c — `count` characters copied, no terminator.
void string_copy_n(uint32_t destination, uint32_t source, uint32_t count) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
        st8(destination + i, ld8(source + i));
    }
}

void wide_string_copy_n(uint32_t destination, uint32_t source, uint32_t count) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
        st16(destination + i * 2, ld16(source + i * 2));
    }
}

namespace {

// 0x18009198 / 0x1800937c — a number as decimal text. `width` > 0 pads with zeros to that
// many digits; `width` < 0 writes at most that many (from the top); 0 means as many as it
// takes. Zero itself is written as |width| zeros (one if none). Negative numbers get a sign.
template <typename Put>
void number_format(uint32_t destination, int32_t value, int32_t width, Put put) {
    const uint32_t powers[11] = {1'000'000'000, 100'000'000, 10'000'000, 1'000'000, 100'000, 10'000,
                                 1'000,         100,         10,         1,         0};
    if (value == 0) {
        if (width < 0) {
            width = -width;
        } else if (width == 0) {
            width = 1;
        }
        while (width-- > 0) {
            destination = put(destination, '0');
        }
        put(destination, 0);
        return;
    }
    if (value < 0) {
        destination = put(destination, '-');
        value = -value;
    }
    uint32_t i = 0;
    while (static_cast<int32_t>(powers[i]) > value) {
        ++i;
    }
    const int32_t digits = 10 - static_cast<int32_t>(i);
    if (width > 0) {
        while (width > digits) {
            destination = put(destination, '0');
            --width;
        }
    }
    const int32_t skip = static_cast<int32_t>(i) - width;  // for a negative width: digits kept
    for (; powers[i] != 0; ++i) {
        if (width < 0 && skip <= static_cast<int32_t>(i)) {
            break;
        }
        uint32_t digit = '0';
        uint32_t rest = static_cast<uint32_t>(value);
        while (rest >= powers[i]) {
            ++digit;
            rest -= powers[i];
        }
        value = static_cast<int32_t>(rest);
        destination = put(destination, digit);
    }
    if (width < 0) {
        for (int32_t pad = -width; pad > digits; --pad) {
            destination = put(destination, '0');
        }
    }
    put(destination, 0);
}

}  // namespace

void number_to_string(uint32_t destination, int32_t value, int32_t width) {
    number_format(destination, value, width, [](uint32_t at, uint32_t c) {
        st8(at, c);
        return at + 1;
    });
}

void wide_number_to_string(uint32_t destination, int32_t value, int32_t width) {
    number_format(destination, value, width, [](uint32_t at, uint32_t c) {
        st16(at, c);
        return at + 2;
    });
}

}  // namespace minigolf::game
