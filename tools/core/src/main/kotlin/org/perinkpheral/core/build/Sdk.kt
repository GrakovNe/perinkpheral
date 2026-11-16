// SPDX-License-Identifier: MIT
package org.perinkpheral.core.build

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.Json
import java.io.File

/**
 * What the build takes from the SDK checkout: the limits of a manifest
 * (`sdk/lib/manifest_limits.h`), the contract (`sdk/host_api.h`) and the languages
 * (`sdk/languages.json`). Read from the files, never written here twice.
 */
class Sdk(val root: File) {
    val sdk = File(root, "sdk")
    val library = File(sdk, "lib")
    private val hostApi: String by lazy { File(sdk, "host_api.h").readText() }

    /** MANIFEST_<NAME> of manifest_limits.h, without the prefix. */
    val limits: Map<String, Long> by lazy {
        val text = File(library, "manifest_limits.h").readText()
        Regex("""^#define\s+MANIFEST_(\w+)\s+([^/\n]+)""", RegexOption.MULTILINE).findAll(text)
            .associate { match ->
                val (name, written) = match.destructured
                // Numbers and products of them, nothing else.
                if (!Regex("""[\d\s*()]+""").matches(written.trim())) {
                    throw BuildError("sdk/lib/manifest_limits.h: $name is not a number")
                }
                name to written.replace("(", "").replace(")", "").split('*')
                    .fold(1L) { product, factor -> product * factor.trim().toLong() }
            }
    }

    fun limit(name: String): Long = limits[name] ?: throw BuildError("sdk/lib/manifest_limits.h has no MANIFEST_$name")

    /** HOST_KV_TOTAL_MAX: what all values of one application may take of its store. */
    val storeMax: Long by lazy {
        Regex("""#define\s+HOST_KV_TOTAL_MAX\s+(\d+)""").find(hostApi)?.groupValues?.get(1)?.toLong()
            ?: throw BuildError("sdk/host_api.h does not define HOST_KV_TOTAL_MAX as a number")
    }

    val hostApiVersion: Long by lazy {
        Regex("""#define\s+HOST_API_VERSION\s+(\d+)""").find(hostApi)?.groupValues?.get(1)?.toLong()
            ?: throw BuildError("sdk/host_api.h does not define HOST_API_VERSION as a number")
    }

    /** The calls host_api.h declares with this macro. */
    fun imports(macro: String = "HOST_IMPORT"): Set<String> =
        hostApi.lineSequence().filter { it.startsWith("$macro(") }
            .map { it.substring(macro.length + 1, it.indexOf(',')) }.toSet()

    /** The exports the host calls, as host_api.h names them. */
    val knownHandlers: Set<String> by lazy {
        Regex("""\bapp_(?:on_[a-z]+|init|data_buffer)\b""").findAll(hostApi).map { it.value }.toSet()
    }

    /** What the system knows: code -> the code points its glyph packs add to ASCII. */
    val languages: Map<String, Set<Int>> by lazy {
        val known = Json.parse(File(sdk, "languages.json").readText()) as Map<*, *>
        val packs = known["packs"] as Map<*, *>
        (known["languages"] as List<*>).associate { language ->
            language as Map<*, *>
            val points = HashSet<Int>()
            for (pack in language["packs"] as List<*>) {
                for (range in packs[pack] as List<*>) {
                    range as List<*>
                    val first = (range[0] as Long).toInt()
                    val last = (range[1] as Long).toInt()
                    for (point in first..last) points += point
                }
            }
            language["code"] as String to points
        }
    }
}
