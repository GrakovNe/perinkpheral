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
    private class Break(val capability: String, val what: String, val edits: List<Pair<String, String>>, val saying: String)

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
        Break("store", "a value larger than the store takes is written",
            listOf("        count++;\n        kv_set(\"count\", 5, &count, sizeof count);" to
                "        count++;\n        static char large[HOST_KV_TOTAL_MAX];\n        kv_set(\"count\", 5, large, sizeof large);"),
            "over the limits"),
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

        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val wasi = System.getenv("WASI_SDK_PATH")?.let { File(it) }?.takeIf { File(it, "bin/clang").exists() || File(it, "bin/clang.exe").exists() }
        val compiler = System.getenv("CC")?.takeIf { it.isNotBlank() } ?: "cc"
        val hasCompiler = runCatching { ProcessBuilder(compiler, "--version").redirectErrorStream(true).start().waitFor() == 0 }.getOrDefault(false)
        if (wasi == null || !hasCompiler) {
            println("app-check: no wasi-sdk (WASI_SDK_PATH) or no C compiler: the checks themselves were not run")
            return
        }
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
            BREAKS.forEachIndexed { number, broken ->
                val copy = File(scratch, "$number/specimen")
                specimen.copyRecursively(copy)
                val source = File(copy, "specimen.c")
                var code = source.readText()
                for ((from, to) in broken.edits) {
                    check(from in code) { "${broken.what}: the specimen no longer has the line this breaks" }
                    code = code.replace(from, to)
                }
                source.writeText(code)
                said.clear()
                val report = checker.run(copy.parentFile, only = broken.capability)
                val line = report.lines.single()
                check(line.state == AppCheck.State.FAILED) { "${broken.what}: ${broken.capability} did not notice" }
                check(line.details.any { broken.saying in it }) { "${broken.what}: ${line.details.joinToString("\n")}" }
            }
        } finally {
            scratch.deleteRecursively()
        }
        println("app-check: the specimen passes; ${BREAKS.size} broken copies are each caught by their capability")
    }
}
