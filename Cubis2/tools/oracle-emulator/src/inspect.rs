//! Look at a file the user picked and say, immediately, whether it will work.
//!
//! The single biggest usability cliff in this project is that a fresh clone does nothing: the NOR
//! dump and the disk image are Apple's, they are gitignored, and you supply your own. The second
//! biggest is what happens next — somebody supplies the wrong ones and finds out ninety seconds
//! into a boot, from a symptom that reads like a bug in the emulator.
//!
//! So both files are parsed before anything is built, and the report says what was *found*, not
//! merely pass or fail. "This is a 2 MiB dump; the 5G/5.5G ROM is 1 MiB" saves an evening.
//!
//! # Everything here is structure, not signature
//!
//! No hashes of Apple's files, no table of known-good dumps. Two reasons, and the second is the
//! load-bearing one: a hash table would only ever recognise the dumps we happen to have, and a
//! **legitimately different** dump — a different capacity, a different firmware version, somebody's
//! own 5.5G — would be rejected for being unfamiliar. The layout is what the emulator actually
//! needs, so the layout is what is checked.
//!
//! Both formats below were read out of the images this project boots, and both agree with the
//! published iPod firmware-directory layout (`ipodlinux`, and `tools/rsrc-extract.py` for the
//! disk side).

use std::fmt::Write as _;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

/// The verdict on one file.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Verdict {
    /// It is what the emulator wants. The string is what was found, for the user to read back.
    Good(String),
    /// It parses, and it is not this machine — or it parses with something odd about it. The
    /// emulator will be started only if the user insists.
    Wrong(String),
    /// It does not parse at all, or could not be read.
    Bad(String),
}

impl Verdict {
    pub fn ok(&self) -> bool {
        matches!(self, Verdict::Good(_))
    }
    pub fn text(&self) -> &str {
        match self {
            Verdict::Good(s) | Verdict::Wrong(s) | Verdict::Bad(s) => s,
        }
    }
}

/// Every image in an iPod firmware directory, on the NOR or on the disk.
///
/// One 40-byte record, and the same record in both places — only the magic differs, and the two
/// magics are not spelled the same way in the bytes. On the drive it reads **`!ATA`**, literally,
/// left to right. In the NOR it reads **`hslf`** — `flsh` stored as a little-endian word. Both are
/// u32 constants and one of them happens to be palindromic under the byte order; the first version
/// of this file assumed they behaved alike, compared the drive's against `ATA!`, and reported the
/// project's own reference image as having no firmware directory. Its unit tests passed, because
/// the test helper wrote the same wrong magic.
///
/// Fields, all little-endian:
///
/// ```text
///   +0x00  magic      "!ATA" / "flsh"
///   +0x04  tag        four characters stored as a u32, so `osos` reads as `soso` in a byte dump
///   +0x08  dev        0. `aupd`'s becomes 1 once the updater has run, which is how the second
///                     boot knows to skip it
///   +0x0c  devOffset  where the body is, relative to the partition (or to the NOR base)
///   +0x10  len        how long the body is
///   +0x14  addr       where it is loaded. 0x10000000 on a 5G/5.5G — the discriminator that
///                     matters here, because a later iPod does not load there
///   +0x18  entryOffset
///   +0x1c  checksum
///   +0x20  version
///   +0x24  loadAddr
/// ```
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Entry {
    pub tag: String,
    pub dev: u32,
    pub offset: u32,
    pub len: u32,
    pub addr: u32,
}

/// Where a 5G/5.5G loads every image it runs, from `osos` to the NOR's diagnostics.
pub const LOAD_ADDR_5G: u32 = 0x1000_0000;
/// The retail 5G/5.5G NOR is one megabyte.
pub const NOR_LEN: u64 = 1024 * 1024;
/// The image directory inside it.
const NOR_DIRECTORY: u64 = 0x000f_fe00;
/// The `!ATA` directory, relative to the start of the firmware partition.
const DISK_DIRECTORY: u64 = 0x4200;

fn parse_entries(buf: &[u8], magic: &[u8; 4]) -> Vec<Entry> {
    let mut out = Vec::new();
    for e in buf.chunks_exact(40) {
        if &e[0..4] != magic {
            break;
        }
        let w = |o: usize| u32::from_le_bytes([e[o], e[o + 1], e[o + 2], e[o + 3]]);
        // The tag is a little-endian u32 of four characters, so the bytes read backwards.
        let tag: String = e[4..8].iter().rev().map(|&b| b as char).collect();
        out.push(Entry { tag, dev: w(8), offset: w(0x0c), len: w(0x10), addr: w(0x14) });
    }
    out
}

fn read_at(path: &Path, at: u64, n: usize) -> std::io::Result<Vec<u8>> {
    let mut f = std::fs::File::open(path)?;
    f.seek(SeekFrom::Start(at))?;
    let mut buf = vec![0u8; n];
    // `read_exact` and not `read`: a short read at the end of a truncated image would otherwise
    // parse as a directory of zeroes and be reported as "no images", which is a different problem
    // from the one the user has.
    f.read_exact(&mut buf)?;
    Ok(buf)
}

// ---------------------------------------------------------------- what shall we call it?
//
// **A filename is what a file is called, not what it is.** Everything this program is handed
// arrives under a name chosen by whoever uploaded it: the boot ROM is
// `internal_rom_000000-0FFFFF.bin` by convention and `A1238/internal_rom.bin` in the archive that
// files it under the wrong product; Apple's bundle is `iPod_20.1.3.ipsw` or `iPod_20.1.3.zip`
// depending on what the browser did to it. None of those is worth showing to somebody who has
// three of them.
//
// So a file is described by what is *inside* it, and the filename is what you get on hover. This
// matters more the moment there is more than one iPod: `internal_rom_000000-0FFFFF.bin` three times
// is a list of nothing, and "iPod Video · 5A82…" three times is a list of iPods.

/// What a boot ROM is, in a few words — the model, and the device it came off.
///
/// The serial is the one printed on the back of the case, and the GUID is the FireWire ID that a
/// USB host sees; between them they are the difference between "an iPod Video ROM" and "*this* iPod
/// Video's ROM". That distinction is not cosmetic: `SysCfg` is per-device, the authorisation work
/// binds to the GUID in it, and two dumps of the same model are the same file only if you never
/// look at those bytes.
///
/// `None` when the file is not a ROM this program recognises — the caller has a verdict for that.
pub fn describe_rom(path: &Path, model: &str) -> Option<String> {
    let nor = std::fs::read(path).ok()?;
    if nor.len() as u64 != NOR_LEN {
        return None;
    }
    let cfg = syscfg(&nor)?;
    // The serial reads better than the GUID and is the thing written on the case, so it wins when
    // both are there. Truncated to its tail: the leading characters are the factory and the model,
    // identical across every iPod of a kind, and the distinguishing part is at the end.
    if let Some(sn) = cfg.serial.as_ref() {
        let tail: String = sn.chars().rev().take(5).collect::<Vec<_>>().into_iter().rev().collect();
        return Some(format!("{model} · {tail}"));
    }
    cfg.guid.map(|g| format!("{model} · {:04X}", g & 0xffff))
}

/// The version and content fingerprint of an Apple software bundle.
///
/// Both come from the firmware member the archive already has to parse: its name carries the
/// version (`Firmware-20.6.3`) and its zip record carries a CRC-32 of the contents. Together they
/// name a drive built from it in a way that is stable for the same bundle and different for a
/// different one — see [`built_drive_name`].
pub fn ipsw_identity(path: &Path) -> Option<(String, u32)> {
    let zip = crate::ipsw::Zip::open(path).ok()?;
    let (m, _) = zip.firmware().ok()?;
    let version = m.name.strip_prefix("Firmware-")?.to_string();
    Some((version, m.crc))
}

/// What to call a drive built from a bundle, on disk.
///
/// **Named for what it is, and never for the last thing that happened to it.** Every build used to
/// land on one path, `ipod-from-ipsw.img`, so building from a second bundle silently overwrote the
/// first — under a name that still looked right, while anything pointed at it had quietly become a
/// different iPod's software. Including the version makes the folder readable, and including the
/// CRC means the same bundle always resolves to the same file (so a rebuild is a no-op) while a
/// different one cannot land on it.
pub fn built_drive_name(version: &str, crc: u32) -> String {
    format!("ipod-{version}-{crc:08x}.img")
}

/// Read a drive's description back out of the name [`built_drive_name`] gave it.
///
/// A drive the user supplied themselves gets no description, because there is nothing honest to
/// say: the firmware version is inside the partition and this does not read it. The caller falls
/// back to the filename, which is the user's own word for it and therefore the right fallback.
pub fn describe_drive(path: &Path) -> Option<String> {
    let name = path.file_name()?.to_str()?;
    let rest = name.strip_prefix("ipod-")?.strip_suffix(".img")?;
    let (version, crc) = rest.rsplit_once('-')?;
    if crc.len() != 8 || !crc.chars().all(|c| c.is_ascii_hexdigit()) || version.is_empty() {
        return None;
    }
    Some(format!("iPod software {version}"))
}

// ---------------------------------------------------------------- what did we find in it?
//
// **The parse already happened; this is what it found.** Every verdict here reads a file and knows
// a great deal about it, and until now all of that was thrown away unless the file failed. A person
// holding three dumps off three iPods wants to know which is which, and a person about to spend
// seventy-five seconds on a boot wants to know the two files go together — both are questions these
// bytes have already answered.

/// One thing worth knowing about a file: a label and a value, short enough to sit on one line.
pub type Fact = (&'static str, String);

/// What a boot ROM turned out to contain.
pub fn rom_facts(path: &Path) -> Vec<Fact> {
    let Ok(nor) = std::fs::read(path) else { return Vec::new() };
    let mut out = Vec::new();
    if let Some(dir) = nor.get(NOR_DIRECTORY as usize..) {
        let images = parse_entries(dir, b"hslf");
        if !images.is_empty() {
            let names: Vec<&str> = images.iter().map(|e| e.tag.as_str()).collect();
            out.push(("Images", names.join(" · ")));
        }
    }
    if let Some(cfg) = syscfg(&nor) {
        if let Some(s) = cfg.serial {
            out.push(("Serial", s));
        }
        // The FireWire GUID **is** the USB serial a host sees, and it is what an authorisation is
        // minted against — so it is the one number that makes this dump *this iPod's* dump.
        if let Some(g) = cfg.guid {
            out.push(("FireWire GUID", format!("{g:016X}")));
        }
    }
    if let Some(b) = build_string(path) {
        out.push(("Build", b));
    }
    out
}

/// What a drive image turned out to contain.
pub fn drive_facts(path: &Path) -> Vec<Fact> {
    let mut out = Vec::new();
    if let Ok(m) = std::fs::metadata(path) {
        out.push(("Size", bytes(m.len())));
    }
    if let Ok(state) = crate::ipsw::firmware_state(path) {
        if !state.tags.is_empty() {
            out.push(("Firmware images", state.tags.join(" · ")));
        }
        out.push(("Operating system", if state.has_os { "present" } else { "MISSING" }.into()));
        if state.aupd_armed {
            // Not a detail. On hardware this is the first of two boots and the second runs the OS;
            // here nothing power-cycles the machine, so this drive stops at the updater.
            out.push(("Flash updater", "armed — this drive boots the updater, not the OS".into()));
        }
    }
    if let Some(f) = drive_family(path) {
        out.push(("Updater family", f.to_string()));
    }
    out
}

/// What an Apple software bundle turned out to contain.
pub fn ipsw_facts(path: &Path) -> Vec<Fact> {
    let Ok(zip) = crate::ipsw::Zip::open(path) else { return Vec::new() };
    let Ok((m, fw)) = zip.firmware() else { return Vec::new() };
    let mut out = vec![
        ("Firmware", m.name.clone()),
        ("Size", format!("{} bytes ({} sectors)", fw.len(), fw.len() / 512)),
    ];
    if let Some(f) = family_of(&m.name) {
        out.push(("Updater family", f.to_string()));
    }
    let images = crate::ipsw::images(&fw);
    if !images.is_empty() {
        let names: Vec<&str> = images.iter().map(|i| i.tag.as_str()).collect();
        out.push(("Images", names.join(" · ")));
    }
    out
}

/// The updater family in a `Firmware-20.6.3` member name.
fn family_of(member: &str) -> Option<u32> {
    member.strip_prefix("Firmware-")?.split('.').next()?.parse().ok()
}

/// The updater family of an Apple bundle.
pub fn ipsw_family(path: &Path) -> Option<u32> {
    let zip = crate::ipsw::Zip::open(path).ok()?;
    let (m, _) = zip.firmware().ok()?;
    family_of(&m.name)
}

/// The updater family of a drive **this program built**, read back out of the name it was given.
///
/// A drive somebody supplied themselves answers `None`, and that is the honest answer rather than a
/// gap: the version lives inside the firmware partition in a form this does not read, and guessing
/// it would put a confident number next to a mismatch warning that might be backwards.
pub fn drive_family(path: &Path) -> Option<u32> {
    let name = path.file_name()?.to_str()?;
    let rest = name.strip_prefix("ipod-")?.strip_suffix(".img")?;
    let (version, _) = rest.rsplit_once('-')?;
    version.split('.').next()?.parse().ok()
}

/// Do this boot ROM and this software belong together?
///
/// **The single most expensive failure this project has**, and it is silent: a bundle from the
/// wrong updater family boots, is not recognised as this iPod's own software, and shows the
/// plug-into-a-computer screen after about 70 ATA commands where a matching pair reaches the
/// language picker with 618. That reads as a broken emulator. It cost the first person who hit it
/// an hour, and it is knowable before the boot starts from two numbers both already parsed.
///
/// `None` means "no reason to object" — which includes *not knowing*, and the two are deliberately
/// the same answer. A drive somebody supplied has no family this can read, and a warning that
/// fired on every such drive would be noise that teaches people to ignore the one that matters.
pub fn family_mismatch(model: &str, model_family: u32, software: Option<u32>) -> Option<String> {
    let found = software?;
    if found == model_family {
        return None;
    }
    // No article before the model name. "a {model}" produces "a iPod Video", and the fix is not a
    // vowel test — the next model added would break it again, in a string nobody re-reads.
    Some(format!("Family {found}. {model} takes family {model_family}."))
}

/// Why a family mismatch matters, for the hover rather than the page.
///
/// Split from the warning itself because the warning has to fit next to everything else that can be
/// wrong at once — measured at 717 px against a 680 px window when this was one paragraph — and
/// because the *fact* is what somebody needs at a glance while the *consequence* is what they need
/// only if they doubt it.
pub const WHY_FAMILY_MATTERS: &str =
    "Apple ships a model's software under an updater family, and an iPod only recognises its own. \
     A mismatched pair is not rejected: it boots, fails to recognise the drive as its own, and \
     asks to be restored from iTunes after about 70 ATA commands where a matching pair reaches the \
     language picker with 618. That looks like a broken emulator, and it is the single most \
     expensive misunderstanding this project has met.";

// ---------------------------------------------------------------- what did they just hand us?

/// Which of the three files the emulator takes this one is, judged by its contents.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    /// A NOR flash dump — the boot ROM.
    Rom,
    /// An Apple software bundle, to build a drive from.
    Ipsw,
    /// A whole-drive image.
    Disk,
    /// None of the three.
    Unknown,
}

/// Say what a file *is*, so the user does not have to say it.
///
/// **This is what lets the first-run screen have no slots.** Asking somebody to put the right file
/// in the right box is asking them to already know the answer to the question they came here with —
/// and the two files are told apart by four bytes and a length, which is a thing a program should
/// do. Drop both in any order, or drop them one at a time, or drop them on the wrong half of the
/// window: each one is identified and routed.
///
/// Cheap enough to run on a drop rather than on a timer: one `metadata` call and four bytes. The
/// real verdict — [`flash`], [`ipsw`], [`disk`] — is what gets run afterwards on the one that
/// matched, and that is where the reading of hundreds of bytes happens.
///
/// The order of the tests is the order of how *specific* they are, not how likely:
///
/// 1. **Zip magic** is four bytes and cannot be anything else here. An `.ipsw` is a zip; a drive
///    image begins with an MBR or an Apple Partition Map, neither of which starts `PK`.
/// 2. **Exactly 1 MiB** is the NOR, and the size is the check the ROM's own verdict leads with.
///    A file of that size that turns out not to be a ROM gets [`flash`]'s message about its reset
///    vector, which is the right message — better than "not recognised".
/// 3. **Everything else is judged as a drive**, because that is the file with the widest legitimate
///    variation (capacities, partition maps, Mac and Windows formats), and because [`disk`] has
///    the best sentences for the near misses: an Apple Partition Map, a GPT, a partition image
///    rather than a whole drive.
///
/// A file that is none of these is [`Kind::Unknown`], and the screen says so with its size — which
/// is more use than the wrong file's verdict would be.
pub fn classify(path: &Path) -> Kind {
    let Ok(meta) = std::fs::metadata(path) else { return Kind::Unknown };
    if !meta.is_file() {
        return Kind::Unknown;
    }
    let head = read_at(path, 0, 4).unwrap_or_default();
    if head.len() == 4 && head[0..2] == *b"PK" && (head[2] == 3 || head[2] == 5 || head[2] == 7) {
        return Kind::Ipsw;
    }
    if meta.len() == NOR_LEN {
        return Kind::Rom;
    }
    // A drive is at least a partition table, and the smallest thing worth calling one is far more
    // than a sector. Below that there is nothing to say but "this is not any of them", and saying
    // it about a 12-byte file is more useful than handing it to the disk parser.
    if meta.len() >= 1024 * 1024 {
        return Kind::Disk;
    }
    Kind::Unknown
}

// ---------------------------------------------------------------- the NOR dump

/// Parse a NOR flash dump and say what it is.
///
/// Three things are checked, in the order that makes the message most useful:
///
/// 1. **Length.** The 5G/5.5G bootrom is 1 MiB. Rockbox's "Dump ROM contents" names its output
///    after the range it read — `internal_rom_000000-0FFFFF.bin` is this one, and a
///    `…-1FFFFF.bin` is 2 MiB and a different machine.
/// 2. **The reset vector.** Word 0 must be an ARM `B` (`0xEA……`), because that is where a PP502x
///    fetches out of reset. Apple's is `0xea001ffe`.
/// 3. **The image directory at `0xffe00`**, magic `flsh`, whose entries name `disk`, `diag`,
///    `scan`, `logo` and `vmcs` and load every one of them at `0x10000000`.
pub fn flash(path: &Path) -> Verdict {
    let len = match std::fs::metadata(path) {
        Ok(m) => m.len(),
        Err(e) => return Verdict::Bad(format!("cannot read this file: {e}")),
    };
    // Size first, because it is the check that produces the most useful sentence, and because
    // reading before measuring turns every short file into "failed to fill whole buffer" — which
    // is what an empty dump used to report, the one case where the length *is* the whole diagnosis.
    if len == 0 {
        return Verdict::Bad(
            "the file is empty (0 bytes). The dump did not write anything. Rockbox's \
             \"Dump ROM contents\" writes its output at the end, so an iPod reset before it \
             finishes — or one that never finishes — leaves exactly this: a file with the right \
             name and no contents."
                .into(),
        );
    }
    // The conventional filename says the range: `internal_rom_000000-0FFFFF.bin` is this one,
    // 0x00000..0xFFFFF.
    if len < NOR_LEN {
        return Verdict::Wrong(format!(
            "{} — too small for a 5G/5.5G NOR, which is exactly 1 MiB (1 048 576 bytes). \
             A 512 KiB dump is a nano-class device. This emulator models the 5G/5.5G (PP5021C) \
             only.",
            bytes(len)
        ));
    }
    if len > NOR_LEN {
        // 2 MiB is the interesting case and deserves its own sentence: it is somebody else's iPod,
        // dumped perfectly correctly. Anything much larger is not a ROM dump at all, and saying
        // "this is a 6G Classic" about a 13 MiB firmware bundle would be a confident wrong answer.
        let what = if len <= 4 * NOR_LEN {
            "A 2 MiB dump is a later model: a 6G Classic or a nano."
        } else {
            "A file this size is not a ROM dump — a firmware bundle, an .ipsw, or a disk image."
        };
        return Verdict::Wrong(format!(
            "{} — larger than a 5G/5.5G NOR, which is exactly 1 MiB (1 048 576 bytes). {what} \
             The 5G/5.5G dump is conventionally named `internal_rom_000000-0FFFFF.bin`, after the \
             offset range it covers; a `…-1FFFFF.bin` is twice the chip and a different machine.",
            bytes(len)
        ));
    }
    let head = match read_at(path, 0, 4) {
        Ok(b) => u32::from_le_bytes([b[0], b[1], b[2], b[3]]),
        Err(e) => return Verdict::Bad(format!("cannot read this file: {e}")),
    };
    if head >> 24 != 0xEA {
        return Verdict::Bad(format!(
            "word 0 is {head:#010x}, which is not an ARM branch. A PP502x fetches its reset vector \
             at address 0, and Apple's is a `B` — {:#010x} on the retail 5G. This file is not a raw \
             NOR dump (an encrypted or wrapped dump, or a different file entirely).",
            0xea00_1ffeu32
        ));
    }

    let dir = match read_at(path, NOR_DIRECTORY, 0x200) {
        Ok(b) => parse_entries(&b, b"hslf"),
        Err(e) => return Verdict::Bad(format!("cannot read the image directory: {e}")),
    };
    if dir.is_empty() {
        return Verdict::Wrong(format!(
            "1 MiB and a plausible reset vector, but no `flsh` image directory at {NOR_DIRECTORY:#x}. \
             The 5G/5.5G NOR carries one, naming the images it can boot — disk, diag, logo, vmcs. This may be a \
             dump of a different model, or of only part of the chip."
        ));
    }
    let stray: Vec<&Entry> = dir.iter().filter(|e| e.addr != LOAD_ADDR_5G).collect();
    if !stray.is_empty() {
        return Verdict::Wrong(format!(
            "the `flsh` directory loads `{}` at {:#010x}, not {LOAD_ADDR_5G:#010x}. \
             Every image on a 5G/5.5G loads at {LOAD_ADDR_5G:#010x}; a different load address is a \
             different machine.",
            stray[0].tag, stray[0].addr
        ));
    }

    let mut s = format!(
        "1 MiB, reset vector {head:#010x} (ARM branch), {} images at {LOAD_ADDR_5G:#010x}: ",
        dir.len()
    );
    let names: Vec<&str> = dir.iter().map(|e| e.tag.as_str()).collect();
    s.push_str(&names.join(" · "));
    if let Some(b) = build_string(path) {
        let _ = write!(s, "\n{b}");
    }
    Verdict::Good(s)
}

/// The bootloader's own build line, if it is there. **Reported, never judged on** — the prototype
/// dump and the retail dump differ here (Nov 28 2006 against Mar 10 2008) and both are 5G NORs, so
/// this identifies which dump you have without deciding anything.
fn build_string(path: &Path) -> Option<String> {
    let data = std::fs::read(path).ok()?;
    let needle = b"Bootloader UI Shell";
    let at = data.windows(needle.len()).position(|w| w == needle)?;
    let end = data[at..].iter().position(|&b| b == 0).unwrap_or(0) + at;
    let s = String::from_utf8_lossy(&data[at..end.min(at + 96)]).into_owned();
    Some(s.trim().to_string())
}

// ---------------------------------------------------------------- the drive image

/// Parse a drive image and say what it is.
///
/// 1. **The MBR** at LBA 0 — signature `0x55AA`, partition 0 of type `0x00`, which is Apple's
///    firmware partition. An Apple Partition Map (`ER` at byte 0) and a GPT are both recognised
///    and named, because "this is a Mac-formatted iPod" is a far more useful thing to be told
///    than "no MBR signature".
/// 2. **The `!ATA` directory** at partition start + `0x4200`.
/// 3. **An `osos` entry loading at `0x10000000`**, whose body is inside the file.
pub fn disk(path: &Path) -> Verdict {
    let len = match std::fs::metadata(path) {
        Ok(m) => m.len(),
        Err(e) => return Verdict::Bad(format!("cannot read this file: {e}")),
    };
    let mbr = match read_at(path, 0, 512) {
        Ok(b) => b,
        Err(e) => return Verdict::Bad(format!("cannot read the first sector: {e}")),
    };

    if &mbr[0..2] == b"ER" {
        return Verdict::Wrong(
            "this is an Apple Partition Map (`ER` at byte 0) — a Mac-formatted iPod. \
             The recipes here read an MBR: partition 0, type 0x00, at LBA 63. \
             A Windows-formatted iPod is the layout this emulator's images use."
                .into(),
        );
    }
    if mbr[510] != 0x55 || mbr[511] != 0xAA {
        return Verdict::Bad(format!(
            "no MBR signature in the first sector ({:#04x}{:02x}, expected 0x55aa). \
             This is not a whole-drive image — a partition image, a compressed file, or a firmware \
             bundle rather than a disk.",
            mbr[510], mbr[511]
        ));
    }
    if read_at(path, 512, 8).map(|b| b == *b"EFI PART").unwrap_or(false) {
        return Verdict::Wrong(
            "the MBR is a GPT protective MBR (`EFI PART` at LBA 1). \
             The iPod's own layout is a plain MBR with the firmware partition first."
                .into(),
        );
    }

    // Partition 0: 16 bytes at 0x1be. +0x04 type, +0x08 first LBA, +0x0c sector count.
    let p = &mbr[0x1be..0x1be + 16];
    let ptype = p[4];
    let lba = u32::from_le_bytes([p[8], p[9], p[10], p[11]]) as u64;
    let sectors = u32::from_le_bytes([p[12], p[13], p[14], p[15]]) as u64;
    if ptype != 0x00 || lba == 0 {
        return Verdict::Wrong(format!(
            "no firmware partition found — MBR partition 1 is type {ptype:#04x} starting at LBA \
             {lba}, where Apple's is type 0x00 at LBA 63. This drive may have been reformatted by \
             a PC, which wipes the firmware partition and leaves the data one. \
             You do not need to find the original: build a fresh disk from an IPSW instead — \
             `ipod-boot make-disk`, or the setup screen's IPSW slot."
        ));
    }

    let dir = match read_at(path, lba * 512 + DISK_DIRECTORY, 0x200) {
        Ok(b) => parse_entries(&b, b"!ATA"),
        Err(e) => return Verdict::Bad(format!("cannot read the firmware directory: {e}")),
    };
    if dir.is_empty() {
        return Verdict::Wrong(format!(
            "no firmware partition found — there is one at LBA {lba} ({sectors} sectors) but no \
             `!ATA` directory at partition + {DISK_DIRECTORY:#x}, so it is empty. \
             This drive may have been reformatted. Build a fresh disk from an IPSW instead — \
             `ipod-boot make-disk`, or the setup screen's IPSW slot."
        ));
    }
    let Some(osos) = dir.iter().find(|e| e.tag == "osos") else {
        let names: Vec<&str> = dir.iter().map(|e| e.tag.as_str()).collect();
        return Verdict::Wrong(format!(
            "the `!ATA` directory lists {} but no `osos`. \
             `osos` is the OS image the bootloader loads; without it there is nothing to boot.",
            names.join(" · ")
        ));
    };
    if osos.addr != LOAD_ADDR_5G {
        return Verdict::Wrong(format!(
            "this is not a 5G/5.5G image: `osos` loads at {:#010x}, not {LOAD_ADDR_5G:#010x}. \
             A 5G/5.5G loads its OS at {LOAD_ADDR_5G:#010x} — a 6G Classic and the nanos do not. \
             This emulator is 5G/5.5G (PP5021C) only.",
            osos.addr
        ));
    }
    let body_end = lba * 512 + osos.offset as u64 + osos.len as u64;
    if body_end > len {
        return Verdict::Bad(format!(
            "`osos` claims {} ending at byte {body_end}, past the end of a {} file. \
             The image is truncated — a partial copy, or a download that stopped.",
            bytes(osos.len as u64),
            bytes(len)
        ));
    }

    let mut s = format!(
        "{}, MBR, firmware partition at LBA {lba} ({} sectors). \
         `osos` {} loading at {:#010x}",
        bytes(len),
        sectors,
        bytes(osos.len as u64),
        osos.addr
    );
    let others: Vec<String> = dir
        .iter()
        .filter(|e| e.tag != "osos")
        .map(|e| format!("{} {}", e.tag, bytes(e.len as u64)))
        .collect();
    if !others.is_empty() {
        let _ = write!(s, ", plus {}", others.join(" · "));
    }
    // `aupd` present and unmarked means the ROM will run Apple's flash updater instead of the OS —
    // correct behaviour, a genuinely different boot, and not what somebody expecting a main menu
    // is about to see. `flash-update.sh` is the recipe for it.
    if let Some(aupd) = dir.iter().find(|e| e.tag == "aupd") {
        if aupd.dev == 0 {
            let _ = write!(
                s,
                "\nNote: `aupd` is present and not yet marked done, so the bootloader will run \
                 Apple's FLASH UPDATER on the first boot rather than the OS. That is correct \
                 behaviour and it takes two boots — `ipod-boot flash-update` is the recipe."
            );
        }
    }
    Verdict::Good(s)
}

// ---------------------------------------------------------------- the IPSW, and the disk it makes

/// Parse an IPSW and say whether its firmware partition is a 5G/5.5G one.
///
/// The same [`Verdict`] the two image checks produce, so the setup screen has one shape to draw.
/// The work is [`eapp_loader::ipsw`]'s: a zip reader, an inflate, a CRC-32 check, and the `!ATA`
/// directory out of the extracted bundle.
pub fn ipsw(path: &Path) -> Verdict {
    match crate::ipsw::inspect(path) {
        crate::ipsw::Ipsw::Good(s, _) => Verdict::Good(s),
        crate::ipsw::Ipsw::Wrong(s) => Verdict::Wrong(s),
        crate::ipsw::Ipsw::Bad(s) => Verdict::Bad(s),
    }
}

/// Build a drive image from an IPSW, at `out`. Returns what to say about it.
pub fn build_from_ipsw(src: &Path, out: &Path) -> Result<String, String> {
    let fw = match crate::ipsw::inspect(src) {
        crate::ipsw::Ipsw::Good(_, fw) => fw,
        crate::ipsw::Ipsw::Wrong(s) | crate::ipsw::Ipsw::Bad(s) => return Err(s),
    };
    if let Some(d) = out.parent() {
        let _ = std::fs::create_dir_all(d);
    }
    crate::ipsw::build_disk(&fw, out, crate::ipsw::DEFAULT_SECTORS)?;
    Ok(format!(
        "built {} — 8 GiB, sparse, about 20 MB on disk. Apple's firmware partition byte for byte, \
         and an empty FAT32 volume that RetailOS populates itself on first boot.",
        out.display()
    ))
}

fn bytes(n: u64) -> String {
    if n >= 1 << 30 {
        format!("{:.2} GiB", n as f64 / (1u64 << 30) as f64)
    } else if n >= 1 << 20 {
        format!("{:.2} MiB", n as f64 / (1u64 << 20) as f64)
    } else {
        format!("{n} bytes")
    }
}

/// `ipod-gui --check-images`: the same two reports, on stdout, with no window.
///
/// So that "does my dump work" is answerable on a machine with no display, in CI, and over SSH —
/// and so the README can tell somebody to run one command rather than describing a dialog.
pub fn report(flash_path: &Path, disk_path: &Path) -> i32 {
    let mut bad = 0;
    for (what, p, v) in [
        ("NOR dump  ", flash_path, flash(flash_path)),
        ("disk image", disk_path, disk(disk_path)),
    ] {
        let mark = match v {
            Verdict::Good(_) => "OK  ",
            Verdict::Wrong(_) => "NOT THIS MACHINE",
            Verdict::Bad(_) => "UNREADABLE",
        };
        println!("{what}  {mark}  {}", p.display());
        for line in v.text().lines() {
            println!("             {line}");
        }
        if !v.ok() {
            bad += 1;
        }
    }
    if bad == 0 {
        0
    } else {
        1
    }
}

#[cfg(test)]
mod naming_tests {
    use super::*;

    /// A built drive is named for the software in it, and two different bundles cannot collide.
    ///
    /// **The bug this closes:** every build landed on one path, `ipod-from-ipsw.img`. Building
    /// from a second bundle overwrote the first in place — no prompt, no rename, and the path an
    /// iPod had been booting from now held a different iPod's software under a name that still
    /// looked right. The version makes the folder readable; the CRC makes the collision impossible.
    #[test]
    fn a_built_drive_is_named_for_what_is_in_it() {
        let a = built_drive_name("20.6.3", 0xdead_beef);
        assert_eq!(a, "ipod-20.6.3-deadbeef.img");

        // Same bundle, same name — so a rebuild resolves to the file that already exists instead
        // of spending eight gigabytes proving it is identical.
        assert_eq!(a, built_drive_name("20.6.3", 0xdead_beef));
        // Different contents at the same version, and different versions, both get their own file.
        assert_ne!(a, built_drive_name("20.6.3", 0x0000_0001));
        assert_ne!(a, built_drive_name("24.1.1", 0xdead_beef));
        // Padded, so the names sort and read as a fixed shape rather than a ragged one.
        assert_eq!(built_drive_name("20.6.3", 1), "ipod-20.6.3-00000001.img");
    }

    /// The name survives the round trip, and nothing else is mistaken for one.
    #[test]
    fn a_drive_describes_itself_only_when_we_named_it() {
        let p = std::path::PathBuf::from("/x/y").join(built_drive_name("20.6.3", 0xabcd_1234));
        assert_eq!(describe_drive(&p).as_deref(), Some("iPod software 20.6.3"));

        // A drive the user brought has no description here, and must not be given a wrong one:
        // the firmware version lives inside the partition and this function does not read it.
        for other in [
            "/x/my-ipod-backup.img",
            "/x/ipod.img",
            "/x/ipod-.img",              // no version
            "/x/ipod-20.6.3.img",        // no fingerprint
            "/x/ipod-20.6.3-zzzzzzzz.img", // not hex
            "/x/ipod-20.6.3-abcd.img",   // too short to be one
        ] {
            assert_eq!(
                describe_drive(std::path::Path::new(other)),
                None,
                "{other} must fall back to its filename"
            );
        }
    }

    /// A mismatched pair is caught, a matching one is silent, and an unknown one is silent too.
    ///
    /// The third is the design decision: a drive somebody supplied has no family this can read, and
    /// a warning that fired on every such drive would be noise that teaches people to ignore the
    /// one that matters.
    #[test]
    fn a_pair_from_two_different_ipods_is_named_before_the_boot() {
        assert_eq!(family_mismatch("iPod Video", 20, Some(20)), None, "the matching pair");
        assert_eq!(family_mismatch("iPod Video", 20, None), None, "not knowing is not objecting");

        let m = family_mismatch("iPod Video", 20, Some(24)).expect("24 against 20 is a mismatch");
        assert!(m.contains("24") && m.contains("20"), "both numbers, so it can be acted on: {m}");
        // The article trap: "a iPod Video" shipped once. There is no article at all now, because
        // the fix for one model is not the fix for the next one.
        assert!(!m.contains(" a iPod"), "grammar: {m}");
        assert!(!m.contains(" an iPod"), "no article, rather than the right article: {m}");
    }

    /// A ROM is described by the device it came off, not by the name it was uploaded under.
    ///
    /// The fixture is a `SysCfg` block built to the layout `syscfg` parses, because the point of
    /// the description is that it reads bytes rather than a filename — and a test that fed it a
    /// filename would be testing nothing.
    #[test]
    fn a_rom_is_described_by_the_ipod_it_came_off() {
        let dir = std::env::temp_dir().join(format!("ipod-describe-{}", std::process::id()));
        let _ = std::fs::create_dir_all(&dir);

        // No SysCfg at all: nothing honest to say, so nothing is said.
        let blank = dir.join("blank.bin");
        std::fs::write(&blank, vec![0u8; NOR_LEN as usize]).unwrap();
        assert_eq!(describe_rom(&blank, "iPod Video"), None);

        // A dump carrying a serial is named by its tail — the leading characters are the factory
        // and the model and are identical across every iPod of a kind.
        let mut nor = vec![0u8; NOR_LEN as usize];
        write_syscfg(&mut nor, Some("7Q7411K2VQK"), None);
        let with_serial = dir.join("serial.bin");
        std::fs::write(&with_serial, &nor).unwrap();
        assert_eq!(
            describe_rom(&with_serial, "iPod Video").as_deref(),
            Some("iPod Video · K2VQK"),
            "the distinguishing part of a serial is its tail"
        );

        // Wrong length is not a ROM, whatever is inside it.
        let short = dir.join("short.bin");
        std::fs::write(&short, vec![0u8; 4096]).unwrap();
        assert_eq!(describe_rom(&short, "iPod Video"), None);

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Write a `SysCfg` block into a NOR image, in the layout [`syscfg`] reads.
    fn write_syscfg(nor: &mut [u8], serial: Option<&str>, guid: Option<u64>) {
        let block = &mut nor[SYSCFG_AT as usize..];
        block[..4].copy_from_slice(SYSCFG_MAGIC);
        let mut count = 0u32;
        let mut at = SYSCFG_HEADER;
        if let Some(s) = serial {
            // The tag is a little-endian u32 of four characters, so it goes in backwards.
            block[at..at + 4].copy_from_slice(b"mNrS");
            let bytes = s.as_bytes();
            block[at + 4..at + 4 + bytes.len()].copy_from_slice(bytes);
            at += SYSCFG_RECORD;
            count += 1;
        }
        if let Some(g) = guid {
            block[at..at + 4].copy_from_slice(b"dIwF");
            block[at + 8..at + 12].copy_from_slice(&((g & 0xffff_ffff) as u32).to_le_bytes());
            block[at + 12..at + 16].copy_from_slice(&((g >> 32) as u32).to_le_bytes());
            count += 1;
        }
        block[0x14..0x18].copy_from_slice(&count.to_le_bytes());
    }
}

#[cfg(test)]
mod classify_tests {
    use super::*;

    /// A file of `n` bytes beginning with `head`, in a directory this test owns.
    fn file(dir: &Path, name: &str, head: &[u8], n: usize) -> std::path::PathBuf {
        let p = dir.join(name);
        let mut body = vec![0u8; n];
        body[..head.len()].copy_from_slice(head);
        std::fs::write(&p, &body).unwrap();
        p
    }

    /// **The first four bytes of each real file**, read off this project's own resources.
    ///
    /// The same rule the magic test below follows, for the same reason: the whole value of routing
    /// a dropped file by content is that the content is what it says it is, and a fixture invented
    /// by the author of the router proves only that the author is self-consistent.
    ///
    /// ```text
    ///   iPod_20.1.3.zip                    50 4b 03 04    "PK\x03\x04"
    ///   internal_rom_000000-0FFFFF.bin     fe 1f 00 ea    0xea001ffe, the ARM branch
    ///   ipod8g-retail.img                  00 00 00 00    an MBR begins with a jump it does not need
    /// ```
    ///
    /// The drive's opening word is the one worth noticing: it is **nothing at all**, which is why
    /// the drive cannot be recognised from its head and is instead what a file is when it is not
    /// one of the other two.
    #[test]
    fn each_file_is_recognised_by_what_it_actually_starts_with() {
        let dir = std::env::temp_dir().join(format!("ipod-classify-{}", std::process::id()));
        let _ = std::fs::create_dir_all(&dir);

        let rom = file(&dir, "rom.bin", &[0xfe, 0x1f, 0x00, 0xea], NOR_LEN as usize);
        let ipsw = file(&dir, "sw.ipsw", b"PK\x03\x04", 4096);
        let drive = file(&dir, "d.img", &[0, 0, 0, 0], 4 * 1024 * 1024);

        assert_eq!(classify(&rom), Kind::Rom, "exactly 1 MiB is the boot ROM");
        assert_eq!(classify(&ipsw), Kind::Ipsw, "a zip is Apple's bundle");
        assert_eq!(classify(&drive), Kind::Disk, "anything else large enough is a drive");

        // The extension is not consulted, and must not be: these files are handed around under
        // every name imaginable, and `internal_rom_000000-0FFFFF.bin` is a convention rather than
        // a rule. A drive named `.bin` is still a drive.
        let lying = file(&dir, "definitely-a-rom.bin", b"PK\x03\x04", 4096);
        assert_eq!(classify(&lying), Kind::Ipsw, "contents win over the name");

        // A 1 MiB zip is a zip. Order matters, and this is the pair that proves which way.
        let zip_1mib = file(&dir, "big.ipsw", b"PK\x03\x04", NOR_LEN as usize);
        assert_eq!(classify(&zip_1mib), Kind::Ipsw, "the zip test runs before the length test");

        // Too small to be any of them, which the screen says with its size rather than by handing
        // it to a parser and reporting whatever that parser makes of twelve bytes.
        let tiny = file(&dir, "note.txt", b"hello", 12);
        assert_eq!(classify(&tiny), Kind::Unknown);

        // Neither a directory nor an absent path is a file, and neither may panic: both are things
        // a drag-and-drop can deliver.
        assert_eq!(classify(&dir), Kind::Unknown, "a dropped folder");
        assert_eq!(classify(&dir.join("nothing-here")), Kind::Unknown);

        let _ = std::fs::remove_dir_all(&dir);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(magic: &[u8; 4], tag: &str, dev: u32, off: u32, len: u32, addr: u32) -> Vec<u8> {
        let mut e = vec![0u8; 40];
        e[0..4].copy_from_slice(magic);
        // Stored little-endian, so the characters go in backwards — the property that makes `osos`
        // read as `soso` in a byte dump, and the one a hand-written test gets wrong first.
        for (i, c) in tag.bytes().rev().enumerate() {
            e[4 + i] = c;
        }
        e[8..12].copy_from_slice(&dev.to_le_bytes());
        e[0x0c..0x10].copy_from_slice(&off.to_le_bytes());
        e[0x10..0x14].copy_from_slice(&len.to_le_bytes());
        e[0x14..0x18].copy_from_slice(&addr.to_le_bytes());
        e
    }

    /// **The bytes off the real images, copied out of a hex dump**, not synthesised by the helper
    /// below.
    ///
    /// This is the test that would have caught the bug this file shipped for an hour: the drive's
    /// magic reads `!ATA` left to right and the NOR's reads `hslf`, and comparing the drive's
    /// against `ATA!` made the project's own reference image report "no firmware partition found".
    /// Every synthetic test passed throughout, because the helper wrote the same wrong magic. A
    /// parser's fixtures have to come from the format, not from the parser's author.
    #[test]
    fn the_two_magics_are_the_bytes_the_real_images_carry() {
        // resources/drives/ipod8g-retail.img at 63*512 + 0x4200 — the `osos` record.
        let disk: [u8; 40] = [
            0x21, 0x41, 0x54, 0x41, 0x73, 0x6f, 0x73, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44,
            0x00, 0x00, 0x00, 0x5a, 0x73, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
            0xf3, 0x48, 0x7c, 0x2c, 0x12, 0xb0, 0x00, 0x00, 0x08, 0x00, 0x7c, 0x18,
        ];
        let e = parse_entries(&disk, b"!ATA");
        assert_eq!(e.len(), 1, "the drive's magic is `!ATA`, left to right");
        assert_eq!(e[0].tag, "osos");
        assert_eq!(e[0].len, 0x0073_5a00, "7 559 680 bytes — the RetailOS image");
        assert_eq!(e[0].addr, LOAD_ADDR_5G);
        assert!(parse_entries(&disk, b"ATA!").is_empty(), "and it is NOT `ATA!`");

        // The retail NOR at 0xffe00 — the `disk` record.
        let nor: [u8; 40] = [
            0x68, 0x73, 0x6c, 0x66, 0x6b, 0x73, 0x69, 0x64, 0x00, 0x00, 0x00, 0x00, 0xd0, 0x3b,
            0x0d, 0x00, 0x30, 0xc2, 0x02, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
            0x72, 0x8c, 0x19, 0x01, 0x12, 0xb0, 0x00, 0x00, 0x78, 0x08, 0x6b, 0x00,
        ];
        let e = parse_entries(&nor, b"hslf");
        assert_eq!(e.len(), 1, "the NOR's magic is `flsh` stored little-endian");
        assert_eq!(e[0].tag, "disk");
        assert_eq!(e[0].addr, LOAD_ADDR_5G);
        assert!(parse_entries(&nor, b"flsh").is_empty(), "and it is NOT `flsh`");
    }

    #[test]
    fn the_tag_is_a_little_endian_word_not_a_string() {
        let e = entry(b"!ATA", "osos", 0, 0x4400, 0x735a00, LOAD_ADDR_5G);
        assert_eq!(&e[4..8], b"soso", "the bytes on disk read backwards");
        let parsed = parse_entries(&e, b"!ATA");
        assert_eq!(parsed[0].tag, "osos");
        assert_eq!(parsed[0].len, 0x735a00);
        assert_eq!(parsed[0].addr, LOAD_ADDR_5G);
    }

    /// The directory ends at the first record whose magic is absent — it is not a counted list.
    #[test]
    fn parsing_stops_at_the_first_record_without_the_magic() {
        let mut buf = entry(b"!ATA", "osos", 0, 0x4400, 16, LOAD_ADDR_5G);
        buf.extend(entry(b"!ATA", "rsrc", 0, 0x73a000, 32, LOAD_ADDR_5G));
        buf.extend(vec![0u8; 40]);
        buf.extend(entry(b"!ATA", "aupd", 0, 0x900000, 8, LOAD_ADDR_5G));
        let e = parse_entries(&buf, b"!ATA");
        assert_eq!(e.len(), 2, "the zeroed record terminates the directory");
        assert_eq!(e[1].tag, "rsrc");
    }

    /// `tag` names the file. Named rather than derived from the arguments, because the first
    /// version hashed them into a filename, two tests hashed to the same one, and the parallel
    /// test runner had them deleting each other's images mid-read.
    fn synthetic_disk(tag: &str, dir: &[u8], total: u64, ptype: u8, lba: u32) -> std::path::PathBuf {
        let p = std::env::temp_dir()
            .join(format!("ipod-gui-inspect-{tag}-{}.img", std::process::id()));
        let mut img = vec![0u8; total as usize];
        img[510] = 0x55;
        img[511] = 0xAA;
        img[0x1be + 4] = ptype;
        img[0x1be + 8..0x1be + 12].copy_from_slice(&lba.to_le_bytes());
        img[0x1be + 12..0x1be + 16].copy_from_slice(&27140u32.to_le_bytes());
        let at = lba as usize * 512 + DISK_DIRECTORY as usize;
        img[at..at + dir.len()].copy_from_slice(dir);
        std::fs::write(&p, &img).unwrap();
        p
    }

    #[test]
    fn a_well_formed_disk_is_recognised_and_described() {
        let dir = entry(b"!ATA", "osos", 0, 0x4400, 4096, LOAD_ADDR_5G);
        let p = synthetic_disk("good", &dir, 1 << 21, 0x00, 63);
        let v = disk(&p);
        assert!(v.ok(), "{v:?}");
        assert!(v.text().contains("LBA 63"), "{v:?}");
        assert!(v.text().contains("0x10000000"), "{v:?}");
        let _ = std::fs::remove_file(p);
    }

    /// The message a 6G Classic owner should get: not "invalid", but "this is not that machine".
    #[test]
    fn a_foreign_load_address_says_which_machine_this_is() {
        let dir = entry(b"!ATA", "osos", 0, 0x4400, 4096, 0x0800_0000);
        let p = synthetic_disk("foreign", &dir, 1 << 21, 0x00, 63);
        let v = disk(&p);
        assert!(matches!(v, Verdict::Wrong(_)), "{v:?}");
        assert!(v.text().contains("0x08000000"), "{v:?}");
        assert!(v.text().contains("5G/5.5G"), "{v:?}");
        let _ = std::fs::remove_file(p);
    }

    #[test]
    fn a_truncated_osos_body_is_reported_as_truncation() {
        let dir = entry(b"!ATA", "osos", 0, 0x4400, 0x0080_0000, LOAD_ADDR_5G);
        let p = synthetic_disk("truncated", &dir, 1 << 21, 0x00, 63);
        let v = disk(&p);
        assert!(matches!(v, Verdict::Bad(_)), "{v:?}");
        assert!(v.text().contains("truncated"), "{v:?}");
        let _ = std::fs::remove_file(p);
    }

    #[test]
    fn an_apple_partition_map_is_named_rather_than_called_invalid() {
        let p = std::env::temp_dir().join(format!("ipod-gui-apm-{}.img", std::process::id()));
        let mut img = vec![0u8; 4096];
        img[0] = b'E';
        img[1] = b'R';
        std::fs::write(&p, &img).unwrap();
        let v = disk(&p);
        assert!(matches!(v, Verdict::Wrong(_)), "{v:?}");
        assert!(v.text().contains("Apple Partition Map"), "{v:?}");
        let _ = std::fs::remove_file(p);
    }

    fn synthetic_nor(len: u64, word0: u32, dir: &[u8]) -> std::path::PathBuf {
        let p = std::env::temp_dir()
            .join(format!("ipod-gui-nor-{}-{len}-{word0:08x}.bin", std::process::id()));
        let mut rom = vec![0u8; len as usize];
        rom[0..4].copy_from_slice(&word0.to_le_bytes());
        if len as usize > NOR_DIRECTORY as usize + dir.len() {
            let at = NOR_DIRECTORY as usize;
            rom[at..at + dir.len()].copy_from_slice(dir);
        }
        std::fs::write(&p, &rom).unwrap();
        p
    }

    #[test]
    fn a_well_formed_nor_is_recognised() {
        let mut dir = entry(b"hslf", "disk", 0, 0xd3bd0, 0x2c230, LOAD_ADDR_5G);
        dir.extend(entry(b"hslf", "diag", 0, 0xbbda8, 0x17e28, LOAD_ADDR_5G));
        let p = synthetic_nor(NOR_LEN, 0xea00_1ffe, &dir);
        let v = flash(&p);
        assert!(v.ok(), "{v:?}");
        assert!(v.text().contains("disk"), "{v:?}");
        assert!(v.text().contains("diag"), "{v:?}");
        let _ = std::fs::remove_file(p);
    }

    /// The 2 MiB case is the one worth a sentence: it is somebody else's iPod, dumped correctly.
    #[test]
    fn a_two_megabyte_dump_is_told_it_is_a_different_ipod() {
        let p = synthetic_nor(2 * NOR_LEN, 0xea00_1ffe, &[]);
        let v = flash(&p);
        assert!(matches!(v, Verdict::Wrong(_)), "{v:?}");
        assert!(v.text().contains("1 MiB"), "{v:?}");
        let _ = std::fs::remove_file(p);
    }

    #[test]
    fn a_non_branch_reset_vector_is_refused() {
        let p = synthetic_nor(NOR_LEN, 0x0000_0000, &[]);
        let v = flash(&p);
        assert!(matches!(v, Verdict::Bad(_)), "{v:?}");
        assert!(v.text().contains("ARM branch"), "{v:?}");
        let _ = std::fs::remove_file(p);
    }

    /// The failure a real person reported (issue #2): Rockbox's dumper leaves a correctly named
    /// file with nothing in it. Measuring before reading is what lets us say that, instead of
    /// reporting the read error that measuring would have prevented.
    #[test]
    fn an_empty_dump_is_diagnosed_rather_than_read() {
        let p = std::env::temp_dir().join("ipod-gui-empty-dump.bin");
        std::fs::write(&p, []).unwrap();
        let v = flash(&p);
        assert!(matches!(v, Verdict::Bad(_)), "{v:?}");
        assert!(v.text().contains("empty"), "{v:?}");
        assert!(!v.text().contains("whole buffer"), "leaked the read error: {v:?}");
        let _ = std::fs::remove_file(p);
    }

    #[test]
    fn a_missing_file_says_so_instead_of_panicking() {
        let p = std::env::temp_dir().join("ipod-gui-definitely-not-here.bin");
        assert!(matches!(flash(&p), Verdict::Bad(_)));
        assert!(matches!(disk(&p), Verdict::Bad(_)));
    }
}

// ---------------------------------------------------------------- the NOR's own identity

/// Where `SysCfg` sits in a 5G/5.5G NOR dump, and its signature stored backwards.
///
/// Every tag in this block is a four-character code written little-endian, so a byte dump reads
/// them reversed: `SCfg` appears as `gfCS`, `SrNm` as `mNrS`, `FwId` as `dIwF`.
const SYSCFG_AT: usize = 0x4000;
const SYSCFG_MAGIC: &[u8; 4] = b"gfCS";
const SYSCFG_HEADER: usize = 0x18;
const SYSCFG_RECORD: usize = 0x14;

/// The identity the iPod this NOR came from presents to the world.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SysCfg {
    /// `SrNm` — the serial printed on the back of the case.
    pub serial: Option<String>,
    /// `FwId` — the FireWire GUID, which **is** the USB serial number a host sees.
    ///
    /// Stored as two little-endian `u32`s, low word first, so the canonical 16-hex-digit form is
    /// the high word followed by the low one. The top of the high word is Apple's FireWire OUI,
    /// `00:0A:27`, which is the cheapest check that this was parsed correctly at all.
    pub guid: Option<u64>,
    /// Every tag found, in order, for a dump that does not look like the others.
    pub tags: Vec<String>,
}

impl SysCfg {
    /// `000A270014EFE726` — the form iTunes, `SysInfo` and a USB descriptor all use.
    pub fn guid_hex(&self) -> Option<String> {
        self.guid.map(|g| format!("{g:016X}"))
    }

    /// Whether the GUID carries Apple's FireWire OUI. False means the parse is wrong, or the dump
    /// is not what it claims to be — either way it is not an identity to go presenting.
    pub fn guid_looks_apple(&self) -> bool {
        self.guid.is_some_and(|g| (g >> 40) == 0x00_0A_27)
    }
}

/// Read `SysCfg` out of a NOR dump.
///
/// This is what makes the flash identity usable outside the emulator: the authorisation work in
/// `ipod-usb` needs the GUID of the iPod whose NOR is being booted, because keys minted against
/// any other identity are keys this machine cannot present.
pub fn syscfg(nor: &[u8]) -> Option<SysCfg> {
    let block = nor.get(SYSCFG_AT..)?;
    if block.get(..4)? != SYSCFG_MAGIC {
        return None;
    }
    let count = u32::from_le_bytes(block.get(0x14..0x18)?.try_into().ok()?) as usize;

    let mut out = SysCfg { serial: None, guid: None, tags: Vec::new() };
    let mut at = SYSCFG_HEADER;
    // Bounded by the declared count and by the buffer, because a truncated dump is a normal thing
    // to be handed and must not be read past.
    for _ in 0..count.min(64) {
        let Some(rec) = block.get(at..at + SYSCFG_RECORD) else { break };
        let tag: String = rec[..4].iter().rev().map(|&b| b as char).collect();
        let payload = &rec[4..];
        match tag.as_str() {
            "SrNm" => {
                let s: String = payload
                    .iter()
                    .take_while(|&&b| b != 0)
                    .map(|&b| b as char)
                    .filter(|c| c.is_ascii_graphic())
                    .collect();
                if !s.is_empty() {
                    out.serial = Some(s);
                }
            }
            "FwId" => {
                let lo = u32::from_le_bytes(payload.get(4..8)?.try_into().ok()?) as u64;
                let hi = u32::from_le_bytes(payload.get(8..12)?.try_into().ok()?) as u64;
                out.guid = Some((hi << 32) | lo);
            }
            _ => {}
        }
        out.tags.push(tag);
        at += SYSCFG_RECORD;
    }
    Some(out)
}

#[cfg(test)]
mod syscfg_tests {
    use super::*;

    /// Build a `SysCfg` block with the byte layout a real 5.5G NOR has, dumped at 0x4000:
    /// `gfCS`, five header words, a record count, then 20-byte records of a backwards tag and a
    /// 16-byte payload. The identities here are invented — the shape is what is under test.
    fn nor(records: &[(&str, [u8; 16])]) -> Vec<u8> {
        let mut v = vec![0u8; SYSCFG_AT + SYSCFG_HEADER + records.len() * SYSCFG_RECORD + 16];
        let b = SYSCFG_AT;
        v[b..b + 4].copy_from_slice(SYSCFG_MAGIC);
        v[b + 0x14..b + 0x18].copy_from_slice(&(records.len() as u32).to_le_bytes());
        for (i, (tag, payload)) in records.iter().enumerate() {
            let at = b + SYSCFG_HEADER + i * SYSCFG_RECORD;
            let t: Vec<u8> = tag.bytes().rev().collect();
            v[at..at + 4].copy_from_slice(&t);
            v[at + 4..at + 20].copy_from_slice(payload);
        }
        v
    }

    fn fwid(lo: u32, hi: u32) -> [u8; 16] {
        let mut p = [0u8; 16];
        p[4..8].copy_from_slice(&lo.to_le_bytes());
        p[8..12].copy_from_slice(&hi.to_le_bytes());
        p
    }

    fn srnm(s: &str) -> [u8; 16] {
        let mut p = [0u8; 16];
        p[..s.len()].copy_from_slice(s.as_bytes());
        p
    }

    /// The GUID is two little-endian words, high one second — so it reads backwards twice over,
    /// and getting either wrong yields a plausible-looking number that is not the device's.
    #[test]
    fn the_guid_is_the_high_word_then_the_low_one() {
        let v = nor(&[("SrNm", srnm("AB123CD4EFG")), ("FwId", fwid(0x1234_5678, 0x000A_2700))]);
        let c = syscfg(&v).expect("a well-formed block must parse");
        assert_eq!(c.serial.as_deref(), Some("AB123CD4EFG"));
        assert_eq!(c.guid_hex().as_deref(), Some("000A270012345678"));
        assert!(c.guid_looks_apple(), "00:0A:27 is Apple's FireWire OUI");
        assert_eq!(c.tags, ["SrNm", "FwId"]);
    }

    /// The OUI check is the cheapest proof the parse is right, so it has to be able to fail.
    #[test]
    fn a_guid_without_apples_oui_is_flagged() {
        let v = nor(&[("FwId", fwid(0x1111_2222, 0xDEAD_BEEF))]);
        assert!(!syscfg(&v).unwrap().guid_looks_apple());
    }

    /// A NOR that is not one, and a dump cut short: both are ordinary things to be handed.
    #[test]
    fn rubbish_and_truncation_are_declined_not_panicked_on() {
        assert!(syscfg(&[0u8; 0x5000]).is_none(), "no magic, no SysCfg");
        assert!(syscfg(&[]).is_none());
        let mut v = nor(&[("SrNm", srnm("AB123CD4EFG")), ("FwId", fwid(1, 2))]);
        v.truncate(SYSCFG_AT + SYSCFG_HEADER + 8);
        let c = syscfg(&v).expect("the header is intact, so the block is still readable");
        assert_eq!(c.guid, None, "a record cut in half must not be invented");
    }

    /// A count larger than the block does not walk off the end.
    #[test]
    fn a_lying_record_count_is_bounded() {
        let mut v = nor(&[("FwId", fwid(1, 0x000A_2700))]);
        v[SYSCFG_AT + 0x14..SYSCFG_AT + 0x18].copy_from_slice(&9999u32.to_le_bytes());
        let c = syscfg(&v).expect("must not panic");
        assert!(c.tags.len() < 64);
    }
}
