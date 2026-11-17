// SPDX-License-Identifier: MIT
package org.perinkpheral.core.contract

import org.perinkpheral.core.BuildError
import java.io.File

/**
 * The contract, sdk/host_api.h, read once for every tool: the calls an application may
 * import and the signature of each as the interpreter types it, the handlers it may export,
 * and the numbers the header and sdk/lib/manifest_limits.h define. The header is the one place
 * a call's signature is written; the host's table of imports, the build's checks, the test
 * that imports every call and the guide's table of limits are all made from what is read here.
 */
class Contract(private val text: String, private val defines: Map<String, String>) {

    /** A call of the host: `name` the import's (the macro's first argument), `declared` the C
     * function's, which need not be the same; the signature the interpreter's - i a number of
     * 32 bits or a pointer, I one of 64, v no result. */
    data class Call(val name: String, val declared: String, val signature: String, val isSystem: Boolean)

    /** A function an application may export and the host calls. */
    data class Export(val name: String, val arguments: Int, val hasResult: Boolean)

    val calls: List<Call> by lazy {
        val found = calls("HOST_IMPORT", false) + calls("HOST_SYSTEM_IMPORT", true)
        found.groupBy { it.name }.filterValues { it.size > 1 }.keys.firstOrNull()?.let {
            throw BuildError("sdk/host: $it is declared twice")
        }
        if (found.size < MIN_CALLS) throw BuildError("sdk/host_api.h: only ${found.size} calls found - the header is read wrong")
        found
    }

    val exports: List<Export> by lazy {
        val found = declarations("APP_EXPORT").map { (name, result, declared, parameters) ->
            if (declared != name) throw BuildError("sdk/host_api.h: $name is exported as $declared")
            Export(name, if (parameters == "void") 0 else parameters.split(',').size, result != "void")
        }
        if (found.size < MIN_EXPORTS) throw BuildError("sdk/host_api.h: only ${found.size} exports found - the header is read wrong")
        found
    }

    /** What a #define says, as written; null when nothing defines it. */
    fun written(name: String): String? = defines[name]

    /** A #define's value as a whole number: numbers, other defines, + - * / and brackets. */
    fun number(name: String): Long = Expression(defines).value(name)

    fun numberOrNull(name: String): Long? = if (defines.containsKey(name)) runCatching { number(name) }.getOrNull() else null

    /** A string #define without its quotes. */
    fun string(name: String): String? = defines[name]?.let { Regex("^\"([^\"]*)\"").find(it)?.groupValues?.get(1) }

    /** The names defined with this beginning, without it. */
    fun namesStartingWith(prefix: String): List<String> = defines.keys.filter { it.startsWith(prefix) }.map { it.removePrefix(prefix) }

    private fun calls(macro: String, isSystem: Boolean): List<Call> =
        declarations(macro).map { (name, result, declared, parameters) ->
            val signature = StringBuilder(if (result == "void") "v" else letter(result, name)).append('(')
            if (parameters != "void") {
                parameters.split(',').forEach { parameter ->
                    // The type is what stands before the parameter's name.
                    val type = parameter.trim().replace(Regex("""\b\w+\s*$"""), "").ifEmpty { parameter }
                    signature.append(letter(type, name))
                }
            }
            Call(name, declared, signature.append(')').toString(), isSystem)
        }

    /** `MACRO(name, result declared(parameters));` - the four, the parameters on one line. */
    private fun declarations(macro: String): List<List<String>> =
        Regex("""^$macro\(\s*(\w+)\s*,\s*([^;]*?)\b(\w+)\s*\(([^;]*?)\)\s*\)\s*;""", RegexOption.MULTILINE)
            .findAll(text).map { match ->
                val (name, result, declared, parameters) = match.destructured
                listOf(name, result.trim(), declared, parameters.split(Regex("\\s+")).filter { it.isNotEmpty() }.joinToString(" "))
            }.toList()

    private fun letter(cType: String, where: String): String {
        val type = cType.replace("const", " ").trim()
        if ('*' in type) return "*"
        val first = type.split(Regex("\\s+")).firstOrNull { it.isNotEmpty() }
            ?: throw BuildError("sdk/host_api.h: $where: a type is missing")
        return when (first) {
            in WIDE -> "I"
            in NARROW -> "i"
            else -> throw BuildError("sdk/host_api.h: $where: the type \"$type\" has no place in a call of the host")
        }
    }

    /** #define bodies made numbers: a small reader of what the headers write. */
    private class Expression(private val defines: Map<String, String>) {
        private var tokens: List<String> = emptyList()
        private var at = 0

        fun value(name: String, depth: Int = 0): Long {
            val body = defines[name] ?: throw BuildError("sdk headers: $name is not defined")
            if (depth > MAX_DEPTH) throw BuildError("sdk headers: $name is defined by itself")
            val saved = tokens to at
            tokens = Regex("""0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*|[()+\-*/]|\S""").findAll(body).map { it.value }.toList()
            at = 0
            val result = sum(depth)
            if (at != tokens.size) throw BuildError("sdk headers: $name = $body is not a number")
            tokens = saved.first
            at = saved.second
            return result
        }

        private fun sum(depth: Int): Long {
            var value = product(depth)
            while (at < tokens.size && (tokens[at] == "+" || tokens[at] == "-")) {
                val operator = tokens[at++]
                val right = product(depth)
                value = if (operator == "+") value + right else value - right
            }
            return value
        }

        private fun product(depth: Int): Long {
            var value = single(depth)
            while (at < tokens.size && (tokens[at] == "*" || tokens[at] == "/")) {
                val operator = tokens[at++]
                val right = single(depth)
                value = if (operator == "*") value * right else Math.floorDiv(value, right)
            }
            return value
        }

        private fun single(depth: Int): Long {
            if (at >= tokens.size) throw BuildError("sdk headers: an expression ends too soon")
            val token = tokens[at++]
            return when {
                token == "(" -> sum(depth).also { if (at >= tokens.size || tokens[at++] != ")") throw BuildError("sdk headers: a bracket is not closed") }
                token == "-" -> -single(depth)
                token.startsWith("0x") || token.startsWith("0X") -> token.substring(2).toLong(16)
                token[0].isDigit() -> token.toLong()
                token[0].isLetter() || token[0] == '_' -> {
                    // A number's suffix (1024u) is read with it: the number stands alone.
                    val saved = tokens to at
                    val value = value(token, depth + 1)
                    tokens = saved.first
                    at = saved.second
                    value
                }
                else -> throw BuildError("sdk headers: \"$token\" in an expression")
            }
        }
    }

    companion object {
        private val WIDE = setOf("int64_t", "uint64_t")
        private val NARROW = setOf("int32_t", "uint32_t", "int", "bool")
        private const val MIN_CALLS = 60
        private const val MIN_EXPORTS = 10
        private const val MAX_DEPTH = 8

        /** The headers of the contract in the order the umbrella includes them: host_api.h,
         * then each host/<subject>.h it names. */
        fun headers(sdk: File): List<File> {
            val umbrella = File(sdk, "host_api.h")
            if (!umbrella.isFile) throw BuildError("${sdk.path} has no host_api.h")
            val subjects = Regex("""^#include "(host/\w+\.h)"""", RegexOption.MULTILINE).findAll(umbrella.readText())
                .map { File(sdk, it.groupValues[1]) }.toList()
            subjects.firstOrNull { !it.isFile }?.let { throw BuildError("sdk/host_api.h includes ${it.path}, which is not there") }
            return listOf(umbrella) + subjects
        }

        /** The contract of an SDK folder: its headers and lib/manifest_limits.h. */
        fun of(sdk: File): Contract {
            val contract = headers(sdk)
            val headers = (contract + File(sdk, "lib/manifest_limits.h")).filter { it.isFile }
            val defines = LinkedHashMap<String, String>()
            headers.forEach { file ->
                val withoutComments = file.readText().replace(Regex("""/\*.*?\*/""", RegexOption.DOT_MATCHES_ALL), " ")
                Regex("""^#define[ \t]+(\w+)[ \t]+([^\n]+)""", RegexOption.MULTILINE).findAll(withoutComments).forEach {
                    val (name, body) = it.destructured
                    // 1024u is 1024.
                    defines.putIfAbsent(name, body.trim().replace(Regex("""(\d+)[uUlL]+\b"""), "$1"))
                }
            }
            return Contract(contract.joinToString("\n") { it.readText() }, defines)
        }
    }
}
