"""Assemble the generated source tree: discover every function, emit it, write the files.

Output (all under the --out directory, all overwritten on every run):

* `game_NN.cpp`   the recompiled game functions, ~24 per file in address order
* `runtime_NN.cpp` the recompiled ARM C-library functions (memcpy, division, ...). They are
                  emitted exactly like game code; `src/runtime/arm_runtime.json` names the few
                  that are hand-written instead (anything the decoder cannot handle)
* `funcs.h`       declarations of every recompiled function plus `call_indirect`
* `calltable.cpp` `call_indirect`: a switch from guest address to C++ function, for calls
                  through registers (vtables, callbacks) and for the frame pump's entry vectors
* `bindings.h`    declarations of every framework thunk (`eapp_OpenGLES_37`) and hand-written
                  runtime entry (`rt_18018b88`)
* `bindings.cpp`  each binding forwards to its hand-written implementation when one is named in
                  `src/libeapp/imports.json` / `src/runtime/arm_runtime.json`, otherwise to the
                  "unimplemented" handlers that log and return — so the tree always links

Functions listed in `src/game/replaced.txt` are declared but not emitted: their bodies come
from the hand-written decompilation.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from .arm import UnsupportedInstruction
from .cfg import ControlFlowError, FunctionBody, discover
from .cpp import FunctionEmitter, function_symbol, hex_u32
from .functions import Function, Kind, classify
from .image import EAppImage

FUNCTIONS_PER_FILE = 24


class GenerationError(Exception):
    pass


@dataclass
class Bindings:
    """Hand-written implementations the generated bindings forward to, by framework ordinal / entry."""

    imports: dict[str, dict[str, str]] = field(default_factory=dict)  # framework -> ordinal -> symbol
    runtime: dict[int, str] = field(default_factory=dict)  # entry -> symbol

    @classmethod
    def load(cls, imports_path: Path, runtime_path: Path) -> "Bindings":
        bindings = cls()
        if imports_path.exists():
            bindings.imports = {
                k: v for k, v in json.loads(imports_path.read_text()).items() if not k.startswith("_")
            }
        if runtime_path.exists():
            bindings.runtime = {
                int(k, 16): v
                for k, v in json.loads(runtime_path.read_text()).items()
                if not k.startswith("_")
            }
        return bindings

    def framework_symbol(self, function: Function) -> str | None:
        entry = self.imports.get(function.framework, {}).get(str(function.ordinal))
        return entry["name"] if isinstance(entry, dict) else entry

    def runtime_symbol(self, function: Function) -> str | None:
        return self.runtime.get(function.entry)

    @property
    def runtime_entries(self) -> set[int]:
        """The addresses that make a function `runtime` rather than `game` — see functions.py."""
        return set(self.runtime)


@dataclass
class Report:
    emitted: int = 0
    replaced: int = 0
    instructions: int = 0
    discovered_entries: list[int] = field(default_factory=list)
    runtime_recompiled: int = 0
    runtime_bound: int = 0
    unbound_thunks: list[Function] = field(default_factory=list)


class Generator:
    def __init__(
        self,
        image: EAppImage,
        functions: list[Function],
        bindings: Bindings,
        replaced: set[int],
        banner: str,
        namespace: str,
    ) -> None:
        # The C++ namespace the recompiled bodies are emitted into, without the `::game` — one of
        # the two things this package cannot know about the title it is recompiling. The other is
        # which functions are runtime rather than game, which `functions.build_function_table`
        # takes as a set for the same reason.
        self.namespace = namespace
        self.image = image
        self.table: dict[int, Function] = {f.entry: f for f in functions}
        self.bindings = bindings
        self.replaced = replaced
        self.banner = banner
        self.bodies: dict[int, FunctionBody] = {}
        self.report = Report()

    # -- discovery -------------------------------------------------------------------------

    def is_recompiled(self, function: Function) -> bool:
        """Game code and runtime code are recompiled unless a hand-written body replaces them."""
        if function.kind is Kind.THUNK or function.entry in self.replaced:
            return False
        return not (function.kind is Kind.RUNTIME and self.bindings.runtime_symbol(function))

    def discover_all(self) -> None:
        """Walk every recompiled function; a call target not yet in the table becomes a function too."""
        pending = [f.entry for f in self.table.values() if self.is_recompiled(f)]
        while pending:
            entry = pending.pop()
            if entry in self.bodies:
                continue
            try:
                self.bodies[entry] = body = discover(entry, self.image, set(self.table))
            except (ControlFlowError, UnsupportedInstruction) as error:
                raise GenerationError(
                    f"function {entry:#010x} cannot be recompiled: {error}\n"
                    "  (hand-write it and list it in src/runtime/arm_runtime.json or src/game/replaced.txt)"
                ) from error
            for target in body.call_targets:
                if target not in self.table:
                    self._add_discovered(target)
                if self.is_recompiled(self.table[target]) and target not in self.bodies:
                    pending.append(target)

    def _add_discovered(self, entry: int) -> None:
        kind = classify(entry, self.image.thunks, self.bindings.runtime_entries)
        self.table[entry] = Function(entry, entry + 4, f"f_{entry:08x}", kind, 0, 0, False)
        self.report.discovered_entries.append(entry)

    # -- output ----------------------------------------------------------------------------

    def write_all(self, out_dir: Path) -> Report:
        out_dir.mkdir(parents=True, exist_ok=True)
        for stale in list(out_dir.glob("game_*.cpp")) + list(out_dir.glob("runtime_*.cpp")):
            stale.unlink()
        self._write_function_files(out_dir, Kind.GAME, "game")
        self._write_function_files(out_dir, Kind.RUNTIME, "runtime")
        (out_dir / "funcs.h").write_text(self._funcs_header())
        (out_dir / "calltable.cpp").write_text(self._calltable())
        (out_dir / "bindings.h").write_text(self._bindings_header())
        (out_dir / "bindings.cpp").write_text(self._bindings_source())
        (out_dir / "emitted.json").write_text(self._emitted_report())
        return self.report

    def _emitted_report(self) -> str:
        """What the run found, for `tools/progress.py`.

        `gen/funcs.json` is only the *seed* — three entry vectors and whatever live coverage
        there was — so it cannot answer "how much of the game is still recompiled". This can:
        every function the walk reached, its kind, and how many instructions it holds. Functions
        `src/game/replaced.txt` names are walked for their size and then not emitted, which is
        what makes the two numbers comparable.
        """
        rows = []
        for entry, function in sorted(self.table.items()):
            if function.kind is Kind.THUNK or not self.is_recompiled(function):
                if entry not in self.replaced:
                    continue
            rows.append(
                {
                    "entry": f"{entry:#010x}",
                    "kind": function.kind.value,
                    "instructions": self._instruction_count(entry),
                    "replaced": entry in self.replaced,
                }
            )
        return json.dumps(
            {
                "generated_by": "tools/emit.py — do not edit; re-run the tool",
                "banner": self.banner,
                "functions": rows,
            },
            indent=1,
        ) + "\n"

    def _instruction_count(self, entry: int) -> int:
        """How many instructions a function holds, walking it now if it was not emitted."""
        body = self.bodies.get(entry)
        if body is None:
            try:
                body = discover(entry, self.image, set(self.table))
            except (ControlFlowError, UnsupportedInstruction):
                return 0
        return len(body.instructions)

    def _functions(self, kind: Kind) -> list[Function]:
        return sorted((f for f in self.table.values() if f.kind is kind), key=lambda f: f.entry)

    def _bound_functions(self) -> list[Function]:
        """Thunks and hand-written runtime entries: everything declared in bindings.h."""
        return self._functions(Kind.THUNK) + [
            f for f in self._functions(Kind.RUNTIME) if not self.is_recompiled(f)
        ]

    def _write_function_files(self, out_dir: Path, kind: Kind, stem: str) -> None:
        to_emit = [f for f in self._functions(kind) if self.is_recompiled(f)]
        if kind is Kind.GAME:
            self.report.replaced = sum(1 for f in self._functions(kind) if f.entry in self.replaced)
        else:
            self.report.runtime_recompiled = len(to_emit)
            self.report.runtime_bound = len(self._functions(kind)) - len(to_emit)
        for index in range(0, len(to_emit), FUNCTIONS_PER_FILE):
            chunk = to_emit[index : index + FUNCTIONS_PER_FILE]
            parts = [
                self._file_header(f"{stem} functions {chunk[0].entry:#010x} – {chunk[-1].entry:#010x}"),
                '#include "bindings.h"',
                '#include "funcs.h"',
                '#include "runtime/cpu.h"',
                '#include "runtime/memory.h"',
                '#include "runtime/runtime.h"',
                "",
                f"namespace {self.namespace}::game {{",
                "",
            ]
            for function in chunk:
                body = self.bodies[function.entry]
                parts.append(FunctionEmitter(body, function, self.image, self.table).emit())
                self.report.emitted += 1
                self.report.instructions += len(body.instructions)
            parts.append(f"}}  // namespace {self.namespace}::game")
            (out_dir / f"{stem}_{index // FUNCTIONS_PER_FILE:02d}.cpp").write_text("\n".join(parts) + "\n")

    def _file_header(self, what: str) -> str:
        return (
            f"// GENERATED by tools/emit.py — do not edit; re-run the tool instead.\n"
            f"// {what}\n"
            f"// {self.banner}\n"
        )

    def _funcs_header(self) -> str:
        lines = [
            self._file_header("declarations of every recompiled (or hand-decompiled) function"),
            "#pragma once",
            "",
            '#include "runtime/cpu.h"',
            "",
            "#include <cstdint>",
            "",
            f"namespace {self.namespace}::game {{",
            "",
            "// Call through a guest address: used for `mov pc, rN` / `bx rN` sites and by the frame pump",
            "// to enter the vector-table functions. Fatal on an address that is not a function entry.",
            "void call_indirect(uint32_t target);",
            "",
        ]
        for function in self._functions(Kind.GAME) + self._functions(Kind.RUNTIME):
            if function.kind is Kind.RUNTIME and not self.is_recompiled(function):
                continue  # declared in bindings.h
            note = "  // hand-decompiled, see src/game/" if function.entry in self.replaced else ""
            lines.append(f"void {function_symbol(function)}(Cpu& cpu);{note}")
        lines += ["", f"}}  // namespace {self.namespace}::game", ""]
        return "\n".join(lines)

    def _calltable(self) -> str:
        lines = [
            self._file_header("call_indirect: guest address -> function"),
            '#include "bindings.h"',
            '#include "funcs.h"',
            '#include "runtime/cpu.h"',
            '#include "runtime/runtime.h"',
            "",
            f"namespace {self.namespace}::game {{",
            "",
            "void call_indirect(uint32_t target) {",
            "    Cpu& cpu = registers();",
            "    switch (target) {",
        ]
        for function in sorted(self.table.values(), key=lambda f: f.entry):
            lines.append(f"    case {hex_u32(function.entry)}: return {function_symbol(function)}(cpu);")
        lines += [
            # The prepared return address names the call site, which is the one thing a reader
            # needs and cannot otherwise get without a full entry trace.
            '    default:',
            '        fatal("indirect call to %#010x, which is not a function entry (from %#010x, "',
            '              "sp %#010x, r0 %#010x r1 %#010x r2 %#010x r3 %#010x r4 %#010x r5 %#010x)",',
            '              target, cpu.r[LR], cpu.r[SP], cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3],',
            '              cpu.r[4], cpu.r[5]);',
            "    }",
            "}",
            "",
            f"}}  // namespace {self.namespace}::game",
            "",
        ]
        return "\n".join(lines)

    def _bindings_header(self) -> str:
        lines = [
            self._file_header("framework thunks and hand-written ARM runtime entries"),
            "#pragma once",
            "",
            '#include "runtime/cpu.h"',
            "",
            f"namespace {self.namespace}::game {{",
            "",
        ]
        for function in self._bound_functions():
            what = (
                f"{function.framework} #{function.ordinal}"
                if function.kind is Kind.THUNK
                else "hand-written ARM runtime entry"
            )
            lines.append(f"void {function_symbol(function)}(Cpu& cpu);  // {what}")
        lines += ["", f"}}  // namespace {self.namespace}::game", ""]
        return "\n".join(lines)

    def _bindings_source(self) -> str:
        lines = [
            self._file_header(
                "bindings: forward each thunk / hand-written runtime entry to its implementation"
            ),
            '#include "bindings.h"',
            "",
            '#include "ipod_eapp.h"',
            *(
                # Only when a routine is actually hand-written. `src/runtime/arm_runtime.h` is
                # the header those live in, and a title that needs none does not have the file.
                ['#include "runtime/arm_runtime.h"'] if self.bindings.runtime else []
            ),
            "",
            f"namespace {self.namespace}::game {{",
            "",
        ]
        for function in self._bound_functions():
            symbol = function_symbol(function)
            if function.kind is Kind.THUNK:
                target = self.bindings.framework_symbol(function)
                if not target:
                    self.report.unbound_thunks.append(function)
                implementation = f"eapp::{target}" if target else "nullptr"
                lines.append(
                    f"void {symbol}(Cpu& cpu) {{\n"
                    f'    eapp::framework_call(cpu, "{function.framework}", {function.ordinal}, {implementation});\n'
                    f"}}"
                )
            else:
                target = self.bindings.runtime_symbol(function)
                lines.append(f"void {symbol}(Cpu& cpu) {{ runtime::{target}(cpu); }}")
        lines += ["", f"}}  // namespace {self.namespace}::game", ""]
        return "\n".join(lines)
