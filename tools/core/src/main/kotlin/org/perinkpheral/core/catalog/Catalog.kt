// SPDX-License-Identifier: MIT
package org.perinkpheral.core.catalog

import org.perinkpheral.core.build.AppBuilder
import org.perinkpheral.core.build.BuildError
import org.perinkpheral.core.build.Json
import java.io.File
import java.security.MessageDigest

/**
 * A catalog: a folder a device installs applications from. Every application of the given
 * folders - a subfolder with a manifest.json - is built and put into the catalog as it will lie
 * on a device, and one index is written beside them, `catalog.json`:
 *
 *     { "catalog": 1,
 *       "apps": [ { "name": "clock", "title": "Clock", "version": "0.1.0", "abi": 2,
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
        }.sortedBy { it.name }
        if (appDirs.isEmpty()) {
            throw BuildError("no applications in ${appsDirs.joinToString(", ") { it.path }}: an application is a " +
                "folder with a manifest.json")
        }
        // Whatever the catalog held before is gone: an application that was taken out of the
        // folder must not stay in the catalog.
        out.deleteRecursively()
        out.mkdirs()
        val apps = appDirs.map { appDir ->
            val built = File(out, appDir.absoluteFile.normalize().name)
            builder.build(appDir, built)
            entryOf(built)
        }
        File(out, INDEX_NAME).writeText(Json.pretty(linkedMapOf("catalog" to VERSION, "apps" to apps), ensureAscii = true) + "\n")
        return apps.size
    }

    private fun entryOf(built: File): Map<String, Any?> {
        val manifest = Json.parse(File(built, "manifest.json").readText()) as Map<*, *>
        val names = built.listFiles().orEmpty().map { it.name }.filter { it != "app.wasm" && it != "manifest.json" }.sorted()
        val files = (listOf("app.wasm") + names + listOf("manifest.json")).map { name ->
            val data = File(built, name).readBytes()
            linkedMapOf("name" to name, "size" to data.size.toLong(),
                "sha256" to MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) })
        }
        val entry = linkedMapOf<String, Any?>("name" to manifest["name"], "title" to manifest["title"],
            "version" to (manifest["version"] ?: ""), "abi" to manifest["abi"], "files" to files)
        // The page asks before it installs an application with the system API.
        if (manifest["system"] == true) entry["system"] = true
        // What a page shows of an application before it is installed.
        if ("icon" in manifest) entry["icon"] = manifest["icon"]
        return entry
    }
}
