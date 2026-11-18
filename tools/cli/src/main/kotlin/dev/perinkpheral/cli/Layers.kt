// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import java.io.File

/**
 * The firmware's layers, held by its includes and its calls (host tests run it).
 *
 * Each rule below is one CLAUDE.md states in words: the components know only the ones under
 * them and never Perinkpheral OS's own code; what is pure knows nothing of ESP-IDF; the views and the
 * scenes know no service; the settings' store knows none of the settings' consumers; the
 * services know no screen; and what other tasks change of the main event loop's state goes through
 * the main event loop. A rule broken is a line naming the file, the include or the call, and the
 * rule. Read by the text of the sources: an include is a line, and a function's body runs from
 * its head at the left margin to the next one.
 */
class Layers(firmwareFolder: File) {
    private val firmware = firmwareFolder.absoluteFile.normalize()
    private val components = File(firmware, "components")
    private val main = File(firmware, "main")
    private val sdk = File(firmware, "../sdk").normalize()

    /** The rules broken, and how many places were looked at. */
    fun check(): Pair<List<String>, Int> {
        val owner = owners()
        val paths = headerPaths()
        val broken = ArrayList<String>()
        var checks = 0
        fun rel(file: File) = file.relativeTo(firmware).path

        // The components: each knows only the ones under it, none Perinkpheral OS's code.
        for (component in components.listFiles().orEmpty().filter { it.isDirectory }.map { it.name }.sorted()) {
            val allowed = ALLOWED[component]
            if (allowed == null) {
                broken += "$component: a component with no place in the layers"
                continue
            }
            for (source in sourcesOf(component)) {
                for (header in includes(source)) {
                    val name = header.substringAfterLast('/')
                    val belongs = owner[name]
                    checks++
                    if (belongs == null || belongs == component) continue
                    if (belongs == "main") {
                        broken += "${rel(source)} includes $header: no component knows Perinkpheral OS's own code"
                    } else if (belongs !in allowed) {
                        broken += "${rel(source)} includes $header: $component does not know $belongs"
                    }
                }
            }
        }

        // The layers have no loop in them.
        fun reaches(start: String, target: String, seen: Set<String>): Boolean =
            ALLOWED[start].orEmpty().any { it == target || (it !in seen && reaches(it, target, seen + it)) }
        for (component in ALLOWED.keys) {
            checks++
            if (reaches(component, component, emptySet())) broken += "$component: the layers go round to it"
        }

        // What is pure knows nothing of ESP-IDF.
        for (source in PURE) {
            val file = File(firmware, source)
            if (!file.isFile) {
                broken += "$source: listed as pure and not there"
                continue
            }
            for ((header, through) in reached(file, paths)) {
                checks++
                if (SYSTEM.containsMatchIn(header)) {
                    broken += "$source includes $header${if (through != null) " (through $through)" else ""}: " +
                        "a pure module knows nothing of ESP-IDF"
                }
            }
        }

        // The views and the scenes know no service.
        for (view in VIEWS) {
            val file = File(main, "$view.c")
            if (!file.isFile) {
                broken += "main/$view.c: listed as a view and not there"
                continue
            }
            for (header in includes(file)) {
                checks++
                val name = header.substringAfterLast('/')
                val belongs = owner[name] ?: continue
                if (name in VIEW_HEADERS || belongs in VIEW_OWNERS) continue
                broken += "main/$view.c includes $header: a view or a scene knows no service"
            }
        }

        // The settings' store knows none of the settings' consumers.
        for (header in includes(File(firmware, STORE))) {
            checks++
            val name = header.substringAfterLast('/')
            if (owner[name] != null && name !in STORE_HEADERS) {
                broken += "$STORE includes $header: the store knows nothing of what a setting is for"
            }
        }

        // The services know no screen.
        for (service in SERVICES) {
            val file = File(main, "$service.c")
            if (!file.isFile) {
                broken += "main/$service.c: listed as a service and not there"
                continue
            }
            for (header in includes(file)) {
                checks++
                if (header.substringAfterLast('/') in SCREEN_HEADERS) broken += "main/$service.c includes $header: a service knows no screen"
            }
        }

        // What other tasks change of the main event loop's state, the main event loop does.
        for (source in OTHER_TASKS) {
            val file = File(firmware, source)
            if (!file.isFile) continue
            val text = file.readText()
            val bodies = functions(text)
            // A function runs only in the main event loop when it is handed to it, or when every
            // function of the file that calls it does. A callback handed to the main event loop and
            // also called straight from a handler runs in both.
            val handed = Regex("""events_call\w*\(\s*(\w+)""").findAll(text).map { it.groupValues[1] }.toSet()
            val callers = bodies.keys.associateWith { HashSet<String>() }
            for ((name, body) in bodies) {
                val inside = body.substring(body.indexOf('{').coerceAtLeast(0))
                for (called in Regex("""\b(\w+)\(""").findAll(inside).map { it.groupValues[1] }.toSet()) {
                    if (called in callers && called != name) callers.getValue(called) += name
                }
            }
            val inLoop = handed.filter { callers[it].isNullOrEmpty() }.toMutableSet()
            var grew = true
            while (grew) {
                grew = false
                for (name in bodies.keys) {
                    val by = callers.getValue(name)
                    if (name !in inLoop && by.isNotEmpty() && inLoop.containsAll(by)) {
                        inLoop += name
                        grew = true
                    }
                }
            }
            for ((name, body) in bodies) {
                for (call in HOST_LOOP_ONLY) {
                    if (Regex("""\b$call\(""").containsMatchIn(body)) {
                        checks++
                        if (name !in inLoop) broken += "$source: $name() calls $call() outside the main event loop"
                    }
                }
            }
        }
        return broken to checks
    }

    /** Every header of ours, by the part of the firmware it belongs to. */
    private fun owners(): Map<String, String> {
        val owner = HashMap<String, String>()
        for (component in components.listFiles().orEmpty().filter { it.isDirectory }.sortedBy { it.name }) {
            component.walk().filter { it.isFile && (it.name.endsWith(".h") || it.name.endsWith(".inc")) }
                .sortedBy { it.path }.forEach { owner.putIfAbsent(it.name, component.name) }
        }
        main.listFiles().orEmpty().filter { it.name.endsWith(".h") }.forEach { owner.putIfAbsent(it.name, "main") }
        for (folder in listOf(sdk, File(sdk, "lib"))) {
            folder.listFiles().orEmpty().filter { it.name.endsWith(".h") }.forEach { owner.putIfAbsent(it.name, "sdk") }
        }
        return owner
    }

    /** Every header of ours but the interpreter's, upstream code, by name. */
    private fun headerPaths(): Map<String, File> {
        val paths = HashMap<String, File>()
        for (root in listOf(components, main, sdk, File(firmware, "../recovery/main"))) {
            root.walk().filter { it.isFile && it.name.endsWith(".h") && !it.path.contains("components/wasm3") }
                .sortedBy { it.path }.forEach { paths.putIfAbsent(it.name, it) }
        }
        return paths
    }

    /** What a source includes, and what the headers of ours it includes do, each with the
     * header of ours it came through (null: the source). */
    private fun reached(source: File, paths: Map<String, File>): List<Pair<String, String?>> {
        val found = ArrayList<Pair<String, String?>>()
        val seen = HashSet<String>()
        val todo = ArrayDeque(listOf(source to null as String?))
        while (todo.isNotEmpty()) {
            val (current, through) = todo.removeLast()
            for (header in includes(current)) {
                val name = header.substringAfterLast('/')
                found += header to through
                val path = paths[name]
                if (path != null && seen.add(name)) todo += path to (through ?: name)
            }
        }
        return found
    }

    private fun sourcesOf(component: String): List<File> =
        File(components, component).walk().filter { file ->
            file.isFile && (file.name.endsWith(".c") || file.name.endsWith(".h")) && "qrcodegen" !in file.path &&
                !(component == "wasm3" && file.parentFile.name == "wasm3")
        }.toList()

    private fun includes(file: File): List<String> =
        Regex("""^\s*#\s*include\s+[<"]([^">]+)[">]""", RegexOption.MULTILINE).findAll(file.readText()).map { it.groupValues[1] }.toList()

    /** A function's name and its body, by the heads at the left margin. */
    private fun functions(text: String): Map<String, String> {
        val heads = Regex("""^[A-Za-z_][\w \t*\n]*?\b(\w+)\s*\([^;{}]*\)\s*\{""", RegexOption.MULTILINE).findAll(text).toList()
        val bodies = LinkedHashMap<String, String>()
        heads.forEachIndexed { i, head ->
            val end = if (i + 1 < heads.size) heads[i + 1].range.first else text.length
            bodies[head.groupValues[1]] = text.substring(head.range.first, end)
        }
        return bodies
    }

    companion object {
        /** Which components a component may know: the layers, from the bottom. The host's own
         * code (main) is above all of them and none may know it. */
        private val ALLOWED = mapOf(
            "board" to listOf(), "appconf" to listOf(), "words" to listOf(), "qr" to listOf(),
            "wasm3" to listOf(), "gfx" to listOf(),
            "deadlines" to listOf("board"), "input" to listOf("board"),
            "epd" to listOf("board", "deadlines"),
            // The network clock uses the supported time range of Perinkpheral OS contract.
            "net" to listOf("board", "deadlines", "sdk"),
            "ui" to listOf("gfx", "words"),
            "appstore" to listOf("board", "sdk"),
            "appsys" to listOf("appstore", "board", "gfx", "ui", "words", "qr", "wasm3", "sdk"),
        )

        /** Pure: built and tested on this computer, with nothing of ESP-IDF. */
        private val PURE = listOf(
            "../recovery/main/screen.c", "../recovery/main/control.h",
            "components/appconf/appconf.c", "components/words/words.c",
            "components/gfx/gfx.c", "components/gfx/gfx_icons.c", "components/gfx/font_square.c",
            "components/ui/wm.c", "components/ui/kit.c", "components/ui/list.c", "components/ui/notice.c",
            "components/ui/notice_queue.c", "components/ui/scrollbar.c",
            "components/epd/refresh_policy.c", "components/epd/frame_box.c",
            "components/deadlines/deadline_table.c", "components/input/btn_core.c",
            "components/net/wifi_plan.c", "components/net/wifi_credentials.c", "components/net/wifi_networks.c",
            "components/net/ota_stream.c", "components/net/ota_health.c", "components/net/captive_portal.c",
            "components/qr/wifi_qr.c",
            "components/appsys/app.c", "components/appsys/app_api_data.c", "components/appsys/app_api_draw.c",
            "components/appsys/app_api_sys.c", "components/appsys/app_api_time.c", "components/appsys/app_api_ui.c",
            "components/appsys/app_delivery.c", "components/appsys/app_scene.c",
            "components/appstore/app_manifest.c", "components/appstore/app_store.c", "components/appstore/app_room.c",
            "components/appstore/catalog.c", "components/appstore/catalog_stream.c", "components/appstore/app_update_schedule.c", "components/appstore/json_tree.c", "components/appstore/poll_schedule.c", "components/appstore/poll_round.c",
            "components/appstore/request_queue.c", "components/appstore/text_template.c", "components/appstore/app_update_policy.c",
            "main/screens.c", "main/header.c", "main/menu_order.c", "main/log_ring.c",
            "main/scene_menu.c", "main/scene_splash.c", "main/scene_confirm.c", "main/scene_working.c",
            "main/scene_storage.c", "main/scene_notify.c",
        )
        private val SYSTEM = Regex("""^(esp_|freertos/|driver/|nvs|lwip/|sdkconfig|hal/|soc/|rom/|riscv/|mdns)""")

        /** The views and the scenes: a model in, pixels or a reaction out. Of Perinkpheral OS's own
         * code they know only one another. */
        private val VIEWS = listOf("screens", "header", "menu_order", "scene_menu", "scene_splash", "scene_confirm",
            "scene_working", "scene_storage", "scene_notify")
        private val VIEW_HEADERS = setOf("screens.h", "header.h", "menu_order.h", "scenes.h")
        private val VIEW_OWNERS = setOf("gfx", "ui", "words", "qr", "board", "sdk")

        /** The settings' store knows nothing of what a setting is for. */
        private const val STORE = "main/settings.c"
        private val STORE_HEADERS = setOf("settings.h", "appconf.h", "catalog.h")

        /** The services: the data, with no idea that a screen exists. */
        private val SERVICES = listOf("status", "sysinfo", "power", "poller", "poller_net", "storage", "glyphs", "app_code",
            "app_kv", "memory", "settings", "timers", "events", "app_install", "app_update")
        private val SCREEN_HEADERS = setOf("gfx.h", "gfx_icons.h", "kit.h", "list.h", "notice.h", "wm.h", "scene.h",
            "scenes.h", "screens.h", "header.h")

        /** What only the main event loop may do: in the web server's handlers, these are called from a
         * function handed to the main event loop (events_call), or from one such a function calls. */
        private val HOST_LOOP_ONLY = listOf("host_reload_apps", "host_reload_apps_later", "launcher_remove", "host_close_app",
            "host_deliver", "app_kv_set", "host_redraw", "glyphs_install", "glyphs_remove",
            "app_store_commit_all", "app_store_commit_file", "settings_change", "settings_set",
            "settings_save_last_app", "settings_save_menu_order", "launcher_scene", "host_open_from_menu", "host_open_installed_app")
        private val OTHER_TASKS = listOf("main/web_server.c", "main/web_apps.c", "main/devtools.c", "main/app_install.c")
    }
}

/** The firmware's list of languages is the SDK's: sdk/languages.json is the one place, and
 * appconf.c repeats it for a device that has no file to read. Returns how many agree. */
fun checkLanguages(root: File): Int {
    val known = dev.perinkpheral.core.Json.parse(File(root, "sdk/languages.json").readText()) as Map<*, *>
    val wanted = (known["languages"] as List<*>).map { (it as Map<*, *>).let { language -> language["code"] as String to language["name"] as String } }
    val have = Regex("""\{ "([a-z]{2})",\s+"([A-Za-z]+)" \}""")
        .findAll(File(root, "firmware/components/appconf/appconf.c").readText()).map { it.groupValues[1] to it.groupValues[2] }.toList()
    if (wanted != have) {
        throw dev.perinkpheral.core.BuildError("appconf.c and sdk/languages.json name different languages:\n  $have\n  $wanted")
    }
    return wanted.size
}
