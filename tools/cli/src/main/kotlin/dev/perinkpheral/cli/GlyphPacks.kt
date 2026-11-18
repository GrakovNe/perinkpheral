// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.Json
import java.awt.image.BufferedImage
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.imageio.ImageIO

/**
 * The glyph packs a device adds to its fonts: letters beyond ASCII. latin.glyphs,
 * cyrillic.glyphs and greek.glyphs hold, for every face of the device's fonts that has the
 * letters, their glyphs in the form the firmware's own fonts have (firmware/components/gfx:
 * gfx_pack_add). A face that lacks a letter leaves it out, and the device spells it in ASCII.
 * The packs and their letters are sdk/languages.json's.
 */
object GlyphPacks {
    /** The faces, as gfx_face_t numbers them, and the sizes the firmware's own fonts are made
     * at. Jersey 20 (face 3) is left out: the firmware uses it for the largest size alone, and
     * with it the packs do not fit their partition. */
    private val FACES = listOf(
        Triple(0, "DepartureMono-Regular.otf", 11.0),
        Triple(1, "Jersey10-Regular.ttf", 18.6667),
        Triple(2, "Jersey15-Regular.ttf", 27.0),
        Triple(4, "Jersey25-Regular.ttf", 41.0),
    )

    /** Jersey has no Cyrillic: its letters are made from its Latin ones. */
    private val MADE = setOf("Jersey10-Regular.ttf", "Jersey15-Regular.ttf", "Jersey25-Regular.ttf")

    /** Letters a face has under another code: the Kazakh schwa is the shape of the Latin one,
     * which Departure Mono has drawn. */
    private val SAME_AS = mapOf(0x04D8 to 0x018F, 0x04D9 to 0x0259)

    private const val DEGREE = 0x00B0 // the host draws its own, sized for every font
    private const val MAGIC = 0x594C4750
    private const val VERSION = 1
    private const val NAME_MAX = 11

    private fun packs(root: File): Map<String, List<Int>> {
        val ranges = (Json.parse(File(root, "sdk/languages.json").readText()) as Map<*, *>)["packs"] as Map<*, *>
        return ranges.entries.associate { (name, spans) ->
            name as String to (spans as List<*>).flatMap { span ->
                span as List<*>
                ((span[0] as Long).toInt()..(span[1] as Long).toInt()).toList()
            }.filter { it != DEGREE }
        }
    }

    private fun facePart(number: Int, font: File, px: Double, codePoints: List<Int>): Triple<ByteArray, List<Int>, Map<Int, FontFace.Glyph>> {
        val face = FontFace(font, px)
        val made = if (font.name in MADE) JerseyCyrillic(face::glyph).glyphs() else emptyMap()
        val entries = java.io.ByteArrayOutputStream()
        val bits = java.io.ByteArrayOutputStream()
        val missing = ArrayList<Int>()
        val drawn = LinkedHashMap<Int, FontFace.Glyph>()
        var count = 0
        for (cp in codePoints.toSortedSet()) {
            val glyph = face.glyph(cp) ?: made[cp] ?: SAME_AS[cp]?.let { face.glyph(it) }
            if (glyph == null) {
                missing += cp
                continue
            }
            entries.write(ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN)
                .putShort(cp.toShort()).put(glyph.width.toByte()).put(glyph.height.toByte())
                .put(glyph.left.toByte()).put(glyph.top.toByte()).put(glyph.advance.toByte()).put(0)
                .putInt(bits.size()).array())
            bits.write(Fonts.packRows(glyph.rows, glyph.width))
            drawn[cp] = glyph
            count++
        }
        while (bits.size() % 4 != 0) bits.write(0)
        val head = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
            .put(number.toByte()).put(0).putShort(count.toShort()).putInt(bits.size()).array()
        return Triple(head + entries.toByteArray() + bits.toByteArray(), missing, drawn)
    }

    /** Every glyph of a face in rows of sixteen, for a person to look at. */
    private fun sheet(file: File, drawn: Map<Int, FontFace.Glyph>, px: Int) {
        val cellWidth = px + 3
        val cellHeight = px + 5
        val perRow = 16
        val codes = drawn.keys.sorted()
        val rows = (codes.size + perRow - 1) / perRow
        val scale = 4
        val image = BufferedImage(cellWidth * perRow * scale, cellHeight * rows * scale, BufferedImage.TYPE_INT_RGB)
        val graphics = image.createGraphics()
        graphics.color = java.awt.Color(0xE9E7E1)
        graphics.fillRect(0, 0, image.width, image.height)
        graphics.color = java.awt.Color(0x141413)
        codes.forEachIndexed { index, cp ->
            val glyph = drawn.getValue(cp)
            val x0 = (index % perRow) * cellWidth + 1 + glyph.left
            val y0 = (index / perRow) * cellHeight + px - glyph.top
            for (y in 0 until glyph.height) for (x in 0 until glyph.width) {
                if (glyph.rows[y][x]) graphics.fillRect((x0 + x) * scale, (y0 + y) * scale, scale, scale)
            }
        }
        ImageIO.write(image, "png", file)
    }

    /** Writes the packs into `out`, and a picture of each face of each into `sheets`. */
    fun make(root: File, out: File, sheets: File?): List<String> {
        out.mkdirs()
        val said = ArrayList<String>()
        for ((name, codePoints) in packs(root)) {
            require(name.length <= NAME_MAX) { "$name: a pack's name is $NAME_MAX letters at most" }
            val parts = ArrayList<ByteArray>()
            for ((number, fontFile, px) in FACES) {
                val (part, missing, drawn) = facePart(number, File(root, "assets/fonts/$fontFile"), px, codePoints)
                if (drawn.isEmpty()) continue
                parts += part
                if (missing.isNotEmpty()) said += "  $fontFile lacks ${missing.size}: " + missing.take(12).joinToString(" ") { "U+%04X".format(it) }
                if (sheets != null) {
                    sheets.mkdirs()
                    sheet(File(sheets, "$name-face$number.png"), drawn, Math.rint(px).toInt())
                }
            }
            val body = parts.fold(ByteArray(0)) { all, part -> all + part }
            val total = 24 + body.size
            val nameBytes = name.toByteArray().copyOf(12)
            val header = ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(MAGIC).putShort(VERSION.toShort()).put(parts.size.toByte()).put(0).putInt(total).put(nameBytes).array()
            File(out, "$name.glyphs").writeBytes(header + body)
            said += "${File(out, "$name.glyphs").path}: $total bytes, ${parts.size} faces"
        }
        return said
    }
}
