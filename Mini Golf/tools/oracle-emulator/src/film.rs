//! Record the co-processor's panel over a whole run, as a deduplicated PNG sequence.
//!
//! `--bcm-dump=ADDR:W:H:FILE` writes the surface **once**, at the end. That answers "what was on
//! the screen when the run stopped" and nothing else: every screen a boot passes through on the way
//! — the bootloader's frame, the Apple logo, the language list, each menu the wheel walked — is
//! gone by the time the dump is taken. This samples the same surface, with the same converter, on
//! an instruction cadence, and keeps every frame that differs from the one before it.
//!
//! Two properties are load-bearing and both are consequences of reading the co-processor's memory
//! rather than a window:
//!
//! - **The frames are exactly `W x H`.** No scaling, no interpolation, no cursor, no window chrome.
//!   A pixel in the file is a halfword the firmware wrote.
//! - **A sample costs no instructions.** The machine is not paused, rendered or asked anything; a
//!   range scan over a `BTreeMap` between two chunks of the run is the whole of it.
//!
//! ## Deduplication, and why the manifest is the interesting output
//!
//! A 2-minute boot at a 2 M-instruction cadence is ~800 samples, and RetailOS spends nearly all of
//! it on one unchanging picture. Writing 800 PNGs of the same screen would bury the four that
//! matter. So consecutive identical samples collapse into one **frame** carrying the instruction
//! count it first appeared at, the one it was last seen at, and how many samples it held — which is
//! the timing a video needs and the number a reader actually wants ("the language list is up for
//! 240 M instructions, the menu for 90 M").
//!
//! A frame whose digest matches one seen **earlier but not immediately before** gets its own entry
//! in the manifest and reuses the earlier file. "The screen went back to what it was" is a fact
//! worth being able to read off the manifest, and it is not the same fact as "nothing happened".

use std::collections::BTreeMap;
use std::path::PathBuf;

/// One run of consecutive samples that all held the same picture.
pub struct Frame {
    /// Position in the sequence. Manifest order, not file order — a repeat reuses an older file.
    pub index: usize,
    /// The file this frame's pixels are in, relative to the film directory.
    pub file: String,
    /// If this picture was already on screen earlier in the run, the index of that earlier frame.
    pub repeat_of: Option<usize>,
    /// Instruction count at the first sample that saw it, and at the last.
    pub first_at: u64,
    pub last_at: u64,
    /// How many consecutive samples held it. `held_instructions` is this times the cadence.
    pub samples: u64,
    /// Non-black pixels, the same count `--bcm-dump` prints. Useful for exactly one thing: telling
    /// a composited frame from the bootloader's. It does **not** distinguish two white UI screens
    /// — research/10 Addendum 30 §8 has three of them scoring 76 607 / 75 267 / 75 791.
    pub nonblack: u32,
    /// FNV-1a over the halfwords. This is what distinguishes screens; the pixel count does not.
    pub digest: u64,
}

/// The recorder. Built from `--bcm-film=ADDR:W:H:EVERY:DIR`, sampled between chunks of the run.
pub struct Film {
    pub base: u32,
    pub w: u32,
    pub h: u32,
    /// Instructions between samples.
    pub every: u64,
    /// Do not sample before this instruction count. `--bcm-film-from=N`, default 0.
    ///
    /// A cadence fine enough to read a moving ball is far too fine to spend on the two billion
    /// instructions of boot and menu navigation in front of it: 100 k over a 2.6 G run is 26 000
    /// surface scans, and 25 000 of them are of a screen nobody is looking at. **The chunking is
    /// deliberately left alone** — the run is still issued in `every`-sized pieces from the first
    /// instruction, so the machine executes exactly the iterations it would have without the flag
    /// and the film's no-perturbation control still holds. Only the scan is skipped.
    pub from: u64,
    pub dir: PathBuf,
    pub frames: Vec<Frame>,
    /// Every sample taken, including the ones that collapsed into a preceding frame.
    pub samples: u64,
    /// Digest -> the frame index that first carried it, so a returning screen reuses its file.
    seen: BTreeMap<u64, usize>,
    /// Write failures, kept so `finish` can say the sequence is incomplete rather than let a
    /// half-written film be read as a whole one.
    pub errors: Vec<String>,
}

impl Film {
    /// `spec` is `ADDR:W:H:EVERY:DIR`.
    ///
    /// `ADDR`, `W` and `H` are **hex**, matching `--bcm-dump` exactly — `0xE0000:140:F0` is the
    /// 320x240 panel, and having the two flags disagree on the base would be a trap. `EVERY` is
    /// decimal and takes a `k`/`M` suffix, matching `--wheel`'s time arithmetic.
    pub fn parse(spec: &str) -> Result<Film, String> {
        let p: Vec<&str> = spec.splitn(5, ':').collect();
        if p.len() != 5 {
            return Err(format!(
                "--bcm-film={spec:?}: expected ADDR:W:H:EVERY:DIR (hex ADDR/W/H, as --bcm-dump)"
            ));
        }
        let hex = |t: &str, what: &str| {
            u32::from_str_radix(t.trim_start_matches("0x"), 16)
                .map_err(|_| format!("--bcm-film: {what} {t:?} is not hex"))
        };
        let (base, w, h) = (hex(p[0], "address")?, hex(p[1], "width")?, hex(p[2], "height")?);
        if w == 0 || h == 0 {
            return Err("--bcm-film: width and height must be non-zero".into());
        }
        let every = parse_count(p[3]).ok_or_else(|| {
            format!("--bcm-film: cadence {:?} is not a number (try 2M, 500k, 250000)", p[3])
        })?;
        if every == 0 {
            return Err("--bcm-film: a cadence of 0 would sample every instruction forever".into());
        }
        let dir = PathBuf::from(p[4]);
        std::fs::create_dir_all(&dir).map_err(|e| format!("--bcm-film: {}: {e}", dir.display()))?;
        Ok(Film {
            base,
            w,
            h,
            every,
            from: 0,
            dir,
            frames: Vec::new(),
            samples: 0,
            seen: BTreeMap::new(),
            errors: Vec::new(),
        })
    }

    /// Read the surface out of the co-processor and fold it into the sequence.
    ///
    /// `at` is the machine's instruction count. Called between chunks of the run, so the cadence is
    /// the chunk size and a frame's time span is exact to that.
    pub fn sample(&mut self, bcm: &crate::Bcm, at: u64) {
        if at < self.from {
            return;
        }
        let px = read_surface(bcm, self.base, self.w, self.h);
        let digest = fnv1a(&px);
        self.samples += 1;

        // Same picture as the sample before it: extend that frame rather than write a file.
        if let Some(last) = self.frames.last_mut() {
            if last.digest == digest {
                last.last_at = at;
                last.samples += 1;
                return;
            }
        }

        let index = self.frames.len();
        let (file, repeat_of) = match self.seen.get(&digest) {
            Some(&earlier) => (self.frames[earlier].file.clone(), Some(earlier)),
            None => {
                let name = format!("frame-{index:05}.png");
                let rgb = to_rgb888(&px);
                let img = crate::png::encode(&rgb, self.w as usize, self.h as usize);
                if let Err(e) = std::fs::write(self.dir.join(&name), &img) {
                    let msg = format!("{}: {e}", self.dir.join(&name).display());
                    eprintln!("film: WRITE FAILED {msg}");
                    self.errors.push(msg);
                }
                self.seen.insert(digest, index);
                (name, None)
            }
        };
        self.frames.push(Frame {
            index,
            file,
            repeat_of,
            first_at: at,
            last_at: at,
            samples: 1,
            nonblack: px.iter().filter(|v| **v != 0).count() as u32,
            digest,
        });
    }

    /// Write `frames.tsv` and return the human-readable summary to print.
    ///
    /// The manifest is the artifact — the PNGs are just its pixels. Anything that assembles a
    /// video, or quotes a frame in a write-up, reads its timing from here.
    pub fn finish(&mut self) -> String {
        let mut tsv = format!(
            "# film of {:#010x}, {}x{}, sampled every {} instructions\n\
             # {} samples collapsed to {} frames ({} distinct pictures)\n\
             # index\tfile\trepeat_of\tfirst_instr\tlast_instr\tsamples\theld_instr\tnonblack\tdigest\n",
            self.base,
            self.w,
            self.h,
            self.every,
            self.samples,
            self.frames.len(),
            self.seen.len(),
        );
        for f in &self.frames {
            tsv.push_str(&format!(
                "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:#018x}\n",
                f.index,
                f.file,
                f.repeat_of.map_or_else(|| "-".to_string(), |i| i.to_string()),
                f.first_at,
                f.last_at,
                f.samples,
                f.samples * self.every,
                f.nonblack,
                f.digest,
            ));
        }
        let manifest = self.dir.join("frames.tsv");
        if let Err(e) = std::fs::write(&manifest, tsv.as_bytes()) {
            self.errors.push(format!("{}: {e}", manifest.display()));
        }

        let mut out = format!(
            "\nfilm {:#010x} {}x{} every {} -> {}\n  {} samples, {} frames, {} distinct pictures\n",
            self.base,
            self.w,
            self.h,
            self.every,
            self.dir.display(),
            self.samples,
            self.frames.len(),
            self.seen.len(),
        );
        for f in &self.frames {
            out.push_str(&format!(
                "  {:>4}  @{:<13} held {:>13} ({:>5} samples)  {:>6} non-black  {}{}\n",
                f.index,
                f.first_at,
                f.samples * self.every,
                f.samples,
                f.nonblack,
                f.file,
                match f.repeat_of {
                    Some(i) => format!("  = frame {i} again"),
                    None => String::new(),
                }
            ));
        }
        if self.errors.is_empty() {
            out.push_str(&format!("  manifest -> {}\n", manifest.display()));
        } else {
            out.push_str(&format!(
                "  FILM INCOMPLETE — {} write failures, first: {}\n",
                self.errors.len(),
                self.errors[0]
            ));
        }
        out
    }
}

/// `2M` / `500k` / `250000`. Same shape as the wheel script's times, deliberately.
pub fn parse_count(t: &str) -> Option<u64> {
    let t = t.replace('_', "");
    let (digits, mul) = match t.strip_suffix(['k', 'K']) {
        Some(d) => (d.to_string(), 1_000u64),
        None => match t.strip_suffix(['m', 'M']) {
            Some(d) => (d.to_string(), 1_000_000),
            None => (t.clone(), 1),
        },
    };
    digits.parse::<u64>().ok().map(|v| v * mul)
}

/// The surface, as halfwords, zero where the co-processor holds nothing.
///
/// A `range` scan rather than `w * h` point lookups: the map holds the firmware upload and both
/// buffers, so a lookup per pixel is 76 800 tree descents per sample and this is one.
fn read_surface(bcm: &crate::Bcm, base: u32, w: u32, h: u32) -> Vec<u16> {
    let n = (w as usize) * (h as usize);
    let mut px = vec![0u16; n];
    let end = base.wrapping_add((n as u32) * 2);
    for (&a, &v) in bcm.mem.range(base..end) {
        let off = a.wrapping_sub(base);
        // Halfword-granular and halfword-aligned. An odd address in that range is not a pixel of
        // this surface, and silently rounding it into one would fabricate a pixel.
        if off % 2 == 0 {
            px[(off / 2) as usize] = v;
        }
    }
    px
}

/// RGB565 -> RGB888, replicating the high bits into the low ones so full-scale is `0xff`.
///
/// The same expression `--bcm-dump` uses. Byte-for-byte identical output is the point: a PNG from
/// a film and a PPM from a dump of the same instant must be the same pixels.
fn to_rgb888(px: &[u16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(px.len() * 3);
    for p in px {
        let (r, g, b) = ((p >> 11) & 0x1f, (p >> 5) & 0x3f, p & 0x1f);
        out.push(((r << 3) | (r >> 2)) as u8);
        out.push(((g << 2) | (g >> 4)) as u8);
        out.push(((b << 3) | (b >> 2)) as u8);
    }
    out
}

/// FNV-1a over the halfword surface.
///
/// Not a cryptographic hash and does not need to be — it separates screens, and a collision would
/// merge two adjacent frames, which the instruction spans in the manifest would then contradict.
fn fnv1a(px: &[u16]) -> u64 {
    let mut h = 0xcbf2_9ce4_8422_2325u64;
    for v in px {
        for b in v.to_le_bytes() {
            h ^= b as u64;
            h = h.wrapping_mul(0x100_0000_01b3);
        }
    }
    h
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bcm_with(base: u32, px: &[u16]) -> crate::Bcm {
        let mut b = crate::Bcm::new(0x3000_0000);
        for (i, v) in px.iter().enumerate() {
            b.mem.insert(base + (i as u32) * 2, *v);
        }
        b
    }

    #[test]
    fn the_spec_parses_the_same_way_bcm_dump_does() {
        let f = Film::parse(&format!("0xE0000:140:F0:2M:{}", tmp("parse").display())).unwrap();
        assert_eq!((f.base, f.w, f.h, f.every), (0x000e_0000, 320, 240, 2_000_000));
        // Widths are hex, exactly as --bcm-dump reads them. 320 decimal would be 0x320 = 800.
        // The directory has to be one this test may create: `Film::parse` calls `create_dir_all`,
        // and a literal `/tmp/x` here made a directory at the filesystem root — on Windows, at
        // `C:\tmp\x`.
        let w = Film::parse(&format!("0xE0000:320:240:2M:{}", tmp("hexwidth").display()))
            .unwrap()
            .w;
        assert!(w == 0x320);
    }

    #[test]
    fn a_malformed_spec_is_refused_rather_than_half_applied() {
        assert!(Film::parse("0xE0000:140:F0:2M").is_err(), "four fields is not five");
        assert!(Film::parse("0xE0000:140:F0:nope:/tmp/x").is_err(), "cadence must be a number");
        assert!(Film::parse("0xE0000:140:F0:0:/tmp/x").is_err(), "a zero cadence is refused");
        assert!(Film::parse("0xE0000:0:F0:2M:/tmp/x").is_err(), "a zero width is refused");
    }

    /// The property the whole tool exists for: a long stretch of an unchanging screen is one frame
    /// and one file, and the manifest still says how long it was up.
    #[test]
    fn identical_consecutive_samples_collapse_and_keep_their_span() {
        let dir = tmp("collapse");
        let mut f = Film::parse(&format!("0x1000:4:2:1M:{}", dir.display())).unwrap();
        let a = bcm_with(0x1000, &[0xffff; 8]);
        for i in 0..5 {
            f.sample(&a, i * 1_000_000);
        }
        assert_eq!(f.samples, 5);
        assert_eq!(f.frames.len(), 1, "five identical samples are one frame");
        assert_eq!(f.frames[0].samples, 5);
        assert_eq!(f.frames[0].first_at, 0);
        assert_eq!(f.frames[0].last_at, 4_000_000);
        assert_eq!(f.frames[0].nonblack, 8);
        assert_eq!(std::fs::read_dir(&dir).unwrap().count(), 1, "one PNG on disk");
    }

    /// A picture that comes back is a new frame with its own span, pointing at the file it already
    /// wrote — so "the menu returned" is readable and costs no disk.
    #[test]
    fn a_returning_picture_is_a_new_frame_reusing_the_old_file() {
        let dir = tmp("return");
        let mut f = Film::parse(&format!("0x1000:4:2:1M:{}", dir.display())).unwrap();
        let a = bcm_with(0x1000, &[0xffff; 8]);
        let b = bcm_with(0x1000, &[0x0000; 8]);
        f.sample(&a, 0);
        f.sample(&b, 1_000_000);
        f.sample(&a, 2_000_000);
        assert_eq!(f.frames.len(), 3);
        assert_eq!(f.frames[2].repeat_of, Some(0));
        assert_eq!(f.frames[2].file, f.frames[0].file);
        assert_eq!(f.frames[2].first_at, 2_000_000);
        let pngs = std::fs::read_dir(&dir).unwrap().count();
        assert_eq!(pngs, 2, "three frames, two distinct pictures, two files");
        let summary = f.finish();
        assert!(summary.contains("= frame 0 again"), "{summary}");
        assert!(dir.join("frames.tsv").exists());
    }

    /// Pixels come out where they went in, and an odd address inside the range is not folded into
    /// a pixel. The surface is halfword-granular; rounding would invent one.
    #[test]
    fn the_surface_is_read_at_halfword_granularity_and_zero_where_nothing_was_written() {
        let mut b = bcm_with(0x1000, &[0x1234, 0x5678]);
        b.mem.insert(0x1005, 0xdead); // odd, inside the range
        let px = read_surface(&b, 0x1000, 2, 2);
        assert_eq!(px, vec![0x1234, 0x5678, 0, 0]);
    }

    /// The converter must agree with `--bcm-dump`'s, or a film frame and a dump of the same instant
    /// would differ in their low bits and nobody would notice until they were compared.
    #[test]
    fn rgb565_expands_the_way_bcm_dump_expands_it() {
        assert_eq!(to_rgb888(&[0xffff]), vec![0xff, 0xff, 0xff]);
        assert_eq!(to_rgb888(&[0x0000]), vec![0x00, 0x00, 0x00]);
        assert_eq!(to_rgb888(&[0xf800]), vec![0xff, 0x00, 0x00]);
        assert_eq!(to_rgb888(&[0x07e0]), vec![0x00, 0xff, 0x00]);
        assert_eq!(to_rgb888(&[0x001f]), vec![0x00, 0x00, 0xff]);
    }

    #[test]
    fn counts_take_the_same_suffixes_the_wheel_script_takes() {
        assert_eq!(parse_count("2M"), Some(2_000_000));
        assert_eq!(parse_count("500k"), Some(500_000));
        assert_eq!(parse_count("2_000_000"), Some(2_000_000));
        assert_eq!(parse_count("x"), None);
    }

    fn tmp(tag: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("ipod-film-test-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }
}
