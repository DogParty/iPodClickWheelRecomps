#!/usr/bin/env python3
"""Are the game's shipped files being read, decoded and uploaded correctly?

    python3 tools/assets.py --tex-log build/texlog.txt > analysis/assets.txt

Two questions, answered separately, because the call-log oracle can answer neither: a draw hands
the framework an address and a count, and what was at that address is never an argument. A whole
suite can be green while every texture on screen is wrong.

**1. Does every shipped image parse?** Every `.raw`, `.pix` and `.ipd` in the game folder is
identified from its own header rather than from its extension — and it has to be, because the
extensions here mean nothing:

  * `.raw` is a **TGA** — 18-byte header, image type 3 (greyscale), 8 or 16 bits a pixel with an
    8-bit alpha channel declared in the descriptor byte, and TGA 2.0's 26-byte
    `TRUEVISION-XFILE` footer. The cube sheets and every font are these.
  * `.pix` is **either** a Windows BMP **or** the same 16-byte-header format `.ipd` uses. Six of
    the ten are BMPs and four are not, in the same folders, under the same extension.
  * `.ipd` is that 16-byte header — width, height, a format code, a spare word — followed by
    16-bit pixels. Format code 1 is RGB565, 2 is RGBA5551, 3 is RGBA4444, read from the low byte.

A file is called self-consistent when its header accounts for its length exactly.

**2. Does what the game uploads match what shipped?** `IPOD_TEX_LOG=1` on the recomp prints
three sample texels of every `glTexImage2D`, and this joins them against the files above: for an
upload of `W x H`, every shipped image of that size is decoded here by a reader written from
scratch and its three texels compared. An upload that reproduces a file *is* proof that the
game's own parser, the framework's decode and this reader all agree — three independent readings
of the same bytes. One that reproduces nothing is either a texture the game composed itself or a
disagreement worth chasing; the report names which files were candidates so it can be told.

Both halves are static: no emulator, no recording, no window. See `analysis/README.md`.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
from pathlib import Path

GAME_DIR = Path(__file__).resolve().parents[4] / "20 iPod games" / "Games_RO" / "99999"
IMAGE_SUFFIXES = {".raw", ".pix", ".ipd"}
# The `.ipd` header's format code, from its low byte — established by comparing each file against
# the format the game hands `glTexImage2D` for it (`EAPP_TEX_FMT_LOG=1` beside `IPOD_TEX_LOG=1`).
IPD_FORMATS = {1: "RGB565", 2: "RGBA5551", 3: "RGBA4444"}
TGA_FOOTER_BYTES = 26
TGA_HEADER_BYTES = 18


def read_ipd(data: bytes):
    """The 16-byte-header format, whatever extension it is wearing."""
    if len(data) <= 16:
        return None
    width, height, code, _spare = struct.unpack_from("<IIII", data, 0)
    if not (0 < width <= 4096 and 0 < height <= 4096) or 16 + width * height * 2 != len(data):
        return None
    name = IPD_FORMATS.get(code & 0xFF)
    if name is None:
        return width, height, None, f"IPD format {code:#x} (not decoded here)"
    pixels = []
    for index in range(width * height):
        p = struct.unpack_from("<H", data, 16 + index * 2)[0]
        if name == "RGBA4444":
            pixels.append(((p >> 12 & 0xF) * 17, (p >> 8 & 0xF) * 17, (p >> 4 & 0xF) * 17, (p & 0xF) * 17))
        elif name == "RGB565":
            r, g, b = (p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F
            pixels.append((((r * 255 + 15) // 31), ((g * 255 + 31) // 63), ((b * 255 + 15) // 31), 255))
        else:  # RGBA5551
            r, g, b, a = (p >> 11) & 0x1F, (p >> 6) & 0x1F, (p >> 1) & 0x1F, p & 1
            pixels.append((((r * 255 + 15) // 31), ((g * 255 + 15) // 31), ((b * 255 + 15) // 31), 255 if a else 0))
    return width, height, pixels, f"IPD {name}"


def read_tga(data: bytes):
    """Image type 3, greyscale, with the alpha channel the descriptor byte declares."""
    if len(data) < 44 or data[:3] != b"\x00\x00\x03":
        return None
    width, height = struct.unpack_from("<HH", data, 12)
    depth = data[16]
    expected = TGA_HEADER_BYTES + width * height * (depth // 8) + TGA_FOOTER_BYTES
    pixels = []
    if depth == 16:  # luminance + alpha
        for i in range(width * height):
            l, a = data[18 + i * 2], data[18 + i * 2 + 1]
            pixels.append((l, l, l, a))
    elif depth == 8:  # coverage only — GL_ALPHA, whose RGB is zero by construction
        for i in range(width * height):
            pixels.append((0, 0, 0, data[18 + i]))
    else:
        return width, height, None, f"TGA {depth}bpp (not decoded here)"
    fit = "exact" if expected == len(data) else f"{len(data) - expected:+} bytes"
    return width, height, pixels, f"TGA {depth}bpp grey+alpha [{fit}]"


def read_bmp(data: bytes):
    """A Windows BMP, read every way it could plausibly be read.

    Two of these files declare `BI_BITFIELDS` masks of `0x0f00/0x00f0/0x000f/0xf000` — ARGB4444 —
    and the game reads them as **RGBA**4444 anyway, which the join below establishes rather than
    assumes. So both orders are offered and the report says which one matched.
    """
    if len(data) < 54 or data[:2] != b"BM":
        return []
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    depth = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    masks = struct.unpack_from("<4I", data, 54) if compression == 3 else (0, 0, 0, 0)
    top_down = height < 0
    width, height = abs(width), abs(height)
    readings = []
    if depth == 16:
        stride = ((width * 2 + 3) // 4) * 4
        packed = masks[3] == 0xF000
        orders = ("ARGB4444", "RGBA4444") if packed else ("RGB565",)
        for order in orders:
            pixels = []
            for y in range(height):
                row = y if top_down else height - 1 - y
                for x in range(width):
                    p = struct.unpack_from("<H", data, offset + row * stride + x * 2)[0]
                    if order == "RGB565":
                        r, g, b = (p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F
                        pixels.append((((r * 255 + 15) // 31), ((g * 255 + 31) // 63), ((b * 255 + 15) // 31), 255))
                    else:
                        n = [(p >> 12) & 0xF, (p >> 8) & 0xF, (p >> 4) & 0xF, p & 0xF]
                        if order == "ARGB4444":
                            n = [n[1], n[2], n[3], n[0]]
                        pixels.append(tuple(v * 17 for v in n))
            readings.append((width, height, pixels, f"BMP {order}"))
    elif depth == 8:
        palette = [(data[54 + i * 4 + 2], data[54 + i * 4 + 1], data[54 + i * 4], data[54 + i * 4 + 3])
                   for i in range(256)]
        # A greyscale ramp whose every alpha byte is zero is a COVERAGE table, not colour: the
        # index is the alpha and the colour comes from the draw. `reference/eapp-loader/lib.rs`
        # arrived at the same reading for the same reason (`decode_bmp`, the `_a8` row).
        ramp = all(palette[i][0] == i and palette[i][3] == 0 for i in range(256))
        stride = ((width + 3) // 4) * 4
        pixels = []
        for y in range(height):
            row = y if top_down else height - 1 - y
            for x in range(width):
                i = data[offset + row * stride + x]
                pixels.append((0, 0, 0, i) if ramp else palette[i])
        readings.append((width, height, pixels, f"BMP 8bpp {'coverage ramp' if ramp else 'palette'}"))
    else:
        readings.append((width, height, None, f"BMP {depth}bpp comp {compression} (not decoded here)"))
    return readings


def readings_of(path: Path):
    data = path.read_bytes()
    out = read_bmp(data)
    if out:
        return out
    for reader in (read_tga, read_ipd):
        one = reader(data)
        if one is not None:
            return [one]
    return []


def probe(width: int, height: int, pixels, x: int, y: int) -> str:
    p = pixels[min(y, height - 1) * width + min(x, width - 1)]
    return f"{p[0]:02x}{p[1]:02x}{p[2]:02x}{p[3]:02x}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game-dir", type=Path, default=GAME_DIR)
    parser.add_argument("--tex-log", type=Path, help="stderr of a run with IPOD_TEX_LOG=1")
    args = parser.parse_args()

    files = sorted(p for p in args.game_dir.rglob("*") if p.suffix.lower() in IMAGE_SUFFIXES)
    by_size: dict[tuple[int, int], list] = {}
    print("Every image asset in the game folder, identified from its own header, not its extension.")
    print()
    print(f"{'file':<34}{'size':>10}  as read here")
    undecoded = []
    for path in files:
        rel = path.relative_to(args.game_dir).as_posix()
        readings = readings_of(path)
        if not readings:
            undecoded.append(rel)
            print(f"{rel:<34}{'?':>10}  NOT RECOGNISED")
            continue
        width, height, pixels, how = readings[0]
        print(f"{rel:<34}{f'{width}x{height}':>10}  {how}")
        for w, h, px, label in readings:
            if px is not None:
                by_size.setdefault((w, h), []).append((rel, px, label))
    print()
    print(f"{len(files)} files, {len(undecoded)} not recognised.")

    if args.tex_log is None:
        return 0
    uploads = []
    for line in args.tex_log.read_text().splitlines():
        m = re.match(r"tex#(\d+) (\d+)x(\d+)( alpha-only)? ([0-9a-f ]+)$", line.strip())
        if m:
            uploads.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(5).split()))
    print()
    print(f"{len(uploads)} texture uploads in {args.tex_log}, joined against those files:")
    print()
    unmatched = []
    matched = 0
    for name, width, height, probes in uploads:
        hit = None
        for rel, pixels, label in by_size.get((width, height), []):
            here = [probe(width, height, pixels, width // 8, height // 2),
                    probe(width, height, pixels, width // 4, height // 4),
                    probe(width, height, pixels, width // 2, height // 2)]
            if here == probes:
                hit = (rel, label)
                break
        if hit:
            matched += 1
            print(f"  tex#{name:<3} {f'{width}x{height}':>9}  = {hit[0]}   ({hit[1]})")
        else:
            unmatched.append((name, width, height, probes,
                              sorted({r for r, _, _ in by_size.get((width, height), [])})))
    print()
    print(f"  {matched} of {len(uploads)} reproduce a shipped file exactly.")
    for name, width, height, probes, candidates in unmatched:
        print(f"  tex#{name} {width}x{height}: no shipped file of that size gives {' '.join(probes)}")
        print(f"      files of that size: {candidates or 'none'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
