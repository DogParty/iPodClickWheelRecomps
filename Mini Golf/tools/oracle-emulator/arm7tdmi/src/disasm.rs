//! ARM-state disassembler, built on the same decode rules as the interpreter.
//!
//! Sharing the decode boundaries with `arm.rs` is the point: a separate disassembler would
//! eventually disagree with the emulator about what some encoding means, and we would spend a
//! day chasing a ghost in code that was executing correctly all along. When these two disagree
//! it should be about *rendering*, never about *which instruction this is*.
//!
//! Literal-pool resolution is the feature that matters for reading RetailOS: `ldr rN, [pc, #imm]`
//! is rendered with the value it actually loads, so constants appear inline instead of as
//! offsets you have to chase by hand.

use crate::bus::Bus;

const COND: [&str; 16] = [
    "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", "nv",
];
const SHIFT: [&str; 4] = ["lsl", "lsr", "asr", "ror"];
const DP_OPS: [&str; 16] = [
    "and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc", "tst", "teq", "cmp", "cmn", "orr",
    "mov", "bic", "mvn",
];

fn reg(n: u32) -> String {
    match n {
        13 => "sp".into(),
        14 => "lr".into(),
        15 => "pc".into(),
        _ => format!("r{n}"),
    }
}

#[inline]
fn bit(v: u32, n: u32) -> bool {
    v & (1 << n) != 0
}

/// Render the shifter operand of a data-processing or load/store instruction.
fn shifter(instr: u32) -> String {
    let rm = reg(instr & 0xF);
    let ty = (instr >> 5) & 3;
    if bit(instr, 4) {
        return format!("{rm}, {} {}", SHIFT[ty as usize], reg((instr >> 8) & 0xF));
    }
    let amount = (instr >> 7) & 0x1F;
    match (ty, amount) {
        (0, 0) => rm,
        (3, 0) => format!("{rm}, rrx"),
        // `lsr #0` and `asr #0` encode #32 — render what they mean, not what they say.
        (1 | 2, 0) => format!("{rm}, {} #32", SHIFT[ty as usize]),
        _ => format!("{rm}, {} #{amount}", SHIFT[ty as usize]),
    }
}

fn reg_list(list: u32) -> String {
    let mut parts = Vec::new();
    let mut i = 0u32;
    while i < 16 {
        if bit(list, i) {
            let start = i;
            while i + 1 < 16 && bit(list, i + 1) {
                i += 1;
            }
            if i > start + 1 {
                parts.push(format!("{}-{}", reg(start), reg(i)));
            } else if i == start + 1 {
                parts.push(reg(start));
                parts.push(reg(i));
            } else {
                parts.push(reg(start));
            }
        }
        i += 1;
    }
    format!("{{{}}}", parts.join(", "))
}

/// Disassemble one ARM instruction at `addr`.
///
/// `bus` is optional and used only to resolve literal pools; without it, PC-relative loads are
/// rendered as plain offsets.
pub fn arm(instr: u32, addr: u32, bus: Option<&mut dyn Bus>) -> String {
    let c = COND[(instr >> 28) as usize];
    let pc = addr.wrapping_add(8);

    // Same ordering as the interpreter's decoder — these live inside the `000` space and would
    // be mis-decoded as data processing if tested later.
    if instr & 0x0FFF_FFF0 == 0x012F_FF10 {
        return format!("bx{c}      {}", reg(instr & 0xF));
    }
    if instr & 0x0FC0_00F0 == 0x0000_0090 {
        let s = if bit(instr, 20) { "s" } else { "" };
        let (rd, rn, rs, rm) = (
            reg((instr >> 16) & 0xF),
            reg((instr >> 12) & 0xF),
            reg((instr >> 8) & 0xF),
            reg(instr & 0xF),
        );
        return if bit(instr, 21) {
            format!("mla{c}{s}    {rd}, {rm}, {rs}, {rn}")
        } else {
            format!("mul{c}{s}    {rd}, {rm}, {rs}")
        };
    }
    if instr & 0x0F80_00F0 == 0x0080_0090 {
        let op = match ((instr >> 22) & 1, (instr >> 21) & 1) {
            (0, 0) => "umull",
            (0, 1) => "umlal",
            (1, 0) => "smull",
            _ => "smlal",
        };
        let s = if bit(instr, 20) { "s" } else { "" };
        return format!(
            "{op}{c}{s}  {}, {}, {}, {}",
            reg((instr >> 12) & 0xF),
            reg((instr >> 16) & 0xF),
            reg(instr & 0xF),
            reg((instr >> 8) & 0xF)
        );
    }
    if instr & 0x0FB0_0FF0 == 0x0100_0090 {
        let b = if bit(instr, 22) { "b" } else { "" };
        return format!(
            "swp{c}{b}    {}, {}, [{}]",
            reg((instr >> 12) & 0xF),
            reg(instr & 0xF),
            reg((instr >> 16) & 0xF)
        );
    }
    if instr & 0x0E00_0090 == 0x0000_0090 && (instr >> 5) & 3 != 0 {
        return halfword(instr, c);
    }

    match (instr >> 25) & 7 {
        0 | 1 => data_processing(instr, c, addr, bus),
        2 | 3 => single_transfer(instr, c, pc, bus),
        4 => {
            let p = if bit(instr, 24) { "b" } else { "a" };
            let u = if bit(instr, 23) { "i" } else { "d" };
            let op = if bit(instr, 20) { "ldm" } else { "stm" };
            let w = if bit(instr, 21) { "!" } else { "" };
            let s = if bit(instr, 22) { "^" } else { "" };
            format!(
                "{op}{u}{p}{c} {}{w}, {}{s}",
                reg((instr >> 16) & 0xF),
                reg_list(instr & 0xFFFF)
            )
        }
        5 => {
            let l = if bit(instr, 24) { "l" } else { "" };
            let offset = (((instr & 0x00FF_FFFF) << 8) as i32 >> 6) as u32;
            format!("b{l}{c}       {:#010x}", pc.wrapping_add(offset))
        }
        7 if bit(instr, 24) => format!("svc{c}     #{:#x}", instr & 0x00FF_FFFF),
        _ => format!(".word    {instr:#010x}"),
    }
}

fn data_processing(instr: u32, c: &str, addr: u32, bus: Option<&mut dyn Bus>) -> String {
    let opcode = (instr >> 21) & 0xF;
    let set = bit(instr, 20);

    if !set && (0x8..=0xB).contains(&opcode) {
        return psr(instr, c);
    }

    let op = DP_OPS[opcode as usize];
    let s = if set { "s" } else { "" };
    let rn = reg((instr >> 16) & 0xF);
    let rd = reg((instr >> 12) & 0xF);

    let operand = if bit(instr, 25) {
        let rot = ((instr >> 8) & 0xF) * 2;
        let val = (instr & 0xFF).rotate_right(rot);
        format!("#{val:#x}")
    } else {
        shifter(instr)
    };

    let _ = (addr, bus);
    match opcode {
        // Comparisons write no destination.
        0x8..=0xB => format!("{op}{c}      {rn}, {operand}"),
        // MOV and MVN take no first operand.
        0xD | 0xF => format!("{op}{c}{s}    {rd}, {operand}"),
        _ => format!("{op}{c}{s}    {rd}, {rn}, {operand}"),
    }
}

fn psr(instr: u32, c: &str) -> String {
    let which = if bit(instr, 22) { "spsr" } else { "cpsr" };
    if bit(instr, 21) {
        let value = if bit(instr, 25) {
            let rot = ((instr >> 8) & 0xF) * 2;
            format!("#{:#x}", (instr & 0xFF).rotate_right(rot))
        } else {
            reg(instr & 0xF)
        };
        let field = (instr >> 16) & 0xF;
        let mut f = String::new();
        for (i, ch) in ['c', 'x', 's', 'f'].iter().enumerate() {
            if bit(field, i as u32) {
                f.push(*ch);
            }
        }
        format!("msr{c}     {which}_{f}, {value}")
    } else {
        format!("mrs{c}     {}, {which}", reg((instr >> 12) & 0xF))
    }
}

fn single_transfer(instr: u32, c: &str, pc: u32, bus: Option<&mut dyn Bus>) -> String {
    if bit(instr, 25) && bit(instr, 4) {
        return format!(".word    {instr:#010x}");
    }
    let op = if bit(instr, 20) { "ldr" } else { "str" };
    let b = if bit(instr, 22) { "b" } else { "" };
    let rd = reg((instr >> 12) & 0xF);
    let rn = (instr >> 16) & 0xF;
    let up = bit(instr, 23);
    let sign = if up { "" } else { "-" };

    // The case worth special-casing: a PC-relative literal load. Show the value, not the offset.
    if rn == 15 && !bit(instr, 25) && bit(instr, 24) {
        let offset = instr & 0xFFF;
        let target = if up {
            pc.wrapping_add(offset)
        } else {
            pc.wrapping_sub(offset)
        };
        return match bus {
            Some(b) => {
                let v = b.read32(target);
                format!("{op}{c}{b_}    {rd}, ={v:#010x}    ; [{target:#010x}]", b_ = b_str(instr))
            }
            None => format!("{op}{c}{b}    {rd}, [pc, #{sign}{offset:#x}]  ; {target:#010x}"),
        };
    }

    let offset = if bit(instr, 25) {
        format!("{sign}{}", shifter(instr))
    } else {
        format!("#{sign}{:#x}", instr & 0xFFF)
    };

    let w = if bit(instr, 21) { "!" } else { "" };
    if bit(instr, 24) {
        format!("{op}{c}{b}    {rd}, [{}, {offset}]{w}", reg(rn))
    } else {
        format!("{op}{c}{b}    {rd}, [{}], {offset}", reg(rn))
    }
}

fn b_str(instr: u32) -> &'static str {
    if bit(instr, 22) {
        "b"
    } else {
        ""
    }
}

fn halfword(instr: u32, c: &str) -> String {
    let op = if bit(instr, 20) { "ldr" } else { "str" };
    let kind = match (instr >> 5) & 3 {
        1 => "h",
        2 => "sb",
        _ => "sh",
    };
    let rd = reg((instr >> 12) & 0xF);
    let rn = reg((instr >> 16) & 0xF);
    let sign = if bit(instr, 23) { "" } else { "-" };
    let offset = if bit(instr, 22) {
        format!("#{sign}{:#x}", ((instr >> 4) & 0xF0) | (instr & 0xF))
    } else {
        format!("{sign}{}", reg(instr & 0xF))
    };
    let w = if bit(instr, 21) { "!" } else { "" };
    if bit(instr, 24) {
        format!("{op}{c}{kind}   {rd}, [{rn}, {offset}]{w}")
    } else {
        format!("{op}{c}{kind}   {rd}, [{rn}], {offset}")
    }
}
