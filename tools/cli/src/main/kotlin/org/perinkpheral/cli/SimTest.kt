// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.Json
import org.perinkpheral.core.app.Applications
import org.perinkpheral.core.build.AppBuilder
import org.perinkpheral.core.device.Bmp
import org.perinkpheral.core.device.DeviceButton
import org.perinkpheral.core.device.DeviceClient
import org.perinkpheral.core.simulator.SimulatorNetwork
import java.io.File
import java.net.ServerSocket
import java.nio.file.Files
import javax.imageio.ImageIO

/**
 * Applications run in the simulator and their screens held against pictures that were looked at
 * and kept: what CI runs on every push, and what a person runs before one.
 *
 * A test is a folder with scenario.json and the pictures it expects, PNGs of the frame:
 *
 *     {
 *       "app": "apps/weather",              the application, from where the tests are run
 *       "at": 1794832200,                    the clock's start, Unix seconds
 *       "tz": "Europe/Moscow",               the computer's zone for the run
 *       "font": "departure", "language": "en", "clock": false, "glyphs": "glyphs",
 *       "store": { "lat": "59.93" },         the application's store before it starts
 *       "mocks": { "https://api...": { "body": "..." } },   the network, as `sim --mocks` takes it
 *       "steps": [ "wait 6", "shot weather", "press 2 2 1L", "forward 60", "shot later" ]
 *     }
 *
 * A step is `wait <seconds>`, `press <keys>` (1-4, 1L-4L, each waited for on the panel),
 * `forward <seconds>` (the simulator's clock moved on) or `shot <name>`: the frame against
 * <name>.png. A frame that differs is written beside it as <name>.actual.png; --update writes
 * the expected pictures instead.
 */
class SimTest(private val root: File, private val builder: () -> AppBuilder) {

    private class Failure(val scenario: String, val why: String)

    /** Runs every test under `folder`; returns what failed, as lines. */
    fun run(folder: File, update: Boolean): List<String> {
        val scenarios = folder.walk().filter { it.name == "scenario.json" }.map { it.parentFile }.sortedBy { it.path }.toList()
        if (scenarios.isEmpty()) throw BuildError("${folder.path}: no scenario.json under it")
        val binary = simulatorBinary()
        val failures = ArrayList<Failure>()
        var shots = 0
        for (scenario in scenarios) {
            val name = scenario.relativeTo(folder).path.ifEmpty { scenario.name }
            try {
                shots += runOne(scenario, binary, update)
            } catch (error: Exception) {
                failures += Failure(name, error.message ?: error.toString())
            }
            println("${if (failures.lastOrNull()?.scenario == name) "FAIL" else "ok  "} $name")
        }
        println("${scenarios.size} scenarios, $shots pictures, ${failures.size} failed")
        return failures.map { "${it.scenario}: ${it.why}" }
    }

    private fun simulatorBinary(): String {
        val folder = File(root, "tools/simulator")
        if (ProcessBuilder("make", "-s", "-C", folder.path).inheritIO().start().waitFor() != 0) {
            throw BuildError("the simulator could not be built: it needs a C compiler")
        }
        return File(folder, "build/perinkpheral-sim").path
    }

    private fun freePort(): Int = ServerSocket(0).use { it.localPort }

    @Suppress("UNCHECKED_CAST")
    private fun runOne(folder: File, binary: String, update: Boolean): Int {
        val scenario = Json.parse(File(folder, "scenario.json").readText()) as Map<String, Any?>
        val appDir = File(scenario["app"] as? String ?: throw BuildError("no \"app\"")).absoluteFile
        val name = Applications.nameOf(appDir)
        val state = Files.createTempDirectory("perinkpheral-simtest").toFile()
        try {
            Applications.toSimulator(builder(), appDir, state)
            val store = File(state, "store/$name").apply { mkdirs() }
            (scenario["store"] as? Map<String, Any?>).orEmpty().forEach { (key, value) ->
                File(store, key.toByteArray().joinToString("") { "%02x".format(it) }).writeText(value.toString())
            }
            val port = freePort()
            val command = mutableListOf(binary, "--apps", File(state, "apps").path, "--app", name, "--state", state.path,
                "--port", port.toString(), "--language", scenario["language"] as? String ?: "en",
                "--font", scenario["font"] as? String ?: "departure", "--fetcher-port", SimulatorNetwork.port().toString())
            (scenario["at"] as? Long)?.let { command += listOf("--at", it.toString()) }
            if (scenario["clock"] == true) command += "--clock"
            (scenario["glyphs"] as? String)?.let { command += listOf("--glyphs", File(it).absolutePath) }
            (scenario["mocks"] as? Map<*, *>)?.let { mocks ->
                val file = File(state, "mocks.json").apply { writeText(Json.write(mocks)) }
                command += listOf("--mocks", file.path)
            }
            val builderOfProcess = ProcessBuilder(command).redirectErrorStream(true).redirectOutput(File(state, "sim.log"))
            builderOfProcess.environment()["TZ"] = scenario["tz"] as? String ?: "UTC"
            val process = builderOfProcess.start()
            try {
                val client = DeviceClient("127.0.0.1:$port")
                waitForStart(client, process, File(state, "sim.log"))
                var shots = 0
                for (step in (scenario["steps"] as? List<String>).orEmpty()) {
                    val words = step.trim().split(Regex("\\s+"))
                    when (words[0]) {
                        "wait" -> Thread.sleep((words[1].toDouble() * 1000).toLong())
                        "forward" -> client.raw("/api/sim/forward?seconds=${words[1]}", "POST")
                        "press" -> for (key in words.drop(1)) {
                            if (!Regex("[1-4][Ll]?").matches(key)) throw BuildError("a key is 1-4 or 1L-4L: $key")
                            val seen = client.screen().number
                            client.press(DeviceButton.entries[key[0] - '1'], key.length == 2)
                            client.settledScreen(seen)
                        }
                        "shot" -> {
                            check(client.screen().rows, File(folder, "${words[1]}.png"), update)
                            shots++
                        }
                        else -> throw BuildError("a step is wait, press, forward or shot: $step")
                    }
                }
                return shots
            } finally {
                process.destroy()
                process.waitFor()
            }
        } finally {
            state.deleteRecursively()
        }
    }

    private fun waitForStart(client: DeviceClient, process: Process, log: File) {
        val deadline = System.currentTimeMillis() + START_TIMEOUT_MS
        while (System.currentTimeMillis() < deadline) {
            if (!process.isAlive) throw BuildError("the simulator stopped: ${log.readText().takeLast(400)}")
            if (runCatching { client.status() }.isSuccess) {
                // As a device after its start: the first frame and the first answers in.
                Thread.sleep(SETTLE_MS)
                return
            }
            Thread.sleep(POLL_MS)
        }
        throw BuildError("the simulator did not answer in ${START_TIMEOUT_MS / 1000} s")
    }

    private fun check(rows: List<BooleanArray>, expected: File, update: Boolean) {
        val actual = File(expected.parentFile, expected.nameWithoutExtension + ".actual.png")
        if (update || !expected.isFile) {
            Bmp.writePng(expected, rows, 1)
            actual.delete()
            if (!update) throw BuildError("${expected.name} was not there: written now, look at it and keep it")
            return
        }
        val image = ImageIO.read(expected)
        val want = List(image.height) { y -> BooleanArray(image.width) { x -> luminance(image.getRGB(x, y)) < INK_BELOW } }
        val differing = if (want.size != rows.size || want[0].size != rows[0].size) -1
        else rows.indices.sumOf { y -> rows[y].indices.count { x -> rows[y][x] != want[y][x] } }
        if (differing != 0) {
            Bmp.writePng(actual, rows, 1)
            throw BuildError("${expected.name}: ${if (differing < 0) "another size" else "$differing pixels differ"} - see ${actual.name}")
        }
        actual.delete()
    }

    private fun luminance(rgb: Int): Int = ((rgb shr 16 and 0xFF) * 299 + (rgb shr 8 and 0xFF) * 587 + (rgb and 0xFF) * 114) / 1000

    companion object {
        private const val START_TIMEOUT_MS = 60_000L
        private const val SETTLE_MS = 6_000L
        private const val POLL_MS = 300L
        private const val INK_BELOW = 128
    }
}
