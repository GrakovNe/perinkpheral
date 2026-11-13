#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The text of the guide, piece by piece: what a translation is made of.

    units.py list                       the pieces of guide.html, one a line
    units.py missing <language>         the pieces guide.<language>.txt has no translation for
    units.py apply <language>           guide.<language>.html from guide.html and guide.<language>.txt

A piece is a paragraph, a list item, a heading, a caption, a table cell or a comment in a code
block. guide.<language>.txt holds pairs: a piece as `list` prints it, its translation on the next
line, an empty line. A piece that is not there stays as it is - a name, a number, code; a
translation of "=" says so on purpose.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PIECE = re.compile(
    r'(<p(?: [^>]*)?>)(.*?)(</p>)|(<li>)(.*?)(</li>)|(<h[123][^>]*>)(.*?)(</h[123]>)|(<figcaption>)(.*?)(</figcaption>)'
    r'|(<t[dh][^>]*>)(.*?)(</t[dh]>)|(<div class="rule"><b>)(.*?)(</b>)|((?<=</b>) )(.*?)(</div>)'
    r'|(<div class="facts">)(.*?)(</div>)|(<title>)(.*?)(</title>)', re.S)
COMMENT = re.compile(r'(/\* )(.*?)( \*/)|(# )([^\n<]*)()|(  [a-z.-]+/?  +)((?:optional|identity|source)[^\n]*?)(  +\(section|\n|(?=</pre>))')


def pieces(source):
    """[(start, end, text)] of everything a reader reads, in the order of the file."""
    found = []
    body_from = source.index("<body>")
    pre = [(m.start(), m.end()) for m in re.finditer(r"<pre>.*?</pre>", source, re.S)]
    in_pre = lambda at: any(a <= at < b for a, b in pre)
    for m in re.finditer(r"<title>(.*?)</title>", source[:body_from], re.S):
        found.append((m.start(1), m.end(1), m.group(1)))
    for m in PIECE.finditer(source, body_from):
        if in_pre(m.start()):
            continue
        group = next(i for i in range(2, 30, 3) if m.group(i) is not None)
        found.append((m.start(group), m.end(group), m.group(group)))
    for a, b in pre:
        for m in COMMENT.finditer(source, a, b):
            group = next(i for i in (2, 5, 8) if m.group(i) is not None)
            if m.group(group).strip():
                found.append((m.start(group), m.end(group), m.group(group)))
    return sorted(found)


def flat(text):
    return re.sub(r"\s+", " ", text).strip()


def translations(language):
    pairs = {}
    with open(os.path.join(HERE, "guide.%s.txt" % language), encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f]
    at = 0
    while at < len(lines):
        if not lines[at].strip():
            at += 1
            continue
        if at + 1 >= len(lines) or not lines[at + 1].strip():
            sys.exit("guide.%s.txt, line %d: a piece without a translation" % (language, at + 1))
        pairs[flat(lines[at])] = lines[at + 1]
        at += 2
    return pairs


def is_text(piece):
    """Whether a person reads it: letters left once code and markup are taken away."""
    bare = re.sub(r"<code>.*?</code>|<[^>]+>|&[a-z]+;", "", piece)
    return re.search(r"[A-Za-z]{2}", bare) is not None


def main():
    with open(os.path.join(HERE, "guide.html")) as f:
        source = f.read()
    found = pieces(source)
    if len(sys.argv) == 2 and sys.argv[1] == "list":
        for _, _, text in found:
            print(flat(text))
        return
    if len(sys.argv) == 3 and sys.argv[1] in ("missing", "apply"):
        language = sys.argv[2]
        pairs = translations(language)
        if sys.argv[1] == "missing":
            seen = set()
            for _, _, text in found:
                if is_text(text) and flat(text) not in pairs and flat(text) not in seen:
                    seen.add(flat(text))
                    print(flat(text))
            return
        out, at, done = [], 0, 0
        for start, end, text in found:
            new = pairs.get(flat(text))
            out.append(source[at:start])
            if new is None or new == "=":
                out.append(source[start:end])
            else:
                out.append(new)
                done += 1
            at = end
        out.append(source[at:])
        result = "".join(out).replace('<html lang="en">', '<html lang="%s">' % language)
        # The running foot is in the style sheet, not in the text.
        foot = "perINKpheral / application guide"
        if foot in pairs:
            result = result.replace('content: "%s"' % foot, 'content: "%s"' % pairs[foot])
        with open(os.path.join(HERE, "guide.%s.html" % language), "w", encoding="utf-8") as f:
            f.write(result)
        print("guide.%s.html: %d pieces translated of %d" % (language, done, len(found)))
        return
    sys.exit(__doc__)


if __name__ == "__main__":
    main()
