# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about The Sims Bowling
is answered by a file here. All of it was produced on 2026-08-27, before a line of this project
was written, with the tools the Hold'em recomp already had.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, vectors, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 ../HoldEm/tools/survey.py --image PATH/1500C/Executables/SimsBowling_1_1_3002478.bin` |
| `ordinals.txt` | every framework ordinal the boot and one-Select probes reached, with call counts and the name Mini Golf, Lost and Hold'em give it (or `unnamed`) | both call logs joined against `../*/src/libeapp/imports.json` |
| `extra-entries.txt` | function entries found by reading rather than running; empty so far | by hand, when `call_indirect` stops on an address the seed lacks |
| `scripts/*.script` | the scripts the probe runs were driven by | by hand |

The probe runs' raw outputs — branch-edge dumps, run summaries, screenshots — live in
`coverage/`, which is not part of this repository. Every copy of the game folder those runs
used had `savefile.dat` removed first (PLAN.md difference 6).

There is no Ghidra output here and the build does not need any: `tools/funcs.py` seeds the
function table from the entry vectors, live branch edges, and the stored function pointers it
finds by reading the image, and the emitter walks the rest (501 functions from the vectors
alone, 2 401 after the fixpoint, 0 unwalkable once the shared recompiler learned the two idioms
in PLAN.md's progress log). Ghidra remains the right tool for decompiling a function and
recovering a structure — and with 767 stored function pointers this title will lean on it more
than the others did — it is simply not a dependency.
