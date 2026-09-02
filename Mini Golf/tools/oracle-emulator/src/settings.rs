//! What the window remembers between launches, and the three directories it remembers it in.
//!
//! Deliberately a hand-written `key = value` file rather than eframe's `persistence` feature. That
//! feature pulls `serde` and `ron` into a crate whose whole dependency argument is "eframe and
//! nothing else", and it persists egui's own window geometry as a side effect, which is state this
//! project has no opinion about. Four keys do not need a serialiser.
//!
//! # Where the directories are, and why not `$TMPDIR` for all of them
//!
//! The snapshot cache used to live under [`std::env::temp_dir`]. That is correct on macOS, where
//! `$TMPDIR` is `/var/folders/…` on a real disk, and wrong on Linux, where `/tmp` is `tmpfs` on
//! most distributions — RAM-backed and typically capped at half of it. The cache holds a snapshot
//! **and an 8 GB working disk image**, so on a Linux box the old default either filled `/tmp` or
//! ate half the machine's memory. It is a cache directory now, on all three platforms.

use std::path::{Path, PathBuf};

/// The two ways the window can be looked at.
///
/// One toggle, not a pile of checkboxes: the choice is between "this is an iPod" and "this is an
/// instrument", and every counter on screen belongs to the second.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Mode {
    /// The iPod and nothing else. The default, and what a stranger who cloned this should meet.
    #[default]
    User,
    /// Instruction counts, both clocks, the task watches, the ATA census, the framebuffer
    /// inspector, the screenshot button.
    Debug,
}

impl Mode {
    fn as_str(self) -> &'static str {
        match self {
            Mode::User => "user",
            Mode::Debug => "debug",
        }
    }

    fn parse(s: &str) -> Option<Mode> {
        match s.trim() {
            "user" => Some(Mode::User),
            "debug" => Some(Mode::Debug),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Settings {
    pub mode: Mode,
    /// The NOR dump and the drive image the setup screen was pointed at. Absent until somebody
    /// picks them, which is the state a fresh clone is in.
    pub flash: Option<PathBuf>,
    pub disk: Option<PathBuf>,
    /// Which of the two colours the 5G shipped in. Not an instrument — it is which iPod you had,
    /// so it lives in user mode and is remembered like the rest of the setup.
    pub black_device: bool,
    /// **Off unless asked for.** An emulator that phones home on launch is a bad first impression
    /// for no benefit, and this audience notices. The menu item works either way — this only
    /// decides whether the check happens on its own.
    pub check_updates_on_start: bool,
    /// Run on a copy of the drive image rather than on the image itself.
    ///
    /// **`None` means "nobody has said", and that is not the same as "no".** With no answer the
    /// choice is made from where the drive came from: one this program built from a bundle is
    /// regenerable byte for byte, so writing to it costs nothing; one the user supplied might be
    /// the only image of an iPod they own, and defaulting to writing on it is how an afternoon
    /// disappears. `Some` is a person's explicit choice and is obeyed for either kind.
    pub work_on_copy: Option<bool>,
}

impl Settings {
    /// Read the settings file, tolerating everything: a missing file, a missing directory, a
    /// truncated write, a key from a future version. A settings file is not a place to fail.
    pub fn load() -> Settings {
        match std::fs::read_to_string(data_dir().join(FILE)).ok() {
            Some(text) => Settings::parse(&text),
            None => Settings::default(),
        }
    }

    pub fn parse(text: &str) -> Settings {
        let mut s = Settings::default();
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let Some((k, v)) = line.split_once('=') else { continue };
            let (k, v) = (k.trim(), v.trim());
            match k {
                "mode" => s.mode = Mode::parse(v).unwrap_or_default(),
                // Empty is "not set", which is what an editor that blanked a line means.
                "flash" if !v.is_empty() => s.flash = Some(PathBuf::from(v)),
                "disk" if !v.is_empty() => s.disk = Some(PathBuf::from(v)),
                "black_device" => s.black_device = v == "true",
                "check_updates_on_start" => s.check_updates_on_start = v == "true",
                "work_on_copy" => s.work_on_copy = Some(v == "true"),
                _ => {}
            }
        }
        s
    }

    pub fn render(&self) -> String {
        let p = |o: &Option<PathBuf>| {
            o.as_ref().map(|p| p.to_string_lossy().into_owned()).unwrap_or_default()
        };
        format!(
            "# ipod-gui settings. Hand-editable; unknown keys are ignored.\n\
             mode = {}\n\
             black_device = {}\n\
             flash = {}\n\
             disk = {}\n\
             # An HTTPS GET of the GitHub releases API and a version comparison, on launch.\n\
             # Off by default on purpose. The menu item works whatever this says.\n\
             check_updates_on_start = {}\n\
             # Run on a COPY of the drive, leaving the original untouched. Absent means \"decide\n\
             # from where the drive came from\": a drive this program built is written to directly,\n\
             # one you supplied is copied. Set it to true or false to answer for both.\n\
             {}",
            self.mode.as_str(),
            self.black_device,
            p(&self.flash),
            p(&self.disk),
            self.check_updates_on_start,
            match self.work_on_copy {
                Some(v) => format!("work_on_copy = {v}\n"),
                None => String::new(),
            },
        )
    }

    /// Best-effort. A window that could not write its preferences is a window that opens in user
    /// mode next time, not a window that refuses to run.
    pub fn save(&self) {
        let dir = data_dir();
        if std::fs::create_dir_all(&dir).is_err() {
            return;
        }
        let _ = std::fs::write(dir.join(FILE), self.render());
    }

    /// Where the settings live, for the UI to print. A preference nobody can find is a preference
    /// nobody can reset.
    pub fn path() -> Option<PathBuf> {
        Some(data_dir().join(FILE))
    }
}

const FILE: &str = "settings.txt";

const APP: &str = "ipod-emulator";
/// What the directory was called before 2026-08-17. Read once, to move a user's settings forward
/// rather than silently starting them over.
const APP_WAS: &str = "ipod-gui";

fn env_dir(key: &str) -> Option<PathBuf> {
    std::env::var_os(key).filter(|v| !v.is_empty()).map(PathBuf::from)
}

/// **One directory for everything this program writes**, and where it is depends on how the program
/// was delivered.
///
/// It used to be two — settings in the config directory, an 8 GB working disk and a 1.6 GB snapshot
/// in the cache directory — which on Windows meant `AppData\Roaming` *and* `AppData\Local`. A
/// reverse-engineering tool shipped as a zip is expected to stay where you put it, and that
/// expectation is right: somebody trying four firmware versions filled a drive they were not
/// watching, on a volume the program was not installed on.
///
/// - **Beside the executable**, in `data/`, when that directory can be written. This is the case for
///   a zip a user unpacked, which is how Windows and Linux get it, and it means deleting the folder
///   deletes everything.
/// - **The platform's application-support directory** otherwise — which is what a macOS `.app`
///   dragged to `/Applications` gets, because writing inside a bundle breaks its signature and the
///   volume may be read-only.
///
/// `IPOD_EMULATOR_DATA` overrides both, and is what the setup screen's "change" button sets.
pub fn data_dir() -> PathBuf {
    if let Some(d) = env_dir("IPOD_EMULATOR_DATA") {
        return d;
    }
    if let Some(beside) = beside_executable() {
        return beside;
    }
    platform_dir().unwrap_or_else(|| std::env::temp_dir().join(APP))
}

/// Is this executable sitting in a cargo build tree?
///
/// Data must not go beside it if so. A build tree is disposable by definition — `cargo clean`
/// deletes it without asking — and on this workspace one target directory is shared by every crate,
/// so it is also the last place that should quietly gain 8 GB. Measured, not imagined: running the
/// binary from `.cargo-target/release` put 6.4 GB in `.cargo-target/release/data`.
///
/// The test is a `debug`/`release` leaf under a `target`-ish ancestor at any depth, because
/// cross-compiling inserts the triple: `target/x86_64-pc-windows-gnu/release/`. A *released* build
/// unpacked into a directory the user happens to have called `release` does not match, since
/// nothing above it is a target directory — and that case is the whole point of beside-the-
/// executable, so it has to keep working.
fn in_build_tree(exe: &std::path::Path) -> bool {
    let Some(parent) = exe.parent() else { return false };
    let leaf = parent.file_name().and_then(|n| n.to_str());
    if !matches!(leaf, Some("debug" | "release")) {
        return false;
    }
    parent
        .ancestors()
        .skip(1)
        .filter_map(|a| a.file_name().and_then(|n| n.to_str()))
        .any(|n| n == "target" || n.ends_with("-target"))
}

/// `data/` next to the running binary, if it is a directory we may write to.
///
/// Probed rather than assumed: a bundle's `Contents/MacOS`, `/usr/local/bin` and a read-only mount
/// all look like ordinary paths until the write fails.
fn beside_executable() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    // Inside a macOS bundle this would put user data in `Contents/MacOS/data`, which is both wrong
    // and signature-breaking. Recognise it and decline.
    if exe.components().any(|c| c.as_os_str().to_string_lossy().ends_with(".app")) {
        return None;
    }
    if in_build_tree(&exe) {
        return None;
    }
    let dir = exe.parent()?.join("data");
    std::fs::create_dir_all(&dir).ok()?;
    // create_dir_all succeeds on a directory that already exists and is read-only, so prove it.
    let probe = dir.join(".writable");
    std::fs::write(&probe, b"").ok()?;
    let _ = std::fs::remove_file(&probe);
    Some(dir)
}

fn platform_dir() -> Option<PathBuf> {
    if cfg!(windows) {
        return env_dir("APPDATA").map(|d| d.join(APP));
    }
    if cfg!(target_os = "macos") {
        return home().map(|h| h.join("Library/Application Support").join(APP));
    }
    env_dir("XDG_CONFIG_HOME")
        .or_else(|| home().map(|h| h.join(".config")))
        .map(|d| d.join(APP))
}

/// The two directories the old build used, so their contents can be carried forward once.
fn legacy_dirs() -> Vec<PathBuf> {
    let mut v = Vec::new();
    if cfg!(windows) {
        v.extend(env_dir("APPDATA").map(|d| d.join(APP_WAS)));
        v.extend(env_dir("LOCALAPPDATA").map(|d| d.join(APP_WAS)));
    } else if cfg!(target_os = "macos") {
        if let Some(h) = home() {
            v.push(h.join("Library/Application Support").join(APP_WAS));
            v.push(h.join("Library/Caches").join(APP_WAS));
        }
    } else {
        v.extend(env_dir("XDG_CONFIG_HOME").or_else(|| home().map(|h| h.join(".config"))).map(|d| d.join(APP_WAS)));
        v.extend(env_dir("XDG_CACHE_HOME").or_else(|| home().map(|h| h.join(".cache"))).map(|d| d.join(APP_WAS)));
    }
    v
}

/// Move a previous installation's settings file into the new directory, once.
///
/// Only the settings file. The old snapshots and working disks are gigabytes and keyed on paths
/// that may no longer exist; they are reported to the user for deletion rather than copied.
pub fn migrate_legacy() {
    let dir = data_dir();
    if dir.join(FILE).exists() {
        return;
    }
    for old in legacy_dirs() {
        let f = old.join(FILE);
        if f.is_file() {
            let _ = std::fs::create_dir_all(&dir);
            let _ = std::fs::copy(&f, dir.join(FILE));
            return;
        }
    }
}

/// Old directories that still exist and still hold bytes, for the setup screen to offer to delete.
/// Returns each with its total size.
pub fn legacy_leftovers() -> Vec<(PathBuf, u64)> {
    legacy_dirs()
        .into_iter()
        .filter(|d| d.is_dir())
        .map(|d| {
            let n = dir_size(&d);
            (d, n)
        })
        .filter(|(_, n)| *n > 0)
        .collect()
}

/// Total bytes in a directory, including what is nested inside it.
///
/// **It used to stop at the first level**, with a comment saying that was all these directories
/// ever held — which was true until built drives moved into `drives/`. A figure that skipped the
/// largest files in the folder, on the screen whose whole job is telling somebody what this program
/// is costing them, is the kind of wrong that reads as reassuring.
pub fn dir_size(d: &Path) -> u64 {
    let Ok(rd) = std::fs::read_dir(d) else { return 0 };
    rd.flatten()
        .map(|e| match e.metadata() {
            Ok(m) if m.is_dir() => dir_size(&e.path()),
            Ok(m) => on_disk_size(&m),
            Err(_) => 0,
        })
        .sum()
}

/// What a file actually costs, rather than how long it claims to be.
///
/// The drive images here are **sparse** and, on APFS, **clones**: `clone_disk` copies with `cp -c`,
/// so every block the emulator has not written is shared with the source rather than duplicated.
/// `len()` reports the logical 8 GB regardless, and summing that told the operator the cache had
/// reached 32 GB. It had not. Deleting one whole set — two 3.1 GB drive files and a snapshot, 6.4 GB
/// by that reckoning — returned **153 MB** of real disk, which is the snapshot and almost nothing
/// else.
///
/// A number that is wrong by a factor of forty, in the direction of alarm, about the user's own
/// disk, is not a cosmetic defect. `st_blocks` is in 512-byte units by POSIX definition, whatever
/// the filesystem's own block size is.
pub fn on_disk_size(m: &std::fs::Metadata) -> u64 {
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        m.blocks() * 512
    }
    #[cfg(not(unix))]
    {
        m.len()
    }
}

fn home() -> Option<PathBuf> {
    env_dir("HOME").or_else(|| env_dir("USERPROFILE"))
}

#[cfg(test)]
mod tests {
    /// A build tree is refused; a real install beside the executable is not.
    ///
    /// The second half matters as much as the first. Declining too eagerly would send a user who
    /// unpacked the release tarball into `~/Downloads/ipod-emulator/` off to Application Support
    /// instead, which is the split that produced two data folders and a bug report.
    #[test]
    fn data_never_lands_in_a_build_tree() {
        use std::path::Path;
        for p in [
            "/Users/x/dev/target/release/ipod-emulator",
            "/Users/x/dev/target/debug/ipod-emulator",
            // The shared workspace target, which is what this was found in.
            "/Users/x/dev/.cargo-target/release/ipod-emulator",
            // Cross-compiling inserts the triple, so the ancestor walk has to go deeper than one.
            "/Users/x/dev/target/x86_64-pc-windows-gnu/release/ipod-emulator.exe",
            "/Users/x/dev/.cargo-target/aarch64-apple-darwin/debug/ipod-emulator",
        ] {
            assert!(in_build_tree(Path::new(p)), "must refuse: {p}");
        }
        for p in [
            // An unpacked release. The directory is called `release`, but nothing above it is a
            // target directory, and putting data here is the entire feature.
            "/Users/x/Downloads/ipod-emulator-0.4.0-release/ipod-emulator",
            "/Users/x/Downloads/ipod-emulator/ipod-emulator",
            "/Applications/ipod-emulator.app/Contents/MacOS/ipod-emulator",
            "/usr/local/bin/ipod-emulator",
        ] {
            assert!(!in_build_tree(Path::new(p)), "must allow: {p}");
        }
    }

    use super::*;

    #[test]
    fn a_missing_file_is_user_mode_with_nothing_configured() {
        let s = Settings::parse("");
        assert_eq!(s.mode, Mode::User);
        assert_eq!(s.flash, None);
        assert_eq!(s.disk, None);
        assert!(!s.check_updates_on_start);
    }

    /// The round trip is the contract: whatever the window writes, the next launch reads back.
    #[test]
    fn settings_round_trip_through_the_file_format() {
        let s = Settings {
            mode: Mode::Debug,
            black_device: true,
            flash: Some(PathBuf::from("/a/b/rom.bin")),
            disk: Some(PathBuf::from("/a/b/disk.img")),
            check_updates_on_start: true,
            work_on_copy: Some(true),
        };
        assert_eq!(Settings::parse(&s.render()), s);
    }

    /// A path with a space in it must survive the format, which is why the value is the rest of
    /// the line and not a token. Spaces in a chosen path are ordinary, not exotic.
    #[test]
    fn a_path_with_spaces_survives() {
        let p = PathBuf::from("/some where/My iPod Backups/x.img");
        let s = Settings { disk: Some(p.clone()), ..Default::default() };
        assert_eq!(Settings::parse(&s.render()).disk, Some(p));
    }

    /// Garbage in a settings file must not stop the program. Every one of these lines is something
    /// a hand-edit produces.
    #[test]
    fn junk_is_ignored_rather_than_fatal() {
        let s = Settings::parse(
            "# comment\n\
             \n\
             mode = sideways\n\
             nonsense\n\
             future_key = 7\n\
             disk =\n\
             check_updates_on_start = maybe\n",
        );
        assert_eq!(s.mode, Mode::User, "an unknown mode falls back to the default");
        assert_eq!(s.disk, None, "an empty value is `not set`, not `the empty path`");
        assert!(!s.check_updates_on_start, "anything but `true` is off");
    }

    #[test]
    fn the_data_directory_is_absolute() {
        let d = data_dir();
        assert!(d.is_absolute(), "{}", d.display());
    }

    /// `IPOD_EMULATOR_DATA` is what the setup screen's "change" button sets, so it has to win over
    /// both the beside-the-executable default and the platform directory.
    #[test]
    fn the_override_wins() {
        // SAFETY: single-threaded test, and the value is restored before it returns.
        let before = std::env::var_os("IPOD_EMULATOR_DATA");
        unsafe { std::env::set_var("IPOD_EMULATOR_DATA", "/tmp/ipod-emulator-test-dir") };
        assert_eq!(data_dir(), PathBuf::from("/tmp/ipod-emulator-test-dir"));
        match before {
            Some(v) => unsafe { std::env::set_var("IPOD_EMULATOR_DATA", v) },
            None => unsafe { std::env::remove_var("IPOD_EMULATOR_DATA") },
        }
    }
}

/// This repository's root, or the working directory when there is no repository — which is what a
/// released binary is always in.
///
/// Two walks, and deliberately no compile-time path. The obvious third fallback is
/// `env!("CARGO_MANIFEST_DIR")`, and it was one here: it is the absolute path of the machine that
/// did the build, so it is baked into every published binary (naming a stranger's home directory)
/// and it is wrong on every machine but that one. `--remap-path-prefix` cannot reach it, because it
/// is a cargo variable rather than a path `rustc` embeds.
///
/// - **Up from the executable**, which is right for `target/release/ipod-boot` in a checkout.
/// - **Up from the working directory**, which is what catches a shared `CARGO_TARGET_DIR` — the
///   binary is then nowhere near the source, and this is the case the compile-time path used to
///   cover.
/// - Otherwise the working directory, so the paths built from it are somewhere the user can see,
///   and the "no NOR dump at …" message names a plausible place rather than a build machine.
pub fn repo_root() -> PathBuf {
    /// The directory that holds the recipes — present in a checkout, absent in a release archive.
    const MARKER: &str = "tools/ipod-boot";

    if let Ok(exe) = std::env::current_exe() {
        let mut p = exe.as_path();
        while let Some(dir) = p.parent() {
            if dir.join(MARKER).is_dir() {
                return dir.to_path_buf();
            }
            p = dir;
        }
    }
    if let Ok(cwd) = std::env::current_dir() {
        let mut p = cwd.as_path();
        loop {
            if p.join(MARKER).is_dir() {
                return p.to_path_buf();
            }
            match p.parent() {
                Some(up) => p = up,
                None => break,
            }
        }
        return cwd;
    }
    PathBuf::from(".")
}
