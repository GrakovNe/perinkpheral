#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The firmware's list of languages is the SDK's: sdk/languages.json is the one place,
and appconf.c repeats it for a device that has no file to read."""
import json
import re
import sys

sdk, firmware = sys.argv[1], sys.argv[2]
with open(sdk) as f:
    wanted = [(language["code"], language["name"]) for language in json.load(f)["languages"]]
with open(firmware) as f:
    have = re.findall(r'\{ "([a-z]{2})",\s+"([A-Za-z]+)" \}', f.read())
if wanted != have:
    sys.exit("appconf.c and sdk/languages.json name different languages:\n  %s\n  %s" % (have, wanted))
print("%d checks, 0 failed" % len(wanted))
