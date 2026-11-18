// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.BuildError
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * A video into a 1-bit stream small enough to be a file of an application. The video is scaled
 * down and cut to black and white. Every frame stands on its own, so a player can show any of
 * them without the ones before:
 *
 *  1. each row is replaced by what differs from the row above it - a shape then costs only
 *     where its outline moves sideways;
 *  2. the result, read row after row, is runs: so many pixels alike, so many that differ, so
 *     many alike - starting with alike;
 *  3. a run of N is written as the Elias gamma code of N + 1: as many zero bits as the number
 *     has after its first, then the number itself.
 *
 * The file, all numbers little-endian:
 *
 *     u16 width, u16 height, u16 frames, u16 frames per ten seconds
 *     u32 where each frame starts, and one more: where the last one ends
 *     the frames, each padded to a whole byte, bits from the top of a byte down
 *
 * The frames are ffmpeg's.
 */
object Video {
    private const val PLAYER_WIDTH_MAX = 400
    private const val PLAYER_FRAME_BYTES_MAX = 2048

    /** Every frame, true where the picture is dark. */
    private fun frames(video: File, width: Int, height: Int, fps: Double, threshold: Int): List<BooleanArray> {
        val process = ProcessBuilder("ffmpeg", "-v", "error", "-i", video.path, "-vf",
            "fps=${Fonts.sizeText(fps)},scale=$width:$height:flags=area", "-pix_fmt", "gray", "-f", "rawvideo", "-")
            .redirectError(ProcessBuilder.Redirect.INHERIT).start()
        val raw = process.inputStream.readBytes()
        if (process.waitFor() != 0) throw BuildError("ffmpeg could not read ${video.path}")
        val size = width * height
        return (0..raw.size - size step size).map { at -> BooleanArray(size) { (raw[at + it].toInt() and 0xFF) < threshold } }
    }

    private class BitWriter {
        private val bytes = ByteArrayOutputStream()
        private var current = 0
        private var count = 0

        private fun bit(value: Int) {
            current = (current shl 1) or value
            if (++count == 8) {
                bytes.write(current); current = 0; count = 0
            }
        }

        /** Elias gamma of a number that is at least 1. */
        fun gamma(number: Int) {
            val binary = Integer.toBinaryString(number)
            repeat(binary.length - 1) { bit(0) }
            binary.forEach { bit(it - '0') }
        }

        fun toBytes(): ByteArray {
            if (count > 0) bytes.write(current shl (8 - count))
            return bytes.toByteArray()
        }
    }

    private fun encode(frame: BooleanArray, width: Int): ByteArray {
        val writer = BitWriter()
        var runDiffers = false
        var runLength = 0
        frame.forEachIndexed { at, ink ->
            val differs = ink != (if (at >= width) frame[at - width] else false)
            if (differs != runDiffers) {
                writer.gamma(runLength + 1)
                runDiffers = differs
                runLength = 0
            }
            runLength++
        }
        writer.gamma(runLength + 1)
        return writer.toBytes()
    }

    fun make(video: File, out: File, width: Int, height: Int, fps: Double, threshold: Int): String {
        val frames = frames(video, width, height, fps, threshold)
        if (frames.isEmpty()) throw BuildError("no frames came out of ${video.path}")
        val encoded = frames.map { encode(it, width) }
        // What sdk players take: say so here, not on the device's screen.
        val largest = encoded.maxOf { it.size }
        if (width > PLAYER_WIDTH_MAX || largest > PLAYER_FRAME_BYTES_MAX) {
            throw BuildError("the player takes frames up to $PLAYER_WIDTH_MAX wide and $PLAYER_FRAME_BYTES_MAX bytes; " +
                "this is $width wide and $largest bytes: use a smaller picture")
        }
        val perTen = Math.rint(fps * 10).toInt()
        if (perTen < 1) throw BuildError("--fps is at least 0.1")
        val headerBytes = 8 + 4 * (frames.size + 1)
        val head = ByteBuffer.allocate(headerBytes).order(ByteOrder.LITTLE_ENDIAN)
            .putShort(width.toShort()).putShort(height.toShort()).putShort(frames.size.toShort()).putShort(perTen.toShort())
        var at = headerBytes
        for (frame in encoded) {
            head.putInt(at)
            at += frame.size
        }
        head.putInt(at)
        out.outputStream().use { stream ->
            stream.write(head.array())
            encoded.forEach { stream.write(it) }
        }
        return "${out.path}: ${frames.size} frames of ${width}x$height, $at bytes, the largest frame $largest"
    }
}
