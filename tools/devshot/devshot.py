#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Drive a device through its development console and save what it shows.

    tools/devshot/devshot.py <port> [--keys 3,2] [--out shot.png] [--wait 2.5]

Types the keys into the USB console one by one (1-4 a short press of that
button, q/w/e/r a long press of button 1-4), waiting after each for the
panel to refresh, then asks the device for the frame its panel shows and
writes it as a PNG. Needs the firmware built with APP_DEV_CONSOLE and the
pyserial package (the ESP-IDF Python environment has it).
"""
import argparse
import os
import sys
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "preview"))
from preview import write_png  # noqa: E402

DUMP_FRAME_KEY = b"p"
FRAME_TIMEOUT_S = 10
PNG_SCALE = 2


def read_frame(port):
    """Returns the rows of the frame as lists of bools (True is black)."""
    port.reset_input_buffer()
    port.write(DUMP_FRAME_KEY)
    deadline = time.time() + FRAME_TIMEOUT_S
    width = height = None
    rows = []
    while time.time() < deadline:
        line = port.readline().decode("ascii", "replace").strip()
        if line.startswith("FRAME BEGIN"):
            _, _, width, height = line.split()
            width, height = int(width), int(height)
            rows = []
        elif line == "FRAME END":
            if width is None or len(rows) != height:
                sys.exit("the frame came through incomplete: %d rows" % len(rows))
            return rows
        elif width is not None and len(line) == width // 8 * 2:
            row_bytes = bytes.fromhex(line)
            # In the frame a set bit is white.
            rows.append([(row_bytes[x >> 3] >> (7 - (x & 7))) & 1 == 0 for x in range(width)])
    sys.exit("no frame from the device: is the firmware built with APP_DEV_CONSOLE?")


def main():
    args = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
    args.add_argument("port")
    args.add_argument("--keys", default="", help="comma-separated keys to type first")
    args.add_argument("--out", default="shot.png")
    args.add_argument("--wait", type=float, default=2.5,
                      help="seconds to wait after each key (a full refresh takes about 3)")
    options = args.parse_args()

    with serial.Serial(options.port, 115200, timeout=0.5) as port:
        for key in filter(None, options.keys.split(",")):
            port.write(key.encode("ascii"))
            time.sleep(options.wait)
        write_png(options.out, read_frame(port), PNG_SCALE)
    print(options.out)


if __name__ == "__main__":
    main()
