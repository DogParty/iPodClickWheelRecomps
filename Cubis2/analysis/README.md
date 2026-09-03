# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about Cubis 2 is answered
by a file here. Everything was produced on 2026-08-28: the first entries with the tools the Vortex
recomp already had, and everything after them with this project's own probe scripts.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, vectors, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 tools/survey.py > analysis/survey.txt` |
| `assets.txt` | every shipped image identified from its own header, and every texture the game uploads joined against those files | `python3 tools/assets.py --tex-log build/texlog.txt > analysis/assets.txt` (the log from an `IPOD_TEX_LOG=1` scripted menu run) |
| `ordinals.txt` | every framework ordinal the boot and play probes reach, with call counts and the name the five finished trees give it (or `unnamed`) | the two probe call logs joined against `../*/src/libeapp/imports.json` |
| `extra-entries.txt` | function entries found by reading rather than by running; empty so far | by hand, when `call_indirect` stops on an address the seed lacks |
| `scripts/*.script` | the scripts the probe runs were driven by | by hand |

The probe runs' raw outputs — branch-edge dumps, run summaries, screenshots — live in
`coverage/`, which is not part of this repository. What the probes established is written into
`PLAN.md` where it is used: Select reaches **Name Entry**, each further Select types a letter,
the tick does nothing until a name has been typed, and the wheel moves the highlight one
position per **ten detents**. Every copy of the game folder those runs used had the game's own
`cubisgame.dat` and `cubissave.dat` removed first (PLAN.md difference 3).

## The two things the probes could not have told us

**The cubes drew grey at first.** A call log passes a wrong picture by construction — a draw
hands the framework an address and a count, and what is at that address is never an argument —
so the first probes above were made against a renderer that drew every cube on the board grey,
and nothing in them says so. `PLAN.md` difference 6 is the account; the fix is in `../common`.

**`assets.txt` is the other half of that lesson.** It exists because two renderers agreeing is
not the same as the picture being right: both can be wrong together, and on this title they
were. It joins each upload to a shipped file through a reader written from scratch, so an
agreeing upload is three independent readings of the same bytes — the game's own parser, the
framework's decode, and this one.

There is no Ghidra output here and the build does not need any: `tools/funcs.py` seeds the
function table from the entry vectors, live branch edges, and the stored function pointers it
finds by reading the image, and the emitter walks the rest (349 functions from the three vectors
alone, 1 382 after the fixpoint, 0 unwalkable). Ghidra remains the right tool for decompiling a
function and recovering a structure — it is simply not a dependency.
