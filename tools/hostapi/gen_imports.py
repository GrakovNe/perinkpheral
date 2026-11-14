#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The host's table of imports, made from the contract.

    gen_imports.py <sdk/host_api.h> <out folder>

Writes host_imports.inc and system_imports.inc: for every HOST_IMPORT and
HOST_SYSTEM_IMPORT of the header a row { "name", "signature", host_name }
of the table firmware/components/appsys/app.c links into a module. The
signature is the interpreter's: i for a number of 32 bits and for a
pointer written `*`, I for one of 64, v for no result. The header is the
one place a call's signature is written; the function that answers the
call is host_<name>, <name> being the import's - the first argument of
the macro, which need not be the name applications call it by.
"""
import os
import re
import sys

WIDE = {"int64_t", "uint64_t"}
NARROW = {"int32_t", "uint32_t", "int", "bool"}


def letter(c_type, where):
    c_type = c_type.replace("const", " ").strip()
    if "*" in c_type:
        return "*"
    words = c_type.split()
    if not words:
        sys.exit("%s: a type is missing" % where)
    if words[0] in WIDE:
        return "I"
    if words[0] in NARROW:
        return "i"
    sys.exit("%s: the type \"%s\" has no place in a call of the host" % (where, c_type))


def rows(header_text, macro):
    found = []
    pattern = r"^%s\(\s*(\w+)\s*,\s*([^;]*?)\b(\w+)\s*\(([^;]*?)\)\s*\)\s*;" % macro
    for name, result, declared, parameters in re.findall(pattern, header_text, re.M | re.S):
        result = result.strip()
        signature = "v" if result == "void" else letter(result, name)
        signature += "("
        parameters = " ".join(parameters.split())
        if parameters != "void":
            for parameter in parameters.split(","):
                # The type is what stands before the parameter's name.
                c_type = re.sub(r"\b\w+\s*$", "", parameter.strip()) or parameter
                signature += letter(c_type, name)
        found.append((name, signature + ")"))
    return found


def main():
    header, out_dir = sys.argv[1], sys.argv[2]
    with open(header) as f:
        text = f.read()
    os.makedirs(out_dir, exist_ok=True)
    total = 0
    for macro, file_name in (("HOST_IMPORT", "host_imports.inc"),
                             ("HOST_SYSTEM_IMPORT", "system_imports.inc")):
        found = rows(text, macro)
        total += len(found)
        with open(os.path.join(out_dir, file_name), "w") as f:
            f.write("/* Made by tools/hostapi/gen_imports.py from sdk/host_api.h - do not edit. */\n")
            for name, signature in found:
                f.write("{ \"%s\", \"%s\", host_%s },\n" % (name, signature, name))
    if total < 60:
        sys.exit("%s: only %d calls found - the header is read wrong" % (header, total))


if __name__ == "__main__":
    main()
