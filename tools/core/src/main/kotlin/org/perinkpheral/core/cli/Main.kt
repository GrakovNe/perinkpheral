// SPDX-License-Identifier: MIT
package org.perinkpheral.core.cli

import org.perinkpheral.core.app.Applications
import org.perinkpheral.core.app.StringsTable
import org.perinkpheral.core.build.AppBuilder
import org.perinkpheral.core.build.BuildError
import org.perinkpheral.core.build.Json
import org.perinkpheral.core.build.Sdk
import org.perinkpheral.core.catalog.Catalog
import org.perinkpheral.core.device.Bmp
import org.perinkpheral.core.device.DeviceClient
import org.perinkpheral.core.device.UsbConsole
import org.perinkpheral.core.simulator.SimulatorNetwork
import java.io.File
import java.io.IOException
import kotlin.system.exitProcess

private const val USAGE = """perinkpheral - build, check and run perINKpheral applications

    perinkpheral build <app> --out <dir>      build and check an application: app.wasm and its package
    perinkpheral headers <app> <dir>          strings.h, strings.c and pictures.h, for an editor
    perinkpheral new <dir> [--title T]        an application to start from: the SDK's sample
    perinkpheral pictures <file or folder>... the drawings as the build reads them
    perinkpheral catalog <folder>... --out <dir>
                                              build a folder of applications into a catalog
    perinkpheral strings export <language> <out.csv> <folder>...
    perinkpheral strings import <language> <in.csv>
                                              the translations as one table, and back
    perinkpheral sim <app> [--port 8401] [--language en] [--font departure|jersey]
                     [--glyphs <dir>] [--mocks <file>] [--clock] [--state <dir>] [--binary <file>]
                                              run the application in the simulator; Ctrl-C stops it

  On a device, named by --host <address> or PERINKPHERAL_HOST (the simulator: 127.0.0.1:8401):
    perinkpheral status                       what the device is and how it is doing
    perinkpheral log [--follow] [--app]       its log; --app: only the lines of applications
    perinkpheral shot [out.png]               the frame its panel shows
    perinkpheral press 3 3 2L [--shot f.png]  buttons: 1-4 a short press, 1L-4L a long one
    perinkpheral open <name> [--shot f.png]   put an installed application on the screen
    perinkpheral run <app> [--shot f.png]     build, upload, restart the device and open it
    perinkpheral set <name> <key> <value>     a value into an application's store, as a setting
    perinkpheral flash [image.bin] [--apps]   a firmware image; --apps: sdk/apps anew too
    perinkpheral serve [app] [--port 8400]    a page with the live screen, the buttons, the log
  Over USB, for a device with no network:
    perinkpheral usb <port> [--keys 3,2] [--out shot.png] [--wait 2.5]

  The checkout is found by the launcher (tools/perinkpheral) or PERINKPHERAL; wasi-sdk by
  WASI_SDK_PATH."""

/** Arguments: the words in order, and --name value or --flag. */
private class Arguments(raw: Array<String>, private val flags: Set<String>) {
    val words = ArrayList<String>()
    val options = HashMap<String, String>()

    init {
        var at = 0
        while (at < raw.size) {
            val word = raw[at++]
            when {
                word.startsWith("--") && word.substring(2) in flags -> options[word.substring(2)] = "true"
                word.startsWith("--") -> {
                    if (at >= raw.size) fail("$word needs a value")
                    options[word.substring(2)] = raw[at++]
                }
                else -> words += word
            }
        }
    }

    fun word(index: Int, what: String): String = words.getOrNull(index) ?: fail("$what is missing\n\n$USAGE")
    fun option(name: String): String? = options[name]
    fun has(name: String): Boolean = options[name] == "true"
}

private fun fail(message: String): Nothing = throw BuildError(message)

private fun root(): File {
    val given = System.getProperty("perinkpheral.root") ?: System.getenv("PERINKPHERAL")
        ?: fail("which checkout? run tools/perinkpheral of it, or set PERINKPHERAL")
    return File(given).absoluteFile
}

private fun builder(): AppBuilder {
    val wasi = System.getenv("WASI_SDK_PATH") ?: fail("WASI_SDK_PATH is not set: point it at an unpacked wasi-sdk")
    return AppBuilder(Sdk(root()), File(wasi)) { println(it) }
}

private fun device(arguments: Arguments): DeviceClient =
    DeviceClient(arguments.option("host") ?: System.getenv("PERINKPHERAL_HOST")
        ?: fail("which device? give --host <address> or set PERINKPHERAL_HOST"))

private fun shot(rows: List<BooleanArray>, path: String) {
    val file = File(path).absoluteFile
    Bmp.writePng(file, rows)
    println(file.path)
}

/** The CMakeLists.txt of an application made from the command line: the checkout as it is now,
 * or PERINKPHERAL. */
private fun cmakeLists(name: String): String = """
    |cmake_minimum_required(VERSION 3.20)
    |
    |if(DEFINED ENV{PERINKPHERAL})
    |    set(PERINKPHERAL "${'$'}ENV{PERINKPHERAL}")
    |else()
    |    set(PERINKPHERAL "${root().path.replace('\\', '/')}")
    |endif()
    |include("${'$'}{PERINKPHERAL}/sdk/cmake/perinkpheral.cmake")
    |
    |project($name C)
    |perinkpheral_app(.)
    |""".trimMargin()

private fun simulator(arguments: Arguments): Int {
    val appDir = File(arguments.word(1, "the application's folder"))
    val name = Applications.nameOf(appDir)
    val state = File(arguments.option("state") ?: File(File(System.getProperty("user.home"), ".perinkpheral/simulator"), name).path)
    Applications.toSimulator(builder(), appDir, state)
    val binary = arguments.option("binary") ?: run {
        // Built here from the firmware's sources.
        val folder = File(root(), "tools/simulator")
        if (ProcessBuilder("make", "-s", "-C", folder.path).inheritIO().start().waitFor() != 0) {
            fail("the simulator could not be built: it needs a C compiler")
        }
        File(folder, "build/perinkpheral-sim").path
    }
    val command = mutableListOf(binary, "--apps", File(state, "apps").path, "--app", name, "--state", state.path,
        "--port", arguments.option("port") ?: "8401", "--language", arguments.option("language") ?: "en",
        "--font", arguments.option("font") ?: "departure", "--fetcher-port", SimulatorNetwork.port().toString())
    arguments.option("glyphs")?.let { command += listOf("--glyphs", it) }
    arguments.option("mocks")?.let { command += listOf("--mocks", it) }
    if (arguments.has("clock")) command += "--clock"
    val process = ProcessBuilder(command).inheritIO().start()
    // Ctrl-C stops it as a restart stops a device: app_on_terminate() is called.
    Runtime.getRuntime().addShutdownHook(Thread { process.destroy(); process.waitFor() })
    return process.waitFor()
}

private fun run(raw: Array<String>): Int {
    val arguments = Arguments(raw, setOf("follow", "app", "apps", "clock"))
    when (arguments.words.firstOrNull()) {
        "build" -> {
            builder().build(File(arguments.word(1, "the application's folder")),
                File(arguments.option("out") ?: fail("--out is required")))
        }
        "headers" -> builder().headers(File(arguments.word(1, "the application's folder")), File(arguments.word(2, "the folder")))
        "new" -> {
            val folder = File(arguments.word(1, "the folder"))
            Applications.create(root(), folder, arguments.option("title").orEmpty(), ::cmakeLists)
            println("${folder.absolutePath}: build it and put it on the device with `perinkpheral run ${folder.path}`")
        }
        "pictures" -> print(org.perinkpheral.core.build.Pictures.show(arguments.words.drop(1).map { File(it) }))
        "catalog" -> {
            val out = File(arguments.option("out") ?: fail("--out is required"))
            val count = Catalog.make(builder(), arguments.words.drop(1).map { File(it) }, out)
            println("${File(out, Catalog.INDEX_NAME).path}: $count applications")
        }
        "strings" -> when (arguments.word(1, "export or import")) {
            "export" -> StringsTable.export(arguments.word(2, "the language"), File(arguments.word(3, "the table")),
                arguments.words.drop(4).map { File(it) })
            "import" -> StringsTable.import(arguments.word(2, "the language"), File(arguments.word(3, "the table")))
                .forEach { println(it) }
            else -> fail(USAGE)
        }
        "sim" -> return simulator(arguments)
        "status" -> {
            val status = Json.parse(device(arguments).status()) as Map<*, *>
            status.forEach { (key, value) -> println("%-18s %s".format(key, value)) }
        }
        "log" -> {
            val client = device(arguments)
            var end: Long? = null
            do {
                val part = client.log(end)
                end = part.end
                part.text.lineSequence().filter { it.isNotEmpty() && (!arguments.has("app") || " app: " in it) }
                    .forEach { println(it) }
                if (arguments.has("follow")) Thread.sleep(1_000)
            } while (arguments.has("follow"))
        }
        "shot" -> {
            val screen = device(arguments).screen()
            if (screen.isPending) System.err.println("a newer frame is waiting for the panel; this is the one it shows now")
            shot(screen.rows, arguments.words.getOrNull(1) ?: "shot.png")
        }
        "press" -> {
            val client = device(arguments)
            var rows: List<BooleanArray>? = null
            for (key in arguments.words.drop(1)) {
                if (!Regex("[1-4][Ll]?").matches(key)) fail("a key is 1-4, or 1L-4L for a long press: $key")
                val seen = client.screen().number
                client.press(org.perinkpheral.core.device.DeviceButton.entries[key[0] - '1'], key.length == 2)
                rows = client.settledScreen(seen).rows
            }
            arguments.option("shot")?.let { path -> rows?.let { shot(it, path) } }
        }
        "open" -> {
            val client = device(arguments)
            val seen = client.screen().number
            client.open(arguments.word(1, "the application's name"))
            arguments.option("shot")?.let { shot(client.settledScreen(seen).rows, it) }
        }
        "run" -> {
            val client = device(arguments)
            Applications.toDevice(builder(), client, File(arguments.word(1, "the application's folder")), { println(it) })
            arguments.option("shot")?.let { shot(client.settledScreen(client.screen().number).rows, it) }
        }
        "set" -> device(arguments).set(arguments.word(1, "the application's name"), arguments.word(2, "the key"),
            arguments.word(3, "the value"))
        "flash" -> {
            val client = device(arguments)
            val image = File(arguments.words.getOrNull(1) ?: File(root(), "firmware/build/perinkpheral.bin").path)
            println(Applications.flash(client, image, { println(it) }))
            if (arguments.has("apps")) {
                // A firmware upload replaces the firmware and nothing else: applications built for
                // the old host API are refused by the new one. They follow the firmware.
                for (appDir in File(root(), "sdk/apps").listFiles().orEmpty().filter { it.isDirectory }.sortedBy { it.name }) {
                    Applications.toDevice(builder(), client, appDir, { println(it) })
                }
            }
        }
        "serve" -> {
            val client = device(arguments)
            val appDir = arguments.words.getOrNull(1)?.let { File(it).absoluteFile }
            PageServer(client, appDir) { folder, out ->
                val builder = AppBuilder(Sdk(root()), File(System.getenv("WASI_SDK_PATH").orEmpty())) { out.println(it) }
                try {
                    Applications.toDevice(builder, client, folder, { out.println(it) })
                    true
                } catch (error: BuildError) {
                    out.println(error.message)
                    false
                }
            }.serve(arguments.option("port")?.toInt() ?: 8400)
            Thread.currentThread().join()
        }
        "usb" -> UsbConsole(arguments.word(1, "the port")).use { console ->
            val wait = ((arguments.option("wait") ?: "2.5").toDouble() * 1000).toLong()
            for (key in arguments.option("keys").orEmpty().split(',').filter { it.isNotEmpty() }) {
                key.forEach { console.type(it) }
                Thread.sleep(wait)
            }
            shot(console.frame(), arguments.option("out") ?: "shot.png")
        }
        else -> {
            println(USAGE)
            return if (arguments.words.isEmpty()) 0 else 2
        }
    }
    return 0
}

fun main(raw: Array<String>) {
    val code = try {
        run(raw)
    } catch (error: BuildError) {
        System.err.println(error.message)
        1
    } catch (error: IOException) {
        System.err.println(error.message ?: error.javaClass.simpleName)
        1
    }
    exitProcess(code)
}
