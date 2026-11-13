#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Look at a device and drive it over the network, without a cable.

    devtool.py status                     what the device is and how it is doing
    devtool.py log [--follow] [--app]     the end of its log; --follow goes on
                                          printing what is logged, --app keeps
                                          only the lines of applications
    devtool.py serve [app dir] [--port N] a page on this computer with the live
                                          screen, the four buttons, the log and,
                                          given an application, a Run button
    devtool.py new my-app [--title T]     a folder with an application to start
                                          from: the SDK's sample under that name
    devtool.py shot [out.png]             the frame its panel shows
    devtool.py press 3 3 2 [--shot f.png] buttons, one after another: 1-4 a short
                                          press, 1L-4L a long one
    devtool.py open clock [--shot f.png]  put an installed application on the screen
    devtool.py run sdk/apps/clock [--shot f.png]
                                          build the application, upload it and
                                          restart the device into it
    devtool.py set weather lat 55.75      a value into an application's store: a
                                          setting, until the web page has a form
    devtool.py flash [image.bin] [--apps] upload a firmware image and wait for the
                                          device to come back; --apps rebuilds and
                                          uploads the applications of sdk/apps too,
                                          which a change of the host API needs

The device is named with --host or PERINKPHERAL_HOST (its address is on
the Device page of its menu). It has to run a firmware built with
APP_DEV_CONSOLE, which is the default. Only the Python standard library
is needed; `run` also needs what tools/mkapp needs.
"""
import argparse
import glob
import http.server
import json
import os
import re
import shutil
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
BMP_HEADER_BYTES = 62
FRAME_SIDE_MAX = 4096
POLL_S = 0.3
# A press that changes the picture shows within this; one that changes
# nothing is not waited for any longer.
NEW_FRAME_WAIT_S = 4.0
# A frame may be held back by the refresh policy for up to a minute.
PENDING_FRAME_WAIT_S = 70.0
RESTART_WAIT_S = 60.0
SDK_APPS = os.path.join(os.path.dirname(TOOLS), "sdk", "apps")
DEFAULT_FIRMWARE = os.path.join(os.path.dirname(TOOLS), "firmware", "build", "perinkpheral.bin")


class Device:
    def __init__(self, host):
        self.host = host
        self.base = "http://" + host

    def fetch(self, path, method="GET"):
        """Returns (body, headers), or raises: for a caller that goes on after a refusal."""
        request = urllib.request.Request(self.base + path, method=method)
        with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_S) as answer:
            return answer.read(), answer.headers

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
        if headers.get("X-Frame") is None:
            sys.exit("the answer is not a frame: is the firmware built with APP_DEV_CONSOLE?")
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
    is_whole = (len(bmp) >= BMP_HEADER_BYTES and 0 < width <= FRAME_SIDE_MAX
                and 0 < height <= FRAME_SIDE_MAX and pixels_at + height * row_bytes <= len(bmp))
    if not is_whole:
        sys.exit("the frame came through cut short: %d bytes" % len(bmp))
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


APP_LOG_MARK = " app: "  # the tag the host logs an application's lines under
FOLLOW_POLL_S = 1.0


def log_since(device, end):
    """What was logged after `end` (None: all there is), and where that ends."""
    text, headers = device.request("/api/log" + ("?since=%d" % end if end is not None else ""))
    return text.decode("utf-8", "replace"), int(headers.get("X-Log-End", 0))


def command_log(device, options):
    end = None
    while True:
        text, end = log_since(device, end)
        for line in text.splitlines(keepends=True):
            if not options.app or APP_LOG_MARK in line:
                sys.stdout.write(line)
        sys.stdout.flush()
        if not options.follow:
            return
        time.sleep(FOLLOW_POLL_S)


def command_new(options):
    """The SDK's sample under another name: a manifest and one source file."""
    name = os.path.basename(os.path.normpath(options.dir))
    if not re.fullmatch(r"[a-z0-9_-]{1,13}", name):
        sys.exit("\"%s\": an application's name is 1-13 of a-z, 0-9, '-' and '_', and it is "
                 "its folder's name" % name)
    if os.path.exists(options.dir):
        sys.exit("%s is there already" % options.dir)
    sample = os.path.join(SDK_APPS, "hello")
    with open(os.path.join(sample, "manifest.json")) as f:
        manifest = json.load(f)
    manifest["name"] = name
    manifest["title"] = options.title or name.replace("-", " ").replace("_", " ").capitalize()
    os.makedirs(options.dir)
    with open(os.path.join(options.dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    shutil.copy(os.path.join(sample, "hello.c"), os.path.join(options.dir, name + ".c"))
    # For an IDE that reads CMake: the code is understood, and building, running and the
    # page are targets. The checkout is found by PERINKPHERAL, or where it was just now.
    checkout = os.path.relpath(os.path.join(os.path.dirname(SDK_APPS), ".."),
                               os.path.abspath(options.dir)).replace(os.sep, "/")
    with open(os.path.join(options.dir, "CMakeLists.txt"), "w") as f:
        f.write("cmake_minimum_required(VERSION 3.20)\n\n"
                "if(DEFINED ENV{PERINKPHERAL})\n"
                "    set(PERINKPHERAL \"$ENV{PERINKPHERAL}\")\n"
                "else()\n"
                "    set(PERINKPHERAL \"${CMAKE_CURRENT_LIST_DIR}/%s\")\n"
                "endif()\n"
                "include(\"${PERINKPHERAL}/sdk/cmake/perinkpheral.cmake\")\n\n"
                "project(%s C)\n"
                "perinkpheral_app(.)\n" % (checkout, name))
    print("%s: build it and put it on the device with `devtool.py run %s`"
          % (os.path.abspath(options.dir), options.dir))


# ---- the page: the device in a browser tab, beside any editor ----
#
# The device takes requests from its own page only, so a page of ours cannot
# ask it directly: this is a small server on the developer's computer that
# gives the page and passes its requests on.

class Page(http.server.BaseHTTPRequestHandler):
    device = None
    app_dir = None

    def log_message(self, *arguments):
        pass

    def answer(self, status, body, content_type="text/plain; charset=utf-8", headers=()):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def pass_on(self, path, method="GET", expose=()):
        try:
            body, headers = self.device.fetch(path, method)
        except (urllib.error.URLError, OSError) as failure:
            return self.answer(502, ("no answer from the device: %s" % failure).encode())
        self.answer(200, body, headers.get("Content-Type", "application/octet-stream"),
                    [(name, headers[name]) for name in expose if headers.get(name)])

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == "/":
            with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "page.html"),
                      "rb") as f:
                page = f.read().replace(b"{{app}}", (self.app_dir or "").encode())
            self.answer(200, page, "text/html; charset=utf-8")
        elif url.path == "/screen":
            self.pass_on("/api/dev/screen", expose=("X-Frame", "X-Pending"))
        elif url.path == "/log":
            self.pass_on("/api/log?" + url.query, expose=("X-Log-End",))
        elif url.path == "/status":
            self.pass_on("/api/status")
        else:
            self.answer(404, b"Not found.")

    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(url.query)
        if url.path == "/press":
            key = query.get("key", [""])[0]
            if not re.fullmatch(r"[1-4]L?", key):
                return self.answer(400, b"a key is 1-4, or 1L-4L")
            self.pass_on("/api/dev/button?button=%s&press=%s"
                         % (key[0], "long" if key.endswith("L") else "short"), "POST")
        elif url.path == "/run" and self.app_dir:
            # The build's own words go back to the page, whatever they are.
            done = subprocess.run([sys.executable, os.path.abspath(__file__), "--host",
                                   self.device.host, "run", self.app_dir],
                                  capture_output=True, text=True)
            self.answer(200 if done.returncode == 0 else 500,
                        (done.stdout + done.stderr).encode())
        else:
            self.answer(404, b"Not found.")


def command_serve(device, options):
    device.status()  # is it there, and built with the development tools
    Page.device = device
    Page.app_dir = os.path.abspath(options.app_dir) if options.app_dir else None
    server = http.server.ThreadingHTTPServer(("127.0.0.1", options.port), Page)
    print("http://127.0.0.1:%d - the device at %s%s; Ctrl-C stops it"
          % (options.port, device.host, ", running " + Page.app_dir if Page.app_dir else ""))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


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


def upload_app(device, app_dir):
    """Builds the application and puts its two files on the device; returns its name."""
    with open(os.path.join(app_dir, "manifest.json")) as manifest:
        name = json.load(manifest)["name"]
    with tempfile.TemporaryDirectory() as built:
        subprocess.run([sys.executable, os.path.join(TOOLS, "mkapp", "mkapp.py"), app_dir,
                        "--out", built], check=True)
        # The manifest last: until it is there the device does not list the
        # application, and by then everything it needs is.
        data_files = sorted(set(os.listdir(built)) - {"app.wasm", "manifest.json"})
        for file_name in ["app.wasm"] + data_files + ["manifest.json"]:
            with open(os.path.join(built, file_name), "rb") as content:
                device.request("/api/dev/app?name=%s&file=%s" % (urllib.parse.quote(name), file_name),
                               method="POST", body=content.read(), timeout=UPLOAD_TIMEOUT_S)
    return name


def command_run(device, options):
    name = upload_app(device, options.app_dir)
    device.request("/api/dev/run?app=" + urllib.parse.quote(name), method="POST")
    print("uploaded %s, restarting into it" % name)
    device.wait_until_back()
    if options.shot:
        # The splash goes first; the application takes the screen after it.
        rows = device.settled_screen(seen_frame=1)
        save_shot(rows, options.shot)


def command_set(device, options):
    device.request("/api/dev/kv?app=%s&key=%s" % (urllib.parse.quote(options.app),
                                                  urllib.parse.quote(options.key)),
                   method="POST", body=options.value.encode())


def command_flash(device, options):
    with open(options.image, "rb") as image:
        answer, _ = device.request("/api/dev/flash", method="POST", body=image.read(),
                                   timeout=UPLOAD_TIMEOUT_S)
    print(answer.decode("utf-8", "replace").strip())
    status = device.wait_until_back()
    print("back: firmware %s in %s%s" % (status["firmware"], status["slot"],
                                         ", on probation" if status["on_probation"] else ""))
    if options.apps:
        # A firmware upload replaces the firmware and nothing else: applications
        # built for the old host API are refused by the new one. They follow the
        # firmware - it is the new one that knows how to take them - and the
        # device starts once more to list them.
        for app_dir in sorted(glob.glob(os.path.join(SDK_APPS, "*", ""))):
            print("uploaded " + upload_app(device, app_dir))
        device.request("/api/dev/restart", method="POST")
        device.wait_until_back()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=os.environ.get("PERINKPHERAL_HOST"),
                        help="the device's address; PERINKPHERAL_HOST by default")
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("status").set_defaults(run=command_status)
    log = commands.add_parser("log")
    log.add_argument("--follow", action="store_true", help="go on printing what is logged")
    log.add_argument("--app", action="store_true", help="only what applications log")
    log.set_defaults(run=command_log)

    serve = commands.add_parser("serve")
    serve.add_argument("app_dir", nargs="?", help="an application the page can build and run")
    serve.add_argument("--port", type=int, default=8400)
    serve.set_defaults(run=command_serve)

    new = commands.add_parser("new")
    new.add_argument("dir", help="the folder to make; its name is the application's")
    new.add_argument("--title", help="what the menu shows; the name by default")
    new.set_defaults(run=None)

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

    set_value = commands.add_parser("set")
    set_value.add_argument("app")
    set_value.add_argument("key")
    set_value.add_argument("value")
    set_value.set_defaults(run=command_set)

    flash = commands.add_parser("flash")
    flash.add_argument("image", nargs="?", default=DEFAULT_FIRMWARE)
    flash.add_argument("--apps", action="store_true",
                       help="upload the applications of sdk/apps as well, built anew")
    flash.set_defaults(run=command_flash)

    options = parser.parse_args()
    if options.command == "new":
        return command_new(options)  # needs no device
    if not options.host:
        sys.exit("which device? give --host <address> or set PERINKPHERAL_HOST")
    try:
        options.run(Device(options.host), options)
    except (urllib.error.URLError, OSError) as failure:
        sys.exit("no answer from %s: %s" % (options.host, failure))


if __name__ == "__main__":
    main()
