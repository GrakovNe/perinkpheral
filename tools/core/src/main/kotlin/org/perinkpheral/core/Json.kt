// SPDX-License-Identifier: MIT
package org.perinkpheral.core

/**
 * JSON as the build reads it: objects keep the order of their keys, a key written twice
 * is an error (the device reads the first, a parser that keeps the last would hide it),
 * a number is a Long when it is whole as written and a Double otherwise, true and false
 * are Booleans, null is null.
 */
object Json {
    class Error(message: String) : Exception(message)

    fun parse(text: String): Any? {
        val parser = Parser(text)
        val value = parser.value()
        parser.space()
        if (parser.at != text.length) parser.fail("text after the end")
        return value
    }

    /** Compact JSON, non-ASCII kept as it is: what Python's json.dumps(ensure_ascii=False,
     * separators=(",", ":")) writes. */
    fun write(value: Any?): String = buildString { write(this, value, false) }

    /** JSON indented by two spaces, as Python's json.dump(indent=2) writes it; with
     * `ensureAscii`, what is not ASCII as \u escapes, as it does by default. */
    fun pretty(value: Any?, ensureAscii: Boolean = false): String = buildString { pretty(this, value, "", ensureAscii) }

    private fun pretty(out: StringBuilder, value: Any?, indent: String, ascii: Boolean) {
        val inner = "$indent  "
        when {
            value is Map<*, *> && value.isNotEmpty() -> {
                out.append("{\n")
                value.entries.forEachIndexed { index, (key, item) ->
                    out.append(inner)
                    write(out, key as String, ascii)
                    out.append(": ")
                    pretty(out, item, inner, ascii)
                    out.append(if (index < value.size - 1) ",\n" else "\n")
                }
                out.append(indent).append('}')
            }
            value is List<*> && value.isNotEmpty() -> {
                out.append("[\n")
                value.forEachIndexed { index, item ->
                    out.append(inner)
                    pretty(out, item, inner, ascii)
                    out.append(if (index < value.size - 1) ",\n" else "\n")
                }
                out.append(indent).append(']')
            }
            else -> write(out, value, ascii)
        }
    }

    private fun write(out: StringBuilder, value: Any?, ascii: Boolean) {
        when (value) {
            null -> out.append("null")
            is Boolean -> out.append(value)
            is Long, is Int -> out.append(value)
            is Double -> out.append(value)
            is String -> {
                out.append('"')
                for (character in value) {
                    when (character) {
                        '"' -> out.append("\\\"")
                        '\\' -> out.append("\\\\")
                        '\n' -> out.append("\\n")
                        '\r' -> out.append("\\r")
                        '\t' -> out.append("\\t")
                        '\b' -> out.append("\\b")
                        '\u000C' -> out.append("\\f")
                        else -> if (character < ' ' || (ascii && character.code > 0x7E)) {
                            out.append("\\u%04x".format(character.code))
                        } else {
                            out.append(character)
                        }
                    }
                }
                out.append('"')
            }
            is Map<*, *> -> {
                out.append('{')
                var first = true
                for ((key, item) in value) {
                    if (!first) out.append(',')
                    first = false
                    write(out, key as String, ascii)
                    out.append(':')
                    write(out, item, ascii)
                }
                out.append('}')
            }
            is List<*> -> {
                out.append('[')
                value.forEachIndexed { index, item ->
                    if (index > 0) out.append(',')
                    write(out, item, ascii)
                }
                out.append(']')
            }
            else -> throw IllegalArgumentException("not JSON: ${value.javaClass}")
        }
    }

    private class Parser(val text: String) {
        var at = 0

        fun fail(why: String): Nothing {
            val line = text.substring(0, minOf(at, text.length)).count { it == '\n' } + 1
            throw Error("$why (line $line)")
        }

        fun space() {
            while (at < text.length && text[at] in " \t\r\n") at++
        }

        fun value(): Any? {
            space()
            if (at >= text.length) fail("a value is missing")
            return when (text[at]) {
                '{' -> obj()
                '[' -> array()
                '"' -> string()
                't' -> word("true", true)
                'f' -> word("false", false)
                'n' -> word("null", null)
                else -> number()
            }
        }

        fun word(word: String, value: Any?): Any? {
            if (!text.startsWith(word, at)) fail("not JSON")
            at += word.length
            return value
        }

        fun obj(): Map<String, Any?> {
            val map = LinkedHashMap<String, Any?>()
            at++
            space()
            if (at < text.length && text[at] == '}') {
                at++
                return map
            }
            while (true) {
                space()
                if (at >= text.length || text[at] != '"') fail("a key is missing")
                val key = string()
                space()
                if (at >= text.length || text[at] != ':') fail("':' is missing")
                at++
                if (map.containsKey(key)) throw Error("\"$key\" is there more than once")
                map[key] = value()
                space()
                if (at >= text.length) fail("'}' is missing")
                when (text[at++]) {
                    ',' -> continue
                    '}' -> return map
                    else -> fail("',' or '}' is missing")
                }
            }
        }

        fun array(): List<Any?> {
            val list = ArrayList<Any?>()
            at++
            space()
            if (at < text.length && text[at] == ']') {
                at++
                return list
            }
            while (true) {
                list += value()
                space()
                if (at >= text.length) fail("']' is missing")
                when (text[at++]) {
                    ',' -> continue
                    ']' -> return list
                    else -> fail("',' or ']' is missing")
                }
            }
        }

        fun string(): String {
            val out = StringBuilder()
            at++
            while (true) {
                if (at >= text.length) fail("a string is not closed")
                val character = text[at++]
                when {
                    character == '"' -> return out.toString()
                    character == '\\' -> {
                        if (at >= text.length) fail("a string is not closed")
                        when (val escaped = text[at++]) {
                            '"', '\\', '/' -> out.append(escaped)
                            'n' -> out.append('\n')
                            'r' -> out.append('\r')
                            't' -> out.append('\t')
                            'b' -> out.append('\b')
                            'f' -> out.append('\u000C')
                            'u' -> {
                                if (at + 4 > text.length) fail("a \\u escape is cut short")
                                val code = text.substring(at, at + 4).toIntOrNull(16) ?: fail("a \\u escape is not hex")
                                out.append(code.toChar())
                                at += 4
                            }
                            else -> fail("an unknown escape \\$escaped")
                        }
                    }
                    character < ' ' -> fail("a control character in a string")
                    else -> out.append(character)
                }
            }
        }

        fun number(): Any {
            val start = at
            if (at < text.length && text[at] == '-') at++
            while (at < text.length && (text[at].isDigit() || text[at] in ".eE+-")) at++
            val written = text.substring(start, at)
            if (!Regex("-?(0|[1-9]\\d*)(\\.\\d+)?([eE][+-]?\\d+)?").matches(written)) {
                at = start
                fail("not JSON")
            }
            val isWhole = written.none { it in ".eE" }
            return if (isWhole) written.toLongOrNull() ?: written.toDouble() else written.toDouble()
        }
    }
}
