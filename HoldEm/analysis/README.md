# What is known about this binary, and how

Evidence, not conclusions. Everything in `PLAN.md` that states a number about Texas Hold'em is
answered by a file here. All of it was produced on 2026-08-27, before a line of this project was
written, with the tools the Lost recomp already had and the emulator at commit `96bfe90`.

| file | what it is | how it was made |
|---|---|---|
| `survey.txt` | the image's header, vectors, frameworks, and what a control-flow walk from the entry vectors reaches | `python3 ../Lost/tools/survey.py --image …/33333/Executables/HoldEm_1_1_2563291.bin` |
| `coverage/edges-boot.txt` | every branch edge a 600-frame silent boot took, `site target count` per line | `play … --script=scripts/boot.script --callgraph-dump=…` |
| `coverage/boot-summary.txt` | the emulator's own summary of that run: frames, quads, ordinals reached, where the button flags word is | stdout of the same run |
| `coverage/name-entry-probe-summary.txt` | the same for `scripts/name-entry-probe.script` (five Select presses, a screenshot at frame 1300) | stdout of that run |
| `coverage/name-entry-probe-frame1300.png` | what the emulator had on screen at frame 1300 of that probe: the ENTER NAME screen | the emulator's `shot` action |
| `ordinals.txt` | every framework ordinal either run reached, with its call count and the name Mini Golf and Lost give it (or `unnamed`) | both call logs joined against `../*/src/libeapp/imports.json` |
| `coverage/first-hand-frame3500.png` | the emulator at frame 3500 of `scripts/hand-probe.script`: the first hand dealt | the emulator's `shot` action |
| `coverage/hand-probe-summary.txt` | that run's summary line | stdout of that run |
| `coverage/edges-first-hand.txt` | every branch edge the recorded `tests/scripts/first-hand.script` took — the seed's second edge dump | `play … --callgraph-dump=…` |
| `scripts/*.script` | the scripts those runs were driven by | by hand |

The emulator runs were made with `../Lost/build/oracle-emulator/release/play`, whose sources are
byte-identical to `tools/eapp-loader` at `96bfe90` (`diff -q` on `play.rs` and `lib.rs`), with
`--allow-creates --fixed-clock --fps=0` and the title defaults `play.rs` applies to a binary named
`HoldEm*` (`--load-on-open`, `--frame-reason=1`, `--ctx-seed=0`, asynchronous file completions).
This project's own pin under `tools/oracle-emulator/` (reference/MANIFEST.md) was taken from the
same commit the same evening and is byte-identical, so nothing here needed re-making; the
recordings under `tests/expected/` are from the pin.

There is no Ghidra output here and the build will not need any: `tools/funcs.py` seeds the
function table from the entry vectors, the live edges above, and the stored function pointers it
finds by reading the image, and the emitter walks the rest (872 functions from the vectors alone,
951 after the fixpoint, 0 unwalkable). Ghidra remains the right tool for decompiling a function and
recovering a structure — it is simply not a dependency.
