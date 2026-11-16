# SPDX-License-Identifier: MIT
"""The host's icons, from drawings a person can type, into the firmware.

    python3 tools/fontgen/mkicons.py [--sheet out.png]

Every icon is a file of firmware/components/gfx/icons: one drawing or
more, each on a square canvas of 11, 22 or 44 cells, under a line that
says how many ("22:"), rows of `#` for ink and `.` for what was there. An
icon is drawn from the finest drawing that it can draw at two pixels a
cell or more - the eleven at 22 pixels, the 22 at 44, the 44 at 88 and 99
- so a line of a drawing is two of its cells at least: at its size it
weighs what the text's beside it does, and only curves and slants are
finer. An icon with no drawing of 11 cells, or one of 44 and none of 22,
gets the smaller made from the next by taking a cell wherever there is ink
under it, so a thin line holds.

Writes firmware/components/gfx/gfx_icon_art.c: the drawings packed a bit
a cell, row by row, and for every icon where each of its drawings is. The
order of ORDER is the contract's (HOST_ICON_* in sdk/host_api.h,
GFX_ICON_* in gfx_icons.h); a name added goes at the end.
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ICONS = ROOT / "firmware/components/gfx/icons"
OUT = ROOT / "firmware/components/gfx/gfx_icon_art.c"
SIZES = (11, 22, 44)

ORDER = [
    "signal", "no_signal", "up", "down", "left", "right", "check", "cross", "plus", "minus",
    "trash", "gear", "clock", "hourglass", "bell", "info", "warning", "play", "pause", "update",
    "sun", "partly_cloudy", "cloud", "rain", "snow", "thunder", "wind", "umbrella", "drop",
    "thermometer", "bolt", "plug", "battery", "home", "bulb", "door", "moon", "chip", "network",
    "gauge", "co2", "sunrise", "sunset",
]


def read(path):
    drawings, size = {}, None
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        if line.endswith(":"):
            size = int(line[:-1])
            if size not in SIZES or size in drawings:
                sys.exit(f"{path.name}: a drawing of {size} cells")
            drawings[size] = []
            continue
        if size is None or set(line) - {"#", "."}:
            sys.exit(f"{path.name}: {line!r} is not a row of a drawing")
        drawings[size].append(line)
    for size, rows in drawings.items():
        if len(rows) != size or any(len(row) != size for row in rows):
            sys.exit(f"{path.name}: the drawing of {size} is not {size} by {size}")
    return drawings


def smaller(rows, size):
    """The drawing on `size` cells: a cell is ink wherever what it covers has some."""
    step = len(rows) // size
    return [
        "".join(
            "#" if any(rows[r * step + i][c * step + j] == "#" for i in range(step) for j in range(step))
            else "."
            for c in range(size)
        )
        for r in range(size)
    ]


def load():
    icons = {}
    for name in ORDER:
        path = ICONS / f"{name}.txt"
        if not path.exists():
            sys.exit(f"{path} is missing")
        drawings = read(path)
        if 22 not in drawings and 44 in drawings:
            drawings[22] = smaller(drawings[44], 22)
        if 11 not in drawings:
            drawings[11] = smaller(drawings[min(drawings)], 11)
        icons[name] = drawings
    strays = {path.stem for path in ICONS.glob("*.txt")} - set(ORDER)
    if strays:
        sys.exit(f"drawings of icons not in ORDER: {sorted(strays)}")
    return icons


def pack(rows):
    out = bytearray()
    for row in rows:
        bits = 0
        width = (len(row) + 7) // 8 * 8
        for column, cell in enumerate(row.ljust(width, ".")):
            bits = bits << 1 | (cell == "#")
        out += bits.to_bytes(width // 8, "big")
    return out


def write(icons):
    data = bytearray()
    entries = []
    for name in ORDER:
        places = []
        for size in SIZES:
            if size in icons[name]:
                places.append(f"{{ {size}, {len(data)} }}")
                data += pack(icons[name][size])
            else:
                places.append("{ 0, 0 }")
        entries.append(f"    [GFX_ICON_{name.upper()}] = {{ {{ {', '.join(places)} }} }},")
    if len(data) > 0xFFFF:
        sys.exit("the drawings outgrow a 16-bit offset")
    lines = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + "," for i in range(0, len(data), 16)]
    OUT.write_text(
        "/*\n * SPDX-License-Identifier: MIT\n * Copyright (c) 2026 GrakovNe\n *\n"
        " * Made by tools/fontgen/mkicons.py from firmware/components/gfx/icons:\n"
        " * change the drawings there and run it again, not this file.\n */\n"
        '#include "gfx_icon_art.h"\n\n'
        f"const uint8_t GFX_ICON_BITS[{len(data)}] = {{\n"
        + "\n".join(f"    {line}" for line in lines)
        + "\n};\n\nconst gfx_icon_art_t GFX_ICON_ART[GFX_ICON_COUNT] = {\n"
        + "\n".join(entries)
        + "\n};\n"
    )
    return len(data)


def sheet(icons, out):
    """Every icon at 22, 44 and 99 pixels as the host draws them, for a person to look at."""
    import subprocess
    import tempfile

    sizes = (22, 44, 99)
    cell_w, cell_h = 110 * len(sizes), 110
    columns = 4
    rows_n = (len(ORDER) + columns - 1) // columns
    width, height = cell_w * columns, cell_h * rows_n
    image = [[1] * width for _ in range(height)]
    for index, name in enumerate(ORDER):
        ox, oy = (index % columns) * cell_w, (index // columns) * cell_h
        for k, pixels in enumerate(sizes):
            doubled = [size for size in icons[name] if pixels // size >= 2]
            cells = max(doubled) if doubled else max(size for size in icons[name] if size <= pixels)
            scale = pixels // cells
            offset = (pixels - cells * scale) // 2
            rows = icons[name][cells]
            for r, row in enumerate(rows):
                for c, cell in enumerate(row):
                    if cell != "#":
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            y = oy + 5 + offset + r * scale + dy
                            x = ox + k * 110 + 5 + offset + c * scale + dx
                            image[y][x] = 0
    with tempfile.NamedTemporaryFile("w", suffix=".pbm", delete=False) as f:
        f.write(f"P1\n{width} {height}\n")
        for row in image:
            f.write(" ".join("0" if v else "1" for v in row) + "\n")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", f.name, out], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--sheet", help="a picture of every icon at the host's three sizes")
    args = parser.parse_args()
    icons = load()
    size = write(icons)
    print(f"{OUT.relative_to(ROOT)}: {len(ORDER)} icons, {size} bytes")
    if args.sheet:
        sheet(icons, args.sheet)


if __name__ == "__main__":
    main()
