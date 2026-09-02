#!/usr/bin/env python3
"""What is in the image: its header, its frameworks, and how much of it a walk can reach.

    python3 tools/survey.py > analysis/survey.txt

Every number `PLAN.md` states about this binary comes from here, so it is a tool rather than a
note: re-run it against a different build of the game and the numbers move with it. It reads
only the image — no recordings, no Ghidra — which is what makes it the honest starting point.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from funcs import DEFAULT_IMAGE  # noqa: E402

# The recompiler itself lives in the shared core, not in this tree: see
# ../../common/README.md. Everything title-specific stays here and is passed to it.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "common" / "tools"))

from recomp import cfg  # noqa: E402
from recomp.arm import UnsupportedInstruction  # noqa: E402
from recomp.image import EAppImage  # noqa: E402


def walk_from_vectors(image: EAppImage) -> tuple[dict, dict]:
    """Follow the control flow from every entry vector, adding call targets to a fixpoint.

    Returns (bodies reached, entries that could not be walked and why). A failure here is not a
    defect in the image: it is an instruction shape the decoder does not model, and naming it is
    the point of the exercise.
    """
    thunks = set(image.thunks)
    known = thunks | set(image.vectors)
    bodies: dict[int, cfg.FunctionBody] = {}
    failed: dict[int, str] = {}
    pending = list(image.vectors)
    while pending:
        entry = pending.pop()
        if entry in bodies or entry in failed:
            continue
        try:
            body = cfg.discover(entry, image, known | set(bodies))
        except (cfg.ControlFlowError, UnsupportedInstruction) as error:
            failed[entry] = str(error)
            continue
        bodies[entry] = body
        for target in body.call_targets:
            if target not in thunks and target not in bodies and target not in failed:
                pending.append(target)
                known.add(target)
    return bodies, failed


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    args = parser.parse_args()
    image = EAppImage.load(args.image)

    print(f"image        {args.image.name}")
    print(f"             {len(image.data)} bytes, loads at {image.load_base:#010x}, ends {image.end:#010x}")
    print("vectors      " + " ".join(f"{v:#010x}" for v in image.vectors))
    print()
    print("frameworks   (name, imported entries, the range their thunks occupy)")
    for framework in image.frameworks:
        first, last = framework.thunks[0].address, framework.thunks[-1].address
        print(f"  {framework.name:<12} {len(framework.thunks):4}  {first:#010x}..{last:#010x}")
    print(f"  {'total':<12} {len(image.thunks):4}")
    print()

    bodies, failed = walk_from_vectors(image)
    instructions = sum(len(b.instructions) for b in bodies.values())
    print("reachable by walking the control flow from the entry vectors:")
    print(f"  functions    {len(bodies)}")
    print(f"  instructions {instructions}")
    print(f"  unwalkable   {len(failed)}")
    for reason, count in Counter(f.split("—")[-1].strip() for f in failed.values()).most_common():
        print(f"    {count:4}  {reason}")
    for entry in sorted(failed):
        print(f"    {entry:#010x}  {failed[entry]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
