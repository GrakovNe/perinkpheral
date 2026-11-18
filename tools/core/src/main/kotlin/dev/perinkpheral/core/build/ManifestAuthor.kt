// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.build

import dev.perinkpheral.core.BuildError

/** Author is display metadata, not a signature or a verified identity. */
class ManifestAuthor(sdk: Sdk) {
    private val maximumBytes = sdk.limit("AUTHOR_MAX")

    fun check(value: Any?, required: Boolean = false): String? {
        if (value == null && !required) return null
        if (value !is String || value.isEmpty() || value.toByteArray(Charsets.UTF_8).size > maximumBytes ||
            value.any { it < ' ' || it == '\u007f' } || value.first() == ' ' || value.last() == ' ') {
            throw BuildError("manifest author must be nonempty text of at most $maximumBytes UTF-8 bytes, without control characters or spaces at either end")
        }
        return value
    }

    fun checkPersonal(value: String): String {
        val author = check(value, required = true)!!
        if (isOfficial(author)) throw BuildError("Perinkpheral is reserved for official applications; enter your own author name")
        return author
    }

    companion object {
        const val OFFICIAL = "Perinkpheral"
        const val OFFICIAL_REPOSITORY = "GrakovNe/perinkpheral-apps"
        fun isOfficial(author: String): Boolean = author.trim().equals(OFFICIAL, ignoreCase = true)
    }
}
