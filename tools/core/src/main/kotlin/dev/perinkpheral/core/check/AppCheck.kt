// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.check

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Built
import dev.perinkpheral.core.build.BuildFlags
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.build.Wasm
import java.io.File
import java.nio.file.Files
import java.time.Instant
import java.time.ZoneId
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * Checks a folder of applications against what the contract promises of any application.
 *
 * Every application's C is compiled for this computer, with the sanitizers, against the
 * SDK's stand-in host (`sdk/check/standin.c`) and run by the capability checks
 * (`sdk/check/checks.c`): sessions, store, timers, network, drawing. Which of them apply to
 * an application is read off its module's imports and exports and its manifest. Nothing
 * here or there knows an application by name.
 *
 * What only an application knows is kept beside it, in its `test` folder, and found there:
 *
 *     test/check.json   when and with what store it is checked, and the answers to its
 *                       requests - the keys of a simulator scenario: "at", "tz", "store",
 *                       "mocks" (an object, or the name of a file beside it)
 *     test/<any>.c      tests of its own logic: each a program, compiled with the same flags
 *                       and the stand-in host, run in that folder; it passes when it exits 0
 */
class AppCheck(private val sdk: Sdk, private val wasiSdk: File, private val say: (String) -> Unit) {

    /** One line of the report: an application and a capability, or one of its own tests. */
    class Line(val app: String, val what: String, val state: State, val details: List<String>)

    enum class State(val word: String) { OK("ok"), FAILED("FAIL"), NOT_APPLICABLE("-") }

    class Report(val lines: List<Line>, val applications: Int) {
        val failed: List<Line> get() = lines.filter { it.state == State.FAILED }
        val checked: Int get() = lines.count { it.state != State.NOT_APPLICABLE }
    }

    private val checkDir = File(sdk.sdk, "check")
    private val compiler = System.getenv("CC")?.takeIf { it.isNotBlank() } ?: "cc"

    /**
     * Checks every application under `folder` (or the one `folder` is), or only the one
     * named; with `only`, that capability alone. Says a line a result as each application
     * is done, and returns them all.
     */
    fun run(folder: File, only: String? = null, onlyApp: String? = null): Report {
        if (only != null && only !in CAPABILITIES + OWN_TESTS) {
            throw BuildError("no capability \"$only\": ${(CAPABILITIES + OWN_TESTS).joinToString(", ")}")
        }
        val apps = applications(folder).filter { onlyApp == null || it.name == onlyApp }
        if (apps.isEmpty()) {
            throw BuildError(if (onlyApp != null) "${folder.path}: no application \"$onlyApp\" in it"
                else "${folder.path}: no application - a folder with a manifest.json - in it")
        }
        val scratch = Files.createTempDirectory("perinkpheral-app-check").toFile()
        try {
            val host = hostObjects(File(scratch, "host"))
            // An application is a few seconds of one processor: as many at once as there are.
            val pool = Executors.newFixedThreadPool(Runtime.getRuntime().availableProcessors().coerceIn(1, apps.size))
            try {
                val waiting = apps.map { app -> pool.submit(Callable { checkOne(app, File(scratch, app.name), host, only) }) }
                val lines = waiting.flatMap { one -> one.get().also { it.forEach(::tell) } }
                return Report(lines, apps.size)
            } finally {
                pool.shutdownNow()
            }
        } finally {
            scratch.deleteRecursively()
        }
    }

    private fun tell(line: Line) {
        say("%-4s %-13s %s".format(line.state.word, line.app, line.what))
        line.details.forEach { say("       $it") }
    }

    private fun applications(folder: File): List<File> =
        if (File(folder, MANIFEST).isFile) listOf(folder.canonicalFile)
        else folder.listFiles().orEmpty().filter { File(it, MANIFEST).isFile }.map { it.canonicalFile }.sortedBy { it.name }

    /** The stand-in host and the SDK's library, compiled once for every application. */
    private class Host(val library: List<File>, val standin: File, val checks: File)

    private fun hostObjects(into: File): Host {
        into.mkdirs()
        // The module's own C library and its arena over the module's page are the device's:
        // this computer has a C library, and the stand-in host has the arena.
        val library = sdk.library.listFiles().orEmpty()
            .filter { it.isFile && it.name.endsWith(".c") && it.name !in DEVICE_ONLY }.sortedBy { it.path }
        fun compiled(source: File): File {
            val made = File(into, source.nameWithoutExtension + ".o")
            val (code, said) = execute(listOf(compiler) + FLAGS + includes() + listOf("-c", source.path, "-o", made.path), into)
            if (code != 0) throw BuildError("${source.path} did not compile for this computer:\n$said")
            return made
        }
        return Host(library.map(::compiled), compiled(File(checkDir, "standin.c")), compiled(File(checkDir, "checks.c")))
    }

    private fun includes(vararg more: File): List<String> =
        (listOf(sdk.sdk, sdk.library, checkDir) + more).map { "-I" + it.path }

    private fun checkOne(appDir: File, scratch: File, host: Host, only: String?): List<Line> {
        val name = appDir.name
        try {
            scratch.mkdirs()
            val log = ArrayList<String>()
            // The module as the device gets it: what it imports and exports says what it uses.
            val quiet = AppBuilder(sdk, wasiSdk) { log += it }
            val built = try {
                quiet.build(appDir, File(scratch, "package"))
            } catch (error: BuildError) {
                return listOf(Line(name, "build", State.FAILED, log.takeLast(12) + listOfNotNull(error.message)))
            }
            val module = Wasm(File(built.dir, "app.wasm"))
            val imports = module.imports.map { it.second }.toSet()
            val manifest = built.manifest
            if (manifest["system"] == true) {
                return listOf(Line(name, "all", State.NOT_APPLICABLE,
                    listOf("it asks for the system API, which the stand-in host does not stand in for")))
            }
            val generated = File(scratch, "generated").apply { mkdirs() }
            quiet.headers(appDir, generated)
            val own = File(appDir, "test")
            val subject = Subject(built, File(own, CHECK_FILE), scratch)
            val strings = listOf(File(generated, "strings.c")).filter { it.isFile }

            val lines = ArrayList<Line>()
            val asked = CAPABILITIES.filter { only == null || it == only }
            val applies = asked.filter { applies(it, imports, module.exports.toSet(), manifest) }
            if (applies.isNotEmpty()) {
                val description = File(scratch, "subject.c").apply { writeText(subject.source(module.exports.toSet())) }
                val sources = appDir.listFiles().orEmpty().filter { it.isFile && it.name.endsWith(".c") }.sortedBy { it.path }
                val program = File(scratch, "checks")
                val (code, said) = execute(listOf(compiler) + FLAGS + includes(appDir, generated) +
                    (sources + strings + description).map { it.path } +
                    (host.library + host.standin + host.checks).map { it.path } + listOf("-o", program.path) + LIBRARIES, appDir)
                if (code != 0) {
                    lines += applies.map { Line(name, it, State.FAILED, listOf("it does not compile for this computer:") + said.lines().take(20)) }
                } else {
                    lines += capabilityLines(name, applies, execute(listOf(program.path) + applies, appDir, RUN_SECONDS))
                }
            }
            // In the capabilities' order, those that do not apply in their places.
            asked.filter { it !in applies }.forEach { lines += Line(name, it, State.NOT_APPLICABLE, emptyList()) }
            lines.sortBy { asked.indexOf(it.what) }
            if (only == null || only == OWN_TESTS) {
                val tests = own.listFiles().orEmpty().filter { it.isFile && it.name.endsWith(".c") }.sortedBy { it.name }
                val description = File(scratch, "manifest.c").apply { writeText(subject.source(null)) }
                for (test in tests) {
                    val program = File(scratch, "test-" + test.nameWithoutExtension)
                    val (code, said) = execute(listOf(compiler) + FLAGS + includes(appDir, generated, own) +
                        (listOf(test) + strings + description).map { it.path } +
                        (host.library + host.standin).map { it.path } + listOf("-o", program.path) + LIBRARIES, own)
                    val what = "$OWN_TESTS ${test.name}"
                    if (code != 0) {
                        lines += Line(name, what, State.FAILED, listOf("it does not compile:") + said.lines().take(20))
                        continue
                    }
                    val (result, output) = execute(listOf(program.path), own, RUN_SECONDS)
                    lines += Line(name, what, if (result == 0) State.OK else State.FAILED,
                        if (result == 0) emptyList() else output.lines().filter { it.isNotBlank() }.takeLast(12))
                }
            }
            return lines
        } catch (error: Exception) {
            return listOf(Line(name, "all", State.FAILED, listOf(error.message ?: error.toString())))
        }
    }

    /** What the checks program printed, as lines of the report. */
    private fun capabilityLines(name: String, asked: List<String>, ran: Pair<Int, String>): List<Line> {
        val (code, output) = ran
        val states = HashMap<String, State>()
        val details = HashMap<String, MutableList<String>>()
        for (line in output.lines()) {
            val words = line.split(' ', limit = 3)
            if (words.size < 3) continue
            when (words[0]) {
                "check" -> states[words[1]] = if (words[2].startsWith("ok")) State.OK else State.FAILED
                "problem" -> details.getOrPut(words[1]) { ArrayList() } += words[2]
                "note" -> details.getOrPut(words[1]) { ArrayList() } += "note: " + words[2]
            }
        }
        if (asked.any { it !in states }) {
            // The program itself ended badly: nothing of what it found can be trusted.
            val why = listOf("the checks stopped with code $code") + output.lines().filter { it.isNotBlank() }.takeLast(12)
            return asked.map { Line(name, it, State.FAILED, why) }
        }
        return asked.map { Line(name, it, states.getValue(it), details[it].orEmpty()) }
    }

    /** Runs a command; its exit code and what it said. A program that outlives `seconds` is stopped. */
    private fun execute(command: List<String>, directory: File, seconds: Long = COMPILE_SECONDS): Pair<Int, String> {
        val out = Files.createTempFile("perinkpheral-app-check", ".out").toFile()
        try {
            val process = ProcessBuilder(command).directory(directory).redirectErrorStream(true).redirectOutput(out).start()
            if (!process.waitFor(seconds, TimeUnit.SECONDS)) {
                process.destroyForcibly().waitFor()
                return -1 to out.readText() + "\nstopped after $seconds s"
            }
            return process.exitValue() to out.readText()
        } finally {
            out.delete()
        }
    }

    /** The application as the stand-in host is told of it: a C file made of its manifest and
     * of what is kept beside it. */
    private inner class Subject(private val built: Built, checkFile: File, private val scratch: File) {
        private val manifest = built.manifest
        @Suppress("UNCHECKED_CAST")
        private val kept: Map<String, Any?> =
            if (checkFile.isFile) Json.parse(checkFile.readText()) as? Map<String, Any?> ?: throw BuildError("${checkFile.path} is not an object")
            else emptyMap()
        private val keptDir = checkFile.parentFile

        private fun maps(key: String): List<Map<*, *>> = (manifest[key] as? List<*>).orEmpty().filterIsInstance<Map<*, *>>()

        /** With the module's exports, the handlers too: for the capability checks. A test of
         * the application's own names the handlers it wants called. */
        fun source(exports: Set<String>?): String = buildString {
            appendLine("/* Generated by tools/perinkpheral app-check. */")
            appendLine("#include \"standin.h\"")
            appendLine()
            val settings = maps("settings")
            if (settings.isNotEmpty()) {
                appendLine("static const standin_setting_t SETTINGS[] = {")
                for (setting in settings) {
                    val kind = when (setting["type"]) {
                        "url", "link" -> "STANDIN_SETTING_USERS"
                        "list" -> "STANDIN_SETTING_LIST"
                        else -> "STANDIN_SETTING_PLAIN"
                    }
                    appendLine("    { ${text(setting["key"] as String)}, $kind, ${text(default(setting))}, ${text(longest(setting))} },")
                }
                appendLine("};")
            }
            val polls = maps("polls")
            polls.forEachIndexed { index, poll ->
                val picks = (poll["pick"] as? List<*>).orEmpty()
                if (picks.isNotEmpty()) appendLine("static const char *const PICKS_$index[] = { ${picks.joinToString(", ") { text(it as String) }} };")
            }
            if (polls.isNotEmpty()) {
                appendLine("static const standin_poll_t POLLS[] = {")
                polls.forEachIndexed { index, poll ->
                    val picks = (poll["pick"] as? List<*>).orEmpty()
                    // The manifest's headers as lines "Name: value", for the host to fill when it asks.
                    val headers = (poll["headers"] as? Map<*, *>)?.entries?.joinToString("\n") { (name, value) -> "$name: $value" }
                    appendLine("    { ${text(poll["url"] as String)}, ${if (picks.isEmpty()) "NULL" else "PICKS_$index"}, ${picks.size}, " +
                        "${text(poll["each"] as? String)}, ${text(poll["prefix"] as? String)}, ${text(headers)} },")
                }
                appendLine("};")
            }
            val hosts = (manifest["hosts"] as? List<*>).orEmpty().filterIsInstance<String>()
            if (hosts.isNotEmpty()) appendLine("static const char *const HOSTS[] = { ${hosts.joinToString(", ") { text(it) }} };")
            // The package as it is installed: its files and the tables of its languages, which count
            // against what it may write as the files it writes do; not the module and the manifest.
            val files = built.dir.listFiles().orEmpty()
                .filter { it.isFile && it.name != MANIFEST && it.name != MODULE }.sortedBy { it.name }
            if (files.isNotEmpty()) {
                appendLine("static const standin_file_t FILES[] = {")
                files.forEach { appendLine("    { ${text(it.name)}, ${text(it.path)} },") }
                appendLine("};")
            }
            val store = (kept["store"] as? Map<*, *>).orEmpty()
            if (store.isNotEmpty()) {
                appendLine("static const standin_pair_t STORE[] = {")
                store.forEach { (key, value) -> appendLine("    { ${text(key.toString())}, ${text(value.toString())} },") }
                appendLine("};")
            }
            val answers = answers()
            if (answers.isNotEmpty()) {
                appendLine("static const standin_answer_t ANSWERS[] = {")
                answers.forEach { (address, status, file) -> appendLine("    { ${text(address)}, $status, ${text(file.path)}, NULL },") }
                appendLine("};")
            }
            val at = kept["at"] as? Long ?: 0L
            // The zone's rules as its changes, for the years around the time it is checked at:
            // what an application converts lies there. Before the first, the offset of the start.
            val rules = (kept["tz"] as? String)?.let { ZoneId.of(it).rules }
            val from = Instant.ofEpochSecond((if (at != 0L) at else DEFAULT_AT) - ZONE_YEARS_BEFORE * SECONDS_A_YEAR)
            val until = from.plusSeconds((ZONE_YEARS_BEFORE + ZONE_YEARS_AFTER) * SECONDS_A_YEAR)
            val zone = rules?.getOffset(from)?.totalSeconds ?: 0
            val changes = generateSequence(rules?.nextTransition(from)) { rules?.nextTransition(it.instant) }
                .takeWhile { it.instant < until }.toList()
            if (changes.isNotEmpty()) {
                appendLine("static const standin_zone_change_t ZONE[] = {")
                changes.forEach { appendLine("    { ${it.instant.epochSecond}LL, ${it.offsetAfter.totalSeconds} },") }
                appendLine("};")
            }
            val arena = arenaBytes(built.memoryEnd)
            val intents = (manifest["intents"] as? List<*>).orEmpty().sumOf { 1 shl (INTENTS.indexOf(it) + 1) }
            appendLine()
            appendLine("const standin_subject_t standin_subject = {")
            appendLine("    .name = ${text(manifest["name"] as String)},")
            appendLine("    .is_fullscreen = ${manifest["fullscreen"] == true}, .writes_files = ${manifest["writes_files"] == true},")
            if (settings.isNotEmpty()) appendLine("    .settings = SETTINGS, .setting_count = ${settings.size},")
            if (polls.isNotEmpty()) appendLine("    .polls = POLLS, .poll_count = ${polls.size},")
            if (hosts.isNotEmpty()) appendLine("    .hosts = HOSTS, .host_count = ${hosts.size},")
            if (files.isNotEmpty()) appendLine("    .files = FILES, .file_count = ${files.size},")
            if (store.isNotEmpty()) appendLine("    .store = STORE, .store_count = ${store.size},")
            if (answers.isNotEmpty()) appendLine("    .answers = ANSWERS, .answer_count = ${answers.size},")
            if (changes.isNotEmpty()) appendLine("    .zone_changes = ZONE, .zone_change_count = ${changes.size},")
            appendLine("    .intents = $intents, .at = ${at}LL, .zone_offset = $zone, .arena_bytes = $arena,")
            appendLine("};")
            if (exports != null) {
                appendLine()
                appendLine("void standin_subject_handlers(void)")
                appendLine("{")
                for (handler in HANDLERS.filter { "app_$it" in exports }) appendLine("    standin_handlers.$handler = app_$handler;")
                appendLine("}")
            }
        }

        private fun default(setting: Map<*, *>): String? = when (val default = setting["default"]) {
            null -> null
            is List<*> -> default.joinToString("\n")
            else -> default.toString()
        }

        /** A value as long as the setting may be: what the application's store has to hold
         * beside its own. A number or an address has a form only the application knows. */
        private fun longest(setting: Map<*, *>): String? {
            val room = sdk.limit("SETTING_VALUE_MAX").toInt()
            return when (setting["type"]) {
                "choice" -> (setting["options"] as? List<*>).orEmpty().filterIsInstance<String>().maxByOrNull { it.toByteArray().size }
                "text", "secret", "place" -> "W".repeat(room)
                "list" -> {
                    // The default's items first, then more of their length until the value is full.
                    val items = (setting["default"] as? List<*>).orEmpty().map { it.toString() }.toMutableList()
                    while (items.sumOf { it.length + 1 } + FILLER.length + 3 <= room) items += "$FILLER${items.size}"
                    items.joinToString("\n")
                }
                else -> null
            }
        }

        /** The answers of check.json, each body in a file. */
        private fun answers(): List<Triple<String, Long, File>> {
            val (mocks, beside) = when (val given = kept["mocks"]) {
                null -> return emptyList()
                is String -> File(keptDir, given).let { file ->
                    (Json.parse(file.readText()) as? Map<*, *> ?: throw BuildError("${file.path} is not an object")) to file.parentFile
                }
                is Map<*, *> -> given to keptDir
                else -> throw BuildError("\"mocks\" of $CHECK_FILE is an object or the name of a file")
            }
            val bodies = File(scratch, "answers").apply { mkdirs() }
            return mocks.entries.mapIndexed { index, (address, answer) ->
                answer as? Map<*, *> ?: throw BuildError("the answer for $address is not an object")
                val file = (answer["file"] as? String)?.let { File(beside, it) }
                    ?: File(bodies, "$index.body").apply { writeText(answer["body"] as? String ?: "") }
                if (!file.isFile) throw BuildError("${file.path}: the answer for $address is not there")
                Triple(address.toString(), answer["status"] as? Long ?: 200L, file)
            }
        }
    }

    companion object {
        const val OWN_TESTS = "logic"
        val CAPABILITIES = listOf("sessions", "store", "timers", "network", "drawing")

        private const val MANIFEST = "manifest.json"
        private const val MODULE = "app.wasm"
        /** wasm-ld puts __heap_base, where app_alloc() begins, on 16 bytes after the data. */
        private const val HEAP_ALIGNMENT = 16L
        private const val ZONE_YEARS_BEFORE = 2L
        private const val ZONE_YEARS_AFTER = 3L
        private const val SECONDS_A_YEAR = 366L * 24 * 3600
        /** The stand-in host's clock when check.json names no time (standin.c, DEFAULT_TIME). */
        private const val DEFAULT_AT = 1795262400L
        private const val CHECK_FILE = "check.json"
        private const val FILLER = "item"
        private const val COMPILE_SECONDS = 300L
        private const val RUN_SECONDS = 600L
        private val DEVICE_ONLY = setOf("app_runtime.c", "app_alloc.c")
        private val INTENTS = listOf("time_changed", "network_up", "started")

        /** The handlers of standin_handlers_t, as its fields are named. */
        private val HANDLERS = listOf("on_load", "on_focus", "on_blur", "on_terminate", "on_installed", "on_updated",
            "on_settings", "on_intent", "on_back", "on_button", "on_timer", "on_alarm", "on_draw", "on_data", "on_picked")

        /** One set of flags for the stand-in host, every application and every test of one:
         * both sanitizers, and a finding of either stops the program. The contract's
         * declarations are the application's imports, here defined by the stand-in host. */
        val FLAGS = listOf("-std=c11", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
            "-Wno-attributes", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
            "-DHOST_IMPORT(name,...)=__VA_ARGS__", "-DAPP_EXPORT(name,...)=__VA_ARGS__")
        private val LIBRARIES = listOf("-lm")

        /** What app_alloc() has of the page after the module's stack and data, which end at
         * `memoryEnd`: none when they fill it. Negative when the build could not say where they
         * end, which the stand-in host takes for the whole page. */
        fun arenaBytes(memoryEnd: Long): Long =
            if (memoryEnd <= 0) -1L
            else maxOf(0L, BuildFlags.WASM_PAGE - (memoryEnd + HEAP_ALIGNMENT - 1) / HEAP_ALIGNMENT * HEAP_ALIGNMENT)

        /** Which applications a capability is checked on. Every application is loaded and unloaded;
         * the rest is what its module asks of the host. */
        fun applies(capability: String, imports: Set<String>, exports: Set<String>, manifest: Map<*, *>): Boolean = when (capability) {
            "sessions" -> true
            "store" -> "kv_set" in imports || "file_write" in imports
            "timers" -> imports.any { it == "timer_set" || it.startsWith("alarm_set") }
            "network" -> "http_send" in imports || (manifest["polls"] as? List<*>).orEmpty().isNotEmpty()
            "drawing" -> "app_on_draw" in exports
            else -> false
        }

        /** A text as a C string; null as NULL. Bytes beyond ASCII are written out. */
        fun text(value: String?): String {
            if (value == null) return "NULL"
            val out = StringBuilder("\"")
            for (byte in value.toByteArray()) {
                val code = byte.toInt() and 0xFF
                when {
                    // A question mark too: two of them begin a trigraph in ISO C.
                    code == '"'.code || code == '\\'.code || code == '?'.code -> out.append('\\').append(code.toChar())
                    code == '\n'.code -> out.append("\\n")
                    code in 0x20..0x7E -> out.append(code.toChar())
                    // Three octal digits: nothing that follows can be read into the escape.
                    else -> out.append("\\%03o".format(code))
                }
            }
            return out.append('"').toString()
        }
    }
}
