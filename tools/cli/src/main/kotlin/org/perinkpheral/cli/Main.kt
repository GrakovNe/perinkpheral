// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

import org.perinkpheral.core.contract.ContractSnapshot
import org.perinkpheral.core.BuildError
import org.perinkpheral.core.Json
import org.perinkpheral.core.app.Applications
import org.perinkpheral.core.app.StringsTable
import org.perinkpheral.core.build.AppBuilder
import org.perinkpheral.core.build.BuildFlags
import org.perinkpheral.core.build.Sdk
import org.perinkpheral.core.catalog.Catalog
import org.perinkpheral.core.device.Bmp
import org.perinkpheral.core.device.DeviceClient
import org.perinkpheral.core.device.Endurance
import org.perinkpheral.core.device.LogFollower
import org.perinkpheral.core.device.UsbConsole
import org.perinkpheral.core.simulator.SimulatorNetwork
import java.io.File
import java.io.IOException
import kotlin.system.exitProcess

private const val USAGE = """perinkpheral - build, check and run perINKpheral applications

    perinkpheral build <app> --out <dir>      build and check an application: app.wasm and its package
    perinkpheral headers <app> <dir>          strings.h, strings.c and pictures.h, for an editor
    perinkpheral flags compile|link           the compiler's flags of the build, as a CMake list
    perinkpheral new <dir> [--title T]        an application to start from: the SDK's sample
    perinkpheral pictures <file or folder>... the drawings as the build reads them
    perinkpheral catalog <folder>... --out <dir>
                                              build a folder of applications into a catalog
    perinkpheral strings export <language> <out.csv> <folder>...
    perinkpheral strings import <language> <in.csv>
                                              the translations as one table, and back
    perinkpheral sim <app> [--port 8401] [--language en] [--font departure|jersey]
                     [--glyphs <dir>] [--mocks <file>] [--clock] [--at <unix>] [--state <dir>] [--binary <file>]
                                              run the application in the simulator; Ctrl-C stops it
    perinkpheral preview [dir] [--font jersey] the host's screens as PNGs, rendered on this computer
    perinkpheral sim-test <dir> [--update]    applications in the simulator, their screens against kept pictures

  What the project makes of its sources:
    perinkpheral check-generated              committed fonts, icons, marks, spelling and guide screens
    perinkpheral check-api                    OpenAPI paths and methods against the firmware routes
    perinkpheral check-pdf <kept> <made>       compare guide text, ignoring PDF metadata and pagination
    perinkpheral contract snapshot|check      record or check the frozen ABI (wasi-sdk required)
    perinkpheral check-wasm <file>            check import/export wasm types against the contract
    perinkpheral imports <dir>                the host's table of imports from sdk/host_api.h
    perinkpheral every-call <dir>             an application that imports every call, for the tests
    perinkpheral check-limits <guide.html>    the guide's table of limits against the headers
    perinkpheral guide [--language ru] [--out <pdf>]
                                              the application guide into a PDF (Chromium, poppler)
    perinkpheral guide-units list|missing <language>|apply <language>
                                              the guide's pieces: what a translation is made of
    perinkpheral icons                        the host's icons into gfx_icon_art.c
    perinkpheral video <video> --out frames.bin [--width 200] [--height 150] [--fps 1.5] [--threshold 128]
                                              a video into a 1-bit stream an application plays
    perinkpheral spelling --out <header>      how letters with no glyph are spelled in ASCII
    perinkpheral glyph-packs --out <dir> [--sheet <dir>]
                                              the glyph packs of letters beyond ASCII
    perinkpheral font <font> --px <size> --name <C name> --out <header> [--chars C] [--only C]
                                              a font into a header of one bit a pixel
    perinkpheral cities [--check]             Settings' cities.h made of cities.json
    perinkpheral check-layers [firmware dir]  the firmware's layers, by its includes and calls
    perinkpheral check-languages              the firmware's languages are the SDK's
    perinkpheral page <web dir> <out> <out.gz> the device's page, made of its parts
    perinkpheral flash-manifest <build> <out> <version> [--prefix debug-]
                                              the files of a first flash and their manifest
    perinkpheral mark [<pixels> <file>]...    the mark into assets/mark, and on paper at other sizes

  On a device, named by --host <address> or PERINKPHERAL_HOST (the simulator: 127.0.0.1:8401):
    perinkpheral status                       what the device is and how it is doing
    perinkpheral soak --out <dir> --image <id> [--hours 72]
                                              observe a release image; logs, status and gaps, no device changes
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
    perinkpheral console <port> [--seconds 30] what the console says, through a reset too

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

private fun builder(say: (String) -> Unit = { println(it) }): AppBuilder {
    val wasi = System.getenv("WASI_SDK_PATH") ?: fail("WASI_SDK_PATH is not set: point it at an unpacked wasi-sdk")
    return AppBuilder(Sdk(root()), File(wasi), say)
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
    arguments.option("at")?.let { command += listOf("--at", it) }
    val process = ProcessBuilder(command).inheritIO().start()
    // Ctrl-C stops it as a restart stops a device: app_on_terminate() is called.
    Runtime.getRuntime().addShutdownHook(Thread { process.destroy(); process.waitFor() })
    return process.waitFor()
}

private fun run(raw: Array<String>): Int {
    val arguments = Arguments(raw, setOf("follow", "app", "apps", "clock", "update", "check"))
    when (arguments.words.firstOrNull()) {
        "build" -> {
            builder().build(File(arguments.word(1, "the application's folder")),
                File(arguments.option("out") ?: fail("--out is required")))
        }
        // Made of the application's files alone: no compiler.
        "headers" -> AppBuilder(Sdk(root()), File(System.getenv("WASI_SDK_PATH").orEmpty())) {}.headers(File(arguments.word(1, "the application's folder")), File(arguments.word(2, "the folder")))
        "flags" -> println(BuildFlags.cmake(when (arguments.word(1, "compile or link")) {
            "compile" -> listOf(BuildFlags.TARGET) + BuildFlags.compile
            "link" -> listOf(BuildFlags.TARGET) + BuildFlags.link
            else -> fail(USAGE)
        }))
        "preview" -> {
            val out = File(arguments.words.getOrNull(1) ?: File(System.getProperty("java.io.tmpdir"), "perinkpheral-preview").path)
            Preview.render(root(), out, arguments.option("font")).forEach { println(it.path) }
        }
        "check-generated" -> Generated.check(root(), Sdk(root()).contract).forEach(::println)
        "check-api" -> println(Generated.checkApi(root()))
        "check-pdf" -> Generated.checkPdf(File(arguments.word(1, "the committed PDF")),
            File(arguments.word(2, "the rebuilt PDF")))
        "contract" -> {
            val sdk = Sdk(root())
            val action = arguments.word(1, "snapshot or check")
            if (action !in setOf("snapshot", "check")) fail("contract snapshot|check [--out file]")
            ContractSnapshot.run(sdk, File(System.getenv("WASI_SDK_PATH") ?: fail("WASI_SDK_PATH is not set")),
                File(arguments.option("out") ?: File(sdk.sdk, "abi/${sdk.hostApiVersion}.txt").path), action == "check")
        }
        "check-wasm" -> {
            org.perinkpheral.core.build.Wasm(File(arguments.word(1, "the module"))).checkTypes(Sdk(root()).contract)
        }
        "imports" ->
 ContractTools.imports(Sdk(root()).contract, File(arguments.word(1, "the folder")))
        "every-call" -> ContractTools.everyCall(Sdk(root()).contract, File(arguments.word(1, "the folder")))
        "check-limits" -> println("${ContractTools.checkLimits(Sdk(root()).contract, File(arguments.word(1, "the guide")))} limits of the guide agree with the headers")
        "page" -> FirmwareTools.page(File(arguments.word(1, "the folder of the page")), File(arguments.word(2, "the page")),
            File(arguments.word(3, "the packed page")))
        "flash-manifest" -> FirmwareTools.flashManifest(File(arguments.word(1, "the build")), File(arguments.word(2, "the folder")),
            arguments.word(3, "the version"), arguments.option("prefix").orEmpty())
        "cities" -> {
            val folder = File(root(), "firmware/system/settings")
            FirmwareTools.cities(File(folder, "cities.json"), File(folder, "cities.h"), arguments.has("check"))
        }
        "check-layers" -> {
            val (broken, checks) = Layers(File(arguments.words.getOrNull(1) ?: File(root(), "firmware").path)).check()
            broken.forEach { println("layers: $it") }
            println("$checks checks, ${broken.size} failed")
            if (broken.isNotEmpty()) return 1
        }
        "check-languages" -> println("${checkLanguages(root())} checks, 0 failed")
        "font" -> {
            val font = File(arguments.word(1, "the font"))
            println(Fonts.header(font, runCatching { font.absoluteFile.relativeTo(root().absoluteFile).path }.getOrNull()?.takeUnless { it.startsWith("..") } ?: font.path,
                (arguments.option("px") ?: fail("--px is required")).toDouble(),
                arguments.option("name") ?: fail("--name is required"), File(arguments.option("out") ?: fail("--out is required")),
                arguments.option("chars").orEmpty(), arguments.option("only")))
        }
        "glyph-packs" -> GlyphPacks.make(root(), File(arguments.option("out") ?: fail("--out is required")), arguments.option("sheet")?.let { File(it) }).forEach { println(it) }
        "spelling" -> println(Spelling.make(File(arguments.option("out") ?: fail("--out is required"))))
        "video" -> println(Video.make(File(arguments.word(1, "the video")), File(arguments.option("out") ?: fail("--out is required")),
            (arguments.option("width") ?: "200").toInt(), (arguments.option("height") ?: "150").toInt(),
            (arguments.option("fps") ?: "1.5").toDouble(), (arguments.option("threshold") ?: "128").toInt()))
        "guide" -> println(Guide(root()).pdf(Sdk(root()).contract, arguments.option("language") ?: "en", arguments.option("out")?.let { File(it) }))
        "guide-units" -> when (arguments.word(1, "list, missing or apply")) {
            "list" -> Guide(root()).list().forEach { println(it) }
            "missing" -> Guide(root()).missing(arguments.word(2, "the language")).forEach { println(it) }
            "apply" -> println(Guide(root()).apply(arguments.word(2, "the language")))
            else -> fail(USAGE)
        }
        "sim-test" -> {
            val failed = SimTest(root()) { builder {} }.run(File(arguments.word(1, "the folder of tests")), arguments.has("update"))
            failed.forEach { println("  $it") }
            if (failed.isNotEmpty()) return 1
        }
        "icons" -> println("firmware/components/gfx/gfx_icon_art.c: ${Icons.make(root(), Sdk(root()).contract)} bytes of drawings")
        "mark" -> {
            val extra = arguments.words.drop(1).chunked(2).map { (pixels, file) -> pixels.toInt() to File(file) }
            val rects = Icons.mark(root(), extra)
            println("cells: " + rects.joinToString(", ") { "[${it.joinToString(", ")}]" })
            println("path: " + rects.joinToString("") { (x, y, w, h) -> "M$x ${y}h${w}v${h}H${x}z" })
        }
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
        "soak" -> Endurance(device(arguments), File(arguments.option("out") ?: fail("--out is required")),
            arguments.option("image") ?: fail("--image is required")).run(
                (arguments.option("hours") ?: "72").toDouble())
        "log" -> {
            val log = LogFollower(device(arguments))
            val isFollowing = arguments.has("follow")
            var isAway = false
            do {
                val part = try {
                    log.next()
                } catch (error: IOException) {
                    // A device that restarts is away for a while: followed, it is waited for.
                    if (!isFollowing) throw error
                    if (!isAway) System.err.println("${error.message}; waiting")
                    isAway = true
                    Thread.sleep(1_000)
                    continue
                }
                isAway = false
                if (part.restarted) println("--- restart ---")
                part.text.lineSequence().filter { it.isNotEmpty() && (!arguments.has("app") || " app: " in it) }
                    .forEach { println(it) }
                if (isFollowing) Thread.sleep(1_000)
            } while (isFollowing)
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
                try {
                    Applications.toDevice(builder { out.println(it) }, client, folder, { out.println(it) })
                    true
                } catch (error: BuildError) {
                    out.println(error.message)
                    false
                }
            }.serve(arguments.option("port")?.toInt() ?: 8400)
            Thread.currentThread().join()
        }
        "console" -> UsbConsole.follow(arguments.word(1, "the port"), (arguments.option("seconds") ?: "30").toDouble()) {
            print(it)
            System.out.flush()
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
    } catch (error: Exception) {
        // Whatever it was, a line of it: a stack is no help to whoever runs a command.
        System.err.println(when (error) {
            is IOException, is Json.Error -> error.message ?: error.javaClass.simpleName
            is NumberFormatException -> "not a number: ${error.message?.substringAfter("For input string: ")}"
            else -> "${error.javaClass.simpleName}: ${error.message}"
        })
        1
    }
    exitProcess(code)
}
