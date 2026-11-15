// SPDX-License-Identifier: MIT
package org.perinkpheral.core.firmware

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.device.Bmp
import java.io.File
import java.nio.file.Files

/**
 * The host's screens as the panel would show them, before flashing: tools/preview, the
 * firmware's own drawing compiled for this computer, writes a PBM a screen, and each becomes
 * a PNG in the panel's colours.
 */
object Preview {
    /** Renders the screens into `out`, in `family` ("jersey" for the other one); returns the PNGs. */
    fun render(root: File, out: File, family: String?): List<File> {
        val folder = File(root, "tools/preview")
        if (ProcessBuilder("make", "-s", "-C", folder.path).inheritIO().start().waitFor() != 0) {
            throw BuildError("tools/preview could not be built: it needs a C compiler")
        }
        val made = Files.createTempDirectory("perinkpheral-preview").toFile()
        try {
            val command = mutableListOf(File(folder, "build/perinkpheral-preview").path, made.path)
            if (family != null) command += family
            if (ProcessBuilder(command).inheritIO().start().waitFor() != 0) throw BuildError("the preview failed")
            out.mkdirs()
            return made.listFiles().orEmpty().filter { it.name.endsWith(".pbm") }.sortedBy { it.name }.map { pbm ->
                val png = File(out, pbm.nameWithoutExtension + ".png")
                Bmp.writePng(png, rows(pbm))
                png
            }
        } finally {
            made.deleteRecursively()
        }
    }

    /** A PBM of the kind the preview writes: P4, a set bit is ink. */
    private fun rows(file: File): List<BooleanArray> {
        val data = file.readBytes()
        var at = 0
        fun word(): String {
            while (data[at].toInt().toChar().isWhitespace()) at++
            val start = at
            while (!data[at].toInt().toChar().isWhitespace()) at++
            return String(data, start, at - start, Charsets.US_ASCII)
        }
        if (word() != "P4") throw BuildError("${file.path}: not a PBM of the preview")
        val width = word().toInt()
        val height = word().toInt()
        at++ // the one whitespace before the bits
        val stride = (width + 7) / 8
        return List(height) { y ->
            BooleanArray(width) { x -> (data[at + y * stride + x / 8].toInt() shr (7 - x % 8)) and 1 == 1 }
        }
    }
}
