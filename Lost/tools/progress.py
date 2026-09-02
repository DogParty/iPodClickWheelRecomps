#!/usr/bin/env python3
"""How much of the game is still recompiled rather than decompiled — the debt the port is paying off.

    python3 tools/emit.py       # first: the numbers come from what it emitted
    python3 tools/progress.py

Reads `gen/src/emitted.json`, which `tools/emit.py` writes: every function the walk reached, its
kind, its instruction count, and whether `src/game/replaced.txt` replaced it. `gen/funcs.json`
cannot answer this — it is only the *seed* the walk starts from.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
EMITTED = PROJECT / "gen" / "src" / "emitted.json"
GENERATED = PROJECT / "gen" / "src"


def check_not_emitted(functions: list[dict]) -> int:
    """A replaced function must not also exist in gen/: the linker would silently pick one copy,
    and a green test suite would then be testing the code the decompilation was meant to retire."""
    generated = "".join(path.read_text() for path in GENERATED.glob("*.cpp"))
    stale = [f["entry"] for f in functions if f["replaced"] and f"void f_{int(f['entry'], 16):08x}(Cpu& cpu) {{" in generated]
    for entry in stale:
        print(f"ERROR: {entry} is in replaced.txt but still defined in gen/ — re-run tools/emit.py")
    return len(stale)


def main() -> int:
    if not EMITTED.exists():
        sys.exit(f"no {EMITTED.relative_to(PROJECT)} — run tools/emit.py first")
    functions = json.loads(EMITTED.read_text())["functions"]
    if check_not_emitted(functions):
        return 1
    for kind, label in (("game", "game code"), ("runtime", "ARM C library")):
        pool = [f for f in functions if f["kind"] == kind]
        done = [f for f in pool if f["replaced"]]
        total = sum(f["instructions"] for f in pool) or 1
        written = sum(f["instructions"] for f in done)
        print(
            f"{label:14} {len(done):4}/{len(pool):4} functions decompiled "
            f"({written}/{total} instructions, {100 * written / total:.1f}%)"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
