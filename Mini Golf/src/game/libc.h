// The ARM C library as the game uses it, on guest memory: memory and string routines, the
// integer divisions, the framework-backed heap, the formatter, and the C runtime's start-up.
// These replace the recompiled RealView runtime (the game's other dependency besides the
// eApp frameworks); none of them reaches the frameworks except the heap, which is why those
// two take the return address the call log records.
#pragma once

#include <cstdint>
#include <initializer_list>

namespace minigolf::game::libc {

void memory_clear(uint32_t at, uint32_t bytes);                 // memclr
void memory_copy(uint32_t to, uint32_t from, uint32_t bytes);   // memcpy / memmove
void memory_fill(uint32_t at, uint32_t bytes, uint32_t value);  // memset (low byte of value)
uint32_t string_length(uint32_t text);                          // strlen
uint32_t string_copy(uint32_t to, uint32_t from);               // strcpy -> to
uint32_t string_copy_bounded(uint32_t to, uint32_t from, uint32_t capacity);  // strncpy -> to
uint32_t string_append(uint32_t to, uint32_t from);                           // strcat -> to
int32_t string_compare(uint32_t a, uint32_t b);                               // strcmp
uint32_t strings_equal(uint32_t a, uint32_t b);  // 1 when equal, else 0

struct Division {
    uint32_t quotient, remainder;
};
Division signed_divide(uint32_t dividend, uint32_t divisor);    // __rt_sdiv: truncating
Division unsigned_divide(uint32_t dividend, uint32_t divisor);  // __rt_udiv
// 64-bit unsigned division of (low, high) by (low, high); the quotient's low word.
uint32_t divide64(uint32_t low, uint32_t high, uint32_t divisor_low, uint32_t divisor_high);

// The heap is the framework's (misc #0 / #1); the call log records who asked, so the
// caller's return address comes along. heap_free of 0 does nothing.
uint32_t heap_allocate(uint32_t bytes);
void heap_free(uint32_t memory);

// The runtime's descriptor block; +0x38 heads the list of functions registered to run at exit.
constexpr uint32_t RUNTIME_DESCRIPTOR = 0x180e'1ac4;
constexpr uint32_t RUNTIME_AT_EXIT_LIST = RUNTIME_DESCRIPTOR + 0x38;

// sprintf for the formats the game uses: %d %i %u %x %X %c %s %%, with '-', '0', a width
// and a precision. `arguments` are guest words (strings are guest addresses). Returns the
// length written.
uint32_t format_text(uint32_t out, uint32_t format, std::initializer_list<uint32_t> arguments);

}  // namespace minigolf::game::libc
