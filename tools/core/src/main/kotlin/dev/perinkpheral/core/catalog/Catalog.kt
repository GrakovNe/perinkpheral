// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.catalog

import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Built
import dev.perinkpheral.core.build.Certificates
import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import java.io.File
import java.security.MessageDigest

/**
 * A catalog: a folder a device installs applications from. Every application of the given
 * folders - a subfolder with a manifest.json - is built and put into the catalog as it will lie
 * on a device, and one index is written beside them, `catalog.json`:
 *
 *     { "catalog": 1,
 *       "apps": [ { "name": "clock", "title": "Clock", "version": "0.1.0", "abi": 3,
 *                   "description": "The time and the date, across the whole screen",
 *                   "translations": { "ru": { "title": "Часы", "description": "..." } },
 *                   "files": [ { "name": "app.wasm", "size": 1741, "sha256": "..." },
 *                              { "name": "manifest.json", "size": 98, "sha256": "..." } ] } ] }
 *
 * The files are listed in the order a device stores them: the manifest last, so that an
 * application cut off halfway is never listed as installed. A catalog is static files: put the
 * folder anywhere that serves it over HTTPS and give a device its address.
 */
object Catalog {
    private const val VERSION = 1L
    const val INDEX_NAME = "catalog.json"

    /** Builds the catalog; returns how many applications it has. */
    fun make(builder: AppBuilder, appsDirs: List<File>, out: File): Int {
        val appDirs = appsDirs.flatMap { folder ->
            folder.listFiles().orEmpty().filter { File(it, "manifest.json").isFile }
        }.map { it.canonicalFile }.sortedBy { it.name }
        if (appDirs.isEmpty()) {
            throw BuildError("no applications in ${appsDirs.joinToString(", ") { it.path }}: an application is a " +
                "folder with a manifest.json")
        }
        val duplicates = appDirs.groupBy { it.name }.filterValues { it.size > 1 }
        if (duplicates.isNotEmpty()) {
            throw BuildError("duplicate application names: " + duplicates.entries.joinToString("; ") { (name, folders) ->
                "$name (${folders.joinToString(", ") { it.path }})"
            })
        }
        // Missing metadata must not replace an earlier catalog or leave a partial new one.
        for (appDir in appDirs) {
            if (builder.checkManifest(appDir)["author"] !is String) {
                throw BuildError("${appDir.name}: author is required when publishing a catalog; add it to manifest.json")
            }
        }
        refuseToRemove(out, appsDirs + appDirs)
        // Whatever the catalog held before is gone: an application that was taken out of the
        // folder must not stay in the catalog.
        out.deleteRecursively()
        out.mkdirs()
        val apps = appDirs.map { appDir -> entryOf(builder.build(appDir, File(out, appDir.name))) }
        File(out, INDEX_NAME).writeText(Json.pretty(linkedMapOf("catalog" to VERSION, "apps" to apps), ensureAscii = true) + "\n")
        return apps.size
    }

    /** The catalog's folder is made anew, and so it must be one: not where the applications
     * are, nor above them, nor a folder of something else. */
    private fun refuseToRemove(out: File, appsDirs: List<File>) {
        val target = out.canonicalFile
        for (folder in appsDirs) {
            val source = folder.canonicalFile
            if (source.toPath().startsWith(target.toPath())) {
                throw BuildError("${out.path}: the catalog is made anew there, and the applications of ${folder.path} " +
                    "are in it: give another folder")
            }
        }
        if (target.isDirectory && target.list().orEmpty().isNotEmpty() && !File(target, INDEX_NAME).isFile) {
            throw BuildError("${out.path} has files and no $INDEX_NAME: it is not a catalog, and the catalog is made " +
                "anew there - give an empty folder or one that is a catalog")
        }
    }

    private fun entryOf(built: Built): Map<String, Any?> {
        val manifest = Json.parse(File(built.dir, AppBuilder.MANIFEST).readText()) as Map<*, *>
        val files = AppBuilder.packageFiles(built.dir).map { name ->
            val data = File(built.dir, name).readBytes()
            linkedMapOf("name" to name, "size" to data.size.toLong(),
                "sha256" to MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) })
        }
        val entry = linkedMapOf<String, Any?>("name" to manifest["name"], "title" to manifest["title"],
            "version" to (manifest["version"] ?: ""), "abi" to manifest["abi"], "files" to files)
        val author = manifest["author"] as? String
            ?: throw BuildError("${built.name}: author is required when publishing a catalog; add it to manifest.json")
        entry["author"] = author
        // The least firmware it runs on, as the build stamped it: the page offers it to no other.
        manifest["firmware"]?.let { entry["firmware"] = it }
        // The page asks before it installs an application with the system API, or one that
        // trusts a certificate authority of its own: it names the authority.
        if (manifest["system"] == true) entry["system"] = true
        // The room it keeps for files of its own, which the page names before it installs.
        if (manifest["writes_files"] == true) entry["writes_files"] = true
        val certificates = (manifest["certificates"] as? List<*>).orEmpty()
        if (certificates.isNotEmpty()) {
            entry["trusts"] = certificates.map { file ->
                Certificates.authority(Certificates.read(File(built.dir, file as String), Long.MAX_VALUE))
            }
        }
        // What a page shows of an application before it is installed: its icon, what it does,
        // and its title and description in the languages it has (manifest.<code> of the package).
        if ("icon" in manifest) entry["icon"] = manifest["icon"]
        if ("description" in manifest) entry["description"] = manifest["description"]
        val translations = sortedMapOf<String, Any?>()
        for (file in built.dir.listFiles().orEmpty().filter { Regex("manifest\\.[a-z]{2,3}").matches(it.name) }) {
            val said = Json.parse(file.readText()) as? Map<*, *> ?: continue
            val words = linkedMapOf<String, Any?>()
            said["title"]?.let { words["title"] = it }
            said["description"]?.let { words["description"] = it }
            if (words.isNotEmpty()) translations[file.name.substringAfter('.')] = words
        }
        if (translations.isNotEmpty()) entry["translations"] = translations
        File(built.dir, AppBuilder.OUTPUT_MARK).delete()
        return entry
    }
}
