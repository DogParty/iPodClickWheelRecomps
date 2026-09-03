# The Cubis 2 recomp — plan of attack

**Goal:** `Cubis2_1_1_2563292.bin` — the 2007 iPod click-wheel port of *Cubis 2*, a
match-three played by firing cubes into an isometric grid — running natively on the ARM Mac as
a statically recompiled C++ program, drawing into an SDL3 window, driven by the keyboard, and
*proven* equivalent to the emulator by diffing the sequence of framework calls both make on the
same scripted input. Then hand-decompiled, function by function, into readable modern C++, each
swap proven against the recompiled version.

Everything lives in this folder. The emulator tree is read, snapshotted, and otherwise left
alone — with one exception this title forced and which is recorded in full below and in
`reference/MANIFEST.md`: the emulator drew every cube on this game's board white, and the fix
for that is the emulator's, not this tree's.

This is the **sixth** title to go through this process, and the fourth to start with
`common/` in place. *Mini Golf* is finished — every one of its 333 functions is
hand-decompiled. *Lost*, *Texas Hold'em*, *The Sims Bowling* and *Vortex* run as pure
recompilations with their oracles green. `../Mini Golf/PLAN.md`, `../Lost/PLAN.md`,
`../HoldEm/PLAN.md`, `../Sims Bowling/PLAN.md` and `../Vortex/PLAN.md` are the record of how the
first five were done, and `../common/README.md` is the record of what they turned out to have in
common. **Read the code quality section of the Mini Golf plan; it binds this project unchanged.**
What is written below is the delta: what Cubis 2 is, what it takes from the shared core and what
it still has to copy, and — the part that actually matters — the places where Cubis 2 is none of
the other five.

---

## What Cubis 2 is

Measured, not guessed. Every number here is answered by a file in `analysis/` (its `README.md`
says how each was made). They come from `tools/eapp-inspect` in the emulator tree, from a static
control-flow walk of the image with the shared recompiler (`analysis/survey.txt`), from this
tree's own `tools/funcs.py` and `tools/emit.py`, and from scripted sessions through the
emulator.

| | |
|---|---|
| image | `Cubis2_1_1_2563292.bin`, 695 800 bytes, loads flat at `0x18000000`, ends `0x180a9df8`. Header version `0x10001000`; `eapp-inspect` warns, as for Hold'em, Sims Bowling and Vortex, that its block-count word says 5 while seven framework blocks are present |
| game data | `Games_RO/99999/` — 114 files named by the game's own `Manifest.plist`, plus Apple's four installer files, ~10 MB. Three **cube sheet sets** (`classic/`, `jewel/`, `metallic/`, four `sheet-N-{c,w}.raw` each — TGAs under a `.raw` name, 16-bit grey-plus-alpha), four **backdrop themes** (`desert/`, `rainforest/`, `underwater/` and the shared `common0*` files, Windows BMPs under a `.pix` name), ten `fonts/*.raw`, nineteen `images/*.ipd`, eleven `strings/*.dat`, 21 loose `media/*.wav` sound effects, two `.m4a` music tracks (`g`, `m`), `Cubis2.raw.lcd5`. Plus **two files the game writes** (difference 3) |
| entry vectors | 3: `0x180384d0` start-up, `0x180384cc` terminate (header slot 1), `0x1803852c` per-frame |
| functions | **349** reachable by walking the control flow from the entry vectors alone; **1 382** once the probes' live edges, the image's stored function pointers and the code addresses its instructions form from the program counter are added and the emitter walks to a fixpoint (417 of those it found on its own) |
| instructions | **64 827** ARM instructions recompiled — 113 234 lines of generated C++ in 63 files, emitted in 0.7 s. **0 unwalkable** |
| import thunks | **429** across seven frameworks |
| frameworks | OpenGLES 179 · Metadata 152 · Audio 61 · AsyncFileIO 17 · miscTBD 15 · Settings 3 · InputEvents 2 — Lost's, Sims Bowling's and Vortex's layout to the byte (OpenGLES first at `0x18000064`, Settings last), not Hold'em's eight |
| ordinals the probes reach | 56 across a silent boot and a run into a level. One of them no title has named (`Audio #46`, called once) |
| code properties | ARM state only; the walk fails on nothing |
| emulator behaviour | boots to its **main menu** — the CUBIS 2 logo over a dark cube backdrop, *New Game / Volume / Options / High Scores / Help / Exit* — by frame 500, `m.m4a` repeating; one Select reaches **Name Entry**, and from there a name and the tick reach the **New Game** options screen, then a level: an isometric board of coloured cubes on an underwater backdrop, with SCORE / CUBES / TIME down the left |

For scale: Mini Golf was 333 functions and 23 268 instructions, Lost 789 and 65 423, Hold'em 951
and 53 499, Sims Bowling 2 401 and 71 629, Vortex 753 and 39 247. **Cubis 2 is the third-largest
of the six** by instruction count and sits beside Lost — and, like Lost, it is a game whose
interesting part is the data it reads rather than the arithmetic it does.

Two facts about its lineage matter for everything below. Its build number (`2563292`) sits
between Hold'em's (`2563291`) and Mini Golf's (`2563296`): the same SDK, the same week, and — as
for Vortex, one below Hold'em — the same *firmware protocols*. The binary shows it where it
counts: the **button-flags word** (the `bic #0x60` signature the emulator's `find_flags_word`
derives an address from), the **float matrix path** (`OpenGLES #125`, not the fixed-point `#149`
Lost and Sims Bowling use) and the constant-colour register in both its forms (`#147` and
`#148`). Yet its import table is the 429-thunk seven-framework layout of the 2007 titles, and for
*the state of the code* Vortex's tree is the newest copy of the shared layers — the forwarding
headers into `../common`, the widest tracing aids, and the recording harness that checks a run
for the contamination rule 11 describes — and is what this tree copies from.

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
core can carry; what differs by a measured fact becomes a parameter), Sims Bowling's (**10.** a
shared-core change is verified in every title before it is used here) and Vortex's (**11.** a
probe or a recording is only as clean as the machine it was made on, and the harness checks —
the emulator has no headless mode, its window takes a click as a Select, and every title's
screenshots share one `/tmp` path).

One addition this project makes, because it is the first whose *first* finding was a defect in
the oracle rather than in itself:

**12. A fix to the emulator is a fix to the oracle, and it is verified against every title
before this one's recordings are made.** The emulator drew all of this game's cubes white
(§ "Where Cubis 2 is none of the other five", difference 6). The rule the playbook already gives
— "nothing in the emulator tree is edited for a recomp; if the emulator is wrong about the
title, the fix goes in the emulator tree in its own commit, followed by a re-pin and
re-recording" — says where the change goes. What it does not say is how much it has to be worth,
and the answer this title settled on is: a rendering change to the emulator is run against **all
twenty titles** in `Games_RO/`, before and after, boot screenshots compared, and every difference
accounted for one by one. `build/sweep/` is that harness; the result is in the progress log.
Rule 10 is the same rule for the shared core, and the widening this fix implies there
(`../common/src/ipod/libeapp/gles.cpp`) is held to it.

---

## What is inherited, from where, and what that costs

The layers of a title, and where each one comes from now:

| layer | state today | what happens here |
|---|---|---|
| `tools/recomp/` — the ARM→C++ recompiler | **shared** (`../common/tools/recomp`) | imported; `Generator(namespace="cubis")`; no change needed — the walk reaches every function and fails on nothing |
| `src/runtime/` | `cpu.h`, `runtime.{h,cpp}`, `memory.h`, `fatal` shared; `eapp_image`, `memory.cpp` copied; `main.cpp` per title | copied from Vortex; `main.cpp`'s reason model, flags-word address and clock model adopted by hand (differences 1, 2 and 5), with the provenance in the comment |
| `src/framework/` — the typed platform interfaces | `types.h`, `graphics.h` shared; `controls`, `storage`, `device`, `audio`, `music_library` copied (they declare what each title's `libeapp` implements — `../common/README.md` says why they stay) | copied unchanged |
| `src/libeapp/` — the iPod frameworks | `gles.cpp`, `heap` shared; `misc`, `host_state`, `arm_abi`, `framework_call`, `input`, `metadata` identical copies; `async_file`, `audio` carry each title's model | copied from Vortex; `imports.json` gains `Audio #46`; `async_file.cpp` loses Vortex's asset-correction hook, which is that title's defect and not this one's |
| `src/platform/` | `save_store`, `text_entry`, `music_decoder` shared; the rest copied | copied; this title's name and bindings |
| `src/gamedata/` | `zip` shared; `install`, `manifest` copied | copied; `manifest_data.cpp` regenerated from `99999` (118 entries), with the two written files ignored (difference 3). Vortex's `asset_fixes.{h,cpp}` is **not** copied — see difference 7 |
| `tests/` harness | copied in every tree | copied from Vortex, rule 11's checks included |
| `src/game/` | no | nothing to inherit; this is the work |

**How a title reaches the shared core** is settled: `../common` is added as a subdirectory and
`ipod_core` is linked through this title's own `cubis_common` interface target, so the shared
sources are compiled under this title's warning rules. Shared code is namespace `ipod` and is
included as `ipod/…`; forwarding headers at the old paths pull the names into `cubis::…` with
`using` declarations. No call site knows which side of the line a file is on.

**What is copied is copied by a script, and only by the script.**
`tools/port-from-vortex.py` is Vortex's `port-from-bowling.py` with its source tree and rewrite
table changed (`vortex`→`cubis`, `VORTEX_`→`CUBIS_`, the data-directory strings). It writes
`reference/PORTED.md` with the source path and SHA-256 of every file at the moment of the port;
`--check` reports drift on either side. As before, **identifiers are rewritten and prose is
not**: a comment that says "Vortex divides by its own frame delta" stays true rather than being
made false, and `grep -rn "Vortex\|Sims Bowling\|Hold'em\|\bLost\b" src tests tools` is the
worklist for adopting each file.

The namespace is `cubis` (`cubis::eapp`, `cubis::platform`, `cubis::game`, `cubis::runtime`,
`cubis::gfx`…), the CMake options are `CUBIS_*`, the targets are `cubis` and `cubis-headless`,
the per-user data directory is `iPod Cubis 2/99999`, and the override is `CUBIS_DATA_DIR`. A
`vortex`, `bowling`, `holdem`, `lost` or `minigolf` identifier anywhere in this tree is a defect.

---

## Architecture

```
Cubis2/
  PLAN.md                this document
  README.md              layout, building, testing, contributing a decompiled function
  CMakeLists.txt         adds ../common; targets cubis (SDL3) and cubis-headless (tests)
  tools/
    survey.py            what is in the image — every number in this plan
    funcs.py             build gen/funcs.json — the function table the emitter works from
    emit.py              run the shared recompiler with this title's namespace and bindings
    progress.py          how much of the game is still recompiled rather than decompiled
    manifest.py          the game folder's file table -> src/gamedata/manifest_data.cpp
    assets.py            do the shipped images parse, and does what the game uploads match them
    probe.sh             run a script through the pinned emulator, cleanly (rule 11)
    port-from-vortex.py  copy what is not yet shared, and record it (reference/PORTED.md)
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

Everything about the guest machine — one flat reservation, `ld32`/`st32` through `memcpy`-based
helpers, one `void f_1800xxxx(Cpu&)` per ARM function, `goto` for intra-function branches,
resolved jump tables as `switch`, `b .` as an assert trap, flags computed eagerly by the rules
ported from `arm7tdmi/arm.rs` — is exactly as the Mini Golf plan describes it and is not
re-litigated here.

**Ghidra is not a dependency.** The seed is the vectors, the live edges, and the image's stored
function pointers; the emitter reaches 1 382 functions with no failure.

---

## Where Cubis 2 is none of the other five

Each of these is a measured fact that has to become code in this tree or a parameter in the
shared one — never a silent edit to a copied file.

**1. The reason byte is read at `[ctx+0x100]`, and nothing ever writes it.** The firmware calls
the frame vector with `(ctx, ctx+0x100)`. Every title before this one takes its reason from
`[ctx+0]`; this one takes it from the second argument:

```
0x1803852c  push {r2..r9, sl, lr}
0x18038530  mov r4, r0            ; ctx
0x18038538  mov r5, r1            ; ctx + 0x100
0x1803853c  bl  0x18000e8c        ; miscTBD #9, with r0 = ctx+4
0x18038540  ldrb r0, [r5]         ; <- the reason byte, at ctx+0x100
0x1803854c  cmp r0, #0
0x18038550  ldreq r0, [r4, #4]    ; zero: republish the firmware's clock into the game's
0x18038558  streq r0, [r9, #0x18] ;  three timer words and clear two counters
0x1803856c  streq r8, [r4, #0x34]
```

A binary named `Cubis2*` matches no arm of the emulator's per-title table and takes its last one
(`_ => d(true, None, None, None)`), so the emulator seeds `[ctx+0]` with 5 once, before the init
vectors, and writes **neither** byte again. `[ctx+0x100]` therefore holds the zero its
allocation was cleared to for every frame of every run, and the game takes the `eq` arm above
every frame. The pump here does the same, in a named constant with this paragraph beside it. Six
titles, six protocols; Sudoku is the only other title known to read the reason at `+0x100`
(`play.rs`, `--reason-offset`), and it is not one of the six.

**2. Buttons are a flags word at `0x180a9db0`; the wheel is a poll the game makes itself.**
The emulator finds the word from the `bic r0,r0,#0x60` signature and reports it at start-up
(`button flags word at 0x180a9db0`; `no press-time words for this title`). That is Hold'em's,
Mini Golf's and Vortex's model at a different address. What is this title's own is the *other*
half: the frame vector polls `InputEvents #0` itself and dispatches on what comes back —

```
0x18038568  mov r0, sp / add r1, sp, #4 / bl 0x18000920   ; InputEvents #0(&a, &b)
0x18038574  ldr r0, [sp, #4]
0x1803857c  tst r0, #0x40000000                           ; the EVENT PRESENT bit
0x18038588  beq 0x180385c4
0x1803858c  and r0, r0, #0xff / bl 0x180054d4             ; the low byte -> the wheel dispatcher
```

— and the byte that arrives is the wheel sample the emulator's input queue hands back
(`Stub::InputPoll`, which pops one queued byte and sets bit 30). So a *press* has to queue a
wheel sample alongside the flag bit, or the game never looks at the frame: that is what the
emulator's `press_button` does and what the pump here copies. `WHEEL_DETENTS` and `wheel_byte`
are the emulator's, one sample per detent. Measured on the Name Entry screen: **ten detents move
the highlight one position** (`wheel -20` moved it two, `wheel -40` four, from `A` to the tick),
and bursts smaller than that leave it where it was.

**3. Two saves, and Apple's own list says which.** `cubissave.dat` (21 760 bytes) and
`cubisgame.dat` (9 264 bytes) sit at the root of the game folder and are named by neither the
`Files` array of the game's `Manifest.plist` (114 entries) nor anything else Apple shipped, so
"written by the game" is not an inference here — it is the shipping manifest's own answer. Both
are opened for writing during the boot, with a buffer carrying the whole file
(`async open "cubissave.dat" mode 0x1 op 6 … buf=… len=21760`). `tools/manifest.py` leaves them
out of `src/gamedata/manifest_data.cpp` and `tests/game-dir.sh` strips them from every private
copy, so no case is ever a second boot.

**4. Its data is other people's formats under Apple's file names.** `classic/sheet-0-c.raw` is a
**TGA** — an 18-byte header (`imagetype 3`, 16 bits a pixel, descriptor `0x28`), the pixels, and
TGA 2.0's 26-byte `TRUEVISION-XFILE` footer — and every `.raw` in the folder is the same, fonts
and `images/battery.raw` included. Every `.pix` is a **Windows BMP**, which is a shape the
emulator already knew (`decode_bmp`). Neither matters to the recompilation: the game reads the
bytes through the file framework and decodes them itself. They matter to the *reader*, and to
difference 6.

**5. Nothing paces it, and nothing says it needs pacing.** The emulator's table gives this title
no `fps`, unlike Vortex, which divides by its own frame delta and faults on a short frame. No
divide-by-zero has been seen here — but every run so far has been under `--fixed-clock`, where
the question cannot arise, so that is not evidence. The pump keeps Vortex's floor because it is
free where it is not needed, and the plan carries the question rather than closing it.

**6. The emulator drew every cube on the board white, and that is an emulator defect.**
This is the one difference that is not about this tree at all.

Cubis 2 draws each cube twice from a pair of `GL_LUMINANCE_ALPHA` sheets: `sheet-N-c.raw`, grey
cube shapes, with the cube's colour in the constant colour register, and then `sheet-N-w.raw`,
a mostly transparent sheet of white highlights, with the register at white.

```
tex#44  uv=[171.5..200.0 , 33.5..67.5]  mod=[1.00 0.95 0.10 1.00]  pipe=10   <- the colour
tex#45  uv=[171.5..200.0 , 33.5..67.5]  mod=[1.00 1.00 1.00 1.00]  pipe=10   <- the highlight
```

The emulator ignores that register by default (`play.rs`, `m.no_modulate = !--modulate`), for a
measured reason: applying it globally turns LOST's whole UI monochrome and The Sims Bowling's
alley black, because both leave a stale value in it before drawing full-colour art. So the whole
board came out grey. Turning it on globally (`--modulate`) coloured the cubes and turned this
game's own `logosml.ipd` green, from a stale `(0,1,0,1)` — the same failure, in the same title,
on the same screen.

**The rule that separates them is the texture's format**, and it is the rule the emulator
already applied to `GL_ALPHA`, one format short. A `GL_ALPHA`, `GL_LUMINANCE` or
`GL_LUMINANCE_ALPHA` texture carries no colour of its own — coverage, or coverage and one grey
ramp — so the only thing that can be supplying a colour is the vertex array or, when there is no
array, the register. A `GL_RGB` or `GL_RGBA` texture carries its own and the register is not it.
Counted over a run through the menu, name entry and a level, every draw in this game with a
non-unit register is one of:

| format | pipeline | what it is | tint |
|---|---|---|---|
| `GL_ALPHA` | 6 | the fonts | ink colour — **wanted** |
| `GL_LUMINANCE_ALPHA` | 1, 10 | the cube sheets, the battery gauge, a 320x240 overlay | the cube's colour — **wanted** |
| `GL_RGBA` | 13 | `logosml.ipd`, `btngreen.ipd` | stale `(0,1,0,1)` and `(0,0,0,1)` — **not wanted** |

The fix is `Texture::colourless` in `tools/eapp-loader/src/lib.rs`: three formats instead of one,
in the gate that was already there. It is verified against all twenty titles in `Games_RO/`
(rule 12), and the pinned copy under `tools/oracle-emulator/` carries it — so every recording in
`tests/expected/` is of the fixed emulator, and `reference/MANIFEST.md` says so with hashes.
`../common/src/ipod/libeapp/gles.cpp` carries the same one-format-short rule and needs the same
widening under rule 10, or the picture oracle compares two different renderers.

**7. It has no data defect, and the hook for one is gone.** Vortex ships a texture pack with
three mislabelled entry headers and corrects them on the way in
(`../Vortex/src/gamedata/asset_fixes.h`), with `--original-assets` for the oracle to opt out.
Nothing of the kind has been established in this title's data, so the file is not copied and the
switch is not carried: a correction applied on anything less than proof from the game's own
loader would be this program inventing a game it is meant to be reproducing. The place it would
go is marked in `src/libeapp/async_file.cpp`.

---

## Schedule

| block | what | exit criterion | fallback |
|---|---|---|---|
| 0 | scaffold: the port, the constants, the manifest | `cmake -B build` configures; `tools/survey.py` reproduces `analysis/survey.txt` | — |
| 0b | the emulator's colour fix, verified across twenty titles; the pin | every title's before/after boot accounted for; `reference/MANIFEST.md` written with hashes | pin the unfixed emulator and record against it, leaving the cubes grey and the fix for later |
| 1 | emit | 1 382 functions, 64 827 instructions, 0 unwalkable; `gen/` compiles | fix the shape in `../common/tools/recomp/` and re-verify every title (rule 10) |
| 2 | runtime and the pump | `--frames=2` runs the start-up vector and the first frames, and the log begins as the emulator's did | — |
| 3 | the oracle: `tests/record.sh`, the cases, `imports.json` | `tests/diff.sh boot` reports its first divergence with a line number | — |
| 4 | libeapp in boot order — miscTBD/Settings/InputEvents → AsyncFileIO → Metadata → Audio → OpenGLES | every case identical, semantic and exact | — |
| 5 | SDL3 and the picture; the shared rasteriser's `colourless` widening under rule 10 | screenshots within threshold; the main menu on screen natively | — |
| 6 | the first oracle that plays, then decompilation | a level recorded and diffed; `vs-recomp.sh` | — |

## Risks, in the order they will bite

1. **The emulator fix is load-bearing for every recording.** A recording made with the unfixed
   emulator and compared against the fixed one differs in no framework call at all — the register
   is a rendering decision — but every *screenshot* differs. The pin is the fixed build and the
   manifest says so; a screenshot compared across that line proves nothing.
2. **The shared rasteriser has to be widened to match, and four other titles run through it.**
   Rule 10. If widening it moves a pixel in Lost, Hold'em, Sims Bowling or Vortex, that has to be
   understood before it is used here.
3. **`[ctx+0x100]` is read but never written.** If any real run ever puts a non-zero byte there,
   the game takes a different arm and the oracle's recordings stop describing it.
4. **Two saves opened with the whole file as a buffer.** The file model here is not Vortex's, and
   block 4's fight will be in `async_file.cpp`.
5. **`Audio #46` is unnamed** and answered with a constant, which the call-log oracle cannot
   catch.

## Not today

* Hand decompilation. Block 6 opens it; "done" for the first pass is the pure recompilation.
* The six frame-reason protocols as one parameter. Sixth instance, still six special cases.
* `.raw`/TGA and `.pix`/BMP decoding in this tree: the game does it, and until a function is
  decompiled nothing here needs to.

---

## Progress log

### 2026-08-28 — block 0: the measurements, the port, the scaffold

`analysis/survey.txt` from `tools/survey.py`, `analysis/assets.txt` from `tools/assets.py`,
`analysis/ordinals.txt` from the two probe recordings, and four probe runs under
`analysis/coverage/` (`analysis/README.md` says how each was made). `tools/port-from-vortex.py`
copied 71 files from the Vortex recomp with the identifier rewrites, and `reference/PORTED.md`
records their hashes. The constants of § 2.5 were adopted by hand and
`src/gamedata/manifest_data.cpp` regenerated from `99999` (118 entries).

Two files came out rather than in. Vortex's `gamedata/asset_fixes.{h,cpp}` and its
`--original-assets` switch correct three mislabelled entries in *that* game's texture pack;
nothing of the kind is established here, so neither is copied and the place one would go is
marked in `src/libeapp/async_file.cpp` (difference 7).

**Established:** the numbers in § "What Cubis 2 is"; differences 1–5 and 7.
**Not verified:** whether this game ever writes `[ctx+0]` (the suspend byte), and whether its
arithmetic needs the frame-time floor the pump keeps from Vortex.

### 2026-08-28 — block 0b: the emulator drew every cube grey

The first thing this title found was a defect in its own oracle. `PLAN.md` difference 6 is the
account. In short: the board's cubes are grey `GL_LUMINANCE_ALPHA` sheets tinted through the GL
constant colour register, the emulator ignores that register by default for a measured reason
(applying it globally wrecks LOST and The Sims Bowling), and so the whole board rendered grey.
The rule that separates the two cases is the *texture's format* — a texture with no colour of its
own takes the register, one with colour does not — which is the rule `lib.rs` already applied to
`GL_ALPHA`, one format short.

The fix is `Texture::colourless` in `tools/eapp-loader/src/lib.rs` and the same widening in
`../common/src/ipod/libeapp/gles.cpp`, and it was verified the way rule 12 now requires: all
twenty titles in `Games_RO/`, before and after, four boot screenshots each. **Sixteen are
pixel-identical.** Cubis 2 moves by 8.4–8.8 % of the frame (the point of the change). LOST moves
by 85 pixels of 76 800 — its battery gauge, grey → green, which is what the device showed. The
other three that appeared to move on the first attempt did not: two "changed" images turned out
to be *other titles' title screens*, arriving through the `/tmp/ipod-shot-NN.png` path every
title shares, and one difference was the clock in the status bar reading a different minute.
`build/sweep/run2.sh` is the harness that has neither hole — each shot copied on the line that
announces it, named by the frame number that line carries, every run at `--time=00:00` — and rule
11 has now cost two projects a wrong answer each.

**Established:** difference 6, and the fix, against twenty titles.
**Not verified:** what location 4 *means* on the real driver. The format rule is a measured
default, like the one it replaces, and the next title that tints something is the next test of it.

### 2026-08-28 — blocks 1–4: the pure recompilation, green on the first attempt

`tools/funcs.py` seeded 1 549 entries and `tools/emit.py` emitted **1 382 functions, 64 827
instructions, 0 unwalkable** into 63 files of generated C++ in 0.7 s — no change to the shared
recompiler. The pump took differences 1 and 2 by hand. Four cases were recorded from the pin —
`boot` (600 frames to the main menu), `menu`, `name-entry`, `first-level` (a name typed, the
options screen, a level played to GAME STATS) — and **all four are identical to the emulator,
semantic and exact, on the first run**: 58 841, 58 841, 110 102 and 1 236 325 calls.

That has not happened before on this project, and the reason is worth recording: this title's
firmware protocols are Hold'em's and Vortex's, its `libeapp` model is Vortex's, and Vortex's tree
had already paid for all of it. The only ordinal this title added is `Audio #46`, called once, and
it is answered with a constant.

**Established:** checkpoints A and B.
**Not verified:** nothing beyond the four cases. A path this game takes that they do not — the
options screens, the other two cube themes, a level actually played by a person — is unrecorded.

### 2026-08-28 — block 5: the picture, and two things the call log could never have said

The first picture comparison put the menu **2.64 % out**: scattered single pixels all over the
CUBIS 2 logo, each one a neighbouring texel of a busy sprite, with the geometry, the textures,
the draw order and the whole call log identical on both sides. Two causes, both real, both in the
shared rasteriser rather than in this title:

**1. Clang contracts, rustc does not.** `fill_triangle`'s barycentric weight is
`((b.x - fx) * (c.y - fy) - (c.x - fx) * (b.y - fy)) / area` in both implementations. Clang fuses
that into an FMA by default in C++; rustc does not contract at all. The fused form is *more*
accurate, and that is the problem — see below. `-ffp-contract=off` on `ipod_core` made the menu
pixel-identical.

**2. The tie underneath it, which is the real defect.** These games draw a full-screen backdrop
with its texture rectangle offset by exactly half a texel — this one's is `pos [0..320]`,
`uv [0.5..320.5]` — so the sample for *every* pixel lands exactly on the boundary between two
texels, and `dx >= 0.5` is then decided by the last bit of the interpolation. Measured against the
artwork the backdrop is drawn from (`underwater/background.pix`, decoded independently): a bare
`>= 0.5` reproduces **57–75 %** of the artist's pixels, the rest coming from the neighbour,
scattered per pixel. That is what made a 320×240 photographic background render as noise, and it
was in the emulator and in `../common` alike. With a tolerance wider than the arithmetic's noise
(~1e-6 texels) and far narrower than any real offset, the same run reproduces **88–99 %**, the
remainder being what the game legitimately draws on top.

The tolerance is the fix that was kept, in both renderers; `-ffp-contract=off` was then not
needed and was taken back out, because rule 10 caught it: it made Mini Golf's second oracle
(its hand-decompiled build against its own pure recompilation) render differently from itself in
all six paired cases. That is the rule working exactly as intended — a change that made this
title's picture right and another title's comparison wrong, found before it was used.

With the tolerance in both, every picture case here is **pixel-identical**: `menu`, `name-entry`
and all five frames of `first-level`, including the board of coloured cubes.

**Established:** checkpoint C on the picture side, and a rendering defect that had been in the
emulator for every title.
**Not verified:** the backdrop still sits **one pixel left** of where the artwork puts it. The
tie now resolves consistently, but it resolves *upward*; resolving it downward puts the columns
exactly right and moves the rows one down instead, because the game offsets its `u` by +0.5 and
its `v` by −0.5 and the projection flips one of them. Which of the two the device did is not
established, and nothing here depends on it — both renderers agree either way. It is the next
thing to measure, and it wants the driver, not another screenshot.

### 2026-08-28 — the assets, end to end

Asked whether the artwork was being read and drawn correctly, `tools/assets.py` answers both
halves without an emulator (`analysis/assets.txt`):

* **Every shipped image parses, and its header accounts for its length exactly.** The extensions
  mean nothing: `.raw` is a **TGA** (26 of them — every font, every cube sheet), `.pix` is
  **either** a Windows BMP (six) **or** the same 16-byte-header format `.ipd` uses (four), in the
  same folders, and `.ipd` is that header — width, height, a format code (1 = RGB565,
  2 = RGBA5551, 3 = RGBA4444), a spare word — followed by 16-bit pixels.
* **56 of the 57 textures a boot uploads reproduce a shipped file exactly**, three sample texels
  each, against a reader written from scratch — so the game's own parser, the framework's decode
  and that reader are three independent readings of the same bytes agreeing. The 57th is a
  320×240 RGB565 texture the game *composes*, uploaded before any background file is read.
* Two of the BMPs (`common02.pix`, `common03b.pix`) declare `BI_BITFIELDS` masks of
  `0x0f00/0x00f0/0x000f/0xf000` — ARGB4444 — and **the game reads them as RGBA4444 anyway**. The
  emulator's own `decode_bmp` honours the masks, so its *preload* of those two files is wrong;
  it never reaches the screen, because the game's upload replaces it. `common03a.pix` cannot be
  told apart, its three probes being all zero.

So the blockiness was never the assets. It was the sampler, and it is fixed above.

### 2026-08-28 — the file model, and the suite green

The last thing the ported tree carried that was another title's was its save-file rule. Vortex's
`is_save_name` named `options`, `stats` and `<lang>/stats`; this game's two saves are
`cubissave.dat` and `cubisgame.dat`, and its *model* is not Vortex's either — each is opened once
per boot, mode 1, **with the whole file's worth of buffer already attached and no transfer
afterwards**, which is a load. `src/libeapp/async_file.cpp` now says that with the trace beside
it, and `tests/unit/save_files_test.cpp` holds the layer to it, including the case that would
have caught the fault the Sims Bowling recomp paid for: a save opens to be *read* whatever mode
it names, because believing mode 1 would write the game's uninitialised buffer over the save on
every launch. `framework/storage.h` gained `save_key` so the test can ask this layer where a save
went rather than keeping a second copy of the rule.

**`ctest --test-dir build`: 19 of 19.** Eight unit tests, four cases in semantic form, four in
exact form, three pictures. `build/cubis` (SDL3) builds and links.

**Established:** everything the schedule's blocks 0–5 asked for.
**Not verified:** the write. No recorded case reaches a point where this game saves — the oracle
runs with writes refused, and a level ended by the clock running out is not a level ended by a
player — so `commit()`'s path for these two files has never been exercised against the game.
Neither has anything past the first level, the other two cube themes, the Options screens, or the
`.wav` sound effects, which are read but not heard in a headless run.

### A note on the port manifest

`python3 tools/port-from-vortex.py --check` reports, alongside the sixteen files adopted here,
**two files changed on the Vortex side since the port**: `src/runtime/main.cpp` and
`tests/unit/save_files_test.cpp`, both with modification times a few minutes after this tree took
its copies. Nothing here wrote them, and this tree was not yet in version control, so what
changed cannot be recovered from it. It is recorded rather than resolved, because that is exactly
what `reference/PORTED.md` exists to make visible — the copies drift, and the manifest is the only
thing that ever notices. Anyone re-porting from Vortex should diff those two first.

### 2026-08-28 — four faults a player sees and no oracle could

Reported from playing the window build. Every one of them was invisible to `ctest`, and three of
the four were in the *shared* layers rather than in this title — which is the point of the rule
that a shared-core change is verified in every title, and of the one that says the picture oracle
compares the recomp with the emulator and not with the truth.

Everything below is behind `--emulator-graphics` or `--emulator-firmware` where it changes what
the oracle sees, so all six titles' suites are green on it: Cubis 2 19/19, Vortex 21/21, Lost
17/17, Hold'em 16/16, Sims Bowling 22/22, Mini Golf 30/30.

**1. Text was drawn blurred, one texel right, and a pixel too wide.** Three separate faults
stacked on the same glyphs, in `../common/src/ipod/libeapp/gles.cpp`:

* *Blurred.* A run of glyphs is one draw of many quads whose texture coordinates are scattered
  across the atlas, so the draw-wide 1:1 test answers no and the whole string was sampled
  bilinearly — while every glyph in it is a perfect 1:1 blit. `every_quad_is_one_to_one` asks the
  question per quad.
* *One texel right.* A 1:1 blit's sample lands exactly between two texels for every pixel of the
  draw, because these games place quads on half-texel offsets. The tie was resolved upward
  always, which for a glyph means dropping its own first column and pulling in a column of the
  *next* letter in the atlas. The quad's own edges settle it — the same number of pixels onto the
  same number of texels — so the tie now resolves toward the edge the coordinate came from
  (`TieBias`).
* *A pixel too wide.* The rectangle a quad covers is half-open at its far edge, and the far edge
  of a `x [83.5..88.5]` glyph lands exactly on a pixel centre: an inclusive box drew six pixels
  for a five-texel cell, and `within` allowed the sixth to read the neighbouring cell. Both the
  pixel box and the texel bounds are half-open now.

`Arcade` had been rendering as `Ahcadc`, `Game Pack` as `Game Packl`, and every string in the
game carried a row of stray ticks. They are clean.

**2. The clock always read 12:00 PM.** The game's own consumer says why, at `0x1800de30`:

```
0x1800de48  mov r4,#0xc / mov r6,#0 / mov r5,#1   ; the defaults: 12, :00, PM
0x1800de58  bl 0x18005694                          ; miscTBD #12
0x1800de5c  cmp r0,#0 / beq 0x1800deac             ; ANSWERED 0 -> keep the defaults
0x1800de64  ldr r0,[sp,#0x1c] / cmp r0,#0xc        ; word[2] is the hour, tested against 12
```

Two faults, both in every recomp: `wall_clock` returned `void` — so `misc_host_time` answered 0,
which this game reads as "no clock" and replaces with a hard-coded **12:00 PM** — and the hour was
handed over already folded to 12, when the game does that conversion itself and needs 24. With
both fixed the status bar reads `3:30 PM` at 15:30 and `12:05 AM` at 00:05.

**3. The battery gauge was painted full whatever the machine's charge.** `return 100;`, in every
recomp.

**4. Closing the window threw the save away.** This game writes `cubissave.dat` and
`cubisgame.dat` only when it is *asked to shut down*, and the way to ask is a player's way: Menu
on the main menu. Traced with `CUBIS_TRACE_FILES=1`, that press produces `store-open` /
`store-write` / `store-close` for both files and then the game sets its suspend byte — which also
answers a question the plan had carried as unverified: **this game does write
`CONTEXT_STATE_SUSPENDED` at `[ctx+0]`.** The pump used to stop the instant the window closed, so
none of that ran. A host quit now *starts* a shutdown — Menu once a frame until the game answers
or 300 frames pass — and a script's `quit` still stops at once, because a recorded case has to end
where the recording did. `close` is a new script action, so the path is testable without a window.

### 2026-08-28 — the clock and the battery move to the shared core

`../common/src/ipod/platform/device.{h,cpp}` is new, and all six titles call it. Faults 2 and 3
above were in five separate copies of the same twenty lines, which is exactly the failure
`../common/README.md` was written about; the answer is not a fact about any binary but about the
host, so it belongs there. It is platform-specific because a battery has no portable spelling:
IOKit's power-source API on macOS, `GetSystemPowerStatus` on Windows,
`/sys/class/power_supply/BAT*/capacity` elsewhere, and *full* where the host has no battery to
report — a desktop is a device that is always on the charger, and reporting 0 would put every
game into its low-battery behaviour. `ipod_core` links `IOKit` and `CoreFoundation` on Apple for
it; nothing else in the shared core links a framework.

`set_emulator_device(true)` (from `--emulator-firmware`) puts the emulator's answers back — the
folded hour, the zero return, the constant 100 — because that is what every recording in every
title's `tests/expected/` was made against. `--battery=N` is new beside `--time=HH:MM`, so a gauge
can be seen at a level this machine is not at.

**Not verified:** Mini Golf's own decompiled consumer (`src/game/frame.cpp`,
`clock_battery_draw`) prints the hour with `"%d:%02d"` and no AM/PM, and ignores the return, as
its ARM does — so that title's status bar now reads `15:30` where it read `3:30`. Cubis 2's code
proves the firmware's hour is 24-hour, so this is the more faithful of the two; it has not been
checked against anything of Mini Golf's own.

### 2026-08-29 — Menu, and where the frame time was going

**Menu ended the program from anywhere.** The game times a Menu *hold* and asks to be put away
when it passes a limit; left at zero, "when it went down" was the beginning of time, so every tap
read as a hold of the whole session. Its own lifecycle routine says where the stamp goes, with
`r9` = the input-state block at `0x180a9d9c` — whose `+0x14` is the button-flags word the emulator
already finds — and `r0` = the clock at `[ctx+4]`:

```
0x180386c8  ldr r1,[r9,#0x14] / and r1,r1,#0x10   ; is MENU down?
0x180386d8  ldrne r1,[r9,#0x18]                   ; when it went down
0x180386dc  subne r0,r0,r1 / ldrne r1,[r9,#4]     ; how long, against the limit
0x180386e8  strbhi sl,[r9] / strbhi r2,[r5]       ; past it: write 5 into [ctx+0]
```

So **`PRESS_TIME_MENU = 0x180a9db4`** and Next's at `+4`, stamped on the frame a press begins and
not while it is held — a real hold still crosses the limit. The emulator reports "no press-time
words for this title" because it only knows Mini Golf's hand-measured pair; this is the second
title to have them read out of its own code. `--emulator-firmware` leaves them at zero, because
that is what every recording holds. Menu now opens the pause menu, steps back a screen, and only
a *held* Menu asks for the way out — which is also how the window's close button now asks
(`ClickWheel::hold`), the tap it used to send having stopped working the moment this was fixed.

That also answers a question this plan has carried since block 0: **this game does write
`CONTEXT_STATE_SUSPENDED` at `[ctx+0]`**, and the sequence is Vortex's exactly — 1 while running,
5 on the frame the limit passes, 6 on the next.

**The frame time was going into thread hand-offs, not into pixels.** `sample` on an M1 Max at
render scale 4, ten threads: **45 % of all CPU in `__psynch_cvwait`** against 53 % inside
`fill_triangle`. The work-sharing test asked whether the *framebuffer* was big enough to be worth
sharing — which above scale 1 it always is — so every draw went to the pool, and this game issues
draws in the thousands a frame, one per letter of every string. Waking nine threads to paint a
5x9 glyph costs orders of magnitude more than painting it.

Two changes in `../common`, both measured on the level the picture oracle draws (1 500 frames):

| | scale 1 | scale 2 | scale 4 | scale 8 |
|---|---|---|---|---|
| before | 4.41 s | 7.09 s | 15.12 s | — |
| after | **2.26 s** | **6.43 s** | **13.80 s** | 14.55 s / 600 frames |
| | **1.95x** | 1.10x | 1.10x | 41 fps |

* the sharing test asks about the *draw's* bounding box, and stripes cover only the rows the draw
  reaches — a worker handed a stripe the draw cannot touch still walks three edge functions a row
  to find that out. `MIN_PIXELS_TO_SHARE` is 2 048 raster pixels, the best of six values tried at
  three scales;
* `* inverse_area` once a triangle instead of `/ area` twice a *pixel*. The two differ by at most
  an ulp, and the one place this rasteriser is sensitive to an ulp — the half-texel tie — is
  decided with a tolerance a thousand times wider, which is why that tolerance exists.

All six suites are green on both, picture oracles included.

**Not verified / not fixed:** at render scale 8 the software rasteriser draws 2 560x1 920 and
reaches ~41 fps, which is above the 30 the game runs at but has little headroom. The remaining
cost is genuinely per-fragment work in `fill_triangle`; specialising its inner loop on the
combination it is called with (textured / flat / tinted / filter) is the next thing to try, and
has not been.

### 2026-08-29 — what the small text is, and is not

The 7-pixel header font still looks poor, and it is worth being exact about why, because the
renderer is no longer the reason. Composing the `Arcade` run straight from `fonts/maiandra-7.raw`
using the game's own per-glyph `uv` and `xy` — the vertices it hands the driver — reproduces what
the recomp draws, letter for letter. The glyphs *are* the atlas cells the game names.

What makes it hard to read is the source: a 7-pixel face whose strokes never reach full alpha,
drawn in a dim cream (`mod [0.83 0.84 0.67]`) over a photographic backdrop, with a one-pixel black
shadow that fills the counters at that size. On a 2.5-inch screen at 320x240 that is legible; at
four times the size on a desk it is not.

`--hi-res-text` is the feature for this, and it did not engage on this font. Attempted below.

### 2026-08-29 — `--hi-res-text` on a 7-pixel face, and the bug that was already there

Asked to try making the glyph reconstruction work on the header font. It does now, and getting
there turned up a *pre-existing* fault in the same feature that mattered more than the request.

**What the old gate actually asked.** `is_text_run` required every cell of a run to hold a texel
at or above 250 — an opaque interior. Measured on this game's own `Arcade` run, cell by cell:

```
cell u  6-10 v 17-25  peak 170      cell u 14-17 v 35-43  peak 187
cell u  9-11 v 44-52  peak 153      cell u 18-21 v 35-43  peak 153
cell u 11-13 v 35-43  peak 136      cell u  3- 6 v 35-43  peak 153
```

`fonts/maiandra-7.raw` holds *two* texels at or above 250 in the whole 84x126 sheet; at 7 pixels
every stroke is thinner than one, so the face is antialiased into permanent translucency. One
failing cell rejects the run, so the gate was really asking "is this a big font" and the header
got plain sampling — **0 pixels changed** by the feature. `cell_coverage_peak` now returns the
cell's own peak (needing only a clear texel and `GLYPH_MINIMUM_INK`), the run takes the largest
of them as its ink, and the reconstruction measures coverage against *that* rather than against
255. With ink at 255 the expression is what it always was, so the big faces are untouched.

**The fault that was already there.** With the feature on, this game's HUD read `SCVRK` and
`CURFS` instead of `SCORE` and `CUBES` — at any render scale, and *before* any of this. Its
`rockart-11` face has opaque texels, so it passed the old gate; but only 27 % of its ink is at
full weight, and the contrast curve is a hard-edge model that erases anything under half
coverage. Three quarters of every ornate stroke went. `maiandra-7` is 15 %, so neither small face
fits the model the feature is built on.

Two changes make it safe rather than merely gentler — a contrast cap alone did not fix it:

* `GLYPH_MAX_CONTRAST` bounds the sharpening at two levels whatever the render scale;
* the reconstruction is **floored at the faithful nearest sample** — the texel the quad's own
  edges put at that pixel, which at 1:1 is exact. A feature that is off by default must never
  draw a glyph with *less* ink than leaving it off would, and that one line is what restores the
  letterforms. It also explains why the earlier attempts failed: `Filter::Glyph` samples
  bilinearly where a 1:1 blit otherwise gets `Nearest`, so it starts out thinner before the curve
  touches it.

Measured over the level frame, against the same frame with the feature off:

| | old code | now |
|---|---|---|
| HUD panel (`rockart-11`) | 4 525 px changed — the erosion | 2 316 px, letterforms restored |
| header (`maiandra-7`) | **0 px** — the gate rejected it | 729 px, smoothed, no ink lost |

All six suites green; the Glyph filter is behind `high_resolution_text && !emulator_graphics`, so
no picture oracle sees any of it.

**Not fixed.** The header is *smoother*, not sharper, and it cannot be sharper: a 7-pixel face
magnified four times has no detail to recover, and what makes it hard to read is the source — thin
translucent strokes in dim cream over a photographic backdrop, with a one-pixel shadow that fills
the counters. The setting is still off by default, with the rest of "what this renderer can do
that the iPod's could not".
