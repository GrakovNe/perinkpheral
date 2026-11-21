// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.catalog

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import java.io.File
import java.nio.file.Files

object PreinstalledTest {
    private lateinit var scratch: File
    private var cases = 0
    private val root by lazy {
        generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
    }
    private val limits by lazy { Preinstalled.Limits.of(root) }
    private const val COMMIT = "0123456789abcdef0123456789abcdef01234567"

    @JvmStatic fun main(args: Array<String>) {
        scratch = Files.createTempDirectory("perinkpheral-preinstalled-test-").toFile()
        try {
            limitsOfTheFirmware()
            theList()
            layoutAndCache()
            tamperedFiles()
            refusedPackages()
            whatTheDeviceHolds()
            checkoutAtTheCommit()
            changesOfThePin()
        } finally { scratch.deleteRecursively() }
        println("Preinstalled checks passed: the list, the layout, the cache, tampered and refused packages, the room, a checkout")
    }

    /** A catalog as a repository has it committed: path in the repository to bytes. */
    private class Repository {
        val files = linkedMapOf<String, ByteArray>()
        val apps = mutableListOf<Map<String, Any?>>()
        var reads = 0

        fun add(name: String, version: String = "0.1.0", abi: Long = 3, firmware: String = "0.1.30",
                moduleBytes: Int = 1000, data: Map<String, ByteArray> = mapOf("strings.ru" to "{}".toByteArray()),
                writesFiles: Boolean = false, index: (MutableMap<String, Any?>) -> Unit = {}) {
            val manifest = linkedMapOf<String, Any?>("name" to name, "version" to version, "abi" to abi, "firmware" to firmware)
            if (writesFiles) manifest["writes_files"] = true
            val module = ByteArray(moduleBytes) { (it * 7 + name.length).toByte() }
            val package_ = linkedMapOf("app.wasm" to module) + data + ("manifest.json" to Json.pretty(manifest).toByteArray())
            for ((file, bytes) in package_) files["catalog/$name/$file"] = bytes
            val entry = linkedMapOf<String, Any?>("name" to name, "title" to name, "version" to version, "abi" to abi,
                "files" to package_.map { (file, bytes) ->
                    linkedMapOf("name" to file, "size" to bytes.size.toLong(), "sha256" to Preinstalled.sha256(bytes))
                }, "firmware" to firmware)
            index(entry)
            apps += entry
        }

        fun index() = Json.pretty(linkedMapOf("catalog" to 1L, "apps" to apps)).toByteArray()

        fun source() = Preinstalled.Source { path ->
            reads++
            if (path == Preinstalled.INDEX_PATH) index() else files[path] ?: throw BuildError("no $path")
        }
    }

    private fun folder() = File(scratch, "case-${cases++}").apply { mkdirs() }

    private fun pin(vararg apps: String) = Preinstalled.Pin("Perinkpheral/perinkpheral-apps", COMMIT, apps.toList())

    private fun refused(expected: String, block: () -> Unit) {
        val message = try {
            block()
            null
        } catch (error: BuildError) {
            error.message.orEmpty()
        }
        check(message != null) { "not refused, expected: $expected" }
        check(expected in message) { "refused with \"$message\", expected \"$expected\"" }
    }

    private fun limitsOfTheFirmware() {
        // Read from the firmware's headers and partitions.csv, the numbers the device keeps.
        check(limits.installedMax == 24)
        check(limits.abis == setOf(3L))
        check(limits.codeBytes / limits.sectorBytes == 80L)
        check(limits.filesBytes / limits.blockBytes == 160L)
        check(limits.spareBytes == 13L * 4096)
        check(limits.moduleMax == 48L * 1024)
    }

    private fun theList() {
        val dir = folder()
        fun list(text: String) = File(dir, "list.properties").apply { writeText(text) }
        val pin = Preinstalled.Pin.read(list("# a comment\nrepository = a/b\ncommit = $COMMIT\napps = clock weather,\\\n  timer\n"))
        check(pin == Preinstalled.Pin("a/b", COMMIT, listOf("clock", "weather", "timer")))
        refused("commit is a full commit id") { Preinstalled.Pin.read(list("repository = a/b\ncommit = main\napps = clock\n")) }
        refused("unknown keys branch") { Preinstalled.Pin.read(list("repository = a/b\ncommit = $COMMIT\napps = clock\nbranch = main\n")) }
        refused("clock is named twice") { Preinstalled.Pin.read(list("repository = a/b\ncommit = $COMMIT\napps = clock clock\n")) }
        refused("apps is missing") { Preinstalled.Pin.read(list("repository = a/b\ncommit = $COMMIT\n")) }
        refused("Clock! is not an application's name") { Preinstalled.Pin.read(list("repository = a/b\ncommit = $COMMIT\napps = Clock!\n")) }
    }

    private fun layoutAndCache() {
        val repository = Repository()
        repository.add("clock")
        repository.add("iss", data = mapOf("land.bin" to ByteArray(10000) { 1 }, "manifest.ru" to "{}".toByteArray()))
        repository.add("kit")
        val dir = folder()
        val out = File(dir, "apps_image")
        val cache = File(dir, "cache")
        // What an earlier build packed and the list has no more goes.
        File(out, "hello").mkdirs()
        File(out, "hello/app.wasm").writeText("old")
        Preinstalled(limits).build(pin("iss", "clock"), { repository.source() }, cache, out, "0.1.73-0adbfb4")
        check(out.list()!!.sorted() == listOf("clock", "iss"))
        for (name in listOf("clock", "iss")) {
            val expected = repository.files.filterKeys { it.startsWith("catalog/$name/") }
            val written = File(out, name).listFiles()!!
            check(written.map { it.name }.sorted() == expected.keys.map { it.substringAfterLast('/') }.sorted()) {
                "$name: ${written.map { it.name }}"
            }
            for (file in written) check(file.readBytes().contentEquals(expected.getValue("catalog/$name/${file.name}")))
        }
        // A rebuild takes everything from the cache: no network, no checkout.
        Preinstalled(limits).build(pin("iss", "clock"), { throw BuildError("the source was asked") }, cache, out, "0.1.73")
        check(out.list()!!.sorted() == listOf("clock", "iss"))
        // A cached file that is not what the index says is fetched again.
        File(cache, "$COMMIT/catalog/clock/app.wasm").writeText("spoiled")
        repository.reads = 0
        Preinstalled(limits).build(pin("iss", "clock"), { repository.source() }, cache, out, "0.1.73")
        check(repository.reads == 1)
        check(File(out, "clock/app.wasm").readBytes().contentEquals(repository.files.getValue("catalog/clock/app.wasm")))
        // A folder that is no application's is not removed.
        File(out, "stray/inner").mkdirs()
        refused("stray, which is no application's folder") {
            Preinstalled(limits).build(pin("clock"), { repository.source() }, cache, out, "0.1.73")
        }
    }

    private fun tamperedFiles() {
        val repository = Repository()
        repository.add("clock")
        repository.add("timer")
        repository.files["catalog/clock/app.wasm"]!![10] = 99
        val dir = folder()
        val out = File(dir, "apps_image")
        refused("clock/app.wasm: its SHA-256 is not what catalog.json says") {
            Preinstalled(limits).build(pin("timer", "clock"), { repository.source() }, File(dir, "cache"), out, "0.1.73")
        }
        check(!out.exists() || out.list()!!.isEmpty())
        check(!File(dir, "cache/$COMMIT/catalog/clock/app.wasm").exists())
        repository.files["catalog/timer/strings.ru"] = "{} ".toByteArray()
        refused("timer/strings.ru: 3 bytes, catalog.json says 2") {
            Preinstalled(limits).build(pin("timer"), { repository.source() }, File(dir, "fresh cache"), out, "0.1.73")
        }
    }

    private fun refusedPackages() {
        val repository = Repository()
        repository.add("clock")
        repository.add("old", abi = 2)
        repository.add("new", firmware = "0.1.80")
        repository.add("odd", index = { entry -> entry["files"] = (entry["files"] as List<*>).reversed() })
        repository.add("escape", data = mapOf("../escape" to ByteArray(1)))
        val dir = folder()
        fun build(vararg apps: String) = Preinstalled(limits).build(pin(*apps), { repository.source() }, File(dir, "cache"),
            File(dir, "apps_image"), "0.1.73-debug")
        refused("weather is not in the catalog of Perinkpheral/perinkpheral-apps at $COMMIT") { build("clock", "weather") }
        refused("old is made for ABI 2; this firmware runs ABI 3") { build("old") }
        refused("new needs firmware 0.1.80; this build is 0.1.73-debug") { build("new") }
        refused("odd: catalog.json lists its files as no package has them") { build("odd") }
        refused("escape: ../escape is not a file's name") { build("escape") }
        check(Preinstalled.version("0.1.73-0adbfb4") == Preinstalled.version("0.1.73"))
        check(Preinstalled.version("0.2.0") > Preinstalled.version("0.1.999"))
    }

    private fun whatTheDeviceHolds() {
        val repository = Repository()
        val names = (1..25).map { "app$it" }
        names.forEach { repository.add(it) }
        val dir = folder()
        // A cache a build: the repository changes here under one commit, as no real one does.
        var builds = 0
        fun build(apps: List<String>, with: Preinstalled.Limits = limits) = Preinstalled(with).build(
            Preinstalled.Pin("a/b", COMMIT, apps), { repository.source() }, File(dir, "cache-${builds++}"), File(dir, "apps_image"), "0.1.73")
        refused("25 applications are preinstalled; a device holds 24") { build(names) }
        check(build(names.take(24)).size == 24)
        repository.add("big", moduleBytes = 49 * 1024)
        refused("big: its module is 50176 bytes, more than 49152") { build(listOf("big")) }
        repository.add("large", moduleBytes = 40 * 1024)
        val smallCode = Preinstalled.Limits(24, setOf(3), limits.moduleMax, 32, 8 * 4096, 4096, limits.filesBytes, 4096,
            limits.spareBytes, limits.writerMax, limits.fileNameMax)
        refused("the modules take 11 sectors of the code partition's 8") { build(listOf("large"), smallCode) }
        repository.add("writer", writesFiles = true)
        // 6 blocks of files, 3 of modules, 8 of folders, 7 kept for the writer, 13 spare: 37 of 28.
        val smallFiles = Preinstalled.Limits(24, setOf(3), limits.moduleMax, 32, limits.codeBytes, 4096, 28 * 4096, 4096,
            limits.spareBytes, limits.writerMax, limits.fileNameMax)
        refused("the image needs 37 blocks of the file system's 28") { build(listOf("writer", "app1", "app2"), smallFiles) }
        check(build(listOf("app1", "app2"), smallFiles).size == 2)
    }

    private fun checkoutAtTheCommit() {
        val repository = Repository()
        repository.add("clock")
        val dir = folder()
        val checkout = File(dir, "apps").apply { mkdirs() }
        fun git(vararg arguments: String): String {
            val process = ProcessBuilder(listOf("git", "-c", "user.name=test", "-c", "user.email=test@example.invalid",
                "-c", "commit.gpgsign=false") + arguments).directory(checkout).redirectErrorStream(true)
                .apply { environment().keys.removeIf { it.startsWith("GIT_") } }.start()
            val output = process.inputStream.bufferedReader().readText()
            check(process.waitFor() == 0) { "git ${arguments.joinToString(" ")}: $output" }
            return output.trim()
        }
        git("init", "--quiet")
        File(checkout, Preinstalled.INDEX_PATH).apply { parentFile.mkdirs() }.writeBytes(repository.index())
        for ((path, bytes) in repository.files) File(checkout, path).apply { parentFile.mkdirs() }.writeBytes(bytes)
        git("add", ".")
        git("commit", "--quiet", "-m", "catalog")
        val commit = git("rev-parse", "HEAD")
        // The working tree moved on: what is read is the commit's.
        File(checkout, "catalog/clock/app.wasm").writeText("edited")
        val out = File(dir, "apps_image")
        Preinstalled(limits).build(Preinstalled.Pin("a/b", commit, listOf("clock")),
            { Preinstalled.checkout(checkout, commit) }, File(dir, "cache"), out, "0.1.73")
        check(File(out, "clock/app.wasm").readBytes().contentEquals(repository.files.getValue("catalog/clock/app.wasm")))
        refused("is no checkout with commit $COMMIT") { Preinstalled.checkout(checkout, COMMIT) }
    }

    private fun changesOfThePin() {
        val before = Repository().apply { add("clock"); add("timer"); add("air") }
        val after = Repository().apply {
            add("clock", version = "0.1.1")
            add("timer")
            add("air", moduleBytes = 1200)
            add("iss", firmware = "0.1.80")
        }
        val changes = Preinstalled(limits).changes(listOf("clock", "timer", "air", "iss"), before.index(), after.index(), "0.1.73")
        check(changes == listOf("clock: 0.1.0 -> 0.1.1", "timer: 0.1.0, unchanged", "air: 0.1.0, its files changed with no new version",
            "iss: new, 0.1.0 - needs firmware 0.1.80, newer than 0.1.73")) { changes.joinToString("\n") }
        refused("weather is not in the new catalog") {
            Preinstalled(limits).changes(listOf("weather"), before.index(), after.index(), null)
        }
    }
}
