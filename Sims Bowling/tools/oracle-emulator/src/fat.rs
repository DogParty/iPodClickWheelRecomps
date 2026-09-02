//! Writing files into a FAT32 volume inside a drive image.
//!
//! **Why this exists.** Installing an operating system is two halves. `ipod-boot install-os` does
//! the firmware partition, which is what the bootloader runs; this does the data partition, which
//! is where that bootloader then looks for everything else. Rockbox's own bootloader says so out
//! loud on a machine that has only had the first half done — *"Can't load rockbox.ipod: File not
//! found"* — and iPodLinux needs `loader.cfg` and a kernel in the same place.
//!
//! **Scope, deliberately narrow.** Creating directories and writing whole files. No deletion, no
//! truncation, no rename, no in-place overwrite of an existing file — every one of those is a way
//! to lose data that this has no reason to be able to do. A name that already exists is an error,
//! not a silent replacement.
//!
//! Long names are supported because they have to be: the two paths this was written for are
//! `.rockbox` (leading dot) and `rockbox.ipod` (four-character extension), and neither fits 8.3.

use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

/// A mounted FAT32 volume, addressed through the image file it lives in.
pub struct Fat32 {
    file: std::fs::File,
    /// Byte offset of the partition within the image.
    base: u64,
    bytes_per_sector: u32,
    sectors_per_cluster: u32,
    reserved: u32,
    fats: u32,
    sectors_per_fat: u32,
    root_cluster: u32,
    /// First sector of the data area, relative to the partition.
    first_data_sector: u32,
    total_clusters: u32,
    /// Where the next free-cluster search starts. A hint, never a guarantee — a wrapped search
    /// checks everything before reporting the volume full.
    next_free: u32,
}

/// End-of-chain marker written into the FAT. Anything `>= 0x0fff_fff8` is end-of-chain on read.
const EOC: u32 = 0x0fff_ffff;
const ATTR_DIR: u8 = 0x10;
const ATTR_LFN: u8 = 0x0f;
const ENTRY: usize = 32;

fn le16(b: &[u8], at: usize) -> u32 {
    u16::from_le_bytes([b[at], b[at + 1]]) as u32
}
fn le32(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([b[at], b[at + 1], b[at + 2], b[at + 3]])
}

impl Fat32 {
    /// Open the FAT32 partition of a drive image for writing.
    ///
    /// Takes the **first** partition whose type is `0x0b` or `0x0c`, which on an iPod is the data
    /// partition; partition 0 is Apple's firmware partition and is not a filesystem at all.
    pub fn open(image: &Path) -> Result<Fat32, String> {
        let mut file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open(image)
            .map_err(|e| format!("{}: {e}", image.display()))?;
        let mut mbr = [0u8; 512];
        file.read_exact(&mut mbr).map_err(|e| e.to_string())?;
        if mbr[510] != 0x55 || mbr[511] != 0xAA {
            return Err(format!("{}: no MBR signature", image.display()));
        }
        let mut base = None;
        for i in 0..4 {
            let e = &mbr[446 + i * 16..446 + i * 16 + 16];
            if matches!(e[4], 0x0b | 0x0c) {
                base = Some(le32(e, 8) as u64 * 512);
                break;
            }
        }
        let base = base.ok_or_else(|| {
            format!("{}: no FAT32 partition (type 0x0b or 0x0c) in the MBR", image.display())
        })?;

        let mut bpb = [0u8; 512];
        file.seek(SeekFrom::Start(base)).and_then(|_| file.read_exact(&mut bpb)).map_err(|e| e.to_string())?;
        let bytes_per_sector = le16(&bpb, 11);
        let sectors_per_cluster = bpb[13] as u32;
        let reserved = le16(&bpb, 14);
        let fats = bpb[16] as u32;
        let sectors_per_fat = le32(&bpb, 36);
        let root_cluster = le32(&bpb, 44);
        // A FAT32 volume has no fixed-size root directory and 0 in the 16-bit sector count; if
        // either of those is untrue this is FAT16 and every offset below would be wrong.
        if le16(&bpb, 17) != 0 || sectors_per_fat == 0 || bytes_per_sector == 0 {
            return Err("not a FAT32 volume (root-entry count or FAT size says FAT12/16)".into());
        }
        let total_sectors = le32(&bpb, 32);
        let first_data_sector = reserved + fats * sectors_per_fat;
        let total_clusters = (total_sectors - first_data_sector) / sectors_per_cluster;

        Ok(Fat32 {
            file,
            base,
            bytes_per_sector,
            sectors_per_cluster,
            reserved,
            fats,
            sectors_per_fat,
            root_cluster,
            first_data_sector,
            total_clusters,
            next_free: 2,
        })
    }

    fn cluster_bytes(&self) -> usize {
        (self.bytes_per_sector * self.sectors_per_cluster) as usize
    }

    fn cluster_at(&self, cluster: u32) -> u64 {
        let sector = self.first_data_sector + (cluster - 2) * self.sectors_per_cluster;
        self.base + sector as u64 * self.bytes_per_sector as u64
    }

    fn read_at(&mut self, at: u64, n: usize) -> Result<Vec<u8>, String> {
        let mut b = vec![0u8; n];
        self.file.seek(SeekFrom::Start(at)).and_then(|_| self.file.read_exact(&mut b)).map_err(|e| e.to_string())?;
        Ok(b)
    }

    fn write_at(&mut self, at: u64, b: &[u8]) -> Result<(), String> {
        self.file.seek(SeekFrom::Start(at)).and_then(|_| self.file.write_all(b)).map_err(|e| e.to_string())
    }

    fn fat_entry(&mut self, cluster: u32) -> Result<u32, String> {
        let at = self.base
            + (self.reserved as u64 * self.bytes_per_sector as u64)
            + cluster as u64 * 4;
        Ok(le32(&self.read_at(at, 4)?, 0) & 0x0fff_ffff)
    }

    /// Write one FAT entry into **every** copy of the FAT.
    ///
    /// Both, always. A volume whose two FATs disagree is one that some other tool will "repair" by
    /// choosing the wrong one, and the file we just wrote is what gets lost.
    fn set_fat_entry(&mut self, cluster: u32, val: u32) -> Result<(), String> {
        for i in 0..self.fats {
            let at = self.base
                + ((self.reserved + i * self.sectors_per_fat) as u64
                    * self.bytes_per_sector as u64)
                + cluster as u64 * 4;
            let mut w = self.read_at(at, 4)?;
            // The top four bits are reserved and must be preserved.
            let keep = le32(&w, 0) & 0xf000_0000;
            w.copy_from_slice(&(keep | (val & 0x0fff_ffff)).to_le_bytes());
            self.write_at(at, &w)?;
        }
        Ok(())
    }

    fn chain(&mut self, first: u32) -> Result<Vec<u32>, String> {
        let mut out = vec![first];
        let mut c = first;
        loop {
            let next = self.fat_entry(c)?;
            if next < 2 || next >= 0x0fff_fff8 {
                return Ok(out);
            }
            if out.len() > self.total_clusters as usize {
                return Err("cluster chain loops".into());
            }
            out.push(next);
            c = next;
        }
    }

    /// Allocate `n` clusters, chain them, and zero them.
    ///
    /// Zeroed because a directory cluster full of previous contents parses as a directory full of
    /// entries, and because a data cluster's tail is visible in a file whose length is not a whole
    /// number of clusters.
    fn alloc(&mut self, n: usize) -> Result<Vec<u32>, String> {
        let mut got = Vec::with_capacity(n);
        // Scanned in blocks, and from where the last search finished. One 4-byte read per cluster
        // is a syscall per cluster, and this volume has four million of them — the difference is
        // between seconds and hours, on the path that writes a whole Rockbox install.
        const BLOCK: usize = 64 * 1024;
        let last = self.total_clusters + 2;
        let mut c = self.next_free.max(2);
        let mut wrapped = false;
        while got.len() < n {
            if c >= last {
                if wrapped {
                    return Err(format!("the volume is full — {n} clusters wanted"));
                }
                wrapped = true;
                c = 2;
            }
            let at = self.base
                + (self.reserved as u64 * self.bytes_per_sector as u64)
                + c as u64 * 4;
            let want = (BLOCK / 4).min((last - c) as usize);
            let block = self.read_at(at, want * 4)?;
            for (i, e) in block.chunks_exact(4).enumerate() {
                if le32(e, 0) & 0x0fff_ffff == 0 {
                    got.push(c + i as u32);
                    if got.len() == n {
                        break;
                    }
                }
            }
            c += want as u32;
        }
        self.next_free = got.last().copied().unwrap_or(2) + 1;
        let zero = vec![0u8; self.cluster_bytes()];
        for (i, &cl) in got.iter().enumerate() {
            let next = got.get(i + 1).copied().unwrap_or(EOC);
            self.set_fat_entry(cl, next)?;
            let at = self.cluster_at(cl);
            self.write_at(at, &zero)?;
        }
        Ok(got)
    }

    fn read_dir(&mut self, first: u32) -> Result<Vec<u8>, String> {
        let mut out = Vec::new();
        for c in self.chain(first)? {
            let at = self.cluster_at(c);
            let n = self.cluster_bytes();
            out.extend_from_slice(&self.read_at(at, n)?);
        }
        Ok(out)
    }

    fn write_dir(&mut self, first: u32, data: &[u8]) -> Result<(), String> {
        let chain = self.chain(first)?;
        let cb = self.cluster_bytes();
        for (i, c) in chain.iter().enumerate() {
            let at = self.cluster_at(*c);
            let from = i * cb;
            if from >= data.len() {
                break;
            }
            let end = (from + cb).min(data.len());
            let mut buf = data[from..end].to_vec();
            buf.resize(cb, 0);
            self.write_at(at, &buf)?;
        }
        Ok(())
    }

    /// Look up one path component in a directory, returning `(first_cluster, is_dir)`.
    fn find(&mut self, dir: u32, name: &str) -> Result<Option<(u32, bool)>, String> {
        let data = self.read_dir(dir)?;
        let mut long = String::new();
        for e in data.chunks_exact(ENTRY) {
            if e[0] == 0 {
                break;
            }
            if e[0] == 0xE5 {
                long.clear();
                continue;
            }
            if e[11] == ATTR_LFN {
                let mut part = String::new();
                for &o in &[1usize, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30] {
                    let u = le16(e, o) as u16;
                    if u == 0 || u == 0xffff {
                        break;
                    }
                    part.push(char::from_u32(u as u32).unwrap_or('?'));
                }
                long.insert_str(0, &part);
                continue;
            }
            let this = if long.is_empty() { short_name_of(e) } else { std::mem::take(&mut long) };
            if this.eq_ignore_ascii_case(name) {
                let cl = (le16(e, 20) << 16) | le16(e, 26);
                return Ok(Some((cl, e[11] & ATTR_DIR != 0)));
            }
            long.clear();
        }
        Ok(None)
    }

    /// Append entries to a directory, growing its cluster chain if there is no room.
    fn append_entries(&mut self, dir: u32, entries: &[u8]) -> Result<(), String> {
        let mut data = self.read_dir(dir)?;
        let need = entries.len();
        // A run of free slots long enough. `0x00` means "this and everything after is free".
        let mut at = None;
        let mut run = 0usize;
        // Where the run of free slots that reaches the END of the directory begins. This is the
        // one that matters when the directory has to grow: entries must continue from there and
        // not from the start of the new cluster.
        //
        // **Leaving that gap is a silent corruption, not a waste of space.** Every FAT reader
        // stops at the first `0x00` slot, so entries written past a gap are invisible — and
        // `names_in` is a FAT reader, so the generated `~N` short names stopped being unique too
        // and four files were written as `A-FAIR~5DAT`. One arithmetic slip, two bugs, and the
        // directory still looked plausible.
        let mut tail = data.len();
        for (i, e) in data.chunks_exact(ENTRY).enumerate() {
            let free = e[0] == 0 || e[0] == 0xE5;
            if free {
                run += ENTRY;
                if at.is_none() && run >= need {
                    at = Some((i + 1) * ENTRY - run);
                }
            } else {
                run = 0;
                tail = (i + 1) * ENTRY;
            }
        }
        let at = match at {
            Some(a) => a,
            None => {
                // Grow by as many clusters as the entries need past the existing tail.
                let cb = self.cluster_bytes();
                let short = (tail + need).saturating_sub(data.len());
                let extra = self.alloc(short.div_ceil(cb))?;
                let mut last = *self.chain(dir)?.last().unwrap();
                for c in &extra {
                    self.set_fat_entry(last, *c)?;
                    last = *c;
                }
                data.resize(data.len() + extra.len() * cb, 0);
                tail
            }
        };
        data[at..at + need].copy_from_slice(entries);
        self.write_dir(dir, &data)
    }

    /// Create a directory called `name` inside `dir`, or return the existing one.
    pub fn mkdir(&mut self, dir: u32, name: &str) -> Result<u32, String> {
        if let Some((cl, is_dir)) = self.find(dir, name)? {
            return if is_dir {
                Ok(cl)
            } else {
                Err(format!("`{name}` exists and is a file"))
            };
        }
        let cluster = self.alloc(1)?[0];
        // `.` and `..`, which fsck and every other reader expect. `..` of a root-level directory is
        // written as cluster 0 by convention, not as the root's actual cluster number.
        let mut dots = vec![0u8; ENTRY * 2];
        for (i, nm) in [b".          ", b"..         "].iter().enumerate() {
            let e = &mut dots[i * ENTRY..(i + 1) * ENTRY];
            e[..11].copy_from_slice(*nm);
            e[11] = ATTR_DIR;
            let target = if i == 0 {
                cluster
            } else if dir == self.root_cluster {
                0
            } else {
                dir
            };
            e[20..22].copy_from_slice(&((target >> 16) as u16).to_le_bytes());
            e[26..28].copy_from_slice(&(target as u16).to_le_bytes());
        }
        self.write_at(self.cluster_at(cluster), &dots)?;

        let existing = self.names_in(dir)?;
        let entries = dir_entries(name, cluster, 0, true, &existing);
        self.append_entries(dir, &entries)?;
        Ok(cluster)
    }

    /// Every short name already used in a directory, so a generated one cannot collide.
    fn names_in(&mut self, dir: u32) -> Result<Vec<String>, String> {
        let data = self.read_dir(dir)?;
        let mut out = Vec::new();
        for e in data.chunks_exact(ENTRY) {
            if e[0] == 0 {
                break;
            }
            if e[0] == 0xE5 || e[11] == ATTR_LFN {
                continue;
            }
            out.push(e[..11].iter().map(|&b| b as char).collect());
        }
        Ok(out)
    }

    /// Write `bytes` as a new file called `name` inside `dir`.
    ///
    /// Refuses to replace an existing name. Overwriting would mean freeing the old chain, and a
    /// tool that can free clusters is a tool that can free the wrong ones.
    pub fn write_file(&mut self, dir: u32, name: &str, bytes: &[u8]) -> Result<(), String> {
        if self.find(dir, name)?.is_some() {
            return Err(format!("`{name}` already exists — this never replaces a file"));
        }
        let cb = self.cluster_bytes();
        let need = bytes.len().div_ceil(cb).max(1);
        let chain = self.alloc(need)?;
        for (i, &c) in chain.iter().enumerate() {
            let from = i * cb;
            if from >= bytes.len() {
                break;
            }
            let end = (from + cb).min(bytes.len());
            let mut buf = bytes[from..end].to_vec();
            buf.resize(cb, 0);
            let at = self.cluster_at(c);
            self.write_at(at, &buf)?;
        }
        let existing = self.names_in(dir)?;
        let first = if bytes.is_empty() { 0 } else { chain[0] };
        let entries = dir_entries(name, first, bytes.len() as u32, false, &existing);
        self.append_entries(dir, &entries)
    }

    /// Resolve a `/`-separated path to a directory cluster, creating components as needed.
    pub fn mkdir_p(&mut self, path: &str) -> Result<u32, String> {
        let mut cur = self.root_cluster;
        for part in path.split('/').filter(|p| !p.is_empty()) {
            cur = self.mkdir(cur, part)?;
        }
        Ok(cur)
    }

    pub fn root(&self) -> u32 {
        self.root_cluster
    }

    pub fn flush(&mut self) -> Result<(), String> {
        self.file.sync_all().map_err(|e| e.to_string())
    }
}

fn short_name_of(e: &[u8]) -> String {
    let base: String = e[..8].iter().map(|&b| b as char).collect();
    let ext: String = e[8..11].iter().map(|&b| b as char).collect();
    let (base, ext) = (base.trim_end(), ext.trim_end());
    if ext.is_empty() {
        base.to_string()
    } else {
        format!("{base}.{ext}")
    }
}

/// The checksum every long-name entry carries, computed over the 11-byte short name it aliases.
fn lfn_checksum(short: &[u8; 11]) -> u8 {
    short.iter().fold(0u8, |sum, &b| (((sum & 1) << 7).wrapping_add(sum >> 1)).wrapping_add(b))
}

/// Generate an 8.3 alias, `~N` style, that no existing entry is using.
fn short_alias(name: &str, existing: &[String]) -> [u8; 11] {
    let up: String = name.to_ascii_uppercase();
    let (stem, ext) = match up.rfind('.') {
        // A leading dot is not an extension separator — `.rockbox` has no extension.
        Some(i) if i > 0 => (&up[..i], &up[i + 1..]),
        _ => (up.as_str(), ""),
    };
    let ok = |c: char| c.is_ascii_alphanumeric() || "$%'-_@~`!(){}^#&".contains(c);
    let clean: String = stem.chars().filter(|&c| ok(c)).collect();
    let ext: String = ext.chars().filter(|&c| ok(c)).take(3).collect();
    for n in 1..=999u32 {
        let tail = format!("~{n}");
        let keep = 8usize.saturating_sub(tail.len());
        let mut s = [b' '; 11];
        let head: String = clean.chars().take(keep).collect();
        let base = format!("{head}{tail}");
        s[..base.len().min(8)].copy_from_slice(&base.as_bytes()[..base.len().min(8)]);
        s[8..8 + ext.len()].copy_from_slice(ext.as_bytes());
        if !existing.iter().any(|e| e.as_bytes() == s) {
            return s;
        }
    }
    [b'~'; 11]
}

/// The full run of directory entries for one name: long-name parts, then the 8.3 entry.
fn dir_entries(name: &str, cluster: u32, size: u32, is_dir: bool, existing: &[String]) -> Vec<u8> {
    let short = short_alias(name, existing);
    let sum = lfn_checksum(&short);
    let utf16: Vec<u16> = name.encode_utf16().collect();
    let parts = utf16.len().div_ceil(13);
    let mut out = Vec::new();
    // Long-name entries are stored **last part first**, so a reader walking forwards prepends.
    for p in (0..parts).rev() {
        let mut e = [0u8; ENTRY];
        e[0] = (p as u8 + 1) | if p == parts - 1 { 0x40 } else { 0 };
        e[11] = ATTR_LFN;
        e[13] = sum;
        for (k, &o) in [1usize, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30].iter().enumerate() {
            let idx = p * 13 + k;
            let v = match idx.cmp(&utf16.len()) {
                std::cmp::Ordering::Less => utf16[idx],
                std::cmp::Ordering::Equal => 0,
                std::cmp::Ordering::Greater => 0xffff,
            };
            e[o..o + 2].copy_from_slice(&v.to_le_bytes());
        }
        out.extend_from_slice(&e);
    }
    let mut e = [0u8; ENTRY];
    e[..11].copy_from_slice(&short);
    e[11] = if is_dir { ATTR_DIR } else { 0x20 };
    e[20..22].copy_from_slice(&((cluster >> 16) as u16).to_le_bytes());
    e[26..28].copy_from_slice(&(cluster as u16).to_le_bytes());
    e[28..32].copy_from_slice(&size.to_le_bytes());
    out.extend_from_slice(&e);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A minimal but real FAT32 volume: MBR, BPB, two FATs, one root cluster.
    ///
    /// Built here rather than borrowed from a drive image so the test owns every parameter — the
    /// bugs this is guarding against are arithmetic (which sector is cluster N, which FAT copy did
    /// we forget), and they hide behind a volume whose numbers you did not choose.
    fn synth(path: &Path) {
        const PART: u32 = 1; // partition LBA
        const SECTORS: u32 = 8192;
        const RESERVED: u32 = 32;
        const FATSZ: u32 = 64;
        let mut img = vec![0u8; ((PART + SECTORS) * 512) as usize];

        img[446 + 4] = 0x0c;
        img[446 + 8..446 + 12].copy_from_slice(&PART.to_le_bytes());
        img[446 + 12..446 + 16].copy_from_slice(&SECTORS.to_le_bytes());
        img[510] = 0x55;
        img[511] = 0xAA;

        let p = (PART * 512) as usize;
        let b = &mut img[p..p + 512];
        b[11..13].copy_from_slice(&512u16.to_le_bytes()); // bytes per sector
        b[13] = 1; // sectors per cluster
        b[14..16].copy_from_slice(&(RESERVED as u16).to_le_bytes());
        b[16] = 2; // FATs
        b[17..19].copy_from_slice(&0u16.to_le_bytes()); // root entries: 0 => FAT32
        b[32..36].copy_from_slice(&SECTORS.to_le_bytes());
        b[36..40].copy_from_slice(&FATSZ.to_le_bytes());
        b[44..48].copy_from_slice(&2u32.to_le_bytes()); // root cluster
        b[510] = 0x55;
        b[511] = 0xAA;

        // Both FATs: media descriptor, end-of-chain, and the root's own cluster.
        for i in 0..2u32 {
            let at = p + ((RESERVED + i * FATSZ) * 512) as usize;
            img[at..at + 4].copy_from_slice(&0x0fff_fff8u32.to_le_bytes());
            img[at + 4..at + 8].copy_from_slice(&0x0fff_ffffu32.to_le_bytes());
            img[at + 8..at + 12].copy_from_slice(&EOC.to_le_bytes());
        }
        std::fs::write(path, &img).unwrap();
    }

    fn tmp(name: &str) -> std::path::PathBuf {
        std::env::temp_dir().join(name)
    }

    /// The two names this was written for, and neither fits 8.3: `.rockbox` starts with a dot and
    /// `rockbox.ipod` has a four-character extension.
    #[test]
    fn a_long_name_round_trips_through_a_directory_it_created() {
        let p = tmp("fat32-longname.img");
        synth(&p);
        let mut v = Fat32::open(&p).unwrap();
        let dir = v.mkdir_p(".rockbox").unwrap();
        v.write_file(dir, "rockbox.ipod", &[0xAB; 3000]).unwrap();
        v.flush().unwrap();

        let mut v = Fat32::open(&p).unwrap();
        let root = v.root();
        let (cl, is_dir) = v.find(root, ".rockbox").unwrap().expect(".rockbox is missing");
        assert!(is_dir, ".rockbox is not a directory");
        assert_eq!(cl, dir);
        let (_, f_is_dir) = v.find(cl, "rockbox.ipod").unwrap().expect("rockbox.ipod is missing");
        assert!(!f_is_dir);
        // Case-insensitive, like every other FAT reader.
        assert!(v.find(root, ".ROCKBOX").unwrap().is_some());
        let _ = std::fs::remove_file(p);
    }

    /// A file's bytes have to be where its directory entry says they are, and its length has to be
    /// the length — not the padded-out cluster count.
    #[test]
    fn a_files_bytes_land_where_its_entry_points_and_its_size_is_exact() {
        let p = tmp("fat32-bytes.img");
        synth(&p);
        let body: Vec<u8> = (0..3000u32).map(|i| (i % 251) as u8).collect();
        let mut v = Fat32::open(&p).unwrap();
        let root = v.root();
        v.write_file(root, "payload.bin", &body).unwrap();
        v.flush().unwrap();

        let mut v = Fat32::open(&p).unwrap();
        let root = v.root();
        let data = v.read_dir(root).unwrap();
        let sfn = data
            .chunks_exact(ENTRY)
            .find(|e| e[0] != 0 && e[0] != 0xE5 && e[11] != ATTR_LFN)
            .expect("no 8.3 entry");
        let size = le32(sfn, 28);
        assert_eq!(size, body.len() as u32, "size is padded to the cluster");
        let first = (le16(sfn, 20) << 16) | le16(sfn, 26);
        // 3000 bytes over 512-byte clusters is six clusters, and the chain must be that long.
        let chain = v.chain(first).unwrap();
        assert_eq!(chain.len(), 6, "chain: {chain:?}");
        let mut got = Vec::new();
        for c in chain {
            let at = v.cluster_at(c);
            got.extend_from_slice(&v.read_at(at, 512).unwrap());
        }
        assert_eq!(&got[..body.len()], &body[..]);
        let _ = std::fs::remove_file(p);
    }

    /// Both FATs, always — a volume whose copies disagree is one another tool will "repair" by
    /// picking the wrong one.
    #[test]
    fn every_fat_copy_is_written() {
        let p = tmp("fat32-fats.img");
        synth(&p);
        let mut v = Fat32::open(&p).unwrap();
        let root = v.root();
        v.write_file(root, "a-long-file-name.txt", &[1u8; 2000]).unwrap();
        v.flush().unwrap();

        let img = std::fs::read(&p).unwrap();
        let base = 512usize;
        let (reserved, fatsz) = (32usize, 64usize);
        let f0 = &img[base + reserved * 512..base + (reserved + fatsz) * 512];
        let f1 = &img[base + (reserved + fatsz) * 512..base + (reserved + 2 * fatsz) * 512];
        assert_eq!(f0, f1, "the two FATs disagree");
        let _ = std::fs::remove_file(p);
    }

    /// Replacing a file means freeing its clusters, and a tool that can free clusters can free the
    /// wrong ones. It refuses instead.
    #[test]
    fn writing_over_an_existing_name_is_refused() {
        let p = tmp("fat32-noclobber.img");
        synth(&p);
        let mut v = Fat32::open(&p).unwrap();
        let root = v.root();
        v.write_file(root, "once.bin", &[7u8; 10]).unwrap();
        let err = v.write_file(root, "once.bin", &[8u8; 10]).unwrap_err();
        assert!(err.contains("already exists"), "{err}");
        let _ = std::fs::remove_file(p);
    }

    /// Growing a directory past one cluster: 512-byte clusters hold 16 entries, and a long name
    /// costs several, so this crosses the boundary many times over.
    #[test]
    fn a_directory_grows_past_its_first_cluster() {
        let p = tmp("fat32-grow.img");
        synth(&p);
        let mut v = Fat32::open(&p).unwrap();
        let dir = v.mkdir_p("many").unwrap();
        for i in 0..40 {
            v.write_file(dir, &format!("a-fairly-long-name-{i:03}.dat"), &[i as u8; 8]).unwrap();
        }
        v.flush().unwrap();
        let mut v = Fat32::open(&p).unwrap();
        let root = v.root();
        let (dir, _) = v.find(root, "many").unwrap().unwrap();
        assert!(v.chain(dir).unwrap().len() > 1, "the directory never grew");
        for i in 0..40 {
            assert!(
                v.find(dir, &format!("a-fairly-long-name-{i:03}.dat")).unwrap().is_some(),
                "entry {i} is missing after the directory grew"
            );
        }
        // No gap, and therefore no repeated short name. Both were one bug: entries written past a
        // free slot are invisible to every FAT reader, and `names_in` is a FAT reader — so the
        // `~N` aliases stopped being unique and four files were written as `A-FAIR~5DAT`.
        let data = v.read_dir(dir).unwrap();
        let mut seen_free = false;
        let mut shorts = Vec::new();
        for e in data.chunks_exact(ENTRY) {
            if e[0] == 0 {
                seen_free = true;
                continue;
            }
            assert!(!seen_free, "a used entry sits after a free one — readers stop at the gap");
            if e[11] != ATTR_LFN {
                shorts.push(e[..11].to_vec());
            }
        }
        let unique: std::collections::BTreeSet<_> = shorts.iter().collect();
        assert_eq!(unique.len(), shorts.len(), "duplicate 8.3 aliases: {} of {}", unique.len(), shorts.len());
        let _ = std::fs::remove_file(p);
    }

}
