// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.simulator

import java.io.File

/**
 * The port a simulator answers on when nobody has to know it beforehand: started with
 * `--port 0` it binds whichever the system gives and writes the number to `--port-file`.
 * A port chosen first and bound later can be taken in between - by another simulator, or by
 * any program of the computer.
 */
object SimulatorPort {
    /** What `--port` is given for a port of the system's choice. */
    const val ANY = 0

    /** The port in the file, or null while the simulator has not written it. The simulator
     * renames the file into place whole; anything else found there is not a port yet. */
    fun read(file: File): Int? {
        val text = try {
            file.readText()
        } catch (error: java.io.IOException) {
            return null
        }
        // The line's end is written with the number: without it the file is not whole.
        if (!text.endsWith("\n")) return null
        return text.trim().toIntOrNull()?.takeIf { it in 1..65535 }
    }
}
