// SPDX-License-Identifier: MIT
package org.perinkpheral.core.simulator

import org.perinkpheral.core.Json
import java.io.IOException
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpConnectTimeoutException
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.net.http.HttpTimeoutException
import java.time.Duration
import java.util.Base64
import javax.net.ssl.SSLHandshakeException

/**
 * The simulator's network, in the process that started it - the command line, or the IDE:
 * the simulator connects to this port (`--fetcher-port`), asks a request a line and gets the
 * answer a line. Programs an IDE starts may not be let into the local network by the
 * system; the IDE itself is, and a user's own server lives there.
 *
 * One request is one exchange: a redirect is the answer, with where it leads resolved, and
 * the simulator decides whether to go there - by the firmware's own rules of hosts and
 * headers, which are C and are not written here again.
 *
 * Request:  {"method", "url", "headers": {name: value}, "body": base64, "max": bytes,
 *            "timeout": seconds}
 * Answer:   {"status": HTTP status, or a HOST_FAILED_* code, "headers": {name: value},
 *            "body": base64, "next": the absolute address a redirect leads to}
 */
object SimulatorNetwork {
    // sdk/host_api.h
    private const val NO_ANSWER = -2L
    private const val TIMEOUT = -5L
    private const val NOT_TRUSTED = -6L
    private const val TOO_LARGE = -7L
    private const val USER_AGENT = "perinkpheral" // what the device sends unless the application gives its own
    private val REDIRECTS = setOf(301, 302, 303, 307, 308)
    private val KEPT_HEADERS = setOf("content-type", "content-length", "etag", "last-modified", "location", "link",
        "retry-after")
    // What the HTTP client sets itself and refuses to be given.
    private val CLIENTS_OWN = setOf("host", "connection", "content-length", "expect", "upgrade")

    private val client: HttpClient by lazy {
        HttpClient.newBuilder().followRedirects(HttpClient.Redirect.NEVER).version(HttpClient.Version.HTTP_1_1)
            .connectTimeout(Duration.ofSeconds(15)).build()
    }

    private var server: ServerSocket? = null

    /** The port the simulator connects to; the server is started on first use. */
    @Synchronized
    fun port(): Int {
        server?.let { return it.localPort }
        val socket = ServerSocket(0, 4, InetAddress.getLoopbackAddress())
        server = socket
        val thread = Thread({
            while (!socket.isClosed) {
                val connection = try {
                    socket.accept()
                } catch (error: IOException) {
                    break
                }
                Thread({ serve(connection) }, "perINKpheral simulator request").apply { isDaemon = true }.start()
            }
        }, "perINKpheral simulator network")
        thread.isDaemon = true
        thread.start()
        return socket.localPort
    }

    private fun serve(connection: Socket) {
        connection.use {
            val reader = it.getInputStream().bufferedReader()
            val writer = it.getOutputStream().bufferedWriter()
            while (true) {
                val line = reader.readLine() ?: break
                val answer = try {
                    fetch(Json.parse(line) as Map<*, *>)
                } catch (error: Exception) { // an answer for every request, whatever went wrong
                    mapOf("status" to NO_ANSWER)
                }
                writer.write(Json.write(answer))
                writer.write("\n")
                writer.flush()
            }
        }
    }

    private fun fetch(asked: Map<*, *>): Map<String, Any?> {
        val method = asked["method"] as String
        val url = URI.create(asked["url"] as String)
        val body = Base64.getDecoder().decode((asked["body"] as? String).orEmpty())
        val headers = (asked["headers"] as? Map<*, *>).orEmpty()
        val most = (asked["max"] as? Long) ?: (256L * 1024)
        val timeout = Duration.ofSeconds((asked["timeout"] as? Long) ?: 15L)
        try {
            val request = HttpRequest.newBuilder(url).timeout(timeout)
                .method(method, if (body.isEmpty()) HttpRequest.BodyPublishers.noBody()
                                else HttpRequest.BodyPublishers.ofByteArray(body))
            var hasAgent = false
            for ((name, value) in headers) {
                name as String
                if (name.lowercase() in CLIENTS_OWN) continue
                hasAgent = hasAgent || name.equals("User-Agent", ignoreCase = true)
                request.header(name, value as String)
            }
            if (!hasAgent) request.header("User-Agent", USER_AGENT)
            val response = client.send(request.build(), HttpResponse.BodyHandlers.ofInputStream())
            val content = response.body().use { stream -> stream.readNBytes((most + 1).toInt()) }
            if (content.size > most) return mapOf("status" to TOO_LARGE)
            val kept = LinkedHashMap<String, Any?>()
            for ((name, values) in response.headers().map()) {
                if (name.lowercase() in KEPT_HEADERS && values.isNotEmpty()) kept[name.lowercase()] = values[0]
            }
            val answer = linkedMapOf<String, Any?>("status" to response.statusCode().toLong(), "headers" to kept,
                "body" to Base64.getEncoder().encodeToString(content))
            val location = response.headers().firstValue("location").orElse(null)
            if (response.statusCode() in REDIRECTS && location != null) {
                try {
                    answer["next"] = url.resolve(location).toString()
                } catch (error: IllegalArgumentException) {
                    // A Location that is no address: the redirect is the answer.
                }
            }
            return answer
        } catch (error: HttpConnectTimeoutException) {
            return mapOf("status" to TIMEOUT)
        } catch (error: HttpTimeoutException) {
            return mapOf("status" to TIMEOUT)
        } catch (error: SSLHandshakeException) {
            return mapOf("status" to NOT_TRUSTED)
        } catch (error: IOException) {
            return mapOf("status" to if (error.cause is SSLHandshakeException) NOT_TRUSTED else NO_ANSWER)
        } catch (error: IllegalArgumentException) {
            return mapOf("status" to NO_ANSWER)
        }
    }
}
