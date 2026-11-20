// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.app

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.build.ManifestAuthor
import dev.perinkpheral.core.device.DeviceClient
import java.io.File
import java.nio.file.Files

/** What is done with an application: put on a device, made anew, prepared for the simulator. */
object Applications {
    /** The name in an application's manifest. */
    fun nameOf(appDir: File): String {
        val manifest = try {
            Json.parse(File(appDir, AppBuilder.MANIFEST).readText()) as? Map<*, *>
        } catch (error: Json.Error) {
            throw BuildError("${appDir.path}/${AppBuilder.MANIFEST}: ${error.message}")
        }
        return manifest?.get("name") as? String ?: throw BuildError("${appDir.path}/${AppBuilder.MANIFEST} has no name")
    }

    /**
     * Builds the application, puts it on the device, restarts the device and opens the
     * application. Returns its name. Cancelled, it stops at the next step and throws
     * InterruptedException; whoever cancels may also cut the request under way
     * (DeviceClient.abort).
     */
    fun toDevice(builder: AppBuilder, client: DeviceClient, appDir: File, say: (String) -> Unit,
                 isCancelled: () -> Boolean = { false }): String {
        val packed = Files.createTempDirectory("perinkpheral-app").toFile()
        try {
            val built = builder.build(appDir, packed)
            for (file in AppBuilder.packageFiles(packed)) {
                if (isCancelled()) throw InterruptedException("cancelled")
                client.uploadAppFile(built.name, file, File(packed, file).readBytes())
            }
            if (isCancelled()) throw InterruptedException("cancelled")
            client.restartInto(built.name)
            say("uploaded ${built.name}, restarting into it")
            client.waitUntilBack(isCancelled)
            // The device starts with its default application when one is chosen: the one just
            // uploaded is opened by name.
            client.open(built.name)
            return built.name
        } finally {
            packed.deleteRecursively()
        }
    }

    /** Sends a firmware image; the device installs it and restarts. Returns what it says when
     * it is back. */
    fun flash(client: DeviceClient, image: File, say: (String) -> Unit, isCancelled: () -> Boolean = { false }): String {
        say("Entering recovery; confirm installation with OK on the device when asked.")
        say(client.flash(image.readBytes(), isCancelled).trim())
        val status = Json.parse(client.waitUntilBack(isCancelled)) as? Map<*, *>
        return "back: firmware ${status?.get("firmware")} in ${status?.get("slot")}" +
            if (status?.get("on_probation") == true) ", on probation" else ""
    }

    /** What of a package an earlier build may have left in a simulator's state and this one
     * would not replace: a language's table or the manifest's words in it, removed since. */
    private val PACKAGE_ONLY = Regex("""(strings|manifest)\.[a-z]{2}""")

    /** Builds the application into a simulator's state: the package goes over what is there,
     * so the files the application wrote stay, as they do on the device when it is updated.
     * Returns its name. */
    fun toSimulator(builder: AppBuilder, appDir: File, state: File): String {
        val packed = Files.createTempDirectory("perinkpheral-app").toFile()
        try {
            val built = builder.build(appDir, packed)
            install(packed, built.name, state)
            return built.name
        } finally {
            packed.deleteRecursively()
        }
    }

    /** A package as the build left it in `packed`, into a simulator's state; `packed` is only
     * read, so one build serves several simulators. */
    fun install(packed: File, name: String, state: File) {
        val installed = File(File(state, "apps"), name)
        installed.listFiles().orEmpty().filter { PACKAGE_ONLY.matches(it.name) }.forEach { it.delete() }
        installed.mkdirs()
        for (file in AppBuilder.packageFiles(packed)) {
            File(packed, file).copyTo(File(installed, file), overwrite = true)
        }
    }

    /** A new application in `folder`: the SDK's sample under its name, a manifest of its own and
     * `cmakeLists`. Returns its source file. */
    @Suppress("UNUSED_PARAMETER")
    fun create(sdkRoot: File, folder: File, title: String, cmakeLists: (String) -> String): File =
        throw BuildError("an author is required for a new application; use --author or update the IDE plugin")

    fun create(sdkRoot: File, folder: File, title: String, author: String, cmakeLists: (String) -> String): File {
        val checkedAuthor = ManifestAuthor(Sdk(sdkRoot)).checkPersonal(author)
        val name = folder.absoluteFile.normalize().name
        val most = Sdk(sdkRoot).limit("NAME_MAX")
        if (!Regex("[a-z0-9_-]{1,$most}").matches(name)) {
            throw BuildError("\"$name\": an application's name is 1-$most of a-z, 0-9, '-' and '_', and it is its folder's name")
        }
        if (folder.exists()) throw BuildError("${folder.path} is there already")
        val sample = File(sdkRoot, "sdk/apps/hello")
        @Suppress("UNCHECKED_CAST")
        val manifest = LinkedHashMap(Json.parse(File(sample, AppBuilder.MANIFEST).readText()) as Map<String, Any?>)
        manifest["name"] = name
        manifest["title"] = title.ifBlank { name.replace('-', ' ').replace('_', ' ').replaceFirstChar { it.uppercase() } }
        manifest["author"] = checkedAuthor
        manifest["version"] = "0.1.0" // a new application's, not the sample's
        folder.mkdirs()
        File(folder, AppBuilder.MANIFEST).writeText(Json.pretty(manifest, ensureAscii = true) + "\n")
        // The sample's code, without the line that says whose it is: the new one is its author's.
        val source = File(folder, "$name.c")
        source.writeText(File(sample, "hello.c").readLines().filterNot { it.startsWith(" * Copyright") }
            .joinToString("\n", postfix = "\n"))
        File(folder, "CMakeLists.txt").writeText(cmakeLists(name))
        return source
    }
}
