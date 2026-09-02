# Making a recomp of an iPod click-wheel game — the playbook

This is the plan that made *Mini Golf*, *Lost*, *Texas Hold'em*, *The Sims Bowling* and
*Vortex*, with
every title-specific fact taken out and replaced by the step that measures it. It is written to
be handed to an agent as one instruction:

> Read `recomps/NEW-RECOMP.md` and make a recomp for **GAME** using **FOLDER**.

where GAME is the title (as in `Manifest.plist`'s `Name`) and FOLDER is the game's directory as
copied off an iPod (`…/Games_RO/<id>/`, holding `Executables/<Name>_<ver>_<build>.bin`,
`Manifest.plist`, and the game's data). Everything below assumes those two inputs and nothing
else. Read `common/README.md` first — it says what is shared and what may not be assumed — and
`Mini Golf/PLAN.md` § "Code quality", which binds every recomp unchanged.

The deliverable is a directory `recomps/GAME/` in which the game **runs natively as a pure
static recompilation, proven identical to the emulator** on recorded scripted sessions — call
for call, word for word, and pixel for pixel on screenshots — with a plan of record that says
what was established and what was not. Hand decompilation starts after that and is not part of
"done" for the first pass.

---

## 0. What you are building

Every title has the same shape, and the shape is not up for redesign:

* The ARM image is **statically recompiled** by the shared recompiler
  (`common/tools/recomp/`): one C++ function per ARM function, running the same instructions on
  a small CPU-state struct. That is the pure recompilation, and it is the oracle that never
  expires.
* The iPod's application frameworks (OpenGL ES, audio, file I/O, the click wheel, settings, the
  music library) are reimplemented as a host library, `libeapp`, behind typed C++ interfaces in
  `src/framework/`. What is the same for every title lives in `common/` and is compiled from
  there; what differs by a *measured fact about this binary* lives in `recomps/GAME/`.
* The verification oracle is the emulator (`tools/eapp-loader` + `tools/arm7tdmi`), **pinned**
  as a copy under `recomps/GAME/tools/oracle-emulator/` at a named commit, never the live tree.
  A scripted session is recorded from it as a framework-call log and compared with the recomp's
  log of the same script (`tests/diff.sh`); screenshots are compared at a threshold
  (`tests/frames.sh`).
* The frame pump (`src/runtime/main.cpp`) copies the emulator's pump step for step, because the
  logs are compared call for call, and it is where a title's *protocols* live: how the firmware
  tells the game why it is being called, and how a button press reaches it.

Nothing in the emulator tree is edited for a recomp. If the emulator is wrong about the title,
the fix goes in the emulator tree in its own commit, followed by a re-pin and re-recording.

---

## 1. Before writing a line: measure the title (about an hour)

Do all of this with the **newest existing title's tools** (the one whose `PLAN.md` was started
most recently — the chain so far is Mini Golf → Lost → HoldEm → Sims Bowling → Vortex, each
ported from the one before; the newest is the one to port from too) run against the new image, and put everything under
`recomps/GAME/analysis/` with a `README.md` saying how each file was made. Every number in the
plan you write in § 2 must be answered by a file here.

1. `python3 <newest>/tools/survey.py --image FOLDER/Executables/<image>.bin > analysis/survey.txt`
   — the header, the entry vectors, the frameworks and their thunk counts, and how many
   functions and instructions a static walk from the vectors reaches, and how many it cannot.
   `../target/release/eapp-inspect <image>` (build it if needed) is a second opinion on the
   header. If the walk fails on an instruction shape, that is a **shared-recompiler** job (§ 4,
   block 1), not a reason to stop.
2. Find the title in the emulator's per-title defaults table: `defaults_for` in
   `tools/eapp-loader/src/bin/play.rs`, matched on the executable's name. It records the
   frame-reason protocol, the file model, the reason-byte seed and the frame budget the emulator
   needed to run this title, with the disassembly addresses that justify each. Then
   `grep -n -i 'GAME' tools/eapp-loader/src/lib.rs tools/eapp-loader/src/bin/play.rs` — the
   emulator's comments that name the title are the best specification of its quirks there is,
   and they cite addresses. Copy those addresses into your plan; they are what you will read.
3. Run the emulator on it. Build any title's pinned copy (or the live tree at the commit you
   will pin) and run, always on a **private copy of FOLDER** (the game writes into its folder):

       play FOLDER/Executables/<image>.bin --gamedir=FOLDER-copy \
           --load-on-open --allow-creates --fixed-clock --fps=0 \
           --script=boot.script --call-log=boot.calls --callgraph-dump=edges-boot.txt

   plus whatever `defaults_for` applies to this title spelled out explicitly. Start with a
   silent boot (`600: quit`, then `3000: quit`) and read: how many framework calls a frame, which
   ordinals are reached (the `summary:` block), when file activity ends, and what is on screen
   (`N: shot` writes `/tmp/ipod-shot-NN.png`). Then probe input: `select`, `menu`, `prev`,
   `play`, `next`, `wheel ±N`, each with screenshots, until you know how to reach the game's
   first playable screen. Keep every script and screenshot; they become `analysis/coverage/`.
   **Warning (Vortex's rule 11):** `play` always opens a window — a click on it is a Select —
   and every emulator writes its screenshots to the same `/tmp/ipod-shot-*.png`. Never run two
   at once, never run one while another title's `frames_*` tests run, and use the newest
   title's `tools/probe.sh` and `tests/record.sh`, which check for both symptoms rather than
   letting a stray click or a foreign screenshot into the evidence.
4. Join the reached ordinals against every existing title's `src/libeapp/imports.json`
   (`analysis/ordinals.txt`). The ones no title has named are work: name them from the pinned
   emulator's `lib.rs` stubs and `Mini Golf/reference/reversing/*.json`. **An ordinal a title
   calls and nobody implements still logs its call**, so the call-log oracle passes it by
   construction; only the picture catches it. This list is the first place to look when a
   picture is wrong.
5. Rehearse the seed and the emit into a scratch directory with the newest title's `funcs.py`
   and `emit.py` (`--image`, `--edges`, `--output`/`--out` take paths). Note the counts: seed
   entries, stored function pointers, functions and instructions emitted, and any failure.
6. List the game's files: formats, sizes, what is a save (a file in FOLDER that the manifest's
   `Files` list does not name was *written by the game* — exclude it from the manifest and strip
   it from every test copy), which files are music (`.m4a`) and which are sound effects.

---

## 2. Write the plan, then the scaffold (block 0, ~30 min)

`recomps/GAME/PLAN.md` is the plan of record. Its shape is the one every title uses; copy the
newest one's headings and replace the content:

* **What GAME is** — a table of measured numbers (image, size, load address, vectors, functions
  from the vectors and after the fixpoint, instructions, thunks per framework, ordinals reached,
  what the emulator reaches on screen and by which frame), then the title's lineage: its build
  number against the others' says which SDK it is from, and the import layout says whose
  `libeapp` model it is closest to.
* **Code quality** — Mini Golf's rules, plus the additions each title made (7 evidence, 8
  provenance, 9 nothing copied that the core can carry, 10 shared-core changes verified in every
  title before use). Add one only if this title teaches one.
* **What is inherited, from where** — the layer table: which files come from `common/`, which
  are copied from the newest title by the port script, which are this title's own.
* **Architecture** — the directory layout (below).
* **Where GAME is none of the others** — the numbered differences, each a measured fact with the
  address or the probe that established it. § 3 says what to look for.
* **Schedule** — the blocks of § 4 with this title's numbers, an exit criterion and a fallback
  for each.
* **Risks, in the order they will bite**, and **Not today**.
* **Progress log** — appended as work happens; each entry says what was *established* and ends
  with what was **not verified**.

Then the scaffold:

1. **Create the directory** `recomps/GAME/` (the name is the title's, with spaces if it has
   them — `Sims Bowling`, `Mini Golf`) with `analysis/`, `reference/`, `tools/`, `tests/scripts/`,
   `tests/expected/`, `src/game/` (empty). `recomps/` is gitignored at the repository root; the
   directory is the deliverable and the plan is its record.
2. Choose the **namespace**: one lowercase word (`bowling`, `holdem`, `lost`, `minigolf`). It
   becomes the C++ namespace, the CMake targets (`GAME` and `GAME-headless`), the option prefix
   (`GAME_*`), the environment variables and the Linux data directory. The macOS data directory
   is `iPod <Title>`. A `bowling`/`holdem`/`lost`/`minigolf` identifier anywhere in the new tree
   that is not this title's is a defect.
3. **Port from the newest title** with its port script: copy `tools/port-from-<prev>.py`, rename
   it `port-from-<newest>.py`, point `SOURCE` at the newest tree, and set `REWRITES` to map its
   namespace, prefix and data-directory strings to yours. Run it. It writes
   `reference/PORTED.md` with the SHA-256 of every file at the moment of the port;
   `--check` reports drift later. **Identifiers are rewritten; prose is not**: a comment that is
   true of the previous title stays true rather than being made false, and
   `grep -rn "<PrevTitle>\|<OtherTitles>" src tests tools` is the worklist for adopting each file.
4. **Pin the emulator**: copy `tools/eapp-loader/src` and `tools/arm7tdmi` at a named commit to
   `tools/oracle-emulator/` (the newest title's copy is the same thing if the commit matches),
   and write `reference/MANIFEST.md` with the commit *and* the SHA-256 of `lib.rs`, `play.rs`
   and the `arm7tdmi` sources, checked against `git show <commit>:tools/…`. Build it:
   `cargo build --release --manifest-path tools/oracle-emulator/Cargo.toml --target-dir build/oracle-emulator`.
5. Adopt the title's constants: `src/gamedata/manifest.h` (`GAME_DIRECTORY_NAME` = FOLDER's
   name, `GAME_IMAGE_PATH`), `tests/game-dir.sh` (the same two, and the save file to strip from
   every copy), `tools/funcs.py` (`DEFAULT_IMAGE`), `tools/manifest.py` (what to ignore — the
   save by name), `tests/unit/install_test.cpp`, `CMakeLists.txt`'s first line, and the
   window title in `main.cpp`. Then `python3 tools/manifest.py FOLDER` →
   `src/gamedata/manifest_data.cpp`.
6. `cmake -B build` configures. `python3 tools/survey.py` reproduces `analysis/survey.txt`.

---

## 3. What to look for: the differences a title can have

Each title so far was "neither of the others" in some of these. Establish each from the
emulator's notes and the disassembly, write it into the plan as a numbered difference with its
evidence, and make it a named constant with that paragraph beside it. Never a silent edit to a
copied file.

**The frame-reason protocol.** The firmware calls the frame vector with `(ctx, ctx+0x100)` and a
reason byte at `ctx+0`; the game may answer at `ctx+0x100`. Four variants are known: a constant
(Mini Golf, 5), a first-frame value (Lost, `first0:1`), a seeded init (Hold'em, seed 0 then 1),
and a two-way handshake (Sims Bowling: ask for init with 0 until the answer byte is non-zero,
then 1). `defaults_for` names the emulator's, and the game's dispatcher — find it from the frame
vector, it reads `ldrb r0,[ctx]` and branches — is the authority. **Read the other direction
too**: a game that writes 5 to the answer byte is asking to be put away, and one that is asked
with reason 5 shuts down and answers 6 (Lost and Sims Bowling both). A pump deaf to that freezes
on Save & Exit. Honour it in a real run (idle requests excepted); never under the oracle, which
reproduces an emulator that honours nothing.

**How a button reaches the game.** Either a *flags word* the game polls (Mini Golf, Hold'em —
the emulator finds it from a `bic #0x60` signature and prints its address), or an *event list*
at `ctx+0x30` of `{type, state, payload, next}` nodes (Lost, the Sims titles — "no button flags
word for this title" at start-up). For the event list, read the game's dispatcher (the function
the frame path hands `[ctx+0x30]` to): which state byte it treats as *down* and which as *up*,
and which button type it times as a hold. In Sims Bowling **state 2 is down and 1 is up**, the
reverse of the emulator's names and of the order it posts them; the oracle keeps the emulator's
order and timing exactly (a press at frame N, the second state at the top of N+1, the head
nulled at N+2), and a real run sends down on the press and up when the key is released
(`FrameInput::buttons_down`). Whatever you find, the **wheel** is the emulator's: `WHEEL_DETENTS`
and `wheel_byte` copied, one sample per detent, nothing refilled between frames unless the
recordings were made with `--wheel-rotate`. Measure detents per menu row in the emulator with
`wheel ±N` and screenshots.

**The file model.** Opens that carry a buffer may be loads (`--load-on-open`); completions are
delivered between frames (the emulator's default); and there are per-title rules the emulator
learned the hard way, all in `lib.rs`'s `Stub::AsyncOpen`/`AsyncRead` comments: a bufferless
open's result is the file size in `[obj+8]`; a seek must return non-zero; a read publishes its
byte count at `req+0x24`; a header-sized buffer is filled; one operation, one completion. The
boot oracle will name the first one you got wrong, by line. Saves: what the game writes, where,
and whether the emulator's write paths (`write_on_open`, `op3_writes`) were on when the
recordings were made — they are off by default, and the oracle runs with writes refused.

**The GL driver.** Which family: Mini Golf's (float matrices, no constant colour, attributes
pointed once) or Lost's (render server `#152/#153/#159/#164`, fixed-point matrices `#149`,
constant colour `#147`/`#148`, paletted textures). Two shared-rasteriser rules need every
title's answer and only the picture gives it: whether the game re-points every attribute before
every draw (`gfx::set_attributes_repointed_per_draw`; read the routines that issue the draws —
Sims Bowling's flat rectangle points attribute 0 only and leaves 1 stale, so the conservative
reading smeared its menu bar), and whether it ever writes the constant colour. An ordinal in
`analysis/ordinals.txt` that nobody implements (Sims Bowling: `glDrawElements`) is a shared-core
addition, verified in the other titles first.

**Sound.** Bank files the game parses itself and hands over as PCM (Mini Golf, Lost, Hold'em),
or loose `.wav` the game reads through `AsyncFileIO` (Sims Bowling). Music is `.m4a` through the
shared decoder. Unnamed `Audio` ordinals get their names from `play.rs`'s stubs.

**The data.** Note the formats nobody has read yet; they are decompilation targets, not
recompilation blockers — the game reads bytes through the file framework and decodes them
itself.

**Memory.** Reads outside the mapped regions: the emulator answers them with zero and carries on
(`note_unmapped`). If the title makes them (Sims Bowling reads `0x01400010`), `memory.cpp` has to
do the same, counted and reported; the origin was a recompiler bug there, and finding it
before answering "as the emulator does" is worth an hour.

---

## 4. The schedule

Each block ends in something runnable *and* reviewed against the quality rules. Cutting quality
is never the fallback.

**Block 1 — emit (30–45 min).** `tools/funcs.py` (vectors, live edges from every
`analysis/coverage/edges-*.txt`, stored function pointers) then `tools/emit.py`
(`Generator(namespace="…")`). Expect the rehearsal's numbers. If the recompiler cannot walk an
instruction shape, fix it in `common/tools/recomp/` and **verify before use**: Mini Golf's
`tests/check-recomp.sh` unchanged (5 of 6 exact today; `next-hole` diverges as it always has),
every other title's `gen/` re-emitted byte-identical (`emit.py --out <scratch>` then `diff -rq`),
their `ctest` green. `gen/` compiles under the `<ns>_generated` warning regime.

**Block 2 — runtime and the pump (1 h).** `main.cpp` around the protocol and the input model
of § 3, as named constants with the dispatcher addresses cited. Every ordinal logs and returns
0. **Checkpoint A:** `--frames=2` runs the start-up vector and the first frames, and the log
begins as the emulator's did.

**Block 3 — the oracle (30 min).** `tests/record.sh` spells out every flag a case is made with
(`--load-on-open --allow-creates --fixed-clock --fps=0`, plus this title's `defaults_for`
entries written out, so no case depends on a table in another tree). Record short cases that end
in `quit`: a boot, the first screen with a `shot`, the path to play. `imports.json` from the
union of the other titles' tables plus this title's unnamed ordinals, with argument counts.
**Exit:** `tests/diff.sh boot` reports its first divergence with a line number.

**Block 4 — libeapp, in boot order (3 h).** Re-run `diff.sh boot` after each group: miscTBD /
Settings / InputEvents → AsyncFileIO (expect the fight here) → Metadata → Audio → OpenGLES. A
divergence is a question, not a verdict: read what `lib.rs` does at that call before writing.
When the *semantic* log agrees but the *exact* one (`--exact`) does not, the difference is a
leftover register or stack word; if it is the emulator writing the real wall clock into the
game (`miscTBD #12`), `tests/exact-allow.txt` names the ordinal and `diff.sh --exact` drops it
from both logs — and nothing else. **Checkpoint B:** every case identical, semantic and exact.

**Block 5 — SDL3 and the picture (1 h).** `frames.sh` compares screenshots at a threshold. A
recomp screenshot for frame N is taken **before** the frame call, as the emulator's is (a
moving element shot after the call is one step ahead). Then the SDL window. **Checkpoint C:**
the first screen on screen, natively, with the keyboard.

**Block 6 — the first oracle that plays, then decompilation.** Script the path into real play
(a hand dealt, a ball rolled) with screenshots and record it — this is the case that finds
recompiler bugs, because play exercises code the boot never touches. Then `vs-recomp.sh` /
`check-recomp.sh` from Mini Golf, and the swap loop: string helpers and dispatchers first,
loaders next, the engine last, each swap diffed.

---

## 5. When the oracle disagrees: the tools, in the order to reach for them

* `tests/diff.sh <case>` names the first differing call and its line. Look at the *previous*
  frames' calls: the divergence is usually a return value or a memory write no log shows.
* The recomp's `--trace-entry=ADDR,…` (registers at every entry) against the emulator's
  `--watch-pc=ADDR,…` (its last ten arrivals, printed in the diagnostic block a `shot` triggers —
  so put a `shot` on the frame after the one you care about). The first entry whose registers
  differ is upstream of the fault. `--trace-from=N` keeps the trace quiet until frame N, so
  *every* function can be traced for one frame: build the list from `gen/src/*.cpp`'s
  `void f_…` definitions, not from `funcs.json` — the emitter discovers hundreds more than the
  seed names.
* `--dump-frame=ADDR:BYTES:FROM` (hex) against the emulator's `--dump-mem=ADDR:N` at a `shot`,
  remembering that the emulator's shot shows the state *before* that frame.
* `BOWLING_WATCH`-style store watches (the title's `<NS>_WATCH`, `_WATCH_LOG`) against
  `--watch-mem`. `<NS>_TRACE_FILES=1` against `--file-ops=N`. `<NS>_TRACE_UNMAPPED=1`.
* A function that **returns with SP wrong** (the next call from its caller runs at the callee's
  frame depth in the trace) is the recompiler treating a register jump as a tail jump when it
  was a call: check `common/tools/recomp/cfg.py`'s link scan. It has bitten three times.
* `call_indirect`'s fatal names the target, the return address, SP and r0–r5. A target that is
  a real function the seed lacks goes into the seed (a stored pointer the walk dropped — check
  why — or `analysis/extra-entries.txt`); a garbage target with a garbage return address is a
  stack already corrupted, usually by the bug above.
* The picture oracle: `IPOD_VERTEX_HASH=1` on the recomp against `--draws=N` on the emulator
  compares draws one by one; `IPOD_VERTEX_DUMP=<draw>` prints one draw's vertices. Diff the two
  screenshots into an image (a few lines of Python over `tools/ppm2png.py`'s readers) before
  theorising about which draw it is.

---

## 6. What "done" looks like

* `recomps/GAME/` with `PLAN.md`, `README.md` (layout, building, testing, playing, debugging
  aids, status), `analysis/README.md`, `reference/MANIFEST.md` and `reference/PORTED.md`.
* `ctest --test-dir build` green: the unit tests, every recorded case in semantic and exact
  form, every screenshot case. The SDL target builds and reaches the first screen.
* Every other title still green after any shared-core change (rule 10), and `common/README.md`
  updated with what moved or changed and why.
* A `tools/port-from-<GAME>.py` in the *next* title is how this one's work travels on: leave
  the tree in a state the port script can copy from — prose true, constants in the files § 2.5
  names, nothing title-specific hiding in a shared-looking file.
* The progress log says what was verified, how, and what was not.
* The user's memory file for the project updated with the title's one-paragraph state.
