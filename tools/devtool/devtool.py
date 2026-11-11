#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Look at a device and drive it over the network, without a cable.

    devtool.py status                     what the device is and how it is doing
    devtool.py log                        the end of its log
    devtool.py shot [out.png]             the frame its panel shows
    devtool.py press 3 3 2 [--shot f.png] buttons, one after another: 1-4 a short
                                          press, 1L-4L a long one
    devtool.py open clock [--shot f.png]  put an installed application on the screen
    devtool.py run sdk/apps/clock [--shot f.png]
                                          build the application, upload it and
                                          restart the device into it
    devtool.py flash [image.bin]          upload a firmware image and wait for the
                                          device to come back

The device is named with --host or PERINKPHERAL_HOST (its address is on
the Device page of its menu). It has to run a firmware built with
APP_DEV_CONSOLE, which is the default. Only the Python standard library
is needed; `run` also needs what tools/mkapp needs.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(TOOLS, "preview"))
from preview import write_png  # noqa: E402

REQUEST_TIMEOUT_S = 10
UPLOAD_TIMEOUT_S = 120
PNG_SCALE = 2
BUSY_RETRIES = 3
POLL_S = 0.3
# A press that changes the picture shows within this; one that changes
# nothing is not waited for any longer.
NEW_FRAME_WAIT_S = 4.0
# A frame may be held back by the refresh policy for up to a minute.
PENDING_FRAME_WAIT_S = 70.0
RESTART_WAIT_S = 60.0
DEFAULT_FIRMWARE = os.path.join(os.path.dirname(TOOLS), "firmware", "build", "perinkpheral.bin")


class Device:
    def __init__(self, host):
        self.base = "http://" + host

    def request(self, path, method="GET", body=None, timeout=REQUEST_TIMEOUT_S):
        """Returns (body, headers); exits with the device's own words when it refuses."""
        request = urllib.request.Request(self.base + path, data=body, method=method)
        for attempt in range(BUSY_RETRIES + 1):
            try:
                with urllib.request.urlopen(request, timeout=timeout) as answer:
                    return answer.read(), answer.headers
            except urllib.error.HTTPError as refusal:
                # 503: the host loop was busy, with the panel most likely.
                if refusal.code == 503 and attempt < BUSY_RETRIES:
                    time.sleep(1)
                    continue
                sys.exit("%s %s: %s %s" % (method, path, refusal.code,
                                           refusal.read().decode("utf-8", "replace").strip()))

    def status(self):
        return json.loads(self.request("/api/status")[0])

    def screen(self):
        """Returns (rows of bools where True is ink, refresh count, a newer frame is waiting)."""
        bmp, headers = self.request("/api/dev/screen")
        return bmp_rows(bmp), int(headers["X-Frame"]), headers["X-Pending"] == "1"

    def settled_screen(self, seen_frame):
        """The picture once the panel has caught up with what was asked of it.

        Waits for a refresh after `seen_frame`, briefly, and then for as long
        as the device says a newer frame is on its way.
        """
        started = time.time()
        while True:
            rows, frame, pending = self.screen()
            waited = time.time() - started
            if pending and waited < PENDING_FRAME_WAIT_S:
                time.sleep(POLL_S)
            elif frame == seen_frame and waited < NEW_FRAME_WAIT_S:
                time.sleep(POLL_S)
            else:
                return rows

    def wait_until_back(self):
        """After a restart: the device is gone for a moment, then answers again."""
        time.sleep(3)
        deadline = time.time() + RESTART_WAIT_S
        while time.time() < deadline:
            try:
                with urllib.request.urlopen(self.base + "/api/status", timeout=2) as answer:
                    return json.loads(answer.read())
            except (urllib.error.URLError, OSError):
                time.sleep(1)
        sys.exit("the device did not come back in %d seconds" % RESTART_WAIT_S)


def bmp_rows(bmp):
    """A 1-bit BMP as rows of bools, True where there is ink (colour 0)."""
    pixels_at = int.from_bytes(bmp[10:14], "little")
    width = int.from_bytes(bmp[18:22], "little")
    height = int.from_bytes(bmp[22:26], "little")
    row_bytes = (width + 31) // 32 * 4
    rows = []
    for row in range(height - 1, -1, -1):  # stored from the bottom up
        line = bmp[pixels_at + row * row_bytes:pixels_at + (row + 1) * row_bytes]
        rows.append([(line[x >> 3] >> (7 - (x & 7))) & 1 == 0 for x in range(width)])
    return rows


def save_shot(rows, path):
    write_png(path, rows, PNG_SCALE)
    print(os.path.abspath(path))


def command_status(device, options):
    for key, value in device.status().items():
        print("%-18s %s" % (key, value))


def command_log(device, options):
    sys.stdout.write(device.request("/api/log")[0].decode("utf-8", "replace"))


def command_shot(device, options):
    rows, _, pending = device.screen()
    if pending:
        print("a newer frame is waiting for the panel; this is the one it shows now",
              file=sys.stderr)
    save_shot(rows, options.out)


def command_press(device, options):
    for key in options.keys:
        button, is_long = key[0], key[1:].upper() == "L"
        if button not in "1234" or key[1:].upper() not in ("", "L"):
            sys.exit("a key is 1-4, or 1L-4L for a long press: %r" % key)
        _, seen_frame, _ = device.screen()
        device.request("/api/dev/button?button=%s&press=%s" % (button, "long" if is_long else "short"),
                       method="POST")
        rows = device.settled_screen(seen_frame)
    if options.shot:
        save_shot(rows, options.shot)


def command_open(device, options):
    _, seen_frame, _ = device.screen()
    device.request("/api/dev/open?app=" + urllib.parse.quote(options.app), method="POST")
    rows = device.settled_screen(seen_frame)
    if options.shot:
        save_shot(rows, options.shot)


def command_run(device, options):
    with open(os.path.join(options.app_dir, "manifest.json")) as manifest:
        name = json.load(manifest)["name"]
    with tempfile.TemporaryDirectory() as built:
        subprocess.run([sys.executable, os.path.join(TOOLS, "mkapp", "mkapp.py"), options.app_dir,
                        "--out", built], check=True)
        # The module first: a manifest alone would list an application that cannot start.
        for file_name in ("app.wasm", "manifest.json"):
            with open(os.path.join(built, file_name), "rb") as content:
                device.request("/api/dev/app?name=%s&file=%s" % (urllib.parse.quote(name), file_name),
                               method="POST", body=content.read(), timeout=UPLOAD_TIMEOUT_S)
    device.request("/api/dev/run?app=" + urllib.parse.quote(name), method="POST")
    print("uploaded %s, restarting into it" % name)
    device.wait_until_back()
    if options.shot:
        # The splash goes first; the application takes the screen after it.
        rows = device.settled_screen(seen_frame=1)
        save_shot(rows, options.shot)


def command_flash(device, options):
    with open(options.image, "rb") as image:
        answer, _ = device.request("/api/ota", method="POST", body=image.read(),
                                   timeout=UPLOAD_TIMEOUT_S)
    print(answer.decode("utf-8", "replace").strip())
    status = device.wait_until_back()
    print("back: firmware %s in %s%s" % (status["firmware"], status["slot"],
                                         ", on probation" if status["on_probation"] else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=os.environ.get("PERINKPHERAL_HOST"),
                        help="the device's address; PERINKPHERAL_HOST by default")
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("status").set_defaults(run=command_status)
    commands.add_parser("log").set_defaults(run=command_log)

    shot = commands.add_parser("shot")
    shot.add_argument("out", nargs="?", default="shot.png")
    shot.set_defaults(run=command_shot)

    press = commands.add_parser("press")
    press.add_argument("keys", nargs="+")
    press.add_argument("--shot", help="save the picture after the last press")
    press.set_defaults(run=command_press)

    open_app = commands.add_parser("open")
    open_app.add_argument("app")
    open_app.add_argument("--shot")
    open_app.set_defaults(run=command_open)

    run = commands.add_parser("run")
    run.add_argument("app_dir")
    run.add_argument("--shot", help="save the picture once the application is on the screen")
    run.set_defaults(run=command_run)

    flash = commands.add_parser("flash")
    flash.add_argument("image", nargs="?", default=DEFAULT_FIRMWARE)
    flash.set_defaults(run=command_flash)

    options = parser.parse_args()
    if not options.host:
        sys.exit("which device? give --host <address> or set PERINKPHERAL_HOST")
    try:
        options.run(Device(options.host), options)
    except (urllib.error.URLError, OSError) as failure:
        sys.exit("no answer from %s: %s" % (options.host, failure))


if __name__ == "__main__":
    main()
