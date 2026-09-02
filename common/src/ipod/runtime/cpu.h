// Guest CPU state and the ARM arithmetic rules the recompiled code is written against.
//
// Recompiled functions have the signature `void f_XXXXXXXX(Cpu& cpu)` and work on the sixteen
// registers and four condition flags below exactly as the original ARM code did. Hand-written
// code does not: the game in `src/game/` is plain C++, and reaches the platform through the
// typed interfaces in `src/framework/`. What is left below serves the boundary between them. The
// helpers here are the *only* place ARM semantics live: carry-out of the barrel shifter, the
// meaning of `LSR #0`, how `ADC` sets overflow. They are a line-for-line port of the emulator's
// `add_with_carry`, `shift_imm` and `shift_reg` (reference/arm7tdmi/arm.rs), which were validated
// against the real games; do not "simplify" them without a test vector.
//
// Design notes:
//   * Flags are eager: an instruction with the S bit set writes N/Z/C/V immediately. The
//     generated code is not hot enough to justify lazy flags, and eager flags keep every
//     function independently readable.
//   * All arithmetic is on uint32_t, which wraps by definition. Signed results are produced by
//     explicit casts at the point they are needed, never by relying on signed overflow.
//   * `tests/unit/cpu_test.cpp` pins these helpers to the vectors from arm.rs.
#pragma once

#include <cstdint>

namespace ipod {

// One bit of a word, by number. Defined here because the status-register helpers below need
// it; the barrel shifter further down is its other user.
constexpr bool bit(uint32_t value, unsigned n) {
    return ((value >> n) & 1u) != 0;
}

// The CPSR bits that are not condition flags: processor mode and the two interrupt masks. An
// eApp runs in Supervisor mode with IRQ and FIQ disabled, which is 0b1101_0011 — the state the
// emulator starts its CPU in (reference/arm7tdmi/cpu.rs, `Cpu::new`). Nothing in the game
// changes them: `msr` to any field but the flags is rejected by the decoder.
constexpr uint32_t CPSR_MODE_BITS = 0x0000'00D3;

struct Cpu {
    uint32_t r[16] = {};
    bool n = false;  // negative
    bool z = false;  // zero
    bool c = false;  // carry / not-borrow
    bool v = false;  // signed overflow

    // The CPSR bits `msr CPSR_f` may write that are not N/Z/C/V — bits 27..24, unused on this
    // core. Kept so that the save-and-restore pair armcc's soft-float library wraps its
    // arithmetic in round-trips exactly, rather than approximately.
    uint32_t cpsr_spare = 0;

    // `mrs Rd, CPSR`: the whole status word.
    [[nodiscard]] uint32_t cpsr() const {
        return (static_cast<uint32_t>(n) << 31) | (static_cast<uint32_t>(z) << 30) |
               (static_cast<uint32_t>(c) << 29) | (static_cast<uint32_t>(v) << 28) | cpsr_spare |
               CPSR_MODE_BITS;
    }

    // `msr CPSR_f, value`: the flag field only, which is the top byte.
    void set_cpsr_flags(uint32_t value) {
        n = bit(value, 31);
        z = bit(value, 30);
        c = bit(value, 29);
        v = bit(value, 28);
        cpsr_spare = value & 0x0F00'0000u;
    }
};

// The one register file. Three things still touch it: the ARM-ABI shims the dispatch table lands
// on, the guest stack pointer a `GuestScratch` moves, and an assert trap's register dump. In a
// pure-recompilation build `src/libeapp/arm_abi.cpp` reads its arguments from here as well.
inline Cpu& registers() {
    static Cpu instance;
    return instance;
}

constexpr unsigned SP = 13;
constexpr unsigned LR = 14;
constexpr unsigned PC = 15;

// ---------------------------------------------------------------------------------------------
// Barrel shifter
// ---------------------------------------------------------------------------------------------

enum class Shift : unsigned { Lsl = 0, Lsr = 1, Asr = 2, Ror = 3 };

struct Shifted {
    uint32_t value;
    bool carry;  // the shifter's carry-out, which logical instructions with S copy into C
};

// Arithmetic shift right for 0..31 places, written without relying on the implementation-defined
// right shift of negative signed integers.
constexpr uint32_t asr(uint32_t value, unsigned amount) {
    return bit(value, 31) ? ~(~value >> amount) : value >> amount;
}

constexpr uint32_t ror(uint32_t value, unsigned amount) {
    amount &= 31u;
    return amount == 0 ? value : (value >> amount) | (value << (32u - amount));
}

// Rotate right with extend: one place right, the old carry entering at the top.
constexpr uint32_t rrx(uint32_t value, bool carry) {
    return (static_cast<uint32_t>(carry) << 31) | (value >> 1);
}

// Shift by an immediate encoded in the instruction. An encoded amount of 0 has per-type
// meanings: LSL #0 is no shift, LSR #0 and ASR #0 mean 32, ROR #0 means RRX.
constexpr Shifted shift_imm(Shift type, uint32_t value, unsigned amount, bool carry_in) {
    switch (type) {
    case Shift::Lsl:
        return amount == 0 ? Shifted{value, carry_in}
                           : Shifted{value << amount, bit(value, 32u - amount)};
    case Shift::Lsr:
        return amount == 0 ? Shifted{0u, bit(value, 31)}
                           : Shifted{value >> amount, bit(value, amount - 1)};
    case Shift::Asr:
        return amount == 0 ? Shifted{asr(value, 31), bit(value, 31)}
                           : Shifted{asr(value, amount), bit(value, amount - 1)};
    case Shift::Ror:
        return amount == 0 ? Shifted{rrx(value, carry_in), bit(value, 0)}
                           : Shifted{ror(value, amount), bit(value, amount - 1)};
    }
    return {value, carry_in};
}

// Shift by the low byte of a register. 0 means no shift; 32 and above are defined, not special.
constexpr Shifted shift_reg(Shift type, uint32_t value, uint32_t register_amount, bool carry_in) {
    const unsigned amount = register_amount & 0xFFu;
    if (amount == 0) {
        return {value, carry_in};
    }
    switch (type) {
    case Shift::Lsl:
        if (amount < 32) {
            return {value << amount, bit(value, 32u - amount)};
        }
        return {0u, amount == 32 && bit(value, 0)};
    case Shift::Lsr:
        if (amount < 32) {
            return {value >> amount, bit(value, amount - 1)};
        }
        return {0u, amount == 32 && bit(value, 31)};
    case Shift::Asr:
        if (amount < 32) {
            return {asr(value, amount), bit(value, amount - 1)};
        }
        return {asr(value, 31), bit(value, 31)};
    case Shift::Ror: {
        const unsigned places = amount & 31u;
        return places == 0 ? Shifted{value, bit(value, 31)}
                           : Shifted{ror(value, places), bit(value, places - 1)};
    }
    }
    return {value, carry_in};
}

// Value-only forms for instructions that do not set flags.
constexpr uint32_t lsl_reg(uint32_t value, uint32_t amount) {
    return shift_reg(Shift::Lsl, value, amount, false).value;
}
constexpr uint32_t lsr_reg(uint32_t value, uint32_t amount) {
    return shift_reg(Shift::Lsr, value, amount, false).value;
}
constexpr uint32_t asr_reg(uint32_t value, uint32_t amount) {
    return shift_reg(Shift::Asr, value, amount, false).value;
}
constexpr uint32_t ror_reg(uint32_t value, uint32_t amount) {
    return shift_reg(Shift::Ror, value, amount, false).value;
}

// ---------------------------------------------------------------------------------------------
// Flag-setting arithmetic
// ---------------------------------------------------------------------------------------------

// `a + b + carry_in`, setting all four flags. Subtraction is `a + ~b + 1` and SBC is
// `a + ~b + C`; deriving every variant from this one routine is what keeps borrow and signed
// overflow consistent (a separate subtract path is where sign bugs come from).
inline uint32_t add_with_carry(Cpu& cpu, uint32_t a, uint32_t b, bool carry_in) {
    const uint64_t wide =
        static_cast<uint64_t>(a) + static_cast<uint64_t>(b) + (carry_in ? 1u : 0u);
    const uint32_t result = static_cast<uint32_t>(wide);
    cpu.n = bit(result, 31);
    cpu.z = result == 0;
    cpu.c = (wide >> 32) != 0;
    cpu.v = ((a ^ result) & (b ^ result) & 0x8000'0000u) != 0;
    return result;
}

inline uint32_t add_flags(Cpu& cpu, uint32_t a, uint32_t b) {
    return add_with_carry(cpu, a, b, false);
}
inline uint32_t adc_flags(Cpu& cpu, uint32_t a, uint32_t b) {
    return add_with_carry(cpu, a, b, cpu.c);
}
inline uint32_t sub_flags(Cpu& cpu, uint32_t a, uint32_t b) {
    return add_with_carry(cpu, a, ~b, true);
}
inline uint32_t sbc_flags(Cpu& cpu, uint32_t a, uint32_t b) {
    return add_with_carry(cpu, a, ~b, cpu.c);
}

// Logical instructions (AND, ORR, MOV, ...) with S set N and Z from the result and C from the
// shifter; V is untouched.
inline uint32_t logic_flags(Cpu& cpu, uint32_t result, bool shifter_carry) {
    cpu.n = bit(result, 31);
    cpu.z = result == 0;
    cpu.c = shifter_carry;
    return result;
}

// MUL/MLA with S set N and Z only (C is architecturally unpredictable and armcc never relies on
// it).
inline uint32_t mul_flags(Cpu& cpu, uint32_t result) {
    cpu.n = bit(result, 31);
    cpu.z = result == 0;
    return result;
}

// 64-bit multiplies write the low and high words to two registers. With S they set N and Z from
// the 64-bit result.
inline void multiply_long(Cpu& cpu, unsigned lo, unsigned hi, uint64_t product, bool set_flags) {
    cpu.r[lo] = static_cast<uint32_t>(product);
    cpu.r[hi] = static_cast<uint32_t>(product >> 32);
    if (set_flags) {
        cpu.n = (product >> 63) != 0;
        cpu.z = product == 0;
    }
}

inline uint64_t signed_product(uint32_t a, uint32_t b) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(a)) *
                                 static_cast<int64_t>(static_cast<int32_t>(b)));
}

inline uint64_t accumulator(const Cpu& cpu, unsigned lo, unsigned hi) {
    return (static_cast<uint64_t>(cpu.r[hi]) << 32) | cpu.r[lo];
}

inline void umull(Cpu& cpu, unsigned lo, unsigned hi, uint32_t a, uint32_t b, bool set_flags) {
    multiply_long(cpu, lo, hi, static_cast<uint64_t>(a) * b, set_flags);
}
inline void smull(Cpu& cpu, unsigned lo, unsigned hi, uint32_t a, uint32_t b, bool set_flags) {
    multiply_long(cpu, lo, hi, signed_product(a, b), set_flags);
}
inline void umlal(Cpu& cpu, unsigned lo, unsigned hi, uint32_t a, uint32_t b, bool set_flags) {
    multiply_long(cpu, lo, hi, static_cast<uint64_t>(a) * b + accumulator(cpu, lo, hi), set_flags);
}
inline void smlal(Cpu& cpu, unsigned lo, unsigned hi, uint32_t a, uint32_t b, bool set_flags) {
    multiply_long(cpu, lo, hi, signed_product(a, b) + accumulator(cpu, lo, hi), set_flags);
}

// ---------------------------------------------------------------------------------------------
// Condition codes
// ---------------------------------------------------------------------------------------------

inline bool cond_eq(const Cpu& cpu) {
    return cpu.z;
}
inline bool cond_ne(const Cpu& cpu) {
    return !cpu.z;
}
inline bool cond_cs(const Cpu& cpu) {
    return cpu.c;
}
inline bool cond_cc(const Cpu& cpu) {
    return !cpu.c;
}
inline bool cond_mi(const Cpu& cpu) {
    return cpu.n;
}
inline bool cond_pl(const Cpu& cpu) {
    return !cpu.n;
}
inline bool cond_vs(const Cpu& cpu) {
    return cpu.v;
}
inline bool cond_vc(const Cpu& cpu) {
    return !cpu.v;
}
inline bool cond_hi(const Cpu& cpu) {
    return cpu.c && !cpu.z;
}
inline bool cond_ls(const Cpu& cpu) {
    return !cpu.c || cpu.z;
}
inline bool cond_ge(const Cpu& cpu) {
    return cpu.n == cpu.v;
}
inline bool cond_lt(const Cpu& cpu) {
    return cpu.n != cpu.v;
}
inline bool cond_gt(const Cpu& cpu) {
    return !cpu.z && cpu.n == cpu.v;
}
inline bool cond_le(const Cpu& cpu) {
    return cpu.z || cpu.n != cpu.v;
}

}  // namespace ipod
