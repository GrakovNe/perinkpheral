// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.app

import dev.perinkpheral.core.Json
import java.io.File

/**
 * The translations of applications as one table, for a person to read and correct. A row is
 * one text: the application's folder, the key ("manifest/..." for what the manifest says), the
 * English and the translation. Importing writes back every translation that was changed into
 * the strings/<language>.json it came from; keys and English are not touched. A folder of
 * applications is one whose subfolders have a `strings` folder each; an application's own
 * folder works too. Paths are as seen from where it was run.
 */
object StringsTable {
    private const val BOM = "﻿"

    private fun flat(texts: Map<*, *>, prefix: String = ""): Map<String, Any?> {
        val rows = LinkedHashMap<String, Any?>()
        for ((key, value) in texts) {
            if (value is Map<*, *>) rows.putAll(flat(value, "$prefix$key/")) else rows["$prefix$key"] = value
        }
        return rows
    }

    private fun applications(folders: List<File>): List<File> = folders.flatMap { folder ->
        (listOf(folder) + folder.listFiles().orEmpty().sortedBy { it.path })
            .filter { File(it, "strings/en.json").isFile }.map { it.absoluteFile }
    }

    /** What of the manifest a person reads, under the keys a translation has it. */
    private fun manifestEnglish(appDir: File): Map<String, Any?> {
        val manifest = Json.parse(File(appDir, "manifest.json").readText()) as Map<*, *>
        val words = linkedMapOf<String, Any?>("manifest/title" to (manifest["title"] ?: ""))
        for (setting in (manifest["settings"] as? List<*>).orEmpty()) {
            setting as Map<*, *>
            val at = "manifest/settings/${setting["key"]}/"
            for (part in listOf("title", "help")) if (part in setting) words[at + part] = setting[part]
            for (option in (setting["options"] as? List<*>).orEmpty()) words["${at}options/$option"] = option
        }
        return words
    }

    private fun relative(file: File): String =
        File("").absoluteFile.toPath().relativize(file.toPath()).toString().ifEmpty { "." }

    fun export(language: String, out: File, folders: List<File>) {
        val rows = mutableListOf(listOf("application", "key", "en", language))
        for (appDir in applications(folders)) {
            val english = LinkedHashMap(flat(Json.parse(File(appDir, "strings/en.json").readText()) as Map<*, *>))
            english.putAll(manifestEnglish(appDir))
            val path = File(appDir, "strings/$language.json")
            val translated = if (path.exists()) flat(Json.parse(path.readText()) as Map<*, *>) else emptyMap()
            for ((key, text) in english) {
                rows += listOf(relative(appDir), key, text.toString(), (translated[key] ?: "").toString())
            }
        }
        out.writeText(BOM + rows.joinToString("") { row -> row.joinToString(",") { quote(it) } + "\r\n" })
    }

    /** A field as Python's csv module writes it: quoted when it has a comma, a quote or a line break. */
    private fun quote(field: String): String =
        if (field.any { it == ',' || it == '"' || it == '\n' || it == '\r' }) "\"" + field.replace("\"", "\"\"") + "\""
        else field

    private fun parse(text: String): List<List<String>> {
        val rows = ArrayList<List<String>>()
        var row = ArrayList<String>()
        val field = StringBuilder()
        var inQuotes = false
        var at = 0
        while (at < text.length) {
            val character = text[at++]
            when {
                inQuotes && character == '"' && at < text.length && text[at] == '"' -> { field.append('"'); at++ }
                character == '"' -> inQuotes = !inQuotes
                !inQuotes && character == ',' -> { row += field.toString(); field.clear() }
                !inQuotes && (character == '\n' || character == '\r') -> {
                    if (character == '\r' && at < text.length && text[at] == '\n') at++
                    row += field.toString()
                    field.clear()
                    rows += row
                    row = ArrayList()
                }
                else -> field.append(character)
            }
        }
        if (field.isNotEmpty() || row.isNotEmpty()) {
            row += field.toString()
            rows += row
        }
        return rows
    }

    private fun put(texts: MutableMap<String, Any?>, key: String, value: String): Boolean {
        var parts = key.split("/")
        // An option's own text may have a slash in it: the key is what follows "options/".
        val options = parts.indexOf("options")
        if (options >= 0) parts = parts.subList(0, options + 1) + listOf(parts.drop(options + 1).joinToString("/"))
        var level = texts
        for (part in parts.dropLast(1)) {
            @Suppress("UNCHECKED_CAST")
            level = level.getOrPut(part) { LinkedHashMap<String, Any?>() } as MutableMap<String, Any?>
        }
        if (level[parts.last()] == value) return false
        level[parts.last()] = value
        return true
    }

    /** Takes the translations of `source` back; returns what it changed, a line a file. */
    fun import(language: String, source: File): List<String> {
        val rows = parse(source.readText().removePrefix(BOM))
        if (rows.isEmpty()) return emptyList()
        val header = rows[0]
        val changed = sortedMapOf<String, Pair<MutableMap<String, Any?>, Int>>()
        for (row in rows.drop(1)) {
            val cells = header.zip(row).toMap()
            val text = cells[language]
            if (text.isNullOrEmpty()) continue
            val path = File(cells["application"].orEmpty(), "strings/$language.json")
            if (!path.exists()) continue // a row somebody added: the host's own words, a note
            val (texts, count) = changed.getOrPut(path.path) {
                @Suppress("UNCHECKED_CAST")
                (LinkedHashMap(Json.parse(path.readText()) as Map<String, Any?>) as MutableMap<String, Any?>) to 0
            }
            changed[path.path] = texts to count + if (put(texts, cells["key"].orEmpty(), text)) 1 else 0
        }
        return changed.filter { it.value.second > 0 }.map { (path, entry) ->
            File(path).writeText(Json.pretty(entry.first) + "\n")
            "$path: ${entry.second} changed"
        }
    }
}
