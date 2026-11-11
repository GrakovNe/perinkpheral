#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Make a catalog: a folder a device can install applications from.

    mkcatalog.py <folder with applications> [<another>...] --out <catalog folder>

Every application in the folders - a subfolder with a manifest.json - is
built with tools/mkapp and put into the catalog as it will lie on a
device, and one index is written beside them:

    catalog.json
    clock/manifest.json
    clock/app.wasm
    weather/manifest.json
    weather/app.wasm
    ...

A catalog is static files: put the folder anywhere that serves it over
HTTPS - a git hosting's raw files, its pages, a server of your own - and
give a device the address of the folder. It works the way a Debian
repository does: the device reads the index, and nothing but the index
tells it what is there.

catalog.json:

    { "catalog": 1,
      "apps": [ { "name": "clock", "title": "Clock", "version": "0.1.0", "abi": 1,
                  "files": [ { "name": "app.wasm", "size": 1741, "sha256": "..." },
                             { "name": "manifest.json", "size": 98, "sha256": "..." } ] } ] }

The files are listed in the order a device stores them: the manifest last,
so that an application cut off halfway is never listed as installed.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATALOG_VERSION = 1
INDEX_NAME = "catalog.json"


def build(app_dir, out_dir):
    subprocess.run([sys.executable, os.path.join(TOOLS, "mkapp", "mkapp.py"), app_dir,
                    "--out", out_dir], check=True)


def entry_of(built_dir):
    with open(os.path.join(built_dir, "manifest.json")) as manifest_file:
        manifest = json.load(manifest_file)
    names = sorted(set(os.listdir(built_dir)) - {"app.wasm", "manifest.json"})
    files = []
    for name in ["app.wasm"] + names + ["manifest.json"]:
        with open(os.path.join(built_dir, name), "rb") as content:
            data = content.read()
        files.append({"name": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    entry = {"name": manifest["name"], "title": manifest["title"],
             "version": manifest.get("version", ""), "abi": manifest["abi"], "files": files}
    # What a page shows of an application before it is installed.
    if manifest.get("system"):
        # The page asks before it installs an application with the system API.
        entry["system"] = True
    if "icon" in manifest:
        entry["icon"] = manifest["icon"]
    return entry


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("apps_dirs", nargs="+")
    parser.add_argument("--out", required=True)
    options = parser.parse_args()

    app_dirs = sorted((os.path.join(apps_dir, name) for apps_dir in options.apps_dirs
                       for name in os.listdir(apps_dir)
                       if os.path.isfile(os.path.join(apps_dir, name, "manifest.json"))),
                      key=os.path.basename)
    if not app_dirs:
        sys.exit("no applications in %s: an application is a folder with a manifest.json"
                 % ", ".join(options.apps_dirs))
    # Whatever the catalog held before is gone: an application that was taken
    # out of the folder must not stay in the catalog.
    if os.path.isdir(options.out):
        shutil.rmtree(options.out)
    os.makedirs(options.out)
    apps = []
    for app_dir in app_dirs:
        built_dir = os.path.join(options.out, os.path.basename(os.path.normpath(app_dir)))
        build(app_dir, built_dir)
        apps.append(entry_of(built_dir))
    with open(os.path.join(options.out, INDEX_NAME), "w") as index:
        json.dump({"catalog": CATALOG_VERSION, "apps": apps}, index, indent=2)
        index.write("\n")
    print("%s: %d applications" % (os.path.join(options.out, INDEX_NAME), len(apps)))


if __name__ == "__main__":
    main()
