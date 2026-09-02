#!/usr/bin/env python3
"""Statically recompile the game: ARM image + function table -> C++ in `gen/src/`.

    python3 tools/funcs.py      # first, to build gen/funcs.json (the function table)
    python3 tools/emit.py       # then this

Every function is recompiled except the ones `src/game/replaced.txt` names — the addresses that
have been hand-decompiled — so the generated tree and `src/game/` fit together without
overlapping, and the generated side shrinks as the decompilation grows. `gen/` is generated and
is not committed; re-run these two tools after changing the emitter, the imports, or the list of
replaced functions.

Every function in the table is walked from its entry (`recomp/cfg.py`), translated instruction
by instruction (`recomp/cpp.py`), and written out with the declarations and bindings that make
the result build (`recomp/generate.py`). The tool fails loudly on any instruction or control-flow
shape it does not understand; it never guesses.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# The recompiler itself lives in the shared core, not in this tree: see
# ../../common/README.md. Everything title-specific stays here and is passed to it.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "common" / "tools"))

from recomp.functions import load_function_table
from recomp.generate import Bindings, GenerationError, Generator, Report
from recomp.image import EAppImage

PROJECT = Path(__file__).resolve().parent.parent
EMITTER_VERSION = "0.1"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument(
        "--image", type=Path, default=None, help="the eApp binary (default: the one funcs.py used)"
    )
    p.add_argument("--funcs", type=Path, default=PROJECT / "gen" / "funcs.json")
    p.add_argument("--out", type=Path, default=PROJECT / "gen" / "src")
    p.add_argument(
        "--replaced",
        type=Path,
        default=PROJECT / "src" / "game" / "replaced.txt",
        help="addresses of hand-decompiled functions to leave out, one per line",
    )
    p.add_argument("--imports", type=Path, default=PROJECT / "src" / "libeapp" / "imports.json")
    p.add_argument("--runtime-bindings", type=Path, default=PROJECT / "src" / "runtime" / "arm_runtime.json")
    return p.parse_args()


def load_replaced(path: Path) -> set[int]:
    """`replaced.txt`: one `0xADDRESS  name` per line; blank lines and `#` comments allowed."""
    if not path.exists():
        print(f"emit.py: no {path} — every function will be recompiled", file=sys.stderr)
        return set()
    entries = set()
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            entries.add(int(line.split()[0], 16))
    return entries


def default_image_path(funcs_path: Path) -> Path:
    # funcs.py records the image name it used; resolve it the same way funcs.py does.
    from funcs import DEFAULT_IMAGE  # local import keeps the two tools' defaults in one place

    return DEFAULT_IMAGE


def main() -> int:
    args = parse_args()
    args.out = args.out.resolve()
    image_path = args.image or default_image_path(args.funcs)
    image = EAppImage.load(image_path)
    functions = load_function_table(args.funcs)
    bindings = Bindings.load(args.imports, args.runtime_bindings)
    replaced = load_replaced(args.replaced)
    banner = f"emit.py {EMITTER_VERSION} · image {image_path.name} · {len(functions)} functions in {args.funcs.name}"

    generator = Generator(image, functions, bindings, replaced, banner, namespace="bowling")
    try:
        generator.discover_all()
    except GenerationError as error:
        print(f"emit.py: {error}", file=sys.stderr)
        return 1
    report = generator.write_all(args.out)
    print_report(report, args.out)
    return 0


def print_report(report: Report, out_dir: Path) -> None:
    shown = out_dir.relative_to(PROJECT) if out_dir.is_relative_to(PROJECT) else out_dir
    print(f"emitted {report.emitted} functions ({report.instructions} instructions) -> {shown}/")
    if report.replaced:
        print(f"left out {report.replaced} hand-decompiled functions (src/game/replaced.txt)")
    if report.discovered_entries:
        # The seed is the entry vectors and whatever live coverage there is (tools/funcs.py), so
        # nearly every function arrives this way. The count is the interesting part; the list is
        # in gen/funcs.json for anyone who wants it.
        found = sorted(report.discovered_entries)
        print(
            f"call targets discovered by walking, not in the seed: {len(found)} "
            f"({found[0]:#010x} … {found[-1]:#010x})"
        )
    print(
        f"framework thunks without an implementation: {len(report.unbound_thunks)} "
        f"(they log and return 0 — see src/libeapp/imports.json)"
    )
    print(
        f"ARM runtime: {report.runtime_recompiled} functions recompiled, "
        f"{report.runtime_bound} hand-written (src/runtime/arm_runtime.json)"
    )


if __name__ == "__main__":
    sys.exit(main())
