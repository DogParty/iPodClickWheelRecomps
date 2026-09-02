# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about Vortex is answered
by a file here. The first entries were produced on 2026-08-28, before a line of this project was
written, with the tools the Sims Bowling recomp already had and the emulator at commit `96bfe90`;
everything after them was made with this project's own `tools/probe.sh`, which runs the pinned
emulator with the flags `tests/record.sh` records with and keeps what the run produced.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, vectors, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 tools/survey.py > analysis/survey.txt` |
| `coverage/edges-boot-probe.txt` | every branch edge a 3 000-frame silent boot took, `site target count` per line | `tools/probe.sh boot-probe` |
| `coverage/boot-probe-summary.txt`, `boot-probe-shot-*.png` | the emulator's own summary of that run — frames, quads, ordinals reached, the flags word it found, the file operations, the texture and draw tails at each screenshot — and its five screenshots, the title screen from frame 500 on | the same run |
| `coverage/screens-probe-shot-*.png`, `screens-probe-summary.txt`, `edges-screens-probe.txt` | what the emulator had on screen at frames 300, 600, 1 200, 2 400 and 3 600 of `scripts/screens-probe.script` — the title screen, its ball orbiting — with that run's summary and edges | `tools/probe.sh screens-probe` |
| `coverage/edges-select-probe.txt`, `select-probe-summary.txt`, `select-probe-shot-*.png` | every branch edge `scripts/select-probe.script` took — a Select at 3 000 and another at 3 600; the seed's second edge dump — with the run's summary and its six screenshots: ENTER NAME from frame 3 100, an `A` typed from 3 700. (The first run of this probe kept four shots that were The Sims Bowling's, from a concurrent session's run through the shared `/tmp` path — PLAN.md rule 11; these are from a clean re-run.) | `tools/probe.sh select-probe` |
| `coverage/wheel-probe-*` | `scripts/wheel-probe.script`: single `wheel ±N` bursts on ENTER NAME with a shot after each — +1, +1, +3, −5 left the highlight on A; +10 moved it to B | `tools/probe.sh wheel-probe` |
| `coverage/past-name-probe-*` | `scripts/past-name-probe.script`: an A typed, `wheel -10`, Select — which landed on BACKSPACE and erased it. With the wheel probe this gives **eight detents a position**, floored | `tools/probe.sh past-name-probe` |
| `coverage/new-game-probe-*` | `scripts/new-game-probe.script`: an A typed, `wheel -8` to DONE, Select — the **MAIN MENU** at 4 300 (an icon ring, NEW GAME); Select — the **first level** at 5 500 ("Infinite Loop", the brick tunnel, the ball in play); GAME OVER by 6 500 with nothing touching the wheel. Run with `--file-ops=400`, so its summary lists every file operation, including the eight reads that pull the `tex` pack apart | `tools/probe.sh new-game-probe --file-ops=400` |
| `coverage/menu-probe-*` | `scripts/menu-probe.script`: Menu pressed on the main menu at frame 4400 and again at 5000. The four screenshots are identical — the game stops working at the press (162 framework calls a frame become 4, and it draws nothing again), which is the suspend it is asking for | `tools/probe.sh menu-probe` |
| `ordinals.txt` | every framework ordinal the boot and the level probes reached, with call counts and the name Mini Golf, Lost, Hold'em and Sims Bowling give it (or `unnamed`) | the two call logs joined against `../*/src/libeapp/imports.json` |
| `extra-entries.txt` | function entries found by reading rather than running; empty so far | by hand, when `call_indirect` stops on an address the seed lacks |
| `scripts/*.script` | the scripts those runs were driven by | by hand |

The boot probe was first made with `../Sims Bowling/build/oracle-emulator/release/play`, whose
sources are byte-identical to `tools/eapp-loader` at `96bfe90` (checked with `diff -rq`), with
`--load-on-open --allow-creates --fixed-clock --fps=0` and the title defaults `play.rs` applies
to a binary named `vortex*` (a reason seed of 5 and no reason writes, asynchronous file
completions, the button-flags word found in the image, `allow_creates` forced). Every copy of
the game folder those runs used had the game's own `options`, `stats` and `en/stats` removed
first (PLAN.md difference 3). This project's own pin under `tools/oracle-emulator/`
(reference/MANIFEST.md) was taken from the same commit the same morning and is byte-identical,
so nothing here needed re-making; the recordings under `tests/expected/` are from the pin.

A first boot probe was discarded: the emulator's window took ten clicks as Selects while it ran
(its output listed them as `button Select`), and the run reached a different path with 407 631
calls. That is where rule 11 comes from, and why `tools/probe.sh` checks.

There is no Ghidra output here and the build does not need any: `tools/funcs.py` seeds the
function table from the entry vectors, the live edges above, and the stored function pointers it
finds by reading the image, and the emitter walks the rest (543 functions from the vectors alone,
702 after the fixpoint, 0 unwalkable). Ghidra remains the right tool for decompiling a function
and recovering a structure — it is simply not a dependency.
