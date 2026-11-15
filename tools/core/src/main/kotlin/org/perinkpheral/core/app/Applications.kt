// SPDX-License-Identifier: MIT
package org.perinkpheral.core.app

import org.perinkpheral.core.build.AppBuilder
import org.perinkpheral.core.build.BuildError
import org.perinkpheral.core.build.Json
import org.perinkpheral.core.device.DeviceClient
import java.io.File
import java.nio.file.Files

/** What is done with an application: put on a device, made anew, prepared for the simulator. */
object Applications {
    private val NAME = Regex("[a-z0-9_-]{1,13}")

    /** The name in an application's manifest. */
    fun nameOf(appDir: File): String =
        (Json.parse(File(appDir, "manifest.json").readText()) as? Map<*, *>)?.get("name") as? String
            ?: throw BuildError("${appDir.path}/manifest.json has no name")

    /** Builds the application, puts it on the device, restarts the device and opens the
     * application. Returns its name. */
    fun toDevice(builder: AppBuilder, client: DeviceClient, appDir: File, say: (String) -> Unit,
                 isCancelled: () -> Boolean = { false }): String {
        val built = Files.createTempDirectory("perinkpheral-app").toFile()
        try {
            val name = builder.build(appDir, built)
            // The manifest last: until it is there the device does not list the application, and
            // by then everything it needs is.
            val data = built.listFiles().orEmpty().map { it.name }.filter { it != "app.wasm" && it != "manifest.json" }.sorted()
            for (file in listOf("app.wasm") + data + listOf("manifest.json")) {
                if (isCancelled()) return name
                client.uploadAppFile(name, file, File(built, file).readBytes())
            }
            client.restartInto(name)
            say("uploaded $name, restarting into it")
            client.waitUntilBack(isCancelled)
            // The device starts with its default application when one is chosen: the one just
            // uploaded is opened by name.
            client.open(name)
            return name
        } finally {
            built.deleteRecursively()
        }
    }

    /** Sends a firmware image; the device installs it and restarts. Returns what it says when
     * it is back. */
    fun flash(client: DeviceClient, image: File, say: (String) -> Unit, isCancelled: () -> Boolean = { false }): String {
        say(client.flash(image.readBytes()).trim())
        val status = Json.parse(client.waitUntilBack(isCancelled)) as? Map<*, *>
        return "back: firmware ${status?.get("firmware")} in ${status?.get("slot")}" +
            if (status?.get("on_probation") == true) ", on probation" else ""
    }

    /** Builds the application into a simulator's state: the package goes over what is there,
     * so the files the application wrote stay, as they do on the device when it is updated.
     * Returns its name. */
    fun toSimulator(builder: AppBuilder, appDir: File, state: File): String {
        val built = Files.createTempDirectory("perinkpheral-app").toFile()
        try {
            val name = builder.build(appDir, built)
            built.copyRecursively(File(File(state, "apps"), name), overwrite = true)
            return name
        } finally {
            built.deleteRecursively()
        }
    }

    /** A new application in `folder`: the SDK's sample under its name, a manifest of its own and
     * `cmakeLists`. Returns its source file. */
    fun create(sdkRoot: File, folder: File, title: String, cmakeLists: (String) -> String): File {
        val name = folder.absoluteFile.normalize().name
        if (!NAME.matches(name)) {
            throw BuildError("\"$name\": an application's name is 1-13 of a-z, 0-9, '-' and '_', and it is its folder's name")
        }
        if (folder.exists()) throw BuildError("${folder.path} is there already")
        val sample = File(sdkRoot, "sdk/apps/hello")
        @Suppress("UNCHECKED_CAST")
        val manifest = LinkedHashMap(Json.parse(File(sample, "manifest.json").readText()) as Map<String, Any?>)
        manifest["name"] = name
        manifest["title"] = title.ifBlank { name.replace('-', ' ').replace('_', ' ').replaceFirstChar { it.uppercase() } }
        manifest["version"] = "0.1.0" // a new application's, not the sample's
        folder.mkdirs()
        File(folder, "manifest.json").writeText(Json.pretty(manifest, ensureAscii = true) + "\n")
        // The sample's code, without the line that says whose it is: the new one is its author's.
        val source = File(folder, "$name.c")
        source.writeText(File(sample, "hello.c").readLines().filterNot { it.startsWith(" * Copyright") }
            .joinToString("\n", postfix = "\n"))
        File(folder, "CMakeLists.txt").writeText(cmakeLists(name))
        return source
    }
}
