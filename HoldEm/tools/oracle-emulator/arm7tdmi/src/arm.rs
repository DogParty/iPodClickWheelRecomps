//! ARM-state (32-bit) instruction decode and execute.

use crate::bus::Bus;
use crate::cpu::{cond_passes, Cpu, Exception, Mode, PSR};

#[inline]
pub(crate) fn bit(v: u32, n: u32) -> bool {
    v & (1 << n) != 0
}

/// `a + b + carry`, returning the result plus the C and V flags.
///
/// Subtraction reuses this: `a - b` is `a + !b + 1`, and `SBC` is `a + !b + C`. Deriving both
/// from one helper is what keeps the borrow and signed-overflow rules consistent — writing a
/// separate subtract path is where sign bugs come from.
#[inline]
pub(crate) fn add_with_carry(a: u32, b: u32, carry: bool) -> (u32, bool, bool) {
    let (r1, c1) = a.overflowing_add(b);
    let (r, c2) = r1.overflowing_add(carry as u32);
    let c = c1 || c2;
    let v = ((a ^ r) & (b ^ r)) & 0x8000_0000 != 0;
    (r, c, v)
}

/// Barrel shift with an immediate amount, where 0 has per-type special meanings
/// (`LSR #0` means #32, `ROR #0` means `RRX`, and so on).
pub(crate) fn shift_imm(ty: u32, val: u32, amount: u32, carry_in: bool) -> (u32, bool) {
    match ty {
        0 => {
            if amount == 0 {
                (val, carry_in)
            } else {
                (val << amount, bit(val, 32 - amount))
            }
        }
        1 => {
            if amount == 0 {
                (0, bit(val, 31)) // LSR #0 encodes LSR #32
            } else {
                (val >> amount, bit(val, amount - 1))
            }
        }
        2 => {
            if amount == 0 {
                let s = ((val as i32) >> 31) as u32; // ASR #0 encodes ASR #32
                (s, bit(val, 31))
            } else {
                (((val as i32) >> amount) as u32, bit(val, amount - 1))
            }
        }
        _ => {
            if amount == 0 {
                // ROR #0 encodes RRX: rotate right one place through the carry flag.
                (((carry_in as u32) << 31) | (val >> 1), bit(val, 0))
            } else {
                (val.rotate_right(amount), bit(val, amount - 1))
            }
        }
    }
}

/// Barrel shift with a register-supplied amount (0..=255), where 0 means "no shift"
/// and amounts of 32 or more are defined rather than special-cased away.
pub(crate) fn shift_reg(ty: u32, val: u32, amount: u32, carry_in: bool) -> (u32, bool) {
    if amount == 0 {
        return (val, carry_in);
    }
    match ty {
        0 => match amount {
            1..=31 => (val << amount, bit(val, 32 - amount)),
            32 => (0, bit(val, 0)),
            _ => (0, false),
        },
        1 => match amount {
            1..=31 => (val >> amount, bit(val, amount - 1)),
            32 => (0, bit(val, 31)),
            _ => (0, false),
        },
        2 => {
            if amount >= 32 {
                let s = ((val as i32) >> 31) as u32;
                (s, bit(val, 31))
            } else {
                (((val as i32) >> amount) as u32, bit(val, amount - 1))
            }
        }
        _ => {
            let a = amount & 31;
            if a == 0 {
                (val, bit(val, 31))
            } else {
                (val.rotate_right(a), bit(val, a - 1))
            }
        }
    }
}

pub fn execute(cpu: &mut Cpu, bus: &mut impl Bus, instr: u32) {
    if !cond_passes(instr >> 28, cpu.cpsr) {
        return;
    }

    // Order matters: several of these encodings live inside the `000` space and would be
    // mis-decoded as data processing if checked later.
    if instr & 0x0FFF_FFF0 == 0x012F_FF10 {
        return bx(cpu, instr);
    }
    if instr & 0x0FC0_00F0 == 0x0000_0090 {
        return multiply(cpu, instr);
    }
    if instr & 0x0F80_00F0 == 0x0080_0090 {
        return multiply_long(cpu, instr);
    }
    if instr & 0x0FB0_0FF0 == 0x0100_0090 {
        return swap(cpu, bus, instr);
    }
    if instr & 0x0E00_0090 == 0x0000_0090 && (instr >> 5) & 3 != 0 {
        return halfword_transfer(cpu, bus, instr);
    }

    match (instr >> 25) & 7 {
        0 | 1 => data_processing(cpu, instr),
        2 | 3 => {
            // A register-offset load/store with bit 4 set is architecturally undefined.
            if (instr >> 25) & 1 == 1 && bit(instr, 4) {
                cpu.raise(Exception::Undefined)
            } else {
                single_data_transfer(cpu, bus, instr)
            }
        }
        4 => block_data_transfer(cpu, bus, instr),
        5 => branch(cpu, instr),
        7 if bit(instr, 24) => cpu.raise(Exception::SoftwareInterrupt),
        // Coprocessor space. The PP5021C games run in system mode and do not use one;
        // taking the undefined-instruction vector is the architecturally correct response.
        _ => cpu.raise(Exception::Undefined),
    }
}

fn bx(cpu: &mut Cpu, instr: u32) {
    let target = cpu.read_reg((instr & 0xF) as usize);
    // A dispatch to address 0 is a call through an unbound function pointer. Branching there is
    // faithful — on this machine address 0 is the reset vector — but it ends the run, so under
    // `--null-dispatch=survive` it is reported as a null RETURN instead: r0 = 0, continue at lr.
    // That is the value the caller's own error path already tests for.
    if target == 0 && cpu.survive_null_dispatch {
        cpu.null_dispatches += 1;
        cpu.regs[0] = 0;
        let ret = cpu.read_reg(14);
        cpu.branch_exchange(ret);
        return;
    }
    cpu.branch_exchange(target);
}

fn branch(cpu: &mut Cpu, instr: u32) {
    let offset = (((instr & 0x00FF_FFFF) << 8) as i32 >> 6) as u32; // sign-extend 24 bits, then <<2
    if bit(instr, 24) {
        // BL: the return address is the instruction after this one.
        cpu.regs[14] = cpu.regs[15];
    }
    let target = cpu.read_reg(15).wrapping_add(offset);
    cpu.write_reg(15, target);
}

fn data_processing(cpu: &mut Cpu, instr: u32) {
    let opcode = (instr >> 21) & 0xF;
    let set_flags = bit(instr, 20);
    let rn = ((instr >> 16) & 0xF) as usize;
    let rd = ((instr >> 12) & 0xF) as usize;

    // TST/TEQ/CMP/CMN with S clear are not comparisons — they are the PSR-transfer encodings.
    if !set_flags && (0x8..=0xB).contains(&opcode) {
        return psr_transfer(cpu, instr);
    }

    let carry_in = cpu.cpsr.c();
    let (op2, shifter_carry, uses_reg_shift) = if bit(instr, 25) {
        let rot = ((instr >> 8) & 0xF) * 2;
        let val = (instr & 0xFF).rotate_right(rot);
        // A rotate of zero leaves the carry flag alone; any other rotate sets it from bit 31.
        (val, if rot == 0 { carry_in } else { bit(val, 31) }, false)
    } else {
        let ty = (instr >> 5) & 3;
        let rm = (instr & 0xF) as usize;
        if bit(instr, 4) {
            let rs = ((instr >> 8) & 0xF) as usize;
            let amount = cpu.read_reg(rs) & 0xFF;
            let val = cpu.read_reg_shifted(rm);
            let (v, c) = shift_reg(ty, val, amount, carry_in);
            (v, c, true)
        } else {
            let amount = (instr >> 7) & 0x1F;
            let val = cpu.read_reg(rm);
            let (v, c) = shift_imm(ty, val, amount, carry_in);
            (v, c, false)
        }
    };

    // Rn is read with the same pipeline displacement the operand used.
    let a = if uses_reg_shift {
        cpu.read_reg_shifted(rn)
    } else {
        cpu.read_reg(rn)
    };

    let mut logical = true;
    let (result, c, v) = match opcode {
        0x0 => (a & op2, shifter_carry, cpu.cpsr.v()),
        0x1 => (a ^ op2, shifter_carry, cpu.cpsr.v()),
        0x2 => {
            logical = false;
            add_with_carry(a, !op2, true)
        }
        0x3 => {
            logical = false;
            add_with_carry(op2, !a, true)
        }
        0x4 => {
            logical = false;
            add_with_carry(a, op2, false)
        }
        0x5 => {
            logical = false;
            add_with_carry(a, op2, carry_in)
        }
        0x6 => {
            logical = false;
            add_with_carry(a, !op2, carry_in)
        }
        0x7 => {
            logical = false;
            add_with_carry(op2, !a, carry_in)
        }
        0x8 => (a & op2, shifter_carry, cpu.cpsr.v()),
        0x9 => (a ^ op2, shifter_carry, cpu.cpsr.v()),
        0xA => {
            logical = false;
            add_with_carry(a, !op2, true)
        }
        0xB => {
            logical = false;
            add_with_carry(a, op2, false)
        }
        0xC => (a | op2, shifter_carry, cpu.cpsr.v()),
        0xD => (op2, shifter_carry, cpu.cpsr.v()),
        0xE => (a & !op2, shifter_carry, cpu.cpsr.v()),
        _ => (!op2, shifter_carry, cpu.cpsr.v()),
    };

    let writes_result = !(0x8..=0xB).contains(&opcode);

    if set_flags && rd == 15 && writes_result {
        // `MOVS pc, lr` and friends: this is the exception-return path, not a flag update.
        cpu.restore_cpsr();
        cpu.write_reg(15, result);
        return;
    }

    if set_flags {
        cpu.set_nz(result);
        cpu.cpsr.set_c(c);
        if !logical {
            cpu.cpsr.set_v(v);
        }
    }
    if writes_result {
        cpu.write_reg(rd, result);
    }
}

fn psr_transfer(cpu: &mut Cpu, instr: u32) {
    let spsr = bit(instr, 22);
    if bit(instr, 21) {
        // MSR
        let value = if bit(instr, 25) {
            let rot = ((instr >> 8) & 0xF) * 2;
            (instr & 0xFF).rotate_right(rot)
        } else {
            cpu.read_reg((instr & 0xF) as usize)
        };

        let field = (instr >> 16) & 0xF;
        let mut mask = 0u32;
        if bit(field, 0) {
            mask |= 0x0000_00FF;
        }
        if bit(field, 1) {
            mask |= 0x0000_FF00;
        }
        if bit(field, 2) {
            mask |= 0x00FF_0000;
        }
        if bit(field, 3) {
            mask |= 0xFF00_0000;
        }

        if spsr {
            let cur = cpu.spsr().0;
            cpu.set_spsr(PSR((cur & !mask) | (value & mask)));
        } else {
            // User mode may only change the condition flags, whatever the field mask asks for.
            if cpu.cpsr.mode() == Mode::User {
                mask &= 0xF000_0000;
            }
            let new = (cpu.cpsr.0 & !mask) | (value & mask);
            // A mode change has to swap register banks, so route it through set_mode.
            if mask & 0x1F != 0 {
                if let Some(m) = Mode::from_bits(new) {
                    cpu.set_mode(m);
                }
            }
            cpu.cpsr = PSR((cpu.cpsr.0 & !mask) | (value & mask));
        }
    } else {
        // MRS
        let rd = ((instr >> 12) & 0xF) as usize;
        let val = if spsr { cpu.spsr().0 } else { cpu.cpsr.0 };
        cpu.write_reg(rd, val);
    }
}

fn multiply(cpu: &mut Cpu, instr: u32) {
    let rd = ((instr >> 16) & 0xF) as usize;
    let rn = ((instr >> 12) & 0xF) as usize;
    let rs = ((instr >> 8) & 0xF) as usize;
    let rm = (instr & 0xF) as usize;

    let mut result = cpu.read_reg(rm).wrapping_mul(cpu.read_reg(rs));
    if bit(instr, 21) {
        result = result.wrapping_add(cpu.read_reg(rn));
    }
    cpu.write_reg(rd, result);
    if bit(instr, 20) {
        cpu.set_nz(result);
        // C is architecturally UNPREDICTABLE after a multiply; V is preserved.
    }
}

fn multiply_long(cpu: &mut Cpu, instr: u32) {
    let rdhi = ((instr >> 16) & 0xF) as usize;
    let rdlo = ((instr >> 12) & 0xF) as usize;
    let rs = ((instr >> 8) & 0xF) as usize;
    let rm = (instr & 0xF) as usize;

    let signed = bit(instr, 22);
    let accumulate = bit(instr, 21);

    let a = cpu.read_reg(rm);
    let b = cpu.read_reg(rs);
    let mut result = if signed {
        ((a as i32 as i64).wrapping_mul(b as i32 as i64)) as u64
    } else {
        (a as u64).wrapping_mul(b as u64)
    };

    if accumulate {
        let prev = ((cpu.read_reg(rdhi) as u64) << 32) | cpu.read_reg(rdlo) as u64;
        result = result.wrapping_add(prev);
    }

    cpu.write_reg(rdlo, result as u32);
    cpu.write_reg(rdhi, (result >> 32) as u32);
    if bit(instr, 20) {
        cpu.cpsr.set_n(result & 0x8000_0000_0000_0000 != 0);
        cpu.cpsr.set_z(result == 0);
    }
}

fn swap(cpu: &mut Cpu, bus: &mut impl Bus, instr: u32) {
    let rn = ((instr >> 16) & 0xF) as usize;
    let rd = ((instr >> 12) & 0xF) as usize;
    let rm = (instr & 0xF) as usize;
    let addr = cpu.read_reg(rn);
    let src = cpu.read_reg(rm);

    if bit(instr, 22) {
        let old = bus.read8(addr);
        bus.write8(addr, src as u8);
        cpu.write_reg(rd, old as u32);
    } else {
        let old = bus.read32_rotated(addr);
        bus.write32(addr, src);
        cpu.write_reg(rd, old);
    }
}

fn single_data_transfer(cpu: &mut Cpu, bus: &mut impl Bus, instr: u32) {
    let pre = bit(instr, 24);
    let up = bit(instr, 23);
    let byte = bit(instr, 22);
    let writeback = bit(instr, 21);
    let load = bit(instr, 20);
    let rn = ((instr >> 16) & 0xF) as usize;
    let rd = ((instr >> 12) & 0xF) as usize;

    let offset = if bit(instr, 25) {
        // Register offset — immediate shift amounts only.
        let ty = (instr >> 5) & 3;
        let amount = (instr >> 7) & 0x1F;
        let val = cpu.read_reg((instr & 0xF) as usize);
        shift_imm(ty, val, amount, cpu.cpsr.c()).0
    } else {
        instr & 0xFFF
    };

    let base = cpu.read_reg(rn);
    let offset_addr = if up {
        base.wrapping_add(offset)
    } else {
        base.wrapping_sub(offset)
    };
    let addr = if pre { offset_addr } else { base };

    if load {
        // Load into a temporary first: with `Rd == Rn` and writeback, the loaded value must win.
        let value = if byte {
            bus.read8(addr) as u32
        } else {
            bus.read32_rotated(addr)
        };
        if !pre || writeback {
            cpu.write_reg(rn, offset_addr);
        }
        cpu.write_reg(rd, value);
    } else {
        // Storing R15 writes PC+12 on ARM7TDMI — one word beyond the usual operand value.
        let value = if rd == 15 {
            cpu.read_reg(15).wrapping_add(4)
        } else {
            cpu.read_reg(rd)
        };
        if byte {
            bus.write8(addr, value as u8);
        } else {
            bus.write32(addr, value);
        }
        if !pre || writeback {
            cpu.write_reg(rn, offset_addr);
        }
    }
}

fn halfword_transfer(cpu: &mut Cpu, bus: &mut impl Bus, instr: u32) {
    let pre = bit(instr, 24);
    let up = bit(instr, 23);
    let imm = bit(instr, 22);
    let writeback = bit(instr, 21);
    let load = bit(instr, 20);
    let rn = ((instr >> 16) & 0xF) as usize;
    let rd = ((instr >> 12) & 0xF) as usize;
    let sh = (instr >> 5) & 3;

    let offset = if imm {
        ((instr >> 4) & 0xF0) | (instr & 0xF)
    } else {
        cpu.read_reg((instr & 0xF) as usize)
    };

    let base = cpu.read_reg(rn);
    let offset_addr = if up {
        base.wrapping_add(offset)
    } else {
        base.wrapping_sub(offset)
    };
    let addr = if pre { offset_addr } else { base };

    if load {
        let value = match sh {
            1 => bus.read16_rotated(addr),
            2 => bus.read8(addr) as i8 as i32 as u32,
            _ => {
                // LDRSH from an odd address: ARM7TDMI sign-extends the *byte* at that address
                // rather than faulting. Architecturally UNPREDICTABLE, but real code has been
                // known to depend on the ARM7 behaviour, so match the silicon.
                if addr & 1 != 0 {
                    bus.read8(addr) as i8 as i32 as u32
                } else {
                    bus.read16(addr) as i16 as i32 as u32
                }
            }
        };
        if !pre || writeback {
            cpu.write_reg(rn, offset_addr);
        }
        cpu.write_reg(rd, value);
    } else {
        let value = if rd == 15 {
            cpu.read_reg(15).wrapping_add(4)
        } else {
            cpu.read_reg(rd)
        };
        bus.write16(addr, value as u16);
        if !pre || writeback {
            cpu.write_reg(rn, offset_addr);
        }
    }
}

fn block_data_transfer(cpu: &mut Cpu, bus: &mut impl Bus, instr: u32) {
    let pre = bit(instr, 24);
    let up = bit(instr, 23);
    let s = bit(instr, 22);
    let writeback = bit(instr, 21);
    let load = bit(instr, 20);
    let rn = ((instr >> 16) & 0xF) as usize;
    let list = instr & 0xFFFF;

    let base = cpu.read_reg(rn);
    let count = list.count_ones();

    // An empty register list transfers R15 alone but adjusts the base by 0x40 — an ARM7
    // quirk that compilers never emit but hand-written assembly occasionally does.
    let (regs, bytes): (Vec<usize>, u32) = if list == 0 {
        (vec![15], 0x40)
    } else {
        ((0..16).filter(|i| bit(list, *i as u32)).collect(), count * 4)
    };

    // Registers always map lowest-to-lowest-address regardless of direction.
    let start = if up {
        base.wrapping_add(if pre { 4 } else { 0 })
    } else {
        base.wrapping_sub(bytes).wrapping_add(if pre { 0 } else { 4 })
    };
    let final_base = if up {
        base.wrapping_add(bytes)
    } else {
        base.wrapping_sub(bytes)
    };

    // `S` without R15 in the list transfers the *user-mode* bank. Swap into it for the
    // duration, then swap back — the base register itself still comes from the current mode.
    let transfers_user_bank = s && !(load && bit(list, 15));
    let saved_mode = cpu.cpsr.mode();
    if transfers_user_bank && saved_mode != Mode::User {
        cpu.switch_mode(saved_mode, Mode::User);
    }

    let mut addr = start;
    for &r in &regs {
        if load {
            let value = bus.read32(addr);
            cpu.regs[r] = value;
            if r == 15 {
                cpu.regs[15] = value & !3;
                cpu.branched = true;
            }
        } else {
            // STM stores the base *after* writeback only if the base is not first in the list.
            let value = if r == 15 {
                cpu.read_reg(15).wrapping_add(4)
            } else if r == rn && writeback && regs.first() != Some(&rn) {
                final_base
            } else {
                cpu.regs[r]
            };
            bus.write32(addr, value);
        }
        addr = addr.wrapping_add(4);
    }

    if transfers_user_bank && saved_mode != Mode::User {
        cpu.switch_mode(Mode::User, saved_mode);
    }

    if writeback && !(load && bit(list, rn as u32)) {
        cpu.regs[rn] = final_base;
    }

    // `LDM ... {..., pc}^` restores CPSR from SPSR — the other exception-return path.
    if load && s && bit(list, 15) {
        cpu.restore_cpsr();
    }
}
