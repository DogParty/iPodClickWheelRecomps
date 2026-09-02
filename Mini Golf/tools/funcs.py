#!/usr/bin/env python3
"""Build `gen/funcs.json`, the function table the emitter works from.

Merges Ghidra's static function list with the entry points proven by live play sessions, then
classifies every function as game / runtime / thunk (see `recomp/functions.py`). Run it again
whenever `analysis/` changes; the output is generated and is not committed.

    python3 tools/funcs.py                      # defaults below
    python3 tools/funcs.py --image path/to.bin  # a different build of the title
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

# The recompiler itself lives in the shared core, not in this tree: see
# ../../common/README.md. Everything title-specific stays here and is passed to it.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "common" / "tools"))

from recomp.functions import (
    Kind,
    build_function_table,
    load_call_targets,
    load_ghidra_table,
    save_function_table,
)
from recomp.image import EAppImage

# Where this image stops being armcc's library and starts being the game.
#
# Established by reading every function below the main state machine: the library (heap, string,
# division helpers) never contains a `b .` assert trap and never calls upward into game code, and
# `0x180024bc` is the first function that does both. The assessment's earlier guess of
# `0x18002c28` put nine small game functions in the library bucket. armcc also links its
# soft-float library *after* the game code; that tail starts at `RUNTIME_TAIL_START` (`_fadd`: the
# `0x7f000000` exponent tests and the `mrs`/`msr` FPSCR emulation are unmistakable) and is runtime
# too.
#
# These are measured facts about *this* binary, which is why they live here rather than in the
# shared recompiler — it takes the answer as a set (see `main`), not the rule that produced it.
GAME_CODE_START = 0x1800_24BC
RUNTIME_TAIL_START = 0x1801_8B88

PROJECT = Path(__file__).resolve().parent.parent
DEFAULT_IMAGE = (
    PROJECT.parents[2] / "20 iPod games" / "Games_RO" / "88888" / "Executables" / "Minigolf_1_1_2563296.bin"
)
DEFAULT_GHIDRA_TABLE = PROJECT / "analysis" / "ghidra" / "functions.tsv"
DEFAULT_EDGE_GLOB = "edges-*.txt"
DEFAULT_OUTPUT = PROJECT / "gen" / "funcs.json"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--image", type=Path, default=DEFAULT_IMAGE, help="the eApp binary")
    p.add_argument(
        "--ghidra", type=Path, default=DEFAULT_GHIDRA_TABLE, help="functions.tsv from DumpFuncs.java"
    )
    p.add_argument(
        "--edges", type=Path, default=PROJECT / "analysis" / "coverage", help="directory of edge dumps"
    )
    p.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    return p.parse_args()


def load_extra_entries(path: Path) -> set[int]:
    """Function entries found by reading, not by running: addresses only ever reached through a
    stored function pointer that no recorded session followed. One `0xADDRESS  note` per line."""
    if not path.exists():
        return set()
    entries = set()
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            entries.add(int(line.split()[0], 16))
    return entries


def main() -> int:
    args = parse_args()
    image = EAppImage.load(args.image)
    ghidra = load_ghidra_table(args.ghidra)
    edge_files = sorted(args.edges.glob(DEFAULT_EDGE_GLOB))
    live_targets = load_call_targets(edge_files, image)
    live_targets |= load_extra_entries(PROJECT / "analysis" / "extra-entries.txt")

    # Code ends where Ghidra's last function ends; what follows is data and the BSS span.
    code_end = max(g.entry + g.size for g in ghidra)
    # Which entries are runtime rather than game. The shared table takes this as a *set*, because
    # the two titles decide it differently and each is right about its own image: Lost lists them
    # in `src/runtime/arm_runtime.json`, and this one has a contiguous range, because armcc put
    # its library either side of the game's code. The two constants are measured facts about this
    # binary — see the note above `GAME_CODE_START` — so they belong here and not in the shared
    # recompiler, which is why they moved when it moved.
    runtime_entries = {
        entry
        for entry in ({g.entry for g in ghidra} | live_targets)
        if entry < GAME_CODE_START or entry >= RUNTIME_TAIL_START
    }
    functions = build_function_table(image, ghidra, live_targets, code_end, runtime_entries)
    save_function_table(
        functions,
        args.output,
        f"{args.ghidra.name}, {len(edge_files)} edge dumps, image {args.image.name}",
    )

    report(functions, ghidra, live_targets)
    print(f"wrote {args.output.relative_to(PROJECT)}")
    return 0


def report(functions, ghidra, live_targets) -> None:
    """Print the counts the plan predicts (~304 game, 53 runtime, 277 thunks) so drift is visible."""
    by_kind = Counter(f.kind for f in functions)
    ghidra_entries = {g.entry for g in ghidra}
    new_from_play = [f for f in functions if f.kind is Kind.GAME and f.entry not in ghidra_entries]
    unreached = [f for f in functions if f.kind is Kind.GAME and not f.reached]
    overlapping = [f for f in functions if f.ghidra_size and f.entry + f.ghidra_size > f.end]

    print(
        f"functions: {len(functions)} total — "
        f"{by_kind[Kind.GAME]} game, {by_kind[Kind.RUNTIME]} runtime, {by_kind[Kind.THUNK]} thunk"
    )
    print(f"live call targets: {len(live_targets)}; game entries Ghidra missed: {len(new_from_play)}")
    for f in new_from_play:
        print(f"  new entry {f.entry:#010x} (inside Ghidra's previous function)")
    print(f"game functions never reached in play: {len(unreached)}")
    if overlapping:
        print(
            f"Ghidra bodies that extend past the next entry: {len(overlapping)} "
            "(non-contiguous functions or shared tails — the emitter follows control flow "
            "from each entry instead of trusting either extent)"
        )
        for f in overlapping[:10]:
            print(f"  {f.entry:#010x} ghidra_end={f.entry + f.ghidra_size:#010x} next_entry={f.end:#010x}")


if __name__ == "__main__":
    sys.exit(main())
