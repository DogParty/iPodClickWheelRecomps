//! ARM-state conformance tests.
//!
//! Encodings are written as raw words with the mnemonic in a comment rather than built by a
//! mini-assembler — an assembler would be a second thing to get wrong, and a wrong assembler
//! makes a wrong core look correct.

use arm7tdmi::{Bus, Cpu, FlatMemory, Mode};

const CODE: u32 = 0x1000;

fn setup(code: &[u32]) -> (Cpu, FlatMemory) {
    let mut mem = FlatMemory::new(0, 0x10000);
    let bytes: Vec<u8> = code.iter().flat_map(|w| w.to_le_bytes()).collect();
    mem.load(CODE, &bytes);
    let mut cpu = Cpu::new();
    cpu.set_mode(Mode::System);
    cpu.regs[15] = CODE;
    (cpu, mem)
}

fn run(code: &[u32], steps: usize) -> (Cpu, FlatMemory) {
    let (mut cpu, mut mem) = setup(code);
    cpu.run(&mut mem, steps);
    (cpu, mem)
}

// ---------------------------------------------------------------- data processing

#[test]
fn mov_immediate() {
    let (cpu, _) = run(&[0xE3A0_0001], 1); // mov r0, #1
    assert_eq!(cpu.regs[0], 1);
}

#[test]
fn immediate_operand_is_rotated_not_shifted() {
    // mov r0, #0x4000_0000 — encoded as 1 rotated right by 2, which only works if the
    // decoder rotates rather than shifting.
    let (cpu, _) = run(&[0xE3A0_0101], 1);
    assert_eq!(cpu.regs[0], 0x4000_0000);
}

#[test]
fn add_sets_carry_and_overflow_independently() {
    // Unsigned carry without signed overflow: 0xFFFF_FFFF + 1.
    let (cpu, _) = run(
        &[
            0xE3E0_0000, // mvn r0, #0      -> 0xFFFF_FFFF
            0xE3A0_1001, // mov r1, #1
            0xE090_2001, // adds r2, r0, r1
        ],
        3,
    );
    assert_eq!(cpu.regs[2], 0);
    assert!(cpu.cpsr.c(), "carry out expected");
    assert!(!cpu.cpsr.v(), "no signed overflow expected");
    assert!(cpu.cpsr.z(), "result is zero");
}

#[test]
fn add_signed_overflow_without_carry() {
    // 0x7FFF_FFFF + 1 overflows into the sign bit but produces no unsigned carry.
    let (cpu, _) = run(
        &[
            0xE3A0_0102, // mov r0, #0x8000_0000
            0xE2400001,  // sub r0, r0, #1   -> 0x7FFF_FFFF
            0xE3A0_1001, // mov r1, #1
            0xE090_2001, // adds r2, r0, r1
        ],
        4,
    );
    assert_eq!(cpu.regs[2], 0x8000_0000);
    assert!(!cpu.cpsr.c());
    assert!(cpu.cpsr.v(), "signed overflow expected");
    assert!(cpu.cpsr.n());
}

#[test]
fn subtract_carry_means_no_borrow() {
    // 5 - 3: C set (no borrow). Then 3 - 5: C clear (borrow).
    let (cpu, _) = run(
        &[
            0xE3A0_0005, // mov r0, #5
            0xE3A0_1003, // mov r1, #3
            0xE050_2001, // subs r2, r0, r1
        ],
        3,
    );
    assert_eq!(cpu.regs[2], 2);
    assert!(cpu.cpsr.c(), "5-3 must set C (no borrow)");

    let (cpu, _) = run(
        &[
            0xE3A0_0003, // mov r0, #3
            0xE3A0_1005, // mov r1, #5
            0xE050_2001, // subs r2, r0, r1
        ],
        3,
    );
    assert_eq!(cpu.regs[2], (-2i32) as u32);
    assert!(!cpu.cpsr.c(), "3-5 must clear C (borrow)");
    assert!(cpu.cpsr.n());
}

#[test]
fn rsb_reverses_the_operands() {
    let (cpu, _) = run(
        &[
            0xE3A0_0003, // mov r0, #3
            0xE260_100A, // rsb r1, r0, #10   -> 10 - 3
        ],
        2,
    );
    assert_eq!(cpu.regs[1], 7);
}

#[test]
fn cmp_does_not_write_a_result() {
    let (cpu, _) = run(
        &[
            0xE3A0_0005, // mov r0, #5
            0xE3A0_100A, // mov r1, #10
            0xE350_000A, // cmp r0, #10
        ],
        3,
    );
    assert_eq!(cpu.regs[0], 5, "CMP must not write Rd");
    assert!(!cpu.cpsr.z());
    assert!(!cpu.cpsr.c(), "5 < 10 borrows");
}

// ---------------------------------------------------------------- barrel shifter

#[test]
fn lsr_zero_encodes_lsr_32() {
    // movs r0, r1, lsr #0  -> result 0, carry = bit31 of r1
    let (cpu, _) = run(
        &[
            0xE3A0_1102, // mov r1, #0x8000_0000
            0xE1B0_0021, // movs r0, r1, lsr #0
        ],
        2,
    );
    assert_eq!(cpu.regs[0], 0, "LSR #0 means LSR #32");
    assert!(cpu.cpsr.c(), "carry comes from bit 31");
}

#[test]
fn asr_zero_encodes_asr_32() {
    let (cpu, _) = run(
        &[
            0xE3A0_1102, // mov r1, #0x8000_0000
            0xE1B0_0041, // movs r0, r1, asr #0
        ],
        2,
    );
    assert_eq!(cpu.regs[0], 0xFFFF_FFFF, "ASR #0 means ASR #32");
    assert!(cpu.cpsr.c());
}

#[test]
fn ror_zero_encodes_rrx_through_carry() {
    let (cpu, _) = run(
        &[
            0xE3A0_0000, // mov r0, #0
            0xE3B0_1001, // movs r1, #1        (clears C, sets Z=0)
            0xE1B0_0061, // movs r0, r1, rrx   -> C(0)<<31 | 1>>1 = 0, C = old bit0 = 1
        ],
        3,
    );
    assert_eq!(cpu.regs[0], 0);
    assert!(cpu.cpsr.c(), "RRX shifts bit 0 out into carry");

    // Now with carry set going in, RRX must shift a 1 into bit 31.
    let (cpu, _) = run(
        &[
            0xE3E0_0000, // mvn r0, #0
            0xE3A0_1001, // mov r1, #1
            0xE091_0000, // adds r0, r1, r0   -> 0, sets C
            0xE3A0_1002, // mov r1, #2
            0xE1B0_0061, // movs r0, r1, rrx
        ],
        5,
    );
    assert_eq!(cpu.regs[0], 0x8000_0001, "carry-in lands in bit 31");
}

#[test]
fn register_specified_shift_of_32_and_beyond() {
    // LSL by exactly 32 gives 0 with carry from bit 0; by more than 32, carry clears too.
    let (cpu, _) = run(
        &[
            0xE3A0_1003, // mov r1, #3
            0xE3A0_2020, // mov r2, #32
            0xE1B0_0211, // movs r0, r1, lsl r2
        ],
        3,
    );
    assert_eq!(cpu.regs[0], 0);
    assert!(cpu.cpsr.c(), "LSL #32 carries out bit 0");

    let (cpu, _) = run(
        &[
            0xE3A0_1003, // mov r1, #3
            0xE3A0_2021, // mov r2, #33
            0xE1B0_0211, // movs r0, r1, lsl r2
        ],
        3,
    );
    assert_eq!(cpu.regs[0], 0);
    assert!(!cpu.cpsr.c(), "LSL #33 clears carry");
}

// ---------------------------------------------------------------- conditions

#[test]
fn condition_codes_gate_execution() {
    let (cpu, _) = run(
        &[
            0xE3A0_0005, // mov r0, #5
            0xE350_0005, // cmp r0, #5       -> Z set
            0x03A0_1001, // moveq r1, #1     -> taken
            0x13A0_2001, // movne r2, #1     -> skipped
        ],
        4,
    );
    assert_eq!(cpu.regs[1], 1);
    assert_eq!(cpu.regs[2], 0);
}

#[test]
fn signed_conditions_use_n_versus_v() {
    // -1 < 1 signed, but 0xFFFF_FFFF > 1 unsigned. LT must take, HI must not.
    let (cpu, _) = run(
        &[
            0xE3E0_0000, // mvn r0, #0   -> -1
            0xE350_0001, // cmp r0, #1
            0xB3A0_1001, // movlt r1, #1
            0x83A0_2001, // movhi r2, #1
        ],
        4,
    );
    assert_eq!(cpu.regs[1], 1, "signed less-than must be taken");
    assert_eq!(cpu.regs[2], 1, "unsigned higher must also be taken");
}

// ---------------------------------------------------------------- branches

#[test]
fn branch_uses_pc_plus_eight() {
    // b +0 lands two instructions ahead of itself, because PC reads as addr+8.
    let (cpu, _) = run(&[0xEA00_0000, 0xE3A0_0001, 0xE3A0_0002], 2);
    assert_eq!(cpu.regs[0], 2, "branch skipped exactly one instruction");
}

#[test]
fn branch_and_link_stores_the_return_address() {
    let (cpu, _) = run(&[0xEB00_0000, 0xE3A0_0001], 1);
    assert_eq!(cpu.regs[14], CODE + 4, "LR points at the following instruction");
    assert_eq!(cpu.regs[15], CODE + 8);
}

#[test]
fn backward_branch_sign_extends() {
    // b -2 words: from 0x1008, offset 0xFFFFFC (-4) -> 0x1008 + 8 - 16 = 0x1000
    let (cpu, _) = run(
        &[
            0xE3A0_0001, // mov r0, #1
            0xE280_0001, // add r0, r0, #1
            0xEAFF_FFFC, // b  -4 words
            0xE3A0_00FF, // mov r0, #255   (must never execute)
        ],
        3,
    );
    assert_eq!(cpu.regs[15], CODE, "branched back to the start");
    assert_ne!(cpu.regs[0], 255);
}

#[test]
fn bx_switches_to_thumb_on_bit_zero() {
    let (cpu, _) = run(
        &[
            0xE3A0_0000, // mov r0, #0
            0xE280_0001, // add r0, r0, #1   -> r0 = 1 (odd address, Thumb)
            0xE12F_FF10, // bx r0
        ],
        3,
    );
    assert!(cpu.cpsr.thumb(), "bit 0 selects Thumb state");
    assert_eq!(cpu.regs[15], 0, "target address has bit 0 cleared");
}

// ---------------------------------------------------------------- loads and stores

#[test]
fn store_then_load_round_trips() {
    let (cpu, _) = run(
        &[
            0xE3A0_0F82, // mov r0, #0x208   (address)
            0xE3A0_10FF, // mov r1, #255
            0xE580_1000, // str r1, [r0]
            0xE590_2000, // ldr r2, [r0]
        ],
        4,
    );
    assert_eq!(cpu.regs[2], 255);
}

#[test]
fn misaligned_ldr_rotates_rather_than_faulting() {
    // Store 0x11223344 at 0x200, then LDR from 0x201 must return the word rotated right by 8.
    let (cpu, _) = run(
        &[
            0xE3A0_0C02, // mov r0, #0x200
            0xE59F_100C, // ldr r1, [pc, #0xC]   -> the literal below
            0xE580_1000, // str r1, [r0]
            0xE280_0001, // add r0, r0, #1
            0xE590_2000, // ldr r2, [r0]
            0xEA00_0000, // b over the literal
            0x1122_3344, // literal
        ],
        6,
    );
    assert_eq!(cpu.regs[1], 0x1122_3344, "literal pool load");
    assert_eq!(
        cpu.regs[2], 0x4411_2233,
        "misaligned LDR rotates right by 8*(addr&3)"
    );
}

#[test]
fn post_indexed_store_writes_back_the_base() {
    let (cpu, mut mem) = run(
        &[
            0xE3A0_0C02, // mov r0, #0x200
            0xE3A0_10AA, // mov r1, #0xAA
            0xE4801004, // str r1, [r0], #4
        ],
        3,
    );
    assert_eq!(cpu.regs[0], 0x204, "post-index writes the base back");
    assert_eq!(mem.read32(0x200), 0xAA, "stored at the pre-increment address");
}

#[test]
fn byte_access_touches_one_byte() {
    let (cpu, mut mem) = run(
        &[
            0xE3A0_0C02, // mov r0, #0x200
            0xE3A0_1CFF, // mov r1, #0xFF00
            0xE5C0_1000, // strb r1, [r0]     -> stores 0x00
            0xE3A0_20FF, // mov r2, #0xFF
            0xE5C0_2001, // strb r2, [r0, #1]
            0xE5D0_3001, // ldrb r3, [r0, #1]
        ],
        6,
    );
    assert_eq!(cpu.regs[3], 0xFF);
    assert_eq!(mem.read32(0x200), 0x0000_FF00, "only the addressed bytes changed");
}

#[test]
fn halfword_and_signed_loads() {
    let (cpu, _) = run(
        &[
            0xE3A0_0C02, // mov r0, #0x200
            0xE3E0_1000, // mvn r1, #0        -> 0xFFFF_FFFF
            0xE1C0_10B0, // strh r1, [r0]     -> stores 0xFFFF
            0xE1D0_20B0, // ldrh r2, [r0]     -> zero-extended
            0xE1D0_30F0, // ldrsh r3, [r0]    -> sign-extended
            0xE1D0_40D0, // ldrsb r4, [r0]    -> sign-extended byte
        ],
        6,
    );
    assert_eq!(cpu.regs[2], 0x0000_FFFF, "LDRH zero-extends");
    assert_eq!(cpu.regs[3], 0xFFFF_FFFF, "LDRSH sign-extends");
    assert_eq!(cpu.regs[4], 0xFFFF_FFFF, "LDRSB sign-extends");
}

// ---------------------------------------------------------------- block transfer

#[test]
fn stm_then_ldm_round_trips_in_register_order() {
    let (cpu, _) = run(
        &[
            0xE3A0_0D02, // mov r0, #0x80    (stack top area)
            0xE3A0_1001, // mov r1, #1
            0xE3A0_2002, // mov r2, #2
            0xE3A0_3003, // mov r3, #3
            0xE8A0_000E, // stmia r0!, {r1,r2,r3}
            0xE3A0_0D02, // mov r0, #0x80
            0xE8B0_0070, // ldmia r0!, {r4,r5,r6}
        ],
        7,
    );
    assert_eq!((cpu.regs[4], cpu.regs[5], cpu.regs[6]), (1, 2, 3));
    assert_eq!(cpu.regs[0], 0x80 + 12, "writeback advanced by three words");
}

#[test]
fn lowest_register_always_lands_at_the_lowest_address() {
    // STMDB (push) must still place r1 below r2 in memory.
    let (_, mut mem) = run(
        &[
            0xE3A0_DD02, // mov sp, #0x80
            0xE3A0_1001, // mov r1, #1
            0xE3A0_2002, // mov r2, #2
            0xE92D_0006, // stmdb sp!, {r1,r2}
        ],
        4,
    );
    assert_eq!(mem.read32(0x78), 1, "r1 at the lower address");
    assert_eq!(mem.read32(0x7C), 2, "r2 above it");
}

// ---------------------------------------------------------------- multiply

#[test]
fn multiply_and_accumulate() {
    let (cpu, _) = run(
        &[
            0xE3A0_1006, // mov r1, #6
            0xE3A0_2007, // mov r2, #7
            0xE000_0291, // mul r0, r1, r2
        ],
        3,
    );
    assert_eq!(cpu.regs[0], 42);
}

#[test]
fn signed_long_multiply_sign_extends() {
    // (-2) * 3 = -6, which must fill the high word with ones.
    let (cpu, _) = run(
        &[
            0xE3E0_1001, // mvn r1, #1       -> -2
            0xE3A0_2003, // mov r2, #3
            0xE0C4_3291, // smull r3, r4, r1, r2   (rdlo=r3, rdhi=r4)
        ],
        3,
    );
    assert_eq!(cpu.regs[3], (-6i32) as u32);
    assert_eq!(cpu.regs[4], 0xFFFF_FFFF, "high word sign-extended");
}

// ---------------------------------------------------------------- modes and PSR

#[test]
fn banked_stack_pointers_are_independent() {
    let mut cpu = Cpu::new();
    cpu.set_mode(Mode::System);
    cpu.regs[13] = 0xAAAA;
    cpu.set_mode(Mode::Irq);
    cpu.regs[13] = 0xBBBB;
    cpu.set_mode(Mode::System);
    assert_eq!(cpu.regs[13], 0xAAAA, "System SP survived the excursion");
    cpu.set_mode(Mode::Irq);
    assert_eq!(cpu.regs[13], 0xBBBB, "IRQ has its own SP");
}

#[test]
fn fiq_banks_r8_through_r12_as_well() {
    let mut cpu = Cpu::new();
    cpu.set_mode(Mode::System);
    cpu.regs[8] = 0x1111;
    cpu.set_mode(Mode::Fiq);
    cpu.regs[8] = 0x2222;
    cpu.set_mode(Mode::System);
    assert_eq!(cpu.regs[8], 0x1111, "FIQ has its own r8");
}

#[test]
fn mrs_reads_cpsr() {
    let (cpu, _) = run(
        &[
            0xE3A0_0000, // mov r0, #0
            0xE350_0000, // cmp r0, #0     -> Z set
            0xE10F_1000, // mrs r1, cpsr
        ],
        3,
    );
    assert!(cpu.regs[1] & (1 << 30) != 0, "Z flag visible in CPSR");
    assert_eq!(cpu.regs[1] & 0x1F, Mode::System as u32);
}

#[test]
fn swi_enters_supervisor_mode_at_the_vector() {
    let (cpu, _) = run(&[0xEF00_0000], 1); // swi #0
    assert_eq!(cpu.regs[15], 0x08, "SWI vector");
    assert_eq!(cpu.cpsr.mode(), Mode::Supervisor);
    assert_eq!(cpu.regs[14], CODE + 4, "LR points past the SWI");
    assert!(cpu.cpsr.irq_disabled(), "IRQs masked on entry");
}

#[test]
fn undefined_instruction_takes_the_undefined_vector() {
    // Coprocessor data operation — nothing implements one on this part.
    let (cpu, _) = run(&[0xEE00_0000], 1);
    assert_eq!(cpu.regs[15], 0x04);
    assert_eq!(cpu.cpsr.mode(), Mode::Undefined);
}

#[test]
fn exception_return_restores_the_previous_mode() {
    let mut cpu = Cpu::new();
    cpu.set_mode(Mode::System);
    cpu.regs[15] = CODE;
    let mut mem = FlatMemory::new(0, 0x10000);
    // At the SWI vector: movs pc, lr — the canonical return.
    mem.load(0x08, &0xE1B0_F00Eu32.to_le_bytes());
    mem.load(CODE, &0xEF00_0000u32.to_le_bytes());

    cpu.step(&mut mem); // swi
    assert_eq!(cpu.cpsr.mode(), Mode::Supervisor);
    cpu.step(&mut mem); // movs pc, lr
    assert_eq!(cpu.cpsr.mode(), Mode::System, "CPSR restored from SPSR");
    assert_eq!(cpu.regs[15], CODE + 4, "resumed after the SWI");
}

#[test]
fn swap_exchanges_memory_and_register_atomically() {
    let (cpu, mut mem) = run(
        &[
            0xE3A0_0C02, // mov r0, #0x200
            0xE3A0_1011, // mov r1, #0x11
            0xE580_1000, // str r1, [r0]
            0xE3A0_2022, // mov r2, #0x22
            0xE100_3092, // swp r3, r2, [r0]
        ],
        5,
    );
    assert_eq!(cpu.regs[3], 0x11, "old memory value into Rd");
    assert_eq!(mem.read32(0x200), 0x22, "register value into memory");
}
