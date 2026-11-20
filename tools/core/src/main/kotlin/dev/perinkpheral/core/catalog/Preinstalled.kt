// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.catalog

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import java.io.File
import java.io.IOException
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.time.Duration
import java.util.Properties

/**
 * The applications a device flashed from scratch has installed: ordinary packages of a catalog
 * repository, taken as that repository has them committed at one commit and laid out as the
 * image of the "apps" partition, which only a first flash writes. The list is a properties file
 * (firmware/preinstalled.properties):
 *
 *     repository = Perinkpheral/perinkpheral-apps
 *     commit = <the commit, 40 hex digits: a build does not change when the catalog moves on>
 *     apps = weather clock ...
 *
 * Every file is taken only if its size and SHA-256 are what the commit's catalog/catalog.json
 * says, as a device's installer takes it, and is written where an installation puts it:
 * `<out>/<name>/<file>`, nothing else beside it - no commit mark, no file on its way - so the
 * device lists it as installed.
 */
class Preinstalled(private val limits: Limits, private val say: (String) -> Unit = {}) {

    data class Pin(val repository: String, val commit: String, val apps: List<String>) {
        companion object {
            private val KEYS = setOf("repository", "commit", "apps")

            fun read(file: File): Pin {
                if (!file.isFile) throw BuildError("${file.path}: no such list of preinstalled applications")
                val properties = Properties().apply { file.reader().use { load(it) } }
                val unknown = properties.stringPropertyNames() - KEYS
                if (unknown.isNotEmpty()) throw BuildError("${file.path}: unknown keys ${unknown.sorted().joinToString(", ")}")
                fun value(key: String) = properties.getProperty(key)?.trim()?.takeIf { it.isNotEmpty() }
                    ?: throw BuildError("${file.path}: $key is missing")
                val repository = value("repository")
                if (!REPOSITORY.matches(repository)) throw BuildError("${file.path}: repository is owner/name on GitHub, not $repository")
                val commit = value("commit")
                if (!COMMIT.matches(commit)) throw BuildError("${file.path}: commit is a full commit id, 40 hex digits, not $commit")
                val apps = value("apps").split(Regex("[\\s,]+")).filter { it.isNotEmpty() }
                apps.firstOrNull { !NAME.matches(it) }?.let { throw BuildError("${file.path}: $it is not an application's name") }
                apps.groupBy { it }.filterValues { it.size > 1 }.keys.firstOrNull()?.let {
                    throw BuildError("${file.path}: $it is named twice")
                }
                return Pin(repository, commit, apps)
            }
        }
    }

    /** The files of the pinned commit: a path in the repository, its bytes. */
    fun interface Source {
        fun read(path: String): ByteArray
    }

    /** A package as it goes into the image: its files in the order a device writes them. */
    class Package(val name: String, val version: String, val writesFiles: Boolean, val files: List<Pair<String, ByteArray>>)

    /**
     * Reads the packages the pin names - from `cache` when an earlier build kept them, else from
     * `source`, keeping them in `cache` - checks them and what they take of the device, and
     * writes them into `out` as the image is made of them. `firmware` is the version being built.
     */
    fun build(pin: Pin, source: () -> Source, cache: File, out: File, firmware: String): List<Package> {
        val kept = File(cache, pin.commit)
        var opened: Source? = null
        fun read(path: String, check: (ByteArray) -> String?): ByteArray {
            val cached = File(kept, path)
            if (cached.isFile) {
                val bytes = cached.readBytes()
                if (check(bytes) == null) return bytes
            }
            val bytes = (opened ?: source().also { opened = it }).read(path)
            check(bytes)?.let { throw BuildError(it) }
            cached.parentFile.mkdirs()
            // Whole or not at all: a build stopped halfway leaves nothing a later one trusts.
            val part = File(cached.parentFile, cached.name + ".part")
            part.writeBytes(bytes)
            Files.move(part.toPath(), cached.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
            return bytes
        }

        val index = parseIndex(read(INDEX_PATH) { bytes -> if (runCatching { parseIndex(bytes) }.isSuccess) null else "$INDEX_PATH: not a catalog" })
        val packages = pin.apps.map { name ->
            val entry = index[name] ?: throw BuildError("$name is not in the catalog of ${pin.repository} at ${pin.commit}; " +
                "it has ${index.keys.sorted().joinToString(", ")}")
            checkCompatible(entry, firmware)
            val files = entry.files.map { file ->
                file.name to read("catalog/$name/${file.name}") { bytes -> refusal(name, file, bytes) }
            }
            val manifest = Json.parse(files.single { it.first == MANIFEST }.second.toString(Charsets.UTF_8)) as? Map<*, *>
                ?: throw BuildError("$name: manifest.json is not an object")
            if (manifest["name"] != name) throw BuildError("$name: its manifest names it ${manifest["name"]}")
            checkCompatible(Entry(name, manifest["version"] as? String ?: "", (manifest["abi"] as? Long) ?: -1,
                manifest["firmware"] as? String, entry.files), firmware)
            Package(name, entry.version, manifest["writes_files"] == true, files)
        }
        say(limits.check(packages))
        write(packages, out)
        return packages
    }

    /** The version of every application of the list in the old index and the new one. */
    fun changes(apps: List<String>, before: ByteArray, after: ByteArray, firmware: String?): List<String> {
        val old = parseIndex(before)
        val new = parseIndex(after)
        return apps.map { name ->
            val was = old[name]
            val now = new[name] ?: throw BuildError("$name is not in the new catalog: take it out of the list first")
            if (now.abi !in limits.abis) throw BuildError("$name is made for ABI ${now.abi} in the new catalog")
            val needs = now.firmware?.takeIf { firmware != null && version(it) > version(firmware) }
                ?.let { " - needs firmware $it, newer than $firmware" }.orEmpty()
            when {
                was == null -> "$name: new, ${now.version}$needs"
                was.files == now.files -> "$name: ${now.version}, unchanged"
                was.version == now.version -> "$name: ${now.version}, its files changed with no new version$needs"
                else -> "$name: ${was.version} -> ${now.version}$needs"
            }
        }
    }

    private class FileEntry(val name: String, val size: Long, val sha256: String) {
        override fun equals(other: Any?) = other is FileEntry && other.name == name && other.size == size && other.sha256 == sha256
        override fun hashCode() = sha256.hashCode()
    }

    private class Entry(val name: String, val version: String, val abi: Long, val firmware: String?, val files: List<FileEntry>)

    private fun parseIndex(bytes: ByteArray): Map<String, Entry> {
        val root = Json.parse(bytes.toString(Charsets.UTF_8)) as? Map<*, *> ?: throw BuildError("$INDEX_PATH: not an object")
        val apps = root["apps"] as? List<*> ?: throw BuildError("$INDEX_PATH: no apps")
        return apps.associate { item ->
            val app = item as? Map<*, *> ?: throw BuildError("$INDEX_PATH: an entry is not an object")
            val name = app["name"] as? String ?: throw BuildError("$INDEX_PATH: an entry has no name")
            val files = (app["files"] as? List<*>).orEmpty().map { file ->
                val map = file as? Map<*, *> ?: throw BuildError("$INDEX_PATH: $name: a file is not an object")
                FileEntry(map["name"] as? String ?: "", map["size"] as? Long ?: -1, map["sha256"] as? String ?: "")
            }
            name to Entry(name, app["version"] as? String ?: "", app["abi"] as? Long ?: -1, app["firmware"] as? String, files)
        }
    }

    private fun checkCompatible(entry: Entry, firmware: String) {
        val name = entry.name
        if (!NAME.matches(name)) throw BuildError("$name is not an application's name")
        if (entry.abi !in limits.abis) {
            throw BuildError("$name is made for ABI ${entry.abi}; this firmware runs ABI ${limits.abis.joinToString(", ")}")
        }
        entry.firmware?.let { needed ->
            if (version(needed) > version(firmware)) throw BuildError("$name needs firmware $needed; this build is $firmware")
        }
        val names = entry.files.map { it.name }
        // The device takes a package's module first and its manifest last, every name as a file's.
        if (names.firstOrNull() != MODULE || names.lastOrNull() != MANIFEST || names.toSet().size != names.size) {
            throw BuildError("$name: catalog.json lists its files as no package has them: ${names.joinToString(", ")}")
        }
        names.firstOrNull { !isFileName(it) }?.let { throw BuildError("$name: $it is not a file's name") }
    }

    private fun isFileName(name: String) = name.length in 1..limits.fileNameMax && name[0] != '.' &&
        Regex("[a-z0-9._-]+").matches(name) && !name.endsWith(".new")

    private fun refusal(app: String, file: FileEntry, bytes: ByteArray): String? = when {
        bytes.size.toLong() != file.size -> "$app/${file.name}: ${bytes.size} bytes, catalog.json says ${file.size}"
        sha256(bytes) != file.sha256 -> "$app/${file.name}: its SHA-256 is not what catalog.json says"
        else -> null
    }

    /** The folder made anew: what an earlier build put there and the list has no more must not be flashed. */
    private fun write(packages: List<Package>, out: File) {
        for (folder in out.listFiles().orEmpty()) {
            // Only what an image is made of is removed: a folder of an application's files.
            if (!folder.isDirectory || folder.listFiles().orEmpty().any { !it.isFile }) {
                throw BuildError("${out.path} has ${folder.name}, which is no application's folder: give another folder")
            }
            folder.deleteRecursively()
        }
        for (pack in packages) {
            val folder = File(out, pack.name).apply { mkdirs() }
            for ((name, bytes) in pack.files) File(folder, name).writeBytes(bytes)
        }
    }

    /**
     * What the device holds and promises, read from the firmware's own sources, so that an image
     * that would not fit fails here and not on a device.
     */
    class Limits(
        val installedMax: Int, val abis: Set<Long>, val moduleMax: Long, val codeHeader: Long, val codeBytes: Long,
        val sectorBytes: Long, val filesBytes: Long, val blockBytes: Long, val spareBytes: Long, val writerMax: Long,
        val fileNameMax: Int,
    ) {
        companion object {
            fun of(root: File): Limits {
                fun define(path: String, name: String): Long {
                    val text = File(root, path).readText().replace(Regex("""/\*.*?\*/""", RegexOption.DOT_MATCHES_ALL), " ")
                    val value = Regex("""^#define[ \t]+$name[ \t]+(.+?)[ \t]*$""", RegexOption.MULTILINE).find(text)
                        ?.groupValues?.get(1) ?: throw BuildError("$path: no $name")
                    // A number, or a product of them in parentheses: (13 * 4096).
                    return value.trim('(', ')', ' ').split('*').fold(1L) { product, factor ->
                        product * (factor.trim().removeSuffix("u").toLongOrNull()
                            ?: throw BuildError("$path: $name is $value, not a number"))
                    }
                }
                val partitions = File(root, "firmware/partitions.csv").readLines().filter { it.isNotBlank() && !it.startsWith("#") }
                    .associate { line -> line.split(',').map { it.trim() }.let { it[0] to it[4].removePrefix("0x").toLong(16) } }
                fun partition(name: String) = partitions[name] ?: throw BuildError("firmware/partitions.csv: no $name")
                val abis = Regex("""APP_SUPPORTED_ABIS\[\]\s*=\s*\{([^}]*)\}""")
                    .find(File(root, "firmware/components/appstore/include/app_abi.h").readText())?.groupValues?.get(1)
                    ?.split(',')?.map { it.trim().toLong() }?.toSet() ?: throw BuildError("app_abi.h: no APP_SUPPORTED_ABIS")
                val limits = "sdk/lib/manifest_limits.h"
                return Limits(
                    installedMax = define("firmware/components/appstore/include/catalog.h", "CATALOG_INSTALLED_MAX").toInt(),
                    abis = abis,
                    moduleMax = define(limits, "MANIFEST_MODULE_MAX_BYTES"),
                    codeHeader = define("firmware/main/app_code.h", "APP_CODE_HEADER_BYTES"),
                    codeBytes = partition("appcode"),
                    sectorBytes = define("firmware/main/app_code.c", "SECTOR_BYTES"),
                    filesBytes = partition("apps"),
                    blockBytes = define(limits, "MANIFEST_FILE_BLOCK_BYTES"),
                    spareBytes = define("firmware/components/appstore/include/app_room.h", "APP_ROOM_FILES_SPARE_BYTES"),
                    writerMax = define(limits, "MANIFEST_FILES_WRITER_MAX_BYTES"),
                    fileNameMax = define(limits, "MANIFEST_FILE_NAME_MAX").toInt(),
                )
            }
        }

        private fun blocks(bytes: Long) = (bytes + blockBytes - 1) / blockBytes

        /**
         * As the device counts (app_code.c, app_room.c, app_install.c): a module takes whole
         * sectors of the code partition after its header; a file whole blocks of the file system,
         * and every folder and the root a metadata pair of two more; each application that writes
         * keeps what its share has left. At the first start the modules are still files in their
         * folders, so they are counted there too: what fits then fits after they move. A small file
         * LittleFS keeps inside its folder's metadata takes less than counted here, never more.
         */
        fun check(packages: List<Package>): String {
            if (packages.size > installedMax) {
                throw BuildError("${packages.size} applications are preinstalled; a device holds $installedMax (CATALOG_INSTALLED_MAX)")
            }
            var sectors = 0L
            var fileBlocks = 0L
            var moduleBlocks = 0L
            var promised = 0L
            for (pack in packages) {
                val module = pack.files.first { it.first == MODULE }.second.size.toLong()
                if (module > moduleMax) throw BuildError("${pack.name}: its module is $module bytes, more than $moduleMax")
                sectors += (codeHeader + module + sectorBytes - 1) / sectorBytes
                moduleBlocks += blocks(module)
                val data = pack.files.filter { it.first != MODULE && it.first != MANIFEST }.sumOf { blocks(it.second.size.toLong()) }
                fileBlocks += data + blocks(pack.files.first { it.first == MANIFEST }.second.size.toLong())
                if (pack.writesFiles) promised += maxOf(0, blocks(writerMax) - data)
            }
            val codeSectors = codeBytes / sectorBytes
            if (sectors > codeSectors) {
                throw BuildError("the modules take $sectors sectors of the code partition's $codeSectors: preinstall fewer applications")
            }
            val total = filesBytes / blockBytes
            val folders = 2L * (packages.size + 1)
            val spare = blocks(spareBytes)
            val needed = fileBlocks + moduleBlocks + folders + promised + spare
            if (needed > total) {
                throw BuildError("the image needs $needed blocks of the file system's $total - files $fileBlocks, modules " +
                    "$moduleBlocks until they move, folders $folders, kept for applications that write $promised, spare " +
                    "$spare (app_room.h): preinstall fewer applications")
            }
            return "${packages.size} applications preinstalled: ${packages.joinToString(" ") { "${it.name} ${it.version}" }}\n" +
                "code: $sectors sectors of $codeSectors; files: $fileBlocks blocks of $total after the modules move " +
                "($needed counted with the modules, folders, $promised kept for writers and $spare spare)"
        }
    }

    companion object {
        const val INDEX_PATH = "catalog/" + Catalog.INDEX_NAME
        private const val MODULE = "app.wasm"
        private const val MANIFEST = "manifest.json"
        private val REPOSITORY = Regex("[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9_.-]{1,100}")
        private val COMMIT = Regex("[0-9a-f]{40}")
        private val NAME = Regex("[a-z0-9_-]{1,13}")

        fun sha256(bytes: ByteArray): String =
            MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

        /** A version's first three numbers: "0.1.73-0adbfb4" and "0.1.73-debug" are 0.1.73. */
        fun version(text: String): Long {
            val parts = Regex("""^(\d+)\.(\d+)\.(\d+)""").find(text)?.groupValues?.drop(1)?.map { it.toLong() }
                ?: throw BuildError("$text is not a version")
            return (parts[0] * 1000 + parts[1]) * 1000 + parts[2]
        }

        /** The commit's files over HTTPS, from where a device reads the catalog. */
        fun remote(pin: Pin, get: (URI) -> ByteArray = ::download): Source = Source { path ->
            get(URI("https://raw.githubusercontent.com/${pin.repository}/${pin.commit}/$path"))
        }

        /** The commit's files from a Git checkout that has it, whatever its working tree holds. */
        fun checkout(dir: File, commit: String): Source {
            fun git(vararg arguments: String): ByteArray {
                val process = try {
                    ProcessBuilder(listOf("git", "-C", dir.path) + arguments).redirectError(ProcessBuilder.Redirect.DISCARD)
                        .apply { environment().keys.removeIf { it.startsWith("GIT_") } }.start()
                } catch (error: IOException) {
                    throw BuildError("cannot run git to read ${dir.path}: ${error.message}")
                }
                val bytes = process.inputStream.use { it.readBytes() }
                if (process.waitFor() != 0) throw BuildError("${dir.path}: git ${arguments.joinToString(" ")} failed")
                return bytes
            }
            if (runCatching { git("cat-file", "-e", "$commit^{commit}") }.isFailure) {
                throw BuildError("${dir.path} is no checkout with commit $commit: fetch it there, or leave the checkout " +
                    "out to download the packages")
            }
            return Source { path -> git("cat-file", "blob", "$commit:$path") }
        }

        private val client by lazy {
            HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(20)).followRedirects(HttpClient.Redirect.NORMAL).build()
        }

        private fun download(uri: URI): ByteArray {
            var problem = ""
            // Three tries: GitHub answers a burst of requests with a refusal now and then.
            for (attempt in 0 until 3) {
                if (attempt > 0) Thread.sleep(2000L shl attempt)
                try {
                    val answer = client.send(HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(60)).build(),
                        HttpResponse.BodyHandlers.ofByteArray())
                    if (answer.statusCode() == 200) return answer.body()
                    problem = "HTTP ${answer.statusCode()}"
                    if (answer.statusCode() == 404) break
                } catch (error: IOException) {
                    problem = error.message ?: error.javaClass.simpleName
                }
            }
            throw BuildError("$uri: $problem. Without the network give a checkout of the catalog's repository " +
                "(--catalog or PERINKPHERAL_APPS)")
        }
    }
}
