// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import com.sun.net.httpserver.HttpServer
import dev.perinkpheral.core.Json
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.atomic.AtomicInteger

object EnduranceTest {
    @JvmStatic fun main(args: Array<String>) {
        val image = "a".repeat(64)
        val baseline = mapOf("image_id" to image, "mode" to "application", "on_probation" to false,
            "uptime_s" to 50L, "storage_ok" to true, "failed_allocations" to 0L, "memory_refusals" to 0L)
        check(Endurance.statusEvents(baseline, baseline + ("uptime_s" to 51L), image).isEmpty())
        check("uptime_decreased" in Endurance.statusEvents(baseline, baseline + ("uptime_s" to 1L), image))
        check("failed_allocations" in Endurance.statusEvents(baseline, baseline + ("failed_allocations" to 1L), image))
        check("memory_refusals" in Endurance.statusEvents(baseline, baseline + ("memory_refusals" to 2L), image))
        check("image_changed" in Endurance.statusEvents(baseline, baseline + ("image_id" to "b".repeat(64)), image))
        check("panic_record" in Endurance.statusEvents(baseline, baseline + ("last_panic" to "watchdog"), image))
        check("storage_unavailable" in Endurance.statusEvents(baseline, baseline + ("storage_ok" to false), image))
        val cursor = Endurance.LogCursor("one", 100)
        check(Endurance.missingBytes(cursor, Endurance.LogCursor("one", 5100), 4096) == 904L)
        check(Endurance.missingBytes(cursor, Endurance.LogCursor("two", 5100), 4096) == 0L)
        check(Endurance.missingBytes(Endurance.LogCursor("one", 0xffff_ffffL - 2),
            Endurance.LogCursor("one", 3), 6) == 0L)

        val directory = Files.createTempDirectory("perinkpheral-soak-test").toFile()
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val calls = AtomicInteger()
        val writes = AtomicInteger()
        server.createContext("/") { exchange ->
            if (exchange.requestMethod != "GET") writes.incrementAndGet()
            val log = exchange.requestURI.path == "/api/log"
            val call = if (log) calls.incrementAndGet() else 0
            val body = if (log) "new log\n" else Json.write(baseline)
            if (log) {
                exchange.responseHeaders.set("X-Log-Boot", if (call <= 1) "old" else "new")
                exchange.responseHeaders.set("X-Log-End", if (call <= 1) "100" else "200")
            }
            val bytes = body.toByteArray()
            exchange.sendResponseHeaders(200, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.start()
        try {
            Endurance(DeviceClient("127.0.0.1:${server.address.port}"), directory, image).run(0.0012)
            check(writes.get() == 0) { "observer modified the device" }
            val events = directory.resolve("events.jsonl").readText()
            check("device_restarted" in events) { "restart with a larger log cursor was missed" }
            val progress = Json.parse(directory.resolve("progress.json").readText()) as Map<*, *>
            check(progress["state"] == "awaiting_review") { "observation was called a pass" }
            check((progress["samples"] as Number).toLong() > 0)
            val original = directory.resolve("run.json").readText()
            Endurance(DeviceClient("127.0.0.1:${server.address.port}"), directory, image).run(72.0)
            check(directory.resolve("run.json").readText() == original) { "resume extended the run" }
        } finally {
            server.stop(0)
            directory.deleteRecursively()
        }
        println("Endurance observer checks passed")
    }
}
