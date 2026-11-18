// SPDX-License-Identifier: MIT
package org.perinkpheral.core.device

import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets

/** The header is checked before a restart; recovery repeats the check before erasing main. */
internal class FirmwareImage(bytes: ByteArray) {
    val version: String
    val layout: Long
    val id: String

    init {
        if (bytes.size !in HEADER_BYTES..MAX_IMAGE_BYTES) throw IOException("invalid firmware size")
        fun field(offset: Int): String {
            val end = (offset until offset + FIELD_BYTES).firstOrNull { bytes[it] == 0.toByte() }
                ?: throw IOException("unterminated firmware header field")
            return String(bytes, offset, end - offset, StandardCharsets.US_ASCII)
        }
        val header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if ((bytes[0].toInt() and 255) != IMAGE_MAGIC || header.getInt(32) != DESCRIPTION_MAGIC ||
            field(80) != "perinkpheral" || header.getInt(288) != LAYOUT_MAGIC) {
            throw IOException("invalid firmware image")
        }
        version = field(48)
        if (version.isEmpty()) throw IOException("firmware version is missing")
        layout = header.getInt(292).toLong() and 0xffffffffL
        id = bytes.copyOfRange(176, 208).joinToString("") { "%02x".format(it.toInt() and 255) }
    }

    private companion object {
        const val HEADER_BYTES = 304
        const val MAX_IMAGE_BYTES = 2 * 1024 * 1024
        const val FIELD_BYTES = 32
        const val IMAGE_MAGIC = 0xe9
        const val DESCRIPTION_MAGIC = 0xabcd5432.toInt()
        const val LAYOUT_MAGIC = 0x54524150
    }
}

/** Status-driven update protocol. A lost POST response is never retried: its effect is read back. */
internal class FirmwareUpdate(
    private val readStatus: () -> Map<*, *>,
    private val post: (String, ByteArray?, Int) -> Unit,
    private val pause: () -> Unit,
    private val now: () -> Long = System::currentTimeMillis,
) {
    class Rejected(message: String) : IOException(message)

    fun install(bytes: ByteArray, before: Map<*, *>): String {
        val image = FirmwareImage(bytes)
        checkProtocol(before)
        checkLayout(before, image)
        if (before["mode"] != "recovery") postOnce("/api/ota/recovery", null, 15_000)
        val recovery = awaitRecovery()
        checkLayout(recovery, image)
        val recoveryBoot = bootId(recovery)
        postOnce("/api/ota", bytes, 300_000)
        val deadline = now() + CONFIRM_WAIT_MS
        while (now() < deadline) {
            pause()
            val status = try { readStatus() } catch (_: IOException) { continue }
            when (status["mode"]) {
                "recovery" -> {
                    checkProtocol(status)
                    if (bootId(status) != recoveryBoot) {
                        throw IOException("firmware returned to recovery; upload another image")
                    }
                    if (status["upload_state"] in setOf("failed", "idle")) {
                        throw IOException("upload stopped in recovery; choose a file and confirm again")
                    }
                    // The same recovery still owns the HTTP server while the panel is drawn.
                }
                "application" -> if (status["on_probation"] == false) {
                    if (status["firmware"] != image.version || status["image_id"] != image.id) {
                        throw IOException("a different firmware started; the uploaded image was not confirmed")
                    }
                    return "Installed and confirmed: ${image.version}"
                }
            }
        }
        throw IOException("firmware was not confirmed; check the device screen and network address")
    }

    private fun awaitRecovery(): Map<*, *> {
        val deadline = now() + RECOVERY_WAIT_MS
        while (now() < deadline) {
            pause()
            val status = try { readStatus() } catch (_: IOException) { continue }
            if (status["mode"] == "recovery") {
                checkProtocol(status)
                return status
            }
        }
        throw IOException("recovery not found; check the device screen for its address")
    }

    private fun postOnce(path: String, bytes: ByteArray?, timeout: Int) {
        try {
            post(path, bytes, timeout)
        } catch (error: Rejected) {
            throw error
        } catch (_: IOException) {
            // The server may have completed the operation before the connection disappeared.
        }
    }

    private fun checkProtocol(status: Map<*, *>) {
        if ((status["ota_protocol"] as? Number)?.toDouble() != 1.0) {
            throw IOException("unsupported recovery protocol; update the tools")
        }
    }

    private fun checkLayout(status: Map<*, *>, image: FirmwareImage) {
        if ((status["partition_table"] as? Number)?.toLong() != image.layout) {
            throw IOException("image is for another partition table; flash over USB")
        }
    }

    private fun bootId(status: Map<*, *>): Long = (status["boot_id"] as? Number)?.toLong()
        ?: throw IOException("recovery did not report its boot identity")

    private companion object {
        const val RECOVERY_WAIT_MS = 90_000L
        const val CONFIRM_WAIT_MS = 240_000L
    }
}
