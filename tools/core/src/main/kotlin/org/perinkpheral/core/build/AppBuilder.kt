// SPDX-License-Identifier: MIT
package org.perinkpheral.core.build

import java.io.File
import java.nio.file.Files

/**
 * Builds a perINKpheral application: C sources -> app.wasm and its package, checked by the
 * rules the host applies and measured against what the device has. What tools/mkapp does,
 * in the same words; the compiler is clang of wasi-sdk, as there.
 */
class AppBuilder(private val sdk: Sdk, private val wasiSdk: File, private val say: (String) -> Unit) {
    private val check = ManifestCheck(sdk)
    private val moduleMax = sdk.limit("MODULE_MAX_BYTES")
    private val fileNameMax = sdk.limit("FILE_NAME_MAX")
    private val filesMax = sdk.limit("FILES_MAX_BYTES")
    private val fileBlock = sdk.limit("FILE_BLOCK_BYTES")

    /** What a file of so many bytes takes on the device: whole blocks. */
    private fun takenBy(size: Long): Long = (size + fileBlock - 1) / fileBlock * fileBlock

    private fun tool(name: String): File {
        val file = File(File(wasiSdk, "bin"), name)
        if (!file.exists() && !File(file.path + ".exe").exists()) {
            throw BuildError("no $name in ${File(wasiSdk, "bin").path}")
        }
        return if (file.exists()) file else File(file.path + ".exe")
    }

    /** strings.h, strings.c and pictures.h into `into`: what an editor needs to read the code. */
    fun headers(given: File, givenInto: File) {
        val appDir = given.absoluteFile
        val into = givenInto.absoluteFile
        into.mkdirs()
        val pictures = File(appDir, "pictures")
        if (pictures.isDirectory) Pictures.make(pictures, File(into, "pictures.h"))
        Strings(sdk, check).make(appDir, into)
    }

    /** Builds `appDir` into `outDir`: app.wasm, manifest.json, the files and the tables of the
     * languages. Throws BuildError with the reason when it fails. Returns the application's name. */
    fun build(given: File, givenOut: File): String {
        val appDir = given.absoluteFile
        val outDir = givenOut.absoluteFile
        val manifest = check.check(appDir)
        outDir.mkdirs()
        val wasm = File(outDir, "app.wasm")
        // Whatever an earlier build left: a build that fails must not look done.
        wasm.delete()
        val strings = Strings(sdk, check)
        val memoryEnd = compile(appDir, wasm, strings)
        try {
            val imports = checkModule(wasm, manifest["system"] == true)
            misspelledHandlers(appDir)
            checkHandlers(wasm, imports, manifest)
            val moduleBytes = wasm.length()
            if (moduleBytes > moduleMax) {
                throw BuildError("${wasm.path}: the module is $moduleBytes bytes, at most $moduleMax fit on the device")
            }
            File(appDir, "manifest.json").copyTo(File(outDir, "manifest.json"), overwrite = true)
            var filesBytes = copyDataFiles(appDir, outDir)
            // The tables of the languages it speaks beside English, as files of its own.
            for ((code, table) in strings.tables) {
                File(outDir, "strings.$code").writeBytes(table)
                filesBytes += takenBy(table.size.toLong())
            }
            for ((code, words) in strings.manifests) {
                File(outDir, "manifest.$code").writeText(words)
                filesBytes += takenBy(words.toByteArray().size.toLong())
            }
            if (filesBytes > filesMax) {
                throw BuildError("${appDir.path}: its files take $filesBytes bytes on the device - each a whole " +
                    "number of $fileBlock-byte blocks - and an application may have $filesMax")
            }
            report(wasm, imports.size, filesBytes, memoryEnd)
        } catch (error: BuildError) {
            // A module that fails a check is not left behind.
            wasm.delete()
            throw error
        }
        return manifest["name"] as String
    }

    /** Compiles; returns where the module's data ends in its memory. */
    private fun compile(appDir: File, wasm: File, strings: Strings): Long {
        val sources = appDir.listFiles().orEmpty().filter { it.isFile && it.name.endsWith(".c") }.sortedBy { it.path }
        if (sources.isEmpty()) throw BuildError("no .c files in ${appDir.path}")
        val library = sdk.library.listFiles().orEmpty().filter { it.isFile && it.name.endsWith(".c") }.sortedBy { it.path }
        val command = mutableListOf(
            tool("clang").path, "--target=wasm32", "-nostdlib", "-ffreestanding", "-fno-builtin",
            "-Oz", "-flto", "-Wall", "-Wextra", "-Werror",
            "-I" + sdk.sdk.path, "-I" + sdk.library.path,
            "-Wl,--no-entry", "-Wl,--strip-all", "-Wl,--gc-sections",
            "-Wl,--initial-memory=$WASM_PAGE", "-Wl,--max-memory=$WASM_PAGE",
            "-Wl,-z,stack-size=$APP_STACK",
            "-o", wasm.path)
        command += (sources + library).map { it.path }
        // The linker's map says where the module's data ends in its memory: the binary itself
        // does not, for what starts as zeroes is not written out.
        val scratch = Files.createTempDirectory("perinkpheral-build").toFile()
        try {
            val pictures = File(appDir, "pictures")
            if (pictures.isDirectory) {
                val constants = Pictures.make(pictures, File(scratch, "pictures.h"))
                Unused.check(appDir, constants, "pictures")
                command += "-I" + scratch.path
            }
            val stringsSource = strings.make(appDir, scratch)
            if (stringsSource != null) command += listOf(stringsSource.path, "-I" + scratch.path)
            val map = File(scratch, "app.map")
            command += "-Wl,-Map=" + map.path
            if (run(command, appDir) != 0) throw BuildError("${appDir.path} did not compile")
            val placed = Regex("""^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\.\w+\s*$""", RegexOption.MULTILINE)
                .findAll(map.readText())
            return placed.maxOfOrNull { it.groupValues[1].toLong(16) + it.groupValues[2].toLong(16) } ?: 0
        } finally {
            scratch.deleteRecursively()
        }
    }

    /** Runs a command, passing its output on line by line; returns its exit code. */
    private fun run(command: List<String>, directory: File): Int {
        val process = ProcessBuilder(command).directory(directory).redirectErrorStream(true).start()
        process.inputStream.bufferedReader().forEachLine { say(it) }
        return process.waitFor()
    }

    private fun checkModule(wasm: File, asksForSystem: Boolean): List<Pair<String, String>> {
        val module = Wasm(wasm)
        val floats = floatOperations(wasm)
        if (floats.isNotEmpty()) {
            throw BuildError("${wasm.path} uses floating point (${floats.take(4).joinToString(", ")}). The device's " +
                "interpreter has none, and the application would stop there: keep numbers as integers - tenths, " +
                "hundredths - the way sdk/lib/app_values.h reads them")
        }
        val offered = sdk.imports()
        val system = sdk.imports("HOST_SYSTEM_IMPORT")
        for ((moduleName, name) in module.imports) {
            if (moduleName == HOST_MODULE && name in system && !asksForSystem) {
                throw BuildError("the module imports $name, which is the system API: an application that wants it " +
                    "says \"system\": true in its manifest")
            }
            if (moduleName != HOST_MODULE || name !in offered + system) {
                throw BuildError("the module imports $moduleName.$name, which the host does not offer")
            }
        }
        if (module.memory != (1L to 1L)) {
            throw BuildError("the module's memory is ${module.memory} pages, it must be exactly one")
        }
        return module.imports
    }

    /** The floating point operations in the module's code, as the disassembler names them. */
    private fun floatOperations(wasm: File): List<String> {
        val process = ProcessBuilder(tool("llvm-objdump").path, "-d", wasm.path).redirectErrorStream(true).start()
        val listing = process.inputStream.bufferedReader().readText()
        if (process.waitFor() != 0) throw BuildError("llvm-objdump: ${listing.trim().take(400)}")
        // The disassembler prints the plain "select" as "f32.select"; that one is not a float
        // operation, whatever its name.
        return Regex("""\b(?:f(?:32|64)\.(?!select\b)\w+|[\w.]+_f(?:32|64)\w*)""").findAll(listing)
            .map { it.value }.toSortedSet().toList()
    }

    /** A function named like a handler that is none: the build drops it without a word. */
    private fun misspelledHandlers(appDir: File) {
        val known = sdk.knownHandlers
        for (file in appDir.listFiles().orEmpty().filter { it.isFile && it.name.endsWith(".c") }.sortedBy { it.path }) {
            for (match in Regex("""\b(app_on_\w+)\s*\([^;{}()]*\)\s*\{""").findAll(file.readText())) {
                val name = match.groupValues[1]
                if (name !in known) {
                    throw BuildError("${file.path}: $name() is not a handler the host calls. Its handlers: " +
                        known.sorted().joinToString(", "))
                }
            }
        }
    }

    /** What the module asks for has to have somewhere to arrive. */
    private fun checkHandlers(wasm: File, imports: List<Pair<String, String>>, manifest: Map<*, *>) {
        val exports = Wasm(wasm).exports.toSet()
        val known = sdk.knownHandlers
        for (name in exports.sorted()) {
            if (name.startsWith("app_") && name !in known && name != "app_strings_buffer") {
                throw BuildError("${wasm.path} exports $name(), which the host never calls: its handlers are " +
                    known.sorted().joinToString(", "))
            }
        }
        val called = imports.filter { it.first == HOST_MODULE }.map { it.second }.toSet()
        for ((call, handler) in ANSWERED_BY.toSortedMap()) {
            if (call in called && handler !in exports) {
                throw BuildError("${wasm.path} calls $call() and has no $handler() for what comes of it")
            }
        }
        ((manifest["polls"] as? List<*>) ?: emptyList<Any>()).forEachIndexed { number, poll ->
            val picks = (poll as Map<*, *>)["pick"].let { it is List<*> && it.isNotEmpty() }
            val handler = if (picks) "app_on_values" else "app_on_data"
            if (handler !in exports) {
                throw BuildError("${wasm.path}: poll $number of the manifest ${if (picks) "has \"pick\"" else "has no \"pick\""}, " +
                    "and the module has no $handler()")
            }
        }
        if ("app_on_data" in exports && "app_data_buffer" !in exports) {
            throw BuildError("${wasm.path} has app_on_data() and no app_data_buffer(): link sdk/lib/app_data.c or " +
                "export a buffer of HOST_DATA_CHUNK_MAX bytes")
        }
        if ((manifest["intents"] as? List<*>)?.isNotEmpty() == true && "app_on_intent" !in exports) {
            throw BuildError("${wasm.path}: the manifest asks for intents and the module has no app_on_intent()")
        }
        val measuring = Regex("""(_width|_height|_ascent|_fit|_visible|_first)$""")
        if (called.any { (it.startsWith("gfx_") || it.startsWith("ui_")) && !measuring.containsMatchIn(it) } &&
            "app_on_draw" !in exports) {
            throw BuildError("${wasm.path} draws and has no app_on_draw(): drawing calls work only there")
        }
    }

    /** What is in the application's `files` folder goes onto the device beside the module. */
    private fun copyDataFiles(appDir: File, outDir: File): Long {
        val folder = File(appDir, "files")
        if (!folder.isDirectory) return 0
        var total = 0L
        for (file in folder.listFiles().orEmpty().sortedBy { it.name }) {
            // What a file manager leaves behind, and folders, are not the application's.
            if (file.name.startsWith(".") || !file.isFile) continue
            if (file.name in setOf("app.wasm", "manifest.json")) throw BuildError("${file.path}: this name is the application's own")
            if (!Regex("[a-z0-9_-][a-z0-9._-]{0,${fileNameMax - 1}}").matches(file.name)) {
                throw BuildError("${file.path}: a file's name is 1-$fileNameMax of a-z, 0-9, '.', '-' and '_', " +
                    "not starting with a dot")
            }
            total += takenBy(file.length())
            file.copyTo(File(outDir, file.name), overwrite = true)
        }
        return total
    }

    /** What the build came to, against what the device has for it. */
    private fun report(wasm: File, importCount: Int, filesBytes: Long, memoryEnd: Long) {
        // Rounded as tools/mkapp rounds: half to even, of the exact value.
        fun kb(count: Long) = java.math.BigDecimal(count / 1024.0).setScale(1, java.math.RoundingMode.HALF_EVEN)
            .toPlainString() + " KB"
        val module = wasm.length()
        val code = Wasm(wasm).codeBytes
        val running = INTERPRETER_BASE_BYTES + INTERPRETER_BYTES_PER_CODE_BYTE * code
        say("${wasm.path}: $module bytes, $importCount host calls")
        say("  module  ${kb(module)} of ${kb(moduleMax)}")
        say("  code    ${kb(code)}; running it takes about ${kb(running)} of the interpreter's ${kb(INTERPRETER_ROOM_BYTES)}")
        if (memoryEnd > 0) {
            say("  memory  ${kb(memoryEnd)} of ${kb(WASM_PAGE.toLong())}: ${kb(APP_STACK.toLong())} of stack, " +
                "${kb(memoryEnd - APP_STACK)} of data; ${kb(WASM_PAGE - memoryEnd)} left for app_alloc()")
        }
        if (filesBytes > 0) say("  files   ${kb(filesBytes)} of ${kb(filesMax)}")
        if (running > INTERPRETER_ROOM_BYTES) say("  warning: this much code may not fit when all of it runs")
    }

    companion object {
        const val WASM_PAGE = 65536
        const val APP_STACK = 8192
        private const val HOST_MODULE = "host"
        // What an application's code costs when it runs, as measured on the device: about this much
        // for any application, and about four bytes for every byte of the code section.
        private const val INTERPRETER_BASE_BYTES = 11L * 1024
        private const val INTERPRETER_BYTES_PER_CODE_BYTE = 4L
        private const val INTERPRETER_ROOM_BYTES = 64L * 1024

        /** A call that asks for something to come later, and the handler it comes to. */
        private val ANSWERED_BY = mapOf(
            "timer_set" to "app_on_timer",
            "alarm_set" to "app_on_alarm",
            "alarm_set_at" to "app_on_alarm",
            "alarm_set_every" to "app_on_alarm",
            "http_request" to "app_on_data",
        )
    }
}
