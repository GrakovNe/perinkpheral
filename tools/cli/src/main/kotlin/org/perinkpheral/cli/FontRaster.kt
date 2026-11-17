// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

import org.apache.fontbox.ttf.OTFParser
import org.apache.fontbox.ttf.OpenTypeFont
import org.apache.fontbox.ttf.TTFParser
import org.apache.fontbox.ttf.TrueTypeFont
import org.apache.pdfbox.io.RandomAccessReadBufferedFile
import org.perinkpheral.core.BuildError
import java.awt.geom.GeneralPath
import java.awt.geom.Path2D
import java.awt.geom.PathIterator
import java.io.File

/**
 * A font's glyphs one bit a pixel, as the panel shows them: the outline scaled as FreeType
 * scales it - 26.6 fixed point, the scale in 16.16 - and a pixel ink when its centre is inside.
 * The fonts are drawn for the pixel grid, their outlines on it at the sizes they are made at,
 * so a centre is never on an edge; a TrueType face's points are put on the grid, as its
 * instructions have FreeType do. Every glyph of the five faces and of the three glyph packs
 * came out as FreeType drew them, to the byte, when this took its place.
 */
class FontFace(file: File, private val px: Double) {
    private val font: TrueTypeFont = RandomAccessReadBufferedFile(file).let {
        if (file.name.lowercase().endsWith(".otf")) OTFParser().parse(it) else TTFParser().parse(it)
    }
    private val upem = font.header.unitsPerEm
    private val cmap = font.getUnicodeCmapLookup(false) ?: throw BuildError("${file.name}: no Unicode cmap")
    private val scale: Long

    init {
        // In 26.6 at 72 dots an inch a point is a pixel, a fraction of one too: a face whose
        // own grid is so many pixels an em is made at that.
        scale = divFix(Math.round(px * 64), upem.toLong())
    }

    /** Ascent and descent in pixels, as the firmware's fonts give them. */
    val ascent: Int get() = (ceil64(mulFix(font.horizontalHeader.ascender.toLong(), scale)) shr 6).toInt()
    val descent: Int get() = (-((floor64(mulFix(font.horizontalHeader.descender.toLong(), scale)) - 32) shr 6)).toInt()

    class Glyph(val width: Int, val height: Int, val left: Int, val top: Int, val advance: Int, val rows: List<BooleanArray>)

    fun hasGlyph(codePoint: Int): Boolean = cmap.getGlyphId(codePoint) != 0

    fun glyph(codePoint: Int): Glyph? {
        val gid = cmap.getGlyphId(codePoint)
        if (gid == 0) return null
        val advance = ((mulFix(font.getAdvanceWidth(gid).toLong(), scale) + 32) and 63L.inv()) shr 6
        val path = outline(gid) ?: return empty(advance.toInt())
        // The points in 26.6, y down; the box of every point, control points too.
        val scaled = Path2D.Double(Path2D.WIND_NON_ZERO)
        var minX = Long.MAX_VALUE; var minY = Long.MAX_VALUE; var maxX = Long.MIN_VALUE; var maxY = Long.MIN_VALUE
        val coords = DoubleArray(6)
        val iterator = path.getPathIterator(null)
        // TrueType's instructions in these faces put every point on the pixel grid; their
        // outlines are on it already, but for a mark of a contour some glyphs carry under the
        // line to set their box - so the box is the grid's, as FreeType's is.
        val snaps = !(font is OpenTypeFont && font.isPostScript)
        fun grid(value: Long) = if (snaps) (value + 32) and 63L.inv() else value
        fun point(i: Int): Pair<Long, Long> {
            val x = grid(mulFix(Math.round(coords[i * 2]), scale))
            val y = grid(mulFix(Math.round(coords[i * 2 + 1]), scale))
            minX = minOf(minX, x); maxX = maxOf(maxX, x); minY = minOf(minY, y); maxY = maxOf(maxY, y)
            return x to y
        }
        while (!iterator.isDone) {
            when (iterator.currentSegment(coords)) {
                PathIterator.SEG_MOVETO -> point(0).let { (x, y) -> scaled.moveTo(x.toDouble(), -y.toDouble()) }
                PathIterator.SEG_LINETO -> point(0).let { (x, y) -> scaled.lineTo(x.toDouble(), -y.toDouble()) }
                PathIterator.SEG_QUADTO -> {
                    val (x1, y1) = point(0); val (x2, y2) = point(1)
                    scaled.quadTo(x1.toDouble(), -y1.toDouble(), x2.toDouble(), -y2.toDouble())
                }
                PathIterator.SEG_CUBICTO -> {
                    val (x1, y1) = point(0); val (x2, y2) = point(1); val (x3, y3) = point(2)
                    scaled.curveTo(x1.toDouble(), -y1.toDouble(), x2.toDouble(), -y2.toDouble(), x3.toDouble(), -y3.toDouble())
                }
                PathIterator.SEG_CLOSE -> scaled.closePath()
            }
            iterator.next()
        }
        if (minX == Long.MAX_VALUE) return empty(advance.toInt())
        val left = floor64(minX); var right = ceil64(maxX); val bottom = floor64(minY); var top = ceil64(maxY)
        // A monochrome bitmap is a pixel a side at least, as FreeType makes it.
        if (right == left) right += 64
        if (top == bottom) top += 64
        val width = ((right - left) shr 6).toInt(); val height = ((top - bottom) shr 6).toInt()
        val rows = List(height) { row ->
            BooleanArray(width) { column ->
                // The centre of the pixel, in 26.6 and y down.
                scaled.contains(left + column * 64.0 + 32, -top + row * 64.0 + 32)
            }
        }
        return Glyph(width, height, (left shr 6).toInt(), (top shr 6).toInt(), advance.toInt(), rows)
    }

    /** A glyph with no ink - the space: FreeType makes it a blank pixel on the baseline. */
    private fun empty(advance: Int) = Glyph(1, 1, 0, 1, advance, listOf(BooleanArray(1)))

    private fun outline(gid: Int): GeneralPath? =
        if (font is OpenTypeFont && font.isPostScript) {
            val cff = font.cff.font
            (cff as? org.apache.fontbox.cff.CFFType1Font)?.getType2CharString(gid)?.path
                ?: (cff as? org.apache.fontbox.cff.CFFCIDFont)?.getType2CharString(gid)?.path
        } else {
            font.glyph.getGlyph(gid)?.path
        }

    companion object {
        /** FreeType's FT_MulFix: a * b / 65536, rounded, the sign apart. */
        fun mulFix(a: Long, b: Long): Long {
            val negative = (a < 0) != (b < 0)
            val product = (Math.abs(a) * Math.abs(b) + 0x8000) shr 16
            return if (negative) -product else product
        }

        /** FreeType's FT_DivFix: a * 65536 / b, rounded. */
        fun divFix(a: Long, b: Long): Long {
            val negative = (a < 0) != (b < 0)
            val quotient = (Math.abs(a) * 65536 + Math.abs(b) / 2) / Math.abs(b)
            return if (negative) -quotient else quotient
        }

        fun floor64(x: Long): Long = x and 63L.inv()
        fun ceil64(x: Long): Long = (x + 63) and 63L.inv()
    }
}
