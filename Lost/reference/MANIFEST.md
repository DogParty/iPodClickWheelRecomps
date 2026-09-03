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
| `tools/oracle-emulator/src/` | `<upstream>/tools/eapp-loader/src/` | commit `54e1049` |
| `tools/oracle-emulator/arm7tdmi/` | `<upstream>/tools/arm7tdmi/` | commit `54e1049` |
| `tools/oracle-emulator/Cargo.toml` | the Mini Golf recomp's, unchanged | commit `54e1049` |

Unlike the Mini Golf recomp's pinned copy, this one carries **no added instruments**: the
`--call-log=FILE` flag it needs is already in the emulator. Nothing in the emulator tree had to
change for this project.

Refreshing the snapshot is a deliberate act: re-copy, update this table, and re-record every
`tests/expected/*.calls`, because a recording made with one build and compared against another
proves nothing.

`PORTED.md` beside this file is the other half of the provenance story — the files copied from
the Mini Golf recomp rather than from the emulator.
