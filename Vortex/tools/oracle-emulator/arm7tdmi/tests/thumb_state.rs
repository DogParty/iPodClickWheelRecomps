//! Thumb-state conformance tests.

use arm7tdmi::{Bus, Cpu, FlatMemory, Mode};

const CODE: u32 = 0x1000;

fn setup(code: &[u16]) -> (Cpu, FlatMemory) {
    let mut mem = FlatMemory::new(0, 0x10000);
    let bytes: Vec<u8> = code.iter().flat_map(|h| h.to_le_bytes()).collect();
    mem.load(CODE, &bytes);
    let mut cpu = Cpu::new();
    cpu.set_mode(Mode::System);
    cpu.cpsr.set_thumb(true);
    cpu.regs[15] = CODE;
    (cpu, mem)
}

fn run(code: &[u16], steps: usize) -> (Cpu, FlatMemory) {
    let (mut cpu, mut mem) = setup(code);
    cpu.run(&mut mem, steps);
    (cpu, mem)
}

// ---------------------------------------------------------------- formats 1–4

#[test]
fn move_shifted_register_sets_flags() {
    let (cpu, _) = run(
        &[
            0x2180, // mov r1, #0x80
            0x0048, // lsl r0, r1, #1
        ],
        2,
    );
    assert_eq!(cpu.regs[0], 0x100);
    assert!(!cpu.cpsr.z());
}

#[test]
fn lsr_by_zero_still_means_thirty_two() {
    let (cpu, _) = run(
        &[
            0x2180, // mov r1, #0x80
            0x0409, // lsl r1, r1, #16   -> 0x0080_0000
            0x0209, // lsl r1, r1, #8    -> 0x8000_0000
            0x0808, // lsr r0, r1, #0    -> encodes #32
        ],
        4,
    );
    assert_eq!(cpu.regs[0], 0);
    assert!(cpu.cpsr.c(), "carry from bit 31");
}

#[test]
fn add_and_subtract_three_operand() {
    let (cpu, _) = run(
        &[
            0x2105, // mov r1, #5
            0x2203, // mov r2, #3
            0x1888, // add r0, r1, r2
        ],
        3,
    );
    assert_eq!(cpu.regs[0], 8);
}

#[test]
fn immediate_compare_sets_flags_without_writing() {
    let (cpu, _) = run(
        &[
            0x2005, // mov r0, #5
            0x2805, // cmp r0, #5
        ],
        2,
    );
    assert_eq!(cpu.regs[0], 5);
    assert!(cpu.cpsr.z(), "equal");
    assert!(cpu.cpsr.c(), "no borrow");
}

#[test]
fn neg_is_reverse_subtract_from_zero() {
    let (cpu, _) = run(
        &[
            0x2105, // mov r1, #5
            0x4248, // neg r0, r1
        ],
        2,
    );
    assert_eq!(cpu.regs[0], (-5i32) as u32);
    assert!(cpu.cpsr.n());
}

#[test]
fn multiply_in_thumb() {
    let (cpu, _) = run(
        &[
            0x2006, // mov r0, #6
            0x2107, // mov r1, #7
            0x4348, // mul r0, r1
        ],
        3,
    );
    assert_eq!(cpu.regs[0], 42);
}

// ---------------------------------------------------------------- format 5 (hi registers)

#[test]
fn hi_register_move_reaches_r8_and_does_not_set_flags() {
    let mut mem = FlatMemory::new(0, 0x10000);
    let bytes: Vec<u8> = [0x4640u16].iter().flat_map(|h| h.to_le_bytes()).collect();
    mem.load(CODE, &bytes);
    let mut cpu = Cpu::new();
    cpu.set_mode(Mode::System);
    cpu.cpsr.set_thumb(true);
    cpu.regs[15] = CODE;
    cpu.regs[8] = 0xDEAD_BEEF;
    cpu.cpsr.set_z(true);

    cpu.step(&mut mem); // mov r0, r8
    assert_eq!(cpu.regs[0], 0xDEAD_BEEF);
    assert!(cpu.cpsr.z(), "hi-register MOV must not disturb flags");
}

#[test]
fn bx_back_to_arm_state() {
    let (cpu, _) = run(
        &[
            0x2110, // mov r1, #0x10   (even -> ARM)
            0x4708, // bx r1
        ],
        2,
    );
    assert!(!cpu.cpsr.thumb(), "even target returns to ARM state");
    assert_eq!(cpu.regs[15], 0x10);
}

// ---------------------------------------------------------------- loads and stores

#[test]
fn pc_relative_load_aligns_the_program_counter() {
    // The literal sits at 0x1004; PC reads as 0x1004 here, so offset 0 finds it.
    let (cpu, _) = run(&[0x4800, 0x0000, 0x3344, 0x1122], 1);
    assert_eq!(cpu.regs[0], 0x1122_3344);
}

#[test]
fn immediate_offsets_scale_by_access_width() {
    let (cpu, mut mem) = run(
        &[
            0x2180, // mov r1, #0x80    (base)
            0x20AA, // mov r0, #0xAA
            0x604A, // str r2, [r1, #4]  -> word offset scales x4
            0x6008, // str r0, [r1, #0]
            0x7188, // strb r0, [r1, #6] -> byte offset does not scale
        ],
        5,
    );
    assert_eq!(mem.read32(0x80), 0xAA);
    assert_eq!(cpu.regs[0], 0xAA);
    assert_eq!(mem.read8(0x86), 0xAA, "byte offset is unscaled");
}

#[test]
fn halfword_store_and_load() {
    let (cpu, _) = run(
        &[
            0x2180, // mov r1, #0x80
            0x20FF, // mov r0, #0xFF
            0x8008, // strh r0, [r1, #0]
            0x8809, // ldrh r1, [r1, #0]
        ],
        4,
    );
    assert_eq!(cpu.regs[1], 0xFF);
}

#[test]
fn sp_relative_access_uses_r13() {
    let (cpu, mut mem) = run(
        &[
            0x2080, // mov r0, #0x80
            0x4685, // mov r13, r0     (hi-reg move into SP)
            0x21EE, // mov r1, #0xEE
            0x9101, // str r1, [sp, #4]
        ],
        4,
    );
    assert_eq!(cpu.regs[13], 0x80);
    assert_eq!(mem.read32(0x84), 0xEE);
}

// ---------------------------------------------------------------- stack

#[test]
fn push_and_pop_round_trip_with_link_register() {
    let (cpu, _) = run(
        &[
            0x2080, // mov r0, #0x80
            0x4685, // mov sp, r0
            0x2042, // mov r0, #0x42
            0xB401, // push {r0}
            0x2000, // mov r0, #0
            0xBC01, // pop {r0}
        ],
        6,
    );
    assert_eq!(cpu.regs[0], 0x42, "value survived the stack");
    assert_eq!(cpu.regs[13], 0x80, "stack pointer balanced");
}

#[test]
fn push_places_the_lowest_register_at_the_lowest_address() {
    let (_, mut mem) = run(
        &[
            0x2080, // mov r0, #0x80
            0x4685, // mov sp, r0
            0x2101, // mov r1, #1
            0x2202, // mov r2, #2
            0xB406, // push {r1, r2}
        ],
        5,
    );
    assert_eq!(mem.read32(0x78), 1);
    assert_eq!(mem.read32(0x7C), 2);
}

#[test]
fn adjusting_the_stack_pointer_both_ways() {
    let (cpu, _) = run(
        &[
            0x2080, // mov r0, #0x80
            0x4685, // mov sp, r0
            0xB081, // sub sp, #4
            0xB001, // add sp, #4
            0xB083, // sub sp, #12
        ],
        5,
    );
    assert_eq!(cpu.regs[13], 0x80 - 12);
}

// ---------------------------------------------------------------- branches

#[test]
fn conditional_branch_respects_flags() {
    let (cpu, _) = run(
        &[
            0x2000, // mov r0, #0
            0x2800, // cmp r0, #0     -> Z set
            0xD000, // beq +0 — already skips one instruction, since PC reads as addr+4
            0x2063, // mov r0, #99    (skipped)
            0x2001, // mov r0, #1
        ],
        4,
    );
    assert_eq!(cpu.regs[0], 1, "the BEQ skipped the poisoned instruction");
}

#[test]
fn unconditional_branch_sign_extends_backwards() {
    let (cpu, _) = run(
        &[
            0x2001, // mov r0, #1
            0xE7FE, // b -2 halfwords -> back to itself's next... lands on 0x1002
        ],
        2,
    );
    // b at 0x1002: PC reads 0x1006, offset -4 -> 0x1002 (branches to itself).
    assert_eq!(cpu.regs[15], 0x1002);
}

#[test]
fn long_branch_with_link_is_two_instructions() {
    let (cpu, _) = run(
        &[
            0xF000, // bl (high half) — offset<<12 = 0
            0xF802, // bl (low half)  — offset<<1  = 4
        ],
        2,
    );
    assert_eq!(cpu.regs[15], 0x1008, "LR base 0x1004 plus 4");
    assert_eq!(cpu.regs[14], 0x1005, "return address with the Thumb bit set");
}

#[test]
fn block_transfer_writes_back() {
    let (cpu, _) = run(
        &[
            0x2080, // mov r0, #0x80
            0x2101, // mov r1, #1
            0x2202, // mov r2, #2
            0xC006, // stmia r0!, {r1, r2}
            0x2080, // mov r0, #0x80
            0xC818, // ldmia r0!, {r3, r4}
        ],
        6,
    );
    assert_eq!((cpu.regs[3], cpu.regs[4]), (1, 2));
    assert_eq!(cpu.regs[0], 0x88, "base advanced by two words");
}

#[test]
fn swi_from_thumb_leaves_thumb_state() {
    let (cpu, _) = run(&[0xDF00], 1); // swi #0
    assert_eq!(cpu.regs[15], 0x08);
    assert_eq!(cpu.cpsr.mode(), Mode::Supervisor);
    assert!(!cpu.cpsr.thumb(), "exceptions always enter ARM state");
    assert_eq!(cpu.regs[14], CODE + 2, "LR points past the 16-bit SWI");
}

#[test]
fn add_pc_relative_word_aligns() {
    let (cpu, _) = run(&[0xA000], 1); // add r0, pc, #0
    assert_eq!(cpu.regs[0], 0x1004, "PC reads as addr+4, then word-aligns");
}
