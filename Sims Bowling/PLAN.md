# The Sims Bowling recomp — plan of attack

**Goal by end of day:** `SimsBowling_1_1_3002478.bin` — Electronic Arts' 2007 iPod click-wheel
game *The Sims Bowling* — running natively on the ARM Mac as a statically recompiled C++ program,
drawing into an SDL3 window, driven by the keyboard, and *proven* equivalent to the emulator by
diffing the sequence of framework calls both make on the same scripted input. Hand decompilation
starts today and does not finish today: 2 402 functions and 71 924 ARM instructions are not
rewritten by hand in a day.

Everything lives in this folder. The emulator tree is read, snapshotted, and otherwise left alone.

This is the **fourth** title to go through this process, and the second to start with
`common/` in place. *Mini Golf* is finished — every one of its 333 functions is
hand-decompiled. *Lost* and *Texas Hold'em* run as pure recompilations with their oracles green,
their decompilations barely begun. `../Mini Golf/PLAN.md`, `../Lost/PLAN.md` and
`../HoldEm/PLAN.md` are the record of how the first three were done, and `../common/README.md` is
the record of what they turned out to have in common. **Read the code quality section of the Mini
Golf plan; it binds this project unchanged.** What is written below is the delta: what Sims
Bowling is, what it takes from the shared core and what it still has to copy, and — the part that
actually matters — the places where Sims Bowling is none of the other three.

---

## What The Sims Bowling is

Measured, not guessed. Every number here is answered by a file in `analysis/` (its `README.md`
says how each was made). They come from `tools/eapp-inspect` in the emulator tree, from a static
control-flow walk of the image with the shared recompiler (`analysis/survey.txt`), from a
rehearsal of the Hold'em tools against this image, and from eight scripted sessions through the
emulator at commit `96bfe90`.

| | |
|---|---|
| image | `SimsBowling_1_1_3002478.bin`, 475 536 bytes, loads flat at `0x18000000`, ends `0x18074190`. Header version `0x10001000`; `eapp-inspect` warns, as it did for Hold'em, that its block-count word says 5 while seven framework blocks are present |
| game data | `Games_RO/1500C/` — 63 shipped files, ~53 MB. One 19 997 809-byte resource library `gameLib.rlb`, 31 `.wav` sound effects and 9 `.m4a` music tracks at the root, `rserver.bin` (105 020 bytes, byte-identical to Lost's), `SimsBowling_Launch.raw.lcd5`, and `Resources/<lang>/` — a `Description.xml` and a guide `.jpg` for each of seven languages. Plus one file the game *wrote*: `savefile.dat` (below) |
| entry vectors | 3: `0x18045588` start-up, `0x18045504` terminate (header slot 1), `0x180455e4` per-frame |
| functions | **501** reachable by walking from the vectors — a fifth of the program; **2 168** seed entries once the two probes' live edges and the image's stored function pointers are added (**768** of those, the most of any title by far); **2 402** once the emitter walks to a fixpoint |
| instructions | **71 924** ARM instructions — the most of the four titles; **1 unwalkable** before the recompiler learned its idiom (difference 8), none after |
| import thunks | **429** across seven frameworks |
| frameworks | OpenGLES 179 · Metadata 152 · Audio 61 · AsyncFileIO 17 · miscTBD 15 · Settings 3 · InputEvents 2 — the same seven, in the same layout, as Lost |
| ordinals a silent boot reaches | 59 in 3 000 frames. Three of them no title has named |
| code properties | ARM state only; the walk fails on exactly one instruction shape in the whole image (an unconditional computed jump in armcc's 64-bit divide — difference 8) |
| emulator behaviour | loads its resources in the first 327 frames (642 file operations), is at the **Main Menu** — *Bowl Now! / Sims Life / Pass'n Play / Volume / Options / Highlights*, the cast montage, the clock — by frame 500 with correct artwork (`analysis/coverage/menu-frame500.png`), and two Select presses later shows the **Controls** screen; ~71 framework calls a frame while loading, ~106 at the menu, most of them the clock |

For scale: Mini Golf was 333 functions and 23 268 instructions, Lost 789 and 65 423, Hold'em 951
and 53 499. **Sims Bowling has 2.5× Hold'em's functions and more instructions than any of them**,
and the 768 stored function pointers say what kind of program it is: an engine of vtables and
dispatch tables — EA's, not Apple's — with the game on top. The 501 functions the vectors reach
directly are the runtime and the frame loop; everything else is behind a pointer.

Two facts about its lineage matter for everything below. It is a **2007 EA title on the 2008
SDK**: its build number (`3002478`) is above Lost's (`2917525`) and its import layout is Lost's
to the byte — the same 429 thunks, `OpenGLES` first at `0x18000064`, `Settings` last. So for *how
the game talks to the firmware* Lost is the close relative (event-list buttons, the render server,
fixed-point matrices, open-as-load), and the emulator's own notes group it with Lost and Sims
Pool at every turn. For *the state of the code*, Hold'em's tree is the newest copy of the shared
layers — it has the forwarding headers into `../common`, the path-honouring file lookup and the
tracing aids — and is what this tree copies from.

---

## Code quality

`../Mini Golf/PLAN.md` § "Code quality — non-negotiable" is this project's rule set, unchanged
and unrestated: C++17 everywhere, `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` on
hand-written code, no exceptions, no RTTI, fixed-width types for anything mirroring guest memory,
`enum class` and `constexpr` over macros, RAII for every host resource, generated code confined to
`gen/` and never hand-edited, every framework entry called by its name and never by its ordinal,
every hand-decompiled function named for what it does with its guest address in a comment, every
non-obvious line explained and every claim traceable to where it was established.

Lost's two additions bind (**7.** a claim is worth its evidence; **8.** nothing copied without a
provenance line in `reference/PORTED.md`), and so does Hold'em's (**9.** nothing is copied that
the shared core can carry; what differs by a measured fact becomes a parameter).

One addition this project makes, because it is the first whose *shared-core work is on the
critical path*:

**10. A shared-core change is verified in every title before it is used here.** Two things this
title needs do not exist yet and belong to no title — a control-flow idiom the recompiler does
not model, and `glDrawElements` in the rasteriser. Each is made in `../common`, and before this
tree depends on it Mini Golf's `tests/check-recomp.sh` must still be byte-identical and Lost's
and Hold'em's `ctest` still green. A fix that is right for this image and wrong for another is
not a fix; it is drift with a better excuse.

---

## What is inherited, from where, and what that costs

The layers of a title, and where each one comes from now:

| layer | state today | what happens here |
|---|---|---|
| `tools/recomp/` — the ARM→C++ recompiler | **shared** (`../common/tools/recomp`) | imported; `Generator(namespace="bowling")`; one idiom added (difference 8) |
| `src/runtime/` | `cpu.h`, `runtime.{h,cpp}`, `memory.h`, `fatal` shared; `eapp_image`, `memory.cpp` copied; `main.cpp` per title | copied from Hold'em; `main.cpp` rewritten around differences 1 and 2 |
| `src/framework/` — the typed platform interfaces | `types.h`, `graphics.h` shared; `controls`, `storage`, `device`, `audio`, `music_library` copied (they declare what each title's `libeapp` implements — `../common/README.md` says why they stay) | copied; extended for this title's three new ordinals |
| `src/libeapp/` — the iPod frameworks | `gles.cpp`, `heap` shared; `misc`, `host_state`, `arm_abi`, `framework_call` near-identical copies; `async_file`, `input`, `audio`, `metadata` carry each title's model | copied from Hold'em; `async_file.cpp` taught the resource-library rules of difference 3; `gles.cpp` in the core gains `draw_elements` (difference 4) |
| `src/platform/` | `save_store`, `text_entry`, `music_decoder` shared; the rest copied | copied; `sdl3_platform.cpp` with this title's name and bindings |
| `src/gamedata/` | `zip` shared; `install`, `manifest` copied | copied; `manifest_data.cpp` regenerated from `1500C` |
| `tests/` harness | copied in every tree | copied from Hold'em — a shared harness is still a separate decision (Not today) |
| `src/game/` | no | nothing to inherit; this is the work |

**How a title reaches the shared core** is settled: `../common` is added as a subdirectory and
`ipod_core` is linked through this title's own `bowling_common` interface target, so the shared
sources are compiled under this title's warning rules. Shared code is namespace `ipod`, included
as `ipod/…`; forwarding headers at the old paths pull the names into `bowling::…` with `using`
declarations. No call site knows which side of the line a file is on.

**What is copied is copied by a script, and only by the script.** `tools/port-from-holdem.py` is
Hold'em's `port-from-lost.py` with its source tree and rewrite table changed (`holdem`→`bowling`,
`HOLDEM_`→`BOWLING_`, the data-directory strings). It writes `reference/PORTED.md` with the source
path and SHA-256 of every file at the moment of the port; `--check` reports drift on either side.
As in Hold'em, **identifiers are rewritten and prose is not**: a comment that says "Hold'em issues
`#3` with a buffer" stays true rather than being made false, and
`grep -rn "Hold'em\|Texas\|\bLost\b" src tests tools` is the worklist for adopting each file.

The namespace is `bowling` (`bowling::eapp`, `bowling::platform`, `bowling::game`,
`bowling::runtime`, `bowling::gfx`…), the CMake options are `BOWLING_*`, the targets are
`bowling` and `bowling-headless`, the per-user data directory is `iPod The Sims Bowling/1500C`,
and the override is `BOWLING_DATA_DIR`. A `holdem`, `lost` or `minigolf` identifier anywhere in
this tree is a defect.

---

## Architecture

```
Sims Bowling/
  PLAN.md                this document
  README.md              layout, building, testing, contributing a decompiled function
  CMakeLists.txt         adds ../common; targets bowling (SDL3) and bowling-headless (tests)
  tools/
    survey.py            what is in the image — every number in this plan
    funcs.py             build gen/funcs.json — the function table the emitter works from
    emit.py              run the shared recompiler with this title's namespace and bindings
    progress.py          how much of the game is still recompiled rather than decompiled
    manifest.py          the game folder's file table -> src/gamedata/manifest_data.cpp
    port-from-holdem.py  copy what is not yet shared, and record it (reference/PORTED.md)
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
    diff.sh, frames.sh, record.sh, game-dir.sh
  analysis/              the reverse-engineering evidence for this title (README.md there)
  reference/             MANIFEST.md (the pinned emulator) and PORTED.md (the copies)
```

Everything about the guest machine — one flat reservation covering `0x11000000`–`0x40020000`,
`ld32`/`st32` through `memcpy`-based helpers, one `void f_1800xxxx(Cpu&)` per ARM function,
`goto` for intra-function branches, resolved jump tables as `switch`, `b .` as an assert trap,
flags computed eagerly by the rules ported from `arm7tdmi/arm.rs` — is exactly as the Mini Golf
plan describes it and is not re-litigated here.

**Ghidra is not a dependency.** The seed is the vectors, the live edges, and the image's stored
function pointers, and `analysis/survey.txt` plus the rehearsal show that reaches 2 400 functions
with one failure. Ghidra is for reading a function and recovering a structure when a
decompilation needs it — and this title, with its vtables, will need it more than the others did.

---

## Where Sims Bowling is none of the other three

Each of these was found by running the game through the emulator, or by reading the emulator's
own notes on it — `play.rs` and `lib.rs` between them mention this title some thirty times, and
those passages are the best spec of items 1–5 there is. Each is a measured fact that has to
become code in this tree or a parameter in the shared one — never a silent edit to a copied file.

**1. The frame-reason byte is half of a handshake, and the pump has to listen.** Every earlier
title's pump *writes* the reason byte at `ctx+0x00` and never reads anything back: Mini Golf
holds it at 5, Lost gives 0 once then 1, Hold'em seeds 0 for the init call and 1 after. Sims
Bowling's dispatcher at `0x18045740` reads the byte —

    0 -> drain the event list, then run the FULL application init (0x180052d4)
    1 -> the normal per-frame path (0x18045794)
    5 -> the suspend/resume path, which answers 6

— and **answers** in the byte at `ctx+0x100` (`0x1804578c` writes 1 when its init is done). The
firmware is meant to keep asking for init until that answer arrives, then ask for frames. With a
constant 0 the game initialises on every frame, never destroys what it built, and exhausts its
5.24 MB pool in 75 iterations; with a constant 1 it never initialises at all and sits idle. The
emulator's `--frame-reason=auto` is the handshake: *reason 0 while `ctx+0x100` reads 0, reason 1
thereafter* (`play.rs`, `reason_auto`), and the init vectors are called with the byte at its
default 5. The frame pump here does exactly that, in named constants with this paragraph beside
them. It is the fourth variant of the protocol, and it is the one that finally has two directions.

**2. Buttons are event-list nodes, at Lost's address, with the emulator's timing.** There is no
button-flags word in this binary (the `bic #0x60` signature does not occur) and no press-time
words. Presses arrive as twelve-byte nodes `{type +0, state +1, payload +4, next +8}` whose head
the pump publishes at `ctx+0x30`; the game's handler at `0x18007584` walks them and dispatches
types 0–5 through a jump table. The emulator posts a node with state 1 on the frame of the press,
overwrites it with state 2 at the top of the next frame, and nulls the head two frames after that
(`play.rs`, `event_hold`). Lost's recomp reads the same list but retires its node a frame earlier
and never posts state 2, for reasons that are Lost's; here the emulator's timing is reproduced
exactly, because it is what the recordings were made with, and what this game makes of state 2
is established later. `InputEvents #0` writes its event word through the *second* argument — this
title passes `r1 == r0 - 4` and reads it back from `[sp]` at `0x18007684`, which is how the
emulator learned that the word goes to `[r1]` and not `[r0+4]`.

**3. The files are a resource library, and the file layer has five rules to get right.** The
game's assets are inside one 20 MB `gameLib.rlb`. For every resource it opens the library
*again* — 66 handles by the main menu — with no buffer, seeks to the resource, reads it in one
chunk of up to a few hundred kilobytes into a 512 KB block, and closes the handle. Each step has a
rule the emulator had to learn on this title (`lib.rs`, the passages that name it):

* a bufferless open's result word `[obj+8]` is the **file size** — its open completion at
  `0x1803443c` stores it and `0x18009888` checks it against 4 before anything is read; with the
  handle there the check fails and every read is clamped to zero bytes;
* a **seek** (operation 5) that moved nothing must still return non-zero, or `0x1800432c` reads
  it as failure and re-asks every frame forever;
* a **read** must publish its byte count at `req+0x24` — the read completion at `0x180345c4`
  copies it to `[stream+0x120]` and the library advances by it; unwritten, the same chunk is
  fetched forever;
* a buffer **smaller than the file is a header probe and is filled**: every `.wav` is opened with
  a 44-byte buffer for its RIFF header, then again with none. Refusing the fill stalls the audio
  loader at handle 15 and the menu is never reached;
* **one operation, one completion.** The game reuses a single request object for back-to-back
  operations — `rserver.bin` and then `savefile.dat` through the same object at `0x1907d530` in
  one frame — and a queue that merged them stalled its state machine.

Opens *with* a buffer are loads (Lost's model, `--load-on-open`), and completions are delivered
between frames (Mini Golf's, the emulator's default). Hold'em's `async_file.cpp` already carries
both of those; the five rules above are what it is taught here, and the boot oracle is what says
each is right — `tests/diff.sh boot` will stop at the first one that is not.

**4. It draws with `glDrawElements`, which nothing on this side implements.** `OpenGLES #38` is
called 7 289 times in a 3 000-frame boot — four of the five draws in a menu frame are indexed,
one is `glDrawArrays`. No title has named it (the reversing table has it as `glDrawElements`;
the emulator implements it as `Stub::GlDrawElements` → `draw_elements(mode, count, type,
indices)`), and the shared rasteriser has no indexed path at all. It gains one, in `../common`,
as the emulator does it: the same attribute arrays, the vertices fetched through an index array
of the given type. The rest of the driver is Lost's family — render server `#152`/`#153`/`#159`/
`#164` (the `rserver.bin` image is Lost's, byte for byte), the fixed-point matrix path `#149`
(12 193 calls), the *vector* constant colour `#148` (Hold'em's, where Lost used the scalar
`#147`) — plus paletted textures through `#19` in `PALETTE8_RGBA8` and `PALETTE8_R5_G6_B5`,
which the shared decoder already reads (it learned the entry-size rule on this very title's
"diagonal-streak garbage"). The bowling scene sets the constant colour to `(0.03, 0.01, 0.03)`,
which is the "screen goes black" the emulator once had; the shared rule that the register tints
only alpha-only textures already sidesteps that, and the picture oracle is what confirms it.

**5. Sound is loose `.wav` files the game reads itself, and one ordinal is a repeat count.**
Thirty-one `.wav` at the folder root, each opened through `AsyncFileIO` by name (the 44-byte probe
of difference 3, then the whole file), decoded by the game and handed to `Audio #7` as a buffer —
eleven of them at boot. `Audio #16` is new: the emulator's `SfxRepeat { handle, count }`, a
per-effect loop count; `Audio #47` is answered zero and unexplained. Music is `a.m4a`, registered
and set repeating through `#40`/`#43`/`#48`, and the shared decoder plays it. Four voices.

**6. The save is `savefile.dat` at the root, and the reference folder already has one.** The game
creates it empty during boot and writes 18 784 bytes into it later. The owner's `Games_RO/1500C`
contains one dated 2026-08-20 — an earlier emulator session's, filled with `a5` — so it is *not*
a shipped file: `tools/manifest.py` ignores it by name, and `tests/game-dir.sh` strips it from
every private copy so that every recorded case is a first boot. Whether the game accepts a save
it finds, and what it writes, is a decompilation question and is listed under Not today.

**7. It is the biggest program of the four, and its interesting part is a file format.** A
bowling game is a physics loop, an aiming UI, a scoring engine, and a "Sims Life" career mode on
top; its assets — meshes, sprites, fonts, the seven languages' strings — are records in
`gameLib.rlb`, a format nobody has read. The 786 stored function pointers mean the engine is
dispatched through tables the static walk cannot see through, which is exactly where a function
reached only by a *computed* address can hide from the seed (`analysis/extra-entries.txt` is
where those go when `call_indirect` finds one). The boot and menu oracles prove the
recompilation; the first oracle that *bowls* is the one that matters, and the path to it — two
Selects to the Controls screen so far — is walked on day one.

**8. One control-flow idiom the recompiler does not model.** armcc's 64-bit unsigned divide at
`0x18001cd8` steps into an unrolled loop with

    0x18001e20  and r0, r2, #7 / eor r0, r0, #7 / adds r0, r0, r0, lsl #1
    0x18001e2c  add pc, pc, r0, lsl #2

— a computed jump whose bound comes from the `and #7` mask rather than from a `cmp`/`addls`
guard, so `cfg.py`'s `_resolve_jump_table` refuses it. It is the only unwalkable instruction in
the image (`analysis/survey.txt`, and the seed walk in the progress log). The fix is shared:
`_resolve_jump_table` learns to bound an *unconditional* `add pc, pc, rN, lsl #2` by walking back
through the few instructions that computed `rN` — a mask sets the range, an `eor` with the same
mask keeps it, an `add rN, rN, rN, lsl #k` scales it — and emits the case table from that bound.
Every case target is a real instruction of the function, so the unreachable cases (the index
here is always a multiple of three) cost nothing but a label. Hand-writing the routine in
`arm_runtime.json` is the fallback, and it is a worse answer: the same idiom is in every armcc
runtime and the next title would meet it again.

---

## Schedule

Each block ends in something runnable *and* reviewed against the quality rules. If a block
overruns, the fallback is named. Cutting quality is never the fallback.

### Block 0 — scaffold, provenance, and the pinned emulator (30 min)

* `README.md`, `.clang-format`, `.gitignore` (`gen/`, `build*/`), `pyproject.toml`.
* `tools/port-from-holdem.py` from Hold'em's script; run it. `reference/PORTED.md` written.
* `reference/MANIFEST.md` and `tools/oracle-emulator/`: `tools/eapp-loader` and `tools/arm7tdmi`
  at commit `96bfe90`, plus the detached `Cargo.toml`. Record the commit **and** the SHA-256 of
  the sources, as Hold'em did and for the same reason. The copy will be byte-identical to
  Hold'em's pin; that is timing, not a dependency.
* `tools/manifest.py "…/Games_RO/1500C"` → `src/gamedata/manifest_data.cpp`, with `savefile.dat`
  at the root ignored (difference 6) — 63 files.

**Exit:** `cmake -B build` configures; `python3 tools/survey.py` reproduces `analysis/survey.txt`.

### Block 1 — the recompiler idiom, then emit (45 min, the first block that touches `../common`)

* `_resolve_jump_table` in `../common/tools/recomp/cfg.py` bounds an unconditional
  `add pc, pc, rN, lsl #2` from the mask that computed `rN` (difference 8). **Verify before use
  (rule 10):** Mini Golf `tests/check-recomp.sh` byte-identical, Lost and Hold'em re-emitted with
  the same function and instruction counts as before.
* `tools/funcs.py` with this title's paths (`Games_RO/1500C/Executables/…`, `analysis/coverage/`)
  and `tools/emit.py` with `Generator(namespace="bowling")`. Rehearsed: 2 144 seed entries,
  2 400 walked, 71 364 instructions, one failure. The number to reproduce is 2 400 with none.

**Exit:** `gen/` compiles under `bowling_generated`. **Fallback:** the routine hand-written in
`arm_runtime.json` over host `uint64_t` arithmetic, and the idiom left on the shared core's list.

### Block 2 — runtime and the frame pump (1 h)

* `src/runtime/` from Hold'em; `memory.cpp` region sizes to be re-measured from the longest
  recorded session before the day ends (start with Hold'em's, which are Lost's).
* `main.cpp`: the handshake of difference 1 — init vectors at the default 5, then *ask for init
  until `ctx+0x100` answers, then frames* — and the event list of difference 2 with the emulator's
  three-frame node timing, both as named constants with the dispatch addresses cited.
  `TERMINATE_VECTOR_SLOT = 1`, as in both previous titles (the emulator measured it on this one:
  `0x18045504` ends in `__cxa_finalize` and running it at start-up nulls the resource queue).
* Every ordinal logging and returning 0.

**Checkpoint A:** `build/bowling-headless … --frames=2` runs the start-up vector and the first
frames and its log begins as `analysis/coverage/boot-summary.txt`'s run began: ten `miscTBD #0`
allocations of `0x7ff80` bytes.

### Block 3 — the oracle (30 min)

* `tests/record.sh` with this title's flags, which are part of every case and not a matter of
  taste: `--load-on-open --frame-reason=auto --ctx-seed=5 --allow-creates --fixed-clock --fps=0`.
  (`auto` and 5 are the emulator's defaults for a binary named `SimsBowling*`; they are spelled
  out so the case does not depend on a defaults table in another tree.)
* Record `boot` (`600: quit` — every file operation is done by frame 327 and the menu is drawn by
  500), `menu` (a screenshot at the menu, `1000: quit`) and `controls` (two Selects, a screenshot
  of the Controls screen). Every script short and ending in `quit`; `tests/game-dir.sh` gives
  each run a fresh, save-less copy of the folder.
* `imports.json`: the union of Hold'em's and Lost's tables, plus `OpenGLES #38`, `Audio #16` and
  `Audio #47` from `lib.rs` and the reversing table, with argument counts.

**Exit:** `tests/diff.sh boot` runs and reports its first divergence with a line number.

### Block 4 — libeapp, in boot order (3 h)

In the order the boot log calls them, re-running `tests/diff.sh boot` after each group:
miscTBD / Settings / InputEvents → AsyncFileIO (difference 3 — expect the real fight here: five
rules, and the oracle names the first one wrong by line) → Metadata (`#62`, `#134`) → Audio
(`#16`, `#47`, the buffers of difference 5) → OpenGLES (the shared rasteriser plus
`draw_elements`, difference 4 — **in `../common`, verified in the other trees first**). Then
`tests/diff.sh menu` and `controls`.

Two shared-rasteriser rules need this title's answer and only the picture can give it: whether
the game re-points every attribute before every draw (`gfx::set_attributes_repointed_per_draw`;
the boot log's `#137`/`#40` pattern — nine of each a frame at the menu for five draws — says it
does, like Lost) and whether the constant-colour rule holds in the bowling scene. Read the log
first; confirm with `frames.sh`.

**Checkpoint B:** `diff.sh boot`, `menu` and `controls` pass — the recomp is *proven* before a
pixel is visible. **Fallback:** if AsyncFileIO is still diverging at the two-hour mark, `boot`
is the deliverable and the rest moves to tomorrow with the divergence written up.

### Block 5 — SDL3 and the picture (1 h)

* `src/platform/` from Hold'em; `sdl3_platform.cpp` with the title's name and bindings; the
  shared music decoder through `IPOD_CORE_SDL3_SOURCES`.
* `tests/frames.sh` from Hold'em, with the `shot` frames of the `menu` and `controls` scripts
  compared against the emulator's PNGs at a threshold. `draw_elements` has no oracle but this one.

**Checkpoint C:** the Main Menu on screen, natively, and the wheel moving the highlight.

### Block 6 — the first oracle that bowls, then decompilation (the rest)

* Walk the path past the Controls screen in the emulator — *Press Center to continue*, whatever
  follows, to a ball rolled — and record it as `first-frame.script`. If the emulator cannot get
  there, that is the first finding of the progress log and an emulator-tree change in its own
  commit, followed by a re-pin.
* `tests/vs-recomp.sh` and `tests/check-recomp.sh` ported from Mini Golf on day one, not later.
* Then the swap loop, each swap diffed: the string helpers; the tick dispatcher at `0x18045740`
  and the event dispatcher at `0x18007584` (both small, both tables); the resource library —
  its open/seek/read state machine at `0x1803443c`/`0x180345c4` and the `.rlb` directory it
  reads, which is the format the whole game is stored in; and only then the engine. Top-down
  from the vectors, because that is the order in which the state structures are forced into the
  open.

---

## Risks, in the order they will bite

1. **`draw_elements` is new shared code with one oracle.** Lost and Hold'em never call `#38`, so
   nothing but this title's picture can say it is right, and a wrong index type or base reads as
   garbage triangles that the call-log oracle passes by construction. Mitigation: the emulator's
   `draw_elements` is the spec, read before writing; `frames.sh menu` in block 5 is the test; the
   other titles' pictures are the regression check that the shared file still draws what it did.
2. **The five file rules of difference 3 interact.** Each one was found by the emulator after the
   previous one was in, and a file layer that gets four of them right stalls in a way that looks
   like the fifth. Mitigation: the boot log has every operation in order and the emulator's
   `--file-ops` prints its side; the divergence line is the rule.
3. **The seed misses a computed target.** 786 stored pointers is a lot of vtable; a slot that is
   *built* rather than stored is invisible to `funcs.py` until `call_indirect` stops on it.
   Mitigation: every recorded session's edge dump goes into the seed; the first fatal names the
   address and it goes into `extra-entries.txt`.
4. **The emulator may not bowl.** Two Selects reach the Controls screen; nothing yet shows a ball
   on a lane, and the emulator's own notes on the bowling scene (the black tint, the palette
   streaks) are about pictures it drew *once*, in someone's hand-driven session. Mitigation:
   block 6 tries the path before decompiling anything, so the answer is known early.
5. **Compile time.** 71 364 instructions is ~120 000 lines of generated C++, a third more than
   Lost's, and the generated library is the slow part of every build. Watch it in block 1; more
   translation units is the lever.
6. **The recompiler fix is wrong for a table it has never seen.** A mask-bounded jump with a
   shape this walk-back does not recognise would fail loudly, which is the safe direction; one it
   recognises *wrongly* would emit too few cases and trap on a real input. Mitigation: the bound
   is only accepted when every instruction between the mask and the jump is one the walk
   understands; anything else is the old error.

---

## Not today, written down so it is not forgotten

* **The frame-reason protocol as a parameter.** Four titles, four protocols: a constant (Mini
  Golf), a first-frame value (Lost), a seeded init (Hold'em), a handshake (this one). The shape is
  now visible — *what the init call sees, what each frame sees, and whether the answer byte is
  read* — and it belongs in `ipod` with each title supplying its instance.
* **The event-list pump, shared.** Lost's and this title's `ClickWheel` differ only in the node's
  retire timing and the release state. That is two parameters, not two classes.
* **A shared test harness.** `diff.py`, `frames.py`, `game-dir.sh` are copied for the fourth
  time. They belong in `../common/tests/` with the image path and flags as arguments.
* **`gameLib.rlb`.** A tool that lists and extracts its records would make every asset question
  answerable outside the game. It starts from the open/seek/read log, which already has every
  offset and length the game asked for.
* **The save.** What the game does with a `savefile.dat` it finds; what the `a5` fill means; the
  Hold'em question of what a *successful* write is answered with.
* **Localisation.** `Resources/<lang>/` for seven languages, and the `Settings #0` answer that
  selects one.
* **The Switch.** Mini Golf has a platform; nothing here should make a second one harder.

---

## Progress log

Each entry says what was established, not what was attempted, and ends with what was **not**
verified.

### 2026-08-27 — before the first line: what the image and the emulator say

Done before this tree existed, with the Hold'em tools and the Hold'em-pinned emulator
(byte-identical to `tools/eapp-loader` at `96bfe90`), to size the job and find the differences
above. Everything is in `analysis/`.

* `survey.py` against the image: 501 functions and 18 085 instructions from the vectors, 0
  unwalkable; seven frameworks, 429 thunks in Lost's exact layout. `eapp-inspect`: build
  `3002478`, header `0x10001000`, the block-count warning.
* `funcs.py` with two probes' edges: 2 144 seed entries — 1 715 game, 429 thunks, 786 stored
  function pointers. A walk of every seed entry to a fixpoint: **2 400 functions, 71 364
  instructions, 1 failure** — `0x18001cd8`, the mask-bounded computed jump of difference 8. The
  Hold'em emitter, unmodified, stops there.
* Emulator sessions, all with `--load-on-open --allow-creates --fixed-clock --fps=0` and the
  title defaults (`--frame-reason=auto`, seed 5, asynchronous files, event-list buttons):
  * a 3 000-frame silent boot: 214 434 framework calls, 9 193 quads, 1 clear, 59 ordinals, 5 972
    branch edges; `a.m4a` playing; `savefile.dat` created empty. File activity ends at frame
    327 after 642 operations and 66 handles; a screenshot at 500 is the Main Menu with correct
    artwork. The menu's own clock reads the fixed clock's midnight.
  * an 80 000-frame run: the same 5 972 edges, the menu animating and nothing else — the
    "72 000 frames to the menu" in `play.rs` is a wall-clock figure; under the fixed clock the
    loader is done in 327.
  * input probes at the menu (2 000 frames each): one Select adds ~100 edges and moves nothing
    visible; **two Selects reach the Controls screen** ("Adjust launch position / Confirm
    position, power, aim and spin / Press Center to continue" — `coverage/controls-frame1400.png`);
    the wheel, Play, Next and Menu each change the picture (the highlight, presumably — not
    inspected) and add ~80–130 edges.
* Three reached ordinals are unnamed in every title (`ordinals.txt`): `OpenGLES #38` (7 289
  calls; the reversing table says `glDrawElements`, the emulator implements it), `Audio #16`
  (the emulator's `SfxRepeat`), `Audio #47` (answered 0).
* `rserver.bin` is byte-identical to the one in Lost's folder (`cmp`).

**Not verified:** anything past the Controls screen (Risk 4). Which button the game reads state 2
as — the emulator posts it and Lost's dispatcher read it as "held"; this title's `0x18007584`
has not been read. Region high-water marks. Whether the IRAM at `0x40000000` is used (the
emulator maps it for Sudoku's sake and names this title among the crashes that mapping fixed).
What `Audio #47` is. What the ~100 edges a single Select adds are.

### 2026-08-27/28 — blocks 0 to 5: the recompilation runs, and all three oracles are identical

Done the same night, in the order of the schedule. Established:

* **Block 0.** `tools/port-from-holdem.py` copied 70 files from Hold'em with identifier
  rewrites only (`holdem`→`bowling`, `HOLDEM_`→`BOWLING_`, the two data-directory strings);
  prose that names Hold'em or Lost was left true rather than rewritten false, and
  `grep -rn "Hold'em\|Texas\|\bLost\b" src tests tools` is the worklist for adopting each
  file (ten files carry some). The emulator is pinned at `96bfe90` with file hashes as well as
  the commit, each checked against `git show` (`reference/MANIFEST.md`). The manifest has 63
  files; `tools/manifest.py` ignores `savefile.dat` by name, and `tests/game-dir.sh` strips it
  from every private copy.
* **Block 1.** The recompiler fix of difference 8 turned out to be two: the mask-bounded
  `add pc, pc, r0, lsl #2` at 0x18001e2c, and a second table in the same divide at 0x18001f5c
  with `lsl #3` — two instructions per case, which no table the recompiler had met used.
  `JumpTable` gained a stride and `_resolve_jump_table` a mask walk (`_mask_bound`: `and`,
  `eor`, `rsb`, `add rN, rN, rN, lsl #k`, `add #k`, `mov lsl #k`; anything else is the old
  error). Verified before use: Lost and Hold'em re-emit byte-identical; Mini Golf's
  `check-recomp.sh` unchanged (5 of 6 exact, `next-hole` as it was). Then `funcs.py` → 2 167
  seed entries (767 stored pointers, 1 012 live targets from two edge dumps); `emit.py` → 2 401
  functions, 71 629 instructions, 0 unwalkable, 140 000 lines of C++ in 0.7 s; the tree builds in
  ten seconds with eight jobs. Numbers elsewhere in this document that say 2 400 or 71 364 are
  the rehearsal's seed.
* **Block 2.** `main.cpp` rewritten around differences 1 and 2: the init vectors see reason 5,
  every frame is asked for init until `ctx+0x100` reads non-zero and for a frame after; buttons
  are event-list nodes with the emulator's timing. The first divergence the oracle found was a
  byte read at **0x01400010** — 250 MB below anything mapped. The emulator answers such a read
  with zero and goes on (`note_unmapped`); the recomp's `guest_pointer` was fatal there. It now
  answers as the emulator does (zero read, dropped write, counted and reported once at exit,
  `BOWLING_TRACE_UNMAPPED=1` for a backtrace), which is this title's `memory.cpp`. *Why* the game
  reads there — its UI walker `f_180447a4` had a fixed-point 320.0 where a node pointer belongs —
  turned out to be the next finding, not a fact about the game.
* **Block 3.** `tests/record.sh` spells out `--load-on-open --frame-reason=auto --ctx-seed=5
  --allow-creates --fixed-clock --fps=0`. Three cases: `boot` (600 frames, 32 361 calls), `menu`
  (1 000 frames, a screenshot at 900, 62 777 calls), `controls` (two Selects, a screenshot at
  1 400, 76 812 calls). `imports.json` gained `OpenGLES #38` `gl_draw_elements`, `Audio #16`
  `audio_sfx_set_repeat` and `Audio #47` (answered zero).
* **Block 4.** Four divergences, each found by the oracle:
  1. **A second recompiler bug, the same class as Hold'em's.** The walker `f_18044944` prepares
     a return address with `add lr, pc, #0x2c` at 0x18044a84 and jumps twelve instructions later
     (`bx ip` at 0x18044ab4). With `LINK_SETUP_WINDOW` at eight the jump read as a tail call, the
     function returned a frame early, and its caller's loop read a 16.16 width as a node pointer
     — the 0x01400010 read above. Found with `--trace-from=2 --trace-entry=<every function>`
     and the emulator's `--watch-pc` on the same functions: the register states agreed at every
     entry until the one the recomp never made. The window is 16, with the same equality guard;
     Lost and Hold'em re-emit byte-identical.
  2. **The seek rule and the byte-count rule (difference 3).** Hold'em's `async_perform`
     answered a seek with 0 and published no byte count at `req+0x24`. Both are in; the header
     probe and the size-on-bufferless-open were already Hold'em's behaviour, and the emulator's
     `partial_load_max` default is unbounded, so no bound was needed.
  3. **The save.** `is_save_name` is `savefile.dat`, so the boot's write-mode open with an
     18 784-byte buffer reads the (empty) store rather than writing the buffer to disk, which is
     what the emulator does with its write-on-open path off — and `writes_refused` now covers an
     open-time write too. The unit test `save_files_test.cpp` is this title's save.
  4. **The event node's timing was a frame off.** A first reading of play.rs put the head's
     clearing two frames after the release; its `event_hold` decrement in fact runs *after* the
     frame's presses, every frame, so a press at N is visible at N, the release at N+1, and the
     head is null from N+2. With the release still published at N+2 the recomp reacted to a
     Select a frame before the emulator did. `ClickWheel::settle` is that decrement.
  **Checkpoint B:** `diff.sh boot`, `menu` and `controls` are identical, semantic and exact —
  the exact comparison with `miscTBD #12` dropped from both logs (`tests/exact-allow.txt`): the
  emulator answers the host clock with the real date and time of the recording, and this game
  asks once a frame into a stack slot the next frame's call logs.
* **Block 5.** `glDrawElements` in the shared rasteriser (difference 4), as the emulator does it.
  `tests/frames.sh menu`: 0.35% of pixels over the threshold on the first try, then
  `frames.sh controls` **pixel-identical** once the event timing was right; both pictures are
  the emulator's. The SDL3 `bowling` target builds. **Checkpoint C.** `ctest`: **16/16** — eight
  unit tests, three cases each in semantic and exact form, two pictures.

**Not verified:** anything past the Controls screen (Risk 4, unchanged). What the game reads
state 2 as (0x18007584 not yet read; the emulator's timing is reproduced, not understood).
Whether the game re-points attributes per draw (the picture oracle runs with the conservative
reading; the native picture has not been compared with it on). Region high-water marks (Lost's
sizes, unmeasured here). How the game writes its save, and what a real run does with the one it
wrote. Whether the wider link window mis-reads any `bx` that really is a tail jump — the
equality guard says it cannot, and every oracle in four titles is green, but that is the
measure. `Audio #47`.

### 2026-08-28 — playing it: two rendering faults, a third recompiler bug, and a lost vtable slot

Reported from the SDL build: changing the render scale left the screen magenta; the menu's
selection bar was a smear; the first throw died with `indirect call to 0`; touching the volume
slider died with `indirect call to 0x18029d4c`. Established, each from the game's own code or
the oracle:

* **The bar.** The flat rectangle at 0x18042450 points attribute 0 only, selects pipeline 1,
  sets the constant colour from a stack vector (`#148`) and draws `QUADS 0..4`; the sprite
  batch flush at 0x180424a0 re-points 0 and 1 (and 2 when coloured) before every
  `glDrawElements`. So this title re-points per draw, like Lost, and
  `gfx::set_attributes_repointed_per_draw(true)` is now claimed with those two routines cited.
  Under the conservative reading the bar sampled the previous batch's texture through attribute
  1's stale pointer; the emulator reads it the same way, which is why the picture oracle
  (`--emulator-graphics` keeps the emulator's reading) never saw it. Natively the bar is a solid
  fill in the constant colour, with white text on it.
* **The magenta.** The game clears the screen once at boot (`clears=1` in every recording) and
  each frame redraws only what animates — five draws at the menu. The shared rasteriser painted
  the buffer magenta on a resize, and nothing ever repainted the rest. `set_render_scale` now
  resamples the old picture into the new size (nearest); the parts the game redraws sharpen as
  it does. Shared-core change, verified in the other titles.
* **The throw.** Reproduced with `tests/scripts/first-frame.script` (Bowl Now!, Controls, Set
  position with the wheel, Aim, Set power, the ball rolled — the emulator knocks down seven).
  The recomp died in frame 3401 with no framework call between the last matching one and the
  fault. `--trace-entry` of every emitted function (the 663 the emitter discovers on its own are
  *not* in `funcs.json`, and the first trace missed them) showed `f_18028de4` returning to its
  caller with SP 0x90 bytes low: `add lr, pc, #0x5c` at 0x18028f68 prepares the return address
  and the `bx r2` comes **twenty-four** instructions later, past the sixteen-instruction window
  — the same bug as Hold'em's and this title's earlier one, a third time. `_follows_link_setup`
  now scans back to the nearest instruction that writes lr, with the equality test as the only
  guard (`LINK_SETUP_WINDOW` is a 256-instruction backstop). Lost and Hold'em re-emit
  byte-identical (their `calltable.cpp` differs only by the richer fatal below); Mini Golf's
  `check-recomp.sh` unchanged; this title gained 283 instructions of epilogue. `first-frame` is
  recorded (720 175 calls) and identical, semantic and exact.
* **The volume slider.** `0x18029d4c` is a vtable slot stored in plain sight at 0x180723d8, and
  `tools/funcs.py`'s stored-pointer walk threw it away: the candidate's body tail-branches into
  an import thunk (`b 0x180007d0`, armcc returning `Audio #53`'s result unchanged), the walk was
  not told the thunks are entries, and it walked into the thunk's `ldr pc, [pc, #N]`. The walk
  now knows them; the seed gained exactly that function. A `volume` case (the wheel to the
  Volume row, `next`/`prev`) is recorded and identical.
* **The picture oracle and a moving meter.** `frames.sh first-frame` failed on the shots after
  the first ball at ~2% of pixels, all in the power meter. Every draw matched — geometry,
  texture, pipeline, tint — and so did every decoder; the difference was *when*: the emulator
  handles a script's `shot` before it calls the frame, so a shot of frame N is frame N-1's
  picture, and the recomp took it after. The earlier titles' shots were of static screens. The
  recomp's screenshot now happens where the emulator's does.
  With it, `ctest` is **22/22**: eight unit tests, five cases each semantic and exact, four
  pictures — and Lost's and Hold'em's picture tests, re-run alone, are green too (an earlier
  "failure" of theirs was this session's emulator probes writing to the shared `/tmp` shot path
  while they ran; the test says so when it happens).
* Two smaller things on the way: the generated `call_indirect`'s fatal now names the guest
  return address, SP and r0–r5, which is what turned a day of tracing into one line; and
  `tests/exact-allow.txt` is the mechanism that lets `--exact` pass on a title whose recordings
  carry the real wall clock.

* **The arrows.** The SDL build's arrow keys were the four wheel *sides* — Lost's walking
  gesture, which nothing in this game reads. They are now the menus' controls: Up/Down turn the
  wheel a row (eight detents, measured: eight move the highlight exactly one row, six move it
  nothing; a positive turn goes down the list), Left/Right are the two wheel buttons. `,`/`.` and
  `[`/`]` stay as second bindings; Settings > Input changes any of it.

* **Save & Exit froze — and the buttons were upside down.** Reported as *Menu > Options >
  Confirm exit > freeze*. Reproduced as the Pause menu's Save & Exit: after it the game makes no
  framework call but the clock, the poll and the swap, for as long as it is left. The frame path
  (0x18045794–0x18045930) says why: when its tick returns 0 (0x18045884) it writes **5** to
  `ctx+0x100` — "put me away" — and records the reason at `0x18073854+7` (1 its own exit, 2
  idle, 3 Menu held 4 s, 4 Next held 2 s); a call with reason 5 runs a shutdown step
  (0x18005234) until done and answers **6**. Lost's protocol exactly, and the pump was deaf to it
  (as the emulator is). A real run now answers 5 with 5 and ends on 6, idle excepted; under
  `--emulator-firmware` nothing is honoured, as in the recordings.
  Reading the dispatcher (0x18007570) for the reason byte found the second thing: node state 2
  sets the game's "Menu is down" flag and state 1 clears it — **state 2 is the button going
  down, 1 is it coming up**, the reverse of the emulator's names and of the order it posts
  them. That is why every menu in the recordings needs *two* Selects (the first's "up" fires
  nothing, its "down" arms; the second's "up" fires) and why a Menu tap there starts the
  four-second countdown. A real run now posts down on the press and up when the key is
  released (`FrameInput::buttons_down`, a level the platform reads from the keyboard), which is
  also what the name screen's "Hold Center to validate" needs. The recordings keep the
  emulator's order and timing to the frame; `ctest` is unchanged at 22/22.
  Natively, one Select now walks Bowl Now! → *Player Name* (a screen the recordings never show:
  the emulator's reversed states skip it) → Controls → the lane; a Menu tap in play opens Pause;
  Save & Exit writes `savefile.dat` (18 784 bytes) and ends the program.

**Not verified:** what the game reads back from that save on the next boot (its boot open is
a mode-1 open with an 18 784-byte buffer, answered here by loading the store into it). What
Abandon Game and a held Menu do natively beyond raising the same request. The native run of `first-frame` reaches a different screen by frame 3900 than
the `--emulator-firmware` run does (an overhead view of the pins rather than the meter): the
two differ in the wall clock and in whether the save write succeeds, and which of those moves
the game has not been read. What the game does with a succeeded `savefile.dat` write. Whether
`wheel` alone ever moves the menu highlight by less than a row (the recordings use long turns).
