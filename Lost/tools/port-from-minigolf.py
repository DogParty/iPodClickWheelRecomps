#!/usr/bin/env python3
"""Copy the title-independent layers of the Mini Golf recomp into this tree, and record what
was copied.

PLAN.md § "What is inherited" explains why the layers are copied rather than shared: Mini Golf
is finished and green, and factoring its runtime and platform layers into a library shared by
two projects means re-verifying all of that on a day whose budget is spent elsewhere. The copy
is deliberate debt. This script is what keeps it from becoming *invisible* debt — it is the only
way files enter this tree from there, it is re-runnable, and it writes
`reference/PORTED.md` recording the source path and SHA-256 of every file at the moment it was
taken. Comparing the two copies later is then one command, and extracting a shared core is
mechanical for every file that has not diverged.

Names are rewritten as the files are copied: the C++ namespace and every identifier built on it
(`minigolf` -> `lost`), the CMake options and include guards (`MINIGOLF_` -> `LOST_`), the
target and program names, and the prose name of the game. A namespace called `minigolf` inside
this tree would be exactly the kind of thing the quality rules exist to prevent.

    python3 tools/port-from-minigolf.py                 # copy every file, write the manifest
    python3 tools/port-from-minigolf.py --only PATH...  # copy just these, keeping the rest
    python3 tools/port-from-minigolf.py --check         # report drift; copy nothing

`--check` is the command referred to above: it re-hashes the Mini Golf originals and this tree's
copies and says which files have diverged on either side since the port.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
SOURCE = PROJECT.parent / "Mini Golf"

# What is ported, and what deliberately is not.
#
# Ported: the emitter, the guest machine, the typed platform interfaces, the host library, the
# portable platform code and its two platforms, and the parts of the test harness that are about
# the format of a call log rather than about Mini Golf.
#
# Not ported, and why:
#   src/game/          Mini Golf's game. There is nothing to inherit; it is the work.
#   src/gamedata/manifest_data.cpp   a table of Mini Golf's own 169 files. `tools/manifest.py` is
#                      ported and writes this title's from its own folder; copying the other
#                      game's would be worse than having none.
#   src/platform/switch/   a second platform is not today's goal (PLAN.md "Not today").
#   tests/expected/    recordings of another game.
#   src/platform/wav.{h,cpp} and its test   a .wav decoder, for a console that is handed one PCM
#                      stream and has to mix for itself. Nothing here decodes a .wav: this game
#                      keeps its sound effects as raw PCM in its own banks and reads them itself.
#   src/runtime/arm_runtime.{h,cpp}   Mini Golf hand-wrote two ARM C-library routines the
#                      emitter could not translate. This title needs neither: the decoder now
#                      models the flag-field `mrs`/`msr` that was the reason for one of them,
#                      and the other's idiom does not appear here. `arm_runtime.json` is still
#                      ported — it is the file that would name such a routine, and the emitter
#                      reads it — and it is empty.
#   src/runtime/runtime.cpp, src/libeapp/heap.cpp, src/gamedata/zip.cpp   moved to the shared
#                        core on 2026-08-27 (recomps/common/src/ipod/...) together with their
#                        headers; the headers here are forwarding headers and stay in the list.
#                        Moved by the Texas Hold'em recomp's block 0b; see its PLAN.md.
#   tools/recomp/      the recompiler now lives in `recomps/common/tools/recomp` and is imported
#                      from there by both titles, so there is nothing left to copy. This is the
#                      first thing to leave this list, and the intention is that everything does:
#                      see ../../common/README.md for why copies were the wrong answer.
PORTED = [
    "tools/emit.py",
    "tools/funcs.py",
    "tools/progress.py",
    "tools/manifest.py",
    "tools/ppm2png.py",
    "src/gamedata/install.cpp",
    "src/gamedata/install.h",
    "src/gamedata/manifest.h",
    "src/gamedata/zip.h",
    "src/runtime/arm_runtime.json",
    "src/runtime/cpu.h",
    "src/runtime/eapp_image.cpp",
    "src/runtime/eapp_image.h",
    "src/runtime/main.cpp",
    "src/runtime/memory.cpp",
    "src/runtime/memory.h",
    "src/runtime/runtime.h",
    "src/framework/audio.h",
    "src/framework/controls.h",
    "src/framework/device.h",
    "src/framework/graphics.h",
    "src/framework/storage.h",
    "src/framework/types.h",
    "src/libeapp/include/ipod_eapp.h",
    "src/libeapp/arm_abi.cpp",
    "src/libeapp/async_file.cpp",
    "src/libeapp/audio.cpp",
    "src/libeapp/framework_call.cpp",
    "src/libeapp/gles.cpp",
    "src/libeapp/gles.h",
    "src/libeapp/heap.h",
    "src/libeapp/host_state.cpp",
    "src/libeapp/host_state.h",
    "src/libeapp/imports.json",
    "src/libeapp/input.cpp",
    "src/libeapp/misc.cpp",
    "src/platform/input_bindings.cpp",
    "src/platform/input_bindings.h",
    "src/platform/paths.cpp",
    "src/platform/paths.h",
    "src/platform/platform.h",
    "src/platform/save_store.cpp",
    "src/platform/save_store.h",
    "src/platform/settings.cpp",
    "src/platform/settings.h",
    "src/platform/text_entry.cpp",
    "src/platform/text_entry.h",
    "src/platform/null/null_platform.cpp",
    "src/platform/sdl3/sdl3_platform.cpp",
    "src/platform/sdl3/macos_settings.h",
    "src/platform/sdl3/macos_settings.mm",
    "src/platform/sdl3/macos_settings_stub.cpp",
    "tests/diff.py",
    "tests/diff.sh",
    "tests/unit/cpu_test.cpp",
    "tests/unit/input_bindings_test.cpp",
    "tests/unit/save_store_test.cpp",
    "tests/unit/settings_test.cpp",
    "tests/unit/text_entry_test.cpp",
    ".clang-format",
    ".gitignore",
    "CMakeLists.txt",
    "pyproject.toml",
]

# Applied in order, to text files only. The first three are the ones that matter; the rest keep
# prose and paths honest so a ported file does not talk about another game.
REWRITES = [
    (r"\bMINIGOLF_", "LOST_"),
    (r"\bminigolf_", "lost_"),  # CMake targets and C++ symbols; `_` is a word character, so
                                # the plain-word rule below does not reach them
    (r"\bMINIGOLF\b", "LOST"),
    (r"\bminigolf\b", "lost"),
    (r"\bMinigolf\b", "Lost"),
    (r"Mini Golf", "Lost"),
    (r"\bmini-golf\b", "lost"),
]

TEXT_SUFFIXES = {".py", ".cpp", ".h", ".mm", ".json", ".txt", ".sh", ".toml", ".md", ""}


@dataclass(frozen=True)
class Entry:
    path: str
    source_sha: str
    ported_sha: str


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rewrite(text: str) -> str:
    for pattern, replacement in REWRITES:
        text = re.sub(pattern, replacement, text)
    return text


def is_text(path: Path) -> bool:
    return path.suffix in TEXT_SUFFIXES


def port_one(relative: str) -> Entry:
    source = SOURCE / relative
    target = PROJECT / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    if is_text(source):
        target.write_text(rewrite(source.read_text()))
    else:
        target.write_bytes(source.read_bytes())
    target.chmod(source.stat().st_mode)
    return Entry(relative, sha256(source), sha256(target))


def write_manifest(entries: list[Entry]) -> Path:
    path = PROJECT / "reference" / "PORTED.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Ported from the Mini Golf recomp",
        "",
        "Written by `tools/port-from-minigolf.py`; do not edit by hand. Every file here entered",
        "this tree as a copy of the file named in the second column, with the name rewrites that",
        "script applies. `source` is the SHA-256 of the Mini Golf original at the moment of the",
        "port; `ported` is the SHA-256 of this tree's copy immediately after it — so a file whose",
        "current hash differs from `ported` has been edited here, and one whose Mini Golf",
        "original differs from `source` has moved on there. A row whose file is absent here was",
        "ported and then deliberately removed; the reason is in the script's `PORTED` list.",
        "",
        "`python3 tools/port-from-minigolf.py --check` reports both.",
        "",
        f"Source tree: `{SOURCE}`",
        "",
        "| file | source SHA-256 | ported SHA-256 |",
        "|---|---|---|",
    ]
    lines += [f"| `{e.path}` | `{e.source_sha[:16]}…` | `{e.ported_sha[:16]}…` |" for e in entries]
    lines += [
        "",
        "<!-- full hashes, for the --check command",
        *(f"{e.path} {e.source_sha} {e.ported_sha}" for e in entries),
        "-->",
        "",
    ]
    path.write_text("\n".join(lines))
    return path


def read_manifest(optional: bool = False) -> list[Entry]:
    path = PROJECT / "reference" / "PORTED.md"
    if not path.exists():
        if optional:
            return []
        sys.exit("no reference/PORTED.md — run without --check first")
    entries = []
    inside = False
    for line in path.read_text().splitlines():
        if line.startswith("<!-- full hashes"):
            inside = True
        elif line.startswith("-->"):
            inside = False
        elif inside and line.strip():
            relative, source_sha, ported_sha = line.split()
            entries.append(Entry(relative, source_sha, ported_sha))
    return entries


def check() -> int:
    drifted_source, drifted_here, missing = [], [], []
    for entry in read_manifest():
        source, target = SOURCE / entry.path, PROJECT / entry.path
        if not source.exists() or not target.exists():
            missing.append(entry.path)
            continue
        if sha256(source) != entry.source_sha:
            drifted_source.append(entry.path)
        if sha256(target) != entry.ported_sha:
            drifted_here.append(entry.path)
    for title, paths in (
        ("changed in Mini Golf since the port", drifted_source),
        ("changed here since the port", drifted_here),
        ("missing on one side", missing),
    ):
        print(f"{len(paths)} {title}")
        for path in paths:
            print(f"  {path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="report drift; copy nothing")
    parser.add_argument(
        "--only",
        nargs="+",
        metavar="PATH",
        help="port only these paths, leaving every other file in this tree untouched. Each must "
        "be in the PORTED list above; the manifest keeps its other rows.",
    )
    args = parser.parse_args()
    if args.check:
        return check()

    if not SOURCE.is_dir():
        sys.exit(f"source tree not found: {SOURCE}")

    wanted = PORTED
    if args.only:
        unknown = [path for path in args.only if path not in PORTED]
        if unknown:
            sys.exit("not in the PORTED list: " + ", ".join(unknown))
        wanted = args.only

    fresh = {entry.path: entry for entry in (port_one(relative) for relative in wanted)}
    # Rows for files this run did not touch keep the hashes they were recorded with, so `--check`
    # still reports the drift of everything else.
    kept = [entry for entry in read_manifest(optional=True) if entry.path not in fresh]
    entries = sorted(kept + list(fresh.values()), key=lambda e: PORTED.index(e.path)
                     if e.path in PORTED else len(PORTED))
    manifest = write_manifest(entries)
    print(f"ported {len(fresh)} file(s); wrote {manifest.relative_to(PROJECT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
