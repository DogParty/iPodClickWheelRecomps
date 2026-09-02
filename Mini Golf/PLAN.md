# Mini Golf recomp — plan of attack for one day

**Goal by end of day:** `Minigolf_1_1_2563296.bin` running natively on the ARM Mac as a
statically recompiled C program, drawing into an SDL3 window, driven by keyboard, and proven
equivalent to the emulator by diffing the sequence of framework calls both make on the same
scripted input. Hand decompilation *starts* today (the swap workflow is proven on a handful of
functions); it does not finish today — 304 functions do not get rewritten by hand in a day.

Everything lives in this folder. The emulator tree is touched only for one small instrument
(a `--call-log=FILE` flag on `play`, see step 3) and is otherwise left alone.

**The emulator tree is a moving target** (other people and agents are changing it while this
work happens), so every emulator file this plan cites — `lib.rs`, `play.rs`, `arm.rs`, the
`reversing/` dossiers, research/13 — is snapshotted in `reference/` with a `MANIFEST.md` giving
the commit and checksums. Code and comments cite the **`reference/` copies**. Refreshing the
snapshot is a deliberate step: re-copy, update the manifest, re-check every citation.

All numbers below come from `analysis/` (Ghidra 12.1.3 headless pass, two interactive sessions):

| | |
|---|---|
| game functions | **304** (20 942 ARM instructions, `0x18002c28–0x18018e40`) |
| ARM C runtime | 53 functions (`0x18000a3c–0x18002c28`) — replaced by libc, never recompiled |
| import thunks | 277 stubs, **65 ordinals actually used** |
| live coverage from play | **256/304 functions, 92 % of instructions** (`analysis/coverage/`) |
| code properties | ARM state only, no Thumb, no floats, no VFP, 20 indirect call sites, 25 jump tables, 354 `b .` asserts |

---

## Code quality — non-negotiable, applies to every block below

This code will be read by many people. It must read like a project written by someone who
cares, not like a disassembler's exhaust. Speed today never buys a pass on any of this; a block
is not "done" until its code meets the bar.

**0. Language: C++17, everywhere.** The original game is C++ (armcc's C++ runtime is in the
binary — `Pure virtual fn called`, vtables, `new`/`delete` wrappers), so hand decompilation
recovers classes, not structs-with-function-pointers. C++17 rather than 20 because every target
toolchain (Apple clang, MSVC, NDK, MinGW, devkitARM gcc for 3DS) supports it fully. Generated
code is emitted as `.cpp` too, so there is no C/C++ boundary or `extern "C"` shim inside the
project. The *style* is "C++ as a better C" in the runtime and generated layers (no exceptions,
no RTTI, no dynamic allocation in the hot path) and idiomatic modern C++ in `libeapp`,
`platform`, and `decomp`.

**1. Human-readable above all.** Every hand-written file (`tools/`, `src/runtime/`,
`src/libeapp/`, `src/platform/`, `src/game/`, `tests/`) must be understandable by a competent C
programmer who has never seen an iPod binary. If a reader needs the disassembly open to follow
hand-written code, the code is wrong. No clever macros, no 300-line functions, no magic numbers
without a named constant or a comment saying where the number comes from.

**2. Names mean things.**
* Functions, variables, types, constants, files: descriptive, consistent, in one style
  (`snake_case` for C functions/variables, `UPPER_SNAKE` for constants and macros, `PascalCase`
  for struct typedefs). No `tmp2`, `foo`, `do_thing`, `x1`.
* Framework ordinals are never called by number in hand-written code. `ipod_eapp.h` gives each
  one its real name (`gles_draw_arrays`, `audio_play_sound`, `afio_open`, …) with the ordinal
  noted in a comment beside it. `eapp_OpenGLES_37` may exist only as the *generated* binding the
  emitter targets, never in code a person writes.
* Hand-decompiled functions (`src/game/`) get real names the moment they are understood
  (`ball_apply_friction`, not `f_1800c2e0`), and the address lives in a comment on the definition.
  A function that cannot yet be named honestly is not ready to be moved out of `gen/`.
* Recovered globals and struct fields are named in one shared header (`src/game/game_state.h`)
  as they are identified; the same field is never called two things in two files.

**3. Commented properly, not exhaustively.**
* Every file opens with a short comment: what it is, how it fits the whole, anything a reader
  must know before changing it.
* Every non-trivial function has a comment stating what it does, what it assumes, and — for
  ported or decompiled code — where the behaviour was established (`lib.rs` stub, a research
  note, an address in the binary). Cite sources; a future reader should be able to verify a claim.
* Comment the *why* and the non-obvious (fixed-point scaling, an off-by-0x128 relation the game
  asserts, why a flag must be computed eagerly). Do not comment the obvious (`i++; // increment i`).
* `TODO`/`FIXME` carry an owner-less but specific note of what is unknown and how to find out.

**4. Maintainable structure.**
* Small functions with one job; one concept per file; no file over ~600 lines of hand-written code
  without a reason stated at the top. The emitter is split into modules (decode, analyse, emit)
  rather than one script.
* Generated code lives only in `gen/` and is never hand-edited; it is clearly marked as generated
  in its header, with the emitter version and command that produced it. Generated code should
  still be *legible* — consistent formatting, the source address on each labelled block, the
  original mnemonic as a trailing comment on lines where the C is not self-evident — because people
  will read it while decompiling.
* No dead code, no commented-out blocks, no "temporary" hacks left in. If something is a crutch
  (`afplay` for AAC), it is isolated behind a clear interface and labelled as such.
* Portability is designed in, not bolted on: platform code stays in `src/platform/`, everything
  else is plain portable C with no host assumptions (endianness, pointer width) hidden in it.
* Build warnings are errors (`-Wall -Wextra -Werror` on hand-written code). Formatting is
  mechanical: a `.clang-format` in the repo, and every commit is formatted.

**5. C++ best practices, specifically.**
* Compile with `-std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`;
  `-fno-exceptions -fno-rtti` project-wide (the guest code never throws; keeps 3DS honest).
  Debug builds run under ASan/UBSan; the oracle tests run sanitised at least once per block.
* Fixed-width types only (`uint32_t`, `int16_t`) for anything that mirrors guest memory or the
  16.16 fixed-point math; never `int`/`long` for guest data. Wrap fixed-point in a small
  `Fixed16` type with explicit operators so `x * y` can't silently lose the `>> 16`.
* `enum class` for every enumeration (framework ordinals, button bits, GL enums, audio states);
  `constexpr` instead of `#define` for constants; macros only where the emitter's generated code
  genuinely needs them (`LD32`/`ST32`, flag helpers), each documented.
* `const`/`constexpr` by default; `[[nodiscard]]` on anything returning a status; no raw
  `new`/`delete` in hand-written code — `std::unique_ptr`, `std::vector`, `std::array`, or a
  named arena for guest-heap emulation.
* Headers: one `#pragma once`, minimal includes, forward declarations where they suffice; no
  `using namespace` in headers; everything in namespace `minigolf` with sub-namespaces per layer
  (`minigolf::eapp`, `minigolf::platform`, `minigolf::game`).
* Ownership and lifetime are explicit: who owns a handle, a texture, a file request is stated at
  the declaration. RAII for every host resource (SDL window, audio device, file). No globals
  except the guest address space and the `Cpu` state, both behind accessor functions.
* No undefined behaviour as a feature: guest loads/stores go through `memcpy`-based helpers
  (no type-punning casts), shifts are range-checked as the ARM rules specify, signed overflow is
  handled with unsigned arithmetic and explicit casts.
* Error handling is uniform: a fatal guest condition calls one `fatal(...)` that prints the guest
  PC and call log tail; host-API failures return a typed result or log and degrade, never silently
  `return 0`.
* Tests are code too: `tests/` scripts and the diff harness follow the same rules, and unit tests
  (`tests/unit/`, plain Catch2-free `assert`-style to keep deps at zero) cover the flag/shift
  helpers against vectors taken from `arm.rs`.

**6. Reviewed before it counts.** The last step of every schedule block is a read-through of
everything written in that block against this list — renaming, splitting, commenting, deleting
— *before* moving on. A `README.md` in this folder explains the layout, how to build, how to run
the tests, and how to contribute a decompiled function, and is kept current as the layout evolves.

---

## Architecture (decide once, now)

```
recomps/Mini Golf/
  tools/emit.py            ARM → C++ static recompiler (Python, reads the .bin + functions.tsv)
  tools/DumpFuncs.java     Ghidra headless post-script (already here)
  gen/                     GENERATED, never hand-edited: game_NNNN.cpp, funcs.h, calltable.cpp
  src/runtime/             cpu.h (regs/flags/memory helpers), main.cpp (frame pump, script player,
                           call log), mem.cpp (guest address space)
  src/libeapp/             the host library: one .cpp per framework, named functions (ordinal in a comment)
    include/ipod_eapp.h    the 8 frameworks as named C++ functions — shared by gen/ and decomp/
  src/platform/sdl3/       window, present(), audio out, keyboard → detents/buttons
  src/game/              hand-written replacements; each one removes a gen function from the build
  tests/scripts/*.script   same FRAME: ACTION format play uses
  tests/expected/*.calls   framework-call logs recorded from the emulator
  CMakeLists.txt
```

**Guest memory.** One `uint8_t *G` reservation covering `0x11000000..0x40020000` (mmap,
`PROT_READ|WRITE`, `MAP_NORESERVE`; untouched pages cost nothing). Every guest access is
`LD32(a)`/`ST32(a)` = `*(uint32_t*)(G + (a) - GUEST_LO)`. Same layout as the emulator so
addresses in logs line up: image at `0x18000000` (+8 MB BSS span), heap `0x19000000` (64 MB),
stack in `0x11000000` (8 MB), IRAM `0x40000000` (128 KB). Host library functions receive guest
pointers as `uint32_t` and use the same macros — portable to 32-bit targets later (3DS would shrink
the reservation; nothing else changes).

**Generated code shape.** One C function per ARM function, `void f_18002c28(Cpu *c)`:

* registers `c->r[16]`, flags `c->n,z,cv` as plain ints, computed eagerly with the exact
  `add_with_carry` / barrel-shift rules from `tools/arm7tdmi/src/arm.rs` (those are proven; port
  them as macros, do not re-derive).
* intra-function branches → `goto L_1800xxxx;` (every instruction gets a label — the compiler
  strips unused ones).
* `bl f` → `f_XXXX(c);` direct call. `bl <thunk>` → `eapp_OpenGLES_37(c)` etc. from
  `ipod_eapp.h`. `b f` where `f` is another function entry → `return f_XXXX(c);` (tail call —
  armcc emits these constantly).
* `bx lr`, `pop {…, pc}`, `mov pc, lr`, `ldmfd sp!, {…pc}` → `return;`.
* indirect (`mov lr,pc` + `mov pc,rN`/`bx rN`, 20 sites) → `call_indirect(c, c->r[N])`: a
  generated `switch` over every function entry point (`gen/calltable.cpp`). Unknown target = abort
  with the address printed.
* jump tables (`addls pc,pc,rX,lsl #2` / `ldrls pc,[pc,rX,lsl #2]`, 25 sites) → the emitter
  resolves the table statically (both armcc idioms are regular) and emits a C `switch`.
* `ldr rX,[pc,#imm]` → constant (the literal is in the image; fold it).
* `b .` → `eapp_assert_trap(0x1800xxxx);` (354 of these: free invariant breadcrumbs).
* `svc 0x123456` (3 sites) → `eapp_semihost(c)`, logs and returns.
* conditional execution → `if (COND_xx(c)) { … }` around the statement.
* **Function list** = union of Ghidra's 415 entries (`analysis/ghidra/functions.tsv`) and every
  branch target in `analysis/coverage/edges-*.txt` (the live entry points). The 53 runtime
  functions and 58 thunks are excluded from emission and bound by name instead.

**ARM C runtime → libc binding (no emission).** The 32 runtime entries game code calls reduce to:
`memcpy 0x18001124`, `memset 0x18001190`, `__rt_udiv 0x18001468` (returns quotient in r0,
remainder in r1 — keep that), `__rt_sdiv`, the 64-bit divide `0x18000ba4`, strlen/strcpy/
wide-string helpers at `0x180092bc/0x180094a0` (those are game code, leave them), heap
alloc/free wrappers (`0x18000b64/0x18000b80` → forward to `miscTBD #0/#1`), the init/exit shims
(`0x18000e84`, `0x180015cc`, `0x180011e8`), the signal raisers. Each gets a hand-written C body
in `src/runtime/rt_arm.cpp` operating on `Cpu *`.

**Host library `libeapp` = port of the Rust stubs.** `eapp-loader/src/lib.rs` `Stub` variants
are the spec; `reversing/asyncfileio-abi.md` is the commentary. Day-one scope is exactly
Mini Golf's 65 ordinals (`analysis/ghidra/mg-ordinals.json`):

| framework | ordinals | day-one implementation |
|---|---|---|
| OpenGLES (20) | 4 12 13 19 21 36 37 40 53 84 99 101 125 137 157 158 159 165 167 175 | software rasteriser, a port of `Machine::draw_arrays` (69 lines) + texture table + the §17 pipeline semantics; `#157` swap → `platform_present()` |
| Audio (25) | 0 1 2 7–15 17 18 23 40 42 43 45 48 51 52 53 55 56 | handle table with the field offsets from §18.0.2; `#2` play → SDL3 audio stream with the `.wav` from `cNNbank/`; music `#40/#48` → `afplay` child process exactly as `play.rs` does (AAC; proper decoder is a later milestone) |
| miscTBD (10) | 0 5 6 7 9 10 11 12 13 14 | alloc/free (bump + free list), level trio, µs clock (must *move*), 1000/0 constants, wall clock, battery, resource-name resolve |
| AsyncFileIO (8) | 0 1 2 3 4 12 14 16 | open/read/close against `--gamedir`, request object offsets `REQ_CALLBACK`/`REQ_CONTEXT` from lib.rs, completions queued and fired by the frame pump; `#12/#14` logged and answered 0 (unknown, see risks) |
| InputEvents (1) | 0 | detent counter + button bits, same flags-word mechanism `play` uses (`0x18037a0c`) |
| Settings (1) | 0 | `"Language"` → 0 |

**Frame pump** (`src/runtime/main.cpp`) — copy `play.rs` exactly: scratch ctx of 0x400 bytes,
`[ctx+0]=5`, args `(ctx, ctx+0x100)`, run every non-zero vector once, then the last one per frame;
reason byte auto logic (`[ctx+0x100] != 0` → steady reason); drain file completions before each
frame; keep one wheel sample in flight per frame; clear held button bits at frame start.

**Verification oracle.** Both sides emit the same line per framework call:
`FRAME  Framework#ord  r0 r1 r2 r3  sp0 sp1 sp2 sp3  from PC`. The emulator already formats this
(`Call` struct in lib.rs, printed as "last 24 framework calls"); step 3 adds `--call-log=FILE` to
dump all of them. A test = script + expected log; `diff` decides. Framebuffer hashes at `shot`
frames are the second channel once the rasteriser is ported.

---

## Schedule

Times assume a focused ~10-hour day. Each block ends in something runnable *and* reviewed against
the code-quality section; if a block overruns, the fallback is named. Cutting quality is never the
fallback.

### 0 · 20 min — scaffold
* `README.md`, `.clang-format`, `.gitignore` (`gen/`, `build/`), and the quality rules above
  wired into the build (`-Wall -Wextra -Werror` for hand-written targets).
* `CMakeLists.txt` (clang++, C++17, `-O1 -g`, SDL3 via `pkg-config`), empty targets: `minigolf`
  (runtime + gen + libeapp + sdl3) and `minigolf-headless` (same, null platform, for CI/diffs).
* `tools/funcs.py`: merge `functions.tsv` with the edge-dump targets → `gen/funcs.json`
  (entry, end, name, kind ∈ {game, runtime, thunk}). Print the count. Expect ~304 game entries
  ± a few that the live edges split or merge.

### 1 · 2.5 h — `tools/emit.py`, the ARM→C++ emitter
Order of work, each step compiled and smoke-tested against `gen/`:
1. decoder for the ARMv4 subset present (count from `objdump.txt`: data-processing, mul/mla,
   smull/umull, ldr/str (+byte, +halfword/signed), ldm/stm, b/bl, bx, swp is unlikely, no
   coprocessor, no Thumb).
2. data-processing + flags macros, conditionals, literal folding.
3. branches: local `goto`, direct/tail calls, returns, thunk calls by name.
4. ldm/stm with writeback, `pop {pc}` detection.
5. jump tables (resolve both idioms), indirect call table.
6. `b .` → assert trap, `svc` → semihost hook.
7. **Self-check:** every `bl` target in the image is either a known function, a thunk, or a
   runtime entry; the emitter aborts on anything else. Compile `gen/` with `-Wall -Werror=implicit-function-declaration` — unresolved names are the emitter's bug list.

*Fallback if the emitter is not clean by hour 3:* emit only the 256 functions the two play
sessions reached, stub the remaining 48 as `abort("unreached: 0x…")`. They are all small and none
are on the path to a played hole.

### 2 · 1.5 h — runtime + ARM-library bindings
* `cpu.h` helpers, `mem.cpp` reservation + image load (parse the eapp header the way
  `EApp::parse` does: load base from `+0x10`, vectors at `+0x14`, BSS span).
* `rt_arm.cpp`: the ~12 runtime semantics listed above.
* `main.cpp`: frame pump, `--script`, `--call-log`, `--frames=N`, `--gamedir`.
* **Checkpoint A:** headless build runs the three init vectors and the first frame with every
  framework ordinal logging and returning 0. Expected: same first 7 calls the emulator makes
  (`miscTBD #0` alloc, `#9` clock, `InputEvents #0`, `OpenGLES #12 #13 #157`).

### 3 · 30 min — the oracle (one emulator-tree change)
* Add `--call-log=FILE` to `tools/eapp-loader/src/bin/play.rs`: write every `Call` the
  machine records, in the format above. ~15 lines next to `--callgraph-dump`. (If you would
  rather not touch the emulator today, `trace` already prints the full call list for the first
  88 k instructions — enough for Checkpoint A, not for B.)
* Record `tests/expected/boot.calls` (no input, 300 frames) and `tests/expected/name-entry.calls`
  (`tests/scripts/name-entry.script`).
* `tests/diff.sh`: run headless recomp with the same script, diff first N lines, exit non-zero on
  divergence and print the first differing call with both PCs.

### 4 · 2.5 h — `libeapp`, Mini Golf's 65 ordinals
Port in the order the boot needs them; after each group re-run `tests/diff.sh boot`:
1. miscTBD (alloc/free/clock/resolve) + Settings + InputEvents.
2. AsyncFileIO open/read/close with completions (the game loads ~4 MB of course 00 on boot; the
   asserted `arg0 == arg1 + 0x128` relation must hold).
3. Audio as a pure state table; `#2` logs the sound name, playback comes last.
4. OpenGLES: texture table (`#4 #19 #21 #84 #99 #101`), attribs (`#40 #137`), matrices
   (`#125 #165 #167 #175` — column-major, `multMatrix` into a temp first, Y-flip from ortho's
   element 5), pipeline (`#159`), `#37 glDrawArrays` → the rasteriser port, `#157` → present.
* **Checkpoint B:** `tests/diff.sh name-entry` passes for the whole script — the recomp makes
  the identical sequence of framework calls the emulator makes through boot, title, and name
  entry. This is the moment the recomp is *proven*, before a single pixel is visible.

### 5 · 1 h — SDL3 platform
* Window 320×240 ×3, `SDL_Texture` streaming from the 24-bit framebuffer on `present()`.
* Keys: ↑/↓ detents (8 per row, see research/13 §2.2), Space = Select, W/A/S/D = the four
  buttons, Q quits — same as `play` so muscle memory transfers.
* SDL3 audio stream for `.wav` SFX; `afplay` for `.m4a` music (macOS only, flagged).
* **Checkpoint C (the end-of-day demo):** title → name entry → a hole played, natively. Compare
  framebuffer hashes at scripted `shot` frames against `play`'s PNGs for the name-entry script.

### 6 · remaining time — first hand-decompiled functions
Prove the swap loop with functions whose Ghidra output is already clean C:
* the string helpers `0x180092bc` (strcpy-with-null-checks, 47 call sites) and `0x180094a0`
  (UTF-16 variant, 45 sites),
* the state-table dispatcher `0x180051d0` (the `switch (obj->0x41)` shape is fully readable),
* two or three leaf fixed-point helpers from the `0x18009788–0x18009d50` cluster.

Mechanism: `src/game/<descriptive_name>.cpp` defines the function under its real name (the
address in a comment), exported to `gen/` through a thin `f_1800XXXX` shim with the *same* `Cpu *` signature and
moves arguments out of `c->r[0..3]` on entry; `CMakeLists.txt` takes the list of replaced
entries and the emitter skips them (`--exclude-from game/replaced.txt`). Diff against the
oracle after each swap. Once a function has no callers left in `gen/`, its signature becomes
real C++ (a method on the recovered class where the original was one) and the `Cpu *` shim goes away — that is the long-tail of the decomp, not today.

---

## Risks, in the order they will bite

1. **Emitter correctness** is the whole day. Mitigation: the oracle diff is available from hour
   4, and `play --callgraph-dump` gives a ground-truth edge list — if the recomp's executed
   edges diverge, the first divergent edge is the bug.
2. **Flags after `mul`/`lsl` corner cases** (carry out of shifter, `LSR #32`, `RRX`). Port
   `arm.rs` `shift_imm`/`shift_reg` verbatim.
3. **`AsyncFileIO #12/#14`** (the `0xc0debabe` request at name-confirm) is unknown and stalls the
   game in the emulator too. Day-one answer: return 0 and log, same as the emulator; the run-2
   session shows a route to a played hole that avoids it. Reading `0x18015e80`/`0x18015e9c` in
   `analysis/ghidra/decomp.c` is the first thing to do tomorrow.
4. **Wheel acceleration** makes scripted `wheel ±N` bursts non-deterministic in `play`. Scripts
   used as oracles should use single-row gestures (`wheel +8`) separated by ≥ 20 frames.
5. **Music is AAC.** `afplay` is a macOS crutch. Real cross-platform answer later: platform
   decoders (AudioToolbox / MediaCodec) or one-time transcode on first launch.
6. **Ghidra function boundaries** can be wrong in a few places (the tool notes mention it
   conflating functions). The live edge targets from `analysis/coverage/` override Ghidra where
   they disagree; emitting per-basic-block labels means a wrong boundary only costs a tail call,
   not correctness.

## Not today (written down so it is not forgotten)
* `ipod_eapp.h` for all 161 ordinals across the 18 titles, and a generic eApp loader in the
  runtime so every title is one `emit.py` run away.
* GPU backends (SDL_GPU / GL / Metal) — the software rasteriser is the reference; keep it.
* 3DS / iOS / Android platform dirs.
* Replacing `Cpu *` plumbing with real signatures as the hand decomp spreads.
* Struct typing in Ghidra (the `DAT_1800b040 + -0x370c` base-pointer globals) — one good pass
  here cleans up dozens of functions at once; do it before the big five.

---

## Progress log

**2026-08-20 — blocks 0–3 done, block 4 nearly done.**
* Block 0 ✅ scaffold, `tools/funcs.py` → 655 functions (332 game, 46 runtime, 277 thunk). Game
  code starts at `0x180024bc`, not `0x18002c28` (the assessment's guess). `reference/` snapshot
  of the emulator files with `MANIFEST.md`.
* Block 1 ✅ `tools/emit.py` (package `tools/recomp/`: image, functions, arm, cfg, cpp, generate).
  376 functions / 23 268 instructions emitted, all compile under `-Wall -Werror`. Decision change:
  the ARM C library is **recompiled** like game code (44 functions); only `_fadd` (`mrs`/`msr`)
  and `_ll_udiv` (arithmetic-bounded jump tables) are hand-written (`src/runtime/arm_runtime.*`).
* Block 2 ✅ runtime (`cpu.h`, `memory.*`, `runtime.*`, `eapp_image.*`, `main.cpp`), null
  platform, `tests/unit/cpu_test.cpp`. Checkpoint A met.
* Block 3 ✅ `play.rs --call-log` (the one emulator change), `tests/expected/boot.calls`
  (301 frames, 35 694 calls) and `name-entry.calls` (406 585 calls), both recorded with
  `--async-files --allow-creates --fixed-clock --fps=0`; `tests/diff.sh`.
* Block 4 ✅ all 65 ordinals in `src/libeapp/` (misc, input, async_file, audio, gles).
  **Checkpoint B met:** `tests/diff.sh boot` identical (35 694 calls) and `tests/diff.sh
  name-entry` identical (406 585 calls, 2 200 frames, 8.6 s). The last divergence was the pump
  zeroing r2/r3 before completion callbacks where the emulator passes only two arguments — the
  game reads the leftover registers, so `call_guest` now sets exactly the arguments given.
  Debug aid added: `--trace-entry=ADDR` (pairs with `play --watch-pc`).
* Bugs fixed along the way: jump-table `cmp` window (16), `add lr, pc, #4` indirect-call idiom,
  function bodies that include code below their entry now open with `goto L_<entry>`.
* Block 5 ✅ `src/platform/sdl3/` — window ×3, ↑/↓/wheel/Space/W/A/S/D/P/Q, SDL audio streams
  for `.wav` SFX, `afplay` child for `.m4a` music (isolated in `MusicPlayer`). `shot` writes
  `build/shot-NN.ppm` + FNV-1a hash; `tools/ppm2png.py` converts and hashes PNG/PPM alike. The
  name-entry frames render correctly (title, name entry with the scripted letters). **Not yet
  done:** the byte-exact framebuffer comparison with `play` — its screenshots go to a shared
  `/tmp/ipod-shot-NN.png` that other agents' runs overwrite, and `miscTBD #12` (wall clock, shown
  on screen) would have to be pinned on both sides first.
* Block 6 ✅ first hand-decompiled functions: `src/game/strings.cpp` replaces `0x180092bc`
  (`string_append`) and `0x180094a0` (`wide_string_append`); `replaced.txt` + `emit.py` drop them
  from `gen/`; both oracles still identical. Lesson recorded in the file header: a replacement's
  shim must reproduce the original's exit values in r1–r3, because the oracle logs them at the
  next framework call. Next candidates: the dispatcher `0x180051d0`, then the fixed-point
  helpers around `0x18009788`.
* Checkpoint C (a hole played natively) needs a human at the keyboard; everything it depends on
  is in place and scripted play reaches the course. `ctest --test-dir build` runs the unit test
  and both oracle cases.

**2026-08-20, later — restructuring toward a game.**
* `src/decomp/` → `src/game/`; `game_state.h` names the firmware context, the answer block, the
  input state at `0x180379f8` (the flags word is its `+0x14`), and the wheel rings;
  `calling.h` gives decompiled code `call()` and `StackFrame`.
* Decompiled `src/game/app.cpp`: the three vector-table functions — `app_entry` (armcc
  start-up), the empty second vector, and `app_frame`, the per-frame body (clock, poll, tap
  detector, button dispatch, update, frame timing, long-press suspend, swap). 5/332 game
  functions now hand-written (`tools/progress.py`).
* **Oracle in two tiers.** Raw logs include leftovers (unused argument registers, callee-saved
  pushes), which only register-for-register code can reproduce. `tests/diff.py` therefore
  compares real arguments per ordinal (arity in `imports.json`) by default; `--exact` compares
  everything and is run by `tests/check-recomp.sh` on a build of the pure recompilation.
* Lesson: after the rename the emitter briefly lost `replaced.txt` and the linker silently kept
  the recompiled copies, so green tests tested nothing — `tools/progress.py` now fails if a
  replaced function is still defined in `gen/`.
* Input subsystem decompiled (`src/game/input.cpp`, `input.h`): `wheel_position_update`,
  `input_snapshot_store/read/clear_*`, `dispatch_buttons` (press/release edges with the
  same-frame-tap deferral), `release_request` / `release_completed_requests` (the "event nodes"
  are AsyncFileIO request objects — callback at +0x34, context at +0x38). `game_update`
  (0x18011538: the two start-up frames, then the tick) joined `app.cpp`. 16/332 functions.
* Two more rules learned the hard way, now in `calling.h`: keep the original's **whole** frame
  (pushes *and* `sub sp, #N` locals — `StackFrame(cpu, regs, local_bytes)`) for as long as the
  function calls anything, because stack addresses are values the game passes on (glOrtho's
  matrix lives on the stack); and don't filter `sp` lines out of a listing you are reading.
* Down another layer: `game_tick` (the firmware-state dispatcher: initialise / run / suspend),
  `game_step` (millisecond accounting with carried remainder, wheel movement with the short-way-
  round and the three-speed acceleration, button hold filtering, then the state machine),
  `suspend`, the two no-op hooks, and `random_seed` — which turned out to be a Mersenne Twister
  (`src/game/random.cpp`; table at `0x18040528`, index at `0x1801a9c0`). 22/332 functions, both
  oracles green. Next down: `state_machine_step` (0x1800ecd8, 655 instructions — the big one),
  `init_audio_and_settings` (0x18012268), `start_render_pipeline` (0x1800ead8),
  `prepare_step_input`, `wheel_movement_apply`, `buttons_apply`.
* `src/game/init.cpp`: `game_init` (0x18012268) and its helpers — version string, BSS head,
  the 0x8faf4-byte game state (`GAME_STATE`, with its `SETTINGS`/`TEXT`/`COURSE_TABLE`
  sub-blocks now named in `game_state.h`), score table, resource slots, device/audio levels,
  font-for-language table, title image load. The three step helpers (`wheel_movement_apply`,
  `buttons_apply`, `prepare_step_input`) joined `input.cpp`. 36/332 functions (2.9%), both
  oracles green. FPS counter added to the SDL window title.
* Note to self: when a build fails, `tests/diff.sh` still runs the previous binary — always read
  the build output before believing a green diff.
* `src/game/flow.cpp`: the top-level flow state machine (0x1800ecd8, 655 instructions) — boot
  sequence load save (`jdmgp.sav`, fallback `jdmgp2.sav`, magic 0xc0debabe at both ends) →
  reset/load score entries → open resource packs ("jdmg", then "c00"/"c01"/"c02" + "…sheets"
  by course, glyph sheet by language) → enter the first screen → run the screen each step with
  the 120 s / 240 s idle timers; phases 8/9 write both save files when asked. Two lessons: the
  file-layer calls take the service object in r0 (`add r1, sp, #0x10` was the request), and a
  `mov r3, #0x140` before a call was its fourth argument, not a stray constant — check every
  register a callee might read, not just the ones set right before the `bl`.
  37/332 functions, 6.0% of instructions, both oracles green; SDL build renders as before.
* `src/game/files.cpp`: the game's file service — a heap object of ten 0x184-byte operation
  slots on a free list; `file_request_prepare`, `file_begin` (read/write), `file_finished`,
  `file_status`, `file_close`. Below it sit the AsyncFileIO request objects (a C++ class with
  `operator new` at 0x180184cc, constructor 0x18018458, methods 0x180183cc/0x180183b0) and the
  two completion callbacks (0x18016ee8 → 0x180172e8, 0x18016e98 → 0x18017130) — next in this
  subsystem. 44/332 functions, 6.8% of instructions, both oracles green.
* `src/game/async_request.cpp`: the AsyncFileIO request class (`operator new`/`delete`,
  construct, the three set-ups, attach), the three issuers, and the completion path
  (request → operation record → slot). 61/332 functions (8.2%), both oracles green. The
  open-then-read continuation (0x18017130 …) stays recompiled for now.
* Screens are objects: a block at `GAME_STATE + 0x82000` with a handler (+0xbb8), a tick
  (+0xbbc), a render (+0xbc0) and a menu table (+0xbcc); each screen's enter routine installs
  its functions. Screen 11's tick is 0x18002c28 (1354 instructions: the course-play sub-state
  machine, 32 states), not name entry. The generic menu tick is 0x1800c8d0; menus differ by
  handler (0x1800cfa8, 0x1800d058, …).
* Screens: `screens.cpp` (the screen object, `screen_set`, the two tick wrappers the flow
  calls, menu sounds), `title.cpp` (enter with the parallax geometry, the three-phase tick,
  the resume-or-menu exit at 0x1800cf60 — a hand-found entry, see `analysis/extra-entries.txt`),
  `name_entry.cpp` (enter, the Select/Menu handler: glyph wheel, backspace 0x3a, confirm 0x3b,
  16-letter cap), `dialog.cpp` (the six message types and where each leads), `menu.cpp` (the
  generic slide tick, main-menu enter and handler, the shared back handler, `text_width`).
  Discovery that simplified everything: `TEXT + 0x72x` and `COURSE_SELECT + 0x2x` are the same
  bytes — one `menu` state block (cursor, first row, item count, name length). 83/333
  functions, 14.2% of instructions; `boot` and `name-entry` identical. The `menus` oracle case (`tests/scripts/menus.script`, 4 300 frames,
  567 734 calls: name confirmed → dialog → main menu → a hole) is identical too. Renderers (0x1801289c, 0x18013848, 0x1800c1dc) and
  the other menus' enter routines/handlers are next.
* Emitter: complementary conditional terminals (`bne X; beq Y`) no longer fall through into
  the literal pool (tools/recomp/cfg.py).
* Every menu is hand-decompiled: Game Modes (0x18005bc0, handler 0x1801181c), Options
  (0x180057d4, handler 0x1800f7f0 — Music / Sound FX / Clock-Batt / About / Player / Reset),
  Statistics (0x180054bc, handler 0x1800cfa8), the course carousel (`course_select.cpp`:
  0x18005980 builds the filmstrip from pack pictures 7–9 or a placeholder for locked courses;
  handler 0x18013760), Select Hole (0x18005c40, handler 0x180118e4), the pause menu (0x18005874,
  handler 0x1800e160) and the dialog's answers (0x18010654). Names were settled from screenshots
  of the headless build and `jdmg.en` (the text resources in menu order), which corrected three
  guesses: screen 2 is Game Modes (not high scores), 0x180057d4 is Options (not course select),
  0x18005980 is the course carousel. Three more bytes turned out to be aliases of the one menu
  block: `MENU + 0x30` is the game mode (single player is the only mode that saves), `MENU +
  0x40` is the page `ENTER_COURSE_PLAY` shows first (volume, help, about, statistics, "saved"),
  `MENU + 0x41` the dialog type. The options live at `GAME_STATE + 0x83008..b`. Fourth oracle
  case `options` (`tests/scripts/options.script`, 10 400 frames, 1 035 427 calls: all six option
  rows, the reset dialog declined, Game Modes → Practice → carousel → Select Hole and back) is
  identical in both tiers. 95/333 functions, 17.8% of instructions. Still recompiled: the
  renderers (0x1801289c title, 0x18013848 name entry, 0x1800c1dc menu, 0x18014734 carousel,
  0x18012fa8 dialog), course play (enter 0x18005f54, tick 0x18002c28, render 0x1800a080), the
  score card (0x18002968) and the open-then-read file continuation (0x18017130).
* First-run install and verification (`src/gamedata/`): `tools/manifest.py` records the size
  and CRC-32 of the game's 169 files; `zip.cpp` is a small central-directory reader (stored and
  deflated entries, zlib); `install.cpp` verifies a chosen zip against the manifest before
  unpacking it into the platform's data directory (`src/platform/paths.h`: macOS
  `~/Library/Application Support/iPod Mini Golf`, Windows `%APPDATA%`, Linux XDG) and verifies
  the installed copy on every launch. The SDL platform gained the native file browser
  (`choose_file`, `SDL_ShowOpenFileDialog`), full screen (F / F11, letterboxed), and a
  resizable window; `main.cpp` creates the platform before the guest runs and takes
  `--install-zip` and `--time=HH:MM`. Two oracle fragilities found and fixed on the way: the
  title screen shows the real time of day, so the recordings only matched at one-digit hours
  (`diff.sh` now pins `--time=07:53`), and the game writes its saves into its own directory,
  which changes the next start-up (`diff.sh` now runs each case in a fresh copy under
  `build/game-<case>/`, so the reference directory stays read-only).
* Renderers and the oracle that can see them: `draw.h` names the drawing primitives (image,
  rect, line, textured quad, text, glyph — their bodies, the GL batcher, stay recompiled),
  `ui.cpp` holds what the screens share (background and dim, logo, panel and its grow-in, the
  highlighted row with the spinning ball and rippling letters); `title_render`,
  `dialog_render`, `menu_render` (`menu_render.cpp`), `name_entry_render`,
  `course_select_render` are hand-decompiled. Two bugs the call-log oracle could not see fell
  out: the title layers' drop speed was computed 480× too small (a 64-bit division misread) and
  the dim overlay's blend differs between menus and dialog. So there is now a second oracle,
  `tests/draw-trace.sh`: the pure recompilation and the decompiled build run the same case with
  the primitives traced (registers and stack words) and the traces are diffed. `page_enter`
  (`page.cpp`) turned out to be what 0x18005f54 is — statistics, help, volume/brightness
  sliders, "saved" — not course play; the pause menu moved to `pause_menu.cpp`; `course.cpp`
  has `hole_enter`, `course_start`, `score_card_open`; the open-then-read continuation
  (0x18017130) joined `async_request.cpp`. 105/333 functions, 28.3% of instructions.
* The oracle emulator: the live emulator was rebuilt by other agents with different Audio
  behaviour, and its scripted runs also took live keyboard/mouse input and read the host's
  battery, so recordings were neither reproducible against `libeapp` nor between runs.
  `tools/oracle-emulator/` is the `reference/` snapshot built on its own (with `--call-log`,
  `--enter-log`/`--watch-pc`, `--time`, and live input disabled under `--script`); every
  recording now comes from it with `--time=07:53 --battery=100`. Fifth oracle case `hole`
  (`tests/scripts/hole.script`, 10 300 frames, 1 629 283 calls: a hole played through two putts,
  pause and resume) is identical in all three oracles — and on the pure recompilation, which
  means the runtime, `libeapp` and the emitter carry the course physics correctly.
* The hole: `hole_tick.cpp` (0x18002c28, 1354 instructions — placing, aiming, power, rolling,
  sinking, the stroke limit, the other player's turn, the score card, the end-of-course message,
  the unlocked course's picture, the wipe to the next hole, and `hole_finish` for 0x18009c0c,
  which the emitter had folded into the tick) and `hole_render.cpp` (0x1800a080, 2092
  instructions — the ground captured once into a texture through raw OpenGL ES and drawn as a
  quad after, the course objects with their per-course special cases and animation, the aim
  line, tee arrows, ball and arrow, the HUD, the score card, the unlocked-course picture, the
  power meter, the message panel). Two finds on the way: 0x1800740c takes the left texel
  coordinate in r0, not the batcher handle (the carousel's slides had it wrong — invisible to the
  call log, caught once `draw-trace.sh` watched every drawing entry point, which it now does),
  and the ground capture's corner order. 107/333 functions, 44.5% of instructions; all five
  cases identical in the call log, the exact tier and the draw trace. Still recompiled: the ball
  physics (0x18009f28, 0x1800ce04, 0x1800d1c4, 0x18010588), the text and GL batchers, the
  page tick and render (0x180113c4, 0x18010c3c), course loading (0x1800e868, 0x180154bc).

- 2026-08-21 (overnight). The ball physics (`physics.cpp`: ball_step, ball_move — the tile
  switch, cups, bumpers, slopes, friction, wall/mesh/peg bounces — point_blocked, trail_reset,
  ball_to_tee, surface_apply, mesh_collide, hint_sequence_start, integer_sqrt, sine_degrees,
  power_meter_value), the turn bookkeeping (`turn.cpp`: ball_rest_record, turn_resume), the
  panel message (panel_message_show) and the whole GL vertex batcher (`batcher.cpp` + `gl.h`:
  buffers, free list, rect/line/quad adders, buffer_flush, batcher_flush, and the five draw
  entry points behind draw.h). One catch on the way: the bounce negates before it shifts
  (`(-(n·v)) >> 8`), which a tidy `-(x >> 8)` got wrong by one and sent a ball past the cup.
  139/333 functions, 55.6% of instructions; all six cases identical in the call log.
- 2026-08-21 (overnight, later). Text (`text.cpp`: text_draw_at, glyph_draw_at, text_layout),
  the page handler/render/slider (`page.cpp`), the panel (`ui.cpp`: panel_draw, panel_draw_scaled,
  panel_message_draw in hole_render.cpp), the whole hole loader (`hole_load.cpp`: tile map
  rasteriser with curves, sprite sheets, objects and meshes, images, ball placement, the course
  and hole data with walls and pegs, the hole layout with the ground texture, the ground camera
  and matrix transform), `course_unload`, `save_record_snapshot`, the frame renderer and loading
  screen (`frame.cpp`), `music_start` (`music.cpp`), `save_reset` and `score_entries_begin`
  (flow.cpp), arrow/object draws (hole_render.cpp). Lessons: a tail call's framework calls log
  the *caller's* LR, so a decompiled tail call must pass the shim's return address; the course
  pack handle is per course (GS + 0xc + course*4); `frame.slot()` is not the argument area —
  `stack_arguments` is. New runtime flag `--dump-entry=ADDR:START:BYTES` prints guest memory at
  a function entry, for diffing tables between the pure and decompiled builds. draw-trace.sh
  no longer compares the decompiled build meaningfully (its primitives are C++ now); the call
  log is the oracle. 174/333 functions, 76.9% of instructions; all six cases identical.
- 2026-08-21 (overnight, later still). The resource layer (`resources.cpp`: in-memory files,
  the bit-stream decompressor, pack entries, resource_load/open, images, the tracked heap),
  sounds and device levels (`sounds.cpp`), the Mersenne Twister (`random.cpp`), the screen
  ticks that sit over a menu (`screen_ticks.cpp`: page, dialog, name entry, course select),
  the save card (pause_menu.cpp), course resume/save/new round/hole start/load request
  (course.cpp). 240/333 functions, 87.0% of instructions; all six cases identical. Frame
  shapes matter even for semantics: a pointer into the stack that reaches a framework call
  (the clock read in battery_status) must sit where the original put it, so StackFrame lists
  follow the original push list (padding registers included).

### 2026-08-21 — file service, file objects, sound bank, save-file records, renderer
- Remaining file-service functions (`files.cpp`, `async_request.cpp`): slot/service construction,
  positioned open/read/write/close and their completions, `operation_close` (the close request's
  callback is 0x18017f6c, not the open-then-close completion — that cost one `operator delete`),
  the simple synchronous file calls, the C-runtime stubs (`atexit`, locks, guards).
- `file_objects.cpp` (0x1803fc34 ×10): open by file name ("games_RO"/"gamedata_RW"/"gamestats_WO")
  with a C mode string, transfer, close, completion. `save_files.cpp`: the ring of eight records
  and `score_file_begin` (chunked through FILE_TABLE).
- `sound_bank.cpp` (0x18041418): WAV header → Audio set-up → data read state machine, plus
  `course_sounds_load`. Audio#0's arity is 2 (slot flag, index) so the call passes both.
- `renderer_create` (gl_state.cpp), `title_pack_open` (resources.cpp), `carousel_slide`,
  `stream_register` (misc #14 takes the name in r3).
- 328/333 game functions, 99.9% of instructions; boot/name-entry/menus/options identical,
  hole/next-hole identical before the last batch (rerun pending).

### 2026-08-21 (later) — 333/333, and the bugs the call-log oracle cannot see
- The last five functions: the read/write set-up thunks (`async_request_setup_read/write`),
  the file-object and request-record constructors, `sound_bank_construct`.
- Visual regressions reported (broken course-switch animation, two golfers) were invisible to
  the call-log oracle, which compares call arguments, not the vertex/texture data behind the
  pointers. Two new comparisons found them:
  - `MINIGOLF_VERTEX_HASH=1` makes libeapp print a hash of every glDrawArrays' vertices
    (`MINIGOLF_VERTEX_DUMP=<draw>` prints one draw's vertices); diff the streams of `build/`
    and `build-recomp/` on the same script. `--dump-frame=START:BYTES` prints memory after
    every frame; `MINIGOLF_WATCH=ADDR` aborts with a host backtrace on the first non-zero
    store to a word. The `shot` script action's framebuffer hash does the same per screenshot.
  - Found and fixed: byte fonts draw glyphs at the full cell height (only halfword fonts
    subtract one); panel edges are rgb 0xff00 with alpha 0x10000; the ground texture's image
    variant is 0; `random_generate` reseeds only when LEFT < -1 (an exhausted state sits at
    -1 — reseeding every 624 draws changed every object's random wait); the golfer sprite's
    height is at +0x18 and its octant-7 overhang uses +0x14 (not the image's U/V).
  - The between-hole wipe: `quad_add` took a texel rectangle (u0, v0, u1, v1) and rebuilt the
    four corners from it, which is right for every sprite but not for the wipe's rotated ground
    quad. The quad routines now carry the original's four (u, v) pairs (the last pair lands on
    the first corner).
  - The dotted diagonal lines over the ground were moiré: the ground art is dithered and was
    shown at two to three texels per pixel; the rasteriser now box-filters the texel footprint
    when a triangle minifies its texture (`sample_texture_box`), as the hardware's mipmaps would.
- SDL build: a macOS "Speed ▸ Lock Frame Rate" menu item (⌘L; the L key everywhere) toggles
  the 30 fps pacing; unlocked, the game runs as fast as the machine allows.

### 2026-08-21 (evening) — the recompilation scaffolding comes out
Snapshot before this work: git tag `minigolf-recomp-decompiled-snapshot`.
- The ARM C library is C++ (`src/game/libc.{h,cpp}`: memory/string routines, the divides,
  the framework heap with the caller's return address, a `sprintf` subset, the runtime's
  descriptor for `atexit`). The static constructors are called directly in the image's
  table order; the library's own initialisation had no effect the game can see.
- The main build no longer compiles `gen/` at all: the framework thunks are a kept file
  (`src/libeapp/thunks.cpp`), `call_indirect` is a hand-written table of the 55 addresses the
  game stores as function pointers (`dispatch.cpp`, found by logging every indirect target
  across the six scripts plus a scan for function addresses in the code), and the other 216
  shims are gone. `MINIGOLF_GEN_DIR` is empty by default; `check-recomp.sh` still emits the
  pure recompilation into `build/gen-pure/` for the exact and vertex oracles.
- 249 calls that went through `call(cpu, f_…)` are direct C++ calls; every `constexpr auto&`
  alias of a shim is gone; 167 `StackFrame`s that served nothing are gone (38 remain: guest
  locals or stack arguments that reach a framework call); the draw wrappers call the batcher
  directly, which removed ~300 return-address parameters/arguments. Per-file headers now
  declare what other files call (`course.h`, `files.h`, `resources.h`, …).
- The semantic oracle reports guest-stack pointer arguments as `stack`: with fewer frames the
  addresses of locals move, the bytes behind them do not (the vertex/framebuffer comparison
  against `build-recomp/` still covers those). Six cases identical; vertex streams identical.
- Left for the structs phase: ~120 `cpu.r[0] = f(...)` result hand-offs, 44 return-address
  parameters on paths that end in a framework call, 117 ARM-ABI entries (55 needed by the
  dispatch table).

### 2026-08-21 (night) — state into structures, step 1: overlays
- `guest.h` (`guest<T>(address)`) and `state.h`: packed structures overlaid on the blocks the
  game keeps in guest memory, every offset pinned by `static_assert`. Generated from the
  offset namespaces in game_state.h and the access widths in the code by a scratch script
  (`overlay.py`: fields, arrays for indexed accesses, byte/halfword/word and signedness), then
  the accesses rewritten. Done: PlayState, ScreenState, MenuState, PlayersState, OptionsState,
  InputState, AppState, App2State — ~750 field accesses replace `ld32(BASE + ns::FIELD)`.
  Four quick oracle cases identical; the long cases and the vertex comparison still to run.
- Next: the variable-base records (image, text, file_slot, bank, object, file_object, font,
  async_request, operation, file_request, kind, random, stream) as `guest<Record>(address)`
  views; then the GAME_STATE block (173 accesses, two namespaces); then move self-contained
  subsystems (physics, random, batcher buffers) off guest memory where nothing external points.
- Step 2 (same night): the variable-base records as `as_<record>(address)` views (image, text,
  file slot, sound bank, hole object, file object, font, pack, async request, operation, file
  request, object kind, bit stream, menu item, file entry, course picture) and the GAME_STATE
  block. The generator's width rules needed three corrections found by the hole case
  (`wheel_repeat_limit`, `kind.rate`, `menu_item.y` are words; a compound index lost its scale):
  an audit script now compares every overlay field's width with the snapshot's accesses.
- First state off guest memory: the random generator (`random.cpp`) is a host object; callers
  keep passing the original's object address as an identity. Nothing else read its bytes and
  no framework is handed them, which is the test for what can move next.
- Step 3 (same night): player records (`player_record(i)`), the merged SETTINGS/TEXT fields, and
  the remaining fixed-offset accesses; then two more subsystems off guest memory: the tracked
  allocation registry (`resources.cpp`, a `std::vector<TrackedBlock>`; the 0xcd guard bytes
  still bracket each heap block because the heap itself is guest memory) and the sound slots
  (`SoundSlots` in sounds.h: five flags and 64 handles each, shared by sounds.cpp and init.cpp).
  All six oracle cases identical, vertex stream identical (59 094 draws), ctest 7/7.
- Left: ~1200 raw `ld/st` lines, mostly records read out of course and hole files (sections,
  object sources, tee/wall/peg/obstacle entries, score rows, save data) that need file-format
  overlays rather than state overlays; the coarse GameState arrays (save_data, course_table,
  text, settings); and game_state.h's remaining offset namespaces, which the overlays
  supersede one at a time.

### 2026-08-22 — no raw memory access, typed framework calls
- Every `ld8/ld16/ld32/st8/st16/st32` outside `strings.cpp`/`libc.cpp` is gone (1 138 → 0):
  `records.h` holds the course/hole file records and the loader's tables (tee points and edges,
  walls, pegs, obstacles, surfaces, sprites and frame lists, sprite sheets, object sources, mesh
  visits, course info, pack entries, file kinds, the save record, the statistics record, GL
  matrices and draw lists); the batcher's buffers are an overlay; the text block's slides,
  the image record (cell, origin), the device block, the badge flags and the options scratch are
  named; ROM tables are read through `guest_array<T>()`. Mistakes the oracle caught on the way:
  a mis-indexed physics constant, a text block shifted by 8 by a broken tool run, a player
  record base, and two default image records at the wrong offset — every one found by the long
  cases and the vertex comparison, which now also covers `next-hole`.
- Framework calls are typed (`frameworks.h`, 66 wrappers over the thunks); `call()` is gone,
  every `constexpr auto&` alias is gone, and the return-address parameters with them (`tests/
  diff.py` compares the ordinal and its arguments only). Game entries reached through the
  dispatch table use `call_entry`. `StackFrame` became `GuestScratch`: bytes on the guest stack
  for what a framework reads or writes, nothing about registers.
- Left for a later phase: ~260 `cpu.r[0] = f(...)` hand-offs, the 55 dispatch-table entries and
  their ARM-ABI shims, the `Cpu&` parameter threaded everywhere, and guest memory itself.

### 2026-08-22 (later) — the ARM shape comes off: dispatch, `Cpu&`, guest memory

- The dispatch table is down from 55 entries to 14, and the ARM-ABI shims from 117 to 23. A
  screen is a `Screen` of typed host function pointers (`ScreenHandler`, `ScreenTick`,
  `ScreenRender`, `ScreenEnter`); `flow.cpp`'s `screen_step`/`screen_enter` return real
  functions instead of addresses. What is left is what genuinely has to be an address: the
  image's three entry vectors, and the completions the framework itself dispatches (five are
  read out of the guest request records by `src/libeapp/async_file.cpp` at +0x34). This also
  killed a class of latent bug — four addresses (`TITLE_HANDLER`, `CARD_RENDER`, the card
  handler, `ENTER_HOLE`) were stored as dispatch targets but missing from the table, and would
  have been fatal if reached. `card_render` is now an explicit documented stub: the original
  was never decompiled because nothing in the six cases reaches it.
- The `Cpu&` parameter is gone from all ~175 game functions, and with it the ~260
  `cpu.r[0] = f(...)` hand-offs — a function returns its result. The register file is one
  singleton, `registers()` (`runtime/cpu.h`). `Cpu&` survives only where the ABI is the point:
  the 23 shims, `dispatch.cpp`, and the runtime.
- Guest memory: the state blocks were tested against the oracle logs directly — for each block,
  does any address inside it ever appear as a framework argument? Two came back clean and moved
  to host state: the seven wheel/button slots (was `PLAYER_TABLE`) and the menu item tables
  (was `MENU_TABLE`/`MENU_TABLE_ALT`, and the score card's two rows, which the original built
  inside the screen object's padding). Everything else must stay, and now for a recorded reason:
  `PLAY` is the settings block the file framework reads and writes; `HOLE_OBJECTS` overlaps
  `GROUND_IMAGE`, an image record the renderer reads; `SCREEN_OBJECT`, `TEXT`, `MENU`,
  `PLAYERS`, `OPTIONS` and the head of `GAME_STATE` all appear as framework arguments (204 815
  of them for the text block alone); `STEP_INPUT` is written by the framework through a pointer
  it was handed. `INPUT_STATE` is the one block that passes the log test and still cannot move:
  it is initialised data, so its start-up values come out of the player's own copy of the game —
  reproducing them in source would mean embedding game data, which this project does not do.
- Verified after every step: the six oracle cases identical, both vertex streams identical
  (`vis2` 59 094 draws, `next-hole` 131 021 draws), `ctest` 7/7, after `clang-format`.

### 2026-08-22 (modernisation) — a native framework interface, typed records, fixed point, tests

Five steps, each verified against the six oracle cases and both vertex streams before the next.

1. **The framework boundary is C++, not registers.** `src/framework/` holds five headers —
   `graphics.h` (`gfx`), `audio.h` (`audio`), `storage.h` (`storage`), `controls.h` (`controls`),
   `device.h` (`device`) — and `src/libeapp/` implements them. The 68 `fw::` wrappers, `invoke`,
   the 179 `eapp_*` thunks and `thunks.h` are gone; a call is now `gfx::draw_arrays(...)`.
   The oracle survived intact because the call log is written by the typed entry point from its
   own arguments: `tests/diff.py` compares the ordinal and as many arguments as the ordinal
   really takes, and those are unchanged. The ARM calling convention lives on in one file,
   `src/libeapp/arm_abi.cpp`, compiled only for the pure recompilation, which keeps
   `check-recomp.sh --exact` working.
2. **Records are passed as references.** `uint32_t image` became `ImageRecord&`, and so on for
   fonts, packs, banks, streams, objects, requests, operations, contexts, answers, file entries
   and the rest — parameters first, then the locals that were only ever read through an
   accessor. `as_x(address)` is now only the bridge from a stored address to the record, and
   `address_of(record)` the way back; `guest_address()` in the runtime is its implementation.
   Typed-reference parameters in the game's headers went 0 -> 74 and raw `uint32_t` ones
   546 -> 370. Two shapes resisted and were left alone: constructors that free the object they
   are given, and records whose sub-objects are addressed by offset (`slot + ASYNC_REQUEST`) —
   those need the sub-object modelled as a member first, which `SoundBank::request` now is.
3. **`enum class` and fixed point.** The GL values the game passes are nine enums in
   `graphics.h` (`TextureTarget`, `Primitive`, `AttributeType`, `PixelFormat`, `PixelType`,
   `Buffer`, `PixelStore`, `TextureParameter`, `Pipeline`), so a texture target can no longer be
   passed where a primitive belongs. Two of the game's own names were wrong and are fixed with a
   note: what it called a triangle fan the pipeline reads as quads, and what it called the unpack
   alignment is the pack alignment. `fixed.h` adds `to_fixed`/`to_whole`/`to_fixed_signed` and a
   `Fixed16` type whose `*` keeps the scale; raw 16.16 shifts went 213 -> 70.
4. **Unit tests.** `tests/unit/` grew from one file to five: fixed point, the ARM C library
   (memory, strings, the three divisions, the formatter), the game's byte and UTF-16 string
   helpers against each other, and the matrix helpers every vertex goes through. They need no
   game data. All four new ones passed the first time they ran, which is the useful outcome:
   the library underneath the decompilation does what C says it does.
5. **Portability.** `libc.cpp` was already `std::memset`/`memmove` over guest pointers; the
   platform split (`src/platform/sdl3`, `src/platform/null`) already held. The remaining host
   assumption is that guest memory is little-endian, which the overlays in `state.h` depend on.

**`tests/check-recomp.sh` runs again.** It had been broken since the `Cpu&` phase — the emitter
still generated `assert_trap(cpu, addr)`, `trace_entry(cpu, addr)` and `call_indirect(cpu, …)`
against signatures that had lost their `Cpu&`. `tools/recomp/cpp.py` and `generate.py` now emit
the current ones, `ipod_eapp.h` declares the ARM adapters the generated bindings call, and unit
tests are skipped in a `MINIGOLF_GEN_DIR` build (there is no `src/game/` to link against).
Five of the six exact cases are identical. `next-hole` differs on 20 lines out of 2 871 115, all
within frame 7485 and all in the two stack words past the arity of the ordinals involved —
leftovers below the stack pointer that nothing reads; every register argument matches, and the
semantic comparison and both vertex streams are identical.

The tooling is kept, in `tools/refactor/`, because the job is not finished: `refs.py`
(parameters -> references, with call sites), `locals.py` (the same for locals), `nullchecks.py`
(drop the null-address asserts a reference makes impossible), `needstate.py` (the include).
Both scanners had the same two bugs, and both corrupted code *silently*: `0xffff'ffff` digit
separators read as character literals, and a name declared twice in one function. Anything of
this shape needs the build **and** the oracle after every step.

### 2026-08-22 (afternoon) — what a review found, and what it cost to fix

Two fresh-eyes reviews (one over `src/game/`, one over everything else) plus a coverage
measurement. Everything below is done unless it says otherwise; the six oracle cases and both
vertex streams were green after each step.

**Real defects, now fixed.** `Heap::alloc` rounded `size + 7` without checking the size first,
so a 4 GB request wrapped to an 8-byte block that passed the exhaustion test.
`guest_pointer`'s bounds check computed `GUEST_SPAN - length`, which wraps for a length past the
span and lets everything through — and callers do pass sizes read from the image. The rasteriser
reached for a texture with `std::map::at`, which throws, in a project built `-fno-exceptions`;
`async_file.cpp` dereferenced `file_for(handle)` twice without the null check every other call
site has; the SDL file dialog handed SDL a pointer to a stack local it could outlive.
`resolve_resource` wrote its terminator past the clamp rather than at it.

**Portability.** The guest address space was `mmap` only, in the layer that is supposed to be
host-independent; it is now `reserve_span()` with three implementations (`VirtualAlloc`, `mmap`,
`calloc`) and a specific note about what a small-memory console would need instead. `execinfo.h`
and `localtime_r` are behind capability checks, the `afplay` music crutch behind `__APPLE__` with
a working no-music fallback, and every compiler flag in `CMakeLists.txt` has an MSVC spelling.
`find_package(SDL3)` is tried before pkg-config. The one real host assumption — the overlays read
guest words directly, so the host must be little-endian — is now asserted in `guest.h` and stated
where `memory.h` used to claim the opposite.

**Duplication.** `COURSE_COUNT` meant 11 in `game_state.h` and 3 in two `.cpp` files, both in
scope; the table's slot count is now `COURSE_RECORD_COUNT` and the playable count lives once.
"Eighteen holes" had five names, the screen size eleven definitions, the game modes five. 190
constants in `game_state.h` were dead — superseded by the structures in `state.h` — and are gone,
along with 65 unnecessary includes, three `shims.h` declarations with no definition, ten empty
`// --- shims ---` banners, and the write-only GL state that advertised a row-alignment the
uploads never implemented.

**Readability.** 1 973 `static_cast`s are now 1 499: the `int8_t -> uint32_t -> int32_t` chains
were identity, as were 43 casts of `int8_t` fields to `int8_t`. The fourteen `divisionN` locals
have names. The hole's state machine was three partial `enum`s in three files and is now one in
`hole_tick.h`, so `hole_render.cpp` compares against `ROLLING` and `HOLED` rather than 5 and 6.

**Coverage, measured.** A profile build over the six oracle cases: 65.7% of lines in `src/game/`,
77.7% of functions. The gaps are whole screens — `page.cpp` 2.6%, `pause_menu.cpp` 17.0%,
`hole_tick.cpp` 44.9% (20 of its 41 functions never run). Refactoring there is unprotected, which
is worth knowing before touching it.

**Blocked: new oracle cases.** The obvious fix for that coverage gap is more recorded cases, and
it cannot be done today. `tools/oracle-emulator` is the reference snapshot plus this project's
`--time` and `--enter-log` flags, and it no longer reproduces the recorded logs: re-recording
`boot.script` diverges from `tests/expected/boot.calls` at call 39 of 35 694, where the fresh run
emits an extra `miscTBD#1` (free, from 0x180184bc, freeing the request `AsyncFileIO#3` was just
given) that the recording does not, and then makes 23 145 calls where the recording makes 35 694.
It is not nondeterminism — two runs agree with each other exactly — and it is unchanged by
`--async-files`, `--fixed-clock`, `--event-buttons`, `--load-on-open` or `--open-returns-handle`.
Someone has to find which emulator revision recorded `tests/expected/` before the oracle can grow.
A script that reaches the three page screens from the main menu is written and takes `page.cpp`
from 2.6% to 31.0%; it is waiting on a reference log.

### 2026-08-24 — saved games work, and two bugs that stopped them

The save store (AsyncFileIO #12/#14/#16) is implemented, and where a save goes is now the
platform's choice: `src/platform/save_store.h` is a name-and-bytes interface, `Platform::
create_save_store` lets a platform return its own, and the default is one file per save in the
game's directory. `src/libeapp/async_file.cpp` routes reads through the same store, so a
platform that keeps saves elsewhere reads them back from there. `tests/unit/save_store_test.cpp`
pins the contract both implementations have to honour.

Implementing the store was the easy half. Two bugs stood between it and a save that survived,
and neither was visible to the call-log oracle, because the log records a buffer's address and
never its contents:

1. **The boot-time read truncated the save.** `flow.cpp` opens the save with mode 1 whether it is
   loading or saving, and `async_open` took mode 1 to mean "write", so start-up opened the save
   for writing — creating it empty — on its way to reading it. Every launch destroyed the save it
   was about to load. Saves are now recognised by name and never opened for writing here; the
   only thing that writes one is the store.
2. **`save_reset` read the wrong byte.** `SaveRecord::in_progress` was modelled at +0x84; the
   original reads it at +4 (0x18004248: `ldrb r0,[r6,#132]` with r6 = GAME_STATE + 0x82d00, which
   is SAVE_DATA + 4). So the guard was always zero, and every start-up cleared the record it had
   just loaded — name included. The field at +0x84 is now `byte_84`, honestly unknown.

The oracle survived almost intact. Making a *missing* save open empty rather than fail keeps the
start-up call sequence exactly as recorded, so `boot`, `name-entry` and `next-hole` are unchanged.
The three cases that exit through the save — `hole`, `menus`, `options` — now issue the `#16`
that actually writes, which no recording has. Rather than re-record (blocked) or weaken the
comparison, `tests/expected/save-store.allow` names those three ordinals and `tests/diff.py
--allow` drops them from both sides; every other call is still compared exactly. The file says
what it is for and when to delete it.

Verified end to end: play a hole, quit, launch again — the main menu, not the name entry.

### 2026-08-24 (later) — rebindable controls, and a settings window on macOS

The seven things a player can do — swipe left, swipe right, select, play/pause, menu, rewind,
fast forward — are now a table rather than a `switch`: `src/platform/input_bindings.h`. A binding
is an opaque `InputCode` that only the platform which produced it understands, so the portable
half (the actions, their names, the defaults, the rule that one input does exactly one thing, and
the saved file) is shared, and a platform supplies only its codes and a way to ask for one.
The bindings are kept in the same platform store as the saved games, since both are the player's
own data and should follow them wherever that platform puts such things.

macOS gets the first settings window: **Settings…** (⌘,) in the application menu, a row per
action, click a key and press a new one, Escape cancels, Restore Defaults. It is a Cocoa window
(`src/platform/sdl3/macos_settings.mm`) rather than something drawn inside the game's 320x240
picture, because it is a setting of the program, not part of the game. The capture reads the
NSEvent directly — while that window has focus SDL sees no keys at all — and translates it to
the SDL keycode the game's key handling is written against; for everything printable the two
already agree, so only the keys that type nothing need a table.

The SDL platform's key handling now looks the key up in the table and falls through to the two
keys that are the program's rather than the device's: P for a screenshot, Q or Escape to quit.
Those stay fixed. The defaults are exactly the keys the build always had, so nothing changes for
anyone who does not go looking.

`tests/unit/input_bindings_test.cpp` covers the portable half end to end, including the round
trip through the store that a settings window depends on. `.clang-format` grew an Objective-C
section and the `format` target now includes `src/*.mm`, so the Cocoa code is held to the same
style as everything else.

### 2026-08-24 (later still) — the settings window did not take a key

Rebinding had no effect, and the reason was entirely in the Cocoa half. The row's button waited
for its key by overriding `keyDown:` after `makeFirstResponder:`, which is not how a Mac window
delivers keys to a button: clicking one does not make it the first responder unless Full Keyboard
Access is on, and an `NSButton`'s cell eats Space and Return before `keyDown:` anyway. So the
capture never completed and the table was never written.

It now uses `+[NSEvent addLocalMonitorForEventsMatchingMask:]`, which is what a "press any key"
prompt wants: every key press in the application arrives while a row is waiting, wherever the
responder chain would have sent it, and returning nil from the handler swallows the event so the
key does not also do whatever it normally does. Escape is recognised by virtual key code, which
is why the file now includes Carbon for the one constant worth naming.

Two things fixed alongside it. The wheel's defaults are ← and → rather than ↑ and ↓, which is
what the actions are called. And `SDL_EVENT_KEY_DOWN` used to check F, F11 and L before the
bindings, so those three keys could not be rebound; `handle_key` now reports whether it used the
key and the window's own shortcuts take only what is left.

Worth remembering: the load path was fine all along — a hand-written `bindings.txt` was read
correctly, which is what ruled out everything below the window and left only the Cocoa capture.

### 2026-08-24 (last) — rebinding, actually working

Two more things were wrong, and one of them was masking the other.

**A saved bindings file outranked the new defaults, silently.** `Restore Defaults` worked from the
first version of the window — it needs no key capture — so a click on it wrote a `bindings.txt`
holding the *old* defaults. When the wheel's defaults later changed to the arrow keys, that file
was still loaded over them, so Up still scrolled and the new keys did nothing. `from_text` now
starts from the platform's defaults instead of from nothing, and `to_text` writes every action
including the unbound ones as code 0. An action a file does not mention keeps its default; one
written as 0 stays unbound. Adding an action no longer leaves everyone who ever saved bindings
without it.

**Key capture was replaced rather than fixed again.** The window now offers a pop-up menu of the
keys the platform is willing to assign (`set_assignable_inputs` / `assignable_inputs`, with SDL
naming each key), so nothing has to intercept the keyboard at all. Two attempts at capture — the
responder chain, then a local event monitor — were each plausible and neither could be tested
here; a list is deterministic, and the platform-supplied choice list is something every other
platform's settings UI can use too.

Also: the actions are called "Scroll left" and "Scroll right", which is what they do; the file
names stay `swipe-left`/`swipe-right` so existing saved bindings still load. The Settings item
sets `keyEquivalentModifierMask` explicitly rather than relying on the default, so it really is
⌘, and not a bare comma.

### 2026-08-24 (rebinding, third time) — stop guessing, start reporting

The pop-up window changed the binding — the old key stopped working, which proved the table was
being written — but the new key did nothing, so what the window stored was not what the game
compares against. Two things were relying on `tag`: the pop-up's own tag carried the row, and each
item's tag carried the key code. `NSPopUpButton` is a control whose cell also has a tag, and one
word doing two jobs across two objects is not something to be confident about. Both are gone: the
pop-up is a small subclass with an `actionIndex` property, and an item is identified by its
position (item 0 is "nothing bound", item c + 1 is choice c), so nothing can get out of step.

⌘, is now handled in the SDL key path as well as by the menu item. Whether a menu key equivalent
is delivered depends on how the focused window was created, and SDL creates its own; catching the
key directly works either way.

And the window now shows what the game last did with a key press — its name, and whether it was
bound to anything. Three attempts at this feature were spent inferring what the machine was doing
from a description of what it was not doing. A settings window that reports what the game actually
receives makes the next question answerable in one look, by whoever is holding the keyboard.

### 2026-08-24 (last) — a row per press, and a keyboard for the name

Two reports, one root each.

**The wheel moved an eighth of a row a press.** `DETENTS_PER_KEY_PRESS` was 1, but the wheel has
120 detents to a turn and a menu moves a row every eight of them — which the oracle scripts have
always known, since they turn it in eights. Eight presses a letter. It is now `DETENTS_PER_ROW`
= 8, used by key presses and mouse-wheel notches alike.

**Typing a name, where the platform has a keyboard.** The iPod had none, so the game spells a
name out on the wheel, and it still does. A platform opts in by answering `true` from
`Platform::text_input_supported()` and filling in `FrameInput::typed` — the characters typed this
frame, the backspaces, and whether Return was pressed. `src/platform/text_entry.h` is the inbox
between the two halves; `name_entry_typing()` in `src/game/name_entry.cpp` applies what it holds
through exactly the steps the wheel's own handler uses, so the store, the length, the sixteen
character limit and the way Return finishes a name are all the ones that were already there. The
alphabet resource decides what a name may contain, so a character it does not hold is dropped and
lower case is folded to upper. The inbox keeps only the last 64 characters: nothing reads it away
from the name entry screen, and a stray afternoon of keys should not arrive all at once.

The null platform used by the tests does not support typing, so the oracle sees nothing new: six
cases identical, both vertex streams identical (59094 and 131021 draws), `ctest` 14/14 with
`tests/unit/text_entry_test.cpp` added.

**A consequence worth the change it forced.** A key bound to a control never types, which is what
keeps rebinding predictable — but it also meant the letters W, A, S and D could not appear in a
name. The macOS defaults moved off the letters entirely: the four arrows (← → for the wheel, ↑
menu, ↓ play/pause), Space to select, `[` and `]` for rewind and fast forward. A saved
`bindings.txt` would have kept the old defaults alive for anyone who had ever opened the settings
window, so the file now carries a `format` line and one written against another format is not
read. That is a blunt rule — it discards a player's customisations when a default moves — and it
is the right one while the defaults are still settling.

### 2026-08-24 (after) — the axis rebinding could not reach

Rebinding the scroll keys did not stop a two-finger swipe up and down from scrolling, because the
trackpad never went through the bindings at all: `SDL_EVENT_MOUSE_WHEEL` added `event.wheel.y`
straight to the detent accumulator. It now reads `event.wheel.x` — sideways, the direction the
menu actually moves — and negates it when SDL reports a flipped ("natural") trackpad. Swiping up
and down does nothing.

Worth stating as a rule: a rebinding table is only as good as the number of input paths that go
through it. This one had two paths and the table covered one.

The settings window's "Game last saw …" readout is gone, along with `note_input_seen` /
`last_input_seen` / `last_input_was_used` and the timer that refreshed it. It was built to answer
"did that binding take?" during three rounds of debugging, and it answered it; leaving debug
instrumentation in a player-facing window afterwards is not the same as leaving it in the source.

### 2026-08-24 (after that) — the settings window grows tabs

General, Input, Graphics, in an `NSTabView`. Input is what was already there. Graphics is empty
and says so. General holds the frame-rate switch, which is the same setting as Speed ▸ Lock Frame
Rate and the L key — so the window keeps no copy of it: `SettingsHooks` (in macos_settings.h)
carries a toggle to call and the value as it stands, and `macos_settings_set_frame_rate_locked`
reports it back whenever it changes anywhere, exactly as `macos_menu_set_locked` already did for
the menu item. Three ways to reach one setting is fine; three places that each believe they own it
is not.

`macos_settings_install` now takes that struct rather than a callback pair, which is what kept the
signature from growing a parameter per setting.

Rows are laid out against `[tabs contentRect]` rather than against guessed insets, so the panes
follow whatever the system draws for the tab strip. Two wordings were wrong and are fixed: the
frame rate is the game's own timebase (60 by default, `--fps=`), not "30 a second", in both the
window and the comment on `toggle_frame_rate_lock`.

### 2026-08-24 (later still) — the frame rate is a value, not a switch

The Speed menu is gone from the menu bar — `macos_menu.h`, `macos_menu.mm` and
`macos_menu_stub.cpp` are deleted, and with them the last thing this program added to the menu bar
besides Settings. General now has two controls: **FPS**, a pop-up of 30, 60 and Unlocked, and
**Show FPS in the title bar**.

So `frame_rate_locked_` became `frame_rate_` — the frames a second to pace to, with 0 meaning
unlocked — and `frame_interval_ns_` became a function of it rather than a value fixed at
construction. `--fps=` still sets the starting rate; a rate that is neither 30 nor 60 joins the
front of the pop-up rather than being quietly rounded to one that is. L now switches between
unlocked and `paced_rate_`, the last rate that was paced, so it is still the one-key shortcut it
was.

Neither setting is persisted yet — they last for the session, as the frame-rate lock always has.
The bindings are saved because a player rebinds once; a frame rate is something they change while
watching something.

**A warning about verifying this window.** Driving it with `osascript … keystroke "," using
command down` plus `screencapture` works only while the game is frontmost — one run sent ⌘, to
whatever had focus instead and captured a full screen of the user's own windows. Capture is a
blunt instrument on someone else's desktop: check what is in front first, or do not reach for it.

### 2026-08-24 (Graphics) — the last step, and only the last step

Settings ▸ Graphics: a **Scaling** pop-up (Sharp / Nearest / Smooth) and a **Whole multiples
only** switch.

What is *not* on offer matters as much. The rasteriser is fixed at 320×240 (`libeapp/gles.h`) and
the game's geometry is fixed-point maths tuned to that screen, so there is no higher internal
resolution to render at — and re-rendering at one would break the vertex-hash oracle, which is the
main thing keeping the decompilation honest. Every option here is about enlarging a picture that
has already been drawn.

**Sharp**, the new default, is the two-pass trick: nearest-neighbour into an intermediate texture
at the smallest whole multiple that covers the window (`update_prescale`, capped at 8×), then a
linear fit of that to the window. The linear pass only ever has a fraction of a pixel to soften,
so pixels stay square and equal-sized and edges stop crawling as the window is resized. At an exact
whole multiple it is pixel-for-pixel identical to Nearest, which is why it is safe as a default.
**Whole multiples only** is `SDL_LOGICAL_PRESENTATION_INTEGER_SCALE` in place of `LETTERBOX`.

`Scaling` lives in platform.h rather than in the SDL platform: it is a choice any platform that
enlarges the picture has to make, and macos_settings.h now includes platform.h for it.

Verified by screenshot: the render-target pass draws correctly under logical presentation (SDL3
keeps the view state per target, so no juggling is needed), and the Graphics pane lays out without
clipping. The safe way to capture: activate the process, **confirm it is frontmost**, ask System
Events for that window's bounds, and pass them to `screencapture -R` — never a full-screen grab.

Still not persisted: scaling, whole-multiples, frame rate, and the title readout all last for the
session only.

### 2026-08-24 (settings, saved) — one struct, one file

`src/platform/settings.{h,cpp}`: a `Settings` struct (frame rate, the title readout, scaling,
whole multiples), the single `settings()` every part of the program reads, and `load_settings` /
`save_settings` through the same store as the saved games and the bindings — `settings.txt`, with
the same `format` line the bindings file has, and the same rule that a file written against
another meaning is left alone rather than restoring a default that has since moved.

`Scaling` moved here from platform.h, which now includes this instead; `Platform` gained
`apply_settings()`, called once after the file is read and again whenever something changes them.
The SDL platform keeps no copy of any of it any more: every read goes through `settings()`, and
every setter writes it, applies it, and saves. What is still its own is `paced_rate_`, which is not
a setting but a memory of what L should go back to.

`--fps=` outranks the saved rate — it is an instruction for this run — which needed
`Options::frames_per_second_given`, since the option's own default is indistinguishable from it
being asked for.

Verified end to end, not just by unit test: pressing L wrote `fps 0` into settings.txt, a fresh
run came up unlocked, and `--fps=30` came up at 30 with the unlocked setting still in the file.
`tests/unit/settings_test.cpp` covers the defaults, the round trip, an unknown key, a foreign
format, and load-over-platform-defaults. ctest is 15/15.

### 2026-08-24 (the last of the wheel) — drift, and a window that keeps its shape

**Swiping up and down still moved the menu**, even after the vertical axis was dropped. The cause
was the horizontal one: a trackpad reports a fraction of a notch per event, and no swipe up the pad
is perfectly straight, so a long vertical swipe also carries a little sideways drift — and
`WHEEL_UNITS_PER_DETENT` is 1.0, so enough drift adds up to a row. Each event now goes to the axis
it is mostly along, and nowhere if it is mostly the other one.

Guessing at that would have been another round. `MINIGOLF_TRACE_INPUT=1` now prints every wheel
and key event, and stays: what a device sends differs by device and by the system's settings, and
there is no other way to see it from inside the program. It earned its keep immediately — an
automated `=` keypress arrived as `SDLK_KP_EQUALS`, not `SDLK_EQUALS`, which is why the zoom keys
accept both.

**The window keeps the screen's shape.** `SDL_SetWindowAspectRatio` locks it to 4:3 however it is
dragged, so the picture fills the window and there is nothing to letterbox. Locking it can leave
the window at a size of the system's choosing, so the wanted size is asked for again afterwards.
`-` and `=` step through whole multiples of 320×240, 1× to 8×, subject to the same rule as F and L:
a bound key wins.

Verified with the window itself: start 960×720, `=` → 1280×960, `-` `-` → 640×480, and forcing
1200×700 from outside settles at 890×668, which is 4:3.

### 2026-08-24 (the wheel, settled) — judge the swipe, not the event

The trace answered it in one reading. Two things the previous fix could not survive:

    wheel x=-0.400 y=3.900     a swipe up the pad drifts sideways by a quarter of its travel
    wheel x=-1.000 y=0.000     and its tail arrives with no vertical part left at all

The second is fatal to any event-by-event test — `|x| > |y|` is trivially true when `y` is zero —
and one unit is a whole menu row, so a single leaked event moves the menu.

`src/platform/swipe.{h,cpp}`: `SwipeFilter` accumulates both axes with a decay of 0.85 an event and
answers whether the swipe *so far* is a sideways one; 250 ms of quiet starts a new swipe. Decay
rather than a plain sum, so that a sideways swipe following an up-and-down one with no pause takes
over within a few events instead of being swallowed by it.

It is portable and tested: `tests/unit/swipe_test.cpp` replays four streams captured from the
user's own trackpad with `MINIGOLF_TRACE_INPUT=1` — up, down, across, and across-immediately-after-
up — and pins that not one event of the vertical swipes counts while every event of the sideways
one does. Three attempts at this were spent reasoning about what a trackpad ought to send. The
fourth had the numbers. ctest is 16/16.

### 2026-08-24 (the wheel, removed) — the right answer was to delete it

Asked for plainly: no swipe should do anything. The `SDL_EVENT_MOUSE_WHEEL` path is gone, and with
it `src/platform/swipe.{h,cpp}`, `tests/unit/swipe_test.cpp`, the accumulator and
`WHEEL_UNITS_PER_DETENT`. The keyboard is the only way in, and every key is one the player can see
and change in Settings ▸ Input.

Four attempts went into making a trackpad turn the menu correctly, and the fourth one worked. It
should not have been attempted at all: a gesture nobody chose, that appears in no settings window
and cannot be rebound, is not a control — and the physical fact underneath (a quarter of an upward
swipe's travel lands on the sideways axis, and its tail lands there alone) meant every version of
it was a heuristic guessing at intent. Deleting the feature removed the whole class of bug, and the
diff is smaller than any of the fixes.

ctest is 15/15 again: the swipe test went with the code it tested.

### 2026-08-24 (the pause menu) — a firmware duty the host never took on

Opening the pause menu froze the game: the title and its underline drew, no rows appeared, and the
picture never changed again.

Nothing was wrong with `pause_menu.cpp`. The rows were loaded, styled and positioned correctly —
`x` was `MENU_START_X`, off the right edge, waiting for the slide-in that a tick would have done.
Tracing upward: `run_screen` stopped being called, then `flow_step`, then `game_step`, which is
gated on `app_state().mode == 2`. The mode had gone to 4, which is suspended, and the reason was
one line in `handle_long_presses`:

    MENU held for 456909138 us, limit 4000000

The game works out how long Menu or Next has been held from when it last saw the button go *down*,
which it learns from the firmware's list of button events (`dispatch_buttons` in game/input.cpp
records `menu_press_time`). Holding either for four seconds is how an eApp is told to suspend
itself. This host has no event list — `ClickWheel::press` writes the button-flags word directly —
so `menu_press_time` kept whatever start-up left in it, and a press seven minutes into a round read
as a hold seven minutes long. `press` now also records the press time for those two buttons, which
is the truth here: a press lasts exactly one frame.

**Why the oracle was green through all of this.** The recordings were made with the emulator, whose
`play.rs` takes the same shortcut, so they contain the freeze. Three cases (hole, menus, options)
diverged the moment the fix went in. `--emulator-firmware` reproduces the shortcut deliberately and
`tests/diff.sh` passes it, so the oracle still compares like with like; nobody should play with it.

That leaves a gap the oracle cannot cover, so `tests/pause-menu.sh` covers it directly: open the
pause menu, resume, and check the picture is still changing (two screenshot hashes that must
differ). Verified both ways — it fails with the fix reverted. ctest is 16/16.

The general lesson: this host implements the firmware's side of an interface, and the parts of that
side nobody has needed yet are invisible until the game asks. The oracle cannot find them, because
the emulator the recordings came from has the same gaps.

### 2026-08-24 (two keys apiece) — Escape is Menu, and the arrows agree

Asked for: Menu on Escape, and the up and down arrows to work in menus. The second one does not fit
a table with one input per action — ← and → were already the wheel — so the table now holds two
inputs per action (`BINDING_SLOTS`), and both perform it. "One input does one thing" still holds
across both slots: binding a key takes it from wherever it was.

Defaults: ← / → and ↑ / ↓ both turn the wheel, Space selects, **Escape** is Menu, Tab is
play/pause, `[` and `]` are rewind and fast forward. Escape no longer quits the program — Q still
does — and it joins the assignable list, which it had been kept out of precisely because it quit.

Return is deliberately left unbound: it finishes a typed name (see Typing in the README), and a key
bound to a control never types, so binding it to Select would take the keyboard's own confirm away.

The bindings file gains a second code per line and the format goes to 3, so files written against
the old defaults are ignored rather than restoring ↑ as Menu. The Input tab has a second pop-up per
row; the window is wider to fit it.

Not visually verified: a copy of the game the user had started themselves was in front, so the
capture recipe would have driven their window rather than mine — twice it did, and once it caught
their desktop. The layout arithmetic is checked (two 160pt menus at 150 and 320 inside a 496pt
pane), ctest is 16/16, and the pop-ups are the same code as before with a slot index added.

### 2026-08-24 (pages) — a screen with no way out

Reported as "Escape in a submenu freezes the game". Escape was innocent: it is the Menu button now,
and Menu on a *page* — Statistics, Help, Volume, Brightness, the mode descriptions, the reset
notice — did nothing at all, which looks exactly like a freeze, since a page has no animation of
its own to give the lie away.

The trace made it plain. On the Menu press the panel shrank as it should, and then every single
tick called the page's handler with the button that closed it:

    PAGE tick input=6 growing=0 scale=65536 step=6554  pressed=5
    PAGE tick input=7 growing=0 scale=0     step=-6553 pressed=6   <- and this, for ever

`page_handle_event` leaves a page by calling the screen's `next_enter`, and `page_enter` is what
works out where each page leads — the statistics page back to the main menu, a help page onward,
the volume page back to whichever menu opened it. It set `current_screen().next_enter` at the top,
and then at the bottom called `screen_install(...)`, which writes **all four** of the screen's
fields, the fourth being `next_enter`. It was overwritten with null before the page was ever
displayed. `page_enter` now keeps the choice in a local and hands it to `screen_install`.

Every other screen sets `next_enter` after installing, or lets its handler set it; page.cpp was the
only one that did it the other way round, and it is 2.6% covered by the oracle — the least-tested
file in the project (see the coverage note earlier). No recorded case opens a page and comes back,
which is exactly why six green oracle cases said nothing about it. `tests/page-back.sh` now does:
open Statistics, press Menu, and check the picture changes. ctest is 17/17.

`MINIGOLF_TRACE_INPUT` also now names what each key is bound to, which is how Escape was cleared of
suspicion in one line:

    key 0x1b (Escape) Menu

### 2026-08-24 (the last function) — 334 of 333

"Return to Menu" ended the process. The row opens the card that asks whether to save first, and
that card's renderer, 0x18014300, was a stub:

    // The original's code was never reached in any recorded run and is not decompiled;
    // nothing may install it and then draw a frame.
    uint32_t card_render() { assert_trap(0x18014300u); return 0; }

It was honest, and it was right until something installed it and drew a frame. Now decompiled: 260
instructions, and shorter in C++ than it looks, because most of it is three things this project
already has — `background_draw(Blend::Alpha)`, `logo_draw(Blend::Alpha)` and the panel, then two
rows drawn from the menu items with `highlighted_row_draw` for the one the cursor is on. The card
carries no message of its own, which is why it is simpler than `dialog_render`.

**Verified against the original, not by eye.** The pure recompilation runs the real ARM for this
function, so it is the reference: with `0x18014300` added to `analysis/extra-entries.txt` (Ghidra
had run it into the function before it, which is why the pure build could not call it either) both
builds draw the card frame for frame — `a0bff0f7` and `14d075e6` on the two card frames, identical.
That is the strongest verification available here, and stronger than the call-log oracle, which
compares calls rather than pixels.

`tests/return-to-menu.sh` keeps it: play a hole, pause, choose Return to Menu, and check the run
survives and the card is a different picture from the pause menu. ctest is 18/18.

Also: `halve` existed five times, once per renderer, and adding a sixth was the moment to stop —
it is `ui.h`'s now.

### 2026-08-24 (Exit) — nothing to hand back to

"Exit" on the main menu answers 1 from the menu handler, which the flow turns into "suspend me" and
`game_tick` answers with `SUSPENDED` in the context's state byte. On an iPod that is the whole
story: the firmware takes the eApp away and its own menu comes back. This firmware ignored it and
went on calling a game that had stopped stepping, which is a freeze wearing a different hat — the
same hat the long-Menu-press bug wore.

The frame loop now ends when the game reports itself suspended. That covers Exit, and it would
cover anything else that asks to be put away.

`--emulator-input` is renamed `--emulator-firmware`, since it now stands for two differences
between this firmware and the emulator's rather than one: no button press times, and running on
after a suspend. The oracle needs both to compare with recordings that contain a suspend.

`tests/exit.sh`: choose Exit, and the screenshot the script asks for afterwards must never be
taken. ctest is 19/19.

### 2026-08-24 (the menu's frame rate) — fifteen milliseconds a row

Moving the cursor plays a sound, and each sound was opening its own audio device. Measured on the
spot rather than guessed at:

    VOICE load 0.12 ms, open 15.28 ms
    VOICE load 0.14 ms, open 15.62 ms

`SDL_LoadWAV` is free; `SDL_OpenAudioDeviceStream` costs about 15 ms, on the game's own thread,
inside the frame. A frame at 60 Hz is 16.7 ms, so one row moved cost most of a frame and holding a
direction cost one per row. It was never a menu problem — every sound in the game paid it — but the
menu is where they come fastest.

Now the streams are opened once and kept: `VOICE_LIMIT` voices, each with a stream for the life of
the program, re-armed with `SDL_ClearAudioStream` + `SDL_PutAudioStreamData` and re-formatted with
`SDL_SetAudioStreamFormat` if a clip needs it. The .wav files are read once into a `Clip` cache as
well, which was already cheap but is now free. Same measurement afterwards:

    SOUND c00bank/1.wav took 0.00 ms

The polyphony rule is unchanged — four voices, a retrigger restarts the voice already playing that
sound — so nothing about how the game sounds should differ. ctest is 19/19.

### 2026-08-25 — a fourth platform: the Nintendo Switch

`src/platform/switch/` is a homebrew build for HorizonOS: libnx, a linear framebuffer, the pad,
and a small mixer over `audout`. `tools/switch-build.sh` builds `build-switch/minigolf-switch.nro`
in devkitPro's own container, which is what makes it buildable at all here — devkitA64 installs
under `/opt` and this machine has no password to give it. Docker is the only local requirement.

**The port was mostly not about the Switch.** Cross-compiling for aarch64 with GCC found 40 real
portability defects that Apple clang had been quietly accepting, and they are all in shared code:

* 15 references bound to fields of packed structures, which GCC refuses outright — every guest
  overlay in this project is packed, so this was `dispatch_buttons`, the batcher's slot tables,
  `page_enter`'s pack handles, the sound levels, the sprite-sheet loader. Each is now addressed by
  its guest address, which is what the original works with anyway.
* 11 addresses taken of packed members — the vertex buffers, the object matrices, the hole's quads.
  The matrices are now read out with `matrix_read`; the rest went the same way as the references.
* Two array subscripts genuinely out of bounds: `screen[0x9a]` and `save_data[0x4d]` reach past the
  arrays that name them into the block behind, which is how the original addresses them but not
  something a C++ compiler has to allow. `screen_block_byte` and `save_data_byte` say it plainly.
* Half a dozen sign-conversion and narrowing errors, and one `std::fill` over a packed array.

The oracle stayed green through all of it, which is the only reason a change of that size was
safe to make.

**Memory.** The guest address space is 790 MB, nearly all of it the gap between the image and the
IRAM. `memory.cpp` now has two models: the flat mapping for hosts with demand paging, and four
allocated regions (about 28 MB) for consoles. The region sizes come from measurement — the longest
recorded session reaches `0x180e1b24` in the image and `0x1940c5cd` in the heap — and overrunning
one is a bounds error rather than corruption. `-DMINIGOLF_REGION_MEMORY=ON` builds the console
model on a desktop, and all six oracle cases are identical under it. That is as close to running
the console build as this machine gets.

**Not done:** music (AAC, no decoder), the software keyboard, and a settings screen. The buttons
are registered as assignable inputs so that screen has something to build against when it comes.

**Two things the port added for everyone.** `src/platform/wav.{h,cpp}` is a portable .wav decoder —
a console is handed one PCM stream and has to do its own mixing, where SDL hands a file to the
system — and it is tested on the desktop (`tests/unit/wav_test.cpp`, which caught a length check
that could have read past the end of a file). `set_fatal_handler` lets a platform put a fatal
message where the player can see it: on a console stderr is a place with nobody in it, so the
Switch prints it on the console and waits, and the missing-game screen now says *what* is wrong
with the files rather than assuming they are absent.

**How much of it is actually verified.** The console model is not hand-waving: `-DMINIGOLF_REGION_MEMORY=ON`
builds it on the desktop, where all six oracle cases are identical, both vertex streams are
identical (59094 and 131021 draws), and ctest is 20/20. The exact oracle on the pure recompilation
still passes five of six cases with next-hole's known 20-line difference unchanged. What is *not*
verified is everything that only exists on the console: the framebuffer blit, the pad, and audout.
Those cross-compile clean, link against libnx, and have been read carefully — nothing more.

**A controls screen on the console.** `src/platform/switch/switch_settings.cpp`: the seven actions
and their two buttons each, drawn as text on libnx's console — which is the same display as the
game's framebuffer, so it changes hands while the screen is up. Minus opens it, Plus quits, and
those two are the only buttons that cannot be bound; everything else is the portable table doing
its job, which is why the screen is fifty lines. Asking the player to press the button they want
is safe here in a way it was not on macOS: reading the pad is one call that nothing can intercept.

### 2026-08-25 (the second oracle) — a script no longer needs a recording

The blocker at the top of this file — new cases cannot be added because the emulator no longer
reproduces its own recordings — turned out to be about the wrong reference. The pure recompilation
*is* the original code: every ARM instruction translated, nothing hand-written. Running a script
through both builds and comparing what they ask the frameworks for tests exactly what a recording
tests. `tests/vs-recomp.sh` does that, and every script in `tests/scripts/` without a recording of
its own becomes a test automatically.

Four cases arrived with it: `pages` (the Volume, Statistics and Help screens — `page.cpp` from 2.6%
of lines covered to 31%), and the three regression scripts written earlier today, which are now
compared call for call rather than only "did the picture change". `return-to-menu` agrees on
2 299 071 calls and 10 screenshots, which is a second, independent verification of this morning's
`card_render`.

**It found a gap immediately.** The `exit` case diverged: the decompiled game wrote a saved game on
its way out and the pure recompilation did not. The store's three ordinals (#12, #14, #16) had
never been given ARM adapters when the store was made real, so the pure recompilation kept the old
stub that answers "not ready". `src/libeapp/arm_abi.cpp` has them now, and `imports.json` carries
their real arity, so the logs compare their arguments too.

**And that let the allowance file go.** `--emulator-firmware` now also stubs the store, which is
what the emulator did when the recordings were made, so all six recordings compare with no
exceptions at all: `tests/expected/save-store.allow` is deleted and `tests/diff.sh` has no
`--allow` path. The counts went back up to what they were before the store existed — 567 734 for
menus, 1 035 427 for options, 1 629 283 for hole — because the calls that were being dropped are
compared again. The exact oracle is 5 of 6 as before, with next-hole's known leftover difference.

**And then the coverage.** With cases no longer needing recordings, the two documented holes
closed in an evening: `pages` (the Volume, Statistics and Help screens) and `pause-menu` (the menu
opened mid-hole and each of its screens visited — Volume, the score card, Help — then Resume).
Measured over every case: `src/game/` is 72% of lines and 86% of functions, up from 66% and 78%;
`page.cpp` 2.6% → 77%, `pause_menu.cpp` 17% → 61%. `hole_tick.cpp` is still 45% and is now the
whole of what is missing.

One thing the pause-menu script taught, which is in its header: turning the wheel while the hole is
still placing the ball resumes the game rather than moving the menu's cursor. That is the game's
own behaviour, and it is why the case pauses after the second putt.

**A note on tonight's GUI checking.** The SDL build could not be looked at after about midnight: the
Mac locked, and a locked screen means `screencapture` returns black, System Events reports no
windows, and CoreAudio refuses to start a stream. None of that is the program — it builds clean,
runs at exactly 60 frames a second, and prints nothing. It is worth knowing before chasing a ghost:
`osascript … get count of windows of process "minigolf"` answering 0 says as much about the screen
as about the window.

**One more case, and where the line is.** `strokes` putts fourteen times at hole 1 with the aim and
power varied, reaches the stroke limit and is moved on: `hole_tick.cpp` 50% → 61% of lines, the
whole of `src/game/` 72% → 74.5%, functions 88%. Twenty putts covered no more than fourteen, so
fourteen is what the case does.

What none of it reaches is the ball going in. `tick_holed`, `tick_sinking` and `result_text` need
an aim that cannot be worked out from outside the game, and guessing at it costs a minute a try.
That is the honest edge of what scripted input can cover here, and it is written into the case's
own header so the next person does not spend an evening rediscovering it.

### 2026-08-27 — six things the iPod game did not have

The first features that are the port's rather than the original's: a Cheats screen, a per-hole
record with the ball's path in it, and gamepads. The rules that keep them from quietly corrupting
what the game already keeps turned out to be the interesting part, and they are written into
`src/game/cheats.h` and `round_history.h` rather than left implicit.

**A menu row needs a word for itself.** Every row's label is a resource id the course pack
resolves, and no pack has a word for "CHEATS" in any of its eleven languages. `host_text.h` spends
the top bit of a text id: an id with bit 31 set is one of this port's own labels, resolved by
`host_text_load` into the same scratch buffer `resource_load` fills, in the same encoding. Two
call sites route through it — the renderer and the slide-out's width measurement — and after that
a row this port added is indistinguishable from a pack row, ripple and ball and all. The labels
are ASCII and untranslated, which is honest rather than ideal: every one of the game's fonts
indexes its advances from the space, so ASCII draws in the UTF-16 language too, but it is still
English on a Japanese page.

**Two rules, decided once.** A cheat is a *choice*, not a change to the saved game: unlocking the
courses widens the gate the carousel reads (`courses_available`) and never touches the 0x144-byte
record, so turning it off gives back exactly what was earned and a save carried elsewhere is
untouched. And a round played with a rule-changing cheat *does not count* — no best round, no
statistics, no unlock, nothing in the round history. It is sticky on purpose: switching a cheat on
for one bad hole and off again before the card is written still voids the round. Ghost trail is
marked an aid rather than a cheat, because it shows a player their own path on a hole they have
already finished and so cannot tell them anything about one they have not.

**A hole is only finished when it is holed.** Tracing why hole 1 recorded and hole 0 did not found
a real seam in the original: a hole that runs out its strokes reaches the score card by a
different route entirely — `stroke_limit_reached` sets the result to "holed", and `ball_rest_record`
opens the card the moment such a result comes to rest — so `tick_hole_done` never runs for it in
single player, though it does in Practice Hole. Rather than let the two modes record different
things, `hole_abandoned()` says so explicitly at the one place that knows.

**Both oracles were right to complain, and that is the point of them.** A seventh row on Options
is a real difference from the recordings: `tests/diff.sh options` found it at call 426 035 of
1 035 427. Then the *second* oracle found the other half — `recomp_page-back` and `recomp_pages`
failed, because the extra lines on the Statistics page are not in the pure recompilation either,
which is the original code and has no port in it at all.

The fix is not to teach either oracle to overlook the difference but to name it. One switch,
`set_port_additions_hidden` (host_text.h, where "what this port adds to the UI" already lives),
takes away the Cheats row and the extra statistics together. `--emulator-firmware` sets it for
the recorded logs, and a new `--no-port-additions` sets it for `vs-recomp.sh`, which gives it to
the decompiled side only so that both builds are drawing the same game. All three cases are
identical again — 1 035 427, 783 968 and 2 085 784 calls, and the pictures too.

**And the pure recompilation nearly stopped building.** `MINIGOLF_GEN_DIR` builds without
`src/game/` at all, so the port's start-up calls in `main.cpp` — `load_cheats`,
`load_round_history` — were three undefined symbols waiting to happen. A
`MINIGOLF_PURE_RECOMPILATION` define, set from CMake in the same place that build already gets
its include path, compiles them out. Worth remembering: anything the runtime calls in `src/game/`
has to be guarded, and the ordinary build will never tell you.

**Three slots, not two.** A gamepad button should not cost the keyboard one of its two bindings,
so `BINDING_SLOTS` is 3 and the file format is 4. `from_text` already read a file with fewer slots
than the build has, so old files load and keep their defaults for the new one; the macOS window
was written against `BINDING_SLOTS` throughout and grew a third column with no change at all. The
left stick is not a bindable button — it *is* the wheel, and that is the point of it: deflection
squared, 48 detents a second at full throw, and the leftover fraction carried between frames, so
a stick barely off centre turns the wheel slowly instead of not at all. That is the resolution a
key cannot express and a putt wants.

**A lead, found by accident and not chased.** `tests/scripts/cheats.script` is auto-paired by
the second oracle like every other script, and `recomp_cheats` failed. It is not the cheats: with
`--no-port-additions` the decompiled side shows the same six rows the pure recompilation does, all
five screenshots are identical, and both builds walk the same menus. What differs is one argument
to a sound call —

```
recomp: 4311 Audio#5 00000009 00000009 18040ee8 ... from 18004ab4
decomp: 4311 Audio#5 00000009 00000000 00000000 ... from 00000000
```

— the second word, 9 against 0. (The trailing zeros and `from 00000000` are the ordinary shape of
a call made from decompiled C++ rather than through the ARM shim; `diff.py` ignores those, and
every passing case shows them too. The second argument it does not ignore.) Six rows means the
script's `wheel +48` clamps onto Reset Game and opens its dialog, which is a path *no* existing
case covers — the README's coverage notes say as much. So this is most likely a real gap in the
decompiled sound path that nothing had reached before, and it wants a script of its own rather
than being smuggled in on this one.

The script is therefore left out of the pairing (`PORT_ONLY_SCRIPTS` in CMakeLists.txt) — a
script written to drive a screen the pure recompilation does not have, and that the decompiled
side is told to hide, cannot do that oracle's job either way — and `tests/cheats.sh` drives it
instead. The Audio#5 difference is written down here so it is not lost with it.

**What was checked.** `tests/cheats.sh` drives the screen itself — opened, two cheats toggled,
Menu to leave — and reads the file back, because neither oracle can see a feature that both are
told to hide. `unit_cheats_test` and `unit_round_history_test` pin the saved forms, the
voiding rule and the decimation; the Cheats screen, the unlock and the ghost were driven through
scripted sessions and read off screenshots (the third course's picture in place of its
placeholder, the ghost's dotted arc across hole 1). The ghost's first alpha was too faint to see
against the course art and is now the aim line's own dot at a little over half its brightness.

The aim guide was checked by counting rather than squinting, which is worth copying. The same
script was run with the cheat off and on, and the near-white pixels counted in each of
twenty-four frames spanning the power meter's swing. Off, the count sits at 479 every frame — the
aim line holds its full reach while the meter moves, which is the original's behaviour. On, it
runs 447, 451, 455, 463, 467, 475, 479 and back down: a clean sine, because `power_meter_value`
is one. Two screenshots could not have told those apart; twenty-four and a pixel count can.

`round_finished` is the one path not driven end to end. It needs all eighteen holes of a course
holed out, and scripted input cannot aim a putt — the note on the `strokes` case above says why.
Its arithmetic is covered by `unit_round_history_test`, and its call site is the line after the
best-round write that every course completion already runs.

The gamepad is the other thing not exercised: there is no controller on this machine, and SDL
will not report one that is not there. It compiles, it is bound by default in the third slot, and
the settings window lists its buttons because they come from the same table — but nobody has yet
pressed one. That is the first thing to try with a pad to hand.

### 2026-08-27 (the sound) — three settings that did nothing, and the end of afplay

Reported as three faults: Music off did not stop the music, Sound FX off did not stop the
effects, and the Volume slider did not change the volume. They turned out to be three different
things, only one of which was where it looked.

**Sound FX was already right.** Measured before touching it: a scripted session that turns the
option off makes 61 `Audio#2` calls before the toggle and none after. The gate in
`menu_sound_play` (OPTIONS + 9, which is the option's own byte) works. What was missing was
smaller — `Audio#5`, stop, only wrote itself to the log — so a sound already sounding played out.
That one is now a stop request like any other.

**Music off was a call into nothing.** Turning Music off is `audio::stop_music` and nothing else
(menu.cpp), and #45 logged itself and returned. The music was a child `afplay` process, which had
no way to be stopped short of a signal and no volume this program could set. #45 now queues a
stop, `Platform::stop_music` carries it, and the trace shows the whole cycle: `music m0.m4a`,
`music stopped`, `music m0.m4a, repeating`.

**The Volume slider still does not move, and chasing it found something bigger.** The level is
walked by `music_level_adjust`, whose two branches look crossed — event 0 takes its step from
slot 1, event 1 from slot 0 — and the first move was to "fix" that. It is not a fault: the
disassembly reads +4 in the event-0 branch (0x18013ffc) and +0xc in the event-1 branch
(0x1801401c), which are slots 0 and 1, crossed exactly as written. Swapping them made the slider
move, and `recomp_pages` immediately said the moving was wrong — 143 `Audio#53` calls against the
original's 43, the same destination by a different ramp. Reverted.

The real difference is upstream. Dumping `play::MUSIC_LEVEL` (0x180d0da4) frame by frame through
both builds on the same script: the pure recompilation walks the level 0 → 40 over forty frames,
and the decompiled build walks it nowhere. Since `music_level_adjust` is the same in both, what
differs is which slot the wheel's direction is put *into* — `input_gather` (input.cpp,
0x180082c4), which sends negative movement to slot 0 and positive to slot 1. The original
evidently does the opposite, which is what makes the crossed read above land on the slot that
moved.

That is not a line to change on the way past. Every screen reads those two slots — the menus, the
aim, the hole — and `selection` is derived from which of them fired, so swapping them there
reverses what the wheel does everywhere. It wants a change of its own with both oracles on it,
and it now has a reproduction, an address and a measurement to start from. Written up rather than
guessed at.

What *is* fixed is everything under the slider: a level the game sets is kept, reported back,
and applied as the gain on every stream. When the page can move it, it will control the volume.

**The starting volume is the port's, and the oracles get the emulator's.** #51 answered zero, so
the page drew an empty bar however loud the game was. It now answers the level it holds, which
starts full — what hardware would report for a player who had not turned it down. That is a
deliberate difference from both oracles, which start from the emulator's stubbed zero, so
`--no-port-additions` and `--emulator-firmware` set it back; `vs-recomp.sh` now gives that flag
to both sides, since the pure recompilation links libeapp too. All three cases identical again.

**afplay is gone.** The tracks are AAC, which SDL does not decode, and that was the whole reason
for the child process. `src/platform/sdl3/music_decoder.h` is the seam instead: AudioToolbox's
`ExtAudioFile` on macOS — a system framework, not a new dependency — decoding the track as it
plays, a chunk at a time, into an SDL stream fed exactly as a sound effect's samples are. A track
is not decoded up front: the longest is 115 seconds, about 20 MB, and reading it in one go would
cost that and a visible pause at the start of every course. Everywhere else
`music_decoding_supported()` answers false and the game plays on in silence after saying so once,
which is what it did before on those platforms anyway.

**What was checked.** `unit_audio_test` pins the requests and the level, including that reading
the level does not disturb it (the Volume page reads it every frame). The decoder and the stop
were driven through the real SDL build with `MINIGOLF_TRACE_AUDIO=1` — a new trace in the same
spirit as `MINIGOLF_TRACE_INPUT`, and for the same reason: sound that does not come out has no
other symptom and nothing on screen to read. It prints the whole cycle: `music m0.m4a (44100 Hz,
2 ch)`, `music stopped`, `music m0.m4a, repeating`. `oracle_options`, `recomp_pages` and
`recomp_pause-menu` are identical again after the revert.

The lesson worth keeping: three faults were reported and the first two were where they looked,
while the third was two layers below the screen it was reported on. Measuring each one before
touching it — 61 sound calls before the toggle and none after; 43 level changes in the original
against 0 here — is what kept the fix for the third from being the wrong fix, twice.

### 2026-08-27 — the render scale, ported back from the Lost recomp

The second title grew three things this one wants, and none of them is title-specific: an optimised
default build, a rasteriser that is several times faster, and a **render scale** — drawing at a
whole multiple of 320x240 and handing the platform the larger picture. All three are now here.

They were applied by hand rather than copied. `tools/port-from-minigolf.py` runs one way, this
tree to that one, and the files involved have diverged a long way since: `src/libeapp/gles.cpp` is
800 lines here against 1708 there, with 1156 differing, and much of that difference is Lost's own
render server and paletted formats. What came over is the change set, not the file. Every step was
checked against `ctest`, which is 30 for 30, and against 46 screenshots from five scripts compared
byte for byte with a build made from the backup taken before any of this.

**The build default was the largest single win and it was a default, not a bug.** `CMakeLists.txt`
made RelWithDebInfo the default and RelWithDebInfo `-O1 -g`, deliberately, so the recompiled code
stays steppable. That is the right configuration to *debug* a recompilation in and the wrong one
to hand everybody who follows the README, because the renderer is software: there is no GPU to
take the per-pixel work, so the optimiser's output is the frame rate. Release is now the default
at `-O3 -g` and RelWithDebInfo is still `-O1 -g`. `assert` stays live in both.

Over 29 200 frames of `strokes.script`:

| | ms/frame | |
|---|---|---|
| as it was, `-O1` | 2.93 | |
| `-O3` | 2.40 | x1.22 |
| a span per scanline, texture hoisted per draw | 2.05 | x1.17 |

The first is smaller here than in the Lost tree, where the same change was worth x1.70 — this game
spends much more of its frame in its own recompiled code and much less in the rasteriser.

**Two rasteriser changes, both proved pixel-identical.** Each edge function is affine in fx at a
fixed fy, so a row's span is three divisions rather than two per pixel, and the two thirds of a
bounding box that a pair of triangles does not cover need never be visited; the span is widened by
a pixel at each end and the original per-pixel test is kept, so it decides which pixels are looked
at and never which are drawn. The texture is now looked up once per draw rather than once per
triangle — a `std::map` lookup each — which is also what lets the "never uploaded" fatal be raised
before any worker has been handed anything.

**Then the render scale.** It is applied in exactly one expression, `project`, and nothing else in
the rasteriser knows the game's coordinates from the raster's. The framebuffer is a vector that is
reallocated when the scale changes; `glCopyTexImage2D` averages the `scale`x`scale` block back down
because the game names that rectangle and that texture in its own pixels; the SDL texture is
rebuilt when the size changes, and a picture larger than the window is now *reduced by halves*
rather than point-sampled, because magnification and reduction want opposite filters.

**One thing came with it that had to, and it changes the 1x picture.** This rasteriser had no 1:1
rule — it always sampled bilinearly — so raising the scale would have magnified every sprite blit
with bilinear and made the art blurrier rather than sharper. `is_one_to_one` came over with it, and
asks its question in the *game's* pixels so that raising the scale does not turn it off. Of 46
screenshots at scale 1, **45 are byte-identical and one differs**: a snake sprite on `next-hole`,
1.85% of that frame, which is now sampled nearest because it is a 1:1 blit — visibly sharper, and
the same fix the Lost tree made for the same reason. It is the only deliberate change to this
game's 320x240 picture in the whole port.

Over 3 000 frames of `hole.script`, all ten cores:

| scale | picture | ms/frame | |
|---|---|---|---|
| 1x | 320x240 | 2.41 | one thread; below the sharing threshold |
| 2x | 640x480 | 1.87 | *faster than 1x* — four times the pixels, but shared out |
| 4x | 1280x960 | 5.88 | |
| 8x | 2560x1920 | 25.39 | |

**And the settings window had never read the settings.** `macos_settings_install` runs from the
platform's constructor, before `load_settings`, so the values copied into the window's hooks were
a snapshot of the *defaults*. Every open showed those, whatever the game was doing; the frame rate
escaped it only because `apply_settings` pushes that one value back afterwards. `refresh` now reads
`settings()` live, as the Input tab has always read `input_bindings()`. The same bug was found and
fixed in the Lost tree on the same day.

**Not verified.**

* The Cocoa layout by eye. The Graphics tab gained a control and the arithmetic leaves room, but
  the window has not been opened and looked at.
* Anything on a machine that is not this M1 Max.
* That `-O3` is safe for `gen/` beyond what the oracles say. They say a great deal — the exact
  call-log comparison and the recomp-vs-decomp pair both pass, and for one run that pair was an
  `-O3` decompiled build against an `-O1` pure recompilation — but the generated code has not been
  stepped through at that level.
* Whether the 96k-pixel floor below which a draw is not shared out is near its best. It happens to
  put 320x240 below the line and 640x480 above it, which is why 2x is faster than 1x; that is a
  real effect and not a tuned one.

**Still divergent, and this is the argument for a shared core.** The same change set has now been
written twice, by hand, into two copies of the same rasteriser. `recomps/common/` is named in the
Lost tree's "Not today" list; this entry is the strongest case yet for doing it.

### 2026-08-27 (closing it, here too) — SDL outliving nothing, a second time

Reported after the port: closing the game crashes it. The report says the rest.

    SDL_DestroyAudioStream -> SDL_UnbindAudioStream -> pthread_mutex_lock
      MusicPlayer::~MusicPlayer()      sdl3_platform.cpp:205
      Sdl3Platform::~Sdl3Platform()    sdl3_platform.cpp:437

**This is not the port's doing, and that was checked rather than assumed.** The same scripted run
exits 139 on a build made from the backup taken before any of this work began, and 139 on the
build with the port in. It has been here all along.

A destructor body runs *before* the members of the class it belongs to are destroyed, and
`SDL_Quit` was in that body. So `MusicPlayer` gave its audio stream back to a subsystem that had
already gone, and dereferenced freed memory doing it. It needs music to have actually played — a
player that never opened a stream has nothing to give back — which is why a scripted run of 900
frames reproduces it every time and a shorter one does not.

The fix is the one the Lost recomp already had: an `SdlSession` whose only job is to call
`SDL_Quit` from *its* destructor, declared before every member that holds anything of SDL's, so
that it is destroyed after all of them. `SDL_Quit` is gone from the body.

**The uncomfortable part is that this was a known bug with a known fix.** Lost hit the identical
fault — same three frames, `Voice` instead of `MusicPlayer` — on 2026-08-26, and the fix was
sitting in that tree while this port copied the render scale out of it and left this behind.
Porting a change set is not the same as porting a tree, and nothing about the way this was done
would have noticed.

**Neither would the tests, and that is structural.** `ctest` runs `minigolf-headless`, which links
`src/platform/null/` and never touches SDL, so no oracle in this project can see a fault in the
windowed build's shutdown. The reproduction is one line — a scripted windowed run long enough for
music to play, and its exit code — but it needs a display, so it is written down here rather than
added as a test that would fail wherever there is not one.

`ctest` is 30 for 30, and the windowed build now exits 0 at render scale 1 and at 4.

### 2026-08-27 (a shared core, step one) — the recompiler stops being copied

The recompiler moved to `recomps/common/tools/recomp/` and both titles import it from there. This
tree no longer has a `tools/recomp/`.

Two things this title had to hand over as parameters rather than assume. The namespace, which
`emit.py` now passes as `namespace="minigolf"`. And the runtime/game split: the shared function
table takes a *set* of runtime entries, so `GAME_CODE_START` and `RUNTIME_TAIL_START` moved into
`tools/funcs.py`, which computes the set from them. They belong here — they were measured from
this image, and the note that establishes them came with them.

Verified by regenerating `build/gen-pure/` and comparing: all 20 generated files byte-identical,
plus one new `emitted.json` that the newer emitter writes. `ctest` is 30 for 30, including the six
`recomp_*` cases that build the pure recompilation out of exactly that tree.

The reasoning, and what is expected to follow, is in `../common/README.md`.

### 2026-08-27 (a shared core, step two) — the first C++ out of the copies

This tree no longer has `src/platform/save_store.{h,cpp}`, `src/platform/text_entry.{h,cpp}` or a
`framework/types.h` of its own. They are compiled once from `recomps/common/` into `ipod_core`,
which `minigolf_common` links, and what is left at those paths is a forwarding header that pulls
the shared names into `minigolf::platform` with `using` declarations.

Nothing that calls them changed. `minigolf::platform::save_store()` still resolves and every
`#include "platform/save_store.h"` still finds a header at that path. `ctest` is 30 for 30.

The reasoning and the mechanism are in `../common/README.md`.

### 2026-08-27 (a shared core, step three) — this tree's rasteriser is gone, and that is the point

`src/libeapp/gles.{h,cpp}` no longer exist here. The iPod's GL ES driver was one piece of firmware
and both titles called the same one, so there is one reimplementation of it in
`recomps/common/src/ipod/libeapp/gles.cpp`, and it is Lost's — which had every function this one
had and twenty more, while this one had none of its own. `framework/graphics.h` and
`runtime/memory.h` went the same way, and `fatal` was split out of `runtime.{h,cpp}`.

**Two things this game does differently had to be told to it**, and neither was visible to any
oracle here: a call log records a buffer's address, never its contents, so the screen can be
entirely wrong without one logged word changing. Both were found by rendering these scripts
through both renderers and comparing pictures.

* This game **points its vertex attributes once and then draws many times**. Lost re-points them
  before every draw and its rasteriser reads that as which attributes a draw uses; under that
  rule every draw here after the first read as untextured. The enable flag is the default now and
  Lost opts out of it.
* This game **never writes the constant colour register** — it does not import the ordinal — and
  carries an untextured draw's colour in the vertex array. Lost paints flat draws from the
  register. Painting from an unset one is opaque white: the exit-confirmation screen, the score
  card and the page views all came out blank white with only the text on them.

Once both were right, 46 screenshots across five scripts differ from the old renderer by a mean of
**0.22** out of 255 (worst 0.44) — the alpha-weighted texture filtering and the sprite-sheet edge
clamping Lost had and this tree did not, both of which are corrections. `ctest` is 30 for 30.

**A harness note worth having.** The six `recomp_*` cases compare against `build-recomp/`, and
nothing in `ctest` rebuilds it. After a change to shared code it holds the old rasteriser and all
six fail for that reason and no other; `tests/check-recomp.sh` or a manual
`cmake --build build-recomp` refreshes it. That cost a confusing quarter of an hour today.

### 2026-08-27 (a shared core, step four) — this tree's decoder goes the other way

`src/platform/sdl3/music_decoder.{h,cpp}` moved to `recomps/common/src/ipod/platform/sdl3/` and
what is left here is a forwarding header. Lost was still spawning `afplay` for its music; it now
uses this decoder, and gained volume and a real stop with it.

Worth stating plainly, because the traffic went the other way an hour earlier: the rasteriser came
*from* Lost because Lost's was more complete, and the decoder went *to* it because this one's was.
Neither tree is the senior partner. Whichever knows more about an area is the one that moves.

The decoder is not in `ipod_core` — SDL is found after the shared directory is added, and the
headless oracle build has no SDL — so it is published as `IPOD_CORE_SDL3_SOURCES` and compiled
into each title's window build. `ctest` is 30 for 30 and the music still plays.

### 2026-08-27 (the pink screen) — a frame the game never drew

Reported against the Lost recomp and fixed for both, since the rasteriser is shared: the very
first frame draws nothing — the firmware is telling the game it is running — and the frame pump
presented the framebuffer's start-up magenta, which macOS then held through the window's
appearance animation. `gfx::anything_drawn()` now says whether a clear or a draw has happened and
the pump waits for it; the renderer is cleared to black once at start-up so that frame is defined.
A *screenshot* of frame 0 is still magenta, which is where that fill earns its place.

`ctest` is 30 for 30.
