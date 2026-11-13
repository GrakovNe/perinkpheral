#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The translations of applications as one table, for a person to read and correct.

    strings_csv.py export <language> <out.csv> <folder of applications> ...
    strings_csv.py import <language> <in.csv>

A row is one text: the application's folder, the key ("manifest/..." for what
the manifest says), the English and the translation. `import` writes back
every translation that was changed into the strings/<language>.json it came
from; keys and English are not touched. Paths are as seen from where it was run. A folder of applications is one whose
subfolders have a `strings` folder each; an application's own folder works too.
"""
import csv
import json
import os
import sys


def flat(texts, prefix=""):
    rows = {}
    for key, value in texts.items():
        if isinstance(value, dict):
            rows.update(flat(value, prefix + key + "/"))
        else:
            rows[prefix + key] = value
    return rows


def applications(folders):
    for folder in folders:
        candidates = [folder] + sorted(os.path.join(folder, name) for name in os.listdir(folder))
        for app_dir in candidates:
            if os.path.isfile(os.path.join(app_dir, "strings", "en.json")):
                yield os.path.abspath(app_dir)


def manifest_english(app_dir):
    """What of the manifest a person reads, under the keys a translation has it."""
    with open(os.path.join(app_dir, "manifest.json")) as f:
        manifest = json.load(f)
    words = {"manifest/title": manifest.get("title", "")}
    for setting in manifest.get("settings", []):
        at = "manifest/settings/%s/" % setting["key"]
        for part in ("title", "help"):
            if part in setting:
                words[at + part] = setting[part]
        for option in setting.get("options", []):
            words[at + "options/" + option] = option
    return words


def export(language, out, folders):
    with open(out, "w", newline="", encoding="utf-8-sig") as f:
        table = csv.writer(f)
        table.writerow(["application", "key", "en", language])
        for app_dir in applications(folders):
            with open(os.path.join(app_dir, "strings", "en.json")) as g:
                english = flat(json.load(g))
            english.update(manifest_english(app_dir))
            path = os.path.join(app_dir, "strings", language + ".json")
            translated = {}
            if os.path.exists(path):
                with open(path) as g:
                    translated = flat(json.load(g))
            for key in english:
                table.writerow([os.path.relpath(app_dir), key, english[key],
                                translated.get(key, "")])


def put(texts, key, value):
    parts = key.split("/")
    # An option's own text may have a slash in it: the key is what follows "options/".
    if "options" in parts:
        at = parts.index("options")
        parts = parts[:at + 1] + ["/".join(parts[at + 1:])]
    for part in parts[:-1]:
        texts = texts.setdefault(part, {})
    if texts.get(parts[-1]) == value:
        return False
    texts[parts[-1]] = value
    return True


def take(language, source):
    changed = {}
    with open(source, newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            if not row.get(language):
                continue
            path = os.path.join(row["application"], "strings", language + ".json")
            if not os.path.exists(path):
                continue  # a row somebody added: the host's own words, a note
            if path not in changed:
                with open(path) as g:
                    changed[path] = [json.load(g), 0]
            changed[path][1] += put(changed[path][0], row["key"], row[language])
    for path, (texts, count) in sorted(changed.items()):
        if count:
            with open(path, "w") as g:
                json.dump(texts, g, ensure_ascii=False, indent=2)
                g.write("\n")
            print("%s: %d changed" % (path, count))


if __name__ == "__main__":
    if len(sys.argv) >= 5 and sys.argv[1] == "export":
        export(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif len(sys.argv) == 4 and sys.argv[1] == "import":
        take(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
