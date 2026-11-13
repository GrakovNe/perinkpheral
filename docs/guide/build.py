#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Builds the application guide: guide.html into a PDF.

    docs/guide/build.py [--out perinkpheral-app-guide.pdf]

The guide is one HTML file, written by hand. This puts a table of contents
into it - made of its headings - and prints it with a Chromium browser, twice:
the first print tells on which page every heading landed.

Needs a Chromium-based browser (the CHROME environment variable, or one found
in the usual places) and pdftotext and pdfunite of poppler.
"""
import argparse
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
BROWSERS = ["google-chrome", "chromium", "chromium-browser", "chrome",
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
            "/Applications/Chromium.app/Contents/MacOS/Chromium"]


def browser():
    wanted = os.environ.get("CHROME")
    for candidate in ([wanted] if wanted else []) + BROWSERS:
        found = shutil.which(candidate) or (candidate if os.path.exists(candidate) else None)
        if found:
            return found
    sys.exit("no Chromium browser found: set CHROME to one")


def headings(text):
    """[(level, id, title)] of the h1 and h2 of the guide, in order."""
    found = []
    for level, attributes, title in re.findall(r"<h([12])([^>]*)>(.*?)</h\1>", text, re.S):
        ident = re.search(r'id="([^"]+)"', attributes)
        if ident and "notoc" not in attributes:
            found.append((int(level), ident.group(1), re.sub(r"<[^>]+>", "", title).strip()))
    return found


def contents(found, pages):
    rows = []
    for level, ident, title in found:
        rows.append('<a class="toc toc%d" href="#%s"><span>%s</span><i></i><b>%s</b></a>'
                    % (level, ident, html.escape(title), pages.get(ident, "")))
    return "\n".join(rows)


def print_pdf(page, out):
    done = subprocess.run([browser(), "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                           "--print-to-pdf=" + out, "file://" + page],
                          capture_output=True, text=True)
    if done.returncode != 0 or not os.path.exists(out):
        sys.exit("the browser did not print: " + done.stderr[-400:])


def pages_of(pdf, found):
    """On which page each heading stands: its marker is printed, invisibly small, inside it."""
    text = subprocess.run(["pdftotext", "-layout", pdf, "-"], capture_output=True, text=True).stdout
    pages = {}
    for number, page in enumerate(text.split("\f"), 1):
        for ident in re.findall(r"\[\[([a-z0-9-]+)\]\]", page):
            pages.setdefault(ident, number)
    return pages


def main():
    args = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
    args.add_argument("--out", default=os.path.join(HERE, "perinkpheral-app-guide.pdf"))
    options = args.parse_args()
    with open(os.path.join(HERE, "guide.html")) as f:
        source = f.read()
    found = headings(source)
    # Where the fonts, the mark and the screens are: the page is printed from a scratch folder.
    root = "file://" + os.path.abspath(os.path.join(HERE, "..", ".."))
    source = source.replace("{{ROOT}}", root)
    # A table's first row is its head, and is said again on every page the table runs to.
    source = re.sub(r"<table>\s*(<tr><th.*?</tr>)", r"<table><thead>\1</thead>", source, flags=re.S)
    # A section's number on a plate of its own: "4. A first application".
    source = re.sub(r'(<h1 id="[^"]+">)(\d+)\. ', r'\1<span class="plate">\2</span>', source)
    # The cover is a sheet of its own, printed to its edges; a browser does not print one page of
    # a document without margins and the rest with them. The two are joined afterwards.
    cover_from = source.index('<div class="cover">')
    cover_to = source.index("</div>", source.index('class="facts"')) + len("</div>")
    cover_to = source.index("</div>", cover_to) + len("</div>")
    body_from = source.index("<body>") + len("<body>")
    cover = (source[:body_from] + "\n<style>@page { margin: 0; @bottom-left { content: none; border: 0; } "
             "@bottom-right { content: none; border: 0; } }</style>\n" + source[cover_from:cover_to] +
             "\n</body>\n</html>\n")
    # The pages after the cover count from 2.
    source = (source[:cover_from] + "<style>html { counter-reset: page 1; }</style>" + source[cover_to:])
    with tempfile.TemporaryDirectory() as scratch:
        page, first = os.path.join(scratch, "guide.html"), os.path.join(scratch, "first.pdf")
        cover_pdf, body_pdf = os.path.join(scratch, "cover.pdf"), os.path.join(scratch, "body.pdf")
        # A marker inside every heading, for the first print to be searched for.
        marked = re.sub(r'(<h[12][^>]*id="([^"]+)"[^>]*>)', r'\1<span class="mark">[[\2]]</span>', source)
        with open(page, "w") as f:
            f.write(marked.replace("<!--CONTENTS-->", contents(found, {})))
        print_pdf(page, first)
        pages = {ident: number + 1 for ident, number in pages_of(first, found).items()}
        with open(page, "w") as f:
            f.write(source.replace("<!--CONTENTS-->", contents(found, pages)))
        print_pdf(page, body_pdf)
        with open(page, "w") as f:
            f.write(cover)
        print_pdf(page, cover_pdf)
        joined = subprocess.run(["pdfunite", cover_pdf, body_pdf, os.path.abspath(options.out)],
                                capture_output=True, text=True)
        if joined.returncode != 0:
            sys.exit("pdfunite did not join the cover and the guide: " + joined.stderr[-300:])
    missing = [ident for _, ident, _ in found if ident not in pages]
    if missing:
        print("no page found for: " + ", ".join(missing))
    print("%s: %d headings" % (options.out, len(found)))


if __name__ == "__main__":
    main()
