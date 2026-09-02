#include "libc.h"

#include "calling.h"
#include "framework/device.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"

#include <cstdio>
#include <cstring>

namespace minigolf::game::libc {

void memory_clear(uint32_t at, uint32_t bytes) {
    if (bytes != 0) {
        std::memset(guest_pointer(at, bytes), 0, bytes);
    }
}

void memory_copy(uint32_t to, uint32_t from, uint32_t bytes) {
    if (bytes != 0) {
        std::memmove(guest_pointer(to, bytes), guest_pointer(from, bytes), bytes);
    }
}

void memory_fill(uint32_t at, uint32_t bytes, uint32_t value) {
    if (bytes != 0) {
        std::memset(guest_pointer(at, bytes), static_cast<int>(value & 0xff), bytes);
    }
}

namespace {
uint32_t length_of(uint32_t text) {
    uint32_t n = 0;
    while (ld8(text + n) != 0) {
        ++n;
    }
    return n;
}
}  // namespace

uint32_t string_length(uint32_t text) {
    return length_of(text);
}

uint32_t string_copy(uint32_t to, uint32_t from) {
    memory_copy(to, from, length_of(from) + 1);
    return to;
}

uint32_t string_copy_bounded(uint32_t to, uint32_t from, uint32_t capacity) {
    uint32_t i = 0;
    for (; i < capacity && ld8(from + i) != 0; ++i) {
        st8(to + i, ld8(from + i));
    }
    for (; i < capacity; ++i) {
        st8(to + i, 0);
    }
    return to;
}

uint32_t string_append(uint32_t to, uint32_t from) {
    string_copy(to + length_of(to), from);
    return to;
}

int32_t string_compare(uint32_t a, uint32_t b) {
    for (uint32_t i = 0;; ++i) {
        const int32_t ca = static_cast<int32_t>(ld8(a + i)), cb = static_cast<int32_t>(ld8(b + i));
        if (ca != cb || ca == 0) {
            return ca - cb;
        }
    }
}

uint32_t strings_equal(uint32_t a, uint32_t b) {
    return string_compare(a, b) == 0 ? 1 : 0;
}

Division signed_divide(uint32_t dividend, uint32_t divisor) {
    if (divisor == 0) {
        fatal("signed division by zero (%08x / 0)", dividend);
    }
    const int32_t a = static_cast<int32_t>(dividend), b = static_cast<int32_t>(divisor);
    if (a == INT32_MIN && b == -1) {
        return {dividend, 0};
    }
    return {static_cast<uint32_t>(a / b), static_cast<uint32_t>(a % b)};
}

Division unsigned_divide(uint32_t dividend, uint32_t divisor) {
    if (divisor == 0) {
        fatal("unsigned division by zero (%08x / 0)", dividend);
    }
    return {dividend / divisor, dividend % divisor};
}

uint32_t divide64(uint32_t low, uint32_t high, uint32_t divisor_low, uint32_t divisor_high) {
    const uint64_t dividend = (static_cast<uint64_t>(high) << 32) | low;
    const uint64_t divisor = (static_cast<uint64_t>(divisor_high) << 32) | divisor_low;
    if (divisor == 0) {
        fatal("64-bit division by zero");
    }
    return static_cast<uint32_t>(dividend / divisor);
}

uint32_t heap_allocate(uint32_t bytes) {
    return device::allocate(bytes);
}

void heap_free(uint32_t memory) {
    if (memory != 0) {
        device::release(memory);
    }
}

uint32_t format_text(uint32_t out, uint32_t format, std::initializer_list<uint32_t> arguments) {
    const uint32_t* next = arguments.begin();
    uint32_t written = 0;
    const auto put = [&](char c) {
        st8(out + written++, static_cast<uint32_t>(static_cast<uint8_t>(c)));
    };
    for (uint32_t i = 0; ld8(format + i) != 0; ++i) {
        const char c = static_cast<char>(ld8(format + i));
        if (c != '%') {
            put(c);
            continue;
        }
        // %[flags][width][.precision]conversion — gathered into a host format string.
        char spec[16] = "%";
        size_t n = 1;
        char conversion = 0;
        while (n < sizeof spec - 1) {
            const char s = static_cast<char>(ld8(format + ++i));
            spec[n++] = s;
            if (s == 0 || (s >= 'a' && s <= 'z') || (s >= 'A' && s <= 'Z') || s == '%') {
                conversion = s;
                break;
            }
        }
        spec[n] = 0;
        if (conversion == 0) {
            break;
        }
        char text[64];
        if (conversion == '%') {
            put('%');
            continue;
        }
        const uint32_t argument = next != arguments.end() ? *next++ : 0;
        if (conversion == 's') {
            // The argument is a guest string: format it as a host string.
            const uint32_t length = length_of(argument);
            const char* source = reinterpret_cast<const char*>(guest_pointer(argument, length + 1));
            std::snprintf(text, sizeof text, spec, source);
        } else if (conversion == 'c') {
            std::snprintf(text, sizeof text, spec, static_cast<int>(argument));
        } else if (conversion == 'u' || conversion == 'x' || conversion == 'X') {
            std::snprintf(text, sizeof text, spec, argument);
        } else {
            std::snprintf(text, sizeof text, spec, static_cast<int32_t>(argument));
        }
        for (const char* p = text; *p != 0; ++p) {
            put(*p);
        }
    }
    st8(out + written, 0);
    return written;
}

}  // namespace minigolf::game::libc
