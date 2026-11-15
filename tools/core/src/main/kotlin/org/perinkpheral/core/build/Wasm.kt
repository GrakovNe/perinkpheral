// SPDX-License-Identifier: MIT
package org.perinkpheral.core.build

import org.perinkpheral.core.BuildError
import java.io.File

/** Just enough of the wasm binary format to list imports, exports, memory and code size. */
class Wasm(file: File) {
    private val data = file.readBytes()
    private var at = 0

    /** (module, name) of every import. */
    val imports = ArrayList<Pair<String, String>>()

    /** The names of the functions the module exports. */
    val exports = ArrayList<String>()

    /** Pages of the one memory: initial and maximum (null when none). */
    var memory: Pair<Long, Long?>? = null
        private set

    /** Bytes of the code section. */
    var codeBytes = 0L
        private set

    init {
        if (data.size < 8 || data[0].toInt() != 0 || data[1] != 'a'.code.toByte() ||
            data[2] != 's'.code.toByte() || data[3] != 'm'.code.toByte()) {
            throw BuildError("${file.path} is not a wasm module")
        }
        at = 8
        try {
            readSections()
        } catch (error: IndexOutOfBoundsException) {
            throw BuildError("${file.path} is not a whole wasm module")
        }
    }

    private fun readSections() {
        while (at < data.size) {
            val section = byte()
            val size = unsigned()
            val end = at + size.toInt()
            when (section) {
                SECTION_IMPORT -> repeat(unsigned().toInt()) {
                    val module = name()
                    val name = name()
                    if (byte() != IMPORT_KIND_FUNCTION) throw BuildError("import $module.$name is not a function")
                    unsigned()
                    imports += module to name
                }
                SECTION_MEMORY -> {
                    unsigned() // always one memory
                    val hasMaximum = byte() and 1
                    val initial = unsigned()
                    memory = initial to if (hasMaximum != 0) unsigned() else null
                }
                SECTION_EXPORT -> repeat(unsigned().toInt()) {
                    val name = name()
                    val kind = byte()
                    unsigned()
                    if (kind == EXPORT_KIND_FUNCTION) exports += name
                }
                SECTION_CODE -> codeBytes = size
            }
            at = end
        }
    }

    private fun byte(): Int = data[at++].toInt() and 0xFF

    private fun unsigned(): Long {
        var value = 0L
        var shift = 0
        while (true) {
            val byte = byte()
            value = value or ((byte and 0x7F).toLong() shl shift)
            shift += 7
            if (byte and 0x80 == 0) return value
        }
    }

    private fun name(): String {
        val length = unsigned().toInt()
        val text = String(data, at, length, Charsets.UTF_8)
        at += length
        return text
    }

    companion object {
        private const val SECTION_IMPORT = 2
        private const val SECTION_MEMORY = 5
        private const val SECTION_EXPORT = 7
        private const val SECTION_CODE = 10
        private const val IMPORT_KIND_FUNCTION = 0
        private const val EXPORT_KIND_FUNCTION = 0
    }
}
