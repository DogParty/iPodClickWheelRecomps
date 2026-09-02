# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about The Sims Bowling
is answered by a file here. All of it was produced on 2026-08-27, before a line of this project
was written, with the tools the Hold'em recomp already had and the emulator at commit `96bfe90`.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, vectors, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 ../HoldEm/tools/survey.py --image …/1500C/Executables/SimsBowling_1_1_3002478.bin` |
| `coverage/edges-boot.txt` | every branch edge a 3 000-frame silent boot took, `site target count` per line | `play … --script=scripts/boot-probe.script --callgraph-dump=…` |
| `coverage/boot-summary.txt` | the emulator's own summary of that run: frames, quads, ordinals reached, the buttons going by the event list | stdout of the same run |
| `coverage/menu-probe-summary.txt` | the same for `scripts/menu-probe.script` (a screenshot every 500 frames to 4 500), with the emulator's texture, draw and file-op tails at each | stdout of that run |
| `coverage/menu-frame500.png` | what the emulator had on screen at frame 500 of that probe: the Main Menu | the emulator's `shot` action |
| `coverage/edges-controls-probe.txt` | every branch edge `scripts/controls-probe.script` took — two Selects at the menu; the seed's second edge dump | `play … --callgraph-dump=…` |
| `coverage/controls-probe-summary.txt` | that run's summary and diagnostic tails | stdout of that run |
| `coverage/controls-frame1400.png` | the emulator at frame 1400 of that probe: the Controls screen | the emulator's `shot` action |
| `ordinals.txt` | every framework ordinal the boot and one-Select probes reached, with call counts and the name Mini Golf, Lost and Hold'em give it (or `unnamed`) | both call logs joined against `../*/src/libeapp/imports.json` |
| `extra-entries.txt` | function entries found by reading rather than running; empty so far | by hand, when `call_indirect` stops on an address the seed lacks |
| `scripts/*.script` | the scripts those runs were driven by | by hand |

The emulator runs were made with `../HoldEm/build/oracle-emulator/release/play`, whose sources
are byte-identical to `tools/eapp-loader` at `96bfe90`, with `--load-on-open --allow-creates
--fixed-clock --fps=0` and the title defaults `play.rs` applies to a binary named `SimsBowling*`
(`--frame-reason=auto`, a reason seed of 5, asynchronous file completions, event-list buttons).
Every copy of the game folder those runs used had `savefile.dat` removed first (PLAN.md
difference 6). This project's own pin under `tools/oracle-emulator/` (reference/MANIFEST.md) was
taken from the same commit the same evening and is byte-identical, so nothing here needed
re-making; the recordings under `tests/expected/` are from the pin.

There is no Ghidra output here and the build does not need any: `tools/funcs.py` seeds the
function table from the entry vectors, the live edges above, and the stored function pointers it
finds by reading the image, and the emitter walks the rest (501 functions from the vectors
alone, 2 401 after the fixpoint, 0 unwalkable once the shared recompiler learned the two idioms
in PLAN.md's progress log). Ghidra remains the right tool for decompiling a function and
recovering a structure — and with 767 stored function pointers this title will lean on it more
than the others did — it is simply not a dependency.
