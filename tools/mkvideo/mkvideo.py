#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Turn a video into a 1-bit stream small enough to be a file of an application.

    mkvideo.py <video> --out frames.bin [--width 200] [--height 150] [--fps 1]
               [--threshold 128]

The video is scaled down and cut to black and white. Every frame stands on
its own, so a player can show any of them without the ones before:

  1. each row is replaced by what differs from the row above it - a shape
     then costs only where its outline moves sideways;
  2. the result, read row after row, is runs: so many pixels alike, so
     many that differ, so many alike - starting with alike;
  3. a run of N is written as the Elias gamma code of N + 1: as many zero
     bits as the number has after its first, then the number itself.

The file, all numbers little-endian:

    u16 width, u16 height, u16 frames, u16 frames per ten seconds
    u32 where each frame starts, and one more: where the last one ends
    the frames, each padded to a whole byte, bits from the top of a byte down

Needs ffmpeg.
"""
import argparse
import struct
import subprocess
import sys


PLAYER_WIDTH_MAX = 400
PLAYER_FRAME_BYTES_MAX = 2048


def read_frames(video, width, height, fps, threshold):
    """Every frame as a list of bools, True where the picture is dark."""
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", video, "-vf",
         "fps=%s,scale=%d:%d:flags=area" % (fps, width, height),
         "-pix_fmt", "gray", "-f", "rawvideo", "-"],
        capture_output=True, check=True).stdout
    size = width * height
    return [[level < threshold for level in raw[at:at + size]]
            for at in range(0, len(raw) - size + 1, size)]


class BitWriter:
    def __init__(self):
        self.bits = []

    def gamma(self, number):
        """Elias gamma of a number that is at least 1."""
        binary = bin(number)[2:]
        self.bits += [0] * (len(binary) - 1) + [int(digit) for digit in binary]

    def to_bytes(self):
        padded = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(int("".join(map(str, padded[at:at + 8])), 2)
                     for at in range(0, len(padded), 8))


def encode_frame(frame, width):
    differs = [ink != (frame[at - width] if at >= width else False)
               for at, ink in enumerate(frame)]
    writer = BitWriter()
    run_differs, run_length = False, 0
    for value in differs:
        if value != run_differs:
            writer.gamma(run_length + 1)
            run_differs, run_length = value, 0
        run_length += 1
    writer.gamma(run_length + 1)
    return writer.to_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("video")
    parser.add_argument("--out", required=True)
    parser.add_argument("--width", type=int, default=200)
    parser.add_argument("--height", type=int, default=150)
    parser.add_argument("--fps", type=float, default=1)
    parser.add_argument("--threshold", type=int, default=128,
                        help="a pixel darker than this (0-255) is ink")
    options = parser.parse_args()

    frames = read_frames(options.video, options.width, options.height, options.fps,
                         options.threshold)
    if not frames:
        sys.exit("no frames came out of %s" % options.video)
    encoded = [encode_frame(frame, options.width) for frame in frames]
    # What sdk players take: say so here, not on the device's screen.
    largest = max(len(frame) for frame in encoded)
    if options.width > PLAYER_WIDTH_MAX or largest > PLAYER_FRAME_BYTES_MAX:
        sys.exit("the player takes frames up to %d wide and %d bytes; this is %d wide and %d "
                 "bytes: use a smaller picture" % (PLAYER_WIDTH_MAX, PLAYER_FRAME_BYTES_MAX,
                                                    options.width, largest))
    if round(options.fps * 10) < 1:
        sys.exit("--fps is at least 0.1")
    header_bytes = 8 + 4 * (len(frames) + 1)
    starts, at = [], header_bytes
    for frame in encoded:
        starts.append(at)
        at += len(frame)
    starts.append(at)
    with open(options.out, "wb") as out:
        out.write(struct.pack("<4H", options.width, options.height, len(frames),
                              round(options.fps * 10)))
        out.write(struct.pack("<%dI" % len(starts), *starts))
        for frame in encoded:
            out.write(frame)
    print("%s: %d frames of %dx%d, %d bytes, the largest frame %d"
          % (options.out, len(frames), options.width, options.height, at,
             max(len(frame) for frame in encoded)))


if __name__ == "__main__":
    main()
