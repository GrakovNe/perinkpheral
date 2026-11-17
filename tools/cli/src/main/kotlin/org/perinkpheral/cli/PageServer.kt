// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import org.perinkpheral.core.device.DeviceClient
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.PrintStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.URLDecoder
import java.util.concurrent.Executors

/**
 * The device in a browser tab beside any editor: the live screen, the four buttons, the log
 * and, given an application, a Run button. The device takes requests from its own page only,
 * so a page of ours cannot ask it directly: this server gives the page and passes on the
 * requests of the device's API it makes - to its own page only, as the device does: a page of another site in the same
 * browser, or a name that was made to point here, is refused.
 */
class PageServer(private val client: DeviceClient, private val appDir: File?, private val run: (File, PrintStream) -> Boolean) {
    private var port = 0

    fun serve(port: Int) {
        this.port = port
        val server = HttpServer.create(InetSocketAddress(InetAddress.getLoopbackAddress(), port), 0)
        server.executor = Executors.newCachedThreadPool()
        server.createContext("/") { exchange ->
            try {
                handle(exchange)
            } catch (error: Exception) {
                answer(exchange, 502, "no answer from the device: ${error.message}".toByteArray())
            } finally {
                exchange.close()
            }
        }
        server.start()
        println("http://127.0.0.1:$port - the device at ${client.host}" +
            (appDir?.let { ", running ${it.path}" } ?: "") + "; Ctrl-C stops it")
    }

    private fun answer(exchange: HttpExchange, status: Int, body: ByteArray,
                       type: String = "text/plain; charset=utf-8", headers: Map<String, String> = emptyMap()) {
        exchange.responseHeaders.add("Content-Type", type)
        exchange.responseHeaders.add("Cache-Control", "no-store")
        headers.forEach { (name, value) -> exchange.responseHeaders.add(name, value) }
        exchange.sendResponseHeaders(status, if (body.isEmpty()) -1 else body.size.toLong())
        if (body.isNotEmpty()) exchange.responseBody.use { it.write(body) }
    }

    private fun passOn(exchange: HttpExchange, path: String, method: String = "GET", expose: List<String> = emptyList()) {
        val got = client.raw(path, method)
        answer(exchange, got.status, got.body, got.headers["Content-Type"] ?: "application/octet-stream",
            expose.mapNotNull { name -> got.headers[name]?.let { name to it } }.toMap())
    }

    /** Asked by this server's own page: by its own address, and from no other site. */
    private fun isOwn(exchange: HttpExchange): Boolean {
        val own = listOf("127.0.0.1:$port", "localhost:$port")
        val host = exchange.requestHeaders.getFirst("Host")
        val origin = exchange.requestHeaders.getFirst("Origin")
        return host in own && (origin == null || origin in own.map { "http://$it" })
    }

    private fun handle(exchange: HttpExchange) {
        if (!isOwn(exchange)) {
            answer(exchange, 403, "Forbidden.".toByteArray())
            return
        }
        val path = exchange.requestURI.path
        val query = exchange.requestURI.rawQuery.orEmpty().split('&').filter { it.contains('=') }
            .associate { URLDecoder.decode(it.substringBefore('='), Charsets.UTF_8) to URLDecoder.decode(it.substringAfter('='), Charsets.UTF_8) }
        when {
            exchange.requestMethod == "GET" && path == "/" -> {
                val page = PageServer::class.java.getResourceAsStream("/org/perinkpheral/cli/page.html")!!
                    .use { String(it.readBytes(), Charsets.UTF_8) }.replace("{{app}}", appDir?.path.orEmpty())
                answer(exchange, 200, page.toByteArray(), "text/html; charset=utf-8")
            }
            exchange.requestMethod == "GET" && path == "/api/dev/screen" ->
                passOn(exchange, path, expose = listOf("X-Frame", "X-Pending"))
            exchange.requestMethod == "GET" && path == "/api/log" ->
                passOn(exchange, path + (exchange.requestURI.rawQuery?.let { "?$it" } ?: ""), expose = listOf("X-Log-End"))
            exchange.requestMethod == "GET" && path == "/api/status" -> passOn(exchange, path)
            exchange.requestMethod == "POST" && path == "/api/dev/button" -> {
                if (query["button"].orEmpty() !in setOf("1", "2", "3", "4") || query["press"] !in setOf("short", "long")) {
                    answer(exchange, 400, "button=1..4&press=short|long".toByteArray())
                } else {
                    passOn(exchange, "$path?button=${query["button"]}&press=${query["press"]}", "POST")
                }
            }
            // A simulator's own requests, for a page that looks at one: a device answers them "not here".
            exchange.requestMethod == "POST" && path.startsWith("/api/sim/") ->
                passOn(exchange, path + (exchange.requestURI.rawQuery?.let { "?$it" } ?: ""), "POST")
            exchange.requestMethod == "POST" && path == "/run" && appDir != null -> {
                // The build's own words go back to the page, whatever they are.
                val said = ByteArrayOutputStream()
                val isDone = try {
                    run(appDir, PrintStream(said, true, Charsets.UTF_8))
                } catch (error: IOException) {
                    said.write((error.message ?: "no answer").toByteArray())
                    false
                }
                answer(exchange, if (isDone) 200 else 500, said.toByteArray())
            }
            else -> answer(exchange, 404, "Not found.".toByteArray())
        }
    }
}
