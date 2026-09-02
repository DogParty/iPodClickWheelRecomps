# Lost recomp — plan of attack

**Goal by end of day:** `Lost_1_1_2917525.bin` — the 2008 iPod click-wheel game *Lost: Via
Domus* — running natively on the ARM Mac as a statically recompiled C++ program, drawing into
an SDL3 window, driven by the keyboard, and *proven* equivalent to the emulator by diffing the
sequence of framework calls both make on the same scripted input. Hand decompilation starts
today and does not finish today: 789 functions and 65 423 ARM instructions are not rewritten by
hand in a day.

Everything lives in this folder. The emulator tree is read, snapshotted, and otherwise left
alone.

This is the second title to go through this process. The first, *Mini Golf*, is finished —
every one of its 333 functions is hand-decompiled and it runs on macOS and (untested) the
Switch. `../Mini Golf/PLAN.md` and `../Mini Golf/README.md` are the record of how that was
done, and the machinery it produced is the reason today's goal is achievable. **Read the code
quality section of that plan; it binds this project unchanged.** What is written below is the
delta: what Lost is, what it inherits, and — the part that actually matters — the seven places
where Lost is not Mini Golf.

---

## What Lost is

Measured, not guessed. The numbers come from `tools/eapp-inspect` in the emulator tree, from a
static control-flow walk of the image with the Mini Golf emitter's `recomp.cfg` (recorded in
`analysis/survey.txt`), and from a 2.9-million-call boot log recorded from `play`.

| | |
|---|---|
| image | `Lost_1_1_2917525.bin`, 268 140 bytes, loads flat at `0x18000000`, ends `0x1804176c` |
| game data | `20 iPod games/Games_RO/1B200/` — 72 files, ~35 MB (`l`, `l1`–`l26`, `d1`–`d15`, `s`, 16 music tracks, 10 sound banks, `rserver.bin`) |
| entry vectors | 3: `0x1803d498` start-up, `0x1803d414` terminate, `0x1803d4f4` per-frame |
| functions | **747** reachable by walking from the vectors; **789** once live coverage and the image's own stored function pointers are added |
| instructions | **65 423** ARM instructions recompiled |
| import thunks | **429** across seven frameworks |
| frameworks | OpenGLES 179 · Metadata 152 · Audio 61 · AsyncFileIO 17 · miscTBD 15 · Settings 3 · InputEvents 2 |
| ordinals a silent boot reaches | 42 |
| code properties | ARM state only, no Thumb, no VFP — but armcc's **soft-float library is linked in and reached**, unlike Mini Golf |
| emulator behaviour | boots to the name-entry screen, ~21 700 instructions and ~28 quads a frame in the attract loop |

For scale: Mini Golf was 333 functions and 23 268 instructions. **Lost is 2.8× the code.**

`analysis/survey.txt` is the output of `tools/survey.py`, which is where those numbers come
from. When this plan was written the same walk — with the Mini Golf emitter exactly as it stood
— reached 736 functions and failed on 11, every one of them in armcc's soft-float library at
`0x1803d8f0`–`0x1803e5xx`, all on the same instruction shape: `mrs`/`msr`, which that decoder
deliberately did not model. See difference 7 below, and the progress log for what was done about
it.

---

## Code quality

`../Mini Golf/PLAN.md` § "Code quality — non-negotiable" is this project's rule set, unchanged
and unrestated. Its substance in one paragraph, so nobody has an excuse not to have read it:
C++17 everywhere, `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` on hand-written code,
no exceptions, no RTTI, fixed-width types for anything mirroring guest memory, `enum class` and
`constexpr` over macros and `#define`, RAII for every host resource, generated code confined to
`gen/` and never hand-edited, every framework entry called by its name and never by its ordinal,
every hand-decompiled function named for what it does with its guest address in a comment, every
non-obvious line explained and every claim traceable to where it was established.

Two additions this project makes, both learned from the first one:

**7. A claim about behaviour is worth what its evidence is worth.** "Verified" means an oracle
compared it, a unit test pinned it, or a trace shows it. "Read carefully" is a different word
and must be used when that is what happened. Mini Golf's Switch port is written up that way and
the write-up is better for it.

**8. Nothing is copied from Mini Golf without a provenance line.** Every file ported from the
first project carries where it came from, and `reference/PORTED.md` lists all of them with the
source path and the SHA-256 at the time of the port. The copy is deliberate debt (below); the
manifest is what keeps it from becoming invisible debt.

---

## What is inherited, and the debt that comes with it

Mini Golf produced four layers that are genuinely title-independent, and one that is not:

| layer | title-independent? | what happens here |
|---|---|---|
| `tools/recomp/` — the ARM→C++ emitter | **yes**, entirely | copied, one constant changed |
| `src/runtime/` — guest memory, CPU state, frame pump | **yes** | copied, region sizes re-measured |
| `src/framework/` — the typed platform interfaces | **yes** | copied, extended for Lost's ordinals |
| `src/platform/` — SDL3, null, paths, settings, bindings, save store, wav, zip | **yes** | copied |
| `src/libeapp/` — the iPod frameworks | **partly** | ported and then substantially rewritten (see below) |
| `src/game/` | no | nothing to inherit; this is the work |

**The layers are copied into this tree, not shared.** That is a deliberate choice and it is the
one piece of debt this plan takes on knowingly:

* *Why not share now.* Mini Golf is finished and green — 20 ctest cases, six recorded call-log
  oracles, two vertex-hash streams. Factoring its runtime and platform layers into a shared
  library means re-verifying all of that, on the day whose budget is "get Lost on screen". The
  refactor is not hard; it is just not free, and it is not what is being asked for today.
* *Why it is safe to do it this way.* `reference/PORTED.md` records the source and checksum of
  every ported file, so at any later moment the diff between the two copies is one command.
  Files that have not diverged extract mechanically; files that have diverged are exactly the
  ones a shared library would have needed a decision about anyway.
* *Why it will not be forgotten.* It is the first entry under "Not today", and the two projects
  will pull toward extraction on their own: Lost exercises framework ordinals Mini Golf never
  touched, and a third title would make the case unarguable.

The namespace is `lost` (`lost::eapp`, `lost::platform`, `lost::game`, `lost::runtime`) and the
CMake options are `LOST_*`. A namespace called `minigolf` inside this tree would be exactly the
kind of thing the quality rules exist to prevent, so the rename is mechanical and total.

---

## Architecture

```
recomps/Lost/
  tools/
    funcs.py             build gen/funcs.json — the function table the emitter works from
    emit.py              ARM → C++ static recompiler (the pure recompilation)
    recomp/              the package behind them: image, functions, arm, cfg, cpp, generate
    progress.py          how much of the game is still recompiled rather than decompiled
  gen/                   GENERATED, never hand-edited
  src/
    runtime/             guest CPU state and memory, the eApp image, the frame pump
    framework/           what the game may ask the platform to do — graphics, audio, storage,
                         controls, device. Plain C++, one namespace each, no ordinals
    libeapp/             the iPod frameworks implementing those headers on the host
    game/                hand-decompiled game code; grows as gen/ shrinks
    platform/sdl3/       window, audio, keyboard (desktop)
    platform/null/       no I/O (headless test runs)
  tests/
    scripts/*.script     scripted input, the same format play uses
    expected/*.calls     framework-call logs recorded from the emulator — the first oracle
    unit/                stand-alone unit tests
  analysis/              the reverse-engineering evidence for this title
  reference/             frozen copies of the emulator sources this work cites, and PORTED.md
```

Everything about the guest machine — one flat reservation covering `0x11000000`–`0x40020000`,
`ld32`/`st32` through `memcpy`-based helpers, one `void f_1800xxxx(Cpu&)` per ARM function,
`goto` for intra-function branches, resolved jump tables as `switch`, `b .` as an assert trap,
a generated call table for indirect calls — is exactly as Mini Golf's plan describes it. That
design is proven over 2.9 million calls of oracle comparison on another title; it is not
re-litigated here.

**One change to the emitter, and it is a constant.** `tools/recomp/functions.py` carries
`GAME_CODE_START` and `RUNTIME_TAIL_START`, the two addresses that split armcc's C library from
the game's own code. For Lost those become `0x18000f74` (the first function above the last
import thunk) and `0x1803d8f0` (the first soft-float routine). Everything else in the emitter
is address-independent and needs no edit at all — which the survey already demonstrates, since
it walked Lost's control flow with Mini Golf's unmodified `recomp.cfg`.

**Ghidra is not a dependency.** Mini Golf seeded its function table from a Ghidra headless pass.
That machine no longer has Ghidra installed and the MCP server has no project open, so this
project seeds from the three entry vectors instead and lets `Generator.discover_all()` — which
already walks to a fixpoint, adding every call target it finds — do the rest. It found 736
functions that way. What that method *cannot* find is a function only ever reached through a
stored pointer, so live indirect-call targets from `play --callgraph-dump` are merged in exactly
as Mini Golf merged them, and `analysis/extra-entries.txt` holds the ones found by reading.
This is a better arrangement than the original, not a worse one: the tool chain is now
self-contained.

---

## Where Lost is not Mini Golf

This is the part of the plan that is actually about Lost. Seven differences, in the order they
will be met. Every one of them is documented in the emulator's loader
(`reference/eapp-loader/lib.rs`), which has already been made to run this title — that file is
the specification, and the citations below are to it.

**1. The render server must say yes.** `OpenGLES #152` starts the driver and returns 1 on
success. An unstubbed entry returns 0, and lib.rs records what that did to Lost: *"sat in a
present-only loop forever without ever issuing a draw call — it was being told, every frame,
that it had no renderer."* The four lifecycle entries `#152` start, `#153` stop, `#159`
select-pipeline and `#164` set-image all signal success as 1. Mini Golf never called any of
them. This is the first thing that will go wrong and it will look like a black screen.

**2. Lost's only matrix path is 16.16 fixed point.** `#149 glUniformMatrix4xvAPPLE` has eleven
call sites and `#125` (the float form Mini Golf used) has none. Lost never calls the `mat4`
helpers `#165`/`#167`/`#175` either, so nothing builds a matrix on the stack the way Mini Golf's
`glOrtho` did. The Y-direction — the sticky `proj_flips_y` flag that once made Mini Golf render
upside down out of stack garbage — has to be read out of `#149`'s fixed-point value instead.

**3. Every quad is tinted.** `#147 glUniform4xAPPLE(location, x, y, z, w)` is the *scalar* form,
five arguments with the fifth on the stack, all 16.16 fixed, and Lost calls it once per draw
block. lib.rs is blunt about the consequence of skipping it: *"dropping it would paint every
tinted Lost quad white — the same fault that buried Zuma's art."* Mini Golf's rasteriser has no
concept of a constant colour register; it gains one.

**4. Buttons arrive as event-list nodes, not as a flags word.** `play` says so on start-up for
this title: *"no button flags word for this title — buttons go via the event list"* and *"no
press-time words for this title — a held Menu cannot be told from a tap."* Mini Golf polled a
flags word at `0x180379f8+0x14`. Lost walks a linked list at `ctx+0x30` whose nodes carry a
type and a state (1 press, 2 release), and a node that is published and never retired reads as
a fresh press every frame — the bug that once made the Sims titles click Select forever. The
input framework and `src/platform/`'s seven actions do not change; what changes is how
`libeapp`'s `InputEvents #0` delivers them.

**5. There is a music library.** Lost imports 152 `Metadata` ordinals and uses at least `#62`
(the now-playing playlist) and `#134` (how many tracks it holds). lib.rs establishes the one
non-obvious constraint: Lost samples `#134` either side of an `Audio #40` registration and
returns `count - 1`, *"so the count must GROW by one per registered stream — a constant makes
the caller answer -1, its failure value, forever."* The rest of Metadata is answered as an
empty music library, which is a real device state rather than a placeholder. Mini Golf imported
none of this.

**6. Files come back through open-as-load.** Lost hands `AsyncFileIO #3` a 512 000-byte buffer
and never issues a read; the open *is* the load, and the completion's second result word is the
byte count, not a handle. The emulator has a `--load-on-open` flag for exactly this and it is
off by default. `libeapp`'s async file layer has to answer Lost's opens that way, and get it
right the first time, because the failure mode lib.rs records is the game exhausting its own
request pool and dying.

**7. The soft-float library is live.** Mini Golf reached exactly one soft-float routine and hand
-wrote it. Lost reaches eleven, and they are the `mrs`/`msr` FPSCR-emulating tail of armcc's
library. Each becomes a hand-written body in `src/runtime/arm_runtime.cpp` over the host's own
IEEE arithmetic, listed in `arm_runtime.json` so the emitter leaves it alone — the mechanism
already exists, there is simply more of it. That Lost uses floating point at all is itself worth
noting: the geometry may not be pure 16.16 the way Mini Golf's was.

---

## Schedule

Each block ends in something runnable *and* reviewed against the quality rules. If a block
overruns, the fallback is named. Cutting quality is never the fallback.

### 0 · 30 min — scaffold and provenance
`README.md`, `.clang-format`, `.gitignore`, `CMakeLists.txt` with the same three warning
regimes (`lost_common` / `lost_strict` / `lost_generated`), the port of `tools/recomp/` with
the two constants changed, and `reference/PORTED.md` written by a script that records source
path and SHA-256 for every ported file. `tools/funcs.py` seeds from the vectors and prints the
counts; expect 736 functions ± the ones only reached indirectly.

### 1 · 45 min — emit
Run the emitter over Lost. It should mostly work — the survey already proves the decoder and
control-flow analysis handle this image. What will need attention: the eleven soft-float
entries (block 2), any jump-table idiom armcc used here and not there, and the
`bl`-target self-check, which aborts on any call to something that is neither a known function,
a thunk, nor a runtime entry. Compile `gen/` under `-Wall -Werror`.

*Fallback:* stub anything that will not emit as `assert_trap` and keep going. An unreachable
stub costs nothing until it is reached, and then it says so with an address.

### 2 · 45 min — runtime and the soft-float library
Port `src/runtime/`. Identify the eleven soft-float routines by their call sites and their
shape (`_fadd`, `_fsub`, `_fmul`, `_fdiv`, the comparisons, the conversions) and hand-write
each over host `float`/`double`. **Checkpoint A:** the headless build runs the start-up vector
and the first frame with every ordinal logging and returning 0.

### 3 · 30 min — the oracle
Record `tests/expected/*.calls` from `play` with `--call-log`. The live emulator already has
the flag, so unlike Mini Golf this needs no change to the emulator tree at all. Record short
cases: a boot log is 300 MB at 13 000 frames, so cases are a few hundred frames and stop.
Port `tests/diff.py` and `diff.sh` — semantic comparison by default (arity from
`imports.json`), exact under `--exact`.

*Note on the recordings:* they must come from a pinned copy of the emulator, not the live tree,
for the reason Mini Golf learned the hard way — the live tree changes under other people's work
and stops reproducing its own recordings. `tools/oracle-emulator/` gets a pinned copy today or
the recordings get an expiry date.

### 4 · 3 h — `libeapp` for Lost's 42 ordinals
In boot order, re-running `tests/diff.sh boot` after each group:
1. miscTBD, Settings, InputEvents — with the event-list button delivery of difference 4.
2. AsyncFileIO with open-as-load (difference 6).
3. Metadata as an empty library with a growing stream count (difference 5).
4. Audio.
5. OpenGLES: the render-server lifecycle (difference 1), the fixed-point matrix path
   (difference 2), the constant-colour register (difference 3), then textures, attributes,
   `#37 glDrawArrays` into the ported rasteriser, `#157` present.

**Checkpoint B:** `tests/diff.sh` passes for the whole of a scripted case — the recomp makes
the identical sequence of framework calls the emulator makes. *This is the moment the recomp is
proven, before a single pixel is visible.*

### 5 · 1 h — SDL3
Port `src/platform/`. Window, keyboard through the portable binding table, audio streams for
the sound banks. **Checkpoint C, the end-of-day demo:** Lost on screen, natively, keyboard
driven, at the name-entry screen the emulator reaches — and past it.

### 6 · remaining — the first hand-decompiled functions
Prove the swap loop the same way Mini Golf did: the string helpers, then whatever the boot path
makes unavoidable. Each swap is diffed against the oracle. 736 functions is the long tail, not
today.

---

## Risks, in the order they will bite

1. **The render server (difference 1).** If `#152` is answered wrong the screen is black and
   nothing else about the graphics can be debugged. It is first in block 4 for that reason.
2. **The tint register (difference 3).** Failure mode is a white screen full of correctly-shaped
   quads — which looks like a texture bug and is not one.
3. **Oracle log size.** 2.9 M calls in a 13 000-frame boot. Scripts must be short and must
   `quit`; a case that runs long produces a file too big to diff usefully.
4. **The emulator is a moving target and an imperfect oracle.** Both problems have the same
   answer: pin a copy under `tools/oracle-emulator/` before recording anything, and treat a
   divergence as a question rather than a verdict until the emulator's own behaviour at that
   point has been read in lib.rs.
5. **Floating point.** Recompiled ARM integer code is bit-exact by construction. Soft-float
   replaced by host float is *not* bit-exact in every rounding corner, and if Lost's geometry
   depends on it, the exact oracle will show it. If that happens the answer is to recompile the
   soft-float bodies properly rather than to widen the tolerance.
6. **63 225 instructions of generated C++** is 2.7× what the Mini Golf build compiles, and that
   build was already the slow part. Watch the compile time in block 1; splitting `gen/` across
   more translation units is the lever.

---

## Not today, written down so it is not forgotten

* **Extract the shared core.** `recomps/common/` — runtime, framework, platform, tools — with
  Mini Golf and Lost as two thin titles on top. `reference/PORTED.md` is the work list.
* Music. Lost ships sixteen tracks, and eight of them are `.m4a`. Mini Golf's answer was an
  `afplay` child process, isolated behind `MusicPlayer` and honestly labelled a macOS crutch.
  The same crutch, the same label.
* **`lost.sav0`'s checksum.** `options.sav`'s is the sum of its bytes from offset 6; the saved
  game's is not that, nor a byte or word sum over any contiguous range of it (searched: all starts
  0..80, all ends, against two samples). Until it is known, this program cannot write a saved game
  the game itself will accept — which is why `src/game/cheats.cpp` works on the game's own state
  and not on its save, and why there is no "jump to chapter N *and keep it*" cheat.
* The other 387 imported ordinals. Lost imports 429 and a silent boot uses 42; the rest arrive
  as the game is played further.
* Ghidra, when a project is open again — not for the function table, which no longer needs it,
  but for decompilation and struct recovery, which is where it actually earns its keep.
* Platforms beyond macOS. Mini Golf's port to the Switch found 40 real portability defects in
  shared code; that work is already paid for and this tree inherits it.

---

## Progress log

*(Appended as work happens. Entries are dated and say what was established, not what was
attempted.)*

### 2026-08-25 — blocks 0–5 done; the game runs natively and both oracles are exact

**Where it got to.** `./build/lost --gamedir=…` opens a window, boots Lost, and plays it from the
keyboard. Two oracle cases pass, and they pass in the *exact* mode — every logged register and
every stack word behind it, leftovers included, identical to the emulator's recording:

| case | calls compared | what it does |
|---|---|---|
| `boot` | 84 810 | 401 frames, no input: load, title, the name-entry screen |
| `name-entry` | 115 630 | the same, then two wheel gestures and two Select presses that type `AA` |

`ctest` is 10 for 10 (six unit tests, each case semantically and exactly). `tools/progress.py`:
**0 of 789 functions decompiled** — the whole game is still the recompilation, which is exactly
what "day one" was supposed to mean.

**Block 0 ✅ the port.** `tools/port-from-minigolf.py` copied 72 files from the Mini Golf recomp
and rewrote their names; `reference/PORTED.md` records the source path and SHA-256 of each.
`--check` says 27 of the 72 have since been edited here and 45 have not, which is the extraction
work list for the shared core that "Not today" promises. Two of the 72 were deleted again (see
block 2). `reference/MANIFEST.md` pins the emulator under `tools/oracle-emulator/`; unlike the
Mini Golf recomp's copy it needed **no added instruments** — `--call-log` was already there — so
nothing in the emulator tree had to change for this project at all.

**Block 1 ✅ the emitter, and Ghidra out of the loop.** Ghidra is not installed on this machine
and its MCP server has no project open, so the function table is seeded from the three entry
vectors instead and `Generator.discover_all` walks to a fixpoint. That alone reaches 747
functions. Two more seed sources close the gap the walk cannot: the live branch edges from
`play --callgraph-dump`, and — new here — **every word in the image that reads as a pointer into
the code**, kept only if the walk succeeds on it. That last one was not optional: the first
headless run died on `indirect call to 0x1803d0f0, which is not a function entry`, and three of
the fourteen candidates it finds are literal-pool constants that happen to land in the code
range, which is why the walk is the filter. Final seed: 685 entries, 789 functions emitted,
65 423 instructions.

**Difference 7 turned out better than planned.** The plan said hand-write the eleven soft-float
routines the decoder could not translate. What they all fail on is `mrs`/`msr` on the *flag
field* — the pair armcc wraps its arithmetic in to save and restore the condition flags — and
that is exactly modellable: `mrs` composes N/Z/C/V with the mode bits, `msr CPSR_f` writes them
back. Forty lines across `recomp/arm.py`, `recomp/cpp.py` and `runtime/cpu.h`, and all eleven
recompile like everything else — bit-exact, where hand-written IEEE arithmetic would only have
been nearly so. `msr` to any field but the flags is a decoder error rather than a silent no-op,
because a recompiled program has no processor mode to change. `tests/unit/cpu_test.cpp` pins the
round trip, including the bits 27–24 the field carries and this core does not otherwise use.

**Block 2 ✅ runtime.** `src/runtime/arm_runtime.{h,cpp}` — Mini Golf's two hand-written
routines — are deleted: this title needs neither, and an empty file is not an extension point.
`arm_runtime.json` stays, because it is both the file that would name such a routine and the
thing that decides which functions count as ARM C library rather than game; the generated
bindings now include its header only when it names something. **Checkpoint A** (the init vectors
and one frame, every ordinal logging and returning zero) came out byte-identical to the emulator
for its first sixteen calls, once one thing was fixed: **vector slot 1 is the terminate entry**,
and calling it at start-up runs the whole shutdown path before the game has drawn. `EAppImage`
now keeps the header slot each vector came from so the slot can be named rather than counted.

**Block 3 ✅ the oracle.** `tests/record.sh` makes a recording with the pinned emulator and the
flags a case is defined by; `tests/game-dir.sh` gives every run a freshly cloned copy of the
game's folder, because the game writes into it and what it wrote last time changes what it does
this time. Recordings are short on purpose: an unbounded boot is 2.9 M calls and 300 MB.

**Block 4 ✅ libeapp — and this is where the seven differences were paid for.** In the order they
actually bit:

1. *Open-as-load* (difference 6). The first divergence, at call 18: `OpenGLES #153` was handed 1
   where the emulator handed it `0x19a3c` — the size of `rserver.bin`. Lost's opens *are* loads:
   the request carries the buffer, no read is ever issued, and the file object's result word is
   the byte count rather than the handle. The game passes that straight to `#164` as the render
   server's image size, so a handle there tells the driver its firmware is one byte long.
2. *The frame reason byte* (not in the plan, and the biggest single find). The next divergence
   was at frame 2, where the game restarted its renderer instead of polling input. `[ctx+0]` is
   not a state the firmware sets once: Lost's frame loop reads it every frame and branches three
   ways — 1 run a frame, 5 (re)initialise, anything else shut down. Left at the 5 the init call
   is seeded with, the game re-runs its init path every frame and tears the level down again as
   it goes. Zero on the first frame after initialisation, 1 thereafter. That one line took the
   agreement from 18 calls to 2 282.
3. *Metadata* (difference 5). Call 2 283: `Metadata #134` wanted the now-playing playlist's track
   count, and it has to **grow by one per registered stream** or the game reads its own failure
   value forever. `src/framework/music_library.h` is the new framework — the iPod's music
   library, answered as an empty one, which is a real device state. Its playlist block is
   reserved from the game's heap at the same moment the emulator reserves it, because the
   addresses either side of it are compared. After that: **84 810 of 84 810 calls identical.**
4. *Where the wheel poll writes.* The exact comparison then found one stale stack word,
   `0x4000003d` — an event-present bit and a wheel position, written four bytes into the wrong
   one of the poll's two out-parameters. Mini Golf's offsets happened to coincide; Lost's do not.
   Fixing it exposed the next one: nothing refills the wheel queue any more. A sample per frame
   is a finger resting on the wheel, and this game reads that as continuous movement; an empty
   queue answers zero, which is the release that ends a gesture.
5. *Buttons as event-list nodes* (difference 4). This title has no button flags word and no
   press-time words, so all of Mini Golf's button machinery went. A press is a twelve-byte node
   published as the whole list at `ctx+0x30`, state 1 for down and 2 for up, with a wheel sample
   posted alongside it — the game only looks at the list on a frame whose input flags a poll set,
   so a button pressed while the wheel is still is never read at all. A node lives for the frame
   that posted it and comes off at the start of the next; leaving it one frame longer put every
   button dispatch a frame late, which is how the lifetime was pinned (the wheel's gestures
   landed on the recorded frame and the button's did not — 443 against 442, twice).
6. *The render server* (difference 1) and *the fixed-point matrix* (difference 2) went in without
   incident: `#152/#153/#164` answer 1, `#149` reads a 16.16 matrix and takes the Y direction
   from it.
7. *The tint* (difference 3) was the one the call log could not see. `#147` is implemented, and
   then applying it as a general tint painted the whole name-entry screen green. The driver's
   combiner leaves an ordinary texture's colours alone; the constant colour is the *ink* only for
   an alpha-only texture, and only when no colour array is supplying a primary colour instead.
   A draw whose register is all zeros is skipped whole, since it contributes nothing under any
   reading of the combiner.
8. *Two texture units.* `glActiveTexture` is implemented, and a bind to unit 1 no longer replaces
   the texture the next draw samples. Nothing in the boot path exercises it — the framebuffer
   hash did not move — so this one is **correct by construction and not by measurement**.

**And then the frames were compared, which is the only reason three more faults were found.**
The call log cannot see what a draw drew, and by eye the name-entry screen was already right.
Hashing it said otherwise: **18.9% of its pixels differed** from the emulator's, worst channel
delta 110. A difference map put every one of them inside the *text* — the background panel was
already exact — and three rules of the emulator's rasteriser turned out to be missing from the
port, all three documented in `lib.rs` and none of them optional:

* A **1:1 blit samples the nearest texel**, not a bilinear blend. Text is laid out at fractional
  positions, so every glyph was sampling between texels and softening; while it moves, that
  offset changes continuously and it smears.
* **The colour is filtered weighted by alpha.** These titles key transparency with magenta at
  alpha 0, and straight bilinear averages that magenta into every keyed edge.
* **A texture modulates the fragment's colour, it does not replace it.** The two agree whenever
  the primary colour is white, which is nearly every draw, and differ exactly where a title puts
  its ink colour in the vertex array and draws a white glyph through it.

With those three: **0.30% of pixels differ, worst delta 1** on the two static frames — rounding,
and nothing else. The third comparison, at frame 150, differs on 316 pixels in one small patch
mid-animation, which is a frame's worth of timing rather than a rendering fault, and is left as
such rather than explained away.

*(Mini Golf's rasteriser has the same three gaps. It never noticed, because its own art is drawn
white and its text is not laid out at fractional positions. That is one more entry for the
extraction work list.)*

**Block 5 ✅ SDL3.** No work needed beyond the port: the window, the key bindings, the audio and
the macOS settings panel came across whole. The scripted run through the SDL build types `AA`
into the name field, which is **Checkpoint C**.

**What is not established.** The two-unit texture fix (above). The 316-pixel patch at frame 150.
Whether `Menu` and `Previous` are the right way round: the event types the emulator posts for
each of its buttons are reproduced exactly, and the recordings were made with them, but what the
*game* calls type 1 and type 3 is not known here — a swap would be a guess, not a fix. And
nothing beyond the name-entry screen has been played: the two cases reach it and stop.

### 2026-08-25 (later) — "a lot of graphical errors", and the oracle that could not see them

Reported after actually playing it. Reproduced in one run: past the menus, **every 320×240 scene
background rendered as colour noise** — the jungle, and by extension every location in the game.
The call log for those frames was identical to the emulator's, to the word, on both tiers.

**The fault.** `glCompressedTexImage2D` hands over an OES paletted texture, and the format
argument says how big one palette entry is. The uploader ignored it and assumed four bytes
always. Lost uploads in two formats — `0x8b96` (RGBA8, four bytes, which is what the assumption
happened to be) and `0x8b97` (R5 G6 B5, **two**) — and 21 of its textures are the second kind.
A two-byte palette read as four bytes decodes every colour through the wrong lens *and* starts
the index array 512 bytes late, which is not a wrong colour. It is noise. All ten OES paletted
formats are decoded properly now, entry size and colour both, and the game's own texture samples
match the emulator's byte for byte on every upload.

**The real lesson is the second one.** Two faults today were invisible to the call-log oracle and
were found by looking at frames — this one, and the soft text of the morning. They had to be,
because of what a draw call *is*: the framework is handed an address and a count, and what lives
at that address is never an argument. A texture decoded through the wrong palette, a quad in the
wrong place, a colour key left in: none of it changes a single word of the log.

So there is a third oracle now. `tests/frames.sh <case>` runs one script through the recomp and
through the pinned emulator and compares the pictures, and CMake makes a test of every script
that takes a screenshot. Nothing is stored — both sides are run — because screenshots of the
game are the owner's art and a stored reference is only as good as the day it was taken.

It compares with a threshold rather than a hash, and the threshold is the interesting part. The
two rasterisers are the same algorithm written twice, in Rust and in C++, and about 0.3% of a
frame differs by exactly one, which a hash would fail on while telling you nothing. A real fault
is neither small nor few: this one was 72% of the frame at a delta of 255. The default — 1% of
pixels differing by more than 8 — sits in the gap. **Verified by putting the bug back**: with the
palette entry size forced to four again, `frames_jungle` fails on four of its five frames at
72.38%; with it correct, all five pass with a worst case of 0.32%. A test that has never been
seen to fail is not known to test anything.

`LOST_TEX_LOG=1` prints one line per texture upload with three sample texels, in the same shape
the emulator prints under `EAPP_TEX_FMT_LOG=1`, which is how "do our textures decode the same?"
became one command rather than an afternoon.

**What is left, and it is one thing.** The click-wheel tutorial overlay — the disc with MENU and
the arrows on it — comes out olive-green here and bright pink in the emulator, about 9% of that
frame. It is not caused by any of this morning's rasteriser changes: reverting each of them in
turn leaves it unchanged, and the texture it samples decodes identically on both sides. What
differs is the fragment combiner, and that is chosen by the render server's pipeline index
(`#159`, which varies from draw to draw — 1, 13, 38, 39 in one frame) whose meaning is baked
into `rserver.bin` and is modelled by neither renderer. The emulator's pink has the look of its
own documented colour-key trouble and ours has the look of the game's own palette, but **neither
is verified against hardware and this note is not a claim that ours is right.**

### 2026-08-25 (later still) — where the game's files live

Until now the build ran straight out of whatever `--gamedir` pointed at, which is fine for a test
and wrong for a person: **the game writes into its own folder**, and so does this program. Three
files had already appeared in the reference copy — `options.sav`, `lost.sav0` and `settings.txt` —
from runs made while getting the thing on screen. They have been moved into the installed copy
(the two saves were empty, the settings were the defaults), and the folder is back to the 85 files
it shipped with.

The Mini Golf recomp's arrangement is now this one's, ported through the same provenance tool
(`--only`, added for the purpose, ports a subset and keeps the rest of the manifest):
`src/gamedata/` — a manifest of every file as shipped with its size and CRC-32, verification on
every launch, and a first-run install — plus `tools/manifest.py` to generate the table. The
installed game, its saves, its settings and its key bindings all live together under the per-user
data directory, and the reference copy is never touched again.

**With one difference, which is what was asked for.** Mini Golf asks for a *zip*; this asks for
the **folder**, because that is what a player has — they copied `1B200` off an iPod. So
`Platform::choose_directory` joins `choose_file` (SDL3's `SDL_ShowOpenFolderDialog`; a platform
without one inherits the default and answers no), and `install_from_directory` joins
`install_from_zip`. The picker asks for the folder first and falls back to the zip where the
browser cannot choose one. Either way the source is only ever *read*: the checked contents are
copied, and a player who points at the folder above `1B200` gets what they meant.

Both installers now share `write_game_files`, which is called only once every file has been read
and matched — the rule being that a refused install must leave no half-installed game behind, and
that rule is worth having in one place rather than two.

`tests/unit/install_test.cpp` covers it against a manifest of its own two files: installs from
the game's folder and from the folder above it, refuses a damaged source *and writes nothing*,
and verifies a folder the game has since written its saves into. It checks first which manifest
the linker gave it — its own lives in this file, the generated one in a static library — because
that arrangement works by a rule about archive members and would otherwise fail as five confusing
test failures rather than as itself.

`tools/manifest.py` leaves out everything written *into* the folder rather than shipped in it:
Finder droppings, `settings.txt`, `bindings.txt`, and `*.sav`, `*.sav0`, `*.sav1` — Lost numbers
its save slots into the suffix, which the Mini Golf version's plain `.sav` rule would have missed.

One test failed once during all this — `frames_jungle`, on a run made while an interactive
`./build/lost` was still open — and has not reproduced in three full runs since. Unexplained is
unexplained, so it is written down rather than waved off; what *was* clearly worth hardening is
the emulator's screenshot path, which is a shared `/tmp/ipod-shot-NN.png` that any other run of it
can write to. `tests/frames.sh` now counts what it finds against what the case asked for and says
so, rather than comparing frames that may not be the same frames.

### 2026-08-25 (last) — the wheel has two gestures, not one

Asked for: *Touch Up*, *Touch Right*, *Touch Bottom* and *Touch Left* as bindable controls, on the
keys that go with them. What that turns out to be about is a distinction the input layer did not
have. Turning the wheel and resting a finger on it are different gestures, this game uses both,
and neither can be built out of the other: a turn is a *change* of position and a touch is a
position, so holding one side reports the same place every frame and moves no menu at all.

`Action` therefore goes from seven to eleven, and `FrameInput` gains a `WheelTouch` that is a
**level** rather than an edge — the character walks for as long as the finger stays there, so
SDL reads the keyboard's state each frame instead of watching for a key-down. `hold <side>
[frames]` joins the script actions, spelled as the emulator spells it so a case runs on both.

**Two things had to be got right, and both were already measured.** A held finger *clears* the
queue before asserting its position: samples still queued behind it are from somewhere else, and
left there the game reads the difference as the wheel turning. And the four sides are not where
compass order puts them — the position byte starts at three o'clock and runs counter-clockwise,
so they are right 0, top 64, left 128, bottom 192. Both come from the emulator, whose note says
an even compass-order assignment came out with top and right transposed and left and bottom
transposed. That note also says why the byte and not the detent: `wheel_byte` maps 120 detents
onto 320 byte units, a turn and a quarter, so quartering the detent space gives four positions
176, −80 and −80 apart, which is not four cardinal points.

**Verified by the game itself.** `tests/scripts/walk.script` clears the opening dialogue, reaches
the tutorial — which draws the wheel with its lower quarter lit and says TOUCH THE LOWER SIDE OF
THE WHEEL TO MAKE JACK MOVE DOWNWARDS — holds the bottom, and Jack walks down the screen onto the
beach while the game answers **EXCELLENT!**. That is the game confirming the direction rather than
this project asserting it, and the picture oracle compares both frames against the emulator. No
screenshot is taken while the tutorial's wheel is on screen: that overlay is the one thing the two
disagree about, and comparing there would measure a known open question instead of this feature.

**The defaults, and the cost of them.** The arrows go to the four sides. An input does exactly one
thing, so they had to come off Scroll left and Scroll right, which start on `,` and `.` — stated
here because it is the one part of this that a player might reasonably want the other way round,
and it is two menus in Settings ▸ Input to change. The bindings file goes to format 4, so nobody
keeps arrow keys that no longer turn anything.

The settings window works out its own height from the number of controls now, rather than
carrying the 380 points that fitted seven of them. **Checked by arithmetic, not by eye** — 32 +
30 × 13 + 16 = 438 points of pane, 462 of window, the last row ending 26 points above the inset —
because this project has been caught before trying to photograph a window on a Mac somebody else
is using.

`tests/unit/input_bindings_test.cpp` covers the four new actions, their stable file names, and the
rule that giving an arrow to a touch takes it away from the turn that had it. Its sample bindings
file now takes the format line from what the build writes instead of naming a version, since the
version moves whenever a default does and that case was never about the number. ctest is 14/14.

### 2026-08-25 (sound) — the game's own banks, and a file name that was never there

Reported as `sound: cannot load …/soundbank_generic.dat: Could not find RIFF or WAVE identifiers`.
Correct, as far as it went: that file is not a Waveform file and nothing was ever going to load it
as one. The question is why the sound path was looking at it.

**It was Mini Golf's mechanism, carried over.** That game keeps its effects as `.wav` files in
`cNNbank/` and its sound handles are resolved by *name* — `Audio #0`'s index becomes
`<course>bank/<index>.wav`, and a handle with no such file falls back to "the Nth `.wav` this game
has opened". Lost has no `.wav` anywhere in its 85 files. So every handle fell through to that
fallback, the Nth file it had opened was a `soundbank_*.dat`, and the name went to `SDL_LoadWAV`.

**What Lost actually does is simpler, and it was already telling us.** It reads its own banks and
describes each sound to the framework, field by field, before playing it — one call each:

    Audio #7  (voice, 0x1919b5ce)   the PCM buffer, in guest memory
    Audio #8  (voice, 0x111dc)      70 108 bytes of it
    Audio #10 (voice, 0x5622)       22 050 Hz
    Audio #11 (voice, 1)            one channel
    Audio #12 (voice, 0x10)         sixteen bits
    Audio #2  (voice, 1, 0x3fff)    play it

Every one of those numbers is in the call log and was being logged and discarded. So the fix is
not to parse anything: `SoundRequest` now carries the **samples** and the format to read them in,
`Platform::play_sound` takes a `SoundClip` and the game's own voice handle instead of a path, and
the SDL voice opens its stream in whatever format the sound says. Nothing in the project knows the
bank format, and nothing needs to.

**The bank format anyway, since it was decoded on the way to the answer**, and it is what makes
the fields above credible: a fixed table of 64 records of 113 bytes (7 232 bytes, whatever the
bank), each holding a file name at +0, a symbol at +0x1e, the length at +0x5a, bits at +0x6b,
channels at +0x6c and the sample rate at +0x6d — then the raw PCM of every used record,
concatenated in order. `soundbank_generic.dat` is 16 records and its lengths sum to exactly the
472 792 bytes that follow the table. Pulling the first four sounds out by that layout gives peaks
of 65 to 5 713 at 1.6–2.6 kHz of zero crossings, which is audio and not a misread table — and
their lengths are the same numbers the game passes to `Audio #8`, which is the two readings
agreeing.

**What went with it.** The `.wav`-by-name scheme took a good deal of machinery with it, all now
deleted rather than left to rot: the read-extent table the audio layer used to map a buffer back
to a file, the list of opened `.wav`s, the course tracking that named the bank directory, and
`src/platform/wav.{h,cpp}` with its test — a decoder for a format this game does not use. The
port script records why each is absent, so a console port that needs the decoder back knows where
it went.

**Not verified: how it sounds.** Every play now arrives with a complete description — the seven in
a walk through the opening are 16-bit mono and stereo at 22 050 Hz, a fifth of a second each — and
the samples are real audio by the statistics above. Whether the mixing, the volume (`#13`) and the
looping are right is a thing to be *heard*, and that has not been done here.

### 2026-08-25 (the spin) — a flick is not a turn

Reported: spinning the wheel does not make Jack lift the object on the beach.

**What a scroll key was doing.** A press queued its eight detents, the poll drained one a frame,
and after eight frames the queue was empty — which is precisely how this program tells the game
that the finger has come off the wheel (`src/libeapp/input.cpp`). Holding the key did not help:
the key repeats arrived half a second apart, so a held key was a flick, a lift, a flick, a lift.
A menu cannot tell the difference, because a menu counts rows. Anything that wants a *sustained*
turn never saw one, and there is no way to make one out of flicks.

That is also why the emulator has a `--wheel-rotate` mode at all, and why the fault does not show
there in ordinary use: its scrolling comes from a trackpad, and a swipe across a trackpad
generates events for as long as it lasts, so contact never breaks.

**Now a scroll key does two things.** A press is still a flick worth one row. Holding one past
`SPIN_DELAY_FRAMES` — twelve frames, a fifth of a second — becomes a turn that keeps going: one
detent a frame, contact reasserted every frame, the queue never dry. The delay is longer than the
eight detents a press queues take to drain, so a tap is worth exactly one row and no more, which
is checked: three flicks at the name-entry screen still walk the highlight three letters. A rest
and a turn are the same finger, so at most one of them speaks per frame and the rest wins.
`spin <+|-> [frames]` joins the script actions, beside `hold`.

**Not verified: that this is the fault the report describes.** The tutorial gates movement until
it has been satisfied, and no script written here got past it to the object — a dozen attempts,
and the emulator sits in exactly the same place on the same script, which at least says the
scripts are wrong rather than this build. So the mechanism above is *established* — a flick ended
in a lifted finger, and now a hold does not — but that it is what stops the lift is reasoning, not
a demonstration. There is a second candidate with the same symptom, and it is this morning's own
doing: the arrow keys were moved to the four Touch actions, so scrolling is on `,` and `.` now,
and an arrow key pressed in the hope of spinning rests a finger on the wheel instead.

**And one thing that is not ours.** `error messaging the mach port for IMKCFRunLoopWakeUpReliable`
comes from macOS's input-method framework, which this program talks to because it asks SDL for
text input so that a name can be typed instead of spelled out on the wheel. It is a log line, not
a failure, and nothing in the program produces it: two scripted runs, one with text input started
and one without, printed nothing either way, so it needs a real keyboard and a focused window to
appear at all. Turning text input off would silence it and take typing with it.

### 2026-08-26 — pipeline 1, and what the constant colour is for

Two reports from the same screen: the meter fills when the wheel is spun and Jack never lifts the
object, and the meter and the top and bottom bars look wrong.

**The bars first, because they turned out to be the interesting one.** They are drawn from a
patch of the interface atlas that is *entirely transparent*, so this renderer dropped them on the
colour key and left whatever was behind showing through — the streaks of vegetation across the
letterbox. The emulator does exactly the same, so the picture oracle was quiet about it.

What draws them is `#159`'s pipeline 1, and the pipeline is the thing neither renderer had been
reading. Across a whole run through the opening, pipeline 1 is used for four things and nothing
else:

    mod=[0.00 0.00 0.00 0.62]  y   0..40    the letterbox bar at the top
    mod=[0.00 0.00 0.00 0.62]  y 224..240   the one at the bottom, behind the subtitles
    mod=[1.00 1.00 1.00 1.00]  y  39..40    the white rule along the edge of each
    mod=[0.00 0.00 0.00 0.25]  y 101..139   the dialogue panel

Every one a flat translucent fill, and every one sampling that transparent patch. So the first
reading was "pipeline 1 ignores its texture and fills with the constant colour" — and that is
wrong, which the name-entry screen says immediately: its brushed-metal panel is drawn through the
same pipeline with the register at an olive `[0.45 0.50 0.23 0.81]`, and ignoring the texture
washes the whole screen olive. That is the exact fault the emulator's own notes warn about, seen
from the inside at last.

What fits both is plainer: **the constant colour is a backdrop and the texture is composited over
it.** Where the texture is transparent the backdrop shows, which is the bar; where it is opaque
the texture does, which is the panel. One rule, both screens right: the letterbox is a dark band
with a white rule and legible subtitles, and the name-entry panel is brushed metal again.

**The oracle kept its full strength.** This is the first place this project has *deliberately*
drawn something differently from the emulator, and a comparison against it would now differ on a
third of every frame — the bars and the panels — which is precisely where a real fault would then
hide. So `--emulator-graphics` renders the backdrop pipeline the emulator's way, `tests/frames.sh`
passes it, and whole frames are still compared. It is the same device `--emulator-firmware`
already is: reproduce the reference's own shortcut rather than blind the test to it.

**The spin.** That the meter fills at all is new — it is the sustained turn from last night doing
its job, and the reason it was not enough is likely that it was a crawl. Reading the game's input
handler settles what it wants: at `0x180080fc` a still-touching frame dispatches a move event
carrying `previous - current` as a signed byte, so it reads the *distance moved since the last
poll* — the speed, not just the direction. One detent a frame is half a turn a second. A spin now
gathers pace as a thumb does, from one detent a frame up to six over a second of holding, which is
three turns a second and about as fast as a hand goes; starting slow is what keeps the first
moment of a held key readable in a menu, and a tap is still worth exactly one row.

**Not verified: that the object now lifts.** Nothing written here reached it — the tutorial gates
movement and a dozen scripts stalled short, in this build and in the emulator alike. The meter
filling says the rotation is counted; that six detents a frame is enough where one was not is
reasoning from what the handler reads, not a demonstration.

### 2026-08-26 (tearing) — nothing was ever waiting for the display

Reported: horizontal tearing when Jack moves, worst at 30 frames a second.

There was no vsync at all. `SDL_CreateWindowAndRenderer` does not turn it on and this platform
never asked, so every frame was shown the moment a timer expired — in the middle of a refresh as
often as not, with the seam between two frames across the window. At 30 frames a second on this
machine's 120 Hz display the two rates drift against each other, so the seam marches down the
picture instead of sitting still, which is why it was worst there.

**The fix took two goes, and the first one changed nothing.** Asking for a vsync *interval* —
present every Nth refresh — is the better answer where it works: exact pacing, no timer at all.
macOS's Metal renderer refuses it, which had to be measured, because the error says only "that
operation is not supported" without saying of what:

    vsync 1 -> ok                vsync 3 -> not supported
    vsync 2 -> not supported     vsync 4 -> not supported

The first attempt asked for interval 4 (120 Hz, 30 fps), was refused, and fell back to *no vsync*
— which is where it started. Now the interval is asked for first and plain vsync is the fallback:
presenting still waits for a refresh, which is the whole of what stops a tear, and the timer only
decides which refresh to land on. Measured afterwards: 301 frames in 10.40 s at 30 and 5.38 s at
60, both including start-up.

Unlocked keeps no vsync, because that is what it is for, and says so. The program prints which of
the three it got, once, on any change — "why does it tear" is a question that line answers, and
guessing at it is what cost the hour this was written in.

**Not fixed, because it cannot be at that rate:** 30 frames a second on a 120 Hz display shows
each frame for four refreshes, and that is judder rather than tearing. 60 is smoother for the same
reason it is on any machine.

### 2026-08-26 (saving) — the Mini Golf answer was the wrong one, and it was in the way

Asked: does Lost need Mini Golf's save path? **No — and that path was actively throwing this
game's saves away.**

Mini Golf saves through the store ordinals `AsyncFileIO #12/#14/#16`. Lost never calls one of
them: across every run made here it uses `#0`, `#1`, `#2` and `#3` and nothing else. It saves the
ordinary way — open, transfer, close — and three separate things stopped that working, each
inherited from the other game:

1. **A save was refused write mode outright.** Mini Golf's file layer treats a `.sav` name as
   read-only, on purpose: that game opens its save with mode 1 whether it means to read it or
   write it, and opening it for writing truncated it on the way to reading — so the special case
   made saving happen through the store calls instead. Lost has no store calls, so the special
   case left it with no way to write at all.
2. **Half its saves were not recognised as saves.** The match was on a `.sav` suffix, and this
   game numbers its slots into it: `options.sav`, but `lost.sav0`, `lost.sav1`, … So the options
   went to the platform's store and the saved games went loose into the game directory.
3. **A transfer only ever went one way.** The direction was taken from the operation code, and
   this game uses code 3 for every transfer in either direction — so a write into a file was
   read out of it instead, and the save stayed empty.

Now: a save is any `.sav` or `.savN`; a transfer goes whichever way the *file* was opened; and an
open still never truncates — the first transfer to arrive is what settles the file's length,
which is the rule that keeps "open with mode 1 to read it" from emptying a save. Saves go to the
platform's store either way, so a platform that keeps them somewhere unusual still gets them.

`tests/unit/save_files_test.cpp` is the proof, and it has a case per rule above: a save under both
spellings round-trips, an open with nothing to write leaves the save alone, and what was written
is in the store with the right bytes. Each of those three failed at some point on the way here.
ctest is 14/14.

**One thing this changes for the better on its own:** the game no longer leaves empty `options.sav`
and `lost.sav0` files behind on a first run. It opened both at boot and wrote nothing, and the old
path created them anyway; now nothing is written until there are bytes.

**Not verified: that the game's own save now survives a restart.** No script written here reaches
a point where the game writes one — it opens both files at boot, transfers nothing, and closes
them, and the emulator does the same, so there was nothing to compare against either. What is
established is that the machinery underneath does the right thing when bytes do arrive.

### 2026-08-26 (closing it) — SDL outliving nothing

Reported: closing the game crashes it. The report says the rest:

    SDL_DestroyAudioStream -> SDL_UnbindAudioStream -> pthread_mutex_lock
      Voice::~Voice()            sdl3_platform.cpp:100
      Sdl3Platform::~Sdl3Platform()   sdl3_platform.cpp:347

A destructor body runs *before* the members of the class it belongs to are destroyed, and
`SDL_Quit` was in that body. So every voice gave its audio stream back to a subsystem that had
already gone, and dereferenced freed memory doing it. Reproduced first, in one line: a scripted
run long enough for a sound to play exits **139**; the same run without one exits 0, which is why
this went unseen for a day — a voice that never opened a stream has nothing to give back.

**Fixed by ordering rather than by remembering.** The first version released the voices explicitly
at the top of the destructor, which works and would break again the next time something with a
destructor of its own joined the class. Instead `SDL_Quit` now lives in a member declared *first*,
which makes it the *last* thing destroyed: SDL outlives everything of SDL's that this class holds,
whatever is added later. The destructor body keeps only what nothing else would — the window, the
renderer and their textures, which are raw handles with no owner. Exit status is 0 with a sound
played and 0 without.

**Not guarded by a test.** Every case in the suite runs the headless build, which has no voices
and cannot reach this; a case that could would have to open a window and play for half a minute
inside `ctest`. The ordering above is the guard instead, which is why it was worth making
structural.

### 2026-08-26 (the bars) — decompiled, and it was never a blur

Asked: why do the top and bottom bars look wrong, and is the intended effect a blur of the
background? The game's own code answers it, and the answer is no — but the guess was a good one,
because what it *was* doing looks exactly like a blur.

**The function that draws them** is at `0x1803b5xx`, found from the call log: the constant colour
for those quads is set from `0x1803b694`. Read straight through, it is:

    1803b660  bl 0x1800723c   ; select a built-in pipeline
    1803b690  bl 0x180002b0   ; #147 the constant colour, from the object's own +0x44..+0x50
    1803b6d0  bl 0x18001650   ; copy a 64-byte four-vertex template
    1803b6d8  stm ...         ; overwrite the four corners with this quad's own
    1803b710  bl 0x18000288   ; #137 attribute 0 <- those vertices, four GL_FIXED components
    1803b718  bl 0x18000104   ; #40  enable attribute 0
    1803b728  bl 0x18007340   ; draw four vertices as a quad

The template at `0x1803ed08` is four copies of `(0, 0, 0, 1.0)`, so the two words the game does
not overwrite are z and w — a position, and no texture coordinates anywhere. **The draw points
attribute 0 and stops.** The letterbox bars, the white rule along each of them and the dialogue
panels are all this one function, 10 785 times in a run through the opening.

**And the enable flag is worthless here.** This game never turns an attribute array off — it does
not import `glDisableVertexAttribArray` at all — so attribute 1 is on from the first textured
draw of the run to the last. This renderer decided "textured" from that flag, so a bar was
textured through *the previous draw's* coordinates, smeared across the whole quad. A scene
smeared across a bar looks remarkably like a blurred copy of what is behind it, which is what it
was taken for. The emulator does the same, which is why the picture oracle was quiet.

**The rule the game does supply** is that it re-points every attribute it wants immediately
before every draw. The three textured paths (`0x18007e20`+`0x18007e48`, and two like them) point
attribute 0 and then attribute 1; the flat path points attribute 0 alone. So an attribute belongs
to a draw when it was pointed *since the last one*, and a draw with no texture coordinates takes
its colour from the constant register — which is what makes the bars the black-at-62% the game
asks for, with a white rule along each edge and the subtitles legible on top.

**One place this changes something I cannot check.** The name-entry screen's middle panel is drawn
by the same flat path with the register at an olive `[0.45 0.50 0.23 0.81]`, so it becomes an
olive veil over the character montage rather than the brushed metal both renderers showed before.
Both cannot be right, and the metal was arriving *by accident*: it was the stale coordinates of
the metal strips drawn immediately before it. That the accident looked plausible in one place and
absurd in the other is the whole reason this was hard to see. The olive is what the game's own
code asks for; whether it is what the screen looked like on an iPod is a question for someone who
has seen one.

### 2026-08-26 (the seams) — half a sprite, drawn twice

Reported: sprites look cut off, with a line down the middle of Jack, of his shadow, and of the
tutorial's click wheel. Reproduced here by magnifying that wheel: a dark column straight down its
centre, through MENU, the middle circle and the green wedge.

**The wheel is one half-image drawn twice.** Its vertices say so exactly:

    x 114.832 .. 159.766   u  0 .. 45     the left half
    x 204.700 .. 159.766   u  0 .. 45     the right half, mirrored about the same edge

45 texels wide, 90 tall, and the second quad runs backwards so the sheet holds only one side of a
symmetrical thing. That is why the seam is dead centre, and why it appears on anything drawn this
way — Jack, his shadow, the small wheel in the corner.

**And a bilinear tap half a texel past the edge of the half-image reads its neighbour in the
sheet.** The innermost pixel of each half interpolates towards u = 45, which is one past the last
texel this sprite owns, so both halves finish with a column of whatever was packed beside them.
Dark, in this case, and doubled by the mirror.

Sampling is now clamped to the corner of the sheet a draw actually names — GL's clamp-to-edge,
with the edge being the sprite's rather than the sheet's. The bounds come from the draw's own
coordinates: the three vertices of a triangle span the whole of its quad's texture rectangle,
because a quad is split along a diagonal whose ends are opposite corners. A full-screen background
whose coordinates cover the whole texture is unaffected, which is most draws.

The seam is gone from the wheel. `--emulator-graphics` restores the old whole-sheet clamp so the
picture oracle still compares whole frames; ctest is 14/14.

**Where to start tomorrow.** The tutorial overlay above — and the way in is `#159`, the pipeline
index, which nothing currently reads. After that, the first hand-decompiled functions: the exact
oracle is green on both cases, so the moment it goes red on a swap, the swap is what broke it.
And more picture-oracle cases, one per screen the game has; they cost a script each now.

### 2026-08-26 (Save and Exit) — the game had been asking to leave, and nobody was listening

Reported: in game, Menu, scroll to **Save and Exit**, and the SAVING screen never ends.
Reproduced first — `tests/scripts/save-and-exit.script` plays in, opens the pause menu, walks
sixteen rows right onto SAVE AND EXIT and presses it — and the pinned emulator hangs there in
exactly the same way, which is the first thing worth knowing: this was never a fault the picture
oracle or the call-log oracle could have caught, because both compare against a firmware with the
same bug.

**The save itself was already working.** The trace says so: three store groups in one frame,
`options.sav` 26 bytes, `lost.sav0` 12 524, and one more of 88, each `#12` open, `#16` write,
`#14` close, all answered OK, and the files on disk afterwards with the right sizes. Nothing was
stuck in the file layer at all. After that frame the game made *no* framework call it had not
made before — it simply drew the same frame for ever, byte for byte identical.

**What it was waiting for is the byte it answers in.** `ctx+0x100` is not a status the firmware
may read and discard. It is the reason the game is asking to be called with next, and the last
thing its frame vector does is copy it into the reason byte itself:

    0x1803d82c  ldrb r0,[r4] / strb r0,[r5]        ; r4 = ctx+0x100, r5 = ctx

so a firmware that leaves `[ctx+0]` alone honours the request by doing nothing. This loop wrote
its own reason over the top of it every single frame. Save and Exit ends with the game's own tick
returning 0 —

    0x1803d790  bl 0x180063b8 / cmp fp,#0 / strbeq sl,[r4] / strbeq r7,[r9,#7]   ; sl = 5

— which is the game saying *I am finished; call me back so I can shut down*. Called back with
reason 5 it tears itself down and answers 6, and the eApp is over. Both halves are now honoured,
and the run ends by itself at frame 3609 instead of drawing SAVING for ever.

**And a Menu tap had been asking for the same thing, four seconds later, all along.** Once the
answer is honoured that becomes fatal, so it had to be found before this could ship. The event
dispatcher reads a node's state byte as *press* (1) or *still down* (2) —

    0x18008010  cmp r7,#2 / movne r0,#0 / moveq r0,#1 / strb r0,[r8,#2]   ; menu_held = held

— and the game's own input handler at 0x18005f38 acts on 1 and on nothing else; every button it
knows tests `cmp r7,#1` first. So state 2 is not a release. Sending it to *end* a press told the
game Menu had gone down and stayed down, nothing ever arrived to clear it, and four seconds later
the game began asking to be suspended and never stopped. `[r9+7]` records which of the four sites
raised a request, and all four were watched: 3 four seconds after a Menu tap, 2 after twenty
seconds of nobody pressing anything, 1 on the frame after the save. A release is now neither of
the two values the dispatcher recognises, which clears the held flag and asks for nothing.

**Idle sleep is refused, on purpose.** Reason 2 is the iPod dimming and then sleeping when it is
left alone, and the game asks to be put away when it happens — measured at 600 frames to the
backlight and 1 200 to the request. A window on a desktop has no such policy and the player has
not gone anywhere, so that one request is declined; the game asks again every frame, harmlessly,
until the next input refreshes its activity clock. Verified by leaving the pause menu open for
5 700 frames: still there, still drawing.

**A third fault fell out of testing it: booting the game destroyed the save.** The save survived
Save and Exit, and the first launch afterwards overwrote it — 12 333 of its 12 524 bytes gone. The
file layer took a transfer's direction from the mode the file was opened with, and this game opens
a save with *write* mode to read it back at start-up. That was a fix for a fault that did not
exist: as above, a save is never written through this call at all, only through the store. A save
now opens to be read whatever mode it names. `tests/unit/save_files_test.cpp` had encoded the old
belief and is rewritten around the two calls the game actually uses.

**What is verified.** The route ends the program at frame 3609. `lost.sav0` is 12 524 bytes
afterwards and byte-identical after a boot. Starting the game with that save in place opens on
RESUME over the jungle rather than ENTER YOUR NAME — the whole round trip, not just the file.
`tests/save-and-exit.sh` is all three of those, and is checked against the game rather than
against the emulator, which cannot reach the end of this route. ctest is 15/15.

**Not verified.** That a *held* Menu still puts the game away, because the platform layer reports
button edges and not whether one is still down, so state 2 is never sent at all. Nothing else
sends it either, so the four-second path is now unreachable rather than wrong. `LOST_TRACE_FILES=1`
prints one line per file operation, which is how the store groups above were read.

### 2026-08-26 (the window, and tapping a menu) — one number meaning two things

**The window already held its shape, and ⌘, already opened Settings.** Both were in place;
`SDL_SetWindowAspectRatio` was measured enforcing 4:3 on this backend (asking for 900×500 gives
667×500), and Settings has both an SDL key handler and a `⌘,` menu item. What was in the way was a
saved setting: `whole-multiples 1`. It made the *picture* take the largest whole multiple of
320×240 that fitted and bordered the rest, so the game never filled the window and the margin grew
and shrank as the window was dragged.

That setting is now kept on the **window** instead of on the picture: with it on, a resize snaps
the window to a whole multiple, so the pixels are still exactly square and there is no border at
all. Measured: with it on, dragging to any size gives 640×480 or 1280×960 and the picture covers
100% of it; with it off, 667×500 and 100%. Full screen is the exception and still borders, because
there the size is the display's and not ours to round.

**Tapping ← or → now moves a menu, and that took finding out what a menu moves on.** Not the
position of the finger: the decoder at `0x18008074` subtracts the previous wheel position from the
current one, sign-extends the difference to eight bits and passes it to `f_18005f38(6, …)`, which
raises UI event 0x11 or 0x12. A finger resting on a side reports the same byte every frame, so its
delta is zero and no menu ever moved — which is exactly what was reported.

Raising 0x11/0x12 is not enough either. The row only changes when the position has travelled
further than a threshold the game reads per screen from `[app+0x1104c]`, and *then* it raises
0x0d/0x0e:

    0x18006054  cmp r1,r2 / ble skip          ; r1 = |distance since the last notch|
    0x1800607c  movlt r1,#0xd / movgt r1,#0xe ; one row back, or one row on
    0x18006090  str r4,[r9,#4]                ; and this is where the next one is measured from

That threshold was measured at 13, 20, 30 and 40 units on different screens — and 40 units of
position is 56° of wheel. Offsetting a resting finger by that much to make a menu move would point
the walk in a different direction, and moving it back afterwards notches the menu straight back
again, because the distance is measured from wherever the last notch fired. **One number cannot be
both where the finger is and how far it has turned.**

So the tap is turned into a flick *after* the finger lifts, where there is no walk left to spoil:
two samples 42 units apart, which clears every threshold seen and is over in two frames rather
than the sixteen that one-sample-per-detent would take. A press longer than ten frames is a walk
and sends nothing. Verified: three taps right walk the pause menu RESUME → RESTART → VOLUME →
OPTIONS, two taps left bring it back to RESTART, and holding ← still walks Jack and still satisfies
the tutorial that asks for it. Off under `--emulator-firmware`, so the recordings still match;
ctest is 15/15.

**Not verified.** That a tap during play never reaches a screen that reads 0x0d/0x0e for something
of its own — the gameplay screens tried ignore it, but every screen has not been tried. Holding a
direction in a menu does not repeat, and that is inherent: a held finger has no delta.

### 2026-08-26 (the bar again) — a stale attribute, and a draw that was prepared and abandoned

Reported: the bottom bar's colour changes as the player walks, and it looks like the assets on
screen are getting into it — random, and only in this build. Three screenshots: twice the bar's
dark fill simply missing, with the scene running straight through where it should be, and once
replaced by horizontal streaks of purple and tan.

**Streaks are the signature of a flat quad being sampled as a texture**, which is the fault the
bars work of earlier today was supposed to have ended. It had not. The rule that replaced the
enable flag was "an attribute belongs to a draw when it was pointed since the last one", and it is
right as far as it goes — but it never asked what happens to a setup that is prepared and then
*not* drawn. Attribute 1 stays marked, and the next draw inherits it. The bar points attribute 0
alone, so the bar is exactly what inherits it.

Reproduced from the player's own saved game rather than from a script that walks in from the
title, because it depends on what is on screen: at the beach camp, the black bar was drawn 923
times and **385 of them — 42% — came out textured**. Those are the glitched frames. Where the
inherited coordinates landed on a transparent patch the bar was keyed away to nothing; where they
landed on something else it was smeared across it.

The rule now has the missing half. Pointing attribute *0* starts a new draw's set and clears the
rest, because every path in this game points the position first and then whatever else that draw
needs — the three textured paths point 0 and then 1, the flat path points 0 alone. Nothing legal
is lost and the leftover cannot be read.

    before   pipeline 1: 3363 flat, 385 textured      the 385 are the glitch
    after    pipeline 1: 3748 flat, 0 textured
    pipelines 11, 13, 38, 39: every draw still textured, before and after

**Neither oracle can see this**, which is why it survived: `--emulator-graphics` decides texturing
from the enable flag, so the picture oracle never exercises the rule that was wrong. That is a real
gap in the coverage, not a quiet corner — the same shape of fault will hide there again.

**What is left, and is not a bug.** The bar is still the 62.5% black the game asks for itself
(`#147(4, 0, 0, 0, 0xa000)` from `0x1803b694`), drawn over a ground layer that covers the whole
screen, so it still takes a dark tint from whatever is behind it. Forcing it opaque was tried and
is wrong: the same pipeline draws the name-entry panel at olive `[0.45 0.50 0.23 0.81]`, and opaque
turns the character montage into a solid olive block. The alpha is genuinely a veil.

### 2026-08-27 (past the iPod) — a chapter lock that is one bit, and text that was never resolved

Three things asked for, and all three are departures from the device rather than corrections to
it, so all three are off unless someone turns them on: a **Cheats** tab whose one entry unlocks
every chapter, **dialogue text at the raster's resolution**, and a **render scale** above 320×240.
Settings ▸ Graphics holds the last two, Settings ▸ Cheats the first.

**The chapter lock is one bit, and finding it went the long way round first.** The obvious reading
was that a save records which chapters are finished, so the first two hours went into the save
format: `options.sav` is 26 bytes, `lost.sav0` is 12 524, and a third file called `stats` is 88.
Two of those gave up something. `options.sav`'s first two bytes are the sum of its bytes from
offset 6, which was proved by mutating a byte, fixing the sum and watching the profile load again
— and by the sweep either side of it, where every byte set to `0xff` was refused, so the fields
are range-checked as well as summed. `lost.sav0`'s checksum was **not** found: it is not a byte or
word sum over any contiguous range (all starts 0..80, all ends, both of two samples), and a
one-byte differential only shows that byte 4 is included with weight 1. That is where the save
route stopped, and it is the reason the cheat does not touch a save at all.

What worked was reading the menu. The strings are in the file `s` — a 13-section table, English
from offset 54, each entry NUL-terminated — and counting to `SELECT CHAPTER` gives index `0x8b`.
That constant appears **once** in 65 423 instructions, at `0x180367d0`, in the arm of a menu
dispatch that then loads `0x18040530` and `mov r2, #11`. Eleven items at `0x18040530` are the nine
chapter names `0x8c`..`0x94` and then BACK and SELECT.

The shape of a menu item fell out of the setup function at `0x18036118`, which for the PLAY
submenu looks up the items for strings 2 and 3 and clears a bit on one and sets it on the other —
and `--dump-frame=180405dc:20` at that screen prints `00000002 00020003 0000008b …`, with START
NEW GAME drawn and CONTINUE GAME not. **The low halfword is the string index, the high halfword is
flags, and `0x20000` is hidden.** At the chapter screen on a fresh profile:

    0000008c 0002008d 0002008e 0002008f 00020090 00020091 00020092 00020093 00020094 …

One bit, eight times. `src/game/cheats.cpp` clears it every frame — every frame, because the game
sets it again whenever it builds the menu — after checking that the nine low halfwords are still
`0x8c`..`0x94`, so a differently built image is left alone rather than written into. Verified by
picture as well as by dump: the menu lists THE ARRIVAL, FIRST TASTE, SURVIVORS… and selecting the
second loads a scene that is not chapter 1's. `tests/cheats.sh` pins both readings.

**The caption is not wrong.** "CHAPTER 1" stays under the first three entries because the game's
own table at `0x1803e778` is `1 1 1 2 3 4 5 5 5`: nine chapters in the menu, five Chapters in the
story.

**Text was being sampled bilinearly at 1:1, and nobody had noticed.** `is_one_to_one` asks its
question over a whole draw, and a line of dialogue is *one* draw — 484 vertices, 121 glyph quads,
each about 11×13 and 1:1 with its own cell of the sheet, but spanning the width of the screen
while their coordinates wander over the sheet. So the draw-wide test says "not 1:1", and every
glyph has been going through the bilinear path since day one. `is_text_run` asks it per quad
instead, which is also the only thing in this game with that shape.

Above 1× those glyphs are sampled with the coverage taken back to full contrast across one raster
pixel — `pixels_per_texel` is just `1 / texels_per_pixel`, which the rasteriser already had — so
the edge lands where the sheet says rather than being spread over the enlargement. Compared at 3×
against the plain path, the black outline goes from a gradient to a line.

**The render scale multiplies in exactly one expression**, `project`, and nothing else in the
rasteriser knows the game's coordinates from the raster's. Three consequences worth writing down:

* `is_one_to_one` had to start dividing by the scale, or every 1:1 sprite would have answered "no"
  above 1× and been silently blurred — the sharpness that test exists to keep, lost by turning up
  a setting that is supposed to add detail.
* `glCopyTexImage2D` averages the `scale`×`scale` block back down. The game names that rectangle
  and that texture in its own pixels and then draws with its own numbers; handing it the larger
  one would be a size it never asked for.
* The SDL texture is rebuilt when the size changes, and the Sharp prescale now measures whole
  multiples against *the picture it was handed* rather than against 320×240 — otherwise a
  4×-enlarged frame would be prescaled by the same factor again.

**Both are refused under `--emulator-graphics`**, which owns the renderer outright, because the
picture oracle has nothing to compare a differently sized frame with. That is a unit test rather
than a comment (`tests/unit/render_scale_test.cpp`): the settings arrive from a file the player
owns, and a recorded case must not depend on what is in it.

**What it costs**, over the same 2 100-frame run: 3.7 ms a frame at 1×, 14.4 at 2×, 32.4 at 3×,
57.6 at 4×. The rasteriser is scalar software, so every step costs its square; 2× is about where a
60 fps machine stops keeping up. Said in the settings window rather than decided for the player.

`ctest` is 17 for 17, the two new cases being `cheats` and `unit_render_scale_test`.

**Not verified.**

* That the Cocoa layout is right *by eye*. `graphics_pane_height()` adds up what `makeGraphicsPane`
  lays out and the arithmetic leaves the last note 24 points clear of the bottom, but the window
  has not been opened and looked at.
* That `is_text_run` catches every run of text in the game and nothing else. It was measured on
  the chapter cards and the in-game dialogue panel; a screen with a different text layout has not
  been tried, and the failure mode either way is cosmetic and reversible.
* That no *other* screen also reaches `0x18040530`. The cheat writes those nine words whenever it
  is on, and the guard checks the string indices, not who is reading them.
* Whether the render scale interacts well with `Scaling::Sharp` on a display whose output is not a
  whole multiple of the enlarged picture. The prescale arithmetic was corrected for it; the result
  has not been looked at.

### 2026-08-27 (the frame rate) — sixty milliseconds a frame, and where every one of them went

Reported: render scale 4 runs at about 12 fps on an M1 Max, which is not what that machine should
do. It is now **3.8 ms a frame — 260 fps — and the picture is byte-for-byte the picture it was**,
at every scale, on all three of the cases that take screenshots. Four things, measured one at a
time over the same 2 400-frame run (into the jungle and then standing there, 4.9 million fragments
a frame at scale 4):

| | ms/frame at 4× | |
|---|---|---|
| as reported | 60.1 | |
| `-O3` instead of `-O1` | 35.3 | ×1.70 |
| a span per scanline | 31.7 | ×1.11 |
| `Nearest` as one texel | 17.6 | ×1.80 |
| drawn on ten cores | 3.8 | ×4.3 |

**The first one was a default, not a bug, and it was the biggest.** `CMakeLists.txt` set
`CMAKE_BUILD_TYPE` to RelWithDebInfo and RelWithDebInfo to `-O1 -g`, deliberately, so that the
recompiled code stays steppable. That is the right configuration to *debug* a recompilation in and
the wrong one to hand everybody who follows the README, because this program's renderer is
software: there is no GPU to take the per-pixel work, so the optimiser's output is the frame rate.
Release is now the default and is `-O3 -g`; RelWithDebInfo is still `-O1 -g` and is still the one
to ask for when stepping. `assert` stays live in both — measured at 84.8 s against 85.8 s over the
same run, so there was nothing to buy by dropping it.

**A false start worth recording.** The first measurement said `-O2` and `-O3` were worth nothing at
all, which was wrong: `CMakeLists.txt` sets `CMAKE_CXX_FLAGS_RELWITHDEBINFO` as a plain variable,
so it silently overrode the `-D…` on the command line and all three "different" builds were the
same `-O1`. A flag experiment that changes nothing is a result about the experiment.

**Where the fragments were going.** A `sample` profile put essentially the whole frame in
`fill_triangle` — the recompiled game itself is about 0.2 ms a frame, which is worth knowing on its
own — and inside it, `sample_texture`. Two things followed:

* **A quad is two triangles and each one's bounding box is twice its area**, so half of every
  fragment's worth of edge arithmetic was being spent deciding not to draw it. Each edge function
  is affine in `fx` at a fixed `fy`, so a row's interval is three divisions rather than two per
  pixel. The interval is widened by a pixel at each end and *the original per-pixel test is kept*:
  this decides which pixels are looked at, never which are drawn.
* **`Nearest` was going through the full bilinear tap** — twelve multiplications and three
  divisions to combine four texels of which three have weight zero. It is now one texel, and that
  is the same answer rather than an approximation of it: the snapped weights are exactly 1 and 0,
  so the alpha sum is that texel's own alpha and each channel's `alpha × colour` is a product of
  two integers under 256, exact in a float and exact again when divided by that alpha. It matters
  because it is the *common* path — every sprite in this game is a 1:1 blit, and `is_one_to_one`
  asks in the game's own pixels, so raising the render scale does not turn any of them into
  bilinear taps.

**Then ten cores instead of one.** The raster is cut into horizontal stripes and each is taken by
whichever thread is free. Stripes are *taken* rather than dealt out — four per worker, one atomic
increment each — because this machine has eight fast cores and two slow ones and an even share
leaves the fast eight waiting on the slow two; 4 threads gave ×2.7, 8 gave ×4.0, 10 gave ×4.3.

The thing that makes this safe is that a pixel belongs to exactly one stripe, so the draws that
touch it still arrive in the order the game issued them, through the same arithmetic. That is not
an argument, it is a check: every screenshot of `jungle`, `walk` and `name-entry` at scales 1 and 4
is byte-identical to the same shot from the single-threaded build, and `ctest` is 17 for 17 with
the picture oracle. `--render-threads=1` starts no threads at all, which is how each of the numbers
in the table above was taken.

The texture is also looked up once per draw now instead of once per triangle — a line of dialogue
is 242 triangles and that was a `std::map` lookup each — which is also what lets the "texture never
uploaded" fatal be raised before any worker has been handed anything.

**Not verified.**

* Anything about a machine that is not this one. Every number here is one M1 Max; the shape of the
  curve should hold and the constants will not.
* That `-O3` is safe for `gen/`. The recompiled code is mechanical output and the oracles compare
  it call for call at `-O3` now, which is the strongest statement available, but it has not been
  stepped through at that level.
* Whether the stripe height and the 96k-pixel floor below which a draw is not shared out are near
  their best. They were chosen to be obviously safe rather than tuned.

### 2026-08-27 (5x to 8x) — the ceiling was arbitrary, so it moved

Asked for: render scales past 4. The limit was two constants — `MAX_RENDER_SCALE` in
`src/libeapp/gles.cpp` and in `src/platform/settings.h`, which have to agree and now both say 8 —
plus the places that quoted the old range. The settings window's menu builds itself from
`MIN_RENDER_SCALE`..`MAX_RENDER_SCALE`, so it grew four entries on its own.

Nothing else had to change, and that is the interesting part: the scale is applied in exactly one
expression (`project`), the framebuffer is a vector that is reallocated when it changes, the SDL
texture is rebuilt to whatever size it is handed, and `glCopyTexImage2D` averages back down to the
game's own pixels. 2560x1920 is 14 MB of framebuffer and Metal takes the texture without
complaint; the windowed build draws the same frame as the headless one, hash for hash.

Measured over the same 2 400-frame run, all ten cores:

| 4× | 1280×960 | 4.3 ms · 235 fps |
| 5× | 1600×1200 | 5.6 ms · 178 fps |
| 6× | 1920×1440 | 7.8 ms · 129 fps |
| 7× | 2240×1680 | 10.8 ms · 93 fps |
| 8× | 2560×1920 | 13.2 ms · 76 fps |

So 8× still clears 60 fps on this machine, which is the reason it is the ceiling rather than 6 or
16: it is the largest whole multiple that a fast machine can still pace, and past it the honest
answer is that the game's art is from 2008 and only the geometry's edges and the glyphs are still
getting sharper. `ctest` is 17 for 17.

**Not verified.** Anything above 8 — the clamp refuses it, and the framebuffer allocation and the
platform's texture creation have not been tried at a size that fails.

### 2026-08-27 (the shadows) — a drop shadow is the same shape as a word, and a settings window that never read the settings

Reported, with two screenshots of the beach camp: at 8x the survivors have no drop shadows and at
1x they do. Both true, both mine, and neither was where the first two days of looking went.

**The rasteriser was never the problem, and it took an embarrassing amount of measuring to
believe it.** Comparing the framebuffer at 1x against 2x, 4x and 8x — box-averaged down and
diffed — over six scenes found no shadow-shaped region missing anywhere; the largest connected
difference was sixty pixels along a screen-edge column. Jack's shadow is draw 28619, two mirrored
11x13 quads, and at 1x and 2x it takes the same `nearest` path and samples the same texels, which
was traced pixel by pixel. Two claims made along the way were wrong and are worth recording as
wrong: a first "reproduction" compared two crops taken at different magnifications, and a first
"confirmed cause" — nearest minification in the window — turned out to draw a byte-identical
window at 4x, because the Sharp path's second pass was already a linear fit.

**What it actually was: `is_text_run` matched the shadow.** The test asked for small quads, each
1:1 with its own cell of a sheet, at least two of them. A drop shadow is two mirrored 11x13 quads,
each 1:1 with its own cell. So with *Dialogue text at window resolution* on, every drop shadow in
the game was handed to the glyph reconstruction, and the reconstruction removed it. Measured, mean
luminance over Jack's shadow, background 148:

| scale | text off | text on, before | text on, after |
|---|---|---|---|
| 1x | 128.4 | 128.4 | 128.4 |
| 2x | 128.7 | 144.0 | 128.9 |
| 4x | 128.7 | 147.8 | 129.1 |
| 8x | 128.7 | 147.8 | 129.1 |

The fix is the question the feature always rested on and never asked: **is this sheet's alpha a
coverage field or a translucency?** Reconstruction assumes coverage — opaque inside the letter,
clear outside, a ramp between them that is the edge. Run the contrast curve on a sprite that is
uniformly half-there and opaque nowhere and it does not sharpen an edge; it concludes the whole
sprite is outside the letter. So `cell_holds_a_coverage_edge` now requires each cell to hold both
a texel at alpha 250 or more and one below the colour key, and the minimum run went from two quads
to four, because two mirrored quads are a sprite far more often than they are a word. Text
sharpening still measurably changes the text band at 2x, 4x and 8x, and is still exactly nothing
at 1x. It costs 0.12 ms a frame at 4x.

**And the settings window had never read the settings.** `macos_settings_install` is called from
the platform's constructor — `create_platform` is the first thing the frame pump does — and
`load_settings` runs sixty lines later, so the values copied into the window's hooks were a
snapshot of the *defaults*, taken before the player's file had been opened. Every open showed
those defaults whatever the game was doing. The frame rate escaped it only because
`apply_settings` pushes that one value back afterwards through `macos_settings_set_frame_rate`;
scaling, whole-multiples and all three new settings had no such path. `refresh` now reads
`settings()` live, which is what Input has always done with `input_bindings()`. Verified from the
other end: a `settings.txt` naming `render-scale 4` gives a 1280x960 framebuffer with no flag on
the command line.

**Also fixed, though it was not this.** The window path assumed the picture was always being
magnified, which stopped being true when a render scale could make it larger than the window.
`Nearest` scaling had no filtered pass at all, and any reduction beyond 2x was one bilinear tap;
`present` now branches on whether it is reducing and reduces by successive halvings. At 4x it
draws the same window as before, pixel for pixel; at 8x it is a measurable improvement and no
more than that.

`ctest` is 17 for 17.

**Not verified.** That `cell_holds_a_coverage_edge` admits every run of text in the game. It was
checked on the chapter cards, the tutorial line and the objective banner; a screen whose font is
drawn without any fully opaque texel would now fall back to `Nearest`, which is the old behaviour
and no worse than 1x, but it would be a silent loss of the feature rather than a visible fault.

### 2026-08-27 (a shared core, step one) — the recompiler stops being copied

`recomps/common/` exists, and the first thing in it is the recompiler: `tools/recomp/`, the image
reader, the instruction decoder, the control-flow walk, the C++ writer and the function table.
Both titles now import it from there. Neither has a copy.

**Why this one first.** It is the largest piece that is already the same file — `image.py`,
`cfg.py` and `cpp.py` were byte-identical between the two trees, `arm.py` 93% and `generate.py`
91% — and it is Python, so it proved the arrangement without touching a namespace, a build, or an
oracle.

**Two things had to become parameters, and both are the shape every future one will take.**

* **The namespace.** `generate.py` wrote `namespace lost::game` in ten places. It now takes
  `namespace=` and each title's `emit.py` passes its own.
* **Which functions are runtime rather than game.** The two titles decide this differently and
  each is right about its own image. Lost lists them in `src/runtime/arm_runtime.json`; Mini Golf
  has a contiguous address range, because armcc laid its soft-float library out on the other side
  of the game's code. The shared table takes the answer as a *set*; the rule that produces it, and
  the two constants it needs, moved into `Mini Golf/tools/funcs.py` where they were always facts
  about that binary rather than about recompilation.

That is the rule this directory is run on: a difference that is a **measured fact about a binary**
stays with the title and becomes a parameter; a difference that exists only because one tree was
fixed and the other was not belongs to neither, and belongs here.

**Verified by regenerating, not by reasoning.** Lost's emitter was re-run and its 37 generated
files compared with the ones already in `gen/`: byte-identical. Mini Golf's was re-run into
`build/gen-pure/` and its 20 generated files compared the same way: byte-identical, plus one
*new* file — `emitted.json`, which the newer emitter writes and its older copy did not. `ctest` is
17 for 17 here and 30 for 30 there, the latter including the six `recomp_*` cases that rebuild the
pure recompilation from that generated tree and compare it register for register.

`tools/port-from-minigolf.py` lost its first seven entries, and `reference/PORTED.md` its first
seven rows — in both sections, the table and the hash block `--check` actually reads. `--check`
went from twelve files "missing on one side" to the five that were always deliberate. The
intention is that the list keeps shrinking until the script has nothing left to do.

### 2026-08-27 (a shared core, step two) — the first C++ out of the copies

`recomps/common/` now builds a library, `ipod_core`, and both titles link it. Five files went in:
`platform/save_store.{h,cpp}`, `platform/text_entry.{h,cpp}` and `framework/types.h`. All five
were byte-identical between the two trees once their namespace names were normalised, and all
five are self-contained — they include nothing from a title.

**The mechanism, which is the part worth keeping.** Shared code is namespace `ipod` and is
included with an `ipod/` prefix. Each title keeps a forwarding header at the old path that pulls
the names into its own namespace with `using` declarations. **No call site changed** — not one of
the twenty-odd files that include these headers, and not a single `lost::platform::save_store()`.

`using` rather than a namespace alias, deliberately: an alias would make `lost::platform` *be*
`ipod::platform` and nothing could then be declared into it, but `platform` still holds things
that are genuinely this title's — its input actions, its `Settings` fields. `using` lets both sit
under one qualified name, which is what makes it possible to move half a namespace and leave the
rest. That is what will make the next files tractable; it was checked with a compiled probe before
anything was moved.

**Which files could go, and why not more.** `runtime/runtime.{h,cpp}` are also byte-identical but
include `runtime/cpu.h` and `runtime/memory.h`, which are not shared yet. `framework/controls.h`
and `storage.h` are byte-identical too, but their *implementations* — `libeapp/input.cpp`,
`libeapp/async_file.cpp` — are only 78% alike and stay here, so moving the interface would leave a
declaration in one namespace and a definition in another. An interface and its implementation have
to cross together.

`ctest` is 17 for 17 here and 30 for 30 there.

**One thing that is not a regression but is worth writing down.** Running both titles' suites at
the same time failed `frames_name-entry` once. Alone it passes five times out of five, and the
cause is in the harness rather than in either tree: `tests/frames.sh` says itself that the pinned
emulator "writes its screenshots to a fixed path in /tmp that any other run would overwrite", and
with two recomps that is no longer a hypothetical. **Do not run two titles' suites concurrently**
until that path is per-run.

### 2026-08-27 (a shared core, step three) — one rasteriser, and the two things it had to be told

`libeapp/gles.{h,cpp}` and `framework/graphics.h` are the shared core's now, and they are this
tree's — Lost's rasteriser had every function Mini Golf's had and twenty more, and Mini Golf's had
none of its own. Mini Golf's copy is deleted. With it went `runtime/memory.h` (identical in both:
the device is the same device) and `fatal`, split out of `runtime.{h,cpp}` because it is the one
runtime service that takes no `Cpu&`.

The rasteriser needed exactly two things from a title in the end: `ipod::log_call`, declared in
`ipod/libeapp/call_log.h` and answered by each title's `framework_call.cpp`, and nothing else. The
`host_state.h` include had been vestigial for some time and is gone.

**Two behaviours turned out to be the game's rather than the hardware's**, and both were invisible
to every oracle in either project — a call log records a buffer's address, never its contents, so
a rasteriser can be completely wrong without a single logged word changing. They were found by
rendering Mini Golf's scripts through both renderers and comparing the pictures, which is the only
thing that could have found them.

* **How a draw's attributes are recognised.** The rule here — an attribute belongs to the draw
  that pointed it — rests on this game re-pointing every attribute before every draw. It does.
  Mini Golf points once and draws many times, so under this rule every draw after the first read
  as untextured. It is now `gfx::set_attributes_repointed_per_draw`, which this title calls and
  the default does not. **The justification in the old comment was wrong**: neither game imports
  `glDisableVertexAttribArray`, so that was never what made the difference; the re-pointing is.
* **What an untextured draw is painted in.** This game puts a colour in the constant register and
  its letterbox bars and dialogue panels are painted from it. Mini Golf never writes that register
  and carries a flat draw's colour in the vertex array. Painting from an unset register is opaque
  white, and whole Mini Golf screens came out blank. That one needs no flag: the state now records
  whether the game ever set a constant colour.

Measured after both: 46 Mini Golf screenshots across five scripts differ from its old renderer by
a **mean of 0.22** out of 255, worst 0.44, which is the alpha-weighted filtering and the
sprite-sheet edge clamping this tree had and that one did not. Before the second fix the worst was
**199.6** with 95% of the frame changed. `ctest` is 17 for 17 here and 30 for 30 there.

**Two harness notes.** `recomp_*` compares against `build-recomp/`, which nothing in `ctest`
rebuilds — after a change to shared code it is stale, and six cases fail for that reason alone
until `check-recomp.sh` or a manual build refreshes it. And `LOST_VERTEX_HASH` is now
`IPOD_VERTEX_HASH`, since the debug switch moved with the file.

### 2026-08-27 (a shared core, step four) — the music, and `afplay` finally goes

The crutch this file has carried since day one is gone. Music was a child `afplay` process:
macOS only, no volume this program could set, and no way to stop it that was not a signal. The
comment above `MusicPlayer` had a TODO asking for "a decoder per platform feeding SDL's audio
stream like the sound effects do", and said that whatever did it "replaces this class entirely;
nothing outside it knows how music is produced". Both turned out to be exactly right.

**The Mini Golf recomp had already written it.** That is the point worth keeping: the newer half
of a pair is not always the same tree's. The rasteriser went from here to there; the decoder came
the other way. `ipod/platform/sdl3/music_decoder.{h,cpp}` is now the shared core's, and this
tree's `MusicPlayer` is that one — which brought `stop()` and `set_gain()` with it, so the game's
own volume and Music: OFF now reach the music, which under `afplay` they never could.

Removed with it: `LOST_MUSIC_AFPLAY`, `<signal.h>`, `<spawn.h>`, `<sys/wait.h>` and `environ`.
There is no longer any code in this program that spawns a process. `LOST_TRACE_AUDIO=1` came
across too and prints what the audio is asked to do.

Verified by playing it: into the jungle, the trace reads

    audio: music …/0.mp3 (44100 Hz, 2 ch)
    audio: music stopped
    audio: music …/3.m4a (44100 Hz, 2 ch), repeating

— the second of those being the AAC that SDL cannot decode and that `afplay` existed for.

The decoder is not in `ipod_core`, because SDL is found *after* the shared directory is added and
the headless build has no SDL: it is published as `IPOD_CORE_SDL3_SOURCES` and compiled into each
title's window build. `lost` now links `-framework AudioToolbox` as `minigolf` already did.

`ctest` is 17 for 17 — run three times over, because two earlier single failures of
`frames_name-entry` had made the suite look flaky. Both were the concurrency noted in step two;
alone it is green every time.

### 2026-08-27 (the pink screen) — a frame the game never drew

Reported: opening the game shows a pink screen. It is the framebuffer's own magenta, and it was
reaching the window.

**Frame 0 draws nothing.** The firmware calls the game once with `CONTEXT_REASON_FIRST_FRAME` —
"you are now running" — and it answers without a clear and without a draw. The buffer at that
moment is still the magenta it is filled with at start-up, so that an *un-drawn region* is
unmistakable in a screenshot, and the frame pump presented it like any other frame. One frame at
60 fps ought to be imperceptible; it is not, because macOS holds the first presented frame through
the window's appearance animation, which is exactly long enough to read as a deliberate splash.

Measured, not guessed: a screenshot of frame 0 is 100% magenta and frame 1 is 0%. **Not a
regression** — a build from the backup taken before today's shared-core work does the same thing,
which was checked rather than assumed.

The magenta is worth keeping where it earns its place, so it stays: `gfx::anything_drawn()` says
whether a clear or a draw has happened, the frame pump does not present until it has, and a
*screenshot* of frame 0 is still 100% magenta because `shot` reads the framebuffer and not the
window. The renderer is also cleared to black once at start-up, so what the window shows for that
one frame is decided here rather than left to whatever SDL happens to start with.

Both titles got it — the flag is in the shared rasteriser and the guard is three lines in each
frame pump. `ctest` is 17 for 17 here and 30 for 30 there.
