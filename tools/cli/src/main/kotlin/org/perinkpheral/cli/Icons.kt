// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.build.Pictures
import org.perinkpheral.core.contract.Contract
import java.awt.image.BufferedImage
import java.io.File
import javax.imageio.ImageIO

/**
 * The host's icons and the project's mark, from drawings a person can type.
 *
 * Every icon is a file of firmware/components/gfx/icons: one drawing or more, each on a square
 * canvas of 11, 22 or 44 cells, under a line that says how many ("22:"), rows of `#` for ink
 * and `.` for what was there - read by the core's reader of drawings, as an application's
 * pictures are. An icon is drawn from the finest drawing it can draw at two pixels a cell or
 * more - the eleven at 22 pixels, the 22 at 44, the 44 at 88 and 99 - so a line of a drawing is
 * two of its cells at least. Every icon has a drawing of 11 cells, drawn by hand: one made from
 * a larger drawing blots at the sizes it is drawn at. One of 44 and none of 22 gets the 22 made
 * from it, a cell wherever there is ink under it, which holds at 44 pixels and over.
 *
 * Their order is the contract's: HOST_ICON_* of sdk/host_api.h, by number.
 */
object Icons {
    private val SIZES = listOf(11, 22, 44)
    private val INK = 0xFF141413.toInt()
    private val PAPER = 0xFFE9E7E1.toInt()
    private const val MARK_MARGIN = 2 // cells of paper round the mark
    private const val MARK_PIXELS = 512

    private fun icons(root: File) = File(root, "firmware/components/gfx/icons")

    /** The icons' names, lower case, in the order of their numbers in the contract. */
    private fun order(contract: Contract): List<String> {
        val count = contract.number("HOST_ICON_COUNT")
        val numbered = contract.namesStartingWith("HOST_ICON_").mapNotNull { name ->
            contract.numberOrNull("HOST_ICON_$name")?.takeIf { it in 0 until count }?.let { it to name }
        }.filter { (_, name) -> name !in setOf("COUNT", "CELLS", "CELL_MAX") }.sortedBy { it.first }
        if (numbered.size.toLong() != count || numbered.map { it.first } != (0 until count).toList()) {
            throw BuildError("sdk/host_api.h: HOST_ICON_* are not numbered 0 to ${count - 1}, one each")
        }
        return numbered.map { it.second.lowercase() }
    }

    /** An icon's drawings by their sizes, the 22 made of the 44 when it has none of its own. */
    private fun read(file: File): Map<Int, List<BooleanArray>> {
        val drawings = LinkedHashMap<Int, List<BooleanArray>>()
        for ((name, rows) in Pictures.drawingsOf(file)) {
            val size = name.toIntOrNull()
            if (size == null || size !in SIZES || size in drawings) throw BuildError("${file.name}: a drawing of $name cells")
            if (rows.size != size || rows.any { it.size != size }) throw BuildError("${file.name}: the drawing of $size is not $size by $size")
            drawings[size] = rows
        }
        if (11 !in drawings) throw BuildError("${file.name}: no drawing of 11 cells")
        if (22 !in drawings && 44 in drawings) drawings[22] = smaller(drawings.getValue(44), 22)
        return drawings
    }

    /** The drawing on `size` cells: a cell is ink wherever what it covers has some. */
    private fun smaller(rows: List<BooleanArray>, size: Int): List<BooleanArray> {
        val step = rows.size / size
        return List(size) { r -> BooleanArray(size) { c -> (0 until step).any { i -> (0 until step).any { j -> rows[r * step + i][c * step + j] } } } }
    }

    /** firmware/components/gfx/gfx_icon_art.c: the drawings packed a bit a cell, row by row,
     * and for every icon where each of its drawings is. Returns the bytes of drawings. */
    fun make(root: File, contract: Contract): Int {
        val folder = icons(root)
        val order = order(contract)
        val strays = folder.listFiles().orEmpty().filter { it.name.endsWith(".txt") }.map { it.nameWithoutExtension }.toSet() - order.toSet()
        if (strays.isNotEmpty()) throw BuildError("drawings of icons the contract has no HOST_ICON_ for: ${strays.sorted()}")
        val data = java.io.ByteArrayOutputStream()
        val entries = order.map { name ->
            val file = File(folder, "$name.txt")
            if (!file.isFile) throw BuildError("${file.path} is missing")
            val drawings = read(file)
            val places = SIZES.map { size ->
                drawings[size]?.let { rows -> "{ $size, ${data.size()} }".also { data.write(Pictures.bits(rows)) } } ?: "{ 0, 0 }"
            }
            "    [GFX_ICON_${name.uppercase()}] = { { ${places.joinToString(", ")} } },"
        }
        val bytes = data.toByteArray()
        if (bytes.size > 0xFFFF) throw BuildError("the drawings outgrow a 16-bit offset")
        val lines = bytes.toList().chunked(16).joinToString("\n") { chunk ->
            "    " + chunk.joinToString(", ") { "0x%02x".format(it.toInt() and 0xFF) } + ","
        }
        File(root, "firmware/components/gfx/gfx_icon_art.c").writeText(
            "/*\n * SPDX-License-Identifier: MIT\n * Copyright (c) 2026 GrakovNe\n *\n" +
                " * Made by tools/perinkpheral icons from firmware/components/gfx/icons:\n" +
                " * change the drawings there and run it again, not this file.\n */\n" +
                "#include \"gfx_icon_art.h\"\n\n" +
                "const uint8_t GFX_ICON_BITS[${bytes.size}] = {\n$lines\n};\n\n" +
                "const gfx_icon_art_t GFX_ICON_ART[GFX_ICON_COUNT] = {\n${entries.joinToString("\n")}\n};\n")
        return bytes.size
    }

    // ---- the mark: the update icon's drawing of eleven cells ----

    /** Runs of ink across a row, the same one-cell run in the rows under it joined. */
    private fun rectangles(rows: List<BooleanArray>): List<IntArray> {
        val joined = ArrayList<IntArray>()
        rows.forEachIndexed { y, row ->
            var x = 0
            while (x < row.size) {
                if (!row[x]) { x++; continue }
                var end = x
                while (end < row.size && row[end]) end++
                val below = joined.firstOrNull { it[0] == x && it[2] == end - x && it[1] + it[3] == y && it[2] == 1 }
                if (below != null) below[3]++ else joined += intArrayOf(x, y, end - x, 1)
                x = end
            }
        }
        return joined
    }

    private fun svg(rects: List<IntArray>, size: Int, offset: Int, paper: Boolean): String = buildString {
        append("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 $size $size\" shape-rendering=\"crispEdges\">")
        if (paper) append("<rect width=\"$size\" height=\"$size\" fill=\"#E9E7E1\"/>")
        append("<g fill=\"#141413\">")
        rects.forEach { (x, y, w, h) -> append("<rect x=\"${x + offset}\" y=\"${y + offset}\" width=\"$w\" height=\"$h\"/>") }
        append("</g></svg>\n")
    }

    /** The cells, `size` a side with the mark in the middle, each pixel the cell under it. */
    private fun png(rows: List<BooleanArray>, size: Int, pixels: Int, paper: Boolean, out: File) {
        val offset = (size - rows.size) / 2
        val image = BufferedImage(pixels, pixels, BufferedImage.TYPE_INT_ARGB)
        for (py in 0 until pixels) {
            val cy = py * size / pixels - offset
            for (px in 0 until pixels) {
                val cx = px * size / pixels - offset
                val isInk = cy in rows.indices && cx in rows[cy].indices && rows[cy][cx]
                image.setRGB(px, py, if (isInk) INK else if (paper) PAPER else 0)
            }
        }
        ImageIO.write(image, "png", out)
    }

    /**
     * assets/mark: mark.svg (the cells bare), mark-on-paper.svg (on paper, two cells round it),
     * mark-512.png and mark-on-paper-512.png - and `onPaper`, the mark on paper at other sizes.
     * Returns the cells as rectangles, for the copies that stand in code.
     */
    fun mark(root: File, onPaper: List<Pair<Int, File>> = emptyList()): List<IntArray> {
        val drawing = Pictures.drawingsOf(File(icons(root), "update.txt")).firstOrNull { it.first == "11" }?.second
            ?: throw BuildError("update.txt has no drawing of eleven cells")
        val rects = rectangles(drawing)
        val out = File(root, "assets/mark").apply { mkdirs() }
        val withPaper = drawing.size + 2 * MARK_MARGIN
        File(out, "mark.svg").writeText(svg(rects, drawing.size, 0, false))
        File(out, "mark-on-paper.svg").writeText(svg(rects, withPaper, MARK_MARGIN, true))
        png(drawing, drawing.size, MARK_PIXELS, false, File(out, "mark-512.png"))
        png(drawing, withPaper, MARK_PIXELS, true, File(out, "mark-on-paper-512.png"))
        onPaper.forEach { (pixels, file) -> png(drawing, withPaper, pixels, true, file) }
        return rects
    }
}
