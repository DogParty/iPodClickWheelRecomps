"""The function table: which guest addresses are functions, what kind, and how far they extend.

Two sources are merged:

* Ghidra's static pass (`analysis/ghidra/functions.tsv`, written by `tools/DumpFuncs.java`):
  entry, size, instruction count, call counts.
* The emulator's live branch-edge dumps (`analysis/coverage/edges-*.txt`, one `site target count`
  per line, from `play --callgraph-dump`): every `bl` target and every indirect-call target that
  real play sessions reached is a function entry, whether or not Ghidra found it.

Each function is classified by where it sits in the image:

* `thunk`   — an import slot inside a framework descriptor (see `image.py`); bound by name.
* `runtime` — the ARM C/C++ library armcc linked in, between the descriptors and the first game
  function; replaced by hand-written bindings in `src/runtime/rt_arm.cpp`, never recompiled.
* `game`    — everything else: the code the emitter recompiles and the decomp replaces.

`game` and `runtime` are not separated by an address boundary in this image. armcc laid this title's
C library out *after* the game's own code and then interleaved the two: `0x1803ddc4` and
`0x1803e434` are plainly library routines, and so are the entry vectors at `0x1803d414`
onward, but the same address range holds ordinary game functions. Rather than invent a
boundary that is not there, a function is `runtime` exactly when something says it is —
`src/runtime/arm_runtime.json`, which is also the file that says what hand-written body
replaces it. Everything else is `game`. The classification has one consequence and only one:
which generated file a function lands in, and whether a hand-written binding may take its
place. Nothing about the translation depends on it.

(The Mini Golf recomp did use two boundary constants, because that image's library really was
two contiguous blocks. See `../Mini Golf/tools/recomp/functions.py` if that history matters.)
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from enum import Enum
from pathlib import Path
from typing import Iterable

from .image import EAppImage


# Instruction shapes that mark their branch target as a function entry (any condition code).
BRANCH_LINK_MASK, BRANCH_LINK_BITS = 0x0F00_0000, 0x0B00_0000  # bl <target>
MOV_PC_REG_MASK, MOV_PC_REG_BITS = 0x0FFF_FFF0, 0x01A0_F000  # mov pc, rN
BX_REG_MASK, BX_REG_BITS = 0x0FFF_FFF0, 0x012F_FF10  # bx rN
LR = 14


class Kind(str, Enum):
    GAME = "game"
    RUNTIME = "runtime"
    THUNK = "thunk"


@dataclass
class Function:
    entry: int
    end: int  # first address past the function: the next entry, or the image's code end
    name: str
    kind: Kind
    ghidra_size: int  # 0 when Ghidra did not know this function
    instructions: int  # Ghidra's count; 0 when unknown
    reached: bool  # seen as a call target in a live play session
    framework: str | None = None  # thunks only
    ordinal: int | None = None  # thunks only

    def to_json(self) -> dict:
        d = asdict(self)
        d["entry"], d["end"], d["kind"] = f"{self.entry:#010x}", f"{self.end:#010x}", self.kind.value
        return d

    @classmethod
    def from_json(cls, d: dict) -> "Function":
        return cls(
            entry=int(d["entry"], 16),
            end=int(d["end"], 16),
            name=d["name"],
            kind=Kind(d["kind"]),
            ghidra_size=d["ghidra_size"],
            instructions=d["instructions"],
            reached=d["reached"],
            framework=d.get("framework"),
            ordinal=d.get("ordinal"),
        )


@dataclass
class GhidraEntry:
    entry: int
    size: int
    instructions: int


def load_ghidra_table(path: Path) -> list[GhidraEntry]:
    """Parse `functions.tsv` (header line, then addr/size/name/callers/callees/insns/thunk)."""
    rows = []
    with path.open() as f:
        header = f.readline().rstrip("\n").split("\t")
        col = {name: i for i, name in enumerate(header)}
        for line in f:
            fields = line.rstrip("\n").split("\t")
            rows.append(
                GhidraEntry(
                    entry=int(fields[col["addr"]], 16),
                    size=int(fields[col["size"]]),
                    instructions=int(fields[col["insns"]]),
                )
            )
    return rows


def load_call_targets(edge_files: Iterable[Path], image: EAppImage) -> set[int]:
    """Function entries proven by execution: targets of `bl`, `mov pc, rN`, and `bx rN` (N != lr).

    Plain `b`/`bcc` edges are skipped — they are almost always loops and if/else inside one
    function. `mov pc, lr` and `bx lr` are returns, so their targets are return addresses, not
    entries. Sites and targets outside the image (the emulator's framework stubs) are ignored.
    """
    targets: set[int] = set()
    for path in edge_files:
        for line in path.read_text().splitlines():
            site_text, target_text, _count = line.split()
            site, target = int(site_text, 16), int(target_text, 16)
            if not (image.contains(site) and image.contains(target)):
                continue
            if _is_call_site(image.u32(site)):
                targets.add(target)
    return targets


def _is_call_site(instruction: int) -> bool:
    if instruction & BRANCH_LINK_MASK == BRANCH_LINK_BITS:
        return True
    is_register_jump = (
        instruction & MOV_PC_REG_MASK == MOV_PC_REG_BITS or instruction & BX_REG_MASK == BX_REG_BITS
    )
    return is_register_jump and (instruction & 0xF) != LR


def classify(entry: int, thunks: dict, runtime_entries: set[int]) -> Kind:
    """Which bucket a function entry belongs to. See the note at the top of this file."""
    if entry in thunks:
        return Kind.THUNK
    return Kind.RUNTIME if entry in runtime_entries else Kind.GAME


def build_function_table(
    image: EAppImage,
    ghidra: list[GhidraEntry],
    live_targets: set[int],
    code_end: int,
    runtime_entries: set[int],
) -> list[Function]:
    """Merge the static and live views into one sorted table with contiguous extents."""
    by_entry: dict[int, GhidraEntry] = {g.entry: g for g in ghidra}
    thunks = image.thunks
    entries = sorted(set(by_entry) | live_targets | set(thunks) | set(image.vectors))

    functions = []
    for i, entry in enumerate(entries):
        following = entries[i + 1] if i + 1 < len(entries) else code_end
        known = by_entry.get(entry)
        thunk = thunks.get(entry)
        kind = classify(entry, thunks, runtime_entries)
        functions.append(
            Function(
                entry=entry,
                end=max(following, entry + 4),
                name=_default_name(entry, thunk),
                kind=kind,
                ghidra_size=known.size if known else 0,
                instructions=known.instructions if known else 0,
                reached=entry in live_targets or entry in image.vectors,
                framework=thunk.framework if thunk else None,
                ordinal=thunk.ordinal if thunk else None,
            )
        )
    return functions


def _default_name(entry: int, thunk) -> str:
    if thunk:
        return f"{thunk.framework}_{thunk.ordinal}"
    return f"f_{entry:08x}"


def save_function_table(functions: list[Function], path: Path, source_note: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    document = {
        "generated_by": "tools/funcs.py — do not edit; re-run the tool",
        "sources": source_note,
        "functions": [f.to_json() for f in functions],
    }
    path.write_text(json.dumps(document, indent=1) + "\n")


def load_function_table(path: Path) -> list[Function]:
    return [Function.from_json(d) for d in json.loads(path.read_text())["functions"]]
