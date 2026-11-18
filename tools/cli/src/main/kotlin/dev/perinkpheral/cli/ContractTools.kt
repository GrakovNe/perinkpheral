// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import dev.perinkpheral.core.contract.Contract
import java.io.File

/** What is made of the contract for the firmware, its tests and its guide. */
object ContractTools {

    private const val MADE = "/* Made by tools/perinkpheral from sdk/host_api.h - do not edit. */\n"

    /**
     * The host's table of imports: host_imports.inc and system_imports.inc, a row
     * HOST_IMPORT_ROW(name, "signature") for every call, which firmware/components/appsys
     * makes into the table it links into a module and the declarations of the functions that
     * answer (app_api_<name>); and host_exports.inc, a row APP_EXPORT_ROW(name, arguments,
     * has_result) for every function an application may export.
     */
    fun imports(contract: Contract, out: File) {
        out.mkdirs()
        for ((isSystem, name) in listOf(false to "host_imports.inc", true to "system_imports.inc")) {
            File(out, name).writeText(MADE + contract.calls.filter { it.isSystem == isSystem }
                .joinToString("") { "HOST_IMPORT_ROW(${it.name}, \"${it.signature}\")\n" })
        }
        File(out, "host_exports.inc").writeText(MADE + contract.exports
            .joinToString("") { "APP_EXPORT_ROW(${it.name}, ${it.arguments}, ${if (it.hasResult) 1 else 0})\n" })
    }

    /**
     * An application that imports every call of the contract. It calls none of them: it takes
     * the address of each, which is enough for the module to import it with the signature the
     * header gives. Loading it is then the check that the host's table has every call, each
     * with that signature - for the calls no other test happens to make.
     */
    fun everyCall(contract: Contract, out: File) {
        out.mkdirs()
        val calls = contract.calls.map { it.declared }
        File(out, "every_call.c").writeText(buildString {
            append("/* Made by tools/perinkpheral every-call from sdk/host_api.h. */\n")
            append("#include \"host_api.h\"\n\n")
            append("typedef void (*call_t)(void);\n")
            append("static const call_t CALLS[] = {\n")
            calls.forEach { append("    (call_t)$it,\n") }
            append("};\n\nstatic volatile call_t taken;\n\n")
            append("void app_on_timer(int32_t id)\n{\n")
            append("    taken = CALLS[(uint32_t)id % ${calls.size}];\n}\n")
            // The handlers the build asks of a module that imports the calls they answer.
            append("\nvoid app_on_alarm(int32_t id)\n{\n    (void)id;\n}\n")
            append("\nvoid app_on_draw(void)\n{\n}\n")
            append("\nvoid app_on_data(int32_t request, const uint8_t *data, int32_t length, " +
                "int32_t flags, int32_t http_status)\n{\n    (void)request;\n    (void)data;\n" +
                "    (void)length;\n    (void)flags;\n    (void)http_status;\n}\n")
        })
        File(out, "manifest.json").writeText(Json.write(linkedMapOf(
            "name" to out.name, "title" to "Every call", "abi" to contract.number("HOST_API_VERSION"), "system" to true, "writes_files" to true)))
        File(out, "count.h").writeText("#define EVERY_CALL_COUNT ${calls.size}\n")
    }

    /**
     * The guide's table of limits against the headers it quotes: every row that names
     * constants - HOST_KV_KEY_MAX / _VALUE_MAX - and gives their values - 15 / 1024 - is
     * compared with sdk/host_api.h and sdk/lib/manifest_limits.h. Returns how many agree.
     */
    fun checkLimits(contract: Contract, guide: File): Int {
        var html = guide.readText()
        // The table of limits alone: other tables name constants beside numbers that are not
        // their values - a font and the height of its capitals.
        val start = html.indexOf("id=\"ref-limits\"")
        if (start < 0) throw BuildError("${guide.name}: the section of limits (id=\"ref-limits\") is not found")
        val end = html.indexOf("<h1", start + 1)
        html = html.substring(start, if (end > 0) end else html.length)
        val name = """(?:[A-Z][A-Z0-9_]*|_[A-Z0-9_]+)"""
        val rows = Regex("""<tr><td class="c">($name(?: / $name)*)</td><td>([^<]*)</td>""").findAll(html)
        val problems = ArrayList<String>()
        var checked = 0
        for (row in rows) {
            val (cell, values) = row.destructured
            val numbers = Regex("""(\d+)(?:\s*(KB|MB))?""").findAll(values).map { it.groupValues[1] to it.groupValues[2] }.toList()
            val names = namesOf(cell)
            if (numbers.size < names.size) {
                problems += "$cell: ${names.size} names and ${numbers.size} values"
                continue
            }
            // One unit at the end stands for the whole row: "511 / 255 bytes", "23 / 64 KB".
            names.forEachIndexed { index, options ->
                val known = options.firstOrNull { contract.written(it) != null }
                if (known == null) {
                    problems += "${options.first()}: no such constant in the headers"
                    return@forEachIndexed
                }
                val (number, unit) = numbers[index]
                val said = number.toLong() * (UNITS[unit] ?: 1)
                val actual = contract.number(known)
                checked++
                if (said != actual) problems += "$known: the guide says $number${if (unit.isNotEmpty()) " $unit" else ""}, the header has $actual"
            }
        }
        if (checked < MIN_LIMITS) problems += "only $checked limits found in the guide - its table is read wrong"
        if (problems.isNotEmpty()) throw BuildError("${guide.name} disagrees with the headers:\n  " + problems.joinToString("\n  "))
        return checked
    }

    private val UNITS = mapOf("KB" to 1024L, "MB" to 1024L * 1024)
    private const val MIN_LIMITS = 20

    /** HOST_KV_KEY_MAX / _VALUE_MAX / HOST_FILES_MAX: a short one continues the first, after
     * any of its heads. Each name is the names it may be, the likeliest first. */
    private fun namesOf(cell: String): List<List<String>> {
        val names = ArrayList<List<String>>()
        for (part in cell.split('/').map { it.trim() }) {
            if (part.startsWith("_") && names.isNotEmpty()) {
                val candidates = ArrayList<String>()
                var head = names[0][0]
                while ('_' in head) {
                    head = head.substring(0, head.lastIndexOf('_'))
                    candidates += head + part
                }
                names += candidates
            } else {
                names += listOf(part)
            }
        }
        return names
    }
}
