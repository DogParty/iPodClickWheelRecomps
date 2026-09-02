#!/usr/bin/env python3
"""Convert the runtime's PPM screenshots to PNG (no third-party modules), and print a pixel hash.

    python3 tools/ppm2png.py build/shot-01.ppm [more.ppm ...]

Also accepts the emulator's PNG screenshots, so `play`'s and the recomp's frames can be compared
with the same hash: the FNV-1a over the raw RGB bytes, as runtime/main.cpp prints it.
"""

import struct
import sys
import zlib
from pathlib import Path


def read_ppm(path):
    data = path.read_bytes()
    fields, position = [], 0
    while len(fields) < 4:
        while data[position : position + 1].isspace():
            position += 1
        start = position
        while not data[position : position + 1].isspace():
            position += 1
        fields.append(data[start:position])
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[position + 1 : position + 1 + width * height * 3]


def read_png_rgb(path):
    data = path.read_bytes()
    position, idat, width = 8, b"", 0
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position : position + 8])
        body = data[position + 8 : position + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            if depth != 8 or colour not in (2, 6):
                raise SystemExit(f"{path}: only 8-bit RGB/RGBA PNGs are handled")
            channels = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat += body
        position += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        filter_type = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + a) & 0xFF
            elif filter_type == 2:
                line[i] = (line[i] + b) & 0xFF
            elif filter_type == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif filter_type == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + predictor) & 0xFF
        rows.append(bytes(line))
        previous = line
    rgb = b"".join(bytes(row[i : i + 3]) for row in rows for i in range(0, stride, channels))
    return width, height, rgb


def write_png(path, width, height, rgb):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    raw = b"".join(b"\0" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def fnv1a(data):
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def main(arguments):
    for argument in arguments:
        source = Path(argument)
        if source.suffix == ".png":
            width, height, rgb = read_png_rgb(source)
        else:
            width, height, rgb = read_ppm(source)
            write_png(source.with_suffix(".png"), width, height, rgb)
        print(f"{source}: {width}x{height} fnv1a {fnv1a(rgb):08x}")


# Guarded, because `tests/frames.py` imports the two readers above rather than duplicating them,
# and an import that converts whatever is on the command line is a trap.
if __name__ == "__main__":
    main(sys.argv[1:])
