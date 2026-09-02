# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about Cubis 2 is answered
by a file here. Everything was produced on 2026-08-28: the first entries with the tools the Vortex
recomp already had, and everything after them with this project's own `tools/probe.sh`, which runs
the pinned emulator with the flags `tests/record.sh` records with and keeps what the run produced.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, vectors, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 tools/survey.py > analysis/survey.txt` |
| `assets.txt` | every shipped image identified from its own header, and every texture the game uploads joined against those files | `python3 tools/assets.py --tex-log build/texlog.txt > analysis/assets.txt` (the log from `IPOD_TEX_LOG=1 build/cubis-headless … --script=tests/scripts/menu.script`) |
| `ordinals.txt` | every framework ordinal the boot and play probes reach, with call counts and the name the five finished trees give it (or `unnamed`) | the two probe call logs joined against `../*/src/libeapp/imports.json` |
| `coverage/edges-boot-probe.txt`, `boot-probe-summary.txt`, `boot-probe-shot-*.png` | every branch edge a 3 000-frame silent boot took, the emulator's own summary of that run, and its five screenshots — the main menu from frame 500 on | `tools/probe.sh boot-probe` |
| `coverage/new-game-probe-*` | `scripts/new-game-probe.script`: Select on NEW GAME, then Selects on what follows. This is what established that Select reaches **Name Entry**, that each further Select types a letter, and that the tick does nothing until a name has been typed | `tools/probe.sh new-game-probe` |
| `coverage/wheel-probe-*` | `scripts/wheel-probe.script`: single `wheel` bursts on Name Entry with a shot after each — `-20` moved the highlight two positions and `-40` four, from `A` to the tick. **Ten detents a position** | `tools/probe.sh wheel-probe` |
| `coverage/play-probe-*` | `scripts/play-probe.script`: a name typed, forty detents back to the tick, Select — the **New Game** options screen; Select — the **first level**, an isometric board of coloured cubes over the underwater backdrop; then GAME STATS when the clock runs it out | `tools/probe.sh play-probe` |
| `extra-entries.txt` | function entries found by reading rather than by running; empty so far | by hand, when `call_indirect` stops on an address the seed lacks |
| `scripts/*.script` | the scripts those runs were driven by | by hand |

Every copy of the game folder those runs used had the game's own `cubisgame.dat` and
`cubissave.dat` removed first (PLAN.md difference 3), which `tests/game-dir.sh` does for every run.

## The two things the probes could not have told us

**The emulator drew the cubes grey.** The call-log oracle passes a wrong picture by construction —
a draw hands the framework an address and a count, and what is at that address is never an
argument — so the first four probes above were all made against an emulator that rendered every
cube on the board grey, and nothing in them says so. `PLAN.md` difference 6 is the account; the
fix is in the emulator and in `../common`, verified against all twenty titles in `Games_RO/`
(`build/sweep/`), and the pin under `tools/oracle-emulator/` carries it.

**`assets.txt` is the other half of that lesson.** It exists because "the picture matches the
emulator" is not the same as "the picture is right": both renderers can be wrong together, and on
this title they were. It joins each upload to a shipped file through a reader written from
scratch, so an agreeing upload is three independent readings of the same bytes — the game's own
parser, the framework's decode, and this one.

There is no Ghidra output here and the build does not need any: `tools/funcs.py` seeds the
function table from the entry vectors, the live edges above, and the stored function pointers it
finds by reading the image, and the emitter walks the rest (349 functions from the three vectors
alone, 1 382 after the fixpoint, 0 unwalkable). Ghidra remains the right tool for decompiling a
function and recovering a structure — it is simply not a dependency.
