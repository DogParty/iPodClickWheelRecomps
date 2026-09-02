# Texas Hold'em recomp — plan of attack

**Goal by end of day:** `HoldEm_1_1_2563291.bin` — the 2006 iPod click-wheel game *Texas
Hold'em*, Apple's own, from the first wave of iPod games — running natively on the ARM Mac as a
statically recompiled C++ program, drawing into an SDL3 window, driven by the keyboard, and
*proven* equivalent to the emulator by diffing the sequence of framework calls both make on the
same scripted input. Hand decompilation starts today and does not finish today: 951 functions and
53 264 ARM instructions are not rewritten by hand in a day.

Everything lives in this folder. The emulator tree is read, snapshotted, and otherwise left alone.

This is the **third** title to go through this process, and the first to start after
`recomps/common/` exists. *Mini Golf* is finished — every one of its 333 functions is
hand-decompiled. *Lost* runs as a pure recompilation with three oracles green and its
decompilation barely begun. Between them they produced the machinery, and then they produced the
argument for the shared core: two copies of the same runtime drifted, and a fix made in one tree
was still a crash in the other a day later (`../common/README.md`). `../Mini Golf/PLAN.md` and
`../Lost/PLAN.md` are the record of how the first two were done. **Read the code quality section
of the Mini Golf plan; it binds this project unchanged.** What is written below is the delta:
what Hold'em is, what it takes from the shared core and what it still has to copy, and — the part
that actually matters — the places where Hold'em is neither Mini Golf nor Lost.

---

## What Texas Hold'em is

Measured, not guessed. Every number here is answered by a file in `analysis/` (its `README.md`
says how each was made). They come from `tools/eapp-inspect` in the emulator tree, from a static
control-flow walk of the image with the shared recompiler (`analysis/survey.txt`), from a run of
the Lost tools against this image, and from two scripted sessions through the emulator at commit
`96bfe90`.

| | |
|---|---|
| image | `HoldEm_1_1_2563291.bin`, 371 924 bytes, loads flat at `0x18000000`, ends `0x1805acd4`. Header version `0x10001000`; `eapp-inspect` warns that its block-count word says 5 while eight framework blocks are present |
| game data | `20 iPod games/Games_RO/33333/` — 295 files, ~80 MB. 111 `.ipd` textures, 89 `.anm` character animations, 15 `.blob` packs, 33 `.strings`, 15 `.txt` tables, two music tracks (`t.m4a` 3.1 MB, `c.m4a` 7.6 MB), `Holdem.raw.lcd5` |
| entry vectors | 3: `0x180315c8` start-up, `0x180315c4` terminate (header slot 1), `0x18031624` per-frame |
| functions | **872** reachable by walking from the vectors; **951** once the boot's live edges and the image's stored function pointers are added and the emitter walks to a fixpoint (500 of those it found on its own) |
| instructions | **53 264** ARM instructions recompiled — 89 302 lines of generated C++, emitted in half a second, **0 unwalkable** |
| import thunks | **433** across eight frameworks |
| frameworks | OpenGLES 179 · Metadata 152 · Audio 61 · AsyncFileIO 17 · miscTBD 15 · **Filesytem 4** (sic; the firmware's spelling) · Settings 3 · InputEvents 2 |
| ordinals a silent boot reaches | 50 in 600 frames; 53 by the name-entry screen. Seven of them neither Mini Golf nor Lost has named |
| code properties | ARM state only; the walk fails on nothing, so whatever armcc linked in — soft-float included, if it is there — the shared decoder already models |
| emulator behaviour | boots, loads its texture table, plays `t.m4a`, and reaches the **ENTER NAME** screen with correct textured artwork by frame 1300 (`analysis/coverage/name-entry-probe-frame1300.png`); ~13 000 instructions and ~3 quads a frame in the attract loop |

For scale: Mini Golf was 333 functions and 23 268 instructions; Lost is 789 and 65 423. **Hold'em
has the most functions of the three (2.9× Mini Golf) and the second-most instructions (2.3×).**
The image is the largest of the three, and about 160 KB of it is not code: tables, strings, and
whatever the poker engine keeps in ROM.

Two facts about its lineage matter for everything below. Its build number (`2563291`) is five
away from Mini Golf's (`2563296`): they were built by the same SDK in the same week, and the
binaries show it — the same 433-thunk import layout with OpenGLES first at `0x18000064`, the same
button-flags-word input model, the same asynchronous file model. Lost is from a year later and a
different developer. So for *how the game talks to the firmware*, Mini Golf's `libeapp` is the
closer relative; for *the state of the code*, Lost's tree is the newer and better-fixed copy.

---

## Code quality

`../Mini Golf/PLAN.md` § "Code quality — non-negotiable" is this project's rule set, unchanged
and unrestated: C++17 everywhere, `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` on
hand-written code, no exceptions, no RTTI, fixed-width types for anything mirroring guest memory,
`enum class` and `constexpr` over macros, RAII for every host resource, generated code confined to
`gen/` and never hand-edited, every framework entry called by its name and never by its ordinal,
every hand-decompiled function named for what it does with its guest address in a comment, every
non-obvious line explained and every claim traceable to where it was established.

Lost's two additions bind too: **7.** a claim about behaviour is worth what its evidence is worth
("Verified" and "Read carefully" are different words); **8.** nothing is copied from another title
without a provenance line in `reference/PORTED.md`.

One addition this project makes, because it is the first to start with a shared core to draw on:

**9. Nothing is copied that the shared core can carry.** A file that is the same in Mini Golf and
Lost does not get a third copy here; it moves to `../common` first and all three titles include
it from there. A file that differs between them by a *measured fact about a binary* is copied
with provenance and the fact becomes a parameter, so it too can move. Only a file that is
genuinely one title's (its game, its data manifest, its recorded oracles, its ordinal table)
stays a copy without a plan to stop being one. The rule is the one `../common/README.md` states;
this project is where it is applied at the start rather than after the drift.

---

## What is inherited, from where, and what that costs

The layers of a title, and where each one comes from now:

| layer | state today | what happens here |
|---|---|---|
| `tools/recomp/` — the ARM→C++ recompiler | **shared** (`../common/tools/recomp`) | imported; `Generator(namespace="holdem")` |
| `src/runtime/` — guest memory, CPU state, the eApp image, the frame pump | `memory.h`, `fatal` shared; `runtime.{h,cpp}` and `cpu.h` the same in both trees but still copied; `main.cpp` divergent | the same files move to `../common` first (block 0b); `memory.cpp` (region sizes) and `main.cpp` (this title's frame protocol) are copied with provenance |
| `src/framework/` — the typed platform interfaces | `types.h`, `graphics.h` shared; `controls`, `storage`, `device`, `audio` the same in both trees but copied | the same files move to `../common` first; extended here for Hold'em's ordinals |
| `src/libeapp/` — the iPod frameworks | `gles.cpp` shared; `misc`, `heap`, `host_state`, `arm_abi` the same or nearly; `async_file`, `input`, `audio`, `metadata` carry each title's model | the same files move; the model-carrying ones are copied from whichever title's model Hold'em shares (below), with provenance |
| `src/platform/` — SDL3, null, paths, settings, bindings, save store | `save_store`, `text_entry`, `music_decoder` shared; `input_bindings`, `settings` the same but copied; `sdl3_platform.cpp` divergent | the same files move; the rest copied with provenance |
| `src/gamedata/` — zip, install, manifest | identical in both trees | moves to `../common`; `manifest_data.cpp` regenerated from `33333` |
| `tests/` harness — `diff.py`, `diff.sh`, `frames.py`, `frames.sh`, `game-dir.sh`, `record.sh`, unit tests of shared code | the same in both trees | copied from Lost (the newer harness) — a shared test harness is a later, separate decision |
| `src/game/` | no | nothing to inherit; this is the work |

**How a title reaches the shared core** is settled and is not re-litigated: `../common` is added
as a subdirectory and `ipod_core` is linked through this title's own `holdem_common` interface
target, so the shared sources are compiled under this title's warning rules. Shared code is
namespace `ipod`, included as `ipod/…`, and every file that moves leaves a **forwarding header**
at the old path that includes the shared one and pulls its names into `holdem::…` with `using`
declarations — never a namespace alias, so `holdem::platform` can still hold what is genuinely
this title's. No call site changes when a file moves. `../Lost/src/platform/save_store.h` is the
shape to copy.

**What is copied is copied by a script, and only by the script.** `tools/port-from-lost.py` is
`../Lost/tools/port-from-minigolf.py` with its source tree and its rewrite table changed
(`lost`→`holdem`, `LOST_`→`HOLDEM_`, `Lost`→`Texas Hold'em` in prose) and one addition: a file's
row may name Mini Golf as its source instead of Lost, for the files where Hold'em's model is Mini
Golf's. It writes `reference/PORTED.md` with the source path and SHA-256 of every file at the
moment of the port; `--check` reports drift on either side. Its `PORTED` list is meant to shrink:
every file that later moves to `../common` leaves it, and the intention is that only this title's
own files remain.

**The debt, named.** Lost copied its layers with a promise to share later. This project starts by
keeping that promise for the files that were already the same, and takes on the smaller debt of
copying only what still differs. What it does *not* do today is share the three divergent files
(`main.cpp`, `sdl3_platform.cpp`, the model-carrying `libeapp` files) — those need the per-title
models to become parameters, which is real design work and is listed under "Not today" with the
shape it should take.

The namespace is `holdem` (`holdem::eapp`, `holdem::platform`, `holdem::game`,
`holdem::runtime`, `holdem::gfx`…), the CMake options are `HOLDEM_*`, the targets are `holdem`
and `holdem-headless`, the per-user data directory is `iPod Texas Hold'em/33333`, and the
override is `HOLDEM_DATA_DIR`. A `lost` or `minigolf` anywhere in this tree is a defect.

---

## Architecture

```
recomps/HoldEm/
  PLAN.md                this document
  README.md              layout, building, testing, contributing a decompiled function
  CMakeLists.txt         adds ../common; targets holdem (SDL3) and holdem-headless (tests)
  tools/
    survey.py            what is in the image — every number in this plan
    funcs.py             build gen/funcs.json — the function table the emitter works from
    emit.py              run the shared recompiler with this title's namespace and bindings
    progress.py          how much of the game is still recompiled rather than decompiled
    manifest.py          the game folder's file table -> src/gamedata/manifest_data.cpp
    port-from-lost.py    copy what is not yet shared, and record it (reference/PORTED.md)
    oracle-emulator/     a pinned copy of the emulator; recordings are made with this one
  gen/                   GENERATED, never hand-edited, gitignored
  src/
    runtime/             guest CPU state and memory, the eApp image, the frame pump
    framework/           what the game may ask the platform to do — graphics, audio, storage,
                         controls, device, music library. Plain C++, one namespace each
    libeapp/             the iPod frameworks implementing those headers on the host;
                         imports.json is this title's ordinal table
    gamedata/            this title's file manifest; the installer is shared
    game/                hand-decompiled game code; grows as gen/ shrinks
    platform/sdl3/       window, audio, keyboard (desktop)
    platform/null/       no I/O (headless test runs)
  tests/
    scripts/*.script     scripted input, the same FRAME: ACTION format play reads
    expected/*.calls     framework-call logs recorded from the pinned emulator — the first oracle
    unit/                stand-alone unit tests
    diff.sh, frames.sh, vs-recomp.sh, check-recomp.sh, record.sh, game-dir.sh
  analysis/              the reverse-engineering evidence for this title (README.md there)
  reference/             MANIFEST.md (the pinned emulator) and PORTED.md (the copies)
```

Everything about the guest machine — one flat reservation covering `0x11000000`–`0x40020000`,
`ld32`/`st32` through `memcpy`-based helpers, one `void f_1800xxxx(Cpu&)` per ARM function,
`goto` for intra-function branches, resolved jump tables as `switch`, `b .` as an assert trap,
flags computed eagerly by the rules ported from `arm7tdmi/arm.rs` — is exactly as the Mini Golf
plan describes it and is not re-litigated here. The recompiler is the shared one and needs no
change for this image: the walk reaches every function it is pointed at.

**Ghidra is not a dependency.** Lost proved the seed can come from the vectors, the live edges,
and the image's own stored function pointers, and `analysis/survey.txt` shows the same holds
here. Ghidra (the MCP server in `tools/ghidra/`) is for reading a function and recovering a
structure when a decompilation needs it, not for the build.

---

## Where Hold'em is neither Mini Golf nor Lost

Each of these was found by running the game through the emulator, or by reading the emulator's
own notes on it (`play.rs` carries a per-title defaults table, and Hold'em's entry has a comment
that is the best spec of items 1 and 2 there is). Each is a measured fact that has to become code
in this tree or a parameter in the shared one — never a silent edit to a copied file.

**1. The frame-reason byte does two jobs, and the first frame must be reason 0.** The game keys
everything off `ctx+0x00`. Its tick at `0x18008dec` dispatches on it through a seven-way table at
`0x18008e3c` — 0 = one-time boot, 1 = run a frame, 2/6 = idle, 3/4/5 = lifecycle — but it only
*registers* the context as its state object while the byte reads 0 (`0x18004988`:
`ldrb r0,[r0,#0] / cmp r0,#0 / bleq 0x180057f8`). Seeded to the 5 that Mini Golf's pump uses, the
registration never happens, `[0x180595d4]` stays null, every later tick reads its dispatch value
from address 0, and the game re-runs its boot case forever — the second boot builds the table
sprites before their textures load and dies in a divide by zero. The emulator's answer is
`--ctx-seed=0` with steady reason 1, and the frame pump here does the same thing, in one named
constant with this paragraph beside it. Lost's `main.cpp` already models a reason byte the game
writes back (`first0:1`); Hold'em's protocol is a third variant and is the reason `main.cpp` is
copied rather than shared today.

**2. The file model is both of the others at once.** Hold'em issues `AsyncFileIO #3` with a
buffer — an open that is a load, Lost's model — *and* expects the firmware to park the request
and call back later, Mini Golf's asynchronous completion model. Against a synchronous open it gets
a handle it never uses, `Data/textures.txt` is opened and never read, its texture table stays
zeroed, and the loader walks off the end of an unterminated descriptor list opening NULL names
~700 times before the runtime aborts at `0x1800839c` ("Abnormal termination"). The emulator runs
it with `--load-on-open --async-files`. The recomp's `async_file.cpp` is copied from Mini Golf
(the completion queue) and taught Lost's open-as-load, and the boot oracle is what says it is
right: the first divergence in `tests/diff.sh boot` will be here if it is not.

**3. Input is Mini Golf's flags word, at a different address.** The emulator finds the button
flags word at `0x180597a8` and reports *no* press-time words for this title — so a held Menu
cannot be told from a tap the way Mini Golf's could. `input.cpp` comes from Mini Golf's model;
the addresses are this title's constants. How the wheel is read (detents per row, direction) is
measured from `walk`-style scripts against the name-entry wheel on screen, the way Lost did.

**4. Eight frameworks, and seven ordinals nobody has named.** `analysis/ordinals.txt` lists every
ordinal the two sessions reached against what the other titles call it. New to this project:
`OpenGLES #35` (`glDisable` per the reversing table; the emulator answers zero), `#45`
(`glGenTextures`), `#148` (`glUniform4xvAPPLE` — 1 235 calls in 600 frames, roughly one per
draw, so it is not decorative), `#169` and `#173` (unknown; 69 calls each, exactly the number of
clears — once a frame on frames that clear), `Audio #50` (the emulator answers zero), and the
`Filesytem` framework, four ordinals no recorded session has reached yet (the emulator knows
`#0` treats zero as success and `#1` unregisters a handle). `Metadata #62` and `#134` are reached
and Lost has named them. Every one of these gets a name in `imports.json` from the pinned
`lib.rs` and `../Mini Golf/reference/reversing/*.json` before block 4 begins; the two unknown GL
ordinals are named by what the call log shows them being passed, and marked "(inferred)" until a
picture proves them.

**5. The data is in formats neither title has read.** Mini Golf loaded `.xml` course records and
`.wav` sounds; Lost loaded `.dat` chapters. Hold'em has `.ipd` textures by the hundred, `.anm`
character animations, `.blob` packs (`Textures/constant.blob`, `Textures/ingame.blob`,
`Sounds/sounds.blob`, one per location), plain-text tables in `Data/` (`textures.txt` names the
textures and their sheet geometry, `seasons.txt` the tournament ladder, `aiavatars.txt` the
opponents' playing tendencies as four rows of four numbers each, `avataranims.txt` the animation
frame lists), its own fonts under `Fonts/Euro` and `Fonts/Ja`, and eleven `.lproj` localisations
of `.strings` files. None of that is a framework's business — the game reads bytes through
`AsyncFileIO` and decodes them itself — so none of it blocks the recompilation. All of it is
decompilation work: each loader is a well-bounded function with a file format on the other side,
which makes the loaders the best early targets after the string helpers.

**6. Sound is a bank, not files.** `Data/sounds.txt` names `Sounds/sounds.blob` and then the
`.wav` names *inside* it; the folder holds no `.wav`. The 27 `audio_sfx_create` calls at boot are
the game registering slices of that blob. Mini Golf's by-name `.wav` scheme (which Lost had to
rip out) does not apply here either; the host's audio layer serves buffers the game hands it,
which is what `framework/audio.h` already says. Two music streams are registered (`t.m4a` for the
title, `c.m4a` presumably for play) and the shared decoder plays them.

**7. It is the biggest game so far, and the interesting part is not on the boot path.** A poker
game is a rules engine (hand evaluation, betting rounds, side pots), an AI (the tendency tables
in `aiavatars.txt`), a tournament ladder (`seasons.txt`), and a presentation layer with animated
opponents. The boot and name-entry oracles will prove the recompilation; they will not exercise
any of that. The plan for the oracles therefore reaches into play as soon as the wheel is
understood: a script that names a player, enters the first season, and plays a hand is the
oracle that matters, and it is recorded on day one if the emulator gets there (it renders the
title correctly; whether it plays a hand is item 1 under Risks).

---

## Schedule

Each block ends in something runnable *and* reviewed against the quality rules. If a block
overruns, the fallback is named. Cutting quality is never the fallback.

### Block 0 — scaffold, provenance, and the pinned emulator (30 min)

* `README.md`, `.clang-format`, `.gitignore` (`gen/`, `build*/`), `pyproject.toml`.
* `tools/port-from-lost.py` from Lost's script; run it. `reference/PORTED.md` written.
* `reference/MANIFEST.md` and `tools/oracle-emulator/`: `tools/eapp-loader` and `tools/arm7tdmi`
  at commit `96bfe90`, plus Lost's detached `Cargo.toml`. Record the commit **and** the SHA-256
  of `lib.rs` and `play.rs`: Lost's manifest cites a commit (`54e1049`) that this branch's history
  does not contain, and the sources are what the recordings depend on. Today the copy is
  byte-identical to Lost's pin, which is worth a line in the manifest and nothing more — each
  title pins its own, so a re-pin in one never silently re-dates another's recordings.
* `CMakeLists.txt` from Lost's with the names changed: `add_subdirectory(../common)`,
  `holdem_common` / `holdem_strict` / `holdem_generated`, `HOLDEM_SANITIZE`,
  `HOLDEM_REGION_MEMORY`, `HOLDEM_SDL3`, `HOLDEM_GEN_DIR`, Release = `-O3 -g` with assertions
  live, RelWithDebInfo = `-O1 -g`. Empty layer directories skipped with a `message(STATUS)` so the
  tree configures at every stage of this plan.
* `tools/manifest.py "…/Games_RO/33333"` → `src/gamedata/manifest_data.cpp`. The tool must
  ignore what the *game* writes into its folder, not only `*.sav`: Mini Golf's reference folder
  was found today to contain a `stats` file the game had written, and it was in the manifest.
  Hold'em's own file is `<lang>.lproj/data.txt` at the folder root (created empty at boot —
  progress log), and the reference folder under `Games_RO/33333` must be checked for one before
  the manifest is generated.

**Exit:** `cmake -B build` configures; `python3 tools/survey.py` reproduces `analysis/survey.txt`.

### Block 0b — promote what is already the same (1 h, and the one block that touches other trees)

This is rule 9 made concrete, and it is done *before* this title depends on any of the files, so
it is a mechanical move rather than a three-way merge.

* For each file `../common/README.md` lists as identical after namespace normalisation
  (`runtime/runtime.{h,cpp}`, `libeapp/misc.cpp`, `libeapp/heap.{h,cpp}`,
  `framework/{controls,storage}.h`, `platform/input_bindings.*` if the action list can be
  parameterised, `gamedata/{zip,install,manifest}.*`): move it to `../common/src/ipod/…` in
  namespace `ipod`, add it to `ipod_core`, leave a forwarding header in Mini Golf and Lost, and
  delete their copies.
* For the near-identical ones (`cpu.h` 94%, `host_state` 98%, `arm_abi` 95%, `device.h` 99%,
  `audio.h` 97%, `settings` 95%): `diff` the two, decide which side is the fix and which is the
  fact, move the fixed version, and make the fact a parameter. If a diff turns out to be neither
  in five minutes of reading, leave that file for a later pass and say so in the progress log.
* **Verify:** Mini Golf `ctest` all green and `tests/check-recomp.sh` still byte-identical; Lost
  `ctest` green. Both trees' `port-from-minigolf.py --check` / this tree's `--check` updated.

**Fallback:** if this block reaches an hour with files still in flight, stop, leave the rest
copied with provenance, and put the remainder at the top of "Not today" by name. Three copies of
a file for a week is a bounded cost; a half-moved file that breaks Mini Golf's oracle is not.

### Block 1 — emit (30 min)

* `tools/funcs.py` with this title's paths (`Games_RO/33333/Executables/HoldEm_1_1_2563291.bin`,
  `analysis/coverage/`, `analysis/extra-entries.txt`); `tools/emit.py` with
  `Generator(namespace="holdem")`; `src/runtime/arm_runtime.json` empty, kept because the emitter
  reads it and it is where a hand-written runtime routine would be named.
* This was rehearsed today with the Lost tools against this image: 884 seed entries, 951 emitted,
  0 unwalkable, 359 thunks without a binding under Lost's `imports.json`. The number to reproduce
  is 951.

**Exit:** `gen/` compiles under `holdem_generated`. **Fallback:** none should be needed; if an
idiom this image uses is not modelled, it is a shared-recompiler fix, made in `../common` and
verified against Mini Golf's pure recompilation before it is used here.

### Block 2 — runtime and the frame pump (1 h)

* `src/runtime/` from Lost, `memory.cpp` region sizes to be re-measured from the longest
  recorded session before the day ends (start with Lost's; they are generous).
* `main.cpp`: the reason protocol of difference 1 — seed 0 for the init call, steady 1 — as named
  constants with the dispatch-table addresses cited; `TERMINATE_VECTOR_SLOT = 1` (the terminate
  vector is header slot 1, as in Lost; calling it at start-up runs the whole shutdown).
* Every ordinal logging and returning 0.

**Checkpoint A:** `build/holdem-headless … --frames=2` runs the start-up vector and the first
frame and its log begins the way `analysis/coverage/boot-summary.txt`'s run began:
`miscTBD#0 … miscTBD#6`.

### Block 3 — the oracle (30 min)

* `tests/record.sh` with this title's flags, which are part of every case and not a matter of
  taste: `--load-on-open --allow-creates --fixed-clock --fps=0`. (`--async-files` and the
  reason/seed are already the emulator's defaults for a binary named `HoldEm*`; write them out
  anyway, so the case does not depend on a defaults table in someone else's tree.)
* Record `boot` (`600: quit`, 28 138 calls) and `name-entry` (the probe script from
  `analysis/scripts/`, trimmed to what it needs). Keep every script short and ending in `quit`.
  `tests/game-dir.sh` gives every run a fresh copy of the folder, as in Lost, because the game
  creates `<lang>.lproj/data.txt` on its first boot and what is in it changes the next boot.
* `imports.json`: start from the union of both titles' tables, add the seven unnamed ordinals
  and the `Filesytem` framework from `lib.rs`, with argument counts, so `diff.py` compares on the
  arguments each really takes.

**Exit:** `tests/diff.sh boot` runs and reports its first divergence with a line number.

### Block 4 — libeapp, in boot order (3 h)

In the order the boot log calls them, re-running `tests/diff.sh boot` after each group:
miscTBD / Settings / InputEvents → AsyncFileIO (difference 2 — expect the first real fight here)
→ Metadata (`#62`, `#134`, named by Lost) → Audio (the bank slices of difference 6; `#50`) →
OpenGLES (the shared rasteriser, plus `#35`, `#45`, `#148`, `#169`, `#173`). Then
`tests/diff.sh name-entry`.

Two shared-rasteriser rules need this title's answer, and the picture oracle is the only thing
that can give it: whether Hold'em re-points every attribute before every draw
(`gfx::set_attributes_repointed_per_draw`) and whether it ever writes the constant colour
register. Neither shows in a call log. Read the boot log for the `#137` pattern first; confirm
with `frames.sh`.

**Checkpoint B:** `tests/diff.sh boot` and `tests/diff.sh name-entry` pass — the recomp is
*proven* before a single pixel is visible. **Fallback:** if AsyncFileIO is still diverging at the
two-hour mark, the boot case is the deliverable and name-entry moves to tomorrow; the divergence
and what was read in `lib.rs` about it go in the progress log.

### Block 5 — SDL3 and the picture (1 h)

* `src/platform/` from Lost; `sdl3_platform.cpp` with the title's name and bindings; the shared
  music decoder through `IPOD_CORE_SDL3_SOURCES`.
* `tests/frames.sh` from Lost, with `shot` frames in the name-entry script, compared against the
  emulator's PNGs at a threshold, not a hash.

**Checkpoint C:** the ENTER NAME screen on screen, natively, and the wheel spelling a name.

### Block 6 — the first oracle that plays poker, then decompilation (the rest)

* Measure the wheel (difference 3), then write `first-hand.script`: name, first season, one hand
  played to a showdown, `quit`. Record it. If the emulator cannot get there, that is the first
  finding of the progress log and the first thing to fix in the emulator tree — in its own
  commit, followed by a re-pin.
* `tests/vs-recomp.sh` and `tests/check-recomp.sh` ported from Mini Golf on day one, not later:
  Lost noted it lacked them, and Mini Golf learned that the pure recompilation is the oracle that
  never expires.
* Then the swap loop, each swap diffed: the string and table-parsing helpers (`textures.txt` and
  `sounds.txt` are read by the same parser, almost certainly), the tick dispatcher at
  `0x18008dec` with its seven-way table, the loaders of difference 5, and only then the engine.
  The order is the Mini Golf order — top-down from the vectors — because it is the order in
  which the state structures are forced into the open.

---

## Risks, in the order they will bite

1. **The emulator may not play a hand.** It renders the title and the name-entry screen; nothing
   yet shows it dealing cards. If it stops short, the boot and name-entry oracles still prove the
   recompilation, but the engine has no oracle until the emulator is fixed — and that fix is an
   emulator-tree change, which this plan otherwise avoids. Mitigation: try the `first-hand` script
   in block 6 *before* decompiling anything, so the answer is known early.
2. **Difference 2 is a model neither title's `async_file.cpp` implements.** Open-as-load with a
   deferred completion is a small change to Mini Golf's file, but "small" is what every file-I/O
   change has been called in both previous projects before it cost an afternoon. Mitigation: the
   boot log has 32 `#3` calls and 14 `#2` reads; read what `lib.rs` does for each *before*
   writing, then diff.
3. **Block 0b breaks a green tree.** Moving files under two finished projects is the one thing
   here that can regress work already verified. Mitigation: every move is followed by both
   `ctest` runs before the next; the fallback is named; the block has a clock.
4. **The two unknown GL ordinals.** `#169` and `#173` are called once per frame that clears and
   have no name anywhere. If they are state the rasteriser needs (a scissor, a viewport), the
   call-log oracle will be green and the picture wrong. Mitigation: `frames.sh` in block 5, and
   the argument values in the log — a viewport is four numbers that look like `0 0 320 240`.
5. **The per-title models in `libeapp` will want to be shared before they are understood.** Three
   variants of a frame-reason protocol and two of a file model are exactly the parameters the
   shared core is for — and exactly the kind of abstraction that goes wrong when designed from
   two examples. Mitigation: do not share them today; write each as a named constant with its
   evidence, and let "Not today" carry the design.
6. **Scripts that press the wheel are fragile as oracles.** Both earlier titles found that a
   wheel gesture split across frames records differently from run to run. Mitigation: single-row
   `wheel ±N` bursts at least 20 frames apart, as Mini Golf's Risk 4 prescribes.

---

## Not today, written down so it is not forgotten

* **Share the model-carrying files.** `main.cpp`'s frame pump wants a `FrameReasonProtocol` with
  three known instances (Mini Golf: seed 5; Lost: first 0 then 1, game writes back; Hold'em: seed 0
  then 1). `async_file.cpp` wants `open_is_load` and `completions_are_deferred` as two flags.
  `input.cpp` wants "flags word at address X, press times at Y or absent" against "event-list
  nodes". Each is a measured fact per title and a parameter in `ipod`. When the third instance of
  each is written and green, the shape is known and the move is mechanical.
* **A shared test harness.** `diff.py`, `frames.py`, `game-dir.sh` are copied for the third time
  today. They belong in `../common/tests/` with the title's image path and flags as arguments.
* **The three-way `--check`.** `port-from-*.py --check` in each tree compares a copy with one
  source. With three trees the useful question is "which files are still copies anywhere", and
  that is one script over all three `PORTED.md` files.
* **Localisation.** Eleven `.lproj` folders and the `Fonts/Ja` set. The port should be able to
  run in any of them; the game already can, and the setting is a `Settings #0` answer.
* **The Switch.** Mini Golf has a platform; nothing here should make a second one harder.

---

## Progress log

Each entry says what was established, not what was attempted, and ends with what was **not**
verified.

### 2026-08-27 — before the first line: what the image and the emulator say

Done before this tree existed, with the Lost tools and the Lost-pinned emulator (byte-identical
to `tools/eapp-loader` at `96bfe90`), to size the job and find the differences above. Everything
is in `analysis/`.

* `survey.py` against the image: 872 functions and 51 516 instructions from the vectors, 0
  unwalkable; eight frameworks, 433 thunks. `funcs.py` with the boot edges: 884 seed entries.
  `emit.py` with Lost's bindings: 951 functions, 53 264 instructions, 89 302 lines, 0.5 s.
* A 600-frame silent boot: 28 138 framework calls, 1 837 quads, 69 clears, 50 ordinals;
  `t.m4a` playing. A 1 300-frame probe with five Select presses reaches ENTER NAME with correct
  textured artwork (`coverage/name-entry-probe-frame1300.png`) and three more ordinals
  (`OpenGLES #21`, `miscTBD #10`, `#11`). In both runs the game created exactly one file: an
  empty `en.lproj/data.txt` at the folder's root — its save/statistics file, in a folder named
  for the current language (the shipped `Data/en.lproj/` and `Localization/en.lproj/` are
  different folders) — and wrote nothing into it before a name was entered.
* Seven reached ordinals are unnamed in both existing titles (`ordinals.txt`); the `Filesytem`
  framework was not reached.
* The emulator's `play.rs` carries Hold'em's frame-reason and file-model facts (differences 1
  and 2) in its defaults table, with the addresses cited above.

**Not verified:** anything past the name-entry screen — whether the emulator deals a hand is
unknown (Risk 1). The wheel model. Region high-water marks. Whether soft-float is linked in (the
walk does not fail, which is a different statement from "not present"). The meaning of `#169`
and `#173`. Which of the near-identical files in block 0b are fix and which are fact.

### 2026-08-27 — blocks 0 to 5: the recompilation runs, and both oracles are identical

Done the same evening, in the order of the schedule. Established:

* **Block 0.** `tools/port-from-lost.py` copied 73 files from Lost with identifier rewrites
  only (`lost`→`holdem`, `LOST_`→`HOLDEM_`, the two data-directory strings); prose that names
  Lost was deliberately left true rather than rewritten false, and `grep -rn '\bLost\b' src tests
  tools` is the worklist for adopting each file. The emulator is pinned at `96bfe90` with file
  hashes as well as the commit (`reference/MANIFEST.md`); the copy is byte-identical to Lost's
  pin, including the one-line `arm7tdmi/Cargo.toml` change a detached crate needs. The manifest
  has 294 files; `tools/manifest.py` now ignores a `<lang>.lproj/` folder at the root, which is
  where this game's save goes.
* **Block 0b.** Seven files moved to `../common` from Lost's versions — `runtime/cpu.h` (the
  superset that models `mrs`/`msr`), `runtime/runtime.{h,cpp}`, `libeapp/heap.{h,cpp}`,
  `gamedata/zip.{h,cpp}` — with forwarding headers in all three trees, `runtime.h` keeping only
  each title's `game::call_indirect` declaration. Mini Golf rebuilt: 30/30 (its baseline, taken
  before the move, was also 30/30). Lost rebuilt: 17/17. The four framework headers
  (`controls`, `device`, `storage`, `audio`) were moved and put back within the hour: they
  declare what each title's `libeapp` *implements*, and a shared declaration drags three
  implementations into `ipod::` with it — the "interface and implementation cross together"
  rule, met in practice. `../common/README.md` records both.
* **Block 1.** `funcs.py` → 884 seed entries; `emit.py` → 951 functions, 53 264 instructions,
  0 unwalkable, half a second. `gen/` compiles under `holdem_generated`.
* **Block 2.** `main.cpp` rewritten around difference 1 and 3: the context is seeded 0 for the
  init call and 1 every frame, buttons are a flags word at `0x180597a8` with no press times,
  no wheel sample is refilled between frames (the recordings are made without `--wheel-rotate`),
  and the emulator's sixteen-byte event-node allocation is still taken so every later heap
  address agrees. Lost's touch/spin gestures and cheats are gone. **Checkpoint A:** the first
  two frames match the emulator call for call, heap addresses included.
* **Block 3.** `tests/record.sh` spells out `--load-on-open --ctx-seed=0 --frame-reason=1
  --allow-creates --fixed-clock --fps=0` (`--async-files` is not a flag: asynchronous completion
  is the emulator's default and `--sync-files` the exception). Two cases recorded: `boot`
  (28 138 calls) and `name-entry` (52 434 calls, five Select presses, a screenshot at 1300).
* **Block 4.** Three divergences, each found by the oracle and each a real fact:
  1. call 23 — `glGenTextures` (#45) was unimplemented, so the game bound texture 0. The shared
     rasteriser gained `gen_textures` (names from 1, as the driver's counter), and with it
     `disable` (#35), the vector constant colour (#148 — roughly one call per draw here), and
     the `mat4` helpers `matrix_translate` (#169) and `matrix_rotate` (#173): so `#169`/`#173`
     are answered, and they are called once per frame that clears because the game rebuilds
     its model-view each frame.
  2. call 220 — a 512×230 font texture where the emulator had 512×57. Lost's file layer finds a
     file by *bare name* anywhere under the folder, and `Data/en.lproj/fonts.txt` and
     `Data/ja.lproj/fonts.txt` share a name and a size; the English game got its Japanese font.
     `find_resource` now honours the path as given and falls back to the name search.
  3. call 1589 — the save. The game writes `en.lproj/data.txt` (320 KB, its whole scratch
     buffer) at frame 31 and, in the recording, goes on to load `tutorial.strings` in frame 32.
     The emulator's write path is behind `EAPP_OP3_WRITES` and *off*: it moved nothing and
     reported the write failed, and the game went on without its save. With the write allowed
     to succeed — in the emulator (`EAPP_OP3_WRITES=1`, 327 680 bytes on disk) and in the
     recomp alike — the game does *not* go on: it opens the file again, writes four bytes, and
     sits. So under `--emulator-firmware` the recomp refuses writes the way the emulator does
     (`storage::set_writes_refused`), which is what the recordings depend on; a real run
     writes. What the firmware answered a successful write with, and what the game waits for
     after one, is the open question below.
  **Checkpoint B:** `diff.sh boot` and `diff.sh name-entry` are identical, semantic and exact.
* **Block 5.** The SDL3 `holdem` target builds and runs the name-entry script in a window to
  the end; `tests/frames.sh name-entry` passes, and the native frame at 1300 is the emulator's
  ENTER NAME screen. **Checkpoint C.** `ctest`: 11/11.

**Not verified:** what happens after a *successful* save — the game's four-byte follow-up write
and what it expects back (Risk 2, still open; a real run keeps the 320 KB file and the game may
stall on it). Whether this game re-points its attributes per draw: the picture oracle passes at
the name screen with the conservative default, which is evidence for one screen. The wheel
(no case turns it yet). Region high-water marks (Lost's sizes, unmeasured here).

### 2026-08-27, later — block 6 begins: the emulator deals a hand, and so does the recomp

* **Risk 1 is retired.** Probing in the emulator: Play does nothing at the name screen, Menu
  backs out to a SAVING… screen (the open save question again), and Select types a letter —
  after eight the wheel jumps to a ✓ glyph and a ninth Select confirms the name. START GAME
  (NEW GAME / TUTORIAL) follows, and two more Selects deal: by frame 3500 the player holds
  5♥ 10♥ at a six-seat table with the blinds posted and a $30 pot
  (`analysis/coverage/first-hand-frame3500.png`). `tests/scripts/first-hand.script` is that
  path, recorded: 245 185 calls.
* **A recompiler bug, found by the oracle at call 224 904.** The game read address 9 as a
  pointer seven frames after the CONTINUE press. The stack words at the fatal showed SP still
  inside the tick's frame, and an entry trace of that frame (every function, `--trace-from`,
  `--trace-entry`) showed which callee returned with SP 24 bytes low: `f_1800b850`, whose path
  at `0x1800b8e4` does `add lr, pc, #0xc`, two loads, a `mov`, then `bx r2`. The shared
  recompiler looks back three instructions for the `add lr, pc` that makes a register jump a
  call; this one was four back, so the `bx` was emitted as a tail jump and the instructions
  after it — that path's six-register epilogue — never existed. `LINK_SETUP_WINDOW` is 8 now
  and the scan stops at any intervening `bl`; the equality test on the prepared return address
  is what makes the wider window safe. Re-emitting Lost gained the same twelve instructions in
  two of its functions (`0x18002940`, one other), which its oracles had never reached; Lost is
  17/17 on the new emit and Mini Golf's pure recompilation is unchanged (5 of 6 exact; `next-hole`
  as before). **After the fix `first-hand` is identical, semantic and exact.**
* **Then the picture, which the call log cannot see.** `frames.sh first-hand` differed on 42% of
  pixels: no felt, no card backs, the player's cards huge at the bottom-left. Draw by draw
  (`IPOD_VERTEX_HASH` against the emulator's `--draws`), 14 659 draws agreed and the first flying
  card did not; the matrices agreed at frame end; watching the model-view's first word on both
  sides (`--watch-mem` / `HOLDEM_WATCH_LOG`) showed the emulator's going `1.0 → 0.5` by a driver
  stub the recomp never ran: **`#171 scalef`**, called 530 times in the recording and answered
  with a logged zero — the call-log oracle passes an unimplemented ordinal by construction.
  With it, `#105 glTexSubImage2D` (one call: the felt, into a full-screen RGB565 texture). Both
  are in the shared rasteriser now, `#105` decoding through the existing upload into a scratch
  name as the emulator does. 42% → 2.5%, all on the edges of the half-size card backs: the
  shared rasteriser's box filter for minified textures, a deliberate improvement on the
  emulator's nearest sampling, now stays off under `--emulator-graphics`, which is the flag
  whose whole purpose is to draw as the emulator does. **The table frame is pixel-identical.**
  (Tried and reverted on the way: `-ffp-contract=off` for the rasteriser — it changed nothing,
  so it is not in the tree.)
* `ctest`: 16/16 — eight unit tests, three cases each in semantic and exact form, two pictures.
* The first-hand recording's branch edges (`analysis/coverage/edges-first-hand.txt`, 7 453 of
  them) went into the seed: still 951 functions, but 53 499 instructions — 223 more, in blocks
  the static walk could not reach and a live edge could. Numbers elsewhere in this document that
  say 53 264 or 53 276 are the earlier seeds.

**Not verified:** the save (unchanged from above). Whether the wider link window mis-reads any
`bx` that really is a tail jump — the equality test says it cannot, and every oracle in three
titles is green, but that is the measure. Nothing after the first betting round: the script ends
one Select past the deal. Whether the `#171`/`#105` implementations are right for arguments other
than the ones this recording passes (scale by 0.5; a full-screen RGB565 patch at 0,0).
