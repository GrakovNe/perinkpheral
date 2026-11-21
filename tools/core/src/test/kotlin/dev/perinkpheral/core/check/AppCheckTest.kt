// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.check

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.build.Sdk
import java.io.File
import java.nio.file.Files

/**
 * The capability checks held to an application made for them, `sdk/check/apps/specimen`:
 * every check passes on it as it is, and each fails on a copy with one line of the
 * contract broken. A check that cannot fail is not a check.
 *
 * It compiles C for this computer and builds a module: without a C compiler or wasi-sdk
 * (WASI_SDK_PATH) only what needs neither is tested, and it says so.
 */
object AppCheckTest {
    /** One line of the contract broken in the specimen, and the capability that has to notice. */
    private class Break(val capability: String, val what: String, val edits: List<Pair<String, String>>, val saying: String,
                        val manifestEdits: List<Pair<String, String>> = emptyList())

    private val BREAKS = listOf(
        Break("sessions", "the page is not kept in the session",
            listOf("    session_set(&page, sizeof page);\n" to ""), "back from the stack"),
        Break("sessions", "a handler never returns",
            listOf("void app_on_back(void)\n{\n" to "static volatile int forever = 1;\n\nvoid app_on_back(void)\n{\n    while (forever) { }\n"),
            "did not return in 3 s"),
        Break("store", "every answer is counted in the store, changed or not",
            listOf("{\n    if (is_same(kept, text) && !*is_unsaved) {" to
                "{\n    static int32_t answers;\n    answers++;\n    kv_set(\"answers\", 7, &answers, sizeof answers);\n" +
                "    if (is_same(kept, text) && !*is_unsaved) {"), "unchanged answers"),
        Break("store", "a refused write is not tried again",
            listOf("    if (is_same(kept, text) && !*is_unsaved) {" to "    if (is_same(kept, text)) {"),
            "not saved when the same answer comes again"),
        Break("timers", "the timer is set by a press and not again in app_on_focus()",
            listOf("    timer_set(TICK_TIMER, TICK_MS);\n    alarm_set_every" to "    alarm_set_every",
                "    if (page == 0) {\n        page = 1;" to "    timer_set(TICK_TIMER, TICK_MS);\n    if (page == 0) {\n        page = 1;"),
            "set again in app_on_focus()"),
        Break("timers", "app_on_updated() does not set the alarm again",
            listOf("void app_on_updated(void)\n{\n    alarm_set_every(ASK_ALARM, ASK_EVERY_S, 0);" to "void app_on_updated(void)\n{"),
            "app_on_updated() sets them again"),
        Break("network", "HOST_DATA_FAILED keeps the pieces that came before it",
            listOf("        arriving_length = 0;\n        return;\n    }\n    if ((flags & HOST_DATA_FIRST)" to
                "        arriving[arriving_length] = '\\0';\n        keep_text(\"note\", note, arriving, &note_is_unsaved);\n        return;\n    }\n    if ((flags & HOST_DATA_FIRST)"),
            "fails half way"),
        Break("network", "an answer's pieces are kept as they come, whole or not",
            listOf("    if ((flags & HOST_DATA_LAST) != 0) {\n        arriving[arriving_length] = '\\0';" to
                "    {\n        arriving[arriving_length] = '\\0';"), "stops half way"),
        Break("network", "the buffer of a note is shorter than the note",
            listOf("static char arriving[TEXT_BYTES];" to "static char arriving[16];"), "stopped in app_on_data"),
        Break("drawing", "a button's handler draws",
            listOf("    if (event != BTN_OK_PRESSED) {\n        return;\n    }\n" to
                "    if (event != BTN_OK_PRESSED) {\n        return;\n    }\n    gfx_draw_text(0, 0, HOST_FONT_TEXT, \"x\", 1);\n"),
            "outside app_on_draw()"),
        Break("drawing", "a rectangle is drawn past what the host takes",
            listOf("    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n    gfx_draw_rect(0, 0, 5000, 10, HOST_RECT_FILLED);\n"),
            "HOST_ERR_ARGUMENT"),
        Break("network", "an answer is taken only by the instance that asked for it",
            listOf("static void ask_note(void)\n{\n" to "static bool has_asked;\n\nstatic void ask_note(void)\n{\n    has_asked = true;\n",
                "    if (request != NOTE_REQUEST) {" to "    if (request != NOTE_REQUEST || !has_asked) {"),
            "an application that was on the screen when the answers came"),
        Break("network", "what an answer changed is not asked to be drawn",
            listOf("    *is_unsaved = kv_set(key, length_of(key), kept, at) != HOST_OK;\n    screen_redraw(HOST_REDRAW_NORMAL);\n" to
                "    *is_unsaved = kv_set(key, length_of(key), kept, at) != HOST_OK;\n"),
            "screen_redraw()"),
        Break("network", "a request goes to a host the manifest does not list",
            listOf("static const char NOTE_URL[] = \"https://example.test/note\";" to
                "static const char NOTE_URL[] = \"https://elsewhere.test/note\";"),
            "does not list"),
        Break("network", "a request with a tag the contract does not take",
            listOf("#define NOTE_REQUEST   HOST_REQUEST_FIRST" to "#define NOTE_REQUEST   (HOST_REQUEST_FIRST - 1)"),
            "does not take"),
        Break("network", "a request to a listed host at another port",
            listOf("static const char NOTE_URL[] = \"https://example.test/note\";" to
                "static const char NOTE_URL[] = \"https://example.test:8443/note\";"),
            "does not list"),
        Break("network", "one handler queues more than the device's 16 KB of requests",
            listOf("static void ask_note(void)\n{\n" to "static void ask_note(void)\n{\n" +
                "    static char filler[HOST_HTTP_BODY_MAX], headers[4 * 252];\n" +
                "    for (int32_t at = 0; at < (int32_t)sizeof headers; at++) {\n" +
                "        headers[at] = at % 252 == 0 ? 'A' : at % 252 == 1 ? ':' : at % 252 == 251 ? '\\n' : 'x';\n" +
                "    }\n" +
                "    for (int32_t count = 0; count < 11; count++) {\n" +
                "        http_send(NOTE_REQUEST + 1 + count, HOST_HTTP_POST, NOTE_URL, length_of(NOTE_URL), headers,\n" +
                "                  sizeof headers, filler, sizeof filler);\n" +
                "    }\n"),
            "16 KB"),
        Break("drawing", "a bar of no width",
            listOf("    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n    ui_draw_bar(MARGIN, MARGIN, 0, 1, 2);\n"),
            "ui_draw_bar() answered HOST_ERR_ARGUMENT"),
        Break("drawing", "app_on_draw() asks the surface's size and draws nothing",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    (void)gfx_width();\n    return;\n"),
            "drew nothing"),
        Break("drawing", "a picture of no width",
            listOf("    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" +
                "    static const uint8_t nothing[] = { 0, 1 };\n    gfx_draw_picture(MARGIN, MARGIN, nothing, 1);\n"),
            "gfx_draw_picture() answered HOST_ERR_ARGUMENT"),
        Break("network", "a value picked from an answer that broke off is kept",
            listOf("    if (request != 0 || http_status != 200) {" to "    (void)http_status;\n    if (request != 0) {"),
            "failed with no pieces"),
        Break("timers", "a timer of an id the device does not have",
            listOf("#define TICK_TIMER     0" to "#define TICK_TIMER     HOST_TIMER_COUNT"),
            "timer_set("),
        Break("drawing", "app_on_draw() chooses a colour and draws nothing",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    gfx_set_color(HOST_INK);\n    return;\n"),
            "drew nothing"),
        Break("store", "nothing written at an offset of a file",
            listOf("    if (event != BTN_OK_PRESSED) {\n" to
                "    file_write(\"cache\", 5, 0, \"x\", 1);\n    file_write(\"cache\", 5, 1, NULL, 0);\n    if (event != BTN_OK_PRESSED) {\n"),
            "file_write(\"cache\"", listOf("\"fullscreen\": false," to "\"fullscreen\": false, \"writes_files\": true,")),
        Break("store", "a write longer than the module's page",
            listOf("    if (event != BTN_OK_PRESSED) {\n" to
                "    static char byte;\n    file_write(\"cache\", 5, 0, &byte, 2147483647);\n    if (event != BTN_OK_PRESSED) {\n"),
            "2147483647 bytes", listOf("\"fullscreen\": false," to "\"fullscreen\": false, \"writes_files\": true,")),
        Break("drawing", "a press changes what is drawn, held in memory only, and asks for no redraw",
            listOf("static int32_t count;" to "static int32_t presses_seen;\nstatic int32_t count;",
                "void app_on_button(int32_t event)\n{\n" to "void app_on_button(int32_t event)\n{\n    presses_seen++;\n",
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                    "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" +
                    "    gfx_draw_text(MARGIN, 200, HOST_FONT_TEXT, presses_seen > 0 ? \"x\" : \"o\", 1);\n"),
            "than it draws now"),
        Break("network", "a handler hangs on the failure of a poll over an empty list, which has no item",
            listOf("    char picked[HOST_VALUE_MAX + 1];\n" to "    char picked[HOST_VALUE_MAX + 1];\n" +
                "    if (answer_item() < 0 && http_status < 0) {\n        static volatile int forever = 1;\n        while (forever) { }\n    }\n"),
            "did not return",
            listOf("\"url\": \"https://example.test/now?place={{place}}\", \"every\": 60," to
                    "\"url\": \"https://example.test/now?place={{places}}\", \"every\": 60, \"each\": \"places\",",
                "{ \"key\": \"place\", \"type\": \"text\", \"title\": \"Place\", \"default\": \"here\" }," to
                    "{ \"key\": \"place\", \"type\": \"text\", \"title\": \"Place\", \"default\": \"here\" },\n" +
                    "    { \"key\": \"places\", \"type\": \"list\", \"title\": \"Places\" },")),
        Break("drawing", "app_on_draw() fills a rectangle of no size and draws nothing",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    gfx_draw_rect(0, 0, 0, 0, HOST_RECT_FILLED);\n    return;\n"),
            "drew nothing"),
        Break("store", "a value larger than the store takes is written",
            listOf("        count++;\n        kv_set(\"count\", 5, &count, sizeof count);" to
                "        count++;\n        static char large[HOST_KV_TOTAL_MAX];\n        kv_set(\"count\", 5, large, sizeof large);"),
            "over the limits"),
    )

    /** What the stand-in host copied from the firmware, as a name there and a name here. */
    private val PINS = listOf(
        Triple("firmware/components/ui/kit.c", "TITLE_TOP", "TITLE_TOP"),
        Triple("firmware/components/ui/kit.c", "TITLE_RULE_GAP", "TITLE_RULE_GAP"),
        Triple("firmware/components/ui/kit.c", "TITLE_AFTER_RULE", "TITLE_AFTER_RULE"),
        Triple("firmware/components/ui/kit.c", "CELL_PADDING", "CELL_PADDING"),
        Triple("firmware/components/ui/kit.c", "CELL_PICTURE_GAP", "CELL_PICTURE_GAP"),
        Triple("firmware/components/ui/kit.c", "KEY_PAD_X", "KEY_PAD_X"),
        Triple("firmware/components/ui/kit.c", "KEY_ACTION_GAP", "KEY_ACTION_GAP"),
        Triple("firmware/components/ui/kit.c", "VALUE_UNIT_GAP", "VALUE_UNIT_GAP"),
        Triple("firmware/components/ui/kit.c", "VALUE_UNIT_GAP_LARGE", "VALUE_UNIT_GAP_LARGE"),
        Triple("firmware/components/ui/kit.c", "TEXT_BUFFER", "KIT_TEXT_BUFFER"),
        Triple("firmware/components/ui/list.c", "ROW_HEIGHT", "LIST_ROW_HEIGHT"),
        Triple("firmware/components/ui/list.c", "TOP_GAP", "LIST_TOP_GAP"),
        Triple("firmware/components/appsys/app_api_ui.c", "BUTTON_NAME_MAX", "BUTTON_NAME_MAX"),
        Triple("firmware/components/appstore/include/request_queue.h", "REQUEST_QUEUE_BYTES_MAX", "REQUEST_QUEUE_BYTES"),
    )

    private fun defines(file: File): Map<String, String> =
        Regex("""^#define\s+(\w+)\s+([^/\n]*?)\s*(/\*.*)?$""", RegexOption.MULTILINE).findAll(file.readText())
            .associate { it.groupValues[1] to it.groupValues[2] }

    /** The stand-in host's numbers are the firmware's: a change of kit.c, list.c, app_api_ui.c,
     * request_queue.h or the font that is not made here too fails this. The record of a queued
     * request is measured for wasm32, whose layout is the device's 32-bit one. */
    private fun pinned(root: File, wasi: File) {
        val standin = File(root, "sdk/check/standin.c")
        val here = defines(standin)
        for ((file, there, ours) in PINS) {
            val theirs = defines(File(root, file))[there] ?: error("$file no longer defines $there")
            check(here[ours] == theirs) { "standin.c's $ours is ${here[ours]}, $file's $there is $theirs" }
        }
        val ascent = Regex("""departure_11_ascent = (\d+)""").find(File(root, "firmware/components/gfx/fonts/departure_11.h").readText())
        check(ascent != null && here["DEPARTURE_ASCENT"] == ascent.groupValues[1]) { "the font's ascent is not standin.c's" }
        val record = here["REQUEST_RECORD_BYTES"] ?: error("standin.c has no REQUEST_RECORD_BYTES")
        val probe = Files.createTempFile("request-record", ".c").toFile()
        try {
            probe.writeText("#include \"request_queue.h\"\n_Static_assert(sizeof(queued_request_t) == $record, \"the record\");\n")
            val clang = File(wasi, "bin/clang").path
            val command = listOf(clang, "--target=wasm32", "-ffreestanding", "-fsyntax-only",
                "-I" + File(root, "firmware/components/appstore/include").path, "-I" + File(root, "sdk").path,
                "-I" + File(root, "sdk/lib").path, probe.path)
            val process = ProcessBuilder(command).redirectErrorStream(true).start()
            val said = process.inputStream.bufferedReader().readText()
            check(process.waitFor() == 0) { "a queued request is not $record bytes on the device:\n$said" }
        } finally {
            probe.delete()
        }
        println("app-check: the stand-in host's measures are the firmware's (${PINS.size + 2} pinned)")
    }

    /** Copies of the specimen that are right, if unusual: the capability passes them. */
    private val FINE = listOf(
        Break("timers", "a timer as long as one may be", listOf("#define TICK_MS        1000" to "#define TICK_MS        2147483647"), ""),
        Break("timers", "a countdown of three seconds, over by the time a late tick comes",
            listOf("void app_on_timer(int32_t id)\n{\n    if (id == TICK_TIMER) {" to
                "void app_on_timer(int32_t id)\n{\n    if (id == TICK_TIMER && time_uptime_ms() < 3000) {"), ""),
    )

    @JvmStatic fun main(args: Array<String>) {
        check(AppCheck.text("a \"b\" \\ ?? \n é") == "\"a \\\"b\\\" \\\\ \\?\\? \\n \\303\\251\"")
        check(AppCheck.text(null) == "NULL")
        check(AppCheck.applies("sessions", emptySet(), emptySet(), emptyMap<String, Any>()))
        check(!AppCheck.applies("store", setOf("kv_get"), emptySet(), emptyMap<String, Any>()))
        check(AppCheck.applies("store", setOf("file_write"), emptySet(), emptyMap<String, Any>()))
        check(AppCheck.applies("timers", setOf("alarm_set_every"), emptySet(), emptyMap<String, Any>()))
        check(AppCheck.applies("network", emptySet(), emptySet(), mapOf("polls" to listOf(mapOf("url" to "https://a")))))
        check(!AppCheck.applies("network", setOf("poll_run"), emptySet(), emptyMap<String, Any>()))
        check(!AppCheck.applies("drawing", emptySet(), setOf("app_on_load"), emptyMap<String, Any>()))
        // A module whose stack and data fill the page leaves app_alloc() nothing, not the default.
        check(AppCheck.arenaBytes(65530) == 0L && AppCheck.arenaBytes(65536) == 0L)
        check(AppCheck.arenaBytes(57000) == 65536L - 57008L && AppCheck.arenaBytes(0) == -1L)

        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val wasi = System.getenv("WASI_SDK_PATH")?.let { File(it) }?.takeIf { File(it, "bin/clang").exists() || File(it, "bin/clang.exe").exists() }
        val compiler = System.getenv("CC")?.takeIf { it.isNotBlank() } ?: "cc"
        val hasCompiler = runCatching { ProcessBuilder(compiler, "--version").redirectErrorStream(true).start().waitFor() == 0 }.getOrDefault(false)
        if (wasi == null || !hasCompiler) {
            println("app-check: no wasi-sdk (WASI_SDK_PATH) or no C compiler: the checks themselves were not run")
            return
        }
        pinned(root, wasi)
        val specimen = File(root, "sdk/check/apps/specimen")
        val said = ArrayList<String>()
        val checker = AppCheck(Sdk(root), wasi) { said += it }

        // As it is: every capability applies to it and passes, and its own test is found and run.
        val whole = checker.run(specimen.parentFile)
        check(whole.failed.isEmpty()) { said.joinToString("\n") }
        check(whole.lines.map { it.what } == AppCheck.CAPABILITIES + "logic host.c") { whole.lines.map { it.what } }
        check(whole.lines.all { it.state == AppCheck.State.OK })
        check(said.first().startsWith("ok   specimen")) { said.first() }

        try { checker.run(specimen.parentFile, only = "pixels"); error("an unknown capability was accepted") }
        catch (error: BuildError) { check("no capability" in error.message.orEmpty()) }
        try { checker.run(specimen.parentFile, onlyApp = "absent"); error("an absent application was accepted") }
        catch (error: BuildError) { check("no application" in error.message.orEmpty()) }

        val scratch = Files.createTempDirectory("perinkpheral-app-check-test").toFile()
        try {
            fun copied(number: Int, broken: Break): File {
                val copy = File(scratch, "$number/specimen")
                specimen.copyRecursively(copy)
                val source = File(copy, "specimen.c")
                var code = source.readText()
                for ((from, to) in broken.edits) {
                    check(from in code) { "${broken.what}: the specimen no longer has the line this breaks" }
                    code = code.replace(from, to)
                }
                source.writeText(code)
                val manifest = File(copy, "manifest.json")
                var described = manifest.readText()
                for ((from, to) in broken.manifestEdits) {
                    check(from in described) { "${broken.what}: the specimen's manifest no longer has what this breaks" }
                    described = described.replace(from, to)
                }
                manifest.writeText(described)
                said.clear()
                return copy
            }
            BREAKS.forEachIndexed { number, broken ->
                val report = checker.run(copied(number, broken).parentFile, only = broken.capability)
                val line = report.lines.single()
                check(line.state == AppCheck.State.FAILED) { "${broken.what}: ${broken.capability} did not notice" }
                check(line.details.any { broken.saying in it }) { "${broken.what}: ${line.details.joinToString("\n")}" }
            }
            // And what is right, if unusual, passes: the checks find no fault the device would not have.
            FINE.forEachIndexed { number, fine ->
                val report = checker.run(copied(BREAKS.size + number, fine).parentFile, only = fine.capability)
                val line = report.lines.single()
                check(line.state == AppCheck.State.OK) { "${fine.what}: ${line.details.joinToString("\n")}" }
            }
        } finally {
            scratch.deleteRecursively()
        }
        println("app-check: the specimen passes; ${BREAKS.size} broken copies are each caught by their capability, " +
            "${FINE.size} unusual but right ones pass")
    }
}
