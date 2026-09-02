// The game's string helpers over guest memory, 8-bit and UTF-16 (strings.cpp).
#pragma once

#include <cstdint>

namespace minigolf::game {

struct AppendResult {
    uint32_t destination_length;  // characters in the destination after the append
    uint32_t source_length;
};

AppendResult string_append(uint32_t destination, uint32_t source);               // 0x180092bc
AppendResult wide_string_append(uint32_t destination, uint32_t source);          // 0x180094a0
uint32_t string_length(uint32_t text);                                           // 0x18009358
uint32_t wide_string_length(uint32_t text);                                      // 0x18009560
void string_copy(uint32_t destination, uint32_t source);                         // 0x18009314
void wide_string_copy(uint32_t destination, uint32_t source);                    // 0x1800950c
void string_copy_n(uint32_t destination, uint32_t source, uint32_t count);       // 0x18014f30
void wide_string_copy_n(uint32_t destination, uint32_t source, uint32_t count);  // 0x18009a7c
void number_to_string(uint32_t destination, int32_t value, int32_t width);       // 0x18009198
void wide_number_to_string(uint32_t destination, int32_t value, int32_t width);  // 0x1800937c

}  // namespace minigolf::game
