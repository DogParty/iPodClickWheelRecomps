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
| `tools/oracle-emulator/src/` | `<upstream>/tools/eapp-loader/src/` | commit `96bfe90`, 2026-08-28, **plus one instrument and two fixes** (below), all three uncommitted in the live tree at the time of the pin |
| `tools/oracle-emulator/arm7tdmi/` | `<upstream>/tools/arm7tdmi/` | commit `96bfe90` |
| `tools/oracle-emulator/Cargo.toml`, `Cargo.lock` | the Vortex recomp's (itself the Sims Bowling's, itself Hold'em's, itself Lost's), unchanged | — |
| `tools/oracle-emulator/arm7tdmi/Cargo.toml` | the emulator's, with `version.workspace = true` replaced by `version = "0.1.0"` — there is no workspace above the pinned copy to inherit from | commit `96bfe90` |

## What this copy carries that `96bfe90` does not

**The instrument: `--time=HH:MM`.** `miscTBD #12` reports that hour and minute (seconds zero,
the host's date) instead of the machine's clock — a `time_override` field beside
`battery_override` in `lib.rs`, a flag beside `--battery=` in `play.rs`, 22 lines in all. It came
from the Vortex recomp, whose main menu *formats* the clock into a string that is one character
shorter before ten o'clock than after, so a recording made without it diverged from every replay
by an allocation size. This title needs it for a plainer reason: it draws the time in its status
bar, and a before/after comparison of two runs made a minute apart differs in the digits.

**The fix: the constant colour register reaches a colourless texture.** PLAN.md difference 6 is
the whole account; in short, the emulator ignored the register by default because applying it
globally wrecks LOST and The Sims Bowling, and with it ignored **every cube on this game's board
rendered grey**. The register is now applied when the sampled texture carries no colour of its
own — `GL_ALPHA`, `GL_LUMINANCE` or `GL_LUMINANCE_ALPHA` — which is the rule `lib.rs` already
applied to `GL_ALPHA` alone, one format short. Two fields and one condition:
`Texture::colourless`, set in `upload_plain`; `tex_colourless` in `fill_triangle`, replacing
`tex_alpha_only` in the gate that was already there.

**The second fix: a 1:1 blit's half-texel tie is decided with a tolerance.** These games draw a
full-screen backdrop whose texture rectangle is offset by exactly half a texel — this one's is
`pos [0..320]`, `uv [0.5..320.5]` — so the nearest-texel tap's `dx >= 0.5` test lands *exactly*
on the tie for every pixel of it, and which texel it reads is then decided by the last bit of a
barycentric interpolation. Measured against the artwork the backdrop is drawn from: a bare
`>= 0.5` reproduces 57–75 % of the artist's pixels, the rest coming from the neighbouring texel,
scattered per pixel — which is what made a 320x240 photographic background render as noise. With
a tolerance (`0.5 - 1e-3`, far wider than the arithmetic's ~1e-6 noise and far narrower than any
real offset) the same run reproduces 88–99 %. Four lines in `fill_triangle`.

It is verified the same way as the colour fix — all twenty titles, before and after. Fourteen are
pixel-identical; **Tetris (20 % of the frame), Cubis 2 (17 %), Mahjong (4 %), PAC-MAN (2.5 %),
Royal Solitaire (0.5 %) and Sudoku (0.25 %) all move, and every one of them moves in the same
direction**: speckle out of a background, a broken maze border made continuous, a logo's edges
made clean. The mean change is 0.003–3.8 of a level out of 255, which is the signature of "a
neighbouring texel" rather than of anything structural. `../common/src/ipod/libeapp/gles.cpp`
carries the identical tolerance, and Mini Golf's second oracle — which compares two of its own
builds and is therefore the strictest reader of a rasteriser change — is green on it.

None of the three changes is this folder's to keep: all were made in the live tree
(`<upstream>/tools/eapp-loader/src/`) and copied here, and both are the live tree's to commit.
Every other flag the recordings use (`--call-log`, `--script`, `--callgraph-dump`,
`--fixed-clock`, `--fps=0`, `--load-on-open`, `--allow-creates`, `--ctx-seed`) was already in the
emulator, and so is the per-title defaults table in `play.rs` — which gives a binary named
`Cubis2*` nothing of its own at all, and falls through to `_ => d(true, None, None, None)`
(PLAN.md difference 1) — the `find_flags_word` scan that locates this title's button-flags word
at `0x180a9db0`, and the `--allow-creates` this title needs for the two files it writes.

### What the fix was verified against (PLAN.md rule 12)

A rendering change to the emulator is a change to every title's oracle, so it was run against
**all twenty titles** in `Games_RO/`, before and after, four boot screenshots each
(`build/sweep/run2.sh`, frames 600/1500/2400/3600 of a boot with three Selects). Sixteen titles
are **pixel-identical**. The four that move are:

| title | pixels | what moved |
|---|---|---|
| Cubis 2 | 6 470–6 781 of 76 800 (8.4–8.8 %) | the point of the change: the gold window frame, the battery gauge and, off these shots, every cube on the board |
| LOST | 85 of 76 800 (0.11 %) | the battery gauge in the status bar, grey `a5a5a5` → green `00a500`. It is a `GL_LUMINANCE_ALPHA` gauge drawn with green in the register; a green battery is what the device showed |
| Mini Golf, PAC-MAN, Tetris, Ms. PAC-MAN, SAT Prep, Mahjong … | 0 | identical |

(The tie fix's own sweep is the paragraph above; the two were measured one at a time, against each
other's result, so neither is credited with the other's pixels.)

The first attempt at this sweep is worth recording because it nearly produced a wrong answer:
run through a harness that copied `/tmp/ipod-shot-NN.png` after the run rather than on the line
that announces each shot, it reported five titles changing by 85–100 % of the frame — and the
"changed" images turned out to be *other titles' title screens*, and one difference that survived
into a second attempt turned out to be the clock in the status bar reading a different minute.
That is PLAN.md rule 11 twice over, and `build/sweep/run2.sh` is the harness that does not have
either hole: each shot is copied on the line that announces it, named by the frame number that
line carries, and every run passes `--time=00:00`.

## The hashes

A commit hash names a point in *one* history, and this branch's history has been rewritten before
(the Lost recomp's manifest cites `54e1049`, which no longer exists in this repository). The files
are what the recordings actually depend on, so their hashes are recorded too — each one checked
against `git show 96bfe90:tools/…` on the day of the pin:

| file | SHA-256 |
|---|---|
| `src/lib.rs` | `d3daee4b08c5d6d7fb6dfd747cf284fee0f5e273e7fcaa8d774deb996c35cecf` (at `96bfe90`: `c0294849a3dd8e2b…`; the Vortex pin, instrument only: `fe7d730300941d25…`) |
| `src/bin/play.rs` | `acd89d589e7b215d1e078285b011a870602077b33d3a14e96f4aae1059b48c64` (at `96bfe90`: `852aac0f3af94fe3…`) — the instrument only; both fixes are entirely in `lib.rs` |
| `arm7tdmi/src/arm.rs` | `b73d0f1b4cf3e2a37113ee69e19a33b9d913a3ec4af65716c0be08d011c6f7a6` |
| `arm7tdmi/src/bus.rs` | `6108b5a557568b63e6d3efe13e378143aab6382857713125a16b8cfe607ab56c` |
| `arm7tdmi/src/cpu.rs` | `757193c48d93f25f002a103584f87f935cde98b5d6246d59f760a23d00ac3caf` |
| `arm7tdmi/src/disasm.rs` | `d1014108e3713db92700e3ee15e4620448f4c8ecc5dfe26bd08df50368c31e24` |
| `arm7tdmi/src/lib.rs` | `10f6c28d3056b398f331faf442d05b774463c0eced81e3d4db8a4cce393d2fc6` |
| `arm7tdmi/src/thumb.rs` | `0b1905dead0ec27b3d42bffce065c489056ff53c4e11812debb9882ae0d55019` |

`arm7tdmi/` is byte-identical to the Vortex, Sims Bowling, Hold'em and Lost recomps' pins
(`diff -rq`). `src/` is not, and for the first time that is deliberate rather than a coincidence
of timing: this pin carries the colour fix and theirs do not. **A screenshot compared across that
line proves nothing**, which is why each title pins its own.

**When the emulator must move** — a behaviour this title needs that the pinned copy gets wrong —
the fix goes into the live tree in its own commit, this folder is re-copied, this table is
updated, and every `tests/expected/*.calls` is re-recorded, because a recording made with one
build and compared against another proves nothing.

**`reference/PORTED.md`** is the other manifest: the files copied from the Vortex recomp rather
than from the emulator, with their hashes. `tools/port-from-vortex.py` writes it.
