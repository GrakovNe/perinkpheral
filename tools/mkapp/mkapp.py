#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Build a perINKpheral application: C sources -> app.wasm.

    tools/mkapp/mkapp.py sdk/apps/hello --out build/apps/hello

The result is build/apps/hello/app.wasm and manifest.json, ready for the
device's /apps/hello.

Compiles every .c file of the application together with the SDK's small
library (sdk/lib) for wasm32, without a C library, with exactly one page
of memory, and checks that the module asks the host only for what the
host API offers. The application's manifest.json is checked by the rules
the host applies and put next to the module. Needs wasi-sdk: set
WASI_SDK_PATH to where it is.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SDK = os.path.join(ROOT, "sdk")

# As in firmware/components/appsys/include/app_manifest.h.
NAME_MAX = 13
TITLE_MAX = 23
VERSION_MAX = 15
POLLS_MAX = 4
POLL_EVERY_MIN_S = 10
POLL_EVERY_MAX_S = 24 * 60 * 60
URL_MAX = 255
HEADERS_MAX = 4
PICK_PATHS_MAX = 16
PICK_PATH_MAX = 63
HEADER_NAME_MAX = 31
HEADER_VALUE_MAX = 127

WASM_PAGE = 65536
APP_STACK = 8192
HOST_MODULE = "host"
SECTION_IMPORT = 2
SECTION_MEMORY = 5
IMPORT_KIND_FUNCTION = 0


def clang():
    sdk_path = os.environ.get("WASI_SDK_PATH")
    if not sdk_path:
        sys.exit("WASI_SDK_PATH is not set: point it at an unpacked wasi-sdk")
    path = os.path.join(sdk_path, "bin", "clang")
    if not os.path.exists(path):
        sys.exit("no clang in %s" % os.path.join(sdk_path, "bin"))
    return path


def build(app_dir, wasm_path):
    sources = sorted(glob.glob(os.path.join(app_dir, "*.c")))
    if not sources:
        sys.exit("no .c files in %s" % app_dir)
    sources += sorted(glob.glob(os.path.join(SDK, "lib", "*.c")))
    command = [
        clang(), "--target=wasm32", "-nostdlib", "-ffreestanding", "-fno-builtin",
        "-Os", "-Wall", "-Wextra", "-Werror",
        "-I" + SDK, "-I" + os.path.join(SDK, "lib"),
        "-Wl,--no-entry", "-Wl,--strip-all", "-Wl,--gc-sections",
        "-Wl,--initial-memory=%d" % WASM_PAGE, "-Wl,--max-memory=%d" % WASM_PAGE,
        "-Wl,-z,stack-size=%d" % APP_STACK,
        "-o", wasm_path,
    ] + sources
    if subprocess.run(command).returncode != 0:
        sys.exit("%s did not compile" % app_dir)


class Reader:
    """Just enough of the wasm binary format to list imports and memory."""

    def __init__(self, data):
        self.data = data
        self.at = 0

    def byte(self):
        value = self.data[self.at]
        self.at += 1
        return value

    def unsigned(self):
        value = shift = 0
        while True:
            byte = self.byte()
            value |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                return value

    def name(self):
        length = self.unsigned()
        text = self.data[self.at:self.at + length].decode("utf-8")
        self.at += length
        return text


def inspect(wasm_path):
    """Returns (imports as (module, name), memory pages as (initial, maximum))."""
    with open(wasm_path, "rb") as f:
        reader = Reader(f.read())
    if reader.data[:4] != b"\0asm":
        sys.exit("%s is not a wasm module" % wasm_path)
    reader.at = 8
    imports, memory = [], None
    while reader.at < len(reader.data):
        section = reader.byte()
        section_end = reader.unsigned() + reader.at
        if section == SECTION_IMPORT:
            for _ in range(reader.unsigned()):
                module, name = reader.name(), reader.name()
                kind = reader.byte()
                if kind != IMPORT_KIND_FUNCTION:
                    sys.exit("import %s.%s is not a function" % (module, name))
                reader.unsigned()
                imports.append((module, name))
        elif section == SECTION_MEMORY:
            reader.unsigned()  # always one memory
            has_maximum = reader.byte() & 1
            initial = reader.unsigned()
            memory = (initial, reader.unsigned() if has_maximum else None)
        reader.at = section_end
    return imports, memory


def host_imports():
    """The names host_api.h declares, read from the header itself."""
    names = set()
    with open(os.path.join(SDK, "host_api.h")) as f:
        for line in f:
            if line.startswith("HOST_IMPORT("):
                names.add(line[len("HOST_IMPORT("):line.index(",")])
    return names


def check(wasm_path):
    imports, memory = inspect(wasm_path)
    offered = host_imports()
    for module, name in imports:
        if module != HOST_MODULE or name not in offered:
            sys.exit("the module imports %s.%s, which the host does not offer" % (module, name))
    if memory != (1, 1):
        sys.exit("the module's memory is %r pages, it must be exactly one" % (memory,))
    return imports


def host_api_version():
    with open(os.path.join(SDK, "host_api.h")) as f:
        found = re.search(r"#define\s+HOST_API_VERSION\s+(\d+)", f.read())
    if not found:
        sys.exit("sdk/host_api.h does not define HOST_API_VERSION as a number")
    return int(found.group(1))


def unique_keys(pairs):
    """For json.load: the device reads the first of two equal keys, Python the last."""
    keys = [key for key, _ in pairs]
    for key in keys:
        if keys.count(key) > 1:
            raise ValueError("\"%s\" is there more than once" % key)
    return dict(pairs)


def check_poll(path, number, poll):
    where = "%s: poll %d" % (path, number)
    if not isinstance(poll, dict):
        sys.exit("%s must be an object" % where)
    url = poll.get("url")
    if (not isinstance(url, str) or len(url.encode()) > URL_MAX
            or not url.startswith(("http://", "https://"))):
        sys.exit("%s: \"url\" must be an http(s) URL of at most %d bytes" % (where, URL_MAX))
    every = poll.get("every")
    if (isinstance(every, bool) or not isinstance(every, (int, float))
            or not POLL_EVERY_MIN_S <= every <= POLL_EVERY_MAX_S):
        sys.exit("%s: \"every\" must be %d to %d seconds"
                 % (where, POLL_EVERY_MIN_S, POLL_EVERY_MAX_S))
    headers = poll.get("headers", {})
    if not isinstance(headers, dict) or len(headers) > HEADERS_MAX:
        sys.exit("%s: \"headers\" must be an object of at most %d" % (where, HEADERS_MAX))
    for name, value in headers.items():
        if (not name or len(name.encode()) > HEADER_NAME_MAX or not isinstance(value, str)
                or len(value.encode()) > HEADER_VALUE_MAX):
            sys.exit("%s: header \"%s\" has a name over %d bytes or a value that is not a "
                     "string of at most %d" % (where, name, HEADER_NAME_MAX, HEADER_VALUE_MAX))


    pick = poll.get("pick", [])
    if not isinstance(pick, list) or len(pick) > PICK_PATHS_MAX:
        sys.exit("%s: \"pick\" must be a list of at most %d paths" % (where, PICK_PATHS_MAX))
    for picked in pick:
        if not isinstance(picked, str) or not 0 < len(picked.encode()) <= PICK_PATH_MAX:
            sys.exit("%s: a path in \"pick\" must be a string of 1 to %d bytes: %r"
                     % (where, PICK_PATH_MAX, picked))


def check_manifest(app_dir):
    """The rules of firmware/components/appsys/app_manifest.c, before the device sees it."""
    path = os.path.join(app_dir, "manifest.json")
    try:
        with open(path) as f:
            text = f.read()
        manifest = json.loads(text, object_pairs_hook=unique_keys)
    except (OSError, ValueError) as error:
        sys.exit("%s: %s" % (path, error))
    if "\\u0000" in text:
        sys.exit("%s: a string must not contain \\u0000" % path)
    name = manifest.get("name")
    if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9_-]{1,%d}" % NAME_MAX, name):
        sys.exit("%s: \"name\" must be 1-%d of a-z, 0-9, - and _" % (path, NAME_MAX))
    if name != os.path.basename(os.path.normpath(app_dir)):
        sys.exit("%s: \"name\" must be the name of the application's folder" % path)
    title = manifest.get("title")
    if not isinstance(title, str) or not 0 < len(title.encode()) <= TITLE_MAX:
        sys.exit("%s: \"title\" must be 1-%d bytes" % (path, TITLE_MAX))
    version = manifest.get("version", "")
    if not isinstance(version, str) or len(version.encode()) > VERSION_MAX:
        sys.exit("%s: \"version\" must be a string of at most %d bytes" % (path, VERSION_MAX))
    if not isinstance(manifest.get("fullscreen", False), bool):
        sys.exit("%s: \"fullscreen\" must be true or false" % path)
    polls = manifest.get("polls", [])
    if not isinstance(polls, list) or len(polls) > POLLS_MAX:
        sys.exit("%s: \"polls\" must be a list of at most %d" % (path, POLLS_MAX))
    for number, poll in enumerate(polls):
        check_poll(path, number, poll)
    abi = manifest.get("abi")
    # True == 1 in Python, and the device wants a number.
    if isinstance(abi, bool) or abi != host_api_version():
        sys.exit("%s: \"abi\" must be %d, the version of sdk/host_api.h"
                 % (path, host_api_version()))
    return path


def main():
    args = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
    args.add_argument("app_dir")
    args.add_argument("--out", required=True, help="directory for app.wasm")
    options = args.parse_args()

    manifest_path = check_manifest(options.app_dir)
    os.makedirs(options.out, exist_ok=True)
    wasm_path = os.path.join(options.out, "app.wasm")
    build(options.app_dir, wasm_path)
    imports = check(wasm_path)
    shutil.copy(manifest_path, os.path.join(options.out, "manifest.json"))
    print("%s: %d bytes, %d host calls" % (wasm_path, os.path.getsize(wasm_path), len(imports)))


if __name__ == "__main__":
    main()
