#!/usr/bin/env python3
"""Build `gen/funcs.json`, the function table the emitter works from.

The table is a *seed*, not an answer. `tools/emit.py` walks the control flow of every function
in it and adds any call target it meets, to a fixpoint (`recomp/generate.py`,
`Generator.discover_all`), so a seed of three addresses grows into the whole reachable program.
What the seed has to supply is the entry points that walking cannot find:

* the image's own **entry vectors** — the three addresses in the eApp header the firmware calls;
* every **live call target** from `play --callgraph-dump` (`analysis/coverage/edges-*.txt`),
  which is how functions reached only through a stored function pointer get in;
* `analysis/extra-entries.txt`, for the ones found by reading rather than by running;
* every **word in the image that reads as a pointer into the code** — the stored function
  pointers themselves, which is how a vtable slot or a dispatch table gets its target emitted
  before anything has run. A candidate is kept only if the control-flow walk succeeds on it,
  because a literal pool word can land in the code range by coincidence; three of Lost's do;
* every **code address an instruction forms from the program counter** — armcc's `adr`, an
  `add rN, pc, #imm` — which is how a function pointer to a routine in the same object is
  built without a literal at all. Vortex's C library hands its `printf` core a five-instruction
  string reader that way (`0x1800329c`, formed in the `sprintf` wrapper), and the pure
  recompilation died on the first string the game formatted after ENTER NAME, because nothing
  above could have found it. The same walk-and-keep filter applies.

The Mini Golf recomp seeded this from a Ghidra headless pass as well. This project does not, and
is better for it: the tool chain is self-contained, and the fixpoint found 736 functions from
the three vectors alone. Ghidra remains worth having for decompilation and struct recovery — it
is simply not a dependency of the build any more.

    python3 tools/funcs.py                      # defaults below
    python3 tools/funcs.py --image path/to.bin  # a different build of the title
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

# The recompiler itself lives in the shared core, not in this tree: see
# ../../common/README.md. Everything title-specific stays here and is passed to it.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "common" / "tools"))

from recomp.arm import LR, PC, Op, UnsupportedInstruction, decode
from recomp.cfg import ControlFlowError, discover
from recomp.functions import (
    Kind,
    build_function_table,
    load_call_targets,
    save_function_table,
)
from recomp.image import EAppImage

PROJECT = Path(__file__).resolve().parent.parent
DEFAULT_IMAGE = (
    PROJECT.parents[2] / "20 iPod games" / "Games_RO" / "99999" / "Executables" / "Cubis2_1_1_2563292.bin"
)
DEFAULT_EDGE_GLOB = "edges-*.txt"
DEFAULT_OUTPUT = PROJECT / "gen" / "funcs.json"
RUNTIME_BINDINGS = PROJECT / "src" / "runtime" / "arm_runtime.json"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--image", type=Path, default=DEFAULT_IMAGE, help="the eApp binary")
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


def load_runtime_entries(path: Path) -> set[int]:
    """The addresses `src/runtime/arm_runtime.json` gives a hand-written body — see functions.py."""
    if not path.exists():
        return set()
    return {int(k, 16) for k in json.loads(path.read_text()) if not k.startswith("_")}


def stored_function_pointers(image: EAppImage, known: set[int]) -> set[int]:
    """Words in the image that read as a pointer to code, and walk cleanly when treated as one.

    A function reached only through a stored pointer — a vtable slot, an entry in a dispatch
    table — is invisible to a walk of the control flow, and the recompilation is fatal on an
    indirect call to an address it has no function for. Where the pointer is *initialised data*
    it can be found by reading, which is what this does: take the range the known functions span
    as the code range, and treat every aligned word pointing into it as a candidate entry.

    The filter is the walk itself. A literal pool holds constants, and a constant can land in
    the code range by coincidence — three of Lost's do, and they decode as `ffffffff` and as
    ARMv5 doubleword transfers. Anything that does not walk is not an entry.
    """
    if not known:
        return set()
    low, high = min(known), max(known)
    candidates = set()
    for address in range(image.load_base, image.end - 3, 4):
        word = image.u32(address)
        if low <= word <= high and word % 4 == 0 and word not in known:
            candidates.add(word)
    # The walk has to know the import thunks as entries, or a candidate whose body tail-branches
    # into one (`b <thunk>`, armcc's way of returning a framework call's result unchanged) is
    # walked *into* the thunk's `ldr pc, [pc, #N]` and thrown away as unwalkable. The Sims Bowling's
    # volume slider handler at 0x18029d4c was lost exactly that way — a vtable slot the image
    # stores in plain sight — and surfaced as "indirect call to 0x18029d4c, which is not a
    # function entry" the first time the player touched the slider.
    walkable_from = known | set(image.thunks)
    entries = set()
    for candidate in sorted(candidates):
        try:
            discover(candidate, image, walkable_from)
        except (ControlFlowError, UnsupportedInstruction):
            continue
        entries.add(candidate)
    return entries


def pc_relative_code_addresses(image: EAppImage, known: set[int]) -> set[int]:
    """Code addresses formed by `add rN, pc, #imm` (armcc's `adr`), and that walk when treated as
    function entries.

    `add lr, pc, #imm` is left out: that is how a return address is prepared before a register
    jump (recomp/cfg.py, LINK_SETUP_WINDOW), and the address it forms is a continuation, not an
    entry. So is a result that is already a known entry. Everything else that lands in the code
    range is tried, and the walk is the filter, as for the stored pointers.
    """
    if not known:
        return set()
    low, high = min(known), max(known)
    candidates = set()
    for address in range(image.load_base, image.end - 3, 4):
        try:
            insn = decode(address, image.u32(address))
        except UnsupportedInstruction:
            continue
        if insn.op is not Op.DATA or insn.name not in ("add", "sub") or insn.rn != PC:
            continue
        if insn.rd in (PC, LR) or insn.operand2 is None:
            continue
        if insn.operand2.immediate is not None:
            # `add rD, pc, #imm` — the near form.
            immediate = insn.operand2.immediate
            formed = insn.pc_value + immediate if insn.name == "add" else insn.pc_value - immediate
        elif insn.name == "add" and insn.operand2.shift_imm in (None, 0) and insn.operand2.rm == insn.rd:
            # `ldr rD, [pc, #n]` then `add rD, pc, rD` — the far form: the literal holds the
            # target's distance from the `add`, not the target. armcc emits the pair a few
            # instructions apart, with nothing touching rD in between (Vortex's `sprintf`
            # wrapper at 0x18002188 is the first this project met).
            offset = _literal_loaded_into(image, address, insn.rd)
            if offset is None:
                continue
            formed = insn.pc_value + offset
        else:
            continue
        formed &= 0xFFFFFFFF
        if low <= formed <= high and formed % 4 == 0 and formed not in known:
            candidates.add(formed)
    entries = set()
    for candidate in sorted(candidates):
        try:
            discover(candidate, image, known)
        except (ControlFlowError, UnsupportedInstruction):
            continue
        entries.add(candidate)
    return entries


def _literal_loaded_into(image: EAppImage, before: int, register: int, window: int = 4) -> int | None:
    """The word an `ldr register, [pc, #n]` within `window` instructions before `before` loads, if
    that is where the register last came from."""
    for back in range(1, window + 1):
        address = before - 4 * back
        if address < image.load_base:
            return None
        try:
            insn = decode(address, image.u32(address))
        except UnsupportedInstruction:
            return None
        if insn.op is Op.LOAD_STORE and insn.load and insn.rd == register and insn.rn == PC:
            if insn.offset is None or insn.offset.immediate is None:
                return None
            literal = (
                insn.pc_value + insn.offset.immediate
                if insn.add_offset
                else insn.pc_value - insn.offset.immediate
            )
            return image.u32(literal) if image.load_base <= literal < image.end - 3 else None
        if _writes(insn, register):
            return None
    return None


def _writes(insn, register: int) -> bool:
    if insn.op in (Op.DATA, Op.MUL, Op.LOAD_STORE, Op.LOAD_STORE_HALF) and insn.rd == register:
        return insn.op is not Op.LOAD_STORE or insn.load
    if insn.op is Op.LOAD_STORE_MULTIPLE and insn.load and (insn.register_list >> register) & 1:
        return True
    return False


def main() -> int:
    args = parse_args()
    image = EAppImage.load(args.image)
    edge_files = sorted(args.edges.glob(DEFAULT_EDGE_GLOB)) if args.edges.is_dir() else []
    live_targets = load_call_targets(edge_files, image)
    live_targets |= load_extra_entries(PROJECT / "analysis" / "extra-entries.txt")
    runtime_entries = load_runtime_entries(RUNTIME_BINDINGS)

    # Two passes. The first establishes where the code is — the range the vectors and the live
    # call targets span — so the second can recognise a stored function pointer by the fact that
    # it points into it. Where the code ends and data begins is never needed exactly: the walk
    # follows control flow and cannot run off into a literal pool, so a function's `end` is a
    # hint for readers and the end of the image is an honest one.
    functions = build_function_table(image, [], live_targets, image.end, runtime_entries)
    code_entries = {f.entry for f in functions if f.kind is not Kind.THUNK}
    stored = stored_function_pointers(image, code_entries)
    formed = pc_relative_code_addresses(image, code_entries | stored)
    if stored or formed:
        functions = build_function_table(
            image, [], live_targets | stored | formed, image.end, runtime_entries
        )

    save_function_table(
        functions,
        args.output,
        f"{len(image.vectors)} entry vectors, {len(edge_files)} edge dumps, "
        f"{len(stored)} stored function pointers, {len(formed)} pc-relative code addresses, "
        f"image {args.image.name}",
    )

    report(functions, live_targets, stored, formed, edge_files)
    print(f"wrote {args.output.relative_to(PROJECT)}")
    return 0


def report(
    functions, live_targets: set[int], stored: set[int], formed: set[int], edge_files: list[Path]
) -> None:
    by_kind = Counter(f.kind for f in functions)
    print(
        f"seed: {len(functions)} entries — "
        f"{by_kind[Kind.GAME]} game, {by_kind[Kind.RUNTIME]} runtime, {by_kind[Kind.THUNK]} thunk"
    )
    print(f"stored function pointers found by reading the image: {len(stored)}")
    print(f"code addresses formed from the program counter (adr): {len(formed)}")
    if edge_files:
        print(f"live call targets from {len(edge_files)} edge dump(s): {len(live_targets)}")
    else:
        print(
            "no edge dumps under analysis/coverage/ — the seed is the vectors alone, so any "
            "function reached only through a stored pointer will be missing. Record one with "
            "`play --callgraph-dump=analysis/coverage/edges-NAME.txt`."
        )


if __name__ == "__main__":
    sys.exit(main())
