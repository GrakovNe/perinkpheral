#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Render the host's screens natively and write PNGs of them.

    tools/preview/preview.py [out-dir]     # default: <system temp dir>/perinkpheral-preview

Compiles tools/preview/preview.c with the pure gfx and screens modules,
runs it and turns its PBMs into PNGs with the panel's gray-white paper.
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FW = os.path.join(ROOT, "firmware")
SRCS = [
    os.path.join(ROOT, "tools", "preview", "preview.c"),
    os.path.join(FW, "components", "gfx", "gfx.c"),
    os.path.join(FW, "components", "gfx", "font_square.c"),
    os.path.join(FW, "components", "ui", "wm.c"),
    os.path.join(FW, "main", "screens.c"),
]
INCS = [os.path.join(FW, "components", "gfx", "include"), os.path.join(FW, "components", "gfx", "fonts"),
        os.path.join(FW, "components", "ui", "include"), os.path.join(FW, "main")]
SCALE = 2


def read_pbm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(maxsplit=3)
    w, h = int(parts[1]), int(parts[2])
    stride = (w + 7) // 8
    bits = parts[3]
    rows = []
    for y in range(h):
        row = bits[y * stride:(y + 1) * stride]
        rows.append([(row[x >> 3] >> (7 - (x & 7))) & 1 == 1 for x in range(w)])
    return rows


def write_png(path, rows, scale):
    w = len(rows[0]) * scale
    h = len(rows) * scale
    raw = bytearray()
    for row in rows:
        line = bytearray()
        for black in row:
            line += (b"\x14\x14\x13" if black else b"\xE9\xE7\xE1") * scale
        for _ in range(scale):
            raw += b"\x00" + line

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(tempfile.gettempdir(), "perinkpheral-preview")
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, "preview")
        cmd = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", exe] + \
              ["-I" + i for i in INCS] + SRCS
        subprocess.run(cmd, check=True)
        subprocess.run([exe, d], check=True)
        for name in sorted(os.listdir(d)):
            if name.endswith(".pbm"):
                png = os.path.join(out, name[:-4] + ".png")
                write_png(png, read_pbm(os.path.join(d, name)), SCALE)
                print(png)


if __name__ == "__main__":
    main()
