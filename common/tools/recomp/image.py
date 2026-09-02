"""The eApp executable image: header, vector table, framework descriptors, and byte access.

An iPod eApp (`Minigolf_1_1_2563296.bin`) is a flat image loaded at a fixed base address
(0x18000000 for every title seen), so file offset `o` is guest address `base + o`. The header
layout below was established in the emulator's loader (`tools/eapp-loader/src/lib.rs`,
`EApp::parse`) and is reproduced here so the recomp tools stand alone:

    +0x00  "eapp"
    +0x10  pointer to the primary framework descriptor (OpenGLES) — also fixes the load base
    +0x14  vector table: five absolute entry points (entry, frame hook, ...), zero = unused
    +0x28  code and data

A framework descriptor is a 0x20-byte name, a 16-byte MD5, a count at +0x30, a link pointer at
+0x34, and then `count` import thunks of the form `ldr pc, [pc, #imm]` starting at +0x38. The
primary descriptor hangs off +0x10 with no magic; the others are preceded by the four-byte
block magic. Thunk addresses are what game code `bl`s to, so they are how we recognise a call
into a framework.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

EAPP_MAGIC = b"eapp"
# Four bytes that precede every secondary descriptor; seen in both byte orders in the wild.
BLOCK_MAGIC = (b"\x68\x19\x06\x29", b"\x29\x06\x19\x68")

HEADER_PRIMARY_DESCRIPTOR = 0x10
HEADER_VECTORS = 0x14
HEADER_VECTOR_COUNT = 5

DESCRIPTOR_NAME_SIZE = 0x20
DESCRIPTOR_COUNT_OFFSET = 0x30
DESCRIPTOR_LINK_OFFSET = 0x34
DESCRIPTOR_THUNKS_OFFSET = 0x38

# `ldr pc, [pc, #imm]` with any condition: the only instruction a thunk slot may contain.
LDR_PC_PC_MASK = 0x0FFF_F000
LDR_PC_PC_BITS = 0x059F_F000

DEFAULT_LOAD_BASE = 0x1800_0000


@dataclass(frozen=True)
class Thunk:
    """One import slot: guest address of the `ldr pc` instruction and what it imports."""

    address: int
    framework: str
    ordinal: int


@dataclass(frozen=True)
class Framework:
    name: str
    thunks: tuple[Thunk, ...]


class EAppImage:
    """Read-only view of an eApp file addressed by guest address."""

    def __init__(self, data: bytes, load_base: int = DEFAULT_LOAD_BASE) -> None:
        if len(data) < HEADER_VECTORS + 4 * HEADER_VECTOR_COUNT or data[:4] != EAPP_MAGIC:
            raise ValueError("not an eApp image (bad magic or truncated header)")
        self.data = data
        self.load_base = load_base
        self.vectors = tuple(
            v
            for v in (self.u32(load_base + HEADER_VECTORS + 4 * i) for i in range(HEADER_VECTOR_COUNT))
            if v != 0
        )
        self.frameworks = tuple(self._parse_frameworks())

    @classmethod
    def load(cls, path: Path, load_base: int = DEFAULT_LOAD_BASE) -> "EAppImage":
        return cls(path.read_bytes(), load_base)

    # -- addressing ------------------------------------------------------------------------

    @property
    def end(self) -> int:
        """First guest address past the file image (BSS follows it at run time)."""
        return self.load_base + len(self.data)

    def contains(self, address: int) -> bool:
        return self.load_base <= address < self.end

    def offset(self, address: int) -> int:
        if not self.contains(address):
            raise ValueError(f"address {address:#010x} is outside the image")
        return address - self.load_base

    def u32(self, address: int) -> int:
        return struct.unpack_from("<I", self.data, self.offset(address))[0]

    def cstring(self, address: int) -> str:
        start = self.offset(address)
        stop = self.data.find(b"\0", start)
        return self.data[start : stop if stop >= 0 else len(self.data)].decode("ascii", "replace")

    # -- frameworks ------------------------------------------------------------------------

    @property
    def thunks(self) -> dict[int, Thunk]:
        """Every import thunk by guest address."""
        return {t.address: t for fw in self.frameworks for t in fw.thunks}

    def _parse_frameworks(self) -> Iterator[Framework]:
        primary = self.u32(self.load_base + HEADER_PRIMARY_DESCRIPTOR)
        if fw := self._parse_descriptor(primary):
            yield fw
        for offset in range(0, len(self.data) - 4, 4):
            if self.data[offset : offset + 4] in BLOCK_MAGIC:
                if fw := self._parse_descriptor(self.load_base + offset + 4):
                    yield fw

    def _parse_descriptor(self, address: int) -> Framework | None:
        if not self.contains(address + DESCRIPTOR_THUNKS_OFFSET):
            return None
        name = self.cstring(address)
        if not name or not name.isprintable():
            return None
        if self.u32(address + DESCRIPTOR_LINK_OFFSET) == 0:
            return None  # RetailOS rejects descriptors without a link pointer; so do we.
        count = self.u32(address + DESCRIPTOR_COUNT_OFFSET)
        thunks = []
        slot = address + DESCRIPTOR_THUNKS_OFFSET
        # Trust the thunks actually present over the declared count (same policy as the emulator).
        while len(thunks) < count and self.contains(slot):
            if self.u32(slot) & LDR_PC_PC_MASK != LDR_PC_PC_BITS:
                break
            thunks.append(Thunk(slot, name, len(thunks)))
            slot += 4
        if not thunks:
            return None
        return Framework(name, tuple(thunks))
