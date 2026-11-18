// SPDX-License-Identifier: MIT
package org.perinkpheral.core

import org.perinkpheral.core.build.AppBuilder
import org.perinkpheral.core.build.Sdk
import org.perinkpheral.core.catalog.Catalog
import java.io.File
import java.nio.file.Files

object BuilderSafetyTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val scratch = Files.createTempDirectory("perinkpheral-build-safety").toFile()
        try {
            val source = File(scratch, "one/hello")
            File(root, "sdk/apps/hello").copyRecursively(source)
            val original = source.listFiles()!!.filter { it.isFile }.associate { it.name to it.readBytes().toList() }
            val builder = AppBuilder(Sdk(root), File(scratch, "absent-sdk")) {}
            fun refused(out: File) {
                try { builder.build(source, out); error("unsafe output was accepted") }
                catch (error: BuildError) { check("output" in error.message.orEmpty()) { error.message.orEmpty() } }
                check(original == source.listFiles()!!.filter { it.isFile }.associate { it.name to it.readBytes().toList() })
            }
            refused(source)
            refused(File(source, "."))
            refused(source.parentFile)
            val alias = File(scratch, "source-link")
            Files.createSymbolicLink(alias.toPath(), source.toPath())
            refused(alias)
            val unrelated = File(scratch, "notes").apply { mkdirs() }
            val note = File(unrelated, "notes.txt").apply { writeText("keep me") }
            refused(unrelated)
            check(note.readText() == "keep me")

            val other = File(scratch, "two/hello")
            source.copyRecursively(other)
            val catalog = File(scratch, "catalog").apply { mkdirs() }
            File(catalog, "catalog.json").writeText("old catalog")
            try {
                Catalog.make(builder, listOf(source.parentFile, other.parentFile), catalog)
                error("duplicate names accepted")
            } catch (error: BuildError) { check("duplicate application names" in error.message.orEmpty()) }
            check(File(catalog, "catalog.json").readText() == "old catalog")

            val unsafeCatalog = File(scratch, "unsafe-catalog").apply { mkdirs() }
            File(unsafeCatalog, "catalog.json").writeText("old catalog")
            val nestedSource = File(unsafeCatalog, "source/hello")
            source.copyRecursively(nestedSource)
            val linkedApps = File(scratch, "linked-apps").apply { mkdirs() }
            Files.createSymbolicLink(File(linkedApps, "hello").toPath(), nestedSource.toPath())
            try {
                Catalog.make(builder, listOf(linkedApps), unsafeCatalog)
                error("catalog containing symlinked source accepted")
            } catch (error: BuildError) { check("give another folder" in error.message.orEmpty()) }
            check(File(nestedSource, "manifest.json").isFile)
            check(File(unsafeCatalog, "catalog.json").readText() == "old catalog")

            val packageDir = File(scratch, "package").apply { mkdirs() }
            File(packageDir, AppBuilder.OUTPUT_MARK).writeText("private build metadata")
            File(packageDir, "asset.txt").writeText("data")
            check(AppBuilder.packageFiles(packageDir) == listOf("app.wasm", "asset.txt", "manifest.json"))

            System.getenv("WASI_SDK_PATH")?.let { wasi ->
                val realBuilder = AppBuilder(Sdk(root), File(wasi)) {}
                val built = File(scratch, "built")
                realBuilder.build(source, built)
                // Rebuild both current and pre-marker packages without keeping stale files.
                File(built, "stale.txt").writeText("old asset")
                realBuilder.build(source, built)
                check(!File(built, "stale.txt").exists())
                File(built, AppBuilder.OUTPUT_MARK).delete()
                realBuilder.build(source, built)
                check(File(built, "app.wasm").length() > 8)

                val aliases = File(scratch, "aliases").apply { mkdirs() }
                Files.createSymbolicLink(File(aliases, "renamed").toPath(), source.toPath())
                val aliasCatalog = File(scratch, "alias-catalog")
                Catalog.make(realBuilder, listOf(aliases), aliasCatalog)
                val index = Json.parse(File(aliasCatalog, "catalog.json").readText()) as Map<*, *>
                val entry = (index["apps"] as List<*>).single() as Map<*, *>
                check(entry["name"] == "hello")
                check(File(aliasCatalog, "hello/app.wasm").isFile)
                check(!File(aliasCatalog, "renamed").exists())
            }
        } finally { scratch.deleteRecursively() }
        println("Builder safety checks passed: canonical paths, unrelated files, duplicate names and package contents")
    }
}
