//! `ipod-boot` — the boot recipes, as a program rather than as seven shell scripts.
//!
//! ```text
//! ipod-boot retail   [trace flags…]     # the recipe every number in research/ is measured on
//! ipod-boot cold     [trace flags…]
//! ipod-boot warm     [trace flags…]
//! ipod-boot flsh     [trace flags…]
//! ipod-boot rockbox  [trace flags…]
//! ipod-boot flash-update [trace flags…]
//! ipod-boot from-idle    [trace flags…]
//! ```
//!
//! # Why this exists, and what it is not
//!
//! `tools/ipod-boot/*.sh` are bash, and bash is not on a stock Windows. They are also the recipes
//! **every measurement in `research/` was taken through**, which makes rewriting them the one
//! change here with no upside and a large downside: a recipe that has drifted from the one a number
//! was measured on turns that number into a claim about a machine nobody can rebuild.
//!
//! So the scripts are untouched and this is a **second front end that composes the same argv**. Not
//! a port, not a replacement, not a wrapper that shells out to `bash` — the same list of arguments,
//! in the same order, handed to the same `trace` binary. `--print` shows the list without running
//! it, and `recipe_flags_match_the_shell_scripts` in this file's tests reads the `.sh` files off
//! disk and asserts that the flags each recipe passes are the flags the script passes, in order. If
//! somebody edits one and not the other, `cargo test` says so.
//!
//! # Two deliberate differences from the scripts, both documented rather than hidden
//!
//! - **`TRACE` defaults to the `trace` binary beside this one**, not to
//!   `$HOME/dev/.cargo-target/release/trace`. The scripts' default is one machine's target
//!   directory; a released binary cannot have that. `cargo build --release` puts `ipod-boot` and
//!   `trace` in the same directory, so the sibling is right by construction. `TRACE=` still wins.
//! - **The disk clone tries three copies, not one.** `cp -c` is an APFS clone and Linux `cp` does
//!   not have the flag; `cp --reflink=auto` is the btrfs/XFS equivalent and macOS `cp` does not
//!   have *that*. Both are attempted, in that order, before a full 8 GB byte copy. See
//!   [`clone_file`].
//!
//! Everything else — the defaults, the environment variables, the flags, the order — is the
//! scripts'. `research/` numbers are reproducible through either.

use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let argv: Vec<String> = std::env::args().skip(1).collect();
    let Some(name) = argv.first() else {
        usage();
        std::process::exit(2);
    };
    if name == "-h" || name == "--help" {
        usage();
        return;
    }
    // `--print` anywhere means "compose the argv and show it, run nothing". The recipes that build
    // something first (a disk clone, a snapshot) do not build it either.
    let dry = argv.iter().any(|a| a == "--print");
    let rest: Vec<String> = argv[1..].iter().filter(|a| *a != "--print").cloned().collect();

    // The same two questions the window's setup screen asks, on stdin, for a machine with no
    // window to open one on — a headless Linux box, or an ssh session. Writes the same file the
    // window does, so either one sets up both.
    if name == "setup" {
        match setup() {
            Ok(()) => return,
            Err(e) => {
                eprintln!("ipod-boot setup: {e}");
                std::process::exit(1);
            }
        }
    }

    // Also not a recipe: it reads the NOR rather than booting it, and prints the identity of the
    // iPod the dump came from. That identity is what `ipod-usb` has to present to iTunes, because
    // authorisation keys minted against any other one are keys this machine cannot use.
    if name == "syscfg" {
        let Some(path) = rest.first() else {
            eprintln!("usage: ipod-boot syscfg NOR.bin");
            std::process::exit(2);
        };
        let nor = match std::fs::read(path) {
            Ok(b) => b,
            Err(e) => {
                eprintln!("ipod-boot syscfg: {path}: {e}");
                std::process::exit(1);
            }
        };
        let Some(c) = eapp_loader::inspect::syscfg(&nor) else {
            eprintln!(
                "ipod-boot syscfg: {path} has no SysCfg block at 0x4000.\n\
                 A 5G/5.5G NOR dump is 1 MiB and starts with the boot ROM; a file that is neither \
                 will land here."
            );
            std::process::exit(1);
        };
        println!("{path}");
        println!("  serial  {}", c.serial.as_deref().unwrap_or("(absent)"));
        match c.guid_hex() {
            Some(g) => println!("  GUID    {g}"),
            None => println!("  GUID    (absent)"),
        }
        println!("  records {}", c.tags.join(", "));
        if !c.guid_looks_apple() {
            // Said rather than assumed correct: the OUI is the one field whose right answer is
            // known in advance, so it is the only check available that this parsed at all.
            println!();
            println!("  warning: the GUID does not carry Apple's FireWire OUI (00:0A:27).");
            println!("           Either this is not an iPod NOR, or it did not parse.");
        }
        return;
    }

    // `make-disk` is not a recipe — it builds the thing a recipe needs, and it is the front door
    // for anyone who does not already have an 8 GB image of an iPod.
    if name == "make-disk" {
        match make_disk(&rest) {
            Ok(()) => return,
            Err(e) => {
                eprintln!("ipod-boot make-disk: {e}");
                std::process::exit(1);
            }
        }
    }

    // `install-os` puts somebody else's operating system where Apple's bootloader will find it,
    // and cold-boots exactly as the hardware does — the alternative being to warm-enter an image
    // and skip the bootloader, which is a bypass and would make every later observation
    // un-attributable. See docs/ideas/run-any-os.md.
    if name == "install-os" {
        match install_os(&rest) {
            Ok(()) => return,
            Err(e) => {
                eprintln!("ipod-boot install-os: {e}");
                std::process::exit(1);
            }
        }
    }

    // The other half of installing an OS: the firmware partition holds what the bootloader runs,
    // the data partition holds everything that bootloader then looks for.
    if name == "put-files" {
        match put_files(&rest) {
            Ok(()) => return,
            Err(e) => {
                eprintln!("ipod-boot put-files: {e}");
                std::process::exit(1);
            }
        }
    }

    let Some(recipe) = Recipe::parse(name) else {
        eprintln!("unknown recipe `{name}`\n");
        usage();
        std::process::exit(2);
    };

    match run(recipe, &rest, dry) {
        Ok(code) => std::process::exit(code),
        Err(e) => {
            eprintln!("ipod-boot {name}: {e}");
            std::process::exit(1);
        }
    }
}

fn usage() {
    eprintln!(
        "\
ipod-boot — Apple's firmware, booted under the emulator

  ipod-boot retail        [flags…]   Apple's shipping 5G bootloader + the image it accepts.
                                     Every current number in research/ is measured on this one.
  ipod-boot warm          [flags…]   RetailOS entered directly at 0x10000000, handoff faked
  ipod-boot flsh          [flags…]   one of the NOR's own images: IMG=diag|disk|scan|logo|vmcs
  ipod-boot rockbox       [flags…]   Rockbox as a source-available oracle
  ipod-boot flash-update  [flags…]   Apple's `aupd` updater, then the boot that proves it took
  ipod-boot from-idle     [flags…]   restore a cached 1.6 G snapshot: 3 s instead of 110 s

  ipod-boot setup                    ask for the two files and remember them, on stdin. The
                                     window's setup screen asks the same two and writes the same
                                     file; this is for a machine with no window to open.

  ipod-boot make-disk IPSW OUT.img [SECTORS]
                                     build a bootable drive image from an IPSW. This is the way
                                     to get a disk if you do not already have one: an IPSW is
                                     ~14 MB against 8 GB, it is Apple's own firmware rather than
                                     an image of somebody's iPod, and RetailOS builds the rest of
                                     the volume itself on first boot. SECTORS defaults to
                                     16777216 (8 GiB); the file is sparse.

  --print                            compose the argv, print it, run nothing
  -h, --help                         this

Everything else is passed through to `trace` unchanged.

The NOR dump and the drive come from `ipod-gui`'s setup screen unless you say otherwise, so
setting them up once in the window is enough for every recipe here. `--print` says where each
one came from: environment, setup screen, or repository default.

Environment, all optional and all the same as the shell recipes in tools/ipod-boot/:
  TRACE     the emulator binary            (default: the `trace` beside this program)
  BUDGET    instruction budget             (per recipe: 150M cold, 4G is the usual for retail)
  FLASH     the NOR dump                   (default: what the setup screen was pointed at)
  DISK      the drive image                (default: what the setup screen was pointed at)
  WORKDISK  keep the writable clone across runs instead of a per-run temporary
  OSOS      warm only — the RetailOS image
  IMG       flsh / rockbox — which image
  SNAP_AT   from-idle — where the snapshot is taken       (default 1600000000)
  CACHE     from-idle — where snapshots live              (default <tempdir>/ipod-from-idle)

The shell recipes in tools/ipod-boot/ remain the reference. This program composes the same argv;
`--print` shows it, and a test asserts the two agree."
    );
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Recipe {
    Retail,
    Warm,
    Flsh,
    Rockbox,
    FlashUpdate,
    FromIdle,
}

impl Recipe {
    fn parse(s: &str) -> Option<Recipe> {
        Some(match s {
            "retail" | "retail-boot" => Recipe::Retail,
            "warm" | "warm-boot" => Recipe::Warm,
            "flsh" => Recipe::Flsh,
            "rockbox" => Recipe::Rockbox,
            "flash-update" => Recipe::FlashUpdate,
            "from-idle" => Recipe::FromIdle,
            _ => return None,
        })
    }

    /// The `.sh` this recipe is the second front end for. Used by the drift test, and only by it —
    /// the mapping is the test's premise, so it lives beside the recipes rather than inside
    /// `#[cfg(test)]` where a reader would have to go looking for it.
    #[cfg_attr(not(test), allow(dead_code))]
    fn script(self) -> &'static str {
        match self {
            Recipe::Retail => "retail-boot.sh",
            Recipe::Warm => "warm-boot.sh",
            Recipe::Flsh => "flsh.sh",
            Recipe::Rockbox => "rockbox.sh",
            Recipe::FlashUpdate => "flash-update.sh",
            Recipe::FromIdle => "from-idle.sh",
        }
    }
}

// ---------------------------------------------------------------- where things are


fn env_path(key: &str) -> Option<PathBuf> {
    std::env::var_os(key).filter(|v| !v.is_empty()).map(PathBuf::from)
}

/// Where a path came from. Carried so `--print` can say, because a recipe whose behaviour depends
/// on a file you cannot see in the command line is a recipe you cannot reason about — and this
/// program's whole claim is that `--print` shows exactly what ran.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum From {
    /// `FLASH=` / `DISK=` in the environment.
    Env,
    /// What the setup screen was pointed at, in `ipod-gui`'s settings file.
    Setup,
    /// This repository's own layout. Only ever right in a source tree.
    Repo,
}

impl From {
    fn label(self) -> &'static str {
        match self {
            From::Env => "environment",
            From::Setup => "setup screen",
            From::Repo => "repository default",
        }
    }
}

/// `FLASH=`/`DISK=` wins, then whatever the setup screen was pointed at, then this repository's
/// layout.
///
/// The middle one is the point: `ipod-gui`'s setup screen asks for these two files, validates them,
/// and remembers them. Before this, it remembered them only for itself — you could complete setup
/// in the window and every shell recipe would still fail. The file is `eapp_loader::settings`,
/// which is a plain `key = value` file with no dependencies, which is why this binary can read it.
fn resolve(env_key: &str, from_setup: Option<PathBuf>, repo_default: PathBuf) -> (PathBuf, From) {
    if let Some(p) = env_path(env_key) {
        return (p, From::Env);
    }
    // A remembered path that has since been deleted is not a usable answer, and falling through to
    // the repository default gives a better error than "no such file" against a stale one.
    if let Some(p) = from_setup.filter(|p| p.exists()) {
        return (p, From::Setup);
    }
    (repo_default, From::Repo)
}

fn env_u64(key: &str, default: u64) -> u64 {
    std::env::var(key).ok().and_then(|v| v.trim().parse().ok()).unwrap_or(default)
}

/// The `trace` beside this binary. `cargo build --release` puts both in the same directory, and a
/// release archive ships them side by side, so "the sibling" is correct by construction — where
/// `$HOME/dev/.cargo-target/release/trace`, the shell default, is correct on exactly one machine.
fn default_trace() -> PathBuf {
    let exe_name = if cfg!(windows) { "trace.exe" } else { "trace" };
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            let sibling = dir.join(exe_name);
            if sibling.is_file() {
                return sibling;
            }
        }
    }
    PathBuf::from(exe_name)
}

/// A path that must exist before anything is spawned, so the failure names the file rather than
/// arriving 40 lines into `trace`'s output as a read error.
fn require(p: &Path, what: &str) -> Result<(), String> {
    if p.exists() {
        return Ok(());
    }
    // The old message explained that `resources/` is gitignored. That is the repository's mental
    // model, and someone who unpacked a release has no `resources/` and never will — so it named a
    // directory that does not exist and gave them nothing to do. Say what to do instead.
    let where_settings = eapp_loader::settings::Settings::path()
        .map(|p| format!("\n  Remembered in {}.", p.display()))
        .unwrap_or_default();
    Err(format!(
        "no {what} at {}\n\n\
         Apple's firmware is not in this program and never will be, so it needs two files from \
         you: a 1 MB NOR dump, and either an .ipsw or a drive image.\n\n\
         Run `ipod-gui` — its setup screen asks for both, says what each file actually is, and \
         builds a drive from an .ipsw. Every recipe here then uses what you picked.{where_settings}\n\
         Or name them here: FLASH=/path/to/nor.bin DISK=/path/to/disk.img ipod-boot <recipe>",
        p.display()
    ))
}

// ---------------------------------------------------------------- copying an 8 GB disk image

/// Clone `from` to `to`, using the filesystem's copy-on-write if it has one.
///
/// Three rungs, and the order is the point:
///
/// 1. **`cp -c`** — APFS `clonefile(2)`. ~3 ms for 8 GB, and the reason a fresh disk per run is
///    free on a Mac. Not a GNU flag; on Linux `cp -c` is an error, so it falls through.
/// 2. **`cp --reflink=auto`** — the btrfs / XFS / bcachefs equivalent, and the rung that was
///    missing: without it a Linux run paid a full 8 GB byte copy *per boot*. GNU `cp` with
///    `--reflink=auto` never fails for want of reflink support; it silently does a full copy. So
///    reaching rung 3 means neither `cp` exists at all.
/// 3. **`std::fs::copy`** — Windows, and any Unix without a usable `cp`.
///
/// Windows skips straight to rung 3: `cp` is not there, and ReFS block cloning is an `FSCTL` this
/// program is not going to reach for. `WORKDISK=` is the answer on a filesystem with no clone —
/// pay the copy once and keep the image.
fn clone_file(from: &Path, to: &Path) -> Result<(), String> {
    if to.exists() {
        return Ok(());
    }
    if let Some(dir) = to.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    if cfg!(unix) {
        for flag in ["-c", "--reflink=auto"] {
            let ok = Command::new("cp")
                .arg(flag)
                .arg(from)
                .arg(to)
                .status()
                .map(|s| s.success())
                .unwrap_or(false);
            if ok {
                return Ok(());
            }
            // A failed `cp` can still have created a truncated destination; a partial 8 GB disk
            // image that later reads as a valid file is exactly the silent failure this project
            // keeps paying for.
            let _ = std::fs::remove_file(to);
        }
    }
    std::fs::copy(from, to)
        .map(|_| ())
        .map_err(|e| format!("copying {} -> {}: {e}", from.display(), to.display()))
}

// ---------------------------------------------------------------- the recipes

/// The argv a recipe hands `trace`, and the temporary it wants removed afterwards.
struct Plan {
    trace: PathBuf,
    /// Where the NOR dump and the drive came from, for `--print` to state. Not used to run
    /// anything — it exists so the config cannot be invisible.
    sources: Vec<(&'static str, PathBuf, From)>,
    /// One entry per `trace` invocation. `flash-update` boots twice, on purpose.
    runs: Vec<Vec<String>>,
    /// Deleted after the last run. The per-run disk clone, when it is not a `WORKDISK`.
    cleanup: Option<PathBuf>,
}

fn run(recipe: Recipe, user: &[String], dry: bool) -> Result<i32, String> {
    let plan = plan(recipe, user, dry)?;
    if dry {
        for r in &plan.runs {
            println!("{}", shell_quote(&plan.trace, r));
        }
        // Say where each path came from. Two of them can now come from a settings file, and a
        // recipe with an input you cannot see in its command line is one you cannot reason about.
        for (what, path, from) in &plan.sources {
            println!("# {what}: {} — {}", path.display(), from.label());
        }
        return Ok(0);
    }
    require(&plan.trace, "trace binary")?;

    let mut code = 0;
    for (i, args) in plan.runs.iter().enumerate() {
        if plan.runs.len() > 1 {
            println!("===== boot {} =====", i + 1);
        }
        let status = Command::new(&plan.trace)
            .args(args)
            .status()
            .map_err(|e| format!("{}: {e}", plan.trace.display()))?;
        code = status.code().unwrap_or(1);
        if code != 0 {
            break;
        }
    }
    if let Some(p) = plan.cleanup {
        let _ = std::fs::remove_file(p);
    }
    Ok(code)
}

fn plan(recipe: Recipe, user: &[String], dry: bool) -> Result<Plan, String> {
    let root = eapp_loader::settings::repo_root();
    let res = root.join("resources");
    let trace = env_path("TRACE").unwrap_or_else(default_trace);
    let saved = eapp_loader::settings::Settings::load();

    // The eApp every `trace` invocation is handed. A boot never executes it — RetailOS is entered
    // from the reset vector and never looks at 0x18000000 — but `trace`'s first positional is the
    // image, so the recipes all name the same one.
    let retail_flash =
        || res.join("roms/retail_5g_MA146_HwVr000B0005_internal_rom_000000-0FFFFF.bin");

    match recipe {

        // retail-boot.sh: the retail defaults, a writable per-run clone, then cold-boot.sh
        // --disk-writable "$@" — so --disk-writable lands ahead of the caller's own flags.
        Recipe::Retail => {
            let (flash, flash_from) = resolve("FLASH", saved.flash.clone(), retail_flash());
            let (src, disk_from) = resolve("DISK", saved.disk.clone(), res.join("drives/ipod8g-retail.img"));
            let budget = env_u64("BUDGET", 150_000_000);
            let (work, cleanup) = match env_path("WORKDISK") {
                Some(w) => (w, None),
                None => {
                    let w = std::env::temp_dir()
                        .join(format!("ipod-retail-boot-{}.img", std::process::id()));
                    (w.clone(), Some(w))
                }
            };
            if !dry {
                require(&flash, "NOR dump (FLASH=)")?;
                require(&src, "disk image (DISK=)")?;
                clone_file(&src, &work)?;
            }
            Ok(Plan {
                trace,
                sources: vec![("NOR dump", flash.clone(), flash_from), ("drive", src.clone(), disk_from)],
                runs: vec![cold_argv(
                    budget,
                    &flash,
                    &work,
                    &["--disk-writable".into()],
                    user,
                )],
                cleanup,
            })
        }

        // trace BUDGET --osos= --boot-osos --osos-at=0x04000000 --sysinfo --flash= --disk=
        //       --bcm --pmu "$@"
        Recipe::Warm => {
            let (flash, flash_from) = resolve("FLASH", saved.flash.clone(), retail_flash());
            let (disk, disk_from) = resolve("DISK", saved.disk.clone(), res.join("drives/ipod8g.img"));
            let osos = env_path("OSOS").unwrap_or_else(|| res.join("derived/fw/OSOS_correct.bin"));
            let budget = env_u64("BUDGET", 600_000_000);
            if !dry {
                require(&flash, "NOR dump (FLASH=)")?;
                require(&disk, "disk image (DISK=)")?;
                require(&osos, "RetailOS image (OSOS=)")?;
            }
            let mut a = head(budget);
            a.push(opt("--osos=", &osos));
            a.push("--boot-osos".into());
            a.push("--osos-at=0x04000000".into());
            a.push("--sysinfo".into());
            a.push(opt("--flash=", &flash));
            a.push(opt("--disk=", &disk));
            a.push("--bcm".into());
            a.push("--pmu".into());
            a.extend(user.iter().cloned());
            Ok(Plan { trace, sources: vec![("NOR dump", flash.clone(), flash_from), ("drive", disk.clone(), disk_from)], runs: vec![a], cleanup: None })
        }

        // trace BUDGET --osos=.../flsh/$IMG.bin --boot-osos --flash= --disk= --sysinfo
        //       --bcm --pmu --nor "$@"
        Recipe::Flsh => {
            let img = std::env::var("IMG").unwrap_or_else(|_| "diag".into());
            let (flash, flash_from) = resolve("FLASH", saved.flash.clone(), retail_flash());
            let (disk, disk_from) = resolve("DISK", saved.disk.clone(), res.join("drives/ipod8g.img"));
            let osos = res.join(format!("derived/fw/flsh/{img}.bin"));
            let budget = env_u64("BUDGET", 200_000_000);
            if !dry {
                require(&flash, "NOR dump (FLASH=)")?;
                require(&disk, "disk image (DISK=)")?;
                require(&osos, "flash image (IMG=)")?;
            }
            let mut a = head(budget);
            a.push(opt("--osos=", &osos));
            a.push("--boot-osos".into());
            a.push(opt("--flash=", &flash));
            a.push(opt("--disk=", &disk));
            a.push("--sysinfo".into());
            a.push("--bcm".into());
            a.push("--pmu".into());
            a.push("--nor".into());
            a.extend(user.iter().cloned());
            Ok(Plan { trace, sources: vec![("NOR dump", flash.clone(), flash_from), ("drive", disk.clone(), disk_from)], runs: vec![a], cleanup: None })
        }

        // trace BUDGET --osos=$RB/$IMG --boot-osos --flash= --disk= --sysinfo --bcm --pmu "$@"
        Recipe::Rockbox => {
            let img = std::env::var("IMG").unwrap_or_else(|_| "rb-main.raw".into());
            let (flash, flash_from) = resolve("FLASH", saved.flash.clone(), retail_flash());
            let (disk, disk_from) = resolve("DISK", saved.disk.clone(), res.join("drives/ipod8g.img"));
            let osos = res.join("vendor/rockbox/bin").join(&img);
            let budget = env_u64("BUDGET", 200_000_000);
            if !dry {
                require(&flash, "NOR dump (FLASH=)")?;
                require(&disk, "disk image (DISK=)")?;
                require(&osos, "Rockbox image (IMG=)")?;
            }
            let mut a = head(budget);
            a.push(opt("--osos=", &osos));
            a.push("--boot-osos".into());
            a.push(opt("--flash=", &flash));
            a.push(opt("--disk=", &disk));
            a.push("--sysinfo".into());
            a.push("--bcm".into());
            a.push("--pmu".into());
            a.extend(user.iter().cloned());
            Ok(Plan { trace, sources: vec![("NOR dump", flash.clone(), flash_from), ("drive", disk.clone(), disk_from)], runs: vec![a], cleanup: None })
        }

        // Two boots of the same argv, against a disk whose firmware partition was written from the
        // pristine bundle. The first is the update; the second is the proof that the update took.
        Recipe::FlashUpdate => {
            let (flash, flash_from) = resolve("FLASH", saved.flash.clone(), retail_flash());
            let srcdisk =
                env_path("SRCDISK").unwrap_or_else(|| res.join("drives/ipod8g.img"));
            let fw = env_path("FW").unwrap_or_else(|| res.join("derived/fw/Firmware-20.6.3"));
            let work = env_path("WORK").unwrap_or_else(|| std::env::temp_dir().join("ipod-flash-update"));
            let budget = env_u64("BUDGET", 600_000_000);
            let disk = work.join("disk.img");
            if !dry {
                require(&flash, "NOR dump (FLASH=)")?;
                require(&srcdisk, "disk image (SRCDISK=)")?;
                require(&fw, "firmware bundle (FW=)")?;
                if !disk.exists() {
                    clone_file(&srcdisk, &disk)?;
                    write_firmware_partition(&disk, &fw)?;
                    println!("built {} — firmware partition written from {}", disk.display(), fw.display());
                }
            }
            // NOT `cold_argv` + an extra. `flash-update.sh` puts `--disk-writable` immediately
            // after `--disk=`, where `retail-boot.sh` — which reaches `trace` through
            // `cold-boot.sh`'s `"$@"` — puts it after `--nor`. Same flags, different order, and
            // the drift test caught the difference the first time this was written the tidy way.
            let mut argv = head(budget);
            argv.push("--boot-osos".into());
            argv.push("--cold-boot".into());
            argv.push(opt("--flash=", &flash));
            argv.push(opt("--disk=", &disk));
            argv.push("--disk-writable".into());
            argv.push("--bcm".into());
            argv.push("--pmu".into());
            argv.push("--nor".into());
            argv.extend(user.iter().cloned());
            Ok(Plan { trace, sources: vec![("NOR dump", flash.clone(), flash_from)], runs: vec![argv.clone(), argv], cleanup: None })
        }

        // Restore a cached snapshot instead of replaying 1.6 G instructions of boot.
        Recipe::FromIdle => {
            let snap_at = env_u64("SNAP_AT", 1_600_000_000);
            let budget = env_u64("BUDGET", 60_000_000);
            let cache = env_path("CACHE").unwrap_or_else(|| std::env::temp_dir().join("ipod-from-idle"));
            let (flash, flash_from) = resolve("FLASH", saved.flash.clone(), retail_flash());
            let (src, disk_from) = resolve("DISK", saved.disk.clone(), res.join("drives/ipod8g-retail.img"));

            // Keyed on the emulator binary, and this is the load-bearing line in the file: a
            // snapshot restored under a different build is a hybrid machine whose numbers stay
            // plausible and stop meaning anything. `shasum -a 256 "$TRACE" | cut -c1-16` is what
            // from-idle.sh computes, so the two front ends share one cache rather than each
            // paying its own 80-second cold boot.
            // Computed under `--print` too, whenever the binary is there to read: a printed
            // command line with a placeholder in it is not a command line, and the whole point of
            // `--print` is that it can be pasted.
            let key = match std::fs::read(&trace) {
                Ok(bytes) => sha256_hex(&bytes)[..16].to_string(),
                Err(e) if dry => {
                    eprintln!("(no trace binary at {}: {e})", trace.display());
                    "<sha256 of the trace binary, first 16 hex>".to_string()
                }
                Err(e) => return Err(format!("{}: {e}", trace.display())),
            };
            let snap = cache.join(format!("idle-{key}-{snap_at}.snap"));
            let disk = cache.join(format!("idle-{key}-{snap_at}.img"));

            if !dry && !snap.exists() {
                std::fs::create_dir_all(&cache).map_err(|e| format!("{}: {e}", cache.display()))?;
                eprintln!("building snapshot at {snap_at} for trace {key} (one-off, ~80 s) …");
                let _ = std::fs::remove_file(&disk);
                require(&flash, "NOR dump (FLASH=)")?;
                require(&src, "disk image (DISK=)")?;
                clone_file(&src, &disk)?;
                let build = cold_argv(
                    snap_at + 1_000_000,
                    &flash,
                    &disk,
                    &["--disk-writable".into()],
                    &[
                        "--clock=5".to_string(),
                        format!("--snapshot={}:{}", snap_at, snap.display()),
                    ],
                );
                let status = Command::new(&trace)
                    .args(&build)
                    .status()
                    .map_err(|e| format!("{}: {e}", trace.display()))?;
                if !status.success() {
                    return Err("the snapshot build did not finish".into());
                }
                // A partial snapshot is worse than none — it would restore and quietly under-run.
                let ok = std::fs::metadata(&snap).map(|m| m.len() > 0).unwrap_or(false);
                if !ok {
                    return Err("snapshot was not written; refusing to continue".into());
                }
            }

            let mut tail = vec!["--clock=5".to_string(), format!("--restore={}", snap.display())];
            tail.extend(user.iter().cloned());
            Ok(Plan {
                trace,
                sources: vec![("NOR dump", flash.clone(), flash_from), ("drive", src.clone(), disk_from)],
                runs: vec![cold_argv(
                    budget,
                    &flash,
                    &disk,
                    &["--disk-writable".into()],
                    &tail,
                )],
                cleanup: None,
            })
        }
    }
}

/// `ipod-boot make-disk IPSW OUT.img [SECTORS]` — the front door for somebody with no disk image.
///
/// Prints what the IPSW turned out to be before writing anything, so a wrong bundle is a sentence
/// rather than a boot that fails ninety seconds later for a reason nobody can read.
/// The setup screen's two questions, on stdin.
///
/// For a machine with no window to open one on — a headless Linux box, an ssh session, a container.
/// It writes the same file the window writes and uses the same verdicts, so neither front end knows
/// which one did the asking, and an answer that is wrong is rejected here rather than at boot.
fn setup() -> Result<(), String> {
    use eapp_loader::{inspect, settings::Settings};
    use std::io::Write as _;

    let mut s = Settings::load();
    println!(
        "Apple's firmware is not in this program and never will be, so it needs two files from \
         you.\nLeave an answer empty to keep what is there.\n"
    );

    // The NOR dump. No way to build one — it comes off an iPod, or an archive of one.
    let flash = ask("NOR dump (1 MB, e.g. internal_rom_000000-0FFFFF.bin)", s.flash.as_deref())?;
    if let Some(p) = &flash {
        let v = inspect::flash(p);
        println!("  {}\n", v.text());
        if !v.ok() {
            print!("  Use it anyway? [y/N] ");
            let _ = std::io::stdout().flush();
            if !yes()? {
                return Err("nothing saved".into());
            }
        }
        s.flash = Some(p.clone());
    }

    // The drive, or the .ipsw one is built from. `make-disk` is right here, so offer it rather
    // than making somebody run a second command they have not been told about yet.
    let disk = ask("drive image, or an .ipsw to build one from", s.disk.as_deref())?;
    if let Some(p) = &disk {
        if p.extension().is_some_and(|e| e.eq_ignore_ascii_case("ipsw")) {
            let out = p.with_extension("img");
            println!("  That is an .ipsw. Building a drive at {} …", out.display());
            make_disk(&[p.display().to_string(), out.display().to_string()])?;
            s.disk = Some(out);
        } else {
            println!("  {}\n", inspect::disk(p).text());
            s.disk = Some(p.clone());
        }
    }

    s.save();
    match Settings::path() {
        Some(p) => println!("\nSaved to {}. Every recipe here and the window both use it.", p.display()),
        None => println!("\nSaved."),
    }
    Ok(())
}

/// One prompt. Empty keeps the current value; `-` clears it.
fn ask(what: &str, current: Option<&Path>) -> Result<Option<PathBuf>, String> {
    use std::io::Write as _;
    match current {
        Some(p) => println!("{what}\n  currently: {}", p.display()),
        None => println!("{what}"),
    }
    print!("  path: ");
    let _ = std::io::stdout().flush();
    let mut line = String::new();
    std::io::stdin().read_line(&mut line).map_err(|e| e.to_string())?;
    println!();
    let line = line.trim().trim_matches('\'').trim_matches('"');
    let p = match line {
        "" | "-" => return Ok(None),
        p => PathBuf::from(shellexpand_tilde(p)),
    };
    // Absolute, always. This is written to a file that another program reads from another working
    // directory — a relative path saved here resolves against whatever that happens to be, which
    // is a setting that works once and then quietly points somewhere else.
    Ok(Some(match p.canonicalize() {
        Ok(abs) => abs,
        Err(_) => std::env::current_dir().map(|d| d.join(&p)).unwrap_or(p),
    }))
}

/// `~/x` from a terminal is the shell's job, and there is no shell here when a path is pasted.
fn shellexpand_tilde(p: &str) -> String {
    match p.strip_prefix("~/") {
        Some(rest) => match std::env::var_os("HOME") {
            Some(h) => format!("{}/{rest}", h.to_string_lossy()),
            None => p.to_string(),
        },
        None => p.to_string(),
    }
}

fn yes() -> Result<bool, String> {
    let mut line = String::new();
    std::io::stdin().read_line(&mut line).map_err(|e| e.to_string())?;
    Ok(matches!(line.trim(), "y" | "Y" | "yes"))
}

fn make_disk(args: &[String]) -> Result<(), String> {
    let src = args.first().ok_or(
        "usage: ipod-boot make-disk IPSW OUT.img [SECTORS]\n\
         IPSW is an iPod software-update bundle — a zip holding `Firmware-<version>` and \
         `manifest.plist`. It is not distributed with this project.",
    )?;
    let out = args.get(1).ok_or("usage: ipod-boot make-disk IPSW OUT.img [SECTORS]")?;
    let with_aupd = args.iter().any(|a| a == "--with-aupd");
    let sectors = args
        .get(2)
        .filter(|s| !s.starts_with("--"))
        .map(|s| s.replace('_', "").parse::<u64>().map_err(|e| format!("SECTORS: {e}")))
        .transpose()?
        .unwrap_or(eapp_loader::ipsw::DEFAULT_SECTORS);

    let mut fw = match eapp_loader::ipsw::inspect(Path::new(src)) {
        eapp_loader::ipsw::Ipsw::Good(what, fw) => {
            println!("{src}\n  {what}");
            fw
        }
        eapp_loader::ipsw::Ipsw::Wrong(why) | eapp_loader::ipsw::Ipsw::Bad(why) => {
            return Err(why)
        }
    };
    if with_aupd {
        println!(
            "  `aupd` left armed: the FIRST boot will run Apple's flash updater and the second \
             will run the OS, which is what a real iPod does after a restore. \
             `ipod-boot flash-update` is the recipe that measures both."
        );
    } else if eapp_loader::ipsw::mark_aupd_applied(&mut fw) {
        println!(
            "  `aupd` marked applied (+0x08 = 1), so the first boot runs the OS. That is the state \
             a real iPod is in after its post-restore firmware update; `--with-aupd` leaves it \
             armed and reproduces the two-boot sequence."
        );
    }
    eapp_loader::ipsw::build_disk(&fw, Path::new(out), sectors)?;
    println!(
        "wrote {out} — {sectors} sectors ({} MiB), sparse. \
         The firmware partition is Apple's, byte for byte; the FAT32 volume is empty and RetailOS \
         populates it on first boot.",
        sectors / 2048
    );
    println!("  ipod-boot retail --clock=5     # DISK={out}");
    Ok(())
}

/// The positional head every recipe shares: the budget, and nothing else.
///
/// No eApp. A boot is entered from the reset vector and never looks at `EAPP_LOAD_BASE`, so there
/// is nothing for a title to do here — which is why booting needs only a NOR dump and a drive.
fn head(budget: u64) -> Vec<String> {
    vec![budget.to_string()]
}

fn opt(flag: &str, p: &Path) -> String {
    format!("{flag}{}", p.display())
}

/// `cold-boot.sh`'s argv, which `retail-boot.sh`, `flash-update.sh` and `from-idle.sh` all reach
/// through. `extra` is what the calling recipe inserts ahead of the caller's own flags, exactly
/// where `"$@"` puts it when one script `exec`s another.
fn cold_argv(
    budget: u64,
    flash: &Path,
    disk: &Path,
    extra: &[String],
    user: &[String],
) -> Vec<String> {
    let mut a = head(budget);
    a.push("--boot-osos".into());
    a.push("--cold-boot".into());
    a.push(opt("--flash=", flash));
    a.push(opt("--disk=", disk));
    a.push("--bcm".into());
    a.push("--pmu".into());
    a.push("--nor".into());
    a.extend(extra.iter().cloned());
    a.extend(user.iter().cloned());
    a
}

/// `dd if=$FW of=$DISK bs=512 seek=63 conv=notrunc`.
///
/// MBR partition 0 starts at LBA 63 and is 27 140 sectors — 13 895 680 bytes, exactly the size of
/// the pristine firmware. It fits with nothing left over, which is how the offset is known.
/// `conv=notrunc` is the whole point: the 8 GB image keeps its length.
fn write_firmware_partition(disk: &Path, fw: &Path) -> Result<(), String> {
    let bytes = std::fs::read(fw).map_err(|e| format!("{}: {e}", fw.display()))?;
    let mut f = std::fs::OpenOptions::new()
        .write(true)
        .open(disk)
        .map_err(|e| format!("{}: {e}", disk.display()))?;
    f.seek(SeekFrom::Start(63 * 512)).map_err(|e| format!("{}: {e}", disk.display()))?;
    f.write_all(&bytes).map_err(|e| format!("{}: {e}", disk.display()))?;
    f.flush().map_err(|e| format!("{}: {e}", disk.display()))
}

/// Render an argv the way a shell would need it written, for `--print`. A chosen path can have
/// spaces in it, and an unquoted `--print` line that cannot be pasted back is a documentation bug
/// waiting to be reported as a code one.
fn shell_quote(trace: &Path, args: &[String]) -> String {
    let mut out = quote_one(&trace.to_string_lossy());
    for a in args {
        out.push(' ');
        out.push_str(&quote_one(a));
    }
    out
}

fn quote_one(s: &str) -> String {
    if !s.is_empty() && s.chars().all(|c| c.is_ascii_alphanumeric() || "-_=./:+,@".contains(c)) {
        s.to_string()
    } else {
        format!("'{}'", s.replace('\'', r"'\''"))
    }
}

// ---------------------------------------------------------------- SHA-256

/// SHA-256, ~60 lines of it, so this program and `from-idle.sh` compute the **same** cache key and
/// share one snapshot instead of each paying its own 80-second cold boot.
///
/// Written out rather than depended on: `eapp-loader` has one dependency (`arm7tdmi`, a path) and
/// the README's claim that the core crates build with no third-party code is worth more than the
/// sixty lines. There is a NIST known-answer test below.
fn sha256_hex(data: &[u8]) -> String {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19,
    ];

    let mut msg = data.to_vec();
    let bits = (data.len() as u64).wrapping_mul(8);
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&bits.to_be_bytes());

    for block in msg.chunks_exact(64) {
        let mut w = [0u32; 64];
        for (i, c) in block.chunks_exact(4).enumerate() {
            w[i] = u32::from_be_bytes([c[0], c[1], c[2], c[3]]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }
        let (mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut hh) =
            (h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ (!e & g);
            let t1 = hh
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            hh = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        for (i, v) in [a, b, c, d, e, f, g, hh].into_iter().enumerate() {
            h[i] = h[i].wrapping_add(v);
        }
    }
    h.iter().map(|x| format!("{x:08x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    /// NIST FIPS 180-2 test vectors, plus the empty string. If this passes, `from-idle.sh` and
    /// `ipod-boot from-idle` name the same cache file.
    #[test]
    fn sha256_matches_the_published_vectors() {
        assert_eq!(
            sha256_hex(b""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(
            sha256_hex(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert_eq!(
            sha256_hex(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
        );
        // A multi-block input, to exercise the padding boundary rather than only the first block.
        assert_eq!(
            sha256_hex(&vec![b'a'; 1_000_000])[..16],
            *"cdc76e5c9914fb92"
        );
    }

    fn flags_of(argv: &[String]) -> Vec<String> {
        argv.iter()
            .filter(|a| a.starts_with("--"))
            .map(|a| match a.split_once('=') {
                Some((k, _)) => format!("{k}="),
                None => a.clone(),
            })
            .collect()
    }

    /// The drift guard this file exists to make possible.
    ///
    /// Reads each `.sh` off disk, pulls the flags out of its `trace` invocation, and asserts they
    /// are the flags this program passes, in order. Two front ends composing "the same" argv is a
    /// claim; this is the test that keeps it one.
    #[test]
    fn recipe_flags_match_the_shell_scripts() {
        let scripts = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .parent()
            .unwrap()
            .join("ipod-boot");
        // Retail is in this list now. It used to be covered only by a test that defined it as
        // "cold-boot.sh's flags plus --disk-writable", and when the cold recipe was deleted that
        // test went with it — which would have left the recipe every number in research/ is
        // measured on with no drift coverage at all.
        for recipe in [
            Recipe::Retail,
            Recipe::Warm,
            Recipe::Flsh,
            Recipe::Rockbox,
            Recipe::FlashUpdate,
        ] {
            let text = std::fs::read_to_string(scripts.join(recipe.script()))
                .unwrap_or_else(|e| panic!("{}: {e}", recipe.script()));
            let from_script = script_flags(&text);
            let plan = plan(recipe, &[], true).unwrap();
            let mine = flags_of(&plan.runs[0]);
            assert_eq!(
                mine,
                from_script,
                "{} and ipod-boot {:?} disagree about the flags",
                recipe.script(),
                recipe
            );
        }
    }


    /// Extract the flags from a script's `trace` invocation: everything from the line that runs
    /// `$TRACE` to the end of its backslash continuations.
    fn script_flags(text: &str) -> Vec<String> {
        let mut out = Vec::new();
        let mut in_call = false;
        for line in text.lines() {
            let t = line.trim();
            if t.starts_with('#') {
                continue;
            }
            if !in_call && t.contains("\"$TRACE\"") {
                in_call = true;
            }
            if in_call {
                for tok in t.split_whitespace() {
                    if let Some(f) = tok.strip_prefix("--") {
                        let name = f.split('=').next().unwrap_or(f);
                        let has_value = f.contains('=');
                        // `"$@"` is the caller's flags, not the recipe's.
                        out.push(if has_value {
                            format!("--{name}=")
                        } else {
                            format!("--{name}")
                        });
                    }
                }
                if !t.ends_with('\\') {
                    break;
                }
            }
        }
        out
    }

    /// `flash-update` boots twice with an identical argv. That is the measurement — the second boot
    /// proves the first one's write took, and it only proves it if nothing differs between them.
    #[test]
    fn flash_update_boots_the_same_argv_twice() {
        let p = plan(Recipe::FlashUpdate, &[], true).unwrap();
        assert_eq!(p.runs.len(), 2);
        assert_eq!(p.runs[0], p.runs[1]);
    }

    /// The caller's flags go last, so `--clock=5` on the command line reaches `trace` after the
    /// recipe's own — which is what lets a caller override one.
    #[test]
    fn user_flags_are_appended_last() {
        let user = vec!["--clock=5".to_string(), "--profile".to_string()];
        let p = plan(Recipe::Warm, &user, true).unwrap();
        let n = p.runs[0].len();
        assert_eq!(&p.runs[0][n - 2..], &user[..]);
    }

    /// A per-run temporary belongs under the platform's temp directory, wherever that is.
    /// Hardcoding `/tmp` is the bug this is here to prevent coming back.
    #[test]
    fn the_per_run_disk_lives_under_the_platform_temp_dir() {
        let p = plan(Recipe::Retail, &[], true).unwrap();
        let disk = p
            .runs[0]
            .iter()
            .find_map(|a| a.strip_prefix("--disk="))
            .expect("retail passes --disk=");
        assert!(
            Path::new(disk).starts_with(std::env::temp_dir()),
            "{disk} is not under {}",
            std::env::temp_dir().display()
        );
        assert_eq!(p.cleanup.as_deref(), Some(Path::new(disk)));
    }

    /// Paths in this project contain spaces. A `--print` line that cannot be pasted into a shell is
    /// worse than no `--print` line, because it looks like it can.
    #[test]
    fn print_quotes_paths_with_spaces() {
        let q = shell_quote(
            Path::new("/x/trace"),
            &["/res/My Firmware Dumps/t.bin".to_string(), "--clock=5".to_string()],
        );
        assert_eq!(q, "/x/trace '/res/My Firmware Dumps/t.bin' --clock=5");
    }

    #[test]
    fn every_recipe_name_round_trips() {
        for (name, want) in [
            ("retail", Recipe::Retail),
            ("warm", Recipe::Warm),
            ("flsh", Recipe::Flsh),
            ("rockbox", Recipe::Rockbox),
            ("flash-update", Recipe::FlashUpdate),
            ("from-idle", Recipe::FromIdle),
        ] {
            assert_eq!(Recipe::parse(name), Some(want));
            assert!(scripts_dir().join(want.script()).is_file(), "{}", want.script());
        }
        assert_eq!(Recipe::parse("nonsense"), None);
    }

    fn scripts_dir() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).parent().unwrap().join("ipod-boot")
    }
}

// ---------------------------------------------------------------- installing an OS

/// The sector size the firmware directory's offsets are quantised to.
const FW_SECTOR: u32 = 512;
/// The `!ATA` directory, relative to the start of the firmware partition.
const FW_DIRECTORY: u64 = 0x4200;

/// One 40-byte record of the firmware directory, by the offsets the field names sit at.
struct DirEntry {
    tag: String,
    dev_offset: u32,
    len: u32,
    entry_offset: u32,
    chksum: u32,
}

fn le(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([b[at], b[at + 1], b[at + 2], b[at + 3]])
}

/// Install an operating system into a drive image's firmware partition, into a **new file**.
///
/// This is Apple's own arrangement and `ipodpatcher`'s recipe, not an invention: the image is
/// appended after the existing `osos`, the directory's entry point is moved to it, and the machine's
/// real bootloader finds it at the address it already looks at. Nothing new boots — the existing
/// cold path loads it — so a divergence afterwards belongs to the OS rather than to us.
///
/// **It never writes to the source.** Apple's `osos` is 7.21 MiB of software that can no longer be
/// downloaded, and an installer that edits in place is one mistake away from destroying the only
/// copy somebody has.
fn install_os(args: &[String]) -> Result<(), String> {
    const USAGE: &str = "usage: ipod-boot install-os SRC.img OS.ipod OUT.img";
    let (src, os, out) = match (args.first(), args.get(1), args.get(2)) {
        (Some(a), Some(b), Some(c)) => (Path::new(a), Path::new(b), Path::new(c)),
        _ => return Err(USAGE.into()),
    };
    if out == src {
        return Err("OUT.img must not be SRC.img — this never edits the source in place".into());
    }

    // The payload. A `.ipod` file is an 8-byte wrapper — big-endian checksum, then a 4-character
    // model id — over a raw ARM image (`tools/scramble.c`). Checking it is the difference between
    // installing a verified image and installing whatever the file happened to contain.
    let raw = std::fs::read(os).map_err(|e| format!("{}: {e}", os.display()))?;
    let payload = if raw.len() > 8 && raw[4..8].iter().all(|c| c.is_ascii_alphanumeric()) {
        let want = u32::from_be_bytes([raw[0], raw[1], raw[2], raw[3]]);
        let model: String = raw[4..8].iter().map(|&c| c as char).collect();
        let body = &raw[8..];
        // `scramble` seeds the sum with the model number; 5 is the Video's, per `modelnum`.
        let sum = body.iter().fold(5u32, |a, &b| a.wrapping_add(b as u32));
        if sum != want {
            return Err(format!(
                "{}: `.ipod` checksum does not match — header says {want:#010x}, the bytes sum to \
                 {sum:#010x}. The file is truncated or is for another model (`{model}`).",
                os.display()
            ));
        }
        println!("  {} — `{model}`, {} bytes, checksum OK", os.display(), body.len());
        body.to_vec()
    } else {
        println!("  {} — {} bytes, raw (no `.ipod` header)", os.display(), raw.len());
        raw
    };

    // The destination, as a copy. Sparse-aware on APFS and harmless elsewhere.
    std::fs::copy(src, out).map_err(|e| format!("copying {} -> {}: {e}", src.display(), out.display()))?;
    let mut f = std::fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(out)
        .map_err(|e| format!("{}: {e}", out.display()))?;

    // Where the firmware partition is. Type 0x00 is Apple's, and it is the first entry.
    let mut mbr = [0u8; 512];
    f.seek(SeekFrom::Start(0)).and_then(|_| f.read_exact(&mut mbr)).map_err(|e| e.to_string())?;
    if mbr[510] != 0x55 || mbr[511] != 0xAA {
        return Err(format!("{}: no MBR signature — is this a drive image?", src.display()));
    }
    if mbr[446 + 4] != 0x00 {
        return Err(format!(
            "{}: partition 0 is type {:#04x}, not Apple's firmware partition (0x00)",
            src.display(),
            mbr[446 + 4]
        ));
    }
    let part = le(&mbr, 446 + 8) as u64 * 512;
    let part_sectors = le(&mbr, 446 + 12) as u64;
    // **Image data lives one sector past the partition; the directory does not.**
    // `ipodpatcher.c:1586` — `fwoffset = start + sector_size` — and it is measurable rather than
    // taken on trust: both recorded checksums in a stock image match a byte sum over
    // `[devOffset + 512, +len)` and neither matches one over `[devOffset, +len)`. Getting this
    // wrong writes an image Apple's bootloader rejects after ~70 ATA commands with
    // "Connect to your computer. Use iTunes to restore." — which is what it did.
    let fw = part + FW_SECTOR as u64;

    // The directory.
    let mut dir = vec![0u8; FW_SECTOR as usize];
    f.seek(SeekFrom::Start(part + FW_DIRECTORY)).and_then(|_| f.read_exact(&mut dir)).map_err(|e| e.to_string())?;
    let mut images = Vec::new();
    for i in 0..(FW_SECTOR as usize / 40) {
        let r = &dir[i * 40..i * 40 + 40];
        if &r[0..4] != b"ATA!" && &r[0..4] != b"!ATA" {
            break;
        }
        images.push(DirEntry {
            tag: r[4..8].iter().rev().map(|&b| b as char).collect(),
            dev_offset: le(r, 0x0c),
            len: le(r, 0x10),
            entry_offset: le(r, 0x18),
            chksum: le(r, 0x1c),
        });
    }
    let first = images.first().ok_or("no images in the firmware directory")?;
    if first.tag != "osos" {
        return Err(format!("image 0 is `{}`, expected `osos`", first.tag));
    }
    // **Reproduce the checksums that are already there before writing new ones.** If our idea of
    // where an image starts or how it is summed is wrong, this fails here — on a file nobody has
    // modified — instead of producing a plausible image that the bootloader silently rejects
    // seventy ATA commands into a boot. It is the same check `--check-images` performs on the
    // inputs, applied to our own understanding of the format.
    for img in &images {
        let mut sum = 0u32;
        let mut left = img.len as usize;
        let mut buf = vec![0u8; 1 << 20];
        f.seek(SeekFrom::Start(fw + img.dev_offset as u64)).map_err(|e| e.to_string())?;
        while left > 0 {
            let n = left.min(buf.len());
            f.read_exact(&mut buf[..n]).map_err(|e| e.to_string())?;
            sum = buf[..n].iter().fold(sum, |a, &b| a.wrapping_add(b as u32));
            left -= n;
        }
        if sum != img.chksum {
            return Err(format!(
                "`{}`: the checksum in the directory is {:#010x} but its bytes sum to {sum:#010x}.                  Either this image is already damaged, or this tool has the firmware layout wrong                  — and in both cases writing to it would make things worse.",
                img.tag, img.chksum
            ));
        }
    }
    println!("  existing checksums reproduce — the layout is understood");
    println!(
        "  firmware partition at {part:#x}, {} image(s): {}",
        images.len(),
        images.iter().map(|i| i.tag.as_str()).collect::<Vec<_>>().join(" · ")
    );

    // Where the new image goes. Re-installing over a previous one reuses the same slot rather than
    // pushing everything along again, which is what `entryOffset > 0` means.
    let align = |n: u32| (n + FW_SECTOR - 1) & !(FW_SECTOR - 1);
    let entry_offset =
        if first.entry_offset > 0 { first.entry_offset } else { align(first.len) };
    let length = payload.len() as u32;
    let padded = align(length);

    // Anything after `osos` has to move out of the way. The gap here is 512 bytes against a
    // 52 KB bootloader, so this is the normal case and not an edge one.
    let mut delta = 0u32;
    if let Some(next) = images.get(1) {
        let end = first.dev_offset + entry_offset + padded;
        if end > next.dev_offset {
            delta = end - next.dev_offset + FW_SECTOR;
            let last = images.last().unwrap();
            let needed = (last.dev_offset + align(last.len) + delta) as u64;
            if needed > part_sectors * 512 {
                return Err(format!(
                    "no room: moving the later images by {delta} bytes needs {needed} of a \
                     {}-byte partition",
                    part_sectors * 512
                ));
            }
            println!("  moving {} later image(s) on by {delta} bytes", images.len() - 1);
            // Backwards, so a shift never overwrites the source of a later block.
            for img in images[1..].iter().rev() {
                let n = align(img.len) as usize;
                let mut buf = vec![0u8; n];
                f.seek(SeekFrom::Start(fw + img.dev_offset as u64))
                    .and_then(|_| f.read_exact(&mut buf))
                    .map_err(|e| e.to_string())?;
                f.seek(SeekFrom::Start(fw + (img.dev_offset + delta) as u64))
                    .and_then(|_| f.write_all(&buf))
                    .map_err(|e| e.to_string())?;
            }
        }
    }

    // The combined image, and its checksum over every byte of it.
    f.seek(SeekFrom::Start(fw + first.dev_offset as u64)).map_err(|e| e.to_string())?;
    let mut combined = vec![0u8; entry_offset as usize];
    f.read_exact(&mut combined).map_err(|e| e.to_string())?;
    combined.extend_from_slice(&payload);
    let chksum = combined.iter().fold(0u32, |a, &b| a.wrapping_add(b as u32));
    combined.resize((entry_offset + padded) as usize, 0);
    f.seek(SeekFrom::Start(fw + first.dev_offset as u64))
        .and_then(|_| f.write_all(&combined))
        .map_err(|e| e.to_string())?;

    // And the directory: image 0 gains the payload and points its entry at it; the rest follow
    // their data. `loadAddr` is cleared the way `ipodpatcher` clears it.
    dir[0x10..0x14].copy_from_slice(&(entry_offset + length).to_le_bytes());
    dir[0x18..0x1c].copy_from_slice(&entry_offset.to_le_bytes());
    dir[0x1c..0x20].copy_from_slice(&chksum.to_le_bytes());
    dir[0x24..0x28].copy_from_slice(&0xffff_ffffu32.to_le_bytes());
    if delta > 0 {
        for i in 1..images.len() {
            let at = i * 40 + 0x0c;
            let moved = le(&dir, at) + delta;
            dir[at..at + 4].copy_from_slice(&moved.to_le_bytes());
        }
    }
    f.seek(SeekFrom::Start(part + FW_DIRECTORY))
        .and_then(|_| f.write_all(&dir))
        .map_err(|e| e.to_string())?;
    f.sync_all().map_err(|e| e.to_string())?;

    println!(
        "  installed at +{entry_offset:#x}, {length} bytes, checksum {chksum:#010x}\n\
         {} — cold boot it and Apple's own bootloader will run it.",
        out.display()
    );
    Ok(())
}

#[cfg(test)]
mod install_tests {
    use super::*;

    /// A minimal drive image with a firmware partition: MBR, a directory of two images, and their
    /// bytes at `partition + 512 + devOffset` — the layout `fwoffset = start + sector_size` means,
    /// and the one whose absence produced an image the bootloader rejected.
    fn synth_disk(path: &Path, osos: &[u8], rsrc: &[u8]) {
        const PART_LBA: u32 = 63;
        const SECTORS: u32 = 27140;
        let mut img = vec![0u8; (PART_LBA as usize + SECTORS as usize) * 512];
        img[446 + 4] = 0x00; // Apple firmware partition
        img[446 + 8..446 + 12].copy_from_slice(&PART_LBA.to_le_bytes());
        img[446 + 12..446 + 16].copy_from_slice(&SECTORS.to_le_bytes());
        img[510] = 0x55;
        img[511] = 0xAA;

        let part = PART_LBA as usize * 512;
        let fw = part + 512;
        let sum = |b: &[u8]| b.iter().fold(0u32, |a, &x| a.wrapping_add(x as u32));
        let mut put = |i: usize, tag: &[u8; 4], dev: u32, body: &[u8]| {
            let at = part + 0x4200 + i * 40;
            img[at..at + 4].copy_from_slice(b"ATA!");
            let mut t = *tag;
            t.reverse();
            img[at + 4..at + 8].copy_from_slice(&t);
            img[at + 0x0c..at + 0x10].copy_from_slice(&dev.to_le_bytes());
            img[at + 0x10..at + 0x14].copy_from_slice(&(body.len() as u32).to_le_bytes());
            img[at + 0x14..at + 0x18].copy_from_slice(&0x1000_0000u32.to_le_bytes());
            img[at + 0x1c..at + 0x20].copy_from_slice(&sum(body).to_le_bytes());
            img[fw + dev as usize..fw + dev as usize + body.len()].copy_from_slice(body);
        };
        put(0, b"osos", 0x4400, osos);
        put(1, b"rsrc", 0x4400 + osos.len() as u32, rsrc);
        std::fs::write(path, &img).unwrap();
    }

    fn dir_entry(path: &Path, i: usize) -> [u32; 8] {
        let mut f = std::fs::File::open(path).unwrap();
        f.seek(SeekFrom::Start(63 * 512 + 0x4200 + (i * 40) as u64)).unwrap();
        let mut b = [0u8; 40];
        f.read_exact(&mut b).unwrap();
        let mut out = [0u32; 8];
        for (k, o) in out.iter_mut().enumerate() {
            *o = le(&b, 8 + k * 4);
        }
        out
    }

    #[test]
    fn installing_appends_the_image_moves_the_rest_and_rewrites_the_directory() {
        let dir = std::env::temp_dir();
        let (src, os, out) = (
            dir.join("ipodboot-src.img"),
            dir.join("ipodboot-os.bin"),
            dir.join("ipodboot-out.img"),
        );
        let osos: Vec<u8> = (0..0x2000u32).map(|i| (i % 251) as u8).collect();
        let rsrc: Vec<u8> = (0..0x1000u32).map(|i| (i % 241) as u8).collect();
        synth_disk(&src, &osos, &rsrc);
        std::fs::write(&os, vec![0xAAu8; 0x900]).unwrap();
        let _ = std::fs::remove_file(&out);

        install_os(&[
            src.display().to_string(),
            os.display().to_string(),
            out.display().to_string(),
        ])
        .expect("install");

        // entryOffset is the old length rounded up; len covers the payload; loadAddr is cleared.
        let e0 = dir_entry(&out, 0);
        assert_eq!(e0[4], 0x2000, "entryOffset");
        assert_eq!(e0[2], 0x2000 + 0x900, "len must cover the appended image");
        assert_eq!(e0[7], 0xffff_ffff, "loadAddr must be cleared");

        // `rsrc` had to move, and its directory entry has to follow its bytes.
        let e1 = dir_entry(&out, 1);
        assert!(e1[1] > 0x4400 + 0x2000, "rsrc did not move: {:#x}", e1[1]);

        // The new checksum has to describe what is actually on the disk, at fwoffset + devOffset.
        let mut f = std::fs::File::open(&out).unwrap();
        f.seek(SeekFrom::Start(63 * 512 + 512 + e0[1] as u64)).unwrap();
        let mut body = vec![0u8; e0[2] as usize];
        f.read_exact(&mut body).unwrap();
        let sum = body.iter().fold(0u32, |a, &x| a.wrapping_add(x as u32));
        assert_eq!(sum, e0[5], "the directory checksum does not describe the bytes written");

        // And `rsrc` survived the move intact.
        f.seek(SeekFrom::Start(63 * 512 + 512 + e1[1] as u64)).unwrap();
        let mut moved = vec![0u8; rsrc.len()];
        f.read_exact(&mut moved).unwrap();
        assert_eq!(moved, rsrc, "rsrc was corrupted by the move");

        for p in [src, os, out] {
            let _ = std::fs::remove_file(p);
        }
    }

    /// The guard that would have caught the wrong base offset immediately, instead of after a
    /// 1.5 G-instruction boot ending in "Use iTunes to restore".
    #[test]
    fn a_directory_whose_checksums_do_not_reproduce_is_refused() {
        let dir = std::env::temp_dir();
        let (src, os, out) = (
            dir.join("ipodboot-bad-src.img"),
            dir.join("ipodboot-bad-os.bin"),
            dir.join("ipodboot-bad-out.img"),
        );
        synth_disk(&src, &vec![1u8; 0x1000], &vec![2u8; 0x1000]);
        // Corrupt one byte of `osos` so its recorded checksum no longer describes it.
        let mut img = std::fs::read(&src).unwrap();
        img[63 * 512 + 512 + 0x4400] ^= 0xff;
        std::fs::write(&src, &img).unwrap();
        std::fs::write(&os, vec![0xAAu8; 16]).unwrap();

        let err = install_os(&[
            src.display().to_string(),
            os.display().to_string(),
            out.display().to_string(),
        ])
        .expect_err("a damaged image must be refused");
        assert!(err.contains("osos"), "{err}");
        assert!(err.contains("checksum") || err.contains("sum to"), "{err}");

        for p in [src, os, out] {
            let _ = std::fs::remove_file(p);
        }
    }
}

/// Copy a local directory tree into the drive image's FAT32 volume.
///
/// **This modifies DISK.img in place**, unlike `install-os`, and says so before it starts. The
/// image it is meant for is one `install-os` just produced — a derived file — and copying 8 GB
/// again for every file added would be its own kind of hostile.
fn put_files(args: &[String]) -> Result<(), String> {
    const USAGE: &str = "usage: ipod-boot put-files DISK.img SRC_DIR [DEST_PATH]\n\
                         copies the CONTENTS of SRC_DIR into DEST_PATH (default: the volume root)";
    let (disk, src) = match (args.first(), args.get(1)) {
        (Some(a), Some(b)) => (Path::new(a), Path::new(b)),
        _ => return Err(USAGE.into()),
    };
    let dest = args.get(2).map(String::as_str).unwrap_or("");
    if !src.is_dir() {
        return Err(format!("{}: not a directory", src.display()));
    }

    let mut vol = eapp_loader::fat::Fat32::open(disk)?;
    let root = if dest.is_empty() { vol.root() } else { vol.mkdir_p(dest)? };
    println!("  {} — writing into {}", disk.display(), if dest.is_empty() { "/" } else { dest });

    // Breadth-first, so a directory exists before anything is written into it.
    let mut queue = vec![(src.to_path_buf(), root)];
    let (mut files, mut bytes, mut dirs) = (0u64, 0u64, 0u64);
    while let Some((from, into)) = queue.pop() {
        let mut items: Vec<_> = std::fs::read_dir(&from)
            .map_err(|e| format!("{}: {e}", from.display()))?
            .filter_map(|e| e.ok())
            .collect();
        items.sort_by_key(|e| e.file_name());
        for item in items {
            let name = item.file_name().to_string_lossy().to_string();
            let path = item.path();
            let meta = std::fs::metadata(&path).map_err(|e| format!("{}: {e}", path.display()))?;
            if meta.is_dir() {
                let sub = vol.mkdir(into, &name)?;
                dirs += 1;
                queue.push((path, sub));
            } else {
                let body = std::fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
                vol.write_file(into, &name, &body)?;
                files += 1;
                bytes += body.len() as u64;
            }
        }
    }
    vol.flush()?;
    println!("  {files} file(s) in {dirs} directory(ies), {bytes} bytes");
    Ok(())
}
