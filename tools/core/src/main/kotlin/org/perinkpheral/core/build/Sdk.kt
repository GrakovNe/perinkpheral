// SPDX-License-Identifier: MIT
package org.perinkpheral.core.build

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.Json
import org.perinkpheral.core.contract.Contract
import java.io.File

/**
 * What the build takes from the SDK checkout: the limits of a manifest
 * (`sdk/lib/manifest_limits.h`), the contract (`sdk/host_api.h`) and the languages
 * (`sdk/languages.json`). Read from the files, never written here twice.
 */
class Sdk(val root: File) {
    val sdk = File(root, "sdk")
    val library = File(sdk, "lib")
    /** The contract and the limits, read once. */
    val contract: Contract by lazy { Contract.of(sdk) }

    /** MANIFEST_<NAME> of manifest_limits.h, without the prefix. */
    val limits: Map<String, Long> by lazy {
        contract.namesStartingWith("MANIFEST_").associateWith { contract.number("MANIFEST_$it") }
    }

    fun limit(name: String): Long = limits[name] ?: throw BuildError("sdk/lib/manifest_limits.h has no MANIFEST_$name")

    /** HOST_KV_TOTAL_MAX: what all values of one application may take of its store. */
    val storeMax: Long by lazy { contract.number("HOST_KV_TOTAL_MAX") }

    /** VERSION of the checkout: the least version of the next firmware, past which no
     * application's "firmware" may ask. What a build stamps is the latest of its calls'. */
    val version: String by lazy {
        val text = File(root, "VERSION").takeIf { it.isFile }?.readText()?.trim()
            ?: throw BuildError("${root.path} has no VERSION")
        if (versionNumber(text) == null) throw BuildError("VERSION: \"$text\" is not a version such as 0.1.8")
        text
    }

    /** HOST_SINCE_<entry> stamps only additions after the ABI firmware floor.
     * Every stamp names a contract entry and no version past VERSION. */
    val callSince: Map<String, String> by lazy {
        val calls = (contract.calls.map { it.name } + contract.exports.map { it.name }).toSet()
        contract.namesStartingWith("HOST_SINCE_").associate { call ->
            val since = contract.string("HOST_SINCE_$call") ?: throw BuildError("sdk/host_api.h: HOST_SINCE_$call is not a version")
            if (call !in calls && contract.written(call) == null) throw BuildError("sdk/host_api.h: HOST_SINCE_$call names no contract entry")
            val number = versionNumber(since) ?: throw BuildError("sdk/host_api.h: HOST_SINCE_$call is not a version")
            if (number <= versionNumber(firmwareFirst)!!) {
                throw BuildError("sdk/host_api.h: HOST_SINCE_$call must be after the ABI floor $firmwareFirst")
            }
            if (number > versionNumber(version)!!) {
                throw BuildError("sdk/host_api.h: HOST_SINCE_$call is $since, past VERSION $version")
            }
            call to since
        }
    }

    val firmwareFirst: String by lazy {
        contract.string("HOST_FIRMWARE_FIRST")?.takeIf { versionNumber(it) != null }
            ?: throw BuildError("sdk/host_api.h does not define HOST_FIRMWARE_FIRST as a version")
    }

    /** The least firmware an application that makes these calls runs on, and the call that
     * asks for it (null when none asks for more than the first). */
    fun firmwareFor(calls: Collection<String>): Pair<String, String?> {
        val floor = contract.string("HOST_ABI_${hostApiVersion}_FIRMWARE_FIRST")
            ?: throw BuildError("SDK has no first firmware for ABI $hostApiVersion")
        if (versionNumber(floor) == null || versionNumber(floor)!! > versionNumber(version)!!) {
            throw BuildError("ABI firmware floor $floor is invalid or past VERSION $version")
        }
        val newest = calls.filter { it in callSince }.maxByOrNull { versionNumber(callSince.getValue(it))!! }
        return if (newest != null && versionNumber(callSince.getValue(newest))!! > versionNumber(floor)!!) {
            callSince.getValue(newest) to newest
        } else floor to null
    }

    val hostApiVersion: Long by lazy { contract.number("HOST_API_VERSION") }

    /** The calls host_api.h declares with this macro: HOST_IMPORT, or HOST_SYSTEM_IMPORT. */
    fun imports(macro: String = "HOST_IMPORT"): Set<String> =
        contract.calls.filter { it.isSystem == (macro == "HOST_SYSTEM_IMPORT") }.map { it.name }.toSet()

    /** The exports the host calls, as host_api.h declares them. */
    val knownHandlers: Set<String> by lazy { contract.exports.map { it.name }.toSet() }

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

/** "0.1.8" as a number that orders versions, as the host reads one (app_version_number):
 * each part 0 to 999; null when it is no such version. */
fun versionNumber(text: String): Long? {
    val parts = Regex("""^(\d{1,3})\.(\d{1,3})\.(\d{1,3})$""").find(text)?.destructured?.toList() ?: return null
    return parts.fold(0L) { whole, part -> whole * 1000 + part.toLong() }
}
