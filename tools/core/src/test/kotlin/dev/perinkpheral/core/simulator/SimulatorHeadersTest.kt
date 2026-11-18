// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.simulator

import com.sun.net.httpserver.HttpServer
import dev.perinkpheral.core.Json
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket

object SimulatorHeadersTest {
    @JvmStatic fun main(args: Array<String>) {
        val server = HttpServer.create(InetSocketAddress(InetAddress.getLoopbackAddress(), 0), 0)
        server.createContext("/auth") { exchange ->
            exchange.responseHeaders.add("X-NDM-Realm", "router")
            exchange.responseHeaders.add("X-NDM-Challenge", "challenge")
            exchange.responseHeaders.add("Set-Cookie", "sid=one; Path=/")
            exchange.responseHeaders.add("Set-Cookie", "other=two; Expires=Wed, 21 Oct 2030 07:28:00 GMT")
            exchange.sendResponseHeaders(401, -1)
            exchange.close()
        }
        server.createContext("/overflow") { exchange ->
            exchange.responseHeaders.add("A-Large", "x".repeat(1200))
            exchange.responseHeaders.add("ETag", "still-metadata")
            exchange.sendResponseHeaders(200, -1)
            exchange.close()
        }
        server.createContext("/redirect") { exchange ->
            exchange.responseHeaders.add("Location", "/auth")
            exchange.responseHeaders.add("Set-Cookie", "redirect=separate")
            exchange.sendResponseHeaders(302, -1)
            exchange.close()
        }
        server.start()
        try {
            Socket(InetAddress.getLoopbackAddress(), SimulatorNetwork.port()).use { socket ->
                val reader = socket.getInputStream().bufferedReader()
                val writer = socket.getOutputStream().bufferedWriter()
                fun get(path: String): Map<*, *> {
                    val request = mapOf("method" to "GET", "url" to "http://127.0.0.1:${server.address.port}$path",
                        "headers" to emptyMap<String, String>(), "body" to "", "max" to 1024, "timeout" to 3)
                    writer.write(Json.write(request) + "\n")
                    writer.flush()
                    return Json.parse(reader.readLine()) as Map<*, *>
                }
                val challenge = get("/auth")
                check(challenge["status"] == 401L)
                val headers = challenge["headers"] as Map<*, *>
                check(headers["x-ndm-realm"] == listOf("router"))
                check(headers["x-ndm-challenge"] == listOf("challenge"))
                check(headers["set-cookie"] == listOf("sid=one; Path=/",
                    "other=two; Expires=Wed, 21 Oct 2030 07:28:00 GMT"))
                check(challenge["headers_overflow"] == false)
                val overflow = get("/overflow")
                check(overflow["headers_overflow"] == true)
                check((overflow["metadata"] as Map<*, *>)["etag"] == "still-metadata")
                val redirected = get("/redirect")
                check(redirected["status"] == 302L)
                check((redirected["headers"] as Map<*, *>)["set-cookie"] == listOf("redirect=separate"))
                val again = get("/auth")["headers"] as Map<*, *>
                check(again["set-cookie"] == headers["set-cookie"])
                check(again["x-ndm-realm"] == listOf("router"))
            }
        } finally {
            server.stop(0)
        }
        println("Simulator response headers: challenge, separate cookies, bounded capture, metadata and exchanges passed.")
    }
}
