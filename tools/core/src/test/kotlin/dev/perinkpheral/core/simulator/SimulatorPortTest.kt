// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.simulator

import java.io.File
import java.nio.file.Files

object SimulatorPortTest {
    @JvmStatic fun main(args: Array<String>) {
        val folder = Files.createTempDirectory("perinkpheral-port-test").toFile()
        try {
            val file = File(folder, "sim.port")
            check(SimulatorPort.read(file) == null) { "no file is no port" }
            check(SimulatorPort.read(folder) == null) { "a folder is no port" }
            file.writeText("")
            check(SimulatorPort.read(file) == null) { "an empty file is no port" }
            // Half a number, should a simulator ever write in place: not taken for the port 5.
            file.writeText("5")
            check(SimulatorPort.read(file) == null) { "a number without its line's end is not whole" }
            file.writeText("54321\n")
            check(SimulatorPort.read(file) == 54321)
            for (wrong in listOf("0\n", "65536\n", "-1\n", "port\n", "12 34\n")) {
                file.writeText(wrong)
                check(SimulatorPort.read(file) == null) { "${wrong.trim()} is no port" }
            }
            file.writeText("65535\n")
            check(SimulatorPort.read(file) == 65535)
            check(SimulatorPort.ANY == 0) { "the simulator takes 0 for a port of the system's choice" }
        } finally {
            folder.deleteRecursively()
        }
        println("simulator port: ok")
    }
}
