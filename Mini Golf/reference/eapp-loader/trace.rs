//! Load a real eApp and trace the framework calls it makes.
//!
//!   trace [app.bin] [instruction_budget]
//!
//! Both positionals are optional. A boot (`--boot-osos`) never executes an eApp — RetailOS is
//! entered from the reset vector — so the recipes pass none, and a bare integer in first position
//! is the budget. A path is never a bare integer, so the two cannot be confused.

use std::env;
use std::fs;

use arm7tdmi::{disasm, Bus};
use eapp_loader::{EApp, Machine, Stop, Stub};

// Above OSOS, which occupies 0x10000000..~0x10736000 when mapped.
const RAM_BASE: u32 = 0x1100_0000;
const RAM_SIZE: usize = 0x0080_0000; // 8 MB — the 5G has 32/64 MB, this is ample for a trace

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.is_empty() {
        eprintln!("usage: trace [app.bin] [budget] [flags…]");
        std::process::exit(2);
    }
    // `--bcm-film` is wired into the `--boot-osos` run loop and nowhere else, because that is the
    // only path with a co-processor to read. Said here rather than left to be discovered: a flag
    // that is quietly a no-op on the other entry points is exactly the shape of silence that has
    // cost this project published conclusions.
    if args.iter().any(|a| a.starts_with("--bcm-film=")) && !args.iter().any(|a| a == "--boot-osos")
    {
        eprintln!("--bcm-film only records the --boot-osos path; there is no panel on the others.");
        std::process::exit(2);
    }
    // The eApp image, if one was named. A leading positional that parses as an integer is the
    // budget, not a path, so `trace 4000000000 --boot-osos …` needs no file that a boot never reads.
    let image_path: Option<&String> = args
        .first()
        .filter(|a| !a.starts_with("--") && a.parse::<usize>().is_err());

    const DEFAULT_BUDGET: usize = 2_000_000;
    let budget: usize = args
        .get(usize::from(image_path.is_some()))
        .filter(|s| !s.starts_with("--"))
        .and_then(|s| s.parse().ok())
        .unwrap_or(DEFAULT_BUDGET);

    // --write=Framework:index:argreg=value — make a query report through its out-parameter.
    let writes: Vec<(String, usize, usize, u32, u32)> = args
        .iter()
        .filter_map(|a| a.strip_prefix("--write="))
        .filter_map(|spec| {
            let (lhs, val) = spec.split_once('=')?;
            let mut parts = lhs.split(':');
            let fw = parts.next()?.to_string();
            let idx: usize = parts.next()?.parse().ok()?;
            let argspec = parts.next()?;
            let (arg, off) = match argspec.split_once('+') {
                Some((a, o)) => (a.parse().ok()?, o.parse().ok()?),
                None => (argspec.parse().ok()?, 0u32),
            };
            Some((fw, idx, arg, off, val.parse().ok()?))
        })
        .collect();

    // --stub Framework:index=value — try a return value without rebuilding.
    let overrides: Vec<(String, usize, u32)> = args
        .iter()
        .filter_map(|a| a.strip_prefix("--stub="))
        .filter_map(|spec| {
            let (lhs, val) = spec.split_once('=')?;
            let (fw, idx) = lhs.split_once(':')?;
            let v = val
                .strip_prefix("0x")
                .and_then(|h| u32::from_str_radix(h, 16).ok())
                .or_else(|| val.parse().ok())?;
            Some((fw.to_string(), idx.parse().ok()?, v))
        })
        .collect();

    let app = match image_path {
        Some(path) => {
            let image = fs::read(path).unwrap_or_else(|e| {
                eprintln!("{path}: {e}");
                std::process::exit(1);
            });
            match EApp::parse(image) {
                Ok(a) => a,
                Err(e) => {
                    eprintln!("not loadable: {e:?}");
                    std::process::exit(1);
                }
            }
        }
        None => EApp::none(),
    };

    if app.is_loaded() {
        println!("load base   {:#010x}", app.load_base);
        println!("entry       {:#010x}", app.entry);
        println!("frameworks  {}", app.frameworks.len());
        for fw in &app.frameworks {
            println!(
                "  {:<14} {:>3} imports   hash {}",
                fw.name,
                fw.thunks.len(),
                fw.hash.iter().map(|b| format!("{b:02x}")).collect::<String>()
            );
        }
        println!("total imports {}", app.import_count());
    }

    let mut m = Machine::new(&app, RAM_BASE, RAM_SIZE);
    // Identified from its own call pattern: sizes in, pointer immediately dereferenced.
    m.set_stub("miscTBD", 0, Stub::Alloc);
    // Identified from the FPS calculation it feeds; see Stub::Clock. ~60 fps worth per call.
    m.set_stub("miscTBD", 9, Stub::Clock { arg: 0, step: 16_667 });
    // Same pointers back in reverse order — see the identification table in the README.
    m.set_stub("miscTBD", 1, Stub::Free { arg: 0 });
    // GL entry points identified from their argument enums — see research/02.
    m.set_stub("OpenGLES", 12, Stub::GlClear);        // r0 = 0x4000 = GL_COLOR_BUFFER_BIT
    m.set_stub("OpenGLES", 13, Stub::GlClearColor);   // r0 = 0x3f800000 = 1.0f
    m.set_stub("OpenGLES", 157, Stub::GlSwap);        // brackets every frame
    // #137 takes six arguments (stride and pointer on the stack); #40 takes one and is
    // glEnableVertexAttribArray. Wiring the pointer stub to #40 "worked" only because the
    // stale stack still held #137's arguments — correct output, wrong reason.
    m.set_stub("OpenGLES", 137, Stub::GlVertexAttribPointer);
    m.set_stub("OpenGLES", 37, Stub::GlDrawArrays);
    m.set_stub("OpenGLES", 4, Stub::GlBindTexture);
    m.set_stub("OpenGLES", 19, Stub::GlCompressedTexImage2D);
    // #99 glTexImage2D — named from Apple's implementation at 0x00270240. Unstubbed it returned
    // 0 and the upload was dropped, which is why the golf course rendered as a white field.
    m.set_stub("OpenGLES", 99, Stub::GlTexImage2D);
    // #21 glCopyTexImage2D — render-to-texture. Minigolf allocates a screen-sized texture from a
    // placeholder and then fills it from the framebuffer; without this the placeholder is drawn.
    m.set_stub("OpenGLES", 21, Stub::GlCopyTexImage2D);
    m.set_stub("OpenGLES", 40, Stub::GlEnableVertexAttribArray);
    m.set_stub("OpenGLES", 36, Stub::GlDisableVertexAttribArray);
    // --open-returns-handle : make an open report success by returning the handle rather than 0.
    // Off by default, because the two conventions are opposites and every title measured before
    // Minigolf wanted the zero. See `Stub::FileOpen`.
    let open_ret_handle = args.iter().any(|a| a == "--open-returns-handle");
    if open_ret_handle {
        println!("open-returns-handle: FileOpen returns the handle (0 = miss = failure)");
    }
    // --async-files : model AsyncFileIO the way RetailOS implements it — accept the operation,
    // park the request, and run the game's completion callback between frames. Opt-in, because
    // it replaces the synchronous open/read every other title has been measured against.
    // --profile on the eApp path too; the flag was only honoured inside the boot branch, so it
    // was accepted, did nothing, and printed nothing.
    if args.iter().any(|a| a.starts_with("--profile")) && m.profile.is_none() {
        m.profile = Some(std::collections::HashMap::new());
    }
    m.ignore_colour_key = args.iter().any(|a| a == "--no-colour-key");
    let async_files = args.iter().any(|a| a == "--async-files");
    if async_files {
        println!("async-files: AsyncFileIO #0/#3 open, #2 read, completions drained per frame");
    }
    // open(?, path, ?, &handle) — confirmed by reading the string argument: "Sounds/All_Out.wav"
    // Peek at what the game registers as an audio stream.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--peek=")) {
        if let Some((fw, rest)) = spec.split_once(':') {
            if let Some((idx, reg)) = rest.split_once('=') {
                let (rs, os) = reg.split_once('+').unwrap_or((reg, "0"));
                if let (Ok(i), Ok(r), Ok(o)) =
                    (idx.parse::<usize>(), rs.parse::<usize>(), os.parse::<u32>())
                {
                    m.set_stub(fw, i, Stub::PeekStr { arg: r, off: o });
                    println!("peek {fw}#{i} r{r}+{o}");
                }
            }
        }
    }
    m.set_stub("Filesytem", 0, Stub::FileOpen { path: 1, out: 3, return_handle: open_ret_handle });
    m.set_stub("AsyncFileIO", 0, Stub::FileOpen { path: 1, out: 3, return_handle: open_ret_handle });
    // read(handle, buffer, length, &bytesRead) — the game allocates exactly `length` first.
    let rd = Stub::FileRead { handle: 0, buffer: 1, length: 2, out: 3 };
    m.set_stub("Filesytem", 2, rd.clone());
    m.set_stub("AsyncFileIO", 2, rd.clone());
    // AsyncFileIO #3 takes a filename in r1 — Pac-Man passes "pac_man.dat", its save file.
    m.set_stub("AsyncFileIO", 3, Stub::FileOpen { path: 1, out: 2, return_handle: open_ret_handle });
    if async_files {
        // Request-object register per import, from the shims at 0x002680e4 / 0x00268118 /
        // 0x00268144: #0 takes it fifth (game r3), #3 third (game r2), #2 first (game r0).
        m.set_stub("AsyncFileIO", 0, Stub::AsyncOpen { path: 1, request: 3 });
        m.set_stub("AsyncFileIO", 3, Stub::AsyncOpen { path: 1, request: 2 });
        m.set_stub("AsyncFileIO", 2, Stub::AsyncRead { request: 0 });
        // #1 takes the request in r0, #4 in r2 (shims at 0x002680c8 / 0x00268160).
        m.set_stub("AsyncFileIO", 1, Stub::AsyncOp { request: 0 });
        m.set_stub("AsyncFileIO", 4, Stub::AsyncOp { request: 2 });
        // #12/#14/#16 are the save/settings store — they route through a different singleton
        // (0x0017154c) than the file entries and only appear when the pause menu opens. Left
        // unstubbed they return 0, i.e. "failed", and the menu stalls before drawing its items.
        // Reporting success is a guess at the value but a well-founded one about the direction.
        m.set_stub("AsyncFileIO", 12, Stub::Value(1));
        m.set_stub("AsyncFileIO", 14, Stub::Value(1));
        m.set_stub("AsyncFileIO", 16, Stub::Value(1));
    }
    // Everything the §18.0 coverage audit settled. Shared with `play` — this tool exists to
    // measure what the viewer does, so the two must answer the same imports the same way.
    m.install_audit_stubs();
    // --input=CODE[,CODE...] queues edge-triggered events, one consumed per poll.
    if let Some(list) = args.iter().find_map(|a| a.strip_prefix("--input=")) {
        m.set_stub("InputEvents", 0, Stub::InputPoll { arg: 0, offset: 4 });
        // Accept full 32-bit words (0x...) as well as byte codes, so the whole event
        // encoding can be probed rather than just the low byte.
        for tok in list.split(',') {
            let t = tok.trim();
            if let Some(h) = t.strip_prefix("0x") {
                if let Ok(v) = u32::from_str_radix(h, 16) {
                    m.input_queue.push(v);
                    continue;
                }
            }
            if let Ok(c) = t.parse::<u8>() {
                m.queue_input(c);
            }
        }
        println!("queued {} input events", m.input_queue.len());
    }
    m.log_indirect = args.iter().any(|a| a == "--indirect");
    // --callgraph[=ADDR] : record every branch edge actually taken. With an address, report the
    // runtime CALLERS of that address — the question static analysis cannot answer for virtual
    // dispatch, and the one that has dead-ended this investigation four times.
    if args.iter().any(|a| a.starts_with("--callgraph")) {
        m.edges = Some(Default::default());
        println!("callgraph: recording every branch edge taken");
    }
    if args.iter().any(|a| a == "--verify-memory") {
        m.mem.verify_memory = true;
        println!("verify-memory: cross-checking the page cache against the slow path");
    }
    if args.iter().any(|a| a == "--null-dispatch=survive") {
        m.cpu.survive_null_dispatch = true;
        println!("null dispatch: BX to 0 reported as a null return (DIAGNOSTIC, not a fix)");
    }
    // --break=ADDR : dump the register file every time the PC reaches ADDR.
    // --watch=ADDR : record every change to the 32-bit word at ADDR, with the PC responsible.
    let parse_addr = |s: &str| u32::from_str_radix(s.trim_start_matches("0x"), 16).ok();
    m.breakpoints = args
        .iter()
        .filter_map(|a| a.strip_prefix("--break="))
        .filter_map(|s| parse_addr(s))
        .collect();
    m.watch = args
        .iter()
        .find_map(|a| a.strip_prefix("--watch="))
        .and_then(|s| parse_addr(s));
    // --stop-at=ADDR[:N] : halt on the Nth arrival at ADDR (default the 1st), so --history
    // describes the first fault rather than whichever repeat the budget ended in.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--stop-at=")) {
        let (a, n) = spec.split_once(':').unwrap_or((spec, "1"));
        if let (Some(addr), Ok(nth)) = (parse_addr(a), n.parse::<u64>()) {
            m.stop_at.push((addr, nth.max(1)));
        }
    }
    if !m.stop_at.is_empty() {
        println!("stop-at: {:?}", m.stop_at.iter().map(|(a, n)| format!("{a:#010x} hit {n}")).collect::<Vec<_>>());
    }
    for spec in args.iter().filter_map(|a| a.strip_prefix("--sum-at=")) {
        let p: Vec<&str> = spec.split(':').collect();
        if p.len() == 3 {
            let g = |t: &str| u32::from_str_radix(t.trim_start_matches("0x"), 16).ok();
            if let (Some(pc), Some(a), Some(l)) = (g(p[0]), g(p[1]), g(p[2])) {
                m.sum_at.push((pc, a, l));
            }
        }
    }
    m.retwatch = args
        .iter()
        .find_map(|a| a.strip_prefix("--retwatch="))
        .and_then(|s| parse_addr(s));
    // --storelog=PC[,PC…] and --enterlog=PC[,PC…]: two views of one function, keyed on the code
    // rather than on an address. Heap addresses move between runs; the instruction that writes them
    // does not.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--storelog=")) {
        m.mem.store_pcs.extend(spec.split(',').filter_map(parse_addr));
    }
    for spec in args.iter().filter_map(|a| a.strip_prefix("--storeaddr=")) {
        // A file of addresses, one per line, is the usable form: the interesting sets come out of a
        // previous run's dump and run to hundreds of entries.
        let body = std::fs::read_to_string(spec).unwrap_or_else(|_| spec.replace(',', "\n"));
        m.mem.store_addrs.extend(body.split_whitespace().filter_map(parse_addr));
    }
    for spec in args.iter().filter_map(|a| a.strip_prefix("--readlog=")) {
        let body = std::fs::read_to_string(spec).unwrap_or_else(|_| spec.replace(',', "\n"));
        m.mem.read_addrs.extend(body.split_whitespace().filter_map(parse_addr));
    }
    m.mem.ide_cfg_ack_off = args.iter().any(|a| a == "--no-cfg-ack");
    m.mem.ide_irq_latch_off = args.iter().any(|a| a == "--no-ide-irq-latch");
    // --pp-dma-irq=N : which interrupt line the 0x60008000 DMA controller's completion drives.
    m.mem.pp_dma_irq = args.iter().find_map(|a| a.strip_prefix("--pp-dma-irq=")).and_then(|v| v.parse().ok());
    // Implies --novelty: the idle test is "no bucket was new", and only novelty tracking knows.
    if let Some(v) = args.iter().find_map(|a| a.strip_prefix("--stop-when-idle=")) {
        m.stop_when_idle = v.replace('_', "").parse::<u64>().ok();
        if m.novelty.is_none() {
            m.novelty = Some(Default::default());
            m.arm_novelty();
        }
    }
    m.mem.read_addrs.sort_unstable();
    m.mem.read_addrs.dedup();
    m.mem.store_addrs.sort_unstable();
    m.mem.store_addrs.dedup();
    m.mem.set_store_addr_bounds();
    for spec in args.iter().filter_map(|a| a.strip_prefix("--enterlog=")) {
        m.enter_pcs.extend(spec.split(',').filter_map(parse_addr));
    }
    // --force-vc-upload / --force-sem=ID[,ID…] : ledger bypass #17, an ablation.
    //
    // Two spellings because the experiment has two halves. `--force-vc-upload` names the one
    // semaphore we have evidence for; `--force-sem` exists because the predicted outcome of the
    // first half is *another* wait on another id, and testing that should not need a rebuild.
    if args.iter().any(|a| a == "--force-vc-upload") {
        m.force_sems.push(0xe0);
    }
    for spec in args.iter().filter_map(|a| a.strip_prefix("--force-sem=")) {
        m.force_sems.extend(spec.split(',').filter_map(parse_addr));
    }
    if let Some(pc) = args.iter().find_map(|a| a.strip_prefix("--force-sem-pc=")).and_then(parse_addr)
    {
        m.force_sem_pend_pc = pc;
    }
    m.force_vc_retire = args.iter().any(|a| a == "--force-vc-retire");
    m.force_sems.sort_unstable();
    m.force_sems.dedup();
    if !m.force_sems.is_empty() {
        let ids: Vec<String> = m.force_sems.iter().map(|s| format!("{s:#x}")).collect();
        println!(
            "  BYPASS #17 active: KS_pend at {:#010x} returns 0 for sem {}",
            m.force_sem_pend_pc,
            ids.join(",")
        );
    }
    for &pc in &m.enter_pcs {
        m.enter_bloom |= 1u64 << ((pc >> 2) & 63);
    }
    if !m.breakpoints.is_empty() {
        println!("breakpoints: {:?}", m.breakpoints.iter().map(|a| format!("{a:#010x}")).collect::<Vec<_>>());
    }
    if let Some(w) = m.watch {
        println!("watching word at {w:#010x}");
    }
    // --map=BASE:SIZE : add a zeroed region before running.
    //
    // The firmware carries pointers into address ranges the emulator does not model — OSOS jumps
    // through a thunk at 0x843d4 to 0x149xxxxx, which is mapped nowhere. Mapping is a question for
    // an experiment, not a guess baked into map_hardware().
    let maps: Vec<(u32, usize)> = args
        .iter()
        .filter_map(|a| a.strip_prefix("--map="))
        .filter_map(|spec| {
            let (b, n) = spec.split_once(':')?;
            Some((parse_addr(b)?, parse_addr(n)? as usize))
        })
        .collect();

    // --poke=ADDR=VALUE : write a 32-bit word before running.
    //
    // The firmware expects state its bootloader left behind. The per-mode stack table at IRAM
    // 0x40006000 is the case that motivated this: OSOS reads it during mode setup, we enter at
    // OSOS's own entry point, and nothing ever filled it — so every mode gets sp = 0.
    let pokes: Vec<(u32, u32)> = args
        .iter()
        .filter_map(|a| a.strip_prefix("--poke="))
        .filter_map(|spec| {
            let (a, v) = spec.split_once('=')?;
            Some((parse_addr(a)?, parse_addr(v)?))
        })
        .collect();

    // --load-on-open: an async open whose request carries a buffer loads the whole file.
    m.load_on_open = args.iter().any(|a| a == "--load-on-open");
    m.game_dir = args
        .iter()
        .find_map(|a| a.strip_prefix("--gamedir="))
        .map(std::path::PathBuf::from);
    for (fw, idx, arg, off, v) in &writes {
        println!("write-out: {fw}#{idx} *(r{arg}+{off}) <- {v}");
        m.set_stub(fw, *idx, Stub::WriteOut { arg: *arg, offset: *off, value: *v, ret: 0 });
    }
    for (fw, idx, v) in &overrides {
        println!("stub override: {fw}#{idx} -> {v:#x}");
        m.set_stub(fw, *idx, Stub::Value(*v));
    }

    // Drive every non-zero vector in turn on one machine, so later ones see what earlier
    // ones set up. Which vector is the main loop is a question for the trace, not a guess.
    // --osos=FILE --run-loader : map RetailOS and execute its own eApp loader, then read back
    // the thunks it patched. Apple's binding logic is the authority on every import address.
    if let Some(path) = args.iter().find_map(|a| a.strip_prefix("--osos=")) {
        match fs::read(path) {
            Ok(d) => {
                let n = d.len();
                // RetailOS executes aliased at 0, so the symbol keys match trace PCs directly.
                m.symbols = eapp_loader::extract_symbols(&d, 0);
                if args.iter().any(|a| a == "--symbols") {
                    println!("recovered {} function names from RetailOS's own labels:", m.symbols.len());
                    for (a, nm) in &m.symbols {
                        println!("  {a:#010x}  {nm}");
                    }
                }
                match m.map_osos(d) {
                    Ok(()) => println!("mapped OSOS: {n} bytes at 0x10000000"),
                    Err(e) => {
                        eprintln!("cannot map OSOS: {e}");
                        std::process::exit(1);
                    }
                }
            }
            Err(e) => eprintln!("{path}: {e}"),
        }
    }
    // --native[=FRAMEWORK] : bind imports to RetailOS's own implementations, found by searching
    // the firmware for each framework's interface hash. No booting — the export table is static.
    if let Some(flag) = args.iter().find(|a| a.starts_with("--native")) {
        let only = flag.strip_prefix("--native=");
        // Export-table pointers are low-mirror addresses (0x26c534, not 0x1026c534), matching
        // where the firmware's own boot executes. Without this region they resolve to nothing.
        if let Some(osos) = m.mem.regions.iter().find(|r| r.name == "osos") {
            let mirror = osos.data.clone();
            m.mem.regions.push(eapp_loader::Region { name: "osos-low", base: 0, data: mirror });
        } else {
            eprintln!("--native needs --osos=FILE");
            std::process::exit(1);
        }
        // RetailOS implementations touch the same memory the firmware's own boot does — its BSS,
        // SDRAM and peripheral windows. Without these they fault on their first global.
        map_hardware(&mut m, args.iter().any(|a| a == "--cold-boot"));
        println!("binding imports to RetailOS implementations:");
        for (name, bound, total) in m.bind_native(&app, only) {
            let mark = if bound == total { "✅" } else if bound == 0 { "·" } else { "⚠️" };
            println!("  {mark} {name:<14} {bound:>3}/{total}");
        }
    }
    // --call=Framework:index[:arg,arg,...] : invoke one RetailOS implementation directly and
    // report how it terminated. The minimal test of whether firmware functions can run at all
    // outside a booted OS — a whole game exercises hundreds at once and says nothing about which.
    let mut called_directly = false;
    for spec in args.iter().filter_map(|a| a.strip_prefix("--call=")) {
        let mut parts = spec.split(':');
        let (Some(fname), Some(idx)) = (parts.next(), parts.next()) else {
            eprintln!("--call=Framework:index[:args]");
            std::process::exit(2);
        };
        let idx: usize = idx.parse().unwrap_or(0);
        let cargs: Vec<u32> = parts
            .next()
            .map(|s| {
                s.split(',')
                    .filter_map(|t| {
                        t.strip_prefix("0x")
                            .and_then(|h| u32::from_str_radix(h, 16).ok())
                            .or_else(|| t.parse().ok())
                    })
                    .collect()
            })
            .unwrap_or_default();
        let Some(fw) = app.frameworks.iter().find(|f| f.name == fname) else {
            eprintln!("no framework {fname:?}");
            std::process::exit(1);
        };
        let Some(target) = m
            .find_exports(&fw.hash, fw.thunks.len())
            .and_then(|e| e.get(idx).copied())
        else {
            eprintln!("no export {fname}#{idx}");
            std::process::exit(1);
        };
        let before = m.executed;
        let stop = m.call_with(target, &cargs, 2_000_000);
        println!(
            "call {fname}#{idx} @{target:#010x}({}) -> {stop:?}  r0={:#x}  {} instructions",
            cargs.iter().map(|a| format!("{a:#x}")).collect::<Vec<_>>().join(", "),
            m.cpu.regs[0],
            m.executed - before
        );
        // Printed here, not at the end: the vector run would otherwise overwrite the ring and
        // the history would describe the wrong code entirely.
        if !matches!(stop, Stop::Returned) {
            let recent = m.recent();
            for a in recent.iter().rev().take(6).rev() {
                println!("      {a:08x}  {}", disasm::arm(m.mem.read32(*a), *a, None));
            }
            // Registers, not just the disassembly: a spin on `ldrh r0,[r6]` says nothing until
            // you know what r6 holds, and inferring it from the surrounding code is how the
            // earlier arity mistakes happened.
            for row in m.cpu.regs.chunks(6).enumerate() {
                let (i, rs) = row;
                let cells: Vec<String> = rs
                    .iter()
                    .enumerate()
                    .map(|(j, v)| format!("r{:<2}={v:08x}", i * 6 + j))
                    .collect();
                println!("      {}", cells.join("  "));
            }
        }
        called_directly = true;
    }
    // --call is for isolating one function; running the game's vectors afterwards would both
    // pollute the trace and disturb whatever state the call left behind.
    if called_directly {
        return;
    }
    // --boot-osos : point the CPU at OSOS's entry and see how far RetailOS gets.
    // The firmware directory records entryOffset 0 against a load address of 0x10000000.
    if args.iter().any(|a| a == "--boot-osos") {
        use arm7tdmi::Bus as _;
        let mut flash_entry: Option<u32> = None;
        // `--bcm-film`'s recorder, built below once the co-processor exists. Declared here because
        // it outlives the setup block and is sampled by the run itself.
        let mut film: Option<eapp_loader::film::Film> = None;
        // The boot code jumps to physical 0x23c, so OSOS must also appear at address 0 — the
        // usual ARM arrangement where the vector table is mirrored into low memory.
        if let Some(osos) = m.mem.regions.iter().find(|r| r.name == "osos") {
            let mirror = osos.data.clone();
            m.mem.regions.push(eapp_loader::Region { name: "osos-low", base: 0, data: mirror });
        }
        // --osos-at=ADDR : an additional mirror of the image at ADDR.
        //
        // RetailOS's scatter-load reads its initialised-data sections straight out of the firmware
        // image, and the source pointers in its literal pool are image-relative to a base that is
        // neither 0 nor the 0x10000000 load address. Mirroring is how we find out which base makes
        // those pointers land inside the image, instead of guessing at the answer.
        for spec in args.iter().filter_map(|a| a.strip_prefix("--osos-at=")) {
            let Some(base) = spec
                .strip_prefix("0x")
                .and_then(|h| u32::from_str_radix(h, 16).ok())
                .or_else(|| spec.parse().ok())
            else {
                continue;
            };
            if let Some(osos) = m.mem.regions.iter().find(|r| r.name == "osos") {
                let mirror = osos.data.clone();
                println!("  osos mirror at {base:#010x} ({} bytes)", mirror.len());
                m.mem.regions.push(eapp_loader::Region { name: "osos-alias", base, data: mirror });
            }
        }
        map_hardware(&mut m, args.iter().any(|a| a == "--cold-boot"));
        // Hardware revision probe: boot reads 0x70000000, takes bits 16..23 and compares to
        // 0x36. Seed it so the PP5021C-specific path is taken rather than the fallback.
        m.mem.write32(0x7000_0000, 0x0036_0000);
        for (base, size) in &maps {
            m.mem.regions.push(eapp_loader::Region {
                name: "extra", base: *base, data: vec![0; *size],
            });
            println!("  map  {base:#010x} .. {:#010x}", base + *size as u32 - 1);
        }
        // --sysinfo[=SIZE] : install the bootloader's IRAM handoff block, SIZE bytes of SDRAM.
        if let Some(spec) = args.iter().find_map(|a| {
            a.strip_prefix("--sysinfo=").map(Some).or(if a == "--sysinfo" { Some(None) } else { None })
        }) {
            let size = spec.and_then(parse_addr).unwrap_or(0x0400_0000);
            install_sysinfo(&mut m, 0x4001_fd00, size);
        }
        // --input-regs=BASE:SIZE : which addresses are read before ever being written.
        if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--input-regs=")) {
            if let Some((b, n)) = spec.split_once(':') {
                if let (Some(b), Some(n)) = (parse_addr(b), parse_addr(n)) {
                    m.mem.input_probe = Some((b, n));
                    println!("  input-regs {b:#010x} .. {:#010x}", b + n - 1);
                }
            }
        }
        // --watch-range=BASE:LEN : every write into a structure, with PC and value.
        if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--watch-range=")) {
            if let Some((b, n)) = spec.split_once(':') {
                if let (Some(b), Some(n)) = (parse_addr(b), parse_addr(n)) {
                    m.mem.watch_range = Some((b, n));
                    println!("  watch-range {b:#010x} .. {:#010x}", b + n - 1);
                }
            }
        }
        // --writelog=BASE:SIZE : record where stores in a range actually land, and from which PC.
        if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--writelog=")) {
            if let Some((b, n)) = spec.split_once(':') {
                if let (Some(b), Some(n)) = (parse_addr(b), parse_addr(n)) {
                    m.mem.write_log = Some((b, n));
                    println!("  writelog {b:#010x} .. {:#010x}", b + n - 1);
                }
            }
        }
        // --pagelog=BASE:SIZE : account for a range at register-block granularity, before the run.
        if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--pagelog=")) {
            let parts: Vec<&str> = spec.split(':').collect();
            if parts.len() >= 2 {
                if let (Some(b), Some(n)) = (parse_addr(parts[0]), parse_addr(parts[1])) {
                    let g = parts.get(2).and_then(|s| parse_addr(s)).unwrap_or(0x100).max(4);
                    m.mem.page_log = Some((b, n));
                    m.mem.page_gran = g.next_power_of_two();
                    println!("  pagelog {b:#010x} .. {:#010x} by {}", b + n - 1, m.mem.page_gran);
                }
            }
        }
        // --flash=PATH : map the 1 MB NOR Flash ROM at 0x20000000.
        //
        // It holds the first-stage bootloader plus the `flsh` directory at +0xffe00 indexing the
        // disk-mode, diagnostics, scan, logo and **vmcs** images. `vmcs` is the firmware uploaded to
        // the Broadcom BCM2722 video co-processor during display bring-up, and it exists nowhere
        // else — not in OSOS, not in Apple's updaters (where the equivalent `aupd` section is
        // RC4-encrypted). Without it the display cannot come up.
        if let Some(path) = args.iter().find_map(|a| a.strip_prefix("--flash=")) {
            match std::fs::read(path) {
                Ok(data) => {
                    println!("  flash {path} — {} bytes at 0x20000000", data.len());
                    // --cold-boot : the flash also answers at 0 out of reset, which is where the
                    // CPU fetches its first instruction. Pushed ahead of the OSOS mirror so it
                    // wins the lookup for low addresses.
                    if args.iter().any(|a| a == "--cold-boot") {
                        println!("  flash also at 0x00000000 (cold boot, read-only)");
                        m.mem.readonly.push("flash-low");
                        m.mem.regions.insert(
                            0,
                            eapp_loader::Region { name: "flash-low", base: 0, data: data.clone() },
                        );
                    }
                    let size = data.len() as u32;
                    // The NOR window is read-only for the same reason the low alias is: a store
                    // does not change a flash cell, a command sequence does. Leaving it writable
                    // meant the one place an errant write could land silently was the image the
                    // whole boot is read out of.
                    m.mem.readonly.push("flash");
                    m.mem.regions.push(eapp_loader::Region {
                        name: "flash",
                        base: 0x2000_0000,
                        data,
                    });
                    // --nor : answer the ROM's JEDEC identify instead of leaving the chip as bytes.
                    // Ledger bypass #12 — see the Nor doc comment for what the ROM's probe expects
                    // and which of the eight rows in its device table this answers as.
                    if args.iter().any(|a| a == "--nor") {
                        let mut windows = vec![(0x2000_0000, size)];
                        let mut regions = vec!["flash"];
                        if args.iter().any(|a| a == "--cold-boot") {
                            windows.push((0, size));
                            regions.push("flash-low");
                        }
                        let nor = eapp_loader::Nor::sst39wf800a(windows, regions);
                        println!(
                            "  nor model: JEDEC {:#06x}/{:#06x}, {} KiB, {} KiB sectors",
                            nor.mfr,
                            nor.dev,
                            size / 1024,
                            nor.sector / 1024
                        );
                        m.mem.nor = Some(nor);
                    }
                }
                Err(e) => println!("  flash {path}: {e}"),
            }
        }
        // --bcm : model the video co-processor's host protocol instead of leaving it as memory.
        if args.iter().any(|a| a == "--bcm") {
            let mut b = eapp_loader::Bcm::new(0x3000_0000);
            // --bcm-registry : publish the GENCMD service directory RetailOS reads at internal
            // 0x1f0, and answer the ring RPC behind it. Off by default — every baseline number in
            // research/20 was measured with the co-processor as a memory and a protocol.
            b.registry = args.iter().any(|a| a == "--bcm-registry");
            if b.registry {
                println!("  bcm gencmd registry: publishing a tag-2 display service");
            }
            m.mem.bcm = Some(b);
            println!("  bcm model at 0x30000000");
        }
        // --clickwheel : model the wheel's four registers instead of answering them with zero.
        // --wheel=SCRIPT : inject a sequence, and imply --clickwheel.
        // --wheel-click-instr=N : instructions between the frames of a rotate (default 20000, which
        //   at --clock=5 is 4 ms per click — a brisk but human scroll).
        // --wheel-no-irq : model the registers but never raise IRQ 40. The ablation that separates
        //   "the firmware read a frame" from "the firmware was interrupted into reading one".
        //
        // The script is expanded and PRINTED before the run, so the schedule in the log is the
        // schedule that executed and a run is reproducible from its own output.
        let wheel_spec = args.iter().find_map(|a| a.strip_prefix("--wheel="));
        if args.iter().any(|a| a == "--clickwheel") || wheel_spec.is_some() {
            let mut w = eapp_loader::ClickWheel::new(0x7000_c000);
            w.irq_enabled = !args.iter().any(|a| a == "--wheel-no-irq");
            let gap = args
                .iter()
                .find_map(|a| a.strip_prefix("--wheel-click-instr="))
                .and_then(|v| v.parse::<u64>().ok())
                .unwrap_or(20_000)
                .max(1);
            if let Some(spec) = wheel_spec {
                match eapp_loader::parse_wheel_script(spec, gap) {
                    Ok(steps) => w.script = steps,
                    // Refused rather than partially applied: a script that silently drops the step
                    // it could not parse would report a delta from a sequence nobody wrote.
                    Err(e) => {
                        println!("  --wheel: {e}");
                        std::process::exit(2);
                    }
                }
            }
            println!(
                "  clickwheel model at 0x7000c100/0x104/0x120/0x140, irq {} ({})",
                eapp_loader::OPTO_IRQ_HI + 32,
                if w.irq_enabled { "enabled" } else { "ablated by --wheel-no-irq" }
            );
            if w.script.is_empty() {
                println!("  wheel script: none — the device is modelled, nothing is injected");
            } else {
                println!(
                    "  wheel script: {} steps, {gap} instructions per click",
                    w.script.len()
                );
                for s in &w.script {
                    println!("    @{:<12} {}", s.at, eapp_loader::wheel_step_name(s.event));
                }
            }
            m.mem.clickwheel = Some(w);
        }
        // --boot-flash=NAME : load one of the flash's own images and enter it.
        //
        // The `flsh` directory at +0xffe00 indexes self-contained payloads: `diag` (Apple's
        // diagnostics), `disk` (disk mode), `scan`, `logo`, `vmcs`. `diag` in particular needs no
        // disk, no filesystem and no OSOS, and it draws to the screen — which makes it the natural
        // first target for the display path.
        let flash_image = args.iter().find_map(|a| a.strip_prefix("--boot-flash="));
        if let (Some(name), Some(path)) =
            (flash_image, args.iter().find_map(|a| a.strip_prefix("--flash=")))
        {
            if let Ok(rom) = std::fs::read(path) {
                let want = u32::from_be_bytes(
                    name.as_bytes()[..4].try_into().unwrap_or([0; 4]),
                );
                let rd = |o: usize| {
                    u32::from_le_bytes(rom[o..o + 4].try_into().unwrap_or([0; 4]))
                };
                for i in 0..12 {
                    let e = 0xffe00 + i * 40;
                    if e + 40 > rom.len() || rd(e) != u32::from_be_bytes(*b"flsh") {
                        break;
                    }
                    if rd(e + 4) == want {
                        let (off, len, load) =
                            (rd(e + 12) as usize, rd(e + 16) as usize, rd(e + 20));
                        println!(
                            "  flash image {name}: {len} bytes at flash+{off:#x} -> {load:#010x}"
                        );
                        m.mem.regions.insert(
                            0,
                            eapp_loader::Region {
                                name: "flash-image",
                                base: load,
                                data: rom[off..(off + len).min(rom.len())].to_vec(),
                            },
                        );
                        flash_entry = Some(load);
                    }
                }
            }
        }
        // --disk=PATH : attach the image as the ATA drive, so RetailOS can read its own filesystem.
        if let Some(path) = args.iter().find_map(|a| a.strip_prefix("--disk=")) {
            match eapp_loader::Ata::open(std::path::Path::new(path), args.iter().any(|a| a == "--disk-writable")) {
                Ok(d) => {
                    println!("  disk {path} — {} sectors ({} MB)", d.sectors, d.sectors / 2048);
                    m.mem.ata = Some((0xc300_0000, d));
                }
                Err(e) => println!("  disk {path}: {e}"),
            }
        }
        if args.iter().any(|a| a == "--i2c")
            || args.iter().any(|a| a.starts_with("--i2c-fill="))
            || args.iter().any(|a| a == "--pmu")
        {
            m.mem.i2c_base = Some(0x7000_c000);
        }
        if let Some(v) = args.iter().find_map(|a| a.strip_prefix("--i2c-fill=")) {
            m.mem.i2c_fill = parse_addr(v).map(|x| x as u8);
        }
        // --pmu : a modelled PCF50605 instead of a fixed answer. Takes precedence over --i2c-fill,
        // so the two can be given together and the device wins — which is what makes them
        // comparable in one run of the recipe.
        if args.iter().any(|a| a == "--pmu") {
            let mut pmu = eapp_loader::Pcf50605::new();
            // The emulated iPod reports the charge of the machine it is running on, and its clock
            // is the host's local time. --battery=N overrides the percentage; a host with no
            // battery reads 100. Both are set before the flags below so an explicit --pmu-adc=2
            // still wins.
            let pct = args
                .iter()
                .find_map(|a| a.strip_prefix("--battery="))
                .and_then(|n| n.parse::<u8>().ok())
                .unwrap_or_else(eapp_loader::host_battery_percent);
            pmu.set_battery_percent(pct);
            let tm = eapp_loader::host_local_time();
            pmu.set_clock(tm);
            println!(
                "  pcf50605 battery {pct}%, clock 20{:02}-{:02}-{:02} {:02}:{:02}:{:02}",
                tm[6], tm[5], tm[4], tm[2], tm[1], tm[0]
            );
            // --pmu-adc=CH=VALUE, repeatable: answer one ADC channel on its own scale.
            //
            // This pushed into `m.mem.pmu` until 2026-08-14 — the device that existed *before* this
            // block, which on every recipe is `None` — and then the `m.mem.pmu = Some(pmu)` below
            // replaced it with the freshly built chip whose `adc_values` was empty. **The flag was
            // a no-op in every run that ever used it**, and it printed a confirmation line while
            // being one, which is why research/19's channel sweep concluded that no ADC value lets
            // the boot proceed. It was sweeping a value the device never saw. `--pmu-force` sat
            // three lines below and pushed into the right object, which is why forcing worked and
            // is what made the difference look like a fact about the firmware.
            for spec in args.iter().filter_map(|a| a.strip_prefix("--pmu-adc=")) {
                if let Some((c, v)) = spec.split_once('=') {
                    if let (Some(c), Some(v)) = (parse_addr(c), parse_addr(v)) {
                        pmu.adc_values.push((c as u8, v as u16));
                        println!("  pcf50605 ADC channel {c:#x} answers {v:#06x}");
                    }
                }
            }
            for spec in args.iter().filter_map(|a| a.strip_prefix("--pmu-force=")) {
                if let Some((r, v)) = spec.split_once('=') {
                    if let (Some(r), Some(v)) = (parse_addr(r), parse_addr(v)) {
                        pmu.force.push((r as u8, v as u8));
                        println!("  pcf50605 reg {r:#04x} forced to {v:#04x}");
                    }
                }
            }
            m.mem.pmu = Some(pmu);
            println!("  pcf50605 modelled at i2c 0x08");
        }
        // --rdval=ADDR=VALUE : make a word always read as VALUE, whatever is written to it.
        //
        // For status bits belonging to hardware we do not model. Kept as a flag rather than baked
        // into map_hardware() so a candidate can be tried without a rebuild — several of these are
        // registers absent from every published map, and each one is a hypothesis until the run
        // says otherwise.
        // --rdtoggle=ADDR=A:B : a word that alternates between A and B on each read, for busy flags.
        for spec in args.iter().filter_map(|a| a.strip_prefix("--rdtoggle=")) {
            if let Some((a, rest)) = spec.split_once('=') {
                if let Some((x, y)) = rest.split_once(':') {
                    if let (Some(a), Some(x), Some(y)) = (parse_addr(a), parse_addr(x), parse_addr(y)) {
                        m.mem.read_toggle.push((a, x, y));
                        println!("  rdtoggle {a:#010x} alternates {x:#010x} / {y:#010x}");
                    }
                }
            }
        }
        for spec in args.iter().filter_map(|a| a.strip_prefix("--rdval=")) {
            if let Some((a, v)) = spec.split_once('=') {
                if let (Some(a), Some(v)) = (parse_addr(a), parse_addr(v)) {
                    m.mem.read_overrides.push((a, v));
                    println!("  rdval {a:#010x} always reads {v:#010x}");
                }
            }
        }
        // --clock=N : interpreter instructions per simulated microsecond (default 75 = real time).
        if let Some(n) = args.iter().find_map(|a| a.strip_prefix("--clock=")) {
            if let Ok(n) = n.parse::<usize>() {
                m.instr_per_usec = n.max(1);
                println!("  clock {n} instructions per simulated microsecond");
            }
        }
        // --snapshot=N:FILE : run N instructions, then write the whole machine to FILE.
        // --restore=FILE     : start from a saved machine instead of from reset.
        let snap_spec = args.iter().find_map(|a| a.strip_prefix("--snapshot="));
        if let Some(spec) = snap_spec {
            if let Some((n, _)) = spec.split_once(':') {
                if let Ok(n) = n.parse::<usize>() {
                    m.snap_at = Some(n);
                    println!("  snapshot after {n} instructions");
                }
            }
        }
        if let Some(path) = args.iter().find_map(|a| a.strip_prefix("--restore=")) {
            match std::fs::read(path) {
                Ok(b) if m.restore(&b) => {
                    println!(
                        "  restored {path} — {} instructions already executed, pc {:#010x}",
                        m.executed, m.cpu.regs[15]
                    );
                }
                Ok(_) => println!("  restore {path}: not a valid snapshot"),
                Err(e) => println!("  restore {path}: {e}"),
            }
        }
        // --bcm-film=ADDR:W:H:EVERY:DIR : record the panel over the whole run, not just at its end.
        //
        // Refused rather than ignored when there is no co-processor to read: `--bcm-dump` prints
        // nothing in that case and is easy to miss, and a film that silently records zero frames
        // would be indistinguishable from a screen that never changed.
        if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--bcm-film=")) {
            if m.mem.bcm.is_none() {
                eprintln!("--bcm-film needs a co-processor to read: add --bcm (and --bcm-registry).");
                std::process::exit(2);
            }
            match eapp_loader::film::Film::parse(spec) {
                Ok(mut f) => {
                    // --bcm-film-from=N : start sampling at N instructions. The run is still issued
                    // in `every`-sized chunks from instruction 0, so the machine is unchanged; what
                    // is skipped is the surface scan, which is the whole cost of a fine cadence.
                    if let Some(v) = args.iter().find_map(|a| a.strip_prefix("--bcm-film-from=")) {
                        match eapp_loader::film::parse_count(v) {
                            Some(n) => f.from = n,
                            None => {
                                println!("  --bcm-film-from={v:?} is not a number (try 2400M, 500k)");
                                std::process::exit(2);
                            }
                        }
                    }
                    println!(
                        "  film {:#010x} {}x{} every {} instructions{} -> {}",
                        f.base,
                        f.w,
                        f.h,
                        f.every,
                        if f.from > 0 { format!(", from {}", f.from) } else { String::new() },
                        f.dir.display()
                    );
                    film = Some(f);
                }
                Err(e) => {
                    println!("  {e}");
                    std::process::exit(2);
                }
            }
        }
        if args.iter().any(|a| a == "--devices") {
            m.mem.accounting = true;
        }
        if args.iter().any(|a| a == "--calls") {
            m.call_log_on = true;
        }
        if args.iter().any(|a| a.starts_with("--novelty")) {
        m.novelty = Some(Default::default());
        m.arm_novelty();
    }
    if args.iter().any(|a| a.starts_with("--profile")) {
            m.profile = Some(std::collections::HashMap::new());
        }
        // --profile-window=FROM:TO : sample only inside an instruction range, so a phase that is
        // outnumbered 4:1 by the loop the boot ends in can still be read.
        if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--profile-window=")) {
            if let Some((a, b)) = spec.split_once(':') {
                if let (Ok(a), Ok(b)) = (a.parse::<u64>(), b.parse::<u64>()) {
                    m.profile_window = Some((a, b));
                    println!("  profile samples only [{a}, {b})");
                }
            }
        }
        for (a, v) in &pokes {
            m.mem.write32(*a, *v);
            println!("  poke {a:#010x} <- {v:#010x}");
        }
        m.log_indirect = true;
        // `budget` is the parsed --budget/positional value. It used to be ignored here in favour
        // of a hardcoded 200M, so a sweep across six budgets produced six identical runs and read
        // as steady progress. A flag that is accepted and ignored is worse than one that is
        // rejected, so honour it — falling back to 200M only when nothing was asked for.
        let boot_budget = if budget == DEFAULT_BUDGET { 200_000_000 } else { budget };
        // A cold boot enters Apple's first-stage bootloader at 0 and lets *it* set the machine up
        // — the sysinfo block, the Gestalt ID, the memory-bank sizes — instead of us reconstructing
        // that state by hand. It then loads OSOS off the firmware partition itself.
        let cold = args.iter().any(|a| a == "--cold-boot");
        let entry = flash_entry.unwrap_or(if cold { 0x0000_0000 } else { 0x1000_0000 });
        // A restored machine continues from where it was; only a fresh one enters at `entry`.
        let restored = args.iter().any(|a| a.starts_with("--restore="));
        // What `--boot-osos` actually requires is an image at the entry — which is NOT the same as
        // requiring `--osos=`, and conflating the two is what kept a pre-placed `OSOS_correct.bin`
        // in the cold-boot recipe (ledger #14). A cold boot enters the NOR at 0 and the ROM loads
        // the image itself; only a warm boot, which starts at 0x10000000 with SDRAM freshly zeroed,
        // has nothing to execute. Say so, rather than running 200M instructions of zeros.
        if !cold && !restored && flash_entry.is_none() && m.mem.region_named("osos").is_none() {
            eprintln!(
                "--boot-osos enters {entry:#010x}, where a warm boot has only zeroed SDRAM.\n\
                 Give it an image: --osos=FILE, or --cold-boot so the ROM loads one off --disk."
            );
            std::process::exit(1);
        }
        println!(
            "booting {} at {entry:#010x} (budget {boot_budget}) …",
            if cold { "FLASH (cold)" } else { "OSOS" }
        );
        // Without a film this is one call. With one it is the same total number of loop iterations
        // split into chunks, with a sample of the co-processor's surface between them.
        //
        // Chunking is behaviour-neutral by construction, and that is worth spelling out because a
        // recording instrument that perturbs the thing it records is this project's oldest failure
        // mode. `Machine::run(n)` runs `n` iterations of its loop and returns `BudgetExhausted`
        // having consumed exactly `n`; issuing the same total in pieces issues the same iterations
        // in the same order. The only per-call work is `Memory::invalidate_fast`, which drops a
        // resolution cache — it cannot change what an access resolves to, only how fast. The
        // measured arm of that claim is in `tools/ipod-film/README.md`: the same recipe filmed and
        // unfilmed reaches Idle at the same instruction, with the same buckets, ATA commands and
        // final frame digest.
        let stop = match film.as_mut() {
            None => {
                if restored {
                    m.run(boot_budget)
                } else {
                    m.call_with(entry, &[0, 0, 0, 0], boot_budget)
                }
            }
            Some(f) => {
                // Frame 0 is the machine as it stands before a single instruction of this run —
                // for a restored machine that is the snapshot's screen, which is a real frame.
                if let Some(b) = &m.mem.bcm {
                    f.sample(b, m.executed as u64);
                }
                let chunk = (f.every as usize).max(1);
                let mut left = boot_budget;
                let mut stop = Stop::BudgetExhausted;
                let mut entered = restored;
                while left > 0 {
                    let n = chunk.min(left);
                    stop = if entered {
                        m.run(n)
                    } else {
                        entered = true;
                        m.call_with(entry, &[0, 0, 0, 0], n)
                    };
                    left -= n;
                    if let Some(b) = &m.mem.bcm {
                        f.sample(b, m.executed as u64);
                    }
                    if !matches!(stop, Stop::BudgetExhausted) {
                        break;
                    }
                }
                stop
            }
        };
        if let Some(spec) = snap_spec {
            if let Some((_, path)) = spec.split_once(':') {
                let img = m.snapshot();
                match std::fs::write(path, &img) {
                    Ok(()) => println!("  snapshot -> {path} ({} bytes)", img.len()),
                    Err(e) => println!("  snapshot {path}: {e}"),
                }
            }
        }
        println!("  -> {stop:?} after {} instructions", m.executed);
        // `Idle` names a novelty stall, not a quiet machine, and the bare line above was read as
        // the second for a whole day. Say which it was: a machine that is waiting asks the core to
        // sleep, so zero sleeps across the trailing window means it was busy in a loop over code it
        // had already run — raise --stop-when-idle rather than conclude a call never returns.
        if matches!(stop, Stop::Idle) {
            let win = m.executed as u64 - m.last_novel;
            let naps = m.mem.sleeps - m.last_novel_sleeps;
            println!(
                "     last new code @{}; {win} instructions since, {naps} CPU sleeps in them{}",
                m.last_novel,
                if naps == 0 {
                    "  <- BUSY, not blocked: raise --stop-when-idle"
                } else {
                    ""
                }
            );
        }
        if !m.output.is_empty() {
            println!("  firmware output: {}", m.output.trim_end());
        }
        // Recover RetailOS's own function labels from the image the ROM loaded, when no image was
        // handed to us. This is what let `--osos=` leave the cold-boot recipe (ledger #14): a cold
        // boot exists to make the ROM find `osos` on disk and DMA it into SDRAM itself, so handing
        // it a known-good copy of the same image at the same address was circular. The only thing
        // the copy was still buying was `extract_symbols`, and symbols are a reporting instrument —
        // `--novelty`, `--profile` and `--callgraph` all name addresses *after* the run.
        //
        // Reading them out of memory is also the more honest source: it describes the image that
        // executed rather than the file we believe it to be. Bounded by the DMA high-water mark
        // because SDRAM past the image is RetailOS's heap, and a heap full of C++ strings would
        // manufacture symbols out of allocations — which it does: an unbounded scan of the 64 MB
        // region recovers 141 names against the file's 140, and the extra one is heap.
        //
        // Only transfers whose destination lands INSIDE the region count. RetailOS's own later
        // reads go to 0x17edbea0 and 0x93eea730, neither of which is SDRAM, and a plain maximum
        // over every transfer would take one of those, clamp to the region end, and put the heap
        // back in scope.
        if m.symbols.is_empty() {
            let sdram = m.mem.regions.iter().find(|r| r.name == "sdram").map(|r| {
                (r.base, r.base.wrapping_add(r.data.len() as u32), r.data.clone())
            });
            let recovered = sdram.and_then(|(base, end, data)| {
                let top = m.mem.ata.as_ref()?.1.dma_transfers.iter()
                    .filter(|(_, dest, _)| (base..end).contains(dest))
                    .map(|(_, dest, n)| dest.wrapping_add(*n))
                    .max()?;
                let n = (top.saturating_sub(base) as usize).min(data.len());
                Some(eapp_loader::extract_symbols(&data[..n], 0))
            });
            if let Some(syms) = recovered {
                println!("  recovered {} function names from loaded SDRAM", syms.len());
                if args.iter().any(|a| a == "--symbols") {
                    for (a, nm) in &syms {
                        println!("    {a:#010x}  {nm}");
                    }
                }
                m.symbols = syms;
            }
        }
        if let Some(nov) = &m.novelty {
            let n: usize = args.iter().find_map(|a| a.strip_prefix("--novelty="))
                .and_then(|v| v.parse().ok()).unwrap_or(24);
            let mut rows: Vec<_> = nov.iter().map(|(a, t)| (*t, *a)).collect();
            rows.sort_unstable();
            println!("\n{} code buckets executed; last {n} to run for the FIRST time:", rows.len());
            for (t, a) in rows.iter().rev().take(n).rev() {
                let name = m.symbolise(*a).unwrap_or_default();
                println!("  at {t:>13} instructions  {a:#010x}  {name}");
            }
        }
        let depth: usize = args
            .iter()
            .filter_map(|a| a.strip_prefix("--history="))
            .filter_map(|s| s.parse().ok())
            .next()
            .unwrap_or(16);
        println!("  last instructions:");
        for a in m.recent().iter().rev().take(depth).rev() {
            let w = m.mem.read32(*a);
            println!("    {a:08x}  {}", disasm::arm(w, *a, None));
        }
        // The register file at the halt. A crash is a value that went wrong somewhere upstream,
        // and the disassembly alone never says which register held it.
        println!("  registers at halt:");
        for row in 0..4 {
            let cells: Vec<String> = (0..4)
                .map(|c| {
                    let i = row * 4 + c;
                    format!("r{i:<2}={:08x}", m.cpu.regs[i])
                })
                .collect();
            println!("    {}", cells.join("  "));
        }
        println!(
            "  irqs: {} asserted, {} taken; usec {}",
            m.irqs_asserted, m.irqs_taken, m.mem.usec
        );
        // The drive's line specifically. "Interrupts are being taken" is a statement about the
        // timers; whether the disk's completion ever reaches the CPU is a different question and
        // has to be counted separately.
        let (raised, acked, deliv) = (m.mem.ide_irq_raised, m.mem.ide_irq_acked, m.mem.ide_irq_delivered);
        let en = m.mem.read32(0x6000_4020) >> eapp_loader::IDE_IRQ & 1;
        let pend = m.mem.int_pending >> eapp_loader::IDE_IRQ & 1;
        if m.mem.dma_dropped > 0 {
            println!(
                "  DMA DROPPED {} bytes at {} destinations, first {:#010x}",
                m.mem.dma_dropped,
                m.mem.dma_drop_sites.len(),
                m.mem.dma_drop_sites[0].0
            );
        } else {
            println!("  dma: every staged byte landed");
        }
        println!(
            "  ide irq: raised {} times, DELIVERED to a handler {} times, acked by status read {} times; enabled={} pending={}",
            raised, deliv, acked, en, pend,
        );
        // The PP502x DMA controllers. Separate from the ATA engine above — different hardware,
        // different register block, and the only interesting thing about it is *where* it was
        // pointed, which is a handful of lines rather than a histogram.
        println!(
            "  pp dma: {} transfers, {} bytes",
            m.mem.pp_dma_transfers, m.mem.pp_dma_bytes
        );
        for (base, src, dst, len) in m.mem.pp_dma_log.iter() {
            println!("    ch {base:#010x}  {src:#010x} -> {dst:#010x}  {len} bytes");
        }
        if let Some(l) = m.mem.pp_dma_log.more_line(m.mem.pp_dma_log.sample().len()) {
            println!("  {l}");
        }
        for (i, c) in eapp_loader::PP_DMA.iter().enumerate() {
            let irq = if i == 0 { m.mem.pp_dma_irq.unwrap_or(c.irq) } else { c.irq };
            let en = m.mem.read32(0x6000_4020) >> irq & 1;
            let pend = m.mem.int_pending >> irq & 1;
            println!(
                "    ctl {:#010x} master={:#010x} irq {irq} enabled={en} pending={pend}",
                c.master,
                m.mem.read32(c.master),
            );
        }
        if m.mem.accounting {
            println!("\ndevices touched:");
            for line in m.mem.device_report() {
                println!("  {line}");
            }
        }
        // The last run of data writes is never terminated by a discontinuity, so without this the
        // report is one run short and the missing one is the most recent.
        if let Some(b) = &mut m.mem.bcm {
            b.flush_run();
        }
        if let Some(b) = &m.mem.bcm {
            println!(
                "\nbcm: {} commands kicked, {} frame updates",
                b.commands.len(),
                b.frames
            );
            if !b.commands.is_empty() {
                let mut cmds = String::new();
                for (i, c) in b.commands.iter().enumerate() {
                    if i > 0 {
                        cmds.push_str(", ");
                    }
                    cmds.push_str(&format!("{c:#x}"));
                }
                println!("  in order: {cmds}");
            }
            println!(
                "bcm: {} halfwords written, {} read, {} internal words held",
                b.halfwords_written,
                b.halfwords_read,
                b.mem.len()
            );
            if b.registry {
                let mut by_op: std::collections::BTreeMap<u32, usize> = Default::default();
                for (op, _) in &b.gencmd {
                    *by_op.entry(*op).or_insert(0) += 1;
                }
                println!(
                    "bcm gencmd: {} requests answered, {} dropped",
                    b.gencmd.len(),
                    b.gencmd_dropped
                );
                for (op, n) in by_op {
                    println!("  opcode {op:#04x}  x{n}");
                }
            }
            println!("  address latches: {}", b.latch_log.census());
            for (kind, off, val, high) in b.latch_log.iter().take(12) {
                println!("  latch {kind} off {off:#07x} val {val:#06x} {}", if *high { "HI" } else { "lo" });
            }
            if let Some(l) = b.latch_log.more_line(12) {
                println!("{l}");
            }
            // Contiguous runs in the co-processor's address space. A framebuffer shows up here as
            // one large run; the firmware upload as another.
            let mut runs: Vec<(u32, u32, usize)> = Vec::new();
            for (&a, _) in b.mem.iter() {
                match runs.last_mut() {
                    Some((_, end, n)) if a == *end + 2 => {
                        *end = a;
                        *n += 1;
                    }
                    _ => runs.push((a, a, 1)),
                }
            }
            runs.sort_by_key(|(_, _, n)| std::cmp::Reverse(*n));
            // --bcm-ppm=FILE[:BASE] — render the co-processor's framebuffer.
            //
            // The BCM's internal space is sparse, so the framebuffer is wherever the host chose to
            // put it; the write-run report names it. Rockbox lands a full 320x240 RGB565 frame at
            // 0x000e0000, which is the default.
            if let Some(spec) = args.iter().find_map(|a| a.strip_prefix("--bcm-ppm=")) {
                let (path, base) = match spec.rsplit_once(':') {
                    Some((p, b)) => (p, u32::from_str_radix(b.trim_start_matches("0x"), 16).unwrap_or(0x000e_0000)),
                    None => (spec, 0x000e_0000),
                };
                let (w, h) = (320usize, 240usize);
                let mut out = format!("P6\n{w} {h}\n255\n").into_bytes();
                let mut nonzero = 0u32;
                for i in 0..w * h {
                    let px = *b.mem.get(&(base + (i as u32) * 2)).unwrap_or(&0);
                    if px != 0 { nonzero += 1; }
                    // RGB565 -> RGB888, replicating high bits into the low ones so white is 0xff.
                    let (r, g, bl) = ((px >> 11) & 0x1f, (px >> 5) & 0x3f, px & 0x1f);
                    out.push(((r << 3) | (r >> 2)) as u8);
                    out.push(((g << 2) | (g >> 4)) as u8);
                    out.push(((bl << 3) | (bl >> 2)) as u8);
                }
                match std::fs::write(path, &out) {
                    Ok(()) => println!("  bcm framebuffer -> {path} ({w}x{h} from {base:#010x}, {nonzero} non-black pixels)"),
                    Err(e) => println!("  {path}: {e}"),
                }
            }
            // What the co-processor was ASKED to do, in order — data runs, commands, and the image
            // operations the commands became. Consecutive write runs of the same length at a
            // constant stride fold into one line: that is the shape of a row-by-row rect upload,
            // and printing 240 identical rows would bury it.
            println!("  co-processor timeline: {}", b.timeline.seen());
            let rs = b.timeline.sample();
            let mut i = 0usize;
            let mut printed = 0usize;
            let run_of = |op: &eapp_loader::BcmOp| match *op {
                eapp_loader::BcmOp::Write { base, halfwords } => Some((base, halfwords)),
                _ => None,
            };
            while i < rs.len() && printed < 32 {
                match rs[i] {
                    eapp_loader::BcmOp::Command { cmd } => {
                        println!("    command {cmd:#x}");
                        i += 1;
                    }
                    eapp_loader::BcmOp::Blit { x0, y0, x1, y1, src } => {
                        println!(
                            "      -> blit {}x{} to ({x0},{y0})-({x1},{y1}) from {src:#010x}",
                            x1 - x0 + 1,
                            y1 - y0 + 1
                        );
                        i += 1;
                    }
                    eapp_loader::BcmOp::Write { base, halfwords } => {
                        let stride = match rs.get(i + 1).and_then(run_of) {
                            Some((nb, nl)) if nl == halfwords => nb as i64 - base as i64,
                            _ => 0,
                        };
                        let mut j = i + 1;
                        while stride != 0 {
                            match (run_of(&rs[j - 1]), rs.get(j).and_then(run_of)) {
                                (Some((pb, _)), Some((nb, nl)))
                                    if nl == halfwords && nb as i64 - pb as i64 == stride =>
                                {
                                    j += 1
                                }
                                _ => break,
                            }
                        }
                        let n = j - i;
                        if n > 1 {
                            println!(
                                "    {base:#010x}  {halfwords} halfwords  x{n} rows, stride {stride} bytes"
                            );
                        } else {
                            println!("    {base:#010x}  {halfwords} halfwords");
                        }
                        i = j;
                    }
                }
                printed += 1;
            }
            if let Some(l) = b.timeline.more_line(rs.len()) {
                println!("  {l}");
            }
            if b.blits_rejected.seen() > 0 {
                println!(
                    "  rect commands this model would not honour: {}",
                    b.blits_rejected.seen()
                );
                for hdr in b.blits_rejected.iter() {
                    println!(
                        "    header {:#x} x0={} y0={} x1={} y1={} len={:#x}",
                        hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[7]
                    );
                }
            }
            println!("  internal write runs (largest first):");
            for (start, end, n) in runs.iter().take(6) {
                println!("    {start:#010x}..{end:#010x}  {n} halfwords ({} bytes)", n * 2);
            }
            // Every distinct offset, not the top 6, and a reconciliation line against the total.
            //
            // The cap made this report unusable for the question it is most often asked: "does the
            // firmware ever read back what it uploaded?" A 6-row list summing to 38 sat directly
            // under a header saying 56 reads, and the 18 missing ones are exactly where an answer
            // would hide. Sorted by address rather than count so a contiguous read-back region
            // shows up as a run instead of being scattered through a frequency ranking.
            let mut rows: Vec<_> = b.read_hist.iter().collect();
            rows.sort_by_key(|(a, _)| **a);
            let shown: u64 = rows.iter().map(|(_, n)| **n).sum();
            println!(
                "  internal reads: {} distinct offsets, {shown} of {} accounted for",
                rows.len(),
                b.halfwords_read
            );
            for (a, n) in rows.iter().take(64) {
                println!("  internal {a:#010x} read {n} times");
            }
            if rows.len() > 64 {
                println!("  … and {} more offsets", rows.len() - 64);
            }
        }
        // The film's own report: every frame, when it appeared and how long it held. Printed here
        // rather than written only to the manifest, so a run's log carries its own contact sheet.
        if let Some(f) = film.as_mut() {
            print!("{}", f.finish());
        }
        if let Some(n) = &m.mem.nor {
            // Named rather than numbered, because "0x30 x256" says nothing and "sector erase x256"
            // says whether the update ran. `unknown` is the self-check: a non-empty list is a
            // command set we are answering wrong, which no other line in this report would show.
            let name = |c: u16| match c {
                0x10 => "chip erase",
                0x30 => "sector erase",
                0x80 => "erase setup",
                0x90 => "autoselect",
                0x98 => "CFI query",
                0xa0 => "program setup",
                0xaa | 0x55 => "unlock",
                0xf0 => "reset",
                0xff => "reset (Intel)",
                _ => "?",
            };
            println!(
                "\nnor: {} sector erases, {} words programmed",
                n.erases, n.programs
            );
            for (c, k) in &n.cmds {
                println!("  cycle {c:#04x} {:<14} x{k}", name(*c));
            }
            // The census first: an undecoded cycle is a command set we answer wrong, and "how
            // many" is the whole verdict. A capped list under a capped count would have hidden it.
            if !n.unknown.is_empty() {
                println!("  UNDECODED cycles: {}", n.unknown.census());
            }
            for (a, v) in n.unknown.iter() {
                println!("  UNDECODED cycle at {a:#010x} = {v:#06x}");
            }
            if let Some(l) = n.unknown.more_line(n.unknown.sample().len()) {
                println!("{l}");
            }
        }
        if let Some((_, d)) = &m.mem.ata {
            if !d.cfg_writes.is_empty() {
                // The per-register totals come from the device's own uncapped tally, not from the
                // ordered log below them — that log stops at 512 and the histogram under it used to
                // stop with it, silently.
                println!(
                    "\nide controller registers written: {} byte-writes across {} registers",
                    d.cfg_writes.seen(),
                    d.cfg_writes_by_reg.len()
                );
                for (r, n) in &d.cfg_writes_by_reg {
                    println!("  IDE_BASE+{r:#05x}  {n} byte-writes");
                }
                println!("  first 24 in order ({} kept of {} — SAMPLE):", d.cfg_writes.sample().len(), d.cfg_writes.seen());
                let mut line = String::new();
                for (o, v) in d.cfg_writes.iter().take(24) {
                    line.push_str(&format!("{o:#05x}={v:02x} "));
                }
                println!("    {line}");
            }
            if !d.reads_log.is_empty() {
                use std::collections::BTreeMap;
                let per: BTreeMap<u32, u64> = BTreeMap::new();
                let _ = per;
                println!("\nide window reads ({} bytes handed over):", d.bytes_read);
                for (o, n) in d.reads_log.iter() {
                    println!("  IDE_BASE+{o:#05x}  x{n}");
                }
            }
            if m.mem.sleeps > 0 {
                println!(
                    "\ncpu sleep: {} halts, {} ms of simulated time skipped",
                    m.mem.sleeps,
                    m.mem.slept_usec / 1000
                );
            }
            if !d.dma_transfers.is_empty() {
                let total: u64 = d.dma_transfers.iter().map(|(_, _, n)| *n as u64).sum();
                println!(
                    "\nata dma: {} transfers, {total} bytes to memory",
                    d.dma_transfers.len()
                );
                for (lba, dest, n) in d.dma_transfers.iter().take(64) {
                    println!("  lba {lba:<6} -> {dest:#010x}  {n} bytes");
                }
                // `dma_transfers` is uncapped, but this print is not. Say which is which.
                if d.dma_transfers.len() > 64 {
                    println!("  … and {} more (log is complete; this print shows 64)", d.dma_transfers.len() - 64);
                }
                if let Some((lba, l, n)) = d.dma_transfers.last() {
                    println!("  last: lba {lba} -> {l:#010x} + {n} = {:#010x}", l + n);
                }
            }
            // All of them, not the first 16. The interesting commands on this path are the LAST
            // ones — RetailOS's, after the bootloader has finished — and a head-of-log sample
            // shows only the bootloader's.
            // The count and the log are printed as two numbers because they are two instruments.
            // This line used to print `commands.len()` alone, which silently became a constant 256
            // once the log filled — and that constant was quoted in research/ as a measurement and
            // used as the project's baseline check. Print the census first, and say plainly when
            // the sample below it is truncated. `Capped::census` is now the shared wording, and
            // every other saturating instrument in this report was taught to speak it.
            println!("\nata commands: {}", d.commands.census());
            for (i, (c, f, n, lba)) in d.commands.iter().enumerate() {
                println!("  [{i:>3}] cmd {c:#04x}  features {f:#04x}  nsector {n:#04x}  lba {lba}");
            }
        }
        // Two numbers rather than a "modelled" line, because both are checkable: the gate opens and
        // closes should pair, and the SDRAM kicks should be the bring-up's two configurations.
        if let Some(x) = &m.mem.xmb {
            println!(
                "\nxmb: NOR write gate opened {} times, closed {}; SDRAM config kicked {} times",
                x.gate_opens, x.gate_closes, x.ram_kicks
            );
        }
        // Every number here is checkable against something. `commands` should equal the transmits
        // RetailOS's `0x00283fa0` starts; `data reads` should equal the loads at `0x00281364` plus
        // `0x00283f04`; `frames dropped` says whether an injected sequence outran the driver; and
        // `arm` / `DEV_EN` say whether the firmware ever turned the receiver on at all — which is
        // the one way a model that changed nothing is distinguishable from a device nobody enabled.
        if let Some(w) = &m.mem.clickwheel {
            use arm7tdmi::Bus as _;
            println!(
                "\nclickwheel: {} frames posted ({} dropped unread), {} word reads of DATA ({} with a frame waiting)",
                w.frames_posted, w.frames_dropped, w.data_reads, w.data_reads_ready
            );
            println!(
                "  {} transmits started, {} of them commands we have no evidence for{}",
                w.commands,
                w.unknown_commands,
                if w.unknown.is_empty() {
                    String::new()
                } else {
                    // The distinct set is capped at 16; `unknown_commands` beside it is not. Say so
                    // rather than letting a truncated set read as the whole vocabulary.
                    format!(
                        ": {}{}",
                        w.unknown.iter().map(|c| format!("{c:#010x}")).collect::<Vec<_>>().join(" "),
                        if w.unknown.truncated() {
                            format!(" (+{} further distinct words dropped — SAMPLE)", w.unknown.seen() - w.unknown.sample().len() as u64)
                        } else {
                            String::new()
                        }
                    )
                }
            );
            // `0x052a` is a setter, not a question — the reply to it is silence, and this is where
            // that shows up as something other than a suspicious zero. `suppressed` is the whole
            // A/B: a run whose script fires before the firmware's own enable reports frames it
            // refused to post, and a run whose script fires after it reports none.
            println!(
                "  reporting {} ({} `0x052a` set commands{}); {} autonomous frames suppressed while off",
                if w.reporting { "ON" } else { "OFF" },
                w.set_commands,
                match w.last_set {
                    Some((n, v)) => format!(", last payload {v} @{n}"),
                    None => ", never set".into(),
                },
                w.frames_suppressed
            );
            println!(
                "  irq {} asserted {} times; CTRL now {:#010x} (receiver {}), STATUS {:#010x}, last frame {:#010x}",
                eapp_loader::OPTO_IRQ_HI + 32,
                w.irqs,
                w.ctrl,
                if w.ctrl & 0x4000_0000 != 0 { "armed" } else { "NEVER ARMED" },
                w.status,
                w.rx
            );
            println!("  script: {} of {} steps fired", w.next, w.script.len());
            if !w.log.is_empty() {
                println!("  frames posted, in order: {}", w.log.census());
                for (n, f) in w.log.iter().take(24) {
                    let kind = if f & 0xbc00_00ff == 0x8000_001a {
                        format!(
                            "stream  pos {:>2}  buttons {:#04x}  {}",
                            (f >> 16) & 0x7f,
                            (f >> 8) & 0x1f,
                            if f & 0x4000_0000 != 0 { "touched" } else { "released" }
                        )
                    } else if f & 0x8000_ffff == 0x8000_023a {
                        format!("query   buttons {:#04x}", (f >> 16) & 0x1f)
                    } else {
                        "?".into()
                    };
                    println!("    @{n:<12} {f:#010x}  {kind}");
                }
                if let Some(l) = w.log.more_line(24) {
                    println!("  {l}");
                }
            }
            // Read out of memory rather than tracked, so this reports what the firmware left in the
            // registers rather than what this model thinks it saw.
            let dev_en = m.mem.read32(0x6000_600c);
            let init1 = m.mem.read32(0x7000_0010);
            println!(
                "  DEV_EN {dev_en:#010x} (DEV_OPTO {}), DEV_INIT1 {init1:#010x} (INIT_BUTTONS {})",
                if dev_en & 0x0001_0000 != 0 { "set" } else { "CLEAR" },
                if init1 & 0x0004_0000 != 0 { "set" } else { "CLEAR" }
            );
        }
        if let Some(pmu) = &m.mem.pmu {
            if !pmu.adc_log.is_empty() {
                // From the device's uncapped per-channel tally, not from the ordered log below it.
                // This histogram used to be built from a log that stops at 4 096 conversions.
                println!(
                    "\npcf50605 ADC conversions by channel ({} total):",
                    pmu.adc_log.seen()
                );
                for (ch, (n, v)) in &pmu.adc_by_channel {
                    println!("  channel {ch:#04x}  x{n:<6} last value {v:#06x}  ({} mV by Rockbox scale)",
                             (*v as u32 * 6000) >> 10);
                }
                println!(
                    "  order (first 12 of {} kept): {:?}",
                    pmu.adc_log.sample().len(),
                    pmu.adc_log.iter().take(12).collect::<Vec<_>>()
                );
            }
            println!(
                "\npcf50605: {} read transfers, {} write transfers; data registers now [{}]",
                pmu.reads,
                pmu.writes,
                (0..4).map(|i| format!("{:#04x}", pmu.data_byte(i))).collect::<Vec<_>>().join(" ")
            );
            let mut by_reg: Vec<_> = pmu.polled.iter().collect();
            by_reg.sort_by_key(|&(r, n)| (std::cmp::Reverse(*n), *r));
            // `polled` is a map and cannot saturate, so the tally IS complete — but this print was
            // taking 8 rows under a header that said "all of them", which is the same lie one level
            // out. Print all of them, since the header promises it.
            println!("  registers read, busiest first ({} distinct, all shown):", by_reg.len());
            for (r, n) in by_reg.iter() {
                println!("    reg {r:#04x}  x{n}");
            }
        }
        if !m.mem.watch_range_log.is_empty() {
            // Two failures produced one line here. The log capped at 4 096 — filled by Apple's
            // bootloader before RetailOS executes an instruction — and the per-word table was built
            // from it and showed only the FIRST writing PC, so a span both of them write reported
            // the bootloader as its sole author. That published "RetailOS never touches the
            // VideoCore", which was wrong and sent the strategy after a co-processor that was not
            // in the way. The table below now comes from `watch_range_words`, which is uncapped and
            // keeps every writer.
            println!(
                "\nwrites into the watched range: {} byte-writes across {} words (uncapped census)",
                m.mem.watch_range_words.values().map(|w| w.writes).sum::<u64>(),
                m.mem.watch_range_words.len()
            );
            println!("  ordered sample: {}", m.mem.watch_range_log.census());
            for (a, w) in &m.mem.watch_range_words {
                let mut pcs: Vec<_> = w.pcs.iter().collect();
                pcs.sort_by_key(|(_, n)| std::cmp::Reverse(**n));
                // Eight, not four. The question this instrument is asked is "who ELSE wrote here",
                // and the answer is routinely the writer with the smallest count — RetailOS's ten
                // stores next to the bootloader's eighty thousand.
                let shown: Vec<String> =
                    pcs.iter().take(8).map(|(pc, n)| format!("{pc:#010x} x{n}")).collect();
                println!(
                    "  {a:#010x}  {:>7} byte-writes from {} pc{}, first @{}: {}{}",
                    w.writes,
                    w.pcs.len(),
                    if w.pcs.len() == 1 { "" } else { "s" },
                    w.first_at,
                    shown.join("  "),
                    if w.pcs.len() > 8 { format!("  … and {} more", w.pcs.len() - 8) } else { String::new() }
                );
            }
        }
        if let Some(edges) = &m.edges {
            println!("\nruntime call graph: {} distinct edges taken", edges.len());
            // --callgraph-dump=FILE : every edge, for offline analysis. The set of branch TARGETS
            // is a set of real entry points, which is strictly better evidence about function
            // boundaries than the "nearest preceding push-lr" heuristic — that one demonstrably
            // split 0x002100f4/0x002102bc into two.
            if let Some(path) = args.iter().find_map(|a| a.strip_prefix("--callgraph-dump=")) {
                let mut out = String::new();
                for ((site, tgt), n) in edges {
                    out.push_str(&format!("{site:08x} {tgt:08x} {n}\n"));
                }
                match std::fs::write(path, out) {
                    Ok(()) => println!("  edges -> {path}"),
                    Err(e) => println!("  {path}: {e}"),
                }
            }
            let targets: Vec<u32> = args.iter()
                .filter_map(|a| a.strip_prefix("--callgraph="))
                .filter_map(|s| u32::from_str_radix(s.trim_start_matches("0x"), 16).ok())
                .collect();
            for t in &targets {
                // Callers of the function containing `t`, not just of `t` exactly: a virtual call
                // lands on the entry point, while the address of interest is usually inside.
                let mut hits: Vec<_> = edges.iter()
                    .filter(|((_, tgt), _)| *tgt <= *t && t.wrapping_sub(*tgt) < 0x400)
                    .collect();
                hits.sort_by_key(|(_, n)| std::cmp::Reverse(**n));
                println!("  runtime callers of {t:#010x} ({} edges):", hits.len());
                for ((site, tgt), n) in hits.iter().take(12) {
                    let name = m.symbolise(*tgt).unwrap_or_default();
                    println!("    {site:#010x} -> {tgt:#010x}  x{n}  {name}");
                }
            }
        }
        if !m.mem.input_regs.is_empty() {
            let mut rows: Vec<_> = m.mem.input_regs.iter().filter(|(_, v)| v.0 > 0).collect();
            rows.sort_by_key(|(_, v)| std::cmp::Reverse(v.0));
            println!("\nregisters READ BEFORE EVER BEING WRITTEN ({} of {} touched):",
                     rows.len(), m.mem.input_regs.len());
            println!("  these are values the firmware expects hardware to supply, and we invent");
            for (a, (r, w, pc)) in rows.iter() {
                println!("  {a:#010x}  {r:>10} reads before write, {w:>8} writes after, first pc {pc:#010x}");
            }
        }
        if m.mem.verify_memory {
            let v = &m.mem.verify_mismatches;
            if v.is_empty() {
                println!("\nverify-memory: no fast/slow disagreements");
            } else {
                println!(
                    "\nverify-memory: {} DISAGREEMENTS (fast path answered a different region)",
                    v.census()
                );
                for (pc, a, f, sl) in v.iter().take(12) {
                    println!("  pc {pc:#010x}  addr {a:#010x}  fast={f} slow={sl}");
                }
                if let Some(l) = v.more_line(12) {
                    println!("{l}");
                }
            }
        }
        if !m.mem.write_log_entries.is_empty() {
            let e = &m.mem.write_log_entries;
            // "How many stores were DROPPED" is the question this instrument exists for, so it comes
            // from the uncapped per-region tally rather than from the 8 192-entry log.
            let dropped = m.mem.write_log_regions.get("DROPPED").copied().unwrap_or(0);
            println!("\nwrite log: {} stores, {dropped} DROPPED (uncapped census)", e.seen());
            for (r, n) in &m.mem.write_log_regions { println!("  -> {r:<12} {n}"); }
            println!("  ordered sample: {}", e.census());
            println!("  first 4 and last 4 OF THE SAMPLE (pc, addr, value, region):");
            for x in e.iter().take(4).chain(e.iter().rev().take(4).collect::<Vec<_>>().into_iter().rev()) {
                println!("    pc {:#010x}  {:#010x} = {:#010x}  {}", x.0, x.1, x.2, x.3);
            }
            // The "last 4" is the last 4 KEPT, which on a truncated log is not the last 4 that
            // happened. That distinction is exactly what made a head-of-log ATA sample misleading.
            if e.truncated() {
                println!("    ^ the last rows are the last KEPT, not the last that happened");
            }
        }
        if !m.mem.i2c_log.is_empty() {
            use std::collections::BTreeMap;
            // Every table here comes from `i2c_tally`, which is uncapped, rather than from
            // `i2c_log`, which stops at 4 096 — and the standard baseline fills it exactly. Under
            // the old code this whole section was a picture of the first 4 096 transfers wearing a
            // total's label: `dev 0x34  52 transfers` (the WM8758) was a floor, and NEXT.md §5 was
            // proposing to fit a codec model to it.
            let total: u64 = m.mem.i2c_tally.values().sum();

            // CTRL decoded, because the length field is what says how many data registers a
            // transfer actually latches -- and that is the difference between a device model and a
            // bus that answers every byte the same way.
            let mut ctrls: BTreeMap<u8, u64> = BTreeMap::new();
            for ((_, c, _), n) in &m.mem.i2c_tally {
                *ctrls.entry(*c).or_default() += n;
            }
            println!("\ni2c CTRL values seen (all {total} transfers):");
            for (c, n) in &ctrls {
                println!(
                    "  ctrl {c:#04x}  {}  len {}  x{n}",
                    if c & 0x20 != 0 { "read " } else { "write" },
                    ((c >> 1) & 3) + 1
                );
            }

            let mut per_dev: BTreeMap<u8, u64> = BTreeMap::new();
            for ((d, _, _), n) in &m.mem.i2c_tally {
                *per_dev.entry(*d).or_insert(0) += n;
            }
            println!("\ni2c: {total} transfers, by device address (uncapped census):");
            for (d, n) in per_dev.iter() {
                println!("  dev {d:#04x}  {n} transfers");
            }
            let mut per_reg: BTreeMap<(u8, u8), u64> = BTreeMap::new();
            for ((d, _, r), n) in &m.mem.i2c_tally {
                *per_reg.entry((*d, *r)).or_insert(0) += n;
            }
            let mut rows: Vec<_> = per_reg.into_iter().collect();
            rows.sort_by_key(|(_, n)| std::cmp::Reverse(*n));
            println!("  hottest (device, register) of {} pairs:", rows.len());
            for ((d, r), n) in rows.iter().take(12) {
                println!("    dev {d:#04x} reg {r:#04x}  {n}");
            }
            if rows.len() > 12 {
                println!("    … and {} more pairs", rows.len() - 12);
            }
            // The ordered log is still here, and it is still a sample. It answers "in what order",
            // which the tally cannot; it must never again answer "how many".
            println!("  ordered sample: {}", m.mem.i2c_log.census());
        }
        if !m.mem.page_counts.is_empty() {
            let mut rows: Vec<_> = m.mem.page_counts.iter().collect();
            rows.sort_by_key(|(_, (r, w))| std::cmp::Reverse(r + w));
            // All of them. A top-10 is fine for "what is busy" and useless for comparing the
            // register SETS two operating systems touch, which is what names the ones only one of
            // them exercises — and therefore the ones no working OS has ever validated.
            println!("\nregister blocks touched: ({} distinct)", rows.len());
            for (a, (r, w)) in rows.iter() {
                println!("  {a:#010x}  {r:>12} reads {w:>12} writes");
            }
        }
        if !m.call_log.is_empty() {
            // Oldest-first ordering out of the ring.
            let n = m.call_log.len();
            let start = if m.call_at > n { m.call_at % n } else { 0 };
            let ordered: Vec<_> = (0..n).map(|i| m.call_log[(start + i) % n]).collect();
            println!("\ncall history: {} BLs total, last {n} kept", m.call_at);
            println!("  last 20 (site -> target):");
            for (from, to) in ordered.iter().rev().take(20).rev() {
                println!("    {from:#010x} -> {to:#010x}");
            }
        }
        if !m.print_sites.is_empty() {
            println!("\nconsole writes (caller -> string): {}", m.print_sites.census());
            for (lr, p) in m.print_sites.iter() {
                let mut txt = String::new();
                let mut a = *p;
                for _ in 0..48 {
                    let c = m.mem.read8(a);
                    if c == 0 { break }
                    txt.push(if (0x20..0x7f).contains(&c) { c as char } else { '.' });
                    a += 1;
                }
                println!("  from {lr:#010x}  str {p:#010x}  {txt:?}");
            }
        }
        report_bcm_dump(&args, &m);
        report_bcm_peek(&args, &m);
        report_findptr(&args, &m);
        report_dumps(&args, &mut m);
    report_profile(&m);
        report_unmapped(&mut m);
        // This path returns from main, so the shared reporting at the bottom never runs. Without
        // this call --break, --watch and --dump are accepted, do fire, and print nothing — which
        // reads as "breakpoints do not work on the boot path" and cost a long detour to diagnose.
        report_break_watch(&mut m);
        return;
    }

    if args.iter().any(|a| a == "--run-loader") {
        use arm7tdmi::Bus as _;
        // RetailOS's globals live above OSOS and are populated during boot, which we skip.
        // Map that space so the loader's registry head is at least writable.
        m.mem.regions.push(eapp_loader::Region {
            name: "osos-bss",
            base: 0x1073_6000,
            data: vec![0; 0x0090_0000],
        });
        // The framework registry head. Records are the export table itself, chained through the
        // word at +0x28 — which is what the loader walks (`ldrne r4, [r4, #0x28]`).
        const REGISTRY_HEAD: u32 = 0x1081_ec8c;
        if let Some(v) = args.iter().find_map(|a| a.strip_prefix("--registry=")) {
            let addr = u32::from_str_radix(v.trim_start_matches("0x"), 16).unwrap_or(0);
            m.mem.write32(REGISTRY_HEAD, addr);
            println!("registry head {REGISTRY_HEAD:#x} <- {addr:#010x}");
        }
        // 0x001222c4 mapped at 0x10000000. This read 0x1012_24C4 until 2026-08-19, which is the
        // `bne` failure exit *inside* the validator, not its entry: the call executed four
        // instructions (`mvn r0,#1; b …; add sp…; ldmia`), returned -2, resolved 0 of 277 thunks,
        // and reported it as a result.
        const RETAILOS_EAPP_LOADER: u32 = 0x1012_22C4;
        let before = m.thunk_targets(&app);
        println!("\nrunning RetailOS's eApp loader at {RETAILOS_EAPP_LOADER:#010x} with r0 = image base");
        let ctx1 = m.scratch(0x1000);
        let ctx2 = m.scratch(0x1000);
        let r2 = args
            .iter()
            .find_map(|a| a.strip_prefix("--loader-r2="))
            .and_then(|v| u32::from_str_radix(v.trim_start_matches("0x"), 16).ok())
            .unwrap_or(ctx2);
        println!("  args: r0={:#x} r1={ctx1:#x} r2={r2:#x}", app.load_base);
        let before_steps = m.trace.len();
        let stop = m.call_with(RETAILOS_EAPP_LOADER, &[app.load_base, ctx1, r2, 0], 20_000_000);
        println!(
            "  -> {stop:?}   r0 = {:#010x}   framework calls during load: {}",
            m.cpu.regs[0],
            m.trace.len() - before_steps
        );
        println!("  instructions executed: {}", m.executed);
        let after = m.thunk_targets(&app);
        let changed: Vec<_> = before
            .iter()
            .zip(after.iter())
            .filter(|(b, a)| b.2 != a.2)
            .collect();
        println!("  thunks patched: {} of {}", changed.len(), after.len());
        for (_, a) in changed.iter().take(12) {
            println!("    {:<14} #{:<3} -> {:#010x}", a.0, a.1, a.2);
        }
        println!("  last instructions executed:");
        for addr in m.recent().iter().rev().take(14).rev() {
            let w = m.mem.read32(*addr);
            println!("    {addr:08x}  {}", disasm::arm(w, *addr, None));
        }
        report_unmapped(&mut m);
        return;
    }

    if let Some(v) = args.iter().find_map(|a| a.strip_prefix("--tex-base=")) {
        m.tex_base = v.parse().unwrap_or(1);
    }
    if args.iter().any(|a| a == "--preload-tga") {
        for l in m.preload_textures() {
            println!("  {l}");
        }
    }

    // Synthetic context arguments. RetailOS passes real structures here; zeroed scratch at
    // least makes the pointers dereferenceable instead of null.
    // The real shape, from `0x0024da80`: `mov r0, r4` / `add r1, r4, #0x100`, so the two
    // arguments are one object and a pointer 0x100 into it. `[ctx+0x00]` is a state byte the
    // pump sets to 5 (or 4) immediately before the call.
    let ctx: Vec<u32> = if args.iter().any(|a| a == "--ctx") {
        let a = m.scratch(0x400);
        m.mem.poke8(a, 5);
        println!("context args: r0={a:#x} r1={:#x} (r0+0x100)", a + 0x100);
        vec![a, a + 0x100, 0, 0]
    } else {
        vec![]
    };

    let mut stop = Stop::Returned;
    let mut last_vector = None;
    for (i, &v) in app.vectors.iter().enumerate() {
        if v == 0 {
            continue;
        }
        let before = m.trace.len();
        stop = m.call_with(v, &ctx, budget);
        println!(
            "vector[{i}] {v:#010x} -> {:?}  (+{} calls)",
            stop,
            m.trace.len() - before
        );
        last_vector = Some(v);
    }

    // A title that returns has not stalled — it finished a unit of work and expects to be
    // called again. RetailOS drives the last vector once per frame; doing the same here is
    // what turns "init completed" into "game running".
    let frames: usize = args
        .iter()
        .filter_map(|a| a.strip_prefix("--frames="))
        .filter_map(|s| s.parse().ok())
        .next()
        .unwrap_or(0);
    // --poke-at=FRAME:ADDR=VALUE — write one byte just before the given frame runs.
    //
    // The async file API these titles use is callback-driven: the game hands RetailOS a request
    // object and RetailOS calls back on completion, moving the object's state byte on. Nothing
    // here models that callback, so a request issued in frame N is still pending in frame N+1
    // forever. This delivers the completion by hand, which is how the state machine gets read
    // out at all — an instrument for identifying the transitions, not a fix for the missing
    // callback, and it writes only what the caller names.
    let poke_at: Vec<(usize, u32, u8)> = args
        .iter()
        .filter_map(|a| a.strip_prefix("--poke-at="))
        .filter_map(|spec| {
            let (fr, rest) = spec.split_once(':')?;
            let (addr, val) = rest.split_once('=')?;
            Some((fr.parse().ok()?, parse_addr(addr)?, parse_addr(val)? as u8))
        })
        .collect();
    for (f, a, v) in &poke_at {
        println!("poke-at: frame {f} -> [{a:#010x}] = {v:#04x}");
    }

    // --call-at=FRAME:ADDR:A0[,A1…] — call a guest function between two frames.
    //
    // The completion side of the async file API is a callback the game registers and RetailOS
    // invokes; the registration is real code in the image, so the honest way to deliver a
    // completion is to call what the game actually asked for rather than to forge the state it
    // would have written. Minigolf's open registers `0x18017f00` at request+0x34, and that
    // function reads its status from request+0x20, hands the handle to the file object, frees
    // the request and falls through to `0x18017f34`, which is what moves the state byte to 2.
    let call_at: Vec<(usize, u32, Vec<u32>)> = args
        .iter()
        .filter_map(|a| a.strip_prefix("--call-at="))
        .filter_map(|spec| {
            let mut p = spec.splitn(3, ':');
            let fr = p.next()?.parse().ok()?;
            let addr = parse_addr(p.next()?)?;
            let a: Vec<u32> = p
                .next()
                .map(|s| s.split(',').filter_map(parse_addr).collect())
                .unwrap_or_default();
            Some((fr, addr, a))
        })
        .collect();
    for (f, a, v) in &call_at {
        println!("call-at: frame {f} -> {a:#010x}({})", v.iter().map(|x| format!("{x:#x}")).collect::<Vec<_>>().join(", "));
    }

    if let (Some(v), true) = (last_vector, frames > 0) {
        let mut prev = m.trace.len();
        for f in 0..frames {
            for (pf, addr, val) in &poke_at {
                if *pf == f {
                    m.mem.poke8(*addr, *val);
                    println!("  frame {f:<4} poked [{addr:#010x}] = {val:#04x}");
                }
            }
            // Deliver any completion the host owes the game before the next frame runs. The
            // callback is the game's own code, at the address it parked in the request.
            let due: Vec<u32> = m.pending_completions.drain(..).collect();
            for req in due {
                let cb = m.mem.read32(req + eapp_loader::REQ_CALLBACK);
                // Two arguments, not one. The read completion at 0x18017574 asserts
                // `arg0 == arg1 + 0x128` and spins on `b .` at 0x180175d0 when it does not hold,
                // so a one-argument call hangs in the game's own code rather than erroring.
                let ctx_arg = m.mem.read32(req + eapp_loader::REQ_CONTEXT);
                if cb != 0 {
                    let s = m.call_with(cb, &[req, ctx_arg], budget);
                    if !matches!(s, Stop::Returned) {
                        println!("  frame {f:<4} completion {cb:#010x}({req:#010x}) -> {s:?}");
                    }
                }
            }
            for (cf, addr, cargs) in &call_at {
                if *cf == f {
                    let s = m.call_with(*addr, cargs, budget);
                    println!("  frame {f:<4} called {addr:#010x} -> {s:?}");
                }
            }
            stop = m.call_with(v, &ctx, budget);
            let now = m.trace.len();
            // Only report frames that differ, so a steady state is visible at a glance.
            if f < 3 || now - prev != 0 {
                println!("  frame {f:<4} -> {stop:?}  (+{} calls)", now - prev);
            }
            prev = now;
            if !matches!(stop, Stop::Returned) {
                break;
            }
        }
    }

    println!("\nstopped: {stop:?}");
    println!("calls made: {}", m.trace.len());
    println!("heap used:  {} bytes", m.heap_used());
    println!("instructions executed: {}", m.executed);
    println!("input polls: {}  queue remaining: {}", m.polls, m.input_queue.len());
    println!("frames presented: {}  clears: {}  quads drawn: {}", m.frames_presented, m.clears, m.quads_drawn);
    if let Some(path) = args.iter().find_map(|a| a.strip_prefix("--ppm=")) {
        match fs::write(path, m.framebuffer_ppm()) {
            Ok(()) => println!("wrote {path} ({}x{})", eapp_loader::FB_WIDTH, eapp_loader::FB_HEIGHT),
            Err(e) => eprintln!("{path}: {e}"),
        }
    }
    if !m.output.is_empty() {
        println!("\n--- the game's own debug output ---\n{}", m.output.trim_end());
        println!("--- end ---");
    }

    if m.log_indirect {
        // From `indirect_edges`, which is uncapped. Deriving the edge set from `indirect_log` meant
        // reporting the distinct edges among the first 4 096 branches as though they were the set.
        let mut v: Vec<_> = m.indirect_edges.iter().collect();
        v.sort_by_key(|(_, n)| std::cmp::Reverse(**n));
        println!(
            "\n--- indirect branches: {} distinct edges over {} branches ---",
            v.len(),
            m.indirect_log.seen()
        );
        for ((from, to), n) in v.iter().take(20) {
            println!("  {from:#010x} -> {to:#010x}   x{n}");
        }
        if v.len() > 20 {
            println!("  … and {} more edges", v.len() - 20);
        }
    }

    report_break_watch(&mut m);


    // --dump=ADDR:N — hexdump guest memory after the run. The async request structs are built by
    // the game and read by the host, so their layout is only visible in a live object.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--dump=")) {
        if let Some((a, n)) = spec.split_once(':') {
            if let (Some(base), Some(len)) = (parse_addr(a), parse_addr(n)) {
                println!("\n--- memory {base:#010x}..{:#010x} ---", base + len);
                for row in (0..len).step_by(16) {
                    let addr = base + row;
                    let words: Vec<String> =
                        (0..4).map(|k| format!("{:08x}", m.mem.read32(addr + k * 4))).collect();
                    println!("  +{:#05x} {addr:#010x}  {}", row, words.join(" "));
                }
            }
        }
    }

        // --calls=NAME : every call to one framework, uncapped. The generic call trace shows the
    // first 400, and a subsystem that only wakes up later — audio, save — never appears in it.
    for want in args.iter().filter_map(|a| a.strip_prefix("--calls=")) {
        let sel: Vec<&eapp_loader::Call> =
            m.trace.iter().filter(|c| c.framework == *want).collect();
        println!("\n--- {want} calls: {} ---", sel.len());
        for c in sel.iter().take(60) {
            println!(
                "  #{:<4} r:{:08x} {:08x} {:08x} {:08x}  sp:{:08x} {:08x} {:08x} {:08x}  <-{:#010x}",
                c.index, c.args[0], c.args[1], c.args[2], c.args[3],
                c.stack[0], c.stack[1], c.stack[2], c.stack[3], c.return_to
            );
        }
    }

    report_profile(&m);

    if !m.file_log.is_empty() {
        println!("\n--- file activity: {} ---", m.file_log.census());
        for l in m.file_log.iter().take(60) {
            println!("  {l}");
        }
        if let Some(l) = m.file_log.more_line(60) {
            println!("{l}");
        }
    }

    if !m.tex_log.is_empty() {
        println!("\n--- texture / draw diagnostics: {} ---", m.tex_log.census());
        // Uploads in full, separately: they are what a draw's `tex#N` refers to, and truncating
        // the middle of a mixed log hides every one of them behind the draws.
        for l in m.tex_log.iter().filter(|l| !l.starts_with("n=")) {
            println!("  {l}");
        }
        // The interesting draws are the late ones — the early frames are the title card, and a
        // first-N sample can only ever show that.
        let kept: Vec<&String> = m.tex_log.iter().collect();
        if kept.len() > 6 {
            println!("  ... last 22 of {} kept:", kept.len());
            for l in kept.iter().rev().take(22).rev() {
                println!("  {l}");
            }
        }
    }

    let reached = m.reached();
    let mut names: Vec<&&str> = reached.keys().collect();
    names.sort();
    println!("\ndistinct imports reached:");
    for n in names {
        let used = &reached[*n];
        let total = app
            .frameworks
            .iter()
            .find(|f| f.name == **n)
            .map_or(0, |f| f.thunks.len());
        println!("  {:<14} {:>3} of {:>3}   {:?}", n, used.len(), total, used);
    }

    // `m.trace` is uncapped, so this is a display truncation rather than a saturated instrument —
    // but "full call trace:" over a 400-row head is the same sentence a cap would produce, and this
    // report is full of readers who have been burned by exactly that.
    println!(
        "\ncall trace: {} calls, showing the first {}:",
        m.trace.len(),
        m.trace.len().min(400)
    );
    for c in m.trace.iter().take(400) {
        println!(
            "  {:<14} #{:<3} r:{:08x} {:08x} {:08x} {:08x}  sp:{:08x} {:08x} {:08x} {:08x}  from {:08x}",
            c.framework, c.index,
            c.args[0], c.args[1], c.args[2], c.args[3],
            c.stack[0], c.stack[1], c.stack[2], c.stack[3], c.return_to
        );
    }

    let depth: usize = args
        .iter()
        .filter_map(|a| a.strip_prefix("--history="))
        .filter_map(|s| s.parse().ok())
        .next()
        .unwrap_or(0);
    if depth > 0 || matches!(stop, Stop::Lost(_)) {
        let n = if depth > 0 { depth } else { 12 };
        println!("\nlast {n} instructions executed:");
        let recent = m.recent();
        for a in recent.iter().rev().take(n).rev() {
            let instr = m.mem.read32(*a);
            println!("  {a:08x}  {}", disasm::arm(instr, *a, None));
        }
    }

    report_dumps(&args, &mut m);

    report_unmapped(&mut m);
}

/// `--dump=ADDR:LEN` — inspect live memory.
///
/// Vertex buffers are built at runtime in BSS, so they do not exist in the file and can only be
/// seen from inside a running machine. Same for hardware registers the firmware programs itself:
/// reading them back is how we learn what it decided, instead of guessing.
fn report_dumps(args: &[String], m: &mut eapp_loader::Machine) {
    // --disasm=ADDR:COUNT — read code out of the running machine. The alternative is dumping hex
    // and decoding ARM by eye, which is where guesses come from.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--disasm=")) {
        let Some((a, n)) = spec.split_once(':') else { continue };
        let parse = |t: &str| {
            t.strip_prefix("0x")
                .and_then(|h| u32::from_str_radix(h, 16).ok())
                .or_else(|| t.parse().ok())
        };
        let (Some(addr), Some(count)) = (parse(a), parse(n)) else { continue };
        println!("\ndisassembly at {addr:#010x}:");
        for i in 0..count {
            let at = addr + i * 4;
            let w = u32::from_le_bytes([
                m.mem.read8(at),
                m.mem.read8(at + 1),
                m.mem.read8(at + 2),
                m.mem.read8(at + 3),
            ]);
            println!("  {at:08x}  {w:08x}  {}", disasm::arm(w, at, None));
        }
    }

    // --callers=ADDR — every branch in memory that targets ADDR.
    //
    // "Who calls this?" was being answered with runtime breakpoints, which only ever shows the
    // paths a particular run happened to take. A static scan shows all of them, including the ones
    // that did not execute, and costs nothing.
    //
    // Both `BL` and plain `B` are counted, because tail calls are branches: `0x4000b534` is reached
    // only by `b` from `0x4000318c`, and a BL-only scan reports it as having no callers at all.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--callers=")) {
        let Some(target) = u32::from_str_radix(spec.trim_start_matches("0x"), 16).ok() else {
            continue;
        };
        println!("\nbranches to {target:#010x}:");
        let mut found = 0usize;
        for r in &m.mem.regions {
            for off in (0..r.data.len().saturating_sub(4)).step_by(4) {
                let w = u32::from_le_bytes([
                    r.data[off],
                    r.data[off + 1],
                    r.data[off + 2],
                    r.data[off + 3],
                ]);
                // Branch family is bits 27..25 == 0b101; bit 24 selects BL over B. Condition 0xf
                // is not a condition — it is the unconditional-instruction space.
                if (w >> 25) & 0x7 != 0b101 || w >> 28 == 0xf {
                    continue;
                }
                let imm = ((w & 0x00ff_ffff) << 8) as i32 >> 6; // sign-extend, then <<2
                let pc = r.base + off as u32;
                if pc.wrapping_add(8).wrapping_add(imm as u32) == target {
                    found += 1;
                    if found <= 24 {
                        let kind = if w & (1 << 24) != 0 { "bl" } else { "b " };
                        println!("  {:<12} {pc:#010x}  {kind}", r.name);
                    }
                }
            }
        }
        if found == 0 {
            println!("  none");
        } else if found > 24 {
            println!("  … and {} more", found - 24);
        }
    }

    // --save-region=NAME:FILE — write a region out as it stands in the running machine.
    //
    // Reading forty instructions used to cost a full cold boot, because the only way to see the
    // bootloader's code was to run it: it lives in IRAM, scatter-loaded out of NOR, so the flash
    // file on disk is not what executes. Dumping it once makes it a file — instantly
    // disassemblable, greppable, and openable by any external tool, with no run at all.
    for spec in args.iter().filter_map(|a| a.strip_prefix("--save-region=")) {
        let Some((name, path)) = spec.split_once(':') else { continue };
        match m.mem.regions.iter().find(|r| r.name == name) {
            Some(r) => match std::fs::write(path, &r.data) {
                Ok(()) => println!(
                    "saved region {name} ({} bytes, base {:#010x}) -> {path}",
                    r.data.len(),
                    r.base
                ),
                Err(e) => eprintln!("{path}: {e}"),
            },
            None => {
                let have: Vec<&str> = m.mem.regions.iter().map(|r| r.name).collect();
                eprintln!("no region {name:?}; have {have:?}");
            }
        }
    }

    for spec in args.iter().filter_map(|a| a.strip_prefix("--dump=")) {
        let Some((a, l)) = spec.split_once(':') else { continue };
        let parse = |t: &str| {
            t.strip_prefix("0x")
                .and_then(|h| u32::from_str_radix(h, 16).ok())
                .or_else(|| t.parse().ok())
        };
        let (Some(addr), Some(len)) = (parse(a), parse(l)) else { continue };
        println!("\nmemory at {addr:#010x}:");
        for row in (0..len).step_by(16) {
            let base = addr + row;
            let bytes: Vec<u8> = (0..16).map(|i| m.mem.read8(base + i)).collect();
            let hex: Vec<String> = bytes.iter().map(|b| format!("{b:02x}")).collect();
            let ascii: String = bytes
                .iter()
                .map(|&c| if (0x20..0x7f).contains(&c) { c as char } else { '.' })
                .collect();
            println!("  {base:08x}  {}  |{ascii}|", hex.join(" "));
        }
    }
}

/// Print the sampled PC histogram, hottest first.
fn report_profile(m: &eapp_loader::Machine) {
    let Some(p) = &m.profile else { return };
    let total: u64 = p.values().sum();
    if total == 0 {
        return;
    }
    let mut rows: Vec<_> = p.iter().collect();
    rows.sort_by_key(|(_, n)| std::cmp::Reverse(**n));
    // 15 rows covered 79% of a whole-run profile but only 41% of a windowed one — a tail that long
    // is a finding, not noise, so let the caller ask for it.
    let want = std::env::args()
        .find_map(|a| a.strip_prefix("--profile=").and_then(|n| n.parse::<usize>().ok()))
        .unwrap_or(15);
    println!("\nprofile: {total} samples over {} buckets", p.len());
    for (addr, n) in rows.iter().take(want) {
        let name = m.symbolise(**addr).unwrap_or_default();
        println!("  {addr:#010x}  {:>5.1}%  {n:<9} {name}", **n as f64 * 100.0 / total as f64);
    }
}

/// Install the `sysinfo_t` block the Apple flash bootloader leaves in IRAM for RetailOS.
///
/// The last 256 bytes of IRAM are the documented handoff area — `0x4001ff00..0x40020000` on the
/// 128 KB PP5022-class part that the 5G is. Two words in it matter here:
///
/// ```text
/// 0x4001ff18  "IsyS"                    SYSINFO_TAG
/// 0x4001ff1c  -> struct sysinfo_t       SYSINFO_PTR
/// ```
///
/// Both names and both addresses are from iPodLinux — `ipodloader2/ipodhw.c` (`SYSINFO_TAG_PP5022`,
/// `SYSINFO_PTR_PP5022`) and the kernel's `arch-ipod/hardware.h`, which also gives the field order
/// reproduced below. RetailOS reads the pointer at `0x13e0`; without it the pointer is null, the
/// SDRAM bank sizes all resolve to zero, and the boot asserts at `0xda0`.
///
/// `sdram_size` is what the firmware is ultimately after. It is written into every field that could
/// plausibly carry it — including `+0xe0`, which the firmware demonstrably reads and which falls
/// inside the region iPodLinux calls `pad7[120]`, i.e. bytes they never identified. That one is a
/// deliberate guess, and the run either clears the assertion or it does not.
fn install_sysinfo(m: &mut eapp_loader::Machine, base: u32, sdram_size: u32) {
    // Four-character tags, as they appear in memory: bytes 'I','s','y','S' little-endian.
    let tag = |s: &[u8; 4]| u32::from_le_bytes(*s);
    let w = |m: &mut eapp_loader::Machine, off: u32, v: u32| m.mem.write32(base + off, v);

    w(m, 0x00, tag(b"IsyS"));
    w(m, 0x04, 0x184); // len, per clicky's HLE bootloader
    // 0x08 BoardHwName[16], 0x18 pszSerialNumber[32], 0x38 pu8FirewireGuid[16] — left zeroed.
    w(m, 0x48, 0x0000_0005); // boardHwRev
    w(m, 0x60, tag(b"Flsh"));
    w(m, 0x68, 0x2000_0000); // flash_base — where Rockbox's mmap window puts the 1 MB NOR
    w(m, 0x6c, 0x0010_0000); // flash_size
    w(m, 0x74, tag(b"Sdrm"));
    w(m, 0x7c, 0x1000_0000); // sdram_base
    w(m, 0x80, sdram_size);
    w(m, 0x88, tag(b"Frwr"));
    w(m, 0x9c, tag(b"Iram"));
    w(m, 0xa4, 0x4000_0000); // iram_base
    w(m, 0xa8, 0x0002_0000); // iram_size — 128 KB, the PP5022-class part
    // The hardware/Gestalt ID. RetailOS copies the first 248 bytes of this block verbatim into a
    // device singleton (`0x281224`) and then reads `+0x84` as the model — measured, by breaking at
    // `0x281238` and finding `r1` pointing straight at this structure.
    //
    // The value is **this machine's own**, not the published one: it is the `HwVr` record of the
    // `SCfg` block at flash `0x4054`, which Apple's bootloader fetches by key lookup at `0x400098dc`
    // and stores here. Cold boot reads `0x000b0011` straight out of the NOR dump.
    //
    // It used to be `0x000B0005` — theapplewiki's published 5G Gestalt, borrowed from the USB work
    // — and the warm path never noticed, because the selector at `0x2653a4` switches on the HIGH
    // half and both values carry 11. Correcting it changes exactly one thing that is visible:
    // RetailOS's first MBR read becomes `nsector 4` instead of `nsector 1` — 2048 bytes instead of
    // 512, which is the drive-configuration step asking for more of the disk than it used to.
    // Everything else holds: 104 arrivals at the selector from the same two call sites, 18 ATA
    // commands, 220 unmapped reads and no unmapped writes, 17 972 code buckets against 17 968.
    w(m, 0x84, 0x000b_0011);
    w(m, 0xe0, sdram_size); // read by RetailOS at 0x13e8; inside iPodLinux's pad7
    w(m, 0x128, 0x0005_0014); // boardHwSwInterfaceRev, per clicky

    m.mem.write32(0x4001_ff18, tag(b"IsyS"));
    m.mem.write32(0x4001_ff1c, base);
    println!("  sysinfo at {base:#010x}, sdram_size {sdram_size:#x}");
}

/// `--findptr=VALUE[/MASK]` — every aligned word in memory matching VALUE under MASK.
///
/// RetailOS is a debug build carrying its own task names as strings (`DiskReaderTask`,
/// `USBDeviceTask`, `TimerTaskClass`, …). A task control block that has a name *points* at that
/// string, so scanning for the string's address finds the TCBs — which is far cheaper than
/// recovering an unknown RTOS's structures by disassembly.
///
/// The mask is what makes it work on *code*. Searching for a literal `mov r0, #0x58` misses every
/// conditional form, because ARM puts the condition in the top nibble — and a bootloader's error
/// paths are overwhelmingly `moveq` / `movne`. `0xe3a00058/0x0fffffff` finds all sixteen at once.
/// Matches are disassembled, since a hit that is code is unreadable as a bare hex word.
fn report_findptr(args: &[String], m: &eapp_loader::Machine) {
    for spec in args.iter().filter_map(|a| a.strip_prefix("--findptr=")) {
        let hex = |t: &str| u32::from_str_radix(t.trim().trim_start_matches("0x"), 16).ok();
        let (val, mask) = match spec.split_once('/') {
            Some((v, k)) => (hex(v), hex(k).unwrap_or(u32::MAX)),
            None => (hex(spec), u32::MAX),
        };
        let Some(target) = val else { continue };
        if mask == u32::MAX {
            println!("\npointers to {target:#010x}:");
        } else {
            println!("\nwords matching {target:#010x} under mask {mask:#010x}:");
        }
        let mut found = 0usize;
        for r in &m.mem.regions {
            // Regions overlap (the image is mirrored), so report the first few per region rather
            // than every alias of the same word.
            let mut per_region = 0;
            for off in (0..r.data.len().saturating_sub(4)).step_by(4) {
                let w = u32::from_le_bytes([
                    r.data[off],
                    r.data[off + 1],
                    r.data[off + 2],
                    r.data[off + 3],
                ]);
                if w & mask == target & mask {
                    found += 1;
                    per_region += 1;
                    if per_region <= 16 {
                        let at = r.base + off as u32;
                        println!("  {:<12} {at:#010x}  {}", r.name, disasm::arm(w, at, None));
                    }
                }
            }
            if per_region > 16 {
                println!("  {:<12} … and {} more", r.name, per_region - 16);
            }
        }
        if found == 0 {
            println!("  none");
        }
    }
}

/// `--bcm-dump=ADDR:W:H:FILE` — read a framebuffer out of the co-processor and write it as a PPM.
///
/// The host hands the BCM a bitmap; capturing what it *writes* gives us the image without having to
/// execute the co-processor's own firmware. Pixels are RGB565, the 5G panel format.
fn report_bcm_dump(args: &[String], m: &eapp_loader::Machine) {
    let Some(b) = &m.mem.bcm else { return };
    for spec in args.iter().filter_map(|a| a.strip_prefix("--bcm-dump=")) {
        // `splitn(4, …)` and not `split(…)`: the fourth field is a PATH, and a Windows path has a
        // colon in it. Split unbounded, `C:\out.ppm` becomes five fields, the length test fails,
        // and the `continue` below drops the flag **silently** — an instrument that does nothing
        // and says nothing. `film.rs` already spelled it this way; this one did not.
        let p: Vec<&str> = spec.splitn(4, ':').collect();
        if p.len() != 4 {
            continue;
        }
        let parse = |t: &str| u32::from_str_radix(t.trim_start_matches("0x"), 16).ok();
        let (Some(addr), Some(w), Some(h)) = (parse(p[0]), parse(p[1]), parse(p[2])) else {
            continue;
        };
        let mut out = format!("P6\n{w} {h}\n255\n").into_bytes();
        let mut nonzero = 0u32;
        for i in 0..w * h {
            let px = b.mem.get(&(addr + i * 2)).copied().unwrap_or(0);
            if px != 0 {
                nonzero += 1;
            }
            let r = ((px >> 11) & 0x1f) as u8;
            let g = ((px >> 5) & 0x3f) as u8;
            let bl = (px & 0x1f) as u8;
            out.extend_from_slice(&[(r << 3) | (r >> 2), (g << 2) | (g >> 4), (bl << 3) | (bl >> 2)]);
        }
        match std::fs::write(p[3], &out) {
            Ok(()) => println!(
                "\nbcm dump {addr:#010x} {w}x{h} -> {} ({nonzero} non-zero pixels of {})",
                p[3],
                w * h
            ),
            Err(e) => println!("bcm dump {}: {e}", p[3]),
        }
    }
}

/// `--bcm-peek=ADDR[:N]` — print N 32-bit words of the co-processor's internal memory at the end
/// of the run. Pure instrument: the framebuffer dump renders halfwords as RGB565 and loses the low
/// bits, which is useless when the question is whether a word reads exactly `1`.
fn report_bcm_peek(args: &[String], m: &eapp_loader::Machine) {
    let Some(b) = &m.mem.bcm else { return };
    for spec in args.iter().filter_map(|a| a.strip_prefix("--bcm-peek=")) {
        let (a, n) = spec.split_once(':').unwrap_or((spec, "4"));
        let Ok(addr) = u32::from_str_radix(a.trim_start_matches("0x"), 16) else { continue };
        let n: u32 = n.parse().unwrap_or(4);
        println!("\nbcm peek {addr:#010x} +{n} words:");
        for i in 0..n {
            let w = addr + i * 4;
            let lo = b.mem.get(&w).copied().unwrap_or(0) as u32;
            let hi = b.mem.get(&(w + 2)).copied().unwrap_or(0) as u32;
            println!("  {w:#010x} = {:#010x}", lo | (hi << 16));
        }
    }
}

/// Print unmapped accesses grouped by page, busiest first.
///
/// Every line here is a piece of hardware we have not modelled, named by the address range it
/// answers on and by the PC that went looking for it — which is the part that makes it actionable.
fn report_unmapped(m: &mut eapp_loader::Machine) {
    if m.mem.unmapped.is_empty() {
        return;
    }
    let (reads, writes) = m.mem.unmapped_totals();
    println!(
        "\nunmapped: {reads} reads, {writes} writes across {} pages",
        m.mem.unmapped.len()
    );
    let report = m.mem.unmapped_report();
    for line in report.iter().take(12) {
        println!("  {line}");
    }
    // `unmapped` is a per-page map and cannot saturate; this print can. Say which.
    if report.len() > 12 {
        println!("  … and {} more pages (the totals above are complete)", report.len() - 12);
    }
    // The register file at the fault, which is the only thing that names *where the bad address
    // came from*. A PC plus an address cannot distinguish "firmware computed this" from "our
    // emulator handed it back", and that distinction was the whole question for 0xea000078.
    println!("  register files captured at the fault: {}", m.unmapped_regs.census());
    let faults: Vec<_> = m.unmapped_regs.iter().take(8).cloned().collect();
    for (pc, r) in faults {
        let w = m.mem.read32(pc);
        println!("  at {pc:#010x}  {}", disasm::arm(w, pc, None));
        for row in 0..4 {
            let c: Vec<String> =
                (0..4).map(|i| format!("r{:<2}={:08x}", row * 4 + i, r[row * 4 + i])).collect();
            println!("       {}", c.join("  "));
        }
    }
}

/// Print whatever `--break` and `--watch` collected.
///
/// Shared because `--boot-osos` returns from `main` early; when this only existed at the bottom
/// of `main`, both flags were accepted, fired correctly, and reported nothing on that path.
fn report_break_watch(m: &mut eapp_loader::Machine) {
    let args: Vec<String> = std::env::args().collect();
    if !m.break_log.is_empty() {
        // Every hit, not a sample. An earlier truncation to 8 was read as the whole set and
        // produced a confidently wrong conclusion about which branch a wait had taken.
        println!("\n--- breakpoint hits: {} ---", m.break_log.len());
        let mut tally: std::collections::BTreeMap<u32, usize> = Default::default();
        for (pc, _) in &m.break_log {
            *tally.entry(*pc).or_default() += 1;
        }
        for (pc, n) in &tally {
            println!("  {pc:#010x}  x{n}");
        }
        // A compact tail as well as the verbose head. For a blocking primitive the question is
        // "which object, from which caller", asked of the LAST calls before the system settled —
        // and a 16-register dump of the first 64 hits answers neither.
        let tail = m.break_log.len().saturating_sub(40);
        println!("  last {} hits (pc, r0..r3, lr):", m.break_log.len() - tail);
        for (pc, r) in m.break_log.iter().skip(tail) {
            println!(
                "    {pc:#010x}  r0={:#010x} r1={:#010x} r2={:#010x} r3={:#010x}  lr={:#010x}",
                r[0], r[1], r[2], r[3], r[14]
            );
        }
        for (pc, regs) in m.break_log.iter().take(0) {
            println!("  at {pc:#010x}");
            for row in 0..4 {
                let cells: Vec<String> = (0..4)
                    .map(|c| { let i = row * 4 + c; format!("r{i:<2}={:#010x}", regs[i]) })
                    .collect();
                println!("    {}", cells.join("  "));
            }
        }
        if m.break_log.len() > 64 {
            println!("  … {} more register dumps elided (tally above is complete)", m.break_log.len() - 64);
        }
    }
    for (pc, addr, sum, head) in &m.sum_at_log {
        let h: Vec<String> = head.iter().map(|b| format!("{b:02x}")).collect();
        println!("\nat {pc:#010x}: sum over {addr:#010x} = {sum:#010x}\n  first 16: {}", h.join(" "));
    }
    if let Some(path) = args.iter().find_map(|a| a.strip_prefix("--storelog-dump=")) {
        let mut out = String::from("pc\taddr\tvalue\ticount\n");
        for &(pc, addr, val, n) in m.mem.store_pc_log.iter() {
            out.push_str(&format!("{pc:#010x}\t{addr:#010x}\t{val:#010x}\t{n}\n"));
        }
        std::fs::write(path, out).ok();
        println!(
            "wrote {} store records to {path} — of {} that happened",
            m.mem.store_pc_log.sample().len(),
            m.mem.store_pc_log.seen()
        );
    }
    if let Some(path) = args.iter().find_map(|a| a.strip_prefix("--readlog-dump=")) {
        let mut out = String::from("pc\taddr\tvalue\ticount\n");
        for &(pc, addr, v, n) in m.mem.read_log.iter() {
            out.push_str(&format!("{pc:#010x}\t{addr:#010x}\t{v:#04x}\t{n}\n"));
        }
        std::fs::write(path, out).ok();
        println!(
            "wrote {} read records to {path} — of {} that happened",
            m.mem.read_log.sample().len(),
            m.mem.read_log.seen()
        );
    }
    if !m.mem.read_log.is_empty() {
        // This is the instrument whose 2 000 000-entry cap turned a control read 9 588 012 times
        // into a clean zero for four fifths of a run — a "confirmation" that read as a measurement.
        // The per-reader table now comes from `read_sites`, which cannot saturate.
        println!("\n--- reads of watched addresses: {} ---", m.mem.read_log.census());
        for ((addr, pc), (n, first)) in &m.mem.read_sites {
            println!("  [{addr:#010x}] read by {pc:#010x}  x{n}  first @{first}");
        }
    }
    if !m.mem.store_pc_log.is_empty() {
        let l = m.mem.store_pc_log.sample();
        println!("\n--- stores by watched instructions: {} ---", m.mem.store_pc_log.census());
        // Consecutive targets, with the gap between them. An object array shows up as a constant
        // gap; a heap allocation shows up as an irregular one. That distinction is the whole reason
        // this instrument exists, so it is computed rather than left to be eyeballed.
        let mut prev: Option<u32> = None;
        for &(pc, addr, val, n) in l.iter().take(400) {
            let gap = prev.map(|p| addr.wrapping_sub(p) as i32).unwrap_or(0);
            let g = if prev.is_some() { format!("{gap:+#x}") } else { "-".into() };
            println!("  {pc:#010x} -> [{addr:#010x}] = {val:#010x}   gap {g:>8}   @{n}");
            prev = Some(addr);
        }
        if let Some(x) = m.mem.store_pc_log.more_line(400) {
            println!("{x}");
        }
        // Strides are computed over the KEPT rows: a gap that straddles the cap boundary would be
        // an artefact of the instrument, not of the heap.
        let mut strides: std::collections::BTreeMap<i64, usize> = Default::default();
        for w in l.windows(2) {
            *strides.entry(w[1].1 as i64 - w[0].1 as i64).or_default() += 1;
        }
        let mut top: Vec<_> = strides.into_iter().collect();
        top.sort_by_key(|&(_, n)| std::cmp::Reverse(n));
        let s: Vec<String> =
            top.iter().take(6).map(|(d, n)| format!("{d:+#x} x{n}")).collect();
        println!("  strides: {}", s.join("  "));
    }
    if m.force_vc_retire {
        println!(
            "\n--- bypass #17 stage 2: descriptor rings drained on the spin edge: {} ---",
            m.force_retire_log.census()
        );
        for &(ch, n) in m.force_retire_log.iter().take(16) {
            println!("  channel={ch:#010x}  @{n}");
        }
        if let Some(x) = m.force_retire_log.more_line(16) {
            println!("{x}");
        }
    }
    if !m.force_sem_log.is_empty() {
        println!("\n--- bypass #17: pends satisfied without a producer: {} ---", m.force_sem_log.len());
        for &(lr, sem, n) in m.force_sem_log.iter().take(64) {
            println!("  sem={sem:#04x}  returned to lr={lr:#010x}  @{n}");
        }
        if m.force_sem_log.len() > 64 {
            println!("  … {} more", m.force_sem_log.len() - 64);
        }
    }
    // Armed and never reached is a RESULT, and it used to print nothing at all — indistinguishable
    // from having forgotten the flag, which is the difference between "the task never ran" and
    // "you did not ask". Say it.
    if !m.enter_pcs.is_empty() && m.enter_log.is_empty() {
        println!("\n--- arrivals at watched addresses: 0 ---");
        for pc in &m.enter_pcs {
            let name = m.symbolise(*pc).unwrap_or_else(|| "unnamed".into());
            println!("  {pc:#010x}  {name}  NEVER REACHED");
        }
    }
    if !m.enter_log.is_empty() {
        let l = &m.enter_log;
        println!("\n--- arrivals at watched addresses: {} ---", l.census());
        for &(pc, lr, a, n) in l.iter().take(400) {
            println!(
                "  {pc:#010x} lr={lr:#010x}  r0={:#010x} r1={:#010x} r2={:#010x} r3={:#010x}  @{n}",
                a[0], a[1], a[2], a[3]
            );
        }
        if let Some(x) = l.more_line(400) {
            println!("{x}");
        }
        // Grouped by caller: twenty static call sites and one that behaves differently is the
        // shape this is looking for. Tallied on arrival rather than from the rows above, so this
        // stays a true census after the 65 536-entry log has filled — which is what the instrument
        // table has been telling readers it already was.
        println!("  callers (uncapped census, {} distinct):", m.enter_callers.len());
        for ((pc, lr), n) in &m.enter_callers {
            println!("    {pc:#010x} from lr={lr:#010x}  x{n}");
        }
    }
    if !m.retwatch_log.is_empty() {
        let v = m.retwatch.unwrap_or(0);
        println!("\n--- r0 became {v:#x}: {} times ---", m.retwatch_log.census());
        // Distinct producing instructions, with how often each fired: one site repeated in a loop
        // is one answer, not a hundred. From `retwatch_sites`, which is uncapped.
        let sites: Vec<(u32, (u64, u32))> =
            m.retwatch_sites.iter().map(|(pc, e)| (*pc, *e)).collect();
        for (pc, (n, lr)) in sites {
            let w = m.mem.read32(pc);
            println!("  at {pc:#010x}  {:<28} lr={lr:#010x}  x{n}", disasm::arm(w, pc, None));
        }
    }
    if !m.watch_log.is_empty() {
        println!("\n--- watch: {} changes ---", m.watch_log.census());
        for (pc, old, new) in m.watch_log.iter().take(24) {
            let w = m.mem.read32(*pc);
            println!("  {pc:#010x}  {old:#010x} -> {new:#010x}   {}", disasm::arm(w, *pc, None));
        }
        if let Some(x) = m.watch_log.more_line(24) {
            println!("{x}");
        }
    } else if m.watch.is_some() {
        // Say so explicitly. A silent absence reads as "the flag does not work"; it is a result.
        println!("\n--- watch: no writes observed ---");
    }
}

/// Map the memory RetailOS code expects — see [`eapp_loader::map_hardware`], which is where it
/// lives now.
///
/// Moved out of this file when `tools/ipod-gui` became a second front end over the same machine.
/// Kept as a delegate rather than replaced at the two call sites so that the diff which moved it
/// proves itself: the body went to the library and nothing here changed but this line.
fn map_hardware(m: &mut eapp_loader::Machine, cold_boot: bool) {
    eapp_loader::map_hardware(m, cold_boot);
}
