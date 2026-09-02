// Pins the ARM arithmetic helpers in runtime/cpu.h to known-good vectors.
//
// The expected values are the ARM Architecture Reference Manual's rules as implemented by the
// emulator's `add_with_carry`, `shift_imm` and `shift_reg` (reference/arm7tdmi/arm.rs), which
// ran the real games. Every special case the generated code depends on is covered: the encoded
// zero amounts (LSR #0 = 32, ROR #0 = RRX), register shifts of 32 and more, and the carry and
// overflow rules for add/subtract.
#include "runtime/cpu.h"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void check_shift(cubis::Shifted got, uint32_t value, bool carry, const char* what) {
    check(got.value == value && got.carry == carry, what);
}

void test_shift_imm() {
    using cubis::Shift;
    using cubis::shift_imm;
    check_shift(shift_imm(Shift::Lsl, 0x8000'0001u, 0, false), 0x8000'0001u, false,
                "LSL #0 keeps value and carry");
    check_shift(shift_imm(Shift::Lsl, 0x8000'0001u, 1, false), 0x0000'0002u, true,
                "LSL #1 carries out bit 31");
    check_shift(shift_imm(Shift::Lsr, 0x8000'0001u, 0, false), 0u, true, "LSR #0 means LSR #32");
    check_shift(shift_imm(Shift::Lsr, 0x8000'0001u, 1, false), 0x4000'0000u, true,
                "LSR #1 carries out bit 0");
    check_shift(shift_imm(Shift::Asr, 0x8000'0000u, 0, false), 0xFFFF'FFFFu, true,
                "ASR #0 means ASR #32");
    check_shift(shift_imm(Shift::Asr, 0x8000'0000u, 4, false), 0xF800'0000u, false,
                "ASR #4 sign-extends");
    check_shift(shift_imm(Shift::Ror, 0x0000'0003u, 0, true), 0x8000'0001u, true, "ROR #0 is RRX");
    check_shift(shift_imm(Shift::Ror, 0x0000'0003u, 1, false), 0x8000'0001u, true,
                "ROR #1 rotates bit 0 to the top");
}

void test_shift_reg() {
    using cubis::Shift;
    using cubis::shift_reg;
    check_shift(shift_reg(Shift::Lsl, 0x1234'5678u, 0, true), 0x1234'5678u, true,
                "register LSL 0 keeps carry");
    check_shift(shift_reg(Shift::Lsl, 0x0000'0001u, 32, false), 0u, true,
                "register LSL 32 carries out bit 0");
    check_shift(shift_reg(Shift::Lsl, 0xFFFF'FFFFu, 33, true), 0u, false,
                "register LSL 33 clears carry");
    check_shift(shift_reg(Shift::Lsr, 0x8000'0000u, 32, false), 0u, true,
                "register LSR 32 carries out bit 31");
    check_shift(shift_reg(Shift::Lsr, 0xFFFF'FFFFu, 40, true), 0u, false,
                "register LSR 40 clears carry");
    check_shift(shift_reg(Shift::Asr, 0x8000'0000u, 40, false), 0xFFFF'FFFFu, true,
                "register ASR >= 32 fills with sign");
    check_shift(shift_reg(Shift::Ror, 0x8000'0001u, 32, false), 0x8000'0001u, true,
                "register ROR 32 keeps value, carry = bit 31");
    check_shift(shift_reg(Shift::Ror, 0x0000'0003u, 0x101, false), 0x8000'0001u, true,
                "register ROR uses the low byte");
}

void test_add_sub_flags() {
    using cubis::Cpu;
    Cpu cpu;

    check(cubis::add_flags(cpu, 0x7FFF'FFFFu, 1u) == 0x8000'0000u,
          "add result wraps into the sign bit");
    check(cpu.n && !cpu.z && !cpu.c && cpu.v, "0x7fffffff + 1: N V set, C clear");

    check(cubis::add_flags(cpu, 0xFFFF'FFFFu, 1u) == 0u, "add result wraps to zero");
    check(!cpu.n && cpu.z && cpu.c && !cpu.v, "0xffffffff + 1: Z C set, V clear");

    check(cubis::sub_flags(cpu, 0u, 1u) == 0xFFFF'FFFFu, "0 - 1 wraps");
    check(cpu.n && !cpu.z && !cpu.c && !cpu.v, "0 - 1: N set, borrow clears C");

    check(cubis::sub_flags(cpu, 5u, 5u) == 0u, "5 - 5 is zero");
    check(!cpu.n && cpu.z && cpu.c && !cpu.v, "equal compare: Z and C set");

    check(cubis::sub_flags(cpu, 0x8000'0000u, 1u) == 0x7FFF'FFFFu, "INT_MIN - 1 wraps");
    check(!cpu.n && !cpu.z && cpu.c && cpu.v, "INT_MIN - 1: signed overflow, no borrow");

    cpu.c = false;
    check(cubis::sbc_flags(cpu, 10u, 3u) == 6u, "SBC subtracts the borrow");
    cpu.c = true;
    check(cubis::adc_flags(cpu, 10u, 3u) == 14u, "ADC adds the carry");
}

void test_conditions() {
    cubis::Cpu cpu;
    cubis::sub_flags(cpu, 3u, 5u);  // 3 < 5, signed and unsigned
    check(cubis::cond_lt(cpu) && cubis::cond_cc(cpu) && cubis::cond_ls(cpu),
          "3 cmp 5: lt, cc (lo), ls");
    check(!cubis::cond_ge(cpu) && !cubis::cond_hi(cpu) && cubis::cond_ne(cpu),
          "3 cmp 5: not ge, not hi, ne");
    cubis::sub_flags(cpu, 0xFFFF'FFFFu, 1u);  // -1 cmp 1 signed; huge cmp 1 unsigned
    check(cubis::cond_lt(cpu) && cubis::cond_hi(cpu), "-1 cmp 1: signed lt, unsigned hi");
}

void test_multiply_long() {
    cubis::Cpu cpu;
    cubis::smull(cpu, 0, 1, 0xFFFF'FFFFu, 2u, true);  // -1 * 2 = -2
    check(cpu.r[0] == 0xFFFF'FFFEu && cpu.r[1] == 0xFFFF'FFFFu && cpu.n && !cpu.z, "smull -1 * 2");
    cubis::umull(cpu, 2, 3, 0xFFFF'FFFFu, 2u, false);
    check(cpu.r[2] == 0xFFFF'FFFEu && cpu.r[3] == 1u, "umull 0xffffffff * 2");
    cubis::umlal(cpu, 2, 3, 1u, 1u, false);
    check(cpu.r[2] == 0xFFFF'FFFFu && cpu.r[3] == 1u, "umlal accumulates");
}

// `mrs`/`msr` on the flag field, which armcc's soft-float library uses to save and restore the
// condition flags around its own arithmetic. The pair has to round-trip exactly: what `mrs`
// reads, `msr` must put back, including the bits this core does not otherwise use.
void test_status_register() {
    cubis::Cpu cpu;
    cpu.n = true;
    cpu.c = true;
    const uint32_t saved = cpu.cpsr();
    check((saved & 0xF000'0000u) == 0xA000'0000u, "mrs reports N and C, not Z or V");
    check((saved & 0xFFu) == cubis::CPSR_MODE_BITS, "mrs reports supervisor mode, interrupts off");

    cpu.set_cpsr_flags(0x5000'0000u);  // Z and V, no N or C
    check(!cpu.n && cpu.z && !cpu.c && cpu.v, "msr cpsr_f writes all four flags");

    cpu.set_cpsr_flags(saved);
    check(cpu.n && !cpu.z && cpu.c && !cpu.v && cpu.cpsr() == saved, "the pair round-trips");

    // Bits 27..24 are in the field `msr cpsr_f` writes and are not condition flags. Keeping them
    // is what makes the round-trip exact rather than approximate.
    cpu.set_cpsr_flags(0x0300'0000u);
    check(cpu.cpsr() == (0x0300'0000u | cubis::CPSR_MODE_BITS),
          "the spare flag-field bits survive");
}

}  // namespace

int main() {
    test_shift_imm();
    test_shift_reg();
    test_add_sub_flags();
    test_conditions();
    test_multiply_long();
    test_status_register();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("cpu_test: all checks passed");
    return EXIT_SUCCESS;
}
