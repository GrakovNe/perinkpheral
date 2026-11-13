#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Turns drawings into pictures an application carries in its code.

    mkpicture.py bell.png sun.txt --out pictures.h
    mkpicture.py pictures/ --out pictures.h
    mkpicture.py bell.png --show

A drawing is a PNG, a PBM, or a text file of rows of '#' for ink and '.' for
paper - what a person can type. A text file may hold several drawings: a
line "name:" begins each, and the file's own name is the one before any.
Of an image, what is darker than the middle is ink (--threshold), and what
is transparent is paper.

Every drawing becomes `static const uint8_t PICTURE_<NAME>[]` in the header:
a byte of how many cells wide, a byte of how many tall, and the cells a bit
each - what gfx_picture() and ui_cell() of sdk/host_api.h take. A picture is
at most 255 cells a side; it is drawn with a cell of as many pixels as the
application asks, so a small drawing serves large sizes too.

tools/mkapp does this by itself for an application's `pictures` folder.
"""
import argparse
import os
import re
import struct
import sys
import zlib

SIDE_MAX = 255
EXTENSIONS = (".png", ".pbm", ".txt")


def fail(path, why):
    sys.exit("%s: %s" % (path, why))


# ---- reading ----

def unfilter(raw, width_bytes, pixel_bytes, height):
    """The rows of a PNG with its line filters taken off."""
    rows, before, at = [], bytearray(width_bytes), 0
    for _ in range(height):
        kind, line = raw[at], bytearray(raw[at + 1:at + 1 + width_bytes])
        at += 1 + width_bytes
        for i in range(width_bytes):
            left = line[i - pixel_bytes] if i >= pixel_bytes else 0
            up = before[i]
            corner = before[i - pixel_bytes] if i >= pixel_bytes else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                guess = left + up - corner
                nearest = min((abs(guess - left), 0, left), (abs(guess - up), 1, up),
                              (abs(guess - corner), 2, corner))[2]
                line[i] = (line[i] + nearest) & 255
        rows.append(line)
        before = line
    return rows


def read_png(path, threshold):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        fail(path, "not a PNG")
    at, header, palette, transparency, packed = 8, None, None, b"", b""
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            transparency = body
        elif kind == b"IDAT":
            packed += body
    width, height, depth, colour, _, _, interlace = header
    if interlace:
        fail(path, "an interlaced PNG is not read: save it without interlacing")
    if depth == 16:
        fail(path, "16 bits a channel are not read: save it with 8")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    width_bytes = (width * channels * depth + 7) // 8
    rows = unfilter(zlib.decompress(packed), width_bytes, max(1, channels * depth // 8), height)

    def sample(row, index):
        if depth == 8:
            return row[index]
        value = (row[index * depth // 8] >> (8 - depth - index * depth % 8)) & ((1 << depth) - 1)
        return value if colour == 3 else value * 255 // ((1 << depth) - 1)

    def is_ink(row, x):
        if colour == 3:
            index = sample(row, x)
            red, green, blue = palette[index * 3:index * 3 + 3]
            alpha = transparency[index] if index < len(transparency) else 255
        elif colour in (0, 4):
            red = green = blue = sample(row, x * channels)
            alpha = sample(row, x * channels + 1) if colour == 4 else 255
        else:
            red, green, blue = (sample(row, x * channels + i) for i in range(3))
            alpha = sample(row, x * channels + 3) if colour == 6 else 255
        return alpha >= 128 and (red * 299 + green * 587 + blue * 114) // 1000 < threshold

    return [[is_ink(row, x) for x in range(width)] for row in rows]


def read_pbm(path):
    with open(path, "rb") as f:
        data = f.read()
    tokens = re.sub(rb"#[^\n]*", b"", data).split(None, 3)
    kind, width, height = tokens[0], int(tokens[1]), int(tokens[2])
    if kind == b"P1":
        bits = [c == ord("1") for c in b"".join(tokens[3:]) if c in b"01"]
        return [bits[y * width:(y + 1) * width] for y in range(height)]
    if kind != b"P4":
        fail(path, "a PBM is P1 or P4")
    stride, body = (width + 7) // 8, tokens[3]
    return [[bool(body[y * stride + x // 8] & (0x80 >> (x % 8))) for x in range(width)]
            for y in range(height)]


def read_text(path):
    """One or several drawings: [(name or None, rows)]."""
    drawings, name, rows = [], None, []
    with open(path) as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]*:", line):
                if rows:
                    drawings.append((name, rows))
                name, rows = line[:-1], []
            elif set(line) <= set("#."):
                rows.append([c == "#" for c in line])
            else:
                fail(path, "line %d: a row is '#' and '.', a name ends with ':'" % number)
    if rows:
        drawings.append((name, rows))
    return drawings


def read(path, threshold):
    """[(name, rows)] of one file."""
    stem = os.path.splitext(os.path.basename(path))[0]
    extension = os.path.splitext(path)[1].lower()
    if extension == ".png":
        drawings = [(None, read_png(path, threshold))]
    elif extension == ".pbm":
        drawings = [(None, read_pbm(path))]
    elif extension == ".txt":
        drawings = read_text(path)
    else:
        fail(path, "a drawing is one of " + ", ".join(EXTENSIONS))
    if not drawings:
        fail(path, "no drawing in it")
    named = []
    for name, rows in drawings:
        width = len(rows[0])
        if any(len(row) != width for row in rows):
            fail(path, "%s: its rows are not of one length" % (name or stem))
        if not 1 <= width <= SIDE_MAX or not 1 <= len(rows) <= SIDE_MAX:
            fail(path, "%s: %dx%d cells, and a picture is 1 to %d a side"
                 % (name or stem, width, len(rows), SIDE_MAX))
        named.append((name or stem, rows))
    return named


# ---- writing ----

def constant(name):
    return "PICTURE_" + re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").upper()


def packed(rows):
    out = bytearray([len(rows[0]), len(rows)])
    for row in rows:
        for start in range(0, len(row), 8):
            byte = 0
            for i, is_ink in enumerate(row[start:start + 8]):
                byte |= (0x80 >> i) if is_ink else 0
            out.append(byte)
    return bytes(out)


def art(rows):
    return ["".join("#" if cell else "." for cell in row) for row in rows]


def header(drawings):
    lines = ["/* Generated by tools/mkpicture: do not edit, change the drawings. */",
             "#pragma once", "", "#include <stdint.h>", ""]
    for name, rows in drawings:
        data = packed(rows)
        lines.append("/* %s: %dx%d" % (name, len(rows[0]), len(rows)))
        lines += [" *   " + row for row in art(rows)]
        lines.append(" */")
        lines.append("static const uint8_t %s[] = {" % constant(name))
        for start in range(0, len(data), 12):
            lines.append("    " + " ".join("0x%02x," % byte for byte in data[start:start + 12]))
        lines += ["};", ""]
    return "\n".join(lines)


def collect(paths, threshold):
    files = []
    for path in paths:
        if os.path.isdir(path):
            files += [os.path.join(path, name) for name in sorted(os.listdir(path))
                      if name.lower().endswith(EXTENSIONS) and not name.startswith(".")]
        else:
            files.append(path)
    drawings, seen = [], {}
    for path in files:
        for name, rows in read(path, threshold):
            if constant(name) in seen:
                fail(path, "%s is also in %s" % (constant(name), seen[constant(name)]))
            seen[constant(name)] = path
            drawings.append((name, rows))
    return drawings


def main():
    args = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
    args.add_argument("drawings", nargs="+", help="files, or a folder of them")
    args.add_argument("--out", help="the header to write")
    args.add_argument("--show", action="store_true", help="print the drawings as rows of # and .")
    args.add_argument("--threshold", type=int, default=128,
                      help="how dark is ink, 1-255 (default 128)")
    options = args.parse_args()
    if not options.out and not options.show:
        args.error("give --out or --show")
    drawings = collect(options.drawings, options.threshold)
    if not drawings:
        sys.exit("no drawings")
    if options.show:
        for name, rows in drawings:
            print("%s:" % name)
            print("\n".join(art(rows)) + "\n")
    if options.out:
        with open(options.out, "w") as f:
            f.write(header(drawings))
        total = sum(len(packed(rows)) for _, rows in drawings)
        print("%s: %d pictures, %d bytes" % (options.out, len(drawings), total))


if __name__ == "__main__":
    main()
