# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about Vortex is answered
by a file here. The first entries were produced on 2026-08-28, before a line of this project was
written, with the tools the Sims Bowling recomp already had; everything after them was made with
this project's own probe scripts.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, vectors, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 tools/survey.py > analysis/survey.txt` |
| `ordinals.txt` | every framework ordinal the boot and the level probes reached, with call counts and the name Mini Golf, Lost, Hold'em and Sims Bowling give it (or `unnamed`) | the two call logs joined against `../*/src/libeapp/imports.json` |
| `extra-entries.txt` | function entries found by reading rather than running; empty so far | by hand, when `call_indirect` stops on an address the seed lacks |
| `scripts/*.script` | the scripts the probe runs were driven by | by hand |

The probe runs' raw outputs — branch-edge dumps, run summaries, screenshots — live in
`coverage/`, which is not part of this repository. What the probes established is written into
`PLAN.md` where it is used: the wheel moves the ENTER NAME highlight one position per **eight
detents**, floored; DONE, BACKSPACE and the letters ring the dial; and Menu at the main menu is
the game asking to be suspended (162 framework calls a frame become 4, and it draws nothing
again). Every copy of the game folder those runs used had the game's own `options`, `stats` and
`en/stats` removed first (PLAN.md difference 3).

There is no Ghidra output here and the build does not need any: `tools/funcs.py` seeds the
function table from the entry vectors, live branch edges, and the stored function pointers it
finds by reading the image, and the emitter walks the rest (543 functions from the vectors alone,
702 after the fixpoint, 0 unwalkable). Ghidra remains the right tool for decompiling a function
and recovering a structure — it is simply not a dependency.
