#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The project's mark, made from the one drawing of it: the host's update
icon, firmware/components/gfx/icons/update.txt, eleven cells square.

    python3 tools/fontgen/mkmark.py

Writes assets/mark: mark.svg (the cells bare), mark-on-paper.svg (on paper,
two cells round it), mark-512.png and mark-on-paper-512.png; and prints the
cells as rectangles, for the copies that stand in code - the portal's
Brand.tsx and the device page's markup. No library: a PNG is written by hand,
pixel for pixel of the cells."""
import os
import struct
import sys
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
ICON = os.environ.get("MARK_ICON") or os.path.join(ROOT, "firmware", "components", "gfx", "icons", "update.txt")
OUT = os.path.join(ROOT, "assets", "mark")
INK = (0x14, 0x14, 0x13)
PAPER = (0xE9, 0xE7, 0xE1)
MARGIN = 2  # cells of paper round the mark


def cells():
    with open(ICON) as f:
        lines = f.read().splitlines()
    start = lines.index("11:") + 1
    rows = lines[start:start + 11]
    if len(rows) != 11 or any(len(row) != 11 or set(row) - set(".#") for row in rows):
        sys.exit("mkmark: update.txt has no eleven by eleven drawing")
    return rows


def rectangles(rows):
    """Runs of ink across a row, the same run in the rows under it joined."""
    runs = []
    for y, row in enumerate(rows):
        x = 0
        while x < len(row):
            if row[x] == "#":
                end = x
                while end < len(row) and row[end] == "#":
                    end += 1
                runs.append([x, y, end - x, 1])
                x = end
            else:
                x += 1
    joined = []
    for run in runs:
        below = next((r for r in joined if r[0] == run[0] and r[2] == run[2] and
                      r[1] + r[3] == run[1] and r[2] == 1), None)
        if below is not None:
            below[3] += 1
        else:
            joined.append(run)
    return joined


def svg(rects, size, offset, paper):
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" shape-rendering="crispEdges">'
             % (size, size)]
    if paper:
        parts.append('<rect width="%d" height="%d" fill="#E9E7E1"/>' % (size, size))
    parts.append('<g fill="#141413">')
    for x, y, w, h in rects:
        parts.append('<rect x="%d" y="%d" width="%d" height="%d"/>' % (x + offset, y + offset, w, h))
    parts.append("</g></svg>\n")
    return "".join(parts)


def png(rows, size, pixels, paper):
    """The cells, `size` of them a side with the mark in the middle, each
    pixel the cell under its centre."""
    offset = (size - 11) // 2
    lines = []
    for py in range(pixels):
        line = bytearray([0])
        cy = py * size // pixels - offset
        for px in range(pixels):
            cx = px * size // pixels - offset
            is_ink = 0 <= cx < 11 and 0 <= cy < 11 and rows[cy][cx] == "#"
            if is_ink:
                line += bytes(INK) + b"\xff"
            elif paper:
                line += bytes(PAPER) + b"\xff"
            else:
                line += b"\x00\x00\x00\x00"
        lines.append(bytes(line))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    head = struct.pack(">IIBBBBB", pixels, pixels, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) +
            chunk(b"IDAT", zlib.compress(b"".join(lines), 9)) + chunk(b"IEND", b""))


def main():
    rows = cells()
    rects = rectangles(rows)
    with open(os.path.join(OUT, "mark.svg"), "w") as f:
        f.write(svg(rects, 11, 0, False))
    with open(os.path.join(OUT, "mark-on-paper.svg"), "w") as f:
        f.write(svg(rects, 11 + 2 * MARGIN, MARGIN, True))
    with open(os.path.join(OUT, "mark-512.png"), "wb") as f:
        f.write(png(rows, 11, 512, False))
    with open(os.path.join(OUT, "mark-on-paper-512.png"), "wb") as f:
        f.write(png(rows, 11 + 2 * MARGIN, 512, True))
    if len(sys.argv) > 2 and sys.argv[1] == "--on-paper-png":
        # mkmark.py --on-paper-png <pixels> <file>: the mark on paper at another size.
        with open(sys.argv[3], "wb") as f:
            f.write(png(rows, 11 + 2 * MARGIN, int(sys.argv[2]), True))
    print("cells:", ", ".join("[%d, %d, %d, %d]" % tuple(r) for r in rects))
    print("path:", "".join("M%d %dh%dv%dH%dz" % (x, y, w, h, x) for x, y, w, h in rects))


main()
