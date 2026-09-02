// Hand-written ARM C-library routines. See arm_runtime.h for why each one is here.
#include "runtime/arm_runtime.h"

#include "runtime/runtime.h"

#include <cstring>

namespace minigolf::runtime {

namespace {

float float_from_bits(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

uint32_t bits_from_float(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

}  // namespace

void soft_float_add(Cpu& cpu) {
    cpu.r[0] = bits_from_float(float_from_bits(cpu.r[0]) + float_from_bits(cpu.r[1]));
}

void long_long_udiv(Cpu& cpu) {
    const uint64_t numerator = (static_cast<uint64_t>(cpu.r[1]) << 32) | cpu.r[0];
    const uint64_t denominator = (static_cast<uint64_t>(cpu.r[3]) << 32) | cpu.r[2];
    if (denominator == 0) {
        fatal("64-bit division by zero (numerator %llu) from %#010x",
              static_cast<unsigned long long>(numerator), cpu.r[LR]);
    }
    const uint64_t quotient = numerator / denominator;
    const uint64_t remainder = numerator % denominator;
    cpu.r[0] = static_cast<uint32_t>(quotient);
    cpu.r[1] = static_cast<uint32_t>(quotient >> 32);
    cpu.r[2] = static_cast<uint32_t>(remainder);
    cpu.r[3] = static_cast<uint32_t>(remainder >> 32);
}

}  // namespace minigolf::runtime
