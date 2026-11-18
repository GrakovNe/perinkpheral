// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import dev.perinkpheral.core.app.Applications
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.ManifestAuthor
import dev.perinkpheral.core.build.ManifestCheck
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.catalog.Catalog
import java.io.File
import java.nio.file.Files

object ManifestAuthorTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val sdk = Sdk(root)
        val authors = ManifestAuthor(sdk)
        val scratch = Files.createTempDirectory("perinkpheral-author-test-").toFile()
        try {
            check(authors.check(null) == null)
            for (invalid in listOf(null, "", " ", " Name", "Name ", "one\ntwo", "one\ttwo", "one\u007ftwo", "a".repeat(64), "Я".repeat(32))) {
                fails { authors.check(invalid, required = true) }
            }
            for (valid in listOf("a".repeat(63), "Я".repeat(31), "李", "Anne & Bob", "Автор приложения", "A. Developer")) {
                check(authors.checkPersonal(valid) == valid)
            }
            for (reserved in listOf("Perinkpheral", "perinkpheral", "PERINKPHERAL", "\u00a0Perinkpheral\u00a0")) {
                fails { authors.checkPersonal(reserved) }
            }
            check(authors.check(ManifestAuthor.OFFICIAL, required = true) == ManifestAuthor.OFFICIAL)
            val source = File(scratch, "created")
            Applications.create(root, source, "My app", "Автор", { "# $it\n" })
            val created = ManifestCheck(sdk).check(source)
            check(created["author"] == "Автор" && created["name"] == "created")
            for (reserved in listOf("Perinkpheral", "perinkpheral")) {
                val target = File(scratch, "refused")
                fails { Applications.create(root, target, "App", reserved, { "" }) }
                check(!target.exists())
            }
            val legacyTarget = File(scratch, "old-plugin")
            fails { Applications.create(root, legacyTarget, "App", { "" }) }
            check(!legacyTarget.exists())

            // Old source manifests remain buildable; publication is the strict boundary.
            val legacy = File(scratch, "apps/hello")
            File(root, "sdk/apps/hello").copyRecursively(legacy)
            val manifestFile = File(legacy, "manifest.json")
            @Suppress("UNCHECKED_CAST")
            val manifest = LinkedHashMap(Json.parse(manifestFile.readText()) as Map<String, Any?>)
            manifest.remove("author")
            manifestFile.writeText(Json.pretty(manifest))
            check("author" !in ManifestCheck(sdk).check(legacy))
            manifest["author"] = null
            manifestFile.writeText(Json.pretty(manifest))
            fails { ManifestCheck(sdk).check(legacy) }
            manifest.remove("author")
            manifestFile.writeText(Json.pretty(manifest))
            System.getenv("WASI_SDK_PATH")?.let { wasi ->
                val builder = AppBuilder(sdk, File(wasi)) {}
                val built = builder.build(legacy, File(scratch, "built"))
                check("author" !in Json.parse(File(built.dir, "manifest.json").readText()) as Map<*, *>)
                fails { Catalog.make(builder, listOf(legacy.parentFile), File(scratch, "catalog")) }
                manifest["author"] = "Автор"
                manifestFile.writeText(Json.pretty(manifest))
                Catalog.make(builder, listOf(legacy.parentFile), File(scratch, "catalog"))
                val index = Json.parse(File(scratch, "catalog/catalog.json").readText()) as Map<*, *>
                val entry = (index["apps"] as List<*>).single() as Map<*, *>
                check(entry["author"] == "Автор")
                val packed = Json.parse(File(scratch, "catalog/hello/manifest.json").readText()) as Map<*, *>
                check(packed["author"] == entry["author"])
            }
        } finally { scratch.deleteRecursively() }
        println("Author checks passed: UTF-8 limits, personal scaffold, reserved name, legacy compatibility and catalog propagation")
    }

    private fun fails(action: () -> Unit) {
        try { action(); error("invalid author accepted") } catch (_: BuildError) { }
    }
}
