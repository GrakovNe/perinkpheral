// SPDX-License-Identifier: MIT
package org.perinkpheral.core.device

import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.IOException
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * A device driven through its development console over USB, for one with no network: keys
 * typed one by one (1-4 a short press of that button, q/w/e/r a long press of 1-4), then the
 * frame its panel shows, asked for with 'p'. Needs a firmware built with APP_DEV_CONSOLE. The
 * port is set up with stty and read as a file: macOS and Linux.
 */
class UsbConsole(private val port: String) : AutoCloseable {
    private val lines = LinkedBlockingQueue<String>()
    private val input: FileInputStream
    private val output: FileOutputStream

    init {
        // The USB Serial/JTAG takes any speed; raw, so that nothing is translated on the way.
        val flag = if (System.getProperty("os.name").lowercase().contains("mac")) "-f" else "-F"
        val stty = ProcessBuilder("stty", flag, port, "115200", "raw", "-echo").redirectErrorStream(true).start()
        val said = stty.inputStream.bufferedReader().readText()
        if (stty.waitFor() != 0) throw IOException("stty $port: ${said.trim()}")
        input = FileInputStream(File(port))
        output = FileOutputStream(File(port))
        val reader = Thread({
            try {
                input.bufferedReader(Charsets.US_ASCII).forEachLine { lines.put(it) }
            } catch (error: IOException) {
                // the port was closed
            }
        }, "perINKpheral USB console")
        reader.isDaemon = true
        reader.start()
    }

    fun type(key: Char) {
        output.write(key.code)
        output.flush()
    }

    /** The frame the panel shows, as rows (true: ink). */
    fun frame(): List<BooleanArray> {
        lines.clear()
        type(DUMP_FRAME_KEY)
        val deadline = System.currentTimeMillis() + FRAME_TIMEOUT_MS
        var width = -1
        var height = -1
        val rows = ArrayList<BooleanArray>()
        while (System.currentTimeMillis() < deadline) {
            val line = lines.poll(deadline - System.currentTimeMillis(), TimeUnit.MILLISECONDS)?.trim() ?: break
            when {
                line.startsWith("FRAME BEGIN") -> {
                    val parts = line.split(Regex("\\s+"))
                    width = parts[2].toInt()
                    height = parts[3].toInt()
                    rows.clear()
                }
                line == "FRAME END" -> {
                    if (width < 0 || rows.size != height) throw IOException("the frame came through incomplete: ${rows.size} rows")
                    return rows
                }
                width > 0 && line.length == width / 8 * 2 -> {
                    val bytes = ByteArray(line.length / 2) { line.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
                    // In the frame a set bit is white.
                    rows += BooleanArray(width) { x -> (bytes[x shr 3].toInt() shr (7 - (x and 7))) and 1 == 0 }
                }
            }
        }
        throw IOException("no frame from the device: is the firmware built with APP_DEV_CONSOLE?")
    }

    override fun close() {
        input.close()
        output.close()
    }

    companion object {
        private const val DUMP_FRAME_KEY = 'p'
        private const val FRAME_TIMEOUT_MS = 10_000L
    }
}
