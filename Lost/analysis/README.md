# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about Lost is answered
by a file here.

| file | what it is | how it was made |
|---|---|---|
| `objdump.txt` | the whole image disassembled, `0x18000000`-based | `arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x18000000` over the game's `.bin` |
| `survey.txt` | the image's header, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 tools/survey.py` |
| `coverage/edges-*.txt` | every branch edge a real play session took, `site target count` per line | `play --callgraph-dump=…` (see `tests/record.sh` for the flags a run is made with) |
| `extra-entries.txt` | function entries found by reading rather than by running — the ones no recorded session reached through a stored pointer | by hand; each line says why |

There is no Ghidra output here and the build does not need any: `tools/funcs.py` seeds the
function table from the entry vectors, the live edges above, and the stored function pointers it
finds by reading the image, and the emitter walks the rest. Ghidra is still the right tool for
decompiling a function and recovering a structure — it is simply not a dependency.
