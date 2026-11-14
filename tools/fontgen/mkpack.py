#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Makes the glyph packs a device adds to its fonts: letters beyond ASCII.

    tools/fontgen/mkpack.py --out <folder> [--sheet <folder>]

Writes latin.glyphs, cyrillic.glyphs and greek.glyphs: for every face of the
device's fonts that has the letters, their glyphs in the form the firmware's
own fonts have (firmware/components/gfx: gfx_pack_add). A face that lacks a
letter leaves it out, and the device spells it in ASCII. --sheet also writes a
picture of every pack, to be looked at by a person.

Needs freetype-py, as fontgen.py does.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fontgen  # noqa: E402  (render, pack_rows, write_png)
import jersey_cyrillic  # noqa: E402

try:
    import freetype
except ImportError:
    sys.exit("freetype-py is missing: pip install freetype-py")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONTS = os.path.join(ROOT, "assets", "fonts")

# The faces, as gfx_face_t numbers them, and how each is rasterized: the
# sizes the firmware's own fonts were made at.
FACES = [
    (0, "DepartureMono-Regular.otf", 11),
    (1, "Jersey10-Regular.ttf", 18.6667),
    (2, "Jersey15-Regular.ttf", 27),
    (4, "Jersey25-Regular.ttf", 41),
]

# Jersey 20 (face 3) is left out: the firmware uses it for the largest size
# alone, and with it the packs do not fit their partition.

# Jersey has no Cyrillic: its letters are made from its Latin ones.
MADE = {"Jersey10-Regular.ttf", "Jersey15-Regular.ttf", "Jersey25-Regular.ttf"}

DEGREE = 0x00B0  # the host draws its own, sized for every font


def packs():
    """The packs and their code points, from the one place they are written."""
    import json
    with open(os.path.join(ROOT, "sdk", "languages.json")) as f:
        ranges = json.load(f)["packs"]
    return {name: [cp for first, last in spans for cp in range(first, last + 1) if cp != DEGREE]
            for name, spans in ranges.items()}


PACKS = packs()

# Letters a face has under another code: the Kazakh schwa is the shape of the
# Latin one, which Departure Mono has drawn.
SAME_AS = {0x04D8: 0x018F, 0x04D9: 0x0259}

MAGIC, VERSION, NAME_MAX = 0x594C4750, 1, 11


def face_part(number, font_file, px, code_points):
    face = freetype.Face(os.path.join(FONTS, font_file))
    face.set_char_size(0, round(px * 64), 72, 72)
    glyphs, bits, missing, drawn = [], bytearray(), [], {}
    made = jersey_cyrillic.glyphs(lambda cp: fontgen.render(face, cp)) if font_file in MADE else {}
    for cp in sorted(set(code_points)):
        rendered = fontgen.render(face, cp)
        if rendered is None:
            rendered = made.get(cp)
        if rendered is None and cp in SAME_AS:
            rendered = fontgen.render(face, SAME_AS[cp])
        if rendered is None:
            missing.append(cp)
            continue
        width, height, left, top, advance, rows = rendered
        glyphs.append(struct.pack("<HBBbbBxI", cp, width, height, left, top, advance, len(bits)))
        bits += fontgen.pack_rows(rows, width)
        drawn[cp] = rendered
    while len(bits) % 4:
        bits.append(0)
    part = struct.pack("<BxHI", number, len(glyphs), len(bits)) + b"".join(glyphs) + bytes(bits)
    return part, missing, drawn


def sheet(path, drawn, px):
    """Every glyph of a face in rows of sixteen, the code point's last digits as columns."""
    cell_w, cell_h, per_row = px + 3, px + 5, 16
    cps = sorted(drawn)
    rows = (len(cps) + per_row - 1) // per_row
    canvas = [[False] * (cell_w * per_row) for _ in range(cell_h * rows)]
    for index, cp in enumerate(cps):
        width, height, left, top, _, glyph_rows = drawn[cp]
        x0 = (index % per_row) * cell_w + 1 + left
        y0 = (index // per_row) * cell_h + px - top
        for y in range(height):
            for x in range(width):
                if glyph_rows[y][x] and 0 <= y0 + y < len(canvas) and 0 <= x0 + x < len(canvas[0]):
                    canvas[y0 + y][x0 + x] = True
    fontgen.write_png(path, canvas, 4)


def main():
    args = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
    args.add_argument("--out", required=True, help="folder for the packs")
    args.add_argument("--sheet", help="folder for a picture of each pack")
    options = args.parse_args()
    os.makedirs(options.out, exist_ok=True)
    for name, code_points in PACKS.items():
        parts = []
        for number, font_file, px in FACES:
            part, missing, drawn = face_part(number, font_file, px, code_points)
            if not drawn:
                continue
            parts.append(part)
            if missing:
                print("  %s lacks %d: %s" % (font_file, len(missing),
                                             " ".join("U+%04X" % cp for cp in missing[:12])))
            if options.sheet:
                os.makedirs(options.sheet, exist_ok=True)
                sheet(os.path.join(options.sheet, "%s-face%d.png" % (name, number)), drawn,
                      round(px))
        body = b"".join(parts)
        total = 24 + len(body)
        header = struct.pack("<IHBxI12s", MAGIC, VERSION, len(parts), total, name.encode())
        assert len(header) == 24 and len(name) <= NAME_MAX
        path = os.path.join(options.out, name + ".glyphs")
        with open(path, "wb") as f:
            f.write(header + body)
        print("%s: %d bytes, %d faces" % (path, total, len(parts)))


if __name__ == "__main__":
    main()
