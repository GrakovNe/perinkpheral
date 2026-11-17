// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.Json
import java.io.File
import java.util.Base64
import java.util.zip.Deflater
import java.util.zip.GZIPOutputStream

/** What the firmware's build and its release make of their sources. */
object FirmwareTools {

    /**
     * The device's page, made of its parts: page.html with its markers filled in.
     *
     *     <!-- style: style.css -->          the stylesheet, in a <style>
     *     <!-- include: about.html -->       a piece of markup, as it is
     *     <!-- script: js/a.js js/b.js -->   the scripts in this order, as one <script>
     *
     * A url("<name>.woff2") in the stylesheet becomes the font itself, as a data: address: the
     * page is one file. departure.woff2 is Departure Mono cut to the letters the page shows
     * (U+0020-007E, U+00A0-00FF, U+0400-045F, U+2013-2014, U+2018-201E, U+2026, U+2116, U+2212),
     * made once with a font subsetter and kept as it is. Run by the firmware's build
     * (main/CMakeLists.txt); the device serves the page packed, and every browser unpacks gzip.
     */
    fun page(web: File, out: File, packed: File) {
        fun read(name: String) = File(web, name).readText()
        fun inlineFonts(css: String) = Regex("""url\("([\w.-]+\.woff2)"\)""").replace(css) { found ->
            "url(\"data:font/woff2;base64," + Base64.getEncoder().encodeToString(File(web, found.groupValues[1]).readBytes()) + "\")"
        }
        val marker = Regex("""<!-- (style|include|script): ([^>]+?) -->""")
        val page = marker.replace(read("page.html")) { found ->
            val names = found.groupValues[2].trim().split(Regex("\\s+"))
            when (found.groupValues[1]) {
                "style" -> "<style>\n" + inlineFonts(names.joinToString("") { read(it) }) + "</style>"
                "include" -> names.joinToString("") { read(it) }
                else -> "<script>\n" + names.joinToString("\n") { read(it) } + "</script>"
            }
        }
        if (Regex("""<!-- (style|include|script):""").containsMatchIn(page)) throw BuildError("page: a marker was left unfilled")
        out.parentFile?.mkdirs()
        out.writeText(page)
        packed.outputStream().use { stream ->
            object : GZIPOutputStream(stream) {
                init { def.setLevel(Deflater.BEST_COMPRESSION) }
            }.use { it.write(page.toByteArray()) }
        }
    }

    /**
     * The files a first flash writes, named by a manifest a web flasher reads: what
     * flasher_args.json of an ESP-IDF build lists - the bootloader, the partition table, the
     * OTA data, the firmware and the applications' file system - copied into `out`, and
     * manifest.json beside them in the form of ESP Web Tools: the chip and, for every file,
     * where it goes. `prefix` goes before every name, the manifest's too: the build for
     * debugging stands beside the one for release in one release, as debug-*.
     */
    fun flashManifest(build: File, out: File, version: String, prefix: String = "") {
        val args = Json.parse(File(build, "flasher_args.json").readText()) as Map<*, *>
        out.mkdirs()
        val files = (args["flash_files"] as Map<*, *>).entries.sortedBy { (it.key as String).removePrefix("0x").toLong(16) }
        val parts = files.map { (offset, path) ->
            val source = File(build, path as String)
            source.copyTo(File(out, prefix + source.name), overwrite = true)
            linkedMapOf("path" to prefix + source.name, "offset" to (offset as String).removePrefix("0x").toLong(16))
        }
        val manifest = linkedMapOf(
            "name" to "perINKpheral",
            "version" to version,
            "new_install_prompt_erase" to true,
            "builds" to listOf(linkedMapOf("chipFamily" to "ESP32-C6", "parts" to parts)),
            "flash" to args["flash_settings"],
        )
        File(out, "${prefix}manifest.json").writeText(Json.pretty(manifest) + "\n")
    }
}
