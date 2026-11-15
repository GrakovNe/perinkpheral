#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The device's page, made of its parts: page.html with its markers filled in.

    assemble.py <web folder> <page.html out> <page.html.gz out>

    <!-- style: style.css -->          the stylesheet, in a <style>
    <!-- include: about.html -->       a piece of markup, as it is
    <!-- script: js/a.js js/b.js -->   the scripts in this order, as one <script>

Run by the firmware's build (main/CMakeLists.txt) with ESP-IDF's own Python; the device
serves the page packed, and every browser unpacks gzip.
"""
import gzip
import os
import re
import sys


def main():
    web, out, packed = sys.argv[1:4]
    read = lambda name: open(os.path.join(web, name), encoding="utf-8").read()

    def fill(found):
        kind, names = found.group(1), found.group(2).split()
        if kind == "style":
            return "<style>\n" + "".join(read(name) for name in names) + "</style>"
        if kind == "include":
            return "".join(read(name) for name in names)
        return "<script>\n" + "\n".join(read(name) for name in names) + "</script>"

    page = re.sub(r"<!-- (style|include|script): ([^>]+?) -->", fill, read("page.html"))
    if re.search(r"<!-- (style|include|script):", page):
        sys.exit("assemble.py: a marker was left unfilled")
    with open(out, "w", encoding="utf-8") as f:
        f.write(page)
    with open(packed, "wb") as f:
        f.write(gzip.compress(page.encode("utf-8"), 9, mtime=0))


if __name__ == "__main__":
    main()
