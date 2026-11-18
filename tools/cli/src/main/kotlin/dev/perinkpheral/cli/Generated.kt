// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.contract.Contract
import java.io.File
import java.nio.file.Files
import javax.imageio.ImageIO

/** Committed output is checked in a scratch directory; a check never rewrites it. */
object Generated {
    private val screens = listOf("icons", "icons-more", "icons-air", "kit-date", "kit-list",
        "kit-message", "kit-picker", "kit-primitives", "notice-progress", "notice-question")

    private fun samePicture(kept: File, made: File): Boolean {
        val before = ImageIO.read(kept) ?: return false
        val after = ImageIO.read(made) ?: return false
        return before.width == after.width && before.height == after.height &&
            (0 until before.height).all { y -> (0 until before.width).all { x -> before.getRGB(x, y) == after.getRGB(x, y) } }
    }

    fun check(root: File, contract: Contract): List<String> {
        val scratch = Files.createTempDirectory("perinkpheral-generated").toFile()
        val checked = ArrayList<String>()
        val stale = ArrayList<String>()
        fun compare(path: String, made: File) {
            val kept = File(root, path)
            val same = kept.isFile && if (path.endsWith(".png")) samePicture(kept, made)
                else kept.readBytes().contentEquals(made.readBytes())
            checked += path
            if (!same) stale += path
        }
        try {
            val icons = File(scratch, "gfx_icon_art.c")
            Icons.make(root, contract, icons)
            compare("firmware/components/gfx/gfx_icon_art.c", icons)
            val mark = File(scratch, "mark")
            val rectangles = Icons.mark(root, out = mark)
            mark.listFiles().orEmpty().sortedBy { it.name }.forEach { compare("assets/mark/${it.name}", it) }
            val spelling = File(scratch, "spelling.h")
            Spelling.make(spelling)
            compare("firmware/components/gfx/spelling.h", spelling)
            val faces = listOf(Triple("DepartureMono-Regular.otf", "departure_11", 11.0),
                Triple("Jersey10-Regular.ttf", "jersey_10", 18.6667),
                Triple("Jersey15-Regular.ttf", "jersey_15", 27.0),
                Triple("Jersey20-Regular.ttf", "jersey_20", 34.0),
                Triple("Jersey25-Regular.ttf", "jersey_25", 41.0))
            for ((font, name, pixels) in faces) {
                val source = "assets/fonts/$font"
                val header = File(scratch, "$name.h")
                Fonts.header(File(root, source), source, pixels, name, header)
                compare("firmware/components/gfx/fonts/$name.h", header)
            }
            FirmwareTools.cities(File(root, "firmware/system/settings/cities.json"),
                File(root, "firmware/system/settings/cities.h"), true)
            checked += "firmware/system/settings/cities.h"
            val markPath = rectangles.joinToString("") { (x, y, width, height) -> "M$x ${y}h${width}v${height}H${x}z" }
            val page = File(root, "firmware/main/web/page.html").readText()
            val embedded = Regex("<svg class=\"mark\"[^>]*><path d=\"([^\"]+)\"").findAll(page).toList()
            if (embedded.size != 2 || embedded.any { it.groupValues[1] != markPath }) stale += "firmware/main/web/page.html: mark"
            val wordmark = File(root, "assets/mark/wordmark.svg").readText()
            val wordmarkPath = Regex(" d=\"([^\"]+)\"").find(wordmark)?.groupValues?.get(1)
                ?: throw BuildError("wordmark.svg: missing path")
            val embeddedWord = Regex("<svg class=\"wordmark\"[^>]*><path d=\"([^\"]+)\"").find(page)?.groupValues?.get(1)
            if (embeddedWord != wordmarkPath) stale += "firmware/main/web/page.html: wordmark"
            checked += "firmware/main/web/page.html: mark and wordmark"
            val preview = File(scratch, "screens")
            Preview.render(root, preview, null)
            for (name in screens) compare("docs/guide/source/screens/$name.png", File(preview, "$name.png"))
            if (stale.isNotEmpty()) throw BuildError("generated files are stale:\n" + stale.joinToString("\n") { "  $it" })
            return checked.map { "$it: current" }
        } finally {
            scratch.deleteRecursively()
        }
    }

    /** Route tables are the endpoint surface. Payloads remain documented by hand. */
    fun checkApi(root: File): String {
        val routes = listOf("web_server.c", "web_apps.c", "devtools.c").flatMap { name ->
            Regex("\\{\\s*\"([^\"]+)\",\\s*HTTP_(GET|POST),")
                .findAll(File(root, "firmware/main/$name").readText())
                .map { "${it.groupValues[2]} ${it.groupValues[1]}" }.toList()
        }.toSet()
        var path = ""
        val documented = LinkedHashSet<String>()
        File(root, "docs/api/openapi.yaml").forEachLine { line ->
            Regex("^  (/[^:]*):$").find(line)?.let { path = it.groupValues[1] }
            Regex("^    (get|post):$").find(line)?.let { documented += "${it.groupValues[1].uppercase()} $path" }
        }
        val missing = routes - documented
        val removed = documented - routes
        if (missing.isNotEmpty() || removed.isNotEmpty()) throw BuildError(
            "openapi.yaml: missing ${missing.sorted()}; no route for ${removed.sorted()}")
        return "${routes.size} API routes agree with openapi.yaml"
    }

    /** PDF bytes include dates and browser details. Compare all text except running feet and
     * page numbers, so Linux and macOS may use different font metrics and page breaks. */
    fun checkPdf(kept: File, made: File) {
        fun text(pdf: File): String {
            val process = ProcessBuilder("pdftotext", "-layout", pdf.path, "-").start()
            val output = process.inputStream.readBytes().toString(Charsets.UTF_8)
            val error = process.errorStream.readBytes().toString(Charsets.UTF_8)
            if (process.waitFor() != 0) throw BuildError("pdftotext: $error")
            var inContents = false
            return output.replace("\u000c", "\n").lineSequence().map { it.trim() }.filterNot {
                it.contains("perINKpheral / ")
            }.map { line ->
                if (line == "Contents" || line == "Содержание") inContents = true
                if ((line.startsWith("Part I.") || line.startsWith("Часть I.")) &&
                    !Regex("\\d+$").containsMatchIn(line)) inContents = false
                if (inContents) line.replace(Regex("\\s+\\d+$"), "") else line
            }.joinToString(" ").replace(Regex("\\s+"), " ").trim()
        }
        if (text(kept) != text(made)) throw BuildError("${kept.path}: stale; rebuild with tools/perinkpheral guide")
        println("${kept.path}: current text")
    }
}
