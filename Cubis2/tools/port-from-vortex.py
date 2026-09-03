#!/usr/bin/env python3
"""Copy the layers this title has not yet been able to take from the shared core out of the
Vortex recomp, and record what was copied.

PLAN.md § "What is inherited" explains the position: the recompiler and the parts of the runtime,
platform and framework layers that had already proved identical between titles are compiled from
`common/` and imported, never copied. What is copied is what still differs between titles
by a fact measured from a binary — the frame pump, the file and input models, the SDL window —
plus the tools and test harness that have not been made shared yet. This script is the only way a
file enters this tree from there; it is re-runnable; and it writes `reference/PORTED.md` with the
source path and SHA-256 of every file at the moment it was taken, so the drift between the copies
is one command to measure.

Vortex, the fifth title, is the source for every file: its tree is the newest copy of the same
layers — the forwarding headers into the shared core, the fixes made since, the widest tracing
aids, and the recording harness that checks a run for the contamination rule 11 describes. Where
Cubis 2's *behaviour* is another title's rather than Vortex's, that title's file is read and the
change is made by hand here, with its provenance in the comment.

**Identifiers are rewritten; prose is not.** The C++ namespace and everything built on it
(`vortex` -> `cubis`), the CMake options and include guards (`VORTEX_` -> `CUBIS_`), the target
and program names and the environment variables are rewritten as the files are copied, because a
namespace called `vortex` in this tree is exactly what the quality rules exist to prevent. A
comment that says "Vortex divides by its own frame delta" is left saying so: it is a true
statement about that game, and rewriting it to name this one would make it a false one. Each
ported file's prose is corrected by hand when the file is adopted for this title —
`grep -rn "Vortex\\|Sims Bowling\\|Bowling\\|Hold'em\\|\\bLost\\b" src tests tools` is the
worklist, and it should reach zero as the port is finished.

    python3 tools/port-from-vortex.py                 # copy every file, write the manifest
    python3 tools/port-from-vortex.py --only PATH...  # copy just these, keeping the rest
    python3 tools/port-from-vortex.py --check         # report drift; copy nothing

`--check` re-hashes the Vortex originals and this tree's copies and says which files have
diverged on either side since the port.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
SOURCE = PROJECT.parent / "Vortex"

# What is ported, and what deliberately is not.
#
# Ported: the tools that drive the shared recompiler, the parts of the guest runtime that still
# differ per title, the typed platform interfaces that have not moved to the shared core yet, the
# host library, the portable platform code and its two platforms, the installer, and the test
# harness.
#
# Not ported, and why:
#   src/game/            Vortex has none yet. Nothing to inherit; this is the work.
#   src/gamedata/manifest_data.cpp   a table of Vortex's own files. `tools/manifest.py` is
#                        ported and writes this title's from its own folder.
#   tests/expected/, tests/scripts/   recordings and scripts of another game.
#   tests/exact-allow.txt   the ordinals Vortex's exact comparison drops, for reasons that are
#                        that title's. Whether this title needs one is answered by its own exact
#                        comparison.
#   tools/probe.sh       ported: rule 11's clean-run checks are not one title's.
#   tools/port-from-bowling.py   this file's ancestor; this file replaces it.
#   tools/oracle-emulator/   pinned separately, from the emulator tree, by reference/MANIFEST.md.
#   Forwarding headers to the shared core (`src/framework/graphics.h`, `types.h`,
#   `src/platform/save_store.h`, `text_entry.h`, `sdl3/music_decoder.h`, `src/runtime/memory.h`,
#   `cpu.h`, `runtime.h`, `src/libeapp/heap.h`, `src/gamedata/zip.h`)
#                        *are* ported: they are the title's side of the shared core and are the
#                        same few lines in every tree. When a file moves to the core, its
#                        forwarding header joins this group and its implementation leaves the list.
PORTED = [
    "tools/survey.py",
    "tools/funcs.py",
    "tools/emit.py",
    "tools/progress.py",
    "tools/manifest.py",
    "tools/ppm2png.py",
    "tools/probe.sh",
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
    "src/framework/music_library.h",
    "src/framework/storage.h",
    "src/framework/types.h",
    "src/libeapp/include/ipod_eapp.h",
    "src/libeapp/arm_abi.cpp",
    "src/libeapp/async_file.cpp",
    "src/libeapp/audio.cpp",
    "src/libeapp/framework_call.cpp",
    "src/libeapp/heap.h",
    "src/libeapp/host_state.cpp",
    "src/libeapp/host_state.h",
    "src/libeapp/imports.json",
    "src/libeapp/input.cpp",
    "src/libeapp/metadata.cpp",
    "src/libeapp/misc.cpp",
    "src/platform/input_bindings.cpp",
    "src/platform/input_bindings.h",
    "src/platform/paths.cpp",
    "src/platform/paths.h",
    "src/platform/platform.h",
    "src/platform/save_store.h",
    "src/platform/settings.cpp",
    "src/platform/settings.h",
    "src/platform/text_entry.h",
    "src/platform/null/null_platform.cpp",
    "src/platform/sdl3/sdl3_platform.cpp",
    "src/platform/sdl3/music_decoder.h",
    "src/platform/sdl3/macos_settings.h",
    "src/platform/sdl3/macos_settings.mm",
    "src/platform/sdl3/macos_settings_stub.cpp",
    "tests/diff.py",
    "tests/diff.sh",
    "tests/frames.py",
    "tests/frames.sh",
    "tests/game-dir.sh",
    "tests/record.sh",
    "tests/unit/cpu_test.cpp",
    "tests/unit/input_bindings_test.cpp",
    "tests/unit/install_test.cpp",
    "tests/unit/render_scale_test.cpp",
    "tests/unit/save_files_test.cpp",
    "tests/unit/save_store_test.cpp",
    "tests/unit/settings_test.cpp",
    "tests/unit/text_entry_test.cpp",
    ".clang-format",
    ".gitignore",
    "CMakeLists.txt",
    "pyproject.toml",
]

# Applied in order, to text files only. Identifiers and the strings code uses as names; never the
# prose (see the module comment). `_` is a word character, so the plain-word rules do not reach
# `vortex_foo` and the two prefixed rules have to come first.
REWRITES = [
    (r"\bVORTEX_", "CUBIS_"),
    (r"\bvortex_", "cubis_"),  # CMake targets and C++ symbols
    (r"\bVORTEX\b", "CUBIS"),
    (r"\bvortex\b", "cubis"),  # the namespace, the program name, the Linux data directory
    (r"iPod Vortex", "iPod Cubis 2"),  # the per-user data directory (paths.cpp)
    (r"ipod-vortex", "ipod-cubis"),
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
        "# Ported from the Vortex recomp",
        "",
        "Written by `tools/port-from-vortex.py`; do not edit by hand. Every file here entered this",
        "tree as a copy of the file named in the first column, with the identifier rewrites that",
        "script applies. `source` is the SHA-256 of the Vortex original at the moment of the",
        "port; `ported` is the SHA-256 of this tree's copy immediately after it — so a file whose",
        "current hash differs from `ported` has been edited here, and one whose Vortex",
        "original differs from `source` has moved on there. A row whose file is absent here was",
        "ported and then deliberately removed; the reason is in the script's `PORTED` list.",
        "",
        "`python3 tools/port-from-vortex.py --check` reports both.",
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
        ("changed in Vortex since the port", drifted_source),
        ("changed here since the port", drifted_here),
        ("missing on one side", missing),
    ):
        print(f"{len(paths)} {title}")
        for path in paths:
            print(f"  {path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
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
    entries = sorted(
        kept + list(fresh.values()), key=lambda e: PORTED.index(e.path) if e.path in PORTED else len(PORTED)
    )
    manifest = write_manifest(entries)
    print(f"ported {len(fresh)} file(s); wrote {manifest.relative_to(PROJECT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
