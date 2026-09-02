//! Build a bootable iPod drive image out of an IPSW, so nobody has to supply somebody else's disk.
//!
//! # Why this is the front door
//!
//! The only thing a drive image *must* carry is the firmware partition — `osos` and `rsrc`.
//! Everything else RetailOS builds itself on first boot: measured, it writes 41 sectors, creates
//! `Contacts`, `Calendars`, `Notes` and `iPod_Control/Device/Accessories`, writes two placeholder
//! vCards, and deletes `IC-Info.sid`. It bootstraps its own volume.
//!
//! And the firmware partition is exactly what an IPSW contains: `Firmware-20.6.3` inside
//! `iPod_20.1.3.ipsw` is 13 895 680 bytes, which is 27 140 sectors, which is precisely the size of
//! MBR partition 0 on a real iPod. It fits with nothing left over — that is how the offset was
//! known in the first place, and `tools/ipod-boot/flash-update.sh` has been writing it into a disk
//! with `dd … seek=63 conv=notrunc` since before this module existed.
//!
//! So the supply burden drops from **8 GB of somebody's iPod to 14 MB of Apple's firmware**, and
//! three problems go with it: no third-party serial number or FireWire GUID, no stranger's music
//! library, and no mismatch between a NOR that belonged to one machine and a drive that belonged to
//! another. It is also what iTunes does when it restores a device, so it is not a workaround.
//!
//! `--disk=` is untouched and still the way every number in `research/` was measured.
//!
//! # No dependencies, and what that costs
//!
//! An IPSW is a zip, and its members are deflated, so this file carries an inflate. That is the
//! price of `eapp-loader` having one dependency (`arm7tdmi`, a path) and the README's claim that
//! the core crates build with no third-party code. It is not a guess at correctness either: every
//! member carries a CRC-32 and [`Zip::extract`] checks it, so a wrong bit anywhere in the inflate
//! is a hard error on the very first real file rather than a subtly corrupt firmware partition.

use std::io::{Seek, SeekFrom, Write};
use std::path::Path;

// ---------------------------------------------------------------- CRC-32, and the zip's own check

/// CRC-32/ISO-HDLC — the one zip stores per member, and the one that makes the inflate below
/// self-checking rather than merely plausible.
pub fn crc32(data: &[u8]) -> u32 {
    let mut table = [0u32; 256];
    for (i, e) in table.iter_mut().enumerate() {
        let mut c = i as u32;
        for _ in 0..8 {
            c = if c & 1 != 0 { 0xEDB8_8320 ^ (c >> 1) } else { c >> 1 };
        }
        *e = c;
    }
    let mut crc = 0xFFFF_FFFFu32;
    for &b in data {
        crc = table[((crc ^ b as u32) & 0xff) as usize] ^ (crc >> 8);
    }
    !crc
}

// ---------------------------------------------------------------- DEFLATE

/// Canonical Huffman decoding table, built from a list of code lengths — RFC 1951 §3.2.2.
struct Huffman {
    /// `counts[n]` = how many codes are `n` bits long.
    counts: [u16; 16],
    /// Symbols, ordered by code length then by symbol.
    symbols: Vec<u16>,
}

impl Huffman {
    fn new(lengths: &[u8]) -> Result<Huffman, String> {
        let mut counts = [0u16; 16];
        for &l in lengths {
            counts[l as usize] += 1;
        }
        counts[0] = 0;
        let mut offs = [0u16; 16];
        for i in 1..16 {
            offs[i] = offs[i - 1] + counts[i - 1];
        }
        let mut symbols = vec![0u16; lengths.len()];
        for (sym, &l) in lengths.iter().enumerate() {
            if l != 0 {
                symbols[offs[l as usize] as usize] = sym as u16;
                offs[l as usize] += 1;
            }
        }
        Ok(Huffman { counts, symbols })
    }
}

struct BitReader<'a> {
    data: &'a [u8],
    pos: usize,
    bit: u32,
    acc: u32,
}

impl<'a> BitReader<'a> {
    fn new(data: &'a [u8]) -> Self {
        BitReader { data, pos: 0, bit: 0, acc: 0 }
    }

    fn bits(&mut self, n: u32) -> Result<u32, String> {
        while self.bit < n {
            let b = *self.data.get(self.pos).ok_or("deflate: ran off the end of the stream")?;
            self.pos += 1;
            self.acc |= (b as u32) << self.bit;
            self.bit += 8;
        }
        let v = self.acc & ((1u32 << n) - 1).max(if n == 0 { 0 } else { 0 });
        let v = if n == 0 { 0 } else { v };
        self.acc >>= n;
        self.bit -= n;
        Ok(v)
    }

    /// Huffman codes are packed most-significant-bit-first within the code but the stream is read
    /// least-significant-bit-first, so the code is accumulated one bit at a time. RFC 1951 §3.1.1.
    fn decode(&mut self, h: &Huffman) -> Result<u16, String> {
        let (mut code, mut first, mut index) = (0i32, 0i32, 0i32);
        for len in 1..16 {
            code |= self.bits(1)? as i32;
            let count = h.counts[len] as i32;
            if code - count < first {
                return Ok(h.symbols[(index + (code - first)) as usize]);
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        Err("deflate: a code longer than 15 bits".into())
    }

    fn align(&mut self) {
        self.acc = 0;
        self.bit = 0;
    }
}

const LEN_BASE: [u16; 29] = [
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
    163, 195, 227, 258,
];
const LEN_EXTRA: [u8; 29] = [
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
];
const DIST_BASE: [u16; 30] = [
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
];
const DIST_EXTRA: [u8; 30] = [
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13,
    13,
];

/// Raw DEFLATE (RFC 1951) — no zlib header, which is what a zip member holds.
///
/// `expect` is the uncompressed size the zip's central directory declares; it is used to size the
/// output up front and as a bound, so a malformed stream cannot make this allocate without limit.
pub fn inflate(data: &[u8], expect: usize) -> Result<Vec<u8>, String> {
    let mut out: Vec<u8> = Vec::with_capacity(expect);
    let mut br = BitReader::new(data);
    loop {
        let last = br.bits(1)?;
        match br.bits(2)? {
            // Stored: byte-aligned, a length and its complement, then the bytes.
            0 => {
                br.align();
                let p = br.pos;
                let len = u16::from_le_bytes([
                    *data.get(p).ok_or("deflate: truncated stored block")?,
                    *data.get(p + 1).ok_or("deflate: truncated stored block")?,
                ]) as usize;
                let nlen = u16::from_le_bytes([
                    *data.get(p + 2).ok_or("deflate: truncated stored block")?,
                    *data.get(p + 3).ok_or("deflate: truncated stored block")?,
                ]);
                if len as u16 != !nlen {
                    return Err("deflate: stored block length does not match its complement".into());
                }
                let s = p + 4;
                let e = s + len;
                out.extend_from_slice(data.get(s..e).ok_or("deflate: truncated stored block")?);
                br.pos = e;
            }
            1 => {
                let (lit, dist) = fixed_tables()?;
                block(&mut br, &lit, &dist, &mut out, expect)?;
            }
            2 => {
                let (lit, dist) = dynamic_tables(&mut br)?;
                block(&mut br, &lit, &dist, &mut out, expect)?;
            }
            _ => return Err("deflate: reserved block type".into()),
        }
        if last == 1 {
            return Ok(out);
        }
    }
}

fn fixed_tables() -> Result<(Huffman, Huffman), String> {
    let mut l = [0u8; 288];
    for (i, e) in l.iter_mut().enumerate() {
        *e = match i {
            0..=143 => 8,
            144..=255 => 9,
            256..=279 => 7,
            _ => 8,
        };
    }
    Ok((Huffman::new(&l)?, Huffman::new(&[5u8; 30])?))
}

fn dynamic_tables(br: &mut BitReader) -> Result<(Huffman, Huffman), String> {
    const ORDER: [usize; 19] = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];
    let hlit = br.bits(5)? as usize + 257;
    let hdist = br.bits(5)? as usize + 1;
    let hclen = br.bits(4)? as usize + 4;
    let mut cl = [0u8; 19];
    for i in 0..hclen {
        cl[ORDER[i]] = br.bits(3)? as u8;
    }
    let clh = Huffman::new(&cl)?;

    let mut lengths = vec![0u8; hlit + hdist];
    let mut i = 0;
    while i < lengths.len() {
        let sym = br.decode(&clh)?;
        match sym {
            0..=15 => {
                lengths[i] = sym as u8;
                i += 1;
            }
            16 => {
                let prev = *lengths.get(i.wrapping_sub(1)).ok_or("deflate: repeat with no previous length")?;
                let n = 3 + br.bits(2)? as usize;
                for _ in 0..n {
                    *lengths.get_mut(i).ok_or("deflate: length repeat overruns")? = prev;
                    i += 1;
                }
            }
            17 => i += 3 + br.bits(3)? as usize,
            18 => i += 11 + br.bits(7)? as usize,
            _ => return Err("deflate: bad code-length symbol".into()),
        }
    }
    if i > lengths.len() {
        return Err("deflate: code lengths overrun their table".into());
    }
    Ok((Huffman::new(&lengths[..hlit])?, Huffman::new(&lengths[hlit..])?))
}

fn block(
    br: &mut BitReader,
    lit: &Huffman,
    dist: &Huffman,
    out: &mut Vec<u8>,
    limit: usize,
) -> Result<(), String> {
    loop {
        let sym = br.decode(lit)?;
        match sym {
            0..=255 => out.push(sym as u8),
            256 => return Ok(()),
            257..=285 => {
                let i = sym as usize - 257;
                let len = LEN_BASE[i] as usize + br.bits(LEN_EXTRA[i] as u32)? as usize;
                let d = br.decode(dist)? as usize;
                if d >= 30 {
                    return Err("deflate: distance symbol out of range".into());
                }
                let back = DIST_BASE[d] as usize + br.bits(DIST_EXTRA[d] as u32)? as usize;
                if back > out.len() {
                    return Err("deflate: back-reference before the start of the output".into());
                }
                // Byte by byte on purpose: overlapping copies are legal and are how run-length
                // encoding falls out of LZ77.
                let start = out.len() - back;
                for k in 0..len {
                    let b = out[start + k];
                    out.push(b);
                }
            }
            _ => return Err("deflate: literal/length symbol out of range".into()),
        }
        // `limit` is the declared uncompressed size. A stream that exceeds it is malformed, and
        // without this check a crafted one could exhaust memory.
        if out.len() > limit + 1 {
            return Err("deflate: output is longer than the archive says it should be".into());
        }
    }
}

// ---------------------------------------------------------------- the zip container

/// One member of the archive.
#[derive(Clone, Debug)]
pub struct Member {
    pub name: String,
    pub method: u16,
    pub packed: u64,
    pub size: u64,
    pub crc: u32,
    /// Offset of the local file header.
    header: u64,
}

pub struct Zip {
    data: Vec<u8>,
    pub members: Vec<Member>,
}

fn le16(d: &[u8], at: usize) -> u16 {
    u16::from_le_bytes([d[at], d[at + 1]])
}
fn le32(d: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([d[at], d[at + 1], d[at + 2], d[at + 3]])
}

impl Zip {
    /// Read the central directory. An IPSW is two members and a few hundred bytes of directory, so
    /// the whole file is read into memory — it is 6.5 MB.
    pub fn open(path: &Path) -> Result<Zip, String> {
        let data = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
        if data.len() < 22 {
            return Err("not a zip archive: the file is too short to hold a directory".into());
        }
        // The end-of-central-directory record is last, after a comment of up to 64 KiB.
        let start = data.len().saturating_sub(22 + 0xFFFF);
        let eocd = (start..=data.len() - 22)
            .rev()
            .find(|&i| data[i..i + 4] == [0x50, 0x4b, 0x05, 0x06])
            .ok_or("not a zip archive: no end-of-central-directory record")?;
        let count = le16(&data, eocd + 10) as usize;
        let mut at = le32(&data, eocd + 16) as usize;

        let mut members = Vec::with_capacity(count);
        for _ in 0..count {
            if data.get(at..at + 4) != Some(&[0x50, 0x4b, 0x01, 0x02][..]) {
                return Err("zip: the central directory is not where the archive says it is".into());
            }
            let nlen = le16(&data, at + 28) as usize;
            let elen = le16(&data, at + 30) as usize;
            let clen = le16(&data, at + 32) as usize;
            let name = String::from_utf8_lossy(&data[at + 46..at + 46 + nlen]).into_owned();
            members.push(Member {
                name,
                method: le16(&data, at + 10),
                crc: le32(&data, at + 16),
                packed: le32(&data, at + 20) as u64,
                size: le32(&data, at + 24) as u64,
                header: le32(&data, at + 42) as u64,
            });
            at += 46 + nlen + elen + clen;
        }
        Ok(Zip { data, members })
    }

    /// Decompress one member and check its CRC-32.
    ///
    /// The check is not decoration: it is what turns "this inflate looks right" into "this inflate
    /// reproduced 13 895 680 bytes that hash to the value Apple's archiver recorded in 2008".
    pub fn extract(&self, m: &Member) -> Result<Vec<u8>, String> {
        let h = m.header as usize;
        if self.data.get(h..h + 4) != Some(&[0x50, 0x4b, 0x03, 0x04][..]) {
            return Err(format!("zip: no local header for {}", m.name));
        }
        // The local header repeats the name and may carry a DIFFERENT extra field from the central
        // one, so both lengths are read from here rather than reused.
        let nlen = le16(&self.data, h + 26) as usize;
        let elen = le16(&self.data, h + 28) as usize;
        let start = h + 30 + nlen + elen;
        let end = start + m.packed as usize;
        let packed = self
            .data
            .get(start..end)
            .ok_or_else(|| format!("zip: {} runs past the end of the archive", m.name))?;

        let out = match m.method {
            0 => packed.to_vec(),
            8 => inflate(packed, m.size as usize)?,
            other => return Err(format!("zip: {} uses compression method {other}, which this reader does not implement", m.name)),
        };
        if out.len() as u64 != m.size {
            return Err(format!(
                "zip: {} unpacked to {} bytes, not the {} the directory declares",
                m.name,
                out.len(),
                m.size
            ));
        }
        if crc32(&out) != m.crc {
            return Err(format!(
                "zip: {} failed its CRC-32 — the archive is damaged, or this reader is wrong. \
                 Either way the firmware would be corrupt and nothing is written.",
                m.name
            ));
        }
        Ok(out)
    }

    /// The firmware partition image, which is the whole point of opening an IPSW.
    ///
    /// Matched by name rather than by position: `Firmware-20.6.3` in `iPod_20.1.3.ipsw`, and
    /// `Firmware-<x>` in every other one. `manifest.plist` is the only other member.
    pub fn firmware(&self) -> Result<(Member, Vec<u8>), String> {
        let m = self
            .members
            .iter()
            .find(|m| m.name.starts_with("Firmware-"))
            .or_else(|| self.members.iter().max_by_key(|m| m.size))
            .ok_or("this archive has no members")?
            .clone();
        if !m.name.starts_with("Firmware-") {
            return Err(format!(
                "no `Firmware-…` member in this archive — its largest is `{}`. \
                 An iPod IPSW contains exactly two files: the firmware partition image and \
                 `manifest.plist`.",
                m.name
            ));
        }
        let body = self.extract(&m)?;
        Ok((m, body))
    }
}

// ---------------------------------------------------------------- what is inside the firmware

/// One image in an iPod firmware directory: `osos`, `rsrc`, `aupd`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Image {
    pub tag: String,
    pub dev: u32,
    pub offset: u32,
    pub len: u32,
    pub addr: u32,
}

/// Where the `!ATA` directory sits inside a firmware partition.
pub const DIRECTORY_AT: usize = 0x4200;
/// Where a 5G/5.5G loads every image it runs.
pub const LOAD_ADDR_5G: u32 = 0x1000_0000;
/// MBR partition 0 starts here on every iPod this project has seen.
pub const FIRMWARE_LBA: u32 = 63;

/// Parse the `!ATA` directory out of a firmware partition image.
pub fn images(fw: &[u8]) -> Vec<Image> {
    let mut out = Vec::new();
    let Some(dir) = fw.get(DIRECTORY_AT..DIRECTORY_AT + 0x200) else { return out };
    for e in dir.chunks_exact(40) {
        if &e[0..4] != b"!ATA" {
            break;
        }
        // The tag is a little-endian u32 of four characters, so the bytes read backwards — `osos`
        // appears as `soso` in a byte dump.
        let tag: String = e[4..8].iter().rev().map(|&b| b as char).collect();
        out.push(Image {
            tag,
            dev: le32(e, 8),
            offset: le32(e, 0x0c),
            len: le32(e, 0x10),
            addr: le32(e, 0x14),
        });
    }
    out
}

// ---------------------------------------------------------------- building the drive

/// Default drive size: 8 GiB, which is `ipod8g-retail.img`'s, so a disk built here and the
/// reference image have the same geometry and a measurement can be compared between them.
pub const DEFAULT_SECTORS: u64 = 16_777_216;
/// Where the data partition starts on a real iPod, and therefore here. 16 MiB in, well clear of a
/// 27 140-sector firmware partition.
pub const DATA_LBA: u32 = 32_768;

/// Mark `aupd` as already applied, in place, and say whether there was one.
///
/// **This is the difference between a disk that boots and one that sits in the bootloader**, and it
/// is one byte.
///
/// A pristine IPSW's firmware directory carries three images: `osos`, `rsrc` and `aupd`, Apple's
/// NOR flash updater. Given all three the boot ROM runs the **updater** rather than the OS — which
/// is correct behaviour and is what `tools/ipod-boot/flash-update.sh` exists to reproduce — and the
/// updater's last act is a `WRITE SECTORS` back to the directory it was launched from, setting
/// `aupd`'s field at +0x08 to 1 so the next boot skips it. That is why a real iPod boots its OS on
/// the second power-up after a restore and not the first.
///
/// Writing the 1 here produces exactly the state a real iPod is in **after** that has happened, so
/// a freshly built disk boots RetailOS straight away instead of asking the user to sit through a
/// firmware update they did not ask for on a NOR the emulator is not writing anyway. Nothing else
/// in the partition is touched: `osos` and `rsrc` are Apple's bytes, unaltered.
///
/// `with_aupd` leaves it armed, which is the configuration `flash-update.sh` measures.
pub fn mark_aupd_applied(fw: &mut [u8]) -> bool {
    let dir = images(fw);
    let Some(i) = dir.iter().position(|e| e.tag == "aupd") else { return false };
    if dir[i].dev == 1 {
        return false;
    }
    let at = DIRECTORY_AT + i * 40 + 8;
    fw[at..at + 4].copy_from_slice(&1u32.to_le_bytes());
    true
}

/// Write a fresh, bootable drive image: MBR, Apple's firmware partition, and an empty FAT32 volume.
///
/// What RetailOS then does to it is measured and is why nothing more is written here: on first boot
/// it creates `Contacts`, `Calendars`, `Notes` and `iPod_Control/Device/Accessories`, writes two
/// placeholder vCards, and touches 41 sectors in all. It bootstraps its own volume; it just needs
/// one that is formatted.
///
/// The file is created **sparse** — `set_len` then seek-and-write — so an 8 GiB image costs about
/// 14 MB on any filesystem that has holes, which is all three of ext4, APFS and NTFS.
pub fn build_disk(fw: &[u8], out: &Path, sectors: u64) -> Result<(), String> {
    if fw.len() % 512 != 0 {
        return Err(format!(
            "the firmware partition is {} bytes, which is not a whole number of 512-byte sectors",
            fw.len()
        ));
    }
    let fw_sectors = (fw.len() / 512) as u32;
    if (FIRMWARE_LBA + fw_sectors) as u64 >= DATA_LBA as u64 {
        return Err(format!(
            "the firmware partition is {fw_sectors} sectors and would run past LBA {DATA_LBA}, \
             where the data partition starts"
        ));
    }
    if sectors <= DATA_LBA as u64 + 65_536 {
        return Err(format!("a {sectors}-sector drive is too small for a FAT32 volume"));
    }

    let mut f = std::fs::File::create(out).map_err(|e| format!("{}: {e}", out.display()))?;
    f.set_len(sectors * 512).map_err(|e| format!("{}: {e}", out.display()))?;

    // ---- the MBR. Two entries, and the first one's type is 0x00, which is not a mistake: Apple's
    // firmware partition is marked "empty" so that no PC operating system offers to mount it.
    let mut mbr = [0u8; 512];
    part(&mut mbr, 0, 0x00, FIRMWARE_LBA, fw_sectors);
    part(&mut mbr, 1, 0x0b, DATA_LBA, (sectors - DATA_LBA as u64) as u32);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    write_at(&mut f, 0, &mbr)?;

    // ---- Apple's firmware partition, byte for byte, exactly where `dd … seek=63` puts it.
    write_at(&mut f, FIRMWARE_LBA as u64 * 512, fw)?;

    // ---- an empty FAT32 volume for the rest.
    let vol = fat32(sectors - DATA_LBA as u64)?;
    for (rel, block) in vol {
        write_at(&mut f, (DATA_LBA as u64 + rel) * 512, &block)?;
    }
    f.flush().map_err(|e| format!("{}: {e}", out.display()))
}

fn part(mbr: &mut [u8], i: usize, kind: u8, lba: u32, sectors: u32) {
    let at = 0x1be + i * 16;
    mbr[at] = 0x00; // not bootable; the iPod's ROM does not read this byte
    // CHS is meaningless past 8 GB and every consumer of these images is LBA-only. 0xfe/0xff/0xff
    // is the conventional "use LBA" filler, and it is what the reference image carries.
    mbr[at + 1] = 0xfe;
    mbr[at + 2] = 0xff;
    mbr[at + 3] = 0xff;
    mbr[at + 4] = kind;
    mbr[at + 5] = 0xfe;
    mbr[at + 6] = 0xff;
    mbr[at + 7] = 0xff;
    mbr[at + 8..at + 12].copy_from_slice(&lba.to_le_bytes());
    mbr[at + 12..at + 16].copy_from_slice(&sectors.to_le_bytes());
}

fn write_at(f: &mut std::fs::File, at: u64, data: &[u8]) -> Result<(), String> {
    f.seek(SeekFrom::Start(at)).map_err(|e| format!("seek to {at}: {e}"))?;
    f.write_all(data).map_err(|e| format!("write at {at}: {e}"))
}

/// An empty FAT32 volume, as a list of `(sector offset within the partition, bytes)`.
///
/// Returned as sparse blocks rather than one buffer because the two FATs of an 8 GB volume are
/// 4 MB each and all but their first twelve bytes are zero — and the file is sparse, so a zero not
/// written is a block not allocated.
fn fat32(sectors: u64) -> Result<Vec<(u64, Vec<u8>)>, String> {
    let reserved: u32 = 32;
    let nfats: u32 = 2;
    // 32 sectors per cluster — 16 KiB — which is what a drive this size gets and what keeps the FAT
    // to a few megabytes.
    let spc: u32 = 32;

    // Microsoft's own sizing formula (FAT32 spec, "FatSz32"): solve for a FAT big enough to
    // describe the clusters that remain once the FAT itself is subtracted.
    let tmp1 = sectors - reserved as u64;
    let tmp2 = (256 * spc as u64 + nfats as u64) / 2;
    let fat_sectors = ((tmp1 + tmp2 - 1) / tmp2) as u32;
    let data_sectors = sectors - reserved as u64 - (nfats as u64 * fat_sectors as u64);
    let clusters = data_sectors / spc as u64;
    if clusters < 65_525 {
        return Err(format!(
            "a {sectors}-sector volume with {spc}-sector clusters gives {clusters} clusters, \
             which is FAT16 territory, not FAT32"
        ));
    }

    let mut bs = vec![0u8; 512];
    bs[0..3].copy_from_slice(&[0xEB, 0x58, 0x90]); // jmp short +0x58; nop — the conventional stub
    bs[3..11].copy_from_slice(b"MSWIN4.1"); // the OEM name every formatter writes and none reads
    bs[11..13].copy_from_slice(&512u16.to_le_bytes()); // bytes per sector
    bs[13] = spc as u8;
    bs[14..16].copy_from_slice(&(reserved as u16).to_le_bytes());
    bs[16] = nfats as u8;
    // FAT32 has no fixed root directory, so both of these are zero, and that is how a reader tells
    // FAT32 from FAT16.
    bs[17..19].copy_from_slice(&0u16.to_le_bytes()); // root entries
    bs[19..21].copy_from_slice(&0u16.to_le_bytes()); // total sectors (16-bit) — 0 means "see +0x20"
    bs[21] = 0xF8; // fixed disk
    bs[22..24].copy_from_slice(&0u16.to_le_bytes()); // FAT size (16-bit) — 0 on FAT32
    bs[24..26].copy_from_slice(&63u16.to_le_bytes()); // sectors per track, cosmetic
    bs[26..28].copy_from_slice(&255u16.to_le_bytes()); // heads, cosmetic
    bs[28..32].copy_from_slice(&DATA_LBA.to_le_bytes()); // hidden sectors: where this partition is
    bs[32..36].copy_from_slice(&(sectors as u32).to_le_bytes()); // total sectors (32-bit)
    bs[36..40].copy_from_slice(&fat_sectors.to_le_bytes());
    bs[40..42].copy_from_slice(&0u16.to_le_bytes()); // flags: both FATs live
    bs[42..44].copy_from_slice(&0u16.to_le_bytes()); // version
    bs[44..48].copy_from_slice(&2u32.to_le_bytes()); // root directory's first cluster
    bs[48..50].copy_from_slice(&1u16.to_le_bytes()); // FSInfo sector
    bs[50..52].copy_from_slice(&6u16.to_le_bytes()); // backup boot sector
    bs[64] = 0x80; // drive number
    bs[66] = 0x29; // extended boot signature: the three fields below are present
    bs[67..71].copy_from_slice(&0x1234_5678u32.to_le_bytes()); // volume id
    bs[71..82].copy_from_slice(b"IPOD       ");
    bs[82..90].copy_from_slice(b"FAT32   ");
    bs[510] = 0x55;
    bs[511] = 0xAA;

    let mut fsinfo = vec![0u8; 512];
    fsinfo[0..4].copy_from_slice(b"RRaA");
    fsinfo[484..488].copy_from_slice(b"rrAa");
    // Free count and next-free hint. Cluster 2 is the root directory, so one is in use.
    fsinfo[488..492].copy_from_slice(&((clusters - 1) as u32).to_le_bytes());
    fsinfo[492..496].copy_from_slice(&3u32.to_le_bytes());
    fsinfo[510] = 0x55;
    fsinfo[511] = 0xAA;

    // The head of each FAT: the media descriptor, the end-of-chain marker, and the root directory's
    // one-cluster chain terminating immediately.
    let mut fat = vec![0u8; 512];
    fat[0..4].copy_from_slice(&0x0FFF_FFF8u32.to_le_bytes());
    fat[4..8].copy_from_slice(&0x0FFF_FFFFu32.to_le_bytes());
    fat[8..12].copy_from_slice(&0x0FFF_FFFFu32.to_le_bytes());

    // The root directory: one cluster, and one entry in it — the volume label, which is what makes
    // a freshly built volume show a name rather than an empty string.
    let mut root = vec![0u8; 512];
    root[0..11].copy_from_slice(b"IPOD       ");
    root[11] = 0x08; // ATTR_VOLUME_ID

    let root_lba = reserved as u64 + nfats as u64 * fat_sectors as u64;
    let mut out = vec![
        (0u64, bs.clone()),
        (1, fsinfo.clone()),
        // The backup at sector 6 is three sectors of the same three structures.
        (6, bs),
        (7, fsinfo),
        (reserved as u64, fat.clone()),
        (reserved as u64 + fat_sectors as u64, fat.clone()),
        (root_lba, root),
    ];
    // The rest of the root cluster must be zeroed, and on a fresh sparse file it already is.
    out.sort_by_key(|(a, _)| *a);
    fat.clear();
    Ok(out)
}

// ---------------------------------------------------------------- inspecting an IPSW

/// What an IPSW turned out to be, in a sentence a person can act on.
pub enum Ipsw {
    /// Usable. The string describes it; the bytes are the firmware partition.
    Good(String, Vec<u8>),
    /// It parses and it is not this machine.
    Wrong(String),
    /// It is not an IPSW, or it is damaged.
    Bad(String),
}

/// Open an IPSW, extract its firmware partition, and decide whether it is a 5G/5.5G one.
pub fn inspect(path: &Path) -> Ipsw {
    let zip = match Zip::open(path) {
        Ok(z) => z,
        Err(e) => {
            return Ipsw::Bad(format!(
                "{e}\nAn IPSW is a zip archive containing a firmware partition image and \
                 `manifest.plist`. A `.ipsw` that will not open as a zip is a partial download."
            ))
        }
    };
    let (m, fw) = match zip.firmware() {
        Ok(v) => v,
        Err(e) => return Ipsw::Bad(e),
    };
    let dir = images(&fw);
    if dir.is_empty() {
        return Ipsw::Wrong(format!(
            "`{}` is {} bytes but has no `!ATA` directory at {DIRECTORY_AT:#x}. \
             Every iPod firmware partition carries one; this is a bundle for a device that does \
             not use this layout.",
            m.name,
            fw.len()
        ));
    }
    let Some(osos) = dir.iter().find(|i| i.tag == "osos") else {
        let names: Vec<&str> = dir.iter().map(|i| i.tag.as_str()).collect();
        return Ipsw::Wrong(format!(
            "`{}` lists {} but no `osos` — there is no OS image in it to boot.",
            m.name,
            names.join(" · ")
        ));
    };
    if osos.addr != LOAD_ADDR_5G {
        return Ipsw::Wrong(format!(
            "`{}` loads `osos` at {:#010x}, not {LOAD_ADDR_5G:#010x}. \
             A 5G/5.5G loads its OS at {LOAD_ADDR_5G:#010x} — this is a bundle for a different \
             iPod, and this emulator models the 5G/5.5G (PP5021C) only.",
            m.name, osos.addr
        ));
    }
    let mut s = format!(
        "{} — {} bytes ({} sectors), and it fits MBR partition 0 exactly. Images: ",
        m.name,
        fw.len(),
        fw.len() / 512
    );
    let list: Vec<String> = dir
        .iter()
        .map(|i| format!("{} {} KiB @ {:#010x}", i.tag, i.len / 1024, i.addr))
        .collect();
    s.push_str(&list.join(" · "));
    // The load-address check above rejects bundles for a different *iPod*. It does not catch a
    // bundle for a different *updater family* of the same iPod, because they all load `osos` at the
    // same address — and that mismatch does not fail loudly. RetailOS boots, does not recognise the
    // drive as its own, and shows "Connect to your computer. Use iTunes to restore." after about 70
    // ATA commands, which reads as a broken emulator rather than a mismatched pair.
    //
    // Measured 2026-08-14: family 25 against a family-20 NOR gives exactly that screen; the matching
    // pair reaches the language picker with 618. So say the family out loud, because the user is the
    // only one who knows which iPod their flash dump came off.
    if let Some(fam) = m.name.strip_prefix("Firmware-").and_then(|r| r.split('.').next()) {
        s.push_str(&format!(
            "\n  **Updater family {fam}.** This must match the iPod your NOR dump came from. \
             A mismatch does not fail loudly: RetailOS boots and then asks you to restore it \
             from iTunes, after roughly 70 ATA commands instead of 600."
        ));
    }
    Ipsw::Good(s, fw)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_matches_the_published_check_value() {
        // The CRC-32 check value from the standard: "123456789" -> 0xCBF43926.
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
        assert_eq!(crc32(b""), 0);
    }

    /// A stored (method 0) DEFLATE block, hand-assembled: final block, type 00, LEN/NLEN, payload.
    #[test]
    fn inflate_reads_a_stored_block() {
        let payload = b"the panel is the one surface that must not be prettified";
        let mut z = vec![0x01];
        z.extend_from_slice(&(payload.len() as u16).to_le_bytes());
        z.extend_from_slice(&(!(payload.len() as u16)).to_le_bytes());
        z.extend_from_slice(payload);
        assert_eq!(inflate(&z, payload.len()).unwrap(), payload);
    }

    /// The fixed-Huffman path, with a back-reference that **overlaps its own output** — a literal
    /// `a` followed by a length-19 distance-1 copy. This is where a `copy_within` gets it wrong,
    /// because the source of the copy is being written as it is read; run-length encoding falls out
    /// of LZ77 exactly here.
    ///
    /// Bytes produced by zlib at level 9 with `wbits=-15`, so this is a real stream and not a
    /// hand-assembled guess at one.
    #[test]
    fn inflate_reads_fixed_huffman_with_an_overlapping_backreference() {
        let want = vec![b'a'; 20];
        let z = [0x4b, 0x4c, 0xc4, 0x04, 0x00];
        assert_eq!(inflate(&z, want.len()).unwrap(), want);
    }

    /// The dynamic-Huffman path — the one an IPSW's members actually use, and the one with the
    /// code-length alphabet and its three repeat codes in it. Also zlib level 9, `wbits=-15`.
    #[test]
    fn inflate_reads_dynamic_huffman() {
        let want: Vec<u8> = b"the quick brown fox jumps over the lazy dog. ".repeat(8);
        let z = [
            0x2b, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c, 0xce, 0x56, 0x48, 0x2a, 0xca, 0x2f,
            0xcf, 0x53, 0x48, 0xcb, 0xaf, 0x50, 0xc8, 0x2a, 0xcd, 0x2d, 0x28, 0x56, 0xc8, 0x2f,
            0x4b, 0x2d, 0x52, 0x28, 0x01, 0x4a, 0xe7, 0x24, 0x56, 0x55, 0x2a, 0xa4, 0xe4, 0xa7,
            0xeb, 0x81, 0x79, 0xa3, 0x8a, 0xc9, 0x52, 0x0c, 0x00,
        ];
        let got = inflate(&z, want.len()).unwrap();
        assert_eq!(got.len(), want.len());
        assert_eq!(got, want);
        assert_eq!(crc32(&got), crc32(&want));
    }

    #[test]
    fn a_truncated_stream_is_an_error_not_a_panic() {
        assert!(inflate(&[0x01, 0x05, 0x00], 5).is_err());
        assert!(inflate(&[], 5).is_err());
        // Block type 3 is reserved.
        assert!(inflate(&[0x07], 5).is_err());
    }

    /// The directory record is the same 40 bytes on the NOR and on the drive, and its tag is a
    /// little-endian word rather than a string — the detail a hand-written parser gets wrong first.
    #[test]
    fn the_firmware_directory_reads_its_tags_backwards() {
        let mut fw = vec![0u8; DIRECTORY_AT + 0x200];
        let e = DIRECTORY_AT;
        fw[e..e + 4].copy_from_slice(b"!ATA");
        fw[e + 4..e + 8].copy_from_slice(b"soso");
        fw[e + 0x0c..e + 0x10].copy_from_slice(&0x4400u32.to_le_bytes());
        fw[e + 0x10..e + 0x14].copy_from_slice(&0x0073_5a00u32.to_le_bytes());
        fw[e + 0x14..e + 0x18].copy_from_slice(&LOAD_ADDR_5G.to_le_bytes());
        let v = images(&fw);
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].tag, "osos");
        assert_eq!(v[0].len, 0x0073_5a00);
        assert_eq!(v[0].addr, LOAD_ADDR_5G);
    }

    /// The built disk must parse as the emulator's own loader reads it: MBR signature, partition 0
    /// type 0x00 at LBA 63, and `!ATA` at partition + 0x4200. That is the whole contract.
    #[test]
    fn a_built_disk_has_the_layout_the_bootloader_looks_for() {
        let mut fw = vec![0u8; 13_895_680];
        let e = DIRECTORY_AT;
        fw[e..e + 4].copy_from_slice(b"!ATA");
        fw[e + 4..e + 8].copy_from_slice(b"soso");
        fw[e + 0x14..e + 0x18].copy_from_slice(&LOAD_ADDR_5G.to_le_bytes());

        let p = std::env::temp_dir().join(format!("ipsw-build-{}.img", std::process::id()));
        build_disk(&fw, &p, DEFAULT_SECTORS).unwrap();

        let f = std::fs::File::open(&p).unwrap();
        assert_eq!(f.metadata().unwrap().len(), DEFAULT_SECTORS * 512);
        let mut read = |at: u64, n: usize| {
            use std::io::Read;
            let mut f = std::fs::File::open(&p).unwrap();
            f.seek(SeekFrom::Start(at)).unwrap();
            let mut b = vec![0u8; n];
            f.read_exact(&mut b).unwrap();
            b
        };
        let mbr = read(0, 512);
        assert_eq!(&mbr[510..512], &[0x55, 0xAA]);
        assert_eq!(mbr[0x1be + 4], 0x00, "Apple's firmware partition is type 0x00");
        assert_eq!(le32(&mbr, 0x1be + 8), FIRMWARE_LBA);
        assert_eq!(le32(&mbr, 0x1be + 12), 27_140, "13 895 680 bytes is 27 140 sectors");
        assert_eq!(mbr[0x1be + 16 + 4], 0x0b, "and the data partition is FAT32");
        assert_eq!(le32(&mbr, 0x1be + 16 + 8), DATA_LBA);

        let dir = read(FIRMWARE_LBA as u64 * 512 + DIRECTORY_AT as u64, 8);
        assert_eq!(&dir[0..4], b"!ATA", "the directory landed at partition + 0x4200");
        assert_eq!(&dir[4..8], b"soso");

        // The FAT32 volume, read back through its own boot sector.
        let bs = read(DATA_LBA as u64 * 512, 512);
        assert_eq!(&bs[510..512], &[0x55, 0xAA]);
        assert_eq!(&bs[82..90], b"FAT32   ");
        assert_eq!(le16(&bs, 11), 512, "bytes per sector");
        assert_eq!(le16(&bs, 17), 0, "FAT32 has no fixed root directory");
        assert_eq!(le32(&bs, 44), 2, "and its root starts at cluster 2");
        assert_eq!(le32(&bs, 28), DATA_LBA, "hidden sectors names where the partition is");
        let fat_sectors = le32(&bs, 36) as u64;
        let fat = read((DATA_LBA as u64 + 32) * 512, 12);
        assert_eq!(le32(&fat, 0) & 0x0FFF_FFFF, 0x0FFF_FFF8, "media descriptor");
        assert_eq!(le32(&fat, 8), 0x0FFF_FFFF, "the root's chain ends at its first cluster");
        // The second FAT is a copy, and a volume whose two FATs disagree is one a checker will
        // condemn on sight.
        let fat2 = read((DATA_LBA as u64 + 32 + fat_sectors) * 512, 12);
        assert_eq!(fat, fat2);

        let _ = std::fs::remove_file(p);
    }

    #[test]
    fn a_firmware_that_is_not_a_whole_number_of_sectors_is_refused() {
        let p = std::env::temp_dir().join("ipsw-build-odd.img");
        assert!(build_disk(&vec![0u8; 1001], &p, DEFAULT_SECTORS).is_err());
        let _ = std::fs::remove_file(p);
    }

    /// A firmware partition big enough to collide with the data partition must be refused rather
    /// than quietly overwritten — that is a corrupt volume that boots far enough to look fine.
    #[test]
    fn a_firmware_that_would_overrun_the_data_partition_is_refused() {
        let p = std::env::temp_dir().join("ipsw-build-huge.img");
        let too_big = vec![0u8; (DATA_LBA as usize) * 512];
        assert!(build_disk(&too_big, &p, DEFAULT_SECTORS).is_err());
        let _ = std::fs::remove_file(p);
    }
}

/// What a drive image's firmware partition says about itself, read without booting anything.
///
/// The point of this is a screen. RetailOS answers several unrelated faults with one picture — the
/// plug-into-a-computer glyph — and a user restarting the emulator cannot tell which they hit. The
/// emulator can read the same partition RetailOS reads and say so plainly.
#[derive(Debug, Clone, PartialEq)]
pub struct FirmwareState {
    /// Every tag in the `!ATA` directory, in order: normally `osos`, `rsrc`, sometimes `aupd`.
    pub tags: Vec<String>,
    /// An `aupd` image is present and **not** marked applied, so the next boot runs the flash
    /// updater instead of the OS. On hardware that is the first of two boots; here it is a dead
    /// end, because nothing power-cycles the machine and runs the second one.
    pub aupd_armed: bool,
    /// There is an operating system to boot at all.
    pub has_os: bool,
}

/// Read the firmware partition of a drive image and report what is in it.
///
/// Cheap by construction — it reads the first 17 KB of the partition, not the 8 GB drive.
pub fn firmware_state(disk: &Path) -> Result<FirmwareState, String> {
    use std::io::{Read, Seek, SeekFrom};
    let mut f = std::fs::File::open(disk).map_err(|e| format!("{}: {e}", disk.display()))?;
    let mut fw = vec![0u8; DIRECTORY_AT + 0x200];
    f.seek(SeekFrom::Start(u64::from(FIRMWARE_LBA) * 512))
        .and_then(|_| f.read_exact(&mut fw))
        .map_err(|e| format!("{}: firmware partition unreadable: {e}", disk.display()))?;

    let dir = images(&fw);
    Ok(FirmwareState {
        // `dev == 1` is Apple's "already applied" mark — the same field `mark_aupd_applied` sets,
        // read here rather than written.
        aupd_armed: dir.iter().any(|e| e.tag == "aupd" && e.dev != 1),
        has_os: dir.iter().any(|e| e.tag == "osos"),
        tags: dir.into_iter().map(|e| e.tag).collect(),
    })
}

#[cfg(test)]
mod firmware_state_tests {
    use super::*;

    /// Write a drive image whose firmware partition carries `entries` as its `!ATA` directory.
    ///
    /// The byte layout is copied from a real 5.5G drive, dumped at absolute offset 0xC000
    /// (`FIRMWARE_LBA` 63 × 512 + `DIRECTORY_AT`): `!ATA`, then the four tag characters stored
    /// backwards, then `dev` as a little-endian u32.
    fn drive(entries: &[(&str, u32)]) -> std::path::PathBuf {
        let p = std::env::temp_dir().join(format!(
            "ipod-fw-{}-{}.img",
            std::process::id(),
            entries.iter().map(|(t, d)| format!("{t}{d}")).collect::<String>()
        ));
        let mut img = vec![0u8; FIRMWARE_LBA as usize * 512 + DIRECTORY_AT + 0x200];
        let base = FIRMWARE_LBA as usize * 512 + DIRECTORY_AT;
        for (i, (tag, dev)) in entries.iter().enumerate() {
            let at = base + i * 40;
            img[at..at + 4].copy_from_slice(b"!ATA");
            let t: Vec<u8> = tag.bytes().rev().collect();
            img[at + 4..at + 8].copy_from_slice(&t);
            img[at + 8..at + 12].copy_from_slice(&dev.to_le_bytes());
        }
        std::fs::write(&p, &img).unwrap();
        p
    }

    /// The ordinary drive: an OS, no updater. This is what a real one dumps as.
    #[test]
    fn a_normal_drive_has_an_os_and_no_armed_updater() {
        let p = drive(&[("osos", 0), ("rsrc", 0)]);
        let s = firmware_state(&p).unwrap();
        assert_eq!(s.tags, ["osos", "rsrc"], "tags are stored backwards and must be read back");
        assert!(s.has_os);
        assert!(!s.aupd_armed);
        let _ = std::fs::remove_file(p);
    }

    /// An armed updater — the boot that never reaches RetailOS, because nothing here power-cycles
    /// the machine to run the second one.
    #[test]
    fn an_unapplied_aupd_reads_as_armed() {
        let p = drive(&[("osos", 0), ("rsrc", 0), ("aupd", 0)]);
        assert!(firmware_state(&p).unwrap().aupd_armed);
        let _ = std::fs::remove_file(p);
    }

    /// `dev == 1` is Apple's applied mark, and `make-disk` sets it by default. Reading it as armed
    /// would report the fault on every ordinary drive, which is worse than reporting nothing.
    #[test]
    fn an_applied_aupd_is_not_armed() {
        let p = drive(&[("osos", 0), ("rsrc", 0), ("aupd", 1)]);
        let s = firmware_state(&p).unwrap();
        assert!(!s.aupd_armed);
        assert!(s.tags.contains(&"aupd".to_string()), "still present, just not armed");
        let _ = std::fs::remove_file(p);
    }

    /// A drive with nothing bootable on it, which is the honest reading of a file that is not an
    /// iPod drive at all.
    #[test]
    fn a_drive_with_no_os_says_so() {
        let p = drive(&[]);
        let s = firmware_state(&p).unwrap();
        assert!(!s.has_os);
        assert!(s.tags.is_empty());
        let _ = std::fs::remove_file(p);
    }

    /// Too small to hold a firmware partition: an error, not a silent "no OS".
    #[test]
    fn a_truncated_image_is_an_error() {
        let p = std::env::temp_dir().join(format!("ipod-fw-short-{}.img", std::process::id()));
        std::fs::write(&p, b"not a drive").unwrap();
        assert!(firmware_state(&p).is_err());
        let _ = std::fs::remove_file(p);
    }
}
