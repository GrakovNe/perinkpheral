// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.build

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.contract.Contract
import java.io.File

/** The module's actual wasm types, resolved through type and function indices. */
class Wasm(file: File) {
    private val data = file.readBytes()
    private var at = 0
    private var limit = data.size
    private val types = ArrayList<String>()
    private val functionTypes = ArrayList<Int>()
    private val importedTypes = ArrayList<Int>()
    private val exportedFunctions = linkedMapOf<String, Int>()
    private val exportKinds = linkedMapOf<String, Int>()

    val imports = ArrayList<Pair<String, String>>()
    val exports = ArrayList<String>()
    var memory: Pair<Long, Long?>? = null
        private set
    var codeBytes = 0L
        private set

    init {
        val header = byteArrayOf(0, 'a'.code.toByte(), 's'.code.toByte(), 'm'.code.toByte(), 1, 0, 0, 0)
        if (data.size < header.size || !data.copyOfRange(0, header.size).contentEquals(header)) {
            throw BuildError("${file.path} is not a wasm module")
        }
        at = header.size
        readSections()
    }

    /** Pointer types in C lower to i32, exactly like signed and unsigned 32-bit integers. */
    fun checkTypes(contract: Contract) {
        imports.forEachIndexed { index, (module, name) ->
            val call = contract.calls.firstOrNull { it.name == name }
            if (module != "host" || call == null) throw BuildError("the module imports $module.$name, which the host does not offer")
            checkType("import $module.$name", importedTypes[index], call.signature)
        }
        for (export in contract.exports) {
            val kind = exportKinds[export.name] ?: continue
            if (kind != FUNCTION) throw BuildError("export ${export.name} is not a function")
            val index = exportedFunctions.getValue(export.name)
            val type = functionTypes.getOrNull(index) ?: throw BuildError("export ${export.name}: invalid function index")
            checkType("export ${export.name}", type, export.signature)
        }
    }

    private fun checkType(where: String, type: Int, expected: String) {
        val actual = types.getOrNull(type) ?: throw BuildError("$where: invalid type index")
        val lowered = expected.replace('*', 'i')
        if (actual != lowered) throw BuildError("$where has wasm type $actual, contract requires $lowered")
    }

    private fun readSections() {
        while (at < data.size) {
            limit = data.size
            val section = byte()
            val size = unsigned()
            if (size > (data.size - at).toLong()) throw BuildError("wasm section ends outside the module")
            val end = at + size.toInt()
            limit = end
            when (section) {
                1 -> repeat(count()) {
                    if (byte() != 0x60) throw BuildError("wasm type is not a function")
                    val arguments = (0 until count()).joinToString("") { valueType() }
                    val results = (0 until count()).joinToString("") { valueType() }
                    types += (results.ifEmpty { "v" } + "(" + arguments + ")")
                }
                2 -> repeat(count()) {
                    val module = name()
                    val name = name()
                    if (byte() != FUNCTION) throw BuildError("import $module.$name is not a function")
                    val type = count()
                    imports += module to name
                    importedTypes += type
                    functionTypes += type
                }
                3 -> repeat(count()) { functionTypes += count() }
                5 -> {
                    if (count() != 1) throw BuildError("the module must have one memory")
                    val flags = byte()
                    if (flags !in 0..1) throw BuildError("unsupported wasm memory")
                    val initial = unsigned()
                    memory = initial to if (flags == 1) unsigned() else null
                }
                7 -> repeat(count()) {
                    val name = name()
                    val kind = byte()
                    val index = count()
                    if (exportKinds.put(name, kind) != null) throw BuildError("duplicate export $name")
                    if (kind == FUNCTION) {
                        exports += name
                        exportedFunctions[name] = index
                    }
                }
                10 -> codeBytes = size
            }
            at = end
        }
    }

    private fun valueType(): String = when (val value = byte()) {
        0x7F -> "i"
        0x7E -> "I"
        0x7D -> "f"
        0x7C -> "F"
        else -> throw BuildError("unsupported wasm value type $value")
    }

    private fun byte(): Int {
        if (at >= limit) throw BuildError("wasm section is incomplete")
        return data[at++].toInt() and 0xFF
    }

    private fun count(): Int = unsigned().let {
        if (it > Int.MAX_VALUE) throw BuildError("wasm index or count is too large")
        it.toInt()
    }

    private fun unsigned(): Long {
        var value = 0L
        for (shift in 0..28 step 7) {
            val next = byte()
            if (shift == 28 && next > 15) throw BuildError("wasm integer is too large")
            value = value or ((next and 0x7F).toLong() shl shift)
            if (next and 0x80 == 0) return value
        }
        throw BuildError("wasm integer is incomplete")
    }

    private fun name(): String {
        val length = count()
        if (length > limit - at) throw BuildError("wasm name is incomplete")
        return String(data, at, length, Charsets.UTF_8).also { at += length }
    }

    companion object {
        private const val FUNCTION = 0
    }
}
