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
lib.rs`, `reference/arm7tdmi/arm.rs`. Those names refer to the pinned copy under
`tools/oracle-emulator/` (`src/` is `eapp-loader/`), not to the live tree. When a claim needs
checking, check it there first, then against the live tree if the two disagree.

| what | taken from | at |
|---|---|---|
| `tools/oracle-emulator/src/` | `ipod-emulator/tools/eapp-loader/src/` | commit `96bfe90`, 2026-08-27, working tree clean |
| `tools/oracle-emulator/arm7tdmi/` | `ipod-emulator/tools/arm7tdmi/` | commit `96bfe90` |
| `tools/oracle-emulator/Cargo.toml` | the Lost recomp's, unchanged | — |
| `tools/oracle-emulator/arm7tdmi/Cargo.toml` | the emulator's, with `version.workspace = true` replaced by `version = "0.1.0"` — there is no workspace above the pinned copy to inherit from (the Lost recomp's copy carries the same line) | commit `96bfe90` |

The copy carries **no added instruments**: every flag the recordings use (`--call-log`,
`--script`, `--callgraph-dump`, `--fixed-clock`, `--fps=0`, `--load-on-open`, `--allow-creates`,
`--ctx-seed`, `--frame-reason`) is already in the emulator, and so is the per-title defaults
table in `play.rs` that applies `--load-on-open`, `--ctx-seed=0`, `--frame-reason=1` and the
asynchronous file model to a binary named `HoldEm*`.

A commit hash names a point in *one* history, and this branch's history has already been
rewritten once under an earlier pin (the Lost recomp's manifest cites `54e1049`, which no longer
exists in this repository). The files are what the recordings actually depend on, so their hashes
are recorded too:

| file | SHA-256 |
|---|---|
| `src/lib.rs` | `c0294849a3dd8e2b803cf4fb996d65cd52bf93a075e50d62c49a77e1fb3636d6` |
| `src/bin/play.rs` | `852aac0f3af94fe39c588c07bcf26b44eda55890d5aac6aad3f738f786fd078e` |
| `arm7tdmi/src/arm.rs` | `b73d0f1b4cf3e2a37113ee69e19a33b9d913a3ec4af65716c0be08d011c6f7a6` |
| `arm7tdmi/src/bus.rs` | `6108b5a557568b63e6d3efe13e378143aab6382857713125a16b8cfe607ab56c` |
| `arm7tdmi/src/cpu.rs` | `757193c48d93f25f002a103584f87f935cde98b5d6246d59f760a23d00ac3caf` |
| `arm7tdmi/src/disasm.rs` | `d1014108e3713db92700e3ee15e4620448f4c8ecc5dfe26bd08df50368c31e24` |
| `arm7tdmi/src/lib.rs` | `10f6c28d3056b398f331faf442d05b774463c0eced81e3d4db8a4cce393d2fc6` |
| `arm7tdmi/src/thumb.rs` | `0b1905dead0ec27b3d42bffce065c489056ff53c4e11812debb9882ae0d55019` |

On the day of the pin this copy was byte-identical to the Lost recomp's
(`diff -rq tools/oracle-emulator ../Lost/tools/oracle-emulator` is empty apart from build
trees). That is a coincidence of timing and not a dependency: each title pins its own, so a
re-pin in one never silently re-dates the other's recordings.

**When the emulator must move** — a behaviour this title needs that the pinned copy gets wrong —
the fix goes into the live tree in its own commit, this folder is re-copied, this table is
updated, and every `tests/expected/*.calls` is re-recorded, because a recording made with one
build and compared against another proves nothing.

**`reference/PORTED.md`** is the other manifest: the files copied from the Lost recomp rather
than from the emulator, with their hashes. `tools/port-from-lost.py` writes it.
