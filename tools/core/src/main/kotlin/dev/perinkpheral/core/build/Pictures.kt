// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.build

import dev.perinkpheral.core.BuildError
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.Inflater

/**
 * Drawings into pictures an application carries in its code.
 * A drawing is a PNG, a PBM, or a text file of rows of '#' for ink and '.' for paper; a
 * text file may hold several, each after a line "name:". Of an image, what is darker than
 * the middle is ink, and what is transparent is paper. Every drawing becomes
 * `static const uint8_t PICTURE_<NAME>[]`: a byte of width, a byte of height, a bit a cell.
 */
object Pictures {
    private const val SIDE_MAX = 255
    private const val THRESHOLD = 128
    private val EXTENSIONS = listOf(".png", ".pbm", ".txt")

    private class Drawing(val name: String, val rows: List<BooleanArray>)

    private fun fail(path: String, why: String): Nothing = throw BuildError("$path: $why")

    /** Writes `out` from the drawings of `folder`; returns the constants it defines. */
    fun make(folder: File, out: File): List<String> {
        val files = folder.listFiles().orEmpty()
            .filter { file -> !file.name.startsWith(".") && EXTENSIONS.any { file.name.lowercase().endsWith(it) } }
            .sortedBy { it.name }
        val drawings = ArrayList<Drawing>()
        val seen = HashMap<String, String>()
        for (file in files) {
            for (drawing in read(file)) {
                val constant = constant(drawing.name)
                seen[constant]?.let { fail(file.path, "$constant is also in $it") }
                seen[constant] = file.path
                drawings += drawing
            }
        }
        if (drawings.isEmpty()) throw BuildError("${folder.path}: no drawings")
        out.parentFile.mkdirs()
        out.writeText(header(drawings))
        return drawings.map { constant(it.name) }
    }

    /** The drawings of files or folders as the build reads them: rows of '#' and '.'. */
    fun show(paths: List<File>): String = buildString {
        val files = paths.flatMap { path ->
            if (path.isDirectory) path.listFiles().orEmpty()
                .filter { file -> !file.name.startsWith(".") && EXTENSIONS.any { file.name.lowercase().endsWith(it) } }
                .sortedBy { it.name }
            else listOf(path)
        }
        for (file in files) {
            for (drawing in read(file)) {
                append(drawing.name).append(":\n")
                drawing.rows.forEach { row -> append(row.joinToString("") { if (it) "#" else "." }).append('\n') }
                append('\n')
            }
        }
    }

    private fun read(file: File): List<Drawing> {
        val path = file.path
        val stem = file.nameWithoutExtension
        val drawings: List<Pair<String?, List<BooleanArray>>> = try {
            when (file.extension.lowercase()) {
                "png" -> listOf(null to readPng(file))
                "pbm" -> listOf(null to readPbm(file))
                "txt" -> readText(file)
                else -> fail(path, "a drawing is one of ${EXTENSIONS.joinToString(", ")}")
            }
        } catch (error: RuntimeException) {
            // A file cut short or broken inside: what the reader stumbled on is no help to anybody.
            fail(path, "cannot be read: it is damaged or not what its name says")
        }
        if (drawings.isEmpty()) fail(path, "no drawing in it")
        return drawings.map { (name, rows) ->
            val width = rows[0].size
            if (rows.any { it.size != width }) fail(path, "${name ?: stem}: its rows are not of one length")
            if (width !in 1..SIDE_MAX || rows.size !in 1..SIDE_MAX) {
                fail(path, "${name ?: stem}: ${width}x${rows.size} cells, and a picture is 1 to $SIDE_MAX a side")
            }
            Drawing(name ?: stem, rows)
        }
    }

    /** The drawings of a text file, each under its name ("11:" for the host's icons), or under
     * the file's when it has none: the one reader of the format, the host's icons and mark
     * read by it as an application's pictures are. */
    fun drawingsOf(file: File): List<Pair<String, List<BooleanArray>>> =
        readText(file).map { (name, rows) -> (name ?: file.nameWithoutExtension) to rows }

    /** Rows as the firmware takes them: a bit a cell, the leftmost the highest, every row in
     * whole bytes. */
    fun bits(rows: List<BooleanArray>): ByteArray {
        val out = ByteArrayOutputStream()
        for (row in rows) {
            for (start in row.indices step 8) {
                var byte = 0
                for (i in 0 until minOf(8, row.size - start)) {
                    if (row[start + i]) byte = byte or (0x80 ushr i)
                }
                out.write(byte)
            }
        }
        return out.toByteArray()
    }

    private fun readText(file: File): List<Pair<String?, List<BooleanArray>>> {
        val drawings = ArrayList<Pair<String?, List<BooleanArray>>>()
        var name: String? = null
        var rows = ArrayList<BooleanArray>()
        file.readLines().forEachIndexed { index, raw ->
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith("//")) return@forEachIndexed
            if (Regex("[A-Za-z0-9_][A-Za-z0-9_-]*:").matches(line)) {
                if (rows.isNotEmpty()) drawings += name to rows
                name = line.dropLast(1)
                rows = ArrayList()
            } else if (line.all { it == '#' || it == '.' }) {
                rows += BooleanArray(line.length) { line[it] == '#' }
            } else {
                fail(file.path, "line ${index + 1}: a row is '#' and '.', a name ends with ':'")
            }
        }
        if (rows.isNotEmpty()) drawings += name to rows
        return drawings
    }

    private fun readPbm(file: File): List<BooleanArray> {
        val data = file.readBytes()
        // Comments out, then the kind, the width, the height and what follows.
        val text = String(data, Charsets.ISO_8859_1).replace(Regex("#[^\n]*"), "")
        val tokens = text.trimStart().split(Regex("\\s+"), limit = 4)
        val width = tokens[1].toInt()
        val height = tokens[2].toInt()
        return when (tokens[0]) {
            "P1" -> {
                val bits = tokens.getOrElse(3) { "" }.filter { it == '0' || it == '1' }.map { it == '1' }
                List(height) { y -> BooleanArray(width) { x -> bits[y * width + x] } }
            }
            "P4" -> {
                val body = tokens[3].toByteArray(Charsets.ISO_8859_1)
                val stride = (width + 7) / 8
                List(height) { y -> BooleanArray(width) { x -> body[y * stride + x / 8].toInt() and (0x80 ushr (x % 8)) != 0 } }
            }
            else -> fail(file.path, "a PBM is P1 or P4")
        }
    }

    private fun readPng(file: File): List<BooleanArray> {
        val path = file.path
        val data = file.readBytes()
        val signature = byteArrayOf(0x89.toByte(), 'P'.code.toByte(), 'N'.code.toByte(), 'G'.code.toByte(), 13, 10, 26, 10)
        if (data.size < 8 || !data.copyOfRange(0, 8).contentEquals(signature)) fail(path, "not a PNG")
        fun u32(at: Int) = (data[at].toInt() and 0xFF shl 24) or (data[at + 1].toInt() and 0xFF shl 16) or
            (data[at + 2].toInt() and 0xFF shl 8) or (data[at + 3].toInt() and 0xFF)
        var at = 8
        var header: ByteArray? = null
        var palette = ByteArray(0)
        var transparency = ByteArray(0)
        val packed = ByteArrayOutputStream()
        while (at + 8 <= data.size) {
            val length = u32(at)
            val kind = String(data, at + 4, 4, Charsets.ISO_8859_1)
            val body = data.copyOfRange(at + 8, at + 8 + length)
            at += 12 + length
            when (kind) {
                "IHDR" -> header = body
                "PLTE" -> palette = body
                "tRNS" -> transparency = body
                "IDAT" -> packed.write(body)
            }
        }
        val head = header ?: fail(path, "not a PNG")
        val width = ((head[0].toInt() and 0xFF) shl 24) or ((head[1].toInt() and 0xFF) shl 16) or
            ((head[2].toInt() and 0xFF) shl 8) or (head[3].toInt() and 0xFF)
        val height = ((head[4].toInt() and 0xFF) shl 24) or ((head[5].toInt() and 0xFF) shl 16) or
            ((head[6].toInt() and 0xFF) shl 8) or (head[7].toInt() and 0xFF)
        val depth = head[8].toInt() and 0xFF
        val colour = head[9].toInt() and 0xFF
        val interlace = head[12].toInt() and 0xFF
        if (interlace != 0) fail(path, "an interlaced PNG is not read: save it without interlacing")
        if (depth == 16) fail(path, "16 bits a channel are not read: save it with 8")
        val channels = when (colour) {
            0 -> 1; 2 -> 3; 3 -> 1; 4 -> 2; 6 -> 4
            else -> fail(path, "an unknown kind of PNG")
        }
        val widthBytes = (width * channels * depth + 7) / 8
        val raw = inflate(packed.toByteArray())
        val rows = unfilter(raw, widthBytes, maxOf(1, channels * depth / 8), height)

        fun sample(row: IntArray, index: Int): Int {
            if (depth == 8) return row[index]
            val value = (row[index * depth / 8] shr (8 - depth - index * depth % 8)) and ((1 shl depth) - 1)
            return if (colour == 3) value else value * 255 / ((1 shl depth) - 1)
        }

        fun isInk(row: IntArray, x: Int): Boolean {
            val red: Int
            val green: Int
            val blue: Int
            val alpha: Int
            when (colour) {
                3 -> {
                    val index = sample(row, x)
                    red = palette[index * 3].toInt() and 0xFF
                    green = palette[index * 3 + 1].toInt() and 0xFF
                    blue = palette[index * 3 + 2].toInt() and 0xFF
                    alpha = if (index < transparency.size) transparency[index].toInt() and 0xFF else 255
                }
                0, 4 -> {
                    red = sample(row, x * channels)
                    green = red
                    blue = red
                    alpha = if (colour == 4) sample(row, x * channels + 1) else 255
                }
                else -> {
                    red = sample(row, x * channels)
                    green = sample(row, x * channels + 1)
                    blue = sample(row, x * channels + 2)
                    alpha = if (colour == 6) sample(row, x * channels + 3) else 255
                }
            }
            return alpha >= 128 && (red * 299 + green * 587 + blue * 114) / 1000 < THRESHOLD
        }
        return rows.map { row -> BooleanArray(width) { x -> isInk(row, x) } }
    }

    private fun inflate(packed: ByteArray): ByteArray {
        val inflater = Inflater()
        inflater.setInput(packed)
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(65536)
        while (!inflater.finished()) {
            val count = inflater.inflate(buffer)
            if (count == 0 && (inflater.needsInput() || inflater.needsDictionary())) break
            out.write(buffer, 0, count)
        }
        inflater.end()
        return out.toByteArray()
    }

    /** The rows of a PNG with its line filters taken off. */
    private fun unfilter(raw: ByteArray, widthBytes: Int, pixelBytes: Int, height: Int): List<IntArray> {
        val rows = ArrayList<IntArray>()
        var before = IntArray(widthBytes)
        var at = 0
        repeat(height) {
            val kind = raw[at].toInt() and 0xFF
            val line = IntArray(widthBytes) { raw[at + 1 + it].toInt() and 0xFF }
            at += 1 + widthBytes
            for (i in 0 until widthBytes) {
                val left = if (i >= pixelBytes) line[i - pixelBytes] else 0
                val up = before[i]
                val corner = if (i >= pixelBytes) before[i - pixelBytes] else 0
                when (kind) {
                    1 -> line[i] = (line[i] + left) and 255
                    2 -> line[i] = (line[i] + up) and 255
                    3 -> line[i] = (line[i] + (left + up) / 2) and 255
                    4 -> {
                        val guess = left + up - corner
                        val toLeft = Math.abs(guess - left)
                        val toUp = Math.abs(guess - up)
                        val toCorner = Math.abs(guess - corner)
                        val nearest = if (toLeft <= toUp && toLeft <= toCorner) left else if (toUp <= toCorner) up else corner
                        line[i] = (line[i] + nearest) and 255
                    }
                }
            }
            rows += line
            before = line
        }
        return rows
    }

    private fun constant(name: String): String =
        "PICTURE_" + name.replace(Regex("[^A-Za-z0-9]+"), "_").trim('_').uppercase()

    private fun packed(rows: List<BooleanArray>): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(rows[0].size)
        out.write(rows.size)
        out.write(bits(rows))
        return out.toByteArray()
    }

    private fun header(drawings: List<Drawing>): String {
        val lines = mutableListOf("/* Generated by tools/perinkpheral: do not edit, change the drawings. */",
            "#pragma once", "", "#include <stdint.h>", "")
        for (drawing in drawings) {
            val data = packed(drawing.rows)
            lines += "/* ${drawing.name}: ${drawing.rows[0].size}x${drawing.rows.size}"
            drawing.rows.forEach { row -> lines += " *   " + row.joinToString("") { if (it) "#" else "." } }
            lines += " */"
            lines += "static const uint8_t ${constant(drawing.name)}[] = {"
            for (start in data.indices step 12) {
                lines += "    " + data.copyOfRange(start, minOf(start + 12, data.size))
                    .joinToString(" ") { "0x%02x,".format(it.toInt() and 0xFF) }
            }
            lines += listOf("};", "")
        }
        return lines.joinToString("\n")
    }
}
