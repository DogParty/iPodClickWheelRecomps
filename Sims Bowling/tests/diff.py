#!/usr/bin/env python3
"""Compare two framework-call logs, exactly or on each call's real arguments.

    tests/diff.py expected.calls actual.calls            # semantic: real arguments only
    tests/diff.py --exact expected.calls actual.calls    # every logged register and stack word
    tests/diff.py --allow FILE expected.calls actual.calls   # ignoring the ordinals FILE names

Both logs are `FRAME Framework#ordinal r0 r1 r2 r3 sp0 sp1 sp2 sp3 from LR` lines, written by the
emulator's `play --call-log` and the recomp's `--call-log`.

Why two modes: the raw log also records leftovers — argument registers an ordinal does not use,
stack words that are a caller's saved registers, the return address the call came from. The pure
recompilation reproduces all of it and is checked with --exact. Hand-decompiled code is ordinary
C++ with its own registers, stack and call sites, so it is checked on what the call actually
means: the frame, the ordinal, and its real arguments (arity from src/libeapp/imports.json).
Nothing else is compared in semantic mode.

--allow names a file of `Framework#ordinal` lines (# starts a comment) whose calls are dropped
from both logs before they are compared. It exists for one situation: a recording that predates
a deliberate change, where the calls that differ are known, few, and named. Everything else in
the log is still compared, and the allowance file has to say why it is there.

Nothing passes --allow today. The one allowance there has ever been — recordings made before the
save store worked — went away when `--emulator-firmware` learned to stub the store the way the
emulator did, so all six recordings now compare with no exceptions. It stays here because the
situation recurs the moment a framework's behaviour changes ahead of the recordings.

Exit status 0 when the logs agree, 1 at the first difference (printed with both lines), 2 on a
usage or file error.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
IMPORTS = PROJECT / "src" / "libeapp" / "imports.json"
REGISTER_ARGUMENTS = 4  # r0-r3; the fifth argument onward is the first stack word


def load_arity() -> dict[str, int]:
    table = json.loads(IMPORTS.read_text())
    arity = {}
    for framework, entries in table.items():
        if framework.startswith("_"):
            continue
        for ordinal, entry in entries.items():
            arity[f"{framework}#{ordinal}"] = entry["args"] if isinstance(entry, dict) else REGISTER_ARGUMENTS
    return arity


STACK_LOW, STACK_HIGH = 0x117F0000, 0x11800000  # the guest stack (runtime/memory.h: top of RAM)


def normalise(value: str) -> str:
    """An argument as the comparison sees it: a pointer into the guest stack is reported as
    `stack`, because where a local lands depends on the stack frames between the caller and
    the program's entry, and the decompiled game keeps fewer of those than the original."""
    try:
        word = int(value, 16)
    except ValueError:
        return value
    return "stack" if STACK_LOW <= word < STACK_HIGH else value


def semantic_key(line: str, arity: dict[str, int]) -> str:
    """The parts of a log line that describe the call itself."""
    fields = line.split()
    frame, call, values = fields[0], fields[1], fields[2:10]
    count = arity.get(call, REGISTER_ARGUMENTS)  # unknown ordinal: the four register arguments
    return " ".join([frame, call, *map(normalise, values[:count])])


def load_allowance(path: Path) -> set[str]:
    """The `Framework#ordinal` calls to drop from both logs."""
    allowed = set()
    for line in path.read_text().splitlines():
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        allowed.add(text.split()[0])
    return allowed


def main(argv: list[str]) -> int:
    exact = "--exact" in argv
    allowance: set[str] = set()
    if "--allow" in argv:
        index = argv.index("--allow")
        try:
            allowance = load_allowance(Path(argv[index + 1]))
        except (OSError, IndexError) as error:
            print(f"diff.py: --allow: {error}", file=sys.stderr)
            return 2
        del argv[index : index + 2]
    paths = [a for a in argv[1:] if not a.startswith("--")]
    if len(paths) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        expected = Path(paths[0]).read_text().splitlines()
        actual = Path(paths[1]).read_text().splitlines()
    except OSError as error:
        print(f"diff.py: {error}", file=sys.stderr)
        return 2
    arity = {} if exact else load_arity()
    normalise = (lambda line: line) if exact else (lambda line: semantic_key(line, arity))
    if allowance:
        def wanted(line: str) -> bool:
            fields = line.split()
            return len(fields) < 2 or fields[1] not in allowance

        expected = [line for line in expected if wanted(line)]
        actual = [line for line in actual if wanted(line)]

    for index, (want, got) in enumerate(zip(expected, actual), start=1):
        if normalise(want) != normalise(got):
            print(
                f"diverges at call {index} of {len(expected)} ({'exact' if exact else 'semantic'} comparison)"
            )
            print(f"  expected: {want}")
            print(f"  actual:   {got}")
            return 1
    if len(expected) != len(actual):
        shorter = "actual" if len(actual) < len(expected) else "expected"
        print(f"{shorter} log ends early: {len(actual)} calls against {len(expected)}")
        return 1
    print(f"identical ({len(expected)} calls, {'exact' if exact else 'semantic'} comparison)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
