#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Holds the guide's table of limits against the headers it quotes.

    check_limits.py [guide.html]

Every row of the guide that names constants - HOST_KV_KEY_MAX / _VALUE_MAX -
and gives their values - 15 / 1024 - is compared with sdk/host_api.h and
sdk/lib/manifest_limits.h. A value that differs, or a constant the headers
do not have, fails the build of the guide.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
HEADERS = [os.path.join(ROOT, "sdk", "host_api.h"),
           os.path.join(ROOT, "sdk", "lib", "manifest_limits.h")]
UNITS = {"KB": 1024, "MB": 1024 * 1024}


def defines():
    found = {}
    for path in HEADERS:
        with open(path) as f:
            text = re.sub(r"/\*.*?\*/", " ", f.read(), flags=re.S)
        for name, body in re.findall(r"^#define[ \t]+(\w+)[ \t]+([^\n]+)", text, re.M):
            found.setdefault(name, body.strip())
    return found


def value_of(name, found, depth=0):
    if name not in found or depth > 8:
        raise KeyError(name)
    body = re.sub(r"\b([A-Z_][A-Z0-9_]*)\b",
                  lambda m: str(value_of(m.group(1), found, depth + 1)), found[name])
    body = re.sub(r"(\d+)[uUlL]+\b", r"\1", body)
    if not re.fullmatch(r"[\d\s()+\-*/x0-9a-fA-F]+", body):
        raise ValueError("%s = %s" % (name, found[name]))
    return int(eval(body.replace("/", "//")))


def names_of(cell):
    """HOST_KV_KEY_MAX / _VALUE_MAX / HOST_FILES_MAX: a short one continues the first."""
    names = []
    for part in [p.strip() for p in cell.split("/")]:
        if part.startswith("_") and names:
            first = names[0]
            stem = first
            # The longest head of the first name the part can follow.
            while stem and not stem.endswith("_"):
                stem = stem[:-1]
            candidates = []
            head = first
            while "_" in head:
                head = head[:head.rindex("_")]
                candidates.append(head + part)
            names.append(candidates)
        else:
            names.append(part)
    return names


def main():
    guide = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "guide.html")
    with open(guide) as f:
        html = f.read()
    found = defines()
    # The table of limits alone: other tables name constants beside numbers that are
    # not their values - a font and the height of its capitals.
    start = html.find('id="ref-limits"')
    if start < 0:
        sys.exit("guide.html: the section of limits (id=\"ref-limits\") is not found")
    end = html.find("<h1", start + 1)
    html = html[start:end if end > 0 else len(html)]
    rows = re.findall(r'<tr><td class="c">((?:[A-Z][A-Z0-9_]*|_[A-Z0-9_]+)(?: / (?:[A-Z][A-Z0-9_]*|_[A-Z0-9_]+))*)'
                      r'</td><td>([^<]*)</td>', html)
    problems, checked = [], 0
    for cell, values in rows:
        numbers = re.findall(r"(\d+)(?:\s*(KB|MB))?", values)
        names = names_of(cell)
        if len(numbers) < len(names):
            problems.append("%s: %d names and %d values" % (cell, len(names), len(numbers)))
            continue
        # One unit at the end stands for the whole row: "511 / 255 bytes", "23 / 64 KB".
        for index, name in enumerate(names):
            options = name if isinstance(name, list) else [name]
            known = [option for option in options if option in found]
            if not known:
                problems.append("%s: no such constant in the headers" % options[0])
                continue
            number, unit = numbers[index]
            said = int(number) * UNITS.get(unit, 1)
            actual = value_of(known[0], found)
            checked += 1
            if said != actual:
                problems.append("%s: the guide says %s%s, the header has %d"
                                % (known[0], number, " " + unit if unit else "", actual))
    if checked < 20:
        problems.append("only %d limits found in the guide - its table is read wrong" % checked)
    if problems:
        sys.exit("guide.html disagrees with the headers:\n  " + "\n  ".join(problems))
    print("%d limits of the guide agree with the headers" % checked)


if __name__ == "__main__":
    main()
