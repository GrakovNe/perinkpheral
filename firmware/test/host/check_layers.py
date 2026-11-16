#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The firmware's layers, held by its includes and its calls.

    check_layers.py <firmware folder>

Each rule below is one CLAUDE.md states in words: the components know only
the ones under them and never the host's own code; what is pure knows
nothing of ESP-IDF; the views and the scenes know no service; the settings'
store knows none of the settings' consumers; the services know no screen;
and what other tasks change of the host loop's state goes through the host
loop. A rule broken is a line naming the file, the include or the call, and
the rule. Read by the text of the sources: an include is a line, and a
function's body runs from its head at the left margin to the next one."""
import os
import re
import sys

FIRMWARE = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "../..")
COMPONENTS = os.path.join(FIRMWARE, "components")
MAIN = os.path.join(FIRMWARE, "main")
SDK = os.path.join(FIRMWARE, "..", "sdk")

# Which components a component may know: the layers, from the bottom. The
# host's own code (main) is above all of them and none may know it.
ALLOWED = {
    "board": [],
    "appconf": [],
    "words": [],
    "qr": [],
    "wasm3": [],
    "gfx": [],
    "deadlines": ["board"],
    "input": ["board"],
    "epd": ["board", "deadlines"],
    "net": ["board", "deadlines"],
    "ui": ["gfx", "words"],
    "appsys": ["board", "gfx", "ui", "words", "qr", "wasm3", "sdk"],
}

# Pure: built and tested on this computer, with nothing of ESP-IDF.
PURE = [
    "components/appconf/appconf.c",
    "components/words/words.c",
    "components/gfx/gfx.c",
    "components/gfx/gfx_icons.c",
    "components/gfx/font_square.c",
    "components/ui/wm.c",
    "components/ui/kit.c",
    "components/ui/list.c",
    "components/ui/notice.c",
    "components/ui/notice_queue.c",
    "components/ui/scrollbar.c",
    "components/epd/refresh_policy.c",
    "components/epd/frame_box.c",
    "components/deadlines/deadline_table.c",
    "components/input/btn_core.c",
    "components/net/wifi_plan.c",
    "components/net/ota_stream.c",
    "components/net/ota_health.c",
    "components/qr/wifi_qr.c",
    "components/appsys/app.c",
    "components/appsys/app_api_data.c",
    "components/appsys/app_api_draw.c",
    "components/appsys/app_api_sys.c",
    "components/appsys/app_api_time.c",
    "components/appsys/app_api_ui.c",
    "components/appsys/app_manifest.c",
    "components/appsys/app_store.c",
    "components/appsys/app_room.c",
    "components/appsys/app_delivery.c",
    "components/appsys/app_scene.c",
    "components/appsys/catalog.c",
    "components/appsys/json_tree.c",
    "components/appsys/poll_schedule.c",
    "components/appsys/request_queue.c",
    "components/appsys/text_template.c",
    "main/screens.c",
    "main/header.c",
    "main/menu_order.c",
    "main/log_ring.c",
    "main/scene_menu.c",
    "main/scene_splash.c",
    "main/scene_confirm.c",
    "main/scene_working.c",
    "main/scene_storage.c",
    "main/scene_notify.c",
]
SYSTEM = re.compile(r"^(esp_|freertos/|driver/|nvs|lwip/|sdkconfig|hal/|soc/|rom/|riscv/|mdns)")

# The views and the scenes: a model in, pixels or a reaction out. Of the
# host's own code they know only one another.
VIEWS = ["screens", "header", "menu_order", "scene_menu", "scene_splash", "scene_confirm",
         "scene_working", "scene_storage", "scene_notify"]
VIEW_HEADERS = {"screens.h", "header.h", "menu_order.h", "scenes.h"}
VIEW_OWNERS = {"gfx", "ui", "words", "qr", "board", "sdk"}

# The settings' store knows nothing of what a setting is for.
STORE = "main/settings.c"
STORE_HEADERS = {"settings.h", "appconf.h", "catalog.h"}

# The services: the data, with no idea that a screen exists.
SERVICES = ["status", "sysinfo", "power", "poller", "poller_net", "storage", "glyphs", "app_code",
            "app_kv", "memory", "settings", "timers", "events"]
SCREEN_HEADERS = {"gfx.h", "gfx_icons.h", "kit.h", "list.h", "notice.h", "wm.h", "scene.h",
                  "scenes.h", "screens.h", "header.h"}

# What only the host loop may do: in the web server's handlers, these are
# called from a function handed to the host loop (events_call), or from one
# such a function calls.
HOST_LOOP_ONLY = ["host_reload_apps", "host_reload_apps_later", "launcher_remove", "host_close_app",
                  "host_deliver", "app_kv_set", "host_redraw", "glyphs_install", "glyphs_remove",
                  "app_store_commit_all", "app_store_commit_file", "settings_change", "settings_set",
                  "settings_save_last_app", "settings_save_menu_order", "launcher_scene",
                  "host_open_from_menu"]
OTHER_TASKS = ["main/web_server.c", "main/web_apps.c", "main/devtools.c"]

INCLUDE = re.compile(r'^\s*#\s*include\s+[<"]([^">]+)[">]', re.M)


def owners():
    """Every header of ours, by the part of the firmware it belongs to."""
    owner = {}
    for name in sorted(os.listdir(COMPONENTS)):
        for folder, _, files in os.walk(os.path.join(COMPONENTS, name)):
            for file in files:
                if file.endswith((".h", ".inc")):
                    owner.setdefault(file, name)
    for file in os.listdir(MAIN):
        if file.endswith(".h"):
            owner.setdefault(file, "main")
    for folder in (SDK, os.path.join(SDK, "lib")):
        for file in os.listdir(folder):
            if file.endswith(".h"):
                owner.setdefault(file, "sdk")
    return owner


def includes(path):
    with open(path, encoding="utf-8") as f:
        return INCLUDE.findall(f.read())


def header_paths():
    """Every header of ours but the interpreter's, upstream code, by name."""
    paths = {}
    for root in (COMPONENTS, MAIN, SDK):
        for folder, _, files in os.walk(root):
            if os.path.join("components", "wasm3") in folder:
                continue
            for file in files:
                if file.endswith(".h"):
                    paths.setdefault(file, os.path.join(folder, file))
    return paths


def reached(path, paths):
    """What a source includes, and what the headers of ours it includes do,
    each with the header of ours it came through (None: the source)."""
    found, seen, todo = [], set(), [(path, None)]
    while todo:
        current, through = todo.pop()
        for header in includes(current):
            name = os.path.basename(header)
            found.append((header, through))
            if name in paths and name not in seen:
                seen.add(name)
                todo.append((paths[name], through or name))
    return found


def sources_of(component):
    found = []
    for folder, _, files in os.walk(os.path.join(COMPONENTS, component)):
        if "qrcodegen" in folder or (component == "wasm3" and folder.endswith("wasm3")):
            continue
        found += [os.path.join(folder, f) for f in files if f.endswith((".c", ".h"))]
    return found


def functions(text):
    """A function's name and its body, by the heads at the left margin."""
    heads = list(re.finditer(r"^[A-Za-z_][\w \t\*\n]*?\b(\w+)\s*\([^;{}]*\)\s*\{", text, re.M))
    for i, head in enumerate(heads):
        end = heads[i + 1].start() if i + 1 < len(heads) else len(text)
        yield head.group(1), text[head.start():end]


def main():
    owner = owners()
    paths = header_paths()
    broken = []
    checks = 0

    def rel(path):
        return os.path.relpath(path, FIRMWARE)

    # The components: each knows only the ones under it, none the host's code.
    for component in sorted(os.listdir(COMPONENTS)):
        if not os.path.isdir(os.path.join(COMPONENTS, component)):
            continue
        if component not in ALLOWED:
            broken.append("%s: a component with no place in the layers" % component)
            continue
        for path in sources_of(component):
            for header in includes(path):
                name = os.path.basename(header)
                belongs = owner.get(name)
                checks += 1
                if belongs is None or belongs == component:
                    continue
                if belongs == "main":
                    broken.append("%s includes %s: no component knows the host's own code" %
                                  (rel(path), header))
                elif belongs not in ALLOWED[component]:
                    broken.append("%s includes %s: %s does not know %s" %
                                  (rel(path), header, component, belongs))

    # The layers have no loop in them.
    def reaches(start, target, seen=()):
        return any(n == target or (n not in seen and reaches(n, target, seen + (n,)))
                   for n in ALLOWED.get(start, []))
    for component in ALLOWED:
        checks += 1
        if reaches(component, component):
            broken.append("%s: the layers go round to it" % component)

    # What is pure knows nothing of ESP-IDF.
    for source in PURE:
        path = os.path.join(FIRMWARE, source)
        if not os.path.exists(path):
            broken.append("%s: listed as pure and not there" % source)
            continue
        for header, through in reached(path, paths):
            checks += 1
            if SYSTEM.match(header):
                broken.append("%s includes %s%s: a pure module knows nothing of ESP-IDF" %
                              (source, header, " (through %s)" % through if through else ""))

    # The views and the scenes know no service.
    for view in VIEWS:
        path = os.path.join(MAIN, view + ".c")
        if not os.path.exists(path):
            broken.append("main/%s.c: listed as a view and not there" % view)
            continue
        for header in includes(path):
            checks += 1
            name = os.path.basename(header)
            belongs = owner.get(name)
            if belongs is None or name in VIEW_HEADERS or belongs in VIEW_OWNERS:
                continue
            broken.append("main/%s.c includes %s: a view or a scene knows no service" %
                          (view, header))

    # The settings' store knows none of the settings' consumers.
    for header in includes(os.path.join(FIRMWARE, STORE)):
        checks += 1
        name = os.path.basename(header)
        if owner.get(name) is not None and name not in STORE_HEADERS:
            broken.append("%s includes %s: the store knows nothing of what a setting is for" %
                          (STORE, header))

    # The services know no screen.
    for service in SERVICES:
        path = os.path.join(MAIN, service + ".c")
        if not os.path.exists(path):
            broken.append("main/%s.c: listed as a service and not there" % service)
            continue
        for header in includes(path):
            checks += 1
            if os.path.basename(header) in SCREEN_HEADERS:
                broken.append("main/%s.c includes %s: a service knows no screen" % (service, header))

    # What other tasks change of the host loop's state, the host loop does.
    for source in OTHER_TASKS:
        path = os.path.join(FIRMWARE, source)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            text = f.read()
        bodies = dict(functions(text))
        # A function runs only in the host loop when it is handed to it, or
        # when every function of the file that calls it does.
        handed = set(re.findall(r"events_call\w*\(\s*(\w+)", text))
        callers = {name: set() for name in bodies}
        for name, body in bodies.items():
            for called in set(re.findall(r"\b(\w+)\(", body[body.index("{"):])):
                if called in callers and called != name:
                    callers[called].add(name)
        # A callback handed to the host loop and also called straight from a
        # handler runs in both: it is the host loop's only when every direct
        # caller is too.
        in_loop = {name for name in handed if not callers.get(name)}
        grew = True
        while grew:
            grew = False
            for name in bodies:
                if name not in in_loop and callers[name] and callers[name] <= in_loop:
                    in_loop.add(name)
                    grew = True
        for name, body in bodies.items():
            for call in HOST_LOOP_ONLY:
                if re.search(r"\b%s\(" % call, body):
                    checks += 1
                    if name not in in_loop:
                        broken.append("%s: %s() calls %s() outside the host loop" %
                                      (source, name, call))

    for line in broken:
        print("layers: " + line)
    print("%d checks, %d failed" % (checks, len(broken)))
    sys.exit(1 if broken else 0)


main()
