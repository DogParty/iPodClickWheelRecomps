# The Vortex recomp — plan of attack

**Goal:** `vortex_1_1_2563290.bin` — Apple's 2006 iPod click-wheel game *Vortex*, a Breakout
played around a circle, from the first wave of iPod games — running natively on the ARM Mac as a
statically recompiled C++ program, drawing into an SDL3 window, driven by the keyboard, and
*proven* equivalent to the emulator by diffing the sequence of framework calls both make on the
same scripted input. Then hand-decompiled, function by function, into readable modern C++, each
swap proven against the recompiled version.

Everything lives in this folder. The emulator tree is read, snapshotted, and otherwise left alone.

This is the **fifth** title to go through this process, and the third to start with
`recomps/common/` in place. *Mini Golf* is finished — every one of its 333 functions is
hand-decompiled. *Lost*, *Texas Hold'em* and *The Sims Bowling* run as pure recompilations with
their oracles green. `../Mini Golf/PLAN.md`, `../Lost/PLAN.md`, `../HoldEm/PLAN.md` and
`../Sims Bowling/PLAN.md` are the record of how the first four were done, and
`../common/README.md` is the record of what they turned out to have in common. **Read the code
quality section of the Mini Golf plan; it binds this project unchanged.** What is written below
is the delta: what Vortex is, what it takes from the shared core and what it still has to copy,
and — the part that actually matters — the places where Vortex is none of the other four.

---

## What Vortex is

Measured, not guessed. Every number here is answered by a file in `analysis/` (its `README.md`
says how each was made). They come from `tools/eapp-inspect` in the emulator tree, from a static
control-flow walk of the image with the shared recompiler (`analysis/survey.txt`), from a
rehearsal of the Sims Bowling tools against this image, and from three scripted sessions through
the emulator at commit `96bfe90`.

| | |
|---|---|
| image | `vortex_1_1_2563290.bin`, 414 600 bytes, loads flat at `0x18000000`, ends `0x18065388`. Header version `0x10001000`; `eapp-inspect` warns, as for Hold'em and Sims Bowling, that its block-count word says 5 while seven framework blocks are present |
| game data | `20 iPod games/Games_RO/12345/` — 96 shipped files, ~30 MB. 62 `.ipd` textures (31 `Backgrounds/` at 320×240 and their 256×256 `_Door1` halves, 15 `UI3/` help and bonus sheets in eleven languages, fonts, bats, balls), one **texture pack** `tex` (2 164 044 bytes, 46 records), one **sound bank** `media/sfx` (2 563 812 bytes, 47 effects), three music tracks `a.m4a`/`b.m4a`/`c.m4a`, `gamedata/levels/levels` (8 640 bytes), `fonts/*.txt` glyph tables with their `.ipd` atlases, eleven `Localization/<lang>.lproj/text.strings`, `Resources/<lang>/` (a `Description.xml` and a guide `.jpg` each), `Vortex.raw.lcd5`. Plus **three files the game writes** (below) |
| entry vectors | 3: `0x18021798` start-up, `0x18021794` terminate (header slot 1), `0x180217f4` per-frame |
| functions | **543** reachable by walking from the vectors; **753** once the probes' live edges, the image's stored function pointers and the code addresses its instructions form from the program counter are added and the emitter walks to a fixpoint (236 of those it found on its own) |
| instructions | **39 247** ARM instructions recompiled — 71 402 lines of generated C++ in 34 files  emitted in half a second  **0 unwalkable** |
| import thunks | **429** across seven frameworks |
| frameworks | OpenGLES 179 · Metadata 152 · Audio 61 · AsyncFileIO 17 · miscTBD 15 · Settings 3 · InputEvents 2 — **Lost's and Sims Bowling's layout to the byte** (OpenGLES first at `0x18000064`, Settings last), not Hold'em's eight |
| ordinals a silent boot reaches | 58 in 3 000 frames; 63 by the end of the first level. Three of them no title has named |
| code properties | ARM state only; the walk fails on nothing |
| emulator behaviour | loads its 45 background and help textures in the first ~120 frames, is on the **title screen** — the *VORTEX* logo over a ring of bricks, a ball orbiting, *Press Select* — by frame 300 with correct artwork (`analysis/coverage/screens-probe-shot-00300.png`), `a.m4a` repeating; one Select reaches **ENTER NAME** (a wheel of letters, `select-probe-shot-03100.png`) and a second Select types the highlighted letter; ~176 framework calls a frame at the title, 4.2 quads |

For scale: Mini Golf was 333 functions and 23 268 instructions, Lost 789 and 65 423, Hold'em 951
and 53 499, Sims Bowling 2 401 and 71 629. **Vortex is the second-smallest program of the five**
— twice Mini Golf, the only finished one — and, like Mini Golf, a game whose interesting part is
a physics loop and not a file format.

Two facts about its lineage matter for everything below. Its build number (`2563290`) is *one*
below Hold'em's (`2563291`) and six below Mini Golf's (`2563296`): the same SDK, the same week,
Apple's own studio. The binary shows it where it counts — **the button-flags word** (the
`bic #0x60` signature the emulator's `find_flags_word` derives an address from, and which no
2007 title has), the **asynchronous open-as-load file model**, and the **float matrix path**
(`OpenGLES #125`, 23 511 calls; Lost and Sims Bowling use the fixed-point `#149`). So for *how
the game talks to the firmware*, Hold'em's `main.cpp` is the closest relative. Yet its import
table is the 429-thunk seven-framework layout of the 2007 titles, and for *the state of the
code* Sims Bowling's tree is the newest copy of the shared layers — the forwarding headers into
`../common`, the unmapped-access model, the widest tracing aids — and is what this tree copies
from.

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
provenance line in `reference/PORTED.md`), Hold'em's (**9.** nothing is copied that the shared
core can carry; what differs by a measured fact becomes a parameter) and Sims Bowling's (**10.** a
shared-core change is verified in every title before it is used here).

One addition this project makes, because it is the first to be started while another title's
suite was running on the same machine:

**11. A probe or a recording is only as clean as the machine it was made on, and the harness
checks.** The emulator has no headless mode — `play` always opens a window, a click on that
window is a Select, and its screenshots go to one fixed path (`/tmp/ipod-shot-NN.png`) that every
title's picture oracle shares. On this project's first day a silent boot picked up ten Selects
from stray clicks, and two of six "Vortex" screenshots turned out to be The Sims Bowling's main
menu, captured from a concurrent run. So: `tests/record.sh` refuses a recording whose emulator
output shows a window-sourced press; `tests/frames.sh` and `tools/probe.sh` copy a screenshot on
the line that announces it and verify the frame number they were promised; and a probe that
shows either symptom is re-run, not interpreted.

---

## What is inherited, from where, and what that costs

The layers of a title, and where each one comes from now:

| layer | state today | what happens here |
|---|---|---|
| `tools/recomp/` — the ARM→C++ recompiler | **shared** (`../common/tools/recomp`) | imported; `Generator(namespace="vortex")`; no change needed — the walk reaches every function |
| `src/runtime/` | `cpu.h`, `runtime.{h,cpp}`, `memory.h`, `fatal` shared; `eapp_image`, `memory.cpp` copied; `main.cpp` per title | copied from Sims Bowling; `main.cpp`'s input and reason model taken from Hold'em by hand (differences 1 and 2), with provenance in the comment |
| `src/framework/` — the typed platform interfaces | `types.h`, `graphics.h` shared; `controls`, `storage`, `device`, `audio`, `music_library` copied (they declare what each title's `libeapp` implements — `../common/README.md` says why they stay) | copied; `device.h` gains `reallocate` (difference 4) |
| `src/libeapp/` — the iPod frameworks | `gles.cpp`, `heap` shared; `misc`, `host_state`, `arm_abi`, `framework_call`, `input`, `metadata` identical copies (0 lines differ between Hold'em and Sims Bowling after namespace normalisation); `async_file`, `audio` carry each title's model | copied from Sims Bowling; `misc.cpp` gains realloc and the clock model of difference 5; the shared `heap` gains `realloc` (difference 4, **in `../common`, verified in the other trees first**) |
| `src/platform/` | `save_store`, `text_entry`, `music_decoder` shared; the rest copied (`sdl3_platform.cpp` is identical in Hold'em and Sims Bowling) | copied; this title's name, bindings, and the 30 fps default (difference 5) |
| `src/gamedata/` | `zip` shared; `install`, `manifest` copied | copied; `manifest_data.cpp` regenerated from `12345`, with the three written files ignored (difference 3) |
| `tests/` harness | copied in every tree (`diff.py` and `frames.py` are byte-identical in Hold'em and Sims Bowling) | copied from Sims Bowling, with rule 11 built in; sharing it is block 7 |
| `src/game/` | no | nothing to inherit; this is the work |

**How a title reaches the shared core** is settled: `../common` is added as a subdirectory and
`ipod_core` is linked through this title's own `vortex_common` interface target, so the shared
sources are compiled under this title's warning rules. Shared code is namespace `ipod`, included
as `ipod/…`; forwarding headers at the old paths pull the names into `vortex::…` with `using`
declarations. No call site knows which side of the line a file is on.

**What is copied is copied by a script, and only by the script.** `tools/port-from-bowling.py`
is Sims Bowling's `port-from-holdem.py` with its source tree and rewrite table changed
(`bowling`→`vortex`, `BOWLING_`→`VORTEX_`, the data-directory strings). It writes
`reference/PORTED.md` with the source path and SHA-256 of every file at the moment of the port;
`--check` reports drift on either side. As before, **identifiers are rewritten and prose is
not**: a comment that says "The Sims Bowling keeps its save in `savefile.dat`" stays true rather
than being made false, and `grep -rn "Sims Bowling\|Bowling\|Hold'em\|\bLost\b" src tests tools`
is the worklist for adopting each file.

The namespace is `vortex` (`vortex::eapp`, `vortex::platform`, `vortex::game`,
`vortex::runtime`, `vortex::gfx`…), the CMake options are `VORTEX_*`, the targets are `vortex`
and `vortex-headless`, the per-user data directory is `iPod Vortex/12345`, and the override is
`VORTEX_DATA_DIR`. A `bowling`, `holdem`, `lost` or `minigolf` identifier anywhere in this tree
is a defect.

---

## Architecture

```
recomps/Vortex/
  PLAN.md                this document
  README.md              layout, building, testing, contributing a decompiled function
  CMakeLists.txt         adds ../common; targets vortex (SDL3) and vortex-headless (tests)
  tools/
    survey.py            what is in the image — every number in this plan
    funcs.py             build gen/funcs.json — the function table the emitter works from
    emit.py              run the shared recompiler with this title's namespace and bindings
    progress.py          how much of the game is still recompiled rather than decompiled
    manifest.py          the game folder's file table -> src/gamedata/manifest_data.cpp
    probe.sh             run a script through the pinned emulator, cleanly (rule 11)
    port-from-bowling.py copy what is not yet shared, and record it (reference/PORTED.md)
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
function pointers; the rehearsal reaches 702 functions with no failure. Ghidra is for reading a
function and recovering a structure when a decompilation needs it.

---

## Where Vortex is none of the other four

Each of these was found by running the game through the emulator, or by reading the emulator's
own notes on it — `play.rs` carries a `vortex` entry in its defaults table, and `lib.rs` names
this title a dozen times, each time for something it does that no other title does. Each is a
measured fact that has to become code in this tree or a parameter in the shared one — never a
silent edit to a copied file.

**1. The frame-reason byte is Mini Golf's: seeded 5 and never written again.** The emulator's
defaults for a binary named `vortex*` are `ctx_seed: 5`, no frame reason, no pump mark
(`play.rs`, `defaults_for`) — so the pump seeds `[ctx+0]` with 5 before the init vectors and
then leaves the byte alone, frame after frame, exactly as Mini Golf's pump does and no other
title's. Whether this game ever reads the byte, and whether it answers at `[ctx+0x100]`, is not
established; nothing in the recordings depends on it, and the pump here writes the constant
once, in a named constant with this paragraph beside it. Five titles, five protocols now — the
"Not today" item that wants them as one parameter has its fifth instance.

**2. Buttons are a flags word at `0x18063e5c`, with no press times — Hold'em's model at a
different address.** The emulator finds the word from the `bic r0,r0,#0x60` signature and
reports it at start-up (`button flags word at 0x18063e5c`; `no press-time words for this
title`). A press sets its bit for one frame alongside a wheel sample, and the game only reads
its buttons on a frame whose `InputEvents #0` poll reported an event. `main.cpp`'s `ClickWheel`
is Hold'em's, with this address, and its provenance in the comment. **The wheel matters more
here than in any earlier title**: the game offers two control schemes — *finger position*
(the bat goes where the finger is on the wheel) and *wheel rotation* (the bat follows the
direction of turning) — and fires the bat's weapon on Select *or a double tap*
(`text.strings` 553–557). The emulator's wheel is an absolute position in 120 detents mapped
through `wheel_byte`, so both schemes are reachable from `wheel ±N` scripts; what the desktop
platform offers for the absolute scheme is a design item (Not today).

**3. The file model is Hold'em's, and the game writes three files into its own folder.**
Opens through `AsyncFileIO #3` carry a buffer and the game waits for the completion rather than
calling the read import (`--load-on-open`, the emulator's Vortex default) — twelve of them in a
boot — while `#0` opens (18) are followed by `#2` reads; completions are delivered between
frames. That is what Sims Bowling's `async_file.cpp` already does. What is new is the writing:
the game creates **`options`** (12 bytes) and **`stats`** (228 bytes) at the folder's root
during its boot, and `en/stats` (2 128 bytes, in a folder named for the language) once a name
is entered; without permission to create them "its loader retries the missing `options` forever
and never leaves the splash" (`play.rs`, which forces `allow_creates` for this title). None of
the three ships, and the owner's `Games_RO/12345` contains all three from an emulator session on
2026-08-22 — so `tools/manifest.py` ignores them by name and `tests/game-dir.sh` strips them from
every private copy, so that every recorded case is a first boot. What the game does with a
`stats` it finds — the reference `en/stats` begins `90 … 5 …`, and a first boot goes to ENTER
NAME — is a decompilation question, listed under Not today.

**4. It grows strings through `realloc`, one byte at a time, and no title has implemented it.**
`miscTBD #2` is called 1 927 times in a boot, and the log shows what it is: the same block
`0x19053530` asked for 0x20 bytes over and over as `text.strings` is appended a character at a
time. The emulator learned two things on this title (`lib.rs`, `Stub::Realloc`): the argument
order is `(old, size)` in `(r0, r1)` — *measured*, by which string keys came out (602, 603, 700
with that order; 0, 1, 2 with the other) — and **a block already big enough is returned in
place**, because the allocator rounds to 8 bytes and a realloc that always moved made the whole
parse quadratic (105 keys, 20 M instructions, the load callback never finished inside any
budget). A null old pointer is `malloc`; a zero size is `free`; a pointer the heap never handed
out gets a fresh block. The oracle compares guest addresses, so the algorithm is fixed, not a
design choice. It goes into the shared `Heap` (`../common`, rule 10), is named `misc_realloc`
in `imports.json`, and reaches the game through `device::reallocate`.

**5. It divides by its own frame delta, so the clock has two models and 30 fps is the default.**
Its tick at `0x1801a314` stores `now − last` in microseconds, converts it to 16.16 seconds, and
`0x18010aa4` divides by that value `asr #10` — by the frame time in 64ths of a second. A frame
shorter than 1/64 s truncates the divisor to zero and the runtime aborts ("Arithmetic
exception: Divide By Zero"); the emulator saw it at frames 69, 402 and 1502 across three 60 fps
runs, floors every frame at the paced interval (`hold_clock_above`), and paces this title at
**30 fps** — the device's rate, and twice the margin. It also reads the clock **~6 times a
frame** (12 571 `miscTBD #9` calls in 3 000 frames; the `logic = %d usec` / `render = %d usec`
strings say why): the fixed-step-per-call clock every earlier recomp uses would hand this game
100 ms frames and run it six times too fast. So `misc.cpp` has two clock models, and which one
is live is a fact about the run, not the title: under `--emulator-firmware` the step is per call
(`16 667 µs`, the emulator's `--fixed-clock`, what the recordings are made with); in a real run
the clock is the platform's, advanced once a frame by the frame interval and never by less than
1/64 s. *(Worth knowing beyond this title: Sims Bowling reads the clock ~40 times a frame and
Hold'em's is also per call, so their windowed builds most likely run fast for the same reason.
Not this plan's problem; noted in Not today.)*

**6. It uses a second texture unit, the framebuffer as a texture, and the float matrix path.**
`OpenGLES #0 glActiveTexture` is called 252 times in a boot, alternating `GL_TEXTURE1` and
`GL_TEXTURE0` at two call sites (`0x18007c00`, `0x18007c1c`) — the only title that selects a
unit other than 0 (`lib.rs`). The shared rasteriser models one sampled unit and already keeps
unit 0's binding separate from the active one (`set_active_texture` / `bind_texture`), which is
the emulator's answer; the picture oracle is what says it is enough. `#21 glCopyTexImage2D` (6
calls, `tex#56 320x240 from (0,0)`) copies the framebuffer into a texture — the ring effect on
the title, presumably — and `#105 glTexSubImage2D` patches two textures at boot. Matrices go
through `#125 glUniformMatrix4fv` (float, 23 511 calls) with `#175 glMultMatrix`, `#165
glLoadIdentity`, `#169`/`#173` translate/rotate and one `#167 glOrtho` — Mini Golf's and
Hold'em's family, every one of them already in the shared rasteriser. `#148` (the vector
constant colour, 9 175 calls, one per draw) is Hold'em's. Pipelines 7, 8, 14 and 32 are selected
through `#159`. Two rules need this title's answer and only the picture gives it: whether the
game re-points every attribute before every draw (`#137`/`#40` come 97 373 times each for 35 516
draws — about three per draw, so it does, like Lost) and what an untextured draw is painted in.

**7. Sound is one bank the game reads itself, and music is three tracks.** The 47 effects come
out of `media/sfx` (a 2 563 812-byte bank whose first word is 47) through the game's own reader,
and are registered as 47 `Audio #0` objects with `#7` buffers at boot — Hold'em's shape, not
Mini Golf's `.wav` files. Music: `#40` registers `a.m4a`, `b.m4a` and `c.m4a`, `a.m4a` plays
repeating through `#43`/`#48`; the shared decoder handles it. Three ordinals want names:
`Audio #47` (Sims Bowling answers 0), `#49` and `#55` (the emulator answers 0 to both), and
`Metadata #125` (the emulator answers −1, "current index = none").

**8. The data formats are its own, and they are small.** `tex` is a 46-record pack (a count
word, then `(offset, size)` pairs — `(320016, 11568)`, `(52000, 23120)` …) that holds the
textures the 45 `.ipd` files do not (the emulator sees 57 texture names). `media/sfx` is the
same shape with 47 records. `gamedata/levels/levels` is 8 640 bytes of brick cells (`0x0020`,
`0x0420` words). `fonts/*.txt` are glyph tables (`code x y w h …`, `#END`). `text.strings` is
`"key"="value";` with `//` comments, keys parsed with `atoi`, terminated by the literal `"-1"=""`
line. `options` is 12 bytes (`01000000 ff7f0000 00000000` — a control scheme and a volume,
presumably) and `stats` 228 bytes of zeros on a first boot. None of it is a framework's
business; all of it is decompilation work, and the loaders are the best early targets after the
string helpers — each is a bounded function with a format on the other side.

**9. Its play path is short, and the wheel is on it.** Title → Select → ENTER NAME (the wheel
picks a letter, Select types it, the DONE glyph finishes) → MAIN MENU (an icon ring: *new game /
personal info / options / exit game*, `text.strings` 200–203) → *new game* → the first level,
"Infinite Loop". The wheel is **eight detents a position, floored**, and a positive turn goes
forward through the alphabet (measured in the progress log); `tests/scripts/first-level.script`
is that path, to GAME OVER.

---

## Schedule

Each block ends in something runnable *and* reviewed against the quality rules. If a block
overruns, the fallback is named. Cutting quality is never the fallback.

### Block 0 — scaffold, provenance, and the pinned emulator (30 min)

* `README.md`, `.clang-format`, `.gitignore` (`gen/`, `build*/`), `pyproject.toml`.
* `tools/port-from-bowling.py` from Sims Bowling's script; run it. `reference/PORTED.md` written.
* `reference/MANIFEST.md` and `tools/oracle-emulator/`: `tools/eapp-loader` and `tools/arm7tdmi`
  at commit `96bfe90`, plus the detached `Cargo.toml`. Record the commit **and** the SHA-256 of
  the sources. The live tree's `src/` is byte-identical to the Sims Bowling pin today (checked
  with `diff -rq`); the copy is taken from the live tree and hashed against `git show`.
* `tools/manifest.py "…/Games_RO/12345"` → `src/gamedata/manifest_data.cpp`, with `options`,
  `stats` and `<lang>/stats` ignored (difference 3) — 96 files.
* `tools/probe.sh`: the emulator run of rule 11 — a fresh save-less copy of the folder, the
  title's flags, the screenshot copied on the line that announces it, a stray-press check.

**Exit:** `cmake -B build` configures; `python3 tools/survey.py` reproduces `analysis/survey.txt`.

### Block 1 — emit (20 min)

* `tools/funcs.py` with this title's paths (`Games_RO/12345/Executables/…`, `analysis/coverage/`)
  and `tools/emit.py` with `Generator(namespace="vortex")`. Rehearsed: 702 functions, 35 199
  instructions, 0 unwalkable. The number to reproduce is 702.

**Exit:** `gen/` compiles under `vortex_generated`. **Fallback:** none should be needed; an
idiom this image uses that the recompiler does not model is a shared fix under rule 10.

### Block 2 — runtime and the frame pump (45 min)

* `src/runtime/` from Sims Bowling; `memory.cpp` region sizes to be re-measured from the longest
  recorded session before the day ends (start with Sims Bowling's, which are Lost's).
* `main.cpp`: the constant reason of difference 1 and the flags word of difference 2 — Hold'em's
  `ClickWheel` at `0x18063e5c`, no press times — as named constants with the evidence cited.
  `TERMINATE_VECTOR_SLOT = 1`, as in every title. The 30 fps default of difference 5.
* Every ordinal logging and returning 0.

**Checkpoint A:** `build/vortex-headless … --frames=2` runs the start-up vector and the first
frames and its log begins as `analysis/coverage/boot-summary.txt`'s run began: ten `miscTBD #0`
allocations of 16 bytes from `0x18020db8`.

### Block 3 — the oracle (30 min)

* `tests/record.sh` with this title's flags, which are part of every case and not a matter of
  taste: `--load-on-open --ctx-seed=5 --allow-creates --fixed-clock --fps=0`. (`--load-on-open`,
  the seed and the asynchronous file model are the emulator's defaults for a binary named
  `vortex*`; they are spelled out so the case does not depend on a defaults table in another
  tree. The 30 fps default is a pacing rule and `--fps=0` overrides it; under `--fixed-clock`
  the pace does not reach the game.) The stray-press check of rule 11.
* Record `boot` (`600: quit` — the title is drawn by 300), `title` (a screenshot at 900,
  `1000: quit`) and `name-entry` (one Select, a screenshot of ENTER NAME, `4000: quit`). Every
  script short and ending in `quit`; `tests/game-dir.sh` gives each run a fresh, file-less copy
  of the folder.
* `imports.json`: Sims Bowling's table plus `miscTBD #2 misc_realloc(2)`, `Audio #49`, `#55`
  and `Metadata #125` from `lib.rs`, with argument counts.

**Exit:** `tests/diff.sh boot` runs and reports its first divergence with a line number.

### Block 4 — libeapp, in boot order (2 h)

In the order the boot log calls them, re-running `tests/diff.sh boot` after each group:
miscTBD (`#2` realloc — **in `../common`, verified in the other four trees first**; the clock
model of difference 5) / Settings / InputEvents → AsyncFileIO (difference 3: the three written
files, the store, `allow_creates`) → Metadata (`#62`, `#134`, `#125`) → Audio (the bank buffers
of difference 7; `#47`, `#49`, `#55`) → OpenGLES (the shared rasteriser as it is; difference 6
is the picture's to judge). Then `tests/diff.sh title` and `name-entry`.

**Checkpoint B:** `diff.sh boot`, `title` and `name-entry` pass, semantic and exact — the recomp
is *proven* before a pixel is visible. **Fallback:** if AsyncFileIO is still diverging at the
ninety-minute mark, `boot` is the deliverable and the rest moves to tomorrow with the divergence
written up.

### Block 5 — SDL3 and the picture (45 min)

* `src/platform/` from Sims Bowling; `sdl3_platform.cpp` with the title's name and bindings;
  the shared music decoder through `IPOD_CORE_SDL3_SOURCES`; the frame-interval clock of
  difference 5 for the windowed build.
* `tests/frames.sh` from Sims Bowling, made rule-11-safe, with the `shot` frames of `title`
  and `name-entry` compared against the emulator's PNGs at a threshold. Differences 6's unit-1
  binding and the framebuffer copy have no oracle but this one.

**Checkpoint C:** the title screen on screen, natively, with its orbiting ball; Select reaches
ENTER NAME and the wheel moves the letter.

### Block 6 — the first oracle that plays, then decompilation (the rest)

* Measure the wheel on the name wheel (`wheel ±N` bursts at least 20 frames apart, as Mini
  Golf's Risk 4 prescribes): detents per letter, direction, where ✓ is. Then `first-level.script`:
  a name, *new game*, the first level with the ball launched and the bat moved, `quit`. Record
  it. If the emulator cannot get there, that is the first finding of the progress log and an
  emulator-tree change in its own commit, followed by a re-pin.
* `tests/vs-recomp.sh` and `tests/check-recomp.sh` ported from Mini Golf on day one, not later.
* Then the swap loop, each swap diffed: the string helpers and the `text.strings` parser (the
  realloc-per-byte loop at `0x1801e4b0`); the tick at `0x1801a314` and the frame-delta divide
  at `0x18010aa4`; the loaders of difference 8 — `tex`, `media/sfx`, `levels`, the fonts — and
  only then the physics. Top-down from the vectors, because that is the order in which the
  state structures are forced into the open.

### Block 7 — the sharing pass (after Checkpoint C, in its own sitting)

Rule 9 applied to what this port copied for the *fifth* time. Measured today, after namespace
normalisation, these files are byte-identical between Hold'em and Sims Bowling and will be
between them and this tree: `tests/diff.py`, `tests/frames.py`, `tools/ppm2png.py`,
`tools/progress.py`, `src/libeapp/{input,misc,host_state,framework_call,metadata}.cpp`,
`src/platform/sdl3/sdl3_platform.cpp`. The Python has no namespace to cross and moves first
(`../common/tests/`, `../common/tools/`), each title keeping a three-line shim; every title's
suite is run before and after (rule 10). The C++ that implements per-title framework headers
waits until the headers can move with it — the lesson `../common/README.md` records.

---

## Risks, in the order they will bite

1. **The clock model (difference 5) is a new kind of parameter.** Every earlier recomp's clock
   is per call and the oracle never noticed because the emulator's `--fixed-clock` is per call
   too. Getting the recorded behaviour byte-exact needs the per-call model; getting the game
   playable needs the other; a mistake in the switch is invisible to the oracle. Mitigation: the
   flag that selects it is `--emulator-firmware`, which already exists and already means "behave
   as the recordings were made"; the windowed build's speed is checked by eye against the
   emulator at 30 fps (the ball's orbit on the title is a clock).
2. **`realloc` is shared code with one title calling it.** A wrong grow-in-place rule moves a
   pointer the emulator kept, and every later heap address diverges — loudly, at the first
   allocation after it, which is the good direction. Mitigation: the emulator's `Stub::Realloc`
   arm is the spec, read before writing; the four other titles never call `#2`, so their
   oracles guard only that `alloc`/`free` are unchanged.
3. **The picture may disagree where the log cannot see.** Unit-1 binding, the framebuffer copy,
   and the float matrix path are all things a call log passes by construction. Mitigation:
   `frames.sh title` in block 5; the title's orbiting ball over a copied framebuffer is the
   hardest frame this game has, and it is frame 300.
4. **The wheel is the game.** A Breakout with a mis-measured wheel is unplayable and its play
   oracle unrecordable. Mitigation: block 6 measures on the name wheel, where a wrong step is
   a wrong letter — visible, cheap, and recorded.
5. **Stray input on a shared machine (rule 11).** Already bitten once today. Mitigation: the
   checks in `record.sh`, `frames.sh` and `probe.sh`; a recording with a press it did not script
   is refused, not kept.
6. **The seed misses a computed target.** Only 21 stored function pointers were needed for
   Hold'em's 951 functions; this image is smaller and simpler, but a slot that is *built* is
   invisible until `call_indirect` stops on it. Mitigation: every recorded session's edge dump
   goes into the seed; the first fatal names the address and it goes into `extra-entries.txt`.

---

## Not today, written down so it is not forgotten

* **The frame-reason protocol as a parameter.** Five titles, five protocols: a constant (Mini
  Golf, and now Vortex), a first-frame value (Lost), a seeded init (Hold'em), a handshake (Sims
  Bowling). Two of the five are the same one now, which is a hint about the shape.
* **The clock as a parameter, in the core.** Per-call for the oracle, per-frame with a floor for
  play. Hold'em and Sims Bowling probably need the same switch for their windowed builds.
* **An absolute wheel input for the finger-position scheme.** The desktop bindings table has
  seven actions and no axis. A mouse angle around the window's centre, an analogue stick, or a
  key-held velocity are the candidates; the game's own *wheel rotation* scheme works from the
  existing bindings, so the port is playable without it.
* **A shared test harness** (block 7) and **the three-way `--check`** across five `PORTED.md`s.
* **The written files.** What the game does with an `options`/`stats` it finds; whether a known
  name skips ENTER NAME; where the save store should put them on a real run (the platform's
  choice, as Mini Golf settled it).
* **Localisation.** Eleven `.lproj` string tables and per-language `UI3/BonusTypes_<lang>.ipd`
  sheets, selected by the `Settings #0` "Language" answer.
* **The packs.** A tool that lists and extracts `tex` and `media/sfx` records would make every
  asset question answerable outside the game.
* **The Switch.** Mini Golf has a platform; nothing here should make a second one harder.

---

## Progress log

Each entry says what was established, not what was attempted, and ends with what was **not**
verified.

### 2026-08-28 — before the first line: what the image and the emulator say

Done before this tree existed, with the Sims Bowling tools and the Sims Bowling-pinned emulator
(byte-identical to `tools/eapp-loader` at `96bfe90`, checked with `diff -rq`), to size the job
and find the differences above. Everything is in `analysis/`.

* `survey.py` against the image: 543 functions and 32 317 instructions from the vectors, 0
  unwalkable; seven frameworks, 429 thunks in Lost's exact layout. `eapp-inspect`: build
  `2563290`, header `0x10001000`, the block-count warning, plaintext (entropy 4.35).
* `funcs.py` with two probes' edges and `emit.py` with Sims Bowling's bindings, in a scratch
  directory: **702 functions, 35 199 instructions, 0 unwalkable**, 269 found by walking, 64 315
  lines of C++ in 35 files in 0.4 s. The Sims Bowling emitter, unmodified, translates the whole
  image.
* Emulator sessions, all with `--load-on-open --allow-creates --fixed-clock --fps=0` and the
  title defaults (reason seed 5, no reason writes, asynchronous files, a flags word at
  `0x18063e5c`, no press times, `allow_creates` forced):
  * a 3 000-frame silent boot: 527 496 framework calls, 58 ordinals, 3 224 branch edges; 45
    `.ipd` textures loaded by frame ~120 and 57 texture names in use; the title screen with its
    orbiting ball at frame 300 and unchanged at 3 600 (`coverage/screens-probe-shot-*.png`);
    `a.m4a` repeating; `options` and `stats` created at the folder's root. ~176 calls a frame.
  * one Select at 3 000 and another at 3 600 (`scripts/select-probe.script`): 753 352 calls,
    3 633 edges, no new ordinals. Frame 3 100 already shows **ENTER NAME** with an empty name,
    and 3 700 onwards the same screen with an `A` typed by the second Select
    (`coverage/select-probe-shot-*.png`). The first run of this probe kept four shots that were
    Sims Bowling's, from a concurrent session — rule 11; the files here are from a clean re-run
    through `tools/probe.sh`.
  * a first, discarded boot picked up ten Selects from clicks on the emulator's window
    (407 631 calls, a different path); it is why rule 11 exists and why `tools/probe.sh` checks.
* Three reached ordinals are unnamed in every title (`ordinals.txt`): `miscTBD #2` (realloc,
  1 927 calls, `(old, size)`), `Audio #49` and `Metadata #125`; `Audio #47` and `#55` are named
  only as zeros. `OpenGLES #0` with `GL_TEXTURE1` draws the emulator's one-unit warning.
* The `text.strings` table names every screen (`analysis/README.md` says where): MAIN MENU,
  PERSONAL INFO, OPTIONS, VOLUME, PAUSED, ENTER NAME, LEVEL %s CLEARED, GAME OVER, HELP; the
  eleven bonuses and six brick types; five difficulties; two control schemes; three music modes.

**Not verified:** anything past ENTER NAME — the main menu and a level are inferred from the
string table, not seen. The wheel (no probe turned it). Whether the game reads `[ctx+0]` or
answers at `[ctx+0x100]`. Region high-water marks. What the game does with the `options`/`stats`
it wrote when it boots again. Whether unit-1 binding and the framebuffer copy render right —
only the picture oracle can say, and it does not exist yet. What `Audio #49`, `#55` and
`Metadata #125` mean beyond the values the emulator answers.

### 2026-08-28 — blocks 0 to 5: the recompilation runs, and every oracle is identical

Done the same morning, in the order of the schedule. Established:

* **Block 0.** `tools/port-from-bowling.py` copied 70 files from Sims Bowling with identifier
  rewrites only; prose that names another title was left true rather than rewritten false, and
  `grep -rn "Sims Bowling\|Bowling\|Hold'em\|\bLost\b" src tests tools` is the worklist for
  adopting each file (the two strings the program *writes* — the headers of `settings.txt` and
  `bindings.txt` — said "Lost" and are fixed). The emulator is pinned at `96bfe90` from the live
  tree, every source hash checked against `git show` (`reference/MANIFEST.md`). The manifest has
  96 files; `tools/manifest.py` ignores `options` and `stats` by name and `tests/game-dir.sh`
  strips them (and `<lang>/stats`) from every private copy. `tools/probe.sh` is rule 11 as a tool.
* **Block 1.** `funcs.py` → 895 seed entries (81 stored function pointers, 429 live targets from
  two edge dumps); `emit.py` → **702 functions, 35 199 instructions, 0 unwalkable**, 64 315 lines
  in 35 files in 0.4 s. No change to the shared recompiler. The tree builds in under a minute.
* **Block 2.** `main.cpp` rewritten around differences 1, 2 and 5: the reason byte seeded 5 and
  never written; Hold'em's `ClickWheel` at `0x18063e5c`; `FrameClock`, which advances the game's
  clock once a frame by the frame's length and never by less than a 64th of a second, and
  `--emulator-firmware`, which steps it per call instead (`misc.cpp`, `set_clock_advances_per_call`).
  **Checkpoint A:** the first two frames identical to the emulator, addresses included.
* **Block 3.** `tests/record.sh` spells out `--load-on-open --ctx-seed=5 --allow-creates
  --fixed-clock --fps=0` and refuses a recording whose emulator output shows a window press —
  its first version refused all three cases on the emulator's own "button flags word at …"
  line, which is why the check now matches `-> flags bit`. Three cases: `boot` (600 frames,
  100 574 calls), `title` (a screenshot at 900, 172 330 calls), `name-entry` (one Select, a
  screenshot at 3 300, 621 744 calls). `imports.json` gained `miscTBD #2 misc_realloc(2)`,
  `Audio #49` (zero) and `Metadata #125 metadata_now_playing_index` (answers `NO_TRACK`).
* **Block 4.** One divergence, found at call 288: every `#2` answered 0, so the string parser
  started a fresh block for every character and never reached the table's end — not a fault in
  the new `Heap::realloc` but the generated bindings, emitted *before* `imports.json` named the
  ordinal. Re-emitting fixed it; the lesson is the README's "re-run the two tools after changing
  the imports", learned the expensive way. `Heap::realloc` is in `../common` under rule 10:
  Hold'em 16/16, Sims Bowling 22/22, Mini Golf 30/30, Lost 17/17 (its `frames_jungle` failed twice while this
  title's `title` recording was writing `/tmp/ipod-shot-01.png` — rule 11 from the other side —
  and passed alone). **Checkpoint B:** `boot`, `title` and `name-entry` identical, semantic and
  exact, with no allowances.
* **The game's own files** go through the platform's store: `is_save_name` is `options`,
  `stats` and `<lang>/stats` (kept as `<lang>.stats`, because a store's names are flat and the
  two `stats` would collide); a read-mode open of a missing one fails, as on the device, and a
  write-mode open starts empty and keeps what the transfer brings. Sims Bowling's rule — a save
  is read whatever mode names it — was wrong for this title, whose `options` is created by a
  write-mode open and a 12-byte transfer (watched with `VORTEX_TRACE_FILES=1`).
  `tests/unit/save_files_test.cpp` is those rules. A real run (per-frame clock, writes allowed)
  reaches the title in 1.9 s of host time for 600 frames and leaves `options` (12 bytes) and
  `stats` (228) in the store.
* **Block 5.** `tests/frames.sh` copies each screenshot on the emulator's own announcing line
  and checks the frame numbers it was promised. `frames.sh title`: 0.00% of pixels over the
  threshold (largest single-channel difference 30 — the rasterisers' rounding); `frames.sh
  name-entry`: **pixel-identical**. So the framebuffer copy, the unit-1 binding and the float
  matrix path draw as the emulator draws them, on the two screens there are. The SDL3 `vortex`
  target builds. **Checkpoint C**, as far as a headless machine can take it: the title screen and
  ENTER NAME come out of the recomp identical to the emulator's; the window itself has not been
  looked at by a person.
* `ctest`: **16/16** at the end of block 5 — eight unit tests, three cases each in semantic and exact form, two pictures; **19/19** after the play session below added `first-level` (semantic, exact, and its three pictures), with every recording re-made under the pinned wall clock.

### 2026-08-28, later — the first play session, and what it found

The owner played the windowed build past ENTER NAME and hit two things. Both are now in the
tree, one fixed and one written up.

* **A pointer the seed could not see.** `fatal: indirect call to 0x1800329c, which is not a
  function entry (from 0x18002428 …)`. The caller is armcc's `printf` core — `ldr r1,[r5,#0x28]
  / mov lr,pc / mov pc,r1` — calling the string *reader* its descriptor carries, and the reader
  is a five-instruction routine whose address `sprintf` forms with `ldr r0,[pc,#0x28] / add
  r0,pc,r0` (`0x18002188`): a *distance* in the literal pool, not an address, so neither the
  stored-pointer scan nor a plain `adr` scan could find it. `tools/funcs.py` has a third source
  now — code addresses formed from the program counter, near (`add rN, pc, #imm`) and far (an
  `ldr rN, [pc, #n]` then `add rN, pc, rN`) — which found **52** of them; the table is 753
  functions and 39 247 instructions, and the path to GAME OVER replays without a fatal. The
  source belongs in the shared recompiler's `functions.py` (block 7).
* **The wheel, measured** (`analysis/coverage/wheel-probe-*`, `past-name-probe-*`): +1, +1, +3,
  −5 leave the highlight on A; +10 moves it to B; −10 from A lands on BACKSPACE, two positions
  back. **Eight detents a position, floored**, positive forward through the alphabet. With that,
  `new-game-probe.script` reached the **MAIN MENU** (an icon ring) and the **first level**
  ("Infinite Loop", the ball in play) and ran to GAME OVER — `tests/scripts/first-level.script`,
  the first oracle that plays, 1 045 823 calls.
* **The recordings carried the minute they were made in.** Replaying that path diverged at call
  626 701 on an allocation of 7 bytes against 8: the main menu formats the wall clock (`miscTBD
  #12`) into a string, and "7:31am" is a character shorter than the recomp's "12:00am". The
  emulator had no way to pin its wall clock, so it has one now — `--time=HH:MM`, 22 lines in
  the live tree beside `--battery=`, copied into the pin (`reference/MANIFEST.md`), and part of
  every recording and probe from here on. It is the live tree's to commit.
* **The name wheel's three glyph tiles draw wrong, and the emulator draws them the same way.**
  The tiles are the 32×32 RGB565 textures `#14`, `#28` and `#50`, uploaded once at boot from
  bytes the game decodes itself out of the `tex` pack (eight reads, `--file-ops`), and drawn
  through pipeline 9 — which, per the firmware's pipeline table (§17.5/17.9 of the emulator's
  reversing dossier), samples exactly *one* texture, so the second unit this game also binds is
  not the cause. Their content is wrong on both sides and even changes between frames (compare
  `wheel-probe-shot-04100.png` and `-04700.png`), which points at the bytes the game produced —
  its pack reader or its decoder — rather than at the rasteriser. **Open.** The recomp is
  pixel-identical to the emulator here, so the picture oracle cannot see it; the way in is the
  decompilation of the `tex` reader (block 6's loaders), with the pack's records as the check.

### 2026-08-28, evening — two faults the player found, and what they were

Both were reported from playing the windowed build, and both turned out to be real and *shared
with the emulator*, which is why no oracle had caught either.

* **Escape froze the game in the menus.** Escape is the Menu button, and Menu on the main menu is
  this game's way out. Watched at `ctx+0` across the press (`--dump-frame=190000a0:20`): the byte
  reads 1 while the game runs, goes to 5 on the frame of the press and **6** on the next, and from
  there the game makes four framework calls a frame and draws nothing — for ever. 6 is
  "suspended": on the device RetailOS would tear the application down and go back to the iPod's
  menu. The emulator does neither — it went on calling the dead game to the end of the script,
  162 calls a frame down to 4 — so the recordings are of a game that never gets put away, and the
  freeze was reproducible in both. The pump now ends the run when it sees 6 (the Mini Golf
  recomp's rule, and its value), so Escape quits; `--emulator-firmware` keeps the old behaviour
  for the oracle. `tests/suspend.sh` is the guard, and it has to be the recomp against itself:
  no recording can hold this, because the emulator does not do it.
* **The ENTER NAME glyphs were noise.** The backspace, space and done tiles are pack entries 6,
  20 and 42 of `tex`, whose 16-byte headers declare 32x32 at **16** bits a pixel over 1024 bytes
  of data — exactly 32x32 at **8**. The game's uploader at `0x18015628` dispatches on byte 9 of
  that header (`ldrb r3,[ip,#9] / cmp r3,#8 / … / cmp r3,#0x10`; 8 selects `GL_ALPHA` from the
  literal at `0x18015788`, 16 selects `GL_RGB` + `GL_UNSIGNED_SHORT_5_6_5`), so it asks the driver
  for a 2048-byte 5-6-5 upload over a 1024-byte source: half of each tile is the *next* entry's
  bytes, and the half that was "right" was an 8-bit mask misread as colour. Read as the 8-bit
  alpha mask it is, the same 1024 bytes are a clean glyph — which is the proof that the label is
  what is wrong, not the data. `src/gamedata/asset_fixes.h` corrects the one byte, and only where
  the entry's own length proves the label wrong; `--original-assets` turns it off and every
  recorded case passes it, because the recordings are of the shipped bytes.
  `tests/unit/asset_fixes_test.cpp` covers the rule's edges — a paletted entry, an entry that
  matches neither label, a file that only looks like the pack — because this code rewrites the
  player's own game data.

Both fixes are in the *port*, not in the emulator: the emulator is the oracle and its job is to
be the device, faithfully, including the ways the device's own data was wrong.

`ctest`: **21/21** — nine unit tests, four cases each in semantic and exact form, three pictures,
and the suspend. One caution learned twice today: running the suite while anything else uses the
emulator makes a picture case fail on its own (`frames_title` did, and passed alone) — rule 11,
from the other side.

### 2026-08-28, later — the saves, and what Escape really does

Three more reports from playing it. The first two are one fault; the third is not a fault at all.

* **Saves did not survive a launch, and this was ours.** Everything this game keeps is written
  through the *store* ordinals when it is left — `stats` (228 bytes), `<lang>/stats` (2 128),
  `options` (12) and the saved game **`quick<name>`** (18 988; the player named it) — and read
  back at start-up through the ordinary file calls with an open that names **mode 1**. This tree
  read that mode as "write", which is what another title's file layer measured for its own save,
  and so at every start-up it took the game's *uninitialised* buffer and wrote it over each save
  before the title screen: `stats` came back 228 bytes of zeros and the saved game was gone. Two
  further defects sat behind it: the store keeps flat names, so `en/stats` was stored as `stats`
  and 2 128 bytes of statistics landed on top of the 228-byte file every time; and nothing knew
  `quick<name>` was a save at all, because the player chooses that name. Now a save is opened to
  be read whatever mode it names, an absent one opens *empty* (refusing it left the game on
  `LOADING…` for ever, waiting for a file it had no way to create), the store's own contents
  decide what a save is, and the last two path components make the key. Measured end to end: a
  play-through writes four distinct files; the next launch reads all four back, leaves every one
  of them byte-identical, and **resumes the saved game** — LEVEL 1 with its time to beat, instead
  of ENTER NAME. `tests/unit/save_files_test.cpp` is those rules, rewritten around the corrected
  model. None of it is visible to the recorded oracles, which run with writes refused and the
  store stubbed, which is why four green oracles said nothing about a save that was being
  destroyed on every boot.
* **Escape during play ends the game, and that is what the button does.** Escape is Menu; Menu is
  how an iPod game is left. The tick at `0x1801a314` reads a reason byte through `[state+0x20]`,
  dispatches on it 0..7, and *answers in the same byte* through `[state+0x24]`: the normal frame
  answers 1, and the Menu press answers **5** — "suspend me" — after which the game answers 6 to
  everything. The firmware is meant to consume that request; nothing here can, so the run ends,
  and it now says so on the way out. Two things were tried and are recorded because they are the
  obvious next guesses: writing a reason every frame (the game then makes four calls a frame from
  the *first* frame and never runs — the byte really is the game's after the seed), and
  Play/Pause during a level (the level goes on scoring; it is not the pause key). The game's
  `PAUSED` screen exists in its string table and **what reaches it is still unknown** — Next and
  Previous were probed but the game had already run to GAME OVER by then, so that probe proves
  nothing.
* **The unmapped access is the game's own, once, and harmless.** `VORTEX_TRACE_UNMAPPED=1` names
  it exactly: the same tick, `ldr r0,[r4,#0x20] / ldrb r0,[r0]` — on the very first frame that
  field is still null, so the game reads byte 0 and dispatches on what it finds. The emulator
  answers such a read with zero and so does this port; one access, on one frame, before the game
  has been handed its context. The line at exit is the port being honest about it, not a fault.

`ctest`: **21/21**.

**Not verified:** what reaches the game's `PAUSED` screen (Menu leaves, Play does not pause).
Whether Menu from a *submenu* backs out rather than leaving (only the main menu and a level were
watched). What the other two mislabelled entries look like in place — the space and done
tiles are corrected by the same rule and drawn, but only the backspace glyph was decoded by hand
and compared texel for texel. Whether any other shipped asset is mislabelled in a way whose
length does not prove it.

**Not verified:** what the game does past GAME OVER, and a level played with the wheel (the
script touches nothing). Whether every computed pointer is now in the seed — the fatal names the
next one. Whether the game reads `[ctx+0]` or answers at
`[ctx+0x100]`. Region high-water marks (Lost's sizes, unmeasured here). What the game does with
the `options`/`stats` it wrote when it boots again, and what it writes into `en/stats`. The
windowed build's speed by eye against the emulator at 30 fps (Risk 1) — the per-frame clock is
reasoned from the emulator's own notes, not yet watched.
