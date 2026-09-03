# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about Lost is answered
by a file here.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 tools/survey.py` |
| `extra-entries.txt` | function entries found by reading rather than by running — the ones no recorded session reached through a stored pointer | by hand; each line says why |

The raw outputs of instrumented play sessions — the whole-image disassembly and the branch-edge
dumps — live in `objdump.txt` and `coverage/`, which are not part of this repository.

There is no Ghidra output here and the build does not need any: `tools/funcs.py` seeds the
function table from the entry vectors, live branch edges, and the stored function pointers it
finds by reading the image, and the emitter walks the rest. Ghidra is still the right tool for
decompiling a function and recovering a structure — it is simply not a dependency.
