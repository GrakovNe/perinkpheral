// SPDX-License-Identifier: MIT
package org.perinkpheral.core.device

import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.io.File
import java.io.IOException
import java.util.zip.CRC32
import java.util.zip.Deflater

/** The frame of the panel: from the device's 1-bit BMP, and into a PNG for a person. */
object Bmp {
    private const val HEADER_BYTES = 62
    private const val SIDE_MAX = 4096
    const val PNG_SCALE = 2
    private val INK = byteArrayOf(0x14, 0x14, 0x13)
    private val PAPER = byteArrayOf(0xE9.toByte(), 0xE7.toByte(), 0xE1.toByte())

    private fun little(bytes: ByteArray, at: Int): Int =
        (bytes[at].toInt() and 0xFF) or (bytes[at + 1].toInt() and 0xFF shl 8) or
            (bytes[at + 2].toInt() and 0xFF shl 16) or (bytes[at + 3].toInt() and 0xFF shl 24)

    /** A 1-bit BMP as rows, true where there is ink (colour 0). */
    fun rows(bmp: ByteArray): List<BooleanArray> {
        if (bmp.size < HEADER_BYTES) throw IOException("the frame came through cut short: ${bmp.size} bytes")
        val pixelsAt = little(bmp, 10)
        val width = little(bmp, 18)
        val height = little(bmp, 22)
        val rowBytes = (width + 31) / 32 * 4
        if (width !in 1..SIDE_MAX || height !in 1..SIDE_MAX || pixelsAt + height * rowBytes > bmp.size) {
            throw IOException("the frame came through cut short: ${bmp.size} bytes")
        }
        // Stored from the bottom up.
        return (height - 1 downTo 0).map { row ->
            val at = pixelsAt + row * rowBytes
            BooleanArray(width) { x -> (bmp[at + (x shr 3)].toInt() shr (7 - (x and 7))) and 1 == 0 }
        }
    }

    /** Writes the rows as a PNG, each pixel `scale` pixels a side, in the panel's colours. */
    fun writePng(file: File, rows: List<BooleanArray>, scale: Int = PNG_SCALE) {
        val width = rows[0].size * scale
        val height = rows.size * scale
        val raw = ByteArrayOutputStream()
        for (row in rows) {
            val line = ByteArrayOutputStream()
            for (isInk in row) repeat(scale) { line.write(if (isInk) INK else PAPER) }
            repeat(scale) {
                raw.write(0)
                raw.write(line.toByteArray())
            }
        }
        val out = ByteArrayOutputStream()
        out.write(byteArrayOf(0x89.toByte(), 'P'.code.toByte(), 'N'.code.toByte(), 'G'.code.toByte(), 13, 10, 26, 10))
        fun chunk(tag: String, body: ByteArray) {
            val data = DataOutputStream(out)
            data.writeInt(body.size)
            val tagged = tag.toByteArray() + body
            data.write(tagged)
            data.writeInt(CRC32().apply { update(tagged) }.value.toInt())
        }
        val header = ByteArrayOutputStream()
        DataOutputStream(header).apply {
            writeInt(width)
            writeInt(height)
            write(byteArrayOf(8, 2, 0, 0, 0))
        }
        chunk("IHDR", header.toByteArray())
        val deflater = Deflater(9)
        deflater.setInput(raw.toByteArray())
        deflater.finish()
        val packed = ByteArrayOutputStream()
        val buffer = ByteArray(65536)
        while (!deflater.finished()) packed.write(buffer, 0, deflater.deflate(buffer))
        deflater.end()
        chunk("IDAT", packed.toByteArray())
        chunk("IEND", ByteArray(0))
        file.absoluteFile.parentFile?.mkdirs()
        file.writeBytes(out.toByteArray())
    }
}
