#!/usr/bin/env python3
"""Compare two runs' screenshots pixel by pixel.

    python3 tests/frames.py EMULATOR_DIR RECOMP_DIR [--max-differing=PERCENT] [--max-delta=N]

`EMULATOR_DIR` holds `ipod-shot-NN.png` from the pinned emulator, `RECOMP_DIR` holds
`shot-NN.ppm` from the recomp; the pairs are matched by number. Exit status is 0 when every pair
agrees within the thresholds, 1 otherwise, and the report names the worst frame either way.

**Why a threshold rather than a hash.** The two rasterisers are the same algorithm written twice,
in Rust and in C++, and their rounding does not always land on the same byte: about 0.3% of a
typical frame differs by exactly 1. A hash comparison would fail on that and tell you nothing.
What it must catch is a *wrong* pixel — a texture decoded through the wrong palette, a quad in
the wrong place, a colour key not dropped — and those are neither small nor few. The defaults
below sit in the gap: 1% of pixels differing by more than 8 is far above the rounding noise and
far below anything a real fault produces (the paletted-format bug this test was written for was
19% of the frame at a delta of 110).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

from ppm2png import read_png_rgb, read_ppm  # noqa: E402

DEFAULT_MAX_DIFFERING_PERCENT = 1.0
DEFAULT_MAX_DELTA = 8


def compare(expected: Path, actual: Path, max_delta: int) -> tuple[float, int]:
    """Returns (percentage of pixels differing by more than max_delta, the worst delta seen)."""
    width, height, left = read_png_rgb(expected)
    actual_width, actual_height, right = read_ppm(actual)
    if (width, height) != (actual_width, actual_height):
        sys.exit(f"frames.py: {expected.name} is {width}x{height}, {actual.name} is "
                 f"{actual_width}x{actual_height}")
    over = 0
    worst = 0
    for i in range(0, len(left), 3):
        delta = max(abs(left[i + k] - right[i + k]) for k in range(3))
        if delta > worst:
            worst = delta
        if delta > max_delta:
            over += 1
    return 100.0 * over / (width * height), worst


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("emulator_dir", type=Path)
    parser.add_argument("recomp_dir", type=Path)
    parser.add_argument("--max-differing", type=float, default=DEFAULT_MAX_DIFFERING_PERCENT,
                        help="percentage of pixels allowed to differ by more than --max-delta")
    parser.add_argument("--max-delta", type=int, default=DEFAULT_MAX_DELTA,
                        help="per-channel difference that counts as the same pixel")
    args = parser.parse_args()

    expected = sorted(args.emulator_dir.glob("ipod-shot-*.png"))
    actual = sorted(args.recomp_dir.glob("shot-*.ppm"))
    if not expected or len(expected) != len(actual):
        sys.exit(f"frames.py: {len(expected)} emulator frame(s) against {len(actual)} recomp "
                 "frame(s) — the two runs did not take the same screenshots")

    failures = 0
    worst_frame = (0.0, 0, "")
    for left, right in zip(expected, actual):
        percent, delta = compare(left, right, args.max_delta)
        if percent > worst_frame[0]:
            worst_frame = (percent, delta, right.name)
        if percent > args.max_differing:
            failures += 1
            print(f"  {right.name}: {percent:.2f}% of pixels differ by more than "
                  f"{args.max_delta} (worst {delta})")
    if failures:
        print(f"{failures} of {len(actual)} frames differ from the emulator's")
        return 1
    print(f"{len(actual)} frames agree with the emulator "
          f"(worst: {worst_frame[2]} at {worst_frame[0]:.2f}% over {args.max_delta}, "
          f"largest single-channel difference {worst_frame[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
