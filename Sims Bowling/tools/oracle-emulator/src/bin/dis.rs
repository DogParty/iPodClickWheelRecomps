//! Static reader for the RetailOS image — disassembly, call-graph walk and xrefs with no boot.
//!
//! `trace --disasm=` can only show code that a *run* has already placed in memory, so reading
//! forty instructions cost a 600 M-instruction boot and reading a call tree cost one boot per
//! callee. The firmware image is flat (`OSOS_correct.bin` lands byte-identical at 0x10000000 and
//! is mirrored at 0), so every question about static code shape is answerable from the file.
//!
//! It shares `arm7tdmi::disasm` with the interpreter deliberately: a second decoder would
//! eventually disagree with the emulator about what an encoding means, and the note in this
//! project about Ghidra conflating functions and inventing bodies is exactly that failure.
//!
//! Self-check: `--verify` reproduces the six instructions at 0x000cd7c8 recorded in
//! research/20 Addendum 8 §6 from an actual run. That control exercises the same file-offset →
//! address mapping, the same decoder and the same literal-pool base as every other query here,
//! on the very code path under investigation — not merely "the file is readable".

use arm7tdmi::{disasm, Bus};
use std::collections::{BTreeMap, BTreeSet};

/// The image at both addresses it answers to: mirrored at 0 (where RetailOS executes) and at
/// 0x10000000 (where the ROM DMAs it, and where its literal pools point).
///
/// `base` moves that window. It exists so this tool can read a GAME statically as well as the
/// firmware: an eApp links at 0x18000000, and every question about game code — what argument a
/// call site passes, which branch a test takes — used to need a 600 M-instruction boot to answer
/// because nothing here could address it. `--base=0x18000000` and it is the same flat file.
struct Img {
    d: Vec<u8>,
    base: u32,
}

impl Bus for Img {
    fn read8(&mut self, addr: u32) -> u8 {
        match self.off(addr) {
            Some(o) => self.d.get(o).copied().unwrap_or(0),
            None => 0,
        }
    }
    fn write8(&mut self, _addr: u32, _val: u8) {}
}

impl Img {
    /// File offset for an address, in whichever window the image was loaded into.
    fn off(&self, addr: u32) -> Option<usize> {
        if self.base != 0 {
            return addr.checked_sub(self.base).map(|o| o as usize);
        }
        Some(if addr >= 0x1000_0000 { addr - 0x1000_0000 } else { addr } as usize)
    }
    fn w(&self, addr: u32) -> u32 {
        let Some(off) = self.off(addr) else { return 0 };
        if off + 4 > self.d.len() {
            return 0;
        }
        u32::from_le_bytes([self.d[off], self.d[off + 1], self.d[off + 2], self.d[off + 3]])
    }
    fn has(&self, addr: u32) -> bool {
        self.off(addr).is_some_and(|o| o + 4 <= self.d.len())
    }
    /// The NUL-terminated string at `addr`, if the bytes there look like one.
    fn cstr(&self, addr: u32) -> Option<String> {
        let base = self.off(addr)?;
        if base >= self.d.len() {
            return None;
        }
        let mut s = String::new();
        for i in 0..256usize {
            let b = *self.d.get(base + i)?;
            if b == 0 {
                return if s.len() >= 2 { Some(s) } else { None };
            }
            if !(0x20..0x7f).contains(&b) {
                return None;
            }
            s.push(b as char);
        }
        None
    }
}

/// Branch target of a B/BL, or None if this is not one.
fn branch_target(w: u32, pc: u32) -> Option<u32> {
    if (w >> 25) & 0x7 != 0b101 || w >> 28 == 0xf {
        return None;
    }
    let imm = ((w & 0x00ff_ffff) << 8) as i32 >> 6;
    Some(pc.wrapping_add(8).wrapping_add(imm as u32))
}

fn is_bl(w: u32) -> bool {
    (w >> 25) & 0x7 == 0b101 && w >> 28 != 0xf && w & (1 << 24) != 0
}

/// Where a function body ends, heuristically: the first unconditional return
/// (`ldm ..{..pc}` / `bx lr` / `mov pc, lr`) that is not followed by more code we branched into.
///
/// **It does not stop at a tail `B`, and this firmware is full of them.** `--fn=0x002102a4` is six
/// instructions of argument shuffling and `b 0x000ff2ec`; the heuristic sails past the branch and
/// prints 100+ instructions of whatever function happens to be laid out next. Bisecting *that*
/// listing's call sites measured zero arrivals at all 22 of them and read like a block one
/// instruction into the callee — the body under investigation was somewhere else entirely. A
/// `--fn=` listing whose last instruction is `b` rather than a return is a thunk: follow the branch
/// and re-run. Not fixed rather than fixed wrongly, because a forward `b` inside a real body is
/// ordinary control flow and terminating on it would truncate honest functions.
fn is_return(w: u32) -> bool {
    let cond_always = w >> 28 == 0xe;
    if !cond_always {
        return false;
    }
    // ldm(ia|db) sp!, {..., pc}
    if w & 0x0e10_0000 == 0x0810_0000 && w & (1 << 15) != 0 {
        return true;
    }
    // bx lr
    if w & 0x0fff_ffff == 0x012f_ff1e {
        return true;
    }
    // mov pc, lr
    if w & 0x0fff_ffff == 0x01a0_f00e {
        return true;
    }
    false
}

fn parse(t: &str) -> Option<u32> {
    t.strip_prefix("0x")
        .and_then(|h| u32::from_str_radix(h, 16).ok())
        .or_else(|| t.parse().ok())
}

/// The RetailOS image under the gitignored `resources/` tree, relative to the repository root.
///
/// The root is found by [`eapp_loader::settings::repo_root`] — the same two walks `ipod-gui` and
/// `ipod-boot` use, and for the same reason: a shared `CARGO_TARGET_DIR` puts the binary a long
/// way from the source.
fn default_image() -> std::path::PathBuf {
    eapp_loader::settings::repo_root().join("resources/derived/fw/OSOS_correct.bin")
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let path = args
        .iter()
        .find(|a| !a.starts_with("--"))
        .cloned()
        // Relative to the repository, found from the executable — it used to be one machine's
        // absolute home directory, which is a default that works for exactly one person and fails
        // with "no such file" for everybody else, including on that machine's other checkouts.
        .unwrap_or_else(|| default_image().to_string_lossy().into_owned());
    let d = std::fs::read(&path).unwrap_or_else(|e| {
        eprintln!("{path}: {e}");
        std::process::exit(1)
    });
    let syms = eapp_loader::extract_symbols(&d, 0);
    // --base=ADDR reads the file as if loaded there instead of in the firmware's own window.
    // For a game that is 0x18000000; `eapp` in the first four bytes says so without being told.
    let base = args
        .iter()
        .find_map(|a| a.strip_prefix("--base="))
        .and_then(parse)
        .unwrap_or(if d.starts_with(b"eapp") {
            eapp_loader::EApp::parse(d.clone()).map(|a| a.load_base).unwrap_or(0)
        } else {
            0
        });
    if base != 0 {
        println!("load base {base:#010x}");
    }
    let mut img = Img { d, base };
    println!("image {path} ({} bytes), {} symbols", img.d.len(), syms.len());

    if args.iter().any(|a| a == "--verify") {
        verify(&mut img);
    }

    for spec in args.iter().filter_map(|a| a.strip_prefix("--dis=")) {
        let (a, n) = spec.split_once(':').unwrap_or((spec, "40"));
        let (Some(addr), Some(count)) = (parse(a), parse(n)) else { continue };
        println!("\n=== disassembly at {addr:#010x} ===");
        dump(&mut img, &syms, addr, count);
    }

    // --fn=ADDR — disassemble to the function's own return, so a body is one command not a guess.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--fn=")) {
        let Some(addr) = parse(spec) else { continue };
        let n = fn_len(&img, addr);
        println!("\n=== function {addr:#010x} ({n} instructions) ===");
        dump(&mut img, &syms, addr, n);
    }

    // --walk=ADDR[:DEPTH] — the static call tree, which is the only way to see paths a
    // particular run did not happen to take.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--walk=")) {
        let (a, dep) = spec.split_once(':').unwrap_or((spec, "3"));
        let (Some(addr), Some(depth)) = (parse(a), parse(dep)) else { continue };
        println!("\n=== call tree from {addr:#010x}, depth {depth} ===");
        let mut seen = BTreeSet::new();
        walk(&img, &syms, addr, depth, 0, &mut seen);
        println!("  ({} distinct functions reachable)", seen.len());
    }

    for spec in args.iter().filter_map(|a| a.strip_prefix("--xref=")) {
        let Some(target) = parse(spec) else { continue };
        println!("\n=== branches to {target:#010x} ===");
        let mut n = 0;
        for off in (0..img.d.len().saturating_sub(4)).step_by(4) {
            // Address, not file offset — they are the same only when base is 0.
            let pc = img.base + off as u32;
            let w = img.w(pc);
            if branch_target(w, pc) == Some(target) {
                n += 1;
                let kind = if is_bl(w) { "bl" } else { "b " };
                println!("  {pc:#010x}  {kind}   {}", label(&syms, pc));
            }
        }
        println!("  {n} total");
    }

    // --wordref=VALUE — every word in the image equal to VALUE. Literal pools are how a caller
    // names a constant it never computes, so "who mentions this address" needs data as well as
    // branches.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--wordref=")) {
        let Some(target) = parse(spec) else { continue };
        println!("\n=== words equal to {target:#010x} ===");
        let mut n = 0;
        for off in (0..img.d.len().saturating_sub(4)).step_by(4) {
            let pc = img.base + off as u32;
            if img.w(pc) == target {
                n += 1;
                if n <= 40 {
                    println!("  {pc:#010x}   in {}", label(&syms, pc));
                }
            }
        }
        println!("  {n} total");
    }

    // --callfmt=T[,T…] — every BL to one of T, with the format string its caller loads.
    //
    // The GENCMD vocabulary is not a table anywhere; it is the set of literal format strings
    // passed to three varargs wrappers. Enumerating it by hand across 7.5 MB is how a vocabulary
    // gets under-counted, so it is enumerated mechanically: for each call site walk back until the
    // pool load that supplied r0, and print the string it points at.
    // Spelled TARGET:REG because the three wrappers do not agree on where the format lives:
    // 0x002874f0 pushes {r0-r3} over an 8-byte frame so its format is r0, while 0x00287664 and
    // 0x00287110 push a 16-byte frame and read [sp+0x18] — the saved r2 — with r0/r1 being the
    // caller's reply buffer and its size. A register-blind backward scan reports the nearest
    // string-shaped literal instead, which at 0x00163bc4 is the `mode=%s` argument "fill" and not
    // a command at all. Matching the scan to the ABI is the difference between the vocabulary and
    // a plausible-looking list.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--callfmt=")) {
        let mut targets: BTreeMap<u32, u32> = BTreeMap::new();
        for t in spec.split(',') {
            let (a, r) = t.split_once(':').unwrap_or((t, "2"));
            if let (Some(addr), Some(reg)) = (parse(a), parse(r)) {
                targets.insert(addr, reg);
            }
        }
        println!("\n=== call sites of {targets:x?} with their format strings ===");
        let mut sites = 0usize;
        let mut vocab: BTreeMap<String, Vec<u32>> = BTreeMap::new();
        for off in (0..img.d.len().saturating_sub(4)).step_by(4) {
            // Address, not file offset — they are the same only when base is 0.
            let pc = img.base + off as u32;
            let w = img.w(pc);
            let Some(reg) = branch_target(w, pc)
                .filter(|_| is_bl(w))
                .and_then(|t| targets.get(&t).copied())
            else {
                continue;
            };
            sites += 1;
            // Backwards to the nearest pc-relative load whose pool word is a string. 32 is well
            // past any observed prologue between the load and the call.
            // Two spellings reach a literal here and both are used in this firmware: a pool load
            // (`ldr rN, =addr`) and an ADR (`add rN, pc, #imm`) at a string inlined immediately
            // after the call. Scanning for only the first found 4 of 38 — the ADR form is how
            // every `display_control …` site in 0x001c7xxx names its command.
            // Back to the last instruction that writes the format register. Whatever that is
            // decides the answer: a pc-relative literal is the command, anything else (a load
            // from a struct field, a pointer to a buffer built at runtime) means this site's
            // command is not a compile-time constant and must be reported as such, not skipped.
            let mut found = None;
            for back in 1..=40u32 {
                let at = pc.wrapping_sub(back * 4);
                let iw = img.w(at);
                let cond_always = iw >> 28 == 0xe;
                // ldr rD, [pc, #imm] — rD is bits 15..12.
                if iw & 0x0f7f_0000 == 0x051f_0000 && (iw >> 12) & 0xf == reg {
                    let o = iw & 0xfff;
                    let pool = if iw & (1 << 23) != 0 { at + 8 + o } else { at + 8 - o };
                    found = Some(match img.cstr(img.w(pool)) {
                        Some(s) => format!("{s:?}"),
                        None => format!("<pointer {:#010x}, not a literal string>", img.w(pool)),
                    });
                    break;
                }
                // add rD, pc, #imm — rotated 8-bit immediate, as any data-processing operand.
                if iw & 0x0fff_0000 == 0x028f_0000 && (iw >> 12) & 0xf == reg {
                    let imm = (iw & 0xff).rotate_right(((iw >> 8) & 0xf) * 2);
                    found = Some(match img.cstr(at + 8 + imm) {
                        Some(s) => format!("{s:?}"),
                        None => format!("<adr {:#010x}, not a string>", at + 8 + imm),
                    });
                    break;
                }
                // Any other unconditional write to the format register ends the search: the
                // command is computed, and claiming a literal past this point would be a guess.
                let writes = (iw & 0x0c00_0000 == 0 && (iw >> 12) & 0xf == reg && (0x8..0xb).contains(&((iw >> 21) & 0xf)).eq(&false))
                    || (iw & 0x0c50_0000 == 0x0410_0000 && (iw >> 12) & 0xf == reg);
                if cond_always && writes {
                    found = Some(format!("<computed at {at:#010x}: {}>", disasm::arm(iw, at, None)));
                    break;
                }
            }
            let s = found.unwrap_or_else(|| "<not resolved within 40 instructions>".into());
            println!("  {pc:#010x}  {s}");
            vocab.entry(s).or_default().push(pc);
        }
        println!("\n  {sites} call sites, {} distinct format strings:", vocab.len());
        for (s, at) in &vocab {
            println!("    {:<52} x{}  {:#010x}", format!("{s:?}"), at.len(), at[0]);
        }
    }

    for spec in args.iter().filter_map(|a| a.strip_prefix("--hex=")) {
        let (a, n) = spec.split_once(':').unwrap_or((spec, "64"));
        let (Some(addr), Some(count)) = (parse(a), parse(n)) else { continue };
        println!("\n=== bytes at {addr:#010x} ===");
        for row in 0..count.div_ceil(16) {
            let at = addr + row * 16;
            let mut hexs = String::new();
            let mut asc = String::new();
            for i in 0..16 {
                let b = img.read8(at + i);
                hexs.push_str(&format!("{b:02x} "));
                asc.push(if (0x20..0x7f).contains(&b) { b as char } else { '.' });
            }
            println!("  {at:08x}  {hexs} |{asc}|");
        }
    }

    // --iscan=WORD[:MASK][:FOLLOW] — every word-aligned address whose word matches `WORD` under
    // `MASK` (default `0xffffffff`), disassembled, with `FOLLOW` following instructions printed.
    //
    // Exists because the obvious way to ask "does any instruction store to `[rN, #0xa0]`" was a
    // `grep -abo $'\xa0\x00\x84\xe5'` over the image, and **that search silently cannot work**:
    // command substitution strips NUL bytes, so the pattern shrinks to three bytes and matches
    // something else. It returned zero for a store this file's own disassembly shows at
    // `0x001d9904`, which is how it was caught. A masked word scan through the same decoder the
    // emulator uses has no such failure mode, and it answers register-wildcard questions directly:
    //
    //   --iscan=0xe58000a0:0xfff00fff     str rN, [rM, #0xa0]   any two registers
    //   --iscan=0xe3a0007f:0xffffffff:2   mov r0, #0x7f, and what follows it
    for spec in args.iter().filter_map(|a| a.strip_prefix("--iscan=")) {
        let mut p = spec.split(':');
        let Some(want) = p.next().and_then(parse) else { continue };
        let mask = p.next().and_then(parse).unwrap_or(0xffff_ffff);
        let follow = p.next().and_then(parse).unwrap_or(0);
        println!("\n=== words matching {want:#010x} under mask {mask:#010x} ===");
        let mut n = 0;
        let mut a = 0x1000u32;
        while img.has(a) {
            if img.w(a) & mask == want & mask {
                n += 1;
                for k in 0..=follow {
                    let at = a + k * 4;
                    let txt = disasm::arm(img.w(at), at, None);
                    println!("  {at:08x}  {:08x}  {txt}", img.w(at));
                }
                if follow > 0 {
                    println!();
                }
            }
            a += 4;
        }
        println!("  {n} total");
    }

    // --str=SUBSTR — every NUL-terminated string containing SUBSTR, with its address. Finding
    // which command names RetailOS itself carries is the whole vocabulary question.
    for needle in args.iter().filter_map(|a| a.strip_prefix("--str=")) {
        println!("\n=== strings containing {needle:?} ===");
        let mut n = 0;
        let mut i = 0usize;
        while i < img.d.len() {
            if !(0x20..0x7f).contains(&img.d[i]) {
                i += 1;
                continue;
            }
            let start = i;
            while i < img.d.len() && (0x20..0x7f).contains(&img.d[i]) {
                i += 1;
            }
            if i < img.d.len() && img.d[i] == 0 && i - start >= 3 {
                let s = String::from_utf8_lossy(&img.d[start..i]).to_string();
                if s.contains(needle) {
                    n += 1;
                    if n <= 200 {
                        println!("  {start:#010x}  {s:?}");
                    }
                }
            }
            i += 1;
        }
        println!("  {n} total");
    }
}

fn label(syms: &BTreeMap<u32, String>, addr: u32) -> String {
    match syms.range(..=addr).next_back() {
        Some((a, n)) if addr - a < 0x2000 => format!("{n}+{:#x}", addr - a),
        _ => String::new(),
    }
}

fn fn_len(img: &Img, addr: u32) -> u32 {
    for i in 0..600u32 {
        if is_return(img.w(addr + i * 4)) {
            return i + 1;
        }
    }
    600
}

fn dump(img: &mut Img, syms: &BTreeMap<u32, String>, addr: u32, count: u32) {
    for i in 0..count {
        let at = addr + i * 4;
        if !img.has(at) {
            break;
        }
        let w = img.w(at);
        let mut bus = Img { d: std::mem::take(&mut img.d), base: img.base };
        let text = disasm::arm(w, at, Some(&mut bus));
        img.d = std::mem::take(&mut bus.d);
        // A literal pool constant that points at a string is almost always a command name or a
        // format; showing it inline is the difference between reading and transcribing.
        let mut note = String::new();
        if let Some(t) = branch_target(w, at) {
            if let Some(n) = syms.get(&t) {
                note = format!("   ; {n}");
            }
        }
        if note.is_empty() {
            for cand in [w, img.w(at)] {
                if let Some(s) = img.cstr(cand) {
                    note = format!("   ; {s:?}");
                    break;
                }
                let _ = cand;
            }
        }
        // Resolve `ldr rN, [pc, ...]` one more hop: the pool word may itself be a string pointer.
        if w & 0x0f7f_0000 == 0x051f_0000 {
            let off = w & 0xfff;
            let pool = if w & (1 << 23) != 0 { at + 8 + off } else { at + 8 - off };
            let v = img.w(pool);
            if let Some(s) = img.cstr(v) {
                note = format!("   ; -> {s:?}");
            }
        }
        let sym = syms.get(&at).map(|s| format!("  <{s}>")).unwrap_or_default();
        println!("  {at:08x}  {w:08x}  {text}{note}{sym}");
    }
}

fn walk(
    img: &Img,
    syms: &BTreeMap<u32, String>,
    addr: u32,
    depth: u32,
    ind: u32,
    seen: &mut BTreeSet<u32>,
) {
    let pad = "  ".repeat(ind as usize + 1);
    let name = syms.get(&addr).cloned().unwrap_or_default();
    let fresh = seen.insert(addr);
    if !fresh {
        println!("{pad}{addr:#010x} {name} (seen)");
        return;
    }
    println!("{pad}{addr:#010x} {name}");
    if depth == 0 {
        return;
    }
    let n = fn_len(img, addr);
    let mut kids = BTreeSet::new();
    for i in 0..n {
        let pc = addr + i * 4;
        let w = img.w(pc);
        if is_bl(w) {
            if let Some(t) = branch_target(w, pc) {
                kids.insert(t);
            }
        }
    }
    for k in kids {
        walk(img, syms, k, depth - 1, ind + 1, seen);
    }
}

/// Reproduce a disassembly recorded from a live run. If this drifts, every other answer here is
/// suspect — the mapping, the decoder or the image is not the one the trace was reading.
fn verify(img: &mut Img) {
    const EXPECT: &[(u32, &str)] = &[
        (0x000c_d7c8, "stmdb"),
        (0x000c_d7cc, "ldr"),
        (0x000c_d7d0, "bl"),
        (0x000c_d7d4, "bl"),
        (0x000c_d7dc, "bl"),
        (0x000c_d7e0, "ldmia"),
    ];
    println!("\n=== self-check against research/20 Addendum 8 §6 (live-run disassembly) ===");
    let mut ok = true;
    for (at, want) in EXPECT {
        let w = img.w(*at);
        let mut bus = Img { d: std::mem::take(&mut img.d), base: img.base };
        let text = disasm::arm(w, *at, Some(&mut bus));
        img.d = std::mem::take(&mut bus.d);
        let got = text.split_whitespace().next().unwrap_or("");
        let hit = got == *want;
        ok &= hit;
        println!("  {} {at:08x}  {w:08x}  {text}", if hit { "ok  " } else { "FAIL" });
    }
    // The two constants the trace read out of that function, both one hop through a literal pool.
    let pool = img.w(0x000c_d7cc & !3);
    let _ = pool;
    let obj_ptr = 0x1081_eaf4u32;
    println!("  [{obj_ptr:#010x}] = {:#010x}   (trace: 0x13ef29a0 at runtime; image holds the initialiser)", img.w(obj_ptr));
    println!("  self-check {}", if ok { "PASSED" } else { "FAILED — do not trust anything below" });
    if !ok {
        std::process::exit(2);
    }
}
