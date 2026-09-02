//! Static thunk-coverage audit: which framework ordinals can the games actually reach, and
//! which of those does the emulator answer with something other than a silent zero?
//!
//! §18 of `reversing/asyncfileio-abi.md` did this by hand for six titles and one framework. It
//! went stale within a week — seven of its twelve "missing" ordinals were implemented, and two
//! more (`OpenGLES #160`, `#168`) turned up in a title it never scanned. This exists so the
//! number stays live.
//!
//! Method, unchanged from §18.1: parse every framework descriptor with the loader's own parser
//! (a second one would eventually disagree with it), then count three kinds of reference into
//! the thunk array —
//!
//!   * a direct `B`/`BL` to a thunk,
//!   * a `B`/`BL` to a **veneer** — a one-instruction `b <thunk>` the linker interposed,
//!   * a literal-pool word holding a thunk address or its resolved slot.
//!
//! A veneer nothing branches to is still a live reference the linker chose to keep, so it counts
//! as one use; §18.1 wrote those as `1v`. They are reported separately as **soft** because a
//! stray `b` into the thunk array from misparsed data looks identical, and on a 700 KB image
//! that happens.
//!
//! Self-check: `--verify` reproduces §18.1's hand-counted rows for Minigolf, Zuma and Pac-Man
//! exactly — same ordinals, same call-site counts. Those three were counted by a different
//! person by a different route, so agreement is evidence the walk is right rather than evidence
//! it is self-consistent.

use eapp_loader::EApp;
use std::collections::{BTreeMap, BTreeSet};

/// `(framework, ordinal)`.
type Ord = (String, usize);

#[derive(Default)]
struct Title {
    name: String,
    /// Ordinals with a real call site or literal reference.
    hard: BTreeMap<Ord, u32>,
    /// Ordinals reached only through an orphan veneer.
    soft: BTreeSet<Ord>,
    /// Every framework this binary declares, and how many thunks it carries.
    published: BTreeMap<String, usize>,
}

fn u32le(b: &[u8], o: usize) -> u32 {
    if o + 4 > b.len() {
        return 0;
    }
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}

fn sext24(v: u32) -> i32 {
    if v & 0x0080_0000 != 0 {
        (v | 0xFF00_0000) as i32
    } else {
        v as i32
    }
}

fn scan(path: &std::path::Path) -> Option<Title> {
    let image = std::fs::read(path).ok()?;
    let app = EApp::parse(image).ok()?;
    let img = &app.image;
    let base = app.load_base;

    let mut t = Title {
        name: path
            .file_stem()?
            .to_string_lossy()
            .split("_1_1_")
            .next()?
            .to_string(),
        ..Default::default()
    };

    // thunk address -> (framework, ordinal), and the resolved slot that follows the array.
    let mut thunk_of: BTreeMap<u32, Ord> = BTreeMap::new();
    let mut slot_of: BTreeMap<u32, Ord> = BTreeMap::new();
    for fw in &app.frameworks {
        t.published.insert(fw.name.clone(), fw.thunks.len());
        let slots = fw.thunks.first().copied().unwrap_or(0) + 4 * fw.thunks.len() as u32;
        for (i, a) in fw.thunks.iter().enumerate() {
            thunk_of.insert(*a, (fw.name.clone(), i));
            slot_of.insert(slots + 4 * i as u32, (fw.name.clone(), i));
        }
    }

    // Every branch edge in the image. `uncond` is the AL condition specifically: a veneer is
    // always unconditional, and a conditional branch into a thunk is a real (predicated) call.
    let mut edges: Vec<(u32, u32, bool, bool)> = Vec::new();
    for off in (0..img.len().saturating_sub(3)).step_by(4) {
        let ins = u32le(img, off);
        if ins >> 28 == 0xF || (ins >> 25) & 7 != 5 {
            continue;
        }
        let pc = base + off as u32;
        let target = pc
            .wrapping_add(8)
            .wrapping_add((sext24(ins & 0x00FF_FFFF) << 2) as u32);
        edges.push((pc, target, ins & (1 << 24) != 0, ins >> 28 == 0xE));
    }

    // A veneer is `b <thunk>`; veneers can chain, so settle the map before using it.
    let mut veneer: BTreeMap<u32, Ord> = BTreeMap::new();
    for &(pc, target, link, uncond) in &edges {
        if uncond && !link {
            if let Some(o) = thunk_of.get(&target) {
                veneer.insert(pc, o.clone());
            }
        }
    }
    for _ in 0..3 {
        let mut add = Vec::new();
        for &(pc, target, link, uncond) in &edges {
            if uncond && !link && !veneer.contains_key(&pc) {
                if let Some(o) = veneer.get(&target) {
                    add.push((pc, o.clone()));
                }
            }
        }
        if add.is_empty() {
            break;
        }
        veneer.extend(add);
    }

    let mut called_veneer: BTreeSet<u32> = BTreeSet::new();
    for &(pc, target, _, _) in &edges {
        if veneer.contains_key(&pc) {
            continue; // the veneer body is not itself a call site
        }
        let who = thunk_of.get(&target).or_else(|| veneer.get(&target));
        if let Some(o) = who {
            *t.hard.entry(o.clone()).or_insert(0) += 1;
            if veneer.contains_key(&target) {
                called_veneer.insert(target);
            }
        }
    }

    // Literal-pool references promote an ordinal to hard: the address is in the data, so
    // something means to call it however it gets there.
    for off in (0..img.len().saturating_sub(3)).step_by(4) {
        let w = u32le(img, off);
        if let Some(o) = thunk_of.get(&w).or_else(|| slot_of.get(&w)) {
            *t.hard.entry(o.clone()).or_insert(0) += 1;
        }
    }

    for (pc, o) in &veneer {
        if !called_veneer.contains(pc) && !t.hard.contains_key(o) {
            t.soft.insert(o.clone());
        }
    }
    Some(t)
}

/// The ordinals `play.rs` gives a behaviour to, read out of its `set_stub` calls.
fn implemented(src: &str) -> BTreeSet<Ord> {
    let mut out = BTreeSet::new();
    for (i, _) in src.match_indices("set_stub(\"") {
        let rest = &src[i + 10..];
        let Some(q) = rest.find('"') else { continue };
        let name = rest[..q].to_string();
        let tail = rest[q + 1..].trim_start_matches([',', ' ']);
        let digits: String = tail.chars().take_while(|c| c.is_ascii_digit()).collect();
        if let Ok(n) = digits.parse::<usize>() {
            out.insert((name, n));
        }
    }
    out
}

/// §18.1's hand-counted OpenGLES rows for the three titles it recorded without ambiguity.
const VERIFY: &[(&str, &[(usize, u32)])] = &[
    (
        "Minigolf",
        &[
            (4, 9), (12, 3), (13, 4), (19, 1), (21, 1), (36, 8), (37, 4), (40, 8), (53, 24),
            (84, 1), (99, 1), (101, 1), (125, 1), (137, 8), (157, 1), (158, 1), (159, 5),
            (165, 2), (167, 1), (175, 3),
        ],
    ),
    (
        "Zuma",
        &[
            (4, 17), (12, 2), (13, 3), (36, 10), (37, 19), (40, 32), (45, 2), (84, 2), (99, 3),
            (105, 1), (125, 17), (137, 32), (148, 16), (157, 1), (158, 1), (159, 17), (165, 26),
            (167, 3), (169, 14), (171, 5), (173, 7), (175, 7),
        ],
    ),
    (
        "Pacman",
        &[
            (4, 2), (12, 3), (13, 2), (35, 2), (36, 3), (37, 6), (38, 2), (40, 6), (99, 2),
            (125, 1), (137, 16), (157, 1), (158, 1), (159, 2), (167, 1),
        ],
    ),
];

fn verify(titles: &[Title]) -> bool {
    let mut ok = true;
    for (name, want) in VERIFY {
        let Some(t) = titles.iter().find(|t| t.name == *name) else {
            println!("verify {name}: NOT SCANNED");
            ok = false;
            continue;
        };
        let got: BTreeMap<usize, u32> = t
            .hard
            .iter()
            .filter(|((fw, _), _)| fw == "OpenGLES")
            .map(|((_, o), c)| (*o, *c))
            .collect();
        let want: BTreeMap<usize, u32> = want.iter().copied().collect();
        if got == want {
            println!("verify {name}: ok ({} ordinals)", want.len());
        } else {
            ok = false;
            println!("verify {name}: MISMATCH");
            for o in want.keys().chain(got.keys()).collect::<BTreeSet<_>>() {
                let (w, g) = (want.get(o), got.get(o));
                if w != g {
                    println!("  #{o}: §18 says {w:?}, scan says {g:?}");
                }
            }
        }
    }
    ok
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let root = args
        .iter()
        .find(|a| !a.starts_with("--"))
        .cloned()
        .unwrap_or_else(|| {
            eprintln!(
                "usage: covscan <Games_RO dir> [--impl=<play.rs>] [--verify] [--per-title]\n\
                 \n\
                 Scans every */Executables/*.bin under the directory."
            );
            std::process::exit(2);
        });

    let mut paths: Vec<std::path::PathBuf> = Vec::new();
    for game in std::fs::read_dir(&root).into_iter().flatten().flatten() {
        let exe = game.path().join("Executables");
        for f in std::fs::read_dir(exe).into_iter().flatten().flatten() {
            if f.path().extension().is_some_and(|e| e == "bin") {
                paths.push(f.path());
            }
        }
    }
    paths.sort();

    // Identical builds shipped under several ids (testprep is present three times) would
    // triple-count in the frequency ranking, so keep one binary per title name.
    let mut titles: Vec<Title> = Vec::new();
    for p in &paths {
        match scan(p) {
            Some(t) if titles.iter().any(|x| x.name == t.name) => {}
            Some(t) => titles.push(t),
            None => eprintln!("skip {}", p.display()),
        }
    }

    if args.iter().any(|a| a == "--verify") {
        std::process::exit(if verify(&titles) { 0 } else { 1 });
    }

    // Stubs live in two places — the viewer's own table and the shared `install_audit_stubs` in
    // the library — so both are read. Compiling them in means the default answer is right
    // without the caller having to know where the sources are.
    let mut impl_src: String = args
        .iter()
        .filter_map(|a| a.strip_prefix("--impl="))
        .map(|p| std::fs::read_to_string(p).expect("cannot read --impl file"))
        .collect::<Vec<_>>()
        .join("\n");
    if impl_src.is_empty() {
        impl_src = include_str!("play.rs").to_string();
    }
    impl_src.push_str(include_str!("../lib.rs"));
    let done = implemented(&impl_src);

    let mut published: BTreeMap<String, usize> = BTreeMap::new();
    for t in &titles {
        for (k, v) in &t.published {
            let e = published.entry(k.clone()).or_insert(0);
            *e = (*e).max(*v);
        }
    }

    let mut hard: BTreeSet<Ord> = BTreeSet::new();
    let mut soft: BTreeSet<Ord> = BTreeSet::new();
    for t in &titles {
        hard.extend(t.hard.keys().cloned());
        soft.extend(t.soft.iter().cloned());
    }
    soft = soft.difference(&hard).cloned().collect();

    println!("{} titles scanned\n", titles.len());
    println!(
        "{:<14}{:>5}{:>8}{:>6}{:>9}{:>7}",
        "framework", "pub", "called", "impl", "MISSING", "soft"
    );
    let (mut tp, mut tc, mut ti, mut tm, mut ts) = (0, 0, 0, 0, 0);
    for (fw, count) in &published {
        let of = |s: &BTreeSet<Ord>| -> BTreeSet<usize> {
            s.iter().filter(|(f, _)| f == fw).map(|(_, o)| *o).collect()
        };
        let (h, sf) = (of(&hard), of(&soft));
        let used: BTreeSet<usize> = h.union(&sf).copied().collect();
        let dn: BTreeSet<usize> = done
            .iter()
            .filter(|(f, _)| f == fw)
            .map(|(_, o)| *o)
            .collect();
        let miss: Vec<usize> = h.difference(&dn).copied().collect();
        let smiss: Vec<usize> = sf.difference(&dn).copied().collect();
        let live = used.intersection(&dn).count();
        println!(
            "{fw:<14}{count:>5}{:>8}{live:>6}{:>9}{:>7}",
            used.len(),
            miss.len(),
            smiss.len()
        );
        if !miss.is_empty() {
            println!("{:<14}  missing: {miss:?}", "");
        }
        if !smiss.is_empty() {
            println!("{:<14}  soft:    {smiss:?}", "");
        }
        let dead: Vec<usize> = dn.difference(&used).copied().collect();
        if !dead.is_empty() {
            println!("{:<14}  implemented but never called: {dead:?}", "");
        }
        tp += count;
        tc += used.len();
        ti += live;
        tm += miss.len();
        ts += smiss.len();
    }
    println!("{:<14}{tp:>5}{tc:>8}{ti:>6}{tm:>9}{ts:>7}", "TOTAL");

    // Frequency ranking: what to implement first is whatever the most titles cannot avoid.
    let mut freq: BTreeMap<Ord, usize> = BTreeMap::new();
    for t in &titles {
        for o in t.hard.keys() {
            if !done.contains(o) {
                *freq.entry(o.clone()).or_insert(0) += 1;
            }
        }
    }
    let mut by_count: BTreeMap<usize, Vec<String>> = BTreeMap::new();
    for (o, c) in &freq {
        by_count
            .entry(*c)
            .or_default()
            .push(format!("{}#{}", o.0, o.1));
    }
    println!("\n=== missing ordinals by how many titles call them ===");
    for (c, v) in by_count.iter().rev() {
        println!("{c:>3}/{}: {}", titles.len(), v.join(" "));
    }

    if args.iter().any(|a| a == "--per-title") {
        println!("\n=== per title ===");
        for t in &titles {
            let miss: Vec<String> = t
                .hard
                .keys()
                .filter(|o| !done.contains(*o))
                .map(|(f, o)| format!("{f}#{o}"))
                .collect();
            println!("{:<14}{:>3}  {}", t.name, miss.len(), miss.join(" "));
        }
    }
}
