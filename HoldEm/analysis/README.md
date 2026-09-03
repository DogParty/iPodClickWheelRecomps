# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about Texas Hold'em is
answered by a file here. All of it was produced on 2026-08-27, before a line of this project was
written, with the tools the Lost recomp already had.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, vectors, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 ../Lost/tools/survey.py --image PATH/33333/Executables/HoldEm_1_1_2563291.bin` |
| `ordinals.txt` | every framework ordinal the probe runs reached, with call counts and the name Mini Golf and Lost give it (or `unnamed`) | the call logs joined against `../*/src/libeapp/imports.json` |
| `extra-entries.txt` | function entries found by reading rather than by running | by hand |
| `scripts/*.script` | the scripts the probe runs were driven by | by hand |

The probe runs' raw outputs — branch-edge dumps, run summaries, screenshots — live in
`coverage/`, which is not part of this repository. What they established is written into
`PLAN.md` where it is used.

There is no Ghidra output here and the build will not need any: `tools/funcs.py` seeds the
function table from the entry vectors, live branch edges, and the stored function pointers it
finds by reading the image, and the emitter walks the rest (872 functions from the vectors alone,
951 after the fixpoint, 0 unwalkable). Ghidra remains the right tool for decompiling a function and
recovering a structure — it is simply not a dependency.
