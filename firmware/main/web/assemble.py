#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The device's page, made of its parts: page.html with its markers filled in.

    assemble.py <web folder> <page.html out> <page.html.gz out>

    <!-- style: style.css -->          the stylesheet, in a <style>
    <!-- include: about.html -->       a piece of markup, as it is
    <!-- script: js/a.js js/b.js -->   the scripts in this order, as one <script>

A url("<name>.woff2") in the stylesheet becomes the font itself, as a data:
address: the page is one file. departure.woff2 is Departure Mono cut to the
letters the page shows, made with fontTools:

    pyftsubset assets/fonts/DepartureMono-Regular.otf --flavor=woff2 \
        --unicodes="U+0020-007E,U+00A0-00FF,U+0400-045F,U+2013-2014,U+2018-201E,U+2026,U+2116,U+2212" \
        --output-file=firmware/main/web/departure.woff2

Run by the firmware's build (main/CMakeLists.txt) with ESP-IDF's own Python; the device
serves the page packed, and every browser unpacks gzip.
"""
import base64
import gzip
import os
import re
import sys


def main():
    web, out, packed = sys.argv[1:4]
    read = lambda name: open(os.path.join(web, name), encoding="utf-8").read()

    def inline_fonts(css):
        def font(found):
            with open(os.path.join(web, found.group(1)), "rb") as f:
                return 'url("data:font/woff2;base64,' + base64.b64encode(f.read()).decode() + '")'
        return re.sub(r'url\("([\w.-]+\.woff2)"\)', font, css)

    def fill(found):
        kind, names = found.group(1), found.group(2).split()
        if kind == "style":
            return "<style>\n" + inline_fonts("".join(read(name) for name in names)) + "</style>"
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
