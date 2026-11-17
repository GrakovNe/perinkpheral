// SPDX-License-Identifier: MIT
package org.perinkpheral.core.build

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.Json
import java.io.File

/**
 * The rules of firmware/components/appsys/app_manifest.c, before the device sees the
 * manifest, in the same words.
 */
class ManifestCheck(private val sdk: Sdk) {
    private val nameMax = sdk.limit("NAME_MAX")
    private val titleMax = sdk.limit("TITLE_MAX")
    private val descriptionMax = sdk.limit("DESCRIPTION_MAX")
    private val settingValueMax = sdk.limit("SETTING_VALUE_MAX")
    private val versionMax = sdk.limit("VERSION_MAX")
    private val pollsMax = sdk.limit("POLLS_MAX")
    private val everyMin = sdk.limit("POLL_EVERY_MIN_S")
    private val everyMax = sdk.limit("POLL_EVERY_MAX_S")
    private val urlMax = sdk.limit("URL_MAX")
    private val headersMax = sdk.limit("HEADERS_MAX")
    private val headerNameMax = sdk.limit("HEADER_NAME_MAX")
    private val headerValueMax = sdk.limit("HEADER_VALUE_MAX")
    private val pickPathsMax = sdk.limit("PICK_PATHS_MAX")
    private val pickPathMax = sdk.limit("PICK_PATH_MAX")
    private val pickKeyMax = sdk.contract.number("JSON_PICK_KEY_MAX")
    private val pickDepthMax = sdk.contract.number("JSON_PICK_DEPTH_MAX")
    private val listItemMax = sdk.limit("LIST_ITEM_MAX")
    private val iconCells = sdk.limit("ICON_CELLS")
    private val hostsMax = sdk.limit("HOSTS_MAX")
    private val hostMax = sdk.limit("HOST_MAX")
    private val settingsMax = sdk.limit("SETTINGS_MAX")
    private val settingKeyMax = sdk.limit("SETTING_KEY_MAX")
    private val settingTitleMax = sdk.limit("SETTING_TITLE_MAX")
    private val settingHelpMax = sdk.limit("SETTING_HELP_MAX")
    private val settingOptionsMax = sdk.limit("SETTING_OPTIONS_MAX")
    private val settingOptionMax = sdk.limit("SETTING_OPTION_MAX")
    private val bytesPerCharacter = sdk.limit("TEXT_BYTES_PER_CHARACTER")
    private val certificatesMax = sdk.limit("CERTIFICATES_MAX")
    private val eachPrefixMax = sdk.limit("EACH_PREFIX_MAX")
    private val fileNameMax = sdk.limit("FILE_NAME_MAX")

    /** Checks `<app>/manifest.json`; returns it parsed. */
    fun check(appDir: File): Map<*, *> {
        val file = File(appDir, "manifest.json")
        val path = file.path
        val text = try {
            file.readText()
        } catch (error: Exception) {
            fail("$path: ${error.message}")
        }
        val manifest = try {
            Json.parse(text)
        } catch (error: Json.Error) {
            fail("$path: ${error.message}")
        } as? Map<*, *> ?: fail("$path: not an object")
        if ("\\u0000" in text) fail("$path: a string must not contain \\u0000")
        val name = manifest["name"]
        if (name !is String || !Regex("[a-z0-9_-]{1,$nameMax}").matches(name)) {
            fail("$path: \"name\" must be 1-$nameMax of a-z, 0-9, - and _")
        }
        if (name != appDir.absoluteFile.normalize().name) {
            fail("$path: \"name\" must be the name of the application's folder")
        }
        if (!isWords(manifest["title"], 1, titleMax)) fail("$path: \"title\" must be 1-$titleMax characters")
        if ("description" in manifest && !isWords(manifest["description"], 1, descriptionMax)) {
            fail("$path: \"description\" must be 1-$descriptionMax characters")
        }
        val version = manifest["version"] ?: ""
        if (version !is String || version.toByteArray().size > versionMax) {
            fail("$path: \"version\" must be a string of at most $versionMax bytes")
        }
        val companion = manifest["companion"]
        if (companion != null && !(companion is String && companion.length <= urlMax &&
                Regex("https://[!#-;=?-~]+").matches(companion))) {
            fail("$path: \"companion\" must be an https address of at most $urlMax bytes")
        }
        if ((manifest["fullscreen"] ?: false) !is Boolean) fail("$path: \"fullscreen\" must be true or false")
        if ((manifest["system"] ?: false) !is Boolean) fail("$path: \"system\" must be true or false")
        if ((manifest["writes_files"] ?: false) !is Boolean) fail("$path: \"writes_files\" must be true or false")
        val certificates = manifest["certificates"] ?: emptyList<Any>()
        val fileName = Regex("[a-z0-9_-][a-z0-9._-]{0,${fileNameMax - 1}}")
        if (certificates !is List<*> || certificates.size > certificatesMax || certificates.any { file ->
                file !is String || !fileName.matches(file) || file == "app.wasm" || file == "manifest.json"
            }) {
            fail("$path: \"certificates\" must be a list of at most $certificatesMax files of the application's " +
                "\"files\" folder, by names of 1-$fileNameMax of a-z, 0-9, '.', '-' and '_'")
        }
        val icon = manifest["icon"]
        if (icon != null && (icon !is List<*> || icon.size.toLong() != iconCells ||
                icon.any { it !is String || !Regex("[#.]{$iconCells}").matches(it) })) {
            fail("$path: \"icon\" must be $iconCells rows of $iconCells characters, '#' for ink and '.' for paper")
        }
        // The settings first: a host and a poll may name the server one of them holds.
        val settings = manifest["settings"] ?: emptyList<Any>()
        checkSettings(path, settings)
        settings as List<*>
        val servers = settings.map { it as Map<*, *> }.filter { it["type"] in setOf("url", "link") }.map { it["key"] as String }
        val links = settings.map { it as Map<*, *> }.filter { it["type"] == "link" }.map { it["key"] as String }
        val hosts = manifest["hosts"] ?: emptyList<Any>()
        if (hosts !is List<*> || hosts.size > hostsMax || hosts.any { host ->
                host !is String || host.toByteArray().size !in 1..hostMax ||
                    (host.startsWith("{{") && referencesServer(host, servers) != "")
            }) {
            fail("$path: \"hosts\" must be a list of at most $hostsMax host names of 1-$hostMax bytes, " +
                "or {{key}} of a setting of the \"url\" or \"link\" type")
        }
        val intents = manifest["intents"] ?: emptyList<Any>()
        if (intents !is List<*> || intents.any { it !in INTENTS }) {
            fail("$path: \"intents\" must be a list of ${INTENTS.joinToString(", ")}")
        }
        val polls = manifest["polls"] ?: emptyList<Any>()
        if (polls !is List<*> || polls.size > pollsMax) fail("$path: \"polls\" must be a list of at most $pollsMax")
        polls.forEachIndexed { number, poll ->
            checkPoll(path, number, poll, servers, settings)
            // A link is the whole address: nothing stands after it.
            if (poll is Map<*, *> && referencesServer(poll["url"] as? String ?: "", links) !in setOf(null, "")) {
                fail("$path: poll $number: a setting of the \"link\" type is the whole \"url\"")
            }
        }
        val abi = manifest["abi"]
        if (abi !is Long || abi != sdk.hostApiVersion) {
            fail("$path: \"abi\" must be ${sdk.hostApiVersion}, the version of sdk/host_api.h")
        }
        // The least firmware it runs on: the build writes the latest its calls came with; one
        // given by hand may ask for more, for a behaviour, never past this SDK's VERSION.
        val firmware = manifest["firmware"]
        if (firmware != null) {
            val number = (firmware as? String)?.let { versionNumber(it) }
                ?: fail("$path: \"firmware\" must be a version such as 0.1.8")
            if (number > versionNumber(sdk.version)!!) {
                fail("$path: \"firmware\" $firmware is newer than this SDK, ${sdk.version}")
            }
        }
        return manifest
    }

    /** A text a person reads: so many characters, and no more bytes than letters of two
     * bytes would take - the device's rule (app_manifest.c). */
    fun isWords(text: Any?, least: Long, most: Long): Boolean =
        text is String && text.codePointCount(0, text.length) in least..most &&
            text.toByteArray().size <= most * bytesPerCharacter

    private fun referencesServer(text: String, servers: List<String>): String? {
        for (key in servers) {
            if (text.startsWith("{{$key}}")) return text.substring(key.length + 4)
        }
        return null
    }

    private fun isNumber(value: Any?): Boolean = value is Long || value is Double

    private fun checkPoll(path: String, number: Int, poll: Any?, servers: List<String>, settings: List<*>) {
        val where = "$path: poll $number"
        if (poll !is Map<*, *>) fail("$where must be an object")
        val url = poll["url"]
        if (url !is String || url.toByteArray().size > urlMax ||
            !(url.startsWith("http://") || url.startsWith("https://") || referencesServer(url, servers) != null)) {
            fail("$where: \"url\" must be an http(s) URL of at most $urlMax bytes, or begin with " +
                "{{key}} of a setting of the \"url\" or \"link\" type")
        }
        val every = poll["every"]
        if (every is String) {
            // "{{key}}": the pace is the user's choice in that setting.
            val named = settings.map { it as Map<*, *> }.firstOrNull { every == "{{${it["key"]}}}" }
                ?: fail("$where: \"every\" as text must be {{key}} of a setting")
            val paces = ((named["options"] as? List<*>) ?: emptyList<Any>()) + listOf(named["default"] ?: "1 min")
            for (value in paces) {
                if (paceSeconds(value) !in everyMin..everyMax) {
                    fail("$where: \"$value\" of the setting \"${named["key"]}\" is not a pace of $everyMin " +
                        "seconds to a day: write \"15 min\", \"1 h\", \"30 s\"")
                }
            }
        } else if (every !is Long || every !in everyMin..everyMax) {
            // An integer, as the device's JSON has only those: 1e2 is no pace to it.
            fail("$where: \"every\" must be $everyMin to $everyMax seconds, or {{key}} of a setting")
        }
        val headers = poll["headers"] ?: emptyMap<String, Any>()
        if (headers !is Map<*, *> || headers.size > headersMax) {
            fail("$where: \"headers\" must be an object of at most $headersMax")
        }
        for ((name, value) in headers) {
            name as String
            if (name.isEmpty() || name.toByteArray().size > headerNameMax || value !is String ||
                value.toByteArray().size > headerValueMax) {
                fail("$where: header \"$name\" has a name over $headerNameMax bytes or a value that is not a " +
                    "string of at most $headerValueMax")
            }
        }
        val pick = poll["pick"] ?: emptyList<Any>()
        if (pick !is List<*> || pick.size > pickPathsMax) {
            fail("$where: \"pick\" must be a list of at most $pickPathsMax paths")
        }
        for (picked in pick) {
            if (picked !is String || picked.toByteArray().size !in 1..pickPathMax ||
                !isPickPath(picked)) {
                fail("$where: a path in \"pick\" must be a string of 1 to $pickPathMax bytes with \"[*]\" " +
                    "once at most, keys of at most $pickKeyMax ASCII bytes and at most $pickDepthMax " +
                    "property or index levels: $picked")
            }
        }
        val each = poll["each"]
        val lists = settings.map { it as Map<*, *> }.filter { it["type"] == "list" }.map { it["key"] }
        if (each != null && each !in lists) {
            fail("$where: \"each\" must be the key of a setting of the \"list\" type")
        }
        val prefix = poll["prefix"]
        if (prefix != null && (each == null || prefix !is String || prefix.isEmpty() ||
                prefix.length > eachPrefixMax || prefix.any { it <= ' ' || it > '~' })) {
            fail("$where: \"prefix\" is of a poll with \"each\": 1-$eachPrefixMax printable characters " +
                "without spaces, which the items it takes begin with")
        }
    }

    private fun isPickPath(path: String): Boolean {
        val part = Regex("""([^.\[\]]+)|\[(\*|0|[1-9][0-9]*)\]""")
        var at = 0
        var depth = 0
        var wildcards = 0
        while (at < path.length) {
            val found = part.find(path, at) ?: return false
            if (found.range.first != at || ++depth > pickDepthMax) return false
            val key = found.groupValues[1]
            val index = found.groupValues[2]
            if (key.isNotEmpty()) {
                if (key.length > pickKeyMax || key.any { it < ' ' || it > '~' }) return false
            } else if (index == "*") {
                if (++wildcards > 1) return false
            } else if (index.toIntOrNull() == null) return false
            at = found.range.last + 1
            if (at < path.length && path[at] == '.') {
                at++
                if (at == path.length || path[at] == '[') return false
            } else if (at < path.length && path[at] != '[') return false
        }
        return depth > 0
    }

    private fun checkSettings(path: String, settings: Any?) {
        if (settings !is List<*> || settings.size > settingsMax) {
            fail("$path: \"settings\" must be a list of at most $settingsMax")
        }
        val keys = HashSet<String>()
        settings.forEachIndexed { number, setting ->
            val where = "$path: setting $number"
            if (setting !is Map<*, *>) fail("$where must be an object")
            val key = setting["key"]
            if (key !is String || key.codePointCount(0, key.length) !in 1..settingKeyMax ||
                key.any { it < '!' || it > '~' }) {
                fail("$where: \"key\" must be 1-$settingKeyMax characters from '!' to '~'")
            }
            if (!keys.add(key)) fail("$where: the key \"$key\" is used twice")
            if (!isWords(setting["title"], 1, settingTitleMax)) {
                fail("$where: \"title\" must be 1-$settingTitleMax characters")
            }
            val kind = setting["type"]
            if (kind !in SETTING_TYPES) fail("$where: \"type\" must be one of ${SETTING_TYPES.joinToString(", ")}")
            // Set by something else - an integration, a companion page - and left off the device's form.
            if (setting["hidden"] != null && setting["hidden"] !is Boolean) fail("$where: \"hidden\" must be true or false")
            if (!isWords(setting["help"] ?: "", 0, settingHelpMax)) {
                fail("$where: \"help\" must be a string of at most $settingHelpMax characters")
            }
            val options = setting["options"]
            if (kind != "choice") {
                if (options != null) fail("$where: only a choice has \"options\"")
            } else if (options !is List<*> || options.size !in 1..settingOptionsMax ||
                options.any { it !is String || !isWords(it, 1, settingOptionMax) }) {
                fail("$where: \"options\" must be 1-$settingOptionsMax strings of 1-$settingOptionMax characters")
            }
        }
        settings.forEachIndexed { number, setting ->
            setting as Map<*, *>
            val default = setting["default"] ?: return@forEachIndexed
            val kind = setting["type"]
            if (kind == "list") {
                if (default !is List<*> || default.isEmpty() || default.any { item ->
                        item !is String || item.length.toLong() !in 1..listItemMax || item.any { it < ' ' || it > '~' }
                    }) {
                    fail("$path: setting $number: the \"default\" of a list is a list of items of " +
                        "1-$listItemMax printable ASCII characters")
                }
                // Stored as one value, the items a line apart: as long as any value may be.
                if (default.joinToString("\n").length > settingValueMax) {
                    fail("$path: setting $number: the \"default\" of a list is $settingValueMax bytes at most, " +
                        "its items a line apart")
                }
                return@forEachIndexed
            }
            // The device's rules for a default (app_manifest.c, has_sound_default): what it would
            // refuse to load is refused here.
            val fits = default is String && default.isNotEmpty() && kind !in setOf("secret", "place") &&
                default.toByteArray().size <= settingValueMax &&
                (kind != "toggle" || default in setOf("0", "1")) &&
                (kind != "choice" || default in (setting["options"] as List<*>)) &&
                (kind != "number" || Regex("""-?(\d+\.?\d*|\.\d+)""").matches(default)) &&
                (kind != "url" || Regex("""https?://[A-Za-z0-9.-]+(:[0-9]{1,5})?""").matches(default)) &&
                (kind != "link" || Regex("""https?://[!-~]+""").matches(default))
            if (!fits) fail("$path: setting $number: \"default\" must be a value the setting takes")
        }
        val numbers = settings.map { it as Map<*, *> }.filter { it["type"] == "number" }.map { it["key"] }.toSet()
        settings.forEachIndexed { number, setting ->
            setting as Map<*, *>
            if (setting["type"] != "place") return@forEachIndexed
            val latitude = setting["latitude"]
            val longitude = setting["longitude"]
            if (latitude == longitude || latitude !in numbers || longitude !in numbers) {
                fail("$path: setting $number: a place names two number settings as its \"latitude\" and \"longitude\"")
            }
        }
    }

    companion object {
        val INTENTS = listOf("time_changed", "network_up", "started")
        val SETTING_TYPES = listOf("text", "secret", "number", "choice", "toggle", "place", "url", "link", "list")

        /** A pace as a person writes it, in seconds; 0 when the text is not one. */
        fun paceSeconds(text: Any?): Long {
            if (text !is String) return 0
            val found = Regex("""(\d{1,6}) *(?:([smhd])[a-z]*)?""").matchEntire(text) ?: return 0
            val unit = when (found.groupValues[2]) {
                "", "s" -> 1L
                "m" -> 60L
                "h" -> 3600L
                else -> 86400L
            }
            return found.groupValues[1].toLong() * unit
        }

        fun fail(message: String): Nothing = throw BuildError(message)
    }
}
