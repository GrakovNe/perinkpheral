// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.build

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import java.io.File
import java.nio.file.Files

/** An application built: its name, its manifest as checked, and the folder of its package. */
class Built(val name: String, val manifest: Map<*, *>, val dir: File)

/**
 * Builds a perINKpheral application: C sources -> app.wasm and its package, checked by the
 * rules the host applies and measured against what the device has. The compiler is clang of
 * wasi-sdk, with BuildFlags.
 */
class AppBuilder(private val sdk: Sdk, private val wasiSdk: File, private val say: (String) -> Unit) {
    private val check = ManifestCheck(sdk)
    private val moduleMax = sdk.limit("MODULE_MAX_BYTES")
    private val fileNameMax = sdk.limit("FILE_NAME_MAX")
    private val filesMax = sdk.limit("FILES_MAX_BYTES")
    private val writerFilesMax = sdk.limit("FILES_WRITER_MAX_BYTES")
    private val settingValueMax = sdk.limit("SETTING_VALUE_MAX")
    private val fileBlock = sdk.limit("FILE_BLOCK_BYTES")

    internal fun checkManifest(appDir: File): Map<*, *> = check.check(appDir)

    /** What a file of so many bytes takes on the device: whole blocks. */
    private fun takenBy(size: Long): Long = (size + fileBlock - 1) / fileBlock * fileBlock

    private fun tool(name: String): File {
        val file = File(File(wasiSdk, "bin"), name)
        if (!file.exists() && !File(file.path + ".exe").exists()) {
            throw BuildError("no $name in ${File(wasiSdk, "bin").path}")
        }
        return if (file.exists()) file else File(file.path + ".exe")
    }

    /**
     * strings.h, strings.c and pictures.h into `into`: what an editor needs to read the code.
     * Made of what is there while the code is being written - a string not used yet, a
     * translation that lags behind - which the build itself refuses. What cannot be made
     * leaves `into` as it was, so that the code an editor reads does not lose its names.
     */
    fun headers(given: File, givenInto: File) {
        val appDir = given.absoluteFile
        val into = givenInto.absoluteFile
        val made = Files.createTempDirectory("perinkpheral-headers").toFile()
        try {
            val pictures = File(appDir, "pictures")
            if (pictures.isDirectory) Pictures.make(pictures, File(made, "pictures.h"))
            Strings(sdk, check).make(appDir, made, isStrict = false)
            into.mkdirs()
            // What the application no longer has goes too: a header of pictures that were removed.
            for (name in GENERATED) File(into, name).delete()
            for (file in made.listFiles().orEmpty()) file.copyTo(File(into, file.name), overwrite = true)
        } finally {
            made.deleteRecursively()
        }
    }

    /** Builds `appDir` into `outDir`: app.wasm, manifest.json, the files and the tables of the
     * languages. Throws BuildError with the reason when it fails. */
    fun build(given: File, givenOut: File): Built {
        val appDir = given.canonicalFile
        val outDir = givenOut.canonicalFile
        val manifest = check.check(appDir)
        prepareOutput(appDir, outDir, manifest["name"] as String)
        val wasm = File(outDir, MODULE)
        // Whatever an earlier build left: a build that fails must not look done, and a file the
        // application no longer has must not travel in its package.
        for (name in packageFiles(outDir)) File(outDir, name).takeIf { it.isFile }?.delete()
        wasm.delete()
        val strings = Strings(sdk, check)
        val memoryEnd = compile(appDir, wasm, strings, manifest)
        try {
            val module = Wasm(wasm)
            val writesFiles = manifest["writes_files"] == true
            checkModule(wasm, module, manifest["system"] == true, writesFiles)
            misspelledHandlers(appDir)
            checkHandlers(wasm, module, manifest)
            val moduleBytes = wasm.length()
            if (moduleBytes > moduleMax) {
                throw BuildError("${wasm.path}: the module is $moduleBytes bytes, at most $moduleMax fit on the device")
            }
            // As it was written, and the least firmware it runs on: the latest the calls it
            // makes came with. A manifest may ask for more - for a behaviour, not a call - never less.
            val calls = module.imports.filter { it.first == HOST_MODULE }.map { it.second }
            val (needed, byCall) = sdk.firmwareFor(calls + module.exports)
            val declared = manifest["firmware"] as? String
            val firmware = if (declared != null && versionNumber(declared)!! > versionNumber(needed)!!) declared else needed
            val firmwareLine = when {
                declared == firmware -> "  firmware $firmware or later, as the manifest says"
                byCall != null -> "  firmware $firmware or later, for $byCall"
                else -> "  firmware $firmware or later"
            }
            val stamped = LinkedHashMap(manifest).apply { put("firmware", firmware) }
            File(outDir, MANIFEST).writeText(Json.pretty(stamped) + "\n")
            var filesBytes = copyDataFiles(appDir, outDir)
            // The authorities it trusts for its own requests: files of the package, held to what one is.
            for (file in (manifest["certificates"] as? List<*>).orEmpty()) {
                Certificates.read(File(File(appDir, "files"), file as String), sdk.limit("CERTIFICATE_MAX_BYTES"))
            }
            // The tables of the languages it speaks beside English, as files of its own.
            for ((code, table) in strings.tables) {
                File(outDir, "strings.$code").writeBytes(table)
                filesBytes += takenBy(table.size.toLong())
            }
            for ((code, words) in strings.manifests) {
                File(outDir, "manifest.$code").writeText(words)
                filesBytes += takenBy(words.toByteArray().size.toLong())
            }
            val filesLimit = if (writesFiles) writerFilesMax else filesMax
            if (filesBytes > filesLimit) {
                throw BuildError("${appDir.path}: its files take $filesBytes bytes on the device - each a whole " +
                    "number of $fileBlock-byte blocks - and an application " +
                    (if (writesFiles) "that writes files may have $filesLimit, those it writes included" else "may have $filesLimit"))
            }
            report(wasm, module, filesBytes, filesLimit, memoryEnd, settingsStoreBytes(manifest), firmwareLine)
        } catch (error: Exception) {
            // A module that fails a check, or a package that could not be written, is not left behind.
            wasm.delete()
            throw error
        }
        return Built(manifest["name"] as String, manifest, outDir)
    }

    private fun prepareOutput(appDir: File, outDir: File, name: String) {
        if (appDir.toPath().startsWith(outDir.toPath())) {
            throw BuildError("${outDir.path}: output must not be the source folder or contain it")
        }
        val entries = outDir.listFiles().orEmpty()
        val marker = File(outDir, OUTPUT_MARK)
        if (entries.isNotEmpty() && !marker.isFile) {
            // Older packages predate the ownership mark. Source trees and unrelated
            // directories must never be adopted as disposable build output.
            val oldManifest = runCatching { Json.parse(File(outDir, MANIFEST).readText()) as? Map<*, *> }.getOrNull()
            val module = File(outDir, MODULE)
            val wasmMagic = module.isFile && module.inputStream().use {
                it.readNBytes(4).contentEquals(byteArrayOf(0, 97, 115, 109))
            }
            val hasSources = entries.any { it.isDirectory || it.extension in setOf("c", "h", "cpp", "hpp") || it.name == "CMakeLists.txt" }
            if (oldManifest?.get("name") != name || !wasmMagic || hasSources) {
                throw BuildError("${outDir.path}: non-empty output is not a built application package; use an empty folder")
            }
        }
        if (!outDir.isDirectory && !outDir.mkdirs()) throw BuildError("cannot create output ${outDir.path}")
        marker.writeText("perinkpheral application package\n")
    }

    /** The application's own C files, in the order they are compiled. */
    private fun sources(appDir: File): List<File> =
        appDir.listFiles().orEmpty().filter { it.isFile && it.name.endsWith(".c") }.sortedBy { it.path }

    /** Compiles; returns where the module's data ends in its memory. */
    private fun compile(appDir: File, wasm: File, strings: Strings, manifest: Map<*, *>): Long {
        val sources = sources(appDir)
        if (sources.isEmpty()) throw BuildError("no .c files in ${appDir.path}")
        val library = sdk.library.listFiles().orEmpty().filter { it.isFile && it.name.endsWith(".c") }.sortedBy { it.path }
        val command = mutableListOf(tool("clang").path, BuildFlags.TARGET)
        command += BuildFlags.compile
        command += listOf("-I" + sdk.sdk.path, "-I" + sdk.library.path)
        command += BuildFlags.linker
        command += listOf("-o", wasm.path)
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
            val stringsSource = strings.make(appDir, scratch, isStrict = true, manifest = manifest)
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

    private fun checkModule(wasm: File, module: Wasm, asksForSystem: Boolean, writesFiles: Boolean) {
        module.checkTypes(sdk.contract)
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
            if (moduleName == HOST_MODULE && name == FILE_WRITE && !writesFiles) {
                throw BuildError("the module imports $name: an application that writes files says " +
                    "\"writes_files\": true in its manifest, and the device keeps ${writerFilesMax / 1024} KB free for it")
            }
            if (moduleName != HOST_MODULE || name !in offered + system) {
                throw BuildError("the module imports $moduleName.$name, which the host does not offer")
            }
        }
        if (module.memory != (1L to 1L)) {
            throw BuildError("the module's memory is ${module.memory} pages, it must be exactly one")
        }
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
        for (file in sources(appDir)) {
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
    private fun checkHandlers(wasm: File, module: Wasm, manifest: Map<*, *>) {
        val exports = module.exports.toSet()
        val known = sdk.knownHandlers
        for (name in exports.sorted()) {
            if (name.startsWith("app_") && name !in known && name != "app_strings_buffer") {
                throw BuildError("${wasm.path} exports $name(), which the host never calls: its handlers are " +
                    known.sorted().joinToString(", "))
            }
        }
        val called = module.imports.filter { it.first == HOST_MODULE }.map { it.second }.toSet()
        for ((call, handler) in ANSWERED_BY.toSortedMap()) {
            if (call in called && handler !in exports) {
                throw BuildError("${wasm.path} calls $call() and has no $handler() for what comes of it")
            }
        }
        ((manifest["polls"] as? List<*>) ?: emptyList<Any>()).forEachIndexed { number, poll ->
            val picks = (poll as Map<*, *>)["pick"].let { it is List<*> && it.isNotEmpty() }
            val handler = if (picks) "app_on_picked" else "app_on_data"
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
            if (file.name == MODULE || file.name == MANIFEST) throw BuildError("${file.path}: this name is the application's own")
            if (!Regex("[a-z0-9_-][a-z0-9._-]{0,${fileNameMax - 1}}").matches(file.name)) {
                throw BuildError("${file.path}: a file's name is 1-$fileNameMax of a-z, 0-9, '.', '-' and '_', " +
                    "not starting with a dot")
            }
            total += takenBy(file.length())
            file.copyTo(File(outDir, file.name), overwrite = true)
        }
        return total
    }

    /** What the settings take of the application's store at their longest, as the host counts a
     * value: two entries of 32 bytes and the value in whole entries. */
    private fun settingsStoreBytes(manifest: Map<*, *>): Long {
        fun taken(length: Long) = (2 + (length + 31) / 32) * 32
        return ((manifest["settings"] as? List<*>).orEmpty()).sumOf { setting ->
            setting as Map<*, *>
            when (setting["type"]) {
                "toggle" -> taken(1)
                "number" -> taken(NUMBER_TEXT_COUNTED)
                "choice" -> taken((setting["options"] as? List<*>).orEmpty()
                    .maxOfOrNull { it.toString().toByteArray().size.toLong() } ?: 0)
                else -> taken(settingValueMax)
            }
        }
    }

    /** What the build came to, against what the device has for it. */
    private fun report(wasm: File, module: Wasm, filesBytes: Long, filesLimit: Long, memoryEnd: Long,
                       settingsBytes: Long, firmwareLine: String) {
        // Half to even, of the exact value.
        fun kb(count: Long) = java.math.BigDecimal(count / 1024.0).setScale(1, java.math.RoundingMode.HALF_EVEN)
            .toPlainString() + " KB"
        val moduleBytes = wasm.length()
        val code = module.codeBytes
        val running = INTERPRETER_BASE_BYTES + INTERPRETER_BYTES_PER_CODE_BYTE * code
        say("${wasm.path}: $moduleBytes bytes, ${module.imports.size} host calls")
        say("  module  ${kb(moduleBytes)} of ${kb(moduleMax)}")
        say("  code    ${kb(code)}; running it takes about ${kb(running)} of the interpreter's ${kb(INTERPRETER_ROOM_BYTES)}")
        if (memoryEnd > 0) {
            say("  memory  ${kb(memoryEnd)} of ${kb(BuildFlags.WASM_PAGE.toLong())}: " +
                "${kb(BuildFlags.APP_STACK.toLong())} of stack, ${kb(memoryEnd - BuildFlags.APP_STACK)} of data; " +
                "${kb(BuildFlags.WASM_PAGE - memoryEnd)} left for app_alloc()")
        }
        if (filesBytes > 0 || filesLimit == writerFilesMax) say("  files   ${kb(filesBytes)} of ${kb(filesLimit)}")
        if (settingsBytes > 0) {
            say("  store   its settings at their longest take ${kb(settingsBytes)} of ${kb(sdk.storeMax)}")
            if (settingsBytes > sdk.storeMax) say("  warning: settings this long would not all fit the store")
        }
        say(firmwareLine)
        if (running > INTERPRETER_ROOM_BYTES) say("  warning: this much code may not fit when all of it runs")
    }

    companion object {
        /** What `headers` makes; nothing else in its folder is touched. */
        private val GENERATED = listOf("strings.h", "strings.c", "pictures.h")
        internal const val OUTPUT_MARK = ".perinkpheral-output"

        /** A package's files in the order a device takes them: the module first, the manifest
         * last - until it is there the device does not list the application, and by then
         * everything it needs is. */
        fun packageFiles(dir: File): List<String> {
            val data = dir.listFiles().orEmpty().filter { it.isFile && !it.name.startsWith(".") }
                .map { it.name }.filter { it != MODULE && it != MANIFEST }.sorted()
            return listOf(MODULE) + data + listOf(MANIFEST)
        }

        const val MODULE = "app.wasm"
        const val MANIFEST = "manifest.json"
        private const val HOST_MODULE = "host"
        private const val FILE_WRITE = "file_write"
        // What a number setting is counted at: a sign, the digits of 64 bits and a point - a number
        // as a person writes one, not the 255 bytes the host would take of any text.
        private const val NUMBER_TEXT_COUNTED = 21L
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
            "http_send" to "app_on_data",
        )
    }
}
