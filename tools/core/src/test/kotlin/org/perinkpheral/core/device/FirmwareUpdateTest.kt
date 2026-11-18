// SPDX-License-Identifier: MIT
package org.perinkpheral.core.device

import com.sun.net.httpserver.HttpServer
import org.perinkpheral.core.Json
import java.io.IOException
import java.net.InetSocketAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicInteger

/** Protocol failure cases plus the real HTTP client against a local fake device. */
object FirmwareUpdateTest {
    private const val LAYOUT = 0xf1234567L
    private val image = ByteArray(512).also { bytes ->
        val header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        bytes[0] = 0xe9.toByte()
        header.putInt(32, 0xabcd5432.toInt())
        "0.1.34".toByteArray().copyInto(bytes, 48)
        "perinkpheral".toByteArray().copyInto(bytes, 80)
        header.putInt(288, 0x54524150)
        header.putInt(292, LAYOUT.toInt())
        repeat(32) { bytes[176 + it] = it.toByte() }
    }
    private val expectedId = FirmwareImage(image).id
    private fun main(probation: Boolean = false, id: String = expectedId) = mapOf(
        "mode" to "application", "ota_protocol" to 1L, "partition_table" to LAYOUT,
        "firmware" to "0.1.34", "image_id" to id, "on_probation" to probation,
    )
    private fun recovery(boot: Long = 7L, state: String = "idle") = mapOf(
        "mode" to "recovery", "ota_protocol" to 1L, "partition_table" to LAYOUT,
        "boot_id" to boot, "upload_state" to state,
    )
    private fun fails(message: String, operation: () -> Unit) {
        try {
            operation()
            error("expected failure containing $message")
        } catch (error: IOException) {
            check(error.message.orEmpty().contains(message)) { error.toString() }
        }
    }

    private fun protocolCases() {
        var requests = mutableListOf<String>()
        fun flow(statuses: List<Map<*, *>>, failPost: Boolean = false,
                 rejectPost: Boolean = false, before: Map<*, *> = main()): String {
            var time = 0L
            var next = 0
            requests = mutableListOf()
            return FirmwareUpdate({ statuses[minOf(next++, statuses.lastIndex)] }, { path, _, _ ->
                requests += path
                if (rejectPost) throw FirmwareUpdate.Rejected("not confirmed")
                if (failPost) throw IOException("lost response")
            }, { time += 1_000 }, { time }).install(image, before)
        }
        check(flow(listOf(recovery(), recovery(state = "restarting"), main(true), main()), failPost = true)
            .contains("confirmed"))
        check(requests == listOf("/api/ota/recovery", "/api/ota")) { "POST was retried" }
        fails("returned to recovery") { flow(listOf(recovery(), recovery(8))) }
        fails("stopped in recovery") { flow(listOf(recovery(), recovery(state = "failed")), failPost = true) }
        fails("different firmware") { flow(listOf(recovery(), main(id = "00".repeat(32)))) }
        fails("not confirmed") { flow(listOf(recovery()), rejectPost = true) }
        check(requests == listOf("/api/ota/recovery"))
        fails("another partition") { flow(listOf(recovery()), before = main() + ("partition_table" to 0L)) }
        check(requests.isEmpty()) { "handoff preceded header validation" }
        fails("unsupported") { flow(listOf(recovery()), before = main() + ("ota_protocol" to 2L)) }
        check(requests.isEmpty())
        check(flow(listOf(recovery(), main()), before = recovery()).contains("confirmed"))
        check(requests == listOf("/api/ota"))
        fails("not confirmed") { flow(listOf(recovery(), main(true))) }
        val invalidName = image.copyOf().also { it[80] = 'x'.code.toByte() }
        fails("invalid firmware") { FirmwareImage(invalidName) }
        val unterminated = image.copyOf().also { bytes -> repeat(32) { bytes[48 + it] = 'v'.code.toByte() } }
        fails("unterminated") { FirmwareImage(unterminated) }
    }

    private fun httpCase(dropReply: Boolean) {
        val statusCount = AtomicInteger()
        val uploads = AtomicInteger()
        val handoffs = AtomicInteger()
        val statuses = listOf(main(), recovery(), recovery(state = "restarting"), main(true), main())
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/api/status") { exchange ->
            val status = statuses[minOf(statusCount.getAndIncrement(), statuses.lastIndex)]
            val response = Json.write(status).toByteArray()
            exchange.sendResponseHeaders(200, response.size.toLong())
            exchange.responseBody.use { it.write(response) }
        }
        server.createContext("/api/ota/recovery") { exchange ->
            handoffs.incrementAndGet()
            exchange.requestBody.readBytes()
            if (!dropReply) exchange.sendResponseHeaders(200, -1)
            exchange.close()
        }
        server.createContext("/api/ota") { exchange ->
            uploads.incrementAndGet()
            check(exchange.requestBody.readBytes().contentEquals(image))
            if (!dropReply) exchange.sendResponseHeaders(200, -1)
            exchange.close()
        }
        server.start()
        try {
            val result = DeviceClient("127.0.0.1:${server.address.port}").flash(image)
            check(result.contains("Installed and confirmed"))
            check(uploads.get() == 1 && handoffs.get() == 1) { "HTTP mutation was resent" }
        } finally {
            server.stop(0)
        }
    }

    @JvmStatic fun main(arguments: Array<String>) {
        protocolCases()
        httpCase(dropReply = false)
        httpCase(dropReply = true)
        println("Firmware update checks passed: header validation, same-version identity, probation, rollback, lost responses and real HTTP.")
    }
}
