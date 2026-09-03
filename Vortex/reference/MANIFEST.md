# Frozen copies of the emulator this work cites

The emulator tree above this folder is a moving target: other people and agents change it while
this work happens. Two things here depend on it, and both would rot silently.

**`tools/oracle-emulator/`** is the emulator as it was when the recordings in `tests/expected/`
were made — the `eapp-loader` crate and the `arm7tdmi` crate it depends on, lifted out of the
emulator's workspace so they build on their own. Recordings are made with this and never with
the live tree. The Mini Golf recomp learned why the hard way: its live emulator eventually
stopped reproducing its own recordings, which cost it the ability to add oracle cases at all
until a second oracle was invented.

```sh
cargo build --release --manifest-path tools/oracle-emulator/Cargo.toml \
    --target-dir build/oracle-emulator
```

**Citations.** Comments in this project cite the emulator by file — `reference/eapp-loader/
lib.rs`, `reference/eapp-loader/play.rs`, `reference/arm7tdmi/arm.rs`. Those names refer to the
pinned copy under `tools/oracle-emulator/` (`src/` is `eapp-loader/`; `src/bin/play.rs` is
`play.rs`), not to the live tree. When a claim needs checking, check it there first, then against
the live tree if the two disagree.

| what | taken from | at |
|---|---|---|
| `tools/oracle-emulator/src/` | `<upstream>/tools/eapp-loader/src/` | commit `96bfe90`, 2026-08-28, **plus one instrument** (below), uncommitted in the live tree at the time of the pin |
| `tools/oracle-emulator/arm7tdmi/` | `<upstream>/tools/arm7tdmi/` | commit `96bfe90` |
| `tools/oracle-emulator/Cargo.toml`, `Cargo.lock` | the Sims Bowling recomp's (itself Hold'em's, itself Lost's), unchanged | — |
| `tools/oracle-emulator/arm7tdmi/Cargo.toml` | the emulator's, with `version.workspace = true` replaced by `version = "0.1.0"` — there is no workspace above the pinned copy to inherit from | commit `96bfe90` |

The copy carries **one added instrument**: `--time=HH:MM`, which makes `miscTBD #12` report
that hour and minute (seconds zero, the host's date) instead of the machine's clock — a
`time_override` field beside `battery_override` in `lib.rs`, a flag beside `--battery=` in
`play.rs`, 22 lines in all. It exists because this game *formats* the clock into a string on
its main menu, and a string built at 7:31 is one character shorter than one built at 12:00, so
a recording made without it diverged from every replay by an allocation size (PLAN.md progress
log, 2026-08-28). The recomp's own `--time=` already existed; the two now name the same thing.
The change was made in the live tree (`tools/eapp-loader/src/`) and copied here; it is the live
tree's to commit. Every other flag the recordings use (`--call-log`, `--script`,
`--callgraph-dump`, `--fixed-clock`, `--fps=0`, `--load-on-open`, `--allow-creates`,
`--ctx-seed`) was already in the emulator, and so is the per-title defaults table in `play.rs`
that applies `--load-on-open`, a reason seed of 5, no reason writes, the asynchronous file model
and a 30 fps pace to a binary named `vortex*` (`defaults_for`), the `find_flags_word` scan that
locates this title's button-flags word from its `bic #0x60` signature, and the forced
`allow_creates` for the three files this title writes.

A commit hash names a point in *one* history, and this branch's history has been rewritten before
(the Lost recomp's manifest cites `54e1049`, which no longer exists in this repository). The files
are what the recordings actually depend on, so their hashes are recorded too — each one checked
against `git show 96bfe90:tools/…` on the day of the pin:

| file | SHA-256 |
|---|---|
| `src/lib.rs` | `fe7d730300941d25d6df2aa669346fb5221a278f08a3fe29e8e435eae78b349e` (at `96bfe90`: `c0294849a3dd8e2b…`) |
| `src/bin/play.rs` | `acd89d589e7b215d1e078285b011a870602077b33d3a14e96f4aae1059b48c64` (at `96bfe90`: `852aac0f3af94fe3…`) |
| `arm7tdmi/src/arm.rs` | `b73d0f1b4cf3e2a37113ee69e19a33b9d913a3ec4af65716c0be08d011c6f7a6` |
| `arm7tdmi/src/bus.rs` | `6108b5a557568b63e6d3efe13e378143aab6382857713125a16b8cfe607ab56c` |
| `arm7tdmi/src/cpu.rs` | `757193c48d93f25f002a103584f87f935cde98b5d6246d59f760a23d00ac3caf` |
| `arm7tdmi/src/disasm.rs` | `d1014108e3713db92700e3ee15e4620448f4c8ecc5dfe26bd08df50368c31e24` |
| `arm7tdmi/src/lib.rs` | `10f6c28d3056b398f331faf442d05b774463c0eced81e3d4db8a4cce393d2fc6` |
| `arm7tdmi/src/thumb.rs` | `0b1905dead0ec27b3d42bffce065c489056ff53c4e11812debb9882ae0d55019` |

Apart from the instrument, this copy is byte-identical to the Sims Bowling, Hold'em and Lost
recomps' pins. That is a coincidence of timing and not a dependency: each title pins its own,
so a re-pin in one never silently re-dates another's recordings — and this one's instrument
re-dates only this title's.

**When the emulator must move** — a behaviour this title needs that the pinned copy gets wrong —
the fix goes into the live tree in its own commit, this folder is re-copied, this table is
updated, and every `tests/expected/*.calls` is re-recorded, because a recording made with one
build and compared against another proves nothing.

**`reference/PORTED.md`** is the other manifest: the files copied from the Sims Bowling recomp
rather than from the emulator, with their hashes. `tools/port-from-bowling.py` writes it.
