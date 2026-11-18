// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.Json
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
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
     *     <!-- data: NAME ../a.json -->      a JSON file as a constant of the scripts: const NAME = ...;
     *     <!-- defines: a.h NAME b.h NAME --> numbers a C header defines, as constants of the scripts:
     *                                          the page holds the device to the device's own limits
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
        val marker = Regex("""<!-- (style|include|script|data|defines): ([^>]+?) -->""")
        val page = marker.replace(read("page.html")) { found ->
            val names = found.groupValues[2].trim().split(Regex("\\s+"))
            when (found.groupValues[1]) {
                "style" -> "<style>\n" + inlineFonts(names.joinToString("") { read(it) }) + "</style>"
                "include" -> names.joinToString("") { read(it) }
                "data" -> "<script>\nconst ${names[0]} = " + Json.write(Json.parse(read(names[1]))) + ";\n</script>"
                "defines" -> "<script>\n" + defines(web, names) + "</script>"
                else -> "<script>\n" + names.joinToString("\n") { read(it) } + "</script>"
            }
        }
        if (Regex("""<!-- (style|include|script|data|defines):""").containsMatchIn(page)) throw BuildError("page: a marker was left unfilled")
        out.parentFile?.mkdirs()
        out.writeText(page)
        packed.outputStream().use { stream ->
            object : GZIPOutputStream(stream) {
                init { def.setLevel(Deflater.BEST_COMPRESSION) }
            }.use { it.write(page.toByteArray()) }
        }
    }

    /** `const NAME = <number>;` for every name, read from the header named before it. */
    private fun defines(web: File, names: List<String>): String {
        var header: String? = null
        return buildString {
            for (name in names) {
                if (name.endsWith(".h")) {
                    header = File(web, name).readText().replace(Regex("""/\*.*?\*/""", RegexOption.DOT_MATCHES_ALL), " ")
                    continue
                }
                val text = header ?: throw BuildError("page: $name is named before any header")
                val value = Regex("""^#define[ \t]+$name[ \t]+(\d+)[ \t]*$""", RegexOption.MULTILINE).find(text)
                    ?.groupValues?.get(1) ?: throw BuildError("page: $name is no number of its header")
                append("const $name = $value;\n")
            }
        }
    }

    /**
     * The cities Settings offers for the time zone, as a C table made of cities.json - the one
     * list, which the device's page reads too: each city's English name (the one it is kept by),
     * its Russian, its POSIX zone. `check`: only say whether the header is what the list makes.
     */
    fun cities(json: File, header: File, check: Boolean) {
        @Suppress("UNCHECKED_CAST")
        val cities = Json.parse(json.readText()) as List<Map<String, Any?>>
        fun c(text: Any?) = "\"" + (text as String).replace("\\", "\\\\").replace("\"", "\\\"") + "\""
        val rows = cities.joinToString("\n") { city -> "    { ${c(city["name"])}, ${c(city["ru"])}, ${c(city["zone"])} }," }
        val text = """
            |/*
            | * SPDX-License-Identifier: MIT
            | * Copyright (c) 2026 GrakovNe
            | *
            | * Made by tools/perinkpheral cities of cities.json; edit that, not this.
            | *
            | * The time zones the device offers with its buttons, each by a city in it,
            | * as POSIX TZ strings with the rules for summer time they have today: the
            | * city's English name - the one it is kept by - and its Russian. A zone
            | * that is not here is set from the device's web page.
            | */
            |#pragma once
            |
            |typedef struct {
            |    const char *name;
            |    const char *ru;
            |    const char *zone;
            |} city_t;
            |
            |static const city_t CITIES[] = {
            |$rows
            |};
            |
            |#define CITY_COUNT ((int32_t)(sizeof CITIES / sizeof CITIES[0]))
            |""".trimMargin()
        if (check) {
            if (!header.isFile || header.readText() != text) {
                throw BuildError("${header.path} is not what ${json.name} makes: run tools/perinkpheral cities")
            }
            return
        }
        header.writeText(text)
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
        val files = (args["flash_files"] as Map<*, *>).entries.associate { (offset, path) ->
            (offset as String).removePrefix("0x").toLong(16) to File(build, path as String)
        }.toSortedMap()
        if (files.values.any { !it.isFile || it.length() == 0L } || files.values.distinctBy { it.name }.size != files.size) {
            throw BuildError("Flash files must be present, nonempty and have unique names.")
        }
        val migration = migrationFiles(files)

        out.mkdirs()
        val parts = files.map { (offset, source) ->
            source.copyTo(File(out, prefix + source.name), overwrite = true)
            linkedMapOf("path" to prefix + source.name, "offset" to offset)
        }
        val manifest = linkedMapOf(
            "name" to "perINKpheral",
            "version" to version,
            "new_install_prompt_erase" to true,
            "builds" to listOf(linkedMapOf("chipFamily" to "ESP32-C6", "parts" to parts)),
            "flash" to args["flash_settings"],
        )
        File(out, "${prefix}manifest.json").writeText(Json.pretty(manifest) + "\n")
        if (migration != null) {
            val migrationParts = parts.filter { it["offset"] as Long in migration }
            val migrationManifest = manifest + mapOf("new_install_prompt_erase" to false,
                "builds" to listOf(linkedMapOf("chipFamily" to "ESP32-C6", "parts" to migrationParts)))
            File(out, "${prefix}migration-manifest.json").writeText(Json.pretty(migrationManifest) + "\n")
        }
    }

    private data class Partition(val offset: Long, val size: Int, val type: Int, val subtype: Int)

    /** A migration is valid only for the recovery layout and the unchanged user data partitions. */
    private fun migrationFiles(files: Map<Long, File>): Set<Long>? {
        val table = files[0x8000L]?.takeIf { it.name == "partition-table.bin" && it.isFile }
            ?: throw BuildError("Flash layout must include partition-table.bin at 0x8000.")
        val raw = table.readBytes()
        val entries = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
        val partitions = buildMap {
            for (at in 0..raw.size - 32 step 32) {
                if (entries.getShort(at).toInt() != 0x50aa) break
                val name = raw.copyOfRange(at + 12, at + 28).toString(Charsets.US_ASCII).substringBefore('\u0000')
                put(name, Partition(entries.getInt(at + 4).toLong(), entries.getInt(at + 8),
                    raw[at + 2].toInt() and 0xff, raw[at + 3].toInt() and 0xff))
            }
        }
        val looksLikeRecovery = "factory" in partitions || 0x100000L in files ||
            files.values.any { it.name == "perink_recovery.bin" }
        if (!looksLikeRecovery) return null

        val layout = mapOf(
            "nvs" to Partition(0x9000, 0x6000, 1, 2),
            "otadata" to Partition(0xf000, 0x2000, 1, 0),
            "phy_init" to Partition(0x11000, 0x1000, 1, 1),
            "appkv" to Partition(0x12000, 0xe000, 1, 2),
            "factory" to Partition(0x20000, 0xe0000, 0, 0),
            "ota_0" to Partition(0x100000, 0x200000, 0, 0x10),
            "appcode" to Partition(0x300000, 0x50000, 1, 0x40),
            "apps" to Partition(0x350000, 0xa0000, 1, 0x83),
            "glyphs" to Partition(0x3f0000, 0x10000, 1, 0x41),
        )
        if (partitions != layout) throw BuildError("Partition table does not match the data-preserving recovery layout.")
        val required = mapOf(
            0L to "bootloader.bin",
            0x8000L to "partition-table.bin",
            0xf000L to "ota_data_initial.bin",
            0x20000L to "perink_recovery.bin",
            0x100000L to "perinkpheral.bin",
        )
        val firstInstall = required + (0x350000L to "apps.bin")
        if (files.mapValues { it.value.name } != firstInstall) {
            throw BuildError("Flash files do not match the required recovery and main image addresses.")
        }
        val capacities = mapOf(0L to 0x8000, 0x8000L to 0x1000) +
            listOf("otadata", "factory", "ota_0", "apps").associate { name ->
                val part = layout.getValue(name)
                part.offset to part.size
            }
        if (files.any { (offset, file) -> file.length() > capacities.getValue(offset) }) {
            throw BuildError("A flash file extends past its partition.")
        }
        val otaData = files.getValue(0xf000L).readBytes()
        if (otaData.size != layout.getValue("otadata").size || otaData.any { it != (-1).toByte() }) {
            throw BuildError("Flash manifest must start with blank OTA metadata.")
        }
        return required.keys
    }
}
