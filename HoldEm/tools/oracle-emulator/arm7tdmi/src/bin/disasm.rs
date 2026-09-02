//! Disassemble a range of a raw ARM binary.
//!
//! Exists because no usable disassembler is available here — `llvm-objdump` refuses raw binary
//! and `llvm-mc` is not installed. Built on the emulator's own decoder so the two cannot
//! disagree about what an instruction is.
//!
//!   disasm <file> <file_offset> <count_bytes> [vma_of_file_offset]
//!
//! Example — the RetailOS eApp loader, whose OSOS offset 0x122708 maps to address 0x10122708:
//!   disasm OSOS.bin 0x122600 0x200 0x10122600

use std::env;
use std::fs;

use arm7tdmi::{disasm, Bus, FlatMemory};

fn parse(s: &str) -> Option<u32> {
    let s = s.trim();
    match s.strip_prefix("0x") {
        Some(hex) => u32::from_str_radix(hex, 16).ok(),
        None => s.parse().ok(),
    }
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.len() < 3 {
        eprintln!("usage: disasm <file> <file_offset> <count_bytes> [vma]");
        std::process::exit(2);
    }

    let data = match fs::read(&args[0]) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("{}: {e}", args[0]);
            std::process::exit(1);
        }
    };

    let off = parse(&args[1]).expect("bad offset") as usize;
    let count = parse(&args[2]).expect("bad count") as usize;
    let vma = args.get(3).and_then(|s| parse(s)).unwrap_or(off as u32);

    // Map the whole file at the VMA implied by the requested slice, so literal-pool loads
    // resolve against real data rather than being rendered as bare offsets.
    let base = vma.wrapping_sub(off as u32);
    let mut mem = FlatMemory {
        base,
        data: data.clone(),
    };

    let end = (off + count).min(data.len());
    let mut addr = vma;
    for i in (off..end).step_by(4) {
        if i + 4 > data.len() {
            break;
        }
        let instr = u32::from_le_bytes([data[i], data[i + 1], data[i + 2], data[i + 3]]);
        let text = disasm::arm(instr, addr, Some(&mut mem));

        // Show the ASCII too — literal pools sit inline in ARM code, and seeing "eapp" next to
        // its own word is what identified the loader in the first place.
        let ascii: String = data[i..i + 4]
            .iter()
            .map(|&c| if (0x20..0x7f).contains(&c) { c as char } else { '.' })
            .collect();

        println!("{addr:08x}  {instr:08x}  |{ascii}|  {text}");
        addr = addr.wrapping_add(4);
    }
}

// `FlatMemory` needs its fields public for this to work; the read is only ever a lookup.
#[allow(dead_code)]
fn _assert_bus(m: &mut FlatMemory) -> u32 {
    m.read32(0)
}
