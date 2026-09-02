//! Thumb-state (16-bit) instruction decode and execute.
//!
//! Thumb reuses the ARM barrel-shifter and add-with-carry helpers rather than reimplementing
//! them — the flag rules are identical and duplicating them is how the two states drift apart.

use crate::arm::{add_with_carry, bit, shift_imm, shift_reg};
use crate::bus::Bus;
use crate::cpu::{cond_passes, Cpu, Exception};

pub fn execute(cpu: &mut Cpu, bus: &mut impl Bus, instr: u16) {
    let i = instr as u32;
    match instr >> 13 {
        0b000 => {
            if (i >> 11) & 3 == 3 {
                add_subtract(cpu, i)
            } else {
                move_shifted(cpu, i)
            }
        }
        0b001 => immediate_ops(cpu, i),
        0b010 => match (i >> 10) & 7 {
            0b000 => alu_ops(cpu, i),
            0b001 => hi_register_ops(cpu, i),
            0b010 | 0b011 => pc_relative_load(cpu, bus, i),
            _ => {
                if bit(i, 9) {
                    load_store_sign_extended(cpu, bus, i)
                } else {
                    load_store_register_offset(cpu, bus, i)
                }
            }
        },
        0b011 => load_store_immediate(cpu, bus, i),
        0b100 => {
            if bit(i, 12) {
                sp_relative_load_store(cpu, bus, i)
            } else {
                load_store_halfword(cpu, bus, i)
            }
        }
        0b101 => {
            if !bit(i, 12) {
                load_address(cpu, i)
            } else if (i >> 8) & 0xF == 0b0000 {
                adjust_stack_pointer(cpu, i)
            } else if (i >> 9) & 3 == 0b10 {
                push_pop(cpu, bus, i)
            } else {
                cpu.raise(Exception::Undefined)
            }
        }
        0b110 => {
            if bit(i, 12) {
                conditional_branch(cpu, i)
            } else {
                block_transfer(cpu, bus, i)
            }
        }
        _ => {
            if !bit(i, 12) {
                unconditional_branch(cpu, i)
            } else {
                long_branch_link(cpu, i)
            }
        }
    }
}

/// Format 1 — `LSL`/`LSR`/`ASR` by an immediate.
fn move_shifted(cpu: &mut Cpu, i: u32) {
    let ty = (i >> 11) & 3;
    let amount = (i >> 6) & 0x1F;
    let rs = ((i >> 3) & 7) as usize;
    let rd = (i & 7) as usize;

    let (result, carry) = shift_imm(ty, cpu.read_reg(rs), amount, cpu.cpsr.c());
    cpu.set_nz(result);
    cpu.cpsr.set_c(carry);
    cpu.write_reg(rd, result);
}

/// Format 2 — `ADD`/`SUB` with a register or 3-bit immediate.
fn add_subtract(cpu: &mut Cpu, i: u32) {
    let immediate = bit(i, 10);
    let subtract = bit(i, 9);
    let operand = (i >> 6) & 7;
    let rs = ((i >> 3) & 7) as usize;
    let rd = (i & 7) as usize;

    let a = cpu.read_reg(rs);
    let b = if immediate {
        operand
    } else {
        cpu.read_reg(operand as usize)
    };

    let (result, c, v) = if subtract {
        add_with_carry(a, !b, true)
    } else {
        add_with_carry(a, b, false)
    };
    cpu.set_nz(result);
    cpu.cpsr.set_c(c);
    cpu.cpsr.set_v(v);
    cpu.write_reg(rd, result);
}

/// Format 3 — `MOV`/`CMP`/`ADD`/`SUB` against an 8-bit immediate.
fn immediate_ops(cpu: &mut Cpu, i: u32) {
    let op = (i >> 11) & 3;
    let rd = ((i >> 8) & 7) as usize;
    let imm = i & 0xFF;
    let a = cpu.read_reg(rd);

    let (result, c, v, writes) = match op {
        0 => (imm, cpu.cpsr.c(), cpu.cpsr.v(), true),
        1 => {
            let (r, c, v) = add_with_carry(a, !imm, true);
            (r, c, v, false)
        }
        2 => {
            let (r, c, v) = add_with_carry(a, imm, false);
            (r, c, v, true)
        }
        _ => {
            let (r, c, v) = add_with_carry(a, !imm, true);
            (r, c, v, true)
        }
    };

    cpu.set_nz(result);
    if op != 0 {
        cpu.cpsr.set_c(c);
        cpu.cpsr.set_v(v);
    }
    if writes {
        cpu.write_reg(rd, result);
    }
}

/// Format 4 — the 16 register-to-register ALU operations.
fn alu_ops(cpu: &mut Cpu, i: u32) {
    let op = (i >> 6) & 0xF;
    let rs = ((i >> 3) & 7) as usize;
    let rd = (i & 7) as usize;
    let a = cpu.read_reg(rd);
    let b = cpu.read_reg(rs);
    let carry_in = cpu.cpsr.c();

    let mut arithmetic = false;
    let mut writes = true;
    let (result, c, v) = match op {
        0x0 => (a & b, carry_in, cpu.cpsr.v()),
        0x1 => (a ^ b, carry_in, cpu.cpsr.v()),
        0x2 => {
            let (r, c) = shift_reg(0, a, b & 0xFF, carry_in);
            (r, c, cpu.cpsr.v())
        }
        0x3 => {
            let (r, c) = shift_reg(1, a, b & 0xFF, carry_in);
            (r, c, cpu.cpsr.v())
        }
        0x4 => {
            let (r, c) = shift_reg(2, a, b & 0xFF, carry_in);
            (r, c, cpu.cpsr.v())
        }
        0x5 => {
            arithmetic = true;
            add_with_carry(a, b, carry_in)
        }
        0x6 => {
            arithmetic = true;
            add_with_carry(a, !b, carry_in)
        }
        0x7 => {
            let (r, c) = shift_reg(3, a, b & 0xFF, carry_in);
            (r, c, cpu.cpsr.v())
        }
        0x8 => {
            writes = false;
            (a & b, carry_in, cpu.cpsr.v())
        }
        0x9 => {
            arithmetic = true;
            add_with_carry(0, !b, true) // NEG == RSB #0
        }
        0xA => {
            arithmetic = true;
            writes = false;
            add_with_carry(a, !b, true)
        }
        0xB => {
            arithmetic = true;
            writes = false;
            add_with_carry(a, b, false)
        }
        0xC => (a | b, carry_in, cpu.cpsr.v()),
        0xD => (a.wrapping_mul(b), carry_in, cpu.cpsr.v()),
        0xE => (a & !b, carry_in, cpu.cpsr.v()),
        _ => (!b, carry_in, cpu.cpsr.v()),
    };

    cpu.set_nz(result);
    cpu.cpsr.set_c(c);
    if arithmetic {
        cpu.cpsr.set_v(v);
    }
    if writes {
        cpu.write_reg(rd, result);
    }
}

/// Format 5 — operations that can reach the high registers, plus `BX`. These do *not* set flags
/// (except `CMP`), which is what makes them the only way to move R8–R15 around in Thumb.
fn hi_register_ops(cpu: &mut Cpu, i: u32) {
    let op = (i >> 8) & 3;
    let rd = ((i & 7) | ((i >> 4) & 8)) as usize;
    let rs = (((i >> 3) & 7) | ((i >> 3) & 8)) as usize;

    match op {
        0 => {
            let r = cpu.read_reg(rd).wrapping_add(cpu.read_reg(rs));
            cpu.write_reg(rd, r);
        }
        1 => {
            let (r, c, v) = add_with_carry(cpu.read_reg(rd), !cpu.read_reg(rs), true);
            cpu.set_nz(r);
            cpu.cpsr.set_c(c);
            cpu.cpsr.set_v(v);
        }
        2 => {
            let r = cpu.read_reg(rs);
            cpu.write_reg(rd, r);
        }
        _ => {
            let target = cpu.read_reg(rs);
            cpu.branch_exchange(target);
        }
    }
}

/// Format 6 — `LDR Rd, [PC, #imm]`. The PC is word-aligned first, which matters whenever the
/// instruction itself sits at a non-word address.
fn pc_relative_load(cpu: &mut Cpu, bus: &mut impl Bus, i: u32) {
    let rd = ((i >> 8) & 7) as usize;
    let addr = (cpu.read_reg(15) & !3).wrapping_add((i & 0xFF) * 4);
    let value = bus.read32(addr);
    cpu.write_reg(rd, value);
}

/// Format 7 — `LDR`/`STR`/`LDRB`/`STRB` with a register offset.
fn load_store_register_offset(cpu: &mut Cpu, bus: &mut impl Bus, i: u32) {
    let load = bit(i, 11);
    let byte = bit(i, 10);
    let ro = ((i >> 6) & 7) as usize;
    let rb = ((i >> 3) & 7) as usize;
    let rd = (i & 7) as usize;
    let addr = cpu.read_reg(rb).wrapping_add(cpu.read_reg(ro));

    match (load, byte) {
        (true, true) => {
            let v = bus.read8(addr) as u32;
            cpu.write_reg(rd, v);
        }
        (true, false) => {
            let v = bus.read32_rotated(addr);
            cpu.write_reg(rd, v);
        }
        (false, true) => bus.write8(addr, cpu.read_reg(rd) as u8),
        (false, false) => bus.write32(addr, cpu.read_reg(rd)),
    }
}

/// Format 8 — `STRH`/`LDRH`/`LDRSB`/`LDRSH` with a register offset.
fn load_store_sign_extended(cpu: &mut Cpu, bus: &mut impl Bus, i: u32) {
    let op = (i >> 10) & 3;
    let ro = ((i >> 6) & 7) as usize;
    let rb = ((i >> 3) & 7) as usize;
    let rd = (i & 7) as usize;
    let addr = cpu.read_reg(rb).wrapping_add(cpu.read_reg(ro));

    match op {
        0 => bus.write16(addr, cpu.read_reg(rd) as u16),
        1 => {
            let v = bus.read8(addr) as i8 as i32 as u32;
            cpu.write_reg(rd, v);
        }
        2 => {
            let v = bus.read16_rotated(addr);
            cpu.write_reg(rd, v);
        }
        _ => {
            // Same ARM7 oddity as the ARM-state LDRSH: an odd address sign-extends a byte.
            let v = if addr & 1 != 0 {
                bus.read8(addr) as i8 as i32 as u32
            } else {
                bus.read16(addr) as i16 as i32 as u32
            };
            cpu.write_reg(rd, v);
        }
    }
}

/// Format 9 — `LDR`/`STR`/`LDRB`/`STRB` with a 5-bit immediate. Word offsets scale by 4,
/// byte offsets do not.
fn load_store_immediate(cpu: &mut Cpu, bus: &mut impl Bus, i: u32) {
    let byte = bit(i, 12);
    let load = bit(i, 11);
    let offset = (i >> 6) & 0x1F;
    let rb = ((i >> 3) & 7) as usize;
    let rd = (i & 7) as usize;

    let addr = if byte {
        cpu.read_reg(rb).wrapping_add(offset)
    } else {
        cpu.read_reg(rb).wrapping_add(offset * 4)
    };

    match (load, byte) {
        (true, true) => {
            let v = bus.read8(addr) as u32;
            cpu.write_reg(rd, v);
        }
        (true, false) => {
            let v = bus.read32_rotated(addr);
            cpu.write_reg(rd, v);
        }
        (false, true) => bus.write8(addr, cpu.read_reg(rd) as u8),
        (false, false) => bus.write32(addr, cpu.read_reg(rd)),
    }
}

/// Format 10 — `LDRH`/`STRH` with a 5-bit immediate, scaled by 2.
fn load_store_halfword(cpu: &mut Cpu, bus: &mut impl Bus, i: u32) {
    let load = bit(i, 11);
    let offset = ((i >> 6) & 0x1F) * 2;
    let rb = ((i >> 3) & 7) as usize;
    let rd = (i & 7) as usize;
    let addr = cpu.read_reg(rb).wrapping_add(offset);

    if load {
        let v = bus.read16_rotated(addr);
        cpu.write_reg(rd, v);
    } else {
        bus.write16(addr, cpu.read_reg(rd) as u16);
    }
}

/// Format 11 — `LDR`/`STR` relative to SP.
fn sp_relative_load_store(cpu: &mut Cpu, bus: &mut impl Bus, i: u32) {
    let load = bit(i, 11);
    let rd = ((i >> 8) & 7) as usize;
    let addr = cpu.read_reg(13).wrapping_add((i & 0xFF) * 4);

    if load {
        let v = bus.read32_rotated(addr);
        cpu.write_reg(rd, v);
    } else {
        bus.write32(addr, cpu.read_reg(rd));
    }
}

/// Format 12 — `ADD Rd, PC/SP, #imm`.
fn load_address(cpu: &mut Cpu, i: u32) {
    let use_sp = bit(i, 11);
    let rd = ((i >> 8) & 7) as usize;
    let offset = (i & 0xFF) * 4;
    let base = if use_sp {
        cpu.read_reg(13)
    } else {
        cpu.read_reg(15) & !3
    };
    cpu.write_reg(rd, base.wrapping_add(offset));
}

/// Format 13 — `ADD SP, #±imm`.
fn adjust_stack_pointer(cpu: &mut Cpu, i: u32) {
    let offset = (i & 0x7F) * 4;
    let sp = cpu.read_reg(13);
    let new = if bit(i, 7) {
        sp.wrapping_sub(offset)
    } else {
        sp.wrapping_add(offset)
    };
    cpu.write_reg(13, new);
}

/// Format 14 — `PUSH`/`POP`, i.e. a full-descending stack on SP.
fn push_pop(cpu: &mut Cpu, bus: &mut impl Bus, i: u32) {
    let load = bit(i, 11);
    let extra = bit(i, 8); // LR on push, PC on pop
    let list = i & 0xFF;

    let mut regs: Vec<usize> = (0..8).filter(|r| bit(list, *r as u32)).collect();
    let mut sp = cpu.read_reg(13);

    if load {
        if extra {
            regs.push(15);
        }
        for &r in &regs {
            let v = bus.read32(sp);
            if r == 15 {
                // ARMv4T pops into PC without interworking — bit 0 is simply discarded.
                cpu.regs[15] = v & !1;
                cpu.branched = true;
            } else {
                cpu.regs[r] = v;
            }
            sp = sp.wrapping_add(4);
        }
    } else {
        if extra {
            regs.push(14);
        }
        // Descending: reserve the space first, then fill upward so the lowest register
        // still lands at the lowest address.
        sp = sp.wrapping_sub(4 * regs.len() as u32);
        let mut addr = sp;
        for &r in &regs {
            bus.write32(addr, cpu.read_reg(r));
            addr = addr.wrapping_add(4);
        }
    }
    cpu.regs[13] = sp;
}

/// Format 15 — `LDMIA`/`STMIA Rb!`.
fn block_transfer(cpu: &mut Cpu, bus: &mut impl Bus, i: u32) {
    let load = bit(i, 11);
    let rb = ((i >> 8) & 7) as usize;
    let list = i & 0xFF;

    let mut addr = cpu.read_reg(rb);
    if list == 0 {
        // Empty list: transfer PC and advance the base by 0x40, matching ARM-state LDM/STM.
        if load {
            let v = bus.read32(addr);
            cpu.regs[15] = v & !1;
            cpu.branched = true;
        } else {
            bus.write32(addr, cpu.read_reg(15).wrapping_add(2));
        }
        cpu.regs[rb] = addr.wrapping_add(0x40);
        return;
    }

    let regs: Vec<usize> = (0..8).filter(|r| bit(list, *r as u32)).collect();
    let writes_back = !(load && regs.contains(&rb));
    for &r in &regs {
        if load {
            cpu.regs[r] = bus.read32(addr);
        } else {
            bus.write32(addr, cpu.read_reg(r));
        }
        addr = addr.wrapping_add(4);
    }
    if writes_back {
        cpu.regs[rb] = addr;
    }
}

/// Format 16/17 — conditional branch, with `0b1111` stolen for `SWI`.
fn conditional_branch(cpu: &mut Cpu, i: u32) {
    let cond = (i >> 8) & 0xF;
    if cond == 0xF {
        return cpu.raise(Exception::SoftwareInterrupt);
    }
    if cond == 0xE {
        return cpu.raise(Exception::Undefined);
    }
    if !cond_passes(cond, cpu.cpsr) {
        return;
    }
    let offset = ((i & 0xFF) as u8 as i8 as i32) << 1;
    let target = cpu.read_reg(15).wrapping_add(offset as u32);
    cpu.write_reg(15, target);
}

/// Format 18 — unconditional branch with an 11-bit offset.
fn unconditional_branch(cpu: &mut Cpu, i: u32) {
    let offset = (((i & 0x7FF) << 21) as i32 >> 20) as u32; // sign-extend 11 bits, then <<1
    let target = cpu.read_reg(15).wrapping_add(offset);
    cpu.write_reg(15, target);
}

/// Format 19 — `BL`, assembled as two independent 16-bit instructions. The first stages the
/// high half of the offset in LR; the second completes the branch.
fn long_branch_link(cpu: &mut Cpu, i: u32) {
    if !bit(i, 11) {
        let offset = (((i & 0x7FF) << 21) as i32 >> 9) as u32; // sign-extend, then <<12
        cpu.regs[14] = cpu.read_reg(15).wrapping_add(offset);
    } else {
        let return_addr = cpu.regs[15] | 1;
        let target = cpu.regs[14].wrapping_add((i & 0x7FF) << 1);
        cpu.regs[14] = return_addr;
        cpu.write_reg(15, target);
    }
}
