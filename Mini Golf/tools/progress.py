#!/usr/bin/env python3
"""How much of the game is still recompiled rather than decompiled — the debt the port is paying off.

    python3 tools/progress.py

Counts functions and instructions from gen/funcs.json against src/game/replaced.txt, so "how much
is still emulation" is a number that can go in the progress log.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# The recompiler itself lives in the shared core, not in this tree: see
# ../../common/README.md. Everything title-specific stays here and is passed to it.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "common" / "tools"))

from recomp.functions import Kind, load_function_table  # noqa: E402

PROJECT = Path(__file__).resolve().parent.parent


def replaced_entries(path: Path) -> set[int]:
    if not path.exists():
        return set()
    entries = set()
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            entries.add(int(line.split()[0], 16))
    return entries


def check_not_emitted(replaced: set[int]) -> int:
    """A replaced function must not also exist in gen/: the linker would silently pick one copy."""
    generated = "".join(p.read_text() for p in (PROJECT / "gen").glob("*.cpp"))
    stale = [entry for entry in replaced if f"void f_{entry:08x}(Cpu& cpu) {{" in generated]
    for entry in stale:
        print(f"ERROR: {entry:#010x} is in replaced.txt but still defined in gen/ — re-run tools/emit.py")
    return len(stale)


def main() -> int:
    functions = load_function_table(PROJECT / "gen" / "funcs.json")
    replaced = replaced_entries(PROJECT / "src" / "game" / "replaced.txt")
    if check_not_emitted(replaced):
        return 1
    for kind, label in ((Kind.GAME, "game code"), (Kind.RUNTIME, "ARM C library")):
        pool = [f for f in functions if f.kind is kind]
        done = [f for f in pool if f.entry in replaced]
        total_insns = sum(f.instructions for f in pool) or 1
        done_insns = sum(f.instructions for f in done)
        print(
            f"{label:14} {len(done):3}/{len(pool):3} functions decompiled "
            f"({done_insns}/{total_insns} instructions, {100 * done_insns / total_insns:.1f}%)"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
