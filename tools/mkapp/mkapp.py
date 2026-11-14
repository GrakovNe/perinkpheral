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
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SDK = os.path.join(ROOT, "sdk")



def manifest_limits():
    """The limits of a manifest, from the one place they are written."""
    limits = {}
    with open(os.path.join(SDK, "lib", "manifest_limits.h")) as f:
        for name, value in re.findall(r"^#define\s+MANIFEST_(\w+)\s+([^/\n]+)", f.read(), re.M):
            # Numbers and products of them, nothing else.
            if not re.fullmatch(r"[\d\s*()]+", value):
                sys.exit("sdk/lib/manifest_limits.h: %s is not a number" % name)
            limits[name] = eval(value)
    return limits


LIMITS = manifest_limits()
NAME_MAX = LIMITS["NAME_MAX"]
TITLE_MAX = LIMITS["TITLE_MAX"]
VERSION_MAX = LIMITS["VERSION_MAX"]
POLLS_MAX = LIMITS["POLLS_MAX"]
POLL_EVERY_MIN_S = LIMITS["POLL_EVERY_MIN_S"]
POLL_EVERY_MAX_S = LIMITS["POLL_EVERY_MAX_S"]
URL_MAX = LIMITS["URL_MAX"]
HEADERS_MAX = LIMITS["HEADERS_MAX"]
PICK_PATHS_MAX = LIMITS["PICK_PATHS_MAX"]
PICK_PATH_MAX = LIMITS["PICK_PATH_MAX"]
ICON_CELLS = LIMITS["ICON_CELLS"]
HOSTS_MAX = LIMITS["HOSTS_MAX"]
HOST_MAX = LIMITS["HOST_MAX"]
SETTINGS_MAX = LIMITS["SETTINGS_MAX"]
SETTING_KEY_MAX = LIMITS["SETTING_KEY_MAX"]
SETTING_TITLE_MAX = LIMITS["SETTING_TITLE_MAX"]
SETTING_HELP_MAX = LIMITS["SETTING_HELP_MAX"]
SETTING_OPTIONS_MAX = LIMITS["SETTING_OPTIONS_MAX"]
SETTING_OPTION_MAX = LIMITS["SETTING_OPTION_MAX"]
FILE_NAME_MAX = LIMITS["FILE_NAME_MAX"]
INTENTS = ("time_changed", "network_up", "started")
SETTING_TYPES = ("text", "secret", "number", "choice", "toggle", "place", "url", "link")
FILES_MAX_BYTES = LIMITS["FILES_MAX_BYTES"]
FILE_BLOCK_BYTES = LIMITS["FILE_BLOCK_BYTES"]


def taken_by(size):
    """What a file of so many bytes takes on the device: whole blocks."""
    return -(-size // FILE_BLOCK_BYTES) * FILE_BLOCK_BYTES
MODULE_MAX_BYTES = LIMITS["MODULE_MAX_BYTES"]
HEADER_NAME_MAX = LIMITS["HEADER_NAME_MAX"]
HEADER_VALUE_MAX = LIMITS["HEADER_VALUE_MAX"]

WASM_PAGE = 65536
APP_STACK = 8192
HOST_MODULE = "host"
SECTION_IMPORT = 2
SECTION_MEMORY = 5
IMPORT_KIND_FUNCTION = 0


def sdk_tool(name):
    sdk_path = os.environ.get("WASI_SDK_PATH")
    if not sdk_path:
        sys.exit("WASI_SDK_PATH is not set: point it at an unpacked wasi-sdk")
    path = os.path.join(sdk_path, "bin", name)
    if not os.path.exists(path):
        sys.exit("no %s in %s" % (name, os.path.join(sdk_path, "bin")))
    return path


MKPICTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mkpicture",
                         "mkpicture.py")


# ---- strings in the user's language ----

STRINGS_HEADER = 4  # the buffer's size and how many strings, 16 bits each
STRINGS_MAX_BYTES = 8 * 1024  # what the host takes: app.c says the same
BUILT_STRINGS = {}  # language code -> its table, for main() to put beside the module


def languages():
    """What the system knows: {code: the code points its glyph packs add to ASCII}."""
    with open(os.path.join(SDK, "languages.json")) as f:
        known = json.load(f)
    return {language["code"]: {cp for pack in language["packs"]
                               for first, last in known["packs"][pack]
                               for cp in range(first, last + 1)}
            for language in known["languages"]}


def strings_table(texts, capacity):
    offsets, blob = [], b""
    start = STRINGS_HEADER + 2 * len(texts)
    for text in texts:
        offsets.append(start + len(blob))
        blob += text.encode() + b"\0"
    return (capacity.to_bytes(2, "little") + len(texts).to_bytes(2, "little")
            + b"".join(offset.to_bytes(2, "little") for offset in offsets) + blob)


BUILT_MANIFESTS = {}  # language code -> the manifest's words in it, as JSON text


def translated_manifest(path, words, app_dir, glyphs):
    """The "manifest" of a translation, held to the manifest: a title, and for settings
    that exist their titles, help and the labels of options that exist."""
    with open(os.path.join(app_dir, "manifest.json")) as f:
        manifest = json.load(f)
    settings = {setting["key"]: setting for setting in manifest.get("settings", [])}

    def words_of(text, most, what):
        strange = sorted({c for c in text if not (" " <= c <= "~") and ord(c) not in glyphs}) \
            if isinstance(text, str) else []
        if not is_words(text, 1, most) or strange:
            sys.exit("%s: %s must be 1-%d characters of the language's own letters%s"
                     % (path, what, most,
                        ": " + " ".join("U+%04X" % ord(c) for c in strange) if strange else ""))
        return text

    if not isinstance(words, dict) or set(words) - {"title", "settings"}:
        sys.exit("%s: \"manifest\" has a \"title\" and \"settings\", nothing else" % path)
    out = {}
    if "title" in words:
        out["title"] = words_of(words["title"], TITLE_MAX, "the title")
    for key, said in (words.get("settings") or {}).items():
        if key not in settings:
            sys.exit("%s: the manifest has no setting \"%s\"" % (path, key))
        if not isinstance(said, dict) or set(said) - {"title", "help", "options"}:
            sys.exit("%s: setting \"%s\" has a \"title\", \"help\" and \"options\"" % (path, key))
        one = {}
        if "title" in said:
            one["title"] = words_of(said["title"], SETTING_TITLE_MAX, "the title of \"%s\"" % key)
        if "help" in said:
            one["help"] = words_of(said["help"], SETTING_HELP_MAX, "the help of \"%s\"" % key)
        if "options" in said:
            have = settings[key].get("options", [])
            if not isinstance(said["options"], dict) or set(said["options"]) - set(have):
                sys.exit("%s: setting \"%s\": labels are for the options the manifest has: %s"
                         % (path, key, ", ".join(have) or "none"))
            one["options"] = {option: words_of(label, SETTING_OPTION_MAX,
                                               "the label of \"%s\"" % option)
                              for option, label in said["options"].items()}
        out.setdefault("settings", {})[key] = one
    return json.dumps(out, ensure_ascii=False, separators=(",", ":"))


def make_strings(app_dir, scratch):
    """An application's `strings` folder as strings.h and strings.c for the build, and a
    table for every language but English. Returns the generated source, or None."""
    folder = os.path.join(app_dir, "strings")
    if not os.path.isdir(folder):
        return None
    known = languages()
    translations = {}
    BUILT_MANIFESTS.clear()
    for name in sorted(os.listdir(folder)):
        if name.startswith(".") or not name.endswith(".json"):
            continue
        code, path = name[:-5], os.path.join(folder, name)
        if code not in known:
            sys.exit("%s: the system does not know the language \"%s\" (sdk/languages.json)"
                     % (path, code))
        try:
            with open(path) as f:
                texts = json.load(f, object_pairs_hook=unique_keys)
        except ValueError as error:
            sys.exit("%s: %s" % (path, error))
        if not isinstance(texts, dict):
            sys.exit("%s: an object of keys and texts" % path)
        # What of the manifest a person reads, in this language: not a string of
        # the code, and checked against the manifest itself.
        manifest_words = texts.pop("manifest", None)
        if manifest_words is not None:
            if code == "en":
                sys.exit("%s: English is the manifest itself: no \"manifest\" here" % path)
            BUILT_MANIFESTS[code] = translated_manifest(path, manifest_words, app_dir,
                                                        known[code])
        if any(not isinstance(text, str) for text in texts.values()):
            sys.exit("%s: an object of keys and texts" % path)
        for key, text in texts.items():
            strange = sorted({c for c in text if not (" " <= c <= "~") and ord(c) not in known[code]
                              and c != "\u00b0"})
            if strange:
                sys.exit("%s: \"%s\" has %s, which the glyphs of \"%s\" do not have"
                         % (path, key, " ".join("U+%04X" % ord(c) for c in strange), code))
        translations[code] = texts
    if "en" not in translations:
        sys.exit("%s: en.json is the language every application speaks: it is missing" % folder)
    keys = list(translations["en"])
    for key in keys:
        if not re.fullmatch(r"[a-z][a-z0-9_]*", key):
            sys.exit("%s/en.json: a key is a-z, 0-9 and '_', beginning with a letter: \"%s\""
                     % (folder, key))
    unused_names(app_dir, ["STR_" + key.upper() for key in keys], "strings")
    for code, texts in translations.items():
        missing, extra = [k for k in keys if k not in texts], [k for k in texts if k not in keys]
        if missing or extra:
            sys.exit("%s/%s.json: %s" % (folder, code, "; ".join(
                part for part in ("lacks " + ", ".join(missing) if missing else "",
                                  "has " + ", ".join(extra) + ", which en.json has not" if extra else "")
                if part)))
    # One buffer holds any of them: as large as the largest, to a multiple of four.
    sizes = [len(strings_table([texts[key] for key in keys], 0)) for texts in translations.values()]
    capacity = (max(sizes) + 3) // 4 * 4
    if capacity > STRINGS_MAX_BYTES:
        sys.exit("%s: %d bytes of strings in one language, and the device takes %d"
                 % (folder, capacity, STRINGS_MAX_BYTES))
    tables = {code: strings_table([texts[key] for key in keys], capacity)
              for code, texts in translations.items()}
    english = tables.pop("en")
    BUILT_STRINGS.clear()
    BUILT_STRINGS.update(tables)
    with open(os.path.join(scratch, "strings.h"), "w") as f:
        f.write("/* Generated by tools/mkapp from the `strings` folder: do not edit. */\n#pragma once\n\n")
        f.write("#include <stdint.h>\n\nenum {\n")
        f.write("".join("    STR_%s,\n" % key.upper() for key in keys))
        f.write("    STR_COUNT,\n};\n\n#define APP_STRINGS_BYTES %d\nextern uint8_t app_strings[APP_STRINGS_BYTES];\n\n"
                % capacity)
        f.write("/* The text of a string in the language the device is set to. */\n"
                "static inline const char *app_string(int32_t id)\n{\n"
                "    return (const char *)app_strings + (app_strings[4 + 2 * id] | app_strings[5 + 2 * id] << 8);\n}\n")
    source = os.path.join(scratch, "strings.c")
    with open(source, "w") as f:
        f.write("/* Generated by tools/mkapp: English, until the host puts another language here. */\n")
        f.write('#include "strings.h"\n\nuint8_t app_strings[APP_STRINGS_BYTES] = {\n')
        for start in range(0, len(english), 16):
            f.write("    " + " ".join("0x%02x," % byte for byte in english[start:start + 16]) + "\n")
        f.write("};\n\n__attribute__((export_name(\"app_strings_buffer\"))) uint8_t *app_strings_buffer(void)\n"
                "{\n    return app_strings;\n}\n")
    return source


def clang():
    return sdk_tool("clang")


def make_pictures(app_dir, scratch):
    """The drawings of the `pictures` folder, as pictures.h for the code. Says whether
    there were any."""
    pictures_dir = os.path.join(app_dir, "pictures")
    if not os.path.isdir(pictures_dir):
        return False
    made = subprocess.run([sys.executable, MKPICTURE, pictures_dir, "--out",
                           os.path.join(scratch, "pictures.h")], capture_output=True, text=True)
    if made.returncode != 0:
        sys.exit(made.stderr.strip() or "%s: no pictures made" % pictures_dir)
    with open(os.path.join(scratch, "pictures.h")) as f:
        unused_names(app_dir, re.findall(r"\b(PICTURE_[A-Z0-9_]+)\[\]", f.read()), "pictures")
    return True


def build(app_dir, wasm_path):
    sources = sorted(glob.glob(os.path.join(app_dir, "*.c")))
    if not sources:
        sys.exit("no .c files in %s" % app_dir)
    sources += sorted(glob.glob(os.path.join(SDK, "lib", "*.c")))
    command = [
        clang(), "--target=wasm32", "-nostdlib", "-ffreestanding", "-fno-builtin",
        "-Oz", "-flto", "-Wall", "-Wextra", "-Werror",
        "-I" + SDK, "-I" + os.path.join(SDK, "lib"),
        "-Wl,--no-entry", "-Wl,--strip-all", "-Wl,--gc-sections",
        "-Wl,--initial-memory=%d" % WASM_PAGE, "-Wl,--max-memory=%d" % WASM_PAGE,
        "-Wl,-z,stack-size=%d" % APP_STACK,
        "-o", wasm_path,
    ] + sources
    # The linker's map says where the module's data ends in its memory: the
    # binary itself does not, for what starts as zeroes is not written out.
    with tempfile.TemporaryDirectory() as scratch:
        if make_pictures(app_dir, scratch):
            command.append("-I" + scratch)
        strings_source = make_strings(app_dir, scratch)
        if strings_source is not None:
            command += [strings_source, "-I" + scratch]
        map_path = os.path.join(scratch, "app.map")
        if subprocess.run(command + ["-Wl,-Map=" + map_path]).returncode != 0:
            sys.exit("%s did not compile" % app_dir)
        with open(map_path) as f:
            placed = re.findall(r"^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\.\w+\s*$",
                                f.read(), re.M)
    return max((int(start, 16) + int(size, 16) for start, size in placed), default=0)


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


SECTION_EXPORT = 7
EXPORT_KIND_FUNCTION = 0


def exported_functions(wasm_path):
    """The names of the functions the module exports."""
    with open(wasm_path, "rb") as f:
        reader = Reader(f.read())
    reader.at = 8
    names = []
    while reader.at < len(reader.data):
        section = reader.byte()
        section_end = reader.unsigned() + reader.at
        if section == SECTION_EXPORT:
            for _ in range(reader.unsigned()):
                name = reader.name()
                kind = reader.byte()
                reader.unsigned()
                if kind == EXPORT_KIND_FUNCTION:
                    names.append(name)
        reader.at = section_end
    return names


def known_handlers():
    """The exports the host calls, as sdk/host_api.h names them."""
    with open(os.path.join(SDK, "host_api.h")) as f:
        return set(re.findall(r"\bapp_(?:on_[a-z]+|init|data_buffer)\b", f.read()))


# A call that asks for something to come later, and the handler it comes to.
ANSWERED_BY = {
    "timer_set": "app_on_timer",
    "alarm_set": "app_on_alarm",
    "alarm_set_at": "app_on_alarm",
    "alarm_set_every": "app_on_alarm",
    "http_request": "app_on_data",
}


def misspelled_handlers(app_dir):
    """A function named like a handler that is none: the build drops it without a word."""
    known = known_handlers()
    for path in sorted(glob.glob(os.path.join(app_dir, "*.c"))):
        with open(path) as f:
            for name in re.findall(r"^[A-Za-z_][\w \t\*]*?\b(app_on_\w+)\s*\(", f.read(), re.M):
                if name not in known:
                    sys.exit("%s: %s() is not a handler the host calls. Its handlers: %s"
                             % (path, name, ", ".join(sorted(known))))


def check_handlers(wasm_path, imports, manifest):
    """What the module asks for has to have somewhere to arrive: a request without
    its handler, a poll nobody takes and a handler's name misspelled are mistakes
    the device would only show as a screen that never changes."""
    exports = set(exported_functions(wasm_path))
    known = known_handlers()
    for name in sorted(exports):
        if name.startswith("app_") and name not in known and name != "app_strings_buffer":
            sys.exit("%s exports %s(), which the host never calls: its handlers are %s"
                     % (wasm_path, name, ", ".join(sorted(known))))
    called = {name for module, name in imports if module == HOST_MODULE}
    for call, handler in sorted(ANSWERED_BY.items()):
        if call in called and handler not in exports:
            sys.exit("%s calls %s() and has no %s() for what comes of it"
                     % (wasm_path, call, handler))
    for number, poll in enumerate(manifest.get("polls", [])):
        handler = "app_on_values" if poll.get("pick") else "app_on_data"
        if handler not in exports:
            sys.exit("%s: poll %d of the manifest %s, and the module has no %s()"
                     % (wasm_path, number,
                        "has \"pick\"" if poll.get("pick") else "has no \"pick\"", handler))
    if "app_on_data" in exports and "app_data_buffer" not in exports:
        sys.exit("%s has app_on_data() and no app_data_buffer(): link sdk/lib/app_data.c or "
                 "export a buffer of HOST_DATA_CHUNK_MAX bytes" % wasm_path)
    if manifest.get("intents") and "app_on_intent" not in exports:
        sys.exit("%s: the manifest asks for intents and the module has no app_on_intent()"
                 % wasm_path)
    if any(name.startswith(("gfx_", "ui_")) for name in called) and "app_on_draw" not in exports:
        sys.exit("%s draws and has no app_on_draw(): drawing calls work only there" % wasm_path)


def unused_names(app_dir, names, what):
    """Strings and pictures the code never names are bytes of the module for nothing."""
    code = ""
    for path in sorted(glob.glob(os.path.join(app_dir, "*.[ch]"))):
        with open(path) as f:
            code += f.read()
    # One of a numbered row - STR_DAY_0, STR_DAY_1 - is used when the row is: the code
    # may reach the others by counting from the first.
    def is_used(name):
        row = re.sub(r"\d+$", "", name)
        return re.search(r"\b%s\b" % re.escape(name), code) or (
            row != name and re.search(r"\b%s\d+\b" % re.escape(row), code))
    unused = [name for name in names if not is_used(name)]
    if unused:
        sys.exit("%s: %s never used in the code: %s. Remove them - each is bytes of the "
                 "module, and of every translation" % (app_dir, what, ", ".join(unused)))


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


SECTION_CODE = 10
# What an application's code costs when it runs, as measured on the device
# (`app_area_used` in /api/status, less the 64 KB page): the interpreter
# needs about this much for any application, and about four bytes for every
# byte of the module's code section - 12 KB for the sample, 26 for the
# weather, 28 for HA Companion, 35 for Settings.
INTERPRETER_BASE_BYTES = 11 * 1024
INTERPRETER_BYTES_PER_CODE_BYTE = 4
INTERPRETER_ROOM_BYTES = 64 * 1024


def measure(wasm_path):
    """Returns the bytes of code in the module."""
    with open(wasm_path, "rb") as f:
        reader = Reader(f.read())
    reader.at = 8
    code = 0
    while reader.at < len(reader.data):
        section = reader.byte()
        size = reader.unsigned()
        section_end = reader.at + size
        if section == SECTION_CODE:
            code = size
        reader.at = section_end
    return code


def report(wasm_path, import_count, files_bytes, memory_end):
    """What the build came to, against what the device has for it."""
    def kb(count):
        return "%.1f KB" % (count / 1024)
    module = os.path.getsize(wasm_path)
    code = measure(wasm_path)
    running = INTERPRETER_BASE_BYTES + INTERPRETER_BYTES_PER_CODE_BYTE * code
    print("%s: %d bytes, %d host calls" % (wasm_path, module, import_count))
    print("  module  %s of %s" % (kb(module), kb(MODULE_MAX_BYTES)))
    print("  code    %s; running it takes about %s of the interpreter's %s"
          % (kb(code), kb(running), kb(INTERPRETER_ROOM_BYTES)))
    if memory_end > 0:
        print("  memory  %s of %s: %s of stack, %s of data; %s left for app_alloc()"
              % (kb(memory_end), kb(WASM_PAGE), kb(APP_STACK), kb(memory_end - APP_STACK),
                 kb(WASM_PAGE - memory_end)))
    if files_bytes > 0:
        print("  files   %s of %s" % (kb(files_bytes), kb(FILES_MAX_BYTES)))
    if running > INTERPRETER_ROOM_BYTES:
        print("  warning: this much code may not fit when all of it runs", file=sys.stderr)


def host_imports(macro="HOST_IMPORT"):
    """The names host_api.h declares, read from the header itself."""
    names = set()
    with open(os.path.join(SDK, "host_api.h")) as f:
        for line in f:
            if line.startswith(macro + "("):
                names.add(line[len(macro) + 1:line.index(",")])
    return names


def float_operations(wasm_path):
    """The floating point operations in the module's code, as the disassembler names them."""
    listing = subprocess.run([sdk_tool("llvm-objdump"), "-d", wasm_path],
                             capture_output=True, text=True, check=True).stdout
    # The disassembler prints the plain "select" as "f32.select"; that one is not a
    # float operation, whatever its name.
    operations = re.findall(r"\b(?:f(?:32|64)\.(?!select\b)\w+|[\w.]+_f(?:32|64)\w*)", listing)
    return sorted(set(operations))


def check(wasm_path, asks_for_system):
    imports, memory = inspect(wasm_path)
    floats = float_operations(wasm_path)
    if floats:
        sys.exit("%s uses floating point (%s). The device's interpreter has none, and the "
                 "application would stop there: keep numbers as integers - tenths, "
                 "hundredths - the way sdk/lib/app_values.h reads them"
                 % (wasm_path, ", ".join(floats[:4])))
    offered, system = host_imports(), host_imports("HOST_SYSTEM_IMPORT")
    for module, name in imports:
        if module == HOST_MODULE and name in system and not asks_for_system:
            sys.exit("the module imports %s, which is the system API: an application that "
                     "wants it says \"system\": true in its manifest" % name)
        if module != HOST_MODULE or name not in offered | system:
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


def is_words(text, least, most):
    """A text a person reads: so many characters, and no more bytes than letters of
    two bytes would take - the device's rule (app_manifest.c)."""
    return (isinstance(text, str) and least <= len(text) <= most
            and len(text.encode()) <= most * LIMITS["TEXT_BYTES_PER_CHARACTER"])


def unique_keys(pairs):
    """For json.load: the device reads the first of two equal keys, Python the last."""
    keys = [key for key, _ in pairs]
    for key in keys:
        if keys.count(key) > 1:
            raise ValueError("\"%s\" is there more than once" % key)
    return dict(pairs)


def references_server(text, servers):
    """Whether the text begins with {{key}} of a setting of the "url" type: the server
    the user named. Returns what follows it, or None."""
    for key in servers:
        if text.startswith("{{%s}}" % key):
            return text[len(key) + 4:]
    return None


def pace_seconds(text):
    """A pace as a person writes it, in seconds; 0 when the text is not one."""
    found = re.fullmatch(r"(\d{1,6}) *(?:([smhd])[a-z]*)?", text if isinstance(text, str) else "")
    if not found:
        return 0
    return int(found.group(1)) * {None: 1, "s": 1, "m": 60, "h": 3600, "d": 86400}[found.group(2)]


def check_poll(path, number, poll, servers, settings):
    where = "%s: poll %d" % (path, number)
    if not isinstance(poll, dict):
        sys.exit("%s must be an object" % where)
    url = poll.get("url")
    if (not isinstance(url, str) or len(url.encode()) > URL_MAX
            or not (url.startswith(("http://", "https://"))
                    or references_server(url, servers) is not None)):
        sys.exit("%s: \"url\" must be an http(s) URL of at most %d bytes, or begin with "
                 "{{key}} of a setting of the \"url\" or \"link\" type" % (where, URL_MAX))
    every = poll.get("every")
    if isinstance(every, str):
        # "{{key}}": the pace is the user's choice in that setting.
        named = [setting for setting in settings if every == "{{%s}}" % setting.get("key")]
        if not named:
            sys.exit("%s: \"every\" as text must be {{key}} of a setting" % where)
        for value in named[0].get("options", []) + [named[0].get("default", "1 min")]:
            if not POLL_EVERY_MIN_S <= pace_seconds(value) <= POLL_EVERY_MAX_S:
                sys.exit("%s: \"%s\" of the setting \"%s\" is not a pace of %d seconds to a "
                         "day: write \"15 min\", \"1 h\", \"30 s\""
                         % (where, value, named[0]["key"], POLL_EVERY_MIN_S))
    elif (isinstance(every, bool) or not isinstance(every, (int, float))
            or not POLL_EVERY_MIN_S <= every <= POLL_EVERY_MAX_S):
        sys.exit("%s: \"every\" must be %d to %d seconds, or {{key}} of a setting"
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


def check_settings(path, settings):
    if not isinstance(settings, list) or len(settings) > SETTINGS_MAX:
        sys.exit("%s: \"settings\" must be a list of at most %d" % (path, SETTINGS_MAX))
    keys = set()
    for number, setting in enumerate(settings):
        where = "%s: setting %d" % (path, number)
        if not isinstance(setting, dict):
            sys.exit("%s must be an object" % where)
        key, title, kind = setting.get("key"), setting.get("title"), setting.get("type")
        if (not isinstance(key, str) or not 0 < len(key) <= SETTING_KEY_MAX
                or any(not "!" <= character <= "~" for character in key)):
            sys.exit("%s: \"key\" must be 1-%d characters from '!' to '~'" % (where, SETTING_KEY_MAX))
        if key in keys:
            sys.exit("%s: the key \"%s\" is used twice" % (where, key))
        keys.add(key)
        if not is_words(title, 1, SETTING_TITLE_MAX):
            sys.exit("%s: \"title\" must be 1-%d characters" % (where, SETTING_TITLE_MAX))
        if kind not in SETTING_TYPES:
            sys.exit("%s: \"type\" must be one of %s" % (where, ", ".join(SETTING_TYPES)))
        help_text = setting.get("help", "")
        if not is_words(help_text, 0, SETTING_HELP_MAX):
            sys.exit("%s: \"help\" must be a string of at most %d characters" % (where, SETTING_HELP_MAX))
        options = setting.get("options")
        if kind != "choice":
            if options is not None:
                sys.exit("%s: only a choice has \"options\"" % where)
        elif (not isinstance(options, list) or not 0 < len(options) <= SETTING_OPTIONS_MAX
              or any(not isinstance(option, str)
                     or not is_words(option, 1, SETTING_OPTION_MAX) for option in options)):
            sys.exit("%s: \"options\" must be 1-%d strings of 1-%d characters"
                     % (where, SETTING_OPTIONS_MAX, SETTING_OPTION_MAX))
    for number, setting in enumerate(settings):
        default, kind = setting.get("default"), setting["type"]
        if default is None:
            continue
        fits = (isinstance(default, str) and default and kind not in ("secret", "place")
                and (kind != "toggle" or default in ("0", "1"))
                and (kind != "choice" or default in setting["options"])
                and (kind != "number" or re.fullmatch(r"-?(\d+\.?\d*|\.\d+)", default)))
        if not fits:
            sys.exit("%s: setting %d: \"default\" must be a value the setting takes" % (path, number))
    numbers = {setting["key"] for setting in settings if setting["type"] == "number"}
    for number, setting in enumerate(settings):
        if setting["type"] != "place":
            continue
        latitude, longitude = setting.get("latitude"), setting.get("longitude")
        if latitude == longitude or latitude not in numbers or longitude not in numbers:
            sys.exit("%s: setting %d: a place names two number settings as its "
                     "\"latitude\" and \"longitude\"" % (path, number))


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
    if not is_words(title, 1, TITLE_MAX):
        sys.exit("%s: \"title\" must be 1-%d characters" % (path, TITLE_MAX))
    version = manifest.get("version", "")
    if not isinstance(version, str) or len(version.encode()) > VERSION_MAX:
        sys.exit("%s: \"version\" must be a string of at most %d bytes" % (path, VERSION_MAX))
    companion = manifest.get("companion")
    if companion is not None and not (
            isinstance(companion, str) and len(companion) <= URL_MAX and
            re.fullmatch(r"https://[!#-;=?-~]+", companion)):
        sys.exit("%s: \"companion\" must be an https address of at most %d bytes"
                 % (path, URL_MAX))
    if not isinstance(manifest.get("fullscreen", False), bool):
        sys.exit("%s: \"fullscreen\" must be true or false" % path)
    if not isinstance(manifest.get("system", False), bool):
        sys.exit("%s: \"system\" must be true or false" % path)
    icon = manifest.get("icon")
    if icon is not None and (not isinstance(icon, list) or len(icon) != ICON_CELLS
                             or any(not isinstance(row, str)
                                    or not re.fullmatch(r"[#.]{%d}" % ICON_CELLS, row)
                                    for row in icon)):
        sys.exit("%s: \"icon\" must be %d rows of %d characters, '#' for ink and '.' for paper"
                 % (path, ICON_CELLS, ICON_CELLS))
    # The settings first: a host and a poll may name the server one of them holds.
    check_settings(path, manifest.get("settings", []))
    servers = [setting["key"] for setting in manifest.get("settings", [])
               if setting["type"] in ("url", "link")]
    links = [setting["key"] for setting in manifest.get("settings", [])
             if setting["type"] == "link"]
    hosts = manifest.get("hosts", [])
    if (not isinstance(hosts, list) or len(hosts) > HOSTS_MAX
            or any(not isinstance(host, str) or not 0 < len(host.encode()) <= HOST_MAX
                   or (host.startswith("{{") and references_server(host, servers) != "")
                   for host in hosts)):
        sys.exit("%s: \"hosts\" must be a list of at most %d host names of 1-%d bytes, "
                 "or {{key}} of a setting of the \"url\" or \"link\" type" % (path, HOSTS_MAX, HOST_MAX))
    intents = manifest.get("intents", [])
    if not isinstance(intents, list) or any(intent not in INTENTS for intent in intents):
        sys.exit("%s: \"intents\" must be a list of %s" % (path, ", ".join(INTENTS)))
    polls = manifest.get("polls", [])
    if not isinstance(polls, list) or len(polls) > POLLS_MAX:
        sys.exit("%s: \"polls\" must be a list of at most %d" % (path, POLLS_MAX))
    for number, poll in enumerate(polls):
        check_poll(path, number, poll, servers, manifest.get("settings", []))
        # A link is the whole address: nothing stands after it.
        if isinstance(poll, dict) and references_server(poll.get("url", ""), links) not in (None, ""):
            sys.exit("%s: poll %d: a setting of the \"link\" type is the whole \"url\""
                     % (path, number))
    abi = manifest.get("abi")
    # True == 1 in Python, and the device wants a number.
    if isinstance(abi, bool) or abi != host_api_version():
        sys.exit("%s: \"abi\" must be %d, the version of sdk/host_api.h"
                 % (path, host_api_version()))
    return path


def copy_data_files(app_dir, out_dir):
    """What is in the application's `files` folder goes onto the device beside the module."""
    files_dir = os.path.join(app_dir, "files")
    if not os.path.isdir(files_dir):
        return 0
    total = 0
    for name in sorted(os.listdir(files_dir)):
        path = os.path.join(files_dir, name)
        # What a file manager leaves behind, and folders, are not the application's.
        if name.startswith(".") or not os.path.isfile(path):
            continue
        if name in ("app.wasm", "manifest.json"):
            sys.exit("%s: this name is the application's own" % path)
        if not re.fullmatch(r"[a-z0-9_-][a-z0-9._-]{0,%d}" % (FILE_NAME_MAX - 1), name):
            sys.exit("%s: a file's name is 1-%d of a-z, 0-9, '.', '-' and '_', not starting "
                     "with a dot" % (path, FILE_NAME_MAX))
        total += taken_by(os.path.getsize(path))
        shutil.copy(path, os.path.join(out_dir, name))
    return total


def main():
    args = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
    args.add_argument("app_dir")
    args.add_argument("--out", help="directory for app.wasm")
    args.add_argument("--headers", help="only write strings.h, strings.c and pictures.h there: "
                                        "what an editor needs to read the code")
    options = args.parse_args()
    if options.headers:
        os.makedirs(options.headers, exist_ok=True)
        make_pictures(options.app_dir, options.headers)
        make_strings(options.app_dir, options.headers)
        return
    if not options.out:
        args.error("--out is required")

    manifest_path = check_manifest(options.app_dir)
    os.makedirs(options.out, exist_ok=True)
    wasm_path = os.path.join(options.out, "app.wasm")
    memory_end = build(options.app_dir, wasm_path)
    with open(manifest_path) as f:
        manifest = json.load(f)
    imports = check(wasm_path, manifest.get("system", False))
    # A module that fails a check is not left behind: a build that stopped here must
    # not look done to whoever only asks whether app.wasm is there.
    built = wasm_path
    try:
        misspelled_handlers(options.app_dir)
        check_handlers(wasm_path, imports, manifest)
        built = None
    finally:
        if built is not None and os.path.exists(built):
            os.remove(built)
    module_bytes = os.path.getsize(wasm_path)
    if module_bytes > MODULE_MAX_BYTES:
        sys.exit("%s: the module is %d bytes, at most %d fit on the device"
                 % (wasm_path, module_bytes, MODULE_MAX_BYTES))
    shutil.copy(manifest_path, os.path.join(options.out, "manifest.json"))
    files_bytes = copy_data_files(options.app_dir, options.out)
    # The tables of the languages it speaks beside English, as files of its own.
    for code, table in sorted(BUILT_STRINGS.items()):
        with open(os.path.join(options.out, "strings." + code), "wb") as f:
            f.write(table)
        files_bytes += taken_by(len(table))
    for code, words in sorted(BUILT_MANIFESTS.items()):
        with open(os.path.join(options.out, "manifest." + code), "w") as f:
            f.write(words)
        files_bytes += taken_by(len(words.encode()))
    if files_bytes > FILES_MAX_BYTES:
        sys.exit("%s: its files take %d bytes on the device - each a whole number of %d-byte "
                 "blocks - and an application may have %d"
                 % (options.app_dir, files_bytes, FILE_BLOCK_BYTES, FILES_MAX_BYTES))
    report(wasm_path, len(imports), files_bytes, memory_end)


if __name__ == "__main__":
    main()
