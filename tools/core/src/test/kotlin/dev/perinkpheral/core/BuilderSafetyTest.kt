// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import dev.perinkpheral.core.build.BuildFlags
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.ManifestCheck
import dev.perinkpheral.core.build.Pictures
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.catalog.Catalog
import java.io.File
import java.nio.file.Files

object BuilderSafetyTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("perinkpheral.test.root", System.getProperty("user.dir"))).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        if (args.isNotEmpty()) {
            when (args[0]) {
                "flags" -> println((if (args[1] == "compile") BuildFlags.compile else BuildFlags.link).joinToString(";"))
                "headers" -> AppBuilder(Sdk(root), File("unused")) {}.headers(File(args[1]), File(args[2]))
                else -> error("unexpected test tool command")
            }
            return
        }
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

            val defaults = File(scratch, "defaults/hello")
            source.copyRecursively(defaults)
            val defaultsFile = File(defaults, "manifest.json")
            val defaultsManifest = LinkedHashMap(Json.parse(defaultsFile.readText()) as Map<*, *>)
            for (invalid in listOf("hello\nworld", "café", "tab\there", "delete\u007f")) {
                defaultsManifest["settings"] = listOf(mapOf("key" to "text", "title" to "Text", "type" to "text", "default" to invalid))
                defaultsFile.writeText(Json.pretty(defaultsManifest))
                try { ManifestCheck(Sdk(root)).check(defaults); error("invalid default accepted") }
                catch (_: BuildError) { }
            }
            for (valid in listOf("hello ~", listOf("one", "two"))) {
                defaultsManifest["settings"] = listOf(mapOf("key" to "text", "title" to "Text",
                    "type" to if (valid is List<*>) "list" else "text", "default" to valid))
                defaultsFile.writeText(Json.pretty(defaultsManifest))
                ManifestCheck(Sdk(root)).check(defaults)
            }
            val pbm = File(scratch, "raster.pbm")
            for (pixel in listOf(0x23, 0x20, 0x09, 0x0a, 0x0d, 0x00, 0xff)) {
                pbm.writeBytes("P4\n# header comment\n8 1\n".toByteArray() + byteArrayOf(pixel.toByte()))
                val row = (7 downTo 0).joinToString("") { if (pixel and (1 shl it) != 0) "#" else "." }
                check(Pictures.show(listOf(pbm)) == "raster:\n$row\n\n")
            }
            pbm.writeText("P1\n# comment\n3 1\n0 # white\n1 0\n")
            check(Pictures.show(listOf(pbm)) == "raster:\n.#.\n\n")

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

                val packageSource = File(scratch, "limits/hello")
                source.copyRecursively(packageSource)
                File(packageSource, "strings").deleteRecursively()
                val data = File(packageSource, "files").apply { mkdirs() }
                fun refusedPackage(reason: String) {
                    try { realBuilder.build(packageSource, built); error("invalid package accepted") }
                    catch (error: BuildError) { check(reason in error.message.orEmpty()) { error.message.orEmpty() } }
                    check(!File(built, "app.wasm").exists())
                }
                for (number in 1..6) File(data, "data$number").writeText("small")
                realBuilder.build(packageSource, built)
                File(data, "data7").writeText("small")
                refusedPackage("at most 8 files")
                File(data, "data7").delete()
                File(source, "strings").copyRecursively(File(packageSource, "strings"))
                refusedPackage("at most 8 files")
                data.deleteRecursively()
                data.mkdirs()
                File(data, "data.new").writeText("reserved staging name")
                refusedPackage("file's name")
                data.deleteRecursively()
                val packageManifest = File(packageSource, "manifest.json")
                val largeManifest = LinkedHashMap(Json.parse(packageManifest.readText()) as Map<*, *>)
                largeManifest["extra"] = "x".repeat(8192)
                packageManifest.writeText(Json.pretty(largeManifest))
                refusedPackage("manifest exceeds 8192")

                // Ordinary CMake builds must observe edits to existing generation inputs.
                val cmakeSource = File(scratch, "cmake/changed").apply { mkdirs() }
                File(cmakeSource, "manifest.json").writeText("""{"name":"changed","title":"Changed","abi":3}""")
                val strings = File(cmakeSource, "strings").apply { mkdirs() }
                val english = File(strings, "en.json").apply { writeText("""{"text":"Before"}""") }
                val pictures = File(cmakeSource, "pictures").apply { mkdirs() }
                val picture = File(pictures, "dot.txt").apply { writeText("#.\n") }
                File(cmakeSource, "main.c").writeText("""
                    #include "host_api.h"
                    #include "strings.h"
                    #include "pictures.h"
                    void app_on_draw(void) {
                        gfx_draw_picture(0, 0, PICTURE_DOT, 1);
                        log_write(1, app_string(STR_TEXT), 6);
                    }
                """.trimIndent())
                val cmakeSdk = File(scratch, "cmake-sdk")
                File(root, "sdk/cmake").copyRecursively(File(cmakeSdk, "sdk/cmake"))
                for (name in listOf("lib", "host", "host_api.h")) {
                    File(root, "sdk/$name").copyRecursively(File(cmakeSdk, "sdk/$name"))
                }
                fun quoted(text: String) = "'" + text.replace("'", "'\"'\"'") + "'"
                val testTool = File(cmakeSdk, "tools/perinkpheral")
                testTool.parentFile.mkdirs()
                val java = File(System.getProperty("java.home"), "bin/java").path
                val classpath = System.getProperty("java.class.path").split(File.pathSeparator)
                    .joinToString(File.pathSeparator) { File(it).absolutePath }
                testTool.writeText("#!/bin/sh\nexec ${quoted(java)} ${quoted("-Dperinkpheral.test.root=${root.path}")} -cp ${quoted(classpath)} " +
                    "dev.perinkpheral.core.BuilderSafetyTest \"\$@\"\n")
                check(testTool.setExecutable(true))
                File(cmakeSource, "CMakeLists.txt").writeText("""
                    cmake_minimum_required(VERSION 3.20)
                    include("${cmakeSdk.path}/sdk/cmake/perinkpheral.cmake")
                    project(changed C)
                    perinkpheral_app(.)
                """.trimIndent())
                val cmakeBuild = File(scratch, "cmake-build")
                fun cmake(vararg arguments: String) {
                    val process = ProcessBuilder(listOf("cmake") + arguments).redirectErrorStream(true).start()
                    val output = process.inputStream.bufferedReader().readText()
                    check(process.waitFor() == 0) { output }
                }
                cmake("-S", cmakeSource.path, "-B", cmakeBuild.path)
                cmake("--build", cmakeBuild.path, "--target", "changed")
                val module = File(cmakeBuild, "modules/changed/app.wasm")
                val before = module.readBytes()
                english.writeText("""{"text":"After!","added":"New"}""")
                picture.writeText(".#\n")
                // Changed within the same second as the first build, a file can look no newer to make.
                val later = System.currentTimeMillis() + 5_000
                check(english.setLastModified(later) && picture.setLastModified(later))
                cmake("--build", cmakeBuild.path, "--target", "changed")
                check(!before.contentEquals(module.readBytes()))
                check("0x41, 0x66, 0x74, 0x65, 0x72, 0x21," in File(cmakeBuild, "generated/changed/strings.c").readText())
                check("STR_ADDED" in File(cmakeBuild, "generated/changed/strings.h").readText())
                check("0x40" in File(cmakeBuild, "generated/changed/pictures.h").readText())

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
