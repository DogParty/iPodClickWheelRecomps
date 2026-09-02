//! `tcb` — read the whole RTXC scheduler out of an SDRAM dump.
//!
//! Addendum 7 §1 established that RetailOS's blocking state is a pure function of memory: the TCB
//! array records every task's state, priority and saved stack pointer; the kernel trampoline at
//! `0x00084644` saves `{cpsr, r0-r12, lr, lr}` before switching stacks, so the frame's last word is
//! the resume PC; and every resume PC lands one instruction after the `bl 0x00084644` inside one of
//! the 38 contiguous service wrappers. Read the wrapper, and you know which RTXC primitive the task
//! called. Read `r0` out of the same frame — the wrapper's `mov r0, sp` makes it a pointer to the
//! request block it built on the task's own stack — and you know the argument.
//!
//! That walk was done by hand three times (Addendum 7 §2, Addendum 8 §1, Addendum 11 §2) and each
//! time it produced a *sample*: the one or two tasks the session was already chasing. This is the
//! same walk as a **census** — every TCB, every wait, in one pass, off a `--save-region=sdram` file
//! that costs no run once it exists.
//!
//! Nothing here is a model or a bypass. It reads a file and prints what is in it.
//!
//! ```text
//! tcb SDRAM.bin [--array=0x0087198c] [--stride=0x3c] [--count=128] [--walk] [--free]
//!               [--wrappers] [--irq=OBJ] [--findobj=OFF:LO:HI] [--sem=ID] [--dump=ADDR:LEN]
//! ```
//!
//! - `--walk` adds the BL-preceded return-address walk of each blocked task's stack, which is how
//!   "blocked in a recursive view-tree builder" was established, and — because it also accepts the
//!   `mov lr, pc; bx rN` form — how the twenty-one pooled tasks entered through the trampoline at
//!   `0x000e1b10` get named at all.
//! - `--free` includes terminated and unused slots. `APPLEBOOT` finishing its body is a *field read*
//!   in that listing: TCB 9, `state = 0x100`, resume PC inside the termination wrapper.
//! - `--wrappers` prints the derived service table, which is this tool's own self-check.
//! - `--irq=OBJ` prints the interrupt controller's handler tables; `--findobj`, repeated, is how the
//!   controller object is located without a run (see the `--irq` comment for its two-field
//!   signature).
//! - `--sem=ID` scans for 8-byte objects whose `[obj+4]` is that RTXC id, per Addendum 15 §2. It is a
//!   **candidate list, not an identification** — the id also appears as an ordinary small integer —
//!   and it is only useful for semaphores that are reached through the counting pend/signal pair at
//!   `0x000a0ebc`/`0x000a0c84`. Semaphores a task pends on by literal id (`SerialOptoTask`'s `0x7f`,
//!   `mov r0, #0x7f; bl 0x000a6924`) have no such object and will not be found here.

use std::collections::BTreeMap;

/// SDRAM is 64 MB and answers at both `0x00000000` (the post-remap alias RetailOS executes from)
/// and `0x10000000` (the physical window the ROM loads through). One mask serves both.
const MASK: u32 = 0x03ff_ffff;
const TRAMPOLINE: u32 = 0x0008_4644;
const WRAPPERS_LO: u32 = 0x000a_613c;
const WRAPPERS_HI: u32 = 0x000a_69cc;

struct Img(Vec<u8>);

impl Img {
    fn w(&self, addr: u32) -> u32 {
        let o = (addr & MASK) as usize;
        if o + 4 > self.0.len() {
            return 0;
        }
        u32::from_le_bytes([self.0[o], self.0[o + 1], self.0[o + 2], self.0[o + 3]])
    }
    fn b(&self, addr: u32) -> u8 {
        let o = (addr & MASK) as usize;
        if o >= self.0.len() { 0 } else { self.0[o] }
    }
    /// A NUL-terminated ASCII string, or None if the bytes are not one.
    fn cstr(&self, addr: u32) -> Option<String> {
        let mut s = String::new();
        for i in 0..48 {
            let c = self.b(addr.wrapping_add(i));
            if c == 0 {
                return if s.len() >= 3 { Some(s) } else { None };
            }
            if !(0x20..0x7f).contains(&c) {
                return None;
            }
            s.push(c as char);
        }
        None
    }
}

/// RTXC service numbers, from the `mov r0, #imm` in each wrapper (Addendum 7 §1). Numbers not in
/// this table print bare — an unknown service is a fact worth seeing, not worth guessing at.
fn service_name(n: u32) -> &'static str {
    match n {
        0x01 => "KS_pend",
        0x02 => "KS_signal",
        0x05 => "KS_receive",
        0x06 => "KS_send",
        0x0e => "KS_lock",
        0x0f => "KS_unlock",
        0x14 => "KS_delay",
        0x15 => "KS_execute",
        0x19 => "KS_suspend",
        0x22 => "KS_waitm",
        _ => "",
    }
}

/// One decoded service wrapper.
struct Wrapper {
    svc: u32,
    entry: u32,
    /// Byte offset of the caller's first argument within the request block `r0` points at, or
    /// `None` when the wrapper stashes it somewhere this derivation does not follow.
    arg0: Option<i32>,
}

/// Which primitive each resume PC belongs to, and where that primitive's first argument sits —
/// **derived from the wrappers themselves**, not tabulated.
///
/// Scan `0x000a613c..0x000a69cc` for `bl 0x00084644`; for each one, find the wrapper's own prologue
/// and single-step forward over it tracking two things: the `mov r0, #imm` that sets the service
/// number, and where the caller's `r0` was stashed **before** that `mov` clobbered it. The request
/// block's base is wherever `r0` points at the `bl` (`mov r0, sp` or `add r0, sp, #imm`), so the
/// argument's offset within the block is the difference.
///
/// The wrappers do not agree on a layout — `KS_pend` puts its argument at `+0x08`, `KS_receive` at
/// `+0x0c` via an `stmia` of `{r0, r1}`, `KS_delay` at `+0x04` — which is exactly why this is
/// derived. It is also self-checking: word 0 of the block the *task* left on its stack must equal
/// the service number this scan read out of the *wrapper*, and a disagreement prints `(!req)`.
fn wrapper_map(img: &Img) -> BTreeMap<u32, Wrapper> {
    let mut out = BTreeMap::new();
    let imm12 = |i: u32| (i & 0xff).rotate_right(((i >> 8) & 0xf) * 2);
    let mut pc = WRAPPERS_LO;
    while pc < WRAPPERS_HI {
        let w = img.w(pc);
        // BL: cond xxxx, 1011 in bits 27..24.
        if (w >> 24) & 0x0f == 0x0b {
            let off = ((w & 0x00ff_ffff) << 8) as i32 >> 6;
            if (pc as i32 + 8 + off) as u32 == TRAMPOLINE {
                // Back to the wrapper's prologue: `str lr, [sp, #-4]!` or `stmdb sp!, {…}`.
                let mut entry = pc;
                while entry > WRAPPERS_LO && pc - entry < 0x80 {
                    let i = img.w(entry);
                    if i == 0xe52d_e004 || i & 0x0fff_0000 == 0x092d_0000 {
                        break;
                    }
                    entry -= 4;
                }
                // Forward over it, tracking sp-relative pointers and the service number.
                //
                // `r0_live` is the load-bearing part: the caller's first argument is in `r0` on
                // entry, and only the store that happens *while it is still there* is the argument.
                // Without it, `KS_delay`'s later `str r0, [sp, #0x30]` — a pointer to a scratch
                // slot, computed into r0 four instructions earlier — reads as the argument and puts
                // the answer 8 bytes wrong.
                let (mut svc, mut r0_live, mut stash, mut reqbase) = (u32::MAX, true, None, None);
                let mut sprel: BTreeMap<u32, u32> = BTreeMap::new();
                let mut q = entry;
                while q < pc {
                    let i = img.w(q);
                    let rd = (i >> 12) & 0xf;
                    let is_store = i & 0x0c10_0000 == 0x0400_0000;
                    let block = i & 0x0e00_0000 == 0x0800_0000;
                    if i & 0x0fff_f000 == 0x03a0_0000 && rd == 0 {
                        svc = imm12(i); // mov r0, #imm — the service number, last one wins
                    }
                    if i & 0x0fff_0000 == 0x028d_0000 {
                        sprel.insert(rd, imm12(i)); // add rd, sp, #imm
                        r0_live &= rd != 0;
                    } else if i == 0xe1a0_000d {
                        sprel.insert(0, 0); // mov r0, sp
                        r0_live = false;
                    } else if !is_store && !block && rd == 0 && i & 0x0c00_0000 <= 0x0400_0000 {
                        sprel.remove(&0);
                        r0_live = false; // anything that writes r0 ends the argument's life there
                    }
                    if stash.is_none() && r0_live {
                        if is_store && rd == 0 && i & 0x0fff_0000 == 0x058d_0000 {
                            stash = Some((i & 0xfff) as i32); // str r0, [sp, #imm]
                        } else if block && i & 0x0010_0001 == 1 {
                            // stmia rn, {r0, …} — the argument pair, via an sp-relative pointer.
                            if let Some(&b) = sprel.get(&((i >> 16) & 0xf)) {
                                stash = Some(b as i32);
                            }
                        }
                    }
                    if q + 4 == pc {
                        reqbase = sprel.get(&0).map(|&b| b as i32);
                    }
                    q += 4;
                }
                let arg0 = match (stash, reqbase) {
                    (Some(s), Some(b)) if s >= b => Some(s - b),
                    _ => None,
                };
                out.insert(pc + 4, Wrapper { svc, entry, arg0 });
            }
        }
        pc += 4;
    }
    out
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let path = match args.iter().find(|a| !a.starts_with("--")) {
        Some(p) => p.clone(),
        None => {
            eprintln!("usage: tcb SDRAM.bin [--array=…] [--stride=…] [--count=…] [--walk] [--free]");
            std::process::exit(2);
        }
    };
    let num = |k: &str, d: u32| -> u32 {
        args.iter()
            .find_map(|a| a.strip_prefix(k))
            .map(|v| {
                let v = v.replace('_', "");
                if let Some(h) = v.strip_prefix("0x") {
                    u32::from_str_radix(h, 16).unwrap_or(d)
                } else {
                    v.parse().unwrap_or(d)
                }
            })
            .unwrap_or(d)
    };
    let img = Img(std::fs::read(&path).unwrap_or_else(|e| {
        eprintln!("{path}: {e}");
        std::process::exit(2);
    }));
    let array = num("--array=", 0x0087_198c);
    let stride = num("--stride=", 0x3c);
    let count = num("--count=", 128);
    let walk = args.iter().any(|a| a == "--walk");
    let free = args.iter().any(|a| a == "--free");

    if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--dump=")) {
        let (a, l) = spec.split_once(':').unwrap_or((spec, "0x40"));
        let a = u32::from_str_radix(a.trim_start_matches("0x"), 16).unwrap_or(0);
        let l = u32::from_str_radix(l.trim_start_matches("0x"), 16).unwrap_or(0x40);
        for row in 0..l.div_ceil(16) {
            let base = a + row * 16;
            let words: Vec<String> = (0..4).map(|i| format!("{:08x}", img.w(base + i * 4))).collect();
            println!("{base:08x}  {}", words.join(" "));
        }
        return;
    }

    // `--findobj=OFF:LO:HI` — every word-aligned address A in SDRAM whose word at `A+OFF` falls in
    // `[LO, HI)`. Repeat the flag to intersect: an object is identified by several of its fields at
    // once, and one field alone matches thousands of addresses by chance. This is how the interrupt
    // controller's object is located without a run — its `+0x574`/`+0x578` are pointers to the two
    // `CPU_INT_EN` registers, which is a two-field signature nothing else in 64 MB satisfies.
    let finds: Vec<&str> = args.iter().filter_map(|a| a.strip_prefix("--findobj=")).collect();
    if !finds.is_empty() {
        let mut hits: Option<Vec<u32>> = None;
        for spec in &finds {
            let p: Vec<u32> = spec
                .split(':')
                .map(|v| u32::from_str_radix(v.trim_start_matches("0x"), 16).unwrap_or(0))
                .collect();
            let (off, lo, hi) = (p[0], p[1], *p.get(2).unwrap_or(&(p[1] + 1)));
            let keep = |a: &u32| {
                let v = img.w(a.wrapping_add(off));
                v >= lo && v < hi
            };
            hits = Some(match hits {
                Some(h) => h.into_iter().filter(|a| keep(a)).collect(),
                None => (0..img.0.len() as u32 / 4)
                    .map(|i| 0x1000_0000 + i * 4)
                    .filter(keep)
                    .collect(),
            });
        }
        let hits = hits.unwrap_or_default();
        println!("--findobj: {} candidates", hits.len());
        for a in hits.iter().take(24) {
            println!("  {a:#010x}");
        }
        return;
    }

    // Names, from three sources, most authoritative last. Every one of them pairs a name with an
    // entry point *in the same record*, so none of them relies on the literal-pool adjacency that
    // shifted `extract_symbols`'s six boot tasks by one (Addendum 7 §2).
    //
    //  (1) `extract_symbols` over the image — inline labels and the device registry's name→pointer
    //      pattern. The weakest source; the other two override it.
    //  (2) the device-task table at 0x00880f8c: 5-word records {stack top, entry, name, n, prio}.
    //      Its `name` is a pointer, so the pairing is explicit rather than positional.
    //  (3) the boot-task descriptors the creation code at 0x000d3b60 memcpy's to 0x108c77b4:
    //      6 words, {id, prio, stack top, size, entry, name}. This is the record Addendum 7 §2 read
    //      by hand; the field order is confirmed by every priority in it matching the resulting TCB.
    let mut by_entry: BTreeMap<u32, String> = eapp_loader::extract_symbols(&img.0, 0);
    let mut by_id: BTreeMap<u32, String> = BTreeMap::new();
    let prologue = |a: u32| img.w(a) & 0x0fff_0000 == 0x092d_0000 || img.w(a) == 0xe52d_e004;
    for k in 0..64u32 {
        let r = 0x0088_0f8c + k * 20;
        let (stack, entry, name, prio) = (img.w(r), img.w(r + 4), img.w(r + 8), img.w(r + 16));
        let Some(n) = img.cstr(name) else { continue };
        if stack == 0 || prio > 127 || !prologue(entry) {
            continue;
        }
        by_entry.insert(entry, n);
    }
    for k in 0..16u32 {
        let d = 0x108c_77b4 + k * 0x18;
        let (id, prio, entry, name) = (img.w(d), img.w(d + 4), img.w(d + 0x10), img.w(d + 0x14));
        let Some(n) = img.cstr(name) else { continue };
        // No prologue test here: `t_csa`'s entry at 0x00284a98 is a two-instruction thunk
        // (`ldr r0, =…; b`), and requiring a stack-push prologue silently dropped it.
        if id > 127 || prio > 127 || entry < 0x1000 {
            continue;
        }
        by_id.insert(id, n.clone());
        by_entry.insert(entry, n);
    }

    // `--irq=OBJ` — the interrupt controller's three tables, read off the object the IRQ vector
    // dispatches through. Field offsets come from the code, not from a guess:
    //
    //   demux   0x001fc5c0  `add r1, this, src lsl #2` then `ldr r0, [r1, #0x8]`  -> handler object
    //                       and, when that slot is null, `ldr r0, [r1, #0x108]`   -> raw handler
    //   enable  0x001fc588  `ldr r1, [this + id*4 + 0x208]`                       -> id -> source
    //   register 0x001fc730 writes the `+0x8` slot, and only when it is still null
    //
    // The object itself is found by `--findobj=574:60004024:60004025 --findobj=578:…`, which is
    // unique in 64 MB, and then confirmed the other way round: the IRQ vector at `0x0012763c` loads
    // `0x1084be48`, and `[0x1084be48 + 4]` holds exactly that pointer.
    if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--irq=")) {
        let obj = u32::from_str_radix(spec.trim_start_matches("0x"), 16).unwrap_or(0);
        println!("interrupt controller {obj:#010x}");
        println!("  handlers, by hardware source:");
        for src in 0..64u32 {
            let (o, f) = (img.w(obj + 8 + src * 4), img.w(obj + 0x108 + src * 4));
            if o == 0 && f == 0 {
                continue;
            }
            let ids: Vec<String> = (0..0x51u32)
                .filter(|k| img.w(obj + 0x208 + k * 4) == src)
                .map(|k| format!("{k:#x}"))
                .collect();
            let sym = |a: u32| match by_entry.range(..=a).next_back() {
                Some((&b, n)) if a - b <= 0x1000 => format!(" <{n}+{:#x}>", a - b),
                _ => String::new(),
            };
            println!(
                "    src {src:>2} ({src:#04x}): object {o:#010x}  fn {f:#010x}{}  logical id {}",
                sym(f),
                if ids.is_empty() { "-".into() } else { ids.join(",") }
            );
        }
        let empty: Vec<u32> =
            (0..64).filter(|&s| img.w(obj + 8 + s * 4) == 0 && img.w(obj + 0x108 + s * 4) == 0).collect();
        println!("  {} of 64 sources have no handler in either table", empty.len());
        println!(
            "  source 40 (click wheel / I2C, hi bank bit 8): object {:#010x}  fn {:#010x}",
            img.w(obj + 8 + 40 * 4),
            img.w(obj + 0x108 + 40 * 4)
        );
        return;
    }

    // Every counting-semaphore object in SDRAM, keyed by the RTXC id it carries.
    //
    // `0x000d1e5c` allocates 8 bytes, zeroes `[0]` and asks RTXC service 3 to write an id into
    // `[4]` (Addendum 15 §2). `0x000a0ebc`/`0x000a0c84` are the counting pend/signal pair, and they
    // load `[obj+4]` **only** on the branch where a task really blocks or is really woken — which
    // makes `--readlog` on that word an exact census of blocking pends and kernel wakes, with no
    // instrumentation of the kernel at all. This scan is what turns a semaphore *id* from the TCB
    // walk into the *address* that instrument needs.
    let mut semobjs: BTreeMap<u32, Vec<u32>> = BTreeMap::new();
    for o in (0..img.0.len().saturating_sub(8)).step_by(4) {
        let a = 0x1000_0000 + o as u32;
        let id = img.w(a + 4);
        if (1..=0x200).contains(&id) && (-64..=64).contains(&(img.w(a) as i32)) {
            semobjs.entry(id).or_default().push(a);
        }
    }

    let wmap = wrapper_map(&img);
    println!(
        "wrappers: {} entries into {TRAMPOLINE:#010x} between {WRAPPERS_LO:#010x} and {WRAPPERS_HI:#010x}",
        wmap.len()
    );
    if args.iter().any(|a| a == "--wrappers") {
        for (resume, w) in &wmap {
            let n = service_name(w.svc);
            println!(
                "  resume {resume:#010x}  wrapper {:#010x}  svc {:#04x} {:<11} arg0 {}",
                w.entry,
                w.svc,
                if n.is_empty() { "?" } else { n },
                match w.arg0 {
                    Some(o) => format!("req+{o:#x}"),
                    None => "unresolved".into(),
                }
            );
        }
    }
    println!(
        "\n{:>3} {:<24} {:>3} {:>6} {:<10} {:>9} {:<24} {}",
        "id", "name", "pri", "state", "entry", "tick", "blocked in", "on"
    );

    let mut sems: BTreeMap<u32, Vec<u32>> = BTreeMap::new();
    let mut mboxes: BTreeMap<u32, Vec<u32>> = BTreeMap::new();
    let mut nfree = 0;
    for id in 0..count {
        let t = array + id * stride;
        let (entry, ssp, stk, size, state, sid, prio, tick) = (
            img.w(t + 0x0c),
            img.w(t + 0x10),
            img.w(t + 0x14),
            img.w(t + 0x18),
            img.w(t + 0x1c),
            img.w(t + 0x20),
            img.w(t + 0x24),
            img.w(t + 0x28),
        );
        if state == 0x100 || (entry == 0 && ssp == 0) {
            nfree += 1;
            if !free {
                continue;
            }
        }
        if sid != id {
            // The array is indexed `base + id*60` by the kernel itself (0x000a63b8), so a record
            // whose +0x20 disagrees is not a TCB and must not be reported as one.
            continue;
        }
        let name = by_id
            .get(&id)
            .cloned()
            .or_else(|| by_entry.get(&entry).cloned())
            .unwrap_or_else(|| "-".into());
        // The saved frame: 16 words, {cpsr, r0-r12, lr, lr}. Word 15 is the resume PC.
        let resume = img.w(ssp + 15 * 4);
        let r0 = img.w(ssp + 4);
        let (mut prim, mut arg) = (String::new(), String::new());
        if let Some(w) = wmap.get(&resume) {
            let svc = w.svc;
            let n = service_name(svc);
            prim = if n.is_empty() { format!("svc {svc:#04x}") } else { n.into() };
            if img.w(r0) != svc {
                prim.push_str(" (!req)");
            }
            arg = match w.arg0 {
                None => {
                    let ws: Vec<String> =
                        (1..5).map(|i| format!("{:08x}", img.w(r0 + i * 4))).collect();
                    format!("req+4.. {}", ws.join(" "))
                }
                Some(off) => {
                    let a0 = img.w(r0.wrapping_add(off as u32));
                    match svc {
                        0x01 | 0x02 => {
                            sems.entry(a0).or_default().push(id);
                            format!("sem {a0:#04x}")
                        }
                        0x05 | 0x06 => {
                            mboxes.entry(a0).or_default().push(id);
                            format!("mailbox {a0:#04x}")
                        }
                        0x0e | 0x0f => format!("resource {a0:#05x}"),
                        0x14 => format!("{a0} ticks"),
                        0x22 => format!("mask {a0:#010x}"),
                        _ => format!("{a0:#010x}"),
                    }
                }
            };
        } else if state == 0 {
            prim = "RUNNABLE".into();
            arg = format!("at {resume:#010x}");
        } else if ssp != 0 {
            prim = format!("? resume {resume:#010x}");
        }
        println!(
            "{id:>3} {name:<24} {prio:>3} {state:>#6x} {entry:#010x} {tick:>9} {prim:<24} {arg}"
        );
        if walk && ssp != 0 {
            // Return addresses between the saved frame and the top of the stack, accepted only when
            // the instruction that would have pushed them really is a call: `bl` four bytes below,
            // or the `mov lr, pc; bx rN` pair eight and four bytes below — which is how the thread
            // trampoline at 0x000e1b10 calls a task body it was handed at runtime, and therefore
            // the only way to recover which body a pooled task is running.
            let top = stk.wrapping_add(size);
            let mut chain = vec![];
            let mut p = ssp + 0x40;
            while p < top && p < ssp + 0x2000 {
                let v = img.w(p);
                let call = v > 0x1000
                    && v < 0x0040_0000
                    && v % 4 == 0
                    && ((img.w(v - 4) >> 24) & 0x0f == 0x0b
                        || (img.w(v - 4) & 0x0fff_fff0 == 0x012f_ff10
                            && img.w(v - 8) & 0x0fff_ffff == 0x01a0_e00f));
                if call {
                    chain.push(match by_entry.range(..=v).next_back() {
                        Some((&a, n)) if v - a <= 0x1000 && v != a => format!("{v:08x}<{n}+{:#x}>", v - a),
                        Some((&a, n)) if v == a => format!("{v:08x}<{n}>"),
                        _ => format!("{v:08x}"),
                    });
                }
                p += 4;
            }
            if !chain.is_empty() {
                println!("      stack {stk:#010x}+{size:#x} sp {ssp:#010x}: {}", chain.join(" "));
            }
        }
    }
    println!("\n{nfree} free slots of {count} scanned");

    if !sems.is_empty() {
        println!("\nsemaphores pended on at this instant:");
        for (s, who) in &sems {
            println!("  {s:#04x}  tasks {who:?}");
        }
    }
    if !mboxes.is_empty() {
        println!("\nmailboxes waited on at this instant:");
        for (s, who) in &mboxes {
            println!("  {s:#04x}  tasks {who:?}");
        }
    }

    // `--sem=ID`: find the semaphore object. `0x000d1e5c` allocates 8 bytes, `[0]` the counter and
    // `[4]` the RTXC id (Addendum 15 §2), so a whole-image scan for `[obj+4] == ID` locates it.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--sem=")) {
        let want = u32::from_str_radix(spec.trim_start_matches("0x"), 16).unwrap_or(0);
        println!("\nsemaphore objects carrying id {want:#x}:");
        let mut n = 0;
        for o in (0..img.0.len().saturating_sub(8)).step_by(4) {
            let a = 0x1000_0000 + o as u32;
            if img.w(a + 4) == want && (img.w(a) as i32).abs() < 0x1000 {
                println!("  obj {a:#010x}  counter {:#010x} ({})", img.w(a), img.w(a) as i32);
                n += 1;
                if n > 32 {
                    println!("  … stopped at 32");
                    break;
                }
            }
        }
    }
}
